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
#include "HelpIDs.h"
#include "Opcodes.h"
#include "FriendList.h"
#include "ChatWnd.h"
#include "ClientCredits.h"
#include "IconStatic.h"
#include "UserMsgs.h"
#include "SmileySelector.h"
#include "HttpDownloadDlg.h"
#include "ED2KLink.h"
#include "InputBox.h"
#include "MenuCmds.h"
#include "Log.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define	SPLITTER_HORZ_MARGIN	0
#define	SPLITTER_HORZ_WIDTH		4
#define	SPLITTER_HORZ_RANGE_MIN	170
#define	SPLITTER_HORZ_RANGE_MAX	400


// CChatWnd dialog

IMPLEMENT_DYNAMIC(CChatWnd, CDialog)

BEGIN_MESSAGE_MAP(CChatWnd, CResizableDialog)
	ON_WM_KEYDOWN()
	ON_WM_SHOWWINDOW()
	ON_MESSAGE(UM_CLOSETAB, OnCloseTab)
	ON_WM_SYSCOLORCHANGE()
	ON_WM_CTLCOLOR()
	ON_WM_CONTEXTMENU()
	ON_WM_HELPINFO()
	ON_NOTIFY(LVN_ITEMACTIVATE, IDC_FRIENDS_LIST, OnLvnItemActivateFriendList)
	ON_NOTIFY(NM_CLICK, IDC_FRIENDS_LIST, OnNmClickFriendList)
	ON_STN_DBLCLK(IDC_FRIENDSICON, OnStnDblClickFriendIcon)
	ON_BN_CLICKED(IDC_CSEND, OnBnClickedSend)
	ON_BN_CLICKED(IDC_CCLOSE, OnBnClickedClose)
	ON_BN_CLICKED(IDC_BTN_MENU, OnBnClickedBnmenu)
END_MESSAGE_MAP()

CChatWnd::CChatWnd(CWnd *pParent /*=NULL*/)
	: CResizableDialog(CChatWnd::IDD, pParent)
	, icon_friend()
	, icon_msg()
	, m_pwndSmileySel()
{
}

CChatWnd::~CChatWnd()
{
	if (m_pwndSmileySel != NULL) {
		m_pwndSmileySel->DestroyWindow();
		delete m_pwndSmileySel;
	}
	if (icon_friend)
		VERIFY(::DestroyIcon(icon_friend));
	if (icon_msg)
		VERIFY(::DestroyIcon(icon_msg));
}

void CChatWnd::DoDataExchange(CDataExchange *pDX)
{
	CResizableDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_CHATSEL, chatselector);
	DDX_Control(pDX, IDC_FRIENDS_LIST, m_FriendListCtrl);
	DDX_Control(pDX, IDC_FRIENDS_MSG, m_cUserInfo);
	DDX_Control(pDX, IDC_TEXT_FORMAT, m_wndFormat);
	DDX_Control(pDX, IDC_CMESSAGE, m_wndMessage);
	DDX_Control(pDX, IDC_CSEND, m_wndSend);
	DDX_Control(pDX, IDC_CCLOSE, m_wndClose);
}

void CChatWnd::OnLvnItemActivateFriendList(LPNMHDR, LRESULT*)
{
	UpdateSelectedFriendMsgDetails();
}

void CChatWnd::UpdateSelectedFriendMsgDetails()
{
	int iSel = m_FriendListCtrl.GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	if (iSel < 0 || iSel >= m_FriendListCtrl.GetItemCount())
		iSel = m_FriendListCtrl.GetSelectionMark();

	CFriend *pFriend = (iSel >= 0 && iSel < m_FriendListCtrl.GetItemCount()) ? reinterpret_cast<CFriend*>(m_FriendListCtrl.GetItemData(iSel)) : NULL;
	if (pFriend != NULL && (theApp.friendlist == NULL || !theApp.friendlist->IsValid(pFriend)))
		pFriend = NULL;

	ShowFriendMsgDetails(pFriend);
}

