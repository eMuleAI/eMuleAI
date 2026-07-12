#pragma once
#include "Preferences.h"
#include "resource.h"
#include "eMuleAI/DarkMode.h"
#include "ToolTipCtrlX.h"

//a value that's not a multiple of 4 and uncommon
#define MLC_MAGIC 0xFEEBDEEF

class CIni;
class CMemoryDC;
class COperationOverlayWnd;
class CMuleListCtrl;

enum EListStateField {
	LSF_NONE = 0x00,
	LSF_SELECTION = 0x01,
	LSF_SCROLL = 0x02,
	LSF_SORT = 0x04,
	LSF_ALL = 0x07
};

template<typename TObject>
class CListState
{
public:
	CArray<TObject*, TObject*> m_aSelectedItems;	// Selected items
	uint32  m_nSortItem = 0;						// Sort column index
	int m_nScrollPosition = -1;					// Vertical scroll position
	int m_nHScrollPosition = 0;						// Horizontal scroll position
	CArray<TObject*, TObject*> m_aVisibleItems;		// Visible items in row order
	TObject* m_pFocusedItem = nullptr;				// Focused item
	TObject* m_pSelectionMarkItem = nullptr;		// Selection anchor item
	bool    m_bAllItemsSelected = false;			// True when every item was selected
	bool    m_bAtBottom = false;					// Last item was visible
	bool    m_bSortAscending = true;				// Sort direction
	CList<int, int> m_liSortHistory;				// Multisort history
	uint32  m_nValidFields = LSF_ALL;				// Saved fields bitmask
};

// ---------------------------------------------------------------------------
// Generic helper which stores and restores list-view state for every list ID
// ---------------------------------------------------------------------------
template<class Derived, class ItemT>
class CListStateTemplate
{
public:
	typedef CMap<int, int, CListState<ItemT>*, CListState<ItemT>*> CListStatesMap;
	CListStatesMap m_mapListStates; // Map to store list states by ID

	// Use this for incremental bulk mutations. Model-first bulk flows can usually defer UI updates to one ReloadList instead.
	void BeginListStateBatch(uint32 nListID, uint32 nFlags)
	{
		if (m_uListStateBatchDepth > 0) {
			ASSERT(m_uListStateBatchListID == nListID);
			++m_uListStateBatchDepth;
			return;
		}

		m_uListStateBatchDepth = 1;
		m_uListStateBatchListID = nListID;
		m_uListStateBatchSavedFlags = nFlags;
		SaveListStateInternal(nListID, nFlags);
	}

	void EndListStateBatch(uint32 nListID, uint32 nFlags, bool bKeepState)
	{
		if (m_uListStateBatchDepth == 0)
			return;

		ASSERT(m_uListStateBatchListID == nListID);
		if (--m_uListStateBatchDepth > 0)
			return;

		const uint32 uRestoreFlags = (nFlags == LSF_ALL) ? m_uListStateBatchSavedFlags : nFlags;
		m_uListStateBatchListID = 0;
		m_uListStateBatchSavedFlags = LSF_NONE;

		if (uRestoreFlags != LSF_NONE)
			RestoreListStateInternal(nListID, uRestoreFlags, bKeepState);
	}

	bool IsListStateBatchActive(uint32 nListID) const
	{
		return m_uListStateBatchDepth > 0 && m_uListStateBatchListID == nListID;
	}

	// Destructor  delete every stored state once and empty the map.
	~CListStateTemplate()
	{
		// Delete all stored states
		for (POSITION pos = m_mapListStates.GetStartPosition(); pos != NULL; ) {
			int key;
			CListState<ItemT>* state;
			m_mapListStates.GetNextAssoc(pos, key, state);
			delete state;
		}

		m_mapListStates.RemoveAll(); // Clear the map
	}

	// Save current selection / sort / scroll position for the given list ID.
	void SaveListState(uint32 nListID, uint32 nFlags)
	{
		if (IsListStateBatchActive(nListID))
			return;

		SaveListStateInternal(nListID, nFlags);
	}

	// Restore a previously saved list state.
	void RestoreListState(uint32 nListID, uint32 nFlags, bool bKeepState)
	{
		if (IsListStateBatchActive(nListID))
			return;

		RestoreListStateInternal(nListID, nFlags, bKeepState);
	}

private:
	static uint32 NormalizeListStateFlags(uint32 nFlags)
	{
		if (nFlags != LSF_ALL && (nFlags & LSF_SELECTION) != 0)
			nFlags |= LSF_SCROLL;
		return nFlags;
	}

	static bool IsItemCurrentlyVisible(Derived& self, int iItem)
	{
		if (iItem < 0 || iItem >= self.GetItemCount())
			return false;

		CRect rcClient;
		CRect rcItem;
		self.GetClientRect(&rcClient);
		return self.GetItemRect(iItem, &rcItem, LVIR_BOUNDS)
			&& rcItem.bottom > rcClient.top
			&& rcItem.top < rcClient.bottom;
	}

