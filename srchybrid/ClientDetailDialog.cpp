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
#include "emule.h"
#include "ClientDetailDialog.h"
#include "UpDownClient.h"
#include "PartFile.h"
#include "ClientCredits.h"
#include "Server.h"
#include "ServerList.h"
#include "SharedFileList.h"
#include "HighColorTab.hpp"
#include "UserMsgs.h"
#include "ListenSocket.h"
#include "preferences.h"
#include "ClientList.h"
#include "eMuleAI/IPGeolocation.h"
#include "emuledlg.h" 
#include "eMuleAI/DarkMode.h"
#include <unordered_map>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	struct SClientDetailSnapshot
	{
		SClientDetailSnapshot() : bValid(false) {}
		bool bValid;
		CString strName;
		CString strHash;
		CString strSoft;
		CString strGeo;
		CString strCity;
		CString strIP;
		CString strObfuscation;
		CString strID;
		CString strServerIP;
		CString strServerName;
		CString strLeecher;
		CString strPunishment;
		CString strDownloading;
		CString strUploading;
		CString strDown;
		CString strUp;
		CString strAverageDown;
		CString strAverageUp;
		CString strDownTotal;
		CString strUpTotal;
		CString strRatio;
		CString strIdent;
		CString strRating;
		CString strScore;
		CString strKad;
		CString strPorts;
	};

	static CString GetClientDetailLabelText(LPCTSTR pszResourceID)
	{
		CString strText(GetResString(pszResourceID));
		strText.TrimRight();
		while (!strText.IsEmpty()) {
			const int iLast = strText.GetLength() - 1;
			const TCHAR cLast = strText[iLast];
			const bool bTrailingDash = cLast == _T('-') && iLast > 0 && _istspace(strText[iLast - 1]);
			if (cLast != _T(':') && cLast != 0x055D && cLast != 0x1361 && cLast != 0x1366 && cLast != 0x17D6 && cLast != 0xFF1A && !bTrailingDash)
				break;
			strText.Delete(iLast);
			strText.TrimRight();
		}
		return strText;
	}

	static int ClientDetailDluX(CWnd* pWnd, int iDlu)
	{
		CRect rc(0, 0, iDlu, 0);
		::MapDialogRect(pWnd->GetSafeHwnd(), &rc);
		return rc.Width();
	}

	static int MeasureClientDetailText(CDC& dc, const CString& strText)
	{
		if (strText.IsEmpty())
			return 0;

		CRect rcText(0, 0, 0, 0);
		dc.DrawText(strText, &rcText, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
		return rcText.Width();
	}

	static int MeasureClientDetailControlText(CWnd* pWnd, CDC& dc, UINT nID)
	{
		CString strText;
		pWnd->GetDlgItemText(nID, strText);
		return MeasureClientDetailText(dc, strText);
	}

	static void MoveClientDetailControl(CWnd* pWnd, UINT nID, int iLeft, int iWidth)
	{
		CWnd* pControl = pWnd->GetDlgItem(nID);
		ASSERT(pControl != NULL);
		if (pControl == NULL)
			return;

		CRect rcControl;
		pControl->GetWindowRect(&rcControl);
		pWnd->ScreenToClient(&rcControl);
		pControl->SetWindowPos(NULL, iLeft, rcControl.top, max(1, iWidth), rcControl.Height(), SWP_NOZORDER | SWP_NOACTIVATE);
	}

	static CString FormatClientPorts(uint16 nTCPPort, uint16 nUDPPort)
	{
		CString strTCPPort;
		CString strUDPPort;
		if (nTCPPort != 0)
			strTCPPort.Format(_T("%u"), nTCPPort);
		else
			strTCPPort = _T("?");
		if (nUDPPort != 0)
			strUDPPort.Format(_T("%u"), nUDPPort);
		else
			strUDPPort = _T("?");

		const CString strFormat(GetResString(_T("CD_CLIENT_PORTS")));
		CString strPorts;
		strPorts.FormatMessage(strFormat, (LPCTSTR)strTCPPort, (LPCTSTR)strUDPPort);
		return strPorts;
	}

	static void BuildClientDetailSnapshot(CUpDownClient* pClient, SClientDetailSnapshot& snapshot)
	{
		snapshot = SClientDetailSnapshot();
		if (pClient == NULL)
			return;

		snapshot.bValid = true;
		snapshot.strName = pClient->GetUserName() ? CString(pClient->GetUserName()) : CString(_T("?"));
		snapshot.strHash = pClient->HasValidHash() ? md4str(pClient->GetUserHash()) : CString(_T("?"));
		snapshot.strSoft = pClient->DbgGetFullClientSoftVer();
		bool bLongCountryName = true;
		const bool bGeolocationActive = theApp.ipgeolocation != NULL && theApp.ipgeolocation->IsIPGeolocationActive();
		snapshot.strGeo = bGeolocationActive ? pClient->GetGeolocationData(bLongCountryName) : CString(_T("?"));
		snapshot.strCity = bGeolocationActive ? pClient->GetGeolocationCity() : CString(_T("?"));
		snapshot.strIP = ipstr(!pClient->GetIP().IsNull() ? pClient->GetIP() : pClient->GetConnectIP());

		LPCTSTR pszObfuscation = NULL;
		if (!pClient->SupportsCryptLayer())
			pszObfuscation = _T("IDENTNOSUPPORT");
		else
			pszObfuscation = (thePrefs.IsCryptLayerEnabled() && (pClient->RequestsCryptLayer() || thePrefs.IsCryptLayerPreferred()) && (pClient->IsObfuscatedConnectionEstablished() || pClient->socket == NULL || !pClient->socket->IsConnected())) ? _T("ENABLED") : _T("SUPPORTED");
		snapshot.strObfuscation = GetResString(pszObfuscation);
#if defined(_DEBUG)
		if (pClient->IsObfuscatedConnectionEstablished())
			snapshot.strObfuscation += _T("(In Use)");
#endif
		snapshot.strID = GetResString(pClient->HasLowID() ? _T("IDLOW") : _T("IDHIGH"));

		if (pClient->GetServerIP()) {
			snapshot.strServerIP = ipstr(pClient->GetServerIP());
			const CServer *pServer = theApp.serverlist != NULL ? theApp.serverlist->GetServerByIPTCP(pClient->GetServerIP(), pClient->GetServerPort()) : NULL;
			snapshot.strServerName = pServer ? pServer->GetListName() : CString(_T("?"));
		} else {
			snapshot.strServerIP = _T("?");
			snapshot.strServerName = _T("?");
		}

		snapshot.strLeecher = pClient->GetPunishmentReason();
		snapshot.strPunishment = pClient->GetPunishmentText();
		const CKnownFile *pUploadFile = theApp.sharedfiles != NULL ? theApp.sharedfiles->GetFileByID(pClient->GetUploadFileID()) : NULL;
		snapshot.strDownloading = pUploadFile ? pUploadFile->GetFileName() : CString(_T("-"));
		snapshot.strUploading = pClient->GetRequestFile() ? pClient->GetRequestFile()->GetFileName() : CString(_T("-"));
		snapshot.strDown = CastItoXBytes(pClient->GetTransferredDown());
		snapshot.strUp = CastItoXBytes(pClient->GetTransferredUp());
		snapshot.strAverageDown = CastItoXBytes(pClient->GetDownloadDatarate(), false, true);
		snapshot.strAverageUp = CastItoXBytes(pClient->GetUploadDatarate(), false, true);

		CClientCredits* pCredits = pClient->Credits();
		if (pCredits != NULL) {
			snapshot.strDownTotal = CastItoXBytes(pCredits->GetDownloadedTotal());
			snapshot.strUpTotal = CastItoXBytes(pCredits->GetUploadedTotal());
			snapshot.strRatio.Format(_T("%.1f [%.1f]"), pCredits->GetScoreRatio(pClient->GetIP()), (float)pClient->Credits()->GetMyScoreRatio(pClient->GetIP()));
			if (theApp.clientcredits != NULL && theApp.clientcredits->CryptoAvailable()) {
				switch (pCredits->GetCurrentIdentState(pClient->GetIP())) {
				case IS_NOTAVAILABLE:
					snapshot.strIdent = GetResString(_T("IDENTNOSUPPORT"));
					break;
				case IS_IDFAILED:
				case IS_IDNEEDED:
				case IS_IDBADGUY:
					snapshot.strIdent = GetResString(_T("IDENTFAILED"));
					break;
				case IS_IDENTIFIED:
					snapshot.strIdent = GetResString(_T("IDENTOK"));
					break;
				default:
					snapshot.strIdent = _T("?");
				}
			} else
				snapshot.strIdent = GetResString(_T("IDENTNOSUPPORT"));
		} else {
			snapshot.strDownTotal = _T("?");
			snapshot.strUpTotal = _T("?");
			snapshot.strRatio = _T("?");
			snapshot.strIdent = _T("?");
		}

		if (pClient->GetUserName() && pCredits != NULL)
			snapshot.strRating.Format(_T("%.1f"), (float)pClient->GetScore(false, pClient->IsDownloading(), true));
		else
			snapshot.strRating = _T("?");

		if (pClient->GetUploadState() != US_NONE && pCredits != NULL) {
			if (!pClient->GetFriendSlot())
				snapshot.strScore.Format(_T("%u"), pClient->GetScore(false, pClient->IsDownloading(), false));
			else
				snapshot.strScore = GetResStringWithParens(_T("FRIEND_SLOT"));
		} else
			snapshot.strScore = _T("-");

		snapshot.strKad = GetResString(pClient->GetKadPort() ? _T("CONNECTED") : _T("DISCONNECTED"));
		snapshot.strPorts = FormatClientPorts(pClient->GetUserPort(), pClient->GetUDPPort());
	}

	class CClientDetailRuntimeToken : public CObject
	{
	public:
		explicit CClientDetailRuntimeToken(ClientRuntimeID uRuntimeID)
			: m_uRuntimeID(uRuntimeID)
		{
		}

		ClientRuntimeID GetRuntimeID() const
		{
			return m_uRuntimeID;
		}

	private:
		ClientRuntimeID m_uRuntimeID;
	};

	CCriticalSection s_csClientDetailRuntimeTokens;
	std::unordered_map<const CObject*, ClientRuntimeID> s_ClientDetailRuntimeTokens;
	std::unordered_map<const CObject*, SClientDetailSnapshot> s_ClientDetailSnapshots;

	CObject* CreateClientDetailToken(ClientRuntimeID uRuntimeID, const SClientDetailSnapshot& snapshot)
	{
		if (uRuntimeID == 0)
			return NULL;

		CObject* pToken = new CClientDetailRuntimeToken(uRuntimeID);
		{
			CSingleLock tokenLock(&s_csClientDetailRuntimeTokens, TRUE);
			s_ClientDetailRuntimeTokens[pToken] = uRuntimeID;
			s_ClientDetailSnapshots[pToken] = snapshot;
		}
		return pToken;
	}

	void DestroyClientDetailToken(CObject* pToken)
	{
		if (pToken == NULL)
			return;

		{
			CSingleLock tokenLock(&s_csClientDetailRuntimeTokens, TRUE);
			const auto itToken = s_ClientDetailRuntimeTokens.find(pToken);
			if (itToken != s_ClientDetailRuntimeTokens.end())
				s_ClientDetailRuntimeTokens.erase(itToken);
			const auto itSnapshot = s_ClientDetailSnapshots.find(pToken);
			if (itSnapshot != s_ClientDetailSnapshots.end())
				s_ClientDetailSnapshots.erase(itSnapshot);
		}
		delete static_cast<CClientDetailRuntimeToken*>(pToken);
	}

	ClientRuntimeID ClientDetailRuntimeIDFromToken(const CObject* pToken)
	{
		if (pToken == NULL)
			return 0;

		{
			CSingleLock tokenLock(&s_csClientDetailRuntimeTokens, TRUE);
			const auto itToken = s_ClientDetailRuntimeTokens.find(pToken);
			if (itToken != s_ClientDetailRuntimeTokens.end())
				return itToken->second;
		}

		const ULONG_PTR uTokenValue = reinterpret_cast<ULONG_PTR>(pToken);
		return (uTokenValue & 1) != 0 ? static_cast<ClientRuntimeID>(uTokenValue >> 1) : 0;
	}

	bool ClientDetailSnapshotFromToken(const CObject* pToken, SClientDetailSnapshot& snapshot)
	{
		if (pToken == NULL)
			return false;
		CSingleLock tokenLock(&s_csClientDetailRuntimeTokens, TRUE);
		const auto itSnapshot = s_ClientDetailSnapshots.find(pToken);
		if (itSnapshot == s_ClientDetailSnapshots.end() || !itSnapshot->second.bValid)
			return false;
		snapshot = itSnapshot->second;
		return true;
	}

	CObject* CreateTrackedClientDetailToken(const CUpDownClient* pClient)
	{
		if (pClient == NULL || theApp.clientlist == NULL)
			return NULL;

		CUpDownClient* pTrackedClient = theApp.clientlist->AcquireTrackedClientByPointer(pClient);
		if (pTrackedClient == NULL)
			return NULL;

		SClientDetailSnapshot snapshot;
		BuildClientDetailSnapshot(pTrackedClient, snapshot);
		CObject* pToken = CreateClientDetailToken(pTrackedClient->GetRuntimeID(), snapshot);
		pTrackedClient->ReleaseRuntimeReference();
		return pToken;
	}
}

