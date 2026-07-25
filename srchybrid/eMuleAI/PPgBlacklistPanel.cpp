//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#include "stdafx.h"
#include <Richedit.h>
#include "emule.h"
#include "MuleStatusbarCtrl.h"
#include "ClientList.h"
#include "PPgBlacklistPanel.h"
#include "PreferencesDlg.h"
#include "emuledlg.h"
#include "SearchDlg.h"
#include "SearchResultsWnd.h"
#include "UserMsgs.h"
#include "Log.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#endif

namespace
{
	int GetMultilineCheckboxIdealHeight(CWnd* pCheckBox)
	{
		CString strText;
		pCheckBox->GetWindowText(strText);

		CRect rcCheckBox;
		pCheckBox->GetClientRect(&rcCheckBox);
		CClientDC dc(pCheckBox);
		CFont* pOldFont = NULL;
		if (pCheckBox->GetFont() != NULL)
			pOldFont = dc.SelectObject(pCheckBox->GetFont());
		TEXTMETRIC textMetric = {};
		dc.GetTextMetrics(&textMetric);
		const int iGlyphWidth = max(::GetSystemMetrics(SM_CXMENUCHECK), textMetric.tmHeight);
		CRect rcText(0, 0, max(1, rcCheckBox.Width() - iGlyphWidth - CPreferencesDlg::ScaleOptionsValue(3)), 0);
		dc.DrawText(strText, &rcText, DT_CALCRECT | DT_LEFT | DT_WORDBREAK);
		if (pOldFont != NULL)
			dc.SelectObject(pOldFont);

		const int iGlyphHeight = max(::GetSystemMetrics(SM_CYMENUCHECK), textMetric.tmHeight);
		return max(rcText.Height(), iGlyphHeight) + max(CPreferencesDlg::ScaleOptionsValue(2), textMetric.tmExternalLeading);
	}

	CString FormatToolTipText(const CString& strText)
	{
		CString strNormalized(strText);
		strNormalized.Replace(_T("\r\n"), _T("\n"));
		strNormalized.Replace(_T('\r'), _T('\n'));

		CString strResult;
		int iPos = 0;
		const int iWrapColumn = 88;
		while (iPos >= 0) {
			CString strRemaining(strNormalized.Tokenize(_T("\n"), iPos));
			if (strRemaining.IsEmpty()) {
				if (!strResult.IsEmpty())
					strResult += _T("\r\n");
				continue;
			}

			while (!strRemaining.IsEmpty()) {
				if (strRemaining.GetLength() <= iWrapColumn) {
					if (!strResult.IsEmpty())
						strResult += _T("\r\n");
					strResult += strRemaining;
					break;
				}

				int iBreakPos = strRemaining.Left(iWrapColumn + 1).ReverseFind(_T(' '));
				if (iBreakPos <= 0)
					iBreakPos = iWrapColumn;

				CString strWrappedLine(strRemaining.Left(iBreakPos));
				strWrappedLine.TrimRight();
				if (!strResult.IsEmpty())
					strResult += _T("\r\n");
				strResult += strWrappedLine;

				strRemaining = strRemaining.Mid(iBreakPos);
				strRemaining.TrimLeft();
			}
		}
		strResult.TrimRight(_T("\r\n"));
		return strResult;
	}
}

///////////////////////////////////////////////////////////////////////////////
// CPPgBlacklistPanel dialog

IMPLEMENT_DYNAMIC(CPPgBlacklistPanel, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgBlacklistPanel, CPropertyPage)
	ON_WM_HSCROLL()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_BLACKLIST_ENABLE_AUTOMATIC_CHECKBOX, OnSettingsChange)
	ON_BN_CLICKED(IDC_BLACKLIST_ENABLE_MANUAL_CHECKBOX, OnSettingsChange)
	ON_BN_CLICKED(IDC_BLACKLIST_AUTOREMOVE_CHECKBOX, OnSettingsChange)
	ON_BN_CLICKED(IDC_BLACKLIST_LOG_CHECKBOX, OnSettingsChange)
	ON_EN_CHANGE(IDC_BLACKLIST_DEFINITIONS_TEXTBOX, OnDefinitionsChanged)
	ON_BN_CLICKED(IDC_BLACKLIST_VALIDATE, OnValidateDefinitions)
	ON_BN_CLICKED(IDC_BLACKLIST_RELOAD, OnReloadDefinitions)
	ON_WM_SYSCOLORCHANGE()
	ON_WM_SIZE()
	ON_NOTIFY(TCN_SELCHANGE, IDC_BLACKLIST_VIEW_TABS, OnTcnSelchangeViewTabs)