	static int ResolveSavedBottomAnchor(Derived& self, const CListState<ItemT>* pState)
	{
		if (pState == nullptr || self.GetItemCount() <= 0)
			return -1;

		int iBottomAnchor = -1;
		for (INT_PTR i = pState->m_aVisibleItems.GetCount(); i > 0; --i) {
			if (self.m_ListedItemsMap.Lookup(pState->m_aVisibleItems[i - 1], iBottomAnchor))
				break;
		}
		return iBottomAnchor >= 0 ? iBottomAnchor : self.GetItemCount() - 1;
	}

	static int ResolveSavedTopIndex(Derived& self, const CListState<ItemT>* pState)
	{
		const int iItemCount = self.GetItemCount();
		if (pState == nullptr || iItemCount <= 0)
			return -1;
		if (pState->m_nScrollPosition <= 0)
			return 0;

		for (INT_PTR i = 0; i < pState->m_aVisibleItems.GetCount(); ++i) {
			int iVisibleItemIndex = -1;
			if (self.m_ListedItemsMap.Lookup(pState->m_aVisibleItems[i], iVisibleItemIndex))
				return iVisibleItemIndex;
		}
		return max(0, min(pState->m_nScrollPosition, iItemCount - 1));
	}

	static bool ShouldLockWindowUpdateForRestore(Derived& self, const CListState<ItemT>* pState, uint32 uEffectiveFlags)
	{
		if ((uEffectiveFlags & LSF_SCROLL) == 0 || pState == nullptr || !::IsWindow(self.GetSafeHwnd()))
			return false;

		if (self.GetItemCount() > 0) {
			if (pState->m_bAtBottom) {
				const int iBottomAnchor = ResolveSavedBottomAnchor(self, pState);
				if (iBottomAnchor >= 0 && !IsItemCurrentlyVisible(self, iBottomAnchor))
					return true;
			} else {
				const int iWantedTop = ResolveSavedTopIndex(self, pState);
				if (iWantedTop >= 0 && self.GetTopIndex() != iWantedTop)
					return true;
			}
		}

		SCROLLINFO siHorz = { sizeof(siHorz) };
		if (!self.GetScrollInfo(SB_HORZ, &siHorz, SIF_POS | SIF_RANGE | SIF_PAGE))
			return false;

		int iMaxHPos = siHorz.nMax;
		if (siHorz.nPage > 0)
			iMaxHPos = max(siHorz.nMin, siHorz.nMax - static_cast<int>(siHorz.nPage) + 1);
		const int iWantHPos = max(siHorz.nMin, min(pState->m_nHScrollPosition, iMaxHPos));
		return siHorz.nPos != iWantHPos;
	}

	void SaveListStateInternal(uint32 nListID, uint32 nFlags)
	{
		nFlags = NormalizeListStateFlags(nFlags);
		if (!nFlags)
			return; // Nothing to save

		// if there is an older state for this ID, throw it away first
		CListState<ItemT>* pOld;
		if (m_mapListStates.Lookup(static_cast<int>(nListID), pOld)) {
			delete pOld;
			m_mapListStates.RemoveKey(static_cast<int>(nListID));
		}

		auto& self = static_cast<Derived&>(*this); // Get the derived class reference
		auto* pCur = new CListState<ItemT>; // Create a new state object
		pCur->m_nValidFields = nFlags; // Set the valid fields bitmask

		// Save the current selection
		if (nFlags & LSF_SELECTION) {
			const int iItemCount = self.GetItemCount();
			const int iSelectedCount = self.GetSelectedCount();
			if (iItemCount > 0 && iSelectedCount == iItemCount) {
				pCur->m_bAllItemsSelected = true;
				for (int idx = 0; idx < iItemCount && idx < static_cast<int>(self.m_ListedItemsVector.size()); ++idx)
					pCur->m_aSelectedItems.Add(self.m_ListedItemsVector[idx]);
			} else {
				for (POSITION pos = self.GetFirstSelectedItemPosition(); pos != NULL;) {
					int idx = self.GetNextSelectedItem(pos);
					if (idx >= 0 && idx < static_cast<int>(self.m_ListedItemsVector.size()))
						pCur->m_aSelectedItems.Add(self.m_ListedItemsVector[idx]);
				}
			}

			const int iFocusedItem = self.GetNextItem(-1, LVNI_FOCUSED);
			if (iFocusedItem >= 0 && iFocusedItem < static_cast<int>(self.m_ListedItemsVector.size()))
				pCur->m_pFocusedItem = self.m_ListedItemsVector[iFocusedItem];

			const int iSelectionMark = self.GetSelectionMark();
			if (iSelectionMark >= 0 && iSelectionMark < static_cast<int>(self.m_ListedItemsVector.size()))
				pCur->m_pSelectionMarkItem = self.m_ListedItemsVector[iSelectionMark];
		}

		// Save the current sort order
		if (nFlags & LSF_SORT) {
			pCur->m_nSortItem = self.GetSortItem();
			pCur->m_bSortAscending = self.GetSortAscending();
			for (POSITION pos = self.m_liSortHistory.GetHeadPosition(); pos != NULL;)
				pCur->m_liSortHistory.AddTail(self.m_liSortHistory.GetNext(pos));
		}

		// Save the current scroll position
		if (nFlags & LSF_SCROLL) {
			const int iTopIndex = self.GetTopIndex();
			pCur->m_nScrollPosition = iTopIndex;
			const int iItemCount = self.GetItemCount();
			const int iRowsPerPage = self.GetCountPerPage();
			pCur->m_bAtBottom = self.IsAtBottom();
			const int iLastVisibleIndex = min(iItemCount - 1, iTopIndex + max(1, iRowsPerPage));
			for (int i = iTopIndex; i >= 0 && i <= iLastVisibleIndex && i < static_cast<int>(self.m_ListedItemsVector.size()); ++i)
				pCur->m_aVisibleItems.Add(self.m_ListedItemsVector[i]);

			SCROLLINFO siHorz = { sizeof(siHorz) };
			if (self.GetScrollInfo(SB_HORZ, &siHorz, SIF_POS))
				pCur->m_nHScrollPosition = siHorz.nPos;
		}

		m_mapListStates.SetAt(static_cast<int>(nListID), pCur); // Store the state in the map
	}

