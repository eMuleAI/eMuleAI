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
#include "emuledlg.h"
#include "SearchDlg.h"
#include "SearchParamsWnd.h"
#include "SearchResultsWnd.h"
#include "OtherFunctions.h"
#include "CustomAutoComplete.h"
#include "HelpIDs.h"
#include "Opcodes.h"
#include "StringConversion.h"
#include <list>
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define	SEARCH_STRINGS_PROFILE	_T("AC_SearchStrings.dat")

IMPLEMENT_DYNAMIC(CSearchParamsWnd, CDialogBar);

BEGIN_MESSAGE_MAP(CSearchParamsWnd, CDialogBar)
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_SYSCOLORCHANGE()
	ON_MESSAGE(WM_INITDIALOG, OnInitDialog)
	ON_BN_CLICKED(IDC_STARTS, OnBnClickedStart)
	ON_BN_CLICKED(IDC_CANCELS, OnBnClickedCancel)
	ON_BN_CLICKED(IDC_MORE, OnBnClickedMore)
	ON_EN_CHANGE(IDC_SEARCHNAME, OnEnChangeName)
	ON_CBN_SELCHANGE(IDC_TYPESEARCH, OnEnChangeType)
	ON_CBN_SELENDOK(IDC_TYPESEARCH, OnEnChangeType)
	ON_BN_CLICKED(IDC_SEARCH_RESET, OnBnClickedSearchReset)
	ON_BN_CLICKED(IDC_DD, OnDDClicked)
	ON_CBN_SELCHANGE(IDC_COMBO1, OnCbnSelChangeMethod)
	ON_CBN_SELENDOK(IDC_COMBO1, OnCbnSelEndOkMethod)
	ON_WM_SYSCOMMAND()
	ON_WM_SETCURSOR()
	ON_WM_HELPINFO()
	ON_WM_LBUTTONDOWN()
END_MESSAGE_MAP()


CSearchParamsWnd::CSearchParamsWnd()
	: m_searchdlg()
	, m_hcurMove(::LoadCursor(NULL, IDC_SIZEALL)) // load default windows system cursor (a shared resource, do not destoy)
	, m_pacSearchString()
{
}

CSearchParamsWnd::~CSearchParamsWnd()
{
	ReleaseSearchAutoComplete();
}

void CSearchParamsWnd::ReleaseSearchAutoComplete()
{
	if (m_pacSearchString) {
		m_pacSearchString->Unbind();
		m_pacSearchString->Release();
		m_pacSearchString = NULL;
	}
}

void CSearchParamsWnd::DoDataExchange(CDataExchange *pDX)
{
	CDialogBar::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_COMBO1, m_ctlMethod);
	DDX_Control(pDX, IDC_TYPESEARCH, m_ctlFileType);
	DDX_Control(pDX, IDC_SEARCHNAME, m_ctlName);
	DDX_Control(pDX, IDC_SEARCH_OPTS, m_ctlOpts);
	DDX_Control(pDX, IDC_STARTS, m_ctlStart);
	DDX_Control(pDX, IDC_CANCELS, m_ctlCancel);
	DDX_Control(pDX, IDC_MORE, m_ctlMore);
}