static void SetClientDetailText(CWnd* pWnd, int nControlID, LPCTSTR pszText)
{
	if (pWnd != NULL)
		pWnd->SetDlgItemText(nControlID, pszText);
}

static void ClearClientDetailView(CClientDetailPage* pPage)
{
	if (pPage == NULL)
		return;

	CWnd* pFlagIcon = pPage->GetDlgItem(IDC_COUNTRY_FLAG_ICON);
	if (pFlagIcon != NULL) {
		((CStatic*)pFlagIcon)->SetIcon(NULL);
		pFlagIcon->ShowWindow(SW_HIDE);
	}

	SetClientDetailText(pPage, IDC_DNAME, _T("?"));
	SetClientDetailText(pPage, IDC_DHASH, _T("?"));
	SetClientDetailText(pPage, IDC_DSOFT, _T("?"));
	SetClientDetailText(pPage, IDC_GEOLOCATION_TXT, _T("?"));
	SetClientDetailText(pPage, IDC_CITY, _T("?"));
	SetClientDetailText(pPage, IDC_CLIENT_IP, _T("?"));
	SetClientDetailText(pPage, IDC_CLIENT_PORT, _T("?"));
	SetClientDetailText(pPage, IDC_OBFUSCATION_STAT, _T("?"));
	SetClientDetailText(pPage, IDC_DID, _T("?"));
	SetClientDetailText(pPage, IDC_DSIP, _T("?"));
	SetClientDetailText(pPage, IDC_DSNAME, _T("?"));
	SetClientDetailText(pPage, IDC_LEECHER, _T("?"));
	SetClientDetailText(pPage, IDC_PUNISHMENT, _T("?"));
	SetClientDetailText(pPage, IDC_DDOWNLOADING, _T("-"));
	SetClientDetailText(pPage, IDC_UPLOADING, _T("-"));
	SetClientDetailText(pPage, IDC_DDUP, _T("?"));
	SetClientDetailText(pPage, IDC_DDOWN, _T("?"));
	SetClientDetailText(pPage, IDC_DAVUR, _T("?"));
	SetClientDetailText(pPage, IDC_DAVDR, _T("?"));
	SetClientDetailText(pPage, IDC_DUPTOTAL, _T("?"));
	SetClientDetailText(pPage, IDC_DDOWNTOTAL, _T("?"));
	SetClientDetailText(pPage, IDC_DRATIO, _T("?"));
	SetClientDetailText(pPage, IDC_CDIDENT, _T("?"));
	SetClientDetailText(pPage, IDC_DRATING, _T("?"));
	SetClientDetailText(pPage, IDC_DSCORE, _T("-"));
	SetClientDetailText(pPage, IDC_CLIENTDETAIL_KADCON, _T("?"));
}