	void RestoreListStateInternal(uint32 nListID, uint32 nFlags, bool bKeepState)
	{
		if (!nFlags)
			return;  // Nothing to restore

		auto& self = static_cast<Derived&>(*this); // Get the derived class reference

		CListState<ItemT>* pState = nullptr; // Pointer to the state to restore
		if (!m_mapListStates.Lookup(static_cast<int>(nListID), pState) || !pState)
			return; // Nothing stored for this ID

		const uint32 uEffectiveFlags = (nFlags == LSF_ALL) ? pState->m_nValidFields : NormalizeListStateFlags(nFlags); // Effective flags to restore

		// Restore sort order
		if (uEffectiveFlags & LSF_SORT) {
			self.m_liSortHistory.RemoveAll();
			for (POSITION pos = pState->m_liSortHistory.GetHeadPosition(); pos != NULL; )
				self.m_liSortHistory.AddTail(pState->m_liSortHistory.GetNext(pos));

			self.SetSortArrow(pState->m_nSortItem, pState->m_bSortAscending);
			self.SortItems(self.SortProc, MAKELONG(pState->m_nSortItem, !pState->m_bSortAscending));
		}

		// Restore selection before the viewport so focus changes cannot override the final scroll position.
		if (uEffectiveFlags & LSF_SELECTION) {
			self.SetItemState(-1, 0, LVIS_FOCUSED | LVIS_SELECTED); // Clear all previous selections

			bool bAllSavedItemsStillVisible = pState->m_bAllItemsSelected
				&& pState->m_aSelectedItems.GetCount() == static_cast<INT_PTR>(self.GetItemCount());
			for (INT_PTR i = 0; bAllSavedItemsStillVisible && i < pState->m_aSelectedItems.GetCount(); ++i) {
				int idx;
				if (!self.m_ListedItemsMap.Lookup(pState->m_aSelectedItems[i], idx))
					bAllSavedItemsStillVisible = false;
			}

			if (bAllSavedItemsStillVisible && self.GetItemCount() > 0)
				self.SetItemState(-1, LVIS_SELECTED, LVIS_SELECTED);
			else {
				// Restore selected items
				for (INT_PTR i = 0; i < pState->m_aSelectedItems.GetCount(); ++i) {
					int idx;
					if (self.m_ListedItemsMap.Lookup(pState->m_aSelectedItems[i], idx))
						self.SetItemState(idx, LVIS_SELECTED, LVIS_SELECTED);
				}
			}

			int iFocusedIndex = -1;
			if (pState->m_pFocusedItem != nullptr && self.m_ListedItemsMap.Lookup(pState->m_pFocusedItem, iFocusedIndex))
				self.SetItemState(iFocusedIndex, LVIS_FOCUSED, LVIS_FOCUSED);

			int iSelectionMark = -1;
			if (pState->m_pSelectionMarkItem != nullptr)
				self.m_ListedItemsMap.Lookup(pState->m_pSelectionMarkItem, iSelectionMark);
			self.SetSelectionMark(iSelectionMark);
		}

		// List-view scroll messages move visible pixels even while WM_SETREDRAW is disabled.
		// Lock only when the saved viewport actually needs to move.
		const bool bWindowUpdateLocked = ShouldLockWindowUpdateForRestore(self, pState, uEffectiveFlags)
			&& ::LockWindowUpdate(self.GetSafeHwnd()) != FALSE;

		// Restore scroll position
		if (uEffectiveFlags & LSF_SCROLL) {
			if (self.GetItemCount() > 0) {
				if (pState->m_bAtBottom) {
					const int iBottomAnchor = ResolveSavedBottomAnchor(self, pState);
					if (iBottomAnchor >= 0 && !IsItemCurrentlyVisible(self, iBottomAnchor))
						self.EnsureVisible(iBottomAnchor, FALSE);
				} else {
					const int iWantedTop = ResolveSavedTopIndex(self, pState);
					if (iWantedTop >= 0 && self.GetTopIndex() != iWantedTop)
						self.ScrollToTopIndex(iWantedTop);
				}
			}
		}

		if (uEffectiveFlags & LSF_SCROLL) {
			// Owner-data list rebuilds may reset the horizontal origin even if columns stay unchanged.
			SCROLLINFO siHorz = { sizeof(siHorz) };
			if (self.GetScrollInfo(SB_HORZ, &siHorz, SIF_POS | SIF_RANGE | SIF_PAGE)) {
				int iWantHPos = pState->m_nHScrollPosition;
				int iMaxHPos = siHorz.nMax;
				if (siHorz.nPage > 0)
					iMaxHPos = max(siHorz.nMin, siHorz.nMax - static_cast<int>(siHorz.nPage) + 1);
				iWantHPos = max(siHorz.nMin, min(iWantHPos, iMaxHPos));
				if (siHorz.nPos != iWantHPos)
					self.Scroll(CSize(iWantHPos - siHorz.nPos, 0));
			}
		}

		if (bWindowUpdateLocked)
			::LockWindowUpdate(NULL);

		// If we are not keeping the state, delete it
		if (!bKeepState) {
			delete pState;
			m_mapListStates.RemoveKey(static_cast<int>(nListID));
		}
	}