LRESULT CSearchParamsWnd::OnInitDialog(WPARAM, LPARAM)
{
	Default();
	InitWindowStyles(this);

	CRect sizeDefault;
	GetWindowRect(sizeDefault);
	CRect rcBorders(4, 4, 4, 4);
	SetBorders(rcBorders);
	m_szFloat.cx = sizeDefault.Width() + rcBorders.left + rcBorders.right + ::GetSystemMetrics(SM_CXEDGE) * 2;
	m_szFloat.cy = sizeDefault.Height() + rcBorders.top + rcBorders.bottom + ::GetSystemMetrics(SM_CYEDGE) * 2;
	m_szMRU = m_szFloat;

	UpdateData(FALSE);
	SetAllIcons();

	GetDlgItem(IDC_MSTATIC3)->GetWindowRect(m_rcNameLbl);
	ScreenToClient(m_rcNameLbl);

	m_ctlName.GetWindowRect(m_rcName);
	ScreenToClient(m_rcName);

	GetDlgItem(IDC_DD)->GetWindowRect(m_rcDropDownArrow);
	ScreenToClient(m_rcDropDownArrow);

	GetDlgItem(IDC_MSTATIC7)->GetWindowRect(m_rcFileTypeLbl);
	ScreenToClient(m_rcFileTypeLbl);

	m_ctlFileType.GetWindowRect(m_rcFileType);
	ScreenToClient(m_rcFileType);

	GetDlgItem(IDC_SEARCH_RESET)->GetWindowRect(m_rcReset);
	ScreenToClient(m_rcReset);

	GetDlgItem(IDC_METH)->GetWindowRect(m_rcMethodLbl);
	ScreenToClient(m_rcMethodLbl);

	m_ctlMethod.GetWindowRect(m_rcMethod);
	ScreenToClient(m_rcMethod);

	m_ctlOpts.GetWindowRect(m_rcOpts);
	ScreenToClient(m_rcOpts);

	m_ctlStart.GetWindowRect(m_rcStart);
	ScreenToClient(m_rcStart);

	m_ctlMore.GetWindowRect(m_rcMore);
	ScreenToClient(m_rcMore);

	m_ctlCancel.GetWindowRect(m_rcCancel);
	ScreenToClient(m_rcCancel);

	if (thePrefs.GetUseAutocompletion()) {
		ReleaseSearchAutoComplete();
		m_pacSearchString = new CCustomAutoComplete();
		m_pacSearchString->AddRef();
		if (m_pacSearchString->Bind(m_ctlName, ACO_UPDOWNKEYDROPSLIST | ACO_AUTOSUGGEST))
			m_pacSearchString->LoadList(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + SEARCH_STRINGS_PROFILE);
		if (theApp.m_fontSymbol.m_hObject) {
			GetDlgItem(IDC_DD)->SetFont(&theApp.m_fontSymbol);
			SetDlgItemText(IDC_DD, _T("6")); // show a down-arrow
		}
	} else
		GetDlgItem(IDC_DD)->ShowWindow(SW_HIDE);

	m_ctlName.LimitText(MAX_SEARCH_EXPRESSION_LEN); // max. length of search expression

	m_ctlMethod.ModifyStyle(0, WS_CLIPCHILDREN); // Reduce flickering during resize
	InitMethodsCtrl();
	if (m_ctlMethod.SetCurSel(thePrefs.GetSearchMethod()) == CB_ERR)
		m_ctlMethod.SetCurSel(SearchTypeEd2kServer);


	SetBarStyle(GetBarStyle() & ~CBRS_GRIPPER); // remove gripper

	m_ctlOpts.ModifyStyle(0, WS_CLIPCHILDREN); // Does not help, control keeps flickering like mad
	m_ctlOpts.SetExtendedStyle(IsDarkModeEnabled() ? LVS_EX_LABELTIP : LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
	// Alignment rule: left for text, dates, and status labels; right for sizes, rates, counts, durations, and percentages.
	m_ctlOpts.InsertColumn(0, GetResString(_T("PARAMETER")));
	m_ctlOpts.InsertColumn(1, GetResString(_T("VALUE")));

	m_ctlOpts.InsertItem(orMinSize, GetResString(_T("SEARCHMINSIZE")));
	m_ctlOpts.InsertItem(orMaxSize, GetResString(_T("SEARCHMAXSIZE")));
	m_ctlOpts.InsertItem(orAvailability, GetResString(_T("SEARCHAVAIL")));
	m_ctlOpts.InsertItem(orExtension, GetResString(_T("SEARCHEXTENTION")));
	m_ctlOpts.InsertItem(orCompleteSources, GetResString(_T("COMPLSOURCES")));
	m_ctlOpts.InsertItem(orCodec, GetResString(_T("CODEC")));
	m_ctlOpts.InsertItem(orBitrate, GetResString(_T("MINBITRATE")));
	m_ctlOpts.InsertItem(orLength, GetResString(_T("MINLENGTH")));
	m_ctlOpts.InsertItem(orTitle, GetResString(_T("TITLE")));
	m_ctlOpts.InsertItem(orAlbum, GetResString(_T("ALBUM")));
	m_ctlOpts.InsertItem(orArtist, GetResString(_T("ARTIST")));
	m_ctlOpts.SetColumnWidth(0, 100/*LVSCW_AUTOSIZE*/);
	m_ctlOpts.SetColumnWidth(1, 120);
	UpdateControls();
	OnEnChangeName();
	
	return TRUE;
}

#define	MIN_HORZ_WIDTH	500
#define	MIN_VERT_WIDTH	200

CSize CSearchParamsWnd::CalcDynamicLayout(int nLength, DWORD dwMode)
{
	CFrameWnd *pFrm = GetDockingFrame();

	// This function is typically called with

	CRect rcFrmClnt;
	pFrm->GetClientRect(&rcFrmClnt);
	CRect rcInside(rcFrmClnt);
	CalcInsideRect(rcInside, dwMode & LM_HORZDOCK);
	RECT rcBorders;
	rcBorders.left = rcInside.left - rcFrmClnt.left;
	rcBorders.top = rcInside.top - rcFrmClnt.top;
	rcBorders.bottom = rcFrmClnt.bottom - rcInside.bottom;
	rcBorders.right = rcFrmClnt.right - rcInside.right;

	if (dwMode & (LM_HORZDOCK | LM_VERTDOCK)) {
		if (dwMode & LM_VERTDOCK) {
			CSize szFloat;
			szFloat.cx = MIN_VERT_WIDTH;
			szFloat.cy = rcFrmClnt.Height() + ::GetSystemMetrics(SM_CYEDGE) * 2;
			m_szFloat = szFloat;
			return szFloat;
		}
		if (dwMode & LM_HORZDOCK) {
			CSize szFloat;
			szFloat.cx = rcFrmClnt.Width() + ::GetSystemMetrics(SM_CXEDGE) * 2;
			szFloat.cy = m_sizeDefault.cy + rcBorders.top + rcBorders.bottom;
			m_szFloat = szFloat;
			return szFloat;
		}
		return CDialogBar::CalcDynamicLayout(nLength, dwMode);
	}

	if (dwMode & LM_MRUWIDTH)
		return m_szMRU;

	if (dwMode & LM_COMMIT) {
		m_szMRU = m_szFloat;
		return m_szFloat;
	}

	CSize szFloat;
	if ((dwMode & LM_LENGTHY) == 0) {
		szFloat.cx = nLength;
		if (nLength < m_sizeDefault.cx + rcBorders.left + rcBorders.right) {
			szFloat.cx = MIN_VERT_WIDTH;
			szFloat.cy = MIN_HORZ_WIDTH;
		} else
			szFloat.cy = m_sizeDefault.cy + rcBorders.top + rcBorders.bottom;
	} else {
		szFloat.cy = nLength;
		if (nLength < MIN_HORZ_WIDTH) {
			szFloat.cx = m_sizeDefault.cx + rcBorders.left + rcBorders.right;
			szFloat.cy = m_sizeDefault.cy + rcBorders.top + rcBorders.bottom;
		} else
			szFloat.cx = MIN_VERT_WIDTH;
	}

	m_szFloat = szFloat;
	return szFloat;
}

BOOL CSearchParamsWnd::OnSetCursor(CWnd *pWnd, UINT nHitTest, UINT message)
{
	if (m_hcurMove && ((m_dwStyle & (CBRS_GRIPPER | CBRS_FLOATING)) == CBRS_GRIPPER) && pWnd->GetSafeHwnd() == m_hWnd) {
		POINT ptCursor;
		if (::GetCursorPos(&ptCursor)) {
			ScreenToClient(&ptCursor);
			CRect rcClient;
			GetClientRect(&rcClient);
			bool bMouseOverGripper = rcClient.PtInRect(ptCursor)
				&& ((m_dwStyle & CBRS_ORIENT_HORZ) ? ptCursor.x : ptCursor.y) <= 10;
			if (bMouseOverGripper) {
				::SetCursor(m_hcurMove);
				return TRUE;
			}
		}
	}
	return CDialogBar::OnSetCursor(pWnd, nHitTest, message);
}

void CSearchParamsWnd::OnSize(UINT nType, int cx, int cy)
{
	CDialogBar::OnSize(nType, cx, cy);

	if (m_ctlName.m_hWnd == NULL)
		return;
	if (cx >= MIN_HORZ_WIDTH) {
		CRect rcClient;
		GetClientRect(&rcClient);
		CalcInsideRect(rcClient, TRUE);

		// resizing the name field instead the filter fields makes sense, because the filter input mostly
		// stays small while a bigger name filed allows longer search queries without scrolling.
		// however it doesn't look nice because of the asymmetry which is created by not resizing
		// the method selectors but resizing them wouldn't make any sense at all, so we need to find a better
		// build up at some point. For now stay with the old method which is not perfect but looks fair enough

		HDWP hdwp = BeginDeferWindowPos(12);
		if (hdwp) {
			int iWidthOpts = rcClient.right - (rcClient.left + m_rcOpts.left);
			const UINT uFlags = SWP_NOZORDER | SWP_NOACTIVATE;
			hdwp = DeferWindowPos(hdwp, *GetDlgItem(IDC_MSTATIC3), NULL, rcClient.left + m_rcNameLbl.left, rcClient.top + m_rcNameLbl.top, m_rcNameLbl.Width(), m_rcNameLbl.Height(), uFlags);
			hdwp = DeferWindowPos(hdwp, m_ctlName, NULL, rcClient.left + m_rcName.left, rcClient.top + m_rcName.top, m_rcName.Width(), m_rcName.Height(), uFlags);
			hdwp = DeferWindowPos(hdwp, *GetDlgItem(IDC_DD), NULL, rcClient.left + m_rcDropDownArrow.left, rcClient.top + m_rcDropDownArrow.top, m_rcDropDownArrow.Width(), m_rcDropDownArrow.Height(), uFlags);
			hdwp = DeferWindowPos(hdwp, *GetDlgItem(IDC_MSTATIC7), NULL, rcClient.left + m_rcFileTypeLbl.left, rcClient.top + m_rcFileTypeLbl.top, m_rcFileTypeLbl.Width(), m_rcFileTypeLbl.Height(), uFlags);
			hdwp = DeferWindowPos(hdwp, m_ctlFileType, NULL, rcClient.left + m_rcFileType.left, rcClient.top + m_rcFileType.top, m_rcFileType.Width(), m_rcFileType.Height(), uFlags);
			hdwp = DeferWindowPos(hdwp, *GetDlgItem(IDC_SEARCH_RESET), NULL, rcClient.left + m_rcReset.left, rcClient.top + m_rcReset.top, m_rcReset.Width(), m_rcReset.Height(), uFlags);
			hdwp = DeferWindowPos(hdwp, *GetDlgItem(IDC_METH), NULL, rcClient.left + m_rcMethodLbl.left, rcClient.top + m_rcMethodLbl.top, m_rcMethodLbl.Width(), m_rcMethodLbl.Height(), uFlags);
			hdwp = DeferWindowPos(hdwp, m_ctlMethod, NULL, rcClient.left + m_rcMethod.left, rcClient.top + m_rcMethod.top, m_rcMethod.Width(), m_rcMethod.Height(), uFlags);
			hdwp = DeferWindowPos(hdwp, m_ctlStart, NULL, rcClient.left + m_rcStart.left, rcClient.top + m_rcStart.top, m_rcStart.Width(), m_rcStart.Height(), uFlags);
			hdwp = DeferWindowPos(hdwp, m_ctlMore, NULL, rcClient.left + m_rcMore.left, rcClient.top + m_rcMore.top, m_rcMore.Width(), m_rcMore.Height(), uFlags);
			hdwp = DeferWindowPos(hdwp, m_ctlCancel, NULL, rcClient.left + m_rcCancel.left, rcClient.top + m_rcCancel.top, m_rcCancel.Width(), m_rcCancel.Height(), uFlags);
			hdwp = DeferWindowPos(hdwp, m_ctlOpts, NULL, rcClient.left + m_rcOpts.left, rcClient.top + m_rcOpts.top, iWidthOpts, m_rcOpts.Height(), uFlags);
			VERIFY(EndDeferWindowPos(hdwp));
		}

		m_ctlOpts.ModifyStyle(0, LVS_NOCOLUMNHEADER);
		CRect rcOptsClnt;
		m_ctlOpts.GetClientRect(&rcOptsClnt);
		m_ctlOpts.SetColumnWidth(0, LVSCW_AUTOSIZE);
		// (**1) Adjust for Windows Classic Theme and Flat Style control
		int iCol2Width = rcOptsClnt.Width() - m_ctlOpts.GetColumnWidth(0) - 2/*(**1)*/;
		if (m_ctlOpts.GetColumnWidth(1) != iCol2Width)
			m_ctlOpts.SetColumnWidth(1, iCol2Width);
	} else if (cx < MIN_HORZ_WIDTH) {
		CRect rcClient;
		GetClientRect(&rcClient);
		CalcInsideRect(rcClient, FALSE);

		int y = rcClient.top;

		CWnd *item = GetDlgItem(IDC_MSTATIC3);
		CRect rcNameLbl;
		item->GetWindowRect(rcNameLbl);
		ScreenToClient(rcNameLbl);
		item->MoveWindow(rcClient.left, y, rcNameLbl.Width(), rcNameLbl.Height());
		y += rcNameLbl.Height() + 2;

		CRect rcName;
		m_ctlName.GetWindowRect(rcName);
		ScreenToClient(rcName);
		item = GetDlgItem(IDC_DD);
		CRect rcDropDownArrow;
		item->GetWindowRect(rcDropDownArrow);
		ScreenToClient(rcDropDownArrow);
		int iNameWidth = rcClient.Width() - 4 - rcDropDownArrow.Width();
		m_ctlName.MoveWindow(rcClient.left, y, iNameWidth, rcName.Height());
		item->MoveWindow(rcClient.left + iNameWidth + 4, y, rcDropDownArrow.Width(), rcDropDownArrow.Height());
		y += rcName.Height() + 2;

		item = GetDlgItem(IDC_MSTATIC7);
		CRect rcFileTypeLbl;
		item->GetWindowRect(rcFileTypeLbl);
		ScreenToClient(rcFileTypeLbl);
		item->MoveWindow(rcClient.left, y, rcFileTypeLbl.Width(), rcFileTypeLbl.Height());
		y += rcFileTypeLbl.Height() + 2;

		CRect rcFileType;
		m_ctlFileType.GetWindowRect(rcFileType);
		ScreenToClient(rcFileType);
		m_ctlFileType.MoveWindow(rcClient.left, y, rcFileType.Width(), rcFileType.Height());

		//Combo box height is 24 and button is 22, but the same on-screen height
		item = GetDlgItem(IDC_SEARCH_RESET);
		CRect rcReset;
		item->GetWindowRect(rcReset);
		ScreenToClient(rcReset);
		item->MoveWindow(rcClient.left + rcFileType.Width() + 8, y - 1, rcReset.Width(), rcReset.Height());

		y += rcFileType.Height() + 8;

		item = GetDlgItem(IDC_METH);
		CRect rcMethodLbl;
		item->GetWindowRect(rcMethodLbl);
		ScreenToClient(rcMethodLbl);
		item->MoveWindow(rcClient.left, y, rcMethodLbl.Width(), rcMethodLbl.Height());
		y += rcMethodLbl.Height() + 2;

		CRect rcMethod;
		m_ctlMethod.GetWindowRect(rcMethod);
		ScreenToClient(rcMethod);
		m_ctlMethod.MoveWindow(rcClient.left, y, rcMethod.Width(), rcMethod.Height());
		y += rcMethod.Height() + 8;

		m_ctlStart.MoveWindow(rcClient.left, y, m_rcStart.Width(), m_rcStart.Height());
		m_ctlMore.MoveWindow(rcClient.left + m_rcStart.Width() + 4, y, m_rcMore.Width(), m_rcMore.Height());
		y += m_rcStart.Height() + 4;
		m_ctlCancel.MoveWindow(rcClient.left, y, m_rcCancel.Width(), m_rcCancel.Height());
		y += m_rcStart.Height() + 4;

		int iOptsHeight = rcClient.bottom - y - 2;
		m_ctlOpts.MoveWindow(rcClient.left, y, rcClient.Width(), iOptsHeight);
		CRect rcOptsClnt;
		m_ctlOpts.GetClientRect(rcOptsClnt);
		m_ctlOpts.SetColumnWidth(1, rcOptsClnt.Width() - m_ctlOpts.GetColumnWidth(0));

		m_ctlOpts.ModifyStyle(LVS_NOCOLUMNHEADER, 0);
	}
}

void CSearchParamsWnd::OnUpdateCmdUI(CFrameWnd* /*pTarget*/, BOOL /*bDisableIfNoHndler*/)
{
	// Disable MFC's command routing by not passing the process flow to base class
}

void CSearchParamsWnd::UpdateControls()
{
	int iMethod = m_ctlMethod.GetCurSel();
	if (iMethod != CB_ERR && iMethod != thePrefs.GetSearchMethod()) {
		if (iMethod == SearchTypeKademlia)
			OnEnChangeName();
		else if (m_searchdlg->IsLocalEd2kSearchRunning() && (iMethod == SearchTypeEd2kServer || iMethod == SearchTypeEd2kGlobal))
			m_ctlStart.EnableWindow(FALSE);
		thePrefs.SetSearchMethod(iMethod);
	}

	DWORD_PTR dwData = static_cast<DWORD_PTR>(iMethod == SearchTypeEd2kServer || iMethod == SearchTypeEd2kGlobal);
	m_ctlOpts.SetItemData(orAvailability, 0);
	m_ctlOpts.SetItemData(orExtension, 0);
	m_ctlOpts.SetItemData(orCompleteSources, static_cast<DWORD_PTR>(iMethod == SearchTypeKademlia)); //ed2k only
	m_ctlOpts.SetItemData(orCodec, 0);
	m_ctlOpts.SetItemData(orBitrate, 0);
	m_ctlOpts.SetItemData(orLength, 0);
	m_ctlOpts.SetItemData(orTitle, dwData); //KAD only
	m_ctlOpts.SetItemData(orAlbum, dwData);	//
	m_ctlOpts.SetItemData(orArtist, dwData);//
}

void CSearchParamsWnd::SetAllIcons()
{
	CImageList iml;
	iml.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	iml.Add(CTempIconLoader(_T("SearchMethod_KadServer")));
	iml.Add(CTempIconLoader(_T("SearchMethod_SERVER")));
	iml.Add(CTempIconLoader(_T("SearchMethod_GLOBAL")));
	iml.Add(CTempIconLoader(_T("SearchMethod_KADEMLIA")));

	m_ctlMethod.SetImageList(&iml);
	m_imlSearchMethods.DeleteImageList();
	m_imlSearchMethods.Attach(iml.Detach());

}

void CSearchParamsWnd::OnDestroy()
{
	ReleaseSearchAutoComplete();
	if (m_ctlOpts.m_hWnd)
		m_ctlOpts.DeleteAllItems();

	CDialogBar::OnDestroy();
	m_imlSearchMethods.DeleteImageList();
}

void CSearchParamsWnd::OnSysColorChange()
{
	CDialogBar::OnSysColorChange();
	SetAllIcons();
}

void CSearchParamsWnd::InitMethodsCtrl()
{
	int iMethod = m_ctlMethod.GetCurSel();
	m_ctlMethod.ResetContent();
	VERIFY(m_ctlMethod.AddItem(GetResString(_T("AUTOMATIC")), 0) == SearchTypeAutomatic);
	VERIFY(m_ctlMethod.AddItem(GetResString(_T("SERVER")), 1) == SearchTypeEd2kServer);
	VERIFY(m_ctlMethod.AddItem(GetResString(_T("GLOBALSEARCH")), 2) == SearchTypeEd2kGlobal);
	VERIFY(m_ctlMethod.AddItem(GetResString(_T("KADEMLIA")) + _T(' ') + GetResString(_T("NETWORK")), 3) == SearchTypeKademlia);

	UpdateHorzExtent(m_ctlMethod, 16); // adjust dropped width to ensure all strings are fully visible
	m_ctlMethod.SetCurSel(iMethod != CB_ERR ? iMethod : SearchTypeAutomatic);
}


void CSearchParamsWnd::Localize()
{
	SetWindowText(GetResString(_T("SEARCHPARAMS")));

	SetDlgItemText(IDC_MSTATIC3, GetResString(_T("SW_NAME")));
	SetDlgItemText(IDC_MSTATIC7, GetResString(_T("TYPE")));
	SetDlgItemText(IDC_SEARCH_RESET, GetResString(_T("PW_RESET")));
	SetDlgItemText(IDC_METH, GetResString(_T("METHOD")));

	m_ctlStart.SetWindowText(GetResString(_T("SW_START")));
	m_ctlCancel.SetWindowText(GetResString(_T("CANCEL")));
	m_ctlMore.SetWindowText(GetResString(_T("MORE")));

	SetWindowText(GetResString(_T("SEARCHPARAMS")));

	InitMethodsCtrl();

	InitFileTypesCtrl(&m_ctlFileType, thePrefs.GetSearchFileType(), false);

	m_ctlOpts.SetItemText(orMinSize, 0, GetResString(_T("SEARCHMINSIZE")));
	m_ctlOpts.SetItemText(orMaxSize, 0, GetResString(_T("SEARCHMAXSIZE")));
	m_ctlOpts.SetItemText(orAvailability, 0, GetResString(_T("SEARCHAVAIL")));
	m_ctlOpts.SetItemText(orExtension, 0, GetResString(_T("SEARCHEXTENTION")));
	m_ctlOpts.SetItemText(orCompleteSources, 0, GetResString(_T("COMPLSOURCES")));
	m_ctlOpts.SetItemText(orCodec, 0, GetResString(_T("CODEC")));
	m_ctlOpts.SetItemText(orBitrate, 0, GetResString(_T("MINBITRATE")));
	m_ctlOpts.SetItemText(orLength, 0, GetResString(_T("MINLENGTH")));
	m_ctlOpts.SetItemText(orTitle, 0, GetResString(_T("TITLE")));
	m_ctlOpts.SetItemText(orAlbum, 0, GetResString(_T("ALBUM")));
	m_ctlOpts.SetItemText(orArtist, 0, GetResString(_T("ARTIST")));

	CHeaderCtrl *pHeaderCtrl = m_ctlOpts.GetHeaderCtrl();
	HDITEM hdi;
	hdi.mask = HDI_TEXT;

	const CString &sHdr(GetResString(_T("PARAMETER")));
	hdi.pszText = const_cast<LPTSTR>((LPCTSTR)sHdr);
	pHeaderCtrl->SetItem(0, &hdi);

	const CString &sHdr1(GetResString(_T("VALUE")));
	hdi.pszText = const_cast<LPTSTR>((LPCTSTR)sHdr1);
	pHeaderCtrl->SetItem(1, &hdi);
}

BOOL CSearchParamsWnd::PreTranslateMessage(MSG *pMsg)
{
	if (pMsg->message == WM_KEYDOWN) {
		if (pMsg->wParam == VK_ESCAPE)
			return FALSE;

		if (pMsg->wParam == VK_DELETE && m_pacSearchString && m_pacSearchString->IsBound() && pMsg->hwnd == m_ctlName.m_hWnd) {
			if (GetKeyState(VK_MENU) < 0 || GetKeyState(VK_CONTROL) < 0)
				m_pacSearchString->Clear();
			else
				m_pacSearchString->RemoveSelectedItem();
		}

		if (pMsg->wParam == VK_RETURN && m_ctlStart.IsWindowEnabled()) {
			if (m_pacSearchString && m_pacSearchString->IsBound() && pMsg->hwnd == m_ctlName.m_hWnd) {
				CString strText;
				m_ctlName.GetWindowText(strText);
				if (!strText.IsEmpty()) {
					m_ctlName.SetWindowText(EMPTY); // this seems to be the only chance to let the drop-down list to disappear
					m_ctlName.SetWindowText(strText);
					m_ctlName.SetSel(strText.GetLength(), strText.GetLength());
				}
			}
		}
	}

	return CDialogBar::PreTranslateMessage(pMsg);
}

void CSearchParamsWnd::OnBnClickedStart()
{
	m_ctlMore.EnableWindow(FALSE);
	if (m_ctlOpts.GetEditCtrl()->GetSafeHwnd())
		m_ctlOpts.CommitEdit();

	SSearchParams *pParams = GetParameters();
	if (pParams)
		if (pParams->strExpression.IsEmpty())
			delete pParams;
		else {
			if (m_pacSearchString && m_pacSearchString->IsBound())
				m_pacSearchString->AddItem(pParams->strExpression, 0);
			m_searchdlg->StartSearch(pParams);
		}
}

void CSearchParamsWnd::OnBnClickedMore()
{
	CWnd *pWndFocus = GetFocus();
	m_ctlMore.EnableWindow(FALSE);
	if (pWndFocus && pWndFocus->m_hWnd == m_ctlMore.m_hWnd)
		m_ctlStart.SetFocus();

	if (m_searchdlg->SearchMore()) {
		pWndFocus = GetFocus();
		m_ctlStart.EnableWindow(FALSE);
		if (pWndFocus && pWndFocus->m_hWnd == m_ctlStart.m_hWnd)
			m_ctlName.SetFocus();
		m_ctlCancel.EnableWindow(TRUE);
	}
}

void CSearchParamsWnd::OnBnClickedCancel()
{
	m_searchdlg->CancelSearch();

	CWnd *pWndFocus = GetFocus();
	m_ctlCancel.EnableWindow(FALSE);
	if (pWndFocus && pWndFocus->m_hWnd == m_ctlCancel.m_hWnd)
		m_ctlName.SetFocus();
	m_ctlStart.EnableWindow(TRUE);
}

void CSearchParamsWnd::ResetHistory()
{
	if (m_pacSearchString != NULL) {
		m_ctlName.SendMessage(WM_KEYDOWN, VK_ESCAPE, 0x00510001);
		m_pacSearchString->Clear();
	}
}

void CSearchParamsWnd::OnCbnSelChangeMethod()
{
	UpdateControls();
	OnEnChangeName();
}

void CSearchParamsWnd::OnCbnSelEndOkMethod()
{
	UpdateControls();
}

void CSearchParamsWnd::OnDDClicked()
{
	m_ctlName.SetFocus();
	m_ctlName.SetWindowText(EMPTY);
	m_ctlName.SendMessage(WM_KEYDOWN, VK_DOWN, 0x00510001);
}

BOOL CSearchParamsWnd::SaveSearchStrings()
{
	return m_pacSearchString && m_pacSearchString->SaveList(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + SEARCH_STRINGS_PROFILE);
}

void CSearchParamsWnd::SaveSettings()
{
	SaveSearchStrings();
}

void CSearchParamsWnd::OnEnChangeName()
{
	m_ctlStart.EnableWindow(m_ctlName.GetWindowTextLength() > 0);
}

void CSearchParamsWnd::OnEnChangeType()
{
	OnEnChangeName();

	// get the current selected entry by value (language independent)
	CString strCurSelFileType = _T(ED2KFTSTR_ANY);
	int iItem = m_ctlFileType.GetCurSel();
	if (iItem != CB_ERR) {
		LPCTSTR pszED2KFileType = (LPCTSTR)m_ctlFileType.GetItemDataPtr(iItem);
		ASSERT(pszED2KFileType != NULL);
		strCurSelFileType = pszED2KFileType;
	}

	if (strCurSelFileType != thePrefs.GetSearchFileType())
		thePrefs.SetSearchFileType(strCurSelFileType);
}

void CSearchParamsWnd::OnBnClickedSearchReset()
{
	m_ctlName.SetWindowText(EMPTY);

	if (!m_ctlFileType.SelectItemDataString(_T(ED2KFTSTR_ANY)))
		m_ctlFileType.SetCurSel(0);

	WipeOptionFields();

	OnEnChangeName();
}

void CSearchParamsWnd::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) != SC_KEYMENU)
		CDialogBar::OnSysCommand(nID, lParam);
	else if (lParam == EMULE_HOTMENU_ACCEL)
			theApp.emuledlg->SendMessage(WM_COMMAND, IDC_HOTMENU);
		else
			theApp.emuledlg->SendMessage(WM_SYSCOMMAND, nID, lParam);
}

