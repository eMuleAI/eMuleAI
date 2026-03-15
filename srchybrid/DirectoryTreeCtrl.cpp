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
#include "DirectoryTreeCtrl.h"
#include "MenuCmds.h"
#include "otherfunctions.h"
#include "Preferences.h"
#include "eMuleAI/MenuXP.h"
#include "UserMsgs.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


/////////////////////////////////////////////
// written by robert rostek - tecxx@rrs.at //
/////////////////////////////////////////////

struct STreeItem
{
	CString strPath;
};


// CDirectoryTreeCtrl

IMPLEMENT_DYNAMIC(CDirectoryTreeCtrl, CTreeCtrl)

BEGIN_MESSAGE_MAP(CDirectoryTreeCtrl, CTreeCtrl)
	ON_NOTIFY_REFLECT(TVN_ITEMEXPANDING, OnTvnItemexpanding)
	ON_NOTIFY_REFLECT(TVN_GETDISPINFO, OnTvnGetdispinfo)
	ON_WM_LBUTTONDOWN()
	ON_NOTIFY_REFLECT(TVN_DELETEITEM, OnTvnDeleteItem)
	ON_WM_CONTEXTMENU()
	ON_WM_RBUTTONDOWN()
	ON_WM_KEYDOWN()
	ON_WM_CHAR()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CDirectoryTreeCtrl::~CDirectoryTreeCtrl()
{
	// don't destroy the system's image list
	m_images.Detach();
}

void CDirectoryTreeCtrl::OnDestroy()
{
	// If a treeview control is created with TVS_CHECKBOXES, the application has to
	// delete the image list which was implicitly created by the control.
	CImageList *piml = GetImageList(TVSIL_STATE);
	if (piml)
		piml->DeleteImageList();

	CTreeCtrl::OnDestroy();
}

void CDirectoryTreeCtrl::OnTvnItemexpanding(LPNMHDR pNMHDR, LRESULT *pResult)
{
	CWaitCursor curWait;
	SetRedraw(FALSE);

	LPNMTREEVIEW pNMTreeView = reinterpret_cast<LPNMTREEVIEW>(pNMHDR);
	HTREEITEM hItem = pNMTreeView->itemNew.hItem;
	// remove all sub-items
	HTREEITEM hRemove;
	while ((hRemove = GetChildItem(hItem)) != NULL)
		DeleteItem(hRemove);

	// fetch all subdirectories and add them to the node
	AddSubdirectories(hItem, GetFullPath(hItem));

	SetRedraw(TRUE);
	Invalidate();
	*pResult = 0;
}

void CDirectoryTreeCtrl::ShareSubDirTree(HTREEITEM hItem, BOOL bRecurse)
{
	CWaitCursor curWait;
	SetRedraw(FALSE);
	BOOL bCheck = !GetCheck(hItem);
	HTREEITEM hItemVisibleItem = GetFirstVisibleItem();
	CheckChanged(hItem, bCheck);
	if (bRecurse) {
		Expand(hItem, TVE_TOGGLE);
		for (HTREEITEM hChild = GetChildItem(hItem); hChild != NULL; hChild = GetNextSiblingItem(hChild))
			MarkChildren(hChild, bCheck);
		Expand(hItem, TVE_TOGGLE);
	}
	if (hItemVisibleItem)
		SelectSetFirstVisible(hItemVisibleItem);

	SetRedraw(TRUE);
	Invalidate();
}
void CDirectoryTreeCtrl::AddDirectory(const CString &strDir)
{
	m_lstShared.AddTail(strDir);
	if (::PathIsUNC(strDir)) {
		const CString &sShare(GetShareName(strDir));
		INT_PTR i = m_aUNCshares.GetCount();
		if (!i)
			m_aUNCshares.Add(sShare);
		else
			while (--i >= 0) {
				int cmp = sShare.CompareNoCase(m_aUNCshares[i]);
				if (cmp >= 0) {
					if (cmp)
						m_aUNCshares.InsertAt(i + 1, sShare);
					break;
				}
			}
	}
}

void CDirectoryTreeCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	//VQB adjustments to provide for sharing or unsharing of subdirectories when control key is Down
	UINT uHitFlags;
	HTREEITEM hItem = HitTest(point, &uHitFlags);
	if (hItem && (uHitFlags & TVHT_ONITEMSTATEICON))
		ShareSubDirTree(hItem, nFlags & MK_CONTROL);
	CTreeCtrl::OnLButtonDown(nFlags, point);
}

void CDirectoryTreeCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar == VK_SPACE) {
		HTREEITEM hItem = GetSelectedItem();
		if (hItem) {
			ShareSubDirTree(hItem, GetKeyState(VK_CONTROL) < 0);

			// if Ctrl+Space is passed to the tree control, it just beeps and does not check/uncheck the hItem!
			SetCheck(hItem, !GetCheck(hItem));
			return;
		}
	}

	CTreeCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CDirectoryTreeCtrl::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	// If we let any keystrokes which are handled by us - but not by the tree control -
	// be passed to the control, the user will hear a system event sound (Standard Error!)
	if (GetKeyState(VK_CONTROL) >= 0 || nChar != VK_SPACE)
		CTreeCtrl::OnChar(nChar, nRepCnt, nFlags);
}

void CDirectoryTreeCtrl::MarkChildren(HTREEITEM hChild, bool mark)
{
	CheckChanged(hChild, mark);
	SetCheck(hChild, mark);
	Expand(hChild, TVE_TOGGLE); // VQB - make sure tree has entries
	for (HTREEITEM hChild2 = GetChildItem(hChild); hChild2 != NULL; hChild2 = GetNextSiblingItem(hChild2))
		MarkChildren(hChild2, mark);

	Expand(hChild, TVE_TOGGLE); // VQB - restore tree to initial disposition
}

void CDirectoryTreeCtrl::OnTvnGetdispinfo(LPNMHDR pNMHDR, LRESULT *pResult)
{
	reinterpret_cast<LPNMTVDISPINFO>(pNMHDR)->item.cChildren = 1;
	*pResult = 0;
}

