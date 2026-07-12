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
#pragma once
#include "emule.h"
#include <map>
#include "MuleListCtrl.h"
#include "eMuleAI/MenuXP.h"
#include "ListCtrlItemWalk.h"
#include "ToolTipCtrlX.h"
#include "DownloadQueue.h"
#include <vector>

#define COLLAPSE_ONLY	0
#define EXPAND_ONLY		1
#define EXPAND_COLLAPSE	2

// Forward declaration
class CPartFile;
class CUpDownClient;
class CDownloadListCtrl;

///////////////////////////////////////////////////////////////////////////////
// CtrlItem_Struct

enum ItemType
{
	INVALID_TYPE = -1,
	FILE_TYPE = 1,
	AVAILABLE_SOURCE = 2,
	UNAVAILABLE_SOURCE = 3
};

class CtrlItem_Struct : public CObject
{
	DECLARE_DYNAMIC(CtrlItem_Struct)

public:
	~CtrlItem_Struct()							{ status.DeleteObject(); }

	ItemType		type;
	CPartFile		*owner;
	void			*value; // could be either CPartFile or CUpDownClient
	CtrlItem_Struct	*parent;
	CString			strOwnerHash;
	DWORD			dwUpdated;
	bool			m_bDeletedGhost;
	CBitmap			status;
};


///////////////////////////////////////////////////////////////////////////////
// CDownloadListListCtrlItemWalk

class CDownloadListListCtrlItemWalk : public CListCtrlItemWalk
{
public:
	explicit CDownloadListListCtrlItemWalk(CDownloadListCtrl *pListCtrl);

	virtual CObject* GetNextSelectableItem();
	virtual CObject* GetPrevSelectableItem();
	const bool PlayNextPreviewableFile(const int iAppIndex = -1);

	void SetItemType(ItemType eItemType)		{ m_eItemType = eItemType; }

protected:
	CDownloadListCtrl *m_pDownloadListCtrl;
	ItemType m_eItemType;
};

///////////////////////////////////////////////////////////////////////////////
// CDownloadListCtrl
class CDownloadListCtrl : public CMuleListCtrl, public CDownloadListListCtrlItemWalk, public CListStateTemplate<CDownloadListCtrl, CtrlItem_Struct>
{
	friend class CListStateTemplate<CDownloadListCtrl, CtrlItem_Struct>;

private:
	using ListStateHelper = CListStateTemplate<CDownloadListCtrl, CtrlItem_Struct>;
public:
	using ListStateHelper::SaveListState;
	using ListStateHelper::RestoreListState;

	DECLARE_DYNAMIC(CDownloadListCtrl)
	friend class CDownloadListListCtrlItemWalk;

public:
	CDownloadListCtrl();
	virtual	~CDownloadListCtrl();
	CDownloadListCtrl(const CDownloadListCtrl&) = delete;
	CDownloadListCtrl& operator=(const CDownloadListCtrl&) = delete;

	UINT	curTab;

	void	UpdateItem(void *toupdate, bool bForce = false);
	void	Init();
	void	AddFile(CPartFile *toadd, bool bBatchVisibleListUpdate = false);
	void	AddSource(CPartFile *owner, CUpDownClient *source, bool notavailable);
	void	RemoveSource(CUpDownClient *source, CPartFile *owner);
	bool	RemoveFile(CPartFile* toremove);
	void	ReloadList(const bool bSortCurrentList, const EListStateField LsfFlag);
	void	MarkDeferredReload();
	void	FlushDeferredReload(const EListStateField LsfFlag);
	void	FlushBulkAddListUpdate(const EListStateField LsfFlag);
	void	RefreshBulkAddDisplayCounts();
	void	RebuildListedItemsMap();
	virtual DWORD_PTR GetVirtualItemData(int iItem) const override { return (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size() ? 0 : static_cast<DWORD_PTR>(iItem + 1)); } // Owner-data row data is a stable visible index, not a backend pointer
	int		GetVirtualItemCount() const override { return m_ListedItemsVector.size(); }
	CObject* GetItemObject(int iIndex) const;

	std::vector<CtrlItem_Struct*> m_ListedItemsVector; // This vector is used to list, iterate and sort files.
	std::vector<CtrlItem_Struct*> m_vecDownloadReloadScratch; // Reused scratch buffer for atomic reload visible list.
	typedef	CMap<CtrlItem_Struct*, CtrlItem_Struct*, int, int&> CListedItemsMap;
	CListedItemsMap m_ListedItemsMap; // This map is used to lookup items index.
	uint32 m_uListedFilesCount;

