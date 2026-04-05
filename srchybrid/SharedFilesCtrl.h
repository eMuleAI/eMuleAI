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
#include "MuleListCtrl.h"
#include "eMuleAI/MenuXP.h"
#include "ListCtrlItemWalk.h"
#include "KnownFileList.h"
#include <vector>

class CSharedFileList;
class CKnownFile;
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

	DECLARE_DYNAMIC(CSharedFilesCtrl)
	friend class CSharedDirsTreeCtrl;

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
	void	AddFile(CKnownFile*file);
	void	RemoveFile(CKnownFile*file, const bool bDeletedFromDisk, const bool bWillReloadListLater = false);
	void	RemoveFromHistory(CKnownFile* toRemove, const bool bWillReloadListLater = false);
	void	ReloadList(const bool bSortCurrentList, const EListStateField LsfFlag);
	void	RebuildListedItemsMap();
	virtual DWORD_PTR GetVirtualItemData(int iItem) const override { return (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size() ? 0 : reinterpret_cast<DWORD_PTR>(m_ListedItemsVector[iItem])); } // Return null if index invalid, otherwise return the pointer
	int		GetVirtualItemCount() const override { return m_ListedItemsVector.size(); }
	CObject* GetItemObject(int iIndex) const; 
	uint32  GetFilterId() const;
	void	SetAllIcons();
	std::vector<CKnownFile*> m_ListedItemsVector; // This vector is used to list, iterate and sort files.
	typedef	CMap<CKnownFile*, CKnownFile*, int, int&> CHistoryFilesMap;
	CHistoryFilesMap m_ListedItemsMap; // This map is used to lookup file index.
	FilterType m_eFilter; // Type of directory this control is displaying
	uint32 m_uFilterID; // ID of the filter, used to identify the filter in the list
	volatile LONG nAICHHashing;
	void	UpdateFile(CKnownFile* file, const bool bUpdateFileSummary = true, const bool bDeletedFromDisk = false, const int iIndex = -1);
	bool	CheckBoxesEnabled() const;
	void	Localize();
	void	ShowFilesCount();
	void	ShowComments(CShareableFile *file);
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
	bool			m_aSortBySecondValue[4];
	CImageList		m_ImageList;
	CDirectoryItem	*m_pDirectoryFilter;
	//volatile INT_PTR	nAICHHashing;
	static LPARAM	m_pSortParam;
	CMenuXP			m_FileHistorysMenu;
	int 			m_iDataSize;
	CToolTipCtrlX	*m_pToolTip;
	CTypedPtrList<CPtrList, CShareableFile*>	liTempShareableFilesInDir;
	CShareableFile *m_pHighlightedItem;
	CShareDropTarget m_ShareDropTarget;
	bool m_bSelectionRestoreInProgress;

	void UpdateListedItemsMapRange(int iStartIndex, int iEndIndex);
	static int CALLBACK SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort);
	static bool SortFunc(const CKnownFile* fileA, const CKnownFile* fileB);
	bool HasActiveSortOrder() const;
	bool NeedsSortReposition(int iIndex) const;
	bool RepositionFileByCurrentSort(CKnownFile* file, int iIndex);
	void OpenFile(const CShareableFile *file);
	void ShowFileDialog(CTypedPtrList<CPtrList, CShareableFile*> &aFiles, UINT uInvokePage = 0);
	const CString GetItemDisplayText(const CShareableFile *file, const int iSubItem) const;
	const bool IsFilteredOut(const CShareableFile *pKnownFile) const;
	const bool IsSharedInKad(const CKnownFile *file) const;
	void CheckBoxClicked(const int iItem);
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
	afx_msg BOOL OnNMClick(LPNMHDR pNMHDR, LRESULT *pResult);
};
