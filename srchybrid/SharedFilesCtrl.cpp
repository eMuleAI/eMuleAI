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
#include "emuledlg.h"
#include "SharedFilesCtrl.h"
#include "KnownFileList.h"
#include "DownloadQueue.h" 
#include "UpDownClient.h"
#include "FileInfoDialog.h"
#include "MetaDataDlg.h"
#include "ED2kLinkDlg.h"
#include "ArchivePreviewDlg.h"
#include "CommentDialog.h"
#include "HighColorTab.hpp"
#include "ListViewWalkerPropertySheet.h"
#include "UserMsgs.h"
#include "ResizableLib/ResizableSheet.h"
#include "KnownFile.h"
#include "MapKey.h"
#include "SharedFileList.h"
#include "MemDC.h"
#include "PartFile.h"
#include "MenuCmds.h"
#include "IrcWnd.h"
#include "SharedFilesWnd.h"
#include "Opcodes.h"
#include "InputBox.h"
#include "WebServices.h"
#include "TransferDlg.h"
#include "ClientList.h"
#include "Collection.h"
#include "CollectionCreateDialog.h"
#include "CollectionViewDialog.h"
#include "SearchParams.h"
#include "SearchDlg.h"
#include "SearchResultsWnd.h"
#include "ToolTipCtrlX.h"
#include "kademlia/kademlia/kademlia.h"
#include "kademlia/kademlia/UDPFirewallTester.h"
#include "MediaInfo.h"
#include "Log.h"
#include "OtherFunctions.h"
#include "KnownFileList.h"
#include "ListViewSearchDlg.h"
#include <algorithm>
#include "MuleStatusBarCtrl.h"
#include "TransferDlg.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

LPARAM CSharedFilesCtrl::m_pSortParam = NULL ;

namespace
{
	const EListStateField kSharedFilesViewState = static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL);
	const DWORD kSharedFilesSetItemCountFlags = LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL;

	typedef CMap<CString, LPCTSTR, CShareableFile*, CShareableFile*> CTempShareableFilesMap;
	typedef CMap<CShareableFile*, CShareableFile*, ULONGLONG, ULONGLONG> CTempShareableFileWriteTimeMap;

	CTempShareableFileWriteTimeMap g_mapTempShareableFileWriteTimes;

	void UpdateSharedFilesItemCount(CListCtrl& listCtrl, const size_t itemCount)
	{
		listCtrl.SetItemCountEx(static_cast<int>(itemCount), kSharedFilesSetItemCountFlags);
	}

	CString BuildTempShareableFileKey(const CString& strFilePath)
	{
		CString strKey(strFilePath);
		strKey.MakeLower();
		return strKey;
	}

	ULONGLONG FileTimeToUInt64(const FILETIME& fileTime)
	{
		return (static_cast<ULONGLONG>(fileTime.dwHighDateTime) << 32) | fileTime.dwLowDateTime;
	}

	void RemoveTempShareableFileWriteTime(const CShareableFile* pTempShareableFile)
	{
		if (pTempShareableFile != NULL)
			g_mapTempShareableFileWriteTimes.RemoveKey(const_cast<CShareableFile*>(pTempShareableFile));
	}

	void SetTempShareableFileWriteTime(const CShareableFile& tempShareableFile, const FILETIME& fileTime)
	{
		g_mapTempShareableFileWriteTimes.SetAt(const_cast<CShareableFile*>(&tempShareableFile), FileTimeToUInt64(fileTime));
	}

	bool TryGetTempShareableFileWriteTime(const CShareableFile& tempShareableFile, ULONGLONG& ullFileTime)
	{
		return g_mapTempShareableFileWriteTimes.Lookup(const_cast<CShareableFile*>(&tempShareableFile), ullFileTime) != FALSE;
	}

	void DeleteTempShareableFilesList(CTypedPtrList<CPtrList, CShareableFile*>& liTempShareableFiles)
	{
		while (!liTempShareableFiles.IsEmpty()) {
			CShareableFile* pTempShareableFile = liTempShareableFiles.RemoveHead();
			RemoveTempShareableFileWriteTime(pTempShareableFile);
			delete pTempShareableFile;
		}
	}

	void DeleteTempShareableFilesMap(CTempShareableFilesMap& mapTempShareableFiles)
	{
		for (POSITION pos = mapTempShareableFiles.GetStartPosition(); pos != NULL;) {
			CString strKey;
			CShareableFile* pTempShareableFile = NULL;
			mapTempShareableFiles.GetNextAssoc(pos, strKey, pTempShareableFile);
			RemoveTempShareableFileWriteTime(pTempShareableFile);
			delete pTempShareableFile;
		}
		mapTempShareableFiles.RemoveAll();
	}

	bool CanReuseTempShareableFile(const CShareableFile* pTempShareableFile, const ULONGLONG ullFoundFileSize, const bool bHasFoundFileTime, const FILETIME& tFoundFileTime)
	{
		if (pTempShareableFile == NULL || pTempShareableFile->GetFileSize() != ullFoundFileSize || !bHasFoundFileTime)
			return false;

		ULONGLONG ullStoredFileTime = 0;
		return TryGetTempShareableFileWriteTime(*pTempShareableFile, ullStoredFileTime) && ullStoredFileTime == FileTimeToUInt64(tFoundFileTime);
	}

	void RefreshTempShareableFile(CShareableFile& tempShareableFile, const CString& strFoundFilePath, const CString& strFoundFileName, const CString& strFoundDirectory, const ULONGLONG ullFoundFileSize, const bool bHasFoundFileTime, const FILETIME& tFoundFileTime)
	{
		tempShareableFile.SetFilePath(strFoundFilePath);
		tempShareableFile.SetAFileName(strFoundFileName);
		tempShareableFile.SetPath(strFoundDirectory);
		tempShareableFile.SetSharedDirectory(_T(""));
		tempShareableFile.SetFileSize(ullFoundFileSize);
		tempShareableFile.SetVerifiedFileType(FILETYPE_UNKNOWN);
		tempShareableFile.ClearTags();
		const uchar aucMD4[MDX_DIGEST_SIZE] = {};
		tempShareableFile.SetFileHash(aucMD4);
		if (bHasFoundFileTime)
			SetTempShareableFileWriteTime(tempShareableFile, tFoundFileTime);
		else
			RemoveTempShareableFileWriteTime(&tempShareableFile);
	}

	struct SharedFilesRepositionState
	{
		CArray<CKnownFile*, CKnownFile*> aSelectedItems;
		CKnownFile* pFocusedItem = NULL;
		CKnownFile* pSelectionMarkItem = NULL;
		int iTopIndex = 0;
		int iHScrollPosition = 0;
	};

	CKnownFile* GetSharedFilesItemByIndex(const CSharedFilesCtrl& listCtrl, const int iIndex)
	{
		if (iIndex < 0 || iIndex >= static_cast<int>(listCtrl.m_ListedItemsVector.size()))
			return NULL;

		return listCtrl.m_ListedItemsVector[iIndex];
	}

	void CaptureSharedFilesRepositionState(CSharedFilesCtrl& listCtrl, SharedFilesRepositionState& state)
	{
		for (POSITION pos = listCtrl.GetFirstSelectedItemPosition(); pos != NULL;) {
			const int iSelectedIndex = listCtrl.GetNextSelectedItem(pos);
			CKnownFile* pSelectedFile = GetSharedFilesItemByIndex(listCtrl, iSelectedIndex);
			if (pSelectedFile != NULL)
				state.aSelectedItems.Add(pSelectedFile);
		}

		state.pFocusedItem = GetSharedFilesItemByIndex(listCtrl, listCtrl.GetNextItem(-1, LVNI_FOCUSED));
		state.pSelectionMarkItem = GetSharedFilesItemByIndex(listCtrl, listCtrl.GetSelectionMark());
		state.iTopIndex = max(0, listCtrl.GetTopIndex());

		SCROLLINFO siHorz = { sizeof(siHorz) };
		if (listCtrl.GetScrollInfo(SB_HORZ, &siHorz, SIF_POS))
			state.iHScrollPosition = siHorz.nPos;
	}

	void RestoreSharedFilesTopIndex(CSharedFilesCtrl& listCtrl, int iTopIndex)
	{
		const int iItemCount = listCtrl.GetItemCount();
		if (iItemCount <= 0)
			return;

		iTopIndex = max(0, min(iTopIndex, iItemCount - 1));
		if (iTopIndex == 0) {
			listCtrl.EnsureVisible(0, FALSE);
			return;
		}

		const int iRowsPerPage = listCtrl.GetCountPerPage();
		if (iRowsPerPage > 0) {
			int iBottomIndex = iTopIndex + iRowsPerPage - 1;
			if (iBottomIndex >= iItemCount)
				iBottomIndex = iItemCount - 1;
			listCtrl.EnsureVisible(iBottomIndex, FALSE);
		} else
			listCtrl.EnsureVisible(iTopIndex, FALSE);

		const int iCurrentTop = listCtrl.GetTopIndex();
		if (iCurrentTop == iTopIndex || iCurrentTop < 0)
			return;

		CRect rcTop;
		CRect rcNext;
		if (!listCtrl.GetItemRect(iCurrentTop, &rcTop, LVIR_BOUNDS) || iCurrentTop + 1 >= iItemCount || !listCtrl.GetItemRect(iCurrentTop + 1, &rcNext, LVIR_BOUNDS))
			return;

		const int iRowHeight = rcNext.top - rcTop.top;
		if (iRowHeight > 0)
			listCtrl.Scroll(CSize(0, (iTopIndex - iCurrentTop) * iRowHeight));
	}

	void RestoreSharedFilesHorizontalScroll(CSharedFilesCtrl& listCtrl, int iHScrollPosition)
	{
		SCROLLINFO siHorz = { sizeof(siHorz) };
		if (!listCtrl.GetScrollInfo(SB_HORZ, &siHorz, SIF_POS | SIF_RANGE | SIF_PAGE))
			return;

		int iMaxHScroll = siHorz.nMax;
		if (siHorz.nPage > 0)
			iMaxHScroll = max(siHorz.nMin, siHorz.nMax - static_cast<int>(siHorz.nPage) + 1);

		iHScrollPosition = max(siHorz.nMin, min(iHScrollPosition, iMaxHScroll));
		if (siHorz.nPos != iHScrollPosition)
			listCtrl.Scroll(CSize(iHScrollPosition - siHorz.nPos, 0));
	}

	void RestoreSharedFilesRepositionState(CSharedFilesCtrl& listCtrl, const SharedFilesRepositionState& state)
	{
		listCtrl.SetItemState(-1, 0, LVIS_FOCUSED | LVIS_SELECTED);

		int iFirstSelectedIndex = -1;
		for (INT_PTR i = 0; i < state.aSelectedItems.GetCount(); ++i) {
			int iSelectedIndex = -1;
			if (!listCtrl.m_ListedItemsMap.Lookup(state.aSelectedItems[i], iSelectedIndex) || iSelectedIndex < 0)
				continue;

			listCtrl.SetItemState(iSelectedIndex, LVIS_SELECTED, LVIS_SELECTED);
			if (iFirstSelectedIndex == -1 || iSelectedIndex < iFirstSelectedIndex)
				iFirstSelectedIndex = iSelectedIndex;
		}

		int iSelectionMarkIndex = -1;
		if (state.pSelectionMarkItem != NULL)
			listCtrl.m_ListedItemsMap.Lookup(state.pSelectionMarkItem, iSelectionMarkIndex);
		if (iSelectionMarkIndex < 0)
			iSelectionMarkIndex = iFirstSelectedIndex;
		listCtrl.SetSelectionMark(iSelectionMarkIndex);

		int iFocusedIndex = -1;
		if (state.pFocusedItem != NULL)
			listCtrl.m_ListedItemsMap.Lookup(state.pFocusedItem, iFocusedIndex);
		if (iFocusedIndex < 0)
			iFocusedIndex = iSelectionMarkIndex;
		if (iFocusedIndex < 0)
			iFocusedIndex = iFirstSelectedIndex;
		if (iFocusedIndex >= 0 && iFocusedIndex < listCtrl.GetItemCount())
			listCtrl.SetItemState(iFocusedIndex, LVIS_FOCUSED, LVIS_FOCUSED);

		RestoreSharedFilesTopIndex(listCtrl, state.iTopIndex);
		RestoreSharedFilesHorizontalScroll(listCtrl, state.iHScrollPosition);
	}

	class CSharedFilesSelectionRestoreGuard
	{
	public:
		explicit CSharedFilesSelectionRestoreGuard(CSharedFilesCtrl& listCtrl)
			: m_listCtrl(listCtrl)
		{
			m_listCtrl.SetSelectionRestoreInProgress(true);
		}

		~CSharedFilesSelectionRestoreGuard()
		{
			m_listCtrl.SetSelectionRestoreInProgress(false);
		}

	private:
		CSharedFilesCtrl& m_listCtrl;
	};
}

bool NeedArchiveInfoPage(const CSimpleArray<CObject*> *paItems);
void UpdateFileDetailsPages(CListViewPropertySheet *pSheet
	, CResizablePage *pArchiveInfo, CResizablePage *pMediaInfo, CResizablePage *pFileLink);


//////////////////////////////////////////////////////////////////////////////
// CSharedFileDetailsSheet

class CSharedFileDetailsSheet : public CListViewWalkerPropertySheet
{
	DECLARE_DYNAMIC(CSharedFileDetailsSheet)

	void Localize();
public:
	CSharedFileDetailsSheet(CTypedPtrList<CPtrList, CShareableFile*> &aFiles, UINT uInvokePage = 0, CListCtrlItemWalk *pListCtrl = NULL);

protected:
	CArchivePreviewDlg	m_wndArchiveInfo;
	CCommentDialog		m_wndFileComments;
	CED2kLinkDlg		m_wndFileLink;
	CFileInfoDialog		m_wndMediaInfo;
	CMetaDataDlg		m_wndMetaData;
	CClosableTabCtrl	m_tabDark;

	UINT m_uInvokePage;
	static LPCTSTR m_pPshStartPage;

	void UpdateTitle();

	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnDestroy();
	afx_msg LRESULT OnDataChanged(WPARAM, LPARAM);
};

LPCTSTR CSharedFileDetailsSheet::m_pPshStartPage;

IMPLEMENT_DYNAMIC(CSharedFileDetailsSheet, CListViewWalkerPropertySheet)

BEGIN_MESSAGE_MAP(CSharedFileDetailsSheet, CListViewWalkerPropertySheet)
	ON_WM_DESTROY()
	ON_MESSAGE(UM_DATA_CHANGED, OnDataChanged)
END_MESSAGE_MAP()

void CSharedFileDetailsSheet::Localize()
{
	m_wndMediaInfo.Localize();
	SetTabTitle(_T("CONTENT_INFO"), &m_wndMediaInfo, this);
	m_wndMetaData.Localize();
	SetTabTitle(_T("META_DATA"), &m_wndMetaData, this);
	m_wndFileLink.Localize();
	SetTabTitle(_T("SW_LINK"), &m_wndFileLink, this);
	m_wndFileComments.Localize();
	SetTabTitle(_T("COMMENT"), &m_wndFileComments, this);
	m_wndArchiveInfo.Localize();
	SetTabTitle(_T("CONTENT_INFO"), &m_wndArchiveInfo, this);
}

