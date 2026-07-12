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
#include "DirectDownloadDlg.h"
#include "ED2KLink.h"
#include "emuleDlg.h"
#include "DownloadQueue.h"
#include "Preferences.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define	PREF_INI_SECTION	_T("DirectDownloadDlg")

namespace
{
	const UINT kDirectDownloadEditLimit = 128U * 1024U * 1024U;
	const int kDirectDownloadNormalizeEditLimit = 64 * 1024;
}

IMPLEMENT_DYNAMIC(CDirectDownloadDlg, CDialog)

BEGIN_MESSAGE_MAP(CDirectDownloadDlg, CResizableDialog)
	ON_EN_KILLFOCUS(IDC_ELINK, OnEnKillfocusElink)
	ON_EN_UPDATE(IDC_ELINK, OnEnUpdateElink)
END_MESSAGE_MAP()

CDirectDownloadDlg::CDirectDownloadDlg(CWnd *pParent /*=NULL*/)
	: CResizableDialog(CDirectDownloadDlg::IDD, pParent)
	, m_icoWnd()
{
}

CDirectDownloadDlg::~CDirectDownloadDlg()
{
	if (m_icoWnd)
		VERIFY(::DestroyIcon(m_icoWnd));
}

void CDirectDownloadDlg::DoDataExchange(CDataExchange *pDX)
{
	CResizableDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_DDOWN_FRM, m_ctrlDirectDlFrm);
	DDX_Control(pDX, IDC_CATS, m_cattabs);
}

void CDirectDownloadDlg::UpdateControls()
{
	GetDlgItem(IDOK)->EnableWindow(GetDlgItem(IDC_ELINK)->GetWindowTextLength() > 0);
}

void CDirectDownloadDlg::OnEnUpdateElink()
{
	UpdateControls();
}

void CDirectDownloadDlg::OnEnKillfocusElink()
{
	CWnd *pLinks = GetDlgItem(IDC_ELINK);
	if (pLinks == NULL)
		return;

	const int iTextLen = pLinks->GetWindowTextLength();
	if (iTextLen <= 0 || iTextLen > kDirectDownloadNormalizeEditLimit)
		return;

	CString strLinks;
	pLinks->GetWindowText(strLinks);
	if (!strLinks.IsEmpty()) {
		CString strNormalized(strLinks);
		strNormalized.Replace(_T("\n"), _T("\r\n"));
		strNormalized.Replace(_T("\r\r"), _T("\r"));
		if (strNormalized != strLinks)
			pLinks->SetWindowText(strNormalized);
	}
}

void CDirectDownloadDlg::OnOK()
{
	CString strLinks;
	GetDlgItemText(IDC_ELINK, strLinks);
	strLinks.Trim();

	if (!strLinks.IsEmpty()) {
		const int iCat = thePrefs.GetCatCount() ? m_cattabs.GetCurSel() : 0;
		theApp.AddEd2kLinksToDownload(strLinks, iCat);
	}

	CResizableDialog::OnOK();
}

BOOL CDirectDownloadDlg::OnInitDialog()
{
	CResizableDialog::OnInitDialog();
	InitWindowStyles(this);
	SendDlgItemMessage(IDC_ELINK, EM_SETLIMITTEXT, kDirectDownloadEditLimit, 0);
	SetIcon(m_icoWnd = theApp.LoadIcon(_T("PasteLink")), FALSE);

	AddOrReplaceAnchor(this, IDC_DDOWN_FRM, TOP_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_ELINK, TOP_LEFT, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDCANCEL, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDOK, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_CATLABEL, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, IDC_CATS, BOTTOM_LEFT, BOTTOM_RIGHT);

	EnableSaveRestore(PREF_INI_SECTION);

	SetWindowText(GetResString(_T("SW_DIRECTDOWNLOAD")));
	m_ctrlDirectDlFrm.SetIcon(_T("Download"));
	m_ctrlDirectDlFrm.SetWindowText(GetResString(_T("SW_DIRECTDOWNLOAD")));
	SetDlgItemText(IDOK, GetResString(_T("DOWNLOAD")));
	SetDlgItemText(IDC_FSTATIC2, GetResString(_T("SW_LINK")));
	SetDlgItemText(IDC_CATLABEL, GetResString(_T("CAT")) + _T(':'));

	SetDlgItemText(IDOK, GetResString(_T("DOWNLOAD")));
	SetDlgItemText(IDCANCEL, GetResString(_T("CANCEL")));

	if (thePrefs.GetCatCount() == 0) {
		GetDlgItem(IDC_CATLABEL)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_CATS)->ShowWindow(SW_HIDE);
	} else {
		UpdateCatTabs();
		if (theApp.m_fontSymbol.m_hObject) {
			GetDlgItem(IDC_CATLABEL)->SetFont(&theApp.m_fontSymbol);
			SetDlgItemText(IDC_CATLABEL, (GetExStyle() & WS_EX_LAYOUTRTL) ? _T("3") : _T("4")); // show a right-arrow
		}

	}

	UpdateControls();

	return TRUE;  // return TRUE unless you set the focus to the control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

void CDirectDownloadDlg::UpdateCatTabs()
{
	int oldsel = m_cattabs.GetCurSel();
	m_cattabs.DeleteAllItems();
	for (INT_PTR i = thePrefs.GetCatCount(); --i >= 0;) {
		CString label(thePrefs.GetCategoryDisplayTitle(i));
		DupAmpersand(label);
		m_cattabs.InsertItem(0, label);
	}

	m_cattabs.SetCurSel(oldsel >= 0 && oldsel < m_cattabs.GetItemCount() ? oldsel : 0);
	m_cattabs.RefreshDarkScrollButtons();
}
