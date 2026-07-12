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
#include "TreeOptionsCtrlEx.h"
#include "UserMsgs.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


BEGIN_MESSAGE_MAP(CTreeOptionsCtrlEx, CTreeOptionsCtrl)
	ON_WM_DESTROY()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_CAPTURECHANGED()
	ON_NOTIFY_REFLECT_EX(NM_CUSTOMDRAW, OnCustomDraw)
END_MESSAGE_MAP()

CTreeOptionsCtrlEx::CTreeOptionsCtrlEx(UINT uImageListColorFlags)
	: m_hHotCommandButton(NULL)
	, m_hPressedCommandButton(NULL)
	, m_bTrackingCommandButtonMouse(FALSE)
{
	m_uImageListColorFlags = uImageListColorFlags;
	SetToggleOverIconOnly(TRUE);
}

HTREEITEM CTreeOptionsCtrlEx::InsertGroup(LPCTSTR lpszItem, int nImage, HTREEITEM hParent, HTREEITEM hAfter, DWORD dwItemData)
{
	if (nImage <= 9)
		nImage = TREEOPTSCTRLIMG_GROUP;
	return CTreeOptionsCtrl::InsertGroup(lpszItem, nImage, hParent, hAfter, dwItemData);
}

HTREEITEM CTreeOptionsCtrlEx::InsertCommandButton(LPCTSTR lpszItem, HTREEITEM hParent, HTREEITEM hAfter, DWORD dwItemData)
{
	ASSERT((hParent == TVI_ROOT) || IsGroup(hParent) || IsCheckBox(hParent));

	HTREEITEM hItem = InsertItem(lpszItem, TREEOPTSCTRLIMG_COMMAND, TREEOPTSCTRLIMG_COMMAND, hParent, hAfter);
	CTreeOptionsItemData *pItemData = new CTreeOptionsItemData;
	pItemData->m_pRuntimeClass1 = NULL;
	pItemData->m_Type = CTreeOptionsItemData::CommandButton;
	pItemData->m_dwItemData = dwItemData;
	SetItemData(hItem, (DWORD_PTR)pItemData);

	int iDpiY = 96;
	CDC *pDC = GetDC();
	if (pDC != NULL) {
		iDpiY = pDC->GetDeviceCaps(LOGPIXELSY);
		ReleaseDC(pDC);
	}
	SetItemMinHeight(hItem, MulDiv(23, iDpiY, 96));

	return hItem;
}

BOOL CTreeOptionsCtrlEx::IsCommandButton(HTREEITEM hItem)
{
	CTreeOptionsItemData *pItemData = hItem != NULL ? reinterpret_cast<CTreeOptionsItemData*>(GetItemData(hItem)) : NULL;
	return pItemData != NULL && pItemData->m_Type == CTreeOptionsItemData::CommandButton;
}

HTREEITEM CTreeOptionsCtrlEx::HitTestCommandButton(CPoint point)
{
	for (HTREEITEM hVisible = GetFirstVisibleItem(); hVisible != NULL; hVisible = GetNextVisibleItem(hVisible)) {
		if (IsCommandButton(hVisible)) {
			CRect rcButton = GetCommandButtonRect(hVisible);
			if (rcButton.PtInRect(point))
				return hVisible;
		}

		CRect rcItem;
		if (GetItemRect(hVisible, rcItem, FALSE) && rcItem.top > point.y)
			break;
	}

	return NULL;
}

void CTreeOptionsCtrlEx::InvalidateCommandButton(HTREEITEM hItem)
{
	if (hItem == NULL)
		return;

	CRect rcButton = GetCommandButtonRect(hItem);
	if (!rcButton.IsRectEmpty())
		InvalidateRect(rcButton, FALSE);
}

CRect CTreeOptionsCtrlEx::GetCommandButtonRect(HTREEITEM hItem)
{
	CRect rcText;
	if (hItem == NULL || !GetItemRect(hItem, rcText, TRUE))
		return CRect(0, 0, 0, 0);

	CRect rcItem = GetItemReservedRect(hItem);
	if (rcItem.IsRectEmpty())
		return CRect(0, 0, 0, 0);

	CString strText = GetItemText(hItem);
	int iButtonWidth = 80;
	CDC *pDC = GetDC();
	if (pDC != NULL) {
		CFont *pFont = GetFont();
		CFont *pOldFont = pFont != NULL ? pDC->SelectObject(pFont) : NULL;
		CSize sizeText = pDC->GetTextExtent(strText, strText.GetLength());
		iButtonWidth = sizeText.cx + 22;
		if (pOldFont != NULL)
			pDC->SelectObject(pOldFont);
		ReleaseDC(pDC);
	}

	int iButtonLeft = rcText.left - static_cast<int>(GetIndent());
	if (iButtonLeft < rcItem.left)
		iButtonLeft = rcItem.left;
	if (iButtonLeft > rcText.left)
		iButtonLeft = rcText.left;

	int iDpiY = 96;
	CDC *pDCHeight = GetDC();
	if (pDCHeight != NULL) {
		iDpiY = pDCHeight->GetDeviceCaps(LOGPIXELSY);
		ReleaseDC(pDCHeight);
	}

	const int iMinButtonHeight = MulDiv(23, iDpiY, 96);
	int iButtonHeight = min(iMinButtonHeight, rcItem.Height() - 2);
	if (iButtonHeight < 1)
		iButtonHeight = rcItem.Height();

	CRect rcButton(iButtonLeft, rcItem.top + 1, iButtonLeft + iButtonWidth, rcItem.top + 1 + iButtonHeight);
	if (rcButton.bottom > rcItem.bottom - 1)
		rcButton.bottom = rcItem.bottom - 1;
	if (rcButton.right > rcItem.right - 2)
		rcButton.right = rcItem.right - 2;
	return rcButton;
}

