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
#include "MuleListCtrl.h"
#include "eMuleAI/MenuXP.h"
#include "ListCtrlItemWalk.h"
#include "KnownFileList.h"
#include <vector>

class CSharedFileList;
class CKnownFile;
class CPartFile;
class CShareableFile;
class CDirectoryItem;
class CToolTipCtrlX;

enum FilterType : uint8
{
	Shared,
	History,
	Duplicate,
	FileSystem
};

class CSharedFilesCtrl : public CMuleListCtrl, public CListCtrlItemWalk, public CListStateTemplate<CSharedFilesCtrl, CKnownFile>
{
	friend class CListStateTemplate<CSharedFilesCtrl, CKnownFile>;

private:
	using ListStateHelper = CListStateTemplate<CSharedFilesCtrl, CKnownFile>;
public:
	using ListStateHelper::SaveListState;
	using ListStateHelper::RestoreListState;

	enum { SharedFilesColumnCount = 26 };

	DECLARE_DYNAMIC(CSharedFilesCtrl)
	friend class CSharedDirsTreeCtrl;
	friend class CemuleDlg;

public:
	class CShareDropTarget: public COleDropTarget
	{
	public:
		CShareDropTarget();
		virtual	~CShareDropTarget();
		void	SetParent(CSharedFilesCtrl *pParent)	{ m_pParent = pParent; }

		DROPEFFECT	OnDragEnter(CWnd *pWnd, COleDataObject *pDataObject, DWORD dwKeyState, CPoint point);
		DROPEFFECT	OnDragOver(CWnd*, COleDataObject *pDataObject, DWORD, CPoint point);
		BOOL		OnDrop(CWnd*, COleDataObject *pDataObject, DROPEFFECT dropEffect, CPoint point);
		void		OnDragLeave(CWnd*);

	protected:
		IDropTargetHelper	*m_piDropHelper;
		bool				m_bUseDnDHelper;
//		BOOL ReadHdropData (COleDataObject *pDataObject);
		CSharedFilesCtrl	*m_pParent;
	};

	CSharedFilesCtrl();
	virtual	~CSharedFilesCtrl();

	void	Init();
	void	SetToolTipsDelay(DWORD dwDelay);
	void	CreateMenus();
	void	AddFile(CKnownFile*file, bool bBatchVisibleListUpdate = false);
	void	FlushBulkAddListUpdate(const EListStateField LsfFlag);
	bool	HasPendingBulkAddListUpdate() const			{ return m_bSharedFilesBulkAddPending; }
	bool	IsDeleteLikeBulkOperationActive() const;
	void	RemoveFile(CKnownFile*file, const bool bDeletedFromDisk, const bool bWillReloadListLater = false);
	void	RemoveFromHistory(CKnownFile* toRemove, const bool bWillReloadListLater = false, const bool bNotifySharedFilesList = true);
	void	ReloadList(const bool bSortCurrentList, const EListStateField LsfFlag);
	void	ReloadListForActivation(const EListStateField LsfFlag);
	void	ReloadListFromApplicationEvent(const bool bSortCurrentList, const EListStateField LsfFlag);
	void	RebuildListedItemsMap();
	int		FindListedIndexByPointer(CKnownFile* pFile) const;
	virtual DWORD_PTR GetVirtualItemData(int iItem) const override { return (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size() ? 0 : static_cast<DWORD_PTR>(iItem + 1)); } // Owner-data row data is a stable visible index, not a backend pointer
	int		GetVirtualItemCount() const override { return m_ListedItemsVector.size(); }
	CObject* GetItemObject(int iIndex) const; 
	uint32  GetFilterId() const;
	void	SetAllIcons();
	std::vector<CKnownFile*> m_ListedItemsVector; // This vector is used to list and iterate files.
	std::vector<CKnownFile*> m_vecSharedFilesReloadScratch;
	typedef	CMap<CKnownFile*, CKnownFile*, int, int&> CHistoryFilesMap;
	CHistoryFilesMap m_ListedItemsMap; // This map is used to lookup file index.
	FilterType m_eFilter; // Type of directory this control is displaying
	uint32 m_uFilterID; // ID of the filter, used to identify the filter in the list
	volatile LONG nAICHHashing;
	void	UpdateFile(CKnownFile* file, const bool bUpdateFileSummary = true, const bool bDeletedFromDisk = false, const int iIndex = -1);
	bool	CheckBoxesEnabled() const;
	void	Localize();
	void	ShowFilesCount();
	void	UpdateBackendDownloadRemoveOverlay(UINT uDone, UINT uTotal, uint64 uSequence, uint64 uCorrelationId);
	void	HideBackendDownloadRemoveOverlay(uint64 uSequence, uint64 uCorrelationId);
	void	BeginBackendDownloadRemoveVisibleRows(const CStringArray& astrDownloadHashes, uint64 uSequence = 0, uint64 uCorrelationId = 0);
	void	RemoveBackendDownloadRowsByHash(const std::vector<CString>& vecFileHashes);
	bool	IsCompletedBackendDownloadRemoveOverlay(uint64 uSequence, uint64 uCorrelationId) const;
	void	MarkCompletedBackendDownloadRemoveOverlay(uint64 uSequence, uint64 uCorrelationId);
	void	BeginDownloadRemoveBatch();
	void	EndDownloadRemoveBatch();
	bool	IsDownloadRemoveBatchActive() const		{ return m_uDownloadRemoveBatchDepth != 0; }
	bool	GetActiveSharedFilesBulkOperationProgress(bool& bDeleteLike, UINT& uDone, UINT& uTotal) const;
	bool	GetActiveSharedFilesHashingProgress(UINT& uDone, UINT& uTotal) const;
	bool	GetActiveSharedFilesMetadataProgress(UINT& uDone, UINT& uTotal) const;
	bool	IsFileSystemReloadActive() const;
	static bool ProcessFileSystemReloadWorkerItem(const CemuleApp::SWorkerTopologyItem &item);

