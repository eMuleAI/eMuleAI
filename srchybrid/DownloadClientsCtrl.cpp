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
#include "Friend.h"
#include "ChatWnd.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "SharedFileList.h"
#include "MuleStatusBarCtrl.h"
#include "ClientList.h"
#include "eMuleAI/Shield.h"
#include "eMuleAI/DarkMode.h"
#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern UINT g_uMainThreadId;

namespace
{

	CObject* ClientDetailTokenFromRuntimeIDValue(const CDownloadClientsCtrl::DownloadClientItemID uRuntimeID)
	{
		if (uRuntimeID == 0)
			return NULL;

		return reinterpret_cast<CObject*>((static_cast<ULONG_PTR>(uRuntimeID) << 1) | 1);
	}
}

IMPLEMENT_DYNAMIC(CDownloadClientsCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CDownloadClientsCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetDispInfo)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
	ON_WM_CONTEXTMENU()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_KEYDOWN()
	ON_WM_DESTROY()
	ON_MESSAGE(WM_DOWNLOADCLIENTSCTRL_ADD_CLIENT, OnUiAddClient)
	ON_MESSAGE(WM_DOWNLOADCLIENTSCTRL_REMOVE_CLIENT, OnUiRemoveClient)
	ON_MESSAGE(WM_DOWNLOADCLIENTSCTRL_UPDATE_CLIENT_ROW, OnClientRowUpdate)
	ON_MESSAGE(WM_DOWNLOADCLIENTSCTRL_REMOVE_STALE_CLIENT, OnUiRemoveStaleClient)
END_MESSAGE_MAP()

CDownloadClientsCtrl::CDownloadClientsCtrl()
	: CListCtrlItemWalk(this)
	, m_lUiAddClientPending(0)
	, m_lUiRemoveClientPending(0)
	, m_lClientRowUpdatePending(0)
	, m_lUiRemoveStaleClientPending(0)
{
	SetGeneralPurposeFind(true);
	SetSkinKey(_T("DownloadingLv"));
}

CDownloadClientsCtrl::~CDownloadClientsCtrl()
{
	CSingleLock lock(&m_uiClientUpdateLock, TRUE);
	m_PendingAddRuntimeIDs.clear();
	m_PendingRemoveRuntimeIDs.clear();
	m_PendingRefreshRuntimeIDs.clear();
	m_PendingRemovalRuntimeIDs.clear();
	lock.Unlock();
	m_ListItemsMap.clear();
}

void CDownloadClientsCtrl::OnDestroy()
{
	MSG msg = {};
	while (::PeekMessage(&msg, m_hWnd, WM_DOWNLOADCLIENTSCTRL_ADD_CLIENT, WM_DOWNLOADCLIENTSCTRL_REMOVE_STALE_CLIENT, PM_REMOVE)) {
	}
	{
		CSingleLock lock(&m_uiClientUpdateLock, TRUE);
		m_PendingAddRuntimeIDs.clear();
		m_PendingRemoveRuntimeIDs.clear();
		m_PendingRefreshRuntimeIDs.clear();
		m_PendingRemovalRuntimeIDs.clear();
		InterlockedExchange(&m_lUiAddClientPending, 0);
		InterlockedExchange(&m_lUiRemoveClientPending, 0);
		InterlockedExchange(&m_lClientRowUpdatePending, 0);
		InterlockedExchange(&m_lUiRemoveStaleClientPending, 0);
	}

	CMuleListCtrl::OnDestroy();
}

CDownloadClientsCtrl::ClientReference::ClientReference()
	: m_pClient(NULL)
{
}

CDownloadClientsCtrl::ClientReference::~ClientReference()
{
	Release();
}

void CDownloadClientsCtrl::ClientReference::Attach(CUpDownClient* pClient)
{
	if (m_pClient == pClient)
		return;

	Release();
	m_pClient = pClient;
}

void CDownloadClientsCtrl::ClientReference::Release()
{
	if (m_pClient != NULL) {
		m_pClient->ReleaseRuntimeReference();
		m_pClient = NULL;
	}
}

