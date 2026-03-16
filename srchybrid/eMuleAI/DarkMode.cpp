//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#include "stdafx.h"
#include "DarkMode.h"
#include "resource.h"
#include "Emule.h"
#include "EmuleDlg.h"
#include "OtherFunctions.h"
#include <windowsx.h>          // SetWindowSubclass, RemoveWindowSubclass, DefSubclassProc, EnumChildWindows
#include <shellapi.h>          // SHGetPathFromIDList
#include <CommCtrl.h>          // TreeView_SetBkColor, TreeView_SetTextColor
#include "MuleToolBarCtrl.h"
#include "DropDownButton.h"
#include "SearchDlg.h"
#include "TransferDlg.h"
#include "SearchParamsWnd.h"
#include "ServerWnd.h"
#include "ProgressCtrlX.h"
#include "MuleListCtrl.h"
#include "MenuXP.h"
#include "ExitBox.h"
#include "FileDetailDialogInfo.h"
#include "IPFilterDlg.h"
#include "Log.h"
#include <future>				// std::packaged_task, std::future

static const wchar_t* kBBPropDarkHdr = L"BB_DarkHdr";
static const wchar_t* kBBPropHdrHover = L"BB_DarkHdrHover";
static const wchar_t* kBBPropHdrSortCol = L"BB_SortCol";
static const wchar_t* kBBPropHdrSortDir = L"BB_SortDir"; // 1: up, 0: down
static const wchar_t* kBBPropMigrationDetailsError = L"BB_MigrationDetailsError";
static const DWORD kThemeTimeoutMs = 50; // Max wait for theming when guarding against hangs

static bool ApplyThemeToWindowWithTimeout(HWND hWnd, bool bForceRedraw, bool bForceWindowsTheme);
static const COLORREF s_aIrcColorsLight[16] =
{
	RGB(0xff, 0xff, 0xff),	// 00: white
	RGB(0,		 0,    0),	// 01: black
	RGB(0,		 0, 0x7f),	// 02: dark blue
	RGB(0,	  0x93,    0),	// 03: dark green
	RGB(0xff,    0,    0),	// 04: red
	RGB(0x7f,    0,    0),	// 05: dark red
	RGB(0x9c,    0, 0x9c),	// 06: purple
	RGB(0xfc, 0x7f,    0),	// 07: orange
	RGB(0xff, 0xff,    0),	// 08: yellow
	RGB(0,	  0xff,    0),	// 09: green
	RGB(0,	  0x7f, 0x7f),	// 10: dark cyan
	RGB(0,	  0xff, 0xff),	// 11: cyan
	RGB(0,		 0, 0xff),	// 12: blue
	RGB(0xff,    0, 0xff),	// 13: pink
	RGB(0x7f, 0x7f, 0x7f),	// 14: dark grey
	RGB(0xd2, 0xd2, 0xd2)	// 15: light grey
};

static const COLORREF s_aIrcColorsDark[16] =
{
	RGB(0xff, 0xff, 0xff),	// 00: white
	RGB(0xe6, 0xe6, 0xe6),	// 01: black (lightened for dark mode readability)
	RGB(0x66, 0x99, 0xff),	// 02: dark blue
	RGB(0x66, 0xcc, 0x66),	// 03: dark green
	RGB(0xff, 0x66, 0x66),	// 04: red
	RGB(0xcc, 0x66, 0x66),	// 05: dark red
	RGB(0xcc, 0x99, 0xff),	// 06: purple
	RGB(0xff, 0xb3, 0x66),	// 07: orange
	RGB(0xff, 0xea, 0x80),	// 08: yellow
	RGB(0x66, 0xff, 0x66),	// 09: green
	RGB(0x66, 0xcc, 0xcc),	// 10: dark cyan
	RGB(0x66, 0xff, 0xff),	// 11: cyan
	RGB(0x85, 0xad, 0xff),	// 12: blue
	RGB(0xff, 0x99, 0xff),	// 13: pink
	RGB(0xc0, 0xc0, 0xc0),	// 14: dark grey
	RGB(0xe0, 0xe0, 0xe0)	// 15: light grey
};

// Checks if Dark Mode is enabled
bool IsDarkModeEnabled() {
	if (thePrefs.GetUIDarkMode() == 1) // Dark mode enabled
		return true;
	else if (thePrefs.GetUIDarkMode() == 2) // Light mode enabled
		return false;

	return thePrefs.m_bDarkModeEnabled; // GetUIDarkMode() == 0 : Dark mode is set to automatic
}

// Function to update dark mode status from the registry
void GetSystemDarkModeStatus() {
	HKEY hKey;
	DWORD dwValue = 0;
	DWORD dwSize = sizeof(dwValue);

	if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
		RegQueryValueEx(hKey, L"AppsUseLightTheme", nullptr, nullptr, (LPBYTE)&dwValue, &dwSize);
		thePrefs.m_bDarkModeEnabled = (dwValue == 0); // 0 means dark mode is enabled
		RegCloseKey(hKey);
	} 
}

// Switch color scheme for the entire application or a specific window and its children
void ApplyTheme(HWND hWnd) {
	if (hWnd == nullptr || theApp.IsClosing())
		return;

	// Apply Dark Mode to the current window (control)
	ApplyThemeToWindow(hWnd, false, false);

	HWND hChild = GetWindow(hWnd, GW_CHILD); // Get the first child control
	while (hChild != nullptr) {
		// Recursively apply Dark Mode to all child controls
		ApplyTheme(hChild);
		hChild = GetWindow(hChild, GW_HWNDNEXT);
	}

	// This is the main window, so we also need to update other specific elements
	if (hWnd == AfxGetMainWnd()->GetSafeHwnd()) {
		BOOL m_bDarkMode = IsDarkModeEnabled();
		if (m_bDarkMode) {
			// Setup log text colors
			if (thePrefs.m_crLogError == RGB(255, 0, 0))
				thePrefs.m_crLogError = RGB(255, 102, 102);		// Light Red
			if (thePrefs.m_crLogWarning == RGB(128, 0, 128))
				thePrefs.m_crLogWarning = RGB(186, 85, 211);	// Light Purple (Orchid)
			if (thePrefs.m_crLogSuccess == RGB(0, 0, 255))
				thePrefs.m_crLogSuccess = RGB(173, 216, 255);	// Very Light Blue
		} else {
			// Setup log text colors
			if (thePrefs.m_crLogError == RGB(255, 102, 102))	// Light Red
				thePrefs.m_crLogError = RGB(255, 0, 0);
			if (thePrefs.m_crLogWarning == RGB(186, 85, 211))	// Light Purple (Orchid)
				thePrefs.m_crLogWarning = RGB(128, 0, 128);
			if (thePrefs.m_crLogSuccess == RGB(173, 216, 255))	// Very Light Blue
				thePrefs.m_crLogSuccess = RGB(0, 0, 255);
		}

		theApp.emuledlg->toolbar->UpdateBackground();
		AfxGetMainWnd()->SendMessage(WM_SYSCOLORCHANGE);

		// Update CRichEditCtrlX colors
		theApp.emuledlg->serverwnd->m_MyInfo.UpdateColors();

		// Trigger color change for auto complete dropdown windows which are not children of the main window. Instead they're separate "Auto-Suggest Dropdown" windows created by the system.
		for (HWND hwndDropdown = NULL; (hwndDropdown = FindWindowEx(NULL, hwndDropdown, _T("Auto-Suggest Dropdown"), NULL)) != NULL;) {
			EnumChildWindows(hwndDropdown, [](HWND hwndChild, LPARAM) -> BOOL {
				TCHAR className[256];
				GetClassName(hwndChild, className, 256);
				TCHAR caption[256];
				GetWindowText(hwndChild, caption, 256);

				if (_tcscmp(className, WC_LISTVIEW) == 0 && _tcscmp(caption, _T("Internet Explorer")) == 0) {
					DWORD_PTR bb = 0; 
					if (!GetWindowSubclass(hwndChild, CustomListViewProc, /*uIdSubclass=*/1, &bb))
						SetWindowSubclass(hwndChild, CustomListViewProc, /*uIdSubclass=*/1, /*dwRefData=*/0);
					return FALSE; // stop after subclassing this ListView
				} else if (_tcscmp(className, WC_SCROLLBAR) == 0)
					ApplyThemeToWindowWithTimeout(hwndChild, false, false);

				return TRUE; // continue enumeration
			}, 0);
		}

		RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_FRAME); // Invalidate and repaint every child window
		SetWindowPos(hWnd, nullptr, 0, 0, 0, 0, SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED); // Force non-client area (title bar, borders) to be repainted

		// Deactivate then reactivate the windows non-client area. This help to force redrawing title bar.
		::SendMessage(hWnd, WM_NCACTIVATE, FALSE, 0);
		::SendMessage(hWnd, WM_NCACTIVATE, TRUE, 0);
	}
}

static bool ApplyThemeToWindowWithTimeout(HWND hWnd, bool bForceRedraw, bool bForceWindowsTheme)
{
	std::packaged_task<void()> task([hWnd, bForceRedraw, bForceWindowsTheme]() {
		ApplyThemeToWindow(hWnd, bForceRedraw, bForceWindowsTheme);
	});
	std::future<void> fut = task.get_future();
	std::thread worker(std::move(task));
	const std::future_status status = fut.wait_for(std::chrono::milliseconds(kThemeTimeoutMs));
	if (status == std::future_status::timeout) {
		AddDebugLogLine(false, _T("[DarkMode][Theme] Timeout theming hwnd=0x%p after %lumsec"), hWnd, static_cast<unsigned long>(kThemeTimeoutMs));
		worker.detach(); // allow OS to clean up thread if stuck
		return false;
	}
	worker.join();
	return true;
}