	uint32 m_uListStateBatchDepth = 0;
	uint32 m_uListStateBatchListID = 0;
	uint32 m_uListStateBatchSavedFlags = LSF_NONE;
};

///////////////////////////////////////////////////////////////////////////////
// CMuleListCtrl

#define MLC_DT_TEXT (DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS)

#define DFLT_FILENAME_COL_WIDTH		260
#define DFLT_FILETYPE_COL_WIDTH		 60
#define	DFLT_CLIENTNAME_COL_WIDTH	150
#define	DFLT_CLIENTSOFT_COL_WIDTH	100
#define	DFLT_SIZE_COL_WIDTH			 65
#define	DFLT_HASH_COL_WIDTH			220
#define	DFLT_DATARATE_COL_WIDTH		 65
#define	DFLT_PRIORITY_COL_WIDTH		 60
#define	DFLT_PARTSTATUS_COL_WIDTH	170
#define	DFLT_ARTIST_COL_WIDTH		100
#define	DFLT_ALBUM_COL_WIDTH		100
#define	DFLT_TITLE_COL_WIDTH		100
#define	DFLT_LENGTH_COL_WIDTH		 50
#define	DFLT_BITRATE_COL_WIDTH		 65
#define	DFLT_CODEC_COL_WIDTH		 50
#define	DFLT_FOLDER_COL_WIDTH		260

class CMuleListCtrl : public CListCtrl
{
	DECLARE_DYNAMIC(CMuleListCtrl)
	friend class COperationOverlayWnd;
	friend class CToolTipCtrlX;

public:
	static constexpr UINT MULELIST_BULK_OPERATION_OVERLAY_MIN_ITEM_COUNT = 3;
	static constexpr DWORD MULELIST_BULK_OPERATION_OVERLAY_DELAY_MS = 1000;

	CMuleListCtrl(PFNLVCOMPARE pfnCompare = SortProc, LPARAM iParamSort = 0);
	virtual	~CMuleListCtrl();

	// Default sort proc, this does nothing
	static int CALLBACK SortProc(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort);

	// Sets the list name, used for settings in "preferences.ini"
	void SetPrefsKey(LPCTSTR lpszName)			{ m_Name = lpszName; };

	// Keep the cached owner-draw colors aligned with the underlying list-view state.
	virtual void RefreshThemeColors();

	// Save to preferences
	void SaveSettings(const bool bCalledBySaveAppState = false);

	// Load from preferences
	void LoadSettings();

	DWORD SetExtendedStyle(DWORD dwNewStyle)	{ return CListCtrl::SetExtendedStyle((dwNewStyle | LVS_EX_HEADERDRAGDROP) & ~LVS_EX_INFOTIP); };

	// Hide the column
	void HideColumn(int iColumn);

	// Unhide the column
	void ShowColumn(int iColumn);

	// Check to see if the column is hidden
	bool IsColumnHidden(int iColumn) const	{ return iColumn >= 1 && iColumn < m_iColumnsTracked && m_aColumns[iColumn].bHidden; }

	// Get the correct column width even if column is hidden
	int GetColumnWidth(int iColumn) const
	{
		if (iColumn < 0 || iColumn >= m_iColumnsTracked)
			return 0;
		if (m_aColumns[iColumn].bHidden)
			return m_aColumns[iColumn].iWidth;
		return CListCtrl::GetColumnWidth(iColumn);
	}

