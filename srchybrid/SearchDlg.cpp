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
#include "emuleDlg.h"
#include "SearchDlg.h"
#include "SearchResultsWnd.h"
#include "OtherFunctions.h"
#include "HelpIDs.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define	IDBAR_SEARCH_PARAMS		(AFX_IDW_CONTROLBAR_FIRST + 32 + 1)	// do NOT change that ID, if not absolutely needed (it is stored by MFC in the bar profile!)
#define	SEARCH_PARAMS_PROFILE	_T("SearchFrmBarState")

IMPLEMENT_DYNCREATE(CSearchDlg, CFrameWnd)

BEGIN_MESSAGE_MAP(CSearchDlg, CFrameWnd)
	ON_WM_CREATE()
	ON_WM_SHOWWINDOW()
	ON_WM_SETFOCUS()
	ON_WM_CLOSE()
	ON_WM_SYSCOMMAND()
	ON_WM_HELPINFO()
END_MESSAGE_MAP()

CSearchDlg::CSearchDlg()
	: m_pwndResults()
{
}

BOOL CSearchDlg::CreateWnd(CWnd *pParent)
{
	// *) The initial size of that frame window must not exceed the window size of the
	//    dialog resource template of the client window (the search results window).
	// *) The dialog resource template's window size (the search results window) must not
	//	  exceed the minimum client area of the frame window.
	// Otherwise we may get scrollbars in the search results window
	return CFrameWnd::Create(NULL, _T("Search"), WS_CHILD | WS_CLIPCHILDREN, CRect(0, 0, 50, 50), pParent, NULL, 0, NULL);
}

int CSearchDlg::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CFrameWnd::OnCreate(lpCreateStruct) == -1)
		return -1;

	CCreateContext context;
	context.m_pCurrentFrame = this;
	context.m_pCurrentDoc = NULL;
	context.m_pNewViewClass = RUNTIME_CLASS(CSearchResultsWnd);
	context.m_pNewDocTemplate = NULL;
#ifdef _DEBUG
	CFrameDoc dummy;
	context.m_pCurrentDoc = &dummy;
#endif
	m_pwndResults = static_cast<CSearchResultsWnd*>(CreateView(&context));
	m_wndParams.m_searchdlg = m_pwndResults;
	m_pwndResults->ModifyStyle(WS_BORDER, 0);
	m_pwndResults->ModifyStyleEx(WS_EX_CLIENTEDGE, WS_EX_STATICEDGE);

	m_wndParams.Create(this, IDD_SEARCH_PARAMS , WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_SIZE_FIXED , IDBAR_SEARCH_PARAMS);
	ASSERT(m_wndParams.GetStyle() & WS_CLIPSIBLINGS);
	ASSERT(m_wndParams.GetStyle() & WS_CLIPCHILDREN);
	m_wndParams.SetWindowText(GetResString(_T("SEARCHPARAMS")));
	m_wndParams.EnableDocking(CBRS_ALIGN_ANY);

	EnableDocking(CBRS_ALIGN_ANY);
	DockControlBar(&m_wndParams, AFX_IDW_DOCKBAR_TOP, (LPRECT)NULL);

	m_pwndResults->m_pwndParams = &m_wndParams;
	m_pwndResults->SendMessage(WM_INITIALUPDATE);

	LoadBarState(SEARCH_PARAMS_PROFILE);
	DockParametersWnd(); // Too many bug reports about vanished search parameters window. Force to dock.
	OpenParametersWnd();
	Localize();

	return 0;
}

void CSearchDlg::OnClose()
{
	SaveBarState(SEARCH_PARAMS_PROFILE);
	if (theApp.IsClosing())
		return;
	CFrameWnd::OnClose();
}


void CSearchDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CFrameWnd::OnShowWindow(bShow, nStatus);
	if (m_wndParams.IsFloating()) {
		DockParametersWnd(); // Too much bug reports about vanished search parameters window. Force to dock.
	}
}

void CSearchDlg::OnSetFocus(CWnd *pOldWnd)
{
	CFrameWnd::OnSetFocus(pOldWnd);
	if (m_wndParams.m_hWnd)
		m_wndParams.SetFocus();
}





void CSearchDlg::DockParametersWnd()
{
	if (m_wndParams.IsFloating()) {
		UINT uMRUDockID = AFX_IDW_DOCKBAR_TOP;
		if (m_wndParams.m_pDockContext)
			uMRUDockID = m_wndParams.m_pDockContext->m_uMRUDockID;
		DockControlBar(&m_wndParams, uMRUDockID);
	}
}

int CSearchDlg::GetSelectedCat()
{
	return m_pwndResults->GetSelectedCat();
}

void CSearchDlg::UpdateCatTabs()
{
	m_pwndResults->UpdateCatTabs();
}

