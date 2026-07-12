//This file is part of eMule AI
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//Copyright (C)2026 eMule AI
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "stdafx.h"
#include <math.h>
#include <algorithm>
#include "emule.h"
#include "PPgConnection.h"
#include "wizard.h"
#include "Scheduler.h"
#include "emuledlg.h"
#include "Preferences.h"
#include "Opcodes.h"
#include "StatisticsDlg.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "HelpIDs.h"
#include "Statistics.h"
#include "ListenSocket.h"
#include "ClientUDPSocket.h"
#include "ServerConnect.h"
#include "Log.h"
#include "LastCommonRouteFinder.h"
#include "PreferencesDlg.h"
#include "PPgWebServer.h"
#include "eMuleAI/NetBind.h"
#include "eMuleAI/IpGuard.h"
#include "eMuleAI/IPGeolocation.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
#ifndef GEO_ISO2
#define GEO_ISO2 0x00000004
#endif
#ifndef GEO_FRIENDLYNAME
#define GEO_FRIENDLYNAME 0x00000008
#endif
#ifndef GEOCLASS_NATION
#define GEOCLASS_NATION 16
#endif
	struct SVpnGuardCountryEntry
	{
		CString strCode;
		CString strName;
		int iFlagIndex;
	};

	std::vector<SVpnGuardCountryEntry>* g_pVpnGuardCountryEntries = NULL;

	BOOL CALLBACK EnumVpnGuardCountryGeoID(GEOID geoId)
	{
		if (g_pVpnGuardCountryEntries == NULL || theApp.ipgeolocation == NULL)
			return TRUE;

		TCHAR szCode[8] = {};
		if (::GetGeoInfo(geoId, GEO_ISO2, szCode, _countof(szCode), 0) <= 0)
			return TRUE;
		CString strCode(szCode);
		strCode.Trim();
		strCode.MakeUpper();
		if (strCode.GetLength() != 2)
			return TRUE;

		const int iFlagIndex = theApp.ipgeolocation->GetFlagIndexByCountryCode(strCode);
		if (iFlagIndex < 0)
			return TRUE;

		TCHAR szName[128] = {};
		if (::GetGeoInfo(geoId, GEO_FRIENDLYNAME, szName, _countof(szName), 0) <= 0)
			_sntprintf(szName, _countof(szName), _T("%s"), strCode.GetString());
		szName[_countof(szName) - 1] = _T('\0');

		CString strFallbackName(szName);
		strFallbackName.Trim();

		SVpnGuardCountryEntry entry;
		entry.strCode = strCode;
		entry.strName = CIPGeolocation::GetLocalizedCountryName(strCode, strFallbackName);
		entry.iFlagIndex = iFlagIndex;
		g_pVpnGuardCountryEntries->push_back(entry);
		return TRUE;
	}

	void BuildVpnGuardCountryEntries(std::vector<SVpnGuardCountryEntry>& entries)
	{
		entries.clear();
		if (theApp.ipgeolocation == NULL || !theApp.ipgeolocation->EnsureFlagsLoaded())
			return;

		g_pVpnGuardCountryEntries = &entries;
		::EnumSystemGeoID(GEOCLASS_NATION, 0, EnumVpnGuardCountryGeoID);
		g_pVpnGuardCountryEntries = NULL;

		std::sort(entries.begin(), entries.end(), [](const SVpnGuardCountryEntry& left, const SVpnGuardCountryEntry& right) {
			const int iNameCompare = left.strName.CompareNoCase(right.strName);
			return iNameCompare != 0 ? iNameCompare < 0 : left.strCode.CompareNoCase(right.strCode) < 0;
		});
		entries.erase(std::unique(entries.begin(), entries.end(), [](const SVpnGuardCountryEntry& left, const SVpnGuardCountryEntry& right) {
			return left.strCode.CompareNoCase(right.strCode) == 0;
		}), entries.end());
	}

	DWORD_PTR EncodeVpnGuardCountryCode(const CString& strCountryCode)
	{
		CString strCode(strCountryCode);
		strCode.Trim();
		strCode.MakeUpper();
		if (strCode.GetLength() != 2)
			return 0;
		return (static_cast<DWORD_PTR>(strCode[0]) << 8) | static_cast<DWORD_PTR>(strCode[1]);
	}

	CString DecodeVpnGuardCountryCode(DWORD_PTR dwCode)
	{
		if (dwCode == 0)
			return CString();
		TCHAR szCode[3] = { static_cast<TCHAR>((dwCode >> 8) & 0xff), static_cast<TCHAR>(dwCode & 0xff), _T('\0') };
		return CString(szCode);
	}
	uint32 MbitPerSecToKBytesPerSec(uint32 nMbitPerSec)
	{
		return (uint32)(((uint64)nMbitPerSec * 1000000ull + 4096ull) / 8192ull);
	}

	uint32 KBytesPerSecToRoundedMbitPerSec(uint32 nKBytesPerSec)
	{
		return (uint32)(((uint64)nKBytesPerSec * 8192ull + 500000ull) / 1000000ull);
	}

	double KBytesPerSecToMbitPerSec(uint32 nKBytesPerSec)
	{
		return (double)nKBytesPerSec * 8192.0 / 1000000.0;
	}

	CString FormatMbitPerSecValue(uint32 nKBytesPerSec)
	{
		const CString strUnit(GetResString(_T("MBITSSEC")));
		const double fMbitPerSec = KBytesPerSecToMbitPerSec(nKBytesPerSec);
		const double fRounded = floor(fMbitPerSec + 0.5);
		CString strValue;

		if (fabs(fMbitPerSec - fRounded) < 0.05)
			strValue.Format(_T("%.0f %s"), fMbitPerSec, (LPCTSTR)strUnit);
		else
			strValue.Format(_T("%.1f %s"), fMbitPerSec, (LPCTSTR)strUnit);
		return strValue;
	}

	CString FormatSpeedCapacityValue(uint32 nKBytesPerSec, bool bDisplayInKB)
	{
		CString strValue;
		if (bDisplayInKB)
			strValue.Format(_T("%u"), nKBytesPerSec);
		else
			strValue.Format(_T("%u"), KBytesPerSecToRoundedMbitPerSec(nKBytesPerSec));
		return strValue;
	}

	CString FormatSpeedLimitValue(uint32 nKBytesPerSec, bool bDisplayInKB)
	{
		if (!bDisplayInKB)
			return FormatMbitPerSecValue(nKBytesPerSec);

		CString strValue;
		strValue.Format(_T("%u %s"), nKBytesPerSec, (LPCTSTR)GetResString(_T("KBYTESPERSEC")));
		return strValue;
	}

	uint32 DisplayCapacityValueToKBytesPerSec(UINT nValue, bool bDisplayInKB)
	{
		return bDisplayInKB ? nValue : MbitPerSecToKBytesPerSec(nValue);
	}


	class CNetworkBindSocketCreationScope
	{
	public:
		CNetworkBindSocketCreationScope()
		{
			theApp.BeginNetworkBindSocketCreation();
		}

		~CNetworkBindSocketCreationScope()
		{
			theApp.EndNetworkBindSocketCreation();
		}
	};

	CString FormatBindResolveFailureForConnectionPage(ENetBindResolveResult eResult, const CString& strInterfaceId, const CString& strInterfaceName, const CString& strAddress)
	{
		CString strTarget;
		if (!strAddress.IsEmpty())
			strTarget = strAddress;
		else if (!strInterfaceName.IsEmpty())
			strTarget = strInterfaceName;
		else
			strTarget = strInterfaceId;

		CString strFormat;
		switch (eResult) {
		case NBR_InterfaceNotFound:
			strFormat = GetResString(_T("NETBIND_REASON_INTERFACE_NOT_FOUND"));
			break;
		case NBR_InterfaceNameAmbiguous:
			strFormat = GetResString(_T("NETBIND_REASON_INTERFACE_AMBIGUOUS"));
			break;
		case NBR_InterfaceHasNoAddress:
			strFormat = GetResString(_T("NETBIND_REASON_INTERFACE_NO_ADDRESS"));
			break;
		case NBR_AddressNotFoundOnInterface:
			strFormat = GetResString(_T("NETBIND_REASON_ADDRESS_NOT_ON_INTERFACE"));
			break;
		case NBR_AddressNotFound:
			strFormat = GetResString(_T("NETBIND_REASON_ADDRESS_NOT_FOUND"));
			break;
		case NBR_InvalidAddress:
			strFormat = GetResString(_T("NETBIND_REASON_INVALID_ADDRESS"));
			break;
		default:
			return GetResString(_T("NETBIND_REASON_UNKNOWN"));
		}

		CString strReason;
		if (!strTarget.IsEmpty())
			strReason.Format(strFormat, (LPCTSTR)strTarget);
		else
			strReason = GetResString(_T("NETBIND_REASON_UNKNOWN"));
		return strReason;
	}
}



