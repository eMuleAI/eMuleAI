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
#include "QueueListCtrl.h"
#include "UpDownClient.h"
#include "MenuCmds.h"
#include "ClientDetailDialog.h"
#include "Exceptions.h"
#include "KademliaWnd.h"
#include "emuledlg.h"
#include "FriendList.h"
#include "Friend.h"
#include "UploadQueue.h"
#include "TransferDlg.h"
#include "OtherFunctions.h"
#include "MemDC.h"
#include "SharedFileList.h"
#include "ClientCredits.h"
#include "PartFile.h"
#include "ChatWnd.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "Kademlia/Kademlia/Prefs.h"
#include "kademlia/net/KademliaUDPListener.h"
#include "Log.h"
#include "MuleStatusBarCtrl.h"
#include "ClientList.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{


	class CQueueClientReference
	{
	public:
		CQueueClientReference()
			: m_pClient(NULL)
		{
		}

		~CQueueClientReference()
		{
			Release();
		}

		void Attach(CUpDownClient* pClient)
		{
			if (m_pClient == pClient)
				return;
			Release();
			m_pClient = pClient;
		}

		CUpDownClient* Get() const
		{
			return m_pClient;
		}

		void Release()
		{
			if (m_pClient != NULL) {
				m_pClient->ReleaseRuntimeReference();
				m_pClient = NULL;
			}
		}

	private:
		CUpDownClient* m_pClient;
	};

	CUpDownClient* AcquireRuntimeClient(CQueueListCtrl::QueueClientItemID uRuntimeID)
	{
		return (uRuntimeID != 0 && theApp.clientlist != NULL) ? theApp.clientlist->AcquireTrackedClientByRuntimeID(static_cast<ClientRuntimeID>(uRuntimeID)) : NULL;
	}

	CObject* CreateClientDetailWalkerToken(CQueueListCtrl::QueueClientItemID uRuntimeID)
	{
		return uRuntimeID != 0 ? reinterpret_cast<CObject*>((static_cast<ULONG_PTR>(uRuntimeID) << 1) | 1) : NULL;
	}
}


IMPLEMENT_DYNAMIC(CQueueListCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CQueueListCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetDispInfo)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
	ON_WM_CONTEXTMENU()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_KEYDOWN()
END_MESSAGE_MAP()

CQueueListCtrl::CQueueListCtrl()
	: CListCtrlItemWalk(this)
{
	SetGeneralPurposeFind(true);
	SetSkinKey(_T("QueuedLv"));

	// Use user's configured list update period (minimum 100ms already enforced in preferences)
	VERIFY((m_hTimer = ::SetTimer(NULL, 0, thePrefs.GetUITweaksListUpdatePeriod(), QueueUpdateTimer)) != 0);
	if (thePrefs.GetVerbose() && !m_hTimer)
		AddDebugLogLine(false, _T("Failed to create 'queue list control' timer - %s"), (LPCTSTR)EscPercent(GetErrorMessage(::GetLastError())));
}

CQueueListCtrl::~CQueueListCtrl()
{
	if (m_hTimer)
		VERIFY(::KillTimer(NULL, m_hTimer));
	m_ListItemsMap.clear();
}

void CQueueListCtrl::Init()
{
	SetPrefsKey(_T("QueueListCtrl"));
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	// Alignment rule: left for text, dates, and status labels; right for sizes, rates, counts, durations, and percentages.
	InsertColumn(0, EMPTY,	LVCFMT_LEFT, DFLT_CLIENTNAME_COL_WIDTH);	//QL_USERNAME
	InsertColumn(1, EMPTY,	LVCFMT_LEFT, DFLT_FILENAME_COL_WIDTH);		//FILE
	InsertColumn(2, EMPTY,	LVCFMT_LEFT, DFLT_PRIORITY_COL_WIDTH);		//FILEPRIO
	InsertColumn(3, EMPTY,	LVCFMT_RIGHT,  60);						//QL_RATING
	InsertColumn(4, EMPTY,	LVCFMT_RIGHT,  60);						//SCORE
	InsertColumn(5, EMPTY,	LVCFMT_RIGHT,  60);						//ASKED
	InsertColumn(6, EMPTY,	LVCFMT_RIGHT, 110);						//LASTSEEN
	InsertColumn(7, EMPTY,	LVCFMT_RIGHT, 110);						//ENTERQUEUE
	InsertColumn(8, EMPTY, LVCFMT_LEFT, DFLT_PARTSTATUS_COL_WIDTH);	//UPSTATUS
	InsertColumn(9, EMPTY, LVCFMT_LEFT,  90);
	InsertColumn(10, EMPTY, LVCFMT_LEFT, DFLT_HASH_COL_WIDTH);
	InsertColumn(11, EMPTY, LVCFMT_LEFT, 100);
	InsertColumn(12, EMPTY, LVCFMT_LEFT, 100, 13);
	InsertColumn(13, EMPTY, LVCFMT_LEFT, 100);
	InsertColumn(14, EMPTY, LVCFMT_RIGHT, 80);
	InsertColumn(15, EMPTY, LVCFMT_RIGHT, 100);
	InsertColumn(16, EMPTY, LVCFMT_LEFT, 100);
	InsertColumn(17, EMPTY, LVCFMT_LEFT, 90);
	InsertColumn(18, EMPTY, LVCFMT_LEFT, 100);
	InsertColumn(19, EMPTY, LVCFMT_LEFT, 100);
	InsertColumn(20, EMPTY, LVCFMT_LEFT, 100);
	InsertColumn(21, EMPTY, LVCFMT_RIGHT, 100);
	InsertColumn(22, EMPTY, LVCFMT_RIGHT, 100);
	InsertColumn(23, EMPTY, LVCFMT_LEFT, 100);
	InsertColumn(24, EMPTY, LVCFMT_RIGHT, 60);
	InsertColumn(25, EMPTY, LVCFMT_RIGHT, 60);

	SetAllIcons();
	LoadSettings();
	SetSortArrow();
	SortItems(SortProc, MAKELONG(GetSortItem(), !GetSortAscending()));
}

