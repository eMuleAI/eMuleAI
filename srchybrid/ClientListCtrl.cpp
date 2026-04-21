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
#include "ClientListCtrl.h"
#include "MenuCmds.h"
#include "ClientDetailDialog.h"
#include "KademliaWnd.h"
#include "ClientList.h"
#include "emuledlg.h"
#include "FriendList.h"
#include "TransferDlg.h"
#include "MemDC.h"
#include "UpDownClient.h"
#include "ClientCredits.h"
#include "ListenSocket.h"
#include "ChatWnd.h"
#include "OtherFunctions.h"
#include "Friend.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "Kademlia/net/KademliaUDPListener.h"
#include "Preferences.h"
#include "eMuleAI/GeoLite2.h"
#include "Log.h"
#include "ListViewSearchDlg.h"
#include "MuleStatusBarCtrl.h"
#include "eMuleAI/Shield.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern UINT g_uMainThreadId;

LPARAM CClientListCtrl::m_pSortParam = NULL;

namespace
{
	const EListStateField kClientListViewState = static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL);
	const DWORD kClientListSetItemCountFlags = LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL;

	void RestoreClientListStateAfterStructureChange(CClientListCtrl& listCtrl)
	{
		listCtrl.RestoreListState(0, kClientListViewState, false, LRP_RestoreScroll_PreserveTopBottom);
	}

	void RefreshClientListAfterStructureChange(CClientListCtrl& listCtrl)
	{
		listCtrl.SetRedraw(true);
		listCtrl.Invalidate(FALSE);
	}

	CObject* CreateClientDetailWalkerToken(const DWORD uRuntimeID)
	{
		return uRuntimeID != 0 ? reinterpret_cast<CObject*>((static_cast<ULONG_PTR>(uRuntimeID) << 1) | 1) : NULL;
	}

	class ClientRuntimeReference
	{
	public:
		ClientRuntimeReference()
			: m_pClient(NULL)
		{
		}

		~ClientRuntimeReference()
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

	DWORD GetListedClientRuntimeID(const CClientListCtrl& listCtrl, int iItem)
	{
		return (iItem >= 0 && static_cast<size_t>(iItem) < listCtrl.m_ListedItemRuntimeIDs.size()) ? listCtrl.m_ListedItemRuntimeIDs[iItem] : 0;
	}

	bool LookupListedClientIndex(const CClientListCtrl& listCtrl, const CUpDownClient* pClient, const DWORD uRuntimeID, int& iItem, bool* pbPointerReuseConflict = NULL)
	{
		iItem = -1;
		if (pbPointerReuseConflict != NULL)
			*pbPointerReuseConflict = false;
		if (pClient == NULL)
			return false;

		if (!listCtrl.m_ListedItemsMap.Lookup(const_cast<CUpDownClient*>(pClient), iItem) || iItem < 0)
			return false;

		if (GetListedClientRuntimeID(listCtrl, iItem) == uRuntimeID)
			return true;

		if (pbPointerReuseConflict != NULL)
			*pbPointerReuseConflict = true;
		iItem = -1;
		return false;
	}

	bool IsExactListedClientEntry(const CClientListCtrl& listCtrl, const size_t iItem, const CUpDownClient* pClient, const DWORD uRuntimeID)
	{
		return iItem < listCtrl.m_ListedItemsVector.size()
			&& iItem < listCtrl.m_ListedItemRuntimeIDs.size()
			&& listCtrl.m_ListedItemsVector[iItem] == pClient
			&& listCtrl.m_ListedItemRuntimeIDs[iItem] == uRuntimeID;
	}

	CUpDownClient* AcquireListedClient(const CUpDownClient* pClient, DWORD uRuntimeID, ClientRuntimeReference& clientRef)
	{
		clientRef.Release();
		if (uRuntimeID == 0 || theApp.clientlist == NULL)
			return NULL;

		clientRef.Attach(theApp.clientlist->AcquireTrackedClientByRuntimeID((ClientRuntimeID)uRuntimeID));
		CUpDownClient* pTrackedClient = clientRef.Get();
		if (pTrackedClient == NULL)
			return NULL;

		if (pClient != NULL && pTrackedClient != pClient) {
			clientRef.Release();
			return NULL;
		}

		return pTrackedClient;
	}

	CUpDownClient* AcquireItemTrackedClient(CClientListCtrl& listCtrl, int iItem, ClientRuntimeReference& clientRef)
	{
		clientRef.Release();
		if (iItem < 0 || theApp.clientlist == NULL)
			return NULL;

		if (static_cast<size_t>(iItem) >= listCtrl.m_ListedItemsVector.size())
			return NULL;

		return AcquireListedClient(listCtrl.m_ListedItemsVector[iItem], GetListedClientRuntimeID(listCtrl, iItem), clientRef);
	}

	void UpdateClientListItemCount(CListCtrl& listCtrl, const size_t itemCount)
	{
		listCtrl.SetItemCountEx(static_cast<int>(itemCount), kClientListSetItemCountFlags);
	}

	struct ClientListCtrlRemoveMessage
	{
		CUpDownClient*	pClient;
		DWORD			uClientRuntimeID;
		DWORD			uArchivedRuntimeID;
	};

	struct ClientListCtrlRefreshMessage
	{
		DWORD	uClientRuntimeID;
		int		iIndex;
		uint32	uSortImpactFlags;
	};

	uint32 GetRefreshImpactMaskForSortColumn(const int iSortColumn)
	{
		switch (iSortColumn) {
		case 0:
			return CClientListCtrl::kSortImpactUserName;
		case 1:
			return CClientListCtrl::kSortImpactUploadState;
		case 2:
			return CClientListCtrl::kSortImpactTransferredUp;
		case 3:
			return CClientListCtrl::kSortImpactDownloadState;
		case 4:
			return CClientListCtrl::kSortImpactTransferredDown;
		case 5:
			return CClientListCtrl::kSortImpactSoftware;
		case 6:
			return CClientListCtrl::kSortImpactClientStatus;
		case 7:
			return CClientListCtrl::kSortImpactHash;
		case 8:
			return CClientListCtrl::kSortImpactIpPort;
		case 9:
			return CClientListCtrl::kSortImpactIpPort | CClientListCtrl::kSortImpactGeolocation;
		case 10:
		case 11:
		case 12:
			return CClientListCtrl::kSortImpactSharedFiles;
		case 13:
			return CClientListCtrl::kSortImpactFriend;
		case 14:
			return CClientListCtrl::kSortImpactIdType;
		case 15:
			return CClientListCtrl::kSortImpactPunishment;
		case 16:
			return CClientListCtrl::kSortImpactPunishment | CClientListCtrl::kSortImpactFriend;
		case 17:
			return CClientListCtrl::kSortImpactFirstSeen;
		case 18:
			return CClientListCtrl::kSortImpactLastSeen;
		case 19:
			return CClientListCtrl::kSortImpactNote;
		default:
			return CClientListCtrl::kSortImpactAll;
		}
	}

	std::vector<CUpDownClient*> SnapshotArchivedClients()
	{
		std::vector<CUpDownClient*> aArchivedClients;
		if (theApp.clientlist == NULL || !thePrefs.GetClientHistory())
			return aArchivedClients;

		CSingleLock runtimeMapLock(&theApp.clientlist->GetTrackedClientRuntimeMapsLock(), TRUE);
		aArchivedClients.reserve(static_cast<size_t>(theApp.clientlist->m_ArchivedClientsMap.GetCount()));
		for (POSITION pos = theApp.clientlist->m_ArchivedClientsMap.GetStartPosition(); pos != NULL;) {
			CString strHash;
			CUpDownClient* pArchivedClient = NULL;
			theApp.clientlist->m_ArchivedClientsMap.GetNextAssoc(pos, strHash, pArchivedClient);
			if (pArchivedClient != NULL)
				aArchivedClients.push_back(pArchivedClient);
		}
		return aArchivedClients;
	}

	bool ResolveArchivedClientReference(CUpDownClient* client, ClientRuntimeReference& archivedClientRef)
	{
		archivedClientRef.Release();
		if (client == NULL || theApp.clientlist == NULL || !thePrefs.GetClientHistory())
			return false;

		CUpDownClient* pArchivedClient = client->AcquireArchivedClient();
		if (pArchivedClient == NULL && !IsCorruptOrBadUserHash(client->GetUserHash()))
			pArchivedClient = theApp.clientlist->AcquireArchivedClientByUserHash(client->GetUserHash());
		archivedClientRef.Attach(pArchivedClient);

		pArchivedClient = archivedClientRef.Get();
		if (pArchivedClient == NULL || pArchivedClient == client || !pArchivedClient->m_bIsArchived) {
			archivedClientRef.Release();
			return false;
		}

		client->SetArchivedClientLink(pArchivedClient);
		return true;
	}

	bool IsPointerReusedByDifferentTrackedClient(const CUpDownClient* pClient, DWORD uExpectedRuntimeID)
	{
		if (pClient == NULL || uExpectedRuntimeID == 0 || theApp.clientlist == NULL)
			return false;

		CUpDownClient* pTrackedClient = theApp.clientlist->AcquireTrackedClientByPointer(pClient);
		if (pTrackedClient == NULL)
			return false;

		const bool bReused = pTrackedClient->GetRuntimeID() != uExpectedRuntimeID;
		pTrackedClient->ReleaseRuntimeReference();
		return bReused;
	}
}

IMPLEMENT_DYNAMIC(CClientListCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CClientListCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetDispInfo)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
	ON_WM_CONTEXTMENU()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_KEYDOWN()
	ON_WM_DESTROY()
	ON_MESSAGE(WM_CLIENTLISTCTRL_ADD_CLIENT, OnUiAddClient)
	ON_MESSAGE(WM_CLIENTLISTCTRL_REMOVE_CLIENT, OnUiRemoveClient)
	ON_MESSAGE(WM_CLIENTLISTCTRL_REFRESH_CLIENT, OnUiRefreshClient)
END_MESSAGE_MAP()

CClientListCtrl::CClientListCtrl()
	: CListCtrlItemWalk(this)
	, m_iDataSize(-1)
	, m_iCountryFlagCount()
{
	SetGeneralPurposeFind(true);
	SetSkinKey(_T("ClientsLv"));
}

