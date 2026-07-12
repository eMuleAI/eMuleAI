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
#include "SearchListCtrl.h"
#include "emule.h"
#include "ResizableLib/ResizableSheet.h"
#include "SearchFile.h"
#include "SearchList.h"
#include "emuledlg.h"
#include "MetaDataDlg.h"
#include "CommentDialogLst.h"
#include "SearchDlg.h"
#include "SearchParams.h"
#include "ClosableTabCtrl.h"
#include "PreviewDlg.h"
#include "Preview.h"
#include "UpDownClient.h"
#include "ClientList.h"
#include "MemDC.h"
#include "SharedFileList.h"
#include "DownloadQueue.h"
#include "PartFile.h"
#include "KnownFileList.h"
#include "OtherFunctions.h"
#include "MenuCmds.h"
#include "Opcodes.h"
#include "Packets.h"
#include "WebServices.h"
#include "Log.h"
#include "HighColorTab.hpp"
#include "ListViewWalkerPropertySheet.h"
#include "UserMsgs.h"
#include "SearchDlg.h"
#include "SearchResultsWnd.h"
#include "ServerConnect.h"
#include "server.h"
#include "MediaInfo.h"
#include "MuleStatusBarCtrl.h"
#include "TransferDlg.h"
#include "eMuleAI/DarkMode.h"
#include "MuleListCtrl.h"
#include "ListViewSearchDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define COLLAPSE_ONLY	0
#define EXPAND_ONLY		1
#define EXPAND_COLLAPSE	2

#define	TREE_WIDTH		10

LPARAM CSearchListCtrl::m_pSortParam = NULL;

namespace
{
	const EListStateField kSearchListViewState = static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL);
	const DWORD kSearchListSetItemCountFlags = LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL;
	int GetSearchCompleteState(bool bKademlia, bool bHasDirectory, UINT uSources, UINT uCompleteSources)
	{
		if (bKademlia)
			return -1;
		if (bHasDirectory && uSources == 1 && uCompleteSources == 0)
			return -1;
		if (uSources > 0 && uCompleteSources > 0)
			return 1;
		return 0;
	}

	CString BuildSearchRemoveHashKey(uint32 nSearchID, const uchar *pFileHash)
	{
		CString strKey;
		if (nSearchID != 0 && pFileHash != NULL)
			strKey.Format(_T("%u:%s"), nSearchID, (LPCTSTR)md4str(pFileHash));
		return strKey;
	}

	void UpdateSearchListItemCount(CListCtrl& listCtrl, const size_t itemCount)
	{
		listCtrl.SetItemCountEx(static_cast<int>(itemCount), kSearchListSetItemCountFlags);
	}

	void FillSearchFallbackOwnerDataRow(CListCtrl& listCtrl, LPDRAWITEMSTRUCT lpDrawItemStruct)
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

	bool ShouldApplySearchSpamOrBlacklistBottomGrouping()
	{
		return thePrefs.IsSearchSpamFilterEnabled() || thePrefs.GetBlacklistManual() || thePrefs.GetBlacklistAutomatic();
	}

	bool IsSearchFileSpamOrBlacklistedBottomGroup(const CSearchFile* item)
	{
		return item != NULL && ShouldApplySearchSpamOrBlacklistBottomGrouping() && item->IsConsideredSpam(true);
	}

	int GetSearchFileBottomGroupRank(const CSearchFile* item)
	{
		if (!thePrefs.GetGroupKnownAtTheBottom() || item == NULL)
			return 0;
		if (IsSearchFileSpamOrBlacklistedBottomGroup(item))
			return 2;
		return item->GetKnownType() != CSearchFile::NotDetermined ? 1 : 0;
	}

	int GetSearchKnownTieRank(uint8 uKnownType, bool bManualBlacklisted, bool bAutomaticBlacklisted, bool bConsideredSpam)
	{
		if (!thePrefs.GetGroupKnownAtTheBottom())
			return 0;
		if (bManualBlacklisted)
			return 5;
		if (bAutomaticBlacklisted || bConsideredSpam)
			return 6;

		switch (uKnownType) {
		case CSearchFile::Downloading:
			return 1;
		case CSearchFile::Shared:
			return 2;
		case CSearchFile::Downloaded:
			return 3;
		case CSearchFile::Cancelled:
			return 4;
		default:
			return 0;
		}
	}

	int GetSearchFileKnownTieRank(const CSearchFile* item)
	{
		if (item == NULL)
			return 0;
		return GetSearchKnownTieRank(static_cast<uint8>(item->GetKnownType()), item->GetManualBlacklisted(), item->GetAutomaticBlacklisted(), item->IsConsideredSpam(false));
	}

	int CompareSearchFixedGroupRank(int iRank1, int iRank2, bool bSortAscending)
	{
		const int iResult = CompareUnsigned(static_cast<uint32>(iRank1), static_cast<uint32>(iRank2));
		return bSortAscending ? iResult : -iResult;
	}

	CString GetSearchKnownTypeDisplayString(uint8 uKnownType, bool bManualBlacklisted, bool bAutomaticBlacklisted, bool bConsideredSpam)
	{
		LPCTSTR uid = EMPTY;
		switch (uKnownType) {
		case CSearchFile::Shared:
			uid = _T("SHARED");
			break;
		case CSearchFile::Downloading:
			uid = _T("DOWNLOADING");
			break;
		case CSearchFile::Downloaded:
			uid = _T("DOWNLOADED");
			break;
		case CSearchFile::Cancelled:
			uid = _T("CANCELLED");
			break;
		default:
			if (thePrefs.GetBlacklistManual() && bManualBlacklisted)
				uid = _T("MANUAL_BLACKLISTED");
			else if (thePrefs.GetBlacklistAutomatic() && bAutomaticBlacklisted)
				uid = _T("AUTOMATIC_BLACKLISTED");
			else if (thePrefs.IsSearchSpamFilterEnabled() && bConsideredSpam)
				uid = _T("SPAM");
		}
		return uid != NULL && uid[0] != _T('\0') ? GetResString(uid) : CString();
	}

	bool ShouldApplySearchBottomGrouping()
	{
		return thePrefs.GetGroupKnownAtTheBottom();
	}

	const CSearchFile* GetSearchFileSortGroupFile(const CSearchFile *item)
	{
		return item != NULL && item->GetListParent() != NULL ? item->GetListParent() : item;
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
		menu.AppendODMenu(MF_STRING | (bEnablePreview ? MF_ENABLED : MF_GRAYED), MP_PREVIEW, new CMenuXPText(MP_PREVIEW, strPrimaryLabel.IsEmpty() ? GetResString(_T("DL_PREVIEW")) : strPrimaryLabel, thePreviewApps.GetPreviewCommandIcon(strPrimaryCommand)));
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

//////////////////////////////////////////////////////////////////////////////
// CSearchResultFileDetailSheet

class CSearchResultFileDetailSheet : public CListViewWalkerPropertySheet
{
	DECLARE_DYNAMIC(CSearchResultFileDetailSheet)

	void Localize();
public:
	CSearchResultFileDetailSheet(CTypedPtrList<CPtrList, CSearchFile*> &paFiles, UINT uInvokePage = 0, CListCtrlItemWalk *pListCtrl = NULL);

protected:
	CMetaDataDlg m_wndMetaData;
	CCommentDialogLst m_wndComments;
	CClosableTabCtrl			m_tabDark;

	UINT m_uInvokePage;
	static LPCTSTR m_pPshStartPage;

	void UpdateTitle();

	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnDestroy();
	afx_msg LRESULT OnDataChanged(WPARAM, LPARAM);
};

LPCTSTR CSearchResultFileDetailSheet::m_pPshStartPage;

IMPLEMENT_DYNAMIC(CSearchResultFileDetailSheet, CListViewWalkerPropertySheet)

BEGIN_MESSAGE_MAP(CSearchResultFileDetailSheet, CListViewWalkerPropertySheet)
	ON_WM_DESTROY()
	ON_MESSAGE(UM_DATA_CHANGED, OnDataChanged)
END_MESSAGE_MAP()

void CSearchResultFileDetailSheet::Localize()
{
	m_wndMetaData.Localize();
	SetTabTitle(_T("META_DATA"), &m_wndMetaData, this);
	m_wndComments.Localize();
	SetTabTitle(_T("CMT_READALL"), &m_wndComments, this);
}

CSearchResultFileDetailSheet::CSearchResultFileDetailSheet(CTypedPtrList<CPtrList, CSearchFile*> &paFiles, UINT uInvokePage, CListCtrlItemWalk *pListCtrl)
	: CListViewWalkerPropertySheet(pListCtrl)
	, m_uInvokePage(uInvokePage)
{
	for (POSITION pos = paFiles.GetHeadPosition(); pos != NULL;)
		m_aItems.Add(paFiles.GetNext(pos));
	m_psh.dwFlags &= ~PSH_HASHELP;
	m_psh.dwFlags |= PSH_NOAPPLYNOW;

	m_wndMetaData.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndMetaData.m_psp.dwFlags |= PSP_USEICONID;
	m_wndMetaData.m_psp.pszIcon = _T("METADATA");
	if (thePrefs.IsExtControlsEnabled() && m_aItems.GetSize() == 1) {
		m_wndMetaData.SetFiles(&m_aItems);
		AddPage(&m_wndMetaData);
	}

	m_wndComments.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndComments.m_psp.dwFlags |= PSP_USEICONID;
	m_wndComments.m_psp.pszIcon = _T("FileComments");
	m_wndComments.SetFiles(&m_aItems);
	AddPage(&m_wndComments);

	LPCTSTR pPshStartPage = m_pPshStartPage;
	if (m_uInvokePage != 0)
		pPshStartPage = MAKEINTRESOURCE(m_uInvokePage);
	for (int i = (int)m_pages.GetSize(); --i >= 0;)
		if (GetPage(i)->m_psp.pszTemplate == pPshStartPage) {
			m_psh.nStartPage = i;
			break;
		}
}

void CSearchResultFileDetailSheet::OnDestroy()
{
	if (m_uInvokePage == 0)
		m_pPshStartPage = GetPage(GetActiveIndex())->m_psp.pszTemplate;
	CListViewWalkerPropertySheet::OnDestroy();
}

BOOL CSearchResultFileDetailSheet::OnInitDialog()
{
	EnableStackedTabs(FALSE);
	BOOL bResult = CListViewWalkerPropertySheet::OnInitDialog();
	HighColorTab::UpdateImageList(*this);
	InitWindowStyles(this);
	EnableSaveRestore(_T("SearchResultFileDetailsSheet")); // call this after(!) OnInitDialog
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

LRESULT CSearchResultFileDetailSheet::OnDataChanged(WPARAM, LPARAM)
{
	UpdateTitle();
	return 1;
}

void CSearchResultFileDetailSheet::UpdateTitle()
{
	CString sTitle(GetResString(_T("DETAILS")));
	if (m_aItems.GetSize() == 1)
		sTitle.AppendFormat(_T(": %s"), (LPCTSTR)(static_cast<CSearchFile*>(m_aItems[0])->GetFileName()));
	SetWindowText(sTitle);
}


//////////////////////////////////////////////////////////////////////////////
// CSearchListCtrl

IMPLEMENT_DYNAMIC(CSearchListCtrl, CMuleListCtrl)

namespace
{
	const UINT_PTR kTimerChunkedSearchRemove = 0x5E71;
	const UINT_PTR kTimerDeferredSearchReload = 0x5E72;
	const UINT kDeferredSearchReloadDelayMs = 250;
	const size_t kClientSharedFilesAutoSortThreshold = 1000;
}


BEGIN_MESSAGE_MAP(CSearchListCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_DELETEALLITEMS, OnLvnDeleteAllItems)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetDispInfo)
	ON_NOTIFY_REFLECT(LVN_GETINFOTIP, OnLvnGetInfoTip)
	ON_NOTIFY_REFLECT(LVN_KEYDOWN, OnLvnKeyDown)
	ON_NOTIFY_REFLECT(NM_CLICK, OnNmClick)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
	ON_WM_CONTEXTMENU()
	ON_WM_DESTROY()
	ON_WM_KEYDOWN()
	ON_WM_TIMER()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_SHOWWINDOW()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CSearchListCtrl::CSearchListCtrl()
	: CListCtrlItemWalk(this)
	, searchlist()
	, m_crSearchResultDownloading()
	, m_crSearchResultDownloadStopped()
	, m_crSearchResultKnown()
	, m_crSearchResultSharing()
	, m_crSearchResultCancelled()
	, m_crShades()
	, m_nResultsID()
	, m_iDataSize(-1)
	, m_iNextChunkedSearchRemoveItem(0)
	, m_uChunkedSearchRemoveProcessed(0)
	, m_uChunkedSearchRemoveStale(0)
	, m_uChunkedSearchRemoveFailed(0)
	, m_dwChunkedSearchRemoveStartedTick(0)
	, m_dwChunkedSearchRemoveLastProgressTick(0)
	, m_bChunkedSearchRemoveActive(false)
	, m_bDeferredSearchReloadPending(false)
	, m_bDeferredSearchReloadSort(false)
	, m_eDeferredSearchReloadState(kSearchListViewState)
	, m_lListedItemsModelSequence(0)
{
	SetGeneralPurposeFind(true);
	m_eFileSizeFormat = (EFileSizeFormat)theApp.GetProfileInt(_T("eMule"), _T("SearchResultsFileSizeFormat"), fsizeDefault);
	SetSkinKey(_T("SearchResultsLv"));
}

void CSearchListCtrl::OnDestroy()
{
	ClearChunkedSearchRemoveItems(false);
	KillTimer(kTimerDeferredSearchReload);
	m_bDeferredSearchReloadPending = false;
	theApp.WriteProfileInt(_T("eMule"), _T("SearchResultsFileSizeFormat"), m_eFileSizeFormat);
	__super::OnDestroy();
}


void CSearchListCtrl::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kTimerDeferredSearchReload) {
		KillTimer(kTimerDeferredSearchReload);
		if (m_bDeferredSearchReloadPending) {
			if (theApp.emuledlg != NULL && theApp.emuledlg->IsStartupLoadingDialogVisible()) {
				SetTimer(kTimerDeferredSearchReload, 500, NULL);
				return;
			}
			if (theApp.emuledlg != NULL && theApp.emuledlg->activewnd == theApp.emuledlg->searchwnd && !IsWindowVisible()) {
				SetTimer(kTimerDeferredSearchReload, 200, NULL);
				return;
			}
			if (theApp.emuledlg != NULL && theApp.emuledlg->activewnd != theApp.emuledlg->searchwnd) {
				m_bDeferredSearchReloadPending = false;
				m_bDeferredSearchReloadSort = false;
				return;
			}
			const bool bSortCurrentList = m_bDeferredSearchReloadSort;
			const EListStateField eState = m_eDeferredSearchReloadState;
			m_bDeferredSearchReloadPending = false;
			m_bDeferredSearchReloadSort = false;
			ReloadList(bSortCurrentList, eState);
		}
		return;
	}


	if (nIDEvent == kTimerChunkedSearchRemove) {
		ProcessChunkedSearchRemoveItems();
		if (m_bChunkedSearchRemoveActive && ::IsWindow(m_hWnd) && SetTimer(kTimerChunkedSearchRemove, 1, NULL) == 0) {
			AddDebugLogLine(DLP_HIGH, false, _T("Chunked search result remove aborted because the continuation timer could not be restarted. processed=%u remaining=%d\n"), m_uChunkedSearchRemoveProcessed, static_cast<int>(m_vecChunkedSearchRemoveItems.size() - static_cast<size_t>(m_iNextChunkedSearchRemoveItem)));
			FinishChunkedSearchRemoveItems(true);
		}
		return;
	}

	__super::OnTimer(nIDEvent);
}