	typedef std::multimap<void*, CtrlItem_Struct*> ListItems;
	ListItems	m_ListItems; // Moved to public

	void	ClearCompleted(int incat = -2);
	void	ClearCompleted(const CPartFile *pFile);
	void	SetStyle();
	void	CreateMenus();
	void	Localize();
	void	ChangeCategory(int newsel);
	CString getTextList();
	void	ShowSelectedFileDetails();
	void	HideFile(CPartFile *tohide);
	void	ShowFile(CPartFile *toshow);
	void	ExpandCollapseItem(int iItem, int iAction, bool bCollapseSource = false);
	void	HideSources(CPartFile *toCollapse);
	void	GetDisplayedPartFiles(CArray<CPartFile*, CPartFile*> *list);
	void	MoveCompletedfilesCat(UINT from, UINT to);
	int		GetCompleteDownloads(int cat, int &total);
	void	UpdateCurrentCategoryView();
	void	UpdateCurrentCategoryView(CPartFile *thisfile);
	CImageList* CreateDragImage(int iItem, LPPOINT lpPoint);
	void	FillCatsMenu(CMenuXP &rCatsMenu, int iFilesInCats = -1);
	CMenuXP* GetPrioMenu();
	float	GetFinishedSize();
	bool	ReportAvailableCommands(CList<int> &liAvailableCommands);
	void	DownloadInspector(const bool bForce = false);
	void	ResetDownloadInspectorAutoDeleteState();
	void	ShowActiveDownloadsBold(const bool bEnabled);
	const bool IsFilteredOut(CPartFile* pFile);
	uint32 GetTotalFilesCount();
	bool	m_bRightClicked;
	
	static void SetFileDeletionInProgress(bool bInProgress) { s_bFileDeletionInProgress = bInProgress; }
	static bool IsFileDeletionInProgress() { return s_bFileDeletionInProgress; }
	void	StartChunkedRemoveDownloadsFromCommand(const CStringArray &astrItemHashes, bool bAddToCanceledMet, bool bDeleteCompletedFile, uint64 uSequence = 0, uint64 uCorrelationId = 0);
	void	StartChunkedDownloadStateChangeFromCommand(const CStringArray &astrItemHashes, UINT uAction, int iActionValue = 0, uint64 uSequence = 0, uint64 uCorrelationId = 0);
	bool	HasActiveChunkedDownloadOperation() const;
	void	CancelActiveChunkedDownloadOperation();
	bool	GetActiveChunkedDownloadOperationProgress(CString& strTitle, CString& strBody, CString& strCancelAndExit, CString& strWaitAndExit, UINT& uDone, UINT& uTotal) const;
	void	UpdateBackendDownloadCommandOverlay(bool bRemove, UINT uDone, UINT uTotal, uint64 uSequence, uint64 uCorrelationId);
	void	HideBackendDownloadCommandOverlay(uint64 uSequence, uint64 uCorrelationId);
	void	CompleteChunkedRemoveDownloadDiskCleanup(uint64 uSequence, uint64 uCorrelationId, UINT uCompletedCount, UINT uFailedCount);
	void	RemoveFilesByHash(const std::vector<CString>& vecFileHashes);
	void	RemoveDeletedCompletedFilesByHash(const std::vector<CString>& vecFileHashes);
	void	BeginBackendDownloadRemoveBatch();
	void	EndBackendDownloadRemoveBatch(bool bFlushVisibleItems);
	void	UpdateMirroredSearchDownloadOverlay(const CString& strTitle, const CString& strDetail, UINT uDone, UINT uTotal);
	void	HideMirroredSearchDownloadOverlay();
	CPartFile* ResolveDownloadItemForCommand(const SDownloadItemId &id) const;
	void	RefreshAfterBackendDownloadCommand(UINT uAction = 0);
	void	RefreshAfterDownloadListMembershipChanged();
	bool	IsListedDownloadFileRow(int iItem) const;
	bool	ChangeSelectedFilesCategoryFromUi(UINT uCategory);
protected:
	CImageList  m_ImageList;
	CMenuXP	m_PermMenu;
	CMenuXP	m_PrioMenu;
	CMenuXP	m_FileMenu;
	CMenuXP	m_PreviewMenu;
	CMenuXP	m_SourcesMenu;
	bool		m_bRemainSort;
	bool		m_bDeferredReload;
	bool		m_bRawSortInProgress;
	typedef std::pair<void*, CtrlItem_Struct*> ListItemsPair;
protected:
	CFont		m_fontBold; // may contain a locally created bold font
	CFont		*m_pFontBold;// points to the bold font which is to be used (may be the locally created or the default bold font)
	CToolTipCtrlX m_tooltip;
	DWORD		m_dwLastAvailableCommandsCheck;
	bool		m_availableCommandsDirty;

