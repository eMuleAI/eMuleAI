//This file is part of eMule AI
//Copyright (C)2002-2026 Merkur ( devs@emule-project.net / https://www.emule-project.net )
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
#include "SharedDirsTreeCtrl.h"
#include "preferences.h"
#include "otherfunctions.h"
#include "SharedFilesCtrl.h"
#include "Knownfile.h"
#include "MenuCmds.h"
#include "partfile.h"
#include "emuledlg.h"
#include "TransferDlg.h"
#include "SharedFileList.h"
#include "SharedFilesWnd.h"
#include "MuleStatusBarCtrl.h"
#include "eMuleAI/DarkMode.h"
#include "UserMsgs.h"

#include <functional>
#include <algorithm>
#include <vector>
#include <windows.h>

// Forward declarations for long-path helpers used below
static void EnumSubdirectoriesLongPath(const CString& dir, const std::function<void(const CString&)>& cb);
static bool IsAccessibleDirectoryLongPath(const CString& dir);

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//**********************************************************************************
// CDirectoryItem

CDirectoryItem::CDirectoryItem(const CString &strFullPath, HTREEITEM htItem, ESpecialDirectoryItems eItemType, int nCatFilter)
	: m_strFullPath(strFullPath)
	, m_htItem(htItem)
	, m_nCatFilter(nCatFilter)
	, m_eItemType(eItemType)
{
}

CDirectoryItem::~CDirectoryItem()
{
	while (!liSubDirectories.IsEmpty())
		delete liSubDirectories.RemoveHead();
}

// search tree for a given filter
HTREEITEM CDirectoryItem::FindItem(CDirectoryItem *pContentToFind) const
{
	if (pContentToFind == NULL) {
		ASSERT(0);
		return NULL;
	}

	if (pContentToFind->m_eItemType == m_eItemType && pContentToFind->m_strFullPath == m_strFullPath && pContentToFind->m_nCatFilter == m_nCatFilter)
		return m_htItem;

	for (POSITION pos = liSubDirectories.GetHeadPosition(); pos != NULL;) {
		HTREEITEM htResult = liSubDirectories.GetNext(pos)->FindItem(pContentToFind);
		if (htResult != NULL)
			return htResult;
	}
	return NULL;
}

//**********************************************************************************
// CSharedDirsTreeCtrl


IMPLEMENT_DYNAMIC(CSharedDirsTreeCtrl, CTreeCtrl)

BEGIN_MESSAGE_MAP(CSharedDirsTreeCtrl, CTreeCtrl)
	ON_WM_CONTEXTMENU()
	ON_WM_RBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_CANCELMODE()
	ON_WM_LBUTTONUP()
	ON_WM_SYSCOLORCHANGE()
	ON_NOTIFY_REFLECT(TVN_ITEMEXPANDING, OnTvnItemexpanding)
	ON_NOTIFY_REFLECT(TVN_GETDISPINFO, OnTvnGetdispinfo)
	ON_NOTIFY_REFLECT(TVN_BEGINDRAG, OnTvnBeginDrag)
END_MESSAGE_MAP()

CSharedDirsTreeCtrl::CSharedDirsTreeCtrl()
	: m_pRootDirectoryItem()
	, m_pRootUnsharedDirectries()
	, m_pDraggingItem()
	, m_pSharedFilesCtrl()
	, m_bFileSystemRootDirty()
	, m_bCreatingTree()
	, m_bUseIcons()
	, pHistory()
	, m_dwLastTreeStructCRC(0)
{
}

CSharedDirsTreeCtrl::~CSharedDirsTreeCtrl()
{
	delete m_pRootDirectoryItem;
	delete m_pRootUnsharedDirectries;
	delete pHistory;
}

void CSharedDirsTreeCtrl::Initialize(CSharedFilesCtrl *pSharedFilesCtrl)
{
	m_pSharedFilesCtrl = pSharedFilesCtrl;

	// Win98: Explicitly set to Unicode to receive Unicode notifications.
	SendMessage(CCM_SETUNICODEFORMAT, TRUE);

	m_bUseIcons = true;
	SetAllIcons();
	Localize();
}

void CSharedDirsTreeCtrl::OnSysColorChange()
{
	CTreeCtrl::OnSysColorChange();
	SetAllIcons();
	CreateMenus();
}

void CSharedDirsTreeCtrl::SetAllIcons()
{
	// This treeview control contains an image list which contains our own icons and a
	// couple of icons which are copied from the Windows System image list. To properly
	// support an update of the control and the image list, we need to 'replace' our own
	// images so that we are able to keep the already stored images from the Windows System
	// image list.

	//Should be terminated with a backslash for a directory image
	int nImage = theApp.GetFileTypeSystemImageIdx(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));

	CImageList *pCurImageList = GetImageList(TVSIL_NORMAL);
	if (pCurImageList != NULL && pCurImageList->GetImageCount() >= 18) {
		pCurImageList->Replace(0, CTempIconLoader(_T("AllFiles")));			// 0: All Directory
		pCurImageList->Replace(1, CTempIconLoader(_T("Incomplete")));		// 1: Temp Directory
		pCurImageList->Replace(2, CTempIconLoader(_T("Incoming")));			// 2: Incoming Directory
		pCurImageList->Replace(3, CTempIconLoader(_T("Category")));			// 3: Cats
		pCurImageList->Replace(4, CTempIconLoader(_T("HardDisk")));			// 4: All Dirs
		if (nImage > 0 && theApp.GetSystemImageList() != NULL) {			// 5: System Folder Icon
			HICON hIcon = ::ImageList_GetIcon(theApp.GetSystemImageList(), nImage, 0);
			pCurImageList->Replace(5, hIcon);
			::DestroyIcon(hIcon);
		} else
			pCurImageList->Replace(5, CTempIconLoader(_T("OpenFolder")));
			pCurImageList->Replace(6, CTempIconLoader(_T("FileHistory"))); //6
			pCurImageList->Replace(7, CTempIconLoader(_T("SearchFileType_Audio"))); //7
			pCurImageList->Replace(8, CTempIconLoader(_T("SearchFileType_Video"))); //8
			pCurImageList->Replace(9, CTempIconLoader(_T("SearchFileType_Picture"))); //9
			pCurImageList->Replace(10, CTempIconLoader(_T("SearchFileType_Program"))); //10
			pCurImageList->Replace(11, CTempIconLoader(_T("SearchFileType_Document")));  //11
			pCurImageList->Replace(12, CTempIconLoader(_T("SearchFileType_Archive"))); //12
			pCurImageList->Replace(13, CTempIconLoader(_T("SearchFileType_CDImage"))); //13
			pCurImageList->Replace(14, CTempIconLoader(_T("AABCOLLECTIONFILETYPE"))); // 14
			pCurImageList->Replace(15, CTempIconLoader(_T("SearchFileType_Other")));// 15: Other Files
			pCurImageList->Replace(16, CTempIconLoader(_T("DUPLICATE")));// 16: Duplicate Files
			pCurImageList->Replace(17, CTempIconLoader(_T("SharedFolderOvl")));	// 17: Overlay
			pCurImageList->Replace(18, CTempIconLoader(_T("NoAccessFolderOvl")));// 18: Overlay
	} else {
		CImageList iml;
		iml.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
		iml.Add(CTempIconLoader(_T("AllFiles")));							// 0: All Directory
		iml.Add(CTempIconLoader(_T("Incomplete")));							// 1: Temp Directory
		iml.Add(CTempIconLoader(_T("Incoming")));							// 2: Incoming Directory
		iml.Add(CTempIconLoader(_T("Category")));							// 3: Cats
		iml.Add(CTempIconLoader(_T("HardDisk")));							// 4: All Dirs
		if (nImage > 0 && theApp.GetSystemImageList() != NULL) {			// 5: System Folder Icon
			HICON hIcon = ::ImageList_GetIcon(theApp.GetSystemImageList(), nImage, 0);
			iml.Add(hIcon);
			::DestroyIcon(hIcon);
		} else
			iml.Add(CTempIconLoader(_T("OpenFolder")));

		iml.Add(CTempIconLoader(_T("FileHistory"))); //6
		iml.Add(CTempIconLoader(_T("SearchFileType_Audio"))); //7
		iml.Add(CTempIconLoader(_T("SearchFileType_Video"))); //8
		iml.Add(CTempIconLoader(_T("SearchFileType_Picture"))); //9
		iml.Add(CTempIconLoader(_T("SearchFileType_Program"))); //10
		iml.Add(CTempIconLoader(_T("SearchFileType_Document")));  //11
		iml.Add(CTempIconLoader(_T("SearchFileType_Archive"))); //12
		iml.Add(CTempIconLoader(_T("SearchFileType_CDImage"))); //13
		iml.Add(CTempIconLoader(_T("AABCOLLECTIONFILETYPE")));// 14
		iml.Add(CTempIconLoader(_T("SearchFileType_Other")));// 15
		iml.Add(CTempIconLoader(_T("DUPLICATE"))); // 16: Duplicate Files
		iml.SetOverlayImage(iml.Add(CTempIconLoader(_T("SharedFolderOvl"))), 1); // 17: Overlay
		iml.SetOverlayImage(iml.Add(CTempIconLoader(_T("NoAccessFolderOvl"))), 2);	// 18: Overlay

		SetImageList(&iml, TVSIL_NORMAL);
		m_mapSystemIcons.RemoveAll();
		m_imlTree.DeleteImageList();
		m_imlTree.Attach(iml.Detach());
	}

	COLORREF crBk = GetCustomSysColor(COLOR_WINDOW);
	COLORREF crFg = GetCustomSysColor(COLOR_WINDOWTEXT);
	theApp.LoadSkinColorAlt(_T("SharedDirsTvBk"), _T("SharedFilesLvBk"), crBk);
	theApp.LoadSkinColorAlt(_T("SharedDirsTvFg"), _T("SharedFilesLvFg"), crFg);
	SetBkColor(crBk);
	SetTextColor(crFg);
}

