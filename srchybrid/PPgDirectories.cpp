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
#include "emule.h"
#include "emuledlg.h"
#include "SharedFilesWnd.h"
#include "PPgDirectories.h"
#include "otherfunctions.h"
#include "InputBox.h"
#include "SharedFileList.h"
#include "Preferences.h"
#include "HelpIDs.h"
#include "UserMsgs.h"
#include "opcodes.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


IMPLEMENT_DYNAMIC(CPPgDirectories, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgDirectories, CPropertyPage)
	ON_BN_CLICKED(IDC_SELTEMPDIR, OnBnClickedSeltempdir)
	ON_BN_CLICKED(IDC_SELINCDIR, OnBnClickedSelincdir)
	ON_EN_CHANGE(IDC_INCFILES, OnSettingsChange)
	ON_EN_CHANGE(IDC_TEMPFILES, OnSettingsChange)
	ON_BN_CLICKED(IDC_UNCADD, OnBnClickedAddUNC)
	ON_WM_HELPINFO()
	ON_BN_CLICKED(IDC_SELTEMPDIRADD, OnBnClickedSeltempdiradd)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CPPgDirectories::CPPgDirectories()
	: CPropertyPage(CPPgDirectories::IDD)
	, m_icoBrowse()
{
}

void CPPgDirectories::DoDataExchange(CDataExchange *pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SHARESELECTOR, m_ShareSelector);
}

BOOL CPPgDirectories::OnInitDialog()
{
	CWaitCursor curWait; // initialization of that dialog may take a while
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);

	static_cast<CEdit*>(GetDlgItem(IDC_INCFILES))->SetLimitText(MAX_PATH);

	AddBuddyButton(GetDlgItem(IDC_INCFILES)->m_hWnd, ::GetDlgItem(m_hWnd, IDC_SELINCDIR));
	InitAttachedBrowseButton(::GetDlgItem(m_hWnd, IDC_SELINCDIR), m_icoBrowse);

	AddBuddyButton(GetDlgItem(IDC_TEMPFILES)->m_hWnd, ::GetDlgItem(m_hWnd, IDC_SELTEMPDIR));
	InitAttachedBrowseButton(::GetDlgItem(m_hWnd, IDC_SELTEMPDIR), m_icoBrowse);


	GetDlgItem(IDC_SELTEMPDIRADD)->ShowWindow(thePrefs.IsExtControlsEnabled() ? SW_SHOW : SW_HIDE);

	LoadSettings();
	Localize();

	return TRUE;  // return TRUE unless you set the focus to the control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

void CPPgDirectories::LoadSettings()
{
	SetDlgItemText(IDC_INCFILES, thePrefs.m_strIncomingDir);

	CString tempfolders;
	for (INT_PTR i = 0; i < thePrefs.GetTempDirCount(); ++i) {
		if (i > 0)
			tempfolders += _T('|');
		tempfolders += thePrefs.GetTempDir(i);
	}
	SetDlgItemText(IDC_TEMPFILES, tempfolders);

	CStringList sharedDirs;
	thePrefs.CopySharedDirectoryList(sharedDirs);
	m_ShareSelector.SetSharedDirectories(sharedDirs);
}

void CPPgDirectories::OnBnClickedSelincdir()
{
	TCHAR buffer[MAX_PATH];
	buffer[GetDlgItemText(IDC_INCFILES, buffer, MAX_PATH)] = _T('\0');
	if (SelectDir(GetSafeHwnd(), buffer, GetResString(_T("SELECT_INCOMINGDIR"))))
		SetDlgItemText(IDC_INCFILES, buffer);
}

void CPPgDirectories::OnBnClickedSeltempdir()
{
	TCHAR buffer[MAX_PATH];
	buffer[GetDlgItemText(IDC_TEMPFILES, buffer, MAX_PATH)] = _T('\0');
	if (SelectDir(GetSafeHwnd(), buffer, GetResString(_T("SELECT_TEMPDIR"))))
		SetDlgItemText(IDC_TEMPFILES, buffer);
}