CClientListCtrl::~CClientListCtrl() 
{ 
	m_IconList.DeleteImageList();
}

void CClientListCtrl::Init()
{
	SetPrefsKey(_T("ClientListCtrl"));
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	// Alignment rule: left for text, dates, and status labels; right for sizes, rates, counts, durations, and percentages.
	InsertColumn(0, EMPTY,	LVCFMT_LEFT,	DFLT_CLIENTNAME_COL_WIDTH);	//QL_USERNAME
	InsertColumn(1, EMPTY,	LVCFMT_LEFT,	100);						//CL_UPLOADSTATUS
	InsertColumn(2, EMPTY,	LVCFMT_RIGHT,	DFLT_SIZE_COL_WIDTH);		//CL_TRANSFUP
	InsertColumn(3, EMPTY,	LVCFMT_LEFT,	100);						//CL_DOWNLSTATUS
	InsertColumn(4, EMPTY,	LVCFMT_RIGHT,	DFLT_SIZE_COL_WIDTH);		//CL_TRANSFDOWN
	InsertColumn(5, EMPTY,	LVCFMT_LEFT,	DFLT_CLIENTSOFT_COL_WIDTH);	//CD_CSOFT
	InsertColumn(6, EMPTY,	LVCFMT_LEFT,	100);						//CLIENT_STATUS
	InsertColumn(7, EMPTY, LVCFMT_LEFT,	DFLT_HASH_COL_WIDTH);		//CD_UHASH
	InsertColumn(8, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(9, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(10, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(11, EMPTY, LVCFMT_RIGHT,	80);
	InsertColumn(12, EMPTY, LVCFMT_RIGHT,	100);
	InsertColumn(13, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(14, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(15, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(16, EMPTY, LVCFMT_LEFT,	100);
	InsertColumn(17, EMPTY, LVCFMT_RIGHT,	100);
	InsertColumn(18, EMPTY, LVCFMT_RIGHT,	100);
	InsertColumn(19, EMPTY, LVCFMT_LEFT,	100);

	SetAllIcons();
	LoadSettings();
	SetSortArrow();
	m_pSortParam = MAKELONG(GetSortItem(), !GetSortAscending());
	UpdateSortHistory(m_pSortParam); // This will save sort parameter history in m_liSortHistory which will be used when we call GetNextSortOrder.

	if (thePrefs.GetClientHistory()) {
		theApp.clientlist->LoadList();
		theApp.clientlist->m_tLastSaved = time(NULL); // Set initial value here.
	}
}

void CClientListCtrl::Localize()
{
	static const LPCTSTR uids[20] =
	{
		_T("QL_USERNAME"), _T("CL_UPLOADSTATUS"), _T("CL_TRANSFUP"), _T("CL_DOWNLSTATUS"), _T("CL_TRANSFDOWN")
		, _T("CD_CSOFT"), _T("CLIENT_STATUS")
		, _T("CD_UHASH2")
		, _T("IPPORT")
		, _T("GEOLOCATION")
		, _T("SHAREDFILESSTATUS")
		, _T("SHAREDFILESCOUNTCOLUMN")
		, _T("SHAREDFILESLASTQUERIED")
		, _T("FRIEND")
		, _T("ID_TYPE")
		, _T("BAD_CLIENT_TYPE")
		, _T("PUNISHMENT")
		, _T("FIRST_SEEN")
		, _T("LAST_SEEN")
		, _T("CLIENT_NOTE")
	};

	LocaliseHeaderCtrl(uids, _countof(uids));

	CString strRes(GetResString(_T("CD_UHASH")));
	strRes.Remove(_T(':'));
	HDITEM hdi;
	hdi.mask = HDI_TEXT;
	hdi.pszText = const_cast<LPTSTR>((LPCTSTR)strRes);
	GetHeaderCtrl()->SetItem(7, &hdi);
}

void CClientListCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
	SetAllIcons();
}

void CClientListCtrl::SetAllIcons()
{
	ApplyImageList(NULL);

	if (m_IconList.GetSafeHandle() != NULL) 
		m_IconList.DeleteImageList();

	
	if (m_IconList.GetSafeHandle() != NULL) // Ensure old image list is fully released before creating a new one.
		m_IconList.DeleteImageList();
	
	m_IconList.Create(FLAG_WIDTH, FLAG_HEIGHT, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1); // Create target image list up-front to avoid double Attach assertions.

	// Add flag icons
	CImageList* pFlagImageList = theApp.geolite2->GetFlagImageList();
	if (pFlagImageList != nullptr) {
		for (int i = 0; i < pFlagImageList->GetImageCount(); ++i) {
			HICON hIcon = pFlagImageList->ExtractIcon(i);
			m_IconList.Add(hIcon);
			::DestroyIcon(hIcon);  // Memory cleanup
		}
	}

	m_iCountryFlagCount = (pFlagImageList != nullptr) ? pFlagImageList->GetImageCount() : 0; // This will be used to as a base index to access the following images.

	// Add other images
	m_IconList.Add(CTempIconLoader(_T("ClientEDonkey")));			//0 - eDonkey
	m_IconList.Add(CTempIconLoader(_T("ClientEDonkeyPlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientCompatible")));		//2 - Compat
	m_IconList.Add(CTempIconLoader(_T("ClientCompatiblePlus")));
	m_IconList.Add(CTempIconLoader(_T("Friend")));					//4 - friend
	m_IconList.Add(CTempIconLoader(_T("ClientMLDonkey")));			//5 - ML
	m_IconList.Add(CTempIconLoader(_T("ClientMLDonkeyPlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientEDonkeyHybrid")));		//7 - Hybrid
	m_IconList.Add(CTempIconLoader(_T("ClientEDonkeyHybridPlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientShareaza")));			//9 - Shareaza
	m_IconList.Add(CTempIconLoader(_T("ClientShareazaPlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientAMule")));				//11 - amule
	m_IconList.Add(CTempIconLoader(_T("ClientAMulePlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientLPhant")));			//13 - Lphant
	m_IconList.Add(CTempIconLoader(_T("ClientLPhantPlus")));
	m_IconList.Add(CTempIconLoader(_T("Server")));					//15 - http source
	m_IconList.SetOverlayImage(m_IconList.Add(CTempIconLoader(_T("ClientSecureOvl"))), 1);
	m_IconList.SetOverlayImage(m_IconList.Add(CTempIconLoader(_T("OverlayObfu"))), 2);
	m_IconList.SetOverlayImage(m_IconList.Add(CTempIconLoader(_T("OverlaySecureObfu"))), 3);

	ApplyImageList(m_IconList); // Apply image list.
}

void CClientListCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{

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

	ClientRuntimeReference clientRef;
	CUpDownClient* client = AcquireListedClient(m_ListedItemsVector[index], GetListedClientRuntimeID(*this, index), clientRef);
	const bool bClientOk = client != NULL;

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
		rcItem.right = itemLeft + iColumnWidth - sm_iSubItemInset;
		if (rcItem.left < rcItem.right && HaveIntersection(rcClient, rcItem)) {
			const CString sItem(bClientOk ? GetItemDisplayText(client, iColumn) : CString());
			switch (iColumn) {
			case 9:
				{
					if (theApp.geolite2->ShowCountryFlag() && bClientOk) {
						POINT point2 = { rcItem.left,rcItem.top + 1 };
						theApp.geolite2->GetFlagImageList()->DrawIndirect(&theApp.geolite2->GetFlagImageDrawParams(dc, client->GetCountryFlagIndex(), point2));
						rcItem.left += 22;
					}
					rcItem.left += sm_iIconOffset;
					dc->DrawText(sItem, sItem.GetLength(), &rcItem, MLC_DT_TEXT);
				}
				break;
			case 0: //user name
				{
					if (bClientOk) {
						int iImage;
						UINT uOverlayImage;
						client->GetDisplayImage(iImage, uOverlayImage);
						rcItem.left = itemLeft + sm_iIconOffset;
						const POINT point = { rcItem.left, rcItem.top + iIconY };
						SafeImageListDraw(&m_IconList, dc, iImage + m_iCountryFlagCount, point, ILD_NORMAL | INDEXTOOVERLAYMASK(uOverlayImage)); // m_IconList has country flag icons before client icons.
						if (theApp.geolite2->ShowCountryFlag() && IsColumnHidden(9)) {
							rcItem.left += 20;
							POINT point2 = { rcItem.left,rcItem.top + 1 };
							theApp.geolite2->GetFlagImageList()->DrawIndirect(&theApp.geolite2->GetFlagImageDrawParams(dc, client->GetCountryFlagIndex(), point2));
							rcItem.left += sm_iSubItemInset;
						}
						rcItem.left += 17;
					}
				}
			default: //any text column
				rcItem.left += sm_iSubItemInset;
				dc.DrawText(sItem, -1, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
			}
		}
		itemLeft += iColumnWidth;
	}

	DrawFocusRect(dc, &lpDrawItemStruct->rcItem, (lpDrawItemStruct->itemState & ODS_FOCUS) != 0, bCtrlFocused, (lpDrawItemStruct->itemState & ODS_SELECTED) != 0);
}

CString CClientListCtrl::GetItemDisplayText(const CUpDownClient *client, int iSubItem) const
{
	CString sText;
	switch (iSubItem) {
	case 0: //user name
		if (client->GetUserName() != NULL)
			sText = client->GetUserName();
		else
			sText.Format(_T("(%s)"), (LPCTSTR)GetResString(_T("UNKNOWN")));
		break;
	case 1: //upload status
		sText = client->GetUploadStateDisplayString();
		break;
	case 2: //transferred up
		{
			const uint64 sessionUp = client->GetSessionUp();
			const uint64 totalUp = (client->credits != NULL) ? client->credits->GetUploadedTotal() : 0;
			// Prefer live session upload when available so active uTP uploads are visible immediately.
			if (sessionUp > 0) {
				if (totalUp > sessionUp)
					sText.Format(_T("%s (%s)"), (LPCTSTR)CastItoXBytes(sessionUp), (LPCTSTR)CastItoXBytes(totalUp));
				else
					sText = CastItoXBytes(sessionUp);
			} else if (client->credits != NULL) {
				sText = CastItoXBytes(totalUp);
			}
		}
		break;
	case 3: //download status
		sText = client->GetDownloadStateDisplayString();
		break;
	case 4: //transferred down
		if (client->credits != NULL)
			sText = CastItoXBytes(client->credits->GetDownloadedTotal());
		break;
	case 5: //software
		sText = client->DbgGetFullClientSoftVer();
		if (sText.IsEmpty())
			sText = GetResString(_T("UNKNOWN"));
		else
			sText = client->DbgGetFullClientSoftVer();
		break;
	case 6: //client status
		sText = client->GetClientStatus();
		break;
	case 7: //hash
		sText = md4str(client->GetUserHash());
		break;
	case 8:
		sText.Format(_T("%s:%u"), ipstr(!client->GetIP().IsNull() ? client->GetIP() : client->GetConnectIP()), client->GetUserPort());
		break;
	case 9:
		sText = client->GetGeolocationData();
		break;
	case 10:
		sText = client->GetSharedFilesStatusText();
		break;
	case 11:
		sText.Format(_T("%u"), client->m_uSharedFilesCount);
		break;
	case 12:
		if (client->m_tSharedFilesLastQueriedTime)
			sText.Format(_T("%s"), CastSecondsToHM((time(NULL) - client->m_tSharedFilesLastQueriedTime)));
		else
			sText = EMPTY;
		break;
	case 13:
		if (client->IsFriend())
			sText = GetResString(_T("YES"));
		else
			sText = GetResString(_T("NO"));
		break;
	case 14:
		if (client->HasLowID())
			sText = GetResString(_T("IDLOW"));
		else
			sText = GetResString(_T("IDHIGH"));
		break;
	case 15:
		sText = client->GetPunishmentReason();
		break;
	case 16:
		sText = client->GetPunishmentText();
		break;
	case 17:
		if (client->tFirstSeen)
			sText.Format(_T("%s"), CastSecondsToHM((time(NULL) - client->tFirstSeen)));
		else
			sText = _T("Unknown");
		break;
	case 18:
		if (client->tLastSeen)
			sText.Format(_T("%s"), CastSecondsToHM((time(NULL) - client->tLastSeen)));
		else
			sText = _T("Unknown");
		break;
	case 19:
		sText = client->m_strClientNote;
		break;
	}
	return sText;
}

int CClientListCtrl::GetDefaultPersistentInfoTipExtraLeftPadding(const SPersistentInfoTipContext& context) const
{
	if (!theApp.geolite2->ShowCountryFlag())
		return 0;

	if (context.iSubItem == 9)
		return 22 + sm_iIconOffset;

	if (context.iSubItem == 0 && IsColumnHidden(9))
		return 20 + sm_iSubItemInset;

	return 0;
}

void CClientListCtrl::OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult)
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
			if (rItem.iItem >= 0 && rItem.iItem < (int)m_ListedItemsVector.size()) {
				ClientRuntimeReference clientRef;
				CUpDownClient* cur_client = AcquireListedClient(m_ListedItemsVector[rItem.iItem], GetListedClientRuntimeID(*this, rItem.iItem), clientRef);
				if (cur_client != NULL && rItem.pszText && rItem.cchTextMax > 0)
					_tcsncpy_s(rItem.pszText, rItem.cchTextMax, GetItemDisplayText(cur_client, rItem.iSubItem), _TRUNCATE);
				else if (rItem.pszText && rItem.cchTextMax > 0)
					rItem.pszText[0] = _T('\0');
			}
		}
	}
	*pResult = 0;
}

void CClientListCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	bool sortAscending;
	if (GetSortItem() != pNMLV->iSubItem) {
		switch (pNMLV->iSubItem) {
		case 1: // Upload State
		case 2: // Uploaded Total
		case 4: // Downloaded Total
		case 5: // Client Software
		case 6: // Connected
			sortAscending = false;
			break;
		default:
			sortAscending = true;
		}
	} else
		sortAscending = !GetSortAscending();

	// Sort table
	UpdateSortHistory(MAKELONG(pNMLV->iSubItem, !sortAscending));
	SetSortArrow(pNMLV->iSubItem, sortAscending);
	m_pSortParam = MAKELONG(pNMLV->iSubItem, !sortAscending);
	ReloadList(true, kClientListViewState);
	*pResult = 0;
}

