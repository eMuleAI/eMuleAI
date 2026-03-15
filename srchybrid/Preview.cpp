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
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <share.h>
#include "emule.h"
#include "Preferences.h"
#include "PartFile.h"
#include "Preview.h"
#include "MenuCmds.h"
#include "opcodes.h"
#include "eMuleAI/MenuXP.h"
#include "eMuleAI/DarkMode.h"
#include "OtherFunctions.h"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	FILE* OpenFileStreamLongPathForWriteText(const CString& path)
	{
		CString prepared = PreparePathForWin32LongPath(path);
		HANDLE h = ::CreateFile(prepared, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h == INVALID_HANDLE_VALUE)
			return NULL;

		int fd = _open_osfhandle((intptr_t)h, _O_WRONLY | _O_TEXT);
		if (fd == -1) {
			::CloseHandle(h);
			return NULL;
		}

		FILE* fp = _fdopen(fd, "w");
		if (fp == NULL) {
			_close(fd);
			return NULL;
		}

		return fp;
	}

	bool DoesExpandedPathExist(LPCTSTR pszPath)
	{
		CString strExpanded(pszPath);
		ExpandEnvironmentStrings(strExpanded);
		return ::PathFileExists(strExpanded);
	}

	CString GetAvailablePreviewAppPath(LPCTSTR pszPath64, LPCTSTR pszPath32)
	{
		if (DoesExpandedPathExist(pszPath64))
			return pszPath64;
		if (DoesExpandedPathExist(pszPath32))
			return pszPath32;
		return EMPTY;
	}

	void AppendPreviewAppLine(CStringArray& astrLines, LPCTSTR pszTitle, const CString& strPath)
	{
		if (!strPath.IsEmpty())
			astrLines.Add(CString(pszTitle) + _T("=\"") + strPath + _T("\""));
	}

	void CreateDefaultPreviewAppsFileIfNeeded(const CString& strFilePath)
	{
		if (::PathFileExists(strFilePath))
			return;

		CStringArray astrLines;
		AppendPreviewAppLine(astrLines, _T("QMPLAY2"), GetAvailablePreviewAppPath(_T("%ProgramFiles%\\QMPlay2\\QMPlay2.exe"), _T("%ProgramFiles(x86)%\\QMPlay2\\QMPlay2.exe")));
		AppendPreviewAppLine(astrLines, _T("VLC"), GetAvailablePreviewAppPath(_T("%ProgramFiles%\\VideoLAN\\VLC\\vlc.exe"), _T("%ProgramFiles(x86)%\\VideoLAN\\VLC\\vlc.exe")));
		AppendPreviewAppLine(astrLines, _T("POT"), GetAvailablePreviewAppPath(_T("%ProgramFiles%\\DAUM\\PotPlayer\\PotPlayerMini64.exe"), _T("%ProgramFiles(x86)%\\DAUM\\PotPlayer\\PotPlayerMini.exe")));
		if (astrLines.IsEmpty())
			return;

		FILE* createFile = OpenFileStreamLongPathForWriteText(strFilePath);
		if (createFile == NULL)
			return;

		for (INT_PTR i = 0; i < astrLines.GetCount(); ++i)
			_ftprintf(createFile, _T("%s\r\n"), (LPCTSTR)astrLines[i]);

		fclose(createFile);
	}

	CString NormalizePreviewCommandPath(LPCTSTR pszCommand)
	{
		CString strCommand(pszCommand != NULL ? pszCommand : EMPTY);
		strCommand.Trim();
		strCommand.Trim(_T(" \t\""));
		ExpandEnvironmentStrings(strCommand);
		return strCommand;
	}

	HICON LoadPreviewCommandIcon(LPCTSTR pszCommand)
	{
		CString strCommand = NormalizePreviewCommandPath(pszCommand);
		if (strCommand.IsEmpty())
			return NULL;

		SHFILEINFO sfi = {};
		if (::SHGetFileInfo(strCommand, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON))
			return sfi.hIcon;

		return NULL;
	}

	CString NormalizePreviewDisplayName(LPCTSTR pszName)
	{
		CString strName(pszName != NULL ? pszName : EMPTY);
		strName.Trim();
		if (strName.IsEmpty())
			return strName;

		strName.MakeLower();
		strName.SetAt(0, (TCHAR)_totupper(strName[0]));
		return strName;
	}
}