void CSearchListCtrl::SetStyle()
{
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
}

void CSearchListCtrl::SetAllIcons()
{
	ApplyImageList(NULL);
	m_ImageList.DeleteImageList();
	m_ImageList.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	m_ImageList.Add(CTempIconLoader(_T("EMPTY"))); //0
	m_ImageList.Add(CTempIconLoader(_T("Rating_NotRated"))); //1
	m_ImageList.Add(CTempIconLoader(_T("Rating_Fake"))); //2
	m_ImageList.Add(CTempIconLoader(_T("Rating_Poor"))); //3
	m_ImageList.Add(CTempIconLoader(_T("Rating_Fair"))); //4
	m_ImageList.Add(CTempIconLoader(_T("Rating_Good"))); //5
	m_ImageList.Add(CTempIconLoader(_T("Rating_Excellent"))); //6
	m_ImageList.Add(CTempIconLoader(_T("Collection_Search"))); //7 rating for comments are searched on kad
	m_ImageList.Add(CTempIconLoader(_T("SPAM"))); //8 spam indicator
	m_ImageList.SetOverlayImage(m_ImageList.Add(CTempIconLoader(_T("FileCommentsOvl"))), 1);
	m_ImageList.Add(CTempIconLoader(_T("SPAM_PINK"))); //10 pink indicator
	m_ImageList.Add(CTempIconLoader(_T("SPAM_PURPLE"))); //11 purple indicator
	m_ImageList.Add(CTempIconLoader(_T("SPAM_YELLOW"))); //12 yellow indicator
	m_ImageList.Add(CTempIconLoader(_T("SPAM_GREEN"))); //13 green indicator
	m_ImageList.Add(CTempIconLoader(_T("SPAM_DARK_GREEN"))); //14 blue indicator
	m_ImageList.Add(CTempIconLoader(_T("SPAM_ORANGE"))); //15 blue indicator
	m_ImageList.Add(CTempIconLoader(_T("SPAM_BLUE"))); //16 blue indicator
	// Apply the image list also to the listview control, even if we use our own 'DrawItem'.
	// This is needed to give the listview control a chance to initialize the row height.
	VERIFY(ApplyImageList(m_ImageList) == NULL);

	// NOTE: There is another image list applied to this particular listview control!
	// See also the 'Init' function.
}

void CSearchListCtrl::Init(CSearchList *in_searchlist)
{
	SetPrefsKey(_T("SearchListCtrl"));
	ASSERT((GetStyle() & LVS_SINGLESEL) == 0);
	SetStyle();

	CToolTipCtrl *tooltip = GetToolTips();
	if (tooltip) {
		m_tooltip.SetFileIconToolTip(true);
		m_tooltip.SubclassWindow(*tooltip);
		tooltip->ModifyStyle(0, TTS_NOPREFIX);
		tooltip->SetDelayTime(TTDT_AUTOPOP, SEC2MS(20));
	}
	searchlist = in_searchlist;

	// Alignment rule: left for text, dates, and status labels; right for sizes, rates, counts, durations, and percentages.
	InsertColumn(0,		EMPTY,	LVCFMT_LEFT,	DFLT_FILENAME_COL_WIDTH);			//DL_FILENAME
	InsertColumn(1,		EMPTY,	LVCFMT_RIGHT,	DFLT_SIZE_COL_WIDTH);				//DL_SIZE
	InsertColumn(2,		EMPTY,	LVCFMT_RIGHT,	60);								//SEARCHAVAIL
	InsertColumn(3,		EMPTY,	LVCFMT_RIGHT,	70);								//COMPLSOURCES
	InsertColumn(4,		EMPTY,	LVCFMT_LEFT,	DFLT_FILETYPE_COL_WIDTH);			//TYPE
	InsertColumn(5,		EMPTY,	LVCFMT_LEFT,	DFLT_HASH_COL_WIDTH, -1, true);		//FILEID
	InsertColumn(6,		EMPTY,	LVCFMT_LEFT,	DFLT_ARTIST_COL_WIDTH);				//ARTIST
	InsertColumn(7,		EMPTY,	LVCFMT_LEFT,	DFLT_ALBUM_COL_WIDTH);				//ALBUM
	InsertColumn(8,		EMPTY,	LVCFMT_LEFT,	DFLT_TITLE_COL_WIDTH);				//TITLE
	InsertColumn(9,		EMPTY,	LVCFMT_RIGHT,	DFLT_LENGTH_COL_WIDTH);				//LENGTH
	InsertColumn(10,	EMPTY,	LVCFMT_RIGHT,	DFLT_BITRATE_COL_WIDTH);			//BITRATE
	InsertColumn(11,	EMPTY,	LVCFMT_LEFT,	DFLT_CODEC_COL_WIDTH);				//CODEC
	InsertColumn(12,	EMPTY,	LVCFMT_LEFT,	DFLT_FOLDER_COL_WIDTH, -1, true);	//FOLDER
	InsertColumn(13,	EMPTY,	LVCFMT_LEFT,	50);								//KNOWN
	InsertColumn(14,	EMPTY,	LVCFMT_LEFT,	DFLT_HASH_COL_WIDTH, -1, true);		//AICHHASH
	InsertColumn(15,	EMPTY,	LVCFMT_RIGHT,	65, -1, true);						//SPAM_RATING

	SetAllIcons();

	// This states image list with that particular width is only there to let the listview control
	// auto-size the column width properly (double clicking on header divider). The items in the
	// list view contain a file type icon and optionally also a 'tree' icon (in case there are
	// more search entries related to one file hash). The width of that 'tree' icon (even if it is
	// not drawn) has to be known by the default list view control code to determine the total width
	// needed to show a particular item. The image list itself can be even empty, it is used by
	// the listview control just for querying the width of on image in the list, even if that image
	// was never added.
	CImageList imlDummyStates;
	imlDummyStates.Create(TREE_WIDTH, 16, ILC_COLOR, 0, 0);
	CImageList *pOldStates = SetImageList(&imlDummyStates, LVSIL_STATE);
	imlDummyStates.Detach();
	if (pOldStates)
		pOldStates->DeleteImageList();

	CreateMenus();

	LoadSettings();
	SetHighlightColors();

	SetSortArrow();
	m_pSortParam = MAKELONG(GetSortItem(), !GetSortAscending());
	UpdateSortHistory(m_pSortParam); // This will save sort parameter history in m_liSortHistory which will be used when we call GetNextSortOrder.
	
}


CSearchListCtrl::~CSearchListCtrl()
{
}
CSearchListCtrl::SChunkedSearchRemoveItem::SChunkedSearchRemoveItem()
	: nSearchID(0)
	, bChild(false)
{
	md4clr(abyFileHash);
}

bool CSearchListCtrl::BuildChunkedSearchRemoveItem(const CSearchFile *pFile, SChunkedSearchRemoveItem &item) const
{
	item = SChunkedSearchRemoveItem();
	if (pFile == NULL)
		return false;
	item.nSearchID = pFile->GetSearchID();
	md4cpy(item.abyFileHash, pFile->GetFileHash());
	item.bChild = pFile->GetListParent() != NULL;
	item.strFileName = pFile->GetFileName();
	return item.nSearchID != 0 && !isnulmd4(item.abyFileHash);
}

bool CSearchListCtrl::ResolveChunkedSearchRemoveItem(const SChunkedSearchRemoveItem &item, SSearchResultId &id, CSearchFile *&pFile) const
{
	pFile = NULL;
	id.Clear();
	if (searchlist == NULL || item.nSearchID == 0 || isnulmd4(item.abyFileHash))
		return false;
	id.Set(item.nSearchID, item.abyFileHash, item.bChild, item.strFileName);
	if (!id.IsValid())
		return false;
	pFile = searchlist->GetSearchFileByResultRow(id, item.bChild, item.strFileName);
	return pFile != NULL && id.EqualsRow(pFile->GetSearchID(), pFile->GetFileHash(), pFile->GetListParent() != NULL, pFile->GetFileName());
}

void CSearchListCtrl::ClearChunkedSearchRemoveItems(bool bReloadVisibleList)
{
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->ClearBulkOperationProgressState(CemuleDlg::BulkOperationProgressSearch);
	if (::IsWindow(m_hWnd))
		KillTimer(kTimerChunkedSearchRemove);
	m_vecChunkedSearchRemoveItems.clear();
	m_iNextChunkedSearchRemoveItem = 0;
	m_bChunkedSearchRemoveActive = false;
	m_uChunkedSearchRemoveProcessed = 0;
	m_uChunkedSearchRemoveStale = 0;
	m_uChunkedSearchRemoveFailed = 0;
	m_dwChunkedSearchRemoveStartedTick = 0;
	m_dwChunkedSearchRemoveLastProgressTick = 0;
	if (bReloadVisibleList && ::IsWindow(m_hWnd) && !theApp.IsClosing())
		ReloadList(false, kSearchListViewState);
}

void CSearchListCtrl::StartChunkedRemoveSelectedSearchResults(CTypedPtrList<CPtrList, CSearchFile*> &selectedList)
{
	ClearChunkedSearchRemoveItems(false);
	CMapPtrToPtr mapSelectedRows;
	CMapStringToPtr mapSelectedParentHashes;
	for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
		CSearchFile *pFile = selectedList.GetNext(pos);
		if (pFile == NULL)
			continue;
		SChunkedSearchRemoveItem item;
		if (BuildChunkedSearchRemoveItem(pFile, item)) {
			m_vecChunkedSearchRemoveItems.push_back(item);
			mapSelectedRows.SetAt(pFile, reinterpret_cast<void*>(1));
			if (pFile->GetListParent() == NULL) {
				const CString strParentHashKey(BuildSearchRemoveHashKey(pFile->GetSearchID(), pFile->GetFileHash()));
				if (!strParentHashKey.IsEmpty())
					mapSelectedParentHashes.SetAt(strParentHashKey, reinterpret_cast<void*>(1));
			}
		}
	}

	if (m_vecChunkedSearchRemoveItems.empty())
		return;

	SaveListState(m_nResultsID, kSearchListViewState);
	SetRedraw(false);
	std::vector<CSearchFile*> keptItems;
	keptItems.reserve(m_ListedItemsVector.size());
	for (size_t i = 0; i < m_ListedItemsVector.size(); ++i) {
		CSearchFile *pListedFile = m_ListedItemsVector[i];
		if (pListedFile == NULL)
			continue;
		void *pDummy = NULL;
		if (mapSelectedRows.Lookup(pListedFile, pDummy))
			continue;
		const CString strListedHashKey(BuildSearchRemoveHashKey(pListedFile->GetSearchID(), pListedFile->GetFileHash()));
		if (!strListedHashKey.IsEmpty() && mapSelectedParentHashes.Lookup(strListedHashKey, pDummy))
			continue;
		keptItems.push_back(pListedFile);
	}
	m_ListedItemsVector.swap(keptItems);
	RebuildListedItemsMap();
	UpdateSearchListItemCount(*this, m_ListedItemsVector.size());
	RestoreListState(m_nResultsID, kSearchListViewState, false);
	SetRedraw(true);
	Invalidate(FALSE);

	m_bChunkedSearchRemoveActive = true;
	m_iNextChunkedSearchRemoveItem = 0;
	m_uChunkedSearchRemoveProcessed = 0;
	m_uChunkedSearchRemoveStale = 0;
	m_uChunkedSearchRemoveFailed = 0;
	m_dwChunkedSearchRemoveStartedTick = ::GetTickCount();
	m_dwChunkedSearchRemoveLastProgressTick = m_dwChunkedSearchRemoveStartedTick;
	if (theApp.emuledlg != NULL) {
		theApp.emuledlg->SetBulkOperationProgressState(CemuleDlg::BulkOperationProgressSearch, true, true, false, true, false, 0, static_cast<UINT>(m_vecChunkedSearchRemoveItems.size()));
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
	}

	if (SetTimer(kTimerChunkedSearchRemove, 1, NULL) == 0) {
		AddDebugLogLine(DLP_HIGH, false, _T("Chunked search result remove aborted because the continuation timer could not be created. total=%u\n"), static_cast<UINT>(m_vecChunkedSearchRemoveItems.size()));
		FinishChunkedSearchRemoveItems(true);
	}
}

void CSearchListCtrl::ProcessChunkedSearchRemoveItems()
{
	if (!m_bChunkedSearchRemoveActive)
		return;
	if (theApp.IsClosing() || searchlist == NULL) {
		FinishChunkedSearchRemoveItems(true);
		return;
	}

	const DWORD dwSliceStartTick = ::GetTickCount();
	UINT uProcessedInSlice = 0;
	while (m_iNextChunkedSearchRemoveItem < static_cast<INT_PTR>(m_vecChunkedSearchRemoveItems.size())) {
		const SChunkedSearchRemoveItem &item = m_vecChunkedSearchRemoveItems[static_cast<size_t>(m_iNextChunkedSearchRemoveItem++)];
		SSearchResultId id;
		CSearchFile *pFile = NULL;
		if (ResolveChunkedSearchRemoveItem(item, id, pFile)) {
			searchlist->RemoveResult(pFile);
			++m_uChunkedSearchRemoveProcessed;
		} else
			++m_uChunkedSearchRemoveStale;
		++uProcessedInSlice;

		const DWORD dwNow = ::GetTickCount();
		if (static_cast<DWORD>(dwNow - m_dwChunkedSearchRemoveLastProgressTick) >= theApp.GetTimeBudgetedProgressTraceMs(CemuleApp::TimeBudgetSearchResultRemove)) {
			m_dwChunkedSearchRemoveLastProgressTick = dwNow;
			AddDebugLogLine(DLP_VERYLOW, false, _T("Chunked search result remove progress. processed=%u stale=%u failed=%u remaining=%d\n"), m_uChunkedSearchRemoveProcessed, m_uChunkedSearchRemoveStale, m_uChunkedSearchRemoveFailed, static_cast<int>(m_vecChunkedSearchRemoveItems.size() - static_cast<size_t>(m_iNextChunkedSearchRemoveItem)));
		}

		if (theApp.IsTimeBudgetExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchResultRemove))
			break;
	}

	DWORD dwSliceElapsed = 0;
	if (theApp.IsTimeBudgetHardExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchResultRemove, &dwSliceElapsed))
		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetSearchResultRemove, _T("ProcessChunkedSearchRemoveItems"), dwSliceElapsed, uProcessedInSlice, static_cast<INT_PTR>(m_vecChunkedSearchRemoveItems.size()) - m_iNextChunkedSearchRemoveItem);

	if (m_iNextChunkedSearchRemoveItem >= static_cast<INT_PTR>(m_vecChunkedSearchRemoveItems.size()))
		FinishChunkedSearchRemoveItems(false);
	else if (theApp.emuledlg != NULL) {
		const UINT uTotal = static_cast<UINT>(m_vecChunkedSearchRemoveItems.size());
		theApp.emuledlg->SetBulkOperationProgressState(CemuleDlg::BulkOperationProgressSearch, true, true, false, true, false, min(static_cast<UINT>(m_iNextChunkedSearchRemoveItem), uTotal), uTotal);
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
	}
}

