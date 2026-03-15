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
#include "MuleStatusBarCtrl.h"
#include "OtherFunctions.h"
#include "emuledlg.h"
#include "ServerWnd.h"
#include "StatisticsDlg.h"
#include "ChatWnd.h"
#include "kademlia/kademlia/kademlia.h"
#include "ServerConnect.h"
#include "Server.h"
#include "ServerList.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
void AppendStatusBarToolTipLine(CString& strText, LPCTSTR pszBuddyKey)
{
	if (pszBuddyKey == NULL || *pszBuddyKey == _T('\0'))
		return;

	strText += _T('\n');
	strText += GetResString(pszBuddyKey);
}
}


// CMuleStatusBarCtrl

IMPLEMENT_DYNAMIC(CMuleStatusBarCtrl, CStatusBarCtrl)

BEGIN_MESSAGE_MAP(CMuleStatusBarCtrl, CStatusBarCtrl)
	ON_WM_LBUTTONDBLCLK()
	ON_WM_PAINT()
END_MESSAGE_MAP()

void CMuleStatusBarCtrl::Init()
{
	SetBkColor(GetCustomSysColor(COLOR_WINDOW));
	EnableToolTips();
}

void CMuleStatusBarCtrl::OnLButtonDblClk(UINT /*nFlags*/, CPoint point)
{
	int iPane = GetPaneAtPosition(point);
	switch (iPane) {
	case SBarLog:
		{
			CString sBar;
			sBar.Format(_T("eMule %s\n\n%s"), (LPCTSTR)GetResString(_T("SV_LOG")), (LPCTSTR)GetText(SBarLog));
			CDarkMode::MessageBox(sBar);
		}
		break;
	case SBarUsers:
	case SBarED2K:
	case SBarKad:
		theApp.emuledlg->serverwnd->ShowNetworkInfo();
		break;
	case SBarUpDown:
		theApp.emuledlg->SetActiveDialog(theApp.emuledlg->statisticswnd);
		break;
	case SBarChatMsg:
		theApp.emuledlg->SetActiveDialog(theApp.emuledlg->chatwnd);
		break;
	case SBarUSS:
		theApp.emuledlg->ShowPreferences(IDD_PPG_TWEAKS);
	}
}

int CMuleStatusBarCtrl::GetPaneAtPosition(CPoint &point) const
{
	CRect rect;
	for (int i = GetParts(0, NULL); --i >= 0;) {
		GetRect(i, rect);
		if (rect.PtInRect(point))
			return i;
	}
	return -1;
}

CString CMuleStatusBarCtrl::GetPaneToolTipText(EStatusBarPane iPane) const
{
	CString strText;
	if (iPane == SBarED2K && theApp.serverconnect && theApp.serverconnect->IsConnected()) {
		const CServer *cur_server = theApp.serverconnect->GetCurrentServer();
		if (cur_server) {
			const CServer *srv = theApp.serverlist->GetServerByAddress(cur_server->GetAddress(), cur_server->GetPort());
			if (srv) {
				strText.Format(GetResString(_T("IDS_SBAR_ED2K_TIP"))
					, (LPCTSTR)srv->GetListName()
					, (LPCTSTR)GetFormatedUInt(srv->GetUsers()));
				
				if (theApp.serverconnect->IsLowID()) {
					servingBuddyState eBuddy = (servingBuddyState)theApp.clientlist->GetEServerBuddyStatus();
					if (eBuddy == Disconnected && theApp.clientlist->IsEServerBuddySearchActive())
						eBuddy = Connecting;

					LPCTSTR pszBuddyKey = _T("");
					if (eBuddy == Connected)
						pszBuddyKey = _T("IDS_SBAR_ESERVER_BUDDY_CONNECTED");
					else if (eBuddy == Connecting)
						pszBuddyKey = _T("IDS_SBAR_ESERVER_BUDDY_SEARCHING");
					else
						pszBuddyKey = _T("IDS_SBAR_ESERVER_BUDDY_DISCONNECTED");

					AppendStatusBarToolTipLine(strText, pszBuddyKey);
				}
			}
		}
	} else if (iPane == SBarKad && Kademlia::CKademlia::IsConnected()) {
		strText.Format(GetResString(_T("IDS_SBAR_KAD_TIP")), (LPCTSTR)GetFormatedUInt(Kademlia::CKademlia::GetKademliaUsers()));
		
		if (Kademlia::CKademlia::IsFirewalled()) {
			servingBuddyState eBuddy = (servingBuddyState)theApp.clientlist->GetServingBuddyStatus();
			if (eBuddy == Disconnected && theApp.clientlist->IsKadBuddySearchActive())
				eBuddy = Connecting;

			LPCTSTR pszBuddyKey = _T("");
			if (eBuddy == Connected)
				pszBuddyKey = _T("IDS_SBAR_KAD_BUDDY_CONNECTED");
			else if (eBuddy == Connecting)
				pszBuddyKey = _T("IDS_SBAR_KAD_BUDDY_SEARCHING");
			else
				pszBuddyKey = _T("IDS_SBAR_KAD_BUDDY_DISCONNECTED");

			AppendStatusBarToolTipLine(strText, pszBuddyKey);
		}
	}
	return strText;
}

