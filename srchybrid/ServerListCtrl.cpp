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
#include <share.h>
#include <io.h>
#include "emule.h"
#include "ServerListCtrl.h"
#include "OtherFunctions.h"
#include "emuledlg.h"
#include "DownloadQueue.h"
#include "ServerList.h"
#include "Server.h"
#include "ServerConnect.h"
#include "MenuCmds.h"
#include "ServerWnd.h"
#include "IrcWnd.h"
#include "Opcodes.h"
#include "Log.h"
#include "IPFilter.h"
#include "MemDC.h"
#include "eMuleAI/GeoLite2.h"
#include "eMuleAI/DarkMode.h"
#include "MuleStatusBarCtrl.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


IMPLEMENT_DYNAMIC(CServerListCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CServerListCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_GETINFOTIP, OnLvnGetInfoTip)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetDispInfo)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
	ON_WM_CONTEXTMENU()
	ON_WM_SYSCOLORCHANGE()
END_MESSAGE_MAP()

CServerListCtrl::CServerListCtrl()
	: m_pImageList(NULL)
	, m_pFontBold(NULL)
{
	SetGeneralPurposeFind(true);
	SetSkinKey(_T("ServersLv"));
}

void CServerListCtrl::UpdateBoldFont()
{
	m_pFontBold = &theApp.m_fontDefaultBold;

	if (m_fontBold.m_hObject != NULL)
		m_fontBold.DeleteObject();

	if (!thePrefs.GetUseSystemFontForMainControls())
		return;

	CFont *pFont = GetFont();
	if (pFont == NULL)
		return;

	LOGFONT lfFont;
	if (!pFont->GetLogFont(&lfFont))
		return;

	lfFont.lfWeight = FW_BOLD;
	if (m_fontBold.CreateFontIndirect(&lfFont))
		m_pFontBold = &m_fontBold;
}

bool CServerListCtrl::Init()
{
	SetPrefsKey(_T("ServerListCtrl"));
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	CToolTipCtrl *tooltip = GetToolTips();
	if (tooltip) {
		m_tooltip.SubclassWindow(*tooltip);
		tooltip->ModifyStyle(0, TTS_NOPREFIX);
		tooltip->SetDelayTime(TTDT_AUTOPOP, SEC2MS(20));
	}

	// Alignment rule: left for text, dates, and status labels; right for sizes, rates, counts, durations, and percentages.
	InsertColumn(0,	 EMPTY,	LVCFMT_LEFT,	150);			//SL_SERVERNAME
	InsertColumn(1,	 EMPTY,	LVCFMT_LEFT,	140);			//IP
	InsertColumn(2,	 EMPTY,	LVCFMT_LEFT,	150);			//DESCRIPTION
	InsertColumn(3,	 EMPTY,	LVCFMT_RIGHT,	50);			//PING
	InsertColumn(4,	 EMPTY,	LVCFMT_RIGHT,	60);			//UUSERS
	InsertColumn(5,	 EMPTY,	LVCFMT_RIGHT,	60);			//MAXCLIENT
	InsertColumn(6,	 EMPTY,	LVCFMT_RIGHT,	60);			//PW_FILES
	InsertColumn(7,	 EMPTY,	LVCFMT_LEFT,	50);			//PREFERENCE
	InsertColumn(8,	 EMPTY,	LVCFMT_RIGHT,	50);			//UFAILED
	InsertColumn(9,	 EMPTY,	LVCFMT_LEFT,	50);			//STATICSERVER
	InsertColumn(10, EMPTY,	LVCFMT_RIGHT,	60);			//SOFTFILES
	InsertColumn(11, EMPTY,	LVCFMT_RIGHT,	60, -1, true);	//HARDFILES
	InsertColumn(12, EMPTY,	LVCFMT_LEFT,	50, -1, true);	//VERSION
	InsertColumn(13, EMPTY,	LVCFMT_RIGHT,	60);			//IDLOW
	InsertColumn(14, EMPTY,	LVCFMT_RIGHT,	50);			//OBFUSCATION
	if (thePrefs.GetGeoLite2Mode() == GL2_DISABLE)
		InsertColumn(15, EMPTY, LVCFMT_LEFT, 100, -1, true);
	else
		InsertColumn(15, EMPTY, LVCFMT_LEFT, 100);

	SetAllIcons();
	UpdateBoldFont();
	Localize();
	LoadSettings();

	// Barry - Use preferred sort order from preferences
	SetSortArrow();
	SortItems(SortProc, MAKELONG(GetSortItem(), !GetSortAscending()));

	ShowServerCount();

	return true;
}

void CServerListCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
	SetAllIcons();
	UpdateBoldFont();
}