	// Get the column width and the alignment flags for 'DrawText'
	int GetColumnWidth(int iColumn, UINT &uDrawTextAlignment) const
	{
		if (iColumn < 0 || iColumn >= m_iColumnsTracked) {
			uDrawTextAlignment = DT_LEFT;
			return 0;
		}
		ASSERT(!m_aColumns[iColumn].bHidden);
		LVCOLUMN lvcol;
		lvcol.mask = LVCF_FMT | LVCF_WIDTH;
		if (!CListCtrl::GetColumn(iColumn, &lvcol)) {
			uDrawTextAlignment = DT_LEFT;
			return 0;
		}
		switch (lvcol.fmt & LVCFMT_JUSTIFYMASK) {
		default:
		case LVCFMT_LEFT:
			uDrawTextAlignment = DT_LEFT;
			break;
		case LVCFMT_RIGHT:
			uDrawTextAlignment = DT_RIGHT;
			break;
		case LVCFMT_CENTER:
			uDrawTextAlignment = DT_CENTER;
		}
		return lvcol.cx;
	}

	// Call SetRedraw to allow changes to be redrawn or to prevent changes from being redrawn.
	void SetRedraw(bool bRedraw = true)
	{
		if (bRedraw) {
			if (m_iRedrawCount > 0 && --m_iRedrawCount == 0)
				CListCtrl::SetRedraw(TRUE);
		} else {
			if (m_iRedrawCount++ == 0)
				CListCtrl::SetRedraw(FALSE);
		}
	}

	// Sorts the list
	BOOL SortItems(PFNLVCOMPARE pfnCompare, DWORD dwData)
	{
		ASSERT(::IsWindow(m_hWnd));
		if (!::IsWindow(m_hWnd))
			return FALSE;

		return (BOOL)::SendMessage(m_hWnd, LVM_SORTITEMS, static_cast<WPARAM>(dwData), (LPARAM)pfnCompare);
	}

	// Sorts the list
	BOOL SortItems(DWORD dwData)
	{
		return SortItems(m_SortProc, dwData);
	}

	// Sets the sorting procedure
	void SetSortProcedure(PFNLVCOMPARE funcSortProcedure)
	{
		m_SortProc = funcSortProcedure;
	}

	// Gets the sorting procedure
	PFNLVCOMPARE GetSortProcedure()
	{
		return m_SortProc;
	}

	// Retrieves the data (lParam) associated with a particular item.
	DWORD_PTR GetItemData(int iItem);

	// Retrieves the number of items in the control.
	int GetItemCount() const
	{
		if (GetStyle() & LVS_OWNERDATA)
			return GetVirtualItemCount();
		return static_cast<int>(m_Params.GetCount());
	};

	// Override these functions if you use LVS_OWNERDATA for virtual lists
	virtual void ReloadList(const bool bSortCurrentList, const EListStateField LsfFlag) { }
	virtual DWORD_PTR GetVirtualItemData(int iItem) const { return 0; }
	virtual int GetVirtualItemCount() const { return 0; }
	
	// Override this function to handle post-update sorting for maintaining sort order
	virtual bool ShouldMaintainSortOrderOnUpdate() const { return GetSortItem() != -1; }
	virtual void MaintainSortOrderAfterUpdate();
	void MaintainSortOrderAfterThrottledUpdate();

	enum ArrowType
	{
		arrowDown = IDB_DOWN,
		arrowUp = IDB_UP,
		arrowDoubleDown = IDB_DOWN2X,
		arrowDoubleUp = IDB_UP2X
	};

	int	GetSortType(ArrowType at);
	ArrowType GetArrowType(int iat);
	int GetSortItem() const						{ return m_iCurrentSortItem; }
	ArrowType GetSortArrowType() const { return m_atSortArrow; }

	const uint32 GetMaxSortHistory() const { return m_iMaxSortHistory; }
	void SetMaxSortHistory(const uint32 in) { m_iMaxSortHistory = in; }

	bool GetSortAscending() const				{ return m_atSortArrow == arrowUp || m_atSortArrow == arrowDoubleUp; }
	bool GetSortSecondValue() const				{ return m_atSortArrow == arrowDoubleDown || m_atSortArrow == arrowDoubleUp; }
	// Places a sort arrow in a column
	void SetSortArrow(int iColumn, ArrowType atType);
	void SetSortArrow()							{ SetSortArrow(m_iCurrentSortItem, m_atSortArrow); }
	void SetSortArrow(int iColumn, bool bAscending) { SetSortArrow(iColumn, bAscending ? arrowUp : arrowDown); }
	LPARAM GetNextSortOrder(LPARAM iCurrentSortOrder) const;
	void UpdateSortHistory(LPARAM dwNewOrder);

