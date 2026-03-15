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
#include "ClientDetailDialog.h"
#include "emuledlg.h"
#include "ClientList.h"
#include "OtherFunctions.h"
#include "Addfriend.h"
#include "FriendList.h"
#include "FriendListCtrl.h"
#include "UpDownClient.h"
#include "ListenSocket.h"
#include "MenuCmds.h"
#include "ChatWnd.h"
#include "ED2KLink.h"
#include "Log.h"
#include "TransferDlg.h"
#include "eMuleAI/Shield.h"
#include "eMuleAI/DarkMode.h"

#ifdef NATTTESTMODE
#include "kademlia/kademlia/Kademlia.h"
#include "kademlia/net/KademliaUDPListener.h"
#include "kademlia/kademlia/Prefs.h"
#include "kademlia/routing/RoutingZone.h"
#include "kademlia/routing/contact.h"
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


IMPLEMENT_DYNAMIC(CFriendListCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CFriendListCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
	ON_WM_CONTEXTMENU()
	ON_WM_SYSCOLORCHANGE()
END_MESSAGE_MAP()

CFriendListCtrl::CFriendListCtrl()
{
	SetGeneralPurposeFind(true);
	SetSkinKey(_T("FriendsLv"));
}

void CFriendListCtrl::Init()
{
	SetPrefsKey(_T("FriendListCtrl"));

	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	CRect rcWindow;
	GetWindowRect(rcWindow);
	InsertColumn(0, EMPTY, LVCFMT_LEFT, rcWindow.Width() - 4);	//QL_USERNAME

	SetAllIcons();
	theApp.friendlist->SetWindow(this);
	LoadSettings();
	SetSortArrow();
}

void CFriendListCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
	SetAllIcons();
}

void CFriendListCtrl::SetAllIcons()
{
	CImageList iml;
	iml.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	iml.Add(CTempIconLoader(_T("FriendNoClient")));
	iml.Add(CTempIconLoader(_T("FriendWithClient")));
	iml.Add(CTempIconLoader(_T("FriendConnected")));
	HIMAGELIST himlOld = ApplyImageList(iml.Detach());
	if (himlOld)
		::ImageList_Destroy(himlOld);
}

void CFriendListCtrl::Localize()
{
	static const LPCTSTR uids[1] =
	{
		_T("QL_USERNAME")
	};
	LocaliseHeaderCtrl(uids, _countof(uids));

	for (int i = GetItemCount(); --i >= 0;)
		UpdateFriend(i, reinterpret_cast<CFriend*>(GetItemData(i)));
}

void CFriendListCtrl::UpdateFriend(int iItem, const CFriend *pFriend)
{
	SetItemText(iItem, 0, pFriend->m_strName.IsEmpty() ? _T('(') + md4str(pFriend->m_abyUserhash) + _T(')') : pFriend->m_strName);

	CUpDownClient* pLinkedClient = pFriend->GetLinkedClient(true);
	int iImage;
	if (pLinkedClient == NULL)
		iImage = 0;
	else if (pLinkedClient->socket && pLinkedClient->socket->IsConnected())
		iImage = 2;
	else
		iImage = 1;
	SetItem(iItem, 0, LVIF_IMAGE, 0, iImage, 0, 0, 0, 0);
}

void CFriendListCtrl::AddFriend(const CFriend *pFriend)
{
	int iItem = InsertItem(LVIF_TEXT | LVIF_PARAM, GetItemCount(), pFriend->m_strName, 0, 0, 0, (LPARAM)pFriend);
	if (iItem >= 0)
		UpdateFriend(iItem, pFriend);
	theApp.emuledlg->chatwnd->UpdateFriendlistCount(theApp.friendlist->GetCount());
}

void CFriendListCtrl::RemoveFriend(const CFriend *pFriend)
{
	LVFINDINFO find;
	find.flags = LVFI_PARAM;
	find.lParam = (LPARAM)pFriend;
	int iItem = FindItem(&find);
	if (iItem >= 0)
		DeleteItem(iItem);
	theApp.emuledlg->chatwnd->UpdateFriendlistCount(theApp.friendlist->GetCount());
}

