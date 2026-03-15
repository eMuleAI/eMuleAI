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
#include "PPgDisplay.h"
#include <dlgs.h>
#include "HTRichEditCtrl.h"
#include "Preferences.h"
#include "OtherFunctions.h"
#include "emuledlg.h"
#include "TransferDlg.h"
#include "ServerWnd.h"
#include "HelpIDs.h"
#include "opcodes.h"
#include "SharedFilesWnd.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define MAX_TOOLTIP_DELAY_SEC	32


IMPLEMENT_DYNAMIC(CPPgDisplay, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgDisplay, CPropertyPage)
	ON_BN_CLICKED(IDC_MINTRAY, OnSettingsChange)
	ON_BN_CLICKED(IDC_DBLCLICK, OnSettingsChange)
	ON_EN_CHANGE(IDC_TOOLTIPDELAY, OnSettingsChange)
	ON_WM_HSCROLL()
	ON_BN_CLICKED(IDC_SHOWRATEONTITLE, OnSettingsChange)
	ON_BN_CLICKED(IDC_DISABLEHIST, OnSettingsChange)
	ON_BN_CLICKED(IDC_DISABLEKNOWNLIST, OnSettingsChange)
	ON_BN_CLICKED(IDC_DISABLEQUEUELIST, OnSettingsChange)
	ON_BN_CLICKED(IDC_SHOWCATINFO, OnSettingsChange)
	ON_BN_CLICKED(IDC_SHOWDWLPERCENT, OnSettingsChange)
	ON_BN_CLICKED(IDC_SELECT_HYPERTEXT_FONT, OnBnClickedSelectHypertextFont)
	ON_BN_CLICKED(IDC_CLEARCOMPL, OnSettingsChange)
	ON_BN_CLICKED(IDC_SHOWTRANSTOOLBAR, OnSettingsChange)
	ON_BN_CLICKED(IDC_STORESEARCHES, OnSettingsChange)
	ON_BN_CLICKED(IDC_WIN7TASKBARGOODIES, OnSettingsChange)
	ON_BN_CLICKED(IDC_RESETHIST, OnBtnClickedResetHist)
	ON_WM_HELPINFO()
END_MESSAGE_MAP()

CPPgDisplay::CPPgDisplay()
	: CPropertyPage(CPPgDisplay::IDD)
	, m_eSelectFont(sfServer)
{
}

void CPPgDisplay::DoDataExchange(CDataExchange *pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_PREVIEW, m_3DPreview);
}

void CPPgDisplay::LoadSettings()
{
	CheckDlgButton(IDC_MINTRAY, static_cast<UINT>(thePrefs.mintotray));
	CheckDlgButton(IDC_DBLCLICK, static_cast<UINT>(thePrefs.transferDoubleclick));
	CheckDlgButton(IDC_SHOWRATEONTITLE, static_cast<UINT>(thePrefs.showRatesInTitle));
	CheckDlgButton(IDC_DISABLEKNOWNLIST, static_cast<UINT>(thePrefs.m_bDisableKnownClientList));
	CheckDlgButton(IDC_DISABLEQUEUELIST, static_cast<UINT>(thePrefs.m_bDisableQueueList));
	CheckDlgButton(IDC_STORESEARCHES, static_cast<UINT>(thePrefs.IsStoringSearchesEnabled()));
	CheckDlgButton(IDC_SHOWCATINFO, static_cast<UINT>(thePrefs.ShowCatTabInfos()));
	CheckDlgButton(IDC_SHOWDWLPERCENT, static_cast<UINT>(thePrefs.GetUseDwlPercentage()));
	CheckDlgButton(IDC_CLEARCOMPL, static_cast<UINT>(thePrefs.GetRemoveFinishedDownloads()));
	CheckDlgButton(IDC_SHOWTRANSTOOLBAR, static_cast<UINT>(thePrefs.IsTransToolbarEnabled()));
	CheckDlgButton(IDC_DISABLEHIST, static_cast<UINT>(thePrefs.GetUseAutocompletion()));

#ifdef HAVE_WIN7_SDK_H
	if (thePrefs.GetWindowsVersion() >= _WINVER_7_)
		CheckDlgButton(IDC_WIN7TASKBARGOODIES, static_cast<UINT>(thePrefs.IsWin7TaskbarGoodiesEnabled()));
	else
#endif
		GetDlgItem(IDC_WIN7TASKBARGOODIES)->EnableWindow(FALSE);

	SetDlgItemInt(IDC_TOOLTIPDELAY, thePrefs.m_iToolDelayTime, FALSE);
}

