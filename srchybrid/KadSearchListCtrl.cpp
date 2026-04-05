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
#include "KademliaWnd.h"
#include "KadSearchListCtrl.h"
#include "KadContactListCtrl.h"
#include "Ini2.h"
#include "OtherFunctions.h"
#include "emuledlg.h"
#include "DownloadQueue.h"
#include "PartFile.h"
#include "kademlia/kademlia/search.h"
#include "kademlia/utils/LookupHistory.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


// CKadSearchListCtrl

IMPLEMENT_DYNAMIC(CKadSearchListCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CKadSearchListCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_WM_SYSCOLORCHANGE()
END_MESSAGE_MAP()

CKadSearchListCtrl::CKadSearchListCtrl()
{
	SetGeneralPurposeFind(true);
	SetSkinKey(_T("KadActionsLv"));
}

void CKadSearchListCtrl::Init()
{
	SetPrefsKey(_T("KadSearchListCtrl"));
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	// Alignment rule: left for text, dates, and status labels; right for sizes, rates, counts, durations, and percentages.
	InsertColumn(colNum,			EMPTY,	LVCFMT_RIGHT,	60);						//NUMBER
	InsertColumn(colKey,			EMPTY,	LVCFMT_LEFT,	DFLT_HASH_COL_WIDTH);		//KEY
	InsertColumn(colType,			EMPTY,	LVCFMT_LEFT,	100);						//TYPE
	InsertColumn(colName,			EMPTY,	LVCFMT_LEFT,	DFLT_FILENAME_COL_WIDTH);	//SW_NAME
	InsertColumn(colStop,			EMPTY,	LVCFMT_LEFT,	100);						//STATUS
	InsertColumn(colLoad,			EMPTY,	LVCFMT_RIGHT,	100);						//THELOAD
	InsertColumn(colPacketsSent,	EMPTY,	LVCFMT_RIGHT,	100);						//(PACKSENT
	InsertColumn(colResponses,		EMPTY,	LVCFMT_RIGHT,	100);						//RESPONSES

	SetAllIcons();
	Localize();

	LoadSettings();
	SetSortArrow();
	SortItems(SortProc, MAKELONG(GetSortItem(), !GetSortAscending()));
}

void CKadSearchListCtrl::UpdateKadSearchCount()
{
	CString id(GetResString(_T("KADSEARCHLAB")));
	id.AppendFormat(_T(" (%i)"), GetItemCount());
	theApp.emuledlg->kademliawnd->SetDlgItemText(IDC_KADSEARCHLAB, id);
}

void CKadSearchListCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
	SetAllIcons();
}

void CKadSearchListCtrl::SetAllIcons()
{
	CImageList iml;
	iml.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	iml.Add(CTempIconLoader(_T("KadFileSearch")));
	iml.Add(CTempIconLoader(_T("KadWordSearch")));
	iml.Add(CTempIconLoader(_T("KadNodeSearch")));
	iml.Add(CTempIconLoader(_T("KadStoreFile")));
	iml.Add(CTempIconLoader(_T("KadStoreWord")));
	HIMAGELIST himl = ApplyImageList(iml.Detach());
	if (himl)
		::ImageList_Destroy(himl);
}

void CKadSearchListCtrl::Localize()
{
	static const LPCTSTR uids[8] =
	{
		_T("NUMBER"), _T("KEY"), _T("TYPE"), _T("SW_NAME"), _T("STATUS"),
		_T("THELOAD"), _T("PACKSENT"), _T("RESPONSES")
	};

	LocaliseHeaderCtrl(uids, _countof(uids));

	for (int i = GetItemCount(); --i >= 0;)
		SearchRef(reinterpret_cast<Kademlia::CSearch*>(GetItemData(i)));

	UpdateKadSearchCount();
}

