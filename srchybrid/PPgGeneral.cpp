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
#include "SearchDlg.h"
#include "PreferencesDlg.h"
#include "ppggeneral.h"
#include "HttpDownloadDlg.h"
#include "Preferences.h"
#include "emuledlg.h"
#include "StatisticsDlg.h"
#include "ServerWnd.h"
#include "TransferDlg.h"
#include "ChatWnd.h"
#include "SharedFilesWnd.h"
#include "KademliaWnd.h"
#include "IrcWnd.h"
#include "WebServices.h"
#include "HelpIDs.h"
#include "StringConversion.h"
#include "Log.h"
#include "eMuleAI/Shield.h"
#include "eMuleAI/DarkMode.h"
#include "UserMsgs.h"
#include "translations/lang_registry.gen.h"

#if !defined(LOCALE_SLOCALIZEDDISPLAYNAME)
#define LOCALE_SLOCALIZEDDISPLAYNAME 0x00000002
#endif
#if !defined(LOCALE_SNATIVELANGNAME)
#define LOCALE_SNATIVELANGNAME 0x00000004
#endif

static void GetLocaleNamesCompat(LPCTSTR code, CString &localized, CString &native)
{
	TCHAR loc[128] = { 0 };
	TCHAR nat[128] = { 0 };
#if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0600
	int lenLoc = GetLocaleInfoEx(code, LOCALE_SLOCALIZEDDISPLAYNAME, loc, _countof(loc));
	int lenNat = GetLocaleInfoEx(code, LOCALE_SNATIVELANGNAME, nat, _countof(nat));
#else
	typedef int (WINAPI *PFNGetLocaleInfoEx)(LPCWSTR, LCTYPE, LPWSTR, int);
	HMODULE hKernel = ::GetModuleHandleW(L"kernel32.dll");
	PFNGetLocaleInfoEx pGetLocaleInfoEx = hKernel ? (PFNGetLocaleInfoEx)::GetProcAddress(hKernel, "GetLocaleInfoEx") : NULL;
	int lenLoc = 0, lenNat = 0;
	if (pGetLocaleInfoEx) {
		lenLoc = pGetLocaleInfoEx(code, LOCALE_SLOCALIZEDDISPLAYNAME, loc, _countof(loc));
		lenNat = pGetLocaleInfoEx(code, LOCALE_SNATIVELANGNAME, nat, _countof(nat));
	}
#endif
	localized = (lenLoc > 0) ? loc : CString(code);
	native = (lenNat > 0) ? nat : localized;

	if (_tcsicmp(code, _T("hmw")) == 0) { localized = _T("Hmong Daw"); native = _T("Hmong Daw"); }
	else if (_tcsicmp(code, _T("ht")) == 0) { localized = _T("Haitian Creole"); native = _T("Kreyòl ayisyen"); }
	else if (_tcsicmp(code, _T("iw")) == 0) { localized = _T("Hebrew"); native = _T("עברית"); }
	else if (_tcsicmp(code, _T("jw")) == 0) { localized = _T("Javanese"); native = _T("Basa Jawa"); }
	else if (_tcsicmp(code, _T("ceb")) == 0) { localized = _T("Cebuano"); native = _T("Cebuano"); }
	else if (_tcsicmp(code, _T("ny")) == 0) { localized = _T("Nyanja"); native = _T("Chi-Chewa"); }
	else if (_tcsicmp(code, _T("sm")) == 0) { localized = _T("Samoan"); native = _T("Gagana fa'a Sāmoa"); }
	else if (_tcsicmp(code, _T("st")) == 0) { localized = _T("Sesotho"); native = _T("Sesotho"); }
	else if (_tcsicmp(code, _T("su")) == 0) { localized = _T("Sundanese"); native = _T("Basa Sunda"); }
	else if (_tcsicmp(code, _T("tl")) == 0) { localized = _T("Tagalog"); native = _T("Tagalog"); }
	else if (_tcsicmp(code, _T("ca-VAL")) == 0) { localized = _T("Valencian"); native = _T("Valencià"); }
	else {
		CString strCodeParen;
		strCodeParen.Format(_T("(%s)"), code);
		if (localized.Find(strCodeParen) != -1) localized = code;
		if (native.Find(strCodeParen) != -1) native = code;
	}

	if (localized.IsEmpty() || localized == code) {
		localized = native.IsEmpty() ? code : native;
	}
	if (native.IsEmpty()) {
		native = localized;
	}

	if (!native.IsEmpty()) {
		CString firstChar = native.Left(1);
		firstChar.MakeUpper();
		native = firstChar + native.Mid(1);
	}
}

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