void CDownloadClientsCtrl::Init()
{
	SetPrefsKey(_T("DownloadClientsCtrl"));
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	// Alignment rule: left for text, dates, and status labels; right for sizes, rates, counts, durations, and percentages.
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
	InsertColumn(12, EMPTY, LVCFMT_RIGHT, 80);
	InsertColumn(13, EMPTY, LVCFMT_RIGHT,	100);
	InsertColumn(14, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(15, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(16, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(17, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(18, EMPTY, LVCFMT_RIGHT,	100);
	InsertColumn(19, EMPTY, LVCFMT_RIGHT,	100);
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

CUpDownClient* CDownloadClientsCtrl::AcquireRuntimeClient(DownloadClientItemID uRuntimeID)
{
	return (uRuntimeID != 0 && theApp.clientlist != NULL) ? theApp.clientlist->AcquireTrackedClientByRuntimeID((ClientRuntimeID)uRuntimeID) : NULL;
}

CObject* CDownloadClientsCtrl::GetNextSelectableItem()
{
	const int iItemCount = GetItemCount();
	if (iItemCount < 2)
		return NULL;

	POSITION pos = GetFirstSelectedItemPosition();
	if (pos == NULL)
		return NULL;

	const int iSelectedItem = GetNextSelectedItem(pos);
	for (int iNewItem = iSelectedItem + 1; iNewItem < iItemCount; ++iNewItem) {
		const DownloadClientItemID uRuntimeID = (DownloadClientItemID)GetItemData(iNewItem);
		if (uRuntimeID == 0)
			continue;

		ClientReference clientRef;
		if (!ResolveTrackedClient(uRuntimeID, clientRef)) {
			QueueTrackedClientRemoval(uRuntimeID);
			continue;
		}
		if (!IsDisplayableClient(clientRef.Get())) {
			QueueTrackedClientRemoval(uRuntimeID);
			continue;
		}

		SetItemState(iSelectedItem, 0, LVIS_SELECTED | LVIS_FOCUSED);
		SetItemState(iNewItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		SetSelectionMark(iNewItem);
		EnsureVisible(iNewItem, FALSE);
		return ClientDetailTokenFromRuntimeIDValue(uRuntimeID);
	}

	return NULL;
}

CObject* CDownloadClientsCtrl::GetPrevSelectableItem()
{
	const int iItemCount = GetItemCount();
	if (iItemCount < 2)
		return NULL;

	POSITION pos = GetFirstSelectedItemPosition();
	if (pos == NULL)
		return NULL;

	const int iSelectedItem = GetNextSelectedItem(pos);
	for (int iNewItem = iSelectedItem - 1; iNewItem >= 0; --iNewItem) {
		const DownloadClientItemID uRuntimeID = (DownloadClientItemID)GetItemData(iNewItem);
		if (uRuntimeID == 0)
			continue;

		ClientReference clientRef;
		if (!ResolveTrackedClient(uRuntimeID, clientRef)) {
			QueueTrackedClientRemoval(uRuntimeID);
			continue;
		}
		if (!IsDisplayableClient(clientRef.Get())) {
			QueueTrackedClientRemoval(uRuntimeID);
			continue;
		}

		SetItemState(iSelectedItem, 0, LVIS_SELECTED | LVIS_FOCUSED);
		SetItemState(iNewItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		SetSelectionMark(iNewItem);
		EnsureVisible(iNewItem, FALSE);
		return ClientDetailTokenFromRuntimeIDValue(uRuntimeID);
	}

	return NULL;
}

bool CDownloadClientsCtrl::ResolveArchivedClientForActiveClient(CUpDownClient* client, ClientReference& clientRef) const
{
	clientRef.Release();
	if (client == NULL || client->m_bIsArchived || !thePrefs.GetClientHistory())
		return false;

	const DownloadClientItemID uArchivedRuntimeID = (DownloadClientItemID)client->GetArchivedClientRuntimeID();
	if (uArchivedRuntimeID == 0)
		return false;

	clientRef.Attach(AcquireRuntimeClient(uArchivedRuntimeID));
	return clientRef.Get() != NULL && clientRef.Get() != client && clientRef.Get()->m_bIsArchived;
}

bool CDownloadClientsCtrl::TryReplaceArchivedClient(CUpDownClient* client)
{
	ClientReference archivedClientRef;
	if (!ResolveArchivedClientForActiveClient(client, archivedClientRef))
		return false;

	CUpDownClient* pArchivedClient = archivedClientRef.Get();
	const DownloadClientItemID uArchivedRuntimeID = (DownloadClientItemID)pArchivedClient->GetRuntimeID();
	if (uArchivedRuntimeID == 0 || uArchivedRuntimeID == (DownloadClientItemID)client->GetRuntimeID())
		return false;

	if (m_ListItemsMap.find(uArchivedRuntimeID) == m_ListItemsMap.end() && (!::IsWindow(m_hWnd) || FindItemIndexByRuntimeID(uArchivedRuntimeID) < 0))
		return false;

	return ReplaceTrackedClient(uArchivedRuntimeID, client);
}

void CDownloadClientsCtrl::QueueTrackedClientRemoval(DownloadClientItemID uRuntimeID)
{
	QueueUiRemoveStaleClient(uRuntimeID);
}

void CDownloadClientsCtrl::QueueUiAddClient(DownloadClientItemID uRuntimeID)
{
	if (uRuntimeID == 0 || !::IsWindow(m_hWnd) || theApp.IsClosing())
		return;

	bool bPostMessage = false;
	{
		CSingleLock lock(&m_uiClientUpdateLock, TRUE);
		m_PendingAddRuntimeIDs.insert(uRuntimeID);
		bPostMessage = InterlockedCompareExchange(&m_lUiAddClientPending, 1, 0) == 0;
	}
	if (bPostMessage && !PostMessage(WM_DOWNLOADCLIENTSCTRL_ADD_CLIENT, 0, 0))
		InterlockedExchange(&m_lUiAddClientPending, 0);
}

void CDownloadClientsCtrl::QueueUiRemoveClient(DownloadClientItemID uRuntimeID, DownloadClientItemID uArchivedRuntimeID)
{
	if (uRuntimeID == 0 || !::IsWindow(m_hWnd) || theApp.IsClosing())
		return;

	bool bPostMessage = false;
	{
		CSingleLock lock(&m_uiClientUpdateLock, TRUE);
		m_PendingRemoveRuntimeIDs[uRuntimeID] = uArchivedRuntimeID;
		bPostMessage = InterlockedCompareExchange(&m_lUiRemoveClientPending, 1, 0) == 0;
	}
	if (bPostMessage && !PostMessage(WM_DOWNLOADCLIENTSCTRL_REMOVE_CLIENT, 0, 0))
		InterlockedExchange(&m_lUiRemoveClientPending, 0);
}

void CDownloadClientsCtrl::QueueClientRowUpdate(DownloadClientItemID uRuntimeID)
{
	if (uRuntimeID == 0 || !::IsWindow(m_hWnd) || theApp.IsClosing())
		return;

	bool bPostMessage = false;
	{
		CSingleLock lock(&m_uiClientUpdateLock, TRUE);
		m_PendingRefreshRuntimeIDs.insert(uRuntimeID);
		bPostMessage = InterlockedCompareExchange(&m_lClientRowUpdatePending, 1, 0) == 0;
	}
	if (bPostMessage && !PostMessage(WM_DOWNLOADCLIENTSCTRL_UPDATE_CLIENT_ROW, 0, 0))
		InterlockedExchange(&m_lClientRowUpdatePending, 0);
}

void CDownloadClientsCtrl::QueueUiRemoveStaleClient(DownloadClientItemID uRuntimeID)
{
	if (uRuntimeID == 0 || !::IsWindow(m_hWnd) || theApp.IsClosing())
		return;

	bool bPostMessage = false;
	{
		CSingleLock lock(&m_uiClientUpdateLock, TRUE);
		m_PendingRemovalRuntimeIDs.insert(uRuntimeID);
		bPostMessage = InterlockedCompareExchange(&m_lUiRemoveStaleClientPending, 1, 0) == 0;
	}
	if (bPostMessage && !PostMessage(WM_DOWNLOADCLIENTSCTRL_REMOVE_STALE_CLIENT, 0, 0))
		InterlockedExchange(&m_lUiRemoveStaleClientPending, 0);
}

bool CDownloadClientsCtrl::ResolveTrackedClient(DownloadClientItemID uRuntimeID, ClientReference& clientRef)
{
	clientRef.Attach(AcquireRuntimeClient(uRuntimeID));
	return clientRef.Get() != NULL;
}

bool CDownloadClientsCtrl::IsDisplayableClient(const CUpDownClient* client) const
{
	return client != NULL && !client->m_bIsArchived && client->GetRequestFile() != NULL;
}

bool CDownloadClientsCtrl::GetClientFromItem(int iItem, ClientReference& clientRef, DownloadClientItemID* puRuntimeID)
{
	DownloadClientItemID uRuntimeID = 0;
	if (iItem >= 0)
		uRuntimeID = (DownloadClientItemID)GetItemData(iItem);
	if (puRuntimeID != NULL)
		*puRuntimeID = uRuntimeID;
	if (uRuntimeID == 0 || !ResolveTrackedClient(uRuntimeID, clientRef)) {
		if (uRuntimeID != 0)
			RemoveTrackedClientByRuntimeID(uRuntimeID);
		return false;
	}
	if (!IsDisplayableClient(clientRef.Get())) {
		RemoveTrackedClientByRuntimeID(uRuntimeID);
		return false;
	}
	return true;
}

bool CDownloadClientsCtrl::GetSelectedClient(ClientReference& clientRef, DownloadClientItemID* puRuntimeID, int* piItem)
{
	const int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	if (piItem != NULL)
		*piItem = iSel;
	return GetClientFromItem(iSel, clientRef, puRuntimeID);
}

int CDownloadClientsCtrl::FindItemIndexByRuntimeID(DownloadClientItemID uRuntimeID) const
{
	LVFINDINFO find = {};
	find.flags = LVFI_PARAM;
	find.lParam = (LPARAM)uRuntimeID;
	return const_cast<CDownloadClientsCtrl*>(this)->FindItem(&find);
}

int CDownloadClientsCtrl::FindSortedInsertIndex(DownloadClientItemID uRuntimeID)
{
	const int iItemCount = GetItemCount();
	if (GetSortItem() == -1 || iItemCount <= 0)
		return iItemCount;

	const LPARAM lParamSort = MAKELONG(GetSortItem(), !GetSortAscending());
	for (int iItem = 0; iItem < iItemCount; ++iItem) {
		if (SortProc((LPARAM)uRuntimeID, (LPARAM)GetItemData(iItem), lParamSort) < 0)
			return iItem;
	}
	return iItemCount;
}

int CDownloadClientsCtrl::PurgeVisibleRows(DownloadClientItemID uRuntimeID, int iKeepItem)
{
	if (!::IsWindow(m_hWnd))
		return 0;

	int iRemoved = 0;
	for (int iItem = GetItemCount() - 1; iItem >= 0; --iItem) {
		if (iItem == iKeepItem)
			continue;
		if ((DownloadClientItemID)GetItemData(iItem) != uRuntimeID)
			continue;
		DeleteItem(iItem);
		++iRemoved;
	}
	return iRemoved;
}

void CDownloadClientsCtrl::RemoveTrackedClientByRuntimeID(DownloadClientItemID uRuntimeID, bool bUpdateCount)
{
	if (uRuntimeID == 0)
		return;

	m_PendingRemovalRuntimeIDs.erase(uRuntimeID);
	m_ListItemsMap.erase(uRuntimeID);

	const int iRemoved = PurgeVisibleRows(uRuntimeID);
	if (bUpdateCount && iRemoved > 0)
		theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
}

bool CDownloadClientsCtrl::ReplaceTrackedClient(DownloadClientItemID uOldRuntimeID, CUpDownClient* pNewClient)
{
	if (uOldRuntimeID == 0 || pNewClient == NULL)
		return false;

	const DownloadClientItemID uNewRuntimeID = (DownloadClientItemID)pNewClient->GetRuntimeID();
	if (uNewRuntimeID == 0)
		return false;

	auto itOld = m_ListItemsMap.find(uOldRuntimeID);
	const bool bHadTrackedOldRuntimeID = itOld != m_ListItemsMap.end();
	const bool bHadVisibleOldRuntimeID = ::IsWindow(m_hWnd) && FindItemIndexByRuntimeID(uOldRuntimeID) >= 0;
	if (!bHadTrackedOldRuntimeID && !bHadVisibleOldRuntimeID)
		return false;

	bool bCountChanged = false;
	int iKeepNewItem = -1;

	if (bHadTrackedOldRuntimeID && uOldRuntimeID != uNewRuntimeID) {
		m_ListItemsMap.erase(itOld);
	}
	m_ListItemsMap.insert(uNewRuntimeID);

	if (::IsWindow(m_hWnd)) {
		const bool bFilteredOut = IsFilteredOut(pNewClient);
		int iOldItem = FindItemIndexByRuntimeID(uOldRuntimeID);
		int iNewItem = FindItemIndexByRuntimeID(uNewRuntimeID);
		bool bSortAfterVisibleChange = false;

		if (iNewItem == -1 && iOldItem >= 0 && !bFilteredOut) {
			SetItemData(iOldItem, (DWORD_PTR)uNewRuntimeID);
			iNewItem = iOldItem;
			bSortAfterVisibleChange = true;
			Update(iOldItem);
		} else if (iNewItem == -1 && !bFilteredOut) {
			if (InsertItem(LVIF_TEXT | LVIF_PARAM, FindSortedInsertIndex(uNewRuntimeID), LPSTR_TEXTCALLBACK, 0, 0, 0, (LPARAM)uNewRuntimeID) >= 0) {
				bCountChanged = true;
				iNewItem = FindItemIndexByRuntimeID(uNewRuntimeID);
			}
		}

		if (iOldItem >= 0 && iOldItem != iNewItem) {
			DeleteItem(iOldItem);
			bCountChanged = true;
			if (iNewItem > iOldItem)
				--iNewItem;
		}

		if (bFilteredOut && iNewItem >= 0) {
			DeleteItem(iNewItem);
			bCountChanged = true;
			iNewItem = -1;
		}

		iKeepNewItem = iNewItem;
		const int iRemovedOld = PurgeVisibleRows(uOldRuntimeID);
		const int iRemovedNew = PurgeVisibleRows(uNewRuntimeID, iKeepNewItem);
		if (iRemovedOld > 0 || iRemovedNew > 0)
			bCountChanged = true;
		if (bSortAfterVisibleChange) {
			iKeepNewItem = FindItemIndexByRuntimeID(uNewRuntimeID);
			if (iKeepNewItem >= 0) {
				MaintainSortOrderAfterUpdate();
				iKeepNewItem = FindItemIndexByRuntimeID(uNewRuntimeID);
				if (iKeepNewItem >= 0 && IsItemIndexVisible(iKeepNewItem))
					RequestRowRedrawAsync(iKeepNewItem, iKeepNewItem);
			}
		}
	}

	if (bCountChanged)
		theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
	return true;
}

void CDownloadClientsCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	if (!lpDrawItemStruct->itemData || theApp.IsClosing())
		return;

	CRect rcItem(lpDrawItemStruct->rcItem);
	CRect rcClientFullRow;
	GetClientRect(&rcClientFullRow);
	CRect rcPaint(rcClientFullRow.left, rcItem.top, rcClientFullRow.right, rcItem.bottom);
	CMemoryDC dc(CDC::FromHandle(lpDrawItemStruct->hDC), rcPaint);
	BOOL bCtrlFocused;
	InitItemMemDC(dc, lpDrawItemStruct, bCtrlFocused);

	// Set selected item background color
	if ((lpDrawItemStruct->itemState & ODS_SELECTED) != 0)
		dc.FillSolidRect(rcPaint, GetCustomSysColor(COLOR_HIGHLIGHT));

	RECT rcClient;
	GetClientRect(&rcClient);
	const DownloadClientItemID uRuntimeID = (DownloadClientItemID)lpDrawItemStruct->itemData;
	if (m_ListItemsMap.find(uRuntimeID) == m_ListItemsMap.end())
		return;
	ClientReference clientRef;
	if (!ResolveTrackedClient(uRuntimeID, clientRef)) {
		QueueTrackedClientRemoval(uRuntimeID);
		return;
	}
	CUpDownClient* client = clientRef.Get();
	if (!IsDisplayableClient(client)) {
		QueueTrackedClientRemoval(uRuntimeID);
		return;
	}

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
			const CString sItem(GetItemDisplayText(client, iColumn));
			switch (iColumn) {
			case 0: //user name
			{
					int iImage = 0;
					UINT uOverlayImage = 0;
					client->GetDisplayImage(iImage, uOverlayImage);

					rcItem.left += sm_iIconOffset;
					const POINT point = { rcItem.left, rcItem.top + iIconY };
					SafeImageListDraw(m_pImageList, dc, iImage, point, ILD_NORMAL | INDEXTOOVERLAYMASK(uOverlayImage));
				    if (theApp.ipgeolocation->ShowCountryFlag() && IsColumnHidden(10)) {
				    	rcItem.left += 20;
						POINT point2 = { rcItem.left,rcItem.top + 1 };
						IMAGELISTDRAWPARAMS flagDrawParams = theApp.ipgeolocation->GetFlagImageDrawParams(dc, client->GetCountryFlagIndex(), point2);

						theApp.ipgeolocation->GetFlagImageList()->DrawIndirect(&flagDrawParams);
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
				if (client->GetRequestFile() != NULL) {
					CRect rcStatus(rcItem);
					++rcStatus.top;
					--rcStatus.bottom;
					if (rcStatus.Width() > 0 && rcStatus.Height() > 0) {
						const bool bUseFlatBar = thePrefs.UseFlatBar();
						const int iSavedDC = bUseFlatBar ? dc->SaveDC() : 0;
						client->DrawStatusBar(dc, rcStatus, false, bUseFlatBar);
						if (iSavedDC != 0)
							dc->RestoreDC(iSavedDC);
					}
				}
				break;
			case 10:
			{
				if (theApp.ipgeolocation->ShowCountryFlag()) {
					POINT point2 = { rcItem.left,rcItem.top + 1 };
					IMAGELISTDRAWPARAMS flagDrawParams = theApp.ipgeolocation->GetFlagImageDrawParams(dc, client->GetCountryFlagIndex(), point2);
					theApp.ipgeolocation->GetFlagImageList()->DrawIndirect(&flagDrawParams);
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

}

CString CDownloadClientsCtrl::GetItemDisplayText( CUpDownClient* client, int iSubItem) const
{
	CString sText;
	if (client == NULL)
		return sText;
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
		if (client->GetRequestFile() != NULL)
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
		const LVITEMW &rItem = reinterpret_cast<NMLVDISPINFO*>(pNMHDR)->item;
		if ((rItem.mask & LVIF_TEXT) && rItem.pszText && rItem.cchTextMax > 0) {
			DownloadClientItemID uRuntimeID = 0;
			if (rItem.iItem >= 0)
				uRuntimeID = (DownloadClientItemID)GetItemData(rItem.iItem);
			ClientReference clientRef;
			if (uRuntimeID != 0)
				ResolveTrackedClient(uRuntimeID, clientRef);
			CUpDownClient* pClient = clientRef.Get();
			if (pClient != NULL && IsDisplayableClient(pClient)) {
				_tcsncpy_s(rItem.pszText, rItem.cchTextMax, GetItemDisplayText(pClient, rItem.iSubItem), _TRUNCATE);
			} else {
				if (uRuntimeID != 0)
					QueueTrackedClientRemoval(uRuntimeID);
				rItem.pszText[0] = _T('\0');
			}
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
	ClientReference item1Ref;
	ClientReference item2Ref;
	item1Ref.Attach(AcquireRuntimeClient((DownloadClientItemID)lParam1));
	item2Ref.Attach(AcquireRuntimeClient((DownloadClientItemID)lParam2));
	CUpDownClient* item1 = item1Ref.Get();
	CUpDownClient* item2 = item2Ref.Get();
	CDownloadClientsCtrl* pCtrl = (!theApp.IsClosing() && theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL)
		? theApp.emuledlg->transferwnd->GetDownloadClientsList()
		: NULL;
	LPARAM iColumn = (lParamSort >= 100) ? lParamSort - 100 : lParamSort;
	if (item1 == NULL && pCtrl != NULL)
		pCtrl->QueueTrackedClientRemoval((DownloadClientItemID)lParam1);
	if (item2 == NULL && pCtrl != NULL)
		pCtrl->QueueTrackedClientRemoval((DownloadClientItemID)lParam2);
	if (item1 == NULL && item2 == NULL)
		return 0;
	if (item1 == NULL)
		return 1;
	if (item2 == NULL)
		return -1;
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
	ClientReference clientRef;
	if (GetSelectedClient(clientRef)) {
		CUpDownClient* client = clientRef.Get();
		CClientDetailDialog dialog(client, this);
		clientRef.Release();
		dialog.DoModal();
	}
	*pResult = 0;
}

void CDownloadClientsCtrl::OnContextMenu(CWnd*, CPoint point)
{
	ClientReference selectedClientRef;
	GetSelectedClient(selectedClientRef);
	const CUpDownClient *client = selectedClientRef.Get();
	const bool is_ed2k = client && client->IsEd2kClient();
	const CFriend *pFriend = client != NULL ? client->GetFriend() : NULL;

	CMenuXP ClientMenu;
	ClientMenu.CreatePopupMenu();
	ClientMenu.AddMenuSidebar(GetResString(_T("CLIENTS")));
	ClientMenu.AppendMenu(MF_STRING | (client ? MF_ENABLED : MF_GRAYED), MP_DETAIL, GetResString(_T("SHOWDETAILS")), _T("CLIENTDETAILS"));
	ClientMenu.SetDefaultItem(MP_DETAIL);
	ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && !client->IsFriend()) ? MF_ENABLED : MF_GRAYED), MP_ADDFRIEND, GetResString(_T("ADDFRIEND")), _T("ADDFRIEND"));
	ClientMenu.AppendMenu(MF_STRING | (pFriend != NULL ? MF_ENABLED : MF_GRAYED), MP_FRIENDSLOT, GetResString(_T("FRIENDSLOT")), _T("FRIENDSLOT"));
	ClientMenu.CheckMenuItem(MP_FRIENDSLOT, (pFriend != NULL && pFriend->GetFriendSlot()) ? MF_CHECKED : MF_UNCHECKED);
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

	int iSel = -1;
	ClientReference clientRef;
	if (GetSelectedClient(clientRef, NULL, &iSel)) {
		CUpDownClient* client = clientRef.Get();
		auto RefreshQueueCountAfterManualPunishment = []() {
			if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL)
				theApp.emuledlg->transferwnd->InvalidateQueueCount(true);
		};
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
		case MP_FRIENDSLOT:
			{
				CFriend *pFriend = client->GetFriend();
				if (pFriend != NULL) {
					pFriend->SetFriendSlot(!pFriend->GetFriendSlot());
					theApp.friendlist->SaveList();
					Update(iSel);
				}
			}
			break;
			case MP_DETAIL:
			case MPG_ALTENTER:
			case IDA_ENTER:
				{
					CClientDetailDialog dialog(client, this);
					clientRef.Release();
					dialog.DoModal();
				}
				break;
		case MP_BOOT:
			if (theApp.emuledlg != NULL && !theApp.emuledlg->CanUseP2PConnectionCommands()) {
				theApp.emuledlg->LogP2PConnectionCommandBlocked(true);
				break;
			}
			if (client->GetKadPort() && client->GetKadVersion() >= KADEMLIA_VERSION2_47a)
				Kademlia::CKademlia::Bootstrap(client->GetIPv4().ToUInt32(true), client->GetKadPort());
			break;
		case MP_EDIT_NOTE:
			client->SetClientNote();
			break;
		case MP_PUNISMENT_IPUSERHASHBAN:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_IP_BAN")), PR_MANUAL, P_IPUSERHASHBAN);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_USERHASHBAN:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_USER_HASH_BAN")), PR_MANUAL, P_USERHASHBAN);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_UPLOADBAN:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_UPLOAD_BAN")), PR_MANUAL, P_UPLOADBAN);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_SCOREX01:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX01);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_SCOREX02:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX02);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_SCOREX03:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX03);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_SCOREX04:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX04);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_SCOREX05:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX05);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_SCOREX06:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX06);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_SCOREX07:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX07);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_SCOREX08:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX08);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_SCOREX09:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX09);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_NONE:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_CANCELATION")), PR_MANUAL, P_NOPUNISHMENT);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		}
	}
	return TRUE;
}

void CDownloadClientsCtrl::AddClient(CUpDownClient* client)
{
	if (theApp.IsClosing() || client == NULL)
		return;

	if (GetCurrentThreadId() != g_uMainThreadId) {
		QueueUiAddClient((DownloadClientItemID)client->GetRuntimeID());
		return;
	}

	AddClientInternal(client);
}

void CDownloadClientsCtrl::AddClientInternal(CUpDownClient* client)
{
	if (theApp.IsClosing() || client == NULL)
		return;

	ASSERT(GetCurrentThreadId() == g_uMainThreadId);
	const DownloadClientItemID uRuntimeID = (DownloadClientItemID)client->GetRuntimeID();
	if (!IsDisplayableClient(client)) {
		RemoveTrackedClientByRuntimeID(uRuntimeID);
		const DownloadClientItemID uArchivedRuntimeID = (DownloadClientItemID)client->GetArchivedClientRuntimeID();
		if (uArchivedRuntimeID != 0 && uArchivedRuntimeID != uRuntimeID)
			RemoveTrackedClientByRuntimeID(uArchivedRuntimeID);
		return;
	}

	auto itTracked = m_ListItemsMap.find(uRuntimeID);
	if (itTracked != m_ListItemsMap.end()) {
		if (!::IsWindow(m_hWnd))
			return;

		if (IsFilteredOut(client))
			HideClient(client);
		else
			ShowClient(client);
		return;
	}

	m_ListItemsMap.insert(uRuntimeID);

	if (!::IsWindow(m_hWnd))
		return;

	if (!IsFilteredOut(client)) {
		PurgeVisibleRows(uRuntimeID);
		const int iItem = InsertItem(LVIF_TEXT | LVIF_PARAM, FindSortedInsertIndex(uRuntimeID), LPSTR_TEXTCALLBACK, 0, 0, 0, (LPARAM)uRuntimeID);
		if (iItem >= 0) {
			if (IsItemIndexVisible(iItem))
				RequestRowRedrawAsync(iItem, iItem);
			theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
		}
	}
}

LRESULT CDownloadClientsCtrl::OnUiAddClient(WPARAM wParam, LPARAM /*lParam*/)
{
	std::vector<DownloadClientItemID> aRuntimeIDs;
	{
		CSingleLock lock(&m_uiClientUpdateLock, TRUE);
		const DownloadClientItemID uRuntimeID = (DownloadClientItemID)wParam;
		if (uRuntimeID != 0)
			m_PendingAddRuntimeIDs.insert(uRuntimeID);
		aRuntimeIDs.assign(m_PendingAddRuntimeIDs.begin(), m_PendingAddRuntimeIDs.end());
		m_PendingAddRuntimeIDs.clear();
		InterlockedExchange(&m_lUiAddClientPending, 0);
	}
	for (size_t i = 0; i < aRuntimeIDs.size(); ++i) {
		ClientReference clientRef;
		if (ResolveTrackedClient(aRuntimeIDs[i], clientRef))
			AddClientInternal(clientRef.Get());
	}
	return 0;
}

void CDownloadClientsCtrl::RemoveClient(CUpDownClient* client)
{
	if (theApp.IsClosing() || client == NULL)
		return;

	if (GetCurrentThreadId() != g_uMainThreadId) {
		QueueUiRemoveClient((DownloadClientItemID)client->GetRuntimeID(), (DownloadClientItemID)client->GetArchivedClientRuntimeID());
		return;
	}

	RemoveClientInternal(client);
}

void CDownloadClientsCtrl::RemoveClientInternal(CUpDownClient* client)
{
	if (theApp.IsClosing() || client == NULL)
		return;

	ASSERT(GetCurrentThreadId() == g_uMainThreadId);
	const DownloadClientItemID uRuntimeID = (DownloadClientItemID)client->GetRuntimeID();
	RemoveTrackedClientByRuntimeID(uRuntimeID);

	const DownloadClientItemID uArchivedRuntimeID = (DownloadClientItemID)client->GetArchivedClientRuntimeID();
	if (uArchivedRuntimeID != 0 && uArchivedRuntimeID != uRuntimeID)
		RemoveTrackedClientByRuntimeID(uArchivedRuntimeID);
}

LRESULT CDownloadClientsCtrl::OnUiRemoveClient(WPARAM wParam, LPARAM lParam)
{
	std::vector<std::pair<DownloadClientItemID, DownloadClientItemID> > aRuntimeIDs;
	{
		CSingleLock lock(&m_uiClientUpdateLock, TRUE);
		const DownloadClientItemID uRuntimeID = (DownloadClientItemID)wParam;
		if (uRuntimeID != 0)
			m_PendingRemoveRuntimeIDs[uRuntimeID] = (DownloadClientItemID)lParam;
		for (std::map<DownloadClientItemID, DownloadClientItemID>::const_iterator it = m_PendingRemoveRuntimeIDs.begin(); it != m_PendingRemoveRuntimeIDs.end(); ++it)
			aRuntimeIDs.push_back(*it);
		m_PendingRemoveRuntimeIDs.clear();
		InterlockedExchange(&m_lUiRemoveClientPending, 0);
	}
	for (size_t i = 0; i < aRuntimeIDs.size(); ++i) {
		const DownloadClientItemID uRuntimeID = aRuntimeIDs[i].first;
		const DownloadClientItemID uArchivedRuntimeID = aRuntimeIDs[i].second;
		RemoveTrackedClientByRuntimeID(uRuntimeID);
		if (uArchivedRuntimeID != 0 && uArchivedRuntimeID != uRuntimeID)
			RemoveTrackedClientByRuntimeID(uArchivedRuntimeID);
	}
	return 0;
}

void CDownloadClientsCtrl::RefreshClientByRuntimeID(DownloadClientItemID uRuntimeID)
{
	if (theApp.IsClosing() || uRuntimeID == 0)
		return;

	ClientReference clientRef;
	clientRef.Attach(AcquireRuntimeClient(uRuntimeID));
	CUpDownClient* client = clientRef.Get();
	if (client == NULL) {
		if (m_ListItemsMap.find(uRuntimeID) != m_ListItemsMap.end() || (::IsWindow(m_hWnd) && FindItemIndexByRuntimeID(uRuntimeID) >= 0))
			QueueTrackedClientRemoval(uRuntimeID);
		return;
	}
	if (!IsDisplayableClient(client)) {
		RemoveTrackedClientByRuntimeID(uRuntimeID);
		const DownloadClientItemID uArchivedRuntimeID = (DownloadClientItemID)client->GetArchivedClientRuntimeID();
		if (uArchivedRuntimeID != 0 && uArchivedRuntimeID != uRuntimeID)
			RemoveTrackedClientByRuntimeID(uArchivedRuntimeID);
		return;
	}

	const DownloadClientItemID uArchivedRuntimeID = (DownloadClientItemID)client->GetArchivedClientRuntimeID();
	if (uArchivedRuntimeID != 0 && uArchivedRuntimeID != uRuntimeID)
		RemoveTrackedClientByRuntimeID(uArchivedRuntimeID);

	if (theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !theApp.emuledlg->transferwnd->GetDownloadClientsList()->IsWindowVisible())
		return;

	if (m_ListItemsMap.find(uRuntimeID) != m_ListItemsMap.end()) {
		int iItem = FindItemIndexByRuntimeID(uRuntimeID);
		if (iItem < 0)
			return;

		MaintainSortOrderAfterThrottledUpdate();
		iItem = FindItemIndexByRuntimeID(uRuntimeID);
		if (IsItemIndexVisible(iItem))
			RequestRowRedrawAsync(iItem, iItem);
	}
}

void CDownloadClientsCtrl::RefreshClient(CUpDownClient* client)
{
	if (theApp.IsClosing() || client == NULL)
		return;

	const DownloadClientItemID uRuntimeID = (DownloadClientItemID)client->GetRuntimeID();
	if (uRuntimeID == 0)
		return;

	if (GetCurrentThreadId() != g_uMainThreadId) {
		QueueClientRowUpdate(uRuntimeID);
		return;
	}

	RefreshClientByRuntimeID(uRuntimeID);
}

void CDownloadClientsCtrl::UpdateView()
{
	std::vector<DownloadClientItemID> aStaleRuntimeIDs;
	for (auto it = m_ListItemsMap.begin(); it != m_ListItemsMap.end(); ++it) {
		const DownloadClientItemID uRuntimeID = *it;
		ClientReference clientRef;
		if (!ResolveTrackedClient(uRuntimeID, clientRef)) {
			aStaleRuntimeIDs.push_back(uRuntimeID);
			continue;
		}

		CUpDownClient* pClient = clientRef.Get();
		if (!IsDisplayableClient(pClient)) {
			aStaleRuntimeIDs.push_back(uRuntimeID);
			continue;
		}

		const DownloadClientItemID uArchivedRuntimeID = (DownloadClientItemID)pClient->GetArchivedClientRuntimeID();
		if (uArchivedRuntimeID != 0 && uArchivedRuntimeID != uRuntimeID)
			aStaleRuntimeIDs.push_back(uArchivedRuntimeID);

		if (!IsFilteredOut(pClient))
			ShowClient(pClient);
		else
			HideClient(pClient);
	}

	for (size_t i = 0; i < aStaleRuntimeIDs.size(); ++i)
		RemoveTrackedClientByRuntimeID(aStaleRuntimeIDs[i], false);

	theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
}

LRESULT CDownloadClientsCtrl::OnClientRowUpdate(WPARAM wParam, LPARAM lParam)
{
	std::vector<DownloadClientItemID> aRuntimeIDs;
	{
		CSingleLock lock(&m_uiClientUpdateLock, TRUE);
		const DownloadClientItemID uRuntimeID = (DownloadClientItemID)(lParam != 0 ? lParam : wParam);
		if (uRuntimeID != 0)
			m_PendingRefreshRuntimeIDs.insert(uRuntimeID);
		aRuntimeIDs.assign(m_PendingRefreshRuntimeIDs.begin(), m_PendingRefreshRuntimeIDs.end());
		m_PendingRefreshRuntimeIDs.clear();
		InterlockedExchange(&m_lClientRowUpdatePending, 0);
	}
	for (size_t i = 0; i < aRuntimeIDs.size(); ++i)
		RefreshClientByRuntimeID(aRuntimeIDs[i]);
	return 0;
}

LRESULT CDownloadClientsCtrl::OnUiRemoveStaleClient(WPARAM wParam, LPARAM lParam)
{
	std::vector<DownloadClientItemID> aRuntimeIDs;
	{
		CSingleLock lock(&m_uiClientUpdateLock, TRUE);
		const DownloadClientItemID uRuntimeID = (DownloadClientItemID)(lParam != 0 ? lParam : wParam);
		if (uRuntimeID != 0)
			m_PendingRemovalRuntimeIDs.insert(uRuntimeID);
		aRuntimeIDs.assign(m_PendingRemovalRuntimeIDs.begin(), m_PendingRemovalRuntimeIDs.end());
		m_PendingRemovalRuntimeIDs.clear();
		InterlockedExchange(&m_lUiRemoveStaleClientPending, 0);
	}
	for (size_t i = 0; i < aRuntimeIDs.size(); ++i)
		RemoveTrackedClientByRuntimeID(aRuntimeIDs[i]);
	return 0;
}

void CDownloadClientsCtrl::HideClient(CUpDownClient* client)
{
	if (!::IsWindow(m_hWnd))
		return;

	if (m_ListItemsMap.find((DownloadClientItemID)client->GetRuntimeID()) == m_ListItemsMap.end())
		return;

	if (PurgeVisibleRows((DownloadClientItemID)client->GetRuntimeID()) > 0)
		theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
}

void CDownloadClientsCtrl::ShowClient(CUpDownClient* client)
{
	if (!::IsWindow(m_hWnd))
		return;

	const DownloadClientItemID uRuntimeID = (DownloadClientItemID)client->GetRuntimeID();
	auto itTracked = m_ListItemsMap.find(uRuntimeID);
	if (itTracked == m_ListItemsMap.end())
		return;

	ClientReference clientRef;
	if (!ResolveTrackedClient(uRuntimeID, clientRef)) {
		RemoveTrackedClientByRuntimeID(uRuntimeID);
		return;
	}
	CUpDownClient* pClient = clientRef.Get();
	if (!IsDisplayableClient(pClient)) {
		RemoveTrackedClientByRuntimeID(uRuntimeID);
		return;
	}

	const int iFound = FindItemIndexByRuntimeID(uRuntimeID);
	const int iRemoved = PurgeVisibleRows(uRuntimeID, iFound);
	bool bCountChanged = iRemoved > 0;
	if (iFound == -1 && !IsFilteredOut(pClient)) {
		const int iItem = InsertItem(LVIF_PARAM | LVIF_TEXT, FindSortedInsertIndex(uRuntimeID), LPSTR_TEXTCALLBACK, 0, 0, 0, (LPARAM)uRuntimeID);
		if (iItem >= 0) {
			if (IsItemIndexVisible(iItem))
				RequestRowRedrawAsync(iItem, iItem);
			bCountChanged = true;
		}
	}

	if (bCountChanged)
		theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
}

bool CDownloadClientsCtrl::IsFilteredOut(CUpDownClient* client)
{
	if (!IsDisplayableClient(client))
		return true;

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

	ClientReference clientRef;
	if (GetClientFromItem(GetSelectionMark(), clientRef)) {
		CUpDownClient* client = clientRef.Get();
		CClientDetailDialog dialog(client, this);
		clientRef.Release();
		dialog.DoModal();
	}
}

void CDownloadClientsCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar == 'C' && GetKeyState(VK_CONTROL) < 0) {
		ClientReference clientRef;
		if (GetSelectedClient(clientRef)) {
			CUpDownClient* client = clientRef.Get();
			theApp.CopyTextToClipboard(md4str(client->GetUserHash()));
			theApp.emuledlg->statusbar->SetText(GetResString(_T("USER_HASH_COPIED_TO_CLIPBOARD")), SBarLog, 0);
			return;
		}
	}

	if (nChar == 'X' && GetKeyState(VK_CONTROL) < 0) {
		ClientReference clientRef;
		if (GetSelectedClient(clientRef)) {
			CUpDownClient* client = clientRef.Get();
			CString m_strClientIpport;
			m_strClientIpport.Format(_T("%s:%u"), ipstr(client->GetConnectIP()), client->GetUserPort());
			if (!m_strClientIpport.IsEmpty()) {
				theApp.CopyTextToClipboard(m_strClientIpport);
				theApp.emuledlg->statusbar->SetText(GetResString(_T("USER_IP_PORT_COPIED_TO_CLIPBOARD")), SBarLog, 0);
			}
		}
		return;
	}

	CMuleListCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CDownloadClientsCtrl::MaintainSortOrderAfterUpdate()
{
	if (!::IsWindow(m_hWnd) || GetSortItem() == -1)
		return;

	SetRedraw(false);
	SortItems(SortProc, MAKELONG(GetSortItem(), !GetSortAscending()));
	SetRedraw(true);
	Invalidate(FALSE);
}
