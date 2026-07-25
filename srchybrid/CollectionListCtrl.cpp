//This file is part of eMule AI
//Copyright (C)2002-2026 Merkur ( merkur-@users.sourceforge.net / https://www.emule-project.net )
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
#include "CollectionListCtrl.h"
#include "OtherFunctions.h"
#include "AbstractFile.h"
#include "MetaDataDlg.h"
#include "HighColorTab.hpp"
#include "ListViewWalkerPropertySheet.h"
#include "UserMsgs.h"
#include "eMuleAI/DarkMode.h"
#include "ClosableTabCtrl.h" 
#include <algorithm>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	const EListStateField kCollectionListViewState = static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL);
}

//////////////////////////////////////////////////////////////////////////////
// CCollectionFileDetailsSheet

class CCollectionFileDetailsSheet : public CListViewWalkerPropertySheet
{
	DECLARE_DYNAMIC(CCollectionFileDetailsSheet)

public:
	explicit CCollectionFileDetailsSheet(CTypedPtrList<CPtrList, CAbstractFile*> &aFiles, UINT uInvokePage = 0, CListCtrlItemWalk *pListCtrl = NULL);

	virtual BOOL OnInitDialog();

protected:
	CMetaDataDlg		m_wndMetaData;
	CClosableTabCtrl	m_tabDark;

	UINT m_uInvokePage;
	static LPCTSTR m_pPshStartPage;

	void UpdateTitle();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnDestroy();
	afx_msg LRESULT OnDataChanged(WPARAM, LPARAM);
};

LPCTSTR CCollectionFileDetailsSheet::m_pPshStartPage;

IMPLEMENT_DYNAMIC(CCollectionFileDetailsSheet, CListViewWalkerPropertySheet)

BEGIN_MESSAGE_MAP(CCollectionFileDetailsSheet, CListViewWalkerPropertySheet)
	ON_WM_DESTROY()
	ON_MESSAGE(UM_DATA_CHANGED, OnDataChanged)
END_MESSAGE_MAP()

CCollectionFileDetailsSheet::CCollectionFileDetailsSheet(CTypedPtrList<CPtrList, CAbstractFile*> &aFiles, UINT uInvokePage, CListCtrlItemWalk *pListCtrl)
	: CListViewWalkerPropertySheet(pListCtrl)
	, m_uInvokePage(uInvokePage)
{
	for (POSITION pos = aFiles.GetHeadPosition(); pos != NULL;)
		m_aItems.Add(aFiles.GetNext(pos));
	m_psh.dwFlags &= ~PSH_HASHELP;

	m_wndMetaData.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndMetaData.m_psp.dwFlags |= PSP_USEICONID;
	m_wndMetaData.m_psp.pszIcon = _T("METADATA");
	if (thePrefs.IsExtControlsEnabled() && m_aItems.GetSize() == 1) {
		m_wndMetaData.SetFiles(&m_aItems);
		AddPage(&m_wndMetaData);
	}

	LPCTSTR pPshStartPage = m_pPshStartPage;
	if (m_uInvokePage != 0)
		pPshStartPage = MAKEINTRESOURCE(m_uInvokePage);
	for (int i = (int)m_pages.GetCount(); --i >= 0;)
		if (GetPage(i)->m_psp.pszTemplate == pPshStartPage) {
			m_psh.nStartPage = i;
			break;
		}
}

void CCollectionFileDetailsSheet::OnDestroy()
{
	if (m_uInvokePage == 0)
		m_pPshStartPage = GetPage(GetActiveIndex())->m_psp.pszTemplate;
	CListViewWalkerPropertySheet::OnDestroy();
}

BOOL CCollectionFileDetailsSheet::OnInitDialog()
{
	EnableStackedTabs(FALSE);
	BOOL bResult = CListViewWalkerPropertySheet::OnInitDialog();
	HighColorTab::UpdateImageList(*this);
	InitWindowStyles(this);
	EnableSaveRestore(_T("CollectionFileDetailsSheet")); // call this after(!) OnInitDialog
	UpdateTitle();

	m_tabDark.m_bClosable = false;
	m_tabDark.m_bAllowTabReordering = false;

	if (IsDarkModeEnabled()) {
		HWND hTab = PropSheet_GetTabControl(m_hWnd);
		if (hTab != NULL) {
			::SetWindowTheme(hTab, _T(""), _T(""));
			m_tabDark.SubclassWindow(hTab);
		}
	}

	return bResult;
}