	static LPARAM	m_pSortParam;
	int 			m_iDataSize;
	const static bool SortFunc(const CtrlItem_Struct* first, const CtrlItem_Struct* second);

	void ShowFileDialog(UINT uInvokePage);
	void ShowClientDialog(CUpDownClient *pClient);
	bool TryGetActionPoint(const NMITEMACTIVATE* pNMIA, CPoint& point);
	bool IsPointOverFileNameColumn(int iItem, const CPoint& point);
	bool IsPointOverFilePreviewIcon(int iItem, const CPoint& point);
	bool IsPointOverFileRatingIcon(int iItem, const CPoint& point, const CPartFile* pFile);
	bool IsPointOverPreviewActivationArea(int iItem, const CPoint& point);
	void PreviewFileOrBeep(CPartFile* pFile);
	void SetAllIcons();
	bool AddFileToListModel(CPartFile *pFile);
	bool SyncFileItemsWithDownloadModel();
	bool ShouldShowDownloadItemInList(const CtrlItem_Struct *pCtrlItem);
	bool IsHiddenByChunkedRemoveDownload(const CtrlItem_Struct *pCtrlItem) const;
	bool IsChunkedRemoveDownloadSnapshotActive() const;
	bool IsChunkedRemoveDownloadDetachTarget(const CtrlItem_Struct *pCtrlItem) const;
	void ApplyChunkedRemoveDownloadVisibleItemCount(bool bForceFrameUpdate);
	void DetachChunkedRemoveDownloadVisibleRows();
	void ClearChunkedRemoveDownloadHiddenRows(bool bReloadVisibleList);
	CtrlItem_Struct* FindFileItem(CPartFile *pFile) const;
	CtrlItem_Struct* FindSourceItem(CPartFile *pOwner, CUpDownClient *pSource) const;
	bool GetSourceItemTypeFromOwner(CPartFile *pOwner, CUpDownClient *pSource, ItemType &eItemType) const;
	bool EnsureSourceItem(CPartFile *pOwner, CUpDownClient *pSource, ItemType eItemType, CtrlItem_Struct *pOwnerItem);
	bool SyncSourceItemsForOwner(CPartFile *pOwner, CtrlItem_Struct *pOwnerItem);
	bool SyncSourceItemsWithDownloadModel();
	void BuildVisibleDownloadItems(std::vector<CtrlItem_Struct*> &visibleItems, uint32 &uListedFilesCount);
	bool RemoveVisibleSourcesForOwner(CPartFile *pOwner);
	bool RemoveSourceItemsForOwner(CPartFile *pOwner);
	void BuildSortedSourceItemsForFile(CPartFile *pOwner, std::vector<CtrlItem_Struct*> &sourceItems);
	void InsertSortedVisibleSourcesForFile(CPartFile *pOwner, int iParentIndex);
	void DrawFileItem(CDC *dc, int iItem, int nColumn, LPCRECT lpRect, UINT uDrawTextAlignment, CtrlItem_Struct *pCtrlItem);
	void DrawSourceItem(CDC *dc, int iItem, int nColumn, LPCRECT lpRect, UINT uDrawTextAlignment, CtrlItem_Struct *pCtrlItem);
	CString GetFileItemDisplayText(const CPartFile *lpPartFile, int iSubItem) const;
	CString GetSourceItemDisplayText(const CtrlItem_Struct *pCtrlItem, int iSubItem) const;
	ItemType GetListedItemType(int iItem) const;
	bool TryGetListedDownloadItemId(int iItem, SDownloadItemId& id) const;
	CPartFile* ResolveListedDownloadFile(int iItem) const;
	CPartFile* ResolveListedParentDownloadFile(int iItem) const;
	DWORD GetListedSourceClientRuntimeID(int iItem) const;
	CUpDownClient* AcquireListedSourceClient(int iItem) const;
	CObject* CreateListedDetailWalkerToken(int iItem, ItemType eItemType) const;
	bool TryGetListedItemDisplayText(int iItem, int iSubItem, CString &strText) const;
	CString GetListedItemDisplayText(int iItem, int iSubItem) const;
	void RequestTransferListRedrawForRange(int iFirst, int iLast);
	void RequestTransferListRedraw();
	bool IsLiveUpdateSortColumn(int iSortColumn) const;
	bool IsLiveUpdateSortOrderAffected() const;
	bool HasListedItemsSortOrderChanged() const;
	virtual bool UsePersistentInfoTips() const override { return true; }
	virtual bool GetPersistentInfoTipText(const SPersistentInfoTipContext& context, CString& strText) override;
	virtual int GetDefaultPersistentInfoTipExtraLeftPadding(const SPersistentInfoTipContext& context) const override;
	virtual void OnOperationOverlayCancel() override;

