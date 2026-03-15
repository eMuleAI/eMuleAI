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
#include "ToolTipCtrlX.h"
#include <vector>

#define AVBLYSHADECOUNT 13

class CSearchList;
class CSearchFile;

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

class CSearchListCtrl : public CMuleListCtrl, public CListCtrlItemWalk, public CListStateTemplate<CSearchListCtrl, CSearchFile>
{
	friend class CListStateTemplate<CSearchListCtrl, CSearchFile>;

private:
	using ListStateHelper = CListStateTemplate<CSearchListCtrl, CSearchFile>;
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
	void	ReloadList(const bool bSortCurrentList, const EListStateField LsfFlag);
	void	RebuildListedItemsMap();
	virtual DWORD_PTR GetVirtualItemData(int iItem) const override { return (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size() ? 0 : reinterpret_cast<DWORD_PTR>(m_ListedItemsVector[iItem])); } // Return null if index invalid, otherwise return the pointer
	int		GetVirtualItemCount() const override { return m_ListedItemsVector.size(); }
	CObject* GetItemObject(int iIndex) const;

	std::vector<CSearchFile*> m_ListedItemsVector; // This vector is used to list, iterate and sort results.
	typedef	CMap<CSearchFile*, CSearchFile*, int, int&> CListedItemsMap;
	CListedItemsMap m_ListedItemsMap; // This map is used to lookup search results index.
	void	Localize();
	void	NoTabs()								{ m_nResultsID = 0; }
	void	UpdateSearch(CSearchFile *toupdate);
	void	UpdateTabHeader(uint32 nSearchID, CString strClientHash, bool bUpdateAllSharedListTabs);
	EFileSizeFormat GetFileSizeFormat() const		{ return m_eFileSizeFormat; }
	void	SetFileSizeFormat(EFileSizeFormat eFormat);
	static int CALLBACK SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort); // Moved to public
	uint32	m_nResultsID; // Moved to public

protected:
	CMenuXP	m_SearchFileMenu;
	CSearchList	*searchlist;
	CToolTipCtrlX m_tooltip;
	CImageList	m_ImageList;
	COLORREF	m_crSearchResultDownloading;
	COLORREF	m_crSearchResultDownloadStopped;
	COLORREF	m_crSearchResultKnown;
	COLORREF	m_crSearchResultSharing;
	COLORREF	m_crSearchResultCancelled;
	COLORREF	m_crShades[AVBLYSHADECOUNT];
	EFileSizeFormat m_eFileSizeFormat;


	COLORREF GetSearchItemColor(/*const*/ CSearchFile* src);
	bool	IsComplete(const CSearchFile *pFile, UINT uSources) const;
	CString GetCompleteSourcesDisplayString(const CSearchFile *pFile, UINT uSources, bool *pbComplete = NULL) const;
	void	ExpandCollapseItem(int iItem, int iAction);
	void	HideSources(CSearchFile *toCollapse);
	void	SetStyle();
	void	SetHighlightColors();
	void	SetAllIcons();
	CString	FormatFileSize(ULONGLONG ullFileSize) const;
	CString GetItemDisplayText(const CSearchFile *src, int iSubItem) const;
	const bool	IsFilteredOut(const CSearchFile *pSearchFile) const;
	const static CString CSearchListCtrl::GetKnownTypeStr(const CSearchFile* src);

	void	DrawSourceParent(CDC *dc, int nColumn, LPRECT lpRect, UINT uDrawTextAlignment, const CSearchFile *src);
	void	DrawSourceChild(CDC *dc, int nColumn, LPRECT lpRect, UINT uDrawTextAlignment, const CSearchFile *src);

	static int Compare(const CSearchFile *item1, const CSearchFile *item2, LPARAM lParamSort, bool bSortMod);
	static int CompareChild(const CSearchFile *file1, const CSearchFile *file2, LPARAM lParamSort);

	virtual BOOL OnCommand(WPARAM wParam, LPARAM);
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);

	static LPARAM	m_pSortParam;
	int 			m_iDataSize;
	const static bool SortFunc(const CSearchFile* first, const CSearchFile* second);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnContextMenu(CWnd*, CPoint point);
	afx_msg void OnDestroy();
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnDeleteAllItems(LPNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnKeyDown(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnNmClick(LPNMHDR pNMHDR, LRESULT*);
	afx_msg void OnNmDblClk(LPNMHDR, LRESULT*);
	afx_msg void OnSysColorChange();
};