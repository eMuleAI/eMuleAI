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
#include <afxinet.h>
#include <share.h>
#include "emule.h"
#include "PPgSecurity.h"
#include "OtherFunctions.h"
#include "IPFilter.h"
#include "Preferences.h"
#include "CustomAutoComplete.h"
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

#define	IPFILTERUPDATEURL_STRINGS_PROFILE	AC_IPFILTER_UPDATE_URLS_FILENAME

static bool ApplyDownloadedIPFilterFile(const CString &url, const CString &strTempFilePath, bool bInteractive);

namespace
{
	const DWORD IPFILTER_DOWNLOAD_PROGRESS_INTERVAL = 250;

	struct SIPFilterDownloadJob
	{
		SIPFilterDownloadJob()
			: lRefCount(1)
			, lCancel()
			, hNotifyWnd()
			, uToken()
			, bInteractive(false)
		{
		}

		LONG lRefCount;
		volatile LONG lCancel;
		HWND hNotifyWnd;
		uint64 uToken;
		bool bInteractive;
		CString strURL;
		CString strTempFile;
	};

	struct SIPFilterDownloadProgress
	{
		SIPFilterDownloadProgress()
			: uToken()
			, uBytesRead()
			, uTotalBytes()
			, bHasTotalBytes(false)
		{
		}

		uint64 uToken;
		uint64 uBytesRead;
		uint64 uTotalBytes;
		bool bHasTotalBytes;
		CString strURL;
	};

	struct SIPFilterDownloadResult
	{
		SIPFilterDownloadResult()
			: uToken()
			, uBytesRead()
			, uTotalBytes()
			, bHasTotalBytes(false)
			, bSucceeded(false)
			, bCanceled(false)
			, bInteractive(false)
		{
		}

		uint64 uToken;
		uint64 uBytesRead;
		uint64 uTotalBytes;
		bool bHasTotalBytes;
		bool bSucceeded;
		bool bCanceled;
		bool bInteractive;
		CString strURL;
		CString strTempFile;
		CString strError;
	};

	SIPFilterDownloadJob* g_pIPFilterDownloadJob = NULL;
	uint64 g_uIPFilterDownloadToken = 0;
	uint64 g_uIPFilterDownloadBytesRead = 0;
	uint64 g_uIPFilterDownloadTotalBytes = 0;
	bool g_bIPFilterDownloadHasTotalBytes = false;
	bool g_bIPFilterDownloadOverlayDelayActive = false;
	CString g_strIPFilterDownloadURL;

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

	void AddIPFilterDownloadJobRef(SIPFilterDownloadJob* pJob)
	{
		if (pJob == NULL)
			return;
		InterlockedIncrement(&pJob->lRefCount);
	}

	void ReleaseIPFilterDownloadJob(SIPFilterDownloadJob* pJob)
	{
		if (pJob != NULL && InterlockedDecrement(&pJob->lRefCount) == 0)
			delete pJob;
	}

	bool IsIPFilterDownloadCanceled(SIPFilterDownloadJob* pJob)
	{
		return pJob == NULL || InterlockedCompareExchange(&pJob->lCancel, 0, 0) != 0;
	}

	void SetIPFilterDownloadError(SIPFilterDownloadResult& result, LPCTSTR pszContext, DWORD dwError)
	{
		CString strLastError;
		if (dwError >= INTERNET_ERROR_BASE && dwError <= INTERNET_ERROR_LAST)
			GetModuleErrorString(dwError, strLastError, _T("wininet"));
		else
			GetSystemErrorString(dwError, strLastError);

		if (strLastError.IsEmpty())
			result.strError = pszContext;
		else
			result.strError.Format(_T("%s: %s"), pszContext, (LPCTSTR)strLastError);
	}

	bool PostIPFilterDownloadPayload(SIPFilterDownloadJob* pJob, UINT uMessage, LPARAM lParam)
	{
		HWND hWnd = pJob != NULL ? pJob->hNotifyWnd : NULL;
		return hWnd != NULL && ::IsWindow(hWnd) && ::PostMessage(hWnd, uMessage, 0, lParam);
	}