void CServerListCtrl::SetAllIcons()
{
	ApplyImageList(NULL);
	// Apply the image list also to the listview control, even if we use our own 'DrawItem'.
	// This is needed to give the listview control a chance to initialize the row height.
	m_pImageList = &theApp.emuledlg->GetClientIconList();
	VERIFY(ApplyImageList(*m_pImageList) == NULL);
}

CString CServerListCtrl::GetItemDisplayText(const CServer *server, int iSubItem) const
{
	CString sText;
	switch (iSubItem) {
	case 0: //name
		sText = server->GetListName();
		break;
	case 1: //ip:port
		sText.Format(_T("%s : %i"), server->GetAddress(), server->GetPort());
		break;
	case 2: //description
		sText = server->GetDescription();
		break;
	case 3: //ping
		if (server->GetPing())
			sText.Format(_T("%u"), server->GetPing());
		break;
	case 4: //users
		if (server->GetUsers())
			sText = CastItoIShort(server->GetUsers());
		break;
	case 5: //max users
		if (server->GetUsers())
			sText = CastItoIShort(server->GetMaxUsers());
		break;
	case 6: //files
		if (server->GetFiles())
			sText = CastItoIShort(server->GetFiles());
		break;
	case 7: //priority
		{
			LPCTSTR uid = EMPTY;;
			switch (server->GetPreference()) {
			case SRV_PR_LOW:
				uid = _T("PRIOLOW");
				break;
			case SRV_PR_NORMAL:
				uid = _T("PRIONORMAL");
				break;
			case SRV_PR_HIGH:
				uid = _T("PRIOHIGH");
				break;
			default:
				uid = _T("PRIONOPREF");
			}
			sText = GetResString(uid);
		}
		break;
	case 8: //failed count
		sText.Format(_T("%u"), server->GetFailedCount());
		break;
	case 9: //static
		sText = GetResString(server->IsStaticMember() ? _T("YES") : _T("NO"));
		break;
	case 10: //soft files
		sText = CastItoIShort(server->GetSoftFiles());
		break;
	case 11: //hard files
		sText = CastItoIShort(server->GetHardFiles());
		break;
	case 12: //version
		sText = server->GetVersion();
		if (thePrefs.GetDebugServerUDPLevel() > 0 && server->GetUDPFlags() > 0)
			sText.AppendFormat(&_T("; ExtUDP=%x")[sText.IsEmpty() ? 2 : 0], server->GetUDPFlags());
		if (thePrefs.GetDebugServerTCPLevel() > 0 && server->GetTCPFlags() > 0)
			sText.AppendFormat(&_T("; ExtTCP=%x")[sText.IsEmpty() ? 2 : 0], server->GetTCPFlags());
		break;
	case 13: //low ID users
		sText = CastItoIShort(server->GetLowIDUsers());
		break;
	case 14: //obfuscation
		sText = GetResString(server->SupportsObfuscationTCP() ? _T("YES") : _T("NO"));
		break;
	case 15:
		sText = server->GetGeolocationData();
		break;
	}
	return sText;
}

void CServerListCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	const CServer *pServer = reinterpret_cast<CServer*>(lpDrawItemStruct->itemData);
	if (!pServer || theApp.IsClosing())
		return;

	CRect rcItem(lpDrawItemStruct->rcItem);
	CMemoryDC dc(CDC::FromHandle(lpDrawItemStruct->hDC), rcItem);
	BOOL bCtrlFocused;
	InitItemMemDC(dc, lpDrawItemStruct, bCtrlFocused);
	RECT rcServer;
	GetClientRect(&rcServer);

	const CServer *pConnectedServer = theApp.serverconnect->GetCurrentServer();
	// the server which we are connected to, always has a valid numerical IP member assigned,
	// therefore we do not need to call CServer::IsEqual which would be expensive
	const bool bConnectedServer = pConnectedServer != NULL
		&& pConnectedServer->GetIP() == pServer->GetIP()
		&& pConnectedServer->GetPort() == pServer->GetPort();

	// Set selected item background color
	const bool bSelected = (lpDrawItemStruct->itemState & ODS_SELECTED) != 0;
	if (bSelected)
		dc.FillSolidRect(rcItem, GetCustomSysColor(COLOR_HIGHLIGHT));

	int nTextColorIndex = bSelected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT;
	if (bConnectedServer)
		nTextColorIndex = COLOR_SERVER_CONNECTED;
	else if (pServer->GetFailedCount() >= thePrefs.GetDeadServerRetries())
		nTextColorIndex = COLOR_SERVER_DEAD;
	else if (pServer->GetFailedCount() >= 2)
		nTextColorIndex = COLOR_SERVER_FAILED;

	dc.SetTextColor(GetServerListTextColor(nTextColorIndex, bSelected));
	if (bConnectedServer && m_pFontBold != NULL)
		dc.SelectObject(m_pFontBold);

	const CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	int iCount = pHeaderCtrl->GetItemCount();
	LONG itemLeft = rcItem.left;
	LONG iIconY = max((rcItem.Height() - 15) / 2, 0);
	for (int iCurrent = 0; iCurrent < iCount; ++iCurrent) {
		int iColumn = pHeaderCtrl->OrderToIndex(iCurrent);
		if (IsColumnHidden(iColumn))
			continue;

		UINT uDrawTextAlignment;
		int iColumnWidth = GetColumnWidth(iColumn, uDrawTextAlignment);
		rcItem.left = itemLeft;
		rcItem.right = itemLeft + iColumnWidth - sm_iSubItemInset;
		if (rcItem.left < rcItem.right && HaveIntersection(rcServer, rcItem)) {
			const CString &sItem(GetItemDisplayText(pServer, iColumn));
			switch (iColumn) {
			case 15:
			{
					if (theApp.geolite2->ShowCountryFlag()) {
						POINT point2 = { rcItem.left,rcItem.top + 1 };
						theApp.geolite2->GetFlagImageList()->DrawIndirect(&theApp.geolite2->GetFlagImageDrawParams(dc, pServer->GetCountryFlagIndex(), point2));
						rcItem.left += 22;
					}
					rcItem.left += sm_iIconOffset;
					dc.DrawText(sItem, -1, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
			}
			break;
			case 0: //server name
			{
					int iImage = 15; //server
					UINT uOverlayImage = pServer->SupportsObfuscationTCP() ? 2 : 0;

					rcItem.left = itemLeft + sm_iIconOffset;
					const POINT point = { rcItem.left, rcItem.top + iIconY };
					SafeImageListDraw(m_pImageList, dc, iImage, point, ILD_NORMAL | INDEXTOOVERLAYMASK(uOverlayImage));
					if (theApp.geolite2->ShowCountryFlag() && IsColumnHidden(15)) {
						rcItem.left += 17;
						POINT point2 = { rcItem.left,rcItem.top + 1 };
						theApp.geolite2->GetFlagImageList()->DrawIndirect(&theApp.geolite2->GetFlagImageDrawParams(dc, pServer->GetCountryFlagIndex(), point2));
						rcItem.left += sm_iSubItemInset;
					}
					rcItem.left += 17;
			}
			default: //any text column
				rcItem.left += sm_iSubItemInset;
				dc.DrawText(sItem, -1, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
			}
		}
		itemLeft += iColumnWidth;
	}

	DrawFocusRect(dc, &lpDrawItemStruct->rcItem, lpDrawItemStruct->itemState & ODS_FOCUS, bCtrlFocused, lpDrawItemStruct->itemState & ODS_SELECTED);

	m_updatethread->AddItemUpdated((LPARAM)pServer);
}

void CServerListCtrl::Localize()
{
	static const LPCTSTR uids[16] =
	{
		_T("SL_SERVERNAME"), _T("IP"), _T("DESCRIPTION"), _T("PING"), _T("UUSERS")
		, _T("MAXCLIENT"), _T("PW_FILES"), _T("PREFERENCE"), _T("UFAILED"), _T("STATICSERVER")
		, _T("SOFTFILES"), _T("HARDFILES"), _T("VERSION"), _T("IDLOW"), _T("OBFUSCATION")
		, _T("GEOLOCATION")
	};

	LocaliseHeaderCtrl(uids, _countof(uids));
}

void CServerListCtrl::RemoveServer(const CServer *pServer)
{
	int iItem = FindServer(pServer);
	if (iItem >= 0) {
		theApp.serverlist->RemoveServer(pServer);
		DeleteItem(iItem);
		ShowServerCount();
	}
}

void CServerListCtrl::RemoveAllDeadServers()
{
	ShowWindow(SW_HIDE);
	for (POSITION pos = theApp.serverlist->list.GetHeadPosition(); pos != NULL;) {
		const CServer *cur_server = theApp.serverlist->list.GetNext(pos);
		if (cur_server->GetFailedCount() >= thePrefs.GetDeadServerRetries())
		{
			// Static servers can be prevented from being removed from the list.
			if ((!cur_server->IsStaticMember()) || (!thePrefs.GetDontRemoveStaticServers()))
			{
			RemoveServer(cur_server);
	}
		}
	}
	ShowWindow(SW_SHOW);
}

void CServerListCtrl::RemoveAllFilteredServers()
{
	if (!thePrefs.GetFilterServerByIP())
		return;
	ShowWindow(SW_HIDE);
	for (POSITION pos = theApp.serverlist->list.GetHeadPosition(); pos != NULL;) {
		const CServer *cur_server = theApp.serverlist->list.GetNext(pos);
		if (theApp.ipfilter->IsFiltered(cur_server->GetIP())) {
			if (thePrefs.GetLogFilteredIPs())
				AddDebugLogLine(false, _T("IPFilter(Updated): Filtered server \"%s\" (IP=%s) - IP filter (%s)"), (LPCTSTR)EscPercent(cur_server->GetListName()), (LPCTSTR)ipstr(cur_server->GetIP()), ((LPCTSTR)theApp.ipfilter->GetLastHit()));
			// Static servers can be prevented from being removed from the list.
			if ((!cur_server->IsStaticMember()) || (!thePrefs.GetDontRemoveStaticServers()))
			{
			RemoveServer(cur_server);
			}
		}
	}
	ShowWindow(SW_SHOW);
}

bool CServerListCtrl::AddServer(const CServer *pServer, bool bAddToList, bool bRandom)
{
	bool bAddTail = !bRandom || (rand() % (1 + theApp.serverlist->GetServerCount()) != 0);
	if (!theApp.serverlist->AddServer(pServer, bAddTail))
		return false;
	if (bAddToList) {
		int iItem = InsertItem(LVIF_TEXT | LVIF_PARAM, bAddTail ? GetItemCount() : 0, pServer->GetListName(), 0, 0, 0, (LPARAM)pServer);
		Update(iItem);
	}
	ShowServerCount();
	return true;
}

void CServerListCtrl::OnContextMenu(CWnd*, CPoint point)
{
	// get merged settings
	bool bFirstItem = true;
	int iSelectedItems = GetSelectedCount();
	int iStaticServers = 0;
	UINT uPrioMenuItem = 0;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		const CServer *pServer = reinterpret_cast<CServer*>(GetItemData(GetNextSelectedItem(pos)));
		iStaticServers += static_cast<int>(pServer->IsStaticMember());

		UINT uCurPrioMenuItem;
		switch (pServer->GetPreference()) {
		case SRV_PR_LOW:
			uCurPrioMenuItem = MP_PRIOLOW;
			break;
		case SRV_PR_NORMAL:
			uCurPrioMenuItem = MP_PRIONORMAL;
			break;
		case SRV_PR_HIGH:
			uCurPrioMenuItem = MP_PRIOHIGH;
			break;
		default:
			uCurPrioMenuItem = 0;
			ASSERT(0);
		}

		if (bFirstItem)
			uPrioMenuItem = uCurPrioMenuItem;
		else if (uPrioMenuItem != uCurPrioMenuItem)
			uPrioMenuItem = 0;

		bFirstItem = false;
	}

	CMenuXP ServerMenu;
	ServerMenu.CreatePopupMenu();
	ServerMenu.AddMenuSidebar(GetResString(_T("EM_SERVER")));
	ServerMenu.AppendMenu(MF_STRING | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), MP_CONNECTTO, GetResString(_T("CONNECTTHIS")), _T("CONNECT"));
	ServerMenu.SetDefaultItem(iSelectedItems > 0 ? MP_CONNECTTO : -1);

	CMenuXP ServerPrioMenu;
	ServerPrioMenu.CreateMenu();
	if (iSelectedItems > 0) {
		ServerPrioMenu.AppendMenu(MF_STRING, MP_PRIOLOW, GetResString(_T("PRIOLOW")));
		ServerPrioMenu.AppendMenu(MF_STRING, MP_PRIONORMAL, GetResString(_T("PRIONORMAL")));
		ServerPrioMenu.AppendMenu(MF_STRING, MP_PRIOHIGH, GetResString(_T("PRIOHIGH")));
		ServerPrioMenu.CheckMenuRadioItem(MP_PRIOLOW, MP_PRIOHIGH, uPrioMenuItem, 0);
	}
	ServerMenu.AppendMenu(MF_POPUP | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), (UINT_PTR)ServerPrioMenu.m_hMenu, GetResString(_T("PRIORITY")), _T("PRIORITY"));

	// enable add/remove from static server list, if there is at least one selected server which can be used for the action
	ServerMenu.AppendMenu(MF_STRING | (iStaticServers < iSelectedItems ? MF_ENABLED : MF_GRAYED), MP_ADDTOSTATIC, GetResString(_T("ADDTOSTATIC")), _T("ListAdd"));
	ServerMenu.AppendMenu(MF_STRING | (iStaticServers > 0 ? MF_ENABLED : MF_GRAYED), MP_REMOVEFROMSTATIC, GetResString(_T("REMOVEFROMSTATIC")), _T("ListRemove"));
	ServerMenu.AppendMenu(MF_SEPARATOR);

	ServerMenu.AppendMenu(MF_STRING | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), MP_GETED2KLINK, GetResString(_T("DL_LINK1")), _T("ED2KLINK"));
	ServerMenu.AppendMenu(MF_STRING | (theApp.IsEd2kServerLinkInClipboard() ? MF_ENABLED : MF_GRAYED), MP_PASTE, GetResString(_T("SW_DIRECTDOWNLOAD")), _T("PASTELINK"));
	ServerMenu.AppendMenu(MF_STRING | (iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED), MP_REMOVE, GetResString(_T("REMOVETHIS")), _T("DELETESELECTED"));
	ServerMenu.AppendMenu(MF_STRING | (GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED), MP_REMOVEALL, GetResString(_T("REMOVEALL")), _T("DELETE"));

	ServerMenu.AppendMenu(MF_SEPARATOR);
	ServerMenu.AppendMenu(MF_STRING | (GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED), MP_FIND, GetResString(_T("FIND")), _T("Search"));

	GetPopupMenuPos(*this, point);
	ServerMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);

	VERIFY(ServerPrioMenu.DestroyMenu());
	VERIFY(ServerMenu.DestroyMenu());
}