void CSearchListCtrl::FinishChunkedSearchRemoveItems(bool bAborted)
{
	if (::IsWindow(m_hWnd))
		KillTimer(kTimerChunkedSearchRemove);
	const UINT uTotal = static_cast<UINT>(m_vecChunkedSearchRemoveItems.size());
	const DWORD dwElapsed = m_dwChunkedSearchRemoveStartedTick != 0 ? static_cast<DWORD>(::GetTickCount() - m_dwChunkedSearchRemoveStartedTick) : 0;
	AddDebugLogLine(DLP_LOW, false, _T("Chunked search result remove %s. processed=%u stale=%u failed=%u total=%u elapsed=%u\n"), bAborted ? _T("aborted") : _T("completed"), m_uChunkedSearchRemoveProcessed, m_uChunkedSearchRemoveStale, m_uChunkedSearchRemoveFailed, uTotal, dwElapsed);
	m_vecChunkedSearchRemoveItems.clear();
	m_iNextChunkedSearchRemoveItem = 0;
	m_bChunkedSearchRemoveActive = false;
	m_uChunkedSearchRemoveProcessed = 0;
	m_uChunkedSearchRemoveStale = 0;
	m_uChunkedSearchRemoveFailed = 0;
	m_dwChunkedSearchRemoveStartedTick = 0;
	m_dwChunkedSearchRemoveLastProgressTick = 0;
	if (theApp.emuledlg != NULL) {
		theApp.emuledlg->ClearBulkOperationProgressState(CemuleDlg::BulkOperationProgressSearch);
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
	}
	if (!theApp.IsClosing() && ::IsWindow(m_hWnd)) {
		if (bAborted)
			ReloadList(false, kSearchListViewState);
		else {
			UpdateTabHeader(m_nResultsID, EMPTY, false);
			AutoSelectItem();
			Invalidate(FALSE);
		}
	}
}


void CSearchListCtrl::CancelActiveChunkedSearchOperation()
{
	ClearChunkedSearchRemoveItems(true);
	HideOperationOverlay();
	if (theApp.emuledlg != NULL) {
		theApp.emuledlg->ClearBulkOperationProgressState(CemuleDlg::BulkOperationProgressSearch);
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
	}
}


void CSearchListCtrl::OnOperationOverlayCancel()
{
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->CancelActiveBulkOperations();
}

void CSearchListCtrl::Localize()
{
	static const LPCTSTR uids[16] =
	{
		_T("DL_FILENAME"), _T("DL_SIZE"), 0/*SEARCHAVAIL*/, _T("COMPLSOURCES"), _T("TYPE")
		, _T("FILEID"), _T("ARTIST"), _T("ALBUM"), _T("TITLE"), _T("LENGTH")
		, _T("BITRATE"), _T("CODEC"), _T("FOLDER"), _T("KNOWN"), _T("AICHHASH")
		, _T("SPAM_RATING")
	};

	LocaliseHeaderCtrl(uids, _countof(uids));

	HDITEM hdi;
	hdi.mask = HDI_TEXT;
	CString strRes(GetResString(_T("SEARCHAVAIL")));
	if (thePrefs.IsExtControlsEnabled())
		strRes.AppendFormat(_T(" (%s)"), (LPCTSTR)GetResString(_T("DL_SOURCES"))); //modify "availability" header
	hdi.pszText = (LPTSTR)(LPCTSTR)strRes;
	GetHeaderCtrl()->SetItem(2, &hdi);

	CreateMenus();
}

void CSearchListCtrl::AddResult(CSearchFile* toshow)
{
	if (m_bChunkedSearchRemoveActive)
		return;
	int m_iIndex = -1;
	// Ignore hidden children of collapsed parents
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->searchwnd || !IsWindowVisible() || toshow == NULL || toshow->GetSearchID() != m_nResultsID || !ShouldShowSearchItemInList(toshow) || (m_ListedItemsMap.Lookup(toshow, m_iIndex) && m_iIndex >= 0))
		return;

	SaveListState(m_nResultsID, kSearchListViewState); // Save selections and scroll state
	if (theApp.searchlist == NULL)
		return;

	SetRedraw(false); // Suspend painting

	// Determine insert position
	int insertPos = (int)m_ListedItemsVector.size();
	if (toshow->GetListParent()) {
		int parentIdx;
		if (m_ListedItemsMap.Lookup(toshow->GetListParent(), parentIdx))
			insertPos = parentIdx + 1;
	}
	m_ListedItemsVector.insert(m_ListedItemsVector.begin() + insertPos, toshow);

	SortListedItemsRaw();
	GroupListedItemsByBottomCandidates();
	RebuildListedItemsMap(); // Rebuild the map after sorting.
	UpdateSearchListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list before restoring state.
	RestoreListState(m_nResultsID, kSearchListViewState, false); // Restore selections and scroll state
	SetRedraw(true); // Resume painting
	Invalidate(FALSE);
}

void CSearchListCtrl::RemoveResult(CSearchFile* toremove, bool bUpdateTabCount)
{
	if (m_bChunkedSearchRemoveActive)
		return;
	int m_iIndex = -1;
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->searchwnd || !IsWindowVisible() || toremove == NULL)
		return;
		if (toremove->GetSearchID() != m_nResultsID || !m_ListedItemsMap.Lookup(toremove, m_iIndex) || m_iIndex < 0)
		return;

	SaveListState(m_nResultsID, kSearchListViewState); // Save selections and scroll state
	SetRedraw(false); // Suspend painting

	// remove_if will move items to delete (parent + its children) at the end of the vector.
	auto itFirst = std::remove_if(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), [toremove](CSearchFile* f) { return f == toremove || f->GetListParent() == toremove; });
	m_ListedItemsVector.erase(itFirst, m_ListedItemsVector.end()); // Erase the moved items from the vector.
	RebuildListedItemsMap();

	UpdateSearchListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list before restoring state.
	RestoreListState(m_nResultsID, kSearchListViewState, false); // Restore selections and scroll state
	SetRedraw(true);// resume painting
	Invalidate(FALSE);

	if (bUpdateTabCount)
		UpdateTabHeader(m_nResultsID, EMPTY, false);
}

void CSearchListCtrl::UpdateSources(CSearchFile* toupdate, const bool bSort)
{
	if (m_bChunkedSearchRemoveActive)
		return;
	int m_iIndex = -1;
	// Ignore hidden children of collapsed parents
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->searchwnd || !IsWindowVisible() || toupdate == NULL || toupdate->GetSearchID() != m_nResultsID || (toupdate->GetListParent() && !toupdate->GetListParent()->IsListExpanded()) || !m_ListedItemsMap.Lookup(toupdate, m_iIndex) || m_iIndex < 0)
		return;

	int iRedrawFirst = m_iIndex;
	int iRedrawLast = m_iIndex;

	// Update child items
	if (toupdate->IsListExpanded()) {
		const SearchChildList* pChildren = theApp.searchlist->GetSearchChildrenForParent(toupdate);
		if (pChildren != NULL) {
			for (POSITION pos = pChildren->GetHeadPosition(); pos != NULL;) {
				CSearchFile* cur = pChildren->GetNext(pos);
				int childIdx;
				if (cur != NULL && m_ListedItemsMap.Lookup(cur, childIdx)) {
					iRedrawFirst = min(iRedrawFirst, childIdx);
					iRedrawLast = max(iRedrawLast, childIdx);
				}
			}
		}
	}

	if (bSort) {
		SaveListState(m_nResultsID, kSearchListViewState);
		SetRedraw(false);
		SortListedItemsRaw();
		GroupListedItemsByBottomCandidates();
		RebuildListedItemsMap();
		UpdateSearchListItemCount(*this, m_ListedItemsVector.size());
		RestoreListState(m_nResultsID, kSearchListViewState, false);
		SetRedraw(true);
		RequestFullRedrawAsync();
	} else {
		RequestRowRedrawAsync(iRedrawFirst, iRedrawLast);
		MarkListedModelCurrent();
	}
}

void CSearchListCtrl::MarkListedModelCurrent()
{
	m_lListedItemsModelSequence = searchlist != NULL ? searchlist->GetSearchModelSequence() : 0;
}

bool CSearchListCtrl::IsListedModelCurrent(uint32 nSearchID) const
{
	return searchlist != NULL && m_nResultsID == nSearchID && m_lListedItemsModelSequence == searchlist->GetSearchModelSequence();
}

void CSearchListCtrl::UpdateSearch(CSearchFile* toupdate)
{
	if (m_bChunkedSearchRemoveActive)
		return;
	int m_iIndex = -1;
	// Ignore hidden children of collapsed parents
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->searchwnd || !IsWindowVisible() || toupdate == NULL || toupdate->GetSearchID() != m_nResultsID || !m_ListedItemsMap.Lookup(toupdate, m_iIndex) || m_iIndex < 0)
		return;

	RequestRowRedrawAsync(m_iIndex, m_iIndex);
	MarkListedModelCurrent();
}

bool CSearchListCtrl::ShouldShowSearchItemInList(const CSearchFile *pSearchFile) const
{
	return pSearchFile != NULL && !IsFilteredOut(pSearchFile) && (pSearchFile->GetListParent() == NULL || pSearchFile->GetListParent()->IsListExpanded());
}

void CSearchListCtrl::BuildVisibleSearchItems(const CTypedPtrList<CPtrList, CSearchFile*> &sourceList, std::vector<CSearchFile*> &visibleItems) const
{
	visibleItems.clear();
	visibleItems.reserve(static_cast<size_t>(sourceList.GetCount()));
	for (POSITION pos = sourceList.GetHeadPosition(); pos != NULL;) {
		CSearchFile* pCurFile = sourceList.GetNext(pos);
		if (ShouldShowSearchItemInList(pCurFile))
			visibleItems.push_back(pCurFile);
	}
}

CString CSearchListCtrl::GetListedItemDisplayText(int iItem, int iSubItem) const
{
	const CSearchFile *pSearchFile = ResolveSearchFileByRowIndex(iItem);
	return pSearchFile != NULL ? GetItemDisplayText(pSearchFile, iSubItem) : EMPTY;
}

bool CSearchListCtrl::BuildSearchInfoTipText(int iItem, CString& strText) const
{
	strText.Empty();
	const CSearchFile* file = ResolveSearchFileByRowIndex(iItem);
	if (file == NULL)
		return false;

	CString strHead(file->GetFileName());
	strHead.AppendFormat(_T("\n") _T("%s %s\n") _T("%s %s\n<br_head>\n"), (LPCTSTR)GetResString(_T("FD_HASH")), (LPCTSTR)md4str(file->GetFileHash()), (LPCTSTR)GetResString(_T("FD_SIZE")), (LPCTSTR)CastItoXBytes((uint64)file->GetFileSize()));
	strText = strHead;

	const CArray<CTag*, CTag*>& tags = file->GetTags();
	for (INT_PTR i = 0; i < tags.GetCount(); ++i) {
		const CTag* tag = tags[i];
		if (tag == NULL)
			continue;

		CString strTag;
		switch (tag->GetNameID()) {
		case FT_FILETYPE:
			strTag.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("TYPE")), (LPCTSTR)tag->GetStr());
			break;
		case FT_FILEFORMAT:
			strTag.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("SEARCHEXTENTION")), (LPCTSTR)tag->GetStr());
			break;
		case FT_SOURCES:
			strTag.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("SEARCHAVAIL")), file->GetSourceCount());
			break;
		default:
			if (tag->GetNameID() == FT_FILENAME || tag->GetNameID() == FT_FILESIZE)
				break;
			if (tag->HasName()) {
				strTag.Format(_T("%hs: "), tag->GetName());
				strTag.SetAt(0, _totupper(strTag[0]));
			} else {
				extern CString GetName(const CTag *pTag);
				const CString& strTagName(GetName(tag));
				if (!strTagName.IsEmpty())
					strTag.Format(_T("%s: "), (LPCTSTR)strTagName);
			}
			if (!strTag.IsEmpty()) {
				if (tag->IsStr())
					strTag += tag->GetStr();
				else if (tag->IsInt())
					strTag.AppendFormat(_T("%u"), tag->GetInt());
				else if (tag->IsFloat())
					strTag.AppendFormat(_T("%f"), tag->GetFloat());
			}
			break;
		}

		if (!strTag.IsEmpty())
			strText.AppendFormat(_T("%s\n"), (LPCTSTR)strTag);
	}

	strText.AppendChar(TOOLTIP_AUTOFORMAT_SUFFIX_CH);
	return true;
}

CSearchFile* CSearchListCtrl::ResolveSearchFileByRowIndex(int iItem) const
{
	if (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size())
		return NULL;
	CSearchFile* pSearchFile = m_ListedItemsVector[static_cast<size_t>(iItem)];
	return pSearchFile;
}


void CSearchListCtrl::CollectSelectedSearchFiles(CTypedPtrList<CPtrList, CSearchFile*> &selectedList) const
{
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		CSearchFile *pSearchFile = ResolveSearchFileByRowIndex(GetNextSelectedItem(pos));
		if (pSearchFile != NULL)
			selectedList.AddTail(pSearchFile);
	}
}

void CSearchListCtrl::QueueDeferredReload(const bool bSortCurrentList, const EListStateField LsfFlag, UINT uDelayMs)
{
	if (theApp.IsClosing() || !::IsWindow(m_hWnd))
		return;

	m_bDeferredSearchReloadPending = true;
	m_bDeferredSearchReloadSort = m_bDeferredSearchReloadSort || bSortCurrentList;
	m_eDeferredSearchReloadState = LsfFlag;
	if (uDelayMs == 0)
		uDelayMs = kDeferredSearchReloadDelayMs;
	if (SetTimer(kTimerDeferredSearchReload, uDelayMs, NULL) == 0) {
		AddDebugLogLine(DLP_HIGH, false, _T("Deferred search reload timer could not be started. Falling back to immediate reload. sort=%u\n"), bSortCurrentList ? 1U : 0U);
		KillTimer(kTimerDeferredSearchReload);
		m_bDeferredSearchReloadPending = false;
		m_bDeferredSearchReloadSort = false;
		ReloadList(bSortCurrentList, LsfFlag);
	}
}

