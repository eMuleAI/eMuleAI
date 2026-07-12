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
#include "emule.h"
#include "ServerWnd.h"
#include "HTRichEditCtrl.h"
#include "ED2KLink.h"
#include "kademlia/kademlia/kademlia.h"
#include "kademlia/kademlia/prefs.h"
#include "kademlia/utils/MiscUtils.h"
#include "emuledlg.h"
#include "WebServer.h"
#include "CustomAutoComplete.h"
#include "Server.h"
#include "ServerList.h"
#include "ServerConnect.h"
#include "MuleStatusBarCtrl.h"
#include "HelpIDs.h"
#include "NetworkInfoDlg.h"
#include "Log.h"
#include "OtherFunctions.h"
#include "PPgSecurity.h"
#include "UserMsgs.h"
#include "opcodes.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define	SVWND_SPLITTER_YOFF		6
#define	SVWND_SPLITTER_HEIGHT	4

#define	SERVERMET_STRINGS_PROFILE	_T("AC_ServerMetURLs.dat")
#define SZ_DEBUG_LOG_TITLE			_T("Verbose")

struct SServerMetDownloadJob
{
	SServerMetDownloadJob()
		: lRefCount(1)
		, lCancel()
		, hNotifyWnd()
		, uToken()
	{
	}

	LONG lRefCount;
	volatile LONG lCancel;
	HWND hNotifyWnd;
	uint64 uToken;
	CString strURL;
	CString strTempFile;
};

namespace
{
	const UINT WM_SERVERMET_DOWNLOAD_PROGRESS = WM_APP + 210;
	const UINT WM_SERVERMET_DOWNLOAD_FINISHED = WM_APP + 211;
	const DWORD SERVERMET_DOWNLOAD_PROGRESS_INTERVAL = 250;

	struct SServerMetDownloadProgress
	{
		SServerMetDownloadProgress()
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

	struct SServerMetDownloadResult
	{
		SServerMetDownloadResult()
			: uToken()
			, uBytesRead()
			, uTotalBytes()
			, bHasTotalBytes(false)
			, bSucceeded(false)
			, bCanceled(false)
		{
		}

		uint64 uToken;
		uint64 uBytesRead;
		uint64 uTotalBytes;
		bool bHasTotalBytes;
		bool bSucceeded;
		bool bCanceled;
		CString strURL;
		CString strTempFile;
		CString strError;
	};

	void AddServerMetDownloadJobRef(SServerMetDownloadJob* pJob)
	{
		if (pJob == NULL)
			return;
		InterlockedIncrement(&pJob->lRefCount);
	}

	void ReleaseServerMetDownloadJob(SServerMetDownloadJob* pJob)
	{
		if (pJob != NULL && InterlockedDecrement(&pJob->lRefCount) == 0)
			delete pJob;
	}

	bool IsServerMetDownloadCanceled(SServerMetDownloadJob* pJob)
	{
		return pJob == NULL || InterlockedCompareExchange(&pJob->lCancel, 0, 0) != 0;
	}

	void SetServerMetDownloadError(SServerMetDownloadResult& result, LPCTSTR pszContext, DWORD dwError)
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

	bool PostServerMetDownloadPayload(SServerMetDownloadJob* pJob, UINT uMessage, LPARAM lParam)
	{
		HWND hWnd = pJob != NULL ? pJob->hNotifyWnd : NULL;
		return hWnd != NULL && ::IsWindow(hWnd) && ::PostMessage(hWnd, uMessage, 0, lParam);
	}

	void PostServerMetDownloadProgress(SServerMetDownloadJob* pJob, uint64 uBytesRead, uint64 uTotalBytes, bool bHasTotalBytes, DWORD& dwLastProgressTick, bool bForce)
	{
		const DWORD dwNow = ::GetTickCount();
		if (!bForce && dwLastProgressTick != 0 && dwNow - dwLastProgressTick < SERVERMET_DOWNLOAD_PROGRESS_INTERVAL)
			return;

		SServerMetDownloadProgress* pProgress = new SServerMetDownloadProgress;
		pProgress->uToken = pJob->uToken;
		pProgress->uBytesRead = uBytesRead;
		pProgress->uTotalBytes = uTotalBytes;
		pProgress->bHasTotalBytes = bHasTotalBytes;
		pProgress->strURL = pJob->strURL;

		if (PostServerMetDownloadPayload(pJob, WM_SERVERMET_DOWNLOAD_PROGRESS, reinterpret_cast<LPARAM>(pProgress)))
			dwLastProgressTick = dwNow;
		else
			delete pProgress;
	}

