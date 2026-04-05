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
#include "ClosableTabCtrl.h"
#include "OtherFunctions.h"
#include "MenuCmds.h"
#include "UserMsgs.h"
#include <algorithm>
#include "eMuleAI/DarkMode.h"
#include "eMuleAI/MenuXP.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define INDICATOR_WIDTH  4
#define INDICATOR_COLOR  COLOR_HOTLIGHT
#define METHOD           DSTINVERT

namespace
{
	CFont* SelectTabFont(CDC& dc, CFont* pBaseFont, bool bBold, CFont& boldFont)
	{
		if (pBaseFont == NULL)
			return NULL;

		if (!bBold)
			return dc.SelectObject(pBaseFont);

		LOGFONT lf = {};
		if (pBaseFont->GetLogFont(&lf) == 0)
			return dc.SelectObject(pBaseFont);

		lf.lfWeight = FW_BOLD;
		if (!boldFont.CreateFontIndirect(&lf))
			return dc.SelectObject(pBaseFont);

		return dc.SelectObject(&boldFont);
	}
}

#define _WM_THEMECHANGED		0x031A
#define _ON_WM_THEMECHANGED()													\
	{	_WM_THEMECHANGED, 0, 0, 0, AfxSig_l,									\
		(AFX_PMSG)(AFX_PMSGW)													\
		(static_cast<LRESULT (AFX_MSG_CALL CWnd::*)(void)>(_OnThemeChanged))	\
	},

///////////////////////////////////////////////////////////////////////////////
// CClosableTabCtrl

IMPLEMENT_DYNAMIC(CClosableTabCtrl, CTabCtrl)

BEGIN_MESSAGE_MAP(CClosableTabCtrl, CTabCtrl)
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_MBUTTONUP()
	ON_WM_CREATE()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_CONTEXTMENU()
	_ON_WM_THEMECHANGED()
	ON_WM_CTLCOLOR_REFLECT()
	ON_WM_CTLCOLOR()
	ON_WM_ERASEBKGND()
	ON_WM_MEASUREITEM()
	ON_WM_MEASUREITEM_REFLECT()
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_CAPTURECHANGED()
	ON_WM_PAINT()
	ON_WM_MOUSELEAVE()
	ON_WM_SIZE()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CClosableTabCtrl::CClosableTabCtrl()
	: m_bClosable(true)
	, m_bShowCloseButton(true)
	, m_iiCloseButton()
	, m_ptCtxMenu(-1, -1)
	, m_bDragging()
	, m_nSrcTab()
	, m_nDstTab()
	, m_bHotTracking()
	, m_bDownloadCategoryStyle(false)
	, m_pSpinCtrl()
{
}

CClosableTabCtrl::~CClosableTabCtrl()
{
	if (m_pSpinCtrl) {
		m_pSpinCtrl->Detach();
		delete m_pSpinCtrl;
	}
}

void CClosableTabCtrl::GetCloseButtonRect(int iItem, const CRect &rcItem, CRect &rcCloseButton, bool bItemSelected, bool bVistaThemeActive)
{
	rcCloseButton.top = rcItem.top + 2;
	rcCloseButton.bottom = rcCloseButton.top + (m_iiCloseButton.rcImage.bottom - m_iiCloseButton.rcImage.top);
	rcCloseButton.right = rcItem.right - 2;
	rcCloseButton.left = rcCloseButton.right - (m_iiCloseButton.rcImage.right - m_iiCloseButton.rcImage.left);
	if (bVistaThemeActive)
		rcCloseButton.left -= 1; // the close button does not look 'symmetric' with a width of 16, give it 17
	if (bItemSelected) {
		rcCloseButton.OffsetRect(-1, 0);
		if (bVistaThemeActive) {
			int iItems = GetItemCount();
			if (iItems > 1 && iItem == iItems - 1)
				rcCloseButton.OffsetRect(-2, 0);
		}
	} else {
		if (bVistaThemeActive) {
			int iItems = GetItemCount();
			if (iItems > 1 && iItem < iItems - 1)
				rcCloseButton.OffsetRect(2, 0);
		}
	}
}

int CClosableTabCtrl::GetTabUnderPoint(const CPoint &point) const
{
	CRect rcItem;
	for (int i = GetItemCount(); --i >= 0;)
		if (GetItemRect(i, rcItem)) {
			rcItem.InflateRect(2, 2); // get the real tab item rect
			if (rcItem.PtInRect(point))
				return i;
		}
	return -1;
}