void CSearchParamsWnd::SetParameters(const SSearchParams *pParams)
{
	//There was a weird report about persistent "Syntax error" with all fields
	//seemingly empty. That effectively disabled any search until restart.
	WipeOptionFields(); //set all to blanks
	if (!pParams->bClientSharedFiles) {
		const int iStoredMethod = static_cast<int>(pParams->eType);
		if (iStoredMethod >= SearchTypeAutomatic && iStoredMethod <= SearchTypeKademlia) {
			if (m_ctlMethod.SetCurSel(iStoredMethod) == CB_ERR)
				m_ctlMethod.SetCurSel(SearchTypeAutomatic);
			UpdateControls();
		}

		m_ctlName.SetWindowText((pParams->strBooleanExpr.IsEmpty() ? pParams->strExpression : pParams->strBooleanExpr));
		if (!m_ctlFileType.SelectItemDataString(pParams->strFileType))
			if (!m_ctlFileType.SelectItemDataString(_T(ED2KFTSTR_ANY)))
				m_ctlFileType.SetCurSel(0);

		m_ctlOpts.SetItemText(orMinSize, 1, pParams->strMinSize);
		m_ctlOpts.SetItemText(orMaxSize, 1, pParams->strMaxSize);

		m_ctlOpts.SetItemText(orExtension, 1, pParams->strExtension);
		CString strBuff;
		if (pParams->uAvailability > 0) {
			strBuff.Format(_T("%u"), pParams->uAvailability);
			m_ctlOpts.SetItemText(orAvailability, 1, strBuff);
		}
		if (pParams->uComplete > 0) {
			strBuff.Format(_T("%u"), pParams->uComplete);
			m_ctlOpts.SetItemText(orCompleteSources, 1, strBuff);
		}
		if (!pParams->strCodec.IsEmpty())
			m_ctlOpts.SetItemText(orCodec, 1, pParams->strCodec);
		if (pParams->uiMinBitrate > 0) {
			strBuff.Format(_T("%u"), pParams->uiMinBitrate);
			m_ctlOpts.SetItemText(orBitrate, 1, strBuff);
		}
		if (pParams->uiMinLength > 0)
			m_ctlOpts.SetItemText(orLength, 1, SecToTimeLength(pParams->uiMinLength));
		if (!pParams->strTitle.IsEmpty())
			m_ctlOpts.SetItemText(orTitle, 1, pParams->strTitle);
		if (!pParams->strAlbum.IsEmpty())
			m_ctlOpts.SetItemText(orAlbum, 1, pParams->strAlbum);
		if (!pParams->strArtist.IsEmpty())
			m_ctlOpts.SetItemText(orArtist, 1, pParams->strArtist);
	}
}