void CTreeOptionsCtrlEx::DrawCommandButton(CDC &dc, HTREEITEM hItem)
{
	CRect rcButton = GetCommandButtonRect(hItem);
	if (rcButton.IsRectEmpty())
		return;

	const bool bDarkMode = IsDarkModeEnabled();
	const bool bSelected = (GetItemState(hItem, TVIS_SELECTED) & TVIS_SELECTED) != 0;
	const COLORREF crItemBackground = bDarkMode ? GetCustomSysColor(bSelected ? COLOR_HIGHLIGHT : COLOR_WINDOW) : GetSysColor(bSelected ? COLOR_HIGHLIGHT : COLOR_WINDOW);
	const bool bHot = hItem == m_hHotCommandButton;
	const bool bPressed = hItem == m_hPressedCommandButton;
	const bool bEnabled = IsWindowEnabled() != FALSE;
	const COLORREF crText = bDarkMode ? GetCustomSysColor(bEnabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT, true) : GetSysColor(bEnabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT);

	CRect rcItemText;
	CRect rcItem = GetItemReservedRect(hItem);
	if (GetItemRect(hItem, rcItemText, TRUE) && !rcItem.IsRectEmpty()) {
		CRect rcErase(rcButton.left, rcItem.top, max(rcButton.right, rcItemText.right + 2), rcItem.bottom);
		dc.FillSolidRect(rcErase, crItemBackground);
	}

	const int iStateId = !bEnabled ? PBS_DISABLED : bPressed ? PBS_PRESSED : bHot ? PBS_HOT : PBS_NORMAL;
	bool bDrawn = false;
	if (IsThemeActive() && IsAppThemed()) {
		HTHEME hTheme = OpenThemeData(m_hWnd, L"BUTTON");
		if (hTheme != NULL) {
			if (IsThemeBackgroundPartiallyTransparent(hTheme, BP_PUSHBUTTON, iStateId))
				DrawThemeParentBackground(m_hWnd, dc.GetSafeHdc(), &rcButton);
			DrawThemeBackground(hTheme, dc.GetSafeHdc(), BP_PUSHBUTTON, iStateId, &rcButton, NULL);
			CloseThemeData(hTheme);
			bDrawn = true;
		}
	}

	if (!bDrawn) {
		const COLORREF crFill = bDarkMode ? GetCustomSysColor(bPressed ? COLOR_3DLIGHT : bHot ? COLOR_BTNHIGHLIGHT : COLOR_BTNFACE, true) : GetSysColor(bPressed ? COLOR_3DLIGHT : bHot ? COLOR_BTNHIGHLIGHT : COLOR_BTNFACE);
		const COLORREF crBorder = bDarkMode ? GetCustomSysColor(bEnabled ? COLOR_GRAYTEXT : COLOR_INACTIVEBORDER, true) : GetSysColor(bEnabled ? COLOR_GRAYTEXT : COLOR_INACTIVEBORDER);
		CBrush brFill(crFill);
		CPen penBorder(PS_SOLID, 1, crBorder);
		CBrush *pOldBrush = dc.SelectObject(&brFill);
		CPen *pOldPen = dc.SelectObject(&penBorder);
		dc.RoundRect(rcButton, CPoint(4, 4));
		if (pOldPen != NULL)
			dc.SelectObject(pOldPen);
		if (pOldBrush != NULL)
			dc.SelectObject(pOldBrush);
	}

	CRect rcButtonText(rcButton);
	rcButtonText.DeflateRect(2, 1);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(crText);
	CString strText = GetItemText(hItem);
	dc.DrawText(strText, rcButtonText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void CTreeOptionsCtrlEx::OnLButtonDown(UINT nFlags, CPoint point)
{
	HTREEITEM hItem = HitTestCommandButton(point);
	if (IsCommandButton(hItem)) {
		SelectItem(hItem);
		SetFocus();
		m_hPressedCommandButton = hItem;
		SetCapture();
		InvalidateCommandButton(hItem);
		return;
	}

	CTreeOptionsCtrl::OnLButtonDown(nFlags, point);
}

void CTreeOptionsCtrlEx::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_hPressedCommandButton != NULL) {
		HTREEITEM hPressed = m_hPressedCommandButton;
		m_hPressedCommandButton = NULL;
		if (GetCapture() == this)
			ReleaseCapture();

		InvalidateCommandButton(hPressed);
		if (hPressed == HitTestCommandButton(point))
			NotifyParent(BN_CLICKED, hPressed);
		return;
	}

	CTreeOptionsCtrl::OnLButtonUp(nFlags, point);
}