IMPLEMENT_DYNAMIC(CPPgConnection, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgConnection, CPropertyPage)
	ON_BN_CLICKED(IDC_STARTTEST, OnStartPortTest)
	ON_EN_CHANGE(IDC_DOWNLOAD_CAP, OnCapacityChange)
	ON_EN_CHANGE(IDC_UPLOAD_CAP, OnCapacityChange)
	ON_BN_CLICKED(IDC_UDPDISABLE, OnEnChangeUDPDisable)
	ON_EN_CHANGE(IDC_UDPPORT, OnSettingsChange)
	ON_EN_CHANGE(IDC_PORT, OnSettingsChange)
	ON_EN_CHANGE(IDC_RANDOM_PORT_RANGE_START, OnSettingsChange)
	ON_EN_CHANGE(IDC_RANDOM_PORT_RANGE_END, OnSettingsChange)
	ON_EN_KILLFOCUS(IDC_UDPPORT, OnEnKillFocusUDP)
	ON_EN_KILLFOCUS(IDC_PORT, OnEnKillFocusTCP)
	ON_EN_CHANGE(IDC_MAXCON, OnSettingsChange)
	ON_EN_CHANGE(IDC_MAXSOURCEPERFILE, OnSettingsChange)
	ON_BN_CLICKED(IDC_AUTOCONNECT, OnSettingsChange)
	ON_BN_CLICKED(IDC_RECONN, OnSettingsChange)
	ON_BN_CLICKED(IDC_WIZARD, OnBnClickedWizard)
	ON_BN_CLICKED(IDC_NETWORK_ED2K, OnSettingsChange)
	ON_BN_CLICKED(IDC_SHOWOVERHEAD, OnSettingsChange)
	ON_BN_CLICKED(IDC_FORCE_SPEEDS_TO_KB, OnForceSpeedsToKBChange)
	ON_BN_CLICKED(IDC_ULIMIT_LBL, OnLimiterChange)
	ON_BN_CLICKED(IDC_DLIMIT_LBL, OnLimiterChange)
	ON_WM_HSCROLL()
	ON_BN_CLICKED(IDC_NETWORK_KADEMLIA, OnSettingsChange)
	ON_WM_HELPINFO()
	ON_BN_CLICKED(IDC_RANDOMIZE_PORTS_ON_STARTUP, OnRandomizePortsOnStartup)
	ON_BN_CLICKED(IDC_OPEN_PORTS_WINDOWS_FIREWALL, OnSettingsChange)
	ON_BN_CLICKED(IDC_PREF_UPNPONSTART, OnSettingsChange)
END_MESSAGE_MAP()

CPPgConnection::CPPgConnection()
	: CPropertyPage(CPPgConnection::IDD)
	, m_lastudp()
	, m_bUpdatingControls(false)
	, m_bDisplaySpeedsInKB(false)
	, m_nDisplayDownloadCapacityKBS()
	, m_nDisplayUploadCapacityKBS()
{
}


IMPLEMENT_DYNAMIC(CPPgNetworkInterface, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgNetworkInterface, CPropertyPage)
	ON_CBN_SELCHANGE(IDC_BIND_INTERFACE, OnBindSettingsChange)
	ON_EN_CHANGE(IDC_BIND_ADDRESS, OnBindSettingsChange)
	ON_BN_CLICKED(IDC_IP_GUARD_ENABLED, OnBindSettingsChange)
	ON_EN_CHANGE(IDC_IP_GUARD_RANGES, OnBindSettingsChange)
	ON_BN_CLICKED(IDC_VPN_GUARD_ENABLED, OnVpnGuardSettingsChange)
	ON_CBN_SELCHANGE(IDC_VPN_GUARD_COUNTRY, OnVpnGuardSettingsChange)
	ON_BN_CLICKED(IDC_VPN_GUARD_BLOCK_UNKNOWN, OnVpnGuardSettingsChange)
	ON_WM_HELPINFO()
END_MESSAGE_MAP()

CPPgNetworkInterface::CPPgNetworkInterface()
	: CPropertyPage(CPPgNetworkInterface::IDD)
	, m_bUpdatingControls(false)
{
}


void CPPgNetworkInterface::LoadBindableInterfaces()
{
	m_bindInterfaces = CNetBind::GetInterfaces();
}

void CPPgNetworkInterface::FillBindInterfaceCombo(bool bPreserveCurrentSelection)
{
	if (m_ctlBindInterface.GetSafeHwnd() == NULL)
		return;

	CString strSelectedInterfaceId;
	CString strSelectedInterfaceName;
	CString strUnusedAddress;
	if (bPreserveCurrentSelection)
		GetBindSelection(strSelectedInterfaceId, strSelectedInterfaceName, strUnusedAddress);
	if (!bPreserveCurrentSelection) {
		strSelectedInterfaceId = thePrefs.GetBindInterfaceId();
		strSelectedInterfaceName = thePrefs.GetBindInterfaceName();
	}

	m_ctlBindInterface.ResetContent();
	const int iAny = m_ctlBindInterface.AddString(GetResString(_T("NETBIND_ANY_INTERFACE")));
	m_ctlBindInterface.SetItemData(iAny, static_cast<DWORD_PTR>(-1));

	int iSelected = iAny;
	for (size_t i = 0; i < m_bindInterfaces.size(); ++i) {
		const int iItem = m_ctlBindInterface.AddString(m_bindInterfaces[i].strDisplayName);
		m_ctlBindInterface.SetItemData(iItem, static_cast<DWORD_PTR>(i));
		if ((!strSelectedInterfaceId.IsEmpty() && m_bindInterfaces[i].strId.CompareNoCase(strSelectedInterfaceId) == 0)
			|| (strSelectedInterfaceId.IsEmpty() && !strSelectedInterfaceName.IsEmpty() && m_bindInterfaces[i].strName.CompareNoCase(strSelectedInterfaceName) == 0))
			iSelected = iItem;
	}
	if (iSelected == iAny && (!strSelectedInterfaceId.IsEmpty() || !strSelectedInterfaceName.IsEmpty())) {
		CString strDisplay = !strSelectedInterfaceName.IsEmpty() ? strSelectedInterfaceName : strSelectedInterfaceId;
		CString strUnavailable;
		strUnavailable.Format(GetResString(_T("NETBIND_UNAVAILABLE_INTERFACE")), (LPCTSTR)strDisplay);
		const int iItem = m_ctlBindInterface.AddString(strUnavailable);
		m_ctlBindInterface.SetItemData(iItem, static_cast<DWORD_PTR>(-2));
		iSelected = iItem;
	}
	m_ctlBindInterface.SetCurSel(iSelected);
}

void CPPgNetworkInterface::GetBindSelection(CString& strInterfaceId, CString& strInterfaceName, CString& strAddress)
{
	strInterfaceId.Empty();
	strInterfaceName.Empty();
	GetDlgItemText(IDC_BIND_ADDRESS, strAddress);
	strAddress.Trim();

	if (m_ctlBindInterface.GetSafeHwnd() == NULL)
		return;

	const int iSel = m_ctlBindInterface.GetCurSel();
	if (iSel == CB_ERR)
		return;

	const DWORD_PTR dwData = m_ctlBindInterface.GetItemData(iSel);
	if (dwData == static_cast<DWORD_PTR>(-1))
		return;
	if (dwData == static_cast<DWORD_PTR>(-2)) {
		strInterfaceId = thePrefs.GetBindInterfaceId();
		strInterfaceName = thePrefs.GetBindInterfaceName();
		return;
	}
	if (dwData >= m_bindInterfaces.size())
		return;

	strInterfaceId = m_bindInterfaces[static_cast<size_t>(dwData)].strId;
	strInterfaceName = m_bindInterfaces[static_cast<size_t>(dwData)].strName;
}

void CPPgNetworkInterface::UpdateNetworkInterfaceTooltips()
{
	struct STooltipEntry
	{
		UINT nControlID;
		LPCTSTR pszKey;
	};

	static const STooltipEntry entries[] =
	{
		{ IDC_NETWORK_INTERFACE_FRM, _T("IP_GUARD_TIP_NETWORK_INTERFACE") },
		{ IDC_BIND_INTERFACE_LABEL, _T("IP_GUARD_TIP_BIND_INTERFACE") },
		{ IDC_BIND_INTERFACE, _T("IP_GUARD_TIP_BIND_INTERFACE") },
		{ IDC_BIND_ADDRESS_LABEL, _T("IP_GUARD_TIP_BIND_ADDRESS") },
		{ IDC_BIND_ADDRESS, _T("IP_GUARD_TIP_BIND_ADDRESS") },
		{ IDC_IP_GUARD_ENABLED, _T("IP_GUARD_TIP_ENABLE") },
		{ IDC_IP_GUARD_RANGES_LABEL, _T("IP_GUARD_TIP_ALLOWED_PUBLIC_IP_RANGES") },
		{ IDC_IP_GUARD_RANGES, _T("IP_GUARD_TIP_ALLOWED_PUBLIC_IP_RANGES") },
		{ IDC_BIND_STATUS, _T("IP_GUARD_TIP_ACTIVE_BIND") }
	};

	if (m_ToolTip.GetSafeHwnd() == NULL) {
		if (!m_ToolTip.Create(this))
			return;
		m_ToolTip.SetMaxTipWidth(420);
		for (int i = 0; i < static_cast<int>(_countof(entries)); ++i) {
			CWnd* pWnd = GetDlgItem(entries[i].nControlID);
			if (pWnd != NULL)
				m_ToolTip.AddTool(pWnd, GetResString(entries[i].pszKey));
		}
		m_ToolTip.Activate(TRUE);
	}
	else {
		for (int i = 0; i < static_cast<int>(_countof(entries)); ++i) {
			CWnd* pWnd = GetDlgItem(entries[i].nControlID);
			if (pWnd != NULL)
				m_ToolTip.UpdateTipText(GetResString(entries[i].pszKey), pWnd);
		}
	}
}