LRESULT CCollectionFileDetailsSheet::OnDataChanged(WPARAM, LPARAM)
{
	UpdateTitle();
	return 1;
}

void CCollectionFileDetailsSheet::UpdateTitle()
{
	CString sTitle(GetResString(_T("DETAILS")));
	if (m_aItems.GetSize() == 1)
		sTitle.AppendFormat(_T(": %s"), (LPCTSTR)(static_cast<CAbstractFile*>(m_aItems[0])->GetFileName()));
	SetWindowText(sTitle);
}



//////////////////////////////////////////////////////////////////////////////
// CCollectionListCtrl

IMPLEMENT_DYNAMIC(CCollectionListCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CCollectionListCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetDispInfo)
	ON_NOTIFY_REFLECT(NM_RCLICK, OnNmRClick)
END_MESSAGE_MAP()

CCollectionListCtrl::CCollectionListCtrl()
	: CListCtrlItemWalk(this)
{
}

void CCollectionListCtrl::Init(const CString &strNameAdd)
{
	SetPrefsKey(_T("CollectionListCtrl") + strNameAdd);

	SendMessage(LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)theApp.GetSystemImageList());

	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	// Alignment rule: left for text, dates, and status labels; right for sizes, rates, counts, durations, and percentages.
	InsertColumn(colName, GetResString(_T("DL_FILENAME")),	LVCFMT_LEFT,  DFLT_FILENAME_COL_WIDTH);
	InsertColumn(colSize, GetResString(_T("DL_SIZE")),		LVCFMT_RIGHT, DFLT_SIZE_COL_WIDTH);
	InsertColumn(colHash, GetResString(_T("FILEHASH")),		LVCFMT_LEFT,  DFLT_HASH_COL_WIDTH);

	LoadSettings();
	SetSortArrow();
	if (IsVirtualList())
		RefreshVirtualItemCount();
	else
		SortByCurrentSettings();
}

void CCollectionListCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	// Determine ascending based on whether already sorted on this column
	bool bSortAscending = (GetSortItem() != pNMLV->iSubItem || !GetSortAscending());

	// Item is the column clicked
	int iSortItem = pNMLV->iSubItem;

	// Sort table
	UpdateSortHistory(MAKELONG(iSortItem, !bSortAscending));
	SetSortArrow(iSortItem, bSortAscending);
	SortByCurrentSettings();
	*pResult = 0;
}

int CALLBACK CCollectionListCtrl::SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort)
{
	const CAbstractFile *item1 = reinterpret_cast<CAbstractFile*>(lParam1);
	const CAbstractFile *item2 = reinterpret_cast<CAbstractFile*>(lParam2);
	if (item1 == NULL || item2 == NULL)
		return 0;

	int iResult;
	switch (LOWORD(lParamSort)) {
	case colName:
		iResult = CompareLocaleStringNoCase(item1->GetFileName(), item2->GetFileName());
		break;
	case colSize:
		iResult = CompareUnsigned(item1->GetFileSize(), item2->GetFileSize());
		break;
	case colHash:
		iResult = memcmp(item1->GetFileHash(), item2->GetFileHash(), 16);
		break;
	default:
		return 0;
	}
	return HIWORD(lParamSort) ? -iResult : iResult;
}


bool CCollectionListCtrl::IsVirtualList() const
{
	return (GetStyle() & LVS_OWNERDATA) != 0;
}

