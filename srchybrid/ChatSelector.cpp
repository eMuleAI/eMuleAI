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
#include "HTRichEditCtrl.h"
#include "emuledlg.h"
#include "UpDownClient.h"
#include "TaskbarNotifier.h"
#include "ListenSocket.h"
#include "ChatWnd.h"
#include "ChatSelector.h"
#include "Log.h"
#include "MenuCmds.h"
#include "ClientDetailDialog.h"
#include "FriendList.h"
#include "ClientList.h"
#include "eMuleAI/Shield.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define	STATUS_MSG_COLOR		RGB(0,128,0)		// dark green
#define	SENT_TARGET_MSG_COLOR	RGB(0,192,0)		// bright green
#define	RECV_SOURCE_MSG_COLOR	RGB(0,128,255)		// bright cyan/blue

#define	TIME_STAMP_FORMAT		_T("[%H:%M] ")

#define	IDT_CHATITEMS	20

///////////////////////////////////////////////////////////////////////////////
// CChatItem

CChatItem::CChatItem()
	: client()
	, log()
	, history_pos()
	, notify()
{
}

CChatItem::~CChatItem()
{
	delete log;
}

///////////////////////////////////////////////////////////////////////////////
// CChatSelector

IMPLEMENT_DYNAMIC(CChatSelector, CClosableTabCtrl)

BEGIN_MESSAGE_MAP(CChatSelector, CClosableTabCtrl)
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_TIMER()
	ON_WM_SYSCOLORCHANGE()
	ON_NOTIFY_REFLECT(TCN_SELCHANGE, OnTcnSelChangeChatSel)
	ON_WM_CONTEXTMENU()
END_MESSAGE_MAP()

CChatSelector::CChatSelector()
	: m_pParent()
	, m_Timer()
	, m_iContextIndex(-1)
	, m_blinkstate()
	, m_lastemptyicon()
{
}

void CChatSelector::Init(CChatWnd *pParent)
{
	m_pParent = pParent;

	ModifyStyle(0, WS_CLIPCHILDREN);
	SetAllIcons();

	VERIFY((m_Timer = SetTimer(IDT_CHATITEMS, SEC2MS(3) / 2, NULL)) != 0);
}

void CChatSelector::OnSysColorChange()
{
	CClosableTabCtrl::OnSysColorChange();
	SetAllIcons();
}

void CChatSelector::SetAllIcons()
{
	CImageList iml;
	iml.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	iml.Add(CTempIconLoader(_T("Chat")));
	iml.Add(CTempIconLoader(_T("Message")));
	iml.Add(CTempIconLoader(_T("MessagePending")));
	SetImageList(&iml);
	m_imlChat.DeleteImageList();
	m_imlChat.Attach(iml.Detach());
	SetPadding(CSize(12, 3));
}

void CChatSelector::UpdateFonts(CFont *pFont)
{
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	for (int i = GetItemCount(); --i >= 0;)
		if (GetItem(i, &ti)) {
			CChatItem *ci = reinterpret_cast<CChatItem*>(ti.lParam);
		ci->log->SetFont(pFont);
	}
}