void CTreeOptionsCtrlEx::OnMouseMove(UINT nFlags, CPoint point)
{
	HTREEITEM hHot = HitTestCommandButton(point);
	if (hHot != m_hHotCommandButton) {
		HTREEITEM hOldHot = m_hHotCommandButton;
		m_hHotCommandButton = hHot;
		InvalidateCommandButton(hOldHot);
		InvalidateCommandButton(m_hHotCommandButton);
	}

	if (!m_bTrackingCommandButtonMouse) {
		TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hWnd, 0 };
		m_bTrackingCommandButtonMouse = ::TrackMouseEvent(&tme) != FALSE;
	}

	CTreeOptionsCtrl::OnMouseMove(nFlags, point);
}

void CTreeOptionsCtrlEx::OnMouseLeave()
{
	m_bTrackingCommandButtonMouse = FALSE;
	HTREEITEM hOldHot = m_hHotCommandButton;
	m_hHotCommandButton = NULL;
	InvalidateCommandButton(hOldHot);
	CTreeOptionsCtrl::OnMouseLeave();
}

void CTreeOptionsCtrlEx::OnCaptureChanged(CWnd *pWnd)
{
	HTREEITEM hPressed = m_hPressedCommandButton;
	m_hPressedCommandButton = NULL;
	InvalidateCommandButton(hPressed);
	CTreeOptionsCtrl::OnCaptureChanged(pWnd);
}

BOOL CTreeOptionsCtrlEx::OnCustomDraw(LPNMHDR pNMHDR, LRESULT *pResult)
{
	BOOL bHandled = CTreeOptionsCtrl::OnCustomDraw(pNMHDR, pResult);
	NMTVCUSTOMDRAW *pCustomDraw = reinterpret_cast<NMTVCUSTOMDRAW*>(pNMHDR);
	if (pCustomDraw == NULL)
		return bHandled;

	HTREEITEM hItem = reinterpret_cast<HTREEITEM>(pCustomDraw->nmcd.dwItemSpec);
	if (pCustomDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && IsItemHeightSpacer(hItem)) {
		HTREEITEM hPrev = GetPrevSiblingItem(hItem);
		while (IsItemHeightSpacer(hPrev))
			hPrev = GetPrevSiblingItem(hPrev);
		if (IsCommandButton(hPrev)) {
			CDC dc;
			dc.Attach(pCustomDraw->nmcd.hdc);
			DrawCommandButton(dc, hPrev);
			dc.Detach();
		}
	} else if (pCustomDraw->nmcd.dwDrawStage == CDDS_ITEMPOSTPAINT && IsCommandButton(hItem)) {
		CDC dc;
		dc.Attach(pCustomDraw->nmcd.hdc);
		DrawCommandButton(dc, hItem);
		dc.Detach();
	}
	return bHandled;
}

void CTreeOptionsCtrlEx::HandleCheckBox(HTREEITEM hItem, BOOL bCheck)
{
	//Turn of redraw to Q all the changes we're going to make here
	SetRedraw(FALSE);

	//Toggle the state
	BOOL bOldState;
	GetCheckBox(hItem, bOldState);
	VERIFY(SetCheckBox(hItem, !bCheck));
	if (bOldState != !bCheck)
		NotifyParent(BN_CLICKED, hItem);

	//If the item has children, then iterate through them and for all items
	//which are check boxes set their state to be the same as the parent
	HTREEITEM hChild = GetNextItem(hItem, TVGN_CHILD);
	while (hChild) {
		if (IsCheckBox(hChild)) {
			BOOL bThisChecked;
			GetCheckBox(hChild, bThisChecked);
			SetCheckBox(hChild, !bCheck);
			if (bThisChecked != !bCheck)
				NotifyParent(BN_CLICKED, hChild);
		}

		//Move on to the next item
		hChild = GetNextItem(hChild, TVGN_NEXT);
	}

	//Get the parent item and if it is a checkbox, then iterate through
	//all its children and if all the checkboxes are checked, then also
	//automatically check the parent. If no checkboxes are checked, then
	//also automatically uncheck the parent.
	HTREEITEM hParent = GetNextItem(hItem, TVGN_PARENT);
	UpdateCheckBoxGroup(hParent);

	//Reset the redraw flag
	SetRedraw(TRUE);
}

