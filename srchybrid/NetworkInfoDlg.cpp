#include "stdafx.h"
#include "emule.h"
#include "NetworkInfoDlg.h"
#include "RichEditCtrlX.h"
#include "OtherFunctions.h"
#include "ServerConnect.h"
#include "Preferences.h"
#include "Log.h"
#include "ServerList.h"
#include "Server.h"
#include "kademlia/kademlia/kademlia.h"
#include "kademlia/kademlia/UDPFirewallTester.h"
#include "kademlia/kademlia/prefs.h"
#include "kademlia/kademlia/indexed.h"
#include "WebServer.h"
#include "clientlist.h"
#include "UpDownClient.h"
#include "KnownFile.h"
#include "kademlia\kademlia\Search.h"
#include "Kademlia/Kademlia/SearchManager.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define	PREF_INI_SECTION	_T("NetworkInfoDlg")

IMPLEMENT_DYNAMIC(CNetworkInfoDlg, CDialog)

CNetworkInfoDlg* CNetworkInfoDlg::s_pActiveInstance = NULL;

BEGIN_MESSAGE_MAP(CNetworkInfoDlg, CResizableDialog)
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_NATT_FORCE_PUBLISH, OnNattForcePublish)
END_MESSAGE_MAP()

CNetworkInfoDlg::CNetworkInfoDlg(CWnd *pParent /*=NULL*/)
	: CResizableDialog(CNetworkInfoDlg::IDD, pParent)
{
	ZeroMemory(&m_cfDef, sizeof m_cfDef);
	ZeroMemory(&m_cfBold, sizeof m_cfBold);
}

void CNetworkInfoDlg::DoDataExchange(CDataExchange *pDX)
{
	CResizableDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_NETWORK_INFO, m_info);
}

BOOL CNetworkInfoDlg::OnInitDialog()
{
	ReplaceRichEditCtrl(GetDlgItem(IDC_NETWORK_INFO), this, GetDlgItem(IDC_NETWORK_INFO_LABEL)->GetFont());
	CResizableDialog::OnInitDialog();
	InitWindowStyles(this);

	AddOrReplaceAnchor(this, IDC_NETWORK_INFO, TOP_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDOK, BOTTOM_RIGHT);
	EnableSaveRestore(PREF_INI_SECTION);

	SetWindowText(GetResString(_T("NETWORK_INFO")));
	SetDlgItemText(IDOK, GetResString(_T("TREEOPTIONS_OK")));
	SetDlgItemText(IDC_NETWORK_INFO_LABEL, GetResString(_T("NETWORK_INFO")));

	m_info.SendMessage(EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELONG(3, 3));
	m_info.SetAutoURLDetect();
	m_info.SetEventMask(m_info.GetEventMask() | ENM_LINK);

	AddOrReplaceAnchor(this, IDC_NETWORK_INFO_AUTOREFRESH, BOTTOM_RIGHT);
	SetDlgItemText(IDC_NETWORK_INFO_AUTOREFRESH, GetResString(_T("NETWORK_INFO_AUTOREFRESH")));
	CheckDlgButton(IDC_NETWORK_INFO_AUTOREFRESH, BST_CHECKED); // Default: auto refresh enabled whenever dialog opens

#ifdef NATTTESTMODE
	AddOrReplaceAnchor(this, IDC_NATT_FORCE_PUBLISH, BOTTOM_RIGHT);
	SetDlgItemText(IDC_NATT_FORCE_PUBLISH, GetResString(_T("NATT_FORCE_PUBLISH")));
	CheckDlgButton(IDC_NATT_FORCE_PUBLISH, BST_CHECKED);
#else
	GetDlgItem(IDC_NATT_FORCE_PUBLISH)->ShowWindow(SW_HIDE);
#endif

	ZeroMemory(&m_cfDef, sizeof m_cfDef);
	ZeroMemory(&m_cfBold, sizeof m_cfBold);

	PARAFORMAT pf = {};
	pf.cbSize = (UINT)sizeof pf;
	if (m_info.GetParaFormat(pf)) {
		pf.dwMask |= PFM_TABSTOPS;
		pf.cTabCount = 4;
		pf.rgxTabs[0] = 900;
		pf.rgxTabs[1] = 1000;
		pf.rgxTabs[2] = 1100;
		pf.rgxTabs[3] = 1200;
		m_info.SetParaFormat(pf);
	}

	m_cfDef.cbSize = (UINT)sizeof m_cfDef;
	m_info.GetDefaultCharFormat(m_cfDef); // Use default char format as baseline to ensure consistency

	m_cfBold = m_cfDef;
	m_cfBold.dwMask |= CFM_BOLD;
	m_cfBold.dwEffects |= CFE_BOLD;

	m_info.SetDefaultCharFormat(m_cfDef);// Ensure default typing attributes are the same as def format
	m_info.SetSel(0, 0);
	m_info.SetSelectionCharFormat(m_cfDef); // Reset insertion format to default at start

    CreateNetworkInfo(m_info, m_cfDef, m_cfDef, true); // Use default format for 'bold' to keep headings non-bold consistently in this dialog
    FixAllLinksOff(); // Remove any auto-applied hyperlink styling across the whole content for consistency
	DisableAutoSelect(m_info);
	m_info.UpdateColors();
	s_pActiveInstance = this; // Register as active instance for UploadTimer driven refresh
	return TRUE;
}

