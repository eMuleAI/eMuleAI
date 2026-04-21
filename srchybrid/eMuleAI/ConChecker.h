//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#pragma once
#include "Preferences.h"

class CWinThread;

#define CONSTATE_NULL			0
#define CONSTATE_ONLINE			1
#define CONSTATE_OFFLINE		2

extern const UINT UWM_CONCHECKER;

class CConChecker
{
	public:
		CConChecker(void);
		~CConChecker(void);

		bool	Start();
		bool	Stop();
		bool	IsActive();

	private:
		static UINT ThreadProc(LPVOID pParam);
		bool CleanupStoppedThread(bool bLogStopped);

	protected:
		CWinThread*	m_pThread;
		HANDLE		m_hStopEvent;
		HWND		m_hNotifyWnd;
		CString		m_strConnectionCheckerServer;
};
