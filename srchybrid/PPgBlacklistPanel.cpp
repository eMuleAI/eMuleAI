//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#include "stdafx.h"
#include "emule.h"
#include "MuleStatusbarCtrl.h"
#include "ClientList.h"
#include "PPgBlacklistPanel.h"
#include "emuledlg.h"
#include "UserMsgs.h"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#endif

#define WM_ITEM_HELP			(WM_USER + 0x102 + 1)
static LPCTSTR kHelpKeyProp = _T("BB_HelpKey");

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
	ON_EN_CHANGE(IDC_BLACKLIST_DEFINITIONS_TEXTBOX, OnSettingsChange)
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

CPPgBlacklistPanel::CPPgBlacklistPanel()
	: CPropertyPage(CPPgBlacklistPanel::IDD)
{
}

CPPgBlacklistPanel::~CPPgBlacklistPanel()
{
}

void CPPgBlacklistPanel::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);

}

void CPPgBlacklistPanel::LoadSettings(void)
{
	CheckDlgButton(IDC_BLACKLIST_ENABLE_AUTOMATIC_CHECKBOX, thePrefs.GetBlacklistAutomatic());
	CheckDlgButton(IDC_BLACKLIST_ENABLE_MANUAL_CHECKBOX, thePrefs.GetBlacklistManual());
	CheckDlgButton(IDC_BLACKLIST_AUTOREMOVE_CHECKBOX, thePrefs.GetBlacklistAutoRemoveFromManual());
	CheckDlgButton(IDC_BLACKLIST_LOG_CHECKBOX, thePrefs.GetBlacklistLog());
	thePrefs.LoadBlacklistFile(); // Loads blacklist.conf
}

BOOL CPPgBlacklistPanel::OnApply()
{
	// if prop page is closed by pressing VK_ENTER we have to explicitly commit any possibly pending
	// data from an open edit control

	if (!UpdateData())
		return FALSE;

	thePrefs.SetBlacklistAutomatic(IsDlgButtonChecked(IDC_BLACKLIST_ENABLE_AUTOMATIC_CHECKBOX));
	thePrefs.SetBlacklistManual(IsDlgButtonChecked(IDC_BLACKLIST_ENABLE_MANUAL_CHECKBOX));
	thePrefs.SetBlacklistAutoRemoveFromManual(IsDlgButtonChecked(IDC_BLACKLIST_AUTOREMOVE_CHECKBOX));
	thePrefs.SetBlacklistLog(IsDlgButtonChecked(IDC_BLACKLIST_LOG_CHECKBOX));
	ReadBlacklistDefinitionsFromTextbox();
	thePrefs.SaveBlacklistFile(); // This will show trimmed textbox as it is saved to the file

	SetModified(FALSE);
	return CPropertyPage::OnApply();
}

void CPPgBlacklistPanel::ReadBlacklistDefinitionsFromTextbox() {
	thePrefs.blacklist_list.RemoveAll();
	CString m_strBlacklistDefinitions = NULL;
	int m_iNewlinePosition = 0;
	GetDlgItemText(IDC_BLACKLIST_DEFINITIONS_TEXTBOX, m_strBlacklistDefinitions);
	CString m_strBlacklistLine = m_strBlacklistDefinitions.Tokenize(_T("\r\n"), m_iNewlinePosition);
	while (m_iNewlinePosition != -1) {
		m_strBlacklistLine.Trim(_T("\t\r\n")); // Trim '\r' in binary mode.
		if (m_strBlacklistLine[0] != _T('#') && m_strBlacklistLine[0] != _T('\\')) // If this is not a comment or regex definition line, convert it to lower case for case insensitive comparisons.
			m_strBlacklistLine.MakeLower();
		if (!m_strBlacklistLine.IsEmpty())
			thePrefs.blacklist_list.AddTail(m_strBlacklistLine);
		m_strBlacklistLine = m_strBlacklistDefinitions.Tokenize(_T("\r\n"), m_iNewlinePosition);
	}
}

