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
#include <share.h>
#include "emule.h"
#include "PPgSecurity.h"
#include "OtherFunctions.h"
#include "IPFilter.h"
#include "Preferences.h"
#include "CustomAutoComplete.h"
#include "HttpDownloadDlg.h"
#include "emuledlg.h"
#include "HelpIDs.h"
#include "ZipFile.h"
#include "GZipFile.h"
#include "RarFile.h"
#include "Log.h"
#include "ServerWnd.h"
#include "ServerListCtrl.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

bool GetMimeType(LPCTSTR pszFilePath, CString &rstrMimeType);

#define	IPFILTERUPDATEURL_STRINGS_PROFILE	_T("AC_IPFilterUpdateURLs.dat")

namespace
{
	CString GetIPFilterUpdateUrlHistoryPath()
	{
		return thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + IPFILTERUPDATEURL_STRINGS_PROFILE;
	}

	void ReloadCurrentIPFilter()
	{
		CWaitCursor curHourglass;
		theApp.ipfilter->LoadFromDefaultFile();
		if (thePrefs.GetFilterServerByIP())
			theApp.emuledlg->serverwnd->serverlistctrl.RemoveAllFilteredServers();
	}

	void ReportIPFilterUpdateError(const CString &strError, bool bInteractive)
	{
		if (bInteractive)
			CDarkMode::MessageBox(strError, MB_ICONERROR);
		else
			AddDebugLogLine(DLP_LOW, false, _T("%s"), (LPCTSTR)strError);
	}

	bool ReplaceDefaultIPFilterFile(const CString &strSourceFilePath, const CString &strTempFileToRemove = CString())
	{
		const CString strDefaultFilePath = CIPFilter::GetDefaultFilePath();
		if (_tremove(strDefaultFilePath) != 0)
			AddDebugLogLine(DLP_LOW, false, _T("Failed to remove default IP filter file \"%s\" - %s"), (LPCTSTR)strDefaultFilePath, _tcserror(errno));
		if (_trename(strSourceFilePath, strDefaultFilePath) != 0) {
			AddDebugLogLine(DLP_LOW, false, _T("Failed to rename IP filter file \"%s\" to \"%s\" - %s"), (LPCTSTR)strSourceFilePath, (LPCTSTR)strDefaultFilePath, _tcserror(errno));
			return false;
		}
		if (!strTempFileToRemove.IsEmpty() && _tremove(strTempFileToRemove) != 0)
			AddDebugLogLine(DLP_LOW, false, _T("Failed to remove temporary IP filter file \"%s\" - %s"), (LPCTSTR)strTempFileToRemove, _tcserror(errno));
		return true;
	}
}

IMPLEMENT_DYNAMIC(CPPgSecurity, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgSecurity, CPropertyPage)
	ON_BN_CLICKED(IDC_FILTER_SERVER_BY_IPFILTER, OnSettingsChange)
	ON_BN_CLICKED(IDC_DONTFILTERPRIVATEIPS, OnSettingsChange)
	ON_BN_CLICKED(IDC_RELOADFILTER, OnReloadIPFilter)
	ON_BN_CLICKED(IDC_EDITFILTER, OnEditIPFilter)
	ON_EN_CHANGE(IDC_FILTERLEVEL, OnSettingsChange)
	ON_BN_CLICKED(IDC_USESECIDENT, OnSettingsChange)
	ON_BN_CLICKED(IDC_LOADURL, OnLoadIPFFromURL)
	ON_EN_CHANGE(IDC_UPDATEURL, OnEnChangeUpdateUrl)
	ON_BN_CLICKED(IDC_DD, OnDDClicked)
	ON_WM_HELPINFO()
	ON_BN_CLICKED(IDC_RUNASUSER, OnBnClickedRunAsUser)
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_SEESHARE1, OnSettingsChange)
	ON_BN_CLICKED(IDC_SEESHARE2, OnSettingsChange)
	ON_BN_CLICKED(IDC_SEESHARE3, OnSettingsChange)
	ON_BN_CLICKED(IDC_ENABLEOBFUSCATION, OnObfuscatedRequestedChange)
	ON_BN_CLICKED(IDC_ONLYOBFUSCATED, OnSettingsChange)
	ON_BN_CLICKED(IDC_DISABLEOBFUSCATION, OnObfuscatedDisabledChange)
	ON_BN_CLICKED(IDC_SEARCHSPAMFILTER, OnSettingsChange)
	ON_BN_CLICKED(IDC_CHECK_FILE_OPEN, OnSettingsChange)
	ON_BN_CLICKED(IDC_AUTOUPDATE_IPFILTER, OnBnClickedAutoupdateIpfilter)
	ON_EN_CHANGE(IDC_IPFILTERPERIOD, OnEnChangeIpfilterperiod)
