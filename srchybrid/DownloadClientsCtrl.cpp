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
#include "emuledlg.h"
#include "DownloadClientsCtrl.h"
#include "ClientDetailDialog.h"
#include "MemDC.h"
#include "MenuCmds.h"
#include "TransferDlg.h"
#include "OtherFunctions.h"
#include "UpDownClient.h"
#include "UploadQueue.h"
#include "ClientCredits.h"
#include "PartFile.h"
#include "FriendList.h"
#include "ChatWnd.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "SharedFileList.h"
#include "MuleStatusBarCtrl.h"
#include "ClientList.h"
#include "eMuleAI/Shield.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


IMPLEMENT_DYNAMIC(CDownloadClientsCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CDownloadClientsCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetDispInfo)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
	ON_WM_CONTEXTMENU()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_KEYDOWN()
END_MESSAGE_MAP()

CDownloadClientsCtrl::CDownloadClientsCtrl()
	: CListCtrlItemWalk(this)
{
	SetGeneralPurposeFind(true);
	SetSkinKey(_T("DownloadingLv"));
}

CDownloadClientsCtrl::~CDownloadClientsCtrl()
{
	m_ListItemsMap.clear();
}

void CDownloadClientsCtrl::Init()
{
	SetPrefsKey(_T("DownloadClientsCtrl"));
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	InsertColumn(0,	EMPTY,	LVCFMT_LEFT,	DFLT_CLIENTNAME_COL_WIDTH);	//QL_USERNAME
	InsertColumn(1,	EMPTY,	LVCFMT_LEFT,	DFLT_CLIENTSOFT_COL_WIDTH);	//CD_CSOFT
	InsertColumn(2,	EMPTY,	LVCFMT_LEFT,	DFLT_FILENAME_COL_WIDTH);	//FILE
	InsertColumn(3,	EMPTY,	LVCFMT_RIGHT,	DFLT_DATARATE_COL_WIDTH);	//DL_SPEED
	InsertColumn(4,	EMPTY,	LVCFMT_LEFT,	DFLT_PARTSTATUS_COL_WIDTH);	//AVAILABLEPARTS
	InsertColumn(5,	EMPTY,	LVCFMT_RIGHT,	DFLT_SIZE_COL_WIDTH);		//CL_TRANSFDOWN
	InsertColumn(6,	EMPTY,	LVCFMT_RIGHT,	DFLT_SIZE_COL_WIDTH);		//CL_TRANSFUP
	InsertColumn(7,	EMPTY,	LVCFMT_LEFT,	100);						//META_SRCTYPE
	InsertColumn(8, EMPTY, LVCFMT_LEFT,	DFLT_HASH_COL_WIDTH);
	InsertColumn(9, EMPTY, LVCFMT_LEFT,	100); // IP:Port Column
	InsertColumn(10, EMPTY, LVCFMT_LEFT,	50);
	InsertColumn(11, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(12, EMPTY, LVCFMT_LEFT, 80);
	InsertColumn(13, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(14, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(15, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(16, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(17, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(18, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(19, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(20, EMPTY, LVCFMT_LEFT, 100);

	SetAllIcons();
	LoadSettings();
	SetSortArrow();
	SortItems(SortProc, MAKELONG(GetSortItem(), !GetSortAscending()));
}

void CDownloadClientsCtrl::Localize()
{
	static const LPCTSTR uids[21] =
	{
		_T("QL_USERNAME"), _T("CD_CSOFT"), _T("FILE"), _T("DL_SPEED"), _T("AVAILABLEPARTS")
		, _T("CL_TRANSFDOWN"), _T("CL_TRANSFUP"), _T("META_SRCTYPE")
		, _T("CD_UHASH2")
		, _T("IPPORT")
		, _T("GEOLOCATION")
		, _T("SHAREDFILESSTATUS")
		, _T("SHAREDFILESCOUNTCOLUMN")
		, _T("SHAREDFILESLASTQUERIED")
		, _T("FRIEND")
		, _T("ID_TYPE")
		, _T("BAD_CLIENT_TYPE")
		, _T("PUNISHMENT")
		, _T("FIRST_SEEN")
		, _T("LAST_SEEN")
		, _T("CLIENT_NOTE")

	};

	LocaliseHeaderCtrl(uids, _countof(uids));
}

void CDownloadClientsCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
	SetAllIcons();
}

void CDownloadClientsCtrl::SetAllIcons()
{
	ApplyImageList(NULL);
	// Apply the image list also to the listview control, even if we use our own 'DrawItem'.
	// This is needed to give the listview control a chance to initialize the row height.
	m_pImageList = &theApp.emuledlg->GetClientIconList();
	VERIFY(ApplyImageList(*m_pImageList) == NULL);
}

void CDownloadClientsCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	if (!lpDrawItemStruct->itemData || theApp.IsClosing())
		return;

	CRect rcItem(lpDrawItemStruct->rcItem);
	CMemoryDC dc(CDC::FromHandle(lpDrawItemStruct->hDC), rcItem);
	BOOL bCtrlFocused;
	InitItemMemDC(dc, lpDrawItemStruct, bCtrlFocused);

	// Set selected item background color
	if ((lpDrawItemStruct->itemState & ODS_SELECTED) != 0)
		dc.FillSolidRect(rcItem, GetCustomSysColor(COLOR_HIGHLIGHT));

	RECT rcClient;
	GetClientRect(&rcClient);
	CUpDownClient *client = reinterpret_cast<CUpDownClient*>(lpDrawItemStruct->itemData);

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
		rcItem.right = itemLeft + iColumnWidth;
		if (rcItem.left < rcItem.right && HaveIntersection(rcClient, rcItem)) {
			const CString &sItem(GetItemDisplayText(client, iColumn));
			switch (iColumn) {
			case 0: //user name
			{
					int iImage;
					UINT uOverlayImage;
					client->GetDisplayImage(iImage, uOverlayImage);

					rcItem.left += sm_iIconOffset;
					const POINT point = { rcItem.left, rcItem.top + iIconY };
					SafeImageListDraw(m_pImageList, dc, iImage, point, ILD_NORMAL | INDEXTOOVERLAYMASK(uOverlayImage));
				    if (theApp.geolite2->ShowCountryFlag() && IsColumnHidden(10)) {
					    rcItem.left += 20;
						POINT point2 = { rcItem.left,rcItem.top + 1 };
						theApp.geolite2->GetFlagImageList()->DrawIndirect(&theApp.geolite2->GetFlagImageDrawParams(dc, client->GetCountryFlagIndex(), point2));
						rcItem.left += sm_iSubItemInset;
					}
					rcItem.left += 17;
			}
			default: //any text column
				rcItem.left += sm_iSubItemInset;
				rcItem.right -= sm_iSubItemInset;
				dc.DrawText(sItem, -1, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
				break;
			case 4: //download status bar
				++rcItem.top;
				--rcItem.bottom;
				client->DrawStatusBar(dc, &rcItem, false, thePrefs.UseFlatBar());
				++rcItem.bottom;
				--rcItem.top;
				break;
			case 10:
			{
				if (theApp.geolite2->ShowCountryFlag()) {
					POINT point2 = { rcItem.left,rcItem.top + 1 };
					theApp.geolite2->GetFlagImageList()->DrawIndirect(&theApp.geolite2->GetFlagImageDrawParams(dc, client->GetCountryFlagIndex(), point2));
					rcItem.left += 22;
				}
				rcItem.left += sm_iIconOffset;
				dc->DrawText(sItem, sItem.GetLength(), &rcItem, MLC_DT_TEXT);
			}
			break;
			}
		}
		itemLeft += iColumnWidth;
	}

	DrawFocusRect(dc, &lpDrawItemStruct->rcItem, lpDrawItemStruct->itemState & ODS_FOCUS, bCtrlFocused, lpDrawItemStruct->itemState & ODS_SELECTED);

	m_updatethread->AddItemUpdated((LPARAM)client);
}

CString CDownloadClientsCtrl::GetItemDisplayText( CUpDownClient* client, int iSubItem) const
{
	CString sText;
	switch (iSubItem) {
	case 0:
		if (client->GetUserName() != NULL)
			sText = client->GetUserName();
		else
			sText.Format(_T("(%s)"), (LPCTSTR)GetResString(_T("UNKNOWN")));
		break;
	case 1:
		sText = client->DbgGetFullClientSoftVer();
		if (sText.IsEmpty())
			sText = GetResString(_T("UNKNOWN"));
		break;
	case 2:
		sText = client->GetRequestFile()->GetFileName();
		break;
	case 3:
		sText = CastItoXBytes((float)client->GetDownloadDatarate(), false, true);
		break;
	case 4:
		sText = GetResString(_T("AVAILABLEPARTS"));
		break;
	case 5:
		if (client->credits == NULL || client->GetSessionDown() >= client->credits->GetDownloadedTotal())
			sText = CastItoXBytes(client->GetSessionDown());
		else
			sText.Format(_T("%s (%s)"), (LPCTSTR)CastItoXBytes(client->GetSessionDown()), (LPCTSTR)CastItoXBytes(client->credits->GetDownloadedTotal()));
		break;
	case 6:
		if (client->credits == NULL || client->GetSessionUp() >= client->credits->GetUploadedTotal())
			sText = CastItoXBytes(client->GetSessionUp());
		else
			sText.Format(_T("%s (%s)"), (LPCTSTR)CastItoXBytes(client->GetSessionUp()), (LPCTSTR)CastItoXBytes(client->credits->GetUploadedTotal()));
		break;
	case 7:
		{
			LPCTSTR uid;
			switch (client->GetSourceFrom()) {
			case SF_SERVER:
				uid = _T("ED2KSERVER");
				break;
			case SF_KADEMLIA:
				uid = _T("KADEMLIA");
				break;
			case SF_SOURCE_EXCHANGE:
				uid = _T("SE");
				break;
			case SF_PASSIVE:
				uid = _T("PASSIVE");
				break;
			case SF_LINK:
				uid = _T("SW_LINK");
				break;
				case SF_SLS:
					uid = _T("SOURCE_LOADER_SAVER");
					break;
			default:
				uid = _T("UNKNOWN");
			}
			sText = GetResString(uid);
		}
		break;
	case 8:
		sText = md4str(client->GetUserHash());
		break;
	case 9:
		sText.Format(_T("%s:%u"), ipstr(!client->GetIP().IsNull() ? client->GetIP() : client->GetConnectIP()), client->GetUserPort());
		break;
	case 10:
		sText = client->GetGeolocationData();
		break;
	case 11:
		sText = client->GetSharedFilesStatusText();
		break;
	case 12:
		sText.Format(_T("%u"), client->m_uSharedFilesCount);
		break;
	case 13:
		if (client->m_tSharedFilesLastQueriedTime)
			sText.Format(_T(" %s"), CastSecondsToHM((time(NULL) - client->m_tSharedFilesLastQueriedTime)));
		else
			sText = EMPTY;
		break;
	case 14:
		if (client->IsFriend())
			sText = GetResString(_T("YES"));
		else
			sText = GetResString(_T("NO"));
		break;
	case 15:
		if (client->HasLowID())
			sText = GetResString(_T("IDLOW"));
		else
			sText = GetResString(_T("IDHIGH"));
		break;
	case 16:
		sText = client->GetPunishmentReason();
		break;
	case 17:
		sText = client->GetPunishmentText();
		break;
	case 18:
		if (client->tFirstSeen)
			sText.Format(_T(" %s"), CastSecondsToHM(time(NULL) - client->tFirstSeen));
		else
			sText = _T("Unknown");
		break;
	case 19:
		if (client->tLastSeen)
			sText.Format(_T(" %s"), CastSecondsToHM(time(NULL) - client->tLastSeen));
		else
			sText = _T("Unknown");
		break;
	case 20:
		sText = client->m_strClientNote;
		break;
	}
	return sText;
}


void CDownloadClientsCtrl::OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult)
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
			CUpDownClient *pClient = reinterpret_cast<CUpDownClient*>(rItem.lParam);
			if (pClient != NULL)
				_tcsncpy_s(rItem.pszText, rItem.cchTextMax, GetItemDisplayText(pClient, rItem.iSubItem), _TRUNCATE);
		}
	}
	*pResult = 0;
}

void CDownloadClientsCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	bool sortAscending;
	if (GetSortItem() != pNMLV->iSubItem)
		switch (pNMLV->iSubItem) {
		case 1: // Client Software
		case 3: // Download Rate
		case 4: // Part Count
		case 5: // Session Down
		case 6: // Session Up
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

int CALLBACK CDownloadClientsCtrl::SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort)
{
	CUpDownClient* item1 = reinterpret_cast<CUpDownClient*>(lParam1);
	CUpDownClient* item2 = reinterpret_cast<CUpDownClient*>(lParam2);
	LPARAM iColumn = (lParamSort >= 100) ? lParamSort - 100 : lParamSort;
	int iResult = 0;
	switch (LOWORD(lParamSort)) {
	case 0: //user name
		if (item1->GetUserName() && item2->GetUserName())
			iResult = CompareLocaleStringNoCase(item1->GetUserName(), item2->GetUserName());
		else if (item1->GetUserName() == NULL)
			iResult = 1; // place clients with no user names at bottom
		else if (item2->GetUserName() == NULL)
			iResult = -1; // place clients with no user names at bottom
		break;
	case 1: //version
		iResult = CompareLocaleStringNoCase(item1->DbgGetFullClientSoftVer(), item2->DbgGetFullClientSoftVer());
		break;
	case 2: //file name
		{
			const CKnownFile *file1 = item1->GetRequestFile();
			const CKnownFile *file2 = item2->GetRequestFile();
			if ((file1 != NULL) && (file2 != NULL))
				iResult = CompareLocaleStringNoCase(file1->GetFileName(), file2->GetFileName());
			else if (file1 == NULL)
				iResult = 1;
			else
				iResult = -1;
		}
		break;
	case 3: //download rate
		iResult = CompareUnsigned(item1->GetDownloadDatarate(), item2->GetDownloadDatarate());
		break;
	case 4: //part count
		iResult = CompareUnsigned(item1->GetPartCount(), item2->GetPartCount());
		break;
	case 5: //session download
		iResult = CompareUnsigned(item1->GetSessionDown(), item2->GetSessionDown());
		break;
	case 6: //session upload
		iResult = CompareUnsigned(item1->GetSessionUp(), item2->GetSessionUp());
		break;
	case 7: //source origin
		iResult = item1->GetSourceFrom() - item2->GetSourceFrom();
		break;
	case 8: //hash
		iResult = memcmp(item1->GetUserHash(), item2->GetUserHash(), 16);
		break;
	case 9:
		iResult = CompareIP(!item1->GetIP().IsNull() ? item1->GetIP() : item1->GetConnectIP(), !item2->GetIP().IsNull() ? item2->GetIP() : item2->GetConnectIP());
		if (iResult == 0)
			iResult = CompareUnsigned(item1->GetUserPort(), item2->GetUserPort());
		break;
	case 10:
		if (item1->GetGeolocationData(true) && item2->GetGeolocationData(true))
			iResult = CompareLocaleStringNoCase(item1->GetGeolocationData(true), item2->GetGeolocationData(true));
		else if (item1->GetGeolocationData(true))
			iResult = 1;
		else
			iResult = -1;
		break;
	case 11:
		iResult = CompareLocaleStringNoCase(item1->GetSharedFilesStatusText(), item2->GetSharedFilesStatusText());
		break;
	case 12:
		iResult = CompareUnsigned(item1->m_uSharedFilesCount, item2->m_uSharedFilesCount);
		break;
	case 13:
		iResult = CompareUnsigned(item1->m_tSharedFilesLastQueriedTime, item2->m_tSharedFilesLastQueriedTime);
		break;
	case 14:
		iResult = CompareUnsigned(item1->IsFriend(), item2->IsFriend());
		break;
	case 15:
		iResult = CompareUnsigned(item1->HasLowID(), item2->HasLowID());
		break;
	case 16:
		iResult = CompareLocaleStringNoCase(item1->GetPunishmentReason(), item2->GetPunishmentReason());
		break;
	case 17:
		iResult = CompareLocaleStringNoCase(item1->GetPunishmentText(), item2->GetPunishmentText());
		break;
	case 18:
		iResult = CompareUnsigned(item1->tFirstSeen, item2->tFirstSeen);
		break;
	case 19:
		iResult = CompareUnsigned(item1->tLastSeen, item2->tLastSeen);
		break;
	case 20:
		iResult = CompareLocaleStringNoCase(item1->m_strClientNote, item2->m_strClientNote);
		break;
	}

	if (HIWORD(lParamSort))
		iResult = -iResult;

	// Handled in parent class

	return iResult;
}

void CDownloadClientsCtrl::OnNmDblClk(LPNMHDR, LRESULT *pResult)
{
	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	if (iSel >= 0) {
		CUpDownClient *client = reinterpret_cast<CUpDownClient*>(GetItemData(iSel));
		if (client) {
			CClientDetailDialog dialog(client, this);
			dialog.DoModal();
		}
	}
	*pResult = 0;
}

void CDownloadClientsCtrl::OnContextMenu(CWnd*, CPoint point)
{
	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	const CUpDownClient *client = reinterpret_cast<CUpDownClient*>(iSel >= 0 ? GetItemData(iSel) : NULL);
	const bool is_ed2k = client && client->IsEd2kClient();

	CMenuXP ClientMenu;
	ClientMenu.CreatePopupMenu();
	ClientMenu.AddMenuSidebar(GetResString(_T("CLIENTS")));
	ClientMenu.AppendMenu(MF_STRING | (client ? MF_ENABLED : MF_GRAYED), MP_DETAIL, GetResString(_T("SHOWDETAILS")), _T("CLIENTDETAILS"));
	ClientMenu.SetDefaultItem(MP_DETAIL);
	ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && !client->IsFriend()) ? MF_ENABLED : MF_GRAYED), MP_ADDFRIEND, GetResString(_T("ADDFRIEND")), _T("ADDFRIEND"));
	ClientMenu.AppendMenu(MF_STRING | (is_ed2k ? MF_ENABLED : MF_GRAYED), MP_MESSAGE, GetResString(_T("SEND_MSG")), _T("SENDMESSAGE"));
	ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && client->GetViewSharedFilesSupport()) ? MF_ENABLED : MF_GRAYED), MP_SHOWLIST, GetResString(_T("VIEWFILES")), _T("VIEWFILES"));
	ClientMenu.AppendMenu(MF_STRING | (client ? MF_ENABLED : MF_GRAYED), MP_EDIT_NOTE, GetResString(_T("EDIT_CLIENT_NOTE")), _T("RENAME"));
	if (Kademlia::CKademlia::IsRunning() && !Kademlia::CKademlia::IsConnected())
		ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && client->GetKadPort() && client->GetKadVersion() >= KADEMLIA_VERSION2_47a) ? MF_ENABLED : MF_GRAYED), MP_BOOT, GetResString(_T("BOOTSTRAP")));

	ClientMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	CMenuXP m_PunishmentMenu;
	m_PunishmentMenu.CreateMenu();
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_IPUSERHASHBAN, GetResString(_T("IP_USER_HASH_BAN")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_USERHASHBAN, GetResString(_T("USER_HASH_BAN")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_UPLOADBAN, GetResString(_T("UPLOAD_BAN")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX01, GetResString(_T("SCORE_01")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX02, GetResString(_T("SCORE_02")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX03, GetResString(_T("SCORE_03")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX04, GetResString(_T("SCORE_04")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX05, GetResString(_T("SCORE_05")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX06, GetResString(_T("SCORE_06")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX07, GetResString(_T("SCORE_07")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX08, GetResString(_T("SCORE_08")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX09, GetResString(_T("SCORE_09")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_NONE, GetResString(_T("NO_PUNISHMENT")));
	ClientMenu.EnableMenuItem((UINT)m_PunishmentMenu.m_hMenu, MF_ENABLED);
	int m_PunishmentMenuItem = client ? MP_PUNISMENT_IPUSERHASHBAN + client->m_uPunishment : 0;
	m_PunishmentMenu.CheckMenuRadioItem(MP_PUNISMENT_IPUSERHASHBAN, MP_PUNISMENT_NONE, m_PunishmentMenuItem, 0);
	ClientMenu.AppendMenu(MF_STRING | MF_POPUP | (client ? MF_ENABLED : MF_GRAYED), (UINT_PTR)m_PunishmentMenu.m_hMenu, GetResString(_T("PUNISHMENT")), _T("PUNISHMENT"));
	ClientMenu.AppendMenu(MF_STRING | MF_SEPARATOR);

	ClientMenu.AppendMenu(MF_STRING | (GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED), MP_FIND, GetResString(_T("FIND")), _T("Search"));
	GetPopupMenuPos(*this, point);
	ClientMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
}

BOOL CDownloadClientsCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	wParam = LOWORD(wParam);

	if (wParam == MP_FIND) {
		OnFindStart();
		return TRUE;
	}

	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	if (iSel >= 0) {
		CUpDownClient *client = reinterpret_cast<CUpDownClient*>(GetItemData(iSel));
		switch (wParam) {
		case MP_SHOWLIST:
			{
				CUpDownClient* NewClient = theApp.emuledlg->transferwnd->GetClientList()->ArchivedToActive(client);
				if (NewClient && (client == NewClient || theApp.clientlist->IsValidClient(NewClient)))
					NewClient->RequestSharedFileList();
			}
			break;
		case MP_MESSAGE:
			{
				CUpDownClient* NewClient = theApp.emuledlg->transferwnd->GetClientList()->ArchivedToActive(client);
				if (NewClient && (client == NewClient || theApp.clientlist->IsValidClient(NewClient)))
					theApp.emuledlg->chatwnd->StartSession(NewClient);
			}
			break;
		case MP_ADDFRIEND:
			if (theApp.friendlist->AddFriend(client))
				Update(iSel);
			break;
		case MP_DETAIL:
		case MPG_ALTENTER:
		case IDA_ENTER:
			{
				CClientDetailDialog dialog(client, this);
				dialog.DoModal();
			}
			break;
		case MP_BOOT:
			if (client->GetKadPort() && client->GetKadVersion() >= KADEMLIA_VERSION2_47a)
				Kademlia::CKademlia::Bootstrap(client->GetIPv4().ToUInt32(true), client->GetKadPort());
			break;
		case MP_EDIT_NOTE:
			client->SetClientNote();
			break;
		case MP_PUNISMENT_IPUSERHASHBAN:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_IP_BAN")), PR_MANUAL, P_IPUSERHASHBAN);
			RefreshClient(client);
			break;
		case MP_PUNISMENT_USERHASHBAN:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_USER_HASH_BAN")), PR_MANUAL, P_USERHASHBAN);
			RefreshClient(client);
			break;
		case MP_PUNISMENT_UPLOADBAN:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_UPLOAD_BAN")), PR_MANUAL, P_UPLOADBAN);
			RefreshClient(client);
			break;
		case MP_PUNISMENT_SCOREX01:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX01);
			RefreshClient(client);
			break;
		case MP_PUNISMENT_SCOREX02:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX02);
			RefreshClient(client);
			break;
		case MP_PUNISMENT_SCOREX03:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX03);
			RefreshClient(client);
			break;
		case MP_PUNISMENT_SCOREX04:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX04);
			RefreshClient(client);
			break;
		case MP_PUNISMENT_SCOREX05:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX05);
			RefreshClient(client);
			break;
		case MP_PUNISMENT_SCOREX06:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX06);
			RefreshClient(client);
			break;
		case MP_PUNISMENT_SCOREX07:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX07);
			RefreshClient(client);
			break;
		case MP_PUNISMENT_SCOREX08:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX08);
			RefreshClient(client);
			break;
		case MP_PUNISMENT_SCOREX09:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX09);
			RefreshClient(client);
			break;
		case MP_PUNISMENT_NONE:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_CANCELATION")), PR_MANUAL, P_NOPUNISHMENT);
			RefreshClient(client);
			break;
		}
	}
	return TRUE;
}