void CCollectionListCtrl::RefreshVirtualItemCount()
{
	if (IsVirtualList())
		SetItemCountEx(static_cast<int>(m_ListedItemsVector.size()), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
}

void CCollectionListCtrl::SetVirtualFiles(const std::vector<CAbstractFile*> &aFiles)
{
	ASSERT(IsVirtualList());
	SaveListState(0, kCollectionListViewState);
	SetRedraw(false);
	m_ListedItemsVector = aFiles;
	ResortVirtualFiles();
	RebuildListedItemsMap();
	RefreshVirtualItemCount();
	RestoreListState(0, kCollectionListViewState, false);
	SetRedraw(true);
	Invalidate(FALSE);
}

void CCollectionListCtrl::ClearVirtualFiles()
{
	m_ListedItemsVector.clear();
	m_ListedItemsMap.RemoveAll();
	m_mapFileTypeImageCache.RemoveAll();
	RefreshVirtualItemCount();
	Invalidate(FALSE);
}

const CAbstractFile* CCollectionListCtrl::GetVirtualFileAt(int iItem) const
{
	if (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size())
		return NULL;
	return m_ListedItemsVector[static_cast<size_t>(iItem)];
}

DWORD_PTR CCollectionListCtrl::GetVirtualItemData(int iItem) const
{
	return reinterpret_cast<DWORD_PTR>(GetVirtualFileAt(iItem));
}

int CCollectionListCtrl::GetVirtualItemCount() const
{
	return static_cast<int>(m_ListedItemsVector.size());
}

CObject* CCollectionListCtrl::GetItemObject(int iIndex) const
{
	if (IsVirtualList())
		return const_cast<CAbstractFile*>(GetVirtualFileAt(iIndex));
	return CMuleListCtrl::GetItemObject(iIndex);
}

CString CCollectionListCtrl::GetFileItemText(const CAbstractFile *pAbstractFile, int iSubItem) const
{
	if (pAbstractFile == NULL)
		return EMPTY;

	switch (iSubItem) {
	case colName:
		return pAbstractFile->GetFileName();
	case colSize:
		return CastItoXBytes(pAbstractFile->GetFileSize());
	case colHash:
		return md4str(pAbstractFile->GetFileHash());
	default:
		return EMPTY;
	}
}

int CCollectionListCtrl::GetCachedFileTypeSystemImageIdx(const CString &strFileName) const
{
	CString strKey(strFileName);
	int iExt = strKey.ReverseFind(_T('.'));
	if (iExt >= 0)
		strKey = strKey.Mid(iExt);
	strKey.MakeLower();
	if (strKey.IsEmpty())
		strKey = _T("*");

	void *pImage = NULL;
	if (m_mapFileTypeImageCache.Lookup(strKey, pImage))
		return static_cast<int>(reinterpret_cast<INT_PTR>(pImage)) - 1;

	const int iImage = theApp.GetFileTypeSystemImageIdx(strFileName);
	m_mapFileTypeImageCache.SetAt(strKey, reinterpret_cast<void*>(static_cast<INT_PTR>(iImage + 1)));
	return iImage;
}

void CCollectionListCtrl::ResortVirtualFiles()
{
	if (!IsVirtualList() || m_ListedItemsVector.size() < 2)
		return;

	const int iSortItem = GetSortItem();
	if (iSortItem < 0)
		return;

	const LPARAM lParamSort = MAKELONG(iSortItem, !GetSortAscending());
	std::stable_sort(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), [lParamSort](const CAbstractFile *pLeft, const CAbstractFile *pRight) {
		return SortProc(reinterpret_cast<LPARAM>(pLeft), reinterpret_cast<LPARAM>(pRight), lParamSort) < 0;
	});
}

void CCollectionListCtrl::RebuildListedItemsMap()
{
	m_ListedItemsMap.RemoveAll();
	if (!m_ListedItemsVector.empty())
		m_ListedItemsMap.InitHashTable(static_cast<UINT>(m_ListedItemsVector.size() * 2 + 1));
	for (int i = 0; i < static_cast<int>(m_ListedItemsVector.size()); ++i)
		m_ListedItemsMap[m_ListedItemsVector[static_cast<size_t>(i)]] = i;
}

void CCollectionListCtrl::SortByCurrentSettings()
{
	if (IsVirtualList()) {
		SaveListState(0, kCollectionListViewState);
		SetRedraw(false);
		ResortVirtualFiles();
		RebuildListedItemsMap();
		RefreshVirtualItemCount();
		RestoreListState(0, kCollectionListViewState, false);
		SetRedraw(true);
		Invalidate(FALSE);
	} else
		SortItems(SortProc, MAKELONG(GetSortItem(), !GetSortAscending()));
}