CSharedFileDetailsSheet::CSharedFileDetailsSheet(CTypedPtrList<CPtrList, CShareableFile*> &aFiles, UINT uInvokePage, CListCtrlItemWalk *pListCtrl)
	: CListViewWalkerPropertySheet(pListCtrl)
	, m_uInvokePage(uInvokePage)
{
	for (POSITION pos = aFiles.GetHeadPosition(); pos != NULL;)
		m_aItems.Add(aFiles.GetNext(pos));
	m_psh.dwFlags &= ~PSH_HASHELP;

	m_wndFileComments.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndFileComments.m_psp.dwFlags |= PSP_USEICONID;
	m_wndFileComments.m_psp.pszIcon = _T("FileComments");
	m_wndFileComments.SetFiles(&m_aItems);
	AddPage(&m_wndFileComments);

	m_wndArchiveInfo.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndArchiveInfo.m_psp.dwFlags |= PSP_USEICONID;
	m_wndArchiveInfo.m_psp.pszIcon = _T("ARCHIVE_PREVIEW");
	m_wndArchiveInfo.SetFiles(&m_aItems);

	m_wndMediaInfo.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndMediaInfo.m_psp.dwFlags |= PSP_USEICONID;
	m_wndMediaInfo.m_psp.pszIcon = _T("MEDIAINFO");
	m_wndMediaInfo.SetFiles(&m_aItems);
	if (NeedArchiveInfoPage(&m_aItems))
		AddPage(&m_wndArchiveInfo);
	else
		AddPage(&m_wndMediaInfo);

	m_wndMetaData.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndMetaData.m_psp.dwFlags |= PSP_USEICONID;
	m_wndMetaData.m_psp.pszIcon = _T("METADATA");
	if (m_aItems.GetSize() == 1 && thePrefs.IsExtControlsEnabled()) {
		m_wndMetaData.SetFiles(&m_aItems);
		AddPage(&m_wndMetaData);
	}

	m_wndFileLink.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndFileLink.m_psp.dwFlags |= PSP_USEICONID;
	m_wndFileLink.m_psp.pszIcon = _T("ED2KLINK");
	m_wndFileLink.SetFiles(&m_aItems);
	AddPage(&m_wndFileLink);

	LPCTSTR pPshStartPage = m_pPshStartPage;
	if (m_uInvokePage != 0)
		pPshStartPage = MAKEINTRESOURCE(m_uInvokePage);
	for (int i = (int)m_pages.GetCount(); --i >= 0;)
		if (GetPage(i)->m_psp.pszTemplate == pPshStartPage) {
			m_psh.nStartPage = i;
			break;
		}
}

void CSharedFileDetailsSheet::OnDestroy()
{
	if (m_uInvokePage == 0)
		m_pPshStartPage = GetPage(GetActiveIndex())->m_psp.pszTemplate;
	CListViewWalkerPropertySheet::OnDestroy();
}

BOOL CSharedFileDetailsSheet::OnInitDialog()
{
	EnableStackedTabs(FALSE);
	BOOL bResult = CListViewWalkerPropertySheet::OnInitDialog();
	HighColorTab::UpdateImageList(*this);
	InitWindowStyles(this);
	EnableSaveRestore(_T("SharedFileDetailsSheet")); // call this after(!) OnInitDialog
	Localize();
	UpdateTitle();

	m_tabDark.m_bClosable = false;

	if (IsDarkModeEnabled()) {
		HWND hTab = PropSheet_GetTabControl(m_hWnd);
		if (hTab != NULL) {
			::SetWindowTheme(hTab, _T(""), _T(""));
			m_tabDark.SubclassWindow(hTab);
		}
	}

	return bResult;
}

LRESULT CSharedFileDetailsSheet::OnDataChanged(WPARAM, LPARAM)
{
	UpdateTitle();
	UpdateFileDetailsPages(this, &m_wndArchiveInfo, &m_wndMediaInfo, &m_wndFileLink);
	return 1;
}

void CSharedFileDetailsSheet::UpdateTitle()
{
	CString sTitle(GetResString(_T("DETAILS")));
	if (m_aItems.GetSize() == 1)
		sTitle.AppendFormat(_T(": %s"), (LPCTSTR)(static_cast<CAbstractFile*>(m_aItems[0])->GetFileName()));
	SetWindowText(sTitle);
}

BOOL CSharedFileDetailsSheet::OnCommand(WPARAM wParam, LPARAM lParam)
{
	if (wParam == ID_APPLY_NOW) {
		CSharedFilesCtrl *pSharedFilesCtrl = DYNAMIC_DOWNCAST(CSharedFilesCtrl, m_pListCtrl->GetListCtrl());
		if (pSharedFilesCtrl)
			for (int i = m_aItems.GetSize(); --i >= 0;)
				// so, and why does this not(!) work while the sheet is open ??
				pSharedFilesCtrl->UpdateFile(DYNAMIC_DOWNCAST(CKnownFile, m_aItems[i]));
	}
	return CListViewWalkerPropertySheet::OnCommand(wParam, lParam);
}


//////////////////////////////////////////////////////////////////////////////
// CSharedFilesCtrl

IMPLEMENT_DYNAMIC(CSharedFilesCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CSharedFilesCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetDispInfo)
	ON_NOTIFY_REFLECT(LVN_GETINFOTIP, OnLvnGetInfoTip)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
	ON_NOTIFY_REFLECT_EX(NM_CLICK, OnNMClick)
	ON_WM_CONTEXTMENU()
	ON_WM_KEYDOWN()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_MOUSEMOVE()
END_MESSAGE_MAP()

CSharedFilesCtrl::CSharedFilesCtrl()
	: CListCtrlItemWalk(this)
	, nAICHHashing()
	, m_eFilter(FilterType::Shared)
	, m_uFilterID(1)
	, m_aSortBySecondValue()
	, m_pDirectoryFilter()
	, m_iDataSize(-1)
	, m_pToolTip(NULL)
	, m_pHighlightedItem()
	, m_bSelectionRestoreInProgress(false)
{
	SetGeneralPurposeFind(true);
	m_pToolTip = new CToolTipCtrlX;
	SetSkinKey(_T("SharedFilesLv"));
}

CSharedFilesCtrl::~CSharedFilesCtrl()
{
	DeleteTempShareableFilesList(liTempShareableFilesInDir);
	delete m_pToolTip;
}

void CSharedFilesCtrl::Init()
{
	SetPrefsKey(_T("SharedFilesCtrl"));
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	ASSERT((GetStyle() & LVS_SINGLESEL) == 0);

	// Alignment rule: left for text, dates, and status labels; right for sizes, rates, counts, durations, and percentages.
	InsertColumn(0,		EMPTY,	LVCFMT_LEFT,	DFLT_FILENAME_COL_WIDTH);			//DL_FILENAME
	InsertColumn(1,		EMPTY,	LVCFMT_RIGHT,	DFLT_SIZE_COL_WIDTH);				//DL_SIZE
	InsertColumn(2,		EMPTY,	LVCFMT_LEFT,	DFLT_FILETYPE_COL_WIDTH);			//TYPE
	InsertColumn(3,		EMPTY,	LVCFMT_LEFT,	DFLT_PRIORITY_COL_WIDTH);			//PRIORITY
	InsertColumn(4,		EMPTY,	LVCFMT_LEFT,	DFLT_HASH_COL_WIDTH, -1, true);		//FILEID
	InsertColumn(5,		EMPTY,	LVCFMT_RIGHT,	100);								//SF_REQUESTS
	InsertColumn(6,		EMPTY,	LVCFMT_RIGHT,	100, -1, true);						//SF_ACCEPTS
	InsertColumn(7,		EMPTY,	LVCFMT_RIGHT,	120);								//SF_TRANSFERRED
	InsertColumn(8,		EMPTY,	LVCFMT_LEFT,	DFLT_PARTSTATUS_COL_WIDTH);			//SHARED_STATUS
	InsertColumn(9,		EMPTY,	LVCFMT_LEFT,	DFLT_FOLDER_COL_WIDTH, -1, true);	//FOLDER
	InsertColumn(10,	EMPTY,	LVCFMT_RIGHT,	60);								//COMPLSOURCES
	InsertColumn(11,	EMPTY,	LVCFMT_LEFT,	100);								//SHAREDTITLE
	InsertColumn(12,	EMPTY,	LVCFMT_LEFT,	DFLT_ARTIST_COL_WIDTH, -1, true);	//ARTIST
	InsertColumn(13,	EMPTY,	LVCFMT_LEFT,	DFLT_ALBUM_COL_WIDTH, -1, true);	//ALBUM
	InsertColumn(14,	EMPTY,	LVCFMT_LEFT,	DFLT_TITLE_COL_WIDTH, -1, true);	//TITLE
	InsertColumn(15,	EMPTY,	LVCFMT_RIGHT,	DFLT_LENGTH_COL_WIDTH, -1, true);	//LENGTH
	InsertColumn(16,	EMPTY,	LVCFMT_RIGHT,	DFLT_BITRATE_COL_WIDTH, -1, true);	//BITRATE
	InsertColumn(17,	EMPTY,	LVCFMT_LEFT,	DFLT_CODEC_COL_WIDTH, -1, true);	//CODEC
	InsertColumn(18,	EMPTY,	LVCFMT_RIGHT,	DFLT_LENGTH_COL_WIDTH);
	InsertColumn(19,	EMPTY,	LVCFMT_RIGHT,	DFLT_LENGTH_COL_WIDTH);

	SetAllIcons();
	LoadSettings();

	m_aSortBySecondValue[0] = true; // Requests:			Sort by 2nd value by default
	m_aSortBySecondValue[1] = true; // Accepted Requests:	Sort by 2nd value by default
	m_aSortBySecondValue[2] = true; // Transferred Data:	Sort by 2nd value by default
	m_aSortBySecondValue[3] = false; // Shared ED2K|Kad:	Sort by 1st value by default
	if (GetSortItem() >= 5 && GetSortItem() <= 7)
		m_aSortBySecondValue[GetSortItem() - 5] = GetSortSecondValue();
	else if (GetSortItem() == 11)
		m_aSortBySecondValue[3] = GetSortSecondValue();
	SetSortArrow();
	m_pSortParam = MAKELONG(GetSortItem() + (GetSortSecondValue() ? 100 : 0), !GetSortAscending());
	UpdateSortHistory(m_pSortParam); // This will save sort parameter history in m_liSortHistory which will be used when we call GetNextSortOrder.

	CToolTipCtrl *tooltip = GetToolTips();
	if (tooltip) {
		m_pToolTip->SetFileIconToolTip(true);
		m_pToolTip->SubclassWindow(*tooltip);
		tooltip->ModifyStyle(0, TTS_NOPREFIX);
		tooltip->SetDelayTime(TTDT_AUTOPOP, SEC2MS(20));
		tooltip->SetDelayTime(TTDT_INITIAL, SEC2MS(thePrefs.GetToolTipDelay()));
	}

	m_ShareDropTarget.SetParent(this);
	VERIFY(m_ShareDropTarget.Register(this));
}

void CSharedFilesCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
	SetAllIcons();
	CreateMenus();
}

void CSharedFilesCtrl::SetAllIcons()
{
	ApplyImageList(NULL);
	m_ImageList.DeleteImageList();
	m_ImageList.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	m_ImageList.Add(CTempIconLoader(_T("EMPTY"))); //0
	m_ImageList.Add(CTempIconLoader(_T("FileSharedServer"))); //1
	m_ImageList.Add(CTempIconLoader(_T("FileSharedKad"))); //2
	m_ImageList.Add(CTempIconLoader(_T("Rating_NotRated"))); //3
	m_ImageList.Add(CTempIconLoader(_T("Rating_Fake"))); //4
	m_ImageList.Add(CTempIconLoader(_T("Rating_Poor"))); //5
	m_ImageList.Add(CTempIconLoader(_T("Rating_Fair"))); //6
	m_ImageList.Add(CTempIconLoader(_T("Rating_Good"))); //7
	m_ImageList.Add(CTempIconLoader(_T("Rating_Excellent"))); //8
	m_ImageList.Add(CTempIconLoader(_T("Collection_Search"))); //9 rating for comments are searched on kad
	m_ImageList.SetOverlayImage(m_ImageList.Add(CTempIconLoader(_T("FileCommentsOvl"))), 1);
	// Apply the image list also to the listview control, even if we use our own 'DrawItem'.
	// This is needed to give the listview control a chance to initialize the row height.
	VERIFY(ApplyImageList(m_ImageList) == NULL);
	theApp.GetFileTypeSystemImageIdx(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR)); // This is just a dummy call to force it to set m_hSystemImageList.
}

void CSharedFilesCtrl::Localize()
{
	static const LPCTSTR uids[20] =
	{
		_T("DL_FILENAME"), _T("DL_SIZE"), _T("TYPE"), _T("PRIORITY"), _T("FILEID")
		, _T("SF_REQUESTS"), _T("SF_ACCEPTS"), _T("SF_TRANSFERRED"), _T("SHARED_STATUS"), _T("FOLDER")
		, _T("COMPLSOURCES"), _T("SHAREDTITLE"), _T("ARTIST"), _T("ALBUM"), _T("TITLE")
		, _T("LENGTH"), _T("BITRATE"), _T("CODEC")
		, _T("RATIO"), _T("RATIO_SESSION")
	};

	LocaliseHeaderCtrl(uids, _countof(uids));

	CreateMenus();

	for (int i = GetItemCount(); --i >= 0;)
		Update(i);

	ShowFilesCount();
}

void CSharedFilesCtrl::AddFile(CKnownFile* file)
{
	int m_iIndex = -1;
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->sharedfileswnd || !IsWindowVisible() || !file || IsFilteredOut(file) || (m_ListedItemsMap.Lookup(file, m_iIndex) && m_iIndex >= 0))
		return;

	// if we are in the file system view, this might be a CKnownFile which has to replace a CShareableFile
	// (in case we start sharing this file), so make sure to replace the old one instead of adding a new
	if (m_eFilter == FilterType::FileSystem) {
		for (POSITION pos = liTempShareableFilesInDir.GetHeadPosition(); pos != NULL;) {
			CShareableFile* pFileSharable = liTempShareableFilesInDir.GetNext(pos);
			CKnownFile* pfileKnown = static_cast<CKnownFile*>(pFileSharable);
			if (pfileKnown->GetFileSize() == file->GetFileSize() && pfileKnown->GetFilePath().CompareNoCase(file->GetFilePath()) == 0) {
				int m_iOldFileIndex = -1;
				if (m_ListedItemsMap.Lookup(pfileKnown, m_iOldFileIndex) && m_iOldFileIndex >= 0) {
					UpdateFile(pfileKnown, m_iOldFileIndex);
					ShowFilesCount();
					return;
				}
			}
		}
	} else
		// Don't save/reload list state if this is a file system view. Because all objects will be deleted and reloaded every time ReloadList is called.
		SaveListState(m_uFilterID, kSharedFilesViewState); // Save selections and scroll state

	auto it = std::lower_bound(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), file, SortFunc); // Find the position to insert the new value using lower_bound
	int m_iStartIndex = std::distance(m_ListedItemsVector.begin(), it);

	// Enlarge and update map starting from the found index to the end.
	if (m_iStartIndex >= 0) {
		SetRedraw(false); // Suspend painting
		m_ListedItemsVector.insert(it, file); // Insert the new value at the determined position
		RebuildListedItemsMap();
	} else { // This case is not expected, but handled for robustness.
		ReloadList(false, kSharedFilesViewState); // Something is wrong at this point, let's do a full reload instead having possible glitches or crashes.
		return;
	}

	UpdateSharedFilesItemCount(*this, m_ListedItemsVector.size()); // Set current count for virtual list.

	if (m_eFilter != FilterType::FileSystem) { // Don't save/reload list state if this is a file system view. Because all objects will be deleted and reloaded every time ReloadList is called.
		CSharedFilesSelectionRestoreGuard guard(*this);
		RestoreListState(m_uFilterID, kSharedFilesViewState, false); // Restore selections and scroll state
	}

	SetRedraw(true); // Resume painting
	RedrawItems(m_iStartIndex, m_ListedItemsVector.size()-1); // Redraw updated items.
	ShowFilesCount();
}