BOOL CPPgDirectories::OnApply()
{
	CString strIncomingDir;
	GetDlgItemText(IDC_INCFILES, strIncomingDir);
	MakeFoldername(strIncomingDir);
	if (strIncomingDir.IsEmpty()) {
		strIncomingDir = thePrefs.GetDefaultDirectory(EMULE_INCOMINGDIR, true); // will create the directory here if it doesn't exist
		SetDlgItemText(IDC_INCFILES, strIncomingDir);
	} else if (thePrefs.IsInstallationDirectory(strIncomingDir)) {
		ErrorBalloon(IDC_INCFILES, _T("WRN_INCFILE_RESERVED"));
		return FALSE;
	}

	const CString &sOldIncoming(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
	if (strIncomingDir.CompareNoCase(sOldIncoming) != 0 && strIncomingDir.CompareNoCase(thePrefs.GetDefaultDirectory(EMULE_INCOMINGDIR, false)) != 0) {
		// if the user chooses a non-default directory which already contains files,
		// inform him that all those files will be shared
		bool bExistingFile = false;
		CFileFind ff;
		for (BOOL bFound = ff.FindFile(strIncomingDir + _T('*')); bFound && !bExistingFile;) {
			bFound = ff.FindNextFile();
			if (ff.IsDirectory() || ff.IsSystem() || ff.IsTemporary() || ff.GetLength() == 0 || ff.GetLength() > MAX_EMULE_FILE_SIZE)
				continue;

			// ignore real LNK files
			if (ExtensionIs(ff.GetFileName(), _T(".lnk"))) {
				SHFILEINFO info;
				if (::SHGetFileInfo(ff.GetFilePath(), 0, &info, sizeof info, SHGFI_ATTRIBUTES) && (info.dwAttributes & SFGAO_LINK))
					if (!thePrefs.GetResolveSharedShellLinks())
						continue;
			}

			// ignore real THUMBS.DB files -- seems that lot of ppl have 'thumbs.db' files without the 'System' file attribute
			bExistingFile = (ff.GetFileName().CompareNoCase(_T("thumbs.db")) != 0);
		}
		if (bExistingFile && LocMessageBox(_T("WRN_INCFILE_EXISTS"), MB_OKCANCEL | MB_ICONINFORMATION, 0) == IDCANCEL)
			return FALSE;
	}

	// checking specified tempdir(s)
	CString strTempDir;
	GetDlgItemText(IDC_TEMPFILES, strTempDir);
	if (strTempDir.IsEmpty()) {
		strTempDir = thePrefs.GetDefaultDirectory(EMULE_TEMPDIR, true); // will create the directory if it doesn't exist
		SetDlgItemText(IDC_TEMPFILES, strTempDir);
	}

	bool testtempdirchanged = false;
	CStringArray temptempfolders;
	for (int iPos = 0; iPos >= 0;) {
		CString atmp(strTempDir.Tokenize(_T("|"), iPos));
		if (atmp.Trim().IsEmpty())
			continue;

		if (EqualPaths(strIncomingDir, atmp)) {
			ErrorBalloon(IDC_TEMPFILES, _T("WRN_INCTEMP_SAME"));
			return FALSE;
		} else if (thePrefs.IsInstallationDirectory(atmp)) {
			ErrorBalloon(IDC_TEMPFILES, _T("WRN_TEMPFILES_RESERVED"));
			return FALSE;
		}

		bool bDup = false;
		for (INT_PTR i = temptempfolders.GetCount(); --i >= 0;)	// avoid duplicate tempdirs
			if (atmp.CompareNoCase(temptempfolders[i]) == 0) {
				bDup = true;
				break;
			}

		if (!bDup) {
			temptempfolders.Add(atmp);
			if (thePrefs.GetTempDirCount() < temptempfolders.GetCount()
				|| atmp.CompareNoCase(thePrefs.GetTempDir(temptempfolders.GetCount() - 1)) != 0)
			{
				testtempdirchanged = true;
			}
		}
	}

	if (temptempfolders.IsEmpty())
		temptempfolders.Add(thePrefs.GetDefaultDirectory(EMULE_TEMPDIR, true));

	if (temptempfolders.GetCount() != thePrefs.GetTempDirCount())
		testtempdirchanged = true;

	// applying tempdirs
	if (testtempdirchanged) {
		thePrefs.tempdir.RemoveAll();
		for (INT_PTR i = 0; i < temptempfolders.GetCount(); ++i) {
			CString toadd(temptempfolders[i]);
			MakeFoldername(toadd);
			if (!::PathFileExists(toadd))
				::CreateDirectory(toadd, NULL);
			if (::PathFileExists(toadd))
				thePrefs.tempdir.Add(toadd);
		}
	}
	if (thePrefs.tempdir.IsEmpty())
		thePrefs.tempdir.Add(thePrefs.GetDefaultDirectory(EMULE_TEMPDIR, true));

	thePrefs.m_strIncomingDir = strIncomingDir;
	MakeFoldername(thePrefs.m_strIncomingDir);

	CStringList sharedDirs;
	m_ShareSelector.GetSharedDirectories(sharedDirs);

	// Normalise paths & optionally add subdirs
	for (POSITION pos = sharedDirs.GetHeadPosition(); pos != NULL;) {
		CString& strDir = sharedDirs.GetNext(pos);
		MakeFoldername(strDir); // Ensures trailing '\'
	}

		// Do not persistently append subdirectories to shared roots when AutoShareSubdirs is enabled.
		// AutoShareSubdirs affects scanning/runtime sharing, not the stored root list.
		// (UI tree may still show subdirs recursively; shareddir.dat should keep minimal roots.)


	// check shared directories for reserved folder names
	for (POSITION pos = sharedDirs.GetHeadPosition(); pos != NULL;) {
		POSITION posLast = pos;
		if (!thePrefs.IsShareableDirectory(sharedDirs.GetNext(pos)))
			sharedDirs.RemoveAt(posLast);
	}
	thePrefs.ReplaceSharedDirectoryList(sharedDirs);

	// on changing incoming dir, update directories for categories with the same path
	if (sOldIncoming.CompareNoCase(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR)) != 0) {
		thePrefs.GetCategory(0)->strIncomingPath = thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR);
		bool bAskedOnce = false;
		for (INT_PTR cat = thePrefs.GetCatCount(); --cat > 0;) { //skip 0
			const CString &oldpath(thePrefs.GetCatPath(cat));
			if (oldpath.Left(sOldIncoming.GetLength()).CompareNoCase(sOldIncoming) == 0) {
				if (!bAskedOnce) {
					bAskedOnce = true;
					if (LocMessageBox(_T("UPDATECATINCOMINGDIRS"), MB_YESNO, 0) == IDNO)
						break;
				}
				thePrefs.GetCategory(cat)->strIncomingPath = thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR) + oldpath.Mid(sOldIncoming.GetLength());
			}
		}
		thePrefs.SaveCats();
	}


	if (testtempdirchanged)
		LocMessageBox(_T("SETTINGCHANGED_RESTART"), MB_OK, 0);

	// Notify Shared Files window: shared roots changed -> force tree rebuild + rescan
	CemuleDlg* pDlg = theApp.emuledlg;
	if (pDlg && pDlg->sharedfileswnd && ::IsWindow(pDlg->sharedfileswnd->m_hWnd))
		::PostMessage(pDlg->sharedfileswnd->m_hWnd, UM_AUTO_RELOAD_SHARED_FILES, 1, 0);

	SetModified(0);
	return CPropertyPage::OnApply();
}