void CChatWnd::ShowFriendMsgDetails(CFriend *pFriend)
{
	if (pFriend != NULL && (theApp.friendlist == NULL || !theApp.friendlist->IsValid(pFriend)))
		pFriend = NULL;

	if (pFriend) {
		const CUpDownClient *linkedc = pFriend->GetLinkedClient(true);

		// Name, Hash, Client
		if (linkedc) {
			SetDlgItemText(IDC_FRIENDS_NAME_EDIT, linkedc->GetUserName());
			SetDlgItemText(IDC_FRIENDS_USERHASH_EDIT, md4str(linkedc->GetUserHash()));
			SetDlgItemText(IDC_FRIENDS_CLIENTE_EDIT, linkedc->GetClientSoftVer());
		} else {
			SetDlgItemText(IDC_FRIENDS_NAME_EDIT, (pFriend->m_strName.IsEmpty() ? _T("?") : (LPCTSTR)pFriend->m_strName));
			SetDlgItemText(IDC_FRIENDS_USERHASH_EDIT, (pFriend->HasUserhash() ? (LPCTSTR)md4str(pFriend->m_abyUserhash) : _T("?")));
			SetDlgItemText(IDC_FRIENDS_CLIENTE_EDIT, _T("?"));
		}

		if (linkedc && linkedc->Credits()) {
			// Identification
			if (theApp.clientcredits->CryptoAvailable()) {
				switch (linkedc->Credits()->GetCurrentIdentState(linkedc->GetIP())) {
				case IS_NOTAVAILABLE:
					SetDlgItemText(IDC_FRIENDS_IDENTIFICACION_EDIT, GetResString(_T("IDENTNOSUPPORT")));
					break;
				case IS_IDFAILED:
				case IS_IDNEEDED:
				case IS_IDBADGUY:
					SetDlgItemText(IDC_FRIENDS_IDENTIFICACION_EDIT, GetResString(_T("IDENTFAILED")));
					break;
				case IS_IDENTIFIED:
					SetDlgItemText(IDC_FRIENDS_IDENTIFICACION_EDIT, GetResString(_T("IDENTOK")));
				}
			} else
				SetDlgItemText(IDC_FRIENDS_IDENTIFICACION_EDIT, GetResString(_T("IDENTNOSUPPORT")));

			// Download
			SetDlgItemText(IDC_FRIENDS_DESCARGADO_EDIT, CastItoXBytes(linkedc->Credits()->GetDownloadedTotal()));
			// Upload
			SetDlgItemText(IDC_FRIENDS_SUBIDO_EDIT, CastItoXBytes(linkedc->Credits()->GetUploadedTotal()));
		} else {
			SetDlgItemText(IDC_FRIENDS_IDENTIFICACION_EDIT, _T("?"));
			SetDlgItemText(IDC_FRIENDS_DESCARGADO_EDIT, _T("?"));
			SetDlgItemText(IDC_FRIENDS_SUBIDO_EDIT, _T("?"));
		}

	} else {
		SetDlgItemText(IDC_FRIENDS_NAME_EDIT, _T("-"));
		SetDlgItemText(IDC_FRIENDS_USERHASH_EDIT, _T("-"));
		SetDlgItemText(IDC_FRIENDS_CLIENTE_EDIT, _T("-"));
		SetDlgItemText(IDC_FRIENDS_IDENTIFICACION_EDIT, _T("-"));
		SetDlgItemText(IDC_FRIENDS_DESCARGADO_EDIT, _T("-"));
		SetDlgItemText(IDC_FRIENDS_SUBIDO_EDIT, _T("-"));
	}
}