	void PostIPFilterDownloadProgress(SIPFilterDownloadJob* pJob, uint64 uBytesRead, uint64 uTotalBytes, bool bHasTotalBytes, DWORD& dwLastProgressTick, bool bForce)
	{
		const DWORD dwNow = ::GetTickCount();
		if (!bForce && dwLastProgressTick != 0 && dwNow - dwLastProgressTick < IPFILTER_DOWNLOAD_PROGRESS_INTERVAL)
			return;

		SIPFilterDownloadProgress* pProgress = new SIPFilterDownloadProgress;
		pProgress->uToken = pJob->uToken;
		pProgress->uBytesRead = uBytesRead;
		pProgress->uTotalBytes = uTotalBytes;
		pProgress->bHasTotalBytes = bHasTotalBytes;
		pProgress->strURL = pJob->strURL;

		if (PostIPFilterDownloadPayload(pJob, CemuleDlg::UWM_EMULEAI_IPFILTER_DOWNLOAD_PROGRESS, reinterpret_cast<LPARAM>(pProgress)))
			dwLastProgressTick = dwNow;
		else
			delete pProgress;
	}

	void BuildIPFilterDownloadOverlayText(uint64 uBytesRead, uint64 uTotalBytes, bool bHasTotalBytes, const CString& strURL, CString& strTitle, CString& strDetail, UINT& uDone, UINT& uTotal)
	{
		uDone = 0;
		uTotal = 0;
		if (bHasTotalBytes && uTotalBytes > 0) {
			const uint64 uClampedBytesRead = uBytesRead < uTotalBytes ? uBytesRead : uTotalBytes;
			uint64 uPercent64 = (uClampedBytesRead * 100) / uTotalBytes;
			if (uPercent64 > 100)
				uPercent64 = 100;
			const UINT uPercent = static_cast<UINT>(uPercent64);
			CString strBytesRead = CastItoXBytes(uBytesRead);
			CString strTotalBytes = CastItoXBytes(uTotalBytes);
			strDetail.Format(GetResString(_T("SERVERMET_DOWNLOAD_PROGRESS")), uPercent, (LPCTSTR)strBytesRead, (LPCTSTR)strTotalBytes, (LPCTSTR)strURL);
			uDone = uPercent;
			uTotal = 100;
		} else {
			CString strBytesRead = CastItoXBytes(uBytesRead);
			strDetail.Format(GetResString(_T("SERVERMET_DOWNLOAD_PROGRESS_UNKNOWN")), (LPCTSTR)strBytesRead, (LPCTSTR)strURL);
		}

		strTitle = GetResString(_T("DOWNLOADING_IPFILTER_FILE"));
	}

	void UpdateIPFilterDownloadOverlays(uint64 uBytesRead, uint64 uTotalBytes, bool bHasTotalBytes, const CString& strURL)
	{
		g_uIPFilterDownloadBytesRead = uBytesRead;
		g_uIPFilterDownloadTotalBytes = uTotalBytes;
		g_bIPFilterDownloadHasTotalBytes = bHasTotalBytes;
		g_strIPFilterDownloadURL = strURL;
		if (theApp.emuledlg != NULL)
			theApp.emuledlg->RefreshActiveBulkOperationOverlays();
	}

	void ResetIPFilterDownloadOverlayState()
	{
		g_uIPFilterDownloadBytesRead = 0;
		g_uIPFilterDownloadTotalBytes = 0;
		g_bIPFilterDownloadHasTotalBytes = false;
		g_bIPFilterDownloadOverlayDelayActive = false;
		g_strIPFilterDownloadURL.Empty();
	}

	void StartIPFilterDownloadOverlayDelay()
	{
		g_bIPFilterDownloadOverlayDelayActive = true;
		if (theApp.emuledlg != NULL)
			theApp.emuledlg->StartDownloadOverlayCompletionDelay();
	}

	void HideIPFilterDownloadOverlays()
	{
		ResetIPFilterDownloadOverlayState();
		if (theApp.emuledlg != NULL)
			theApp.emuledlg->RefreshActiveBulkOperationOverlays();
	}

