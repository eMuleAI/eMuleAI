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
#include "KadContactListCtrl.h"
#include "Ini2.h"
#include "OtherFunctions.h"
#include "emuledlg.h"
#include "MemDC.h"
#include "eMuleAI/IPGeolocation.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// CONContactListCtrl

IMPLEMENT_DYNAMIC(CKadContactListCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CKadContactListCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetDispInfo)
	ON_WM_DESTROY()
	ON_WM_SYSCOLORCHANGE()
END_MESSAGE_MAP()

CKadContactListCtrl::CKadContactListCtrl()
{
	SetGeneralPurposeFind(true);
	SetSkinKey(_T("KadContactsLv"));
}

void CKadContactListCtrl::Init()
{
	SetPrefsKey(_T("ONContactListCtrl"));
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	// Alignment rule: left for text, dates, and status labels; right for sizes, rates, counts, durations, and percentages.
	InsertColumn(colIP, EMPTY,	LVCFMT_LEFT, 50);
	InsertColumn(colID, EMPTY, LVCFMT_LEFT, 16 + DFLT_HASH_COL_WIDTH);	//ID
	InsertColumn(colType, EMPTY, LVCFMT_LEFT, 50);						//TYPE
	InsertColumn(colDistance, EMPTY, LVCFMT_LEFT, 600);				//KADDISTANCE
	InsertColumn(colCountry, EMPTY, LVCFMT_LEFT, 100);

	SetAllIcons();
	Localize();

	LoadSettings();
	int iSortItem = GetSortItem();
	bool bSortAscending = GetSortAscending();

	SetSortArrow(iSortItem, bSortAscending);
	SortItems(SortProc, MAKELONG(iSortItem, !bSortAscending));
}

void CKadContactListCtrl::SaveAllSettings()
{
	SaveSettings();
}

void CKadContactListCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
	SetAllIcons();
}

void CKadContactListCtrl::SetAllIcons()
{
	ApplyImageList(NULL);
	m_ImageList.DeleteImageList();
	m_ImageList.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	m_ImageList.Add(CTempIconLoader(_T("Contact0")));
	m_ImageList.Add(CTempIconLoader(_T("Contact1")));
	m_ImageList.Add(CTempIconLoader(_T("Contact2")));
	m_ImageList.Add(CTempIconLoader(_T("Contact3")));
	m_ImageList.Add(CTempIconLoader(_T("Contact4")));
	m_ImageList.Add(CTempIconLoader(_T("SrcUnknown"))); // replace
	VERIFY(ApplyImageList(m_ImageList) == NULL);
}

void CKadContactListCtrl::Localize()
{
	static const LPCTSTR uids[5] =
	{
		//ID, TYPE, KADDISTANCE
		_T("IP"), _T("ID"), _T("TYPE"), _T("KADDISTANCE"), _T("GEOLOCATION")
	};

	LocaliseHeaderCtrl(uids, _countof(uids));

	for (int iItem = GetItemCount(); --iItem >= 0;)
		RequestContactRowRedraw(iItem);
}

void CKadContactListCtrl::UpdateKadContactCount()
{
	theApp.emuledlg->kademliawnd->UpdateContactCount();
}

bool CKadContactListCtrl::ContactAdd(const Kademlia::CContact *contact)
{
	try {
		ASSERT(contact != NULL);
		if (contact == NULL)
			return false;

		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = reinterpret_cast<LPARAM>(contact);
		int iItem = FindItem(&find);
		if (iItem >= 0) {
			ContactRef(contact);
			return true;
		}

		iItem = InsertItem(LVIF_TEXT | LVIF_PARAM, GetItemCount(), EMPTY, 0, 0, 0, reinterpret_cast<LPARAM>(contact));
		if (iItem >= 0) {
			RequestContactRowRedraw(iItem);
			UpdateKadContactCount();
			return true;
		}
	} catch (...) {
		ASSERT(0);
	}
	return false;
}

void CKadContactListCtrl::ContactRem(const Kademlia::CContact *contact)
{
	try {
		ASSERT(contact != NULL);
		if (contact == NULL)
			return;

		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = reinterpret_cast<LPARAM>(contact);
		int iItem = FindItem(&find);
		if (iItem >= 0) {
			DeleteItem(iItem);
			UpdateKadContactCount();
		}
	} catch (...) {
		ASSERT(0);
	}
}

void CKadContactListCtrl::ContactRef(const Kademlia::CContact *contact)
{
	try {
		ASSERT(contact != NULL);
		if (contact == NULL)
			return;

		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = reinterpret_cast<LPARAM>(contact);
		int iItem = FindItem(&find);
		if (iItem >= 0)
			RequestContactRowRedraw(iItem);
	} catch (...) {
		ASSERT(0);
	}
}

void CKadContactListCtrl::ClearContactRows()
{
	DeleteAllItems();
	UpdateKadContactCount();
}