int CClosableTabCtrl::GetTabUnderContextMenu() const
{
	return (m_ptCtxMenu.x == -1 || m_ptCtxMenu.y == -1) ? -1 : GetTabUnderPoint(m_ptCtxMenu);
}

bool CClosableTabCtrl::SetDefaultContextMenuPos()
{
	int iTab = GetCurSel();
	if (iTab >= 0) {
		CRect rcItem;
		if (GetItemRect(iTab, &rcItem)) {
			rcItem.InflateRect(2, 2); // get the real tab item rect
			m_ptCtxMenu.SetPoint(rcItem.left + rcItem.Width() / 2, (rcItem.top + rcItem.bottom) / 2);
			return true;
		}
	}
	return false;
}

void CClosableTabCtrl::OnMButtonUp(UINT nFlags, CPoint point)
{
	if (m_bClosable) {
		int iTab = GetTabUnderPoint(point);
		if (iTab >= 0) {
			GetParent()->SendMessage(UM_CLOSETAB, (WPARAM)iTab);
			return;
		}
	}

	CTabCtrl::OnMButtonUp(nFlags, point);
}


void CClosableTabCtrl::OnLButtonUp(UINT nFlags, CPoint point)
{
	CTabCtrl::OnLButtonUp(nFlags, point);

	if (m_bDragging) {
		// We're going to drop something now...

		// Stop the dragging process and release the mouse capture
		// This will eventually call our OnCaptureChanged which stops the dragging
		if (GetCapture() == this)
			::ReleaseCapture();

		// Modify the tab control style so that Hot Tracking is re-enabled
		if (m_bHotTracking)
			ModifyStyle(0, TCS_HOTTRACK);

		if (m_nSrcTab == m_nDstTab)
			return;

		// Inform Parent about Drag request
		NMHDR nmh;
		nmh.code = UM_TABMOVED;
		nmh.hwndFrom = GetSafeHwnd();
		nmh.idFrom = GetDlgCtrlID();
		GetParent()->SendMessage(WM_NOTIFY, nmh.idFrom, (LPARAM)&nmh);
	}
}

void CClosableTabCtrl::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	int iTab = GetTabUnderPoint(point);
	if (iTab >= 0)
		GetParent()->SendMessage(UM_DBLCLICKTAB, (WPARAM)iTab);
	else
		CTabCtrl::OnLButtonDblClk(nFlags, point);
}

// It would be nice if there would the option to restrict the maximum width of a tab control.
// We would need that feature actually for almost all our tab controls. Especially for the
// search results list - those tab control labels can get quite large. But I did not yet a
// find a way to limit the width of tabs. Although MSDN says that an owner drawn
// tab control receives a WM_MEASUREITEM, I never got one.

// Vista: This gets never called for an owner drawn tab control
void CClosableTabCtrl::OnMeasureItem(int iCtlId, LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	if (lpMeasureItemStruct && lpMeasureItemStruct->CtlType == ODT_MENU) {
		CMenuXPItem* pMenuItem = reinterpret_cast<CMenuXPItem*>(lpMeasureItemStruct->itemData);
		if (pMenuItem && pMenuItem->IsMyData()) {
			CMenuXP menuHelper;
			menuHelper.MeasureItem(lpMeasureItemStruct);
			return;
		}

		CWnd::OnMeasureItem(iCtlId, lpMeasureItemStruct);
		return;
	}
	__super::OnMeasureItem(iCtlId, lpMeasureItemStruct);
}

// Vista: This gets never called for an owner drawn tab control
void CClosableTabCtrl::MeasureItem(LPMEASUREITEMSTRUCT)
{
	TRACE2("CClosableTabCtrl::MeasureItem\n");
}

void CClosableTabCtrl::OnDestroy()
{
	// Ensure we release any attached spin control wrapper before the base destroys child HWNDs
	if (m_pSpinCtrl) {
		if (m_pSpinCtrl->m_hWnd)
			m_pSpinCtrl->Detach(); // make sure we do not keep a dangling association to a soon-to-die HWND
		delete m_pSpinCtrl;
		m_pSpinCtrl = nullptr;
	}

	CTabCtrl::OnDestroy();
}

