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
#include "ServerWnd.h"
#include "HttpDownloadDlg.h"
#include "HTRichEditCtrl.h"
#include "ED2KLink.h"
#include "kademlia/kademlia/kademlia.h"
#include "kademlia/kademlia/prefs.h"
#include "kademlia/utils/MiscUtils.h"
#include "emuledlg.h"
#include "WebServer.h"
#include "CustomAutoComplete.h"
#include "Server.h"
#include "ServerList.h"
#include "ServerConnect.h"
#include "MuleStatusBarCtrl.h"
#include "HelpIDs.h"
#include "NetworkInfoDlg.h"
#include "Log.h"
#include "UserMsgs.h"
#include "opcodes.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define	SVWND_SPLITTER_YOFF		6
#define	SVWND_SPLITTER_HEIGHT	4

#define	SERVERMET_STRINGS_PROFILE	_T("AC_ServerMetURLs.dat")
#define SZ_DEBUG_LOG_TITLE			_T("Verbose")

// CServerWnd dialog

IMPLEMENT_DYNAMIC(CServerWnd, CDialog)

BEGIN_MESSAGE_MAP(CServerWnd, CResizableDialog)
	ON_BN_CLICKED(IDC_ADDSERVER, OnBnClickedAddserver)
	ON_BN_CLICKED(IDC_UPDATESERVERMETFROMURL, OnBnClickedUpdateServerMetFromUrl)
	ON_BN_CLICKED(IDC_LOGRESET, OnBnClickedResetLog)
	ON_NOTIFY(TCN_SELCHANGE, IDC_TAB3, OnTcnSelchangeTab3)
	ON_NOTIFY(EN_LINK, IDC_SERVMSG, OnEnLinkServerBox)
	ON_BN_CLICKED(IDC_ED2KCONNECT, OnBnConnect)
	ON_WM_SYSCOLORCHANGE()
	ON_WM_CTLCOLOR()
	ON_BN_CLICKED(IDC_DD, OnDDClicked)
	ON_WM_HELPINFO()
	ON_EN_CHANGE(IDC_IPADDRESSPORT, OnSvrTextChange)
	ON_EN_CHANGE(IDC_SNAME, OnSvrTextChange)
	ON_EN_CHANGE(IDC_SERVERMETURL, OnSvrTextChange)
	ON_STN_DBLCLK(IDC_SERVLST_ICO, OnStnDblclickServlstIco)
	ON_NOTIFY(UM_SPN_SIZED, IDC_SPLITTER_SERVER, OnSplitterMoved)
END_MESSAGE_MAP()

CServerWnd::CServerWnd(CWnd *pParent /*=NULL*/)
	: CResizableDialog(CServerWnd::IDD, pParent)
	, icon_srvlist()
	, m_cfDef()
	, m_cfBold()
	, m_pacServerMetURL()
	, debug()
{
	servermsgbox = new CHTRichEditCtrl;
	logbox = new CHTRichEditCtrl;
	debuglog = new CHTRichEditCtrl;
	protectionlog = new CHTRichEditCtrl;
	m_cfDef.cbSize = (UINT)sizeof m_cfDef;
	m_cfBold.cbSize = (UINT)sizeof m_cfBold;
	StatusSelector.m_bClosable = false;
}

CServerWnd::~CServerWnd()
{
	if (icon_srvlist)
		VERIFY(::DestroyIcon(icon_srvlist));
	if (m_pacServerMetURL) {
		m_pacServerMetURL->Unbind();
		m_pacServerMetURL->Release();
	}
	delete protectionlog;
	delete debuglog;
	delete logbox;
	delete servermsgbox;
}