void CDownloadClientsCtrl::AddClient(CUpDownClient* client)
{
	if (theApp.IsClosing() || client == NULL)
		return;

	ASSERT(m_ListItemsMap.find(client) == m_ListItemsMap.end()); // The same file shall be added only once
	m_ListItemsMap.emplace(client, client);

	if (!IsFilteredOut(client)) {
		InsertItem(LVIF_TEXT | LVIF_PARAM, GetItemCount(), LPSTR_TEXTCALLBACK, 0, 0, 0, (LPARAM)client);
		theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
	}
}

void CDownloadClientsCtrl::RemoveClient(CUpDownClient* client)
{
	if (theApp.IsClosing() || client == NULL)
		return;

	// Retrieve all entries matching the File or linked to the file
	auto it = m_ListItemsMap.find(client);
	if (it != m_ListItemsMap.end()) { // If client is on the map we can proceed
		// Remove it from the m_ListItems
		m_ListItemsMap.erase(it);
		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)client;
		int iItem = FindItem(&find);
		if (iItem >= 0) {
			DeleteItem(iItem);
			theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
		}
	}
}

void CDownloadClientsCtrl::RefreshClient(CUpDownClient* client)
{
	if (theApp.IsClosing() || !client || theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !theApp.emuledlg->transferwnd->GetDownloadClientsList()->IsWindowVisible())
		return;

	m_updatethread->AddItemToUpdate((LPARAM)client);
}

