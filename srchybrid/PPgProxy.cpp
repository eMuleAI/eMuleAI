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
#include "PPGProxy.h"
#include "opcodes.h"
#include "OtherFunctions.h"
#include "HelpIDs.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


IMPLEMENT_DYNAMIC(CPPgProxy, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgProxy, CPropertyPage)
	ON_BN_CLICKED(IDC_ENABLEPROXY, OnBnClickedEnableProxy)
	ON_BN_CLICKED(IDC_ENABLEAUTH, OnBnClickedEnableAuthentication)
	ON_CBN_SELCHANGE(IDC_PROXYTYPE, OnCbnSelChangeProxyType)
	ON_EN_CHANGE(IDC_PROXYNAME, OnEnChangeProxyName)
	ON_EN_CHANGE(IDC_PROXYPORT, OnEnChangeProxyPort)
	ON_EN_CHANGE(IDC_USERNAME_A, OnEnChangeUserName)
	ON_EN_CHANGE(IDC_PASSWORD, OnEnChangePassword)
	ON_WM_HELPINFO()
END_MESSAGE_MAP()

CPPgProxy::CPPgProxy()
	: CPropertyPage(CPPgProxy::IDD)
	, proxy(thePrefs.GetProxySettings())
{
}

void CPPgProxy::DoDataExchange(CDataExchange *pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_PROXYTYPE, m_ctlProxyType);
}

BOOL CPPgProxy::OnInitDialog()
{
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);
	static_cast<CEdit*>(GetDlgItem(IDC_PROXYPORT))->SetLimitText(5);

	FillComboBox();
	LoadSettings();
	Localize();

	return TRUE;  // return TRUE unless you set the focus to the control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

void CPPgProxy::FillComboBox() {
	m_ctlProxyType.ResetContent();
	m_ctlProxyType.AddItem(GetResString(_T("PROXY_NOPROXY")), 0);
	m_ctlProxyType.AddItem(GetResString(_T("PROXY_SOCKS4")), 1);
	m_ctlProxyType.AddItem(GetResString(_T("PROXY_SOCKS4A")), 2);
	m_ctlProxyType.AddItem(GetResString(_T("PROXY_SOCKS5")), 3);
	m_ctlProxyType.AddItem(GetResString(_T("PROXY_HTTP10")), 4);
	m_ctlProxyType.AddItem(GetResString(_T("PROXY_HTTP11")), 5);
	m_ctlProxyType.SetCurSel(0);
}

BOOL CPPgProxy::OnApply()
{
	proxy.type = (uint16)static_cast<CComboBox*>(GetDlgItem(IDC_PROXYTYPE))->GetCurSel();

	CString str;
	if (GetDlgItemText(IDC_PROXYNAME, str)) {
		proxy.bUseProxy = IsDlgButtonChecked(IDC_ENABLEPROXY) != 0;
		int iColon = str.Find(':');
		if (iColon >= 0) {
			SetDlgItemText(IDC_PROXYPORT, CPTR(str, iColon + 1));
			str.Truncate(iColon);
		}
	} else
		proxy.bUseProxy = false;
	proxy.host = str;

	proxy.port = (uint16)GetDlgItemInt(IDC_PROXYPORT, NULL, FALSE);
	if (!proxy.port)
		proxy.port = 1080;

	if (GetDlgItemText(IDC_USERNAME_A, str)) {
		proxy.user = str;
		proxy.bEnablePassword = IsDlgButtonChecked(IDC_ENABLEAUTH) != 0;
	} else {
		proxy.user.Empty();
		proxy.bEnablePassword = false;
	}

	if (GetDlgItemText(IDC_PASSWORD, str))
		proxy.password = str;
	else {
		proxy.password.Empty();
		proxy.bEnablePassword = false;
	}

	thePrefs.SetProxySettings(proxy);
	LoadSettings();
	return TRUE;
}

void CPPgProxy::OnBnClickedEnableProxy()
{
	SetModified(TRUE);

	bool bEnable = IsDlgButtonChecked(IDC_ENABLEPROXY) != 0;
	GetDlgItem(IDC_PROXYTYPE)->EnableWindow(bEnable);
	GetDlgItem(IDC_PROXYNAME)->EnableWindow(bEnable);
	GetDlgItem(IDC_PROXYPORT)->EnableWindow(bEnable);

	OnCbnSelChangeProxyType();
}

void CPPgProxy::OnBnClickedEnableAuthentication()
{
	SetModified(TRUE);
	bool bEnable = IsDlgButtonChecked(IDC_ENABLEAUTH) != 0;
	GetDlgItem(IDC_USERNAME_A)->EnableWindow(bEnable);
	GetDlgItem(IDC_PASSWORD)->EnableWindow(bEnable);
}

void CPPgProxy::OnCbnSelChangeProxyType()
{
	SetModified(TRUE);

	BOOL bEnable;
	switch (static_cast<CComboBox*>(GetDlgItem(IDC_PROXYTYPE))->GetCurSel()) {
	case PROXYTYPE_SOCKS5:
	case PROXYTYPE_HTTP10:
	case PROXYTYPE_HTTP11:
		bEnable = TRUE;
		break;
	default:
		CheckDlgButton(IDC_ENABLEAUTH, BST_UNCHECKED);
		bEnable = FALSE;
	}
	GetDlgItem(IDC_ENABLEAUTH)->EnableWindow(bEnable);
	OnBnClickedEnableAuthentication();
}

void CPPgProxy::LoadSettings()
{
	CheckDlgButton(IDC_ENABLEPROXY, proxy.bUseProxy ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_ENABLEAUTH, proxy.bEnablePassword ? BST_CHECKED : BST_UNCHECKED);
	static_cast<CComboBox*>(GetDlgItem(IDC_PROXYTYPE))->SetCurSel(proxy.type);
	SetDlgItemText(IDC_PROXYNAME, proxy.host);
	SetDlgItemInt(IDC_PROXYPORT, proxy.port);
	SetDlgItemText(IDC_USERNAME_A, proxy.user);
	SetDlgItemText(IDC_PASSWORD, proxy.password);
	OnBnClickedEnableProxy();
}

void CPPgProxy::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(_T("PW_PROXY")));
		SetDlgItemText(IDC_ENABLEPROXY, GetResString(_T("PROXY_ENABLE")));
		SetDlgItemText(IDC_PROXYTYPE_LBL, GetResString(_T("PROXY_TYPE")));
		SetDlgItemText(IDC_PROXYNAME_LBL, GetResString(_T("PROXY_HOST")));
		SetDlgItemText(IDC_PROXYPORT_LBL, GetResString(_T("PROXY_PORT")));
		SetDlgItemText(IDC_ENABLEAUTH, GetResString(_T("PROXY_AUTH")));
		SetDlgItemText(IDC_USERNAME_LBL, GetResString(_T("CD_UNAME")));
		SetDlgItemText(IDC_PASSWORD_LBL, GetResString(_T("WS_PASS")) + _T(':'));
		SetDlgItemText(IDC_AUTH_LBL, GetResString(_T("AUTH")));
		SetDlgItemText(IDC_AUTH_LBL2, GetResString(_T("PW_GENERAL")));
	}
}

void CPPgProxy::OnHelp()
{
	theApp.ShowHelp(eMule_FAQ_Preferences_Proxy);
}

BOOL CPPgProxy::OnCommand(WPARAM wParam, LPARAM lParam)
{
	return (wParam == ID_HELP) ? OnHelpInfo(NULL) : __super::OnCommand(wParam, lParam);
}

BOOL CPPgProxy::OnHelpInfo(HELPINFO*)
{
	OnHelp();
	return TRUE;
}