	// General purpose listview find dialog+functions (optional)
	void	SetGeneralPurposeFind(bool bEnable, bool bCanSearchInAllColumns = true)
	{
		m_bGeneralPurposeFind = bEnable;
		m_bCanSearchInAllColumns = bCanSearchInAllColumns;
	}
	void	DoFind(int iStartItem, int iDirection /*1=down, 0 = up*/, BOOL bShowError);
	void	DoFindNext(BOOL bShowError);

	enum EUpdateMode
	{
		lazy,
		direct,
		none
	};
	EUpdateMode SetUpdateMode(EUpdateMode eUpdateMode);
	void BeginBulkInsert();
	void EndBulkInsert();
	bool IsBulkInsertActive() const				{ return m_uBulkInsertDepth > 0; }
	void SetAutoSizeWidth(int iAutoSizeWidth)	{ m_iAutoSizeWidth = iAutoSizeWidth; };

	int InsertColumn(int nCol, LPCTSTR lpszColumnHeading, int nFormat = LVCFMT_LEFT, int nWidth = -1, int nSubItem = -1, bool bHiddenByDefault = false);


	struct SVisibleItemRange
	{
		SVisibleItemRange() : m_iFirst(-1), m_iLast(-1) {}
		SVisibleItemRange(int iFirst, int iLast) : m_iFirst(iFirst), m_iLast(iLast) {}
		bool IsValid() const { return m_iFirst >= 0 && m_iLast >= m_iFirst; }
		int m_iFirst;
		int m_iLast;
	};

	struct SAsyncRedrawRange
	{
		SAsyncRedrawRange() : m_bFull(false), m_iFirst(-1), m_iLast(-1) {}
		void Clear() { m_bFull = false; m_iFirst = -1; m_iLast = -1; }
		void MarkFull() { m_bFull = true; m_iFirst = -1; m_iLast = -1; }
		void Merge(int iFirst, int iLast);
		bool m_bFull;
		int m_iFirst;
		int m_iLast;
	};


	HIMAGELIST ApplyImageList(HIMAGELIST himl);
	void BeginListModelGeneration(LONG lModelGeneration, bool bClearRows);
	static CString MakeListSortKey(const CString& strSortText, bool bUndefined = false);
	static int CompareListSortKeys(const CString& strSortKey1, const CString& strSortKey2);
	static int ApplyListSortDirection(int iSortResult, bool bDescending);
	SVisibleItemRange GetVisibleItemRange() const;
	bool IsAtBottom() const;
	void ScrollToTopIndex(int iTopIndex);
	void SetItemCountAndKeepPageFilled(size_t itemCount, DWORD dwFlags);
	bool IsItemIndexVisible(int iIndex) const;
	bool GetNextBackgroundItemRange(int& iNextIndex, int iMaxIndex, int iRowsPerSlice, int& iFirst, int& iLast) const;
	void RequestRowRedrawAsync(int iStart, int iEnd);
	void RequestFullRedrawAsync();
	void ShutdownUpdateThread();
	void AutoSelectItem();
	void SetSkinKey(LPCTSTR pszKey)			{ m_strSkinKey = pszKey; }
	const CString& GetSkinKey() const		{ return m_strSkinKey; }

	void ShowOperationOverlay(const CString& strTitle, const CString& strDetail, UINT uDone, UINT uTotal, bool bCanCancel, const CString& strCancelText);
	void UpdateOperationOverlay(const CString& strTitle, const CString& strDetail, UINT uDone, UINT uTotal, bool bCanCancel = true);
	void HideOperationOverlay();
	bool IsOperationOverlayVisible() const;
	void SetOperationOverlaySuppressed(bool bSuppressed);
	bool IsOperationOverlaySuppressed() const;

	virtual CObject* GetItemObject(int iIndex) const
	{
		// Cast away constness so we can call the non const GetItemData
		DWORD_PTR dwData = const_cast<CMuleListCtrl*>(this)->GetItemData(iIndex);
		return reinterpret_cast<CObject*>(dwData);
	}

protected:
	virtual void PreSubclassWindow();
	virtual BOOL OnWndMsg(UINT message, WPARAM wParam, LPARAM lParam, LRESULT *pResult);
	virtual BOOL OnChildNotify(UINT message, WPARAM wParam, LPARAM lParam, LRESULT *pResult);
	virtual BOOL PreTranslateMessage(MSG *pMsg);

	struct SPersistentInfoTipContext
	{
		int iItem = -1;
		int iSubItem = -1;
		DWORD_PTR dwItemKey = 0;
		bool bExplicitTip = false;
		CRect rcItem = CRect(0, 0, 0, 0);
		CRect rcHotArea = CRect(0, 0, 0, 0);
	};