void CSharedDirsTreeCtrl::Localize()
{
	InitalizeStandardItems();
	FilterTreeReloadTree();
	CreateMenus();
}

void CSharedDirsTreeCtrl::InitalizeStandardItems()
{
	// add standard items
	DeleteAllItems();
	delete m_pRootDirectoryItem;
	delete m_pRootUnsharedDirectries;
	delete pHistory;

	FetchSharedDirsList();

	static const CString sEmpty;
	m_pRootDirectoryItem = new CDirectoryItem(sEmpty, TVI_ROOT);
	CDirectoryItem *pAll = new CDirectoryItem(sEmpty, 0, SDI_ALL);
	pAll->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE, GetResString(_T("ALLSHAREDFILES")), 0, 0, TVIS_EXPANDED, TVIS_EXPANDED, (LPARAM)pAll, TVI_ROOT, TVI_LAST);
	m_pRootDirectoryItem->liSubDirectories.AddTail(pAll);

	CDirectoryItem *pIncoming = new CDirectoryItem(sEmpty, pAll->m_htItem, SDI_INCOMING);
	pIncoming->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE, GetResString(_T("INCOMING_FILES")), 2, 2, 0, 0, (LPARAM)pIncoming, pAll->m_htItem, TVI_LAST);
	m_pRootDirectoryItem->liSubDirectories.AddTail(pIncoming);

	CDirectoryItem *pTemp = new CDirectoryItem(sEmpty, pAll->m_htItem, SDI_TEMP);
	pTemp->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE, GetResString(_T("INCOMPLETE_FILES")), 1, 1, 0, 0, (LPARAM)pTemp, pAll->m_htItem, TVI_LAST);
	m_pRootDirectoryItem->liSubDirectories.AddTail(pTemp);

	CDirectoryItem *pDir = new CDirectoryItem(sEmpty, pAll->m_htItem, SDI_DIRECTORY);
	pDir->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE, GetResString(_T("SHARED_DIRECTORIES")), 5, 5, TVIS_EXPANDED, TVIS_EXPANDED, (LPARAM)pDir, pAll->m_htItem, TVI_LAST);
	m_pRootDirectoryItem->liSubDirectories.AddTail(pDir);

	CDirectoryItem* pDup = new CDirectoryItem(sEmpty, 0, SDI_DUP);
	pDup->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE, GetResString(_T("DUPLICATE_FILES")), 16, 16, 0, 0, (LPARAM)pDup, TVI_ROOT, TVI_LAST);
	m_pRootDirectoryItem->liSubDirectories.AddTail(pDup);

	m_pRootUnsharedDirectries = new CDirectoryItem(sEmpty, TVI_ROOT, SDI_FILESYSTEMPARENT);
	const CString &sAll(GetResString(_T("ALLDIRECTORIES")));
	TVINSERTSTRUCT tvis;
	tvis.hParent = TVI_ROOT;
	tvis.hInsertAfter = TVI_LAST;
	tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_CHILDREN;
	tvis.item.pszText = const_cast<LPTSTR>((LPCTSTR)sAll);
	tvis.item.iImage = 4;
	tvis.item.iSelectedImage = 4;
	tvis.item.state = 0;
	tvis.item.stateMask = 0;
	tvis.item.lParam = (LPARAM)m_pRootUnsharedDirectries;
	tvis.item.cChildren = 1; //ensure '+' symbol for the item
	m_pRootUnsharedDirectries->m_htItem = InsertItem(&tvis);

	CDirectoryItem* pHistory = new CDirectoryItem(sEmpty, 0, SDI_ALLHISTORY);
	pHistory->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_STATE, GetResString(_T("FILE_HISTORY")), 6, 6, TVIS_EXPANDED, TVIS_EXPANDED, (LPARAM)pHistory, TVI_ROOT, TVI_LAST);
	m_pRootDirectoryItem->liSubDirectories.AddTail(pHistory);
}

struct SEd2kTypeView
{
	int eType;
	LPCTSTR uStringId;
};

static const SEd2kTypeView _aEd2kTypeView[] =
{
	{ ED2KFT_AUDIO, _T("SEARCH_AUDIO") },
	{ ED2KFT_VIDEO, _T("SEARCH_VIDEO") },
	{ ED2KFT_IMAGE, _T("SEARCH_PICS") },
	{ ED2KFT_PROGRAM, _T("SEARCH_PRG") },
	{ ED2KFT_DOCUMENT, _T("SEARCH_DOC") },
	{ ED2KFT_ARCHIVE, _T("SEARCH_ARC") },
	{ ED2KFT_CDIMAGE, _T("SEARCH_CDIMG") },
	{ ED2KFT_EMULECOLLECTION, _T("SEARCH_EMULECOLLECTION") }
};

bool CSharedDirsTreeCtrl::FilterTreeIsSubDirectory(const CString &strDir, const CString &strRoot, const CStringList &liDirs)
{
	CString sRoot(strRoot);
	sRoot.MakeLower();
	ASSERT(strRoot.IsEmpty() || strRoot.Right(1) == _T("\\"));
	CString sDir(strDir);
	sDir.MakeLower();
	ASSERT(strDir.Right(1) == _T("\\"));
	for (POSITION pos = liDirs.GetHeadPosition(); pos != NULL;) {
		CString strCurrent(liDirs.GetNext(pos));
		strCurrent.MakeLower();
		ASSERT(strCurrent.Right(1) == _T("\\"));
		if (sRoot.Find(strCurrent, 0) != 0 && sDir.Find(strCurrent, 0) == 0 && strCurrent != sRoot && strCurrent != sDir)
			return true;
	}
	return false;
}

CString GetFolderLabel(const CString &strFolderPath, bool bTopFolder, bool bAccessible)
{
	CString strLabel(strFolderPath);
	if (strLabel.GetLength() == 2 && strLabel[1] == _T(':')) {
		ASSERT(bTopFolder);
		strLabel += _T('\\');
	} else {
		unslosh(strLabel);
		strLabel.Delete(0, strLabel.ReverseFind(_T('\\')) + 1);
		if (bTopFolder) {
			CString strParentFolder(strFolderPath);
			::PathRemoveFileSpec(strParentFolder.GetBuffer());
			strParentFolder.ReleaseBuffer();
			strLabel.AppendFormat(_T("  (%s)"), (LPCTSTR)strParentFolder);
		}
	}
	if (!bAccessible && bTopFolder)
		strLabel.AppendFormat(_T(" [%s]"), (LPCTSTR)GetResString(_T("NOTCONNECTED")));

	return strLabel;
}

void CSharedDirsTreeCtrl::FilterTreeAddSubDirectories(CDirectoryItem *pDirectory, const CStringList &liDirs
	, int nLevel, bool &rbShowWarning, bool bParentAccessible)
{
	// just some sanity check against too deep shared dirs
	// shouldn't be needed, but never trust the file system or a recursive function ;)
	// Guard against runaway recursion (junctions, cycles or pathological depth).
	// Show a placeholder child so the user knows there are more entries instead of silently dropping them.
	constexpr int MAX_RECURSION_DEPTH = 500;
	if (nLevel >= MAX_RECURSION_DEPTH) {
		CString strMore = _T("...");
		CDirectoryItem *pMore = new CDirectoryItem(CString());
		pMore->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE, strMore, 5, 5, 0, 0, (LPARAM)pMore, pDirectory->m_htItem, TVI_LAST);
		pDirectory->liSubDirectories.AddTail(pMore);
		return;
	}

	const CString &strDirectoryPath(pDirectory->m_strFullPath);
	int iLen = strDirectoryPath.GetLength();
	for (POSITION pos = liDirs.GetHeadPosition(); pos != NULL;) { //all paths in liDirs should have a trailing backslash
		const CString &strCurrent(liDirs.GetNext(pos));
		if ((iLen <= 0 || _tcsnicmp(strCurrent, strDirectoryPath, iLen) == 0) && iLen != strCurrent.GetLength()) {
			if (!FilterTreeIsSubDirectory(strCurrent, strDirectoryPath, liDirs)) {
				bool bAccessible = bParentAccessible ? IsAccessibleDirectoryLongPath(strCurrent) : false;
				const CString &strName(GetFolderLabel(strCurrent, nLevel == 0, bAccessible));
				CDirectoryItem *pNewItem = new CDirectoryItem(strCurrent);
				pNewItem->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE, strName, 5, 5, 0, 0, (LPARAM)pNewItem, pDirectory->m_htItem, TVI_SORT);
				if (!bAccessible) {
					SetItemState(pNewItem->m_htItem, INDEXTOOVERLAYMASK(2), TVIS_OVERLAYMASK);
					rbShowWarning = true;
				}
				pDirectory->liSubDirectories.AddTail(pNewItem);
				FilterTreeAddSubDirectories(pNewItem, liDirs, nLevel + 1, rbShowWarning, bAccessible);
			}
		}
	}
}