END_MESSAGE_MAP()

CPPgSecurity::CPPgSecurity()
	: CPropertyPage(CPPgSecurity::IDD)
	, m_pacIPFilterURL()
	, m_bAutoUpdate(false)
	, m_nPeriodDays(7)
{
}

void CPPgSecurity::DoDataExchange(CDataExchange *pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	int nAutoUpdate = m_bAutoUpdate ? 1 : 0;
	DDX_Check(pDX, IDC_AUTOUPDATE_IPFILTER, nAutoUpdate);
	DDX_Text(pDX, IDC_IPFILTERPERIOD, m_nPeriodDays);
	DDV_MinMaxInt(pDX, m_nPeriodDays, 1, 365);
	m_bAutoUpdate = (nAutoUpdate != 0);
}

void CPPgSecurity::LoadSettings()
{
	SetDlgItemInt(IDC_FILTERLEVEL, thePrefs.filterlevel);
	CheckDlgButton(IDC_FILTER_SERVER_BY_IPFILTER, thePrefs.filterserverbyip);
	CheckDlgButton(IDC_DONTFILTERPRIVATEIPS, thePrefs.m_bDontFilterPrivateIPs);

	CheckDlgButton(IDC_USESECIDENT, thePrefs.m_bUseSecureIdent);

	WORD wv = thePrefs.GetWindowsVersion();
	GetDlgItem(IDC_RUNASUSER)->EnableWindow(wv >= _WINVER_2K_ && wv <= _WINVER_2003_ && thePrefs.m_nCurrentUserDirMode == 2);
	CheckDlgButton(IDC_RUNASUSER, thePrefs.IsRunAsUserEnabled());

	CheckDlgButton(IDC_DISABLEOBFUSCATION, static_cast<UINT>(!thePrefs.IsCryptLayerEnabled()));
	GetDlgItem(IDC_ENABLEOBFUSCATION)->EnableWindow(thePrefs.IsCryptLayerEnabled());

	CheckDlgButton(IDC_ENABLEOBFUSCATION, static_cast<UINT>(thePrefs.IsCryptLayerPreferred()));
	GetDlgItem(IDC_ONLYOBFUSCATED)->EnableWindow(thePrefs.IsCryptLayerPreferred());

	CheckDlgButton(IDC_ONLYOBFUSCATED, thePrefs.IsCryptLayerRequired());
	CheckDlgButton(IDC_SEARCHSPAMFILTER, thePrefs.IsSearchSpamFilterEnabled());
	CheckDlgButton(IDC_CHECK_FILE_OPEN, thePrefs.GetCheckFileOpen());

	m_bAutoUpdate = thePrefs.GetAutoIPFilterUpdate();
	m_nPeriodDays = thePrefs.GetIPFilterUpdatePeriodDays();

	ASSERT(vsfaEverybody == 0);
	ASSERT(vsfaFriends == 1);
	ASSERT(vsfaNobody == 2);
	CheckRadioButton(IDC_SEESHARE1, IDC_SEESHARE3, IDC_SEESHARE1 + thePrefs.m_iSeeShares);
}

	BOOL CPPgSecurity::OnInitDialog()
{
	CPropertyPage::OnInitDialog();

	LoadSettings();
	UpdateData(FALSE);
	Localize();

	if (thePrefs.GetUseAutocompletion()) {
		if (!m_pacIPFilterURL) {
			m_pacIPFilterURL = new CCustomAutoComplete();
			m_pacIPFilterURL->AddRef();
			if (m_pacIPFilterURL->Bind(::GetDlgItem(m_hWnd, IDC_UPDATEURL), ACO_UPDOWNKEYDROPSLIST | ACO_AUTOSUGGEST | ACO_FILTERPREFIXES))
				m_pacIPFilterURL->LoadList(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + IPFILTERUPDATEURL_STRINGS_PROFILE);
		}
		SetDlgItemText(IDC_UPDATEURL, m_pacIPFilterURL->GetItem(0));
		if (theApp.m_fontSymbol.m_hObject) {
			GetDlgItem(IDC_DD)->SetFont(&theApp.m_fontSymbol);
			SetDlgItemText(IDC_DD, _T("6")); // show a down-arrow
		}
	} else
		GetDlgItem(IDC_DD)->ShowWindow(SW_HIDE);

	InitWindowStyles(this); // Moved down

	return TRUE;  // return TRUE unless you set the focus to the control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

BOOL CPPgSecurity::OnApply()
{
	if (!UpdateData(TRUE))
		return FALSE;

	UINT uLevel = thePrefs.filterlevel;

	bool bFilter = thePrefs.filterserverbyip;
	thePrefs.filterlevel = GetDlgItemInt(IDC_FILTERLEVEL, NULL, FALSE);
	thePrefs.filterserverbyip = IsDlgButtonChecked(IDC_FILTER_SERVER_BY_IPFILTER) != 0;
	thePrefs.m_bDontFilterPrivateIPs = IsDlgButtonChecked(IDC_DONTFILTERPRIVATEIPS) != 0;
	if (thePrefs.filterserverbyip && (!bFilter || uLevel != thePrefs.filterlevel))
		theApp.emuledlg->serverwnd->serverlistctrl.RemoveAllFilteredServers();

	thePrefs.m_bUseSecureIdent = IsDlgButtonChecked(IDC_USESECIDENT) != 0;
	thePrefs.m_bRunAsUser = IsDlgButtonChecked(IDC_RUNASUSER) != 0;

	thePrefs.m_bCryptLayerRequested = IsDlgButtonChecked(IDC_ENABLEOBFUSCATION) != 0;
	thePrefs.m_bCryptLayerRequired = IsDlgButtonChecked(IDC_ONLYOBFUSCATED) != 0;
	thePrefs.m_bCryptLayerSupported = !IsDlgButtonChecked(IDC_DISABLEOBFUSCATION);
	thePrefs.m_bCheckFileOpen = IsDlgButtonChecked(IDC_CHECK_FILE_OPEN) != 0;
	thePrefs.m_bEnableSearchResultFilter = IsDlgButtonChecked(IDC_SEARCHSPAMFILTER) != 0;


	if (IsDlgButtonChecked(IDC_SEESHARE1))
		thePrefs.m_iSeeShares = vsfaEverybody;
	else if (IsDlgButtonChecked(IDC_SEESHARE2))
		thePrefs.m_iSeeShares = vsfaFriends;
	else
		thePrefs.m_iSeeShares = vsfaNobody;

	thePrefs.SetAutoIPFilterUpdate(m_bAutoUpdate);
	thePrefs.SetIPFilterUpdatePeriodDays(m_nPeriodDays);

	LoadSettings();
	UpdateData(FALSE);
	SetModified(FALSE);
	return CPropertyPage::OnApply();
}

void CPPgSecurity::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(_T("SECURITY")));
		SetDlgItemText(IDC_STATIC_IPFILTER, GetResString(_T("IPFILTER")));
		SetDlgItemText(IDC_RELOADFILTER, GetResString(_T("SF_RELOAD")));
		SetDlgItemText(IDC_EDITFILTER, GetResString(_T("EDIT")));
		SetDlgItemText(IDC_STATIC_FILTERLEVEL, GetResString(_T("FILTERLEVEL")) + _T(':'));
		SetDlgItemText(IDC_FILTER_SERVER_BY_IPFILTER, GetResString(_T("FILTER_SERVER_BY_IPFILTER")));
		SetDlgItemText(IDC_DONTFILTERPRIVATEIPS, GetResString(_T("DONTFILTERPRIVATEIPS")));

		SetDlgItemText(IDC_SEC_MISC, GetResString(_T("PW_MISC")));
		SetDlgItemText(IDC_USESECIDENT, GetResString(_T("USESECIDENT")));
		SetDlgItemText(IDC_RUNASUSER, GetResString(_T("RUNASUSER")));

		SetDlgItemText(IDC_STATIC_UPDATEFROM, GetResString(_T("UPDATEFROM")));
		SetDlgItemText(IDC_LOADURL, GetResString(_T("LOADURL")));

		SetDlgItemText(IDC_SEEMYSHARE_FRM, GetResString(_T("PW_SHARE")));
		SetDlgItemText(IDC_SEESHARE1, GetResString(_T("PW_EVER")));
		SetDlgItemText(IDC_SEESHARE2, GetResString(_T("FSTATUS_FRIENDSONLY")));
		SetDlgItemText(IDC_SEESHARE3, GetResString(_T("PW_NOONE")));

		SetDlgItemText(IDC_DISABLEOBFUSCATION, GetResString(_T("DISABLEOBFUSCATION")));
		SetDlgItemText(IDC_ONLYOBFUSCATED, GetResString(_T("ONLYOBFUSCATED")));
		SetDlgItemText(IDC_ENABLEOBFUSCATION, GetResString(_T("ENABLEOBFUSCATION")));
		SetDlgItemText(IDC_SEC_OBFUSCATIONBOX, GetResString(_T("PROTOCOLOBFUSCATION")));
		SetDlgItemText(IDC_SEARCHSPAMFILTER, GetResString(_T("SEARCHSPAMFILTER")));
		SetDlgItemText(IDC_CHECK_FILE_OPEN, GetResString(_T("CHECK_FILE_OPEN")));
		SetDlgItemText(IDC_AUTOUPDATE_IPFILTER, GetResString(_T("IPFILTER_AUTO_UPDATE")));
		SetDlgItemText(IDC_PERIODDAYS_LABEL, GetResString(_T("IPFILTER_PERIOD_DAYS")));
	}
}

