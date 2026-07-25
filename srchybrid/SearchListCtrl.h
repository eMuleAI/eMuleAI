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
#include "ToolTipCtrlX.h"
#include <map>
#include <vector>

#define AVBLYSHADECOUNT 13

class CSearchList;
class CSearchFile;
struct SDownloadValidatorFuzzyQueryData;
struct SSearchResultId;

enum EFileSizeFormat
{
	fsizeDefault,
	fsizeKByte,
	fsizeMByte
};

struct SearchCtrlItem_Struct
{
	CSearchFile	*value;
	CSearchFile	*owner;
	uchar		filehash[16];
	uint16		childcount;
};

enum ESearchListRowType
{
	SearchListRowSearchFile = 0,
	SearchListRowPossibleKnownHeader,
	SearchListRowPossibleKnownFile
};

struct SSearchListRow
{
	SSearchListRow();
	ESearchListRowType eType;
	CSearchFile* pSearchFile;
	CSearchFile* pParentSearchFile;
	uint32 nSearchID;
	CString strName;
	CString strFolder;
	CString strMediaArtist;
	CString strMediaAlbum;
	CString strMediaTitle;
	CString strMediaCodec;
	CString strAICHHash;
	EMFileSize uSize;
	uint32 uMediaLengthSec;
	uint32 uMediaBitrateKbps;
	uint32 uSimilarityScore;
	uint8 uFileType;
	uint8 uSourceFlags;
	uint8 uBottomGroupStatusFlags;
	uchar ucHash[MDX_DIGEST_SIZE];
};

class CSearchListCtrl : public CMuleListCtrl, public CListCtrlItemWalk, public CListStateTemplate<CSearchListCtrl, SSearchListRow>
{
	friend class CListStateTemplate<CSearchListCtrl, SSearchListRow>;

private:
	using ListStateHelper = CListStateTemplate<CSearchListCtrl, SSearchListRow>;
public:
	using ListStateHelper::SaveListState;
	using ListStateHelper::RestoreListState;

	DECLARE_DYNAMIC(CSearchListCtrl)

public:
	CSearchListCtrl();
	virtual	~CSearchListCtrl();

	void	Init(CSearchList *in_searchlist);
	void	CreateMenus();
	void	UpdateSources(CSearchFile *toupdate, const bool bSort);
	void	AddResult(CSearchFile *toshow);
	void	RemoveResult(CSearchFile* toremove, bool bUpdateTabCount);
	void	StartChunkedRemoveSelectedSearchResults(CTypedPtrList<CPtrList, CSearchFile*> &selectedList);
	void	CancelActiveChunkedSearchOperation();
	void	ReloadList(const bool bSortCurrentList, const EListStateField LsfFlag);
	void	QueueDeferredReload(const bool bSortCurrentList, const EListStateField LsfFlag, UINT uDelayMs, bool bKeepPendingWhileInactive = false);
	void	QueuePossibleKnownRefresh(UINT uDelayMs);
	void	QueuePossibleKnownSoftRefresh();
	void	CancelPendingPossibleKnownProcessing(uint32 nSearchID);
	bool	ApplyPreparedPossibleKnownCaches(uint32 nSearchID);
	bool	HasPendingPossibleKnownProcessing(uint32 nSearchID) const;
	void	ApplyPossibleKnownQueryResult(UINT_PTR uParentToken, uint32 nSearchID, const uchar* pHash, const CString& strFileName, EMFileSize uFileSize,
		uint32 uMediaLengthSec, uint32 uAliasFingerprint, uint32 uRevision, uint32 uCandidateDataRevision, bool bReplaceRows, bool bRowsRequested, bool bHasMatches, bool bFinalResult, const SDownloadValidatorFuzzyQueryData& queryData,
		const std::vector<SSearchListRow>& rows);
	void	RebuildListedItemsMap();
	void	CollectSearchDownloadItems(uint32 nSearchID, bool bOnlyUnknown, CTypedPtrList<CPtrList, CSearchFile*> &downloadItems) const;
	void	CollectSelectedSearchFiles(CTypedPtrList<CPtrList, CSearchFile*> &selectedList) const;
	void	ClearListedItems(bool bClearSearchRows);
	void	RemoveCachedSearchRows(uint32 nSearchID);
	bool	IsPassiveRowIndex(int iItem) const;
	virtual DWORD_PTR GetVirtualItemData(int iItem) const override {
		return iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size() ? 0 : static_cast<DWORD_PTR>(iItem + 1); // Owner-data row data is a stable visible index, not a backend pointer
	}
	int		GetVirtualItemCount() const override { return static_cast<int>(m_ListedItemsVector.size()); }
	CObject* GetItemObject(int iIndex) const;
	virtual void OnOperationOverlayCancel() override;