void CSharedFilesCtrl::RemoveFile(CKnownFile*file, const bool bDeletedFromDisk, const bool bWillReloadListLater)
{
	int m_iIndex = -1;
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->sharedfileswnd || !IsWindowVisible() || !file || !m_ListedItemsMap.Lookup(file, m_iIndex) || m_iIndex < 0)
		return;

	if (bWillReloadListLater) {
		// For this case we'll remove items only from the map and then we'll set the corresponding entries in the vector to NULL. They will be cleared later when ReloadList is called.
		if (m_ListedItemsMap.RemoveKey(file)) {
			// Keep the virtual row empty until ReloadList rebuilds the vector.
			m_ListedItemsVector[m_iIndex] = NULL;
			theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails(true);
		}
		return;
	}

	if (!bDeletedFromDisk && m_eFilter == FilterType::FileSystem) {
		// in the file system view we usually don't need to remove a file, if it becomes unshared it will
		// still be visible as its still in the file system and the knownfile object doesn't get deleted neither
		// so to avoid having to reload the whole list we just update it instead of removing and re-finding
		UpdateFile(file, true, bDeletedFromDisk, m_iIndex);
		return;
	}

	if (m_eFilter != FilterType::FileSystem)
		// Don't save/reload list state if this is a file system view. Because all objects will be deleted and reloaded every time ReloadList is called.
		SaveListState(m_uFilterID, kSharedFilesViewState); // Save selections and scroll state

	SetRedraw(false); // Suspend painting
	m_ListedItemsMap.RemoveKey(file); // Remove the item from the map
	m_ListedItemsVector.erase(m_ListedItemsVector.begin() + m_iIndex); // Remove the item from the vector.
	RebuildListedItemsMap();

	UpdateSharedFilesItemCount(*this, m_ListedItemsVector.size()); // Set current count for virtual list

	if (m_eFilter != FilterType::FileSystem) { // Don't save/reload list state if this is a file system view. Because all objects will be deleted and reloaded every time ReloadList is called.
		CSharedFilesSelectionRestoreGuard guard(*this);
		RestoreListState(m_uFilterID, kSharedFilesViewState, false); // Restore selections and scroll state
	}

	SetRedraw(true); // Resume painting
	RedrawItems(m_iIndex, m_ListedItemsVector.size()-1); // Redraw updated items.
	ShowFilesCount();
	theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails(true);
}

void CSharedFilesCtrl::UpdateFile(CKnownFile* file, const bool bUpdateFileSummary, const bool bDeletedFromDisk, const int iIndex)
{
	// Note: For thread safety, do not call this function directly from worker threads. Instead, post TM_SHAREDFILESCTRLUPDATEFILE message.

	int m_iIndex = iIndex;
	// If index isn't provided by the input parameter and also not found in m_ListedItemsMap
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->sharedfileswnd || !IsWindowVisible() || !file || (iIndex == -1 && !m_ListedItemsMap.Lookup(file, m_iIndex)) || m_iIndex < 0)
		return;

	if (m_iIndex >= static_cast<int>(m_ListedItemsVector.size()) || m_ListedItemsVector[m_iIndex] != file) {
		if (!m_ListedItemsMap.Lookup(file, m_iIndex) || m_iIndex < 0)
			return;
	}

	if (bDeletedFromDisk)
		RedrawItems(m_iIndex, m_ListedItemsVector.size()-1); // Redraw all items starting from the index of the deleted item if it was deleted from disk.
	else {
		bool bItemMoved = false;
		if (HasActiveSortOrder() && NeedsSortReposition(m_iIndex))
			bItemMoved = RepositionFileByCurrentSort(file, m_iIndex);

		if (!bItemMoved)
			Update(m_iIndex); // Redraw updated item.
		else if (!m_ListedItemsMap.Lookup(file, m_iIndex))
			m_iIndex = -1;
	}

	if (bUpdateFileSummary && m_iIndex >= 0 && GetItemState(m_iIndex, LVIS_SELECTED))
		theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails(false);
}

void CSharedFilesCtrl::RemoveFromHistory(CKnownFile* toRemove, const bool bWillReloadListLater) {
	if (theApp.IsClosing() || !toRemove)
		return;

	if (toRemove->IsKindOf(RUNTIME_CLASS(CPartFile)))
		theApp.emuledlg->transferwnd->GetDownloadList()->ClearCompleted(static_cast<CPartFile*>(toRemove));

	RemoveFile(toRemove, true, bWillReloadListLater); // We need to remove it first from virtual list, otherwise OnLvnGetDispInfo can try to query a deleted file and crashes.
	if (theApp.knownfiles)
		theApp.knownfiles->RemoveKnownFile(toRemove);
}

void CSharedFilesCtrl::ReloadList(const bool bSortCurrentList, const EListStateField LsfFlag)
{
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->sharedfileswnd || !IsWindowVisible())
		return;

	CWaitCursor curWait;
	CCKey bufKey;
	bool bInitializing = (m_iDataSize == -1); // Check if this is the first call to ReloadList

	// Initializing the vector and map
	if (bInitializing) {
		m_iDataSize = NextPrime(theApp.knownfiles->GetCount() + theApp.sharedfiles->GetCount() + 10000); // Any reasonable prime number for the initial size.
		m_ListedItemsVector.reserve(m_iDataSize);
		m_ListedItemsMap.InitHashTable(m_iDataSize);
	} else if (m_eFilter != FilterType::FileSystem) {
		// Don't save/reload list state if this is a file system view. Because all objects will be deleted and reloaded every time ReloadList is called.
		SaveListState(m_uFilterID, LsfFlag); // Save selections, sort and scroll values for the previous m_nResultsID if this is not the first call.
		m_uFilterID = GetFilterId();
	}

	SetRedraw(false); // Suspend painting

	if (!bSortCurrentList) {
		// Clear and reload data
		m_ListedItemsVector.clear();
		m_ListedItemsMap.RemoveAll();

		if (m_eFilter != FilterType::FileSystem || m_pDirectoryFilter == NULL || m_pDirectoryFilter->m_strFullPath.IsEmpty())
			DeleteTempShareableFilesList(liTempShareableFilesInDir);

		// List part files if "All Shared Files", "Incomplete Files" or "File History" root or branches selected. For the last case GetFileHistoryShowPart also need to be true.
		// m_pDirectoryFilter can be NULL while loading the window first time. So we need to consider this case, too.
		const CDirectoryItem* pSelectedTreeFilter = (theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL)
			? theApp.emuledlg->sharedfileswnd->m_ctlSharedDirTree.GetSelectedFilter()
			: NULL;
		const ESpecialDirectoryItems eSelectedTreeItemType = (pSelectedTreeFilter != NULL) ? pSelectedTreeFilter->m_eItemType : SDI_ALL;
		if ((eSelectedTreeItemType == SDI_ALL || eSelectedTreeItemType == SDI_TEMP) || (thePrefs.GetFileHistoryShowPart() && m_eFilter == FilterType::History)) {
			//Add all part files from download list. This way will include 0bytes parts too
			CArray<CPartFile*, CPartFile*> partlist;
			theApp.emuledlg->transferwnd->GetDownloadList()->GetDisplayedFiles(&partlist);
			for (INT_PTR i = 0; i < partlist.GetCount(); ++i) {
				CPartFile* pPartFile = partlist[i];
				if (pPartFile != NULL && !IsFilteredOut(pPartFile))
					m_ListedItemsVector.push_back(pPartFile);
			}
		}

		if (m_eFilter == FilterType::FileSystem && !m_pDirectoryFilter->m_strFullPath.IsEmpty()) {
			// File system view
			CFileFind ff;
			BOOL bFound = ff.FindFile(m_pDirectoryFilter->m_strFullPath + _T('*'));
			if (!bFound) {
				DeleteTempShareableFilesList(liTempShareableFilesInDir);
				DWORD dwError = ::GetLastError();
				if (dwError != ERROR_FILE_NOT_FOUND)
					DebugLogError(_T("Failed to find files for SharedFilesListCtrl in %s, %s"), (LPCTSTR)EscPercent(m_pDirectoryFilter->m_strFullPath), (LPCTSTR)EscPercent(GetErrorMessage(dwError)));
				SetRedraw(true); // Resume painting
				return;
			}

			CTempShareableFilesMap mapReusableTempFiles;
			while (!liTempShareableFilesInDir.IsEmpty()) {
				CShareableFile* pTempShareableFile = liTempShareableFilesInDir.RemoveHead();
				if (pTempShareableFile != NULL)
					mapReusableTempFiles.SetAt(BuildTempShareableFileKey(pTempShareableFile->GetFilePath()), pTempShareableFile);
			}

			do {
				bFound = ff.FindNextFile();
				if (ff.IsDirectory() || ff.IsSystem() || ff.IsTemporary() || ff.GetLength() == 0 || ff.GetLength() > MAX_EMULE_FILE_SIZE)
					continue;

				const CString& strFoundFileName(ff.GetFileName());
				const CString& strFoundFilePath(ff.GetFilePath());
				const CString& strFoundDirectory(strFoundFilePath.Left(ff.GetFilePath().ReverseFind(_T('\\')) + 1));
				ULONGLONG ullFoundFileSize = ff.GetLength();

				FILETIME tFoundFileTime = {};
				const bool bHasFoundFileTime = (ff.GetLastWriteTime(&tFoundFileTime) != FALSE);
				// ignore real(!) LNK files
				if (ExtensionIs(strFoundFileName, _T(".lnk"))) {
					SHFILEINFO info;
					if (::SHGetFileInfo(strFoundFilePath, 0, &info, sizeof info, SHGFI_ATTRIBUTES) && (info.dwAttributes & SFGAO_LINK))
						continue;
				}

				// ignore real(!) thumbs.db files -- seems that lot of ppl have 'thumbs.db' files without the 'System' file attribute
				if (IsThumbsDb(strFoundFilePath, strFoundFileName))
					continue;

				time_t fdate = (time_t)-1;
				if (bHasFoundFileTime) {
					fdate = (time_t)FileTimeToUnixTime(tFoundFileTime);
					if (fdate <= 0)
						fdate = (time_t)-1;
				}

				if (fdate == (time_t)-1) {
					if (thePrefs.GetVerbose())
						AddDebugLogLine(false, _T("Failed to get file date of \"%s\""), (LPCTSTR)EscPercent(strFoundFilePath));
				} else
					AdjustNTFSDaylightFileTime(fdate, strFoundFilePath);

				CShareableFile* pTempShareableFile = NULL;
				const CString strTempShareableFileKey(BuildTempShareableFileKey(strFoundFilePath));
				if (mapReusableTempFiles.Lookup(strTempShareableFileKey, pTempShareableFile) && CanReuseTempShareableFile(pTempShareableFile, ullFoundFileSize, bHasFoundFileTime, tFoundFileTime))
					mapReusableTempFiles.RemoveKey(strTempShareableFileKey);
				else
					pTempShareableFile = new CShareableFile();

				RefreshTempShareableFile(*pTempShareableFile, strFoundFilePath, strFoundFileName, strFoundDirectory, ullFoundFileSize, bHasFoundFileTime, tFoundFileTime);
				liTempShareableFilesInDir.AddTail(pTempShareableFile);
				CKnownFile* pKnownFile = reinterpret_cast<CKnownFile*>(pTempShareableFile);
				if (!IsFilteredOut(pKnownFile))
					m_ListedItemsVector.push_back(pKnownFile);
			} while (bFound);

			DeleteTempShareableFilesMap(mapReusableTempFiles);
		} else {
			// Determine root of the selected directory filter
			if (m_eFilter == FilterType::Shared || (thePrefs.GetFileHistoryShowShared() && m_eFilter == FilterType::History)) {
				// Shared files
				// Take a snapshot of shared files under write lock to avoid iterator invalidation
				CArray<CKnownFile*, CKnownFile*> arSharedSnapshot;
				{
					CSingleLock listlock(&theApp.sharedfiles->m_mutWriteList, TRUE);
					for (const CKnownFilesMap::CPair* pair = theApp.sharedfiles->m_Files_map.PGetFirstAssoc(); pair != NULL; pair = theApp.sharedfiles->m_Files_map.PGetNextAssoc(pair)) {
						if (pair->value)
							arSharedSnapshot.Add(pair->value);
					}
				}
				for (INT_PTR i = 0; i < arSharedSnapshot.GetCount(); ++i) {
					CKnownFile* pKF = arSharedSnapshot[i];
					// m_Files_map only contains part files with downloaded parts, we want to show all part files including 0bytes if GetFileHistoryShowPart is true, so exclude parts for this loop.
					if (pKF && !theApp.downloadqueue->IsPartFile(pKF) && !IsFilteredOut(pKF))
						m_ListedItemsVector.push_back(pKF);
				}
			}

			if (m_eFilter == FilterType::History) {
				// Known files
				for (POSITION pos = theApp.knownfiles->m_Files_map.GetStartPosition(); pos != NULL;) {
					CKnownFile* cur_file = NULL;
					theApp.knownfiles->m_Files_map.GetNextAssoc(pos, bufKey, cur_file);
					if (cur_file != NULL && !IsFilteredOut(cur_file) && theApp.sharedfiles->GetFileByID(cur_file->GetFileHash()) == NULL)
						m_ListedItemsVector.push_back(cur_file);
				}
			}

			if (m_eFilter == FilterType::Duplicate || (m_eFilter == FilterType::History && thePrefs.GetFileHistoryShowDuplicate())) {
				// Duplicate shared files
				CSingleLock slDuplicatesLock(&theApp.knownfiles->m_csDuplicatesLock, TRUE);
				for (auto&& duplicateFile : theApp.knownfiles->m_duplicateFileList)
					if (duplicateFile && !IsFilteredOut(duplicateFile))
						m_ListedItemsVector.push_back(duplicateFile);
			}
		}
	}

	// Reloading data completed at this point. Now we need to sort the vector.
	// Sort vector, then load sorted data to map and reverse map
	CombinedSort(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), SortFunc);
	RebuildListedItemsMap();

	UpdateSharedFilesItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list

	if (!bInitializing && m_eFilter != FilterType::FileSystem) { // Don't save/reload list state if this is a file system view. Because all objects will be deleted and reloaded every time ReloadList is called.
		CSharedFilesSelectionRestoreGuard guard(*this);
		RestoreListState(m_uFilterID, LsfFlag, false); // Restore selections, sort and scroll values if this is not the first call.
	}

	theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails(false);
	ShowFilesCount();
	SetRedraw(true); // Resume painting
	Invalidate(); //Force redraw
}

// Index map after vector changes
void CSharedFilesCtrl::RebuildListedItemsMap()
{
	m_ListedItemsMap.RemoveAll();
	for (int i = 0; i < static_cast<int>(m_ListedItemsVector.size()); ++i) {
		if (m_ListedItemsVector[i] != NULL) // Skip NULL entries that may exist temporarily during removal operations
			m_ListedItemsMap[m_ListedItemsVector[i]] = i;
	}
}

void CSharedFilesCtrl::UpdateListedItemsMapRange(int iStartIndex, int iEndIndex)
{
	if (iStartIndex < 0 || iEndIndex < iStartIndex || iStartIndex >= static_cast<int>(m_ListedItemsVector.size()))
		return;

	iEndIndex = min(iEndIndex, static_cast<int>(m_ListedItemsVector.size()) - 1);
	for (int i = iStartIndex; i <= iEndIndex; ++i) {
		if (m_ListedItemsVector[i] != NULL)
			m_ListedItemsMap[m_ListedItemsVector[i]] = i;
	}
}

bool CSharedFilesCtrl::SortFunc(const CKnownFile* first, const CKnownFile* second)
{
	return SortProc((LPARAM)first, (LPARAM)second, m_pSortParam) < 0; // If the first one has a smaller value returns true, otherwise returns false.
}

bool CSharedFilesCtrl::HasActiveSortOrder() const
{
	return (GetSortItem() != -1 && m_ListedItemsVector.size() >= 2);
}

bool CSharedFilesCtrl::NeedsSortReposition(const int iIndex) const
{
	if (iIndex < 0 || iIndex >= static_cast<int>(m_ListedItemsVector.size()))
		return false;

	const CKnownFile* pFile = m_ListedItemsVector[iIndex];
	if (pFile == NULL)
		return false;

	if (iIndex > 0) {
		const CKnownFile* pPrev = m_ListedItemsVector[iIndex - 1];
		if (pPrev != NULL && SortProc((LPARAM)pPrev, (LPARAM)pFile, m_pSortParam) > 0)
			return true;
	}

	if (iIndex + 1 < static_cast<int>(m_ListedItemsVector.size())) {
		const CKnownFile* pNext = m_ListedItemsVector[iIndex + 1];
		if (pNext != NULL && SortProc((LPARAM)pFile, (LPARAM)pNext, m_pSortParam) > 0)
			return true;
	}

	return false;
}