CNetworkInfoDlg* CNetworkInfoDlg::GetActiveInstance()
{
	return s_pActiveInstance;
}

void CNetworkInfoDlg::OnDestroy()
{
	// Deregister active instance
	if (s_pActiveInstance == this)
		s_pActiveInstance = NULL;
	CResizableDialog::OnDestroy();
}

void CNetworkInfoDlg::RefreshInfo()
{
	// Refresh content with minimal flicker and stable formatting
	m_info.SetRedraw(FALSE);
	m_info.SetWindowText(EMPTY);

	// Re-apply paragraph tabs (tab stops may reset on full clear)
	PARAFORMAT pf = {};
	pf.cbSize = (UINT)sizeof pf;
	if (m_info.GetParaFormat(pf)) {
		pf.dwMask |= PFM_TABSTOPS;
		pf.cTabCount = 4;
		pf.rgxTabs[0] = 900;
		pf.rgxTabs[1] = 1000;
		pf.rgxTabs[2] = 1100;
		pf.rgxTabs[3] = 1200;
		m_info.SetParaFormat(pf);
	}

	// Reset default typing attributes to def format for consistency
	m_info.SetDefaultCharFormat(m_cfDef);
	m_info.SetSel(0, 0);
	m_info.SetSelectionCharFormat(m_cfDef);

    // Use default format for 'bold' to keep headings non-bold consistently
    CreateNetworkInfo(m_info, m_cfDef, m_cfDef, true);
    // Normalize links: turn off any auto-detected hyperlink styling altogether
    FixAllLinksOff();
	m_info.SetRedraw(TRUE);
	m_info.Invalidate();
}

bool CNetworkInfoDlg::IsAutoRefreshEnabled()
{
	// Query checkbox each time to avoid storing duplicated state.
	CButton* pBtn = (CButton*)GetDlgItem(IDC_NETWORK_INFO_AUTOREFRESH);
	return pBtn != NULL && pBtn->GetCheck() != 0;
}

void CNetworkInfoDlg::FixNickLink()
{
	// Find the nick line and remove any link formatting from that range
	CString text;
	m_info.GetWindowText(text);
	CString label = GetResString(_T("PW_NICK")) + _T(":\t");
	int pos = text.Find(label);
	if (pos >= 0) {
		int start = pos + label.GetLength();
		int end = text.Find(_T('\r'), start);
		if (end < 0)
			end = text.GetLength();
		long oldStart, oldEnd;
		m_info.GetSel(oldStart, oldEnd);
		m_info.SetSel(start, end);
		CHARFORMAT2 cf = {};
		cf.cbSize = (UINT)sizeof cf;
		cf.dwMask = CFM_LINK; // only link flag
		cf.dwEffects = 0; // clear link
		m_info.SetSelectionCharFormat(cf);
		m_info.SetSel(oldStart, oldEnd);
	}
}

