//This file is part of eMule AI
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
#include "MigrationWizardDlg.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "Ini2.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	enum
	{
		kMigrationDialogWidthDlu = 392,
		kMigrationMarginXDlu = 10,
		kMigrationContentXDlu = 92,
		kMigrationMarginTopDlu = 12,
		kMigrationContentWidthDlu = 290,
		kMigrationMessageHeightDlu = 38,
		kMigrationSectionGapDlu = 8,
		kMigrationSmallGapDlu = 3,
		kMigrationSourceLabelHeightDlu = 8,
		kMigrationSourceEditHeightDlu = 14,
		kMigrationDetailsHeightDlu = 30,
		kMigrationButtonWidthDlu = 80,
		kMigrationButtonHeightDlu = 14,
		kMigrationButtonGapDlu = 6,
		kMigrationBottomMarginDlu = 12,
		kDialogSideIconSizePx = 96
	};

	static const LPCTSTR kMigrationDetailsErrorProp = _T("BB_MigrationDetailsError");

	struct MigrationFileDef
	{
		LPCTSTR pszFileName;
		bool bAllowParentFallback;
	};

	static const MigrationFileDef s_akMigrationFiles[] = {
		{ _T("downloads.txt"), true },
		{ _T("AC_BootstrapIPs.dat"), false },
		{ _T("AC_BootstrapURLs.dat"), false },
		{ _T("AC_IPFilterUpdateURLs.dat"), false },
		{ _T("AC_SearchStrings.dat"), false },
		{ _T("AC_ServerMetURLs.dat"), false },
		{ _T("AC_VF_RegExpr.dat"), false },
		{ _T("addresses.dat"), false },
		{ _T("blacklist.conf"), false },
		{ _T("cancelled.met"), false },
		{ _T("Category.ini"), false },
		{ _T("clienthistory.met"), false },
		{ _T("clients.met"), false },
		{ _T("cryptkey.dat"), false },
		{ _T("emfriends.met"), false },
		{ _T("GeoLite2-City.mmdb"), false },
		{ _T("ipfilter.dat"), false },
		{ _T("ipfilter_static.dat"), false },
		{ _T("ipfilter_white.dat"), false },
		{ _T("key_index.dat"), false },
		{ _T("known.met"), false },
		{ _T("known2_64.met"), false },
		{ _T("load_index.dat"), false },
		{ _T("nodes.dat"), false },
		{ _T("Notifier.ini"), false },
		{ _T("preferences.dat"), false },
		{ _T("preferencesKad.dat"), false },
		{ _T("PreviewApps.dat"), false },
		{ _T("SearchSpam.met"), false },
		{ _T("server.met"), false },
		{ _T("shareddir.dat"), false },
		{ _T("sharedfiles.dat"), false },
		{ _T("sharedsubdir.ini"), false },
		{ _T("src_index.dat"), false },
		{ _T("staticservers.dat"), false },
		{ _T("statistics.ini"), false },
		{ _T("StoredSearches.met"), false }
	};

	CString BuildFailedFileList(const CStringArray &failedFiles)
	{
		CString result;
		for (INT_PTR i = 0; i < failedFiles.GetCount(); ++i) {
			if (!result.IsEmpty())
				result += _T("\r\n");
			result += failedFiles[i];
		}
		return result;
	}

	bool HasAnyConfigScopedMigrationFile(const CString &configDir)
	{
		CString normalizedConfigDir(configDir);
		MakeFoldername(normalizedConfigDir);

		for (const MigrationFileDef &fileDef : s_akMigrationFiles) {
			if (fileDef.bAllowParentFallback)
				continue;
			if (::PathFileExists(normalizedConfigDir + fileDef.pszFileName))
				return true;
		}

		return false;
	}

	CRect GetRectFromDialogUnits(CDialog *pDlg, int x, int y, int cx, int cy)
	{
		CRect rect(x, y, x + cx, y + cy);
		pDlg->MapDialogRect(&rect);
		return rect;
	}

	HICON LoadDialogSideIcon()
	{
		return (HICON)::LoadImage(AfxGetResourceHandle(), _T("EMULEAI_SIDE"), IMAGE_ICON, kDialogSideIconSizePx, kDialogSideIconSizePx, LR_DEFAULTCOLOR | LR_SHARED);
	}
}

