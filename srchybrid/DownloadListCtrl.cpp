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
#include "Friend.h"
#include "PartFile.h"
#include "ClientCredits.h"
#include "MemDC.h"
#include "OtherFunctions.h"
#include <algorithm>
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
#include "SearchResultsWnd.h"
#include "SharedFileList.h"
#include "KnownFileList.h"
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
#include "SafeFile.h"
#include <limits>
#include <set>

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
	const EListStateField kDownloadListRemoveBatchState = static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL);
	const DWORD kDownloadListSetItemCountFlags = LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL;
	void GetLiveFileSourceDisplayCounts(const CPartFile *pFile, UINT &uTotalSources, UINT &uCurrentSources, UINT &uTransferringSources)
	{
		uTotalSources = 0;
		uCurrentSources = 0;
		uTransferringSources = 0;
		if (pFile == NULL)
			return;

		std::set<const CUpDownClient*> countedSources;
		for (POSITION pos = pFile->srclist.GetHeadPosition(); pos != NULL;) {
			const CUpDownClient *pClient = pFile->srclist.GetNext(pos);
			if (pClient == NULL || !countedSources.insert(pClient).second)
				continue;

			++uTotalSources;
			switch (pClient->GetDownloadState()) {
			case DS_DOWNLOADING:
				++uCurrentSources;
				++uTransferringSources;
				break;
			case DS_ONQUEUE:
				if (!pClient->IsRemoteQueueFull())
					++uCurrentSources;
				break;
			default:
				break;
			}
		}
	}

	CString GetLiveFileSourceDisplayText(const CPartFile *pFile)
	{
		CString strText;
		if (pFile == NULL)
			return strText;

		UINT uTotalSources = 0;
		UINT uCurrentSources = 0;
		UINT uTransferringSources = 0;
		GetLiveFileSourceDisplayCounts(pFile, uTotalSources, uCurrentSources, uTransferringSources);
		if ((pFile->GetStatus() != PS_PAUSED || uTotalSources) && pFile->GetStatus() != PS_COMPLETE) {
			strText.Format(_T("%u"), uCurrentSources);
			if (uCurrentSources < uTotalSources)
				strText.AppendFormat(_T("/%u"), uTotalSources);
			if (thePrefs.IsExtControlsEnabled() && pFile->GetSrcA4AFCount() > 0)
				strText.AppendFormat(_T("+%u"), pFile->GetSrcA4AFCount());
			if (uTransferringSources > 0)
				strText.AppendFormat(_T(" (%u)"), uTransferringSources);
		}
		if (thePrefs.IsExtControlsEnabled() && pFile->GetPrivateMaxSources() > 0)
			strText.AppendFormat(_T(" [%u]"), pFile->GetPrivateMaxSources());
		return strText;
	}

	uint32 GetLiveFileDownloadDatarate(const CPartFile *pFile)
	{
		if (pFile == NULL || pFile->GetStatus() == PS_COMPLETE || pFile->GetStatus() == PS_COMPLETING)
			return 0;

		uint64 uSourceRate = 0;
		for (POSITION pos = pFile->srclist.GetHeadPosition(); pos != NULL;) {
			const CUpDownClient *pClient = pFile->srclist.GetNext(pos);
			if (pClient != NULL && pClient->GetRequestFile() == pFile && pClient->GetDownloadState() == DS_DOWNLOADING)
				uSourceRate += pClient->GetDownloadDatarate();
		}
		if (uSourceRate > 0)
			return uSourceRate > UINT_MAX ? UINT_MAX : static_cast<uint32>(uSourceRate);
		return pFile->GetDatarate();
	}

	bool IsLiveDownloadFileDisplayColumn(int iSubItem)
	{
		switch (iSubItem) {
		case 2: // Transferred
		case 3: // Completed
		case 4: // Speed
		case 5: // Progress
		case 6: // Sources
		case 8: // Status
		case 9: // Remaining
		case 10: // Complete sources
		case 11: // Last received
		case 16: // Compression
			return true;
		default:
			return false;
		}
	}

	bool IsLiveDownloadSourceDisplayColumn(int iSubItem)
	{
		switch (iSubItem) {
		case 2: // Transferred
		case 4: // Speed
		case 7: // Queue rank
		case 8: // Status
			return true;
		default:
			return false;
		}
	}

	enum
	{
		WM_DOWNLOADLISTCTRL_CHUNKED_REMOVE = WM_APP + 4121,
		WM_DOWNLOADLISTCTRL_CHUNKED_STATE = WM_APP + 4122,
		WM_DOWNLOADLISTCTRL_DOWNLOAD_INSPECTOR_APPLY = WM_APP + 4124,
		TimerChunkedRemoveDownload = 0x7D01,
		TimerChunkedDownloadState = 0x7D02,
		TimerDownloadInspectorApply = 0x7D04
	};

	const size_t kTransferLargeReloadRows = 2000;

	struct CStringNoCaseLess
	{
		bool operator()(const CString& left, const CString& right) const { return left.CompareNoCase(right) < 0; }
	};

	DWORD GetRecentDownloadBulkInputAgeMs(DWORD dwNow)
	{
		LASTINPUTINFO lastInput;
		memset(&lastInput, 0, sizeof(lastInput));
		lastInput.cbSize = sizeof(lastInput);
		return ::GetLastInputInfo(&lastInput) ? static_cast<DWORD>(dwNow - lastInput.dwTime) : static_cast<DWORD>(-1);
	}

	void GetChunkedRemoveDownloadSliceLimits(DWORD &dwSliceBudgetMs, UINT &uMaxItemsPerSlice)
	{
		const DWORD dwNow = ::GetTickCount();
		const UINT uQueueStatus = HIWORD(::GetQueueStatus(QS_KEY | QS_MOUSE | QS_PAINT | QS_TIMER | QS_POSTMESSAGE));
		const bool bInputPending = (uQueueStatus & (QS_KEY | QS_MOUSE)) != 0;
		const bool bPaintPending = (uQueueStatus & QS_PAINT) != 0;
		const bool bDispatchPending = (uQueueStatus & (QS_TIMER | QS_POSTMESSAGE)) != 0;
		const DWORD dwInputAge = GetRecentDownloadBulkInputAgeMs(dwNow);

		if (bInputPending || dwInputAge < 250) {
			dwSliceBudgetMs = 3;
			uMaxItemsPerSlice = 64;
			return;
		}
		if (bPaintPending || bDispatchPending) {
			dwSliceBudgetMs = 5;
			uMaxItemsPerSlice = 192;
			return;
		}
		if (dwInputAge < 1000) {
			dwSliceBudgetMs = 8;
			uMaxItemsPerSlice = 512;
			return;
		}

		dwSliceBudgetMs = 18;
		uMaxItemsPerSlice = 2048;
	}

	class CScopedDownloadClientRef
	{
	public:
		explicit CScopedDownloadClientRef(CUpDownClient* pClient = NULL)
			: m_pClient(pClient)
		{
		}

		~CScopedDownloadClientRef()
		{
			Release();
		}

		CUpDownClient* Get() const
		{
			return m_pClient;
		}

		void Attach(CUpDownClient* pClient)
		{
			if (m_pClient == pClient)
				return;
			Release();
			m_pClient = pClient;
		}

		void Release()
		{
			if (m_pClient != NULL) {
				m_pClient->ReleaseRuntimeReference();
				m_pClient = NULL;
			}
		}

	private:
		CUpDownClient* m_pClient;
	};

	UINT GetSharePermissionMenuItem(const CKnownFile* pFile)
	{
		if (pFile == NULL)
			return 0;

		switch (pFile->GetPermissions()) {
		case -1:
			return MP_PERMDEFAULT;
		case PERM_ALL:
			return MP_PERMALL;
		case PERM_FRIENDS:
			return MP_PERMFRIENDS;
		case PERM_NOONE:
			return MP_PERMNONE;
		default:
			ASSERT(false);
			return 0;
		}
	}

	CString GetSharePermissionLabel(const int iPermission)
	{
		switch (iPermission) {
		case PERM_ALL:
			return GetResString(_T("SHARE_PERMISSION_EVERYBODY"));
		case PERM_FRIENDS:
			return GetResString(_T("SHARE_PERMISSION_FRIENDSONLY"));
		case PERM_NOONE:
			return GetResString(_T("SHARE_PERMISSION_HIDDEN"));
		default:
			return CString();
		}
	}

	void UpdateSharePermissionMenuChecks(CMenu& menu, UINT uCheckedItem)
	{
		static const UINT s_auPermissionMenuItems[] = { MP_PERMDEFAULT, MP_PERMNONE, MP_PERMFRIENDS, MP_PERMALL };
		for (size_t i = 0; i < _countof(s_auPermissionMenuItems); ++i)
			menu.CheckMenuItem(s_auPermissionMenuItems[i], MF_BYCOMMAND | ((s_auPermissionMenuItems[i] == uCheckedItem) ? MF_CHECKED : MF_UNCHECKED));
	}

	CString GetAutoRenameToMajorityNameLabel(const bool bUseInvertPrefix)
	{
		CString strLabel(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_RENAME_TO_MAJORITY_NAME")));
		if (bUseInvertPrefix)
			strLabel = GetResString(_T("INVERT")) + _T(" ") + strLabel;
		return strLabel;
	}

	bool IsAutoRenameToMajorityNameModeEnabled()
	{
		return thePrefs.GetDownloadInspector() > 0;
	}

	int FindMenuCommandPosition(CMenu& menu, const UINT uCommand)
	{
		const int iMenuItemCount = menu.GetMenuItemCount();
		for (int iMenuItem = 0; iMenuItem < iMenuItemCount; ++iMenuItem) {
			if (menu.GetMenuItemID(iMenuItem) == uCommand)
				return iMenuItem;
		}

		return -1;
	}

	void SyncAutoRenameToMajorityNameMenuItem(CMenuXP& menu)
	{
		const int iAutoRenameMenuPosition = FindMenuCommandPosition(menu, MP_AUTORENAMETOMAJORITYNAME);
		if (!IsAutoRenameToMajorityNameModeEnabled()) {
			if (iAutoRenameMenuPosition != -1)
				menu.RemoveMenu(MP_AUTORENAMETOMAJORITYNAME, MF_BYCOMMAND);
			return;
		}

		if (iAutoRenameMenuPosition != -1)
			return;

		const int iCommentsMenuPosition = FindMenuCommandPosition(menu, MP_VIEWFILECOMMENTS);
		if (iCommentsMenuPosition == -1)
			return;

		menu.InsertMenu(iCommentsMenuPosition + 1, MF_BYPOSITION | MF_STRING, MP_AUTORENAMETOMAJORITYNAME, GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_RENAME_TO_MAJORITY_NAME")), _T("EDIT"));
	}

	CObject* CreateClientDetailWalkerTokenFromRuntimeID(const DWORD uRuntimeID)
	{
		return uRuntimeID != 0 ? reinterpret_cast<CObject*>((static_cast<ULONG_PTR>(uRuntimeID) << 1) | 1) : NULL;
	}


	int GetBitmapWidth(CBitmap& bitmap)
	{
		BITMAP bitmapInfo = {};
		if (bitmap.GetSafeHandle() == NULL || bitmap.GetBitmap(&bitmapInfo) == 0)
			return 0;

		return bitmapInfo.bmWidth;
	}

	void UpdateDownloadListItemCount(CListCtrl& listCtrl, const size_t itemCount, bool bInvalidateAll = false)
	{
		listCtrl.SetItemCountEx(static_cast<int>(itemCount), bInvalidateAll ? LVSICF_NOSCROLL : kDownloadListSetItemCountFlags);
	}

	void FillDownloadFallbackOwnerDataRow(CListCtrl& listCtrl, LPDRAWITEMSTRUCT lpDrawItemStruct)
	{
		if (lpDrawItemStruct == NULL || lpDrawItemStruct->hDC == NULL)
			return;
		CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
		if (pDC == NULL)
			return;
		CRect rcItem(lpDrawItemStruct->rcItem);
		CRect rcClient;
		listCtrl.GetClientRect(&rcClient);
		rcItem.left = rcClient.left;
		rcItem.right = rcClient.right;
		pDC->FillSolidRect(rcItem, GetCustomSysColor(COLOR_WINDOW));
	}

	bool HasVisibleFileItem(CDownloadListCtrl& downloadListCtrl, CPartFile* file, int* pFileIndex = NULL)
	{
		if (theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !downloadListCtrl.IsWindowVisible() || file == NULL)
			return false;

		CDownloadListCtrl::ListItems::const_iterator itFile = downloadListCtrl.m_ListItems.find(file);
		if (itFile == downloadListCtrl.m_ListItems.end() || itFile->second == NULL)
			return false;

		int iFileIndex = -1;
		if (!downloadListCtrl.m_ListedItemsMap.Lookup(itFile->second, iFileIndex))
			return false;

		if (pFileIndex != NULL)
			*pFileIndex = iFileIndex;

		return true;
	}

	bool HasVisibleSourceItem(CDownloadListCtrl& downloadListCtrl, CUpDownClient* source, CPartFile* owner)
	{
		if (theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !downloadListCtrl.IsWindowVisible())
			return false;

		for (CDownloadListCtrl::ListItems::const_iterator it = downloadListCtrl.m_ListItems.lower_bound(source); it != downloadListCtrl.m_ListItems.end() && it->first == source; ++it) {
			CtrlItem_Struct* curItem = it->second;
			if (curItem == NULL)
				continue;
			if (owner != NULL && owner != curItem->owner)
				continue;
			if (curItem->owner == NULL)
				continue;

			int iVectorIndex;
			if (downloadListCtrl.m_ListedItemsMap.Lookup(curItem, iVectorIndex))
				return true;
		}

		return false;
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
		const bool bHasPreviewData = file != NULL && (uint64)file->GetCompletedSize() > 0;
		const bool bPreviewCommandEnabled = bEnablePreview || bHasPreviewData;
		menu.AppendODMenu(MF_STRING | (bPreviewCommandEnabled ? MF_ENABLED : MF_GRAYED), MP_PREVIEW, new CMenuXPText(MP_PREVIEW, strPrimaryLabel.IsEmpty() ? GetResString(_T("DL_PREVIEW")) : strPrimaryLabel, thePreviewApps.GetPreviewCommandIcon(strPrimaryCommand)));
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
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_MESSAGE(UM_EMPTYFAKEFILEFOUND, OnEmptyFakeFileFound)
	ON_MESSAGE(UM_INVALIDEXTENSIONFOUND, OnInvalidExtensionFound)
	ON_MESSAGE(WM_DOWNLOADLISTCTRL_CHUNKED_REMOVE, OnProcessChunkedRemoveDownloads)
	ON_MESSAGE(WM_DOWNLOADLISTCTRL_CHUNKED_STATE, OnProcessChunkedDownloadState)
	ON_MESSAGE(WM_DOWNLOADLISTCTRL_DOWNLOAD_INSPECTOR_APPLY, OnApplyDownloadInspectorResults)
END_MESSAGE_MAP()

CDownloadListCtrl::CDownloadListCtrl()
	: CDownloadListListCtrlItemWalk(this)
	, curTab()
	, m_uListedFilesCount()
	, m_bRightClicked()
	, m_bRemainSort()
	, m_bDeferredReload()
	, m_bRawSortInProgress(false)
	, m_pFontBold()
	, m_dwLastAvailableCommandsCheck()
	, m_availableCommandsDirty(true)
	, m_iDataSize(-1)
	, m_bChunkedRemoveDownloadPending(false)
	, m_bChunkedRemoveDownloadAddToCanceledMet(true)
	, m_bChunkedRemoveDownloadDeleteCompletedFile(false)
	, m_bChunkedRemoveDownloadListStateBatchActive(false)
	, m_bChunkedRemoveDownloadQueueBulkActive(false)
	, m_bChunkedRemoveDownloadVisibleItemsPending(false)
	, m_bChunkedRemoveDownloadVisibleSnapshotActive(false)
	, m_uChunkedRemoveDownloadVisibleSnapshotRows()
	, m_bChunkedRemoveDownloadWaitingForDiskCleanup(false)
	, m_uChunkedRemoveDownloadPendingDiskDeletes()
	, m_uChunkedRemoveDownloadProcessed()
	, m_uChunkedRemoveDownloadStale()
	, m_uChunkedRemoveDownloadFailed()
	, m_uChunkedRemoveDownloadTotal()
	, m_dwChunkedRemoveDownloadStartedTick()
	, m_dwChunkedRemoveDownloadLastProgressTick()
	, m_uChunkedRemoveDownloadSequence(0)
	, m_uChunkedRemoveDownloadCorrelationId(0)
	, m_bChunkedDownloadStatePending(false)
	, m_bChunkedDownloadStateListStateBatchActive(false)
	, m_eChunkedDownloadStateAction(ChunkedDownloadStatePause)
	, m_iChunkedDownloadStateValue(0)
	, m_uChunkedDownloadStateProcessed()
	, m_uChunkedDownloadStateStale()
	, m_uChunkedDownloadStateFailed()
	, m_uChunkedDownloadStateTotal()
	, m_dwChunkedDownloadStateStartedTick()
	, m_dwChunkedDownloadStateLastProgressTick()
	, m_uChunkedDownloadStateSequence(0)
	, m_uChunkedDownloadStateCorrelationId(0)
	, m_bBackendDownloadCommandOverlayActive(false)
	, m_uBackendDownloadCommandSequence(0)
	, m_uBackendDownloadCommandCorrelationId(0)
	, m_uCompletedBackendDownloadCommandSequence(0)
	, m_uCompletedBackendDownloadCommandCorrelationId(0)
	, m_bMirroredSearchDownloadOverlayActive(false)
	, m_uMirroredSearchDownloadDone()
	, m_uMirroredSearchDownloadTotal()
	, pDownloadInspectorThread()
	, m_dwLastDetection()
	, m_tNextAutoDeleteScan(time(NULL) + MIN2S(10))
	, m_lDownloadInspectorApplyPending(0)
	, m_bDownloadInspectorApplyPending(false)
{
	SetGeneralPurposeFind(true);
	SetSkinKey(_T("DownloadsLv"));
}

CDownloadListCtrl::~CDownloadListCtrl()
{
	if (::IsWindow(m_hWnd))
		KillTimer(TimerDownloadInspectorApply);
	ClearDownloadInspectorApplyItems();
	ClearChunkedRemoveDownloadItems();
	ClearChunkedDownloadStateItems();

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

void CDownloadListCtrl::OnDestroy()
{
	MSG msg = {};
	while (::PeekMessage(&msg, m_hWnd, WM_DOWNLOADLISTCTRL_CHUNKED_REMOVE, WM_DOWNLOADLISTCTRL_CHUNKED_STATE, PM_REMOVE)) {
	}
	while (::PeekMessage(&msg, m_hWnd, WM_DOWNLOADLISTCTRL_DOWNLOAD_INSPECTOR_APPLY, WM_DOWNLOADLISTCTRL_DOWNLOAD_INSPECTOR_APPLY, PM_REMOVE)) {
		SDownloadInspectorApplyParams* pParams = reinterpret_cast<SDownloadInspectorApplyParams*>(msg.lParam);
		if (pParams != NULL) {
			delete pParams;
			::InterlockedDecrement(&m_lDownloadInspectorApplyPending);
		}
	}

	ClearDownloadInspectorApplyItems();
	ClearChunkedRemoveDownloadItems();
	ClearChunkedDownloadStateItems();

	CMuleListCtrl::OnDestroy();
}

void CDownloadListCtrl::OnOperationOverlayCancel()
{
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->CancelActiveBulkOperations();
}


bool CDownloadListCtrl::IsCompletedBackendDownloadCommandOverlay(uint64 uSequence, uint64 uCorrelationId) const
{
	if (uSequence != 0 && m_uCompletedBackendDownloadCommandSequence != 0 && uSequence <= m_uCompletedBackendDownloadCommandSequence)
		return true;
	if (uCorrelationId != 0 && m_uCompletedBackendDownloadCommandCorrelationId == uCorrelationId && (uSequence == 0 || m_uCompletedBackendDownloadCommandSequence == 0 || m_uCompletedBackendDownloadCommandSequence == uSequence))
		return true;
	return false;
}

void CDownloadListCtrl::MarkCompletedBackendDownloadCommandOverlay(uint64 uSequence, uint64 uCorrelationId)
{
	if (uSequence == 0 && uCorrelationId == 0)
		return;
	if (uSequence != 0) {
		if (m_uCompletedBackendDownloadCommandSequence == 0 || uSequence > m_uCompletedBackendDownloadCommandSequence) {
			m_uCompletedBackendDownloadCommandSequence = uSequence;
			m_uCompletedBackendDownloadCommandCorrelationId = uCorrelationId;
		}
		return;
	}
	m_uCompletedBackendDownloadCommandCorrelationId = uCorrelationId;
}


void CDownloadListCtrl::UpdateBackendDownloadCommandOverlay(bool bRemove, UINT uDone, UINT uTotal, uint64 uSequence, uint64 uCorrelationId)
{
	if (uTotal < BULK_OPERATION_MIN_ITEMS)
		return;
	if (IsCompletedBackendDownloadCommandOverlay(uSequence, uCorrelationId))
		return;

	m_bBackendDownloadCommandOverlayActive = true;
	m_uBackendDownloadCommandSequence = uSequence;
	m_uBackendDownloadCommandCorrelationId = uCorrelationId;
	if (HasActiveChunkedDownloadOperation()) {
		if (theApp.emuledlg != NULL)
			theApp.emuledlg->RefreshActiveBulkOperationOverlays();
		return;
	}

	CString strDetail;
	strDetail.Format(GetResString(_T("BULKOP_PROGRESS_DETAIL")), uDone, uTotal);
	UpdateOperationOverlay(GetResString(bRemove ? _T("BULKOP_DELETE_DOWNLOADS_TITLE") : _T("BULKOP_UPDATE_DOWNLOADS_TITLE")), strDetail, uDone, uTotal, true);
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
}

void CDownloadListCtrl::HideBackendDownloadCommandOverlay(uint64 uSequence, uint64 uCorrelationId)
{
	if (uSequence == 0 && uCorrelationId == 0 && m_bBackendDownloadCommandOverlayActive)
		MarkCompletedBackendDownloadCommandOverlay(m_uBackendDownloadCommandSequence, m_uBackendDownloadCommandCorrelationId);
	else
		MarkCompletedBackendDownloadCommandOverlay(uSequence, uCorrelationId);

	if (!m_bBackendDownloadCommandOverlayActive)
		return;
	if (uSequence != 0 && m_uBackendDownloadCommandSequence != 0 && m_uBackendDownloadCommandSequence != uSequence)
		return;
	if (uCorrelationId != 0 && m_uBackendDownloadCommandCorrelationId != 0 && m_uBackendDownloadCommandCorrelationId != uCorrelationId)
		return;

	m_bBackendDownloadCommandOverlayActive = false;
	m_uBackendDownloadCommandSequence = 0;
	m_uBackendDownloadCommandCorrelationId = 0;
	if (m_bMirroredSearchDownloadOverlayActive && !HasActiveChunkedDownloadOperation())
		RefreshMirroredSearchDownloadOverlay();
	else if (!HasActiveChunkedDownloadOperation())
		HideOperationOverlay();
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
}

void CDownloadListCtrl::BeginBackendDownloadRemoveBatch()
{
	if (!::IsWindow(m_hWnd) || m_bChunkedRemoveDownloadListStateBatchActive)
		return;

	BeginListStateBatch(0, kDownloadListRemoveBatchState);
	m_bChunkedRemoveDownloadListStateBatchActive = true;
}

void CDownloadListCtrl::EndBackendDownloadRemoveBatch(bool bFlushVisibleItems)
{
	if (!m_bChunkedRemoveDownloadListStateBatchActive)
		return;

	if (bFlushVisibleItems)
		FlushChunkedRemoveVisibleItems();

	m_bChunkedRemoveDownloadListStateBatchActive = false;
	if (::IsWindow(m_hWnd))
		EndListStateBatch(0, kDownloadListRemoveBatchState, false);
	if (::IsWindow(m_hWnd))
		Invalidate(FALSE);
}

void CDownloadListCtrl::UpdateMirroredSearchDownloadOverlay(const CString& strTitle, const CString& strDetail, UINT uDone, UINT uTotal)
{
	m_bMirroredSearchDownloadOverlayActive = true;
	m_strMirroredSearchDownloadTitle = strTitle;
	m_strMirroredSearchDownloadDetail = strDetail;
	m_uMirroredSearchDownloadDone = uDone;
	m_uMirroredSearchDownloadTotal = uTotal;

	if (!HasActiveChunkedDownloadOperation())
		UpdateOperationOverlay(m_strMirroredSearchDownloadTitle, m_strMirroredSearchDownloadDetail, m_uMirroredSearchDownloadDone, m_uMirroredSearchDownloadTotal, true);
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
}

void CDownloadListCtrl::HideMirroredSearchDownloadOverlay()
{
	if (!m_bMirroredSearchDownloadOverlayActive)
		return;

	m_bMirroredSearchDownloadOverlayActive = false;
	m_strMirroredSearchDownloadTitle.Empty();
	m_strMirroredSearchDownloadDetail.Empty();
	m_uMirroredSearchDownloadDone = 0;
	m_uMirroredSearchDownloadTotal = 0;

	if (!HasActiveChunkedDownloadOperation())
		HideOperationOverlay();
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
}

void CDownloadListCtrl::RefreshMirroredSearchDownloadOverlay()
{
	if (m_bMirroredSearchDownloadOverlayActive && !HasActiveChunkedDownloadOperation())
		UpdateOperationOverlay(m_strMirroredSearchDownloadTitle, m_strMirroredSearchDownloadDetail, m_uMirroredSearchDownloadDone, m_uMirroredSearchDownloadTotal, true);
}

bool CDownloadListCtrl::HasActiveChunkedDownloadOperation() const
{
	return (m_uChunkedRemoveDownloadTotal > 0 && (!m_chunkedRemoveDownloadItems.IsEmpty() || m_bChunkedRemoveDownloadPending || m_bChunkedRemoveDownloadListStateBatchActive || m_bChunkedRemoveDownloadQueueBulkActive || m_bChunkedRemoveDownloadWaitingForDiskCleanup || m_uChunkedRemoveDownloadPendingDiskDeletes > 0))
		|| (m_uChunkedDownloadStateTotal > 0 && (!m_chunkedDownloadStateItems.IsEmpty() || m_bChunkedDownloadStatePending || m_bChunkedDownloadStateListStateBatchActive));
}

void CDownloadListCtrl::CancelActiveChunkedDownloadOperation()
{
	ClearChunkedRemoveDownloadItems();
	ClearChunkedDownloadStateItems();
	if (m_bMirroredSearchDownloadOverlayActive)
		RefreshMirroredSearchDownloadOverlay();
	else
		HideOperationOverlay();
}

bool CDownloadListCtrl::GetActiveChunkedDownloadOperationProgress(CString& strTitle, CString& strBody, CString& strCancelAndExit, CString& strWaitAndExit, UINT& uDone, UINT& uTotal) const
{
	if (m_uChunkedRemoveDownloadTotal > 0 && (!m_chunkedRemoveDownloadItems.IsEmpty() || m_bChunkedRemoveDownloadPending || m_bChunkedRemoveDownloadListStateBatchActive || m_bChunkedRemoveDownloadQueueBulkActive || m_bChunkedRemoveDownloadWaitingForDiskCleanup || m_uChunkedRemoveDownloadPendingDiskDeletes > 0)) {
		uTotal = m_uChunkedRemoveDownloadTotal;
		uDone = GetChunkedRemoveDownloadDoneCount();
		strTitle = GetResString(_T("BULKOP_EXIT_TITLE"));
		strBody.Format(GetResString(_T("BULKOP_EXIT_DELETE_BODY")), uTotal, uDone, uTotal - uDone);
		strCancelAndExit = GetResString(_T("BULKOP_EXIT_CANCEL_DELETE_AND_EXIT"));
		strWaitAndExit = GetResString(_T("BULKOP_EXIT_FINISH_AND_EXIT"));
		return true;
	}

	if (m_uChunkedDownloadStateTotal > 0 && (!m_chunkedDownloadStateItems.IsEmpty() || m_bChunkedDownloadStatePending || m_bChunkedDownloadStateListStateBatchActive)) {
		uTotal = m_uChunkedDownloadStateTotal;
		const UINT uRemaining = static_cast<UINT>(m_chunkedDownloadStateItems.GetCount());
		uDone = (uTotal >= uRemaining) ? (uTotal - uRemaining) : 0;
		strTitle = GetResString(_T("BULKOP_EXIT_TITLE"));
		strBody.Format(GetResString(_T("BULKOP_EXIT_UPDATE_BODY")), uTotal, uDone, uTotal - uDone);
		strCancelAndExit = GetResString(_T("BULKOP_EXIT_CANCEL_UPDATE_AND_EXIT"));
		strWaitAndExit = GetResString(_T("BULKOP_EXIT_FINISH_AND_EXIT"));
		return true;
	}

	return false;
}

UINT CDownloadListCtrl::GetChunkedRemoveDownloadDoneCount() const
{
	const UINT uDone = m_uChunkedRemoveDownloadProcessed + m_uChunkedRemoveDownloadFailed + m_uChunkedRemoveDownloadStale;
	return uDone < m_uChunkedRemoveDownloadTotal ? uDone : m_uChunkedRemoveDownloadTotal;
}

UINT CDownloadListCtrl::GetChunkedRemoveDownloadProgressProcessed() const
{
	return m_uChunkedRemoveDownloadProcessed < m_uChunkedRemoveDownloadTotal ? m_uChunkedRemoveDownloadProcessed : m_uChunkedRemoveDownloadTotal;
}

bool CDownloadListCtrl::IsChunkedRemoveDownloadSequenceActive(uint64 uSequence, uint64 uCorrelationId) const
{
	if (m_uChunkedRemoveDownloadTotal == 0 || m_uChunkedRemoveDownloadPendingDiskDeletes == 0)
		return false;
	if (uSequence != 0 && m_uChunkedRemoveDownloadSequence != 0 && uSequence != m_uChunkedRemoveDownloadSequence)
		return false;
	if (uCorrelationId != 0 && m_uChunkedRemoveDownloadCorrelationId != 0 && uCorrelationId != m_uChunkedRemoveDownloadCorrelationId)
		return false;
	return true;
}


void CDownloadListCtrl::CompleteChunkedRemoveDownloadDiskCleanup(uint64 uSequence, uint64 uCorrelationId, UINT uCompletedCount, UINT uFailedCount)
{
	const UINT uFinishedCount = uCompletedCount + uFailedCount;
	if (uFinishedCount == 0 || !IsChunkedRemoveDownloadSequenceActive(uSequence, uCorrelationId))
		return;

	if (uFinishedCount >= m_uChunkedRemoveDownloadPendingDiskDeletes)
		m_uChunkedRemoveDownloadPendingDiskDeletes = 0;
	else
		m_uChunkedRemoveDownloadPendingDiskDeletes -= uFinishedCount;

	if (uFailedCount != 0) {
		const UINT uFailedFromProcessed = min(uFailedCount, m_uChunkedRemoveDownloadProcessed);
		m_uChunkedRemoveDownloadProcessed -= uFailedFromProcessed;
		m_uChunkedRemoveDownloadFailed += uFailedFromProcessed;
		AddDebugLogLine(DLP_HIGH, false, _T("Chunked download remove disk cleanup reported physical delete failure. sequence=%I64u correlation=%I64u failed=%u applied=%u\n"), uSequence, uCorrelationId, uFailedCount, uFailedFromProcessed);
	}

	if (m_chunkedRemoveDownloadItems.IsEmpty() && !m_bChunkedRemoveDownloadPending && m_uChunkedRemoveDownloadPendingDiskDeletes == 0) {
		FinishChunkedRemoveDownloads();
		return;
	}

	theApp.QueueDownloadListCommandEvent(CemuleApp::ApplicationEventDownloadRemoveProgress, 0, GetChunkedRemoveDownloadProgressProcessed(), m_uChunkedRemoveDownloadFailed, m_uChunkedRemoveDownloadStale, m_uChunkedRemoveDownloadTotal, m_uChunkedRemoveDownloadSequence, m_uChunkedRemoveDownloadCorrelationId);
	UpdateChunkedRemoveDownloadOverlay();
}

void CDownloadListCtrl::UpdateChunkedRemoveDownloadOverlay()
{
	if (m_uChunkedRemoveDownloadTotal < BULK_OPERATION_MIN_ITEMS || (m_chunkedRemoveDownloadItems.IsEmpty() && !m_bChunkedRemoveDownloadPending && !m_bChunkedRemoveDownloadListStateBatchActive && !m_bChunkedRemoveDownloadQueueBulkActive && !m_bChunkedRemoveDownloadWaitingForDiskCleanup && m_uChunkedRemoveDownloadPendingDiskDeletes == 0)) {
		HideOperationOverlay();
		if (theApp.emuledlg != NULL)
			theApp.emuledlg->RefreshActiveBulkOperationOverlays();
		return;
	}

	const UINT uDone = GetChunkedRemoveDownloadDoneCount();
	CString strDetail;
	strDetail.Format(GetResString(_T("BULKOP_PROGRESS_DETAIL")), uDone, m_uChunkedRemoveDownloadTotal);
	UpdateOperationOverlay(GetResString(_T("BULKOP_DELETE_DOWNLOADS_TITLE")), strDetail, uDone, m_uChunkedRemoveDownloadTotal, true);
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
	ApplyChunkedRemoveDownloadVisibleItemCount(false);
}

void CDownloadListCtrl::UpdateChunkedDownloadStateOverlay()
{
	if (m_uChunkedDownloadStateTotal < BULK_OPERATION_MIN_ITEMS || (m_chunkedDownloadStateItems.IsEmpty() && !m_bChunkedDownloadStatePending && !m_bChunkedDownloadStateListStateBatchActive)) {
		HideOperationOverlay();
		if (theApp.emuledlg != NULL)
			theApp.emuledlg->RefreshActiveBulkOperationOverlays();
		return;
	}

	const UINT uRemaining = static_cast<UINT>(m_chunkedDownloadStateItems.GetCount());
	const UINT uDone = (m_uChunkedDownloadStateTotal >= uRemaining) ? (m_uChunkedDownloadStateTotal - uRemaining) : 0;
	CString strDetail;
	strDetail.Format(GetResString(_T("BULKOP_PROGRESS_DETAIL")), uDone, m_uChunkedDownloadStateTotal);
	UpdateOperationOverlay(GetResString(_T("BULKOP_UPDATE_DOWNLOADS_TITLE")), strDetail, uDone, m_uChunkedDownloadStateTotal, true);
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
}

void CDownloadListCtrl::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TimerChunkedRemoveDownload) {
		VERIFY(KillTimer(TimerChunkedRemoveDownload));
		OnProcessChunkedRemoveDownloads(0, 0);
		return;
	}
	if (nIDEvent == TimerChunkedDownloadState) {
		VERIFY(KillTimer(TimerChunkedDownloadState));
		OnProcessChunkedDownloadState(0, 0);
		return;
	}
	if (nIDEvent == TimerDownloadInspectorApply) {
		VERIFY(KillTimer(TimerDownloadInspectorApply));
		OnApplyDownloadInspectorResults(0, 0);
		return;
	}

	CMuleListCtrl::OnTimer(nIDEvent);
}


CDownloadListCtrl::SChunkedRemoveDownloadItem::SChunkedRemoveDownloadItem()
{
}

CDownloadListCtrl::SChunkedDownloadStateItem::SChunkedDownloadStateItem()
{
}

void CDownloadListCtrl::ClearChunkedRemoveDownloadItems()
{
	if (::IsWindow(m_hWnd))
		KillTimer(TimerChunkedRemoveDownload);
	m_bChunkedRemoveDownloadPending = false;
	HideOperationOverlay();
	while (!m_chunkedRemoveDownloadItems.IsEmpty())
		delete m_chunkedRemoveDownloadItems.RemoveHead();
	if (m_bChunkedRemoveDownloadVisibleItemsPending && ::IsWindow(m_hWnd))
		FlushChunkedRemoveVisibleItems();
	m_bChunkedRemoveDownloadVisibleItemsPending = false;
	m_bChunkedRemoveDownloadWaitingForDiskCleanup = false;
	m_uChunkedRemoveDownloadPendingDiskDeletes = 0;
	m_aChunkedRemoveStartNextCats.RemoveAll();
	if (m_bChunkedRemoveDownloadQueueBulkActive) {
		m_bChunkedRemoveDownloadQueueBulkActive = false;
		if (theApp.downloadqueue != NULL)
			theApp.downloadqueue->EndBulkRemoveDownloads();
	}

	if (m_bChunkedRemoveDownloadListStateBatchActive) {
		m_bChunkedRemoveDownloadListStateBatchActive = false;
		if (::IsWindow(m_hWnd))
			EndListStateBatch(0, kDownloadListRemoveBatchState, false);
	}
	ClearChunkedRemoveDownloadHiddenRows(true);
}

bool CDownloadListCtrl::HasQueuedChunkedRemoveDownloadItem(const SDownloadItemId &id) const
{
	if (!id.IsValid())
		return true;

	for (POSITION pos = m_chunkedRemoveDownloadItems.GetHeadPosition(); pos != NULL;) {
		const SChunkedRemoveDownloadItem *pQueuedItem = m_chunkedRemoveDownloadItems.GetNext(pos);
		if (pQueuedItem != NULL && pQueuedItem->m_id.EqualsHash(id.m_abyFileHash))
			return true;
	}
	return false;
}

bool CDownloadListCtrl::QueueChunkedRemoveDownloadItem(const CPartFile *pFile)
{
	if (pFile == NULL)
		return false;

	SDownloadItemId id;
	if (theApp.downloadqueue == NULL || !theApp.downloadqueue->GetDownloadItemId(pFile, id))
		id.SetFile(pFile);
	if (HasQueuedChunkedRemoveDownloadItem(id))
		return false;

	SChunkedRemoveDownloadItem *pItem = new SChunkedRemoveDownloadItem();
	pItem->m_id = id;
	m_chunkedRemoveDownloadItems.AddTail(pItem);
	return true;
}

bool CDownloadListCtrl::QueueChunkedRemoveDownloadHash(LPCTSTR pszHash)
{
	if (pszHash == NULL || pszHash[0] == _T('\0'))
		return false;

	SDownloadItemId id;
	id.Clear();
	if (!strmd4(CString(pszHash), id.m_abyFileHash))
		return false;

	SChunkedRemoveDownloadItem *pItem = new SChunkedRemoveDownloadItem();
	pItem->m_id = id;
	m_chunkedRemoveDownloadItems.AddTail(pItem);
	return true;
}

void CDownloadListCtrl::StartChunkedRemoveDownloads(CTypedPtrList<CPtrList, CPartFile*> &selectedList, bool bAddToCanceledMet, bool bDeleteCompletedFile)
{
	CStringArray astrItemHashes;
	while (!selectedList.IsEmpty()) {
		const CPartFile *pFile = selectedList.RemoveHead();
		if (pFile != NULL)
			astrItemHashes.Add(md4str(pFile->GetFileHash()));
	}
	StartChunkedRemoveDownloadsFromCommand(astrItemHashes, bAddToCanceledMet, bDeleteCompletedFile);
}

void CDownloadListCtrl::StartChunkedRemoveDownloadsFromCommand(const CStringArray &astrItemHashes, bool bAddToCanceledMet, bool bDeleteCompletedFile, uint64 uSequence, uint64 uCorrelationId)
{
	ClearChunkedRemoveDownloadItems();

	for (INT_PTR i = 0; i < astrItemHashes.GetSize(); ++i)
		QueueChunkedRemoveDownloadHash(astrItemHashes.GetAt(i));

	if (m_chunkedRemoveDownloadItems.IsEmpty())
		return;

	m_bChunkedRemoveDownloadAddToCanceledMet = bAddToCanceledMet;
	m_bChunkedRemoveDownloadDeleteCompletedFile = bDeleteCompletedFile;
	m_uChunkedRemoveDownloadSequence = uSequence;
	m_uChunkedRemoveDownloadCorrelationId = uCorrelationId;
	if (m_uChunkedRemoveDownloadSequence == 0) {
		static volatile LONG s_lLocalRemoveSequence = 0;
		LONG lLocalSequence = ::InterlockedIncrement(&s_lLocalRemoveSequence);
		if (lLocalSequence == 0)
			lLocalSequence = ::InterlockedIncrement(&s_lLocalRemoveSequence);
		m_uChunkedRemoveDownloadSequence = (static_cast<uint64>(::GetCurrentProcessId()) << 32) | static_cast<DWORD>(lLocalSequence);
	}
	if (m_uChunkedRemoveDownloadCorrelationId == 0)
		m_uChunkedRemoveDownloadCorrelationId = m_uChunkedRemoveDownloadSequence;
	m_uChunkedRemoveDownloadProcessed = 0;
	m_uChunkedRemoveDownloadStale = 0;
	m_uChunkedRemoveDownloadFailed = 0;
	m_uChunkedRemoveDownloadTotal = static_cast<UINT>(m_chunkedRemoveDownloadItems.GetCount());
	m_uChunkedRemoveDownloadPendingDiskDeletes = 0;
	m_bChunkedRemoveDownloadWaitingForDiskCleanup = false;
	m_dwChunkedRemoveDownloadStartedTick = ::GetTickCount();
	m_dwChunkedRemoveDownloadLastProgressTick = m_dwChunkedRemoveDownloadStartedTick;
	UpdateChunkedRemoveDownloadOverlay();

	BeginListStateBatch(0, kDownloadListRemoveBatchState);
	m_bChunkedRemoveDownloadListStateBatchActive = true;
	if (theApp.downloadqueue != NULL) {
		theApp.downloadqueue->BeginBulkRemoveDownloads();
		m_bChunkedRemoveDownloadQueueBulkActive = true;
	}
	DetachChunkedRemoveDownloadVisibleRows();

	if (!PostChunkedRemoveDownloadMessage()) {
		AddDebugLogLine(DLP_HIGH, false, _T("Chunked download remove aborted because the first continuation message could not be posted. remaining=%d\n"), static_cast<int>(m_chunkedRemoveDownloadItems.GetCount()));
		ClearChunkedRemoveDownloadItems();
		return;
	}

	if (theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL && ::IsWindow(theApp.emuledlg->sharedfileswnd->sharedfilesctrl.GetSafeHwnd()))
		theApp.emuledlg->sharedfileswnd->sharedfilesctrl.BeginBackendDownloadRemoveVisibleRows(astrItemHashes, m_uChunkedRemoveDownloadSequence, m_uChunkedRemoveDownloadCorrelationId);
}

CPartFile* CDownloadListCtrl::FindListedDownloadById(const SDownloadItemId &id) const
{
	if (!id.IsValid())
		return NULL;

	CPartFile *pQueueFile = theApp.downloadqueue != NULL ? theApp.downloadqueue->GetFileByItemId(id) : NULL;
	if (pQueueFile != NULL)
		return pQueueFile;

	const CString strHash(md4str(id.m_abyFileHash));
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct *pItem = it->second;
		if (pItem == NULL || pItem->type != FILE_TYPE || pItem->value == NULL || pItem->strOwnerHash.CompareNoCase(strHash) != 0)
			continue;

		return static_cast<CPartFile*>(pItem->value);
	}

	return NULL;
}

CPartFile* CDownloadListCtrl::ResolveDownloadItemForCommand(const SDownloadItemId &id) const
{
	return FindListedDownloadById(id);
}

void CDownloadListCtrl::RefreshAfterBackendDownloadCommand(UINT uAction)
{
	if (theApp.IsClosing() || !::IsWindow(m_hWnd))
		return;

	if (theApp.emuledlg != NULL && theApp.emuledlg->IsStartupLoadingDialogVisible()) {
		MarkDeferredReload();
		return;
	}

	if (IsChunkedRemoveDownloadSnapshotActive()) {
		MarkDeferredReload();
		DetachChunkedRemoveDownloadVisibleRows();
		ApplyChunkedRemoveDownloadVisibleItemCount(false);
		return;
	}

	ReloadList(true, kDownloadListViewState);
	if (uAction == static_cast<UINT>(ChunkedDownloadStateSetCategory))
		UpdateCurrentCategoryView();
	if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL)
		theApp.emuledlg->transferwnd->UpdateCatTabTitles();
	Invalidate(FALSE);
}