	void	ShowComments(CShareableFile *file);
	bool	ExecuteSharedFilesCommandFromEvent(UINT uAction, const std::vector<CString> &vecItemHashes, uint64 uSequence, uint64 uCorrelationId);
	LONG	GetAICHHashing()							{ return InterlockedCompareExchange(&nAICHHashing, 0, 0); }
	void	SetAICHHashing(INT_PTR nVal)				{ InterlockedExchange(&nAICHHashing, static_cast<LONG>(nVal)); }
	CDirectoryItem* GetDirectoryFilter()				{ return m_pDirectoryFilter; }
	void	SetDirectoryFilter(CDirectoryItem *pNewFilter, bool bRefresh = true);
	bool	IsSelectionRestoreInProgress() const		{ return m_bSelectionRestoreInProgress; }
	void	SetSelectionRestoreInProgress(bool bInProgress)	{ m_bSelectionRestoreInProgress = bInProgress; }
protected:
	CMenuXP		m_SharedFilesMenu;
	CMenuXP		m_CollectionsMenu;
	CMenuXP			m_PrioMenu;
	CMenuXP			m_PermMenu;
	CMenuXP			m_PowershareMenu;
	CMenuXP			m_PowerShareLimitMenu;
	CMenuXP			m_SpreadbarMenu;
	CMenuXP			m_HideOSMenu;
	CMenuXP			m_SelectiveChunkMenu;
	CMenuXP			m_ShareOnlyTheNeedMenu;
	bool			m_aSortBySecondValue[4];
	CImageList		m_ImageList;
	CDirectoryItem	*m_pDirectoryFilter;
	//volatile INT_PTR	nAICHHashing;
	static LPARAM	m_pSortParam;
	CMenuXP			m_FileHistorysMenu;
	CMenuXP			m_PreviewMenu;
	int 			m_iDataSize;
	CToolTipCtrlX	*m_pToolTip;
	CTypedPtrList<CPtrList, CShareableFile*>	liTempShareableFilesInDir;
	CShareableFile *m_pHighlightedItem;
	CShareDropTarget m_ShareDropTarget;
	bool m_bSelectionRestoreInProgress;
	bool m_bExecutingSharedFilesCommand;
	volatile LONG m_lFileSystemReloadGeneration;
	volatile LONG m_lFileSystemReloadActive;

	enum ESharedFilesBulkOperation
	{
		SharedFilesBulkOperationNone,
		SharedFilesBulkOperationDelete,
		SharedFilesBulkOperationUnshare,
		SharedFilesBulkOperationUpdateMetadata,
		SharedFilesBulkOperationRemoveHistory,
		SharedFilesBulkOperationClearHistory,
		SharedFilesBulkOperationSetPriority,
		SharedFilesBulkOperationToggleShareStatus
	};

	struct SSharedFilesBulkItem
	{
		SSharedFilesBulkItem() {}
		CString m_strKey;
		CString m_strFilePath;
	};

