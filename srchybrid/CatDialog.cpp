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
#include "CustomAutoComplete.h"
#include "SharedFileList.h"
#include "emuledlg.h"
#include "TransferDlg.h"
#include "CatDialog.h"
#include "UserMsgs.h"
#include "eMuleAI/DarkMode.h" 
#include "emuledlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define	REGULAREXPRESSIONS_STRINGS_PROFILE	_T("AC_VF_RegExpr.dat")

// CCatDialog dialog

IMPLEMENT_DYNAMIC(CCatDialog, CDialog)

BEGIN_MESSAGE_MAP(CCatDialog, CDialog)
	ON_BN_CLICKED(IDC_BROWSE, OnBnClickedBrowse)
	ON_BN_CLICKED(IDOK, OnBnClickedOk)
	ON_BN_CLICKED(IDC_REB, OnDDBnClicked)
	ON_MESSAGE(UM_CPN_SELENDOK, OnSelChange) //UM_CPN_SELCHANGE
END_MESSAGE_MAP()

CCatDialog::CCatDialog(int index)
	: CDialog(CCatDialog::IDD)
	, m_pacRegExp()
{
	m_myCat = thePrefs.GetCategory(index);
	if (m_myCat != NULL)
		m_newcolor = CLR_NONE;
}

CCatDialog::~CCatDialog()
{
	if (m_pacRegExp) {
		m_pacRegExp->SaveList(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + REGULAREXPRESSIONS_STRINGS_PROFILE);
		m_pacRegExp->Unbind();
		m_pacRegExp->Release();
	}
}

BOOL CCatDialog::OnInitDialog()
{
	CDialog::OnInitDialog();
	Localize();
	m_ctlColor.SetDefaultColor(GetCustomSysColor(COLOR_BTNTEXT));
	UpdateData();

	if (!thePrefs.IsExtControlsEnabled()) {
		GetDlgItem(IDC_REGEXPR)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_STATIC_REGEXP)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_REGEXP)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_REB)->ShowWindow(SW_HIDE);
	}

	if (m_pacRegExp) {
		m_pacRegExp->Unbind();
		m_pacRegExp->Release();
	}
	m_pacRegExp = new CCustomAutoComplete();
	m_pacRegExp->AddRef();
	if (m_pacRegExp->Bind(::GetDlgItem(m_hWnd, IDC_REGEXP), ACO_UPDOWNKEYDROPSLIST | ACO_AUTOSUGGEST))
		m_pacRegExp->LoadList(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + REGULAREXPRESSIONS_STRINGS_PROFILE);

	if (theApp.m_fontSymbol.m_hObject) {
		GetDlgItem(IDC_REB)->SetFont(&theApp.m_fontSymbol);
		SetDlgItemText(IDC_REB, _T("6")); // show a down-arrow
	}

	InitWindowStyles(this); // Moved down

	return TRUE;
}

void CCatDialog::UpdateData()
{
	SetDlgItemText(IDC_TITLE, m_myCat->strTitle);
	SetDlgItemText(IDC_INCOMING, m_myCat->strIncomingPath);
	SetDlgItemText(IDC_COMMENT, m_myCat->strComment);

	if (m_myCat->filter == 18)
		SetDlgItemText(IDC_REGEXP, m_myCat->regexp);

	CheckDlgButton(IDC_REGEXPR, m_myCat->ac_regexpeval);

	m_newcolor = m_myCat->color;
	m_ctlColor.SetColor(m_myCat->color == CLR_NONE ? m_ctlColor.GetDefaultColor() : m_myCat->color);

	SetDlgItemText(IDC_AUTOCATEXT, m_myCat->autocat);

	m_prio.SetCurSel(m_myCat->prio);
}

void CCatDialog::DoDataExchange(CDataExchange *pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_CATCOLOR, m_ctlColor);
	DDX_Control(pDX, IDC_PRIOCOMBO, m_prio);
}