void CDownloadListCtrl::RefreshAfterDownloadListMembershipChanged()
{
	if (theApp.IsClosing() || !::IsWindow(m_hWnd))
		return;

	if (theApp.emuledlg != NULL && theApp.emuledlg->IsStartupLoadingDialogVisible()) {
		MarkDeferredReload();
		return;
	}

	if (IsChunkedRemoveDownloadSnapshotActive()) {
		MarkDeferredReload();
		DetachChunkedRemoveDownloadVisibleRows();
		ApplyChunkedRemoveDownloadVisibleItemCount(false);
		return;
	}

	ReloadList(false, kDownloadListViewState);
	if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL)
		theApp.emuledlg->transferwnd->UpdateCatTabTitles();
	Invalidate(FALSE);
}

bool CDownloadListCtrl::IsListedDownloadFileRow(int iItem) const
{
	return GetListedItemType(iItem) == FILE_TYPE;
}

bool CDownloadListCtrl::ChangeSelectedFilesCategoryFromUi(UINT uCategory)
{
	CStringArray astrItemHashes;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		SDownloadItemId id;
		const int iItem = GetNextSelectedItem(pos);
		if (TryGetListedDownloadItemId(iItem, id))
			astrItemHashes.Add(md4str(id.m_abyFileHash));
	}

	if (astrItemHashes.GetSize() == 0)
		return false;

	theApp.ExecuteDownloadListStateCommand(astrItemHashes, static_cast<UINT>(ChunkedDownloadStateSetCategory), static_cast<int>(uCategory));
	return true;
}


void CDownloadListCtrl::QueueChunkedRemoveFailureEvent(const SChunkedRemoveDownloadItem &item, LPCTSTR pszStage, LPCTSTR pszFilePath, DWORD dwError)
{
	CString strMessage;
	strMessage.Format(_T("%s hash=%s"), pszStage != NULL ? pszStage : _T("unknown"), (LPCTSTR)md4str(item.m_id.m_abyFileHash));
	theApp.QueueDownloadListCommandFailureEvent(CemuleApp::ApplicationEventDownloadRemoveItemFailed, 0, (LPCTSTR)strMessage, pszFilePath, dwError, m_uChunkedRemoveDownloadSequence, m_uChunkedRemoveDownloadCorrelationId);
}

bool CDownloadListCtrl::ProcessChunkedRemoveDownloadItem(const SChunkedRemoveDownloadItem &item)
{
	if (!theApp.GuardModelMutation(CemuleApp::ModelMutationDownloadQueue, _T("CDownloadListCtrl::ProcessChunkedRemoveDownloadItem"))) {
		++m_uChunkedRemoveDownloadFailed;
		QueueChunkedRemoveFailureEvent(item, _T("owner-guard"), NULL, ERROR_INVALID_FUNCTION);
		return false;
	}

	CPartFile *partfile = FindListedDownloadById(item.m_id);
	if (partfile == NULL) {
		++m_uChunkedRemoveDownloadStale;
		return false;
	}

	HideSources(partfile);
	switch (partfile->GetStatus()) {
	case PS_WAITINGFORHASH:
	case PS_HASHING:
	case PS_COMPLETING:
		++m_uChunkedRemoveDownloadStale;
		return false;
	case PS_COMPLETE:
		if (m_bChunkedRemoveDownloadDeleteCompletedFile) {
			const CString strFilePath = partfile->GetFilePath();
			const bool bDelSucc = ShellDeleteFile(strFilePath);
			if (bDelSucc)
				theApp.sharedfiles->RemoveFile(partfile, true);
			else {
				const DWORD dwError = ::GetLastError();
				AddDebugLogLine(DLP_HIGH, false, _T("Chunked download remove failed to delete completed file. hash=%s error=%lu path=%s\n"), (LPCTSTR)md4str(item.m_id.m_abyFileHash), dwError, (LPCTSTR)strFilePath);
				QueueChunkedRemoveFailureEvent(item, _T("delete-completed-file"), strFilePath, dwError);
				++m_uChunkedRemoveDownloadFailed;
				return false;
			}
		}
		RemoveFile(partfile);
		break;
	default:
		if (partfile->GetCategory() != 0 && theApp.downloadqueue != NULL) {
			if (m_bChunkedRemoveDownloadQueueBulkActive)
				QueueChunkedRemoveStartNextCategory(partfile->GetCategory());
			else
				theApp.downloadqueue->StartNextFileIfPrefs(partfile->GetCategory());
		}
		if (partfile->DeletePartFile(m_bChunkedRemoveDownloadAddToCanceledMet, m_uChunkedRemoveDownloadSequence, m_uChunkedRemoveDownloadCorrelationId, false)) {
			++m_uChunkedRemoveDownloadPendingDiskDeletes;
			m_bChunkedRemoveDownloadWaitingForDiskCleanup = true;
		}
		break;
	}

	++m_uChunkedRemoveDownloadProcessed;
	return true;
}

void CDownloadListCtrl::FlushChunkedRemoveVisibleItems()
{
	if (!m_bChunkedRemoveDownloadVisibleItemsPending)
		return;

	m_bChunkedRemoveDownloadVisibleItemsPending = false;
	if (theApp.IsClosing() || !::IsWindow(m_hWnd))
		return;
	if (theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible()) {
		MarkDeferredReload();
		return;
	}
	if (IsChunkedRemoveDownloadSnapshotActive()) {
		DetachChunkedRemoveDownloadVisibleRows();
		ApplyChunkedRemoveDownloadVisibleItemCount(false);
		return;
	}

	SaveListState(0, kDownloadListRemoveBatchState);
	SetRedraw(false);
	size_t uWrite = 0;
	UINT uListedFiles = 0;
	for (size_t uRead = 0; uRead < m_ListedItemsVector.size(); ++uRead) {
		CtrlItem_Struct *pItem = m_ListedItemsVector[uRead];
		if (pItem == NULL)
			continue;
		if (uWrite != uRead)
			m_ListedItemsVector[uWrite] = pItem;
		if (pItem->type == FILE_TYPE)
			++uListedFiles;
		++uWrite;
	}
	m_ListedItemsVector.resize(uWrite);
	m_uListedFilesCount = uListedFiles;
	RebuildListedItemsMap();
	SetItemCountAndKeepPageFilled(m_ListedItemsVector.size(), 0);
	RestoreListState(0, kDownloadListRemoveBatchState, false);
	SetRedraw(true);
	Invalidate(FALSE);
	if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL && theApp.emuledlg->transferwnd->m_pwndTransfer != NULL)
		theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
}

void CDownloadListCtrl::QueueChunkedRemoveStartNextCategory(UINT uCategory)
{
	if (uCategory == 0)
		return;
	for (INT_PTR i = 0; i < m_aChunkedRemoveStartNextCats.GetSize(); ++i) {
		if (m_aChunkedRemoveStartNextCats.GetAt(i) == uCategory)
			return;
	}
	m_aChunkedRemoveStartNextCats.Add(uCategory);
}

void CDownloadListCtrl::FinishChunkedRemoveDownloads()
{
	if (m_bChunkedRemoveDownloadQueueBulkActive) {
		m_bChunkedRemoveDownloadQueueBulkActive = false;
		if (theApp.downloadqueue != NULL)
			theApp.downloadqueue->EndBulkRemoveDownloads();
	}

	if (theApp.downloadqueue != NULL) {
		for (INT_PTR i = 0; i < m_aChunkedRemoveStartNextCats.GetSize(); ++i)
			theApp.downloadqueue->StartNextFileIfPrefs(static_cast<int>(m_aChunkedRemoveStartNextCats.GetAt(i)));
	}
	m_aChunkedRemoveStartNextCats.RemoveAll();

	const bool bHadHiddenRows = m_mapChunkedRemoveDownloadHiddenRows.GetCount() != 0;
	FlushChunkedRemoveVisibleItems();
	if (m_bChunkedRemoveDownloadListStateBatchActive) {
		m_bChunkedRemoveDownloadListStateBatchActive = false;
		EndListStateBatch(0, kDownloadListRemoveBatchState, false);
	}
	if (bHadHiddenRows)
		ClearChunkedRemoveDownloadHiddenRows(true);

	AutoSelectItem();
	if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL)
		theApp.emuledlg->transferwnd->UpdateCatTabTitles();
	Invalidate(FALSE);

	m_bChunkedRemoveDownloadWaitingForDiskCleanup = false;
	m_uChunkedRemoveDownloadPendingDiskDeletes = 0;

	AddDebugLogLine(DLP_LOW, false, _T("Chunked download remove completed. processed=%u stale=%u failed=%u elapsed=%u\n"), m_uChunkedRemoveDownloadProcessed, m_uChunkedRemoveDownloadStale, m_uChunkedRemoveDownloadFailed, static_cast<DWORD>(::GetTickCount() - m_dwChunkedRemoveDownloadStartedTick));
	theApp.QueueDownloadListCommandEvent(CemuleApp::ApplicationEventDownloadRemoveCompleted, 0, m_uChunkedRemoveDownloadProcessed, m_uChunkedRemoveDownloadFailed, m_uChunkedRemoveDownloadStale, m_uChunkedRemoveDownloadTotal, m_uChunkedRemoveDownloadSequence, m_uChunkedRemoveDownloadCorrelationId, CemuleApp::BackendCommandSourceUi, CemuleApp::BackendCommandOrderingDownloadList, _T("download-list:chunked-remove"));
	m_uChunkedRemoveDownloadTotal = 0;
	m_uChunkedRemoveDownloadPendingDiskDeletes = 0;
	if (m_bMirroredSearchDownloadOverlayActive)
		RefreshMirroredSearchDownloadOverlay();
	else
		HideOperationOverlay();
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
}

bool CDownloadListCtrl::PostChunkedRemoveDownloadMessage()
{
	if (m_bChunkedRemoveDownloadPending || theApp.IsClosing() || !::IsWindow(m_hWnd))
		return false;

	m_bChunkedRemoveDownloadPending = SetTimer(TimerChunkedRemoveDownload, 1, NULL) != 0;
	if (!m_bChunkedRemoveDownloadPending)
		m_bChunkedRemoveDownloadPending = PostMessage(WM_DOWNLOADLISTCTRL_CHUNKED_REMOVE, 0, 0) != FALSE;
	return m_bChunkedRemoveDownloadPending;
}

LRESULT CDownloadListCtrl::OnProcessChunkedRemoveDownloads(WPARAM, LPARAM)
{
	m_bChunkedRemoveDownloadPending = false;
	if (theApp.IsClosing() || !::IsWindow(m_hWnd)) {
		ClearChunkedRemoveDownloadItems();
		return 0;
	}
	if (m_chunkedRemoveDownloadItems.IsEmpty() && !m_bChunkedRemoveDownloadListStateBatchActive && !m_bChunkedRemoveDownloadQueueBulkActive)
		return 0;

	const DWORD dwSliceStartTick = ::GetTickCount();
	DWORD dwSliceBudgetMs = 8;
	UINT uMaxItemsPerSlice = 512;
	GetChunkedRemoveDownloadSliceLimits(dwSliceBudgetMs, uMaxItemsPerSlice);
	UINT uProcessedInSlice = 0;
	while (!m_chunkedRemoveDownloadItems.IsEmpty()) {
		SChunkedRemoveDownloadItem *pItem = m_chunkedRemoveDownloadItems.RemoveHead();
		if (pItem != NULL) {
			ProcessChunkedRemoveDownloadItem(*pItem);
			delete pItem;
		}
		++uProcessedInSlice;

		const DWORD dwNow = ::GetTickCount();
		if (static_cast<DWORD>(dwNow - m_dwChunkedRemoveDownloadLastProgressTick) >= theApp.GetTimeBudgetedProgressTraceMs(CemuleApp::TimeBudgetDownloadRemove)) {
			m_dwChunkedRemoveDownloadLastProgressTick = dwNow;
			AddDebugLogLine(DLP_VERYLOW, false, _T("Chunked download remove progress. processed=%u stale=%u failed=%u remaining=%d\n"), m_uChunkedRemoveDownloadProcessed, m_uChunkedRemoveDownloadStale, m_uChunkedRemoveDownloadFailed, static_cast<int>(m_chunkedRemoveDownloadItems.GetCount()));
			theApp.QueueDownloadListCommandEvent(CemuleApp::ApplicationEventDownloadRemoveProgress, 0, GetChunkedRemoveDownloadProgressProcessed(), m_uChunkedRemoveDownloadFailed, m_uChunkedRemoveDownloadStale, m_uChunkedRemoveDownloadTotal, m_uChunkedRemoveDownloadSequence, m_uChunkedRemoveDownloadCorrelationId);
		}

		if ((uProcessedInSlice & 0x0F) == 0)
			GetChunkedRemoveDownloadSliceLimits(dwSliceBudgetMs, uMaxItemsPerSlice);
		const DWORD dwElapsed = static_cast<DWORD>(::GetTickCount() - dwSliceStartTick);
		if (uProcessedInSlice >= uMaxItemsPerSlice || (uProcessedInSlice != 0 && dwElapsed >= dwSliceBudgetMs))
			break;
	}

	if (m_bChunkedRemoveDownloadVisibleItemsPending && m_mapChunkedRemoveDownloadHiddenRows.GetCount() == 0)
		FlushChunkedRemoveVisibleItems();

	DWORD dwSliceElapsed = 0;
	if (theApp.IsTimeBudgetHardExceeded(dwSliceStartTick, CemuleApp::TimeBudgetDownloadRemove, &dwSliceElapsed))
		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetDownloadRemove, _T("OnProcessChunkedRemoveDownloads"), dwSliceElapsed, uProcessedInSlice, m_chunkedRemoveDownloadItems.GetCount());

	if (!m_chunkedRemoveDownloadItems.IsEmpty()) {
		UpdateChunkedRemoveDownloadOverlay();
		if (!PostChunkedRemoveDownloadMessage()) {
			AddDebugLogLine(DLP_HIGH, false, _T("Chunked download remove aborted because the continuation message could not be posted. processed=%u remaining=%d\n"), m_uChunkedRemoveDownloadProcessed, static_cast<int>(m_chunkedRemoveDownloadItems.GetCount()));
			ClearChunkedRemoveDownloadItems();
			AutoSelectItem();
			if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL)
				theApp.emuledlg->transferwnd->UpdateCatTabTitles();
			Invalidate(FALSE);
		}
	} else
		FinishChunkedRemoveDownloads();
	return 0;
}


void CDownloadListCtrl::ClearChunkedDownloadStateItems()
{
	if (::IsWindow(m_hWnd))
		KillTimer(TimerChunkedDownloadState);
	m_bChunkedDownloadStatePending = false;
	HideOperationOverlay();
	while (!m_chunkedDownloadStateItems.IsEmpty())
		delete m_chunkedDownloadStateItems.RemoveHead();

	if (m_bChunkedDownloadStateListStateBatchActive) {
		m_bChunkedDownloadStateListStateBatchActive = false;
		if (::IsWindow(m_hWnd))
			EndListStateBatch(0, kDownloadListViewState, false);
	}
}

bool CDownloadListCtrl::HasQueuedChunkedDownloadStateItem(const SDownloadItemId &id) const
{
	if (!id.IsValid())
		return true;

	for (POSITION pos = m_chunkedDownloadStateItems.GetHeadPosition(); pos != NULL;) {
		const SChunkedDownloadStateItem *pQueuedItem = m_chunkedDownloadStateItems.GetNext(pos);
		if (pQueuedItem != NULL && pQueuedItem->m_id.EqualsHash(id.m_abyFileHash))
			return true;
	}
	return false;
}

bool CDownloadListCtrl::QueueChunkedDownloadStateItem(const CPartFile *pFile)
{
	if (pFile == NULL)
		return false;

	SDownloadItemId id;
	if (theApp.downloadqueue == NULL || !theApp.downloadqueue->GetDownloadItemId(pFile, id))
		id.SetFile(pFile);
	if (HasQueuedChunkedDownloadStateItem(id))
		return false;

	SChunkedDownloadStateItem *pItem = new SChunkedDownloadStateItem();
	pItem->m_id = id;
	m_chunkedDownloadStateItems.AddTail(pItem);
	return true;
}

bool CDownloadListCtrl::QueueChunkedDownloadStateHash(LPCTSTR pszHash)
{
	if (pszHash == NULL || pszHash[0] == _T('\0'))
		return false;

	SDownloadItemId id;
	id.Clear();
	if (!strmd4(CString(pszHash), id.m_abyFileHash) || HasQueuedChunkedDownloadStateItem(id))
		return false;

	SChunkedDownloadStateItem *pItem = new SChunkedDownloadStateItem();
	pItem->m_id = id;
	m_chunkedDownloadStateItems.AddTail(pItem);
	return true;
}

void CDownloadListCtrl::StartChunkedDownloadStateChange(CTypedPtrList<CPtrList, CPartFile*> &selectedList, EChunkedDownloadStateAction eAction, int iActionValue)
{
	CStringArray astrItemHashes;
	while (!selectedList.IsEmpty()) {
		const CPartFile *pFile = selectedList.RemoveHead();
		if (pFile != NULL)
			astrItemHashes.Add(md4str(pFile->GetFileHash()));
	}
	StartChunkedDownloadStateChangeFromCommand(astrItemHashes, static_cast<UINT>(eAction), iActionValue);
}

void CDownloadListCtrl::StartChunkedDownloadStateChangeFromCommand(const CStringArray &astrItemHashes, UINT uAction, int iActionValue, uint64 uSequence, uint64 uCorrelationId)
{
	if (uAction > static_cast<UINT>(ChunkedDownloadStateClearCompleted)) {
		AddDebugLogLine(DLP_HIGH, false, _T("Chunked download state command rejected because action is invalid. action=%u total=%d\n"), uAction, static_cast<int>(astrItemHashes.GetSize()));
		return;
	}

	ClearChunkedDownloadStateItems();

	for (INT_PTR i = 0; i < astrItemHashes.GetSize(); ++i)
		QueueChunkedDownloadStateHash(astrItemHashes.GetAt(i));

	if (m_chunkedDownloadStateItems.IsEmpty())
		return;

	m_eChunkedDownloadStateAction = static_cast<EChunkedDownloadStateAction>(uAction);
	m_iChunkedDownloadStateValue = iActionValue;
	m_uChunkedDownloadStateSequence = uSequence;
	m_uChunkedDownloadStateCorrelationId = uCorrelationId;
	m_uChunkedDownloadStateProcessed = 0;
	m_uChunkedDownloadStateStale = 0;
	m_uChunkedDownloadStateFailed = 0;
	m_uChunkedDownloadStateTotal = static_cast<UINT>(m_chunkedDownloadStateItems.GetCount());
	m_dwChunkedDownloadStateStartedTick = ::GetTickCount();
	m_dwChunkedDownloadStateLastProgressTick = m_dwChunkedDownloadStateStartedTick;
	UpdateChunkedDownloadStateOverlay();

	BeginListStateBatch(0, kDownloadListViewState);
	m_bChunkedDownloadStateListStateBatchActive = true;

	if (!PostChunkedDownloadStateMessage()) {
		AddDebugLogLine(DLP_HIGH, false, _T("Chunked download state change aborted because the first continuation message could not be posted. remaining=%d\n"), static_cast<int>(m_chunkedDownloadStateItems.GetCount()));
		ClearChunkedDownloadStateItems();
	}
}

void CDownloadListCtrl::QueueChunkedDownloadStateFailureEvent(const SChunkedDownloadStateItem &item, LPCTSTR pszStage)
{
	CString strMessage;
	strMessage.Format(_T("%s hash=%s"), pszStage != NULL ? pszStage : _T("unknown"), (LPCTSTR)md4str(item.m_id.m_abyFileHash));
	theApp.QueueDownloadListCommandFailureEvent(CemuleApp::ApplicationEventDownloadStateItemFailed, static_cast<UINT>(m_eChunkedDownloadStateAction), (LPCTSTR)strMessage, NULL, ERROR_INVALID_FUNCTION, m_uChunkedDownloadStateSequence, m_uChunkedDownloadStateCorrelationId);
}