CChatItem* CChatSelector::StartSession(CUpDownClient *client, bool show)
{
	if (show)
		m_pParent->m_wndMessage.SetFocus();
	if (GetTabByClient(client) >= 0) {
		if (show) {
			SetCurSel(GetTabByClient(client));
			ShowChat();
		}
		return NULL;
	}

	CChatItem *chatitem = new CChatItem();
	chatitem->client = client;
	chatitem->log = new CHTRichEditCtrl;

	CRect rcChat;
	GetChatSize(rcChat);
	if (GetItemCount() == 0)
		rcChat.top += 19; // add the height of the tab which is not yet there
	// If that style is not specified there are troubles with right clicking into the control for the very first time!?
	chatitem->log->Create(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL, rcChat, this, UINT_MAX);
	chatitem->log->ModifyStyleEx(0, WS_EX_STATICEDGE, SWP_FRAMECHANGED);
	chatitem->log->SendMessage(EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(3, 3));
	chatitem->log->SetEventMask(chatitem->log->GetEventMask() | ENM_LINK);
	chatitem->log->SetFont(&theApp.m_fontHyperText);
	chatitem->log->SetProfileSkinKey(_T("Chat"));
	chatitem->log->ApplySkin();
	chatitem->log->EnableSmileys(thePrefs.GetMessageEnableSmileys());

	PARAFORMAT pf = {};
	pf.cbSize = (UINT)sizeof pf;
	pf.dwMask = PFM_OFFSET;
	pf.dxOffset = 150;
	chatitem->log->SetParaFormat(pf);

	if (thePrefs.GetIRCAddTimeStamp())
		AddTimeStamp(chatitem);
	chatitem->log->AppendKeyWord(GetResString(_T("CHAT_START")) + ((client->GetUserName() == NULL || client->GetUserName()[0] == '\0') ? _T('(') + md4str(client->GetUserHash()) + _T(')') : client->GetUserName()) + _T('\n'), STATUS_MSG_COLOR);

	client->SetChatState(MS_CHATTING);

	CString name;
	if (client->GetUserName() != NULL && client->GetUserName()[0] != '\0')
		name = client->GetUserName();
	else 
		name.Format(_T("(%s)"), _T('(') + md4str(client->GetUserHash()) + _T(')') );
	chatitem->log->SetTitle(name);

	TCITEM ti;
	ti.mask = TCIF_PARAM | TCIF_TEXT | TCIF_IMAGE;
	ti.lParam = (LPARAM)chatitem;
	DupAmpersand(name);
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
	ti.iImage = 0;
	int iItemNr = InsertItem(GetItemCount(), &ti);
	if (show || IsWindowVisible()) {
		SetCurSel(iItemNr);
		ShowChat();
	}
	return chatitem;
}

int CChatSelector::GetTabByClient(CUpDownClient *client)
{
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	for (int i = GetItemCount(); --i >= 0;)
		if (GetItem(i, &ti) && reinterpret_cast<CChatItem*>(ti.lParam)->client == client)
			return i;
	return -1;
}

CChatItem* CChatSelector::GetItemByIndex(int index)
{
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	if (!GetItem(index, &ti))
		return NULL;

	return reinterpret_cast<CChatItem*>(ti.lParam);
}

CChatItem* CChatSelector::GetItemByClient(CUpDownClient *client)
{
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	for (int i = GetItemCount(); --i >= 0;)
		if (GetItem(i, &ti) && reinterpret_cast<CChatItem*>(ti.lParam)->client == client)
			return reinterpret_cast<CChatItem*>(ti.lParam);
	return NULL;
}

void CChatSelector::ProcessMessage(CUpDownClient *sender, const CString &message)
{
	CChatItem *ci = GetItemByClient(sender);

	CString strMessage(message);
	if (sender->GetUploadState() == US_BANNED)
		return; //just to be sure

	if (theApp.shield->CheckSpamMessage(sender, strMessage)) {
		if (ci)
			EndSession(sender);
		return;
	}

	sender->IncMessagesReceived(); // moved down

	AddLogLine(true, GetResString(_T("NEWMSG")), (LPCTSTR)EscPercent(sender->GetUserName()), (LPCTSTR)ipstr(sender->GetConnectIP()));

	bool isNewChatWindow = !ci;
	if (isNewChatWindow) {
		if ((UINT)GetItemCount() >= thePrefs.GetMsgSessionsMax())
			return;
		ci = StartSession(sender, false);
	}
	if (thePrefs.GetIRCAddTimeStamp())
		AddTimeStamp(ci);
	ci->log->AppendKeyWord(sender->GetUserName(), RECV_SOURCE_MSG_COLOR);
	ci->log->AppendText(_T(": "));
	ci->log->AppendText(message);
	ci->log->AppendText(_T("\n"));
	bool isCurTab = (GetTabByClient(sender) == GetCurSel());
	if (!isCurTab || !GetParent()->IsWindowVisible()) {
		ci->notify = true;
		if (isCurTab && (isNewChatWindow || thePrefs.GetNotifierOnEveryChatMsg())) {
			CString str;
			str.Format(_T("%s %s:'%s'\n"), (LPCTSTR)GetResString(_T("TBN_NEWCHATMSG"))
				, sender->GetUserName(), (LPCTSTR)message);
			theApp.emuledlg->ShowNotifier(str, TBN_CHAT);
		}
	}
}