void CNetworkInfoDlg::FixAllLinksOff()
{
	long oldStart, oldEnd;
	m_info.GetSel(oldStart, oldEnd);
	m_info.SetSel(0, -1);
	CHARFORMAT2 cf = {};
	cf.cbSize = (UINT)sizeof cf;
	cf.dwMask = CFM_LINK;
	cf.dwEffects = 0;
	m_info.SetSelectionCharFormat(cf);
	m_info.SetSel(oldStart, oldEnd);
}

void CreateNetworkInfo(CRichEditCtrlX &rCtrl, CHARFORMAT &rcfDef, CHARFORMAT &rcfBold, bool bFullInfo)
{
	if (bFullInfo) {
		///////////////////////////////////////////////////////////////////////////
		// Ports Info
		///////////////////////////////////////////////////////////////////////////
		rCtrl.SetSelectionCharFormat(rcfBold);
		rCtrl << GetResString(_T("CLIENT")) << _T("\r\n");
		rCtrl.SetSelectionCharFormat(rcfDef);

		rCtrl << GetResString(_T("PW_NICK")) << _T(":\t") << thePrefs.GetUserNick() << _T("\r\n");
		rCtrl << GetResString(_T("CD_UHASH")) << _T("\t") << md4str(thePrefs.GetUserHash()) << _T("\r\n");
		rCtrl << _T("TCP ") << GetResString(_T("PORT")) << _T(":\t") << thePrefs.GetPort() << _T("\r\n");
		rCtrl << _T("UDP ") << GetResString(_T("PORT")) << _T(":\t") << thePrefs.GetUDPPort() << _T("\r\n");
		rCtrl << _T("\r\n");
	}

	///////////////////////////////////////////////////////////////////////////
	// ED2K
	///////////////////////////////////////////////////////////////////////////
	rCtrl.SetSelectionCharFormat(rcfBold);
	rCtrl << _T("eD2K ") << GetResString(_T("NETWORK")) << _T("\r\n");
	rCtrl.SetSelectionCharFormat(rcfDef);

	rCtrl << GetResString(_T("STATUS")) << _T(":\t");
	LPCTSTR uid;
	if (theApp.serverconnect->IsConnected())
		uid = _T("CONNECTED");
	else if (theApp.serverconnect->IsConnecting())
		uid = _T("CONNECTING");
	else
		uid = _T("DISCONNECTED");
	rCtrl << GetResString(uid) << _T("\r\n");

	//I only show this in full display as the normal display is not
	//updated at regular intervals.
	if (bFullInfo && theApp.serverconnect->IsConnected()) {
		uint32 uTotalUser = 0;
		uint32 uTotalFile = 0;

		theApp.serverlist->GetUserFileStatus(uTotalUser, uTotalFile);
		rCtrl << GetResString(_T("UUSERS")) << _T(":\t") << GetFormatedUInt(uTotalUser) << _T("\r\n");
		rCtrl << GetResString(_T("PW_FILES")) << _T(":\t") << GetFormatedUInt(uTotalFile) << _T("\r\n");
	}

	CString buffer;
	if (theApp.serverconnect->IsConnected()) {

		if (theApp.serverconnect->IsLowID() && theApp.GetPublicIPv4() == 0)
			buffer = GetResString(_T("UNKNOWN"));
		else
			buffer.Format(_T("%s"), (LPCTSTR)ipstr(theApp.GetPublicIPv4()));
		rCtrl << GetResString(_T("IP")) << L" (v4):\t" << buffer << L"\r\n";

		if (!theApp.GetPublicIPv6().IsNull())
			rCtrl << GetResString(_T("IP")) << L" (v6):\t" << ipstr(theApp.GetPublicIPv6()) << L"\r\n";

		rCtrl << _T("TCP ") << GetResString(_T("PORT")) << L":\t" << thePrefs.GetPort() << L"\r\n";
		rCtrl << _T("UDP ") << GetResString(_T("PORT")) << L":\t" << thePrefs.GetUDPPort() << L"\r\n";

		rCtrl << GetResString(_T("ID")) << _T(":\t");
		if (theApp.serverconnect->IsConnected()) {
			buffer.Format(_T("%u"), theApp.serverconnect->GetClientID());
			rCtrl << buffer;
		}
		rCtrl << _T("\r\n");

		rCtrl << _T("\t");
		rCtrl << GetResString(theApp.serverconnect->IsLowID() ? _T("IDLOW") : _T("IDHIGH"));
		rCtrl << _T("\r\n");

		CServer *cur_server = theApp.serverconnect->GetCurrentServer();
		CServer *srv = cur_server ? theApp.serverlist->GetServerByAddress(cur_server->GetAddress(), cur_server->GetPort()) : NULL;
		if (srv) {
			rCtrl << _T("\r\n");
			rCtrl.SetSelectionCharFormat(rcfBold);
			rCtrl << _T("eD2K ") << GetResString(_T("SERVER")) << _T("\r\n");
			rCtrl.SetSelectionCharFormat(rcfDef);

			rCtrl << GetResString(_T("SW_NAME")) << _T(":\t") << srv->GetListName() << _T("\r\n");
			rCtrl << GetResString(_T("DESCRIPTION")) << _T(":\t") << srv->GetDescription() << _T("\r\n");
			rCtrl << GetResString(_T("IP")) << _T(":") << GetResString(_T("PORT")) << _T(":\t") << srv->GetAddress() << _T(":") << srv->GetPort() << _T("\r\n");
			rCtrl << GetResString(_T("VERSION")) << _T(":\t") << srv->GetVersion() << _T("\r\n");
			rCtrl << GetResString(_T("UUSERS")) << _T(":\t") << GetFormatedUInt(srv->GetUsers()) << _T("\r\n");
			rCtrl << GetResString(_T("PW_FILES")) << _T(":\t") << GetFormatedUInt(srv->GetFiles()) << _T("\r\n");
			rCtrl << GetResString(_T("CONNECTIONS")) << _T(":\t");
			rCtrl << GetResString(theApp.serverconnect->IsConnectedObfuscated() ? _T("OBFUSCATED") : _T("PRIONORMAL"));
			rCtrl << _T("\r\n");


			if (bFullInfo) {
				rCtrl << GetResString(_T("IDLOW")) << _T(":\t") << GetFormatedUInt(srv->GetLowIDUsers()) << _T("\r\n");
				rCtrl << GetResString(_T("PING")) << _T(":\t") << (UINT)srv->GetPing() << _T(" ms\r\n");

				// eServer Buddy info (right after basic server info, before features)
				if (theApp.serverconnect->IsLowID()) {
					// LowID: show our serving buddy status
					rCtrl << _T("eServer ") << GetResString(_T("SERVING_BUDDY")) << _T(":\t");
					switch (theApp.clientlist->GetEServerBuddyStatus()) {
					case Disconnected:
						uid = _T("SERVING_BUDDYNONE");
						break;
					case Connecting:
						uid = _T("CONNECTING");
						break;
					case Connected:
						uid = _T("CONNECTED");
						break;
					default:
						uid = EMPTY;
					}
					if (uid)
						rCtrl << GetResString(uid);
					rCtrl << _T("\r\n");

					// Show serving buddy details if connected
					if (theApp.clientlist->GetServingEServerBuddy() && theApp.clientlist->GetEServerBuddyStatus() == Connected) {
						CUpDownClient* pBuddy = theApp.clientlist->GetServingEServerBuddy();
						CString buddyInfo;
						buddyInfo.Format(_T("\t%s: %s\r\n"), (LPCTSTR)GetResString(_T("CLIENT")), (LPCTSTR)EscPercent(pBuddy->DbgGetClientInfo()));
						rCtrl << buddyInfo;
					}
				} else {
					// HighID: show how many LowID clients we're serving with numbered list
					if (theApp.clientlist->GetServedEServerBuddyCount() > 0) {
						CString servedHeader;
						servedHeader.Format(_T("eServer %s:\t%d\r\n"), (LPCTSTR)GetResString(_T("SERVED_BUDDIES")), (int)theApp.clientlist->GetServedEServerBuddyCount());
						rCtrl << servedHeader;
						POSITION pos = theApp.clientlist->GetServedEServerBuddyStartPosition();
						int idx = 1;
						while (pos != NULL) {
							CUpDownClient* p = theApp.clientlist->GetNextServedEServerBuddy(pos);
							if (p != NULL) {
								CString line;
								const CAddress& dispIP = !p->GetIP().IsNull() ? p->GetIP() : p->GetConnectIP();
								line.Format(_T("\t%d) %s: %s:%u\t%s\r\n"), idx, 
									(LPCTSTR)GetResString(_T("IPPORT")), 
									(LPCTSTR)ipstr(dispIP), 
									(unsigned)p->GetUserPort(),
									(LPCTSTR)EscPercent(p->GetUserName()));
								rCtrl << line;
								++idx;
							}
						}
					} else {
						CString servedInfo;
						servedInfo.Format(_T("eServer %s:\t%d\r\n"), (LPCTSTR)GetResString(_T("SERVED_BUDDIES")), 0);
						rCtrl << servedInfo;
					}
				}

				rCtrl << _T("\r\n");
				rCtrl.SetSelectionCharFormat(rcfBold);
				rCtrl << _T("eD2K ") << GetResString(_T("SERVER")) << _T(" ") << GetResString(_T("FEATURES")) << _T("\r\n");
				rCtrl.SetSelectionCharFormat(rcfDef);

				rCtrl << GetResString(_T("SERVER_LIMITS")) << _T(": ") << GetFormatedUInt(srv->GetSoftFiles()) << _T("/") << GetFormatedUInt(srv->GetHardFiles()) << _T("\r\n");

				if (thePrefs.IsExtControlsEnabled()) {
					CString sNo, sYes;
					sNo.Format(_T(": %s\r\n"), (LPCTSTR)GetResString(_T("NO")));
					sYes.Format(_T(": %s\r\n"), (LPCTSTR)GetResString(_T("YES")));
					bool bYes = srv->GetTCPFlags() & SRV_TCPFLG_COMPRESSION;
					rCtrl << GetResString(_T("SRV_TCPCOMPR")) << (bYes ? sYes : sNo);

					bYes = (srv->GetTCPFlags() & SRV_TCPFLG_NEWTAGS) || (srv->GetUDPFlags() & SRV_UDPFLG_NEWTAGS);
					rCtrl << GetResString(_T("SHORTTAGS")) << (bYes ? sYes : sNo);

					bYes = (srv->GetTCPFlags() & SRV_TCPFLG_UNICODE) || (srv->GetUDPFlags() & SRV_UDPFLG_UNICODE);
					rCtrl << _T("Unicode") << (bYes ? sYes : sNo);

					bYes = srv->GetTCPFlags() & SRV_TCPFLG_TYPETAGINTEGER;
					rCtrl << GetResString(_T("SERVERFEATURE_INTTYPETAGS")) << (bYes ? sYes : sNo);

					bYes = srv->GetUDPFlags() & SRV_UDPFLG_EXT_GETSOURCES;
					rCtrl << GetResString(_T("SRV_UDPSR")) << (bYes ? sYes : sNo);

					bYes = srv->GetUDPFlags() & SRV_UDPFLG_EXT_GETSOURCES2;
					rCtrl << GetResString(_T("SRV_UDPSR")) << _T(" #2") << (bYes ? sYes : sNo);

					bYes = srv->GetUDPFlags() & SRV_UDPFLG_EXT_GETFILES;
					rCtrl << GetResString(_T("SRV_UDPFR")) << (bYes ? sYes : sNo);

					bYes = srv->SupportsLargeFilesTCP() || srv->SupportsLargeFilesUDP();
					rCtrl << GetResString(_T("SRV_LARGEFILES")) << (bYes ? sYes : sNo);

					bYes = srv->SupportsObfuscationUDP();
					rCtrl << GetResString(_T("PROTOCOLOBFUSCATION")) << _T(" (UDP)") << (bYes ? sYes : sNo);

					bYes = srv->SupportsObfuscationTCP();
					rCtrl << GetResString(_T("PROTOCOLOBFUSCATION")) << _T(" (TCP)") << (bYes ? sYes : sNo);
				}
			}
		}
	}
	rCtrl << _T("\r\n");

	///////////////////////////////////////////////////////////////////////////
	// Kademlia
	///////////////////////////////////////////////////////////////////////////
	rCtrl.SetSelectionCharFormat(rcfBold);
	rCtrl << GetResString(_T("KADEMLIA")) << _T(" ") << GetResString(_T("NETWORK")) << _T("\r\n");
	rCtrl.SetSelectionCharFormat(rcfDef);

	rCtrl << GetResString(_T("STATUS")) << _T(":\t");
	if (Kademlia::CKademlia::IsConnected()) {
		rCtrl << GetResString(Kademlia::CKademlia::IsFirewalled() ? _T("FIREWALLED") : _T("KADOPEN"));
		if (Kademlia::CKademlia::IsRunningInLANMode())
			rCtrl << _T(" (") << GetResString(_T("LANMODE")) << _T(")");
		rCtrl << _T("\r\n");
		rCtrl << _T("UDP ") << GetResString(_T("STATUS")) << _T(":\t");
		if (Kademlia::CUDPFirewallTester::IsFirewalledUDP(true))
			rCtrl << GetResString(_T("FIREWALLED"));
		else {
			rCtrl << GetResString(_T("KADOPEN"));
			if (!Kademlia::CUDPFirewallTester::IsVerified())
				rCtrl << _T(" (") << GetResString(_T("UNVERIFIED")).MakeLower() << _T(")");
		}
		rCtrl << _T("\r\n");

		buffer.Format(_T("%s:%i"), (LPCTSTR)ipstr(htonl(Kademlia::CKademlia::GetPrefs()->GetIPAddress())), thePrefs.GetUDPPort());
		rCtrl << GetResString(_T("IP")) << _T(":") << GetResString(_T("PORT")) << _T(":\t") << buffer << _T("\r\n");

		buffer.Format(_T("%u"), Kademlia::CKademlia::GetPrefs()->GetIPAddress());
		rCtrl << GetResString(_T("ID")) << _T(":\t") << buffer << _T("\r\n");
		if (Kademlia::CKademlia::GetPrefs()->GetUseExternKadPort() && Kademlia::CKademlia::GetPrefs()->GetExternalKadPort() != 0
			&& Kademlia::CKademlia::GetPrefs()->GetInternKadPort() != Kademlia::CKademlia::GetPrefs()->GetExternalKadPort())
		{
			buffer.Format(_T("%u"), Kademlia::CKademlia::GetPrefs()->GetExternalKadPort());
			rCtrl << GetResString(_T("EXTERNUDPPORT")) << _T(":\t") << buffer << _T("\r\n");
		}

#ifndef NATTTESTMODE
		if (Kademlia::CUDPFirewallTester::IsFirewalledUDP(true)) {
#endif
			rCtrl << GetResString(_T("SERVING_BUDDY")) << _T(":\t");
			switch (theApp.clientlist->GetServingBuddyStatus()) {
			case Disconnected:
				uid = _T("SERVING_BUDDYNONE");
				break;
			case Connecting:
				uid = _T("CONNECTING");
				break;
			case Connected:
				uid = _T("CONNECTED");
				break;
			default:
				uid = EMPTY;
			}
			if (uid)
				rCtrl << GetResString(uid);
			rCtrl << _T("\r\n");
#ifndef NATTTESTMODE
		}
#endif

		if (bFullInfo) {
			CString sKadID;
			Kademlia::CKademlia::GetPrefs()->GetKadID(sKadID);
			rCtrl << GetResString(_T("CD_UHASH")) << _T("\t") << sKadID << _T("\r\n");
			// List served buddy clients (IP:Port and Kad ID)
			if (theApp.clientlist->GetServedBuddyCount() > 0) {
				CString servedHeader;
				servedHeader.Format(_T("%s:\t%d\r\n"), (LPCTSTR)GetResString(_T("SERVED_BUDDIES")), (int)theApp.clientlist->GetServedBuddyCount());
				rCtrl << servedHeader;
				POSITION pos = theApp.clientlist->GetServedBuddyStartPosition();
				int idx = 1;
				while (pos != NULL) {
					CUpDownClient* p = theApp.clientlist->GetNextServedBuddy(pos);
					if (p != NULL) {
						CString line;
						CString sPeerKadID;
						if (p->HasValidServingBuddyID())
							sPeerKadID = md4str(p->GetServingBuddyID());
						// Prefer real IP, fallback to connect IP to avoid 0.0.0.0 entries.
						const CAddress& dispIP = !p->GetIP().IsNull() ? p->GetIP() : p->GetConnectIP();
						line.Format(_T("\t%d) %s: %s:%u\t%s %s\r\n"), idx, (LPCTSTR)GetResString(_T("IPPORT")), (LPCTSTR)ipstr(dispIP), (unsigned)p->GetKadPort(), (LPCTSTR)GetResString(_T("CD_UHASH")), (LPCTSTR)sPeerKadID);
						rCtrl << line;
						++idx;
					}
				}
			}

			rCtrl << GetResString(_T("UUSERS")) << _T(":\t") << GetFormatedUInt(Kademlia::CKademlia::GetKademliaUsers()) << _T(" (Experimental: ") << GetFormatedUInt(Kademlia::CKademlia::GetKademliaUsers(true)) << _T(")\r\n");
			rCtrl << GetResString(_T("PW_FILES")) << _T(":\t") << GetFormatedUInt(Kademlia::CKademlia::GetKademliaFiles()) << _T("\r\n");
			rCtrl << GetResString(_T("INDEXED")) << _T(":\r\n");
			buffer.Format(GetResString(_T("KADINFO_SRC")), Kademlia::CKademlia::GetIndexed()->m_uTotalIndexSource);
			rCtrl << buffer;
			buffer.Format(GetResString(_T("KADINFO_KEYW")), Kademlia::CKademlia::GetIndexed()->m_uTotalIndexKeyword);
			rCtrl << buffer;
			buffer.Format(_T("\t%s: %u\r\n"), (LPCTSTR)GetResString(_T("NOTES")), Kademlia::CKademlia::GetIndexed()->m_uTotalIndexNotes);
			rCtrl << buffer;
			buffer.Format(_T("\t%s: %u\r\n"), (LPCTSTR)GetResString(_T("THELOAD")), Kademlia::CKademlia::GetIndexed()->m_uTotalIndexLoad);
			rCtrl << buffer;
		}
	} else
		rCtrl << GetResString(Kademlia::CKademlia::IsRunning() ? _T("CONNECTING") : _T("DISCONNECTED")) << _T("\r\n");

	rCtrl << _T("\r\n");

	///////////////////////////////////////////////////////////////////////////
	// Web Interface
	///////////////////////////////////////////////////////////////////////////
	rCtrl.SetSelectionCharFormat(rcfBold);
	rCtrl << GetResString(_T("WEBSRV")) << _T("\r\n");
	rCtrl.SetSelectionCharFormat(rcfDef);
	rCtrl << GetResString(_T("STATUS")) << _T(":\t");
	rCtrl << GetResString(thePrefs.GetWSIsEnabled() ? _T("ENABLED") : _T("DISABLED")) << _T("\r\n");
	if (thePrefs.GetWSIsEnabled()) {
		CString sTemp;
		sTemp.Format(_T("%d %s"), static_cast<int>(theApp.webserver->GetSessionCount()), (LPCTSTR)GetResString(_T("ACTSESSIONS")));
		rCtrl << _T("\t") << sTemp << _T("\r\n"); //count

		if (thePrefs.GetYourHostname().IsEmpty() || thePrefs.GetYourHostname().Find(_T('.')) < 0)
			sTemp = ipstr(theApp.serverconnect->GetLocalIP());
		else
			sTemp = thePrefs.GetYourHostname();
		rCtrl << _T("URL:\t") << (thePrefs.GetWebUseHttps() ? _T("https://") : _T("http://"));
		rCtrl << sTemp << _T(":") << thePrefs.GetWSPort() << _T("/\r\n"); //web interface host name
	}
}