bool CDownloadListCtrl::ProcessChunkedDownloadStateItem(const SChunkedDownloadStateItem &item)
{
	if (!theApp.GuardModelMutation(CemuleApp::ModelMutationPartFile, _T("CDownloadListCtrl::ProcessChunkedDownloadStateItem"))) {
		++m_uChunkedDownloadStateFailed;
		QueueChunkedDownloadStateFailureEvent(item, _T("owner-guard"));
		return false;
	}

	if (m_eChunkedDownloadStateAction == ChunkedDownloadStateClearCompleted) {
		const CString strHash(md4str(item.m_id.m_abyFileHash));
		bool bRemovedCompleted = false;
		for (;;) {
			CPartFile *pCompletedFile = NULL;
			for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
				const CtrlItem_Struct *pListItem = it->second;
				if (pListItem == NULL || pListItem->type != FILE_TYPE || pListItem->value == NULL || pListItem->strOwnerHash.CompareNoCase(strHash) != 0)
					continue;
				CPartFile *pFile = static_cast<CPartFile*>(pListItem->value);
				if (pFile != NULL && !pFile->IsPartFile() && (!IsFilteredOut(pFile) || m_iChunkedDownloadStateValue == -1)) {
					pCompletedFile = pFile;
					break;
				}
			}
			if (pCompletedFile == NULL)
				break;
			RemoveFile(pCompletedFile);
			bRemovedCompleted = true;
		}
		if (!bRemovedCompleted) {
			++m_uChunkedDownloadStateStale;
			return false;
		}
		++m_uChunkedDownloadStateProcessed;
		return true;
	}

	CPartFile *partfile = FindListedDownloadById(item.m_id);
	if (partfile == NULL) {
		++m_uChunkedDownloadStateStale;
		return false;
	}

	switch (m_eChunkedDownloadStateAction) {
	case ChunkedDownloadStatePermissionDefault:
		partfile->SetPermissions(-1);
		break;
	case ChunkedDownloadStatePermissionNone:
		partfile->SetPermissions(PERM_NOONE);
		break;
	case ChunkedDownloadStatePermissionFriends:
		partfile->SetPermissions(PERM_FRIENDS);
		break;
	case ChunkedDownloadStatePermissionAll:
		partfile->SetPermissions(PERM_ALL);
		break;
	case ChunkedDownloadStatePriorityHigh:
		partfile->SetAutoDownPriority(false);
		partfile->SetDownPriority(PR_HIGH);
		break;
	case ChunkedDownloadStatePriorityLow:
		partfile->SetAutoDownPriority(false);
		partfile->SetDownPriority(PR_LOW);
		break;
	case ChunkedDownloadStatePriorityNormal:
		partfile->SetAutoDownPriority(false);
		partfile->SetDownPriority(PR_NORMAL);
		break;
	case ChunkedDownloadStatePriorityAuto:
		partfile->SetAutoDownPriority(true);
		partfile->SetDownPriority(PR_HIGH);
		break;
	case ChunkedDownloadStatePause:
		if (partfile->CanPauseFile())
			partfile->PauseFile();
		break;
	case ChunkedDownloadStateResume:
		if (partfile->CanResumeFile()) {
			if (partfile->GetStatus() == PS_INSUFFICIENT)
				partfile->ResumeFileInsufficient();
			else
				partfile->ResumeFile();
		}
		break;
	case ChunkedDownloadStateStop:
		if (partfile->CanStopFile()) {
			HideSources(partfile);
			partfile->StopFile(false);
		}
		break;
	case ChunkedDownloadStateSetSourceLimit:
		partfile->SetPrivateMaxSources(m_iChunkedDownloadStateValue);
		partfile->UpdateDisplayedInfo(true);
		break;
	case ChunkedDownloadStateSetCategory:
		partfile->SetCategory(m_iChunkedDownloadStateValue);
		partfile->UpdateDisplayedInfo(true);
		break;
	case ChunkedDownloadStateSetPauseOnPreview:
		if (partfile->IsPreviewableFileType() && !partfile->IsReadyForPreview())
			partfile->SetPauseOnPreview(m_iChunkedDownloadStateValue != 0);
		break;
	case ChunkedDownloadStateToggleAutoRenameToMajorityName:
		if (IsAutoRenameToMajorityNameModeEnabled() && partfile->GetStatus() != PS_COMPLETE && partfile->GetStatus() != PS_COMPLETING)
			partfile->ToggleAutoRenameToMajorityName();
		break;
	case ChunkedDownloadStateCleanupFilename:
		if (partfile->IsPartFile()) {
			HideSources(partfile);
			partfile->SetAutoRenameToMajorityName(false);
			partfile->SetFileName(CleanupFilename(partfile->GetFileName()));
		}
		break;
	case ChunkedDownloadStateClearCompleted:
		if (!partfile->IsPartFile())
			RemoveFile(partfile);
		break;
	case ChunkedDownloadStateSetFileName:
	case ChunkedDownloadStateTogglePreviewPriority:
	case ChunkedDownloadStateImportParts:
		++m_uChunkedDownloadStateStale;
		QueueChunkedDownloadStateFailureEvent(item, _T("legacy-ui-bridge-unsupported-action"));
		return false;
	default:
		++m_uChunkedDownloadStateStale;
		return false;
	}

	++m_uChunkedDownloadStateProcessed;
	return true;
}

void CDownloadListCtrl::FinishChunkedDownloadStateChange()
{
	if (m_bChunkedDownloadStateListStateBatchActive) {
		m_bChunkedDownloadStateListStateBatchActive = false;
		EndListStateBatch(0, kDownloadListViewState, false);
	}

	AutoSelectItem();
	if (m_eChunkedDownloadStateAction == ChunkedDownloadStateSetCategory)
		UpdateCurrentCategoryView();
	if (m_eChunkedDownloadStateAction == ChunkedDownloadStateStop && theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL)
		theApp.emuledlg->transferwnd->UpdateCatTabTitles();
	else if ((m_eChunkedDownloadStateAction == ChunkedDownloadStateSetCategory || m_eChunkedDownloadStateAction == ChunkedDownloadStateClearCompleted) && theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL && thePrefs.ShowCatTabInfos())
		theApp.emuledlg->transferwnd->UpdateCatTabTitles();
	if (m_eChunkedDownloadStateAction == ChunkedDownloadStateClearCompleted)
		ReloadList(false, kDownloadListViewState);
	else
		Invalidate(FALSE);

	AddDebugLogLine(DLP_LOW, false, _T("Chunked download state change completed. action=%u value=%d processed=%u failed=%u stale=%u elapsed=%u\n"), static_cast<UINT>(m_eChunkedDownloadStateAction), m_iChunkedDownloadStateValue, m_uChunkedDownloadStateProcessed, m_uChunkedDownloadStateFailed, m_uChunkedDownloadStateStale, static_cast<DWORD>(::GetTickCount() - m_dwChunkedDownloadStateStartedTick));
	theApp.QueueDownloadListCommandEvent(CemuleApp::ApplicationEventDownloadStateCompleted, static_cast<UINT>(m_eChunkedDownloadStateAction), m_uChunkedDownloadStateProcessed, m_uChunkedDownloadStateFailed, m_uChunkedDownloadStateStale, m_uChunkedDownloadStateTotal, m_uChunkedDownloadStateSequence, m_uChunkedDownloadStateCorrelationId);
	m_uChunkedDownloadStateTotal = 0;
	if (m_bMirroredSearchDownloadOverlayActive)
		RefreshMirroredSearchDownloadOverlay();
	else
		HideOperationOverlay();
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
}

bool CDownloadListCtrl::PostChunkedDownloadStateMessage()
{
	if (m_bChunkedDownloadStatePending || theApp.IsClosing() || !::IsWindow(m_hWnd))
		return false;

	m_bChunkedDownloadStatePending = SetTimer(TimerChunkedDownloadState, 1, NULL) != 0;
	if (!m_bChunkedDownloadStatePending)
		m_bChunkedDownloadStatePending = PostMessage(WM_DOWNLOADLISTCTRL_CHUNKED_STATE, 0, 0) != FALSE;
	return m_bChunkedDownloadStatePending;
}

LRESULT CDownloadListCtrl::OnProcessChunkedDownloadState(WPARAM, LPARAM)
{
	m_bChunkedDownloadStatePending = false;
	if (theApp.IsClosing() || !::IsWindow(m_hWnd)) {
		ClearChunkedDownloadStateItems();
		return 0;
	}
	if (m_chunkedDownloadStateItems.IsEmpty() && !m_bChunkedDownloadStateListStateBatchActive)
		return 0;

	const DWORD dwSliceStartTick = ::GetTickCount();
	UINT uProcessedInSlice = 0;
	while (!m_chunkedDownloadStateItems.IsEmpty()) {
		SChunkedDownloadStateItem *pItem = m_chunkedDownloadStateItems.RemoveHead();
		if (pItem != NULL) {
			ProcessChunkedDownloadStateItem(*pItem);
			delete pItem;
		}
		++uProcessedInSlice;

		const DWORD dwNow = ::GetTickCount();
		if (static_cast<DWORD>(dwNow - m_dwChunkedDownloadStateLastProgressTick) >= theApp.GetTimeBudgetedProgressTraceMs(CemuleApp::TimeBudgetDownloadState)) {
			m_dwChunkedDownloadStateLastProgressTick = dwNow;
			AddDebugLogLine(DLP_VERYLOW, false, _T("Chunked download state change progress. action=%u processed=%u failed=%u stale=%u remaining=%d\n"), static_cast<UINT>(m_eChunkedDownloadStateAction), m_uChunkedDownloadStateProcessed, m_uChunkedDownloadStateFailed, m_uChunkedDownloadStateStale, static_cast<int>(m_chunkedDownloadStateItems.GetCount()));
			theApp.QueueDownloadListCommandEvent(CemuleApp::ApplicationEventDownloadStateProgress, static_cast<UINT>(m_eChunkedDownloadStateAction), m_uChunkedDownloadStateProcessed, m_uChunkedDownloadStateFailed, m_uChunkedDownloadStateStale, m_uChunkedDownloadStateTotal, m_uChunkedDownloadStateSequence, m_uChunkedDownloadStateCorrelationId);
		}

		if (theApp.IsTimeBudgetExceeded(dwSliceStartTick, CemuleApp::TimeBudgetDownloadState))
			break;
	}

	DWORD dwSliceElapsed = 0;
	if (theApp.IsTimeBudgetHardExceeded(dwSliceStartTick, CemuleApp::TimeBudgetDownloadState, &dwSliceElapsed))
		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetDownloadState, _T("OnProcessChunkedDownloadState"), dwSliceElapsed, uProcessedInSlice, m_chunkedDownloadStateItems.GetCount());

	if (!m_chunkedDownloadStateItems.IsEmpty()) {
		UpdateChunkedDownloadStateOverlay();
		if (!PostChunkedDownloadStateMessage()) {
			AddDebugLogLine(DLP_HIGH, false, _T("Chunked download state change aborted because the continuation message could not be posted. action=%u processed=%u remaining=%d\n"), static_cast<UINT>(m_eChunkedDownloadStateAction), m_uChunkedDownloadStateProcessed, static_cast<int>(m_chunkedDownloadStateItems.GetCount()));
			ClearChunkedDownloadStateItems();
			AutoSelectItem();
			if (m_eChunkedDownloadStateAction == ChunkedDownloadStateSetCategory)
				UpdateCurrentCategoryView();
			if (m_eChunkedDownloadStateAction == ChunkedDownloadStateStop && theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL)
				theApp.emuledlg->transferwnd->UpdateCatTabTitles();
			else if ((m_eChunkedDownloadStateAction == ChunkedDownloadStateSetCategory || m_eChunkedDownloadStateAction == ChunkedDownloadStateClearCompleted) && theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL && thePrefs.ShowCatTabInfos())
				theApp.emuledlg->transferwnd->UpdateCatTabTitles();
			Invalidate(FALSE);
		}
	} else
		FinishChunkedDownloadStateChange();
	return 0;
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

	// Alignment rule: left for text, dates, and status labels; right for sizes, rates, counts, durations, and percentages.
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
	InsertColumn(16,	EMPTY, LVCFMT_RIGHT,	60);

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

void CDownloadListCtrl::RefreshThemeColors()
{
	CMuleListCtrl::RefreshThemeColors();
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
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

void CDownloadListCtrl::MarkDeferredReload()
{
	m_bDeferredReload = true;
}

void CDownloadListCtrl::FlushDeferredReload(const EListStateField LsfFlag)
{
	if (!m_bDeferredReload)
		return;

	if (IsChunkedRemoveDownloadSnapshotActive()) {
		DetachChunkedRemoveDownloadVisibleRows();
		ApplyChunkedRemoveDownloadVisibleItemCount(false);
		return;
	}

	ReloadList(false, LsfFlag);
}

void CDownloadListCtrl::FlushBulkAddListUpdate(const EListStateField LsfFlag)
{
	if (theApp.IsClosing() || !::IsWindow(m_hWnd))
		return;
	if (theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible()) {
		MarkDeferredReload();
		return;
	}

	ReloadList(false, LsfFlag);
}

void CDownloadListCtrl::RefreshBulkAddDisplayCounts()
{
	if (theApp.IsClosing() || !::IsWindow(m_hWnd))
		return;
	if (theApp.emuledlg == NULL || theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible()) {
		MarkDeferredReload();
		return;
	}

	RequestTransferListRedraw();
	UpdateDownloadListItemCount(*this, m_ListedItemsVector.size());
}

void CDownloadListCtrl::AddFile(CPartFile* toadd, bool bBatchVisibleListUpdate)
{
	if (theApp.IsClosing() || !toadd)
		return;

	// The same file shall be added only once
	if (!AddFileToListModel(toadd)) {
		ASSERT(0);
		MarkDeferredReload();
		return;
	}
	CtrlItem_Struct* newitem = FindFileItem(toadd);
	if (newitem == NULL) {
		MarkDeferredReload();
		return;
	}

	if (bBatchVisibleListUpdate) {
		if (theApp.emuledlg != NULL && theApp.emuledlg->IsStartupLoadingDialogVisible()) {
			MarkDeferredReload();
			return;
		}
		if (theApp.emuledlg->activewnd == theApp.emuledlg->transferwnd && IsWindowVisible() && !IsFilteredOut(toadd)) {
			m_ListedItemsVector.push_back(newitem);
			m_ListedItemsMap[newitem] = static_cast<int>(m_ListedItemsVector.size() - 1);
			++m_uListedFilesCount;
			UpdateDownloadListItemCount(*this, m_ListedItemsVector.size());
			RequestTransferListRedrawForRange(static_cast<int>(m_ListedItemsVector.size() - 1), static_cast<int>(m_ListedItemsVector.size() - 1));
			if (!theApp.emuledlg->IsInitializing() && theApp.emuledlg->transferwnd != NULL && theApp.emuledlg->transferwnd->m_pwndTransfer != NULL)
				theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
		} else
			MarkDeferredReload();
		return;
	}

	if (theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible() || IsFilteredOut(toadd)) {
		MarkDeferredReload();
		return;
	}

	SaveListState(0, kDownloadListViewState); // Save selections and scroll state
	SetRedraw(false); // Suspend painting
	m_ListedItemsVector.insert(m_ListedItemsVector.begin() + (int)m_ListedItemsVector.size(), newitem); // Add the new item to the vector.
	++m_uListedFilesCount;
	const bool bOldRawSortState = m_bRawSortInProgress;
	m_bRawSortInProgress = true;
	CombinedSort(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), SortFunc); // Keep current sort order.
	m_bRawSortInProgress = bOldRawSortState;
	RebuildListedItemsMap(); // Rebuild the map after sorting.
	RequestTransferListRedraw();
	UpdateDownloadListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list.
	RestoreListState(0, kDownloadListViewState, false); // Restore selections and scroll state
	SetRedraw(true); // Resume painting

	if (!theApp.emuledlg->IsInitializing())
		theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount(); // Refresh the file count.
}

void CDownloadListCtrl::AddSource(CPartFile* owner, CUpDownClient* source, bool notavailable)
{
	if (theApp.IsClosing() || !owner || !source)
		return;
	if (IsChunkedRemoveDownloadSnapshotActive()) {
		MarkDeferredReload();
		return;
	}

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
	if (bFound) {
		if (theApp.downloadqueue != NULL && theApp.downloadqueue->IsBulkAddingDownloads()) {
			MarkDeferredReload();
			return;
		}

		if (theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible()) {
			MarkDeferredReload();
			return;
		}

		for (ListItems::const_iterator it = m_ListItems.lower_bound(source); it != m_ListItems.end() && it->first == source; ++it) {
			CtrlItem_Struct* cur_item = it->second;
			int iVectorIndex;
			if (cur_item != NULL && m_ListedItemsMap.Lookup(cur_item, iVectorIndex)) {
				RequestTransferListRedrawForRange(iVectorIndex, iVectorIndex);
			}
		}
		MaintainSortOrderAfterUpdate();
		return;
	}

	// Parent file entry (must exist).
	ListItems::const_iterator itOwner = m_ListItems.find(owner);
	if (itOwner == m_ListItems.end())
		return;
	CtrlItem_Struct* ownerItem = itOwner->second;

	// Create source rows lazily. Collapsed owners are completed from the live model when expanded.
	if (!owner->srcarevisible) {
		MarkDeferredReload();
		return;
	}

	// Create new Item
	CtrlItem_Struct* newitem = new CtrlItem_Struct;
	newitem->type = itemtype;
	newitem->owner = owner;
	newitem->value = source;
	newitem->parent = ownerItem; // cross link to the owner
	newitem->strOwnerHash = md4str(owner->GetFileHash());
	newitem->dwUpdated = 0;

	m_ListItems.emplace(source, newitem);

	if (theApp.downloadqueue != NULL && theApp.downloadqueue->IsBulkAddingDownloads()) {
		MarkDeferredReload();
		return;
	}

	// Only show if parents sources branch is expanded and passes current filter.
	if (theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible() || IsFilteredOut(owner)) {
		MarkDeferredReload();
		return;
	}

	int iParentIndex = -1;
	if (!m_ListedItemsMap.Lookup(ownerItem, iParentIndex)) {
		MarkDeferredReload();
		return;
	}

	SaveListState(0, kDownloadListViewState); // Save selections and scroll state
	SetRedraw(false); // Suspend painting

	if (RemoveVisibleSourcesForOwner(owner))
		RebuildListedItemsMap();
	if (m_ListedItemsMap.Lookup(ownerItem, iParentIndex))
		InsertSortedVisibleSourcesForFile(owner, iParentIndex);
	RebuildListedItemsMap(); // Rebuild the map after inserting.
	RequestTransferListRedraw();
	UpdateDownloadListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list.

	RestoreListState(0, kDownloadListViewState, false); // Restore selections and scroll state
	SetRedraw(true); // Resume painting

}

void CDownloadListCtrl::RemoveSource(CUpDownClient* source, CPartFile* owner)
{
	if (theApp.IsClosing() || !source)
		return;
	if (IsChunkedRemoveDownloadSnapshotActive()) {
		MarkDeferredReload();
		return;
	}

	bool bVectorModified = false;
	bool bUpdateListedItems = HasVisibleSourceItem(*this, source, owner);
	const bool bTransferListInactive = theApp.emuledlg == NULL || theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible();
	std::vector<CtrlItem_Struct*> removedItems;

	if (bUpdateListedItems) {
		SaveListState(0, kDownloadListViewState); // Save selections and scroll state
		SetRedraw(false); // Suspend painting
	}

	for (ListItems::const_iterator it = m_ListItems.lower_bound(source); it != m_ListItems.end() && it->first == source; ) {
		CtrlItem_Struct* delItem = it->second;

		if (owner == NULL || owner == delItem->owner) {
			it = m_ListItems.erase(it);      // remove from main list
			removedItems.push_back(delItem);
		} else
			++it;
	}

	if (bUpdateListedItems && !removedItems.empty()) {
		for (size_t i = 0; i < m_ListedItemsVector.size(); ) {
			bool bRemove = false;
			for (size_t j = 0; j < removedItems.size(); ++j) {
				if (m_ListedItemsVector[i] == removedItems[j]) {
					bRemove = true;
					break;
				}
			}

			if (bRemove) {
				m_ListedItemsVector.erase(m_ListedItemsVector.begin() + i);
				bVectorModified = true;
			} else
				++i;
		}
	}

	if (!bUpdateListedItems && !removedItems.empty() && bTransferListInactive)
		MarkDeferredReload();

	for (size_t i = 0; i < removedItems.size(); ++i)
		delete removedItems[i];

	if (bUpdateListedItems) {
		if (bVectorModified) { // If the vector was modified, we need to rebuild the map and update the item count.
			RebuildListedItemsMap(); // Rebuild map after vector shrink.
			RequestTransferListRedraw();
			UpdateDownloadListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list.
		}

		RestoreListState(0, kDownloadListViewState, false); // Restore selections and scroll state
		SetRedraw(true); // Resume painting
	}
}

void CDownloadListCtrl::RemoveDeletedCompletedFilesByHash(const std::vector<CString>& vecFileHashes)
{
	if (theApp.IsClosing() || vecFileHashes.empty())
		return;

	std::set<CString, CStringNoCaseLess> setCanonicalHashes;
	for (std::vector<CString>::const_iterator itHash = vecFileHashes.begin(); itHash != vecFileHashes.end(); ++itHash) {
		CString strHash(*itHash);
		strHash.Trim();
		if (!strHash.IsEmpty())
			setCanonicalHashes.insert(strHash);
	}
	if (setCanonicalHashes.empty())
		return;

	std::set<void*> setRemovedOwners;
	std::set<CtrlItem_Struct*> setRemoveItems;
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		CtrlItem_Struct* pItem = it->second;
		if (pItem == NULL || pItem->type != FILE_TYPE || setCanonicalHashes.find(pItem->strOwnerHash) == setCanonicalHashes.end())
			continue;

		const CPartFile *pFile = static_cast<const CPartFile*>(pItem->value);
		if (pFile != NULL && theApp.downloadqueue != NULL && theApp.downloadqueue->GetFileByID(pFile->GetFileHash()) == pFile)
			continue;

		uchar abyFileHash[16];
		const CKnownFile *pSharedFile = NULL;
		if (!pItem->strOwnerHash.IsEmpty() && strmd4(pItem->strOwnerHash, abyFileHash) && theApp.sharedfiles != NULL)
			pSharedFile = theApp.sharedfiles->GetLiveFileByID(abyFileHash);
		if (pSharedFile != NULL && pSharedFile == static_cast<const CKnownFile*>(pFile))
			continue;

		setRemoveItems.insert(pItem);
		if (pItem->value != NULL)
			setRemovedOwners.insert(pItem->value);
	}

	if (setRemoveItems.empty())
		return;

	bool bVectorModified = false;
	bool bVisibleItemRemoved = false;
	for (ListItems::iterator it = m_ListItems.begin(); it != m_ListItems.end();) {
		CtrlItem_Struct* delItem = it->second;
		const bool bRemoveItem = delItem != NULL && (setRemoveItems.find(delItem) != setRemoveItems.end() || (delItem->type != FILE_TYPE && setRemovedOwners.find(delItem->owner) != setRemovedOwners.end()));
		if (!bRemoveItem) {
			++it;
			continue;
		}

		it = m_ListItems.erase(it);

		int iVectorIndex;
		if (m_ListedItemsMap.Lookup(delItem, iVectorIndex)) {
			if (!bVisibleItemRemoved) {
				SaveListState(0, kDownloadListViewState);
				SetRedraw(false);
				bVisibleItemRemoved = true;
			}
			if (iVectorIndex >= 0 && iVectorIndex < static_cast<int>(m_ListedItemsVector.size()))
				m_ListedItemsVector[iVectorIndex] = NULL;
			if (delItem->type == FILE_TYPE && m_uListedFilesCount > 0)
				--m_uListedFilesCount;
			m_ListedItemsMap.RemoveKey(delItem);
			bVectorModified = true;
		}

		delete delItem;
	}

	if (bVisibleItemRemoved) {
		if (bVectorModified) {
			m_ListedItemsVector.erase(std::remove(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), static_cast<CtrlItem_Struct*>(NULL)), m_ListedItemsVector.end());
			RebuildListedItemsMap();
			RequestTransferListRedraw();
			UpdateDownloadListItemCount(*this, m_ListedItemsVector.size(), true);
		}
		RestoreListState(0, kDownloadListViewState, false);
		SetRedraw(true);
		if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL && theApp.emuledlg->transferwnd->m_pwndTransfer != NULL)
			theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
	}
}

void CDownloadListCtrl::RemoveFilesByHash(const std::vector<CString>& vecFileHashes)
{
	if (theApp.IsClosing() || vecFileHashes.empty())
		return;

	std::set<CString, CStringNoCaseLess> setCanonicalHashes;
	for (std::vector<CString>::const_iterator itHash = vecFileHashes.begin(); itHash != vecFileHashes.end(); ++itHash) {
		CString strHash(*itHash);
		strHash.Trim();
		if (!strHash.IsEmpty())
			setCanonicalHashes.insert(strHash);
	}
	if (setCanonicalHashes.empty())
		return;

	bool bVectorModified = false;
	bool bVisibleItemRemoved = false;
	for (ListItems::iterator it = m_ListItems.begin(); it != m_ListItems.end();) {
		CtrlItem_Struct* delItem = it->second;
		bool bRemoveItem = false;
		if (delItem != NULL && setCanonicalHashes.find(delItem->strOwnerHash) != setCanonicalHashes.end())
			bRemoveItem = true;
		if (bRemoveItem) {
			it = m_ListItems.erase(it);

			int iVectorIndex;
			if (m_ListedItemsMap.Lookup(delItem, iVectorIndex)) {
				if (!bVisibleItemRemoved) {
					SaveListState(0, kDownloadListViewState);
					SetRedraw(false);
					bVisibleItemRemoved = true;
				}
				if (iVectorIndex >= 0 && iVectorIndex < static_cast<int>(m_ListedItemsVector.size()))
					m_ListedItemsVector[iVectorIndex] = NULL;
				if (delItem->type == FILE_TYPE && m_uListedFilesCount > 0)
					--m_uListedFilesCount;
				m_ListedItemsMap.RemoveKey(delItem);
				bVectorModified = true;
			}

			delete delItem;
		} else
			++it;
	}

	if (bVisibleItemRemoved) {
		if (bVectorModified) {
			m_ListedItemsVector.erase(std::remove(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), static_cast<CtrlItem_Struct*>(NULL)), m_ListedItemsVector.end());
			RebuildListedItemsMap();
			RequestTransferListRedraw();
			UpdateDownloadListItemCount(*this, m_ListedItemsVector.size(), true);
		}
		RestoreListState(0, kDownloadListViewState, false);
		SetRedraw(true);
		if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL && theApp.emuledlg->transferwnd->m_pwndTransfer != NULL)
			theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
	}
}

bool CDownloadListCtrl::RemoveFile(CPartFile* toremove)
{
	bool bResult = false;
	if (theApp.IsClosing() || !toremove)
		return bResult;

	bool bVectorModified = false;
	bool bUpdateListedItems = HasVisibleFileItem(*this, toremove);
	const bool bBatchVisibleListUpdate = bUpdateListedItems && m_bChunkedRemoveDownloadListStateBatchActive;
	const bool bTransferListInactive = theApp.emuledlg == NULL || theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible();
	std::vector<CtrlItem_Struct*> removedItems;

	if (bUpdateListedItems && !bBatchVisibleListUpdate) {
		SaveListState(0, kDownloadListViewState); // Save selections and scroll state
		SetRedraw(false); // Suspend painting
	}

	const bool bFileHasKnownSources = toremove->GetSourceCount() != 0 || toremove->GetSrcA4AFCount() != 0;
	if (bBatchVisibleListUpdate && !bFileHasKnownSources) {
		for (ListItems::iterator it = m_ListItems.lower_bound(toremove); it != m_ListItems.end() && it->first == toremove;) {
			CtrlItem_Struct* delItem = it->second;
			it = m_ListItems.erase(it);
			removedItems.push_back(delItem);
			bResult = true;
		}
	} else {
		for (ListItems::iterator it = m_ListItems.begin(); it != m_ListItems.end();) {
			CtrlItem_Struct* delItem = it->second;
			if (delItem->owner == toremove || delItem->value == toremove) {
				it = m_ListItems.erase(it);	// Drop from main list
				removedItems.push_back(delItem);
				bResult = true; // Indicate that at least one item was removed
			} else
				++it; // Continue iterating through the list
		}
	}

	if (bUpdateListedItems && !removedItems.empty()) {
		if (bBatchVisibleListUpdate) {
			for (size_t i = 0; i < removedItems.size(); ++i) {
				CtrlItem_Struct* delItem = removedItems[i];
				if (delItem != NULL && (!delItem->owner || delItem->owner->srcarevisible)) {
					int idx;
					if (m_ListedItemsMap.Lookup(delItem, idx)) {
						if (idx >= 0 && idx < static_cast<int>(m_ListedItemsVector.size())) {
							if (m_ListedItemsVector[static_cast<size_t>(idx)] == delItem)
								m_ListedItemsVector[static_cast<size_t>(idx)] = NULL;
						}
						m_ListedItemsMap.RemoveKey(delItem);
						m_bChunkedRemoveDownloadVisibleItemsPending = true;
					}
				}
			}
		} else {
			for (size_t i = 0; i < m_ListedItemsVector.size(); ) {
				bool bRemove = false;
				for (size_t j = 0; j < removedItems.size(); ++j) {
					if (m_ListedItemsVector[i] == removedItems[j]) {
						bRemove = true;
						break;
					}
				}

				if (bRemove) {
					if (m_ListedItemsVector[i] != NULL && m_ListedItemsVector[i]->type == FILE_TYPE && m_uListedFilesCount > 0)
						--m_uListedFilesCount;
					m_ListedItemsVector.erase(m_ListedItemsVector.begin() + i);
					bVectorModified = true; // Indicate that the vector was modified
				} else
					++i;
			}
		}
	}

	if (!bUpdateListedItems && !removedItems.empty() && bTransferListInactive)
		MarkDeferredReload();

	for (size_t i = 0; i < removedItems.size(); ++i)
		delete removedItems[i]; // Free memory

	if (bUpdateListedItems && !bBatchVisibleListUpdate) {
		if (bVectorModified) { // If the vector was modified, we need to rebuild the map and update the item count.
			RebuildListedItemsMap(); // Rebuild map after vector shrink.
			RequestTransferListRedraw();
			UpdateDownloadListItemCount(*this, m_ListedItemsVector.size(), true); // Set current count for the virtual list.
		}

		RestoreListState(0, kDownloadListViewState, false); // Restore selections and scroll state
		SetRedraw(true); // Resume painting

		theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount(); // Refresh the file count.
	}

	return bResult;
}