BOOL CChatWnd::OnInitDialog()
{
	CResizableDialog::OnInitDialog();
	InitWindowStyles(this);
	SetAllIcons();

	m_wndMessage.SetLimitText(MAX_CLIENT_MSG_LEN);
	if (theApp.m_fontChatEdit.m_hObject) {
		m_wndMessage.SendMessage(WM_SETFONT, (WPARAM)theApp.m_fontChatEdit.m_hObject, FALSE);
		RECT rcEdit;
		m_wndMessage.GetWindowRect(&rcEdit);
		ScreenToClient(&rcEdit);
		rcEdit.top -= 2;
		rcEdit.bottom += 2;
		m_wndMessage.MoveWindow(&rcEdit, FALSE);
	}

	chatselector.Init(this);
	m_FriendListCtrl.Init();

	if (theApp.m_fontSymbol.m_hObject)
	{
		GetDlgItem(IDC_BTN_MENU)->SetFont(&theApp.m_fontSymbol);
		GetDlgItem(IDC_BTN_MENU)->SetWindowText(_T("6")); // show a down-arrow
	}

	RECT rcSpl;
	m_FriendListCtrl.GetWindowRect(&rcSpl);
	ScreenToClient(&rcSpl);
	rcSpl.left = rcSpl.right + SPLITTER_HORZ_MARGIN;
	rcSpl.right = rcSpl.left + SPLITTER_HORZ_WIDTH;
	m_wndSplitterHorz.CreateWnd(WS_CHILD | WS_VISIBLE, rcSpl, this, IDC_SPLITTER_FRIEND);

	// Vista: Remove the TBSTYLE_TRANSPARENT to avoid flickering (can be done only after the toolbar was initially created with TBSTYLE_TRANSPARENT !?)
	m_wndFormat.SetExtendedStyle(m_wndFormat.GetExtendedStyle() | TBSTYLE_EX_MIXEDBUTTONS);
	TBBUTTON atb[1] = {};
	atb[0].idCommand = IDC_SMILEY;
	atb[0].fsState = TBSTATE_ENABLED;
	atb[0].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
	atb[0].iString = -1;
	m_wndFormat.AddButtons(_countof(atb), atb);

	SIZE size;
	m_wndFormat.GetMaxSize(&size);
	if (size.cx < 24) // avoid glitch with COMCTL32 v5.81 and Win2000
		size.cx = 24;
	if (size.cy < 22) // avoid glitch with COMCTL32 v5.81 and Win2000
		size.cy = 22;
	::SetWindowPos(m_wndFormat, NULL, 0, 0, size.cx, size.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

	AddOrReplaceAnchor(this, IDC_FRIENDSICON, TOP_LEFT);
	AddOrReplaceAnchor(this, IDC_FRIENDS_LBL, TOP_LEFT);
	AddOrReplaceAnchor(this, IDC_FRIENDS_NAME, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, IDC_FRIENDS_USERHASH, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, IDC_FRIENDS_CLIENT, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, IDC_FRIENDS_IDENT, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, IDC_FRIENDS_UPLOADED, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, IDC_FRIENDS_DOWNLOADED, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, m_wndSplitterHorz, TOP_LEFT, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, m_wndFormat, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, m_wndMessage, BOTTOM_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, m_wndSend, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, m_wndClose, BOTTOM_RIGHT);

	int iPosStatInit = rcSpl.left;
	int iPosStatNew = thePrefs.GetSplitterbarPositionFriend();
	if (iPosStatNew > SPLITTER_HORZ_RANGE_MAX)
		iPosStatNew = SPLITTER_HORZ_RANGE_MAX;
	else if (iPosStatNew < SPLITTER_HORZ_RANGE_MIN)
		iPosStatNew = SPLITTER_HORZ_RANGE_MIN;
	rcSpl.left = iPosStatNew;
	rcSpl.right = iPosStatNew + SPLITTER_HORZ_WIDTH;
	if (iPosStatNew != iPosStatInit) {
		m_wndSplitterHorz.MoveWindow(&rcSpl);
		DoResize(iPosStatNew - iPosStatInit);
	}

	Localize();
	theApp.friendlist->ShowFriends();
	EnableClose();
	OnChatTextChange();

	return TRUE;
}

void CChatWnd::DoResize(int iDelta)
{
	CSplitterControl::ChangeWidth(&m_FriendListCtrl, iDelta);
	m_FriendListCtrl.SetColumnWidth(0, LVSCW_AUTOSIZE_USEHEADER);
	CSplitterControl::ChangeWidth(&m_cUserInfo, iDelta);
	CSplitterControl::ChangeWidth(GetDlgItem(IDC_FRIENDS_NAME_EDIT), iDelta);
	CSplitterControl::ChangeWidth(GetDlgItem(IDC_FRIENDS_USERHASH_EDIT), iDelta);
	CSplitterControl::ChangeWidth(GetDlgItem(IDC_FRIENDS_CLIENTE_EDIT), iDelta);
	CSplitterControl::ChangeWidth(GetDlgItem(IDC_FRIENDS_IDENTIFICACION_EDIT), iDelta);
	CSplitterControl::ChangeWidth(GetDlgItem(IDC_FRIENDS_SUBIDO_EDIT), iDelta);
	CSplitterControl::ChangeWidth(GetDlgItem(IDC_FRIENDS_DESCARGADO_EDIT), iDelta);
	CSplitterControl::ChangeWidth(&chatselector, -iDelta, CW_RIGHTALIGN);
	CSplitterControl::ChangePos(GetDlgItem(IDC_MESSAGES_LBL), -iDelta, 0);
	CSplitterControl::ChangePos(GetDlgItem(IDC_MESSAGEICON), -iDelta, 0);
	CSplitterControl::ChangePos(&m_wndFormat, -iDelta, 0);
	CSplitterControl::ChangePos(&m_wndMessage, -iDelta, 0);
	CSplitterControl::ChangeWidth(&m_wndMessage, -iDelta);

	RECT rcSpl;
	m_wndSplitterHorz.GetWindowRect(&rcSpl);
	ScreenToClient(&rcSpl);
	thePrefs.SetSplitterbarPositionFriend(rcSpl.left);

	AddOrReplaceAnchor(this, m_FriendListCtrl, TOP_LEFT, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, m_cUserInfo, BOTTOM_LEFT, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, chatselector, TOP_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_FRIENDS_NAME_EDIT, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, IDC_FRIENDS_USERHASH_EDIT, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, IDC_FRIENDS_CLIENTE_EDIT, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, IDC_FRIENDS_IDENTIFICACION_EDIT, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, IDC_FRIENDS_SUBIDO_EDIT, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, IDC_FRIENDS_DESCARGADO_EDIT, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, m_wndSplitterHorz, TOP_LEFT, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, m_wndFormat, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, m_wndMessage, BOTTOM_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, m_wndSend, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, m_wndClose, BOTTOM_RIGHT);

	AddAllOtherAnchors();

	RECT rcWnd;
	GetWindowRect(&rcWnd);
	ScreenToClient(&rcWnd);
	m_wndSplitterHorz.SetRange(rcWnd.left + SPLITTER_HORZ_RANGE_MIN + SPLITTER_HORZ_WIDTH / 2
							, rcWnd.left + SPLITTER_HORZ_RANGE_MAX - SPLITTER_HORZ_WIDTH / 2);

	Invalidate();
	UpdateWindow();
}

LRESULT CChatWnd::DefWindowProc(UINT uMessage, WPARAM wParam, LPARAM lParam)
{
	switch (uMessage) {
	case WM_PAINT:
		if (m_wndSplitterHorz) {
			CRect rcWnd;
			GetWindowRect(rcWnd);
			if (rcWnd.Width() > 0) {
				RECT rcSpl;
				m_FriendListCtrl.GetWindowRect(&rcSpl);
				ScreenToClient(&rcSpl);
				rcSpl.left = rcSpl.right + SPLITTER_HORZ_MARGIN;
				rcSpl.right = rcSpl.left + SPLITTER_HORZ_WIDTH;
				ScreenToClient(rcWnd);
				rcSpl.bottom = rcWnd.bottom - 6;
				m_wndSplitterHorz.MoveWindow(&rcSpl, TRUE);
			}
		}
		break;
	case WM_NOTIFY:
		if (wParam == IDC_SPLITTER_FRIEND) {
			SPC_NMHDR *pHdr = reinterpret_cast<SPC_NMHDR*>(lParam);
			DoResize(pHdr->delta);
		}
		break;
	case WM_SIZE:
		if (m_wndSplitterHorz) {
			RECT rcWnd;
			GetWindowRect(&rcWnd);
			ScreenToClient(&rcWnd);
			m_wndSplitterHorz.SetRange(rcWnd.left + SPLITTER_HORZ_RANGE_MIN + SPLITTER_HORZ_WIDTH / 2
				, rcWnd.left + SPLITTER_HORZ_RANGE_MAX - SPLITTER_HORZ_WIDTH / 2);
		}
	}
	return CResizableDialog::DefWindowProc(uMessage, wParam, lParam);
}

void CChatWnd::StartSession(CUpDownClient *client)
{
	if (client != NULL && client->GetUserName()) {
		theApp.emuledlg->SetActiveDialog(this);
		chatselector.StartSession(client, true);
		EnableClose();
	}
}

void CChatWnd::OnShowWindow(BOOL bShow, UINT /*nStatus*/)
{
	if (bShow)
		chatselector.ShowChat();
}

BOOL CChatWnd::PreTranslateMessage(MSG *pMsg)
{
	if (theApp.emuledlg->m_pSplashWnd)
		return FALSE;
	if (pMsg->message == WM_KEYDOWN) {
		// Don't handle Ctrl+Tab in this window. It will be handled by main window.
		if (pMsg->wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0)
			return FALSE;

		if (pMsg->hwnd == m_wndMessage)
			switch (pMsg->wParam) {
			case VK_RETURN:
				OnBnClickedSend();
				break;
			case VK_UP:
			case VK_DOWN:
				ScrollHistory(pMsg->wParam == VK_DOWN);
				return TRUE;
			}

	} else if (pMsg->message == WM_KEYUP)
		if (pMsg->hwnd == m_FriendListCtrl.m_hWnd)
			OnLvnItemActivateFriendList(0, 0);

	OnChatTextChange();
	return CResizableDialog::PreTranslateMessage(pMsg);
}

void CChatWnd::OnNmClickFriendList(LPNMHDR pNMHDR, LRESULT *pResult)
{
	OnLvnItemActivateFriendList(pNMHDR, pResult);
	*pResult = 0;
}

void CChatWnd::OnChatTextChange()
{
	GetDlgItem(IDC_CSEND)->EnableWindow(m_wndMessage.GetWindowTextLength() > 0);
}

void CChatWnd::SetAllIcons()
{
	if (icon_friend)
		VERIFY(::DestroyIcon(icon_friend));
	if (icon_msg)
		VERIFY(::DestroyIcon(icon_msg));
	icon_friend = theApp.LoadIcon(_T("Friend"), 16, 16);
	icon_msg = theApp.LoadIcon(_T("Message"), 16, 16);
	static_cast<CStatic*>(GetDlgItem(IDC_MESSAGEICON))->SetIcon(icon_msg);
	static_cast<CStatic*>(GetDlgItem(IDC_FRIENDSICON))->SetIcon(icon_friend);
	m_cUserInfo.SetIcon(_T("Info"));

	CImageList iml;
	iml.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	iml.Add(CTempIconLoader(_T("Smiley_Smile")));
	CImageList *pimlOld = m_wndFormat.SetImageList(&iml);
	iml.Detach();
	if (pimlOld)
		pimlOld->DeleteImageList();
}

void CChatWnd::Localize()
{
	SetDlgItemText(IDC_FRIENDS_LBL, GetResString(_T("CW_FRIENDS")));
	SetDlgItemText(IDC_MESSAGES_LBL, GetResString(_T("CW_MESSAGES")));
	m_cUserInfo.SetWindowText(GetResString(_T("INFO")));
	SetDlgItemText(IDC_FRIENDS_DOWNLOADED, GetResString(_T("CHAT_DOWNLOADED")));
	SetDlgItemText(IDC_FRIENDS_UPLOADED, GetResString(_T("CHAT_UPLOADED")));
	SetDlgItemText(IDC_FRIENDS_IDENT, GetResString(_T("CHAT_IDENT")));
	SetDlgItemText(IDC_FRIENDS_CLIENT, GetResString(_T("CD_CSOFT")));
	SetDlgItemText(IDC_FRIENDS_NAME, GetResString(_T("CD_UNAME")));
	SetDlgItemText(IDC_FRIENDS_USERHASH, GetResString(_T("CD_UHASH")));
	m_wndSend.SetWindowText(GetResString(_T("CW_SEND")));
	m_wndClose.SetWindowText(GetResString(_T("CW_CLOSE")));
	m_wndFormat.SetBtnText(IDC_SMILEY, _T("Smileys"));
	m_FriendListCtrl.Localize();
}

LRESULT CChatWnd::OnCloseTab(WPARAM wParam, LPARAM)
{
	TCITEM ti;
	ti.mask = TCIF_PARAM;
	if (chatselector.GetItem((int)wParam, &ti))
		chatselector.EndSession(reinterpret_cast<CChatItem*>(ti.lParam)->client);
	EnableClose();
	return TRUE;
}

void CChatWnd::ScrollHistory(bool down)
{
	CChatItem *ci = chatselector.GetCurrentChatItem();
	if (ci == NULL)
		return;
	INT_PTR last = ci->history.GetCount();
	if ((ci->history_pos <= 0 && !down) || (ci->history_pos >= last && down))
		return;
	if (down)
		++ci->history_pos;
	else
		--ci->history_pos;

	LPCTSTR pTxt;
	DWORD len;
	if (ci->history_pos >= last) {
		pTxt = (LPCTSTR)ci->history[last];
		len = ci->history[last].GetLength();
	} else {
		pTxt = EMPTY;
		len = 0;
	}
	m_wndMessage.SetWindowText(pTxt);
	m_wndMessage.SetSel(len, len);
}

void CChatWnd::OnSysColorChange()
{
	CResizableDialog::OnSysColorChange();
	SetAllIcons();
}

void CChatWnd::UpdateFriendlistCount(INT_PTR count)
{
	CString sCount;
	sCount.Format(_T("%s (%u)"), (LPCTSTR)GetResString(_T("CW_FRIENDS")), (unsigned)count);
	SetDlgItemText(IDC_FRIENDS_LBL, sCount);
}

BOOL CChatWnd::OnHelpInfo(HELPINFO*)
{
	theApp.ShowHelp(eMule_FAQ_GUI_Messages);
	return TRUE;
}

void CChatWnd::OnStnDblClickFriendIcon()
{
	theApp.emuledlg->ShowPreferences(IDD_PPG_MESSAGES);
}

BOOL CChatWnd::OnCommand(WPARAM wParam, LPARAM lParam)
{
	switch (wParam) {
		case IDC_SMILEY:
			OnBnClickedSmiley();
			break;
		case MP_GETFRIENDED2KLINK:
		{
			CString sLink;
			CED2KFriendLink myLink(CPreferences::GetUserNick(), CPreferences::GetUserHash());
			myLink.GetLink(sLink);
			theApp.CopyTextToClipboard(sLink);
		}
		break;
		case MP_GETHTMLFRIENDED2KLINK:
		{
			CString sLink;
			CED2KFriendLink myLink(CPreferences::GetUserNick(), CPreferences::GetUserHash());
			myLink.GetLink(sLink);
			sLink = _T("<a href=\"") + sLink + _T("\">") + StripInvalidFilenameChars(CPreferences::GetUserNick()) + _T("</a>");
			theApp.CopyTextToClipboard(sLink);
		}
		case MP_GETEMFRIENDMETFROMURL: {

			InputBox inp;
			inp.SetLabels(GetResString(_T("DOWNLOADEMFRIENDSMET")), GetResString(_T("EMFRIENDSMETURL")), EMPTY);
			inp.DoModal();
			CString url = inp.GetInput();

			if (!url.IsEmpty() && !inp.WasCancelled())
				UpdateEmfriendsMetFromURL(url);
		} break;
	}
	return CResizableDialog::OnCommand(wParam, lParam);
}

void CChatWnd::OnBnClickedSmiley()
{
	if (m_pwndSmileySel) {
		m_pwndSmileySel->DestroyWindow();
		delete m_pwndSmileySel;
		m_pwndSmileySel = NULL;
	}
	m_pwndSmileySel = new CSmileySelector;

	RECT rcBtn;
	m_wndFormat.GetWindowRect(&rcBtn);
	rcBtn.top -= 2;

	if (!m_pwndSmileySel->CreateWnd(this, &rcBtn, &m_wndMessage)) {
		delete m_pwndSmileySel;
		m_pwndSmileySel = NULL;
	}
}

void CChatWnd::OnBnClickedClose()
{
	chatselector.EndSession();
	EnableClose();
}

void CChatWnd::OnBnClickedSend()
{
	CString strText;
	m_wndMessage.GetWindowText(strText);
	if (!strText.Trim().IsEmpty() && chatselector.SendText(strText))
		m_wndMessage.SetWindowText(EMPTY);

	m_wndMessage.SetFocus();
}

HBRUSH CChatWnd::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = theApp.emuledlg->GetCtlColor(pDC, pWnd, nCtlColor);
	return hbr ? hbr : __super::OnCtlColor(pDC, pWnd, nCtlColor);
}

