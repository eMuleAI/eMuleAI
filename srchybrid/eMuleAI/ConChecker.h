//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#pragma once
#include "Preferences.h"

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
		bool	IsActive() const;
		bool	IsWorkerGenerationActive(LONG lGeneration) const;
		bool	QueueWorkerCheck(LONG lGeneration, DWORD dwDelayMs);
		uint8	RunWorkerCheck(LONG lGeneration, LPCTSTR pszServer);
		void	ApplyWorkerResult(LONG lGeneration, uint8 uConnectionState);

	private:
		volatile LONG m_lGeneration;
		volatile LONG m_lActive;
		CCriticalSection m_serverLock;
		CString		m_strConnectionCheckerServer;
};
