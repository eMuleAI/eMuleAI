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

#include "StdAfx.h"
#include "IrcChannelListCtrl.h"
#include "emuleDlg.h"
#include "otherfunctions.h"
#include "MenuCmds.h"
#include "IrcWnd.h"
#include "IrcMain.h"
#include "emule.h"
#include "MemDC.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

struct ChannelName
{
	ChannelName(const CString &sName, UINT uUsers, const CString &sDesc)
		: m_sName(sName)
		, m_sDesc(sDesc)
		, m_uUsers(uUsers)
	{
	}

	CString m_sName;
	CString m_sDesc;
	UINT	m_uUsers;
};

IMPLEMENT_DYNAMIC(CIrcChannelListCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CIrcChannelListCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetDispInfo)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
	ON_WM_CONTEXTMENU()
END_MESSAGE_MAP()

CIrcChannelListCtrl::CIrcChannelListCtrl()
{
	m_pParent = NULL;
	SetSkinKey(_T("IRCChannelsLv"));
}

CIrcChannelListCtrl::~CIrcChannelListCtrl()
{
	ResetServerChannelList(true);
}

void CIrcChannelListCtrl::Init()
{
	SetPrefsKey(_T("IrcChannelListCtrl"));
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	// Alignment rule: left for text, dates, and status labels; right for sizes, rates, counts, durations, and percentages.
	InsertColumn(0, EMPTY, LVCFMT_LEFT, 200);	//IRC_NAME
	InsertColumn(1, EMPTY, LVCFMT_RIGHT, 50);	//UUSERS
	InsertColumn(2, EMPTY, LVCFMT_LEFT, 350);	//DESCRIPTION

	LoadSettings();
	SetSortArrow();
	SortItems(&SortProc, MAKELONG(GetSortItem(), !GetSortAscending()));
}

void CIrcChannelListCtrl::Localize()
{
	static const LPCTSTR uids[3] =
	{
		_T("IRC_NAME"), _T("UUSERS"), _T("DESCRIPTION")
	};
	LocaliseHeaderCtrl(uids, _countof(uids));
}

CString CIrcChannelListCtrl::GetItemDisplayText(const ChannelName *pChannel, int iSubItem) const
{
	CString sText;
	switch (iSubItem) {
	case 0:
		sText = pChannel->m_sName;
		break;
	case 1:
		sText.Format(_T("%u"), pChannel->m_uUsers);
		break;
	case 2:
		sText = pChannel->m_sDesc;
	}
	return sText;
}

void CIrcChannelListCtrl::OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult)
{
	if (!theApp.IsClosing()) {
		// Although we have an owner drawn listview control we store the text for the primary item in the
		// listview, to be capable of quick searching those items via the keyboard. Because our listview
		// items may change their contents, we do this via a text callback function. The listview control
		// will send us the LVN_DISPINFO notification if it needs to know the contents of the primary item.
		//
		// But, the listview control sends this notification all the time, even if we do not search for an item.
		// At least this notification is only sent for the visible items and not for all items in the list.
		// Though, because this function is invoked *very* often, do *NOT* put any time consuming code in here.
		//
		// Vista: That callback is used to get the strings for the label tips for the sub(!)-items.
		//
		const LVITEMW &rItem = reinterpret_cast<NMLVDISPINFO*>(pNMHDR)->item;
		if (rItem.mask & LVIF_TEXT) {
			const ChannelName *pChannel = reinterpret_cast<ChannelName*>(rItem.lParam);
			if (pChannel != NULL)
				_tcsncpy_s(rItem.pszText, rItem.cchTextMax, GetItemDisplayText(pChannel, rItem.iSubItem), _TRUNCATE);
		}
	}
	*pResult = 0;
}

void CIrcChannelListCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	bool bSortAscending = (GetSortItem() != pNMLV->iSubItem || !GetSortAscending());

	SetSortArrow(pNMLV->iSubItem, bSortAscending);
	SortItems(SortProc, MAKELONG(pNMLV->iSubItem, !bSortAscending));
	*pResult = 0;
}

int CALLBACK CIrcChannelListCtrl::SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort)
{
	const ChannelName *pItem1 = reinterpret_cast<ChannelName*>(lParam1);
	const ChannelName *pItem2 = reinterpret_cast<ChannelName*>(lParam2);

	int iResult = 0;
	switch (LOWORD(lParamSort)) {
	case 0:
		iResult = pItem1->m_sName.CompareNoCase(pItem2->m_sName);
		break;
	case 1:
		iResult = CompareUnsigned(pItem1->m_uUsers, pItem2->m_uUsers);
		break;
	case 2:
		iResult = pItem1->m_sDesc.CompareNoCase(pItem2->m_sDesc);
		break;
	default:
		return 0;
	}
	return HIWORD(lParamSort) ? -iResult : iResult;
}

void CIrcChannelListCtrl::OnNmDblClk(LPNMHDR, LRESULT *pResult)
{
	JoinChannels();
	*pResult = 0;
}

void CIrcChannelListCtrl::OnContextMenu(CWnd*, CPoint point)
{
	int iCurSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	CMenuXP menuChannel;
	menuChannel.CreatePopupMenu();
	menuChannel.AddMenuSidebar(GetResString(_T("IRC_CHANNEL")));
	menuChannel.AppendMenu(MF_STRING, Irc_Join, GetResString(_T("IRC_JOIN")));
	if (iCurSel == -1 || !m_pParent->GetLoggedIn())
		menuChannel.EnableMenuItem(Irc_Join, MF_GRAYED);
	GetPopupMenuPos(*this, point);
	menuChannel.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
	VERIFY(menuChannel.DestroyMenu());
}

BOOL CIrcChannelListCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	switch (wParam) {
	case Irc_Join:
		//Pressed the join button.
		JoinChannels();
		return TRUE;
	}
	return TRUE;
}

bool CIrcChannelListCtrl::AddChannelToList(const CString &sName, const CString &sUsers, const CString &sDesc)
{
	UINT uUsers = _tstoi(sUsers);
	if (thePrefs.GetIRCUseChannelFilter() && uUsers < thePrefs.GetIRCChannelUserFilter())
		return false;

	ChannelName *pChannel = new ChannelName(sName, uUsers, m_pParent->StripMessageOfFontCodes(sDesc));
	m_lstChannelNames.AddTail(pChannel);
	int iItem = InsertItem(LVIF_TEXT | LVIF_PARAM, GetItemCount(), LPSTR_TEXTCALLBACK, 0, 0, 0, (LPARAM)pChannel);
	if (iItem < 0)
		return false;
	SetItemText(iItem, 1, LPSTR_TEXTCALLBACK);
	SetItemText(iItem, 2, LPSTR_TEXTCALLBACK);
	return true;
}

void CIrcChannelListCtrl::ResetServerChannelList(bool bShutDown)
{
	while (!m_lstChannelNames.IsEmpty())
		delete m_lstChannelNames.RemoveHead();
	if (!bShutDown)
		DeleteAllItems();
}

void CIrcChannelListCtrl::JoinChannels()
{
	if (!m_pParent->GetLoggedIn())
		return;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		int iIndex = GetNextSelectedItem(pos);
		if (iIndex >= 0)
			m_pParent->m_pIrcMain->SendString(_T("JOIN ") + GetItemText(iIndex, 0));
	}
}