void CSearchListCtrl::ReloadList(const bool bSortCurrentList, const EListStateField LsfFlag)
{
	if (m_bChunkedSearchRemoveActive)
		return;
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->searchwnd || !IsWindowVisible())
		return;

	const DWORD dwReloadStartTick = ::GetTickCount();
	bool bInitializing = (m_iDataSize == -1); // Check if this is the first call to ReloadList

	// Initializing the vector and map
	if (bInitializing) {
		m_iDataSize = 10007; // Any reasonable prime number for the initial size.
		m_ListedItemsVector.reserve(m_iDataSize);
		m_ListedItemsMap.InitHashTable(m_iDataSize);
	} else
		SaveListState(m_nResultsID, LsfFlag); // Save selections, sort and scroll values for the previous m_nResultsID if this is not the first call.

	// ReloadList should always use search ID of the active search tab.
	bool bCurrentClientSharedFiles = false;
	int cur_sel = theApp.emuledlg->searchwnd->m_pwndResults->searchselect.GetCurSel();
	if (cur_sel >= 0) {
		TCITEM item;
		item.mask = TCIF_PARAM;
		if (theApp.emuledlg->searchwnd->m_pwndResults->searchselect.GetItem(cur_sel, &item) && item.lParam != NULL) {
			SSearchParams *pSearchParams = reinterpret_cast<SSearchParams*>(item.lParam);
			m_nResultsID = pSearchParams->dwSearchID;
			bCurrentClientSharedFiles = pSearchParams->bClientSharedFiles;
		}
	}

	if (theApp.searchlist == NULL)
		return;

	SetRedraw(false); // Suspend painting
	if (!bInitializing && (LsfFlag & LSF_SELECTION) != 0) {
		SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
		SetSelectionMark(-1);
	}
	{
		CSingleLock searchModelLock(theApp.searchlist->GetSearchModelLock(), TRUE);
		const SearchList* list = theApp.searchlist->GetSearchListForID(m_nResultsID);
		if (!list) {
			m_lListedItemsModelSequence = 0;
			SetRedraw(true);
			return;
		}

		if (!bSortCurrentList)
			BuildVisibleSearchItems(*list, m_ListedItemsVector);

		SortListedItemsRaw();
		GroupListedItemsByBottomCandidates();
		RebuildListedItemsMap();
	}

	UpdateSearchListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list before restoring state.

	if (!bInitializing)
		RestoreListState(m_nResultsID, LsfFlag, false); // Restore selections, sort and scroll values if this is not the first call.

	UpdateTabHeader(m_nResultsID, EMPTY, false);
	SetRedraw(true); // Resume painting
	Invalidate(); //Force redraw
	DWORD dwReloadElapsed = 0;
	if (theApp.IsTimeBudgetHardExceeded(dwReloadStartTick, CemuleApp::TimeBudgetSearchRedraw, &dwReloadElapsed))
		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetSearchRedraw, _T("SearchListCtrl::ReloadList"), dwReloadElapsed, static_cast<UINT>(m_ListedItemsVector.size()), 0);
}

// Index map after vector changes
void CSearchListCtrl::RebuildListedItemsMap()
{
	m_ListedItemsMap.RemoveAll();
	if (!m_ListedItemsVector.empty())
		m_ListedItemsMap.InitHashTable(static_cast<UINT>(m_ListedItemsVector.size() * 2 + 1));
	for (int i = 0; i < static_cast<int>(m_ListedItemsVector.size()); ++i)
		m_ListedItemsMap[m_ListedItemsVector[i]] = i;
	MarkListedModelCurrent();
}

const bool CSearchListCtrl::SortFunc(const CSearchFile* first, const CSearchFile* second)
{
	return SortProc((LPARAM)first, (LPARAM)second, m_pSortParam) < 0; // If the first one has a smaller value returns true, otherwise returns false.
}

CObject* CSearchListCtrl::GetItemObject(int iIndex) const
{
	if (iIndex < 0 || static_cast<size_t>(iIndex) >= m_ListedItemsVector.size())
		return nullptr;
	return ResolveSearchFileByRowIndex(iIndex);
}

void CSearchListCtrl::UpdateTabHeader(uint32 nResultsID, CString strClientHash, bool bUpdateAllSharedListTabs)
{
	CClosableTabCtrl &searchselect = theApp.emuledlg->searchwnd->GetSearchSelector();
	TCITEM ti;
	ti.mask = TCIF_PARAM;

	for (int iTabIndex = searchselect.GetItemCount(); --iTabIndex >= 0;)
		if (searchselect.GetItem(iTabIndex, &ti) && ti.lParam != NULL) {
			const SSearchParams* pSearchParams = reinterpret_cast<SSearchParams*>(ti.lParam);
			// Update tab header for the specified search ID or client hash, or all shared file tabs
			if ((pSearchParams->dwSearchID == nResultsID && strClientHash.IsEmpty() && !bUpdateAllSharedListTabs) // A specific nResultsID is given
				|| (!strClientHash.IsEmpty() && strClientHash == pSearchParams->m_strClientHash) || (bUpdateAllSharedListTabs && pSearchParams->bClientSharedFiles)) { // A specific m_strClientHash is given or bUpdateAllSharedListTabs is true
				CString strTabLabel(pSearchParams->strSearchTitle);

				if (pSearchParams->bClientSharedFiles && (thePrefs.GetRemoteSharedFilesUserHash() || thePrefs.GetRemoteSharedFilesClientNote()) && !pSearchParams->m_strClientHash.IsEmpty()) {
					CString m_strClientHash = pSearchParams->m_strClientHash;
					uchar m_uchClientHash[MDX_DIGEST_SIZE];
					if (strmd4(m_strClientHash, m_uchClientHash)) {
						CUpDownClient* pTabClient = theApp.clientlist != NULL
							? theApp.clientlist->AcquireTrackedClientByUserHash(m_uchClientHash, thePrefs.GetClientHistory())
							: NULL;
						if (pTabClient != NULL) {
							if (thePrefs.GetRemoteSharedFilesUserHash() && !isnulmd4(pTabClient->GetUserHash()))
								strTabLabel = md4str(pTabClient->GetUserHash()); // Replace search title with client hash

							if (thePrefs.GetRemoteSharedFilesClientNote() && !pTabClient->m_strClientNote.IsEmpty())
								strTabLabel.AppendFormat(_T(" [%s]"), (LPCTSTR)pTabClient->m_strClientNote); // Append client note

							pTabClient->ReleaseRuntimeReference();
						}
					}
				}

				const uint32 m_uSearchID = pSearchParams->dwSearchID;
				const uint32 m_uOriginalResultCount = theApp.searchlist->GetOriginalFoundFiles(m_uSearchID);
				const uint32 m_uParentItemsCount = theApp.searchlist->GetParentItemCount(m_uSearchID);
				const bool m_bHasMergeHistory = theApp.searchlist->HasMergedSearchHistory(m_uSearchID);

				if (m_bHasMergeHistory || m_uParentItemsCount != m_uOriginalResultCount) {
					strTabLabel.AppendFormat(_T(" (%u/%u)"), m_uParentItemsCount, m_uOriginalResultCount);
				} else if (m_uParentItemsCount > 0) {
					strTabLabel.AppendFormat(_T(" (%u)"), m_uParentItemsCount);
				}

				DupAmpersand(strTabLabel);
				ti.pszText = const_cast<LPTSTR>((LPCTSTR)strTabLabel);
				ti.mask = TCIF_TEXT;
				searchselect.SetItem(iTabIndex, &ti);
				theApp.emuledlg->searchwnd->m_pwndResults->searchselect.UpdateTabToolTips(iTabIndex);
				if (searchselect.GetCurSel() != iTabIndex)
					searchselect.HighlightItem(iTabIndex);
				break;
			}
		}
}

bool CSearchListCtrl::IsComplete(const CSearchFile *pFile, UINT uSources) const
{

	// '< 0' ... unknown; treat 'unknown' as complete
	// '> 0' ... complete

	// '= 0' ... not complete
	return pFile->IsComplete(uSources, pFile->GetCompleteSourceCount()) != 0;

}

CString CSearchListCtrl::GetCompleteSourcesDisplayString(const CSearchFile *pFile, UINT uSources, bool *pbComplete) const
{
	UINT uCompleteSources = pFile->GetCompleteSourceCount();
	int iComplete = pFile->IsComplete(uSources, uCompleteSources);

	// If we have no 'Complete' info at all but the file size is <= PARTSIZE,
	// though we know that the file is complete (otherwise it would not be shared).
	if (iComplete < 0 && (uint64)pFile->GetFileSize() <= PARTSIZE) {
		iComplete = 1;
		// If this search result is from a remote client's shared file list, we know the 'complete' count.
		if (pFile->GetDirectory() != NULL)
			uCompleteSources = 1;
	}

	CString str;
	if (iComplete < 0) {		// '< 0' ... unknown
		str += _T('?');
		if (pbComplete)
			*pbComplete = true;	// treat 'unknown' as complete
	} else if (iComplete > 0) {	// '> 0' ... we know it's complete
		if (uSources && uCompleteSources) {
			str.Format(_T("%u%%"), (uCompleteSources * 100) / uSources);
			if (thePrefs.IsExtControlsEnabled())
				str.AppendFormat(_T(" (%u)"), uCompleteSources);
		} else {
			// we know it's complete, but we don't know the degree. (for files <= PARTSIZE in Kad searches)
			str = GetResString(_T("YES"));
		}
		if (pbComplete)
			*pbComplete = true;
	} else {					// '= 0' ... we know it's not complete
		str = _T("0%");
		if (thePrefs.IsExtControlsEnabled())
			str.AppendFormat(_T(" (0)"));
		if (pbComplete)
			*pbComplete = false;
	}
	return str;
}

int CSearchListCtrl::CompareSearchFilesRaw(const CSearchFile *item1, const CSearchFile *item2, LPARAM lParamSort) const
{
	if (item1 == NULL || item2 == NULL)
		return item1 == item2 ? 0 : (item1 == NULL ? 1 : -1);
	const bool bDirect = !HIWORD(lParamSort);
	int iResult = 0;
	if (item1->GetListParent() == NULL && item2->GetListParent() != NULL) {
		if (item1 == item2->GetListParent())
			return -1;
		iResult = Compare(item1, item2->m_list_parent, lParamSort, bDirect);
		if (!bDirect)
			iResult = -iResult;
	} else if (item2->GetListParent() == NULL && item1->GetListParent() != NULL) {
		if (item1->m_list_parent == item2)
			return 1;
		iResult = Compare(item1->GetListParent(), item2, lParamSort, bDirect);
		if (!bDirect)
			iResult = -iResult;
	} else if (item1->GetListParent() == NULL) {
		iResult = Compare(item1, item2, lParamSort, bDirect);
		if (!bDirect)
			iResult = -iResult;
	} else {
		iResult = Compare(item1->GetListParent(), item2->GetListParent(), lParamSort, bDirect);
		if (iResult != 0)
			return bDirect ? iResult : -iResult;

		if ((item1->GetListParent() == NULL && item2->GetListParent() != NULL) || (item2->GetListParent() == NULL && item1->GetListParent() != NULL))
			return item1->GetListParent() ? 1 : -1;
		iResult = CompareChild(item1, item2, lParamSort);
	}

	if (iResult == 0 && thePrefs.GetGroupKnownAtTheBottom()) {
		const CSearchFile *pSortItem1 = GetSearchFileSortGroupFile(item1);
		const CSearchFile *pSortItem2 = GetSearchFileSortGroupFile(item2);
		iResult = CompareUnsigned(static_cast<uint32>(GetSearchFileKnownTieRank(pSortItem1)), static_cast<uint32>(GetSearchFileKnownTieRank(pSortItem2)));
	}

	if (iResult == 0) {
		LPARAM iNextSort = GetNextSortOrder(lParamSort);
		if (iNextSort != -1)
			iResult = CompareSearchFilesRaw(item1, item2, iNextSort);
	}

	return iResult;
}

void CSearchListCtrl::SortListedItemsRaw()
{
	if (m_ListedItemsVector.size() < 2 || theApp.searchlist == NULL)
		return;

	CSingleLock searchModelLock(theApp.searchlist->GetSearchModelLock(), TRUE);
	CombinedSort(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), [this](const CSearchFile* left, const CSearchFile* right) -> bool
	{
		return CompareSearchFilesRaw(left, right, m_pSortParam) < 0;
	});

}


bool CSearchListCtrl::GroupListedItemsByBottomCandidates()
{
	if (!ShouldApplySearchBottomGrouping() || m_ListedItemsVector.size() < 2 || searchlist == NULL)
		return false;

	std::vector<CSearchFile*> normalItems;
	std::vector<CSearchFile*> knownItems;
	std::vector<CSearchFile*> blockedItems;
	normalItems.reserve(m_ListedItemsVector.size());
	knownItems.reserve(m_ListedItemsVector.size());
	blockedItems.reserve(m_ListedItemsVector.size());

	for (size_t i = 0; i < m_ListedItemsVector.size(); ++i) {
		CSearchFile *pFile = m_ListedItemsVector[i];
		if (pFile == NULL)
			continue;
		const CSearchFile *pSortFile = GetSearchFileSortGroupFile(pFile);
		const int iRank = GetSearchFileBottomGroupRank(pSortFile);
		if (iRank >= 2)
			blockedItems.push_back(pFile);
		else if (iRank == 1 && thePrefs.GetGroupKnownAtTheBottom())
			knownItems.push_back(pFile);
		else if (iRank == 1)
			blockedItems.push_back(pFile);
		else
			normalItems.push_back(pFile);
	}

	const int iNonEmptyBucketCount = (normalItems.empty() ? 0 : 1) + (knownItems.empty() ? 0 : 1) + (blockedItems.empty() ? 0 : 1);
	if (iNonEmptyBucketCount < 2)
		return false;

	m_ListedItemsVector.clear();
	m_ListedItemsVector.reserve(normalItems.size() + knownItems.size() + blockedItems.size());
	m_ListedItemsVector.insert(m_ListedItemsVector.end(), normalItems.begin(), normalItems.end());
	m_ListedItemsVector.insert(m_ListedItemsVector.end(), knownItems.begin(), knownItems.end());
	m_ListedItemsVector.insert(m_ListedItemsVector.end(), blockedItems.begin(), blockedItems.end());
	return true;
}

void CSearchListCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	bool sortAscending;
	if (GetSortItem() != pNMLV->iSubItem)
		switch (pNMLV->iSubItem) {
		case 2: // Availability
		case 3: // Complete Sources
			sortAscending = false;
			break;
		default:
			sortAscending = true;
		}
	else
		sortAscending = !GetSortAscending();

	// Sort table
	UpdateSortHistory(MAKELONG(pNMLV->iSubItem, !sortAscending));
	SetSortArrow(pNMLV->iSubItem, sortAscending);
	// Although SortItems will not sort anything since this is a virtual list, it will save sort parameter
	// history in m_liSortHistory which will be used when we call GetNextSortOrder.
	m_pSortParam = MAKELONG(pNMLV->iSubItem, !sortAscending);
	SortItems(SortProc, m_pSortParam);
	ReloadList(true, kSearchListViewState);
	*pResult = 0;
}