static void ApplyClientDetailSnapshot(CClientDetailPage* pPage, const SClientDetailSnapshot& snapshot)
{
	if (pPage == NULL || !snapshot.bValid)
		return;

	CWnd* pFlagIcon = pPage->GetDlgItem(IDC_COUNTRY_FLAG_ICON);
	if (pFlagIcon != NULL) {
		((CStatic*)pFlagIcon)->SetIcon(NULL);
		pFlagIcon->ShowWindow(SW_HIDE);
	}

	SetClientDetailText(pPage, IDC_DNAME, snapshot.strName);
	SetClientDetailText(pPage, IDC_DHASH, snapshot.strHash);
	SetClientDetailText(pPage, IDC_DSOFT, snapshot.strSoft);
	SetClientDetailText(pPage, IDC_GEOLOCATION_TXT, snapshot.strGeo);
	SetClientDetailText(pPage, IDC_CITY, snapshot.strCity);
	SetClientDetailText(pPage, IDC_CLIENT_IP, snapshot.strIP);
	SetClientDetailText(pPage, IDC_CLIENT_PORT, snapshot.strPorts);
	SetClientDetailText(pPage, IDC_OBFUSCATION_STAT, snapshot.strObfuscation);
	SetClientDetailText(pPage, IDC_DID, snapshot.strID);
	SetClientDetailText(pPage, IDC_DSIP, snapshot.strServerIP);
	SetClientDetailText(pPage, IDC_DSNAME, snapshot.strServerName);
	SetClientDetailText(pPage, IDC_LEECHER, snapshot.strLeecher);
	SetClientDetailText(pPage, IDC_PUNISHMENT, snapshot.strPunishment);
	SetClientDetailText(pPage, IDC_DDOWNLOADING, snapshot.strDownloading);
	SetClientDetailText(pPage, IDC_UPLOADING, snapshot.strUploading);
	SetClientDetailText(pPage, IDC_DDUP, snapshot.strDown);
	SetClientDetailText(pPage, IDC_DDOWN, snapshot.strUp);
	SetClientDetailText(pPage, IDC_DAVUR, snapshot.strAverageDown);
	SetClientDetailText(pPage, IDC_DAVDR, snapshot.strAverageUp);
	SetClientDetailText(pPage, IDC_DUPTOTAL, snapshot.strDownTotal);
	SetClientDetailText(pPage, IDC_DDOWNTOTAL, snapshot.strUpTotal);
	SetClientDetailText(pPage, IDC_DRATIO, snapshot.strRatio);
	SetClientDetailText(pPage, IDC_CDIDENT, snapshot.strIdent);
	SetClientDetailText(pPage, IDC_DRATING, snapshot.strRating);
	SetClientDetailText(pPage, IDC_DSCORE, snapshot.strScore);
	SetClientDetailText(pPage, IDC_CLIENTDETAIL_KADCON, snapshot.strKad);
}

class CScopedDetailClientRef
{
public:
	CScopedDetailClientRef()
		: m_pClient(NULL)
	{
	}

	explicit CScopedDetailClientRef(ClientRuntimeID uRuntimeID)
		: m_pClient(NULL)
	{
		AttachRuntimeID(uRuntimeID);
	}

	~CScopedDetailClientRef()
	{
		Release();
	}