void CPPgSecurity::OnReloadIPFilter()
{
	ReloadCurrentIPFilter();
}

void CPPgSecurity::OnEditIPFilter()
{
	ShellOpen(thePrefs.GetTxtEditor(), _T('"') + CIPFilter::GetDefaultFilePath() + _T('"'));
}

CString CPPgSecurity::GetStoredIPFilterUpdateURL()
{
	CCustomAutoComplete acIPFilterURL;
	if (acIPFilterURL.LoadList(GetIPFilterUpdateUrlHistoryPath()) && acIPFilterURL.GetItemCount() > 0)
		return acIPFilterURL.GetItem(0);
	return CString();
}

bool CPPgSecurity::UpdateIPFilterFromURL(const CString &url, bool bInteractive)
{
	if (url.IsEmpty())
		return false;

	bool bHaveNewFilterFile = false;
	const CString &sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	CString strTempFilePath;
	_tmakepathlimit(strTempFilePath.GetBuffer(MAX_PATH), NULL, sConfDir, DFLT_IPFILTER_FILENAME, _T("tmp"));
	strTempFilePath.ReleaseBuffer();

	CHttpDownloadDlg dlgDownload;
	dlgDownload.m_strTitle = GetResString(_T("DOWNLOADING_IPFILTER_FILE"));
	dlgDownload.m_sURLToDownload = url;
	dlgDownload.m_sFileToDownloadInto = strTempFilePath;
	if (dlgDownload.DoModal() != IDOK) {
		(void)_tremove(strTempFilePath);
		CString strError(GetResString(_T("IPFILTER_DOWNLOAD_FAILED")));
		if (!dlgDownload.GetError().IsEmpty())
			strError.AppendFormat(_T("\r\n\r\n%s"), (LPCTSTR)dlgDownload.GetError());
		ReportIPFilterUpdateError(strError, bInteractive);
		return false;
	}

	CString strMimeType;
	GetMimeType(strTempFilePath, strMimeType);

	bool bIsArchiveFile = false;
	bool bUncompressed = false;
	CZIPFile zip;
	if (zip.Open(strTempFilePath)) {
		bIsArchiveFile = true;

		CZIPFile::File *zfile = zip.GetFile(_T("ipfilter.dat"));
		if (zfile == NULL) {
			zfile = zip.GetFile(_T("guarding.p2p"));
			if (zfile == NULL)
				zfile = zip.GetFile(_T("guardian.p2p"));
		}
		if (zfile) {
			CString strTempUnzipFilePath;
			_tmakepathlimit(strTempUnzipFilePath.GetBuffer(MAX_PATH), NULL, sConfDir, DFLT_IPFILTER_FILENAME, _T(".unzip.tmp"));
			strTempUnzipFilePath.ReleaseBuffer();

			if (zfile->Extract(strTempUnzipFilePath)) {
				zip.Close();
				bUncompressed = true;
				bHaveNewFilterFile = ReplaceDefaultIPFilterFile(strTempUnzipFilePath, strTempFilePath);
			} else {
				CString strError;
				strError.Format(GetResString(_T("IPFILTER_ZIP_ERROR")), (LPCTSTR)strTempFilePath);
				ReportIPFilterUpdateError(strError, bInteractive);
			}
		} else {
			CString strError;
			strError.Format(GetResString(_T("IPFILTER_CONTENT_ERROR")), (LPCTSTR)strTempFilePath);
			ReportIPFilterUpdateError(strError, bInteractive);
		}

		zip.Close();
	} else if (strMimeType.CompareNoCase(_T("application/x-rar-compressed")) == 0) {
		bIsArchiveFile = true;

		CRARFile rar;
		if (rar.Open(strTempFilePath)) {
			CString strFile;
			if (rar.GetNextFile(strFile)
				&& (strFile.CompareNoCase(_T("ipfilter.dat")) == 0
					|| strFile.CompareNoCase(_T("guarding.p2p")) == 0
					|| strFile.CompareNoCase(_T("guardian.p2p")) == 0))
			{
				CString strTempUnzipFilePath;
				_tmakepathlimit(strTempUnzipFilePath.GetBuffer(MAX_PATH), NULL, sConfDir, DFLT_IPFILTER_FILENAME, _T(".unzip.tmp"));
				strTempUnzipFilePath.ReleaseBuffer();
				if (rar.Extract(strTempUnzipFilePath)) {
					rar.Close();
					bUncompressed = true;
					bHaveNewFilterFile = ReplaceDefaultIPFilterFile(strTempUnzipFilePath, strTempFilePath);
				} else {
					CString strError;
					strError.Format(_T("Failed to extract IP filter file from RAR file \"%s\"."), (LPCTSTR)strTempFilePath);
					ReportIPFilterUpdateError(strError, bInteractive);
				}
			} else {
				CString strError;
				strError.Format(_T("Failed to find IP filter file \"guarding.p2p\" or \"ipfilter.dat\" in RAR file \"%s\"."), (LPCTSTR)strTempFilePath);
				ReportIPFilterUpdateError(strError, bInteractive);
			}
			rar.Close();
		} else {
			CString strError;
			strError.Format(_T("Failed to open file \"%s\".\r\n\r\nInvalid file format?\r\n\r\n%s"), (LPCTSTR)url, CRARFile::sUnrar_download);
			ReportIPFilterUpdateError(strError, bInteractive);
		}
	} else {
		CGZIPFile gz;
		if (gz.Open(strTempFilePath)) {
			bIsArchiveFile = true;

			CString strTempUnzipFilePath;
			_tmakepathlimit(strTempUnzipFilePath.GetBuffer(MAX_PATH), NULL, sConfDir, DFLT_IPFILTER_FILENAME, _T(".unzip.tmp"));
			strTempUnzipFilePath.ReleaseBuffer();

			// Add filename and extension of uncompressed file to temporary file.
			const CString &strUncompressedFileName(gz.GetUncompressedFileName());
			if (!strUncompressedFileName.IsEmpty())
				strTempUnzipFilePath.AppendFormat(_T(".%s"), (LPCTSTR)strUncompressedFileName);

			if (gz.Extract(strTempUnzipFilePath)) {
				gz.Close();
				bUncompressed = true;
				bHaveNewFilterFile = ReplaceDefaultIPFilterFile(strTempUnzipFilePath, strTempFilePath);
			} else {
				CString strError;
				strError.Format(GetResString(_T("IPFILTER_ZIP_ERROR")), (LPCTSTR)strTempFilePath);
				ReportIPFilterUpdateError(strError, bInteractive);
			}
		}
		gz.Close();
	}

	if (!bIsArchiveFile && !bUncompressed) {
		// Check first lines of downloaded file for potential HTML content (e.g. 404 error pages).
		bool bValidIPFilterFile = true;
		FILE *fp = _tfsopen(strTempFilePath, _T("rb"), _SH_DENYWR);
		if (fp) {
			char szBuff[16384];
			size_t iRead = fread(szBuff, 1, sizeof szBuff - 1, fp);
			fclose(fp);
			if (iRead <= 0)
				bValidIPFilterFile = false;
			else {
				szBuff[iRead - 1] = '\0';

				const char *pc = szBuff;
				while (*pc && *pc <= ' ')
					++pc;
				if (_strnicmp(pc, "<html", 5) == 0 || _strnicmp(pc, "<xml", 4) == 0 || _strnicmp(pc, "<!doc", 5) == 0)
					bValidIPFilterFile = false;
			}
		}

		if (bValidIPFilterFile)
			bHaveNewFilterFile = ReplaceDefaultIPFilterFile(strTempFilePath);
		else
			ReportIPFilterUpdateError(GetResString(_T("IPFILTER_DOWNLOAD_FAILED")), bInteractive);
	}

	if (!bHaveNewFilterFile)
		return false;

	ReloadCurrentIPFilter();

	// Warn if the new file left the filter list empty.
	if (theApp.ipfilter->GetIPFilter().IsEmpty()) {
		CString strLoaded;
		strLoaded.Format(GetResString(_T("IPFILTER_LOADED")), theApp.ipfilter->GetIPFilter().GetCount());
		CString strError(GetResString(_T("IPFILTER_DOWNLOAD_FAILED")));
		strError.AppendFormat(_T("\r\n\r\n%s"), (LPCTSTR)strLoaded);
		ReportIPFilterUpdateError(strError, bInteractive);
		return false;
	}

	thePrefs.SetLastIPFilterUpdate(time(nullptr));
	return true;
}

