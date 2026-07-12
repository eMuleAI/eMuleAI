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
#include <io.h>
#include <share.h>
#include "emule.h"
#include "Log.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "emuledlg.h"
#include "StringConversion.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif



static bool ContainsLogTokenNoCase(LPCTSTR pszLine, LPCTSTR pszToken)
{
	if (pszLine == NULL || pszToken == NULL || *pszToken == _T('\0'))
		return false;

	const int iTokenLen = static_cast<int>(_tcslen(pszToken));
	for (LPCTSTR p = pszLine; *p != _T('\0'); ++p) {
		if (_tcsnicmp(p, pszToken, iTokenLen) == 0)
			return true;
	}
	return false;
}

bool IsNatTraversalDebugLog(LPCTSTR pszLine)
{
	return ContainsLogTokenNoCase(pszLine, _T("NatTraversal"))
		|| ContainsLogTokenNoCase(pszLine, _T("NAT-T"))
		|| ContainsLogTokenNoCase(pszLine, _T("NATTTESTMODE"));
}

bool IsUiResponsivenessDebugLog(LPCTSTR pszLine)
{
	static const LPCTSTR s_apszTokens[] = {
		_T("TimeBudget"),
		_T("Chunked "),
		_T("chunked "),
		_T("UI list event"),
		_T("UI notification"),
		_T("continuation"),
		_T("Startup metadata"),
		_T("Startup loading"),
		_T("startup load"),
		_T("startup scan"),
		_T("Stored search"),
		_T("stored search"),
		_T("known files readiness"),
		_T("Known files"),
		_T("client history"),
		_T("Worker topology"),
		_T("Backend lifecycle"),
		_T("backend owner lane"),
		_T("owner violation"),
		_T("owner lane"),
		_T("AsyncDiskWrite"),
		_T("queue pressure"),
		_T("deferred AICH"),
		_T("Deferred AICH"),
		_T("AICHSyncThread deferred"),
		_T("Shared files startup"),
		_T("Shared files bulk"),
		_T("Shared files file-system reload"),
		_T("Search ingest"),
		_T("search ingest"),
		_T("Search packet parse"),
		_T("search answer parse"),
		_T("spam rating"),
		_T("UI command bridge"),
		_T("application event"),
		_T("UI log backlog"),
		_T("FlushQueuedUiLogLines"),
		_T("heartbeat exceeded"),
		_T("Disk cleanup"),
		_T("download state command"),
		_T("download remove command"),
		_T("bulk add"),
		_T("bulk command"),
		_T("Persistence requested"),
		_T("persistence save"),
		_T("model mutation"),
		_T("Backend command"),
		_T("Backend download"),
		_T("Download command"),
		_T("network command"),
		_T("Network parser"),
		_T("Download link parse"),
		_T("Chunked download"),
		_T("Bulk download"),
		_T("Transfer list update"),
		_T("Kad search list update"),
		_T("Shared files command"),
		_T("Shared files search thread"),
		_T("Import part queued"),
		_T("Part file owner event"),
		_T("Telemetry application event"),
		_T("[Persistence]"),
		_T("Persistence command"),
		_T("Persistence worker"),
		_T("Startup download"),
		_T("Skipping staged startup"),
		_T("Skipping startup"),
		_T("part-file disk"),
		_T("Part file write thread"),
		_T("Download batch"),
		_T("Download state"),
		_T("Download remove"),
		_T("Download process-link"),
		_T("Search start command"),
		_T("Search cancel command"),
		_T("Search results changed"),
		_T("Local ED2K search"),
		_T("Collection import command"),
		_T("Kad contact-list event"),
		_T("Kad search-list event"),
		_T("Kad search-graph event"),
		_T("Kad UI status refresh"),
		_T("Kad connection state"),
		_T("Upload/client refresh")
	};

	for (int i = 0; i < _countof(s_apszTokens); ++i) {
		if (ContainsLogTokenNoCase(pszLine, s_apszTokens[i]))
			return true;
	}
	return false;
}

void LogV(UINT uFlags, LPCTSTR pszFmt, va_list argp)
{
	AddLogTextV(uFlags, DLP_DEFAULT, pszFmt, argp);
}

///////////////////////////////////////////////////////////////////////////////
//

void Log(LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(LOG_DEFAULT, pszFmt, argp);
	va_end(argp);
}

void LogError(LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(LOG_ERROR, pszFmt, argp);
	va_end(argp);
}