// Updates the color settings of a control for Dark Mode
void ApplyThemeToWindow(HWND hWnd, bool bForceRedraw, bool bForceWindowsTheme)
{
	if (!hWnd || !::IsWindow(hWnd)) // Guard: window must be valid and created.
		return;

	// Idempotent subclassing on a conservative whitelist of control classes
	wchar_t className[256] = { 0 };
	GetClassName(hWnd, className, _countof(className));
	if  ((wcscmp(className, WC_LISTVIEW) == 0) ||
		(wcscmp(className, WC_TREEVIEW) == 0) ||
		(wcscmp(className, WC_HEADER) == 0) ||
		(wcscmp(className, WC_TABCONTROL) == 0) ||
		(wcscmp(className, TOOLBARCLASSNAME) == 0) ||
		(wcscmp(className, PROGRESS_CLASS) == 0) ||
		(wcscmp(className, L"ReBarWindow32") == 0) ||
		(wcscmp(className, L"msctls_statusbar32") == 0) ||
		(wcscmp(className, L"SysLink") == 0) ||
		(wcscmp(className, WC_EDIT) == 0) ||
		(wcscmp(className, WC_BUTTON) == 0) ||
		(wcscmp(className, WC_STATIC) == 0) ||
		(wcscmp(className, WC_COMBOBOX) == 0) ||
		(wcscmp(className, WC_COMBOBOXEX) == 0) ||
		(wcscmp(className, WC_SCROLLBAR) == 0) ||
		(wcscmp(className, L"ListBox") == 0) ||
		(wcscmp(className, L"ComboLBox") == 0) ||
		(wcscmp(className, L"#32770") == 0) ||
		(wcscmp(className, L"RichEdit20A") == 0) || (wcscmp(className, L"RichEdit20W") == 0) ||
		(wcscmp(className, L"RichEdit50W") == 0)) {
			// This avoids repeated SetWindowSubclass calls
			DWORD_PTR dummy = 0;
			if (!GetWindowSubclass(hWnd, SubclassProc, /*uIdSubclass=*/1, &dummy)) {
				SetWindowSubclass(hWnd, SubclassProc, /*uIdSubclass=*/1, /*dwRefData=*/0);
			}
	}

	// Background and text color changes
	HDC hdc = GetDC(hWnd);
	if (hdc) {
		SetBkColor(hdc, GetCustomSysColor(COLOR_WINDOW));
		SetTextColor(hdc, GetCustomSysColor(COLOR_WINDOWTEXT));
		ReleaseDC(hWnd, hdc);
	}

	BOOL m_bDarkMode = IsDarkModeEnabled();
	if (m_bDarkMode) {
		LONG style = ::GetWindowLong(hWnd, GWL_STYLE);
		UINT uType = style & BS_TYPEMASK;

		if (bForceWindowsTheme || (wcscmp(className, WC_TABCONTROL) != 0 && wcscmp(className, WC_COMBOBOX) != 0 && wcscmp(className, WC_COMBOBOXEX) != 0 &&
				wcscmp(className, WC_HEADER) != 0 && wcscmp(className, PROGRESS_CLASS) != 0	&& wcscmp(className, STATUSCLASSNAME) != 0 && wcscmp(className, TOOLBARCLASSNAME) != 0 &&
				!(wcscmp(className, WC_BUTTON) == 0 && uType != BS_DEFPUSHBUTTON && uType != BS_PUSHBUTTON))) {
			SetWindowTheme(hWnd, L"DarkMode_Explorer", nullptr); // Use Windows API to set Dark Mode theme colors
			DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &m_bDarkMode, sizeof(m_bDarkMode)); // Apply settings for DWM (Desktop Window Manager)
		} else
			SetWindowTheme(hWnd, L"", L""); // Disable visual styles for special control types

		if (wcscmp(className, WC_BUTTON) == 0 && uType != BS_DEFPUSHBUTTON && uType != BS_PUSHBUTTON &&
		   (uType == BS_CHECKBOX || uType == BS_AUTOCHECKBOX || uType == BS_3STATE || uType == BS_AUTO3STATE || uType == BS_RADIOBUTTON || uType == BS_AUTORADIOBUTTON)) {
			DWORD_PTR bbBtn = 0; 
			if (!GetWindowSubclass(hWnd, ButtonSubclassProc, /*uIdSubclass=*/1, &bbBtn))
				SetWindowSubclass(hWnd, ButtonSubclassProc, /*uIdSubclass=*/1,/*dwRefData=*/0);
		} else if (wcscmp(className, WC_LISTVIEW) == 0) { // If this is a listview, subclass its header
			HWND hHeader = ListView_GetHeader(hWnd); // Get the header control handle for this list control
			if (hHeader) { // Guard against NULL header handles.
				// Idempotent: if already marked, just ensure style and skip
				if (GetProp(hHeader, L"BB_DarkHdr")) {
					LONG_PTR style = GetWindowLongPtr(hHeader, GWL_STYLE);
					SetWindowLongPtr(hHeader, GWL_STYLE, style | HDS_HOTTRACK); // Ensure hot tracking
				} else {
					DWORD_PTR bbHdr = 0;
					if (!GetWindowSubclass(hHeader, HeaderSubclassProc, /*uIdSubclass=*/1, &bbHdr)) {
						SetWindowSubclass(hHeader, HeaderSubclassProc, /*uIdSubclass=*/1, /*dwRefData=*/0);
						SetProp(hHeader, L"BB_SortCol", (HANDLE)(INT_PTR)-1); // Initialize sort state
						SetProp(hHeader, L"BB_SortDir", (HANDLE)(INT_PTR)1);
					}
					LONG_PTR style = GetWindowLongPtr(hHeader, GWL_STYLE);
					SetWindowLongPtr(hHeader, GWL_STYLE, style | HDS_HOTTRACK);
					SetProp(hHeader, L"BB_DarkHdr", (HANDLE)1);
				}
			}
		}
	} else {
		// Use Windows API to set Light Mode theme colors
		SetWindowTheme(hWnd, nullptr, nullptr); // Reset to default theme
		DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &m_bDarkMode, sizeof(m_bDarkMode)); // Apply settings for DWM (Desktop Window Manager) to disable Dark Mode
	}

	if (wcscmp(className, WC_LISTVIEW) == 0) { // Set colors for listview controls
		if (CMuleListCtrl* pMuleList = DYNAMIC_DOWNCAST(CMuleListCtrl, CWnd::FromHandlePermanent(hWnd)))
			pMuleList->RefreshThemeColors();
		else {
			ListView_SetBkColor(hWnd, GetCustomSysColor(COLOR_WINDOW));
			ListView_SetTextBkColor(hWnd, GetCustomSysColor(COLOR_WINDOW));
			ListView_SetTextColor(hWnd, GetCustomSysColor(COLOR_WINDOWTEXT));
		}
	} else if (wcscmp(className, WC_TREEVIEW) == 0) { // Set colors for treeview controls
		TreeView_SetBkColor(hWnd, GetCustomSysColor(COLOR_WINDOW));
		TreeView_SetTextColor(hWnd, GetCustomSysColor(COLOR_WINDOWTEXT));
		TreeView_SetLineColor(hWnd, GetCustomSysColor(COLOR_WINDOWTEXT));
	} else if (wcscmp(className, PROGRESS_CLASS) == 0) { // Set colors for progress bar controls
		// Attempt to set colors via CProgressCtrlX if available
		CWnd* pWnd = CWnd::FromHandle(hWnd);
		CProgressCtrlX* pCtrlX = nullptr;
		if (pWnd)
			pCtrlX = dynamic_cast<CProgressCtrlX*>(pWnd);

		if (pCtrlX)	{
			pCtrlX->SetBkColor(GetCustomSysColor(COLOR_WINDOW));
			pCtrlX->SetBarColor(GetCustomSysColor(COLOR_PROGRESSBAR, true));
			pCtrlX->SetTextColor(GetCustomSysColor(COLOR_WINDOWTEXT));
		} else {
			// Fallback: send messages directly for regular CProgressCtrl appearance
			SendMessage(hWnd, PBM_SETBKCOLOR, 0, GetCustomSysColor(COLOR_WINDOW));
			SendMessage(hWnd, PBM_SETBARCOLOR, 0, GetCustomSysColor(COLOR_PROGRESSBAR, true));
		}
	} else if (wcscmp(className, TOOLBARCLASSNAME) == 0) { // Modify style for toolbar controls
		LONG_PTR style = ::GetWindowLongPtr(hWnd, GWL_STYLE); // retrieve current window style
		// If darkmode is active add the custom-erase toolbar style, otherwise remove it, then write back the modified style
		::SetWindowLongPtr(hWnd, GWL_STYLE, m_bDarkMode ? style |= TBSTYLE_CUSTOMERASE : style &= ~TBSTYLE_CUSTOMERASE);
	} else if (wcscmp(className, WC_COMBOBOX) == 0 || wcscmp(className, WC_COMBOBOXEX) == 0) {
		LONG style = ::GetWindowLong(hWnd, GWL_STYLE);
		if ((style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE)) == 0) // Only enforce owner-draw if not already owner-drawn (fixed or variable).
			::SetWindowLong(hWnd, GWL_STYLE, style | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS);
		
		DWORD_PTR bbCmb = 0;
		if (!GetWindowSubclass(hWnd, ComboBoxSubclassProc, /*uIdSubclass=*/1, &bbCmb)) // Idempotent subclassing for combobox (avoid duplicate SetWindowSubclass).
			SetWindowSubclass(hWnd, ComboBoxSubclassProc, /*uIdSubclass=*/1, /*dwRefData=*/0);

		// Apply DarkMode theme to the drop-down list portion
		COMBOBOXINFO cbi = { sizeof(cbi) };
		if (GetComboBoxInfo(hWnd, &cbi)) {
			HWND hList = cbi.hwndList;
			if (hList) {
				SetWindowTheme(hList, L"DarkMode_Explorer", nullptr);
				DwmSetWindowAttribute(hList, DWMWA_USE_IMMERSIVE_DARK_MODE, &m_bDarkMode, sizeof(m_bDarkMode));
			}
		}
	} else if (wcscmp(className, WC_EDIT) == 0) { // Attach DarkMode compatible context menus to all Edit controls
		DWORD_PTR bbEdit = 0;
		if (!GetWindowSubclass(hWnd, DarkEditSubclassProc, /*uIdSubclass=*/1, &bbEdit))
			SetWindowSubclass(hWnd, DarkEditSubclassProc, /*uIdSubclass=*/1, /*dwRefData=*/0);
	} else if (wcscmp(className, DATETIMEPICK_CLASS) == 0) { // handle DateTimePicker
		DWORD_PTR bbDT = 0;
		if (!GetWindowSubclass(hWnd, DateTimeSubclassProc, /*uIdSubclass=*/1, &bbDT))
			SetWindowSubclass(hWnd, DateTimeSubclassProc, /*uIdSubclass=*/1, /*dwRefData=*/0);
	} else if (wcscmp(className, UPDOWN_CLASS) == 0) { // SpinButton on CTabCtrl pages (Win10 and lower)
		HWND hParent = GetParent(hWnd);
		if (hParent) {
			wchar_t parentClass[256] = {};
			GetClassName(hParent, parentClass, 255);
			if (wcscmp(parentClass, WC_TABCONTROLW) == 0) { // Check if parent is of type CTabCtrl
				if (thePrefs.GetWindowsVersion() <= _WINVER_10_) { // Check if OS version is Windows 10 or lower
					SetWindowTheme(hWnd, L"", L""); // disable default themed arrows
					DWORD_PTR bbSpin = 0;
					if (!GetWindowSubclass(hWnd, SpinButtonSubclassProc, /*uIdSubclass=*/1, &bbSpin)) 
						SetWindowSubclass(hWnd, SpinButtonSubclassProc, /*uIdSubclass=*/1, /*dwRefData=*/0);
				}
			}
		}
	}

	if (bForceRedraw) {
		RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_FRAME); // Invalidate and repaint every child window
		SetWindowPos(hWnd, nullptr, 0, 0, 0, 0, SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED); // Force non-client area (title bar, borders) to be repainted
	}
}

const COLORREF* GetIrcColorTable()
{
	return IsDarkModeEnabled() ? s_aIrcColorsDark : s_aIrcColorsLight;
}

UINT GetIrcColorTableSize()
{
	return static_cast<UINT>(_countof(s_aIrcColorsLight));
}

COLORREF GetIrcColorByIndex(int nIndex)
{
	if (nIndex < 0 || static_cast<UINT>(nIndex) >= GetIrcColorTableSize())
		return CLR_DEFAULT;

	return GetIrcColorTable()[nIndex];
}

static UINT GetColorLuma(COLORREF crColor)
{
	return (299U * GetRValue(crColor) + 587U * GetGValue(crColor) + 114U * GetBValue(crColor)) / 1000U;
}

static COLORREF BlendThemeColor(COLORREF crBase, COLORREF crAccent, BYTE byAccentWeight)
{
	const BYTE byBaseWeight = static_cast<BYTE>(255 - byAccentWeight);
	return RGB(
		((GetRValue(crBase) * byBaseWeight) + (GetRValue(crAccent) * byAccentWeight)) / 255,
		((GetGValue(crBase) * byBaseWeight) + (GetGValue(crAccent) * byAccentWeight)) / 255,
		((GetBValue(crBase) * byBaseWeight) + (GetBValue(crAccent) * byAccentWeight)) / 255);
}

void EnsureReadableTextColors(COLORREF& crForeground, COLORREF& crBackground, COLORREF crDefaultForeground, COLORREF crDefaultBackground)
{
	const COLORREF resolvedForeground = (crForeground == CLR_DEFAULT) ? crDefaultForeground : crForeground;
	const COLORREF resolvedBackground = (crBackground == CLR_DEFAULT) ? crDefaultBackground : crBackground;

	// Keep IRC color formatting but avoid near-identical fg/bg combinations.
	const UINT uLumaForeground = GetColorLuma(resolvedForeground);
	const UINT uLumaBackground = GetColorLuma(resolvedBackground);
	const UINT uLumaDelta = (uLumaForeground > uLumaBackground) ? (uLumaForeground - uLumaBackground) : (uLumaBackground - uLumaForeground);
	const UINT uMinReadableDelta = 90U;
	if (uLumaDelta >= uMinReadableDelta)
		return;

	COLORREF crCandidate = crDefaultForeground;
	UINT uCandidateLuma = GetColorLuma(crCandidate);
	UINT uCandidateDelta = (uCandidateLuma > uLumaBackground) ? (uCandidateLuma - uLumaBackground) : (uLumaBackground - uCandidateLuma);
	if (uCandidateDelta < uMinReadableDelta)
		crCandidate = (uLumaBackground < 128U) ? RGB(255, 255, 255) : RGB(0, 0, 0);

	crForeground = crCandidate;
}

TooltipThemeColors GetTooltipThemeColors()
{
	TooltipThemeColors colors = {};
	const bool bDarkMode = IsDarkModeEnabled();
	const COLORREF crBackground = GetCustomSysColor(COLOR_INFOBK);
	const COLORREF crDefaultText = GetCustomSysColor(COLOR_INFOTEXT);

	colors.crBackground = crBackground;
	colors.crValueText = bDarkMode ? BlendThemeColor(crDefaultText, crBackground, 30) : BlendThemeColor(crDefaultText, crBackground, 22);
	if (bDarkMode) {
		colors.crCaptionBackground = BlendThemeColor(crBackground, RGB(58, 70, 84), 92);
		colors.crBorder = BlendThemeColor(crBackground, RGB(124, 142, 166), 88);
		colors.crMainTitleText = RGB(181, 214, 204);
		colors.crTitleText = RGB(142, 171, 163);
		colors.crKeyText = RGB(104, 186, 242);
		colors.crSeparator = BlendThemeColor(crBackground, RGB(104, 186, 242), 54);
	} else {
		colors.crCaptionBackground = BlendThemeColor(crBackground, RGB(214, 222, 232), 132);
		colors.crBorder = BlendThemeColor(crBackground, RGB(118, 136, 160), 84);
		colors.crMainTitleText = RGB(104, 58, 104);
		colors.crTitleText = RGB(129, 82, 128);
		colors.crKeyText = RGB(42, 114, 188);
		colors.crSeparator = BlendThemeColor(crBackground, RGB(42, 114, 188), 40);
	}

	COLORREF crBackgroundForContrast = colors.crBackground;
	EnsureReadableTextColors(colors.crValueText, crBackgroundForContrast, crDefaultText, colors.crBackground);
	crBackgroundForContrast = colors.crBackground;
	EnsureReadableTextColors(colors.crMainTitleText, crBackgroundForContrast, crDefaultText, colors.crBackground);
	crBackgroundForContrast = colors.crBackground;
	EnsureReadableTextColors(colors.crTitleText, crBackgroundForContrast, crDefaultText, colors.crBackground);
	crBackgroundForContrast = colors.crBackground;
	EnsureReadableTextColors(colors.crKeyText, crBackgroundForContrast, crDefaultText, colors.crBackground);
	crBackgroundForContrast = colors.crBackground;
	EnsureReadableTextColors(colors.crSeparator, crBackgroundForContrast, crDefaultText, colors.crBackground);
	return colors;
}