// OnPaint will be called if the Dark Mode is active
void CClosableTabCtrl::OnPaint()
{
	// Call the default paint handler for non-dark mode
	if (!IsDarkModeEnabled()) {
		CTabCtrl::Default();
		return;
	}

	CatchSpinControl();

	CPaintDC dc(this); // Create a device context for painting
	CRect rect;
	GetClientRect(&rect); // Get the dimensions of the tab control

	const HWND hSpin = (m_pSpinCtrl != NULL) ? m_pSpinCtrl->GetSafeHwnd() : NULL;
	const bool bHasVisibleSpin = (hSpin != NULL) && ::IsWindow(hSpin) && ::IsWindowVisible(hSpin);
	CRect rcSpin;
	const int iSavedDc = dc.SaveDC();
	if (bHasVisibleSpin) {
		::GetWindowRect(hSpin, &rcSpin);
		ScreenToClient(&rcSpin);
		dc.ExcludeClipRect(&rcSpin);
	}

	// Fill the background without painting over the tab scroll spin control.
	dc.FillSolidRect(&rect, GetCustomSysColor(COLOR_WINDOW));

	// Iterate through each tab item and draw it
	int tabCount = GetItemCount();
	for (int nTabIndex = 0; nTabIndex < tabCount; nTabIndex++) {
		TCHAR szLabel[256];
		TC_ITEM tci;
		tci.pszText = szLabel;
		tci.cchTextMax = _countof(szLabel);
		if (m_bDownloadCategoryStyle) {
			tci.mask = TCIF_TEXT | TCIF_PARAM;
			szLabel[_countof(szLabel) - 1] = _T('\0');
		} else {
			tci.mask = TCIF_TEXT | TCIF_IMAGE | TCIF_STATE;
			tci.dwStateMask = TCIS_HIGHLIGHTED;
		}

		if (!GetItem(nTabIndex, &tci)) // Get the tab item information
			continue; // Skip if the item could not be retrieved

		// Get the rectangle for the current tab item
		CRect rcItem, rcItemText;
		GetItemRect(nTabIndex, rcItem);
		rcItemText = rcItem;

		bool bSelected = (nTabIndex == GetCurSel()); // Determine if the tab is selected
		bool bHovered = (nTabIndex == m_nHoverTabIndex); // Determine if the tab is hovered

		// Set background color for selected, hovered, and normal tabs
		COLORREF bgColor;
		if (bHovered)
			bgColor = GetCustomSysColor(COLOR_GRADIENTACTIVECAPTION);
		else if (bSelected)
			bgColor = GetCustomSysColor(COLOR_ACTIVECAPTION);
		else
			bgColor = GetCustomSysColor(COLOR_BTNFACE);

		dc.FillSolidRect(&rcItem, bgColor);

		// Draw the icon if it exists
		CImageList* pImageList = GetImageList();
		if (pImageList && tci.iImage >= 0) {
			SafeImageListDraw(pImageList, &dc, tci.iImage, CPoint(rcItem.left + 4, rcItem.top + 2), ILD_TRANSPARENT);
			if (!m_bClosable || !m_bShowCloseButton) // Closable tabs in search window already have a padding
				rcItemText.left += 20; // Move the text position to the right of the icon
		}

		// Emphasize the active download category tab without affecting other tabs.
		CFont boldFont;
		CFont* pOldFont = SelectTabFont(dc, GetFont(), m_bDownloadCategoryStyle && bSelected, boldFont);
		int iOldBkMode = dc.SetBkMode(TRANSPARENT);
		COLORREF crOldColor;

		if (m_bDownloadCategoryStyle) {
			if (tci.lParam != -1)
				crOldColor = dc.SetTextColor((COLORREF)tci.lParam);
			else
				crOldColor = CLR_NONE;
		} else {
			if (tci.dwState & TCIS_HIGHLIGHTED)
				crOldColor = dc.SetTextColor(GetCustomSysColor(COLOR_HOTLIGHT)); // Highlighted color
			else if (bSelected)
				crOldColor = dc.SetTextColor(GetCustomSysColor(COLOR_BTNTEXT)); // Normal text color
			else 
				crOldColor = dc.SetTextColor(GetCustomSysColor(COLOR_INACTIVECAPTIONTEXT)); // Inactive tab text color
		}

		// Draw the tab label
		rcItemText.top += bSelected ? 4 : 3; // Adjust position based on selection
		dc.DrawText(szLabel, rcItemText, DT_SINGLELINE | DT_TOP | DT_CENTER);

		// Restore previous settings
		dc.SetTextColor(crOldColor);
		dc.SetBkMode(iOldBkMode);
		if (pOldFont != NULL)
			dc.SelectObject(pOldFont);

		// Draw 'Close button' for the tab if closable
		bool bShowCloseButton = m_bClosable && m_bShowCloseButton; // Initialize the value with the control default. Tabs can still have different values.
		if (bShowCloseButton && GetParent()->SendMessage(UM_QUERYTAB, nTabIndex))
			bShowCloseButton = false;

		if (bShowCloseButton && m_ImgLstCloseButton.m_hImageList) {
			CRect rcCloseButton;
			GetCloseButtonRect(nTabIndex, rcItem, rcCloseButton, bSelected, false);
			SafeImageListDraw(&m_ImgLstCloseButton, &dc, static_cast<int>(!bSelected && true), rcCloseButton.TopLeft(), ILD_TRANSPARENT);
		}

		// Draw a black vertical line to separate each tab
		if (nTabIndex < tabCount - 1) // No line after the last tab
			dc.FillSolidRect(rcItem.right - 2, rcItem.top, 2, rcItem.Height(), GetCustomSysColor(COLOR_WINDOW)); // Draw 2-pixel black line

		// Draw a line at the top of the selected tab
		if (bSelected)
			dc.FillSolidRect(rcItem.left, rcItem.top, rcItem.Width() - 1, 2, GetCustomSysColor(COLOR_TABBORDER)); // Draw 2-pixel medium slate blue line
	}

	dc.RestoreDC(iSavedDc);

	if (bHasVisibleSpin)
		::RedrawWindow(hSpin, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_FRAME);
}

