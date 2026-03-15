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
#include "KadContactHistogramCtrl.h"
#include "KadLookupGraph.h"
#include "KadSearchListCtrl.h"
#include "Kademlia/Kademlia/kademlia.h"
#include "Kademlia/Kademlia/prefs.h"
#include "kademlia/utils/LookupHistory.h"
#include "Kademlia/net/kademliaudplistener.h"
#include "kademlia/kademlia/search.h"
#include "Ini2.h"
#include "CustomAutoComplete.h"
#include "OtherFunctions.h"
#include "emuledlg.h"
#include "clientlist.h"
#include "log.h"
#include "HttpDownloadDlg.h"
#include "Kademlia/routing/RoutingZone.h"
#include "HelpIDs.h"
#include "kademliawnd.h"
#include "DropDownButton.h"
#include "MenuCmds.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define	ONBOOTSTRAP_STRINGS_PROFILE	_T("AC_BootstrapIPs.dat")
#define	ONBOOTSTRAP_URL_STRINGS_PROFILE	_T("AC_BootstrapURLs.dat")

#define	DFLT_TOOLBAR_BTN_WIDTH	24

#define	WND1_BUTTON_XOFF	8
#define	WND1_BUTTON_WIDTH	250
#define	WND1_BUTTON_HEIGHT	22	// don't set the height to something different than 22 unless you know exactly what you are doing!
#define	WND1_NUM_BUTTONS	2

// KademliaWnd dialog

IMPLEMENT_DYNAMIC(CKademliaWnd, CDialog)

BEGIN_MESSAGE_MAP(CKademliaWnd, CResizableDialog)
	ON_BN_CLICKED(IDC_BOOTSTRAPBUTTON, OnBnClickedBootstrapbutton)
	ON_BN_CLICKED(IDC_FIREWALLCHECKBUTTON, OnBnClickedFirewallcheckbutton)
	ON_BN_CLICKED(IDC_KADCONNECT, OnBnConnect)
	ON_BN_CLICKED(IDC_DD, OnDDClicked)
	ON_BN_CLICKED(IDC_DD2, OnDD2Clicked)
	ON_WM_SYSCOLORCHANGE()
	ON_WM_CTLCOLOR()
	ON_WM_SETTINGCHANGE()
	ON_EN_SETFOCUS(IDC_BOOTSTRAPIPPORT, OnEnSetfocusBootstrapip)
	ON_EN_CHANGE(IDC_BOOTSTRAPIPPORT, UpdateControlsState)
	ON_EN_SETFOCUS(IDC_BOOTSTRAPURL, OnEnSetfocusBootstrapNodesdat)
	ON_EN_CHANGE(IDC_BOOTSTRAPURL, UpdateControlsState)
	ON_BN_CLICKED(IDC_RADCLIENTS, UpdateControlsState)
	ON_BN_CLICKED(IDC_RADIP, UpdateControlsState)
	ON_BN_CLICKED(IDC_RADNODESURL, UpdateControlsState)
	ON_WM_HELPINFO()
	ON_NOTIFY(NM_DBLCLK, IDC_KADSEARCHLIST, OnNMDblclkSearchlist)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_KADSEARCHLIST, OnListModifiedSearchlist)
END_MESSAGE_MAP()

CKademliaWnd::CKademliaWnd(CWnd *pParent /*=NULL*/)
	: CResizableDialog(CKademliaWnd::IDD, pParent)
	, m_btnsetsize(true)
	, m_pacONBSIPs()
	, m_pacONBSURLs()
	, icon_kadsea()
	, m_bBootstrapListMode()
{
	m_contactListCtrl = new CKadContactListCtrl;
	m_contactHistogramCtrl = new CKadContactHistogramCtrl;
	m_kadLookupGraph = new CKadLookupGraph;
	searchList = new CKadSearchListCtrl;
	m_pbtnWnd = new CDropDownButton;
}

CKademliaWnd::~CKademliaWnd()
{
	if (m_pacONBSIPs) {
		m_pacONBSIPs->Unbind();
		m_pacONBSIPs->Release();
	}

	if (m_pacONBSURLs) {
		m_pacONBSURLs->Unbind();
		m_pacONBSURLs->Release();
	}

	delete m_contactListCtrl;
	delete m_contactHistogramCtrl;
	delete searchList;
	delete m_kadLookupGraph;
	delete m_pbtnWnd;

	if (icon_kadsea)
		VERIFY(::DestroyIcon(icon_kadsea));
}

