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
#include "DropDownButton.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


IMPLEMENT_DYNAMIC(CDropDownButton, CToolBarCtrlX)

BEGIN_MESSAGE_MAP(CDropDownButton, CToolBarCtrlX)
	ON_WM_SIZE()
	ON_WM_SETTINGCHANGE()
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnNmCustomDraw)
END_MESSAGE_MAP()

CDropDownButton::CDropDownButton()
{
	m_bSingleDropDownBtn = true;
}

BOOL CDropDownButton::CreateBar(DWORD dwStyle, const RECT &rect, CWnd *pParentWnd, UINT nID, bool bSingleDropDownBtn)
{
	m_bSingleDropDownBtn = bSingleDropDownBtn;
	dwStyle |= CCS_NOMOVEY
			| CCS_NOPARENTALIGN
			| CCS_NORESIZE		// prevent adjusting of specified width & height(!) by Create func.
			| CCS_NODIVIDER
			| TBSTYLE_LIST
			| TBSTYLE_FLAT
			| TBSTYLE_TRANSPARENT
			| 0;

	if (!CToolBarCtrlX::Create(dwStyle, rect, pParentWnd, nID))
		return FALSE;
	return Init(bSingleDropDownBtn);
}

BOOL CDropDownButton::Init(bool bSingleDropDownBtn, bool bWholeDropDown)
{
	DeleteAllButtons();
	m_bSingleDropDownBtn = bSingleDropDownBtn;

	// If a toolbar control was created indirectly via a dialog resource one can not
	// add any buttons without setting an image list before. (?)
	// So, for this to work, we have to attach an image list to the toolbar control!
	// The image list can be empty, and it does not need to be used at all, but it has
	// to be attached.
	CImageList *piml = GetImageList();
	if (piml == NULL || piml->m_hImageList == NULL) {
		CImageList iml;
		iml.Create(16, 16, ILC_COLOR, 0, 0);
		SetImageList(&iml);
		iml.Detach();
	}
	if (m_bSingleDropDownBtn) {
		TBBUTTON atb[1] = {};
		atb[0].iBitmap = -1;
		atb[0].idCommand = (int)::GetWindowLongPtr(m_hWnd, GWLP_ID);
		atb[0].fsState = TBSTATE_ENABLED;
		atb[0].fsStyle = m_bSingleDropDownBtn ? (bWholeDropDown ? BTNS_WHOLEDROPDOWN : BTNS_DROPDOWN) : BTNS_BUTTON;
		atb[0].iString = -1;
		VERIFY(AddButtons(1, atb));

		ResizeToMaxWidth();
		if (!IsDarkModeEnabled())
			SetExtendedStyle(TBSTYLE_EX_DRAWDDARROWS);
	}

	return TRUE;
}

void CDropDownButton::SetWindowText(LPCTSTR pszString)
{
	int id = (int)::GetWindowLongPtr(m_hWnd, GWLP_ID);
	int cx = m_bSingleDropDownBtn ? 0 : GetBtnWidth(id);

	TBBUTTONINFO tbbi;
	tbbi.cbSize = (UINT)sizeof tbbi;
	tbbi.dwMask = TBIF_TEXT;
	tbbi.pszText = const_cast<LPTSTR>(pszString);
	SetButtonInfo(id, &tbbi);

	if (cx)
		SetBtnWidth(id, cx);
}

void CDropDownButton::SetIcon(LPCTSTR pszResourceID)
{
	if (!m_bSingleDropDownBtn)
		return;

	CImageList iml;
	iml.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 1, 1);
	iml.Add(CTempIconLoader(pszResourceID));
	CImageList *pImlOld = SetImageList(&iml);
	iml.Detach();
	if (pImlOld)
		pImlOld->DeleteImageList();

	TBBUTTONINFO tbbi;
	tbbi.cbSize = (UINT)sizeof tbbi;
	tbbi.dwMask = TBIF_IMAGE;
	tbbi.iImage = 0;
	SetButtonInfo((int)::GetWindowLongPtr(m_hWnd, GWLP_ID), &tbbi);
}

void CDropDownButton::ResizeToMaxWidth()
{
	if (!m_bSingleDropDownBtn)
		return;

	CRect rcWnd;
	GetWindowRect(&rcWnd);
	if (rcWnd.Width() > 0) {
		TBBUTTONINFO tbbi = {};
		tbbi.cbSize = (UINT)sizeof tbbi;
		tbbi.dwMask = TBIF_SIZE;
		tbbi.cx = (WORD)rcWnd.Width();
		SetButtonInfo((int)::GetWindowLongPtr(m_hWnd, GWLP_ID), &tbbi);
	}
}

void CDropDownButton::OnSize(UINT nType, int cx, int cy)
{
	CToolBarCtrlX::OnSize(nType, cx, cy);

	if (cx > 0 && cy > 0)
		ResizeToMaxWidth();
}