	void AttachRuntimeID(ClientRuntimeID uRuntimeID)
	{
		Release();
		if (uRuntimeID != 0 && theApp.clientlist != NULL)
			m_pClient = theApp.clientlist->AcquireTrackedClientByRuntimeID(uRuntimeID);
	}

	CUpDownClient* Get() const
	{
		return m_pClient;
	}

	void Release()
	{
		if (m_pClient != NULL) {
			m_pClient->ReleaseRuntimeReference();
			m_pClient = NULL;
		}
	}

private:
	CUpDownClient* m_pClient;
};

static CUpDownClient* ResolveClientDetailClient(const CObject* pToken, CScopedDetailClientRef& clientRef)
{
	const ClientRuntimeID uRuntimeID = ClientDetailRuntimeIDFromToken(pToken);
	clientRef.AttachRuntimeID(uRuntimeID);
	return clientRef.Get();
}


///////////////////////////////////////////////////////////////////////////////
// CClientDetailPage

IMPLEMENT_DYNAMIC(CClientDetailPage, CResizablePage)

BEGIN_MESSAGE_MAP(CClientDetailPage, CResizablePage)
	ON_MESSAGE(UM_DATA_CHANGED, OnDataChanged)
	ON_WM_SIZE()
END_MESSAGE_MAP()

CClientDetailPage::CClientDetailPage()
	: CResizablePage(CClientDetailPage::IDD)
	, m_paClients()
	, m_bDataChanged()
	, countryflag()
	, m_iLeftLabelWidth()
	, m_iRightLabelWidth()
	, m_iLeftMinimumValueWidth()
	, m_iRightMinimumValueWidth()
	, m_iColonWidth()
	, m_iMinimumClientWidth()
	, m_bLayoutReady()
{
	m_strCaption = GetResString(_T("CD_TITLE"));
	m_psp.pszTitle = m_strCaption;
	m_psp.dwFlags |= PSP_USETITLE;
}

void CClientDetailPage::DoDataExchange(CDataExchange *pDX)
{
	CResizablePage::DoDataExchange(pDX);
}

void CClientDetailPage::UpdateLayoutMetrics()
{
	if (!m_hWnd)
		return;

	static const UINT s_auLeftLabelIDs[] =
	{
		IDC_STATIC31, IDC_STATIC32, IDC_STATIC_SOFTWARE, IDC_STATIC35, IDC_STATIC133x, IDC_STATIC_GEOLOCATION, IDC_STATIC_CLIENT_IP,
		IDC_STATIC41, IDC_STATIC48, IDC_STATIC42, IDC_STATIC43, IDC_STATIC44, IDC_STATIC51, IDC_STATIC52, IDC_STATIC53
	};
	static const UINT s_auRightLabelIDs[] =
	{
		IDC_CLIENTDETAIL_KAD, IDC_STATIC38, IDC_STATIC_OBF_LABEL, IDC_STATIC_CITY, IDC_STATIC_CLIENT_PORT,
		IDC_STATIC45, IDC_STATIC46, IDC_STATIC47, IDC_STATIC_PUNISHMENT, IDC_STATIC_BAD_CLIENT_TYPE
	};
	static const LPCTSTR s_apszLeftFixedValueKeys[] =
	{
		_T("IDENTNOSUPPORT"), _T("IDENTFAILED"), _T("IDENTOK")
	};
	static const LPCTSTR s_apszRightFixedValueKeys[] =
	{
		_T("IDHIGH"), _T("IDLOW"), _T("CONNECTED"), _T("DISCONNECTED"), _T("IDENTNOSUPPORT"), _T("ENABLED"), _T("SUPPORTED")
	};

	CClientDC dc(this);
	CFont* pFont = GetDlgItem(IDC_STATIC31)->GetFont();
	CFont* pOldFont = pFont != NULL ? dc.SelectObject(pFont) : NULL;

	m_iLeftLabelWidth = 0;
	for (size_t i = 0; i < _countof(s_auLeftLabelIDs); ++i)
		m_iLeftLabelWidth = max(m_iLeftLabelWidth, MeasureClientDetailControlText(this, dc, s_auLeftLabelIDs[i]));

	m_iRightLabelWidth = 0;
	for (size_t i = 0; i < _countof(s_auRightLabelIDs); ++i)
		m_iRightLabelWidth = max(m_iRightLabelWidth, MeasureClientDetailControlText(this, dc, s_auRightLabelIDs[i]));

	const int iHashMinimumWidth = MeasureClientDetailText(dc, _T("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"));
	m_iLeftMinimumValueWidth = 0;
	for (size_t i = 0; i < _countof(s_apszLeftFixedValueKeys); ++i)
		m_iLeftMinimumValueWidth = max(m_iLeftMinimumValueWidth, MeasureClientDetailText(dc, GetResString(s_apszLeftFixedValueKeys[i])));
	m_iLeftMinimumValueWidth = max(m_iLeftMinimumValueWidth, MeasureClientDetailText(dc, _T("100.0 [100.0]")));

	m_iRightMinimumValueWidth = MeasureClientDetailText(dc, FormatClientPorts(65535, 65535));
	for (size_t i = 0; i < _countof(s_apszRightFixedValueKeys); ++i)
		m_iRightMinimumValueWidth = max(m_iRightMinimumValueWidth, MeasureClientDetailText(dc, GetResString(s_apszRightFixedValueKeys[i])));

	CString strDebugObfuscation(GetResString(_T("ENABLED")));
	strDebugObfuscation += _T("(In Use)");
	m_iRightMinimumValueWidth = max(m_iRightMinimumValueWidth, MeasureClientDetailText(dc, strDebugObfuscation));

	m_iColonWidth = max(ClientDetailDluX(this, 4), MeasureClientDetailText(dc, _T(":")) + ClientDetailDluX(this, 1));
	const int iTextPadding = ClientDetailDluX(this, 3);
	m_iLeftLabelWidth += iTextPadding;
	m_iRightLabelWidth += iTextPadding;
	m_iLeftMinimumValueWidth = max(m_iLeftMinimumValueWidth + iTextPadding, ClientDetailDluX(this, 76));
	m_iRightMinimumValueWidth = max(m_iRightMinimumValueWidth + iTextPadding, ClientDetailDluX(this, 76));

	const int iOuterMargin = ClientDetailDluX(this, 10);
	const int iLabelColonGap = ClientDetailDluX(this, 2);
	const int iColonValueGap = ClientDetailDluX(this, 4);
	const int iBlockGap = ClientDetailDluX(this, 10);
	const int iHashAvailableWidth = m_iLeftMinimumValueWidth + m_iRightLabelWidth + iLabelColonGap + m_iColonWidth + iColonValueGap;
	if (iHashAvailableWidth < iHashMinimumWidth + iTextPadding)
		m_iLeftMinimumValueWidth += iHashMinimumWidth + iTextPadding - iHashAvailableWidth;
	m_iMinimumClientWidth = iOuterMargin + m_iLeftLabelWidth + iLabelColonGap + m_iColonWidth + iColonValueGap + m_iLeftMinimumValueWidth
		+ iBlockGap + m_iRightLabelWidth + iLabelColonGap + m_iColonWidth + iColonValueGap + m_iRightMinimumValueWidth + iOuterMargin;

	if (pOldFont != NULL)
		dc.SelectObject(pOldFont);
}