BOOL CKademliaWnd::SaveAllSettings()
{
	if (m_pacONBSIPs)
		m_pacONBSIPs->SaveList(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + ONBOOTSTRAP_STRINGS_PROFILE);

	if (m_pacONBSURLs)
		m_pacONBSURLs->SaveList(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + ONBOOTSTRAP_URL_STRINGS_PROFILE);

	return TRUE;
}

BOOL CKademliaWnd::OnInitDialog()
{
	CResizableDialog::OnInitDialog();
	InitWindowStyles(this);
	m_contactListCtrl->Init();
	m_kadLookupGraph->Init();
	searchList->Init();

	// Initialize Toolbar
	RECT rcBtn1;
	rcBtn1.left = WND1_BUTTON_XOFF;
	rcBtn1.top = 5;
	rcBtn1.right = rcBtn1.left + WND1_BUTTON_WIDTH + WND1_NUM_BUTTONS * DFLT_TOOLBAR_BTN_WIDTH;
	rcBtn1.bottom = rcBtn1.top + WND1_BUTTON_HEIGHT;
	m_pbtnWnd->Init(false);
	m_pbtnWnd->MoveWindow(&rcBtn1);
	SetAllIcons();

	// Vista: Remove the TBSTYLE_TRANSPARENT to avoid flickering (can be done only after the toolbar was initially created with TBSTYLE_TRANSPARENT !?)
	m_pbtnWnd->SetExtendedStyle(m_pbtnWnd->GetExtendedStyle() | TBSTYLE_EX_MIXEDBUTTONS);

	TBBUTTON atb1[1 + WND1_NUM_BUTTONS] = {};
	atb1[0].idCommand = IDC_KADICO1;
	atb1[0].fsState = TBSTATE_ENABLED;
	atb1[0].fsStyle = BTNS_BUTTON | BTNS_SHOWTEXT;
	atb1[0].iString = -1;

	atb1[1].idCommand = MP_VIEW_KADCONTACTS;
	atb1[1].fsState = TBSTATE_ENABLED;
	atb1[1].fsStyle = BTNS_BUTTON | BTNS_CHECKGROUP | BTNS_AUTOSIZE;
	atb1[1].iString = -1;

	atb1[2].iBitmap = 1;
	atb1[2].idCommand = MP_VIEW_KADLOOKUP;
	atb1[2].fsState = TBSTATE_ENABLED;
	atb1[2].fsStyle = BTNS_BUTTON | BTNS_CHECKGROUP | BTNS_AUTOSIZE;
	atb1[2].iString = -1;
	m_pbtnWnd->AddButtons(_countof(atb1), atb1);

	TBBUTTONINFO tbbi = {};
	tbbi.cbSize = (UINT)sizeof tbbi;
	tbbi.dwMask = TBIF_SIZE | TBIF_BYINDEX;
	tbbi.cx = WND1_BUTTON_WIDTH;
	m_pbtnWnd->SetButtonInfo(0, &tbbi);

	// 'GetMaxSize' does not work properly under:
	//	- Win98SE with COMCTL32 v5.80
	//	- Win2000 with COMCTL32 v5.81
	// The value returned by 'GetMaxSize' is just couple of pixels too small so that the
	// last toolbar button is nearly not visible at all.
	// So, to circumvent such problems, the toolbar control should be created right with
	// the needed size so that we do not really need to call the 'GetMaxSize' function.
	// Although it would be better to call it to adapt for system metrics basically.
	if (theApp.m_ullComCtrlVer > MAKEDLLVERULL(5, 81, 0, 0)) {
		SIZE size;
		m_pbtnWnd->GetMaxSize(&size);
		CRect rc;
		m_pbtnWnd->GetWindowRect(rc);
		ScreenToClient(rc);
		// the with of the toolbar should already match the needed size (see comment above)
		ASSERT(size.cx == rc.Width());
		m_pbtnWnd->MoveWindow(rc.left, rc.top, size.cx, rc.Height());
	}

	Localize();

	AddOrReplaceAnchor(this, IDC_KADICO1, TOP_LEFT);
	AddOrReplaceAnchor(this, IDC_CONTACTLIST, TOP_LEFT, MIDDLE_RIGHT);
	AddOrReplaceAnchor(this, IDC_KAD_LOOKUPGRAPH, TOP_LEFT, MIDDLE_RIGHT);
	AddOrReplaceAnchor(this, IDC_KAD_HISTOGRAM, TOP_RIGHT, MIDDLE_RIGHT);
	AddOrReplaceAnchor(this, IDC_KADICO2, MIDDLE_LEFT);
	AddOrReplaceAnchor(this, IDC_KADSEARCHLIST, MIDDLE_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_KADSEARCHLAB, MIDDLE_LEFT);

	// Resize the edit controls to fit the down-arrow button before anchors are snapshotted
	if (thePrefs.GetUseAutocompletion()) {
		CWnd* pIPPort = GetDlgItem(IDC_BOOTSTRAPIPPORT);
		if (pIPPort) {
			CRect rcIPPort;
			pIPPort->GetWindowRect(&rcIPPort);
			ScreenToClient(&rcIPPort);
			rcIPPort.right = rcIPPort.right - 16;
			pIPPort->MoveWindow(rcIPPort, TRUE);
		}

		CWnd* pURL = GetDlgItem(IDC_BOOTSTRAPURL);
		if (pURL) {
			CRect rcURL;
			pURL->GetWindowRect(&rcURL);
			ScreenToClient(&rcURL);
			rcURL.right = rcURL.right - 16;
			pURL->MoveWindow(rcURL, TRUE);
		}
	}

	AddAllOtherAnchors(TOP_RIGHT);

	searchList->UpdateKadSearchCount();
	m_contactListCtrl->UpdateKadContactCount();

	if (thePrefs.GetUseAutocompletion()) {
		if (m_pacONBSIPs) {
			m_pacONBSIPs->Unbind();
			m_pacONBSIPs->Release();
		}
		m_pacONBSIPs = new CCustomAutoComplete();
		if (m_pacONBSIPs) {
			m_pacONBSIPs->AddRef();
			if (m_pacONBSIPs->Bind(::GetDlgItem(m_hWnd, IDC_BOOTSTRAPIPPORT), ACO_UPDOWNKEYDROPSLIST | ACO_AUTOSUGGEST | ACO_FILTERPREFIXES))
				m_pacONBSIPs->LoadList(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + ONBOOTSTRAP_STRINGS_PROFILE);

			int n = m_pacONBSIPs->GetItemCount();
			if (n > 0) {
				CString last = m_pacONBSIPs->GetItem(0);
				if (!last.IsEmpty()) {
					SetDlgItemText(IDC_BOOTSTRAPIPPORT, last);
				}
			}
		}

		if (m_pacONBSURLs) {
			m_pacONBSURLs->Unbind();
			m_pacONBSURLs->Release();
		}
		m_pacONBSURLs = new CCustomAutoComplete();
		if (m_pacONBSURLs) {
			m_pacONBSURLs->AddRef();
			if (m_pacONBSURLs->Bind(::GetDlgItem(m_hWnd, IDC_BOOTSTRAPURL), ACO_UPDOWNKEYDROPSLIST | ACO_AUTOSUGGEST | ACO_FILTERPREFIXES))
				m_pacONBSURLs->LoadList(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + ONBOOTSTRAP_URL_STRINGS_PROFILE);

			int n = m_pacONBSURLs->GetItemCount();
			if (n > 0) {
				CString last = m_pacONBSURLs->GetItem(0);
				if (!last.IsEmpty()) {
					SetDlgItemText(IDC_BOOTSTRAPURL, last);
				}
			}
		}

		if (theApp.m_fontSymbol.m_hObject) {
			GetDlgItem(IDC_DD)->SetFont(&theApp.m_fontSymbol);
			SetDlgItemText(IDC_DD, _T("6")); // show a down-arrow
			GetDlgItem(IDC_DD2)->SetFont(&theApp.m_fontSymbol);
			SetDlgItemText(IDC_DD2, _T("6")); // show a down-arrow
		}
	} else 
		 GetDlgItem(IDC_DD2)->ShowWindow(SW_HIDE);
	
	if (thePrefs.GetBootstrapSelection() == B_URL)
		CheckDlgButton(IDC_RADNODESURL, 1);
	else if (thePrefs.GetBootstrapSelection() == B_IP)
		CheckDlgButton(IDC_RADIP, 1);
	else 
		CheckDlgButton(IDC_RADCLIENTS, 1);

	UpdateControlsState();

	ShowLookupGraph(false);

	return TRUE;
}