static int CompareClientListItems(const CUpDownClient* pClient1, DWORD uRuntimeID1, const CUpDownClient* pClient2, DWORD uRuntimeID2, const LPARAM lParamSort)
{
	ClientRuntimeReference item1Ref;
	ClientRuntimeReference item2Ref;
	CUpDownClient* item1 = AcquireListedClient(pClient1, uRuntimeID1, item1Ref);
	CUpDownClient* item2 = AcquireListedClient(pClient2, uRuntimeID2, item2Ref);
	const bool v1 = item1 != NULL;
	const bool v2 = item2 != NULL;

	if (!v1 && !v2) 
		return 0;

	if (!v1)
		return 1;

	if (!v2)
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
	case 1: //upload status
		iResult = item1->GetUploadState() - item2->GetUploadState();
		break;
	case 2: //transferred up
		if (item1->credits && item2->credits)
			iResult = CompareUnsigned(item1->credits->GetUploadedTotal(), item2->credits->GetUploadedTotal());
		else
			iResult = (item1->credits) ? 1 : -1;
		break;
	case 3: //download status
		if (item1->GetDownloadState() == item2->GetDownloadState()) {
			if (item1->IsRemoteQueueFull() && item2->IsRemoteQueueFull())
				iResult = 0;
			else if (item2->IsRemoteQueueFull())
				iResult = -1;
		} else
			iResult = item1->GetDownloadState() - item2->GetDownloadState();
		break;
	case 4: //transferred down
		if (item1->credits && item2->credits)
			iResult = CompareUnsigned(item1->credits->GetDownloadedTotal(), item2->credits->GetDownloadedTotal());
		else
			iResult = (item1->credits) ? 1 : -1;
		break;
	case 5: //software
		iResult = CompareLocaleStringNoCase(item1->DbgGetFullClientSoftVer(), item2->DbgGetFullClientSoftVer());
		break;
	case 6: // Client Status
		iResult = CompareLocaleStringNoCase(item1->GetClientStatus(), item2->GetClientStatus());
		break;
	case 7: //hash
		iResult = memcmp(item1->GetUserHash(), item2->GetUserHash(), 16);
		break;
	case 8:
		iResult = CompareIP(!item1->GetIP().IsNull() ? item1->GetIP() : item1->GetConnectIP(), !item2->GetIP().IsNull() ? item2->GetIP() : item2->GetConnectIP());
		if (iResult == 0)
			iResult = CompareUnsigned(item1->GetUserPort(), item2->GetUserPort());
		break;
	case 9:
		if (item1->GetGeolocationData(true) && item2->GetGeolocationData(true))
			iResult = CompareLocaleStringNoCase(item1->GetGeolocationData(true), item2->GetGeolocationData(true));
		else if (item1->GetGeolocationData(true))
			iResult = 1;
		else
			iResult = -1;
		break;
	case 10:
		iResult = CompareLocaleStringNoCase(item1->GetSharedFilesStatusText(), item2->GetSharedFilesStatusText());
		break;
	case 11:
		iResult = CompareUnsigned(item1->m_uSharedFilesCount, item2->m_uSharedFilesCount);
		break;
	case 12:
		iResult = CompareUnsigned(item1->m_tSharedFilesLastQueriedTime, item2->m_tSharedFilesLastQueriedTime);
		break;
	case 13:
		iResult = CompareUnsigned(item1->IsFriend(), item2->IsFriend());
		break;
	case 14:
		iResult = CompareUnsigned(item1->HasLowID(), item2->HasLowID());
		break;
	case 15:
		iResult = CompareLocaleStringNoCase(item1->GetPunishmentReason(), item2->GetPunishmentReason());
		break;
	case 16:
		iResult = CompareLocaleStringNoCase(item1->GetPunishmentText(), item2->GetPunishmentText());
		break;
	case 17:
		iResult = CompareUnsigned(item1->tFirstSeen, item2->tFirstSeen);
		break;
	case 18:
		iResult = CompareUnsigned(item1->tLastSeen, item2->tLastSeen);
		break;
	case 19:
		iResult = CompareLocaleStringNoCase(item1->m_strClientNote, item2->m_strClientNote);
		break;
	}

	if (HIWORD(lParamSort))
		iResult = -iResult;

	// Call secondary sort order, if the first one resulted as equal
	if (iResult == 0) {
		LPARAM iNextSort = theApp.emuledlg->transferwnd->GetClientList()->GetNextSortOrder(lParamSort);
		if (iNextSort != -1)
			iResult = CompareClientListItems(pClient1, uRuntimeID1, pClient2, uRuntimeID2, iNextSort);
	}

	return iResult;
}

int CALLBACK CClientListCtrl::SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort)
{
	CClientListCtrl* pClientListCtrl = theApp.emuledlg != NULL ? theApp.emuledlg->transferwnd->GetClientList() : NULL;
	const CUpDownClient* pClient1 = reinterpret_cast<CUpDownClient*>(lParam1);
	const CUpDownClient* pClient2 = reinterpret_cast<CUpDownClient*>(lParam2);
	DWORD uRuntimeID1 = 0;
	DWORD uRuntimeID2 = 0;
	int iIndex = -1;
	if (pClientListCtrl != NULL) {
		if (pClientListCtrl->m_ListedItemsMap.Lookup(const_cast<CUpDownClient*>(pClient1), iIndex))
			uRuntimeID1 = GetListedClientRuntimeID(*pClientListCtrl, iIndex);
		iIndex = -1;
		if (pClientListCtrl->m_ListedItemsMap.Lookup(const_cast<CUpDownClient*>(pClient2), iIndex))
			uRuntimeID2 = GetListedClientRuntimeID(*pClientListCtrl, iIndex);
	}
	return CompareClientListItems(pClient1, uRuntimeID1, pClient2, uRuntimeID2, lParamSort);
}

void CClientListCtrl::OnNmDblClk(LPNMHDR, LRESULT *pResult)
{
	ShowSelectedUserDetails();
	*pResult = 0;
}

