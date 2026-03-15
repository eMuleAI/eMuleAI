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

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


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

    // Handle tab control colors for pressed and non-pressed states
    RECT rcItem(lpDIS->rcItem);
    CRect rcFullItem(rcItem);

    // Set background color based on the state of the tab (selected or not)
    if ((lpDIS->itemState & ODS_SELECTED) != 0)
        pDC->FillSolidRect(&lpDIS->rcItem, GetCustomSysColor(COLOR_ACTIVECAPTION)); // Set selected tab color
    else
        pDC->FillSolidRect(&lpDIS->rcItem, GetCustomSysColor(COLOR_WINDOW)); // Set non-selected tabs color

    pDC->SetTextColor(GetCustomSysColor(COLOR_BTNTEXT)); // Set text color
    rcItem.top += 2;
    pDC->DrawText(szLabel, &rcItem, DT_SINGLELINE | DT_TOP | DT_CENTER); // Draw the text label on the tab
}

void CButtonsTabCtrl::PreSubclassWindow()
{
    CTabCtrl::PreSubclassWindow();
}