void CChatSelector::ShowCaptchaRequest(CUpDownClient *sender, HBITMAP bmpCaptcha)
{
	CChatItem *ci = GetItemByClient(sender);
	if (ci != NULL) {
		if (thePrefs.GetIRCAddTimeStamp())
			AddTimeStamp(ci);
		ci->log->AppendKeyWord(_T("*** ") + GetResString(_T("CAPTCHAREQUEST")), STATUS_MSG_COLOR);
		ci->log->AddCaptcha(bmpCaptcha);
		ci->log->AddLine(_T("\n"));
	}
}

void CChatSelector::ShowCaptchaResult(CUpDownClient *sender, const CString &strResult)
{
	CChatItem *ci = GetItemByClient(sender);
	if (ci != NULL) {
		if (thePrefs.GetIRCAddTimeStamp())
			AddTimeStamp(ci);
		ci->log->AppendKeyWord(_T("*** ") + strResult + _T('\n'), STATUS_MSG_COLOR);
	}
}

bool CChatSelector::SendText(const CString &rstrText)
{
	CChatItem *ci = GetCurrentChatItem();
	if (!ci)
		return false;

	if (ci->history.GetCount() == thePrefs.GetMaxChatHistoryLines())
		ci->history.RemoveAt(0);
	ci->history.Add(rstrText);
	ci->history_pos = ci->history.GetCount();

	// advance spam filter stuff
	ci->client->IncMessagesSent();
	ci->client->SetSpammer(false);
	if (ci->client->GetChatState() == MS_CONNECTING)
		return false;

	if (ci->client->GetChatCaptchaState() == CA_CAPTCHARECV)
		ci->client->SetChatCaptchaState(CA_SOLUTIONSENT);
	else if (ci->client->GetChatCaptchaState() == CA_SOLUTIONSENT)
		ASSERT(0); // we responded to a captcha, but didn't hear from the client afterwards - hopefully it's just lag and this message would get through
	else
		ci->client->SetChatCaptchaState(CA_ACCEPTING);

	// there are three cases on connecting/sending the message:
	if (ci->client->socket && ci->client->socket->IsConnected()) {
		// 1.) the client is connected already - this is simple, just send it
		ci->client->SendChatMessage(rstrText);
		if (thePrefs.GetIRCAddTimeStamp())
			AddTimeStamp(ci);
		ci->log->AppendKeyWord(thePrefs.GetUserNick(), SENT_TARGET_MSG_COLOR);
		ci->log->AppendText(_T(": "));
		ci->log->AppendText(rstrText);
		ci->log->AppendText(_T("\n"));
	} else if (ci->client->GetFriend() != NULL) {
		// We are not connected and this client is a friend - friends have additional ways to connect and additional
		// checks to make sure they are really friends; let the friend class handle it
		ci->strMessagePending = rstrText;
		ci->client->SetChatState(MS_CONNECTING);
		ci->client->GetFriend()->TryToConnect(this);
	} else {
		// this is a normal client, who is not connected right now. just try to connect to the given IP, without any
		// additional checks or searchings.
		if (thePrefs.GetIRCAddTimeStamp())
			AddTimeStamp(ci);
		ci->log->AppendKeyWord(_T("*** ") + GetResString(_T("CONNECTING")), STATUS_MSG_COLOR);
		ci->strMessagePending = rstrText;
		ci->client->SetChatState(MS_CONNECTING);
		ci->client->TryToConnect(true);
	}
	return true;
}