BOOL CPPgDisplay::OnInitDialog()
{
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);

	// Barry - Controls depth of 3d colour shading
	CSliderCtrl *slider3D = static_cast<CSliderCtrl*>(GetDlgItem(IDC_3DDEPTH));
	slider3D->SetRange(0, 5, true);
	slider3D->SetPos(thePrefs.Get3DDepth());
	slider3D->SetTicFreq(1);
	DrawPreview();

	CSpinButtonCtrl *pSpinCtrl = static_cast<CSpinButtonCtrl*>(GetDlgItem(IDC_TOOLTIPDELAY_SPIN));
	if (pSpinCtrl)
		pSpinCtrl->SetRange(0, MAX_TOOLTIP_DELAY_SEC);

	LoadSettings();
	Localize();

	return TRUE;  // return TRUE unless you set the focus to the control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

BOOL CPPgDisplay::OnApply()
{
	bool mintotray_old = thePrefs.mintotray;
	thePrefs.mintotray = IsDlgButtonChecked(IDC_MINTRAY) != 0;
	thePrefs.transferDoubleclick = IsDlgButtonChecked(IDC_DBLCLICK) != 0;
	thePrefs.depth3D = static_cast<CSliderCtrl*>(GetDlgItem(IDC_3DDEPTH))->GetPos();
	thePrefs.m_bShowDwlPercentage = IsDlgButtonChecked(IDC_SHOWDWLPERCENT) != 0;
	thePrefs.m_bRemoveFinishedDownloads = IsDlgButtonChecked(IDC_CLEARCOMPL) != 0;
	thePrefs.m_bUseAutocompl = IsDlgButtonChecked(IDC_DISABLEHIST) != 0;
	thePrefs.m_bStoreSearches = IsDlgButtonChecked(IDC_STORESEARCHES) != 0;

#ifdef HAVE_WIN7_SDK_H
	thePrefs.m_bShowWin7TaskbarGoodies = IsDlgButtonChecked(IDC_WIN7TASKBARGOODIES) != 0;
	theApp.emuledlg->EnableTaskbarGoodies(thePrefs.m_bShowWin7TaskbarGoodies);
#endif

	thePrefs.showRatesInTitle = IsDlgButtonChecked(IDC_SHOWRATEONTITLE) != 0;

	thePrefs.ShowCatTabInfos(IsDlgButtonChecked(IDC_SHOWCATINFO) != 0);
	if (!thePrefs.ShowCatTabInfos())
		theApp.emuledlg->transferwnd->UpdateCatTabTitles();

	bool bListDisabled = false;
	bool bResetToolbar = false;
	if (thePrefs.m_bDisableKnownClientList != (IsDlgButtonChecked(IDC_DISABLEKNOWNLIST) != 0)) {
		thePrefs.m_bDisableKnownClientList = IsDlgButtonChecked(IDC_DISABLEKNOWNLIST) != 0;
		if (thePrefs.m_bDisableKnownClientList)
			bListDisabled = true;
		else
			theApp.emuledlg->transferwnd->GetClientList()->ReloadList(false, LSF_SELECTION);
		bResetToolbar = true;
	}

	if (thePrefs.m_bDisableQueueList != (IsDlgButtonChecked(IDC_DISABLEQUEUELIST) != 0)) {
		thePrefs.m_bDisableQueueList = IsDlgButtonChecked(IDC_DISABLEQUEUELIST) != 0;
		if (thePrefs.m_bDisableQueueList)
			bListDisabled = true;
		else
			theApp.emuledlg->transferwnd->GetQueueList()->ShowQueueClients();
		bResetToolbar = true;
	}

	UINT i = GetDlgItemInt(IDC_TOOLTIPDELAY, NULL, FALSE);
	thePrefs.m_iToolDelayTime = (i > MAX_TOOLTIP_DELAY_SEC) ? MAX_TOOLTIP_DELAY_SEC : i;
	theApp.emuledlg->SetToolTipsDelay(SEC2MS(thePrefs.GetToolTipDelay()));

	theApp.emuledlg->transferwnd->GetDownloadList()->SetStyle();

	if (bListDisabled)
		theApp.emuledlg->transferwnd->OnDisableList();
	if ((IsDlgButtonChecked(IDC_SHOWTRANSTOOLBAR) != 0) != thePrefs.IsTransToolbarEnabled()) {
		thePrefs.m_bWinaTransToolbar = !thePrefs.m_bWinaTransToolbar;
		theApp.emuledlg->transferwnd->ResetTransToolbar(thePrefs.m_bWinaTransToolbar);
	} else if (IsDlgButtonChecked(IDC_SHOWTRANSTOOLBAR) && bResetToolbar)
		theApp.emuledlg->transferwnd->ResetTransToolbar(thePrefs.m_bWinaTransToolbar);
	LoadSettings();

	if (mintotray_old != thePrefs.mintotray)
		theApp.emuledlg->TrayMinimizeToTrayChange();
	if (!thePrefs.ShowRatesOnTitle())
		theApp.emuledlg->SetWindowText(theApp.emuledlg->GetMainWindowTitleText());

	SetModified(FALSE);
	return CPropertyPage::OnApply();
}