void CTreeOptionsCtrlEx::UpdateCheckBoxGroup(HTREEITEM hItem)
{
	SetRedraw(FALSE);

	//Iterate through all children and if all the checkboxes are checked, then also
	//automatically check the item. If no checkboxes are checked, then
	//also automatically uncheck the item.
	HTREEITEM hParent = hItem;
	if (hParent && IsCheckBox(hParent)) {
		BOOL bNoCheckBoxesChecked = TRUE;
		BOOL bAllCheckBoxesChecked = TRUE;
		HTREEITEM hChild = GetNextItem(hParent, TVGN_CHILD);
		while (hChild) {
			if (IsCheckBox(hChild)) {
				BOOL bThisChecked;
				VERIFY(GetCheckBox(hChild, bThisChecked));
				bNoCheckBoxesChecked = bNoCheckBoxesChecked && !bThisChecked;
				bAllCheckBoxesChecked = bAllCheckBoxesChecked && bThisChecked;
			}

			//Move on to the next item
			hChild = GetNextItem(hChild, TVGN_NEXT);
		}

		if (bNoCheckBoxesChecked) {
			BOOL bOldState;
			GetCheckBox(hParent, bOldState);
			SetCheckBox(hParent, FALSE);
			if (bOldState)
				NotifyParent(BN_CLICKED, hParent);
		} else if (bAllCheckBoxesChecked) {
			BOOL bOldState;
			GetCheckBox(hParent, bOldState);
			SetCheckBox(hParent, FALSE); //gets rid of the semi state
			SetCheckBox(hParent, TRUE);
			if (!bOldState)
				NotifyParent(BN_CLICKED, hParent);
		} else {
			BOOL bEnable;
			VERIFY(GetCheckBoxEnable(hParent, bEnable));
			SetEnabledSemiCheckBox(hParent, bEnable);
		}
	}

	//Reset the redraw flag
	SetRedraw(TRUE);
}

BOOL CTreeOptionsCtrlEx::SetRadioButton(HTREEITEM hParent, int nIndex)
{
	//Validate our parameters
	ASSERT(IsGroup(hParent)); //Parent item must be a group item

	//Iterate through the child items and turn on the specified one and turn off all the other ones
	HTREEITEM hChild = GetNextItem(hParent, TVGN_CHILD);

	//Turn of redraw to Q all the changes we're going to make here
	SetRedraw(FALSE);

	int i = 0;
	BOOL bCheckedSomeItem = FALSE;
	while (hChild) {
		//if we reach a non radio button then break out of the loop
		if (!IsRadioButton(hChild))
			break;

		if (i == nIndex) {
			//Turn this item on
			BOOL bOldState;
			GetRadioButton(hChild, bOldState);
			VERIFY(SetItemImage(hChild, 3, 3));
			bCheckedSomeItem = TRUE;
			if (!bOldState)
				NotifyParent(BN_CLICKED, hChild);
		} else {
			BOOL bEnable;
			VERIFY(GetRadioButtonEnable(hChild, bEnable));

			//Turn this item off
			if (bEnable)
				VERIFY(SetItemImage(hChild, 2, 2));
			else
				VERIFY(SetItemImage(hChild, 4, 4));
		}

		//Move on to the next item
		hChild = GetNextItem(hChild, TVGN_NEXT);
		++i;
	}
	ASSERT(bCheckedSomeItem); //You specified an index which does not exist

	//Reset the redraw flag
	SetRedraw(TRUE);

	return TRUE;
}

BOOL CTreeOptionsCtrlEx::SetRadioButton(HTREEITEM hItem)
{
	//Validate our parameters
	ASSERT(IsRadioButton(hItem)); //Must be a radio item to check it

	//Iterate through the sibling items and turn them all off except this one
	HTREEITEM hParent = GetNextItem(hItem, TVGN_PARENT);
	ASSERT(IsGroup(hParent)); //Parent item must be a group item

	//Iterate through the child items and turn on the specified one and turn off all the other ones
	HTREEITEM hChild = GetNextItem(hParent, TVGN_CHILD);

	//Turn of redraw to Q all the changes we're going to make here
	SetRedraw(FALSE);

	while (hChild) {
		//if we reach a non radio button then break out of the loop
		if (!IsRadioButton(hChild))
			break;

		if (hChild == hItem) {
			//Turn this item on
			BOOL bOldState;
			GetRadioButton(hChild, bOldState);
			VERIFY(SetItemImage(hChild, 3, 3));
			if (!bOldState)
				NotifyParent(BN_CLICKED, hChild);
		} else {
			BOOL bEnable;
			VERIFY(GetRadioButtonEnable(hChild, bEnable));

			//Turn this item off
			if (bEnable)
				VERIFY(SetItemImage(hChild, 2, 2));
			else
				VERIFY(SetItemImage(hChild, 6, 6));
		}

		//Move on to the next item
		hChild = GetNextItem(hChild, TVGN_NEXT);
	}

	//Reset the redraw flag
	SetRedraw(TRUE);

	return TRUE;
}

BOOL CTreeOptionsCtrlEx::NotifyParent(UINT uCode, HTREEITEM hItem)
{
	CWnd *pWnd = GetParent();
	if (!pWnd)
		return FALSE;

	TREEOPTSCTRLNOTIFY ton;
	ton.nmhdr.hwndFrom = m_hWnd;
	ton.nmhdr.idFrom = ::GetWindowLongPtr(m_hWnd, GWLP_ID);
	ton.nmhdr.code = uCode;
	ton.hItem = hItem;
	return pWnd->SendMessage(UM_TREEOPTSCTRL_NOTIFY, ::GetWindowLongPtr(m_hWnd, GWLP_ID), (LPARAM)&ton) != 0;
}