CPreviewApps thePreviewApps;

///////////////////////////////////////////////////////////////////////////////
// CPreviewThread

IMPLEMENT_DYNCREATE(CPreviewThread, CWinThread)


CPreviewThread::CPreviewThread()
	: m_pPartfile()
	, m_aFilled()
{
}

BOOL CPreviewThread::InitInstance()
{
	DbgSetThreadName("PartFilePreview");
	return TRUE;
}

BOOL CPreviewThread::Run()
{
	try {
		const CString srcName(m_pPartfile->GetFileName());
		CString strPreviewName(m_pPartfile->GetTmpPath());
		strPreviewName.AppendFormat(_T("%s_preview%s"), (LPCTSTR)srcName.Left(5), ::PathFindExtension(srcName));

		if (!IsWin32LongPathsEnabled() && strPreviewName.GetLength() >= MAX_PATH) {
			AddDebugLogLine(DLP_LOW, false, _T("Preview: Skipping overlong preview path (%u chars): %s\n"), strPreviewName.GetLength(), (LPCTSTR)strPreviewName);
			m_pPartfile->m_bPreviewing = false;
			m_aFilled.RemoveAll();
			return FALSE;
		}

		CString longPreviewName = PreparePathForWin32LongPath(strPreviewName);
		bool bRet = m_pPartfile->CopyPartFile(m_aFilled, longPreviewName);

		m_pPartfile->m_bPreviewing = false;
		m_aFilled.RemoveAll();
		if (!bRet)
			return FALSE;

		SHELLEXECUTEINFO SE = {};
		SE.cbSize = (DWORD)sizeof SE;
		SE.fMask = SEE_MASK_NOCLOSEPROCESS;
		SE.nShow = SW_SHOW;

		if (m_strCommand.IsEmpty()) {
			// use the default verb for the document
			SE.lpFile = strPreviewName;
			ShellExecuteEx(&SE);
		} else {
			SE.lpVerb = _T("open");	// "open" the specified video player

			// get directory of video player application
			CString strCommandDir(m_strCommand);
			int iPos = strCommandDir.ReverseFind(_T('\\'));
			strCommandDir.Truncate(iPos + 1); //may be empty

			CString strArgs(m_strCommandArgs);
			if (!strArgs.IsEmpty())
				strArgs += _T(' ');
			if (strPreviewName.Find(_T(' ')) >= 0)
				strArgs.AppendFormat(_T("\"%s\""), (LPCTSTR)strPreviewName);
			else
				strArgs += strPreviewName;

			CString strCommand(m_strCommand);
			ExpandEnvironmentStrings(strCommand);
			ExpandEnvironmentStrings(strArgs);
			ExpandEnvironmentStrings(strCommandDir);
			SE.lpFile = strCommand;
			SE.lpParameters = strArgs;
			SE.lpDirectory = strCommandDir;
			ShellExecuteEx(&SE);
		}

		if (SE.hProcess) {
			::WaitForSingleObject(SE.hProcess, INFINITE);
			::CloseHandle(SE.hProcess);
		}

		CString delPath = PreparePathForWin32LongPath(strPreviewName);
		if (!::DeleteFile(delPath)) {
			DWORD dwErr = ::GetLastError();
			AddDebugLogLine(DLP_LOW, false, _T("Preview: Failed to delete preview file: %s (err=%lu)\n"), (LPCTSTR)strPreviewName, dwErr);
		}

	} catch (CFileException *ex) {
		m_pPartfile->m_bPreviewing = false;
		ex->Delete();
	}
	return TRUE;
}

void CPreviewThread::SetValues(CPartFile *pPartFile, LPCTSTR pszCommand, LPCTSTR pszCommandArgs)
{
	m_pPartfile = pPartFile;
	pPartFile->GetFilledArray(m_aFilled);
	m_strCommand = pszCommand;
	m_strCommandArgs = pszCommandArgs;
}


