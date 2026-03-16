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
#include "DownloadListCtrl.h"
#include "updownclient.h"
#include "MenuCmds.h"
#include "ClientDetailDialog.h"
#include "FileDetailDialog.h"
#include "commentdialoglst.h"
#include "MetaDataDlg.h"
#include "InputBox.h"
#include "KademliaWnd.h"
#include "emuledlg.h"
#include "DownloadQueue.h"
#include "FriendList.h"
#include "PartFile.h"
#include "ClientCredits.h"
#include "MemDC.h"
#include "OtherFunctions.h"
#include "ChatWnd.h"
#include "TransferDlg.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "Kademlia/Kademlia/Prefs.h"
#include "Kademlia/net/KademliaUDPListener.h"
#include "WebServices.h"
#include "Preview.h"
#include "StringConversion.h"
#include "AddSourceDlg.h"
#include "CollectionViewDialog.h"
#include "SearchDlg.h"
#include "SharedFileList.h"
#include "ToolbarWnd.h"
#include "UploadQueue.h"
#include "log.h"
#include "UserMsgs.h"
#include "io.h"
#include "fcntl.h"
#include "MuleStatusBarCtrl.h"
#include "ClientList.h"
#include "eMuleAI/DarkMode.h"
#include "ListViewSearchDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


// CDownloadListCtrl

#define DLC_BARUPDATE 512

#define RATING_ICON_WIDTH	16

LPARAM CDownloadListCtrl::m_pSortParam = NULL;
bool CDownloadListCtrl::s_bFileDeletionInProgress = false;

namespace
{
	const EListStateField kDownloadListViewState = static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL);
	const DWORD kDownloadListSetItemCountFlags = LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL;

	int GetBitmapWidth(CBitmap& bitmap)
	{
		BITMAP bitmapInfo = {};
		if (bitmap.GetSafeHandle() == NULL || bitmap.GetBitmap(&bitmapInfo) == 0)
			return 0;

		return bitmapInfo.bmWidth;
	}

	void UpdateDownloadListItemCount(CListCtrl& listCtrl, const size_t itemCount)
	{
		listCtrl.SetItemCountEx(static_cast<int>(itemCount), kDownloadListSetItemCountFlags);
	}

	void RebuildPreviewMenu(CMenuXP& menu, const CPartFile* file, bool bEnablePreview, bool bEnablePauseOnPreview, bool bPauseOnPreviewChecked, bool bEnablePreviewParts, bool bPreviewPartsChecked)
	{
		while (menu.GetMenuItemCount() > 0)
			menu.RemoveMenu(0, MF_BYPOSITION);

		CString strPrimaryCommand = thePrefs.GetVideoPlayer();
		if (file != NULL) {
			const int iPreviewApp = thePreviewApps.GetPreviewApp(file);
			if (iPreviewApp >= 0)
				strPrimaryCommand = thePreviewApps.GetPreviewAppCmd(iPreviewApp);
		}

		const CString strPrimaryLabel = thePreviewApps.GetPreviewAppDisplayNameByCommand(strPrimaryCommand);
		menu.AppendODMenu(MF_STRING | (bEnablePreview ? MF_ENABLED : MF_GRAYED), MP_PREVIEW, new CMenuXPText(MP_PREVIEW, strPrimaryLabel.IsEmpty() ? GetResString(_T("DL_PREVIEW")) : strPrimaryLabel, thePreviewApps.GetPreviewCommandIcon(strPrimaryCommand)));
		thePreviewApps.GetAllMenuEntries(menu, file, strPrimaryCommand);
		menu.AppendMenu(MF_SEPARATOR);
		if (!thePrefs.GetPreviewPrio()) {
			menu.AppendMenu(MF_STRING | (bEnablePreviewParts ? MF_ENABLED : MF_GRAYED), MP_TRY_TO_GET_PREVIEW_PARTS, GetResString(_T("DL_TRY_TO_GET_PREVIEW_PARTS")));
			menu.CheckMenuItem(MP_TRY_TO_GET_PREVIEW_PARTS, bPreviewPartsChecked ? MF_CHECKED : MF_UNCHECKED);
		}
		menu.AppendMenu(MF_STRING | (bEnablePauseOnPreview ? MF_ENABLED : MF_GRAYED), MP_PAUSEONPREVIEW, GetResString(_T("PAUSEONPREVIEW")));
		menu.CheckMenuItem(MP_PAUSEONPREVIEW, bPauseOnPreviewChecked ? MF_CHECKED : MF_UNCHECKED);
	}
}

IMPLEMENT_DYNAMIC(CtrlItem_Struct, CObject)

IMPLEMENT_DYNAMIC(CDownloadListCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CDownloadListCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_DELETEITEM, OnListModified)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetDispInfo)
	ON_NOTIFY_REFLECT(LVN_GETINFOTIP, OnLvnGetInfoTip)
	ON_NOTIFY_REFLECT(LVN_INSERTITEM, OnListModified)
	ON_NOTIFY_REFLECT(LVN_ITEMACTIVATE, OnLvnItemActivate)
	ON_NOTIFY_REFLECT(LVN_ITEMCHANGED, OnListModified)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
	ON_WM_CONTEXTMENU()
	ON_WM_SYSCOLORCHANGE()
	ON_MESSAGE(UM_EMPTYFAKEFILEFOUND, OnEmptyFakeFileFound)
	ON_MESSAGE(UM_INVALIDEXTENSIONFOUND, OnInvalidExtensionFound)
END_MESSAGE_MAP()

CDownloadListCtrl::CDownloadListCtrl()
	: CDownloadListListCtrlItemWalk(this)
	, curTab()
	, m_bRemainSort()
	, m_pFontBold()
	, m_dwLastAvailableCommandsCheck()
	, m_availableCommandsDirty(true)
	, pDetectEmptyFakeFiles()
	, m_dwLastDetection()
	, m_bRightClicked()
	, m_iDataSize(-1)
	, m_uListedFilesCount()
{
	SetGeneralPurposeFind(true);
	SetSkinKey(_T("DownloadsLv"));
}

CDownloadListCtrl::~CDownloadListCtrl()
{
	if (m_PreviewMenu)
		VERIFY(m_PreviewMenu.DestroyMenu());
	if (m_PrioMenu)
		VERIFY(m_PrioMenu.DestroyMenu());
	if (m_SourcesMenu)
		VERIFY(m_SourcesMenu.DestroyMenu());
	if (m_FileMenu)
		VERIFY(m_FileMenu.DestroyMenu());

	while (!m_ListItems.empty()) {
		delete m_ListItems.begin()->second; // second = CtrlItem_Struct*
		m_ListItems.erase(m_ListItems.begin());
	}
}

void CDownloadListCtrl::Init()
{
	SetPrefsKey(_T("DownloadListCtrl"));
	SetStyle();
	ASSERT((GetStyle() & LVS_SINGLESEL) == 0);

	CToolTipCtrl *tooltip = GetToolTips();
	if (tooltip) {
		m_tooltip.SetFileIconToolTip(true);
		m_tooltip.SubclassWindow(*tooltip);
		tooltip->ModifyStyle(0, TTS_NOPREFIX);
		tooltip->SetDelayTime(TTDT_AUTOPOP, SEC2MS(20));
		tooltip->SetDelayTime(TTDT_INITIAL, SEC2MS(thePrefs.GetToolTipDelay()));
	}

	InsertColumn(0,		EMPTY,	LVCFMT_LEFT,	DFLT_FILENAME_COL_WIDTH);		//DL_FILENAME
	InsertColumn(1,		EMPTY,	LVCFMT_RIGHT,	DFLT_SIZE_COL_WIDTH);			//DL_SIZE
	InsertColumn(2,		EMPTY,	LVCFMT_RIGHT,	DFLT_SIZE_COL_WIDTH, -1, true);	//DL_TRANSF
	InsertColumn(3,		EMPTY,	LVCFMT_RIGHT,	DFLT_SIZE_COL_WIDTH);			//DL_TRANSFCOMPL
	InsertColumn(4,		EMPTY,	LVCFMT_RIGHT,	DFLT_DATARATE_COL_WIDTH);		//DL_SPEED
	InsertColumn(5,		EMPTY,	LVCFMT_LEFT,	DFLT_PARTSTATUS_COL_WIDTH);		//DL_PROGRESS
	InsertColumn(6,		EMPTY,	LVCFMT_RIGHT,	60);							//DL_SOURCES
	InsertColumn(7,		EMPTY,	LVCFMT_LEFT,	DFLT_PRIORITY_COL_WIDTH);		//PRIORITY
	InsertColumn(8,		EMPTY,	LVCFMT_LEFT,	70);							//STATUS
	InsertColumn(9,		EMPTY,	LVCFMT_RIGHT,	110);							//DL_REMAINS
	InsertColumn(10,	EMPTY,	LVCFMT_LEFT,	150, -1, true);					//LASTSEENCOMPL
	InsertColumn(11,	EMPTY,	LVCFMT_LEFT,	120, -1, true);					//FD_LASTCHANGE
	InsertColumn(12,	EMPTY,	LVCFMT_LEFT,	100, -1, true);					//CAT
	InsertColumn(13,	EMPTY,	LVCFMT_LEFT,	120);							//ADDEDON
	InsertColumn(14,	EMPTY, LVCFMT_LEFT,		100);
	InsertColumn(15,	EMPTY, LVCFMT_LEFT,		90);
	InsertColumn(16,	EMPTY, LVCFMT_LEFT,		60);

	SetAllIcons();
	LoadSettings();
	curTab = 0;

	ShowActiveDownloadsBold(thePrefs.GetShowActiveDownloadsBold());

	// Barry - Use preferred sort order from preferences
	m_bRemainSort = thePrefs.TransferlistRemainSortStyle();
	int adder;
	if (GetSortItem() != 9 || !m_bRemainSort) {
		SetSortArrow();
		adder = 0;
	} else {
		SetSortArrow(GetSortItem(), GetSortAscending() ? arrowDoubleUp : arrowDoubleDown);
		adder = 81; //9+81=90 - used in Compare(,,)
	}
	m_pSortParam = MAKELONG(GetSortItem() + adder, !GetSortAscending());
	UpdateSortHistory(m_pSortParam); // This will save sort parameter history in m_liSortHistory which will be used when we call GetNextSortOrder.
}

void CDownloadListCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
	SetAllIcons();
	CreateMenus();
}

void CDownloadListCtrl::SetAllIcons()
{
	ApplyImageList(NULL);
	m_ImageList.DeleteImageList();
	m_ImageList.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	m_ImageList.Add(CTempIconLoader(_T("SrcDownloading")));	//0
	m_ImageList.Add(CTempIconLoader(_T("SrcOnQueue")));		//1
	m_ImageList.Add(CTempIconLoader(_T("SrcConnecting")));	//2
	m_ImageList.Add(CTempIconLoader(_T("SrcNNPQF")));		//3
	m_ImageList.Add(CTempIconLoader(_T("SrcUnknown")));		//4
	m_ImageList.Add(CTempIconLoader(_T("ClientCompatible")));//5
	m_ImageList.Add(CTempIconLoader(_T("Friend")));			//6
	m_ImageList.Add(CTempIconLoader(_T("ClientEDonkey")));	//7
	m_ImageList.Add(CTempIconLoader(_T("ClientMLDonkey")));	//8
	m_ImageList.Add(CTempIconLoader(_T("ClientEDonkeyHybrid")));//9
	m_ImageList.Add(CTempIconLoader(_T("ClientShareaza")));	//10
	m_ImageList.Add(CTempIconLoader(_T("Server")));			//11
	m_ImageList.Add(CTempIconLoader(_T("ClientAMule")));	//12
	m_ImageList.Add(CTempIconLoader(_T("ClientLPhant")));	//13
	m_ImageList.Add(CTempIconLoader(_T("Rating_NotRated")));//14
	m_ImageList.Add(CTempIconLoader(_T("Rating_Fake")));	//15
	m_ImageList.Add(CTempIconLoader(_T("Rating_Poor")));	//16
	m_ImageList.Add(CTempIconLoader(_T("Rating_Fair")));	//17
	m_ImageList.Add(CTempIconLoader(_T("Rating_Good")));	//18
	m_ImageList.Add(CTempIconLoader(_T("Rating_Excellent")));//19
	m_ImageList.Add(CTempIconLoader(_T("Collection_Search"))); //20 rating for comments are searched on kad
	m_ImageList.SetOverlayImage(m_ImageList.Add(CTempIconLoader(_T("ClientSecureOvl"))), 1);
	m_ImageList.SetOverlayImage(m_ImageList.Add(CTempIconLoader(_T("OverlayObfu"))), 2);
	m_ImageList.SetOverlayImage(m_ImageList.Add(CTempIconLoader(_T("OverlaySecureObfu"))), 3);
	// Apply the image list also to the listview control, even if we use our own 'DrawItem'.
	// This is needed to give the listview control a chance to initialize the row height.
	VERIFY(ApplyImageList(m_ImageList) == NULL);
}

void CDownloadListCtrl::Localize()
{
	static const LPCTSTR uids[17] =
	{
		_T("DL_FILENAME"), _T("DL_SIZE"), _T("DL_TRANSF"), _T("DL_TRANSFCOMPL"), _T("DL_SPEED")
		, _T("DL_PROGRESS"), _T("DL_SOURCES"), _T("PRIORITY"), _T("STATUS"), _T("DL_REMAINS")
		, 0/*LASTSEENCOMPL*/, 0/*FD_LASTCHANGE*/, _T("CAT"), _T("ADDEDON")
                , _T("GEOLOCATION")
                , _T("PREVIEW_AVAILABLE")
				, _T("FD_COMPR")
	};

	LocaliseHeaderCtrl(uids, _countof(uids));

	CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	HDITEM hdi;
	hdi.mask = HDI_TEXT;

	CString strRes(GetResString(_T("LASTSEENCOMPL")));
	strRes.Remove(_T(':'));
	hdi.pszText = const_cast<LPTSTR>((LPCTSTR)strRes);
	pHeaderCtrl->SetItem(10, &hdi);

	strRes = GetResString(_T("FD_LASTCHANGE"));
	strRes.Remove(_T(':'));
	hdi.pszText = const_cast<LPTSTR>((LPCTSTR)strRes);
	pHeaderCtrl->SetItem(11, &hdi);

	CreateMenus();
}

void CDownloadListCtrl::AddFile(CPartFile* toadd)
{
	if (theApp.IsClosing() || !toadd)
		return;

	// Create new Item
	CtrlItem_Struct* newitem = new CtrlItem_Struct;
	int itemnr = GetItemCount();
	newitem->owner = NULL;
	newitem->type = FILE_TYPE;
	newitem->value = toadd;
	newitem->parent = NULL;
	newitem->dwUpdated = 0;

	// The same file shall be added only once
	ASSERT(m_ListItems.find(toadd) == m_ListItems.end());
	m_ListItems.emplace(toadd, newitem);
	
	if (theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible() || IsFilteredOut(toadd))
		return;

	SaveListState(0, kDownloadListViewState); // Save selections and scroll state
	SetRedraw(false); // Suspend painting
	m_ListedItemsVector.insert(m_ListedItemsVector.begin() + (int)m_ListedItemsVector.size(), newitem); // Add the new item to the vector.
	CombinedSort(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), SortFunc); // Keep current sort order.
	RebuildListedItemsMap(); // Rebuild the map after sorting.
	UpdateDownloadListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list.
	RestoreListState(0, kDownloadListViewState, false); // Restore selections and scroll state
	SetRedraw(true); // Resume painting
	Invalidate(FALSE); // Trigger redraw

	if (!theApp.emuledlg->IsInitializing())
		theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount(); // Refresh the file count.
}

void CDownloadListCtrl::AddSource(CPartFile* owner, CUpDownClient* source, bool notavailable)
{
	if (theApp.IsClosing() || !owner || !source)
		return;

	ItemType itemtype = notavailable ? UNAVAILABLE_SOURCE : AVAILABLE_SOURCE;

	// Check for existing entries of this source (may appear in other files).
	bool bFound = false;
	for (ListItems::const_iterator it = m_ListItems.lower_bound(source); it != m_ListItems.end() && it->first == source; ++it) {
		CtrlItem_Struct* cur_item = it->second;

		// Check if this source has been already added to this file => to be sure
		if (cur_item->owner == owner) { // Same file-source pair: just update flags.
			cur_item->type = itemtype;
			cur_item->dwUpdated = 0;
			bFound = true;
		} else if (!notavailable) { // Different file: ensure exclusivity of available.
			cur_item->type = UNAVAILABLE_SOURCE;
			cur_item->dwUpdated = 0;
		}
	}

	// The same source could be added a few times but only once per file
	if (bFound)
		return;

	// Parent file entry (must exist).
	ListItems::const_iterator itOwner = m_ListItems.find(owner);
	if (itOwner == m_ListItems.end())
		return;
	CtrlItem_Struct* ownerItem = itOwner->second;

	// Create new Item
	CtrlItem_Struct* newitem = new CtrlItem_Struct;
	newitem->type = itemtype;
	newitem->owner = owner;
	newitem->value = source;
	newitem->parent = ownerItem; // cross link to the owner
	newitem->dwUpdated = 0;

	m_ListItems.emplace(source, newitem);

	// Only show if parents sources branch is expanded and passes current filter.
	if (theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible() || !owner->srcarevisible || IsFilteredOut(owner))
		return;

	SaveListState(0, kDownloadListViewState); // Save selections and scroll state
	SetRedraw(false); // Suspend painting

	int iParentIndex = -1;
	bool bVectorModified = false;
	if (m_ListedItemsMap.Lookup(ownerItem, iParentIndex)) { // If parent item is found, insert after it.
		m_ListedItemsVector.insert(m_ListedItemsVector.begin() + iParentIndex + 1, newitem);
		RebuildListedItemsMap(); // Rebuild the map after inserting.
		UpdateDownloadListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list.
		bVectorModified = true;
	}

	RestoreListState(0, kDownloadListViewState, false); // Restore selections and scroll state
	SetRedraw(true); // Resume painting
	if (bVectorModified)
		Invalidate(FALSE); // Trigger redraw

}

void CDownloadListCtrl::RemoveSource(CUpDownClient* source, CPartFile* owner)
{
	if (theApp.IsClosing() || !source)
		return;

	bool bVectorModified = false;
	bool m_bUpdateListedItems = true; // Flag to check if we need to update the vector and map
	if (theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible())
		m_bUpdateListedItems = false;

	if (m_bUpdateListedItems) {
		SaveListState(0, kDownloadListViewState); // Save selections and scroll state
		SetRedraw(false); // Suspend painting
	}

	for (ListItems::const_iterator it = m_ListItems.lower_bound(source); it != m_ListItems.end() && it->first == source; ) {
		CtrlItem_Struct* delItem = it->second;

		if (owner == NULL || owner == delItem->owner) {
			it = m_ListItems.erase(it);      // remove from main list

			if (m_bUpdateListedItems) {
				int iVectorIndex;
				if (m_ListedItemsMap.Lookup(delItem, iVectorIndex))	{
					m_ListedItemsVector.erase(m_ListedItemsVector.begin() + iVectorIndex);
					bVectorModified = true;
				}
			}

			delete delItem; // Free memory
		} else
			++it;
	}

	if (m_bUpdateListedItems) {
		if (bVectorModified) { // If the vector was modified, we need to rebuild the map and update the item count.
			RebuildListedItemsMap(); // Rebuild map after vector shrink.
			UpdateDownloadListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list.
		}

		RestoreListState(0, kDownloadListViewState, false); // Restore selections and scroll state
		SetRedraw(true); // Resume painting
		Invalidate(FALSE); // Trigger redraw
	}
}