void CPPgNetworkInterface::UpdateIpGuardControls()
{
	CString strInterfaceId;
	CString strInterfaceName;
	CString strAddress;
	GetBindSelection(strInterfaceId, strInterfaceName, strAddress);
	const bool bHasTarget = CNetBind::HasExplicitSelection(strInterfaceId, strInterfaceName, strAddress);

	if (!bHasTarget)
		CheckDlgButton(IDC_IP_GUARD_ENABLED, 0);
	const bool bGuardEnabled = bHasTarget && IsDlgButtonChecked(IDC_IP_GUARD_ENABLED) != 0;
	GetDlgItem(IDC_IP_GUARD_ENABLED)->EnableWindow(bHasTarget);
	GetDlgItem(IDC_IP_GUARD_RANGES_LABEL)->EnableWindow(bGuardEnabled);
	GetDlgItem(IDC_IP_GUARD_RANGES)->EnableWindow(bGuardEnabled);
}

void CPPgNetworkInterface::FillVpnGuardCountryCombo(bool bPreserveCurrentSelection)
{
	if (m_ctlVpnGuardCountry.GetSafeHwnd() == NULL)
		return;

	CString strSelectedCountryCode;
	if (bPreserveCurrentSelection)
		strSelectedCountryCode = GetVpnGuardCountrySelection();
	else
		strSelectedCountryCode = thePrefs.GetVpnGuardCountryCode();
	strSelectedCountryCode.Trim();
	strSelectedCountryCode.MakeUpper();

	m_ctlVpnGuardCountry.ResetContent();
	if (theApp.ipgeolocation != NULL)
		m_ctlVpnGuardCountry.SetImageList(theApp.ipgeolocation->GetFlagImageList());

	int iSelected = m_ctlVpnGuardCountry.AddItem(_T(""), -1);
	m_ctlVpnGuardCountry.SetItemData(iSelected, 0);

	std::vector<SVpnGuardCountryEntry> entries;
	BuildVpnGuardCountryEntries(entries);
	for (size_t i = 0; i < entries.size(); ++i) {
		const CString strText(CIPGeolocation::FormatLocalizedCountryNameAndCode(entries[i].strCode, entries[i].strName));
		const int iItem = m_ctlVpnGuardCountry.AddItem(strText, entries[i].iFlagIndex);
		m_ctlVpnGuardCountry.SetItemData(iItem, EncodeVpnGuardCountryCode(entries[i].strCode));
		if (!strSelectedCountryCode.IsEmpty() && entries[i].strCode.CompareNoCase(strSelectedCountryCode) == 0)
			iSelected = iItem;
	}
	m_ctlVpnGuardCountry.SetCurSel(iSelected);
	CComboBox* pCombo = m_ctlVpnGuardCountry.GetComboBoxCtrl();
	if (pCombo != NULL)
		UpdateHorzExtent(*pCombo, FLAG_WIDTH);
}

CString CPPgNetworkInterface::GetVpnGuardCountrySelection() const
{
	if (m_ctlVpnGuardCountry.GetSafeHwnd() == NULL)
		return CString();
	const int iSel = m_ctlVpnGuardCountry.GetCurSel();
	if (iSel == CB_ERR)
		return CString();
	return DecodeVpnGuardCountryCode(m_ctlVpnGuardCountry.GetItemData(iSel));
}

void CPPgNetworkInterface::SetVpnGuardCountrySelection(const CString& strCountryCode)
{
	if (m_ctlVpnGuardCountry.GetSafeHwnd() == NULL)
		return;
	const DWORD_PTR dwWantedCode = EncodeVpnGuardCountryCode(strCountryCode);
	CComboBox* pCombo = m_ctlVpnGuardCountry.GetComboBoxCtrl();
	const int iCount = pCombo != NULL ? pCombo->GetCount() : 0;
	for (int i = 0; i < iCount; ++i) {
		if (m_ctlVpnGuardCountry.GetItemData(i) == dwWantedCode) {
			m_ctlVpnGuardCountry.SetCurSel(i);
			return;
		}
	}
	m_ctlVpnGuardCountry.SetCurSel(0);
}

void CPPgNetworkInterface::UpdateVpnGuardControls()
{
	const bool bEnabled = IsDlgButtonChecked(IDC_VPN_GUARD_ENABLED) != 0;
	GetDlgItem(IDC_VPN_GUARD_COUNTRY_LABEL)->EnableWindow(bEnabled);
	GetDlgItem(IDC_VPN_GUARD_COUNTRY)->EnableWindow(bEnabled);
	GetDlgItem(IDC_VPN_GUARD_BLOCK_UNKNOWN)->EnableWindow(bEnabled);
}

void CPPgNetworkInterface::UpdateBindStatus()
{
	CString strStatus;
	if (thePrefs.GetActiveBindResolveResult() == NBR_Resolved) {
		CString strTarget;
		if (!thePrefs.GetActiveBindInterfaceName().IsEmpty())
			strTarget = thePrefs.GetActiveBindInterfaceName();
		if (thePrefs.GetP2PBindAddrW() != NULL) {
			if (!strTarget.IsEmpty())
				strTarget += _T(" / ");
			strTarget += thePrefs.GetP2PBindAddrW();
		}
		if (strTarget.IsEmpty())
			strTarget = GetResString(_T("NETBIND_ANY_INTERFACE"));
		strStatus.Format(GetResString(_T("NETBIND_ACTIVE_INTERFACE")), (LPCTSTR)strTarget);
	}
	else {
		strStatus.Format(GetResString(_T("NETBIND_ACTIVE_INTERFACE")), (LPCTSTR)GetResString(_T("NETBIND_ANY_INTERFACE")));
	}
	SetDlgItemText(IDC_BIND_STATUS, strStatus);
}

void CPPgNetworkInterface::DoDataExchange(CDataExchange *pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_BIND_INTERFACE, m_ctlBindInterface);
	DDX_Control(pDX, IDC_VPN_GUARD_COUNTRY, m_ctlVpnGuardCountry);
}

BOOL CPPgNetworkInterface::OnInitDialog()
{
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);

	static_cast<CEdit*>(GetDlgItem(IDC_BIND_ADDRESS))->SetLimitText(128);

	LoadSettings();
	Localize();

	return TRUE;
}

void CPPgNetworkInterface::LoadSettings()
{
	if (m_hWnd) {
		m_bUpdatingControls = true;
		LoadBindableInterfaces();
		FillBindInterfaceCombo();
		SetDlgItemText(IDC_BIND_ADDRESS, thePrefs.GetConfiguredBindAddr());
		CheckDlgButton(IDC_IP_GUARD_ENABLED, static_cast<UINT>(thePrefs.IsIpGuardEnabled()));
		SetDlgItemText(IDC_IP_GUARD_RANGES, thePrefs.GetIpGuardAllowedPublicIpRanges());
		FillVpnGuardCountryCombo();
		CheckDlgButton(IDC_VPN_GUARD_ENABLED, static_cast<UINT>(thePrefs.IsVpnGuardEnabled()));
		SetVpnGuardCountrySelection(thePrefs.GetVpnGuardCountryCode());
		CheckDlgButton(IDC_VPN_GUARD_BLOCK_UNKNOWN, static_cast<UINT>(thePrefs.IsVpnGuardBlockUnknownCountryEnabled()));
		UpdateIpGuardControls();
		UpdateVpnGuardControls();
		UpdateBindStatus();
		m_bUpdatingControls = false;
	}
}