void CDownloadListCtrl::UpdateItem(void* toupdate, bool bForce)
{
	if (theApp.IsClosing() || !toupdate || theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || theApp.emuledlg->transferwnd->GetDownloadList()->IsWindowVisible() == false)
		return;
	if (IsChunkedRemoveDownloadSnapshotActive())
		return;

	static DWORD dwLastUpdateTime = 0;
	const DWORD dwCurrentTime = GetTickCount();
	const DWORD dwUpdatePeriod = static_cast<DWORD>(thePrefs.GetUITweaksListUpdatePeriod());

	if (!bForce && dwCurrentTime - dwLastUpdateTime < dwUpdatePeriod)
		return;

	dwLastUpdateTime = dwCurrentTime;

	std::set<CtrlItem_Struct*> updatedParentItems;
	std::set<CPartFile*> sourceOwnersToResort;
	const bool bResortSourceBranches = false;

	// Retrieve all entries matching the source
	for (ListItems::const_iterator it = m_ListItems.lower_bound(toupdate); it != m_ListItems.end() && it->first == toupdate; ++it) {
		CtrlItem_Struct* updateItem = it->second;

		if (updateItem == NULL)
			continue;

		// Update only if item is currently displayed in the virtual list
		int m_iIndex;
		if (m_ListedItemsMap.Lookup(updateItem, m_iIndex)) {
			const bool bVisibleRow = IsItemIndexVisible(m_iIndex);
			if (updateItem->type != FILE_TYPE) {
				if (updateItem->owner == NULL || updateItem->value == NULL || !updateItem->owner->srcarevisible)
					continue;
				updateItem->dwUpdated = 0;
				if (bResortSourceBranches && updateItem->owner != NULL)
					sourceOwnersToResort.insert(updateItem->owner);
				if (bForce && bVisibleRow)
					RequestTransferListRedrawForRange(m_iIndex, m_iIndex);

				CtrlItem_Struct *pParentItem = updateItem->parent;
				if (pParentItem != NULL && updatedParentItems.insert(pParentItem).second) {
					int iParentIndex = -1;
					if (m_ListedItemsMap.Lookup(pParentItem, iParentIndex)) {
						if (bForce && IsItemIndexVisible(iParentIndex))
							RequestTransferListRedrawForRange(iParentIndex, iParentIndex);
					}
				}
				continue;
			}

			if (updateItem->value == NULL)
				continue;
			CPartFile* partFile = static_cast<CPartFile*>(updateItem->value);
			if (!IsFilteredOut(partFile)) {
				updateItem->dwUpdated = 0; // Reset update flag
				if (bForce && bVisibleRow)
					RequestTransferListRedrawForRange(m_iIndex, m_iIndex);
			} else
				HideFile(partFile); // Hide the item if it is filtered out.
		}
	}

	if (!sourceOwnersToResort.empty()) {
		bool bVisibleItemsChanged = false;
		SaveListState(0, kDownloadListViewState);
		SetRedraw(false);
		for (std::set<CPartFile*>::const_iterator itOwner = sourceOwnersToResort.begin(); itOwner != sourceOwnersToResort.end(); ++itOwner) {
			CPartFile *pOwner = *itOwner;
			if (pOwner == NULL || !pOwner->srcarevisible || IsFilteredOut(pOwner))
				continue;
			CtrlItem_Struct *pOwnerItem = FindFileItem(pOwner);
			int iParentIndex = -1;
			if (pOwnerItem == NULL || !m_ListedItemsMap.Lookup(pOwnerItem, iParentIndex))
				continue;
			if (RemoveVisibleSourcesForOwner(pOwner)) {
				RebuildListedItemsMap();
				if (m_ListedItemsMap.Lookup(pOwnerItem, iParentIndex))
					InsertSortedVisibleSourcesForFile(pOwner, iParentIndex);
				bVisibleItemsChanged = true;
			}
		}
		if (bVisibleItemsChanged) {
			RebuildListedItemsMap();
			RequestTransferListRedraw();
			UpdateDownloadListItemCount(*this, m_ListedItemsVector.size());
		}
		RestoreListState(0, kDownloadListViewState, false);
		SetRedraw(true);
	}

	if (!bForce) {
		const SVisibleItemRange visibleRange = GetVisibleItemRange();
		if (visibleRange.IsValid()) {
			for (int i = visibleRange.m_iFirst; i <= visibleRange.m_iLast && static_cast<size_t>(i) < m_ListedItemsVector.size(); ++i) {
				if (m_ListedItemsVector[i] != NULL)
					m_ListedItemsVector[i]->dwUpdated = 0;
			}
			RequestTransferListRedrawForRange(visibleRange.m_iFirst, visibleRange.m_iLast);
		}
	}

	m_availableCommandsDirty = true;

	// Live transfer updates refresh visible row text and keep the active sort order.
	MaintainSortOrderAfterUpdate();
}

bool CDownloadListCtrl::AddFileToListModel(CPartFile *pFile)
{
	if (pFile == NULL || FindFileItem(pFile) != NULL)
		return false;

	CtrlItem_Struct *pNewItem = new CtrlItem_Struct;
	pNewItem->owner = NULL;
	pNewItem->type = FILE_TYPE;
	pNewItem->value = pFile;
	pNewItem->parent = NULL;
	pNewItem->strOwnerHash = md4str(pFile->GetFileHash());
	pNewItem->dwUpdated = 0;

	m_ListItems.emplace(pFile, pNewItem);
	return true;
}

bool CDownloadListCtrl::SyncFileItemsWithDownloadModel()
{
	if (theApp.downloadqueue == NULL || theApp.downloadqueue->IsBulkAddingDownloads() || theApp.downloadqueue->IsBulkRemovingDownloads())
		return false;

	bool bChanged = false;
	for (POSITION pos = theApp.downloadqueue->filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *pFile = theApp.downloadqueue->filelist.GetNext(pos);
		if (pFile != NULL && AddFileToListModel(pFile))
			bChanged = true;
	}
	return bChanged;
}

bool CDownloadListCtrl::IsHiddenByChunkedRemoveDownload(const CtrlItem_Struct *pCtrlItem) const
{
	if (pCtrlItem == NULL || pCtrlItem->strOwnerHash.IsEmpty() || m_mapChunkedRemoveDownloadHiddenRows.GetCount() == 0)
		return false;

	void *pHidden = NULL;
	return m_mapChunkedRemoveDownloadHiddenRows.Lookup(pCtrlItem->strOwnerHash, pHidden) != FALSE;
}

bool CDownloadListCtrl::IsChunkedRemoveDownloadSnapshotActive() const
{
	return m_mapChunkedRemoveDownloadHiddenRows.GetCount() != 0 && (m_bChunkedRemoveDownloadVisibleSnapshotActive || m_uChunkedRemoveDownloadTotal > 0 || m_bChunkedRemoveDownloadListStateBatchActive || m_bChunkedRemoveDownloadQueueBulkActive);
}

bool CDownloadListCtrl::IsChunkedRemoveDownloadDetachTarget(const CtrlItem_Struct *pCtrlItem) const
{
	if (pCtrlItem == NULL || m_mapChunkedRemoveDownloadHiddenRows.GetCount() == 0)
		return false;

	CString strOwnerHash(pCtrlItem->strOwnerHash);
	if (strOwnerHash.IsEmpty()) {
		if (pCtrlItem->type == FILE_TYPE && pCtrlItem->value != NULL)
			strOwnerHash = md4str(static_cast<const CPartFile*>(pCtrlItem->value)->GetFileHash());
		else if (pCtrlItem->owner != NULL)
			strOwnerHash = md4str(pCtrlItem->owner->GetFileHash());
	}
	if (strOwnerHash.IsEmpty())
		return false;

	void *pHidden = NULL;
	return m_mapChunkedRemoveDownloadHiddenRows.Lookup(strOwnerHash, pHidden) != FALSE;
}

void CDownloadListCtrl::ApplyChunkedRemoveDownloadVisibleItemCount(bool bForceFrameUpdate)
{
	if (theApp.IsClosing() || !::IsWindow(m_hWnd) || m_mapChunkedRemoveDownloadHiddenRows.GetCount() == 0)
		return;

	const size_t uSnapshotRows = m_bChunkedRemoveDownloadVisibleSnapshotActive ? m_uChunkedRemoveDownloadVisibleSnapshotRows : m_ListedItemsVector.size();
	const size_t uClampedCount = uSnapshotRows > static_cast<size_t>(INT_MAX) ? static_cast<size_t>(INT_MAX) : uSnapshotRows;
	const int iExpectedCount = static_cast<int>(uClampedCount);
	const bool bCountChanged = GetItemCount() != iExpectedCount;
	const bool bZeroCountScrollReset = iExpectedCount == 0;
	if (!bForceFrameUpdate && !bCountChanged && !bZeroCountScrollReset)
		return;

	if (iExpectedCount == 0) {
		if (bForceFrameUpdate || bCountChanged) {
			SetItemState(-1, 0, LVIS_FOCUSED | LVIS_SELECTED);
			SetSelectionMark(-1);
		}
		SetItemCountEx(0, 0);
		SCROLLINFO si = { sizeof(si) };
		si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
		si.nMin = 0;
		si.nMax = 0;
		si.nPage = 1;
		si.nPos = 0;
		SetScrollInfo(SB_VERT, &si, TRUE);
		ShowScrollBar(SB_VERT, FALSE);
	}
	else
		SetItemCountAndKeepPageFilled(uClampedCount, 0);

	if (bCountChanged || bForceFrameUpdate)
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_NOERASE | RDW_NOCHILDREN);
}

void CDownloadListCtrl::DetachChunkedRemoveDownloadVisibleRows()
{
	if (m_uChunkedRemoveDownloadTotal < BULK_OPERATION_MIN_ITEMS || theApp.IsClosing() || !::IsWindow(m_hWnd))
		return;

	for (POSITION pos = m_chunkedRemoveDownloadItems.GetHeadPosition(); pos != NULL;) {
		const SChunkedRemoveDownloadItem *pItem = m_chunkedRemoveDownloadItems.GetNext(pos);
		if (pItem == NULL || !pItem->m_id.IsValid())
			continue;

		CString strHash(md4str(pItem->m_id.m_abyFileHash));
		if (!strHash.IsEmpty())
			m_mapChunkedRemoveDownloadHiddenRows.SetAt(strHash, reinterpret_cast<void*>(static_cast<UINT_PTR>(1)));
	}

	if (m_mapChunkedRemoveDownloadHiddenRows.GetCount() == 0)
		return;

	if (theApp.emuledlg == NULL || theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible()) {
		MarkDeferredReload();
		return;
	}

	HidePersistentInfoTip(true);
	std::vector<CtrlItem_Struct*> keptItems;
	keptItems.reserve(m_ListedItemsVector.size());
	uint32 uListedFilesCount = 0;
	for (size_t i = 0; i < m_ListedItemsVector.size(); ++i) {
		CtrlItem_Struct *pItem = m_ListedItemsVector[i];
		if (pItem == NULL || IsChunkedRemoveDownloadDetachTarget(pItem))
			continue;
		keptItems.push_back(pItem);
		if (pItem->type == FILE_TYPE)
			++uListedFilesCount;
	}

	if (keptItems.size() == m_ListedItemsVector.size()) {
		m_bChunkedRemoveDownloadVisibleSnapshotActive = true;
		m_uChunkedRemoveDownloadVisibleSnapshotRows = m_ListedItemsVector.size();
		ApplyChunkedRemoveDownloadVisibleItemCount(true);
		return;
	}

	SaveListState(0, kDownloadListRemoveBatchState);
	SetRedraw(false);
	m_ListedItemsVector.swap(keptItems);
	m_uListedFilesCount = uListedFilesCount;
	m_bChunkedRemoveDownloadVisibleSnapshotActive = true;
	m_uChunkedRemoveDownloadVisibleSnapshotRows = m_ListedItemsVector.size();
	RebuildListedItemsMap();
	if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL && theApp.emuledlg->transferwnd->m_pwndTransfer != NULL)
		theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
	ApplyChunkedRemoveDownloadVisibleItemCount(true);
	RestoreListState(0, kDownloadListRemoveBatchState, false);
	SetRedraw(true);
	Invalidate(FALSE);
}

void CDownloadListCtrl::ClearChunkedRemoveDownloadHiddenRows(bool bReloadVisibleList)
{
	if (m_mapChunkedRemoveDownloadHiddenRows.GetCount() == 0)
		return;

	m_mapChunkedRemoveDownloadHiddenRows.RemoveAll();
	m_bChunkedRemoveDownloadVisibleSnapshotActive = false;
	m_uChunkedRemoveDownloadVisibleSnapshotRows = 0;
	if (bReloadVisibleList && !theApp.IsClosing() && ::IsWindow(m_hWnd))
		ReloadList(false, kDownloadListViewState);
}

bool CDownloadListCtrl::ShouldShowDownloadItemInList(const CtrlItem_Struct *pCtrlItem)
{
	if (pCtrlItem == NULL || IsHiddenByChunkedRemoveDownload(pCtrlItem))
		return false;

	if (pCtrlItem->type == FILE_TYPE) {
		CPartFile* file = static_cast<CPartFile*>(pCtrlItem->value);
		if (file == NULL)
			return false;
		return file != NULL && !IsFilteredOut(file);
	}

	if (pCtrlItem->owner == NULL || pCtrlItem->value == NULL)
		return false;

	CPartFile* parent = pCtrlItem->owner;
	return parent != NULL && parent->srcarevisible && !IsFilteredOut(parent);
}

CtrlItem_Struct* CDownloadListCtrl::FindFileItem(CPartFile *pFile) const
{
	if (pFile == NULL)
		return NULL;

	for (ListItems::const_iterator it = m_ListItems.lower_bound(pFile); it != m_ListItems.end() && it->first == pFile; ++it) {
		CtrlItem_Struct *pItem = it->second;
		if (pItem != NULL && pItem->type == FILE_TYPE && pItem->value == pFile)
			return pItem;
	}

	return NULL;
}

CtrlItem_Struct* CDownloadListCtrl::FindSourceItem(CPartFile *pOwner, CUpDownClient *pSource) const
{
	if (pOwner == NULL || pSource == NULL)
		return NULL;

	for (ListItems::const_iterator it = m_ListItems.lower_bound(pSource); it != m_ListItems.end() && it->first == pSource; ++it) {
		CtrlItem_Struct *pItem = it->second;
		if (pItem != NULL && pItem->type != FILE_TYPE && pItem->owner == pOwner && pItem->value == pSource)
			return pItem;
	}

	return NULL;
}

bool CDownloadListCtrl::GetSourceItemTypeFromOwner(CPartFile *pOwner, CUpDownClient *pSource, ItemType &eItemType) const
{
	if (pOwner == NULL || pSource == NULL)
		return false;

	if (pOwner->srclist.Find(pSource) != NULL) {
		eItemType = AVAILABLE_SOURCE;
		return true;
	}

	if (pOwner->A4AFsrclist.Find(pSource) != NULL) {
		eItemType = UNAVAILABLE_SOURCE;
		return true;
	}

	return false;
}

bool CDownloadListCtrl::EnsureSourceItem(CPartFile *pOwner, CUpDownClient *pSource, ItemType eItemType, CtrlItem_Struct *pOwnerItem)
{
	if (pOwner == NULL || pSource == NULL || pOwnerItem == NULL)
		return false;

	CtrlItem_Struct *pItem = FindSourceItem(pOwner, pSource);
	if (pItem != NULL) {
		bool bChanged = false;
		if (pItem->type != eItemType) {
			pItem->type = eItemType;
			pItem->dwUpdated = 0;
			bChanged = true;
		}
		if (pItem->parent != pOwnerItem) {
			pItem->parent = pOwnerItem;
			bChanged = true;
		}
		if (pItem->strOwnerHash.IsEmpty()) {
			pItem->strOwnerHash = md4str(pOwner->GetFileHash());
			bChanged = true;
		}
		return bChanged;
	}

	CtrlItem_Struct *pNewItem = new CtrlItem_Struct;
	pNewItem->type = eItemType;
	pNewItem->owner = pOwner;
	pNewItem->value = pSource;
	pNewItem->parent = pOwnerItem;
	pNewItem->strOwnerHash = md4str(pOwner->GetFileHash());
	pNewItem->dwUpdated = 0;

	m_ListItems.emplace(pSource, pNewItem);
	return true;
}

bool CDownloadListCtrl::SyncSourceItemsForOwner(CPartFile *pOwner, CtrlItem_Struct *pOwnerItem)
{
	if (pOwner == NULL || pOwnerItem == NULL)
		return false;

	bool bChanged = false;
	for (ListItems::iterator it = m_ListItems.begin(); it != m_ListItems.end(); ) {
		CtrlItem_Struct *pItem = it->second;
		if (pItem == NULL || pItem->type == FILE_TYPE || pItem->owner != pOwner) {
			++it;
			continue;
		}

		CUpDownClient *pSource = static_cast<CUpDownClient*>(pItem->value);
		ItemType eModelItemType = INVALID_TYPE;
		if (!GetSourceItemTypeFromOwner(pOwner, pSource, eModelItemType)) {
			it = m_ListItems.erase(it);
			delete pItem;
			bChanged = true;
			continue;
		}

		if (pItem->type != eModelItemType) {
			pItem->type = eModelItemType;
			pItem->dwUpdated = 0;
			bChanged = true;
		}
		if (pItem->parent != pOwnerItem) {
			pItem->parent = pOwnerItem;
			bChanged = true;
		}
		++it;
	}

	for (POSITION pos = pOwner->srclist.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *pSource = pOwner->srclist.GetNext(pos);
		ItemType eModelItemType = INVALID_TYPE;
		if (GetSourceItemTypeFromOwner(pOwner, pSource, eModelItemType))
			bChanged |= EnsureSourceItem(pOwner, pSource, eModelItemType, pOwnerItem);
	}

	for (POSITION pos = pOwner->A4AFsrclist.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *pSource = pOwner->A4AFsrclist.GetNext(pos);
		ItemType eModelItemType = INVALID_TYPE;
		if (GetSourceItemTypeFromOwner(pOwner, pSource, eModelItemType))
			bChanged |= EnsureSourceItem(pOwner, pSource, eModelItemType, pOwnerItem);
	}

	return bChanged;
}

bool CDownloadListCtrl::SyncSourceItemsWithDownloadModel()
{
	if (theApp.downloadqueue == NULL)
		return false;

	std::set<CPartFile*> setModelFiles;
	for (POSITION pos = theApp.downloadqueue->filelist.GetHeadPosition(); pos != NULL;)
		setModelFiles.insert(theApp.downloadqueue->filelist.GetNext(pos));

	bool bChanged = false;
	for (ListItems::iterator it = m_ListItems.begin(); it != m_ListItems.end(); ) {
		CtrlItem_Struct *pItem = it->second;
		if (pItem == NULL || pItem->type == FILE_TYPE) {
			++it;
			continue;
		}

		CPartFile *pOwner = pItem->owner;
		CUpDownClient *pSource = static_cast<CUpDownClient*>(pItem->value);
		CtrlItem_Struct *pOwnerItem = FindFileItem(pOwner);
		ItemType eModelItemType = INVALID_TYPE;
		if (setModelFiles.find(pOwner) == setModelFiles.end() || pOwnerItem == NULL || !GetSourceItemTypeFromOwner(pOwner, pSource, eModelItemType)) {
			it = m_ListItems.erase(it);
			delete pItem;
			bChanged = true;
			continue;
		}

		if (pItem->type != eModelItemType) {
			pItem->type = eModelItemType;
			pItem->dwUpdated = 0;
			bChanged = true;
		}
		if (pItem->parent != pOwnerItem) {
			pItem->parent = pOwnerItem;
			bChanged = true;
		}
		++it;
	}

	for (POSITION pos = theApp.downloadqueue->filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *pFile = theApp.downloadqueue->filelist.GetNext(pos);
		CtrlItem_Struct *pFileItem = FindFileItem(pFile);
		if (pFileItem == NULL)
			continue;
		if (!pFile->srcarevisible)
			continue;

		for (POSITION posSource = pFile->srclist.GetHeadPosition(); posSource != NULL;) {
			CUpDownClient *pSource = pFile->srclist.GetNext(posSource);
			ItemType eModelItemType = INVALID_TYPE;
			if (GetSourceItemTypeFromOwner(pFile, pSource, eModelItemType))
				bChanged |= EnsureSourceItem(pFile, pSource, eModelItemType, pFileItem);
		}

		for (POSITION posSource = pFile->A4AFsrclist.GetHeadPosition(); posSource != NULL;) {
			CUpDownClient *pSource = pFile->A4AFsrclist.GetNext(posSource);
			ItemType eModelItemType = INVALID_TYPE;
			if (GetSourceItemTypeFromOwner(pFile, pSource, eModelItemType))
				bChanged |= EnsureSourceItem(pFile, pSource, eModelItemType, pFileItem);
		}
	}

	return bChanged;
}

void CDownloadListCtrl::BuildVisibleDownloadItems(std::vector<CtrlItem_Struct*> &visibleItems, uint32 &uListedFilesCount)
{
	visibleItems.clear();
	uListedFilesCount = 0;
	for (auto it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		CtrlItem_Struct* cur_item = it->second;
		if (!ShouldShowDownloadItemInList(cur_item))
			continue;
		visibleItems.push_back(cur_item);
		if (cur_item->type == FILE_TYPE)
			++uListedFilesCount;
	}
}

bool CDownloadListCtrl::RemoveVisibleSourcesForOwner(CPartFile *pOwner)
{
	if (pOwner == NULL)
		return false;

	bool bRemoved = false;
	for (size_t i = 0; i < m_ListedItemsVector.size(); ) {
		CtrlItem_Struct* item = m_ListedItemsVector[i];
		if (item != NULL && item->owner == pOwner) {
			item->dwUpdated = 0;
			m_ListedItemsMap.RemoveKey(item);
			m_ListedItemsVector.erase(m_ListedItemsVector.begin() + i);
			bRemoved = true;
		} else
			++i;
	}

	return bRemoved;
}

bool CDownloadListCtrl::RemoveSourceItemsForOwner(CPartFile *pOwner)
{
	if (pOwner == NULL)
		return false;

	bool bRemoved = false;
	for (ListItems::iterator it = m_ListItems.begin(); it != m_ListItems.end(); ) {
		CtrlItem_Struct* pItem = it->second;
		if (pItem != NULL && pItem->type != FILE_TYPE && pItem->owner == pOwner) {
			it = m_ListItems.erase(it);
			delete pItem;
			bRemoved = true;
		} else
			++it;
	}

	return bRemoved;
}

void CDownloadListCtrl::BuildSortedSourceItemsForFile(CPartFile *pOwner, std::vector<CtrlItem_Struct*> &sourceItems)
{
	sourceItems.clear();
	if (pOwner == NULL)
		return;

	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		CtrlItem_Struct* cur_item = it->second;
		if (cur_item != NULL && cur_item->owner == pOwner)
			sourceItems.push_back(cur_item);
	}

	if (sourceItems.size() > 1) {
		const bool bOldRawSortState = m_bRawSortInProgress;
		m_bRawSortInProgress = true;
		CombinedSort(sourceItems.begin(), sourceItems.end(), SortFunc);
		m_bRawSortInProgress = bOldRawSortState;
	}
}

void CDownloadListCtrl::InsertSortedVisibleSourcesForFile(CPartFile *pOwner, int iParentIndex)
{
	if (pOwner == NULL || iParentIndex < 0 || static_cast<size_t>(iParentIndex) >= m_ListedItemsVector.size())
		return;

	std::vector<CtrlItem_Struct*> sourceItems;
	BuildSortedSourceItemsForFile(pOwner, sourceItems);
	if (sourceItems.empty())
		return;

	m_ListedItemsVector.insert(m_ListedItemsVector.begin() + iParentIndex + 1, sourceItems.begin(), sourceItems.end());
}




bool CDownloadListCtrl::TryGetListedItemDisplayText(int iItem, int iSubItem, CString &strText) const
{
	strText.Empty();
	if (iItem < 0 || iSubItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size())
		return false;

	const CtrlItem_Struct *pCtrlItem = m_ListedItemsVector[static_cast<size_t>(iItem)];
	if (pCtrlItem == NULL || pCtrlItem->value == NULL)
		return false;

	if (pCtrlItem->type == FILE_TYPE) {
		strText = GetFileItemDisplayText(static_cast<const CPartFile*>(pCtrlItem->value), iSubItem);
		return true;
	}

	if (pCtrlItem->type == AVAILABLE_SOURCE || pCtrlItem->type == UNAVAILABLE_SOURCE) {
		if (pCtrlItem->owner == NULL)
			return false;
		strText = GetSourceItemDisplayText(pCtrlItem, iSubItem);
		return true;
	}

	return false;
}

CString CDownloadListCtrl::GetListedItemDisplayText(int iItem, int iSubItem) const
{
	CString strText;
	return TryGetListedItemDisplayText(iItem, iSubItem, strText) ? strText : EMPTY;
}



void CDownloadListCtrl::RequestTransferListRedrawForRange(int iFirst, int iLast)
{
	if (theApp.GetBackendLifecycleState() >= CemuleApp::BackendLifecycleStoppingUiUpdates || !::IsWindow(m_hWnd))
		return;
	if (iFirst < 0 || iLast < iFirst || GetItemCount() <= 0)
		return;
	iFirst = max(0, iFirst);
	iLast = min(iLast, GetItemCount() - 1);
	if (iLast >= iFirst)
		RequestRowRedrawAsync(iFirst, iLast);
}

void CDownloadListCtrl::RequestTransferListRedraw()
{
	if (::IsWindow(m_hWnd))
		RequestFullRedrawAsync();
}



ItemType CDownloadListCtrl::GetListedItemType(int iItem) const
{
	if (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size())
		return INVALID_TYPE;

	const CtrlItem_Struct *pItem = m_ListedItemsVector[iItem];
	return pItem != NULL ? pItem->type : INVALID_TYPE;
}

bool CDownloadListCtrl::TryGetListedDownloadItemId(int iItem, SDownloadItemId& id) const
{
	id.Clear();
	if (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size())
		return false;

	const CtrlItem_Struct *pItem = m_ListedItemsVector[iItem];
	if (pItem != NULL && pItem->type == FILE_TYPE && pItem->value != NULL) {
		const CPartFile *pFile = static_cast<const CPartFile*>(pItem->value);
		id.SetFile(pFile);
		return id.IsValid();
	}

	return false;
}

CPartFile* CDownloadListCtrl::ResolveListedDownloadFile(int iItem) const
{
	if (iItem >= 0 && static_cast<size_t>(iItem) < m_ListedItemsVector.size()) {
		CtrlItem_Struct *pItem = m_ListedItemsVector[iItem];
		if (pItem != NULL && pItem->type == FILE_TYPE && pItem->value != NULL)
			return static_cast<CPartFile*>(pItem->value);
	}

	SDownloadItemId id;
	if (TryGetListedDownloadItemId(iItem, id))
		return theApp.downloadqueue != NULL ? theApp.downloadqueue->GetFileByItemId(id) : NULL;

	return NULL;
}

CPartFile* CDownloadListCtrl::ResolveListedParentDownloadFile(int iItem) const
{
	if (theApp.downloadqueue == NULL)
		return NULL;

	if (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size())
		return NULL;

	const CtrlItem_Struct *pItem = m_ListedItemsVector[iItem];
	if (pItem == NULL)
		return NULL;

	if (pItem->type == FILE_TYPE) {
		CPartFile *pFile = static_cast<CPartFile*>(pItem->value);
		return pFile != NULL && pFile->IsPartFile() ? pFile : NULL;
	}

	if (pItem->owner == NULL || pItem->value == NULL)
		return NULL;
	return pItem->owner;
}

DWORD CDownloadListCtrl::GetListedSourceClientRuntimeID(int iItem) const
{
	if (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size() || theApp.clientlist == NULL)
		return 0;

	const CtrlItem_Struct *pItem = m_ListedItemsVector[iItem];
	if (pItem == NULL || pItem->type == FILE_TYPE || pItem->value == NULL)
		return 0;

	CUpDownClient *pTrackedClient = theApp.clientlist->AcquireTrackedClientByPointer(static_cast<const CUpDownClient*>(pItem->value));
	if (pTrackedClient == NULL)
		return 0;

	const DWORD uRuntimeID = static_cast<DWORD>(pTrackedClient->GetRuntimeID());
	pTrackedClient->ReleaseRuntimeReference();
	return uRuntimeID;
}

CUpDownClient* CDownloadListCtrl::AcquireListedSourceClient(int iItem) const
{
	const DWORD uRuntimeID = GetListedSourceClientRuntimeID(iItem);
	return uRuntimeID != 0 && theApp.clientlist != NULL ? theApp.clientlist->AcquireTrackedClientByRuntimeID(uRuntimeID) : NULL;
}

CObject* CDownloadListCtrl::CreateListedDetailWalkerToken(int iItem, ItemType eItemType) const
{
	if (eItemType == FILE_TYPE)
		return reinterpret_cast<CObject*>(ResolveListedDownloadFile(iItem));

	return CreateClientDetailWalkerTokenFromRuntimeID(GetListedSourceClientRuntimeID(iItem));
}

void CDownloadListCtrl::ReloadList(const bool bSortCurrentList, const EListStateField LsfFlag)
{
	if (theApp.IsClosing())
		return;
	if (theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible()) {
		MarkDeferredReload();
		return;
	}

	if (IsChunkedRemoveDownloadSnapshotActive()) {
		DetachChunkedRemoveDownloadVisibleRows();
		ApplyChunkedRemoveDownloadVisibleItemCount(false);
		m_bDeferredReload = true;
		return;
	}

	CWaitCursor curWait;
	bool bInitializing = (m_iDataSize == -1); // Check if this is the first call to ReloadList

	// Initializing the vector and map
	if (bInitializing) {
		m_iDataSize = 10007; // Any reasonable prime number for the initial size.
		m_ListedItemsVector.reserve(m_iDataSize);
		m_ListedItemsMap.InitHashTable(m_iDataSize);
	} else
		SaveListState(0, LsfFlag); // Save selections, sort and scroll values for the previous m_nResultsID if this is not the first call.

	const bool bSyncedFileItems = SyncFileItemsWithDownloadModel();
	const bool bSyncedSourceItems = SyncSourceItemsWithDownloadModel();
	const bool bRebuildVisibleItems = !bSortCurrentList || bSyncedFileItems || bSyncedSourceItems;
	std::vector<CtrlItem_Struct*>& aNewVisibleItems = m_vecDownloadReloadScratch;
	uint32 uNewListedFilesCount = m_uListedFilesCount;
	if (bRebuildVisibleItems) {
		aNewVisibleItems.clear();
		const size_t uVisibleItemCapacity = m_ListItems.size();
		if (aNewVisibleItems.capacity() < uVisibleItemCapacity)
			aNewVisibleItems.reserve(uVisibleItemCapacity);
		BuildVisibleDownloadItems(aNewVisibleItems, uNewListedFilesCount);
	}

	// Reloading data completed at this point. Now we need to sort the vector.
	// Sort vector, then load sorted data to map and reverse map.
	if (bRebuildVisibleItems) {
		const bool bOldRawSortState = m_bRawSortInProgress;
		m_bRawSortInProgress = true;
		CombinedSort(aNewVisibleItems.begin(), aNewVisibleItems.end(), SortFunc);
		m_bRawSortInProgress = bOldRawSortState;
	}

	SetRedraw(false); // Suspend painting while the visible model is committed.

	if (bRebuildVisibleItems) {
		m_ListedItemsVector.swap(aNewVisibleItems);
		aNewVisibleItems.clear();
		m_uListedFilesCount = uNewListedFilesCount;
	} else {
		const bool bOldRawSortState = m_bRawSortInProgress;
		m_bRawSortInProgress = true;
		CombinedSort(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), SortFunc);
		m_bRawSortInProgress = bOldRawSortState;
	}
	RebuildListedItemsMap(); // Rebuild the map after sorting.

	RequestTransferListRedraw();
	UpdateDownloadListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list before restoring state.
	theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount(); // Refresh the file count.

	// Skip RestoreListState if file deletion is in progress to avoid redundant SaveListState/RestoreListState calls
	if (!bInitializing && !s_bFileDeletionInProgress)
		RestoreListState(0, LsfFlag, false); // Restore selections, sort and scroll values if this is not the first call.

	m_bDeferredReload = false;
	SetRedraw(true); // Resume painting
	Invalidate(FALSE);
}

// Index map after vector changes
void CDownloadListCtrl::RebuildListedItemsMap()
{
	m_ListedItemsMap.RemoveAll();
	for (int i = 0; i < static_cast<int>(m_ListedItemsVector.size()); ++i) {
		if (m_ListedItemsVector[i] != NULL)
			m_ListedItemsMap[m_ListedItemsVector[i]] = i;
	}
}