bool CSharedFilesCtrl::RepositionFileByCurrentSort(CKnownFile* file, const int iIndex)
{
	if (!file || iIndex < 0 || iIndex >= static_cast<int>(m_ListedItemsVector.size()) || m_ListedItemsVector[iIndex] != file)
		return false;

	const bool bMoveLeft = (iIndex > 0 && m_ListedItemsVector[iIndex - 1] != NULL && SortProc((LPARAM)m_ListedItemsVector[iIndex - 1], (LPARAM)file, m_pSortParam) > 0);
	const bool bMoveRight = (!bMoveLeft && iIndex + 1 < static_cast<int>(m_ListedItemsVector.size()) && m_ListedItemsVector[iIndex + 1] != NULL && SortProc((LPARAM)file, (LPARAM)m_ListedItemsVector[iIndex + 1], m_pSortParam) > 0);
	if (!bMoveLeft && !bMoveRight)
		return false;

	SharedFilesRepositionState savedState;
	CaptureSharedFilesRepositionState(*this, savedState);

	SetRedraw(false); // Suspend painting

	int iNewIndex = iIndex;
	if (bMoveLeft) {
		std::vector<CKnownFile*>::iterator itNew = std::lower_bound(m_ListedItemsVector.begin(), m_ListedItemsVector.begin() + iIndex, file, SortFunc);
		iNewIndex = static_cast<int>(std::distance(m_ListedItemsVector.begin(), itNew));
		std::rotate(itNew, m_ListedItemsVector.begin() + iIndex, m_ListedItemsVector.begin() + iIndex + 1);
	} else {
		std::vector<CKnownFile*>::iterator itNew = std::upper_bound(m_ListedItemsVector.begin() + iIndex + 1, m_ListedItemsVector.end(), file, SortFunc);
		iNewIndex = static_cast<int>(std::distance(m_ListedItemsVector.begin(), itNew)) - 1;
		std::rotate(m_ListedItemsVector.begin() + iIndex, m_ListedItemsVector.begin() + iIndex + 1, itNew);
	}

	const int iStartIndex = min(iIndex, iNewIndex);
	const int iEndIndex = max(iIndex, iNewIndex);
	UpdateListedItemsMapRange(iStartIndex, iEndIndex);

	{
		CSharedFilesSelectionRestoreGuard guard(*this);
		RestoreSharedFilesRepositionState(*this, savedState); // Restore selection, focus and exact viewport after row indexes changed.
	}

	SetRedraw(true); // Resume painting
	RedrawItems(iStartIndex, iEndIndex); // Redraw all rows whose data pointer changed.
	return true;
}

CObject* CSharedFilesCtrl::GetItemObject(int iIndex) const
{
	if (iIndex < 0 || iIndex >= m_ListedItemsVector.size())
		return nullptr;
	return m_ListedItemsVector[iIndex];
}

uint32 CSharedFilesCtrl::GetFilterId() const
{
	// We aim to differentiate different filter types here:
	uint retval = 0;
	const CDirectoryItem* pSelectedTreeFilter = (theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL)
		? theApp.emuledlg->sharedfileswnd->m_ctlSharedDirTree.GetSelectedFilter()
		: NULL;
	const ESpecialDirectoryItems eSelectedTreeItemType = (pSelectedTreeFilter != NULL) ? pSelectedTreeFilter->m_eItemType : SDI_ALL;
	if (eSelectedTreeItemType == SDI_ED2KFILETYPE) {
		if (m_pDirectoryFilter->m_nCatFilter == ED2KFT_OTHER)
			retval = (m_eFilter * 1000) + (99 * 100); // reserve 99 for "Other"
		else if (m_pDirectoryFilter->m_nCatFilter != -1)
			retval = (m_eFilter * 1000) + (m_pDirectoryFilter->m_nCatFilter * 100);
		else
			retval = (m_eFilter * 1000) + eSelectedTreeItemType;
	} else
		retval = (m_eFilter * 1000) + eSelectedTreeItemType;
	return retval;
}

void CSharedFilesCtrl::ShowFilesCount()
{
	if (theApp.IsClosing())
		return;

	CString m_strCount;
	m_strCount.Format(_T(":%Iu"), static_cast<size_t>(m_ListedItemsVector.size()));

	// Since sharedfilesctrl.SetAICHHashing is called directly from original code, we need to use this to get nAICHHashing count
	const LONG nVisibleAICHHashing = GetAICHHashing();
	const ULONGLONG uHashingCount = static_cast<ULONGLONG>(theApp.sharedfiles->GetHashingCount()) + (nVisibleAICHHashing > 0 ? static_cast<ULONGLONG>(nVisibleAICHHashing) : 0ULL);
	if (uHashingCount > 0) {
		const CString strHashingLabel = GetResString(_T("HASHING"));
		m_strCount.AppendFormat(_T(", %s:%I64u"), (LPCTSTR)strHashingLabel, uHashingCount);
	}

	if (theApp.sharedfiles->m_uMetadataUpdatingCount > 0) {
		const CString strUpdatingLabel = GetResString(_T("UPDATING"));
		m_strCount.AppendFormat(_T(", %s:%I64u"), (LPCTSTR)strUpdatingLabel, static_cast<ULONGLONG>(theApp.sharedfiles->m_uMetadataUpdatingCount));
	}

	if (m_eFilter == FilterType::History)
		theApp.emuledlg->sharedfileswnd->SetDlgItemText(IDC_TRAFFIC_TEXT, GetResString(_T("FILE_HISTORY")) + m_strCount);
	else if (m_eFilter == FilterType::Duplicate)
		theApp.emuledlg->sharedfileswnd->GetDlgItem(IDC_TRAFFIC_TEXT)->SetWindowText(GetResString(_T("DUPLICATE_FILES")) + m_strCount);
	else if (m_eFilter == FilterType::FileSystem)
		theApp.emuledlg->sharedfileswnd->GetDlgItem(IDC_TRAFFIC_TEXT)->SetWindowText(GetResString(_T("FILES")) + m_strCount);
	else
		theApp.emuledlg->sharedfileswnd->GetDlgItem(IDC_TRAFFIC_TEXT)->SetWindowText(GetResString(_T("SF_FILES")) + m_strCount);
}

void CSharedFilesCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	// for virtual lists, itemData is always nulluse dwItemSpec as the index
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

	CKnownFile* pKnownFile = m_ListedItemsVector[index];
	CShareableFile* file = NULL;
	if (pKnownFile != NULL)
		file = reinterpret_cast<CShareableFile*>(m_ListedItemsVector[index]);
	if (!file->IsKindOf(RUNTIME_CLASS(CKnownFile)))
		pKnownFile = NULL;

	const CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	int iCount = pHeaderCtrl->GetItemCount();
	LONG itemLeft = rcItem.left;
	int iIconDrawWidth = theApp.GetSmallSytemIconSize().cx;
	LONG iIconY = max((rcItem.Height() - theApp.GetSmallSytemIconSize().cy - 1) / 2, 0);
	for (int iCurrent = 0; iCurrent < iCount; ++iCurrent) {
		int iColumn = pHeaderCtrl->OrderToIndex(iCurrent);
		if (IsColumnHidden(iColumn))
			continue;

		UINT uDrawTextAlignment;
		int iColumnWidth = GetColumnWidth(iColumn, uDrawTextAlignment);
		rcItem.left = itemLeft;
		rcItem.right = itemLeft + iColumnWidth;
		if (rcItem.left < rcItem.right && HaveIntersection(rcClient, rcItem)) {
			const CString &sItem(GetItemDisplayText(file, iColumn));
			switch (iColumn) {
			case 0: //file name
				{
					rcItem.left += sm_iIconOffset;
					LONG rcIconTop = rcItem.top + iIconY;
					if (CheckBoxesEnabled()) {
						CHECKBOXSTATES iState;
						int iNoStyleState;
						// no interaction with shell linked files or default shared directories
						if ((file->IsShellLinked() && theApp.sharedfiles->ShouldBeShared(file->GetSharedDirectory(), file->GetFilePath(), false))
							|| (theApp.sharedfiles->ShouldBeShared(file->GetSharedDirectory(), file->GetFilePath(), true)))
						{
							iState = CBS_CHECKEDDISABLED;
							iNoStyleState = DFCS_CHECKED | DFCS_INACTIVE;
						} else if (theApp.sharedfiles->ShouldBeShared(file->GetSharedDirectory(), file->GetFilePath(), false)) {
							iState = (file == m_pHighlightedItem) ? CBS_CHECKEDHOT : CBS_CHECKEDNORMAL;
							iNoStyleState = (file == m_pHighlightedItem) ? DFCS_PUSHED | DFCS_CHECKED : DFCS_CHECKED;
						} else if (!thePrefs.IsShareableDirectory(file->GetPath())) {
							iState = CBS_UNCHECKEDDISABLED;
							iNoStyleState = DFCS_INACTIVE;
						} else {
							iState = (file == m_pHighlightedItem) ? CBS_UNCHECKEDHOT : CBS_UNCHECKEDNORMAL;
							iNoStyleState = (file == m_pHighlightedItem) ? DFCS_PUSHED : 0;
						}

						RECT rcCheckBox = { rcItem.left, rcIconTop, rcItem.left + 16, rcIconTop + 16 };
						dc.DrawFrameControl(&rcCheckBox, DFC_BUTTON, DFCS_BUTTONCHECK | iNoStyleState | DFCS_FLAT);
						rcItem.left += 16 + sm_iLabelOffset;
					}

					if (theApp.GetSystemImageList() != NULL) {
						int iImage = theApp.GetFileTypeSystemImageIdx(file->GetFileName());
						::ImageList_Draw(theApp.GetSystemImageList(), iImage, dc.GetSafeHdc(), rcItem.left, rcIconTop, ILD_TRANSPARENT);
					}

					if (!file->GetFileComment().IsEmpty() || file->GetFileRating()) //not rated
						SafeImageListDraw(&m_ImageList, dc, 0, POINT{ rcItem.left, rcIconTop }, ILD_NORMAL | INDEXTOOVERLAYMASK(1));

					rcItem.left += iIconDrawWidth + sm_iLabelOffset;
					if (thePrefs.ShowRatingIndicator() && (file->HasComment() || file->HasRating() || file->IsKadCommentSearchRunning())) {
							SafeImageListDraw(&m_ImageList, dc, 3 + file->UserRating(true), POINT{ rcItem.left, rcIconTop }, ILD_NORMAL);
						rcItem.left += 16 + sm_iLabelOffset;
					}
					rcItem.left -= sm_iSubItemInset;
				}
			default: //any text column
				rcItem.left += sm_iSubItemInset;
				rcItem.right -= sm_iSubItemInset;
				dc.DrawText(sItem, -1, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
				break;
			case 8: //shared parts bar
				if (pKnownFile != NULL && pKnownFile->GetPartCount()) {
					++rcItem.top;
					--rcItem.bottom;
					pKnownFile->DrawShareStatusBar(dc, &rcItem, false, thePrefs.UseFlatBar());
					++rcItem.bottom;
					--rcItem.top;
				}
				break;
			case 11: //shared ed2k/kad
				if (pKnownFile != NULL) {
					rcItem.left += sm_iIconOffset;
					POINT point = { rcItem.left, rcItem.top + iIconY };
					if (pKnownFile->GetPublishedED2K())
						SafeImageListDraw(&m_ImageList, dc, 1, point, ILD_NORMAL);
					if (IsSharedInKad(pKnownFile)) {
						point.x += 16 + sm_iSubItemInset;
							SafeImageListDraw(&m_ImageList, dc, IsSharedInKad(pKnownFile) ? 2 : 0, point, ILD_NORMAL);
					}
				}
			}
		}
		itemLeft += iColumnWidth;
	}

	DrawFocusRect(dc, &lpDrawItemStruct->rcItem, (lpDrawItemStruct->itemState & ODS_FOCUS) != 0, bCtrlFocused, (lpDrawItemStruct->itemState & ODS_SELECTED) != 0);
}

const CString CSharedFilesCtrl::GetItemDisplayText(const CShareableFile *file, const int iSubItem) const
{
	CString sText;
	switch (iSubItem) {
	case 0:
		return file->GetFileName();
	case 1:
		return CastItoXBytes((uint64)file->GetFileSize());
	case 2:
		return file->GetFileTypeDisplayStr();
	case 9:
		sText = file->GetPath();
		unslosh(sText);
		return sText;
	}

	if (file->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
		const CKnownFile *pKnownFile = static_cast<const CKnownFile*>(file);
		switch (iSubItem) {
		case 3:
			sText = pKnownFile->GetUpPriorityDisplayString();
			break;
		case 4:
			sText = md4str(pKnownFile->GetFileHash());
			break;
		case 5:
			sText.Format(_T("%u (%u)"), pKnownFile->statistic.GetRequests(), pKnownFile->statistic.GetAllTimeRequests());
			break;
		case 6:
			sText.Format(_T("%u (%u)"), pKnownFile->statistic.GetAccepts(), pKnownFile->statistic.GetAllTimeAccepts());
			break;
		case 7:
			sText.Format(_T("%s (%s)"), (LPCTSTR)CastItoXBytes(pKnownFile->statistic.GetTransferred()), (LPCTSTR)CastItoXBytes(pKnownFile->statistic.GetAllTimeTransferred()));
			break;
		case 8:
			sText.Format(_T("%u"), pKnownFile->GetPartCount());
			break;
		case 10:
			if (pKnownFile->m_nCompleteSourcesCountLo == pKnownFile->m_nCompleteSourcesCountHi)
				sText.Format(_T("%u"), pKnownFile->m_nCompleteSourcesCountLo);
			else if (pKnownFile->m_nCompleteSourcesCountLo == 0)
				sText.Format(_T("< %u"), pKnownFile->m_nCompleteSourcesCountHi);
			else
				sText.Format(_T("%u - %u"), pKnownFile->m_nCompleteSourcesCountLo, pKnownFile->m_nCompleteSourcesCountHi);
			break;
		case 11:
			sText.Format(_T("%s|%s"), (LPCTSTR)GetResString(pKnownFile->GetPublishedED2K() ? _T("YES") : _T("NO")), (LPCTSTR)GetResString(IsSharedInKad(pKnownFile) ? _T("YES") : _T("NO")));
			break;
		case 12:
			sText = pKnownFile->GetStrTagValue(FT_MEDIA_ARTIST);
			break;
		case 13:
			sText = pKnownFile->GetStrTagValue(FT_MEDIA_ALBUM);
			break;
		case 14:
			sText = pKnownFile->GetStrTagValue(FT_MEDIA_TITLE);
			break;
		case 15:
			{
				uint32 nMediaLength = pKnownFile->GetIntTagValue(FT_MEDIA_LENGTH);
				if (nMediaLength)
					sText = SecToTimeLength(nMediaLength);
			}
			break;
		case 16:
			{
				uint32 nBitrate = pKnownFile->GetIntTagValue(FT_MEDIA_BITRATE);
				if (nBitrate)
					sText.Format(_T("%u %s"), nBitrate, (LPCTSTR)GetResString(_T("KBITSSEC")));
			}
			break;
		case 17:
			sText = GetCodecDisplayName(pKnownFile->GetStrTagValue(FT_MEDIA_CODEC));
			break;
		case 18:
			sText.Format(_T("%.1f"), pKnownFile->GetAllTimeRatio());
			break;
		case 19:
			sText.Format(_T("%.1f"), pKnownFile->GetRatio());
			break;
		}
	}
	return sText;
}