INT_PTR CMuleStatusBarCtrl::OnToolHitTest(CPoint point, TOOLINFO *pTI) const
{
	INT_PTR iHit = CWnd::OnToolHitTest(point, pTI);
	if (iHit == -1 && pTI != NULL && pTI->cbSize >= sizeof(AFX_OLDTOOLINFO)) {
		int iPane = GetPaneAtPosition(point);
		if (iPane >= 0) {
			const CString &strToolTipText = GetPaneToolTipText((EStatusBarPane)iPane);
			if (!strToolTipText.IsEmpty()) {
				pTI->hwnd = m_hWnd;
				pTI->uId = iPane;
				pTI->uFlags &= ~TTF_IDISHWND;
				pTI->uFlags |= TTF_NOTBUTTON | TTF_ALWAYSTIP;
				pTI->lpszText = _tcsdup(strToolTipText); // gets freed by MFC
				GetRect(iPane, &pTI->rect);
				iHit = iPane;
			}
		}
	}
	return iHit;
}

void CMuleStatusBarCtrl::OnPaint()
{
	if (!IsDarkModeEnabled()) {
		// Call the default paint handler for non-dark mode
		CStatusBarCtrl::Default(); // Calls the default painting routine
		return;
	}

	CPaintDC dc(this); // Device context for painting
	CRect rect;
	const int nParts = GetParts(0, NULL); // Get the number of parts
	const COLORREF backgroundColor = GetCustomSysColor(COLOR_WINDOW);
	const int nIconWidth = GetSystemMetrics(SM_CXSMICON);
	const int nIconHeight = GetSystemMetrics(SM_CYSMICON);
	const int nIconLeftPadding = 5;
	const int nTextLeftPadding = 4;

	CFont* pOldFont = dc.SelectObject(GetFont()); // Save the current font
	TEXTMETRIC textMetrics = {};
	dc.GetTextMetrics(&textMetrics);
	const int nTextHeight = textMetrics.tmHeight;
	const int nDescenderCompensation = max(0, textMetrics.tmDescent / 2);
	GetClientRect(&rect); // Get the dimensions of the control
	dc.FillSolidRect(&rect, backgroundColor); // Fill the entire control
	dc.SetTextColor(GetCustomSysColor(COLOR_WINDOWTEXT));
	dc.SetBkMode(TRANSPARENT); // Transparent background for text

	for (int i = 0; i < nParts; ++i) {
		CRect paneRect;
		GetRect(i, &paneRect); // Get the rectangle for each part
		dc.FillSolidRect(&paneRect, backgroundColor);

		CRect textRect(paneRect);
		CString strText = GetText(i);
		HICON hIcon = (HICON)SendMessage(SB_GETICON, i, 0);
		if (hIcon != NULL) {
			CDC memDC;
			memDC.CreateCompatibleDC(&dc);

			CBitmap bmp;
			bmp.CreateCompatibleBitmap(&dc, nIconWidth, nIconHeight);
			CBitmap* pOldBmp = memDC.SelectObject(&bmp);

			CBrush brush(backgroundColor);
			memDC.FillRect(CRect(0, 0, nIconWidth, nIconHeight), &brush);
			DrawIconEx(memDC.m_hDC, 0, 0, hIcon, nIconWidth, nIconHeight, 0, NULL, DI_NORMAL);

			const int nIconTop = paneRect.top + max(0, (paneRect.Height() - nIconHeight) / 2);
			dc.BitBlt(paneRect.left + nIconLeftPadding, nIconTop, nIconWidth, nIconHeight, &memDC, 0, 0, SRCCOPY);

			memDC.SelectObject(pOldBmp);
			bmp.DeleteObject();

			textRect.left += nIconLeftPadding + nIconWidth + nTextLeftPadding;
		}

		// Ignore part of the descender space so the glyphs align with the icon center.
		const int nTextTop = max(paneRect.top, paneRect.top + max(0, (paneRect.Height() - nTextHeight) / 2) - nDescenderCompensation);
		textRect.top = nTextTop;
		textRect.bottom = min(paneRect.bottom, nTextTop + nTextHeight);
		dc.DrawText(strText, -1, &textRect, DT_LEFT | DT_SINGLELINE);

		// Draw a separator line after the part, but keep the last pane open-ended.
		if (i < nParts - 1)
			dc.FillSolidRect(CRect(paneRect.right + 1, paneRect.top + 3, paneRect.right + 2, paneRect.bottom - 3), GetCustomSysColor(COLOR_TABBORDER));
	}

	dc.SelectObject(pOldFont);
}