BOOL CServerListCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	wParam = LOWORD(wParam);

	switch (wParam) {
	case MP_CONNECTTO:
	case IDA_ENTER:
		if (GetSelectedCount() > 1) {
			theApp.serverconnect->Disconnect();
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				int iItem = GetNextSelectedItem(pos);
				if (iItem > -1) {
					const CServer *pServer = reinterpret_cast<CServer*>(GetItemData(iItem));
					if (!thePrefs.IsCryptLayerRequired() || pServer->SupportsObfuscationTCP() || !pServer->TriedCrypt())
						theApp.serverlist->MoveServerDown(pServer);
				}
			}
			theApp.serverconnect->ConnectToAnyServer((UINT)(theApp.serverlist->GetServerCount() - GetSelectedCount()), false, false);
		} else {
			int iItem = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
			if (iItem > -1) {
				const CServer *pServer = reinterpret_cast<CServer*>(GetItemData(iItem));
					if (!thePrefs.IsCryptLayerRequired() || pServer->SupportsObfuscationTCP() || !pServer->TriedCrypt())
						theApp.serverconnect->ConnectToServer(reinterpret_cast<CServer*>(GetItemData(iItem)));
			}
		}
		theApp.emuledlg->ShowConnectionState();
		return TRUE;
	case MP_CUT:
		// We need a consistent behavior with the other list controls.
		{
			CString m_strServerNames;
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				const CServer* pServer = reinterpret_cast<CServer*>(GetItemData(GetNextSelectedItem(pos)));
				if (!m_strServerNames.IsEmpty())
					m_strServerNames += _T("\r\n");
				m_strServerNames += pServer->GetListName();
			}

			if (!m_strServerNames.IsEmpty()) {
				theApp.CopyTextToClipboard(m_strServerNames);
				theApp.emuledlg->statusbar->SetText(GetResString(_T("SERVER_NAME_COPIED_TO_CLIPBOARD")), SBarLog, 0);
			}
		}
		return TRUE;
	case MP_COPYSELECTED:
	case MP_GETED2KLINK:
	case Irc_SetSendLink:
		{
			const CString &strURLs(CreateSelectedServersURLs());
			if (!strURLs.IsEmpty())
				if (wParam == Irc_SetSendLink)
					theApp.emuledlg->ircwnd->SetSendFileString(strURLs);
				else {
					theApp.CopyTextToClipboard(strURLs);
					theApp.emuledlg->statusbar->SetText(GetResString(_T("SERVER_LINK_COPIED_TO_CLIPBOARD")), SBarLog, 0);
				}
		}
		return TRUE;
	case MP_PASTE:
		if (theApp.IsEd2kServerLinkInClipboard())
			theApp.emuledlg->serverwnd->PasteServerFromClipboard();
		return TRUE;
	case MP_REMOVE:
	case MPG_DELETE:
		DeleteSelectedServers();
		return TRUE;
	case MP_REMOVEALL:
		if (LocMessageBox(_T("REMOVEALLSERVERS"), MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2, 0) == IDYES) {
			if (theApp.serverconnect->IsConnecting()) {
				theApp.downloadqueue->StopUDPRequests();
				theApp.serverconnect->StopConnectionTry();
				theApp.serverconnect->Disconnect();
				theApp.emuledlg->ShowConnectionState();
			}
			ShowWindow(SW_HIDE);
			theApp.serverlist->RemoveAllServers();
			DeleteAllItems();
			ShowWindow(SW_SHOW);
			ShowServerCount();
		}
		return TRUE;
	case MP_FIND:
		OnFindStart();
		return TRUE;
	case MP_ADDTOSTATIC:
		for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
			CServer *pServer = reinterpret_cast<CServer*>(GetItemData(GetNextSelectedItem(pos)));
			if (!StaticServerFileAppend(pServer))
				return FALSE;
			RefreshServer(pServer);
		}
		return TRUE;
	case MP_REMOVEFROMSTATIC:
		for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
			CServer *pServer = reinterpret_cast<CServer*>(GetItemData(GetNextSelectedItem(pos)));
			if (!StaticServerFileRemove(pServer))
				return FALSE;
			RefreshServer(pServer);
		}
		return TRUE;
	case MP_PRIOLOW:
		SetSelectedServersPriority(SRV_PR_LOW);
		return TRUE;
	case MP_PRIONORMAL:
		SetSelectedServersPriority(SRV_PR_NORMAL);
		return TRUE;
	case MP_PRIOHIGH:
		SetSelectedServersPriority(SRV_PR_HIGH);
		return TRUE;
	}
	return FALSE;
}