void CSharedFilesCtrl::OnContextMenu(CWnd*, CPoint point)
{
	// get merged settings
	bool bFirstItem = true;
	bool bContainsShareableFiles = false;
	bool bContainsOnlyShareableFile = true;
	bool bContainsOnlyUnshareableFile = true;
	bool m_bAllInDownloadList = true;
	int iSelectedItems = GetSelectedCount();
	UINT uPrioMenuItem = 0;
	const CShareableFile *pSingleSelFile = NULL;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		int index = GetNextSelectedItem(pos);
		CKnownFile* cur_file = m_ListedItemsVector[index];
		if (cur_file == NULL)
			continue;
		const CShareableFile *pFile = reinterpret_cast<CShareableFile*>(cur_file);

		pSingleSelFile = bFirstItem ? pFile : NULL;

		if (!theApp.downloadqueue->GetFileByID(pFile->GetFileHash()))
			m_bAllInDownloadList = false;

		bContainsOnlyUnshareableFile = bContainsOnlyUnshareableFile && pFile && !pFile->IsShellLinked() && !pFile->IsPartFile() && (theApp.sharedfiles->ShouldBeShared(pFile->GetSharedDirectory(), pFile->GetFilePath(), false)
			&& !theApp.sharedfiles->ShouldBeShared(pFile->GetSharedDirectory(), pFile->GetFilePath(), true));

		if (pFile && pFile->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
			if (pFile->GetFilePath().GetLength() == 0)
				bContainsOnlyShareableFile = false;
			UINT uCurPrioMenuItem = 0;
			if (static_cast<const CKnownFile*>(pFile)->IsAutoUpPriority())
				uCurPrioMenuItem = MP_PRIOAUTO;
			else
				switch (static_cast<const CKnownFile*>(pFile)->GetUpPriority()) {
				case PR_VERYLOW:
					uCurPrioMenuItem = MP_PRIOVERYLOW;
					break;
				case PR_LOW:
					uCurPrioMenuItem = MP_PRIOLOW;
					break;
				case PR_NORMAL:
					uCurPrioMenuItem = MP_PRIONORMAL;
					break;
				case PR_HIGH:
					uCurPrioMenuItem = MP_PRIOHIGH;
					break;
				case PR_VERYHIGH:
					uCurPrioMenuItem = MP_PRIOVERYHIGH;
					break;
				default:
					ASSERT(0);
				}

			if (bFirstItem)
				uPrioMenuItem = uCurPrioMenuItem;
			else if (uPrioMenuItem != uCurPrioMenuItem)
				uPrioMenuItem = 0;
		} else
			bContainsShareableFiles = true;

		bFirstItem = false;
	}

	bool m_bContainsSharedFile = false;
	bool m_bContainsNotSharedFile = false;
	bool m_bContainsPartFile = false;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		int index = GetNextSelectedItem(pos);
		if (index >= 0) {
			CKnownFile* cur_file = m_ListedItemsVector[index];
			if (cur_file != NULL) {
				if (theApp.sharedfiles->GetFileByID(cur_file->GetFileHash()) != NULL && !theApp.knownfiles->IsOnDuplicates(cur_file->GetFileName(), cur_file->GetUtcFileDate(), cur_file->GetFileSize()))
					m_bContainsSharedFile = true;
				else
					m_bContainsNotSharedFile = true;

				if (cur_file->IsPartFile())
					m_bContainsPartFile = true;
			} else
				m_bContainsNotSharedFile = true;
		}
	}

	bool bSingleCompleteFileSelected = (iSelectedItems == 1 && (!m_bContainsPartFile || bContainsOnlyShareableFile));

	if (thePrefs.GetFileHistoryShowPart())
		m_SharedFilesMenu.CheckMenuItem(MP_VIEWPARTFILES, MF_CHECKED);
	else
		m_SharedFilesMenu.CheckMenuItem(MP_VIEWPARTFILES, MF_UNCHECKED);

	if (thePrefs.GetFileHistoryShowShared())
		m_SharedFilesMenu.CheckMenuItem(MP_VIEWSHAREDFILES, MF_CHECKED);
	else
		m_SharedFilesMenu.CheckMenuItem(MP_VIEWSHAREDFILES, MF_UNCHECKED);

	if (thePrefs.GetFileHistoryShowDuplicate())
		m_SharedFilesMenu.CheckMenuItem(MP_VIEWDUPLICATEFILES, MF_CHECKED);
	else
		m_SharedFilesMenu.CheckMenuItem(MP_VIEWDUPLICATEFILES, MF_UNCHECKED);

	m_SharedFilesMenu.EnableMenuItem(MP_OPEN, (iSelectedItems == 1 && ((m_eFilter != FilterType::History && !m_bContainsPartFile) || (m_eFilter == FilterType::History && !m_bContainsPartFile && m_bContainsSharedFile))) ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_REMOVEFROMHISTORY, (iSelectedItems != 0 && m_eFilter == FilterType::History && !m_bContainsPartFile && !m_bContainsSharedFile) ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_CANCEL, iSelectedItems > 0 && m_bAllInDownloadList ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_CANCEL_FORGET, iSelectedItems > 0 && m_bAllInDownloadList ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_VIEWPARTFILES, m_eFilter == FilterType::History ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_VIEWSHAREDFILES, m_eFilter == FilterType::History ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_VIEWDUPLICATEFILES, m_eFilter == FilterType::History ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem((UINT)m_FileHistorysMenu.m_hMenu, m_eFilter == FilterType::History ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem((UINT)m_PrioMenu.m_hMenu, (iSelectedItems != 0 && ((m_eFilter != FilterType::Duplicate && m_eFilter != FilterType::History && !m_bContainsNotSharedFile && m_bContainsSharedFile) || (m_eFilter == FilterType::History && m_bContainsSharedFile && bContainsOnlyShareableFile))) ? MF_ENABLED : MF_GRAYED);
	m_PrioMenu.CheckMenuRadioItem(MP_PRIOVERYLOW, MP_PRIOAUTO, uPrioMenuItem, 0);

	UINT uInsertedMenuItem = 0;
	static const TCHAR _szSkinPkgSuffix1[] = _T(".") EMULSKIN_BASEEXT _T(".zip");
	static const TCHAR _szSkinPkgSuffix2[] = _T(".") EMULSKIN_BASEEXT _T(".rar");
	if (bSingleCompleteFileSelected
		&& pSingleSelFile
		&& (pSingleSelFile->GetFilePath().Right(_countof(_szSkinPkgSuffix1) - 1).CompareNoCase(_szSkinPkgSuffix1) == 0
			|| pSingleSelFile->GetFilePath().Right(_countof(_szSkinPkgSuffix2) - 1).CompareNoCase(_szSkinPkgSuffix2) == 0))
	{
		MENUITEMINFO mii = {};
		mii.cbSize = (UINT)sizeof mii;
		mii.fMask = MIIM_TYPE | MIIM_STATE | MIIM_ID;
		mii.fType = MFT_STRING;
		mii.fState = MFS_ENABLED;
		mii.wID = MP_INSTALL_SKIN;
		const CString &strBuff(GetResString(_T("INSTALL_SKIN")));
		mii.dwTypeData = const_cast<LPTSTR>((LPCTSTR)strBuff);
		if (m_SharedFilesMenu.InsertMenuItem(MP_OPENFOLDER, &mii, FALSE))
			uInsertedMenuItem = mii.wID;
	}

	m_SharedFilesMenu.EnableMenuItem(MP_OPENFOLDER, (iSelectedItems == 1 && ((m_eFilter != FilterType::History && !m_bContainsPartFile) || (m_eFilter == FilterType::History && !m_bContainsPartFile && m_bContainsSharedFile))) ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_RENAME, (iSelectedItems == 1 && ((m_eFilter != FilterType::History && !m_bContainsPartFile && m_bContainsSharedFile && bSingleCompleteFileSelected) || (m_eFilter == FilterType::History && !m_bContainsPartFile))) ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_REMOVE, (iSelectedItems != 0 && ((m_eFilter != FilterType::History && !m_bContainsPartFile) || (m_eFilter == FilterType::History && !m_bContainsPartFile && m_bContainsSharedFile && bContainsOnlyShareableFile))) ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_UPDATE_METADATA, (iSelectedItems != 0 && ((m_eFilter != FilterType::History && !m_bContainsNotSharedFile && m_bContainsSharedFile) || (m_eFilter == FilterType::History && !m_bContainsPartFile && m_bContainsSharedFile && bContainsOnlyShareableFile))) ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_UNSHAREFILE, (iSelectedItems != 0 && (((m_eFilter != FilterType::History && bContainsOnlyUnshareableFile) || (m_eFilter == FilterType::History && bContainsOnlyUnshareableFile && !m_bContainsNotSharedFile && m_bContainsSharedFile)))) ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.SetDefaultItem(bSingleCompleteFileSelected ? MP_OPEN : -1);
	m_SharedFilesMenu.EnableMenuItem(MP_CMT, (!bContainsShareableFiles && iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_DETAIL, iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_SHOWED2KLINK, (iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_CUT, (iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED);
	if (thePrefs.GetShowCopyEd2kLinkCmd())
		m_SharedFilesMenu.EnableMenuItem(MP_GETED2KLINK, (iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_FIND, !m_ListedItemsVector.empty() ? MF_ENABLED : MF_GRAYED);

	const CCollection *coll = pSingleSelFile ? static_cast<const CKnownFile*>(pSingleSelFile)->m_pCollection : NULL;
	m_CollectionsMenu.EnableMenuItem(MP_MODIFYCOLLECTION, (!bContainsShareableFiles && coll != NULL) ? MF_ENABLED : MF_GRAYED);
	m_CollectionsMenu.EnableMenuItem(MP_VIEWCOLLECTION, (!bContainsShareableFiles && coll != NULL) ? MF_ENABLED : MF_GRAYED);
	m_CollectionsMenu.EnableMenuItem(MP_SEARCHAUTHOR, (!bContainsShareableFiles && coll != NULL && !coll->GetAuthorKeyHashString().IsEmpty()) ? MF_ENABLED : MF_GRAYED);
#if defined(_DEBUG)
	if (thePrefs.IsExtControlsEnabled()) {
		//JOHNTODO: Not for release as we need kad lowID users in the network to see how well this work. Also, we do not support these links yet.
		bool bEnable = (iSelectedItems > 0 && theApp.IsConnected() && theApp.IsFirewalled() && theApp.clientlist->GetServingBuddy());
		m_SharedFilesMenu.EnableMenuItem(MP_GETKADSOURCELINK, (bEnable ? MF_ENABLED : MF_GRAYED));
	}
#endif
	m_SharedFilesMenu.EnableMenuItem(Irc_SetSendLink, (iSelectedItems == 1 && theApp.emuledlg->ircwnd->IsConnected()) ? MF_ENABLED : MF_GRAYED);

	CMenuXP WebMenu;
	WebMenu.CreateMenu();
	int iWebMenuEntries = theWebServices.GetFileMenuEntries(&WebMenu);
	UINT flag2 = (iWebMenuEntries == 0 || iSelectedItems == 0) ? MF_GRAYED : MF_STRING;
	m_SharedFilesMenu.AppendMenu(flag2 | MF_POPUP, (UINT_PTR)WebMenu.m_hMenu, GetResString(_T("WEBSERVICES")), _T("WEB"));

	GetPopupMenuPos(*this, point);
	m_SharedFilesMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);

	m_SharedFilesMenu.RemoveMenu(m_SharedFilesMenu.GetMenuItemCount() - 1, MF_BYPOSITION);
	VERIFY(WebMenu.DestroyMenu());
	if (uInsertedMenuItem)
		VERIFY(m_SharedFilesMenu.RemoveMenu(uInsertedMenuItem, MF_BYCOMMAND));
}

BOOL CSharedFilesCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	wParam = LOWORD(wParam);

	CKnownFile* pKnownFile = NULL;
	bool m_bFirstFile = true;
	CTypedPtrList<CPtrList, CShareableFile*> selectedList;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		int index = GetNextSelectedItem(pos);
		if (index >= 0) {
			CKnownFile* cur_file = m_ListedItemsVector[index];
			if (cur_file != NULL) {
				selectedList.AddTail(reinterpret_cast<CShareableFile*>(cur_file));
				if (m_bFirstFile && selectedList.GetCount() == 1) {
					pKnownFile = cur_file;
					m_bFirstFile = false;
				}
			}
		}
	}

	if (wParam == MP_VIEWPARTFILES || wParam == MP_VIEWSHAREDFILES || wParam == MP_VIEWDUPLICATEFILES || wParam == MP_CREATECOLLECTION || wParam == MP_CLEARHISTORY || wParam == MP_FIND || !selectedList.IsEmpty()) {
		CShareableFile* file = (selectedList.GetCount() == 1) ? selectedList.GetHead() : NULL;
		bool m_bAddToCanceledMet = true;


		switch (wParam) {
		case Irc_SetSendLink:
			if (pKnownFile != NULL)
				theApp.emuledlg->ircwnd->SetSendFileString(pKnownFile->GetED2kLink());
			break;
		case MP_CUT:
			{
				CString m_strFileNames;
				for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
					int index = GetNextSelectedItem(pos);
					if (index >= 0) {
						CKnownFile* pFile = m_ListedItemsVector[index];
						if (pFile != NULL) {
							if (!m_strFileNames.IsEmpty())
								m_strFileNames += _T("\r\n");
							m_strFileNames += pFile->GetFileName();
						}
					}
				}

				if (!m_strFileNames.IsEmpty()) {
					theApp.CopyTextToClipboard(m_strFileNames);
					theApp.emuledlg->statusbar->SetText(GetResString(_T("FILE_NAME_COPIED_TO_CLIPBOARD")), SBarLog, 0);
				}
			}
			break;
		case MP_COPYSELECTED:
		case MP_GETED2KLINK:
		{
				CString str;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					CKnownFile *pfile = static_cast<CKnownFile*>(selectedList.GetNext(pos));
					if (pfile != NULL && pfile->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
						if (!str.IsEmpty())
							str += _T("\r\n");
						str += pfile->GetED2kLink();
					}
				}
				if (!str.IsEmpty()) {
					theApp.CopyTextToClipboard(str);
					theApp.emuledlg->statusbar->SetText(GetResString(_T("ED2K_LINK_COPIED_TO_CLIPBOARD")), SBarLog, 0);
				}
			}
			break;
#if defined(_DEBUG)
		//JOHNTODO: Not for release as we need kad lowID users in the network to see how well this works. Also, we do not support these links yet.
		case MP_GETKADSOURCELINK:
			{
				CString str;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					const CKnownFile *pfile = static_cast<CKnownFile*>(selectedList.GetNext(pos));
					if (pfile->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
						if (!str.IsEmpty())
							str += _T("\r\n");
						str += theApp.CreateKadSourceLink(pfile);
					}
				}
				theApp.CopyTextToClipboard(str);
			}
			break;
#endif
		// file operations
		case MP_OPEN:
#if TEST_FRAMEGRABBER //see also FrameGrabThread::GrabFrames
			if (file) {
				CKnownFile *previewFile = theApp.sharedfiles->GetFileByID(file->GetFileHash());
				if (previewFile != NULL)
					previewFile->GrabImage(4, 15, true, 450, this);
				break;
			}
#endif
		case IDA_ENTER:
			if (file && !file->IsPartFile())
				OpenFile(file);
			break;
		case MP_INSTALL_SKIN:
			if (file && !file->IsPartFile())
				InstallSkin(file->GetFilePath());
			break;
		case MP_OPENFOLDER:
			if (file && !file->IsPartFile()) {
				CString sParam;
				sParam.Format(_T("/select,\"%s\""), (LPCTSTR)file->GetFilePath());
				ShellOpen(_T("explorer"), sParam);
			}
			break;
		case MP_RENAME:
		case MPG_F2:
			if (pKnownFile && !pKnownFile->IsPartFile()) {
				InputBox inputbox;
				inputbox.SetLabels(GetResNoAmp(_T("RENAME")), GetResString(_T("DL_FILENAME")), pKnownFile->GetFileName());
				inputbox.SetEditFilenameMode();
				inputbox.DoModal();
				const CString &newname(inputbox.GetInput());
				if (!inputbox.WasCancelled() && !newname.IsEmpty()) {
					// at least prevent users from specifying something like "..\dir\file"
					if (newname.FindOneOf(sBadFileNameChar) >= 0) {
						CDarkMode::MessageBox(GetErrorMessage(ERROR_BAD_PATHNAME));
						break;
					}

					CString newpath(pKnownFile->GetPath());
					if (!newpath.IsEmpty() && newpath[newpath.GetLength() - 1] != _T('\\'))
						newpath += _T('\\');

					newpath += newname;
					bool bSharedFile = (theApp.sharedfiles->GetFileByID(pKnownFile->GetFileHash()) != NULL);
					CString oldpath;
					if (bSharedFile) {
						const CString src = pKnownFile->GetFilePath();
						const CString dst = newpath;
						const CString lsrc = PreparePathForWin32LongPath(src);
						const CString ldst = PreparePathForWin32LongPath(dst);
						oldpath = src;

						if (!::MoveFileEx(lsrc, ldst, MOVEFILE_COPY_ALLOWED)) {
							CString strError;
							strError.Format(GetResString(_T("ERR_RENAMESF")), (LPCTSTR)src, (LPCTSTR)dst, (LPCTSTR)EscPercent(GetErrorMessage(::GetLastError())));
							CDarkMode::MessageBox(strError);
							break;
						}
					}

					if (pKnownFile->IsKindOf(RUNTIME_CLASS(CPartFile))) {
						pKnownFile->SetFileName(newname);
						static_cast<CPartFile*>(pKnownFile)->SetFullName(newpath);
					} else {
						pKnownFile->SetFileName(newname);
					}

					if (bSharedFile) {
						pKnownFile->SetFilePath(newpath);
						theApp.sharedfiles->UpdateSharedPathCache(pKnownFile, oldpath);
					}
					UpdateFile(pKnownFile);
				}
			} else
				MessageBeep(MB_OK);
			break;
		case MP_REMOVE:
		case MPG_DELETE:
			{
				if (pKnownFile && (pKnownFile->IsPartFile() || (m_eFilter == FilterType::History && theApp.sharedfiles->GetFileByID(pKnownFile->GetFileHash()) == NULL)))
					break;

				if (IDNO == LocMessageBox(_T("CONFIRM_FILEDELETE"), MB_ICONWARNING | MB_DEFBUTTON2 | MB_YESNO, 0))
					return TRUE;

				// Shared Files bulk removals may still use one ReloadList, but we also batch list state to avoid restoring large selections per item.
				const bool bWillReloadListLater = (selectedList.GetCount() > 1);
				const bool bBatchListState = (bWillReloadListLater && m_eFilter != FilterType::FileSystem);
				const uint32 uListStateID = m_uFilterID;
				if (bBatchListState)
					BeginListStateBatch(uListStateID, kSharedFilesViewState);

				SetRedraw(false);
				bool bRemovedItems = false;
				while (!selectedList.IsEmpty()) {
					CShareableFile *myfile = selectedList.RemoveHead();
					if (!myfile || myfile->IsPartFile())
						continue;

					bool delsucc = ShellDeleteFile(myfile->GetFilePath(), false);
					if (delsucc) {
						if (myfile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
							theApp.sharedfiles->RemoveFile(static_cast<CKnownFile*>(myfile), true, bWillReloadListLater);
						else
							RemoveFile(static_cast<CKnownFile*>(myfile), true, bWillReloadListLater);
						bRemovedItems = true;
						if (myfile->IsKindOf(RUNTIME_CLASS(CPartFile)))
							theApp.emuledlg->transferwnd->GetDownloadList()->ClearCompleted(static_cast<CPartFile*>(myfile));
					} else {
						CString strError;
						strError.Format(GetResString(_T("ERR_DELFILE")), (LPCTSTR)myfile->GetFilePath());
						strError.AppendFormat(_T("\r\n\r\n%s"), (LPCTSTR)EscPercent(GetErrorMessage(GetLastError())));
						CDarkMode::MessageBox(strError);
					}
				}
				SetRedraw(true);
				if (bRemovedItems && bWillReloadListLater)
					ReloadList(false, kSharedFilesViewState);
				if (bBatchListState)
					EndListStateBatch(uListStateID, kSharedFilesViewState, false, LRP_RestoreSingleSelection);
				if (bRemovedItems) {
					AutoSelectItem();
					// Depending on <no-idea> this does not always cause an LVN_ITEMACTIVATE
					// message to be sent. So, explicitly redraw the item.
					theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails();
					theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged(); // might have been a single shared file
				}
			}
			break;
		case MP_CANCEL_FORGET:
			m_bAddToCanceledMet = false;
		case MP_CANCEL:
			{
				CWaitCursor curWait;
				if (selectedList.GetCount() > 0) {
					CString fileList(GetResString(selectedList.GetCount() == 1 ? _T("Q_CANCELDL2") : _T("Q_CANCELDL")));
					bool validdelete = false;
					bool removecompl = false;
					int cFiles = 0;
					const int iMaxDisplayFiles = 10;
					for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
						file = selectedList.GetNext(pos);
						const CPartFile* cur_file = theApp.downloadqueue->GetFileByID(file->GetFileHash());
						if (cur_file == NULL)
							continue;
						if (cur_file->GetStatus() != PS_COMPLETING && cur_file->GetStatus() != PS_COMPLETE) {
							validdelete = true;
							if (++cFiles < iMaxDisplayFiles)
								fileList.AppendFormat(_T("\n%s"), (LPCTSTR)cur_file->GetFileName());
							else if (cFiles == iMaxDisplayFiles && pos != NULL)
								fileList += _T("\n...");
						} else if (cur_file->GetStatus() == PS_COMPLETE)
							removecompl = true;
					}

					if ((removecompl && !validdelete) || (validdelete && CDarkMode::MessageBox(fileList, MB_DEFBUTTON2 | MB_ICONQUESTION | MB_YESNO) == IDYES)) {
						const bool bBatchListState = (selectedList.GetCount() > 1 && m_eFilter != FilterType::FileSystem);
						const uint32 uListStateID = m_uFilterID;
						bool bRemovedItems = false;
						if (bBatchListState)
							BeginListStateBatch(uListStateID, kSharedFilesViewState);
						for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
							file = selectedList.GetNext(pos);
							CPartFile* partfile = theApp.downloadqueue->GetFileByID(file->GetFileHash());
							if (partfile == NULL)
								continue;
							theApp.emuledlg->transferwnd->GetDownloadList()->HideSources(partfile);
							switch (partfile->GetStatus()) {
							case PS_WAITINGFORHASH:
							case PS_HASHING:
							case PS_COMPLETING:
								break;
							case PS_COMPLETE:
							{
								bool delsucc = ShellDeleteFile(partfile->GetFilePath());
								if (delsucc) {
									theApp.sharedfiles->RemoveFile(partfile, true);
									bRemovedItems = true;
								} else {
									CString strError;
									strError.Format(GetResString(_T("ERR_DELFILE")) + _T("\r\n\r\n%s"), (LPCTSTR)partfile->GetFilePath(), (LPCTSTR)EscPercent(GetErrorMessage(::GetLastError())));
									CDarkMode::MessageBox(strError);
								}

								theApp.emuledlg->transferwnd->GetDownloadList()->RemoveFile(partfile);
								break;
							}
							default:
								if (partfile->GetCategory())
									theApp.downloadqueue->StartNextFileIfPrefs(partfile->GetCategory());
							case PS_PAUSED:
							{
								uchar aucFileHash[MDX_DIGEST_SIZE];
								md4cpy(aucFileHash, partfile->GetFileHash());
								partfile->DeletePartFile(m_bAddToCanceledMet);
								if (theApp.downloadqueue->GetFileByID(aucFileHash) == NULL)
									bRemovedItems = true;
								break;
							}
							}
						}
						if (bBatchListState)
							EndListStateBatch(uListStateID, kSharedFilesViewState, false, LRP_RestoreSingleSelection);
						if (bRemovedItems) {
							AutoSelectItem();
							theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails(true);
							theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged();
							theApp.emuledlg->transferwnd->UpdateCatTabTitles();
						}
					}
				}
			}
		break;
		case MP_UNSHAREFILE:
			{
				SetRedraw(false);
				bool bUnsharedItems = false;
				while (!selectedList.IsEmpty()) {
					CShareableFile* myfile = selectedList.RemoveHead();
					if (myfile && !myfile->IsPartFile() && theApp.sharedfiles->ShouldBeShared(myfile->GetPath(), myfile->GetFilePath(), false)
						&& !theApp.sharedfiles->ShouldBeShared(myfile->GetPath(), myfile->GetFilePath(), true))
					{
						bUnsharedItems |= theApp.sharedfiles->ExcludeFile(myfile->GetFilePath());
						ASSERT(bUnsharedItems);
						if (bUnsharedItems && myfile->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
							CKnownFile* pfile = static_cast<CKnownFile*>(myfile);
							UpdateFile(pfile);
						}
					}
				}
				SetRedraw(true);
				if (bUnsharedItems) {
					theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails();
					theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged();
					if (GetFirstSelectedItemPosition() == NULL)
						AutoSelectItem();
				}
			}
			break;
		case MP_UPDATE_METADATA:
		{
			while (!selectedList.IsEmpty()) {
				CShareableFile* myfile = selectedList.RemoveHead();
				if (!myfile || myfile->IsPartFile())
					continue;
				if (myfile->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
					CKnownFile* pfile = static_cast<CKnownFile*>(myfile);
					pfile->UpdateMetaDataTags();
					UpdateFile(pfile);
				}
			}
		}
		break;
		case MP_CMT:
			ShowFileDialog(selectedList, IDD_COMMENT);
			break;
		case MPG_ALTENTER:
		case MP_DETAIL:
			ShowFileDialog(selectedList);
			break;
		case MP_FIND:
			OnFindStart();
			break;
		case MP_CREATECOLLECTION:
			{
				CCollection *pCollection = new CCollection();
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					CShareableFile *pFile = selectedList.GetNext(pos);
					if (pFile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
						pCollection->AddFileToCollection(pFile, true);
				}
				CCollectionCreateDialog dialog;
				dialog.SetCollection(pCollection, true);
				dialog.DoModal();
				//We delete this collection object because when the newly created
				//collection file is added to the shared file list, it is read and verified
				//and which creates the collection object that is attached to that file.
				delete pCollection;
			}
			break;
		case MP_SEARCHAUTHOR:
			if (pKnownFile && pKnownFile->m_pCollection) {
				SSearchParams *pParams = new SSearchParams;
				pParams->strExpression = pKnownFile->m_pCollection->GetCollectionAuthorKeyString();
				pParams->eType = SearchTypeKademlia;
				pParams->strFileType = _T(ED2KFTSTR_EMULECOLLECTION);
				pParams->strSpecialTitle = pKnownFile->m_pCollection->m_sCollectionAuthorName;
				if (pParams->strSpecialTitle.GetLength() > 50) {
					pParams->strSpecialTitle.Truncate(50);
					pParams->strSpecialTitle += _T("...");
				}

				theApp.emuledlg->searchwnd->m_pwndResults->StartSearch(pParams);
			}
			break;
		case MP_VIEWCOLLECTION:
			if (pKnownFile && pKnownFile->m_pCollection) {
				CCollectionViewDialog dialog;
				dialog.SetCollection(pKnownFile->m_pCollection);
				dialog.DoModal();
			}
			break;
		case MP_MODIFYCOLLECTION:
			if (pKnownFile && pKnownFile->m_pCollection) {
				CCollectionCreateDialog dialog;
				CCollection *pCollection = new CCollection(pKnownFile->m_pCollection);
				dialog.SetCollection(pCollection, false);
				dialog.DoModal();
				delete pCollection;
			}
			break;
		case MP_SHOWED2KLINK:
			ShowFileDialog(selectedList, IDD_ED2KLINK);
			break;
		case MP_PRIOVERYLOW:
		case MP_PRIOLOW:
		case MP_PRIONORMAL:
		case MP_PRIOHIGH:
		case MP_PRIOVERYHIGH:
		case MP_PRIOAUTO:
			for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
				CKnownFile *pfile = static_cast<CKnownFile*>(selectedList.GetNext(pos));
				if (pfile->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
					pfile->SetAutoUpPriority(wParam == MP_PRIOAUTO);
					switch (wParam) {
					case MP_PRIOVERYLOW:
						pfile->SetUpPriority(PR_VERYLOW);
						break;
					case MP_PRIOLOW:
						pfile->SetUpPriority(PR_LOW);
						break;
					case MP_PRIONORMAL:
						pfile->SetUpPriority(PR_NORMAL);
						break;
					case MP_PRIOHIGH:
						pfile->SetUpPriority(PR_HIGH);
						break;
					case MP_PRIOVERYHIGH:
						pfile->SetUpPriority(PR_VERYHIGH);
						break;
					case MP_PRIOAUTO:
						pfile->UpdateAutoUpPriority();
					}
					UpdateFile(pfile);
				}
			}
			break;
			case MP_REMOVEFROMHISTORY:
			{
				if (selectedList.IsEmpty() || CDarkMode::MessageBox(GetResString(_T("FILE_HISTORY_REMOVE_QUESTION")),	MB_YESNO | MB_ICONQUESTION) == IDNO) 
					break;

				const bool bMultipleFiles = (selectedList.GetCount() > 1);
				const bool bBatchListState = (bMultipleFiles && m_eFilter != FilterType::FileSystem);
				const uint32 uListStateID = m_uFilterID;
				if (bBatchListState)
					BeginListStateBatch(uListStateID, kSharedFilesViewState);

				while (!selectedList.IsEmpty()) {
					CShareableFile* myfile = selectedList.RemoveHead();
					if (myfile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
						RemoveFromHistory(static_cast<CKnownFile*>(myfile), bMultipleFiles); // For multiple selected files we'll only set the item to NULL in map m_ListedItemsVector and reload at the end. This way will be faster.
				}

				if (bMultipleFiles)
					ReloadList(false, kSharedFilesViewState);
				if (bBatchListState)
					EndListStateBatch(uListStateID, kSharedFilesViewState, false, LRP_RestoreSingleSelection);
			}
			break;
			case MP_CLEARHISTORY:
			{
				if (CDarkMode::MessageBox(GetResString(_T("FILE_HISTORY_PURGE_QUESTION")), MB_YESNO | MB_ICONQUESTION) == IDYES) {
					theApp.knownfiles->ClearHistory();
					ReloadList(false, kSharedFilesViewState);
				}
			}
			break;
			case MP_VIEWPARTFILES:
				thePrefs.SetFileHistoryShowPart(!thePrefs.GetFileHistoryShowPart());
				ReloadList(false, kSharedFilesViewState);
				break;
			case MP_VIEWSHAREDFILES:
				thePrefs.SetFileHistoryShowShared(!thePrefs.GetFileHistoryShowShared());
				ReloadList(false, kSharedFilesViewState);
			break;
			case MP_VIEWDUPLICATEFILES:
				thePrefs.SetFileHistoryShowDuplicate(!thePrefs.GetFileHistoryShowDuplicate());
				ReloadList(false, kSharedFilesViewState);
				break;
		default:
			if (wParam >= MP_WEBURL && wParam <= MP_WEBURL + 256) {
				for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
					int index = GetNextSelectedItem(pos);
					if (index >= 0) {
						CKnownFile* pFile = m_ListedItemsVector[index];
						if (pFile != NULL)
							theWebServices.RunURL(pFile, (UINT)wParam);
					}
				}
			}
		}
	}
	return TRUE;
}

void CSharedFilesCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	bool sortAscending;
	if (GetSortItem() != pNMLV->iSubItem)
		switch (pNMLV->iSubItem) {
		case 3:  // Priority
		case 5:  // Requests
		case 6:  // Accepted Requests
		case 7:  // Transferred Data
		case 10: // Complete Sources
		case 11: // Shared ed2k/kad
			// Keep the current 'm_aSortBySecondValue' for that column, but reset to 'descending'
			sortAscending = false;
			break;
		default:
			sortAscending = true;
		}
	else
		sortAscending = !GetSortAscending();

	// Ornis 4-way-sorting
	int adder = 0;
	if (pNMLV->iSubItem >= 5 && pNMLV->iSubItem <= 7) { // 5=SF_REQUESTS, 6=SF_ACCEPTS, 7=SF_TRANSFERRED
		ASSERT(pNMLV->iSubItem - 5 < _countof(m_aSortBySecondValue));
		if (GetSortItem() == pNMLV->iSubItem && !sortAscending) // check for 'descending' because the initial sort order is also 'descending'
			m_aSortBySecondValue[pNMLV->iSubItem - 5] = !m_aSortBySecondValue[pNMLV->iSubItem - 5];
		if (m_aSortBySecondValue[pNMLV->iSubItem - 5])
			adder = 100;
	} else if (pNMLV->iSubItem == 11) { // 11=SHAREDTITLE
		ASSERT(3 < _countof(m_aSortBySecondValue));
		if (GetSortItem() == pNMLV->iSubItem && !sortAscending) // check for 'descending' because the initial sort order is also 'descending'
			m_aSortBySecondValue[3] = !m_aSortBySecondValue[3];
		if (m_aSortBySecondValue[3])
			adder = 100;
	}

	// Sort table
	if (adder == 0)
		SetSortArrow(pNMLV->iSubItem, sortAscending);
	else
		SetSortArrow(pNMLV->iSubItem, sortAscending ? arrowDoubleUp : arrowDoubleDown);

	UpdateSortHistory(MAKELONG(pNMLV->iSubItem + adder, !sortAscending));
	m_pSortParam = MAKELONG(pNMLV->iSubItem + adder, !sortAscending);
	ReloadList(true, kSharedFilesViewState);
	*pResult = 0;
}