void CKademliaWnd::DoDataExchange(CDataExchange *pDX)
{
	CResizableDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_CONTACTLIST, *m_contactListCtrl);
	DDX_Control(pDX, IDC_KAD_HISTOGRAM, *m_contactHistogramCtrl);
	DDX_Control(pDX, IDC_KAD_LOOKUPGRAPH, *m_kadLookupGraph);
	DDX_Control(pDX, IDC_KADSEARCHLIST, *searchList);
	DDX_Control(pDX, IDC_BSSTATIC, m_ctrlBootstrap);
	DDX_Control(pDX, IDC_KADICO1, *m_pbtnWnd);
}

BOOL CKademliaWnd::PreTranslateMessage(MSG* pMsg)
{
	if (theApp.emuledlg->m_pSplashWnd) //splash or about dialogs are active
		return FALSE;

	if (m_btnsetsize) {
		m_btnsetsize = false;
		if (m_pbtnWnd && m_pbtnWnd->m_hWnd && m_pbtnWnd->GetBtnWidth(IDC_KADICO1) != WND1_BUTTON_WIDTH)
			m_pbtnWnd->SetBtnWidth(IDC_KADICO1, WND1_BUTTON_WIDTH);
	}

	if (pMsg->message == WM_KEYDOWN) {
		// Don't handle Ctrl+Tab in this window. It will be handled by the main window.
		if (pMsg->wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0)
			return FALSE;
		switch (pMsg->wParam) {
		case VK_ESCAPE:
			return FALSE;
		case VK_DELETE:
			if (m_pacONBSURLs && m_pacONBSURLs->IsBound() && pMsg->hwnd == GetDlgItem(IDC_BOOTSTRAPURL)->m_hWnd) {
				if (GetKeyState(VK_MENU) < 0 || GetKeyState(VK_CONTROL) < 0)
					m_pacONBSURLs->Clear();
				else
					m_pacONBSURLs->RemoveSelectedItem();
			} else if (m_pacONBSIPs && m_pacONBSIPs->IsBound() && pMsg->hwnd == GetDlgItem(IDC_BOOTSTRAPIPPORT)->m_hWnd) {
				if (GetKeyState(VK_MENU) < 0 || GetKeyState(VK_CONTROL) < 0)
					m_pacONBSIPs->Clear();
				else
					m_pacONBSIPs->RemoveSelectedItem();
			}
			break;
		case VK_RETURN:
			if (GetDlgItem(IDC_BOOTSTRAPBUTTON)->IsWindowEnabled() &&
				(pMsg->hwnd == GetDlgItem(IDC_BOOTSTRAPIPPORT)->m_hWnd || pMsg->hwnd == GetDlgItem(IDC_BOOTSTRAPURL)->m_hWnd)) {
				OnBnClickedBootstrapbutton();
				return TRUE;
			}
		}
	}

	return CResizableDialog::PreTranslateMessage(pMsg);
}

