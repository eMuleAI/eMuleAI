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
#include "otherfunctions.h"
#include "Preferences.h"
#include "AddFriend.h"
#include "FriendList.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


// CAddFriend dialog

IMPLEMENT_DYNAMIC(CAddFriend, CDialog)

BEGIN_MESSAGE_MAP(CAddFriend, CDialog)
	ON_BN_CLICKED(IDC_ADD, OnAddBtn)
END_MESSAGE_MAP()

CAddFriend::CAddFriend()
	: CDialog(CAddFriend::IDD)
	, m_pShowFriend()
	, m_icoWnd()
{
}

CAddFriend::~CAddFriend()
{
	if (m_icoWnd)
		VERIFY(::DestroyIcon(m_icoWnd));
}

void CAddFriend::DoDataExchange(CDataExchange *pDX)
{
	CDialog::DoDataExchange(pDX);
}

BOOL CAddFriend::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);
	Localize();
	if (m_pShowFriend) {
		SetIcon(m_icoWnd = theApp.LoadIcon(_T("ClientDetails")), FALSE);
		SendDlgItemMessage(IDC_IP, EM_SETREADONLY, TRUE);
		SendDlgItemMessage(IDC_PORT, EM_SETREADONLY, TRUE);
		SendDlgItemMessage(IDC_USERNAME, EM_SETREADONLY, TRUE);
		SetDlgItemText(IDC_IP, ipstr(m_pShowFriend->m_LastUsedIP));
		SetDlgItemInt(IDC_PORT, m_pShowFriend->m_nLastUsedPort, FALSE);
		SetDlgItemText(IDC_USERNAME, m_pShowFriend->m_strName);
		SetDlgItemText(IDC_USERHASH, (m_pShowFriend->HasUserhash() ? md4str(m_pShowFriend->m_abyUserhash) : EMPTY));

		if (m_pShowFriend->m_dwLastSeen) {
			CTime t((time_t)m_pShowFriend->m_dwLastSeen);
			SetDlgItemText(IDC_EDIT2, t.Format(thePrefs.GetDateTimeFormat()));
		}
		SetDlgItemText(IDC_AFKADID, GetResString(m_pShowFriend->HasKadID() ? _T("KNOWN") : _T("UNKNOWN")));

		GetDlgItem(IDC_ADD)->ShowWindow(SW_HIDE);
	} else {
		SetIcon(m_icoWnd = theApp.LoadIcon(_T("AddFriend")), FALSE);
		static_cast<CEdit*>(GetDlgItem(IDC_USERNAME))->SetLimitText(thePrefs.GetMaxUserNickLength());
		SetDlgItemText(IDC_USERHASH, EMPTY);
	}
	return TRUE;
}

void CAddFriend::Localize()
{
	SetWindowText(GetResString(m_pShowFriend ? _T("DETAILS") : _T("ADDAFRIEND")));
	SetDlgItemText(IDC_INFO1, GetResString(_T("PAF_REQINFO")));
	SetDlgItemText(IDC_INFO2, GetResString(_T("PAF_MOREINFO")));

	SetDlgItemText(IDC_ADD, GetResString(_T("ADD")));
	SetDlgItemText(IDCANCEL, GetResString(m_pShowFriend ? _T("FD_CLOSE") : _T("CANCEL")));

	SetDlgItemText(IDC_STATIC31, GetResString(_T("CD_UNAME")));
	SetDlgItemText(IDC_STATIC32, GetResString(_T("CD_UHASH")));
	SetDlgItemText(IDC_STATIC34, (m_pShowFriend ? GetResString(_T("USERID")) + _T(':') : GetResString(_T("CD_UIP"))));
	SetDlgItemText(IDC_STATIC35, GetResString(_T("PORT")) + _T(':'));
	SetDlgItemText(IDC_LAST_SEEN_LABEL, GetResString(_T("LASTSEEN")) + _T(':'));
	SetDlgItemText(IDC_AFKADIDLABEL, GetResString(_T("KADID")) + _T(':'));
}

void CAddFriend::OnAddBtn()
{
	if (!m_pShowFriend) {
		CString strBuff;
		uint32 ip;
		GetDlgItemText(IDC_IP, strBuff);
		UINT u1, u2, u3, u4, uPort;
		if (_stscanf(strBuff, _T("%u.%u.%u.%u:%u"), &u1, &u2, &u3, &u4, &uPort) != 5 || u1 > 255 || u2 > 255 || u3 > 255 || u4 > 255 || uPort > 65535) {
			if (_stscanf(strBuff, _T("%u.%u.%u.%u"), &u1, &u2, &u3, &u4) != 4 || u1 > 255 || u2 > 255 || u3 > 255 || u4 > 255) {
				LocMessageBox(_T("ERR_NOVALIDFRIENDINFO"), MB_OK, 0);
				GetDlgItem(IDC_IP)->SetFocus();
				return;
			}
			uPort = 0;
		}
		in_addr iaFriend;
		iaFriend.S_un.S_un_b.s_b1 = (UCHAR)u1;
		iaFriend.S_un.S_un_b.s_b2 = (UCHAR)u2;
		iaFriend.S_un.S_un_b.s_b3 = (UCHAR)u3;
		iaFriend.S_un.S_un_b.s_b4 = (UCHAR)u4;
		ip = iaFriend.s_addr;

		if (uPort == 0) {
			GetDlgItemText(IDC_PORT, strBuff);
			if (_stscanf(strBuff, _T("%u"), &uPort) != 1) {
				LocMessageBox(_T("ERR_NOVALIDFRIENDINFO"), MB_OK, 0);
				GetDlgItem(IDC_PORT)->SetFocus();
				return;
			}
		}

		CString strUserName;
		GetDlgItemText(IDC_USERNAME, strUserName);
		if (strUserName.Trim().GetLength() > thePrefs.GetMaxUserNickLength())
			strUserName.Truncate(thePrefs.GetMaxUserNickLength());

		// why did we offer an edit control for entering the userhash but did not store it?
		;

		if (!theApp.friendlist->AddFriend(NULL, 0, CAddress(ip, false), (uint16)uPort, 0, strUserName, 0)) {
			LocMessageBox(_T("WRN_FRIENDDUPLIPPORT"), MB_OK, 0);
			GetDlgItem(IDC_IP)->SetFocus();
			return;
		}
	} else {
		// No "update" friend's data for now -- too much work to synchronize/update all
		// possible available related data in the client list...
	}

	OnCancel();
}