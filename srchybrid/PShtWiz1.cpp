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
#include <afxinet.h>
#include "emule.h"
#include "enbitmap.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "Statistics.h"
#include "ListenSocket.h"
#include "ClientUDPSocket.h"
#include "UPnPImpl.h"
#include "UPnPImplWrapper.h"
#include "opcodes.h"
#include "emuledlg.h"
#include "eMuleAI/DarkMode.h"
#include "RichEditCtrlX.h"
#include "eMuleAI/GeoLiteDownloadDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define	IDT_UPNP_TICKS	1

static void SetWizardCloseEnabled(CPropertySheetEx *pSheet, bool bEnable)
{
	if (pSheet == NULL || pSheet->GetSafeHwnd() == NULL)
		return;

	HMENU hMenu = ::GetSystemMenu(pSheet->GetSafeHwnd(), FALSE);
	if (hMenu != NULL) {
		::EnableMenuItem(hMenu, SC_CLOSE, MF_BYCOMMAND | (bEnable ? MF_ENABLED : (MF_GRAYED | MF_DISABLED)));
		pSheet->DrawMenuBar();
	}
}

///////////////////////////////////////////////////////////////////////////////
// A static control that paints its own background dark in Dark Mode
class CDarkStatic : public CStatic
{
public:
	DECLARE_MESSAGE_MAP()
	afx_msg void OnPaint();
};

BEGIN_MESSAGE_MAP(CDarkStatic, CStatic)
	ON_WM_PAINT()
END_MESSAGE_MAP()

void CDarkStatic::OnPaint()
{
	CPaintDC dc(this);
	CRect rc;
	GetClientRect(&rc);

	// fill background with the app's default dark brush
	dc.FillRect(rc, &CDarkMode::m_brDefault);

	// draw the static text in the control's own font
	CString text;
	GetWindowText(text);

	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(GetCustomSysColor(COLOR_WINDOWTEXT));

	// select the font that was assigned via SetFont()
	CFont* pFont = GetFont();
	HGDIOBJ oldFont = pFont ? dc.SelectObject(pFont) : nullptr;

	dc.DrawText(text, rc, DT_LEFT | DT_WORDBREAK);

	if (oldFont)
		dc.SelectObject(oldFont);
}


///////////////////////////////////////////////////////////////////////////////
// CDlgPageWizard dialog

class CDlgPageWizard : public CPropertyPageEx
{
	DECLARE_DYNCREATE(CDlgPageWizard)

public:
	CDlgPageWizard();

	explicit CDlgPageWizard(UINT nIDTemplate, LPCTSTR pszCaption = NULL, LPCTSTR pszHeaderTitle = NULL, LPCTSTR pszHeaderSubTitle = NULL)
		: CPropertyPageEx(nIDTemplate)
	{
		if (pszCaption) {
			m_strCaption = pszCaption; // "convenience storage"
			m_psp.pszTitle = m_strCaption;
			m_psp.dwFlags |= PSP_USETITLE;
		}
		if (pszHeaderTitle && pszHeaderTitle[0] != _T('\0')) {
			m_strHeaderTitle = pszHeaderTitle;
			m_psp.dwSize = (DWORD)sizeof m_psp;
			m_psp.pszHeaderTitle = m_strHeaderTitle;		// hook up the title pointer
			m_psp.dwFlags |= PSP_USEHEADERTITLE;			// tell Win32 to use it
		}
		if (pszHeaderSubTitle && pszHeaderSubTitle[0] != _T('\0')) {
			m_strHeaderSubTitle = pszHeaderSubTitle;
			m_psp.dwSize = (DWORD)sizeof m_psp;
			m_psp.pszHeaderSubTitle = m_strHeaderSubTitle;	// hook up the subtitle pointer
			m_psp.dwFlags |= PSP_USEHEADERSUBTITLE;			// tell Win32 to use it
		}
	}

protected:

	virtual BOOL OnSetActive();
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
	afx_msg BOOL OnInitDialog();
	afx_msg BOOL OnHelpInfo(HELPINFO* pHelpInfo);
};

IMPLEMENT_DYNCREATE(CDlgPageWizard, CPropertyPageEx)

BEGIN_MESSAGE_MAP(CDlgPageWizard, CPropertyPageEx)
	ON_WM_HELPINFO()
END_MESSAGE_MAP()

CDlgPageWizard::CDlgPageWizard()
	: CPropertyPageEx()
{
}

void CDlgPageWizard::DoDataExchange(CDataExchange *pDX)
{
	CPropertyPageEx::DoDataExchange(pDX);
}

BOOL CDlgPageWizard::OnSetActive()
{
	CPropertySheetEx *pSheet = (CPropertySheetEx*)GetParent();
	if (pSheet->IsWizard()) {
		if (!m_strCaption.IsEmpty())
			pSheet->SetWindowText(m_strCaption);
		int iPages = pSheet->GetPageCount();
		int iActPage = pSheet->GetActiveIndex();
		DWORD dwButtons = 0;
		if (iActPage > 0)
			dwButtons |= PSWIZB_BACK;
		if (iActPage < (iPages - 1))
			dwButtons |= PSWIZB_NEXT;
		if ((pSheet->m_psh.dwFlags & PSH_WIZARDHASFINISH) && iActPage > 0)
			dwButtons |= PSWIZB_FINISH;
		if (iActPage == iPages - 1) {
			if (pSheet->m_psh.dwFlags & PSH_WIZARDHASFINISH)
				dwButtons &= ~PSWIZB_NEXT;
			dwButtons |= PSWIZB_FINISH;
		}
		pSheet->SetWizardButtons(dwButtons);
		if (CWnd *pCancel = pSheet->GetDlgItem(IDCANCEL))
			pCancel->EnableWindow(TRUE);
		if (CWnd *pFinish = pSheet->GetDlgItem(ID_WIZFINISH))
			pFinish->EnableWindow(TRUE);
		SetWizardCloseEnabled(pSheet, true);
	}
	return CPropertyPageEx::OnSetActive();
}

BOOL CDlgPageWizard::OnCommand(WPARAM wParam, LPARAM lParam)
{
	if (wParam == ID_HELP || wParam == IDHELP)
		return OnHelpInfo(NULL);

	return __super::OnCommand(wParam, lParam);
}

BOOL CDlgPageWizard::OnInitDialog()
{
	BOOL bRes = __super::OnInitDialog();

	if (IsDarkModeEnabled())
		ApplyTheme(m_hWnd);
	return bRes;
}

