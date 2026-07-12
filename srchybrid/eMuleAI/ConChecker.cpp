//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#include "StdAfx.h"
#include <afxinet.h>
#include "emule.h"
#include "emuleDlg.h"
#include "conchecker.h"
#include "Log.h"
#include "Preferences.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#endif

const UINT UWM_CONCHECKER = ::RegisterWindowMessage(_T("UWM_CONCHECKER_{C44CF9E8-06B0-4ce4-A422-53DAE6802A1E}"));

namespace
{
	const DWORD CONNECTION_CHECKER_TIMEOUT_MS = 1500;
	const DWORD CONNECTION_CHECKER_INTERVAL_MS = 30000;

	void SetConnectionCheckerInternetTimeout(HINTERNET hInternet, DWORD dwOption)
	{
		DWORD dwTimeout = CONNECTION_CHECKER_TIMEOUT_MS;
		::InternetSetOption(hInternet, dwOption, &dwTimeout, sizeof(dwTimeout));
	}

	CString BuildConnectionCheckerUrl(LPCTSTR pszServer)
	{
		CString strUrl(pszServer != NULL ? pszServer : _T(""));
		strUrl.Trim();
		if (!strUrl.IsEmpty() && strUrl.Find(_T("://")) < 0)
			strUrl.Insert(0, _T("http://"));
		return strUrl;
	}
}

CConChecker::CConChecker(void)
	: m_lGeneration(0)
	, m_lActive(0)
{
}

CConChecker::~CConChecker(void)
{
	Stop();
}

bool CConChecker::Start()
{
	if (theApp.IsNetworkActivityBlockedByBind())
		return false;

	const LONG lGeneration = ::InterlockedIncrement(&m_lGeneration);
	::InterlockedExchange(&m_lActive, 0);
	{
		CSingleLock lock(&m_serverLock, TRUE);
		m_strConnectionCheckerServer.Empty();
	}

	theApp.SetConnectionState(CONSTATE_NULL);
	AddLogLine(false, _T("***"));

	if (!thePrefs.GetConnectionChecker()) {
		AddLogLine(false, GetResString(_T("CONN_CHECK_PASSIVE")));
		AddLogLine(false, _T("***"));
		return false;
	}

	CString strConnectionCheckerServer = thePrefs.GetConnectionCheckerServer();
	if (strConnectionCheckerServer.IsEmpty()) {
		AddLogLine(false, GetResString(_T("CONN_CHECK_PASSIVE")));
		AddLogLine(false, _T("***"));
		return false;
	}

	{
		CSingleLock lock(&m_serverLock, TRUE);
		m_strConnectionCheckerServer = strConnectionCheckerServer;
	}
	::InterlockedExchange(&m_lActive, 1);
	if (!QueueWorkerCheck(lGeneration, 1000)) {
		::InterlockedExchange(&m_lActive, 0);
		AddDebugLogLine(DLP_HIGH, _T("Connection Checker could not queue network utility worker job."));
		AddLogLine(false, GetResString(_T("CONN_CHECK_PASSIVE")));
		AddLogLine(false, _T("***"));
		return false;
	}

	AddLogLine(false, GetResString(_T("CONN_CHECK_ACTIVE")));
	AddLogLine(false, _T("***"));
	return true;
}

bool CConChecker::Stop()
{
	const bool bWasActive = ::InterlockedExchange(&m_lActive, 0) != 0;
	::InterlockedIncrement(&m_lGeneration);
	{
		CSingleLock lock(&m_serverLock, TRUE);
		m_strConnectionCheckerServer.Empty();
	}
	if (bWasActive && !theApp.IsClosing())
		AddLogLine(false, GetResString(_T("CONN_CHECK_STOPPED")));
	return true;
}

bool CConChecker::IsActive() const
{
	return ::InterlockedCompareExchange(const_cast<volatile LONG*>(&m_lActive), 0, 0) != 0;
}

bool CConChecker::IsWorkerGenerationActive(LONG lGeneration) const
{
	return lGeneration != 0
		&& ::InterlockedCompareExchange(const_cast<volatile LONG*>(&m_lActive), 0, 0) != 0
		&& ::InterlockedCompareExchange(const_cast<volatile LONG*>(&m_lGeneration), 0, 0) == lGeneration
		&& !theApp.IsClosing()
		&& !theApp.IsBackendLifecycleStopping();
}