// Helper: recursively add real filesystem subdirectories under a virtual tree node (used for Incoming folder)
static void FilterTreeAddIncomingSubdirectories(CSharedDirsTreeCtrl* pThis, CDirectoryItem* pParent, const CString& basePath, int nLevel, bool &rbShowWarning)
{
    constexpr int MAX_RECURSION_DEPTH = 50;
    if (nLevel >= MAX_RECURSION_DEPTH) {
        CString strMore = _T("...");
        CDirectoryItem *pMore = new CDirectoryItem(CString());
        pMore->m_htItem = pThis->InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE, strMore, 5, 5, 0, 0, (LPARAM)pMore, pParent->m_htItem, TVI_LAST);
        pParent->liSubDirectories.AddTail(pMore);
        return;
    }

    // enumerate immediate subdirectories using long-path aware helper
    EnumSubdirectoriesLongPath(basePath, [pThis, pParent, nLevel, &rbShowWarning](const CString& childFullPath) {
        bool bAccessible = IsAccessibleDirectoryLongPath(childFullPath);
        const CString &strName = GetFolderLabel(childFullPath, false, bAccessible);
        CDirectoryItem *pNewItem = new CDirectoryItem(childFullPath);
        pNewItem->m_htItem = pThis->InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE, strName, 5, 5, 0, 0, (LPARAM)pNewItem, pParent->m_htItem, TVI_SORT);
        if (!bAccessible) {
            pThis->SetItemState(pNewItem->m_htItem, INDEXTOOVERLAYMASK(2), TVIS_OVERLAYMASK);
            rbShowWarning = true;
        }
        pParent->liSubDirectories.AddTail(pNewItem);
        // recurse
        FilterTreeAddIncomingSubdirectories(pThis, pNewItem, childFullPath, nLevel + 1, rbShowWarning);
    });
}

void CSharedDirsTreeCtrl::FilterTreeReloadTree()
{
	m_bCreatingTree = true;
	// store current selection
	CDirectoryItem *pOldSelectedItem = NULL;
	if (GetSelectedFilter() != NULL)
		pOldSelectedItem = GetSelectedFilter()->CloneContent();

	// create the tree substructure of directories we want to show
	for (POSITION pos = m_pRootDirectoryItem->liSubDirectories.GetHeadPosition(); pos != NULL;) {
		CDirectoryItem *pCurrent = m_pRootDirectoryItem->liSubDirectories.GetNext(pos);
		// clear old items
		DeleteChildItems(pCurrent);

		switch (pCurrent->m_eItemType) {
		case SDI_ALL:
		case SDI_DUP:
		case SDI_ALLHISTORY:
			{
				for (int i = 0; i < _countof(_aEd2kTypeView); ++i) {
					CDirectoryItem* pEd2kType = new CDirectoryItem(CString(EMPTY), 0, SDI_ED2KFILETYPE, _aEd2kTypeView[i].eType);
					pEd2kType->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE, GetResString(_aEd2kTypeView[i].uStringId), i + 7, i + 7, 0, 0, (LPARAM)pEd2kType, pCurrent->m_htItem, TVI_LAST);
					pCurrent->liSubDirectories.AddTail(pEd2kType);
				}

				// Add "Other" as sibling (same level as Collections)
				CDirectoryItem* pOther = new CDirectoryItem(CString(EMPTY), 0, SDI_ED2KFILETYPE, ED2KFT_OTHER);
				pOther->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE, GetResString(_T("STATS_PRTOTHER")), 15, 15, 0, 0, (LPARAM)pOther, pCurrent->m_htItem, TVI_LAST);
				pCurrent->liSubDirectories.AddTail(pOther);
			}
			break;
		case SDI_INCOMING:
			{
				CString strMainIncDir(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));

				bool bShowWarning = false;
				if (thePrefs.GetCatCount() > 1) {
					m_strliCatIncomingDirs.RemoveAll();
					for (INT_PTR i = 0; i < thePrefs.GetCatCount(); ++i) {
						Category_Struct *pCatStruct = thePrefs.GetCategory(i);
						if (pCatStruct != NULL) {
							const CString &strCatIncomingPath(pCatStruct->strIncomingPath);
							ASSERT(strCatIncomingPath.IsEmpty() || strCatIncomingPath.Right(1) == _T("\\"));
							if (!strCatIncomingPath.IsEmpty() && strCatIncomingPath.CompareNoCase(strMainIncDir) != 0
								&& m_strliCatIncomingDirs.Find(strCatIncomingPath) == NULL)
							{
								m_strliCatIncomingDirs.AddTail(strCatIncomingPath);
								bool bAccessible = IsAccessibleDirectoryLongPath(strCatIncomingPath);
								const CString &strName(GetFolderLabel(strCatIncomingPath, true, bAccessible));
								CDirectoryItem *pCatInc = new CDirectoryItem(strCatIncomingPath, 0, SDI_CATINCOMING);
								pCatInc->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE, strName, 5, 5, 0, 0, (LPARAM)pCatInc, pCurrent->m_htItem, TVI_SORT);
								if (!bAccessible) {
									SetItemState(pCatInc->m_htItem, INDEXTOOVERLAYMASK(2), TVIS_OVERLAYMASK);
									bShowWarning = true;
								}
								pCurrent->liSubDirectories.AddTail(pCatInc);
								// If AutoShareSubdirs enabled, add recursive subdirectories under this category incoming path
								if (thePrefs.GetAutoShareSubdirs()) {
									FilterTreeAddIncomingSubdirectories(this, pCatInc, strCatIncomingPath, 0, bShowWarning);
								}
							}
						}
					}
				}
				// If AutoShareSubdirs is enabled, enumerate the main incoming directory's subfolders directly under the "Incoming Files" node
				if (thePrefs.GetAutoShareSubdirs() && !strMainIncDir.IsEmpty()) {
					FilterTreeAddIncomingSubdirectories(this, pCurrent, strMainIncDir, 0, bShowWarning);
				}

				SetItemState(pCurrent->m_htItem, bShowWarning ? INDEXTOOVERLAYMASK(2) : 0, TVIS_OVERLAYMASK);
			}
			break;
		case SDI_TEMP:
			if (thePrefs.GetCatCount() > 1) {
				for (INT_PTR i = 0; i < thePrefs.GetCatCount(); ++i) {
					Category_Struct *pCatStruct = thePrefs.GetCategory(i);
					if (pCatStruct != NULL) {
						//temp dir
						CDirectoryItem *pCatTemp = new CDirectoryItem(CString(), 0, SDI_TEMP, (int)i);
						pCatTemp->m_htItem = InsertItem(TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE, pCatStruct->strTitle, 3, 3, 0, 0, (LPARAM)pCatTemp, pCurrent->m_htItem, TVI_LAST);
						pCurrent->liSubDirectories.AddTail(pCatTemp);

					}
				}
			}
			break;
		case SDI_DIRECTORY:
			{
				// add subdirectories
				bool bShowWarning = false;
				FilterTreeAddSubDirectories(pCurrent, m_strliSharedDirs, 0, bShowWarning, true);
				SetItemState(pCurrent->m_htItem, bShowWarning ? INDEXTOOVERLAYMASK(2) : 0, TVIS_OVERLAYMASK);
			}
			break;
		default:
			ASSERT(0);
		}
	}

	// restore selection
	HTREEITEM htOldSection;
	if (pOldSelectedItem != NULL && (htOldSection = m_pRootDirectoryItem->FindItem(pOldSelectedItem)) != NULL) {
		Select(htOldSection, TVGN_CARET);
		EnsureVisible(htOldSection);
	} else if (GetSelectedItem() == NULL && !m_pRootDirectoryItem->liSubDirectories.IsEmpty())
		Select(m_pRootDirectoryItem->liSubDirectories.GetHead()->m_htItem, TVGN_CARET);

	delete pOldSelectedItem;
	m_bCreatingTree = false;
	m_dwLastTreeStructCRC = ComputeTreeStructureCRC(); // Update structure signature after a successful rebuild
}