const bool CDownloadListCtrl::SortFunc(const CtrlItem_Struct* first, const CtrlItem_Struct* second)
{
	if (first == second)
		return false;
	if (first == NULL || second == NULL)
		return first != NULL;
	return SortProc((LPARAM)first, (LPARAM)second, m_pSortParam) < 0; // If the first one has a smaller value returns true, otherwise returns false.
}

CObject* CDownloadListCtrl::GetItemObject(int iIndex) const
{
	if (iIndex < 0 || static_cast<size_t>(iIndex) >= m_ListedItemsVector.size())
		return nullptr;
	return m_ListedItemsVector[static_cast<size_t>(iIndex)];
}

void CDownloadListCtrl::DrawFileItem(CDC *dc, int iItem, int nColumn, LPCRECT lpRect, UINT uDrawTextAlignment, CtrlItem_Struct *pCtrlItem)
{
	CPartFile *pPartFile = pCtrlItem != NULL && pCtrlItem->type == FILE_TYPE ? static_cast<CPartFile*>(pCtrlItem->value) : NULL;
	CString sItem;
	if (!TryGetListedItemDisplayText(iItem, nColumn, sItem) && pPartFile != NULL)
		sItem = GetFileItemDisplayText(pPartFile, nColumn);
	CRect rcDraw(lpRect);
	switch (nColumn) {
	case 0: // file name
		{
			LONG iIconPosY = max((rcDraw.Height() - theApp.GetSmallSytemIconSize().cy) / 2,  0);
			CString strFileName;
			if (pPartFile != NULL)
				strFileName = pPartFile->GetFileName();
			int iImage = theApp.GetFileTypeSystemImageIdx(strFileName);
			if (theApp.GetSystemImageList() != NULL)
				::ImageList_Draw(theApp.GetSystemImageList(), iImage, dc->GetSafeHdc(), rcDraw.left, rcDraw.top + iIconPosY, ILD_TRANSPARENT);
			rcDraw.left += theApp.GetSmallSytemIconSize().cx;

			const bool bShowRating = pPartFile != NULL && thePrefs.ShowRatingIndicator() && (pPartFile->HasComment() || pPartFile->HasRating() || pPartFile->IsKadCommentSearchRunning());
			const int iRatingImage = pPartFile != NULL ? 14 + pPartFile->UserRating(true) : -1;
			if (bShowRating && iRatingImage >= 0) {
				SafeImageListDraw(&m_ImageList, dc, iRatingImage, CPoint(rcDraw.left + 2, rcDraw.top + iIconPosY), ILD_NORMAL);
				rcDraw.left += 2 + RATING_ICON_WIDTH;
			}

			rcDraw.left += sm_iLabelOffset;
			dc->DrawText(sItem, -1, rcDraw, MLC_DT_TEXT | uDrawTextAlignment);
		}
		break;
	case 5: // progress
		{
			if (pCtrlItem == NULL)
				break;

			CPartFile *pStatusFile = pPartFile;
			--rcDraw.bottom;
			++rcDraw.top;
			int iWidth = rcDraw.Width();
			int iHeight = rcDraw.Height();

			// Validate dimensions and avoid invalid GDI calls
			if (iWidth <= 0 || iHeight <= 0)
				break;

			// Read the cached bitmap width with GetBitmap to avoid GetBitmapDimensionEx asserts.
			const int cx = GetBitmapWidth(pCtrlItem->status);
			const bool bHasCachedStatus = pCtrlItem->status.GetSafeHandle() != NULL && cx == iWidth;
			if (pStatusFile == NULL && !bHasCachedStatus)
				break;

			CDC cdcStatus;
			if (!cdcStatus.CreateCompatibleDC(dc))
				break;

			const DWORD curTick = ::GetTickCount();
			const bool bRefreshStatus = pStatusFile != NULL && (curTick >= pCtrlItem->dwUpdated + DLC_BARUPDATE || cx != iWidth || !pCtrlItem->dwUpdated);
			if (bRefreshStatus) {
				pCtrlItem->status.DeleteObject();
				if (!pCtrlItem->status.CreateCompatibleBitmap(dc, iWidth, iHeight))
					break;
			}

			HGDIOBJ hOldBitmap = cdcStatus.SelectObject(pCtrlItem->status);
			if (hOldBitmap == NULL)
				break;
			if (bRefreshStatus) {
				CRect statusRect(0, 0, iWidth, iHeight);
				const bool bUseFlatBar = thePrefs.UseFlatBar();
				const int iSavedDC = bUseFlatBar ? cdcStatus.SaveDC() : 0;
				pStatusFile->DrawStatusBar(&cdcStatus, statusRect, bUseFlatBar);
				if (iSavedDC != 0)
					cdcStatus.RestoreDC(iSavedDC);
				pCtrlItem->dwUpdated = curTick + (rand() & 0x7f);
			}
			dc->BitBlt(rcDraw.left, rcDraw.top, iWidth, iHeight, &cdcStatus, 0, 0, SRCCOPY);
			cdcStatus.SelectObject(hOldBitmap);

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

CString CDownloadListCtrl::GetSourceItemDisplayText(const CtrlItem_Struct *pCtrlItem, int iSubItem) const
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
		if (pCtrlItem->type == AVAILABLE_SOURCE && (pClient->GetTransferredDown() || pClient->GetDownloadState() == DS_DOWNLOADING))
			return CastItoXBytes(pClient->GetTransferredDown());
		break;
	case 3: //completed
		break;
	case 4: //speed
		if (pCtrlItem->type == AVAILABLE_SOURCE && (pClient->GetDownloadDatarate() || pClient->GetDownloadState() == DS_DOWNLOADING))
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

void CDownloadListCtrl::DrawSourceItem(CDC *dc, int iItem, int nColumn, LPCRECT lpRect, UINT uDrawTextAlignment, CtrlItem_Struct *pCtrlItem)
{
	const CUpDownClient *pClient = pCtrlItem != NULL && pCtrlItem->type != FILE_TYPE ? static_cast<const CUpDownClient*>(pCtrlItem->value) : NULL;
	CString sItem;
	if (!TryGetListedItemDisplayText(iItem, nColumn, sItem) && pCtrlItem != NULL && pClient != NULL)
		sItem = GetSourceItemDisplayText(pCtrlItem, nColumn);
	switch (nColumn) {
	case 0: // icon, name, status
		{
			CRect rcItem(*lpRect);
			int iIconPosY = (rcItem.Height() > 16) ? ((rcItem.Height() - 15) / 2) : 0;
			POINT point = {rcItem.left, rcItem.top + iIconPosY};
			int iImage = -1;
			if (pClient != NULL) {
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
			}
			if (iImage >= 0)
				SafeImageListDraw(&m_ImageList, dc, iImage, point, ILD_NORMAL);
			rcItem.left += 20;

			UINT uOvlImg = 0;
			int iClientImage = -1;
			if (pClient != NULL) {
				uOvlImg = static_cast<UINT>((pClient->Credits() && pClient->Credits()->GetCurrentIdentState(pClient->GetIP()) == IS_IDENTIFIED));
				uOvlImg |= (static_cast<UINT>(pClient->IsObfuscatedConnectionEstablished()) << 1);

				if (pClient->IsFriend())
					iClientImage = 6;
				else {
					switch (pClient->GetClientSoft()) {
					case SO_EDONKEYHYBRID:
						iClientImage = 9;
						break;
					case SO_MLDONKEY:
						iClientImage = 8;
						break;
					case SO_SHAREAZA:
						iClientImage = 10;
						break;
					case SO_URL:
						iClientImage = 11;
						break;
					case SO_AMULE:
						iClientImage = 12;
						break;
					case SO_LPHANT:
						iClientImage = 13;
						break;
					default:
						iClientImage = pClient->ExtProtocolAvailable() ? 5 : 7;
					}
				}
			}
			const POINT point2 = { rcItem.left, rcItem.top + iIconPosY };
			if (iClientImage >= 0)
				SafeImageListDraw(&m_ImageList, dc, iClientImage, point2, ILD_NORMAL | INDEXTOOVERLAYMASK(uOvlImg));
			if (theApp.ipgeolocation->ShowCountryFlag() && IsColumnHidden(14)) {
				rcItem.left += 20;
			    POINT point3 = { rcItem.left,rcItem.top + 1 };
				const int iCountryFlagIndex = pClient != NULL ? pClient->GetCountryFlagIndex() : 0;
			    IMAGELISTDRAWPARAMS flagDrawParams = theApp.ipgeolocation->GetFlagImageDrawParams(dc, iCountryFlagIndex, point3);
			    theApp.ipgeolocation->GetFlagImageList()->DrawIndirect(&flagDrawParams);
				rcItem.left += sm_iSubItemInset;
			}
			rcItem.left += 20;
			dc->DrawText(sItem, -1, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
		}
		break;
	case 5: // file info
		{
			if (pCtrlItem == NULL)
				break;

			CRect rcDraw(lpRect);
			--rcDraw.bottom;
			++rcDraw.top;
			int iWidth = rcDraw.Width();
			int iHeight = rcDraw.Height();

			// Validate dimensions and avoid invalid GDI calls
			if (iWidth <= 0 || iHeight <= 0)
				break;

			CScopedDownloadClientRef statusClientRef(AcquireListedSourceClient(iItem));
			CUpDownClient *pStatusClient = statusClientRef.Get();
			if (pStatusClient == NULL)
				break;

			// Read the cached bitmap width with GetBitmap to avoid GetBitmapDimensionEx asserts.
			const int cx = GetBitmapWidth(pCtrlItem->status);
			CDC cdcStatus;
			if (!cdcStatus.CreateCompatibleDC(dc))
				break;

			const DWORD curTick = ::GetTickCount();
			const bool bRefreshStatus = curTick >= pCtrlItem->dwUpdated + DLC_BARUPDATE || cx != iWidth || !pCtrlItem->dwUpdated;
			if (bRefreshStatus) {
				pCtrlItem->status.DeleteObject();
				if (!pCtrlItem->status.CreateCompatibleBitmap(dc, iWidth, iHeight))
					break;
			}

			HGDIOBJ hOldBitmap = cdcStatus.SelectObject(pCtrlItem->status);
			if (hOldBitmap == NULL)
				break;
			if (bRefreshStatus) {
				CRect statusRect(0, 0, iWidth, iHeight);
				const bool bUseFlatBar = thePrefs.UseFlatBar();
				const int iSavedDC = bUseFlatBar ? cdcStatus.SaveDC() : 0;
				const bool bUnavailableSource = pCtrlItem->type == UNAVAILABLE_SOURCE;
				pStatusClient->DrawStatusBar(&cdcStatus, statusRect, bUnavailableSource, bUseFlatBar);
				if (iSavedDC != 0)
					cdcStatus.RestoreDC(iSavedDC);
				pCtrlItem->dwUpdated = curTick + (rand() & 0x7f);
			}
			dc->BitBlt(rcDraw.left, rcDraw.top, iWidth, iHeight, &cdcStatus, 0, 0, SRCCOPY);
			cdcStatus.SelectObject(hOldBitmap);
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
			if (theApp.ipgeolocation->ShowCountryFlag()) {
				POINT point3 = { cur_rec.left,cur_rec.top + 1 };
				const int iCountryFlagIndex = pClient != NULL ? pClient->GetCountryFlagIndex() : 0;
				IMAGELISTDRAWPARAMS flagDrawParams = theApp.ipgeolocation->GetFlagImageDrawParams(dc, iCountryFlagIndex, point3);
				theApp.ipgeolocation->GetFlagImageList()->DrawIndirect(&flagDrawParams);
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
	if (index < 0 || theApp.IsClosing() || m_ListedItemsVector.empty() || static_cast<size_t>(index) >= m_ListedItemsVector.size()) {
		FillDownloadFallbackOwnerDataRow(*this, lpDrawItemStruct);
		return;
	}

	CRect rcItem(lpDrawItemStruct->rcItem);
	CRect rcClientFullRow;
	GetClientRect(&rcClientFullRow);
	CRect rcPaint(rcClientFullRow.left, rcItem.top, rcClientFullRow.right, rcItem.bottom);
	CDC* pBaseDC = CDC::FromHandle(lpDrawItemStruct->hDC);
	CMemoryDC dc(pBaseDC, rcPaint);
	BOOL bCtrlFocused;
	InitItemMemDC(dc, lpDrawItemStruct, bCtrlFocused);

	RECT rcClient;
	GetClientRect(&rcClient);
	CtrlItem_Struct* content = reinterpret_cast<CtrlItem_Struct*>(m_ListedItemsVector[index]);
	if (content == NULL || content->value == NULL)
		return;

	if (m_pFontBold) {
		bool bUseBold = false;
		if (content->value != NULL) {
			if (content->type == FILE_TYPE && content->value != NULL) {
				const CPartFile* pPartFile = static_cast<CPartFile*>(content->value);
				bUseBold = pPartFile->GetStatus() != PS_COMPLETE && pPartFile->GetTransferringSrcCount() > 0;
			} else if ((content->type == UNAVAILABLE_SOURCE || content->type == AVAILABLE_SOURCE) && content->value != NULL)
				bUseBold = static_cast<CUpDownClient*>(content->value)->GetDownloadState() == DS_DOWNLOADING;
		}

		if (bUseBold)
			dc.SelectObject(m_pFontBold);
	}

	bool isChild = content->type != FILE_TYPE;
	bool notLast = static_cast<size_t>(index) + 1 < m_ListedItemsVector.size();
	bool notFirst = index > 0;
	int tree_start = 0;
	int tree_end = 0;

	int iTreeOffset = 8 - sm_iLabelOffset; //6
	const CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	int iCount = pHeaderCtrl->GetItemCount();
	LONG itemLeft = rcItem.left;
	rcItem.right = rcItem.left - sm_iLabelOffset;
	rcItem.left += sm_iIconOffset;

	if (!isChild && !g_bLowColorDesktop && (lpDrawItemStruct->itemState & ODS_SELECTED) == 0) {
		DWORD dwCatColor = 0;
		if (content->value != NULL)
			dwCatColor = thePrefs.GetCatColor(static_cast<CPartFile*>(content->value)->GetCategory(), COLOR_WINDOWTEXT);
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
					DrawSourceItem(dc, index, iColumn, &rcItem, uDrawTextAlignment, content);
				else
					DrawFileItem(dc, index, iColumn, &rcItem, uDrawTextAlignment, content);
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
		CtrlItem_Struct *pNextContent = notLast ? m_ListedItemsVector[index + 1] : NULL;
		bool hasNext = pNextContent != NULL && pNextContent->type != FILE_TYPE;
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
	if (IsChunkedRemoveDownloadSnapshotActive()) {
		MarkDeferredReload();
		return;
	}

	toCollapse->srcarevisible = false;

	SaveListState(0, kDownloadListViewState); // Save selections and scroll state
	SetRedraw(false); // Suspend painting

	const bool bVectorModified = RemoveVisibleSourcesForOwner(toCollapse);
	RemoveSourceItemsForOwner(toCollapse);

	if (bVectorModified) { // If we modified the vector, we need to update the control
		RebuildListedItemsMap(); // Rebuild the map after removing rows.
		UpdateDownloadListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list before restoring state.
	}

	RestoreListState(0, kDownloadListViewState, false); // Restore selections and scroll state
	SetRedraw(true); // Resume painting
	if (bVectorModified)
		Invalidate(FALSE); // Force redraw
}

void CDownloadListCtrl::ExpandCollapseItem(int iItem, int iAction, bool bCollapseSource)
{
	if (IsChunkedRemoveDownloadSnapshotActive()) {
		MarkDeferredReload();
		return;
	}
	if (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size())
		return;

	CtrlItem_Struct* content = m_ListedItemsVector[static_cast<size_t>(iItem)];

	// To collapse/expand files when one of its sources is selected
	if (content && bCollapseSource && content->parent) {
		content = content->parent;
		if (!m_ListedItemsMap.Lookup(content, iItem) || iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size() || m_ListedItemsVector[static_cast<size_t>(iItem)] != content)
			return;
	}

	if (!content || content->type != FILE_TYPE || content->value == NULL)
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
			if (RemoveVisibleSourcesForOwner(partfile))
				RebuildListedItemsMap();
			SyncSourceItemsForOwner(partfile, content);

			int parentIdx = -1;
			if (m_ListedItemsMap.Lookup(content, parentIdx))
				InsertSortedVisibleSourcesForFile(partfile, parentIdx);

			RebuildListedItemsMap(); // Rebuild the map after inserting source rows.
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
	LPNMITEMACTIVATE pNMIA = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
	*pResult = 0;
	if (pNMIA == NULL || pNMIA->iItem < 0)
		return;

	if (GetListedItemType(pNMIA->iItem) != FILE_TYPE)
		return;

	CPartFile* pFile = ResolveListedDownloadFile(pNMIA->iItem);
	if (pFile == NULL)
		return;
	CPoint point;
	const bool bHasPoint = TryGetActionPoint(pNMIA, point);

	if (!thePrefs.IsDoubleClickEnabled()) {
		if (bHasPoint && IsPointOverPreviewActivationArea(pNMIA->iItem, point))
			return;

		ExpandCollapseItem(pNMIA->iItem, EXPAND_COLLAPSE);
		return;
	}

	if (bHasPoint && IsPointOverFileRatingIcon(pNMIA->iItem, point, pFile)) {
		ShowFileDialog(IDD_COMMENTLST);
		return;
	}

	if (bHasPoint && IsPointOverPreviewActivationArea(pNMIA->iItem, point)) {
		PreviewFileOrBeep(pFile);
		return;
	}

	ExpandCollapseItem(pNMIA->iItem, EXPAND_COLLAPSE);
}

void CDownloadListCtrl::OnContextMenu(CWnd*, CPoint point)
{
	SyncAutoRenameToMajorityNameMenuItem(m_FileMenu);
	const bool bShowAutoRenameToMajorityNameMenu = IsAutoRenameToMajorityNameModeEnabled();

	int iSel = GetNextItem(-1, LVIS_SELECTED);
	if (iSel >= 0) {
		const ItemType eSelectedItemType = GetListedItemType(iSel);
		if (eSelectedItemType == FILE_TYPE) {
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
			int iFilesAutoRenameToMajorityName = 0;
			int iFilesAutoRenameToMajorityNameApplicable = 0;
			UINT uPermMenuItem = 0;
			UINT uPrioMenuItem = 0;
			const CPartFile *file1 = NULL;

			bool bFirstItem = true;
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				const int iIdxSel = GetNextSelectedItem(pos);
				const CPartFile *pFile = ResolveListedDownloadFile(iIdxSel);
				if (pFile == NULL)
					continue;
				++iSelectedItems;

				const EPartFileStatus eStatus = pFile->GetStatus();
				const bool bCanCancel = eStatus != PS_COMPLETING;
				const bool bNotDone = eStatus != PS_COMPLETE && eStatus != PS_COMPLETING;
				const bool bCanPause = pFile->CanPauseFile();
				iFilesToCancel += static_cast<int>(bCanCancel);
				iFilesNotDone += static_cast<int>(bNotDone);
				iFilesToStop += static_cast<int>(pFile->CanStopFile());
				iFilesToPause += static_cast<int>(bCanPause);
				iFilesToResume += static_cast<int>(pFile->CanResumeFile());
				iFilesToOpen += static_cast<int>(pFile->CanOpenFile());
				iFilesGetPreviewParts += static_cast<int>(pFile->GetPreviewPrio());
				const bool bPreviewable = pFile->IsPreviewableFileType();
				const bool bReadyForPreview = pFile->IsReadyForPreview();
				iFilesPreviewType += static_cast<int>(bPreviewable);
				iFilesToPreview += static_cast<int>(bReadyForPreview);
				iFilesCanPauseOnPreview += static_cast<int>(bPreviewable && !bReadyForPreview && bCanPause);
				iFilesDoPauseOnPreview += static_cast<int>(pFile->IsPausingOnPreview());
				iFilesInCats += static_cast<int>(!pFile->HasDefaultCategory());
				iFilesToImport += static_cast<int>(pFile->GetFileOp() == PFOP_IMPORTPARTS);
				if (bNotDone) {
					++iFilesAutoRenameToMajorityNameApplicable;
					iFilesAutoRenameToMajorityName += static_cast<int>(pFile->IsAutoRenameToMajorityNameEnabled());
				}

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

				const UINT uCurPermMenuItem = GetSharePermissionMenuItem(pFile);
				if (bFirstItem) {
					bFirstItem = false;
					file1 = pFile;
					uPrioMenuItem = uCurPrioMenuItem;
					uPermMenuItem = uCurPermMenuItem;
				} else {
					if (uPrioMenuItem != uCurPrioMenuItem)
						uPrioMenuItem = 0;
					if (uPermMenuItem != uCurPermMenuItem)
						uPermMenuItem = 0;
				}
			}

			m_FileMenu.EnableMenuItem((UINT)m_PermMenu.m_hMenu, iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED);
			CString strDefaultPermission(GetResString(_T("DEFAULT")));
			const CString strGlobalPermission(GetSharePermissionLabel(thePrefs.GetSharePermissions()));
			if (!strGlobalPermission.IsEmpty())
				strDefaultPermission.AppendFormat(_T(" (%s)"), (LPCTSTR)strGlobalPermission);
			m_PermMenu.SetMenuText(MP_PERMDEFAULT, strDefaultPermission);
			UpdateSharePermissionMenuChecks(m_PermMenu, uPermMenuItem);
			m_FileMenu.EnableMenuItem((UINT)m_PrioMenu.m_hMenu, iFilesNotDone > 0 ? MF_ENABLED : MF_GRAYED);
			m_PrioMenu.CheckMenuRadioItem(MP_PRIOLOW, MP_PRIOAUTO, uPrioMenuItem, MF_BYCOMMAND);

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
			if (bShowAutoRenameToMajorityNameMenu) {
				const bool bAutoRenameToMajorityNameMixed = iFilesAutoRenameToMajorityNameApplicable > 1 && iFilesAutoRenameToMajorityName > 0 && iFilesAutoRenameToMajorityName < iFilesAutoRenameToMajorityNameApplicable;
				m_FileMenu.SetMenuText(MP_AUTORENAMETOMAJORITYNAME, GetAutoRenameToMajorityNameLabel(bAutoRenameToMajorityNameMixed));
				m_FileMenu.EnableMenuItem(MP_AUTORENAMETOMAJORITYNAME, iFilesAutoRenameToMajorityNameApplicable > 0 ? MF_ENABLED : MF_GRAYED);
				m_FileMenu.CheckMenuItem(MP_AUTORENAMETOMAJORITYNAME, MF_BYCOMMAND | ((!bAutoRenameToMajorityNameMixed && iFilesAutoRenameToMajorityNameApplicable > 0 && iFilesAutoRenameToMajorityName == iFilesAutoRenameToMajorityNameApplicable) ? MF_CHECKED : MF_UNCHECKED));
			}
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

			m_FileMenu.EnableMenuItem(MP_SHOWED2KLINK, iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED);
			m_FileMenu.EnableMenuItem(MP_CUT, iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED);
			if (thePrefs.GetShowCopyEd2kLinkCmd())
				m_FileMenu.EnableMenuItem(MP_GETED2KLINK, iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED);
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
			if (eSelectedItemType != AVAILABLE_SOURCE && eSelectedItemType != UNAVAILABLE_SOURCE)
				return;
			CScopedDownloadClientRef selectedClientRef(AcquireListedSourceClient(iSel));
			const CUpDownClient *pSelectedClient = selectedClientRef.Get();
			if (pSelectedClient == NULL)
				return;
			const bool is_ed2k = pSelectedClient->IsEd2kClient();
			const CFriend *pFriend = pSelectedClient->GetFriend();
			CMenuXP ClientMenu;
			ClientMenu.CreatePopupMenu();
			ClientMenu.AddMenuSidebar(GetResString(_T("CLIENTS")));
			ClientMenu.AppendMenu(MF_STRING, MP_DETAIL, GetResString(_T("SHOWDETAILS")), _T("CLIENTDETAILS"));
			ClientMenu.SetDefaultItem(MP_DETAIL);
			ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && !pSelectedClient->IsFriend()) ? MF_ENABLED : MF_GRAYED), MP_ADDFRIEND, GetResString(_T("ADDFRIEND")), _T("ADDFRIEND"));
			ClientMenu.AppendMenu(MF_STRING | (pFriend != NULL ? MF_ENABLED : MF_GRAYED), MP_FRIENDSLOT, GetResString(_T("FRIENDSLOT")), _T("FRIENDSLOT"));
			ClientMenu.CheckMenuItem(MP_FRIENDSLOT, (pFriend != NULL && pFriend->GetFriendSlot()) ? MF_CHECKED : MF_UNCHECKED);
			ClientMenu.AppendMenu(MF_STRING | (is_ed2k ? MF_ENABLED : MF_GRAYED), MP_MESSAGE, GetResString(_T("SEND_MSG")), _T("SENDMESSAGE"));
			ClientMenu.AppendMenu(MF_STRING | ((pSelectedClient->GetViewSharedFilesSupport() && is_ed2k) ? MF_ENABLED : MF_GRAYED), MP_SHOWLIST, GetResString(_T("VIEWFILES")), _T("VIEWFILES"));
			ClientMenu.AppendMenu(MF_STRING | MF_ENABLED, MP_EDIT_NOTE, GetResString(_T("EDIT_CLIENT_NOTE")), _T("RENAME"));
			if (Kademlia::CKademlia::IsRunning() && !Kademlia::CKademlia::IsConnected())
				ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && pSelectedClient->GetKadPort() != 0 && pSelectedClient->GetKadVersion() >= KADEMLIA_VERSION2_47a) ? MF_ENABLED : MF_GRAYED), MP_BOOT, GetResString(_T("BOOTSTRAP")));

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
			const int m_PunishmentMenuItem = MP_PUNISMENT_IPUSERHASHBAN + pSelectedClient->m_uPunishment;
			m_PunishmentMenu.CheckMenuRadioItem(MP_PUNISMENT_IPUSERHASHBAN, MP_PUNISMENT_NONE, m_PunishmentMenuItem, 0);
			ClientMenu.AppendMenu(MF_STRING | MF_POPUP | MF_ENABLED, (UINT_PTR)m_PunishmentMenu.m_hMenu, GetResString(_T("PUNISHMENT")), _T("PUNISHMENT"));
			ClientMenu.AppendMenu(MF_STRING | MF_SEPARATOR);

			ClientMenu.AppendMenu(MF_STRING | (GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED), MP_FIND, GetResString(_T("FIND")), _T("Search"));
			CMenuXP A4AFMenu;
			A4AFMenu.CreateMenu();
			if (thePrefs.IsExtControlsEnabled()) {
#ifdef _DEBUG
				if (eSelectedItemType == UNAVAILABLE_SOURCE)
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
		m_FileMenu.EnableMenuItem((UINT)m_PermMenu.m_hMenu, MF_GRAYED);
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
		if (bShowAutoRenameToMajorityNameMenu) {
			m_FileMenu.SetMenuText(MP_AUTORENAMETOMAJORITYNAME, GetAutoRenameToMajorityNameLabel(false));
			m_FileMenu.EnableMenuItem(MP_AUTORENAMETOMAJORITYNAME, MF_GRAYED);
			m_FileMenu.CheckMenuItem(MP_AUTORENAMETOMAJORITYNAME, MF_BYCOMMAND | MF_UNCHECKED);
		}
		if (thePrefs.m_bImportParts)
			m_FileMenu.EnableMenuItem(MP_IMPORTPARTS, MF_GRAYED);

		m_FileMenu.EnableMenuItem(MP_CLEARCOMPLETED, GetCompleteDownloads(curTab, total) > 0 ? MF_ENABLED : MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_SHOWED2KLINK, MF_GRAYED);
		m_FileMenu.EnableMenuItem(MP_CUT, MF_GRAYED);
		if (thePrefs.GetShowCopyEd2kLinkCmd())
			m_FileMenu.EnableMenuItem(MP_GETED2KLINK, MF_GRAYED);
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
		if (iSel >= 0 && GetListedItemType(iSel) == FILE_TYPE) {
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				const int iIdxSel = GetNextSelectedItem(pos);
				const CPartFile *pFile = ResolveListedDownloadFile(iIdxSel);
				if (pFile != NULL)
					iFilesInCats += static_cast<int>((!pFile->HasDefaultCategory()));
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
			label = thePrefs.GetCategoryDisplayTitle(i);
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
		if (GetListedItemType(iSel) == FILE_TYPE) {
			bool bFirstItem = true;
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				const int iIdxSel = GetNextSelectedItem(pos);
				const CPartFile *pFile = ResolveListedDownloadFile(iIdxSel);
				if (pFile == NULL)
					continue;
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
	m_PrioMenu.CheckMenuRadioItem(MP_PRIOLOW, MP_PRIOAUTO, uPrioMenuItem, MF_BYCOMMAND);
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
	case MP_CLEARCOMPLETED:
		ClearCompleted();
		return TRUE;
	}

	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	if (iSel < 0)
		iSel = GetNextItem(-1, LVIS_SELECTED);
	if (iSel >= 0) {
		const ItemType eSelectedItemType = GetListedItemType(iSel);
		if (eSelectedItemType == FILE_TYPE) {
			//for multiple selections
			unsigned selectedCount = 0;
			CTypedPtrList<CPtrList, CPartFile*> selectedList;
			CStringArray selectedHashes;
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				const int index = GetNextSelectedItem(pos);
				if (index < 0)
					continue;

				CPartFile *pSelectedFile = ResolveListedDownloadFile(index);
				if (pSelectedFile == NULL)
					continue;

				++selectedCount;
				selectedList.AddTail(pSelectedFile);
				selectedHashes.Add(md4str(pSelectedFile->GetFileHash()));
			}

			CPartFile *file = ResolveListedDownloadFile(iSel);
			if (file == NULL)
				return TRUE;
			bool m_bAddToCanceledMet = true;

			switch (wParam) {
			case MP_PERMDEFAULT:
			case MP_PERMNONE:
			case MP_PERMFRIENDS:
			case MP_PERMALL:
				{
					EChunkedDownloadStateAction eAction = ChunkedDownloadStatePermissionAll;
					switch (wParam) {
					case MP_PERMDEFAULT:
						eAction = ChunkedDownloadStatePermissionDefault;
						break;
					case MP_PERMNONE:
						eAction = ChunkedDownloadStatePermissionNone;
						break;
					case MP_PERMFRIENDS:
						eAction = ChunkedDownloadStatePermissionFriends;
						break;
					default:
						eAction = ChunkedDownloadStatePermissionAll;
						break;
					}
					theApp.ExecuteDownloadListStateCommand(selectedHashes, static_cast<UINT>(eAction), 0);
				}
				return TRUE;
			case MP_CANCEL_FORGET:
				m_bAddToCanceledMet = false;
			case MP_CANCEL:
			case MPG_DELETE: // Keyboard delete removes completed files from the list; cancel commands delete them from disk.
				if (selectedCount > 0) {
					CString fileList(GetResString(selectedCount == 1 ? _T("Q_CANCELDL2") : _T("Q_CANCELDL")));
					CStringArray removableHashes;
					bool validdelete = false;
					bool removecompl = false;
					int cFiles = 0;
					const int iMaxDisplayFiles = 10;
					for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
						const CPartFile *cur_file = selectedList.GetNext(pos);
						if (cur_file == NULL)
							continue;

						const EPartFileStatus eStatus = cur_file->GetStatus();
						if (eStatus == PS_COMPLETING)
							continue;
						if (eStatus == PS_COMPLETE && wParam == MPG_DELETE) {
							removecompl = true;
							removableHashes.Add(md4str(cur_file->GetFileHash()));
							continue;
						}

						validdelete = true;
						removableHashes.Add(md4str(cur_file->GetFileHash()));
						if (++cFiles < iMaxDisplayFiles)
							fileList.AppendFormat(_T("\n%s"), (LPCTSTR)cur_file->GetFileName());
						else if (cFiles == iMaxDisplayFiles && pos != NULL)
							fileList += _T("\n...");
					}

					if (removableHashes.GetSize() > 0 && removecompl && !validdelete) {
						theApp.ExecuteDownloadListStateCommand(removableHashes, static_cast<UINT>(ChunkedDownloadStateClearCompleted), -1);
						return TRUE;
					}

					if (removableHashes.GetSize() > 0 && validdelete && CDarkMode::MessageBox(fileList, MB_DEFBUTTON2 | MB_ICONQUESTION | MB_YESNO) == IDYES) {
						theApp.ExecuteDownloadListRemoveCommand(removableHashes, m_bAddToCanceledMet, wParam == MP_CANCEL || wParam == MP_CANCEL_FORGET);
						return TRUE;
					}
				}
				break;
			case MP_PRIOHIGH:
				theApp.ExecuteDownloadListStateCommand(selectedHashes, static_cast<UINT>(ChunkedDownloadStatePriorityHigh), 0);
				return TRUE;
			case MP_PRIOLOW:
				theApp.ExecuteDownloadListStateCommand(selectedHashes, static_cast<UINT>(ChunkedDownloadStatePriorityLow), 0);
				return TRUE;
			case MP_PRIONORMAL:
				theApp.ExecuteDownloadListStateCommand(selectedHashes, static_cast<UINT>(ChunkedDownloadStatePriorityNormal), 0);
				return TRUE;
			case MP_PRIOAUTO:
				theApp.ExecuteDownloadListStateCommand(selectedHashes, static_cast<UINT>(ChunkedDownloadStatePriorityAuto), 0);
				return TRUE;
			case MP_PAUSE:
				theApp.ExecuteDownloadListStateCommand(selectedHashes, static_cast<UINT>(ChunkedDownloadStatePause), 0);
				return TRUE;
			case MP_RESUME:
				theApp.ExecuteDownloadListStateCommand(selectedHashes, static_cast<UINT>(ChunkedDownloadStateResume), 0);
				return TRUE;
			case MP_STOP:
				theApp.ExecuteDownloadListStateCommand(selectedHashes, static_cast<UINT>(ChunkedDownloadStateStop), 0);
				return TRUE;
			case MPG_F2:
				if (GetKeyState(VK_CONTROL) < 0 || selectedCount > 1) {
					// when ctrl is pressed -> filename cleanup
					if (IDYES == LocMessageBox(_T("MANUAL_FILENAMECLEANUP"), MB_YESNO, 0)) {
						theApp.ExecuteDownloadListStateCommand(selectedHashes, static_cast<UINT>(ChunkedDownloadStateCleanupFilename), 0);
						return TRUE;
					}
				} else {
					if (file->GetStatus() != PS_COMPLETE && file->GetStatus() != PS_COMPLETING) {
						InputBox inputbox;
						inputbox.SetLabels(GetResNoAmp(_T("RENAME")), GetResString(_T("DL_FILENAME")), file->GetFileName());
						inputbox.SetEditFilenameMode();
						if (inputbox.DoModal() == IDOK && !inputbox.GetInput().IsEmpty() && IsValidEd2kString(inputbox.GetInput())) {
							theApp.ExecuteDownloadListStateTextCommand(selectedHashes, static_cast<UINT>(ChunkedDownloadStateSetFileName), 0, inputbox.GetInput());
							return TRUE;
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
				if (selectedCount == 1) {
					theApp.ExecuteDownloadListStateCommand(selectedHashes, static_cast<UINT>(ChunkedDownloadStateTogglePreviewPriority), 0);
					return TRUE;
				}
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
				theApp.ExecuteSaveAppStateCommand(false, _T("DownloadListCtrl"));
				break;
			case MP_RELOADCONF:
				thePrefs.LoadBlacklistFile(); // Loads blacklist.conf
				theApp.shield->LoadShieldFile(); // Loads shield.conf
				break;
			case MP_BACKUP:
				theApp.Backup(false);
				break;
			case MP_DOWNLOADINSPECTOR:
				theApp.emuledlg->transferwnd->GetDownloadList()->DownloadInspector(true);
				break;
			case MP_PAUSEONPREVIEW:
				{
					bool bAllPausedOnPreview = true;
					for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL && bAllPausedOnPreview;)
						bAllPausedOnPreview = static_cast<CPartFile*>(selectedList.GetNext(pos))->IsPausingOnPreview();
					const bool bNewPauseOnPreview = !bAllPausedOnPreview;
					theApp.ExecuteDownloadListStateCommand(selectedHashes, static_cast<UINT>(ChunkedDownloadStateSetPauseOnPreview), bNewPauseOnPreview ? 1 : 0);
					return TRUE;
				}
				break;
			case MP_VIEWFILECOMMENTS:
				ShowFileDialog(IDD_COMMENTLST);
				break;
			case MP_AUTORENAMETOMAJORITYNAME:
				if (!IsAutoRenameToMajorityNameModeEnabled())
					break;
				theApp.ExecuteDownloadListStateCommand(selectedHashes, static_cast<UINT>(ChunkedDownloadStateToggleAutoRenameToMajorityName), 0);
				return TRUE;
			case MP_IMPORTPARTS:
				if (selectedCount == 1) {
					if (file->GetFileOp() == PFOP_IMPORTPARTS)
						theApp.ExecuteDownloadListStateTextCommand(selectedHashes, static_cast<UINT>(ChunkedDownloadStateImportParts), 0, NULL);
					else {
						CFileDialog dlg(true, NULL, NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY);
						if (dlg.DoModal() == IDOK)
							theApp.ExecuteDownloadListStateTextCommand(selectedHashes, static_cast<UINT>(ChunkedDownloadStateImportParts), 0, dlg.GetPathName());
					}
				}
				return TRUE;
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
						theApp.ExecuteDownloadListStateCommand(selectedHashes, static_cast<UINT>(ChunkedDownloadStateSetSourceLimit), newlimit);
						return TRUE;
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
					theApp.ExecuteDownloadListStateCommand(selectedHashes, static_cast<UINT>(ChunkedDownloadStateSetCategory), nCatNumber);
					return TRUE;
				} else if (wParam >= MP_PREVIEW_APP_MIN && wParam <= MP_PREVIEW_APP_MAX)
					thePreviewApps.RunApp(file, (UINT)wParam);
			}
			} else if (eSelectedItemType != INVALID_TYPE) {
				CScopedDownloadClientRef clientRef(AcquireListedSourceClient(iSel));
				CUpDownClient *client = clientRef.Get();
				if (client == NULL)
					return TRUE;
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
					UpdateItem(client);
				break;
			case MP_FRIENDSLOT:
				{
					CFriend *pFriend = client->GetFriend();
					if (pFriend != NULL) {
						pFriend->SetFriendSlot(!pFriend->GetFriendSlot());
						theApp.friendlist->SaveList();
						UpdateItem(client);
					}
				}
				break;
			case MP_DETAIL:
			case MPG_ALTENTER:
				ShowClientDialog(client);
				break;
				case MP_PUNISMENT_IPUSERHASHBAN:
					theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_IP_BAN")), PR_MANUAL, P_IPUSERHASHBAN);
					RefreshQueueCountAfterManualPunishment();
					break;
				case MP_PUNISMENT_USERHASHBAN:
					theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_USER_HASH_BAN")), PR_MANUAL, P_USERHASHBAN);
					RefreshQueueCountAfterManualPunishment();
					break;
				case MP_PUNISMENT_UPLOADBAN:
					theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_UPLOAD_BAN")), PR_MANUAL, P_UPLOADBAN);
					RefreshQueueCountAfterManualPunishment();
					break;
				case MP_PUNISMENT_SCOREX01:
					theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX01);
					RefreshQueueCountAfterManualPunishment();
					break;
				case MP_PUNISMENT_SCOREX02:
					theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX02);
					RefreshQueueCountAfterManualPunishment();
					break;
				case MP_PUNISMENT_SCOREX03:
					theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX03);
					RefreshQueueCountAfterManualPunishment();
					break;
				case MP_PUNISMENT_SCOREX04:
					theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX04);
					RefreshQueueCountAfterManualPunishment();
					break;
				case MP_PUNISMENT_SCOREX05:
					theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX05);
					RefreshQueueCountAfterManualPunishment();
					break;
				case MP_PUNISMENT_SCOREX06:
					theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX06);
					RefreshQueueCountAfterManualPunishment();
					break;
				case MP_PUNISMENT_SCOREX07:
					theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX07);
					RefreshQueueCountAfterManualPunishment();
					break;
				case MP_PUNISMENT_SCOREX08:
					theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX08);
					RefreshQueueCountAfterManualPunishment();
					break;
				case MP_PUNISMENT_SCOREX09:
					theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX09);
					RefreshQueueCountAfterManualPunishment();
					break;
				case MP_PUNISMENT_NONE:
					theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_CANCELATION")), PR_MANUAL, P_NOPUNISHMENT);
					RefreshQueueCountAfterManualPunishment();
					break;
			case MP_BOOT:
				if (theApp.emuledlg != NULL && !theApp.emuledlg->CanUseP2PConnectionCommands()) {
					theApp.emuledlg->LogP2PConnectionCommandBlocked(true);
					break;
				}
				if (client->GetKadPort() && client->GetKadVersion() >= KADEMLIA_VERSION2_47a)
					Kademlia::CKademlia::Bootstrap(client->GetIPv4().ToUInt32(true), client->GetKadPort());
