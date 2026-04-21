// This file is part of eMule AI
// Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//Copyright (C)2026 eMule AI
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "stdafx.h"
#include "emule.h"
#include "ButtonsTabCtrl.h"
#include "MenuCmds.h"
#include "UserMsgs.h"
#include "eMuleAI/DarkMode.h"
#include <uxtheme.h>
#include <vssym32.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	const CSize s_szButtonTabPadding(11, 4);

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

///////////////////////////////////////////////////////////////////////////////
// CButtonsTabCtrl

IMPLEMENT_DYNAMIC(CButtonsTabCtrl, CTabCtrl)

BEGIN_MESSAGE_MAP(CButtonsTabCtrl, CTabCtrl)
END_MESSAGE_MAP()

void CButtonsTabCtrl::DrawItem(LPDRAWITEMSTRUCT lpDIS)
{
	int nTabIndex = lpDIS->itemID;
	if (nTabIndex < 0)
		return;

	TCHAR szLabel[256];
	TC_ITEM tci;
	tci.mask = TCIF_TEXT;
	tci.pszText = szLabel;
	tci.cchTextMax = _countof(szLabel);
	if (!GetItem(nTabIndex, &tci))
		return;

	CDC* pDC = CDC::FromHandle(lpDIS->hDC);
	if (!pDC)
		return;

	RECT rcItem(lpDIS->rcItem);
	const bool bSelected = (lpDIS->itemState & ODS_SELECTED) != 0;

	if (IsDarkModeEnabled()) {
		if (bSelected)
			pDC->FillSolidRect(&lpDIS->rcItem, GetCustomSysColor(COLOR_ACTIVECAPTION));
		else
			pDC->FillSolidRect(&lpDIS->rcItem, GetCustomSysColor(COLOR_WINDOW));

		CFont boldFont;
		CFont* pOldFont = SelectTabFont(*pDC, GetFont(), bSelected, boldFont);
		const COLORREF crOldColor = pDC->SetTextColor(GetCustomSysColor(COLOR_BTNTEXT));
		const int iOldBkMode = pDC->SetBkMode(TRANSPARENT);
		rcItem.top += 2;
		pDC->DrawText(szLabel, &rcItem, DT_SINGLELINE | DT_TOP | DT_CENTER);
		pDC->SetBkMode(iOldBkMode);
		pDC->SetTextColor(crOldColor);
		if (pOldFont != NULL)
			pDC->SelectObject(pOldFont);
		return;
	}

	CRect rcFullItem(rcItem);
	HTHEME hTheme = NULL;
	const bool bHotTracked = pDC->GetTextColor() == ::GetSysColor(COLOR_HOTLIGHT);

	// Reuse the native push-button theme so light mode matches the stock eMule tabs.
	if (IsThemeActive() && IsAppThemed()) {
		hTheme = OpenThemeData(m_hWnd, L"BUTTON");
		if (hTheme != NULL) {
			rcFullItem.InflateRect(2, 2);

			int iStateId = PBS_NORMAL;
			if (bSelected)
				iStateId = PBS_PRESSED;
			else if (bHotTracked)
				iStateId = PBS_HOT;

			CRect rcTopBorder(rcFullItem);
			rcTopBorder.bottom = rcTopBorder.top + 2;
			pDC->FillSolidRect(&rcTopBorder, ::GetSysColor(COLOR_BTNFACE));

			if (IsThemeBackgroundPartiallyTransparent(hTheme, BP_PUSHBUTTON, iStateId))
				DrawThemeParentBackground(m_hWnd, pDC->GetSafeHdc(), &rcFullItem);
			DrawThemeBackground(hTheme, pDC->GetSafeHdc(), BP_PUSHBUTTON, iStateId, &rcFullItem, NULL);
		}
	}

	if (hTheme == NULL)
		pDC->FillSolidRect(&lpDIS->rcItem, ::GetSysColor(COLOR_BTNFACE));

	const int iOldBkMode = pDC->SetBkMode(TRANSPARENT);
	COLORREF crOldColor = CLR_NONE;
	if (bHotTracked)
		crOldColor = pDC->SetTextColor(::GetSysColor(COLOR_BTNTEXT));

	rcItem.top += 2;
	pDC->DrawText(szLabel, &rcItem, DT_SINGLELINE | DT_TOP | DT_CENTER);

	if (crOldColor != CLR_NONE)
		pDC->SetTextColor(crOldColor);
	pDC->SetBkMode(iOldBkMode);

	if (hTheme != NULL) {
		pDC->ExcludeClipRect(&rcFullItem);
		CloseThemeData(hTheme);
	}
}

void CButtonsTabCtrl::PreSubclassWindow()
{
	CTabCtrl::PreSubclassWindow();

	// Reserve extra width so the selected bold caption does not get clipped.
	SetPadding(s_szButtonTabPadding);
}
