#include "stdafx.h"
#include "resource.h"
#include <vector>
#include <algorithm>
#include "emule.h"
#include "LanguageSelectDlg.h"
#include "translations/lang_registry.gen.h"
#include "Preferences.h"
#include "Ini2.h"
#include "eMuleAI/DarkMode.h"

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
#endif

namespace
{
	enum
	{
		kDialogSideIconSizePx = 96,
		kLanguageDialogContentGapPx = 24,
		kLanguageDialogComboPaddingPx = 20,
			kLanguageDialogMinComboWidthPx = 220
	};

	HICON LoadDialogSideIcon()
	{
		return (HICON)::LoadImage(AfxGetResourceHandle(), _T("EMULEAI_SIDE"), IMAGE_ICON, kDialogSideIconSizePx, kDialogSideIconSizePx, LR_DEFAULTCOLOR | LR_SHARED);
	}

	int MeasureLongestComboTextWidth(CComboBox &combo)
	{
		CClientDC dc(&combo);
		CFont *pFont = combo.GetFont();
		CFont *pOldFont = (pFont != NULL) ? dc.SelectObject(pFont) : NULL;
		int maxWidth = 0;
		const int itemCount = combo.GetCount();
		for (int i = 0; i < itemCount; ++i) {
			CString text;
			combo.GetLBText(i, text);
			maxWidth = max(maxWidth, dc.GetTextExtent(text).cx);
		}
		if (pOldFont != NULL)
			dc.SelectObject(pOldFont);
		return maxWidth;
	}

	void ResizeDialogToContent(CDialog *pDlg, CComboBox &combo)
	{
		CWnd *pIcon = pDlg->GetDlgItem(IDC_LANGUAGE_ICON);
			CWnd *pLabel = pDlg->GetDlgItem(IDC_LANGUAGE_PROMPT);
		CWnd *pOk = pDlg->GetDlgItem(IDOK);
		CWnd *pCancel = pDlg->GetDlgItem(IDCANCEL);
		if (pIcon == NULL || pLabel == NULL || pOk == NULL || pCancel == NULL)
			return;

		CRect rcClient;
		pDlg->GetClientRect(&rcClient);

		CRect rcIcon, rcLabel, rcCombo, rcOk, rcCancel;
		pIcon->GetWindowRect(&rcIcon);
		pLabel->GetWindowRect(&rcLabel);
		combo.GetWindowRect(&rcCombo);
		pOk->GetWindowRect(&rcOk);
		pCancel->GetWindowRect(&rcCancel);
		pDlg->ScreenToClient(&rcIcon);
		pDlg->ScreenToClient(&rcLabel);
		pDlg->ScreenToClient(&rcCombo);
		pDlg->ScreenToClient(&rcOk);
		pDlg->ScreenToClient(&rcCancel);

		const int leftMargin = rcIcon.left;
		const int rightMargin = rcClient.Width() - rcCombo.right;
		const int buttonGap = rcCancel.left - rcOk.right;
		const int buttonGroupWidth = rcOk.Width() + buttonGap + rcCancel.Width();
		const int contentLeft = rcIcon.right + kLanguageDialogContentGapPx;
		const int comboWidth = max(kLanguageDialogMinComboWidthPx, MeasureLongestComboTextWidth(combo) + GetSystemMetrics(SM_CXVSCROLL) + kLanguageDialogComboPaddingPx);
		const int desiredClientWidth = max(contentLeft + comboWidth + rightMargin, buttonGroupWidth + (leftMargin * 2));

		if (desiredClientWidth != rcClient.Width()) {
			CRect rcWindow;
			pDlg->GetWindowRect(&rcWindow);
			const int nonClientWidth = rcWindow.Width() - rcClient.Width();
			const int nonClientHeight = rcWindow.Height() - rcClient.Height();
			pDlg->SetWindowPos(NULL, 0, 0, desiredClientWidth + nonClientWidth, rcClient.Height() + nonClientHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			pDlg->GetClientRect(&rcClient);
		}

		pLabel->MoveWindow(contentLeft, rcLabel.top, rcClient.Width() - contentLeft - rightMargin, rcLabel.Height());
		combo.MoveWindow(contentLeft, rcCombo.top, rcClient.Width() - contentLeft - rightMargin, rcCombo.Height());
		combo.SetDroppedWidth(rcClient.Width() - contentLeft - rightMargin);

		const int buttonLeft = (rcClient.Width() - buttonGroupWidth) / 2;
		pOk->MoveWindow(buttonLeft, rcOk.top, rcOk.Width(), rcOk.Height());
		pCancel->MoveWindow(buttonLeft + rcOk.Width() + buttonGap, rcCancel.top, rcCancel.Width(), rcCancel.Height());
		pDlg->CenterWindow(CWnd::FromHandle(::GetDesktopWindow()));
	}
}

BEGIN_MESSAGE_MAP(CLanguageSelectDlg, CDialog)
END_MESSAGE_MAP()

CLanguageSelectDlg::CLanguageSelectDlg()
	: CDialog(IDD)
{
}

void CLanguageSelectDlg::DoDataExchange(CDataExchange *pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LANGS, m_language);
}

BOOL CLanguageSelectDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	if (CWnd *pIcon = GetDlgItem(IDC_LANGUAGE_ICON))
		pIcon->SendMessage(STM_SETICON, (WPARAM)LoadDialogSideIcon(), 0);
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

	SetWindowText(GetResString(_T("PW_LANG")));
	SetDlgItemText(IDC_LANGUAGE_PROMPT, GetResString(_T("PW_LANG_CHOOSE")));
	SetDlgItemText(IDOK, GetResString(_T("TREEOPTIONS_OK")));
	SetDlgItemText(IDCANCEL, GetResString(_T("CANCEL")));

	int idx = m_language.AddString(GetResString(_T("PW_LANG_SYSTEM_DEFAULT")));
	m_langCodes.Add(_T("system"));
	(void)idx;
	for (size_t i = 0; i < items.size(); ++i) {
		int pos = m_language.AddString(items[i].disp);
		m_langCodes.Add(items[i].code);
		(void)pos;
	}
	// Preselect system
	m_language.SetCurSel(0);

	GetSystemDarkModeStatus();
	if (thePrefs.m_bDarkModeEnabled) {
		CDarkMode::Initialize();
		ApplyTheme(m_hWnd);
	}
	ResizeDialogToContent(this, m_language);
	return TRUE;
}

void CLanguageSelectDlg::OnOK()
{
	int sel = m_language.GetCurSel();
	if (sel >= 0 && sel < m_langCodes.GetSize()) {
		CString chosen = m_langCodes[sel];
		if (chosen.IsEmpty()) chosen = _T("system");
		thePrefs.SetUiLanguage(chosen);
		thePrefs.SetLanguage();
		// Persist early to avoid re-asking on crash
			CIni ini(thePrefs.GetConfigFile(), _T("eMule"));
			ini.WriteString(_T("Ui.Language"), chosen);
		}
	CDialog::OnOK();
}