BOOL CPPgNetworkInterface::OnApply()
{
	CString strBindInterfaceId;
	CString strBindInterfaceName;
	CString strBindAddress;
	GetBindSelection(strBindInterfaceId, strBindInterfaceName, strBindAddress);
	const bool bHasBindTarget = CNetBind::HasExplicitSelection(strBindInterfaceId, strBindInterfaceName, strBindAddress);
	if (bHasBindTarget) {
		SNetBindResolution bindResolution;
		const ENetBindResolveResult eBindResult = CNetBind::Resolve(strBindInterfaceId, strBindInterfaceName, strBindAddress, bindResolution);
		if (eBindResult != NBR_Resolved || bindResolution.strResolvedAddress.IsEmpty()) {
			CString strMessage;
			strMessage.Format(GetResString(_T("NETBIND_UNAVAILABLE_FMT")), (LPCTSTR)FormatBindResolveFailureForConnectionPage(eBindResult, strBindInterfaceId, strBindInterfaceName, strBindAddress));
			CDarkMode::MessageBox(strMessage, MB_OK | MB_ICONERROR);
			if (!strBindAddress.IsEmpty())
				GetDlgItem(IDC_BIND_ADDRESS)->SetFocus();
			else
				m_ctlBindInterface.SetFocus();
			return FALSE;
		}
	}
	const EIpGuardMode eIpGuardMode = bHasBindTarget && (IsDlgButtonChecked(IDC_IP_GUARD_ENABLED) != 0) ? IpGuardModeBlock : IpGuardModeOff;
	CString strIpGuardRanges;
	GetDlgItemText(IDC_IP_GUARD_RANGES, strIpGuardRanges);
	strIpGuardRanges.Trim();
	if (eIpGuardMode == IpGuardModeBlock) {
		std::vector<SIpGuardAllowedPublicIpRange> ranges;
		CString strError;
		if (!CIpGuard::TryParseAllowedPublicIpRanges(strIpGuardRanges, ranges, strError)) {
			CDarkMode::MessageBox(GetResString(_T("IP_GUARD_INVALID_RANGES")), MB_OK | MB_ICONERROR);
			GetDlgItem(IDC_IP_GUARD_RANGES)->SetFocus();
			return FALSE;
		}
	}

	const bool bVpnGuardEnabled = IsDlgButtonChecked(IDC_VPN_GUARD_ENABLED) != 0;
	CString strVpnGuardCountryCode = GetVpnGuardCountrySelection();
	if (bVpnGuardEnabled && strVpnGuardCountryCode.IsEmpty()) {
		CDarkMode::MessageBox(GetResString(_T("VPN_GUARD_SELECT_COUNTRY")), MB_OK | MB_ICONERROR);
		m_ctlVpnGuardCountry.SetFocus();
		return FALSE;
	}
	const bool bVpnGuardBlockUnknownCountry = IsDlgButtonChecked(IDC_VPN_GUARD_BLOCK_UNKNOWN) != 0;

	const bool bVpnGuardSettingsChanged = bVpnGuardEnabled != thePrefs.IsVpnGuardEnabled()
		|| strVpnGuardCountryCode.CompareNoCase(thePrefs.GetVpnGuardCountryCode()) != 0
		|| bVpnGuardBlockUnknownCountry != thePrefs.IsVpnGuardBlockUnknownCountryEnabled();

	const bool bBindSettingsChanged = strBindInterfaceId.CompareNoCase(thePrefs.GetBindInterfaceId()) != 0
		|| strBindInterfaceName.CompareNoCase(thePrefs.GetBindInterfaceName()) != 0
		|| strBindAddress.CompareNoCase(thePrefs.GetConfiguredBindAddr()) != 0
		|| eIpGuardMode != thePrefs.GetIpGuardMode()
		|| strIpGuardRanges.CompareNoCase(thePrefs.GetIpGuardAllowedPublicIpRanges()) != 0;
	const bool bVpnGuardBlockWasActive = bBindSettingsChanged && theApp.emuledlg != NULL && theApp.emuledlg->IsVpnGuardNetworkBlockActive();
	bool bVpnGuardSettingsApplied = false;
	if (bVpnGuardSettingsChanged && !bVpnGuardEnabled) {
		thePrefs.SetVpnGuardEnabled(false);
		thePrefs.SetVpnGuardCountryCode(strVpnGuardCountryCode);
		thePrefs.SetVpnGuardBlockUnknownCountry(bVpnGuardBlockUnknownCountry);
		bVpnGuardSettingsApplied = true;
		if (theApp.emuledlg != NULL && !bBindSettingsChanged)
			theApp.emuledlg->UpdateVpnGuardMonitor(false);
	}

	if (bBindSettingsChanged) {
		const CString strOldBindAddr(thePrefs.GetP2PBindAddrW() != NULL ? thePrefs.GetP2PBindAddrW() : _T(""));
		const CString strOldBindInterfaceId(thePrefs.GetBindInterfaceId());
		const CString strOldBindInterfaceName(thePrefs.GetBindInterfaceName());
		const CString strOldBindAddress(thePrefs.GetConfiguredBindAddr());
		const EIpGuardMode eOldIpGuardMode = thePrefs.GetIpGuardMode();
		const CString strOldIpGuardRanges(thePrefs.GetIpGuardAllowedPublicIpRanges());
		const bool bPendingPortRestart = !thePrefs.IsPortRandomizationOnStartupEnabled()
			&& ((theApp.listensocket != NULL && theApp.listensocket->GetConnectedPort() != thePrefs.GetPort())
				|| (theApp.clientudp != NULL && theApp.clientudp->GetConnectedPort() != thePrefs.GetUDPPort()));
		if (theApp.emuledlg != NULL && !bVpnGuardBlockWasActive) {
			theApp.emuledlg->ClearIpGuardNetworkBlock(false);
			if (!bVpnGuardSettingsApplied)
				theApp.emuledlg->ClearVpnGuardNetworkBlock(false);
		}
		thePrefs.SetBindSelection(strBindInterfaceId, strBindInterfaceName, strBindAddress);
		thePrefs.SetIpGuardMode(eIpGuardMode);
		thePrefs.SetIpGuardAllowedPublicIpRanges(strIpGuardRanges);
		const CString strNewBindAddr(thePrefs.GetP2PBindAddrW() != NULL ? thePrefs.GetP2PBindAddrW() : _T(""));
		if (strOldBindAddr.CompareNoCase(strNewBindAddr) != 0 && !bVpnGuardBlockWasActive) {
			CNetworkBindSocketCreationScope allowSocketCreation;
			bool bListenSocketTouched = false;
			bool bClientUDPSocketTouched = false;
			bool bServerUDPSocketTouched = false;
			bool bBindChangeFailed = false;
			bool bRestoreEd2k = false;
			bool bRestoreKad = false;
			bool bConnectionSuspended = false;

			if (!bPendingPortRestart) {
				if (theApp.emuledlg != NULL) {
					theApp.emuledlg->StopConnectionForNetworkBindChange(bRestoreEd2k, bRestoreKad);
					bRestoreEd2k = bRestoreEd2k && thePrefs.GetNetworkED2K();
					bRestoreKad = bRestoreKad && thePrefs.GetNetworkKademlia();
					bConnectionSuspended = bRestoreEd2k || bRestoreKad;
				}

				theApp.listensocket->DisconnectAllSockets(_T("Network bind change"));

				bListenSocketTouched = true;
				if (!theApp.listensocket->RecreateListeningSocket())
					bBindChangeFailed = true;

				if (!bBindChangeFailed) {
					bClientUDPSocketTouched = true;
					if (!theApp.clientudp->Recreate())
						bBindChangeFailed = true;
				}

				if (!bBindChangeFailed && theApp.serverconnect != NULL) {
					bServerUDPSocketTouched = true;
					if (!theApp.serverconnect->RecreateServerUDPSocket())
						bBindChangeFailed = true;
				}
			}
			else
				bBindChangeFailed = true;

			if (bBindChangeFailed) {
				bool bRollbackFailed = false;
				thePrefs.SetBindSelection(strOldBindInterfaceId, strOldBindInterfaceName, strOldBindAddress);
				thePrefs.SetIpGuardMode(eOldIpGuardMode);
				thePrefs.SetIpGuardAllowedPublicIpRanges(strOldIpGuardRanges);
				if (bServerUDPSocketTouched && theApp.serverconnect != NULL && !theApp.serverconnect->RecreateServerUDPSocket()) {
					bRollbackFailed = true;
					TRACE(_T("Failed to restore server UDP socket after network bind rollback.\n"));
				}
				if (bClientUDPSocketTouched && !theApp.clientudp->Recreate()) {
					bRollbackFailed = true;
					TRACE(_T("Failed to restore client UDP socket after network bind rollback.\n"));
				}
				if (bListenSocketTouched && !theApp.listensocket->RecreateListeningSocket()) {
					bRollbackFailed = true;
					TRACE(_T("Failed to restore listen socket after network bind rollback.\n"));
				}
				if (bConnectionSuspended && !bRollbackFailed && theApp.emuledlg != NULL)
					theApp.emuledlg->RestoreConnectionAfterNetworkBindChange(bRestoreEd2k, bRestoreKad);
				LogWarning(LOG_STATUSBAR, _T("%s"), (LPCTSTR)GetResString(_T("NETBIND_REBIND_FAILED_RESTORED")));
			}
			else if (bConnectionSuspended && theApp.emuledlg != NULL)
				theApp.emuledlg->RestoreConnectionAfterNetworkBindChange(bRestoreEd2k, bRestoreKad);
		}
		if (bVpnGuardSettingsChanged && !bVpnGuardSettingsApplied) {
			thePrefs.SetVpnGuardEnabled(bVpnGuardEnabled);
			thePrefs.SetVpnGuardCountryCode(strVpnGuardCountryCode);
			thePrefs.SetVpnGuardBlockUnknownCountry(bVpnGuardBlockUnknownCountry);
			bVpnGuardSettingsApplied = true;
		}
		if (theApp.emuledlg != NULL) {
			if (thePrefs.IsVpnGuardEnabled()) {
				if (thePrefs.IsIpGuardEnabled())
					theApp.emuledlg->UpdateIpGuardMonitor();
				else
					theApp.emuledlg->ClearIpGuardNetworkBlock(false);
				theApp.emuledlg->UpdateVpnGuardMonitor(true);
			}
			else if (bVpnGuardBlockWasActive || bVpnGuardSettingsChanged) {
				theApp.emuledlg->UpdateVpnGuardMonitor(false);
				if (thePrefs.IsIpGuardEnabled())
					theApp.emuledlg->UpdateIpGuardMonitor(false);
			}
			else
				theApp.emuledlg->UpdateIpGuardMonitor();
		}
	}

	if (bVpnGuardSettingsChanged && !bVpnGuardSettingsApplied) {
		thePrefs.SetVpnGuardEnabled(bVpnGuardEnabled);
		thePrefs.SetVpnGuardCountryCode(strVpnGuardCountryCode);
		thePrefs.SetVpnGuardBlockUnknownCountry(bVpnGuardBlockUnknownCountry);
		if (theApp.emuledlg != NULL)
			theApp.emuledlg->UpdateVpnGuardMonitor();
	}

	SetModified(FALSE);
	LoadSettings();

	return CPropertyPage::OnApply();
}