	bool DownloadServerMetToFile(SServerMetDownloadJob* pJob, SServerMetDownloadResult& result)
	{
		result.uToken = pJob->uToken;
		result.strURL = pJob->strURL;
		result.strTempFile = pJob->strTempFile;
		if (theApp.IsNetworkActivityBlockedByBind()) {
			result.strError = theApp.GetNetworkActivityBlockMessage();
			return false;
		}

		DWORD dwServiceType = 0;
		CString strServer;
		CString strObject;
		INTERNET_PORT nPort = 0;
		if (!AfxParseURL(pJob->strURL, dwServiceType, strServer, strObject, nPort)) {
			result.strError = _T("Invalid URL");
			return false;
		}
		if (dwServiceType != AFX_INET_SERVICE_HTTP && dwServiceType != AFX_INET_SERVICE_HTTPS) {
			result.strError = _T("Unsupported URL service");
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
		TCHAR szContentEncoding[32] = {};
		TCHAR szContentLength[64] = {};
		DWORD dwInfoSize = 0;
		DWORD dwEncodingSize = 0;
		long nStatusCode = 0;
		char szReadBuf[16 * 1024];

		if (!file.Open(PreparePathForWin32LongPath(pJob->strTempFile), CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite)) {
			SetServerMetDownloadError(result, _T("Failed to open download target"), ::GetLastError());
			goto cleanup;
		}
		bFileOpen = true;

		hInternetSession = ::InternetOpen(AfxGetAppName(), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
		if (hInternetSession == NULL) {
			SetServerMetDownloadError(result, _T("InternetOpen failed"), ::GetLastError());
			goto cleanup;
		}

		(void)::InternetSetOption(hInternetSession, INTERNET_OPTION_CONNECT_TIMEOUT, &dwTimeout, sizeof dwTimeout);
		(void)::InternetSetOption(hInternetSession, INTERNET_OPTION_SEND_TIMEOUT, &dwTimeout, sizeof dwTimeout);
		(void)::InternetSetOption(hInternetSession, INTERNET_OPTION_RECEIVE_TIMEOUT, &dwTimeout, sizeof dwTimeout);

		if (IsServerMetDownloadCanceled(pJob)) {
			result.bCanceled = true;
			goto cleanup;
		}

		if (dwServiceType == AFX_INET_SERVICE_HTTPS) {
			dwRequestFlags |= INTERNET_FLAG_SECURE;
			dwServiceType = INTERNET_SERVICE_HTTP;
		}

		hHttpConnection = ::InternetConnect(hInternetSession, strServer, nPort, NULL, NULL, dwServiceType, 0, 0);
		if (hHttpConnection == NULL) {
			SetServerMetDownloadError(result, _T("InternetConnect failed"), ::GetLastError());
			goto cleanup;
		}

		if (IsServerMetDownloadCanceled(pJob)) {
			result.bCanceled = true;
			goto cleanup;
		}

		hHttpFile = ::HttpOpenRequest(hHttpConnection, NULL, strObject, NULL, NULL, ppszAcceptTypes, dwRequestFlags, 0);
		if (hHttpFile == NULL) {
			SetServerMetDownloadError(result, _T("HttpOpenRequest failed"), ::GetLastError());
			goto cleanup;
		}

		(void)::HttpAddRequestHeaders(hHttpFile, _T("Accept-Encoding: identity, *;q=0\r\n"), _UI32_MAX, HTTP_ADDREQ_FLAG_ADD);
		(void)::HttpAddRequestHeaders(hHttpFile, _T("User-Agent: Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; SLCC1)\r\n"), _UI32_MAX, HTTP_ADDREQ_FLAG_ADD);

		PostServerMetDownloadProgress(pJob, 0, 0, false, dwLastProgressTick, true);

		if (!::HttpSendRequest(hHttpFile, NULL, 0, NULL, 0)) {
			SetServerMetDownloadError(result, _T("HttpSendRequest failed"), ::GetLastError());
			goto cleanup;
		}

		dwInfoSize = _countof(szStatusCode);
		if (!::HttpQueryInfo(hHttpFile, HTTP_QUERY_STATUS_CODE, szStatusCode, &dwInfoSize, NULL)) {
			SetServerMetDownloadError(result, _T("HTTP status query failed"), ::GetLastError());
			goto cleanup;
		}
		nStatusCode = _ttol(szStatusCode);
		if (nStatusCode != HTTP_STATUS_OK) {
			result.strError.Format(_T("Invalid HTTP response: HTTP %ld"), nStatusCode);
			goto cleanup;
		}

		dwEncodingSize = _countof(szContentEncoding);
		if (::HttpQueryInfo(hHttpFile, HTTP_QUERY_CONTENT_ENCODING, szContentEncoding, &dwEncodingSize, NULL)
			&& (!_tcsicmp(szContentEncoding, _T("gzip")) || !_tcsicmp(szContentEncoding, _T("x-gzip")))) {
			result.strError = _T("Unsupported compressed HTTP response");
			goto cleanup;
		}

		dwInfoSize = _countof(szContentLength);
		if (::HttpQueryInfo(hHttpFile, HTTP_QUERY_CONTENT_LENGTH, szContentLength, &dwInfoSize, NULL)) {
			result.uTotalBytes = _tcstoui64(szContentLength, NULL, 10);
			result.bHasTotalBytes = result.uTotalBytes > 0;
		}

		for (;;) {
			if (IsServerMetDownloadCanceled(pJob)) {
				result.bCanceled = true;
				break;
			}

			DWORD dwBytesRead = 0;
			if (!::InternetReadFile(hHttpFile, szReadBuf, sizeof szReadBuf, &dwBytesRead)) {
				SetServerMetDownloadError(result, _T("InternetReadFile failed"), ::GetLastError());
				break;
			}
			if (dwBytesRead == 0) {
				bSuccess = true;
				break;
			}

			try {
				file.Write(szReadBuf, dwBytesRead);
			} catch (CFileException* ex) {
				SetServerMetDownloadError(result, _T("Failed to write download target"), ex->m_lOsError);
				ex->Delete();
				break;
			}

			result.uBytesRead += dwBytesRead;
			PostServerMetDownloadProgress(pJob, result.uBytesRead, result.uTotalBytes, result.bHasTotalBytes, dwLastProgressTick, false);
		}

		if (bSuccess) {
			try {
				file.Close();
				bFileOpen = false;
			} catch (CFileException* ex) {
				SetServerMetDownloadError(result, _T("Failed to close download target"), ex->m_lOsError);
				ex->Delete();
				bSuccess = false;
			}
		}

		if (bSuccess) {
			if (!result.bHasTotalBytes) {
				result.uTotalBytes = result.uBytesRead;
				result.bHasTotalBytes = result.uTotalBytes > 0;
			}
			PostServerMetDownloadProgress(pJob, result.uBytesRead, result.uTotalBytes, result.bHasTotalBytes, dwLastProgressTick, true);
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

	UINT AFX_CDECL ServerMetDownloadThreadProc(LPVOID pParam)
	{
		DbgSetThreadName("ServerMetDownload");
		SServerMetDownloadJob* pJob = reinterpret_cast<SServerMetDownloadJob*>(pParam);
		SServerMetDownloadResult* pResult = new SServerMetDownloadResult;
		(void)DownloadServerMetToFile(pJob, *pResult);
		if (!PostServerMetDownloadPayload(pJob, WM_SERVERMET_DOWNLOAD_FINISHED, reinterpret_cast<LPARAM>(pResult)))
			delete pResult;
		ReleaseServerMetDownloadJob(pJob);
		return 0;
	}
}

// CServerWnd dialog

IMPLEMENT_DYNAMIC(CServerWnd, CDialog)

BEGIN_MESSAGE_MAP(CServerWnd, CResizableDialog)
	ON_BN_CLICKED(IDC_ADDSERVER, OnBnClickedAddserver)
	ON_BN_CLICKED(IDC_UPDATESERVERMETFROMURL, OnBnClickedUpdateServerMetFromUrl)
	ON_BN_CLICKED(IDC_LOGRESET, OnBnClickedResetLog)
	ON_NOTIFY(TCN_SELCHANGE, IDC_TAB3, OnTcnSelchangeTab3)
	ON_NOTIFY(EN_LINK, IDC_SERVMSG, OnEnLinkServerBox)
	ON_BN_CLICKED(IDC_ED2KCONNECT, OnBnConnect)
	ON_WM_SYSCOLORCHANGE()
	ON_WM_DESTROY()
	ON_WM_CTLCOLOR()
	ON_BN_CLICKED(IDC_DD, OnDDClicked)
	ON_WM_HELPINFO()
	ON_EN_CHANGE(IDC_IPADDRESSPORT, OnSvrTextChange)
	ON_EN_CHANGE(IDC_SNAME, OnSvrTextChange)
	ON_EN_CHANGE(IDC_SERVERMETURL, OnSvrTextChange)
	ON_STN_DBLCLK(IDC_SERVLST_ICO, OnStnDblclickServlstIco)
	ON_NOTIFY(UM_SPN_SIZED, IDC_SPLITTER_SERVER, OnSplitterMoved)
	ON_MESSAGE(WM_SERVERMET_DOWNLOAD_PROGRESS, OnServerMetDownloadProgress)
	ON_MESSAGE(WM_SERVERMET_DOWNLOAD_FINISHED, OnServerMetDownloadFinished)
END_MESSAGE_MAP()

CServerWnd::CServerWnd(CWnd *pParent /*=NULL*/)
	: CResizableDialog(CServerWnd::IDD, pParent)
	, icon_srvlist()
	, m_cfDef()
	, m_cfBold()
	, m_pacServerMetURL()
	, m_pServerMetDownloadJob()
	, m_serverMetDownloadQueue()
	, m_uServerMetDownloadToken()
	, m_uServerMetDownloadBytesRead()
	, m_uServerMetDownloadTotalBytes()
	, m_bServerMetDownloadHasTotalBytes(false)
	, m_bServerMetDownloadOverlayDelayActive(false)
	, m_strServerMetDownloadURL()
	, debug()
{
	servermsgbox = new CHTRichEditCtrl;
	logbox = new CHTRichEditCtrl;
	debuglog = new CHTRichEditCtrl;
	protectionlog = new CHTRichEditCtrl;
	m_cfDef.cbSize = (UINT)sizeof m_cfDef;
	m_cfBold.cbSize = (UINT)sizeof m_cfBold;
	StatusSelector.m_bClosable = false;
}

CServerWnd::~CServerWnd()
{
	CancelServerMetDownload();
	if (icon_srvlist)
		VERIFY(::DestroyIcon(icon_srvlist));
	if (m_pacServerMetURL) {
		m_pacServerMetURL->Unbind();
		m_pacServerMetURL->Release();
	}
	delete protectionlog;
	delete debuglog;
	delete logbox;
	delete servermsgbox;
}

BOOL CServerWnd::OnInitDialog()
{
	if (theApp.m_fontLog.m_hObject == NULL) {
		CFont *pFont = GetDlgItem(IDC_SSTATIC)->GetFont();
		LOGFONT lf;
		pFont->GetObject(sizeof lf, &lf);
		theApp.m_fontLog.CreateFontIndirect(&lf);
	}

	ReplaceRichEditCtrl(GetDlgItem(IDC_MYINFOLIST), this, GetDlgItem(IDC_SSTATIC)->GetFont());
	CResizableDialog::OnInitDialog();

	// using ES_NOHIDESEL is actually not needed, but it helps to get around a tricky window update problem!
	// If that style is not specified there are troubles with right clicking into the control for the very first time!?
#define	LOG_PANE_RICHEDIT_STYLES WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL

	RECT rect;
	GetDlgItem(IDC_SERVMSG)->GetWindowRect(&rect);
	GetDlgItem(IDC_SERVMSG)->DestroyWindow();
	::MapWindowPoints(NULL, m_hWnd, (LPPOINT)&rect, 2);
	if (servermsgbox->Create(LOG_PANE_RICHEDIT_STYLES, rect, this, IDC_SERVMSG)) {
		servermsgbox->SetProfileSkinKey(_T("ServerInfoLog"));
		servermsgbox->ModifyStyleEx(0, WS_EX_STATICEDGE, SWP_FRAMECHANGED);
		servermsgbox->SendMessage(EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(3, 3));
		servermsgbox->SetEventMask(servermsgbox->GetEventMask() | ENM_LINK);
		servermsgbox->SetFont(&theApp.m_fontHyperText);
		servermsgbox->ApplySkin();
		servermsgbox->SetTitle(GetResString(_T("SV_SERVERINFO")));
	}

	GetDlgItem(IDC_LOGBOX)->GetWindowRect(&rect);
	GetDlgItem(IDC_LOGBOX)->DestroyWindow();
	::MapWindowPoints(NULL, m_hWnd, (LPPOINT)&rect, 2);
	if (logbox->Create(LOG_PANE_RICHEDIT_STYLES, rect, this, IDC_LOGBOX)) {
		logbox->SetProfileSkinKey(_T("Log"));
		logbox->ModifyStyleEx(0, WS_EX_STATICEDGE, SWP_FRAMECHANGED);
		logbox->SendMessage(EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(3, 3));
		if (theApp.m_fontLog.m_hObject)
			logbox->SetFont(&theApp.m_fontLog);
		logbox->ApplySkin();
		logbox->SetTitle(GetResString(_T("SV_LOG")));
		logbox->SetAutoURLDetect(FALSE);
	}

	GetDlgItem(IDC_DEBUG_LOG)->GetWindowRect(&rect);
	GetDlgItem(IDC_DEBUG_LOG)->DestroyWindow();
	::MapWindowPoints(NULL, m_hWnd, (LPPOINT)&rect, 2);
	if (debuglog->Create(LOG_PANE_RICHEDIT_STYLES, rect, this, IDC_DEBUG_LOG)) {
		debuglog->SetProfileSkinKey(_T("VerboseLog"));
		debuglog->ModifyStyleEx(0, WS_EX_STATICEDGE, SWP_FRAMECHANGED);
		debuglog->SendMessage(EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(3, 3));
		if (theApp.m_fontLog.m_hObject)
			debuglog->SetFont(&theApp.m_fontLog);
		debuglog->ApplySkin();
		debuglog->SetTitle(SZ_DEBUG_LOG_TITLE);
		debuglog->SetAutoURLDetect(FALSE);
	}

	GetDlgItem(IDC_LEECHERLOG)->GetWindowRect(&rect);
	GetDlgItem(IDC_LEECHERLOG)->DestroyWindow();
	::MapWindowPoints(NULL, m_hWnd, (LPPOINT)&rect, 2);
	if (protectionlog->Create(LOG_PANE_RICHEDIT_STYLES, rect, this, IDC_LEECHERLOG)){
		protectionlog->SetProfileSkinKey(_T("VerboseLog"));
		protectionlog->ModifyStyleEx(0, WS_EX_STATICEDGE, SWP_FRAMECHANGED);
		protectionlog->SendMessage(EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(3, 3));
		if (theApp.m_fontLog.m_hObject)
			protectionlog->SetFont(&theApp.m_fontLog);
		protectionlog->ApplySkin();
		protectionlog->SetTitle(GetResString(_T("LEERCHERLOGTITLE")));
		protectionlog->SetAutoURLDetect(FALSE);
	}

	SetAllIcons();
	Localize();
	serverlistctrl.Init();

	TCITEM ti;
	CString name(GetResString(_T("SV_SERVERINFO")));
	DupAmpersand(name);
	ti.mask = TCIF_TEXT | TCIF_IMAGE;
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
	ti.iImage = 1;
	VERIFY(StatusSelector.InsertItem(StatusSelector.GetItemCount(), &ti) == PaneServerInfo);

	name = GetResString(_T("SV_LOG"));
	DupAmpersand(name);
	ti.mask = TCIF_TEXT | TCIF_IMAGE;
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
	ti.iImage = 0;
	VERIFY(StatusSelector.InsertItem(StatusSelector.GetItemCount(), &ti) == PaneLog);

	name = SZ_DEBUG_LOG_TITLE;
	DupAmpersand(name);
	ti.mask = TCIF_TEXT | TCIF_IMAGE;
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
	ti.iImage = 0;
	VERIFY(StatusSelector.InsertItem(StatusSelector.GetItemCount(), &ti) == PaneVerboseLog);

	name=GetResString(_T("LEERCHERLOGTITLE"));
	DupAmpersand(name);
	ti.mask = TCIF_TEXT|TCIF_IMAGE;
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
	ti.iImage = 0;
	VERIFY( StatusSelector.InsertItem(StatusSelector.GetItemCount(), &ti) == PaneLeecherLog );

	AddOrReplaceAnchor(this, IDC_SERVLST_ICO, TOP_LEFT);
	AddOrReplaceAnchor(this, IDC_SERVLIST_TEXT, TOP_LEFT);
	AddOrReplaceAnchor(this, serverlistctrl, TOP_LEFT, MIDDLE_RIGHT);
	AddOrReplaceAnchor(this, m_ctrlMyInfoFrm, TOP_RIGHT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, m_MyInfo, TOP_RIGHT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_UPDATESERVERMETFROMURL, TOP_RIGHT);
	AddOrReplaceAnchor(this, StatusSelector, MIDDLE_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_LOGRESET, MIDDLE_RIGHT); // avoid resizing GUI glitches with the tab control by adding this control as the last one (Z-order)
	// The resizing of those log controls (rich edit controls) works 'better' when added as last anchors (?)
	AddOrReplaceAnchor(this, *servermsgbox, MIDDLE_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, *logbox, MIDDLE_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, *debuglog, MIDDLE_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, *protectionlog, MIDDLE_LEFT, BOTTOM_RIGHT);

	// Resize the URL edit control to fit the down-arrow button before anchors are snapshotted
	if (thePrefs.GetUseAutocompletion()) {
		CWnd* pURL = GetDlgItem(IDC_SERVERMETURL);
		if (pURL) {
			CRect rcURL;
			pURL->GetWindowRect(&rcURL);
			ScreenToClient(&rcURL);
			rcURL.right = rcURL.right - 16;
			pURL->MoveWindow(rcURL, TRUE);
		}
	}

	AddAllOtherAnchors(TOP_RIGHT);

	// Set the tab control to the bottom of the z-order. This solves a lot of strange repainting problems with
	// the rich edit controls (the log panes).
	::SetWindowPos(StatusSelector, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOSIZE);

	debug = true;
	ToggleDebugWindow();

	protectionlog->ShowWindow(SW_HIDE);
	debuglog->ShowWindow(SW_HIDE);
	logbox->ShowWindow(SW_HIDE);
	servermsgbox->ShowWindow(SW_SHOW);

	// optional: restore last used log pane
	if (thePrefs.GetRestoreLastLogPane()) {
		if (thePrefs.GetLastLogPaneID() >= 0 && thePrefs.GetLastLogPaneID() < StatusSelector.GetItemCount()) {
			int iCurSel = StatusSelector.GetCurSel();
			StatusSelector.SetCurSel(thePrefs.GetLastLogPaneID());
			if (thePrefs.GetLastLogPaneID() == StatusSelector.GetCurSel())
				UpdateLogTabSelection();
			else
				StatusSelector.SetCurSel(iCurSel);
		}
	}

	m_MyInfo.SendMessage(EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(3, 3));
	m_MyInfo.SetAutoURLDetect();
	m_MyInfo.SetEventMask(m_MyInfo.GetEventMask() | ENM_LINK);

	PARAFORMAT pf = {};
	pf.cbSize = (UINT)sizeof pf;
	if (m_MyInfo.GetParaFormat(pf)) {
		pf.dwMask |= PFM_TABSTOPS;
		pf.cTabCount = 4;
		pf.rgxTabs[0] = 900;
		pf.rgxTabs[1] = 1000;
		pf.rgxTabs[2] = 1100;
		pf.rgxTabs[3] = 1200;
		m_MyInfo.SetParaFormat(pf);
	}

	m_cfDef.cbSize = (UINT)sizeof m_cfDef;
	if (m_MyInfo.GetSelectionCharFormat(m_cfDef)) {
		m_cfBold = m_cfDef;
		m_cfBold.dwMask |= CFM_BOLD;
		m_cfBold.dwEffects |= CFE_BOLD;
	}

	if (thePrefs.GetUseAutocompletion()) {
		if (m_pacServerMetURL) {
			m_pacServerMetURL->Unbind();
			m_pacServerMetURL->Release();
		}
		m_pacServerMetURL = new CCustomAutoComplete();
		if (m_pacServerMetURL) {
			m_pacServerMetURL->AddRef();
			if (m_pacServerMetURL->Bind(::GetDlgItem(m_hWnd, IDC_SERVERMETURL), ACO_UPDOWNKEYDROPSLIST | ACO_AUTOSUGGEST | ACO_FILTERPREFIXES))
				m_pacServerMetURL->LoadList(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + SERVERMET_STRINGS_PROFILE);
			// Prefill last used server.met update URL from the autocomplete list
				int n = m_pacServerMetURL->GetItemCount();
				if (n > 0) {
					CString last = m_pacServerMetURL->GetItem(0);
					if (!last.IsEmpty()) {
						SetDlgItemText(IDC_SERVERMETURL, last);
						OnSvrTextChange();
					}
				}
		}

		if (theApp.m_fontSymbol.m_hObject) {
			GetDlgItem(IDC_DD)->SetFont(&theApp.m_fontSymbol);
			SetDlgItemText(IDC_DD, _T("6")); // show a down-arrow
		}
	} else
		GetDlgItem(IDC_DD)->ShowWindow(SW_HIDE);

	InitWindowStyles(this);

	// splitter
	CRect rcSpl(55, 55, 300, 55 + SVWND_SPLITTER_HEIGHT);
	m_wndSplitter.CreateWnd(WS_CHILD | WS_VISIBLE, rcSpl, this, IDC_SPLITTER_SERVER);
	m_wndSplitter.SetDrawBorder(true);
	InitSplitter();
	GetDlgItem(IDC_ED2KCONNECT)->EnableWindow(false);

	return TRUE;
}

void CServerWnd::DoDataExchange(CDataExchange *pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SERVLIST, serverlistctrl);
	DDX_Control(pDX, IDC_SSTATIC, m_ctrlNewServerFrm);
	DDX_Control(pDX, IDC_SSTATIC6, m_ctrlUpdateServerFrm);
	DDX_Control(pDX, IDC_MYINFO, m_ctrlMyInfoFrm);
	DDX_Control(pDX, IDC_TAB3, StatusSelector);
	DDX_Control(pDX, IDC_MYINFOLIST, m_MyInfo);
}

bool CServerWnd::IsServerMetDownloadActive() const
{
	return m_pServerMetDownloadJob != NULL;
}

void CServerWnd::ResetServerMetDownloadOverlayState()
{
	m_uServerMetDownloadBytesRead = 0;
	m_uServerMetDownloadTotalBytes = 0;
	m_bServerMetDownloadHasTotalBytes = false;
	m_bServerMetDownloadOverlayDelayActive = false;
	m_strServerMetDownloadURL.Empty();
}

void CServerWnd::StartServerMetDownloadOverlayDelay()
{
	m_bServerMetDownloadOverlayDelayActive = true;
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->StartDownloadOverlayCompletionDelay();
}

void CServerWnd::FinishServerMetDownloadOverlayDelay()
{
	if (!m_bServerMetDownloadOverlayDelayActive)
		return;

	ResetServerMetDownloadOverlayState();
	if (!IsServerMetDownloadActive() && ::IsWindow(serverlistctrl.GetSafeHwnd()))
		serverlistctrl.HideOperationOverlay();
}

void CServerWnd::CancelServerMetDownload()
{
	SServerMetDownloadJob* pJob = m_pServerMetDownloadJob;
	if (pJob == NULL) {
		FinishServerMetDownloadOverlayDelay();
		return;
	}

	m_pServerMetDownloadJob = NULL;
	pJob->hNotifyWnd = NULL;
	InterlockedExchange(&pJob->lCancel, 1);
	ReleaseServerMetDownloadJob(pJob);
	m_serverMetDownloadQueue.RemoveAll();
	ResetServerMetDownloadOverlayState();
	if (::IsWindow(serverlistctrl.GetSafeHwnd()))
		serverlistctrl.HideOperationOverlay();
}

bool CServerWnd::RefreshServerMetDownloadOverlay()
{
	if (!IsServerMetDownloadActive() && !m_bServerMetDownloadOverlayDelayActive) {
		if (::IsWindow(serverlistctrl.GetSafeHwnd()))
			serverlistctrl.HideOperationOverlay();
		return false;
	}

	UpdateServerMetDownloadOverlay(m_uServerMetDownloadBytesRead, m_uServerMetDownloadTotalBytes, m_bServerMetDownloadHasTotalBytes, m_strServerMetDownloadURL);
	return true;
}

bool CServerWnd::StartNextQueuedServerMetDownload()
{
	while (!m_serverMetDownloadQueue.IsEmpty()) {
		const CString strURL = m_serverMetDownloadQueue.RemoveHead();
		if (UpdateServerMetFromURL(strURL))
			return true;
	}
	return false;
}

bool CServerWnd::UpdateServerMetFromURLs(const CStringList& urls)
{
	if (IsServerMetDownloadActive())
		return true;

	m_serverMetDownloadQueue.RemoveAll();
	for (POSITION pos = urls.GetHeadPosition(); pos != NULL;)
		m_serverMetDownloadQueue.AddTail(urls.GetNext(pos));

	return StartNextQueuedServerMetDownload();
}

void CServerWnd::UpdateServerMetDownloadOverlay(uint64 uBytesRead, uint64 uTotalBytes, bool bHasTotalBytes, const CString& strURL)
{
	m_uServerMetDownloadBytesRead = uBytesRead;
	m_uServerMetDownloadTotalBytes = uTotalBytes;
	m_bServerMetDownloadHasTotalBytes = bHasTotalBytes;
	m_strServerMetDownloadURL = strURL;
	if (!::IsWindow(serverlistctrl.GetSafeHwnd()))
		return;

	CString strDetail;
	UINT uDone = 0;
	UINT uTotal = 0;
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

	CString strTitle = GetResString(_T("DOWNLOADING_SERVERMET"));
	CString strIPFilterTitle;
	CString strIPFilterDetail;
	UINT uIPFilterDone = 0;
	UINT uIPFilterTotal = 0;
	if (CPPgSecurity::GetIPFilterDownloadOverlayInfo(strIPFilterTitle, strIPFilterDetail, uIPFilterDone, uIPFilterTotal)) {
		const UINT uCombinedDone = min(uDone, uTotal) + min(uIPFilterDone, uIPFilterTotal);
		const UINT uCombinedTotal = uTotal + uIPFilterTotal;
		strTitle = GetResString(_T("BULKOP_MULTI_OPERATIONS_TITLE"));
		strDetail.Format(GetResString(_T("BULKOP_MULTI_OPERATIONS_DETAIL")), 2, uCombinedDone, uCombinedTotal);
		uDone = uCombinedDone;
		uTotal = uCombinedTotal;
	}

	if (serverlistctrl.IsOperationOverlayVisible())
		serverlistctrl.UpdateOperationOverlay(strTitle, strDetail, uDone, uTotal, false);
	else
		serverlistctrl.ShowOperationOverlay(strTitle, strDetail, uDone, uTotal, false, CString());
}

LRESULT CServerWnd::OnServerMetDownloadProgress(WPARAM, LPARAM lParam)
{
	SServerMetDownloadProgress* pProgress = reinterpret_cast<SServerMetDownloadProgress*>(lParam);
	if (pProgress != NULL) {
		if (m_pServerMetDownloadJob != NULL && pProgress->uToken == m_uServerMetDownloadToken)
			UpdateServerMetDownloadOverlay(pProgress->uBytesRead, pProgress->uTotalBytes, pProgress->bHasTotalBytes, pProgress->strURL);
		delete pProgress;
	}
	return 0;
}

LRESULT CServerWnd::OnServerMetDownloadFinished(WPARAM, LPARAM lParam)
{
	SServerMetDownloadResult* pResult = reinterpret_cast<SServerMetDownloadResult*>(lParam);
	if (pResult == NULL)
		return 0;

	if (m_pServerMetDownloadJob != NULL && pResult->uToken == m_uServerMetDownloadToken) {
		SServerMetDownloadJob* pJob = m_pServerMetDownloadJob;
		m_pServerMetDownloadJob = NULL;
		pJob->hNotifyWnd = NULL;

		if (pResult->bSucceeded) {
			m_serverMetDownloadQueue.RemoveAll();
			UpdateServerMetDownloadOverlay(pResult->uBytesRead, pResult->uTotalBytes, pResult->bHasTotalBytes, pResult->strURL);
			serverlistctrl.Hide();
			serverlistctrl.AddServerMetToList(pResult->strTempFile);
			serverlistctrl.Visible();
			(void)::DeleteFile(PreparePathForWin32LongPath(pResult->strTempFile));
		} else if (!pResult->bCanceled) {
			if (!pResult->strError.IsEmpty())
				AddDebugLogLine(DLP_LOW, false, _T("server.met download failed: %s"), (LPCTSTR)EscPercent(pResult->strError));
			LogError(LOG_STATUSBAR, GetResString(_T("ERR_FAILEDDOWNLOADMET")), (LPCTSTR)pResult->strURL);
		}

		if (pResult->bSucceeded)
			StartServerMetDownloadOverlayDelay();
		else {
			ResetServerMetDownloadOverlayState();
			serverlistctrl.HideOperationOverlay();
		}
		ReleaseServerMetDownloadJob(pJob);
		OnSvrTextChange();
		if (!pResult->bSucceeded && !pResult->bCanceled && StartNextQueuedServerMetDownload()) {
			delete pResult;
			return 0;
		}
	} else if (!pResult->strTempFile.IsEmpty())
		(void)::DeleteFile(PreparePathForWin32LongPath(pResult->strTempFile));

	delete pResult;
	return 0;
}

bool CServerWnd::UpdateServerMetFromURL(const CString &strURL)
{
	if (IsServerMetDownloadActive())
		return true;
	if (theApp.emuledlg != NULL && !theApp.emuledlg->CanUseP2PConnectionCommands()) {
		theApp.emuledlg->LogP2PConnectionCommandBlocked(false);
		return false;
	}

	if (strURL.IsEmpty() || strURL.Find(_T("://")) < 0) {
		// not a valid URL
		LogError(LOG_STATUSBAR, GetResString(_T("INVALIDURL")));
		return false;
	}

	// add entered URL to LRU list even if it's not yet known whether we can download from this URL (it's just more convenient this way)
	if (m_pacServerMetURL && m_pacServerMetURL->IsBound())
		m_pacServerMetURL->AddItem(strURL, 0);

	CString strTempFilename(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	strTempFilename.AppendFormat(_T("temp-%u-server.met"), ::GetTickCount());

	// try to download server.met
	AddLogLine(true, GetResString(_T("DOWNLOADING_SERVERMET_FROM")), (LPCTSTR)EscPercent(strURL));
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->FlushQueuedUiLogLines();

	SServerMetDownloadJob* pJob = new SServerMetDownloadJob;
	pJob->hNotifyWnd = GetSafeHwnd();
	pJob->uToken = ++m_uServerMetDownloadToken;
	pJob->strURL = strURL;
	pJob->strTempFile = strTempFilename;
	m_pServerMetDownloadJob = pJob;
	m_bServerMetDownloadOverlayDelayActive = false;
	AddServerMetDownloadJobRef(pJob);

	UpdateServerMetDownloadOverlay(0, 0, false, strURL);
	OnSvrTextChange();

	CWinThread* pThread = AfxBeginThread(ServerMetDownloadThreadProc, pJob, THREAD_PRIORITY_NORMAL);
	if (pThread == NULL) {
		m_pServerMetDownloadJob = NULL;
		ReleaseServerMetDownloadJob(pJob);
		ReleaseServerMetDownloadJob(pJob);
		ResetServerMetDownloadOverlayState();
		serverlistctrl.HideOperationOverlay();
		OnSvrTextChange();
		LogError(LOG_STATUSBAR, GetResString(_T("ERR_FAILEDDOWNLOADMET")), (LPCTSTR)strURL);
		return false;
	}

	return true;
}

void CServerWnd::OnDestroy()
{
	CancelServerMetDownload();
	CResizableDialog::OnDestroy();
}

void CServerWnd::OnSysColorChange()
{
	CResizableDialog::OnSysColorChange();
	SetAllIcons();
}

void CServerWnd::SetAllIcons()
{
	m_ctrlNewServerFrm.SetIcon(_T("AddServer"));
	m_ctrlUpdateServerFrm.SetIcon(_T("ServerUpdateMET"));
	m_ctrlMyInfoFrm.SetIcon(_T("Info"));

	CImageList iml;
	iml.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	iml.Add(CTempIconLoader(_T("Log")));
	iml.Add(CTempIconLoader(_T("ServerInfo")));
	StatusSelector.SetImageList(&iml);
	m_imlLogPanes.DeleteImageList();
	m_imlLogPanes.Attach(iml.Detach());

	if (icon_srvlist)
		VERIFY(::DestroyIcon(icon_srvlist));
	icon_srvlist = theApp.LoadIcon(_T("ServerList"), 16, 16);
	static_cast<CStatic*>(GetDlgItem(IDC_SERVLST_ICO))->SetIcon(icon_srvlist);
}

void CServerWnd::Localize()
{
	serverlistctrl.Localize();

	serverlistctrl.ShowServerCount();
	m_ctrlNewServerFrm.SetWindowText(GetResString(_T("SV_NEWSERVER")));
	SetDlgItemText(IDC_SSTATIC4, GetResString(_T("SV_ADDRESS_PORT")));
	SetDlgItemText(IDC_SSTATIC3, GetResString(_T("SW_NAME")));
	SetDlgItemText(IDC_ADDSERVER, GetResString(_T("SV_ADD")));
	m_ctrlUpdateServerFrm.SetWindowText(GetResString(_T("SV_MET")));
	SetDlgItemText(IDC_UPDATESERVERMETFROMURL, GetResString(_T("SV_UPDATE")));
	SetDlgItemText(IDC_LOGRESET, GetResString(_T("PW_RESET")));
	m_ctrlMyInfoFrm.SetWindowText(GetResString(_T("MYINFO")));

	TCITEM ti;
	CString name(GetResString(_T("SV_SERVERINFO")));
	DupAmpersand(name);
	ti.mask = TCIF_TEXT;
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
	StatusSelector.SetItem(PaneServerInfo, &ti);

	name = GetResString(_T("SV_LOG"));
	DupAmpersand(name);
	ti.mask = TCIF_TEXT;
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
	StatusSelector.SetItem(PaneLog, &ti);

	name = SZ_DEBUG_LOG_TITLE;
	DupAmpersand(name);
	ti.mask = TCIF_TEXT;
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
	StatusSelector.SetItem(PaneVerboseLog, &ti);

	name = GetResString(_T("LEERCHERLOGTITLE"));
	DupAmpersand(name);
	ti.mask = TCIF_TEXT;
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
	StatusSelector.SetItem(PaneLeecherLog, &ti);

	UpdateLogTabSelection();
	UpdateControlsState();
}

void CServerWnd::OnBnClickedAddserver()
{
	CString serveraddr;
	GetDlgItemText(IDC_IPADDRESSPORT, serveraddr);
	if (serveraddr.Trim().IsEmpty()) {
		LocMessageBox(_T("SRV_ADDR"), MB_OK, 0);
		return;
	}

	uint16 uPort = 0;
	int iPos = serveraddr.Trim().Find(_T(':'));
	if (iPos >= 0) {
		uPort = (uint16)_ttoi(CPTR(serveraddr, iPos + 1));
		serveraddr.Truncate(iPos);
	}

	if (_tcsnicmp(serveraddr, _T("ed2k://"), 7) == 0) {
		CED2KLink *pLink = NULL;
		try {
			pLink = CED2KLink::CreateLinkFromUrl(serveraddr);
			serveraddr.Empty();
			if (pLink && pLink->GetKind() == CED2KLink::kServer) {
				CED2KServerLink *pServerLink = pLink->GetServerLink();
				if (pServerLink) {
					serveraddr = pServerLink->GetAddress();
					uPort = pServerLink->GetPort();
					serveraddr.AppendFormat(_T(":%u"), uPort);
					SetDlgItemText(IDC_IPADDRESSPORT, serveraddr);
				}
			}
		} catch (const CString &strError) {
			CDarkMode::MessageBox(strError);
			serveraddr.Empty();
		}
		delete pLink;
	} else {
		if (!uPort) {
			LocMessageBox(_T("SRV_PORT"), MB_OK, 0);
			return;
		}
	}

	if (serveraddr.IsEmpty() || uPort == 0) {
		LocMessageBox(_T("SRV_ADDR"), MB_OK, 0);
		return;
	}

	CString strServerName;
	GetDlgItemText(IDC_SNAME, strServerName);

	AddServer(uPort, serveraddr, strServerName);
}

void CServerWnd::PasteServerFromClipboard()
{
	CString strServer(theApp.CopyTextFromClipboard());
	if (strServer.Trim().IsEmpty())
		return;

	bool bAdd = true;
	for (int nPos = 0; bAdd && nPos >= 0;) {
		const CString &sToken(strServer.Tokenize(_T(" \t\r\n"), nPos));
		if (sToken.IsEmpty())
			break;
		CED2KLink *pLink = NULL;
		try {
			pLink = CED2KLink::CreateLinkFromUrl(sToken);
			if (pLink && pLink->GetKind() == CED2KLink::kServer) {
				const CED2KServerLink *pServerLink = pLink->GetServerLink();
				if (pServerLink) {
					const CString &strAddress(pServerLink->GetAddress());
					uint16 nPort = pServerLink->GetPort();
					if (!strAddress.IsEmpty() && nPort)
						(void)AddServer(nPort, strAddress, CString(), false);
					else
						bAdd = false;
				}
			}
		} catch (const CString &strError) {
			CDarkMode::MessageBox(strError);
		}
		delete pLink;
	}
}

bool CServerWnd::AddServer(uint16 nPort, const CString &strAddress, const CString &strName, bool bShowErrorMB)
{
	CServer *toadd = new CServer(nPort, strAddress);

	// Barry - Default all manually added servers to high priority
	if (thePrefs.GetManualAddedServersHighPriority())
		toadd->SetPreference(SRV_PR_HIGH);

	toadd->SetListName(strName.IsEmpty() ? strAddress : strName);

	if (!serverlistctrl.AddServer(toadd, true)) {
		CServer *pFoundServer = theApp.serverlist->GetServerByAddress(toadd->GetAddress(), toadd->GetPort());
		if (pFoundServer == NULL && toadd->GetIP() != 0)
			pFoundServer = theApp.serverlist->GetServerByIPTCP(toadd->GetIP(), toadd->GetPort());
		if (pFoundServer) {
			static TCHAR const _aszServerPrefix[] = _T("Server");
			if (_tcsnicmp(toadd->GetListName(), _aszServerPrefix, _countof(_aszServerPrefix) - 1) != 0) {
				pFoundServer->SetListName(toadd->GetListName());
				serverlistctrl.RefreshServer(pFoundServer);
			}
		} else if (bShowErrorMB)
			LocMessageBox(_T("SRV_NOTADDED"), MB_OK, 0);

		delete toadd;
		return false;
	}

	AddLogLine(true, GetResString(_T("SERVERADDED")), (LPCTSTR)EscPercent(toadd->GetListName()));
	return true;
}

void CServerWnd::OnBnClickedUpdateServerMetFromUrl()
{
	if (IsServerMetDownloadActive())
		return;
	if (theApp.emuledlg != NULL && !theApp.emuledlg->CanUseP2PConnectionCommands()) {
		theApp.emuledlg->LogP2PConnectionCommandBlocked(true);
		return;
	}

	CString strURL;
	GetDlgItemText(IDC_SERVERMETURL, strURL);
	m_serverMetDownloadQueue.RemoveAll();

	if (strURL.Trim().IsEmpty()) {
		if (thePrefs.addresses_list.IsEmpty())
			AddLogLine(true, GetResString(_T("SRV_NOURLAV")));
		else
			UpdateServerMetFromURLs(thePrefs.addresses_list);
	} else
		UpdateServerMetFromURL(strURL);
}

void CServerWnd::OnBnClickedResetLog()
{
	int cur_sel = StatusSelector.GetCurSel();
	if (cur_sel == -1)
		return;
	if (cur_sel == PaneLeecherLog)
	{
		theApp.emuledlg->ResetLeecherLog();
		theApp.emuledlg->statusbar->SetText(EMPTY, SBarLog, 0);
	}
	if (cur_sel == PaneVerboseLog) {
		theApp.emuledlg->ResetDebugLog();
		theApp.emuledlg->statusbar->SetText(EMPTY, SBarLog, 0);
	}
	if (cur_sel == PaneLog) {
		theApp.emuledlg->ResetLog();
		theApp.emuledlg->statusbar->SetText(EMPTY, SBarLog, 0);
	}
	if (cur_sel == PaneServerInfo) {
		servermsgbox->Reset();
		// the statusbar does not contain any server log related messages, so it's not cleared.
	}
}

void CServerWnd::OnTcnSelchangeTab3(LPNMHDR, LRESULT *pResult)
{
	UpdateLogTabSelection();
	*pResult = 0;
}

void CServerWnd::UpdateLogTabSelection()
{
	int cur_sel = StatusSelector.GetCurSel();
	if (cur_sel == -1)
		return;
	if (cur_sel == PaneLeecherLog)
	{
		servermsgbox->ShowWindow(SW_HIDE);
		logbox->ShowWindow(SW_HIDE);
		debuglog->ShowWindow(SW_HIDE);
		protectionlog->ShowWindow(SW_SHOW);
		if (protectionlog->IsAutoScroll() && (StatusSelector.GetItemState(cur_sel, TCIS_HIGHLIGHTED) & TCIS_HIGHLIGHTED))
			protectionlog->ScrollToLastLine(true);
		protectionlog->Invalidate();
		StatusSelector.HighlightItem(cur_sel, FALSE);
	}
	if (cur_sel == PaneVerboseLog) {
		servermsgbox->ShowWindow(SW_HIDE);
		logbox->ShowWindow(SW_HIDE);
		protectionlog->ShowWindow(SW_HIDE);
		debuglog->ShowWindow(SW_SHOW);
		if (debuglog->IsAutoScroll() && (StatusSelector.GetItemState(cur_sel, TCIS_HIGHLIGHTED) & TCIS_HIGHLIGHTED))
			debuglog->ScrollToLastLine(true);
		debuglog->Invalidate();
		StatusSelector.HighlightItem(cur_sel, FALSE);
	}
	if (cur_sel == PaneLog) {
		debuglog->ShowWindow(SW_HIDE);
		servermsgbox->ShowWindow(SW_HIDE);
		protectionlog->ShowWindow(SW_HIDE);
		logbox->ShowWindow(SW_SHOW);
		if (logbox->IsAutoScroll() && (StatusSelector.GetItemState(cur_sel, TCIS_HIGHLIGHTED) & TCIS_HIGHLIGHTED))
			logbox->ScrollToLastLine(true);
		logbox->Invalidate();
		StatusSelector.HighlightItem(cur_sel, FALSE);
	}
	if (cur_sel == PaneServerInfo) {
		debuglog->ShowWindow(SW_HIDE);
		logbox->ShowWindow(SW_HIDE);
		protectionlog->ShowWindow(SW_HIDE);
		servermsgbox->ShowWindow(SW_SHOW);
		if (servermsgbox->IsAutoScroll() && (StatusSelector.GetItemState(cur_sel, TCIS_HIGHLIGHTED) & TCIS_HIGHLIGHTED))
			servermsgbox->ScrollToLastLine(true);
		servermsgbox->Invalidate();
		StatusSelector.HighlightItem(cur_sel, FALSE);
	}
}

void CServerWnd::ToggleDebugWindow()
{
	if (thePrefs.GetVerbose() && !debug) {
		TCITEM ti;
		CString name(SZ_DEBUG_LOG_TITLE);
		DupAmpersand(name);
		ti.mask = TCIF_TEXT | TCIF_IMAGE;
		ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
		ti.iImage = 0;
		StatusSelector.InsertItem(StatusSelector.GetItemCount(), &ti);

		name = GetResString(_T("LEERCHERLOGTITLE"));
		DupAmpersand(name);
		ti.mask = TCIF_TEXT|TCIF_IMAGE;
		ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
		ti.iImage = 0;
		StatusSelector.InsertItem(StatusSelector.GetItemCount(),&ti);

		debug = true;
	} else if (!thePrefs.GetVerbose() && debug) {
		if (StatusSelector.GetCurSel() == PaneVerboseLog || StatusSelector.GetCurSel() == PaneLeecherLog) {
			StatusSelector.SetCurSel(PaneLog);
			StatusSelector.SetFocus();
		}
		debuglog->ShowWindow(SW_HIDE);
		servermsgbox->ShowWindow(SW_HIDE);
		protectionlog->ShowWindow(SW_HIDE);
		logbox->ShowWindow(SW_SHOW);
		StatusSelector.DeleteItem(PaneLeecherLog);
		StatusSelector.DeleteItem(PaneVerboseLog);
		debug = false;
	}
}

void CServerWnd::UpdateMyInfo()
{
	m_MyInfo.SetRedraw(FALSE);
	m_MyInfo.SetWindowText(EMPTY);
	CreateNetworkInfo(m_MyInfo, m_cfDef, m_cfBold);
	m_MyInfo.SetRedraw(TRUE);
	m_MyInfo.Invalidate();
}

CString CServerWnd::GetMyInfoString()
{
	CString buffer;
	m_MyInfo.GetWindowText(buffer);
	return buffer;
}

BOOL CServerWnd::PreTranslateMessage(MSG *pMsg)
{
	if (theApp.emuledlg->m_pSplashWnd) //splash or about dialogs are active
		return FALSE;
	if (pMsg->message == WM_KEYDOWN) {
		// Don't handle Ctrl+Tab in this window. It will be handled by the main window.
		if (pMsg->wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0)
			return FALSE;
		switch (pMsg->wParam) {
		case VK_ESCAPE:
			return FALSE;
		case VK_DELETE:
			if (m_pacServerMetURL && m_pacServerMetURL->IsBound() && pMsg->hwnd == GetDlgItem(IDC_SERVERMETURL)->m_hWnd)
				if (GetKeyState(VK_MENU) < 0 || GetKeyState(VK_CONTROL) < 0)
					m_pacServerMetURL->Clear();
				else
					m_pacServerMetURL->RemoveSelectedItem();
			break;
		case VK_RETURN:
			if (pMsg->hwnd == GetDlgItem(IDC_IPADDRESSPORT)->m_hWnd
				|| pMsg->hwnd == GetDlgItem(IDC_SNAME)->m_hWnd)
			{
				OnBnClickedAddserver();
				return TRUE;
			}
			if (pMsg->hwnd == GetDlgItem(IDC_SERVERMETURL)->m_hWnd) {
				if (m_pacServerMetURL && m_pacServerMetURL->IsBound()) {
					CString strText;
					GetDlgItemText(IDC_SERVERMETURL, strText);
					if (!strText.IsEmpty()) {
						SetDlgItemText(IDC_SERVERMETURL, EMPTY); // this seems to be the only chance to let the drop-down list to disappear
						SetDlgItemText(IDC_SERVERMETURL, strText);
						static_cast<CEdit*>(GetDlgItem(IDC_SERVERMETURL))->SetSel(strText.GetLength(), strText.GetLength());
					}
				}
				OnBnClickedUpdateServerMetFromUrl();
				return TRUE;
			}
		}
	}

	return CResizableDialog::PreTranslateMessage(pMsg);
}

bool CServerWnd::SaveServerMetStrings()
{
	if (m_pacServerMetURL == NULL)
		return false;
	return m_pacServerMetURL->SaveList(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + SERVERMET_STRINGS_PROFILE);
}

void CServerWnd::ShowNetworkInfo()
{
	CNetworkInfoDlg dlg;
	dlg.DoModal();
}

void CServerWnd::OnEnLinkServerBox(LPNMHDR pNMHDR, LRESULT *pResult)
{
	ENLINK *pEnLink = reinterpret_cast<ENLINK*>(pNMHDR);
	if (pEnLink && pEnLink->msg == WM_LBUTTONDOWN) {
		CString strUrl;
		servermsgbox->GetTextRange(pEnLink->chrg.cpMin, pEnLink->chrg.cpMax, strUrl);
		BrowserOpen(strUrl, NULL);
		*pResult = 1;
	} else
		*pResult = 0;
}

void CServerWnd::UpdateControlsState()
{
	LPCTSTR uid;
	if (theApp.serverconnect->IsConnected())
		uid = _T("MAIN_BTN_DISCONNECT");
	else if (theApp.serverconnect->IsConnecting())
		uid = _T("MAIN_BTN_CANCEL");
	else
		uid = _T("MAIN_BTN_CONNECT");
	SetDlgItemText(IDC_ED2KCONNECT, GetResNoAmp(uid));
}

void CServerWnd::OnBnConnect()
{
	if (theApp.serverconnect->IsConnected())
		theApp.serverconnect->Disconnect();
	else if (theApp.serverconnect->IsConnecting())
		theApp.serverconnect->StopConnectionTry();
	else {
		if (theApp.emuledlg != NULL && !theApp.emuledlg->CanUseP2PConnectionCommands()) {
			theApp.emuledlg->LogP2PConnectionCommandBlocked(true);
			return;
		}
		if (theApp.serverlist != NULL && theApp.serverlist->GetServerCount() == 0) {
			CDarkMode::MessageBox(GetResString(_T("EMULE_AI_SERVERMET_REQUIRED_CONNECT")), MB_OK | MB_ICONINFORMATION);
			return;
		}
		theApp.serverconnect->ConnectToAnyServer();
	}
}

void CServerWnd::SaveAllSettings()
{
	thePrefs.SetLastLogPaneID(StatusSelector.GetCurSel());
	SaveServerMetStrings();
}

void CServerWnd::OnDDClicked()
{
	CWnd *box = GetDlgItem(IDC_SERVERMETURL);
	box->SetFocus();
	box->SetWindowText(EMPTY);
	box->SendMessage(WM_KEYDOWN, VK_DOWN, 0x00510001);
}

void CServerWnd::ResetHistory()
{
	if (m_pacServerMetURL != NULL) {
		GetDlgItem(IDC_SERVERMETURL)->SendMessage(WM_KEYDOWN, VK_ESCAPE, 0x00510001);
		m_pacServerMetURL->Clear();
	}
}

BOOL CServerWnd::OnHelpInfo(HELPINFO*)
{
	theApp.ShowHelp(eMule_FAQ_GUI_Server);
	return TRUE;
}

void CServerWnd::OnSvrTextChange()
{
	GetDlgItem(IDC_ADDSERVER)->EnableWindow(GetDlgItem(IDC_IPADDRESSPORT)->GetWindowTextLength());
	GetDlgItem(IDC_UPDATESERVERMETFROMURL)->EnableWindow(!IsServerMetDownloadActive() && GetDlgItem(IDC_SERVERMETURL)->GetWindowTextLength() > 0);
}

void CServerWnd::OnStnDblclickServlstIco()
{
	theApp.emuledlg->ShowPreferences(IDD_PPG_SERVER);
}

void CServerWnd::DoResize(int delta)
{
	CSplitterControl::ChangeHeight(&serverlistctrl, delta, CW_TOPALIGN);
	CSplitterControl::ChangeHeight(&StatusSelector, -delta, CW_BOTTOMALIGN);
	CSplitterControl::ChangeHeight(servermsgbox, -delta, CW_BOTTOMALIGN);
	CSplitterControl::ChangeHeight(logbox, -delta, CW_BOTTOMALIGN);
	CSplitterControl::ChangeHeight(debuglog, -delta, CW_BOTTOMALIGN);
	CSplitterControl::ChangeHeight(protectionlog, -delta, CW_BOTTOMALIGN);
	UpdateSplitterRange();
}

void CServerWnd::InitSplitter()
{
	CRect rcWnd;
	GetWindowRect(rcWnd);
	ScreenToClient(rcWnd);

	m_wndSplitter.SetRange(rcWnd.top + 100, rcWnd.bottom - 50);
	LONG splitpos = 5 + (thePrefs.GetSplitterbarPositionServer() * rcWnd.Height()) / 100;

	RECT rcDlgItem;
	serverlistctrl.GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);
	rcDlgItem.bottom = splitpos - 10;
	serverlistctrl.MoveWindow(&rcDlgItem);

	GetDlgItem(IDC_LOGRESET)->GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);
	rcDlgItem.top = splitpos + 9;
	rcDlgItem.bottom = splitpos + 30;
	GetDlgItem(IDC_LOGRESET)->MoveWindow(&rcDlgItem);

	StatusSelector.GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);
	rcDlgItem.top = splitpos + 10;
	rcDlgItem.bottom = rcWnd.bottom - 5;
	StatusSelector.MoveWindow(&rcDlgItem);

	servermsgbox->GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);
	rcDlgItem.top = splitpos + 35;
	rcDlgItem.bottom = rcWnd.bottom - 12;
	servermsgbox->MoveWindow(&rcDlgItem);