void CChatSelector::ConnectingResult(CUpDownClient *sender, bool success)
{
	CChatItem *ci = GetItemByClient(sender);
	if (!ci)
		return;

	ci->client->SetChatState(MS_CHATTING);
	if (success)
		if (ci->strMessagePending.IsEmpty()) {
			if (thePrefs.GetIRCAddTimeStamp())
				AddTimeStamp(ci);
			ci->log->AppendKeyWord(_T("*** Connected\n"), STATUS_MSG_COLOR);
		} else {
			ci->log->AppendKeyWord(_T(" ...") + GetResString(_T("TREEOPTIONS_OK")) + _T('\n'), STATUS_MSG_COLOR);
			ci->client->SendChatMessage(ci->strMessagePending);

			if (thePrefs.GetIRCAddTimeStamp())
				AddTimeStamp(ci);
			ci->log->AppendKeyWord(thePrefs.GetUserNick(), SENT_TARGET_MSG_COLOR);
			ci->log->AppendText(_T(": "));
			ci->log->AppendText(ci->strMessagePending);
			ci->log->AppendText(_T("\n"));

			ci->strMessagePending.Empty();
		}
	else
		if (ci->strMessagePending.IsEmpty()) {
			if (thePrefs.GetIRCAddTimeStamp())
				AddTimeStamp(ci);
			ci->log->AppendKeyWord(GetResString(_T("CHATDISCONNECTED")) + _T('\n'), STATUS_MSG_COLOR);
		} else {
			ci->log->AppendKeyWord(_T(" ...") + GetResString(_T("FAILED")) + _T('\n'), STATUS_MSG_COLOR);
			ci->strMessagePending.Empty();
		}
}

void CChatSelector::DeleteAllItems()
{
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	for (int i = GetItemCount(); --i >= 0;)
		if (GetItem(i, &ti))
			delete reinterpret_cast<CChatItem*>(ti.lParam);
}

void CChatSelector::OnTimer(UINT_PTR /*nIDEvent*/)
{
	m_blinkstate = !m_blinkstate;
	bool globalnotify = false;
	TCITEM ti;
	ti.mask = TCIF_PARAM | TCIF_IMAGE;
	for (int i = GetItemCount(); --i >= 0;)
		if (GetItem(i, &ti)) {

			ti.mask = TCIF_IMAGE;
			if (reinterpret_cast<CChatItem*>(ti.lParam)->notify) {
				ti.iImage = (m_blinkstate) ? 1 : 2;
				SetItem(i, &ti);
			HighlightItem(i, TRUE);
			globalnotify = true;
			} else if (ti.iImage != 0) {
				ti.iImage = 0;
				SetItem(i, &ti);
			HighlightItem(i, FALSE);
		}
	}

	if (globalnotify) {
		theApp.emuledlg->ShowMessageState(m_blinkstate ? 1 : 2);
		m_lastemptyicon = false;
	} else if (!m_lastemptyicon) {
		theApp.emuledlg->ShowMessageState(0);
		m_lastemptyicon = true;
	}
}

CChatItem* CChatSelector::GetCurrentChatItem()
{
	int iCurSel = GetCurSel();
	if (iCurSel == -1)
		return NULL;

	TCITEM ti;
	ti.mask = TCIF_PARAM;
	if (!GetItem(iCurSel, &ti))
		return NULL;

	return reinterpret_cast<CChatItem*>(ti.lParam);
}