void CClientDetailPage::LayoutControls(int iClientWidth)
{
	if (!m_bLayoutReady || iClientWidth <= 0)
		return;

	static const UINT s_auGroupBoxIDs[] = { IDC_STATIC30, IDC_STATIC40, IDC_STATIC50 };
	static const UINT s_auLeftLabelIDs[] =
	{
		IDC_STATIC31, IDC_STATIC32, IDC_STATIC_SOFTWARE, IDC_STATIC35, IDC_STATIC133x, IDC_STATIC_GEOLOCATION, IDC_STATIC_CLIENT_IP,
		IDC_STATIC41, IDC_STATIC48, IDC_STATIC42, IDC_STATIC43, IDC_STATIC44, IDC_STATIC51, IDC_STATIC52, IDC_STATIC53
	};
	static const UINT s_auLeftColonIDs[] =
	{
		IDC_CLIENTDETAIL_COLON_NAME, IDC_CLIENTDETAIL_COLON_HASH, IDC_CLIENTDETAIL_COLON_SOFTWARE, IDC_CLIENTDETAIL_COLON_SERVER_IP,
		IDC_CLIENTDETAIL_COLON_IDENTIFICATION, IDC_CLIENTDETAIL_COLON_COUNTRY, IDC_CLIENTDETAIL_COLON_IP_ADDRESS,
		IDC_CLIENTDETAIL_COLON_CURRENTLY_DOWNLOADING, IDC_CLIENTDETAIL_COLON_REQUESTED_UPLOAD, IDC_CLIENTDETAIL_COLON_DOWNLOADED_SESSION,
		IDC_CLIENTDETAIL_COLON_DOWNLOAD_RATE, IDC_CLIENTDETAIL_COLON_DOWNLOADED_TOTAL, IDC_CLIENTDETAIL_COLON_DLUP_MODIFIER,
		IDC_CLIENTDETAIL_COLON_RATING, IDC_CLIENTDETAIL_COLON_UPLOAD_QUEUE_SCORE
	};
	static const UINT s_auRightLabelIDs[] =
	{
		IDC_CLIENTDETAIL_KAD, IDC_STATIC38, IDC_STATIC_OBF_LABEL, IDC_STATIC_CITY, IDC_STATIC_CLIENT_PORT,
		IDC_STATIC45, IDC_STATIC46, IDC_STATIC47, IDC_STATIC_PUNISHMENT, IDC_STATIC_BAD_CLIENT_TYPE
	};
	static const UINT s_auRightColonIDs[] =
	{
		IDC_CLIENTDETAIL_COLON_KAD, IDC_CLIENTDETAIL_COLON_SERVER_NAME, IDC_CLIENTDETAIL_COLON_OBFUSCATION, IDC_CLIENTDETAIL_COLON_CITY,
		IDC_CLIENTDETAIL_COLON_PORT, IDC_CLIENTDETAIL_COLON_UPLOADED_SESSION, IDC_CLIENTDETAIL_COLON_UPLOAD_RATE,
		IDC_CLIENTDETAIL_COLON_UPLOADED_TOTAL, IDC_CLIENTDETAIL_COLON_PUNISHMENT, IDC_CLIENTDETAIL_COLON_REASON
	};
	static const UINT s_auLeftValueIDs[] =
	{
		IDC_DSOFT, IDC_DSIP, IDC_CDIDENT, IDC_CLIENT_IP, IDC_DDOWN, IDC_DAVDR, IDC_DDOWNTOTAL, IDC_DRATIO, IDC_DRATING
	};
	static const UINT s_auRightValueIDs[] =
	{
		IDC_DID, IDC_CLIENTDETAIL_KADCON, IDC_DSNAME, IDC_OBFUSCATION_STAT, IDC_CITY, IDC_CLIENT_PORT,
		IDC_DDUP, IDC_DAVUR, IDC_DUPTOTAL, IDC_PUNISHMENT, IDC_LEECHER
	};
	static const UINT s_auFullWidthValueIDs[] = { IDC_DNAME, IDC_DDOWNLOADING, IDC_UPLOADING, IDC_DSCORE };

	const int iGroupMargin = ClientDetailDluX(this, 4);
	const int iOuterMargin = ClientDetailDluX(this, 10);
	const int iLabelColonGap = ClientDetailDluX(this, 2);
	const int iColonValueGap = ClientDetailDluX(this, 4);
	const int iBlockGap = ClientDetailDluX(this, 10);
	const int iAvailableExtra = max(0, iClientWidth - m_iMinimumClientWidth);
	const int iLeftValueWidth = m_iLeftMinimumValueWidth + iAvailableExtra / 2;
	const int iRightValueWidth = m_iRightMinimumValueWidth + iAvailableExtra - iAvailableExtra / 2;

	const int iLeftLabelX = iOuterMargin;
	const int iLeftColonX = iLeftLabelX + m_iLeftLabelWidth + iLabelColonGap;
	const int iLeftValueX = iLeftColonX + m_iColonWidth + iColonValueGap;
	const int iRightLabelX = iLeftValueX + iLeftValueWidth + iBlockGap;
	const int iRightColonX = iRightLabelX + m_iRightLabelWidth + iLabelColonGap;
	const int iRightValueX = iRightColonX + m_iColonWidth + iColonValueGap;

	for (size_t i = 0; i < _countof(s_auGroupBoxIDs); ++i)
		MoveClientDetailControl(this, s_auGroupBoxIDs[i], iGroupMargin, max(1, iClientWidth - 2 * iGroupMargin));
	for (size_t i = 0; i < _countof(s_auLeftLabelIDs); ++i)
		MoveClientDetailControl(this, s_auLeftLabelIDs[i], iLeftLabelX, m_iLeftLabelWidth);
	for (size_t i = 0; i < _countof(s_auLeftColonIDs); ++i)
		MoveClientDetailControl(this, s_auLeftColonIDs[i], iLeftColonX, m_iColonWidth);
	for (size_t i = 0; i < _countof(s_auRightLabelIDs); ++i)
		MoveClientDetailControl(this, s_auRightLabelIDs[i], iRightLabelX, m_iRightLabelWidth);
	for (size_t i = 0; i < _countof(s_auRightColonIDs); ++i)
		MoveClientDetailControl(this, s_auRightColonIDs[i], iRightColonX, m_iColonWidth);
	for (size_t i = 0; i < _countof(s_auLeftValueIDs); ++i)
		MoveClientDetailControl(this, s_auLeftValueIDs[i], iLeftValueX, iLeftValueWidth);
	for (size_t i = 0; i < _countof(s_auRightValueIDs); ++i)
		MoveClientDetailControl(this, s_auRightValueIDs[i], iRightValueX, iRightValueWidth);
	for (size_t i = 0; i < _countof(s_auFullWidthValueIDs); ++i)
		MoveClientDetailControl(this, s_auFullWidthValueIDs[i], iLeftValueX, max(1, iClientWidth - iOuterMargin - iLeftValueX));
	MoveClientDetailControl(this, IDC_DHASH, iLeftValueX, max(1, iRightValueX - iBlockGap - iLeftValueX));

	const int iFlagWidth = ClientDetailDluX(this, 16);
	const int iFlagGap = ClientDetailDluX(this, 2);
	CWnd* pFlag = GetDlgItem(IDC_COUNTRY_FLAG_ICON);
	const bool bFlagVisible = pFlag != NULL && (pFlag->GetStyle() & WS_VISIBLE) != 0;
	MoveClientDetailControl(this, IDC_COUNTRY_FLAG_ICON, iLeftValueX, iFlagWidth);
	MoveClientDetailControl(this, IDC_GEOLOCATION_TXT, bFlagVisible ? iLeftValueX + iFlagWidth + iFlagGap : iLeftValueX,
		max(1, iLeftValueWidth - (bFlagVisible ? iFlagWidth + iFlagGap : 0)));
}