	logbox->GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);
	rcDlgItem.top = splitpos + 35;
	rcDlgItem.bottom = rcWnd.bottom - 12;
	logbox->MoveWindow(&rcDlgItem);

	debuglog->GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);
	rcDlgItem.top = splitpos + 35;
	rcDlgItem.bottom = rcWnd.bottom - 12;
	debuglog->MoveWindow(&rcDlgItem);

	protectionlog->GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);
	rcDlgItem.top=splitpos+35;
	rcDlgItem.bottom = rcWnd.bottom-12;
	protectionlog->MoveWindow(&rcDlgItem);

	long right = rcDlgItem.right;
	GetDlgItem(IDC_SPLITTER_SERVER)->GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);
	rcDlgItem.right = right;
	GetDlgItem(IDC_SPLITTER_SERVER)->MoveWindow(&rcDlgItem);

	ReattachAnchors();
}

void CServerWnd::ReattachAnchors()
{
	AddOrReplaceAnchor(this, serverlistctrl, TOP_LEFT, ANCHOR(100, thePrefs.GetSplitterbarPositionServer()));
	AddOrReplaceAnchor(this, StatusSelector, ANCHOR(0, thePrefs.GetSplitterbarPositionServer()), BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_LOGRESET, MIDDLE_RIGHT);
	AddOrReplaceAnchor(this, *servermsgbox, ANCHOR(0, thePrefs.GetSplitterbarPositionServer()), BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, *logbox, ANCHOR(0, thePrefs.GetSplitterbarPositionServer()), BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, *debuglog, ANCHOR(0, thePrefs.GetSplitterbarPositionServer()), BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, *protectionlog, ANCHOR(0, thePrefs.GetSplitterbarPositionServer()), BOTTOM_RIGHT);

	GetDlgItem(IDC_LOGRESET)->Invalidate();

	if (servermsgbox->IsWindowVisible())
		servermsgbox->Invalidate();
	if (logbox->IsWindowVisible())
		logbox->Invalidate();
	if (debuglog->IsWindowVisible())
		debuglog->Invalidate();
	if (protectionlog->IsWindowVisible())
		protectionlog->Invalidate();
}