bool CDownloadListCtrl::RemoveFile(CPartFile* toremove)
{
	bool bResult = false;
	if (theApp.IsClosing() || !toremove)
		return bResult;

	bool bVectorModified = false;
	bool m_bUpdateListedItems = true; // Flag to check if we need to update the vector and map
	if (theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible())
		m_bUpdateListedItems = false;

	if (m_bUpdateListedItems) {
		SaveListState(0, kDownloadListViewState); // Save selections and scroll state
		SetRedraw(false); // Suspend painting
	}

	for (ListItems::iterator it = m_ListItems.begin(); it != m_ListItems.end();) {
		CtrlItem_Struct* delItem = it->second;
		if (delItem->owner == toremove || delItem->value == toremove) {
			it = m_ListItems.erase(it);	// Drop from main list

			if (m_bUpdateListedItems) {
				// Remove from visible vector only when the file branch is expanded
				if (!delItem->owner || delItem->owner->srcarevisible) {
					int idx;
					if (m_ListedItemsMap.Lookup(delItem, idx)) { // If item is found in the map, remove it from the vector.
						m_ListedItemsVector.erase(m_ListedItemsVector.begin() + idx); // Remove from vector
						bVectorModified = true; // Indicate that the vector was modified
					}
				}
			}

			delete delItem; // Free memory
			bResult = true; // Indicate that at least one item was removed
		} else
			++it; // Continue iterating through the list
	}

	if (m_bUpdateListedItems) {
		if (bVectorModified) { // If the vector was modified, we need to rebuild the map and update the item count.
			RebuildListedItemsMap(); // Rebuild map after vector shrink.
			UpdateDownloadListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list.
		}

		RestoreListState(0, kDownloadListViewState, false); // Restore selections and scroll state
		SetRedraw(true); // Resume painting
		if (bVectorModified) // If the vector was modified, we need to trigger a redraw.
			Invalidate(FALSE); // Trigger redraw

		theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount(); // Refresh the file count.
	}

	return bResult;
}

void CDownloadListCtrl::UpdateItem(void* toupdate)
{
	if (theApp.IsClosing() || !toupdate || theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || theApp.emuledlg->transferwnd->GetDownloadList()->IsWindowVisible() == false)
		return;

	// Keep separate throttles for file rows and source rows to prevent source traffic from starving file progress updates.
	bool bFileUpdate = false;
	for (ListItems::const_iterator it = m_ListItems.lower_bound(toupdate); it != m_ListItems.end() && it->first == toupdate; ++it) {
		if (it->second && it->second->type == FILE_TYPE) {
			bFileUpdate = true;
			break;
		}
	}

	static DWORD dwLastFileUpdateTime = 0;
	static DWORD dwLastSourceUpdateTime = 0;
	const DWORD dwCurrentTime = GetTickCount();
	const DWORD dwUpdatePeriod = static_cast<DWORD>(thePrefs.GetUITweaksListUpdatePeriod());
	DWORD& dwLastUpdateTime = bFileUpdate ? dwLastFileUpdateTime : dwLastSourceUpdateTime;

	if (dwCurrentTime - dwLastUpdateTime < dwUpdatePeriod)
		return;

	dwLastUpdateTime = dwCurrentTime;

	bool bItemUpdated = false;

	// Retrieve all entries matching the source
	for (ListItems::const_iterator it = m_ListItems.lower_bound(toupdate); it != m_ListItems.end() && it->first == toupdate; ++it) {
		CtrlItem_Struct* updateItem = it->second;

		if (updateItem->owner && !updateItem->owner->srcarevisible)
			continue; // Skip invisible branches

		// Update only if item is currently displayed in the virtual list
		int m_iIndex;
		if (m_ListedItemsMap.Lookup(updateItem, m_iIndex)) {
			if (updateItem->type != FILE_TYPE) {
				updateItem->dwUpdated = 0;
				Update(m_iIndex);
				bItemUpdated = true;
				continue;
			}

			CPartFile* partFile = static_cast<CPartFile*>(updateItem->value);
			if (!IsFilteredOut(partFile)) {
				updateItem->dwUpdated = 0; // Reset update flag
				Update(m_iIndex); // Repaint row
				bItemUpdated = true;
			} else
				HideFile(partFile); // Hide the item if it is filtered out.
		}
	}

	m_availableCommandsDirty = true;

	// If items were updated and list is sorted, maintain sort order
	// Skip MaintainSortOrderAfterUpdate if file deletion is in progress to avoid multiple SaveListState/RestoreListState calls
	if (bItemUpdated && !s_bFileDeletionInProgress && ShouldMaintainSortOrderOnUpdate()) {
		static DWORD dwLastSortTime = 0;
		const DWORD dwSortNow = GetTickCount();
		
		// Only resort if enough time has passed since last sort (respect user's update period setting)
		if (dwSortNow - dwLastSortTime > static_cast<DWORD>(thePrefs.GetUITweaksListUpdatePeriod())) {
			MaintainSortOrderAfterUpdate();
			dwLastSortTime = dwSortNow;
		}
	}
}

void CDownloadListCtrl::ReloadList(const bool bSortCurrentList, const EListStateField LsfFlag)
{
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible())
		return;

	CWaitCursor curWait;
	bool bInitializing = (m_iDataSize == -1); // Check if this is the first call to ReloadList

	// Initializing the vector and map
	if (bInitializing) {
		m_iDataSize = 10007; // Any reasonable prime number for the initial size.
		m_ListedItemsVector.reserve(m_iDataSize);
		m_ListedItemsMap.InitHashTable(m_iDataSize);
	} else
		SaveListState(0, LsfFlag); // Save selections, sort and scroll values for the previous m_nResultsID if this is not the first call.

	SetRedraw(false); // Suspend painting

	if (!bSortCurrentList) {
		// Clear and reload data
		m_ListedItemsVector.clear();
		m_uListedFilesCount = 0;

		for (auto it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
			CtrlItem_Struct* cur_item = it->second;
			if (!cur_item)
				continue;

			if (cur_item->type == FILE_TYPE) { // File, always parent item
				CPartFile* file = static_cast<CPartFile*>(cur_item->value);
				if (!IsFilteredOut(file)) { // Visible file
					m_ListedItemsVector.push_back(cur_item); // Add file item to the vector.
					m_uListedFilesCount++;
				}
			} else { // Source, always child item
				CPartFile* parent = cur_item->owner;
				if (parent && parent->srcarevisible && !IsFilteredOut(parent)) // Child with a visible branch
					m_ListedItemsVector.push_back(cur_item); // Add source item to the vector.
			}
		}
	}

	// Reloading data completed at this point. Now we need to sort the vector.
	// Sort vector, then load sorted data to map and reverse map
	CombinedSort(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), SortFunc);
	RebuildListedItemsMap(); // Rebuild the map after sorting.

	UpdateDownloadListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list before restoring state.
	theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount(); // Refresh the file count.

	// Skip RestoreListState if file deletion is in progress to avoid redundant SaveListState/RestoreListState calls
	if (!bInitializing && !s_bFileDeletionInProgress)
		RestoreListState(0, LsfFlag, false); // Restore selections, sort and scroll values if this is not the first call.

	SetRedraw(true); // Resume painting
	Invalidate(); //Force redraw
}

// Index map after vector changes
void CDownloadListCtrl::RebuildListedItemsMap()
{
	m_ListedItemsMap.RemoveAll();
	for (int i = 0; i < static_cast<int>(m_ListedItemsVector.size()); ++i)
		m_ListedItemsMap[m_ListedItemsVector[i]] = i;
}

const bool CDownloadListCtrl::SortFunc(const CtrlItem_Struct* first, const CtrlItem_Struct* second)
{
	return SortProc((LPARAM)first, (LPARAM)second, m_pSortParam) < 0; // If the first one has a smaller value returns true, otherwise returns false.
}

CObject* CDownloadListCtrl::GetItemObject(int iIndex) const
{
	if (iIndex < 0 || iIndex >= m_ListedItemsVector.size())
		return nullptr;
	return m_ListedItemsVector[iIndex];
}

void CDownloadListCtrl::DrawFileItem(CDC *dc, int nColumn, LPCRECT lpRect, UINT uDrawTextAlignment, CtrlItem_Struct *pCtrlItem)
{
	/*const*/ CPartFile *pPartFile = static_cast<CPartFile*>(pCtrlItem->value);
	const CString &sItem(GetFileItemDisplayText(pPartFile, nColumn));
	CRect rcDraw(lpRect);
	switch (nColumn) {
	case 0: // file name
		{
			LONG iIconPosY = max((rcDraw.Height() - theApp.GetSmallSytemIconSize().cy) / 2,  0);
			int iImage = theApp.GetFileTypeSystemImageIdx(pPartFile->GetFileName());
			if (theApp.GetSystemImageList() != NULL)
				::ImageList_Draw(theApp.GetSystemImageList(), iImage, dc->GetSafeHdc(), rcDraw.left, rcDraw.top + iIconPosY, ILD_TRANSPARENT);
			rcDraw.left += theApp.GetSmallSytemIconSize().cx;

			if (thePrefs.ShowRatingIndicator() && (pPartFile->HasComment() || pPartFile->HasRating() || pPartFile->IsKadCommentSearchRunning())) {
				SafeImageListDraw(&m_ImageList, dc, 14 + pPartFile->UserRating(true), CPoint(rcDraw.left + 2, rcDraw.top + iIconPosY), ILD_NORMAL);
				rcDraw.left += 2 + RATING_ICON_WIDTH;
			}

			rcDraw.left += sm_iLabelOffset;
			dc->DrawText(sItem, -1, rcDraw, MLC_DT_TEXT | uDrawTextAlignment);
		}
		break;
	case 5: // progress
		{
			--rcDraw.bottom;
			++rcDraw.top;
			int iWidth = rcDraw.Width();
			int iHeight = rcDraw.Height();

			// Validate dimensions and avoid invalid GDI calls
			if (iWidth <= 0 || iHeight <= 0)
				break;

			// Read the cached bitmap width with GetBitmap to avoid GetBitmapDimensionEx asserts.
			const int cx = GetBitmapWidth(pCtrlItem->status);

			const DWORD curTick = ::GetTickCount();
			if (curTick >= pCtrlItem->dwUpdated + DLC_BARUPDATE || cx != iWidth || !pCtrlItem->dwUpdated) {
				RECT statusRect = { rcDraw.left, rcDraw.top, rcDraw.left + iWidth, rcDraw.top + iHeight };
				pPartFile->DrawStatusBar(dc, &statusRect, thePrefs.UseFlatBar());
				pCtrlItem->dwUpdated = curTick + (rand() & 0x7f);
			}

			if (thePrefs.GetUseDwlPercentage()) {
				COLORREF oldTextColor = dc->SetTextColor(RGB(255, 255, 255));
				int oldBkMode = dc->SetBkMode(TRANSPARENT);
				CString percentText = sItem.Mid(sItem.ReverseFind(_T(' ')) + 1);
				dc->DrawText(percentText, -1, rcDraw, (MLC_DT_TEXT & ~DT_LEFT) | DT_CENTER);
				dc->SetBkMode(oldBkMode);
				dc->SetTextColor(oldTextColor);
			}
		}
		break;
	default:
		dc->DrawText(sItem, -1, rcDraw, MLC_DT_TEXT | uDrawTextAlignment);
	}
}

CString CDownloadListCtrl::GetSourceItemDisplayText(const CtrlItem_Struct *pCtrlItem, int iSubItem)
{
	CString sText;
	const CUpDownClient *pClient = static_cast<CUpDownClient*>(pCtrlItem->value);
	switch (iSubItem) {
	case 0: //icon, name, status
		if (pClient->GetUserName())
			return CString(pClient->GetUserName());
		sText.Format(_T("(%s)"), (LPCTSTR)GetResString(_T("UNKNOWN")));
		break;
	case 1: //source from
		{
			LPCTSTR uid;
			switch (pClient->GetSourceFrom()) {
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
				uid = EMPTY;
			}
			if (uid)
				return GetResString(uid);
		}
		break;
	case 2: //transferred
	case 3: //completed
		// - 'Transferred' column: Show transferred data
		// - 'Completed' column: If 'Transferred' column is hidden, show the amount of transferred data
		//	  in 'Completed' column. This is plain wrong (at least when receiving compressed data), but
		//	  users seem to got used to it.
		if (iSubItem == 2 || IsColumnHidden(2)) {
			if (pCtrlItem->type == AVAILABLE_SOURCE && pClient->GetTransferredDown())
				return CastItoXBytes(pClient->GetTransferredDown());
		}
		break;
	case 4: //speed
		if (pCtrlItem->type == AVAILABLE_SOURCE && pClient->GetDownloadDatarate())
			return CastItoXBytes(pClient->GetDownloadDatarate(), false, true);
		break;
	case 5: //file info
		return GetResString(_T("DL_PROGRESS"));
	case 6: //sources
		sText = pClient->DbgGetFullClientSoftVer();
		if (sText.IsEmpty())
			sText = GetResString(_T("UNKNOWN"));
		return sText;
	case 7: //prio
		if (pClient->GetDownloadState() == DS_ONQUEUE) {
			if (pClient->IsRemoteQueueFull())
				return GetResString(_T("QUEUEFULL"));
			if (pClient->GetRemoteQueueRank())
				sText.Format(_T("QR: %u"), pClient->GetRemoteQueueRank());
		}
		break;
	case 8: //status
		{
			if (pCtrlItem->type == AVAILABLE_SOURCE)
				sText = pClient->GetDownloadStateDisplayString();
			else {
				sText = GetResString(_T("ASKED4ANOTHERFILE"));
				if (thePrefs.IsExtControlsEnabled()) {
					LPCTSTR uid;
					if (pClient->IsInNoNeededList(pCtrlItem->owner))
						uid = _T("NONEEDEDPARTS");
					else if (pClient->GetDownloadState() == DS_DOWNLOADING)
						uid = _T("TRANSFERRING");
					else if (const_cast<CUpDownClient*>(pClient)->IsSwapSuspended(pClient->GetRequestFile()))
						uid = _T("SOURCESWAPBLOCKED");
					else
						uid = EMPTY;
					if (uid)
						sText.AppendFormat(_T(" (%s)"), (LPCTSTR)GetResString(uid));
					if (pClient->GetRequestFile() && !pClient->GetRequestFile()->GetFileName().IsEmpty())
						sText.AppendFormat(_T(": \"%s\""), (LPCTSTR)pClient->GetRequestFile()->GetFileName());
				}
			}

			if (thePrefs.IsExtControlsEnabled() && !pClient->m_OtherRequests_list.IsEmpty())
				sText += _T('*');
		break;
		}
	//	break;
	//case 10: //last seen complete
	//case 11: //last received
	//case 12: //category
	//case 13: //added on
	case 14:
		return CString(pClient->GetGeolocationData());
	}
	return sText;
}

void CDownloadListCtrl::DrawSourceItem(CDC *dc, int nColumn, LPCRECT lpRect, UINT uDrawTextAlignment, CtrlItem_Struct *pCtrlItem)
{
	const CUpDownClient *pClient = static_cast<CUpDownClient*>(pCtrlItem->value);
	const CString &sItem(GetSourceItemDisplayText(pCtrlItem, nColumn));
	switch (nColumn) {
	case 0: // icon, name, status
		{
			CRect rcItem(*lpRect);
			int iIconPosY = (rcItem.Height() > 16) ? ((rcItem.Height() - 15) / 2) : 0;
			POINT point = {rcItem.left, rcItem.top + iIconPosY};
			int iImage;
			if (pCtrlItem->type == AVAILABLE_SOURCE) {
				switch (pClient->GetDownloadState()) {
				case DS_CONNECTED:
				case DS_CONNECTING:
				case DS_WAITCALLBACK:
				case DS_WAITCALLBACKKAD:
				case DS_TOOMANYCONNS:
				case DS_TOOMANYCONNSKAD:
					iImage = 2;
					break;
				case DS_ONQUEUE:
					iImage = pClient->IsRemoteQueueFull() ? 3 : 1;
					break;
				case DS_DOWNLOADING:
				case DS_REQHASHSET:
					iImage = 0;
					break;
				case DS_NONEEDEDPARTS:
				case DS_ERROR:
					iImage = 3;
					break;
				default:
					iImage = 4;
				}
			} else
				iImage = 3;
			SafeImageListDraw(&m_ImageList, dc, iImage, point, ILD_NORMAL);
			rcItem.left += 20;

			UINT uOvlImg = static_cast<UINT>((pClient->Credits() && pClient->Credits()->GetCurrentIdentState(pClient->GetIP()) == IS_IDENTIFIED));
			uOvlImg |= (static_cast<UINT>(pClient->IsObfuscatedConnectionEstablished()) << 1);

			if (pClient->IsFriend())
				iImage = 6;
			else
				switch (pClient->GetClientSoft()) {
				case SO_EDONKEYHYBRID:
					iImage = 9;
					break;
				case SO_MLDONKEY:
					iImage = 8;
					break;
				case SO_SHAREAZA:
					iImage = 10;
					break;
				case SO_URL:
					iImage = 11;
					break;
				case SO_AMULE:
					iImage = 12;
					break;
				case SO_LPHANT:
					iImage = 13;
					break;
				default:
					iImage = pClient->ExtProtocolAvailable() ? 5 : 7;
				}
			const POINT point2 = { rcItem.left, rcItem.top + iIconPosY };
			SafeImageListDraw(&m_ImageList, dc, iImage, point2, ILD_NORMAL | INDEXTOOVERLAYMASK(uOvlImg));
			if (theApp.geolite2->ShowCountryFlag() && IsColumnHidden(14)) {
				rcItem.left += 20;
			    POINT point3 = { rcItem.left,rcItem.top + 1 };
			    theApp.geolite2->GetFlagImageList()->DrawIndirect(&theApp.geolite2->GetFlagImageDrawParams(dc, pClient->GetCountryFlagIndex(), point3));
				rcItem.left += sm_iSubItemInset;
			}
			rcItem.left += 20;
			dc->DrawText(sItem, -1, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
		}
		break;
	case 5: // file info
		{
			CRect rcDraw(lpRect);
			--rcDraw.bottom;
			++rcDraw.top;
			int iWidth = rcDraw.Width();
			int iHeight = rcDraw.Height();

			// Validate dimensions and avoid invalid GDI calls
			if (iWidth <= 0 || iHeight <= 0)
				break;

			// Read the cached bitmap width with GetBitmap to avoid GetBitmapDimensionEx asserts.
			const int cx = GetBitmapWidth(pCtrlItem->status);
			const DWORD curTick = ::GetTickCount();
			if (curTick >= pCtrlItem->dwUpdated + DLC_BARUPDATE || cx != iWidth || !pCtrlItem->dwUpdated) {
				RECT statusRect = { rcDraw.left, rcDraw.top, rcDraw.left + iWidth, rcDraw.top + iHeight };
				pClient->DrawStatusBar(dc, &statusRect, (pCtrlItem->type == UNAVAILABLE_SOURCE), thePrefs.UseFlatBar());
				pCtrlItem->dwUpdated = curTick + (rand() & 0x7f);
			}
		}
		break;
	//case 10: // last seen complete
	//case 11: // last received
	//case 12: // category
	//case 13: // added on
	//	break;
	case 14: 
		{
			RECT cur_rec = *lpRect;
			if (theApp.geolite2->ShowCountryFlag()) {
				POINT point3 = { cur_rec.left,cur_rec.top + 1 };
				theApp.geolite2->GetFlagImageList()->DrawIndirect(&theApp.geolite2->GetFlagImageDrawParams(dc, pClient->GetCountryFlagIndex(), point3));
				cur_rec.left += 22;
			}
			dc->DrawText(sItem, -1, &cur_rec, MLC_DT_TEXT | uDrawTextAlignment);
		}
		break;
	default:
		dc->DrawText(sItem, -1, const_cast<LPRECT>(lpRect), MLC_DT_TEXT | uDrawTextAlignment);
	}
}

void CDownloadListCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	int index = static_cast<int>(lpDrawItemStruct->itemID);
	if (index < 0 || theApp.IsClosing() || m_ListedItemsVector.empty())
		return;

	CRect rcItem(lpDrawItemStruct->rcItem);
	CDC* pBaseDC = CDC::FromHandle(lpDrawItemStruct->hDC);
	CMemoryDC dc(pBaseDC, rcItem);
	BOOL bCtrlFocused;
	InitItemMemDC(dc, lpDrawItemStruct, bCtrlFocused);

	COLORREF clrBk = (lpDrawItemStruct->itemState & ODS_SELECTED) ? GetCustomSysColor(COLOR_HIGHLIGHT) : GetCustomSysColor(COLOR_WINDOW);
	dc.FillSolidRect(rcItem, clrBk);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(clrBk);

	RECT rcClient;
	GetClientRect(&rcClient);
	CtrlItem_Struct* content = reinterpret_cast<CtrlItem_Struct*>(m_ListedItemsVector[index]);

	if (m_pFontBold)
		if (content->type == FILE_TYPE && static_cast<CPartFile*>(content->value)->GetTransferringSrcCount()
			|| ((content->type == UNAVAILABLE_SOURCE || content->type == AVAILABLE_SOURCE)
				&& static_cast<CUpDownClient*>(content->value)->GetDownloadState() == DS_DOWNLOADING))
		{
			dc.SelectObject(m_pFontBold);
		}

	bool isChild = content->type != FILE_TYPE;
	bool notLast = lpDrawItemStruct->itemID + 1 < (UINT)GetItemCount();
	bool notFirst = lpDrawItemStruct->itemID > 0;
	int tree_start = 0;
	int tree_end = 0;

	int iTreeOffset = 8 - sm_iLabelOffset; //6
	const CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	int iCount = pHeaderCtrl->GetItemCount();
	LONG itemLeft = rcItem.left;
	rcItem.right = rcItem.left - sm_iLabelOffset;
	rcItem.left += sm_iIconOffset;

	if (!isChild && !g_bLowColorDesktop && (lpDrawItemStruct->itemState & ODS_SELECTED) == 0) {
		DWORD dwCatColor = thePrefs.GetCatColor(static_cast<CPartFile*>(content->value)->GetCategory(), COLOR_WINDOWTEXT);
		if (dwCatColor > 0)
			dc.SetTextColor(dwCatColor);
	}
	for (int iCurrent = 0; iCurrent < iCount; ++iCurrent) {
		int iColumn = pHeaderCtrl->OrderToIndex(iCurrent);
		if (IsColumnHidden(iColumn))
			continue;

		UINT uDrawTextAlignment;
		int iColumnWidth = GetColumnWidth(iColumn, uDrawTextAlignment);
		rcItem.left = itemLeft;
		rcItem.right = itemLeft + iColumnWidth;
		switch (iColumn) {
		case 5: //progress
			//set up tree vars
			tree_start = rcItem.left + 1;
			rcItem.left += iTreeOffset;
			tree_end = rcItem.left;
			rcItem.right -= iTreeOffset - sm_iLabelOffset;
		default:
			rcItem.left += sm_iLabelOffset;
			rcItem.right -= sm_iLabelOffset;
			if (rcItem.left < rcItem.right && HaveIntersection(rcClient, rcItem))
				if (isChild)
					DrawSourceItem(dc, iColumn, &rcItem, uDrawTextAlignment, content);
				else
					DrawFileItem(dc, iColumn, &rcItem, uDrawTextAlignment, content);
		}
		itemLeft += iColumnWidth;
	}

	DrawFocusRect(dc, &lpDrawItemStruct->rcItem, (lpDrawItemStruct->itemState & ODS_FOCUS) != 0, bCtrlFocused, (lpDrawItemStruct->itemState & ODS_SELECTED) != 0);

	//draw tree last so it draws over selected and focus (looks better)
	if (tree_start < tree_end) {
		//set new bounds
		RECT tree_rect = { tree_start, lpDrawItemStruct->rcItem.top, tree_end, lpDrawItemStruct->rcItem.bottom };
		dc.SetBoundsRect(&tree_rect, DCB_DISABLE);

		//gather some information
		bool hasNext = notLast && reinterpret_cast<CtrlItem_Struct*>(GetItemData(lpDrawItemStruct->itemID + 1))->type != FILE_TYPE;
		bool isOpenRoot = hasNext && content->type == FILE_TYPE;
		//might as well calculate these now
		int treeCenter = tree_start + 3;
		int middle = (rcItem.top + rcItem.bottom + 1) / 2;

		//set up a new pen for drawing the tree
		CPen pn, *oldpn;
		pn.CreatePen(PS_SOLID, 1, m_crWindowText);
		oldpn = dc.SelectObject(&pn);

		if (isChild) {
			//draw the line to the status bar
			dc.MoveTo(tree_end, middle);
			dc.LineTo(tree_start + 3, middle);

			//draw the line to the child node
			if (hasNext) {
				dc.MoveTo(treeCenter, middle);
				dc.LineTo(treeCenter, rcItem.bottom + 1);
			}
		} else if (isOpenRoot) {
			//draw circle
			RECT circle_rec = { treeCenter - 2, middle - 2, treeCenter + 3, middle + 3 };
			COLORREF crBk = dc.GetBkColor();
			CBrush brush(m_crWindowText);
			dc.FrameRect(&circle_rec, &brush);
			dc.SetPixelV(circle_rec.left, circle_rec.top, crBk);
			dc.SetPixelV(circle_rec.right - 1, circle_rec.top, crBk);
			dc.SetPixelV(circle_rec.left, circle_rec.bottom - 1, crBk);
			dc.SetPixelV(circle_rec.right - 1, circle_rec.bottom - 1, crBk);
			//draw the line to the child node (hasNext is true here)
			dc.MoveTo(treeCenter, middle + 3);
			dc.LineTo(treeCenter, rcItem.bottom + 1);
		} /*else if(isExpandable) {
			//draw a + sign
			dc.MoveTo(treeCenter, middle - 2);
			dc.LineTo(treeCenter, middle + 3);
			dc.MoveTo(treeCenter - 2, middle);
			dc.LineTo(treeCenter + 3, middle);
		}*/

		//draw the line back up to parent node
		if (notFirst && isChild) {
			dc.MoveTo(treeCenter, middle);
			dc.LineTo(treeCenter, rcItem.top - 1);
		}

		//put the old pen back
		dc.SelectObject(oldpn);
		pn.DeleteObject();
	}
}