void CClientListCtrl::OnContextMenu(CWnd*, CPoint point)
{
	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	ClientRuntimeReference clientRef;
	const CUpDownClient *client = AcquireItemTrackedClient(*this, iSel, clientRef);
	const bool is_ed2k = client && client->IsEd2kClient();

	CMenuXP ClientMenu;
	ClientMenu.CreatePopupMenu();
	ClientMenu.AddMenuSidebar(GetResString(_T("CLIENTS")));
	ClientMenu.AppendMenu(MF_STRING | (client ? MF_ENABLED : MF_GRAYED), MP_DETAIL, GetResString(_T("SHOWDETAILS")), _T("CLIENTDETAILS"));
	ClientMenu.SetDefaultItem(MP_DETAIL);
	ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && !client->IsFriend()) ? MF_ENABLED : MF_GRAYED), MP_ADDFRIEND, GetResString(_T("ADDFRIEND")), _T("ADDFRIEND"));
	ClientMenu.AppendMenu(MF_STRING | (is_ed2k ? MF_ENABLED : MF_GRAYED), MP_MESSAGE, GetResString(_T("SEND_MSG")), _T("SENDMESSAGE"));
	ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && client->GetViewSharedFilesSupport()) ? MF_ENABLED : MF_GRAYED), MP_SHOWLIST, GetResString(_T("VIEWFILES")), _T("VIEWFILES"));
	ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && client->GetViewSharedFilesSupport() && (client->m_bIsArchived || !client->socket || !client->socket->IsConnected())) ? MF_ENABLED : MF_GRAYED), MP_SHOWLIST_AUTO_QUERY, GetResString(_T("VIEW_FILES_ACTIVATE_AUTO_QUERY")), _T("CLOCKGREEN"));
	if (client == NULL)
		ClientMenu.AppendMenu(MF_STRING | MF_GRAYED, MP_ACTIVATE_AUTO_QUERY, GetResString(_T("ACTIVATE_AUTO_QUERY")), _T("CLOCKBLUE"));
	else if (client->m_bAutoQuerySharedFiles)
		ClientMenu.AppendMenu(MF_STRING | MF_ENABLED, MP_DEACTIVATE_AUTO_QUERY, GetResString(_T("DEACTIVATE_AUTO_QUERY")), _T("CLOCKRED"));
	else
		ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && client->GetViewSharedFilesSupport() && (client->m_bIsArchived || !client->socket || !client->socket->IsConnected())) ? MF_ENABLED : MF_GRAYED), MP_ACTIVATE_AUTO_QUERY, GetResString(_T("ACTIVATE_AUTO_QUERY")), _T("CLOCKBLUE"));
	ClientMenu.AppendMenu(MF_STRING | (client ? MF_ENABLED : MF_GRAYED), MP_EDIT_NOTE, GetResString(_T("EDIT_CLIENT_NOTE")), _T("RENAME"));
	if (Kademlia::CKademlia::IsRunning() && !Kademlia::CKademlia::IsConnected())
		ClientMenu.AppendMenu(MF_STRING | ((is_ed2k && client->GetKadPort() && client->GetKadVersion() >= KADEMLIA_VERSION2_47a) ? MF_ENABLED : MF_GRAYED), MP_BOOT, GetResString(_T("BOOTSTRAP")), _T("KADBOOTSTRAP"));

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

BOOL CClientListCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	wParam = LOWORD(wParam);

	if (wParam == MP_FIND) {
		OnFindStart();
		return TRUE;
	}
	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
		if (iSel >= 0) {
			ClientRuntimeReference clientRef;
			CUpDownClient *client = AcquireItemTrackedClient(*this, iSel, clientRef);
			if (client == NULL)
				return TRUE;

			auto RefreshQueueCountAfterManualPunishment = []() {
				if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL)
					theApp.emuledlg->transferwnd->InvalidateQueueCount(true);
			};

			switch (wParam) {
		case MP_SHOWLIST:
		{
			CUpDownClient* NewClient = ArchivedToActive(client);
			if (NewClient && (client == NewClient || theApp.clientlist->IsValidClient(NewClient)))
				NewClient->RequestSharedFileList();
			break;
		}
		case MP_MESSAGE:
		{
			CUpDownClient* NewClient = ArchivedToActive(client);
			if (NewClient && (client == NewClient || theApp.clientlist->IsValidClient(NewClient)))
				theApp.emuledlg->chatwnd->StartSession(NewClient);
		}
			break;
		case MP_ADDFRIEND:
			if (theApp.friendlist->AddFriend(client))
				Update(iSel);
			break;
			case MP_UNBAN:
				if (client->IsBanned() || client->IsBadClient()) {
					client->UnBan();
					Update(iSel);
					RefreshQueueCountAfterManualPunishment();
				}
				break;
		case MP_DETAIL:
		case MPG_ALTENTER:
		case IDA_ENTER:
		{
			CClientDetailDialog dialog(client, this);
			clientRef.Release();
			dialog.DoModal();
			break;
		}
		case MP_BOOT:
			if (client->GetKadPort() && client->GetKadVersion() >= KADEMLIA_VERSION2_47a)
				Kademlia::CKademlia::Bootstrap(client->GetIPv4().ToUInt32(true), client->GetKadPort());
			break;
		case MP_COPYSELECTED:
		{
			theApp.CopyTextToClipboard(md4str(client->GetUserHash()));
			theApp.emuledlg->statusbar->SetText(GetResString(_T("USER_HASH_COPIED_TO_CLIPBOARD")), SBarLog, 0);
			break;
		}
		case MP_CUT:
		{
			CString m_strClientIpport;
			m_strClientIpport.Format(_T("%s:%u"), ipstr(client->GetConnectIP()), client->GetUserPort());
			if (!m_strClientIpport.IsEmpty()) {
				theApp.CopyTextToClipboard(m_strClientIpport);
				theApp.emuledlg->statusbar->SetText(GetResString(_T("USER_IP_PORT_COPIED_TO_CLIPBOARD")), SBarLog, 0);
			}
			break;
		}
		case MP_SHOWLIST_AUTO_QUERY:
		{
			client->SetAutoQuerySharedFiles(true);
			CUpDownClient* NewClient = ArchivedToActive(client);
			if (NewClient && (client == NewClient || theApp.clientlist->IsValidClient(NewClient)))
				NewClient->RequestSharedFileList();
			break;
		}
		case MP_ACTIVATE_AUTO_QUERY:
			client->SetAutoQuerySharedFiles(true);
			break;
		case MP_DEACTIVATE_AUTO_QUERY:
			client->SetAutoQuerySharedFiles(false);
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


void CClientListCtrl::AddClient(CUpDownClient* client)
{
	if (client == NULL)
		return;

	if (GetCurrentThreadId() != g_uMainThreadId) {
		if (::IsWindow(m_hWnd) && client->GetRuntimeID() != 0)
			PostMessage(WM_CLIENTLISTCTRL_ADD_CLIENT, (WPARAM)client->GetRuntimeID(), 0);
		return;
	}

	AddClientInternal(client);
}

void CClientListCtrl::AddClientInternal(CUpDownClient* client)
{
	int m_iIndex = -1;
	ASSERT(GetCurrentThreadId() == g_uMainThreadId);
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible() || thePrefs.m_bFreezeChecked || !client || IsFilteredOut(client))
		return;

	bool bPointerReuseConflict = false;
	if (LookupListedClientIndex(*this, client, client->GetRuntimeID(), m_iIndex, &bPointerReuseConflict) && m_iIndex >= 0)
		return;
	if (bPointerReuseConflict) {
		ReloadList(false, kClientListViewState);
		return;
	}

	SaveListState(0, kClientListViewState); // Save selections and scroll state

	m_iIndex = -1;
	ClientRuntimeReference archivedClientRef;
	CUpDownClient* pArchivedClient = ResolveArchivedClientReference(client, archivedClientRef) ? archivedClientRef.Get() : NULL;
	bool bArchivedPointerReuseConflict = false;

	if (thePrefs.GetClientHistory() && !client->m_bIsArchived && // If this's an active client
		pArchivedClient != NULL // and if we found it's archived version
		&& LookupListedClientIndex(*this, pArchivedClient, pArchivedClient->GetRuntimeID(), m_iIndex, &bArchivedPointerReuseConflict) && m_iIndex >= 0) { // and archived client is already listed
		SetRedraw(false); // Suspend painting
		m_ListedItemsVector[m_iIndex] = client; // Replace archived client with active client at archived client's position of vector.
		if (static_cast<size_t>(m_iIndex) < m_ListedItemRuntimeIDs.size())
			m_ListedItemRuntimeIDs[m_iIndex] = client->GetRuntimeID();
		m_ListedItemsMap[client] = m_iIndex; // Add active client to map.
		m_ListedItemsMap.RemoveKey(pArchivedClient); // Remove archived client from map.
		if (client == pArchivedClient)
			client = client; // If archived client was selected, select active client.
		RestoreListState(0, kClientListViewState, false); // Restore selections and scroll state
		SetRedraw(true); // Resume painting
		Update(m_iIndex); // Redraw updated item.
	} else {
		if (bArchivedPointerReuseConflict) {
			ReloadList(false, kClientListViewState);
			return;
		}
		m_iIndex = 0;
		while (m_iIndex < static_cast<int>(m_ListedItemsVector.size())
			&& CompareClientListItems(m_ListedItemsVector[m_iIndex], GetListedClientRuntimeID(*this, m_iIndex), client, client->GetRuntimeID(), m_pSortParam) < 0)
		{
			++m_iIndex;
		}

		// Enlarge and update map starting from the found index to the end.
		if (m_iIndex >= 0) {
			SetRedraw(false); // Suspend painting
			m_ListedItemsVector.insert(m_ListedItemsVector.begin() + m_iIndex, client); // Insert the new value at the determined position
			m_ListedItemRuntimeIDs.insert(m_ListedItemRuntimeIDs.begin() + m_iIndex, client->GetRuntimeID());
			RebuildListedItemsMap();
			UpdateClientListItemCount(*this, m_ListedItemsVector.size()); // Set current count for virtual list.
			RestoreClientListStateAfterStructureChange(*this);
			RefreshClientListAfterStructureChange(*this);
			theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
		} else { // This case is not expected, but handled for robustness.
			ReloadList(false, kClientListViewState); // Something is wrong at this point, let's do a full reload instead having possible glitches or crashes.
			return;
		}
	}
}

