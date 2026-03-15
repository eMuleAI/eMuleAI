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
#include "MuleToolbarCtrl.h"
#include "SearchDlg.h"
#include "OtherFunctions.h"
#include "KademliaWnd.h"
#include "EnBitmap.h"
#include "OtherFunctions.h"
#include "ServerConnect.h"
#include "MenuCmds.h"
#include "MuleStatusbarCtrl.h"
#include "ServerWnd.h"
#include "TransferDlg.h"
#include "SharedFilesWnd.h"
#include "ChatWnd.h"
#include "IrcWnd.h"
#include "StatisticsDlg.h"
#include "emuledlg.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define	OLD_NUM_BUTTON_BITMAPS	14
#define	PREV_NUM_BUTTON_BITMAPS	18
#define	NUM_BUTTON_BITMAPS	17

#define	EMULTB_BASEEXT		_T("eMuleToolbar.kad02")

static LPCTSTR const s_apszTBFiles[] =
{
	_T("*.") EMULTB_BASEEXT _T(".bmp"),
	_T("*.") EMULTB_BASEEXT _T(".gif"),
	_T("*.") EMULTB_BASEEXT _T(".png")
};

static LPCTSTR const s_apszSkinFiles[] =
{
	_T("*.") EMULSKIN_BASEEXT _T(".ini")
};

static const LPCTSTR TBStringIDs[] =
{
	_T("EM_KADEMLIA"),
	_T("EM_SERVER"),
	_T("EM_TRANS"),
	_T("EM_SEARCH"),
	_T("EM_FILES2"),
	_T("EM_MESSAGES"),
	_T("IRC"),
	_T("EM_STATISTIC"),
	_T("EM_PREFS"),
	_T("TOOLS"),
	_T("SAVE_APP_STATE"),
	_T("RELOAD_CONF"),
	_T("BACKUP"),
	_T("EMULE_AI_LABEL"),
};

#define	MAX_TOOLBAR_FILES	100
#define	MAX_SKIN_FILES		100

// CMuleToolbarCtrl

IMPLEMENT_DYNAMIC(CMuleToolbarCtrl, CToolBarCtrlX)

BEGIN_MESSAGE_MAP(CMuleToolbarCtrl, CToolBarCtrlX)
	ON_WM_SIZE()
	ON_NOTIFY_REFLECT(NM_RCLICK, OnNmRClick)
	ON_NOTIFY_REFLECT(TBN_QUERYDELETE, OnTbnQueryDelete)
	ON_NOTIFY_REFLECT(TBN_QUERYINSERT, OnTbnQueryInsert)
	ON_NOTIFY_REFLECT(TBN_GETBUTTONINFO, OnTbnGetButtonInfo)
	ON_NOTIFY_REFLECT(TBN_TOOLBARCHANGE, OnTbnToolbarChange)
	ON_NOTIFY_REFLECT(TBN_RESET, OnTbnReset)
	ON_NOTIFY_REFLECT(TBN_INITCUSTOMIZE, OnTbnInitCustomize)
	ON_NOTIFY_REFLECT(TBN_ENDADJUST, OnTbnEndAdjust)
	ON_WM_SYSCOLORCHANGE()
	ON_WM_SETTINGCHANGE()
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnNmCustomDraw)
	ON_WM_LBUTTONDBLCLK()
END_MESSAGE_MAP()

CMuleToolbarCtrl::CMuleToolbarCtrl()
	: m_iPreviousHeight()
	, m_iLastPressedButton(-1)
	, m_buttoncount(_countof(TBStringIDs) + 1)
	, TBButtons()
	, TBStrings()
	, m_eLabelType(NoLabels)
{
	if (thePrefs.GetUITweaksSpeedGraph()) {
		m_sizBtnBmp.cx = 32;
		m_sizBtnBmp.cy = 32;
	} else
		m_sizBtnBmp = thePrefs.GetToolbarIconSize();
}

CMuleToolbarCtrl::~CMuleToolbarCtrl()
{
	if (m_bmpBack.m_hObject)
		VERIFY(m_bmpBack.DeleteObject());
}

void CMuleToolbarCtrl::Init()
{
	m_astrToolbarPaths.RemoveAll();

	// Win98: Explicitly set to Unicode to receive Unicode notifications.
	SendMessage(CCM_SETUNICODEFORMAT, TRUE);

	ModifyStyle(0, TBSTYLE_FLAT | TBSTYLE_ALTDRAG | CCS_ADJUSTABLE | TBSTYLE_TRANSPARENT | TBSTYLE_TOOLTIPS | CCS_NODIVIDER);
	EnsureCustomToolTips();
	if (thePrefs.GetUseReBarToolbar()) {
		ModifyStyle(0, CCS_NORESIZE);
		SetExtendedStyle(GetExtendedStyle() | TBSTYLE_EX_HIDECLIPPEDBUTTONS);
	}

	ChangeToolbarBitmap(thePrefs.GetToolbarBitmapSettings(), false);

	// add button text:
	TCHAR cButtonStrings[2000];

	size_t lLen = 0;
	LPCTSTR uid = _T("MAIN_BTN_CONNECT");
	for (int i = 0; ; ++i) {
		const CString &str(GetResString(uid));
		int iLen = str.GetLength() + 1;
		memcpy(cButtonStrings + lLen, (LPCTSTR)str, iLen * sizeof(TCHAR));
		lLen += iLen;
		if (i >= _countof(TBStringIDs))
			break;
		uid = TBStringIDs[i];
	}

	// terminate
	cButtonStrings[lLen] = _T('\0');

	AddStrings(cButtonStrings);

	// initialize buttons:
	for (int i = 0; i < m_buttoncount; ++i) {
		TBButtons[i].fsState = TBSTATE_ENABLED;
		TBButtons[i].idCommand = IDC_TOOLBARBUTTON + i;
		TBButtons[i].iString = i;

		switch (TBButtons[i].idCommand) {
		case TBBTN_CONNECT:
		case TBBTN_OPTIONS:
		case TBBTN_TOOLS:
		case TBBTN_SAVESTATE:
		case TBBTN_RELOADCONF:
		case TBBTN_BACKUP:
		case TBBTN_EMULEAI:
			TBButtons[i].fsStyle = TBSTYLE_BUTTON;
			break;
		default:
			TBButtons[i].fsStyle = TBSTYLE_CHECKGROUP;
		}
	}

	// set button image indices
	int iBitmap = 0;
	for (int i = 0; i < m_buttoncount; ++i) {
		TBButtons[i].iBitmap = iBitmap;
		iBitmap += (TBButtons[i].idCommand == TBBTN_CONNECT) ? 3 : 1; // 'Connect' button has 3 states
	}

	TBBUTTON sepButton = {};
	sepButton.fsStyle = TBSTYLE_SEP;
	sepButton.fsState = TBSTATE_ENABLED;
	sepButton.iString = -1;
	sepButton.iBitmap = -1;

	const CString &config(thePrefs.GetToolbarSettings());
	const int iEmuleAIButtonIndex = TBBTN_EMULEAI - IDC_TOOLBARBUTTON;
	bool bLegacyConfigIndices = false;
	for (int i = 0; i + 1 < config.GetLength(); i += 2) {
		const int index = _tstoi(config.Mid(i, 2));
		if (index == 15) {
			bLegacyConfigIndices = true;
			break;
		}
	}

	bool bHasEmuleAIButton = false;
	CString normalizedConfig;
	for (int i = 0; i < config.GetLength(); i += 2) {
		int index = _tstoi(config.Mid(i, 2));
		if (bLegacyConfigIndices) {
			if (index == 11)
				continue; // Drop legacy Help button from persisted layouts.
			if (index > 11 && index <= 15)
				--index;
		}

		if (index == 99) {
			AddButtons(1, &sepButton);
			normalizedConfig.AppendFormat(_T("%02i"), 99);
			continue;
		}

		if (index < 0 || index >= m_buttoncount)
			continue;
		if (index == iEmuleAIButtonIndex)
			bHasEmuleAIButton = true;
		AddButtons(1, &TBButtons[index]);
		normalizedConfig.AppendFormat(_T("%02i"), index);
	}
	if (!bHasEmuleAIButton) {
		TBBUTTON emuleAIButton = TBButtons[iEmuleAIButtonIndex];
		TBBUTTON lastButton = {};
		if (GetButtonCount() > 0 && GetButton(GetButtonCount() - 1, &lastButton) && (lastButton.fsStyle & TBSTYLE_SEP) == 0)
			AddButtons(1, &sepButton);
		AddButtons(1, &emuleAIButton);

		CString updatedConfig;
		TBBUTTON buttonInfo = {};
		for (int i = 0; i < GetButtonCount(); ++i) {
			if (!GetButton(i, &buttonInfo))
				continue;
			updatedConfig.AppendFormat(_T("%02i"), (buttonInfo.idCommand != 0) ? buttonInfo.idCommand - IDC_TOOLBARBUTTON : 99);
		}
		thePrefs.SetToolbarSettings(updatedConfig);
	} else if (normalizedConfig != config) {
		thePrefs.SetToolbarSettings(normalizedConfig);
	}

	// recalc toolbar size
	SetAllButtonsStrings();
	ChangeTextLabelStyle(thePrefs.GetToolbarLabelSettings(), false, true);
	SetAllButtonsWidth();	// then calc and set the buttons width
	AutoSize();				// and finally call the original (but maybe obsolete) function
	SaveCurHeight();
	EnableButton(TBBTN_CONNECT, FALSE);
}