void LogWarning(LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(LOG_WARNING, pszFmt, argp);
	va_end(argp);
}

///////////////////////////////////////////////////////////////////////////////
//

void Log(UINT uFlags, LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(uFlags, pszFmt, argp);
	va_end(argp);
}

void LogError(UINT uFlags, LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(uFlags | LOG_ERROR, pszFmt, argp);
	va_end(argp);
}

void LogWarning(UINT uFlags, LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(uFlags | LOG_WARNING, pszFmt, argp);
	va_end(argp);
}

///////////////////////////////////////////////////////////////////////////////
//

void DebugLog(LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(LOG_DEBUG, pszFmt, argp);
	va_end(argp);
}

void DebugLogError(LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(LOG_DEBUG | LOG_ERROR, pszFmt, argp);
	va_end(argp);
}

void DebugLogWarning(LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(LOG_DEBUG | LOG_WARNING, pszFmt, argp);
	va_end(argp);
}

///////////////////////////////////////////////////////////////////////////////
//

void DebugLog(UINT uFlags, LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(uFlags | LOG_DEBUG, pszFmt, argp);
	va_end(argp);
}

void DebugLogError(UINT uFlags, LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(uFlags | LOG_DEBUG | LOG_ERROR, pszFmt, argp);
	va_end(argp);
}

void DebugLogWarning(UINT uFlags, LPCTSTR pszFmt, ...)
{
	va_list argp;
	va_start(argp, pszFmt);
	LogV(uFlags | LOG_DEBUG | LOG_WARNING, pszFmt, argp);
	va_end(argp);
}


///////////////////////////////////////////////////////////////////////////////
//

void AddLogLine(bool bAddToStatusBar, LPCTSTR pszLine, ...)
{
	ASSERT(pszLine != NULL);

	va_list argptr;
	va_start(argptr, pszLine);
	AddLogTextV(LOG_DEFAULT | (bAddToStatusBar ? LOG_STATUSBAR : 0), DLP_DEFAULT, pszLine, argptr);
	va_end(argptr);
}

void AddDebugLogLine(bool bAddToStatusBar, LPCTSTR pszLine, ...)
{
	ASSERT(pszLine != NULL);

	va_list argptr;
	va_start(argptr, pszLine);
	AddLogTextV(LOG_DEBUG | (bAddToStatusBar ? LOG_STATUSBAR : 0), DLP_DEFAULT, pszLine, argptr);
	va_end(argptr);
}

void AddDebugLogLine(EDebugLogPriority Priority, LPCTSTR pszLine, ...)
{
	ASSERT(pszLine != NULL);

	va_list argptr;
	va_start(argptr, pszLine);
	AddLogTextV(LOG_DEBUG | 0 , Priority, pszLine, argptr);
	va_end(argptr);
}

void AddDebugLogLine(EDebugLogPriority Priority, bool bAddToStatusBar, LPCTSTR pszLine, ...)
{
	// loglevel needs to be merged with LOG_WARNING and LOG_ERROR later
	// (which only means 3 instead of 5 levels which you can select in the preferences)
	// makes no sense to implement two different priority indicators
	//
	// until there is some time todo this, It will convert DLP_VERYHIGH to ERRORs
	// and DLP_HIGH to LOG_WARNING in order to be able using the Loglevel and color indicator
	ASSERT(pszLine != NULL);

	va_list argptr;
	va_start(argptr, pszLine);
	uint32 nFlag;
	if (Priority == DLP_VERYHIGH)
		nFlag = LOG_ERROR;
	else if (Priority == DLP_HIGH)
		nFlag = LOG_WARNING;
	else
		nFlag = 0;

	AddLogTextV(LOG_DEBUG | nFlag | (bAddToStatusBar ? LOG_STATUSBAR : 0), Priority, pszLine, argptr);
	va_end(argptr);
}

void AddProtectionLogLine(bool bAddToStatusBar, LPCTSTR pszLine, ...)
{
	ASSERT(pszLine != NULL);

	va_list argptr;
	va_start(argptr, pszLine);
	AddLogTextV(LOG_LEECHER | (bAddToStatusBar ? LOG_STATUSBAR : 0), DLP_DEFAULT, pszLine, argptr);
	va_end(argptr);	
}