END_MESSAGE_MAP()

CPPgBlacklistPanel::CPPgBlacklistPanel()
	: CPropertyPage(CPPgBlacklistPanel::IDD)
	, m_eViewMode(ViewSettings)
	, m_bUpdatingDefinitions(false)
	, m_bDefinitionsDirty(false)
	, m_bSettingsDirty(false)
{
}

CPPgBlacklistPanel::~CPPgBlacklistPanel()
{
}

void CPPgBlacklistPanel::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_BLACKLIST_VIEW_TABS, m_ctrlViewTabs);
}

void CPPgBlacklistPanel::LoadSettings(void)
{
	CheckDlgButton(IDC_BLACKLIST_ENABLE_AUTOMATIC_CHECKBOX, thePrefs.GetBlacklistAutomatic());
	CheckDlgButton(IDC_BLACKLIST_ENABLE_MANUAL_CHECKBOX, thePrefs.GetBlacklistManual());
	CheckDlgButton(IDC_BLACKLIST_AUTOREMOVE_CHECKBOX, thePrefs.GetBlacklistAutoRemoveFromManual());
	CheckDlgButton(IDC_BLACKLIST_LOG_CHECKBOX, thePrefs.GetBlacklistLog());
	m_bUpdatingDefinitions = true;
	thePrefs.LoadBlacklistFile(); // Loads blacklist.conf
	m_bUpdatingDefinitions = false;
	m_bDefinitionsDirty = false;
}

void CPPgBlacklistPanel::ResetToDefaults()
{
	CheckDlgButton(IDC_BLACKLIST_ENABLE_AUTOMATIC_CHECKBOX, BST_CHECKED);
	CheckDlgButton(IDC_BLACKLIST_ENABLE_MANUAL_CHECKBOX, BST_CHECKED);
	CheckDlgButton(IDC_BLACKLIST_AUTOREMOVE_CHECKBOX, BST_CHECKED);
	CheckDlgButton(IDC_BLACKLIST_LOG_CHECKBOX, BST_UNCHECKED);
	m_bSettingsDirty = true;
	SetModified(TRUE);
}

BOOL CPPgBlacklistPanel::OnApply()
{
	// if prop page is closed by pressing VK_ENTER we have to explicitly commit any possibly pending
	// data from an open edit control

	if (!UpdateData())
		return FALSE;

	CString strDefinitions;
	GetDlgItemText(IDC_BLACKLIST_DEFINITIONS_TEXTBOX, strDefinitions);
	UINT uErrorLine = 0;
	UINT uRuleCount = 0;
	if (!thePrefs.ApplyBlacklistDefinitions(strDefinitions, uErrorLine, uRuleCount)) {
		SelectDefinitionErrorLine(uErrorLine);
		CString strError;
		strError.Format(GetResString(_T("BLACKLIST_VALIDATION_ERROR")), uErrorLine);
		CDarkMode::MessageBox(strError, MB_ICONWARNING);
		return FALSE;
	}
	thePrefs.SetBlacklistAutomatic(IsDlgButtonChecked(IDC_BLACKLIST_ENABLE_AUTOMATIC_CHECKBOX));
	thePrefs.SetBlacklistManual(IsDlgButtonChecked(IDC_BLACKLIST_ENABLE_MANUAL_CHECKBOX));
	thePrefs.SetBlacklistAutoRemoveFromManual(IsDlgButtonChecked(IDC_BLACKLIST_AUTOREMOVE_CHECKBOX));
	thePrefs.SetBlacklistLog(IsDlgButtonChecked(IDC_BLACKLIST_LOG_CHECKBOX));
	thePrefs.SaveBlacklistFile(); // This will show trimmed textbox as it is saved to the file
	if (theApp.emuledlg != NULL && theApp.emuledlg->searchwnd != NULL && theApp.emuledlg->searchwnd->m_pwndResults != NULL)
		theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.QueueDeferredReload(false, LSF_SELECTION, 50);

	m_bDefinitionsDirty = false;
	m_bSettingsDirty = false;
	SetModified(FALSE);
	return CPropertyPage::OnApply();
}