	bool DownloadIPFilterToFile(SIPFilterDownloadJob* pJob, SIPFilterDownloadResult& result)
	{
		result.uToken = pJob->uToken;
		result.strURL = pJob->strURL;
		result.strTempFile = pJob->strTempFile;
		result.bInteractive = pJob->bInteractive;
		if (theApp.IsNetworkActivityBlockedByBind()) {
			result.strError = theApp.GetNetworkActivityBlockMessage();
			return false;
		}

		DWORD dwServiceType = 0;
		CString strServer;
		CString strObject;
		INTERNET_PORT nPort = 0;
		CString strURL(pJob->strURL);
		if (!AfxParseURL(strURL, dwServiceType, strServer, strObject, nPort)) {
			strURL = _T("http://") + strURL;
			if (!AfxParseURL(strURL, dwServiceType, strServer, strObject, nPort)) {
				result.strError = GetResString(_T("INVALIDURL"));
				return false;
			}
		}
		if (dwServiceType != AFX_INET_SERVICE_HTTP && dwServiceType != AFX_INET_SERVICE_HTTPS) {
			result.strError = GetResString(_T("INVALIDURL"));
			return false;
		}
		if (strObject.IsEmpty())
			strObject = _T("/");

		CFile file;
		bool bFileOpen = false;
		HINTERNET hInternetSession = NULL;
		HINTERNET hHttpConnection = NULL;
		HINTERNET hHttpFile = NULL;
		bool bSuccess = false;
		bool bDeleteTempFile = true;
		DWORD dwLastProgressTick = 0;
		DWORD dwTimeout = SEC2MS(30);
		DWORD dwRequestFlags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_DONT_CACHE | INTERNET_FLAG_KEEP_CONNECTION;
		static LPCTSTR ppszAcceptTypes[2] = {_T("*/*"), NULL};
		TCHAR szStatusCode[32] = {};
		TCHAR szContentLength[64] = {};
		DWORD dwInfoSize = 0;
		long nStatusCode = 0;
		char szReadBuf[16 * 1024];

		CFileException fex;
		if (!file.Open(PreparePathForWin32LongPath(pJob->strTempFile), CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite, &fex)) {
			SetIPFilterDownloadError(result, _T("Failed to open download target"), fex.m_lOsError);
			goto cleanup;
		}
		bFileOpen = true;

		hInternetSession = ::InternetOpen(AfxGetAppName(), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
		if (hInternetSession == NULL) {
			SetIPFilterDownloadError(result, _T("InternetOpen failed"), ::GetLastError());
			goto cleanup;
		}

		(void)::InternetSetOption(hInternetSession, INTERNET_OPTION_CONNECT_TIMEOUT, &dwTimeout, sizeof dwTimeout);
		(void)::InternetSetOption(hInternetSession, INTERNET_OPTION_SEND_TIMEOUT, &dwTimeout, sizeof dwTimeout);
		(void)::InternetSetOption(hInternetSession, INTERNET_OPTION_RECEIVE_TIMEOUT, &dwTimeout, sizeof dwTimeout);

		if (IsIPFilterDownloadCanceled(pJob)) {
			result.bCanceled = true;
			goto cleanup;
		}

		if (dwServiceType == AFX_INET_SERVICE_HTTPS) {
			dwRequestFlags |= INTERNET_FLAG_SECURE;
			dwServiceType = INTERNET_SERVICE_HTTP;
		}

		hHttpConnection = ::InternetConnect(hInternetSession, strServer, nPort, NULL, NULL, dwServiceType, 0, 0);
		if (hHttpConnection == NULL) {
			SetIPFilterDownloadError(result, _T("InternetConnect failed"), ::GetLastError());
			goto cleanup;
		}

		if (IsIPFilterDownloadCanceled(pJob)) {
			result.bCanceled = true;
			goto cleanup;
		}

		hHttpFile = ::HttpOpenRequest(hHttpConnection, NULL, strObject, NULL, NULL, ppszAcceptTypes, dwRequestFlags, 0);
		if (hHttpFile == NULL) {
			SetIPFilterDownloadError(result, _T("HttpOpenRequest failed"), ::GetLastError());
			goto cleanup;
		}

		(void)::HttpAddRequestHeaders(hHttpFile, _T("Accept-Encoding: identity, *;q=0\r\n"), _UI32_MAX, HTTP_ADDREQ_FLAG_ADD);
		(void)::HttpAddRequestHeaders(hHttpFile, _T("User-Agent: Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; SLCC1)\r\n"), _UI32_MAX, HTTP_ADDREQ_FLAG_ADD);

		PostIPFilterDownloadProgress(pJob, 0, 0, false, dwLastProgressTick, true);

		if (!::HttpSendRequest(hHttpFile, NULL, 0, NULL, 0)) {
			SetIPFilterDownloadError(result, _T("HttpSendRequest failed"), ::GetLastError());
			goto cleanup;
		}

		dwInfoSize = _countof(szStatusCode);
		if (!::HttpQueryInfo(hHttpFile, HTTP_QUERY_STATUS_CODE, szStatusCode, &dwInfoSize, NULL)) {
			SetIPFilterDownloadError(result, _T("HTTP status query failed"), ::GetLastError());
			goto cleanup;
		}
		nStatusCode = _ttol(szStatusCode);
		if (nStatusCode != HTTP_STATUS_OK) {
			result.strError.Format(_T("Invalid HTTP response: HTTP %ld"), nStatusCode);
			goto cleanup;
		}

		dwInfoSize = _countof(szContentLength);
		if (::HttpQueryInfo(hHttpFile, HTTP_QUERY_CONTENT_LENGTH, szContentLength, &dwInfoSize, NULL)) {
			result.uTotalBytes = _tcstoui64(szContentLength, NULL, 10);
			result.bHasTotalBytes = result.uTotalBytes > 0;
		}

		for (;;) {
			if (IsIPFilterDownloadCanceled(pJob)) {
				result.bCanceled = true;
				break;
			}

			DWORD dwBytesRead = 0;
			if (!::InternetReadFile(hHttpFile, szReadBuf, sizeof szReadBuf, &dwBytesRead)) {
				SetIPFilterDownloadError(result, _T("InternetReadFile failed"), ::GetLastError());
				break;
			}
			if (dwBytesRead == 0) {
				bSuccess = true;
				break;
			}

			try {
				file.Write(szReadBuf, dwBytesRead);
			} catch (CFileException* ex) {
				SetIPFilterDownloadError(result, _T("Failed to write download target"), ex->m_lOsError);
				ex->Delete();
				break;
			}

			result.uBytesRead += dwBytesRead;
			PostIPFilterDownloadProgress(pJob, result.uBytesRead, result.uTotalBytes, result.bHasTotalBytes, dwLastProgressTick, false);
		}

		if (bSuccess) {
			try {
				file.Close();
				bFileOpen = false;
			} catch (CFileException* ex) {
				SetIPFilterDownloadError(result, _T("Failed to close download target"), ex->m_lOsError);
				ex->Delete();
				bSuccess = false;
			}
		}

		if (bSuccess) {
			if (!result.bHasTotalBytes) {
				result.uTotalBytes = result.uBytesRead;
				result.bHasTotalBytes = result.uTotalBytes > 0;
			}
			PostIPFilterDownloadProgress(pJob, result.uBytesRead, result.uTotalBytes, result.bHasTotalBytes, dwLastProgressTick, true);
			bDeleteTempFile = false;
		}

cleanup:
		if (hHttpFile != NULL)
			::InternetCloseHandle(hHttpFile);
		if (hHttpConnection != NULL)
			::InternetCloseHandle(hHttpConnection);
		if (hInternetSession != NULL)
			::InternetCloseHandle(hInternetSession);
		if (bFileOpen) {
			try {
				file.Close();
			} catch (CFileException* ex) {
				ex->Delete();
			}
		}
		if (bDeleteTempFile)
			(void)::DeleteFile(PreparePathForWin32LongPath(pJob->strTempFile));

		result.bSucceeded = bSuccess && !result.bCanceled;
		return result.bSucceeded;
	}

	UINT AFX_CDECL IPFilterDownloadThreadProc(LPVOID pParam)
	{
		DbgSetThreadName("IPFilterDownload");
		SIPFilterDownloadJob* pJob = reinterpret_cast<SIPFilterDownloadJob*>(pParam);
		SIPFilterDownloadResult* pResult = new SIPFilterDownloadResult;
		(void)DownloadIPFilterToFile(pJob, *pResult);
		if (!PostIPFilterDownloadPayload(pJob, CemuleDlg::UWM_EMULEAI_IPFILTER_DOWNLOAD_FINISHED, reinterpret_cast<LPARAM>(pResult)))
			delete pResult;
		ReleaseIPFilterDownloadJob(pJob);
		return 0;
	}

	bool StartIPFilterDownload(const CString& url, bool bInteractive)
	{
		if (url.IsEmpty())
			return false;
		if (g_pIPFilterDownloadJob != NULL)
			return true;
		if (theApp.emuledlg == NULL || !::IsWindow(theApp.emuledlg->GetSafeHwnd()))
			return false;

		const CString &sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
		CString strTempFilePath;
		_tmakepathlimit(strTempFilePath.GetBuffer(MAX_PATH), NULL, sConfDir, DFLT_IPFILTER_FILENAME, _T("tmp"));
		strTempFilePath.ReleaseBuffer();

		SIPFilterDownloadJob* pJob = new SIPFilterDownloadJob;
		pJob->hNotifyWnd = theApp.emuledlg->GetSafeHwnd();
		pJob->uToken = ++g_uIPFilterDownloadToken;
		pJob->bInteractive = bInteractive;
		pJob->strURL = url;
		pJob->strTempFile = strTempFilePath;
		g_pIPFilterDownloadJob = pJob;
		g_bIPFilterDownloadOverlayDelayActive = false;
		AddIPFilterDownloadJobRef(pJob);

		UpdateIPFilterDownloadOverlays(0, 0, false, url);

		CWinThread* pThread = AfxBeginThread(IPFilterDownloadThreadProc, pJob, THREAD_PRIORITY_NORMAL);
		if (pThread == NULL) {
			g_pIPFilterDownloadJob = NULL;
			ReleaseIPFilterDownloadJob(pJob);
			ReleaseIPFilterDownloadJob(pJob);
			HideIPFilterDownloadOverlays();
			ReportIPFilterUpdateError(GetResString(_T("IPFILTER_DOWNLOAD_FAILED")), bInteractive);
			return false;
		}

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

void CPPgSecurity::ResetToDefaults()
{
	SetDlgItemInt(IDC_FILTERLEVEL, 127, FALSE);
	CheckDlgButton(IDC_FILTER_SERVER_BY_IPFILTER, BST_UNCHECKED);
	CheckDlgButton(IDC_DONTFILTERPRIVATEIPS, BST_CHECKED);
	CheckDlgButton(IDC_USESECIDENT, BST_CHECKED);
	CheckDlgButton(IDC_RUNASUSER, BST_UNCHECKED);
	CheckDlgButton(IDC_DISABLEOBFUSCATION, BST_UNCHECKED);
	CheckDlgButton(IDC_ENABLEOBFUSCATION, BST_CHECKED);
	CheckDlgButton(IDC_ONLYOBFUSCATED, BST_UNCHECKED);
	GetDlgItem(IDC_ENABLEOBFUSCATION)->EnableWindow(TRUE);
	GetDlgItem(IDC_ONLYOBFUSCATED)->EnableWindow(TRUE);
	CheckDlgButton(IDC_SEARCHSPAMFILTER, BST_CHECKED);
	CheckDlgButton(IDC_CHECK_FILE_OPEN, BST_CHECKED);
	m_bAutoUpdate = false;
	m_nPeriodDays = 7;
	UpdateData(FALSE);
	CheckRadioButton(IDC_SEESHARE1, IDC_SEESHARE3, IDC_SEESHARE3);
	SetModified(TRUE);
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
		SetDlgItemText(IDC_AUTOUPDATE_IPFILTER, GetResString(_T("AUTO_UPDATE")));
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

static bool ApplyDownloadedIPFilterFile(const CString &url, const CString &strTempFilePath, bool bInteractive)
{
	bool bHaveNewFilterFile = false;
	const CString &sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));

	CString strMimeType;
	GetMimeType(strTempFilePath, strMimeType);

	bool bIsArchiveFile = false;
	bool bUncompressed = false;
	CZIPFile zip;
	if (zip.Open(strTempFilePath)) {
		bIsArchiveFile = true;

		CZIPFile::File *zfile = zip.GetFile(DFLT_IPFILTER_FILENAME);
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
				&& (strFile.CompareNoCase(DFLT_IPFILTER_FILENAME) == 0
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

bool CPPgSecurity::UpdateIPFilterFromURL(const CString &url, bool bInteractive)
{
	return StartIPFilterDownload(url, bInteractive);
}

bool CPPgSecurity::IsIPFilterDownloadActive()
{
	return g_pIPFilterDownloadJob != NULL || g_bIPFilterDownloadOverlayDelayActive;
}

bool CPPgSecurity::GetIPFilterDownloadOverlayInfo(CString& strTitle, CString& strDetail, UINT& uDone, UINT& uTotal)
{
	if (g_pIPFilterDownloadJob == NULL && !g_bIPFilterDownloadOverlayDelayActive)
		return false;
	BuildIPFilterDownloadOverlayText(g_uIPFilterDownloadBytesRead, g_uIPFilterDownloadTotalBytes, g_bIPFilterDownloadHasTotalBytes, g_strIPFilterDownloadURL, strTitle, strDetail, uDone, uTotal);
	return true;
}

void CPPgSecurity::FinishIPFilterDownloadOverlayDelay()
{
	if (!g_bIPFilterDownloadOverlayDelayActive)
		return;

	HideIPFilterDownloadOverlays();
}

void CPPgSecurity::CancelIPFilterDownload()
{
	SIPFilterDownloadJob* pJob = g_pIPFilterDownloadJob;
	if (pJob == NULL) {
		FinishIPFilterDownloadOverlayDelay();
		return;
	}

	g_pIPFilterDownloadJob = NULL;
	pJob->hNotifyWnd = NULL;
	InterlockedExchange(&pJob->lCancel, 1);
	ReleaseIPFilterDownloadJob(pJob);
	HideIPFilterDownloadOverlays();
}

LRESULT CPPgSecurity::OnIPFilterDownloadProgress(LPARAM lParam)
{
	SIPFilterDownloadProgress* pProgress = reinterpret_cast<SIPFilterDownloadProgress*>(lParam);
	if (pProgress != NULL) {
		if (g_pIPFilterDownloadJob != NULL && pProgress->uToken == g_uIPFilterDownloadToken)
			UpdateIPFilterDownloadOverlays(pProgress->uBytesRead, pProgress->uTotalBytes, pProgress->bHasTotalBytes, pProgress->strURL);
		delete pProgress;
	}
	return 0;
}

LRESULT CPPgSecurity::OnIPFilterDownloadFinished(LPARAM lParam)
{
	SIPFilterDownloadResult* pResult = reinterpret_cast<SIPFilterDownloadResult*>(lParam);
	if (pResult == NULL)
		return 0;

	if (g_pIPFilterDownloadJob != NULL && pResult->uToken == g_uIPFilterDownloadToken) {
		SIPFilterDownloadJob* pJob = g_pIPFilterDownloadJob;
		pJob->hNotifyWnd = NULL;

		if (pResult->bSucceeded) {
			UpdateIPFilterDownloadOverlays(pResult->uBytesRead, pResult->uTotalBytes, pResult->bHasTotalBytes, pResult->strURL);
			(void)ApplyDownloadedIPFilterFile(pResult->strURL, pResult->strTempFile, pResult->bInteractive);
		} else if (!pResult->bCanceled) {
			CString strError(GetResString(_T("IPFILTER_DOWNLOAD_FAILED")));
			if (!pResult->strError.IsEmpty())
				strError.AppendFormat(_T("\r\n\r\n%s"), (LPCTSTR)pResult->strError);
			ReportIPFilterUpdateError(strError, pResult->bInteractive);
		}

		if (!pResult->bSucceeded && !pResult->strTempFile.IsEmpty())
			(void)::DeleteFile(PreparePathForWin32LongPath(pResult->strTempFile));
		g_pIPFilterDownloadJob = NULL;
		if (pResult->bSucceeded)
			StartIPFilterDownloadOverlayDelay();
		else
			HideIPFilterDownloadOverlays();
		ReleaseIPFilterDownloadJob(pJob);
	} else if (!pResult->strTempFile.IsEmpty())
		(void)::DeleteFile(PreparePathForWin32LongPath(pResult->strTempFile));

	delete pResult;
	return 0;
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