CString CServerListCtrl::CreateSelectedServersURLs()
{
	CString links;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		const CServer *pServer = reinterpret_cast<CServer*>(GetItemData(GetNextSelectedItem(pos)));
		if (!links.IsEmpty())
			links += _T("\r\n");
		links.AppendFormat(_T("ed2k://|server|%s|%u|/"), pServer->GetAddress(), pServer->GetPort());
	}
	return links;
}

void CServerListCtrl::DeleteSelectedServers()
{
	SetRedraw(false);
	POSITION pos;
	while ((pos = GetFirstSelectedItemPosition()) != NULL) {
		int iItem = GetNextSelectedItem(pos);
		theApp.serverlist->RemoveServer(reinterpret_cast<const CServer*>(GetItemData(iItem)));
		DeleteItem(iItem);
	}
	ShowServerCount();
	SetRedraw(true);
	SetFocus();
	AutoSelectItem();
}

void CServerListCtrl::SetSelectedServersPriority(UINT uPriority)
{
	bool bUpdateStaticServersFile = false;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		CServer *pServer = reinterpret_cast<CServer*>(GetItemData(GetNextSelectedItem(pos)));
		if (pServer->GetPreference() != uPriority) {
			pServer->SetPreference(uPriority);
			if (pServer->IsStaticMember())
				bUpdateStaticServersFile = true;
			RefreshServer(pServer);
		}
	}
	if (bUpdateStaticServersFile)
		theApp.serverlist->SaveStaticServers();
}