// DrawItem will be called if the Light Mode is active
void CClosableTabCtrl::DrawItem(LPDRAWITEMSTRUCT lpDIS)
{
	int nTabIndex = lpDIS->itemID;
	if (nTabIndex < 0)
		return;

	TCHAR szLabel[256];
	TC_ITEM tci;
	tci.pszText = szLabel;
	tci.cchTextMax = _countof(szLabel);
	if (m_bDownloadCategoryStyle) {
		tci.mask = TCIF_TEXT | TCIF_PARAM;
		szLabel[_countof(szLabel) - 1] = _T('\0');
	} else {
		tci.mask = TCIF_TEXT | TCIF_IMAGE | TCIF_STATE;
		tci.dwStateMask = TCIS_HIGHLIGHTED;
	}
	if (!GetItem(nTabIndex, &tci))
		return;

	CDC *pDC = CDC::FromHandle(lpDIS->hDC);
	if (!pDC)
		return;

	CRect rcItem(lpDIS->rcItem);
	CRect rcFullItem(rcItem);
	bool bSelected = (lpDIS->itemState & ODS_SELECTED) != 0;

	pDC->FillSolidRect(rcItem, GetSysColor(COLOR_BTNFACE));
	CFont boldFont;
	CFont* pOldFont = SelectTabFont(*pDC, GetFont(), m_bDownloadCategoryStyle && bSelected, boldFont);
	int iOldBkMode = pDC->SetBkMode(TRANSPARENT);
	COLORREF crOldColor;

	if (m_bDownloadCategoryStyle) {
		if (tci.lParam != -1)
			crOldColor = pDC->SetTextColor((COLORREF)tci.lParam);
		else
			crOldColor = CLR_NONE;
	} else {
		// Draw image on left side
		CImageList* piml = GetImageList();
		if (CanSafeDrawImageList(piml, pDC, tci.iImage)) {
			IMAGEINFO ii;
			piml->GetImageInfo(0, &ii);
			rcItem.left += bSelected ? 8 : 4;
			SafeImageListDraw(piml, pDC, tci.iImage, POINT{ rcItem.left, rcItem.top + 2 }, ILD_TRANSPARENT);
			rcItem.left += (ii.rcImage.right - ii.rcImage.left + 1);
			if (!bSelected)
				rcItem.left += 4;
		}

		bool bShowCloseButton = m_bClosable && m_bShowCloseButton;
		if (bShowCloseButton && GetParent()->SendMessage(UM_QUERYTAB, nTabIndex))
			bShowCloseButton = false;

		// Draw 'Close button' at right side
		if (bShowCloseButton && m_ImgLstCloseButton.m_hImageList) {
			CRect rcCloseButton;
			GetCloseButtonRect(nTabIndex, rcItem, rcCloseButton, bSelected, false);
			SafeImageListDraw(&m_ImgLstCloseButton, pDC, static_cast<int>(!bSelected && true), rcCloseButton.TopLeft(), ILD_TRANSPARENT);

			rcItem.right = rcCloseButton.left - 2;
			if (bSelected)
				rcItem.left += 2;
		}

		if (tci.dwState & TCIS_HIGHLIGHTED)
			crOldColor = pDC->SetTextColor(RGB(192, 0, 0));
		else
			crOldColor = pDC->SetTextColor(GetCustomSysColor(COLOR_BTNTEXT));
	}

	rcItem.top += bSelected ? 4 : 3;
	// Vista: Tab control has troubles with determining the width of a tab if the
	// label contains one '&' character. To get around this, we use the old code which
	// replaces one '&' character with two '&' characters and we do not specify DT_NOPREFIX
	// here when drawing the text.
	//
	// Vista: "DrawThemeText" can not be used in case we need a certain foreground color. Thus we always us
	// "DrawText" to always get the same font and metrics (just for safety).
	pDC->DrawText(szLabel, rcItem, DT_SINGLELINE | DT_TOP | DT_CENTER /*| DT_NOPREFIX*/);

	if (crOldColor != CLR_NONE)
		pDC->SetTextColor(crOldColor);
	pDC->SetBkMode(iOldBkMode);
	if (pOldFont != NULL)
		pDC->SelectObject(pOldFont);
}