BOOL CDlgPageWizard::OnHelpInfo(HELPINFO*)
{
	BrowserOpen(MOD_PAGES_BASE_URL, thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
	return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// CPPgWiz1ExtRes dialog

class CPPgWiz1ExtRes : public CDlgPageWizard
{
	DECLARE_DYNAMIC(CPPgWiz1ExtRes)

	enum
	{
		IDD = IDD_WIZ1_EXTRES
	};

public:
	CPPgWiz1ExtRes();
	explicit CPPgWiz1ExtRes(UINT nIDTemplate, LPCTSTR pszCaption = NULL, LPCTSTR pszHeaderTitle = NULL, LPCTSTR pszHeaderSubTitle = NULL)
		: CDlgPageWizard(nIDTemplate, pszCaption, pszHeaderTitle, pszHeaderSubTitle)
	{
	}
	virtual BOOL OnInitDialog();
	virtual BOOL OnSetActive();

protected:
	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support
	void ApplyNoticeText();
	void UpdateNoticeLayout();

	CFont m_FontNotice;
	CRichEditCtrlX m_wndNotice;
	CDarkStatic m_ctrlHint;

	DECLARE_MESSAGE_MAP()
};

IMPLEMENT_DYNAMIC(CPPgWiz1ExtRes, CDlgPageWizard)

BEGIN_MESSAGE_MAP(CPPgWiz1ExtRes, CDlgPageWizard)
END_MESSAGE_MAP()

CPPgWiz1ExtRes::CPPgWiz1ExtRes()
	: CDlgPageWizard(CPPgWiz1ExtRes::IDD)
{
}

void CPPgWiz1ExtRes::DoDataExchange(CDataExchange *pDX)
{
	CDlgPageWizard::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_WIZ1_EXTRES_TEXT, m_wndNotice);
}

BOOL CPPgWiz1ExtRes::OnInitDialog()
{
	CDlgPageWizard::OnInitDialog();
	InitWindowStyles(this);
	if (CWnd *pHint = GetDlgItem(IDC_WIZ1_BTN_HINT))
		pHint->SetWindowText(GetResString(_T("WIZ1_WELCOME_BTN_HINT")));

	m_wndNotice.SetReadOnly(TRUE);
	m_wndNotice.SetDisableSelectOnFocus(true);
	m_wndNotice.SetSelectionDisabled(true);
	m_wndNotice.SetDisableMouseInput(true);
	m_wndNotice.SendMessage(EM_AUTOURLDETECT, TRUE, 0);
	m_wndNotice.SetEventMask(m_wndNotice.GetEventMask() | ENM_LINK);
	CreatePointFont(m_FontNotice, 9 * 10, _T("Segoe UI"));
	m_wndNotice.SetFont(&m_FontNotice);
	ApplyNoticeText();

	if (IsDarkModeEnabled()) {
		if (GetDlgItem(IDC_WIZ1_BTN_HINT) != NULL)
			m_ctrlHint.SubclassDlgItem(IDC_WIZ1_BTN_HINT, this);
	}

	return TRUE;
}

void CPPgWiz1ExtRes::ApplyNoticeText()
{
	CString text;
	text.Format(GetResString(_T("EMULE_AI_EXTRES_BODY")), kGeoLiteDownloadUrl);
	m_wndNotice.SetWindowText(text);
	UpdateNoticeLayout();
	m_wndNotice.UpdateColors();
	m_wndNotice.SetSel(0, 0);
	m_wndNotice.SendMessage(EM_SCROLLCARET, 0, 0);
	m_wndNotice.LineScroll(-m_wndNotice.GetLineCount());
}

void CPPgWiz1ExtRes::UpdateNoticeLayout()
{
	CRect rc;
	m_wndNotice.GetClientRect(&rc);
	const int kPadX = 16;
	const int kPadTop = 7;
	const int kPadBottom = 7;
	rc.DeflateRect(kPadX, kPadTop, kPadX, kPadBottom);
	m_wndNotice.SetRect(&rc);
	m_wndNotice.SendMessage(EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
}

BOOL CPPgWiz1ExtRes::OnSetActive()
{
	BOOL bRes = CDlgPageWizard::OnSetActive();
	CPropertySheetEx *pSheet = (CPropertySheetEx*)GetParent();
	if (pSheet != NULL && pSheet->IsWizard()) {
		if (CWnd *pCancel = pSheet->GetDlgItem(IDCANCEL))
			pCancel->EnableWindow(FALSE);
		if (CWnd *pFinish = pSheet->GetDlgItem(ID_WIZFINISH))
			pFinish->EnableWindow(FALSE);
		SetWizardCloseEnabled(pSheet, false);
	}
	return bRes;
}

///////////////////////////////////////////////////////////////////////////////
// CPPgWiz1Welcome dialog

class CPPgWiz1Welcome : public CDlgPageWizard
{
	DECLARE_DYNAMIC(CPPgWiz1Welcome)

	enum
	{
		IDD = IDD_WIZ1_WELCOME
	};

public:
	CPPgWiz1Welcome();
	explicit CPPgWiz1Welcome(UINT nIDTemplate, LPCTSTR pszCaption = NULL, LPCTSTR pszHeaderTitle = NULL, LPCTSTR pszHeaderSubTitle = NULL)
		: CDlgPageWizard(nIDTemplate, pszCaption, pszHeaderTitle, pszHeaderSubTitle)
	{
	}
	virtual BOOL OnInitDialog();

protected:
	CFont m_FontTitle;
	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
	afx_msg void	OnPaint(); 

	CFont      m_fntEndTitle;
	CFont      m_fntEndBody;
	CDarkStatic m_ctrlTitle;
	CDarkStatic m_ctrlActions;
	CDarkStatic m_ctrlBtnHint;

};

IMPLEMENT_DYNAMIC(CPPgWiz1Welcome, CDlgPageWizard)

BEGIN_MESSAGE_MAP(CPPgWiz1Welcome, CDlgPageWizard)
	ON_WM_PAINT()
END_MESSAGE_MAP()

CPPgWiz1Welcome::CPPgWiz1Welcome()
	: CDlgPageWizard(CPPgWiz1Welcome::IDD)
{
}

void CPPgWiz1Welcome::DoDataExchange(CDataExchange *pDX)
{
	CDlgPageWizard::DoDataExchange(pDX);
}

BOOL CPPgWiz1Welcome::OnInitDialog()
{
	CFont fontVerdanaBold;
	CreatePointFont(fontVerdanaBold, 12 * 10, _T("Verdana Bold"));
	LOGFONT lf;
	fontVerdanaBold.GetLogFont(&lf);
	lf.lfWeight = FW_BOLD;
	m_FontTitle.CreateFontIndirect(&lf);

	CStatic *pStatic = static_cast<CStatic*>(GetDlgItem(IDC_WIZ1_TITLE));
	pStatic->SetFont(&m_FontTitle);

	CDlgPageWizard::OnInitDialog();
	InitWindowStyles(this);
	SetDlgItemText(IDC_WIZ1_TITLE, GetResString(_T("WIZ1_WELCOME_TITLE")));
	SetDlgItemText(IDC_WIZ1_ACTIONS, GetResString(_T("WIZ1_WELCOME_ACTIONS")));
	SetDlgItemText(IDC_WIZ1_BTN_HINT, GetResString(_T("WIZ1_WELCOME_BTN_HINT")));

	if (IsDarkModeEnabled()) {
		// subclass the three static controls
		m_ctrlTitle.SubclassDlgItem(IDC_WIZ1_TITLE, this);
		m_ctrlActions.SubclassDlgItem(IDC_WIZ1_ACTIONS, this);
		m_ctrlBtnHint.SubclassDlgItem(IDC_WIZ1_BTN_HINT, this);
	}

	return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// CPPgWiz1General dialog

class CPPgWiz1General : public CDlgPageWizard
{
	DECLARE_DYNAMIC(CPPgWiz1General)

	enum
	{
		IDD = IDD_WIZ1_GENERAL
	};

public:
	CPPgWiz1General();
	explicit CPPgWiz1General(UINT nIDTemplate, LPCTSTR pszCaption = NULL, LPCTSTR pszHeaderTitle = NULL, LPCTSTR pszHeaderSubTitle = NULL)
		: CDlgPageWizard(nIDTemplate, pszCaption, pszHeaderTitle, pszHeaderSubTitle), m_iAutoConnectAtStart(), m_iAutoStart()
	{
	}
	virtual BOOL OnInitDialog();

	CString m_strNick;
	int m_iAutoConnectAtStart;
	int m_iAutoStart;

protected:
	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
};

IMPLEMENT_DYNAMIC(CPPgWiz1General, CDlgPageWizard)

BEGIN_MESSAGE_MAP(CPPgWiz1General, CDlgPageWizard)
END_MESSAGE_MAP()

CPPgWiz1General::CPPgWiz1General()
	: CDlgPageWizard(CPPgWiz1General::IDD)
	, m_iAutoConnectAtStart()
	, m_iAutoStart()
{
}

void CPPgWiz1General::DoDataExchange(CDataExchange *pDX)
{
	CDlgPageWizard::DoDataExchange(pDX);
	DDX_Text(pDX, IDC_NICK, m_strNick);
	DDX_Check(pDX, IDC_AUTOCONNECT, m_iAutoConnectAtStart);
	DDX_Check(pDX, IDC_AUTOSTART, m_iAutoStart);
}

BOOL CPPgWiz1General::OnInitDialog()
{
	CDlgPageWizard::OnInitDialog();
	InitWindowStyles(this);
	static_cast<CEdit*>(GetDlgItem(IDC_NICK))->SetLimitText(thePrefs.GetMaxUserNickLength());
	SetDlgItemText(IDC_NICK_FRM, GetResString(_T("ENTERUSERNAME")));
	SetDlgItemText(IDC_AUTOCONNECT, GetResString(_T("FIRSTAUTOCON")));
	SetDlgItemText(IDC_AUTOSTART, GetResString(_T("WIZ_STARTWITHWINDOWS")));
	return TRUE;
}

///////////////////////////////////////////////////////////////////////////////
// CPPgWiz1Ports & Connections test dialog

class CPPgWiz1Ports : public CDlgPageWizard
{
	DECLARE_DYNAMIC(CPPgWiz1Ports)

	enum
	{
		IDD = IDD_WIZ1_PORTS
	};
	UINT	m_lastudp;

public:
	CPPgWiz1Ports();
	explicit CPPgWiz1Ports(UINT nIDTemplate, LPCTSTR pszCaption = NULL, LPCTSTR pszHeaderTitle = NULL, LPCTSTR pszHeaderSubTitle = NULL)
		: CDlgPageWizard(nIDTemplate, pszCaption, pszHeaderTitle, pszHeaderSubTitle)
		, m_lastudp()
		, m_uTCP()
		, m_uUDP()
		, m_pbUDPDisabled()
		, m_nUPnPTicks()
	{
	}


	virtual BOOL OnInitDialog();
	afx_msg void OnStartConTest();
	afx_msg void OnStartUPnP();
	afx_msg void OnEnChangeUDPDisable();

	afx_msg void OnEnChangeUDP();
	afx_msg void OnEnChangeTCP();
	afx_msg void OnTimer(UINT_PTR nIDEvent);

	BOOL	OnKillActive();
	void	OnOK();
	void	OnCancel();

	void OnPortChange();

	CString	m_sTestURL; // , m_sUDP, m_sTCP;
	uint16	GetTCPPort();
	uint16	GetUDPPort();
	UINT	m_uTCP;
	UINT	m_uUDP;
	bool	*m_pbUDPDisabled;

protected:
	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support
	void	ResetUPnPProgress();
	int		m_nUPnPTicks;

	DECLARE_MESSAGE_MAP()
};

IMPLEMENT_DYNAMIC(CPPgWiz1Ports, CDlgPageWizard)

BEGIN_MESSAGE_MAP(CPPgWiz1Ports, CDlgPageWizard)
	ON_BN_CLICKED(IDC_STARTTEST, OnStartConTest)
	ON_BN_CLICKED(IDC_UDPDISABLE, OnEnChangeUDPDisable)
	ON_BN_CLICKED(IDC_UPNPSTART, OnStartUPnP)
	ON_EN_CHANGE(IDC_TCP, OnEnChangeTCP)
	ON_EN_CHANGE(IDC_UDP, OnEnChangeUDP)
	ON_WM_TIMER()
END_MESSAGE_MAP()

CPPgWiz1Ports::CPPgWiz1Ports()
	: CDlgPageWizard(CPPgWiz1Ports::IDD)
	, m_lastudp()
	, m_uTCP()
	, m_uUDP()
	, m_pbUDPDisabled()
	, m_nUPnPTicks()
{
}

void CPPgWiz1Ports::DoDataExchange(CDataExchange *pDX)
{
	CDlgPageWizard::DoDataExchange(pDX);
	DDX_Text(pDX, IDC_TCP, m_uTCP);
	DDX_Text(pDX, IDC_UDP, m_uUDP);
}

void CPPgWiz1Ports::OnEnChangeTCP()
{
	OnPortChange();
}

void CPPgWiz1Ports::OnEnChangeUDP()
{
	OnPortChange();
}

uint16 CPPgWiz1Ports::GetTCPPort()
{
	return (uint16)GetDlgItemInt(IDC_TCP, NULL, FALSE);
}

uint16 CPPgWiz1Ports::GetUDPPort()
{
	return (uint16)(IsDlgButtonChecked(IDC_UDPDISABLE) ? 0 : GetDlgItemInt(IDC_UDP, NULL, FALSE));
}

void CPPgWiz1Ports::OnPortChange()
{
	bool bEnable = (theApp.IsPortchangeAllowed()
		&&
		(  theApp.listensocket->GetConnectedPort() != GetTCPPort()
		|| theApp.listensocket->GetConnectedPort() == 0
		|| theApp.clientudp->GetConnectedPort() != GetUDPPort()
		|| theApp.clientudp->GetConnectedPort() == 0
		));

	GetDlgItem(IDC_STARTTEST)->EnableWindow(bEnable);
}

BOOL CPPgWiz1Ports::OnKillActive()
{
	ResetUPnPProgress();
	return CDlgPageWizard::OnKillActive();
}

void CPPgWiz1Ports::OnOK()
{
	ResetUPnPProgress();
	CDlgPageWizard::OnOK();
}

void CPPgWiz1Ports::OnCancel()
{
	ResetUPnPProgress();
	CDlgPageWizard::OnCancel();
}

// ** UPnP Button stuff
void CPPgWiz1Ports::OnStartUPnP()
{
	CDlgPageWizard::OnApply();
	theApp.emuledlg->StartUPnP(true, GetTCPPort(), GetUDPPort());

	SetDlgItemText(IDC_UPNPSTATUS, GetResString(_T("UPNPSETUP")));
	GetDlgItem(IDC_UPNPSTART)->EnableWindow(FALSE);
	m_nUPnPTicks = 0;
	static_cast<CProgressCtrl*>(GetDlgItem(IDC_UPNPPROGRESS))->SetPos(0);
	VERIFY(SetTimer(IDT_UPNP_TICKS, SEC2MS(1), NULL) != 0);
}

void CPPgWiz1Ports::OnTimer(UINT_PTR /*nIDEvent*/)
{
	++m_nUPnPTicks;
	if (theApp.m_pUPnPFinder && theApp.m_pUPnPFinder->GetImplementation()->ArePortsForwarded() == TRIS_UNKNOWN)
		if (m_nUPnPTicks < 40) {
			static_cast<CProgressCtrl*>(GetDlgItem(IDC_UPNPPROGRESS))->SetPos(m_nUPnPTicks);
			return;
		}

	if (theApp.m_pUPnPFinder && theApp.m_pUPnPFinder->GetImplementation()->ArePortsForwarded() == TRIS_TRUE) {
		static_cast<CProgressCtrl*>(GetDlgItem(IDC_UPNPPROGRESS))->SetPos(40);
		CString strMessage;
		strMessage.Format(GetResString(_T("UPNPSUCCESS")), GetTCPPort(), GetUDPPort());
		SetDlgItemText(IDC_UPNPSTATUS, strMessage);
		// enable UPnP in the preferences after the successful try
		thePrefs.m_bEnableUPnP = true;
	} else {
		static_cast<CProgressCtrl*>(GetDlgItem(IDC_UPNPPROGRESS))->SetPos(0);
		SetDlgItemText(IDC_UPNPSTATUS, GetResString(_T("UPNPFAILED")));
	}
	GetDlgItem(IDC_UPNPSTART)->EnableWindow(TRUE);
	VERIFY(KillTimer(IDT_UPNP_TICKS));
}

void CPPgWiz1Ports::ResetUPnPProgress()
{
	KillTimer(IDT_UPNP_TICKS);
	static_cast<CProgressCtrl*>(GetDlgItem(IDC_UPNPPROGRESS))->SetPos(0);
	GetDlgItem(IDC_UPNPSTART)->EnableWindow(TRUE);
}

// **

void CPPgWiz1Ports::OnStartConTest()
{
	uint16 tcp = GetTCPPort();
	if (tcp == 0)
		return;
	uint16 udp = GetUDPPort();

	if (tcp != theApp.listensocket->GetConnectedPort() || udp != theApp.clientudp->GetConnectedPort()) {
		if (!theApp.IsPortchangeAllowed()) {
			LocMessageBox(_T("NOPORTCHANGEPOSSIBLE"), MB_OK, 0);
			return;
		}

		// set new ports
		thePrefs.port = tcp;
		thePrefs.udpport = udp;

		theApp.listensocket->Rebind();
		theApp.clientudp->Rebind();
	}

	TriggerPortTest(tcp, udp);
}

BOOL CPPgWiz1Ports::OnInitDialog()
{
	CDlgPageWizard::OnInitDialog();
	m_lastudp = m_uUDP;
	CheckDlgButton(IDC_UDPDISABLE, !m_uUDP);
	GetDlgItem(IDC_UDP)->EnableWindow(!IsDlgButtonChecked(IDC_UDPDISABLE));
	static_cast<CProgressCtrl*>(GetDlgItem(IDC_UPNPPROGRESS))->SetRange(0, 40);

	static_cast<CEdit*>(GetDlgItem(IDC_TCP))->SetLimitText(5);
	static_cast<CEdit*>(GetDlgItem(IDC_UDP))->SetLimitText(5);

	// disable changing ports to prevent harm
	SetDlgItemText(IDC_PORTINFO, GetResString(_T("PORTINFO")));
	SetDlgItemText(IDC_TESTFRAME, GetResString(_T("CONNECTIONTEST")));
	SetDlgItemText(IDC_TESTINFO, GetResString(_T("TESTINFO")));
	SetDlgItemText(IDC_STARTTEST, GetResString(_T("STARTTEST")));
	SetDlgItemText(IDC_UDPDISABLE, GetResString(_T("UDPDISABLED")));
	SetDlgItemText(IDC_UPNPSTART, GetResString(_T("UPNPSTART")));
	SetDlgItemText(IDC_UPNPSTATUS, EMPTY);
	InitWindowStyles(this);

	return TRUE;
}

void CPPgWiz1Ports::OnEnChangeUDPDisable()
{
	bool bDisabled = IsDlgButtonChecked(IDC_UDPDISABLE) != 0;
	GetDlgItem(IDC_UDP)->EnableWindow(!bDisabled);

	if (bDisabled) {
		m_lastudp = GetDlgItemInt(IDC_UDP, NULL, FALSE);
		SetDlgItemInt(IDC_UDP, 0);
	} else
		SetDlgItemInt(IDC_UDP, m_lastudp);

	if (m_pbUDPDisabled != NULL)
		*m_pbUDPDisabled = bDisabled;

	OnPortChange();
}


///////////////////////////////////////////////////////////////////////////////
// CPPgWiz1UlPrio dialog

class CPPgWiz1UlPrio : public CDlgPageWizard
{
	DECLARE_DYNAMIC(CPPgWiz1UlPrio)

	enum
	{
		IDD = IDD_WIZ1_ULDL_PRIO
	};

public:
	CPPgWiz1UlPrio();
	explicit CPPgWiz1UlPrio(UINT nIDTemplate, LPCTSTR pszCaption = NULL, LPCTSTR pszHeaderTitle = NULL, LPCTSTR pszHeaderSubTitle = NULL)
		: CDlgPageWizard(nIDTemplate, pszCaption, pszHeaderTitle, pszHeaderSubTitle), m_iUAP(1), m_iDAP(1)
	{
	}
	virtual BOOL OnInitDialog();

	int m_iUAP;
	int m_iDAP;

protected:
	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
};

IMPLEMENT_DYNAMIC(CPPgWiz1UlPrio, CDlgPageWizard)

BEGIN_MESSAGE_MAP(CPPgWiz1UlPrio, CDlgPageWizard)
END_MESSAGE_MAP()

CPPgWiz1UlPrio::CPPgWiz1UlPrio()
	: CDlgPageWizard(CPPgWiz1UlPrio::IDD)
	, m_iUAP(1)
	, m_iDAP(1)
{
}

void CPPgWiz1UlPrio::DoDataExchange(CDataExchange *pDX)
{
	CDlgPageWizard::DoDataExchange(pDX);
	DDX_Check(pDX, IDC_UAP, m_iUAP);
	DDX_Check(pDX, IDC_DAP, m_iDAP);
}

BOOL CPPgWiz1UlPrio::OnInitDialog()
{
	CDlgPageWizard::OnInitDialog();
	InitWindowStyles(this);
	SetDlgItemText(IDC_UAP, GetResString(_T("FIRSTAUTOUP")));
	SetDlgItemText(IDC_DAP, GetResString(_T("FIRSTAUTODOWN")));

	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////
// CPPgWiz1Upload dialog

class CPPgWiz1Upload : public CDlgPageWizard
{
	DECLARE_DYNAMIC(CPPgWiz1Upload)

	enum
	{
		IDD = IDD_WIZ1_UPLOAD
	};

public:
	CPPgWiz1Upload();
	explicit CPPgWiz1Upload(UINT nIDTemplate, LPCTSTR pszCaption = NULL, LPCTSTR pszHeaderTitle = NULL, LPCTSTR pszHeaderSubTitle = NULL)
		: CDlgPageWizard(nIDTemplate, pszCaption, pszHeaderTitle, pszHeaderSubTitle), m_iObfuscation()
	{
	}
	virtual BOOL OnInitDialog();

	int m_iObfuscation;

protected:
	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
};

IMPLEMENT_DYNAMIC(CPPgWiz1Upload, CDlgPageWizard)

BEGIN_MESSAGE_MAP(CPPgWiz1Upload, CDlgPageWizard)
END_MESSAGE_MAP()

CPPgWiz1Upload::CPPgWiz1Upload()
	: CDlgPageWizard(CPPgWiz1Upload::IDD)
	, m_iObfuscation()
{
}

void CPPgWiz1Upload::DoDataExchange(CDataExchange *pDX)
{
	CDlgPageWizard::DoDataExchange(pDX);
	DDX_Check(pDX, IDC_WIZZARDOBFUSCATION, m_iObfuscation);
}

BOOL CPPgWiz1Upload::OnInitDialog()
{
	CDlgPageWizard::OnInitDialog();
	InitWindowStyles(this);
	SetDlgItemText(IDC_WIZZARDOBFUSCATION, GetResString(_T("WIZZARDOBFUSCATION")));
	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////
// CPPgWiz1Server dialog

class CPPgWiz1Server : public CDlgPageWizard
{
	DECLARE_DYNAMIC(CPPgWiz1Server)

	enum
	{
		IDD = IDD_WIZ1_SERVER
	};

public:
	CPPgWiz1Server();
	explicit CPPgWiz1Server(UINT nIDTemplate, LPCTSTR pszCaption = NULL, LPCTSTR pszHeaderTitle = NULL, LPCTSTR pszHeaderSubTitle = NULL)
		: CDlgPageWizard(nIDTemplate, pszCaption, pszHeaderTitle, pszHeaderSubTitle)
		, m_iSafeServerConnect(), m_iKademlia(1), m_iED2K(1), m_pbUDPDisabled()
	{
	}
	virtual BOOL OnInitDialog();

	int m_iSafeServerConnect;
	int m_iKademlia;
	int m_iED2K;

	bool *m_pbUDPDisabled;

protected:
	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support
	virtual BOOL OnSetActive();

	DECLARE_MESSAGE_MAP()
};

IMPLEMENT_DYNAMIC(CPPgWiz1Server, CDlgPageWizard)

BEGIN_MESSAGE_MAP(CPPgWiz1Server, CDlgPageWizard)
END_MESSAGE_MAP()

CPPgWiz1Server::CPPgWiz1Server()
	: CDlgPageWizard(CPPgWiz1Server::IDD)
	, m_iSafeServerConnect()
	, m_iKademlia(1)
	, m_iED2K(1)
	, m_pbUDPDisabled()
{
}

void CPPgWiz1Server::DoDataExchange(CDataExchange *pDX)
{
	CDlgPageWizard::DoDataExchange(pDX);
	DDX_Check(pDX, IDC_SAFESERVERCONNECT, m_iSafeServerConnect);
	DDX_Check(pDX, IDC_WIZARD_NETWORK_KADEMLIA, m_iKademlia);
	DDX_Check(pDX, IDC_WIZARD_NETWORK_ED2K, m_iED2K);
}

BOOL CPPgWiz1Server::OnInitDialog()
{
	CDlgPageWizard::OnInitDialog();
	InitWindowStyles(this);
	SetDlgItemText(IDC_SAFESERVERCONNECT, GetResString(_T("FIRSTSAFECON")));
	SetDlgItemText(IDC_WIZARD_NETWORK, GetResString(_T("WIZARD_NETWORK")));
	SetDlgItemText(IDC_WIZARD_ED2K, GetResString(_T("WIZARD_ED2K")));
	return TRUE;
}

BOOL CPPgWiz1Server::OnSetActive()
{
	if (m_pbUDPDisabled != NULL) {
		m_iKademlia = *m_pbUDPDisabled ? 0 : m_iKademlia;
		CheckDlgButton(IDC_SHOWOVERHEAD, m_iKademlia);
		GetDlgItem(IDC_WIZARD_NETWORK_KADEMLIA)->EnableWindow(!*m_pbUDPDisabled);
	}
	return CDlgPageWizard::OnSetActive();
}


///////////////////////////////////////////////////////////////////////////////
// CPPgWiz1End dialog

class CPPgWiz1End : public CDlgPageWizard
{
	DECLARE_DYNAMIC(CPPgWiz1End)

	enum
	{
		IDD = IDD_WIZ1_END
	};

public:
	CPPgWiz1End();
	explicit CPPgWiz1End(UINT nIDTemplate, LPCTSTR pszCaption = NULL, LPCTSTR pszHeaderTitle = NULL, LPCTSTR pszHeaderSubTitle = NULL)
		: CDlgPageWizard(nIDTemplate, pszCaption, pszHeaderTitle, pszHeaderSubTitle)
	{
	}
	virtual BOOL OnInitDialog();

protected:
	CFont m_FontTitle;
	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
	afx_msg void	OnPaint();

	CFont      m_fntEndTitle;
	CFont      m_fntEndBody;
	CDarkStatic m_ctrlTitle;
	CDarkStatic m_ctrlActions;
	CDarkStatic m_ctrlBtnHint;
};

IMPLEMENT_DYNAMIC(CPPgWiz1End, CDlgPageWizard)

BEGIN_MESSAGE_MAP(CPPgWiz1End, CDlgPageWizard)
	ON_WM_PAINT()
END_MESSAGE_MAP()

CPPgWiz1End::CPPgWiz1End()
	: CDlgPageWizard(CPPgWiz1End::IDD)
{
}

void CPPgWiz1End::DoDataExchange(CDataExchange *pDX)
{
	CDlgPageWizard::DoDataExchange(pDX);
}

BOOL CPPgWiz1End::OnInitDialog()
{
	CFont fontVerdanaBold;
	CreatePointFont(fontVerdanaBold, 12 * 10, _T("Verdana Bold"));
	LOGFONT lf;
	fontVerdanaBold.GetLogFont(&lf);
	lf.lfWeight = FW_BOLD;
	m_FontTitle.CreateFontIndirect(&lf);

	CStatic *pStatic = static_cast<CStatic*>(GetDlgItem(IDC_WIZ1_TITLE));
	pStatic->SetFont(&m_FontTitle);

	CDlgPageWizard::OnInitDialog();
	InitWindowStyles(this);
	SetDlgItemText(IDC_WIZ1_TITLE, GetResString(_T("WIZ1_END_TITLE")));
	SetDlgItemText(IDC_WIZ1_ACTIONS, GetResString(_T("FIRSTCOMPLETE")));
	SetDlgItemText(IDC_WIZ1_BTN_HINT, GetResString(_T("WIZ1_END_BTN_HINT")));

	if (IsDarkModeEnabled()) {
		// subclass the three static controls
		m_ctrlTitle.SubclassDlgItem(IDC_WIZ1_TITLE, this);
		m_ctrlActions.SubclassDlgItem(IDC_WIZ1_ACTIONS, this);
		m_ctrlBtnHint.SubclassDlgItem(IDC_WIZ1_BTN_HINT, this);
	}

	return TRUE;
}


///////////////////////////////////////////////////////////////////////////////
// CPShtWiz1

class CPShtWiz1 : public CPropertySheetEx
{
	DECLARE_DYNAMIC(CPShtWiz1)

public:
	explicit CPShtWiz1(LPCTSTR strIDCaption, CWnd* pParentWnd = NULL, UINT iSelectPage = 0);

	// Constructor matching CPropertySheetEx(LPCTSTR, CWnd*, UINT, HBITMAP, HBITMAP, HBITMAP)
	CPShtWiz1(
		LPCTSTR		strIDCaption,
		CWnd*		pParentWnd,
		UINT		iSelectPage,
		HBITMAP		hbmWatermark,
		HPALETTE	hpalWatermark,
		HBITMAP		hbmHeader
	);

	virtual ~CPShtWiz1();

private:
	CFont m_fntHdrTitle;
	CFont m_fntHdrSubTitle;

protected:
	DECLARE_MESSAGE_MAP()
	afx_msg BOOL    OnInitDialog();
	afx_msg void	OnPaint();
};

IMPLEMENT_DYNAMIC(CPShtWiz1, CPropertySheetEx)

BEGIN_MESSAGE_MAP(CPShtWiz1, CPropertySheetEx)
	ON_WM_PAINT()
END_MESSAGE_MAP()

CPShtWiz1::CPShtWiz1(LPCTSTR strIDCaption, CWnd* pParentWnd, UINT iSelectPage)
	: CPropertySheetEx(strIDCaption, pParentWnd, iSelectPage)
{
}

CPShtWiz1::~CPShtWiz1()
{
}

CPShtWiz1::CPShtWiz1(LPCTSTR strIDCaption, CWnd* pParentWnd, UINT iSelectPage, HBITMAP hbmWatermark, HPALETTE hpalWatermark, HBITMAP hbmHeader
)
	: CPropertySheetEx(strIDCaption, pParentWnd, iSelectPage, hbmWatermark, hpalWatermark, hbmHeader)
{
}

BOOL CPShtWiz1::OnInitDialog()
{
	BOOL bRes = __super::OnInitDialog();

	if (IsDarkModeEnabled()) {
		// Give the sheet WS_CLIPCHILDREN
		LONG style = ::GetWindowLong(m_hWnd, GWL_STYLE);
		::SetWindowLong(m_hWnd, GWL_STYLE, style | WS_CLIPCHILDREN);

		// For each page, add WS_CLIPCHILDREN and WS_EX_TRANSPARENT
		for (int i = 0; i < GetPageCount(); ++i) {
			CPropertyPage* pPage = GetPage(i);
			HWND hPage = pPage->GetSafeHwnd();
			if (hPage) {
				LONG pStyle = ::GetWindowLong(hPage, GWL_STYLE);
				::SetWindowLong(hPage, GWL_STYLE, pStyle | WS_CLIPCHILDREN);

				LONG exStyle = ::GetWindowLong(hPage, GWL_EXSTYLE);
				::SetWindowLong(hPage, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT);
			}
		}

		// create a bold ~10pt header‐title font
		LOGFONT lf = {};
		CFont* pDlgFont = GetFont();
		pDlgFont->GetLogFont(&lf);
		lf.lfWeight = FW_BOLD;
		lf.lfHeight = -MulDiv(10, GetDeviceCaps(::GetDC(NULL), LOGPIXELSY), 72);
		m_fntHdrTitle.CreateFontIndirect(&lf);

		// create a regular ~8pt header‐subtitle font
		lf.lfWeight = FW_NORMAL;
		lf.lfHeight = -MulDiv(8, GetDeviceCaps(::GetDC(NULL), LOGPIXELSY), 72);
		m_fntHdrSubTitle.CreateFontIndirect(&lf);

		ApplyTheme(m_hWnd);
	}

	return bRes;
}

// Sheet level paint: fill background + draw header/watermark
void CPShtWiz1::OnPaint()
{
	if (IsDarkModeEnabled()) {
		CPaintDC dc(this);
		CRect rc;
		GetClientRect(&rc);

		// 1) fill the entire wizard background with the dark brush
		dc.FillRect(rc, &CDarkMode::m_brDefault);

		int idx = GetActiveIndex();
		CPropertyPageEx *pPage = static_cast<CPropertyPageEx*>(GetPage(idx));
		const bool bHideHeader = (pPage != NULL && (pPage->m_psp.dwFlags & PSP_HIDEHEADER) != 0);
		const bool bIsExtRes = (pPage != NULL && IS_INTRESOURCE(pPage->m_psp.pszTemplate) && (UINT_PTR)pPage->m_psp.pszTemplate == IDD_WIZ1_EXTRES);
		BITMAP bmW = {}, bmH = {};
		int headerW = 0, headerH = 0;

		// 2) draw the big watermark on pages without header
		if (bHideHeader && m_psh.hbmWatermark && !bIsExtRes) {
			::GetObject(m_psh.hbmWatermark, sizeof bmW, &bmW);
			CDC memW; memW.CreateCompatibleDC(&dc);
			HGDIOBJ oldW = memW.SelectObject(m_psh.hbmWatermark);
			dc.BitBlt(0, 0, bmW.bmWidth, bmW.bmHeight, &memW, 0, 0, SRCCOPY);
			memW.SelectObject(oldW);
		}

		// 3) draw the small header image on pages with header
		if (!bHideHeader && m_psh.hbmHeader) {
			::GetObject(m_psh.hbmHeader, sizeof bmH, &bmH);
			headerW = bmH.bmWidth;
			headerH = bmH.bmHeight;

			CDC memH; memH.CreateCompatibleDC(&dc);
			HGDIOBJ oldH = memH.SelectObject(m_psh.hbmHeader);
			dc.BitBlt(rc.right - headerW, 0, headerW, headerH, &memH, 0, 0, SRCCOPY);
			memH.SelectObject(oldH);

			// 4) draw header title (first line), indented 16px
			LPCTSTR title = pPage->m_psp.pszHeaderTitle;
			if (title && *title) {
				dc.SetBkMode(TRANSPARENT);
				dc.SetTextColor(GetCustomSysColor(COLOR_WINDOWTEXT));
				dc.SelectObject(&m_fntHdrTitle);

				CRect titleRect(
					16,							// left indent
					4,							// top
					rc.right - headerW - 16,	// right edge
					headerH / 2 - 2				// bottom of first half
				);
				dc.DrawText(title, -1, titleRect,
					DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			}

			// 5) draw header subtitle (second line), indented 32px
			LPCTSTR sub = pPage->m_psp.pszHeaderSubTitle;
			if (sub && *sub) {
				dc.SetBkMode(TRANSPARENT);
				dc.SetTextColor(GetCustomSysColor(COLOR_WINDOWTEXT));
				dc.SelectObject(&m_fntHdrSubTitle);

				CRect subRect(
					32,							// left indent larger
					headerH / 2 + 2,			// just below first half
					rc.right - headerW - 16,	// same right edge as title
					headerH - 4					// bottom minus small pad
				);
				dc.DrawText(sub, -1, subRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			}
		}
	}

	// 6) let the base class draw the page controls on top
	__super::OnPaint();
}

BOOL FirstTimeWizard()
{
	const CString &sWiz1(GetResString(_T("WIZ1")));
	CEnBitmap bmWatermark;
	VERIFY(bmWatermark.LoadImage(IDR_WIZ1_WATERMARK, _T("PNG"), NULL, GetCustomSysColor(COLOR_WINDOW)));
	CEnBitmap bmHeader;
	VERIFY(bmHeader.LoadImage(IDR_WIZ1_HEADER, _T("PNG"), NULL, GetCustomSysColor(COLOR_WINDOW)));
	
	// create the wizard sheet with caption resource ID
	CPShtWiz1 sheet(_T("WIZ1"), nullptr, 0, bmWatermark, NULL, bmHeader);

	sheet.m_psh.dwFlags |= PSH_WIZARD;
	sheet.m_psh.dwFlags |= PSH_WIZARDHASFINISH;
	sheet.m_psh.dwFlags |= PSH_WIZARD97;

	CPPgWiz1ExtRes page1(IDD_WIZ1_EXTRES, sWiz1);
	page1.m_psp.dwFlags |= PSP_HIDEHEADER;
	sheet.AddPage(&page1);

	CPPgWiz1Welcome	page2(IDD_WIZ1_WELCOME, sWiz1);
	page2.m_psp.dwFlags |= PSP_HIDEHEADER;
	sheet.AddPage(&page2);

	CPPgWiz1General page3(IDD_WIZ1_GENERAL, sWiz1, GetResString(_T("PW_GENERAL")), GetResString(_T("QL_USERNAME")));
	sheet.AddPage(&page3);

	CPPgWiz1Ports page4(IDD_WIZ1_PORTS, sWiz1, GetResString(_T("PORTSCON")), GetResString(_T("CONNECTION")));
	sheet.AddPage(&page4);


	CString sPage4(GetResString(_T("PW_CON_DOWNLBL")));
	sPage4.AppendFormat(_T(" / %s"), (LPCTSTR)GetResString(_T("PW_CON_UPLBL")));
	CPPgWiz1UlPrio page5(IDD_WIZ1_ULDL_PRIO, sWiz1, sPage4, GetResString(_T("PRIORITY")));
	sheet.AddPage(&page5);

	CPPgWiz1Upload page6(IDD_WIZ1_UPLOAD, sWiz1, GetResString(_T("SECURITY")), GetResString(_T("OBFUSCATION")));
	sheet.AddPage(&page6);

	CPPgWiz1Server page7(IDD_WIZ1_SERVER, sWiz1, GetResString(_T("PW_SERVER")), GetResString(_T("NETWORK")));
	sheet.AddPage(&page7);

	CPPgWiz1End page8(IDD_WIZ1_END, sWiz1);
	page8.m_psp.dwFlags |= PSP_HIDEHEADER;
	sheet.AddPage(&page8);

	page3.m_strNick = thePrefs.GetUserNick();
	if (page3.m_strNick.IsEmpty())
		page3.m_strNick = DEFAULT_NICK;
	page3.m_iAutoConnectAtStart = 0;
	page4.m_uTCP = thePrefs.GetPort();
	page4.m_uUDP = thePrefs.GetUDPPort();
	page5.m_iDAP = 1;
	page5.m_iUAP = 1;
	page6.m_iObfuscation = static_cast<int>(thePrefs.IsCryptLayerEnabled()); //was Requested()
	page7.m_iSafeServerConnect = 0;
	page7.m_iKademlia = 1;
	page7.m_iED2K = 1;

	bool bUDPDisabled = thePrefs.GetUDPPort() == 0;
	page4.m_pbUDPDisabled = &bUDPDisabled;
	page7.m_pbUDPDisabled = &bUDPDisabled;

	uint16 oldtcpport = thePrefs.GetPort();
	uint16 oldudpport = thePrefs.GetUDPPort();

	if (sheet.DoModal() == IDCANCEL) {

		// restore port settings?
		thePrefs.port = oldtcpport;
		thePrefs.udpport = oldudpport;
		theApp.listensocket->Rebind();
		theApp.clientudp->Rebind();

		return FALSE;
	}

	page3.m_strNick.Trim();
	if (page3.m_strNick.IsEmpty())
		page3.m_strNick = DEFAULT_NICK;

	thePrefs.SetUserNick(page3.m_strNick);
	thePrefs.SetAutoConnect(page3.m_iAutoConnectAtStart != 0);
	thePrefs.SetAutoStart(page3.m_iAutoStart != 0);
	SetAutoStart(thePrefs.GetAutoStart());

	thePrefs.SetNewAutoDown(page5.m_iDAP != 0);
	thePrefs.SetNewAutoUp(page5.m_iUAP != 0);
	thePrefs.m_bCryptLayerRequested = page6.m_iObfuscation != 0;
	if (page6.m_iObfuscation != 0)
		thePrefs.m_bCryptLayerSupported = true;
	thePrefs.SetSafeServerConnectEnabled(page7.m_iSafeServerConnect != 0);
	thePrefs.SetNetworkKademlia(page7.m_iKademlia != 0);
	thePrefs.SetNetworkED2K(page7.m_iED2K != 0);

	// set ports
	thePrefs.port = (uint16)page4.m_uTCP;
	thePrefs.udpport = (uint16)page4.m_uUDP;
	ASSERT(thePrefs.port != 0 && thePrefs.udpport != 0 + 10);
	if (thePrefs.port == 0)
		thePrefs.port = thePrefs.GetRandomTCPPort();
	if (thePrefs.udpport == 0 + 10)
		thePrefs.udpport = thePrefs.GetRandomUDPPort();
	if ((thePrefs.port != theApp.listensocket->GetConnectedPort()) || (thePrefs.udpport != theApp.clientudp->GetConnectedPort()))
		if (!theApp.IsPortchangeAllowed())
			LocMessageBox(_T("NOPORTCHANGEPOSSIBLE"), MB_OK, 0);
		else {
			theApp.listensocket->Rebind();
			theApp.clientudp->Rebind();
		}

	return TRUE;
}

void CPPgWiz1Welcome::OnPaint()
{
	if (IsDarkModeEnabled()) {
		CPaintDC dc(this);
		CRect rc;
		GetClientRect(&rc);

		// fill entire page client with dark background
		dc.FillRect(rc, &CDarkMode::m_brDefault);

		// draw the left watermark from the parent sheet
		CPShtWiz1* pSheet = static_cast<CPShtWiz1*>(GetParent());
		if (pSheet && pSheet->m_psh.hbmWatermark) {
			BITMAP bmW = {};
			::GetObject(pSheet->m_psh.hbmWatermark, sizeof bmW, &bmW);
			CDC memW; memW.CreateCompatibleDC(&dc);
			HGDIOBJ oldW = memW.SelectObject(pSheet->m_psh.hbmWatermark);
			dc.BitBlt(0, 0, bmW.bmWidth, bmW.bmHeight, &memW, 0, 0, SRCCOPY);
			memW.SelectObject(oldW);
		}

	}

	// let the base class draw the page controls on top
	__super::OnPaint();
}

void CPPgWiz1End::OnPaint()
{
	if (IsDarkModeEnabled()) {
		CPaintDC dc(this);
		CRect rc;
		GetClientRect(&rc);

		// fill entire page client with dark background
		dc.FillRect(rc, &CDarkMode::m_brDefault);

		// draw the left watermark from the parent sheet
		CPShtWiz1* pSheet = static_cast<CPShtWiz1*>(GetParent());
		if (pSheet && pSheet->m_psh.hbmWatermark) {
			BITMAP bmW = {};
			::GetObject(pSheet->m_psh.hbmWatermark, sizeof bmW, &bmW);
			CDC memW; memW.CreateCompatibleDC(&dc);
			HGDIOBJ oldW = memW.SelectObject(pSheet->m_psh.hbmWatermark);
			dc.BitBlt(0, 0, bmW.bmWidth, bmW.bmHeight, &memW, 0, 0, SRCCOPY);
			memW.SelectObject(oldW);
		}
	}

	// let the base class draw the page controls on top
	__super::OnPaint();
}