// Build a stable CRC for the directory structure under all shared roots and incoming roots. This is used to bypass redundant tree rebuilds on pure file-level changes.
DWORD CSharedDirsTreeCtrl::ComputeTreeStructureCRC() const
{
	// Gather directories (full paths, lowercased, without trailing backslashes)
	CStringArray aDirs;
	CMapStringToPtr seen; // avoid loops via reparse points

	// Shared directories roots (always include full recursive structure)
	for (POSITION pos = thePrefs.shareddir_list.GetHeadPosition(); pos != NULL; ) {
		CString dir = thePrefs.shareddir_list.GetNext(pos);
		if (!dir.IsEmpty()) {
			if (dir.Right(1) == _T("\\"))
				dir = dir.Left(dir.GetLength() - 1);
			AppendAllSubdirsRecursive(dir, seen, aDirs);
		}
	}

	// Incoming roots: main incoming + category incoming paths
	CString inc = thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR);
	if (!inc.IsEmpty()) {
		if (inc.Right(1) == _T("\\"))
			inc = inc.Left(inc.GetLength() - 1);
		AppendAllSubdirsRecursive(inc, seen, aDirs);
	}

	for (INT_PTR i = 0; i < thePrefs.GetCatCount(); ++i) {
		const Category_Struct* pCat = thePrefs.GetCategory(i);
		if (pCat != NULL) {
			CString cdir = pCat->strIncomingPath;
			if (!cdir.IsEmpty()) {
				if (cdir.Right(1) == _T("\\"))
					cdir = cdir.Left(cdir.GetLength() - 1);
				AppendAllSubdirsRecursive(cdir, seen, aDirs);
			}
		}
	}

	// Sort for stable hash
	std::vector<CString> v;
	v.reserve(aDirs.GetCount());
	for (INT_PTR i = 0; i < aDirs.GetCount(); ++i)
		v.push_back(aDirs[i]);
	std::sort(v.begin(), v.end(), [](const CString& a, const CString& b){ return a.CompareNoCase(b) < 0; });

	// Compute FNV-1a 32-bit
	const DWORD kFnvOffset = 2166136261u;
	const DWORD kFnvPrime = 16777619u;
	DWORD h = kFnvOffset;
	for (size_t i = 0; i < v.size(); ++i) {
		CString s = v[i];
		s.MakeLower();
		for (int j = 0; j < s.GetLength(); ++j) {
			WCHAR ch = s[j];
			// Update with both bytes for Unicode code unit to keep order sensitivity
			BYTE lo = (BYTE)(ch & 0xFF);
			BYTE hi = (BYTE)((ch >> 8) & 0xFF);
			h ^= lo; h *= kFnvPrime;
			h ^= hi; h *= kFnvPrime;
		}
		// Separator
		h ^= 0x2Fu;
		h *= kFnvPrime;
	}
	// Mix count to differentiate equal concatenations
	h ^= (DWORD)v.size();
	h *= kFnvPrime;
	return h;
}

void CSharedDirsTreeCtrl::AppendAllSubdirsRecursive(const CString& root, CMapStringToPtr& seen, CStringArray& out)
{
	CString key(root);
	if (key.Right(1) == _T("\\"))
		key = key.Left(key.GetLength() - 1);

	CString low(key);
	low.MakeLower();
	void* pv = NULL;
	if (seen.Lookup(low, pv))
		return;

	seen.SetAt(low, (void*)1);
	out.Add(key);

	EnumSubdirectoriesLongPath(key, [&seen, &out](const CString& childFullPath){
		CSharedDirsTreeCtrl::AppendAllSubdirsRecursive(childFullPath, seen, out);
	});
}

CDirectoryItem* CSharedDirsTreeCtrl::GetSelectedFilter() const
{
	const HTREEITEM item = GetSelectedItem();
	return item ? reinterpret_cast<CDirectoryItem*>(GetItemData(item)) : NULL;

}
CDirectoryItem* CSharedDirsTreeCtrl::GetSelectedFilterParent() const
{
	const HTREEITEM item = GetParentItem(GetSelectedItem());
	return item ? reinterpret_cast<CDirectoryItem*>(GetItemData(item)) : NULL;
}