void CPPgNetworkInterface::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(_T("NETBIND_NETWORK_INTERFACE")));
		SetDlgItemText(IDC_NETWORK_INTERFACE_FRM, GetResString(_T("NETBIND_NETWORK_INTERFACE")));
		SetDlgItemText(IDC_BIND_INTERFACE_LABEL, GetResString(_T("NETBIND_INTERFACE")));
		FillBindInterfaceCombo(true);
		SetDlgItemText(IDC_BIND_ADDRESS_LABEL, GetResString(_T("NETBIND_ADDRESS")));
		SetDlgItemText(IDC_IP_GUARD_ENABLED, GetResString(_T("IP_GUARD")));
		SetDlgItemText(IDC_IP_GUARD_RANGES_LABEL, GetResString(_T("IP_GUARD_ALLOWED_PUBLIC_IP_RANGES")));
		SetDlgItemText(IDC_VPN_GUARD_FRM, GetResString(_T("VPN_GUARD")));
		SetDlgItemText(IDC_VPN_GUARD_ENABLED, GetResString(_T("VPN_GUARD_BLOCK_COUNTRY")));
		SetDlgItemText(IDC_VPN_GUARD_COUNTRY_LABEL, GetResString(_T("VPN_GUARD_COUNTRY")));
		FillVpnGuardCountryCombo(true);
		SetDlgItemText(IDC_VPN_GUARD_BLOCK_UNKNOWN, GetResString(_T("VPN_GUARD_BLOCK_UNKNOWN")));
		UpdateNetworkInterfaceTooltips();
		UpdateBindStatus();
	}
}

void CPPgConnection::UpdateRandomPortRangeControls()
{
	const bool bEnabled = IsDlgButtonChecked(IDC_RANDOMIZE_PORTS_ON_STARTUP) != 0;
	GetDlgItem(IDC_RANDOM_PORT_RANGE_FRM)->EnableWindow(bEnabled);
	GetDlgItem(IDC_RANDOM_PORT_RANGE_START_LABEL)->EnableWindow(bEnabled);
	GetDlgItem(IDC_RANDOM_PORT_RANGE_START)->EnableWindow(bEnabled);
	GetDlgItem(IDC_RANDOM_PORT_RANGE_END_LABEL)->EnableWindow(bEnabled);
	GetDlgItem(IDC_RANDOM_PORT_RANGE_END)->EnableWindow(bEnabled);
}

uint16 CPPgConnection::GetActiveTCPPortForDisplay() const
{
	const uint16 nActivePort = theApp.listensocket != NULL ? theApp.listensocket->GetConnectedPort() : 0;
	return nActivePort != 0 ? nActivePort : thePrefs.GetPort();
}

uint16 CPPgConnection::GetActiveUDPPortForDisplay() const
{
	const uint16 nActivePort = theApp.clientudp != NULL ? theApp.clientudp->GetConnectedPort() : 0;
	return nActivePort != 0 ? nActivePort : thePrefs.GetUDPPort();
}

void CPPgConnection::UpdatePortControlsForRandomization(bool bKeepCurrentPorts)
{
	const bool bRandomizePortsOnStartup = IsDlgButtonChecked(IDC_RANDOMIZE_PORTS_ON_STARTUP) != 0;
	const bool bUDPDisabled = IsDlgButtonChecked(IDC_UDPDISABLE) != 0;

	if (bRandomizePortsOnStartup) {
		SetDlgItemInt(IDC_PORT, GetActiveTCPPortForDisplay(), FALSE);
		SetDlgItemInt(IDC_UDPPORT, bUDPDisabled ? 0 : GetActiveUDPPortForDisplay(), FALSE);
	}
	else if (!bKeepCurrentPorts) {
		SetDlgItemInt(IDC_PORT, thePrefs.GetConfiguredPort(), FALSE);
		SetDlgItemInt(IDC_UDPPORT, bUDPDisabled ? 0 : thePrefs.GetConfiguredUDPPort(), FALSE);
	}

	GetDlgItem(IDC_PORT)->EnableWindow(!bRandomizePortsOnStartup);
	GetDlgItem(IDC_UDPPORT)->EnableWindow(!bRandomizePortsOnStartup && !bUDPDisabled);
}

bool CPPgConnection::GetRandomPortRangeFromControls(UINT& rnStart, UINT& rnEnd, bool bShowError)
{
	BOOL bStartOk = FALSE;
	BOOL bEndOk = FALSE;
	rnStart = GetDlgItemInt(IDC_RANDOM_PORT_RANGE_START, &bStartOk, FALSE);
	rnEnd = GetDlgItemInt(IDC_RANDOM_PORT_RANGE_END, &bEndOk, FALSE);
	if (bStartOk && bEndOk && CPreferences::IsValidRandomPortRange(rnStart, rnEnd))
		return true;

	if (bShowError) {
		CDarkMode::MessageBox(GetResString(_T("RANDOM_PORT_RANGE_INVALID")), MB_OK | MB_ICONERROR);
		UINT nFocusID = IDC_RANDOM_PORT_RANGE_START;
		if (bStartOk && rnStart >= 1 && rnStart <= _UI16_MAX)
			nFocusID = IDC_RANDOM_PORT_RANGE_END;
		CWnd* pFocusWnd = GetDlgItem(nFocusID);
		if (pFocusWnd != NULL) {
			pFocusWnd->SetFocus();
			static_cast<CEdit*>(pFocusWnd)->SetSel(0, -1);
		}
	}
	return false;
}

void CPPgConnection::DoDataExchange(CDataExchange *pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MAXDOWN_SLIDER, m_ctlMaxDown);
	DDX_Control(pDX, IDC_MAXUP_SLIDER, m_ctlMaxUp);
}

void CPPgConnection::OnEnKillFocusTCP()
{
	ChangePorts(1);
}

void CPPgConnection::OnEnKillFocusUDP()
{
	ChangePorts(0);
}

void CPPgConnection::ChangePorts(uint8 iWhat)
{
	UINT tcp = GetDlgItemInt(IDC_PORT, NULL, FALSE);
	UINT udp = GetDlgItemInt(IDC_UDPPORT, NULL, FALSE);

	const uint16 nActiveTCPPort = theApp.listensocket->GetConnectedPort();
	const uint16 nActiveUDPPort = theApp.clientudp->GetConnectedPort();
	const bool bRandomizePortsOnStartup = IsDlgButtonChecked(IDC_RANDOMIZE_PORTS_ON_STARTUP) != 0;
	const bool bPortsMatch = bRandomizePortsOnStartup ? (tcp == nActiveTCPPort && udp == nActiveUDPPort) : (tcp == thePrefs.GetConfiguredPort() && udp == thePrefs.GetConfiguredUDPPort());
	GetDlgItem(IDC_STARTTEST)->EnableWindow(bPortsMatch && nActiveTCPPort != 0 && nActiveUDPPort == thePrefs.GetUDPPort());

	if (iWhat == 0) //UDP
		ChangeUDP();
	else if (iWhat == 1) //TCP
		if (!bRandomizePortsOnStartup && (tcp != thePrefs.GetConfiguredPort() || udp != thePrefs.GetConfiguredUDPPort()))
			OnSettingsChange();
}

bool CPPgConnection::ChangeUDP()
{
	const bool bDisabled = IsDlgButtonChecked(IDC_UDPDISABLE) != 0;
	const bool bRandomizePortsOnStartup = IsDlgButtonChecked(IDC_RANDOMIZE_PORTS_ON_STARTUP) != 0;
	GetDlgItem(IDC_UDPPORT)->EnableWindow(!bDisabled && !bRandomizePortsOnStartup);

	uint16 newVal, oldVal = (uint16)GetDlgItemInt(IDC_UDPPORT, NULL, FALSE);
	if (!bRandomizePortsOnStartup && oldVal)
		m_lastudp = oldVal;
	if (bDisabled)
		newVal = 0;
	else if (bRandomizePortsOnStartup)
		newVal = GetActiveUDPPortForDisplay();
	else
		newVal = m_lastudp ? m_lastudp : (10ui16 + thePrefs.GetConfiguredPort());
	if (newVal != oldVal)
		SetDlgItemInt(IDC_UDPPORT, newVal, FALSE);
	return bDisabled;
}

void CPPgConnection::OnEnChangeUDPDisable()
{
	SetModified();
	bool bDisabled = ChangeUDP();
	CheckDlgButton(IDC_NETWORK_KADEMLIA, static_cast<UINT>(thePrefs.networkkademlia && !bDisabled)); // don't use GetNetworkKademlia here
	GetDlgItem(IDC_NETWORK_KADEMLIA)->EnableWindow(!bDisabled);
}

