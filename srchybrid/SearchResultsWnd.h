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
#pragma once
#include "ResizableLib\ResizableFormView.h"
#include "SearchListCtrl.h"
#include "ButtonsTabCtrl.h"
#include "ClosableTabCtrl.h"
#include "DropDownButton.h"
#include "IconStatic.h"
#include "EditX.h"
#include "EditDelayed.h"
#include "ComboBoxEx2.h"
#include "ListCtrlEditable.h"
#include "ProgressCtrlX.h"
#include "ToolTipCtrlX.h"
#include "UpdownClient.h"

class CCustomAutoComplete;
class Packet;
class CSafeMemFile;
class CSearchParamsWnd;
struct SSearchParams;


///////////////////////////////////////////////////////////////////////////////
// CSearchResultsSelector

class CSearchResultsSelector : public CClosableTabCtrl
{
public:
	CSearchResultsSelector();
	void InitToolTips();
	void UpdateTabToolTips(int tab = -1);

protected:
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);

	CUpDownClient* m_SelectedClient;
	CToolTipCtrlX m_tooltipTabs;
	int m_nTooltipTabIndex;

	CUpDownClient* GetClientForTab(int iTab) const;
	CString BuildSearchTooltip(int iTab) const;
	CString BuildSharedFilesTooltip(int iTab) const;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnContextMenu(CWnd*, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnSize(UINT nType, int cx, int cy);
};

///////////////////////////////////////////////////////////////////////////////
// CSearchResultsWnd dialog

class CSearchResultsWnd : public CResizableFormView
{
	DECLARE_DYNCREATE(CSearchResultsWnd)

	enum
	{
		IDD = IDD_SEARCH
	};
	void	NoTabItems();

public:
	explicit CSearchResultsWnd(CWnd *pParent = NULL);   // standard constructor
	virtual	~CSearchResultsWnd();
	CSearchResultsWnd(const CSearchResultsWnd&) = delete;
	CSearchResultsWnd& operator=(const CSearchResultsWnd&) = delete;

	CSearchListCtrl searchlistctrl;
	CSearchResultsSelector searchselect;
	CStringArray m_astrFilter;
	CSearchParamsWnd *m_pwndParams;

	void	Localize();

	void	StartSearch(SSearchParams *pParams);
	bool	SearchMore();
	void	CancelSearch(uint32 uSearchID = 0);

	bool	DoNewEd2kSearch(SSearchParams *pParams);
	void	CancelEd2kSearch();
	bool	IsLocalEd2kSearchRunning() const	{ return (m_uTimerLocalServer != 0); }
	bool	IsGlobalEd2kSearchRunning() const	{ return (global_search_timer != 0); }
	void	LocalEd2kSearchEnd(UINT count, bool bMoreResultsAvailable);
	void	AddEd2kSearchResults(UINT count);
	void	SetNextSearchID(uint32 uNextID)		{ m_nEd2kSearchID = uNextID; }
	uint32	GetNextSearchID()					{ return ++m_nEd2kSearchID; }

	bool	DoNewKadSearch(SSearchParams *pParams);
	void	CancelKadSearch(uint32 uSearchID);

	bool	CanSearchRelatedFiles() const;
	void	SearchRelatedFiles(CPtrList &listFiles);

	void	DownloadSelected();
	void	DownloadSelected(bool bPaused, bool bBypassDownloadChecker = false);

	bool	CanDeleteSearches() const			{ return (searchselect.GetItemCount() > 0); };
	void	DeleteSearch(uint32 uSearchID);
	void	DeleteAllSearches();
	void	DeleteSelectedSearch();

	bool	CreateNewTab(SSearchParams *pParams, bool bActiveIcon = true, bool bShowResults = true);
	void	ShowSearchSelector(bool visible);
	int		GetSelectedCat()					{ return m_cattabs.GetCurSel(); }
	void	UpdateCatTabs();

	SSearchParams* GetSearchResultsParams(uint32 uSearchID) const;

	uint32	GetFilterColumn() const				{ return m_nFilterColumn; }

	uint32	CleanUpSearchResults(int iTab); // Removes known/spam/blacklisted results for the given search tab
	uint32 CleanUpAllSearchResults(); // Iterates over every search tab and removes known/spam/blacklisted results

	uint32 RecheckAllSearchResults();

	BOOL MergeSearchResults(uint32 uFromSearchID, uint32 uToSearchID);
	uint32 MergeAllSearchResults();
	uint32 m_uMergeFromSearchID;
	bool m_bMergeFromSearchIDHasBeenSet;

	CProgressCtrlX searchprogress;

protected:
	CHeaderCtrl m_ctlSearchListHeader;
	CEditDelayed m_ctlFilter;
	CButton		m_ctlOpenParamsWnd;
	CImageList	m_imlSearchResults;
	CButtonsTabCtrl	m_cattabs;
public:
	CDropDownButton	m_btnSearchListMenu;
protected:
	Packet		*m_searchpacket;
	UINT_PTR	global_search_timer;
	UINT_PTR	m_uTimerLocalServer;
	uint32		m_nEd2kSearchID;
	uint32		m_nFilterColumn;
	unsigned	m_servercount;
	int			m_iSentMoreReq;
	bool		m_b64BitSearchPacket;
	bool		m_globsearch;
	bool		m_cancelled;
	CStringArray	m_astrFilterTemp;
	bool			m_bColumnDiff;
	CString			m_strFullFilterExpr;
	uint32			m_nFilterColumnLastApplied;

	bool StartNewSearch(SSearchParams *pParams);
	void SearchStarted();
	void SearchCancelled(uint32 uSearchID);
	void ShowResults(const SSearchParams *pParams);
	void SetAllIcons();
	void SetSearchResultsIcon(uint32 uSearchID, int iImage);
	void SetActiveSearchResultsIcon(uint32 uSearchID);
	void SetInactiveSearchResultsIcon(uint32 uSearchID);


	virtual void OnInitialUpdate();
	virtual BOOL PreTranslateMessage(MSG *pMsg);
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
	virtual void DoDataExchange(CDataExchange *pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
	afx_msg void OnDblClkSearchList(LPNMHDR, LRESULT *pResult);
	afx_msg void OnSelChangeTab(LPNMHDR, LRESULT *pResult);
	afx_msg void OnSelChangingTab(LPNMHDR, LRESULT *pResult);
	afx_msg LRESULT OnCloseTab(WPARAM wParam, LPARAM);
	afx_msg LRESULT OnDblClickTab(WPARAM wParam, LPARAM);
	afx_msg void OnDestroy();
	afx_msg void OnSysColorChange();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnBnClickedDownloadSelected();
	afx_msg void OnBnClickedClearAll();
	afx_msg void OnClose();
	afx_msg BOOL OnHelpInfo(HELPINFO*);
	afx_msg LRESULT OnIdleUpdateCmdUI(WPARAM, LPARAM);
	afx_msg void OnBnClickedOpenParamsWnd();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg LRESULT OnChangeFilter(WPARAM wParam, LPARAM lParam);
	afx_msg void OnSearchListMenuBtnDropDown(LPNMHDR, LRESULT*);
	afx_msg void OnTabMovement(LPNMHDR, LRESULT*);
	afx_msg void OnBnClickedComplete();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
};
