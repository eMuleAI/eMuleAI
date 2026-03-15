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
#pragma once
#include "PartFile.h"

class CPartFile;
class CMenuXP;

///////////////////////////////////////////////////////////////////////////////
// CPreviewThread

class CPreviewThread : public CWinThread
{
	DECLARE_DYNCREATE(CPreviewThread)

public:
	virtual	BOOL	InitInstance();
	virtual int		Run();
	void	SetValues(CPartFile *pPartFile, LPCTSTR pszCommand, LPCTSTR pszCommandArgs);

protected:
	CPreviewThread();			// protected constructor used by dynamic creation

	CPartFile	*m_pPartfile;
	CArray<Gap_Struct> m_aFilled;
	CString		m_strCommand;
	CString		m_strCommandArgs;

};


///////////////////////////////////////////////////////////////////////////////
// CPreviewApps

class CPreviewApps
{
public:
	CPreviewApps();

	static CString GetDefaultAppsFile();
	INT_PTR ReadAllApps();
	void RemoveAllApps();

	int GetAllMenuEntries(CMenuXP &rMenu, const CPartFile *file, LPCTSTR pszExcludeCommand = NULL);
	void RunApp(CPartFile *file, UINT uMenuID) const;

	enum ECanPreviewRes
	{
		NotHandled,
		No,
		Yes
	};
	ECanPreviewRes CanPreview(const CPartFile *file);
	int GetPreviewApp(const CPartFile *file);
	bool Preview(CPartFile *file);

protected:
	struct SPreviewApp
	{
		SPreviewApp() = default;
		SPreviewApp(const SPreviewApp &rCopy)
		{
			*this = rCopy;
		}

		SPreviewApp& operator=(const SPreviewApp &rCopy)
		{
			strTitle = rCopy.strTitle;
			strCommand = rCopy.strCommand;
			strCommandArgs = rCopy.strCommandArgs;
			astrExtensions.Copy(rCopy.astrExtensions);
			ullMinStartOfFile = rCopy.ullMinStartOfFile;
			ullMinCompletedSize = rCopy.ullMinCompletedSize;
			return *this;
		}

		uint64 ullMinStartOfFile;
		uint64 ullMinCompletedSize;
		CString strTitle;
		CString strCommand;
		CString strCommandArgs;
		CStringArray astrExtensions;
	};
	CArray<SPreviewApp> m_aApps;
	time_t m_tDefAppsFileLastModified;
	CPartFile *m_pLastCheckedPartFile;
	SPreviewApp *m_pLastPartFileApp;

	void UpdateApps(bool CheckDefaultAppsFileIfUpdated = false);

public:
	UINT GetPreviewAppCount() { return m_aApps.GetCount(); }
	CString GetPreviewAppName(int AppIndex);
	CString GetPreviewAppCmd(int AppIndex);
	CString GetPreviewAppCmdArgs(int AppIndex);
	CString GetPreviewAppDisplayNameByCommand(LPCTSTR pszCommand);
	HICON GetPreviewCommandIcon(LPCTSTR pszCommand);
	HICON GetPreviewAppIcon(int AppIndex);
};

extern CPreviewApps thePreviewApps;

void ExecutePartFile(CPartFile *file, LPCTSTR pszCommand, LPCTSTR pszCommandArgs);