void CPPgDisplay::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(_T("PW_DISPLAY")));
		SetDlgItemText(IDC_MINTRAY, GetResString(_T("PW_TRAY")));
		SetDlgItemText(IDC_DBLCLICK, GetResString(_T("PW_DBLCLICK")));
		SetDlgItemText(IDC_TOOLTIPDELAY_LBL, GetResString(_T("PW_TOOL")));
		SetDlgItemText(IDC_3DDEP, GetResString(_T("3DDEP")));
		SetDlgItemText(IDC_FLAT, GetResString(_T("FLAT")));
		SetDlgItemText(IDC_ROUND, GetResString(_T("ROUND")));
		SetDlgItemText(IDC_SHOWRATEONTITLE, GetResString(_T("SHOWRATEONTITLE")));
		SetDlgItemText(IDC_DISABLEKNOWNLIST, GetResString(_T("DISABLEKNOWNLIST")));
		SetDlgItemText(IDC_DISABLEQUEUELIST, GetResString(_T("DISABLEQUEUELIST")));
		SetDlgItemText(IDC_STATIC_CPUMEM, GetResString(_T("STATIC_CPUMEM")));
		SetDlgItemText(IDC_SHOWCATINFO, GetResString(_T("SHOWCATINFO")));
		SetDlgItemText(IDC_HYPERTEXT_FONT_HINT, GetResString(_T("HYPERTEXT_FONT_HINT")));
		SetDlgItemText(IDC_SELECT_HYPERTEXT_FONT, GetResString(_T("SELECT_FONT")) + _T("..."));
		SetDlgItemText(IDC_SHOWDWLPERCENT, GetResString(_T("SHOWDWLPERCENTAGE")));
		SetDlgItemText(IDC_CLEARCOMPL, GetResString(_T("AUTOREMOVEFD")));
		SetDlgItemText(IDC_STORESEARCHES, GetResString(_T("STORESEARCHES")));

		SetDlgItemText(IDC_RESETLABEL, GetResString(_T("RESETLABEL")));
		SetDlgItemText(IDC_RESETHIST, GetResString(_T("PW_RESET")));
		SetDlgItemText(IDC_DISABLEHIST, GetResString(_T("ENABLED")));

		SetDlgItemText(IDC_SHOWTRANSTOOLBAR, GetResString(_T("PW_SHOWTRANSTOOLBAR")));
		SetDlgItemText(IDC_WIN7TASKBARGOODIES, GetResString(_T("SHOWWIN7TASKBARGOODIES")));
	}
}