void CPPgSecurity::OnLoadIPFFromURL()
{
	CString url;
	GetDlgItemText(IDC_UPDATEURL, url);
	if (url.IsEmpty()) {
		OnReloadIPFilter();
		return;
	}

	// Add entered URL to the LRU list even if download still fails.
	if (m_pacIPFilterURL && m_pacIPFilterURL->IsBound())
		m_pacIPFilterURL->AddItem(url, 0);

	UpdateIPFilterFromURL(url, true);
}

void CPPgSecurity::OnDestroy()
{
	DeleteDDB();
	CPropertyPage::OnDestroy();
}

void CPPgSecurity::DeleteDDB()
{
	if (m_pacIPFilterURL) {
		m_pacIPFilterURL->SaveList(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + IPFILTERUPDATEURL_STRINGS_PROFILE);
		m_pacIPFilterURL->Unbind();
		m_pacIPFilterURL->Release();
		m_pacIPFilterURL = NULL;
	}
}

BOOL CPPgSecurity::PreTranslateMessage(MSG *pMsg)
{
	if (pMsg->message == WM_KEYDOWN) {

		if (pMsg->wParam == VK_ESCAPE)
			return FALSE;

		if (pMsg->hwnd == GetDlgItem(IDC_UPDATEURL)->m_hWnd) {
			switch (pMsg->wParam) {
			case VK_RETURN:
				if (m_pacIPFilterURL && m_pacIPFilterURL->IsBound()) {
					CString strText;
					GetDlgItemText(IDC_UPDATEURL, strText);
					if (!strText.IsEmpty()) {
						SetDlgItemText(IDC_UPDATEURL, EMPTY); // this seems to be the only chance to let the drop-down list to disappear
						SetDlgItemText(IDC_UPDATEURL, strText);
						static_cast<CEdit*>(GetDlgItem(IDC_UPDATEURL))->SetSel(strText.GetLength(), strText.GetLength());
					}
				}
				return TRUE;
			case VK_DELETE:
				// Fix: Avoid stack corruption. GetKeyState is enough to test modifiers.
				const SHORT sCtrl = GetKeyState(VK_CONTROL);
				const SHORT sLCtrl = GetKeyState(VK_LCONTROL);
				const SHORT sRCtrl = GetKeyState(VK_RCONTROL);
				const SHORT sAlt = GetKeyState(VK_MENU);
				const SHORT sLAlt = GetKeyState(VK_LMENU);
				const SHORT sRAlt = GetKeyState(VK_RMENU);
				const bool  bCtrl = ((sCtrl | sLCtrl | sRCtrl) & 0x8000) != 0;
				const bool  bAlt = ((sAlt | sLAlt | sRAlt) & 0x8000) != 0;

				if (bCtrl || bAlt)
					m_pacIPFilterURL->Clear();
				else
					m_pacIPFilterURL->RemoveSelectedItem();
			}
		}
	}

	return CPropertyPage::PreTranslateMessage(pMsg);
}

