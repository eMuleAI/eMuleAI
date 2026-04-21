//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#include "StdAfx.h"
#include "emule.h"
#include "emuleDlg.h"
#include "conchecker.h"
#include "Log.h"
#include <wininet.h>
#include "Preferences.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#endif

const UINT UWM_CONCHECKER = ::RegisterWindowMessage(_T("UWM_CONCHECKER_{C44CF9E8-06B0-4ce4-A422-53DAE6802A1E}"));

namespace
{
void DrainPendingConCheckerMessages()
{
	if (theApp.emuledlg == NULL)
		return;

	const HWND hWnd = theApp.emuledlg->GetSafeHwnd();
	if (!::IsWindow(hWnd))
		return;

	MSG msg;
	while (::PeekMessage(&msg, hWnd, UWM_CONCHECKER, UWM_CONCHECKER, PM_REMOVE)) {
	}
}
}

CConChecker::CConChecker(void)
	: m_pThread(NULL)
	, m_hStopEvent(::CreateEvent(NULL, TRUE, FALSE, NULL))
	, m_hNotifyWnd(NULL)
{
}

CConChecker::~CConChecker(void)
{
	const bool bStopped = Stop();
	if (bStopped && m_hStopEvent) {
		::CloseHandle(m_hStopEvent);
		m_hStopEvent = NULL;
	}
}

bool CConChecker::CleanupStoppedThread(bool bLogStopped)
{
	if (m_pThread == NULL)
		return true;

	HANDLE hThread = m_pThread->m_hThread;
	if (hThread != NULL && ::WaitForSingleObject(hThread, 0) == WAIT_TIMEOUT)
		return false;

	delete m_pThread;
	m_pThread = NULL;
	DrainPendingConCheckerMessages();
	m_hNotifyWnd = NULL;
	m_strConnectionCheckerServer.Empty();
	if (bLogStopped)
		AddLogLine(false, GetResString(_T("CONN_CHECK_STOPPED")));
	return true;
}

bool CConChecker::Start()
{
	if (!CleanupStoppedThread(false))
		return false;

	theApp.SetConnectionState(CONSTATE_NULL);
	AddLogLine(false, _T("***"));

	if (m_hStopEvent == NULL)
		return false;

	if (!thePrefs.GetConnectionChecker()) {
		AddLogLine(false, GetResString(_T("CONN_CHECK_PASSIVE")));
		AddLogLine(false, _T("***"));
		return false;
	}

	if (theApp.emuledlg == NULL) {
		AddLogLine(false, GetResString(_T("CONN_CHECK_PASSIVE")));
		AddLogLine(false, _T("***"));
		return false;
	}

	const HWND hWnd = theApp.emuledlg->GetSafeHwnd();
	if (!::IsWindow(hWnd)) {
		AddLogLine(false, GetResString(_T("CONN_CHECK_PASSIVE")));
		AddLogLine(false, _T("***"));
		return false;
	}

	m_hNotifyWnd = hWnd;
	m_strConnectionCheckerServer = thePrefs.GetConnectionCheckerServer();
	if (m_strConnectionCheckerServer.IsEmpty()) {
		AddLogLine(false, GetResString(_T("CONN_CHECK_PASSIVE")));
		AddLogLine(false, _T("***"));
		return false;
	}

	if (m_hStopEvent)
		::ResetEvent(m_hStopEvent);

	CWinThread* pThread = AfxBeginThread(ThreadProc, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED);
	if (pThread == NULL)
		return false;

	pThread->m_bAutoDelete = FALSE;
	m_pThread = pThread;
	AddLogLine(false, GetResString(_T("CONN_CHECK_ACTIVE")));
	AddLogLine(false, _T("***"));
	m_pThread->ResumeThread();
	return true;
}

bool CConChecker::Stop()
{
	if (m_pThread == NULL)
		return true;

	if (m_hStopEvent)
		::SetEvent(m_hStopEvent);

	HANDLE hThread = m_pThread->m_hThread;
	if (hThread != NULL) {
		const DWORD waitRes = ::WaitForSingleObject(hThread, 6000);
		if (waitRes == WAIT_TIMEOUT) {
			AddDebugLogLine(DLP_HIGH, _T("Connection Checker thread stop timed out."));
			return false;
		}
		if (waitRes != WAIT_OBJECT_0) {
			AddDebugLogLine(DLP_HIGH, _T("Connection Checker thread stop failed. Error=%lu"), ::GetLastError());
			return false;
		}
	}

	return CleanupStoppedThread(true);
}

bool CConChecker::IsActive()
{
	if (m_pThread == NULL || m_pThread->m_hThread == NULL)
		return false;

	return ::WaitForSingleObject(m_pThread->m_hThread, 0) == WAIT_TIMEOUT;
}

UINT CConChecker::ThreadProc(LPVOID pProcParam)
{
	CConChecker* pThis = reinterpret_cast<CConChecker*>(pProcParam);
	if (pThis == NULL || pThis->m_hStopEvent == NULL)
		return 0;

	const HANDLE hStopEvent = pThis->m_hStopEvent;
	const HWND hNotifyWnd = pThis->m_hNotifyWnd;
	const CString strConnectionCheckerServer = pThis->m_strConnectionCheckerServer;
	if (!::IsWindow(hNotifyWnd) || strConnectionCheckerServer.IsEmpty())
		return 0;

#pragma warning(disable:4127) // Disable compiler warning for constant usage in the loop
	for (;;) {
		if (::WaitForSingleObject(hStopEvent, 1000) != WAIT_TIMEOUT)
			break;

		if (theApp.IsClosing())
			break;

		const LPARAM lConnectionState = ::InternetCheckConnection(strConnectionCheckerServer, FLAG_ICC_FORCE_CONNECTION, 0)
			? CONSTATE_ONLINE
			: CONSTATE_OFFLINE;

		if (::WaitForSingleObject(hStopEvent, 0) != WAIT_TIMEOUT)
			break;
		if (theApp.IsClosing())
			break;
		if (!::IsWindow(hNotifyWnd))
			continue;

		::PostMessage(hNotifyWnd, UWM_CONCHECKER, static_cast<WPARAM>(2), lConnectionState);
	}
#pragma warning(default:4127)

	return 0;
}