void CKademliaWnd::OnEnSetfocusBootstrapip()
{
	CheckRadioButton(IDC_RADIP, IDC_RADNODESURL, IDC_RADIP);
	UpdateControlsState();
}

void CKademliaWnd::OnEnSetfocusBootstrapNodesdat()
{
	CheckRadioButton(IDC_RADIP, IDC_RADNODESURL, IDC_RADNODESURL);
	UpdateControlsState();
}

void CKademliaWnd::OnBnClickedBootstrapbutton()
{
	uint8 bSelection = EBootstrapSelection::B_KNOWN;

	if (IsDlgButtonChecked(IDC_RADIP)) {
		bSelection = EBootstrapSelection::B_IP;
		CString strIP;
		GetDlgItemText(IDC_BOOTSTRAPIPPORT, strIP);

		// auto-handle ip:port
		uint16 nPort = 0;
		int iPos = strIP.Trim().Find(_T(':'));
		if (iPos >= 0) {
			nPort = (uint16)_ttoi(CPTR(strIP, iPos + 1));
			strIP.Truncate(iPos);
		}

		// Invalid IP/hostname or port
		if (strIP.IsEmpty() || nPort == 0) {
			GetDlgItem(IDC_BOOTSTRAPIPPORT)->SetFocus();
			return;
		}

		CString strIPPort;
		strIPPort.Format(_T("%s:%u"), strIP, nPort);

		if (m_pacONBSIPs && m_pacONBSIPs->IsBound())
			m_pacONBSIPs->AddItem(strIPPort, 0);

		if (!Kademlia::CKademlia::IsRunning()) {
			Kademlia::CKademlia::Start();
			theApp.emuledlg->ShowConnectionState();
		}
		// Prefer numeric IPv4 path when possible to avoid name resolution edge cases
		CStringA hostA(strIP);
		uint32 raw = inet_addr(hostA);
		if (raw != INADDR_NONE) {
			// inet_addr returns network byte order; CKademlia expects host order
			uint32 ipHostOrder = ntohl(raw);
			Kademlia::CKademlia::Bootstrap(ipHostOrder, nPort);
		} else
			Kademlia::CKademlia::Bootstrap(strIP, nPort);
	} else if (IsDlgButtonChecked(IDC_RADNODESURL)) {
		bSelection = EBootstrapSelection::B_URL;
		CString strURL;
		GetDlgItemText(IDC_BOOTSTRAPURL, strURL);
		if (strURL.Find(_T("://")) < 0) {
			// not a valid URL
			LogError(LOG_STATUSBAR, GetResString(_T("INVALIDURL")));
			return;
		}

		if (m_pacONBSURLs && m_pacONBSURLs->IsBound())
			m_pacONBSURLs->AddItem(strURL, 0);

		UpdateNodesDatFromURL(strURL);
	} else if (!Kademlia::CKademlia::IsRunning()) {
		Kademlia::CKademlia::Start();
		theApp.emuledlg->ShowConnectionState();
	}

	thePrefs.SetBootstrapSelection(bSelection);
}