void CClosableTabCtrl::PreSubclassWindow()
{
	CTabCtrl::PreSubclassWindow();
	InternalInit();
}

int CClosableTabCtrl::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CTabCtrl::OnCreate(lpCreateStruct) == -1)
		return -1;
	InternalInit();
	return 0;
}

void CClosableTabCtrl::InternalInit()
{
	ModifyStyle(0, TCS_OWNERDRAWFIXED);
	SetAllIcons();
}

void CClosableTabCtrl::OnSysColorChange()
{
	CTabCtrl::OnSysColorChange();
	SetAllIcons();
}

void CClosableTabCtrl::SetAllIcons()
{
	if (m_bClosable) {
		const int iIconWidth = 16;
		const int iIconHeight = 16;
		m_ImgLstCloseButton.DeleteImageList();
		m_ImgLstCloseButton.Create(iIconWidth, iIconHeight, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
		m_ImgLstCloseButton.Add(CTempIconLoader(_T("CloseTabSelected"), iIconWidth, iIconHeight));
		m_ImgLstCloseButton.Add(CTempIconLoader(_T("CloseTab"), iIconWidth, iIconHeight));
		m_ImgLstCloseButton.GetImageInfo(0, &m_iiCloseButton);
		Invalidate();
	}
}

// Handler that is called when the left mouse button is activated.
// The handler examines whether we have initiated a drag 'n drop process.
void CClosableTabCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (DragDetectPlus(this, point)) {
		// Yes, we're beginning to drag, so capture the mouse...
		m_bDragging = true;

		// Find and remember the source tab (the one we're going to move/drag 'n drop)
		TCHITTESTINFO hitinfo;
		hitinfo.pt = point;
		m_nSrcTab = HitTest(&hitinfo);
		m_nDstTab = m_nSrcTab;

		// Reset insert indicator
		m_InsertPosRect.SetRectEmpty();
		DrawIndicator(point);
		SetCapture();
	} else {
		if (m_bClosable && m_bShowCloseButton) {
			int iTab = GetTabUnderPoint(point);
			if (iTab >= 0) {
				CRect rcItem;
				GetItemRect(iTab, rcItem);
				rcItem.InflateRect(2, 2); // get the real tab item rect

				CRect rcCloseButton;
				GetCloseButtonRect(iTab, rcItem, rcCloseButton, iTab == GetCurSel(), false);

				// The visible part of our close icon is one pixel less on each side
				rcCloseButton.InflateRect(-1, -1);

				if (rcCloseButton.PtInRect(point)) {
					GetParent()->SendMessage(UM_CLOSETAB, (WPARAM)iTab);
					return;
				}
			}
		}

		CatchSpinControl();
		CTabCtrl::OnLButtonDown(nFlags, point);
	}

	// Note: We're not calling the base classes CTabCtrl::OnLButtonDown
	//       every time, because we want to be able to drag a tab without
	//       actually selecting it first (so that it gets the focus).
}

void CClosableTabCtrl::OnContextMenu(CWnd*, CPoint point)
{
	//This menu will be created manually where this class is inherited.Otherwise unwanted context menus with close option popups.
}