void CPPgDisplay::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
{
	SetModified(TRUE);

	UpdateData(FALSE);
	CPropertyPage::OnHScroll(nSBCode, nPos, pScrollBar);

	DrawPreview();
}

// NOTE: Can't use 'lCustData' for a structure which would hold that static members,
// because 's_pfnChooseFontHook' will be needed *before* WM_INITDIALOG (which would
// give as the 'lCustData').
static LPCFHOOKPROC s_pfnChooseFontHook = NULL;
static CPPgDisplay *s_pThis = NULL;

// Dialog subclass proc: catch WM_DRAWITEM for the Sample control and WM_PAINT for the Script group box
static LRESULT CALLBACK DialogDrawProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR dlgPtr)
{
	if (!IsDarkModeEnabled())
		return DefSubclassProc(hwnd, uMsg, wParam, lParam);

	switch (uMsg) {
	case WM_DRAWITEM:
	{
		auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
		if (dis->CtlID != cmb5) // only handle the Script combobox
			break;

		HDC hdc = dis->hDC;
		RECT rc = dis->rcItem;

		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, GetCustomSysColor(COLOR_WINDOWTEXT));

		wchar_t buf[128] = {};
		GetWindowTextW(dis->hwndItem, buf, _countof(buf));
		DrawTextW(hdc, buf, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

		return TRUE;  // we handled it
	} 
	case WM_PAINT:
	{
		HWND hSampleGroupBox = GetDlgItem(hwnd, grp2);
		HWND hSampleText = GetDlgItem(hwnd, stc5);

		if (!hSampleGroupBox || !hSampleText)
			break;

		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);

		// draw dialog background
		RECT rc;
		GetClientRect(hwnd, &rc);
		HBRUSH hbr = CreateSolidBrush(GetCustomSysColor(COLOR_WINDOW));
		FillRect(hdc, &rc, hbr);
		DeleteObject(hbr);
		DrawEdge(hdc, &rc, BDR_SUNKENOUTER, BF_RECT);

		// Set dialog caption text color and background mode
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, GetCustomSysColor(COLOR_WINDOWTEXT));

		// compute sample rect in dialog coords
		RECT src;
		GetWindowRect(hSampleGroupBox, &src);
		MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&src), 2);

		// obtain chosen font from the font dialog
		CFontDialog* pDlg = static_cast<CFontDialog*>(CWnd::FromHandle(hwnd));
		LOGFONT lf;
		pDlg->GetCurrentFont(&lf);
		HFONT hSampleFont = CreateFontIndirect(&lf);
		HGDIOBJ hOldSampleFont = SelectObject(hdc, hSampleFont);

		// Draw the actual sample string
		WCHAR buf[128] = {};
		GetWindowTextW(hSampleText, buf, _countof(buf));
		DrawTextW(hdc, buf, -1, &src, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

		SelectObject(hdc, hOldSampleFont);
		DeleteObject(hSampleFont);

		// restore and finish
		EndPaint(hwnd, &ps);
		return TRUE;
	}
	}
	return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