void CDownloadListCtrl::HideSources(CPartFile* toCollapse)
{
	if (theApp.IsClosing() || !toCollapse || !toCollapse->srcarevisible)
		return;

	toCollapse->srcarevisible = false;
	SaveListState(0, kDownloadListViewState); // Save selections and scroll state
	SetRedraw(false); // Suspend painting

	// Find parent item index (if parent is visible)
	int iParentIndex = -1;
	ListItems::const_iterator itParent = m_ListItems.find(toCollapse);
	if (itParent != m_ListItems.end())
		m_ListedItemsMap.Lookup(itParent->second, iParentIndex);

	bool bVectorModified = false;
	for (size_t i = (iParentIndex >= 0 ? iParentIndex + 1 : 0); i < m_ListedItemsVector.size(); ) {
		CtrlItem_Struct* item = m_ListedItemsVector[i];

		// Check if the item belongs to the file being collapsed, then 
		if (item && item->owner == toCollapse) {
			item->dwUpdated = 0;
			// Remove all sources belonging to collapsed file
			m_ListedItemsMap.RemoveKey(item); // Drop from lookup map
			m_ListedItemsVector.erase(m_ListedItemsVector.begin() + i);	// Remove from vector
			bVectorModified = true; // Mark that the vector was modified
		} else
			break; // Children are contiguous; stop at first foreign item
	}

	if (bVectorModified) { // If we modified the vector, we need to update the control
		RebuildListedItemsMap(); // Rebuild the map after sorting.
		UpdateDownloadListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list before restoring state.
	}

	RestoreListState(0, kDownloadListViewState, false); // Restore selections and scroll state
	SetRedraw(true); // Resume painting
	if (bVectorModified)
		Invalidate(FALSE); // Force redraw
}

void CDownloadListCtrl::ExpandCollapseItem(int iItem, int iAction, bool bCollapseSource)
{
	if (iItem < 0)
		return;

	CtrlItem_Struct* content = m_ListedItemsVector[iItem];

	// To collapse/expand files when one of its sources is selected
	if (content && bCollapseSource && content->parent) {
		content = content->parent;
		if (!m_ListedItemsMap.Lookup(content, iItem))
			return;
	}

	if (!content || content->type != FILE_TYPE)
		return;

	CPartFile* partfile = static_cast<CPartFile*>(content->value);
	if (!partfile)
		return;

	if (partfile->CanOpenFile()) { 
		partfile->OpenFile();
		return; 
	}

	if (!partfile->srcarevisible) { // Sources branch currently hidden -> Expand
		if (iAction > COLLAPSE_ONLY) {
			SaveListState(0, kDownloadListViewState); // Save selections and scroll state
			SetRedraw(false); // Suspend painting
			partfile->srcarevisible = true; // Mark the sources as visible
			int parentIdx = -1;
			m_ListedItemsMap.Lookup(content, parentIdx); // Find the index of the parent item in the vector
			int insertPos = parentIdx + 1;

			// Go through the whole list to find out the sources for this file
			// Remark: don't use GetSourceCount() => UNAVAILABLE_SOURCE
			for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) { // Iterate through all items
				CtrlItem_Struct* cur_item = it->second;
				if (cur_item->owner == partfile) { // Check if the item belongs to the file being expanded
					m_ListedItemsVector.insert(m_ListedItemsVector.begin() + insertPos, cur_item); // Insert the source item into the vector
					++insertPos; // Increment the insert position for the next source
				}
			}

			RebuildListedItemsMap(); // Rebuild the map after sorting.
			UpdateDownloadListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list before restoring state.
			RestoreListState(0, kDownloadListViewState, false); // Restore selections and scroll state
			SetRedraw(true); // Resume painting
			Invalidate(FALSE); // Force redraw
		}
	} else if (iAction == EXPAND_COLLAPSE || iAction == COLLAPSE_ONLY) { // Sources branch currently visible -> Collapse
		// Keep focus on parent before collapsing
		if (GetItemState(iItem, LVIS_SELECTED | LVIS_FOCUSED) != (LVIS_SELECTED | LVIS_FOCUSED)) {
			SetItemState(iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			SetSelectionMark(iItem);
		}
		HideSources(partfile);
	}
}

void CDownloadListCtrl::OnLvnItemActivate(LPNMHDR pNMHDR, LRESULT *pResult)
{
	POINT point;
	::GetCursorPos(&point);
	CPoint p = point;
	ScreenToClient(&p);
	CHeaderCtrl* pHeaderCtrl = GetHeaderCtrl();
	int iColumn = pHeaderCtrl->OrderToIndex(1);
	int iColumnWidth = GetColumnWidth(iColumn);

	LPNMITEMACTIVATE pNMIA = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);

	if (thePrefs.IsDoubleClickEnabled() || pNMIA->iSubItem > 0)
		if (!thePrefs.GetPreviewOnIconDblClk() || (p.x > iColumnWidth))
			ExpandCollapseItem(pNMIA->iItem, EXPAND_COLLAPSE);
	*pResult = 0;
}

void CDownloadListCtrl::OnContextMenu(CWnd*, CPoint point)
{
	int iSel = GetNextItem(-1, LVIS_SELECTED);
	if (iSel >= 0) {
		const CtrlItem_Struct* content = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iSel));
		if (content != NULL && content->type == FILE_TYPE) {
			// get merged settings
			int iSelectedItems = 0;
			int iFilesNotDone = 0;
			int iFilesToPause = 0;
			int iFilesToStop = 0;
			int iFilesToResume = 0;
			int iFilesToOpen = 0;
			int iFilesGetPreviewParts = 0;
			int iFilesPreviewType = 0;
			int iFilesToPreview = 0;
			int iFilesToCancel = 0;
			int iFilesCanPauseOnPreview = 0;
			int iFilesDoPauseOnPreview = 0;
			int iFilesInCats = 0;
			int iFilesToImport = 0;
			UINT uPrioMenuItem = 0;
			const CPartFile *file1 = NULL;

			bool bFirstItem = true;
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				const CtrlItem_Struct* pItemData = reinterpret_cast<CtrlItem_Struct*>(GetItemData(GetNextSelectedItem(pos)));
				if (pItemData == NULL || pItemData->type != FILE_TYPE)
					continue;
				const CPartFile *pFile = static_cast<CPartFile*>(pItemData->value);
				++iSelectedItems;

				iFilesToCancel += static_cast<int>(pFile->GetStatus() != PS_COMPLETING);
				iFilesNotDone += static_cast<int>((pFile->GetStatus() != PS_COMPLETE && pFile->GetStatus() != PS_COMPLETING));
				iFilesToStop += static_cast<int>(pFile->CanStopFile());
				iFilesToPause += static_cast<int>(pFile->CanPauseFile());
				iFilesToResume += static_cast<int>(pFile->CanResumeFile());
				iFilesToOpen += static_cast<int>(pFile->CanOpenFile());
				iFilesGetPreviewParts += static_cast<int>(pFile->GetPreviewPrio());
				iFilesPreviewType += static_cast<int>(pFile->IsPreviewableFileType());
				iFilesToPreview += static_cast<int>(pFile->IsReadyForPreview());
				iFilesCanPauseOnPreview += static_cast<int>(pFile->IsPreviewableFileType() && !pFile->IsReadyForPreview() && pFile->CanPauseFile());
				iFilesDoPauseOnPreview += static_cast<int>(pFile->IsPausingOnPreview());
				iFilesInCats += static_cast<int>(!pFile->HasDefaultCategory());
				iFilesToImport += static_cast<int>(pFile->GetFileOp() == PFOP_IMPORTPARTS);

				UINT uCurPrioMenuItem;
				if (pFile->IsAutoDownPriority())
					uCurPrioMenuItem = MP_PRIOAUTO;
				else
					switch (pFile->GetDownPriority()) {
					case PR_HIGH:
						uCurPrioMenuItem = MP_PRIOHIGH;
						break;
					case PR_NORMAL:
						uCurPrioMenuItem = MP_PRIONORMAL;
						break;
					case PR_LOW:
						uCurPrioMenuItem = MP_PRIOLOW;
						break;
					default:
						uCurPrioMenuItem = 0;
						ASSERT(0);
					}

				if (bFirstItem) {
					bFirstItem = false;
					file1 = pFile;
					uPrioMenuItem = uCurPrioMenuItem;
				} else if (uPrioMenuItem != uCurPrioMenuItem)
					uPrioMenuItem = 0;
			}

			m_FileMenu.EnableMenuItem((UINT)m_PrioMenu.m_hMenu, iFilesNotDone > 0 ? MF_ENABLED : MF_GRAYED);
			m_PrioMenu.CheckMenuRadioItem(MP_PRIOLOW, MP_PRIOAUTO, uPrioMenuItem, 0);

			// enable commands if there is at least one item which can be used for the action
			m_FileMenu.EnableMenuItem(MP_CANCEL, iFilesToCancel > 0 ? MF_ENABLED : MF_GRAYED);
			m_FileMenu.EnableMenuItem(MP_CANCEL_FORGET, iFilesToCancel > 0 ? MF_ENABLED : MF_GRAYED);
			m_FileMenu.EnableMenuItem(MP_STOP, iFilesToStop > 0 ? MF_ENABLED : MF_GRAYED);
			m_FileMenu.EnableMenuItem(MP_PAUSE, iFilesToPause > 0 ? MF_ENABLED : MF_GRAYED);
			m_FileMenu.EnableMenuItem(MP_RESUME, iFilesToResume > 0 ? MF_ENABLED : MF_GRAYED);

			bool bOpenEnabled = (iSelectedItems == 1 && iFilesToOpen == 1);
			m_FileMenu.EnableMenuItem(MP_OPEN, bOpenEnabled ? MF_ENABLED : MF_GRAYED);

			RebuildPreviewMenu(m_PreviewMenu, (iSelectedItems == 1) ? file1 : NULL, iSelectedItems == 1 && iFilesToPreview == 1, iFilesCanPauseOnPreview > 0, iSelectedItems > 0 && iFilesDoPauseOnPreview == iSelectedItems, iSelectedItems == 1 && iFilesPreviewType == 1 && iFilesToPreview == 0 && iFilesNotDone == 1, iSelectedItems == 1 && iFilesGetPreviewParts == 1);
			m_FileMenu.EnableMenuItem((UINT)m_PreviewMenu.m_hMenu, m_PreviewMenu.HasEnabledItems() ? MF_ENABLED : MF_GRAYED);

			bool bDetailsEnabled = (iSelectedItems > 0);
			m_FileMenu.EnableMenuItem(MP_METINFO, bDetailsEnabled ? MF_ENABLED : MF_GRAYED);
			if (thePrefs.IsDoubleClickEnabled() && bOpenEnabled)
				m_FileMenu.SetDefaultItem(MP_OPEN);
			else if (!thePrefs.IsDoubleClickEnabled() && bDetailsEnabled)
				m_FileMenu.SetDefaultItem(MP_METINFO);
			else
				m_FileMenu.SetDefaultItem(UINT_MAX);
			m_FileMenu.EnableMenuItem(MP_VIEWFILECOMMENTS, (iSelectedItems >= 1 /*&& iFilesNotDone == 1*/) ? MF_ENABLED : MF_GRAYED);
			if (thePrefs.m_bImportParts) {
				m_FileMenu.RemoveMenu(MP_IMPORTPARTS, MF_BYCOMMAND);
				m_FileMenu.InsertMenu(MP_IMPORTPARTS, MF_STRING | MF_BYPOSITION, MP_IMPORTPARTS, (iFilesToImport > 0) ? GetResString(_T("IMPORTPARTS_STOP")) : GetResString(_T("IMPORTPARTS")), _T("FILEIMPORTPARTS"));
				m_FileMenu.EnableMenuItem(MP_IMPORTPARTS, (thePrefs.m_bImportParts && iSelectedItems == 1 && iFilesNotDone == 1) ? MF_ENABLED : MF_GRAYED);
			}

			int total;
			m_FileMenu.EnableMenuItem(MP_CLEARCOMPLETED, GetCompleteDownloads(curTab, total) > 0 ? MF_ENABLED : MF_GRAYED);
			if (thePrefs.IsExtControlsEnabled()) {
				m_FileMenu.EnableMenuItem((UINT)m_SourcesMenu.m_hMenu, MF_ENABLED);
				m_SourcesMenu.EnableMenuItem(MP_ADDSOURCE, (iSelectedItems == 1 && iFilesToStop == 1) ? MF_ENABLED : MF_GRAYED);
				m_SourcesMenu.EnableMenuItem(MP_SETSOURCELIMIT, (iFilesNotDone == iSelectedItems) ? MF_ENABLED : MF_GRAYED);
			}

			m_FileMenu.EnableMenuItem(MP_CUT, iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED);
			m_FileMenu.EnableMenuItem(thePrefs.GetShowCopyEd2kLinkCmd() ? MP_GETED2KLINK : MP_SHOWED2KLINK, iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED);
			m_FileMenu.EnableMenuItem(MP_PASTE, theApp.IsEd2kFileLinkInClipboard() ? MF_ENABLED : MF_GRAYED);
			m_FileMenu.EnableMenuItem(MP_FIND, GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED);
			m_FileMenu.EnableMenuItem(MP_SEARCHRELATED, theApp.emuledlg->searchwnd->CanSearchRelatedFiles() ? MF_ENABLED : MF_GRAYED);

			CMenuXP WebMenu;
			WebMenu.CreateMenu();
			int iWebMenuEntries = theWebServices.GetFileMenuEntries(&WebMenu);
			UINT flag = (iWebMenuEntries == 0 || iSelectedItems == 0) ? MF_GRAYED : MF_ENABLED;
			m_FileMenu.AppendMenu(MF_POPUP | flag, (UINT_PTR)WebMenu.m_hMenu, GetResString(_T("WEBSERVICES")), _T("WEB"));

			// create cat-submenu
			CMenuXP CatsMenu;
			CatsMenu.CreateMenu();
			FillCatsMenu(CatsMenu, iFilesInCats);
			m_FileMenu.AppendMenu(MF_POPUP, (UINT_PTR)CatsMenu.m_hMenu, GetResString(_T("TOCAT")), _T("CATEGORY"));

			bool bToolbarItem = !thePrefs.IsDownloadToolbarEnabled();
			if (bToolbarItem) {
				m_FileMenu.AppendMenu(MF_SEPARATOR);
				m_FileMenu.AppendMenu(MF_STRING, MP_TOGGLEDTOOLBAR, GetResString(_T("SHOWTOOLBAR")));
			}

			GetPopupMenuPos(*this, point);
			m_FileMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
			if (bToolbarItem) {
				VERIFY(m_FileMenu.RemoveMenu(m_FileMenu.GetMenuItemCount() - 1, MF_BYPOSITION));
				VERIFY(m_FileMenu.RemoveMenu(m_FileMenu.GetMenuItemCount() - 1, MF_BYPOSITION));
			}
			VERIFY(m_FileMenu.RemoveMenu(m_FileMenu.GetMenuItemCount() - 1, MF_BYPOSITION));
			VERIFY(m_FileMenu.RemoveMenu(m_FileMenu.GetMenuItemCount() - 1, MF_BYPOSITION));
			VERIFY(WebMenu.DestroyMenu());
			VERIFY(CatsMenu.DestroyMenu());
		} else {
			const CUpDownClient *client = (content != NULL) ? static_cast<CUpDownClient*>(content->value) : NULL;
			const bool is_ed2k = client && client->IsEd2kClient();
			CMenuXP ClientMenu;
			ClientMenu.CreatePopupMenu();
			ClientMenu.AddMenuSidebar(GetResString(_T("CLIENTS")));
			ClientMenu.AppendMenu(MF_STRING, MP_DETAIL, GetResString(_T("SHOWDETAILS")), _T("CLIENTDETAILS"));
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
			CMenuXP A4AFMenu;
			A4AFMenu.CreateMenu();
			if (thePrefs.IsExtControlsEnabled()) {
#ifdef _DEBUG
				if (content && content->type == UNAVAILABLE_SOURCE)
					A4AFMenu.AppendMenu(MF_STRING, MP_A4AF_CHECK_THIS_NOW, GetResString(_T("A4AF_CHECK_THIS_NOW")));
# endif
				if (A4AFMenu.GetMenuItemCount() > 0)
					ClientMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)A4AFMenu.m_hMenu, GetResString(_T("A4AF")));
			}

			GetPopupMenuPos(*this, point);
			ClientMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);

			VERIFY(A4AFMenu.DestroyMenu());
			VERIFY(ClientMenu.DestroyMenu());
		}
	} else { // nothing selected
		int total;
		m_FileMenu.EnableMenuItem((UINT)m_PrioMenu.m_hMenu, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_CANCEL, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_CANCEL_FORGET, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_PAUSE, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_STOP, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_RESUME, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_OPEN, MF_GRAYED);

		RebuildPreviewMenu(m_PreviewMenu, NULL, false, false, false, false, false);
		m_FileMenu.EnableMenuItem((UINT)m_PreviewMenu.m_hMenu, MF_GRAYED);

		m_FileMenu.EnableMenuItem(MP_METINFO, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_VIEWFILECOMMENTS, MF_GRAYED);
		if (thePrefs.m_bImportParts)
			m_FileMenu.EnableMenuItem(MP_IMPORTPARTS, MF_GRAYED);

		m_FileMenu.EnableMenuItem(MP_CLEARCOMPLETED, GetCompleteDownloads(curTab, total) > 0 ? MF_ENABLED : MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_CUT, MF_GRAYED);
		m_FileMenu.EnableMenuItem(thePrefs.GetShowCopyEd2kLinkCmd() ? MP_GETED2KLINK : MP_SHOWED2KLINK, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_PASTE, theApp.IsEd2kFileLinkInClipboard() ? MF_ENABLED : MF_GRAYED);
		m_FileMenu.SetDefaultItem(UINT_MAX);
		if (m_SourcesMenu)
			m_FileMenu.EnableMenuItem((UINT)m_SourcesMenu.m_hMenu, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_SEARCHRELATED, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_FIND, GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED);

		// also show the "Web Services" entry, even if its disabled and therefore not usable, it though looks a little
		// less confusing this way.
		CMenuXP WebMenu;
		WebMenu.CreateMenu();
		theWebServices.GetFileMenuEntries(&WebMenu);
		m_FileMenu.AppendMenu(MF_POPUP | MF_GRAYED, (UINT_PTR)WebMenu.m_hMenu, GetResString(_T("WEBSERVICES")), _T("WEB"));

		bool bToolbarItem = !thePrefs.IsDownloadToolbarEnabled();
		if (bToolbarItem) {
			m_FileMenu.AppendMenu(MF_SEPARATOR);
			m_FileMenu.AppendMenu(MF_STRING, MP_TOGGLEDTOOLBAR, GetResString(_T("SHOWTOOLBAR")));
		}

		GetPopupMenuPos(*this, point);
		m_FileMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
		if (bToolbarItem) {
			VERIFY(m_FileMenu.RemoveMenu(m_FileMenu.GetMenuItemCount() - 1, MF_BYPOSITION));
			VERIFY(m_FileMenu.RemoveMenu(m_FileMenu.GetMenuItemCount() - 1, MF_BYPOSITION));
		}
		m_FileMenu.RemoveMenu(m_FileMenu.GetMenuItemCount() - 1, MF_BYPOSITION);
		VERIFY(WebMenu.DestroyMenu());
	}
}