BOOL CPPgConnection::OnInitDialog()
{
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);

	static_cast<CEdit*>(GetDlgItem(IDC_PORT))->SetLimitText(5);
	static_cast<CEdit*>(GetDlgItem(IDC_UDPPORT))->SetLimitText(5);
	static_cast<CEdit*>(GetDlgItem(IDC_RANDOM_PORT_RANGE_START))->SetLimitText(5);
	static_cast<CEdit*>(GetDlgItem(IDC_RANDOM_PORT_RANGE_END))->SetLimitText(5);

	LoadSettings();
	Localize();

	ChangePorts(2); //"Test ports" button enable/disable

	return TRUE;  // return TRUE unless you set the focus to the control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

void CPPgConnection::LoadSettings()
{
	if (m_hWnd) {
		m_bUpdatingControls = true;
		if (thePrefs.m_maxupload != 0)
			thePrefs.m_maxdownload = thePrefs.GetMaxDownload();
		m_lastudp = thePrefs.GetConfiguredUDPPort();
		CheckDlgButton(IDC_UDPDISABLE, !m_lastudp); //before the port number!
		SetDlgItemInt(IDC_UDPPORT, m_lastudp, FALSE);

		m_nDisplayDownloadCapacityKBS = thePrefs.maxGraphDownloadRate;
		m_nDisplayUploadCapacityKBS = thePrefs.maxGraphUploadRate != UNLIMITED ? thePrefs.maxGraphUploadRate : 0;
		m_bDisplaySpeedsInKB = thePrefs.GetForceSpeedsToKB();
		CheckDlgButton(IDC_FORCE_SPEEDS_TO_KB, static_cast<UINT>(m_bDisplaySpeedsInKB));
		UpdateSpeedDisplayUnitControls();

		m_ctlMaxDown.SetRange(1, thePrefs.maxGraphDownloadRate);
		SetRateSliderTicks(m_ctlMaxDown);

		m_ctlMaxUp.SetRange(1, thePrefs.GetMaxGraphUploadRate(true));

		SetRateSliderTicks(m_ctlMaxUp);

		uint32 up = thePrefs.m_maxupload;
		uint32 dn = thePrefs.m_maxdownload;
		const bool bDownloadLimitEnabled = (dn != UNLIMITED);
		const bool bUploadLimitEnabled = (up != UNLIMITED);
		CheckDlgButton(IDC_DLIMIT_LBL, static_cast<UINT>(bDownloadLimitEnabled));
		CheckDlgButton(IDC_ULIMIT_LBL, static_cast<UINT>(bUploadLimitEnabled));
		if (!bDownloadLimitEnabled)
			dn = thePrefs.GetLastMaxDownload();
		if (!bUploadLimitEnabled)
			up = thePrefs.GetLastMaxUpload();
		CheckUp(up, dn);
		CheckDown(up, dn);
		m_ctlMaxDown.SetPos(dn);
		m_ctlMaxUp.SetPos(up);

		SetDlgItemInt(IDC_PORT, thePrefs.GetConfiguredPort(), FALSE);
		SetDlgItemInt(IDC_MAXCON, thePrefs.maxconnections);
		SetDlgItemInt(IDC_MAXSOURCEPERFILE, (thePrefs.maxsourceperfile == 0xffff ? 0 : thePrefs.maxsourceperfile));

		CheckDlgButton(IDC_RECONN, static_cast<UINT>(thePrefs.reconnect));
		CheckDlgButton(IDC_SHOWOVERHEAD, static_cast<UINT>(thePrefs.m_bshowoverhead));
		CheckDlgButton(IDC_AUTOCONNECT, static_cast<UINT>(thePrefs.autoconnect));
		CheckDlgButton(IDC_NETWORK_KADEMLIA, static_cast<UINT>(thePrefs.GetNetworkKademlia()));
		GetDlgItem(IDC_NETWORK_KADEMLIA)->EnableWindow(thePrefs.GetUDPPort() > 0);
		CheckDlgButton(IDC_NETWORK_ED2K, static_cast<UINT>(thePrefs.networked2k));

		const WORD wv = thePrefs.GetWindowsVersion();
		GetDlgItem(IDC_PREF_UPNPONSTART)->EnableWindow(wv != _WINVER_95_ && wv != _WINVER_98_ && wv != _WINVER_NT4_);

		CheckDlgButton(IDC_PREF_UPNPONSTART, static_cast<UINT>(thePrefs.IsUPnPEnabled()));

		CheckDlgButton(IDC_RANDOMIZE_PORTS_ON_STARTUP, static_cast<UINT>(thePrefs.IsPortRandomizationOnStartupEnabled()));
		SetDlgItemInt(IDC_RANDOM_PORT_RANGE_START, thePrefs.GetRandomPortRangeStart(), FALSE);
		SetDlgItemInt(IDC_RANDOM_PORT_RANGE_END, thePrefs.GetRandomPortRangeEnd(), FALSE);
		CheckDlgButton(IDC_OPEN_PORTS_WINDOWS_FIREWALL, static_cast<UINT>(thePrefs.IsOpenListenPortsInWindowsFirewallEnabled()));
		UpdateRandomPortRangeControls();
		UpdatePortControlsForRandomization(false);

		m_bUpdatingControls = false;
		OnLimiterChange();
	}
}