BOOL CServerWnd::OnInitDialog()
{
	if (theApp.m_fontLog.m_hObject == NULL) {
		CFont *pFont = GetDlgItem(IDC_SSTATIC)->GetFont();
		LOGFONT lf;
		pFont->GetObject(sizeof lf, &lf);
		theApp.m_fontLog.CreateFontIndirect(&lf);
	}

	ReplaceRichEditCtrl(GetDlgItem(IDC_MYINFOLIST), this, GetDlgItem(IDC_SSTATIC)->GetFont());
	CResizableDialog::OnInitDialog();

	// using ES_NOHIDESEL is actually not needed, but it helps to get around a tricky window update problem!
	// If that style is not specified there are troubles with right clicking into the control for the very first time!?
#define	LOG_PANE_RICHEDIT_STYLES WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL

	RECT rect;
	GetDlgItem(IDC_SERVMSG)->GetWindowRect(&rect);
	GetDlgItem(IDC_SERVMSG)->DestroyWindow();
	::MapWindowPoints(NULL, m_hWnd, (LPPOINT)&rect, 2);
	if (servermsgbox->Create(LOG_PANE_RICHEDIT_STYLES, rect, this, IDC_SERVMSG)) {
		servermsgbox->SetProfileSkinKey(_T("ServerInfoLog"));
		servermsgbox->ModifyStyleEx(0, WS_EX_STATICEDGE, SWP_FRAMECHANGED);
		servermsgbox->SendMessage(EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(3, 3));
		servermsgbox->SetEventMask(servermsgbox->GetEventMask() | ENM_LINK);
		servermsgbox->SetFont(&theApp.m_fontHyperText);
		servermsgbox->ApplySkin();
		servermsgbox->SetTitle(GetResString(_T("SV_SERVERINFO")));
	}

	GetDlgItem(IDC_LOGBOX)->GetWindowRect(&rect);
	GetDlgItem(IDC_LOGBOX)->DestroyWindow();
	::MapWindowPoints(NULL, m_hWnd, (LPPOINT)&rect, 2);
	if (logbox->Create(LOG_PANE_RICHEDIT_STYLES, rect, this, IDC_LOGBOX)) {
		logbox->SetProfileSkinKey(_T("Log"));
		logbox->ModifyStyleEx(0, WS_EX_STATICEDGE, SWP_FRAMECHANGED);
		logbox->SendMessage(EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(3, 3));
		if (theApp.m_fontLog.m_hObject)
			logbox->SetFont(&theApp.m_fontLog);
		logbox->ApplySkin();
		logbox->SetTitle(GetResString(_T("SV_LOG")));
		logbox->SetAutoURLDetect(FALSE);
	}

	GetDlgItem(IDC_DEBUG_LOG)->GetWindowRect(&rect);
	GetDlgItem(IDC_DEBUG_LOG)->DestroyWindow();
	::MapWindowPoints(NULL, m_hWnd, (LPPOINT)&rect, 2);
	if (debuglog->Create(LOG_PANE_RICHEDIT_STYLES, rect, this, IDC_DEBUG_LOG)) {
		debuglog->SetProfileSkinKey(_T("VerboseLog"));
		debuglog->ModifyStyleEx(0, WS_EX_STATICEDGE, SWP_FRAMECHANGED);
		debuglog->SendMessage(EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(3, 3));
		if (theApp.m_fontLog.m_hObject)
			debuglog->SetFont(&theApp.m_fontLog);
		debuglog->ApplySkin();
		debuglog->SetTitle(SZ_DEBUG_LOG_TITLE);
		debuglog->SetAutoURLDetect(FALSE);
	}

	GetDlgItem(IDC_LEECHERLOG)->GetWindowRect(&rect);
	GetDlgItem(IDC_LEECHERLOG)->DestroyWindow();
	::MapWindowPoints(NULL, m_hWnd, (LPPOINT)&rect, 2);
	if (protectionlog->Create(LOG_PANE_RICHEDIT_STYLES, rect, this, IDC_LEECHERLOG)){
		protectionlog->SetProfileSkinKey(_T("VerboseLog"));
		protectionlog->ModifyStyleEx(0, WS_EX_STATICEDGE, SWP_FRAMECHANGED);
		protectionlog->SendMessage(EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(3, 3));
		if (theApp.m_fontLog.m_hObject)
			protectionlog->SetFont(&theApp.m_fontLog);
		protectionlog->ApplySkin();
		protectionlog->SetTitle(GetResString(_T("LEERCHERLOGTITLE")));
		protectionlog->SetAutoURLDetect(FALSE);
	}

	SetAllIcons();
	Localize();
	serverlistctrl.Init();

	TCITEM ti;
	CString name(GetResString(_T("SV_SERVERINFO")));
	DupAmpersand(name);
	ti.mask = TCIF_TEXT | TCIF_IMAGE;
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
	ti.iImage = 1;
	VERIFY(StatusSelector.InsertItem(StatusSelector.GetItemCount(), &ti) == PaneServerInfo);

	name = GetResString(_T("SV_LOG"));
	DupAmpersand(name);
	ti.mask = TCIF_TEXT | TCIF_IMAGE;
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
	ti.iImage = 0;
	VERIFY(StatusSelector.InsertItem(StatusSelector.GetItemCount(), &ti) == PaneLog);

	name = SZ_DEBUG_LOG_TITLE;
	DupAmpersand(name);
	ti.mask = TCIF_TEXT | TCIF_IMAGE;
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
	ti.iImage = 0;
	VERIFY(StatusSelector.InsertItem(StatusSelector.GetItemCount(), &ti) == PaneVerboseLog);

	name=GetResString(_T("LEERCHERLOGTITLE"));
	DupAmpersand(name);
	ti.mask = TCIF_TEXT|TCIF_IMAGE;
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
	ti.iImage = 0;
	VERIFY( StatusSelector.InsertItem(StatusSelector.GetItemCount(), &ti) == PaneLeecherLog );

	AddOrReplaceAnchor(this, IDC_SERVLST_ICO, TOP_LEFT);
	AddOrReplaceAnchor(this, IDC_SERVLIST_TEXT, TOP_LEFT);
	AddOrReplaceAnchor(this, serverlistctrl, TOP_LEFT, MIDDLE_RIGHT);
	AddOrReplaceAnchor(this, m_ctrlMyInfoFrm, TOP_RIGHT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, m_MyInfo, TOP_RIGHT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_UPDATESERVERMETFROMURL, TOP_RIGHT);
	AddOrReplaceAnchor(this, StatusSelector, MIDDLE_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_LOGRESET, MIDDLE_RIGHT); // avoid resizing GUI glitches with the tab control by adding this control as the last one (Z-order)
	// The resizing of those log controls (rich edit controls) works 'better' when added as last anchors (?)
	AddOrReplaceAnchor(this, *servermsgbox, MIDDLE_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, *logbox, MIDDLE_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, *debuglog, MIDDLE_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, *protectionlog, MIDDLE_LEFT, BOTTOM_RIGHT);

	// Resize the URL edit control to fit the down-arrow button before anchors are snapshotted
	if (thePrefs.GetUseAutocompletion()) {
		CWnd* pURL = GetDlgItem(IDC_SERVERMETURL);
		if (pURL) {
			CRect rcURL;
			pURL->GetWindowRect(&rcURL);
			ScreenToClient(&rcURL);
			rcURL.right = rcURL.right - 16;
			pURL->MoveWindow(rcURL, TRUE);
		}
	}

	AddAllOtherAnchors(TOP_RIGHT);

	// Set the tab control to the bottom of the z-order. This solves a lot of strange repainting problems with
	// the rich edit controls (the log panes).
	::SetWindowPos(StatusSelector, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOSIZE);

	debug = true;
	ToggleDebugWindow();

	protectionlog->ShowWindow(SW_HIDE);
	debuglog->ShowWindow(SW_HIDE);
	logbox->ShowWindow(SW_HIDE);
	servermsgbox->ShowWindow(SW_SHOW);

	// optional: restore last used log pane
	if (thePrefs.GetRestoreLastLogPane()) {
		if (thePrefs.GetLastLogPaneID() >= 0 && thePrefs.GetLastLogPaneID() < StatusSelector.GetItemCount()) {
			int iCurSel = StatusSelector.GetCurSel();
			StatusSelector.SetCurSel(thePrefs.GetLastLogPaneID());
			if (thePrefs.GetLastLogPaneID() == StatusSelector.GetCurSel())
				UpdateLogTabSelection();
			else
				StatusSelector.SetCurSel(iCurSel);
		}
	}

	m_MyInfo.SendMessage(EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(3, 3));
	m_MyInfo.SetAutoURLDetect();
	m_MyInfo.SetEventMask(m_MyInfo.GetEventMask() | ENM_LINK);

	PARAFORMAT pf = {};
	pf.cbSize = (UINT)sizeof pf;
	if (m_MyInfo.GetParaFormat(pf)) {
		pf.dwMask |= PFM_TABSTOPS;
		pf.cTabCount = 4;
		pf.rgxTabs[0] = 900;
		pf.rgxTabs[1] = 1000;
		pf.rgxTabs[2] = 1100;
		pf.rgxTabs[3] = 1200;
		m_MyInfo.SetParaFormat(pf);
	}

	m_cfDef.cbSize = (UINT)sizeof m_cfDef;
	if (m_MyInfo.GetSelectionCharFormat(m_cfDef)) {
		m_cfBold = m_cfDef;
		m_cfBold.dwMask |= CFM_BOLD;
		m_cfBold.dwEffects |= CFE_BOLD;
	}

	if (thePrefs.GetUseAutocompletion()) {
		if (m_pacServerMetURL) {
			m_pacServerMetURL->Unbind();
			m_pacServerMetURL->Release();
		}
		m_pacServerMetURL = new CCustomAutoComplete();
		if (m_pacServerMetURL) {
			m_pacServerMetURL->AddRef();
			if (m_pacServerMetURL->Bind(::GetDlgItem(m_hWnd, IDC_SERVERMETURL), ACO_UPDOWNKEYDROPSLIST | ACO_AUTOSUGGEST | ACO_FILTERPREFIXES))
				m_pacServerMetURL->LoadList(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + SERVERMET_STRINGS_PROFILE);
			// Prefill last used server.met update URL from the autocomplete list
				int n = m_pacServerMetURL->GetItemCount();
				if (n > 0) {
					CString last = m_pacServerMetURL->GetItem(0);
					if (!last.IsEmpty()) {
						SetDlgItemText(IDC_SERVERMETURL, last);
						OnSvrTextChange();
					}
				}
		}

		if (theApp.m_fontSymbol.m_hObject) {
			GetDlgItem(IDC_DD)->SetFont(&theApp.m_fontSymbol);
			SetDlgItemText(IDC_DD, _T("6")); // show a down-arrow
		}
	} else
		GetDlgItem(IDC_DD)->ShowWindow(SW_HIDE);

	InitWindowStyles(this);

	// splitter
	CRect rcSpl(55, 55, 300, 55 + SVWND_SPLITTER_HEIGHT);
	m_wndSplitter.CreateWnd(WS_CHILD | WS_VISIBLE, rcSpl, this, IDC_SPLITTER_SERVER);
	m_wndSplitter.SetDrawBorder(true);
	InitSplitter();
	GetDlgItem(IDC_ED2KCONNECT)->EnableWindow(false);

	return TRUE;
}