void CServerListCtrl::OnNmDblClk(LPNMHDR, LRESULT*)
{
	int iItem = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	if (iItem >= 0) {
		theApp.serverconnect->ConnectToServer(reinterpret_cast<CServer*>(GetItemData(iItem)));
		theApp.emuledlg->ShowConnectionState();
	}
}

bool CServerListCtrl::AddServerMetToList(const CString &strFile)
{
	SetRedraw(false);
	bool bResult = theApp.serverlist->AddServerMetToList(strFile, true);
	RemoveAllDeadServers();
	ShowServerCount();
	SetRedraw(true);
	return bResult;
}

void CServerListCtrl::RefreshServer(const CServer *pServer)
{
	if (theApp.IsClosing() || !pServer || theApp.emuledlg->activewnd != theApp.emuledlg->serverwnd || !theApp.emuledlg->serverwnd->serverlistctrl.IsWindowVisible())
		return;

	m_updatethread->AddItemToUpdate((LPARAM)pServer);
}

void CServerListCtrl::RefreshAllServer() {

	for (POSITION pos = theApp.serverlist->list.GetHeadPosition(); pos != NULL;) {
		RefreshServer(theApp.serverlist->list.GetAt(pos));
		theApp.serverlist->list.GetNext(pos);
	}

}

void CServerListCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	bool sortAscending;
	if (GetSortItem() != pNMLV->iSubItem)
		switch (pNMLV->iSubItem) {
		case 4: // Users
		case 5: // Max Users
		case 6: // Files
		case 7: // Priority
		case 9: // Static
		case 10: // Soft Files
		case 11: // Hard Files
		case 12: // Version
		case 13: // Low IDs
		case 14: // Obfuscation
			sortAscending = false;
			break;
		default:
			sortAscending = true;
		}
	else
		sortAscending = !GetSortAscending();

	// Sort table
	UpdateSortHistory(MAKELONG(pNMLV->iSubItem, !sortAscending));
	SetSortArrow(pNMLV->iSubItem, sortAscending);
	SortItems(SortProc, MAKELONG(pNMLV->iSubItem, !sortAscending));
	*pResult = 0;
}