void CClientListCtrl::RemoveClient(CUpDownClient* client)
{
	if (client == NULL)
		return;

	if (GetCurrentThreadId() != g_uMainThreadId) {
		if (::IsWindow(m_hWnd)) {
			ClientListCtrlRemoveMessage* pMessage = new ClientListCtrlRemoveMessage;
			pMessage->pClient = client;
			pMessage->uClientRuntimeID = client->GetRuntimeID();
			pMessage->uArchivedRuntimeID = client->GetArchivedClientRuntimeID();
			if (!PostMessage(WM_CLIENTLISTCTRL_REMOVE_CLIENT, (WPARAM)pMessage, 0))
				delete pMessage;
		}
		return;
	}

	RemoveClientInternal(client);
}

void CClientListCtrl::RemoveClientInternal(CUpDownClient* client)
{
	int m_iIndex = -1;
	int m_iArchivedIndex = -1;

	ASSERT(GetCurrentThreadId() == g_uMainThreadId);
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible() || !client)
		return;

	bool bPointerReuseConflict = false;
	if (!LookupListedClientIndex(*this, client, client->GetRuntimeID(), m_iIndex, &bPointerReuseConflict) || m_iIndex < 0) {
		if (bPointerReuseConflict)
			ReloadList(false, kClientListViewState);
		return;
	}

	SaveListState(0, kClientListViewState); // Save selections and scroll state
	SetRedraw(false); // Suspend painting

	ClientRuntimeReference archivedClientRef;
	CUpDownClient* pArchivedClient = ResolveArchivedClientReference(client, archivedClientRef) ? archivedClientRef.Get() : NULL;
	bool bArchivedPointerReuseConflict = false;
	const bool bArchivedAlreadyListed = pArchivedClient != NULL
		&& LookupListedClientIndex(*this, pArchivedClient, pArchivedClient->GetRuntimeID(), m_iArchivedIndex, &bArchivedPointerReuseConflict);

	if (thePrefs.GetClientHistory() && !client->m_bIsArchived && // If this is an active client
		pArchivedClient != NULL // and if we found it's archived version
		&& !bArchivedAlreadyListed && !IsFilteredOut(pArchivedClient)) { // and archived client is not already listed, also it will not be filtered if we add it.
		int iSelectedClientIndex = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
		const CUpDownClient* SelectedClient = (iSelectedClientIndex >= 0) ? reinterpret_cast<CUpDownClient*>(GetItemData(iSelectedClientIndex)) : NULL;
		m_ListedItemsVector[m_iIndex] = pArchivedClient; // Replace active client with archived client at active client's position of vector.
		if (static_cast<size_t>(m_iIndex) < m_ListedItemRuntimeIDs.size())
			m_ListedItemRuntimeIDs[m_iIndex] = pArchivedClient->GetRuntimeID();
		m_ListedItemsMap[pArchivedClient] = m_iIndex; // Add archived client to map.
		m_ListedItemsMap.RemoveKey(client); // Remove active client from map.
		if (SelectedClient && SelectedClient == client) { // This was selected client and we need to switch to the archived client now.
			SetItemState(-1, 0, LVIS_SELECTED); // Remove selection from all items.
			SetItemState(m_iIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); // Select archived client.
		} else 
			RestoreListState(0, kClientListViewState, false); // Restore selections and scroll state
		SetRedraw(true); // Resume painting
		RedrawItems(m_iIndex, m_iIndex); // Redraw updated item.
	} else {
		if (bArchivedPointerReuseConflict) {
			SetRedraw(true);
			ReloadList(false, kClientListViewState);
			return;
		}
		m_ListedItemsMap.RemoveKey(client); // Remove the item from the map
		m_ListedItemsVector.erase(m_ListedItemsVector.begin() + m_iIndex); // Remove the item from the vector.
		if (static_cast<size_t>(m_iIndex) < m_ListedItemRuntimeIDs.size())
			m_ListedItemRuntimeIDs.erase(m_ListedItemRuntimeIDs.begin() + m_iIndex);
		RebuildListedItemsMap();
		UpdateClientListItemCount(*this, m_ListedItemsVector.size()); // Set current count for virtual list.
		RestoreClientListStateAfterStructureChange(*this);
		RefreshClientListAfterStructureChange(*this);
		theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
	}
}

void CClientListCtrl::RefreshClient(CUpDownClient* client, const int iIndex, const uint32 uSortImpactFlags)
{
	if (client == NULL)
		return;

	if (GetCurrentThreadId() != g_uMainThreadId) {
		if (::IsWindow(m_hWnd) && client->GetRuntimeID() != 0) {
			ClientListCtrlRefreshMessage* pMessage = new ClientListCtrlRefreshMessage{ client->GetRuntimeID(), iIndex, uSortImpactFlags };
			if (!PostMessage(WM_CLIENTLISTCTRL_REFRESH_CLIENT, reinterpret_cast<WPARAM>(pMessage), 0))
				delete pMessage;
		}
		return;
	}

	RefreshClientInternal(client, iIndex, uSortImpactFlags);
}

void CClientListCtrl::RefreshClientInternal(CUpDownClient* client, const int iIndex, const uint32 uSortImpactFlags)
{
	int m_iIndex = iIndex;
	ASSERT(GetCurrentThreadId() == g_uMainThreadId);
	// If index isn't provided by the input parameter and also not found in m_ListedItemsMap
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible() || !client)
		return;

	const bool bShouldBeListed = !IsFilteredOut(client);
	bool bPointerReuseConflict = false;
	if (iIndex == -1) {
		if (!LookupListedClientIndex(*this, client, client->GetRuntimeID(), m_iIndex, &bPointerReuseConflict) || m_iIndex < 0) {
			if (bPointerReuseConflict)
				ReloadList(false, kClientListViewState);
			else if (bShouldBeListed)
				AddClientInternal(client);
			return;
		}
	} else if (m_iIndex < 0 || static_cast<size_t>(m_iIndex) >= m_ListedItemsVector.size()) {
		return;
	} else if (m_ListedItemsVector[m_iIndex] != client || GetListedClientRuntimeID(*this, m_iIndex) != client->GetRuntimeID()) {
		ReloadList(false, kClientListViewState);
		return;
	}

	if (!bShouldBeListed) {
		RemoveClientInternal(client);
		return;
	}

	// Use client-bucket throttle instead of a single global gate to avoid starving other rows.
	static DWORD adwLastUpdate[256] = { 0 };
	const DWORD dwNow = GetTickCount();
	const DWORD dwUpdatePeriod = static_cast<DWORD>(thePrefs.GetUITweaksListUpdatePeriod());
	const UINT_PTR uClientPtr = reinterpret_cast<UINT_PTR>(client);
	const size_t uBucket = static_cast<size_t>((uClientPtr >> 4) & 0xFF);
	if (ShouldMaintainSortOrderOnUpdate() && IsSortOrderAffectedByRefresh(uSortImpactFlags) && RepositionUpdatedClient(m_iIndex)) {
		adwLastUpdate[uBucket] = dwNow;
		return;
	}

	if (dwNow - adwLastUpdate[uBucket] < dwUpdatePeriod)
		return;

	adwLastUpdate[uBucket] = dwNow;
	Update(m_iIndex); // Redraw updated item.
}

bool CClientListCtrl::IsSortOrderAffectedByRefresh(const uint32 uSortImpactFlags) const
{
	if (uSortImpactFlags == kSortImpactAll)
		return GetSortItem() != -1;
	if (uSortImpactFlags == kSortImpactNone || GetSortItem() == -1)
		return false;

	LPARAM lSortOrder = m_pSortParam;
	for (int iVisitedSorts = 0; lSortOrder != -1 && iVisitedSorts < 20; ++iVisitedSorts) {
		if ((GetRefreshImpactMaskForSortColumn(LOWORD(lSortOrder)) & uSortImpactFlags) != 0)
			return true;
		lSortOrder = GetNextSortOrder(lSortOrder);
	}

	return false;
}

bool CClientListCtrl::RepositionUpdatedClient(int iIndex)
{
	if (iIndex < 0 || static_cast<size_t>(iIndex) >= m_ListedItemsVector.size() || GetSortItem() == -1)
		return false;

	CUpDownClient* pUpdatedClient = m_ListedItemsVector[iIndex];
	const DWORD uUpdatedRuntimeID = GetListedClientRuntimeID(*this, iIndex);
	if (pUpdatedClient == NULL || uUpdatedRuntimeID == 0)
		return false;

	const int iLastIndex = static_cast<int>(m_ListedItemsVector.size()) - 1;
	if (iLastIndex <= 0)
		return false;

	const bool bShouldMoveUp = iIndex > 0
		&& CompareClientListItems(m_ListedItemsVector[iIndex - 1], GetListedClientRuntimeID(*this, iIndex - 1), pUpdatedClient, uUpdatedRuntimeID, m_pSortParam) > 0;
	const bool bShouldMoveDown = iIndex < iLastIndex
		&& CompareClientListItems(pUpdatedClient, uUpdatedRuntimeID, m_ListedItemsVector[iIndex + 1], GetListedClientRuntimeID(*this, iIndex + 1), m_pSortParam) > 0;

	if (bShouldMoveUp && bShouldMoveDown) {
		ReloadList(true, kClientListViewState);
		return true;
	}

	if (!bShouldMoveUp && !bShouldMoveDown)
		return false;

	int iInsertIndex = iIndex;
	if (bShouldMoveUp) {
		while (iInsertIndex > 0
			&& CompareClientListItems(m_ListedItemsVector[iInsertIndex - 1], GetListedClientRuntimeID(*this, iInsertIndex - 1), pUpdatedClient, uUpdatedRuntimeID, m_pSortParam) > 0)
		{
			--iInsertIndex;
		}
	} else {
		while (iInsertIndex < iLastIndex
			&& CompareClientListItems(pUpdatedClient, uUpdatedRuntimeID, m_ListedItemsVector[iInsertIndex + 1], GetListedClientRuntimeID(*this, iInsertIndex + 1), m_pSortParam) > 0)
		{
			++iInsertIndex;
		}
	}

	if (iInsertIndex == iIndex)
		return false;

	SaveListState(0, kClientListViewState);
	SetRedraw(false);

	m_ListedItemsVector.erase(m_ListedItemsVector.begin() + iIndex);
	if (static_cast<size_t>(iIndex) < m_ListedItemRuntimeIDs.size())
		m_ListedItemRuntimeIDs.erase(m_ListedItemRuntimeIDs.begin() + iIndex);

	m_ListedItemsVector.insert(m_ListedItemsVector.begin() + iInsertIndex, pUpdatedClient);
	m_ListedItemRuntimeIDs.insert(m_ListedItemRuntimeIDs.begin() + iInsertIndex, uUpdatedRuntimeID);
	RebuildListedItemsMap();

	RestoreClientListStateAfterStructureChange(*this);
	RefreshClientListAfterStructureChange(*this);
	return true;
}