uint64 CSearchParamsWnd::GetSearchAttrSize(const CString &rstrExpr)
{
	CString strExpr(rstrExpr);
	LPTSTR endptr = NULL;
	double dbl = _tcstod(strExpr.Trim(), &endptr);
	if (errno == ERANGE || dbl < 0) //no negative values
		return _UI64_MAX;
	if (endptr && *endptr != _T('\0')) {
		while (*endptr == _T(' '))
			++endptr;

		switch (_totlower(*endptr)) {
		case _T('b'): //bytes
			return (uint64)(dbl + 0.5);
		case _T('k'): //kilobytes
			return (uint64)(dbl * 1024 + 0.5);
		case _T('\0'): //not specified
		case _T('m'): //megabytes
			break;
		case _T('g'): //gigabytes
			return (uint64)(dbl * 1024 * 1024 * 1024 + 0.5);
		default:
			return _UI64_MAX;
		}
	}
	return (uint64)(dbl * 1024 * 1024 + 0.5); // Default = MBytes
}

ULONG CSearchParamsWnd::GetSearchAttrNumber(const CString &rstrExpr)
{
	CString strExpr(rstrExpr);
	LPTSTR endptr = NULL;
	double dbl = _tcstod(strExpr.Trim(), &endptr);
	if (errno == ERANGE || dbl < 0) //no negative values
		return ULONG_MAX;
	if (endptr && *endptr != _T('\0')) {
		while (*endptr == _T(' '))
			++endptr;

		switch (_totlower(*endptr)) {
		case _T('\0'): //not specified
		case _T('b'): //bytes
			break;
		case _T('k'): //kilobytes
			return (ULONG)(dbl * 1000 + 0.5);
		case _T('m'): //megabytes
			return (ULONG)(dbl * 1000 * 1000 + 0.5);
		case _T('g'): //gigabytes
			return (ULONG)(dbl * 1000 * 1000 * 1000 + 0.5);
		default:
			return ULONG_MAX;
		}
	}
	return (ULONG)(dbl + 0.5); //default is bytes
}