void CServerWnd::DoDataExchange(CDataExchange *pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SERVLIST, serverlistctrl);
	DDX_Control(pDX, IDC_SSTATIC, m_ctrlNewServerFrm);
	DDX_Control(pDX, IDC_SSTATIC6, m_ctrlUpdateServerFrm);
	DDX_Control(pDX, IDC_MYINFO, m_ctrlMyInfoFrm);
	DDX_Control(pDX, IDC_TAB3, StatusSelector);
	DDX_Control(pDX, IDC_MYINFOLIST, m_MyInfo);
}

bool CServerWnd::UpdateServerMetFromURL(const CString &strURL)
{
	if (strURL.IsEmpty() || strURL.Find(_T("://")) < 0) {
		// not a valid URL
		LogError(LOG_STATUSBAR, GetResString(_T("INVALIDURL")));
		return false;
	}

	// add entered URL to LRU list even if it's not yet known whether we can download from this URL (it's just more convenient this way)
	if (m_pacServerMetURL && m_pacServerMetURL->IsBound())
		m_pacServerMetURL->AddItem(strURL, 0);

	CString strTempFilename(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	strTempFilename.AppendFormat(_T("temp-%u-server.met"), ::GetTickCount());

	// try to download server.met
	Log(GetResString(_T("DOWNLOADING_SERVERMET_FROM")), (LPCTSTR)EscPercent(strURL));
	CHttpDownloadDlg dlgDownload;
	dlgDownload.m_strTitle = GetResString(_T("DOWNLOADING_SERVERMET"));
	dlgDownload.m_sURLToDownload = strURL;
	dlgDownload.m_sFileToDownloadInto = strTempFilename;
	if (dlgDownload.DoModal() != IDOK) {
		LogError(LOG_STATUSBAR, GetResString(_T("ERR_FAILEDDOWNLOADMET")), (LPCTSTR)strURL);
		return false;
	}

	// add content of server.met to serverlist
	serverlistctrl.Hide();
	serverlistctrl.AddServerMetToList(strTempFilename);
	serverlistctrl.Visible();
	(void)_tremove(strTempFilename);
	return true;
}

void CServerWnd::OnSysColorChange()
{
	CResizableDialog::OnSysColorChange();
	SetAllIcons();
}

void CServerWnd::SetAllIcons()
{
	m_ctrlNewServerFrm.SetIcon(_T("AddServer"));
	m_ctrlUpdateServerFrm.SetIcon(_T("ServerUpdateMET"));
	m_ctrlMyInfoFrm.SetIcon(_T("Info"));

	CImageList iml;
	iml.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	iml.Add(CTempIconLoader(_T("Log")));
	iml.Add(CTempIconLoader(_T("ServerInfo")));
	StatusSelector.SetImageList(&iml);
	m_imlLogPanes.DeleteImageList();
	m_imlLogPanes.Attach(iml.Detach());

	if (icon_srvlist)
		VERIFY(::DestroyIcon(icon_srvlist));
	icon_srvlist = theApp.LoadIcon(_T("ServerList"), 16, 16);
	static_cast<CStatic*>(GetDlgItem(IDC_SERVLST_ICO))->SetIcon(icon_srvlist);
}

void CServerWnd::Localize()
{
	serverlistctrl.Localize();

	serverlistctrl.ShowServerCount();
	m_ctrlNewServerFrm.SetWindowText(GetResString(_T("SV_NEWSERVER")));
	SetDlgItemText(IDC_SSTATIC4, GetResString(_T("SV_ADDRESS_PORT")));
	SetDlgItemText(IDC_SSTATIC3, GetResString(_T("SW_NAME")));
	SetDlgItemText(IDC_ADDSERVER, GetResString(_T("SV_ADD")));
	m_ctrlUpdateServerFrm.SetWindowText(GetResString(_T("SV_MET")));
	SetDlgItemText(IDC_UPDATESERVERMETFROMURL, GetResString(_T("SV_UPDATE")));
	SetDlgItemText(IDC_LOGRESET, GetResString(_T("PW_RESET")));
	m_ctrlMyInfoFrm.SetWindowText(GetResString(_T("MYINFO")));

	TCITEM ti;
	CString name(GetResString(_T("SV_SERVERINFO")));
	DupAmpersand(name);
	ti.mask = TCIF_TEXT;
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
	StatusSelector.SetItem(PaneServerInfo, &ti);

	name = GetResString(_T("SV_LOG"));
	DupAmpersand(name);
	ti.mask = TCIF_TEXT;
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
	StatusSelector.SetItem(PaneLog, &ti);

	name = SZ_DEBUG_LOG_TITLE;
	DupAmpersand(name);
	ti.mask = TCIF_TEXT;
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
	StatusSelector.SetItem(PaneVerboseLog, &ti);

	name = GetResString(_T("LEERCHERLOGTITLE"));
	DupAmpersand(name);
	ti.mask = TCIF_TEXT;
	ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
	StatusSelector.SetItem(PaneLeecherLog, &ti);

	UpdateLogTabSelection();
	UpdateControlsState();
}

void CServerWnd::OnBnClickedAddserver()
{
	CString serveraddr;
	GetDlgItemText(IDC_IPADDRESSPORT, serveraddr);
	if (serveraddr.Trim().IsEmpty()) {
		LocMessageBox(_T("SRV_ADDR"), MB_OK, 0);
		return;
	}

	uint16 uPort = 0;
	int iPos = serveraddr.Trim().Find(_T(':'));
	if (iPos >= 0) {
		uPort = (uint16)_ttoi(CPTR(serveraddr, iPos + 1));
		serveraddr.Truncate(iPos);
	}

	if (_tcsnicmp(serveraddr, _T("ed2k://"), 7) == 0) {
		CED2KLink *pLink = NULL;
		try {
			pLink = CED2KLink::CreateLinkFromUrl(serveraddr);
			serveraddr.Empty();
			if (pLink && pLink->GetKind() == CED2KLink::kServer) {
				CED2KServerLink *pServerLink = pLink->GetServerLink();
				if (pServerLink) {
					serveraddr = pServerLink->GetAddress();
					uPort = pServerLink->GetPort();
					serveraddr.AppendFormat(_T(":%u"), uPort);
					SetDlgItemText(IDC_IPADDRESSPORT, serveraddr);
				}
			}
		} catch (const CString &strError) {
			CDarkMode::MessageBox(strError);
			serveraddr.Empty();
		}
		delete pLink;
	} else {
		if (!uPort) {
			LocMessageBox(_T("SRV_PORT"), MB_OK, 0);
			return;
		}
	}

	if (serveraddr.IsEmpty() || uPort == 0) {
		LocMessageBox(_T("SRV_ADDR"), MB_OK, 0);
		return;
	}

	CString strServerName;
	GetDlgItemText(IDC_SNAME, strServerName);

	AddServer(uPort, serveraddr, strServerName);
}

void CServerWnd::PasteServerFromClipboard()
{
	CString strServer(theApp.CopyTextFromClipboard());
	if (strServer.Trim().IsEmpty())
		return;

	bool bAdd = true;
	for (int nPos = 0; bAdd && nPos >= 0;) {
		const CString &sToken(strServer.Tokenize(_T(" \t\r\n"), nPos));
		if (sToken.IsEmpty())
			break;
		CED2KLink *pLink = NULL;
		try {
			pLink = CED2KLink::CreateLinkFromUrl(sToken);
			if (pLink && pLink->GetKind() == CED2KLink::kServer) {
				const CED2KServerLink *pServerLink = pLink->GetServerLink();
				if (pServerLink) {
					const CString &strAddress(pServerLink->GetAddress());
					uint16 nPort = pServerLink->GetPort();
					if (!strAddress.IsEmpty() && nPort)
						(void)AddServer(nPort, strAddress, CString(), false);
					else
						bAdd = false;
				}
			}
		} catch (const CString &strError) {
			CDarkMode::MessageBox(strError);
		}
		delete pLink;
	}
}

bool CServerWnd::AddServer(uint16 nPort, const CString &strAddress, const CString &strName, bool bShowErrorMB)
{
	CServer *toadd = new CServer(nPort, strAddress);

	// Barry - Default all manually added servers to high priority
	if (thePrefs.GetManualAddedServersHighPriority())
		toadd->SetPreference(SRV_PR_HIGH);

	toadd->SetListName(strName.IsEmpty() ? strAddress : strName);

	if (!serverlistctrl.AddServer(toadd, true)) {
		CServer *pFoundServer = theApp.serverlist->GetServerByAddress(toadd->GetAddress(), toadd->GetPort());
		if (pFoundServer == NULL && toadd->GetIP() != 0)
			pFoundServer = theApp.serverlist->GetServerByIPTCP(toadd->GetIP(), toadd->GetPort());
		if (pFoundServer) {
			static TCHAR const _aszServerPrefix[] = _T("Server");
			if (_tcsnicmp(toadd->GetListName(), _aszServerPrefix, _countof(_aszServerPrefix) - 1) != 0) {
				pFoundServer->SetListName(toadd->GetListName());
				serverlistctrl.RefreshServer(pFoundServer);
			}
		} else if (bShowErrorMB)
			LocMessageBox(_T("SRV_NOTADDED"), MB_OK, 0);

		delete toadd;
		return false;
	}

	AddLogLine(true, GetResString(_T("SERVERADDED")), (LPCTSTR)EscPercent(toadd->GetListName()));
	return true;
}

void CServerWnd::OnBnClickedUpdateServerMetFromUrl()
{
	CString strURL;
	GetDlgItemText(IDC_SERVERMETURL, strURL);

	if (strURL.Trim().IsEmpty()) {
		if (thePrefs.addresses_list.IsEmpty())
			AddLogLine(true, GetResString(_T("SRV_NOURLAV")));
		else
			for (POSITION pos = thePrefs.addresses_list.GetHeadPosition(); pos != NULL;)
				if (UpdateServerMetFromURL(thePrefs.addresses_list.GetNext(pos)))
					break;
	} else
		UpdateServerMetFromURL(strURL);
}

void CServerWnd::OnBnClickedResetLog()
{
	int cur_sel = StatusSelector.GetCurSel();
	if (cur_sel == -1)
		return;
	if (cur_sel == PaneLeecherLog)
	{
		theApp.emuledlg->ResetLeecherLog();
		theApp.emuledlg->statusbar->SetText(EMPTY, SBarLog, 0);
	}
	if (cur_sel == PaneVerboseLog) {
		theApp.emuledlg->ResetDebugLog();
		theApp.emuledlg->statusbar->SetText(EMPTY, SBarLog, 0);
	}
	if (cur_sel == PaneLog) {
		theApp.emuledlg->ResetLog();
		theApp.emuledlg->statusbar->SetText(EMPTY, SBarLog, 0);
	}
	if (cur_sel == PaneServerInfo) {
		servermsgbox->Reset();
		// the statusbar does not contain any server log related messages, so it's not cleared.
	}
}

void CServerWnd::OnTcnSelchangeTab3(LPNMHDR, LRESULT *pResult)
{
	UpdateLogTabSelection();
	*pResult = 0;
}

void CServerWnd::UpdateLogTabSelection()
{
	int cur_sel = StatusSelector.GetCurSel();
	if (cur_sel == -1)
		return;
	if (cur_sel == PaneLeecherLog)
	{
		servermsgbox->ShowWindow(SW_HIDE);
		logbox->ShowWindow(SW_HIDE);
		debuglog->ShowWindow(SW_HIDE);
		protectionlog->ShowWindow(SW_SHOW);
		if (protectionlog->IsAutoScroll() && (StatusSelector.GetItemState(cur_sel, TCIS_HIGHLIGHTED) & TCIS_HIGHLIGHTED))
			protectionlog->ScrollToLastLine(true);
		protectionlog->Invalidate();
		StatusSelector.HighlightItem(cur_sel, FALSE);
	}
	if (cur_sel == PaneVerboseLog) {
		servermsgbox->ShowWindow(SW_HIDE);
		logbox->ShowWindow(SW_HIDE);
		protectionlog->ShowWindow(SW_HIDE);
		debuglog->ShowWindow(SW_SHOW);
		if (debuglog->IsAutoScroll() && (StatusSelector.GetItemState(cur_sel, TCIS_HIGHLIGHTED) & TCIS_HIGHLIGHTED))
			debuglog->ScrollToLastLine(true);
		debuglog->Invalidate();
		StatusSelector.HighlightItem(cur_sel, FALSE);
	}
	if (cur_sel == PaneLog) {
		debuglog->ShowWindow(SW_HIDE);
		servermsgbox->ShowWindow(SW_HIDE);
		protectionlog->ShowWindow(SW_HIDE);
		logbox->ShowWindow(SW_SHOW);
		if (logbox->IsAutoScroll() && (StatusSelector.GetItemState(cur_sel, TCIS_HIGHLIGHTED) & TCIS_HIGHLIGHTED))
			logbox->ScrollToLastLine(true);
		logbox->Invalidate();
		StatusSelector.HighlightItem(cur_sel, FALSE);
	}
	if (cur_sel == PaneServerInfo) {
		debuglog->ShowWindow(SW_HIDE);
		logbox->ShowWindow(SW_HIDE);
		protectionlog->ShowWindow(SW_HIDE);
		servermsgbox->ShowWindow(SW_SHOW);
		if (servermsgbox->IsAutoScroll() && (StatusSelector.GetItemState(cur_sel, TCIS_HIGHLIGHTED) & TCIS_HIGHLIGHTED))
			servermsgbox->ScrollToLastLine(true);
		servermsgbox->Invalidate();
		StatusSelector.HighlightItem(cur_sel, FALSE);
	}
}

void CServerWnd::ToggleDebugWindow()
{
	if (thePrefs.GetVerbose() && !debug) {
		TCITEM ti;
		CString name(SZ_DEBUG_LOG_TITLE);
		DupAmpersand(name);
		ti.mask = TCIF_TEXT | TCIF_IMAGE;
		ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
		ti.iImage = 0;
		StatusSelector.InsertItem(StatusSelector.GetItemCount(), &ti);

		name = GetResString(_T("LEERCHERLOGTITLE"));
		DupAmpersand(name);
		ti.mask = TCIF_TEXT|TCIF_IMAGE;
		ti.pszText = const_cast<LPTSTR>((LPCTSTR)name);
		ti.iImage = 0;
		StatusSelector.InsertItem(StatusSelector.GetItemCount(),&ti);

		debug = true;
	} else if (!thePrefs.GetVerbose() && debug) {
		if (StatusSelector.GetCurSel() == PaneVerboseLog || StatusSelector.GetCurSel() == PaneLeecherLog) {
			StatusSelector.SetCurSel(PaneLog);
			StatusSelector.SetFocus();
		}
		debuglog->ShowWindow(SW_HIDE);
		servermsgbox->ShowWindow(SW_HIDE);
		protectionlog->ShowWindow(SW_HIDE);
		logbox->ShowWindow(SW_SHOW);
		StatusSelector.DeleteItem(PaneLeecherLog);
		StatusSelector.DeleteItem(PaneVerboseLog);
		debug = false;
	}
}

void CServerWnd::UpdateMyInfo()
{
	m_MyInfo.SetRedraw(FALSE);
	m_MyInfo.SetWindowText(EMPTY);
	CreateNetworkInfo(m_MyInfo, m_cfDef, m_cfBold);
	m_MyInfo.SetRedraw(TRUE);
	m_MyInfo.Invalidate();
}

CString CServerWnd::GetMyInfoString()
{
	CString buffer;
	m_MyInfo.GetWindowText(buffer);
	return buffer;
}

BOOL CServerWnd::PreTranslateMessage(MSG *pMsg)
{
	if (theApp.emuledlg->m_pSplashWnd) //splash or about dialogs are active
		return FALSE;
	if (pMsg->message == WM_KEYDOWN) {
		// Don't handle Ctrl+Tab in this window. It will be handled by the main window.
		if (pMsg->wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0)
			return FALSE;
		switch (pMsg->wParam) {
		case VK_ESCAPE:
			return FALSE;
		case VK_DELETE:
			if (m_pacServerMetURL && m_pacServerMetURL->IsBound() && pMsg->hwnd == GetDlgItem(IDC_SERVERMETURL)->m_hWnd)
				if (GetKeyState(VK_MENU) < 0 || GetKeyState(VK_CONTROL) < 0)
					m_pacServerMetURL->Clear();
				else
					m_pacServerMetURL->RemoveSelectedItem();
			break;
		case VK_RETURN:
			if (pMsg->hwnd == GetDlgItem(IDC_IPADDRESSPORT)->m_hWnd
				|| pMsg->hwnd == GetDlgItem(IDC_SNAME)->m_hWnd)
			{
				OnBnClickedAddserver();
				return TRUE;
			}
			if (pMsg->hwnd == GetDlgItem(IDC_SERVERMETURL)->m_hWnd) {
				if (m_pacServerMetURL && m_pacServerMetURL->IsBound()) {
					CString strText;
					GetDlgItemText(IDC_SERVERMETURL, strText);
					if (!strText.IsEmpty()) {
						SetDlgItemText(IDC_SERVERMETURL, EMPTY); // this seems to be the only chance to let the drop-down list to disappear
						SetDlgItemText(IDC_SERVERMETURL, strText);
						static_cast<CEdit*>(GetDlgItem(IDC_SERVERMETURL))->SetSel(strText.GetLength(), strText.GetLength());
					}
				}
				OnBnClickedUpdateServerMetFromUrl();
				return TRUE;
			}
		}
	}

	return CResizableDialog::PreTranslateMessage(pMsg);
}

bool CServerWnd::SaveServerMetStrings()
{
	if (m_pacServerMetURL == NULL)
		return false;
	return m_pacServerMetURL->SaveList(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + SERVERMET_STRINGS_PROFILE);
}

void CServerWnd::ShowNetworkInfo()
{
	CNetworkInfoDlg dlg;
	dlg.DoModal();
}

void CServerWnd::OnEnLinkServerBox(LPNMHDR pNMHDR, LRESULT *pResult)
{
	ENLINK *pEnLink = reinterpret_cast<ENLINK*>(pNMHDR);
	if (pEnLink && pEnLink->msg == WM_LBUTTONDOWN) {
		CString strUrl;
		servermsgbox->GetTextRange(pEnLink->chrg.cpMin, pEnLink->chrg.cpMax, strUrl);
		BrowserOpen(strUrl, NULL);
		*pResult = 1;
	} else
		*pResult = 0;
}

void CServerWnd::UpdateControlsState()
{
	LPCTSTR uid;
	if (theApp.serverconnect->IsConnected())
		uid = _T("MAIN_BTN_DISCONNECT");
	else if (theApp.serverconnect->IsConnecting())
		uid = _T("MAIN_BTN_CANCEL");
	else
		uid = _T("MAIN_BTN_CONNECT");
	SetDlgItemText(IDC_ED2KCONNECT, GetResNoAmp(uid));
}

void CServerWnd::OnBnConnect()
{
	if (theApp.serverconnect->IsConnected())
		theApp.serverconnect->Disconnect();
	else if (theApp.serverconnect->IsConnecting())
		theApp.serverconnect->StopConnectionTry();
	else {
		if (theApp.serverlist != NULL && theApp.serverlist->GetServerCount() == 0) {
			CDarkMode::MessageBox(GetResString(_T("EMULE_AI_SERVERMET_REQUIRED_CONNECT")), MB_OK | MB_ICONINFORMATION);
			return;
		}
		theApp.serverconnect->ConnectToAnyServer();
	}
}

void CServerWnd::SaveAllSettings()
{
	thePrefs.SetLastLogPaneID(StatusSelector.GetCurSel());
	SaveServerMetStrings();
}

void CServerWnd::OnDDClicked()
{
	CWnd *box = GetDlgItem(IDC_SERVERMETURL);
	box->SetFocus();
	box->SetWindowText(EMPTY);
	box->SendMessage(WM_KEYDOWN, VK_DOWN, 0x00510001);
}

void CServerWnd::ResetHistory()
{
	if (m_pacServerMetURL != NULL) {
		GetDlgItem(IDC_SERVERMETURL)->SendMessage(WM_KEYDOWN, VK_ESCAPE, 0x00510001);
		m_pacServerMetURL->Clear();
	}
}

BOOL CServerWnd::OnHelpInfo(HELPINFO*)
{
	theApp.ShowHelp(eMule_FAQ_GUI_Server);
	return TRUE;
}

void CServerWnd::OnSvrTextChange()
{
	GetDlgItem(IDC_ADDSERVER)->EnableWindow(GetDlgItem(IDC_IPADDRESSPORT)->GetWindowTextLength());
	GetDlgItem(IDC_UPDATESERVERMETFROMURL)->EnableWindow(GetDlgItem(IDC_SERVERMETURL)->GetWindowTextLength() > 0);
}

void CServerWnd::OnStnDblclickServlstIco()
{
	theApp.emuledlg->ShowPreferences(IDD_PPG_SERVER);
}

void CServerWnd::DoResize(int delta)
{
	CSplitterControl::ChangeHeight(&serverlistctrl, delta, CW_TOPALIGN);
	CSplitterControl::ChangeHeight(&StatusSelector, -delta, CW_BOTTOMALIGN);
	CSplitterControl::ChangeHeight(servermsgbox, -delta, CW_BOTTOMALIGN);
	CSplitterControl::ChangeHeight(logbox, -delta, CW_BOTTOMALIGN);
	CSplitterControl::ChangeHeight(debuglog, -delta, CW_BOTTOMALIGN);
	CSplitterControl::ChangeHeight(protectionlog, -delta, CW_BOTTOMALIGN);
	UpdateSplitterRange();
}

void CServerWnd::InitSplitter()
{
	CRect rcWnd;
	GetWindowRect(rcWnd);
	ScreenToClient(rcWnd);

	m_wndSplitter.SetRange(rcWnd.top + 100, rcWnd.bottom - 50);
	LONG splitpos = 5 + (thePrefs.GetSplitterbarPositionServer() * rcWnd.Height()) / 100;

	RECT rcDlgItem;
	serverlistctrl.GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);
	rcDlgItem.bottom = splitpos - 10;
	serverlistctrl.MoveWindow(&rcDlgItem);

	GetDlgItem(IDC_LOGRESET)->GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);
	rcDlgItem.top = splitpos + 9;
	rcDlgItem.bottom = splitpos + 30;
	GetDlgItem(IDC_LOGRESET)->MoveWindow(&rcDlgItem);

	StatusSelector.GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);
	rcDlgItem.top = splitpos + 10;
	rcDlgItem.bottom = rcWnd.bottom - 5;
	StatusSelector.MoveWindow(&rcDlgItem);

	servermsgbox->GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);
	rcDlgItem.top = splitpos + 35;
	rcDlgItem.bottom = rcWnd.bottom - 12;
	servermsgbox->MoveWindow(&rcDlgItem);

	logbox->GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);
	rcDlgItem.top = splitpos + 35;
	rcDlgItem.bottom = rcWnd.bottom - 12;
	logbox->MoveWindow(&rcDlgItem);

	debuglog->GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);
	rcDlgItem.top = splitpos + 35;
	rcDlgItem.bottom = rcWnd.bottom - 12;
	debuglog->MoveWindow(&rcDlgItem);

	protectionlog->GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);
	rcDlgItem.top=splitpos+35;
	rcDlgItem.bottom = rcWnd.bottom-12;
	protectionlog->MoveWindow(&rcDlgItem);

	long right = rcDlgItem.right;
	GetDlgItem(IDC_SPLITTER_SERVER)->GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);
	rcDlgItem.right = right;
	GetDlgItem(IDC_SPLITTER_SERVER)->MoveWindow(&rcDlgItem);

	ReattachAnchors();
}