void CQueueListCtrl::Localize()
{
	static const LPCTSTR uids[26] =
	{
		_T("QL_USERNAME"), _T("FILE"), _T("FILEPRIO"), _T("QL_RATING"), _T("SCORE")
		, _T("ASKED"), _T("LAST_ASKED"), _T("ENTERQUEUE"), _T("UPSTATUS")
        , _T("CD_CSOFT")
        , _T("CD_UHASH2")
		, _T("IPPORT")
        , _T("GEOLOCATION")
		, _T("SHAREDFILESSTATUS")
		, _T("SHAREDFILESCOUNTCOLUMN")
		, _T("SHAREDFILESLASTQUERIED")
		, _T("FRIEND")
		, _T("FRIEND_SLOT")
        , _T("ID_TYPE")
        , _T("BAD_CLIENT_TYPE")
        , _T("PUNISHMENT")
        , _T("FIRST_SEEN")
		, _T("LAST_SEEN")
		, _T("CLIENT_NOTE")
		, _T("RATIO")
		, _T("RATIO_SESSION")
	};

	LocaliseHeaderCtrl(uids, _countof(uids));
}

void CQueueListCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
	SetAllIcons();
}

void CQueueListCtrl::SetAllIcons()
{
	ApplyImageList(NULL);
	// Apply the image list also to the listview control, even if we use our own 'DrawItem'.
	// This is needed to give the listview control a chance to initialize the row height.
	m_pImageList = &theApp.emuledlg->GetClientIconList();
	VERIFY(ApplyImageList(*m_pImageList) == NULL);
}

void CQueueListCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
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
	const QueueClientItemID uRuntimeID = static_cast<QueueClientItemID>(lpDrawItemStruct->itemData);
	if (m_ListItemsMap.find(uRuntimeID) == m_ListItemsMap.end())
		return;
	CQueueClientReference clientRef;
	clientRef.Attach(AcquireRuntimeClient(uRuntimeID));
	CUpDownClient* client = clientRef.Get();
	if (client == NULL)
		return;

	const CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
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
		rcItem.right = itemLeft + iColumnWidth;
		if (rcItem.left < rcItem.right && HaveIntersection(rcClient, rcItem)) {
			const CString sItem(GetItemDisplayText(client, iColumn));
			switch (iColumn) {
			case 0: //user name
			{
					int iImage = 0;
					UINT uOverlayImage = 0;
					client->GetDisplayImage(iImage, uOverlayImage);

					const POINT point = { rcItem.left, rcItem.top + iIconY };
					SafeImageListDraw(m_pImageList, dc, iImage, point, ILD_NORMAL | INDEXTOOVERLAYMASK(uOverlayImage));
					if (theApp.ipgeolocation->ShowCountryFlag() && IsColumnHidden(12)) {
						rcItem.left += 20;
						POINT point2 = { rcItem.left,rcItem.top + 1 };
						IMAGELISTDRAWPARAMS flagDrawParams = theApp.ipgeolocation->GetFlagImageDrawParams(dc, client->GetCountryFlagIndex(), point2);

						theApp.ipgeolocation->GetFlagImageList()->DrawIndirect(&flagDrawParams);
						rcItem.left += sm_iSubItemInset;
					}
					rcItem.left += 17;
			}
			default: //any text column
				rcItem.left += sm_iSubItemInset;
				rcItem.right -= sm_iSubItemInset;
				dc.DrawText(sItem, -1, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
				break;
			//case 9: //obtained parts
			case 8: //obtained parts
				if (client->GetUpPartCount()) {
					CRect rcStatus(rcItem);
					++rcStatus.top;
					--rcStatus.bottom;
					if (rcStatus.Width() > 0 && rcStatus.Height() > 0) {
						const bool bUseFlatBar = thePrefs.UseFlatBar();
						const int iSavedDC = bUseFlatBar ? dc->SaveDC() : 0;
						client->DrawUpStatusBar(dc, rcStatus, false, bUseFlatBar);
						if (iSavedDC != 0)
							dc->RestoreDC(iSavedDC);
					}
				}
			break;
			case 12:
			{
				if (theApp.ipgeolocation->ShowCountryFlag()) {
					POINT point2 = { rcItem.left,rcItem.top + 1 };
					IMAGELISTDRAWPARAMS flagDrawParams = theApp.ipgeolocation->GetFlagImageDrawParams(dc, client->GetCountryFlagIndex(), point2);
					theApp.ipgeolocation->GetFlagImageList()->DrawIndirect(&flagDrawParams);
					rcItem.left += 22;
				}
				rcItem.left += sm_iIconOffset;
				dc->DrawText(sItem, sItem.GetLength(), &rcItem, MLC_DT_TEXT);
			}
			break;
			}
		}
		itemLeft += iColumnWidth;
	}

	DrawFocusRect(dc, &lpDrawItemStruct->rcItem, lpDrawItemStruct->itemState & ODS_FOCUS, bCtrlFocused, lpDrawItemStruct->itemState & ODS_SELECTED);

}

void CQueueListCtrl::RequestQueueListRedrawForRange(int iFirst, int iLast)
{
	if (theApp.GetBackendLifecycleState() >= CemuleApp::BackendLifecycleStoppingUiUpdates || !::IsWindow(m_hWnd))
		return;
	if (iFirst < 0 || iLast < iFirst || GetItemCount() <= 0)
		return;
	iFirst = max(0, iFirst);
	iLast = min(iLast, GetItemCount() - 1);
	if (iLast >= iFirst)
		RequestRowRedrawAsync(iFirst, iLast);
}