void CDownloadClientsCtrl::UpdateView()
{
	for (auto it = m_ListItemsMap.begin(); it != m_ListItemsMap.end(); ++it) {
		CUpDownClient* cur_item = it->second;
		if (cur_item) {
			if (!IsFilteredOut(cur_item))
				ShowClient(cur_item);
			else
				HideClient(cur_item);
		}
	}
	theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
}

void CDownloadClientsCtrl::HideClient(CUpDownClient* client)
{
	if (m_ListItemsMap.find(client) != m_ListItemsMap.end()) { // If client is on the map we can proceed
		// Find entry in CListCtrl and update object
		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)client;
		int iItem = FindItem(&find);
		if (iItem >= 0) {
			DeleteItem(iItem);
			return;
		}
	}
}

void CDownloadClientsCtrl::ShowClient(CUpDownClient* client)
{
	if (m_ListItemsMap.find(client) != m_ListItemsMap.end()) { // If client is on the map we can proceed
		// Check if entry is already in the List
		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)client;
		if (FindItem(&find) == -1)
			InsertItem(LVIF_PARAM | LVIF_TEXT, GetItemCount(), LPSTR_TEXTCALLBACK, 0, 0, 0, (LPARAM)client);
	}
}

bool CDownloadClientsCtrl::IsFilteredOut(CUpDownClient* client)
{
	const CStringArray& rastrFilter = theApp.emuledlg->transferwnd->m_pwndTransfer->m_astrFilterDownloadClients;
	if (!rastrFilter.IsEmpty()) {
		// filtering is done by text only for all columns to keep it consistent and simple
		// for the user even if that doesn't allow complex filters
		// for example for a file size range - but this could be done at server search time already
		const CString& szFilterTarget(GetItemDisplayText(client, theApp.emuledlg->transferwnd->m_pwndTransfer->GetFilterColumnDownloadClients()));

		for (INT_PTR i = rastrFilter.GetCount(); --i >= 0;) {
			LPCTSTR pszText = (LPCTSTR)rastrFilter[i];
			bool bAnd = (*pszText != _T('-'));
			if (!bAnd)
				++pszText;

			bool bFound = (stristr(szFilterTarget, pszText) != NULL);
			if (bAnd != bFound)
				return true;
		}
	}
	return false;
}