void CServerWnd::ReattachAnchors()
{
	AddOrReplaceAnchor(this, serverlistctrl, TOP_LEFT, ANCHOR(100, thePrefs.GetSplitterbarPositionServer()));
	AddOrReplaceAnchor(this, StatusSelector, ANCHOR(0, thePrefs.GetSplitterbarPositionServer()), BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_LOGRESET, MIDDLE_RIGHT);
	AddOrReplaceAnchor(this, *servermsgbox, ANCHOR(0, thePrefs.GetSplitterbarPositionServer()), BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, *logbox, ANCHOR(0, thePrefs.GetSplitterbarPositionServer()), BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, *debuglog, ANCHOR(0, thePrefs.GetSplitterbarPositionServer()), BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, *protectionlog, ANCHOR(0, thePrefs.GetSplitterbarPositionServer()), BOTTOM_RIGHT);

	GetDlgItem(IDC_LOGRESET)->Invalidate();

	if (servermsgbox->IsWindowVisible())
		servermsgbox->Invalidate();
	if (logbox->IsWindowVisible())
		logbox->Invalidate();
	if (debuglog->IsWindowVisible())
		debuglog->Invalidate();
	if (protectionlog->IsWindowVisible())
		protectionlog->Invalidate();
}

void CServerWnd::UpdateSplitterRange()
{
	CRect rcWnd;
	GetWindowRect(rcWnd);
	ScreenToClient(rcWnd);

	RECT rcDlgItem;
	serverlistctrl.GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);

	m_wndSplitter.SetRange(rcWnd.top + 100, rcWnd.bottom - 50);

	LONG splitpos = rcDlgItem.bottom + SVWND_SPLITTER_YOFF;
	thePrefs.SetSplitterbarPositionServer((splitpos * 100) / rcWnd.Height());

	GetDlgItem(IDC_LOGRESET)->GetWindowRect(&rcDlgItem);
	ScreenToClient(&rcDlgItem);
	rcDlgItem.top = splitpos + 9;
	rcDlgItem.bottom = splitpos + 30;
	GetDlgItem(IDC_LOGRESET)->MoveWindow(&rcDlgItem);

	ReattachAnchors();
}