BOOL CPPgConnection::OnApply()
{
	if (m_nDisplayDownloadCapacityKBS >= UNLIMITED) {
		GetDlgItem(IDC_DOWNLOAD_CAP)->SetFocus();
		return FALSE;
	}
	if (m_nDisplayUploadCapacityKBS >= UNLIMITED) {
		GetDlgItem(IDC_UPLOAD_CAP)->SetFocus();
		return FALSE;
	}
	UINT u = 0;
	const bool bRandomizePortsOnStartup = IsDlgButtonChecked(IDC_RANDOMIZE_PORTS_ON_STARTUP) != 0;
	UINT nRandomPortRangeStart = CPreferences::GetDefaultRandomPortRangeStart();
	UINT nRandomPortRangeEnd = CPreferences::GetDefaultRandomPortRangeEnd();
	if (!GetRandomPortRangeFromControls(nRandomPortRangeStart, nRandomPortRangeEnd, bRandomizePortsOnStartup)) {
		if (bRandomizePortsOnStartup)
			return FALSE;
		nRandomPortRangeStart = CPreferences::GetDefaultRandomPortRangeStart();
		nRandomPortRangeEnd = CPreferences::GetDefaultRandomPortRangeEnd();
	}

	const uint32 nDownloadCapacity = m_nDisplayDownloadCapacityKBS;
	const uint32 nUploadCapacity = m_nDisplayUploadCapacityKBS;

	uint32 lastmaxgu = thePrefs.maxGraphUploadRate; //save the values
	uint32 lastmaxgd = thePrefs.maxGraphDownloadRate;

	thePrefs.SetMaxGraphDownloadRate(nDownloadCapacity);
	m_ctlMaxDown.SetRange(1, thePrefs.GetMaxGraphDownloadRate(), TRUE);
	SetRateSliderTicks(m_ctlMaxDown);

	thePrefs.SetMaxGraphUploadRate(nUploadCapacity);
	m_ctlMaxUp.SetRange(1, thePrefs.GetMaxGraphUploadRate(true), TRUE);

	SetRateSliderTicks(m_ctlMaxUp);

	const uint32 nLastUploadLimit = (uint32)m_ctlMaxUp.GetPos();
	const uint32 nLastDownloadLimit = (uint32)m_ctlMaxDown.GetPos();

	if (IsDlgButtonChecked(IDC_ULIMIT_LBL)) {
		u = nLastUploadLimit;
		const UINT v = (uint32)thePrefs.GetMaxGraphUploadRate(true);
		if (u > v)
			u = v * 4 / 5; //80%
	} else
		u = UNLIMITED;

	if (u > thePrefs.GetMaxUpload())
		// make USS go up to higher ul limit faster
		theApp.lastCommonRouteFinder->InitiateFastReactionPeriod();

	thePrefs.SetMaxUpload(u);
	thePrefs.SetLastMaxUpload(thePrefs.GetMaxUpload() != UNLIMITED ? thePrefs.GetMaxUpload() : nLastUploadLimit);

	if (thePrefs.GetMaxUpload() != UNLIMITED)
		m_ctlMaxUp.SetPos(thePrefs.GetMaxUpload());

	thePrefs.SetMaxDownload(IsDlgButtonChecked(IDC_DLIMIT_LBL) ? nLastDownloadLimit : UNLIMITED);

	if (thePrefs.GetMaxDownload() != UNLIMITED) {
		u = (uint32)thePrefs.GetMaxGraphDownloadRate();
		if (thePrefs.GetMaxDownload() > u)
			thePrefs.SetMaxDownload(u * 4 / 5); //80%
		thePrefs.SetLastMaxDownload(thePrefs.GetMaxDownload());
		m_ctlMaxDown.SetPos(thePrefs.GetMaxDownload());
	} else
		thePrefs.SetLastMaxDownload(nLastDownloadLimit);

	u = GetDlgItemInt(IDC_MAXSOURCEPERFILE, NULL, FALSE);
	thePrefs.maxsourceperfile = (u > INT_MAX ? 1 : u);

	bool bRestartApp = false;
	bool bPortRestartRequired = false;
	const bool bOldOpenListenPortsInWindowsFirewall = thePrefs.IsOpenListenPortsInWindowsFirewallEnabled();

	if (!bRandomizePortsOnStartup) {
		u = GetDlgItemInt(IDC_PORT, NULL, FALSE);
		uint16 nNewPort = (uint16)(u > _UI16_MAX ? 0 : u);
		if (nNewPort && nNewPort != thePrefs.GetConfiguredPort()) {
			const uint16 nOldConfiguredPort = thePrefs.GetConfiguredPort();
			const uint16 nOldPort = thePrefs.GetPort();
			thePrefs.SetConfiguredPort(nNewPort);
			thePrefs.port = nNewPort;
			if (theApp.IsPortchangeAllowed()) {
				const CListenSocket::ERebindResult eRebindResult = theApp.listensocket->Rebind();
				if (eRebindResult == CListenSocket::RebindRequiresRestart) {
					bRestartApp = true;
					bPortRestartRequired = true;
				}
				else if (eRebindResult == CListenSocket::RebindFailedKeptOldSocket) {
					thePrefs.SetConfiguredPort(nOldConfiguredPort);
					thePrefs.port = nOldPort;
					LogWarning(LOG_STATUSBAR, _T("%s"), (LPCTSTR)GetResString(_T("PORT_REBIND_FAILED_RESTORED")));
				}
			}
			else
				bRestartApp = true;
		}

		u = GetDlgItemInt(IDC_UDPPORT, NULL, FALSE);
		nNewPort = (uint16)(u > _UI16_MAX ? 0 : u);
		if (nNewPort != thePrefs.GetConfiguredUDPPort()) {
			const uint16 nOldConfiguredUDPPort = thePrefs.GetConfiguredUDPPort();
			const uint16 nOldUDPPort = thePrefs.GetUDPPort();
			thePrefs.SetConfiguredUDPPort(nNewPort);
			thePrefs.udpport = nNewPort;
			if (theApp.IsPortchangeAllowed()) {
				const CClientUDPSocket::ERebindResult eRebindResult = theApp.clientudp->Rebind();
				if (eRebindResult == CClientUDPSocket::RebindRequiresRestart) {
					bRestartApp = true;
					bPortRestartRequired = true;
				}
				else if (eRebindResult == CClientUDPSocket::RebindFailedKeptOldSocket) {
					thePrefs.SetConfiguredUDPPort(nOldConfiguredUDPPort);
					thePrefs.udpport = nOldUDPPort;
					LogWarning(LOG_STATUSBAR, _T("%s"), (LPCTSTR)GetResString(_T("PORT_REBIND_FAILED_RESTORED")));
				}
			}
			else
				bRestartApp = true;
		}
	}

	if (thePrefs.m_bshowoverhead != (IsDlgButtonChecked(IDC_SHOWOVERHEAD) != 0)) {
		thePrefs.m_bshowoverhead = !thePrefs.m_bshowoverhead;
		// free memory and reset overhead data counters
		theStats.ResetDownDatarateOverhead();
		theStats.ResetUpDatarateOverhead();
	}

	thePrefs.m_bForceSpeedsToKB = IsDlgButtonChecked(IDC_FORCE_SPEEDS_TO_KB) != 0;

	thePrefs.SetNetworkKademlia(IsDlgButtonChecked(IDC_NETWORK_KADEMLIA) != 0);

	thePrefs.SetNetworkED2K(IsDlgButtonChecked(IDC_NETWORK_ED2K) != 0);

	UpdatePortControlsForRandomization(false);

	thePrefs.autoconnect = IsDlgButtonChecked(IDC_AUTOCONNECT) != 0;
	thePrefs.reconnect = IsDlgButtonChecked(IDC_RECONN) != 0;

	thePrefs.SetPortRandomizationOnStartupEnabled(bRandomizePortsOnStartup);
	thePrefs.SetRandomPortRange(nRandomPortRangeStart, nRandomPortRangeEnd);
	thePrefs.SetOpenListenPortsInWindowsFirewallEnabled(IsDlgButtonChecked(IDC_OPEN_PORTS_WINDOWS_FIREWALL) != 0);
	const bool bOpenListenPortsInWindowsFirewall = thePrefs.IsOpenListenPortsInWindowsFirewallEnabled();
	if (bOpenListenPortsInWindowsFirewall && !bPortRestartRequired)
		theApp.EnsureWindowsFirewallListenPortRules(true);
	else if (!bOpenListenPortsInWindowsFirewall && bOldOpenListenPortsInWindowsFirewall)
		theApp.RemoveWindowsFirewallListenPortRules(true);

	if (lastmaxgu != thePrefs.maxGraphUploadRate) {
		theApp.emuledlg->statisticswnd->SetARange(false, thePrefs.GetMaxGraphUploadRate(true));
		theApp.emuledlg->m_UpSpeedGraph.Init_Graph(_T("Up"), thePrefs.GetMaxGraphUploadRate(true));
	}

	if (lastmaxgd != thePrefs.maxGraphDownloadRate) {
		theApp.emuledlg->statisticswnd->SetARange(true, thePrefs.maxGraphDownloadRate);
		theApp.emuledlg->m_DownSpeedGraph.Init_Graph(_T("Down"), thePrefs.GetMaxGraphDownloadRate());
	}

	UINT tempcon;
	u = GetDlgItemInt(IDC_MAXCON, NULL, FALSE);
	if (u <= 0)
		tempcon = thePrefs.maxconnections;
	else
		tempcon = (u >= INT_MAX ? CPreferences::GetRecommendedMaxConnections() : u);

	thePrefs.maxconnections = tempcon;

	if (thePrefs.IsUPnPEnabled() != (IsDlgButtonChecked(IDC_PREF_UPNPONSTART) != 0)) {
		thePrefs.m_bEnableUPnP = !thePrefs.m_bEnableUPnP;
		if (thePrefs.m_bEnableUPnP)
			theApp.emuledlg->StartUPnP();
		if (theApp.emuledlg->preferenceswnd->m_wndWebServer)
			theApp.emuledlg->preferenceswnd->m_wndWebServer.SetUPnPState();
	}

	theApp.scheduler->SaveOriginals();

	SetModified(FALSE);
	LoadSettings();

	theApp.emuledlg->ShowConnectionState();

	if (bRestartApp)
		LocMessageBox(_T("NOPORTCHANGEPOSSIBLE"), MB_OK, 0);
	ChangePorts(2);	//"Test ports" button enable/disable

	return CPropertyPage::OnApply();
}

void CPPgConnection::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(_T("CONNECTION")));
		SetDlgItemText(IDC_CAPACITIES_FRM, GetResString(_T("PW_CON_CAPFRM")));
		SetDlgItemText(IDC_DCAP_LBL, GetResString(_T("PW_CON_DOWNLBL")));
		SetDlgItemText(IDC_UCAP_LBL, GetResString(_T("PW_CON_UPLBL")));
		SetDlgItemText(IDC_LIMITS_FRM, GetResString(_T("PW_CON_LIMITFRM")));
		SetDlgItemText(IDC_DLIMIT_LBL, GetResString(_T("PW_DOWNL")));
		SetDlgItemText(IDC_ULIMIT_LBL, GetResString(_T("PW_UPL")));
		SetDlgItemText(IDC_CONNECTION_NETWORK, GetResString(_T("NETWORK")));
		SetDlgItemText(IDC_MAXCONN_FRM, GetResString(_T("PW_CONLIMITS")));
		SetDlgItemText(IDC_MAXCONLABEL, GetResString(_T("PW_MAXC")));
		SetDlgItemText(IDC_SHOWOVERHEAD, GetResString(_T("SHOWOVERHEAD")));
		SetDlgItemText(IDC_FORCE_SPEEDS_TO_KB, GetResString(_T("FORCESPEEDSTOKB")));
		SetDlgItemText(IDC_CLIENTPORT_FRM, GetResString(_T("PW_CLIENTPORT")));
		SetDlgItemText(IDC_MAXSRC_FRM, GetResString(_T("PW_MAXSOURCES")));
		SetDlgItemText(IDC_AUTOCONNECT, GetResString(_T("PW_AUTOCON")));
		SetDlgItemText(IDC_RECONN, GetResString(_T("PW_RECON")));
		SetDlgItemText(IDC_MAXSRCHARD_LBL, GetResString(_T("HARDLIMIT")));
		SetDlgItemText(IDC_WIZARD, GetResString(_T("WIZARD")) + _T("..."));
		SetDlgItemText(IDC_UDPDISABLE, GetResString(_T("UDPDISABLED")));
		SetDlgItemText(IDC_OPEN_PORTS_WINDOWS_FIREWALL, GetResString(_T("WINDOWS_FIREWALL_OPEN_LISTEN_PORTS")));
		SetDlgItemText(IDC_STARTTEST, GetResString(_T("STARTTEST")));
		SetDlgItemText(IDC_PREF_UPNPONSTART, GetResString(_T("UPNPSTART")));
		SetDlgItemText(IDC_RANDOMIZE_PORTS_ON_STARTUP, GetResString(_T("RANDOMIZE_LISTEN_PORTS_STARTUP")));
		SetDlgItemText(IDC_RANDOM_PORT_RANGE_FRM, GetResString(_T("RANDOM_PORT_RANGE")));
		SetDlgItemText(IDC_RANDOM_PORT_RANGE_START_LABEL, GetResString(_T("START_NOUN")));
		SetDlgItemText(IDC_RANDOM_PORT_RANGE_END_LABEL, GetResString(_T("END_NOUN")));
		UpdateSpeedDisplayUnitControls();
	}
}