void CClientDetailPage::UpdateLayout()
{
	if (!m_hWnd)
		return;

	CRect rcClient;
	GetClientRect(&rcClient);
	LayoutControls(rcClient.Width());
}

void CClientDetailPage::OnSize(UINT nType, int cx, int cy)
{
	CResizablePage::OnSize(nType, cx, cy);
	LayoutControls(cx);
}

BOOL CClientDetailPage::OnInitDialog()
{
	CResizablePage::OnInitDialog();
	InitWindowStyles(this);
	Localize();
	m_bLayoutReady = true;
	UpdateLayout();
	return TRUE;
}

BOOL CClientDetailPage::OnSetActive()
{
	if (!CResizablePage::OnSetActive())
		return FALSE;

	if (m_bDataChanged) {
		const CObject* pToken = (m_paClients != NULL && m_paClients->GetSize() > 0) ? (*m_paClients)[0] : NULL;
		CScopedDetailClientRef clientRef;
		CUpDownClient* client = ResolveClientDetailClient(pToken, clientRef);
		if (client == NULL) {
			SClientDetailSnapshot snapshot;
			if (ClientDetailSnapshotFromToken(pToken, snapshot))
				ApplyClientDetailSnapshot(this, snapshot);
			else
				ClearClientDetailView(this);
			m_bDataChanged = false;
			return TRUE;
		}

			SetDlgItemText(IDC_DNAME, (client->GetUserName() ? client->GetUserName() : _T("?")));
			SetDlgItemText(IDC_DHASH, (client->HasValidHash() ? (LPCTSTR)md4str(client->GetUserHash()) : (LPCTSTR)_T("?")));
		SetDlgItemText(IDC_DSOFT, client->DbgGetFullClientSoftVer());

		bool longCountryName = true;
		const bool bGeolocationActive = theApp.ipgeolocation != NULL && theApp.ipgeolocation->IsIPGeolocationActive();
		SetDlgItemText(IDC_GEOLOCATION_TXT, bGeolocationActive ? client->GetGeolocationData(longCountryName) : CString(_T("?")));
		SetDlgItemText(IDC_CITY, bGeolocationActive ? client->GetGeolocationCity() : CString(_T("?")));
		if (bGeolocationActive && theApp.ipgeolocation->ShowCountryFlag()) {
			countryflag = theApp.ipgeolocation->GetFlagImageList()->ExtractIcon(client->GetCountryFlagIndex());
			((CStatic*)GetDlgItem(IDC_COUNTRY_FLAG_ICON))->SetIcon(countryflag);
			((CStatic*)GetDlgItem(IDC_COUNTRY_FLAG_ICON))->ShowWindow(SW_SHOW);
		} else {
			((CStatic*)GetDlgItem(IDC_COUNTRY_FLAG_ICON))->SetIcon(NULL);
			((CStatic*)GetDlgItem(IDC_COUNTRY_FLAG_ICON))->ShowWindow(SW_HIDE);
		}
		UpdateLayout();

		LPCTSTR uid;
		if (!client->SupportsCryptLayer())
			uid = _T("IDENTNOSUPPORT");
		else
			uid = (	   thePrefs.IsCryptLayerEnabled()
					&& (client->RequestsCryptLayer() || thePrefs.IsCryptLayerPreferred())
					&& (client->IsObfuscatedConnectionEstablished() || client->socket == NULL || !client->socket->IsConnected())
				  )
				? _T("ENABLED") : _T("SUPPORTED");
		CString buffer(GetResString(uid));
#if defined(_DEBUG)
		if (client->IsObfuscatedConnectionEstablished())
			buffer += _T("(In Use)");
#endif
		GetDlgItem(IDC_CLIENT_IP)->SetWindowText(ipstr(!client->GetIP().IsNull() ? client->GetIP() : client->GetConnectIP()));
		SetDlgItemText(IDC_CLIENT_PORT, FormatClientPorts(client->GetUserPort(), client->GetUDPPort()));

		SetDlgItemText(IDC_OBFUSCATION_STAT, buffer);

		SetDlgItemText(IDC_DID, GetResString(client->HasLowID() ? _T("IDLOW") : _T("IDHIGH")));

		if (client->GetServerIP()) {
			SetDlgItemText(IDC_DSIP, ipstr(client->GetServerIP()));
			const CServer *cserver = theApp.serverlist->GetServerByIPTCP(client->GetServerIP(), client->GetServerPort());
			SetDlgItemText(IDC_DSNAME, cserver ? (LPCTSTR)cserver->GetListName() : (LPCTSTR)_T("?"));
		} else {
			SetDlgItemText(IDC_DSIP, _T("?"));
			SetDlgItemText(IDC_DSNAME, _T("?"));
		}

		GetDlgItem(IDC_LEECHER)->SetWindowText(client->GetPunishmentReason());
		GetDlgItem(IDC_PUNISHMENT)->SetWindowText(client->GetPunishmentText());

		const CKnownFile *file = theApp.sharedfiles->GetFileByID(client->GetUploadFileID());
		SetDlgItemText(IDC_DDOWNLOADING, file ? (LPCTSTR)file->GetFileName() : (LPCTSTR)_T("-"));

		SetDlgItemText(IDC_UPLOADING, client->GetRequestFile() ? (LPCTSTR)client->GetRequestFile()->GetFileName() : (LPCTSTR)_T("-"));
		SetDlgItemText(IDC_DDUP, CastItoXBytes(client->GetTransferredDown()));
		SetDlgItemText(IDC_DDOWN, CastItoXBytes(client->GetTransferredUp()));
		SetDlgItemText(IDC_DAVUR, CastItoXBytes(client->GetDownloadDatarate(), false, true));
		SetDlgItemText(IDC_DAVDR, CastItoXBytes(client->GetUploadDatarate(), false, true));

		CClientCredits* clcredits = client->Credits();
		if (clcredits) {
			SetDlgItemText(IDC_DUPTOTAL, CastItoXBytes(clcredits->GetDownloadedTotal()));
			SetDlgItemText(IDC_DDOWNTOTAL, CastItoXBytes(clcredits->GetUploadedTotal()));
			buffer.Format(_T("%.1f [%.1f]"), clcredits->GetScoreRatio(client->GetIP()), (float)client->Credits()->GetMyScoreRatio(client->GetIP()));
			SetDlgItemText(IDC_DRATIO, buffer);

			if (theApp.clientcredits->CryptoAvailable()) {
				switch (clcredits->GetCurrentIdentState(client->GetIP())) {
				case IS_NOTAVAILABLE:
					SetDlgItemText(IDC_CDIDENT, GetResString(_T("IDENTNOSUPPORT")));
					break;
				case IS_IDFAILED:
				case IS_IDNEEDED:
				case IS_IDBADGUY:
					SetDlgItemText(IDC_CDIDENT, GetResString(_T("IDENTFAILED")));
					break;
				case IS_IDENTIFIED:
					SetDlgItemText(IDC_CDIDENT, GetResString(_T("IDENTOK")));
				}
			} else
				SetDlgItemText(IDC_CDIDENT, GetResString(_T("IDENTNOSUPPORT")));
		} else {
			SetDlgItemText(IDC_DDOWNTOTAL, _T("?"));
			SetDlgItemText(IDC_DUPTOTAL, _T("?"));
			SetDlgItemText(IDC_DRATIO, _T("?"));
			SetDlgItemText(IDC_CDIDENT, _T("?"));
		}

		if (client->GetUserName() && clcredits != NULL) {
			buffer.Format(_T("%.1f"), (float)client->GetScore(false, client->IsDownloading(), true));
			SetDlgItemText(IDC_DRATING, buffer);
		} else
			SetDlgItemText(IDC_DRATING, _T("?"));

		if (client->GetUploadState() != US_NONE && clcredits != NULL) {
			if (!client->GetFriendSlot())
				SetDlgItemInt(IDC_DSCORE, client->GetScore(false, client->IsDownloading(), false));
			else
				SetDlgItemText(IDC_DSCORE, GetResStringWithParens(_T("FRIEND_SLOT")));
		} else
			SetDlgItemText(IDC_DSCORE, _T("-"));

		SetDlgItemText(IDC_CLIENTDETAIL_KADCON, GetResString(client->GetKadPort() ? _T("CONNECTED") : _T("DISCONNECTED")));

		m_bDataChanged = false;
	}
	return TRUE;
}