void CPPgSecurity::OnEnChangeUpdateUrl()
{
	CString strUrl;
	GetDlgItemText(IDC_UPDATEURL, strUrl);
	GetDlgItem(IDC_LOADURL)->EnableWindow(!strUrl.IsEmpty());
}

void CPPgSecurity::OnDDClicked()
{
	CWnd *box = GetDlgItem(IDC_UPDATEURL);
	box->SetFocus();
	box->SetWindowText(EMPTY);
	box->SendMessage(WM_KEYDOWN, VK_DOWN, 0x00510001);
}

void CPPgSecurity::OnHelp()
{
	theApp.ShowHelp(eMule_FAQ_Preferences_Security);
}

BOOL CPPgSecurity::OnCommand(WPARAM wParam, LPARAM lParam)
{
	return (wParam == ID_HELP) ? OnHelpInfo(NULL) : __super::OnCommand(wParam, lParam);
}

BOOL CPPgSecurity::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}

void CPPgSecurity::OnBnClickedRunAsUser()
{
	if (IsDlgButtonChecked(IDC_RUNASUSER))
		if (LocMessageBox(_T("RAU_WARNING"), MB_OKCANCEL | MB_ICONINFORMATION, 0) == IDCANCEL)
			CheckDlgButton(IDC_RUNASUSER, BST_UNCHECKED);

	OnSettingsChange();
}