void CKademliaWnd::OnDDClicked()
{
	CWnd *box = GetDlgItem(IDC_BOOTSTRAPIPPORT);
	box->SetFocus();
	box->SetWindowText(EMPTY);
	box->SendMessage(WM_KEYDOWN, VK_DOWN, 0x00510001);
}

void CKademliaWnd::OnDD2Clicked()
{
	CWnd* box = GetDlgItem(IDC_BOOTSTRAPURL);
	box->SetFocus();
	box->SetWindowText(EMPTY);
	box->SendMessage(WM_KEYDOWN, VK_DOWN, 0x00510001);
}

void CKademliaWnd::OnBnClickedFirewallcheckbutton()
{
	Kademlia::CKademlia::RecheckFirewalled();
}

void CKademliaWnd::OnBnConnect()
{
	const bool bShouldConnect = !(Kademlia::CKademlia::IsConnected() || Kademlia::CKademlia::IsRunning());
	if (bShouldConnect) {
		if (!HasNodesDatContacts()) {
			CDarkMode::MessageBox(GetResString(_T("EMULE_AI_NODESDAT_REQUIRED_CONNECT")), MB_OK | MB_ICONINFORMATION);
		}
	}

	if (!bShouldConnect)
		Kademlia::CKademlia::Stop();
	else
		Kademlia::CKademlia::Start();
	theApp.emuledlg->ShowConnectionState();
}

void CKademliaWnd::OnSysColorChange()
{
	CResizableDialog::OnSysColorChange();
	SetAllIcons();
}

void CKademliaWnd::SetAllIcons()
{
	// frames
	m_ctrlBootstrap.SetIcon(_T("KadBootstrap"));

	if (icon_kadsea)
		VERIFY(::DestroyIcon(icon_kadsea));
	icon_kadsea = theApp.LoadIcon(_T("KadCurrentSearches"), 16, 16);
	static_cast<CStatic*>(GetDlgItem(IDC_KADICO2))->SetIcon(icon_kadsea);

	CImageList iml;
	iml.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 1, 1);
	iml.Add(CTempIconLoader(_T("KadContactList")));
	iml.Add(CTempIconLoader(_T("FriendSlot")));
	CImageList *pImlOld = m_pbtnWnd->SetImageList(&iml);
	iml.Detach();
	if (pImlOld)
		pImlOld->DeleteImageList();
}