void CMuleToolbarCtrl::SetAllButtonsStrings()
{
	TBBUTTONINFO tbbi;
	tbbi.cbSize = (UINT)sizeof(TBBUTTONINFO);
	tbbi.dwMask = TBIF_TEXT;

	LPCTSTR uid;
	if (theApp.serverconnect->IsConnected())
		uid = _T("MAIN_BTN_DISCONNECT");
	else if (theApp.serverconnect->IsConnecting())
		uid = _T("MAIN_BTN_CANCEL");
	else
		uid = _T("MAIN_BTN_CONNECT");

	for (int i = 0; ; ++i) {
		const CString &str(GetResString(uid));
		_tcsncpy_s(TBStrings[i], _countof(TBStrings[i]), str, _TRUNCATE);
		tbbi.pszText = TBStrings[i];
		SetButtonInfo(IDC_TOOLBARBUTTON + i, &tbbi);
		if (i >= _countof(TBStringIDs))
			break;
		uid = TBStringIDs[i];
	}
}

void CMuleToolbarCtrl::Localize()
{
	if (m_hWnd) {
		SetAllButtonsStrings();
		SetAllButtonsWidth();
		AutoSize();
		UpdateIdealSize();
	}
}

void CMuleToolbarCtrl::OnSize(UINT nType, int cx, int cy)
{
	CToolBarCtrl::OnSize(nType, cx, cy);
	SetAllButtonsWidth();
	AutoSize();
}

void CMuleToolbarCtrl::SetAllButtonsWidth()
{
	if (GetButtonCount() <= 0)
		return;

	if (m_eLabelType == LabelsBelow) {
		CDC *pDC = GetDC();
		CFont *pFnt = GetFont();
		CFont *pOldFnt = pDC->SelectObject(pFnt);
		CRect r;

		// calculate the max. possible button size
		int iCalcSize = 0;
		for (int i = 0; i < m_buttoncount; ++i)
			if (!IsButtonHidden(IDC_TOOLBARBUTTON + i)) {
				pDC->DrawText(TBStrings[i], -1, r, DT_SINGLELINE | DT_CALCRECT);
				if (r.Width() > iCalcSize)
					iCalcSize = r.Width();
			}

		iCalcSize += 10;

		pDC->SelectObject(pOldFnt);
		ReleaseDC(pDC);

		if (!thePrefs.GetUseReBarToolbar()) {
			GetClientRect(&r);
			int bc = GetButtonCount();
			int iMaxPossible = r.Width() / (bc ? bc : 1);

			// if the buttons are too big, reduce their size
			if (iCalcSize > iMaxPossible)
				iCalcSize = iMaxPossible;
		} else {
			if (iCalcSize < 56)
				iCalcSize = 56;
			else if (iCalcSize > 72)
				iCalcSize = 72;
		}
		SetButtonWidth(iCalcSize, iCalcSize);
	} else {
		int iSmallIconsButtonHeight;
		if (theApp.m_ullComCtrlVer < MAKEDLLVERULL(6, 0, 0, 0)) {
			// Win98,WinME,Win2000: Comtrl32 prior to 6.0 cannot make a toolbar smaller than 22 pixels
			// in height and if it gets larger than 22 pixels the icons do not get centered vertically.
			iSmallIconsButtonHeight = 22;
		} else
			iSmallIconsButtonHeight = ::GetSystemMetrics(SM_CYSCREEN) <= 600 ? 16 : 28;

		int iFixedButtonWidth;
		int iFixedButtonHeight;
		if (m_eLabelType == NoLabels) {
			if (m_sizBtnBmp.cx == 16) {
				iFixedButtonWidth = 28;
				iFixedButtonHeight = iSmallIconsButtonHeight;
			} else {
				iFixedButtonWidth = 56;
				DWORD dwSize = GetButtonSize();
				iFixedButtonHeight = HIWORD(dwSize);
			}
		} else {
			iFixedButtonWidth = 0;
			iFixedButtonHeight = (m_sizBtnBmp.cx == 16) ? iSmallIconsButtonHeight : 0;
		}
		// it seems that the control updates itself more properly, if 'SetButtonWidth' id called *before* 'SetButtonSize'
		SetButtonWidth(iFixedButtonWidth, iFixedButtonWidth);
		SetButtonSize(CSize(iFixedButtonWidth, iFixedButtonHeight));
	}
}