void CCollectionListCtrl::OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult)
{
	if (!theApp.IsClosing()) {
		NMLVDISPINFO *pDispInfo = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
		LVITEM &rItem = pDispInfo->item;
		const CAbstractFile *pFile = GetVirtualFileAt(rItem.iItem);
		if (pFile != NULL) {
			if ((rItem.mask & LVIF_TEXT) && rItem.pszText != NULL && rItem.cchTextMax > 0) {
				CString strText = GetFileItemText(pFile, rItem.iSubItem);
				_tcsncpy_s(rItem.pszText, rItem.cchTextMax, strText, _TRUNCATE);
			}
			if (rItem.mask & LVIF_IMAGE)
				rItem.iImage = GetCachedFileTypeSystemImageIdx(pFile->GetFileName());
			if (rItem.mask & LVIF_PARAM)
				rItem.lParam = reinterpret_cast<LPARAM>(const_cast<CAbstractFile*>(pFile));
		}
	}
	*pResult = 0;
}

void CCollectionListCtrl::OnNmRClick(LPNMHDR, LRESULT *pResult)
{
	CTypedPtrList<CPtrList, CAbstractFile*> abstractFileList;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		int index = GetNextSelectedItem(pos);
		if (index >= 0)
			abstractFileList.AddTail(reinterpret_cast<CAbstractFile*>(GetItemData(index)));
	}

	if (!abstractFileList.IsEmpty()) {
		CCollectionFileDetailsSheet dialog(abstractFileList, 0, this);
		dialog.DoModal();
	}
	*pResult = 0;
}

void CCollectionListCtrl::AddFileToList(CAbstractFile *pAbstractFile)
{
	if (pAbstractFile == NULL) {
		ASSERT(0);
		return;
	}

	if (IsVirtualList()) {
		if (std::find(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), pAbstractFile) != m_ListedItemsVector.end()) {
			ASSERT(0);
			return;
		}
		SaveListState(0, kCollectionListViewState);
		SetRedraw(false);
		m_ListedItemsVector.push_back(pAbstractFile);
		ResortVirtualFiles();
		RebuildListedItemsMap();
		RefreshVirtualItemCount();
		RestoreListState(0, kCollectionListViewState, false);
		SetRedraw(true);
		Invalidate(FALSE);
		return;
	}

	LVFINDINFO find;
	find.flags = LVFI_PARAM;
	find.lParam = (LPARAM)pAbstractFile;
	int iItem = FindItem(&find);
	if (iItem >= 0) {
		ASSERT(0);
		return;
	}

	int iImage = GetCachedFileTypeSystemImageIdx(pAbstractFile->GetFileName());
	iItem = InsertItem(LVIF_TEXT | LVIF_PARAM | (iImage > 0 ? LVIF_IMAGE : 0), GetItemCount(), EMPTY, 0, 0, iImage, (LPARAM)pAbstractFile);
	if (iItem >= 0) {
		SetItemText(iItem, colName, pAbstractFile->GetFileName());
		SetItemText(iItem, colSize, CastItoXBytes(pAbstractFile->GetFileSize()));
		SetItemText(iItem, colHash, md4str(pAbstractFile->GetFileHash()));
	}
}

void CCollectionListCtrl::RemoveFileFromList(CAbstractFile *pAbstractFile)
{
	if (IsVirtualList()) {
		std::vector<CAbstractFile*>::iterator it = std::find(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), pAbstractFile);
		if (it != m_ListedItemsVector.end()) {
			SaveListState(0, kCollectionListViewState);
			SetRedraw(false);
			m_ListedItemsVector.erase(it);
			RebuildListedItemsMap();
			RefreshVirtualItemCount();
			RestoreListState(0, kCollectionListViewState, false);
			SetRedraw(true);
			Invalidate(FALSE);
		} else
			ASSERT(0);
		return;
	}

	LVFINDINFO find;
	find.flags = LVFI_PARAM;
	find.lParam = (LPARAM)pAbstractFile;
	int iItem = FindItem(&find);
	if (iItem >= 0)
		DeleteItem(iItem);
	else
		ASSERT(0);
}