void CKadSearchListCtrl::UpdateSearch(int iItem, const Kademlia::CSearch *search)
{
	CString id;
	id.Format(_T("%u"), search->GetSearchID());
	SetItemText(iItem, colNum, id);

	int nImage;
	uint32 uType = search->GetSearchType();
	switch (uType) {
	case Kademlia::CSearch::FILE:
		nImage = 0;
		break;
	case Kademlia::CSearch::KEYWORD:
		nImage = 1;
		break;
	case Kademlia::CSearch::NODE:
	case Kademlia::CSearch::NODECOMPLETE:
	case Kademlia::CSearch::NODESPECIAL:
	case Kademlia::CSearch::NODEFWCHECKUDP:
		nImage = 2;
		break;
	case Kademlia::CSearch::STOREFILE:
		nImage = 3;
		break;
	case Kademlia::CSearch::STOREKEYWORD:
		nImage = 4;
		break;
	default:
		nImage = -1; //none
	}
	//JOHNTODO: -
	//I also need to understand skinning so the icons are done correctly.
	if (nImage >= 0)
		SetItem(iItem, 0, LVIF_IMAGE, 0, nImage, 0, 0, 0, 0);

#ifndef _DEBUG
	SetItemText(iItem, colType, Kademlia::CSearch::GetTypeName(uType));
#else
	id.Format(_T("%s (%u)"), (LPCTSTR)Kademlia::CSearch::GetTypeName(uType), uType);
	SetItemText(iItem, colType, id);
#endif
	SetItemText(iItem, colName, (CString)search->GetGUIName());

	if (search->GetTarget() != NULL) {
		search->GetTarget().ToHexString(id);
		SetItemText(iItem, colKey, id);
	}

	SetItemText(iItem, colStop, GetResString(search->Stoping() ? _T("KADSTATUS_STOPPING") : _T("KADSTATUS_ACTIVE")));

	id.Format(_T("%u (%u|%u)"), search->GetNodeLoad(), search->GetNodeLoadResponse(), search->GetNodeLoadTotal());
	SetItemText(iItem, colLoad, id);

	id.Format(_T("%u"), search->GetAnswers());
	SetItemText(iItem, colResponses, id);

	id.Format(_T("%u|%u"), search->GetKadPacketSent(), search->GetRequestAnswer());
	SetItemText(iItem, colPacketsSent, id);
}

void CKadSearchListCtrl::SearchAdd(const Kademlia::CSearch *search)
{
	try {
		ASSERT(search != NULL);
		int iItem = InsertItem(LVIF_TEXT | LVIF_PARAM, GetItemCount(), EMPTY, 0, 0, 0, (LPARAM)search);
		if (iItem >= 0) {
			UpdateSearch(iItem, search);
			UpdateKadSearchCount();
		}
	} catch (...) {
		ASSERT(0);
	}
}

void CKadSearchListCtrl::SearchRem(const Kademlia::CSearch *search)
{
	try {
		ASSERT(search != NULL);
		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)search;
		int iItem = FindItem(&find);
		if (iItem >= 0) {
			DeleteItem(iItem);
			UpdateKadSearchCount();
		}
	} catch (...) {
		ASSERT(0);
	}
}

void CKadSearchListCtrl::SearchRef(const Kademlia::CSearch *search)
{
	try {
		ASSERT(search != NULL);
		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)search;
		int iItem = FindItem(&find);
		if (iItem >= 0)
			UpdateSearch(iItem, search);
	} catch (...) {
		ASSERT(0);
	}
}

BOOL CKadSearchListCtrl::OnCommand(WPARAM, LPARAM)
{
	// ???
	return TRUE;
}

void CKadSearchListCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	// Determine ascending based on whether already sorted on this column
	bool bSortAscending = (GetSortItem() != pNMLV->iSubItem) || !GetSortAscending();

	// Item is the clicked column
	int iSortItem = pNMLV->iSubItem;

	// Sort table
	UpdateSortHistory(MAKELONG(iSortItem, !bSortAscending));
	SetSortArrow(iSortItem, bSortAscending);
	SortItems(SortProc, MAKELONG(iSortItem, !bSortAscending));
	*pResult = 0;
}