	CTypedPtrList<CPtrList, SSharedFilesBulkItem*> m_sharedFilesBulkItems;
	CMapStringToPtr m_sharedFilesBulkResolver;
	CMapStringToPtr m_sharedFilesBulkQueuedKeys;
	bool m_bSharedFilesBulkCollectingSelection;
	int m_iSharedFilesBulkNextSelectionIndex;
	UINT m_uSharedFilesBulkSelectionQueued;
	ESharedFilesBulkOperation m_eSharedFilesBulkOperation;
	UINT m_uSharedFilesBulkAction;
	UINT m_uSharedFilesBulkProcessed;
	UINT m_uSharedFilesBulkFailed;
	UINT m_uSharedFilesBulkStale;
	UINT m_uSharedFilesBulkTotal;
	uint64 m_uSharedFilesBulkSequence;
	uint64 m_uSharedFilesBulkCorrelationId;
	DWORD m_dwSharedFilesBulkStartedTick;
	DWORD m_dwSharedFilesBulkLastProgressTick;
	DWORD m_dwSharedFilesBulkLastCompactTick;
	bool m_bSharedFilesBulkPending;
	bool m_bSharedFilesBulkListStateBatchActive;
	bool m_bSharedFilesBulkRemovePending;
	bool m_bSharedFilesBulkRemoveRowsDetached;
	bool m_bSharedFilesBulkRemoveVisibleSnapshotActive;
	size_t m_uSharedFilesBulkRemoveVisibleSnapshotRows;
	bool m_bBackendDownloadRemoveOverlayActive;
	bool m_bBackendDownloadRemoveRowsDetached;
	bool m_bBackendDownloadRemoveVisibleSnapshotActive;
	size_t m_uBackendDownloadRemoveVisibleSnapshotRows;
	CMapStringToPtr m_backendDownloadRemoveHiddenRows;
	UINT m_uDownloadRemoveBatchDepth;
	bool m_bDownloadRemoveBatchPending;
	uint64 m_uBackendDownloadRemoveSequence;
	uint64 m_uBackendDownloadRemoveCorrelationId;
	uint64 m_uCompletedBackendDownloadRemoveSequence;
	uint64 m_uCompletedBackendDownloadRemoveCorrelationId;
	uint32 m_uSharedFilesBulkListStateID;
	bool m_bSharedFilesBulkAddPending;
	UINT m_uSharedFilesHashingOverlayTotal;
	UINT m_uSharedFilesHashingOverlayLastRemaining;
	UINT m_uSharedFilesMetadataOverlayTotal;
	UINT m_uSharedFilesMetadataOverlayLastRemaining;
	bool m_bSharedFilesRawSortInProgress;
	UINT m_uSharedFilesListReloadDeferDepth;
	bool m_bSharedFilesListReloadDeferred;
	bool m_bSharedFilesListReloadDeferredSortCurrentList;
	EListStateField m_eSharedFilesListReloadDeferredState;