// Returns a custom system color based on the index and dark mode status
COLORREF GetCustomSysColor(int nIndex, bool bForceDarkColor)
{
	if (bForceDarkColor || IsDarkModeEnabled()) { // Handle specific system colors for dark mode
		switch (nIndex) {
		case COLOR_SCROLLBAR:				return RGB(60, 60, 60); // Dark scrollbar background
		case COLOR_BACKGROUND:				return RGB(18, 18, 18); // Dark desktop background (same as COLOR_DESKTOPs)
		case COLOR_ACTIVECAPTION:			return RGB(61, 61, 61); // Dark active window caption
		case COLOR_INACTIVECAPTION:			return RGB(46, 46, 46); // Dark inactive window caption
		case COLOR_MENU:					return RGB(30, 30, 30); // Dark menu background
		case COLOR_WINDOW:					return RGB(30, 30, 30); // Dark window background
		case COLOR_WINDOWFRAME:				return RGB(85, 85, 85); // Dark window frame
		case COLOR_MENUTEXT:				return RGB(255, 255, 255); // White menu text
		case COLOR_WINDOWTEXT:				return RGB(255, 255, 255); // White window text
		case COLOR_CAPTIONTEXT:				return RGB(255, 255, 255); // White caption text
		case COLOR_ACTIVEBORDER:			return RGB(75, 75, 75); // Dark active window border
		case COLOR_INACTIVEBORDER:			return RGB(60, 60, 60); // Dark inactive window border
		case COLOR_APPWORKSPACE:			return RGB(30, 30, 30); // Dark app workspace
		case COLOR_HIGHLIGHT:				return RGB(39, 75, 107); // Dark highlight background
		case COLOR_HIGHLIGHTTEXT:			return RGB(255, 255, 255); // White highlighted text
		case COLOR_BTNFACE:					return RGB(53, 53, 53); // Dark button face (background) (same as COLOR_3DFACE)
		case COLOR_BTNSHADOW:				return RGB(96, 96, 96); // Dark button shadow (same as COLOR_3DSHADOW)
		case COLOR_GRAYTEXT:				return RGB(140, 140, 140); // Gray text for disabled items
		case COLOR_BTNTEXT:					return RGB(255, 255, 255); // White button text
		case COLOR_INACTIVECAPTIONTEXT:		return RGB(160, 160, 160); // Dimmed text for inactive captions
		case COLOR_BTNHIGHLIGHT:			return RGB(90, 90, 90); // Dark button highlight edge (same as COLOR_3DHIGHLIGHT, COLOR_3DHILIGHT,COLOR_BTNHILIGHT)
		case COLOR_3DDKSHADOW:				return RGB(32, 32, 32); // Dark deep 3D shadow
		case COLOR_3DLIGHT:					return RGB(100, 100, 100); // Light 3D effect
		case COLOR_INFOTEXT:				return RGB(255, 255, 255); // White info text
		case COLOR_INFOBK:					return RGB(45, 45, 45); // Dark info background
		case COLOR_HOTLIGHT:				return RGB(255, 140, 0); // Orange for hotlight (hyperlink)
		case COLOR_GRADIENTACTIVECAPTION:	return RGB(70, 70, 70); // Gradient active caption for dark mode
		case COLOR_GRADIENTINACTIVECAPTION:	return RGB(50, 50, 50); // Gradient inactive caption
		case COLOR_MENUHILIGHT:				return RGB(200, 200, 200); // Light gray for menu highlight
		case COLOR_MENUBAR:					return RGB(30, 30, 30); // Dark menu bar
		case COLOR_SHADEBASE:				return RGB(220, 220, 220); // Download list shade base color for dark mode
		case COLOR_SEARCH_DOWNLOADING:		return RGB(189, 183, 107); // Lighter Olive green
		case COLOR_SEARCH_STOPPED:			return RGB(189, 183, 107); // Lighter Olive green
		case COLOR_SEARCH_SHARING:			return RGB(34, 139, 34); // Lighter Dark green
		case COLOR_SEARCH_KNOWN:			return RGB(50, 205, 50); // Lighter Medium green
		case COLOR_SEARCH_CANCELED:			return RGB(255, 165, 79); // Lighter Orange
		case COLOR_MAN_BLACKLIST:			return RGB(186, 85, 211); // Lighter Purple
		case COLOR_AUTO_BLACKLIST:			return RGB(255, 105, 180); // Lighter Pink
		case COLOR_SPAM:					return RGB(255, 99, 71); // Lighter Red
		case COLOR_SERVER_CONNECTED:		return RGB(173, 216, 255); // Light Blue
		case COLOR_SERVER_FAILED:			return RGB(240, 240, 240); // Lightest Grey
		case COLOR_SERVER_DEAD:				return RGB(200, 200, 200); // Lighter Grey
		case COLOR_PROGRESSBAR:				return RGB(70, 130, 180); // Steel Blue
		case COLOR_SELECTEDTABTOPLINE:		return RGB(113, 96, 232); // Medium Slate Blue
		case COLOR_TABBORDER:				return RGB(64, 64, 64); // Dark Slate Gray
		case COLOR_MENUXP_SIDEBAR_TEXT:		return RGB(255, 255, 255); // White
		case COLOR_MENUXP_TITLE_TEXT:		return RGB(255, 255, 255); // White
		case COLOR_MENUXP_SIDEBAR_GRADIENT_START:	return RGB(0, 0, 0); // Black
		case COLOR_MENUXP_SIDEBAR_GRADIENT_END:		return RGB(255, 233, 0); // Yellow
		case COLOR_MENUXP_TITLE_GRADIENT_START:		return RGB(0, 0, 0); // Black
		case COLOR_MENUXP_TITLE_GRADIENT_END:		return RGB(255, 233, 0); // Yellow
		case COLOR_HELP_TEXT_BACKGROUND:				return RGB(230, 230, 230); // Gray
		case COLOR_IRC_INFO_MSG:						return RGB(255, 110, 110); // Light red
		case COLOR_IRC_STATUS_MSG:						return RGB(130, 220, 130); // Light green
		case COLOR_IRC_EVENT_MSG:						return RGB(140, 190, 255); // Light blue
		case COLOR_IRC_ACTION_MSG:						return RGB(221, 160, 221); // Light purple
		default:										break;
		}
		} else { // Handle specific custom colors for light mode
		switch (nIndex) {
		case COLOR_HIGHLIGHT:				return RGB(191, 221, 245); // Highlight background
		case COLOR_SHADEBASE:				return RGB(0, 0, 255); // Download list shade base color for light mode
		case COLOR_SEARCH_DOWNLOADING:		return RGB(139, 128, 0); // Olive green
		case COLOR_SEARCH_STOPPED:			return RGB(139, 128, 0); // Olive green
		case COLOR_SEARCH_SHARING:			return RGB(0, 80, 0); // Dark green
		case COLOR_SEARCH_KNOWN:			return RGB(0, 128, 0); // Medium green
		case COLOR_SEARCH_CANCELED:			return RGB(255, 100, 0); // Orange
		case COLOR_MAN_BLACKLIST:			return RGB(148, 0, 211); // Purple
		case COLOR_AUTO_BLACKLIST:			return RGB(255, 20, 147); // Pink
		case COLOR_SPAM:					return RGB(255, 0, 0); // Red
		case COLOR_SERVER_CONNECTED:		return RGB(32, 32, 255); // Blue
		case COLOR_SERVER_FAILED:			return RGB(192, 192, 192); // Light Grey
		case COLOR_SERVER_DEAD:				return RGB(128, 128, 128); // Grey
		case COLOR_MENUXP_SIDEBAR_TEXT:		return RGB(255, 255, 255); // White
		case COLOR_MENUXP_TITLE_TEXT:		return RGB(255, 255, 255); // White
		case COLOR_MENUXP_SIDEBAR_GRADIENT_START:	return RGB(0, 0, 0); // Black
		case COLOR_MENUXP_SIDEBAR_GRADIENT_END:		return RGB(255, 233, 0); // Yellow
		case COLOR_MENUXP_TITLE_GRADIENT_START:		return RGB(0, 0, 0); // Black
		case COLOR_MENUXP_TITLE_GRADIENT_END:		return RGB(255, 233, 0); // Yellow
		case COLOR_HELP_TEXT_BACKGROUND:				return RGB(230, 230, 230); // Gray
		case COLOR_IRC_INFO_MSG:						return RGB(127, 0, 0); // Dark red
		case COLOR_IRC_STATUS_MSG:						return RGB(0, 147, 0); // Dark green
		case COLOR_IRC_EVENT_MSG:						return RGB(0, 0, 127); // Dark blue
		case COLOR_IRC_ACTION_MSG:						return RGB(156, 0, 156); // Purple
		default:										break;
		}
		}

	// If not in dark mode, or no override needed, return default system color
	return GetSysColor(nIndex);
}

// Dark mode aware background painting. Falls back or defers as needed.
BOOL HandleEraseBkgnd(HWND hWnd, CDC* pDC)
{
	CWnd* pWnd = CWnd::FromHandle(hWnd);
	if (!pWnd || !pDC)
		return FALSE;

	// In dark mode skip background erase for static and button controls
	wchar_t cls[256] = {};
	GetClassNameW(hWnd, cls, _countof(cls));
	if (wcscmp(cls, L"Static") == 0 || wcscmp(cls, WC_BUTTON) == 0)
		return FALSE;

	// Special case: Exclude m_pSpinCtrl area if it is visible for CClosableTabCtrl
	if (pWnd->IsKindOf(RUNTIME_CLASS(CClosableTabCtrl))) {
		CClosableTabCtrl* pTab = static_cast<CClosableTabCtrl*>(pWnd);
		if (pTab->m_pSpinCtrl && pTab->m_pSpinCtrl->IsWindowVisible()) {
			CRect spinRect;
			pTab->m_pSpinCtrl->GetWindowRect(&spinRect);
			pTab->ScreenToClient(&spinRect);
			pDC->ExcludeClipRect(&spinRect);
		}
	}

	if (pWnd->IsKindOf(RUNTIME_CLASS(CButtonsTabCtrl)) || pWnd->IsKindOf(RUNTIME_CLASS(CIPFilterDlg)) || wcscmp(cls, REBARCLASSNAME) == 0) {
		// Dark mode fill entire background
		CRect rc;
		pWnd->GetClientRect(&rc);
		pDC->FillRect(rc, &CDarkMode::m_brDefault);
		return TRUE;
	}

	return DefSubclassProc(hWnd, WM_ERASEBKGND, reinterpret_cast<WPARAM>(pDC->GetSafeHdc()), 0);
}

// Provides brushes and colors for controls in both modes.
HBRUSH HandleCtlColor(HWND hWnd, CDC* pDC, HWND hChild, UINT nCtlColor)
{
	// Fallback to the default processing of WM_CTLCOLORxxx by returning nullptr here, our SubclassProc will call DefSubclassProc, which in turn invokes the original window procedure for CTLCOLOR.
	if (!pDC)
		return nullptr;

	CWnd* cWnd = CWnd::FromHandle(hWnd);
	if (!cWnd)
		return nullptr;

	int ctrlId = ::GetDlgCtrlID(hChild);

	if (hChild != NULL && GetProp(hChild, kBBPropMigrationDetailsError)) {
		pDC->SetBkColor(GetCustomSysColor(COLOR_WINDOW));
		pDC->SetTextColor(thePrefs.GetLogErrorColor());
		return CDarkMode::m_brDefault;
	}

	// Override IDC_FD_X11 control
	if (cWnd->IsKindOf(RUNTIME_CLASS(CFileDetailDialogInfo)) && ctrlId == IDC_FD_X11) {
		// if warning flag is set and this is the special control, draw red text
		CFileDetailDialogInfo* dlg = static_cast<CFileDetailDialogInfo*>(cWnd);
		if (dlg->m_bShowFileTypeWarning) {
			pDC->SetBkColor(GetCustomSysColor(COLOR_WINDOW));
			pDC->SetTextColor(RGB(255, 0, 0));
			return CDarkMode::m_brDefault;
		}
	}

	// CEditDelayed controls
	if (GetProp(hChild, _T("IsEditDelayed"))) {
		CEditDelayed* pEdit = static_cast<CEditDelayed*>(CWnd::FromHandlePermanent(hChild)); // Obtain the CEditDelayed instance from the permanent handle map
		const bool isPlaceholder = pEdit && pEdit->m_bShowsColumnText; // true when showing column text
		pDC->SetBkColor(GetCustomSysColor(COLOR_WINDOW));
		pDC->SetTextColor(GetCustomSysColor(isPlaceholder ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT));
		return static_cast<HBRUSH>(CDarkMode::m_brDefault.GetSafeHandle());
	}

	switch (nCtlColor) {
	case CTLCOLOR_BTN: // Buttons and checkboxes
	{
		// fetch the button style and its type mask
		LONG style = cWnd->GetStyle();
		UINT uType = style & BS_TYPEMASK;

		// Group box: paint as transparent text over default dialog brush.
		if (uType == BS_GROUPBOX) {
			pDC->SetBkMode(TRANSPARENT);
			pDC->SetTextColor(GetCustomSysColor(COLOR_WINDOWTEXT));
			return CDarkMode::m_brDefault;
		}

		// 1) Checkbox and radio buttons
		if (uType == BS_CHECKBOX || uType == BS_AUTOCHECKBOX || uType == BS_3STATE || uType == BS_AUTO3STATE) {
			pDC->SetBkMode(TRANSPARENT);
			// enabled checkboxes get white text; disabled get dark gray
			if (cWnd->IsWindowEnabled())
				pDC->SetTextColor(GetCustomSysColor(COLOR_WINDOWTEXT));
			else
				pDC->SetTextColor(GetCustomSysColor(COLOR_GRAYTEXT));
			return CDarkMode::m_brDefault; // use the default window brush for checkbox background
		}

		// 2) Push buttons and default buttons
		if (!cWnd->IsWindowEnabled()) {
			pDC->SetBkColor(GetCustomSysColor(COLOR_INACTIVECAPTION));
			pDC->SetTextColor(GetCustomSysColor(COLOR_INACTIVECAPTIONTEXT));
			return CDarkMode::m_brInactiveCaption;
		} else if ((cWnd->SendMessage(BM_GETSTATE) & BST_PUSHED) != 0) {
			pDC->SetBkColor(GetCustomSysColor(COLOR_BTNHIGHLIGHT));
			pDC->SetTextColor(GetCustomSysColor(COLOR_HIGHLIGHTTEXT));
		} else {
			pDC->SetBkColor(GetCustomSysColor(COLOR_BTNFACE));
			pDC->SetTextColor(GetCustomSysColor(COLOR_BTNTEXT));
		}
		return CDarkMode::m_brBtn;
	}
	case CTLCOLOR_SCROLLBAR: // Scrollbars
		pDC->SetBkColor(GetCustomSysColor(COLOR_SCROLLBAR));
		pDC->SetTextColor(GetCustomSysColor(COLOR_WINDOWTEXT));
		return CDarkMode::m_brScrollbar;
	}

	// Default return if no conditions are met
	pDC->SetBkColor(GetCustomSysColor(COLOR_WINDOW));
	if (cWnd->IsWindowEnabled())
		pDC->SetTextColor(GetCustomSysColor(COLOR_WINDOWTEXT)); // White text color for enabled controls
	else
		pDC->SetTextColor(GetCustomSysColor(COLOR_GRAYTEXT)); // Gray text for disabled controls

	return CDarkMode::m_brDefault;
}