#ifdef _DEBUG
				break;
			case MP_A4AF_CHECK_THIS_NOW:
				{
					CPartFile *file = ResolveListedParentDownloadFile(iSel);
					if (file != NULL && (file->GetStatus(false) == PS_READY || file->GetStatus(false) == PS_EMPTY)) {
						if (client->GetDownloadState() != DS_DOWNLOADING) {
							client->SwapToAnotherFile(_T("Manual init of source check. Test to be like ProcessA4AFClients(). CDownloadListCtrl::OnCommand() MP_SWAP_A4AF_DEBUG_THIS"), false, false, false, NULL, true, true, true); // ZZ:DownloadManager
							UpdateItem(file);
						}
					}
				}
#endif
			}
		}
	} else if (wParam == MP_SAVEAPPSTATE) // nothing selected
		theApp.ExecuteSaveAppStateCommand(false, _T("DownloadListCtrl"));
	else if (wParam == MP_RELOADCONF) { // nothing selected
		thePrefs.LoadBlacklistFile(); // Loads blacklist.conf
		theApp.shield->LoadShieldFile(); // Loads shield.conf
	}
	else if (wParam == MP_BACKUP) // nothing selected
		theApp.Backup(false);
	else if (wParam == MP_DOWNLOADINSPECTOR) // nothing selected
		theApp.emuledlg->transferwnd->GetDownloadList()->DownloadInspector(true);

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
				if (m_pDownloadListCtrl->GetListedItemType(iItem) == FILE_TYPE) {
					CPartFile* pPartFile = m_pDownloadListCtrl->ResolveListedDownloadFile(iItem);
					if (pPartFile == NULL || !pPartFile->IsReadyForPreview())
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
	if (item1 == NULL || item2 == NULL)
		return item1 == item2 ? 0 : (item1 == NULL ? 1 : -1);


	int iResult;
	if (item1->type == FILE_TYPE && item2->type != FILE_TYPE) {
		if (item2->owner == NULL)
			return -1;
		if (item1->value == item2->owner)
			return -1;
		iResult = Compare(static_cast<CPartFile*>(item1->value), item2->owner, lParamSort);
	} else if (item1->type != FILE_TYPE && item2->type == FILE_TYPE) {
		if (item1->owner == NULL)
			return 1;
		if (item1->owner == item2->value)
			return 1;
		iResult = Compare(item1->owner, static_cast<CPartFile*>(item2->value), lParamSort);
	} else if (item1->type == FILE_TYPE)
		iResult = Compare(static_cast<CPartFile*>(item1->value), static_cast<CPartFile*>(item2->value), lParamSort);
	else {
		if (item1->owner == NULL || item2->owner == NULL) {
			const DWORD_PTR uItem1 = reinterpret_cast<DWORD_PTR>(item1);
			const DWORD_PTR uItem2 = reinterpret_cast<DWORD_PTR>(item2);
			return uItem1 == uItem2 ? 0 : (uItem1 < uItem2 ? -1 : 1);
		}
		if (item1->owner != item2->owner) {
			iResult = Compare(item1->owner, item2->owner, lParamSort);
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

	CStringArray completedHashes;
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct *cur_item = it->second;
		if (cur_item == NULL || cur_item->type != FILE_TYPE || cur_item->value == NULL)
			continue;

		if (cur_item->value == NULL)
			continue;

		CPartFile *file = static_cast<CPartFile*>(cur_item->value);
		if (!file->IsPartFile() && (!IsFilteredOut(file) || incat == -1))
			completedHashes.Add(!cur_item->strOwnerHash.IsEmpty() ? cur_item->strOwnerHash : md4str(file->GetFileHash()));
	}

	if (completedHashes.GetSize() == 0)
		return;

	theApp.ExecuteDownloadListStateCommand(completedHashes, static_cast<UINT>(ChunkedDownloadStateClearCompleted), incat);
}

void CDownloadListCtrl::ClearCompleted(const CPartFile *pFile)
{
	if (pFile == NULL || pFile->IsPartFile())
		return;

	CStringArray completedHashes;
	completedHashes.Add(md4str(pFile->GetFileHash()));
	theApp.ExecuteDownloadListStateCommand(completedHashes, static_cast<UINT>(ChunkedDownloadStateClearCompleted), -1);
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
	m_availableCommandsDirty = true;
	if (m_bChunkedRemoveDownloadListStateBatchActive || m_bChunkedDownloadStateListStateBatchActive)
		return;

	NMLISTVIEW *pNMListView = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	if (pNMListView == NULL || pNMListView->iItem < 0)
		return;

	//this works because true is equal to 1 and false equal to 0
	int notLast = static_cast<int>(pNMListView->iItem + 1 != GetItemCount());
	int notFirst = static_cast<int>(pNMListView->iItem != 0);
	RedrawItems(pNMListView->iItem - notFirst, pNMListView->iItem + notLast);
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
		return CompareUnsigned(GetLiveFileDownloadDatarate(file1), GetLiveFileDownloadDatarate(file2));
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
		return CompareLocaleStringNoCase(thePrefs.GetCategoryDisplayTitle(const_cast<CPartFile*>(file1)->GetCategory()), thePrefs.GetCategoryDisplayTitle(const_cast<CPartFile*>(file2)->GetCategory()));
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
		return CompareUnsigned(client1->GetTransferredDown(), client2->GetTransferredDown());
	case 3: //completed
		return 0;
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

bool CDownloadListCtrl::TryGetActionPoint(const NMITEMACTIVATE* pNMIA, CPoint& point)
{
	if (pNMIA != NULL && pNMIA->ptAction.x >= 0 && pNMIA->ptAction.y >= 0) {
		point = pNMIA->ptAction;
		return true;
	}

	if (!::GetCursorPos(&point))
		return false;

	ScreenToClient(&point);
	return true;
}

bool CDownloadListCtrl::IsPointOverFilePreviewIcon(int iItem, const CPoint& point)
{
	CRect rcIcon;
	if (iItem < 0 || !GetItemRect(iItem, &rcIcon, LVIR_ICON))
		return false;

	const CSize iconSize = theApp.GetSmallSytemIconSize();
	rcIcon.right = rcIcon.left + iconSize.cx;
	rcIcon.bottom = rcIcon.top + iconSize.cy;
	return rcIcon.PtInRect(point) != FALSE;
}

bool CDownloadListCtrl::IsPointOverFileNameColumn(int iItem, const CPoint& point)
{
	if (iItem < 0)
		return false;

	LVHITTESTINFO subhit = {};
	subhit.pt = point;
	return SubItemHitTest(&subhit) >= 0 && subhit.iItem == iItem && subhit.iSubItem == 0;
}

bool CDownloadListCtrl::IsPointOverFileRatingIcon(int iItem, const CPoint& point, const CPartFile* pFile)
{
	if (pFile == NULL || !thePrefs.ShowRatingIndicator() || (!pFile->HasComment() && !pFile->HasRating() && !pFile->IsKadCommentSearchRunning()))
		return false;

	CRect rcIcon;
	if (iItem < 0 || !GetItemRect(iItem, &rcIcon, LVIR_ICON))
		return false;

	const CSize iconSize = theApp.GetSmallSytemIconSize();
	CRect rcRating(rcIcon.left + iconSize.cx + 2, rcIcon.top, rcIcon.left + iconSize.cx + 2 + RATING_ICON_WIDTH, rcIcon.top + iconSize.cy);
	return rcRating.PtInRect(point) != FALSE;
}

bool CDownloadListCtrl::IsPointOverPreviewActivationArea(int iItem, const CPoint& point)
{
	if (!thePrefs.GetPreviewOnIconDblClk())
		return false;

	if (thePrefs.GetPreviewOnFileNameDblClk() && IsPointOverFileNameColumn(iItem, point))
		return true;

	return IsPointOverFilePreviewIcon(iItem, point);
}

void CDownloadListCtrl::PreviewFileOrBeep(CPartFile* pFile)
{
	if (pFile == NULL)
		return;

	if (pFile->IsReadyForPreview())
		pFile->PreviewFile();
	else
		MessageBeep(MB_OK);
}

void CDownloadListCtrl::OnNmDblClk(LPNMHDR pNMHDR, LRESULT* pResult)
{
	*pResult = 0;

	LPNMITEMACTIVATE pNMIA = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
	int iSel = (pNMIA != NULL && pNMIA->iItem >= 0) ? pNMIA->iItem : GetSelectionMark();
	if (iSel < 0)
		return;

	const ItemType eSelectedItemType = GetListedItemType(iSel);
	if (eSelectedItemType == INVALID_TYPE)
		return;

	if (eSelectedItemType != FILE_TYPE) {
		CScopedDownloadClientRef clientRef(AcquireListedSourceClient(iSel));
		if (clientRef.Get() != NULL)
			ShowClientDialog(clientRef.Get());
		return;
	}

	if (thePrefs.IsDoubleClickEnabled())
		return;

	CPoint point;
	if (!TryGetActionPoint(pNMIA, point))
		return;

	LVHITTESTINFO subhit = {};
	subhit.pt = point;
	if (SubItemHitTest(&subhit) < 0 || subhit.iItem != iSel || subhit.iSubItem != 0)
		return;

	CPartFile* pFile = ResolveListedDownloadFile(iSel);
	if (pFile == NULL)
		return;
	if (IsPointOverFileRatingIcon(iSel, point, pFile)) {
		ShowFileDialog(IDD_COMMENTLST);
		return;
	}

	if (IsPointOverPreviewActivationArea(iSel, point)) {
		PreviewFileOrBeep(pFile);
		return;
	}

	return;
}

void CDownloadListCtrl::CreateMenus()
{
	if (m_PreviewMenu)
		VERIFY(m_PreviewMenu.DestroyMenu());
	if (m_PermMenu)
		VERIFY(m_PermMenu.DestroyMenu());
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

	m_PermMenu.CreateMenu();
	m_PermMenu.AppendMenu(MF_STRING, MP_PERMDEFAULT, GetResString(_T("DEFAULT")));
	m_PermMenu.AppendMenu(MF_STRING, MP_PERMNONE, GetResString(_T("SHARE_PERMISSION_HIDDEN")));
	m_PermMenu.AppendMenu(MF_STRING, MP_PERMFRIENDS, GetResString(_T("SHARE_PERMISSION_FRIENDSONLY")));
	m_PermMenu.AppendMenu(MF_STRING, MP_PERMALL, GetResString(_T("SHARE_PERMISSION_EVERYBODY")));
	m_FileMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_PermMenu.m_hMenu, GetResString(_T("SHARE_PERMISSION_GROUP")), _T("FRIEND"));

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
	if (IsAutoRenameToMajorityNameModeEnabled())
		m_FileMenu.AppendMenu(MF_STRING, MP_AUTORENAMETOMAJORITYNAME, GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_RENAME_TO_MAJORITY_NAME")), _T("EDIT"));
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
	m_FileMenu.AppendMenu(MF_STRING, MP_SHOWED2KLINK, GetResString(_T("DL_SHOWED2KLINK")), _T("ED2KLINK"));
	m_FileMenu.AppendMenu(MF_STRING, MP_CUT, GetResString(_T("COPY_FILE_NAMES")), _T("FILERENAME"));
	if (thePrefs.GetShowCopyEd2kLinkCmd())
		m_FileMenu.AppendMenu(MF_STRING, MP_GETED2KLINK, GetResString(_T("DL_LINK1")), _T("ED2KLINK"));
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
		if (cur_item != NULL && cur_item->type == FILE_TYPE && cur_item->value != NULL) {
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
		if (cur_item != NULL && cur_item->type == FILE_TYPE && cur_item->value != NULL) {
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
		if (cur_item != NULL && cur_item->type == FILE_TYPE)
			iCount++;
	}
	return iCount;
}

CString CDownloadListCtrl::GetFileItemDisplayText(const CPartFile *lpPartFile, int iSubItem) const
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
		if (lpPartFile->GetStatus() != PS_COMPLETE && lpPartFile->GetStatus() != PS_COMPLETING) {
			const uint32 uDatarate = GetLiveFileDownloadDatarate(lpPartFile);
			if (lpPartFile->GetTransferringSrcCount() || uDatarate > 0)
				sText = CastItoXBytes(uDatarate, false, true);
		}
		break;
	case 5: //progress
		sText.Format(_T("%s: %.1f%%"), (LPCTSTR)GetResString(_T("DL_PROGRESS")), lpPartFile->GetPercentCompleted());
		break;
	case 6: //sources
		sText = GetLiveFileSourceDisplayText(lpPartFile);
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
			const uint32 uDatarate = GetLiveFileDownloadDatarate(lpPartFile);
			if (restTime < 0 && uDatarate > 0)
				restTime = static_cast<time_t>(static_cast<uint64>(lpPartFile->GetFileSize() - lpPartFile->GetCompletedSize()) / uDatarate);
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
			sText = thePrefs.GetCategoryDisplayTitle(cat);
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

	const int iSelectionMark = GetSelectionMark();
	const ItemType eSelectedItemType = GetListedItemType(iSelectionMark);
	if (eSelectedItemType == FILE_TYPE) {
		const CPartFile *file = ResolveListedDownloadFile(iSelectionMark);
		if (file == NULL)
			return;
		bool b = (thePrefs.ShowRatingIndicator()
			&& (file->HasComment() || file->HasRating() || file->IsKadCommentSearchRunning())
			&& point.x >= sm_iIconOffset + theApp.GetSmallSytemIconSize().cx
			&& point.x <= sm_iIconOffset + theApp.GetSmallSytemIconSize().cx + RATING_ICON_WIDTH);
		ShowFileDialog(b ? IDD_COMMENTLST : 0);
	} else if (eSelectedItemType != INVALID_TYPE) {
		CScopedDownloadClientRef clientRef(AcquireListedSourceClient(iSelectionMark));
		if (clientRef.Get() != NULL)
			ShowClientDialog(clientRef.Get());
	}
}

int CDownloadListCtrl::GetCompleteDownloads(int cat, int &total)
{
	total = 0;
	int count = 0;
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct *cur_item = it->second;
		if (cur_item != NULL && cur_item->type == FILE_TYPE && cur_item->value != NULL) {
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
	if (IsChunkedRemoveDownloadSnapshotActive()) {
		MarkDeferredReload();
		ApplyChunkedRemoveDownloadVisibleItemCount(false);
		return;
	}

	ReloadList(false, kDownloadListViewState);
}

void CDownloadListCtrl::UpdateCurrentCategoryView(CPartFile *thisfile)
{
	if (IsChunkedRemoveDownloadSnapshotActive()) {
		MarkDeferredReload();
		ApplyChunkedRemoveDownloadVisibleItemCount(false);
		return;
	}

	ListItems::const_iterator it = m_ListItems.find(thisfile);
	if (it != m_ListItems.end()) {
		const CtrlItem_Struct *cur_item = it->second;
		if (cur_item != NULL && cur_item->type == FILE_TYPE && cur_item->value != NULL) {
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
	if (IsChunkedRemoveDownloadSnapshotActive()) {
		MarkDeferredReload();
		return;
	}

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
			if (cur == fileItem && m_uListedFilesCount > 0)
				--m_uListedFilesCount;
			m_ListedItemsMap.RemoveKey(cur); // Remove from map
			m_ListedItemsVector.erase(m_ListedItemsVector.begin() + i); // Remove from vector
		} else
			++i; // Only increment if we did not remove the item, otherwise we skip the next item.
	}

	RebuildListedItemsMap(); // Rebuild the map after sorting.
	RequestTransferListRedraw();
	UpdateDownloadListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list before restoring state.
	RestoreListState(0, kDownloadListViewState, false); // Restore selections and scroll state
	SetRedraw(true); // Resume painting
}

void CDownloadListCtrl::ShowFile(CPartFile* toshow)
{
	if (theApp.IsClosing() || !toshow || theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible())
		return;
	if (IsChunkedRemoveDownloadSnapshotActive()) {
		MarkDeferredReload();
		return;
	}

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
	++m_uListedFilesCount;
	const bool bOldRawSortState = m_bRawSortInProgress;
	m_bRawSortInProgress = true;
	CombinedSort(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), SortFunc); // Keep current sort order.
	m_bRawSortInProgress = bOldRawSortState;
	RebuildListedItemsMap(); // Rebuild the map after sorting.
	RequestTransferListRedraw();
	UpdateDownloadListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list before restoring state.
	RestoreListState(0, kDownloadListViewState, false); // Restore selections and scroll state
	SetRedraw(true); // Resume painting
}

void CDownloadListCtrl::ChangeCategory(int newsel)
{
	if (curTab == newsel)
		return; // No change, so do nothing.

	curTab = newsel;
	
	// Mark cached commands for rebuild after category change since visibility changed
	if (theApp.emuledlg && theApp.emuledlg->transferwnd)
		theApp.emuledlg->transferwnd->InvalidateCatTabInfo();
	if (IsChunkedRemoveDownloadSnapshotActive()) {
		MarkDeferredReload();
		ApplyChunkedRemoveDownloadVisibleItemCount(false);
		return;
	}
	
	ReloadList(false, kDownloadListViewState);
}

void CDownloadListCtrl::GetDisplayedPartFiles(CArray<CPartFile*, CPartFile*> *list)
{
	if (list == NULL || theApp.downloadqueue == NULL)
		return;

	for (POSITION pos = theApp.downloadqueue->filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *file = theApp.downloadqueue->filelist.GetNext(pos);
		if (file != NULL && file->IsPartFile())
			list->Add(file);
	}
}

void CDownloadListCtrl::MoveCompletedfilesCat(UINT from, UINT to)
{
	const UINT cmin = min(from, to);
	const UINT cmax = max(from, to);
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct *cur_item = it->second;
		if (cur_item != NULL && cur_item->type == FILE_TYPE && cur_item->value != NULL) {
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
		if (rItem.mask & LVIF_TEXT)
			_tcsncpy_s(rItem.pszText, rItem.cchTextMax, GetListedItemDisplayText(rItem.iItem, rItem.iSubItem), _TRUNCATE);
	}
	*pResult = 0;
}

bool CDownloadListCtrl::GetPersistentInfoTipText(const SPersistentInfoTipContext& context, CString& strText)
{
	const int iMaxInfoLength = 4096;

	const ItemType eItemType = GetListedItemType(context.iItem);
	if (eItemType == INVALID_TYPE)
		return false;

	CString info;

	// Build info text and display it.
	if (eItemType == FILE_TYPE) {
		const CPartFile *pFile = ResolveListedDownloadFile(context.iItem);
		if (pFile == NULL)
			return false;
		info = pFile->GetInfoSummary();
	} else if (eItemType == UNAVAILABLE_SOURCE || eItemType == AVAILABLE_SOURCE) {
		CScopedDownloadClientRef clientRef(AcquireListedSourceClient(context.iItem));
		const CUpDownClient* client = clientRef.Get();
		if (client == NULL)
			return false;
		const CPartFile *pOwnerFile = ResolveListedParentDownloadFile(context.iItem);
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
			if (client->GetDownloadState() != DS_CONNECTING && client->GetDownloadState() != DS_DOWNLOADING) {
				info.AppendFormat(GetResString(_T("NEXT_REASK")) + _T(":%s"), (LPCTSTR)CastSecondsToHM(client->GetTimeUntilReask(client->GetRequestFile()) / SEC2MS(1)));
				if (thePrefs.IsExtControlsEnabled() && pOwnerFile != NULL)
					info.AppendFormat(_T(" (%s)"), (LPCTSTR)CastSecondsToHM(client->GetTimeUntilReask(pOwnerFile) / SEC2MS(1)));
				info += _T('\n');
			}
			info.AppendFormat(GetResString(_T("SOURCEINFO")), client->GetAskedCountDown(), client->GetAvailablePartCount());
			info += _T('\n');

			if (eItemType == AVAILABLE_SOURCE) {
				info.AppendFormat(_T("%s%s"), (LPCTSTR)GetResString(_T("CLIENTSOURCENAME")), client->GetClientFilename().IsEmpty() ? _T("-") : (LPCTSTR)client->GetClientFilename());
				if (!client->GetFileComment().IsEmpty())
					info.AppendFormat(_T("\n%s %s"), (LPCTSTR)GetResString(_T("CMT_READ")), (LPCTSTR)client->GetFileComment());
				if (client->GetFileRating())
					info.AppendFormat(_T("\n%s:%s"), (LPCTSTR)GetResString(_T("QL_RATING")), (LPCTSTR)GetRateString(client->GetFileRating()));
			} else {
				info += GetResString(_T("ASKEDFAF"));
				if (client->GetRequestFile() && !client->GetRequestFile()->GetFileName().IsEmpty())
					info.AppendFormat(_T(": %s"), (LPCTSTR)client->GetRequestFile()->GetFileName());
			}

			if (thePrefs.IsExtControlsEnabled() && !client->m_OtherRequests_list.IsEmpty()) {
				CSimpleArray<const CString*> apstrFileNames;
				for (POSITION pos = client->m_OtherRequests_list.GetHeadPosition(); pos != NULL;)
					apstrFileNames.Add(&client->m_OtherRequests_list.GetNext(pos)->GetFileName());
				Sort(apstrFileNames);
				if (eItemType == AVAILABLE_SOURCE)
					info += _T('\n');
				info.AppendFormat(_T("\n%s:"), (LPCTSTR)GetResString(_T("A4AF_FILES")));

				for (int i = 0; i < apstrFileNames.GetSize(); ++i) {
					const CString* pstrFileName = apstrFileNames[i];
					if (info.GetLength() + (i > 0 ? 2 : 0) + pstrFileName->GetLength() >= iMaxInfoLength) {
						info += _T("\n:...");
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

	if (info.IsEmpty())
		return false;

	strText = info + TOOLTIP_AUTOFORMAT_SUFFIX_CH;
	return true;
}

int CDownloadListCtrl::GetDefaultPersistentInfoTipExtraLeftPadding(const SPersistentInfoTipContext& context) const
{
	if (context.iSubItem != 14 || !theApp.ipgeolocation->ShowCountryFlag())
		return 0;

	if (context.iItem < 0 || context.iItem >= static_cast<int>(m_ListedItemsVector.size()))
		return 0;

	const CtrlItem_Struct* pCtrlItem = m_ListedItemsVector[context.iItem];
	if (pCtrlItem == NULL || (pCtrlItem->type != AVAILABLE_SOURCE && pCtrlItem->type != UNAVAILABLE_SOURCE))
		return 0;

	return 22;
}

void CDownloadListCtrl::OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult)
{
	CMuleListCtrl::OnLvnGetInfoTip(pNMHDR, pResult);
}

void CDownloadListCtrl::ShowFileDialog(UINT uInvokePage)
{
	CSimpleArray<CPartFile*> aFiles;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		int iItem = GetNextSelectedItem(pos);
		if (iItem >= 0) {
			CPartFile *pFile = ResolveListedDownloadFile(iItem);
			if (pFile != NULL)
				aFiles.Add(pFile);
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
				const ItemType eItemType = m_pDownloadListCtrl->GetListedItemType(iItem);
				if (eItemType == m_eItemType || (m_eItemType != FILE_TYPE && eItemType != FILE_TYPE && eItemType != INVALID_TYPE)) {
					CObject* pItem = m_pDownloadListCtrl->CreateListedDetailWalkerToken(iItem, m_eItemType);
					if (pItem == NULL)
						continue;

					m_pDownloadListCtrl->SetItemState(iCurSelItem, 0, LVIS_SELECTED | LVIS_FOCUSED);
					m_pDownloadListCtrl->SetItemState(iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
					m_pDownloadListCtrl->SetSelectionMark(iItem);
					m_pDownloadListCtrl->EnsureVisible(iItem, FALSE);
					return pItem;
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
				const ItemType eItemType = m_pDownloadListCtrl->GetListedItemType(iItem);
				if (eItemType == m_eItemType || (m_eItemType != FILE_TYPE && eItemType != FILE_TYPE && eItemType != INVALID_TYPE)) {
					CObject* pItem = m_pDownloadListCtrl->CreateListedDetailWalkerToken(iItem, m_eItemType);
					if (pItem == NULL)
						continue;

					m_pDownloadListCtrl->SetItemState(iCurSelItem, 0, LVIS_SELECTED | LVIS_FOCUSED);
					m_pDownloadListCtrl->SetItemState(iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
					m_pDownloadListCtrl->SetSelectionMark(iItem);
					m_pDownloadListCtrl->EnsureVisible(iItem, FALSE);
					return pItem;
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
		if (ResolveListedDownloadFile(iItem) != NULL && GetItemRect(iItem, rcLabel, LVIR_LABEL)) {
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
		const CPartFile *pPartFile = ResolveListedDownloadFile(iItem);
		if (pPartFile != NULL) {
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
	liAvailableCommands.AddTail(MP_DOWNLOADINSPECTOR);

	int iSel = GetNextItem(-1, LVIS_SELECTED);
	if (iSel >= 0) {
		const ItemType eSelectedItemType = GetListedItemType(iSel);
		if (eSelectedItemType == FILE_TYPE) {
			// get merged settings
			int iSelectedItems = 0;
			int iFilesToPause = 0;
			int iFilesToStop = 0;
			int iFilesToResume = 0;
			int iFilesToOpen = 0;
			int iFilesToPreview = 0;
			int iFilesToCancel = 0;
			int iFilesNotDone = 0;
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				int iIdxSel = GetNextSelectedItem(pos);
				const CPartFile* pFile = ResolveListedDownloadFile(iIdxSel);
				if (pFile == NULL) 
					continue;

				++iSelectedItems;

				const EPartFileStatus eStatus = pFile->GetStatus();
				iFilesToCancel += static_cast<int>(eStatus != PS_COMPLETING);
				iFilesNotDone += static_cast<int>(eStatus != PS_COMPLETE && eStatus != PS_COMPLETING);
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
				if (IsAutoRenameToMajorityNameModeEnabled() && iFilesNotDone > 0)
					liAvailableCommands.AddTail(MP_AUTORENAMETOMAJORITYNAME);
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

namespace
{
	const uint64 kAutoDeleteBytesPerMb = 1024ui64 * 1024ui64;
	const time_t kAutoDeleteSecondsPerDay = 24 * 60 * 60;
	const time_t kAutoDeleteBusyRetryDelay = MIN2S(10);
	const time_t kAutoDeleteFallbackInterval = HR2S(24);
	const time_t kAutoDeleteSettingsChangeDelay = 60;
	const time_t kAutoDeleteNoRecheck = (std::numeric_limits<time_t>::max)();
	const UINT_PTR kDownloadInspectorThreadForce = 0x1;
	const UINT_PTR kDownloadInspectorThreadAutoDeleteDue = 0x2;
	volatile LONG g_lDownloadInspectorAutoDeleteGeneration = 0;

	struct SDownloadInspectorFileData
	{
		SDownloadInspectorFileData()
			: eVerifiedFileType(FILETYPE_UNKNOWN)
			, uCompletedSize(0)
			, uFileSize(0)
			, uTransferred(0)
			, uCompressionGain(0)
			, tFileDate(0)
			, tLastChecked(0)
			, tCreated(0)
			, tLastReception(0)
			, tLastSeenComplete(0)
			, tLastAutoDeleteEvaluation(0)
			, tNextAutoDeleteCheck(0)
			, tLastSeenCompleteForAutoDelete(0)
			, bAutoDeletePendingWhileBusy(false)
			, lAutoDeleteStateGeneration(0)
			, bAutoDeleteBusy(true)
		{
		}

		SDownloadItemId idDownload;
		CString strFileName;
		CString strFilePath;
		EFileType eVerifiedFileType;
		uint64 uCompletedSize;
		uint64 uFileSize;
		uint64 uTransferred;
		uint64 uCompressionGain;
		time_t tFileDate;
		time_t tLastChecked;
		time_t tCreated;
		time_t tLastReception;
		time_t tLastSeenComplete;
		time_t tLastAutoDeleteEvaluation;
		time_t tNextAutoDeleteCheck;
		time_t tLastSeenCompleteForAutoDelete;
		bool bAutoDeletePendingWhileBusy;
		LONG lAutoDeleteStateGeneration;
		bool bAutoDeleteBusy;
		std::vector<Gap_Struct> vecFilledGaps;
	};

	struct SDownloadInspectorThreadParams
	{
		SDownloadInspectorThreadParams()
			: uThreadFlags(0)
			, lAutoDeleteGeneration(0)
		{
		}

		UINT_PTR uThreadFlags;
		LONG lAutoDeleteGeneration;
		std::vector<SDownloadInspectorFileData> vecFiles;
	};

	struct DownloadInspectorAutoDeleteEvaluation
	{
		DownloadInspectorAutoDeleteEvaluation()
			: bDateGroupMatch(false)
			, bAmountGroupMatch(false)
			, bMatched(false)
			, tNextCheck(kAutoDeleteNoRecheck)
		{
		}

		bool bDateGroupMatch;
		bool bAmountGroupMatch;
		bool bMatched;
		time_t tNextCheck;
		CString strReason;
	};

	bool HasDownloadInspectorAutoDeleteDateCriteriaEnabled()
	{
		return thePrefs.IsDownloadInspectorAutoDeleteAddedBeforeEnabled()
			|| thePrefs.IsDownloadInspectorAutoDeleteLastSeenCompleteBeforeEnabled()
			|| thePrefs.IsDownloadInspectorAutoDeleteLastReceivedBeforeEnabled();
	}

	bool HasDownloadInspectorAutoDeleteAmountCriteriaEnabled()
	{
		return thePrefs.IsDownloadInspectorAutoDeleteDownloadedLessThanPercentEnabled()
			|| thePrefs.IsDownloadInspectorAutoDeleteDownloadedLessThanMbEnabled();
	}

	bool IsDownloadInspectorAutoDeleteScanEnabled()
	{
		return thePrefs.GetDownloadInspector() > 0
			&& thePrefs.IsDownloadInspectorAutoDeleteEnabled()
			&& HasDownloadInspectorAutoDeleteDateCriteriaEnabled()
			&& HasDownloadInspectorAutoDeleteAmountCriteriaEnabled();
	}

	LONG GetDownloadInspectorAutoDeleteGeneration()
	{
		return ::InterlockedCompareExchange(&g_lDownloadInspectorAutoDeleteGeneration, 0, 0);
	}

	bool IsDownloadInspectorAutoDeleteGenerationCurrent(const LONG lGeneration)
	{
		return GetDownloadInspectorAutoDeleteGeneration() == lGeneration;
	}

	void UpdateEarliestAutoDeleteRecheck(time_t& rtCurrentEarliest, const time_t tCandidate)
	{
		if (tCandidate <= 0 || tCandidate == kAutoDeleteNoRecheck)
			return;
		if (rtCurrentEarliest == kAutoDeleteNoRecheck || tCandidate < rtCurrentEarliest)
			rtCurrentEarliest = tCandidate;
	}

	void AppendAutoDeleteReason(CString& rstrReasons, const CString& strReason)
	{
		if (strReason.IsEmpty())
			return;
		if (!rstrReasons.IsEmpty())
			rstrReasons += _T("; ");
		rstrReasons += strReason;
	}

	uint32 GetAutoDeleteAgeInDays(const time_t tReference, const time_t tNow)
	{
		if (tReference <= 0 || tNow <= tReference)
			return 0;
		return static_cast<uint32>((tNow - tReference) / kAutoDeleteSecondsPerDay);
	}

	bool EvaluateAutoDeleteDateCriterion(const time_t tReference, const int iThresholdDays, const time_t tNow, uint32& ruiAgeDays, time_t& rtNextCheck)
	{
		ruiAgeDays = 0;
		rtNextCheck = kAutoDeleteNoRecheck;
		if (tReference <= 0)
			return false;

		ruiAgeDays = GetAutoDeleteAgeInDays(tReference, tNow);
		if (ruiAgeDays >= static_cast<uint32>(iThresholdDays))
			return true;

		const time_t tThresholdTime = tReference + static_cast<time_t>(iThresholdDays) * kAutoDeleteSecondsPerDay;
		if (tThresholdTime > tReference)
			rtNextCheck = tThresholdTime;
		return false;
	}

	bool DoesAutoDeleteLessThanThresholdMatch(const uint64 uValue, const uint64 uThreshold)
	{
		return uThreshold == 0 ? uValue == 0 : uValue < uThreshold;
	}

	bool DoesAutoDeleteLessThanThresholdMatch(const double fValue, const int iThreshold)
	{
		return iThreshold == 0 ? fValue <= 0.0 : fValue < static_cast<double>(iThreshold);
	}

	bool IsDownloadInspectorRangeComplete(const SDownloadInspectorFileData& file, uint64 uStart, uint64 uEnd)
	{
		if (file.uFileSize == 0)
			return true;
		if (uStart >= file.uFileSize)
			return true;
		if (uEnd >= file.uFileSize)
			uEnd = file.uFileSize - 1;
		if (uStart > uEnd)
			return true;
		if (file.vecFilledGaps.empty())
			return file.uCompletedSize >= file.uFileSize;

		for (std::vector<Gap_Struct>::const_iterator it = file.vecFilledGaps.begin(); it != file.vecFilledGaps.end(); ++it) {
			if (it->start > uStart)
				break;
			if (it->start <= uStart && it->end > uEnd)
				return true;
		}
		return false;
	}

	EFileType GetDownloadInspectorHeaderFileType(const SDownloadInspectorFileData& file)
	{
		if (file.eVerifiedFileType != FILETYPE_UNKNOWN)
			return file.eVerifiedFileType;
		if (file.strFilePath.IsEmpty())
			return FILETYPE_UNKNOWN;

		static const BYTE s_aucFileHeader7z[] = { 0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C };
		static const BYTE s_aucFileHeaderAce[] = { 0x2A, 0x2A, 0x41, 0x43, 0x45, 0x2A, 0x2A };
		static const BYTE s_aucFileHeaderAvi[] = { 0x52, 0x49, 0x46, 0x46 };
		static const BYTE s_aucFileHeaderExe[] = { 0x4d, 0x5a };
		static const BYTE s_aucFileHeaderGif[] = { 0x47, 0x49, 0x46, 0x38 };
		static const BYTE s_aucFileHeaderIso[] = { 0x01, 0x43, 0x44, 0x30, 0x30, 0x31 };
		static const BYTE s_aucFileHeaderJpg[] = { 0xff, 0xd8, 0xff };
		static const BYTE s_aucFileHeaderMkv[] = { 0x1A, 0x45, 0xDF, 0xA3 };
		static const BYTE s_aucFileHeaderMp3[] = { 0x49, 0x44, 0x33, 0x03 };
		static const BYTE s_aucFileHeaderMp3v2[] = { 0xFE, 0xFB };
		static const BYTE s_aucFileHeaderMp4[] = { 0x66, 0x74, 0x79, 0x70 };
		static const BYTE s_aucFileHeaderMpg[] = { 0x00, 0x00, 0x01, 0xba };
		static const BYTE s_aucFileHeaderOgg[] = { 0x4F, 0x67, 0x67, 0x53 };
		static const BYTE s_aucFileHeaderPdf[] = { 0x25, 0x50, 0x44, 0x46 };
		static const BYTE s_aucFileHeaderPng[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a };
		static const BYTE s_aucFileHeaderRar[] = { 0x52, 0x61, 0x72, 0x21 };
		static const BYTE s_aucFileHeaderWm[] = { 0x30, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11, 0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c };
		static const BYTE s_aucFileHeaderZip[] = { 0x50, 0x4b, 0x03, 0x04 };
		const uint64 uHeaderCheckSize = sizeof s_aucFileHeaderWm;
		const bool bHeaderComplete = IsDownloadInspectorRangeComplete(file, 0, uHeaderCheckSize);
		const bool bIsoHeaderComplete = file.uCompletedSize > 0x8000 + uHeaderCheckSize && IsDownloadInspectorRangeComplete(file, 0x8000, 0x8000 + uHeaderCheckSize);
		if (!bHeaderComplete && !bIsoHeaderComplete)
			return FILETYPE_UNKNOWN;

		EFileType eResult = FILETYPE_UNKNOWN;
		try {
			CFile inFile;
			if (inFile.Open(file.strFilePath, CFile::modeRead | CFile::shareDenyNone)) {
				BYTE aucHeader[sizeof s_aucFileHeaderWm] = {};
				if (bHeaderComplete) {
					const UINT uRead = inFile.Read(aucHeader, sizeof aucHeader);
					if (uRead == sizeof aucHeader) {
						if (memcmp(aucHeader, s_aucFileHeaderZip, sizeof s_aucFileHeaderZip) == 0)
							eResult = ARCHIVE_ZIP;
						else if (memcmp(aucHeader, s_aucFileHeaderRar, sizeof s_aucFileHeaderRar) == 0)
							eResult = ARCHIVE_RAR;
						else if (memcmp(aucHeader + 7, s_aucFileHeaderAce, sizeof s_aucFileHeaderAce) == 0)
							eResult = ARCHIVE_ACE;
						else if (memcmp(aucHeader, s_aucFileHeader7z, sizeof s_aucFileHeader7z) == 0)
							eResult = ARCHIVE_7Z;
						else if (memcmp(aucHeader, s_aucFileHeaderWm, sizeof s_aucFileHeaderWm) == 0)
							eResult = WM;
						else if (memcmp(aucHeader, s_aucFileHeaderAvi, sizeof s_aucFileHeaderAvi) == 0 && strncmp(reinterpret_cast<const char*>(aucHeader) + 8, "AVI", 3) == 0)
							eResult = VIDEO_AVI;
						else if (memcmp(aucHeader, s_aucFileHeaderMp3, sizeof s_aucFileHeaderMp3) == 0 || memcmp(aucHeader, s_aucFileHeaderMp3v2, sizeof s_aucFileHeaderMp3v2) == 0)
							eResult = AUDIO_MPEG;
						else if (memcmp(aucHeader, s_aucFileHeaderMpg, sizeof s_aucFileHeaderMpg) == 0)
							eResult = VIDEO_MPG;
						else if (memcmp(aucHeader + 4, s_aucFileHeaderMp4, sizeof s_aucFileHeaderMp4) == 0)
							eResult = VIDEO_MP4;
						else if (memcmp(aucHeader, s_aucFileHeaderMkv, sizeof s_aucFileHeaderMkv) == 0)
							eResult = VIDEO_MKV;
						else if (memcmp(aucHeader, s_aucFileHeaderOgg, sizeof s_aucFileHeaderOgg) == 0)
							eResult = VIDEO_OGG;
						else if (memcmp(aucHeader, s_aucFileHeaderPdf, sizeof s_aucFileHeaderPdf) == 0)
							eResult = DOCUMENT_PDF;
						else if (memcmp(aucHeader, s_aucFileHeaderPng, sizeof s_aucFileHeaderPng) == 0)
							eResult = PIC_PNG;
						else if (memcmp(aucHeader, s_aucFileHeaderJpg, sizeof s_aucFileHeaderJpg) == 0 && (aucHeader[3] == 0xe1 || aucHeader[3] == 0xe0))
							eResult = PIC_JPG;
						else if (memcmp(aucHeader, s_aucFileHeaderGif, sizeof s_aucFileHeaderGif) == 0 && aucHeader[5] == 0x61 && (aucHeader[4] == 0x37 || aucHeader[4] == 0x39))
							eResult = PIC_GIF;
						else if (memcmp(aucHeader, s_aucFileHeaderExe, sizeof s_aucFileHeaderExe) == 0)
							eResult = ExtensionIs(file.strFileName, _T(".rar")) ? FILETYPE_UNKNOWN : FILETYPE_EXECUTABLE;
						else if ((aucHeader[0] & 0xFF) == 0xFF && (aucHeader[1] & 0xE0) == 0xE0)
							eResult = AUDIO_MPEG;
					}
				}
				if (eResult == FILETYPE_UNKNOWN && bIsoHeaderComplete) {
					inFile.Seek(0x8000, CFile::begin);
					const UINT uRead = inFile.Read(aucHeader, sizeof aucHeader);
					if (uRead == sizeof aucHeader && memcmp(aucHeader, s_aucFileHeaderIso, sizeof s_aucFileHeaderIso) == 0)
						eResult = IMAGE_ISO;
				}
				inFile.Close();
			}
		} catch (...) {
			ASSERT(0);
			return FILETYPE_UNKNOWN;
		}
		return eResult;
	}

	bool IsAutoDeleteBusyState(const CPartFile* file)
	{
		if (file == NULL)
			return true;
		if (file->m_bPreviewing || file->GetFileOp() != PFOP_NONE)
			return true;
		switch (file->GetStatus()) {
		case PS_WAITINGFORHASH:
		case PS_HASHING:
		case PS_COMPLETING:
			return true;
		default:
			return false;
		}
	}

	DownloadInspectorAutoDeleteEvaluation EvaluateDownloadInspectorAutoDelete(const SDownloadInspectorFileData& file, const time_t tNow)
	{
		DownloadInspectorAutoDeleteEvaluation result;
		const uint64 uCompletedSize = file.uCompletedSize;
		const uint64 uFileSize = file.uFileSize;
		const double fCompletedPercent = uFileSize > 0 ? (static_cast<double>(uCompletedSize) * 100.0 / static_cast<double>(uFileSize)) : 0.0;
		time_t tEarliestDateRecheck = kAutoDeleteNoRecheck;

		if (thePrefs.IsDownloadInspectorAutoDeleteAddedBeforeEnabled()) {
			uint32 uAgeDays = 0;
			time_t tNextCheck = kAutoDeleteNoRecheck;
			if (EvaluateAutoDeleteDateCriterion(file.tCreated, thePrefs.GetDownloadInspectorAutoDeleteAddedBeforeDays(), tNow, uAgeDays, tNextCheck)) {
				result.bDateGroupMatch = true;
				CString strCurrentReason;
				strCurrentReason.Format(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_REASON_ADDED")), uAgeDays, thePrefs.GetDownloadInspectorAutoDeleteAddedBeforeDays());
				AppendAutoDeleteReason(result.strReason, strCurrentReason);
			} else
				UpdateEarliestAutoDeleteRecheck(tEarliestDateRecheck, tNextCheck);
		}

		if (thePrefs.IsDownloadInspectorAutoDeleteLastSeenCompleteBeforeEnabled()) {
			uint32 uAgeDays = 0;
			time_t tNextCheck = kAutoDeleteNoRecheck;
			if (EvaluateAutoDeleteDateCriterion(file.tLastSeenComplete, thePrefs.GetDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays(), tNow, uAgeDays, tNextCheck)) {
				result.bDateGroupMatch = true;
				CString strCurrentReason;
				strCurrentReason.Format(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_REASON_LAST_SEEN_COMPLETE")), uAgeDays, thePrefs.GetDownloadInspectorAutoDeleteLastSeenCompleteBeforeDays());
				AppendAutoDeleteReason(result.strReason, strCurrentReason);
			} else
				UpdateEarliestAutoDeleteRecheck(tEarliestDateRecheck, tNextCheck);
		}

		if (thePrefs.IsDownloadInspectorAutoDeleteLastReceivedBeforeEnabled()) {
			uint32 uAgeDays = 0;
			time_t tNextCheck = kAutoDeleteNoRecheck;
			if (EvaluateAutoDeleteDateCriterion(file.tLastReception, thePrefs.GetDownloadInspectorAutoDeleteLastReceivedBeforeDays(), tNow, uAgeDays, tNextCheck)) {
				result.bDateGroupMatch = true;
				CString strCurrentReason;
				strCurrentReason.Format(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_REASON_LAST_RECEIVED")), uAgeDays, thePrefs.GetDownloadInspectorAutoDeleteLastReceivedBeforeDays());
				AppendAutoDeleteReason(result.strReason, strCurrentReason);
			} else
				UpdateEarliestAutoDeleteRecheck(tEarliestDateRecheck, tNextCheck);
		}

		if (thePrefs.IsDownloadInspectorAutoDeleteDownloadedLessThanPercentEnabled()
			&& DoesAutoDeleteLessThanThresholdMatch(fCompletedPercent, thePrefs.GetDownloadInspectorAutoDeleteDownloadedLessThanPercent())) {
			result.bAmountGroupMatch = true;
			CString strCurrentReason;
			strCurrentReason.Format(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_REASON_PERCENT")), fCompletedPercent, thePrefs.GetDownloadInspectorAutoDeleteDownloadedLessThanPercent());
			AppendAutoDeleteReason(result.strReason, strCurrentReason);
		}

		if (thePrefs.IsDownloadInspectorAutoDeleteDownloadedLessThanMbEnabled()
			&& DoesAutoDeleteLessThanThresholdMatch(uCompletedSize, static_cast<uint64>(thePrefs.GetDownloadInspectorAutoDeleteDownloadedLessThanMb()) * kAutoDeleteBytesPerMb)) {
			result.bAmountGroupMatch = true;
			CString strCurrentReason;
			strCurrentReason.Format(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_REASON_MB")), (LPCTSTR)CastItoXBytes(uCompletedSize), thePrefs.GetDownloadInspectorAutoDeleteDownloadedLessThanMb());
			AppendAutoDeleteReason(result.strReason, strCurrentReason);
		}

		result.bMatched = result.bDateGroupMatch && result.bAmountGroupMatch;
		if (result.bMatched || !result.bAmountGroupMatch)
			result.tNextCheck = kAutoDeleteNoRecheck;
		else
			result.tNextCheck = tEarliestDateRecheck;

		return result;
	}

	bool AppendAutoDeleteEd2kLinkToBackupFile(const CString& strEd2kLink)
	{
		if (strEd2kLink.IsEmpty())
			return false;
		try {
			CSafeBufferedFile file;
			const CString strFilePath = thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + _T("download_inspector.txt");
			if (!file.Open(strFilePath, CFile::modeCreate | CFile::modeNoTruncate | CFile::modeWrite | CFile::shareDenyWrite | CFile::typeBinary))
				return false;
			file.SeekToEnd();
			const CUnicodeToUTF8 utf8Line(strEd2kLink + _T("\r\n"));
			file.Write((LPCSTR)utf8Line, utf8Line.GetLength());
			CommitAndClose(file);
			return true;
		} catch (...) {
			TRACE(_T("Download inspector: Failed to append eD2k backup link.\n"));
			return false;
		}
	}
}

void CDownloadListCtrl::DownloadInspector(const bool bForce)
{
	const bool bHasLegacyInspectorWork = thePrefs.IsDownloadInspectorInvalidExt() || thePrefs.GetDownloadInspectorFake() || thePrefs.GetDownloadInspectorDRM();
	const bool bHasAutoDeleteWork = IsDownloadInspectorAutoDeleteScanEnabled();
	if (!bHasLegacyInspectorWork && !bHasAutoDeleteWork)
		return;

	if (!bForce && thePrefs.GetDownloadInspector() <= 0)
		return;

	const DWORD dwNow = ::GetTickCount();
	const bool bLegacyInspectorDue = bForce || (bHasLegacyInspectorWork && (m_dwLastDetection == 0 || (dwNow - m_dwLastDetection >= static_cast<DWORD>(thePrefs.GetDownloadInspectorCheckPeriod() * 60000/*minutes*/))));
	const time_t tNow = time(NULL);
	const bool bAutoDeleteDue = bForce || (bHasAutoDeleteWork && m_tNextAutoDeleteScan != 0 && tNow >= m_tNextAutoDeleteScan);
	if (theApp.IsClosing() || (!bLegacyInspectorDue && !bAutoDeleteDue))
		return;

	if (pDownloadInspectorThread != NULL) {
		DWORD lpExitCode;
		GetExitCodeThread(pDownloadInspectorThread->m_hThread, &lpExitCode);
		if (lpExitCode == STILL_ACTIVE) {
			AddLogLine(false, GetResString(_T("DOWNLOAD_INSPECTOR_IN_PROGRESS")));
			return;
		}
	}
	if (HasDownloadInspectorApplyWork()) {
		AddLogLine(false, GetResString(_T("DOWNLOAD_INSPECTOR_IN_PROGRESS")));
		return;
	}

	UINT_PTR uThreadFlags = 0;
	if (bForce)
		uThreadFlags |= kDownloadInspectorThreadForce;
	if (bAutoDeleteDue)
		uThreadFlags |= kDownloadInspectorThreadAutoDeleteDue;

	SDownloadInspectorThreadParams* pThreadParams = new SDownloadInspectorThreadParams;
	pThreadParams->uThreadFlags = uThreadFlags;
	pThreadParams->lAutoDeleteGeneration = GetDownloadInspectorAutoDeleteGeneration();
	const bool bNeedFilledGapData = bLegacyInspectorDue && (thePrefs.IsDownloadInspectorInvalidExt() || thePrefs.GetDownloadInspectorFake());
	for (ListItems::const_iterator it = m_ListItems.begin(); it != m_ListItems.end(); ++it) {
		const CtrlItem_Struct* cur_item = it->second;
		if (cur_item == NULL || cur_item->type != FILE_TYPE)
			continue;

		if (cur_item->value == NULL)
			continue;
		CPartFile* file = static_cast<CPartFile*>(cur_item->value);
		if (file == NULL || !file->IsPartFile())
			continue;

		SDownloadInspectorFileData fileData;
		if (theApp.downloadqueue == NULL || !theApp.downloadqueue->GetDownloadItemId(file, fileData.idDownload))
			continue;
		fileData.strFileName = file->GetFileName();
		fileData.strFilePath = file->GetFilePath();
		fileData.eVerifiedFileType = file->GetVerifiedFileType();
		fileData.uCompletedSize = file->GetCompletedSize();
		fileData.uFileSize = file->GetFileSize();
		fileData.uTransferred = file->GetTransferred();
		fileData.uCompressionGain = file->GetCompressionGain();
		fileData.tFileDate = file->GetFileDate();
		fileData.tLastChecked = file->m_tLastChecked;
		fileData.tCreated = file->GetCrFileDate();
		fileData.tLastReception = file->GetLastReceptionDate();
		fileData.tLastSeenComplete = file->lastseencomplete != 0 ? static_cast<time_t>(file->lastseencomplete.GetTime()) : 0;
		fileData.tLastAutoDeleteEvaluation = file->m_tLastAutoDeleteEvaluation;
		fileData.tNextAutoDeleteCheck = file->m_tNextAutoDeleteCheck;
		fileData.tLastSeenCompleteForAutoDelete = file->m_tLastSeenCompleteForAutoDelete;
		fileData.bAutoDeletePendingWhileBusy = file->m_bAutoDeletePendingWhileBusy;
		fileData.lAutoDeleteStateGeneration = file->m_lAutoDeleteStateGeneration;
		fileData.bAutoDeleteBusy = IsAutoDeleteBusyState(file);
		if (bNeedFilledGapData && fileData.uCompletedSize > 0) {
			CArray<Gap_Struct> filled;
			file->GetFilledArray(filled);
			fileData.vecFilledGaps.reserve(static_cast<size_t>(filled.GetCount()));
			for (INT_PTR i = 0; i < filled.GetCount(); ++i)
				fileData.vecFilledGaps.push_back(filled[i]);
		}
		pThreadParams->vecFiles.push_back(fileData);
	}

	pDownloadInspectorThread = AfxBeginThread(DownloadInspectorProc, pThreadParams, THREAD_PRIORITY_IDLE);
	if (pDownloadInspectorThread == NULL) {
		delete pThreadParams;
		return;
	}
	AddLogLine(false, GetResString(_T("DOWNLOAD_INSPECTOR_STARTED")));
	if (bLegacyInspectorDue)
		m_dwLastDetection = dwNow;
}

void CDownloadListCtrl::ResetDownloadInspectorAutoDeleteState()
{
	// Invalidate the cached auto-delete schedule. The worker thread will
	// lazily rebuild per-file state for the next active generation.
	::InterlockedIncrement(&g_lDownloadInspectorAutoDeleteGeneration);
	if (!IsDownloadInspectorAutoDeleteScanEnabled()) {
		m_tNextAutoDeleteScan = 0;
		return;
	}

	m_tNextAutoDeleteScan = time(NULL) + kAutoDeleteSettingsChangeDelay;
}

UINT AFX_CDECL CDownloadListCtrl::DownloadInspectorProc(LPVOID pParam)
{
	DbgSetThreadName("DownloadInspector");
	std::unique_ptr<SDownloadInspectorThreadParams> pThreadParams(reinterpret_cast<SDownloadInspectorThreadParams*>(pParam));
	if (pThreadParams.get() == NULL)
		return 0;
	const UINT_PTR uThreadFlags = pThreadParams->uThreadFlags;
	const bool bForce = (uThreadFlags & kDownloadInspectorThreadForce) != 0;
	const bool bAutoDeleteGloballyDue = bForce || (uThreadFlags & kDownloadInspectorThreadAutoDeleteDue) != 0;
	const LONG lAutoDeleteGeneration = pThreadParams->lAutoDeleteGeneration;
	bool bFileFound = false;
	time_t tEarliestAutoDeleteScan = kAutoDeleteNoRecheck;
	std::vector<PartFileOperationMsgParams> renamelist;
	std::vector<PartFileOperationMsgParams> removelist;
	CDownloadListCtrl* pDownloadList = theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL ? theApp.emuledlg->transferwnd->GetDownloadList() : NULL;
	SAutoDeleteStateApplyParams autoDeleteApply;
	autoDeleteApply.lAutoDeleteGeneration = lAutoDeleteGeneration;

	auto UpdateAutoDeleteScheduleCandidate = [&](const time_t tNextAutoDeleteCheck) {
		if (!bAutoDeleteGloballyDue || !IsDownloadInspectorAutoDeleteGenerationCurrent(lAutoDeleteGeneration))
			return;
		UpdateEarliestAutoDeleteRecheck(tEarliestAutoDeleteScan, tNextAutoDeleteCheck);
	};

	auto QueueLegacyLastCheckedApply = [&](const SDownloadInspectorFileData& file, const time_t tLastChecked) {
		SAutoDeleteStateApplyItem item;
		item.idDownload = file.idDownload;
		item.bUpdateLegacyLastChecked = true;
		item.tLastChecked = tLastChecked;
		autoDeleteApply.vecItems.push_back(item);
	};

	auto QueueAutoDeleteStateApply = [&](const SDownloadInspectorFileData& file, const time_t tLastAutoDeleteEvaluation, const time_t tLastSeenCompleteForAutoDelete, const bool bAutoDeletePendingWhileBusy, const time_t tNextAutoDeleteCheck) {
		if (!bAutoDeleteGloballyDue)
			return;
		SAutoDeleteStateApplyItem item;
		item.idDownload = file.idDownload;
		item.bUpdateAutoDeleteState = true;
		item.tLastAutoDeleteEvaluation = tLastAutoDeleteEvaluation;
		item.tLastSeenCompleteForAutoDelete = tLastSeenCompleteForAutoDelete;
		item.bAutoDeletePendingWhileBusy = bAutoDeletePendingWhileBusy;
		item.tNextAutoDeleteCheck = tNextAutoDeleteCheck;
		autoDeleteApply.vecItems.push_back(item);
		UpdateAutoDeleteScheduleCandidate(tNextAutoDeleteCheck);
	};

	auto InitOperationParams = [](PartFileOperationMsgParams& params, const SDownloadInspectorFileData& file) {
		params.idDownload = file.idDownload;
		params.strExpectedFileName = file.strFileName;
		params.strExpectedFilePath = file.strFilePath;
	};

	for (std::vector<SDownloadInspectorFileData>::const_iterator it = pThreadParams->vecFiles.begin(); it != pThreadParams->vecFiles.end(); ++it) {
		const SDownloadInspectorFileData& file = *it;
		try {
			const bool bAutoDeleteStateGenerationMismatch = file.lAutoDeleteStateGeneration != lAutoDeleteGeneration;
			time_t tLastAutoDeleteEvaluation = bAutoDeleteStateGenerationMismatch ? 0 : file.tLastAutoDeleteEvaluation;
			time_t tNextAutoDeleteCheck = bAutoDeleteStateGenerationMismatch ? 0 : file.tNextAutoDeleteCheck;
			time_t tLastSeenCompleteForAutoDelete = bAutoDeleteStateGenerationMismatch ? 0 : file.tLastSeenCompleteForAutoDelete;
			bool bAutoDeletePendingWhileBusy = bAutoDeleteStateGenerationMismatch ? false : file.bAutoDeletePendingWhileBusy;

			const time_t tNow = time(NULL);
			const bool bFileChangedSinceLastCheck = file.tFileDate >= file.tLastChecked;
			const bool bContentCheckNeeded = bForce || bFileChangedSinceLastCheck;
			const bool bLegacyContentCheckNeeded = bContentCheckNeeded && (thePrefs.IsDownloadInspectorInvalidExt() || thePrefs.GetDownloadInspectorFake() || thePrefs.GetDownloadInspectorDRM());
			const bool bAutoDeleteScanEnabled = bAutoDeleteGloballyDue && IsDownloadInspectorAutoDeleteGenerationCurrent(lAutoDeleteGeneration) && IsDownloadInspectorAutoDeleteScanEnabled();
			const time_t tLastSeenComplete = file.tLastSeenComplete;
			const bool bAutoDeleteNeverEvaluated = tLastAutoDeleteEvaluation == 0;
			const bool bAutoDeleteFileChangedSinceLastEvaluation = file.tFileDate >= tLastAutoDeleteEvaluation;
			// lastseencomplete can change without touching the part file mtime.
			const bool bAutoDeleteMetadataChangedSinceLastEvaluation = thePrefs.IsDownloadInspectorAutoDeleteLastSeenCompleteBeforeEnabled()
				&& tLastSeenComplete != tLastSeenCompleteForAutoDelete;
			const bool bAutoDeleteRetryAfterBusy = bAutoDeletePendingWhileBusy && !file.bAutoDeleteBusy;
			const bool bAutoDeleteScheduledCheckDue = tNextAutoDeleteCheck != kAutoDeleteNoRecheck && (tNextAutoDeleteCheck == 0 || tNextAutoDeleteCheck <= tNow);
			const bool bAutoDeleteCheckDue = bAutoDeleteScanEnabled && (bForce || bAutoDeleteNeverEvaluated || bAutoDeleteRetryAfterBusy || (!bAutoDeletePendingWhileBusy && (bAutoDeleteFileChangedSinceLastEvaluation || bAutoDeleteMetadataChangedSinceLastEvaluation)) || bAutoDeleteScheduledCheckDue);
			if (!bLegacyContentCheckNeeded && !bAutoDeleteCheckDue) {
				if (bAutoDeleteStateGenerationMismatch)
					QueueAutoDeleteStateApply(file, tLastAutoDeleteEvaluation, tLastSeenCompleteForAutoDelete, bAutoDeletePendingWhileBusy, tNextAutoDeleteCheck);
				else
					UpdateAutoDeleteScheduleCandidate(tNextAutoDeleteCheck);
				continue;
			}

			CString cLogMsg;

			if (bLegacyContentCheckNeeded && thePrefs.IsDownloadInspectorInvalidExt() && file.uCompletedSize) {
				EFileType bycontent = GetDownloadInspectorHeaderFileType(file);
				if (bycontent != FILETYPE_UNKNOWN) {
					CString strOldFileName, strNewFileName, strNewExtension;
					if (GetFileNameWithDetectedExtension(file.strFileName, bycontent, strNewFileName, &strNewExtension)) {
						strOldFileName = file.strFileName;

						if (thePrefs.GetDownloadInspector() == 2) {
							cLogMsg.Format(GetResString(_T("INVALID_FILE_EXTENSION_REPLACED_MESSAGE")), (LPCTSTR)EscPercent(strOldFileName), (LPCTSTR)EscPercent(strNewFileName));
							PartFileOperationMsgParams params;
							InitOperationParams(params, file);
							params.strNewFileName = strNewFileName;
							params.cLogMsg = cLogMsg;
							renamelist.push_back(params);
						} else {
							cLogMsg.Format(GetResString(_T("INVALID_FILE_EXTENSION_REPLACED_MESSAGE2")), (LPCTSTR)EscPercent(file.strFileName), (LPCTSTR)strNewExtension);
							theApp.QueueLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
						}
					}
				}
			}

			if (bLegacyContentCheckNeeded && thePrefs.GetDownloadInspectorDRM() && file.uCompletedSize) {
				const bool bDRMFound = GetDRM(file.strFilePath);
				if (bDRMFound) {
					bFileFound = true;
					if (thePrefs.GetDownloadInspector() == 2) {
						cLogMsg.Format(GetResString(_T("DOWNLOAD_INSPECTOR_DELETE_MESSAGE3")), (LPCTSTR)EscPercent(file.strFileName));
						PartFileOperationMsgParams params;
						InitOperationParams(params, file);
						params.cLogMsg = cLogMsg;
						removelist.push_back(params);
					} else {
						cLogMsg.Format(GetResString(_T("DOWNLOAD_INSPECTOR_LOG_MESSAGE3")), (LPCTSTR)EscPercent(file.strFileName));
						theApp.QueueLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
					}

					QueueLegacyLastCheckedApply(file, tNow);
					tNextAutoDeleteCheck = kAutoDeleteNoRecheck;
					QueueAutoDeleteStateApply(file, tLastAutoDeleteEvaluation, tLastSeenCompleteForAutoDelete, bAutoDeletePendingWhileBusy, tNextAutoDeleteCheck);
					if (theApp.IsClosing())
						return 1;
					continue;
				}
			}

			if (bLegacyContentCheckNeeded && thePrefs.GetDownloadInspectorFake() && file.uCompletedSize / 1024 >= thePrefs.GetDownloadInspectorCompletedThreshold()) {
				const uint64 uTransferred = file.uTransferred;
				const int iCompressionPercentage = uTransferred > 0 ? static_cast<int>(file.uCompressionGain * 100.0 / uTransferred) : 0;
				if (iCompressionPercentage >= thePrefs.GetDownloadInspectorCompressionThreshold()) {
					bool bBypassZeroPercentage = false;
					bool bAllZero = true;
					long double ldTotalCount = 0;
					long double ldZeroCount = 0;

					if (!thePrefs.GetDownloadInspectorBypassZeroPercentage() || iCompressionPercentage < thePrefs.GetDownloadInspectorCompressionThresholdToBypassZero()) {
						if (!(thePrefs.GetDownloadInspectorZeroPercentageThreshold() == 100 && GetDownloadInspectorHeaderFileType(file) != FILETYPE_UNKNOWN)) {
							if (!file.vecFilledGaps.empty()) {
								HANDLE hPartFile = ::CreateFile(file.strFilePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
								if (hPartFile != INVALID_HANDLE_VALUE) {
									for (std::vector<Gap_Struct>::const_iterator itFill = file.vecFilledGaps.begin(); itFill != file.vecFilledGaps.end(); ++itFill) {
										for (uint64 uStart = itFill->start; uStart < itFill->end;) {
											ldTotalCount++;
											BYTE buffer[16384];
											const DWORD lenData = static_cast<DWORD>(min(itFill->end - uStart, static_cast<uint64>(sizeof buffer)));
											DWORD dwRead = 0;
											LARGE_INTEGER liDistance;
											liDistance.QuadPart = uStart;
											if (!::SetFilePointerEx(hPartFile, liDistance, NULL, FILE_BEGIN) || !::ReadFile(hPartFile, buffer, lenData, &dwRead, NULL)) {
												bAllZero = false;
												break;
											}
											if (dwRead == 0)
												break;
											const bool bZeroBuffer = std::all_of(buffer, buffer + dwRead, [](BYTE elem) { return elem == 0; });
											if (!bZeroBuffer) {
												bAllZero = false;
												if (thePrefs.GetDownloadInspectorZeroPercentageThreshold() == 100)
													break;
											} else
												ldZeroCount++;
											ASSERT(uStart + dwRead <= itFill->end);
											uStart += dwRead;
										}
										if (!bAllZero && thePrefs.GetDownloadInspectorZeroPercentageThreshold() == 100)
											break;
									}
									::CloseHandle(hPartFile);
								} else
									bAllZero = false;
							} else
								bAllZero = false;
						} else
							bAllZero = false;
					} else
						bBypassZeroPercentage = true;

					const long double ldZeroPercentage = ldTotalCount > 0 ? (ldZeroCount / ldTotalCount) * 100.0 : 0.0;
					if (bBypassZeroPercentage || bAllZero || ldZeroPercentage >= thePrefs.GetDownloadInspectorZeroPercentageThreshold()) {
						bFileFound = true;
						if (thePrefs.GetDownloadInspector() == 2) {
							if (!bBypassZeroPercentage)
								cLogMsg.Format(GetResString(_T("DOWNLOAD_INSPECTOR_DELETE_MESSAGE")), iCompressionPercentage, file.uCompletedSize / 1024, ldZeroPercentage, (LPCTSTR)EscPercent(file.strFileName));
							else
								cLogMsg.Format(GetResString(_T("DOWNLOAD_INSPECTOR_DELETE_MESSAGE2")), iCompressionPercentage, file.uCompletedSize / 1024, (LPCTSTR)EscPercent(file.strFileName));
							PartFileOperationMsgParams params;
							InitOperationParams(params, file);
							params.cLogMsg = cLogMsg;
							removelist.push_back(params);
						} else {
							if (!bBypassZeroPercentage)
								cLogMsg.Format(GetResString(_T("DOWNLOAD_INSPECTOR_LOG_MESSAGE")), iCompressionPercentage, file.uCompletedSize / 1024, ldZeroPercentage, (LPCTSTR)EscPercent(file.strFileName));
							else
								cLogMsg.Format(GetResString(_T("DOWNLOAD_INSPECTOR_LOG_MESSAGE2")), iCompressionPercentage, file.uCompletedSize / 1024, (LPCTSTR)EscPercent(file.strFileName));
							theApp.QueueLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
						}

						QueueLegacyLastCheckedApply(file, tNow);
						tNextAutoDeleteCheck = kAutoDeleteNoRecheck;
						QueueAutoDeleteStateApply(file, tLastAutoDeleteEvaluation, tLastSeenCompleteForAutoDelete, bAutoDeletePendingWhileBusy, tNextAutoDeleteCheck);
						if (theApp.IsClosing())
							return 1;
						continue;
					}
				}
			}

			if (bAutoDeleteCheckDue) {
				if (IsDownloadInspectorAutoDeleteGenerationCurrent(lAutoDeleteGeneration)) {
					const DownloadInspectorAutoDeleteEvaluation evaluation = EvaluateDownloadInspectorAutoDelete(file, tNow);
					if (IsDownloadInspectorAutoDeleteGenerationCurrent(lAutoDeleteGeneration)) {
						tLastAutoDeleteEvaluation = tNow;
						tLastSeenCompleteForAutoDelete = tLastSeenComplete;
						if (evaluation.bMatched) {
							bFileFound = true;
							if (thePrefs.GetDownloadInspector() == 2) {
								if (file.bAutoDeleteBusy) {
									if (!bAutoDeletePendingWhileBusy) {
										cLogMsg.Format(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_SKIP_BUSY_MESSAGE")), (LPCTSTR)EscPercent(file.strFileName), (LPCTSTR)evaluation.strReason);
										theApp.QueueLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
									}
									bAutoDeletePendingWhileBusy = true;
									tNextAutoDeleteCheck = tNow + kAutoDeleteBusyRetryDelay;
								} else {
									cLogMsg.Format(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_DELETE_MESSAGE")), (LPCTSTR)EscPercent(file.strFileName), (LPCTSTR)evaluation.strReason);
									PartFileOperationMsgParams params;
									InitOperationParams(params, file);
									params.cLogMsg = cLogMsg;
									params.bAppendAutoDeleteEd2kLink = thePrefs.IsDownloadInspectorAutoDeleteBackupEd2kLinksEnabled();
									params.bAddToCanceledMet = !thePrefs.IsDownloadInspectorAutoDeleteDontMarkAsCanceledEnabled();
									params.bAutoDeleteOperation = true;
									params.lAutoDeleteGeneration = lAutoDeleteGeneration;
									params.strAutoDeleteReason = evaluation.strReason;
									removelist.push_back(params);
									bAutoDeletePendingWhileBusy = false;
									tNextAutoDeleteCheck = kAutoDeleteNoRecheck;
								}
							} else {
								cLogMsg.Format(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_LOG_MESSAGE")), (LPCTSTR)EscPercent(file.strFileName), (LPCTSTR)evaluation.strReason);
								theApp.QueueLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
								bAutoDeletePendingWhileBusy = false;
								tNextAutoDeleteCheck = kAutoDeleteNoRecheck;
							}
						} else {
							bAutoDeletePendingWhileBusy = false;
							tNextAutoDeleteCheck = evaluation.tNextCheck;
						}
						QueueAutoDeleteStateApply(file, tLastAutoDeleteEvaluation, tLastSeenCompleteForAutoDelete, bAutoDeletePendingWhileBusy, tNextAutoDeleteCheck);
					}
				}
			}

			if (bLegacyContentCheckNeeded)
				QueueLegacyLastCheckedApply(file, tNow);
			if (!bAutoDeleteCheckDue)
				UpdateAutoDeleteScheduleCandidate(tNextAutoDeleteCheck);
			if (theApp.IsClosing())
				return 1;
		} catch (const std::exception& e) {
			theApp.QueueDebugLogLine(true, _T("CDownloadListCtrl::DownloadInspectorProc: Exception caught: %hs"), e.what());
			continue;
		} catch (...) {
			theApp.QueueDebugLogLine(true, _T("CDownloadListCtrl::DownloadInspectorProc: Unknown exception caught"));
			continue;
		}
	}

	if (bAutoDeleteGloballyDue && IsDownloadInspectorAutoDeleteGenerationCurrent(lAutoDeleteGeneration)) {
		autoDeleteApply.bUpdateNextAutoDeleteScan = true;
		if (IsDownloadInspectorAutoDeleteScanEnabled())
			autoDeleteApply.tNextAutoDeleteScan = tEarliestAutoDeleteScan != kAutoDeleteNoRecheck ? tEarliestAutoDeleteScan : time(NULL) + kAutoDeleteFallbackInterval;
		else
			autoDeleteApply.tNextAutoDeleteScan = 0;
	}

	const bool bHasAutoDeleteApply = !autoDeleteApply.vecItems.empty() || autoDeleteApply.bUpdateNextAutoDeleteScan;
	const bool bHasOwnerThreadApply = bHasAutoDeleteApply || !renamelist.empty() || !removelist.empty();
	if (pDownloadList != NULL && !theApp.IsClosing() && bHasOwnerThreadApply) {
		SDownloadInspectorApplyParams* pApplyParams = new SDownloadInspectorApplyParams;
		pApplyParams->bHasAutoDeleteApply = bHasAutoDeleteApply;
		pApplyParams->autoDeleteApply = autoDeleteApply;
		pApplyParams->vecRenameItems.swap(renamelist);
		pApplyParams->vecRemoveItems.swap(removelist);
		pApplyParams->bFileFound = bFileFound;
		::InterlockedIncrement(&pDownloadList->m_lDownloadInspectorApplyPending);
		if (!pDownloadList->PostMessage(WM_DOWNLOADLISTCTRL_DOWNLOAD_INSPECTOR_APPLY, 0, reinterpret_cast<LPARAM>(pApplyParams))) {
			::InterlockedDecrement(&pDownloadList->m_lDownloadInspectorApplyPending);
			delete pApplyParams;
			theApp.QueueDebugLogLine(true, _T("CDownloadListCtrl::DownloadInspectorProc: Failed to post apply payload"));
		}
	} else {
		if (bFileFound)
			theApp.QueueLogLine(true, GetResString(_T("DOWNLOAD_INSPECTOR_COMPLETED_FOUND")));
		else
			theApp.QueueLogLine(true, GetResString(_T("DOWNLOAD_INSPECTOR_COMPLETED_NOT_FOUND")));
	}

	return 0;
}

void CDownloadListCtrl::ClearDownloadInspectorApplyItems()
{
	if (::IsWindow(m_hWnd))
		KillTimer(TimerDownloadInspectorApply);
	m_bDownloadInspectorApplyPending = false;
	while (!m_downloadInspectorApplyItems.IsEmpty()) {
		delete m_downloadInspectorApplyItems.RemoveHead();
		::InterlockedDecrement(&m_lDownloadInspectorApplyPending);
	}
}

bool CDownloadListCtrl::HasDownloadInspectorApplyWork() const
{
	return ::InterlockedCompareExchange(const_cast<volatile LONG*>(&m_lDownloadInspectorApplyPending), 0, 0) > 0
		|| !m_downloadInspectorApplyItems.IsEmpty()
		|| m_bDownloadInspectorApplyPending;
}

bool CDownloadListCtrl::PostDownloadInspectorApplyMessage()
{
	if (m_bDownloadInspectorApplyPending || theApp.IsClosing() || !::IsWindow(m_hWnd))
		return false;

	m_bDownloadInspectorApplyPending = SetTimer(TimerDownloadInspectorApply, 1, NULL) != 0;
	if (!m_bDownloadInspectorApplyPending)
		m_bDownloadInspectorApplyPending = PostMessage(WM_DOWNLOADLISTCTRL_DOWNLOAD_INSPECTOR_APPLY, 0, 0) != FALSE;
	return m_bDownloadInspectorApplyPending;
}

void CDownloadListCtrl::ApplyAutoDeleteStateItem(const SAutoDeleteStateApplyParams &params, const SAutoDeleteStateApplyItem &item)
{
	CPartFile* file = theApp.downloadqueue != NULL ? theApp.downloadqueue->GetFileByItemId(item.idDownload) : NULL;
	if (file == NULL || !file->IsPartFile())
		return;

	const bool bAutoDeleteGenerationCurrent = IsDownloadInspectorAutoDeleteGenerationCurrent(params.lAutoDeleteGeneration);
	if (item.bUpdateLegacyLastChecked)
		file->m_tLastChecked = item.tLastChecked;
	if (item.bUpdateAutoDeleteState && bAutoDeleteGenerationCurrent) {
		file->m_tLastAutoDeleteEvaluation = item.tLastAutoDeleteEvaluation;
		file->m_tLastSeenCompleteForAutoDelete = item.tLastSeenCompleteForAutoDelete;
		file->m_bAutoDeletePendingWhileBusy = item.bAutoDeletePendingWhileBusy;
		file->m_tNextAutoDeleteCheck = item.tNextAutoDeleteCheck;
		file->m_lAutoDeleteStateGeneration = params.lAutoDeleteGeneration;
	}
}

void CDownloadListCtrl::ApplyAutoDeleteSchedule(const SAutoDeleteStateApplyParams &params)
{
	if (!params.bUpdateNextAutoDeleteScan || !IsDownloadInspectorAutoDeleteGenerationCurrent(params.lAutoDeleteGeneration))
		return;

	if (IsDownloadInspectorAutoDeleteScanEnabled())
		m_tNextAutoDeleteScan = params.tNextAutoDeleteScan;
	else
		m_tNextAutoDeleteScan = 0;
}

void CDownloadListCtrl::ApplyInvalidExtensionFound(PartFileOperationMsgParams &params)
{
	if (theApp.downloadqueue == NULL)
		return;
	CPartFile* file = theApp.downloadqueue->GetFileByItemId(params.idDownload);
	if (file == NULL || !file->IsPartFile())
		return;
	if (!params.strExpectedFilePath.IsEmpty() && file->GetFilePath().CompareNoCase(params.strExpectedFilePath) != 0)
		return;
	if (!params.strExpectedFileName.IsEmpty() && file->GetFileName().CompareNoCase(params.strExpectedFileName) != 0)
		return;
	file->SetFileName((LPCTSTR)params.strNewFileName, true);
	file->UpdateDisplayedInfo();
	if (file->SavePartFile())
		AddLogLine(true, (LPCTSTR)EscPercent(params.cLogMsg));
}

void CDownloadListCtrl::ApplyEmptyFakeFileFound(PartFileOperationMsgParams &params)
{
	if (theApp.downloadqueue == NULL)
		return;

	CPartFile* file = theApp.downloadqueue->GetFileByItemId(params.idDownload);
	if (file == NULL || !file->IsPartFile())
		return;
	if (!params.strExpectedFilePath.IsEmpty() && file->GetFilePath().CompareNoCase(params.strExpectedFilePath) != 0)
		return;

	if (params.bAutoDeleteOperation) {
		if (!IsDownloadInspectorAutoDeleteGenerationCurrent(params.lAutoDeleteGeneration)
			|| !thePrefs.IsDownloadInspectorAutoDeleteEnabled()
			|| thePrefs.GetDownloadInspector() != 2)
			return;

		if (IsAutoDeleteBusyState(file)) {
			if (!params.strAutoDeleteReason.IsEmpty()) {
				CString cLogMsg;
				cLogMsg.Format(GetResString(_T("DOWNLOAD_INSPECTOR_AUTO_DELETE_SKIP_BUSY_MESSAGE")), (LPCTSTR)EscPercent(file->GetFileName()), (LPCTSTR)params.strAutoDeleteReason);
				AddLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
			}
			const time_t tNextAutoDeleteCheck = time(NULL) + kAutoDeleteBusyRetryDelay;
			file->m_bAutoDeletePendingWhileBusy = true;
			file->m_tNextAutoDeleteCheck = tNextAutoDeleteCheck;
			file->m_lAutoDeleteStateGeneration = params.lAutoDeleteGeneration;
			if (m_tNextAutoDeleteScan == 0 || tNextAutoDeleteCheck < m_tNextAutoDeleteScan)
				m_tNextAutoDeleteScan = tNextAutoDeleteCheck;
			return;
		}
	}

	if (params.bAppendAutoDeleteEd2kLink && params.strAutoDeleteEd2kLink.IsEmpty())
		params.strAutoDeleteEd2kLink = file->GetED2kLink();
	SetRedraw(false);
	HideSources(file);
	switch (file->GetStatus()) {
	case PS_WAITINGFORHASH:
	case PS_HASHING:
	case PS_COMPLETING:
		break;
	case PS_COMPLETE: {
		const CString strFilePath = file->GetFilePath();
		const bool bDeleteSucceeded = ShellDeleteFile(strFilePath);
		if (!bDeleteSucceeded) {
			CString strError;
			strError.Format(GetResString(_T("ERR_DELFILE")) + _T("\r\n\r\n%s"), (LPCTSTR)strFilePath, (LPCTSTR)GetErrorMessage(::GetLastError()));
			CDarkMode::MessageBox(strError);
			break;
		}
		theApp.sharedfiles->RemoveFile(file, true);
		RemoveFile(file);
		if (params.bAppendAutoDeleteEd2kLink)
			AppendAutoDeleteEd2kLinkToBackupFile(params.strAutoDeleteEd2kLink);
		AddLogLine(true, (LPCTSTR)EscPercent(params.cLogMsg));
		break;
	}
	default: {
		const UINT uCategory = file->GetCategory();
		if (uCategory)
			theApp.downloadqueue->StartNextFileIfPrefs(uCategory);
		if (params.bAppendAutoDeleteEd2kLink)
			AppendAutoDeleteEd2kLinkToBackupFile(params.strAutoDeleteEd2kLink);
		file->DeletePartFile(params.bAddToCanceledMet);
		AddLogLine(true, (LPCTSTR)EscPercent(params.cLogMsg));
		break;
	}
	}
	SetRedraw(true);
}

bool CDownloadListCtrl::ProcessDownloadInspectorApplyItem(SDownloadInspectorApplyParams &item, DWORD dwSliceStartTick, DWORD dwSliceBudgetMs, UINT uMaxItemsPerSlice, UINT &uProcessedInSlice)
{
	auto ShouldYield = [&]() {
		const DWORD dwElapsed = static_cast<DWORD>(::GetTickCount() - dwSliceStartTick);
		return uProcessedInSlice >= uMaxItemsPerSlice || (uProcessedInSlice != 0 && dwElapsed >= dwSliceBudgetMs);
	};

	if (item.bHasAutoDeleteApply) {
		while (item.uNextAutoDeleteStateItem < item.autoDeleteApply.vecItems.size()) {
			ApplyAutoDeleteStateItem(item.autoDeleteApply, item.autoDeleteApply.vecItems[item.uNextAutoDeleteStateItem]);
			++item.uNextAutoDeleteStateItem;
			++uProcessedInSlice;
			if (ShouldYield())
				return false;
		}
		if (!item.bAutoDeleteScheduleApplied) {
			ApplyAutoDeleteSchedule(item.autoDeleteApply);
			item.bAutoDeleteScheduleApplied = true;
			++uProcessedInSlice;
			if (ShouldYield())
				return false;
		}
	}

	while (item.uNextRenameItem < item.vecRenameItems.size()) {
		ApplyInvalidExtensionFound(item.vecRenameItems[item.uNextRenameItem]);
		++item.uNextRenameItem;
		++uProcessedInSlice;
		if (ShouldYield())
			return false;
	}

	while (item.uNextRemoveItem < item.vecRemoveItems.size()) {
		ApplyEmptyFakeFileFound(item.vecRemoveItems[item.uNextRemoveItem]);
		++item.uNextRemoveItem;
		++uProcessedInSlice;
		if (ShouldYield())
			return false;
	}

	if (!item.bCompletionLogged) {
		if (item.bFileFound)
			theApp.QueueLogLine(true, GetResString(_T("DOWNLOAD_INSPECTOR_COMPLETED_FOUND")));
		else
			theApp.QueueLogLine(true, GetResString(_T("DOWNLOAD_INSPECTOR_COMPLETED_NOT_FOUND")));
		item.bCompletionLogged = true;
	}

	return true;
}

LRESULT CDownloadListCtrl::OnApplyDownloadInspectorResults(WPARAM, LPARAM lParam)
{
	m_bDownloadInspectorApplyPending = false;
	SDownloadInspectorApplyParams* pIncoming = reinterpret_cast<SDownloadInspectorApplyParams*>(lParam);
	if (pIncoming != NULL)
		m_downloadInspectorApplyItems.AddTail(pIncoming);

	if (theApp.IsClosing() || !::IsWindow(m_hWnd)) {
		ClearDownloadInspectorApplyItems();
		return 0;
	}

	DWORD dwSliceBudgetMs = 8;
	UINT uMaxItemsPerSlice = 512;
	GetChunkedRemoveDownloadSliceLimits(dwSliceBudgetMs, uMaxItemsPerSlice);
	const DWORD dwSliceStartTick = ::GetTickCount();
	UINT uProcessedInSlice = 0;
	while (!m_downloadInspectorApplyItems.IsEmpty()) {
		SDownloadInspectorApplyParams* pItem = m_downloadInspectorApplyItems.GetHead();
		if (pItem == NULL) {
			m_downloadInspectorApplyItems.RemoveHead();
			::InterlockedDecrement(&m_lDownloadInspectorApplyPending);
			continue;
		}
		if (!ProcessDownloadInspectorApplyItem(*pItem, dwSliceStartTick, dwSliceBudgetMs, uMaxItemsPerSlice, uProcessedInSlice))
			break;
		m_downloadInspectorApplyItems.RemoveHead();
		delete pItem;
		::InterlockedDecrement(&m_lDownloadInspectorApplyPending);
	}

	if (!m_downloadInspectorApplyItems.IsEmpty() && !PostDownloadInspectorApplyMessage()) {
		AddDebugLogLine(DLP_HIGH, false, _T("Download Inspector apply aborted because the continuation message could not be posted. remaining=%d\n"), static_cast<int>(m_downloadInspectorApplyItems.GetCount()));
		ClearDownloadInspectorApplyItems();
	}

	return 0;
}

LRESULT CDownloadListCtrl::OnInvalidExtensionFound(WPARAM, LPARAM lParam)
{
	PartFileOperationMsgParams* params = reinterpret_cast<PartFileOperationMsgParams*>(lParam); // Get parameters
	if (params != NULL)
		ApplyInvalidExtensionFound(*params);
	return 0;
}

LRESULT CDownloadListCtrl::OnEmptyFakeFileFound(WPARAM, LPARAM lParam)
{
	PartFileOperationMsgParams* params = reinterpret_cast<PartFileOperationMsgParams*>(lParam);
	if (params != NULL)
		ApplyEmptyFakeFileFound(*params);

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

bool CDownloadListCtrl::IsLiveUpdateSortColumn(int iSortColumn) const
{
	switch (iSortColumn) {
	case 2:  // Transferred
	case 3:  // Completed
	case 4:  // Speed
	case 5:  // Progress
	case 6:  // Sources / client software
	case 7:  // Priority / queue rank
	case 8:  // Status
	case 9:  // Remaining time
	case 10: // Seen complete
	case 11: // Last reception
	case 15: // Preview
	case 16: // Compression
	case 90: // Remaining size
		return true;
	default:
		return false;
	}
}

bool CDownloadListCtrl::IsLiveUpdateSortOrderAffected() const
{
	if (GetSortItem() == -1)
		return false;

	LPARAM lSortOrder = m_pSortParam;
	for (int iVisitedSorts = 0; lSortOrder != -1 && iVisitedSorts < 20; ++iVisitedSorts) {
		if (IsLiveUpdateSortColumn(LOWORD(lSortOrder)))
			return true;
		lSortOrder = GetNextSortOrder(lSortOrder);
	}
	return false;
}

bool CDownloadListCtrl::HasListedItemsSortOrderChanged() const
{
	if (m_ListedItemsVector.size() < 2)
		return false;

	const bool bOldRawSortState = m_bRawSortInProgress;
	const_cast<CDownloadListCtrl*>(this)->m_bRawSortInProgress = true;
	bool bChanged = false;
	for (size_t i = 1; i < m_ListedItemsVector.size(); ++i) {
		if (SortProc(reinterpret_cast<LPARAM>(m_ListedItemsVector[i - 1]), reinterpret_cast<LPARAM>(m_ListedItemsVector[i]), m_pSortParam) > 0) {
			bChanged = true;
			break;
		}
	}
	const_cast<CDownloadListCtrl*>(this)->m_bRawSortInProgress = bOldRawSortState;
	return bChanged;
}

void CDownloadListCtrl::MaintainSortOrderAfterUpdate()
{
	if (theApp.IsClosing() || !::IsWindow(m_hWnd) || theApp.emuledlg == NULL || theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible())
		return;
	if (IsChunkedRemoveDownloadSnapshotActive())
		return;
	if (!IsLiveUpdateSortOrderAffected() || !HasListedItemsSortOrderChanged())
		return;

	SaveListState(0, kDownloadListViewState);
	SetRedraw(false);
	const bool bOldRawSortState = m_bRawSortInProgress;
	m_bRawSortInProgress = true;
	CombinedSort(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), SortFunc);
	m_bRawSortInProgress = bOldRawSortState;
	RebuildListedItemsMap();
	RequestTransferListRedraw();
	UpdateDownloadListItemCount(*this, m_ListedItemsVector.size());
	RestoreListState(0, kDownloadListViewState, false);
	SetRedraw(true);
}