int CALLBACK CServerListCtrl::SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort)
{
	if (lParam1 == 0 || lParam2 == 0)
		return 0;
	const CServer *item1 = reinterpret_cast<CServer*>(lParam1);
	const CServer *item2 = reinterpret_cast<CServer*>(lParam2);

	int iResult;
	switch (LOWORD(lParamSort)) {
	case 0:
		iResult = Undefined_at_bottom(item1->GetListName(), item2->GetListName());
		break;
	case 1:
		if (item1->HasDynIP() && item2->HasDynIP())
			iResult = item1->GetDynIP().CompareNoCase(item2->GetDynIP());
		else if (item1->HasDynIP())
			iResult = -1;
		else if (item2->HasDynIP())
			iResult = 1;
		else {
			iResult = CompareUnsigned(htonl(item1->GetIP()), htonl(item2->GetIP()));
			if (!iResult)
				iResult = CompareUnsigned(item1->GetPort(), item2->GetPort());
		}
		break;
	case 2:
		iResult = Undefined_at_bottom(item1->GetDescription(), item2->GetDescription());
		break;
	case 3:
		iResult = Undefined_at_bottom(item1->GetPing(), item2->GetPing());
		break;
	case 4:
		iResult = Undefined_at_bottom(item1->GetUsers(), item2->GetUsers());
		break;
	case 5:
		iResult = Undefined_at_bottom(item1->GetMaxUsers(), item2->GetMaxUsers());
		break;
	case 6:
		iResult = Undefined_at_bottom(item1->GetFiles(), item2->GetFiles());
		break;
	case 7:
		if (item2->GetPreference() == item1->GetPreference())
			iResult = 0;
		else if (item2->GetPreference() == SRV_PR_LOW)
			iResult = 1;
		else if (item1->GetPreference() == SRV_PR_LOW)
			iResult = -1;
		else if (item2->GetPreference() == SRV_PR_HIGH)
			iResult = -1;
		else if (item1->GetPreference() == SRV_PR_HIGH)
			iResult = 1;
		else
			iResult = 0;
		break;
	case 8:
		iResult = CompareUnsigned(item1->GetFailedCount(), item2->GetFailedCount());
		break;
	case 9:
		iResult = (int)item1->IsStaticMember() - (int)item2->IsStaticMember();
		break;
	case 10:
		iResult = Undefined_at_bottom(item1->GetSoftFiles(), item2->GetSoftFiles());
		break;
	case 11:
		iResult = Undefined_at_bottom(item1->GetHardFiles(), item2->GetHardFiles());
		break;
	case 12:
		iResult = Undefined_at_bottom(item1->GetVersion(), item2->GetVersion());
		break;
	case 13:
		iResult = Undefined_at_bottom(item1->GetLowIDUsers(), item2->GetLowIDUsers());
		break;
	case 14:
		iResult = (int)(item1->SupportsObfuscationTCP()) - (int)(item2->SupportsObfuscationTCP());
		break;
	case 15:
		iResult = Undefined_at_bottom(item1->GetGeolocationData(), item2->GetGeolocationData());
		break;

	default:
		iResult = 0;
	}
	if (iResult > 3)
		return iResult - 5;

	// Handled in parent class

	return HIWORD(lParamSort) ? -iResult : iResult;
}

bool CServerListCtrl::StaticServerFileAppend(CServer *pServer)
{
	AddLogLine(false, _T("'%s:%i,%s' %s"), pServer->GetAddress(), pServer->GetPort(), (LPCTSTR)EscPercent(pServer->GetListName()), (LPCTSTR)GetResString(_T("ADDED2SSF")));
	pServer->SetIsStaticMember(true);
	bool bResult = theApp.serverlist->SaveStaticServers();
	RefreshServer(pServer);
	return bResult;
}

bool CServerListCtrl::StaticServerFileRemove(CServer *pServer)
{
	if (!pServer->IsStaticMember())
		return true;
	pServer->SetIsStaticMember(false);
	return theApp.serverlist->SaveStaticServers();
}

void CServerListCtrl::ShowServerCount()
{
	CString sCount(GetResString(_T("SV_SERVERLIST")));
	sCount.AppendFormat(_T(" (%i)"), GetItemCount());
	theApp.emuledlg->serverwnd->SetDlgItemText(IDC_SERVLIST_TEXT, sCount);
}