void CChatSelector::ShowChat()
{
	CChatItem *ci = GetCurrentChatItem();
	if (!ci)
		return;

	// show current chat window
	ci->log->ShowWindow(SW_SHOW);
	m_pParent->m_wndMessage.SetFocus();

	TCITEM ti;
	ti.mask = TCIF_IMAGE;
	ti.iImage = 0;
	SetItem(GetCurSel(), &ti);
	HighlightItem(GetCurSel(), FALSE);

	// hide all other chat windows
	ti.mask = TCIF_PARAM;
	int i = 0;
	while (GetItem(i++, &ti)) {
		CChatItem *ci2 = reinterpret_cast<CChatItem*>(ti.lParam);
		if (ci2 != ci)
			ci2->log->ShowWindow(SW_HIDE);
	}

	ci->notify = false;
}

void CChatSelector::OnTcnSelChangeChatSel(LPNMHDR, LRESULT *pResult)
{
	ShowChat();
	*pResult = 0;
}

int CChatSelector::InsertItem(int nItem, TCITEM *pTabCtrlItem)
{
	int iResult = CClosableTabCtrl::InsertItem(nItem, pTabCtrlItem);
	RedrawWindow();
	return iResult;
}

BOOL CChatSelector::DeleteItem(int nItem)
{
	CClosableTabCtrl::DeleteItem(nItem);
	RedrawWindow();
	return TRUE;
}

void CChatSelector::EndSession(CUpDownClient *client)
{
	int iCurSel = client ? GetTabByClient(client) : GetCurSel();
	if (iCurSel == -1)
		return;

	TCITEM ti;
	ti.mask = TCIF_PARAM;
	if (!GetItem(iCurSel, &ti) || ti.lParam == 0)
		return;
	CChatItem *ci = reinterpret_cast<CChatItem*>(ti.lParam);
	ci->client->SetChatState(MS_NONE);
	ci->client->SetChatCaptchaState(CA_NONE);

	DeleteItem(iCurSel);
	delete ci;

	int iTabItems = GetItemCount();
	if (iTabItems > 0) {
		// select next tab
		if (iCurSel >= iTabItems)
			iCurSel = iTabItems - 1;
		(void)SetCurSel(iCurSel);	// returns CB_ERR if error or no prev. selection(!)
		if (GetCurSel() == CB_ERR)	// get the real current selection
			(void)SetCurSel(0);		// if still error
		ShowChat();
	}
}

void CChatSelector::GetChatSize(CRect &rcChat)
{
	GetClientRect(&rcChat);
	AdjustRect(FALSE, rcChat);
	rcChat.InflateRect(-4, -4);
}

void CChatSelector::OnSize(UINT nType, int cx, int cy)
{
	CClosableTabCtrl::OnSize(nType, cx, cy);

	CRect rcChat;
	GetChatSize(rcChat);

	TCITEM ti;
	ti.mask = TCIF_PARAM;
	for (int i = 0; GetItem(i, &ti); ++i) {
		CChatItem *ci = reinterpret_cast<CChatItem*>(ti.lParam);
		ci->log->SetWindowPos(NULL, rcChat.left, rcChat.top, rcChat.Width(), rcChat.Height(), SWP_NOZORDER);
	}
}

void CChatSelector::AddTimeStamp(CChatItem *ci)
{
	ci->log->AppendText(CTime::GetCurrentTime().Format(TIME_STAMP_FORMAT));
}

void CChatSelector::OnDestroy()
{
	if (m_Timer) {
		KillTimer(m_Timer);
		m_Timer = NULL;
	}
	// Ensure all allocated chat items are deleted before the control is destroyed.
	// Otherwise the tab control may free its items without freeing the associated CChatItem.
	DeleteAllItems();
	CClosableTabCtrl::OnDestroy();
}