int CALLBACK CSearchListCtrl::SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort)
{
	const CSearchFile *item1 = reinterpret_cast<CSearchFile*>(lParam1);
	const CSearchFile *item2 = reinterpret_cast<CSearchFile*>(lParam2);
	if (item1 == NULL || item2 == NULL)
		return item1 == item2 ? 0 : (item1 == NULL ? 1 : -1);
	if (theApp.searchlist == NULL)
		return 0;
	CSingleLock searchModelLock(theApp.searchlist->GetSearchModelLock(), TRUE);
	bool bDirect = !HIWORD(lParamSort);

	int iResult;
	if (item1->GetListParent() == NULL && item2->GetListParent() != NULL) {
		if (item1 == item2->GetListParent())
			return -1;
		iResult = Compare(item1, item2->m_list_parent, lParamSort, bDirect);
		if (!bDirect)
			iResult = -iResult;
	} else if (item2->GetListParent() == NULL && item1->GetListParent() != NULL) {
		if (item1->m_list_parent == item2)
			return 1;
		iResult = Compare(item1->GetListParent(), item2, lParamSort, bDirect);
		if (!bDirect)
			iResult = -iResult;
	} else if (item1->GetListParent() == NULL) {
		iResult = Compare(item1, item2, lParamSort, bDirect);
		if (!bDirect)
			iResult = -iResult;
	} else {
		iResult = Compare(item1->GetListParent(), item2->GetListParent(), lParamSort, bDirect);
		if (iResult != 0)
			return bDirect ? iResult : -iResult;

		if ((item1->GetListParent() == NULL && item2->GetListParent() != NULL) || (item2->GetListParent() == NULL && item1->GetListParent() != NULL))
			return item1->GetListParent() ? 1 : -1;
		iResult = CompareChild(item1, item2, lParamSort);
	}

	if (iResult == 0 && thePrefs.GetGroupKnownAtTheBottom()) {
		const CSearchFile *pSortItem1 = GetSearchFileSortGroupFile(item1);
		const CSearchFile *pSortItem2 = GetSearchFileSortGroupFile(item2);
		iResult = CompareUnsigned(static_cast<uint32>(GetSearchFileKnownTieRank(pSortItem1)), static_cast<uint32>(GetSearchFileKnownTieRank(pSortItem2)));
	}

	// Call secondary sort order, if the first one resulted as equal
	if (iResult == 0) {
		LPARAM iNextSort = theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.GetNextSortOrder(lParamSort);
		if (iNextSort != -1)
			iResult = SortProc(lParam1, lParam2, iNextSort);
	}

	return iResult;
}

int CSearchListCtrl::CompareChild(const CSearchFile *item1, const CSearchFile *item2, LPARAM lParamSort)
{
	int iResult;
	switch (LOWORD(lParamSort)) {
	case 0:	//filename
		iResult = CompareLocaleStringNoCase(item1->GetFileName(), item2->GetFileName());
		break;
	case 14: // AICH Hash
		iResult = CompareAICHHash(item1->GetFileIdentifierC(), item2->GetFileIdentifierC(), true);
		break;
	default: // always sort by descending availability
		iResult = -CompareUnsigned(item1->GetSourceCount(), item2->GetSourceCount());
	}
	return HIWORD(lParamSort) ? -iResult : iResult;
}

int CSearchListCtrl::Compare(const CSearchFile *item1, const CSearchFile *item2, LPARAM lParamSort, bool bSortAscending)
{
	const CSearchFile *pSortItem1 = GetSearchFileSortGroupFile(item1);
	const CSearchFile *pSortItem2 = GetSearchFileSortGroupFile(item2);
	const int iBottomGroupResult = CompareSearchFixedGroupRank(GetSearchFileBottomGroupRank(pSortItem1), GetSearchFileBottomGroupRank(pSortItem2), bSortAscending);
	if (iBottomGroupResult != 0)
		return iBottomGroupResult;

	switch (LOWORD(lParamSort)) {
	case 0: //filename asc
		return CompareLocaleStringNoCase(item1->GetFileName(), item2->GetFileName());
	case 1: //size asc
		return CompareUnsigned(item1->GetFileSize(), item2->GetFileSize());
	case 2: //sources asc
		return CompareUnsigned(item1->GetSourceCount(), item2->GetSourceCount());
	case 3: // complete sources asc
		if (item1->GetSourceCount() == 0 || item2->GetSourceCount() == 0 || item1->IsKademlia() || item2->IsKademlia())
			return 0; // should never happen, just a sanity check
		return CompareUnsigned((item1->GetCompleteSourceCount() * 100) / item1->GetSourceCount(), (item2->GetCompleteSourceCount() * 100) / item2->GetSourceCount());
	case 4: //type asc
		{
			int iResult = item1->GetFileTypeDisplayStr().Compare(item2->GetFileTypeDisplayStr());
			if (iResult)
				return iResult;
			// the types are equal, sub-sort by extension
			LPCTSTR pszExt1 = ::PathFindExtension(item1->GetFileName());
			LPCTSTR pszExt2 = ::PathFindExtension(item2->GetFileName());
			if (!*pszExt1 ^ !*pszExt2)
				return *pszExt1 ? -1 : 1;
			return  *pszExt1 ? _tcsicmp(pszExt1, pszExt2) : 0;
		}
	case 5: //file hash asc
		return memcmp(item1->GetFileHash(), item2->GetFileHash(), 16);
	case 6:
		return CompareOptLocaleStringNoCaseUndefinedAtBottom(item1->GetStrTagValue(FT_MEDIA_ARTIST), item2->GetStrTagValue(FT_MEDIA_ARTIST), bSortAscending);
	case 7:
		return CompareOptLocaleStringNoCaseUndefinedAtBottom(item1->GetStrTagValue(FT_MEDIA_ALBUM), item2->GetStrTagValue(FT_MEDIA_ALBUM), bSortAscending);
	case 8:
		return CompareOptLocaleStringNoCaseUndefinedAtBottom(item1->GetStrTagValue(FT_MEDIA_TITLE), item2->GetStrTagValue(FT_MEDIA_TITLE), bSortAscending);
	case 9:
		return CompareUnsignedUndefinedAtBottom(item1->GetIntTagValue(FT_MEDIA_LENGTH), item2->GetIntTagValue(FT_MEDIA_LENGTH), bSortAscending);
	case 10:
		return CompareUnsignedUndefinedAtBottom(item1->GetIntTagValue(FT_MEDIA_BITRATE), item2->GetIntTagValue(FT_MEDIA_BITRATE), bSortAscending);
	case 11:
		return CompareOptLocaleStringNoCaseUndefinedAtBottom(GetCodecDisplayName(item1->GetStrTagValue(FT_MEDIA_CODEC)), GetCodecDisplayName(item2->GetStrTagValue(FT_MEDIA_CODEC)), bSortAscending);
	case 12: //path asc
		return CompareOptLocaleStringNoCaseUndefinedAtBottom(item1->GetDirectory(), item2->GetDirectory(), bSortAscending);
	case 13:
		if (thePrefs.GetGroupKnownAtTheBottom())
			return CompareSearchFixedGroupRank(GetSearchFileKnownTieRank(pSortItem1), GetSearchFileKnownTieRank(pSortItem2), bSortAscending);
		return CompareOptLocaleStringNoCase(GetKnownTypeStr(item1), GetKnownTypeStr(item2));
	case 14:
		return CompareAICHHash(item1->GetFileIdentifierC(), item2->GetFileIdentifierC(), bSortAscending);
	case 15:
		return CompareUnsigned(item1->GetSpamRating(), item2->GetSpamRating());
	}	
	return 0;
}

void CSearchListCtrl::OnContextMenu(CWnd*, CPoint point)
{
	int iSelected = 0;
	int iToDownload = 0;
	int iToPreview = 0;
	int iDownloadListMatches = 0;
	int iFilesToPreview = 0;
	int iFilesCanPauseOnPreview = 0;
	int iFilesDoPauseOnPreview = 0;
	int iFilesPreviewType = 0;
	int iFilesGetPreviewParts = 0;
	const CPartFile* pSingleDownloadFile = NULL;
	bool bContainsNotSpamFile = false;
	bool m_bContainsNotManualBlacklistedFile = false;
	bool m_bAllInDownloadList = true;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		const CSearchFile *pSearchFile = ResolveSearchFileByRowIndex(GetNextSelectedItem(pos));
		if (pSearchFile == NULL)
			continue;

		++iSelected;
		iToPreview += static_cast<int>(pSearchFile->IsPreviewPossible());
		iToDownload += static_cast<int>(theApp.downloadqueue == NULL || !theApp.downloadqueue->IsFileExisting(pSearchFile->GetFileHash(), false));
		const CPartFile* pDownloadFile = theApp.downloadqueue != NULL ? theApp.downloadqueue->GetFileByID(pSearchFile->GetFileHash()) : NULL;
		if (pDownloadFile == NULL)
			m_bAllInDownloadList = false;
		else {
			++iDownloadListMatches;
			iFilesPreviewType += static_cast<int>(pDownloadFile->IsPreviewableFileType());
			iFilesToPreview += static_cast<int>(pDownloadFile->IsReadyForPreview());
			iFilesCanPauseOnPreview += static_cast<int>(pDownloadFile->IsPreviewableFileType() && !pDownloadFile->IsReadyForPreview() && pDownloadFile->CanPauseFile());
			iFilesDoPauseOnPreview += static_cast<int>(pDownloadFile->IsPausingOnPreview());
			iFilesGetPreviewParts += static_cast<int>(pDownloadFile->GetPreviewPrio());
			if (iSelected == 1)
				pSingleDownloadFile = pDownloadFile;
		}
		if (!pSearchFile->GetManualBlacklisted())
			m_bContainsNotManualBlacklistedFile = true;
		if (!pSearchFile->IsConsideredSpam(false))
			bContainsNotSpamFile = true;
	}

	m_SearchFileMenu.EnableMenuItem(MP_RESUME, iToDownload > 0 ? MF_ENABLED : MF_GRAYED);
	if (thePrefs.IsExtControlsEnabled()) {
		m_SearchFileMenu.EnableMenuItem(MP_RESUMEPAUSED, iToDownload > 0 ? MF_ENABLED : MF_GRAYED);
		m_SearchFileMenu.EnableMenuItem(MP_DETAIL, iSelected == 1 ? MF_ENABLED : MF_GRAYED);
	}

	m_SearchFileMenu.EnableMenuItem(MP_BYPASSDOWNLOADVALIDATOR, iSelected > 0 && iToDownload > 0 ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_BYPASSDOWNLOADVALIDATORPAUSED, iSelected > 0 && iToDownload > 0 ? MF_ENABLED : MF_GRAYED);

	m_SearchFileMenu.EnableMenuItem(MP_CANCEL, iSelected > 0 && m_bAllInDownloadList ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_CANCEL_FORGET, iSelected > 0 && m_bAllInDownloadList ? MF_ENABLED : MF_GRAYED);
	const bool bEnableDownloadOnlyMenu = (iSelected > 0 && m_bAllInDownloadList && iDownloadListMatches == iSelected);
	RebuildPreviewMenu(m_PreviewMenu, (bEnableDownloadOnlyMenu && iSelected == 1) ? pSingleDownloadFile : NULL, bEnableDownloadOnlyMenu && iSelected == 1 && iFilesToPreview == 1, bEnableDownloadOnlyMenu && iFilesCanPauseOnPreview > 0, bEnableDownloadOnlyMenu && iSelected > 0 && iFilesDoPauseOnPreview == iSelected, bEnableDownloadOnlyMenu && iSelected == 1 && iFilesPreviewType == 1 && iFilesToPreview == 0 && iDownloadListMatches == 1, bEnableDownloadOnlyMenu && iSelected == 1 && iFilesGetPreviewParts == 1);
	m_SearchFileMenu.EnableMenuItem((UINT)m_PreviewMenu.m_hMenu, bEnableDownloadOnlyMenu && m_PreviewMenu.HasEnabledItems() ? MF_ENABLED : MF_GRAYED);

	m_SearchFileMenu.EnableMenuItem(MP_CMT, iSelected > 0 ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_CUT, iSelected > 0 ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_GETED2KLINK, iSelected > 0 ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_GETHTMLED2KLINK, iSelected > 0 ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_REMOVESELECTED, iSelected > 0 ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_REMOVE, theApp.emuledlg->searchwnd->CanDeleteSearches() ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_REMOVEALL, theApp.emuledlg->searchwnd->CanDeleteSearches() ? MF_ENABLED : MF_GRAYED);
	m_SearchFileMenu.EnableMenuItem(MP_SEARCHRELATED, iSelected > 0 && theApp.emuledlg->searchwnd->CanSearchRelatedFiles() ? MF_ENABLED : MF_GRAYED);
	UINT uInsertedMenuItem = 0;
	if (iToPreview == 1 && !(iSelected == 1 && m_bAllInDownloadList)) {
		if (m_SearchFileMenu.InsertMenu(MP_FIND, MF_STRING | MF_ENABLED, MP_PREVIEW, GetResString(_T("DL_PREVIEW")), _T("PREVIEW")))
			uInsertedMenuItem = MP_PREVIEW;
	}
	m_SearchFileMenu.EnableMenuItem(MP_FIND, GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED);

	UINT uInsertedMenuItem3 = 0;
	if (thePrefs.GetBlacklistManual() && m_SearchFileMenu.InsertMenu(MP_REMOVESELECTED, MF_STRING | MF_ENABLED, MP_MARKASBLACKLISTED, (m_bContainsNotManualBlacklistedFile || iSelected == 0) ? GetResString(_T("MARK_AS_BLACKLISTED")) : GetResString(_T("MARK_AS_NOT_BLACKLISTED")), _T("SPAM_PURPLE"))) {
		uInsertedMenuItem3 = MP_MARKASBLACKLISTED;
		m_SearchFileMenu.EnableMenuItem(MP_MARKASBLACKLISTED, iSelected > 0 ? MF_ENABLED : MF_GRAYED);
	}

	UINT uInsertedMenuItem2 = 0;
	if (thePrefs.IsSearchSpamFilterEnabled() && m_SearchFileMenu.InsertMenu(MP_REMOVESELECTED, MF_STRING | MF_ENABLED, MP_MARKASSPAM, (bContainsNotSpamFile || iSelected == 0) ? GetResString(_T("MARKSPAM")) : GetResString(_T("MARKNOTSPAM")), _T("SPAM"))) {
		uInsertedMenuItem2 = MP_MARKASSPAM;
		m_SearchFileMenu.EnableMenuItem(MP_MARKASSPAM, iSelected > 0 ? MF_ENABLED : MF_GRAYED);
	}

	CMenuXP WebMenu;
	WebMenu.CreateMenu();
	int iWebMenuEntries = theWebServices.GetFileMenuEntries(&WebMenu);
	UINT flag2 = (iWebMenuEntries == 0 || iSelected == 0) ? MF_GRAYED : MF_STRING;
	m_SearchFileMenu.AppendMenu(MF_POPUP | flag2, (UINT_PTR)WebMenu.m_hMenu, GetResString(_T("WEBSERVICES")), _T("WEB"));

	if (iToDownload > 0)
		m_SearchFileMenu.SetDefaultItem((!thePrefs.AddNewFilesPaused() || !thePrefs.IsExtControlsEnabled()) ? MP_RESUME : MP_RESUMEPAUSED);
	else
		m_SearchFileMenu.SetDefaultItem(UINT_MAX);

	GetPopupMenuPos(*this, point);
	m_SearchFileMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
	if (uInsertedMenuItem)
		VERIFY(m_SearchFileMenu.RemoveMenu(uInsertedMenuItem, MF_BYCOMMAND));
	if (uInsertedMenuItem2)
		VERIFY(m_SearchFileMenu.RemoveMenu(uInsertedMenuItem2, MF_BYCOMMAND));
	if (uInsertedMenuItem3)
		VERIFY(m_SearchFileMenu.RemoveMenu(uInsertedMenuItem3, MF_BYCOMMAND));
	m_SearchFileMenu.RemoveMenu(m_SearchFileMenu.GetMenuItemCount() - 1, MF_BYPOSITION);
	VERIFY(WebMenu.DestroyMenu());
}

