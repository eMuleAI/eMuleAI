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
#include <map>
#include "MuleListCtrl.h"
#include "eMuleAI/MenuXP.h"
#include "ListCtrlItemWalk.h"
#include "ToolTipCtrlX.h"
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
	DWORD			dwUpdated;
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

	void	UpdateItem(void *toupdate);
	void	Init();
	void	AddFile(CPartFile *toadd);
	void	AddSource(CPartFile *owner, CUpDownClient *source, bool notavailable);
	void	RemoveSource(CUpDownClient *source, CPartFile *owner);
	bool	RemoveFile(CPartFile* toremove);
	void	ReloadList(const bool bSortCurrentList, const EListStateField LsfFlag);
	void	RebuildListedItemsMap();
	virtual DWORD_PTR GetVirtualItemData(int iItem) const override { return (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size() ? 0 : reinterpret_cast<DWORD_PTR>(m_ListedItemsVector[iItem])); } // Return null if index invalid, otherwise return the pointer
	int		GetVirtualItemCount() const override { return m_ListedItemsVector.size(); }
	CObject* GetItemObject(int iIndex) const;

	std::vector<CtrlItem_Struct*> m_ListedItemsVector; // This vector is used to list, iterate and sort files.
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
	void	GetDisplayedFiles(CArray<CPartFile*, CPartFile*> *list);
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
	void	ShowActiveDownloadsBold(const bool bEnabled);
	const bool IsFilteredOut(CPartFile* pFile);
	uint32 GetTotalFilesCount();
	bool	m_bRightClicked;
	
	static void SetFileDeletionInProgress(bool bInProgress) { s_bFileDeletionInProgress = bInProgress; }
	static bool IsFileDeletionInProgress() { return s_bFileDeletionInProgress; }
protected:
	CImageList  m_ImageList;
	CMenuXP	m_PrioMenu;
	CMenuXP	m_FileMenu;
	CMenuXP	m_PreviewMenu;
	CMenuXP	m_SourcesMenu;
	bool		m_bRemainSort;
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
	void DrawFileItem(CDC *dc, int nColumn, LPCRECT lpRect, UINT uDrawTextAlignment, CtrlItem_Struct *pCtrlItem);
	void DrawSourceItem(CDC *dc, int nColumn, LPCRECT lpRect, UINT uDrawTextAlignment, CtrlItem_Struct *pCtrlItem);
	CString GetFileItemDisplayText(const CPartFile *lpPartFile, int iSubItem);
	CString GetSourceItemDisplayText(const CtrlItem_Struct *pCtrlItem, int iSubItem);
	virtual bool UsePersistentInfoTips() const override { return true; }
	virtual bool GetPersistentInfoTipText(const SPersistentInfoTipContext& context, CString& strText) override;
	virtual int GetDefaultPersistentInfoTipExtraLeftPadding(const SPersistentInfoTipContext& context) const override;

	static int CALLBACK SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort);
	const static int Compare(const CPartFile *file1, const CPartFile *file2, const LPARAM lParamSort);
	const static int Compare(const CUpDownClient *client1, const CUpDownClient *client2, const LPARAM lParamSort);

	virtual BOOL OnCommand(WPARAM wParam, LPARAM);
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
	
	// Override to maintain sort order after updates
	virtual void MaintainSortOrderAfterUpdate() override;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnContextMenu(CWnd*, CPoint point);
	afx_msg void OnListModified(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnItemActivate(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnNmDblClk(LPNMHDR, LRESULT *pResult);
	afx_msg void OnSysColorChange();
	afx_msg LRESULT OnEmptyFakeFileFound(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnInvalidExtensionFound(WPARAM wParam, LPARAM lParam);
private:
	static UINT AFX_CDECL DownloadInspectorProc(LPVOID pParam);
	CWinThread* pDownloadInspectorThread;
	DWORD m_dwLastDetection;

	struct PartFileOperationMsgParams
	{
		CPartFile* pFile;
		CString strNewFileName;
		CString cLogMsg;
	};
	
	static bool s_bFileDeletionInProgress; // Track file deletion to avoid redundant SaveListState/RestoreListState calls
};