BOOL CChatSelector::OnCommand(WPARAM wParam, LPARAM lParam)
{
	switch (wParam) {
	case MP_DETAIL:
		{
			const CChatItem *ci = GetItemByIndex(m_iContextIndex);
			if (ci) {
				CClientDetailDialog dialog(ci->client);
				dialog.DoModal();
			}
		}
		return TRUE;
	case MP_ADDFRIEND:
		{
			const CChatItem *ci = GetItemByIndex(m_iContextIndex);
			if (ci) {
				CFriend* fr = theApp.friendlist->SearchFriend(ci->client->GetUserHash(), CAddress(), 0);
				if (!fr)
					theApp.friendlist->AddFriend(ci->client);
			}
		}
		return TRUE;
	case MP_REMOVEFRIEND:
		{
			const CChatItem *ci = GetItemByIndex(m_iContextIndex);
			if (ci) {
				CFriend* fr = theApp.friendlist->SearchFriend(ci->client->GetUserHash(), CAddress(), 0);
				if (fr)
					theApp.friendlist->RemoveFriend(fr);
			}
		}
		return TRUE;
	case MP_REMOVE:
		{
			const CChatItem *ci = GetItemByIndex(m_iContextIndex);
			if (ci)
				EndSession(ci->client);
		}
		return TRUE;
	}
	return CClosableTabCtrl::OnCommand(wParam, lParam);
}

void CChatSelector::OnContextMenu(CWnd*, CPoint point)
{
	TCHITTESTINFO hti;
	if (!::GetCursorPos(&hti.pt))
		return;
	ScreenToClient(&hti.pt);

	m_iContextIndex = HitTest(&hti);
	if (m_iContextIndex == -1)
		return;

	TCITEM ti;
	ti.mask = TCIF_PARAM;
	GetItem(m_iContextIndex, &ti);

	const CChatItem *ci = reinterpret_cast<CChatItem*>(ti.lParam);
	if (ci == NULL)
		return;

	CFriend* pFriend = theApp.friendlist->SearchFriend(ci->client->GetUserHash(), CAddress(), 0);

	CMenuXP menu;
	menu.CreatePopupMenu();
	menu.AddMenuSidebar(GetResString(_T("CLIENT")));

	menu.AppendMenu(MF_STRING, MP_DETAIL, GetResString(_T("SHOWDETAILS")), _T("CLIENTDETAILS"));

	GetCurrentChatItem();
	if (pFriend == NULL)
		menu.AppendMenu(MF_STRING, MP_ADDFRIEND, GetResString(_T("IRC_ADDTOFRIENDLIST")), _T("ADDFRIEND"));
	else
		menu.AppendMenu(MF_STRING, MP_REMOVEFRIEND, GetResString(_T("REMOVEFRIEND")), _T("DELETEFRIEND"));

	menu.AppendMenu(MF_STRING, MP_REMOVE, GetResString(_T("FD_CLOSE")));

	m_ptCtxMenu = point;
	ScreenToClient(&m_ptCtxMenu);
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
}

void CChatSelector::EnableSmileys(bool bEnable)
{
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	for (int i = GetItemCount(); --i >= 0;)
		if (GetItem(i, &ti) && reinterpret_cast<CChatItem*>(ti.lParam)->log)
			reinterpret_cast<CChatItem*>(ti.lParam)->log->EnableSmileys(bEnable);
}

void CChatSelector::ReportConnectionProgress(CUpDownClient *pClient, const CString &strProgressDesc, bool bNoTimeStamp)
{
	CChatItem *ci = GetItemByClient(pClient);
	if (!ci)
		return;
	if (thePrefs.GetIRCAddTimeStamp() && !bNoTimeStamp)
		AddTimeStamp(ci);
	ci->log->AppendKeyWord(strProgressDesc, STATUS_MSG_COLOR);
}

void CChatSelector::ClientObjectChanged(CUpDownClient *pOldClient, CUpDownClient *pNewClient)
{
	// the friend has decided to change the clients objects (because the old doesn't seem to be our friend) during a connection try
	// in order to not close and reopen a new session and lose the prior chat, switch the objects on an existing tab
	// nothing else changes since the tab is supposed to be still connected to the same friend
	CChatItem *ci = GetItemByClient(pOldClient);
	if (ci)
		ci->client = pNewClient;
}