int CALLBACK CKadSearchListCtrl::SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort)
{
	const Kademlia::CSearch *item1 = reinterpret_cast<Kademlia::CSearch*>(lParam1);
	const Kademlia::CSearch *item2 = reinterpret_cast<Kademlia::CSearch*>(lParam2);
	if (item1 == NULL || item2 == NULL)
		return 0;

	int iResult;
	switch (LOWORD(lParamSort)) {
	case colNum:
		iResult = CompareUnsigned(item1->GetSearchID(), item2->GetSearchID());
		break;
	case colKey:
		if (item1->GetTarget() == NULL && item2->GetTarget() == NULL)
			iResult = 0;
		else if (item1->GetTarget() != NULL && item2->GetTarget() == NULL)
			iResult = -1;
		else if (item1->GetTarget() == NULL && item2->GetTarget() != NULL)
			iResult = 1;
		else
			iResult = item1->GetTarget().CompareTo(item2->GetTarget());
		break;
	case colType:
		iResult = item1->GetSearchType() - item2->GetSearchType();
		break;
	case colName:
		iResult = CompareLocaleStringNoCaseW(item1->GetGUIName(), item2->GetGUIName());
		break;
	case colStop:
		iResult = (int)item1->Stoping() - (int)item2->Stoping();
		break;
	case colLoad:
		iResult = CompareUnsigned(item1->GetNodeLoad(), item2->GetNodeLoad());
		break;
	case colPacketsSent:
		iResult = CompareUnsigned(item1->GetKadPacketSent(), item2->GetKadPacketSent());
		break;
	case colResponses:
		iResult = CompareUnsigned(item1->GetAnswers(), item2->GetAnswers());
		break;
	default:
		return 0;
	}
	return HIWORD(lParamSort) ? -iResult : iResult;
}

Kademlia::CLookupHistory* CKadSearchListCtrl::FetchAndSelectActiveSearch(bool bMark)
{
	int iIntrestingItem = -1;
	int iItem = -1;

	for (int i = GetItemCount(); --i >= 0;) {
		const Kademlia::CSearch *pSearch = (Kademlia::CSearch*)GetItemData(i);
		if (pSearch != NULL && !pSearch->GetLookupHistory()->IsSearchStopped() && !pSearch->GetLookupHistory()->IsSearchDeleted()) {
			// prefer interesting search rather than node searches
			switch (pSearch->GetSearchType()) {
			case Kademlia::CSearch::FILE:
			case Kademlia::CSearch::KEYWORD:
			case Kademlia::CSearch::STORENOTES:
			case Kademlia::CSearch::NOTES:
			case Kademlia::CSearch::STOREFILE:
			case Kademlia::CSearch::STOREKEYWORD:
				iIntrestingItem = i;
				break;
			case Kademlia::CSearch::NODE:
			case Kademlia::CSearch::NODECOMPLETE:
			case Kademlia::CSearch::NODESPECIAL:
			case Kademlia::CSearch::NODEFWCHECKUDP:
			case Kademlia::CSearch::FINDSERVINGBUDDY:
			default:
				if (iItem == -1)
					iItem = i;
			}
			if (iIntrestingItem >= 0)
				break;
		}
	}
	if (iIntrestingItem >= 0) {
		if (bMark)
			SetItemState(iIntrestingItem, LVIS_SELECTED, LVIS_SELECTED);
		return ((Kademlia::CSearch*)GetItemData(iIntrestingItem))->GetLookupHistory();
	}
	if (iItem >= 0) {
		if (bMark)
			SetItemState(iItem, LVIS_SELECTED, LVIS_SELECTED);
		return ((Kademlia::CSearch*)GetItemData(iItem))->GetLookupHistory();
	}
	return NULL;
}