const CString CQueueListCtrl::GetItemDisplayText(CUpDownClient* client, const int iSubItem) const
{
	CString sText;
	if (client == NULL)
		return sText;
	switch (iSubItem) {
	case 0:
		if (client->GetUserName() != NULL)
			sText = client->GetUserName();
		else
			sText.Format(_T("(%s)"), (LPCTSTR)GetResString(_T("UNKNOWN")));
		break;
	case 1:
		{
			const CKnownFile *file = theApp.sharedfiles->GetFileByID(client->GetUploadFileID());
			if (file)
				sText = file->GetFileName();
		}
		break;
	case 2:
		{
			const CKnownFile *file = theApp.sharedfiles->GetFileByID(client->GetUploadFileID());
			if (file) {
				LPCTSTR uid;
				switch (file->GetUpPriority()) {
				case PR_VERYLOW:
					uid = _T("PRIOVERYLOW");
					break;
				case PR_LOW:
					uid = file->IsAutoUpPriority() ? _T("PRIOAUTOLOW") : _T("PRIOLOW");
					break;
				case PR_NORMAL:
					uid = file->IsAutoUpPriority() ? _T("PRIOAUTONORMAL") : _T("PRIONORMAL");
					break;
				case PR_HIGH:
					uid = file->IsAutoUpPriority() ? _T("PRIOAUTOHIGH") : _T("PRIOHIGH");
					break;
				case PR_VERYHIGH:
					uid = _T("PRIORELEASE");
					break;
				default:
					uid = EMPTY;
				}
				if (uid)
					sText = GetResString(uid);
			}
		}
		break;
	case 3:
		sText.Format(_T("%u"), client->GetScore(false, false, true));
		break;
	case 4:
		{
			sText.Format(_T("%u"), client->GetScore(false));
		}
		break;
	case 5:
		sText.Format(_T("%u"), client->GetAskedCount());
		break;
	case 6:
		sText = CastSecondsToHM((::GetTickCount() - client->GetLastUpRequest()) / SEC2MS(1));
		break;
	case 7:
		sText = CastSecondsToHM((::GetTickCount() - client->GetWaitStartTime()) / SEC2MS(1));
		break;
	case 8:
		sText = GetResString(_T("UPSTATUS"));
		break;
	case 9:
		sText = client->DbgGetFullClientSoftVer();
		if (sText.IsEmpty())
			sText = GetResString(_T("UNKNOWN"));
		break;
	case 10: //hash
		sText = md4str(client->GetUserHash());
		break;
	case 11:
		sText.Format(_T("%s:%u"), (LPCTSTR)ipstr(!client->GetIP().IsNull() ? client->GetIP() : client->GetConnectIP()), client->GetUserPort());
		break;
	case 12:
		sText = client->GetGeolocationData();
		break;
	case 13:
		sText = client->GetSharedFilesStatusText();
		break;
	case 14:
		sText.Format(_T("%u"), client->m_uSharedFilesCount);
		break;
	case 15:
		if (client->m_tSharedFilesLastQueriedTime)
			sText.Format(_T(" %s"), (LPCTSTR)CastSecondsToHM((time(NULL) - client->m_tSharedFilesLastQueriedTime)));
		else
			sText = EMPTY;
		break;
	case 16:
		if (client->IsFriend())
			sText = GetResString(_T("YES"));
		else
			sText = GetResString(_T("NO"));
		break;
	case 17:
		sText = GetResString(client->GetFriendSlot() ? _T("YES") : _T("NO"));
		break;
	case 18:
		if (client->HasLowID())
			sText = GetResString(_T("IDLOW"));
		else
			sText = GetResString(_T("IDHIGH"));
		break;
	case 19:
		sText = client->GetPunishmentReason();
		break;
	case 20:
		sText = client->GetPunishmentText();
		break;
	case 21:
		if (client->tFirstSeen)
			sText.Format(_T(" %s"), (LPCTSTR)CastSecondsToHM(time(NULL) - client->tFirstSeen));
		else
			sText = _T("Unknown");
		break;
	case 22:
		if (client->tLastSeen)
			sText.Format(_T(" %s"), (LPCTSTR)CastSecondsToHM(time(NULL) - client->tLastSeen));
		else
			sText = _T("Unknown");
		break;
	case 23:
		sText = client->m_strClientNote;
		break;
	case 24:
		{
			const CKnownFile *file = theApp.sharedfiles->GetFileByID(client->GetUploadFileID());
			if (file)
				sText.Format(_T("%.1f"), file->GetAllTimeRatio());
		}
		break;
	case 25:
		{
			const CKnownFile *file = theApp.sharedfiles->GetFileByID(client->GetUploadFileID());
			if (file)
				sText.Format(_T("%.1f"), file->GetRatio());
		}
		break;
	}
	return sText;
}

int CQueueListCtrl::GetDefaultPersistentInfoTipExtraLeftPadding(const SPersistentInfoTipContext& context) const
{
	if (!theApp.ipgeolocation->ShowCountryFlag())
		return 0;

	if (context.iSubItem == 12)
		return 22 + sm_iIconOffset;

	if (context.iSubItem == 0 && IsColumnHidden(12))
		return 20 + sm_iSubItemInset;

	return 0;
}