void AddLogTextV(UINT uFlags, EDebugLogPriority dlpPriority, LPCTSTR pszLine, va_list argptr)
{
	ASSERT(pszLine != NULL);

	const bool bIsDebugLog = (uFlags & LOG_DEBUG) != 0;
	const bool bIsVerboseLog = bIsDebugLog || (uFlags & LOG_LEECHER) != 0;

	if (bIsVerboseLog && !(thePrefs.GetVerbose() && dlpPriority >= thePrefs.GetVerboseLogPriority()))
		return;

	if (bIsDebugLog) {
		if (IsNatTraversalDebugLog(pszLine) && !thePrefs.GetLogNatTraversalEvents())
			return;
		if (IsUiResponsivenessDebugLog(pszLine) && !thePrefs.GetLogUiResponsivenessEvents())
			return;
	}

	TCHAR szLogLine[1000];
	_vsntprintf(szLogLine, _countof(szLogLine), pszLine, argptr);
	szLogLine[_countof(szLogLine) - 1] = _T('\0');
	int iLogLineLen = static_cast<int>(_tcslen(szLogLine));
	while (iLogLineLen > 0 && (szLogLine[iLogLineLen - 1] == _T('\r') || szLogLine[iLogLineLen - 1] == _T('\n')))
		szLogLine[--iLogLineLen] = _T('\0');
	if (iLogLineLen == 0)
		return;

	if (theApp.emuledlg)
		theApp.emuledlg->AddLogText(uFlags, szLogLine);
	else {
		TRACE(_T("App Log: %s\n"), szLogLine);

		TCHAR szFullLogLine[1060];
		int iLen = _sntprintf(szFullLogLine, _countof(szFullLogLine), _T("%s: %s\r\n"), (LPCTSTR)CTime::GetCurrentTime().Format(thePrefs.GetDateTimeFormat4Log()), szLogLine);
		if (iLen > 0) {
			if (!((uFlags & LOG_DEBUG) || (uFlags & LOG_LEECHER))) {
				if (thePrefs.GetLog2Disk())
					theLog.Log(szFullLogLine, iLen);
			} else if (thePrefs.GetVerbose())
				if (thePrefs.GetDebug2Disk())
					theVerboseLog.Log(szFullLogLine, iLen);
		}
	}
}


///////////////////////////////////////////////////////////////////////////////
// CLogFile

CLogFile::CLogFile()
	: m_fp()
	, m_tStarted()
	, m_uBytesWritten()
	, m_uMaxFileSize(_UI32_MAX)
	, m_bInOpenCall()
	, m_eFileFormat(Unicode)
{
	ASSERT(Unicode == 0);
}

CLogFile::~CLogFile()
{
	Close();
}

const CString& CLogFile::GetFilePath() const
{
	return m_strFilePath;
}

bool CLogFile::SetFilePath(LPCTSTR pszFilePath)
{
	if (IsOpen())
		return false;
	m_strFilePath = pszFilePath;
	return true;
}

#if defined(_DEBUG) && defined(DEBUGLEAKHELPER)
void CLogFile::ClearFilePath()
{
	if (IsOpen())
		return;
	// Debug-only leak snapshot hygiene for CString path buffers.
	m_strFilePath.Empty();
	m_strFilePath.FreeExtra();
}
#endif

void CLogFile::SetMaxFileSize(UINT uMaxFileSize)
{
	if (uMaxFileSize < 0x10000u)
		m_uMaxFileSize = (uMaxFileSize == 0) ? _UI32_MAX : 0x10000u;
	else
		m_uMaxFileSize = uMaxFileSize;
}

bool CLogFile::SetFileFormat(const ELogFileFormat eFileFormat)
{
	if (eFileFormat != Unicode && eFileFormat != Utf8) {
		ASSERT(0);
		return false;
	}
	if (m_fp != NULL)
		return false; // can't change file format on-the-fly
	m_eFileFormat = eFileFormat;
	return true;
}

bool CLogFile::IsOpen() const
{
	return m_fp != NULL;
}

bool CLogFile::Create(LPCTSTR pszFilePath, UINT uMaxFileSize, const ELogFileFormat eFileFormat)
{
	Close();
	m_strFilePath = pszFilePath;
	m_uMaxFileSize = uMaxFileSize;
	m_eFileFormat = eFileFormat;
	return Open();
}