void CTreeOptionsCtrlEx::SetImageListColorFlags(UINT uImageListColorFlags)
{
	m_uImageListColorFlags = uImageListColorFlags;
}

void CTreeOptionsCtrlEx::OnCreateImageList()
{
	// Ensure the tree control is using DarkMode_Explorer theme so that themed glyphs are drawn in dark mode
	BOOL darkMode = IsDarkModeEnabled();
	if (darkMode) {
		SetWindowTheme(m_hWnd, L"DarkMode_Explorer", nullptr);
		DwmSetWindowAttribute(m_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
	}

	CDC* pDCScreen = CDC::FromHandle(::GetDC(HWND_DESKTOP)); // use screen DC for correct DPI and theme rendering
	if (pDCScreen) {
		static const int iBmpWidth = 16;
		static const int iBmpHeight = 16;
		static const int iBitmaps = 14;
		CBitmap bmpControls;

		// Create a compatible bitmap large enough to hold all state images side by side
		if (bmpControls.CreateCompatibleBitmap(pDCScreen, iBmpWidth * iBitmaps, iBmpHeight)) {
			// Initialize the image list (mask-based)
			if (m_ilTree.Create(iBmpWidth, iBmpHeight, m_uImageListColorFlags | ILC_MASK, 0, 1)) {
				CDC dcMem;
				if (dcMem.CreateCompatibleDC(pDCScreen)) {
					CBitmap* pOldBmp = dcMem.SelectObject(&bmpControls);

					// Fill the entire strip background with dark-mode window color
					dcMem.FillSolidRect(0, 0, iBmpWidth * iBitmaps, iBmpHeight, GetCustomSysColor(COLOR_WINDOW));

					// Compute control glyph dimensions and offsets
					int iCtrlWidth = 16 - 3;
					int iCtrlHeight = 16 - 3;
					int iCtrlLeft = (iBmpWidth - iCtrlWidth) / 2;
					int iCtrlTop = (iBmpHeight - iCtrlHeight) / 2;

					// Open the BUTTON theme after setting DarkMode_Explorer on this window
					HTHEME hTheme = OpenThemeData(m_hWnd, L"BUTTON");

					// ---------- Index 0: Checkbox unchecked, normal ----------
					{
						CRect rcBmp(0 * iBmpWidth, 0, 1 * iBmpWidth, iBmpHeight);
						CRect rcCtrl(rcBmp.left + iCtrlLeft, rcBmp.top + iCtrlTop, rcBmp.left + iCtrlLeft + iCtrlWidth, rcBmp.top + iCtrlTop + iCtrlHeight);
						if (hTheme)
							DrawThemeBackground(hTheme, dcMem, BP_CHECKBOX, CBS_UNCHECKEDNORMAL, &rcCtrl, nullptr);
					}

					// ---------- Index 1: Checkbox checked, normal ----------
					{
						CRect rcBmp(1 * iBmpWidth, 0, 2 * iBmpWidth, iBmpHeight);
						CRect rcCtrl(rcBmp.left + iCtrlLeft, rcBmp.top + iCtrlTop, rcBmp.left + iCtrlLeft + iCtrlWidth, rcBmp.top + iCtrlTop + iCtrlHeight);
						if (hTheme)
							DrawThemeBackground(hTheme, dcMem, BP_CHECKBOX, CBS_CHECKEDNORMAL, &rcCtrl, nullptr);
					}

					// ---------- Index 2: Radio unchecked, normal ----------
					{
						CRect rcBmp(2 * iBmpWidth, 0, 3 * iBmpWidth, iBmpHeight);
						CRect rcCtrl(rcBmp.left + iCtrlLeft, rcBmp.top + iCtrlTop, rcBmp.left + iCtrlLeft + iCtrlWidth, rcBmp.top + iCtrlTop + iCtrlHeight);
						if (hTheme)
							DrawThemeBackground(hTheme, dcMem, BP_RADIOBUTTON, RBS_UNCHECKEDNORMAL, &rcCtrl, nullptr);
					}

					// ---------- Index 3: Radio checked, normal ----------
					{
						CRect rcBmp(3 * iBmpWidth, 0, 4 * iBmpWidth, iBmpHeight);
						CRect rcCtrl(rcBmp.left + iCtrlLeft, rcBmp.top + iCtrlTop, rcBmp.left + iCtrlLeft + iCtrlWidth, rcBmp.top + iCtrlTop + iCtrlHeight);
						if (hTheme)
							DrawThemeBackground(hTheme, dcMem, BP_RADIOBUTTON, RBS_CHECKEDNORMAL, &rcCtrl, nullptr);
					}

					// ---------- Index 4: Checkbox unchecked, disabled ----------
					{
						CRect rcBmp(4 * iBmpWidth, 0, 5 * iBmpWidth, iBmpHeight);
						CRect rcCtrl(rcBmp.left + iCtrlLeft, rcBmp.top + iCtrlTop, rcBmp.left + iCtrlLeft + iCtrlWidth, rcBmp.top + iCtrlTop + iCtrlHeight);
						if (hTheme)
							DrawThemeBackground(hTheme, dcMem, BP_CHECKBOX, CBS_UNCHECKEDDISABLED, &rcCtrl, nullptr);
					}

					// ---------- Index 5: Checkbox checked, disabled ----------
					{
						CRect rcBmp(5 * iBmpWidth, 0, 6 * iBmpWidth, iBmpHeight);
						CRect rcCtrl(rcBmp.left + iCtrlLeft, rcBmp.top + iCtrlTop, rcBmp.left + iCtrlLeft + iCtrlWidth, rcBmp.top + iCtrlTop + iCtrlHeight);
						if (hTheme)
							DrawThemeBackground(hTheme, dcMem, BP_CHECKBOX, CBS_CHECKEDDISABLED, &rcCtrl, nullptr);
					}

					// ---------- Index 6: Radio unchecked, disabled ----------
					{
						CRect rcBmp(6 * iBmpWidth, 0, 7 * iBmpWidth, iBmpHeight);
						CRect rcCtrl(rcBmp.left + iCtrlLeft, rcBmp.top + iCtrlTop, rcBmp.left + iCtrlLeft + iCtrlWidth, rcBmp.top + iCtrlTop + iCtrlHeight);
						if (hTheme)
							DrawThemeBackground(hTheme, dcMem, BP_RADIOBUTTON, RBS_UNCHECKEDDISABLED, &rcCtrl, nullptr);
					}

					// ---------- Index 7: Radio checked, disabled ----------
					{
						CRect rcBmp(7 * iBmpWidth, 0, 8 * iBmpWidth, iBmpHeight);
						CRect rcCtrl(rcBmp.left + iCtrlLeft, rcBmp.top + iCtrlTop, rcBmp.left + iCtrlLeft + iCtrlWidth, rcBmp.top + iCtrlTop + iCtrlHeight);
						if (hTheme)
							DrawThemeBackground(hTheme, dcMem, BP_RADIOBUTTON, RBS_CHECKEDDISABLED, &rcCtrl, nullptr);
					}

					// ---------- Index 8: Checkbox tri-state, normal ----------
					{
						CRect rcBmp(8 * iBmpWidth, 0, 9 * iBmpWidth, iBmpHeight);
						CRect rcCtrl(rcBmp.left + iCtrlLeft, rcBmp.top + iCtrlTop, rcBmp.left + iCtrlLeft + iCtrlWidth, rcBmp.top + iCtrlTop + iCtrlHeight);
						if (hTheme)
							DrawThemeBackground(hTheme, dcMem, BP_CHECKBOX, CBS_MIXEDNORMAL, &rcCtrl, nullptr);
					}

					// ---------- Index 9: Checkbox tri-state, disabled ----------
					{
						CRect rcBmp(9 * iBmpWidth, 0, 10 * iBmpWidth, iBmpHeight);
						CRect rcCtrl(rcBmp.left + iCtrlLeft, rcBmp.top + iCtrlTop, rcBmp.left + iCtrlLeft + iCtrlWidth, rcBmp.top + iCtrlTop + iCtrlHeight);
						if (hTheme)
							DrawThemeBackground(hTheme, dcMem, BP_CHECKBOX, CBS_MIXEDDISABLED, &rcCtrl, nullptr);
					}

					// ---------- Index 10: blank command placeholder ----------

					// ---------- Index 11: Edit icon 'I' (original logic) ----------
					{
						ASSERT(TREEOPTSCTRLIMG_EDIT == 11);
						CRect rcBmp(11 * iBmpWidth, 0, 12 * iBmpWidth, iBmpHeight);
						CRect rcCtrl(rcBmp.left + iCtrlLeft, rcBmp.top + iCtrlTop, rcBmp.left + iCtrlLeft + iCtrlWidth, rcBmp.top + iCtrlTop + iCtrlHeight);

						CFont font;
						if (font.CreatePointFont(10, _T("Courier"))) {
							CFont* pOldFont = dcMem.SelectObject(&font);
							dcMem.SetBkMode(TRANSPARENT);
							dcMem.TextOut(rcCtrl.left + 2, rcCtrl.top, _T("I"));
							dcMem.SelectObject(pOldFont);
						}
						RECT rcEdge = rcBmp;
						rcEdge.top += 1;
						rcEdge.bottom -= 1;
						dcMem.DrawEdge(&rcEdge, EDGE_ETCHED, BF_RECT);
					}

					// ---------- Index 12: Combo box scroll glyph (original logic) ----------
					{
						CRect rcBmp(12 * iBmpWidth, 0, 13 * iBmpWidth, iBmpHeight);
						CRect rcCtrl(rcBmp.left + iCtrlLeft, rcBmp.top + iCtrlTop, rcBmp.left + iCtrlLeft + iCtrlWidth, rcBmp.top + iCtrlTop + iCtrlHeight);
						dcMem.DrawFrameControl(&rcCtrl, DFC_SCROLL, DFCS_SCROLLCOMBOBOX | DFCS_FLAT);
					}

					// ---------- Index 13: Branch group icon ----------
					{
						ASSERT(TREEOPTSCTRLIMG_GROUP == 13);
						CRect rcBmp(13 * iBmpWidth, 0, 14 * iBmpWidth, iBmpHeight);
						HICON hIcon = (HICON)::LoadImage(AfxGetResourceHandle(), _T("BRANCH"), IMAGE_ICON, iBmpWidth, iBmpHeight, LR_DEFAULTCOLOR);
						if (hIcon != NULL) {
							::DrawIconEx(dcMem.GetSafeHdc(), rcBmp.left, rcBmp.top, hIcon, iBmpWidth, iBmpHeight, 0, NULL, DI_NORMAL);
							::DestroyIcon(hIcon);
						}
					}

					// Restore original bitmap and add to image list with mask color
					dcMem.SelectObject(pOldBmp);
					m_ilTree.Add(&bmpControls, RGB(255, 0, 255));

					// Close theme handle
					if (hTheme)
						CloseThemeData(hTheme);
				}
			}
		}
		::ReleaseDC(HWND_DESKTOP, *pDCScreen);
	}
}

void CTreeOptionsCtrlEx::HandleChildControlLosingFocus()
{
	CTreeOptionsCtrl::HandleChildControlLosingFocus();
}

void CTreeOptionsCtrlEx::SetEditLabel(HTREEITEM hItem, const CString &rstrLabel)
{
	CString sLabel(rstrLabel);
	const CString& sSeparator = GetTextSeparator();
	if (!sSeparator.IsEmpty()) {
		// Avoid using the exact separator in labels, otherwise value parsing breaks for some locales.
		CString sSafeSeparator(sSeparator);
		int nLast = sSafeSeparator.GetLength() - 1;
		if (nLast >= 0) {
			if (sSafeSeparator[nLast] == _T(' '))
				sSafeSeparator.SetAt(nLast, (TCHAR)0x00A0);
			else
				sSafeSeparator += (TCHAR)0x00A0;
		}
		sLabel.Replace(sSeparator, sSafeSeparator);
	}

	CString sItemText(GetItemText(hItem));
	int nSeparator = sItemText.Find(sSeparator);
	sItemText.Delete(0, nSeparator < 0 ? INT_MAX : nSeparator);
	sItemText.Insert(0, sLabel);
	SetItemText(hItem, sItemText);
}

void CTreeOptionsCtrlEx::OnDestroy()
{
	CTreeOptionsCtrl::OnDestroy();
	m_ilTree.DeleteImageList();
}


//////////////////////////////////////////////////////////////////////////////
// DDX_...

void EditTextFloatFormat(CDataExchange *pDX, int nIDC, HTREEITEM hItem, void *pData, double value, int nSizeGcvt)
{
	ASSERT(pData != NULL);

	HWND hWndCtrl = pDX->PrepareEditCtrl(nIDC);
	ASSERT(hWndCtrl != NULL);
	CTreeOptionsCtrl *pCtrlTreeOptions = static_cast<CTreeOptionsCtrl*>(CWnd::FromHandlePermanent(hWndCtrl));
	ASSERT(pCtrlTreeOptions);
	ASSERT(pCtrlTreeOptions->IsKindOf(RUNTIME_CLASS(CTreeOptionsCtrl)));

	if (pDX->m_bSaveAndValidate) {
		CString sText(pCtrlTreeOptions->GetEditText(hItem));
		double d;
		if (_stscanf(sText, _T("%lf"), &d) != 1) {
			CDarkMode::MessageBox(AFX_IDP_PARSE_REAL);
			pDX->Fail();	// throws exception
		}
		if (nSizeGcvt == FLT_DIG)
			*((float*)pData) = (float)d;
		else
			*((double*)pData) = d;
	} else {
		TCHAR szBuffer[400];
		_sntprintf(szBuffer, _countof(szBuffer), _T("%.*g"), nSizeGcvt, value);
		szBuffer[_countof(szBuffer) - 1] = _T('\0');
		pCtrlTreeOptions->SetEditText(hItem, szBuffer);
	}
}

void EditTextWithFormat(CDataExchange *pDX, int nIDC, HTREEITEM hItem, LPCTSTR lpszFormat, UINT nIDPrompt, ...)
	// only supports windows output formats - no floating point
{
	va_list pData;
	va_start(pData, nIDPrompt);

	HWND hWndCtrl = pDX->PrepareEditCtrl(nIDC);
	ASSERT(hWndCtrl != NULL);
	CTreeOptionsCtrl *pCtrlTreeOptions = static_cast<CTreeOptionsCtrl*>(CWnd::FromHandlePermanent(hWndCtrl));
	ASSERT(pCtrlTreeOptions);
	ASSERT(pCtrlTreeOptions->IsKindOf(RUNTIME_CLASS(CTreeOptionsCtrl)));

	if (pDX->m_bSaveAndValidate) {
		void *pResult = va_arg(pData, void*);
		// the following works for %d, %u, %ld, %lu
		CString sText(pCtrlTreeOptions->GetEditText(hItem));
		if (_stscanf(sText, lpszFormat, pResult) != 1) {
			CDarkMode::MessageBox(nIDPrompt);
			pDX->Fail();	// throws exception
		}
	} else {
		TCHAR szT[64];
		_vsntprintf(szT, _countof(szT), lpszFormat, pData);
		szT[_countof(szT) - 1] = _T('\0');
		// does not support floating point numbers - see dlgfloat.cpp
		pCtrlTreeOptions->SetEditText(hItem, szT);
	}

	va_end(pData);
}

void DDX_TreeCheck(CDataExchange *pDX, int nIDC, HTREEITEM hItem, bool &bCheck)
{
	BOOL biBool = bCheck;
	DDX_TreeCheck(pDX, nIDC, hItem, biBool);
	bCheck = biBool != FALSE;
}

void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, int &value)
{
	if (pDX->m_bSaveAndValidate)
		EditTextWithFormat(pDX, nIDC, hItem, _T("%d"), AFX_IDP_PARSE_INT, &value);
	else
		EditTextWithFormat(pDX, nIDC, hItem, _T("%d"), AFX_IDP_PARSE_INT, value);
}