int CALLBACK CSharedFilesCtrl::SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort)
{
	const CShareableFile *item1 = reinterpret_cast<CShareableFile*>(lParam1);
	const CShareableFile *item2 = reinterpret_cast<CShareableFile*>(lParam2);

	bool bSortAscending = !HIWORD(lParamSort);

	int iResult = 0;
	bool bExtColumn = false;

	switch (LOWORD(lParamSort)) {
	case 0: //file name
		iResult = CompareLocaleStringNoCase(item1->GetFileName(), item2->GetFileName());
		break;
	case 1: //file size
		iResult = CompareUnsigned(item1->GetFileSize(), item2->GetFileSize());
		break;
	case 2: //file type
		iResult = CompareLocaleStringNoCase(item1->GetFileTypeDisplayStr(), item2->GetFileTypeDisplayStr());
		// if the type is equal, sub-sort by extension
		if (iResult == 0) {
			CString pszExt1(::PathFindExtension(item1->GetFileName()));
			if (pszExt1.IsEmpty()) { // No need to calculate pszExt2 before this case, so we'll break instad of an else if
				iResult = 1;
				break; 
			}
			CString pszExt2(::PathFindExtension(item2->GetFileName()));
			if (pszExt2.IsEmpty())
				iResult = -1;
			else
				iResult = CompareLocaleStringNoCase(pszExt1, pszExt2);
		}
		break;
	case 9: //folder
		iResult = CompareLocaleStringNoCase(item1->GetPath(), item2->GetPath());
		break;
	default:
		bExtColumn = true;
	}

	if (bExtColumn) {
		if (item1->IsKindOf(RUNTIME_CLASS(CKnownFile)) && !item2->IsKindOf(RUNTIME_CLASS(CKnownFile)))
			iResult = -1;
		else if (!item1->IsKindOf(RUNTIME_CLASS(CKnownFile)) && item2->IsKindOf(RUNTIME_CLASS(CKnownFile)))
			iResult = 1;
		else if (item1->IsKindOf(RUNTIME_CLASS(CKnownFile)) && item2->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
			const CKnownFile *kitem1 = static_cast<const CKnownFile*>(item1);
			const CKnownFile *kitem2 = static_cast<const CKnownFile*>(item2);

			switch (LOWORD(lParamSort)) {
			case 3: //prio
				{
					uint8 p1 = kitem1->GetUpPriority() + 1;
					if (p1 == 5)
						p1 = 0;
					uint8 p2 = kitem2->GetUpPriority() + 1;
					if (p2 == 5)
						p2 = 0;
					iResult = p1 - p2;
				}
				break;
			case 4: //fileID
				iResult = memcmp(kitem1->GetFileHash(), kitem2->GetFileHash(), 16);
				break;
			case 5: //requests
				iResult = CompareUnsigned(kitem1->statistic.GetRequests(), kitem2->statistic.GetRequests());
				break;
			case 6: //accepted requests
				iResult = CompareUnsigned(kitem1->statistic.GetAccepts(), kitem2->statistic.GetAccepts());
				break;
			case 7: //all transferred
				iResult = CompareUnsigned(kitem1->statistic.GetTransferred(), kitem2->statistic.GetTransferred());
				break;
			case 8: //shared status
				iResult = CompareUnsigned(kitem1->GetPartCount(), kitem2->GetPartCount());
				break;
			case 10: //complete sources
				iResult = CompareUnsigned(kitem1->m_nCompleteSourcesCount, kitem2->m_nCompleteSourcesCount);
				break;
			case 11: //ed2k shared
				iResult = kitem1->GetPublishedED2K() - kitem2->GetPublishedED2K();
				break;
			case 12:
				iResult = CompareOptLocaleStringNoCaseUndefinedAtBottom(kitem1->GetStrTagValue(FT_MEDIA_ARTIST), kitem2->GetStrTagValue(FT_MEDIA_ARTIST), bSortAscending);
				break;
			case 13:
				iResult = CompareOptLocaleStringNoCaseUndefinedAtBottom(kitem1->GetStrTagValue(FT_MEDIA_ALBUM), kitem2->GetStrTagValue(FT_MEDIA_ALBUM), bSortAscending);
				break;
			case 14:
				iResult = CompareOptLocaleStringNoCaseUndefinedAtBottom(kitem1->GetStrTagValue(FT_MEDIA_TITLE), kitem2->GetStrTagValue(FT_MEDIA_TITLE), bSortAscending);
				break;
			case 15:
				iResult = CompareUnsignedUndefinedAtBottom(kitem1->GetIntTagValue(FT_MEDIA_LENGTH), kitem2->GetIntTagValue(FT_MEDIA_LENGTH), bSortAscending);
				break;
			case 16:
				iResult = CompareUnsignedUndefinedAtBottom(kitem1->GetIntTagValue(FT_MEDIA_BITRATE), kitem2->GetIntTagValue(FT_MEDIA_BITRATE), bSortAscending);
				break;
			case 17:
				iResult = CompareOptLocaleStringNoCaseUndefinedAtBottom(GetCodecDisplayName(kitem1->GetStrTagValue(FT_MEDIA_CODEC)), GetCodecDisplayName(kitem2->GetStrTagValue(FT_MEDIA_CODEC)), bSortAscending);
				break;
			case 18:
				{
					const double ratio1 = kitem1->GetAllTimeRatio();
					const double ratio2 = kitem2->GetAllTimeRatio();
					iResult = (ratio1 < ratio2) ? -1 : static_cast<int>(ratio1 > ratio2);
				}
				break;
			case 19:
				{
					const double ratio1 = kitem1->GetRatio();
					const double ratio2 = kitem2->GetRatio();
					iResult = (ratio1 < ratio2) ? -1 : static_cast<int>(ratio1 > ratio2);
				}
				break;

			case 105: //all requests
				iResult = CompareUnsigned(kitem1->statistic.GetAllTimeRequests(), kitem2->statistic.GetAllTimeRequests());
				break;
			case 106: //all accepted requests
				iResult = CompareUnsigned(kitem1->statistic.GetAllTimeAccepts(), kitem2->statistic.GetAllTimeAccepts());
				break;
			case 107: //all transferred
				iResult = CompareUnsigned(kitem1->statistic.GetAllTimeTransferred(), kitem2->statistic.GetAllTimeTransferred());
				break;
			case 111: //kad shared
				{
					time_t tNow = time(NULL);
					int i1 = static_cast<int>(tNow < kitem1->GetLastPublishTimeKadSrc());
					int i2 = static_cast<int>(tNow < kitem2->GetLastPublishTimeKadSrc());
					iResult = i1 - i2;
				}
			}
		}
	}

	// Call secondary sort order, if the first one resulted as equal
	if (iResult == 0) {
		LPARAM iNextSort = theApp.emuledlg->sharedfileswnd->sharedfilesctrl.GetNextSortOrder(lParamSort);
		if (iNextSort != -1)
			return SortProc(lParam1, lParam2, iNextSort);
	}

	return bSortAscending ? iResult : -iResult;
}

void CSharedFilesCtrl::OpenFile(const CShareableFile *file)
{
	if (file->IsKindOf(RUNTIME_CLASS(CKnownFile)) && static_cast<const CKnownFile*>(file)->m_pCollection) {
		CCollectionViewDialog dialog;
		dialog.SetCollection(static_cast<const CKnownFile*>(file)->m_pCollection);
		dialog.DoModal();
	//} else
	} else if (file->GetFilePath().GetLength()>0)
		ShellDefaultVerb(file->GetFilePath());
}

void CSharedFilesCtrl::OnNmDblClk(LPNMHDR, LRESULT* pResult)
{
	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	if (iSel >= 0) {
		CKnownFile* file = m_ListedItemsVector[iSel];
		if (file != NULL) {
			if (GetKeyState(VK_MENU) & 0x8000) {
				CTypedPtrList<CPtrList, CShareableFile*> aFiles;
				aFiles.AddHead(file);
				ShowFileDialog(aFiles);
			} else if (!file->IsPartFile())
				OpenFile(file);
		}
	}
	*pResult = 0;
}