IMPLEMENT_DYNAMIC(CPPgGeneral, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgGeneral, CPropertyPage)
	ON_BN_CLICKED(IDC_STARTMIN, OnSettingsChange)
	ON_BN_CLICKED(IDC_STARTWIN, OnSettingsChange)
	ON_EN_CHANGE(IDC_NICK, OnSettingsChange)
	ON_BN_CLICKED(IDC_EXIT, OnSettingsChange)
	ON_BN_CLICKED(IDC_SPLASHON, OnSettingsChange)
	ON_BN_CLICKED(IDC_BRINGTOFOREGROUND, OnSettingsChange)
	ON_CBN_SELCHANGE(IDC_LANGS, OnLangChange)
	ON_BN_CLICKED(IDC_ED2KFIX, OnBnClickedEd2kfix)
	ON_BN_CLICKED(IDC_WEBSVEDIT, OnBnClickedEditWebservices)
	ON_BN_CLICKED(IDC_ONLINESIG, OnSettingsChange)
	ON_WM_HSCROLL()
	ON_BN_CLICKED(IDC_PREVENTSTANDBY, OnSettingsChange)
	ON_WM_HSCROLL()
	ON_WM_HELPINFO()
END_MESSAGE_MAP()

void CPPgGeneral::SetLangSel()
{
	CString cur = thePrefs.GetUiLanguage();
	if (cur.IsEmpty()) cur = _T("system");
	for (int i = 0; i < m_language.GetCount(); ++i) {
		if (i < m_langCodes.GetSize() && cur.CompareNoCase(m_langCodes[i]) == 0) {
			m_language.SetCurSel(i);
			return;
		}
	}
	m_language.SetCurSel(0);
}

CPPgGeneral::CPPgGeneral()
	: CPropertyPage(CPPgGeneral::IDD)
{
}

void CPPgGeneral::DoDataExchange(CDataExchange *pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LANGS, m_language);
}

void CPPgGeneral::LoadSettings()
{
	SetLangSel();
	SetDlgItemText(IDC_NICK, thePrefs.GetUserNick());
	CheckDlgButton(IDC_BRINGTOFOREGROUND, static_cast<UINT>(thePrefs.bringtoforeground));
	CheckDlgButton(IDC_EXIT, static_cast<UINT>(thePrefs.confirmExit));
	CheckDlgButton(IDC_ONLINESIG, static_cast<UINT>(thePrefs.onlineSig));
	CheckDlgButton(IDC_MINIMULE, static_cast<UINT>(thePrefs.m_bEnableMiniMule));
	CheckDlgButton(IDC_SPLASHON, static_cast<UINT>(thePrefs.splashscreen));
	CheckDlgButton(IDC_STARTMIN, static_cast<UINT>(thePrefs.startMinimized));
	CheckDlgButton(IDC_STARTWIN, static_cast<UINT>(thePrefs.m_bAutoStart));

	if (thePrefs.GetWindowsVersion() != _WINVER_95_)
		CheckDlgButton(IDC_PREVENTSTANDBY, static_cast<UINT>(thePrefs.GetPreventStandby()));
	else {
		CheckDlgButton(IDC_PREVENTSTANDBY, 0);
		GetDlgItem(IDC_PREVENTSTANDBY)->EnableWindow(FALSE);
	}

}