BOOL CSearchListCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	wParam = LOWORD(wParam);

	if (wParam == MP_FIND) {
		OnFindStart();
		return TRUE;
	}

	CTypedPtrList<CPtrList, CSearchFile*> selectedList;
	CTypedPtrList<CPtrList, CPartFile*> selectedDownloadList;
	CPartFile* pSingleDownloadFile = NULL;
	CollectSelectedSearchFiles(selectedList);
	for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
		CSearchFile* pSearchFile = selectedList.GetNext(pos);
		CPartFile* pDownloadFile = pSearchFile != NULL ? theApp.downloadqueue->GetFileByID(pSearchFile->GetFileHash()) : NULL;
		if (pDownloadFile != NULL) {
			selectedDownloadList.AddTail(pDownloadFile);
			if (selectedDownloadList.GetCount() == 1)
				pSingleDownloadFile = pDownloadFile;
		}
	}

	if (!selectedList.IsEmpty()) {
		CSearchFile *file = selectedList.GetHead();
		bool m_bAddToCanceledMet = true;

		switch (wParam) {
		case MP_CUT:
			{
				CString m_strFileNames;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (file) {
						if (!m_strFileNames.IsEmpty())
							m_strFileNames += _T("\r\n");
						m_strFileNames += file->GetFileName();
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
				CString clpbrd;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (file) {
						if (!clpbrd.IsEmpty())
							clpbrd += _T("\r\n");
						clpbrd += file->GetED2kLink();
					}
				}

				if (!clpbrd.IsEmpty()) {
					theApp.CopyTextToClipboard(clpbrd);
					theApp.emuledlg->statusbar->SetText(GetResString(_T("ED2K_LINK_COPIED_TO_CLIPBOARD")), SBarLog, 0);
				}
			}
			return TRUE;
		case MP_GETHTMLED2KLINK:
			{
				CString clpbrd;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (file) {
						if (!clpbrd.IsEmpty())
							clpbrd += _T("<br>\r\n");
						clpbrd += file->GetED2kLink(false, true);
					}
				}

				if (!clpbrd.IsEmpty()) {
					theApp.CopyTextToClipboard(clpbrd);
					theApp.emuledlg->statusbar->SetText(GetResString(_T("ED2K_LINK_COPIED_TO_CLIPBOARD")), SBarLog, 0);
				}
			}
			return TRUE;
		case MP_RESUME:
			if (thePrefs.IsExtControlsEnabled())
				theApp.emuledlg->searchwnd->DownloadSelected(false);
			else
				theApp.emuledlg->searchwnd->DownloadSelected();
			return TRUE;
		case MP_RESUMEPAUSED:
			theApp.emuledlg->searchwnd->DownloadSelected(true);
			return TRUE;
		case MP_BYPASSDOWNLOADVALIDATOR:
			theApp.emuledlg->searchwnd->DownloadSelected(false, true);
			return TRUE;
		case MP_BYPASSDOWNLOADVALIDATORPAUSED:
			theApp.emuledlg->searchwnd->DownloadSelected(true, true);
			return TRUE;
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
						bool bRemovedItems = false;
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
									if (delsucc)
										theApp.sharedfiles->RemoveFile(partfile, true);
									else {
										CString strError;
										strError.Format(GetResString(_T("ERR_DELFILE")) + _T("\r\n\r\n%s"), (LPCTSTR)partfile->GetFilePath(), (LPCTSTR)GetErrorMessage(::GetLastError()));
										CDarkMode::MessageBox(strError);
									}

									theApp.emuledlg->transferwnd->GetDownloadList()->RemoveFile(partfile);
									theApp.searchlist->SetSearchItemKnownType(file);
									UpdateSearch(file);
								}
								break;
							default:
								if (partfile->GetCategory())
									theApp.downloadqueue->StartNextFileIfPrefs(partfile->GetCategory());
							case PS_PAUSED:
								partfile->DeletePartFile(m_bAddToCanceledMet);
								theApp.searchlist->SetSearchItemKnownType(file);
								UpdateSearch(file);
							}
						}
						if (bRemovedItems) {
							AutoSelectItem();
							theApp.emuledlg->transferwnd->UpdateCatTabTitles();
						}
					}
				}
			}
			return TRUE;
		case IDA_ENTER:
			theApp.emuledlg->searchwnd->DownloadSelected();
			return TRUE;
		case MP_REMOVESELECTED:
		case MPG_DELETE:
			StartChunkedRemoveSelectedSearchResults(selectedList);
			return TRUE;
		case MP_DETAIL:
		case MPG_ALTENTER:
		case MP_CMT:
			{
				CSearchResultFileDetailSheet sheet(selectedList, (wParam == MP_CMT ? IDD_COMMENTLST : 0), this);
				sheet.DoModal();
			}
			return TRUE;
		case MP_TRY_TO_GET_PREVIEW_PARTS:
			if (selectedDownloadList.GetCount() == 1 && pSingleDownloadFile != NULL)
				pSingleDownloadFile->SetPreviewPrio(!pSingleDownloadFile->GetPreviewPrio());
			return TRUE;
		case MP_PREVIEW:
			if (selectedDownloadList.GetCount() == 1 && pSingleDownloadFile != NULL) {
				pSingleDownloadFile->PreviewFile();
				return TRUE;
			}
			if (file) {
				if (file->GetPreviews().GetSize() > 0) {
					// already have previews
					(new PreviewDlg())->SetFile(file);
				} else {
					CUpDownClient *newclient = new CUpDownClient(NULL, file->GetClientPort(), file->GetClientID(), file->GetClientServerIP(), file->GetClientServerPort(), true); // IPv6-TODO: Check this
					if (!theApp.clientlist->AttachToAlreadyKnown(&newclient, NULL))
						theApp.clientlist->AddClient(newclient);

					newclient->SendPreviewRequest(*file);
					// add to res - later
						AddLogLine(true, GetResString(_T("PREVIEW_REQUESTED_WAIT")));
				}
			}
			return TRUE;
		case MP_PREVIEW1:
		case MP_PREVIEW2:
		case MP_PREVIEW3:
		case MP_PREVIEW4:
		case MP_PREVIEW5:
		case MP_PREVIEW6:
		case MP_PREVIEW7:
		case MP_PREVIEW8:
		case MP_PREVIEW9:
		case MP_PREVIEW10:
			if (selectedDownloadList.GetCount() == 1 && pSingleDownloadFile != NULL)
				pSingleDownloadFile->PreviewFile((UINT)wParam - MP_PREVIEW1);
			return TRUE;
		case MP_PAUSEONPREVIEW:
			{
				bool bAllPausedOnPreview = true;
				for (POSITION pos = selectedDownloadList.GetHeadPosition(); pos != NULL && bAllPausedOnPreview;)
					bAllPausedOnPreview = selectedDownloadList.GetNext(pos)->IsPausingOnPreview();
				while (!selectedDownloadList.IsEmpty()) {
					CPartFile* pPartFile = selectedDownloadList.RemoveHead();
					if (pPartFile->IsPreviewableFileType() && !pPartFile->IsReadyForPreview())
						pPartFile->SetPauseOnPreview(!bAllPausedOnPreview);
				}
			}
			return TRUE;
		case MP_SEARCHRELATED:
			// just a shortcut for the user typing into the search field "related::[filehash]"
			theApp.emuledlg->searchwnd->SearchRelatedFiles(selectedList);
			return TRUE;
		case MP_MARKASSPAM:
			{
				CWaitCursor curWait;
				SetRedraw(false);
				bool bContainsNotSpamFile = false;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (!file->IsConsideredSpam(false)) {
						bContainsNotSpamFile = true;
						break;
					}
				}
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (file->IsConsideredSpam(false)) {
						if (!bContainsNotSpamFile)
							theApp.searchlist->DoSpamRating(file, false, theApp.searchlist->EActionType::MarkAsNotSpam, true, 0);
					} else if (bContainsNotSpamFile)
						theApp.searchlist->DoSpamRating(file, false, theApp.searchlist->EActionType::MarkAsSpam, true, 0);
				}
				SetRedraw(true);
			}
			return TRUE;
		case MP_MARKASBLACKLISTED:
			{
				CWaitCursor curWait;
				SetRedraw(false);
				bool bContainsNotBlacklistedFile = false;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (!file->GetManualBlacklisted()) {
						bContainsNotBlacklistedFile = true;
						break;
					}
				}
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (file->GetManualBlacklisted()) {
						if (!bContainsNotBlacklistedFile)
							theApp.searchlist->DoSpamRating(file, false, theApp.searchlist->EActionType::MarkAsNotBlacklisted, true, 0);
					} else if (bContainsNotBlacklistedFile)
						theApp.searchlist->DoSpamRating(file, false, theApp.searchlist->EActionType::MarkAsBlacklisted, true, 0);
				}
				SetRedraw(true);
			}
			return TRUE;
		default:
			if (wParam >= MP_PREVIEW_APP_MIN && wParam <= MP_PREVIEW_APP_MAX) {
				if (selectedDownloadList.GetCount() == 1 && pSingleDownloadFile != NULL)
					thePreviewApps.RunApp(pSingleDownloadFile, (UINT)wParam);
				return TRUE;
			}
			if (wParam >= MP_WEBURL && wParam <= MP_WEBURL + 256) {
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					file = selectedList.GetNext(pos);
					if (file) 
						theWebServices.RunURL(file, (UINT)wParam);
				}
				return TRUE;
			}
		}
	}
	switch (wParam) {
	case MP_REMOVEALL:
		{
			CWaitCursor curWait;
			theApp.emuledlg->searchwnd->DeleteAllSearches();
		}
		break;
	case MP_REMOVE:
		{
			CWaitCursor curWait;
			theApp.emuledlg->searchwnd->DeleteSearch(m_nResultsID);
		}
	}

	return FALSE;
}

void CSearchListCtrl::OnLvnDeleteAllItems(LPNMHDR, LRESULT *pResult)
{
	// To suppress subsequent LVN_DELETEITEM notification messages, return TRUE.
	*pResult = TRUE;
}

void CSearchListCtrl::CreateMenus()
{
	if (m_SearchFileMenu.m_hMenu != NULL) {
		if (::IsMenu(m_SearchFileMenu.m_hMenu))
			VERIFY(m_SearchFileMenu.DestroyMenu());
		else
			m_SearchFileMenu.m_hMenu = NULL;
	}
	if (m_PreviewMenu.m_hMenu != NULL) {
		if (::IsMenu(m_PreviewMenu.m_hMenu))
			VERIFY(m_PreviewMenu.DestroyMenu());
		else
			m_PreviewMenu.m_hMenu = NULL;
	}

	m_SearchFileMenu.CreatePopupMenu();
	m_SearchFileMenu.AddMenuSidebar(GetResString(_T("FILE")));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_RESUME, GetResString(_T("DOWNLOAD")), _T("RESUME"));
	if (thePrefs.IsExtControlsEnabled()) {
		CString sResumePaused(GetResString(_T("DOWNLOAD")));
		sResumePaused.AppendFormat(_T(" (%s)"), (LPCTSTR)GetResString(_T("PAUSED")));
		m_SearchFileMenu.AppendMenu(MF_STRING, MP_RESUMEPAUSED, sResumePaused, _T("RESUME"));
	}

	if (thePrefs.GetDownloadValidator() > 0) {
		m_SearchFileMenu.AppendMenu(MF_STRING, MP_BYPASSDOWNLOADVALIDATOR, GetResString(_T("DOWNLOAD_BYPASS_DOWNLOAD_VALIDATOR")), _T("RESUME"));
		m_SearchFileMenu.AppendMenu(MF_STRING, MP_BYPASSDOWNLOADVALIDATORPAUSED, GetResString(_T("DOWNLOAD_BYPASS_DOWNLOAD_VALIDATOR_PAUSED")), _T("RESUME"));
	}

	if (thePrefs.IsExtControlsEnabled())
		m_SearchFileMenu.AppendMenu(MF_STRING, MP_DETAIL, GetResString(_T("SHOWDETAILS")), _T("FILEINFO"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_CMT, GetResString(_T("CMT_ADD")), _T("FILECOMMENTS"));
	m_SearchFileMenu.AppendMenu(MF_SEPARATOR);
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_CANCEL, GetResString(_T("CANCEL_DOWNLOAD")), _T("DELETE"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_CANCEL_FORGET, GetResString(_T("CANCEL_FORGET_DOWNLOAD")), _T("DELETE_FORGET"));
	m_PreviewMenu.CreateMenu();
	RebuildPreviewMenu(m_PreviewMenu, NULL, false, false, false, false, false);
	m_SearchFileMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_PreviewMenu.m_hMenu, GetResString(_T("PREVIEWWITH")), _T("PREVIEW"));
	m_SearchFileMenu.AppendMenu(MF_SEPARATOR);
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_CUT, GetResString(_T("COPY_FILE_NAMES")), _T("FILERENAME"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_GETED2KLINK, GetResString(_T("DL_LINK1")), _T("ED2KLINK"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_GETHTMLED2KLINK, GetResString(_T("DL_LINK2")), _T("ED2KLINK"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_REMOVESELECTED, GetResString(_T("REMOVESELECTED")), _T("DELETESELECTED"));
	m_SearchFileMenu.AppendMenu(MF_SEPARATOR);
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_REMOVE, GetResString(_T("REMOVESEARCHSTRING")), _T("DELETE"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_REMOVEALL, GetResString(_T("REMOVEALLSEARCH")), _T("CLEARCOMPLETE"));
	m_SearchFileMenu.AppendMenu(MF_SEPARATOR);
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_FIND, GetResString(_T("FIND")), _T("SEARCH"));
	m_SearchFileMenu.AppendMenu(MF_STRING, MP_SEARCHRELATED, GetResString(_T("SEARCHRELATED")), _T("KADFILESEARCH"));
}