void CServerWnd::UpdateSplitterRange()
{
	CRect rcWnd;
	GetWindowRect(rcWnd);
	ScreenToClient(rcWnd);

	RECT rcDlgItem;
	serverlistctrl.GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);

	m_wndSplitter.SetRange(rcWnd.top + 100, rcWnd.bottom - 50);

	LONG splitpos = rcDlgItem.bottom + SVWND_SPLITTER_YOFF;
	thePrefs.SetSplitterbarPositionServer((splitpos * 100) / rcWnd.Height());

	GetDlgItem(IDC_LOGRESET)->GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);
	rcDlgItem.top = splitpos + 9;
	rcDlgItem.bottom = splitpos + 30;
	GetDlgItem(IDC_LOGRESET)->MoveWindow(&rcDlgItem);

	ReattachAnchors();
}

LRESULT CServerWnd::DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	// arrange transfer window layout
	switch (message) {
	case WM_PAINT:
		if (m_wndSplitter) {
			CRect rcWnd;
			GetWindowRect(rcWnd);
			if (rcWnd.Height() > 0) {
				RECT rcDown;
				serverlistctrl.GetWindowRect(&rcDown);
				ScreenToClient(&rcDown);

				// splitter paint update
				RECT rcSpl;
				rcSpl.left = 10;
				rcSpl.top = rcDown.bottom + SVWND_SPLITTER_YOFF;
				rcSpl.right = rcDown.right;
				rcSpl.bottom = rcSpl.top + SVWND_SPLITTER_HEIGHT;
				m_wndSplitter.MoveWindow(&rcSpl, TRUE);
				UpdateSplitterRange();
			}
		}
		break;
	case WM_WINDOWPOSCHANGED:
		if (m_wndSplitter)
			m_wndSplitter.Invalidate();
	}

	return CResizableDialog::DefWindowProc(message, wParam, lParam);
}

void CServerWnd::OnSplitterMoved(LPNMHDR pNMHDR, LRESULT*)
{
	SPC_NMHDR *pHdr = reinterpret_cast<SPC_NMHDR*>(pNMHDR);
	DoResize(pHdr->delta);
}

HBRUSH CServerWnd::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = theApp.emuledlg->GetCtlColor(pDC, pWnd, nCtlColor);
	return hbr ? hbr : __super::OnCtlColor(pDC, pWnd, nCtlColor);
}