void CKademliaWnd::Localize()
{
	m_ctrlBootstrap.SetWindowText(GetResString(_T("BOOTSTRAP")));
	SetDlgItemText(IDC_BOOTSTRAPBUTTON, GetResString(_T("BOOTSTRAP")));
	SetDlgItemText(IDC_SSTATIC4, GetResString(_T("SV_ADDRESS_PORT")));
	SetDlgItemText(IDC_NODESDATLABEL, GetResString(_T("BOOTSRAPNODESDAT")));
	SetDlgItemText(IDC_FIREWALLCHECKBUTTON, GetResString(_T("KAD_RECHECKFW")));

	SetDlgItemText(IDC_RADCLIENTS, GetResString(_T("RADCLIENTS")));

	UpdateControlsState();
	UpdateButtonTitle(m_pbtnWnd->IsButtonChecked(MP_VIEW_KADLOOKUP) != FALSE);
	m_contactHistogramCtrl->Localize();
	m_contactListCtrl->Localize();
	searchList->Localize();
	m_kadLookupGraph->Localize();

	m_pbtnWnd->SetBtnText(MP_VIEW_KADCONTACTS, GetResString(_T("KADCONTACTLAB")));
	m_pbtnWnd->SetBtnText(MP_VIEW_KADLOOKUP, GetResString(_T("LOOKUPGRAPH")));
}

void CKademliaWnd::UpdateControlsState()
{
	LPCTSTR uid;
	if (Kademlia::CKademlia::IsConnected())
		uid = _T("MAIN_BTN_DISCONNECT");
	else if (Kademlia::CKademlia::IsRunning())
		uid = _T("MAIN_BTN_CANCEL");
	else
		uid = _T("MAIN_BTN_CONNECT");
	SetDlgItemText(IDC_KADCONNECT, GetResNoAmp(uid));
	GetDlgItem(IDC_KADCONNECT)->EnableWindow(theApp.IsRunning());
	GetDlgItem(IDC_FIREWALLCHECKBUTTON)->EnableWindow(Kademlia::CKademlia::IsConnected());

	CString strBootstrapIP;
	GetDlgItemText(IDC_BOOTSTRAPIPPORT, strBootstrapIP);
	uint16 nPort = 0;
	int iPos = strBootstrapIP.Trim().Find(_T(':'));
	if (iPos >= 0)
		nPort = (uint16)_ttoi(CPTR(strBootstrapIP, iPos + 1));

	CString strBootstrapUrl;
	GetDlgItemText(IDC_BOOTSTRAPURL, strBootstrapUrl);

	GetDlgItem(IDC_BOOTSTRAPBUTTON)->EnableWindow(
		!Kademlia::CKademlia::IsConnected()
		&& ((IsDlgButtonChecked(IDC_RADIP) && !strBootstrapIP.IsEmpty() && nPort)
			|| IsDlgButtonChecked(IDC_RADCLIENTS)
			|| (IsDlgButtonChecked(IDC_RADNODESURL) && !strBootstrapUrl.IsEmpty()))
	);
}

UINT CKademliaWnd::GetContactCount() const
{
	return m_contactListCtrl->GetItemCount();
}

void CKademliaWnd::UpdateKadContactCount()
{
	m_contactListCtrl->UpdateKadContactCount();
}

void CKademliaWnd::StartUpdateContacts()
{
	m_contactHistogramCtrl->SetRedraw(true);
	m_contactHistogramCtrl->Invalidate();
	m_contactListCtrl->SetRedraw(true);
}

void CKademliaWnd::StopUpdateContacts()
{
	m_contactHistogramCtrl->SetRedraw(false);
	m_contactListCtrl->SetRedraw(false);
}

bool CKademliaWnd::ContactAdd(const Kademlia::CContact *contact)
{
	if (contact->IsBootstrapContact() != m_bBootstrapListMode) {
		if (contact->IsBootstrapContact()) {
			ASSERT(0);
			return false;
		}
		// we have real contacts to add, remove all the bootstrap contacts and cancel the mode
		m_bBootstrapListMode = false;
		m_contactListCtrl->DeleteAllItems();
	}
	if (!m_bBootstrapListMode)
		m_contactHistogramCtrl->ContactAdd(contact);
	return m_contactListCtrl->ContactAdd(contact);
}