void CDownloadListCtrl::FillCatsMenu(CMenuXP&rCatsMenu, int iFilesInCats)
{
	ASSERT(rCatsMenu.m_hMenu);
	if (iFilesInCats == -1) {
		iFilesInCats = 0;
		int iSel = GetNextItem(-1, LVIS_SELECTED);
		if (iSel >= 0) {
			const CtrlItem_Struct* content = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iSel));
			if (content != NULL && content->type == FILE_TYPE)
				for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
					const CtrlItem_Struct* pItemData = reinterpret_cast<CtrlItem_Struct*>(GetItemData(GetNextSelectedItem(pos)));
					if (pItemData != NULL && pItemData->type == FILE_TYPE) {
						const CPartFile *pFile = static_cast<CPartFile*>(pItemData->value);
						iFilesInCats += static_cast<int>((!pFile->HasDefaultCategory()));
					}
				}
		}
	}
	rCatsMenu.AppendMenu(MF_STRING, MP_NEWCAT, GetResString(_T("NEW")) + _T("..."));
	CString label(GetResString(_T("CAT_UNASSIGN")));
	label.Remove('(');
	label.Remove(')'); // Remove braces without having to put a new/changed resource string in
	rCatsMenu.AppendMenu(MF_STRING | ((iFilesInCats == 0) ? MF_GRAYED : MF_ENABLED), MP_ASSIGNCAT, label);
	if (thePrefs.GetCatCount() > 1) {
		rCatsMenu.AppendMenu(MF_SEPARATOR);
		for (INT_PTR i = 1; i < thePrefs.GetCatCount(); ++i) {
			label = thePrefs.GetCategory(i)->strTitle;
			DupAmpersand(label);
			rCatsMenu.AppendMenu(MF_STRING, MP_ASSIGNCAT + i, label);
		}
	}
}

CMenuXP* CDownloadListCtrl::GetPrioMenu()
{
	UINT uPrioMenuItem = 0;
	int iSel = GetNextItem(-1, LVIS_SELECTED);
	if (iSel >= 0) {
		const CtrlItem_Struct * content = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iSel));
		if (content != NULL && content->type == FILE_TYPE) {
			bool bFirstItem = true;
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				const CtrlItem_Struct* pItemData = reinterpret_cast<CtrlItem_Struct*>(GetItemData(GetNextSelectedItem(pos)));
				if (pItemData == NULL || pItemData->type != FILE_TYPE)
					continue;
				const CPartFile *pFile = static_cast<CPartFile*>(pItemData->value);
				UINT uCurPrioMenuItem;
				if (pFile->IsAutoDownPriority())
					uCurPrioMenuItem = MP_PRIOAUTO;
				else if (pFile->GetDownPriority() == PR_HIGH)
					uCurPrioMenuItem = MP_PRIOHIGH;
				else if (pFile->GetDownPriority() == PR_NORMAL)
					uCurPrioMenuItem = MP_PRIONORMAL;
				else if (pFile->GetDownPriority() == PR_LOW)
					uCurPrioMenuItem = MP_PRIOLOW;
				else {
					uCurPrioMenuItem = 0;
					ASSERT(0);
				}
				if (bFirstItem)
					uPrioMenuItem = uCurPrioMenuItem;
				else if (uPrioMenuItem != uCurPrioMenuItem) {
					uPrioMenuItem = 0;
					break;
				}
				bFirstItem = false;
			}
		}
	}
	m_PrioMenu.CheckMenuRadioItem(MP_PRIOLOW, MP_PRIOAUTO, uPrioMenuItem, 0);
	return &m_PrioMenu;
}

BOOL CDownloadListCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	wParam = LOWORD(wParam);

	switch (wParam) {
	case MP_PASTE:
		if (theApp.IsEd2kFileLinkInClipboard())
			theApp.PasteClipboard(curTab);
		return TRUE;
	case MP_FIND:
		OnFindStart();
		return TRUE;
	case MP_TOGGLEDTOOLBAR:
		thePrefs.SetDownloadToolbar(true);
		theApp.emuledlg->transferwnd->ShowToolbar(true);
		return TRUE;
	}

	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	if (iSel < 0)
		iSel = GetNextItem(-1, LVIS_SELECTED);
	if (iSel >= 0) {
		const CtrlItem_Struct* content = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iSel));
		if (content != NULL && content->type == FILE_TYPE) {
			//for multiple selections
			unsigned selectedCount = 0;
			CTypedPtrList<CPtrList, CPartFile*> selectedList;
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				int index = GetNextSelectedItem(pos);
				if (index > -1 && reinterpret_cast<CtrlItem_Struct*>(GetItemData(index))->type == FILE_TYPE) {
					++selectedCount;
					selectedList.AddTail(static_cast<CPartFile*>(reinterpret_cast<CtrlItem_Struct*>(GetItemData(index))->value));
				}
			}

			CPartFile *file = static_cast<CPartFile*>(content->value);
			bool m_bAddToCanceledMet = true;

			switch (wParam) {
			case MP_CANCEL_FORGET:
				m_bAddToCanceledMet = false;
			case MP_CANCEL:
			case MPG_DELETE: // keyboard del will continue to remove completed files from the screen while cancel will now also be available for complete files
				if (selectedCount > 0) {
					SetRedraw(false);
					CString fileList(GetResString(selectedCount == 1 ? _T("Q_CANCELDL2") : _T("Q_CANCELDL")));
					bool validdelete = false;
					bool removecompl = false;
					int cFiles = 0;
					const int iMaxDisplayFiles = 10;
					for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
						const CPartFile *cur_file = selectedList.GetNext(pos);
						if (cur_file == NULL)
							continue;
						if (cur_file->GetStatus() != PS_COMPLETING && (cur_file->GetStatus() != PS_COMPLETE || wParam == MP_CANCEL || wParam == MP_CANCEL_FORGET)) {
							validdelete = true;
							if (++cFiles < iMaxDisplayFiles)
								fileList.AppendFormat(_T("\n%s"), (LPCTSTR)cur_file->GetFileName());
							else if (cFiles == iMaxDisplayFiles && pos != NULL)
								fileList += _T("\n...");
						} else if (cur_file->GetStatus() == PS_COMPLETE)
							removecompl = true;
					}

					if ((removecompl && !validdelete) || (validdelete && CDarkMode::MessageBox(fileList, MB_DEFBUTTON2 | MB_ICONQUESTION | MB_YESNO) == IDYES)) {
						bool bRemovedItems = !selectedList.IsEmpty();
						while (!selectedList.IsEmpty()) {
							CPartFile *partfile = selectedList.RemoveHead();
							if (partfile == NULL)
								continue;
							HideSources(partfile);
							switch (partfile->GetStatus()) {
							case PS_WAITINGFORHASH:
							case PS_HASHING:
							case PS_COMPLETING:
								break;
							case PS_COMPLETE:
								if (wParam == MP_CANCEL || wParam == MP_CANCEL_FORGET) {
									bool delsucc = ShellDeleteFile(partfile->GetFilePath());
									if (delsucc)
										theApp.sharedfiles->RemoveFile(partfile, true);
									else {
										CString strError;
										strError.Format(GetResString(_T("ERR_DELFILE")) + _T("\r\n\r\n%s"), (LPCTSTR)partfile->GetFilePath(), (LPCTSTR)GetErrorMessage(::GetLastError()));
										CDarkMode::MessageBox(strError);
									}
								}
								RemoveFile(partfile);
								break;
							default:
								if (partfile->GetCategory())
									theApp.downloadqueue->StartNextFileIfPrefs(partfile->GetCategory());
							case PS_PAUSED:
								partfile->DeletePartFile(m_bAddToCanceledMet);
							}
						}
						if (bRemovedItems) {
							AutoSelectItem();
							theApp.emuledlg->transferwnd->UpdateCatTabTitles();
						}
					}
					SetRedraw(true);
				}
				break;
			case MP_PRIOHIGH:
				SetRedraw(false);
				while (!selectedList.IsEmpty()) {
					CPartFile *partfile = selectedList.RemoveHead();
					partfile->SetAutoDownPriority(false);
					partfile->SetDownPriority(PR_HIGH);
				}
				SetRedraw(true);
				break;
			case MP_PRIOLOW:
				SetRedraw(false);
				while (!selectedList.IsEmpty()) {
					CPartFile *partfile = selectedList.RemoveHead();
					partfile->SetAutoDownPriority(false);
					partfile->SetDownPriority(PR_LOW);
				}
				SetRedraw(true);
				break;
			case MP_PRIONORMAL:
				SetRedraw(false);
				while (!selectedList.IsEmpty()) {
					CPartFile *partfile = selectedList.RemoveHead();
					partfile->SetAutoDownPriority(false);
					partfile->SetDownPriority(PR_NORMAL);
				}
				SetRedraw(true);
				break;
			case MP_PRIOAUTO:
				SetRedraw(false);
				while (!selectedList.IsEmpty()) {
					CPartFile *partfile = selectedList.RemoveHead();
					partfile->SetAutoDownPriority(true);
					partfile->SetDownPriority(PR_HIGH);
				}
				SetRedraw(true);
				break;
			case MP_PAUSE:
				SetRedraw(false);
				while (!selectedList.IsEmpty()) {
					CPartFile *partfile = selectedList.RemoveHead();
					if (partfile->CanPauseFile())
						partfile->PauseFile();
				}
				SetRedraw(true);
				break;
			case MP_RESUME:
				SetRedraw(false);
				while (!selectedList.IsEmpty()) {
					CPartFile *partfile = selectedList.RemoveHead();
					if (partfile->CanResumeFile())
						if (partfile->GetStatus() == PS_INSUFFICIENT)
							partfile->ResumeFileInsufficient();
						else
							partfile->ResumeFile();
				}
				SetRedraw(true);
				break;
			case MP_STOP:
				SetRedraw(false);
				while (!selectedList.IsEmpty()) {
					CPartFile *partfile = selectedList.RemoveHead();
					if (partfile->CanStopFile()) {
						HideSources(partfile);
						partfile->StopFile(false);
					}
				}
				SetRedraw(true);
				theApp.emuledlg->transferwnd->UpdateCatTabTitles();
				break;
			case MP_CLEARCOMPLETED:
				SetRedraw(false);
				ClearCompleted();
				SetRedraw(true);
				break;
			case MPG_F2:
				if (GetKeyState(VK_CONTROL) < 0 || selectedCount > 1) {
					// when ctrl is pressed -> filename cleanup
					if (IDYES == LocMessageBox(_T("MANUAL_FILENAMECLEANUP"), MB_YESNO, 0))
						while (!selectedList.IsEmpty()) {
							CPartFile *partfile = selectedList.RemoveHead();
							if (partfile->IsPartFile()) {
								HideSources(partfile);
								partfile->SetFileName(CleanupFilename(partfile->GetFileName()));
							}
						}
				} else {
					if (file->GetStatus() != PS_COMPLETE && file->GetStatus() != PS_COMPLETING) {
						InputBox inputbox;
						inputbox.SetLabels(GetResNoAmp(_T("RENAME")), GetResString(_T("DL_FILENAME")), file->GetFileName());
						inputbox.SetEditFilenameMode();
						if (inputbox.DoModal() == IDOK && !inputbox.GetInput().IsEmpty() && IsValidEd2kString(inputbox.GetInput())) {
							HideSources(file);
							file->SetFileName(inputbox.GetInput(), true);
							file->UpdateDisplayedInfo();
							file->SavePartFile();
						}
					} else
						MessageBeep(MB_OK);
				}
				break;
			case MP_METINFO:
			case MPG_ALTENTER:
				ShowFileDialog(0);
				break;
			case MP_COPYSELECTED:
			case MP_GETED2KLINK:
				{
					CString str;
					while (!selectedList.IsEmpty()) {
						const CAbstractFile *af = static_cast<CAbstractFile*>(selectedList.RemoveHead());
						if (af) {
							if (!str.IsEmpty())
								str += _T("\r\n");
							str += af->GetED2kLink();
						}
					}

					if (!str.IsEmpty()) {
						theApp.CopyTextToClipboard(str);
						theApp.emuledlg->statusbar->SetText(GetResString(_T("ED2K_LINK_COPIED_TO_CLIPBOARD")), SBarLog, 0);
					}
				}
				break;
			case MP_CUT:
			{
				CString m_strFileNames;
				while (!selectedList.IsEmpty()) {
					const CAbstractFile* pFile = static_cast<CAbstractFile*>(selectedList.RemoveHead());
					if (pFile) {
						if (!m_strFileNames.IsEmpty())
							m_strFileNames += _T("\r\n");
						m_strFileNames += pFile->GetFileName();
					}
				}

				if (!m_strFileNames.IsEmpty()) {
					theApp.CopyTextToClipboard(m_strFileNames);
					theApp.emuledlg->statusbar->SetText(GetResString(_T("FILE_NAME_COPIED_TO_CLIPBOARD")), SBarLog, 0);
				}
			}
			break;
			case MP_SEARCHRELATED:
				theApp.emuledlg->searchwnd->SearchRelatedFiles(selectedList);
				theApp.emuledlg->SetActiveDialog(theApp.emuledlg->searchwnd);
				break;
			case MP_OPEN:
			case IDA_ENTER:
				if (selectedCount == 1 && file->CanOpenFile())
					file->OpenFile();
				break;
			case MP_OPENFOLDER:
				if (selectedCount == 1)
					ShellOpenFile(file->GetPath());
				break;
			case MP_TRY_TO_GET_PREVIEW_PARTS:
				if (selectedCount == 1)
					file->SetPreviewPrio(!file->GetPreviewPrio());
				break;
			case MP_PREVIEW:
				if (selectedCount != 1 || !PlayNextPreviewableFile())
					file->PreviewFile();
				break;
			case MP_PREVIEW1:
				if (selectedCount != 1 || !PlayNextPreviewableFile(0))
					file->PreviewFile(0);
				break;
			case MP_PREVIEW2:
				if (selectedCount != 1 || !PlayNextPreviewableFile(1))
					file->PreviewFile(1);
				break;
			case MP_PREVIEW3:
				if (selectedCount != 1 || !PlayNextPreviewableFile(2))
					file->PreviewFile(2);
				break;
			case MP_PREVIEW4:
				if (selectedCount != 1 || !PlayNextPreviewableFile(3))
					file->PreviewFile(3);
				break;
			case MP_PREVIEW5:
				if (selectedCount != 1 || !PlayNextPreviewableFile(4))
					file->PreviewFile(4);
				break;
			case MP_PREVIEW6:
				if (selectedCount != 1 || !PlayNextPreviewableFile(5))
					file->PreviewFile(5);
				break;
			case MP_PREVIEW7:
				if (selectedCount != 1 || !PlayNextPreviewableFile(6))
					file->PreviewFile(6);
				break;
			case MP_PREVIEW8:
				if (selectedCount != 1 || !PlayNextPreviewableFile(7))
					file->PreviewFile(7);
				break;
			case MP_PREVIEW9:
				if (selectedCount != 1 || !PlayNextPreviewableFile(8))
					file->PreviewFile(8);
				break;
			case MP_PREVIEW10:
				if (selectedCount != 1 || !PlayNextPreviewableFile(9))
					file->PreviewFile(9);
				break;
			case MP_SAVEAPPSTATE:
				theApp.uploadqueue->SaveAppState(false);
				break;
			case MP_RELOADCONF:
				thePrefs.LoadBlacklistFile(); // Loads blacklist.conf
				theApp.shield->LoadShieldFile(); // Loads shield.conf
				break;
			case MP_BACKUP:
				theApp.Backup(false);
				break;
			case MP_FILEINSPECTOR:
				theApp.emuledlg->transferwnd->GetDownloadList()->FileInspector(true);
				break;
			case MP_PAUSEONPREVIEW:
				{
					bool bAllPausedOnPreview = true;
					for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL && bAllPausedOnPreview;)
						bAllPausedOnPreview = static_cast<CPartFile*>(selectedList.GetNext(pos))->IsPausingOnPreview();
					while (!selectedList.IsEmpty()) {
						CPartFile *pPartFile = selectedList.RemoveHead();
						if (pPartFile->IsPreviewableFileType() && !pPartFile->IsReadyForPreview())
							pPartFile->SetPauseOnPreview(!bAllPausedOnPreview);
					}
				}
				break;
			case MP_VIEWFILECOMMENTS:
				ShowFileDialog(IDD_COMMENTLST);
				break;
			case MP_IMPORTPARTS:
				if (!file->m_bMD4HashsetNeeded) //log "no hashset"?
					file->ImportParts();
				break;
			case MP_SHOWED2KLINK:
				ShowFileDialog(IDD_ED2KLINK);
				break;
			case MP_SETSOURCELIMIT:
				{
					CString temp;
					temp.Format(_T("%u"), file->GetPrivateMaxSources());
					InputBox inputbox;
					const CString &title(GetResString(_T("SETPFSLIMIT")));
					inputbox.SetLabels(title, GetResString(_T("SETPFSLIMITEXPLAINED")), temp);

					if (inputbox.DoModal() == IDOK) {
						int newlimit = _tstoi(inputbox.GetInput());
						while (!selectedList.IsEmpty()) {
							CPartFile *partfile = selectedList.RemoveHead();
							partfile->SetPrivateMaxSources(newlimit);
							partfile->UpdateDisplayedInfo(true);
						}
					}
				}
				break;
			case MP_ADDSOURCE:
				if (selectedCount == 1) {
					CAddSourceDlg as;
					as.SetFile(file);
					as.DoModal();
				}
				break;
			default:
				if (wParam >= MP_WEBURL && wParam <= MP_WEBURL + 99)
				{
					while (!selectedList.IsEmpty()) {
						const CAbstractFile* pFile = static_cast<CAbstractFile*>(selectedList.RemoveHead());
						if (pFile)
							theWebServices.RunURL(pFile, (UINT)wParam);
					}
				}
				else if ((wParam >= MP_ASSIGNCAT && wParam <= MP_ASSIGNCAT + 99) || wParam == MP_NEWCAT) {
					int nCatNumber;
					if (wParam == MP_NEWCAT) {
						nCatNumber = theApp.emuledlg->transferwnd->AddCategoryInteractive();
						if (nCatNumber == 0) // Creation canceled
							break;
					} else
						nCatNumber = (int)(wParam - MP_ASSIGNCAT);
					SetRedraw(false);
					while (!selectedList.IsEmpty()) {
						CPartFile *partfile = selectedList.RemoveHead();
						partfile->SetCategory(nCatNumber);
						partfile->UpdateDisplayedInfo(true);
					}
					SetRedraw(true);
					UpdateCurrentCategoryView();
					if (thePrefs.ShowCatTabInfos())
						theApp.emuledlg->transferwnd->UpdateCatTabTitles();
				} else if (wParam >= MP_PREVIEW_APP_MIN && wParam <= MP_PREVIEW_APP_MAX)
					thePreviewApps.RunApp(file, (UINT)wParam);
			}
		} else if (content != NULL) {
			CUpDownClient *client = static_cast<CUpDownClient*>(content->value);

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
					UpdateItem(client);
				break;
			case MP_DETAIL:
			case MPG_ALTENTER:
				ShowClientDialog(client);
				break;
			case MP_PUNISMENT_IPUSERHASHBAN:
				theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_IP_BAN")), PR_MANUAL, P_IPUSERHASHBAN);
				break;
			case MP_PUNISMENT_USERHASHBAN:
				theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_USER_HASH_BAN")), PR_MANUAL, P_USERHASHBAN);
				break;
			case MP_PUNISMENT_UPLOADBAN:
				theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_UPLOAD_BAN")), PR_MANUAL, P_UPLOADBAN);
				break;
			case MP_PUNISMENT_SCOREX01:
				theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX01);
				break;
			case MP_PUNISMENT_SCOREX02:
				theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX02);
				break;
			case MP_PUNISMENT_SCOREX03:
				theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX03);
				break;
			case MP_PUNISMENT_SCOREX04:
				theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX04);
				break;
			case MP_PUNISMENT_SCOREX05:
				theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX05);
				break;
			case MP_PUNISMENT_SCOREX06:
				theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX06);
				break;
			case MP_PUNISMENT_SCOREX07:
				theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX07);
				break;
			case MP_PUNISMENT_SCOREX08:
				theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX08);
				break;
			case MP_PUNISMENT_SCOREX09:
				theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX09);
				break;
			case MP_PUNISMENT_NONE:
				theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_CANCELATION")), PR_MANUAL, P_NOPUNISHMENT);
				break;
			case MP_BOOT:
				if (client->GetKadPort() && client->GetKadVersion() >= KADEMLIA_VERSION2_47a)
					Kademlia::CKademlia::Bootstrap(client->GetIPv4().ToUInt32(true), client->GetKadPort());
