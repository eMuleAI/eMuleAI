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
#include <dbghelp.h>
#include "mdump.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


typedef BOOL (WINAPI *MINIDUMPWRITEDUMP)(HANDLE hProcess, DWORD dwPid, HANDLE hFile, MINIDUMP_TYPE DumpType,
										 CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
										 CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
										 CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam);

CMiniDumper theCrashDumper;
TCHAR CMiniDumper::m_szAppName[MAX_PATH] = {};
TCHAR CMiniDumper::m_szDumpDir[MAX_PATH] = {};

void CMiniDumper::Enable(LPCTSTR pszAppName, bool bShowErrors, LPCTSTR pszDumpDir)
{
	// This assert fires if you have two instances of CMiniDumper which is not allowed
	ASSERT(*m_szAppName == _T('\0'));
	_tcsncpy(m_szAppName, pszAppName, _countof(m_szAppName));
	m_szAppName[_countof(m_szAppName) - 1] = _T('\0');

	// eMule may not have the permission to create a DMP file in the directory where the "emule.exe" is located.
	// Need to pre-determine a valid directory.
	_tcsncpy(m_szDumpDir, pszDumpDir, _countof(m_szDumpDir));
	m_szDumpDir[_countof(m_szDumpDir) - 2] = _T('\0');
	::PathAddBackslash(m_szDumpDir);

	MINIDUMPWRITEDUMP pfnMiniDumpWriteDump;
	HMODULE hDbgHelpDll = GetDebugHelperDll((FARPROC*)&pfnMiniDumpWriteDump, bShowErrors);
	if (hDbgHelpDll) {
		if (pfnMiniDumpWriteDump)
			SetUnhandledExceptionFilter(TopLevelFilter);
		FreeLibrary(hDbgHelpDll);
	}
}

#define DBGHELP_HINT _T("The required DBGHELP.DLL may be obtained from \"Microsoft Download Center\" as a part of \"User Mode Process Dumper\".\r\n\r\n") \
	_T("DBGHELP.DLL should reside in Windows/System32 folder, and also 32-bit DLL in 64-bit OS in Windows/SysWOW64 folder.\r\n") \
	_T("Alternatively, DBGHELP.DLL may be copied to eMule executable's folder (DLL and executable must have the same bitness).")

HMODULE CMiniDumper::GetDebugHelperDll(FARPROC *ppfnMiniDumpWriteDump, bool bShowErrors)
{
	*ppfnMiniDumpWriteDump = NULL;
	HMODULE hDll = LoadLibrary(_T("DBGHELP.DLL"));
	if (hDll == NULL) {
		if (bShowErrors)
			// Do *NOT* localize that string (in fact, do not use MFC to load it)!
			MessageBox(NULL, _T("DBGHELP.DLL not found. Please install a DBGHELP.DLL.\r\n\r\n") DBGHELP_HINT, m_szAppName, MB_ICONSTOP | MB_OK);
	} else {
		*ppfnMiniDumpWriteDump = GetProcAddress(hDll, "MiniDumpWriteDump");
		if (*ppfnMiniDumpWriteDump == NULL && bShowErrors)
			// Do *NOT* localize that string (in fact, do not use MFC to load it)!
			MessageBox(NULL, _T("DBGHELP.DLL found is too old. Please upgrade to the current version of DBGHELP.DLL.\r\n\r\n") DBGHELP_HINT, m_szAppName, MB_ICONSTOP | MB_OK);
	}
	return hDll;
}

#define CRASHTEXT _T("eMule crashed :-(\r\n\r\n") \
	_T("A diagnostic file can be created which will help the author to resolve this problem.\r\n") \
	_T("This file will be saved on your Disk (and not sent).\r\n\r\n") \
	_T("Do you want to create this file now?")

LONG WINAPI CMiniDumper::TopLevelFilter(struct _EXCEPTION_POINTERS *pExceptionInfo) noexcept
{
#ifdef _DEBUG
	LONG lRetValue = EXCEPTION_CONTINUE_SEARCH;
#endif
	MINIDUMPWRITEDUMP pfnMiniDumpWriteDump;
	HMODULE hDll = GetDebugHelperDll((FARPROC*)&pfnMiniDumpWriteDump, true);
	if (hDll) {
		if (pfnMiniDumpWriteDump) {
			SYSTEMTIME t;
			GetLocalTime(&t); //time of this crash
			// Ask user to confirm writing a dump file
			// Do *NOT* localize that string (in fact, do not use MFC to load it)!
			if (theCrashDumper.uCreateCrashDump == 2 || MessageBox(NULL, CRASHTEXT, m_szAppName, MB_ICONSTOP | MB_YESNO) == IDYES) {
				TCHAR szBaseName[MAX_PATH];
				_sntprintf(szBaseName, MAX_PATH, _T("%s_%4d%02d%02d-%02d%02d%02d")
					, m_szAppName, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
				szBaseName[_countof(szBaseName) - 1] = _T('\0');
				// Replace spaces and dots in file name.
				for (LPTSTR p = szBaseName; *p != _T('\0'); ++p)
					if (*p == _T('.'))
						*p = _T('-');
					else if (*p == _T(' '))
						*p = _T('_');

				// Create full path for the dump file
				TCHAR szDumpPath[MAX_PATH];
				_sntprintf(szDumpPath, MAX_PATH, _T("%s%s.dmp"), m_szDumpDir, szBaseName);
				szDumpPath[_countof(szDumpPath) - 1] = _T('\0');

				TCHAR szResult[MAX_PATH + 1024];
				*szResult = _T('\0');
				HANDLE hFile = ::CreateFile(szDumpPath, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
				if (hFile != INVALID_HANDLE_VALUE) {
					_MINIDUMP_EXCEPTION_INFORMATION ExInfo = { GetCurrentThreadId(), pExceptionInfo, FALSE };
					BOOL bOK = (*pfnMiniDumpWriteDump)(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, &ExInfo, NULL, NULL);
					if (bOK) {
						// Do *NOT* localize this string (in fact, do not use MFC to load it)!
						_sntprintf(szResult, _countof(szResult)
							, _T("Saved dump file to \"%s\".\r\n\r\n")
							  _T("Please attach this file to a detailed bug report at forum.emule-project.net\r\n\r\n")
							  _T("Thank you for helping to improve eMule!")
							, szDumpPath);
						szResult[_countof(szResult) - 1] = _T('\0');
#ifdef _DEBUG
						lRetValue = EXCEPTION_EXECUTE_HANDLER;
#endif
					} else {
						// Do *NOT* localize this string (in fact, do not use MFC to load it)!
						_sntprintf(szResult, _countof(szResult), _T("Failed to save dump file to \"%s\".\r\n\r\nError: %lu")
							, szDumpPath, ::GetLastError());
						szResult[_countof(szResult) - 1] = _T('\0');
					}
					::CloseHandle(hFile);
				} else {
					// Do *NOT* localize this string (in fact, do not use MFC to load it)!
					_sntprintf(szResult, _countof(szResult), _T("Failed to create dump file \"%s\".\r\n\r\nError: %lu")
						, szDumpPath, ::GetLastError());
					szResult[_countof(szResult) - 1] = _T('\0');
				}
				if (*szResult != _T('\0'))
					MessageBox(NULL, szResult, m_szAppName, MB_ICONINFORMATION | MB_OK);
			}
		}
		FreeLibrary(hDll);
	}

#ifndef _DEBUG
	// Exit the process only in release builds, so that in debug builds the exception
	// is passed to an installed debugger
	ExitProcess(0);
#else
	return lRetValue;
#endif
}