// Handles WM_ERASEBKGND, CTLCOLOR_*, WM_NCDESTROY for all subclassed windows. Uses DefSubclassProc to chain to default processing.
LRESULT CALLBACK SubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
	// Check if dark mode is enabled, if not, chain to default processing
	if (!IsDarkModeEnabled())
		return DefSubclassProc(hWnd, uMsg, wParam, lParam);

	switch (uMsg)
	{
	case WM_ERASEBKGND:
	{
		CDC* pDC = CDC::FromHandle(reinterpret_cast<HDC>(wParam));
		if (HandleEraseBkgnd(hWnd, pDC))
			return TRUE;
		break; // If HandleEraseBkgnd returned false, fall through to default.
	}
	case WM_CTLCOLORMSGBOX:
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORLISTBOX:
	case WM_CTLCOLORBTN:
	case WM_CTLCOLORDLG:
	case WM_CTLCOLORSCROLLBAR:
	case WM_CTLCOLORSTATIC:
	{
		CDC* pDC = CDC::FromHandle(reinterpret_cast<HDC>(wParam));
		HWND child = reinterpret_cast<HWND>(lParam);
		UINT ctlMsg = uMsg - WM_CTLCOLORMSGBOX;  // remap for dark mode indices
		HBRUSH hBr = HandleCtlColor(hWnd, pDC, child, ctlMsg);
		if (hBr)
			return reinterpret_cast<LRESULT>(hBr);

		break; // If HandleCtlColor returned nullptr, fall through to default.
	}
	case WM_NOTIFY:
	{
		// Handles NM_CUSTOMDRAW for virtual list empty space below the last item in dark mode to eliminate vertical gray stripes.
		LPNMHDR pNM = reinterpret_cast<LPNMHDR>(lParam);
		if (pNM->code == NM_CUSTOMDRAW) {
			TCHAR cls[32]{}; GetClassName(pNM->hwndFrom, cls, _countof(cls));
			if (lstrcmpi(cls, WC_LISTVIEW) == 0 && (static_cast<DWORD>(GetWindowLongPtr(pNM->hwndFrom, GWL_STYLE)) & LVS_OWNERDATA)) {
				LPNMLVCUSTOMDRAW pCD = reinterpret_cast<LPNMLVCUSTOMDRAW>(pNM);
				switch (pCD->nmcd.dwDrawStage)
				{
				case CDDS_PREPAINT:
					return CDRF_NOTIFYPOSTPAINT; // Single callback after whole list is drawn

				case CDDS_POSTPAINT:
				{
					int itemCnt = ListView_GetItemCount(pNM->hwndFrom);
					int perPage = ListView_GetCountPerPage(pNM->hwndFrom);

					// Paint only if there is blank space (itemCnt < perPage)
					if (itemCnt < perPage) {
						RECT rcClient{}; GetClientRect(pNM->hwndFrom, &rcClient);

						if (itemCnt == 0)	// List empty, paint full client area
							FillRect(pCD->nmcd.hdc, &rcClient, CDarkMode::m_brDefault);
						else { // Paint area below last visible item
							RECT rcLast{};
							if (ListView_GetItemRect(pNM->hwndFrom, itemCnt - 1, &rcLast, LVIR_BOUNDS)) {
								RECT rcFill{ rcClient.left, rcLast.bottom, rcClient.right, rcClient.bottom };
								FillRect(pCD->nmcd.hdc, &rcFill, CDarkMode::m_brDefault);
							}
						}
					}
					return CDRF_DODEFAULT;
				}
				}
			}
		}
		break; // If not NM_CUSTOMDRAW or not a listview, fall through to default.
	}
	case WM_NCDESTROY:
	{
		RemoveWindowSubclass(hWnd, SubclassProc, 1); // Remove this subclass when window is destroyed
		break;
	}
	}

	return DefSubclassProc(hWnd, uMsg, wParam, lParam); // Chain to default (or next) subclass procedure
}

// Subclass function for buttons
static LRESULT CALLBACK ButtonSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
	if (!IsDarkModeEnabled())
		return DefSubclassProc(hWnd, uMsg, wParam, lParam);

	switch (uMsg)
	{
	case WM_PAINT:
	{
		BOOL darkMode = TRUE;
		SetWindowTheme(hWnd, L"DarkMode_Explorer", nullptr);
		DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);

		// Let the control draw its default themed appearance (glyph + text)
		SendMessage(hWnd, WM_PRINTCLIENT, (WPARAM)hdc, PRF_CLIENT);

		// Determine control type
		LONG style = ::GetWindowLong(hWnd, GWL_STYLE);
		UINT uType = static_cast<UINT>(style & BS_TYPEMASK);
		bool isCheckbox = (uType == BS_CHECKBOX || uType == BS_AUTOCHECKBOX || uType == BS_3STATE || uType == BS_AUTO3STATE);
		bool isRadio = (uType == BS_RADIOBUTTON || uType == BS_AUTORADIOBUTTON);

		if (isCheckbox || isRadio) {
			// Open theme to measure glyph size
			HTHEME hTheme = OpenThemeData(hWnd, WC_BUTTON);
			int part = isRadio ? BP_RADIOBUTTON : BP_CHECKBOX;
			bool isEnabled = (IsWindowEnabled(hWnd) != FALSE);
			bool isChecked = ((SendMessage(hWnd, BM_GETCHECK, 0, 0) & BST_CHECKED) != 0);

			int state;
			if (part == BP_CHECKBOX)
				state = isEnabled ? (isChecked ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL) : (isChecked ? CBS_CHECKEDDISABLED : CBS_UNCHECKEDDISABLED);
			else
				state = isEnabled ? (isChecked ? RBS_CHECKEDNORMAL : RBS_UNCHECKEDNORMAL) : (isChecked ? RBS_CHECKEDDISABLED : RBS_UNCHECKEDDISABLED);

			SIZE glyphSize = { 0, 0 };
			if (hTheme)	{
				GetThemePartSize(hTheme, hdc, part, state, nullptr, TS_TRUE, &glyphSize);
				CloseThemeData(hTheme);
			} else {
				glyphSize.cx = GetSystemMetrics(SM_CXMENUCHECK);
				glyphSize.cy = GetSystemMetrics(SM_CYMENUCHECK);
			}

			// Compute text rectangle: start slightly inside to fully cover default text
			RECT rcClient;
			GetClientRect(hWnd, &rcClient);
			RECT rcText = rcClient;
			rcText.left = rcClient.left + glyphSize.cx + 3;
			rcText.right = rcClient.right;

			FillRect(hdc, &rcText, CDarkMode::m_brDefault); // Fill text area with dark-mode background to erase default text completely

			// Select appropriate font (inherit from parent if none set on control)
			HFONT hFont = reinterpret_cast<HFONT>(SendMessage(hWnd, WM_GETFONT, 0, 0));
			if (!hFont)	{
				HWND hParent = GetParent(hWnd);
				hFont = reinterpret_cast<HFONT>(SendMessage(hParent, WM_GETFONT, 0, 0));
			}

			if (!hFont)
				hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

			HGDIOBJ oldFont = SelectObject(hdc, hFont);

			// Draw the text in the desired color
			SetBkMode(hdc, TRANSPARENT);
			COLORREF textColor = isEnabled ? GetCustomSysColor(COLOR_WINDOWTEXT) : GetCustomSysColor(COLOR_GRAYTEXT);
			SetTextColor(hdc, textColor);

			wchar_t szText[256] = {};
			GetWindowText(hWnd, szText, _countof(szText));
			UINT dtFlags = (style & BS_MULTILINE) ? (DT_WORDBREAK | DT_VCENTER | DT_LEFT) : (DT_SINGLELINE | DT_VCENTER | DT_LEFT);
			DrawText(hdc, szText, -1, &rcText, dtFlags);

			SelectObject(hdc, oldFont);
		}

		EndPaint(hWnd, &ps);
		return 0;
	}
    case WM_ENABLE:
	{
		// On enable/disable, force a repaint without recursing into WM_PAINT
		InvalidateRect(hWnd, nullptr, TRUE);
		break;
	}
	case WM_NCDESTROY:
		// Remove subclass when control is destroyed
		RemoveWindowSubclass(hWnd, ButtonSubclassProc, /*uIdSubclass=*/1);
		break;
	}

	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Subclass proc to custom draw combo items in dark mode