void CPPgNetworkInterface::OnVpnGuardSettingsChange()
{
	if (m_bUpdatingControls)
		return;
	UpdateVpnGuardControls();
	SetModified();
}

void CPPgNetworkInterface::OnBindSettingsChange()
{
	if (m_bUpdatingControls)
		return;
	UpdateIpGuardControls();
	SetModified();
}

void CPPgConnection::OnRandomizePortsOnStartup()
{
	UpdateRandomPortRangeControls();
	UpdatePortControlsForRandomization(false);
	ChangePorts(2);
	OnSettingsChange();
}

void CPPgConnection::OnBnClickedWizard()
{
	CConnectionWizardDlg conWizard;
	conWizard.DoModal();
}

bool CPPgConnection::CheckUp(uint32 mUp, uint32 &mDown)
{
	if (thePrefs.maxGraphDownloadRate == 0)
		return false;
	uint32 uDown = mDown;
	if (mUp < 4 && mDown > mUp * 3)
		mDown = mUp * 3;
	else if (mUp < 10 && mDown > mUp * 4)
		mDown = mUp * 4;
	else if (mUp < 20 && mDown > mUp * 5)
		mDown = mUp * 5;
	if (mDown > thePrefs.maxGraphDownloadRate) {
		mDown = thePrefs.maxGraphDownloadRate;
		return true;
	}
	return uDown != mDown;
}

bool CPPgConnection::CheckDown(uint32 &mUp, uint32 mDown)
{
	if (thePrefs.maxGraphUploadRate == 0)
		return false;
	uint32 uUp = mUp;
	if (mDown < 13 && mUp * 3 < mDown)
		mUp = (mDown + 2) / 3;
	else if (mDown < 41 && mUp * 4 < mDown)
		mUp = (mDown + 3) / 4;
	else if (mUp < 20 && mUp * 5 < mDown)
		mUp = (mDown + 4) / 5;
	if (mUp > thePrefs.maxGraphUploadRate) {
		mUp = thePrefs.maxGraphUploadRate;
		return true;
	}
	return uUp != mUp;
}

void CPPgConnection::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
{
	SetModified(TRUE);

	uint32 maxup = m_ctlMaxUp.GetPos();
	uint32 maxdown = m_ctlMaxDown.GetPos();

	if (pScrollBar->GetSafeHwnd() == m_ctlMaxUp.m_hWnd) {
		if (CheckUp(maxup, maxdown)) {
			if (CheckDown(maxup, maxdown))
				m_ctlMaxUp.SetPos(maxup);
			m_ctlMaxDown.SetPos(maxdown);
		}
	} else { /*if (hWnd == m_ctlMaxDown.m_hWnd) { */
		if (CheckDown(maxup, maxdown)) {
			if (CheckUp(maxup, maxdown))
				m_ctlMaxDown.SetPos(maxdown);
			m_ctlMaxUp.SetPos(maxup);
		}
	}

	ShowLimitValues();

	UpdateData(FALSE);
	CPropertyPage::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CPPgConnection::UpdateCapacityValuesFromControls()
{
	if (GetDlgItem(IDC_DOWNLOAD_CAP) != NULL) {
		const UINT nDownloadCapacity = GetDlgItemInt(IDC_DOWNLOAD_CAP, NULL, FALSE);
		m_nDisplayDownloadCapacityKBS = DisplayCapacityValueToKBytesPerSec(nDownloadCapacity, m_bDisplaySpeedsInKB);
	}
	if (GetDlgItem(IDC_UPLOAD_CAP) != NULL) {
		const UINT nUploadCapacity = GetDlgItemInt(IDC_UPLOAD_CAP, NULL, FALSE);
		m_nDisplayUploadCapacityKBS = DisplayCapacityValueToKBytesPerSec(nUploadCapacity, m_bDisplaySpeedsInKB);
	}
}

void CPPgConnection::UpdateSpeedDisplayUnitControls()
{
	const bool bOldUpdatingControls = m_bUpdatingControls;
	m_bUpdatingControls = true;

	const CString strUnit(GetResString(m_bDisplaySpeedsInKB ? _T("KBYTESPERSEC") : _T("MBITSSEC")));
	SetDlgItemText(IDC_KBS2, strUnit);
	SetDlgItemText(IDC_KBS3, strUnit);
	SetDlgItemText(IDC_DOWNLOAD_CAP, FormatSpeedCapacityValue(m_nDisplayDownloadCapacityKBS, m_bDisplaySpeedsInKB));
	SetDlgItemText(IDC_UPLOAD_CAP, FormatSpeedCapacityValue(m_nDisplayUploadCapacityKBS, m_bDisplaySpeedsInKB));

	m_bUpdatingControls = bOldUpdatingControls;
	ShowLimitValues();
}

void CPPgConnection::OnCapacityChange()
{
	if (m_bUpdatingControls)
		return;
	UpdateCapacityValuesFromControls();
	SetModified();
}

void CPPgConnection::OnForceSpeedsToKBChange()
{
	if (m_bUpdatingControls)
		return;
	m_bDisplaySpeedsInKB = IsDlgButtonChecked(IDC_FORCE_SPEEDS_TO_KB) != 0;
	UpdateSpeedDisplayUnitControls();
	SetModified();
}

void CPPgConnection::ShowLimitValues()
{
	CString buffer;

	if (IsDlgButtonChecked(IDC_ULIMIT_LBL))
		buffer = FormatSpeedLimitValue((uint32)m_ctlMaxUp.GetPos(), m_bDisplaySpeedsInKB);
	SetDlgItemText(IDC_KBS4, buffer);

	if (!IsDlgButtonChecked(IDC_DLIMIT_LBL))
		buffer.Empty();
	else
		buffer = FormatSpeedLimitValue((uint32)m_ctlMaxDown.GetPos(), m_bDisplaySpeedsInKB);
	SetDlgItemText(IDC_KBS1, buffer);
}

void CPPgConnection::OnLimiterChange()
{
	m_ctlMaxDown.ShowWindow(IsDlgButtonChecked(IDC_DLIMIT_LBL) ? SW_SHOW : SW_HIDE);
	m_ctlMaxUp.ShowWindow(IsDlgButtonChecked(IDC_ULIMIT_LBL) ? SW_SHOW : SW_HIDE);

	ShowLimitValues();
	SetModified(TRUE);
}

void CPPgConnection::OnHelp()
{
	theApp.ShowHelp(eMule_FAQ_Preferences_Connection);
}


BOOL CPPgNetworkInterface::PreTranslateMessage(MSG *pMsg)
{
	if (m_ToolTip.GetSafeHwnd() != NULL)
		m_ToolTip.RelayEvent(pMsg);
	return CPropertyPage::PreTranslateMessage(pMsg);
}

void CPPgNetworkInterface::OnHelp()
{
	theApp.ShowHelp(eMule_FAQ_Preferences_Connection);
}

BOOL CPPgNetworkInterface::OnCommand(WPARAM wParam, LPARAM lParam)
{
	return (wParam == ID_HELP) ? OnHelpInfo(NULL) : __super::OnCommand(wParam, lParam);
}

BOOL CPPgNetworkInterface::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}

BOOL CPPgConnection::OnCommand(WPARAM wParam, LPARAM lParam)
{
	return (wParam == ID_HELP) ? OnHelpInfo(NULL) : __super::OnCommand(wParam, lParam);
}

BOOL CPPgConnection::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}

void CPPgConnection::OnStartPortTest()
{
	uint16 tcp = theApp.listensocket->GetConnectedPort();
	uint16 udp = theApp.clientudp->GetConnectedPort();
	if (tcp == 0 || udp != thePrefs.GetUDPPort())
		return;

	TriggerPortTest(tcp, udp);
}

void CPPgConnection::SetRateSliderTicks(CSliderCtrl &rRate)
{
	rRate.ClearTics();
	int iMin, iMax;
	rRate.GetRange(iMin, iMax);
	int iDiff = iMax - iMin;
	if (iDiff > 0) {
		CRect rc;
		rRate.GetWindowRect(&rc);
		if (rc.Width() > 0) {
			int iTic;
			int iPixels = rc.Width() / iDiff;
			if (iPixels >= 6)
				iTic = 1;
			else {
				iTic = 10;
				while (rc.Width() / (iDiff / iTic) < 8)
					iTic *= 10;
			}
			if (iTic)
				for (int i = ((iMin + (iTic - 1)) / iTic) * iTic; i < iMax; i += iTic)
					rRate.SetTic(i);
			rRate.SetPageSize(iTic);
		}
	}
}