	struct SChunkedRemoveDownloadItem
	{
		SChunkedRemoveDownloadItem();

		SDownloadItemId m_id;
	};

	enum EChunkedDownloadStateAction
	{
		ChunkedDownloadStatePermissionDefault,
		ChunkedDownloadStatePermissionNone,
		ChunkedDownloadStatePermissionFriends,
		ChunkedDownloadStatePermissionAll,
		ChunkedDownloadStatePriorityHigh,
		ChunkedDownloadStatePriorityLow,
		ChunkedDownloadStatePriorityNormal,
		ChunkedDownloadStatePriorityAuto,
		ChunkedDownloadStatePause,
		ChunkedDownloadStateResume,
		ChunkedDownloadStateStop,
		ChunkedDownloadStateSetSourceLimit,
		ChunkedDownloadStateSetCategory,
		ChunkedDownloadStateSetPauseOnPreview,
		ChunkedDownloadStateToggleAutoRenameToMajorityName,
		ChunkedDownloadStateCleanupFilename,
		ChunkedDownloadStateClearCompleted,
		ChunkedDownloadStateSetFileName,
		ChunkedDownloadStateTogglePreviewPriority,
		ChunkedDownloadStateImportParts
	};

	struct SChunkedDownloadStateItem
	{
		SChunkedDownloadStateItem();

		SDownloadItemId m_id;
	};

