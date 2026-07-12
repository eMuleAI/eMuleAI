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
#include "emuledlg.h"
#include "CollectionViewDialog.h"
#include "Collection.h"
#include "CollectionFile.h"
#include "OtherFunctions.h"
#include "DownloadQueue.h"
#include "TransferDlg.h"
#include "CatDialog.h"
#include "SearchDlg.h"
#include "SearchList.h"
#include <vector>
#include <memory>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static void AppendCollectionDownloadSnapshot(const CCollectionFile *pFile, std::vector<CemuleApp::SDownloadFileSnapshot> &snapshots)
{
	if (pFile == NULL)
		return;

	CemuleApp::SDownloadFileSnapshot snapshot;
	snapshot.m_strFileName = pFile->GetFileName();
	snapshot.m_uFileSize = pFile->GetFileSize();
	memcpy(snapshot.m_abyFileHash, pFile->GetFileHash(), sizeof(snapshot.m_abyFileHash));
	if (pFile->GetFileIdentifierC().HasAICHHash())
		snapshot.m_strAICHHash = pFile->GetFileIdentifierC().GetAICHHash().GetString();
	snapshots.push_back(snapshot);
}

#define	PREF_INI_SECTION	_T("CollectionViewDlg")

IMPLEMENT_DYNAMIC(CCollectionViewDialog, CDialog)

BEGIN_MESSAGE_MAP(CCollectionViewDialog, CResizableDialog)
	ON_BN_CLICKED(IDC_VCOLL_CLOSE, OnBnClickedOk)
	ON_BN_CLICKED(IDC_VIEWCOLLECTIONDL, OnBnClickedViewCollection)
	ON_BN_CLICKED(IDC_VIEWCOLLECTIONDLSEL, OnBnClickedViewCollectionSelected)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_COLLECTIONVEWLIST, OnLvnItemChangedCollectionList)
	ON_NOTIFY(NM_DBLCLK, IDC_COLLECTIONVEWLIST, OnNmDblClkCollectionList)
END_MESSAGE_MAP()

CCollectionViewDialog::CCollectionViewDialog(CWnd *pParent /*=NULL*/)
	: CResizableDialog(CCollectionViewDialog::IDD, pParent)
	, m_pCollection()
	, m_icoWnd()
	, m_icoColl()
{
}

CCollectionViewDialog::~CCollectionViewDialog()
{
	if (m_icoWnd)
		VERIFY(::DestroyIcon(m_icoWnd));
	if (m_icoColl)
		VERIFY(::DestroyIcon(m_icoColl));
}

void CCollectionViewDialog::DoDataExchange(CDataExchange *pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_COLLECTIONVEWLIST, m_CollectionViewList);
	DDX_Control(pDX, IDC_COLLECTIONVIEWCATEGORYCHECK, m_AddNewCategory);
	DDX_Control(pDX, IDC_COLLECTIONVIEWLISTLABEL, m_CollectionViewListLabel);
	DDX_Control(pDX, IDC_COLLECTIONVIEWLISTICON, m_CollectionViewListIcon);
	DDX_Control(pDX, IDC_VIEWCOLLECTIONDL, m_CollectionDownload);
	DDX_Control(pDX, IDC_VIEWCOLLECTIONDLSEL, m_CollectionDownloadSelected);
	DDX_Control(pDX, IDC_VCOLL_CLOSE, m_CollectionExit);
	DDX_Control(pDX, IDC_COLLECTIONVIEWAUTHOR, m_CollectionViewAuthor);
	DDX_Control(pDX, IDC_COLLECTIONVIEWAUTHORKEY, m_CollectionViewAuthorKey);
}

void CCollectionViewDialog::SetCollection(CCollection *pCollection)
{
	if (!pCollection) {
		ASSERT(0);
		return;
	}
	m_pCollection = pCollection;
}