#ifdef _DEBUG
				break;
			case MP_A4AF_CHECK_THIS_NOW:
				{
					CPartFile *file = static_cast<CPartFile*>(content->owner);
					if (file->GetStatus(false) == PS_READY || file->GetStatus(false) == PS_EMPTY) {
						if (client->GetDownloadState() != DS_DOWNLOADING) {
							client->SwapToAnotherFile(_T("Manual init of source check. Test to be like ProcessA4AFClients(). CDownloadListCtrl::OnCommand() MP_SWAP_A4AF_DEBUG_THIS"), false, false, false, NULL, true, true, true); // ZZ:DownloadManager
							UpdateItem(file);
						}
					}
				}
#endif
			}
		}
	} else if (wParam == MP_CLEARCOMPLETED) // nothing selected
		ClearCompleted();
	else if (wParam == MP_SAVEAPPSTATE) // nothing selected
		theApp.uploadqueue->SaveAppState(false);
	else if (wParam == MP_RELOADCONF) { // nothing selected
		thePrefs.LoadBlacklistFile(); // Loads blacklist.conf
		theApp.shield->LoadShieldFile(); // Loads shield.conf
	}
	else if (wParam == MP_BACKUP) // nothing selected
		theApp.Backup(false);
	else if (wParam == MP_FILEINSPECTOR) // nothing selected
		theApp.emuledlg->transferwnd->GetDownloadList()->FileInspector(true);

	m_availableCommandsDirty = true;
	return TRUE;
}

const bool CDownloadListListCtrlItemWalk::PlayNextPreviewableFile(const int iAppIndex)
{
	if (m_pDownloadListCtrl == NULL || !m_pDownloadListCtrl->m_bRightClicked)
		return false;

	m_pDownloadListCtrl->m_bRightClicked = false;

	int iItemCount = m_pDownloadListCtrl->GetItemCount();
	if (iItemCount >= 2) {
		POSITION pos = m_pDownloadListCtrl->GetFirstSelectedItemPosition();
		if (pos) {
			int iItem = m_pDownloadListCtrl->GetNextSelectedItem(pos);
			int iCurSelItem = iItem;
			while (++iItem < iItemCount) {
				const CtrlItem_Struct* ctrl_item = reinterpret_cast<CtrlItem_Struct*>(m_pDownloadListCtrl->GetItemData(iItem));
				if (ctrl_item != NULL && ctrl_item->type == FILE_TYPE) {
					CPartFile* pPartFile = static_cast<CPartFile*>(reinterpret_cast<CtrlItem_Struct*>(m_pDownloadListCtrl->GetItemData(iItem))->value);
					if (!pPartFile->IsReadyForPreview())
						continue;
					m_pDownloadListCtrl->SetItemState(iCurSelItem, 0, LVIS_SELECTED | LVIS_FOCUSED);
					m_pDownloadListCtrl->SetItemState(iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
					m_pDownloadListCtrl->SetSelectionMark(iItem);
					m_pDownloadListCtrl->EnsureVisible(iItem, FALSE);
					pPartFile->PreviewFile(iAppIndex);
					return true;
				}
			}
		}
	}
	return false;
}

void CDownloadListCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	bool sortAscending;
	if (GetSortItem() != pNMLV->iSubItem)
		switch (pNMLV->iSubItem) {
		case 2: // Transferred
		case 3: // Completed
		case 4: // Download rate
		case 5: // Progress
		case 6: // Sources / Client Software
			sortAscending = false;
			break;
		case 9:
			// Keep the current 'm_bRemainSort' for that column, but reset to 'ascending'
		default:
			sortAscending = true;
		}
	else
		sortAscending = !GetSortAscending();

	// Ornis 4-way-sorting
	int adder = 0;
	if (pNMLV->iSubItem == 9) {
		if (GetSortItem() == 9 && sortAscending) // check for 'ascending' because the initial sort order is also 'ascending'
			m_bRemainSort = !m_bRemainSort;
		if (m_bRemainSort)
			adder = 81;
	}
	// Sort table
	if (adder == 0)
		SetSortArrow(pNMLV->iSubItem, sortAscending);
	else
		SetSortArrow(pNMLV->iSubItem, sortAscending ? arrowDoubleUp : arrowDoubleDown);
	UpdateSortHistory(MAKELONG(pNMLV->iSubItem + adder, !sortAscending));
	// Although SortItems will not sort anything since this is a virtual list, it will save sort parameter
	// history in m_liSortHistory which will be used when we call GetNextSortOrder.
	m_pSortParam = MAKELONG(pNMLV->iSubItem + adder, !sortAscending);
	SortItems(SortProc, m_pSortParam);
	ReloadList(true, static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL));

	// Save new preferences
	thePrefs.TransferlistRemainSortStyle(m_bRemainSort);
	*pResult = 0;
}

int CALLBACK CDownloadListCtrl::SortProc(const LPARAM lParam1,const LPARAM lParam2, const LPARAM lParamSort)
{
	const CtrlItem_Struct *item1 = reinterpret_cast<CtrlItem_Struct*>(lParam1);
	const CtrlItem_Struct *item2 = reinterpret_cast<CtrlItem_Struct*>(lParam2);

	int iResult;
	if (item1->type == FILE_TYPE && item2->type != FILE_TYPE) {
		if (item1->value == item2->parent->value)
			return -1;
		iResult = Compare(static_cast<CPartFile*>(item1->value), static_cast<CPartFile*>(item2->parent->value), lParamSort);
	} else if (item1->type != FILE_TYPE && item2->type == FILE_TYPE) {
		if (item1->parent->value == item2->value)
			return 1;
		iResult = Compare(static_cast<CPartFile*>(item1->parent->value), static_cast<CPartFile*>(item2->value), lParamSort);
	} else if (item1->type == FILE_TYPE)
		iResult = Compare(static_cast<CPartFile*>(item1->value), static_cast<CPartFile*>(item2->value), lParamSort);
	else {
		if (item1->parent->value != item2->parent->value) {
			iResult = Compare(static_cast<CPartFile*>(item1->parent->value), static_cast<CPartFile*>(item2->parent->value), lParamSort);
			return HIWORD(lParamSort) ? -iResult : iResult;
		}
		if (item1->type != item2->type)
			return item1->type - item2->type;

		iResult = Compare(static_cast<CUpDownClient*>(item1->value), static_cast<CUpDownClient*>(item2->value), lParamSort);
	}

	// SortProc still should be called for virtual lists.
	// Call secondary sort order, if the first one resulted as equal
	if (iResult == 0) {
		LPARAM iNextSort = theApp.emuledlg->transferwnd->GetDownloadList()->GetNextSortOrder(lParamSort);
		if (iNextSort != -1)
			return SortProc(lParam1, lParam2, iNextSort);
	}

	return HIWORD(lParamSort) ? -iResult : iResult;
}

void CDownloadListCtrl::ClearCompleted(int incat)
{
	if (incat == -2)
		incat = curTab;

	// Search for completed file(s)
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end();) {
		const CtrlItem_Struct *cur_item = it->second;
		++it; // Already point to the next iterator.
		if (cur_item->type == FILE_TYPE) {
			CPartFile *file = static_cast<CPartFile*>(cur_item->value);
			if (!file->IsPartFile() && (!IsFilteredOut(file) || incat == -1))
				if (RemoveFile(file))
					it = m_ListItems.begin();
		}
	}

	if (thePrefs.ShowCatTabInfos())
		theApp.emuledlg->transferwnd->UpdateCatTabTitles();
}

void CDownloadListCtrl::ClearCompleted(const CPartFile *pFile)
{
	if (!pFile->IsPartFile())
		for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
			const CtrlItem_Struct *cur_item = it->second;
			if (cur_item->type == FILE_TYPE) {
				CPartFile *pCurFile = static_cast<CPartFile*>(cur_item->value);
				if (pCurFile == pFile) {
					RemoveFile(pCurFile);
					return;
				}
			}
		}
}

void CDownloadListCtrl::SetStyle()
{
	if (thePrefs.IsDoubleClickEnabled())
		SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	else
		SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_ONECLICKACTIVATE);
}

void CDownloadListCtrl::OnListModified(LPNMHDR pNMHDR, LRESULT* /*pResult*/)
{
	NMLISTVIEW *pNMListView = reinterpret_cast<NMLISTVIEW*>(pNMHDR);

	//this works because true is equal to 1 and false equal to 0
	int notLast = static_cast<int>(pNMListView->iItem + 1 != GetItemCount());
	int notFirst = static_cast<int>(pNMListView->iItem != 0);
	RedrawItems(pNMListView->iItem - notFirst, pNMListView->iItem + notLast);
	m_availableCommandsDirty = true;
}

const int CDownloadListCtrl::Compare(const CPartFile *file1, const CPartFile *file2, const LPARAM lParamSort)
{
	switch (LOWORD(lParamSort)) {
	case 0: //filename
		return CompareLocaleStringNoCase(file1->GetFileName(), file2->GetFileName());
	case 1: //size
		return CompareUnsigned(file1->GetFileSize(), file2->GetFileSize());
	case 2: //transferred
		return CompareUnsigned(file1->GetTransferred(), file2->GetTransferred());
	case 3: //completed
		return CompareUnsigned(file1->GetCompletedSize(), file2->GetCompletedSize());
	case 4: //speed
		return CompareUnsigned(file1->GetDatarate(), file2->GetDatarate());
	case 5: //progress
		return sgn((float)file1->GetCompletedSize() / (float)file1->GetFileSize() - (float)file2->GetCompletedSize() / (float)file2->GetFileSize()); //compare exact ratio instead of rounded percents
	case 6: //sources
		return CompareUnsigned(file1->GetSourceCount(), file2->GetSourceCount());
	case 7: //priority
		return CompareUnsigned(file1->GetDownPriority(), file2->GetDownPriority());
	case 8: //Status
		return (file1->getPartfileStatusRank() - file2->getPartfileStatusRank());
	case 9: //Remaining Time
		{
			//Make ascending sort so we can have the smaller remaining time on the top
			//instead of unknowns so we can see which files are about to finish better.
			time_t f1 = file1->getTimeRemaining();
			time_t f2 = file2->getTimeRemaining();
			//Same, do nothing.
			if (f1 == f2)
				break;

			//If descending, put first on top as it is unknown
			//If ascending, put first on bottom as it is unknown
			if (f1 == -1)
				return 1;

			//If descending, put second on top as it is unknown
			//If ascending, put second on bottom as it is unknown
			if (f2 == -1)
				return -1;

			//If descending, put first on top as it is bigger.
			//If ascending, put first on bottom as it is bigger.
			return CompareUnsigned(f1, f2);
		}

	case 90: //Remaining SIZE
		return CompareUnsigned(file1->GetFileSize() - file1->GetCompletedSize(), file2->GetFileSize() - file2->GetCompletedSize());
	case 10: //last seen complete
		return sgn(file1->lastseencomplete - file2->lastseencomplete);
	case 11: //last received Time
		return sgn(file1->GetLastReceptionDate() - file2->GetLastReceptionDate());
	case 12: //category
		//TODO: 'GetCategory' SHOULD be a 'const' function and 'GetResString' should NOT be called.
		return CompareLocaleStringNoCase((const_cast<CPartFile*>(file1)->GetCategory() != 0) ? thePrefs.GetCategory(const_cast<CPartFile*>(file1)->GetCategory())->strTitle : GetResString(_T("ALL")),
			(const_cast<CPartFile*>(file2)->GetCategory() != 0) ? thePrefs.GetCategory(const_cast<CPartFile*>(file2)->GetCategory())->strTitle : GetResString(_T("ALL")));
	case 13: // added on
		return sgn(file1->GetCrFileDate() - file2->GetCrFileDate());
	case 14:
		return 0;
	case 15: // Preview Available (Sort as Preview Available, Category, Size)
		return CompareUnsigned(file1->IsReadyForPreview(), file2->IsReadyForPreview());
	case 16:
		return sgn((file1->GetTransferred() ? file1->GetCompressionGain() * 100.0 / file1->GetTransferred() : 0) - (file2->GetTransferred() ? file2->GetCompressionGain() * 100.0 / file2->GetTransferred() : 0));
	}
	return 0;
}

const int CDownloadListCtrl::Compare(const CUpDownClient *client1, const CUpDownClient *client2, const LPARAM lParamSort)
{
	switch (LOWORD(lParamSort)) {
	case 0: //name
		if (client1->GetUserName() && client2->GetUserName())
			return CompareLocaleStringNoCase(client1->GetUserName(), client2->GetUserName());
		if (client1->GetUserName() == NULL)
			return 1; // place clients with no user names at bottom
		if (client2->GetUserName() == NULL)
			return -1; // place clients with no user names at bottom
		return 0;
	case 1: //size but we use status
		return client1->GetSourceFrom() - client2->GetSourceFrom();
	case 2: //transferred
	case 3: //completed
		return CompareUnsigned(client1->GetTransferredDown(), client2->GetTransferredDown());
	case 4: //speed
		return CompareUnsigned(client1->GetDownloadDatarate(), client2->GetDownloadDatarate());
	case 5: //progress
		return CompareUnsigned(client1->GetAvailablePartCount(), client2->GetAvailablePartCount());
	case 6:
		if (client1->GetClientSoft() == client2->GetClientSoft())
			return client1->GetVersion() - client2->GetVersion();
		return -(client1->GetClientSoft() - client2->GetClientSoft()); // invert result to place eMule's at top
	case 7: //qr
		if (client1->GetDownloadState() == DS_DOWNLOADING)
			return (client2->GetDownloadState() == DS_DOWNLOADING) ? 0 : -1;
		if (client2->GetDownloadState() == DS_DOWNLOADING)
			return 1;
		if (client1->GetRemoteQueueRank() == 0 && client1->GetDownloadState() == DS_ONQUEUE && client1->IsRemoteQueueFull())
			return 1;
		if (client2->GetRemoteQueueRank() == 0 && client2->GetDownloadState() == DS_ONQUEUE && client2->IsRemoteQueueFull())
			return -1;
		if (client1->GetRemoteQueueRank() == 0)
			return 1;
		if (client2->GetRemoteQueueRank() == 0)
			return -1;
		return CompareUnsigned(client1->GetRemoteQueueRank(), client2->GetRemoteQueueRank());
	case 8: //state
		if (client1->GetDownloadState() == client2->GetDownloadState()) {
			if (client1->IsRemoteQueueFull() && client2->IsRemoteQueueFull())
				return 0;
			if (client1->IsRemoteQueueFull())
				return 1;
			if (client2->IsRemoteQueueFull())
				return -1;
		}
		return client1->GetDownloadState() - client2->GetDownloadState();
	case 14:
		if (client1->GetGeolocationData(true) && client2->GetGeolocationData(true))
			return CompareLocaleStringNoCase(client1->GetGeolocationData(true), client2->GetGeolocationData(true));
		else if (client1->GetGeolocationData(true))
			return 1;
		else
			return -1;
	}
	return 0;
}