void CSharedDirsTreeCtrl::CreateMenus()
{
	if (m_PrioMenu)
		VERIFY(m_PrioMenu.DestroyMenu());
	if (m_SharedFilesMenu)
		VERIFY(m_SharedFilesMenu.DestroyMenu());
	if (m_ShareDirsMenu)
		VERIFY(m_ShareDirsMenu.DestroyMenu());

	if (m_ReloadMenu)
		VERIFY(m_ReloadMenu.DestroyMenu());
	m_ReloadMenu.CreatePopupMenu();
	m_ReloadMenu.AppendMenu(MF_STRING, MP_RELOADTREE, GetResString(_T("RELOAD_TREE")), _T("RELOAD"));

	m_PrioMenu.CreateMenu();
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOVERYLOW, GetResString(_T("PRIOVERYLOW")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOLOW, GetResString(_T("PRIOLOW")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIONORMAL, GetResString(_T("PRIONORMAL")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOHIGH, GetResString(_T("PRIOHIGH")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOVERYHIGH, GetResString(_T("PRIORELEASE")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOAUTO, GetResString(_T("PRIOAUTO")));//UAP

	m_SharedFilesMenu.CreatePopupMenu();
	m_SharedFilesMenu.AddMenuSidebar(GetResString(_T("SHAREDFILES")));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_OPENFOLDER, GetResString(_T("OPENFOLDER")), _T("OPENFOLDER"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_REMOVE, GetResString(_T("DELETE")), _T("DELETE"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	CString sPrio(GetResString(_T("PRIORITY")));
	sPrio.AppendFormat(_T(" (%s)"), (LPCTSTR)GetResString(_T("PW_CON_UPLBL")));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_PrioMenu.m_hMenu, sPrio, _T("FILEPRIORITY"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_DETAIL, GetResString(_T("SHOWDETAILS")), _T("FILEINFO"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_CMT, GetResString(_T("CMT_ADD")), _T("FILECOMMENTS"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_CUT, GetResString(_T("COPY_FILE_NAMES")), _T("FILERENAME"));
	if (thePrefs.GetShowCopyEd2kLinkCmd())
		m_SharedFilesMenu.AppendMenu(MF_STRING, MP_GETED2KLINK, GetResString(_T("DL_LINK1")), _T("ED2KLINK"));
	else
		m_SharedFilesMenu.AppendMenu(MF_STRING, MP_SHOWED2KLINK, GetResString(_T("DL_SHOWED2KLINK")), _T("ED2KLINK"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_UNSHAREDIR, GetResString(_T("UNSHAREDIR")));
	if (!thePrefs.GetAutoShareSubdirs())
		m_SharedFilesMenu.AppendMenu(MF_STRING, MP_UNSHAREDIRSUB, GetResString(_T("UNSHAREDIRSUB")));

	m_ShareDirsMenu.CreatePopupMenu();
	m_ShareDirsMenu.AddMenuSidebar(GetResString(_T("SHAREDFILES")));
	m_ShareDirsMenu.AppendMenu(MF_STRING, MP_OPENFOLDER, GetResString(_T("OPENFOLDER")), _T("OPENFOLDER"));
	m_ShareDirsMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	m_ShareDirsMenu.AppendMenu(MF_STRING, MP_SHAREDIR, GetResString(_T("SHAREDIR")));
	if (!thePrefs.GetAutoShareSubdirs())
		m_ShareDirsMenu.AppendMenu(MF_STRING, MP_SHAREDIRSUB, GetResString(_T("SHAREDIRSUB")));
	m_ShareDirsMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	m_ShareDirsMenu.AppendMenu(MF_STRING, MP_UNSHAREDIR, GetResString(_T("UNSHAREDIR")));
	if (!thePrefs.GetAutoShareSubdirs())
		m_ShareDirsMenu.AppendMenu(MF_STRING, MP_UNSHAREDIRSUB, GetResString(_T("UNSHAREDIRSUB")));
}

void CSharedDirsTreeCtrl::OnContextMenu(CWnd*, CPoint point)
{
	if (!PointInClient(*this, point)) {
		Default();
		return;
	}

	CDirectoryItem *pSelectedDir = GetSelectedFilter();
	if (pSelectedDir != NULL && (pSelectedDir->m_eItemType == SDI_DUP || pSelectedDir->m_eItemType == SDI_DIRECTORY || pSelectedDir->m_eItemType == SDI_ALLHISTORY || pSelectedDir->m_eItemType == SDI_ED2KFILETYPE))
		return; //no context menu
	if (pSelectedDir != NULL && (pSelectedDir->m_eItemType == SDI_ALL || pSelectedDir->m_eItemType == SDI_FILESYSTEMPARENT)) {
		m_ReloadMenu.EnableMenuItem(MP_RELOADTREE, MF_ENABLED);
		GetPopupMenuPos(*this, point);
		m_ReloadMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
		return;
	}
	if (pSelectedDir != NULL && pSelectedDir->m_eItemType != SDI_UNSHAREDDIRECTORY && pSelectedDir->m_eItemType != SDI_FILESYSTEMPARENT) {
		int iSelectedItems = m_pSharedFilesCtrl->m_ListedItemsVector.size();
		int iCompleteFileSelected = -1;
		UINT uPrioMenuItem = 0;
		bool bFirstItem = true;
		for (int i = 0; i < iSelectedItems; ++i) {
			const CKnownFile *pFile = reinterpret_cast<CKnownFile*>(m_pSharedFilesCtrl->m_ListedItemsVector[i]);

			int iCurCompleteFile = static_cast<int>(!pFile->IsPartFile());
			if (bFirstItem)
				iCompleteFileSelected = iCurCompleteFile;
			else if (iCompleteFileSelected != iCurCompleteFile)
				iCompleteFileSelected = -1;

			UINT uCurPrioMenuItem;
			if (pFile->IsAutoUpPriority())
				uCurPrioMenuItem = MP_PRIOAUTO;
			else
				switch (pFile->GetUpPriority()) {
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
					uCurPrioMenuItem = 0;
					ASSERT(0);
				}

			if (bFirstItem)
				uPrioMenuItem = uCurPrioMenuItem;
			else if (uPrioMenuItem != uCurPrioMenuItem)
				uPrioMenuItem = 0;

			bFirstItem = false;
		}

		// just avoid that users get bad ideas by showing the comment/delete-option for the "all" selections
		// as the same comment for all files/all incomplete files/ etc is probably not too usefull
		// - even if it can be done in other ways if the user really wants to do it
		bool bWideRangeSelection = (pSelectedDir->m_nCatFilter == -1 && pSelectedDir->m_eItemType != SDI_NO);

		m_SharedFilesMenu.EnableMenuItem((UINT)m_PrioMenu.m_hMenu, iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED);
		m_PrioMenu.CheckMenuRadioItem(MP_PRIOVERYLOW, MP_PRIOAUTO, uPrioMenuItem, 0);

		m_SharedFilesMenu.EnableMenuItem(MP_OPENFOLDER, !pSelectedDir->m_strFullPath.IsEmpty() || pSelectedDir->m_eItemType == SDI_INCOMING || pSelectedDir->m_eItemType == SDI_TEMP || pSelectedDir->m_eItemType == SDI_CATINCOMING ? MF_ENABLED : MF_GRAYED);
		m_SharedFilesMenu.EnableMenuItem(MP_REMOVE, (iCompleteFileSelected > 0 && !bWideRangeSelection) ? MF_ENABLED : MF_GRAYED);
		m_SharedFilesMenu.EnableMenuItem(MP_CMT, (iSelectedItems > 0 && !bWideRangeSelection) ? MF_ENABLED : MF_GRAYED);
		m_SharedFilesMenu.EnableMenuItem(MP_DETAIL, iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED);
		m_SharedFilesMenu.EnableMenuItem(MP_CUT, (iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED);
		m_SharedFilesMenu.EnableMenuItem(thePrefs.GetShowCopyEd2kLinkCmd() ? MP_GETED2KLINK : MP_SHOWED2KLINK, iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED);
		m_SharedFilesMenu.EnableMenuItem(MP_UNSHAREDIR, (pSelectedDir->m_eItemType == SDI_NO && !pSelectedDir->m_strFullPath.IsEmpty() && FileSystemTreeIsShared(pSelectedDir->m_strFullPath)) ? MF_ENABLED : MF_GRAYED);
		m_SharedFilesMenu.EnableMenuItem(MP_UNSHAREDIRSUB
			, (pSelectedDir->m_eItemType == SDI_DIRECTORY && ItemHasChildren(pSelectedDir->m_htItem)
			|| (pSelectedDir->m_eItemType == SDI_NO && !pSelectedDir->m_strFullPath.IsEmpty() && (FileSystemTreeIsShared(pSelectedDir->m_strFullPath)
			|| FileSystemTreeHasSharedSubdirectory(pSelectedDir->m_strFullPath, false)))) ? MF_ENABLED : MF_GRAYED);

		GetPopupMenuPos(*this, point);
		m_SharedFilesMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
	} else if (pSelectedDir != NULL && pSelectedDir->m_eItemType == SDI_UNSHAREDDIRECTORY) {
		m_ShareDirsMenu.EnableMenuItem(MP_UNSHAREDIR, FileSystemTreeIsShared(pSelectedDir->m_strFullPath) ? MF_ENABLED : MF_GRAYED);
		m_ShareDirsMenu.EnableMenuItem(MP_UNSHAREDIRSUB, (FileSystemTreeIsShared(pSelectedDir->m_strFullPath) || FileSystemTreeHasSharedSubdirectory(pSelectedDir->m_strFullPath, false)) ? MF_ENABLED : MF_GRAYED);
		m_ShareDirsMenu.EnableMenuItem(MP_SHAREDIR, !FileSystemTreeIsShared(pSelectedDir->m_strFullPath) && thePrefs.IsShareableDirectory(pSelectedDir->m_strFullPath) ? MF_ENABLED : MF_GRAYED);
		m_ShareDirsMenu.EnableMenuItem(MP_SHAREDIRSUB, FileSystemTreeHasSubdirectories(pSelectedDir->m_strFullPath) && thePrefs.IsShareableDirectory(pSelectedDir->m_strFullPath) ? MF_ENABLED : MF_GRAYED);

		GetPopupMenuPos(*this, point);
		m_ShareDirsMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
	}
}

void CSharedDirsTreeCtrl::OnRButtonDown(UINT, CPoint point)
{
	UINT uHitFlags;
	HTREEITEM hItem = HitTest(point, &uHitFlags);
	if (hItem != NULL && (uHitFlags & TVHT_ONITEM)) {
		Select(hItem, TVGN_CARET);
		SetItemState(hItem, TVIS_SELECTED, TVIS_SELECTED);
	}
}

BOOL CSharedDirsTreeCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	CTypedPtrList<CPtrList, CShareableFile*> selectedList;
	int iSelectedItems = m_pSharedFilesCtrl->m_ListedItemsVector.size();
	for (int i = 0; i < iSelectedItems; ++i)
		selectedList.AddTail(reinterpret_cast<CShareableFile*>(m_pSharedFilesCtrl->m_ListedItemsVector[i]));

	const CDirectoryItem *pSelectedDir = GetSelectedFilter();
	if (pSelectedDir == NULL)
		return TRUE;

	// folder based
	switch (wParam) {
	case MP_OPENFOLDER:
		if (!pSelectedDir->m_strFullPath.IsEmpty() /*&& pSelectedDir->m_eItemType == SDI_NO*/)
			ShellOpenFile(pSelectedDir->m_strFullPath);
		else if (pSelectedDir->m_eItemType == SDI_INCOMING)
			ShellOpenFile(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
		else if (pSelectedDir->m_eItemType == SDI_TEMP)
			ShellOpenFile(thePrefs.GetTempDir());
		break;
	case MP_SHAREDIR:
		EditSharedDirectories(pSelectedDir, true, false);
		break;
	case MP_SHAREDIRSUB:
		EditSharedDirectories(pSelectedDir, true, true);
		break;
	case MP_UNSHAREDIR:
		EditSharedDirectories(pSelectedDir, false, false);
		break;
	case MP_UNSHAREDIRSUB:
		EditSharedDirectories(pSelectedDir, false, true);
		break;
	case MP_RELOADTREE:
	{
		if (pSelectedDir->m_eItemType == SDI_ALL)
			Reload(true);
		else if (pSelectedDir->m_eItemType == SDI_FILESYSTEMPARENT)
			Expand(m_pRootUnsharedDirectries->m_htItem, TVE_COLLAPSE);
	}
	}

	// file based
	if (iSelectedItems > 0) {
		switch (wParam) {
		case MP_CUT:
			{
				CString m_strFileNames;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					const CKnownFile* file = static_cast<CKnownFile*>(selectedList.GetNext(pos));
					if (file && file->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
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
				CString str;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					const CKnownFile *file = static_cast<CKnownFile*>(selectedList.GetNext(pos));
					if (file && file->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
						if (!str.IsEmpty())
							str += _T("\r\n");
						str += file->GetED2kLink();
					}
				}

				if (!str.IsEmpty()) {
					theApp.CopyTextToClipboard(str);
					theApp.emuledlg->statusbar->SetText(GetResString(_T("ED2K_LINK_COPIED_TO_CLIPBOARD")), SBarLog, 0);
				}

			}
			break;
		// file operations
		case MP_REMOVE:
		case MPG_DELETE:
			{
				if (IDNO == LocMessageBox(_T("CONFIRM_FILEDELETE"), MB_ICONWARNING | MB_DEFBUTTON2 | MB_YESNO, 0))
					return TRUE;

				m_pSharedFilesCtrl->SetRedraw(false);
				bool bRemovedItems = false;
				while (!selectedList.IsEmpty()) {
					CShareableFile *myfile = selectedList.RemoveHead();
					if (!myfile || myfile->IsPartFile())
						continue;

					bool delsucc = ShellDeleteFile(myfile->GetFilePath());
					if (delsucc) {
						if (myfile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
							theApp.sharedfiles->RemoveFile(static_cast<CKnownFile*>(myfile), true);
						bRemovedItems = true;
						if (myfile->IsKindOf(RUNTIME_CLASS(CPartFile)))
							theApp.emuledlg->transferwnd->GetDownloadList()->ClearCompleted(static_cast<CPartFile*>(myfile));
					} else {
						CString strError;
						strError.Format(GetResString(_T("ERR_DELFILE")), (LPCTSTR)myfile->GetFilePath());
						strError.AppendFormat(_T("\r\n\r\n%s"), (LPCTSTR)GetErrorMessage(::GetLastError()));
						CDarkMode::MessageBox(strError);
					}
				}
				m_pSharedFilesCtrl->SetRedraw(true);
				if (bRemovedItems) {
					m_pSharedFilesCtrl->AutoSelectItem();
					// Depending on <no-idea> this does not always cause an LVN_ITEMACTIVATE
					// message to be sent. So, explicitly redraw the item.
					theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails();
					theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged(); // might have been a single shared file
				}
			}
			break;
		case MP_CMT:
			ShowFileDialog(selectedList, IDD_COMMENT);
			break;
		case MP_DETAIL:
		case MPG_ALTENTER:
			ShowFileDialog(selectedList);
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
				CKnownFile *file = static_cast<CKnownFile*>(selectedList.GetNext(pos));
				if (file->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
					uint8 pri;
					switch (wParam) {
					case MP_PRIOVERYLOW:
						pri = PR_VERYLOW;
						break;
					case MP_PRIOLOW:
						pri = PR_LOW;
						break;
					case MP_PRIONORMAL:
						pri = PR_NORMAL;
						break;
					case MP_PRIOHIGH:
						pri = PR_HIGH;
						break;
					case MP_PRIOVERYHIGH:
						pri = PR_VERYHIGH;
					case MP_PRIOAUTO:
						break;
					default:
						wParam = MP_PRIOAUTO;
					}
					file->SetAutoUpPriority(wParam == MP_PRIOAUTO);
					if (wParam == MP_PRIOAUTO)
						file->UpdateAutoUpPriority();
					else {
						file->SetUpPriority(pri);
						m_pSharedFilesCtrl->UpdateFile(file);
					}
				}
			}
		default:
			break;
		}
	}
	return TRUE;
}

void CSharedDirsTreeCtrl::ShowFileDialog(CTypedPtrList<CPtrList, CShareableFile*> &aFiles, UINT uInvokePage)
{
	m_pSharedFilesCtrl->ShowFileDialog(aFiles, uInvokePage);
}

void CSharedDirsTreeCtrl::FileSystemTreeCreateTree()
{
	TCHAR drivebuffer[500];
	DWORD dwRet = GetLogicalDriveStrings(_countof(drivebuffer) - 1, drivebuffer);
	if (dwRet > 0 && dwRet < _countof(drivebuffer)) {
		drivebuffer[_countof(drivebuffer) - 1] = _T('\0');

		for (TCHAR *pos = drivebuffer; *pos != _T('\0'); pos += _tcslen(pos) + 2) {
			// Copy drive name
			pos[2] = _T('\0'); //drop backslash, leave only drive letter with column; e.g. "C:"
			FileSystemTreeAddChildItem(m_pRootUnsharedDirectries, pos, true);
		}
	}
}

// Long-path aware helper: prepare path and enumerate immediate subdirectories (non-recursive)
static void EnumSubdirectoriesLongPath(const CString& dir, const std::function<void(const CString&)>& cb)
{
    CString sDir(dir);
    slosh(sDir); // ensure trailing backslash
    CString search = sDir + _T("*");
    CString searchLong = PreparePathForWin32LongPath(search);

#ifdef UNICODE
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = ::FindFirstFileW((LPCWSTR)searchLong.GetString(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;
    do {
        const wchar_t* name = ffd.cFileName;
        if (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0)
            continue;
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM)
            continue;
        CString child = sDir + CString(name);
        // remove any trailing backslash to match old behaviour
        if (child.Right(1) == _T("\\"))
            child = child.Left(child.GetLength()-1);
        cb(child);
    } while (::FindNextFileW(hFind, &ffd));
    ::FindClose(hFind);
#else
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = ::FindFirstFileA((LPCSTR)CW2A(searchLong.GetString()), &ffd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;
    do {
        const char* name = ffd.cFileName;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM)
            continue;
        CString child = sDir + CString(CA2T(name));
        if (child.Right(1) == _T("\\"))
            child = child.Left(child.GetLength()-1);
        cb(child);
    } while (::FindNextFileA(hFind, &ffd));
    ::FindClose(hFind);
#endif
}

// Long-path aware helper: check directory accessibility/existence
static bool IsAccessibleDirectoryLongPath(const CString& dir)
{
    CString sDir(dir);
    slosh(sDir);
    CString longp = PreparePathForWin32LongPath(sDir);
#ifdef UNICODE
    DWORD attrs = ::GetFileAttributesW((LPCWSTR)longp.GetString());
#else
    DWORD attrs = ::GetFileAttributesA((LPCSTR)CW2A(longp.GetString()));
#endif
    return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

void CSharedDirsTreeCtrl::FileSystemTreeAddChildItem(CDirectoryItem *pRoot, const CString &strText, bool bTopLevel)
{
	CString strPath(pRoot->m_strFullPath);
	if (!strPath.IsEmpty())
		slosh(strPath);
	CString strDir(strPath + strText);
	slosh(strDir);
	TVINSERTSTRUCT itInsert = {};

	if (m_bUseIcons) {
		itInsert.item.mask = TVIF_CHILDREN | TVIF_HANDLE | TVIF_TEXT | TVIF_STATE | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
		itInsert.item.stateMask = TVIS_BOLD | TVIS_STATEIMAGEMASK;
	} else {
		itInsert.item.mask = TVIF_CHILDREN | TVIF_HANDLE | TVIF_TEXT | TVIF_STATE;
		itInsert.item.stateMask = TVIS_BOLD;
	}

	if (FileSystemTreeHasSharedSubdirectory(strDir, true) || FileSystemTreeIsShared(strDir))
		itInsert.item.state = TVIS_BOLD;
	else
		itInsert.item.state = 0;
	itInsert.item.cChildren = FileSystemTreeHasSubdirectories(strDir) ? I_CHILDRENCALLBACK : 0;	// used to display the '+' symbol next to each item

	CDirectoryItem *pti = new CDirectoryItem(strDir, 0, SDI_UNSHAREDDIRECTORY);

	itInsert.item.pszText = const_cast<LPTSTR>((LPCTSTR)strText);
	itInsert.hInsertAfter = !bTopLevel ? TVI_SORT : TVI_LAST;
	itInsert.hParent = pRoot->m_htItem;
	itInsert.item.mask |= TVIF_PARAM;
	itInsert.item.lParam = (LPARAM)pti;

	SHFILEINFO shFinfo;
	if (m_bUseIcons) {
		if (FileSystemTreeIsShared(strDir)) {
			itInsert.item.stateMask |= TVIS_OVERLAYMASK;
			itInsert.item.state |= INDEXTOOVERLAYMASK(1);
		}

		slosh(strDir);
		UINT nType = ::GetDriveType(strDir);
		if (DRIVE_REMOVABLE <= nType && nType <= DRIVE_RAMDISK)
			itInsert.item.iImage = nType;

		shFinfo.szDisplayName[0] = _T('\0');
		if (::SHGetFileInfo(strDir, 0, &shFinfo, sizeof(shFinfo), SHGFI_SMALLICON | SHGFI_ICON | SHGFI_OPENICON | SHGFI_DISPLAYNAME)) {
			itInsert.itemex.iImage = AddSystemIcon(shFinfo.hIcon, shFinfo.iIcon);
			::DestroyIcon(shFinfo.hIcon);
			if (bTopLevel && shFinfo.szDisplayName[0] != _T('\0'))
				itInsert.item.pszText = shFinfo.szDisplayName;
		} else {
			TRACE(_T("Error getting SystemFileInfo!"));
			itInsert.itemex.iImage = 0; // :(
		}

	}

	pti->m_htItem = InsertItem(&itInsert);
	pRoot->liSubDirectories.AddTail(pti);
}

bool CSharedDirsTreeCtrl::FileSystemTreeHasSubdirectories(const CString &strDir)
{
	return ::HasSubdirectories(strDir);
}

bool CSharedDirsTreeCtrl::FileSystemTreeHasSharedSubdirectory(const CString &strDir, bool bOrFiles)
{
	int iLen = strDir.GetLength();
	ASSERT(iLen > 0);
	bool bSlosh = (strDir[iLen - 1] == _T('\\'));
	for (POSITION pos = m_strliSharedDirs.GetHeadPosition(); pos != NULL;) {
		const CString &sCurrent(m_strliSharedDirs.GetNext(pos));
		if (_tcsnicmp(sCurrent, strDir, iLen) == 0 && iLen < sCurrent.GetLength() && (bSlosh || sCurrent[iLen] == _T('\\')))
			return true;
	}
	return bOrFiles && theApp.sharedfiles->ContainsSingleSharedFiles(strDir);
}

void CSharedDirsTreeCtrl::FileSystemTreeAddSubdirectories(CDirectoryItem *pRoot)
{
	ASSERT(pRoot->m_strFullPath.Right(1) == _T("\\"));
	// Use long-path aware enumeration
	EnumSubdirectoriesLongPath(pRoot->m_strFullPath, [this, pRoot](const CString& childFullPath){
		CString strFilename(childFullPath);
		strFilename.Delete(0, strFilename.ReverseFind(_T('\\')) + 1);
		FileSystemTreeAddChildItem(pRoot, strFilename, false);
	});
}

int CSharedDirsTreeCtrl::AddSystemIcon(HICON hIcon, int nSystemListPos)
{
	int nPos;
	if (!m_mapSystemIcons.Lookup(nSystemListPos, nPos)) {
		nPos = GetImageList(TVSIL_NORMAL)->Add(hIcon);
		m_mapSystemIcons[nSystemListPos] = nPos;
	} else
		nPos = 0;
	return nPos;
}

void CSharedDirsTreeCtrl::OnTvnItemexpanding(LPNMHDR pNMHDR, LRESULT *pResult)
{
	CWaitCursor curWait;
	SetRedraw(FALSE);

	LPNMTREEVIEW pNMTreeView = reinterpret_cast<LPNMTREEVIEW>(pNMHDR);
	CDirectoryItem *pExpanded = reinterpret_cast<CDirectoryItem*>(pNMTreeView->itemNew.lParam);
	if (pExpanded != NULL) {
		if (pExpanded->m_eItemType == SDI_UNSHAREDDIRECTORY && !pExpanded->m_strFullPath.IsEmpty()) {
			// remove all sub-items
			DeleteChildItems(pExpanded);
			// fetch all subdirectories and add them to the node
			FileSystemTreeAddSubdirectories(pExpanded);
		} else if (pExpanded->m_eItemType == SDI_FILESYSTEMPARENT) {
			DeleteChildItems(pExpanded);
			FileSystemTreeCreateTree();
		}
	} else
		ASSERT(0);

	SetRedraw(TRUE);
	Invalidate();
	*pResult = 0;
}

void CSharedDirsTreeCtrl::DeleteChildItems(CDirectoryItem *pParent)
{
	while (!pParent->liSubDirectories.IsEmpty()) {
		CDirectoryItem *pToDelete = pParent->liSubDirectories.RemoveHead();
		DeleteItem(pToDelete->m_htItem);
		DeleteChildItems(pToDelete);
		delete pToDelete;
	}
}

bool CSharedDirsTreeCtrl::FileSystemTreeIsShared(const CString &strDir)
{
	for (POSITION pos = m_strliSharedDirs.GetHeadPosition(); pos != NULL;) {
		if (EqualPaths(m_strliSharedDirs.GetNext(pos), strDir))
			return true;
	}
	return false;
}

void CSharedDirsTreeCtrl::OnTvnGetdispinfo(LPNMHDR pNMHDR, LRESULT *pResult)
{
	reinterpret_cast<LPNMTVDISPINFO>(pNMHDR)->item.cChildren = 1;
	*pResult = 0;
}

void CSharedDirsTreeCtrl::AddSharedDirectory(const CString &strDir, bool bSubDirectories)
{
	CString sDir(strDir);
	slosh(sDir);
	if (!FileSystemTreeIsShared(sDir) && thePrefs.IsShareableDirectory(sDir))
		m_strliSharedDirs.AddTail(sDir);

	if (bSubDirectories) {
		// enumerate subdirectories using long-path aware helper
		EnumSubdirectoriesLongPath(sDir, [this](const CString& childFullPath){
			AddSharedDirectory(childFullPath, true); //no trailing backslash here
		});
	}
}

void CSharedDirsTreeCtrl::RemoveSharedDirectory(const CString &strDir, bool bSubDirectories)
{
	int iLen = strDir.GetLength();
	for (POSITION pos = m_strliSharedDirs.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		const CString &str(m_strliSharedDirs.GetNext(pos));
		if (_tcsnicmp(str, strDir, (bSubDirectories ? iLen : max(iLen, str.GetLength()))) == 0) {
			m_strliSharedDirs.RemoveAt(pos2);
			if (!bSubDirectories)
				break;
		}
	}
}

void CSharedDirsTreeCtrl::RemoveAllSharedDirectories()
{
	m_strliSharedDirs.RemoveAll();
}

void CSharedDirsTreeCtrl::FileSystemTreeUpdateBoldState(const CDirectoryItem *pDir)
{
	if (pDir == NULL)
		pDir = m_pRootUnsharedDirectries;
	else
		SetItemState(pDir->m_htItem, ((FileSystemTreeHasSharedSubdirectory(pDir->m_strFullPath, true) || FileSystemTreeIsShared(pDir->m_strFullPath)) ? TVIS_BOLD : 0), TVIS_BOLD);
	for (POSITION pos = pDir->liSubDirectories.GetHeadPosition(); pos != NULL;)
		FileSystemTreeUpdateBoldState(pDir->liSubDirectories.GetNext(pos));
}

void CSharedDirsTreeCtrl::FileSystemTreeUpdateShareState(const CDirectoryItem *pDir)
{
	if (pDir == NULL)
		pDir = m_pRootUnsharedDirectries;
	else
		SetItemState(pDir->m_htItem, FileSystemTreeIsShared(pDir->m_strFullPath) ? INDEXTOOVERLAYMASK(1) : 0, TVIS_OVERLAYMASK);
	for (POSITION pos = pDir->liSubDirectories.GetHeadPosition(); pos != NULL;)
		FileSystemTreeUpdateShareState(pDir->liSubDirectories.GetNext(pos));
}

void CSharedDirsTreeCtrl::FileSystemTreeSetShareState(const CDirectoryItem *pDir, bool bSubDirectories)
{
	if (m_bUseIcons && pDir->m_htItem != NULL)
		SetItemState(pDir->m_htItem, FileSystemTreeIsShared(pDir->m_strFullPath) ? INDEXTOOVERLAYMASK(1) : 0, TVIS_OVERLAYMASK);
	if (bSubDirectories)
		for (POSITION pos = pDir->liSubDirectories.GetHeadPosition(); pos != NULL;)
			FileSystemTreeSetShareState(pDir->liSubDirectories.GetNext(pos), true);
}

void CSharedDirsTreeCtrl::EditSharedDirectories(const CDirectoryItem *pDir, bool bAdd, bool bSubDirectories)
{
	ASSERT(pDir->m_eItemType == SDI_UNSHAREDDIRECTORY || pDir->m_eItemType == SDI_NO || (pDir->m_eItemType == SDI_DIRECTORY && !bAdd && pDir->m_strFullPath.IsEmpty()));
	CWaitCursor curWait;

	// Recurse when the user explicitly selected include sub-dirs OR the global preference AutoShareSubdirs is enabled
	bool bRecursive = bSubDirectories || thePrefs.GetAutoShareSubdirs();

	if (bAdd)
		AddSharedDirectory(pDir->m_strFullPath, bRecursive);
	else if (pDir->m_eItemType == SDI_DIRECTORY)
		RemoveAllSharedDirectories();
	else
		RemoveSharedDirectory(pDir->m_strFullPath, bRecursive);

	// Refresh visual share state
	if (pDir->m_eItemType == SDI_NO || pDir->m_eItemType == SDI_DIRECTORY)
		// An 'Unshare' was invoked from within the virtual "Shared Directories" folder, thus we do not have the tree view item handle of the item within the "All Directories" tree -> need to update the
		// entire tree in case the tree view item is currently visible.
		FileSystemTreeUpdateShareState();
	else
		// A 'Share' or 'Unshare' was invoked for a certain tree view item within the "All Directories" tree, thus we know the tree view item handle which needs to be updated for showing the new share state.
		FileSystemTreeSetShareState(pDir, bRecursive);

	FileSystemTreeUpdateBoldState();
	FilterTreeReloadTree();

	// sync with the preferences list
	thePrefs.shareddir_list.RemoveAll();
	// copy list
	thePrefs.shareddir_list.AddTail(&m_strliSharedDirs);

	CemuleDlg* pDlg = theApp.emuledlg;
	if (pDlg && pDlg->sharedfileswnd && ::IsWindow(pDlg->sharedfileswnd->m_hWnd))
		::PostMessage(pDlg->sharedfileswnd->m_hWnd, UM_AUTO_RELOAD_SHARED_FILES, 1, 0);

	if (GetSelectedFilter() != NULL && GetSelectedFilter()->m_eItemType == SDI_UNSHAREDDIRECTORY) // if in file system view, update the list to reflect the changes in the checkboxes
		m_pSharedFilesCtrl->UpdateWindow(); 
	thePrefs.SaveSharedFolders();
}

void CSharedDirsTreeCtrl::Reload(bool bForce)
{
	if (!bForce) {
		// check for changes in shared dirs
		bForce = (thePrefs.shareddir_list.GetCount() != m_strliSharedDirs.GetCount());
		if (!bForce) {
			POSITION pos = m_strliSharedDirs.GetHeadPosition();
			POSITION pos2 = thePrefs.shareddir_list.GetHeadPosition();
			while (pos != NULL && pos2 != NULL) {
				const CString &str(m_strliSharedDirs.GetNext(pos));
				const CString &str2(thePrefs.shareddir_list.GetNext(pos2));
				if (str.CompareNoCase(str2) != 0) {
					bForce = true;
					break;
				}
			}
		}

		if (!bForce) {
			// check for changes in categories incoming dirs
			const CString &strMainIncDir(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
			CStringList strliFound;
			for (INT_PTR i = 0; i < thePrefs.GetCatCount(); ++i) {
				const Category_Struct *pCatStruct = thePrefs.GetCategory(i);
				if (pCatStruct != NULL) {
					CString strCatIncomingPath(pCatStruct->strIncomingPath);

					if (!strCatIncomingPath.IsEmpty() && strCatIncomingPath.CompareNoCase(strMainIncDir) != 0
						&& strliFound.Find(strCatIncomingPath) == NULL)
					{
						if (m_strliCatIncomingDirs.Find(strCatIncomingPath) == NULL) {
							bForce = true;
							break;
						}
						strliFound.AddTail(strCatIncomingPath);
					}
				}
			}
			if (strliFound.GetCount() != m_strliCatIncomingDirs.GetCount())
				bForce = true;

			// Additional bypass: detect subdirectory-level structure changes (under roots)
			if (!bForce) {
				DWORD curCrc = ComputeTreeStructureCRC();
				if (curCrc != m_dwLastTreeStructCRC)
					bForce = true;
			}
		}
	}
	if (bForce) {
		FetchSharedDirsList();
		FilterTreeReloadTree();
		if (m_bFileSystemRootDirty) {
			Expand(m_pRootUnsharedDirectries->m_htItem, TVE_COLLAPSE); // collapsing is enough to sync tree with the filter, as all items are recreated on every expanding
			m_bFileSystemRootDirty = false;
		}
	}
}

void CSharedDirsTreeCtrl::FetchSharedDirsList()
{
	RemoveAllSharedDirectories();

	if (thePrefs.GetAutoShareSubdirs()) {
		for (POSITION pos = thePrefs.shareddir_list.GetHeadPosition(); pos != NULL; ) {
			const CString& dir = thePrefs.shareddir_list.GetNext(pos);
			AddSharedDirectory(dir, true); // Recursively include subdirectories for the tree
		}
	} else
		m_strliSharedDirs.AddTail(&thePrefs.shareddir_list); // Preserve original non-recursive behavior
}

void CSharedDirsTreeCtrl::OnTvnBeginDrag(LPNMHDR pNMHDR, LRESULT *pResult)
{
	LPNMTREEVIEW lpnmtv = (LPNMTREEVIEW)pNMHDR;
	*pResult = 0;

	CDirectoryItem *pToDrag = reinterpret_cast<CDirectoryItem*>(lpnmtv->itemNew.lParam);
	if (pToDrag == NULL || pToDrag->m_eItemType != SDI_UNSHAREDDIRECTORY || FileSystemTreeIsShared(pToDrag->m_strFullPath))
		return;

	ASSERT(m_pDraggingItem == NULL);
	delete m_pDraggingItem;
	m_pDraggingItem = pToDrag->CloneContent(); // to be safe we store a copy, as items can be deleted when collapsing the tree etc

	CImageList *piml = CreateDragImage(lpnmtv->itemNew.hItem);
	if (piml == NULL)
		return;

	CPoint ptOffset;
	CRect rcItem;
	/* get the bounding rectangle of the item being dragged (rel to top-left of control) */
	if (GetItemRect(lpnmtv->itemNew.hItem, &rcItem, TRUE)) {
		/* get offset into image that the mouse is at */
		/* item rect doesn't include the image */
		int nX, nY;
		::ImageList_GetIconSize(piml->GetSafeHandle(), &nX, &nY);
		ptOffset = CPoint(lpnmtv->ptDrag) - rcItem.BottomRight() + POINT{ nX, nY };

		/* convert the item rect to screen co-ords for later use */
		MapWindowPoints(NULL, &rcItem);
	} else {
		GetWindowRect(&rcItem);
		ptOffset.x = ptOffset.y = 8;
	}

	if (piml->BeginDrag(0, ptOffset)) {
		CPoint ptDragEnter = lpnmtv->ptDrag;
		ClientToScreen(&ptDragEnter);
		piml->DragEnter(NULL, ptDragEnter);
	}
	delete piml;

	/* set the focus here, so we get a WM_CANCELMODE if needed */
	SetFocus();

	/* redraw item being dragged, otherwise it remains (looking) selected */
	InvalidateRect(&rcItem, TRUE);
	UpdateWindow();

	/* Hide the mouse cursor, and direct mouse input to this window */
	SetCapture();
}

void CSharedDirsTreeCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_pDraggingItem != NULL) {
		/* drag the item to the current position */
		CPoint pt = point;
		ClientToScreen(&pt);

		CImageList::DragMove(pt);
		CImageList::DragShowNolock(FALSE);
		LPCTSTR pCursor = IDC_NO;
		if (CWnd::WindowFromPoint(pt) == this) {
			TVHITTESTINFO tvhti;
			tvhti.pt = pt;
			ScreenToClient(&tvhti.pt);
			HTREEITEM hItemSel = HitTest(&tvhti);
			if (hItemSel != NULL) {
				CDirectoryItem *pDragTarget = reinterpret_cast<CDirectoryItem*>(GetItemData(hItemSel));
				//allow dragging only to shared folders
				if (pDragTarget != NULL && (pDragTarget->m_eItemType == SDI_DIRECTORY || pDragTarget->m_eItemType == SDI_NO)) {
					pCursor = IDC_ARROW;
					SelectDropTarget(pDragTarget->m_htItem);
				}
			}
		}
		SetCursor(AfxGetApp()->LoadStandardCursor(pCursor));

		CImageList::DragShowNolock(TRUE);
	}

	CTreeCtrl::OnMouseMove(nFlags, point);
}

void CSharedDirsTreeCtrl::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_pDraggingItem != NULL) {
		CPoint pt = point;
		ClientToScreen(&pt);

		TVHITTESTINFO tvhti;
		tvhti.pt = pt;
		ScreenToClient(&tvhti.pt);
						HTREEITEM hItemSel = HitTest(&tvhti);
		if (hItemSel != NULL) {
			CDirectoryItem *pDragTarget = reinterpret_cast<CDirectoryItem*>(GetItemData(hItemSel));
			//only allow dragging to shared folders
			if (pDragTarget && (pDragTarget->m_eItemType == SDI_DIRECTORY || pDragTarget->m_eItemType == SDI_NO)) {

				HTREEITEM htReal = m_pRootUnsharedDirectries->FindItem(m_pDraggingItem);
				// get the original drag src
				CDirectoryItem *pRealDragItem = htReal ? reinterpret_cast<CDirectoryItem*>(GetItemData(htReal)) : NULL;
				// if item was deleted - no problem as when we don't need to update the visible part
				// we can use the content copy just as well
				EditSharedDirectories(pRealDragItem ? pRealDragItem : m_pDraggingItem, true, false);
			}
		}

		CancelMode();
	}
	CTreeCtrl::OnLButtonUp(nFlags, point);
}

void CSharedDirsTreeCtrl::CancelMode()
{
	CImageList::DragLeave(NULL);
	CImageList::EndDrag();
	::ReleaseCapture();
	ShowCursor(TRUE);
	SelectDropTarget(NULL);

	delete m_pDraggingItem;
	m_pDraggingItem = NULL;
	RedrawWindow();
}

void CSharedDirsTreeCtrl::OnCancelMode()
{
	if (m_pDraggingItem != NULL)
		CancelMode();
	CTreeCtrl::OnCancelMode();
}

void CSharedDirsTreeCtrl::OnVolumesChanged()
{
	m_bFileSystemRootDirty = true;
}

bool CSharedDirsTreeCtrl::ShowFileSystemDirectory(const CString &strDir)
{
	// expand directories until we find our target directory and select it
	int iLen = strDir.GetLength();
	const CDirectoryItem *pCurrentItem = m_pRootUnsharedDirectries;
	for (bool bContinue = true; bContinue;) {
		bContinue = false;
		Expand(pCurrentItem->m_htItem, TVE_EXPAND);
		for (POSITION pos = pCurrentItem->liSubDirectories.GetHeadPosition(); pos != NULL;) {
			const CDirectoryItem *pTemp = pCurrentItem->liSubDirectories.GetNext(pos);
			if (EqualPaths(strDir, pTemp->m_strFullPath)) {
				Select(pTemp->m_htItem, TVGN_CARET);
				EnsureVisible(pTemp->m_htItem);
				return true;
			}
			int jLen = pTemp->m_strFullPath.GetLength();
			if (jLen < iLen && _tcsnicmp(strDir, pTemp->m_strFullPath, jLen) == 0) {
				pCurrentItem = pTemp;
				bContinue = true;
				break;
			}
		}
	}
	return false;
}

bool CSharedDirsTreeCtrl::ShowSharedDirectory(const CString &strDir)
{
	// expand directories until we find our target directory and select it
	for (POSITION pos = m_pRootDirectoryItem->liSubDirectories.GetHeadPosition(); pos != NULL;) {
		const CDirectoryItem *pTemp = m_pRootDirectoryItem->liSubDirectories.GetNext(pos);
		if (pTemp->m_eItemType == SDI_DIRECTORY) {
			Expand(pTemp->m_htItem, TVE_EXPAND);
			if (strDir.IsEmpty()) { // we want the parent item only
				Select(pTemp->m_htItem, TVGN_CARET);
				EnsureVisible(pTemp->m_htItem);
				return true;
			}
			// search for the fitting sub dir
			for (POSITION pos2 = pTemp->liSubDirectories.GetHeadPosition(); pos2 != NULL;) {
				CDirectoryItem *pTemp2 = pTemp->liSubDirectories.GetNext(pos2);
				if (strDir.CompareNoCase(pTemp2->m_strFullPath) == 0) {
					Select(pTemp2->m_htItem, TVGN_CARET);
					EnsureVisible(pTemp2->m_htItem);
					return true;
				}
			}
			return false;
		}
	}
	return false;
}

void CSharedDirsTreeCtrl::ShowAllSharedFiles()
{
	Select(GetRootItem(), TVGN_CARET);
	EnsureVisible(GetRootItem());
}