void CMuleToolbarCtrl::OnNmRClick(LPNMHDR, LRESULT *pResult)
{
	if (GetKeyState(VK_CONTROL) < 0) {
		if (!thePrefs.GetToolbarBitmapSettings().IsEmpty())
			ChangeToolbarBitmap(thePrefs.GetToolbarBitmapSettings(), true);
		if (!thePrefs.GetSkinProfile().IsEmpty())
			theApp.ApplySkin(thePrefs.GetSkinProfile());

		*pResult = TRUE;
		return;
	}


	///////////////////////////////////////////////////////////////////////////
	// "Toolbar Bitmap" sub menu
	//
	CMenuXP menuBitmaps;
	menuBitmaps.CreateMenu();
	menuBitmaps.AppendMenu(MF_STRING, MP_SELECTTOOLBARBITMAP, GetResString(_T("SELECTTOOLBARBITMAP")));
	menuBitmaps.AppendMenu(MF_STRING, MP_SELECTTOOLBARBITMAPDIR, GetResString(_T("SELECTTOOLBARBITMAPDIR")));
	menuBitmaps.AppendMenu(MF_SEPARATOR);
	menuBitmaps.AppendMenu(MF_STRING, MP_TOOLBARBITMAP, GetResString(_T("DEFAULT")));

	m_astrToolbarPaths.RemoveAll();
	const CString &currentBitmapSettings(thePrefs.GetToolbarBitmapSettings());
	bool checked = currentBitmapSettings.IsEmpty();
	if (checked) {
		menuBitmaps.CheckMenuItem(MP_TOOLBARBITMAP, MF_CHECKED);
		menuBitmaps.EnableMenuItem(MP_TOOLBARBITMAP, MF_DISABLED);
	}
	m_astrToolbarPaths.Add(EMPTY); // dummy entry for 'Default' menu item
	int i = 1;
	if (!thePrefs.GetMuleDirectory(EMULE_TOOLBARDIR).IsEmpty()) {
		CStringArray astrToolbarFiles;
		for (unsigned f = 0; f < _countof(s_apszTBFiles); ++f) {
			WIN32_FIND_DATA FileData;
			HANDLE hSearch = FindFirstFile(thePrefs.GetMuleDirectory(EMULE_TOOLBARDIR) + s_apszTBFiles[f], &FileData);
			if (hSearch != INVALID_HANDLE_VALUE) {
				do
					astrToolbarFiles.Add(FileData.cFileName);
				while (astrToolbarFiles.GetCount() < MAX_TOOLBAR_FILES && FindNextFile(hSearch, &FileData));
				FindClose(hSearch);
			}
		}

		if (!astrToolbarFiles.IsEmpty()) {
			Sort(astrToolbarFiles);
			for (INT_PTR f = 0; f < astrToolbarFiles.GetCount(); ++f) {
				const CString &bitmapFileName(astrToolbarFiles[f]);
				LPCTSTR pTbBaseExt = stristr(bitmapFileName, EMULTB_BASEEXT);
				int iBaseLen = pTbBaseExt ? (int)(pTbBaseExt - (LPCTSTR)bitmapFileName - 1) : bitmapFileName.GetLength();
				menuBitmaps.AppendMenu(MF_STRING, MP_TOOLBARBITMAP + i, CString(bitmapFileName, iBaseLen));
				m_astrToolbarPaths.Add(thePrefs.GetMuleDirectory(EMULE_TOOLBARDIR) + bitmapFileName);
				if (!checked && currentBitmapSettings.CompareNoCase(m_astrToolbarPaths[i]) == 0) {
					menuBitmaps.CheckMenuItem(MP_TOOLBARBITMAP + i, MF_CHECKED);
					menuBitmaps.EnableMenuItem(MP_TOOLBARBITMAP + i, MF_DISABLED);
					checked = true;
				}
				++i;
			}
		}
		ASSERT(i - 1 == astrToolbarFiles.GetCount());
	}
	if (!checked) {
		menuBitmaps.AppendMenu(MF_STRING, MP_TOOLBARBITMAP + i, currentBitmapSettings);
		menuBitmaps.CheckMenuItem(MP_TOOLBARBITMAP + i, MF_CHECKED);
		menuBitmaps.EnableMenuItem(MP_TOOLBARBITMAP + i, MF_DISABLED);
		m_astrToolbarPaths.Add(currentBitmapSettings);
	}


	///////////////////////////////////////////////////////////////////////////
	// "Skin Profile" sub menu
	//
	CMenuXP menuSkins;
	menuSkins.CreateMenu();
	menuSkins.AppendMenu(MF_STRING, MP_SELECT_SKIN_FILE, GetResString(_T("SEL_SKIN")));
	menuSkins.AppendMenu(MF_STRING, MP_SELECT_SKIN_DIR, GetResString(_T("SEL_SKINDIR")));
	menuSkins.AppendMenu(MF_SEPARATOR);
	menuSkins.AppendMenu(MF_STRING, MP_SKIN_PROFILE, GetResString(_T("DEFAULT")));

	m_astrSkinPaths.RemoveAll();
	const CString &currentSkin(thePrefs.GetSkinProfile());
	checked = currentSkin.IsEmpty();
	if (checked) {
		menuSkins.CheckMenuItem(MP_SKIN_PROFILE, MF_CHECKED);
		menuSkins.EnableMenuItem(MP_SKIN_PROFILE, MF_DISABLED);
	}
	m_astrSkinPaths.Add(EMPTY); // dummy entry for 'Default' menu item
	i = 1;
	if (!thePrefs.GetMuleDirectory(EMULE_SKINDIR, false).IsEmpty()) {
		CStringArray astrSkinFiles;
		for (unsigned f = 0; f < _countof(s_apszSkinFiles); ++f) {
			WIN32_FIND_DATA FileData;
			HANDLE hSearch = FindFirstFile(thePrefs.GetMuleDirectory(EMULE_SKINDIR, false) + s_apszSkinFiles[f], &FileData);
			if (hSearch != INVALID_HANDLE_VALUE) {
				do
					astrSkinFiles.Add(FileData.cFileName);
				while (astrSkinFiles.GetCount() < MAX_SKIN_FILES && FindNextFile(hSearch, &FileData));
				FindClose(hSearch);
			}
		}

		if (!astrSkinFiles.IsEmpty()) {
			Sort(astrSkinFiles);
			for (INT_PTR f = 0; f < astrSkinFiles.GetCount(); ++f) {
				const CString &skinFileName(astrSkinFiles[f]);
				LPCTSTR pSkinBaseExt = stristr(skinFileName, _T(".") EMULSKIN_BASEEXT _T(".ini"));
				int iBaseLen = pSkinBaseExt ? (int)(pSkinBaseExt - (LPCTSTR)skinFileName - 1) : skinFileName.GetLength();
				menuSkins.AppendMenu(MF_STRING, MP_SKIN_PROFILE + i, CString(skinFileName, iBaseLen));
				m_astrSkinPaths.Add(thePrefs.GetMuleDirectory(EMULE_SKINDIR, false) + skinFileName);
				if (!checked && currentSkin.CompareNoCase(m_astrSkinPaths[i]) == 0) {
					menuSkins.CheckMenuItem(MP_SKIN_PROFILE + i, MF_CHECKED);
					menuSkins.EnableMenuItem(MP_SKIN_PROFILE + i, MF_DISABLED);
					checked = true;
				}
				++i;
			}
		}
		ASSERT(i - 1 == astrSkinFiles.GetCount());
	}
	if (!checked) {
		menuSkins.AppendMenu(MF_STRING, MP_SKIN_PROFILE + i, currentSkin);
		menuSkins.CheckMenuItem(MP_SKIN_PROFILE + i, MF_CHECKED);
		menuSkins.EnableMenuItem(MP_SKIN_PROFILE + i, MF_DISABLED);
		m_astrSkinPaths.Add(currentSkin);
	}


	///////////////////////////////////////////////////////////////////////////
	// "Text Label" sub menu
	//
	CMenuXP menuTextLabels;
	menuTextLabels.CreateMenu();
	ASSERT(MP_NOTEXTLABELS == MP_TEXTLABELS - 1 && MP_NOTEXTLABELS == MP_TEXTLABELSONRIGHT - 2);
	ASSERT(MP_NOTEXTLABELS + (int)NoLabels == MP_NOTEXTLABELS);
	ASSERT(MP_NOTEXTLABELS + (int)LabelsBelow == MP_TEXTLABELS);
	ASSERT(MP_NOTEXTLABELS + (int)LabelsRight == MP_TEXTLABELSONRIGHT);
	menuTextLabels.AppendMenu(MF_STRING | MF_ENABLED, MP_NOTEXTLABELS, GetResString(_T("NOTEXTLABELS")));
	menuTextLabels.AppendMenu(MF_STRING | MF_ENABLED, MP_TEXTLABELS, GetResString(_T("ENABLETEXTLABELS")));
	menuTextLabels.AppendMenu(MF_STRING | MF_ENABLED, MP_TEXTLABELSONRIGHT, GetResString(_T("TEXTLABELSONRIGHT")));
	menuTextLabels.CheckMenuRadioItem(MP_NOTEXTLABELS, MP_TEXTLABELSONRIGHT, MP_NOTEXTLABELS + (int)thePrefs.GetToolbarLabelSettings(), MF_BYCOMMAND);
	menuTextLabels.EnableMenuItem(MP_NOTEXTLABELS + (int)thePrefs.GetToolbarLabelSettings(), MF_BYCOMMAND | MF_DISABLED);

	menuTextLabels.AppendMenu(MF_SEPARATOR);
	menuTextLabels.AppendMenu(MF_STRING, MP_LARGEICONS, GetResString(_T("LARGEICONS")));
	menuTextLabels.AppendMenu(MF_STRING, MP_SMALLICONS, GetResString(_T("SMALLICONS")));
	ASSERT(MP_LARGEICONS == MP_SMALLICONS - 1);
	menuTextLabels.CheckMenuRadioItem(MP_LARGEICONS, MP_SMALLICONS, m_sizBtnBmp.cx == 16 ? MP_SMALLICONS : MP_LARGEICONS, MF_BYCOMMAND);
	menuTextLabels.EnableMenuItem(m_sizBtnBmp.cx == 16 ? MP_SMALLICONS : MP_LARGEICONS, MF_BYCOMMAND | MF_DISABLED);


	///////////////////////////////////////////////////////////////////////////
	// Toolbar context menu
	//
	CMenuXP menuToolbar;
	menuToolbar.CreatePopupMenu();
	menuToolbar.AddMenuSidebar(GetResString(_T("TOOLBAR")));
	menuToolbar.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)menuBitmaps.m_hMenu, GetResString(_T("TOOLBARSKINS")));
	menuToolbar.AppendMenu(MF_STRING | MF_POPUP | (IsDarkModeEnabled() ? MF_GRAYED : MF_ENABLED), (UINT_PTR)menuSkins.m_hMenu, GetResString(_T("SKIN_PROF")));
	menuToolbar.AppendMenu(MF_STRING | MF_POPUP | (thePrefs.GetUITweaksSpeedGraph() ? MF_GRAYED : MF_ENABLED), (UINT_PTR)menuTextLabels.m_hMenu, GetResString(_T("TEXTLABELS")));
	menuToolbar.AppendMenu(MF_STRING, MP_CUSTOMIZETOOLBAR, GetResString(_T("CUSTOMIZETOOLBAR")));
	CPoint point;
	GetCursorPos(&point);
	// Defining context menu of MuleToolBarCtrl as CMenuXP instead of CMenu causes an "Encounted and improper argument"
	// exception. Redirecting menu commands to theApp.emuledlg fixis this exception.
	menuToolbar.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, theApp.emuledlg);

	VERIFY(menuBitmaps.DestroyMenu());
	VERIFY(menuSkins.DestroyMenu());
	VERIFY(menuTextLabels.DestroyMenu());
	VERIFY(menuToolbar.DestroyMenu());

	*pResult = TRUE;
}