void CDownloadListCtrl::OnNmDblClk(LPNMHDR, LRESULT* pResult)
{
	*pResult = 0;

	int iSel = GetSelectionMark();
	if (iSel < 0)
		return;

	const CtrlItem_Struct* content = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iSel));
	if (content == NULL || !content->value)
		return;

	if (content->type != FILE_TYPE) {
		ShowClientDialog(static_cast<CUpDownClient*>(content->value));
		return;
	}

	CPoint pt;
	if (!thePrefs.IsDoubleClickEnabled() && thePrefs.GetPreviewOnIconDblClk() && ::GetCursorPos(&pt)) {
		::GetCursorPos(&pt);
		ScreenToClient(&pt);
		CHeaderCtrl* pHeaderCtrl = GetHeaderCtrl();
		// Hidden columns pushing up the order, so we need to find total number of the hidden columns, then add our column index (which is 0 as the file name column here for this case)
		int m_iHiddenColumnCount = IsColumnHidden(2) + IsColumnHidden(3) + IsColumnHidden(4) + IsColumnHidden(5) + IsColumnHidden(6) + IsColumnHidden(7) + IsColumnHidden(8) + IsColumnHidden(9) +
			IsColumnHidden(10) + IsColumnHidden(11) + IsColumnHidden(12) + IsColumnHidden(12) + IsColumnHidden(13) + IsColumnHidden(14) + IsColumnHidden(15) + IsColumnHidden(16);
		int iColumnWidth = GetColumnWidth(pHeaderCtrl->OrderToIndex(m_iHiddenColumnCount)); // 0 + m_iHiddenColumnCount
		CPartFile* file = static_cast<CPartFile*>(content->value);
		if (pt.x < iColumnWidth) {
			if (file->IsReadyForPreview())
				file->PreviewFile();
			else
				MessageBeep(MB_OK);
		}
		return;
	} 
				
	if (!thePrefs.IsDoubleClickEnabled() && ::GetCursorPos(&pt)) {
		ScreenToClient(&pt);
		LVHITTESTINFO hit;
		hit.pt = pt;
		if (HitTest(&hit) >= 0 && (hit.flags & LVHT_ONITEM)) {
			LVHITTESTINFO subhit;
			subhit.pt = pt;
			if (SubItemHitTest(&subhit) >= 0 && subhit.iSubItem == 0) {
				CPartFile* file = static_cast<CPartFile*>(content->value);
				const LONG iconcx = theApp.GetSmallSytemIconSize().cx;
				if (thePrefs.ShowRatingIndicator()
					&& (file->HasComment() || file->HasRating() || file->IsKadCommentSearchRunning())
					&& pt.x >= sm_iIconOffset + iconcx
					&& pt.x <= sm_iIconOffset + iconcx + RATING_ICON_WIDTH)	{
					ShowFileDialog(IDD_COMMENTLST);
				} else if (thePrefs.GetPreviewOnIconDblClk()) {
					POINT point;
					::GetCursorPos(&point);
					ScreenToClient(&point);
					CHeaderCtrl* pHeaderCtrl = GetHeaderCtrl();
					// Hidden columns pushing up the order, so we need to find total number of the hidden columns, then add our column index (which is 1 as the column next to the file name column here for this case)
					int m_iHiddenColumnCount = IsColumnHidden(2) + IsColumnHidden(3) + IsColumnHidden(4) + IsColumnHidden(5) + IsColumnHidden(6) + IsColumnHidden(7) + IsColumnHidden(8) + IsColumnHidden(9) +
						IsColumnHidden(10) + IsColumnHidden(11) + IsColumnHidden(12) + IsColumnHidden(12) + IsColumnHidden(13) + IsColumnHidden(14) + IsColumnHidden(15) + IsColumnHidden(16);
					int iColumnWidth = GetColumnWidth(pHeaderCtrl->OrderToIndex(1 + m_iHiddenColumnCount));
					if (!thePrefs.IsDoubleClickEnabled() || (point.x < iColumnWidth)) {
						if (file->IsReadyForPreview())
							file->PreviewFile();
						else
							MessageBeep(MB_OK);
					}
				} else
					ShowFileDialog(0);
			}
		}
	}


}

void CDownloadListCtrl::CreateMenus()
{
	if (m_PreviewMenu)
		VERIFY(m_PreviewMenu.DestroyMenu());
	if (m_PrioMenu)
		VERIFY(m_PrioMenu.DestroyMenu());
	if (m_SourcesMenu)
		VERIFY(m_SourcesMenu.DestroyMenu());
	if (m_FileMenu)
		VERIFY(m_FileMenu.DestroyMenu());

	m_FileMenu.CreatePopupMenu();
	m_FileMenu.AddMenuSidebar(GetResString(_T("DOWNLOADMENUTITLE")));

	// Add 'Download Priority' sub menu
	//
	m_PrioMenu.CreateMenu();
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOLOW, GetResString(_T("PRIOLOW")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIONORMAL, GetResString(_T("PRIONORMAL")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOHIGH, GetResString(_T("PRIOHIGH")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOAUTO, GetResString(_T("PRIOAUTO")));

	CString sPrio;
	sPrio.Format(_T("%s (%s)"), (LPCTSTR)GetResString(_T("PRIORITY")), (LPCTSTR)GetResString(_T("DOWNLOAD")));
	m_FileMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_PrioMenu.m_hMenu, sPrio, _T("FILEPRIORITY"));

	// Add file commands
	//
	m_FileMenu.AppendMenu(MF_STRING, MP_PAUSE, GetResString(_T("DL_PAUSE")), _T("PAUSE"));
	m_FileMenu.AppendMenu(MF_STRING, MP_STOP, GetResString(_T("DL_STOP")), _T("STOP"));
	m_FileMenu.AppendMenu(MF_STRING, MP_RESUME, GetResString(_T("DL_RESUME")), _T("RESUME"));
	m_FileMenu.AppendMenu(MF_STRING, MP_CANCEL, GetResString(_T("MAIN_BTN_CANCEL")), _T("DELETE"));
	m_FileMenu.AppendMenu(MF_STRING, MP_CANCEL_FORGET, GetResString(_T("MAIN_BTN_CANCEL_FORGET")), _T("DELETE_FORGET"));
	m_FileMenu.AppendMenu(MF_SEPARATOR);

	m_FileMenu.AppendMenu(MF_STRING, MP_OPEN, GetResString(_T("DL_OPEN")), _T("OPENFILE"));
	m_PreviewMenu.CreateMenu();
	RebuildPreviewMenu(m_PreviewMenu, NULL, false, false, false, false, false);
	m_FileMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_PreviewMenu.m_hMenu, GetResString(_T("PREVIEWWITH")), _T("PREVIEW"));

	m_FileMenu.AppendMenu(MF_STRING, MP_METINFO, GetResString(_T("DL_INFO")), _T("FILEINFO"));
	m_FileMenu.AppendMenu(MF_STRING, MP_VIEWFILECOMMENTS, GetResString(_T("CMT_SHOWALL")), _T("FILECOMMENTS"));
	if (thePrefs.m_bImportParts)
		m_FileMenu.AppendMenu(MF_STRING | MF_GRAYED, MP_IMPORTPARTS, GetResString(_T("IMPORTPARTS")), _T("FILEIMPORTPARTS"));
	m_FileMenu.AppendMenu(MF_SEPARATOR);

	m_FileMenu.AppendMenu(MF_STRING, MP_CLEARCOMPLETED, GetResString(_T("DL_CLEAR")), _T("CLEARCOMPLETE"));

	// Add (extended user mode) 'Source Handling' sub menu
	//
	if (thePrefs.IsExtControlsEnabled()) {
		m_SourcesMenu.CreateMenu();
		m_SourcesMenu.AppendMenu(MF_STRING, MP_ADDSOURCE, GetResString(_T("ADDSRCMANUALLY")));
		m_SourcesMenu.AppendMenu(MF_STRING, MP_SETSOURCELIMIT, GetResString(_T("SETPFSLIMIT")));
		m_FileMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_SourcesMenu.m_hMenu, GetResString(_T("A4AF")));
	}
	m_FileMenu.AppendMenu(MF_SEPARATOR);

	// Add 'Copy & Paste' commands
	//
	m_FileMenu.AppendMenu(MF_STRING, MP_CUT, GetResString(_T("COPY_FILE_NAMES")), _T("FILERENAME"));
	if (thePrefs.GetShowCopyEd2kLinkCmd())
		m_FileMenu.AppendMenu(MF_STRING, MP_GETED2KLINK, GetResString(_T("DL_LINK1")), _T("ED2KLINK"));
	else
		m_FileMenu.AppendMenu(MF_STRING, MP_SHOWED2KLINK, GetResString(_T("DL_SHOWED2KLINK")), _T("ED2KLINK"));
	m_FileMenu.AppendMenu(MF_STRING, MP_PASTE, GetResString(_T("SW_DIRECTDOWNLOAD")), _T("PASTELINK"));
	m_FileMenu.AppendMenu(MF_SEPARATOR);

	// Search commands
	//
	m_FileMenu.AppendMenu(MF_STRING, MP_FIND, GetResString(_T("FIND")), _T("Search"));
	m_FileMenu.AppendMenu(MF_STRING, MP_SEARCHRELATED, GetResString(_T("SEARCHRELATED")), _T("KadFileSearch"));
	// Web-services and categories will be added on-the-fly.
}

CString CDownloadListCtrl::getTextList()
{
	CString out;
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct *cur_item = it->second;
		if (cur_item->type == FILE_TYPE) {
			const CPartFile *file = static_cast<CPartFile*>(cur_item->value);
			out.AppendFormat(_T("\n%s\t [%.1f%%] %u/%u - %s")
				, (LPCTSTR)file->GetFileName()
				, file->GetPercentCompleted()
				, file->GetTransferringSrcCount()
				, file->GetSourceCount()
				, (LPCTSTR)file->getPartfileStatus());
		}
	}
	return out;
}

float CDownloadListCtrl::GetFinishedSize()
{
	float fsize = 0;
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct *cur_item = it->second;
		if (cur_item->type == FILE_TYPE) {
			const CPartFile *file = static_cast<CPartFile*>(cur_item->value);
			if (file->GetStatus() == PS_COMPLETE)
				fsize += (uint64)file->GetFileSize();
		}
	}
	return fsize;
}


uint32 CDownloadListCtrl::GetTotalFilesCount()
{
	uint32 iCount = 0;
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct* cur_item = it->second;
		if (cur_item->type == FILE_TYPE)
			iCount++;
	}
	return iCount;
}

CString CDownloadListCtrl::GetFileItemDisplayText(const CPartFile *lpPartFile, int iSubItem)
{
	CString sText;
	switch (iSubItem) {
	case 0: //file name
		sText = lpPartFile->GetFileName();
		break;
	case 1: //size
		sText = CastItoXBytes(lpPartFile->GetFileSize());
		break;
	case 2: //transferred
		sText = CastItoXBytes(lpPartFile->GetTransferred());
		break;
	case 3: //transferred complete
		sText = CastItoXBytes(lpPartFile->GetCompletedSize());
		break;
	case 4: //speed
		if (lpPartFile->GetTransferringSrcCount())
			sText = CastItoXBytes(lpPartFile->GetDatarate(), false, true);
		break;
	case 5: //progress
		sText.Format(_T("%s: %.1f%%"), (LPCTSTR)GetResString(_T("DL_PROGRESS")), lpPartFile->GetPercentCompleted());
		break;
	case 6: //sources
		{
			const UINT sc = lpPartFile->GetSourceCount();
			if ((lpPartFile->GetStatus() != PS_PAUSED || sc) && lpPartFile->GetStatus() != PS_COMPLETE) {
				UINT ncsc = lpPartFile->GetNotCurrentSourcesCount();
				sText.Format(_T("%u"), sc - ncsc);
				if (ncsc > 0)
					sText.AppendFormat(_T("/%u"), sc);
				if (thePrefs.IsExtControlsEnabled() && lpPartFile->GetSrcA4AFCount() > 0)
					sText.AppendFormat(_T("+%u"), lpPartFile->GetSrcA4AFCount());
				if (lpPartFile->GetTransferringSrcCount() > 0)
					sText.AppendFormat(_T(" (%u)"), lpPartFile->GetTransferringSrcCount());
			}
			if (thePrefs.IsExtControlsEnabled() && lpPartFile->GetPrivateMaxSources() > 0)
				sText.AppendFormat(_T(" [%u]"), lpPartFile->GetPrivateMaxSources());
		}
		break;
	case 7: //prio
		{
			LPCTSTR uid;
			switch (lpPartFile->GetDownPriority()) {
			case PR_LOW:
				uid = lpPartFile->IsAutoDownPriority() ? _T("PRIOAUTOLOW") : _T("PRIOLOW");
				break;
			case PR_NORMAL:
				uid = lpPartFile->IsAutoDownPriority() ? _T("PRIOAUTONORMAL") : _T("PRIONORMAL");
				break;
			case PR_HIGH:
				uid = lpPartFile->IsAutoDownPriority() ? _T("PRIOAUTOHIGH") : _T("PRIOHIGH");
				break;
			default:
				uid = EMPTY;
			}
			if (uid)
				sText = GetResString(uid);
		}
		break;
	case 8: //state
		sText = lpPartFile->getPartfileStatus();
		break;
	case 9: //remaining time & size
		if (lpPartFile->GetStatus() != PS_COMPLETING && lpPartFile->GetStatus() != PS_COMPLETE) {
			time_t restTime = lpPartFile->getTimeRemaining();
			sText.Format(_T("%s (%s)"), (LPCTSTR)CastSecondsToHM(restTime), (LPCTSTR)CastItoXBytes((uint64)(lpPartFile->GetFileSize() - lpPartFile->GetCompletedSize())));
		}
		break;
	case 10: //last seen complete
		if (lpPartFile->lastseencomplete == 0)
			sText = GetResString(_T("NEVER"));
		else
			sText = lpPartFile->lastseencomplete.Format(thePrefs.GetDateTimeFormat4Lists());
		if (lpPartFile->m_nCompleteSourcesCountLo == 0)
			sText.AppendFormat(_T(" (< %u)"), lpPartFile->m_nCompleteSourcesCountHi);
		else if (lpPartFile->m_nCompleteSourcesCountLo == lpPartFile->m_nCompleteSourcesCountHi)
			sText.AppendFormat(_T(" (%u)"), lpPartFile->m_nCompleteSourcesCountLo);
		else
			sText.AppendFormat(_T(" (%u - %u)"), lpPartFile->m_nCompleteSourcesCountLo, lpPartFile->m_nCompleteSourcesCountHi);
		break;
	case 11: //last receive
		if (lpPartFile->GetLastReceptionDate() == time_t(-1))
			sText = GetResString(_T("NEVER"));
		else
			sText = lpPartFile->GetCFileDate().Format(thePrefs.GetDateTimeFormat4Lists());
		break;
	case 12: //cat
		{
			UINT cat = const_cast<CPartFile*>(lpPartFile)->GetCategory();
			if (cat)
				sText = thePrefs.GetCategory(cat)->strTitle;
		}
		break;
	case 13: //added on
		if (lpPartFile->GetCrFileDate())
			sText = lpPartFile->GetCrCFileDate().Format(thePrefs.GetDateTimeFormat4Lists());
		else
			sText += _T('?');
		break;
	case 15:
		sText = (lpPartFile->IsReadyForPreview() ? GetResString(_T("YES")) : GetResString(_T("NO")));
		break;
	case 16:
		sText.Format(_T("%.1f%%"), (lpPartFile->GetTransferred() ? lpPartFile->GetCompressionGain() * 100.0 / lpPartFile->GetTransferred() : 0.0));
		break;
	}
	return sText;
}


void CDownloadListCtrl::ShowSelectedFileDetails()
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

	CtrlItem_Struct* content = reinterpret_cast<CtrlItem_Struct*>(GetItemData(GetSelectionMark()));
	if (content != NULL)
		if (content->type == FILE_TYPE) {
			const CPartFile *file = static_cast<CPartFile*>(content->value);
			bool b = (thePrefs.ShowRatingIndicator()
				&& (file->HasComment() || file->HasRating() || file->IsKadCommentSearchRunning())
				&& point.x >= sm_iIconOffset + theApp.GetSmallSytemIconSize().cx
				&& point.x <= sm_iIconOffset + theApp.GetSmallSytemIconSize().cx + RATING_ICON_WIDTH);
			ShowFileDialog(b ? IDD_COMMENTLST : 0);
		} else
			ShowClientDialog(static_cast<CUpDownClient*>(content->value));
}

int CDownloadListCtrl::GetCompleteDownloads(int cat, int &total)
{
	total = 0;
	int count = 0;
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct *cur_item = it->second;
		if (cur_item->type == FILE_TYPE) {
			/*const*/ CPartFile *file = static_cast<CPartFile*>(cur_item->value);
			if (file->CheckShowItemInGivenCat(cat) || cat == -1) {
				++total;
				count += static_cast<int>(file->GetStatus() == PS_COMPLETE);
			}
		}
	}
	return count;
}

const bool CDownloadListCtrl::IsFilteredOut(CPartFile* pFile)
{
	if (!pFile || !pFile->CheckShowItemInGivenCat(curTab))
		return true;

	if (thePrefs.m_strFileTypeSelected != _T(ED2KFTSTR_ANY)) {
		const CString& strED2KFileType(GetED2KFileTypeSearchTerm(GetED2KFileTypeID(pFile->GetFileName()), false));

		if (thePrefs.m_strFileTypeSelected == _T(ED2KFTSTR_OTHER)) {
			if (!strED2KFileType.IsEmpty())
				return true;
		} else 	if (thePrefs.m_strFileTypeSelected != strED2KFileType )
			return true;
	} 

	if (thePrefs.m_uPreviewCheckState != BST_UNCHECKED) {
		bool m_bPreviwable = pFile->IsReadyForPreview();
		if ((thePrefs.m_uPreviewCheckState == BST_CHECKED && !m_bPreviwable) || (thePrefs.m_uPreviewCheckState == BST_INDETERMINATE && m_bPreviwable))
			return true;
	}

	const CStringArray& rastrFilter = theApp.emuledlg->transferwnd->m_pwndTransfer->m_astrFilterDownloadList;
	if (!rastrFilter.IsEmpty()) {
		// filtering is done by text only for all columns to keep it consistent and simple
		// for the user even if that doesn't allow complex filters
		// for example for a file size range - but this could be done at server search time already
		const CString& szFilterTarget(GetFileItemDisplayText(pFile, theApp.emuledlg->transferwnd->m_pwndTransfer->GetFilterColumnDownloadList()));

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

void CDownloadListCtrl::UpdateCurrentCategoryView()
{
	ReloadList(false, kDownloadListViewState);
}

void CDownloadListCtrl::UpdateCurrentCategoryView(CPartFile *thisfile)
{
	ListItems::const_iterator it = m_ListItems.find(thisfile);
	if (it != m_ListItems.end()) {
		const CtrlItem_Struct *cur_item = it->second;
		if (cur_item->type == FILE_TYPE) {
			CPartFile *file = static_cast<CPartFile*>(cur_item->value);
			if (!IsFilteredOut(file))
				ShowFile(file);
			else
				HideFile(file);
		}
	}
}

void CDownloadListCtrl::HideFile(CPartFile* tohide)
{
	if (theApp.IsClosing() || !tohide || theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible())
		return;

	// get associated list item
	ListItems::iterator it = m_ListItems.find(tohide);
	if (it == m_ListItems.end())
		return; // If the file is not in the list, we cannot hide it.

	CtrlItem_Struct* fileItem = it->second;
	int vecIndex;
	if (!m_ListedItemsMap.Lookup(fileItem, vecIndex))
		return; // If the file is not displayed, we cannot hide it.

	SaveListState(0, kDownloadListViewState); // Save selections and scroll state
	SetRedraw(false); // Suspend painting

	// Remove file and its visible sources from vector & map
	for (size_t i = 0; i < m_ListedItemsVector.size(); ) {
		CtrlItem_Struct* cur = m_ListedItemsVector[i];
		if (cur == fileItem || cur->owner == tohide) {
			m_ListedItemsMap.RemoveKey(cur); // Remove from map
			m_ListedItemsVector.erase(m_ListedItemsVector.begin() + i); // Remove from vector
		} else
			++i; // Only increment if we did not remove the item, otherwise we skip the next item.
	}

	RebuildListedItemsMap(); // Rebuild the map after sorting.
	UpdateDownloadListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list before restoring state.
	RestoreListState(0, kDownloadListViewState, false); // Restore selections and scroll state
	SetRedraw(true); // Resume painting
	Invalidate(FALSE); // Trigger redraw
}

void CDownloadListCtrl::ShowFile(CPartFile* toshow)
{
	if (theApp.IsClosing() || !toshow || theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible())
		return;

	ListItems::const_iterator it = m_ListItems.find(toshow);
	if (it == m_ListItems.end())
		return; // If the file is not in the list, we cannot show it.

	CtrlItem_Struct* fileItem = it->second;
	int idx;
	if (IsFilteredOut(toshow) || m_ListedItemsMap.Lookup(fileItem, idx))
		return; // Return if file is filtered out or already displayed.

	SaveListState(0, kDownloadListViewState); // Save selections and scroll state
	SetRedraw(false); // Suspend painting
	m_ListedItemsVector.push_back(fileItem); // Add the new item to the vector.
	CombinedSort(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), SortFunc); // Keep current sort order.
	RebuildListedItemsMap(); // Rebuild the map after sorting.
	UpdateDownloadListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list before restoring state.
	RestoreListState(0, kDownloadListViewState, false); // Restore selections and scroll state
	SetRedraw(true); // Resume painting
	Invalidate(FALSE); // Trigger redraw
}

void CDownloadListCtrl::ChangeCategory(int newsel)
{
	if (curTab == newsel)
		return; // No change, so do nothing.

	curTab = newsel;
	
	// Mark cache as dirty for rebuild after category change since visibility changed
	if (theApp.emuledlg && theApp.emuledlg->transferwnd)
		theApp.emuledlg->transferwnd->InvalidateCatTabInfo();
	
	ReloadList(false, kDownloadListViewState);
}

void CDownloadListCtrl::GetDisplayedFiles(CArray<CPartFile*, CPartFile*> *list)
{
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct *cur_item = it->second;
		if (cur_item->type == FILE_TYPE)
			list->Add(static_cast<CPartFile*>(cur_item->value));
	}
}