	CString BuildSharedFileCommandKey(const CKnownFile* pFile) const;
	void UpdateListedItemsMapRange(int iStartIndex, int iEndIndex);
	int FindListedIndexByCommandKey(const CString& strCommandKey) const;
	static int CALLBACK SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort);
	static bool SortFunc(const CKnownFile* fileA, const CKnownFile* fileB);
	void RequestSharedListRedrawForRange(int iFirst, int iLast);
	void RequestSharedListRedraw();
	bool IsSharedFilesBulkDeleteLikeOperation() const;
	bool IsHiddenBySharedFilesBulkRemove(const CKnownFile *pFile) const;
	bool IsHiddenByBackendDownloadRemove(const CKnownFile *pFile) const;
	bool IsHiddenBySharedFilesVisibleRemove(const CKnownFile *pFile) const;
	bool IsSharedFilesBulkRemoveSnapshotActive() const;
	bool IsBackendDownloadRemoveSnapshotActive() const;
	bool IsSharedFilesVisibleRemoveSnapshotActive() const;
	bool QueueBackendDownloadRemoveHiddenHash(LPCTSTR pszHash);
	void ApplySharedFilesBulkRemoveVisibleItemCount(bool bForceFrameUpdate);
	void DetachSharedFilesVisibleRemoveRows();
	void DetachSharedFilesBulkRemoveVisibleRows();
	void ClearSharedFilesBulkRemoveHiddenRows(bool bReloadVisibleList);
	void ClearBackendDownloadRemoveHiddenRows(bool bReloadVisibleList);
	void BeginSharedFilesListReloadDefer();
	void EndSharedFilesListReloadDefer();
	void ReloadListInternal(const bool bSortCurrentList, const EListStateField LsfFlag, const bool bAllowHidden);
	bool HasActiveSortOrder() const;
	bool NeedsSortReposition(int iIndex) const;
	bool RepositionFileByCurrentSort(CKnownFile* file, int iIndex);
	void OpenFile(const CShareableFile *file);
	void ShowFileDialog(CTypedPtrList<CPtrList, CShareableFile*> &aFiles, UINT uInvokePage = 0);
	const CString GetItemDisplayText(const CShareableFile *file, const int iSubItem) const;
	const bool IsFilteredOut(const CShareableFile *pKnownFile) const;
	const bool IsSharedInKad(const CKnownFile *file) const;
	void CheckBoxClicked(const int iItem);
	bool ShouldRouteSharedFilesCommand(UINT uAction) const;
	void QueueSharedFilesCommandFromCurrentSelection(UINT uAction);
	bool IsCurrentSharedFileForSharedFilesAction(const CKnownFile *pFile) const;
	bool CanUnshareFile(const CShareableFile *pFile) const;
	bool CanUnshareSelectedSharedFiles();
	bool CanDeleteSelectedSharedFilesFromDisk();
	bool CanUpdateSelectedSharedFilesMetadata();
	bool QueueDownloadRemoveCommandFromCurrentSelection(UINT uAction);
	bool IsSharedFilesBulkOperationAction(UINT uAction) const;
	bool StartSharedFilesBulkOperation(UINT uAction, const std::vector<CString> &vecItemKeys, uint64 uSequence, uint64 uCorrelationId);
	bool QueueSharedFilesBulkItem(CKnownFile *pFile);
	bool QueueSharedFilesBulkKey(const CString &strKey);
	bool HasQueuedSharedFilesBulkItem(const CString &strKey);
	CKnownFile* ResolveSharedFilesBulkItem(const SSharedFilesBulkItem &item);
	bool ProcessSharedFilesBulkSelectionQueueSlice(DWORD dwSliceStartTick, DWORD& dwSliceBudgetMs, UINT& uMaxItemsPerSlice, UINT& uProcessedInSlice);
	bool ProcessSharedFilesBulkItem(SSharedFilesBulkItem &item);
	bool ProcessSharedFilesBulkDelete(CKnownFile *pFile, const SSharedFilesBulkItem &item);
	bool ProcessSharedFilesBulkUnshare(CKnownFile *pFile, const SSharedFilesBulkItem &item);
	bool ProcessSharedFilesBulkUpdateMetadata(CKnownFile *pFile, const SSharedFilesBulkItem &item);
	bool ProcessSharedFilesBulkRemoveHistory(CKnownFile *pFile, const SSharedFilesBulkItem &item);
	bool ProcessSharedFilesBulkSetPriority(CKnownFile *pFile, const SSharedFilesBulkItem &item);
	bool ProcessSharedFilesBulkToggleShareStatus(CKnownFile *pFile, const SSharedFilesBulkItem &item);
	bool PostSharedFilesBulkOperationMessage();
	void UpdateSharedFilesBulkOverlay();
	void UpdateSharedFilesHashingOverlay();
	void UpdateSharedFilesMetadataOverlay();
	void ClearSharedFilesBulkOperation();
	void FinishSharedFilesBulkOperation();
	bool CompactNullSharedFilesItems(LPCTSTR pszReason);
	void QueueSharedFilesBulkFailureEvent(const SSharedFilesBulkItem &item, LPCTSTR pszStage, DWORD dwError);
	virtual void OnOperationOverlayCancel() override;
	void StartFileSystemReloadJob(const CString &strDirectory);
	virtual bool UsePersistentInfoTips() const override { return true; }
	virtual bool GetPersistentInfoTipText(const SPersistentInfoTipContext& context, CString& strText) override;

	virtual BOOL OnCommand(WPARAM wParam, LPARAM);
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnContextMenu(CWnd*, CPoint point);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnNmDblClk(LPNMHDR, LRESULT *pResult);
	afx_msg void OnSysColorChange();
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();
	afx_msg LRESULT OnFileSystemReloadReady(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnProcessSharedFilesBulkOperation(WPARAM wParam, LPARAM lParam);
	afx_msg BOOL OnNMClick(LPNMHDR pNMHDR, LRESULT *pResult);
};