void CDirectoryTreeCtrl::Init()
{
	// Win98: Explicitly set to Unicode to receive Unicode notifications.
	SendMessage(CCM_SETUNICODEFORMAT, TRUE);

	ShowWindow(SW_HIDE);
	DeleteAllItems();

	// START: added by FoRcHa /////////////
	SHFILEINFO shFinfo;

	// Get the system image list using a "path" which is available on all systems. [patch by bluecow]
	HIMAGELIST hImgList = (HIMAGELIST)::SHGetFileInfo(_T("."), 0, &shFinfo, sizeof(shFinfo), SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
	if (!hImgList) {
		TRACE(_T("Cannot retrieve the Handle of SystemImageList!"));
		//return;
	}

	m_images.m_hImageList = hImgList;
	SetImageList(&m_images, TVSIL_NORMAL);
	////////////////////////////////

	TCHAR drivebuffer[500];
	DWORD dwRet = GetLogicalDriveStrings(_countof(drivebuffer) - 1, drivebuffer);
	if (dwRet > 0 && dwRet < _countof(drivebuffer))

		for (LPCTSTR pos = drivebuffer; *pos != _T('\0');) {
			// Copy drive name
			TCHAR drive[4];
			_tcsncpy(drive, pos, _countof(drive));
			drive[2] = _T('\0');
			AddChildItem(NULL, drive); // e.g. "C:"

			// Point to the next drive
			pos += _tcslen(pos) + 1;
		}
	
	for (INT_PTR i = 0; i < m_aUNCshares.GetCount(); ++i) {
		const CString &sUNC(m_aUNCshares[i]);
		AddChildItem(NULL, sUNC.Left(sUNC.GetLength() - 1));
	}

	ShowWindow(SW_SHOW);
}

HTREEITEM CDirectoryTreeCtrl::AddChildItem(HTREEITEM hRoot, const CString &strText)
{
	CString strDir(GetFullPath(hRoot));
	ASSERT(strDir.IsEmpty() || strDir.Right(1) == _T("\\"));
	strDir += strText;
	slosh(strDir);

	TVINSERTSTRUCT itInsert = {};
	itInsert.hParent = hRoot;
	itInsert.hInsertAfter = hRoot ? TVI_SORT : TVI_LAST;
	itInsert.item.pszText = const_cast<LPTSTR>((LPCTSTR)strText);

	// START: changed by FoRcHa /////
	itInsert.item.mask = TVIF_CHILDREN | TVIF_HANDLE | TVIF_TEXT | TVIF_STATE | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
	itInsert.item.stateMask = TVIS_BOLD | TVIS_STATEIMAGEMASK;
	// END: changed by FoRcHa ///////

	itInsert.item.state = HasSharedSubdirectory(strDir) ? TVIS_BOLD : 0;
	// used to display [+] and [-] buttons next to items
	itInsert.item.cChildren = HasSubdirectories(strDir) ? I_CHILDRENCALLBACK : 0;

	// START: added by FoRcHa ////////////////
	UINT nType = ::GetDriveType(strDir);
	if (DRIVE_REMOVABLE <= nType && nType <= DRIVE_RAMDISK)
		itInsert.item.iImage = nType;

	SHFILEINFO shFinfo;
	shFinfo.szDisplayName[0] = _T('\0');
	if (::SHGetFileInfo(strDir, 0, &shFinfo, sizeof(shFinfo), SHGFI_SMALLICON | SHGFI_ICON | SHGFI_OPENICON | SHGFI_DISPLAYNAME)) {
		itInsert.itemex.iImage = shFinfo.iIcon;
		::DestroyIcon(shFinfo.hIcon);
		if (hRoot == NULL && shFinfo.szDisplayName[0] != _T('\0')) {
			STreeItem *pti = new STreeItem;
			pti->strPath = strText;
			itInsert.item.pszText = shFinfo.szDisplayName;
			itInsert.item.mask |= TVIF_PARAM;
			itInsert.item.lParam = (LPARAM)pti;
		}

	} else {
		TRACE(_T("Error getting SystemFileInfo!"));
		itInsert.itemex.iImage = 0; // :(
	}
	// END: added by FoRcHa //////////////

	HTREEITEM hItem = InsertItem(&itInsert);
	if (IsShared(strDir))
		SetCheck(hItem);

	return hItem;
}

CString CDirectoryTreeCtrl::GetFullPath(HTREEITEM hItem)
{
	CString strDir;
	for (HTREEITEM hSearchItem = hItem; hSearchItem != NULL; hSearchItem = GetParentItem(hSearchItem)) {
		const STreeItem *pti = reinterpret_cast<STreeItem*>(GetItemData(hSearchItem));
		strDir.Insert(0, _T('\\')); //trailing backslash
		strDir.Insert(0, pti ? pti->strPath : GetItemText(hSearchItem));
	}
	return strDir;
}

void CDirectoryTreeCtrl::AddSubdirectories(HTREEITEM hRoot, const CString &strDir)
{
	ASSERT(strDir.Right(1) == _T("\\"));
	CFileFind finder;
	for (BOOL bFound = finder.FindFile(strDir + _T("*.*")); bFound;) {
		bFound = finder.FindNextFile();
		if (finder.IsDirectory() && !finder.IsDots() && !finder.IsSystem()) {
			CString strFilename(finder.GetFileName());
			int i = strFilename.ReverseFind(_T('\\'));
			if (i >= 0)
				strFilename.Delete(0, i + 1);
			AddChildItem(hRoot, strFilename);
		}
	}
}

bool CDirectoryTreeCtrl::HasSubdirectories(const CString &strDir)
{
	return ::HasSubdirectories(strDir);
}

void CDirectoryTreeCtrl::GetSharedDirectories(CStringList &list)
{
	list.AddTail(&m_lstShared);
}

void CDirectoryTreeCtrl::SetSharedDirectories(CStringList &list)
{
	m_lstShared.RemoveAll();

	for (POSITION pos = list.GetHeadPosition(); pos != NULL;)
		AddDirectory(list.GetNext(pos));
	Init();
	}
bool CDirectoryTreeCtrl::AddUNCShare(const CString &strDir)
{
	ASSERT(strDir.Right(1) == _T('\\'));
	if (IsShared(strDir) || !thePrefs.IsShareableDirectory(strDir))
		return false;
	AddDirectory(strDir);
	Init();
	return true;
}

bool CDirectoryTreeCtrl::HasSharedSubdirectory(const CString &strDir)
{
	int iLen = strDir.GetLength();
	ASSERT(iLen > 0);
	bool bSlosh = (strDir[iLen - 1] == _T('\\'));
	for (POSITION pos = m_lstShared.GetHeadPosition(); pos != NULL;) {
		const CString &sDir(m_lstShared.GetNext(pos));
		if (_tcsnicmp(sDir, strDir, iLen) == 0 && iLen < sDir.GetLength() && (bSlosh || sDir[iLen] == _T('\\')))
			return true;
	}
	return false;
}

void CDirectoryTreeCtrl::CheckChanged(HTREEITEM hItem, bool bChecked)
{
	const CString &strDir(GetFullPath(hItem));
	if (bChecked)
		AddShare(strDir);
	else
		RemShare(strDir);

	if (thePrefs.GetAutoShareSubdirs()) {
		for (HTREEITEM hChild = GetChildItem(hItem); hChild != NULL; hChild = GetNextSiblingItem(hChild))
			MarkChildren(hChild, bChecked);	// Updates list and visual tick
	}

	UpdateParentItems(hItem);
	GetParent()->SendMessage(WM_COMMAND, UM_ITEMSTATECHANGED, reinterpret_cast<LPARAM>(m_hWnd));
}

bool CDirectoryTreeCtrl::IsShared(const CString& strDir)
{
	for (POSITION pos = m_lstShared.GetHeadPosition(); pos != NULL;) {
		const CString& sShare = m_lstShared.GetNext(pos);
		if (EqualPaths(sShare, strDir))
			return true;
		// Treat sub-directories as shared when the option is enabled
		if (thePrefs.GetAutoShareSubdirs() && IsSubDirectoryOf(strDir, sShare))
			return true;
	}

	return false;
}

void CDirectoryTreeCtrl::AddShare(const CString &strDir)
{
	ASSERT(strDir.Right(1) == _T('\\'));
	if (!IsShared(strDir) && thePrefs.IsShareableDirectory(strDir))
		m_lstShared.AddTail(strDir);
}

void CDirectoryTreeCtrl::RemShare(const CString &strDir)
{
	ASSERT(strDir.Right(1) == _T('\\'));
	for (POSITION pos = m_lstShared.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		if (EqualPaths(m_lstShared.GetNext(pos), strDir)) {
			m_lstShared.RemoveAt(pos2);
			break;
		}
	}
}

void CDirectoryTreeCtrl::UpdateParentItems(HTREEITEM hChild)
{
	HTREEITEM hSearch = hChild;
	while ((hSearch = GetParentItem(hSearch)) != NULL)
		SetItemState(hSearch, (HasSharedSubdirectory(GetFullPath(hSearch)) ? TVIS_BOLD : 0), TVIS_BOLD);
}

void CDirectoryTreeCtrl::OnContextMenu(CWnd*, CPoint point)
{
	if (!PointInClient(*this, point)) {
		Default();
		return;
	}

	const HTREEITEM hItem = GetSelectedItem();
	if (!hItem)
		return;
	const CString &sItem(GetFullPath(hItem)); //trailing backslash


	// create the menu
	CMenuXP SharedMenu;
	SharedMenu.CreatePopupMenu();
	SharedMenu.AddMenuSidebar(GetResString(_T("SHAREDFOLDERS")));
	SharedMenu.AppendMenu(MF_STRING, MP_OPENFOLDER, GetResString(_T("OPENFOLDER")));
	SharedMenu.AppendMenu(MF_STRING | MF_SEPARATOR);

	if (IsShared(sItem)) {
		SharedMenu.AppendMenu(MF_STRING, MP_UNSHAREDIR, GetResString(_T("UNSHAREDIR")));
		if (!thePrefs.GetAutoShareSubdirs())
			SharedMenu.AppendMenu(MF_STRING, MP_UNSHAREDIRSUB, GetResString(_T("UNSHAREDIRSUB")));
		if (PathIsUNC(sItem) && !GetParentItem(hItem)) {
	CString sViewPath;
			sViewPath.Format(_T("%s %s"), (LPCTSTR)GetResString(_T("REMOVETHIS")), (LPCTSTR)sItem);

			SharedMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
			SharedMenu.AppendMenu(MF_STRING, MP_REMOVESHARE, sViewPath);
		}
	} else {
		SharedMenu.AppendMenu(MF_STRING, MP_SHAREDIR, GetResString(_T("SHAREDIR")));
		if (!thePrefs.GetAutoShareSubdirs())
			SharedMenu.AppendMenu(MF_STRING, MP_SHAREDIRSUB, GetResString(_T("SHAREDIRSUB")));
	}

	// display menu
	GetPopupMenuPos(*this, point);
	SharedMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
	VERIFY(SharedMenu.DestroyMenu());
}

void CDirectoryTreeCtrl::OnRButtonDown(UINT, CPoint point)
{
	UINT uHitFlags;
	HTREEITEM hItem = HitTest(point, &uHitFlags);
	if (hItem != NULL && (uHitFlags & TVHT_ONITEM)) {
		Select(hItem, TVGN_CARET);
		SetItemState(hItem, TVIS_SELECTED, TVIS_SELECTED);
	}
}

BOOL CDirectoryTreeCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	const HTREEITEM hItem = GetSelectedItem();
	if (!hItem)
		return TRUE;
	const CString &sItem(GetFullPath(hItem));
	switch (wParam) {
	case MP_OPENFOLDER:
		ShellOpenFile(sItem);
		break;
	case MP_SHAREDIR:
	case MP_UNSHAREDIR:
		if (!thePrefs.GetAutoShareSubdirs()) {
			CheckChanged(hItem, wParam == MP_SHAREDIR);
			SetCheck(hItem, wParam == MP_SHAREDIR);
			break;
		}
	case MP_SHAREDIRSUB:
	case MP_UNSHAREDIRSUB:
		ShareSubDirTree(hItem, TRUE);
		SetCheck(hItem, wParam == MP_SHAREDIRSUB);
		break;
	case MP_REMOVESHARE:
		{
			DeleteItem(hItem);
			int iLen = sItem.GetLength();
			for (POSITION pos = m_lstShared.GetHeadPosition(); pos != NULL;) {
				POSITION pos2 = pos;
				const CString &sDir(m_lstShared.GetNext(pos));
				if (_tcsnicmp(sDir, sItem, iLen) == 0)
					m_lstShared.RemoveAt(pos2);
			}
			for (INT_PTR i = m_aUNCshares.GetCount(); --i >= 0;)
				if (_tcsnicmp(m_aUNCshares[i], sItem, iLen) == 0) {
					m_aUNCshares.RemoveAt(i);
					break;
				}
			static_cast<CPropertyPage*>(GetParent())->SetModified();
		}
	}

	return TRUE;
}

void CDirectoryTreeCtrl::OnTvnDeleteItem(LPNMHDR pNMHDR, LRESULT *pResult)
{
	LPNMTREEVIEW pNMTreeView = reinterpret_cast<LPNMTREEVIEW>(pNMHDR);
	if (pNMTreeView->itemOld.lParam)
		delete reinterpret_cast<STreeItem*>(pNMTreeView->itemOld.lParam);
	*pResult = 0;
}