void CDownloadListCtrl::MoveCompletedfilesCat(UINT from, UINT to)
{
	const UINT cmin = min(from, to);
	const UINT cmax = max(from, to);
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct *cur_item = it->second;
		if (cur_item->type == FILE_TYPE) {
			CPartFile *file = static_cast<CPartFile*>(cur_item->value);
			if (!file->IsPartFile()) {
				UINT mycat = file->GetCategory();
				if (mycat >= cmin && mycat <= cmax)
					if (mycat == from)
						mycat = to;
					else
						mycat += (from < to ? -1 : 1);
				file->SetCategory(mycat);
			}
		}
	}
}

void CDownloadListCtrl::OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult)
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
		const LVITEM &rItem = reinterpret_cast<NMLVDISPINFO*>(pNMHDR)->item;

		// This isn't an owner drawn list anymore, instead this is implemented as a virtual list. So above description is now obsolete!
		if (rItem.mask & LVIF_TEXT) {
			CtrlItem_Struct* pCtrlItem = NULL;
			if (rItem.iItem < m_ListedItemsVector.size()) {
				pCtrlItem = m_ListedItemsVector[rItem.iItem];
				if (pCtrlItem && pCtrlItem->value) {
					switch (pCtrlItem->type) {
					case FILE_TYPE:
						_tcsncpy_s(rItem.pszText, rItem.cchTextMax, GetFileItemDisplayText(static_cast<CPartFile*>(pCtrlItem->value), rItem.iSubItem), _TRUNCATE);
						break;
					case UNAVAILABLE_SOURCE:
					case AVAILABLE_SOURCE:
						_tcsncpy_s(rItem.pszText, rItem.cchTextMax, GetSourceItemDisplayText(pCtrlItem, rItem.iSubItem), _TRUNCATE);
						break;
					default:
						ASSERT(0);
					}
				}
			}
		}
	}
	*pResult = 0;
}

void CDownloadListCtrl::OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult)
{
	LPNMLVGETINFOTIP pGetInfoTip = reinterpret_cast<LPNMLVGETINFOTIP>(pNMHDR);
	LVHITTESTINFO hti;
	if (pGetInfoTip && pGetInfoTip->iSubItem == 0 && ::GetCursorPos(&hti.pt)) {
		ScreenToClient(&hti.pt);
		if (SubItemHitTest(&hti) == -1 || hti.iItem != pGetInfoTip->iItem || hti.iSubItem != 0) {
			// don't show the default label tip for the main item, if the mouse is not over the main item
			if ((pGetInfoTip->dwFlags & LVGIT_UNFOLDED) == 0 && pGetInfoTip->cchTextMax > 0 && pGetInfoTip->pszText[0] != _T('\0'))
				pGetInfoTip->pszText[0] = _T('\0');
			return;
		}

		const CtrlItem_Struct* content = reinterpret_cast<CtrlItem_Struct*>(GetItemData(pGetInfoTip->iItem));
		if (content && pGetInfoTip->pszText && pGetInfoTip->cchTextMax > 0) {
			CString info;

			// build info text and display it
			if (content->type == 1) // for downloading files
				info = static_cast<CPartFile*>(content->value)->GetInfoSummary();
			else if (content->type == 3 || content->type == 2) { // for sources
				const CUpDownClient *client = static_cast<CUpDownClient*>(content->value);
				if (client->IsEd2kClient()) {
					in_addr server;
					server.s_addr = client->GetServerIP();
					info.Format(GetResString(_T("USERINFO"))
						+ GetResString(_T("CD_CSOFT")) + _T(": %s\n")
						+ GetResString(_T("GEOLOCATION")) + _T(": %s\n")
						+ _T("%s:%s:%u\n\n")
						, client->GetUserName() ? client->GetUserName() : (LPCTSTR)(_T('(') + GetResString(_T("UNKNOWN")) + _T(')'))
						, client->DbgGetFullClientSoftVer()
						, client->GetGeolocationData(true)
						, (LPCTSTR)GetResString(_T("SERVER"))
						, (LPCTSTR)ipstr(server)
						, client->GetServerPort());
					if (client->GetDownloadState() != DS_CONNECTING && client->GetDownloadState() != DS_DOWNLOADING) { //do not display inappropriate 'next re-ask'
						info.AppendFormat(GetResString(_T("NEXT_REASK")) + _T(":%s"), (LPCTSTR)CastSecondsToHM(client->GetTimeUntilReask(client->GetRequestFile()) / SEC2MS(1)));
						if (thePrefs.IsExtControlsEnabled())
							info.AppendFormat(_T(" (%s)"), (LPCTSTR)CastSecondsToHM(client->GetTimeUntilReask(content->owner) / SEC2MS(1)));
						info += _T('\n');
					}
					info.AppendFormat(GetResString(_T("SOURCEINFO")), client->GetAskedCountDown(), client->GetAvailablePartCount());
					info += _T('\n');

					if (content->type == 2) {
						info.AppendFormat(_T("%s%s"), (LPCTSTR)GetResString(_T("CLIENTSOURCENAME")), client->GetClientFilename().IsEmpty() ? _T("-") : (LPCTSTR)client->GetClientFilename());
						if (!client->GetFileComment().IsEmpty())
							info.AppendFormat(_T("\n%s %s"), (LPCTSTR)GetResString(_T("CMT_READ")), (LPCTSTR)client->GetFileComment());
						if (client->GetFileRating())
							info.AppendFormat(_T("\n%s:%s"), (LPCTSTR)GetResString(_T("QL_RATING")), (LPCTSTR)GetRateString(client->GetFileRating()));
					} else { // client asked twice
						info += GetResString(_T("ASKEDFAF"));
						if (client->GetRequestFile() && !client->GetRequestFile()->GetFileName().IsEmpty())
							info.AppendFormat(_T(": %s"), (LPCTSTR)client->GetRequestFile()->GetFileName());
					}

					if (thePrefs.IsExtControlsEnabled() && !client->m_OtherRequests_list.IsEmpty()) {
						CSimpleArray<const CString*> apstrFileNames;
						for (POSITION pos = client->m_OtherRequests_list.GetHeadPosition(); pos != NULL;)
							apstrFileNames.Add(&client->m_OtherRequests_list.GetNext(pos)->GetFileName());
						Sort(apstrFileNames);
						if (content->type == 2)
							info += _T('\n');
						info.AppendFormat(_T("\n%s:"), (LPCTSTR)GetResString(_T("A4AF_FILES")));

						for (int i = 0; i < apstrFileNames.GetSize(); ++i) {
							const CString *pstrFileName = apstrFileNames[i];
							if (info.GetLength() + (i > 0 ? 2 : 0) + pstrFileName->GetLength() >= pGetInfoTip->cchTextMax) {
								static TCHAR const szEllipsis[] = _T("\n:...");
								if (info.GetLength() + (int)_countof(szEllipsis) - 1 < pGetInfoTip->cchTextMax)
									info += szEllipsis;
								break;
							}
							if (i > 0)
								info += _T("\n:");
							info += *pstrFileName;
						}
					}
				} else
					info.Format(_T("URL: %s\nAvailable parts: %u"), client->GetUserName(), client->GetAvailablePartCount());
			}

			info += TOOLTIP_AUTOFORMAT_SUFFIX_CH;
			_tcsncpy(pGetInfoTip->pszText, info, pGetInfoTip->cchTextMax);
			pGetInfoTip->pszText[pGetInfoTip->cchTextMax - 1] = _T('\0');
		}
	}
	*pResult = 0;
}

void CDownloadListCtrl::ShowFileDialog(UINT uInvokePage)
{
	CSimpleArray<CPartFile*> aFiles;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		int iItem = GetNextSelectedItem(pos);
		if (iItem >= 0) {
			const CtrlItem_Struct* pCtrlItem = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iItem));		
			if (pCtrlItem != NULL && pCtrlItem->type == FILE_TYPE)
				aFiles.Add(static_cast<CPartFile*>(pCtrlItem->value));
		}
	}

	if (aFiles.GetSize() > 0) {
		CDownloadListListCtrlItemWalk::SetItemType(FILE_TYPE);
		CFileDetailDialog dialog(&aFiles, uInvokePage, this);
		dialog.DoModal();
	}
}

CDownloadListListCtrlItemWalk::CDownloadListListCtrlItemWalk(CDownloadListCtrl *pListCtrl)
	: CListCtrlItemWalk(pListCtrl)
	, m_pDownloadListCtrl(pListCtrl)
	, m_eItemType(INVALID_TYPE)
{
}

CObject* CDownloadListListCtrlItemWalk::GetPrevSelectableItem()
{
	if (m_pDownloadListCtrl == NULL) {
		ASSERT(0);
		return NULL;
	}
	ASSERT(m_eItemType != INVALID_TYPE);

	int iItemCount = m_pDownloadListCtrl->GetItemCount();
	if (iItemCount >= 2) {
		POSITION pos = m_pDownloadListCtrl->GetFirstSelectedItemPosition();
		if (pos) {
			int iItem = m_pDownloadListCtrl->GetNextSelectedItem(pos);
			int iCurSelItem = iItem;
			while (--iItem >= 0) {
				const CtrlItem_Struct* ctrl_item = reinterpret_cast<CtrlItem_Struct*>(m_pDownloadListCtrl->GetItemData(iItem));
				if (ctrl_item != NULL && (ctrl_item->type == m_eItemType || (m_eItemType != FILE_TYPE && ctrl_item->type != FILE_TYPE))) {
					m_pDownloadListCtrl->SetItemState(iCurSelItem, 0, LVIS_SELECTED | LVIS_FOCUSED);
					m_pDownloadListCtrl->SetItemState(iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
					m_pDownloadListCtrl->SetSelectionMark(iItem);
					m_pDownloadListCtrl->EnsureVisible(iItem, FALSE);
					return reinterpret_cast<CObject*>(ctrl_item->value);
				}
			}
		}
	}
	return NULL;
}

CObject* CDownloadListListCtrlItemWalk::GetNextSelectableItem()
{
	ASSERT(m_pDownloadListCtrl != NULL);
	if (m_pDownloadListCtrl == NULL)
		return NULL;
	ASSERT(m_eItemType != (ItemType)-1);

	int iItemCount = m_pDownloadListCtrl->GetItemCount();
	if (iItemCount >= 2) {
		POSITION pos = m_pDownloadListCtrl->GetFirstSelectedItemPosition();
		if (pos) {
			int iItem = m_pDownloadListCtrl->GetNextSelectedItem(pos);
			int iCurSelItem = iItem;
			while (++iItem < iItemCount) {
				const CtrlItem_Struct* ctrl_item = reinterpret_cast<CtrlItem_Struct*>(m_pDownloadListCtrl->GetItemData(iItem));
				if (ctrl_item != NULL && (ctrl_item->type == m_eItemType || (m_eItemType != FILE_TYPE && ctrl_item->type != FILE_TYPE))) {
					m_pDownloadListCtrl->SetItemState(iCurSelItem, 0, LVIS_SELECTED | LVIS_FOCUSED);
					m_pDownloadListCtrl->SetItemState(iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
					m_pDownloadListCtrl->SetSelectionMark(iItem);
					m_pDownloadListCtrl->EnsureVisible(iItem, FALSE);
					return reinterpret_cast<CObject*>(ctrl_item->value);
				}
			}
		}
	}
	return NULL;
}

void CDownloadListCtrl::ShowClientDialog(CUpDownClient *pClient)
{
	CDownloadListListCtrlItemWalk::SetItemType(AVAILABLE_SOURCE); // just set to something !=FILE_TYPE
	CClientDetailDialog dialog(pClient, this);
	dialog.DoModal();
}

CImageList* CDownloadListCtrl::CreateDragImage(int /*iItem*/, LPPOINT lpPoint)
{
	static const int iMaxSelectedItems = 30;
	int iSelectedItems = 0;
	CRect rcSelectedItems, rcLabel;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos && iSelectedItems < iMaxSelectedItems;) {
		int iItem = GetNextSelectedItem(pos);
		const CtrlItem_Struct* pCtrlItem = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iItem));
		if (pCtrlItem != NULL && pCtrlItem->type == FILE_TYPE && GetItemRect(iItem, rcLabel, LVIR_LABEL)) {
			if (iSelectedItems <= 0) {
				rcSelectedItems.left = sm_iIconOffset;
				rcSelectedItems.top = rcLabel.top;
				rcSelectedItems.right = rcLabel.right;
				rcSelectedItems.bottom = rcLabel.bottom;
			}
			rcSelectedItems.UnionRect(rcSelectedItems, rcLabel);
			++iSelectedItems;
		}
	}
	if (iSelectedItems <= 0)
		return NULL;

	CClientDC dc(this);
	CDC dcMem;
	if (!dcMem.CreateCompatibleDC(&dc))
		return NULL;

	CBitmap bmpMem;
	if (!bmpMem.CreateCompatibleBitmap(&dc, rcSelectedItems.Width(), rcSelectedItems.Height()))
		return NULL;

	CBitmap *pOldBmp = dcMem.SelectObject(&bmpMem);
	CFont *pOldFont = dcMem.SelectObject(GetFont());

	COLORREF crBackground = GetCustomSysColor(COLOR_WINDOW);
	dcMem.FillSolidRect(0, 0, rcSelectedItems.Width(), rcSelectedItems.Height(), crBackground);
	dcMem.SetTextColor(GetCustomSysColor(COLOR_WINDOWTEXT));

	iSelectedItems = 0;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos && iSelectedItems < iMaxSelectedItems;) {
		int iItem = GetNextSelectedItem(pos);
		const CtrlItem_Struct* pCtrlItem = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iItem));
		if (pCtrlItem && pCtrlItem->type == FILE_TYPE) {
			const CPartFile *pPartFile = static_cast<CPartFile*>(pCtrlItem->value);
			GetItemRect(iItem, rcLabel, LVIR_LABEL);

			RECT rcItem;
			rcItem.left = 16 + sm_iLabelOffset;
			rcItem.top = rcLabel.top - rcSelectedItems.top;
			rcItem.right = rcLabel.right;
			rcItem.bottom = rcItem.top + rcLabel.Height();

			if (theApp.GetSystemImageList()) {
				int iImage = theApp.GetFileTypeSystemImageIdx(pPartFile->GetFileName());
				::ImageList_Draw(theApp.GetSystemImageList(), iImage, dcMem, 0, rcItem.top, ILD_TRANSPARENT);
			}

			dcMem.DrawText(pPartFile->GetFileName(), &rcItem, MLC_DT_TEXT);

			++iSelectedItems;
		}
	}
	dcMem.SelectObject(pOldBmp);
	dcMem.SelectObject(pOldFont);

	// At this point the bitmap in 'bmpMem' may or may not contain alpha data and we have to take special
	// care about passing such a bitmap further into Windows (GDI). Strange things can happen due to that
	// not all GDI functions can deal with RGBA bitmaps. Thus, create an image list with ILC_COLORDDB.
	CImageList *pimlDrag = new CImageList();
	pimlDrag->Create(rcSelectedItems.Width(), rcSelectedItems.Height(), ILC_COLORDDB | ILC_MASK, 1, 0);
	pimlDrag->Add(&bmpMem, crBackground);
	bmpMem.DeleteObject();

	if (lpPoint) {
		CPoint ptCursor;
		::GetCursorPos(&ptCursor);
		ScreenToClient(&ptCursor);
		lpPoint->x = ptCursor.x - rcSelectedItems.left;
		lpPoint->y = ptCursor.y - rcSelectedItems.top;
	}

	return pimlDrag;
}

bool CDownloadListCtrl::ReportAvailableCommands(CList<int> &liAvailableCommands)
{
	const DWORD curTick = ::GetTickCount();

	if (curTick < m_dwLastAvailableCommandsCheck + SEC2MS(3) && !m_availableCommandsDirty)
		return false;
	m_dwLastAvailableCommandsCheck = curTick;
	m_availableCommandsDirty = false;

	liAvailableCommands.AddTail(MP_SAVEAPPSTATE);
	liAvailableCommands.AddTail(MP_RELOADCONF);
	liAvailableCommands.AddTail(MP_BACKUP);
	liAvailableCommands.AddTail(MP_FILEINSPECTOR);

	int iSel = GetNextItem(-1, LVIS_SELECTED);
	if (iSel >= 0) {
		const CtrlItem_Struct* content = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iSel));
		if (content != NULL && content->type == FILE_TYPE) {
			// get merged settings
			int iSelectedItems = 0;
			int iFilesToPause = 0;
			int iFilesToStop = 0;
			int iFilesToResume = 0;
			int iFilesToOpen = 0;
			int iFilesToPreview = 0;
			int iFilesToCancel = 0;
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				int iIdxSel = GetNextSelectedItem(pos);
				const CtrlItem_Struct* pItemData = reinterpret_cast<CtrlItem_Struct*>(GetItemData(iIdxSel));

				if (pItemData == NULL || pItemData->type != FILE_TYPE)
					continue;

				// Extra safety: Validate file pointer against current download queue.
				const CPartFile* pFile = static_cast<const CPartFile*>(pItemData->value);
				if (pFile == NULL || !theApp.downloadqueue->IsPartFile(pFile)) 
					continue;

				++iSelectedItems;

				iFilesToCancel += static_cast<int>(pFile->GetStatus() != PS_COMPLETING);
				iFilesToStop += static_cast<int>(pFile->CanStopFile());
				iFilesToPause += static_cast<int>(pFile->CanPauseFile());
				iFilesToResume += static_cast<int>(pFile->CanResumeFile());
				iFilesToOpen += static_cast<int>(pFile->CanOpenFile());
				iFilesToPreview += static_cast<int>(pFile->IsReadyForPreview());
			}


			// enable commands if there is at least one item which can be used for the action
			if (iFilesToCancel > 0)
				liAvailableCommands.AddTail(MP_CANCEL);
			if (iFilesToStop > 0)
				liAvailableCommands.AddTail(MP_STOP);
			if (iFilesToPause > 0)
				liAvailableCommands.AddTail(MP_PAUSE);
			if (iFilesToResume > 0)
				liAvailableCommands.AddTail(MP_RESUME);
			if (iSelectedItems == 1 && iFilesToOpen == 1)
				liAvailableCommands.AddTail(MP_OPEN);
			if (iSelectedItems == 1 && iFilesToPreview == 1)
			{
				liAvailableCommands.AddTail(MP_PREVIEW);
				liAvailableCommands.AddTail(MP_PREVIEW1);
				liAvailableCommands.AddTail(MP_PREVIEW2);
				liAvailableCommands.AddTail(MP_PREVIEW3);
				liAvailableCommands.AddTail(MP_PREVIEW4);
				liAvailableCommands.AddTail(MP_PREVIEW5);
				liAvailableCommands.AddTail(MP_PREVIEW6);
				liAvailableCommands.AddTail(MP_PREVIEW7);
				liAvailableCommands.AddTail(MP_PREVIEW8);
				liAvailableCommands.AddTail(MP_PREVIEW9);
				liAvailableCommands.AddTail(MP_PREVIEW10);
			}

			if (iSelectedItems == 1)
				liAvailableCommands.AddTail(MP_OPENFOLDER);
			if (iSelectedItems > 0) {
				liAvailableCommands.AddTail(MP_METINFO);
				liAvailableCommands.AddTail(MP_VIEWFILECOMMENTS);
				liAvailableCommands.AddTail(MP_SHOWED2KLINK);
				liAvailableCommands.AddTail(MP_NEWCAT);
				liAvailableCommands.AddTail(MP_PRIOLOW);
				if (theApp.emuledlg->searchwnd->CanSearchRelatedFiles())
					liAvailableCommands.AddTail(MP_SEARCHRELATED);
			}
		}
	}
	int total;
	if (GetCompleteDownloads(curTab, total) > 0)
		liAvailableCommands.AddTail(MP_CLEARCOMPLETED);
	if (GetItemCount() > 0)
		liAvailableCommands.AddTail(MP_FIND);
	return true;
}