BOOL CClosableTabCtrl::OnCommand(WPARAM wParam, LPARAM lParam)
{
	if (wParam == MP_REMOVE) {
		if (m_ptCtxMenu.x != -1 && m_ptCtxMenu.y != -1) {
			int iTab = GetTabUnderPoint(m_ptCtxMenu);
			if (iTab >= 0) {
				GetParent()->SendMessage(UM_CLOSETAB, (WPARAM)iTab);
				return TRUE;
			}
		}
	}
	return CTabCtrl::OnCommand(wParam, lParam);
}

LRESULT CClosableTabCtrl::_OnThemeChanged()
{
	// Owner drawn tab control seems to have troubles with updating itself due to an XP theme change.
	ModifyStyle(TCS_OWNERDRAWFIXED, 0);	// Reset control style to not-owner drawn
	Default();							// Process original WM_THEMECHANGED message
	ModifyStyle(0, TCS_OWNERDRAWFIXED);	// Apply owner drawn style again
	return 0;
}

// Vista: This gets never called for an owner drawn tab control
HBRUSH CClosableTabCtrl::CtlColor(CDC*, UINT)
{
	// Change any attributes of the DC here
	// Return a non-NULL brush if the parent's handler should not be called
	return NULL;
}

// Vista: This gets never called for an owner drawn tab control
HBRUSH CClosableTabCtrl::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = CTabCtrl::OnCtlColor(pDC, pWnd, nCtlColor);
	// Change any attributes of the DC here
	// Return a different brush if the default is not desired
	return hbr;
}

// Vista: Can not be used to workaround the problems with owner drawn tab control
BOOL CClosableTabCtrl::OnEraseBkgnd(CDC* pDC)
{
	if (IsDarkModeEnabled())
		return TRUE;

	return CTabCtrl::OnEraseBkgnd(pDC);
}

BOOL CClosableTabCtrl::DeleteItem(int nItem)
{
	// if we remove a tab which would lead to scrolling back to other tabs, all those become hidden for... whatever reasons
	// its easy enough to work around by scrolling to the first visible tab _before_ we delete the other one
	SetCurSel(0);
	CatchSpinControl();
	return __super::DeleteItem(nItem);
}

void CClosableTabCtrl::OnSize(UINT nType, int cx, int cy)
{
	CTabCtrl::OnSize(nType, cx, cy);
	CatchSpinControl();
}

LONG CClosableTabCtrl::InsertItem(int nItem, TCITEM* pTabCtrlItem)
{
	LONG m_lRetVal = __super::InsertItem(nItem, pTabCtrlItem);
	CatchSpinControl();
	return m_lRetVal;
}

LONG CClosableTabCtrl::InsertItem(int nItem, LPCTSTR lpszItem)
{
	LONG m_lRetVal = __super::InsertItem(nItem, lpszItem);
	CatchSpinControl();
	return m_lRetVal;
}

LONG CClosableTabCtrl::InsertItem(int nItem, LPCTSTR lpszItem, int nImage)
{
	LONG m_lRetVal = __super::InsertItem(nItem, lpszItem, nImage);
	CatchSpinControl();
	return m_lRetVal;
}

void CClosableTabCtrl::CatchSpinControl()
{
	// SpinControl will be created when tabs filled the tabbar and when it is created we need to catch it and switch its dark mode. 
	// Recreate the wrapper whenever the tab control destroys and recreates its scroll buttons.
	CWnd* pWnd = FindWindowEx(GetSafeHwnd(), 0, UPDOWN_CLASS, 0);
	if (pWnd == NULL) {
		if (m_pSpinCtrl != NULL && (!::IsWindow(m_pSpinCtrl->GetSafeHwnd()) || ::GetParent(m_pSpinCtrl->GetSafeHwnd()) != GetSafeHwnd())) {
			if (m_pSpinCtrl->m_hWnd)
				m_pSpinCtrl->Detach();
			delete m_pSpinCtrl;
			m_pSpinCtrl = NULL;
		}
		return;
	}

	HWND hSpin = pWnd->GetSafeHwnd();
	if (m_pSpinCtrl != NULL) {
		if (m_pSpinCtrl->GetSafeHwnd() == hSpin)
			return;

		if (m_pSpinCtrl->m_hWnd)
			m_pSpinCtrl->Detach();
		delete m_pSpinCtrl;
		m_pSpinCtrl = NULL;
	}

	m_pSpinCtrl = new CSpinButtonCtrl;
	m_pSpinCtrl->Attach(hSpin);
	if (IsDarkModeEnabled())
		ApplyThemeToWindow(hSpin, true, true);
}