void CPPgBlacklistPanel::ConfigureBlacklistDefinitionsTextBox()
{
	CWnd* pWnd = GetDlgItem(IDC_BLACKLIST_DEFINITIONS_TEXTBOX);
	if (pWnd == NULL || pWnd->GetSafeHwnd() == NULL)
		return;

	static const WPARAM kBlacklistDefinitionsTextLimit = 4 * 1024 * 1024;
	pWnd->SendMessage(EM_LIMITTEXT, kBlacklistDefinitionsTextLimit);
	pWnd->SendMessage(EM_EXLIMITTEXT, 0, (LPARAM)kBlacklistDefinitionsTextLimit);
	pWnd->SendMessage(EM_SETEVENTMASK, 0, pWnd->SendMessage(EM_GETEVENTMASK) | ENM_CHANGE);
}

void CPPgBlacklistPanel::UpdateBlacklistDefinitionsTextBoxColors()
{
	const COLORREF crBackground = GetCustomSysColor(COLOR_WINDOW);
	const COLORREF crText = GetCustomSysColor(COLOR_WINDOWTEXT);
	const UINT aControlIds[] = { IDC_BLACKLIST_DEFINITIONS_TEXTBOX, IDC_BLACKLIST_PANEL_HELP_TEXTBOX };
	for (size_t i = 0; i < _countof(aControlIds); ++i) {
		CWnd* pWnd = GetDlgItem(aControlIds[i]);
		if (pWnd == NULL || pWnd->GetSafeHwnd() == NULL)
			continue;

		pWnd->SendMessage(EM_SETBKGNDCOLOR, 0, crBackground);
		CHARFORMAT cf = {};
		cf.cbSize = sizeof(cf);
		cf.dwMask = CFM_COLOR;
		cf.dwEffects = 0;
		cf.crTextColor = crText;
		pWnd->SendMessage(EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
		pWnd->SendMessage(EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
		pWnd->Invalidate();
	}
}

BOOL CPPgBlacklistPanel::OnInitDialog()
{
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);
	m_ctrlViewTabs.m_bClosable = false;
	m_ctrlViewTabs.m_bShowCloseButton = false;
	m_ctrlViewTabs.SetVisualScalePercent(CPreferencesDlg::ScaleOptionsValue(100));
	InitializeViewTabs();
	ConfigureBlacklistDefinitionsTextBox();
	CWnd* pHelpText = GetDlgItem(IDC_BLACKLIST_PANEL_HELP_TEXTBOX);
	if (pHelpText != NULL)
		pHelpText->SendMessage(EM_SETREADONLY, TRUE);
	LoadSettings();
	Localize();
	UpdateBlacklistDefinitionsTextBoxColors();
	SwitchView(ViewSettings);
	UpdateLayout();

	return TRUE;  // return TRUE unless you set the focus to the control. EXCEPTION: OCX Property Pages should return FALSE
}

void CPPgBlacklistPanel::UpdateLayout()
{
	UpdateOptionsLayout();

	CWnd* pTabs = GetDlgItem(IDC_BLACKLIST_VIEW_TABS);
	CWnd* pDefinitionsFrame = GetDlgItem(IDC_BLACKLIST_DEF_FRM);
	CWnd* pDefinitionsText = GetDlgItem(IDC_BLACKLIST_DEFINITIONS_TEXTBOX);
	CWnd* pHelpText = GetDlgItem(IDC_BLACKLIST_PANEL_HELP_TEXTBOX);
	CWnd* pValidate = GetDlgItem(IDC_BLACKLIST_VALIDATE);
	CWnd* pReload = GetDlgItem(IDC_BLACKLIST_RELOAD);
	if (pTabs == NULL || pDefinitionsFrame == NULL || pDefinitionsText == NULL || pHelpText == NULL || pValidate == NULL || pReload == NULL)
		return;

	CRect rcClient;
	GetClientRect(&rcClient);
	CRect rcTabs;
	pTabs->GetWindowRect(&rcTabs);
	ScreenToClient(&rcTabs);
	const int iMargin = max(1, rcTabs.left);
	rcTabs.right = rcClient.right - iMargin;
	pTabs->MoveWindow(&rcTabs);

	CRect rcDefinitionsFrame;
	CRect rcDefinitionsText;
	pDefinitionsFrame->GetWindowRect(&rcDefinitionsFrame);
	pDefinitionsText->GetWindowRect(&rcDefinitionsText);
	ScreenToClient(&rcDefinitionsFrame);
	ScreenToClient(&rcDefinitionsText);
	const int iTextRightMargin = max(1, rcDefinitionsFrame.right - rcDefinitionsText.right);
	CRect rcDialogUnits(0, 0, 50, 14);
	MapDialogRect(&rcDialogUnits);
	const int iButtonWidth = CPreferencesDlg::ScaleOptionsValue(max(1, rcDialogUnits.right));
	const int iButtonHeight = CPreferencesDlg::ScaleOptionsValue(max(1, rcDialogUnits.bottom));
	const int iGap = CPreferencesDlg::ScaleOptionsValue(max(1, ::GetSystemMetrics(SM_CYEDGE)));
	const int iButtonTop = rcClient.bottom - iMargin - iButtonHeight;
	pReload->MoveWindow(rcClient.right - iMargin - iButtonWidth, iButtonTop, iButtonWidth, iButtonHeight);
	pValidate->MoveWindow(rcClient.right - iMargin - (iButtonWidth * 2) - iGap, iButtonTop, iButtonWidth, iButtonHeight);
	rcDefinitionsFrame.right = rcClient.right - iMargin;
	rcDefinitionsFrame.bottom = iButtonTop - iGap;
	rcDefinitionsText.right = rcDefinitionsFrame.right - iTextRightMargin;
	rcDefinitionsText.bottom = rcDefinitionsFrame.bottom - CPreferencesDlg::ScaleOptionsValue(max(1, ::GetSystemMetrics(SM_CYEDGE) * 2));
	if (rcDefinitionsFrame.bottom > rcDefinitionsFrame.top)
		pDefinitionsFrame->MoveWindow(&rcDefinitionsFrame);
	if (rcDefinitionsText.bottom > rcDefinitionsText.top)
		pDefinitionsText->MoveWindow(&rcDefinitionsText);

	const int iContentTop = rcTabs.bottom + CPreferencesDlg::ScaleOptionsValue(max(1, ::GetSystemMetrics(SM_CYEDGE)));
	pHelpText->MoveWindow(iMargin, iContentTop, max(1, rcClient.Width() - (iMargin * 2)), max(1, rcClient.bottom - iMargin - iContentTop));
}

void CPPgBlacklistPanel::UpdateOptionsLayout()
{
	CWnd* pOptionsFrame = GetDlgItem(IDC_BLACKLIST_OPT_FRM);
	CWnd* pAutomatic = GetDlgItem(IDC_BLACKLIST_ENABLE_AUTOMATIC_CHECKBOX);
	CWnd* pManual = GetDlgItem(IDC_BLACKLIST_ENABLE_MANUAL_CHECKBOX);
	CWnd* pLog = GetDlgItem(IDC_BLACKLIST_LOG_CHECKBOX);
	CWnd* pAutoRemove = GetDlgItem(IDC_BLACKLIST_AUTOREMOVE_CHECKBOX);
	CWnd* pDefinitionsFrame = GetDlgItem(IDC_BLACKLIST_DEF_FRM);
	CWnd* pDefinitionsText = GetDlgItem(IDC_BLACKLIST_DEFINITIONS_TEXTBOX);
	if (pOptionsFrame == NULL || pAutomatic == NULL || pManual == NULL || pLog == NULL || pAutoRemove == NULL || pDefinitionsFrame == NULL || pDefinitionsText == NULL)
		return;

	CRect rcOptionsFrame;
	CRect rcAutomatic;
	CRect rcManual;
	CRect rcLog;
	CRect rcAutoRemove;
	CRect rcDefinitionsFrame;
	CRect rcDefinitionsText;
	pOptionsFrame->GetWindowRect(&rcOptionsFrame);
	pAutomatic->GetWindowRect(&rcAutomatic);
	pManual->GetWindowRect(&rcManual);
	pLog->GetWindowRect(&rcLog);
	pAutoRemove->GetWindowRect(&rcAutoRemove);
	pDefinitionsFrame->GetWindowRect(&rcDefinitionsFrame);
	pDefinitionsText->GetWindowRect(&rcDefinitionsText);
	ScreenToClient(&rcOptionsFrame);
	ScreenToClient(&rcAutomatic);
	ScreenToClient(&rcManual);
	ScreenToClient(&rcLog);
	ScreenToClient(&rcAutoRemove);
	ScreenToClient(&rcDefinitionsFrame);
	ScreenToClient(&rcDefinitionsText);

	const int iRowGap = CPreferencesDlg::ScaleOptionsValue(max(1, ::GetSystemMetrics(SM_CYEDGE)));
	const int iFrameBottomMargin = CPreferencesDlg::ScaleOptionsValue(max(4, ::GetSystemMetrics(SM_CYEDGE) * 2));
	const int iFrameGap = CPreferencesDlg::ScaleOptionsValue(max(1, ::GetSystemMetrics(SM_CYEDGE)));
	const int iFirstRowHeight = max(GetMultilineCheckboxIdealHeight(pAutomatic), GetMultilineCheckboxIdealHeight(pManual));
	const int iSecondRowHeight = max(GetMultilineCheckboxIdealHeight(pLog), GetMultilineCheckboxIdealHeight(pAutoRemove));
	const int iSecondRowTop = rcAutomatic.top + iFirstRowHeight + iRowGap;

	pAutomatic->SetWindowPos(NULL, rcAutomatic.left, rcAutomatic.top, rcAutomatic.Width(), iFirstRowHeight, SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
	pManual->SetWindowPos(NULL, rcManual.left, rcAutomatic.top, rcManual.Width(), iFirstRowHeight, SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
	pLog->SetWindowPos(NULL, rcLog.left, iSecondRowTop, rcLog.Width(), iSecondRowHeight, SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
	pAutoRemove->SetWindowPos(NULL, rcAutoRemove.left, iSecondRowTop, rcAutoRemove.Width(), iSecondRowHeight, SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);

	const int iOptionsFrameBottom = iSecondRowTop + iSecondRowHeight + iFrameBottomMargin;
	rcOptionsFrame.bottom = iOptionsFrameBottom;
	pOptionsFrame->MoveWindow(&rcOptionsFrame);

	const int iDefinitionsTextTopOffset = rcDefinitionsText.top - rcDefinitionsFrame.top;
	rcDefinitionsFrame.top = iOptionsFrameBottom + iFrameGap;
	pDefinitionsFrame->MoveWindow(&rcDefinitionsFrame);
	rcDefinitionsText.top = rcDefinitionsFrame.top + iDefinitionsTextTopOffset;
	pDefinitionsText->MoveWindow(&rcDefinitionsText);
}

void CPPgBlacklistPanel::OnSize(UINT nType, int cx, int cy)
{
	CPropertyPage::OnSize(nType, cx, cy);
	UpdateLayout();
}

BOOL CPPgBlacklistPanel::OnKillActive()
{
	// if prop page is closed by pressing VK_ENTER we have to explicitly commit any possibly pending
	// data from an open edit control
	return CPropertyPage::OnKillActive();
}


void CPPgBlacklistPanel::Localize(void)
{
	if(m_hWnd)
	{
		SetDlgItemText(IDC_BLACKLIST_ENABLE_AUTOMATIC_CHECKBOX, GetResString(_T("AUTOMATIC_BLACKLIST")));
		SetDlgItemText(IDC_BLACKLIST_ENABLE_MANUAL_CHECKBOX, GetResString(_T("MANUAL_BLACKLIST")));
		SetDlgItemText(IDC_BLACKLIST_AUTOREMOVE_CHECKBOX, GetResString(_T("BLACKLIST_AUTOREMOVE")));
		SetDlgItemText(IDC_BLACKLIST_LOG_CHECKBOX, GetResString(_T("BLACKLIST_LOG")));
		SetDlgItemText(IDC_BLACKLIST_DEF_FRM, GetResString(_T("BLACKLIST_DEFINITIONS")));
		SetDlgItemText(IDC_BLACKLIST_PANEL_HELP_TEXTBOX, GetResString(_T("BLACKLIST_DEFINITIONS_INFO")));
		SetDlgItemText(IDC_BLACKLIST_VALIDATE, GetResString(_T("DOWNLOAD_VALIDATOR_REGEX_VALIDATE")));
		SetDlgItemText(IDC_BLACKLIST_RELOAD, GetResString(_T("SF_RELOAD")));
		UpdateViewTabs();
		UpdateToolTips();
		UpdateLayout();
	}
}

BOOL CPPgBlacklistPanel::PreTranslateMessage(MSG* pMsg)
{
	if (theApp.emuledlg->m_pSplashWnd)
		return FALSE;
	if (m_tooltipControls.GetSafeHwnd() != NULL) {
		const bool bShowToolTips = AreOptionsToolTipsEnabled(this) && m_eViewMode == ViewSettings;
		m_tooltipControls.Activate(bShowToolTips);
		if (bShowToolTips)
			m_tooltipControls.RelayEvent(pMsg);
	}
	return CPropertyPage::PreTranslateMessage(pMsg);
}

void CPPgBlacklistPanel::OnDestroy()
{
	if (m_tooltipControls.GetSafeHwnd() != NULL) {
		const UINT aControlIds[] = { IDC_BLACKLIST_ENABLE_AUTOMATIC_CHECKBOX, IDC_BLACKLIST_ENABLE_MANUAL_CHECKBOX, IDC_BLACKLIST_AUTOREMOVE_CHECKBOX, IDC_BLACKLIST_LOG_CHECKBOX };
		for (size_t i = 0; i < _countof(aControlIds); ++i) {
			CWnd* pWnd = GetDlgItem(aControlIds[i]);
			if (pWnd != NULL)
				m_tooltipControls.DelTool(pWnd);
		}
		m_tooltipControls.CleanupWindow();
	}
	CPropertyPage::OnDestroy();
}

void CPPgBlacklistPanel::OnHelp()
{
	SwitchView(ViewHelp);
}

BOOL CPPgBlacklistPanel::OnCommand(WPARAM wParam, LPARAM lParam)
{
	if (wParam == ID_HELP)
	{
		OnHelp();
		return TRUE;
	}
	return __super::OnCommand(wParam, lParam);
}

BOOL CPPgBlacklistPanel::OnHelpInfo(HELPINFO* /*pHelpInfo*/)
{
	OnHelp();
	return TRUE;
}

void CPPgBlacklistPanel::OnSysColorChange()
{
	CPropertyPage::OnSysColorChange();
	UpdateBlacklistDefinitionsTextBoxColors();
}

void CPPgBlacklistPanel::InitializeViewTabs()
{
	if (m_ctrlViewTabs.GetItemCount() == 0) {
		m_ctrlViewTabs.InsertItem(0, _T(""));
		m_ctrlViewTabs.InsertItem(1, _T(""));
	}
	m_ctrlViewTabs.SetCurSel(static_cast<int>(m_eViewMode));
	UpdateViewTabs();
}

void CPPgBlacklistPanel::UpdateViewTabs()
{
	if (m_ctrlViewTabs.GetSafeHwnd() == NULL)
		return;
	TCITEM item = {};
	item.mask = TCIF_TEXT;
	CString strText(GetResString(_T("DOWNLOAD_VALIDATOR_VIEW_SETTINGS")));
	item.pszText = const_cast<LPTSTR>((LPCTSTR)strText);
	m_ctrlViewTabs.SetItem(0, &item);
	strText = GetResString(_T("EM_HELP"));
	item.pszText = const_cast<LPTSTR>((LPCTSTR)strText);
	m_ctrlViewTabs.SetItem(1, &item);
	m_ctrlViewTabs.SetCurSel(static_cast<int>(m_eViewMode));
}

void CPPgBlacklistPanel::SwitchView(EViewMode eViewMode)
{
	m_eViewMode = eViewMode;
	UpdateViewTabs();
	const bool bSettings = eViewMode == ViewSettings;
	const UINT aSettingsControls[] = {
		IDC_BLACKLIST_OPT_FRM, IDC_BLACKLIST_ENABLE_AUTOMATIC_CHECKBOX, IDC_BLACKLIST_ENABLE_MANUAL_CHECKBOX,
		IDC_BLACKLIST_LOG_CHECKBOX, IDC_BLACKLIST_AUTOREMOVE_CHECKBOX, IDC_BLACKLIST_DEF_FRM, IDC_BLACKLIST_DEFINITIONS_TEXTBOX,
		IDC_BLACKLIST_VALIDATE, IDC_BLACKLIST_RELOAD
	};
	for (size_t i = 0; i < _countof(aSettingsControls); ++i) {
		CWnd* pWnd = GetDlgItem(aSettingsControls[i]);
		if (pWnd != NULL)
			pWnd->ShowWindow(bSettings ? SW_SHOW : SW_HIDE);
	}
	CWnd* pHelpText = GetDlgItem(IDC_BLACKLIST_PANEL_HELP_TEXTBOX);
	if (pHelpText != NULL)
		pHelpText->ShowWindow(bSettings ? SW_HIDE : SW_SHOW);
	if (m_tooltipControls.GetSafeHwnd() != NULL)
		m_tooltipControls.Activate(AreOptionsToolTipsEnabled(this) && bSettings);
	UpdateLayout();
}

void CPPgBlacklistPanel::UpdateToolTips()
{
	struct SToolTipEntry
	{
		UINT uControlId;
		LPCTSTR pszResourceKey;
	};
	static const SToolTipEntry aEntries[] = {
		{ IDC_BLACKLIST_ENABLE_AUTOMATIC_CHECKBOX, _T("AUTOMATIC_BLACKLIST_INFO") },
		{ IDC_BLACKLIST_ENABLE_MANUAL_CHECKBOX, _T("MANUAL_BLACKLIST_INFO") },
		{ IDC_BLACKLIST_AUTOREMOVE_CHECKBOX, _T("BLACKLIST_AUTOREMOVE") },
		{ IDC_BLACKLIST_LOG_CHECKBOX, _T("BLACKLIST_LOG_INFO") }
	};

	if (m_tooltipControls.GetSafeHwnd() == NULL) {
		if (!m_tooltipControls.Create(this))
			return;
		m_tooltipControls.SetMaxTipWidth(CPreferencesDlg::ScaleOptionsValue(420));
		m_tooltipControls.SetAutoTabHeaderIcon(false);
		for (size_t i = 0; i < _countof(aEntries); ++i) {
			CWnd* pWnd = GetDlgItem(aEntries[i].uControlId);
			if (pWnd != NULL)
				m_tooltipControls.AddTool(pWnd, _T(""));
		}
	}

	for (size_t i = 0; i < _countof(aEntries); ++i) {
		CWnd* pWnd = GetDlgItem(aEntries[i].uControlId);
		if (pWnd != NULL)
			m_tooltipControls.UpdateTipText(FormatToolTipText(GetResString(aEntries[i].pszResourceKey)), pWnd);
	}
	m_tooltipControls.Activate(AreOptionsToolTipsEnabled(this) && m_eViewMode == ViewSettings);
}

void CPPgBlacklistPanel::OnSettingsChange()
{
	m_bSettingsDirty = true;
	SetModified();
}

void CPPgBlacklistPanel::OnDefinitionsChanged()
{
	if (m_bUpdatingDefinitions)
		return;
	m_bDefinitionsDirty = true;
	SetModified();
}

bool CPPgBlacklistPanel::ValidateDefinitions(UINT& uErrorLine, UINT& uRuleCount, bool bShowMessage)
{
	CString strDefinitions;
	GetDlgItemText(IDC_BLACKLIST_DEFINITIONS_TEXTBOX, strDefinitions);
	const bool bValid = thePrefs.ValidateBlacklistDefinitions(strDefinitions, uErrorLine, uRuleCount);
	if (bShowMessage) {
		CString strMessage;
		if (bValid)
			strMessage.Format(GetResString(_T("BLACKLIST_VALIDATION_SUCCESS")), uRuleCount);
		else
			strMessage.Format(GetResString(_T("BLACKLIST_VALIDATION_ERROR")), uErrorLine);
		CDarkMode::MessageBox(strMessage, bValid ? MB_ICONINFORMATION : MB_ICONWARNING);
	}
	return bValid;
}

void CPPgBlacklistPanel::SelectDefinitionErrorLine(UINT uLineNumber)
{
	if (uLineNumber == 0)
		return;
	CWnd* pEdit = GetDlgItem(IDC_BLACKLIST_DEFINITIONS_TEXTBOX);
	if (pEdit == NULL)
		return;
	const LRESULT iLineIndex = pEdit->SendMessage(EM_LINEINDEX, static_cast<WPARAM>(uLineNumber - 1), 0);
	if (iLineIndex < 0)
		return;
	const LRESULT iLineLength = pEdit->SendMessage(EM_LINELENGTH, static_cast<WPARAM>(iLineIndex), 0);
	pEdit->SendMessage(EM_SETSEL, static_cast<WPARAM>(iLineIndex), static_cast<LPARAM>(iLineIndex + (iLineLength > 0 ? iLineLength : 0)));
	pEdit->SetFocus();
}

void CPPgBlacklistPanel::OnValidateDefinitions()
{
	UINT uErrorLine = 0;
	UINT uRuleCount = 0;
	if (!ValidateDefinitions(uErrorLine, uRuleCount, true))
		SelectDefinitionErrorLine(uErrorLine);
}

void CPPgBlacklistPanel::OnReloadDefinitions()
{
	if (m_bDefinitionsDirty && CDarkMode::MessageBox(GetResString(_T("BLACKLIST_RELOAD_CONFIRM")), MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;

	CString strDefinitions;
	UINT uErrorLine = 0;
	UINT uRuleCount = 0;
	if (!thePrefs.LoadBlacklistDefinitionsText(strDefinitions, uErrorLine, uRuleCount)) {
		CString strMessage;
		if (uErrorLine != 0)
			strMessage.Format(GetResString(_T("BLACKLIST_VALIDATION_ERROR")), uErrorLine);
		else
			strMessage = GetResString(_T("BLACKLIST_RELOAD_FAILED"));
		CDarkMode::MessageBox(strMessage, MB_ICONWARNING);
		return;
	}
	if (!thePrefs.ApplyBlacklistDefinitions(strDefinitions, uErrorLine, uRuleCount)) {
		SelectDefinitionErrorLine(uErrorLine);
		return;
	}
	m_bUpdatingDefinitions = true;
	SetDlgItemText(IDC_BLACKLIST_DEFINITIONS_TEXTBOX, strDefinitions);
	m_bUpdatingDefinitions = false;
	m_bDefinitionsDirty = false;
	if (theApp.emuledlg != NULL && theApp.emuledlg->searchwnd != NULL && theApp.emuledlg->searchwnd->m_pwndResults != NULL)
		theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.QueueDeferredReload(false, LSF_SELECTION, 50);
	SetModified(m_bSettingsDirty ? TRUE : FALSE);
	CString strMessage;
	strMessage.Format(GetResString(_T("BLACKLIST_VALIDATION_SUCCESS")), uRuleCount);
	CDarkMode::MessageBox(strMessage, MB_ICONINFORMATION);
}

void CPPgBlacklistPanel::OnTcnSelchangeViewTabs(NMHDR*, LRESULT* pResult)
{
	if (pResult != NULL)
		*pResult = 0;
	SwitchView(m_ctrlViewTabs.GetCurSel() == 1 ? ViewHelp : ViewSettings);
}