void CDownloadClientsCtrl::ShowSelectedUserDetails()
{
	CPoint point;
	if (!::GetCursorPos(&point))
		return;
	ScreenToClient(&point);
	int it = HitTest(point);
	if (it == -1)
		return;

	SetItemState(-1, 0, LVIS_SELECTED);
	SetItemState(it, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	SetSelectionMark(it);   // display selection mark correctly!

	CUpDownClient *client = reinterpret_cast<CUpDownClient*>(GetItemData(GetSelectionMark()));
	if (client) {
		CClientDetailDialog dialog(client, this);
		dialog.DoModal();
	}
}

void CDownloadClientsCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar == 'C' && GetKeyState(VK_CONTROL) < 0) {
		int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
		if (iSel >= 0) {
			CUpDownClient* client = reinterpret_cast<CUpDownClient*>(GetItemData(iSel));
			if (client) {
				theApp.CopyTextToClipboard(md4str(client->GetUserHash()));
				theApp.emuledlg->statusbar->SetText(GetResString(_T("USER_HASH_COPIED_TO_CLIPBOARD")), SBarLog, 0);
				return;
			}
		}
	}

	if (nChar == 'X' && GetKeyState(VK_CONTROL) < 0) {
		int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
		if (iSel >= 0) {
			CUpDownClient* client = reinterpret_cast<CUpDownClient*>(GetItemData(iSel));
			if (client) {
				CString m_strClientIpport;
				m_strClientIpport.Format(_T("%s:%u"), ipstr(client->GetConnectIP()), client->GetUserPort());
				if (!m_strClientIpport.IsEmpty()) {
					theApp.CopyTextToClipboard(m_strClientIpport);
					theApp.emuledlg->statusbar->SetText(GetResString(_T("USER_IP_PORT_COPIED_TO_CLIPBOARD")), SBarLog, 0);
				}
			}
		}
		return;
	}

	CMuleListCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CDownloadClientsCtrl::MaintainSortOrderAfterUpdate()
{
	if (GetSortItem() != -1) // Re-sort the list to maintain sort order after updates
		SortItems(SortProc, MAKELONG(GetSortItem(), !GetSortAscending()));
}