static LRESULT CALLBACK ComboBoxSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
	if (!IsDarkModeEnabled())
		return DefSubclassProc(hWnd, uMsg, wParam, lParam);

	auto GetBoolProp = [&](LPCWSTR name)->bool { return GetPropW(hWnd, name) != nullptr; };
	auto SetBoolProp = [&](LPCWSTR name, bool v) {
		if (v) 
			SetPropW(hWnd, name, (HANDLE)1);
		else  
			RemovePropW(hWnd, name);
	};

	switch (uMsg) {
	case WM_MOUSEMOVE:
	{
		POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

		COMBOBOXINFO cbi{ sizeof(cbi) };
		GetComboBoxInfo(hWnd, &cbi);
		bool overBtn = PtInRect(&cbi.rcButton, pt) != FALSE;

		if (overBtn != GetBoolProp(L"COMBO_HOT")) {
			SetBoolProp(L"COMBO_HOT", overBtn);
			InvalidateRect(hWnd, &cbi.rcButton, TRUE);
		}

		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hWnd, 0 };
		TrackMouseEvent(&tme);
		break;
	}
	case WM_MOUSELEAVE:
	{
		SetBoolProp(L"COMBO_HOT", false);
		SetBoolProp(L"COMBO_PRESSED", false);
		InvalidateRect(hWnd, nullptr, TRUE);
		break;
	}
	case WM_LBUTTONDOWN:
	{
		POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		COMBOBOXINFO cbi{ sizeof(cbi) };
		GetComboBoxInfo(hWnd, &cbi);
		if (PtInRect(&cbi.rcButton, pt)) {
			SetBoolProp(L"COMBO_PRESSED", true);
			InvalidateRect(hWnd, &cbi.rcButton, TRUE);
		}
		break;
	}
	case WM_LBUTTONUP:
	{
		if (GetBoolProp(L"COMBO_PRESSED")) {
			SetBoolProp(L"COMBO_PRESSED", false);
			InvalidateRect(hWnd, nullptr, TRUE);
		}
		break;
	}
	case WM_PAINT: // Custom paint: arrow button
	{
		LRESULT lRes = DefSubclassProc(hWnd, uMsg, wParam, lParam); // let default/client draw first

		COMBOBOXINFO cbi{ sizeof(cbi) };
		if (!GetComboBoxInfo(hWnd, &cbi))
			return lRes;

		HDC hdc = GetWindowDC(hWnd); // DC for non-client (button)
		bool enabled = IsWindowEnabled(hWnd) != FALSE;
		bool hot = GetBoolProp(L"COMBO_HOT");
		bool pressed = GetBoolProp(L"COMBO_PRESSED");

		// Choose background and frame colors for the button
		FillRect(hdc, &cbi.rcButton, pressed ? CDarkMode::m_brBtnPressed : hot ? CDarkMode::m_brBtnHover : CDarkMode::m_brBtn);
		FrameRect(hdc, &cbi.rcButton, enabled ? CDarkMode::m_brBtnBorder : CDarkMode::m_brBtnBorderDisabled);

		LOGFONT lf{};
		lf.lfHeight = -MulDiv(cbi.rcButton.right - cbi.rcButton.left - 4 /*arrow width*/, GetDeviceCaps(hdc, LOGPIXELSY), 96);
		lf.lfCharSet = SYMBOL_CHARSET;
		lstrcpy(lf.lfFaceName, _T("Marlett"));
		HFONT hFont = CreateFontIndirect(&lf);
		HFONT hOld = (HFONT)SelectObject(hdc, hFont);

		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, enabled ? GetCustomSysColor(COLOR_WINDOWTEXT) : GetCustomSysColor(COLOR_GRAYTEXT));

		const TCHAR arrow = _T('6'); // Down-arrow glyph in Marlett
		SIZE sz;
		GetTextExtentPoint32(hdc, &arrow, 1, &sz);

		int x = cbi.rcButton.left + ((cbi.rcButton.right - cbi.rcButton.left) - sz.cx) / 2;
		int y = cbi.rcButton.top + ((cbi.rcButton.bottom - cbi.rcButton.top) - sz.cy) / 2;
		ExtTextOut(hdc, x, y, ETO_CLIPPED, &cbi.rcButton, &arrow, 1, nullptr);

		SelectObject(hdc, hOld);
		DeleteObject(hFont);
		ReleaseDC(hWnd, hdc);
		return lRes;
	}
	case WM_DRAWITEM: // Owner-draw list items
	{
		auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
		CDC* dc = CDC::FromHandle(dis->hDC);

		// Identify the *real* combobox that owns this draw request for ComboBoxEx the LISTBOX child lives in a nested window
		HWND hComboWnd = hWnd; // Window receiving message
		HIMAGELIST hImgs = reinterpret_cast<HIMAGELIST>(::SendMessage(hComboWnd, CBEM_GETIMAGELIST, 0, 0));

		if (!hImgs) { // No imagelist on this hwnd  maybe inner listbox of ComboBoxEx
			HWND hParent = ::GetParent(hComboWnd);
			TCHAR cls[32];
			while (hParent && hParent != ::GetDesktopWindow()) {
				::GetClassName(hParent, cls, _countof(cls));
				if (_tcscmp(cls, _T("ComboBoxEx32")) == 0) {
					hImgs = reinterpret_cast<HIMAGELIST>(::SendMessage(hParent, CBEM_GETIMAGELIST, 0, 0));
					if (hImgs)
						hComboWnd = hParent; // Promote to outer ComboBoxEx
					break;
				}
				hParent = ::GetParent(hParent);
			}
		}

		const bool isEx = hImgs != nullptr; // true for ComboBoxEx
		const bool hasItem = dis->itemID != static_cast<UINT>(-1);
		const bool selItem = (dis->itemState & ODS_SELECTED) && hasItem;
		const bool disabled = (dis->itemState & ODS_DISABLED) != 0;

		COLORREF clrBk = GetCustomSysColor(selItem ? COLOR_HIGHLIGHT : COLOR_WINDOW);
		COLORREF clrText = GetCustomSysColor(disabled ? COLOR_GRAYTEXT : selItem ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);

		// Paint item background
		dc->FillSolidRect(&dis->rcItem, clrBk);
		dc->SetBkMode(TRANSPARENT);
		dc->SetTextColor(clrText);

		// A) ComboBoxEx: Draw icon + text, preserving image list
		if (isEx) {
			int itemIdx = hasItem ? static_cast<int>(dis->itemID) : static_cast<int>(::SendMessage(hComboWnd, CB_GETCURSEL, 0, 0));

			COMBOBOXEXITEM cbei = {};
			TCHAR buf[260] = {};
			cbei.mask = CBEIF_TEXT | CBEIF_IMAGE | CBEIF_SELECTEDIMAGE;
			cbei.iItem = itemIdx;
			cbei.pszText = buf;
			cbei.cchTextMax = _countof(buf);
			::SendMessage(hComboWnd, CBEM_GETITEM, 0, reinterpret_cast<LPARAM>(&cbei));

			int imgIdx = selItem ? cbei.iSelectedImage : cbei.iImage;
			RECT rcText = dis->rcItem;
			const int ICON_SP = 2;

			// draw icon if available
			if (hImgs && imgIdx >= 0 && imgIdx != I_IMAGECALLBACK) {
				IMAGEINFO ii{};
				if (ImageList_GetImageInfo(hImgs, imgIdx, &ii)) {
					int iw = ii.rcImage.right - ii.rcImage.left;
					int ih = ii.rcImage.bottom - ii.rcImage.top;

					RECT rcIcon = dis->rcItem;
					rcIcon.right = rcIcon.left + iw;
					rcIcon.top += (rcIcon.bottom - rcIcon.top - ih) / 2;

					ImageList_Draw(hImgs, imgIdx, dis->hDC, rcIcon.left, rcIcon.top, ILD_NORMAL);
					rcText.left = rcIcon.right + ICON_SP; // Make room for text
				}
			} else
				rcText.left += 2; // Minimal indent when no icon

			dc->DrawText(buf, &rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			return TRUE;
		}

		// B) WC_COMBOBOX: Draw text only
		TCHAR buf[260] = {};
		if (hasItem)
			::SendMessage(hComboWnd, CB_GETLBTEXT, dis->itemID, reinterpret_cast<LPARAM>(buf));
		else {
			int cur = static_cast<int>(::SendMessage(hComboWnd, CB_GETCURSEL, 0, 0));
			if (cur >= 0)
				::SendMessage(hComboWnd, CB_GETLBTEXT, cur, reinterpret_cast<LPARAM>(buf));
		}

		RECT rcTxt = dis->rcItem;
		rcTxt.left += 2; // Small left margin
		dc->DrawText(buf, &rcTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		return TRUE;
	}
	case WM_NCDESTROY:
	{
		RemovePropW(hWnd, L"COMBO_HOT");
		RemovePropW(hWnd, L"COMBO_PRESSED");
		RemoveWindowSubclass(hWnd, ComboBoxSubclassProc, /*uIdSubclass=*/1); // Remove subclass when control is destroyed
		break;

	}
	}

	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Subclass proc for edit controls: Shows dark mode context menu
static LRESULT CALLBACK DarkEditSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
	if (!IsDarkModeEnabled())
		return DefSubclassProc(hWnd, uMsg, wParam, lParam);

	switch (uMsg) {
	case WM_RBUTTONUP:
	{
		// figure out screen coords
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		ClientToScreen(hWnd, &pt);

		CEdit* p = (CEdit*)CWnd::FromHandlePermanent(hWnd);
		if (!p) p = (CEdit*)CWnd::FromHandle(hWnd);

		CMenuXP menu;  // owner-draw menu
		menu.CreatePopupMenu();
		menu.AppendMenuW(MF_STRING | (p->CanUndo() ? 0 : MF_GRAYED), ID_EDIT_UNDO, L"Undo");
		menu.AppendMenuW(MF_SEPARATOR, 0);
		BOOL hasSel = p->GetSel() != 0;
		BOOL canPaste = ::IsClipboardFormatAvailable(CF_TEXT);
		menu.AppendMenuW(MF_STRING | (hasSel ? 0 : MF_GRAYED), ID_EDIT_CUT, L"Cut");
		menu.AppendMenuW(MF_STRING | (hasSel ? 0 : MF_GRAYED), ID_EDIT_COPY, L"Copy");
		menu.AppendMenuW(MF_STRING | (canPaste ? 0 : MF_GRAYED), ID_EDIT_PASTE, L"Paste");
		menu.AppendMenuW(MF_STRING | (hasSel ? 0 : MF_GRAYED), ID_EDIT_CLEAR, L"Clear");
		menu.AppendMenuW(MF_SEPARATOR, 0);
		menu.AppendMenuW(MF_STRING, ID_EDIT_SELECT_ALL, L"Select All");

		// show it via Win32 so we actually get the text drawn and the correct size
		int cmd = menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, AfxGetMainWnd());

		// handle the choice immediately
		switch (cmd) {
		case ID_EDIT_UNDO:       p->Undo();           break;
		case ID_EDIT_CUT:        p->Cut();            break;
		case ID_EDIT_COPY:       p->Copy();           break;
		case ID_EDIT_PASTE:      p->Paste();          break;
		case ID_EDIT_CLEAR:      p->Clear();          break;
		case ID_EDIT_SELECT_ALL: p->SetSel(0, -1);    break;
		default:                                      break;
		}

		return TRUE;  // eat it so default never shows
	}
	case WM_NCDESTROY:
		// Remove subclass when control is destroyed
		RemoveWindowSubclass(hWnd, DarkEditSubclassProc, /*uIdSubclass=*/1);
		break;
	}

	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Subclass proc for custom list view drawing in dark mode
LRESULT CALLBACK CustomListViewProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
	if (!IsDarkModeEnabled())
		return DefSubclassProc(hWnd, uMsg, wParam, lParam);

	switch (uMsg) {
	case WM_ERASEBKGND:
		return 1; // Prevent flickering by not erasing the background
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);

		// Set the default background color
		RECT rc;
		GetClientRect(hWnd, &rc);
		FillRect(hdc, &rc, CDarkMode::m_brDefault);

		// Save the current font
		HFONT hOriginalFont = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
		HFONT hOldFont = (HFONT)SelectObject(hdc, hOriginalFont);

		// Set default text color and mode
		SetBkMode(hdc, TRANSPARENT); // Transparent background for text

		// Get item count
		int itemCount = ListView_GetItemCount(hWnd);
		for (int i = 0; i < itemCount; ++i) {
			TCHAR szText[256] = { 0 }; // Buffer for text
			RECT itemRect = { 0 };

			// Check if the item is selected
			UINT itemState = ListView_GetItemState(hWnd, i, LVIS_SELECTED);

			// Get text of the item
			ListView_GetItemText(hWnd, i, 0, szText, ARRAYSIZE(szText));

			// Get bounds of the item
			if (ListView_GetItemRect(hWnd, i, &itemRect, LVIR_BOUNDS)) {
				if (itemState & LVIS_SELECTED)
					FillRect(hdc, &itemRect, CDarkMode::m_brActiveCaption); // Highlight background for selected items
				SetTextColor(hdc, GetCustomSysColor(COLOR_WINDOWTEXT));
				DrawText(hdc, szText, -1, &itemRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE); // Draw the text
			}
		}

		// Restore the original font
		SelectObject(hdc, hOldFont);
		EndPaint(hWnd, &ps);

		// Call default paint to draw items and subitems
		return DefSubclassProc(hWnd, uMsg, wParam, lParam);
	}
	case WM_NCDESTROY:
		// Remove subclass on window destruction
		RemoveWindowSubclass(hWnd, CustomListViewProc, /*uIdSubclass=*/1);
		break;
	}

	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Subclass proc for DateTimePicker controls: Customizes the appearance and selection behavior