void CCatDialog::Localize()
{
	SetDlgItemText(IDC_STATIC_TITLE, GetResString(_T("TITLE")));
	SetDlgItemText(IDC_STATIC_INCOMING, GetResString(_T("PW_INCOMING")) + _T("  ") + GetResString(_T("SHAREWARNING")));
	SetDlgItemText(IDC_STATIC_COMMENT, GetResString(_T("COMMENT")));
	SetDlgItemText(IDCANCEL, GetResString(_T("CANCEL")));
	SetDlgItemText(IDC_STATIC_COLOR, GetResString(_T("COLOR")));
	SetDlgItemText(IDC_STATIC_PRIO, GetResString(_T("STARTPRIO")));
	SetDlgItemText(IDC_STATIC_AUTOCAT, GetResString(_T("AUTOCAT_LABEL")));
	SetDlgItemText(IDC_REGEXPR, GetResString(_T("ASREGEXPR")));
	SetDlgItemText(IDOK, GetResString(_T("TREEOPTIONS_OK")));

	m_ctlColor.CustomText = GetResString(_T("COL_MORECOLORS"));
	m_ctlColor.DefaultText = GetResString(_T("DEFAULT"));

	SetWindowText(GetResString(_T("EDITCAT")));

	SetDlgItemText(IDC_STATIC_REGEXP, GetResString(_T("STATIC_REGEXP")));

	m_prio.ResetContent();
	m_prio.AddString(GetResString(_T("PRIOLOW")));
	m_prio.AddString(GetResString(_T("PRIONORMAL")));
	m_prio.AddString(GetResString(_T("PRIOHIGH")));
	m_prio.SetCurSel(m_myCat->prio);
}

void CCatDialog::OnBnClickedBrowse()
{
	TCHAR buffer[MAX_PATH];
	GetDlgItemText(IDC_INCOMING, buffer, _countof(buffer));
	if (SelectDir(GetSafeHwnd(), buffer, GetResString(_T("SELECT_INCOMINGDIR"))))
		SetDlgItemText(IDC_INCOMING, buffer);
}

void CCatDialog::OnBnClickedOk()
{
	CString oldpath = m_myCat->strIncomingPath;
	if (GetDlgItem(IDC_TITLE)->GetWindowTextLength() > 0)
		GetDlgItemText(IDC_TITLE, m_myCat->strTitle);

	if (GetDlgItem(IDC_INCOMING)->GetWindowTextLength() > 2)
		GetDlgItemText(IDC_INCOMING, m_myCat->strIncomingPath);

	GetDlgItemText(IDC_COMMENT, m_myCat->strComment);

	MakeFoldername(m_myCat->strIncomingPath);
	if (!thePrefs.IsShareableDirectory(m_myCat->strIncomingPath))
		m_myCat->strIncomingPath = thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR);

	if (!::PathFileExists(m_myCat->strIncomingPath) && !::CreateDirectory(m_myCat->strIncomingPath, 0)) {
		ErrorBalloon(IDC_INCOMING, _T("ERR_BADFOLDER"));
		m_myCat->strIncomingPath = oldpath;
		return;
	}

	if (m_myCat->strIncomingPath.CompareNoCase(oldpath) != 0)
		theApp.sharedfiles->Reload();

	m_myCat->color = m_newcolor;
	m_myCat->prio = m_prio.GetCurSel();

	m_myCat->ac_regexpeval = IsDlgButtonChecked(IDC_REGEXPR) != 0;

	GetDlgItemText(IDC_AUTOCATEXT, m_myCat->autocat);
	if (m_myCat->ac_regexpeval && !IsRegExpValid(m_myCat->autocat)) {
		ErrorBalloon(IDC_AUTOCATEXT, _T("ERR_REGEXP"));
		return;
	}

	GetDlgItemText(IDC_REGEXP, m_myCat->regexp);
	if (m_myCat->regexp.GetLength() > 0) {
		if (!IsRegExpValid(m_myCat->regexp)) {
			ErrorBalloon(IDC_REGEXP, _T("ERR_REGEXP"));
			return;
		}
		if (m_pacRegExp && m_pacRegExp->IsBound()) {
			m_pacRegExp->AddItem(m_myCat->regexp, 0);
			m_myCat->filter = 18;
		}
	} else if (m_myCat->filter == 18) {
		// deactivate regexp
		m_myCat->filter = 0;
	}

	theApp.emuledlg->transferwnd->GetDownloadList()->Invalidate();

	OnOK();
}

LRESULT CCatDialog::OnSelChange(WPARAM wParam, LPARAM)
{
	m_newcolor = (wParam == CLR_DEFAULT) ? CLR_NONE : m_ctlColor.GetColor();
	return 0;
}

void CCatDialog::OnDDBnClicked()
{
	CWnd *box = GetDlgItem(IDC_REGEXP);
	box->SetFocus();
	box->SetWindowText(EMPTY);
	box->SendMessage(WM_KEYDOWN, VK_DOWN, 0x00510001);
}

void CCatDialog::ErrorBalloon(int iEdit, LPCTSTR uid)
{
	static_cast<CEdit*>(GetDlgItem(iEdit))->ShowBalloonTip(GetResString(_T("ERROR")), GetResString(uid), TTI_ERROR);
}