void CMuleToolbarCtrl::OnTbnQueryDelete(LPNMHDR, LRESULT *pResult)
{
	*pResult = TRUE;
}

void CMuleToolbarCtrl::OnTbnQueryInsert(LPNMHDR, LRESULT *pResult)
{
	*pResult = TRUE;
}

void CMuleToolbarCtrl::OnTbnGetButtonInfo(LPNMHDR pNMHDR, LRESULT *pResult)
{
	LPNMTOOLBAR pNMTB = reinterpret_cast<LPNMTOOLBAR>(pNMHDR);
	if ((size_t)pNMTB->iItem >= _countof(TBButtons))
		*pResult = FALSE;
	else {
		CString strText(TBStrings[pNMTB->iItem]);
		strText.Remove(_T('&'));
		_tcsncpy(pNMTB->pszText, strText, pNMTB->cchText - 1);
		pNMTB->pszText[pNMTB->cchText - 1] = _T('\0');
		pNMTB->tbButton = TBButtons[pNMTB->iItem];
		if (m_eLabelType == LabelsRight)
			pNMTB->tbButton.fsStyle |= TBSTYLE_AUTOSIZE;
		*pResult = TRUE;
	}
}

void CMuleToolbarCtrl::OnTbnToolbarChange(LPNMHDR, LRESULT *pResult)
{
	CString config;
	for (int i = 0; i < GetButtonCount(); ++i) {
		TBBUTTON buttoninfo;
		if (GetButton(i, &buttoninfo))
			config.AppendFormat(_T("%02i"), (buttoninfo.idCommand != 0) ? buttoninfo.idCommand - IDC_TOOLBARBUTTON : 99);
	}

	thePrefs.SetToolbarSettings(config);
	Localize();

	theApp.emuledlg->ShowConnectionState();

	SetAllButtonsWidth();
	AutoSize();

	*pResult = 0;
}