LRESULT CALLBACK DateTimeSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
	if (!IsDarkModeEnabled())
		return DefSubclassProc(hWnd, uMsg, wParam, lParam);

	switch (uMsg)
	{
	case WM_ERASEBKGND:
	{
		HDC hdc = (HDC)wParam;
		RECT rc;
		GetClientRect(hWnd, &rc);
		FillRect(hdc, &rc, CDarkMode::m_brDefault);
		return 1;
	}
	case WM_LBUTTONDOWN:
	{
		// Allow default to determine which field (hour/minute) is active when spinner is clicked
		DefSubclassProc(hWnd, uMsg, wParam, lParam);

		RECT rcClient;
		GetClientRect(hWnd, &rcClient);
		const int spinWidth = 16;

		int clickX = GET_X_LPARAM(lParam);

		// If click is on spinner, do not override selection
		if (clickX >= rcClient.right - spinWidth)
			return 0;

		// Calculate custom selection in text area
		wchar_t buf[64] = {};
		SendMessage(hWnd, WM_GETTEXT, _countof(buf), (LPARAM)buf);
		int textLen = (int)wcslen(buf);

		int colonPos = -1;
		for (int i = 0; i < textLen; i++) {
			if (buf[i] == L':') {
				colonPos = i;
				break;
			}
		}

		// If no colon found, default to 0 (start of string)
		if (colonPos < 0)
			colonPos = 0;

		// Calculate text area for selection
		RECT rcTextArea = {
			rcClient.left + 2,
			rcClient.top + 2,
			rcClient.right - spinWidth - 2,
			rcClient.bottom - 2
		};

		int xOrigin = rcTextArea.left + 4;

		HDC hdc = GetWindowDC(hWnd);
		HFONT hFont = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
		HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);


		// Calculate width of the hour part before the colon
		SIZE sizeHour = { 0, 0 };
		if (colonPos > 0) {
			wchar_t temp[64] = {};
			wcsncpy_s(temp, buf, colonPos);
			GetTextExtentPoint32(hdc, temp, colonPos, &sizeHour);
		}

		SelectObject(hdc, hOldFont);
		ReleaseDC(hWnd, hdc);

		// Calculate relative click position in text area
		int relX = clickX - xOrigin;
		if (relX < 0)
			relX = 0;

		// Determine selection based on click position relative to the colon
		int selStart = 0;
		int selEnd = 0;
		if (relX <= sizeHour.cx) {
			selStart = 0;
			selEnd = colonPos;
		} else {
			selStart = colonPos + 1;
			selEnd = textLen;
		}

		// Store selection per-control using window properties
		SetProp(hWnd, L"DTPSelStart", (HANDLE)(INT_PTR)selStart);
		SetProp(hWnd, L"DTPSelEnd", (HANDLE)(INT_PTR)selEnd);

		// Apply selection to internal EDIT and set focus
		HWND hwndEdit = FindWindowEx(hWnd, nullptr, WC_EDIT, nullptr);
		if (hwndEdit) {
			SendMessage(hwndEdit, EM_SETSEL, (WPARAM)selStart, (LPARAM)selEnd);
			SetFocus(hwndEdit);
		}

		// Invalidate the text area to trigger repaint
		InvalidateRect(hWnd, &rcTextArea, TRUE);
		return 0;
	}
	case WM_PAINT:
	{
		// Always let default paint draw frame + spinner
		DefSubclassProc(hWnd, uMsg, wParam, lParam);

		HDC hdc = GetWindowDC(hWnd);
		RECT rcClient;
		GetClientRect(hWnd, &rcClient);

		const int spinWidth = 16;
		RECT rcTextArea = {	rcClient.left + 2, rcClient.top + 2, rcClient.right - spinWidth - 2, rcClient.bottom - 2 };

		// Fill the text area with the window background color
		FillRect(hdc, &rcTextArea, CDarkMode::m_brDefault);

		RECT rcText = rcTextArea;
		rcText.left += 4;

		HFONT hFont = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
		HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
		SetBkMode(hdc, TRANSPARENT);

		wchar_t buf[64] = {};
		SendMessage(hWnd, WM_GETTEXT, _countof(buf), (LPARAM)buf);
		int textLen = (int)wcslen(buf);

		// Retrieve per-control selection
		int selStart = (int)(INT_PTR)GetProp(hWnd, L"DTPSelStart");
		int selEnd = (int)(INT_PTR)GetProp(hWnd, L"DTPSelEnd");
		if (selStart < 0) selStart = 0;
		if (selEnd > textLen) selEnd = textLen;
		if (selStart > textLen) selStart = textLen;
		if (selEnd < 0) selEnd = 0;

		// Calculate width of the text before the selection
		SIZE sizeBefore = { 0, 0 }, sizeSel = { 0, 0 };
		if (selStart > 0) {
			wchar_t temp[64] = {};
			wcsncpy_s(temp, buf, selStart);
			GetTextExtentPoint32(hdc, temp, selStart, &sizeBefore);
		}
		int selCount = selEnd - selStart;
		if (selCount > 0) {
			wchar_t temp[64] = {};
			wcsncpy_s(temp, buf + selStart, selCount);
			GetTextExtentPoint32(hdc, temp, selCount, &sizeSel);
		}

		// Draw selection background
		int x = rcText.left;
		if (selCount > 0) {
			RECT rcSel = { x + sizeBefore.cx, rcText.top, x + sizeBefore.cx + sizeSel.cx, rcText.bottom };
			FillRect(hdc, &rcSel, CDarkMode::m_brHighlight);
		}

		// Draw text before selection
		if (selStart > 0) {
			SetTextColor(hdc, GetCustomSysColor(COLOR_WINDOWTEXT));
			RECT rcDraw = rcText;
			rcDraw.left = x;
			DrawTextW(hdc, buf, selStart, &rcDraw, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
			x += sizeBefore.cx;
		}

		// Draw selected text
		if (selCount > 0) {
			SetTextColor(hdc, GetCustomSysColor(COLOR_HIGHLIGHTTEXT));
			RECT rcDraw = rcText;
			rcDraw.left = x;
			DrawTextW(hdc, buf + selStart, selCount, &rcDraw, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
			x += sizeSel.cx;
		}

		// Draw remaining text after selection
		int remCount = textLen - selEnd;
		if (remCount > 0) {
			SetTextColor(hdc, GetCustomSysColor(COLOR_WINDOWTEXT));
			RECT rcDraw = rcText;
			rcDraw.left = x;
			DrawTextW(hdc, buf + selEnd, remCount, &rcDraw, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
		}

		SelectObject(hdc, hOldFont);
		ReleaseDC(hWnd, hdc);
		return 0;
	}
	case WM_NCDESTROY:
		// Remove subclass when control is destroyed
		RemoveWindowSubclass(hWnd, DateTimeSubclassProc, /*uIdSubclass=*/1);
		break;
	}

	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// Customizes the appearance of the spin controls
LRESULT CALLBACK SpinButtonSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,	UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
	if (!IsDarkModeEnabled())
		return DefSubclassProc(hWnd, uMsg, wParam, lParam);

	auto HitTestPart = [](const RECT& rc, POINT pt)->int {
		if (pt.x < (rc.left + rc.right) / 2) 
			return 1;
		if (pt.x > (rc.left + rc.right) / 2) 
			return 2;
		return 0;
	};
	auto GetPartProp = [&](LPCWSTR name)->int { return (int)(INT_PTR)GetPropW(hWnd, name); };
	auto SetPartProp = [&](LPCWSTR name, int v) {
		if (v)
			SetPropW(hWnd, name, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(v)));
		else
			RemovePropW(hWnd, name);
	};

	switch (uMsg) {
	case WM_ERASEBKGND:
	{
		RECT rc; GetClientRect(hWnd, &rc);
		FillRect(reinterpret_cast<HDC>(wParam), &rc, CDarkMode::m_brDefault);
		return 1;
	}
	case WM_MOUSEMOVE:
	{
		POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		RECT rc; GetClientRect(hWnd, &rc); InflateRect(&rc, -2, 0);     // account for shrink
		int part = HitTestPart(rc, pt);
		if (part != GetPartProp(L"SPIN_HOT_PART")) {
			SetPartProp(L"SPIN_HOT_PART", part);
			InvalidateRect(hWnd, nullptr, TRUE);
		}
		TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hWnd, 0 };
		TrackMouseEvent(&tme);
		break;

	}
	case WM_MOUSELEAVE:
	{
		SetPartProp(L"SPIN_HOT_PART", 0);
		SetPartProp(L"SPIN_PRESSED_PART", 0);
		InvalidateRect(hWnd, nullptr, TRUE);
		break;
	}
	case WM_LBUTTONDOWN:
	{
		POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		RECT rc; GetClientRect(hWnd, &rc); InflateRect(&rc, -2, 0);
		int part = HitTestPart(rc, pt);
		if (part) {
			SetPartProp(L"SPIN_PRESSED_PART", part);
			SetCapture(hWnd);
			InvalidateRect(hWnd, nullptr, TRUE);
		}
		break;
	}
	case WM_LBUTTONUP:
	{
		SetPartProp(L"SPIN_PRESSED_PART", 0);
		ReleaseCapture();
		InvalidateRect(hWnd, nullptr, TRUE);
		break;

	}			
	case WM_PAINT:
	{
		const int hotPart = GetPartProp(L"SPIN_HOT_PART");
		const int pressedPart = GetPartProp(L"SPIN_PRESSED_PART");

		PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
		RECT rcAll; GetClientRect(hWnd, &rcAll);
		InflateRect(&rcAll, -2, 0); // total width -4px

		FrameRect(hdc, &rcAll, CDarkMode::m_brBtnBorder);
		InflateRect(&rcAll, -1, -1);

		RECT rcLeft = rcAll, rcRight = rcAll;
		int midX = rcAll.left + (rcAll.right - rcAll.left) / 2;
		rcLeft.right = midX;
		rcRight.left = midX + 1;

		FillRect(hdc, &rcLeft, (pressedPart == 1) ? CDarkMode::m_brBtnPressed : (hotPart == 1) ? CDarkMode::m_brBtnHover : CDarkMode::m_brBtn);
		FillRect(hdc, &rcRight,	(pressedPart == 2) ? CDarkMode::m_brBtnPressed : (hotPart == 2) ? CDarkMode::m_brBtnHover :	CDarkMode::m_brBtn);

		RECT rcSep{ midX, rcAll.top, midX + 1, rcAll.bottom };
		FillRect(hdc, &rcSep, CDarkMode::m_brBtnBorder);

		int h = rcAll.bottom - rcAll.top;
		int arrow = max(3, h / 4);
		int cy = rcAll.top + h / 2;

		HGDIOBJ oldBr = SelectObject(hdc, CDarkMode::m_brText);
		SetPolyFillMode(hdc, WINDING);

		int cxLeft = rcLeft.left + (rcLeft.right - rcLeft.left) / 2;
		POINT leftTri[3] = { { cxLeft - arrow, cy }, { cxLeft + arrow, cy - arrow }, { cxLeft + arrow, cy + arrow }	};
		Polygon(hdc, leftTri, 3);

		int cxRight = rcRight.left + (rcRight.right - rcRight.left) / 2;
		POINT rightTri[3] = { { cxRight + arrow, cy }, { cxRight - arrow, cy - arrow },	{ cxRight - arrow, cy + arrow }	};
		Polygon(hdc, rightTri, 3);

		SelectObject(hdc, oldBr);
		EndPaint(hWnd, &ps);
		return 0;
	}
	case WM_NCDESTROY:
	{
		RemovePropW(hWnd, L"SPIN_HOT_PART");
		RemovePropW(hWnd, L"SPIN_PRESSED_PART");
		RemoveWindowSubclass(hWnd, SpinButtonSubclassProc, 1);
		break;
	}
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK HeaderSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
	switch (uMsg) {
	case HDM_SETITEMA:
	case HDM_SETITEMW:
	{
		LRESULT lr = DefSubclassProc(hWnd, uMsg, wParam, lParam);
		const HDITEM* pItem = reinterpret_cast<const HDITEM*>(lParam);
		if (pItem && (pItem->mask & HDI_FORMAT)) {
			const int iItem = static_cast<int>(wParam);
			if (pItem->fmt & HDF_SORTUP) {
				SetProp(hWnd, kBBPropHdrSortCol, (HANDLE)(INT_PTR)iItem);
				SetProp(hWnd, kBBPropHdrSortDir, (HANDLE)(INT_PTR)1);
				RECT rc{}; Header_GetItemRect(hWnd, iItem, &rc); InvalidateRect(hWnd, &rc, FALSE);
			} else if (pItem->fmt & HDF_SORTDOWN) {
				SetProp(hWnd, kBBPropHdrSortCol, (HANDLE)(INT_PTR)iItem);
				SetProp(hWnd, kBBPropHdrSortDir, (HANDLE)(INT_PTR)0);
				RECT rc{}; Header_GetItemRect(hWnd, iItem, &rc); InvalidateRect(hWnd, &rc, FALSE);
			} else {
				int cur = (int)(INT_PTR)GetProp(hWnd, kBBPropHdrSortCol);
				if (cur == iItem) {
					SetProp(hWnd, kBBPropHdrSortCol, (HANDLE)(INT_PTR)-1);
					RECT rc{}; Header_GetItemRect(hWnd, iItem, &rc); InvalidateRect(hWnd, &rc, FALSE);
				}
			}
		}
		return lr;
	}

	case WM_PAINT:
		{
			if (!IsDarkModeEnabled())
				break;

			PAINTSTRUCT ps{};
			HDC hdc = BeginPaint(hWnd, &ps);
			if (hdc) {
				RECT rcHeader{}; GetClientRect(hWnd, &rcHeader);
				HBRUSH br = CreateSolidBrush(GetCustomSysColor(COLOR_WINDOW));
				FillRect(hdc, &rcHeader, br);
				DeleteObject(br);

				HFONT hFont = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
				HFONT hOldFont = hFont ? (HFONT)SelectObject(hdc, hFont) : nullptr;
				const int count = Header_GetItemCount(hWnd);

				const int  hover   = (int)(INT_PTR)GetProp(hWnd, kBBPropHdrHover);
				int        sortCol = (int)(INT_PTR)GetProp(hWnd, kBBPropHdrSortCol);
				int        sortDir = (int)(INT_PTR)GetProp(hWnd, kBBPropHdrSortDir);

				if (sortCol == 0 && GetProp(hWnd, kBBPropHdrSortCol) == nullptr)
					sortCol = -1;

				if (sortCol < 0) {
					for (int i = 0; i < count; ++i) {
						HDITEM tmp{}; tmp.mask = HDI_FORMAT;
						if (Header_GetItem(hWnd, i, &tmp)) {
							if (tmp.fmt & HDF_SORTUP)  { sortCol = i; sortDir = 1; break; }
							if (tmp.fmt & HDF_SORTDOWN){ sortCol = i; sortDir = 0; break; }
						}
					}

					SetProp(hWnd, kBBPropHdrSortCol, (HANDLE)(INT_PTR)sortCol);
					SetProp(hWnd, kBBPropHdrSortDir, (HANDLE)(INT_PTR)sortDir);
				}

				HIMAGELIST himl = (HIMAGELIST)SendMessage(hWnd, HDM_GETIMAGELIST, 0, 0);

				for (int i = 0; i < count; ++i) {
					RECT rcItem{}; Header_GetItemRect(hWnd, i, &rcItem);

					HDITEM hdi{}; 
					TCHAR textBuf[256] = {0};
					hdi.mask = HDI_TEXT | HDI_FORMAT | HDI_IMAGE;
					hdi.pszText = textBuf;
					hdi.cchTextMax = _countof(textBuf);
					Header_GetItem(hWnd, i, &hdi);

					const bool isHot    = (i == hover);
					const bool isSorted = (i == sortCol);

					const bool hasUp    = isSorted ? (sortDir != 0) : ((hdi.fmt & HDF_SORTUP)   != 0);
					const bool hasDown  = isSorted ? (sortDir == 0) : ((hdi.fmt & HDF_SORTDOWN) != 0);
					const bool hasSortFlagArrow = hasUp || hasDown;

					const bool hasImgFlag = (hdi.fmt & HDF_IMAGE) != 0;
					const bool hasImage   = hasImgFlag && himl != nullptr && hdi.iImage >= 0;
					const bool usesImageRight = hasImage && (hdi.fmt & HDF_BITMAP_ON_RIGHT) != 0;
					const bool hasImageLeft   = hasImage && !usesImageRight;
					int imgW = 0, imgH = 0;

					if (hasImage)
						ImageList_GetIconSize(himl, &imgW, &imgH);

					const int paddingX = 6;
					const int gapArrow = 4;

					COLORREF clrText = isHot ? GetCustomSysColor(COLOR_HIGHLIGHTTEXT) : GetCustomSysColor(COLOR_WINDOWTEXT);
					COLORREF bg = isHot ? GetCustomSysColor(COLOR_ACTIVECAPTION) : GetCustomSysColor(COLOR_WINDOW);

					HBRUSH brItem = CreateSolidBrush(bg);
					FillRect(hdc, &rcItem, brItem);
					DeleteObject(brItem);

					RECT rcText = rcItem;

					if (hasImageLeft)
						rcText.left += paddingX + imgW + 4; // Leave room for a decorative left image if any.
					else
						rcText.left += paddingX;

					UINT dt = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
					if (hdi.fmt & HDF_CENTER) 
						dt |= DT_CENTER;
					else if (hdi.fmt & HDF_RIGHT)
						dt |= DT_RIGHT;
					else
						dt |= DT_LEFT;

					// Reserve space for arrow only when it will sit to the right of the text (right/center align),
					// so we avoid overlap like in the screenshots. For left-align, we will clamp on paint.
					int arrowW = (usesImageRight ? imgW : 10);
					if (usesImageRight || hasSortFlagArrow) {
						if (dt & (DT_RIGHT | DT_CENTER)) rcText.right -= (paddingX + arrowW + gapArrow);
						else rcText.right -= paddingX;
					} else
						rcText.right -= paddingX;

					SetBkMode(hdc, TRANSPARENT);
					SetTextColor(hdc, clrText);
					DrawText(hdc, textBuf, (int)wcslen(textBuf), &rcText, dt);

					// Draw any left-side decorative image (not the sort indicator).
					if (hasImageLeft) {
						int yImg = rcItem.top + ((rcItem.bottom - rcItem.top) - imgH) / 2;
						int xImg = rcItem.left + paddingX;
						ImageList_Draw(himl, hdi.iImage, hdc, xImg, yImg, ILD_NORMAL);
					}

					
					SIZE sz{}; // Compute rendered text bounds to place arrow just after the text.
					GetTextExtentPoint32(hdc, textBuf, (int)wcslen(textBuf), &sz);

					int availW = max(0, rcText.right - rcText.left);
					int wUsed  = min(sz.cx, availW);
					int textLeft = rcText.left;

					if (dt & DT_CENTER)
						textLeft = rcText.left + (availW - wUsed) / 2;
					else if (dt & DT_RIGHT)
						textLeft = rcText.right - wUsed; // Right aligned inside rcText

					int textRight = textLeft + wUsed;

					// Draw arrow near text: use header image if header uses right-side image for sort,
					// otherwise draw a small triangle.
					if (usesImageRight || hasSortFlagArrow) {
						int cx = textRight + gapArrow;
						int rightClamp = rcItem.right - paddingX - (usesImageRight ? imgW : 6);

						if (cx > rightClamp)
							cx = rightClamp;

						int cy = (rcItem.top + rcItem.bottom) / 2;

						if (usesImageRight) {
							int yImg = rcItem.top + ((rcItem.bottom - rcItem.top) - imgH) / 2;
							ImageList_Draw(himl, hdi.iImage, hdc, cx, yImg, ILD_NORMAL);
						} else {
							POINT tri[3];
							HPEN hPen = CreatePen(PS_SOLID, 1, clrText);
							HBRUSH hBr = CreateSolidBrush(clrText);
							HGDIOBJ oldPen = SelectObject(hdc, hPen);
							HGDIOBJ oldBr  = SelectObject(hdc, hBr);
							if (hasUp) {
								tri[0] = { cx - 5, cy + 2 };
								tri[1] = { cx + 5, cy + 2 };
								tri[2] = { cx,     cy - 3 };
								Polygon(hdc, tri, 3);
							} else if (hasDown) {
								tri[0] = { cx - 5, cy - 2 };
								tri[1] = { cx + 5, cy - 2 };
								tri[2] = { cx,     cy + 3 };
								Polygon(hdc, tri, 3);
							}
							SelectObject(hdc, oldPen);
							SelectObject(hdc, oldBr);
							DeleteObject(hPen);
							DeleteObject(hBr);
						}
					}

					RECT rcSep{ rcItem.right - 2, rcItem.top, rcItem.right - 1, rcItem.bottom };
					HBRUSH brSep = CreateSolidBrush(GetCustomSysColor(COLOR_3DSHADOW));
					FillRect(hdc, &rcSep, brSep);
					DeleteObject(brSep);
				}

				if (hOldFont)
					SelectObject(hdc, hOldFont);

				EndPaint(hWnd, &ps);
				return 0;
			}
		}
		break;

	case WM_MOUSEMOVE:
		{
			TRACKMOUSEEVENT tme{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0 };
			TrackMouseEvent(&tme);

			POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			HDHITTESTINFO hti{}; hti.pt = pt;
			int hit = (int)SendMessage(hWnd, HDM_HITTEST, 0, (LPARAM)&hti);
			int prev = (int)(INT_PTR)GetProp(hWnd, kBBPropHdrHover);
			int now = ((hti.flags & HHT_ONHEADER) && hit >= 0) ? hit : -1;

			if (now != prev) {
				if (prev >= 0) {
					RECT rcPrev{};
					Header_GetItemRect(hWnd, prev, &rcPrev);
					InvalidateRect(hWnd, &rcPrev, FALSE);
				}

				if (now  >= 0) {
					RECT rcNow{};
					Header_GetItemRect(hWnd, now,  &rcNow);
					InvalidateRect(hWnd, &rcNow,  FALSE);
				}

				SetProp(hWnd, kBBPropHdrHover, (HANDLE)(INT_PTR)now);
			}
		}
		break;

	case WM_MOUSELEAVE:
		{
			int prev = (int)(INT_PTR)GetProp(hWnd, kBBPropHdrHover);
			if (prev >= 0) {
				RECT rcPrev{};
				Header_GetItemRect(hWnd, prev, &rcPrev);
				InvalidateRect(hWnd, &rcPrev, FALSE);
			}
			SetProp(hWnd, kBBPropHdrHover, (HANDLE)(INT_PTR)-1);
		}
		break;

	case WM_NCDESTROY:
		RemoveWindowSubclass(hWnd, HeaderSubclassProc, /*uIdSubclass=*/1);
		RemoveProp(hWnd, kBBPropDarkHdr);
		RemoveProp(hWnd, kBBPropHdrHover);
		RemoveProp(hWnd, kBBPropHdrSortCol);
		RemoveProp(hWnd, kBBPropHdrSortDir);
		break;
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

// ---------------------------------------------------------------------------
//  CDarkMode
// ---------------------------------------------------------------------------
CBrush CDarkMode::m_brDefault;
CBrush CDarkMode::m_brText;
CBrush CDarkMode::m_brHighlight;
CBrush CDarkMode::m_brBtn;
CBrush CDarkMode::m_brBtnBorder;
CBrush CDarkMode::m_brBtnBorderDisabled;
CBrush CDarkMode::m_brBtnHover;
CBrush CDarkMode::m_brBtnPressed;
CBrush CDarkMode::m_brScrollbar;
CBrush CDarkMode::m_brActiveCaption;
CBrush CDarkMode::m_brInactiveCaption;
CBrush CDarkMode::s_brHelpTextBackground;
CBrush CDarkMode::m_brDefaultLight;

void CDarkMode::Initialize()
{
	if (m_brDefault.GetSafeHandle() != nullptr)
		return;

	m_brDefault.CreateSolidBrush(GetCustomSysColor(COLOR_WINDOW, true));
	m_brText.CreateSolidBrush(GetCustomSysColor(COLOR_WINDOWTEXT, true));
	m_brHighlight.CreateSolidBrush(GetCustomSysColor(COLOR_HIGHLIGHT, true));
	m_brBtn.CreateSolidBrush(GetCustomSysColor(COLOR_BTNFACE, true));
	m_brBtnBorder.CreateSolidBrush(GetCustomSysColor(COLOR_GRAYTEXT, true));
	m_brBtnBorderDisabled.CreateSolidBrush(GetCustomSysColor(COLOR_INACTIVEBORDER, true));
	m_brBtnHover.CreateSolidBrush(GetCustomSysColor(COLOR_BTNHIGHLIGHT, true));
	m_brBtnPressed.CreateSolidBrush(GetCustomSysColor(COLOR_3DLIGHT, true));
	m_brScrollbar.CreateSolidBrush(GetCustomSysColor(COLOR_SCROLLBAR, true));
	m_brActiveCaption.CreateSolidBrush(GetCustomSysColor(COLOR_ACTIVECAPTION, true));
	m_brInactiveCaption.CreateSolidBrush(GetCustomSysColor(COLOR_INACTIVECAPTION, true));
	s_brHelpTextBackground.CreateSolidBrush(GetCustomSysColor(COLOR_HELP_TEXT_BACKGROUND, true));
	m_brDefaultLight.CreateSolidBrush(GetSysColor(COLOR_WINDOW));
}

void CDarkMode::OnDestroy()
{
	m_brDefault.DeleteObject();
	m_brText.DeleteObject();
	m_brHighlight.DeleteObject();
	m_brBtn.DeleteObject();
	m_brBtnBorder.DeleteObject();
	m_brBtnBorderDisabled.DeleteObject();
	m_brBtnHover.DeleteObject();
	m_brBtnPressed.DeleteObject();
	m_brScrollbar.DeleteObject();
	m_brActiveCaption.DeleteObject();
	m_brInactiveCaption.DeleteObject();
	s_brHelpTextBackground.DeleteObject();
	m_brDefaultLight.DeleteObject();
}

HICON CDarkMode::GetCustomSysIcon(UINT iconMask)
{
	// return 3232 system icon based on MB_ICONMASK
	switch (iconMask)
	{
	case MB_ICONERROR:       return ::LoadIcon(nullptr, IDI_ERROR);
	case MB_ICONQUESTION:    return ::LoadIcon(nullptr, IDI_QUESTION);
	case MB_ICONWARNING:     return ::LoadIcon(nullptr, IDI_WARNING);
	case MB_ICONINFORMATION:
	default:                 return ::LoadIcon(nullptr, IDI_INFORMATION);
	}
}

PIDLIST_ABSOLUTE CDarkMode::BrowseForFolder(BROWSEINFO& bi)
{
	if (IsDarkModeEnabled()) {
		bi.ulFlags |= BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
		bi.lpfn = BrowseCallbackProc;
		bi.lParam = 0;
	}
	return SHBrowseForFolder(&bi);
}

int CALLBACK CDarkMode::BrowseCallbackProc(HWND hWnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
	if (!IsDarkModeEnabled())
		return 0;

	switch (uMsg)
	{
	case BFFM_INITIALIZED:
	{
		SetWindowSubclass(hWnd, FolderDlgSubclassProc, /*uIdSubclass=*/1,/*dwRefData=*/0);
		SubclassFolderDialogChildren(hWnd);
		ApplyTheme(hWnd);
		break;
	}
	case WM_NCDESTROY:
		// Remove subclass when control is destroyed
		RemoveWindowSubclass(hWnd, FolderDlgSubclassProc, /*uIdSubclass=*/1);
		break;
	}

	return 0;
}

LRESULT CALLBACK CDarkMode::FolderDlgSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
	if (!IsDarkModeEnabled())
		return DefSubclassProc(hWnd, uMsg, wParam, lParam);

	switch (uMsg) {
	case WM_CTLCOLORDLG:
		return reinterpret_cast<LRESULT>(m_brDefault.GetSafeHandle());
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT: {
		HDC hdc = reinterpret_cast<HDC>(wParam);
		SetBkMode(hdc, TRANSPARENT);
		SetTextColor(hdc, GetCustomSysColor(COLOR_WINDOWTEXT));
		return reinterpret_cast<LRESULT>(m_brDefault.GetSafeHandle());
	}
	case WM_NCDESTROY:
		RemoveWindowSubclass(hWnd, FolderDlgSubclassProc, /*uIdSubclass=*/1);
		break;
	}

	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK CDarkMode::FolderChildSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubclass*/, DWORD_PTR /*dwRefData*/)
{
	if (!IsDarkModeEnabled())
		return DefSubclassProc(hWnd, uMsg, wParam, lParam);

	switch (uMsg) {
	case WM_ERASEBKGND:
	{
		HDC hdc = reinterpret_cast<HDC>(wParam);
		RECT rc;
		GetClientRect(hWnd, &rc);
		FillRect(hdc, &rc, static_cast<HBRUSH>(m_brDefault.GetSafeHandle()));
		return 1;
	}
	case WM_NCDESTROY:
		// Remove subclass when control is destroyed
		RemoveWindowSubclass(hWnd, FolderChildSubclassProc, /*uIdSubclass=*/1);
		break;
	}

	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void CDarkMode::SubclassFolderDialogChildren(HWND hWnd)
{
    EnumChildWindows(hWnd, [](HWND child, LPARAM) -> BOOL {
        wchar_t clsName[256];
        if (GetClassNameW(child, clsName, _countof(clsName))) {
            if (wcscmp(clsName, L"SysTreeView32") == 0) {
                TreeView_SetBkColor(child, GetCustomSysColor(COLOR_WINDOW));
                TreeView_SetTextColor(child, GetCustomSysColor(COLOR_WINDOWTEXT));
            } else if (wcscmp(clsName, WC_EDIT) == 0)
                SetWindowSubclass(child, FolderChildSubclassProc, /*uIdSubclass=*/1,/*dwRefData=*/0);
        }
        return TRUE;
        }, 0);
}

int CDarkMode::MessageBox(LPCTSTR lpszText, UINT nType, UINT nHelpId)
{
	return MessageBoxWithCaption(lpszText, NULL, nType, nHelpId);
}

int CDarkMode::MessageBoxWithCaption(LPCTSTR lpszText, LPCTSTR lpszCaption, UINT nType, UINT nHelpId)
{
	// if system in light mode, use standard MessageBox with custom caption
	if (!IsDarkModeEnabled()) {
		CWnd* pMainWnd = AfxGetMainWnd();
		CString caption((lpszCaption != NULL && lpszCaption[0] != _T('\0')) ? lpszCaption : AfxGetAppName());
		return ::MessageBox(pMainWnd ? pMainWnd->GetSafeHwnd() : NULL, lpszText, caption, nType);
	}

	// dark mode
	CDarkModeMessageBoxDlg dlg(lpszText, nType, lpszCaption);
	return dlg.DoModal();
}

int CDarkMode::MessageBox(UINT nResID, UINT nType, UINT /*nHelpId*/)
{
	// if system in light mode, use original
	if (!IsDarkModeEnabled())
		return AfxMessageBox(nResID, nType);

	// load string resource and show themed dialog
	CString text;
	BOOL ok = text.LoadString(nResID);
	if (!ok)
		return AfxMessageBox(nResID, nType);

	CDarkModeMessageBoxDlg dlg(text, nType, NULL);
	return dlg.DoModal();
}

// ---------------------------------------------------------------------------
//  CDarkModeMessageBoxDlg
// ---------------------------------------------------------------------------
BEGIN_MESSAGE_MAP(CDarkModeMessageBoxDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_DRAWITEM()
END_MESSAGE_MAP()

struct BtnDef {UINT mbFlag;
	UINT id;
	LPCTSTR strResID;
};

static const BtnDef g_btns[] = {
	{ MB_OK,                IDOK,        _T("MB_OK")       },
	{ MB_OKCANCEL,          IDOK,        _T("MB_OK")       },
	{ MB_OKCANCEL,          IDCANCEL,    _T("CANCEL")      },
	{ MB_YESNOCANCEL,       IDYES,       _T("YES")         },
	{ MB_YESNOCANCEL,       IDNO,        _T("NO")          },
	{ MB_YESNOCANCEL,       IDCANCEL,    _T("CANCEL")      },
	{ MB_YESNO,             IDYES,       _T("YES")         },
	{ MB_YESNO,             IDNO,        _T("NO")          },
	{ MB_RETRYCANCEL,       IDRETRY,     _T("MB_RETRY")    },
	{ MB_RETRYCANCEL,       IDCANCEL,    _T("CANCEL")      },
	{ MB_ABORTRETRYIGNORE,  IDABORT,     _T("MB_ABORT")    },
	{ MB_ABORTRETRYIGNORE,  IDRETRY,     _T("MB_RETRY")    },
	{ MB_ABORTRETRYIGNORE,  IDIGNORE,    _T("MB_IGNORE")   },
	{ MB_CANCELTRYCONTINUE, IDCANCEL,    _T("CANCEL")      },
	{ MB_CANCELTRYCONTINUE, IDTRYAGAIN,  _T("MB_TRYAGAIN") },
	{ MB_CANCELTRYCONTINUE, IDCONTINUE,  _T("MB_CONTINUE") }
};

CDarkModeMessageBoxDlg::CDarkModeMessageBoxDlg(LPCTSTR text, UINT type, LPCTSTR caption, CWnd* pParent)
	: CDialog(IDD, pParent)
	, m_text(text)
	, m_caption(caption != NULL ? caption : _T(""))
	, m_type(type)
	, m_hIcon(nullptr)
{
}

BOOL CDarkModeMessageBoxDlg::OnInitDialog()
{
	CDialog::OnInitDialog();    // call base implementation

	m_bTrackingMouse = false;
	m_uHoverBtnId = 0;

	if (CWnd* pTxt = GetDlgItem(IDC_DARKMODE_TEXT))
		// enable char-level wrapping, remove ellipsis & no-wrap
		pTxt->ModifyStyle(SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS | SS_PATHELLIPSIS | SS_WORDELLIPSIS, SS_EDITCONTROL);

	// Set static control text before layout
	SetDlgItemText(IDC_DARKMODE_TEXT, m_text);
	SetWindowText(m_caption.IsEmpty() ? AfxGetAppName() : m_caption);
	HICON hIcon = CDarkMode::GetCustomSysIcon(m_type & MB_ICONMASK);

	if (HICON hIcon = CDarkMode::GetCustomSysIcon(m_type & MB_ICONMASK))
		if (CWnd* pIcon = GetDlgItem(IDC_DARKMODE_ICON))
			pIcon->SendMessage(STM_SETICON, (WPARAM)hIcon, 0);

	ResizeToFitText();
	CreateButtons();
	ApplyTheme(m_hWnd);
	return TRUE;
}

BOOL CDarkModeMessageBoxDlg::OnCommand(WPARAM wParam, LPARAM lParam)
{
	UINT id = LOWORD(wParam);
	UINT code = HIWORD(wParam);
	if (code == BN_CLICKED)
	{
		switch (id)
		{
		case IDOK: case IDCANCEL:
		case IDYES: case IDNO:
		case IDRETRY: case IDABORT: case IDIGNORE:
			EndDialog(id);
			return TRUE;
		}
	}
	return CDialog::OnCommand(wParam, lParam);
}

void CDarkModeMessageBoxDlg::OnPaint()
{
	CPaintDC dc(this);
	auto dlg = theApp.emuledlg;
	HBRUSH br = (HBRUSH)CDarkMode::m_brDefault.GetSafeHandle();
	CRect rc; GetClientRect(&rc);
	::FillRect(dc.m_hDC, &rc, br);
}

void CDarkModeMessageBoxDlg::OnDrawItem(int /*nIDCtl*/, LPDRAWITEMSTRUCT lpDIS)
{
	if (lpDIS->CtlType != ODT_BUTTON)
		return;

	bool isHot = (lpDIS->itemState & ODS_HOTLIGHT) != 0;
	HBRUSH hbrBg = isHot ? static_cast<HBRUSH>(CDarkMode::m_brActiveCaption.GetSafeHandle()) : static_cast<HBRUSH>(CDarkMode::m_brBtn.GetSafeHandle());

	// draw background
	FillRect(lpDIS->hDC, &lpDIS->rcItem, hbrBg);

	// draw border edge
	RECT rcEdge = lpDIS->rcItem;
	UINT edgeStyle = isHot ? EDGE_SUNKEN : EDGE_RAISED;
	DrawEdge(lpDIS->hDC, &rcEdge, edgeStyle, BF_RECT);

	// draw button text
	wchar_t buf[64] = {};
	::GetWindowTextW(lpDIS->hwndItem, buf, _countof(buf));
	SetBkMode(lpDIS->hDC, TRANSPARENT);
	SetTextColor(lpDIS->hDC, GetCustomSysColor(COLOR_BTNTEXT, true));
	DrawTextW(lpDIS->hDC, buf, -1, &lpDIS->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/*  Builds the final layout for the custom dark-mode message-box.
	 Ensures word-wrapped text never touches the icon.
	 Dialog width is limited to 70 % of the work area or 600 px.
	 All constants are pixel values; tweak kIconTextGap to widen the icon-text spacing. */
void CDarkModeMessageBoxDlg::ResizeToFitText()
{
	CWnd* pText = GetDlgItem(IDC_DARKMODE_TEXT);
	CWnd* pIcon = GetDlgItem(IDC_DARKMODE_ICON);
	if (!pText) return;                         // safety guard

	// allow word-wrap, block any kind of ellipsis
	pText->ModifyStyle(SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS |
		SS_PATHELLIPSIS | SS_WORDELLIPSIS, 0);

	CRect txtInit, dlgRect, clientRect;
	pText->GetWindowRect(&txtInit);
	GetWindowRect(&dlgRect);
	ScreenToClient(&txtInit);
	GetClientRect(&clientRect);

	// Layout constants
	const int marginTop = 25;
	const int marginRight = 20;
	const int gapTextBtn = 14;
	const int marginBottom = 12;
	const int btnHeight = 24;
	const int iconTextGap = 10;  // Base gap icon text
	const int extraTextShift = 15;  // Cumulative requested shift (+5 px more)

	// Determine where text starts
	int textLeft = txtInit.left + iconTextGap; // Default when no icon
	if (pIcon && pIcon->IsWindowVisible()) {
		CRect iconRect; pIcon->GetWindowRect(&iconRect); ScreenToClient(&iconRect);
		pIcon->MoveWindow(iconRect.left, marginTop, iconRect.Width(), iconRect.Height());
		textLeft = iconRect.right + iconTextGap + extraTextShift; // Shift text right if icon is present
	}

	// Limit dialog width
	int workW = GetSystemMetrics(SM_CXFULLSCREEN);
	int maxDlgW = std::min<int>((workW * 70) / 100, 600);

	// Measure text with current font
	CDC* dc = pText->GetWindowDC();
	CFont* oldF = dc->SelectObject(pText->GetFont());

	CRect natural(0, 0, 0, 0);
	dc->DrawText(m_text, &natural, DT_CALCRECT | DT_NOPREFIX);

	int maxTextW = maxDlgW - textLeft - marginRight;
	int textW = std::min<int>(natural.Width(), maxTextW);

	CRect calc(0, 0, textW, 0);
	dc->DrawText(m_text, &calc, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);

	dc->SelectObject(oldF);
	ReleaseDC(dc);

	// Resize dialog & position text
	int desiredClientW = textLeft + textW + marginRight;
	int desiredClientH = marginTop + calc.Height() + gapTextBtn + btnHeight + marginBottom;

	int ncW = dlgRect.Width() - clientRect.Width(); // System borders
	int ncH = dlgRect.Height() - clientRect.Height();

	MoveWindow(dlgRect.left, dlgRect.top, desiredClientW + ncW, desiredClientH + ncH);

	CRect textRect;
	textRect.left = textLeft;
	textRect.top = marginTop;
	textRect.right = textLeft + textW;
	textRect.bottom = textRect.top + calc.Height();
	pText->MoveWindow(textRect);
}

void CDarkModeMessageBoxDlg::CreateButtons()
{
	const int btnW = 60;
	const int btnH = 22;
	const int spacing = 8;

	CRect txtRect, clientRect;
	GetClientRect(&clientRect);
	if (CWnd* pText = GetDlgItem(IDC_DARKMODE_TEXT)) {
		pText->GetWindowRect(&txtRect);
		ScreenToClient(&txtRect);
	}
	int y = txtRect.bottom + 18;

	UINT typeOnly = m_type & MB_TYPEMASK;

	// hide every BUTTON child which exists in the template
	for (HWND hChild = ::GetWindow(m_hWnd, GW_CHILD); hChild; hChild = ::GetWindow(hChild, GW_HWNDNEXT)) {
		wchar_t cls[12] = {};
		::GetClassNameW(hChild, cls, _countof(cls));
		if (wcscmp(cls, WC_BUTTON) == 0)
			::ShowWindow(hChild, SW_HIDE);
	}

	CArray<HWND> buttons;
	for (const BtnDef& bd : g_btns) {
		if (bd.mbFlag != typeOnly)
			continue;

		CString text = GetResString(bd.strResID);
		HWND hBtn = ::CreateWindowExW(0, WC_BUTTON, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(bd.id), AfxGetInstanceHandle(), nullptr);

		if (hBtn) {
			::SendMessageW(hBtn, WM_SETFONT, ::SendMessageW(m_hWnd, WM_GETFONT, 0, 0), FALSE); // make sure the button uses the same font as the dialog
			buttons.Add(hBtn);
		}
	}

	int count = buttons.GetCount();
	if (count == 0)
		return;

	int totalW = count * btnW + (count - 1) * spacing;
	int x0 = (clientRect.Width() - totalW) / 2;
	for (int i = 0; i < count; ++i)
		::SetWindowPos(buttons[i], nullptr, x0 + i * (btnW + spacing), y, btnW, btnH, SWP_NOZORDER);
}