///////////////////////////////////////////////////////////////////////////////
// CPreviewApps

CPreviewApps::CPreviewApps()
	: m_tDefAppsFileLastModified()
	, m_pLastCheckedPartFile()
	, m_pLastPartFileApp()
{
	ReadAllApps();
}

CString CPreviewApps::GetDefaultAppsFile()
{
	return thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + _T("PreviewApps.dat");
}

void CPreviewApps::RemoveAllApps()
{
	m_aApps.RemoveAll();
	m_tDefAppsFileLastModified = 0;
}

INT_PTR CPreviewApps::ReadAllApps()
{
	RemoveAllApps();

	const CString &strFilePath(GetDefaultAppsFile());
	CreateDefaultPreviewAppsFileIfNeeded(strFilePath);

	FILE* readFile = OpenFileStreamSharedReadLongPath(strFilePath, true);

	if (readFile != NULL) {
		CString sbuffer;
		while (!feof(readFile)) {
			TCHAR buffer[1024];
			if (_fgetts(buffer, _countof(buffer), readFile) == NULL)
				break;
			sbuffer = buffer;
			sbuffer.TrimRight(_T("\r\n\t"));

			// ignore comments & too short lines
			if (buffer[0] == _T('#') || buffer[0] == _T('/') || sbuffer.GetLength() < 5)
				continue;

			int iPos = 0;
			CString strTitle(sbuffer.Tokenize(_T("="), iPos));
			if (!strTitle.Trim().IsEmpty()) {
				CString strCommand(sbuffer.Tokenize(_T(";"), iPos));
				if (strCommand.Trim().IsEmpty())
					continue;

				LPCTSTR pszCommandArgs = ::PathGetArgs((LPCTSTR)strCommand);
				if (pszCommandArgs)
					strCommand.Truncate((int)(pszCommandArgs - (LPCTSTR)strCommand));
				if (strCommand.Trim(_T(" \t\"")).IsEmpty())
					continue;

				uint64 ullMinCompletedSize = 0;
				uint64 ullMinStartOfFile = 0;
				CStringArray astrExtensions;
				for (; iPos >= 0;) {
					const CString &strParams(sbuffer.Tokenize(_T(";"), iPos));
					if (strParams.IsEmpty())
						break;
					int iPosParam = 0;
					CString strId(strParams.Tokenize(_T("="), iPosParam));
					if (strId.IsEmpty())
						continue;
					const CString &strValue(strParams.Tokenize(_T("="), iPosParam));
					if (strValue.IsEmpty())
						continue;
					strId.MakeLower();
					if (strId == _T("ext"))
						astrExtensions.Add((strValue[0] == _T('.')) ? strValue : _T('.') + strValue);
					else if (strId == _T("minsize"))
						(void)_stscanf(strValue, _T("%I64u"), &ullMinCompletedSize);
					else if (strId == _T("minstart"))
						(void)_stscanf(strValue, _T("%I64u"), &ullMinStartOfFile);
				}

				SPreviewApp svc;
				svc.strTitle = strTitle;
				svc.strCommand = strCommand;
				svc.strCommandArgs = CString(pszCommandArgs).Trim();
				svc.astrExtensions.Append(astrExtensions);
				svc.ullMinCompletedSize = ullMinCompletedSize;
				svc.ullMinStartOfFile = ullMinStartOfFile;
				m_aApps.Add(svc);
			}
		}
		struct _stat64 st;
		if (statUTC((HANDLE)_get_osfhandle(_fileno(readFile)), st) == 0)
			m_tDefAppsFileLastModified = (time_t)st.st_mtime;

		fclose(readFile);

	}

	return m_aApps.GetCount();
}

void CPreviewApps::UpdateApps(bool CheckDefaultAppsFileIfUpdated)
{
	struct _stat64 st;
	if (m_aApps.IsEmpty() || (CheckDefaultAppsFileIfUpdated && statUTC(GetDefaultAppsFile(), st) == 0 && st.st_mtime > m_tDefAppsFileLastModified))
		ReadAllApps();
}