void CMuleToolbarCtrl::ChangeToolbarBitmap(const CString &path, bool bRefresh)
{
	bool bResult = false;
	CImageList ImageList;
	if (!path.IsEmpty()) {
		CEnBitmap Bitmap;
		BITMAP bm;
		if (Bitmap.LoadImage(path)
			&& Bitmap.GetObject(sizeof bm, &bm)
			// Allow compatibility with old toolbar bitmaps
			&& (bm.bmWidth == NUM_BUTTON_BITMAPS * m_sizBtnBmp.cx || bm.bmWidth == PREV_NUM_BUTTON_BITMAPS * m_sizBtnBmp.cx || bm.bmWidth == OLD_NUM_BUTTON_BITMAPS * m_sizBtnBmp.cx)
			&& bm.bmHeight == m_sizBtnBmp.cy)
		{
			bool bAlpha = bm.bmBitsPixel > 24;
			if (ImageList.Create(m_sizBtnBmp.cx, bm.bmHeight, bAlpha ? ILC_COLOR32 : (theApp.m_iDfltImageListColorFlags | ILC_MASK), 0, 1)) {
				ImageList.Add(&Bitmap, bAlpha ? CLR_DEFAULT : RGB(255, 0, 255));
				if (bm.bmWidth == PREV_NUM_BUTTON_BITMAPS * m_sizBtnBmp.cx || bm.bmWidth == OLD_NUM_BUTTON_BITMAPS * m_sizBtnBmp.cx)
					ImageList.Remove(13); // Drop legacy Help icon slot.

				if (bm.bmWidth == OLD_NUM_BUTTON_BITMAPS * m_sizBtnBmp.cx) {
					ImageList.Add(CTempIconLoader(_T("SAVE"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
					ImageList.Add(CTempIconLoader(_T("RELOAD"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
					ImageList.Add(CTempIconLoader(_T("BACKUP"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
					ImageList.Add(CTempIconLoader(_T("EMULEAI"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
				}

				if (ImageList.GetImageCount() == NUM_BUTTON_BITMAPS) {
					CImageList *pimlOld = SetImageList(&ImageList);
					ImageList.Detach();
					if (pimlOld)
						pimlOld->DeleteImageList();
					bResult = true;
				}
			}
		}
	}

	// if image file loading or image list creation failed, create default image list.
	if (!bResult) {
		// load from icon resources
		ImageList.Create(m_sizBtnBmp.cx, m_sizBtnBmp.cy, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
		ImageList.Add(CTempIconLoader(_T("CONNECT"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
		ImageList.Add(CTempIconLoader(_T("DISCONNECT"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
		ImageList.Add(CTempIconLoader(_T("STOPCONNECTING"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
		ImageList.Add(CTempIconLoader(_T("KADEMLIA"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
		ImageList.Add(CTempIconLoader(_T("SERVER"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
		ImageList.Add(CTempIconLoader(_T("TRANSFER"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
		ImageList.Add(CTempIconLoader(_T("SEARCH"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
		ImageList.Add(CTempIconLoader(_T("FILE"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
		ImageList.Add(CTempIconLoader(_T("MESSAGES"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
		ImageList.Add(CTempIconLoader(_T("IRC"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
		ImageList.Add(CTempIconLoader(_T("STATISTICS"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
		ImageList.Add(CTempIconLoader(_T("PREFERENCES"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
		ImageList.Add(CTempIconLoader(_T("TOOLS"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
		ImageList.Add(CTempIconLoader(_T("SAVE"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
		ImageList.Add(CTempIconLoader(_T("RELOAD"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
		ImageList.Add(CTempIconLoader(_T("BACKUP"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
		ImageList.Add(CTempIconLoader(_T("EMULEAI"), m_sizBtnBmp.cx, m_sizBtnBmp.cy));
		ASSERT(ImageList.GetImageCount() == NUM_BUTTON_BITMAPS);
		CImageList *pimlOld = SetImageList(&ImageList);
		ImageList.Detach();
		if (pimlOld)
			pimlOld->DeleteImageList();
	}

	if (bRefresh) {
		UpdateBackground();
		Invalidate();
		Refresh();
	}
}

BOOL CMuleToolbarCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	switch (wParam) {
	case MP_SELECTTOOLBARBITMAPDIR:
		{
			TCHAR buffer[MAX_PATH];
			_sntprintf(buffer, _countof(buffer), _T("%s"), (LPCTSTR)thePrefs.GetMuleDirectory(EMULE_TOOLBARDIR));
			buffer[_countof(buffer) - 1] = _T('\0');
			if (SelectDir(m_hWnd, buffer, GetResString(_T("SELECTTOOLBARBITMAPDIR"))))
				thePrefs.SetMuleDirectory(EMULE_TOOLBARDIR, buffer);
		}
		break;
	case MP_CUSTOMIZETOOLBAR:
		Customize();
		break;
	case MP_SELECTTOOLBARBITMAP:
		{
			// we could also load "*.jpg" here, but because of the typical non solid background of JPGs this
			// doesn't make sense here.
			CString strFilter(GetResString(_T("LOADFILTER_EMTOOLBAR")) + _T(" ("));
			for (unsigned f = 0; f < _countof(s_apszTBFiles); ++f) {
				if (f > 0)
					strFilter += _T(';');
				strFilter += s_apszTBFiles[f];
			}
			strFilter += _T(")|");
			for (unsigned f = 0; f < _countof(s_apszTBFiles); ++f) {
				if (f > 0)
					strFilter += _T(';');
				strFilter += s_apszTBFiles[f];
			}
			strFilter += _T("||");

			const CString &sInitialDir(thePrefs.GetMuleDirectory(EMULE_TOOLBARDIR, false));
			CFileDialog dialog(TRUE, EMULTB_BASEEXT _T(".bmp"), (sInitialDir.IsEmpty() ? NULL : sInitialDir), OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST, strFilter, NULL, 0);
			if (IDOK == dialog.DoModal())
				if (thePrefs.GetToolbarBitmapSettings() != dialog.GetPathName()) {
					ChangeToolbarBitmap(dialog.GetPathName(), true);
					thePrefs.SetToolbarBitmapSettings(dialog.GetPathName());
				}
		}
		break;
	case MP_LARGEICONS:
	case MP_SMALLICONS:
		// Always show big icons when SpeedGraph enabled!
		if (thePrefs.GetUITweaksSpeedGraph())
			break;
		m_sizBtnBmp.cx = m_sizBtnBmp.cy = (wParam == MP_LARGEICONS) ? 32 : 16;
		ChangeToolbarBitmap(thePrefs.GetToolbarBitmapSettings(), true);
		thePrefs.SetToolbarIconSize(m_sizBtnBmp);
		break;
	case MP_NOTEXTLABELS:
		ChangeTextLabelStyle(NoLabels, true);
		thePrefs.SetToolbarLabelSettings(NoLabels);
		break;
	case MP_TEXTLABELS:
		ChangeTextLabelStyle(LabelsBelow, true);
		thePrefs.SetToolbarLabelSettings(LabelsBelow);
		break;
	case MP_TEXTLABELSONRIGHT:
		ChangeTextLabelStyle(LabelsRight, true);
		thePrefs.SetToolbarLabelSettings(LabelsRight);
		break;
	case MP_SELECT_SKIN_DIR:
		{
			TCHAR buffer[MAX_PATH];
			_sntprintf(buffer, _countof(buffer), _T("%s"), (LPCTSTR)thePrefs.GetMuleDirectory(EMULE_SKINDIR, false));
			buffer[_countof(buffer) - 1] = _T('\0');
			if (SelectDir(m_hWnd, buffer, GetResString(_T("SELSKINPROFILEDIR"))))
				thePrefs.SetMuleDirectory(EMULE_SKINDIR, buffer);
		}
		break;
	case MP_SELECT_SKIN_FILE:
		{
			CString strFilter(GetResString(_T("LOADFILTER_EMSKINFILES")) + _T(" ("));
			for (unsigned f = 0; f < _countof(s_apszSkinFiles); ++f) {
				if (f > 0)
					strFilter += _T(';');
				strFilter += s_apszSkinFiles[f];
			}
			strFilter += _T(")|");
			for (unsigned f = 0; f < _countof(s_apszSkinFiles); ++f) {
				if (f > 0)
					strFilter += _T(';');
				strFilter += s_apszSkinFiles[f];
			}
			strFilter += _T("||");

			const CString &sInitialDir(thePrefs.GetMuleDirectory(EMULE_SKINDIR, false));
			CFileDialog dialog(TRUE, EMULSKIN_BASEEXT _T(".ini"), (sInitialDir.IsEmpty() ? NULL : sInitialDir), OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST, strFilter, NULL, 0);
			if (dialog.DoModal() == IDOK) {
				if (thePrefs.GetSkinProfile().CompareNoCase(dialog.GetPathName()) != 0)
					theApp.ApplySkin(dialog.GetPathName());
			}
		}
		break;
	default:
		if (wParam >= MP_TOOLBARBITMAP && wParam < MP_TOOLBARBITMAP + MAX_TOOLBAR_FILES) {
			if (thePrefs.GetToolbarBitmapSettings().CompareNoCase(m_astrToolbarPaths[wParam - MP_TOOLBARBITMAP]) != 0) {
				ChangeToolbarBitmap(m_astrToolbarPaths[wParam - MP_TOOLBARBITMAP], true);
				thePrefs.SetToolbarBitmapSettings(m_astrToolbarPaths[wParam - MP_TOOLBARBITMAP]);
			}
		} else if (wParam >= MP_SKIN_PROFILE && wParam < MP_SKIN_PROFILE + MAX_SKIN_FILES) {
			if (thePrefs.GetSkinProfile().CompareNoCase(m_astrSkinPaths[wParam - MP_SKIN_PROFILE]) != 0)
				theApp.ApplySkin(m_astrSkinPaths[wParam - MP_SKIN_PROFILE]);
		}
	}

	return TRUE;
}

void CMuleToolbarCtrl::ChangeTextLabelStyle(EToolbarLabelType eLabelType, bool bRefresh, bool bForceUpdateButtons)
{
	// always show below when SpeedGraph enabled!
	if (thePrefs.GetUITweaksSpeedGraph())
		eLabelType = LabelsBelow;

	if (m_eLabelType != eLabelType || bForceUpdateButtons) {
		switch (eLabelType) {
		case NoLabels:
			SetStyle(GetStyle() & ~TBSTYLE_LIST);
			SetMaxTextRows(0);
			break;
		case LabelsBelow:
			SetStyle(GetStyle() & ~TBSTYLE_LIST);
			SetMaxTextRows(1);
			break;
		case LabelsRight:
			SetStyle(GetStyle() | TBSTYLE_LIST);
			SetMaxTextRows(1);
		}

		TBBUTTONINFO tbbi;
		tbbi.cbSize = (UINT)sizeof tbbi;
		tbbi.dwMask = TBIF_STYLE;
		for (int i = 0; i < m_buttoncount; ++i) {
			if (GetButtonInfo(IDC_TOOLBARBUTTON + i, &tbbi)) {
				if (eLabelType == LabelsRight)
					tbbi.fsStyle |= TBSTYLE_AUTOSIZE;
				else
					tbbi.fsStyle &= ~TBSTYLE_AUTOSIZE;
				SetButtonInfo(IDC_TOOLBARBUTTON + i, &tbbi);
			}
		}

		m_eLabelType = eLabelType;
		if (bRefresh)
			Refresh();
	}
}

void CMuleToolbarCtrl::Refresh()
{
	SetAllButtonsWidth();
	AutoSize();	// Causes a toolbar to be resized.

	if (theApp.emuledlg->m_ctlMainTopReBar.m_hWnd) {
		theApp.emuledlg->RemoveAnchor(theApp.emuledlg->m_ctlMainTopReBar.m_hWnd);

		REBARBANDINFO rbbi = {};
		CSize sizeBar;
		GetMaxSize(&sizeBar);
		ASSERT(sizeBar.cx != 0 && sizeBar.cy != 0);
		rbbi.cbSize = (UINT)sizeof rbbi;
		rbbi.fMask = RBBIM_CHILDSIZE | RBBIM_IDEALSIZE;
		rbbi.cxMinChild = sizeBar.cy;
		rbbi.cyMinChild = sizeBar.cy;
		rbbi.cxIdeal = sizeBar.cx;
		VERIFY(theApp.emuledlg->m_ctlMainTopReBar.SetBandInfo(MULE_TOOLBAR_BAND_NR, &rbbi));

		AddOrReplaceAnchor(theApp.emuledlg, theApp.emuledlg->m_ctlMainTopReBar.m_hWnd, TOP_LEFT, TOP_RIGHT);
	}

	CRect rToolbarRect;
	GetWindowRect(&rToolbarRect);

	if (m_iPreviousHeight == rToolbarRect.Height()) {
		theApp.emuledlg->ResizeSpeedGraph();
		Invalidate();
		RedrawWindow();
	} else {
		m_iPreviousHeight = rToolbarRect.Height();

		CRect rcClient;
		theApp.emuledlg->GetClientRect(&rcClient);

		CRect rcStatusbar;
		theApp.emuledlg->statusbar->GetWindowRect(&rcStatusbar);

		rcClient.top += rToolbarRect.Height();
		rcClient.bottom -= rcStatusbar.Height();

		CWnd *wnds[] =
		{
			theApp.emuledlg->serverwnd,
			theApp.emuledlg->kademliawnd,
			theApp.emuledlg->transferwnd,
			theApp.emuledlg->sharedfileswnd,
			theApp.emuledlg->searchwnd,
			theApp.emuledlg->chatwnd,
			theApp.emuledlg->ircwnd,
			theApp.emuledlg->statisticswnd
		};

		for (int i = (int)_countof(wnds); --i >= 0;) {
			wnds[i]->SetWindowPos(NULL, rcClient.left, rcClient.top, rcClient.Width(), rcClient.Height(), SWP_NOZORDER);
			AddOrReplaceAnchor(theApp.emuledlg, wnds[i]->m_hWnd, TOP_LEFT, BOTTOM_RIGHT);
		}

		theApp.emuledlg->ResizeSpeedGraph();

		theApp.emuledlg->Invalidate();
		theApp.emuledlg->RedrawWindow();
	}
}

void CMuleToolbarCtrl::OnTbnReset(LPNMHDR, LRESULT*)
{
	// First, get rid of the old buttons while saving their states
	for (int i = GetButtonCount(); --i >= 0;) {
		TBBUTTON Button;
		GetButton(i, &Button);
		for (int j = 0; j < m_buttoncount; ++j)
			if (TBButtons[j].idCommand == Button.idCommand) {
				TBButtons[j].fsState = Button.fsState;
				TBButtons[j].fsStyle = Button.fsStyle;
				TBButtons[j].iString = Button.iString;
				break;
			}

		DeleteButton(i);
	}

	TBBUTTON sepButton;
	sepButton.idCommand = 0;
	sepButton.fsStyle = TBSTYLE_SEP;
	sepButton.fsState = TBSTATE_ENABLED;
	sepButton.iString = -1;
	sepButton.iBitmap = -1;

	// set default configuration
	const CString &config(strDefaultToolbar);
	for (int i = 0; i < config.GetLength(); i += 2) {
		int index = _tstoi(config.Mid(i, 2));
		if (index == 99)
			AddButtons(1, &sepButton);
		else if (index >= 0 && index < m_buttoncount)
			AddButtons(1, &TBButtons[index]);
	}

	// save new (default) configuration
	thePrefs.SetToolbarSettings(config);

	Localize();		// we have to localize the button-text

	theApp.emuledlg->ShowConnectionState();

	ChangeTextLabelStyle(thePrefs.GetToolbarLabelSettings(), false, true);
	SetAllButtonsWidth();	// then calc and set the button width
	AutoSize();
}

void CMuleToolbarCtrl::OnTbnInitCustomize(LPNMHDR, LRESULT *pResult)
{
	*pResult = TBNRF_HIDEHELP;

	if (IsDarkModeEnabled())	{
		// remember which toolbar started customization
		s_pCustomizeInstance = this;
		s_hCustomizeMsgHook = SetWindowsHookEx(WH_CALLWNDPROC, CMuleToolbarCtrl::CustomizeCwpProc, AfxGetInstanceHandle(), GetCurrentThreadId());
	}
}

void CMuleToolbarCtrl::OnTbnEndAdjust(LPNMHDR, LRESULT *pResult)
{
	UpdateIdealSize();
	if (s_hCustomizeMsgHook)	{
		UnhookWindowsHookEx(s_hCustomizeMsgHook);
		s_hCustomizeMsgHook = nullptr;
	}
	*pResult = 0; // return value is ignored
}

void CMuleToolbarCtrl::OnSysColorChange()
{
	CToolBarCtrl::OnSysColorChange();
	ChangeToolbarBitmap(thePrefs.GetToolbarBitmapSettings(), true);
}

void CMuleToolbarCtrl::OnSettingChange(UINT uFlags, LPCTSTR lpszSection)
{
	CToolBarCtrl::OnSettingChange(uFlags, lpszSection);

	// Vista: There are certain situations where the toolbar control does not redraw/resize
	// correctly under Vista. Unfortunately Vista just sends a WM_SETTINGCHANGE when certain
	// system settings have changed. Furthermore Vista sends that particular message way
	// more often than WinXP.
	// Whenever the toolbar control receives a WM_SETTINGCHANGE, it tries to resize itself
	// (most likely because it thinks that some system font settings have changed). However,
	// that resizing does fail when the toolbar control has certain non-standard metrics
	// applied (see the table below).
	//
	// Toolbar configuration		Redraw due to WM_SETTINGCHANGE
	// ----------------------------------------------------------
	// Large Icons + No Text		Fail
	// Small Icons + No Text		Fail
	//
	// Large Icons + Text on Right	OK
	// Small Icons + Text on Right	Fail
	//
	// Large Icons + Text on Bottom	OK
	// Small Icons + Text on Bottom	OK
	//
	// The problem with this kind of 'correction' is that the WM_SETTINGCHANGE message is
	// sometimes sent very often and we need to try to invoke our 'correction' code as seldom
	// as possible to avoid too much window flickering.
	//
	// The toolbar control seems to *not* evaluate the "lpszSection" parameter of the WM_SETTINGCHANGE
	// message to determine if it really needs to resize itself, it seems to just resize itself
	// whenever a WM_SETTINGCHANGE is received, regardless the value of that parameter. Thus, we can
	// not use the value of that parameter to limit the invocation of our correction code.
	//

	if (theApp.m_ullComCtrlVer >= MAKEDLLVERULL(6, 16, 0, 0)
		&& (m_eLabelType == NoLabels || (m_eLabelType == LabelsRight && m_sizBtnBmp.cx == 16)))
	{
		ChangeToolbarBitmap(thePrefs.GetToolbarBitmapSettings(), true);
	}
}

void CMuleToolbarCtrl::PressMuleButton(int nID)
{
	// Customization might splits up the button-group, so we have to (un-)press them on our own
	if (m_iLastPressedButton != -1)
		CheckButton(m_iLastPressedButton, FALSE);
	CheckButton(nID, TRUE);
	m_iLastPressedButton = nID;

	// Force full repaint in case UI thread was blocked
	InvalidateRect(nullptr, FALSE);
	UpdateWindow();
}

void CMuleToolbarCtrl::UpdateIdealSize()
{
	if (theApp.emuledlg->m_ctlMainTopReBar.m_hWnd) {
		// let the rebar know what's our new current ideal size, so the chevron is handled correctly
		CSize sizeBar;
		GetMaxSize(&sizeBar);
		ASSERT(sizeBar.cx != 0 && sizeBar.cy != 0);

		REBARBANDINFO rbbi = {};
		rbbi.cbSize = (UINT)sizeof rbbi;
		rbbi.fMask = RBBIM_IDEALSIZE;
		rbbi.cxIdeal = sizeBar.cx;
		VERIFY(theApp.emuledlg->m_ctlMainTopReBar.SetBandInfo(MULE_TOOLBAR_BAND_NR, &rbbi));
	}
}

#ifdef _DEBUG

void CMuleToolbarCtrl::DumpInfo()
{
	TRACE("---\n");
	CRect rcWnd;
	GetWindowRect(&rcWnd);
	TRACE("Wnd =%4d,%4d-%4d,%4d (%4d x %4d)\n", rcWnd.left, rcWnd.top, rcWnd.right, rcWnd.bottom, rcWnd.Width(), rcWnd.Height());

	CRect rcClnt;
	GetClientRect(&rcClnt);
	TRACE("Clnt=%4d,%4d-%4d,%4d (%4d x %4d)\n", rcClnt.left, rcClnt.top, rcClnt.right, rcClnt.bottom, rcClnt.Width(), rcClnt.Height());

	// Total size of all of the visible buttons and separators in the toolbar.
	CSize siz;
	GetMaxSize(&siz);
	TRACE("MaxSize=                  %4d x %4d\n", siz.cx, siz.cy);

	int iButtons = GetButtonCount();	// Count of the buttons currently in the toolbar.
	int iRows = GetRows();				// Number of rows of buttons in a toolbar with the TBSTYLE_WRAPABLE style
	int iMaxTextRows = GetMaxTextRows();// Maximum number of text rows that can be displayed on a toolbar button.
	TRACE("ButtonCount=%d  Rows=%d  MaxTextRows=%d\n", iButtons, iRows, iMaxTextRows);

	// Current width and height of toolbar buttons, in pixels.
	DWORD dwButtonSize = GetButtonSize();
	TRACE("ButtonSize=%dx%d\n", LOWORD(dwButtonSize), HIWORD(dwButtonSize));

	// Padding for a toolbar control.
	LRESULT dwPadding = SendMessage(TB_GETPADDING);
	TRACE("Padding=%dx%d\n", LOWORD(dwPadding), HIWORD(dwPadding));

	DWORD dwBitmapFlags = GetBitmapFlags(); // TBBF_LARGE=0x0001
	TRACE("BitmapFlags=%u\n", dwBitmapFlags);

	CRect rcButton;
	// Bounding rectangle of a button in a toolbar.
	TRACE("ItemRects:");
	for (int i = 0; i < iButtons; ++i) {
		GetItemRect(i, &rcButton);
		TRACE(" %2dx%2d", rcButton.Width(), rcButton.Height());
	}
	TRACE("\n");

	// Bounding rectangle for a specified toolbar button.
	TRACE("Rects    :");
	for (int i = 0; i < iButtons; ++i) {
		GetRect(IDC_TOOLBARBUTTON + i, &rcButton);
		TRACE(" %2dx%2d", rcButton.Width(), rcButton.Height());
	}
	TRACE("\n");

	TRACE("Info     :");
	TBBUTTONINFO tbbi;
	tbbi.cbSize = (UINT)sizeof tbbi;
	tbbi.dwMask |= TBIF_BYINDEX | TBIF_COMMAND | TBIF_IMAGE | TBIF_LPARAM | TBIF_SIZE | TBIF_STATE | TBIF_STYLE | TBIF_TEXT;

	for (int i = 0; i < iButtons; ++i) {
		TCHAR szLabel[256];
		tbbi.cchText = _countof(szLabel);
		tbbi.pszText = szLabel;
		if (GetButtonInfo(i, &tbbi) >= 0)
			TRACE(" %2d ", tbbi.cx);
		}
	TRACE("\n");
}
#endif

void CMuleToolbarCtrl::AutoSize()
{
	CToolBarCtrl::AutoSize();
#ifdef _DEBUG
#endif
}

BOOL CMuleToolbarCtrl::GetMaxSize(LPSIZE pSize) const
{
	BOOL bResult = CToolBarCtrl::GetMaxSize(pSize);
	if (theApp.m_ullComCtrlVer <= MAKEDLLVERULL(5, 81, 0, 0)) {
		int iWidth = 0;
		CRect rcButton;
		for (int i = GetButtonCount(); --i >= 0;)
			if (GetItemRect(i, &rcButton))
				iWidth += rcButton.Width();

		if (iWidth > pSize->cx)
			pSize->cx = iWidth;
	}
	return bResult;
}

void CMuleToolbarCtrl::SaveCurHeight()
{
	CRect rcWnd;
	GetWindowRect(&rcWnd);
	m_iPreviousHeight = rcWnd.Height();
}

void CMuleToolbarCtrl::UpdateBackground()
{
	if (theApp.emuledlg->m_ctlMainTopReBar) {
		HBITMAP	hbmp = theApp.LoadImage(_T("MainToolBarBk"), _T("BMP"));
		if (hbmp) {
			REBARBANDINFO rbbi = {};
			rbbi.cbSize = (UINT)sizeof rbbi;
			rbbi.fMask = RBBIM_STYLE;
			if (theApp.emuledlg->m_ctlMainTopReBar.GetBandInfo(MULE_TOOLBAR_BAND_NR, &rbbi)) {
				rbbi.fMask = RBBIM_STYLE | RBBIM_BACKGROUND;
				rbbi.fStyle |= RBBS_FIXEDBMP;
				rbbi.hbmBack = hbmp;
				if (theApp.emuledlg->m_ctlMainTopReBar.SetBandInfo(MULE_TOOLBAR_BAND_NR, &rbbi)) {
					if (m_bmpBack.m_hObject)
						VERIFY(m_bmpBack.DeleteObject());
					m_bmpBack.Attach(hbmp);
					hbmp = NULL;
				}
			}
			if (hbmp)
				VERIFY(::DeleteObject(hbmp));
		} else {
			REBARBANDINFO rbbi = {};
			rbbi.cbSize = (UINT)sizeof rbbi;
			rbbi.fMask = RBBIM_STYLE;
			if (theApp.emuledlg->m_ctlMainTopReBar.GetBandInfo(MULE_TOOLBAR_BAND_NR, &rbbi)) {
				rbbi.fMask = RBBIM_STYLE | RBBIM_BACKGROUND;
				rbbi.fStyle &= ~RBBS_FIXEDBMP;
				rbbi.hbmBack = NULL;
				if (theApp.emuledlg->m_ctlMainTopReBar.SetBandInfo(MULE_TOOLBAR_BAND_NR, &rbbi)) {
					if (m_bmpBack.m_hObject)
						VERIFY(m_bmpBack.DeleteObject());
				}
			}
		}
	}
}

// hook handle for WH_CALLWNDPROC
HHOOK CMuleToolbarCtrl::s_hCustomizeMsgHook = nullptr;
CMuleToolbarCtrl* CMuleToolbarCtrl::s_pCustomizeInstance = nullptr;

// WH_CALLWNDPROC hook to catch WM_INITDIALOG before first paint
LRESULT CALLBACK CMuleToolbarCtrl::CustomizeCwpProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION) {
		auto p = reinterpret_cast<CWPSTRUCT*>(lParam);
		// fire only when the dialog is actually shown
		if (p->message == WM_SHOWWINDOW && p->wParam) {
			wchar_t cls[32];
			::GetClassNameW(p->hwnd, cls, _countof(cls));
			// skip main window
			if (wcscmp(cls, L"#32770") == 0 && p->hwnd != theApp.emuledlg->GetSafeHwnd()) {
				// Apply dark NC and client
				ApplyTheme(p->hwnd);
				::SetWindowSubclass(p->hwnd, CMuleToolbarCtrl::CustomizeDlgSubclassProc, /*id=*/1, reinterpret_cast<DWORD_PTR>(s_pCustomizeInstance));
				// unhook so it only runs once
				UnhookWindowsHookEx(s_hCustomizeMsgHook);
				s_hCustomizeMsgHook = nullptr;
			}
		}
	}

	return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// subclass proc for custom-drawing dialog controls 
LRESULT CALLBACK CMuleToolbarCtrl::CustomizeDlgSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR  /*uIdSubclass*/, DWORD_PTR dwRefData)
{
	auto pThis = reinterpret_cast<CMuleToolbarCtrl*>(dwRefData);

	switch (uMsg)
	{
	case WM_ERASEBKGND:
	{
		HDC    hdc = reinterpret_cast<HDC>(wParam);
		RECT   rc;
		::GetClientRect(hWnd, &rc);
		::FillRect(hdc, &rc, static_cast<HBRUSH>(CDarkMode::m_brDefault.GetSafeHandle()));
		return TRUE;
	}

	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORLISTBOX:
	case WM_CTLCOLORBTN:
	{
		HDC     hdc = reinterpret_cast<HDC>(wParam);
		::SetBkMode(hdc, TRANSPARENT);
		::SetTextColor(hdc, GetCustomSysColor(COLOR_WINDOWTEXT, true));
		COLORREF bk = (uMsg == WM_CTLCOLORBTN) ? GetCustomSysColor(COLOR_BTNFACE, true)	: GetCustomSysColor(COLOR_WINDOW, true);
		::SetBkColor(hdc, bk);
		return reinterpret_cast<INT_PTR>(CDarkMode::m_brDefault.GetSafeHandle());
	}

	case WM_DRAWITEM:
	{
		auto pDI = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
		if (pDI->CtlType != ODT_LISTBOX)
			break;

		HWND   hLB = pDI->hwndItem;
		HDC    hdc = pDI->hDC;
		RECT   rc = pDI->rcItem;
		HBRUSH hbrBg = static_cast<HBRUSH>(CDarkMode::m_brDefault.GetSafeHandle());

		// draw background
		::FillRect(hdc, &rc, hbrBg);
		if (pDI->itemState & ODS_SELECTED)
			::FillRect(hdc, &rc, static_cast<HBRUSH>(CDarkMode::m_brActiveCaption.GetSafeHandle()));

		// get item text
		TCHAR szItemText[256] = {};
		::SendMessage(hLB, LB_GETTEXT, pDI->itemID, reinterpret_cast<LPARAM>(szItemText)); // decide which list this is
		RECT rcDlg, rcLB;
		::GetWindowRect(hWnd, &rcDlg);
		::GetWindowRect(hLB, &rcLB);
		bool isAvailableButtonsList = rcLB.left < ((rcDlg.left + rcDlg.right) / 2);

		// drawing icon
		TBBUTTON btn = {};
		bool m_bDrawIcon = false;

		if (isAvailableButtonsList) {
			// compare TBStrings without ampersand
			for (int i = 0; i < pThis->m_buttoncount; ++i)	{
				CString m_strButtonLabel = pThis->TBStrings[i];
				m_strButtonLabel.Remove(_T('&'));
				if (m_strButtonLabel == CString(szItemText)) {
					btn = pThis->TBButtons[i];
					m_bDrawIcon = true;
					break;
				}
			}
		} else {
			int idx = static_cast<int>(pDI->itemID);
			LRESULT btnCount = pThis->SendMessage(TB_BUTTONCOUNT, 0, 0);
			m_bDrawIcon = (idx >= 0 && idx < btnCount);
			if (m_bDrawIcon)
				pThis->SendMessage(TB_GETBUTTON, idx, reinterpret_cast<LPARAM>(&btn));
		}

		if (m_bDrawIcon && !(btn.fsStyle & TBSTYLE_SEP)) {
			if (auto images = pThis->GetImageList()) {
				CDC dcMem;
				dcMem.Attach(hdc);
				int yOff = (rc.bottom - rc.top - pThis->m_sizBtnBmp.cy) / 2;
					SafeImageListDraw(images, &dcMem, btn.iBitmap, CPoint(rc.left + 2, rc.top + yOff), ILD_TRANSPARENT);
				dcMem.Detach();
			}
		}

		// draw text
		::SetBkMode(hdc, TRANSPARENT);
		::SetTextColor(hdc, GetCustomSysColor(COLOR_WINDOWTEXT, true));
		RECT rcText = rc;
		rcText.left += pThis->m_sizBtnBmp.cx + 6;
		::DrawText(hdc, szItemText,	-1,	&rcText, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

		// fill blank area under last item
		int count = static_cast<int>(::SendMessage(hLB, LB_GETCOUNT, 0, 0));
		if (count > 0 && pDI->itemID == count - 1) {
			RECT rcClient;
			::GetClientRect(hLB, &rcClient);
			RECT rcBlank = {
				rcClient.left,
				rc.bottom,
				rcClient.right,
				rcClient.bottom
			};
			::FillRect(hdc, &rcBlank, hbrBg);
		}

		return TRUE;
	}
	}

	return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void CMuleToolbarCtrl::OnNmCustomDraw(LPNMHDR pNMHDR, LRESULT* pResult)
{
	*pResult = CDRF_DODEFAULT; // Default behavior

	if (!IsDarkModeEnabled() || !pNMHDR) // Run only for dark mode and check if pNMHDR is valid
		return;

	LPNMTBCUSTOMDRAW pNMCD = reinterpret_cast<LPNMTBCUSTOMDRAW>(pNMHDR);
	if (!pNMCD ) // Check if pNMCD is valid
		return;

	// Determine the drawing stage
	switch (pNMCD->nmcd.dwDrawStage)
	{
	case CDDS_PREPAINT:
		FillRect(pNMCD->nmcd.hdc, &pNMCD->nmcd.rc, CDarkMode::m_brDefault); // Fill the rectangle
		*pResult = CDRF_NOTIFYITEMDRAW; // Notify that we want to draw items
		return;

	case CDDS_ITEMPREPAINT:
		FillRect(pNMCD->nmcd.hdc, &pNMCD->nmcd.rc, CDarkMode::m_brDefault); // Fill the rectangle
		pNMCD->clrBtnFace = GetCustomSysColor(COLOR_BTNFACE); // Set button color
		pNMCD->clrBtnHighlight = GetCustomSysColor(COLOR_HIGHLIGHT); // Set button highlight color
		pNMCD->clrText = (pNMCD->nmcd.lItemlParam & 1) ? GetCustomSysColor(COLOR_INACTIVECAPTIONTEXT) : GetCustomSysColor(COLOR_BTNTEXT); //Bit 0 = disabled // Set text colors
	}
}

void CMuleToolbarCtrl::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	// Do nothing
}