void CClientListCtrl::RemoveClientByPointer(CUpDownClient* client, DWORD uClientRuntimeID, DWORD uArchivedRuntimeID)
{
	int m_iIndex = -1;
	int m_iArchivedIndex = -1;

	ASSERT(GetCurrentThreadId() == g_uMainThreadId);
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible() || !client || uClientRuntimeID == 0)
		return;

	bool bPointerReuseConflict = false;
	if (!LookupListedClientIndex(*this, client, uClientRuntimeID, m_iIndex, &bPointerReuseConflict) || m_iIndex < 0) {
		if (bPointerReuseConflict)
			ReloadList(false, kClientListViewState);
		return;
	}

	if (IsPointerReusedByDifferentTrackedClient(client, uClientRuntimeID)) {
		ReloadList(false, kClientListViewState);
		return;
	}

	SaveListState(0, kClientListViewState);
	SetRedraw(false);

	ClientRuntimeReference archivedClientRef;
	if (uArchivedRuntimeID != 0 && theApp.clientlist != NULL)
		archivedClientRef.Attach(theApp.clientlist->AcquireTrackedClientByRuntimeID(uArchivedRuntimeID));
	CUpDownClient* pArchivedClient = archivedClientRef.Get();
	bool bArchivedPointerReuseConflict = false;
	const bool bArchivedAlreadyListed = pArchivedClient != NULL
		&& LookupListedClientIndex(*this, pArchivedClient, pArchivedClient->GetRuntimeID(), m_iArchivedIndex, &bArchivedPointerReuseConflict);

	if (pArchivedClient != NULL && pArchivedClient != client && pArchivedClient->m_bIsArchived
		&& !bArchivedAlreadyListed && !IsFilteredOut(pArchivedClient)) {
		const int iSelectedClientIndex = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
		const CUpDownClient* pSelectedClient = (iSelectedClientIndex >= 0) ? reinterpret_cast<CUpDownClient*>(GetItemData(iSelectedClientIndex)) : NULL;
		m_ListedItemsVector[m_iIndex] = pArchivedClient;
		if (static_cast<size_t>(m_iIndex) < m_ListedItemRuntimeIDs.size())
			m_ListedItemRuntimeIDs[m_iIndex] = pArchivedClient->GetRuntimeID();
		m_ListedItemsMap[pArchivedClient] = m_iIndex;
		m_ListedItemsMap.RemoveKey(client);
		if (pSelectedClient == client) {
			SetItemState(-1, 0, LVIS_SELECTED);
			SetItemState(m_iIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		} else
			RestoreListState(0, kClientListViewState, false);
		SetRedraw(true);
		RedrawItems(m_iIndex, m_iIndex);
	} else {
		if (bArchivedPointerReuseConflict) {
			SetRedraw(true);
			ReloadList(false, kClientListViewState);
			return;
		}
		m_ListedItemsMap.RemoveKey(client);
		m_ListedItemsVector.erase(m_ListedItemsVector.begin() + m_iIndex);
		if (static_cast<size_t>(m_iIndex) < m_ListedItemRuntimeIDs.size())
			m_ListedItemRuntimeIDs.erase(m_ListedItemRuntimeIDs.begin() + m_iIndex);
		RebuildListedItemsMap();
		UpdateClientListItemCount(*this, m_ListedItemsVector.size());
		RestoreClientListStateAfterStructureChange(*this);
		RefreshClientListAfterStructureChange(*this);
		theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
	}
}

LRESULT CClientListCtrl::OnUiAddClient(WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	ClientRuntimeReference clientRef;
	if (theApp.clientlist != NULL)
		clientRef.Attach(theApp.clientlist->AcquireTrackedClientByRuntimeID((ClientRuntimeID)wParam));
	if (clientRef.Get() != NULL)
		AddClientInternal(clientRef.Get());
	return 0;
}

LRESULT CClientListCtrl::OnUiRemoveClient(WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	ClientListCtrlRemoveMessage* pMessage = reinterpret_cast<ClientListCtrlRemoveMessage*>(wParam);
	if (pMessage != NULL) {
		RemoveClientByPointer(pMessage->pClient, pMessage->uClientRuntimeID, pMessage->uArchivedRuntimeID);
		delete pMessage;
	}
	return 0;
}

LRESULT CClientListCtrl::OnUiRefreshClient(WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	ClientListCtrlRefreshMessage* pMessage = reinterpret_cast<ClientListCtrlRefreshMessage*>(wParam);
	if (pMessage == NULL)
		return 0;

	ClientRuntimeReference clientRef;
	if (theApp.clientlist != NULL)
		clientRef.Attach(theApp.clientlist->AcquireTrackedClientByRuntimeID((ClientRuntimeID)pMessage->uClientRuntimeID));
	if (clientRef.Get() != NULL)
		RefreshClientInternal(clientRef.Get(), pMessage->iIndex, pMessage->uSortImpactFlags);
	delete pMessage;
	return 0;
}

void CClientListCtrl::OnDestroy()
{
	MSG msg = {};
	while (::PeekMessage(&msg, m_hWnd, WM_CLIENTLISTCTRL_REMOVE_CLIENT, WM_CLIENTLISTCTRL_REMOVE_CLIENT, PM_REMOVE)) {
		ClientListCtrlRemoveMessage* pMessage = reinterpret_cast<ClientListCtrlRemoveMessage*>(msg.wParam);
		delete pMessage;
	}
	while (::PeekMessage(&msg, m_hWnd, WM_CLIENTLISTCTRL_REFRESH_CLIENT, WM_CLIENTLISTCTRL_REFRESH_CLIENT, PM_REMOVE)) {
		ClientListCtrlRefreshMessage* pMessage = reinterpret_cast<ClientListCtrlRefreshMessage*>(msg.wParam);
		delete pMessage;
	}

	CMuleListCtrl::OnDestroy();
}

void CClientListCtrl::ReloadList(const bool bSortCurrentList, const EListStateField LsfFlag)
{
	if (thePrefs.IsKnownClientListDisabled() || theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->transferwnd || !IsWindowVisible())
		return;

	CWaitCursor curWait;
	bool bInitializing = (m_iDataSize == -1); // Check if this is the first call to ReloadList

	// Initializing the vector and map
	if (bInitializing) {
		const std::vector<CUpDownClient*> aArchivedClients = SnapshotArchivedClients();
		m_iDataSize = NextPrime(theApp.clientlist->list.GetCount() + (int)aArchivedClients.size() + 10000); // Any reasonable prime number for the initial size.
		m_ListedItemsVector.reserve(m_iDataSize);
		m_ListedItemsMap.InitHashTable(m_iDataSize);
	} else
		SaveListState(0, LsfFlag); // Save selections, sort and scroll values for the previous m_nResultsID if this is not the first call.

	SetRedraw(false); // Suspend painting

	CCKey bufKey;
	CUpDownClient* cur_client = NULL;

	if (!bSortCurrentList) {
		//Clear and reload data
		m_ListedItemsVector.clear();
		m_ListedItemRuntimeIDs.clear();
		m_ListedItemsMap.RemoveAll();

		// Clients in the history
		if (thePrefs.GetClientHistory()) {
			const std::vector<CUpDownClient*> aArchivedClients = SnapshotArchivedClients();
			for (size_t i = 0; i < aArchivedClients.size(); ++i) {
				CUpDownClient* pArchivedClient = aArchivedClients[i];
				if (pArchivedClient != NULL && !IsFilteredOut(pArchivedClient)) {
					m_ListedItemsVector.push_back(pArchivedClient);
					m_ListedItemRuntimeIDs.push_back(pArchivedClient->GetRuntimeID());
				}
			}
		}

		// Current clients
		for (POSITION pos = theApp.clientlist->list.GetHeadPosition(); pos != NULL;) {
			CUpDownClient* cur_client = theApp.clientlist->list.GetNext(pos);
			if (cur_client) { // If this client is already in history, move data to the current client object and dont include history item in the list.
				const bool bHadArchivedLink = cur_client->GetArchivedClientRuntimeID() != 0;
				ClientRuntimeReference archivedClientRef;
				CUpDownClient* pArchivedClient = ResolveArchivedClientReference(cur_client, archivedClientRef) ? archivedClientRef.Get() : NULL;
				if (thePrefs.GetClientHistory() && pArchivedClient != NULL) {
					if (!bHadArchivedLink) // Move history data to the current client if it is not done yet.
						LoadArchive(cur_client, _T("ReloadList"));
					// Remove client history object from the vector.
					if (!m_ListedItemsVector.empty()) {
						for (size_t i = 0; i < m_ListedItemsVector.size(); ++i) {
							if (!IsExactListedClientEntry(*this, i, pArchivedClient, pArchivedClient->GetRuntimeID()))
								continue;
							m_ListedItemsVector.erase(m_ListedItemsVector.begin() + i);
							if (i < m_ListedItemRuntimeIDs.size())
								m_ListedItemRuntimeIDs.erase(m_ListedItemRuntimeIDs.begin() + i);
							break;
						}
					}
				}
				if (!IsFilteredOut(cur_client)) {
					m_ListedItemsVector.push_back(cur_client); // Add current client to the vector.
					m_ListedItemRuntimeIDs.push_back(cur_client->GetRuntimeID());
				}
			}
		}
	}

	// Reloading data completed at this point. Now we need to sort the vector.
	// Sort vector, then load sorted data to map and reverse map
	{
		struct ListedClientEntry
		{
			CUpDownClient*	pClient;
			DWORD			uRuntimeID;
		};

		std::vector<ListedClientEntry> aEntries;
		aEntries.reserve(m_ListedItemsVector.size());
		for (size_t i = 0; i < m_ListedItemsVector.size(); ++i)
			aEntries.push_back(ListedClientEntry{ m_ListedItemsVector[i], (i < m_ListedItemRuntimeIDs.size()) ? m_ListedItemRuntimeIDs[i] : 0 });

		CombinedSort(aEntries.begin(), aEntries.end(), [](const ListedClientEntry& left, const ListedClientEntry& right) -> bool
		{
			return CompareClientListItems(left.pClient, left.uRuntimeID, right.pClient, right.uRuntimeID, CClientListCtrl::m_pSortParam) < 0;
		});

		for (size_t i = 0; i < aEntries.size(); ++i) {
			m_ListedItemsVector[i] = aEntries[i].pClient;
			if (i < m_ListedItemRuntimeIDs.size())
				m_ListedItemRuntimeIDs[i] = aEntries[i].uRuntimeID;
		}
	}
	RebuildListedItemsMap();

	UpdateClientListItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list.
	theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount(); // Refresh the file count.

	if (!bInitializing)
		RestoreListState(0, LsfFlag, false); // Restore selections, sort and scroll values if this is not the first call.

	SetRedraw(true); // Resume painting
	Invalidate(); //Force redraw
}