void CNetworkInfoDlg::OnNattForcePublish()
{
	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NATTTESTMODE: ForcePublish] Publish all shared files\n"));
	if (!Kademlia::CKademlia::IsConnected()) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NATTTESTMODE: ForcePublish] Kad not connected\n"));
		return;
	}

	// 1) Force keyword/file publish for all shared files (indexing)
	for (UINT i = 0; i < theApp.sharedfiles->GetCount(); ++i) {
		CKnownFile* kf = theApp.sharedfiles->GetFileByIndex((int)i);
		if (kf != NULL) {
			if (Kademlia::CSearchManager::PrepareLookup(Kademlia::CSearch::STOREFILE, true, Kademlia::CUInt128(kf->GetFileHash())) == NULL) {
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NATTTESTMODE: ForcePublish] PrepareLookup (keyword/storefile) failed: %s\n"), (LPCTSTR)kf->GetFileName());
			} else {
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NATTTESTMODE: ForcePublish] Requested keyword/storefile publish: %s\n"), (LPCTSTR)kf->GetFileName());
			}
		}
	}

	// 2) Force source publish so Kad queries can find us immediately (LowID requires serving buddy)
	if (theApp.IsFirewalled() && (Kademlia::CUDPFirewallTester::IsFirewalledUDP(true) || !Kademlia::CUDPFirewallTester::IsVerified())) {
		if (theApp.clientlist->GetServingBuddy() == NULL) {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NATTTESTMODE: ForcePublish] Skipping source publish: no serving buddy connected.\n"));
			return;
		}
	}

	
	theApp.sharedfiles->ClearKadSourcePublishInfo(); // Clear last publish timers to unblock immediate source publish

	for (UINT i = 0; i < theApp.sharedfiles->GetCount(); ++i) {
		CKnownFile* kf = theApp.sharedfiles->GetFileByIndex((int)i);
		if (kf == NULL)
			continue;

		// Mark for immediate source publish and issue a STOREFILE which carries source tags
		kf->SetLastPublishTimeKadSrc(0, 0);
		if (kf->PublishSrc()) {
			if (Kademlia::CSearchManager::PrepareLookup(Kademlia::CSearch::STOREFILE, true, Kademlia::CUInt128(kf->GetFileHash())) == NULL) {
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NATTTESTMODE: ForcePublish] PrepareLookup (source) failed: %s\n"), (LPCTSTR)kf->GetFileName());
			} else {
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NATTTESTMODE: ForcePublish] Requested source publish: %s\n"), (LPCTSTR)kf->GetFileName());
			}
		}
	}
}