IMPLEMENT_DYNAMIC(CMigrationWizardDlg, CDialog)

CMigrationWizardDlg::CMigrationWizardDlg(bool bStartupMode, CWnd *pParent /*=NULL*/)
	: CDialog(CMigrationWizardDlg::IDD, pParent)
	, m_bStartupMode(bStartupMode)
	, m_bDefaultConfigAvailable(false)
	, m_bRestoreCompleted(false)
	, m_bRestoreHadErrors(false)
	, m_bUseDarkModeTheme(false)
	, m_uCopiedCount(0)
{
	CString resolvedConfigDir;
	m_strDefaultConfigDir = GetProfileLegacyConfigDir();
	if (!m_strDefaultConfigDir.IsEmpty() && TryResolveConfigDir(m_strDefaultConfigDir, resolvedConfigDir)) {
		m_bDefaultConfigAvailable = true;
		m_strDefaultConfigDir = resolvedConfigDir;
		m_strSelectedConfigDir = resolvedConfigDir;
	}
}

CMigrationWizardDlg::~CMigrationWizardDlg()
{
}

void CMigrationWizardDlg::DoDataExchange(CDataExchange *pDX)
{
	CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CMigrationWizardDlg, CDialog)
	ON_BN_CLICKED(IDC_MIGRATION_SELECT_DIR, OnBnClickedSelectDir)
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL CMigrationWizardDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	GetSystemDarkModeStatus();
	m_bUseDarkModeTheme = IsDarkModeEnabled();
	if (m_bUseDarkModeTheme) {
		CDarkMode::Initialize();
		ApplyTheme(m_hWnd);
	}
	if (CWnd *pIcon = GetDlgItem(IDC_MIGRATION_ICON))
		pIcon->SendMessage(STM_SETICON, (WPARAM)LoadDialogSideIcon(), 0);
	Localize();
	CenterWindow(CWnd::FromHandle(::GetDesktopWindow()));
	return TRUE;
}

void CMigrationWizardDlg::Localize()
{
	SetWindowText(GetResString(_T("EMULE_AI_MIGRATION_WIZARD")));
	SetDlgItemText(IDC_MIGRATION_SOURCE_DIR_LABEL, GetResString(_T("EMULE_AI_MIGRATION_WIZARD_SOURCE_DIR")));
	UpdateDialogState();
}

void CMigrationWizardDlg::OnOK()
{
	if (m_bRestoreCompleted) {
		CDialog::OnOK();
		return;
	}

	if (!m_strSelectedConfigDir.IsEmpty())
		RestoreFromConfigDir(m_strSelectedConfigDir);
}

void CMigrationWizardDlg::OnBnClickedSelectDir()
{
	BrowseAndRestore();
}