BOOL CCollectionViewDialog::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);

	if (!m_pCollection) {
		ASSERT(0);
		return TRUE;
	}

	m_CollectionViewList.Init(_T("CollectionView"));
	SetIcon(m_icoWnd = theApp.LoadIcon(_T("Collection_View")), FALSE);

	m_AddNewCategory.SetCheck(0);

	CString str;
	str.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("VIEWCOLLECTION")), (LPCTSTR)m_pCollection->m_sCollectionName);
	SetWindowText(str);

	m_icoColl = theApp.LoadIcon(_T("AABCollectionFileType"));
	m_CollectionViewListIcon.SetIcon(m_icoColl);
	m_CollectionDownload.SetWindowText(GetResString(_T("DOWNLOAD_ALL")));
	m_CollectionDownloadSelected.SetWindowText(GetResString(_T("DOWNLOAD_SELECTED")));
	m_CollectionExit.SetWindowText(GetResString(_T("CW_CLOSE")));
	SetDlgItemText(IDC_COLLECTIONVIEWAUTHORLABEL, GetResString(_T("AUTHOR")) + _T(':'));
	SetDlgItemText(IDC_COLLECTIONVIEWAUTHORKEYLABEL, GetResString(_T("AUTHORKEY")) + _T(':'));
	SetDlgItemText(IDC_COLLECTIONVIEWCATEGORYCHECK, GetResString(_T("COLL_ADDINCAT")));
	SetDlgItemText(IDC_VCOLL_DETAILS, GetResString(_T("DETAILS")));
	SetDlgItemText(IDC_VCOLL_OPTIONS, GetResString(_T("OPTIONS")));

	m_CollectionViewAuthor.SetWindowText(m_pCollection->m_sCollectionAuthorName);
	m_CollectionViewAuthorKey.SetWindowText(m_pCollection->GetAuthorKeyHashString());

	AddOrReplaceAnchor(this, IDC_COLLECTIONVEWLIST, TOP_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_VCOLL_DETAILS, BOTTOM_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_VCOLL_OPTIONS, BOTTOM_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_COLLECTIONVIEWAUTHORLABEL, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, IDC_COLLECTIONVIEWAUTHORKEYLABEL, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, IDC_COLLECTIONVIEWCATEGORYCHECK, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, IDC_COLLECTIONVIEWAUTHOR, BOTTOM_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_COLLECTIONVIEWAUTHORKEY, BOTTOM_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_VCOLL_CLOSE, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_VIEWCOLLECTIONDL, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_VIEWCOLLECTIONDLSEL, BOTTOM_RIGHT);
	EnableSaveRestore(PREF_INI_SECTION);

	std::vector<CAbstractFile*> aCollectionFiles;
	aCollectionFiles.reserve(static_cast<size_t>(m_pCollection->m_CollectionFilesMap.GetCount()));
	for (CCollectionFilesMap::CPair *pair = m_pCollection->m_CollectionFilesMap.PGetFirstAssoc(); pair != NULL; pair = m_pCollection->m_CollectionFilesMap.PGetNextAssoc(pair))
		aCollectionFiles.push_back(pair->value);

	m_CollectionViewList.SetRedraw(false);
	m_CollectionViewList.SetVirtualFiles(aCollectionFiles);
	if (!aCollectionFiles.empty())
		m_CollectionViewList.SetSelectionMark(0);
	m_CollectionViewList.SetRedraw(true);
	m_CollectionViewList.Invalidate(FALSE);

	CString strTitle;
	strTitle.Format(_T("%s (%d)"), (LPCTSTR)GetResString(_T("COLLECTIONLIST")), m_CollectionViewList.GetItemCount());
	m_CollectionViewListLabel.SetWindowText(strTitle);
	UpdateDownloadSelectedButtonState();

	return TRUE;
}

void CCollectionViewDialog::OnNmDblClkCollectionList(LPNMHDR, LRESULT *pResult)
{
	DownloadSelected();
	*pResult = 0;
}

void CCollectionViewDialog::OnLvnItemChangedCollectionList(LPNMHDR, LRESULT *pResult)
{
	UpdateDownloadSelectedButtonState();
	*pResult = 0;
}

void CCollectionViewDialog::DownloadAll()
{
	StartDownload(false);
}

void CCollectionViewDialog::DownloadSelected()
{
	StartDownload(true);
}

void CCollectionViewDialog::StartDownload(bool bSelectedOnly)
{
	std::shared_ptr<std::vector<CemuleApp::SDownloadFileSnapshot> > pSnapshots(new std::vector<CemuleApp::SDownloadFileSnapshot>());
	if (bSelectedOnly) {
		const UINT uSelectedCount = m_CollectionViewList.GetSelectedCount();
		if (uSelectedCount == 0)
			return;
		pSnapshots->reserve(uSelectedCount);
		for (POSITION pos = m_CollectionViewList.GetFirstSelectedItemPosition(); pos != NULL;) {
			const int index = m_CollectionViewList.GetNextSelectedItem(pos);
			if (index >= 0)
				AppendCollectionDownloadSnapshot(reinterpret_cast<CCollectionFile*>(m_CollectionViewList.GetItemData(index)), *pSnapshots);
		}
	} else {
		if (m_pCollection == NULL || m_pCollection->m_CollectionFilesMap.IsEmpty())
			return;
		pSnapshots->reserve(static_cast<size_t>(m_pCollection->m_CollectionFilesMap.GetCount()));
		for (CCollectionFilesMap::CPair *pair = m_pCollection->m_CollectionFilesMap.PGetFirstAssoc(); pair != NULL; pair = m_pCollection->m_CollectionFilesMap.PGetNextAssoc(pair))
			AppendCollectionDownloadSnapshot(pair->value, *pSnapshots);
	}

	if (pSnapshots->empty())
		return;

	int iNewIndex = 0;
	for (INT_PTR iIndex = thePrefs.GetCatCount(); --iIndex > 0;)
		if (!m_pCollection->m_sCollectionName.CompareNoCase(thePrefs.GetCategory(iIndex)->strTitle)) {
			iNewIndex = (int)iIndex;
			break;
		}

	if (m_AddNewCategory.GetCheck() && !iNewIndex) {
		iNewIndex = theApp.emuledlg->transferwnd->AddCategory(m_pCollection->m_sCollectionName, thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR), EMPTY, EMPTY, true);
		theApp.emuledlg->searchwnd->UpdateCatTabs();
	}

	theApp.AddFileSnapshotsToDownload(pSnapshots, iNewIndex);
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
}

void CCollectionViewDialog::UpdateDownloadSelectedButtonState()
{
	if (::IsWindow(m_CollectionDownloadSelected.GetSafeHwnd()))
		m_CollectionDownloadSelected.EnableWindow(m_CollectionViewList.GetSelectedCount() > 0);
}

void CCollectionViewDialog::OnBnClickedViewCollection()
{
	DownloadAll();
	OnBnClickedOk();
}

void CCollectionViewDialog::OnBnClickedViewCollectionSelected()
{
	DownloadSelected();
	OnBnClickedOk();
}

void CCollectionViewDialog::OnBnClickedOk()
{
	OnOK();
}