LRESULT CClientDetailPage::OnDataChanged(WPARAM, LPARAM)
{
	m_bDataChanged = true;
	return 1;
}

void CClientDetailPage::Localize()
{
	if (!m_hWnd)
		return;
	SetTabTitle(_T("CD_TITLE"), this);

	SetDlgItemText(IDC_STATIC30, GetResString(_T("CD_GENERAL")));
	SetDlgItemText(IDC_STATIC31, GetClientDetailLabelText(_T("SW_NAME")));
	SetDlgItemText(IDC_STATIC32, GetClientDetailLabelText(_T("CD_UHASH2")));
	SetDlgItemText(IDC_STATIC_SOFTWARE, GetClientDetailLabelText(_T("CD_CSOFT")));
	SetDlgItemText(IDC_STATIC35, GetClientDetailLabelText(_T("META_SERVERIP")));
	SetDlgItemText(IDC_STATIC38, GetClientDetailLabelText(_T("SL_SERVERNAME")));
	SetDlgItemText(IDC_STATIC_OBF_LABEL, GetClientDetailLabelText(_T("OBFUSCATION")));

	SetDlgItemText(IDC_STATIC40, GetResString(_T("CD_TRANS")));
	SetDlgItemText(IDC_STATIC41, GetClientDetailLabelText(_T("CD_CDOWN")));
	SetDlgItemText(IDC_STATIC42, GetClientDetailLabelText(_T("CD_DOWN")));
	SetDlgItemText(IDC_STATIC43, GetClientDetailLabelText(_T("CD_ADOWN")));
	SetDlgItemText(IDC_STATIC44, GetClientDetailLabelText(_T("INFLST_USER_TOTALDOWNLOAD")));
	SetDlgItemText(IDC_STATIC45, GetClientDetailLabelText(_T("CD_UP")));
	SetDlgItemText(IDC_STATIC46, GetClientDetailLabelText(_T("CD_AUP")));
	SetDlgItemText(IDC_STATIC47, GetClientDetailLabelText(_T("INFLST_USER_TOTALUPLOAD")));
	SetDlgItemText(IDC_STATIC48, GetClientDetailLabelText(_T("CD_UPLOADREQ")));

	SetDlgItemText(IDC_STATIC50, GetResString(_T("CD_SCORES")));
	SetDlgItemText(IDC_STATIC51, GetClientDetailLabelText(_T("CD_MOD")));
	SetDlgItemText(IDC_STATIC52, GetClientDetailLabelText(_T("CD_RATING")));
	SetDlgItemText(IDC_STATIC53, GetClientDetailLabelText(_T("CD_USCORE")));
	SetDlgItemText(IDC_STATIC133x, GetClientDetailLabelText(_T("CD_IDENT")));
	SetDlgItemText(IDC_CLIENTDETAIL_KAD, GetClientDetailLabelText(_T("KADEMLIA")));
	SetDlgItemText(IDC_STATIC_GEOLOCATION, GetClientDetailLabelText(_T("GEOLOCATION")));
	SetDlgItemText(IDC_STATIC_CITY, GetClientDetailLabelText(_T("CD_CITY")));
	SetDlgItemText(IDC_STATIC_BAD_CLIENT_TYPE, GetClientDetailLabelText(_T("REASON")));
	SetDlgItemText(IDC_STATIC_PUNISHMENT, GetClientDetailLabelText(_T("PUNISHMENT")));
	SetDlgItemText(IDC_STATIC_CLIENT_IP, GetClientDetailLabelText(_T("CD_UIP")));
	SetDlgItemText(IDC_STATIC_CLIENT_PORT, GetClientDetailLabelText(_T("PORT")));

	UpdateLayoutMetrics();
	if (m_bLayoutReady)
		UpdateLayout();
}