void CFriendListCtrl::RefreshFriend(const CFriend *pFriend)
{
	LVFINDINFO find;
	find.flags = LVFI_PARAM;
	find.lParam = (LPARAM)pFriend;
	int iItem = FindItem(&find);
	if (iItem >= 0)
		UpdateFriend(iItem, pFriend);
}

void CFriendListCtrl::OnContextMenu(CWnd*, CPoint point)
{
	CMenuXP ClientMenu;
	ClientMenu.CreatePopupMenu();
	ClientMenu.AddMenuSidebar(GetResString(_T("FRIENDLIST")));

	CUpDownClient* client = NULL;

	const CFriend *cur_friend = NULL;
	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	if (iSel >= 0) {
		cur_friend = reinterpret_cast<CFriend*>(GetItemData(iSel));
		if (cur_friend)
			client = cur_friend->GetLinkedClient(true);
		ClientMenu.AppendMenu(MF_STRING, MP_DETAIL, GetResString(_T("SHOWDETAILS")), _T("CLIENTDETAILS"));
		ClientMenu.SetDefaultItem(MP_DETAIL);
	}

	ClientMenu.AppendMenu(MF_STRING, MP_ADDFRIEND, GetResString(_T("ADDAFRIEND")), _T("ADDFRIEND"));
	ClientMenu.AppendMenu(MF_STRING | (cur_friend ? MF_ENABLED : MF_GRAYED), MP_REMOVEFRIEND, GetResString(_T("REMOVEFRIEND")), _T("DELETEFRIEND"));
	ClientMenu.AppendMenu(MF_STRING | (cur_friend ? MF_ENABLED : MF_GRAYED), MP_MESSAGE, GetResString(_T("SEND_MSG")), _T("SENDMESSAGE"));
	ClientMenu.AppendMenu(MF_STRING | ((cur_friend && client && client->IsEd2kClient() && client->GetViewSharedFilesSupport()) ? MF_ENABLED : MF_GRAYED), MP_SHOWLIST, GetResString(_T("VIEWFILES")), _T("VIEWFILES"));

	ClientMenu.AppendMenu(MF_STRING | ((client && client->IsEd2kClient() && client->GetViewSharedFilesSupport() && (client->m_bIsArchived || !client->socket || !client->socket->IsConnected())) ? MF_ENABLED : MF_GRAYED), MP_SHOWLIST_AUTO_QUERY, GetResString(_T("VIEW_FILES_ACTIVATE_AUTO_QUERY")), _T("CLOCKGREEN"));
	if (client == NULL)
		ClientMenu.AppendMenu(MF_STRING | MF_GRAYED, MP_ACTIVATE_AUTO_QUERY, GetResString(_T("ACTIVATE_AUTO_QUERY")), _T("CLOCKBLUE"));
	else if (client->m_bAutoQuerySharedFiles)
		ClientMenu.AppendMenu(MF_STRING | MF_ENABLED, MP_DEACTIVATE_AUTO_QUERY, GetResString(_T("DEACTIVATE_AUTO_QUERY")), _T("CLOCKRED"));
	else
		ClientMenu.AppendMenu(MF_STRING | ((client->IsEd2kClient() && client->GetViewSharedFilesSupport() && (client->m_bIsArchived || !client->socket || !client->socket->IsConnected())) ? MF_ENABLED : MF_GRAYED), MP_ACTIVATE_AUTO_QUERY, GetResString(_T("ACTIVATE_AUTO_QUERY")), _T("CLOCKBLUE"));


	ClientMenu.AppendMenu(MF_STRING | (client ? MF_ENABLED : MF_GRAYED), MP_EDIT_NOTE, GetResString(_T("EDIT_CLIENT_NOTE")), _T("RENAME"));
	ClientMenu.AppendMenu(MF_STRING, MP_FRIENDSLOT, GetResString(_T("FRIENDSLOT")), _T("FRIENDSLOT"));
	ClientMenu.AppendMenu(MF_STRING | (GetItemCount() > 0 ? MF_ENABLED : MF_GRAYED), MP_FIND, GetResString(_T("FIND")), _T("Search"));

#ifdef NATTTESTMODE	
	ClientMenu.AppendMenu(MF_STRING | (cur_friend ? MF_ENABLED : MF_GRAYED), MP_SENDKADBUDDYREQUEST, GetResString(_T("SENDKADBUDDYREQ")), _T("KADEMLIA"));
	ClientMenu.AppendMenu(MF_STRING | (cur_friend && client ? MF_ENABLED : MF_GRAYED), MP_SENDESERVERBUDDYREQUEST, GetResString(_T("SENDESERVERBUDDYREQ")), _T("KADEMLIA"));
#endif

	ClientMenu.EnableMenuItem(MP_FRIENDSLOT, (cur_friend ? MF_ENABLED : MF_GRAYED));
	ClientMenu.CheckMenuItem(MP_FRIENDSLOT, (cur_friend && cur_friend->GetFriendSlot()) ? MF_CHECKED : MF_UNCHECKED);

	ClientMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	CMenuXP m_PunishmentMenu;
	if (client != NULL) {
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
		int m_PunishmentMenuItem = MP_PUNISMENT_IPUSERHASHBAN + client->m_uPunishment;
		m_PunishmentMenu.CheckMenuRadioItem(MP_PUNISMENT_IPUSERHASHBAN, MP_PUNISMENT_NONE, m_PunishmentMenuItem, 0);
	}
	ClientMenu.AppendMenu(MF_STRING | MF_POPUP | (client ? MF_ENABLED : MF_GRAYED), (UINT_PTR)m_PunishmentMenu.m_hMenu, GetResString(_T("PUNISHMENT")), _T("PUNISHMENT"));

	ClientMenu.AppendMenu(MF_SEPARATOR);
	ClientMenu.AppendMenu(MF_STRING | (theApp.IsEd2kFriendLinkInClipboard() ? MF_ENABLED : MF_GRAYED), MP_PASTE, GetResString(_T("PASTE")), _T("PASTELINK"));
	ClientMenu.AppendMenu(MF_STRING | (cur_friend ? MF_ENABLED : MF_GRAYED), MP_GETFRIENDED2KLINK, GetResString(_T("GETFRIENDED2KLINK")), _T("ED2KLINK"));
	ClientMenu.AppendMenu(MF_STRING | (cur_friend ? MF_ENABLED : MF_GRAYED), MP_GETHTMLFRIENDED2KLINK, GetResString(_T("GETHTMLFRIENDED2KLINK")), _T("ED2KLINK"));

	GetPopupMenuPos(*this, point);
	ClientMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
}