BOOL CKadContactListCtrl::OnCommand(WPARAM, LPARAM)
{
	// ???
	return TRUE;
}

void CKadContactListCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	// Determine ascending based on whether already sorted on this column
	bool bSortAscending = (GetSortItem() != pNMLV->iSubItem || !GetSortAscending());

	// Item is column clicked
	int iSortItem = pNMLV->iSubItem;

	// Sort table
	UpdateSortHistory(MAKELONG(iSortItem, !bSortAscending));
	SetSortArrow(iSortItem, bSortAscending);
	SortItems(SortProc, MAKELONG(iSortItem, !bSortAscending));
	*pResult = 0;
}

int CALLBACK CKadContactListCtrl::SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort)
{
	const Kademlia::CContact *item1 = reinterpret_cast<Kademlia::CContact*>(lParam1);
	const Kademlia::CContact *item2 = reinterpret_cast<Kademlia::CContact*>(lParam2);
	if (item1 == NULL || item2 == NULL)
		return 0;

	CKadContactListCtrl* pContactListCtrl = theApp.emuledlg != NULL && theApp.emuledlg->kademliawnd != NULL ? theApp.emuledlg->kademliawnd->m_contactListCtrl : NULL;
	CString strSortKey1;
	CString strSortKey2;
	if (pContactListCtrl != NULL && pContactListCtrl->TryGetContactSortKey(item1, LOWORD(lParamSort), strSortKey1) && pContactListCtrl->TryGetContactSortKey(item2, LOWORD(lParamSort), strSortKey2)) {
		int iSortResult = CMuleListCtrl::CompareListSortKeys(strSortKey1, strSortKey2);
		iSortResult = CMuleListCtrl::ApplyListSortDirection(iSortResult, HIWORD(lParamSort) != 0);
		if (iSortResult != 0)
			return iSortResult;
	}

	return 0;
}

void CKadContactListCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	if (!lpDrawItemStruct->itemData || theApp.IsClosing())
		return;

	CRect rcItem(lpDrawItemStruct->rcItem);
	CRect rcClientFullRow;
	GetClientRect(&rcClientFullRow);
	CRect rcPaint(rcClientFullRow.left, rcItem.top, rcClientFullRow.right, rcItem.bottom);
	CMemoryDC dc(CDC::FromHandle(lpDrawItemStruct->hDC), rcPaint);
	BOOL bCtrlFocused;
	InitItemMemDC(dc, lpDrawItemStruct, bCtrlFocused);

	// Set selected item background color
	if ((lpDrawItemStruct->itemState & ODS_SELECTED) != 0)
		dc.FillSolidRect(rcPaint, GetCustomSysColor(COLOR_HIGHLIGHT));

	RECT rcClient;
	GetClientRect(&rcClient);
	Kademlia::CContact* contact = reinterpret_cast<Kademlia::CContact*>(lpDrawItemStruct->itemData);
	if (contact == NULL)
		return;

	const CHeaderCtrl* pHeaderCtrl = GetHeaderCtrl();
	int iCount = pHeaderCtrl->GetItemCount();
	LONG itemLeft = rcItem.left;
	LONG iIconY = max((rcItem.Height() - 15) / 2, 0);
	for (int iCurrent = 0; iCurrent < iCount; ++iCurrent) {
		int iColumn = pHeaderCtrl->OrderToIndex(iCurrent);
		if (IsColumnHidden(iColumn))
			continue;

		UINT uDrawTextAlignment;
		int iColumnWidth = GetColumnWidth(iColumn, uDrawTextAlignment);
		rcItem.left = itemLeft;
		rcItem.right = itemLeft + iColumnWidth - sm_iSubItemInset;
		if (rcItem.left < rcItem.right && HaveIntersection(rcClient, rcItem)) {
			const CString sItem(GetItemDisplayText(contact, iColumn));
			switch (iColumn) {
				case colCountry:
				{
					if (theApp.ipgeolocation->ShowCountryFlag()) {
						POINT point2 = { rcItem.left,rcItem.top + 1 };
						IMAGELISTDRAWPARAMS flagDrawParams = theApp.ipgeolocation->GetFlagImageDrawParams(dc, contact->GetCountryFlagIndex(), point2);

						theApp.ipgeolocation->GetFlagImageList()->DrawIndirect(&flagDrawParams);
						rcItem.left += 22;
					}
					rcItem.left += sm_iIconOffset;
					dc.DrawText(sItem, -1, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
				}
				break;
				case colIP:
				{
					rcItem.left = itemLeft + sm_iIconOffset;
					const POINT point = { rcItem.left, rcItem.top + iIconY };
					uint32 nImageShown = contact->GetType() > 4 ? 4 : contact->GetType();
					if (nImageShown < 3 && !contact->IsIpVerified())
						nImageShown = 5; // if we have an active contact, which is however not IP verified (and therefore not used), show this icon instead
					SafeImageListDraw(&m_ImageList, dc, nImageShown, point, ILD_NORMAL);
					if (theApp.ipgeolocation->ShowCountryFlag() && IsColumnHidden(colCountry)) {
						rcItem.left += 20;
						POINT point2 = { rcItem.left,rcItem.top + 1 };
						IMAGELISTDRAWPARAMS flagDrawParams = theApp.ipgeolocation->GetFlagImageDrawParams(dc, contact->GetCountryFlagIndex(), point2);

						theApp.ipgeolocation->GetFlagImageList()->DrawIndirect(&flagDrawParams);
						rcItem.left += sm_iSubItemInset;
					}
					rcItem.left += 17;
				}
				default:
					rcItem.left += sm_iSubItemInset;
					dc.DrawText(sItem, -1, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
			}
			itemLeft += iColumnWidth;
		}
	}
	DrawFocusRect(dc, &lpDrawItemStruct->rcItem, lpDrawItemStruct->itemState & ODS_FOCUS, bCtrlFocused, lpDrawItemStruct->itemState & ODS_SELECTED);
}

const CString CKadContactListCtrl::GetItemDisplayText(const Kademlia::CContact* contact, const int iSubItem) const
{
	CString sText;
	if (contact == NULL)
		return sText;
	switch (iSubItem) {
	case colIP:
		sText.Format(_T("%s"), ipstr(contact->GetNetIP()));
		break;
	case colID:
		contact->GetClientID(sText);
		break;
	case colType:
		sText.Format(_T("%i(%u)"), contact->GetType(), contact->GetVersion());
		break;
	case colDistance:
		contact->GetDistance(sText);
		break;
	case colCountry:
		sText.Format(_T("%s"), (LPCTSTR)contact->GetGeolocationData());
		break;
	}
	return sText;
}

void CKadContactListCtrl::RequestContactRowRedraw(int iItem)
{
	if (iItem >= 0)
		RequestRowRedrawAsync(iItem, iItem);
}

bool CKadContactListCtrl::TryGetContactText(const Kademlia::CContact *contact, int iSubItem, CString& strText) const
{
	if (contact == NULL) {
		strText.Empty();
		return false;
	}
	strText = GetItemDisplayText(contact, iSubItem);
	return true;
}

bool CKadContactListCtrl::TryGetContactSortKey(const Kademlia::CContact *contact, int iSubItem, CString& strSortKey) const
{
	if (contact == NULL) {
		strSortKey.Empty();
		return false;
	}
	CString strSortText;
	switch (iSubItem) {
	case colIP:
		strSortText.Format(_T("%010u"), htonl(contact->GetNetIP()));
		strSortKey = MakeListSortKey(strSortText);
		break;
	case colID:
		strSortKey = MakeListSortKey(contact->GetClientID().ToHexString());
		break;
	case colType:
		strSortText.Format(_T("%03u:%010u"), contact->GetType(), contact->GetVersion());
		strSortKey = MakeListSortKey(strSortText);
		break;
	case colDistance:
		strSortKey = MakeListSortKey(contact->GetDistance().ToHexString());
		break;
	default:
		strSortKey = MakeListSortKey(GetItemDisplayText(contact, iSubItem));
		break;
	}
	return true;
}

int CKadContactListCtrl::GetDefaultPersistentInfoTipExtraLeftPadding(const SPersistentInfoTipContext& context) const
{
	if (!theApp.ipgeolocation->ShowCountryFlag())
		return 0;

	if (context.iSubItem == colCountry)
		return 22 + sm_iIconOffset;

	if (context.iSubItem == colIP && IsColumnHidden(colCountry))
		return 20 + sm_iSubItemInset;

	return 0;
}

void CKadContactListCtrl::OnLvnGetDispInfo(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (!theApp.IsClosing()) {
		// Although we have an owner drawn listview control we store the text for the primary item in the listview, to be
		// capable of quick searching those items via the keyboard. Because our listview items may change their contents,
		// we do this via a text callback function. The listview control will send us the LVN_DISPINFO notification if
		// it needs to know the contents of the primary item.
		//
		// But, the listview control sends this notification all the time, even if we do not search for an item. At least
		// this notification is only sent for the visible items and not for all items in the list. Though, because this
		// function is invoked *very* often, do *NOT* put any time consuming code in here.
		//
		// Vista: That callback is used to get the strings for the label tips for the sub(!) items.
		//
		NMLVDISPINFO* pDispInfo = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
		if (pDispInfo->item.mask & LVIF_TEXT) {
			const Kademlia::CContact *contact = reinterpret_cast<Kademlia::CContact*>(pDispInfo->item.lParam);
			if (contact != NULL) {
				CString strText;
				TryGetContactText(contact, pDispInfo->item.iSubItem, strText);
				_tcsncpy_s(pDispInfo->item.pszText, pDispInfo->item.cchTextMax, strText, _TRUNCATE);
			}
		}
	}
	*pResult = 0;
}