LRESULT CServerWnd::DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	// arrange transfer window layout
	switch (message) {
	case WM_PAINT:
		if (m_wndSplitter) {
			CRect rcWnd;
			GetWindowRect(rcWnd);
			if (rcWnd.Height() > 0) {
				RECT rcDown;
				serverlistctrl.GetWindowRect(&rcDown);
				ScreenToClient(&rcDown);

				// splitter paint update
				RECT rcSpl;
				rcSpl.left = 10;
				rcSpl.top = rcDown.bottom + SVWND_SPLITTER_YOFF;
				rcSpl.right = rcDown.right;
				rcSpl.bottom = rcSpl.top + SVWND_SPLITTER_HEIGHT;
				m_wndSplitter.MoveWindow(&rcSpl, TRUE);
				UpdateSplitterRange();
			}
		}
		break;
	case WM_WINDOWPOSCHANGED:
		if (m_wndSplitter)
			m_wndSplitter.Invalidate();
	}

	return CResizableDialog::DefWindowProc(message, wParam, lParam);
}

void CServerWnd::OnSplitterMoved(LPNMHDR pNMHDR, LRESULT*)
{
	SPC_NMHDR *pHdr = reinterpret_cast<SPC_NMHDR*>(pNMHDR);
	DoResize(pHdr->delta);
}

HBRUSH CServerWnd::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = theApp.emuledlg->GetCtlColor(pDC, pWnd, nCtlColor);
	return hbr ? hbr : __super::OnCtlColor(pDC, pWnd, nCtlColor);
}