bool CServerListCtrl::ShouldShowPersistentInfoTip(const SPersistentInfoTipContext& context)
{
	if (!CMuleListCtrl::ShouldShowPersistentInfoTip(context))
		return false;

	bool bShowInfoTip = GetSelectedCount() > 1 && GetKeyState(VK_CONTROL) < 0;
	if (bShowInfoTip) {
		bool bInfoTipItemIsPartOfMultiSelection = false;
		for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
			if (GetNextSelectedItem(pos) == context.iItem) {
				bInfoTipItemIsPartOfMultiSelection = true;
				break;
			}
		}
		if (!bInfoTipItemIsPartOfMultiSelection)
			bShowInfoTip = false;
	}

	return bShowInfoTip;
}

bool CServerListCtrl::GetPersistentInfoTipText(const SPersistentInfoTipContext& /*context*/, CString& strText)
{
	int iSelected = 0;
	ULONGLONG ulTotalUsers = 0;
	ULONGLONG ulTotalLowIdUsers = 0;
	ULONGLONG ulTotalFiles = 0;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		const CServer* pServer = reinterpret_cast<CServer*>(GetItemData(GetNextSelectedItem(pos)));
		if (pServer) {
			++iSelected;
			ulTotalUsers += pServer->GetUsers();
			ulTotalFiles += pServer->GetFiles();
			ulTotalLowIdUsers += pServer->GetLowIDUsers();
		}
	}

	if (iSelected <= 0)
		return false;

	strText.Format(_T("%s: %i\r\n%s: %s\r\n%s: %s\r\n%s: %s") TOOLTIP_AUTOFORMAT_SUFFIX
		, (LPCTSTR)GetResString(_T("FSTAT_SERVERS"))
		, iSelected
		, (LPCTSTR)GetResString(_T("UUSERS")), (LPCTSTR)CastItoIShort(ulTotalUsers)
		, (LPCTSTR)GetResString(_T("IDLOW")), (LPCTSTR)CastItoIShort(ulTotalLowIdUsers)
		, (LPCTSTR)GetResString(_T("PW_FILES")), (LPCTSTR)CastItoIShort(ulTotalFiles));
	return true;
}

int CServerListCtrl::GetDefaultPersistentInfoTipExtraLeftPadding(const SPersistentInfoTipContext& context) const
{
	return (context.iSubItem == 15 && theApp.geolite2->ShowCountryFlag()) ? 22 + sm_iIconOffset : 0;
}

void CServerListCtrl::OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult)
{
	CMuleListCtrl::OnLvnGetInfoTip(pNMHDR, pResult);
}

int CServerListCtrl::Undefined_at_bottom(const uint32 i1, const uint32 i2)
{
	if (i1 == i2)
		return 5;
	if (i1 == 0)
		return 6;
	if (i2 == 0)
		return 4;
	return CompareUnsigned(i1, i2);
}

int CServerListCtrl::Undefined_at_bottom(const CString &s1, const CString &s2)
{
	if (s1.IsEmpty())
		return s2.IsEmpty() ? 5 : 6;
	return s2.IsEmpty() ? 4 : sgn(s1.CompareNoCase(s2));
}

int CServerListCtrl::FindServer(const CServer *pServer)
{
	LVFINDINFO find;
	find.flags = LVFI_PARAM;
	find.lParam = (LPARAM)pServer;
	return FindItem(&find);
}

void CServerListCtrl::OnLvnGetDispInfo(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (!theApp.IsClosing()) {
		// Although we have an owner drawn listview control we store the text for the primary item in the listview, to be
		// capable of quick searching those items via the keyboard. Because our listview items may change their contents,
		// we do this via a text callback function. The listview control will send us the LVN_DISPINFO notification if
		// it needs to know the contents of the primary item.
		//
		// But, the listview control sends this notification all the time, even if we do not search for an item. At least
		// this notification is only sent for the visible items and not for all items in the list. Though, because this
		// function is invoked *very* often, do *NOT* put any time consuming code in here.
		//
		// Vista: That callback is used to get the strings for the label tips for the sub(!) items.
		//
		NMLVDISPINFO* pDispInfo = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
		if (pDispInfo->item.mask & LVIF_TEXT) {
			const CServer* server = reinterpret_cast<CServer*>(pDispInfo->item.lParam);
			if (server != NULL)
				_tcsncpy_s(pDispInfo->item.pszText, pDispInfo->item.cchTextMax, GetItemDisplayText(server, pDispInfo->item.iSubItem), _TRUNCATE);
		}
	}
	*pResult = 0;
}