void CSearchDlg::SetToolTipsDelay(UINT uDelay)
{
	CToolTipCtrl *tooltip = m_pwndResults->searchlistctrl.GetToolTips();
	if (tooltip)
		tooltip->SetDelayTime(TTDT_INITIAL, uDelay);
}

void CSearchDlg::RemoveResult(CSearchFile *pFile, bool bUpdateTabCount)
{
	m_pwndResults->searchlistctrl.RemoveResult(pFile, bUpdateTabCount);
}

bool CSearchDlg::CreateNewTab(SSearchParams* pParams, bool bActiveIcon, bool bShowResults)
{
	return m_pwndResults->CreateNewTab(pParams, bActiveIcon, bShowResults);
}

SSearchParams* CSearchDlg::GetSearchParamsBySearchID(uint32 nSearchID)
{
	return m_pwndResults->GetSearchResultsParams(nSearchID);
}

void CSearchDlg::LocalEd2kSearchEnd(UINT nCount, bool bMoreResultsAvailable)
{
	m_pwndResults->LocalEd2kSearchEnd(nCount, bMoreResultsAvailable);
}

void CSearchDlg::CancelEd2kSearch()
{
	m_pwndResults->CancelEd2kSearch();
}

void CSearchDlg::CancelKadSearch(UINT uSearchID)
{
	m_pwndResults->CancelKadSearch(uSearchID);
}

void CSearchDlg::SetNextSearchID(uint32 uNextID)
{
	m_pwndResults->SetNextSearchID(uNextID);
}

void CSearchDlg::AddEd2kSearchResults(UINT nCount)
{
	m_pwndResults->AddEd2kSearchResults(nCount);
}

void CSearchDlg::Localize()
{
	m_pwndResults->Localize();
	m_wndParams.Localize();
}

void CSearchDlg::CreateMenus()
{
	m_pwndResults->searchlistctrl.CreateMenus();
}

bool CSearchDlg::DoNewKadSearch(SSearchParams *pParams)
{
	return m_pwndResults->DoNewKadSearch(pParams);
}

bool CSearchDlg::DoNewEd2kSearch(SSearchParams *pParams)
{
	return m_pwndResults->DoNewEd2kSearch(pParams);
}


void CSearchDlg::DeleteAllSearches()
{
	m_pwndResults->DeleteAllSearches();
}

bool CSearchDlg::CanDeleteSearches() const
{
	return m_pwndResults->CanDeleteSearches();

}

void CSearchDlg::DeleteSearch(uint32 nSearchID)
{
	m_pwndResults->DeleteSearch(nSearchID);
}

void CSearchDlg::DownloadSelected(bool bPaused, bool bBypassDownloadChecker)
{
	m_pwndResults->DownloadSelected(bPaused, bBypassDownloadChecker);
}

void CSearchDlg::DownloadSelected()
{
	m_pwndResults->DownloadSelected();
}

CClosableTabCtrl& CSearchDlg::GetSearchSelector() const
{
	return m_pwndResults->searchselect;
}

void CSearchDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if (nID == SC_KEYMENU) {
		if (lParam == EMULE_HOTMENU_ACCEL)
			theApp.emuledlg->SendMessage(WM_COMMAND, IDC_HOTMENU);
		else
			theApp.emuledlg->SendMessage(WM_SYSCOMMAND, nID, lParam);
	} else
		CFrameWnd::OnSysCommand(nID, lParam);
}

void CSearchDlg::UpdateSearch(CSearchFile *pSearchFile)
{
	if (m_pwndResults)
		m_pwndResults->searchlistctrl.UpdateSearch(pSearchFile);
}

bool CSearchDlg::CanSearchRelatedFiles() const
{
	return m_pwndResults->CanSearchRelatedFiles();
}

void CSearchDlg::SearchRelatedFiles(CPtrList &listFiles)
{
	m_pwndResults->SearchRelatedFiles(listFiles);
}

BOOL CSearchDlg::PreTranslateMessage(MSG *pMsg)
{
	if (pMsg->message == WM_KEYDOWN && GetKeyState(VK_CONTROL) < 0) {
		// Don't handle Ctrl+Tab in this window. It will be handled by main window.
		if (pMsg->wParam == VK_TAB) {
			// UGLY: Because this window is a 'CFrameWnd' (rather than a 'CDialog' like
			// the other eMule main windows) we can not use MFC's message routing.
			// Need to explicitly send that message to the main window.
			theApp.emuledlg->PostMessage(pMsg->message, pMsg->wParam, pMsg->lParam);
			return TRUE;
		}
		if (pMsg->wParam == 'W') {
			m_pwndResults->DeleteSelectedSearch();
			return TRUE;
		}
	}
	return CFrameWnd::PreTranslateMessage(pMsg);
}

BOOL CSearchDlg::OnHelpInfo(HELPINFO*)
{
	theApp.ShowHelp(eMule_FAQ_GUI_Search);
	return TRUE;
}