void CDropDownButton::RecalcLayout(bool bForce)
{
	// If toolbar has at least one button with the button style BTNS_DROPDOWN, the
	// entire toolbar is resized with too large height. So, remove the BTNS_DROPDOWN
	// button style(s) and force the toolbar to resize and apply them again.
	//
	// TODO: Should be moved to CToolBarCtrlX
	int id = (int)::GetWindowLongPtr(m_hWnd, GWLP_ID);
	bool bDropDownBtn = (GetBtnStyle(id) & BTNS_DROPDOWN) != 0;
	if (bDropDownBtn || bForce) {
		if (bDropDownBtn)
			RemoveBtnStyle(id, BTNS_DROPDOWN);
		CToolBarCtrlX::RecalcLayout();
		if (bDropDownBtn)
			AddBtnStyle(id, BTNS_DROPDOWN);
	}
}

void CDropDownButton::OnSettingChange(UINT uFlags, LPCTSTR lpszSection)
{
	CToolBarCtrlX::OnSettingChange(uFlags, lpszSection);

	// The toolbar resizes itself when the system fonts were changed,
	// especially when large/small system fonts were selected. Need
	// to recalc the layout because we have a fixed control size.
	RecalcLayout();
}

void CDropDownButton::OnNmCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (!IsDarkModeEnabled()) {
		*pResult = CDRF_DODEFAULT;
		return;
	}

	LPNMTBCUSTOMDRAW pNMTB = reinterpret_cast<LPNMTBCUSTOMDRAW>(pNMHDR);

	switch (pNMTB->nmcd.dwDrawStage) {

	case CDDS_PREPAINT:
		*pResult = CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
		return;

	case CDDS_ITEMPREPAINT:
	{
		HBRUSH hFill = nullptr;

		if (pNMTB->nmcd.uItemState & TBSTATE_CHECKED) { // Selected button
			hFill = CDarkMode::m_brDefault;
			pNMTB->clrBtnFace = GetCustomSysColor(COLOR_BTNHIGHLIGHT);
			pNMTB->clrText = GetCustomSysColor(COLOR_BTNTEXT);
		} else if (pNMTB->nmcd.uItemState & CDIS_HOT) { // Mouse is over the button
			hFill = CDarkMode::m_brActiveCaption;
			pNMTB->clrBtnFace = GetCustomSysColor(COLOR_ACTIVECAPTION);
			pNMTB->clrText = GetCustomSysColor(COLOR_BTNTEXT);
		} else { // Normal state
			hFill = CDarkMode::m_brInactiveCaption;
			pNMTB->clrBtnFace = (pNMTB->nmcd.lItemlParam & 1) ? GetCustomSysColor(COLOR_INACTIVECAPTION) : GetCustomSysColor(COLOR_BTNFACE);
			pNMTB->clrText = (pNMTB->nmcd.lItemlParam & 1) ? GetCustomSysColor(COLOR_INACTIVECAPTIONTEXT) : GetCustomSysColor(COLOR_BTNTEXT);
		}

		FillRect(pNMTB->nmcd.hdc, &pNMTB->nmcd.rc, hFill); // Fill the rectangle with the appropriate color
		pNMTB->clrBtnHighlight = GetCustomSysColor(COLOR_BTNHIGHLIGHT); // Set button highlight color
		pNMTB->clrTextHighlight = pNMTB->clrText; // Set text highlight color to the same as text color


		*pResult = CDRF_DODEFAULT;
		return;
	}

	case CDDS_POSTPAINT:
	{
		const int cxArea = 12, arrowW = 5, arrowH = 3;

		for (int i = 0; i < GetButtonCount(); ++i) {
			TBBUTTON tbb{};
			GetButton(i, &tbb);

			if (!(tbb.fsStyle & (BTNS_DROPDOWN | BTNS_WHOLEDROPDOWN | TBSTYLE_DROPDOWN)))
				continue;

			CRect rcBtn; GetItemRect(i, &rcBtn);

			CRect rcFill(rcBtn.right - cxArea, rcBtn.top, rcBtn.right, rcBtn.bottom);
			HBRUSH hBk = CreateSolidBrush(GetCustomSysColor(COLOR_BTNFACE));
			FillRect(pNMTB->nmcd.hdc, &rcFill, hBk);
			DeleteObject(hBk);

			int xLeft = rcFill.left - 2 + (cxArea - arrowW) / 2;
			int yTop = rcBtn.top + (rcBtn.Height() - arrowH) / 2;

			POINT pts[3] = {
				{ xLeft,              yTop },
				{ xLeft + arrowW,     yTop },
				{ xLeft + arrowW / 2, yTop + arrowH }
			};

			HGDIOBJ hOldPen = SelectObject(pNMTB->nmcd.hdc, GetStockObject(DC_PEN));
			HGDIOBJ hOldBrush = SelectObject(pNMTB->nmcd.hdc, GetStockObject(DC_BRUSH));
			SetDCPenColor(pNMTB->nmcd.hdc, GetCustomSysColor(COLOR_BTNTEXT));
			SetDCBrushColor(pNMTB->nmcd.hdc, GetCustomSysColor(COLOR_BTNTEXT));
			Polygon(pNMTB->nmcd.hdc, pts, 3);
			SelectObject(pNMTB->nmcd.hdc, hOldPen);
			SelectObject(pNMTB->nmcd.hdc, hOldBrush);
		}
		break;
	}
	}

	*pResult = CDRF_DODEFAULT;
}	