bool CLogFile::Open()
{
	if (m_fp != NULL)
		return true;

	m_fp = _tfsopen(m_strFilePath, _T("a+b"), _SH_DENYWR);
	if (m_fp != NULL) {
		m_tStarted = time(NULL);
		m_uBytesWritten = _filelength(_fileno(m_fp));
		if (m_uBytesWritten == 0) {
			if (m_eFileFormat == Unicode) {
				// write Unicode byte order mark 0xFEFF
				fputwc(u'\xFEFF', m_fp);
			} else {
				ASSERT(m_eFileFormat == Utf8);
				; // could write UTF-8 header.
			}
		} else if (m_uBytesWritten >= sizeof(WORD)) {
			// check for Unicode byte order mark 0xFEFF
			WORD wBOM;
			if (fread(&wBOM, sizeof wBOM, 1, m_fp) == 1) {
				if (wBOM == u'\xFEFF' && m_eFileFormat == Unicode) {
					// log file already in Unicode format
					(void)fseek(m_fp, 0, SEEK_END); // actually not needed because file is opened in 'Append' mode.
				} else if (wBOM != u'\xFEFF' && m_eFileFormat != Unicode) {
					// log file already in UTF-8 format
					(void)fseek(m_fp, 0, SEEK_END); // actually not needed because file is opened in 'Append' mode.
				} else {
					// log file does not have the required format, create a new one (with the req. format)
					ASSERT((m_eFileFormat == Unicode && wBOM != u'\xFEFF') || (m_eFileFormat == Utf8 && wBOM == 0xFEFF));

					ASSERT(!m_bInOpenCall);
					if (!m_bInOpenCall) { // just for safety
						m_bInOpenCall = true;
						StartNewLogFile();
						m_bInOpenCall = false;
					}
				}
			}
		}
	}
	return m_fp != NULL;
}

bool CLogFile::Close()
{
	if (m_fp == NULL)
		return true;
	bool bResult = (fclose(m_fp) == 0);
	m_fp = NULL;
	m_tStarted = 0;
	m_uBytesWritten = 0;
	return bResult;
}

bool CLogFile::Log(LPCTSTR pszMsg, int iLen)
{
	if (m_fp == NULL)
		return false;

	size_t uWritten;
	if (m_eFileFormat == Unicode) {
		// don't use 'fputs' + '_filelength' -- gives poor performance
		size_t uToWrite = ((iLen == -1) ? _tcslen(pszMsg) : (size_t)iLen) * sizeof(TCHAR);
		uWritten = fwrite(pszMsg, 1, uToWrite, m_fp);
	} else {
		TUnicodeToUTF8<2048> utf8(pszMsg, iLen);
		uWritten = fwrite((LPCSTR)utf8, 1, utf8.GetLength(), m_fp);
	}
	bool bResult = !ferror(m_fp);
	m_uBytesWritten += uWritten;

	if (m_uBytesWritten >= m_uMaxFileSize)
		StartNewLogFile();
	else
		fflush(m_fp);

	return bResult;
}

bool CLogFile::Logf(LPCTSTR pszFmt, ...)
{
	if (m_fp == NULL)
		return false;

	va_list argp;
	va_start(argp, pszFmt);

	TCHAR szMsg[1024];
	_vsntprintf(szMsg, _countof(szMsg), pszFmt, argp);
	szMsg[_countof(szMsg) - 1] = _T('\0');
	va_end(argp);

	TCHAR szFullMsg[1060];
	int iLen = _sntprintf(szFullMsg, _countof(szFullMsg), _T("%s: %s\r\n"), (LPCTSTR)CTime::GetCurrentTime().Format(thePrefs.GetDateTimeFormat4Log()), szMsg);

	return (iLen > 0) && Log(szFullMsg, iLen);
}

void CLogFile::StartNewLogFile()
{
	time_t tStarted = m_tStarted;
	Close();

	TCHAR szDateLogStarted[40];
	_tcsftime(szDateLogStarted, _countof(szDateLogStarted), _T("%Y.%m.%d %H.%M.%S"), localtime(&tStarted));

	TCHAR szDrv[_MAX_DRIVE];
	TCHAR szDir[_MAX_DIR];
	TCHAR szNam[_MAX_FNAME];
	TCHAR szExt[_MAX_EXT];
	_tsplitpath(m_strFilePath, szDrv, szDir, szNam, szExt);

	CString strLogBakNam;
	strLogBakNam.Format(_T("%s - %s"), szNam, szDateLogStarted);

	TCHAR szLogBakFilePath[MAX_PATH];
	_tmakepathlimit(szLogBakFilePath, szDrv, szDir, strLogBakNam, szExt);

	if (_trename(m_strFilePath, szLogBakFilePath) != 0)
		VERIFY(_tremove(m_strFilePath) == 0);

	Open();
}