void CQueueListCtrl::OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult)
{
	if (!theApp.IsClosing()) {
		const LVITEMW &rItem = reinterpret_cast<NMLVDISPINFO*>(pNMHDR)->item;
		if ((rItem.mask & LVIF_TEXT) && rItem.pszText != NULL && rItem.cchTextMax > 0) {
			QueueClientItemID uRuntimeID = 0;
			if (rItem.iItem >= 0)
				uRuntimeID = static_cast<QueueClientItemID>(GetItemData(rItem.iItem));
			CQueueClientReference clientRef;
			clientRef.Attach(AcquireRuntimeClient(uRuntimeID));
			CUpDownClient* client = clientRef.Get();
			CString strText = client != NULL ? GetItemDisplayText(client, rItem.iSubItem) : CString();
			_tcsncpy_s(rItem.pszText, rItem.cchTextMax, strText, _TRUNCATE);
		}
	}
	*pResult = 0;
}

void CQueueListCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	bool sortAscending;
	if (GetSortItem() != pNMLV->iSubItem)
		switch (pNMLV->iSubItem) {
		case 2: // Up Priority
		case 3: // Rating
		case 4: // Score
		case 5: // Ask Count
		case 8: // Banned
		case 9: // Part Count
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
	SortItems(SortProc, MAKELONG(pNMLV->iSubItem, !sortAscending));
	*pResult = 0;
}

int CALLBACK CQueueListCtrl::SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort)
{
	CQueueClientReference item1Ref;
	CQueueClientReference item2Ref;
	item1Ref.Attach(AcquireRuntimeClient(static_cast<CQueueListCtrl::QueueClientItemID>(lParam1)));
	item2Ref.Attach(AcquireRuntimeClient(static_cast<CQueueListCtrl::QueueClientItemID>(lParam2)));
	CUpDownClient* item1 = item1Ref.Get();
	CUpDownClient* item2 = item2Ref.Get();
	if (item1 == NULL && item2 == NULL)
		return 0;
	if (item1 == NULL)
		return 1;
	if (item2 == NULL)
		return -1;

	int iResult = 0;
	switch (LOWORD(lParamSort)) {
	case 0: //user name
		if (item1->GetUserName() && item2->GetUserName())
			iResult = CompareLocaleStringNoCase(item1->GetUserName(), item2->GetUserName());
		else if (item1->GetUserName() == NULL)
			iResult = 1; // place clients with no user names at bottom
		else if (item2->GetUserName() == NULL)
			iResult = -1; // place clients with no user names at bottom
		break;
	case 1: //file name
		{
			const CKnownFile *file1 = theApp.sharedfiles->GetFileByID(item1->GetUploadFileID());
			const CKnownFile *file2 = theApp.sharedfiles->GetFileByID(item2->GetUploadFileID());
			if (file1 != NULL && file2 != NULL)
				iResult = CompareLocaleStringNoCase(file1->GetFileName(), file2->GetFileName());
			else
				iResult = (file1 == NULL) ? 1 : -1;
		}
		break;
	case 2:
		{
			const CKnownFile *file1 = theApp.sharedfiles->GetFileByID(item1->GetUploadFileID());
			const CKnownFile *file2 = theApp.sharedfiles->GetFileByID(item2->GetUploadFileID());
			if (file1 != NULL && file2 != NULL)
				iResult = (file1->GetUpPriority() == PR_VERYLOW ? -1 : file1->GetUpPriority()) - (file2->GetUpPriority() == PR_VERYLOW ? -1 : file2->GetUpPriority());
			else
				iResult = (file1 == NULL) ? 1 : -1;

		}
		break;
	case 3:
		iResult = CompareUnsigned(item1->GetScore(false, false, true), item2->GetScore(false, false, true));
		break;
	case 4:
		iResult = CompareUnsigned(item1->GetScore(false), item2->GetScore(false));
		break;
	case 5:
		iResult = CompareUnsigned(item1->GetAskedCount(), item2->GetAskedCount());
		break;
	case 6:
		iResult = CompareUnsigned(item1->GetLastUpRequest(), item2->GetLastUpRequest());
		break;
	case 7:
		iResult = CompareUnsigned(item1->GetWaitStartTime(), item2->GetWaitStartTime());
		break;
	case 8:
		iResult = CompareUnsigned(item1->GetUpPartCount(), item2->GetUpPartCount());
		break;
	case 9:
		iResult = CompareLocaleStringNoCase(item1->DbgGetFullClientSoftVer(), item2->DbgGetFullClientSoftVer());
		break;
	case 10: //hash
		iResult = memcmp(item1->GetUserHash(), item2->GetUserHash(), 16);
		break;
		break;
	case 11:
		iResult = CompareIP(!item1->GetIP().IsNull() ? item1->GetIP() : item1->GetConnectIP(), !item2->GetIP().IsNull() ? item2->GetIP() : item2->GetConnectIP());
		if (iResult == 0)
			iResult = CompareUnsigned(item1->GetUserPort(), item2->GetUserPort());
		break;
	case 12:
		if (item1->GetGeolocationData(true) && item2->GetGeolocationData(true))
			iResult = CompareLocaleStringNoCase(item1->GetGeolocationData(true), item2->GetGeolocationData(true));
		else if (item1->GetGeolocationData(true))
			iResult = 1;
		else
			iResult = -1;
		break;
	case 13:
		iResult = CompareLocaleStringNoCase(item1->GetSharedFilesStatusText(), item2->GetSharedFilesStatusText());
		break;
	case 14:
		iResult = CompareUnsigned(item1->m_uSharedFilesCount, item2->m_uSharedFilesCount);
		break;
	case 15:
		iResult = CompareUnsigned(item1->m_tSharedFilesLastQueriedTime, item2->m_tSharedFilesLastQueriedTime);
		break;
	case 16:
		iResult = CompareUnsigned(item1->IsFriend(), item2->IsFriend());
		break;
	case 17:
		iResult = CompareUnsigned(item1->GetFriendSlot(), item2->GetFriendSlot());
		break;
	case 18:
		iResult = CompareUnsigned(item1->HasLowID(), item2->HasLowID());
		break;
	case 19:
		iResult = CompareLocaleStringNoCase(item1->GetPunishmentReason(), item2->GetPunishmentReason());
		break;
	case 20:
		iResult = CompareLocaleStringNoCase(item1->GetPunishmentText(), item2->GetPunishmentText());
		break;
	case 21:
		iResult = CompareUnsigned(item1->tFirstSeen, item2->tFirstSeen);
		break;
	case 22:
		iResult = CompareUnsigned(item1->tLastSeen, item2->tLastSeen);
		break;
	case 23:
		iResult = CompareLocaleStringNoCase(item1->m_strClientNote, item2->m_strClientNote);
		break;
	case 24:
		{
			const CKnownFile *file1 = theApp.sharedfiles->GetFileByID(item1->GetUploadFileID());
			const CKnownFile *file2 = theApp.sharedfiles->GetFileByID(item2->GetUploadFileID());
			if (file1 != NULL && file2 != NULL) {
				const double ratio1 = file1->GetAllTimeRatio();
				const double ratio2 = file2->GetAllTimeRatio();
				iResult = (ratio1 < ratio2) ? -1 : static_cast<int>(ratio1 > ratio2);
			} else
				iResult = (file1 == NULL) ? 1 : -1;
		}
		break;
	case 25:
		{
			const CKnownFile *file1 = theApp.sharedfiles->GetFileByID(item1->GetUploadFileID());
			const CKnownFile *file2 = theApp.sharedfiles->GetFileByID(item2->GetUploadFileID());
			if (file1 != NULL && file2 != NULL) {
				const double ratio1 = file1->GetRatio();
				const double ratio2 = file2->GetRatio();
				iResult = (ratio1 < ratio2) ? -1 : static_cast<int>(ratio1 > ratio2);
			} else
				iResult = (file1 == NULL) ? 1 : -1;
		}
		break;

	}

	if (HIWORD(lParamSort))
		iResult = -iResult;

	// Handled in parent class

	return iResult;
}