// Index map after vector changes
void CClientListCtrl::RebuildListedItemsMap()
{
	m_ListedItemsMap.RemoveAll();
	for (int i = 0; i < static_cast<int>(m_ListedItemsVector.size()); ++i)
		m_ListedItemsMap[m_ListedItemsVector[i]] = i;
}

void CClientListCtrl::SaveArchive(CUpDownClient* client)
{
	// Don't return here for theApp.IsClosing(), because archive copies of active clients need to be created while app closing.
	if (!thePrefs.GetClientHistory() || !client || client->m_bIsArchived || IsCorruptOrBadUserHash(client->m_achUserHash))
		return;

	CString cur_hash = md4str(client->GetUserHash());
	CUpDownClient* pArchivedClient = NULL;
	bool bNewArchivedClient = false;
	{
		CSingleLock runtimeMapLock(&theApp.clientlist->m_csTrackedClientRuntimeMaps, TRUE);
		if (!theApp.clientlist->m_ArchivedClientsMap.Lookup(cur_hash, pArchivedClient)) {
			// No client with same hash matched in the history, create a new one.
			pArchivedClient = new CUpDownClient(NULL, 0, (!client->m_UserIP.IsNull() ? client->m_UserIP.ToUInt32(true) : client->m_ConnectIP.ToUInt32(true)),
				0, 0, false, (!client->m_UserIP.IsNull() ? client->m_UserIP : client->m_ConnectIP));
			bNewArchivedClient = true;
		}
		pArchivedClient->m_bIsArchived = true;
		pArchivedClient->SetUserHash(client->GetUserHash(), false);
		pArchivedClient->tLastSeen = client->tLastSeen > pArchivedClient->tLastSeen ? client->tLastSeen : pArchivedClient->tLastSeen;
		pArchivedClient->SetUserName(client->GetUserName());
		// If current IP's are null then we should keed old IP's. This way we'll have country, city data and country flag in Geolocation columns.
		if (!client->m_ConnectIP.IsNull()) // If not, keep last valid data
			pArchivedClient->m_ConnectIP = client->m_ConnectIP;
		if (!client->m_UserIP.IsNull()) // If not, keep last valid data
			pArchivedClient->m_UserIP = client->m_UserIP;
		if(client->GetServerIP()) // If not, keep last valid data
			pArchivedClient->SetServerIP(client->GetServerIP());
		if (client->GetUserIDHybrid()) // If not, keep last valid data
			pArchivedClient->SetUserIDHybrid(client->GetUserIDHybrid());
		if (client->GetUserPort()) // If not, keep last valid data
			pArchivedClient->SetUserPort(client->GetUserPort());
		pArchivedClient->SetServerPort(client->GetServerPort());
		pArchivedClient->SetVersion(client->GetVersion());
		pArchivedClient->SetMuleVersion(client->GetMuleVersion());
		pArchivedClient->SetIsHybrid(client->GetIsHybrid());
		if (client->GetUDPPort()) // If not, keep last valid data
			pArchivedClient->SetUDPPort(client->GetUDPPort());
		if (client->GetKadPort()) // If not, keep last valid data
			pArchivedClient->SetKadPort(client->GetKadPort());
		pArchivedClient->SetUDPVersion(client->GetUDPVersion());
		pArchivedClient->SetCompatibleClient(client->GetCompatibleClient());
		pArchivedClient->SetIsML(client->GetIsML());
		pArchivedClient->SetEmuleProtocol(client->ExtProtocolAvailable()); // Copy eMule protocol flag for InitClientSoftwareVersion
		pArchivedClient->SetClientSoftVer(client->GetClientSoftVer());
		pArchivedClient->SetClientModVer(client->GetClientModVer());
		// Parse username to determine client software type - must be called AFTER all dependent fields are set
		pArchivedClient->InitClientSoftwareVersion();
		pArchivedClient->SetMessagesSent(client->GetMessagesSent());
		pArchivedClient->SetMessagesReceived(client->GetMessagesReceived());
		pArchivedClient->SetKadVersion(client->GetKadVersion());
		pArchivedClient->SetViewSharedFilesSupport(client->GetViewSharedFilesSupport());
		pArchivedClient->m_uSharedFilesStatus = client->m_uSharedFilesStatus != S_NOT_QUERIED ? client->m_uSharedFilesStatus : pArchivedClient->m_uSharedFilesStatus;
		pArchivedClient->m_uSharedFilesCount = client->m_uSharedFilesCount ? client->m_uSharedFilesCount : pArchivedClient->m_uSharedFilesCount;
		pArchivedClient->m_tSharedFilesLastQueriedTime = client->m_tSharedFilesLastQueriedTime > pArchivedClient->m_tSharedFilesLastQueriedTime ? client->m_tSharedFilesLastQueriedTime : pArchivedClient->m_tSharedFilesLastQueriedTime;
		pArchivedClient->m_bAutoQuerySharedFiles = client->m_bAutoQuerySharedFiles;
		pArchivedClient->m_strClientNote = client->m_strClientNote.GetLength() ? client->m_strClientNote : pArchivedClient->m_strClientNote;
		pArchivedClient->tFirstSeen = client->tFirstSeen < pArchivedClient->tFirstSeen ? client->tFirstSeen : pArchivedClient->tFirstSeen;
		if (client->IsBadClient()) { // If client's punishment isn't expired
			pArchivedClient->SetGPLEvildoer(client->GetGPLEvildoer());
			pArchivedClient->m_uPunishment = client->m_uPunishment;
			pArchivedClient->m_uBadClientCategory = client->m_uBadClientCategory;
			pArchivedClient->m_tPunishmentStartTime = client->m_tPunishmentStartTime;
			pArchivedClient->m_strPunishmentReason = client->m_strPunishmentReason;
		}
		pArchivedClient->credits = theApp.clientcredits->GetCredit(pArchivedClient->m_achUserHash, false);
		if (bNewArchivedClient)
			theApp.clientlist->m_ArchivedClientsMap[cur_hash] = pArchivedClient;
		theApp.clientlist->m_ArchivedClientsByRuntimeID[pArchivedClient->GetRuntimeID()] = pArchivedClient;
	}
	client->SetArchivedClientLink(pArchivedClient);

	if (thePrefs.GetClientHistoryLog())
		AddDebugLogLine(false, _T("[CLIENT HISTORY]: Client archived -> Hash: %s | Name: %s"), md4str(client->GetUserHash()), (LPCTSTR)EscPercent(client->GetUserName()));
	return;
}

void CClientListCtrl::LoadArchive(CUpDownClient* client, const CString strCallingMethod)
{
	if (!thePrefs.GetClientHistory() || theApp.IsClosing() || !client || client->m_bIsArchived || IsCorruptOrBadUserHash(client->m_achUserHash))
		return;

	CString cur_hash = md4str(client->GetUserHash());
	ClientRuntimeReference archivedClientRef;
	CUpDownClient* pArchivedClient = ResolveArchivedClientReference(client, archivedClientRef) ? archivedClientRef.Get() : NULL;
	if (pArchivedClient != NULL) {
		const time_t dwExpired = pArchivedClient->tLastSeen + time_t(thePrefs.GetClientHistoryExpDays() < 1 ? 12960000 : thePrefs.GetClientHistoryExpDays() * 86400); //12960000=5x30x24x60x60 (5 months)  86400=24x60x60 (1 day)
		if (time(NULL) >= (time_t)dwExpired) {
			// This history is too old to load.
			if (thePrefs.GetClientHistoryLog())
				AddDebugLogLine(false, _T("[CLIENT HISTORY]: Client archive expired and not loaded -> Method: %s | Hash: %s | Name: %s"), strCallingMethod, cur_hash, (LPCTSTR)EscPercent(client->GetUserName()));
			return; 
		}

		client->tFirstSeen = pArchivedClient->tFirstSeen;
		
		if (pArchivedClient->IsBadClient()) { // If history's punishment isn't expired
			client->SetGPLEvildoer(pArchivedClient->GetGPLEvildoer());
			client->m_uPunishment = pArchivedClient->m_uPunishment;
			client->m_uBadClientCategory = pArchivedClient->m_uBadClientCategory;
			client->m_tPunishmentStartTime = pArchivedClient->m_tPunishmentStartTime;
			client->m_strPunishmentReason = pArchivedClient->m_strPunishmentReason;
		}
		
		client->SetMessagesSent(pArchivedClient->GetMessagesSent());
		client->SetMessagesReceived(pArchivedClient->GetMessagesReceived());
		client->m_uSharedFilesStatus = pArchivedClient->m_uSharedFilesStatus;
		client->m_uSharedFilesCount = pArchivedClient->m_uSharedFilesCount;
		client->m_tSharedFilesLastQueriedTime = pArchivedClient->m_tSharedFilesLastQueriedTime;
		client->m_bAutoQuerySharedFiles = pArchivedClient->m_bAutoQuerySharedFiles;
		client->m_strClientNote = pArchivedClient->m_strClientNote;

		if (thePrefs.GetClientHistoryLog())
			AddDebugLogLine(false, _T("[CLIENT HISTORY]: Client archive loaded -> Method: %s | Hash: %s | Name: %s"), strCallingMethod, cur_hash, (LPCTSTR)EscPercent(client->GetUserName()));
	} else if (thePrefs.GetClientHistoryLog())
		AddDebugLogLine(false, _T("[CLIENT HISTORY]: Client archive not found -> Method: %s | Hash: %s  Name: %s"), strCallingMethod, cur_hash, (LPCTSTR)EscPercent(client->GetUserName()));
}