bool CSearchListCtrl::ShouldShowPersistentInfoTip(const SPersistentInfoTipContext& context)
{
	if (!CMuleListCtrl::ShouldShowPersistentInfoTip(context))
		return false;

	bool bShowInfoTip = (GetSelectedCount() > 1 || GetKeyState(VK_CONTROL) < 0);
	if (bShowInfoTip && GetSelectedCount() > 1) {
		bool bInfoTipItemIsPartOfMultiSelection = false;
		for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
			if (GetNextSelectedItem(pos) == context.iItem) {
				bInfoTipItemIsPartOfMultiSelection = true;
				break;
			}
		}
		if (!bInfoTipItemIsPartOfMultiSelection)
			bShowInfoTip = false;
	}

	return bShowInfoTip;
}

bool CSearchListCtrl::GetPersistentInfoTipText(const SPersistentInfoTipContext& context, CString& strText)
{
	if (GetSelectedCount() <= 1)
		return BuildSearchInfoTipText(context.iItem, strText);

	int iSelected = 0;
	uint64 uTotalSize = 0;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		const CSearchFile *pSearchFile = ResolveSearchFileByRowIndex(GetNextSelectedItem(pos));
		if (pSearchFile != NULL) {
			++iSelected;
			uTotalSize += (uint64)pSearchFile->GetFileSize();
		}
	}

	if (iSelected <= 0)
		return false;

	strText.Format(_T("%s: %i\r\n%s: %s%c"), (LPCTSTR)GetResString(_T("FILES")), iSelected, (LPCTSTR)GetResString(_T("DL_SIZE")), (LPCTSTR)FormatFileSize(static_cast<ULONGLONG>(uTotalSize)), TOOLTIP_AUTOFORMAT_SUFFIX_CH);
	return true;
}


void CSearchListCtrl::OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult)
{
	CMuleListCtrl::OnLvnGetInfoTip(pNMHDR, pResult);
}


// virtual-list compliant expand / collapse
void CSearchListCtrl::ExpandCollapseItem(int iItem, int iAction)
{
	if (iItem < 0 || iItem >= static_cast<int>(m_ListedItemsVector.size()))
		return;

	CSearchFile* pSel = m_ListedItemsVector[iItem];
	if (pSel == NULL)
		return;

	CSearchFile* pParent = pSel->GetListParent() ? pSel->GetListParent() : pSel;
	if (!pParent)
		return;

	// Expand
	if (!pParent->IsListExpanded()) {
		if (iAction == COLLAPSE_ONLY || pParent->GetListChildCount() < 2)
			return;

		const SearchList* pList = theApp.searchlist->GetSearchListForID(pParent->GetSearchID());
		if (!pList)
			return;

		SaveListState(m_nResultsID, kSearchListViewState); // Save selections and scroll state
		SetRedraw(false); // Suspend painting

		int insertPos = iItem + 1;
		for (POSITION pos = pList->GetHeadPosition(); pos != NULL;) {
			CSearchFile* pChild = pList->GetNext(pos);
			if (pChild != NULL && pChild->GetListParent() == pParent) {
				m_ListedItemsVector.insert(m_ListedItemsVector.begin() + insertPos, pChild);
				++insertPos;
			}
		}

		pParent->SetListExpanded(true); // Mark parent as expanded
		RebuildListedItemsMap(); // Rebuild the map to update the item indices
		UpdateSearchListItemCount(*this, m_ListedItemsVector.size()); // Update the item count in the list control
		RestoreListState(m_nResultsID, kSearchListViewState, false); // Restore selections and scroll state
		SetRedraw(true); // Resume painting
		Invalidate(FALSE); //Force redraw
		return;
	}

	// Collapse
	if (iAction == EXPAND_ONLY)
		return;

	int iParentIndex = iItem;
	if (!m_ListedItemsMap.Lookup(pParent, iParentIndex))
		return;
	if (GetItemState(iParentIndex, LVIS_SELECTED | LVIS_FOCUSED) != (LVIS_SELECTED | LVIS_FOCUSED)) {
		SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
		SetItemState(iParentIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		SetSelectionMark(iParentIndex);
	}
	SaveListState(m_nResultsID, kSearchListViewState); // Save selections and scroll state
	SetRedraw(false); // Suspend painting
	HideSources(pParent); // internal collapse helper
	RestoreListState(m_nResultsID, kSearchListViewState, false); // Restore selections and scroll state
	SetRedraw(true); // Resume painting
	Update(iParentIndex); // Redraw parent line
	Invalidate(FALSE); // Force redraw
}

// remove child rows in virtual list
void CSearchListCtrl::HideSources(CSearchFile* pParent)
{
	if (!pParent || pParent->GetSearchID() != m_nResultsID)
		return;

	auto it = std::remove_if(m_ListedItemsVector.begin(),	m_ListedItemsVector.end(), [pParent](CSearchFile* f) { return f->GetListParent() == pParent; });
	if (it != m_ListedItemsVector.end()) {
		m_ListedItemsVector.erase(it, m_ListedItemsVector.end());
		pParent->SetListExpanded(false);
		RebuildListedItemsMap();
		UpdateSearchListItemCount(*this, m_ListedItemsVector.size());
		Invalidate(FALSE);
	}
}

void CSearchListCtrl::OnNmClick(LPNMHDR pNMHDR, LRESULT*)
{
	POINT pt;
	::GetCursorPos(&pt);
	ScreenToClient(&pt);
	if (pt.x < TREE_WIDTH) {
		LPNMITEMACTIVATE pNMIA = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
		ExpandCollapseItem(pNMIA->iItem, EXPAND_COLLAPSE);
	}
}

void CSearchListCtrl::OnNmDblClk(LPNMHDR, LRESULT*)
{
	POINT point;
	::GetCursorPos(&point);
	ScreenToClient(&point);
	if (point.x > TREE_WIDTH) {
		if (GetKeyState(VK_MENU) & 0x8000) {
			int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
			if (iSel >= 0) {
				CSearchFile* file = ResolveSearchFileByRowIndex(iSel);
				if (file) {
					CTypedPtrList<CPtrList, CSearchFile*> aFiles;
					aFiles.AddTail(file);
					CSearchResultFileDetailSheet sheet(aFiles, 0, this);
					sheet.DoModal();
				}
			}
		} else
			theApp.emuledlg->searchwnd->DownloadSelected();
	}
}

void CSearchListCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	int index = static_cast<int>(lpDrawItemStruct->itemID);
	CSearchFile* content = ResolveSearchFileByRowIndex(index);
	if (content == NULL) {
		FillSearchFallbackOwnerDataRow(*this, lpDrawItemStruct);
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

	COLORREF clrBk = (lpDrawItemStruct->itemState & ODS_SELECTED) ? GetCustomSysColor(COLOR_HIGHLIGHT) : GetCustomSysColor(COLOR_WINDOW);
	dc.FillSolidRect(rcPaint, clrBk);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(clrBk);

	RECT rcClient;
	GetClientRect(&rcClient);
	if (!g_bLowColorDesktop || (lpDrawItemStruct->itemState & ODS_SELECTED) == 0)
		dc.SetTextColor(GetSearchItemColor(content));

	bool isChild = (content->GetListParent() != NULL);
	bool notLast = (lpDrawItemStruct->itemID + 1 != (UINT)m_ListedItemsVector.size());
	bool notFirst = (lpDrawItemStruct->itemID != 0);
	int tree_start = 0;
	int tree_end = 0;

	const CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	int iCount = pHeaderCtrl->GetItemCount();
	LONG itemLeft = rcItem.left;
	rcItem.right = rcItem.left - sm_iLabelOffset;
	rcItem.left += sm_iIconOffset;

	LONG iIndent = isChild ? 8 : 0;
	LONG iIconY = max((rcItem.Height() - theApp.GetSmallSytemIconSize().cy - 1) / 2, 0);
	const POINT point = { itemLeft + iIndent + TREE_WIDTH + 18, rcItem.top + iIconY };
	int iImage;
	if (content->GetKnownType() == CSearchFile::Shared)
		iImage = 14;
	else if (content->GetKnownType() == CSearchFile::Downloaded)
		iImage = 13;
	else if (content->GetKnownType() == CSearchFile::Downloading)
		iImage = 12;
	else if (content->GetKnownType() == CSearchFile::Cancelled)
		iImage = 15;
	else if (thePrefs.GetBlacklistManual() && content->GetManualBlacklisted())
		iImage = 11;
	else if (thePrefs.GetBlacklistAutomatic() && content->GetAutomaticBlacklisted())
		iImage = 10;
	else if (thePrefs.IsSearchSpamFilterEnabled() && content->IsConsideredSpam())
		iImage = 8;
	else if (thePrefs.ShowRatingIndicator() && (content->HasComment() || content->HasRating() || content->IsKadCommentSearchRunning()))
		iImage = content->UserRating(true) + 1;
	else
		iImage = 0;
	if (iImage)
		SafeImageListDraw(&m_ImageList, dc, iImage, point, ILD_NORMAL);

	iImage = theApp.GetFileTypeSystemImageIdx(content->GetFileName());
	if (iImage >= 0 && theApp.GetSystemImageList() != NULL)
		::ImageList_Draw(theApp.GetSystemImageList(), iImage, dc, point.x - 18, point.y, ILD_TRANSPARENT);

	for (int iCurrent = 0; iCurrent < iCount; ++iCurrent) {
		int iColumn = pHeaderCtrl->OrderToIndex(iCurrent);
		if (IsColumnHidden(iColumn))
			continue;

		UINT uDrawTextAlignment;
		int iColumnWidth = GetColumnWidth(iColumn, uDrawTextAlignment);
		rcItem.left = itemLeft;
		rcItem.right = itemLeft + iColumnWidth - sm_iLabelOffset;
		switch (iColumn) {
		case 0:
			tree_start = rcItem.left + 1;
			rcItem.left += min(8, iColumnWidth);
			tree_end = rcItem.left;
		default:
			rcItem.left += sm_iLabelOffset;
			if (rcItem.left < rcItem.right && HaveIntersection(rcClient, rcItem))
				if (isChild)
					DrawSourceChild(dc, iColumn, &rcItem, uDrawTextAlignment, content);
				else
					DrawSourceParent(dc, iColumn, &rcItem, uDrawTextAlignment, content);
		}
		itemLeft += iColumnWidth;
	}

	DrawFocusRect(dc, &lpDrawItemStruct->rcItem, (lpDrawItemStruct->itemState & ODS_FOCUS) != 0, bCtrlFocused, (lpDrawItemStruct->itemState & ODS_SELECTED) != 0);

	if (tree_start < tree_end) {
		RECT tree_rect = { tree_start, lpDrawItemStruct->rcItem.top, tree_end, lpDrawItemStruct->rcItem.bottom };
		dc.SetBoundsRect(&tree_rect, DCB_DISABLE);

		CSearchFile* pNext = notLast ? ResolveSearchFileByRowIndex(static_cast<int>(lpDrawItemStruct->itemID + 1)) : NULL;
		bool hasNext = pNext != NULL && pNext->GetListParent() != NULL;
		bool isOpenRoot = hasNext && !isChild;

		int treeCenter = tree_start + 4;
		int middle = (rcItem.top + rcItem.bottom + 1) / 2;

		COLORREF crLine = (!g_bLowColorDesktop || (lpDrawItemStruct->itemState & ODS_SELECTED) == 0) ? RGB(128, 128, 128) : m_crHighlightText;
		CPen pn;
		pn.CreatePen(PS_SOLID, 1, crLine);
		CPen *oldpn = dc.SelectObject(&pn);

		if (isChild) {
			dc.MoveTo(tree_end + 10, middle);
			dc.LineTo(tree_start + 4, middle);
			if (hasNext) {
				dc.MoveTo(treeCenter, middle);
				dc.LineTo(treeCenter, rcItem.bottom + 1);
			}
		} else if (isOpenRoot || content->GetListChildCount() > 1) {
			const RECT circle_rec = { treeCenter - 4, middle - 5, treeCenter + 5, middle + 4 };
			CBrush brush(crLine);
			dc.FrameRect(&circle_rec, &brush);
			CPen penBlack;
			penBlack.CreatePen(PS_SOLID, 1, (!g_bLowColorDesktop || (lpDrawItemStruct->itemState & ODS_SELECTED) == 0) ? m_crWindowText : m_crHighlightText);
			CPen *pOldPen2 = dc.SelectObject(&penBlack);
			dc.MoveTo(treeCenter - 2, middle - 1);
			dc.LineTo(treeCenter + 3, middle - 1);
			if (!content->IsListExpanded()) {
				dc.MoveTo(treeCenter, middle - 3);
				dc.LineTo(treeCenter, middle + 2);
			}
			dc.SelectObject(pOldPen2);
			if (hasNext) {
				dc.MoveTo(treeCenter, middle + 4);
				dc.LineTo(treeCenter, rcItem.bottom + 1);
			}
		}

		if (notFirst && isChild) {
			dc.MoveTo(treeCenter, middle);
			dc.LineTo(treeCenter, rcItem.top - 1);
		}

		dc.SelectObject(oldpn);
		pn.DeleteObject();
	}
}


COLORREF CSearchListCtrl::GetSearchItemColor(const CSearchFile* src) const
{
	if (theApp.searchlist == NULL)
		return { RGB(0, 0, 0) };
	CSingleLock searchModelLock(theApp.searchlist->GetSearchModelLock(), TRUE);
	switch (src->GetKnownType()) {
	case CSearchFile::Shared:
		return { m_crSearchResultSharing};
	case CSearchFile::Downloading:
		{
			const CKnownFile* pFile = theApp.downloadqueue->GetFileByID(src->GetFileHash());
			if (pFile && pFile->IsPartFile() && static_cast<const CPartFile*>(pFile)->GetStatus() == PS_PAUSED)
				return { m_crSearchResultDownloadStopped};
			return { m_crSearchResultDownloading };
		}
	case CSearchFile::Downloaded:
		return { m_crSearchResultKnown };
	case CSearchFile::Cancelled:
		return { m_crSearchResultCancelled };
	}

	// Spam check
	if (thePrefs.GetBlacklistManual() && src->GetManualBlacklisted())
		return { GetCustomSysColor(COLOR_MAN_BLACKLIST) }; // Purple
	else if (thePrefs.GetBlacklistAutomatic() && src->GetAutomaticBlacklisted())
		return { GetCustomSysColor(COLOR_AUTO_BLACKLIST) }; // Pink
	else if (thePrefs.IsSearchSpamFilterEnabled() && src->IsConsideredSpam(false))
		return { GetCustomSysColor(COLOR_SPAM) }; // Red

	// unknown file -> show shades of a color
	uint32 srccnt = src->GetSourceCount();
	srccnt -= static_cast<uint32>(srccnt > 0);
	return { m_crShades[min(srccnt, AVBLYSHADECOUNT - 1)] };
}


void CSearchListCtrl::DrawSourceChild(CDC *dc, int nColumn, LPRECT lpRect, UINT uDrawTextAlignment, const CSearchFile *src)
{
	const CString sItem(GetItemDisplayText(src, nColumn));
	switch (nColumn) {
	case 0:
		lpRect->left += 8 + 8 + theApp.GetSmallSytemIconSize().cy;
		if ((thePrefs.ShowRatingIndicator() && (src->HasComment() || src->HasRating() || src->IsKadCommentSearchRunning()))
			|| ((thePrefs.IsSearchSpamFilterEnabled() || thePrefs.GetBlacklistAutomatic() || thePrefs.GetBlacklistManual()) && src->IsConsideredSpam())
			|| (src->GetKnownType() == CSearchFile::Shared || src->GetKnownType() == CSearchFile::Downloaded || src->GetKnownType() == CSearchFile::Downloading || src->GetKnownType() == CSearchFile::Cancelled))
			lpRect->left += 16;
	default:
		dc->DrawText(sItem, -1, lpRect, MLC_DT_TEXT | uDrawTextAlignment);
	case 4:
	case 5:
		break;
	}
}

void CSearchListCtrl::DrawSourceParent(CDC *dc, int nColumn, LPRECT lpRect, UINT uDrawTextAlignment, const CSearchFile *src)
{
	const CString sItem(GetItemDisplayText(src, nColumn));
	switch (nColumn) {
	case 0:
		lpRect->left += 8 + theApp.GetSmallSytemIconSize().cx;
		if ((thePrefs.ShowRatingIndicator() && (src->HasComment() || src->HasRating() || src->IsKadCommentSearchRunning()))
			|| ((thePrefs.IsSearchSpamFilterEnabled() || thePrefs.GetBlacklistAutomatic() || thePrefs.GetBlacklistManual()) && src->IsConsideredSpam())
			|| (src->GetKnownType() == CSearchFile::Shared || src->GetKnownType() == CSearchFile::Downloaded || src->GetKnownType() == CSearchFile::Downloading || src->GetKnownType() == CSearchFile::Cancelled))
			lpRect->left += 16;
	default:
		dc->DrawText(sItem, -1, lpRect, MLC_DT_TEXT | uDrawTextAlignment);
		break;
	case 3:
		{
			bool bComplete = IsComplete(src, src->GetSourceCount());
			COLORREF crOldTextColor = (bComplete ? 0 : dc->SetTextColor(RGB(255, 0, 0)));
			dc->DrawText(sItem, -1, lpRect, MLC_DT_TEXT | uDrawTextAlignment);
			if (!bComplete)
				dc->SetTextColor(crOldTextColor);
		}
	}
}


void CSearchListCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar == VK_F5) {
		theApp.searchlist->RecalculateSpamRatings(m_nResultsID, false, false, true);
		return;
	}


	CMuleListCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CSearchListCtrl::SetHighlightColors()
{
	// Default colors
	// --------------
	//	Blue:	User does not know that file; shades of blue are used to indicate availability of file
	//  Red:	User already has the file; it is currently downloading or it is currently shared
	//			-> 'Red' means: User can not add this file
	//	Green:	User 'knows' the file (it was already download once, but is currently not in share)
	COLORREF crSearchResultAvblyBase = GetCustomSysColor(COLOR_SHADEBASE);
	m_crSearchResultDownloading = GetCustomSysColor(COLOR_SEARCH_DOWNLOADING); // Olive green
	m_crSearchResultDownloadStopped = GetCustomSysColor(COLOR_SEARCH_STOPPED); // Olive green
	m_crSearchResultSharing = GetCustomSysColor(COLOR_SEARCH_SHARING); // Dark green
	m_crSearchResultKnown = GetCustomSysColor(COLOR_SEARCH_KNOWN); // Medium green
	m_crSearchResultCancelled = GetCustomSysColor(COLOR_SEARCH_CANCELED); // Orange

	theApp.LoadSkinColor(GetSkinKey() + _T("Fg_Downloading"), m_crSearchResultDownloading);
	if (!theApp.LoadSkinColor(_T("Fg_DownloadStopped"), m_crSearchResultDownloadStopped))
		m_crSearchResultDownloadStopped = m_crSearchResultDownloading;
	theApp.LoadSkinColor(GetSkinKey() + _T("Fg_Sharing"), m_crSearchResultSharing);
	theApp.LoadSkinColor(GetSkinKey() + _T("Fg_Known"), m_crSearchResultKnown);
	theApp.LoadSkinColor(GetSkinKey() + _T("Fg_AvblyBase"), crSearchResultAvblyBase);

	// precalculate sources shades
	COLORREF normFGC = GetTextColor();
	// precalculate sources shades
	COLORREF darkFGC = GetCustomSysColor(COLOR_WINDOWTEXT); // Dark mode foreground color


	float rdelta = (GetRValue(crSearchResultAvblyBase) - GetRValue(normFGC)) / (float)AVBLYSHADECOUNT;
	float gdelta = (GetGValue(crSearchResultAvblyBase) - GetGValue(normFGC)) / (float)AVBLYSHADECOUNT;
	float bdelta = (GetBValue(crSearchResultAvblyBase) - GetBValue(normFGC)) / (float)AVBLYSHADECOUNT;

	for (int shades = 0; shades < AVBLYSHADECOUNT; ++shades)
	{
		if (IsDarkModeEnabled()) {
			// Adjust shades for dark mode
			m_crShades[shades] = RGB(GetRValue(darkFGC) + (rdelta * shades),
				GetGValue(darkFGC) + (gdelta * shades),
				GetBValue(darkFGC) + (bdelta * shades));
		} else {
			m_crShades[shades] = RGB(GetRValue(normFGC) + (rdelta * shades),
				GetGValue(normFGC) + (gdelta * shades),
				GetBValue(normFGC) + (bdelta * shades));
		}
	}
}