void CClosableTabCtrl::OnMouseLeave()
{
	// Invalidate only the currently hovered tab
	if (m_nHoverTabIndex != -1) {
		CRect tabRect;
		GetItemRect(m_nHoverTabIndex, &tabRect); // Get the rectangle of the hovered tab
		InvalidateRect(tabRect, FALSE); // Invalidate only that tab
	}

	m_nHoverTabIndex = -1; // Reset hover index
	CTabCtrl::OnMouseLeave(); // Call base class implementation
}

// to, when in a drag 'n drop process, to:
//
//  1) Draw the drop indicator of where the tab can be inserted.
//  2) Possible scroll the tab so more tabs is viewed.
void CClosableTabCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
    // Hover effect implementation
    TCHITTESTINFO hitTestInfo = { 0 };
    hitTestInfo.pt = point;
    int nTabIndex = HitTest(&hitTestInfo);

    if (nTabIndex != m_nHoverTabIndex) {
        // Invalidate only the previous hovered tab
        if (m_nHoverTabIndex != -1) {
            CRect tabRect;
            GetItemRect(m_nHoverTabIndex, &tabRect);
            InvalidateRect(tabRect, FALSE);
        }

        m_nHoverTabIndex = nTabIndex;

        // Invalidate only the newly hovered tab
        if (m_nHoverTabIndex != -1) {
            CRect newTabRect;
            GetItemRect(m_nHoverTabIndex, &newTabRect);
            InvalidateRect(newTabRect, FALSE);
        }

        // Start tracking for mouse leave event
        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = m_hWnd;
        TrackMouseEvent(&tme);
    }

	CTabCtrl::OnMouseMove(nFlags, point);

    // Check if the left button is released
    if (!(nFlags & MK_LBUTTON)) {
        m_bDragging = false;
    }

    if (m_bDragging) {
        // Draw the indicator
        DrawIndicator(point);

        RECT rcClient;
        GetClientRect(&rcClient);

        if (m_pSpinCtrl) {
            // Scroll left if needed
            if (point.x < rcClient.left) {
                int nPos = LOWORD(m_pSpinCtrl->GetPos());
                if (nPos > 0) {
                    InvalidateRect(&m_InsertPosRect, FALSE);
                    m_InsertPosRect.SetRectEmpty();
                    SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, nPos - 1), 0);
                }
            }

            // Scroll right if needed
            if (point.x > rcClient.right && m_pSpinCtrl->IsWindowVisible()) {
                InvalidateRect(&m_InsertPosRect, FALSE);
                m_InsertPosRect.SetRectEmpty();
                int nPos = LOWORD(m_pSpinCtrl->GetPos());
                SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, nPos + 1), 0);
            }
        }
    }
}

// Handler that is called when the WM_CAPTURECHANGED message is received. It notifies
// us that we do not longer capture the mouse. Therefore we must stop or drag 'n drop
// process. Clean up code etc.
void CClosableTabCtrl::OnCaptureChanged(CWnd*)
{
	if (m_bDragging) {
		m_bDragging = false;

		// Remove the indicator by invalidate the rectangle (forces repaint)
		InvalidateRect(&m_InsertPosRect);

		// If a drag image is in play this probably should be cleaned up here.
		// ...
	}
}