	std::vector<SSearchListRow*> m_ListedItemsVector; // This vector contains visible view rows.
	typedef CMap<SSearchListRow*, SSearchListRow*, int, int&> CListedItemsMap;
	typedef CMap<CSearchFile*, CSearchFile*, int, int&> CSearchItemsMap;
	CListedItemsMap m_ListedItemsMap; // This map is used by list state restore.
	CSearchItemsMap m_SearchItemsMap; // This map resolves backend search files to visible rows.
	void	Localize();
	void	NoTabs()								{ m_nResultsID = 0; m_lListedItemsModelSequence = 0; }
	bool	IsListedModelCurrent(uint32 nSearchID) const;
	void	UpdateSearch(CSearchFile *toupdate);
	void	UpdateTabHeader(uint32 nSearchID, CString strClientHash, bool bUpdateAllSharedListTabs);
	EFileSizeFormat GetFileSizeFormat() const		{ return m_eFileSizeFormat; }
	void	SetFileSizeFormat(EFileSizeFormat eFormat);
	static int CALLBACK SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort); // Moved to public
	uint32	m_nResultsID; // Moved to public

protected:
	CMenuXP	m_SearchFileMenu;
	CMenuXP	m_PreviewMenu;
	CSearchList	*searchlist;
	CToolTipCtrlX m_tooltip;
	CImageList	m_ImageList;
	COLORREF	m_crSearchResultDownloading;
	COLORREF	m_crSearchResultDownloadStopped;
	COLORREF	m_crSearchResultKnown;
	COLORREF	m_crSearchResultSharing;
	COLORREF	m_crSearchResultCancelled;
	COLORREF	m_crPossibleKnownHeader;
	COLORREF	m_crShades[AVBLYSHADECOUNT];
	EFileSizeFormat m_eFileSizeFormat;
	bool m_bDeferredSearchReloadPending;
	bool m_bDeferredSearchReloadSort;
	bool m_bDeferredSearchReloadKeepPendingWhileInactive;
	EListStateField m_eDeferredSearchReloadState;
	LONG m_lListedItemsModelSequence;
	uint32 m_uPossibleKnownRevision;
	uint32 m_uPossibleKnownCandidateDataRevision;

	COLORREF GetSearchItemColor(const CSearchFile* src) const;
	COLORREF GetPossibleKnownItemColor(const SSearchListRow* pRow) const;
	bool	IsComplete(const CSearchFile *pFile, UINT uSources) const;
	void	MarkListedModelCurrent();
	CString GetCompleteSourcesDisplayString(const CSearchFile *pFile, UINT uSources, bool *pbComplete = NULL) const;
	void	ExpandCollapseItem(int iItem, int iAction);
	void	HideSources(CSearchFile *toCollapse);
	void	SetStyle();
	void	SetHighlightColors();
	void	SetAllIcons();
	CString	FormatFileSize(ULONGLONG ullFileSize) const;
	CString GetItemDisplayText(const CSearchFile *src, int iSubItem) const;
	CString GetListedItemDisplayText(int iItem, int iSubItem) const;
	CString GetPossibleKnownDisplayText(const SSearchListRow* pRow, int iSubItem) const;
	void SortListedItemsRaw();
	int CompareSearchFilesRaw(const CSearchFile *item1, const CSearchFile *item2, LPARAM lParamSort) const;
	bool GroupListedItemsByBottomCandidates();
	bool BuildSearchInfoTipText(int iItem, CString& strText) const;
	SSearchListRow* ResolveRowByIndex(int iItem) const;
	CSearchFile* ResolveSearchFileByRowIndex(int iItem) const;
	SSearchListRow* GetOrCreateSearchRow(CSearchFile* pSearchFile);
	void RemoveRowsFromSavedStates(const std::vector<SSearchListRow*>& rows);
	void RemovePossibleKnownRowsFromSavedStates();
	void ClearPossibleKnownRows();
	void RemoveCachedSearchRowsForFile(const CSearchFile* pFile);
	bool IsPossibleKnownFeatureEnabled() const;
	bool IsPossibleKnownFeatureActive() const;
	bool HasPossibleKnownMatches(CSearchFile* pParent);
	bool HasCachedPossibleKnownMatches(const CSearchFile* pParent) const;
	bool CanExpandSearchParent(CSearchFile* pParent);
	void RebuildPossibleKnownRows();
	void AppendPossibleKnownRows(CSearchFile* pParent, std::vector<SSearchListRow*>& rows);
	void DrawPossibleKnownRow(CDC& dc, LPDRAWITEMSTRUCT lpDrawItemStruct, const SSearchListRow* pRow, BOOL bCtrlFocused);
	bool IsRowDescendantOfParent(const SSearchListRow* pRow, const CSearchFile* pParent) const;
	bool HasSelectedPassiveRows() const;
	bool CollectSelectedPossibleKnownRows(std::vector<const SSearchListRow*>& rows) const;
	bool ExecutePossibleKnownCancelCommand(UINT uCommand);
	bool ExecutePossibleKnownCopyCommand(UINT uCommand, const std::vector<const SSearchListRow*>& rows);
	bool ExecutePossibleKnownSearchRelatedCommand(const std::vector<const SSearchListRow*>& rows);
	bool ExecutePossibleKnownWebServiceCommand(UINT uCommand, const std::vector<const SSearchListRow*>& rows);
	bool ExecutePossibleKnownPreviewCommand(UINT uCommand, const std::vector<const SSearchListRow*>& rows);
	bool ResolvePossibleKnownSharedFilePath(const SSearchListRow* pRow, CString& strFilePath) const;
	void ClearPossibleKnownAvailabilityQueue();
	bool AppendSameHashPossibleKnownRow(CSearchFile* pParent, std::vector<SSearchListRow>& rows) const;
	void QueuePossibleKnownAvailability(CSearchFile* pParent, bool bLoadRows = false, bool bReplaceRows = false);
	void ProcessPossibleKnownAvailability();
	bool ShouldShowSearchItemInList(const CSearchFile *pSearchFile) const;
	void BuildVisibleSearchItems(const CTypedPtrList<CPtrList, CSearchFile*> &sourceList, std::vector<SSearchListRow*> &visibleItems);
	const bool	IsFilteredOut(const CSearchFile *pSearchFile) const;
	static CString GetKnownTypeStr(const CSearchFile* src);
	std::map<CSearchFile*, SSearchListRow*> m_SearchRows;
	std::vector<SSearchListRow*> m_PossibleKnownRows;
	struct SPossibleKnownCacheEntry
	{
		bool bAvailabilityKnown = false;
		bool bHasMatches = false;
		bool bAvailabilityPending = false;
		bool bRowsLoaded = false;
		bool bRowsPending = false;
		bool bReplaceRowsPending = false;
		bool bPendingHasMatches = false;
		uint32 uRevision = 0;
		uint32 uCandidateDataRevision = 0;
		uint32 uSourceMediaLengthSec = 0;
		uint32 uAliasFingerprint = 0;
		uint32 uPendingCandidateDataRevision = 0;
		std::vector<SSearchListRow> rows;
		std::vector<SSearchListRow> pendingRows;
	};
	std::map<CSearchFile*, SPossibleKnownCacheEntry> m_PossibleKnownCache;
	bool ImportPossibleKnownCache(CSearchFile* pParent, SPossibleKnownCacheEntry& cacheEntry, bool bForce = false);
	void StorePossibleKnownCache(CSearchFile* pParent, const SPossibleKnownCacheEntry& cacheEntry);
	struct SPossibleKnownAvailabilityItem
	{
		CSearchFile* pParent;
		uint32 nSearchID;
		CString strFileName;
		std::vector<CString> astrFileNames;
		EMFileSize uFileSize;
		uint32 uMediaLengthSec;
		uint32 uAliasFingerprint;
		uchar ucHash[MDX_DIGEST_SIZE];
		bool bLoadRows;
		bool bReplaceRows;
		uint32 uRevision;
		uint32 uCandidateDataRevision;
	};
	std::vector<SPossibleKnownAvailabilityItem> m_PossibleKnownAvailabilityQueue;
	size_t m_uNextPossibleKnownAvailability = 0;
	struct SChunkedSearchRemoveItem
	{
		SChunkedSearchRemoveItem();
		uint32 nSearchID;
		uchar abyFileHash[16];
		bool bChild;
		CString strFileName;
	};
	std::vector<SChunkedSearchRemoveItem> m_vecChunkedSearchRemoveItems;
	INT_PTR m_iNextChunkedSearchRemoveItem;
	UINT m_uChunkedSearchRemoveProcessed;
	UINT m_uChunkedSearchRemoveStale;
	UINT m_uChunkedSearchRemoveFailed;
	DWORD m_dwChunkedSearchRemoveStartedTick;
	DWORD m_dwChunkedSearchRemoveLastProgressTick;
	bool m_bChunkedSearchRemoveActive;

	bool BuildChunkedSearchRemoveItem(const CSearchFile *pFile, SChunkedSearchRemoveItem &item) const;
	bool ResolveChunkedSearchRemoveItem(const SChunkedSearchRemoveItem &item, SSearchResultId &id, CSearchFile *&pFile) const;
	void ClearChunkedSearchRemoveItems(bool bReloadVisibleList);
	void ProcessChunkedSearchRemoveItems();
	void FinishChunkedSearchRemoveItems(bool bAborted);

	virtual bool UsePersistentInfoTips() const override { return true; }
	virtual bool ShouldShowPersistentInfoTip(const SPersistentInfoTipContext& context) override;
	virtual bool GetPersistentInfoTipText(const SPersistentInfoTipContext& context, CString& strText) override;

	void	DrawSourceParent(CDC *dc, int nColumn, LPRECT lpRect, UINT uDrawTextAlignment, const CSearchFile *src);
	void	DrawSourceChild(CDC *dc, int nColumn, LPRECT lpRect, UINT uDrawTextAlignment, const CSearchFile *src);

	static int Compare(const CSearchFile *item1, const CSearchFile *item2, LPARAM lParamSort, bool bSortMod);
	static int CompareChild(const CSearchFile *file1, const CSearchFile *file2, LPARAM lParamSort);

	virtual BOOL OnCommand(WPARAM wParam, LPARAM);
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
	virtual void RefreshThemeColors() override;

	static LPARAM	m_pSortParam;
	int 			m_iDataSize;
	const static bool SortFunc(const CSearchFile* first, const CSearchFile* second);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnContextMenu(CWnd*, CPoint point);
	afx_msg void OnDestroy();
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnDeleteAllItems(LPNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnKeyDown(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnNmClick(LPNMHDR pNMHDR, LRESULT*);
	afx_msg void OnNmDblClk(LPNMHDR, LRESULT*);
	afx_msg void OnSysColorChange();
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
};