static inline bool GetDRM(const LPCTSTR pszFilePath)
{
	int fd = _topen(pszFilePath, O_RDONLY | O_BINARY);
	if (fd != -1) {
		static const byte FILEHEADER_WM_ID[] = { 0x30, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11, 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c };
		BYTE aucBuff[16384];
		int iRead = _read(fd, aucBuff, sizeof aucBuff);
		_close(fd);
		if ((size_t)iRead > sizeof FILEHEADER_WM_ID && memcmp(aucBuff, FILEHEADER_WM_ID, sizeof FILEHEADER_WM_ID) == 0) {
			iRead -= sizeof FILEHEADER_WM_ID;
			if (iRead > 0) {
				static const WCHAR s_wszWrmHdr[] = L"<WRMHEADER";
				const BYTE* pucBuff = aucBuff + sizeof FILEHEADER_WM_ID;
				int iPatternSize = sizeof s_wszWrmHdr - sizeof s_wszWrmHdr[0];
				for (int iSearchRange = iRead - iPatternSize; iSearchRange >= 0; --iSearchRange) {
					if (memcmp(pucBuff, (BYTE*)s_wszWrmHdr, iPatternSize) == 0)
						return true;
					++pucBuff;
				}
			}
		}
	}
	return false;
}

void CDownloadListCtrl::FileInspector(const bool bForce)
{
	if (theApp.IsClosing() || (!bForce && m_dwLastDetection != 0 && (::GetTickCount() - m_dwLastDetection < thePrefs.GetFileInspectorCheckPeriod() * 60000/*minutes*/)))
		return;

	if (pDetectEmptyFakeFiles != NULL) {
		DWORD lpExitCode;
		GetExitCodeThread(pDetectEmptyFakeFiles->m_hThread, &lpExitCode);
		if (lpExitCode == STILL_ACTIVE) {
			AddLogLine(false, GetResString(_T("FILE_INSPECTOR_IN_PROGRESS")));
			return;
		}
	}

	AddLogLine(false, GetResString(_T("FILE_INSPECTOR_STARTED")));
	pDetectEmptyFakeFiles = AfxBeginThread(FileInspectorProc, (LPVOID)bForce, THREAD_PRIORITY_IDLE);
	m_dwLastDetection = ::GetTickCount();
}

UINT AFX_CDECL CDownloadListCtrl::FileInspectorProc(LPVOID pParam)
{
	DbgSetThreadName("FakeDRMInvalidExt");
	bool m_bForce = static_cast<bool>(pParam);
	bool m_bFileFound = false;
	CTypedPtrList<CPtrList, PartFileOperationMsgParams*> renamelist;
	CTypedPtrList<CPtrList, PartFileOperationMsgParams*> removelist;

	for (ListItems::const_iterator it = theApp.emuledlg->transferwnd->GetDownloadList()->m_ListItems.begin(); it != theApp.emuledlg->transferwnd->GetDownloadList()->m_ListItems.end(); ++it) {
		const CtrlItem_Struct* cur_item = it->second;
		// Since loop take to much time on long donwload lists, we use lock losely here. Main instance can remove items from  list, 
		// so there is a possibility of having exceptions with this way. So we need to use try catch block here.
		try {
			if (cur_item == NULL || cur_item->type != FILE_TYPE)
				continue;

			CPartFile* file = static_cast<CPartFile*>(cur_item->value);
			if (!file->IsPartFile())
				continue;

			if (!m_bForce && file->GetFileDate() < file->m_tLastChecked) // File has not been modified since last check
				continue;

			CString cLogMsg;

			if (thePrefs.IsFileInspectorInvalidExt() && (uint64)file->GetCompletedSize()) {
				// Try to find valid file type
				EFileType bycontent = GetFileTypeEx(file, false, true);
				if (bycontent != FILETYPE_UNKNOWN) {
					// Current file type
					const CString& fname(file->GetFileName());
					LPCTSTR pDot = ::PathFindExtension(fname);
					CString szExt(pDot + static_cast<int>(*pDot != _T('\0'))); //skip the dot
					szExt.MakeUpper();
					// Check if current file type is invalid
					if (IsExtensionTypeOf(bycontent, szExt) != 1) {
						CString m_strOldFileName, m_strNewFileName, m_strNewExtension;
						m_strNewExtension = GetFileTypeName(bycontent);
						if (m_strNewExtension == "MPEG Audio")
							m_strNewExtension = "mp3";
						else if (m_strNewExtension == "ISO/NRG")
							m_strNewExtension = "iso";
						else if (m_strNewExtension == "MPEG Video")
							m_strNewExtension = "mpg";
						else if (m_strNewExtension == "Microsoft Media Audio/Video")
							m_strNewExtension = "wm";
						else if (m_strNewExtension == "WIN/DOS EXE")
							m_strNewExtension = "exe";
						else
							m_strNewExtension = m_strNewExtension.MakeLower();
						m_strOldFileName = file->GetFileName();
						m_strNewFileName = m_strOldFileName;
						// Get base file name if file has an extension.
						// Some files can have a dot inside filename but no real extension, so limit extension lenght with 4 
						if (szExt != "" && szExt.GetLength() < 5)
							::PathRemoveExtension(m_strNewFileName.GetBuffer(m_strNewFileName.GetLength()));
						m_strNewFileName.Format(_T("%s.%s"), m_strNewFileName, m_strNewExtension); // Add new extension

						if (thePrefs.GetFileInspector() == 2) {
							cLogMsg.Format(GetResString(_T("INVALID_FILE_EXTENSION_REPLACED_MESSAGE")), (LPCTSTR)EscPercent(m_strOldFileName), (LPCTSTR)EscPercent(m_strNewFileName));
							PartFileOperationMsgParams* params = new PartFileOperationMsgParams;
							params->pFile = file;
							params->strNewFileName = m_strNewFileName;
							params->cLogMsg = cLogMsg;
							renamelist.AddTail(params);
						} else { // This will work for GetFileInspector == 0 or 1, so toolbar button will be able log even when GetFileInspector is disabled.
							cLogMsg.Format(GetResString(_T("INVALID_FILE_EXTENSION_REPLACED_MESSAGE2")), (LPCTSTR)EscPercent(file->GetFileName()), m_strNewExtension);
							theApp.QueueLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
						}
					}
				}
			}

			// There is no need to check GetFileInspectorCompletedThreshold for DRM detection, so we'll do this as first step.
			if (thePrefs.GetFileInspectorDRM() && (uint64)file->GetCompletedSize()) {
				file->m_bPreviewing = true; //To prevent the part file from vanishing while copying, a lock on m_FileCompleteMutex might be better than currently used m_bPreviewing flag
				bool m_bDRMFound = GetDRM(file->GetFilePath());
				file->m_bPreviewing = false;
				if (m_bDRMFound) {
					m_bFileFound = true;
					if (thePrefs.GetFileInspector() == 2) {
						cLogMsg.Format(GetResString(_T("FILE_INSPECTOR_DELETE_MESSAGE3")), (LPCTSTR)EscPercent(file->GetFileName()));
						PartFileOperationMsgParams* params = new PartFileOperationMsgParams;
						params->pFile = file;
						params->cLogMsg = cLogMsg;
						removelist.AddTail(params);
					} else { // This will work for GetFileInspector == 0 or 1, so toolbar button will be able log even when GetFileInspector is disabled.
						cLogMsg.Format(GetResString(_T("FILE_INSPECTOR_LOG_MESSAGE3")), (LPCTSTR)EscPercent(file->GetFileName()));
						theApp.QueueLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
					}

					file->m_tLastChecked = time(NULL); // File checks completed successfully. We can update file's last checked time with current time.

					if (theApp.IsClosing()) // This check is needed by long download lists
						return 1;
					else
						continue;
				}
			}

			if (!thePrefs.GetFileInspectorFake() || (uint64)file->GetCompletedSize() / 1024 < thePrefs.GetFileInspectorCompletedThreshold()) {
				file->m_tLastChecked = time(NULL); // File checks completed successfully. We can update file's last checked time with current time.
				if (theApp.IsClosing()) // This check is needed by long download lists
					return 1;
				else
					continue; // If the received bytes are less than the defined value, continue with the next file
			}

			int m_iCompressionPercentage = file->GetCompressionGain() * 100.0 / file->GetTransferred();
			if (m_iCompressionPercentage < thePrefs.GetFileInspectorCompressionThreshold()) {
				file->m_tLastChecked = time(NULL); // File checks completed successfully. We can update file's last checked time with current time.
				if (theApp.IsClosing()) // This check is needed by long download lists
					return 1;
				else
					continue; // If the compression is less than the defined value, continue with the next file
			}

			bool m_bBypassZeroPercentage = false;
			bool m_bAllZero = true;
			long double m_ldTotalCount = 0;
			long double m_ldZeroCount = 0;

			if (!thePrefs.GetFileInspectorBypassZeroPercentage() || m_iCompressionPercentage < thePrefs.GetFileInspectorCompressionThresholdToBypassZero()) {
				if (thePrefs.GetFileInspectorZeroPercentageThreshold() == 100 && GetFileTypeEx(file, false, true) != FILETYPE_UNKNOWN) {  //File type signature area will be included in the calculation, so don't check if GetFileInspectorZeroPercentageThreshold != 100.
					file->m_tLastChecked = time(NULL); // File checks completed successfully. We can update file's last checked time with current time.
					if (theApp.IsClosing()) // This check is needed by long download lists
						return 1;
					else
						continue; // If this file has a valid file type signature, continue with the next file.
				}

				CArray<Gap_Struct>* filled = new CArray<Gap_Struct>;
				file->GetFilledArray(*filled);
				if (filled->IsEmpty()) {
					file->m_tLastChecked = time(NULL); // File checks completed successfully. We can update file's last checked time with current time.
					delete filled;
					continue;
				}

				file->m_bPreviewing = true; //To prevent the part file from vanishing while copying, a lock on m_FileCompleteMutex might be better than currently used m_bPreviewing flag
				try {
					// Loop through the filled areas and copy data
					for (INT_PTR i = 0; i < filled->GetCount(); ++i) {
						const Gap_Struct& fill = (*filled)[i];
						for (uint64 uStart = fill.start; uStart < fill.end;) { //last valid byte was at fill.end-1
							m_ldTotalCount++;
							OVERLAPPED ovr = { 0, 0, {{ LODWORD(uStart), HIDWORD(uStart)}} };
							OVERLAPPED ovw = ovr;
							BYTE buffer[16384];
							DWORD lenData = (DWORD)min(uStart - fill.end, sizeof buffer);
							DWORD dwRead;
							if (!::ReadFile((HANDLE)file->m_hpartfile, buffer, lenData, &dwRead, &ovr))
								CFileException::ThrowOsError((LONG)::GetLastError(), file->GetFileName());
							size_t len = sizeof(buffer) / sizeof(buffer[0]); // Get the size of array			
							bool result = std::all_of(buffer, buffer + len, [](bool elem) {	return elem == 0; }); // Check if all elements of array arr are zero
							if (!result) { // A non full zero array found.
								m_bAllZero = false;
								if (thePrefs.GetFileInspectorZeroPercentageThreshold() == 100)
									break;
							} else
								m_ldZeroCount++;
							ASSERT(dwRead && dwRead < fill.end);
							uStart += dwRead;
						}
					}
				} catch (CFileException* error) {
					m_bAllZero = false; // since we didn't complete operation we should set this to false
					error->Delete();
				} catch (...) {
					m_bAllZero = false; // since we didn't complete operation we should set this to false
					ASSERT(0);
				}
				file->m_bPreviewing = false;
				delete filled;
			} else 
				m_bBypassZeroPercentage = true;

			long double m_ldZeroPercentage = (m_ldZeroCount / m_ldTotalCount) * 100.0;
			if (m_bBypassZeroPercentage || m_bAllZero || m_ldZeroPercentage >= thePrefs.GetFileInspectorZeroPercentageThreshold()) {
				m_bFileFound = true;
				if (thePrefs.GetFileInspector() == 2) {
					if (!m_bBypassZeroPercentage)
						cLogMsg.Format(GetResString(_T("FILE_INSPECTOR_DELETE_MESSAGE")), m_iCompressionPercentage, (uint64)file->GetCompletedSize() / 1024, m_ldZeroPercentage, (LPCTSTR)EscPercent(file->GetFileName()));
					else
						cLogMsg.Format(GetResString(_T("FILE_INSPECTOR_DELETE_MESSAGE2")), m_iCompressionPercentage, (uint64)file->GetCompletedSize() / 1024, (LPCTSTR)EscPercent(file->GetFileName()));

					PartFileOperationMsgParams* params = new PartFileOperationMsgParams;
					params->pFile = file;
					params->cLogMsg = cLogMsg;
					removelist.AddTail(params);
				} else { // This will work for GetFileInspector == 0 or 1, so toolbar button will be able log even when GetFileInspector is disabled.
					if (!m_bBypassZeroPercentage)
						cLogMsg.Format(GetResString(_T("FILE_INSPECTOR_LOG_MESSAGE")), m_iCompressionPercentage, (uint64)file->GetCompletedSize() / 1024, m_ldZeroPercentage, (LPCTSTR)EscPercent(file->GetFileName()));
					else
						cLogMsg.Format(GetResString(_T("FILE_INSPECTOR_LOG_MESSAGE2")), m_iCompressionPercentage, (uint64)file->GetCompletedSize() / 1024, (LPCTSTR)EscPercent(file->GetFileName()));
					theApp.QueueLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
				}
			}

			file->m_tLastChecked = time(NULL); // File checks completed successfully. We can update file's last checked time with current time.

			if (theApp.IsClosing()) // This check is needed by long download lists
				return 1;

		} catch (const std::exception& e) {
			theApp.QueueDebugLogLine(true, _T("CDownloadListCtrl::FileInspectorProc: Exception caught: %s", (LPCTSTR)EscPercent(e.what())));
			continue;
		} catch (...) {
			theApp.QueueDebugLogLine(true, _T("CDownloadListCtrl::FileInspectorProc: Unknown exception caught"));
			continue;
		}
	}

	for (POSITION pos = renamelist.GetHeadPosition(); pos != NULL && !theApp.IsClosing();) {
		POSITION currentPos = pos;
		PartFileOperationMsgParams* pItem = renamelist.GetNext(pos);
		theApp.emuledlg->transferwnd->GetDownloadList()->SendMessage(UM_INVALIDEXTENSIONFOUND, 0, (LPARAM)pItem); // Send message to main thread for the actions.
		renamelist.RemoveAt(currentPos);
		delete pItem; // Free memory
	}

	for (POSITION pos = removelist.GetHeadPosition(); pos != NULL && !theApp.IsClosing();) {
		POSITION currentPos = pos;
		PartFileOperationMsgParams* pItem = removelist.GetNext(pos);
		theApp.emuledlg->transferwnd->GetDownloadList()->SendMessage(UM_EMPTYFAKEFILEFOUND, 0, (LPARAM)pItem); // Send message to main thread for the actions.
		removelist.RemoveAt(currentPos);
		delete pItem; // Free memory
	}

	if (m_bFileFound)
		theApp.QueueLogLine(true, GetResString(_T("FILE_INSPECTOR_COMPLETED_FOUND")));
	else
		theApp.QueueLogLine(true, GetResString(_T("FILE_INSPECTOR_COMPLETED_NOT_FOUND")));

	return 0;
}

LRESULT CDownloadListCtrl::OnInvalidExtensionFound(WPARAM wParam, LPARAM lParam)
{
	PartFileOperationMsgParams* params = reinterpret_cast<PartFileOperationMsgParams*>(lParam); // Get parameters
	params->pFile->SetFileName((LPCTSTR)params->strNewFileName, true);
	params->pFile->UpdateDisplayedInfo();
	if (params->pFile->SavePartFile())
		AddLogLine(true, (LPCTSTR)EscPercent(params->cLogMsg));
	return 0;
}

LRESULT CDownloadListCtrl::OnEmptyFakeFileFound(WPARAM wParam, LPARAM lParam)
{
	PartFileOperationMsgParams* params = reinterpret_cast<PartFileOperationMsgParams*>(lParam); // Get parameters
	theApp.emuledlg->transferwnd->GetDownloadList()->SetRedraw(false);
	theApp.emuledlg->transferwnd->GetDownloadList()->HideSources(params->pFile); // Hide sources of the file to be deleted
	switch (params->pFile->GetStatus()) {
	case PS_WAITINGFORHASH:
	case PS_HASHING:
	case PS_COMPLETING:
		break;
	case PS_COMPLETE: {
		bool delsucc = ShellDeleteFile(params->pFile->GetFilePath());
		if (delsucc)
			theApp.sharedfiles->RemoveFile(params->pFile, true);
		else {
			CString strError;
			strError.Format(GetResString(_T("ERR_DELFILE")) + _T("\r\n\r\n%s"), (LPCTSTR)params->pFile->GetFilePath(), (LPCTSTR)GetErrorMessage(::GetLastError()));
			CDarkMode::MessageBox(strError);
		}
		theApp.emuledlg->transferwnd->GetDownloadList()->RemoveFile(params->pFile);
		AddLogLine(true, (LPCTSTR)EscPercent(params->cLogMsg));
		break;
	}
	default:
		if (params->pFile->GetCategory())
			theApp.downloadqueue->StartNextFileIfPrefs(params->pFile->GetCategory());
	case PS_PAUSED: {
		params->pFile->DeletePartFile();
		AddLogLine(true, (LPCTSTR)EscPercent(params->cLogMsg));
	}
	}
	theApp.emuledlg->transferwnd->GetDownloadList()->SetRedraw(true);

	return 0;
}

void CDownloadListCtrl::ShowActiveDownloadsBold(const bool bEnabled) {
	if (thePrefs.GetUseSystemFontForMainControls()) {
		CFont* pFont = GetFont();
		LOGFONT lfFont;
		pFont->GetLogFont(&lfFont);
		if (bEnabled)
			lfFont.lfWeight = FW_BOLD;
		else
			lfFont.lfWeight = FW_NORMAL;
		m_fontBold.CreateFontIndirect(&lfFont);
		m_pFontBold = &m_fontBold;

	} else if (bEnabled)
		m_pFontBold = &theApp.m_fontDefaultBold;
	else {
		m_pFontBold = GetFont();
	}
}

void CDownloadListCtrl::MaintainSortOrderAfterUpdate()
{
	if (GetSortItem() != -1) // Re-sort the list to maintain sort order after updates
		ReloadList(true, static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL));
}