HBRUSH CMigrationWizardDlg::OnCtlColor(CDC *pDC, CWnd *pWnd, UINT nCtlColor)
{
	if (!m_bUseDarkModeTheme) {
		if (pWnd != NULL && pWnd->GetDlgCtrlID() == IDC_MIGRATION_DETAILS) {
			const bool bHighlightAsError = m_bRestoreHadErrors || (!m_bRestoreCompleted && !m_strInlineDetails.IsEmpty());
			if (bHighlightAsError) {
				pDC->SetBkColor(::GetSysColor(COLOR_WINDOW));
				pDC->SetTextColor(thePrefs.GetLogErrorColor());
				return ::GetSysColorBrush(COLOR_WINDOW);
			}
		}
		return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
	}

	HBRUSH hBrush = HandleCtlColor(m_hWnd, pDC, pWnd != NULL ? pWnd->GetSafeHwnd() : NULL, nCtlColor);
	if (pWnd != NULL && pWnd->GetDlgCtrlID() == IDC_MIGRATION_DETAILS) {
		const bool bHighlightAsError = m_bRestoreHadErrors || (!m_bRestoreCompleted && !m_strInlineDetails.IsEmpty());
		if (bHighlightAsError) {
			pDC->SetBkColor(GetCustomSysColor(COLOR_WINDOW));
			pDC->SetTextColor(thePrefs.GetLogErrorColor());
		}
	}
	return hBrush != NULL ? hBrush : CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

bool CMigrationWizardDlg::BrowseAndRestore()
{
	TCHAR buffer[MAX_PATH] = {};
	_tcsncpy(buffer, m_strSelectedConfigDir, _countof(buffer) - 1);
	if (!SelectDir(GetSafeHwnd(), buffer, GetResString(_T("EMULE_AI_MIGRATION_WIZARD"))))
		return false;

	CString resolvedConfigDir;
	if (!TryResolveConfigDir(buffer, resolvedConfigDir)) {
		m_strInlineDetails = GetResString(_T("EMULE_AI_MIGRATION_WIZARD_INVALID_DIR"));
		UpdateDialogState();
		return false;
	}

	m_strSelectedConfigDir = resolvedConfigDir;
	m_strInlineDetails.Empty();
	return RestoreFromConfigDir(resolvedConfigDir);
}

bool CMigrationWizardDlg::RestoreFromConfigDir(const CString &configDir)
{
	CString normalizedConfigDir(configDir);
	MakeFoldername(normalizedConfigDir);
	if (!HasAnyMigrationFile(normalizedConfigDir)) {
		m_strSelectedConfigDir = normalizedConfigDir;
		m_strInlineDetails.Format(GetResString(_T("EMULE_AI_MIGRATION_WIZARD_RESULT_EMPTY")), (LPCTSTR)normalizedConfigDir);
		UpdateDialogState();
		return false;
	}

	m_strSelectedConfigDir = normalizedConfigDir;
	m_strInlineDetails.Empty();
	m_astrFailedFiles.RemoveAll();

	int copiedCount = 0;
	for (const MigrationFileDef &fileDef : s_akMigrationFiles)
		CopyMigrationFile(normalizedConfigDir, fileDef.pszFileName, fileDef.bAllowParentFallback, copiedCount);

	thePrefs.ImportLegacyPreferencesIniForMigration(normalizedConfigDir);
	if (m_bStartupMode)
		thePrefs.ReloadStartupStateAfterMigration();

	m_bRestoreCompleted = true;
	m_bRestoreHadErrors = !m_astrFailedFiles.IsEmpty();
	m_uCopiedCount = (UINT)copiedCount;
	UpdateDialogState();
	return true;
}

bool CMigrationWizardDlg::CopyMigrationFile(const CString &sourceConfigDir, LPCTSTR pszFileName, bool bAllowParentFallback, int &copiedCount)
{
	CString sourcePath(sourceConfigDir + pszFileName);
	if (bAllowParentFallback && !::PathFileExists(sourcePath)) {
		const CString parentDir(GetParentDirectory(sourceConfigDir));
		if (!parentDir.IsEmpty())
			sourcePath = parentDir + pszFileName;
	}

	if (!::PathFileExists(sourcePath))
		return true;

	const CString destinationPath(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + CString(pszFileName));
	if (EqualPaths(sourcePath, destinationPath))
		return true;

	if (!::CopyFile(sourcePath, destinationPath, FALSE)) {
		m_astrFailedFiles.Add(pszFileName);
		TRACE(_T("MigrationWizard: failed to copy '%s' from '%s' to '%s' (error=%lu)\n"), pszFileName, (LPCTSTR)sourcePath, (LPCTSTR)destinationPath, ::GetLastError());
		return false;
	}

	++copiedCount;
	return true;
}

CString CMigrationWizardDlg::BuildResultSummaryText() const
{
	CString message;
	if (m_bRestoreHadErrors)
		message.Format(GetResString(_T("EMULE_AI_MIGRATION_WIZARD_RESULT_PARTIAL_SUMMARY")), m_uCopiedCount);
	else
		message.Format(GetResString(_T("EMULE_AI_MIGRATION_WIZARD_RESULT_SUCCESS_SUMMARY")), m_uCopiedCount);
	return message;
}

CString CMigrationWizardDlg::BuildResultDetailsText() const
{
	CString details;
	if (m_bRestoreHadErrors && !m_astrFailedFiles.IsEmpty())
		details.Format(GetResString(_T("EMULE_AI_MIGRATION_WIZARD_FAILED_FILES")), (LPCTSTR)BuildFailedFileList(m_astrFailedFiles));
	if (!m_bStartupMode) {
		if (!details.IsEmpty())
			details += _T("\r\n\r\n");
		details += GetResString(_T("EMULE_AI_MIGRATION_WIZARD_RESTART_NOTE"));
	}
	return details;
}

void CMigrationWizardDlg::UpdateDialogState()
{
	CString mainMessage;
	CString detailsMessage;
	CString okText;
	CString selectText;
	bool bShowSourceDir = !m_strSelectedConfigDir.IsEmpty();
	bool bShowDetails = false;
	bool bShowOk = false;
	bool bShowSelect = false;
	bool bShowCancel = false;
	bool bHighlightDetailsAsError = false;
	UINT nDefaultButton = IDCANCEL;

	if (m_bRestoreCompleted) {
		mainMessage = BuildResultSummaryText();
		detailsMessage = BuildResultDetailsText();
		bShowDetails = !detailsMessage.IsEmpty();
		bHighlightDetailsAsError = m_bRestoreHadErrors;
		bShowOk = true;
		okText = GetResString(_T("EMULE_AI_MIGRATION_WIZARD_DONE"));
		nDefaultButton = IDOK;
	}
	else {
		mainMessage = m_bDefaultConfigAvailable ? GetResString(_T("EMULE_AI_MIGRATION_WIZARD_FOUND_TEXT")) : GetResString(_T("EMULE_AI_MIGRATION_WIZARD_MISSING_TEXT"));
		detailsMessage = m_strInlineDetails;
		bShowDetails = !detailsMessage.IsEmpty();
		bHighlightDetailsAsError = !m_strInlineDetails.IsEmpty();
		bShowSelect = true;
		bShowCancel = true;
		selectText = m_bDefaultConfigAvailable ? GetResString(_T("EMULE_AI_MIGRATION_WIZARD_SELECT_OTHER_DIR")) : GetResString(_T("EMULE_AI_MIGRATION_WIZARD_SELECT_DIR"));
		if (m_bDefaultConfigAvailable) {
			bShowOk = true;
			okText = GetResString(_T("YES"));
			nDefaultButton = IDOK;
		}
		else {
			nDefaultButton = IDC_MIGRATION_SELECT_DIR;
		}
	}

	SetRedraw(FALSE);

	SetDlgItemText(IDC_MIGRATION_MESSAGE, mainMessage);
	if (bShowSourceDir)
		SetDlgItemText(IDC_MIGRATION_SOURCE_DIR, m_strSelectedConfigDir);
	if (CWnd *pDetailsWnd = GetDlgItem(IDC_MIGRATION_DETAILS)) {
		if (bShowDetails && bHighlightDetailsAsError)
			::SetProp(pDetailsWnd->GetSafeHwnd(), kMigrationDetailsErrorProp, (HANDLE)1);
		else
			::RemoveProp(pDetailsWnd->GetSafeHwnd(), kMigrationDetailsErrorProp);
	}
	if (bShowDetails) {
		SetDlgItemText(IDC_MIGRATION_DETAILS, detailsMessage);
		CEdit *pDetailsEdit = (CEdit *)GetDlgItem(IDC_MIGRATION_DETAILS);
		if (pDetailsEdit != NULL) {
			const bool bEnableDetailsScroll = m_bRestoreHadErrors && !m_astrFailedFiles.IsEmpty();
			pDetailsEdit->ModifyStyle(bEnableDetailsScroll ? 0 : WS_VSCROLL, bEnableDetailsScroll ? WS_VSCROLL : 0, SWP_FRAMECHANGED);
			pDetailsEdit->SetSel(0, 0);
		}
	}

	if (bShowOk)
		SetDlgItemText(IDOK, okText);
	if (bShowSelect)
		SetDlgItemText(IDC_MIGRATION_SELECT_DIR, selectText);
	if (bShowCancel)
		SetDlgItemText(IDCANCEL, GetResString(_T("NO")));

	GetDlgItem(IDC_MIGRATION_SOURCE_DIR_LABEL)->ShowWindow(bShowSourceDir ? SW_SHOW : SW_HIDE);
	GetDlgItem(IDC_MIGRATION_SOURCE_DIR)->ShowWindow(bShowSourceDir ? SW_SHOW : SW_HIDE);
	GetDlgItem(IDC_MIGRATION_DETAILS)->ShowWindow(bShowDetails ? SW_SHOW : SW_HIDE);
	GetDlgItem(IDOK)->ShowWindow(bShowOk ? SW_SHOW : SW_HIDE);
	GetDlgItem(IDC_MIGRATION_SELECT_DIR)->ShowWindow(bShowSelect ? SW_SHOW : SW_HIDE);
	GetDlgItem(IDCANCEL)->ShowWindow(bShowCancel ? SW_SHOW : SW_HIDE);

	UpdateLayout(bShowSourceDir, bShowDetails, bShowOk, bShowSelect, bShowCancel);
	SetDefaultButton(nDefaultButton);

	SetRedraw(TRUE);
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void CMigrationWizardDlg::UpdateLayout(bool bShowSourceDir, bool bShowDetails, bool bShowOk, bool bShowSelect, bool bShowCancel)
{
	int y = kMigrationMarginTopDlu;
	const int contentLeft = kMigrationContentXDlu;

	GetDlgItem(IDC_MIGRATION_MESSAGE)->MoveWindow(GetRectFromDialogUnits(this, contentLeft, y, kMigrationContentWidthDlu, kMigrationMessageHeightDlu));
	y += kMigrationMessageHeightDlu;

	if (bShowSourceDir) {
		y += kMigrationSectionGapDlu;
		GetDlgItem(IDC_MIGRATION_SOURCE_DIR_LABEL)->MoveWindow(GetRectFromDialogUnits(this, contentLeft, y, kMigrationContentWidthDlu, kMigrationSourceLabelHeightDlu));
		y += kMigrationSourceLabelHeightDlu + kMigrationSmallGapDlu;
		GetDlgItem(IDC_MIGRATION_SOURCE_DIR)->MoveWindow(GetRectFromDialogUnits(this, contentLeft, y, kMigrationContentWidthDlu, kMigrationSourceEditHeightDlu));
		y += kMigrationSourceEditHeightDlu;
	}

	if (bShowDetails) {
		y += kMigrationSectionGapDlu;
		GetDlgItem(IDC_MIGRATION_DETAILS)->MoveWindow(GetRectFromDialogUnits(this, contentLeft, y, kMigrationContentWidthDlu, kMigrationDetailsHeightDlu));
		y += kMigrationDetailsHeightDlu;
	}

	y += kMigrationSectionGapDlu;

	UINT aButtonIds[3] = {};
	int iVisibleButtonCount = 0;
	if (bShowOk)
		aButtonIds[iVisibleButtonCount++] = IDOK;
	if (bShowSelect)
		aButtonIds[iVisibleButtonCount++] = IDC_MIGRATION_SELECT_DIR;
	if (bShowCancel)
		aButtonIds[iVisibleButtonCount++] = IDCANCEL;

	if (iVisibleButtonCount > 0) {
		const int buttonGroupWidth = iVisibleButtonCount * kMigrationButtonWidthDlu + (iVisibleButtonCount - 1) * kMigrationButtonGapDlu;
		int x = (kMigrationDialogWidthDlu - buttonGroupWidth) / 2;
		for (int i = 0; i < iVisibleButtonCount; ++i) {
			GetDlgItem(aButtonIds[i])->MoveWindow(GetRectFromDialogUnits(this, x, y, kMigrationButtonWidthDlu, kMigrationButtonHeightDlu));
			x += kMigrationButtonWidthDlu + kMigrationButtonGapDlu;
		}
	}

	const int clientHeightDlu = y + kMigrationButtonHeightDlu + kMigrationBottomMarginDlu;
	const CRect rcClient(GetRectFromDialogUnits(this, 0, 0, kMigrationDialogWidthDlu, clientHeightDlu));
	CRect rcWindow;
	GetWindowRect(&rcWindow);
	CRect currentClient;
	GetClientRect(&currentClient);
	const int nonClientWidth = rcWindow.Width() - currentClient.Width();
	const int nonClientHeight = rcWindow.Height() - currentClient.Height();
	SetWindowPos(NULL, 0, 0, rcClient.Width() + nonClientWidth, rcClient.Height() + nonClientHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CMigrationWizardDlg::SetDefaultButton(UINT nID)
{
	static const UINT s_auiButtonIds[] = { IDOK, IDC_MIGRATION_SELECT_DIR, IDCANCEL };
	for (UINT nButtonId : s_auiButtonIds) {
		CWnd *pButton = GetDlgItem(nButtonId);
		if (pButton == NULL)
			continue;
		pButton->ModifyStyle(BS_DEFPUSHBUTTON, BS_PUSHBUTTON, SWP_FRAMECHANGED);
		if (nButtonId == nID)
			pButton->ModifyStyle(BS_PUSHBUTTON, BS_DEFPUSHBUTTON, SWP_FRAMECHANGED);
	}

	SendMessage(DM_SETDEFID, nID);
}

CString CMigrationWizardDlg::GetProfileLegacyConfigDir()
{
	CString baseDir;
	if (thePrefs.GetWindowsVersion() >= _WINVER_VISTA_)
		baseDir = ShellGetFolderPath(CSIDL_LOCAL_APPDATA);
	else
		baseDir = ShellGetFolderPath(CSIDL_APPDATA);

	if (baseDir.IsEmpty())
		return CString();

	MakeFoldername(baseDir);
	baseDir += _T("eMule\\config\\");
	return baseDir;
}

CString CMigrationWizardDlg::GetParentDirectory(const CString &configDir)
{
	CString parentDir(configDir);
	if (parentDir.IsEmpty())
		return parentDir;

	unslosh(parentDir);
	::PathRemoveFileSpec(parentDir.GetBuffer());
	parentDir.ReleaseBuffer();
	MakeFoldername(parentDir);
	return parentDir;
}

bool CMigrationWizardDlg::TryResolveConfigDir(const CString &candidateDir, CString &resolvedConfigDir)
{
	if (candidateDir.IsEmpty())
		return false;

	CString directConfigDir(candidateDir);
	MakeFoldername(directConfigDir);
	if (HasAnyConfigScopedMigrationFile(directConfigDir)) {
		resolvedConfigDir = directConfigDir;
		return true;
	}

	CString nestedConfigDir(directConfigDir + _T("config\\"));
	if (HasAnyMigrationFile(nestedConfigDir)) {
		resolvedConfigDir = nestedConfigDir;
		return true;
	}

	if (HasAnyMigrationFile(directConfigDir)) {
		resolvedConfigDir = directConfigDir;
		return true;
	}

	return false;
}

bool CMigrationWizardDlg::HasAnyMigrationFile(const CString &configDir)
{
	CString normalizedConfigDir(configDir);
	MakeFoldername(normalizedConfigDir);

	for (const MigrationFileDef &fileDef : s_akMigrationFiles) {
		if (::PathFileExists(normalizedConfigDir + fileDef.pszFileName))
			return true;
		if (fileDef.bAllowParentFallback) {
			const CString parentDir(GetParentDirectory(normalizedConfigDir));
			if (!parentDir.IsEmpty() && ::PathFileExists(parentDir + fileDef.pszFileName))
				return true;
		}
	}

	return false;
}
