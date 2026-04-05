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

IMPLEMENT_DYNAMIC(CPPgSecurity, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgSecurity, CPropertyPage)
	ON_BN_CLICKED(IDC_FILTERSERVERBYIPFILTER, OnSettingsChange)
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
END_MESSAGE_MAP()

CPPgSecurity::CPPgSecurity()
	: CPropertyPage(CPPgSecurity::IDD)
	, m_pacIPFilterURL()
{
}

void CPPgSecurity::DoDataExchange(CDataExchange *pDX)
{
	CPropertyPage::DoDataExchange(pDX);
}

void CPPgSecurity::LoadSettings()
{
	SetDlgItemInt(IDC_FILTERLEVEL, thePrefs.filterlevel);
	CheckDlgButton(IDC_FILTERSERVERBYIPFILTER, thePrefs.filterserverbyip);
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

	ASSERT(vsfaEverybody == 0);
	ASSERT(vsfaFriends == 1);
	ASSERT(vsfaNobody == 2);
	CheckRadioButton(IDC_SEESHARE1, IDC_SEESHARE3, IDC_SEESHARE1 + thePrefs.m_iSeeShares);
}

BOOL CPPgSecurity::OnInitDialog()
{
	CPropertyPage::OnInitDialog();

	LoadSettings();
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

	UINT uLevel = thePrefs.filterlevel;

	bool bFilter = thePrefs.filterserverbyip;
	thePrefs.filterlevel = GetDlgItemInt(IDC_FILTERLEVEL, NULL, FALSE);
	thePrefs.filterserverbyip = IsDlgButtonChecked(IDC_FILTERSERVERBYIPFILTER) != 0;
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

	LoadSettings();
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
		SetDlgItemText(IDC_FILTERSERVERBYIPFILTER, GetResString(_T("FILTERSERVERBYIPFILTER")));
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
	}
}

void CPPgSecurity::OnReloadIPFilter()
{
	CWaitCursor curHourglass;
	theApp.ipfilter->LoadFromDefaultFile();
	if (thePrefs.GetFilterServerByIP())
		theApp.emuledlg->serverwnd->serverlistctrl.RemoveAllFilteredServers();
}

void CPPgSecurity::OnEditIPFilter()
{
	ShellOpen(thePrefs.GetTxtEditor(), _T('"') + CIPFilter::GetDefaultFilePath() + _T('"'));
}