void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, UINT &value)
{
	if (pDX->m_bSaveAndValidate)
		EditTextWithFormat(pDX, nIDC, hItem, _T("%u"), AFX_IDP_PARSE_UINT, &value);
	else
		EditTextWithFormat(pDX, nIDC, hItem, _T("%u"), AFX_IDP_PARSE_UINT, value);
}

void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, long &value)
{
	if (pDX->m_bSaveAndValidate)
		EditTextWithFormat(pDX, nIDC, hItem, _T("%ld"), AFX_IDP_PARSE_INT, &value);
	else
		EditTextWithFormat(pDX, nIDC, hItem, _T("%ld"), AFX_IDP_PARSE_INT, value);
}

void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, DWORD &value)
{
	if (pDX->m_bSaveAndValidate)
		EditTextWithFormat(pDX, nIDC, hItem, _T("%lu"), AFX_IDP_PARSE_UINT, &value);
	else
		EditTextWithFormat(pDX, nIDC, hItem, _T("%lu"), AFX_IDP_PARSE_UINT, value);
}

void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, float &value)
{
	EditTextFloatFormat(pDX, nIDC, hItem, &value, value, FLT_DIG);
}

void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, double &value)
{
	EditTextFloatFormat(pDX, nIDC, hItem, &value, value, DBL_DIG);
}

