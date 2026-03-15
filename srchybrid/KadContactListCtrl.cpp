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
#include "eMuleAI/GeoLite2.h"
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
}


void CKadContactListCtrl::UpdateKadContactCount()
{
	theApp.emuledlg->kademliawnd->UpdateContactCount();
}

bool CKadContactListCtrl::ContactAdd(const Kademlia::CContact *contact)
{
	try {
		ASSERT(contact != NULL);
		int iItem = InsertItem(LVIF_TEXT | LVIF_PARAM, GetItemCount(), EMPTY, 0, 0, 0, (LPARAM)contact);
		if (iItem >= 0) {
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
		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)contact;
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
		LVFINDINFO find;
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)contact;
		int iItem = FindItem(&find);
		if (iItem >= 0)
			Update(iItem);
	} catch (...) {
		ASSERT(0);
	}
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

	int iResult;
	switch (LOWORD(lParamSort)) {
	case colIP: {
		UINT uIP1 = htonl(item1->GetNetIP());
		UINT uIP2 = htonl(item2->GetNetIP());
		if (uIP1 < uIP2)
			iResult = -1;
		else if (uIP1 > uIP2)
			iResult = 1;
		else
			iResult = 0;
		break;
	}
	case colID:
		{
			Kademlia::CUInt128 i1;
			Kademlia::CUInt128 i2;
			item1->GetClientID(i1);
			item2->GetClientID(i2);
			iResult = i1.CompareTo(i2);
		}
		break;
	case colType:
		iResult = item1->GetType() - item2->GetType();
		if (iResult == 0)
			iResult = item1->GetVersion() - item2->GetVersion();
		break;
	case colDistance:
		{
			Kademlia::CUInt128 distance1, distance2;
			item1->GetDistance(distance1);
			item2->GetDistance(distance2);
			iResult = distance1.CompareTo(distance2);
		}
		break;
	case colCountry: {
		CString strCountry1 = item1->GetGeolocationData();
		CString strCountry2 = item2->GetGeolocationData();
		if (!strCountry1.IsEmpty() && !strCountry2.IsEmpty())
			iResult = CompareLocaleStringNoCase(strCountry1, strCountry2);
		else
			iResult = strCountry1.IsEmpty() ? (strCountry2.IsEmpty() ? 0 : 1) : -1;
		break;
	}
	default:
		return 0;
	}
	return HIWORD(lParamSort) ? -iResult : iResult;
}
void CKadContactListCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	if (!lpDrawItemStruct->itemData || theApp.IsClosing())
		return;

	CRect rcItem(lpDrawItemStruct->rcItem);
	CMemoryDC dc(CDC::FromHandle(lpDrawItemStruct->hDC), rcItem);
	BOOL bCtrlFocused;
	InitItemMemDC(dc, lpDrawItemStruct, bCtrlFocused);

	// Set selected item background color
	if ((lpDrawItemStruct->itemState & ODS_SELECTED) != 0)
		dc.FillSolidRect(rcItem, GetCustomSysColor(COLOR_HIGHLIGHT));

	RECT rcClient;
	GetClientRect(&rcClient);
	Kademlia::CContact* contact = reinterpret_cast<Kademlia::CContact*>(lpDrawItemStruct->itemData);

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
			const CString& sItem(GetItemDisplayText(contact, iColumn));
			switch (iColumn) {
				case colCountry:
				{
					if (theApp.geolite2->ShowCountryFlag()) {
						POINT point2 = { rcItem.left,rcItem.top + 1 };
						theApp.geolite2->GetFlagImageList()->DrawIndirect(&theApp.geolite2->GetFlagImageDrawParams(dc, contact->GetCountryFlagIndex(), point2));
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
					if (theApp.geolite2->ShowCountryFlag() && IsColumnHidden(colCountry)) {
						rcItem.left += 20;
						POINT point2 = { rcItem.left,rcItem.top + 1 };
						theApp.geolite2->GetFlagImageList()->DrawIndirect(&theApp.geolite2->GetFlagImageDrawParams(dc, contact->GetCountryFlagIndex(), point2));
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
	switch (iSubItem) {
	case colIP:
		sText.Format(L"%s", ipstr(contact->GetNetIP()));
		break;
	case colID:
		contact->GetClientID(sText);
		break;
	case colType:
		sText.Format(L"%i(%u)", contact->GetType(), contact->GetVersion());
		break;
	case colDistance:
		contact->GetDistance(sText);
		break;
	case colCountry:
		sText.Format(L"%s", contact->GetGeolocationData());
		break;
	}
	return sText;
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
			Kademlia::CContact* pContact = reinterpret_cast<Kademlia::CContact*>(pDispInfo->item.lParam);
			if (pContact != NULL)
				_tcsncpy_s(pDispInfo->item.pszText, pDispInfo->item.cchTextMax, GetItemDisplayText(pContact, pDispInfo->item.iSubItem), _TRUNCATE);
		}
	}
	*pResult = 0;
}