void CPPgSecurity::OnLoadIPFFromURL()
{
	bool bHaveNewFilterFile = false;
	CString url;
	GetDlgItemText(IDC_UPDATEURL, url);
	if (!url.IsEmpty()) {
		// add entered URL to LRU list even if it's not yet known whether we can download from this URL (it's just more convenient this way)
		if (m_pacIPFilterURL && m_pacIPFilterURL->IsBound())
			m_pacIPFilterURL->AddItem(url, 0);

		const CString &sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
		CString strTempFilePath;
		_tmakepathlimit(strTempFilePath.GetBuffer(MAX_PATH), NULL, sConfDir, DFLT_IPFILTER_FILENAME, _T("tmp"));
		strTempFilePath.ReleaseBuffer();

		CHttpDownloadDlg dlgDownload;
		dlgDownload.m_strTitle = GetResString(_T("DWL_IPFILTERFILE"));
		dlgDownload.m_sURLToDownload = url;
		dlgDownload.m_sFileToDownloadInto = strTempFilePath;
		if (dlgDownload.DoModal() != IDOK) {
			(void)_tremove(strTempFilePath);
			CString strError(GetResString(_T("DWLIPFILTERFAILED")));
			if (!dlgDownload.GetError().IsEmpty())
				strError.AppendFormat(_T("\r\n\r\n%s"), (LPCTSTR)dlgDownload.GetError());
			CDarkMode::MessageBox(strError, MB_ICONERROR);
			return;
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

					if (_tremove(theApp.ipfilter->GetDefaultFilePath()) != 0)
						AddDebugLogLine(DLP_LOW, false, _T("Failed to remove default IP filter file \"%s\" - %s"), (LPCTSTR)theApp.ipfilter->GetDefaultFilePath(), _tcserror(errno));
					if (_trename(strTempUnzipFilePath, theApp.ipfilter->GetDefaultFilePath()) != 0)
						AddDebugLogLine(DLP_LOW, false, _T("Failed to rename uncompressed IP filter file \"%s\" to default IP filter file \"%s\" - %s"), (LPCTSTR)strTempUnzipFilePath, (LPCTSTR)theApp.ipfilter->GetDefaultFilePath(), _tcserror(errno));
					if (_tremove(strTempFilePath) != 0)
						AddDebugLogLine(DLP_LOW, false, _T("Failed to remove temporary IP filter file \"%s\" - %s"), (LPCTSTR)strTempFilePath, _tcserror(errno));
					bUncompressed = true;
					bHaveNewFilterFile = true;
				} else {
					CString strError;
					strError.Format(GetResString(_T("ERR_IPFILTERZIPEXTR")), (LPCTSTR)strTempFilePath);
					CDarkMode::MessageBox(strError, MB_ICONERROR);
				}
			} else {
				CString strError;
				strError.Format(GetResString(_T("ERR_IPFILTERCONTENTERR")), (LPCTSTR)strTempFilePath);
				CDarkMode::MessageBox(strError, MB_ICONERROR);
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

						if (_tremove(theApp.ipfilter->GetDefaultFilePath()) != 0)
							AddDebugLogLine(DLP_LOW, false, _T("Failed to remove default IP filter file \"%s\" - %s"), (LPCTSTR)theApp.ipfilter->GetDefaultFilePath(), _tcserror(errno));
						if (_trename(strTempUnzipFilePath, theApp.ipfilter->GetDefaultFilePath()) != 0)
							AddDebugLogLine(DLP_LOW, false, _T("Failed to rename uncompressed IP filter file \"%s\" to default IP filter file \"%s\" - %s"), (LPCTSTR)strTempUnzipFilePath, (LPCTSTR)theApp.ipfilter->GetDefaultFilePath(), _tcserror(errno));
						if (_tremove(strTempFilePath) != 0)
							AddDebugLogLine(DLP_LOW, false, _T("Failed to remove temporary IP filter file \"%s\" - %s"), (LPCTSTR)strTempFilePath, _tcserror(errno));
						bUncompressed = true;
						bHaveNewFilterFile = true;
					} else {
						CString strError;
						strError.Format(_T("Failed to extract IP filter file from RAR file \"%s\"."), (LPCTSTR)strTempFilePath);
						CDarkMode::MessageBox(strError, MB_ICONERROR);
					}
				} else {
					CString strError;
					strError.Format(_T("Failed to find IP filter file \"guarding.p2p\" or \"ipfilter.dat\" in RAR file \"%s\"."), (LPCTSTR)strTempFilePath);
					CDarkMode::MessageBox(strError, MB_ICONERROR);
				}
				rar.Close();
			} else {
				CString strError;
				strError.Format(_T("Failed to open file \"%s\".\r\n\r\nInvalid file format?\r\n\r\n%s"), (LPCTSTR)url, CRARFile::sUnrar_download);
				CDarkMode::MessageBox(strError, MB_ICONERROR);
			}
		} else {
			CGZIPFile gz;
			if (gz.Open(strTempFilePath)) {
				bIsArchiveFile = true;

				CString strTempUnzipFilePath;
				_tmakepathlimit(strTempUnzipFilePath.GetBuffer(MAX_PATH), NULL, sConfDir, DFLT_IPFILTER_FILENAME, _T(".unzip.tmp"));
				strTempUnzipFilePath.ReleaseBuffer();

				// add filename and extension of uncompressed file to temporary file
				const CString &strUncompressedFileName(gz.GetUncompressedFileName());
				if (!strUncompressedFileName.IsEmpty())
					strTempUnzipFilePath.AppendFormat(_T(".%s"), (LPCTSTR)strUncompressedFileName);

				if (gz.Extract(strTempUnzipFilePath)) {
					gz.Close();

					if (_tremove(theApp.ipfilter->GetDefaultFilePath()) != 0)
						AddDebugLogLine(DLP_LOW, false, _T("Failed to remove default IP filter file \"%s\" - %s"), (LPCTSTR)theApp.ipfilter->GetDefaultFilePath(), _tcserror(errno));
					if (_trename(strTempUnzipFilePath, theApp.ipfilter->GetDefaultFilePath()) != 0)
						AddDebugLogLine(DLP_LOW, false, _T("Failed to rename uncompressed IP filter file \"%s\" to default IP filter file \"%s\" - %s"), (LPCTSTR)strTempUnzipFilePath, (LPCTSTR)theApp.ipfilter->GetDefaultFilePath(), _tcserror(errno));
					if (_tremove(strTempFilePath) != 0)
						AddDebugLogLine(DLP_LOW, false, _T("Failed to remove temporary IP filter file \"%s\" - %s"), (LPCTSTR)strTempFilePath, _tcserror(errno));
					bUncompressed = true;
					bHaveNewFilterFile = true;
				} else {
					CString strError;
					strError.Format(GetResString(_T("ERR_IPFILTERZIPEXTR")), (LPCTSTR)strTempFilePath);
					CDarkMode::MessageBox(strError, MB_ICONERROR);
				}
			}
			gz.Close();
		}

		if (!bIsArchiveFile && !bUncompressed) {
			// Check first lines of downloaded file for potential HTML content (e.g. 404 error pages)
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

			if (bValidIPFilterFile) {
				(void)_tremove(theApp.ipfilter->GetDefaultFilePath());
				VERIFY(_trename(strTempFilePath, theApp.ipfilter->GetDefaultFilePath()) == 0);
				bHaveNewFilterFile = true;
			} else
				LocMessageBox(_T("DWLIPFILTERFAILED"), MB_ICONERROR, 0);
		}
	}

	if (url.IsEmpty() || bHaveNewFilterFile)
		OnReloadIPFilter();

	// In case we received an invalid IP filter file (e.g. an 404 HTML page with HTTP status "OK"),
	// warn the user that there are no IP filters available any longer.
	if (bHaveNewFilterFile && theApp.ipfilter->GetIPFilter().IsEmpty()) {
		CString strLoaded;
		strLoaded.Format(GetResString(_T("IPFILTERLOADED")), theApp.ipfilter->GetIPFilter().GetCount());
		CString strError(GetResString(_T("DWLIPFILTERFAILED")));
		strError.AppendFormat(_T("\r\n\r\n%s"), (LPCTSTR)strLoaded);
		CDarkMode::MessageBox(strError, MB_ICONERROR);
	}
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