void CQueueListCtrl::OnNmDblClk(LPNMHDR, LRESULT *pResult)
{
	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	if (iSel >= 0) {
		CQueueClientReference clientRef;
		clientRef.Attach(AcquireRuntimeClient(static_cast<QueueClientItemID>(GetItemData(iSel))));
		CUpDownClient* client = clientRef.Get();
		if (client) {
			CClientDetailDialog dialog(client, this);
			dialog.DoModal();
		}
	}
	*pResult = 0;
}

void CQueueListCtrl::OnContextMenu(CWnd*, CPoint point)
{
	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	CQueueClientReference clientRef;
	if (iSel >= 0)
		clientRef.Attach(AcquireRuntimeClient(static_cast<QueueClientItemID>(GetItemData(iSel))));
	const CUpDownClient* client = clientRef.Get();
	const bool is_ed2k = client && client->IsEd2kClient();
	const CFriend *pFriend = client != NULL ? client->GetFriend() : NULL;

	CMenuXP ClientMenu;
	ClientMenu.CreatePopupMenu();
	ClientMenu.AddMenuSidebar(GetResString(_T("CLIENTS")));
	ClientMenu.AppendMenu(MF_STRING | (client ? MF_ENABLED : MF_GRAYED), MP_DETAIL, GetResString(_T("SHOWDETAILS")), _T("CLIENTDETAILS"));
	ClientMenu.SetDefaultItem(MP_DETAIL);
	ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && !client->IsFriend()) ? MF_ENABLED : MF_GRAYED), MP_ADDFRIEND, GetResString(_T("ADDFRIEND")), _T("ADDFRIEND"));
	ClientMenu.AppendMenu(MF_STRING | (pFriend != NULL ? MF_ENABLED : MF_GRAYED), MP_FRIENDSLOT, GetResString(_T("FRIENDSLOT")), _T("FRIENDSLOT"));
	ClientMenu.CheckMenuItem(MP_FRIENDSLOT, (pFriend != NULL && pFriend->GetFriendSlot()) ? MF_CHECKED : MF_UNCHECKED);
	ClientMenu.AppendMenu(MF_STRING | (is_ed2k ? MF_ENABLED : MF_GRAYED), MP_MESSAGE, GetResString(_T("SEND_MSG")), _T("SENDMESSAGE"));
	ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && client->GetViewSharedFilesSupport()) ? MF_ENABLED : MF_GRAYED), MP_SHOWLIST, GetResString(_T("VIEWFILES")), _T("VIEWFILES"));
	ClientMenu.AppendMenu(MF_STRING | (client ? MF_ENABLED : MF_GRAYED), MP_EDIT_NOTE, GetResString(_T("EDIT_CLIENT_NOTE")), _T("RENAME"));
	if (thePrefs.IsExtControlsEnabled())
		ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && client->IsBanned() || (client && client->IsBadClient())) ? MF_ENABLED : MF_GRAYED), MP_UNBAN, GetResString(_T("UNBAN")));
	if (Kademlia::CKademlia::IsRunning() && !Kademlia::CKademlia::IsConnected())
		ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && client->GetKadPort() != 0 && client->GetKadVersion() >= KADEMLIA_VERSION2_47a) ? MF_ENABLED : MF_GRAYED), MP_BOOT, GetResString(_T("BOOTSTRAP")));

	ClientMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	CMenuXP m_PunishmentMenu;
	m_PunishmentMenu.CreateMenu();
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_IPUSERHASHBAN, GetResString(_T("IP_USER_HASH_BAN")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_USERHASHBAN, GetResString(_T("USER_HASH_BAN")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_UPLOADBAN, GetResString(_T("UPLOAD_BAN")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX01, GetResString(_T("SCORE_01")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX02, GetResString(_T("SCORE_02")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX03, GetResString(_T("SCORE_03")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX04, GetResString(_T("SCORE_04")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX05, GetResString(_T("SCORE_05")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX06, GetResString(_T("SCORE_06")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX07, GetResString(_T("SCORE_07")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX08, GetResString(_T("SCORE_08")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_SCOREX09, GetResString(_T("SCORE_09")));
	m_PunishmentMenu.AppendMenu(MF_STRING, MP_PUNISMENT_NONE, GetResString(_T("NO_PUNISHMENT")));
	ClientMenu.EnableMenuItem((UINT)m_PunishmentMenu.m_hMenu, MF_ENABLED);
	int m_PunishmentMenuItem = client ? MP_PUNISMENT_IPUSERHASHBAN + client->m_uPunishment : 0;
	m_PunishmentMenu.CheckMenuRadioItem(MP_PUNISMENT_IPUSERHASHBAN, MP_PUNISMENT_NONE, m_PunishmentMenuItem, 0);
	ClientMenu.AppendMenu(MF_STRING | MF_POPUP | (client ? MF_ENABLED : MF_GRAYED), (UINT_PTR)m_PunishmentMenu.m_hMenu, GetResString(_T("PUNISHMENT")), _T("PUNISHMENT"));
	ClientMenu.AppendMenu(MF_STRING | MF_SEPARATOR);

	ClientMenu.AppendMenu(MF_STRING | (GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED), MP_FIND, GetResString(_T("FIND")), _T("Search"));
	GetPopupMenuPos(*this, point);
	ClientMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
}

BOOL CQueueListCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	wParam = LOWORD(wParam);

	switch (wParam) {
	case MP_FIND:
		OnFindStart();
		return TRUE;
	}

	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	if (iSel >= 0) {
		CQueueClientReference clientRef;
		clientRef.Attach(AcquireRuntimeClient(static_cast<QueueClientItemID>(GetItemData(iSel))));
		CUpDownClient* client = clientRef.Get();
		if (client == NULL)
			return TRUE;
		auto RefreshQueueCountAfterManualPunishment = []() {
			if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL)
				theApp.emuledlg->transferwnd->InvalidateQueueCount(true);
		};
		switch (wParam) {
		case MP_SHOWLIST:
			{
				CUpDownClient* NewClient = theApp.emuledlg->transferwnd->GetClientList()->ArchivedToActive(client);
				if (NewClient && (client == NewClient || theApp.clientlist->IsValidClient(NewClient)))
					NewClient->RequestSharedFileList();
			}
		break;
		case MP_MESSAGE:
			{
				CUpDownClient* NewClient = theApp.emuledlg->transferwnd->GetClientList()->ArchivedToActive(client);
				if (NewClient && (client == NewClient || theApp.clientlist->IsValidClient(NewClient)))
					theApp.emuledlg->chatwnd->StartSession(NewClient);
			}
		break;
		case MP_ADDFRIEND:
			if (theApp.friendlist->AddFriend(client)) {
				RequestRowRedrawAsync(iSel, iSel);
			}
			break;
		case MP_FRIENDSLOT:
			{
				CFriend *pFriend = client->GetFriend();
				if (pFriend != NULL) {
					pFriend->SetFriendSlot(!pFriend->GetFriendSlot());
					theApp.friendlist->SaveList();
					RequestRowRedrawAsync(iSel, iSel);
				}
			}
			break;
		case MP_UNBAN:
			if (client->IsBanned() || client->IsBadClient()) {
				theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_CANCELATION")), PR_MANUAL, P_NOPUNISHMENT);
				Update(iSel);
				RefreshQueueCountAfterManualPunishment();
			}
			break;
		case MP_DETAIL:
		case MPG_ALTENTER:
		case IDA_ENTER:
			{
				CClientDetailDialog dialog(client, this);
				dialog.DoModal();
			}
			break;
		case MP_BOOT:
			if (client->GetKadPort() && client->GetKadVersion() >= KADEMLIA_VERSION2_47a)
				Kademlia::CKademlia::Bootstrap(client->GetIPv4().ToUInt32(true), client->GetKadPort());
			break;
		case MP_EDIT_NOTE:
			client->SetClientNote();
			break;
		case MP_PUNISMENT_IPUSERHASHBAN:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_IP_BAN")), PR_MANUAL, P_IPUSERHASHBAN);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_USERHASHBAN:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_USER_HASH_BAN")), PR_MANUAL, P_USERHASHBAN);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_UPLOADBAN:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_UPLOAD_BAN")), PR_MANUAL, P_UPLOADBAN);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_SCOREX01:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX01);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_SCOREX02:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX02);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_SCOREX03:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX03);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_SCOREX04:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX04);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_SCOREX05:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX05);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_SCOREX06:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX06);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_SCOREX07:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX07);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_SCOREX08:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX08);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_SCOREX09:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX09);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		case MP_PUNISMENT_NONE:
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_CANCELATION")), PR_MANUAL, P_NOPUNISHMENT);
			RefreshClient(client);
			RefreshQueueCountAfterManualPunishment();
			break;
		}
	}
	return TRUE;
}

int CQueueListCtrl::FindSortedInsertIndex(QueueClientItemID uRuntimeID)
{
	const int iItemCount = GetItemCount();
	if (GetSortItem() == -1 || iItemCount <= 0)
		return iItemCount;

	const LPARAM lParamSort = MAKELONG(GetSortItem(), !GetSortAscending());
	for (int iItem = 0; iItem < iItemCount; ++iItem) {
		if (SortProc((LPARAM)uRuntimeID, (LPARAM)GetItemData(iItem), lParamSort) < 0)
			return iItem;
	}
	return iItemCount;
}

void CQueueListCtrl::AddClient(CUpDownClient* client)
{
	if (theApp.IsClosing() || client == NULL)
		return;


	SClientItemId id;
	if (!CUploadQueue::GetClientItemId(client, id))
		return;

	if (m_ListItemsMap.find(id.m_uRuntimeID) != m_ListItemsMap.end())
		return;
	m_ListItemsMap.insert(id.m_uRuntimeID);

	if (!thePrefs.IsQueueListDisabled() && !IsFilteredOut(client)) {
		const int iItem = InsertItem(LVIF_TEXT | LVIF_PARAM, FindSortedInsertIndex(id.m_uRuntimeID), LPSTR_TEXTCALLBACK, 0, 0, 0, (LPARAM)id.m_uRuntimeID);
		if (iItem >= 0)
			RequestQueueListRedrawForRange(iItem, iItem);
		theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
	}
}

void CQueueListCtrl::RemoveClient(CUpDownClient* client)
{
	if (theApp.IsClosing() || client == NULL)
		return;

	SClientItemId id;
	if (!CUploadQueue::GetClientItemId(client, id))
		return;

	RemoveClientByRuntimeID(id.m_uRuntimeID);
}

void CQueueListCtrl::RemoveClientByRuntimeID(DWORD uRuntimeID)
{
	if (theApp.IsClosing() || uRuntimeID == 0)
		return;

	auto it = m_ListItemsMap.find(uRuntimeID);
	if (it != m_ListItemsMap.end()) {
		m_ListItemsMap.erase(it);
		LVFINDINFO find = {};
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)uRuntimeID;
		int iItem = FindItem(&find);
		if (iItem >= 0) {
			DeleteItem(iItem);
			theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
		}
	}
}

void CQueueListCtrl::RefreshClient(const CUpDownClient* client)
{
	if (theApp.IsClosing() || !client || theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !theApp.emuledlg->transferwnd->GetQueueList()->IsWindowVisible())
		return;

	SClientItemId id;
	if (!CUploadQueue::GetClientItemId(client, id))
		return;
	LVFINDINFO find = {};
	find.flags = LVFI_PARAM;
	find.lParam = (LPARAM)id.m_uRuntimeID;
	int iItem = FindItem(&find);
	if (iItem < 0)
		return;

	MaintainSortOrderAfterThrottledUpdate();
	iItem = FindItem(&find);
	if (IsItemIndexVisible(iItem))
		RequestQueueListRedrawForRange(iItem, iItem);
}

void CQueueListCtrl::UpdateView()
{
	for (auto it = m_ListItemsMap.begin(); it != m_ListItemsMap.end();) {
		const QueueClientItemID uRuntimeID = *it;
		CQueueClientReference clientRef;
		clientRef.Attach(AcquireRuntimeClient(uRuntimeID));
		CUpDownClient* cur_item = clientRef.Get();
		if (cur_item == NULL) {
			LVFINDINFO find = {};
			find.flags = LVFI_PARAM;
			find.lParam = (LPARAM)uRuntimeID;
			const int iItem = FindItem(&find);
			if (iItem >= 0)
				DeleteItem(iItem);
			auto itErase = it++;
			m_ListItemsMap.erase(itErase);
			continue;
		}

		if (!IsFilteredOut(cur_item))
			ShowClient(cur_item);
		else
			HideClient(cur_item);
		++it;
	}
	theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
	RequestQueueListRedrawForRange(0, GetItemCount() - 1);
}

void CQueueListCtrl::HideClient(CUpDownClient* client)
{
	SClientItemId id;
	if (!CUploadQueue::GetClientItemId(client, id))
		return;

	if (m_ListItemsMap.find(id.m_uRuntimeID) != m_ListItemsMap.end()) { // If client is on the map we can proceed
		LVFINDINFO find = {};
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)id.m_uRuntimeID;
		int iItem = FindItem(&find);
		if (iItem >= 0) {
			DeleteItem(iItem);
			return;
		}
	}
}

void CQueueListCtrl::ShowClient(CUpDownClient* client)
{
	SClientItemId id;
	if (!CUploadQueue::GetClientItemId(client, id))
		return;

	if (m_ListItemsMap.find(id.m_uRuntimeID) != m_ListItemsMap.end()) { // If client is on the map we can proceed
		LVFINDINFO find = {};
		find.flags = LVFI_PARAM;
		find.lParam = (LPARAM)id.m_uRuntimeID;
		if (FindItem(&find) == -1) {
			const int iItem = InsertItem(LVIF_PARAM | LVIF_TEXT, FindSortedInsertIndex(id.m_uRuntimeID), LPSTR_TEXTCALLBACK, 0, 0, 0, (LPARAM)id.m_uRuntimeID);
			if (iItem >= 0)
				RequestQueueListRedrawForRange(iItem, iItem);
		}
	}
}

const bool CQueueListCtrl::IsFilteredOut(CUpDownClient* client)
{
	const CStringArray& rastrFilter = theApp.emuledlg->transferwnd->m_pwndTransfer->m_astrFilterQueueList;
	if (!rastrFilter.IsEmpty()) {
		// filtering is done by text only for all columns to keep it consistent and simple
		// for the user even if that doesn't allow complex filters
		// for example for a file size range - but this could be done at server search time already
		const CString& szFilterTarget(GetItemDisplayText(client, theApp.emuledlg->transferwnd->m_pwndTransfer->GetFilterColumnQueueList()));

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

void CQueueListCtrl::ShowSelectedUserDetails()
{
	CPoint point;
	if (!::GetCursorPos(&point))
		return;
	ScreenToClient(&point);
	int it = HitTest(point);
	if (it == -1)
		return;

	SetItemState(-1, 0, LVIS_SELECTED);
	SetItemState(it, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	SetSelectionMark(it);   // display selection mark correctly!

	CQueueClientReference clientRef;
	clientRef.Attach(AcquireRuntimeClient(static_cast<QueueClientItemID>(GetItemData(GetSelectionMark()))));
	CUpDownClient* client = clientRef.Get();
	if (client) {
		CClientDetailDialog dialog(client, this);
		dialog.DoModal();
	}
}

CObject* CQueueListCtrl::GetNextSelectableItem()
{
	const int iItemCount = GetItemCount();
	if (iItemCount < 2)
		return NULL;

	POSITION pos = GetFirstSelectedItemPosition();
	if (pos == NULL)
		return NULL;

	const int iSelectedItem = GetNextSelectedItem(pos);
	for (int iNewItem = iSelectedItem + 1; iNewItem < iItemCount; ++iNewItem) {
		CObject* pToken = CreateClientDetailWalkerToken(static_cast<QueueClientItemID>(GetItemData(iNewItem)));
		if (pToken == NULL)
			continue;

		SetItemState(iSelectedItem, 0, LVIS_SELECTED | LVIS_FOCUSED);
		SetItemState(iNewItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		SetSelectionMark(iNewItem);
		EnsureVisible(iNewItem, FALSE);
		return pToken;
	}

	return NULL;
}

CObject* CQueueListCtrl::GetPrevSelectableItem()
{
	const int iItemCount = GetItemCount();
	if (iItemCount < 2)
		return NULL;

	POSITION pos = GetFirstSelectedItemPosition();
	if (pos == NULL)
		return NULL;

	const int iSelectedItem = GetNextSelectedItem(pos);
	for (int iNewItem = iSelectedItem - 1; iNewItem >= 0; --iNewItem) {
		CObject* pToken = CreateClientDetailWalkerToken(static_cast<QueueClientItemID>(GetItemData(iNewItem)));
		if (pToken == NULL)
			continue;

		SetItemState(iSelectedItem, 0, LVIS_SELECTED | LVIS_FOCUSED);
		SetItemState(iNewItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		SetSelectionMark(iNewItem);
		EnsureVisible(iNewItem, FALSE);
		return pToken;
	}

	return NULL;
}

void CQueueListCtrl::ShowQueueClients()
{
	DeleteAllItems();
	m_ListItemsMap.clear();
	for (CUpDownClient *update = NULL; (update = theApp.uploadqueue->GetNextClient(update)) != NULL;)
		AddClient(update);
}

// Barry - Refresh the queue every 10 secs
void CALLBACK CQueueListCtrl::QueueUpdateTimer(HWND /*hwnd*/, UINT /*uiMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/) noexcept
{
	// NOTE: Always handle all type of MFC exceptions in TimerProcs - otherwise we'll get mem leaks
	try {
		if (thePrefs.GetUpdateQueueList()
			&& theApp.emuledlg->activewnd == theApp.emuledlg->transferwnd
			&& !theApp.IsClosing() // Don't do anything if the app is shutting down - can cause unhandled exceptions
			&& theApp.emuledlg->transferwnd->GetQueueList()->IsWindowVisible())
		{
			CUpDownClient *update = NULL;
			while ((update = theApp.uploadqueue->GetNextClient(update)) != NULL)
				theApp.QueueUploadClientRowsChanged(update, CemuleApp::UploadClientUiTargetQueueList);
		}
	}
	CATCH_DFLT_EXCEPTIONS(_T("CQueueListCtrl::QueueUpdateTimer"))
}

void CQueueListCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar == 'C' && GetKeyState(VK_CONTROL) < 0) {
		int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
		if (iSel >= 0) {
			CQueueClientReference clientRef;
			clientRef.Attach(AcquireRuntimeClient(static_cast<QueueClientItemID>(GetItemData(iSel))));
			CUpDownClient* client = clientRef.Get();
			if (client) {
				theApp.CopyTextToClipboard(md4str(client->GetUserHash()));
				theApp.emuledlg->statusbar->SetText(GetResString(_T("USER_HASH_COPIED_TO_CLIPBOARD")), SBarLog, 0);
				return;
			}
		}
	}

	if (nChar == 'X' && GetKeyState(VK_CONTROL) < 0) {
		int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
		if (iSel >= 0) {
			CQueueClientReference clientRef;
			clientRef.Attach(AcquireRuntimeClient(static_cast<QueueClientItemID>(GetItemData(iSel))));
			CUpDownClient* client = clientRef.Get();
			if (client) {
				CString m_strClientIpport;
				m_strClientIpport.Format(_T("%s:%u"), (LPCTSTR)ipstr(client->GetConnectIP()), client->GetUserPort());
				if (!m_strClientIpport.IsEmpty()) {
					theApp.CopyTextToClipboard(m_strClientIpport);
					theApp.emuledlg->statusbar->SetText(GetResString(_T("USER_IP_PORT_COPIED_TO_CLIPBOARD")), SBarLog, 0);
				}
			}
		}
		return;
	}

	CMuleListCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CQueueListCtrl::MaintainSortOrderAfterUpdate()
{
	if (!::IsWindow(m_hWnd) || GetSortItem() == -1)
		return;

	SetRedraw(false);
	SortItems(SortProc, MAKELONG(GetSortItem(), !GetSortAscending()));
	SetRedraw(true);
	Invalidate(FALSE);
}