bool CConChecker::QueueWorkerCheck(LONG lGeneration, DWORD dwDelayMs)
{
	if (!IsWorkerGenerationActive(lGeneration))
		return false;

	CString strConnectionCheckerServer;
	{
		CSingleLock lock(&m_serverLock, TRUE);
		strConnectionCheckerServer = m_strConnectionCheckerServer;
	}
	if (strConnectionCheckerServer.IsEmpty())
		return false;

	if (theApp.GetWorkerTopologyState(CemuleApp::WorkerTopologyNetworkUtility) == CemuleApp::WorkerTopologyStopped && !theApp.StartNetworkUtilityWorker())
		return false;

	CemuleApp::SWorkerTopologyItem item;
	item.m_eRole = CemuleApp::WorkerTopologyNetworkUtility;
	item.m_eType = CemuleApp::WorkerTopologyItemNetworkUtility;
	item.m_dwCreatedTick = ::GetTickCount();
	item.m_dwDueTick = item.m_dwCreatedTick + dwDelayMs;
	item.m_lWorkerGeneration = lGeneration;
	item.m_strPayload = strConnectionCheckerServer;
	item.m_strStage = _T("connection-check");
	item.m_strCoalesceKey.Format(_T("connection-check:%ld"), lGeneration);
	return theApp.QueueNetworkUtilityWorkerItem(item);
}

uint8 CConChecker::RunWorkerCheck(LONG lGeneration, LPCTSTR pszServer)
{
	if (theApp.IsNetworkActivityBlockedByBind())
		return CONSTATE_NULL;

	if (!IsWorkerGenerationActive(lGeneration))
		return CONSTATE_NULL;

	const CString strUrl = BuildConnectionCheckerUrl(pszServer);
	if (strUrl.IsEmpty())
		return CONSTATE_NULL;

	HINTERNET hInternet = ::InternetOpen(_T("eMuleAI-ConnectionChecker"), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if (hInternet == NULL)
		return IsWorkerGenerationActive(lGeneration) ? CONSTATE_OFFLINE : CONSTATE_NULL;

	SetConnectionCheckerInternetTimeout(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT);
	SetConnectionCheckerInternetTimeout(hInternet, INTERNET_OPTION_SEND_TIMEOUT);
	SetConnectionCheckerInternetTimeout(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT);

	const DWORD dwOpenUrlFlags = INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_PRAGMA_NOCACHE;
	HINTERNET hUrl = IsWorkerGenerationActive(lGeneration) ? ::InternetOpenUrl(hInternet, strUrl, NULL, 0, dwOpenUrlFlags, 0) : NULL;
	const bool bOnline = hUrl != NULL;
	if (hUrl != NULL)
		::InternetCloseHandle(hUrl);
	::InternetCloseHandle(hInternet);

	if (!IsWorkerGenerationActive(lGeneration))
		return CONSTATE_NULL;

	return bOnline ? CONSTATE_ONLINE : CONSTATE_OFFLINE;
}

void CConChecker::ApplyWorkerResult(LONG lGeneration, uint8 uConnectionState)
{
	if (!IsWorkerGenerationActive(lGeneration))
		return;
	if (uConnectionState != CONSTATE_ONLINE && uConnectionState != CONSTATE_OFFLINE)
		uConnectionState = CONSTATE_NULL;

	if (theApp.emuledlg == NULL)
		return;

	const HWND hWnd = theApp.emuledlg->GetSafeHwnd();
	if (hWnd != NULL && ::IsWindow(hWnd) && ::PostMessage(hWnd, UWM_CONCHECKER, static_cast<WPARAM>(lGeneration), static_cast<LPARAM>(uConnectionState))) {
		if (!QueueWorkerCheck(lGeneration, CONNECTION_CHECKER_INTERVAL_MS))
			AddDebugLogLine(DLP_LOW, _T("Connection Checker did not schedule the next network utility worker job."));
	}
}