BOOL CPPgGeneral::OnInitDialog()
{
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);

	static_cast<CEdit*>(GetDlgItem(IDC_NICK))->SetLimitText(thePrefs.GetMaxUserNickLength());

	m_language.ResetContent();
	m_langCodes.RemoveAll();

	struct LangItem { 
		CString disp; 
		CString code; 
	};
	std::vector<LangItem> items;

	for (uint16_t i = 0; i < Translations::kLanguageCount; ++i) {
		LPCTSTR code = Translations::kLanguages[i].code ? Translations::kLanguages[i].code : _T("");
		if (code[0] == _T('\0')) continue;
		CString cLoc, cNat;
		GetLocaleNamesCompat(code, cLoc, cNat);
		CString disp;
		disp.Format(_T("%s (%s)"), (LPCTSTR)cLoc, (LPCTSTR)cNat);
		LangItem li;
		li.disp = disp;
		li.code = code;
		items.push_back(li);
	}

	std::sort(items.begin(), items.end(), [](const LangItem& a, const LangItem& b) {
		return a.disp.CompareNoCase(b.disp) < 0;
	});

	int idx = m_language.AddString(_T("System (default)"));
	m_langCodes.Add(_T("system"));
	(void)idx;

	for (size_t i = 0; i < items.size(); ++i) {
		int pos = m_language.AddString(items[i].disp);
		m_langCodes.Add(items[i].code);
		(void)pos;
	}

	UpdateEd2kLinkFixCtrl();

	LoadSettings();
	Localize();

	return TRUE;  // return TRUE unless you set the focus to the control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

void ModifyAllWindowStyles(CWnd *pWnd, DWORD dwRemove, DWORD dwAdd)
{
	CWnd *pWndChild = pWnd->GetWindow(GW_CHILD);
	while (pWndChild) {
		ModifyAllWindowStyles(pWndChild, dwRemove, dwAdd);
		pWndChild = pWndChild->GetNextWindow();
	}

	if (pWnd->ModifyStyleEx(dwRemove, dwAdd, SWP_FRAMECHANGED)) {
		pWnd->Invalidate();
	}
}

BOOL CPPgGeneral::OnApply()
{
	CString strNick;
	GetDlgItemText(IDC_NICK, strNick);
	if (!IsValidEd2kString(strNick.Trim()) || strNick.IsEmpty()) {
		strNick = DEFAULT_NICK;
		SetDlgItemText(IDC_NICK, strNick);
	}

	if (!theApp.shield->IsValidUserName(strNick)) {
		CDarkMode::MessageBox(GetResString(_T("INVALID_USER_NAME")));
		strNick = DEFAULT_NICK; // changed to default nick
		GetDlgItem(IDC_NICK)->SetWindowText(strNick);
	}

	thePrefs.SetUserNick(strNick);

	if (m_language.GetCurSel() != CB_ERR) {
		int sel = m_language.GetCurSel();
		if (sel >= 0 && sel < m_langCodes.GetSize()) {
			CString chosen = m_langCodes[sel];
			if (chosen.IsEmpty()) chosen = _T("system");
			if (chosen.CompareNoCase(thePrefs.GetUiLanguage()) != 0) {
				thePrefs.SetUiLanguage(chosen);
				thePrefs.SetLanguage();

#ifdef _DEBUG
			// Can't yet be switched on-the-fly, too many unresolved issues.
			if (thePrefs.GetRTLWindowsLayout()) {
				ModifyAllWindowStyles(theApp.emuledlg, WS_EX_LAYOUTRTL | WS_EX_RTLREADING | WS_EX_RIGHT | WS_EX_LEFTSCROLLBAR, 0);
				ModifyAllWindowStyles(theApp.emuledlg->preferenceswnd, WS_EX_LAYOUTRTL | WS_EX_RTLREADING | WS_EX_RIGHT | WS_EX_LEFTSCROLLBAR, 0);
				theApp.DisableRTLWindowsLayout();
				thePrefs.m_bRTLWindowsLayout = false;
			}
#endif
			theApp.emuledlg->preferenceswnd->Localize();
			theApp.emuledlg->statisticswnd->CreateMyTree();
			theApp.emuledlg->statisticswnd->Localize();
			theApp.emuledlg->statisticswnd->ShowStatistics(true);
			theApp.emuledlg->serverwnd->Localize();
			theApp.emuledlg->transferwnd->Localize();
			theApp.emuledlg->transferwnd->UpdateCatTabTitles();
			theApp.emuledlg->searchwnd->Localize();
			theApp.emuledlg->sharedfileswnd->Localize();
			theApp.emuledlg->chatwnd->Localize();
			theApp.emuledlg->Localize();
			theApp.emuledlg->ircwnd->Localize();
			theApp.emuledlg->kademliawnd->Localize();

			RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_FRAME); // Invalidate and repaint every child window
			SetWindowPos(nullptr, 0, 0, 0, 0, SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED); // Force non-client area (title bar, borders) to be repainted
			}
		}
	}

	thePrefs.bringtoforeground = IsDlgButtonChecked(IDC_BRINGTOFOREGROUND) != 0;
	thePrefs.confirmExit = IsDlgButtonChecked(IDC_EXIT) != 0;
	thePrefs.onlineSig = IsDlgButtonChecked(IDC_ONLINESIG) != 0;
	const bool bEnableMiniMule = IsDlgButtonChecked(IDC_MINIMULE) != 0;
	const bool bCloseMiniMule = thePrefs.m_bEnableMiniMule && !bEnableMiniMule;
	thePrefs.m_bEnableMiniMule = bEnableMiniMule;
	if (bCloseMiniMule && theApp.emuledlg != NULL && ::IsWindow(theApp.emuledlg->GetSafeHwnd())) {
		theApp.emuledlg->SendMessage(UM_CLOSE_MINIMULE);
	}
	thePrefs.m_bPreventStandby = IsDlgButtonChecked(IDC_PREVENTSTANDBY) != 0;
	thePrefs.splashscreen = IsDlgButtonChecked(IDC_SPLASHON) != 0;
	thePrefs.startMinimized = IsDlgButtonChecked(IDC_STARTMIN) != 0;
	thePrefs.m_bAutoStart = IsDlgButtonChecked(IDC_STARTWIN) != 0;
	SetAutoStart(thePrefs.m_bAutoStart);

	LoadSettings();

	SetModified(FALSE);
	return CPropertyPage::OnApply();
}

