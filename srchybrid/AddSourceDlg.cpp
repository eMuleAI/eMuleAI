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
#include "AddSourceDlg.h"
#include "PartFile.h"
#include "UpDownClient.h"
#include "DownloadQueue.h"
#include <wininet.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


// CAddSourceDlg dialog

IMPLEMENT_DYNAMIC(CAddSourceDlg, CDialog)

BEGIN_MESSAGE_MAP(CAddSourceDlg, CDialog)
	ON_BN_CLICKED(IDC_BUTTON1, OnBnClickedButton1)
END_MESSAGE_MAP()

CAddSourceDlg::CAddSourceDlg(CWnd *pParent /*= NULL*/)
	: CDialog(CAddSourceDlg::IDD, pParent)
	, m_pFile()
{
}

void CAddSourceDlg::DoDataExchange(CDataExchange *pDX)
{
	CDialog::DoDataExchange(pDX);
}

void CAddSourceDlg::SetFile(CPartFile *pFile)
{
	m_pFile = pFile;
}

BOOL CAddSourceDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);

	if (m_pFile)
		SetWindowText(m_pFile->GetFileName());

	// localize
	SetDlgItemText(IDC_BUTTON1, GetResString(_T("ADD")));
	SetDlgItemText(IDCANCEL, GetResString(_T("EXIT")));
	SetDlgItemText(IDC_UIP, GetResString(_T("IP")) + _T(':'));
	SetDlgItemText(IDC_PORT, GetResString(_T("PORT")) + _T(':'));
	SetStatusText(_T(""));

	return TRUE; // Let MFC assign the initial focus.
}

void CAddSourceDlg::OnBnClickedButton1()
{
	TryAddSource();
}

void CAddSourceDlg::SetStatusText(LPCTSTR pszText)
{
	SetDlgItemText(IDC_ADDSOURCE_STATUS, pszText != NULL ? pszText : _T(""));
}

CString CAddSourceDlg::BuildSourceDisplay(const CString& sip, uint16 port) const
{
	if (sip.Find(_T(':')) >= 0)
		return sip;

	CString strDisplay(sip);
	if (port != 0) {
		CString strPort;
		strPort.Format(_T(":%u"), port);
		strDisplay += strPort;
	}
	return strDisplay;
}

bool CAddSourceDlg::TryAddSource()
{
	if (!m_pFile)
	{
		SetStatusText(GetResString(_T("ERROR")));
		return false;
	}

	CString sip;
	GetDlgItemText(IDC_EDIT2, sip);
	sip.Trim();
	if (sip.IsEmpty()) {
		SetStatusText(GetResString(_T("ERR_NOVALIDFRIENDINFO")));
		return false;
	}

	// if the port is specified with the IP, ignore any possible specified port in the port control
	uint16 port = 0;
	CString strOriginalSource(sip);
	int iColon = sip.Find(_T(':'));
	if (iColon >= 0) {
		port = (uint16)_tstoi(CPTR(sip, iColon + 1));
		sip.Truncate(iColon);
	} else {
		BOOL bTranslated;
		port = (uint16)GetDlgItemInt(IDC_EDIT3, &bTranslated, FALSE);
		if (!bTranslated) {
			SetStatusText(GetResString(_T("ERR_BADPORT")));
			return false;
		}
	}

	if (port == 0) {
		SetStatusText(GetResString(_T("ERR_BADPORT")));
		return false;
	}

	bool bAdded = false;
	CAddress IP(sip, false);
	if (IP.GetType() == CAddress::IPv6) {
		CUpDownClient* toadd = new CUpDownClient(m_pFile, port, UINT_MAX, 0, 0, false, IP);
		toadd->SetSourceFrom(SF_PASSIVE);
		bAdded = theApp.downloadqueue->CheckAndAddSource(m_pFile, toadd, SF_PASSIVE, false);
	} else {
		uint32 ip = IP.ToUInt32(false);
		if (ip != INADDR_NONE && IsGoodIPPort(ip, port)) {
			CUpDownClient *toadd = new CUpDownClient(m_pFile, port, ntohl(ip), 0, 0);
			toadd->SetSourceFrom(SF_PASSIVE);
			bAdded = theApp.downloadqueue->CheckAndAddSource(m_pFile, toadd, SF_PASSIVE, false);
		} else {
			SetStatusText(GetResString(_T("ERR_NOVALIDFRIENDINFO")));
			return false;
		}
	}

	CString strStatus;
	const CString strSource(BuildSourceDisplay(strOriginalSource, port));
	const CString strStatusFormat = GetResString(bAdded ? _T("ADDSOURCE_STATUS_SOURCE_ADDED") : _T("ADDSOURCE_STATUS_SOURCE_ADD_FAILED"));
	strStatus.Format(strStatusFormat, (LPCTSTR)strSource);
	SetStatusText(strStatus);
	return bAdded;
}