ULONG CSearchParamsWnd::GetSearchAttrLength(const CString &rstrExpr)
{
	CString strExpr(rstrExpr);
	strExpr.Trim();

	UINT hour, min, sec;
	if (_stscanf(strExpr, _T("%u : %u : %u"), &hour, &min, &sec) == 3)
		return HR2S(hour) + MIN2S(min) + sec;
	if (_stscanf(strExpr, _T("%u : %u"), &min, &sec) == 2)
		return MIN2S(min) + sec;

	LPTSTR endptr = NULL;
	double dbl = _tcstod(strExpr, &endptr);
	if (errno == ERANGE || dbl < 0) //no negative values
		return ULONG_MAX;
	if (endptr && *endptr != _T('\0')) {
		while (*endptr == _T(' '))
			++endptr;

		switch (_totlower(*endptr)) {
		case _T('\0'): //not specified
		case _T('s'): //seconds
			break;
		case _T('m'): //minutes
			return (ULONG)(MIN2S(dbl) + 0.5);
		case _T('h'): //hours
			return (ULONG)(HR2S(dbl) + 0.5);
		default:
			return ULONG_MAX;
		}
	}
	return (ULONG)(dbl + 0.5); //default is seconds
}

void CSearchParamsWnd::ParamErrorBox(EOptsRows eRow)
{
	CString strError(GetResString(_T("SEARCH_EXPRERROR")));
	strError += _T("\n\n");
	strError.AppendFormat(GetResString(_T("SEARCH_ATTRERR")), (LPCTSTR)m_ctlOpts.GetItemText(eRow, 0));
	CDarkMode::MessageBox(strError, MB_ICONWARNING | MB_HELP, eMule_FAQ_Search - HID_BASE_PROMPT);
}