void CSharedFilesCtrl::CreateMenus()
{
	// Destroy child submenus before their owner to avoid invalid handle asserts.
	if (m_PrioMenu)
		VERIFY2(m_PrioMenu.DestroyMenu());
	if (m_CollectionsMenu)
		VERIFY2(m_CollectionsMenu.DestroyMenu());
	if (m_FileHistorysMenu)
		VERIFY2(m_FileHistorysMenu.DestroyMenu());
	if (m_SharedFilesMenu)
		VERIFY2(m_SharedFilesMenu.DestroyMenu());

	m_FileHistorysMenu.CreateMenu();
	m_FileHistorysMenu.AppendMenu(MF_STRING, MP_CLEARHISTORY, GetResString(_T("FILE_HISTORY_PURGE")), _T("CLEARCOMPLETE"));

	m_PrioMenu.CreateMenu();
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOVERYLOW, GetResString(_T("PRIOVERYLOW")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOLOW, GetResString(_T("PRIOLOW")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIONORMAL, GetResString(_T("PRIONORMAL")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOHIGH, GetResString(_T("PRIOHIGH")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOVERYHIGH, GetResString(_T("PRIORELEASE")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOAUTO, GetResString(_T("PRIOAUTO")));//UAP

	m_CollectionsMenu.CreateMenu();
	m_CollectionsMenu.AppendMenu(MF_STRING, MP_CREATECOLLECTION, GetResString(_T("CREATECOLLECTION")), _T("COLLECTION_ADD"));
	m_CollectionsMenu.AppendMenu(MF_STRING, MP_MODIFYCOLLECTION, GetResString(_T("MODIFYCOLLECTION")), _T("COLLECTION_EDIT"));
	m_CollectionsMenu.AppendMenu(MF_STRING, MP_VIEWCOLLECTION, GetResString(_T("VIEWCOLLECTION")), _T("COLLECTION_VIEW"));
	m_CollectionsMenu.AppendMenu(MF_STRING, MP_SEARCHAUTHOR, GetResString(_T("SEARCHAUTHORCOLLECTION")), _T("COLLECTION_SEARCH"));

	m_SharedFilesMenu.CreatePopupMenu();
	m_SharedFilesMenu.AddMenuSidebar(GetResString(_T("SHAREDFILES")));

	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_OPEN, GetResString(_T("OPENFILE")), _T("OPENFILE"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_OPENFOLDER, GetResString(_T("OPENFOLDER")), _T("OPENFOLDER"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_RENAME, GetResString(_T("RENAME")) + _T("..."), _T("FILERENAME"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_UPDATE_METADATA, GetResString(_T("UPDATE_METADATA")), _T("METADATA"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_REMOVE, GetResString(_T("DELETE")), _T("DELETE"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_REMOVEFROMHISTORY, GetResString(_T("FILE_HISTORY_REMOVE")), _T("DELETESELECTED"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_CANCEL, GetResString(_T("CANCEL_DOWNLOAD")), _T("DELETE"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_CANCEL_FORGET, GetResString(_T("CANCEL_FORGET_DOWNLOAD")), _T("DELETE_FORGET"));

	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_UNSHAREFILE, GetResString(_T("UNSHARE")), _T("KADBOOTSTRAP")); // TODO: better icon
	if (thePrefs.IsExtControlsEnabled())
		m_SharedFilesMenu.AppendMenu(MF_STRING, Irc_SetSendLink, GetResString(_T("IRC_ADDLINKTOIRC")), _T("IRCCLIPBOARD"));

	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_VIEWPARTFILES, GetResString(_T("FILE_HISTORY_SHOW_PART2")));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_VIEWSHAREDFILES, GetResString(_T("FILE_HISTORY_SHOW_SHARED2")));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_VIEWDUPLICATEFILES, GetResString(_T("FILE_HISTORY_SHOW_DUPLICATE2")));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_FileHistorysMenu.m_hMenu, GetResString(_T("FILE_HISTORY")), _T("DOWNLOAD"));

	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	CString sPrio(GetResString(_T("PRIORITY")));
	sPrio.AppendFormat(_T(" (%s)"), (LPCTSTR)GetResString(_T("PW_CON_UPLBL")));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_PrioMenu.m_hMenu, sPrio, _T("FILEPRIORITY"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);

	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_CollectionsMenu.m_hMenu, GetResString(_T("META_COLLECTION")), _T("AABCollectionFileType"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);

	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_DETAIL, GetResString(_T("SHOWDETAILS")), _T("FILEINFO"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_CMT, GetResString(_T("CMT_ADD")), _T("FILECOMMENTS"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_SHOWED2KLINK, GetResString(_T("DL_SHOWED2KLINK")), _T("ED2KLINK"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_CUT, GetResString(_T("COPY_FILE_NAMES")), _T("FILERENAME"));
	if (thePrefs.GetShowCopyEd2kLinkCmd())
		m_SharedFilesMenu.AppendMenu(MF_STRING, MP_GETED2KLINK, GetResString(_T("DL_LINK1")), _T("ED2KLINK"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_FIND, GetResString(_T("FIND")), _T("Search"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);

#if defined(_DEBUG)
	if (thePrefs.IsExtControlsEnabled()) {
		//JOHNTODO: Not for release as we need kad lowID users in the network to see how well this works. Also, we do not support these links yet.
		m_SharedFilesMenu.AppendMenu(MF_STRING, MP_GETKADSOURCELINK, _T("Copy eD2K Links To Clipboard (Kad)"));
		m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	}
#endif
}

void CSharedFilesCtrl::ShowComments(CShareableFile *file)
{
	if (file) {
		CTypedPtrList<CPtrList, CShareableFile*> aFiles;
		aFiles.AddHead(file);
		ShowFileDialog(aFiles, IDD_COMMENT);
	}
}

void CSharedFilesCtrl::OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult)
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
		// This isn't an owner drawn list anymore, instead this is implemented as a virtual list. So above description is now obsolete!
		LVITEMW& rItem = reinterpret_cast<NMLVDISPINFO*>(pNMHDR)->item;
		if (rItem.mask & LVIF_TEXT) {
			CKnownFile* cur_file = NULL;
			if (rItem.iItem < m_ListedItemsVector.size()) {
				cur_file = m_ListedItemsVector[rItem.iItem];
				if (cur_file && cur_file->GetFileName()) 
					_tcsncpy_s(rItem.pszText, rItem.cchTextMax, GetItemDisplayText(cur_file, rItem.iSubItem), _TRUNCATE);
			}
		}
	}
	*pResult = 0;
}


void CSharedFilesCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar == VK_SPACE && CheckBoxesEnabled()) {
		// Toggle Checkboxes
		// selection and item position might change during processing (shouldn't though, but lets make sure), so first get all pointers instead using the selection pos directly
		SetRedraw(false);
		CTypedPtrList<CPtrList, CShareableFile*> selectedList;
		for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
			int index = GetNextSelectedItem(pos);
			if (index >= 0) {
				CKnownFile* cur_file = m_ListedItemsVector[index];
				if (cur_file != NULL)
					selectedList.AddTail(reinterpret_cast<CShareableFile*>(cur_file));
			}
		}
		while (!selectedList.IsEmpty()) {
			int index = -1;
			if (m_ListedItemsMap.Lookup(reinterpret_cast<CKnownFile*>(selectedList.RemoveHead()), index) && index >= 0)
				CheckBoxClicked(index);
		}
		SetRedraw(true);
		return;
	}

	CMuleListCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CSharedFilesCtrl::ShowFileDialog(CTypedPtrList<CPtrList, CShareableFile*> &aFiles, UINT uInvokePage)
{
	if (!aFiles.IsEmpty()) {
		CSharedFileDetailsSheet dialog(aFiles, uInvokePage, this);
		dialog.DoModal();
	}
}

void CSharedFilesCtrl::SetDirectoryFilter(CDirectoryItem *pNewFilter, bool bRefresh)
{
	if (m_pDirectoryFilter != pNewFilter) {
		m_pDirectoryFilter = pNewFilter;
		if (bRefresh)
			ReloadList(false, kSharedFilesViewState);
	}
}

bool CSharedFilesCtrl::GetPersistentInfoTipText(const SPersistentInfoTipContext& context, CString& strText)
{
	CKnownFile* pFile = m_ListedItemsVector[context.iItem];
	if (pFile == NULL)
		return false;

	strText = pFile->GetInfoSummary() + TOOLTIP_AUTOFORMAT_SUFFIX_CH;
	return true;
}

void CSharedFilesCtrl::OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult)
{
	CMuleListCtrl::OnLvnGetInfoTip(pNMHDR, pResult);
}

const bool CSharedFilesCtrl::IsFilteredOut(const CShareableFile *pFile) const
{
	if (!pFile)
		return true;

	// check filter conditions if we should show this file right now
	if (m_pDirectoryFilter != NULL) {
		ASSERT(pFile->IsKindOf(RUNTIME_CLASS(CKnownFile)) || m_pDirectoryFilter->m_eItemType == SDI_UNSHAREDDIRECTORY);
		switch (m_pDirectoryFilter->m_eItemType) {
		case SDI_ALL: // No filter
		case SDI_DUP: // No filter
		case SDI_ALLHISTORY: // No filter
			break;
		case SDI_FILESYSTEMPARENT:
			return true;
		case SDI_UNSHAREDDIRECTORY: // Items from the whole file system tree
			if (pFile->IsPartFile())
				return true;
		case SDI_NO:
			// some shared directory
		case SDI_CATINCOMING: // Categories with special incoming dirs
			if (!EqualPaths(pFile->GetSharedDirectory(), m_pDirectoryFilter->m_strFullPath))
				return true;
			break;
		case SDI_TEMP: // only temp files
			if (!pFile->IsPartFile())
				return true;
			if (m_pDirectoryFilter->m_nCatFilter != -1 && (UINT)m_pDirectoryFilter->m_nCatFilter != ((CPartFile*)pFile)->GetCategory())
				return true;
			break;
		case SDI_DIRECTORY: // any user selected shared dir but not incoming or temp
			if (pFile->IsPartFile())
				return true;
			if (EqualPaths(pFile->GetSharedDirectory(), thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR)))
				return true;
			break;
		case SDI_INCOMING: // Main incoming directory
		{
			CString sIncoming(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
			if (!EqualPaths(pFile->GetPath(), sIncoming)) {
				if (thePrefs.GetAutoShareSubdirs() && IsSubDirectoryOf(pFile->GetPath(), sIncoming))
					break;
				return true;
			}
			break;
		}
	case SDI_ED2KFILETYPE:
	{
			// Special handling for GUI-only "Other" filter
			if (m_pDirectoryFilter->m_nCatFilter == ED2KFT_OTHER) {
				EED2KFileType t = GetED2KFileTypeID(pFile->GetFileName());
				// Accept only files which are not one of the known classes
				if (t == ED2KFT_AUDIO || t == ED2KFT_VIDEO || t == ED2KFT_IMAGE || t == ED2KFT_PROGRAM || t == ED2KFT_DOCUMENT || t == ED2KFT_ARCHIVE || t == ED2KFT_CDIMAGE || t == ED2KFT_EMULECOLLECTION)
					return true; // Filter out known types, keep the rest

				break;
			}

			if (m_pDirectoryFilter->m_nCatFilter == -1 || m_pDirectoryFilter->m_nCatFilter != GetED2KFileTypeID(pFile->GetFileName()))
				return true;

			break;
		}
		}
	}

	const CStringArray &rastrFilter = theApp.emuledlg->sharedfileswnd->m_astrFilter;
	if (!rastrFilter.IsEmpty()) {
		// filtering is done by text only for all columns to keep it consistent and simple for the user
		// even if that doesn't allow complex filters
		const CString &szFilterTarget(GetItemDisplayText(pFile, theApp.emuledlg->sharedfileswnd->GetFilterColumn()));

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

void CSharedFilesCtrl::SetToolTipsDelay(DWORD dwDelay)
{
	CToolTipCtrl *tooltip = GetToolTips();
	if (tooltip)
		tooltip->SetDelayTime(TTDT_INITIAL, dwDelay);
}

const bool CSharedFilesCtrl::IsSharedInKad(const CKnownFile *file) const
{
	if (!Kademlia::CKademlia::IsConnected() || time(NULL) >= file->GetLastPublishTimeKadSrc())
		return false;
	if (!Kademlia::CKademlia::IsFirewalled())
		return true;
	return (theApp.clientlist->GetServingBuddy() && (file->GetLastPublishServingBuddy() == theApp.clientlist->GetServingBuddy()->GetIP().ToUInt32(false)))
		|| (Kademlia::CKademlia::IsRunning() && !Kademlia::CUDPFirewallTester::IsFirewalledUDP(true) && Kademlia::CUDPFirewallTester::IsVerified());
}

BOOL CSharedFilesCtrl::OnNMClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	NMLISTVIEW *pNMListView = reinterpret_cast<NM_LISTVIEW*>(pNMHDR);
	int iItem = HitTest(pNMListView->ptAction);
	if (iItem >= 0) {
		if (CheckBoxesEnabled()) { // do we have checkboxes?
			// determine if the checkbox was clicked
			CRect rcItem;
			if (GetItemRect(iItem, rcItem, LVIR_BOUNDS)) {
				CPoint pointHit = pNMListView->ptAction;
				ASSERT(rcItem.PtInRect(pointHit));
				rcItem.left += sm_iIconOffset;
				rcItem.right = rcItem.left + 16;
				rcItem.top += (rcItem.Height() > 16) ? ((rcItem.Height() - 15) / 2) : 0;
				rcItem.bottom = rcItem.top + 16;
				if (rcItem.PtInRect(pointHit)) {
					// user clicked on the checkbox
					CheckBoxClicked(iItem);
					return (BOOL)(*pResult = 0); // Since this is a checkbox click, do not proceed selection checks, return now and pass on to the parent window
				}
			}
		}
	}

	return (BOOL)(*pResult = 0); // pass on to the parent window
}

void CSharedFilesCtrl::CheckBoxClicked(const int iItem)
{
	if (iItem == -1) {
		ASSERT(0);
		return;
	}
	// check which state the checkbox (should) currently have
	CKnownFile* cur_file = m_ListedItemsVector[iItem];
	if (cur_file == NULL)
		return;
	const CShareableFile* pFile = reinterpret_cast<CShareableFile*>(cur_file);

	if (pFile->IsShellLinked())
		return; // no interacting with shell-linked files
	if (theApp.sharedfiles->ShouldBeShared(pFile->GetPath(), pFile->GetFilePath(), false)) {
		// this is currently shared so unshare it
		if (theApp.sharedfiles->ShouldBeShared(pFile->GetPath(), pFile->GetFilePath(), true))
			return; // not allowed to unshare this file
		VERIFY(theApp.sharedfiles->ExcludeFile(pFile->GetFilePath()));
		UpdateFile(cur_file);
		// update GUI stuff
		ShowFilesCount();
		theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails();
		theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged();
		// no need to update the list itself, will be handled in the RemoveFile function
	} else {
		if (!thePrefs.IsShareableDirectory(pFile->GetPath()))
			return; // not allowed to share
		VERIFY(theApp.sharedfiles->AddSingleSharedFile(pFile->GetFilePath()));
		ShowFilesCount();
		theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails();
		theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged();
		UpdateFile(cur_file);
	}
}

bool CSharedFilesCtrl::CheckBoxesEnabled() const
{
	return (m_eFilter == FilterType::FileSystem);
}

void CSharedFilesCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	// highlighting Checkboxes
	if (CheckBoxesEnabled()) {
		// are we currently on any checkbox?
		int iItem = HitTest(point);
		if (iItem >= 0) {
			CRect rcItem;
			if (GetItemRect(iItem, rcItem, LVIR_BOUNDS)) {
				rcItem.left += sm_iIconOffset;
				rcItem.right = rcItem.left + 16;
				rcItem.top += (rcItem.Height() > 16) ? ((rcItem.Height() - 15) / 2) : 0;
				rcItem.bottom = rcItem.top + 16;
				if (rcItem.PtInRect(point)) {
					// is this checkbox already hot?
					if (m_pHighlightedItem != reinterpret_cast<CShareableFile*>(GetItemData(iItem))) {
						// update old highlighted item
						CShareableFile* pOldItem = m_pHighlightedItem;
						m_pHighlightedItem = reinterpret_cast<CShareableFile*>(GetItemData(iItem));
						UpdateFile(reinterpret_cast<CKnownFile*>(pOldItem), false);
						// highlight current item
						InvalidateRect(rcItem);
					}
					CMuleListCtrl::OnMouseMove(nFlags, point);
					return;
				}
			}
		}
		// no checkbox should be hot
		if (m_pHighlightedItem != NULL) {
			CShareableFile* pOldItem = m_pHighlightedItem;
			m_pHighlightedItem = NULL;
			UpdateFile(reinterpret_cast<CKnownFile*>(pOldItem), false);
		}
	}
	CMuleListCtrl::OnMouseMove(nFlags, point);
}


CSharedFilesCtrl::CShareDropTarget::CShareDropTarget()
{
	m_piDropHelper = NULL;
	m_pParent = NULL;
	m_bUseDnDHelper = SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, NULL, CLSCTX_INPROC_SERVER, IID_IDropTargetHelper, (void**)&m_piDropHelper));
}

CSharedFilesCtrl::CShareDropTarget::~CShareDropTarget()
{
	if (m_piDropHelper != NULL)
		m_piDropHelper->Release();
}

DROPEFFECT CSharedFilesCtrl::CShareDropTarget::OnDragEnter(CWnd *pWnd, COleDataObject *pDataObject, DWORD /*dwKeyState*/, CPoint point)
{
	DROPEFFECT dwEffect = pDataObject->IsDataAvailable(CF_HDROP) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
	if (m_bUseDnDHelper) {
		IDataObject *piDataObj = pDataObject->GetIDataObject(FALSE);
		m_piDropHelper->DragEnter(pWnd->GetSafeHwnd(), piDataObj, &point, dwEffect);
	}
	return dwEffect;
}

DROPEFFECT CSharedFilesCtrl::CShareDropTarget::OnDragOver(CWnd*, COleDataObject *pDataObject, DWORD, CPoint point)
{
	DROPEFFECT dwEffect = pDataObject->IsDataAvailable(CF_HDROP) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
	if (m_bUseDnDHelper)
		m_piDropHelper->DragOver(&point, dwEffect);
	return dwEffect;
}

BOOL CSharedFilesCtrl::CShareDropTarget::OnDrop(CWnd*, COleDataObject *pDataObject, DROPEFFECT dropEffect, CPoint point)
{
	HGLOBAL hGlobal = pDataObject->GetGlobalData(CF_HDROP);
	if (hGlobal != NULL) {
		HDROP hDrop = (HDROP)::GlobalLock(hGlobal);
		if (hDrop != NULL) {
			CString strFilePath;
			CFileFind ff;
			CStringList liToAddFiles; // all files to add
			CStringList liToAddDirs; // all directories to add
			bool bFromSingleDirectory = true;	// all files are in the same directory,
			CString strSingleDirectory;			// which would be this one

			UINT nFileCount = DragQueryFile(hDrop, UINT_MAX, NULL, 0);
			for (UINT nFile = 0; nFile < nFileCount; ++nFile) {
				if (DragQueryFile(hDrop, nFile, strFilePath.GetBuffer(MAX_PATH), MAX_PATH) > 0) {
					strFilePath.ReleaseBuffer();
					if (ff.FindFile(strFilePath)) {
						ff.FindNextFile();
						CString ffpath(ff.GetFilePath());
						if (ff.IsDirectory())
							slosh(ffpath);
						// just a quick pre-check, complete check is done later in the share function itself
						if (ff.IsDots() || ff.IsSystem() || ff.IsTemporary()
							|| (!ff.IsDirectory() && (ff.GetLength() == 0 || ff.GetLength() > MAX_EMULE_FILE_SIZE
								|| theApp.sharedfiles->ShouldBeShared(ffpath.Left(ffpath.ReverseFind(_T('\\'))), ffpath, false)))
							|| (ff.IsDirectory() && (!thePrefs.IsShareableDirectory(ffpath)
								|| theApp.sharedfiles->ShouldBeShared(ffpath, NULL, false))))
						{
							DebugLog(_T("Drag&Drop'ed shared File ignored (%s)"), (LPCTSTR)EscPercent(ffpath));
						} else if (ff.IsDirectory()) {
							DEBUG_ONLY(DebugLog(_T("Drag&Drop'ed directory: %s"), (LPCTSTR)EscPercent(ffpath)));
							liToAddDirs.AddTail(ffpath);
						} else {
							DEBUG_ONLY(DebugLog(_T("Drag&Drop'ed file: %s"), (LPCTSTR)EscPercent(ffpath)));
							liToAddFiles.AddTail(ffpath);
							if (bFromSingleDirectory) {
								if (strSingleDirectory.IsEmpty())
									strSingleDirectory = ffpath.Left(ffpath.ReverseFind(_T('\\')) + 1);
								else if (strSingleDirectory.CompareNoCase(ffpath.Left(ffpath.ReverseFind(_T('\\')) + 1)) != NULL)
									bFromSingleDirectory = false;
							}
						}
					} else
						DebugLogError(_T("Drag&Drop'ed shared File not found (%s)"), (LPCTSTR)EscPercent(strFilePath));

					ff.Close();
				} else {
					ASSERT(0);
					strFilePath.ReleaseBuffer();
				}
			}

			if (!liToAddFiles.IsEmpty() || !liToAddDirs.IsEmpty()) {
				// add the directories first as this would invalidate addition of
				// single files, contained in one of those dirs
				for (POSITION pos = liToAddDirs.GetHeadPosition(); pos != NULL;)
					VERIFY(theApp.sharedfiles->AddSingleSharedDirectory(liToAddDirs.GetNext(pos))); // should always succeed

				bool bHaveFiles = false;
				while (!liToAddFiles.IsEmpty())
					bHaveFiles |= theApp.sharedfiles->AddSingleSharedFile(liToAddFiles.RemoveHead()); // could fail, due to the dirs added above

				// GUI updates
				if (!liToAddDirs.IsEmpty())
					theApp.emuledlg->sharedfileswnd->m_ctlSharedDirTree.Reload(true);
				if (bHaveFiles)
					theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged();
				m_pParent->ShowFilesCount();
	
				if (bHaveFiles && liToAddDirs.IsEmpty() && bFromSingleDirectory) {
					// if we added only files from the same directory, show and select this in the file system tree
					ASSERT(!strSingleDirectory.IsEmpty());
					VERIFY(theApp.emuledlg->sharedfileswnd->m_ctlSharedDirTree.ShowFileSystemDirectory(strSingleDirectory));
				} else if (!liToAddDirs.IsEmpty() && !bHaveFiles) {
					// only directories added, if only one select the specific shared dir, otherwise the Shared Directories section
					const CString &sShow(liToAddDirs.GetCount() == 1 ? liToAddDirs.GetHead() : EMPTY);
					theApp.emuledlg->sharedfileswnd->m_ctlSharedDirTree.ShowSharedDirectory(sShow);
				} else {
					// otherwise select the All Shared Files category
					theApp.emuledlg->sharedfileswnd->m_ctlSharedDirTree.ShowAllSharedFiles();
				}
			}
			::GlobalUnlock(hGlobal);
		}
		::GlobalFree(hGlobal);
	}

	if (m_bUseDnDHelper) {
		IDataObject *piDataObj = pDataObject->GetIDataObject(FALSE);
		m_piDropHelper->Drop(piDataObj, &point, dropEffect);
	}

	return TRUE;
}

void CSharedFilesCtrl::CShareDropTarget::OnDragLeave(CWnd*)
{
	if (m_bUseDnDHelper)
		m_piDropHelper->DragLeave();
}