BOOL CFriendListCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	wParam = LOWORD(wParam);

	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	CFriend *cur_friend = (iSel >= 0) ? reinterpret_cast<CFriend*>(GetItemData(iSel)) : NULL;
	if (cur_friend && !theApp.friendlist->IsValid(cur_friend)) {
		ASSERT(0);
		cur_friend = NULL;
	}

	CUpDownClient* client = NULL;
	if (cur_friend)
		client = cur_friend->GetLinkedClient(true);

	switch (wParam) {
	case MP_MESSAGE:
		if (cur_friend)
			{
				CUpDownClient* FoundClient = cur_friend->GetClientForChatSession();
				CUpDownClient* NewClient = theApp.emuledlg->transferwnd->GetClientList()->ArchivedToActive(FoundClient);
				if (NewClient && (FoundClient == NewClient || theApp.clientlist->IsValidClient(NewClient)))
					theApp.emuledlg->chatwnd->StartSession(NewClient);
			}
		break;
	case MP_REMOVEFRIEND:
		if (cur_friend) {
			theApp.friendlist->RemoveFriend(cur_friend);
			// auto select next item after deleted one.
			if (iSel < GetItemCount()) {
				SetSelectionMark(iSel);
				SetItemState(iSel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			}
			theApp.emuledlg->chatwnd->UpdateSelectedFriendMsgDetails();
		}
		break;
	case MP_ADDFRIEND:
		{
			CAddFriend dialog2;
			dialog2.DoModal();
		}
		break;
	case MP_DETAIL:
	case MPG_ALTENTER:
	case IDA_ENTER:
		if (cur_friend)
			ShowFriendDetails(cur_friend);
		break;
	case MP_SHOWLIST:
		if (cur_friend) {
			if (cur_friend->GetLinkedClient(true))
				{
					CUpDownClient* LinkedClient = cur_friend->GetLinkedClient();
					CUpDownClient* NewClient = theApp.emuledlg->transferwnd->GetClientList()->ArchivedToActive(LinkedClient);
					if (NewClient && (LinkedClient == NewClient || theApp.clientlist->IsValidClient(NewClient)))
						NewClient->RequestSharedFileList();
				}
			else {
				CUpDownClient* newclient = new CUpDownClient(0, cur_friend->m_nLastUsedPort, cur_friend->m_LastUsedIP.ToUInt32(true), 0, 0, false, cur_friend->m_LastUsedIP);
				newclient->SetUserName(cur_friend->m_strName);
				newclient->SetUserHash(cur_friend->m_abyUserhash);
				theApp.clientlist->AddClient(newclient);
				newclient->RequestSharedFileList();
			}
		}
		break;
	case MP_FRIENDSLOT:
		if (cur_friend) {
			bool bIsAlready = cur_friend->GetFriendSlot();
			theApp.friendlist->RemoveAllFriendSlots();
			if (!bIsAlready)
				cur_friend->SetFriendSlot(true);
		}
		break;
	case MP_SHOWLIST_AUTO_QUERY:
		{
			client->SetAutoQuerySharedFiles(true);
			if (cur_friend) {
				if (cur_friend->GetLinkedClient(true))
					{
						CUpDownClient* LinkedClient = cur_friend->GetLinkedClient();
						CUpDownClient* NewClient = theApp.emuledlg->transferwnd->GetClientList()->ArchivedToActive(LinkedClient);
						if (NewClient && (LinkedClient == NewClient || theApp.clientlist->IsValidClient(NewClient)))
							NewClient->RequestSharedFileList();
					}
				else {
					CUpDownClient *newclient = new CUpDownClient(0, cur_friend->m_nLastUsedPort, cur_friend->m_LastUsedIP.ToUInt32(true), 0, 0, false, cur_friend->m_LastUsedIP);
					newclient->SetUserName(cur_friend->m_strName);
					newclient->SetUserHash(cur_friend->m_abyUserhash);
					theApp.clientlist->AddClient(newclient);
					newclient->RequestSharedFileList();
				}
			}
		}
		break;
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
		if (client)
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_IP_BAN")), PR_MANUAL, P_IPUSERHASHBAN);
		break;
	case MP_PUNISMENT_USERHASHBAN:
		if (client)
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_USER_HASH_BAN")), PR_MANUAL, P_USERHASHBAN);
		break;
	case MP_PUNISMENT_UPLOADBAN:
		if (client)
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_UPLOAD_BAN")), PR_MANUAL, P_UPLOADBAN);
		break;
	case MP_PUNISMENT_SCOREX01:
		if (client)
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX01);
		break;
	case MP_PUNISMENT_SCOREX02:
		if (client)
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX02);
		break;
	case MP_PUNISMENT_SCOREX03:
		if (client)
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX03);
		break;
	case MP_PUNISMENT_SCOREX04:
		if (client)
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX04);
		break;
	case MP_PUNISMENT_SCOREX05:
		if (client)
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX05);
		break;
	case MP_PUNISMENT_SCOREX06:
		if (client)
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX06);
		break;
	case MP_PUNISMENT_SCOREX07:
		if (client)
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX07);
		break;
	case MP_PUNISMENT_SCOREX08:
		if (client)
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX08);
		break;
	case MP_PUNISMENT_SCOREX09:
		if (client)
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_SCORE_REDUCING")), PR_MANUAL, P_SCOREX09);
		break;
	case MP_PUNISMENT_NONE:
		if (client)
			theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_MANUAL_CANCELATION")), PR_MANUAL, P_NOPUNISHMENT);
		break;
	case MP_PASTE:
	{
		CString link = theApp.CopyTextFromClipboard();
		link.Trim();
		if ( link.IsEmpty() )
			break;
		try{
			CED2KLink* pLink = CED2KLink::CreateLinkFromUrl(link);
		
			if (pLink && pLink->GetKind() == CED2KLink::kFriend )
			{
				// Better with dynamic_cast, but no RTTI enabled in the project
				CED2KFriendLink* pFriendLink = static_cast<CED2KFriendLink*>(pLink);
				uchar userHash[16];
				pFriendLink->GetUserHash(userHash);

				if ( ! theApp.friendlist->IsAlreadyFriend(userHash) )
					theApp.friendlist->AddFriend(userHash, 0U, CAddress(), 0U, 0U, pFriendLink->GetUserName(), 1U);
				else
				{
					CString msg;
					msg.Format(GetResString(_T("USER_ALREADY_FRIEND")), pFriendLink->GetUserName());
					AddLogLine(true, (LPCTSTR)EscPercent(msg));
				}
			}
			if(pLink) delete pLink; //zz_fly :: memleak :: thanks DolphinX
		}
		catch(CString strError){
			CDarkMode::MessageBox(strError);
		}
	}
		break;
    case MP_GETFRIENDED2KLINK:
	{
		CString sCompleteLink;
		if ( cur_friend && cur_friend->HasUserhash() )
		{
			CString sLink;
			CED2KFriendLink friendLink(cur_friend->m_strName, cur_friend->m_abyUserhash);
			friendLink.GetLink(sLink);
			if ( !sCompleteLink.IsEmpty() )
				sCompleteLink.Append(_T("\r\n"));
			sCompleteLink.Append(sLink);
		}

		if ( !sCompleteLink.IsEmpty() )
			theApp.CopyTextToClipboard(sCompleteLink);
	}
		break;
	case MP_GETHTMLFRIENDED2KLINK:
	{
		CString sCompleteLink;
			
		if ( cur_friend && cur_friend->HasUserhash() )
		{
			CString sLink;
			CED2KFriendLink friendLink(cur_friend->m_strName, cur_friend->m_abyUserhash);
			friendLink.GetLink(sLink);
			sLink = _T("<a href=\"") + sLink + _T("\">") + StripInvalidFilenameChars(cur_friend->m_strName) + _T("</a>");
			if ( !sCompleteLink.IsEmpty() )
				sCompleteLink.Append(_T("\r\n"));
			sCompleteLink.Append(sLink);
		}
			
		if ( !sCompleteLink.IsEmpty() )
			theApp.CopyTextToClipboard(sCompleteLink);
	}
		break;
	case MP_FIND:
		OnFindStart();
		break;
	#ifdef NATTTESTMODE
		case MP_SENDKADBUDDYREQUEST:
			{
				if (!cur_friend)
					break;

				Kademlia::CPrefs* pKadPrefs = Kademlia::CKademlia::GetPrefs();
				if (!Kademlia::CKademlia::IsRunning() || !Kademlia::CKademlia::IsConnected() || pKadPrefs == NULL) {
					AddLogLine(false, _T("[Buddy]: Kad not connected; cannot send serving buddy request."));
					break;
				}

				uint32 uDstIP = 0;
				uint16 uDstUDPPort = 0;
				Kademlia::CUInt128 cryptTarget; // used if obfuscation is supported
				const Kademlia::CUInt128 *pCryptTarget = NULL;
				Kademlia::CUInt128 uFriendKadID;
				bool bHaveContact = false;

				// Prefer linked client endpoint because it is already validated by client list ownership.
				CUpDownClient* pLinked = cur_friend->GetLinkedClient(true);
				if (pLinked && pLinked->GetKadPort() != 0 && !pLinked->GetIP().IsNull()) {
					// Use host byte order here; SendPacket expects host-order IPv4.
					uDstIP = pLinked->GetIP().ToUInt32(true);
					uDstUDPPort = pLinked->GetKadPort();
					bHaveContact = (uDstIP != 0 && uDstUDPPort != 0);
				}

				// Fallback: if we know friend's KadID, try routing table contact.
				if (!bHaveContact && cur_friend->HasKadID()) {
					uFriendKadID.SetValue(cur_friend->m_abyKadID);
					Kademlia::CRoutingZone* pRoutingZone = Kademlia::CKademlia::TryGetRoutingZone();
					if (pRoutingZone != NULL) {
						Kademlia::CContact* pContact = pRoutingZone->GetContact(uFriendKadID);
						if (pContact) {
							uDstIP = pContact->GetIPAddress();
							uDstUDPPort = pContact->GetUDPPort();

							// Use obfuscation if possible.
							if (pContact->GetVersion() >= KADEMLIA_VERSION6_49aBETA) {
								cryptTarget = pContact->GetClientID();
								pCryptTarget = &cryptTarget;
							}

							bHaveContact = (uDstIP != 0 && uDstUDPPort != 0);
						}
					}
				}

				if (!bHaveContact) {
					AddLogLine(false, _T("[Buddy]: Serving buddy request not sent: friend has no Kad contact info (KadID/UDP port)."));
					break;
				}

				Kademlia::CKademliaUDPListener* pKadUDP = Kademlia::CKademlia::TryGetUDPListener();
				if (pKadUDP == NULL || Kademlia::CKademlia::GetPrefs() != pKadPrefs || !Kademlia::CKademlia::IsRunning()) {
					AddLogLine(false, _T("[Buddy]: Kad runtime not ready; serving buddy request canceled."));
					break;
				}

				// Build request payload
				CSafeMemFile fileIO(35);
				Kademlia::CUInt128 servedBuddyID(pKadPrefs->GetKadID());
				servedBuddyID.Xor(Kademlia::CUInt128(true));
				fileIO.WriteUInt128(servedBuddyID);
				fileIO.WriteUInt128(pKadPrefs->GetClientHash());
				fileIO.WriteUInt16(thePrefs.GetPort());
				// Optionally include our connect options (NAT-T bit et al.), older nodes will ignore the extra byte
				fileIO.WriteUInt8(pKadPrefs->GetMyConnectOptions(true, true));
				
				// Send packet
				pKadUDP->SendPacket(fileIO, KADEMLIA_FINDSERVINGBUDDY_REQ, uDstIP, uDstUDPPort, Kademlia::CKadUDPKey(), pCryptTarget);
				pKadPrefs->SetFindServingBuddy(true); // Hint Kad core to start/continue serving buddy discovery.
				AddLogLine(false, _T("[Buddy]: Serving buddy request sent to %s:%u"), (LPCTSTR)ipstr(htonl(uDstIP)), uDstUDPPort); // uDstIP is in host order; ipstr(uint32) expects network order
			}
			break;

	case MP_SENDESERVERBUDDYREQUEST:
		{
			// eServer Buddy: Send request to friend to become our eServer Buddy
			if (!cur_friend)
				break;

			CUpDownClient* pLinked = cur_friend->GetLinkedClient(true);
			if (!pLinked) {
				AddLogLine(false, _T("[eServer Buddy]: Cannot send request - no linked client for this friend."));
				break;
			}

			// Debug: show current state
			AddLogLine(false, _T("[eServer Buddy]: Friend client state: socket=%s connected=%s SupportsEServerBuddy=%d HasSlot=%d InfoPackets=%d"),
				pLinked->socket ? _T("yes") : _T("no"),
				(pLinked->socket && pLinked->socket->IsConnected()) ? _T("yes") : _T("no"),
				pLinked->SupportsEServerBuddy() ? 1 : 0,
				pLinked->HasEServerBuddySlot() ? 1 : 0,
				pLinked->GetInfoPacketsReceived());

			// Check if linked client has a valid TCP connection
			if (!pLinked->socket || !pLinked->socket->IsConnected()) {
				// Try to connect first
				AddLogLine(false, _T("[eServer Buddy]: Friend not connected, trying to connect first..."));
				pLinked->TryToConnect(true);
				break;
			}

			// Check if friend supports eServer Buddy
			if (!pLinked->SupportsEServerBuddy()) {
				// If no Hello packet received yet, try to find an active client with same userhash
				if (pLinked->GetInfoPacketsReceived() == 0) {
					// Try to find active client with same userhash that has Hello info
					CUpDownClient* pActive = theApp.clientlist->FindClientByUserHash(cur_friend->m_abyUserhash);
					if (pActive && pActive != pLinked && pActive->GetInfoPacketsReceived() > 0) {
						// Found active client, update friend's linked client
						AddLogLine(false, _T("[eServer Buddy]: Found active client with completed Hello, updating friend link..."));
						cur_friend->SetLinkedClient(pActive);
						pLinked = pActive;
						// Re-check after update
						if (pLinked->SupportsEServerBuddy()) {
							// Good, now we can proceed
							AddLogLine(false, _T("[eServer Buddy]: Friend now shows eServer Buddy support."));
						} else {
							AddLogLine(false, _T("[eServer Buddy]: Friend does not support eServer Buddy protocol (InfoPackets=%d)."), 
								pLinked->GetInfoPacketsReceived());
							break;
						}
					} else {
						// No active client found, send Hello to initiate handshake
						AddLogLine(false, _T("[eServer Buddy]: Hello handshake not completed, sending Hello packet..."));
						pLinked->SendHelloPacket();
						AddLogLine(false, _T("[eServer Buddy]: Please try again after Hello exchange completes."));
						break;
					}
				} else {
					AddLogLine(false, _T("[eServer Buddy]: Friend does not support eServer Buddy protocol (InfoPackets=%d)."), 
						pLinked->GetInfoPacketsReceived());
					break;
				}
			}

			// Check if friend has available slot
			if (!pLinked->HasEServerBuddySlot()) {
				AddLogLine(false, _T("[eServer Buddy]: Friend has no available buddy slots."));
				break;
			}

			// Send eServer Buddy Request
			if (pLinked->SendEServerBuddyRequest()) {
				AddLogLine(false, _T("[eServer Buddy]: Buddy request sent to %s"), (LPCTSTR)pLinked->GetUserName());
			} else {
				AddLogLine(false, _T("[eServer Buddy]: Failed to send buddy request to %s"), (LPCTSTR)pLinked->GetUserName());
			}
		}
		break;