UINT_PTR CALLBACK CPPgDisplay::ChooseFontHook(HWND hdlg, UINT uiMsg, WPARAM wParam, LPARAM lParam)
{
	UINT_PTR uResult;

	// Call MFC's common dialog Hook function
	if (s_pfnChooseFontHook != NULL)
		uResult = (*s_pfnChooseFontHook)(hdlg, uiMsg, wParam, lParam);
	else
		uResult = 0;

	// Do our own Hook processing
	switch (uiMsg) {
	case WM_INITDIALOG:
	{
		if (!IsDarkModeEnabled())
			break;

		// Theme the rest of the dialog
		ApplyTheme(hdlg);

		// Make the Script combobox owner draw so WM_DRAWITEM fires
		HWND hScript = ::GetDlgItem(hdlg, cmb5);
		if (hScript) {
			// disable theming so UxTheme wont overdraw
			SetWindowTheme(hScript, L"", L"");
			// set SS_OWNERDRAW style
			LONG style = GetWindowLong(hScript, GWL_STYLE);
			style = (style & ~SS_TYPEMASK) | SS_OWNERDRAW;
			SetWindowLong(hScript, GWL_STYLE, style);
			::SetWindowPos(hScript, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
			// subclass the **dialog** to catch WM_DRAWITEM for this control
			SetWindowSubclass(hdlg, DialogDrawProc, /*uIdSubclass=*/1,/*dwRefData=*/reinterpret_cast<DWORD_PTR>(CFontDialog::FromHandle(hdlg)));
			// initial paint
			::InvalidateRect(hScript, nullptr, TRUE);
		}
		
		// Make the Sample groupbox owner draw so WM_DRAWITEM fires
		HWND hSample = ::GetDlgItem(hdlg, grp2);
		if (hSample)
			// subclass the **dialog** to catch WM_DRAWITEM for this control
			SetWindowSubclass(hdlg, DialogDrawProc, /*uIdSubclass=*/1,/*dwRefData=*/reinterpret_cast<DWORD_PTR>(CFontDialog::FromHandle(hdlg)));

		break;
	}
	case WM_COMMAND:
		if (LOWORD(wParam) == psh3/*Apply*/ && HIWORD(wParam) == BN_CLICKED) {
			CFontDialog *pDlg = static_cast<CFontDialog*>(CWnd::FromHandle(hdlg));
			ASSERT(pDlg != NULL);
			if (pDlg != NULL) {
				LOGFONT lf;
				pDlg->GetCurrentFont(&lf);
				if (s_pThis->m_eSelectFont == sfLog)
					theApp.emuledlg->ApplyLogFont(&lf);
				else
					theApp.emuledlg->ApplyHyperTextFont(&lf);
			}
		}
		break;
	}

	// If the hook procedure returns zero, the default dialog box procedure processes the message.
	return uResult;
}

void CPPgDisplay::OnBnClickedSelectHypertextFont()
{
	m_eSelectFont = (GetKeyState(VK_CONTROL) < 0) ? sfLog : sfServer;

	// get current font description
	CFont *pFont = (m_eSelectFont == sfLog) ? &theApp.m_fontLog : &theApp.m_fontHyperText; //cannot be NULL
	LOGFONT lf;
	if (!pFont->GetObject(sizeof(LOGFONT), &lf))
		AfxGetMainWnd()->GetFont()->GetLogFont(&lf);

	// Initialize 'CFontDialog'
	CFontDialog dlg(&lf, CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT);
	dlg.m_cf.Flags |= CF_APPLY | CF_ENABLEHOOK;

	// Set 'lpfnHook' to our own Hook function. But save MFC's hook!
	s_pfnChooseFontHook = dlg.m_cf.lpfnHook;
	dlg.m_cf.lpfnHook = ChooseFontHook;
	s_pThis = this;

	if (dlg.DoModal() == IDOK)
		if (m_eSelectFont == sfLog)
			theApp.emuledlg->ApplyLogFont(&lf);
		else
			theApp.emuledlg->ApplyHyperTextFont(&lf);

	s_pfnChooseFontHook = NULL;
	s_pThis = NULL;
}

void CPPgDisplay::OnBtnClickedResetHist()
{
	theApp.emuledlg->searchwnd->ResetHistory();
	theApp.emuledlg->serverwnd->ResetHistory();
	theApp.emuledlg->kademliawnd->ResetHistory();
}

void CPPgDisplay::OnHelp()
{
	theApp.ShowHelp(eMule_FAQ_Preferences_Display);
}

BOOL CPPgDisplay::OnCommand(WPARAM wParam, LPARAM lParam)
{
	return (wParam == ID_HELP) ? OnHelpInfo(NULL) : __super::OnCommand(wParam, lParam);
}

BOOL CPPgDisplay::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}

void CPPgDisplay::DrawPreview()
{
	int dep = static_cast<CSliderCtrl*>(GetDlgItem(IDC_3DDEPTH))->GetPos();
	m_3DPreview.SetSliderPos(dep);
}