///////////////////////////////////////////////////////////////////////////////
// CClientDetailDialog

IMPLEMENT_DYNAMIC(CClientDetailDialog, CListViewWalkerPropertySheet)

BEGIN_MESSAGE_MAP(CClientDetailDialog, CListViewWalkerPropertySheet)
	ON_WM_DESTROY()
	ON_WM_GETMINMAXINFO()
END_MESSAGE_MAP()

void CClientDetailDialog::Localize()
{
	m_wndClient.Localize();
	SetTabTitle(_T("CD_TITLE"), &m_wndClient, this);
	UpdateMinimumWidth();
}

CClientDetailDialog::CClientDetailDialog(CUpDownClient *pClient, CListCtrlItemWalk *pListCtrl)
	: CListViewWalkerPropertySheet(pListCtrl)
	, m_iMinimumWidth()
{
	if (pListCtrl != NULL)
		AddRuntimeToken(pClient);
	else
		AddTrackedRuntimeToken(pClient);
	Construct();
}

CClientDetailDialog::CClientDetailDialog(const CSimpleArray<CUpDownClient*> *paClients, CListCtrlItemWalk *pListCtrl)
	: CListViewWalkerPropertySheet(pListCtrl)
	, m_iMinimumWidth()
{
	for (int i = 0; i < paClients->GetSize(); ++i) {
		CUpDownClient* pClient = (*paClients)[i];
		if (pListCtrl != NULL)
			AddRuntimeToken(pClient);
		else
			AddTrackedRuntimeToken(pClient);
	}
	Construct();
}

CClientDetailDialog::~CClientDetailDialog()
{
	DestroyOwnedRuntimeTokens();
}

void CClientDetailDialog::AddRuntimeToken(CUpDownClient *pClient)
{
	if (pClient == NULL)
		return;

	CObject* pToken = CreateTrackedClientDetailToken(pClient);
	if (pToken != NULL) {
		m_aOwnedRuntimeTokens.Add(pToken);
		m_aItems.Add(pToken);
	}
}

void CClientDetailDialog::AddTrackedRuntimeToken(CUpDownClient *pClient)
{
	if (pClient == NULL)
		return;

	CObject* pToken = CreateTrackedClientDetailToken(pClient);
	if (pToken != NULL) {
		m_aOwnedRuntimeTokens.Add(pToken);
		m_aItems.Add(pToken);
	}
}

void CClientDetailDialog::DestroyOwnedRuntimeTokens()
{
	for (INT_PTR i = 0; i < m_aOwnedRuntimeTokens.GetCount(); ++i)
		DestroyClientDetailToken(static_cast<CObject*>(m_aOwnedRuntimeTokens[i]));
	m_aOwnedRuntimeTokens.RemoveAll();
}

void CClientDetailDialog::Construct()
{
	m_psh.dwFlags &= ~PSH_HASHELP;
	m_psh.dwFlags |= PSH_NOAPPLYNOW;

	m_wndClient.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndClient.m_psp.dwFlags |= PSP_USEICONID;
	m_wndClient.m_psp.pszIcon = _T("CLIENTDETAILS");
	m_wndClient.SetClients(&m_aItems);
	AddPage(&m_wndClient);
}

void CClientDetailDialog::OnDestroy()
{
	CListViewWalkerPropertySheet::OnDestroy();
	DestroyOwnedRuntimeTokens();
}

void CClientDetailDialog::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	CListViewWalkerPropertySheet::OnGetMinMaxInfo(lpMMI);
	if (m_iMinimumWidth > 0 && lpMMI->ptMinTrackSize.x < m_iMinimumWidth)
		lpMMI->ptMinTrackSize.x = m_iMinimumWidth;
}

void CClientDetailDialog::UpdateMinimumWidth()
{
	if (!m_hWnd || !m_wndClient.GetSafeHwnd())
		return;

	CRect rcWindow;
	CRect rcPage;
	GetWindowRect(&rcWindow);
	m_wndClient.GetClientRect(&rcPage);
	m_iMinimumWidth = rcWindow.Width() - rcPage.Width() + m_wndClient.GetMinimumClientWidth();

	if (rcWindow.Width() < m_iMinimumWidth)
		SetWindowPos(NULL, 0, 0, m_iMinimumWidth, rcWindow.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	m_wndClient.UpdateLayout();
}

BOOL CClientDetailDialog::OnInitDialog()
{
	EnableStackedTabs(FALSE);
	BOOL bResult = CListViewWalkerPropertySheet::OnInitDialog();
	HighColorTab::UpdateImageList(*this);
	InitWindowStyles(this);

	UpdateMinimumWidth();
	EnableSaveRestore(_T("ClientDetailDialogV3")); // call this after(!) OnInitDialog
	UpdateMinimumWidth();

	SetWindowText(GetResString(_T("CD_TITLE")));

	m_tabDark.m_bClosable = false;
	m_tabDark.m_bAllowTabReordering = false;

	if (IsDarkModeEnabled()) {
		HWND hTab = PropSheet_GetTabControl(m_hWnd);
		if (hTab != NULL) {
			::SetWindowTheme(hTab, _T(""), _T(""));
			m_tabDark.SubclassWindow(hTab);
		}

		ApplyTheme(m_hWnd);
	}

	return bResult;
}