void CSearchParamsWnd::WipeOptionFields()
{
	for (int i = m_ctlOpts.GetItemCount(); --i >= 0;)
		m_ctlOpts.SetItemText(i, 1, EMPTY);
}

SSearchParams* CSearchParamsWnd::GetParameters()
{
	CString strExpression;
	m_ctlName.GetWindowText(strExpression);

	if (!IsValidEd2kString(strExpression.Trim())) {
		CString sBadExpr(GetResString(_T("SEARCH_EXPRERROR")));
		sBadExpr.AppendFormat(_T("\n\n%s"), (LPCTSTR)GetResString(_T("SEARCH_INVALIDCHAR")));
		CDarkMode::MessageBox(sBadExpr, MB_ICONWARNING | MB_HELP, eMule_FAQ_Search - HID_BASE_PROMPT);
		return NULL;
	}

	CString strFileType;
	int iItem = m_ctlFileType.GetCurSel();
	if (iItem != CB_ERR) {
		LPCTSTR pszED2KFileType = (LPCTSTR)m_ctlFileType.GetItemDataPtr(iItem);
		ASSERT(pszED2KFileType != NULL);
		strFileType = pszED2KFileType;
	}

	const CString &strMinSize(m_ctlOpts.GetItemText(orMinSize, 1));
	uint64 ullMinSize = GetSearchAttrSize(strMinSize);
	if (ullMinSize == _UI64_MAX) {
		ParamErrorBox(orMinSize);
		return NULL;
	}

	const CString &strMaxSize(m_ctlOpts.GetItemText(orMaxSize, 1));
	uint64 ullMaxSize = GetSearchAttrSize(strMaxSize);
	if (ullMaxSize == _UI64_MAX) {
		ParamErrorBox(orMaxSize);
		return NULL;
	}

	if (ullMaxSize < ullMinSize) {
		ullMaxSize = 0; // TODO: Create a message box for that
		m_ctlOpts.SetItemText(orMaxSize, 1, EMPTY);
	}

	CString strExtension;
	if ((m_ctlOpts.GetItemData(orExtension) & 1) == 0) {
		strExtension = m_ctlOpts.GetItemText(orExtension, 1).Trim();
		if (strExtension[0] == _T('.')) {
			strExtension.Delete(0, 1);
			m_ctlOpts.SetItemText(orExtension, 1, strExtension);
		}
	}

	UINT uAvailability = 0;
	if ((m_ctlOpts.GetItemData(orAvailability) & 1) == 0) {
		CString strAvailability(m_ctlOpts.GetItemText(orAvailability, 1));
		uAvailability = GetSearchAttrNumber(strAvailability);
		if (uAvailability == _UI32_MAX) {
			ParamErrorBox(orAvailability);
			return NULL;
		}
		if (uAvailability > 1000000) {
			uAvailability = 1000000;
			strAvailability.Format(_T("%u"), uAvailability);
			m_ctlOpts.SetItemText(orAvailability, 1, strAvailability);
		}
	}

	UINT uComplete = 0;
	if ((m_ctlOpts.GetItemData(orCompleteSources) & 1) == 0) {
		CString strComplete(m_ctlOpts.GetItemText(orCompleteSources, 1));
		uComplete = GetSearchAttrNumber(strComplete);
		if (uComplete == UINT_MAX) {
			ParamErrorBox(orCompleteSources);
			return NULL;
		}
		if (uComplete > 1000000) {
			uComplete = 1000000;
			strComplete.Format(_T("%u"), uComplete);
			m_ctlOpts.SetItemText(orCompleteSources, 1, strComplete);
		}
	}

	CString strCodec;
	if ((m_ctlOpts.GetItemData(orCodec) & 1) == 0)
		strCodec = m_ctlOpts.GetItemText(orCodec, 1).Trim();

	UINT ulMinBitrate = 0;
	if ((m_ctlOpts.GetItemData(orBitrate) & 1) == 0) {
		CString strMinBitrate(m_ctlOpts.GetItemText(orBitrate, 1));
		ulMinBitrate = GetSearchAttrNumber(strMinBitrate);
		if (ulMinBitrate == UINT_MAX) {
			ParamErrorBox(orBitrate);
			return NULL;
		}
		if (ulMinBitrate > 1000000) {
			ulMinBitrate = 1000000;
			strMinBitrate.Format(_T("%u"), ulMinBitrate);
			m_ctlOpts.SetItemText(orBitrate, 1, strMinBitrate);
		}
	}

	UINT ulMinLength = 0;
	if ((m_ctlOpts.GetItemData(orLength) & 1) == 0) {
		const CString &strMinLength(m_ctlOpts.GetItemText(orLength, 1));
		ulMinLength = GetSearchAttrLength(strMinLength);
		if (ulMinLength == UINT_MAX) {
			ParamErrorBox(orLength);
			return NULL;
		}
		if (ulMinLength > DAY2S(1)) {
			ulMinLength = DAY2S(1);
			m_ctlOpts.SetItemText(orLength, 1, SecToTimeLength(ulMinLength));
		}
	}

	SSearchParams *pParams = new SSearchParams;
	pParams->strExpression = strExpression;
	pParams->eType = (ESearchType)m_ctlMethod.GetCurSel();
	pParams->strFileType = strFileType;
	pParams->strMinSize = strMinSize;
	pParams->ullMinSize = ullMinSize;
	pParams->strMaxSize = strMaxSize;
	pParams->ullMaxSize = ullMaxSize;
	pParams->uAvailability = uAvailability;
	pParams->strExtension = strExtension;
	pParams->uComplete = uComplete;
	pParams->strCodec = strCodec;
	pParams->uiMinBitrate = ulMinBitrate;
	pParams->uiMinLength = ulMinLength;
	if ((m_ctlOpts.GetItemData(orTitle) & 1) == 0)
		pParams->strTitle = m_ctlOpts.GetItemText(orTitle, 1).Trim();
	if ((m_ctlOpts.GetItemData(orAlbum) & 1) == 0)
		pParams->strAlbum = m_ctlOpts.GetItemText(orAlbum, 1).Trim();
	if ((m_ctlOpts.GetItemData(orArtist) & 1) == 0)
		pParams->strArtist = m_ctlOpts.GetItemText(orArtist, 1).Trim();

	return pParams;
}