void CSearchListCtrl::RefreshThemeColors()
{
	CMuleListCtrl::RefreshThemeColors();
	SetHighlightColors();
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

void CSearchListCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
}

void CSearchListCtrl::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CMuleListCtrl::OnShowWindow(bShow, nStatus);
	if (bShow && ::IsWindow(m_hWnd))
		RefreshThemeColors();
}

BOOL CSearchListCtrl::OnEraseBkgnd(CDC* pDC)
{
	return CMuleListCtrl::OnEraseBkgnd(pDC);
}

void CSearchListCtrl::OnLvnKeyDown(LPNMHDR pNMHDR, LRESULT *pResult)
{
	LPNMLVKEYDOWN pLVKeyDown = reinterpret_cast<LPNMLVKEYDOWN>(pNMHDR);

	bool bAltKey = GetKeyState(VK_MENU) < 0;
	int iAction;
	if (pLVKeyDown->wVKey == VK_ADD || (bAltKey && pLVKeyDown->wVKey == VK_RIGHT))
		iAction = EXPAND_ONLY;
	else if (pLVKeyDown->wVKey == VK_SUBTRACT || (bAltKey && pLVKeyDown->wVKey == VK_LEFT))
		iAction = COLLAPSE_ONLY;
	else
		iAction = EXPAND_COLLAPSE;
	if (iAction < EXPAND_COLLAPSE)
		ExpandCollapseItem(GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED), iAction);
	*pResult = 0;
}

CString CSearchListCtrl::GetItemDisplayText(const CSearchFile *src, int iSubItem) const
{
	CString sText;
	switch (iSubItem) {
	case 0: //file name
		sText = src->GetFileName();
		break;
	case 1: //file size
		if (src->GetListParent() == NULL
			|| (thePrefs.GetDebugSearchResultDetailLevel() >= 1 && src->GetFileSize() != src->GetListParent()->GetFileSize()))
		{
			sText = FormatFileSize(src->GetFileSize());
		}
		break;
	case 2: //avail
		if (src->GetListParent() == NULL) {
			sText.Format(_T("%u"), src->GetSourceCount());
			if (thePrefs.IsExtControlsEnabled()) {
				if (src->IsKademlia()) {
					uint32 nKnownPublisher = (src->GetKadPublishInfo() >> 16) & 0xffu;
					if (nKnownPublisher > 0)
						sText.AppendFormat(_T(" (%u)"), nKnownPublisher);
				} else {
					int iClients = src->GetClientsCount();
					if (iClients > 0)
						sText.AppendFormat(_T(" (%i)"), iClients);
				}
			}
#ifdef _DEBUG
			if (src->GetKadPublishInfo() == 0)
				sText += _T(" | -");
			else
				sText.AppendFormat(_T("%sNames:%u, Pubs:%u, Trust:%0.2f"), sText.IsEmpty() ? _T("") : _T(" | "), (src->GetKadPublishInfo() >> 24) & 0xffu, (src->GetKadPublishInfo() >> 16) & 0xffu, (src->GetKadPublishInfo() & 0xffffu) / 100.0f);
#endif
		} else
			sText.Format(_T("%u"), src->GetListChildCount());
		break;
	case 3: //complete sources
		if (src->GetListParent() == NULL
			|| (thePrefs.IsExtControlsEnabled() && thePrefs.GetDebugSearchResultDetailLevel() >= 1))
		{
			sText = GetCompleteSourcesDisplayString(src, src->GetSourceCount());
		}
		break;
	case 4: //file type
		if (src->GetListParent() == NULL)
			sText = src->GetFileTypeDisplayStr();
		break;
	case 5: //file hash
		if (src->GetListParent() == NULL)
			sText = md4str(src->GetFileHash());
		break;
	case 6:
		sText = src->GetStrTagValue(FT_MEDIA_ARTIST);
		break;
	case 7:
		sText = src->GetStrTagValue(FT_MEDIA_ALBUM);
		break;
	case 8:
		sText = src->GetStrTagValue(FT_MEDIA_TITLE);
		break;
	case 9:
		{
			uint32 nMediaLength = src->GetIntTagValue(FT_MEDIA_LENGTH);
			if (nMediaLength)
				sText = SecToTimeLength(nMediaLength);
		}
		break;
	case 10:
		{
			uint32 nBitrate = src->GetIntTagValue(FT_MEDIA_BITRATE);
			if (nBitrate)
				sText.Format(_T("%u %s"), nBitrate, (LPCTSTR)GetResString(_T("KBITSSEC")));
		}
		break;
	case 11:
		sText = GetCodecDisplayName(src->GetStrTagValue(FT_MEDIA_CODEC));
		break;
	case 12: // dir
		if (src->GetDirectory())
			sText = src->GetDirectory();
		break;
	case 13: //known
		{
			sText = GetKnownTypeStr(src);
#ifdef _DEBUG
			sText.AppendFormat(_T("%sSR: %u%%"), sText.IsEmpty() ? _T("") : _T(" "), src->GetSpamRating());
#endif
		}
		break;
	case 14: //AICH hash
		if (src->GetFileIdentifierC().HasAICHHash())
			sText = src->GetFileIdentifierC().GetAICHHash().GetString();
		break;
	case 15:
		sText.Format(_T("%u"), src->GetSpamRating());
	}
	return sText;
}

void CSearchListCtrl::OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult)
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
			_tcsncpy_s(rItem.pszText, rItem.cchTextMax, GetListedItemDisplayText(rItem.iItem, rItem.iSubItem), _TRUNCATE);
		}
	}
	*pResult = 0;
}

CString CSearchListCtrl::FormatFileSize(ULONGLONG ullFileSize) const
{
	if (m_eFileSizeFormat == fsizeKByte)
		// Always round up to next KiB (this is same as Windows Explorer is doing)
		return GetFormatedUInt64((ullFileSize + 1024 - 1) / 1024) + _T(' ') + GetResString(_T("KBYTES"));

	if (m_eFileSizeFormat == fsizeMByte) {
		double fFileSize = ullFileSize / (1024.0 * 1024.0);
		if (fFileSize < 0.01)
			fFileSize = 0.01;

		static NUMBERFMT nf;
		static TCHAR szDecimalSep[] = _T(".");
		static TCHAR szThousandSep[] = _T(",");
		if (nf.Grouping == 0) {
			nf.NumDigits = 2;
			nf.LeadingZero = 1;
			nf.Grouping = 3;
			// we are hardcoding the following two format chars by intention because the C-RTL also has the decimal sep hardcoded to '.'
			nf.lpDecimalSep = szDecimalSep;
			nf.lpThousandSep = szThousandSep;
			nf.NegativeOrder = 0;
		}
		CString sVal, strVal;
		sVal.Format(_T("%.2f"), fFileSize);
		int iResult = GetNumberFormat(LOCALE_SYSTEM_DEFAULT, 0, sVal, &nf, strVal.GetBuffer(80), 80);
		strVal.ReleaseBuffer();
		return (iResult ? strVal : sVal) + _T(' ') + GetResString(_T("MBYTES"));
	}

	return CastItoXBytes(ullFileSize);
}

void CSearchListCtrl::SetFileSizeFormat(EFileSizeFormat eFormat)
{
	m_eFileSizeFormat = eFormat;
	Invalidate(FALSE);
}

const bool CSearchListCtrl::IsFilteredOut(const CSearchFile *pSearchFile) const
{

	if (!pSearchFile)
		return true;

	if ((thePrefs.m_uCompleteCheckState == BST_CHECKED && !pSearchFile->GetCompleteSourceCount()) ||
		(thePrefs.m_uCompleteCheckState == BST_INDETERMINATE && pSearchFile->GetCompleteSourceCount()))
		return true;

	const CStringArray &rastrFilter = theApp.emuledlg->searchwnd->m_pwndResults->m_astrFilter;
	if (!rastrFilter.IsEmpty()) {
		// filtering is done by text only for all columns to keep it consistent and simple
		// for the user even if that doesn't allow complex filters
		// for example for a file size range - but this could be done at server search time already
		const CString &szFilterTarget(GetItemDisplayText(pSearchFile, theApp.emuledlg->searchwnd->m_pwndResults->GetFilterColumn()));

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

CString CSearchListCtrl::GetKnownTypeStr(const CSearchFile* src)
{
	LPCTSTR uid;
	switch (src->m_eKnown) {
	case CSearchFile::Shared:
		uid = _T("SHARED");
		break;
	case CSearchFile::Downloading:
		uid = _T("DOWNLOADING");
		break;
	case CSearchFile::Downloaded:
		uid = _T("DOWNLOADED");
		break;
	case CSearchFile::Cancelled:
		uid = _T("CANCELLED");
		break;
	default:
		if (thePrefs.GetBlacklistManual() && src->GetManualBlacklisted())
			uid = _T("MANUAL_BLACKLISTED");
		else if (thePrefs.GetBlacklistAutomatic() && src->GetAutomaticBlacklisted())
			uid = _T("AUTOMATIC_BLACKLISTED");
		else if (thePrefs.IsSearchSpamFilterEnabled() && src->IsConsideredSpam(false))
			uid = _T("SPAM");
		else
			uid = EMPTY;
	}
	if (uid)
		return GetResString(uid);
	else
		return NULL;
}