	virtual bool UsePersistentInfoTips() const { return false; }
	virtual bool ShouldShowPersistentInfoTip(const SPersistentInfoTipContext& context);
	virtual bool GetPersistentInfoTipText(const SPersistentInfoTipContext& context, CString& strText);
	virtual bool GetDefaultPersistentInfoTipText(const SPersistentInfoTipContext& context, CString& strText);
	virtual int GetDefaultPersistentInfoTipExtraLeftPadding(const SPersistentInfoTipContext& context) const;
	virtual void OnOperationOverlayCancel();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
	afx_msg void MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct);
	afx_msg BOOL OnEraseBkgnd(CDC *pDC);
	void FillUnusedClientArea(CDC *pDC);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void OnPaint();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg void OnSysColorChange();
	afx_msg void OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnDestroy();
	afx_msg LRESULT OnMouseHover(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnMouseLeave(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnAsyncInvalidate(WPARAM, LPARAM);
	afx_msg void OnTimer(UINT_PTR nIDEvent);

	enum { WM_MULELISTCTRL_INVALIDATE = WM_APP + 4021 };
	void RequestInvalidateAsync() const;
	bool TryGetPersistentInfoTipContext(CPoint point, SPersistentInfoTipContext& context);
	bool TryGetPersistentInfoTip(CPoint point, SPersistentInfoTipContext& context, CString* pstrText);
	void EnsureThemeAwareInfoTipCtrl();
	void EnsurePersistentInfoTipCtrl();
	void ShowPersistentInfoTip(const SPersistentInfoTipContext& context, const CString& strText);
	void HidePersistentInfoTip(bool bClearPendingState = false);
	void ResetPersistentInfoTipState();
	void ResetPendingPersistentInfoTip();
	void RefreshPersistentInfoTipFromPoint(CPoint point);
	void UpdatePersistentInfoTipTracking(CPoint point);
	bool IsSamePersistentInfoTipTarget(const SPersistentInfoTipContext& left, const SPersistentInfoTipContext& right) const;
	bool IsPersistentInfoTipContextCurrent(const SPersistentInfoTipContext& context) const;
	bool ResolvePersistentInfoTipHotArea(SPersistentInfoTipContext& context) const;
	bool IsPointWithinPersistentInfoTipHotArea(CPoint point, const SPersistentInfoTipContext& context) const;
	void StartPersistentInfoTipLeaveTimer();
	void StopPersistentInfoTipLeaveTimer();
	void EnsurePersistentInfoTipMouseLeaveTracking();
	bool IsCursorOverPersistentInfoTipTarget();
	bool IsCursorOverPersistentInfoTipWindow() const;
	bool ShouldKeepPersistentInfoTipVisibleOnMouseLeave();
	bool TryGetExplicitPersistentInfoTipText(const SPersistentInfoTipContext& context, CString& strText);
	bool TryGetPersistentInfoTipForContext(const SPersistentInfoTipContext& context, CString& strText, bool* pbExplicitTip = NULL);
	bool IsDefaultPersistentInfoTipTruncated(const SPersistentInfoTipContext& context, const CString& strText);
	bool GetDefaultPersistentInfoTipRect(const SPersistentInfoTipContext& context, CRect& rcText) const;
	bool TryGetInfoTipWindowText(HWND hWndInfoTip, CString& strText) const;
	bool HandleNativeInfoTipShow(HWND hWndInfoTip, LRESULT* pResult);
	bool ShouldSuppressDefaultInfoTip(LPNMLVGETINFOTIP pGetInfoTip);
	CPoint GetPersistentInfoTipScreenPosition(const SPersistentInfoTipContext& context) const;

	int UpdateLocation(int iItem);
	int MoveItem(int iOldIndex, int iNewIndex);
	void SetColors();
	void DrawFocusRect(CDC *pDC, LPCRECT rcItem, BOOL bItemFocused, BOOL bCtrlFocused, BOOL bItemSelected);
	void InitItemMemDC(CMemoryDC *dc, LPDRAWITEMSTRUCT lpDrawItemStruct, BOOL &bCtrlFocused);
	void LocaliseHeaderCtrl(const LPCTSTR* const uids, size_t cnt);
	void DrawOperationOverlay(CDC *pDC);
	bool GetOperationOverlayRect(CRect& rcOverlay) const;
	void EnsureOperationOverlayWindow();
	void DestroyOperationOverlayWindow();
	void UpdateOperationOverlayWindow();
	bool GetOperationOverlayCancelRect(CRect& rcCancel) const;
	void InvalidateOperationOverlay();
	void RedrawOperationOverlayArea(const CRect& rcInvalidate, bool bErase);
	bool IsOperationOverlayDelayEligible(UINT uTotal) const;
	bool IsOperationOverlayDelayElapsed(DWORD dwNow) const;
	void ApplyOperationOverlayVisibilityPolicy(bool bWasVisible, DWORD dwNow);
	void ScheduleOperationOverlayDelayTimer(DWORD dwNow);
	void CancelOperationOverlayDelayTimer();
	void TryShowDelayedOperationOverlay(DWORD dwNow);

	static inline bool HaveIntersection(const RECT &rc1, const RECT &rc2)
	{
		return (rc1.left   < rc2.right
			 && rc1.top    < rc2.bottom
			 && rc1.right  > rc2.left
			 && rc1.bottom > rc2.top);
	}

	CString         m_Name;
	PFNLVCOMPARE    m_SortProc;
	LPARAM          m_dwParamSort;
	CString			m_strSkinKey;
	COLORREF        m_crWindow;
	COLORREF        m_crWindowText;
	COLORREF        m_crWindowTextBk;
	COLORREF        m_crHighlight;
	COLORREF		m_crHighlightText;
	COLORREF		m_crGlow;
	COLORREF        m_crFocusLine;
	COLORREF        m_crNoHighlight;
	COLORREF        m_crNoFocusLine;
	NMLVCUSTOMDRAW  m_lvcd;
	BOOL            m_bCustomDraw;
	CImageList		m_imlHeaderCtrl;
	CList<LONG>		m_liSortHistory;
	UINT			m_uIDAccel;
	HACCEL			m_hAccel;
	EUpdateMode		m_eUpdateMode;
	int				m_iAutoSizeWidth;
	static const int sm_iIconOffset;
	static const int sm_iLabelOffset;
	static const int sm_iSubItemInset;


	// General purpose listview find dialog+functions (optional)
	CString m_strFindText;
	int m_iFindDirection;
	int m_iFindColumn;
	bool m_bGeneralPurposeFind;
	bool m_bCanSearchInAllColumns;
	bool m_bFindMatchCase;
	void OnFindStart();
	void OnFindNext();
	void OnFindPrev();
	CCriticalSection m_asyncRedrawLock;
	SAsyncRedrawRange m_asyncRedrawRange;
	bool m_bAsyncRedrawMessagePending;
	LONG m_lListModelGeneration;
	DWORD m_dwLastAsyncSortTick;

private:
	static int	IndexToOrder(CHeaderCtrl *pHeader, int iIndex);

	struct MULE_COLUMN
	{
		int iWidth;
		int iLocation;
		bool bHidden;
	};


	MULE_COLUMN *m_aColumns;
	int          m_iColumnsTracked;

	int GetHiddenColumnCount() const
	{
		int iHidden = 0;
		for (int i = m_iColumnsTracked; --i >= 0;)
			iHidden += static_cast<int>(m_aColumns[i].bHidden);
		return iHidden;
	}

	int		  m_iCurrentSortItem;
	ArrowType m_atSortArrow;

	int		  m_iRedrawCount;
	UINT	  m_uBulkInsertDepth;
	CList<DWORD_PTR, DWORD_PTR> m_Params;
	CToolTipCtrlX m_wndInfoTip;
	CToolTipCtrlX m_wndPersistentInfoTip;
	CString m_strPersistentInfoTipText;
	SPersistentInfoTipContext m_PersistentInfoTipContext;
	SPersistentInfoTipContext m_PendingInfoTipContext;
	bool m_bPersistentInfoTipVisible;
	bool m_bTrackingMouseHover;
	bool m_bTrackingMouseLeave;

	struct SOperationOverlayState
	{
		SOperationOverlayState()
			: bVisible(false)
			, bCanCancel(false)
			, bCancelHot(false)
			, bSuppressed(false)
			, bDelayedShow(false)
			, uDone(0)
			, uTotal(0)
			, uAnimationPhase(0)
			, dwFirstUpdateTick(0)
		{
		}

		bool bVisible;
		bool bCanCancel;
		bool bCancelHot;
		bool bSuppressed;
		bool bDelayedShow;
		UINT uDone;
		UINT uTotal;
		UINT uAnimationPhase;
		DWORD dwFirstUpdateTick;
		CString strTitle;
		CString strDetail;
		CString strCancelText;
		CRect rcLastOverlay;
	};
	SOperationOverlayState m_operationOverlay;
	COperationOverlayWnd* m_pOperationOverlayWnd;

	DWORD_PTR GetParamAt(POSITION pos, int iPos)
	{
		DWORD_PTR lParam = m_Params.GetAt(pos);
		if (lParam == 0xFEEBDEEF) { //same as MLC_MAGIC!
			lParam = CListCtrl::GetItemData(iPos);
			m_Params.SetAt(pos, lParam);
		}
		return lParam;
	}

	CList<int> m_liDefaultHiddenColumns;

	const int MultiSortProc(const LPARAM lParam1, const LPARAM lParam2) {
		for (POSITION pos = m_liSortHistory.GetHeadPosition(); pos != NULL; ) {
			// Use sort history for layered sorting
			int dwParamSort = m_liSortHistory.GetNext(pos);

			int ret = m_SortProc(lParam1, lParam2, dwParamSort);
			if (ret)
				return ret;
		}

		return 0; // Failed to sort
	}

	const static int CALLBACK MultiSortCallback(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort) {
		return ((CMuleListCtrl*)lParamSort)->MultiSortProc(lParam1, lParam2);
	}

	uint32 m_iMaxSortHistory;
};