void CPPgSecurity::OnObfuscatedDisabledChange()
{
	GetDlgItem(IDC_ENABLEOBFUSCATION)->EnableWindow(!IsDlgButtonChecked(IDC_DISABLEOBFUSCATION));
	if (IsDlgButtonChecked(IDC_DISABLEOBFUSCATION)) {
		GetDlgItem(IDC_ONLYOBFUSCATED)->EnableWindow(FALSE);
		CheckDlgButton(IDC_ENABLEOBFUSCATION, 0);
		CheckDlgButton(IDC_ONLYOBFUSCATED, 0);
	}
	OnSettingsChange();
}

void CPPgSecurity::OnObfuscatedRequestedChange()
{
	bool bCheck = IsDlgButtonChecked(IDC_ENABLEOBFUSCATION) != 0;
	if (bCheck)
		GetDlgItem(IDC_ENABLEOBFUSCATION)->EnableWindow(bCheck);
	else
		CheckDlgButton(IDC_ONLYOBFUSCATED, bCheck);
	GetDlgItem(IDC_ONLYOBFUSCATED)->EnableWindow(bCheck);
	OnSettingsChange();
}

void CPPgSecurity::OnBnClickedAutoupdateIpfilter()
{
	SetModified();
}

void CPPgSecurity::OnEnChangeIpfilterperiod()
{
	SetModified();
}