void CKademliaWnd::ContactRem(const Kademlia::CContact *contact)
{
	if (contact->IsBootstrapContact() == m_bBootstrapListMode) {
		if (!m_bBootstrapListMode)
			m_contactHistogramCtrl->ContactRem(contact);
		m_contactListCtrl->ContactRem(contact);
	}
}

void CKademliaWnd::ContactRef(const Kademlia::CContact *contact)
{
	if (contact->IsBootstrapContact() == m_bBootstrapListMode)
		m_contactListCtrl->ContactRef(contact);
}

void CKademliaWnd::UpdateNodesDatFromURL(const CString &strURL)
{
	CString strTempFilename(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	strTempFilename.AppendFormat(_T("temp-%lu-nodes.dat"), ::GetTickCount());

	// try to download nodes.dat
	Log(GetResString(_T("DOWNLOADING_NODESDAT_FROM")), (LPCTSTR)strURL);
	CHttpDownloadDlg dlgDownload;
	dlgDownload.m_strTitle = GetResString(_T("DOWNLOADING_NODESDAT"));
	dlgDownload.m_sURLToDownload = strURL;
	dlgDownload.m_sFileToDownloadInto = strTempFilename;
	if (dlgDownload.DoModal() != IDOK) {
		LogError(LOG_STATUSBAR, GetResString(_T("ERR_FAILEDDOWNLOADNODES")), (LPCTSTR)strURL);
		return;
	}

	if (!Kademlia::CKademlia::IsRunning()) {
		Kademlia::CKademlia::Start();
		theApp.emuledlg->ShowConnectionState();
	}
	Kademlia::CKademlia::GetRoutingZone()->ReadFile(strTempFilename);
	(void)_tremove(strTempFilename);
}

BOOL CKademliaWnd::OnHelpInfo(HELPINFO*)
{
	theApp.ShowHelp(eMule_FAQ_GUI_Kad);
	return TRUE;
}

HBRUSH CKademliaWnd::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = theApp.emuledlg->GetCtlColor(pDC, pWnd, nCtlColor);
	return hbr ? hbr : __super::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CKademliaWnd::UpdateSearchGraph(Kademlia::CLookupHistory *pLookupHistory)
{
	if (Kademlia::CKademlia::IsRunning()) {
		m_kadLookupGraph->UpdateSearch(pLookupHistory);
		if (m_kadLookupGraph->GetAutoShowLookups() && !m_kadLookupGraph->HasActiveLookup()) {
			bool bGraphVisible = m_pbtnWnd->IsButtonChecked(MP_VIEW_KADLOOKUP) != FALSE;
			Kademlia::CLookupHistory *pActiveLookupHistory = searchList->FetchAndSelectActiveSearch(bGraphVisible);
			if (pActiveLookupHistory != NULL)
				SetSearchGraph(pActiveLookupHistory, false);
		}
	} else if (m_kadLookupGraph->HasLookup())	// we could allow watching lookups while Kad is disconnected,
		SetSearchGraph(NULL, false);			// but it feels cleaner and more disconnected if we wipe the graph
}

void CKademliaWnd::SetSearchGraph(Kademlia::CLookupHistory *pLookupHistory, bool bMakeVisible)
{
	m_kadLookupGraph->SetSearch(pLookupHistory);
	if (bMakeVisible)
		ShowLookupGraph(true);
	else
		UpdateButtonTitle(m_pbtnWnd->IsButtonChecked(MP_VIEW_KADLOOKUP) != FALSE);
}

void CKademliaWnd::OnNMDblclkSearchlist(LPNMHDR pNMHDR, LRESULT *pResult)
{
	LPNMITEMACTIVATE pItemInfo = (LPNMITEMACTIVATE)pNMHDR;
	if (pItemInfo->iItem >= 0) {
		Kademlia::CSearch *pSearch = (Kademlia::CSearch*)searchList->GetItemData(pItemInfo->iItem);
		if (pSearch != NULL) {
			SetSearchGraph(pSearch->GetLookupHistory(), true);
			thePrefs.SetAutoShowLookups(false);
		}
	}
	*pResult = 0;
}

void CKademliaWnd::OnListModifiedSearchlist(LPNMHDR, LRESULT *pResult)
{
	POSITION pos = searchList->GetFirstSelectedItemPosition();
	if (pos != NULL) {
		Kademlia::CSearch *pSearch = (Kademlia::CSearch*)searchList->GetItemData(searchList->GetNextSelectedItem(pos));
		if (pSearch != NULL)
			SetSearchGraph(pSearch->GetLookupHistory(), false);
	}
	*pResult = 0;
}

void CKademliaWnd::ShowLookupGraph(bool bShow)
{
	int iIcon = static_cast<int>(bShow);
	m_pbtnWnd->CheckButton(bShow ? MP_VIEW_KADLOOKUP : MP_VIEW_KADCONTACTS);
	TBBUTTONINFO tbbi;
	tbbi.cbSize = (UINT)sizeof tbbi;
	tbbi.dwMask = TBIF_IMAGE;
	tbbi.iImage = iIcon;
	m_pbtnWnd->SetButtonInfo((int)::GetWindowLongPtr(*m_pbtnWnd, GWLP_ID), &tbbi);
	m_kadLookupGraph->ShowWindow(bShow ? SW_SHOW : SW_HIDE);
	m_contactListCtrl->ShowWindow(bShow ? SW_HIDE : SW_SHOW);
	UpdateButtonTitle(bShow);
}

void CKademliaWnd::UpdateButtonTitle(bool bLookupGraph)
{
	CString strText;
	if (bLookupGraph) {
		strText = GetResString(_T("LOOKUPGRAPH"));
		if (m_kadLookupGraph->HasLookup())
			strText.AppendFormat(_T(" (%s)"), (LPCTSTR)m_kadLookupGraph->GetCurrentLookupTitle());
	} else
		strText.Format(_T("%s (%u)"), (LPCTSTR)GetResString(_T("KADCONTACTLAB")), GetContactCount());
	m_pbtnWnd->SetWindowText(strText);
}

void CKademliaWnd::UpdateContactCount()
{
	if (m_pbtnWnd->IsButtonChecked(MP_VIEW_KADCONTACTS))
		UpdateButtonTitle(false);
}

BOOL CKademliaWnd::OnCommand(WPARAM wParam, LPARAM lParam)
{
	switch (wParam) {
	case IDC_KADICO1:
		ShowLookupGraph(m_contactListCtrl->IsWindowVisible());
		break;
	case MP_VIEW_KADCONTACTS:
		ShowLookupGraph(false);
		break;
	case MP_VIEW_KADLOOKUP:
		ShowLookupGraph(true);
		break;
	default:
		return CWnd::OnCommand(wParam, lParam);
	}
	return TRUE;
}

//Workaround to solve a glitch with WM_SETTINGCHANGE message
void CKademliaWnd::OnSettingChange(UINT uFlags, LPCTSTR lpszSection)
{
	CResizableDialog::OnSettingChange(uFlags, lpszSection);
	m_btnsetsize = true;
}

void CKademliaWnd::SetBootstrapListMode()
{
	// rather than normal contacts we show contacts only used to bootstrap in this mode
	// once the first "normal" contact was added, the mode is cancelled and all bootstrap contacts removed
	if (GetContactCount() == 0)
		m_bBootstrapListMode = true;
	else
		ASSERT(0);
}
void CKademliaWnd::ResetGeoLite2()
{
	for (int i = 0; i != m_contactListCtrl->GetItemCount(); i++)
		((Kademlia::CContact*)m_contactListCtrl->GetItemData(i))->ResetGeoLite2();
}

void CKademliaWnd::ResetHistory()
{
	if (m_pacONBSURLs) {
		GetDlgItem(IDC_BOOTSTRAPURL)->SendMessage(WM_KEYDOWN, VK_ESCAPE, 0x00510001);
		m_pacONBSURLs->Clear();
	}

	if (m_pacONBSIPs) {
		GetDlgItem(IDC_BOOTSTRAPIPPORT)->SendMessage(WM_KEYDOWN, VK_ESCAPE, 0x00510001);
		m_pacONBSIPs->Clear();
	}
}