void DDX_Text(CDataExchange *pDX, int nIDC, HTREEITEM hItem, CString &sText)
{
	HWND hWndCtrl = pDX->PrepareCtrl(nIDC);
	CTreeOptionsCtrl *pCtrlTreeOptions = static_cast<CTreeOptionsCtrl*>(CWnd::FromHandlePermanent(hWndCtrl));
	ASSERT(pCtrlTreeOptions);
	ASSERT(pCtrlTreeOptions->IsKindOf(RUNTIME_CLASS(CTreeOptionsCtrl)));

	if (pDX->m_bSaveAndValidate)
		sText = pCtrlTreeOptions->GetEditText(hItem);
	else
		pCtrlTreeOptions->SetEditText(hItem, sText);
}


///////////////////////////////////////////////////////////////////////////////
// CNumTreeOptionsEdit

IMPLEMENT_DYNCREATE(CNumTreeOptionsEdit, CTreeOptionsEdit)

BEGIN_MESSAGE_MAP(CNumTreeOptionsEdit, CTreeOptionsEdit)
	ON_WM_CREATE()
	ON_CONTROL_REFLECT(EN_CHANGE, OnEnChange)
END_MESSAGE_MAP()

int CNumTreeOptionsEdit::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	m_bSelf = true;
	if (CTreeOptionsEdit::OnCreate(lpCreateStruct) == -1)
		return -1;
	m_bSelf = false;

	return 0;
}

void CNumTreeOptionsEdit::OnEnChange()
{
	if (!m_bSelf)
		static_cast<CTreeOptionsCtrlEx*>(m_pTreeCtrl)->NotifyParent(EN_CHANGE, m_hTreeCtrlItem);
}


///////////////////////////////////////////////////////////////////////////////
// CTreeOptionsEditEx

IMPLEMENT_DYNCREATE(CTreeOptionsEditEx, CTreeOptionsEdit)

BEGIN_MESSAGE_MAP(CTreeOptionsEditEx, CTreeOptionsEdit)
	ON_WM_CREATE()
	ON_CONTROL_REFLECT(EN_CHANGE, OnEnChange)
END_MESSAGE_MAP()

int CTreeOptionsEditEx::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	m_bSelf = true;
	if (CTreeOptionsEdit::OnCreate(lpCreateStruct) == -1)
		return -1;
	m_bSelf = false;

	return 0;
}

void CTreeOptionsEditEx::OnEnChange()
{
	if (!m_bSelf)
		static_cast<CTreeOptionsCtrlEx*>(m_pTreeCtrl)->NotifyParent(EN_CHANGE, m_hTreeCtrlItem);
}