	static int CALLBACK SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort);
	const static int Compare(const CPartFile *file1, const CPartFile *file2, const LPARAM lParamSort);
	const static int Compare(const CUpDownClient *client1, const CUpDownClient *client2, const LPARAM lParamSort);
	void ClearChunkedRemoveDownloadItems();
	bool HasQueuedChunkedRemoveDownloadItem(const SDownloadItemId &id) const;
	bool QueueChunkedRemoveDownloadItem(const CPartFile *pFile);
	void StartChunkedRemoveDownloads(CTypedPtrList<CPtrList, CPartFile*> &selectedList, bool bAddToCanceledMet, bool bDeleteCompletedFile);
	bool QueueChunkedRemoveDownloadHash(LPCTSTR pszHash);
	CPartFile* FindListedDownloadById(const SDownloadItemId &id) const;
	bool ProcessChunkedRemoveDownloadItem(const SChunkedRemoveDownloadItem &item);
	void QueueChunkedRemoveFailureEvent(const SChunkedRemoveDownloadItem &item, LPCTSTR pszStage, LPCTSTR pszFilePath, DWORD dwError);
	void FlushChunkedRemoveVisibleItems();
	void QueueChunkedRemoveStartNextCategory(UINT uCategory);
	void FinishChunkedRemoveDownloads();
	bool PostChunkedRemoveDownloadMessage();
	UINT GetChunkedRemoveDownloadDoneCount() const;
	UINT GetChunkedRemoveDownloadProgressProcessed() const;
	bool IsChunkedRemoveDownloadSequenceActive(uint64 uSequence, uint64 uCorrelationId) const;
	void UpdateChunkedRemoveDownloadOverlay();
	void ClearChunkedDownloadStateItems();
	bool HasQueuedChunkedDownloadStateItem(const SDownloadItemId &id) const;
	bool QueueChunkedDownloadStateItem(const CPartFile *pFile);
	void StartChunkedDownloadStateChange(CTypedPtrList<CPtrList, CPartFile*> &selectedList, EChunkedDownloadStateAction eAction, int iActionValue = 0);
	bool QueueChunkedDownloadStateHash(LPCTSTR pszHash);
	void QueueChunkedDownloadStateFailureEvent(const SChunkedDownloadStateItem &item, LPCTSTR pszStage);
	bool ProcessChunkedDownloadStateItem(const SChunkedDownloadStateItem &item);
	void FinishChunkedDownloadStateChange();
	bool PostChunkedDownloadStateMessage();
	void UpdateChunkedDownloadStateOverlay();
	void RefreshMirroredSearchDownloadOverlay();
	bool IsCompletedBackendDownloadCommandOverlay(uint64 uSequence, uint64 uCorrelationId) const;
	void MarkCompletedBackendDownloadCommandOverlay(uint64 uSequence, uint64 uCorrelationId);

	CTypedPtrList<CPtrList, SChunkedRemoveDownloadItem*> m_chunkedRemoveDownloadItems;
	bool m_bChunkedRemoveDownloadPending;
	bool m_bChunkedRemoveDownloadAddToCanceledMet;
	bool m_bChunkedRemoveDownloadDeleteCompletedFile;
	bool m_bChunkedRemoveDownloadListStateBatchActive;
	bool m_bChunkedRemoveDownloadQueueBulkActive;
	bool m_bChunkedRemoveDownloadVisibleItemsPending;
	bool m_bChunkedRemoveDownloadVisibleSnapshotActive;
	size_t m_uChunkedRemoveDownloadVisibleSnapshotRows;
	bool m_bChunkedRemoveDownloadWaitingForDiskCleanup;
	UINT m_uChunkedRemoveDownloadPendingDiskDeletes;
	CArray<UINT, UINT> m_aChunkedRemoveStartNextCats;
	CMapStringToPtr m_mapChunkedRemoveDownloadHiddenRows;
	UINT m_uChunkedRemoveDownloadProcessed;
	UINT m_uChunkedRemoveDownloadStale;
	UINT m_uChunkedRemoveDownloadFailed;
	UINT m_uChunkedRemoveDownloadTotal;
	DWORD m_dwChunkedRemoveDownloadStartedTick;
	DWORD m_dwChunkedRemoveDownloadLastProgressTick;
	uint64 m_uChunkedRemoveDownloadSequence;
	uint64 m_uChunkedRemoveDownloadCorrelationId;
	CTypedPtrList<CPtrList, SChunkedDownloadStateItem*> m_chunkedDownloadStateItems;
	bool m_bChunkedDownloadStatePending;
	bool m_bChunkedDownloadStateListStateBatchActive;
	EChunkedDownloadStateAction m_eChunkedDownloadStateAction;
	int m_iChunkedDownloadStateValue;
	UINT m_uChunkedDownloadStateProcessed;
	UINT m_uChunkedDownloadStateStale;
	UINT m_uChunkedDownloadStateFailed;
	UINT m_uChunkedDownloadStateTotal;
	DWORD m_dwChunkedDownloadStateStartedTick;
	DWORD m_dwChunkedDownloadStateLastProgressTick;
	uint64 m_uChunkedDownloadStateSequence;
	uint64 m_uChunkedDownloadStateCorrelationId;
	bool m_bBackendDownloadCommandOverlayActive;
	uint64 m_uBackendDownloadCommandSequence;
	uint64 m_uBackendDownloadCommandCorrelationId;
	uint64 m_uCompletedBackendDownloadCommandSequence;
	uint64 m_uCompletedBackendDownloadCommandCorrelationId;
	bool m_bMirroredSearchDownloadOverlayActive;
	CString m_strMirroredSearchDownloadTitle;
	CString m_strMirroredSearchDownloadDetail;
	UINT m_uMirroredSearchDownloadDone;
	UINT m_uMirroredSearchDownloadTotal;

	virtual BOOL OnCommand(WPARAM wParam, LPARAM);
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
	
	// Live row updates must keep the active sort order without forcing a full reload.
	virtual bool ShouldMaintainSortOrderOnUpdate() const override { return IsLiveUpdateSortOrderAffected(); }
	virtual void MaintainSortOrderAfterUpdate() override;
	virtual void RefreshThemeColors() override;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnContextMenu(CWnd*, CPoint point);
	afx_msg void OnListModified(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnItemActivate(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnNmDblClk(LPNMHDR, LRESULT *pResult);
	afx_msg void OnSysColorChange();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg LRESULT OnEmptyFakeFileFound(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnInvalidExtensionFound(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnProcessChunkedRemoveDownloads(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnProcessChunkedDownloadState(WPARAM wParam, LPARAM lParam);
	afx_msg void OnDestroy();
	afx_msg LRESULT OnApplyDownloadInspectorResults(WPARAM wParam, LPARAM lParam);
private:
	static UINT AFX_CDECL DownloadInspectorProc(LPVOID pParam);
	CWinThread* pDownloadInspectorThread;
	DWORD m_dwLastDetection;
	time_t m_tNextAutoDeleteScan;
	volatile LONG m_lDownloadInspectorApplyPending;

	struct PartFileOperationMsgParams
	{
		PartFileOperationMsgParams()
			: bAppendAutoDeleteEd2kLink(false)
			, bAddToCanceledMet(true)
			, bAutoDeleteOperation(false)
			, lAutoDeleteGeneration(0)
		{
		}

		SDownloadItemId idDownload;
		CString strExpectedFileName;
		CString strExpectedFilePath;
		CString strNewFileName;
		CString cLogMsg;
		bool bAppendAutoDeleteEd2kLink;
		bool bAddToCanceledMet;
		CString strAutoDeleteEd2kLink;
		bool bAutoDeleteOperation;
		LONG lAutoDeleteGeneration;
		CString strAutoDeleteReason;
	};

	struct SAutoDeleteStateApplyItem
	{
		SAutoDeleteStateApplyItem()
			: bUpdateLegacyLastChecked(false)
			, tLastChecked(0)
			, bUpdateAutoDeleteState(false)
			, tLastAutoDeleteEvaluation(0)
			, tLastSeenCompleteForAutoDelete(0)
			, bAutoDeletePendingWhileBusy(false)
			, tNextAutoDeleteCheck(0)
		{
		}

		SDownloadItemId idDownload;
		bool bUpdateLegacyLastChecked;
		time_t tLastChecked;
		bool bUpdateAutoDeleteState;
		time_t tLastAutoDeleteEvaluation;
		time_t tLastSeenCompleteForAutoDelete;
		bool bAutoDeletePendingWhileBusy;
		time_t tNextAutoDeleteCheck;
	};

	struct SAutoDeleteStateApplyParams
	{
		SAutoDeleteStateApplyParams()
			: lAutoDeleteGeneration(0)
			, bUpdateNextAutoDeleteScan(false)
			, tNextAutoDeleteScan(0)
		{
		}

		LONG lAutoDeleteGeneration;
		bool bUpdateNextAutoDeleteScan;
		time_t tNextAutoDeleteScan;
		std::vector<SAutoDeleteStateApplyItem> vecItems;
	};

	struct SDownloadInspectorApplyParams
	{
		SDownloadInspectorApplyParams()
			: bHasAutoDeleteApply(false)
			, bAutoDeleteScheduleApplied(false)
			, bFileFound(false)
			, bCompletionLogged(false)
			, uNextAutoDeleteStateItem(0)
			, uNextRenameItem(0)
			, uNextRemoveItem(0)
		{
		}

		SAutoDeleteStateApplyParams autoDeleteApply;
		bool bHasAutoDeleteApply;
		bool bAutoDeleteScheduleApplied;
		bool bFileFound;
		bool bCompletionLogged;
		size_t uNextAutoDeleteStateItem;
		size_t uNextRenameItem;
		size_t uNextRemoveItem;
		std::vector<PartFileOperationMsgParams> vecRenameItems;
		std::vector<PartFileOperationMsgParams> vecRemoveItems;
	};

	void ClearDownloadInspectorApplyItems();
	bool HasDownloadInspectorApplyWork() const;
	bool PostDownloadInspectorApplyMessage();
	bool ProcessDownloadInspectorApplyItem(SDownloadInspectorApplyParams &item, DWORD dwSliceStartTick, DWORD dwSliceBudgetMs, UINT uMaxItemsPerSlice, UINT &uProcessedInSlice);
	void ApplyAutoDeleteStateItem(const SAutoDeleteStateApplyParams &params, const SAutoDeleteStateApplyItem &item);
	void ApplyAutoDeleteSchedule(const SAutoDeleteStateApplyParams &params);
	void ApplyInvalidExtensionFound(PartFileOperationMsgParams &params);
	void ApplyEmptyFakeFileFound(PartFileOperationMsgParams &params);

	CTypedPtrList<CPtrList, SDownloadInspectorApplyParams*> m_downloadInspectorApplyItems;
	bool m_bDownloadInspectorApplyPending;
	
	static bool s_bFileDeletionInProgress; // Track file deletion to avoid redundant SaveListState/RestoreListState calls
};