BOOL CPPgBlacklistPanel::OnInitDialog()
{
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);
	LoadSettings();
	Localize();

	return TRUE;  // return TRUE unless you set the focus to the control. EXCEPTION: OCX Property Pages should return FALSE
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
		SetDlgItemText(IDC_BLACKLIST_OPT_FRM, GetResString(_T("BLACKLIST_OPTIONS")));
		SetDlgItemText(IDC_BLACKLIST_DEF_FRM, GetResString(_T("BLACKLIST_DEFINITIONS")));
	}
}

BOOL CPPgBlacklistPanel::PreTranslateMessage(MSG* pMsg)
{
	if (theApp.emuledlg->m_pSplashWnd)
		return FALSE;

	CWnd* CtrlID = GetDlgItem((GetWindowLong(pMsg->hwnd, GWL_ID)));
	if (CtrlID == NULL)
		return CPropertyPage::PreTranslateMessage(pMsg);

	switch (pMsg->message)
	{
	case  WM_MOUSEMOVE: {
		// Set help key as a window property when we start tracking
		if (::GetProp(CtrlID->m_hWnd, kHelpKeyProp) == NULL) {
			if (pMsg->hwnd == GetDlgItem(IDC_BLACKLIST_ENABLE_AUTOMATIC_CHECKBOX)->m_hWnd)
				::SetProp(CtrlID->m_hWnd, kHelpKeyProp, (HANDLE)_T("AUTOMATIC_BLACKLIST_INFO"));
			else if (pMsg->hwnd == GetDlgItem(IDC_BLACKLIST_ENABLE_MANUAL_CHECKBOX)->m_hWnd)
				::SetProp(CtrlID->m_hWnd, kHelpKeyProp, (HANDLE)_T("MANUAL_BLACKLIST_INFO"));
			else if (pMsg->hwnd == GetDlgItem(IDC_BLACKLIST_AUTOREMOVE_CHECKBOX)->m_hWnd)
				::SetProp(CtrlID->m_hWnd, kHelpKeyProp, (HANDLE)_T("BLACKLIST_AUTOREMOVE_INFO"));
			else if (pMsg->hwnd == GetDlgItem(IDC_BLACKLIST_LOG_CHECKBOX)->m_hWnd)
				::SetProp(CtrlID->m_hWnd, kHelpKeyProp, (HANDLE)_T("BLACKLIST_LOG_INFO"));
			else if (pMsg->hwnd == GetDlgItem(IDC_BLACKLIST_DEFINITIONS_TEXTBOX)->m_hWnd)
				::SetProp(CtrlID->m_hWnd, kHelpKeyProp, (HANDLE)_T("BLACKLIST_DEFINITIONS_INFO"));
			else
				break;
		}
		else
			break;
		TRACKMOUSEEVENT tme = {};
		tme.cbSize = sizeof(TRACKMOUSEEVENT);
		tme.dwFlags = TME_HOVER | TME_LEAVE;
		tme.hwndTrack = pMsg->hwnd;
		tme.dwHoverTime = HOVER_DEFAULT;
		TrackMouseEvent(&tme);
		break;
	}
	case WM_MOUSEHOVER: {
		LPCTSTR key = (LPCTSTR)::GetProp(CtrlID->m_hWnd, kHelpKeyProp);
		if (key && key[0] != _T('\0'))
			SetDlgItemText(IDC_BLACKLIST_PANEL_HELP_TEXTBOX, GetResString(key));
		break;
	}
	case WM_MOUSELEAVE:
		::RemoveProp(CtrlID->m_hWnd, kHelpKeyProp);
		break;
	}

	return CPropertyPage::PreTranslateMessage(pMsg);
}

void CPPgBlacklistPanel::OnDestroy()
{
	CPropertyPage::OnDestroy();
}

void CPPgBlacklistPanel::OnHelp()
{
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

HBRUSH CPPgBlacklistPanel::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	switch (nCtlColor)
	{
	case CTLCOLOR_STATIC:
		if (pWnd->GetSafeHwnd() == GetDlgItem(IDC_BLACKLIST_PANEL_HELP_TEXTBOX)->GetSafeHwnd()) {
			pDC->SetTextColor(GetCustomSysColor(COLOR_WINDOWTEXT));	// Text font colour
			pDC->SetBkColor(RGB(230, 230, 230)); // Text background colour
			return CDarkMode::s_brHelpTextBackground;
		}
	default:
		return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
	}
}