bool CChatWnd::UpdateEmfriendsMetFromURL(const CString& strURL)
{
	if (strURL.IsEmpty() || strURL.Find(_T("://")) == -1)	// not a valid URL
	{
		LogError(LOG_STATUSBAR, GetResString(_T("INVALIDURL")));
		return false;
	}

	CString strTempFilename;
	strTempFilename.Format(_T("%stemp-%d-emfriends.met"), thePrefs.GetMuleDirectory(EMULE_CONFIGDIR), ::GetTickCount());

	// step2 - try to download emfriends.met
	CHttpDownloadDlg dlgDownload;
	dlgDownload.m_sURLToDownload = strURL;
	dlgDownload.m_sFileToDownloadInto = strTempFilename;
	if (dlgDownload.DoModal() != IDOK)
	{
		LogError(LOG_STATUSBAR, GetResString(_T("ERR_FAILEDDOWNLOADEMFRIENDS")), strURL);
		return false;
	}

	// step3 - add content of emfriends.met to friend list
	m_FriendListCtrl.AddEmfriendsMetToList(strTempFilename);

	_tremove(strTempFilename);
	return true;
}

void CChatWnd::OnBnClickedBnmenu()
{
	CMenuXP tmColumnMenu;
	VERIFY(tmColumnMenu.CreatePopupMenu());
	tmColumnMenu.AddMenuSidebar(GetResString(_T("FRIENDLINKMENUTITLE")));

	VERIFY(tmColumnMenu.AppendMenu(MF_STRING, MP_GETFRIENDED2KLINK, GetResString(_T("GETMYFRIENDED2KLINK")), _T("ED2KLINK")));
	VERIFY(tmColumnMenu.AppendMenu(MF_STRING, MP_GETHTMLFRIENDED2KLINK, GetResString(_T("GETMYHTMLFRIENDED2KLINK")), _T("ED2KLINK")));
	VERIFY(tmColumnMenu.AppendMenu(MF_SEPARATOR));
	VERIFY(tmColumnMenu.AppendMenu(MF_STRING, MP_GETEMFRIENDMETFROMURL, GetResString(_T("DOWNLOADEMFRIENDSMET")), _T("WEB"))); //MORPH - Added by Commander, Manual Download and load of emfriends.met

	RECT rectBtn;
	GetDlgItem(IDC_BTN_MENU)->GetWindowRect(&rectBtn);

	tmColumnMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, rectBtn.right, rectBtn.bottom, this);
	VERIFY(tmColumnMenu.DestroyMenu());
}