// Utility member function to draw the (drop) indicator of where the tab will be inserted.
// Specifies a position (e.g. the mouse pointer position) which
// will be used to determine whether the indicator should be
// painted to the left or right of the indicator.
bool CClosableTabCtrl::DrawIndicator(CPoint point)
{
	TCHITTESTINFO hitinfo;
	hitinfo.pt = point;

	// Adjust position to top of tab control (allow the mouse the actually
	// be outside the top of tab control and still be able to find the right
	// tab index
	CRect rcItem;
	if (GetItemRect(0, &rcItem))
		hitinfo.pt.y = rcItem.top;

	// If the position is inside the rectangle where tabs are visible we
	// can safely draw the insert indicator...
	unsigned int nTab = HitTest(&hitinfo);
	if (hitinfo.flags != TCHT_NOWHERE)
		m_nDstTab = nTab;
	else if (m_nDstTab == (UINT)GetItemCount())
		--m_nDstTab;

	GetItemRect(m_nDstTab, &rcItem);
	CRect newInsertPosRect(rcItem.left - 1, rcItem.top, rcItem.left - 1 + INDICATOR_WIDTH, rcItem.bottom);

	// Determine whether the indicator should be painted at the right of
	// the tab - in which case we update the indicator position and the
	// destination tab...
	if (point.x >= rcItem.right - rcItem.Width() / 2) {
		newInsertPosRect.MoveToX(rcItem.right - 1);
		++m_nDstTab;
	}

	if (newInsertPosRect != m_InsertPosRect) {
		// Remove the current indicator by invalidate the rectangle (forces repaint)
		InvalidateRect(&m_InsertPosRect);

		// Update to new insert indicator position...
		m_InsertPosRect = newInsertPosRect;
	}

	// Create a simple device context in which we initialize the pen and brush
	// that we will use for drawing the new indicator...
	CClientDC dc(this);

	CBrush brush(GetCustomSysColor(INDICATOR_COLOR));
	CPen pen(PS_SOLID, 1, GetCustomSysColor(INDICATOR_COLOR));

	CBrush* pOldBrush = dc.SelectObject(&brush);
	CPen* pOldPen = dc.SelectObject(&pen);

	// Draw the insert indicator
	dc.Rectangle(m_InsertPosRect);

	dc.SelectObject(pOldPen);
	dc.SelectObject(pOldBrush);

	return true; // success
}

// Reorders the tab by moving the source tab to the position of the destination tab.
BOOL CClosableTabCtrl::ReorderTab(unsigned int nSrcTab, unsigned int nDstTab)
{
	if (nSrcTab == nDstTab)
		return TRUE; // Return success (we have nothing to do)

	// Remember the current selected tab
	unsigned int nSelectedTab = GetCurSel();

	// Get information from the tab to move (to be deleted)
	TCHAR sBuffer[256];
	TCITEM item;
	item.mask = TCIF_IMAGE | TCIF_PARAM | TCIF_TEXT; //| TCIF_STATE;
	item.pszText = sBuffer;
	item.cchTextMax = _countof(sBuffer);

	BOOL bOK = GetItem(nSrcTab, &item);
	sBuffer[_countof(sBuffer) - 1] = _T('\0');
	ASSERT(bOK);

	bOK = DeleteItem(nSrcTab);
	ASSERT(bOK);

	// Insert it at new location
	bOK = InsertItem(nDstTab - static_cast<unsigned>(m_nDstTab > m_nSrcTab), &item);

	// Setup new selected tab
	SetCurSel(nDstTab - static_cast<unsigned>(m_nDstTab > m_nSrcTab));

	CatchSpinControl();

	// Force update of tab control
	// Necessary to do so that notified clients ('users') - by selection change call
	// below - can draw the tab contents in correct tab.
	UpdateWindow();

	NMHDR nmh;
	nmh.hwndFrom = GetSafeHwnd();
	nmh.idFrom = GetDlgCtrlID();
	nmh.code = TCN_SELCHANGE;
	GetParent()->SendMessage(WM_NOTIFY, nmh.idFrom, (LPARAM)&nmh);

	return bOK;
}

BOOL CClosableTabCtrl::DragDetectPlus(CWnd* Handle, CPoint p)
{
	BOOL bResult = FALSE;
	Handle->ClientToScreen(&p);
	CRect DragRect(p, p);
	InflateRect(DragRect, ::GetSystemMetrics(SM_CXDRAG), ::GetSystemMetrics(SM_CYDRAG));
	BOOL bDispatch = TRUE;
	Handle->SetCapture();
	while (!bResult && bDispatch) {
		MSG Msg;
		if (::PeekMessage(&Msg, *Handle, 0, 0, PM_REMOVE))
			switch (Msg.message) {
			case WM_MOUSEMOVE:
				bResult = !DragRect.PtInRect(Msg.pt);
				break;
			case WM_RBUTTONUP:
			case WM_LBUTTONUP:
			case WM_CANCELMODE:
				bDispatch = FALSE;
				break;
			case WM_QUIT:
				::ReleaseCapture();
				return FALSE;
			default:
				::TranslateMessage(&Msg);
				::DispatchMessage(&Msg);
			}
		else
			::Sleep(0);
	}
	::ReleaseCapture();
	return bResult;
}

void CClosableTabCtrl::SetTabTextColor(int index, DWORD color)
{
	TCITEM tab;
	tab.mask = TCIF_PARAM;
	if (GetItem(index, &tab)) {
		tab.lParam = color;
		SetItem(index, &tab);
	}
}