int CPreviewApps::GetAllMenuEntries(CMenuXP &rMenu, const CPartFile *file, LPCTSTR pszExcludeCommand)
{
	UpdateApps(true);
	int count = min((int)m_aApps.GetCount(), MP_PREVIEW_APP_MAX - MP_PREVIEW_APP_MIN + 1);
	bool bEnabled = (file && (uint64)file->GetCompletedSize() >= 16ull * 1024);
	CString strExcludedCommand = NormalizePreviewCommandPath(pszExcludeCommand);
	int iAddedEntries = 0;
	for (int i = 0; i < count; ++i) {
		if (!strExcludedCommand.IsEmpty() && NormalizePreviewCommandPath(m_aApps[i].strCommand) == strExcludedCommand)
			continue;

		rMenu.AppendODMenu(MF_STRING | (bEnabled ? MF_ENABLED : MF_GRAYED), MP_PREVIEW_APP_MIN + i, new CMenuXPText(MP_PREVIEW_APP_MIN + i, NormalizePreviewDisplayName(m_aApps[i].strTitle), GetPreviewCommandIcon(m_aApps[i].strCommand)));
		++iAddedEntries;
	}
	return iAddedEntries;
}

void CPreviewApps::RunApp(CPartFile *file, UINT uMenuID) const
{
	const SPreviewApp &svc(m_aApps[uMenuID - MP_PREVIEW_APP_MIN]);
	ExecutePartFile(file, svc.strCommand, svc.strCommandArgs);
}

void ExecutePartFile(CPartFile *file, LPCTSTR pszCommand, LPCTSTR pszCommandArgs)
{
	if (!CheckFileOpen(file->GetFilePath(), file->GetFileName()))
		return;

	// get directory of video player application
	CString strCommandDir(pszCommand);
	int iPos = strCommandDir.ReverseFind(_T('\\'));
	strCommandDir.Truncate(iPos + 1); //may be empty

	CString strArgs(pszCommandArgs);
	if (!strArgs.IsEmpty())
		strArgs += _T(' ');

	const CString &strQuotedPartFilePath(file->GetFilePath());
	// if the path contains spaces, quote the entire path
	if (strQuotedPartFilePath.Find(_T(' ')) >= 0)
		strArgs.AppendFormat(_T("\"%s\""), (LPCTSTR)strQuotedPartFilePath);
	else
		strArgs += strQuotedPartFilePath;

	file->FlushBuffer(true);

	CString strCommand(pszCommand);
	ExpandEnvironmentStrings(strCommand);
	ExpandEnvironmentStrings(strArgs);
	ExpandEnvironmentStrings(strCommandDir);

	LPCTSTR pszVerb;

	// Backward compatibility with old 'preview' command (when no preview application is specified):
	if (strCommand.IsEmpty()) {
		strCommand = strArgs;
		strArgs.Empty();
		strCommandDir.Empty();
		pszVerb = NULL;
	} else
		pszVerb = _T("open");

	TRACE2(_T("Starting preview application:\n"));
	TRACE2(_T("  Command =%s\n"), (LPCTSTR)strCommand);
	TRACE2(_T("  Args    =%s\n"), (LPCTSTR)strArgs);
	TRACE2(_T("  Dir     =%s\n"), (LPCTSTR)strCommandDir);
	DWORD dwError = (DWORD)ShellExecute(NULL, pszVerb, strCommand, strArgs.IsEmpty() ? NULL : (LPCTSTR)strArgs, strCommandDir.IsEmpty() ? NULL : (LPCTSTR)strCommandDir, SW_SHOWNORMAL);
	if (dwError <= 32) {
		//
		// Unfortunately, Windows may already have shown an error dialog which tells
		// the user about the failed 'ShellExecute' call. *BUT* that error dialog is not
		// shown in each case!
		//
		// Examples:
		//	 -	Specifying an executable which does not exist (e.g. APP.EXE)
		//		-> (Error 2) -> No error is shown.
		//
		//   -	Executing a document (verb "open") which has an unregistered extension
		//		-> (Error 31) -> Error is shown.
		//
		// I'm not sure whether this behaviour (showing an error dialog in cases of some
		// specific errors) is handled the same way in all Windows version -> therefore I
		// decide to always show an application specific error dialog!
		//
		CString strMsg;
		strMsg.Format(_T("Failed to execute: %s %s"), (LPCTSTR)strCommand, (LPCTSTR)strArgs);

		LPCTSTR strSysErrMsg = GetShellExecuteErrMsg(dwError);
		if (*strSysErrMsg)
			strMsg.AppendFormat(_T("\r\n\r\n%s"), strSysErrMsg);

		CDarkMode::MessageBox(strMsg, MB_ICONSTOP);
	}
}

