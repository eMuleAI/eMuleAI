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
END_MESSAGE_MAP()

CTreeOptionsCtrlEx::CTreeOptionsCtrlEx(UINT uImageListColorFlags)
{
	m_uImageListColorFlags = uImageListColorFlags;
	SetToggleOverIconOnly(TRUE);
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
		static const int iBitmaps = 13;
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

					// ---------- Index 10: (unused/reserved) ----------
					// Leave empty or implement original logic if needed

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
	CString sItemText(GetItemText(hItem));
	int nSeparator = sItemText.Find(GetTextSeparator());
	sItemText.Delete(0, nSeparator < 0 ? INT_MAX : nSeparator);
	sItemText.Insert(0, rstrLabel);
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