BOOL CPPgDirectories::OnCommand(WPARAM wParam, LPARAM lParam)
{
	if (wParam == UM_ITEMSTATECHANGED)
		SetModified();
	else if (wParam == ID_HELP) {
		OnHelp();
		return TRUE;
	}
	return CPropertyPage::OnCommand(wParam, lParam);
}

void CPPgDirectories::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(_T("PW_DIR")));

		SetDlgItemText(IDC_INCOMING_FRM, GetResString(_T("PW_INCOMING")));
		SetDlgItemText(IDC_TEMP_FRM, GetResString(_T("PW_TEMP")));
		SetDlgItemText(IDC_SHARED_FRM, GetResString(_T("PW_SHARED")));
	}
}




void CPPgDirectories::OnBnClickedAddUNC()
{
	InputBox inputbox;
	inputbox.SetLabels(GetResString(_T("UNCFOLDERS")), GetResString(_T("UNCFOLDERS")), _T("\\\\Server\\Share"));
	if (inputbox.DoModal() != IDOK)
		return;
	CString unc(inputbox.GetInput());

	// basic UNC check
	if (!::PathIsUNC(unc)) {
		LocMessageBox(_T("ERR_BADUNC"), MB_ICONERROR, 0);
		return;
	}
	slosh(unc);

	CStringList sharedDirs;
	thePrefs.CopySharedDirectoryList(sharedDirs);
	for (POSITION pos = sharedDirs.GetHeadPosition(); pos != NULL;)
		if (EqualPaths(sharedDirs.GetNext(pos), unc))
			return;

	if (m_ShareSelector.AddUNCShare(unc))

	SetModified();
}


void CPPgDirectories::OnHelp()
{
	theApp.ShowHelp(eMule_FAQ_Preferences_Directories);
}

BOOL CPPgDirectories::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}

void CPPgDirectories::OnBnClickedSeltempdiradd()
{
	CString paths;
	GetDlgItemText(IDC_TEMPFILES, paths);

	TCHAR buffer[MAX_PATH];

	if (SelectDir(GetSafeHwnd(), buffer, GetResString(_T("SELECT_TEMPDIR")))) {
		paths.AppendFormat(_T("|%s"), (LPCTSTR)buffer);
		SetDlgItemText(IDC_TEMPFILES, paths);
	}
}

void CPPgDirectories::OnDestroy()
{
	CPropertyPage::OnDestroy();
	if (m_icoBrowse) {
		VERIFY(::DestroyIcon(m_icoBrowse));
		m_icoBrowse = NULL;
	}
}

void CPPgDirectories::ErrorBalloon(int iEdit, LPCTSTR uid)
{
	static_cast<CEdit*>(GetDlgItem(iEdit))->ShowBalloonTip(GetResString(_T("ERROR")), GetResString(uid), TTI_ERROR);
}