int CPreviewApps::GetPreviewApp(const CPartFile *file)
{
	LPCTSTR pszExt = ::PathFindExtension(file->GetFileName());
	if (*pszExt) {
		UpdateApps();
		for (INT_PTR i = m_aApps.GetCount(); --i >= 0;) {
			const SPreviewApp &rApp(m_aApps[i]);
			for (INT_PTR j = rApp.astrExtensions.GetCount(); --j >= 0;)
				if (rApp.astrExtensions[j].CompareNoCase(pszExt) == 0)
					return (int)i;
		}
	}
	return -1;
}

CPreviewApps::ECanPreviewRes CPreviewApps::CanPreview(const CPartFile *file)
{
	int iApp = GetPreviewApp(file);
	if (iApp == -1)
		return NotHandled;

	const SPreviewApp &rApp = m_aApps[iApp];
	if ((uint64)file->GetCompletedSize() < rApp.ullMinCompletedSize)
		return No;

	if (rApp.ullMinStartOfFile &&!file->IsComplete(0, min(rApp.ullMinStartOfFile, (uint64)file->GetFileSize()) - 1))
		return No;

	return Yes;
}

bool CPreviewApps::Preview(CPartFile *file)
{
	int iApp = GetPreviewApp(file);
	if (iApp < 0)
		return false;
	RunApp(file, MP_PREVIEW_APP_MIN + iApp);
	return true;
}
CString CPreviewApps::GetPreviewAppName(int AppIndex)
{
	return m_aApps[AppIndex].strTitle;
}

CString CPreviewApps::GetPreviewAppCmd(int AppIndex)
{
	return m_aApps[AppIndex].strCommand;
}

CString CPreviewApps::GetPreviewAppCmdArgs(int AppIndex)
{
	return m_aApps[AppIndex].strCommandArgs;
}

CString CPreviewApps::GetPreviewAppDisplayNameByCommand(LPCTSTR pszCommand)
{
	CString strCommand = NormalizePreviewCommandPath(pszCommand);
	if (strCommand.IsEmpty())
		return EMPTY;

	UpdateApps();
	for (INT_PTR i = 0; i < m_aApps.GetCount(); ++i) {
		if (NormalizePreviewCommandPath(m_aApps[i].strCommand) == strCommand)
			return NormalizePreviewDisplayName(m_aApps[i].strTitle);
	}

	CString strExeName = ::PathFindFileName(strCommand);
	int iDot = strExeName.ReverseFind(_T('.'));
	if (iDot > 0)
		strExeName.Truncate(iDot);
	return NormalizePreviewDisplayName(strExeName);
}

HICON CPreviewApps::GetPreviewCommandIcon(LPCTSTR pszCommand)
{
	HICON hIcon = LoadPreviewCommandIcon(pszCommand);
	if (hIcon != NULL)
		return hIcon;

	return theApp.LoadIcon(_T("PREVIEW"), 16, 16);
}

HICON CPreviewApps::GetPreviewAppIcon(int AppIndex)
{
	UpdateApps(true);
	if (AppIndex < 0 || AppIndex >= m_aApps.GetCount())
		return GetPreviewCommandIcon(NULL);

	return GetPreviewCommandIcon(m_aApps[AppIndex].strCommand);
}