BOOL CSearchParamsWnd::OnHelpInfo(HELPINFO*)
{
	theApp.ShowHelp(eMule_FAQ_GUI_Search);
	return TRUE;
}

void CSearchParamsWnd::ProcessEd2kSearchLinkRequest(const CString &strSearchTerm)
{
	// We have an ED2K search link, asking us to search for stuff
	// For now we handle this very basic: Put the search term into the params window (so the users can also see it)
	// And if we are connected, start the search otherwise not.
	if (strSearchTerm.IsEmpty()) {
		ASSERT(0);
		return;
	}
	OnBnClickedSearchReset();
	m_ctlName.SetWindowText(strSearchTerm);
	if (theApp.IsConnected()) {
		if ((!theApp.IsConnected(true, false) && (ESearchType)m_ctlMethod.GetCurSel() == SearchTypeKademlia)
			|| (!theApp.IsConnected(false, true) && ((ESearchType)m_ctlMethod.GetCurSel() == SearchTypeEd2kServer || (ESearchType)m_ctlMethod.GetCurSel() == SearchTypeEd2kGlobal)))
		{
			m_ctlMethod.SetCurSel(0);
		}
		OnBnClickedStart();
	}
}

void CSearchParamsWnd::OnLButtonDown(UINT nFlags, CPoint point)
{
	CWnd* pChild = ChildWindowFromPoint(point, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT | CWP_SKIPDISABLED);
	if (pChild && pChild != this)
		CDialogBar::OnLButtonDown(nFlags, point); // allow normal clicks on child controls
	// ignore drag on empty area to prevent floating
}