void CPPgGeneral::UpdateEd2kLinkFixCtrl()
{
	GetDlgItem(IDC_ED2KFIX)->EnableWindow(Ask4RegFix(true, false, true));
}

BOOL CPPgGeneral::OnSetActive()
{
	UpdateEd2kLinkFixCtrl();
	return __super::OnSetActive();
}

void CPPgGeneral::OnBnClickedEd2kfix()
{
	Ask4RegFix(false, false, true);
	GetDlgItem(IDC_ED2KFIX)->EnableWindow(Ask4RegFix(true));
}

void CPPgGeneral::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(_T("PW_GENERAL")));
		SetDlgItemText(IDC_NICK_FRM, GetResString(_T("QL_USERNAME")));
		SetDlgItemText(IDC_LANG_FRM, GetResString(_T("PW_LANG")));
		SetDlgItemText(IDC_MISC_FRM, GetResString(_T("PW_MISC")));
		SetDlgItemText(IDC_BRINGTOFOREGROUND, GetResString(_T("PW_FRONT")));
		SetDlgItemText(IDC_EXIT, GetResString(_T("PW_PROMPT")));
		SetDlgItemText(IDC_ONLINESIG, GetResString(_T("PREF_ONLINESIG")));
		SetDlgItemText(IDC_MINIMULE, GetResString(_T("ENABLEMINIMULE")));
		SetDlgItemText(IDC_PREVENTSTANDBY, GetResString(_T("PREVENTSTANDBY")));
		SetDlgItemText(IDC_WEBSVEDIT, GetResString(_T("WEBSVEDIT")));
		SetDlgItemText(IDC_ED2KFIX, GetResString(_T("ED2KLINKFIX")));
		SetDlgItemText(IDC_STARTUP, GetResString(_T("STARTUP")));
		SetDlgItemText(IDC_SPLASHON, GetResString(_T("PW_SPLASH")));
		SetDlgItemText(IDC_STARTMIN, GetResString(_T("PREF_STARTMIN")));
		SetDlgItemText(IDC_STARTWIN, GetResString(_T("STARTWITHWINDOWS")));
	}
}

void CPPgGeneral::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
{
	SetModified(TRUE);

	UpdateData(FALSE);
	CPropertyPage::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CPPgGeneral::OnBnClickedEditWebservices()
{
	theWebServices.Edit();
}

void CPPgGeneral::OnLangChange()
{
	OnSettingsChange();
}


void CPPgGeneral::OnHelp()
{
	theApp.ShowHelp(eMule_FAQ_Preferences_General);
}

BOOL CPPgGeneral::OnCommand(WPARAM wParam, LPARAM lParam)
{
	return (wParam == ID_HELP) ? OnHelpInfo(NULL) : __super::OnCommand(wParam, lParam);
}

BOOL CPPgGeneral::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}