#endif
	}
	return TRUE;
}

void CFriendListCtrl::OnNmDblClk(LPNMHDR, LRESULT *pResult)
{
	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	if (iSel >= 0)
		ShowFriendDetails(reinterpret_cast<CFriend*>(GetItemData(iSel)));
	*pResult = 0;
}

void CFriendListCtrl::ShowFriendDetails(const CFriend *pFriend)
{
	if (pFriend) {
		CUpDownClient* pLinkedClient = pFriend->GetLinkedClient(true);
		if (pLinkedClient != NULL) {
			CClientDetailDialog dlg(pLinkedClient);
			dlg.DoModal();
		} else {
			CAddFriend dlg;
			dlg.m_pShowFriend = const_cast<CFriend*>(pFriend);
			dlg.DoModal();
		}
	}
}

BOOL CFriendListCtrl::PreTranslateMessage(MSG *pMsg)
{
	if (pMsg->message == WM_KEYDOWN)
		switch (pMsg->wParam) {
		case VK_DELETE:
		case VK_INSERT:
			PostMessage(WM_COMMAND, (pMsg->wParam == VK_DELETE ? MP_REMOVEFRIEND : MP_ADDFRIEND), 0);
		}
	return CMuleListCtrl::PreTranslateMessage(pMsg);
}

void CFriendListCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	// Determine ascending based on whether already sorted on this column
	bool bSortAscending = (GetSortItem() != pNMLV->iSubItem || !GetSortAscending());

	// Item is column clicked
	int iSortItem = pNMLV->iSubItem;

	// Sort table
	SetSortArrow(iSortItem, bSortAscending);
	SortItems(SortProc, MAKELONG(iSortItem, !bSortAscending));
	*pResult = 0;
}

int CALLBACK CFriendListCtrl::SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort)
{
	const CFriend *item1 = reinterpret_cast<CFriend*>(lParam1);
	const CFriend *item2 = reinterpret_cast<CFriend*>(lParam2);
	if (item1 == NULL || item2 == NULL)
		return 0;

	int iResult;
	switch (LOWORD(lParamSort)) {
	case 0: //friend's name
		iResult = CompareLocaleStringNoCase(item1->m_strName, item2->m_strName);
		break;
	default:
		return 0;
	}
	return HIWORD(lParamSort) ? -iResult : iResult;
}

void CFriendListCtrl::UpdateList()
{
	theApp.emuledlg->chatwnd->UpdateFriendlistCount(theApp.friendlist->GetCount());
	SortItems(SortProc, MAKELONG(GetSortItem(), !GetSortAscending()));
}

bool CFriendListCtrl::AddEmfriendsMetToList(const CString& strFile)
{
	ShowWindow(SW_HIDE);
	bool ret = theApp.friendlist->AddEmfriendsMetToList(strFile);
	theApp.friendlist->ShowFriends();
	UpdateList();
	ShowWindow(SW_SHOW);
	return ret;
}