CUpDownClient* CClientListCtrl::ArchivedToActive(CUpDownClient* client) {
	if (client == NULL)
		return NULL;

	if (client->m_bIsArchived) { 
		CFriend* pFriend = client->GetFriend();
		// This is an archived client, which is designed for store data only, not to create new sockets. So we need to move this data to a new active client and then we can call RequestSharedFileList.
		CUpDownClient* NewClient = theApp.clientlist->FindClientByUserHash(client->GetUserHash()); // First check if this archived client has already its active client.
		if (NewClient) {
			if (pFriend != NULL && pFriend->GetLinkedClient(true) == client)
				pFriend->SetLinkedClient(NewClient);
			return NewClient;
		}

		// This archived client doesn't have its acvive client, so let's create one.
		NewClient = new CUpDownClient(NULL, 0, (!client->m_UserIP.IsNull() ? client->m_UserIP.ToUInt32(true) : client->m_ConnectIP.ToUInt32(true)),
										0, 0, false, (!client->m_UserIP.IsNull() ? client->m_UserIP : client->m_ConnectIP));
		CString cur_hash = md4str(client->GetUserHash());

		NewClient->m_bIsArchived = false;
		NewClient->SetArchivedClientLink(client);
		NewClient->SetUserHash(client->GetUserHash(), false);
		NewClient->tLastSeen = client->tLastSeen;
		NewClient->SetUserName(client->GetUserName());
		// m_UserIP and m_ConnectIP are set by CUpDownClient::init --> CUpDownClient::SetIP
		NewClient->SetServerIP(client->GetServerIP());
		NewClient->SetUserIDHybrid(client->GetUserIDHybrid());
		NewClient->SetUserPort(client->GetUserPort());
		NewClient->SetServerPort(client->GetServerPort());
		NewClient->SetVersion(client->GetVersion());
		NewClient->SetMuleVersion(client->GetMuleVersion());
		NewClient->SetIsHybrid(client->GetIsHybrid());
		NewClient->SetUDPPort(client->GetUDPPort());
		NewClient->SetKadPort(client->GetKadPort());
		NewClient->SetUDPVersion(client->GetUDPVersion());
		NewClient->SetCompatibleClient(client->GetCompatibleClient());
		NewClient->SetIsML(client->GetIsML());
		NewClient->SetClientSoftVer(client->GetClientSoftVer());
		NewClient->SetClientModVer(client->GetClientModVer());
		NewClient->SetMessagesSent(client->GetMessagesSent());
		NewClient->SetMessagesReceived(client->GetMessagesReceived());
		NewClient->SetKadVersion(client->GetKadVersion());
		NewClient->SetViewSharedFilesSupport(client->GetViewSharedFilesSupport());
		NewClient->m_uSharedFilesStatus = client->m_uSharedFilesStatus;
		NewClient->m_uSharedFilesCount = client->m_uSharedFilesCount;
		NewClient->m_tSharedFilesLastQueriedTime = client->m_tSharedFilesLastQueriedTime;
		NewClient->m_bAutoQuerySharedFiles = client->m_bAutoQuerySharedFiles;
		NewClient->m_strClientNote = client->m_strClientNote;
		NewClient->tFirstSeen = client->tFirstSeen;
		if (client->IsBadClient()) { // If client's punishment isn't expired
			NewClient->SetGPLEvildoer(client->GetGPLEvildoer());
			NewClient->m_uPunishment = client->m_uPunishment;
			NewClient->m_uBadClientCategory = client->m_uBadClientCategory;
			NewClient->m_tPunishmentStartTime = client->m_tPunishmentStartTime;
			NewClient->m_strPunishmentReason = client->m_strPunishmentReason;
		}

		if (thePrefs.GetClientHistoryLog())
			AddDebugLogLine(false, _T("[CLIENT HISTORY]: Client archive loaded -> Method: %s | Hash: %s | Name: %s"), _T("RequestSharedFileList"), md4str(NewClient->GetUserHash()), (LPCTSTR)EscPercent(NewClient->GetUserName()));
		theApp.clientlist->AddClient(NewClient, true);
		if (pFriend != NULL && pFriend->GetLinkedClient(true) == client)
			pFriend->SetLinkedClient(NewClient);

		return NewClient; // Active client created, now we can return this.
	} else
		return client; // This is already an active client.
}

CObject* CClientListCtrl::GetItemObject(int iIndex) const
{
	if (iIndex < 0 || iIndex >= m_ListedItemsVector.size())
		return nullptr;
	return m_ListedItemsVector[iIndex];
}

CObject* CClientListCtrl::GetNextSelectableItem()
{
	const int iItemCount = GetItemCount();
	if (iItemCount < 2)
		return NULL;

	POSITION pos = GetFirstSelectedItemPosition();
	if (pos == NULL)
		return NULL;

	const int iSelectedItem = GetNextSelectedItem(pos);
	for (int iNewItem = iSelectedItem + 1; iNewItem < iItemCount; ++iNewItem) {
		CObject* pToken = CreateClientDetailWalkerToken(GetListedClientRuntimeID(*this, iNewItem));
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

CObject* CClientListCtrl::GetPrevSelectableItem()
{
	const int iItemCount = GetItemCount();
	if (iItemCount < 2)
		return NULL;

	POSITION pos = GetFirstSelectedItemPosition();
	if (pos == NULL)
		return NULL;

	const int iSelectedItem = GetNextSelectedItem(pos);
	for (int iNewItem = iSelectedItem - 1; iNewItem >= 0; --iNewItem) {
		CObject* pToken = CreateClientDetailWalkerToken(GetListedClientRuntimeID(*this, iNewItem));
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

void CClientListCtrl::ShowSelectedUserDetails()
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

	ClientRuntimeReference clientRef;
	CUpDownClient* client = AcquireItemTrackedClient(*this, GetSelectionMark(), clientRef);
	if (client) {
		CClientDetailDialog dialog(client, this);
		clientRef.Release();
		dialog.DoModal();
	}
}

bool CClientListCtrl::IsFilteredOut(const CUpDownClient* client)
{
	if (!client)
		return true;

	bool m_bTemp;

	if ((thePrefs.m_uArchivedCheckState == BST_CHECKED && !client->m_bIsArchived) || (thePrefs.m_uArchivedCheckState == BST_INDETERMINATE && client->m_bIsArchived))
		return true;

	m_bTemp = !client->socket || !client->socket->IsConnected();
	if ((thePrefs.m_uConnectedCheckState == BST_CHECKED && m_bTemp) || (thePrefs.m_uConnectedCheckState == BST_INDETERMINATE && !m_bTemp))
		return true;

	m_bTemp = !client->IsEd2kClient() || !client->GetViewSharedFilesSupport() || client->m_uSharedFilesStatus == S_ACCESS_DENIED ||
		(client->HasLowID() && client->GetConnectIP().IsNull() && !client->AnyCallbackAvailable() && (!client->GetUserIDHybrid() || !client->GetServerIP()) && client->GetConnectingState() == CCS_NONE && client->GetKadState() == KS_NONE);
	if ((thePrefs.m_uQueryableCheckState == BST_CHECKED && m_bTemp) || (thePrefs.m_uQueryableCheckState == BST_INDETERMINATE && !m_bTemp))
		return true;

	m_bTemp = !client->IsEd2kClient() || !client->GetViewSharedFilesSupport() || client->m_uSharedFilesStatus != S_NOT_QUERIED || client->m_bAutoQuerySharedFiles ||
		(client->HasLowID() && client->GetConnectIP().IsNull() && !client->AnyCallbackAvailable() && (!client->GetUserIDHybrid() || !client->GetServerIP()) && client->GetConnectingState() == CCS_NONE && client->GetKadState() == KS_NONE);
	if ((thePrefs.m_uNotQueriedCheckState == BST_CHECKED && m_bTemp) || (thePrefs.m_uNotQueriedCheckState == BST_INDETERMINATE && !m_bTemp))
		return true;

	m_bTemp = client->GetIP().IsNull() && client->GetConnectIP().IsNull();
	if ((thePrefs.m_uValidIPCheckState == BST_CHECKED && m_bTemp) || (thePrefs.m_uValidIPCheckState == BST_INDETERMINATE && !m_bTemp))
		return true;

	m_bTemp = client->HasLowID();
	if ((thePrefs.m_uHighIdCheckState == BST_CHECKED && m_bTemp) || (thePrefs.m_uHighIdCheckState == BST_INDETERMINATE && !m_bTemp))
		return true;

	m_bTemp = !client->IsBadClient();
	if ((thePrefs.m_uBadClientCheckState == BST_CHECKED && m_bTemp) || (thePrefs.m_uBadClientCheckState == BST_INDETERMINATE && !m_bTemp))
		return true;
	
	const CStringArray& rastrFilter = theApp.emuledlg->transferwnd->m_pwndTransfer->m_astrFilterClientList;
	if (!rastrFilter.IsEmpty()) {
		// filtering is done by text only for all columns to keep it consistent and simple
		// for the user even if that doesn't allow complex filters
		// for example for a file size range - but this could be done at server search time already
		const CString& szFilterTarget(GetItemDisplayText(client, theApp.emuledlg->transferwnd->m_pwndTransfer->GetFilterColumnClientList()));

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

void CClientListCtrl::MaintainSortOrderAfterUpdate()
{
	if (GetSortItem() != -1) // Re-sort the list to maintain sort order after updates
		ReloadList(true, kClientListViewState);
}
