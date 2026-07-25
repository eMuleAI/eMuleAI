//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#include "stdafx.h"
#include "emule.h"
#include "MigrationWizardDlg.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "IPFilter.h"
#include "SHAHashSet.h"
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

	static const LPCTSTR kMigrationDetailsErrorProp = _T("MigrationDetailsError");

	struct MigrationFileDef
	{
		LPCTSTR pszFileName;
		bool bAllowParentFallback;
	};

	static const MigrationFileDef s_akMigrationFiles[] = {
		{ DOWNLOADS_TXT_FILENAME, true },
		{ DOWNLOAD_INSPECTOR_FILENAME, true },
		{ DOWNLOADVALIDATORFILE, false },
		{ AC_BOOTSTRAP_IPS_FILENAME, false },
		{ AC_BOOTSTRAP_URLS_FILENAME, false },
		{ AC_IPFILTER_UPDATE_URLS_FILENAME, false },
		{ AC_SEARCH_STRINGS_FILENAME, false },
		{ AC_SERVER_MET_URLS_FILENAME, false },
		{ AC_VF_REGEXP_FILENAME, false },
		{ ADDRESSES_DAT_FILENAME, false },
		{ BLACKLISTFILE, false },
		{ CANCELLED_MET_FILENAME, false },
		{ CATEGORY_INI_FILENAME, false },
		{ CLIENT_HISTORY_MET_FILENAME, false },
		{ CLIENTS_MET_FILENAME, false },
		{ CRYPTKEY_DAT_FILENAME, false },
		{ EMFRIENDS_MET_FILENAME, false },
		{ IPGEOLOCATION_DB_FILENAME, false },
		{ DFLT_IPFILTER_FILENAME, false },
		{ DFLT_STATIC_IPFILTER_FILENAME, false },
		{ DFLT_WHITE_IPFILTER_FILENAME, false },
		{ KAD_KEY_INDEX_FILENAME, false },
		{ KNOWN_MET_FILENAME, false },
		{ KNOWN2_MET_FILENAME, false },
		{ KAD_LOAD_INDEX_FILENAME, false },
		{ NODES_DAT_FILENAME, false },
		{ NOTIFIER_INI_FILENAME, false },
		{ PREFERENCES_DAT_FILENAME, false },
		{ KAD_PREFERENCES_DAT_FILENAME, false },
		{ PREVIEW_APPS_FILENAME, false },
		{ SPAMFILTER_FILENAME, false },
		{ SERVER_MET_FILENAME, false },
		{ SHAREDDIRS, false },
		{ SHARED_FILES_FILENAME, false },
		{ SHARED_CACHE_FILENAME, false },
		{ SHARED_SUBDIR_FILENAME, false },
		{ KAD_SRC_INDEX_FILENAME, false },
		{ STATIC_SERVERS_FILENAME, false },
		{ STATISTICS_INI_FILENAME, false },
		{ STOREDSEARCHES_FILENAME, false }
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
		okText = GetResString(_T("MB_OK"));
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
