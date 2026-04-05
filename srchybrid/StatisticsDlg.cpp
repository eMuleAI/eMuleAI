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
#include "StatisticsDlg.h"
#include "UploadQueue.h"
#include "Statistics.h"
#include "emuledlg.h"
#include "UpDownClient.h"
#include "WebServer.h"
#include "DownloadQueue.h"
#include "ClientList.h"
#include "Preferences.h"
#include "ListenSocket.h"
#include "ServerList.h"
#include "SharedFileList.h"
#include "UserMsgs.h"
#include "HelpIDs.h"
#include "Kademlia/Kademlia/kademlia.h"
#include "Kademlia/Kademlia/Prefs.h"
#include "kademlia/kademlia/UDPFirewallTester.h"
#include "eMuleAI/DarkMode.h"
#include "ToolTipCtrlX.h"
#include <vector>
#include <algorithm>


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#ifdef _DEBUG
extern _CRT_ALLOC_HOOK g_pfnPrevCrtAllocHook;
#endif


// CStatisticsDlg dialog

IMPLEMENT_DYNAMIC(CStatisticsDlg, CDialog)

BEGIN_MESSAGE_MAP(CStatisticsDlg, CResizableDialog)
	ON_WM_SHOWWINDOW()
	ON_WM_SIZE()
	ON_BN_CLICKED(IDC_BNMENU, OnMenuButtonClicked)
	ON_WM_SYSCOLORCHANGE()
	ON_WM_CTLCOLOR()
	ON_STN_DBLCLK(IDC_SCOPE_D, OnStnDblclickScopeD)
	ON_STN_DBLCLK(IDC_SCOPE_U, OnStnDblclickScopeU)
	ON_STN_DBLCLK(IDC_STATSSCOPE, OnStnDblclickStatsscope)
	ON_MESSAGE(UM_OSCOPEPOSITION, OnOscopePositionMsg)
	ON_WM_HELPINFO()
END_MESSAGE_MAP()

// Returns whether debug statistics (per-file CRT block counters) should be shown in the tree.
// Controlled via env var: EMULE_SHOW_DEBUG_STATS=1 (or Y/True). Default: hidden.
static bool IsDebugStatsVisible()
{
#ifdef _DEBUG
	static int s_cached = -1;
	if (s_cached < 0) {
		TCHAR buf[8] = { 0 };
		DWORD n = ::GetEnvironmentVariable(_T("EMULE_SHOW_DEBUG_STATS"), buf, _countof(buf));
		if (n > 0) {
			TCHAR c = buf[0];
			s_cached = (c == _T('1') || c == _T('Y') || c == _T('y') || c == _T('T') || c == _T('t')) ? 1 : 0;
		} else {
			s_cached = 0; // default: hidden
		}
	}
	return s_cached != 0;
#else
	return false;
#endif
}

CStatisticsDlg::CStatisticsDlg(CWnd *pParent /*=NULL*/)
	: CResizableDialog(CStatisticsDlg::IDD, pParent)
	, m_DownloadOMeter(3)
	, m_UploadOMeter(5)
	, m_Statistics(4)
	, m_oldcx()
	, m_oldcy()
	, m_TimeToolTips()
{
}

CStatisticsDlg::~CStatisticsDlg()
{
	delete m_TimeToolTips;
#ifdef _DEBUG
	for (POSITION pos = blockFiles.GetStartPosition(); pos != NULL;) {
		const unsigned char *fileName;
		HTREEITEM *pTag;
		blockFiles.GetNextAssoc(pos, fileName, pTag);
		delete pTag;
	}
#endif
}

void CStatisticsDlg::DoDataExchange(CDataExchange *pDX)
{
	CResizableDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_STATTREE, m_stattree);
}

void CStatisticsDlg::OnSysColorChange()
{
	CResizableDialog::OnSysColorChange();
	SetAllIcons();
}

void CStatisticsDlg::SetAllIcons()
{
	InitWindowStyles(this);

	CImageList iml;
	iml.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	iml.Add(CTempIconLoader(_T("StatsGeneric")));		// Dots & Arrow (Default icon for stats)
	iml.Add(CTempIconLoader(_T("TransferUpDown")));		// Transfer
	iml.Add(CTempIconLoader(_T("Connection")));			// Connection
	iml.Add(CTempIconLoader(_T("StatsClients")));		// Clients
	iml.Add(CTempIconLoader(_T("Server")));				// Server
	iml.Add(CTempIconLoader(_T("SharedFiles")));		// Shared Files
	iml.Add(CTempIconLoader(_T("Upload")));				// Transfer > Upload
	iml.Add(CTempIconLoader(_T("Download")));			// Transfer > Download
	iml.Add(CTempIconLoader(_T("StatsDetail")));		// Session Sections
	iml.Add(CTempIconLoader(_T("StatsCumulative")));	// Cumulative Sections
	iml.Add(CTempIconLoader(_T("StatsRecords")));		// Records
	iml.Add(CTempIconLoader(_T("TransferUpDown")));		// Connection > General
	iml.Add(CTempIconLoader(_T("StatsTime")));			// Time Section
	iml.Add(CTempIconLoader(_T("StatsProjected")));		// Time > Averages and Projections
	iml.Add(CTempIconLoader(_T("StatsDay")));			// Time > Averages and Projections > Daily
	iml.Add(CTempIconLoader(_T("StatsMonth")));			// Time > Averages and Projections > Monthly
	iml.Add(CTempIconLoader(_T("StatsYear")));			// Time > Averages and Projections > Yearly
	iml.Add(CTempIconLoader(_T("HardDisk")));			// Disk space
	m_stattree.SetImageList(&iml, TVSIL_NORMAL);
	imagelistStatTree.DeleteImageList();
	imagelistStatTree.Attach(iml.Detach());

	COLORREF crBk = GetCustomSysColor(COLOR_WINDOW);
	COLORREF crFg = GetCustomSysColor(COLOR_WINDOWTEXT);

	theApp.LoadSkinColorAlt(_T("StatisticsTvBk"), _T("DefLvBk"), crBk);
	theApp.LoadSkinColorAlt(_T("StatisticsTvFg"), _T("DefLvFg"), crFg);

	m_stattree.SetBkColor(crBk);
	m_stattree.SetTextColor(crFg);
	// can't use 'TVM_SETLINECOLOR' because the color may differ from the one used in "StatsGeneric" item image.
}

BOOL CStatisticsDlg::OnInitDialog()
{
	CResizableDialog::OnInitDialog();
	EnableWindow(FALSE);
	SetAllIcons();
	m_bTreepaneHidden = false;

	if (theApp.m_fontSymbol.m_hObject) {
		GetDlgItem(IDC_BNMENU)->SetFont(&theApp.m_fontSymbol);
		SetDlgItemText(IDC_BNMENU, _T("6")); // show a down-arrow
	}

	// Win98: Explicitly set to Unicode to receive Unicode notifications.
	m_stattree.SendMessage(CCM_SETUNICODEFORMAT, TRUE);
	if (thePrefs.GetUseSystemFontForMainControls())
		m_stattree.SendMessage(WM_SETFONT, NULL, FALSE);
	CreateMyTree();

	// Setup download-scope
	CRect rcDown;
	GetDlgItem(IDC_SCOPE_D)->GetWindowRect(rcDown);
	GetDlgItem(IDC_SCOPE_D)->DestroyWindow();
	ScreenToClient(rcDown);
	m_DownloadOMeter.CreateWnd(WS_VISIBLE | WS_CHILD, rcDown, this, IDC_SCOPE_D);
	SetARange(true, thePrefs.GetMaxGraphDownloadRate());
	m_DownloadOMeter.SetYUnits(GetResString(_T("KBYTESPERSEC")));

	// Setup upload-scope
	CRect rcUp;
	GetDlgItem(IDC_SCOPE_U)->GetWindowRect(rcUp);
	GetDlgItem(IDC_SCOPE_U)->DestroyWindow();
	ScreenToClient(rcUp);
	// compensate rounding errors due to dialog units, make each of the 3 panes the same height
	rcUp.top = rcDown.bottom + 4;
	rcUp.bottom = rcUp.top + rcDown.Height();
	m_UploadOMeter.CreateWnd(WS_VISIBLE | WS_CHILD, rcUp, this, IDC_SCOPE_U);
	SetARange(false, thePrefs.GetMaxGraphUploadRate(true));
	m_UploadOMeter.SetYUnits(GetResString(_T("KBYTESPERSEC")));

	// Setup additional graph scope
	CRect rcConn;
	GetDlgItem(IDC_STATSSCOPE)->GetWindowRect(rcConn);
	GetDlgItem(IDC_STATSSCOPE)->DestroyWindow();
	ScreenToClient(rcConn);
	// compensate rounding errors due to dialog units, make each of the 3 panes the same height
	rcConn.top = rcUp.bottom + 4;
	rcConn.bottom = rcConn.top + rcDown.Height();
	m_Statistics.CreateWnd(WS_VISIBLE | WS_CHILD, rcConn, this, IDC_STATSSCOPE);
	m_Statistics.SetRanges(0, thePrefs.GetStatsMax());
	m_Statistics.autofitYscale = false;
	// Set the trend ratio of the Active Connections trend in the Connection Statistics scope.
	m_Statistics.SetTrendRatio(0, thePrefs.GetStatsConnectionsGraphRatio());

	m_Statistics.SetYUnits(EMPTY);
	m_Statistics.SetXUnits(GetResString(_T("TIME")));

	RepaintMeters();
	m_Statistics.SetBackgroundColor(thePrefs.GetStatsColor(0));
	m_Statistics.SetGridColor(thePrefs.GetStatsColor(1));

	m_DownloadOMeter.InvalidateCtrl();
	m_UploadOMeter.InvalidateCtrl();
	m_Statistics.InvalidateCtrl();

	if (thePrefs.GetStatsInterval() == 0)
		m_stattree.EnableWindow(false);

	UpdateData(FALSE);

	EnableWindow(TRUE);

	m_ilastMaxConnReached = 0;
	CRect rcW;
	GetWindowRect(rcW);
	ScreenToClient(rcW);

	RECT rcTree;
	m_stattree.GetWindowRect(&rcTree);
	ScreenToClient(&rcTree);
	m_DownloadOMeter.GetWindowRect(rcDown);
	ScreenToClient(rcDown);
	m_UploadOMeter.GetWindowRect(&rcUp);
	ScreenToClient(&rcUp);

	RECT rcStat;
	m_Statistics.GetWindowRect(&rcStat);
	ScreenToClient(&rcStat);

	//vertical splitter
	CRect rcSpl(rcTree.right, rcW.top + 2, rcTree.right + 4, rcW.bottom - 5);
	m_wndSplitterstat.CreateWnd(WS_CHILD | WS_VISIBLE, rcSpl, this, IDC_SPLITTER_STAT);
	int PosStatVinitX = rcSpl.left;
	int PosStatVnewX = thePrefs.GetSplitterbarPositionStat() * rcW.Width() / 100;
	int maxX = rcW.right - 13;
	int minX = rcW.left + 8;
	if (thePrefs.GetSplitterbarPositionStat() > 90)
		PosStatVnewX = maxX;
	else if (thePrefs.GetSplitterbarPositionStat() < 10)
		PosStatVnewX = minX;
	rcSpl.left = PosStatVnewX;
	rcSpl.right = PosStatVnewX + 4;
	m_wndSplitterstat.MoveWindow(rcSpl);

	//HR splitter
	rcSpl = { rcDown.left, rcDown.bottom, rcDown.right, rcDown.bottom + 4 };
	m_wndSplitterstat_HR.CreateWnd(WS_CHILD | WS_VISIBLE, rcSpl, this, IDC_SPLITTER_STAT_HR);
	int PosStatVinitZ = rcSpl.top;
	int PosStatVnewZ = thePrefs.GetSplitterbarPositionStat_HR() * rcW.Height() / 100;
	int maxZ = rcW.bottom - 14;
	int minZ = 0;
	if (thePrefs.GetSplitterbarPositionStat_HR() > 90)
		PosStatVnewZ = maxZ;
	else if (thePrefs.GetSplitterbarPositionStat_HR() < 10)
		PosStatVnewZ = minZ;
	rcSpl.top = PosStatVnewZ;
	rcSpl.bottom = PosStatVnewZ + 4;
	m_wndSplitterstat_HR.MoveWindow(rcSpl);

	//HL splitter
	rcSpl = { rcUp.left, rcUp.bottom, rcUp.right, rcUp.bottom + 4 };
	m_wndSplitterstat_HL.CreateWnd(WS_CHILD | WS_VISIBLE, rcSpl, this, IDC_SPLITTER_STAT_HL);
	int PosStatVinitY = rcSpl.top;
	int PosStatVnewY = thePrefs.GetSplitterbarPositionStat_HL() * rcW.Height() / 100;
	int maxY = rcW.bottom - 9;
	int minY = 10;
	if (thePrefs.GetSplitterbarPositionStat_HL() > 90)
		PosStatVnewY = maxY;
	else if (thePrefs.GetSplitterbarPositionStat_HL() < 10)
		PosStatVnewY = minY;
	rcSpl.top = PosStatVnewY;
	rcSpl.bottom = PosStatVnewY + 4;
	m_wndSplitterstat_HL.MoveWindow(rcSpl);

	DoResize_V(PosStatVnewX - PosStatVinitX);
	DoResize_HL(PosStatVnewY - PosStatVinitY);
	DoResize_HR(PosStatVnewZ - PosStatVinitZ);

	Localize();
	ShowStatistics(true);

	m_TimeToolTips = new CToolTipCtrlX();
	m_TimeToolTips->Create(this);
	m_TimeToolTips->AddTool(GetDlgItem(IDC_SCOPE_D), EMPTY);
	m_TimeToolTips->AddTool(GetDlgItem(IDC_SCOPE_U), EMPTY);
	m_TimeToolTips->AddTool(GetDlgItem(IDC_STATSSCOPE), EMPTY);
	// Any Autopop-Time which is specified higher than ~30 sec. will get reset to 5 sec.
	m_TimeToolTips->SetDelayTime(TTDT_AUTOPOP, SEC2MS(30));
	m_TimeToolTips->SetDelayTime(TTDT_INITIAL, SEC2MS(30));
	m_TimeToolTips->SetDelayTime(TTDT_RESHOW, SEC2MS(30));
	EnableToolTips(TRUE);

	return TRUE;
}

void CStatisticsDlg::initCSize()
{
	UINT x = thePrefs.GetSplitterbarPositionStat();
	UINT y = thePrefs.GetSplitterbarPositionStat_HL();
	UINT z = thePrefs.GetSplitterbarPositionStat_HR();
	if (x > 90)
		x = 100;
	else if (x < 10)
		x = 0;
	if (y > 90)
		y = 100;
	else if (y < 10)
		y = 0;
	if (z > 90)
		z = 100;
	else if (z < 10)
		z = 0;

	AddOrReplaceAnchor(this, IDC_BNMENU, ANCHOR(0, 0));

	//StatTitle
	AddOrReplaceAnchor(this, IDC_STATIC_LASTRESET, ANCHOR(0, 0), ANCHOR(x, 0));
	//m_stattree
	AddOrReplaceAnchor(this, m_stattree, ANCHOR(0, 0), ANCHOR(x, 100));

	//graph
	AddOrReplaceAnchor(this, m_DownloadOMeter, ANCHOR(x, 0), ANCHOR(100, z));
	AddOrReplaceAnchor(this, m_UploadOMeter, ANCHOR(x, z), ANCHOR(100, y));
	AddOrReplaceAnchor(this, m_Statistics, ANCHOR(x, y), ANCHOR(100, 100));

	//set range
	RECT rcW;
	GetWindowRect(&rcW);
	ScreenToClient(&rcW);
	RECT rcHR;
	m_wndSplitterstat_HR.GetWindowRect(&rcHR);
	ScreenToClient(&rcHR);
	RECT rcHL;
	m_wndSplitterstat_HL.GetWindowRect(&rcHL);
	ScreenToClient(&rcHL);

	m_wndSplitterstat.SetRange(rcW.left + 11, rcW.right - 11);
	m_wndSplitterstat_HL.SetRange(rcHR.bottom + 5, rcW.bottom - 7);
	m_wndSplitterstat_HR.SetRange(rcW.top + 3, rcHL.top - 5);
}


void CStatisticsDlg::DoResize_HL(int delta)
{
	if (!delta)
		return;
	m_DownloadOMeter.InvalidateCtrl(true);
	CSplitterControl::ChangeHeight(&m_UploadOMeter, delta, CW_TOPALIGN);
	CSplitterControl::ChangeHeight(&m_Statistics, -delta, CW_BOTTOMALIGN);

	CRect rcW;
	GetWindowRect(rcW);
	ScreenToClient(rcW);

	RECT rcSpl;
	m_UploadOMeter.GetWindowRect(&rcSpl);
	ScreenToClient(&rcSpl);
	thePrefs.SetSplitterbarPositionStat_HL(rcSpl.bottom * 100 / rcW.Height());

	initCSize();

	ShowInterval();
	Invalidate();
	UpdateWindow();
}

void CStatisticsDlg::DoResize_HR(int delta)
{
	if (!delta)
		return;
	CSplitterControl::ChangeHeight(&m_DownloadOMeter, delta, CW_TOPALIGN);
	CSplitterControl::ChangeHeight(&m_UploadOMeter, -delta, CW_BOTTOMALIGN);
	m_Statistics.InvalidateCtrl(true);

	CRect rcW;
	GetWindowRect(rcW);
	ScreenToClient(rcW);

	RECT rcSpl;
	m_DownloadOMeter.GetWindowRect(&rcSpl);
	ScreenToClient(&rcSpl);
	thePrefs.SetSplitterbarPositionStat_HR(rcSpl.bottom * 100 / rcW.Height());

	initCSize();

	ShowInterval();
	Invalidate();
	UpdateWindow();
}

void CStatisticsDlg::DoResize_V(int delta)
{
	if (!delta)
		return;
	CSplitterControl::ChangeWidth(GetDlgItem(IDC_STATIC_LASTRESET), delta);
	CSplitterControl::ChangeWidth(&m_stattree, delta);
	CSplitterControl::ChangeWidth(&m_DownloadOMeter, -delta, CW_RIGHTALIGN);
	CSplitterControl::ChangeWidth(&m_UploadOMeter, -delta, CW_RIGHTALIGN);
	CSplitterControl::ChangeWidth(&m_Statistics, -delta, CW_RIGHTALIGN);

	CRect rcW;
	GetWindowRect(rcW);
	ScreenToClient(rcW);

	RECT rcSpl;
	m_stattree.GetWindowRect(&rcSpl);
	ScreenToClient(&rcSpl);
	thePrefs.SetSplitterbarPositionStat(rcSpl.right * 100 / rcW.Width());

	if (rcSpl.left == rcSpl.right) {
		m_stattree.ShowWindow(SW_HIDE);
		GetDlgItem(IDC_BNMENU)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_STATIC_LASTRESET)->ShowWindow(SW_HIDE);
		m_bTreepaneHidden = true;
	} else if (m_bTreepaneHidden) {
		m_bTreepaneHidden = false;
		m_stattree.ShowWindow(SW_SHOW);
		GetDlgItem(IDC_BNMENU)->ShowWindow(SW_SHOW);
		GetDlgItem(IDC_STATIC_LASTRESET)->ShowWindow(SW_SHOW);
	}

	initCSize();

	ShowInterval();
	Invalidate();
	UpdateWindow();
}

LRESULT CStatisticsDlg::DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_PAINT:
		{
			CRect rcW;
			GetWindowRect(rcW);
			if (!rcW.IsRectEmpty()) {
				ScreenToClient(rcW);

				RECT rctree;
				m_stattree.GetWindowRect(&rctree);
				ScreenToClient(&rctree);
				RECT rcSpl = { rctree.right, rcW.top + 2, rctree.right + 4, rcW.bottom - 5 };
				m_wndSplitterstat.MoveWindow(&rcSpl, true);

				RECT rcUp;
				m_UploadOMeter.GetWindowRect(&rcUp);
				ScreenToClient(&rcUp);
				rcSpl = RECT{ rcUp.left, rcUp.bottom, rcUp.right, rcUp.bottom + 4 };
				m_wndSplitterstat_HL.MoveWindow(&rcSpl, true);

				RECT rcDown;
				m_DownloadOMeter.GetWindowRect(&rcDown);
				ScreenToClient(&rcDown);
				rcSpl = RECT{ rcDown.left, rcDown.bottom, rcDown.right, rcDown.bottom + 4 };
				m_wndSplitterstat_HR.MoveWindow(&rcSpl, true);
			}
		}
		break;
	case WM_NOTIFY:
		switch(wParam) {
		case IDC_SPLITTER_STAT:
			DoResize_V(reinterpret_cast<SPC_NMHDR *>(lParam)->delta);
			break;
		case IDC_SPLITTER_STAT_HL:
			DoResize_HL(reinterpret_cast<SPC_NMHDR *>(lParam)->delta);
			break;
		case IDC_SPLITTER_STAT_HR:
			DoResize_HR(reinterpret_cast<SPC_NMHDR *>(lParam)->delta);
		}
		break;
	case WM_WINDOWPOSCHANGED:
		if (m_wndSplitterstat)
			m_wndSplitterstat.Invalidate();
		if (m_wndSplitterstat_HL)
			m_wndSplitterstat_HL.Invalidate();
		if (m_wndSplitterstat_HR)
			m_wndSplitterstat_HR.Invalidate();
		break;
	case WM_SIZE:
		{
			CRect rcW;
			GetWindowRect(rcW);
			//set range
			if (!rcW.IsRectEmpty()) {
				ScreenToClient(rcW);
				long splitposstat = thePrefs.GetSplitterbarPositionStat() * rcW.Width() / 100;
				long splitposstat_HL = thePrefs.GetSplitterbarPositionStat_HL() * rcW.Height() / 100;
				long splitposstat_HR = thePrefs.GetSplitterbarPositionStat_HR() * rcW.Height() / 100;
				RECT rcSpl;

				if (m_DownloadOMeter) {
					rcSpl = RECT{ splitposstat, rcW.top + 2, splitposstat + 4, rcW.bottom - 5 };
					m_wndSplitterstat.MoveWindow(&rcSpl, true);
					m_wndSplitterstat.SetRange(rcW.left + 11, rcW.right - 11);
				}
				if (m_wndSplitterstat_HR) {
					rcSpl = RECT{ splitposstat + 7, splitposstat_HR, rcW.right - 14, splitposstat_HR + 4 };
					m_wndSplitterstat_HR.MoveWindow(&rcSpl, true);
					m_wndSplitterstat_HR.SetRange(rcW.top + 3, splitposstat_HL - 4);
				}
				if (m_wndSplitterstat_HL) {
					rcSpl = RECT{ splitposstat + 7, splitposstat_HL, rcW.right - 14, splitposstat_HL + 4 };
					m_wndSplitterstat_HL.MoveWindow(&rcSpl, true);
					m_wndSplitterstat_HL.SetRange(splitposstat_HR + 14, rcW.bottom - 7);
				}
			}
		}
	}
	return CResizableDialog::DefWindowProc(message, wParam, lParam);
}

void CStatisticsDlg::RepaintMeters()
{
	m_DownloadOMeter.SetBackgroundColor(thePrefs.GetStatsColor(0));	// Background
	m_DownloadOMeter.SetGridColor(thePrefs.GetStatsColor(1));		// Grid
	m_DownloadOMeter.SetPlotColor(thePrefs.GetStatsColor(4), 0);	// Download session
	m_DownloadOMeter.SetPlotColor(thePrefs.GetStatsColor(3), 1);	// Download average
	m_DownloadOMeter.SetPlotColor(thePrefs.GetStatsColor(2), 2);	// Download current
	m_DownloadOMeter.SetBarsPlot(thePrefs.GetFillGraphs(), 2);

	m_UploadOMeter.SetBackgroundColor(thePrefs.GetStatsColor(0));
	m_UploadOMeter.SetGridColor(thePrefs.GetStatsColor(1));			// Grid
	m_UploadOMeter.SetPlotColor(thePrefs.GetStatsColor(7), 0);		// Upload session
	m_UploadOMeter.SetPlotColor(thePrefs.GetStatsColor(6), 1);		// Upload average
	m_UploadOMeter.SetPlotColor(thePrefs.GetStatsColor(5), 2);		// Upload current
	m_UploadOMeter.SetPlotColor(thePrefs.GetStatsColor(14), 3);		// Upload current (excl. overhead)
	m_UploadOMeter.SetPlotColor(thePrefs.GetStatsColor(13), 4);		// Upload friend slots
	m_UploadOMeter.SetBarsPlot(thePrefs.GetFillGraphs(), 2);

	m_Statistics.SetBackgroundColor(thePrefs.GetStatsColor(0));
	m_Statistics.SetGridColor(thePrefs.GetStatsColor(1));
	m_Statistics.SetPlotColor(thePrefs.GetStatsColor(8), 0);	// Active Connections
	m_Statistics.SetPlotColor(thePrefs.GetStatsColor(10), 1);	// Active Uploads
	m_Statistics.SetPlotColor(thePrefs.GetStatsColor(9), 2);	// Total Uploads
	m_Statistics.SetPlotColor(thePrefs.GetStatsColor(12), 3);	// Active Downloads
	m_Statistics.SetBarsPlot(thePrefs.GetFillGraphs(), 0);

	m_DownloadOMeter.SetYUnits(GetResString(_T("ST_DOWNLOAD")));
	m_DownloadOMeter.SetLegendLabel(GetResString(_T("ST_SESSION")), 0);			// Download session
	CString Buffer;
	Buffer.Format(_T(" (%u %s)"), thePrefs.GetStatsAverageMinutes(), (LPCTSTR)GetResString(_T("MINS")));
	m_DownloadOMeter.SetLegendLabel(GetResString(_T("AVG")) + Buffer, 1);			// Download average
	m_DownloadOMeter.SetLegendLabel(GetResString(_T("ST_CURRENT")), 2);			// Download current

	m_UploadOMeter.SetYUnits(GetResString(_T("ST_UPLOAD")));
	m_UploadOMeter.SetLegendLabel(GetResString(_T("ST_SESSION")), 0);				// Upload session
	Buffer.Format(_T(" (%u %s)"), thePrefs.GetStatsAverageMinutes(), (LPCTSTR)GetResString(_T("MINS")));
	m_UploadOMeter.SetLegendLabel(GetResString(_T("AVG")) + Buffer, 1);			// Upload average
	m_UploadOMeter.SetLegendLabel(GetResString(_T("ST_ULCURRENT")), 2);			// Upload current
	m_UploadOMeter.SetLegendLabel(GetResString(_T("ST_ULSLOTSNOOVERHEAD")), 3);	// Upload current (excl. overhead)
	m_UploadOMeter.SetLegendLabel(GetResString(_T("ST_ULFRIEND")), 4);			// Upload friend slots

	m_Statistics.SetYUnits(GetResString(_T("CONNECTIONS")));
	Buffer.Format(_T("%s (1:%u)"), (LPCTSTR)GetResString(_T("ST_ACTIVEC")), thePrefs.GetStatsConnectionsGraphRatio());
	m_Statistics.SetLegendLabel(Buffer, 0);										// Active Connections
	m_Statistics.SetLegendLabel(GetResString(_T("ST_ACTIVEU_ZZ")), 1);			// Active Uploads
	m_Statistics.SetLegendLabel(GetResString(_T("SP_TOTALUL")), 2);				// Total Uploads
	m_Statistics.SetLegendLabel(GetResString(_T("ST_ACTIVED")), 3);				// Active Downloads
}

void CStatisticsDlg::SetCurrentRate(float uploadrate, float downloadrate)
{
	if (theApp.IsClosing())
		return;

	// Download
	double m_dPlotDataDown[3];
	m_dPlotDataDown[0] = theStats.GetAvgDownloadRate(AVG_SESSION);
	m_dPlotDataDown[1] = theStats.GetAvgDownloadRate(AVG_TIME);
	m_dPlotDataDown[2] = downloadrate;
	m_DownloadOMeter.AppendPoints(m_dPlotDataDown);

	// Upload
	double m_dPlotDataUp[5];
	m_dPlotDataUp[0] = theStats.GetAvgUploadRate(AVG_SESSION);
	m_dPlotDataUp[1] = theStats.GetAvgUploadRate(AVG_TIME);
	// current rate to network (standardPackets + controlPackets)
	m_dPlotDataUp[2] = uploadrate;
	// current rate (excl. overhead)
	m_dPlotDataUp[3] = uploadrate - theStats.GetUpDatarateOverhead() / 1024.0f;
	// current rate to friends
	m_dPlotDataUp[4] = uploadrate - theApp.uploadqueue->GetToNetworkDatarate() / 1024.0f;
	m_UploadOMeter.AppendPoints(m_dPlotDataUp);

	// Connections
	CDownloadQueue::SDownloadStats myStats;
	theApp.downloadqueue->GetDownloadSourcesStats(myStats);
	m_dPlotDataMore[0] = theApp.listensocket->GetActiveConnections();
	m_dPlotDataMore[1] = (double)theApp.uploadqueue->GetActiveUploadsCount();
	m_dPlotDataMore[2] = (double)theApp.uploadqueue->GetUploadQueueLength();
	m_dPlotDataMore[3] = myStats.a[1];
	m_Statistics.AppendPoints(m_dPlotDataMore);

	// Web Server
	UpDown updown;
	updown.upload = uploadrate;
	updown.download = downloadrate;
	updown.connections = theApp.listensocket->GetActiveConnections();
	theApp.webserver->AddStatsLine(updown);
}

// Set Tree Values
// If a section is not expanded, don't waste CPU cycles updating it.
void CStatisticsDlg::ShowStatistics(bool forceUpdate)
{
	m_stattree.SetRedraw(false);
	CString sText;

	if (thePrefs.GetClientHistory() != m_useClientHistoryUI)
		CreateMyTree();

	// TRANSFER SECTION
	if (forceUpdate || m_stattree.IsExpanded(h_transfer)) {
		uint32 statGoodSessions;
		uint32 statBadSessions;
		double percentSessions;
		// Transfer Ratios
		if (theStats.sessionReceivedBytes > 0 && theStats.sessionSentBytes > 0) {
			// Session
			if (theStats.sessionReceivedBytes < theStats.sessionSentBytes)
				sText.Format(_T("%s %.2f : 1"), (LPCTSTR)GetResString(_T("STATS_SRATIO")), (float)theStats.sessionSentBytes / theStats.sessionReceivedBytes);
			else
				sText.Format(_T("%s 1 : %.2f"), (LPCTSTR)GetResString(_T("STATS_SRATIO")), (float)theStats.sessionReceivedBytes / theStats.sessionSentBytes);
		} else
			sText.Format(_T("%s %s"), (LPCTSTR)GetResString(_T("STATS_SRATIO")), (LPCTSTR)GetResString(_T("FSTAT_WAITING"))); // Localize
		m_stattree.SetItemText(trans[0], sText);

		if (theStats.sessionReceivedBytes > 0 && theStats.sessionSentBytes > 0) {
			// Session
			if (theStats.sessionSentBytes > theStats.sessionSentBytesToFriend && theStats.sessionReceivedBytes < theStats.sessionSentBytes - theStats.sessionSentBytesToFriend)
				sText.Format(_T("%s %.2f : 1"), (LPCTSTR)GetResString(_T("STATS_FRATIO")), (float)(theStats.sessionSentBytes - theStats.sessionSentBytesToFriend) / theStats.sessionReceivedBytes);
			else
				sText.Format(_T("%s 1 : %.2f"), (LPCTSTR)GetResString(_T("STATS_FRATIO")), (float)theStats.sessionReceivedBytes / (theStats.sessionSentBytes - theStats.sessionSentBytesToFriend));
		} else
			sText.Format(_T("%s %s"), (LPCTSTR)GetResString(_T("STATS_FRATIO")), (LPCTSTR)GetResString(_T("FSTAT_WAITING"))); // Localize
		m_stattree.SetItemText(trans[1], sText);

		if ((thePrefs.GetTotalDownloaded() > 0 && thePrefs.GetTotalUploaded() > 0) || (theStats.sessionReceivedBytes > 0 && theStats.sessionSentBytes > 0)) {
			// Cumulative
			if ((theStats.sessionReceivedBytes + thePrefs.GetTotalDownloaded()) < (theStats.sessionSentBytes + thePrefs.GetTotalUploaded()))
				sText.Format(_T("%s %.2f : 1"), (LPCTSTR)GetResString(_T("STATS_CRATIO")), (float)(theStats.sessionSentBytes + thePrefs.GetTotalUploaded()) / (theStats.sessionReceivedBytes + thePrefs.GetTotalDownloaded()));
			else
				sText.Format(_T("%s 1 : %.2f"), (LPCTSTR)GetResString(_T("STATS_CRATIO")), (float)(theStats.sessionReceivedBytes + thePrefs.GetTotalDownloaded()) / (theStats.sessionSentBytes + thePrefs.GetTotalUploaded()));
		} else
			sText.Format(_T("%s %s"), (LPCTSTR)GetResString(_T("STATS_CRATIO")), (LPCTSTR)GetResString(_T("FSTAT_WAITING"))); // Localize
		m_stattree.SetItemText(trans[2], sText);

		// TRANSFER -> DOWNLOADS SECTION
		if (forceUpdate || m_stattree.IsExpanded(h_download)) {
			uint64 DownOHTotal = 0;
			uint64 DownOHTotalPackets = 0;
			CDownloadQueue::SDownloadStats myStats;
			theApp.downloadqueue->GetDownloadSourcesStats(myStats);
			// TRANSFER -> DOWNLOADS -> SESSION SECTION
			if (forceUpdate || m_stattree.IsExpanded(h_down_session)) {
				// Downloaded Data
				sText.Format(GetResString(_T("STATS_DDATA")), (LPCTSTR)CastItoXBytes(theStats.sessionReceivedBytes));
				m_stattree.SetItemText(down_S[0], sText);
				if (forceUpdate || m_stattree.IsExpanded(down_S[0])) {
					// Downloaded Data By Client
					if (forceUpdate || m_stattree.IsExpanded(hdown_scb)) {
						double percentClientTransferred;
						uint64 DownDataTotal = thePrefs.GetDownSessionClientData();
						uint64 DownDataClient = thePrefs.GetDownData_EMULE();
						if (DownDataTotal != 0 && DownDataClient != 0)
							percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
						else
							percentClientTransferred = 0;
						int i = 0;
						sText.Format(_T("eMule: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
						m_stattree.SetItemText(down_scb[i], sText);
						++i;
						DownDataClient = thePrefs.GetDownData_EDONKEYHYBRID();
						if (DownDataTotal != 0 && DownDataClient != 0)
							percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
						else
							percentClientTransferred = 0;
						sText.Format(_T("eD Hybrid: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
						m_stattree.SetItemText(down_scb[i], sText);
						++i;
						DownDataClient = thePrefs.GetDownData_EDONKEY();
						if (DownDataTotal != 0 && DownDataClient != 0)
							percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
						else
							percentClientTransferred = 0;
						sText.Format(_T("eDonkey: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
						m_stattree.SetItemText(down_scb[i], sText);
						++i;
						DownDataClient = thePrefs.GetDownData_AMULE();
						if (DownDataTotal != 0 && DownDataClient != 0)
							percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
						else
							percentClientTransferred = 0;
						sText.Format(_T("aMule: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
						m_stattree.SetItemText(down_scb[i], sText);
						++i;
						DownDataClient = thePrefs.GetDownData_MLDONKEY();
						if (DownDataTotal != 0 && DownDataClient != 0)
							percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
						else
							percentClientTransferred = 0;
						sText.Format(_T("MLdonkey: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
						m_stattree.SetItemText(down_scb[i], sText);
						++i;
						DownDataClient = thePrefs.GetDownData_SHAREAZA();
						if (DownDataTotal != 0 && DownDataClient != 0)
							percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
						else
							percentClientTransferred = 0;
						sText.Format(_T("Shareaza: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
						m_stattree.SetItemText(down_scb[i], sText);
						++i;
						DownDataClient = thePrefs.GetDownData_EMULECOMPAT();
						if (DownDataTotal != 0 && DownDataClient != 0)
							percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
						else
							percentClientTransferred = 0;
						sText.Format(_T("eM Compat: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
						m_stattree.SetItemText(down_scb[i], sText);
						++i;
						DownDataClient = thePrefs.GetDownData_URL();
						if (DownDataTotal != 0 && DownDataClient != 0)
							percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
						else
							percentClientTransferred = 0;
						sText.Format(_T("URL: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
						m_stattree.SetItemText(down_scb[i], sText);
					}
					// Downloaded Data By Port
					if (forceUpdate || m_stattree.IsExpanded(hdown_spb)) {
						uint64	PortDataDefault = thePrefs.GetDownDataPort_4662();
						uint64	PortDataOther = thePrefs.GetDownDataPort_OTHER();
						uint64	PortDataTotal = thePrefs.GetDownSessionDataPort();
						double	percentPortTransferred;

						int i = 0;
						if (PortDataTotal != 0 && PortDataDefault != 0)
							percentPortTransferred = 100.0 * PortDataDefault / PortDataTotal;
						else
							percentPortTransferred = 0;
						sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_PRTDEF")), (LPCTSTR)CastItoXBytes(PortDataDefault), percentPortTransferred);
						m_stattree.SetItemText(down_spb[i], sText);
						++i;
						if (PortDataTotal != 0 && PortDataOther != 0)
							percentPortTransferred = 100.0 * PortDataOther / PortDataTotal;
						else
							percentPortTransferred = 0;
						sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_PRTOTHER")), (LPCTSTR)CastItoXBytes(PortDataOther), percentPortTransferred);
						m_stattree.SetItemText(down_spb[i], sText);
					}
				}
				// Completed Downloads
				sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("STATS_COMPDL")), thePrefs.GetDownSessionCompletedFiles());
				m_stattree.SetItemText(down_S[1], sText);
				// Active Downloads
				sText.Format(GetResString(_T("STATS_ACTDL")), myStats.a[1]);
				m_stattree.SetItemText(down_S[2], sText);
				// Found Sources
				sText.Format(GetResString(_T("STATS_FOUNDSRC")), myStats.a[0]);
				m_stattree.SetItemText(down_S[3], sText);
				if (forceUpdate || m_stattree.IsExpanded(down_S[3])) {
					int i = 0;
					// Sources By Status
					sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("ONQUEUE")), myStats.a[2]);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;
					sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("QUEUEFULL")), myStats.a[3]);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;
					sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("NONEEDEDPARTS")), myStats.a[4]);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;
					sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("ASKING")), myStats.a[5]);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;
					sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("RECHASHSET")), myStats.a[6]);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;
					sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("CONNECTING")), myStats.a[7]);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;
					sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("CONNVIASERVER")), myStats.a[8]);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;
					sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("TOOMANYCONNS")), myStats.a[9]);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;
					sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("NOCONNECTLOW2LOW")), myStats.a[10]);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;
					sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("STATS_PROBLEMATIC")), myStats.a[12]);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;
					sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("BANNED")), myStats.a[13]);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;
					sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("ASKED4ANOTHERFILE")), myStats.a[15]);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;
					sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("UNKNOWN")), myStats.a[11]);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;

					// where from? (3)
					sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("VIAED2KSQ")), myStats.a[16]);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;
					sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("VIAKAD")), myStats.a[17]);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;
					sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("VIASE")), myStats.a[18]);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;
					sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("VIAPASSIVE")), myStats.a[14]);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;
					sText.Format(_T("eD2K: %u (%.1f%%)"), myStats.a[19], myStats.a[0] ? (myStats.a[19] * 100.0 / myStats.a[0]) : 0.0);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;
					sText.Format(_T("Kad: %u (%.1f%%)"), myStats.a[20], myStats.a[0] ? (myStats.a[20] * 100.0 / myStats.a[0]) : 0.0);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;
					sText.Format(_T("eD2K/Kad: %u (%.1f%%)"), myStats.a[21], myStats.a[0] ? (myStats.a[21] * 100.0 / myStats.a[0]) : 0.0);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;

					sText.Format(_T("%s: %s, %s: %s (%.1f%%)"), (LPCTSTR)GetResString(_T("UDPREASKS")), (LPCTSTR)CastItoIShort(theApp.downloadqueue->GetUDPFileReasks()), (LPCTSTR)GetResString(_T("UFAILED")), (LPCTSTR)CastItoIShort(theApp.downloadqueue->GetFailedUDPFileReasks()), theApp.downloadqueue->GetUDPFileReasks() ? (theApp.downloadqueue->GetFailedUDPFileReasks() * 100.0 / theApp.downloadqueue->GetUDPFileReasks()) : 0.0);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;

					//Count failed tcp-connections:
					sText.Format(_T("%s: %s, %s: %s (%.1f%%)"), (LPCTSTR)GetResString(_T("TCPREASKS")), (LPCTSTR)CastItoIShort(theApp.downloadqueue->GetTCPFileReasks()), (LPCTSTR)GetResString(_T("UFAILED")), (LPCTSTR)CastItoIShort(theApp.downloadqueue->GetFailedTCPFileReasks()), theApp.downloadqueue->GetTCPFileReasks() ? (theApp.downloadqueue->GetFailedTCPFileReasks() * 100.0 / theApp.downloadqueue->GetTCPFileReasks()) : 0.0);
					m_stattree.SetItemText(down_sources[i], sText);
					++i;

					sText.Format(_T("%s: %s (%s + %s)"), (LPCTSTR)GetResString(_T("DEADSOURCES")), (LPCTSTR)CastItoIShort(static_cast<uint32>(theApp.clientlist->m_globDeadSourceList.GetDeadSourcesCount() + myStats.a[22])), (LPCTSTR)CastItoIShort(static_cast<uint32>(theApp.clientlist->m_globDeadSourceList.GetDeadSourcesCount())), (LPCTSTR)CastItoIShort(static_cast<uint32>(myStats.a[22])));
					m_stattree.SetItemText(down_sources[i], sText);
				}
				// Set Download Sessions
				statGoodSessions = thePrefs.GetDownS_SuccessfulSessions() + myStats.a[1]; // Add Active Downloads
				statBadSessions = thePrefs.GetDownS_FailedSessions();
				sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("STATS_DLSES")), statGoodSessions + statBadSessions);
				m_stattree.SetItemText(down_S[4], sText);
				if (forceUpdate || m_stattree.IsExpanded(down_S[4])) {
					// Set Successful Download Sessions and Average Downloaded Per Session
					if (statGoodSessions > 0) {
						percentSessions = 100.0 * statGoodSessions / (statGoodSessions + statBadSessions);
						sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_AVGDATADLSES")), (LPCTSTR)CastItoXBytes(theStats.sessionReceivedBytes / statGoodSessions));
					} else {
						percentSessions = 0;
						sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_AVGDATADLSES")), (LPCTSTR)CastItoXBytes((uint32)0));
					}
					m_stattree.SetItemText(down_ssessions[2], sText); // Set Avg DL/Session
					sText.Format(_T("%s: %u (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_SDLSES")), statGoodSessions, percentSessions);
					m_stattree.SetItemText(down_ssessions[0], sText); // Set Successful Sessions
					// Set Failed Download Sessions
					if (statBadSessions > 0)
						percentSessions = 100 - percentSessions; // There were bad sessions
					else
						percentSessions = 0; // No bad sessions at all
					sText.Format(_T("%s: %u (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_FDLSES")), statBadSessions, percentSessions);
					m_stattree.SetItemText(down_ssessions[1], sText);
					// Set Average Download Time
					sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_AVGDLTIME")), (LPCTSTR)CastSecondsToLngHM(thePrefs.GetDownS_AvgTime()));
					m_stattree.SetItemText(down_ssessions[3], sText);
				}
				// Set Gain Due To Compression
				sText.Format(GetResString(_T("STATS_GAINCOMP")) + _T(" (%.1f%%)"), (LPCTSTR)CastItoXBytes(thePrefs.GetSesSavedFromCompression()), theStats.sessionReceivedBytes != 0 ? (thePrefs.GetSesSavedFromCompression() * 100.0 / theStats.sessionReceivedBytes) : 0.0);
				m_stattree.SetItemText(down_S[5], sText);
				// Set Lost Due To Corruption
				sText.Format(GetResString(_T("STATS_LOSTCORRUPT")) + _T(" (%.1f%%)"), (LPCTSTR)CastItoXBytes(thePrefs.GetSesLostFromCorruption()), theStats.sessionReceivedBytes != 0 ? (thePrefs.GetSesLostFromCorruption() * 100.0 / theStats.sessionReceivedBytes) : 0.0);
				m_stattree.SetItemText(down_S[6], sText);
				// Set Parts Saved Due To ICH
				sText.Format(GetResString(_T("STATS_ICHSAVED")), thePrefs.GetSesPartsSavedByICH());
				m_stattree.SetItemText(down_S[7], sText);

				// Calculate downline OH totals
				DownOHTotal = theStats.GetDownDataOverheadFileRequest()
					+ theStats.GetDownDataOverheadSourceExchange()
					+ theStats.GetDownDataOverheadServer()
					+ theStats.GetDownDataOverheadKad()
					+ theStats.GetDownDataOverheadOther();
				DownOHTotalPackets = theStats.GetDownDataOverheadFileRequestPackets()
					+ theStats.GetDownDataOverheadSourceExchangePackets()
					+ theStats.GetDownDataOverheadServerPackets()
					+ theStats.GetDownDataOverheadKadPackets()
					+ theStats.GetDownDataOverheadOtherPackets();

				// Downline Overhead
				sText.Format(GetResString(_T("TOVERHEAD")), (LPCTSTR)CastItoXBytes(DownOHTotal), (LPCTSTR)CastItoIShort(DownOHTotalPackets));
				m_stattree.SetItemText(hdown_soh, sText);
				if (forceUpdate || m_stattree.IsExpanded(hdown_soh)) {
					int i = 0;
					// Set down session file req OH
					sText.Format(GetResString(_T("FROVERHEAD")), (LPCTSTR)CastItoXBytes(theStats.GetDownDataOverheadFileRequest()), (LPCTSTR)CastItoIShort(theStats.GetDownDataOverheadFileRequestPackets()));
					m_stattree.SetItemText(down_soh[i], sText);
					++i;
					// Set down session source exch OH
					sText.Format(GetResString(_T("SSOVERHEAD")), (LPCTSTR)CastItoXBytes(theStats.GetDownDataOverheadSourceExchange()), (LPCTSTR)CastItoIShort(theStats.GetDownDataOverheadSourceExchangePackets()));
					m_stattree.SetItemText(down_soh[i], sText);
					++i;
					// Set down session server OH
					sText.Format(GetResString(_T("SOVERHEAD")),
						(LPCTSTR)CastItoXBytes(theStats.GetDownDataOverheadServer()),
						(LPCTSTR)CastItoIShort(theStats.GetDownDataOverheadServerPackets()));
					m_stattree.SetItemText(down_soh[i], sText);
					++i;
					// Set down session Kad OH
					sText.Format(GetResString(_T("KADOVERHEAD")),
						(LPCTSTR)CastItoXBytes(theStats.GetDownDataOverheadKad()),
						(LPCTSTR)CastItoIShort(theStats.GetDownDataOverheadKadPackets()));
					m_stattree.SetItemText(down_soh[i], sText);
				}
			}
			// TRANSFER -> DOWNLOADS -> CUMULATIVE SECTION
			if (forceUpdate || m_stattree.IsExpanded(h_down_total)) {
				// Downloaded Data
				uint64 ullCumReceived = theStats.sessionReceivedBytes + thePrefs.GetTotalDownloaded();
				sText.Format(GetResString(_T("STATS_DDATA")), (LPCTSTR)CastItoXBytes(ullCumReceived));
				m_stattree.SetItemText(down_T[0], sText);
				if (forceUpdate || m_stattree.IsExpanded(down_T[0])) {
					// Downloaded Data By Client
					if (forceUpdate || m_stattree.IsExpanded(hdown_tcb)) {
						uint64 DownDataTotal = thePrefs.GetDownTotalClientData();
						uint64 DownDataClient = thePrefs.GetCumDownData_EMULE();
						double percentClientTransferred;
						if (DownDataTotal != 0 && DownDataClient != 0)
							percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
						else
							percentClientTransferred = 0;
						int i = 0;
						sText.Format(_T("eMule: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
						m_stattree.SetItemText(down_tcb[i], sText);
						++i;
						DownDataClient = thePrefs.GetCumDownData_EDONKEYHYBRID();
						if (DownDataTotal != 0 && DownDataClient != 0)
							percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
						else
							percentClientTransferred = 0;
						sText.Format(_T("eD Hybrid: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
						m_stattree.SetItemText(down_tcb[i], sText);
						++i;
						DownDataClient = thePrefs.GetCumDownData_EDONKEY();
						if (DownDataTotal != 0 && DownDataClient != 0)
							percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
						else
							percentClientTransferred = 0;
						sText.Format(_T("eDonkey: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
						m_stattree.SetItemText(down_tcb[i], sText);
						++i;
						DownDataClient = thePrefs.GetCumDownData_AMULE();
						if (DownDataTotal != 0 && DownDataClient != 0)
							percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
						else
							percentClientTransferred = 0;
						sText.Format(_T("aMule: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
						m_stattree.SetItemText(down_tcb[i], sText);
						++i;
						DownDataClient = thePrefs.GetCumDownData_MLDONKEY();
						if (DownDataTotal != 0 && DownDataClient != 0)
							percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
						else
							percentClientTransferred = 0;
						sText.Format(_T("MLdonkey: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
						m_stattree.SetItemText(down_tcb[i], sText);
						++i;
						DownDataClient = thePrefs.GetCumDownData_SHAREAZA();
						if (DownDataTotal != 0 && DownDataClient != 0)
							percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
						else
							percentClientTransferred = 0;
						sText.Format(_T("Shareaza: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
						m_stattree.SetItemText(down_tcb[i], sText);
						++i;
						DownDataClient = thePrefs.GetCumDownData_EMULECOMPAT();
						if (DownDataTotal != 0 && DownDataClient != 0)
							percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
						else
							percentClientTransferred = 0;
						sText.Format(_T("eM Compat: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
						m_stattree.SetItemText(down_tcb[i], sText);
						++i;
						DownDataClient = thePrefs.GetCumDownData_URL();
						if (DownDataTotal != 0 && DownDataClient != 0)
							percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
						else
							percentClientTransferred = 0;
						sText.Format(_T("URL: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
						m_stattree.SetItemText(down_tcb[i], sText);
					}
					// Downloaded Data By Port
					if (forceUpdate || m_stattree.IsExpanded(hdown_tpb)) {
						int i = 0;
						uint64	PortDataDefault = thePrefs.GetCumDownDataPort_4662();
						uint64	PortDataOther = thePrefs.GetCumDownDataPort_OTHER();
						uint64	PortDataTotal = thePrefs.GetDownTotalPortData();
						double	percentPortTransferred;

						if (PortDataTotal != 0 && PortDataDefault != 0)
							percentPortTransferred = 100.0 * PortDataDefault / PortDataTotal;
						else
							percentPortTransferred = 0;
						sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_PRTDEF")), (LPCTSTR)CastItoXBytes(PortDataDefault), percentPortTransferred);
						m_stattree.SetItemText(down_tpb[i], sText);
						++i;

						if (PortDataTotal != 0 && PortDataOther != 0)
							percentPortTransferred = 100.0 * PortDataOther / PortDataTotal;
						else
							percentPortTransferred = 0;
						sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_PRTOTHER")), (LPCTSTR)CastItoXBytes(PortDataOther), percentPortTransferred);
						m_stattree.SetItemText(down_tpb[i], sText);
					}
				}
				// Set Cum Completed Downloads
				sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("STATS_COMPDL")), thePrefs.GetDownCompletedFiles());
				m_stattree.SetItemText(down_T[1], sText);
				// Set Cum Download Sessions
				statGoodSessions = thePrefs.GetDownC_SuccessfulSessions() + myStats.a[1]; // Need to reset these from the session section.  Declared up there.
				statBadSessions = thePrefs.GetDownC_FailedSessions(); // ^^^^^^^^^^^^^^
				sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("STATS_DLSES")), statGoodSessions + statBadSessions);
				m_stattree.SetItemText(down_T[2], sText);
				if (forceUpdate || m_stattree.IsExpanded(down_T[2])) {
					// Set Cum Successful Download Sessions & Cum Average Download Per Sessions (Save an if-else statement)
					if (statGoodSessions > 0) {
						percentSessions = 100.0 * statGoodSessions / (statGoodSessions + statBadSessions);
						sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_AVGDATADLSES")), (LPCTSTR)CastItoXBytes(ullCumReceived / statGoodSessions));
					} else {
						percentSessions = 0;
						sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_AVGDATADLSES")), (LPCTSTR)CastItoXBytes((uint32)0));
					}
					m_stattree.SetItemText(down_tsessions[2], sText); // Set Avg DL/Session
					sText.Format(_T("%s: %u (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_SDLSES")), statGoodSessions, percentSessions);
					m_stattree.SetItemText(down_tsessions[0], sText); // Set Successful Sessions
					// Set Cum Failed Download Sessions
					if (statBadSessions > 0)
						percentSessions = 100 - percentSessions; // There were bad sessions
					else
						percentSessions = 0; // No bad sessions at all
					sText.Format(_T("%s: %u (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_FDLSES")), statBadSessions, percentSessions);
					m_stattree.SetItemText(down_tsessions[1], sText);
					sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_AVGDLTIME")), (LPCTSTR)CastSecondsToLngHM(thePrefs.GetDownC_AvgTime()));
					m_stattree.SetItemText(down_tsessions[3], sText);
				}
				// Set Cumulative Gained Due To Compression
				uint64 ullCumCompressed = thePrefs.GetSesSavedFromCompression() + thePrefs.GetCumSavedFromCompression();
				sText.Format(GetResString(_T("STATS_GAINCOMP")) + _T(" (%.1f%%)"), (LPCTSTR)CastItoXBytes(ullCumCompressed), ullCumReceived != 0 ? (ullCumCompressed * 100.0 / ullCumReceived) : 0.0);
				m_stattree.SetItemText(down_T[3], sText);
				// Set Cumulative Lost Due To Corruption
				uint64 ullCumCorrupted = thePrefs.GetSesLostFromCorruption() + thePrefs.GetCumLostFromCorruption();
				sText.Format(GetResString(_T("STATS_LOSTCORRUPT")) + _T(" (%.1f%%)"), (LPCTSTR)CastItoXBytes(ullCumCorrupted), ullCumReceived != 0 ? (ullCumCorrupted * 100.0 / ullCumReceived) : 0.0);
				m_stattree.SetItemText(down_T[4], sText);
				// Set Cumulative Saved Due To ICH
				sText.Format(GetResString(_T("STATS_ICHSAVED")), thePrefs.GetSesPartsSavedByICH() + thePrefs.GetCumPartsSavedByICH());
				m_stattree.SetItemText(down_T[5], sText);

				if (DownOHTotal == 0 || DownOHTotalPackets == 0) {
					DownOHTotal = theStats.GetDownDataOverheadFileRequest()
						+ theStats.GetDownDataOverheadSourceExchange()
						+ theStats.GetDownDataOverheadServer()
						+ theStats.GetDownDataOverheadKad()
						+ theStats.GetDownDataOverheadOther();
					DownOHTotalPackets = theStats.GetDownDataOverheadFileRequestPackets()
						+ theStats.GetDownDataOverheadSourceExchangePackets()
						+ theStats.GetDownDataOverheadServerPackets()
						+ theStats.GetDownDataOverheadKadPackets()
						+ theStats.GetDownDataOverheadOtherPackets();
				}
				// Total Overhead
				sText.Format(GetResString(_T("TOVERHEAD")), (LPCTSTR)CastItoXBytes(DownOHTotal + thePrefs.GetDownOverheadTotal()), (LPCTSTR)CastItoIShort(DownOHTotalPackets + thePrefs.GetDownOverheadTotalPackets()));
				m_stattree.SetItemText(hdown_toh, sText);
				if (forceUpdate || m_stattree.IsExpanded(hdown_toh)) {
					int i = 0;
					// File Request Overhead
					sText.Format(GetResString(_T("FROVERHEAD")), (LPCTSTR)CastItoXBytes(theStats.GetDownDataOverheadFileRequest() + thePrefs.GetDownOverheadFileReq()), (LPCTSTR)CastItoIShort(theStats.GetDownDataOverheadFileRequestPackets() + thePrefs.GetDownOverheadFileReqPackets()));
					m_stattree.SetItemText(down_toh[i], sText);
					++i;
					// Source Exchange Overhead
					sText.Format(GetResString(_T("SSOVERHEAD")), (LPCTSTR)CastItoXBytes(theStats.GetDownDataOverheadSourceExchange() + thePrefs.GetDownOverheadSrcEx()), (LPCTSTR)CastItoIShort(theStats.GetDownDataOverheadSourceExchangePackets() + thePrefs.GetDownOverheadSrcExPackets()));
					m_stattree.SetItemText(down_toh[i], sText);
					++i;
					// Server Overhead
					sText.Format(GetResString(_T("SOVERHEAD")),
						(LPCTSTR)CastItoXBytes(theStats.GetDownDataOverheadServer() +
							thePrefs.GetDownOverheadServer(), false, false),
							(LPCTSTR)CastItoIShort(theStats.GetDownDataOverheadServerPackets() +
								thePrefs.GetDownOverheadServerPackets()));
					m_stattree.SetItemText(down_toh[i], sText);
					++i;
					// Kad Overhead
					sText.Format(GetResString(_T("KADOVERHEAD"))
						, (LPCTSTR)CastItoXBytes(theStats.GetDownDataOverheadKad() + thePrefs.GetDownOverheadKad())
						, (LPCTSTR)CastItoIShort(theStats.GetDownDataOverheadKadPackets() + thePrefs.GetDownOverheadKadPackets()));
					m_stattree.SetItemText(down_toh[i], sText);
				}
			} // - End Transfer -> Downloads -> Cumulative Section
		} // - End Transfer -> Downloads Section
		// TRANSFER-> UPLOADS SECTION
		if (forceUpdate || m_stattree.IsExpanded(h_upload)) {
			uint64 UpOHTotal = 0;
			uint64 UpOHTotalPackets = 0;
			// TRANSFER -> UPLOADS -> SESSION SECTION
			if (forceUpdate || m_stattree.IsExpanded(h_up_session)) {
				// Uploaded Data
				sText.Format(GetResString(_T("STATS_UDATA")), (LPCTSTR)CastItoXBytes(theStats.sessionSentBytes));
				m_stattree.SetItemText(up_S[0], sText);
				if (forceUpdate || m_stattree.IsExpanded(up_S[0])) {
					// Uploaded Data By Client
					if (forceUpdate || m_stattree.IsExpanded(hup_scb)) {
						uint64 UpDataTotal = thePrefs.GetUpSessionClientData();
						uint64 UpDataClient = thePrefs.GetUpData_EMULE();
						double percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
						int i = 0;
						sText.Format(_T("eMule: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
						m_stattree.SetItemText(up_scb[i], sText);
						++i;
						UpDataClient = thePrefs.GetUpData_EDONKEYHYBRID();
						percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
						sText.Format(_T("eD Hybrid: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
						m_stattree.SetItemText(up_scb[i], sText);
						++i;
						UpDataClient = thePrefs.GetUpData_EDONKEY();
						percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
						sText.Format(_T("eDonkey: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
						m_stattree.SetItemText(up_scb[i], sText);
						++i;
						UpDataClient = thePrefs.GetUpData_AMULE();
						percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
						sText.Format(_T("aMule: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
						m_stattree.SetItemText(up_scb[i], sText);
						++i;
						UpDataClient = thePrefs.GetUpData_MLDONKEY();
						percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
						sText.Format(_T("MLdonkey: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
						m_stattree.SetItemText(up_scb[i], sText);
						++i;
						UpDataClient = thePrefs.GetUpData_SHAREAZA();
						percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
						sText.Format(_T("Shareaza: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
						m_stattree.SetItemText(up_scb[i], sText);
						++i;
						UpDataClient = thePrefs.GetUpData_EMULECOMPAT();
						percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
						sText.Format(_T("eM Compat: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
						m_stattree.SetItemText(up_scb[i], sText);
					}
					// Uploaded Data By Port
					if (forceUpdate || m_stattree.IsExpanded(hup_spb)) {
						uint64	PortDataTotal = thePrefs.GetUpSessionPortData();
						uint64	PortDataDefault = thePrefs.GetUpDataPort_4662();
						double	percentPortTransferred = !PortDataTotal ? 0 : 100.0 * PortDataDefault / PortDataTotal;
						int i = 0;
						sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_PRTDEF")), (LPCTSTR)CastItoXBytes(PortDataDefault), percentPortTransferred);
						m_stattree.SetItemText(up_spb[i], sText);
						++i;
						uint64	PortDataOther = thePrefs.GetUpDataPort_OTHER();
						percentPortTransferred = !PortDataTotal ? 0 : 100.0 * PortDataOther / PortDataTotal;
						sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_PRTOTHER")), (LPCTSTR)CastItoXBytes(PortDataOther), percentPortTransferred);
						m_stattree.SetItemText(up_spb[i], sText);
					}
					// Uploaded Data By Source
					if (forceUpdate || m_stattree.IsExpanded(hup_ssb)) {
						int i = 0;
						uint64	DataSourceTotal = thePrefs.GetUpSessionDataFile();

						uint64	DataSourceFile = thePrefs.GetUpData_File();
						double	percentFileTransferred = !DataSourceTotal ? 0 : 100.0 * DataSourceFile / DataSourceTotal;
						sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_DSFILE")), (LPCTSTR)CastItoXBytes(DataSourceFile), percentFileTransferred);
						m_stattree.SetItemText(up_ssb[i], sText);
						++i;

						uint64	DataSourcePF = thePrefs.GetUpData_Partfile();
						percentFileTransferred = !DataSourceTotal ? 0 : 100.0 * DataSourcePF / DataSourceTotal;
						sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_DSPF")), (LPCTSTR)CastItoXBytes(DataSourcePF), percentFileTransferred);
						m_stattree.SetItemText(up_ssb[i], sText);
					}
				}
				// Amount of data uploaded to friends
				sText.Format(GetResString(_T("STATS_UDATA_FRIENDS")), (LPCTSTR)CastItoXBytes(theStats.sessionSentBytesToFriend));
				m_stattree.SetItemText(up_S[1], sText);
				// Set fully Active Uploads
				sText.Format(GetResString(_T("STATS_ACTUL_ZZ")), static_cast<unsigned>(theApp.uploadqueue->GetActiveUploadsCount())); //theApp.uploadqueue->GetUploadQueueLength()
				m_stattree.SetItemText(up_S[2], sText);
				// Set Total Uploads
				sText.Format(GetResString(_T("STATS_TOTALUL")), static_cast<unsigned>(theApp.uploadqueue->GetUploadQueueLength()));
				m_stattree.SetItemText(up_S[3], sText);
				// Set Queue Length
				sText.Format(GetResString(_T("STATS_WAITINGUSERS")), static_cast<unsigned>(theApp.uploadqueue->GetWaitingUserCount()));
				m_stattree.SetItemText(up_S[4], sText);
				// Set Upload Sessions
				statGoodSessions = theApp.uploadqueue->GetSuccessfullUpCount() + (uint32)theApp.uploadqueue->GetUploadQueueLength();
				statBadSessions = theApp.uploadqueue->GetFailedUpCount();
				sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("STATS_ULSES")), statGoodSessions + statBadSessions);
				m_stattree.SetItemText(up_S[5], sText);
				if (forceUpdate || m_stattree.IsExpanded(up_S[5])) {
					// Set Successful Upload Sessions & Average Uploaded Per Session
					if (statGoodSessions > 0) // Black holes are when God divided by 0
						percentSessions = 100.0 * statGoodSessions / (statGoodSessions + statBadSessions);
					else
						percentSessions = 0;
					sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_AVGDATAULSES"))
						, (LPCTSTR)(statGoodSessions > 0 ? CastItoXBytes(theStats.sessionSentBytes / statGoodSessions) : GetResString(_T("FSTAT_WAITING"))));
					m_stattree.SetItemText(up_ssessions[2], sText);
					sText.Format(GetResString(_T("STATS_SUCCUPCOUNT")), statGoodSessions, percentSessions);
					m_stattree.SetItemText(up_ssessions[0], sText);
					// Set Failed Upload Sessions
					if (statBadSessions > 0)
						percentSessions = 100 - percentSessions; // There were bad sessions
					else
						percentSessions = 0; // No bad sessions at all
					sText.Format(GetResString(_T("STATS_FAILUPCOUNT")), statBadSessions, percentSessions);
					m_stattree.SetItemText(up_ssessions[1], sText);
					// Set Avg Upload time
					sText.Format(GetResString(_T("STATS_AVEUPTIME")), (LPCTSTR)CastSecondsToLngHM(theApp.uploadqueue->GetAverageUpTime()));
					m_stattree.SetItemText(up_ssessions[3], sText);
				}
				// Calculate Upload OH Totals
				UpOHTotal = theStats.GetUpDataOverheadFileRequest()
					+ theStats.GetUpDataOverheadSourceExchange()
					+ theStats.GetUpDataOverheadServer()
					+ theStats.GetUpDataOverheadKad()
					+ theStats.GetUpDataOverheadOther();
				UpOHTotalPackets = theStats.GetUpDataOverheadFileRequestPackets()
					+ theStats.GetUpDataOverheadSourceExchangePackets()
					+ theStats.GetUpDataOverheadServerPackets()
					+ theStats.GetUpDataOverheadKadPackets()
					+ theStats.GetUpDataOverheadOtherPackets();
				// Total Upload Overhead
				sText.Format(GetResString(_T("TOVERHEAD")), (LPCTSTR)CastItoXBytes(UpOHTotal), (LPCTSTR)CastItoIShort(UpOHTotalPackets));
				m_stattree.SetItemText(hup_soh, sText);
				if (forceUpdate || m_stattree.IsExpanded(hup_soh)) {
					int i = 0;
					// File Request Overhead
					sText.Format(GetResString(_T("FROVERHEAD")), (LPCTSTR)CastItoXBytes(theStats.GetUpDataOverheadFileRequest()), (LPCTSTR)CastItoIShort(theStats.GetUpDataOverheadFileRequestPackets()));
					m_stattree.SetItemText(up_soh[i], sText);
					++i;
					// Source Exchanged Overhead
					sText.Format(GetResString(_T("SSOVERHEAD")), (LPCTSTR)CastItoXBytes(theStats.GetUpDataOverheadSourceExchange()), (LPCTSTR)CastItoIShort(theStats.GetUpDataOverheadSourceExchangePackets()));
					m_stattree.SetItemText(up_soh[i], sText);
					++i;
					// Server Overhead
					sText.Format(GetResString(_T("SOVERHEAD"))
						, (LPCTSTR)CastItoXBytes(theStats.GetUpDataOverheadServer())
						, (LPCTSTR)CastItoIShort(theStats.GetUpDataOverheadServerPackets()));
					m_stattree.SetItemText(up_soh[i], sText);
					++i;
					// Kad Overhead
					sText.Format(GetResString(_T("KADOVERHEAD"))
						, (LPCTSTR)CastItoXBytes(theStats.GetUpDataOverheadKad())
						, (LPCTSTR)CastItoIShort(theStats.GetUpDataOverheadKadPackets()));
					m_stattree.SetItemText(up_soh[i], sText);
				}
			} // - End Transfer -> Uploads -> Session Section
			// TRANSFER -> UPLOADS -> CUMULATIVE SECTION
			if (forceUpdate || m_stattree.IsExpanded(h_up_total)) {
				// Uploaded Data
				sText.Format(GetResString(_T("STATS_UDATA")), (LPCTSTR)CastItoXBytes(theStats.sessionSentBytes + thePrefs.GetTotalUploaded()));
				m_stattree.SetItemText(up_T[0], sText);
				if (forceUpdate || m_stattree.IsExpanded(up_T[0])) {
					// Uploaded Data By Client
					if (forceUpdate || m_stattree.IsExpanded(hup_tcb)) {
						int i = 0;
						uint64 UpDataTotal = thePrefs.GetUpTotalClientData();

						uint64 UpDataClient = thePrefs.GetCumUpData_EMULE();
						double percentClientTransferred;
						percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
						sText.Format(_T("eMule: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
						m_stattree.SetItemText(up_tcb[i], sText);
						++i;
						UpDataClient = thePrefs.GetCumUpData_EDONKEYHYBRID();
						percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
						sText.Format(_T("eD Hybrid: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
						m_stattree.SetItemText(up_tcb[i], sText);
						++i;
						UpDataClient = thePrefs.GetCumUpData_EDONKEY();
						percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
						sText.Format(_T("eDonkey: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
						m_stattree.SetItemText(up_tcb[i], sText);
						++i;
						UpDataClient = thePrefs.GetCumUpData_AMULE();
						percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
						sText.Format(_T("aMule: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
						m_stattree.SetItemText(up_tcb[i], sText);
						++i;
						UpDataClient = thePrefs.GetCumUpData_MLDONKEY();
						percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
						sText.Format(_T("MLdonkey: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
						m_stattree.SetItemText(up_tcb[i], sText);
						++i;
						UpDataClient = thePrefs.GetCumUpData_SHAREAZA();
						percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
						sText.Format(_T("Shareaza: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
						m_stattree.SetItemText(up_tcb[i], sText);
						++i;
						UpDataClient = thePrefs.GetCumUpData_EMULECOMPAT();
						percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
						sText.Format(_T("eM Compat: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
						m_stattree.SetItemText(up_tcb[i], sText);
					}
					// Uploaded Data By Port
					if (forceUpdate || m_stattree.IsExpanded(hup_tpb)) {
						int i = 0;
						uint64	PortDataTotal = thePrefs.GetUpTotalPortData();
						uint64	PortDataDefault = thePrefs.GetCumUpDataPort_4662();
						double	percentPortTransferred = !PortDataTotal ? 0 : 100.0 * PortDataDefault / PortDataTotal;
						sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_PRTDEF")), (LPCTSTR)CastItoXBytes(PortDataDefault), percentPortTransferred);
						m_stattree.SetItemText(up_tpb[i], sText);
						++i;
						uint64	PortDataOther = thePrefs.GetCumUpDataPort_OTHER();
						percentPortTransferred = !PortDataTotal ? 0 : 100.0 * PortDataOther / PortDataTotal;
						sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_PRTOTHER")), (LPCTSTR)CastItoXBytes(PortDataOther), percentPortTransferred);
						m_stattree.SetItemText(up_tpb[i], sText);
					}
					// Uploaded Data By Source
					if (forceUpdate || m_stattree.IsExpanded(hup_tsb)) {
						uint64	DataSourceTotal = thePrefs.GetUpTotalDataFile();
						uint64	DataSourceFile = thePrefs.GetCumUpData_File();
						double	percentFileTransferred = !DataSourceTotal ? 0 : 100.0 * DataSourceFile / DataSourceTotal;
						int i = 0;
						sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_DSFILE")), (LPCTSTR)CastItoXBytes(DataSourceFile), percentFileTransferred);
						m_stattree.SetItemText(up_tsb[i], sText);
						++i;
						uint64	DataSourcePF = thePrefs.GetCumUpData_Partfile();
						percentFileTransferred = !DataSourceTotal ? 0 : 100.0 * DataSourcePF / DataSourceTotal;
						sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_DSPF")), (LPCTSTR)CastItoXBytes(DataSourcePF), percentFileTransferred);
						m_stattree.SetItemText(up_tsb[i], sText);
					}
				}
				// Upload Sessions
				statGoodSessions = theApp.uploadqueue->GetSuccessfullUpCount() + thePrefs.GetUpSuccessfulSessions() + (uint32)theApp.uploadqueue->GetUploadQueueLength();
				statBadSessions = theApp.uploadqueue->GetFailedUpCount() + thePrefs.GetUpFailedSessions();
				sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("STATS_ULSES")), statGoodSessions + statBadSessions);
				m_stattree.SetItemText(up_T[1], sText);
				if (forceUpdate || m_stattree.IsExpanded(up_T[1])) {
					// Set Successful Upload Sessions & Average Uploaded Per Session
					if (statGoodSessions > 0) // Black holes are when God divided by 0
						percentSessions = 100.0 * statGoodSessions / (statGoodSessions + statBadSessions);
					else
						percentSessions = 0;
					sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_AVGDATAULSES"))
						, (LPCTSTR)(statGoodSessions > 0 ? CastItoXBytes((theStats.sessionSentBytes + thePrefs.GetTotalUploaded()) / statGoodSessions) : GetResString(_T("FSTAT_WAITING"))));

					m_stattree.SetItemText(up_tsessions[2], sText);
					sText.Format(GetResString(_T("STATS_SUCCUPCOUNT")), statGoodSessions, percentSessions);
					m_stattree.SetItemText(up_tsessions[0], sText);
					// Set Failed Upload Sessions
					if (statBadSessions > 0)
						percentSessions = 100 - percentSessions; // There were bad sessions
					else
						percentSessions = 0; // No bad sessions at all
					sText.Format(GetResString(_T("STATS_FAILUPCOUNT")), statBadSessions, percentSessions);
					m_stattree.SetItemText(up_tsessions[1], sText);
					// Set Avg Upload time
					sText.Format(GetResString(_T("STATS_AVEUPTIME")), (LPCTSTR)CastSecondsToLngHM(thePrefs.GetUpAvgTime()));
					m_stattree.SetItemText(up_tsessions[3], sText);
				}

				if (UpOHTotal == 0 || UpOHTotalPackets == 0) {
					// Calculate Upload OH Totals
					UpOHTotal = theStats.GetUpDataOverheadFileRequest()
						+ theStats.GetUpDataOverheadSourceExchange()
						+ theStats.GetUpDataOverheadServer()
						+ theStats.GetUpDataOverheadKad()
						+ theStats.GetUpDataOverheadOther();
					UpOHTotalPackets = theStats.GetUpDataOverheadFileRequestPackets()
						+ theStats.GetUpDataOverheadSourceExchangePackets()
						+ theStats.GetUpDataOverheadServerPackets()
						+ theStats.GetUpDataOverheadKadPackets()
						+ theStats.GetUpDataOverheadOtherPackets();
				}
				// Set Cumulative Total Overhead
				sText.Format(GetResString(_T("TOVERHEAD")), (LPCTSTR)CastItoXBytes(UpOHTotal + thePrefs.GetUpOverheadTotal()), (LPCTSTR)CastItoIShort(UpOHTotalPackets + thePrefs.GetUpOverheadTotalPackets()));
				m_stattree.SetItemText(hup_toh, sText);
				if (forceUpdate || m_stattree.IsExpanded(hup_toh)) {
					int i = 0;
					// Set up total file req OH
					sText.Format(GetResString(_T("FROVERHEAD")), (LPCTSTR)CastItoXBytes(theStats.GetUpDataOverheadFileRequest() + thePrefs.GetUpOverheadFileReq()), (LPCTSTR)CastItoIShort(theStats.GetUpDataOverheadFileRequestPackets() + thePrefs.GetUpOverheadFileReqPackets()));
					m_stattree.SetItemText(up_toh[i], sText);
					++i;
					// Set up total source exch OH
					sText.Format(GetResString(_T("SSOVERHEAD")), (LPCTSTR)CastItoXBytes(theStats.GetUpDataOverheadSourceExchange() + thePrefs.GetUpOverheadSrcEx()), (LPCTSTR)CastItoIShort(theStats.GetUpDataOverheadSourceExchangePackets() + thePrefs.GetUpOverheadSrcExPackets()));
					m_stattree.SetItemText(up_toh[i], sText);
					++i;
					// Set up total server OH
					sText.Format(GetResString(_T("SOVERHEAD")),
						(LPCTSTR)CastItoXBytes(theStats.GetUpDataOverheadServer()
							+ thePrefs.GetUpOverheadServer(), false, false),
							(LPCTSTR)CastItoIShort(theStats.GetUpDataOverheadServerPackets()
								+ thePrefs.GetUpOverheadServerPackets()));
					m_stattree.SetItemText(up_toh[i], sText);
					++i;
					// Set up total Kad OH
					sText.Format(GetResString(_T("KADOVERHEAD")),
						(LPCTSTR)CastItoXBytes(theStats.GetUpDataOverheadKad() +
							thePrefs.GetUpOverheadKad(), false, false),
							(LPCTSTR)CastItoIShort(theStats.GetUpDataOverheadKadPackets() +
								thePrefs.GetUpOverheadKadPackets()));
					m_stattree.SetItemText(up_toh[i], sText);
				}
			} // - End Transfer -> Uploads -> Cumulative Section
		} // - End Transfer -> Uploads Section
	} // - END TRANSFER SECTION


	// CONNECTION SECTION
	if (forceUpdate || m_stattree.IsExpanded(h_connection)) {
		// CONNECTION -> SESSION SECTION
		if (forceUpdate || m_stattree.IsExpanded(h_conn_session)) {
			// CONNECTION -> SESSION -> GENERAL SECTION
			if (forceUpdate || m_stattree.IsExpanded(hconn_sg)) {
				int i = 0;
				// Server reconnects
				CString fmt = GetResString(_T("STATS_RECONNECTS"));
				TRACE(_T("Format STATS_RECONNECTS(session) = '%s'\n"), fmt);
				// Prefer using the provided format if it contains an unsigned placeholder; otherwise, append the number.
				const int posPct = fmt.Find(_T('%'));
				const uint32 val = theStats.reconnects ? theStats.reconnects - 1 : 0;
				if (posPct >= 0) {
					// Accept patterns ending with 'u' (e.g. "%u", "%5u", "%02u", etc.).
					bool ok = false;
					for (int i = posPct + 1; i < fmt.GetLength(); ++i) {
						TCHAR ch = fmt[i];
						if (_tcschr(_T("#0- +'IhlL0123456789.*"), ch) != nullptr) continue;
						ok = (ch == _T('u'));
						break;
					}
					if (ok) {
						sText.Format(fmt, val);
					} else {
						CString num; num.Format(_T("%u"), val);
						sText = fmt + _T(" ") + num;
					}
				} else {
					CString num; num.Format(_T("%u"), val);
					sText = fmt + _T(" ") + num;
				}
				m_stattree.SetItemText(conn_sg[i], sText);
				++i;
				// Active Connections
				sText.Format(_T("%s: %u (%s:%u | %s:%u | %s:%u)"), (LPCTSTR)GetResString(_T("SF_ACTIVECON")), theApp.listensocket->GetActiveConnections(), (LPCTSTR)GetResString(_T("HALF")), theApp.listensocket->GetTotalHalfCon(), (LPCTSTR)GetResString(_T("CONCOMPL")), theApp.listensocket->GetTotalComp(), (LPCTSTR)GetResString(_T("STATS_PRTOTHER")), theApp.listensocket->GetActiveConnections() - theApp.listensocket->GetTotalHalfCon() - theApp.listensocket->GetTotalComp());
				m_stattree.SetItemText(conn_sg[i], sText);
				++i;
				// Average Connections
				sText.Format(_T("%s: %i"), (LPCTSTR)GetResString(_T("SF_AVGCON")), (int)theApp.listensocket->GetAverageConnections());
				m_stattree.SetItemText(conn_sg[i], sText);
				++i;
				// Peak Connections
				sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("SF_PEAKCON")), theApp.listensocket->GetPeakConnections());
				m_stattree.SetItemText(conn_sg[i], sText);
				++i;
				// Connect Limit Reached
				uint32 m_itemp = theApp.listensocket->GetMaxConnectionReached();
				if (m_itemp != m_ilastMaxConnReached) {
					sText.Format(_T("%s: %u : %s"), (LPCTSTR)GetResString(_T("SF_MAXCONLIMITREACHED")), m_itemp, (LPCTSTR)CTime::GetCurrentTime().Format(_T("%c")));
					m_stattree.SetItemText(conn_sg[i], sText);
					m_ilastMaxConnReached = m_itemp;
				} else if (m_itemp == 0) {
					sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("SF_MAXCONLIMITREACHED")), m_itemp);
					m_stattree.SetItemText(conn_sg[i], sText);
				}
			} // - End Connection -> Session -> General Section
			// CONNECTION -> SESSION -> UPLOADS SECTION
			if (forceUpdate || m_stattree.IsExpanded(hconn_su)) {
				int i = 0;
				// Upload Rate
				sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("ST_UPLOAD")), (LPCTSTR)CastItoXBytes(theStats.rateUp, true, true));
				m_stattree.SetItemText(conn_su[i], sText);
				++i;
				// Average Upload Rate
				sText.Format(GetResString(_T("STATS_AVGUL")), (LPCTSTR)CastItoXBytes(theStats.GetAvgUploadRate(AVG_SESSION), true, true));
				m_stattree.SetItemText(conn_su[i], sText);
				++i;
				// Max Upload Rate
				sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_MAXUL")), (LPCTSTR)CastItoXBytes(theStats.maxUp, true, true));
				m_stattree.SetItemText(conn_su[i], sText);
				++i;
				// Max Average Upload Rate
				float myAverageUpRate = theStats.GetAvgUploadRate(AVG_SESSION);
				if (myAverageUpRate > theStats.maxUpavg)
					theStats.maxUpavg = myAverageUpRate;
				sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_MAXAVGUL")), (LPCTSTR)CastItoXBytes(theStats.maxUpavg, true, true));
				m_stattree.SetItemText(conn_su[i], sText);
			} // - End Connection -> Session -> Uploads Section
			// CONNECTION -> SESSION -> DOWNLOADShead SECTION
			if (forceUpdate || m_stattree.IsExpanded(hconn_sd)) {
				int i = 0;
				// Download Rate
				sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("ST_DOWNLOAD")), (LPCTSTR)CastItoXBytes(theStats.rateDown, true, true));
				m_stattree.SetItemText(conn_sd[i], sText);
				++i;
				// Average Download Rate
				sText.Format(GetResString(_T("STATS_AVGDL")), (LPCTSTR)CastItoXBytes(theStats.GetAvgDownloadRate(AVG_SESSION), true, true));
				m_stattree.SetItemText(conn_sd[i], sText);
				++i;
				// Max Download Rate
				sText.Format(GetResString(_T("STATS_MAXDL")), (LPCTSTR)CastItoXBytes(theStats.maxDown, true, true));
				m_stattree.SetItemText(conn_sd[i], sText);
				++i;
				// Max Average Download Rate
				float myAverageDownRate = theStats.GetAvgDownloadRate(AVG_SESSION);
				if (myAverageDownRate > theStats.maxDownavg)
					theStats.maxDownavg = myAverageDownRate;
				sText.Format(GetResString(_T("STATS_MAXAVGDL")), (LPCTSTR)CastItoXBytes(theStats.maxDownavg, true, true));
				m_stattree.SetItemText(conn_sd[i], sText);
			} // - End Connection -> Session -> Downloads Section
		} // - End Connection -> Session Section
		// CONNECTION -> CUMULATIVE SECTION
		if (forceUpdate || m_stattree.IsExpanded(h_conn_total)) {
			// CONNECTION -> CUMULATIVE -> GENERAL SECTION
			if (forceUpdate || m_stattree.IsExpanded(hconn_tg)) {
				int i = 0;
				// Server reconnects (cumulative)
				CString fmt = GetResString(_T("STATS_RECONNECTS"));
				const uint32 val = thePrefs.GetConnNumReconnects() + theStats.reconnects - static_cast<uint32>(theStats.reconnects > 0);
				const int posPct = fmt.Find(_T('%'));
				if (posPct >= 0) {
					bool ok = false;
					for (int i = posPct + 1; i < fmt.GetLength(); ++i) {
						TCHAR ch = fmt[i];
						if (_tcschr(_T("#0- +'IhlL0123456789.*"), ch) != nullptr) 
							continue;
						ok = (ch == _T('u'));
						break;
					}

					if (ok)
						sText.Format(fmt, val);
					else {
						CString num; num.Format(_T("%u"), val);
						sText = fmt + _T(" ") + num;
					}
				} else {
					CString num; num.Format(_T("%u"), val);
					sText = fmt + _T(" ") + num;
				}
				m_stattree.SetItemText(conn_tg[i], sText);
				++i;
				// Average Connections
				sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("SF_AVGCON")), (theApp.listensocket->GetActiveConnections() + thePrefs.GetConnAvgConnections()) / 2);
				m_stattree.SetItemText(conn_tg[i], sText);
				++i;
				// Peak Connections
				sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("SF_PEAKCON")), thePrefs.GetConnPeakConnections());
				m_stattree.SetItemText(conn_tg[i], sText);
				++i;
				// Connection Limit Reached
				sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("SF_MAXCONLIMITREACHED")), theApp.listensocket->GetMaxConnectionReached() + thePrefs.GetConnMaxConnLimitReached());
				m_stattree.SetItemText(conn_tg[i], sText);
			} // - End Connection -> Cumulative -> General Section
			// CONNECTION -> CUMULATIVE -> UPLOADS SECTION
			if (forceUpdate || m_stattree.IsExpanded(hconn_tu)) {
				int i = 0;
				// Average Upload Rate
				sText.Format(GetResString(_T("STATS_AVGUL")), (LPCTSTR)CastItoXBytes(theStats.cumUpavg, true, true));
				m_stattree.SetItemText(conn_tu[i], sText);
				++i;
				// Max Upload Rate
				sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_MAXUL")), (LPCTSTR)CastItoXBytes(theStats.maxcumUp, true, true));
				m_stattree.SetItemText(conn_tu[i], sText);
				++i;
				// Max Average Upload Rate
				sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_MAXAVGUL")), (LPCTSTR)CastItoXBytes(theStats.maxcumUpavg, true, true));
				m_stattree.SetItemText(conn_tu[i], sText);
			} // - End Connection -> Cumulative -> Uploads Section
			// CONNECTION -> CUMULATIVE -> DOWNLOADS SECTION
			if (forceUpdate || m_stattree.IsExpanded(hconn_td)) {
				int i = 0;
				// Average Download Rate
				sText.Format(GetResString(_T("STATS_AVGDL")), (LPCTSTR)CastItoXBytes(theStats.cumDownavg, true, true));
				m_stattree.SetItemText(conn_td[i], sText);
				++i;
				// Max Download Rate
				sText.Format(GetResString(_T("STATS_MAXDL")), (LPCTSTR)CastItoXBytes(theStats.maxcumDown, true, true));
				m_stattree.SetItemText(conn_td[i], sText);
				++i;
				// Max Average Download Rate
				sText.Format(GetResString(_T("STATS_MAXAVGDL")), (LPCTSTR)CastItoXBytes(theStats.maxcumDownavg, true, true));
				m_stattree.SetItemText(conn_td[i], sText);
			} // - End Connection -> Cumulative -> Downloads Section
		} // - End Connection -> Cumulative Section
	} // - END CONNECTION SECTION


	// TIME STATISTICS SECTION
	if (forceUpdate || m_stattree.IsExpanded(h_time)) {
		// Statistics Last Reset
		sText.Format(GetResString(_T("STATS_LASTRESETSTATIC")), (LPCTSTR)thePrefs.GetStatsLastResetStr(false));
		m_stattree.SetItemText(tvitime[0], sText);
		// Time Since Last Reset
		time_t timeDiff;
		if (thePrefs.GetStatsLastResetLng()) {
			time_t timeNow;
			time(&timeNow);
			timeDiff = timeNow - thePrefs.GetStatsLastResetLng(); // In seconds
			sText.Format(GetResString(_T("STATS_TIMESINCERESET")), (LPCTSTR)CastSecondsToLngHM(timeDiff));
		} else {
			timeDiff = 0;
			sText.Format(GetResString(_T("STATS_TIMESINCERESET")), (LPCTSTR)GetResString(_T("UNKNOWN")));
		}
		m_stattree.SetItemText(tvitime[1], sText);
		// TIME STATISTICS -> SESSION SECTION
		if (forceUpdate || m_stattree.IsExpanded(htime_s)) {
			int i = 0;
			// Run Time
			time_t sessionRunTime = (::GetTickCount() - theStats.starttime) / SEC2MS(1);
			sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_RUNTIME")), (LPCTSTR)CastSecondsToLngHM(sessionRunTime));
			m_stattree.SetItemText(tvitime_s[i], sText);
			++i;
			if (!sessionRunTime)
				sessionRunTime = 1;
			// Transfer Time
			sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_TRANSTIME")), (LPCTSTR)CastSecondsToLngHM(theStats.GetTransferTime()), (100.0 * theStats.GetTransferTime()) / sessionRunTime);
			m_stattree.SetItemText(tvitime_s[i], sText);
			if (forceUpdate || m_stattree.IsExpanded(tvitime_s[i])) {
				int x = 0;
				// Upload Time
				sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_UPTIME")), (LPCTSTR)CastSecondsToLngHM(theStats.GetUploadTime()), (100.0 * theStats.GetUploadTime()) / sessionRunTime);
				m_stattree.SetItemText(tvitime_st[x], sText);
				++x;
				// Download Time
				sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_DOWNTIME")), (LPCTSTR)CastSecondsToLngHM(theStats.GetDownloadTime()), (100.0 * theStats.GetDownloadTime()) / sessionRunTime);
				m_stattree.SetItemText(tvitime_st[x], sText);
			}
			++i;
			// Current Server Duration
			sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_CURRSRVDUR")), (LPCTSTR)CastSecondsToLngHM(theStats.time_thisServerDuration), (100.0 * theStats.time_thisServerDuration) / sessionRunTime);
			m_stattree.SetItemText(tvitime_s[i], sText);
			++i;
			// Total Server Duration
			sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_TOTALSRVDUR")), (LPCTSTR)CastSecondsToLngHM(theStats.GetServerDuration()), (100.0 * theStats.GetServerDuration()) / sessionRunTime);
			m_stattree.SetItemText(tvitime_s[i], sText);
		}
		// TIME STATISTICS -> CUMULATIVE SECTION
		if (forceUpdate || m_stattree.IsExpanded(htime_t)) {
			int i = 0;
			// Run Time
			time_t totalRunTime = (::GetTickCount() - theStats.starttime) / SEC2MS(1) + thePrefs.GetConnRunTime();
			sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_RUNTIME")), (LPCTSTR)CastSecondsToLngHM(totalRunTime));
			m_stattree.SetItemText(tvitime_t[i], sText);
			++i;
			if (!totalRunTime)
				totalRunTime = 1;
			// Transfer Time
			sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_TRANSTIME")), (LPCTSTR)CastSecondsToLngHM(theStats.GetTransferTime() + thePrefs.GetConnTransferTime()), (100.0 * (theStats.GetTransferTime() + thePrefs.GetConnTransferTime())) / totalRunTime);
			m_stattree.SetItemText(tvitime_t[i], sText);
			if (forceUpdate || m_stattree.IsExpanded(tvitime_t[i])) {
				int x = 0;
				// Upload Time
				sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_UPTIME")), (LPCTSTR)CastSecondsToLngHM(theStats.GetUploadTime() + thePrefs.GetConnUploadTime()), (100.0 * (theStats.GetUploadTime() + thePrefs.GetConnUploadTime())) / totalRunTime);
				m_stattree.SetItemText(tvitime_tt[x], sText);
				++x;
				// Download Time
				sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_DOWNTIME")), (LPCTSTR)CastSecondsToLngHM(theStats.GetDownloadTime() + thePrefs.GetConnDownloadTime()), (100.0 * (theStats.GetDownloadTime() + thePrefs.GetConnDownloadTime())) / totalRunTime);
				m_stattree.SetItemText(tvitime_tt[x], sText);
			}
			++i;
			// Total Server Duration
			sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_TOTALSRVDUR")), (LPCTSTR)CastSecondsToLngHM(theStats.GetServerDuration() + thePrefs.GetConnServerDuration()), (100.0 * (theStats.GetServerDuration() + thePrefs.GetConnServerDuration())) / totalRunTime);
			m_stattree.SetItemText(tvitime_t[i], sText);
		}
		// TIME STATISTICS -> PROJECTED AVERAGES SECTION
		if ((forceUpdate || m_stattree.IsExpanded(htime_aap)) && timeDiff > 0) {
			double avgModifier[3];
			avgModifier[0] = (double)DAY2S(1) / timeDiff; // Days
			avgModifier[1] = (double)2629746 / timeDiff; // Months - 1/12 of Gregorian year
			avgModifier[2] = (double)31556952 / timeDiff; // Years - Gregorian year in seconds
			// TIME STATISTICS -> PROJECTED AVERAGES -> TIME PERIODS
			// This section is completely scalable.  Might add "Week" to it in the future.
			// For each time period that we are calculating a projected average for...
			for (int mx = 0; mx < 3; ++mx) {
				if (forceUpdate || m_stattree.IsExpanded(time_aaph[mx])) {
					// TIME STATISTICS -> PROJECTED AVERAGES -> TIME PERIOD -> UPLOADS SECTION
					if (forceUpdate || m_stattree.IsExpanded(time_aap_hup[mx])) {
						// Uploaded Data
						sText.Format(GetResString(_T("STATS_UDATA")), (LPCTSTR)CastItoXBytes(((double)(theStats.sessionSentBytes + thePrefs.GetTotalUploaded()))*avgModifier[mx]));
						m_stattree.SetItemText(time_aap_up[mx][0], sText);
						if (forceUpdate || m_stattree.IsExpanded(time_aap_up[mx][0])) {
							// Uploaded Data By Client
							if (forceUpdate || m_stattree.IsExpanded(time_aap_up_hd[mx][0])) {
								int i = 0;
								uint64 UpDataTotal = (uint64)(thePrefs.GetUpTotalClientData() * avgModifier[mx]);
								uint64 UpDataClient = (uint64)(thePrefs.GetCumUpData_EMULE() * avgModifier[mx]);
								double percentClientTransferred;
								percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
								sText.Format(_T("eMule: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
								m_stattree.SetItemText(time_aap_up_dc[mx][i], sText);
								++i;
								UpDataClient = (uint64)(thePrefs.GetCumUpData_EDONKEYHYBRID() * avgModifier[mx]);
								percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
								sText.Format(_T("eD Hybrid: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
								m_stattree.SetItemText(time_aap_up_dc[mx][i], sText);
								++i;
								UpDataClient = (uint64)(thePrefs.GetCumUpData_EDONKEY() * avgModifier[mx]);
								percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
								sText.Format(_T("eDonkey: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
								m_stattree.SetItemText(time_aap_up_dc[mx][i], sText);
								++i;
								UpDataClient = (uint64)(thePrefs.GetCumUpData_AMULE() * avgModifier[mx]);
								percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
								sText.Format(_T("aMule: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
								m_stattree.SetItemText(time_aap_up_dc[mx][i], sText);
								++i;
								UpDataClient = (uint64)(thePrefs.GetCumUpData_MLDONKEY() * avgModifier[mx]);
								percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
								sText.Format(_T("MLdonkey: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
								m_stattree.SetItemText(time_aap_up_dc[mx][i], sText);
								++i;
								UpDataClient = (uint64)(thePrefs.GetCumUpData_SHAREAZA() * avgModifier[mx]);
								percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
								sText.Format(_T("Shareaza: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
								m_stattree.SetItemText(time_aap_up_dc[mx][i], sText);
								++i;
								UpDataClient = (uint64)(thePrefs.GetCumUpData_EMULECOMPAT() * avgModifier[mx]);
								percentClientTransferred = !UpDataTotal ? 0 : 100.0 * UpDataClient / UpDataTotal;
								sText.Format(_T("eM Compat: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(UpDataClient), percentClientTransferred);
								m_stattree.SetItemText(time_aap_up_dc[mx][i], sText);
							}
							// Uploaded Data By Port
							if (forceUpdate || m_stattree.IsExpanded(time_aap_up_hd[mx][1])) {
								int i = 0;
								uint64	PortDataDefault = (uint64)(thePrefs.GetCumUpDataPort_4662() * avgModifier[mx]);
								uint64	PortDataOther = (uint64)(thePrefs.GetCumUpDataPort_OTHER() * avgModifier[mx]);
								uint64	PortDataTotal = (uint64)(thePrefs.GetUpTotalPortData() * avgModifier[mx]);
								double	percentPortTransferred = 0;

								if (PortDataTotal != 0 && PortDataDefault != 0)
									percentPortTransferred = 100.0 * PortDataDefault / PortDataTotal;
								sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_PRTDEF")), (LPCTSTR)CastItoXBytes(PortDataDefault), percentPortTransferred);
								m_stattree.SetItemText(time_aap_up_dp[mx][i], sText);
								++i;
								if (PortDataTotal != 0 && PortDataOther != 0)
									percentPortTransferred = 100.0 * PortDataOther / PortDataTotal;
								else
									percentPortTransferred = 0;
								sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_PRTOTHER")), (LPCTSTR)CastItoXBytes(PortDataOther), percentPortTransferred);
								m_stattree.SetItemText(time_aap_up_dp[mx][i], sText);
							}
							// Uploaded Data By Source
							if (forceUpdate || m_stattree.IsExpanded(time_aap_up_hd[mx][2])) {
								int i = 0;
								uint64	DataSourceTotal = (uint64)(thePrefs.GetUpTotalDataFile() * avgModifier[mx]);

								uint64	DataSourceFile = (uint64)(thePrefs.GetCumUpData_File() * avgModifier[mx]);
								double	percentFileTransferred = !DataSourceTotal ? 0 : 100.0 * DataSourceFile / DataSourceTotal;
								sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_DSFILE")), (LPCTSTR)CastItoXBytes(DataSourceFile), percentFileTransferred);
								m_stattree.SetItemText(time_aap_up_ds[mx][i], sText);
								++i;
								uint64	DataSourcePF = (uint64)(thePrefs.GetCumUpData_Partfile() * avgModifier[mx]);
								percentFileTransferred = !DataSourceTotal ? 0 : 100.0 * DataSourcePF / DataSourceTotal;
								sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_DSPF")), (LPCTSTR)CastItoXBytes(DataSourcePF), percentFileTransferred);
								m_stattree.SetItemText(time_aap_up_ds[mx][i], sText);
							}
						}
						// Upload Sessions
						uint32 statGoodSessions = (uint32)((theApp.uploadqueue->GetSuccessfullUpCount() + thePrefs.GetUpSuccessfulSessions() + theApp.uploadqueue->GetUploadQueueLength()) * avgModifier[mx]);
						uint32 statBadSessions = (uint32)((theApp.uploadqueue->GetFailedUpCount() + thePrefs.GetUpFailedSessions()) * avgModifier[mx]);
						sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("STATS_ULSES")), statGoodSessions + statBadSessions);
						m_stattree.SetItemText(time_aap_up[mx][1], sText);
						if (forceUpdate || m_stattree.IsExpanded(time_aap_up[mx][1])) {
							double percentSessions;
							// Set Successful Upload Sessions
							if (statGoodSessions > 0)
								percentSessions = (100.0 * statGoodSessions) / (statGoodSessions + statBadSessions);
							else
								percentSessions = 0;
							sText.Format(GetResString(_T("STATS_SUCCUPCOUNT")), statGoodSessions, percentSessions);
							m_stattree.SetItemText(time_aap_up_s[mx][0], sText);
							// Set Failed Upload Sessions
							if (statBadSessions > 0)
								percentSessions = 100 - percentSessions; // There were bad sessions
							else
								percentSessions = 0; // No bad sessions at all
							sText.Format(GetResString(_T("STATS_FAILUPCOUNT")), statBadSessions, percentSessions);
							m_stattree.SetItemText(time_aap_up_s[mx][1], sText);
						}

						// Calculate Upload OH Totals
						uint64 UpOHTotal = (uint64)((theStats.GetUpDataOverheadFileRequest()
							+ theStats.GetUpDataOverheadSourceExchange()
							+ theStats.GetUpDataOverheadServer()
							+ theStats.GetUpDataOverheadKad()
							+ theStats.GetUpDataOverheadOther()
							) * avgModifier[mx]);
						uint64 UpOHTotalPackets = (uint64)((theStats.GetUpDataOverheadFileRequestPackets()
							+ theStats.GetUpDataOverheadSourceExchangePackets()
							+ theStats.GetUpDataOverheadServerPackets()
							+ theStats.GetUpDataOverheadKadPackets()
							+ theStats.GetUpDataOverheadOtherPackets()
							) * avgModifier[mx]);

						// Set Cumulative Total Overhead
						sText.Format(GetResString(_T("TOVERHEAD")), (LPCTSTR)CastItoXBytes(UpOHTotal + ((uint64)thePrefs.GetUpOverheadTotal() * avgModifier[mx])), (LPCTSTR)CastItoIShort((uint64)(UpOHTotalPackets + ((uint64)thePrefs.GetUpOverheadTotalPackets() * avgModifier[mx]))));
						m_stattree.SetItemText(time_aap_up[mx][2], sText);
						if (forceUpdate || m_stattree.IsExpanded(time_aap_up[mx][2])) {
							int i = 0;
							// Set up total file req OH
							sText.Format(GetResString(_T("FROVERHEAD")), (LPCTSTR)CastItoXBytes((uint64)(theStats.GetUpDataOverheadFileRequest() + thePrefs.GetUpOverheadFileReq()) * avgModifier[mx]), (LPCTSTR)CastItoIShort((uint64)(theStats.GetUpDataOverheadFileRequestPackets() + thePrefs.GetUpOverheadFileReqPackets()) * avgModifier[mx]));
							m_stattree.SetItemText(time_aap_up_oh[mx][i], sText);
							++i;
							// Set up total source exch OH
							sText.Format(GetResString(_T("SSOVERHEAD")), (LPCTSTR)CastItoXBytes((uint64)(theStats.GetUpDataOverheadSourceExchange() + thePrefs.GetUpOverheadSrcEx()) * avgModifier[mx]), (LPCTSTR)CastItoIShort((uint64)(theStats.GetUpDataOverheadSourceExchangePackets() + thePrefs.GetUpOverheadSrcExPackets()) * avgModifier[mx]));
							m_stattree.SetItemText(time_aap_up_oh[mx][i], sText);
							++i;
							// Set up total server OH
							sText.Format(GetResString(_T("SOVERHEAD"))
								, (LPCTSTR)CastItoXBytes((theStats.GetUpDataOverheadServer()
									+ thePrefs.GetUpOverheadServer()) * avgModifier[mx])
								, (LPCTSTR)CastItoIShort((uint64)(theStats.GetUpDataOverheadServerPackets()
									+ thePrefs.GetUpOverheadServerPackets()) * avgModifier[mx]));
							m_stattree.SetItemText(time_aap_up_oh[mx][i], sText);
							++i;
							// Set up total Kad OH
							sText.Format(GetResString(_T("KADOVERHEAD"))
								, (LPCTSTR)CastItoXBytes((uint64)(theStats.GetUpDataOverheadKad()
									+ thePrefs.GetUpOverheadKad()) * avgModifier[mx])
								, (LPCTSTR)CastItoIShort((uint64)(theStats.GetUpDataOverheadKadPackets()
									+ thePrefs.GetUpOverheadKadPackets()) * avgModifier[mx]));
							m_stattree.SetItemText(time_aap_up_oh[mx][i], sText);
						}
					} // - End Time Statistics -> Projected Averages -> Time Period -> Uploads Section
					// TIME STATISTICS -> PROJECTED AVERAGES -> TIME PERIOD -> DOWNLOADS SECTION
					if (forceUpdate || m_stattree.IsExpanded(time_aap_hdown[mx])) {
						CDownloadQueue::SDownloadStats myStats;
						theApp.downloadqueue->GetDownloadSourcesStats(myStats);
						// Downloaded Data
						sText.Format(GetResString(_T("STATS_DDATA")), (LPCTSTR)CastItoXBytes((uint64)(theStats.sessionReceivedBytes + thePrefs.GetTotalDownloaded()) * avgModifier[mx]));
						m_stattree.SetItemText(time_aap_down[mx][0], sText);
						if (forceUpdate || m_stattree.IsExpanded(time_aap_down[mx][0])) {
							// Downloaded Data By Client
							if (forceUpdate || m_stattree.IsExpanded(time_aap_down_hd[mx][0])) {
								int i = 0;
								uint64 DownDataTotal = (uint64)(thePrefs.GetDownTotalClientData() * avgModifier[mx]);
								uint64 DownDataClient = (uint64)(thePrefs.GetCumDownData_EMULE() * avgModifier[mx]);
								double percentClientTransferred;
								if (DownDataTotal != 0 && DownDataClient != 0)
									percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
								else
									percentClientTransferred = 0;
								sText.Format(_T("eMule: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
								m_stattree.SetItemText(time_aap_down_dc[mx][i], sText);
								++i;
								DownDataClient = (uint64)(thePrefs.GetCumDownData_EDONKEYHYBRID() * avgModifier[mx]);
								if (DownDataTotal != 0 && DownDataClient != 0)
									percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
								else
									percentClientTransferred = 0;
								sText.Format(_T("eD Hybrid: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
								m_stattree.SetItemText(time_aap_down_dc[mx][i], sText);
								++i;
								DownDataClient = (uint64)(thePrefs.GetCumDownData_EDONKEY() * avgModifier[mx]);
								if (DownDataTotal != 0 && DownDataClient != 0)
									percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
								else
									percentClientTransferred = 0;
								sText.Format(_T("eDonkey: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
								m_stattree.SetItemText(time_aap_down_dc[mx][i], sText);
								++i;
								DownDataClient = (uint64)(thePrefs.GetCumDownData_AMULE() * avgModifier[mx]);
								if (DownDataTotal != 0 && DownDataClient != 0)
									percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
								else
									percentClientTransferred = 0;
								sText.Format(_T("aMule: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
								m_stattree.SetItemText(time_aap_down_dc[mx][i], sText);
								++i;
								DownDataClient = (uint64)(thePrefs.GetCumDownData_MLDONKEY() * avgModifier[mx]);
								if (DownDataTotal != 0 && DownDataClient != 0)
									percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
								else
									percentClientTransferred = 0;
								sText.Format(_T("MLdonkey: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
								m_stattree.SetItemText(time_aap_down_dc[mx][i], sText);
								++i;
								DownDataClient = (uint64)(thePrefs.GetCumDownData_SHAREAZA() * avgModifier[mx]);
								if (DownDataTotal != 0 && DownDataClient != 0)
									percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
								else
									percentClientTransferred = 0;
								sText.Format(_T("Shareaza: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
								m_stattree.SetItemText(time_aap_down_dc[mx][i], sText);
								++i;
								DownDataClient = (uint64)(thePrefs.GetCumDownData_EMULECOMPAT() * avgModifier[mx]);
								if (DownDataTotal != 0 && DownDataClient != 0)
									percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
								else
									percentClientTransferred = 0;
								sText.Format(_T("eM Compat: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
								m_stattree.SetItemText(time_aap_down_dc[mx][i], sText);
								++i;
								DownDataClient = (uint64)(thePrefs.GetCumDownData_URL() * avgModifier[mx]);
								if (DownDataTotal != 0 && DownDataClient != 0)
									percentClientTransferred = 100.0 * DownDataClient / DownDataTotal;
								else
									percentClientTransferred = 0;
								sText.Format(_T("URL: %s (%1.1f%%)"), (LPCTSTR)CastItoXBytes(DownDataClient), percentClientTransferred);
								m_stattree.SetItemText(time_aap_down_dc[mx][i], sText);
							}
							// Downloaded Data By Port
							if (forceUpdate || m_stattree.IsExpanded(time_aap_down_hd[mx][1])) {
								int i = 0;
								uint64	PortDataDefault = (uint64)(thePrefs.GetCumDownDataPort_4662() * avgModifier[mx]);
								uint64	PortDataOther = (uint64)(thePrefs.GetCumDownDataPort_OTHER() * avgModifier[mx]);
								uint64	PortDataTotal = (uint64)(thePrefs.GetDownTotalPortData() * avgModifier[mx]);
								double	percentPortTransferred;

								if (PortDataTotal != 0 && PortDataDefault != 0)
									percentPortTransferred = 100.0 * PortDataDefault / PortDataTotal;
								else
									percentPortTransferred = 0;
								sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_PRTDEF")), (LPCTSTR)CastItoXBytes(PortDataDefault), percentPortTransferred);
								m_stattree.SetItemText(time_aap_down_dp[mx][i], sText);
								++i;

								if (PortDataTotal != 0 && PortDataOther != 0)
									percentPortTransferred = 100.0 * PortDataOther / PortDataTotal;
								else
									percentPortTransferred = 0;
								sText.Format(_T("%s: %s (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_PRTOTHER")), (LPCTSTR)CastItoXBytes(PortDataOther), percentPortTransferred);
								m_stattree.SetItemText(time_aap_down_dp[mx][i], sText);
							}
						}
						// Set Cum Completed Downloads
						sText.Format(_T("%s: %I64u"), (LPCTSTR)GetResString(_T("STATS_COMPDL")), (uint64)(thePrefs.GetDownCompletedFiles() * avgModifier[mx]));
						m_stattree.SetItemText(time_aap_down[mx][1], sText);
						// Set Cum Download Sessions
						uint32 statGoodSessions = (uint32)((thePrefs.GetDownC_SuccessfulSessions() + myStats.a[1]) * avgModifier[mx]);
						uint32 statBadSessions = (uint32)(thePrefs.GetDownC_FailedSessions() * avgModifier[mx]);
						sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("STATS_DLSES")), statGoodSessions + statBadSessions);
						m_stattree.SetItemText(time_aap_down[mx][2], sText);
						if (forceUpdate || m_stattree.IsExpanded(time_aap_down[mx][2])) {
							double	percentSessions;
							// Set Cum Successful Download Sessions
							if (statGoodSessions > 0)
								percentSessions = 100.0 * statGoodSessions / (statGoodSessions + statBadSessions);
							else
								percentSessions = 0;
							sText.Format(_T("%s: %u (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_SDLSES")), statGoodSessions, percentSessions);
							m_stattree.SetItemText(time_aap_down_s[mx][0], sText); // Set Successful Sessions
							// Set Cum Failed Download Sessions
							if (percentSessions != 0 && statBadSessions > 0)
								percentSessions = 100 - percentSessions; // There were some good sessions and bad ones...
							else if (percentSessions == 0 && statBadSessions > 0)
								percentSessions = 100; // There were bad sessions and no good ones, must be 100%
							else
								percentSessions = 0; // No sessions at all, or no bad ones.
							sText.Format(_T("%s: %u (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_FDLSES")), statBadSessions, percentSessions);
							m_stattree.SetItemText(time_aap_down_s[mx][1], sText);
						}
						// Set Cumulative Gained Due To Compression
						sText.Format(GetResString(_T("STATS_GAINCOMP")), (LPCTSTR)CastItoXBytes((uint64)(thePrefs.GetSesSavedFromCompression() + thePrefs.GetCumSavedFromCompression()) * avgModifier[mx]));
						m_stattree.SetItemText(time_aap_down[mx][3], sText);
						// Set Cumulative Lost Due To Corruption
						sText.Format(GetResString(_T("STATS_LOSTCORRUPT")), (LPCTSTR)CastItoXBytes((uint64)(thePrefs.GetSesLostFromCorruption() + thePrefs.GetCumLostFromCorruption()) * avgModifier[mx]));
						m_stattree.SetItemText(time_aap_down[mx][4], sText);
						// Set Cumulative Saved Due To ICH
						sText.Format(GetResString(_T("STATS_ICHSAVED")), (uint32)((thePrefs.GetSesPartsSavedByICH() + thePrefs.GetCumPartsSavedByICH()) * avgModifier[mx]));
						m_stattree.SetItemText(time_aap_down[mx][5], sText);

						uint64 DownOHTotal = theStats.GetDownDataOverheadFileRequest()
							+ theStats.GetDownDataOverheadSourceExchange()
							+ theStats.GetDownDataOverheadServer()
							+ theStats.GetDownDataOverheadKad()
							+ theStats.GetDownDataOverheadOther();
						uint64 DownOHTotalPackets = theStats.GetDownDataOverheadFileRequestPackets()
							+ theStats.GetDownDataOverheadSourceExchangePackets()
							+ theStats.GetDownDataOverheadServerPackets()
							+ theStats.GetDownDataOverheadKadPackets()
							+ theStats.GetDownDataOverheadOtherPackets();
						// Total Overhead
						sText.Format(GetResString(_T("TOVERHEAD")), (LPCTSTR)CastItoXBytes((uint64)(DownOHTotal + thePrefs.GetDownOverheadTotal()) * avgModifier[mx]), (LPCTSTR)CastItoIShort((uint64)(DownOHTotalPackets + thePrefs.GetDownOverheadTotalPackets()) * avgModifier[mx]));
						m_stattree.SetItemText(time_aap_down[mx][6], sText);
						if (forceUpdate || m_stattree.IsExpanded(time_aap_down[mx][6])) {
							int i = 0;
							// File Request Overhead
							sText.Format(GetResString(_T("FROVERHEAD")), (LPCTSTR)CastItoXBytes((uint64)(theStats.GetDownDataOverheadFileRequest() + thePrefs.GetDownOverheadFileReq()) * avgModifier[mx]), (LPCTSTR)CastItoIShort((uint64)(theStats.GetDownDataOverheadFileRequestPackets() + thePrefs.GetDownOverheadFileReqPackets()) * avgModifier[mx]));
							m_stattree.SetItemText(time_aap_down_oh[mx][i], sText);
							++i;
							// Source Exchange Overhead
							sText.Format(GetResString(_T("SSOVERHEAD")), (LPCTSTR)CastItoXBytes((uint64)(theStats.GetDownDataOverheadSourceExchange() + thePrefs.GetDownOverheadSrcEx()) * avgModifier[mx]), (LPCTSTR)CastItoIShort((uint64)(theStats.GetDownDataOverheadSourceExchangePackets() + thePrefs.GetDownOverheadSrcExPackets()) * avgModifier[mx]));
							m_stattree.SetItemText(time_aap_down_oh[mx][i], sText);
							++i;
							// Server Overhead
							sText.Format(GetResString(_T("SOVERHEAD"))
								, (LPCTSTR)CastItoXBytes((uint64)(theStats.GetDownDataOverheadServer()
									+ thePrefs.GetDownOverheadServer()) * avgModifier[mx])
								, (LPCTSTR)CastItoIShort((uint64)(theStats.GetDownDataOverheadServerPackets() +
									+ thePrefs.GetDownOverheadServerPackets()) * avgModifier[mx]));
							m_stattree.SetItemText(time_aap_down_oh[mx][i], sText);
							++i;
							// Kad Overhead
							sText.Format(GetResString(_T("KADOVERHEAD"))
								, (LPCTSTR)CastItoXBytes((uint64)(theStats.GetDownDataOverheadKad()
									+ thePrefs.GetDownOverheadKad()) * avgModifier[mx])
								, (LPCTSTR)CastItoIShort((uint64)(theStats.GetDownDataOverheadKadPackets()
									+ thePrefs.GetDownOverheadKadPackets()) * avgModifier[mx]));
							m_stattree.SetItemText(time_aap_down_oh[mx][i], sText);
						}
					} // - End Time Statistics -> Projected Averages -> Time Period -> Downloads Section
				} // - End Time Statistics -> Projected Averages -> Time Period Sections
			} // - End Time Statistics -> Projected Averages Section
		} // - End Time Statistics -> Projected Averages Section Loop
	} // - END TIME STATISTICS SECTION


	// CLIENTS SECTION
	if (forceUpdate || m_stattree.IsExpanded(h_clients)) {
		CClientStatsSnapshot snapshotActive;
		theApp.clientlist->GetStatistics(snapshotActive, ClientPopulationActive);
		CClientStatsSnapshot snapshotArchived;

	if (m_useClientHistoryUI) {
		theApp.clientlist->GetStatistics(snapshotArchived, ClientPopulationArchived);

		// Create combined snapshot for All branch (must be manually built due to CMap copy restrictions)
		CClientStatsSnapshot snapshotAll;
		snapshotAll.totalClients = snapshotActive.totalClients + snapshotArchived.totalClients;
		snapshotAll.totalMODs = snapshotActive.totalMODs + snapshotArchived.totalMODs;
		snapshotAll.aiClientTotal = snapshotActive.aiClientTotal + snapshotArchived.aiClientTotal;
		
		// Combine Banned/Filtered/Bad counts
		snapshotAll.bannedCount = snapshotActive.bannedCount + snapshotArchived.bannedCount;
		snapshotAll.filteredCount = theStats.filteredclients; // Global filtered count (not tracked per population)
		snapshotAll.badCount = snapshotActive.badCount + snapshotArchived.badCount;

		// Combine stats arrays
		for (int i = 0; i < NUM_CLIENTLIST_STATS; ++i)
			snapshotAll.stats[i] = snapshotActive.stats[i] + snapshotArchived.stats[i];

		// Combine version maps (for detailed breakdown)
		// First copy Active data
		for (POSITION pos = snapshotActive.clientVersionEMule.GetStartPosition(); pos != NULL;) {
			uint32 version, count;
			snapshotActive.clientVersionEMule.GetNextAssoc(pos, version, count);
			snapshotAll.clientVersionEMule[version] = count;
		}
		// Then add Archived data
		for (POSITION pos = snapshotArchived.clientVersionEMule.GetStartPosition(); pos != NULL;) {
			uint32 version, count;
			snapshotArchived.clientVersionEMule.GetNextAssoc(pos, version, count);
			uint32 existing = 0;
			snapshotAll.clientVersionEMule.Lookup(version, existing);
			snapshotAll.clientVersionEMule[version] = existing + count;
		}

		for (POSITION pos = snapshotActive.clientVersionEDonkeyHybrid.GetStartPosition(); pos != NULL;) {
			uint32 version, count;
			snapshotActive.clientVersionEDonkeyHybrid.GetNextAssoc(pos, version, count);
			snapshotAll.clientVersionEDonkeyHybrid[version] = count;
		}
		for (POSITION pos = snapshotArchived.clientVersionEDonkeyHybrid.GetStartPosition(); pos != NULL;) {
			uint32 version, count;
			snapshotArchived.clientVersionEDonkeyHybrid.GetNextAssoc(pos, version, count);
			uint32 existing = 0;
			snapshotAll.clientVersionEDonkeyHybrid.Lookup(version, existing);
			snapshotAll.clientVersionEDonkeyHybrid[version] = existing + count;
		}

		for (POSITION pos = snapshotActive.clientVersionEDonkey.GetStartPosition(); pos != NULL;) {
			uint32 version, count;
			snapshotActive.clientVersionEDonkey.GetNextAssoc(pos, version, count);
			snapshotAll.clientVersionEDonkey[version] = count;
		}
		for (POSITION pos = snapshotArchived.clientVersionEDonkey.GetStartPosition(); pos != NULL;) {
			uint32 version, count;
			snapshotArchived.clientVersionEDonkey.GetNextAssoc(pos, version, count);
			uint32 existing = 0;
			snapshotAll.clientVersionEDonkey.Lookup(version, existing);
			snapshotAll.clientVersionEDonkey[version] = existing + count;
		}

		for (POSITION pos = snapshotActive.clientVersionAMule.GetStartPosition(); pos != NULL;) {
			uint32 version, count;
			snapshotActive.clientVersionAMule.GetNextAssoc(pos, version, count);
			snapshotAll.clientVersionAMule[version] = count;
		}
		for (POSITION pos = snapshotArchived.clientVersionAMule.GetStartPosition(); pos != NULL;) {
			uint32 version, count;
			snapshotArchived.clientVersionAMule.GetNextAssoc(pos, version, count);
			uint32 existing = 0;
			snapshotAll.clientVersionAMule.Lookup(version, existing);
			snapshotAll.clientVersionAMule[version] = existing + count;
		}

		for (POSITION pos = snapshotActive.aiVersionMap.GetStartPosition(); pos != NULL;) {
			CString version;
			uint32 count;
			snapshotActive.aiVersionMap.GetNextAssoc(pos, version, count);
			snapshotAll.aiVersionMap[version] = count;
		}
		for (POSITION pos = snapshotArchived.aiVersionMap.GetStartPosition(); pos != NULL;) {
			CString version;
			uint32 count;
			snapshotArchived.aiVersionMap.GetNextAssoc(pos, version, count);
			uint32 existing = 0;
			snapshotAll.aiVersionMap.Lookup(version, existing);
			snapshotAll.aiVersionMap[version] = existing + count;
		}

		for (POSITION pos = snapshotActive.modCounts.GetStartPosition(); pos != NULL;) {
			CString modName;
			uint32 count;
			snapshotActive.modCounts.GetNextAssoc(pos, modName, count);
			snapshotAll.modCounts[modName] = count;
		}
		for (POSITION pos = snapshotArchived.modCounts.GetStartPosition(); pos != NULL;) {
			CString modName;
			uint32 count;
			snapshotArchived.modCounts.GetNextAssoc(pos, modName, count);
			uint32 existing = 0;
			snapshotAll.modCounts.Lookup(modName, existing);
			snapshotAll.modCounts[modName] = existing + count;
		}

		for (POSITION pos = snapshotActive.countries.GetStartPosition(); pos != NULL;) {
			CString countryName;
			uint32 count;
			snapshotActive.countries.GetNextAssoc(pos, countryName, count);
			snapshotAll.countries[countryName] = count;
		}
		for (POSITION pos = snapshotArchived.countries.GetStartPosition(); pos != NULL;) {
			CString countryName;
			uint32 count;
			snapshotArchived.countries.GetNextAssoc(pos, countryName, count);
			uint32 existing = 0;
			snapshotAll.countries.Lookup(countryName, existing);
			snapshotAll.countries[countryName] = existing + count;
		}

			// Update all three branches
			UpdateClientBranch(ClientPopulationAll, snapshotAll, forceUpdate);
			UpdateClientBranch(ClientPopulationActive, snapshotActive, forceUpdate);
			UpdateClientBranch(ClientPopulationArchived, snapshotArchived, forceUpdate);
		} else {
			// When Client History is disabled, only update Active
			UpdateClientBranch(ClientPopulationActive, snapshotActive, forceUpdate);
		}
	}

	// UPDATE RECORDS FOR SERVERS AND SHARED FILES
	thePrefs.SetRecordStructMembers();

	// SERVERS SECTION
	if (forceUpdate || m_stattree.IsExpanded(h_servers)) {
		// Get stat values
		uint32	servtotal, servfail, servuser, servfile, servlowiduser, servtuser, servtfile;
		float	servocc;
		theApp.serverlist->GetStatus(servtotal, servfail, servuser, servfile, servlowiduser, servtuser, servtfile, servocc);
		// Set working servers value
		sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("SF_WORKING")), (LPCTSTR)CastItoIShort(servtotal - servfail));
		m_stattree.SetItemText(srv[0], sText);
		if (forceUpdate || m_stattree.IsExpanded(srv[0])) {
			// Set users on working servers value
			sText.Format(_T("%s: %s; %s: %s (%.1f%%)"), (LPCTSTR)GetResString(_T("SF_WUSER")), (LPCTSTR)CastItoIShort(servuser), (LPCTSTR)GetResString(_T("IDLOW")), (LPCTSTR)CastItoIShort(servlowiduser), servuser ? (servlowiduser * 100.0 / servuser) : 0.0);
			m_stattree.SetItemText(srv_w[0], sText);
			// Set files on working servers value
			sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("SF_WFILE")), (LPCTSTR)CastItoIShort(servfile));
			m_stattree.SetItemText(srv_w[1], sText);
			// Set server occ value
			sText.Format(GetResString(_T("SF_SRVOCC")), servocc);
			m_stattree.SetItemText(srv_w[2], sText);
		}
		// Set failed servers value
		sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("SF_FAIL")), (LPCTSTR)CastItoIShort(servfail));
		m_stattree.SetItemText(srv[1], sText);
		// Set deleted servers value
		sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("SF_DELCOUNT")), (LPCTSTR)CastItoIShort(theApp.serverlist->GetDeletedServerCount()));
		m_stattree.SetItemText(srv[2], sText);
		// Set total servers value
		sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("SF_TOTAL")), (LPCTSTR)CastItoIShort(servtotal));
		m_stattree.SetItemText(srv[3], sText);
		// Set total users value
		sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("SF_USER")), (LPCTSTR)CastItoIShort(servtuser));
		m_stattree.SetItemText(srv[4], sText);
		// Set total files value
		sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("SF_FILE")), (LPCTSTR)CastItoIShort(servtfile));
		m_stattree.SetItemText(srv[5], sText);
		// SERVERS -> RECORDS SECTION
		if (forceUpdate || m_stattree.IsExpanded(hsrv_records)) {
			// Set most working servers
			sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_SVRECWORKING")), (LPCTSTR)CastItoIShort(thePrefs.GetSrvrsMostWorkingServers()));
			m_stattree.SetItemText(srv_r[0], sText);
			// Set most users online
			sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_SVRECUSERS")), (LPCTSTR)CastItoIShort(thePrefs.GetSrvrsMostUsersOnline()));
			m_stattree.SetItemText(srv_r[1], sText);
			// Set most files avail
			sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_SVRECFILES")), (LPCTSTR)CastItoIShort(thePrefs.GetSrvrsMostFilesAvail()));
			m_stattree.SetItemText(srv_r[2], sText);
		} // - End Servers -> Records Section
	} // - END SERVERS SECTION


	// SHARED FILES SECTION
	if (forceUpdate || m_stattree.IsExpanded(h_shared)) {
		// Set Number of Shared Files
		sText.Format(GetResString(_T("SHAREDFILESCOUNT")), (int)theApp.sharedfiles->GetCount());
		// SLUGFILLER: SafeHash - extra statistics
		if (theApp.sharedfiles->GetHashingCount())
			sText.AppendFormat(GetResString(_T("HASHINGFILESCOUNT")), theApp.sharedfiles->GetHashingCount());

		// SLUGFILLER: SafeHash
		m_stattree.SetItemText(shar[0], sText);
		// Set Average File Size
		uint64 bytesLargestFile;
		// Func returns total share size and sets pointer uint64 to largest single file size
		uint64 allsize = theApp.sharedfiles->GetDatasize(bytesLargestFile);
		INT_PTR iCnt = theApp.sharedfiles->GetCount();
		sText.Format(GetResString(_T("SF_AVERAGESIZE")), (LPCTSTR)CastItoXBytes((iCnt > 0) ? allsize / iCnt : 0));
		m_stattree.SetItemText(shar[1], sText);

		// Set Largest File Size
		sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_LARGESTFILE")), (LPCTSTR)CastItoXBytes(bytesLargestFile));
		m_stattree.SetItemText(shar[2], sText);
		// Set Total Share Size
		sText.Format(GetResString(_T("SF_SIZE")), (LPCTSTR)CastItoXBytes(allsize));
		m_stattree.SetItemText(shar[3], sText);

		// SHARED FILES -> RECORDS SECTION
		if (forceUpdate || m_stattree.IsExpanded(hshar_records)) {
			// Set Most Files Shared
			sText.Format(_T("%s: %u"), (LPCTSTR)GetResString(_T("STATS_SHRECNUM")), thePrefs.GetSharedMostFilesShared());
			m_stattree.SetItemText(shar_r[0], sText);
			// Set largest avg file size
			sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_SHRECASIZE")), (LPCTSTR)CastItoXBytes(thePrefs.GetSharedLargestAvgFileSize()));
			m_stattree.SetItemText(shar_r[1], sText);
			// Set largest file size
			sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_LARGESTFILE")), (LPCTSTR)CastItoXBytes(thePrefs.GetSharedLargestFileSize()));
			m_stattree.SetItemText(shar_r[2], sText);
			// Set largest share size
			sText.Format(_T("%s: %s"), (LPCTSTR)GetResString(_T("STATS_SHRECSIZE")), (LPCTSTR)CastItoXBytes(thePrefs.GetSharedLargestShareSize()));
			m_stattree.SetItemText(shar_r[3], sText);
		} // - End Shared Files -> Records Section
	} // - END SHARED FILES SECTION

	if (forceUpdate || m_stattree.IsExpanded(h_total_downloads)) {
		uint64 ui64TotalFileSize = 0;
		uint64 ui64TotalLeftToTransfer = 0;
		uint64 ui64TotalAdditionalNeededSpace = 0;
		int iActiveFiles = theApp.downloadqueue->GetDownloadFilesStats(ui64TotalFileSize, ui64TotalLeftToTransfer, ui64TotalAdditionalNeededSpace);

		sText.Format(GetResString(_T("DWTOT_NR")), iActiveFiles);
		m_stattree.SetItemText(h_total_num_of_dls, sText);
		sText.Format(GetResString(_T("DWTOT_TSD")), (LPCTSTR)CastItoXBytes(ui64TotalFileSize));
		m_stattree.SetItemText(h_total_size_of_dls, sText);

		uint64 ui64TotalTransferred = ui64TotalFileSize - ui64TotalLeftToTransfer;
		double fPercent = ui64TotalFileSize ? (ui64TotalTransferred * 100.0) / ui64TotalFileSize : 0;
		sText.Format(GetResString(_T("DWTOT_TCS")), (LPCTSTR)CastItoXBytes(ui64TotalTransferred), fPercent);
		m_stattree.SetItemText(h_total_size_dld, sText);
		sText.Format(GetResString(_T("DWTOT_TSL")), (LPCTSTR)CastItoXBytes(ui64TotalLeftToTransfer));
		m_stattree.SetItemText(h_total_size_left_to_dl, sText);
		sText.Format(GetResString(_T("DWTOT_TSN")), (LPCTSTR)CastItoXBytes(ui64TotalAdditionalNeededSpace));
		m_stattree.SetItemText(h_total_size_needed, sText);

		uint64 ui64TotalFreeSpace = GetFreeTempSpace(-1);
		sText.Format(GetResString(_T("DWTOT_FS")), (LPCTSTR)CastItoXBytes(ui64TotalFreeSpace));
		if (ui64TotalAdditionalNeededSpace > ui64TotalFreeSpace)
			sText.AppendFormat(GetResString(_T("NEEDFREEDISKSPACE")), EMPTY, (LPCTSTR)CastItoXBytes(ui64TotalAdditionalNeededSpace - ui64TotalFreeSpace));
		m_stattree.SetItemText(h_total_size_left_on_drive, sText);
	}
	// - End Set Tree Values

#ifdef _DEBUG
	if (g_pfnPrevCrtAllocHook && IsDebugStatsVisible()) {
		_CrtMemState memState;
		_CrtMemCheckpoint(&memState);

		sText.Format(_T("%s: %Iu bytes in %Iu blocks"), _T("Free"), memState.lSizes[0], memState.lCounts[0]);
		m_stattree.SetItemText(debug1, sText);
		sText.Format(_T("%s: %Iu bytes in %Iu blocks"), _T("Normal"), memState.lSizes[1], memState.lCounts[1]);
		m_stattree.SetItemText(debug2, sText);
		sText.Format(_T("%s: %Iu bytes in %Iu blocks"), _T("CRT"), memState.lSizes[2], memState.lCounts[2]);
		m_stattree.SetItemText(debug3, sText);
		sText.Format(_T("%s: %Iu bytes in %Iu blocks"), _T("Ignore"), memState.lSizes[3], memState.lCounts[3]);
		m_stattree.SetItemText(debug4, sText);
		sText.Format(_T("%s: %Iu bytes in %Iu blocks"), _T("Client"), memState.lSizes[4], memState.lCounts[4]);
		m_stattree.SetItemText(debug5, sText);

		extern CMap<const unsigned char*, const unsigned char*, UINT, UINT> g_allocations;

		for (const CMap<const unsigned char*, const unsigned char*, HTREEITEM*, HTREEITEM*>::CPair *pair = blockFiles.PGetFirstAssoc(); pair != NULL; pair = blockFiles.PGetNextAssoc(pair))
			m_stattree.SetItemText(*pair->value, EMPTY);

		for (const CMap<const unsigned char*, const unsigned char*, UINT, UINT>::CPair *pair = g_allocations.PGetFirstAssoc(); pair != NULL; pair = g_allocations.PGetNextAssoc(pair)) {
			HTREEITEM *pTag;
			if (blockFiles.Lookup(pair->key, pTag) == 0) {
				pTag = new HTREEITEM;
				*pTag = m_stattree.InsertItem(_T("0"), debug2);
				m_stattree.SetItemData(*pTag, 1);
				blockFiles[pair->key] = pTag;
			}
			sText.Format(_T("%hs : %u blocks"), pair->key, pair->value); //count
			m_stattree.SetItemText(*pTag, sText);
		}
	}
#endif

#ifdef USE_MEM_STATS
	if (forceUpdate || m_stattree.IsExpanded(h_allocs)) {
		ULONGLONG ullTotalAllocs = 0;
		for (int i = 0; i < ALLOC_SLOTS; ++i)
			ullTotalAllocs += g_aAllocStats[i];
		for (int i = 0; i < ALLOC_SLOTS; ++i) {
			unsigned uStart, uEnd;
			if (i <= 1)
				uStart = uEnd = i;
			else {
				uStart = 1 << (i - 1);
				uEnd = (i == ALLOC_SLOTS - 1) ? UINT_MAX : (uStart << 1) - 1;
			}
			sText.Format(_T("Block size %08X-%08X: %s (%1.1f%%)"), uStart, uEnd, (LPCTSTR)CastItoIShort(g_aAllocStats[i], false, 2), ullTotalAllocs != 0 ? g_aAllocStats[i] * 100.0 / ullTotalAllocs : 0.0);
			m_stattree.SetItemText(h_allocSizes[i], sText);
		}
	}
#endif

	m_stattree.SetRedraw(true);

} // ShowStatistics(bool forceRedraw = false){}

void CStatisticsDlg::UpdateConnectionsGraph()
{
	// This updates the Y-Axis scale of the Connections Statistics graph...
	// And it updates the trend ratio for the active connections trend.

	m_Statistics.SetRanges(0, thePrefs.GetStatsMax());
	m_Statistics.SetTrendRatio(0, thePrefs.GetStatsConnectionsGraphRatio());
}

void CStatisticsDlg::OnShowWindow(BOOL /*bShow*/, UINT /*nStatus*/)
{
}

void CStatisticsDlg::OnSize(UINT nType, int cx, int cy)
{
	CResizableDialog::OnSize(nType, cx, cy);
	if (cx > 0 && cy > 0 && (cx != m_oldcx || cy != m_oldcy)) {
		m_oldcx = cx;
		m_oldcy = cy;
		ShowInterval();
	}
}

void CStatisticsDlg::ShowInterval()
{
	if (theApp.IsClosing())
		return;

	// Check if OScope already initialized
	if (m_DownloadOMeter.GetSafeHwnd() != NULL && m_UploadOMeter.GetSafeHwnd() != NULL) {
		// Retrieve the size (in pixel) of the OScope
		CRect plotRect;
		m_UploadOMeter.GetPlotRect(plotRect);

		// Dynamic update of time scale [Maella]
		int shownSecs = plotRect.Width() * thePrefs.GetTrafficOMeterInterval();

		m_Statistics.m_nXPartial = m_DownloadOMeter.m_nXPartial = m_UploadOMeter.m_nXPartial = shownSecs % HR2S(1);
		m_Statistics.m_nXGrids = m_DownloadOMeter.m_nXGrids = m_UploadOMeter.m_nXGrids = shownSecs / HR2S(1);

		if (shownSecs <= 0) {
			m_DownloadOMeter.SetXUnits(GetResString(_T("STOPPED")));
			m_UploadOMeter.SetXUnits(GetResString(_T("STOPPED")));
			m_Statistics.SetXUnits(GetResString(_T("STOPPED")));
		} else {
			const CString &sUnits(CastSecondsToHM(shownSecs));
			m_UploadOMeter.SetXUnits(sUnits);
			m_DownloadOMeter.SetXUnits(sUnits);
			m_Statistics.SetXUnits(sUnits);
		}
		UpdateData(FALSE);
	}
}

void CStatisticsDlg::SetARange(bool SetDownload, uint32 maxValue)
{
	if (SetDownload)
		m_DownloadOMeter.SetRanges(0, maxValue);
	else
		m_UploadOMeter.SetRanges(0, maxValue);
}

// Various changes in Localize() and a new button event...
void CStatisticsDlg::Localize()
{
	RepaintMeters();

	CString sReset;
	sReset.Format(GetResString(_T("STATS_LASTRESETSTATIC")), (LPCTSTR)thePrefs.GetStatsLastResetStr(false));
	SetDlgItemText(IDC_STATIC_LASTRESET, sReset);
}
// End Localize

// Menu Button: Displays the menu of stat tree commands.
void CStatisticsDlg::OnMenuButtonClicked()
{
	CRect rectBn;
	GetDlgItem(IDC_BNMENU)->GetWindowRect(rectBn);
	m_stattree.DoMenu(rectBn.BottomRight());
}

static LPCTSTR GetPopulationStringId(int branchIndex)
{
	if (branchIndex == ClientPopulationAll)
		return _T("ALL");
	else if (branchIndex == ClientPopulationActive)
		return _T("ACTIVE");
	else
		return _T("ARCHIVED");
}

void CStatisticsDlg::InitializeClientBranch(int branchIndex)
{
	HTREEITEM branchRoot = m_clientsPopulation[branchIndex];
	if (branchRoot == NULL)
		return;

	m_clientSecureIdent[branchIndex] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), branchRoot);
	m_clientLowId[branchIndex] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), branchRoot);
	m_clientProblematic[branchIndex] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), branchRoot);
	m_stattree.SetItemState(branchRoot, TVIS_BOLD, TVIS_BOLD);

	m_clientSoftwareRoot[branchIndex] = m_stattree.InsertItem(GetResString(_T("CD_CSOFT")), branchRoot);
	for (int i = 0; i < 8; ++i)
		m_clientSoftware[branchIndex][i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), m_clientSoftwareRoot[branchIndex]);
	for (int cat = 0; cat < MAX_CLIENTS_WITH_SUB_VERSION; ++cat) {
		m_clientSoftwareOther[branchIndex][cat] = NULL;
		m_clientSoftwareLastCount[branchIndex][cat] = 0;
		for (int ver = 0; ver < MAX_SUB_CLIENT_VERSIONS; ++ver)
			m_clientSoftwareVersions[branchIndex][cat][ver] = NULL;
	}
	m_clientAiRoot[branchIndex] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), m_clientSoftware[branchIndex][0]);
	m_clientAiOther[branchIndex] = NULL;
	m_clientAiLastCount[branchIndex] = 0;
	for (int ver = 0; ver < MAX_SUB_CLIENT_VERSIONS; ++ver)
		m_clientAiVersions[branchIndex][ver] = NULL;

	m_clientNetworkRoot[branchIndex] = m_stattree.InsertItem(GetResString(_T("NETWORK")), branchRoot);
	for (int i = 0; i < 4; ++i)
		m_clientNetwork[branchIndex][i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), m_clientNetworkRoot[branchIndex]);

	m_clientPortRoot[branchIndex] = m_stattree.InsertItem(GetResString(_T("PORT")), branchRoot);
	for (int i = 0; i < 2; ++i)
		m_clientPort[branchIndex][i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), m_clientPortRoot[branchIndex]);

	CString firewalledLabel;
	firewalledLabel.Format(_T("%s (%s)"), (LPCTSTR)GetResString(_T("FIREWALLED")), (LPCTSTR)GetResString(_T("KADEMLIA")));
	m_clientFirewalledRoot[branchIndex] = m_stattree.InsertItem(firewalledLabel, branchRoot);
	for (int i = 0; i < 2; ++i)
		m_clientFirewalled[branchIndex][i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), m_clientFirewalledRoot[branchIndex]);

	m_clientModsRoot[branchIndex] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), branchRoot);
	m_clientCountriesRoot[branchIndex] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), branchRoot);

	// Add Banned, Bad, Filtered counters (Filtered shown only for All and at same level)
	m_clientBanned[branchIndex] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), branchRoot);
	m_clientBad[branchIndex] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), branchRoot);
	// Filtered is only shown for All branch (index 0) since it's not tracked per-population
	if (branchIndex == 0)
		m_clientFiltered[branchIndex] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), branchRoot);
}

void CStatisticsDlg::UpdateClientAiBreakdown(int branchIndex, uint32 aiClientTotal, const CClientVersionNameMap& versionMap, bool forceUpdate)
{
	HTREEITEM aiRoot = m_clientAiRoot[branchIndex];
	if (aiRoot == NULL)
		return;

	if (aiClientTotal == 0) {
		if (m_clientAiOther[branchIndex] != NULL) {
			m_stattree.DeleteItem(m_clientAiOther[branchIndex]);
			m_clientAiOther[branchIndex] = NULL;
		}
		for (uint32 i = 0; i < m_clientAiLastCount[branchIndex]; ++i) {
			if (m_clientAiVersions[branchIndex][i] != NULL) {
				m_stattree.DeleteItem(m_clientAiVersions[branchIndex][i]);
				m_clientAiVersions[branchIndex][i] = NULL;
			}
		}
		m_clientAiLastCount[branchIndex] = 0;
		return;
	}

	if (!(forceUpdate || m_stattree.IsExpanded(aiRoot) || m_clientAiLastCount[branchIndex] == 0))
		return;

	std::vector<std::pair<CString, uint32>> entries;
	for (POSITION pos = versionMap.GetStartPosition(); pos != NULL;) {
		CString versionLabel;
		uint32 count = 0;
		versionMap.GetNextAssoc(pos, versionLabel, count);
		entries.emplace_back(versionLabel, count);
	}
	std::sort(entries.begin(), entries.end(), [](const std::pair<CString, uint32>& lhs, const std::pair<CString, uint32>& rhs) {
		if (lhs.second != rhs.second)
			return lhs.second > rhs.second;
		return lhs.first.CompareNoCase(rhs.first) > 0;
	});

	uint32 verCountAI = 0;
	uint32 totalOtherAI = 0;
	size_t limit = entries.size();
	if (limit > static_cast<size_t>(MAX_SUB_CLIENT_VERSIONS))
		limit = static_cast<size_t>(MAX_SUB_CLIENT_VERSIONS);
	for (size_t idx = 0; idx < limit; ++idx) {
		const CString& label = entries[idx].first;
		uint32 count = entries[idx].second;
		double share = aiClientTotal > 0 ? static_cast<double>(count) * 100.0 / aiClientTotal : 0.0;
		CString sText;
		sText.Format(_T("%s: %u (%1.1f%%)"), (LPCTSTR)label, count, share);

		if (idx >= MAX_SUB_CLIENT_VERSIONS / 2)
			totalOtherAI += count;

			if (idx >= static_cast<size_t>(m_clientAiLastCount[branchIndex])) {
			if (idx == MAX_SUB_CLIENT_VERSIONS / 2) {
				if (m_clientAiOther[branchIndex] == NULL)
					m_clientAiOther[branchIndex] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), aiRoot);
			}
				if (idx >= MAX_SUB_CLIENT_VERSIONS / 2)
					m_clientAiVersions[branchIndex][idx] = m_stattree.InsertItem(sText, m_clientAiOther[branchIndex]);
				else
					m_clientAiVersions[branchIndex][idx] = m_stattree.InsertItem(sText, aiRoot);
		} else {
			m_stattree.SetItemText(m_clientAiVersions[branchIndex][idx], sText);
		}

		++verCountAI;
	}

	if (verCountAI > MAX_SUB_CLIENT_VERSIONS / 2) {
		double otherShare = aiClientTotal > 0 ? static_cast<double>(totalOtherAI) * 100.0 / aiClientTotal : 0.0;
		CString otherText;
		otherText.Format(_T("%s: %u (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_MINORCLIENTS")), totalOtherAI, otherShare);
		m_stattree.SetItemText(m_clientAiOther[branchIndex], otherText);
	} else if (m_clientAiOther[branchIndex] != NULL) {
		m_stattree.DeleteItem(m_clientAiOther[branchIndex]);
		m_clientAiOther[branchIndex] = NULL;
	}

	if (verCountAI < m_clientAiLastCount[branchIndex]) {
		for (uint32 idx = verCountAI; idx < m_clientAiLastCount[branchIndex]; ++idx) {
			if (m_clientAiVersions[branchIndex][idx] != NULL) {
				m_stattree.DeleteItem(m_clientAiVersions[branchIndex][idx]);
				m_clientAiVersions[branchIndex][idx] = NULL;
			}
		}
		if (verCountAI <= MAX_SUB_CLIENT_VERSIONS / 2 && m_clientAiOther[branchIndex] != NULL) {
			m_stattree.DeleteItem(m_clientAiOther[branchIndex]);
			m_clientAiOther[branchIndex] = NULL;
		}
	}

	m_clientAiLastCount[branchIndex] = verCountAI;
}

void CStatisticsDlg::UpdateClientSoftwareBreakdown(int branchIndex, int softwareIndex, const CClientVersionMap& versionMap, uint32 baseCount, bool forceUpdate)
{
	HTREEITEM softwareItem = m_clientSoftware[branchIndex][softwareIndex];
	if (softwareItem == NULL)
		return;

	if (baseCount == 0) {
		for (uint32 i = 0; i < m_clientSoftwareLastCount[branchIndex][softwareIndex]; ++i) {
			if (m_clientSoftwareVersions[branchIndex][softwareIndex][i] != NULL) {
				m_stattree.DeleteItem(m_clientSoftwareVersions[branchIndex][softwareIndex][i]);
				m_clientSoftwareVersions[branchIndex][softwareIndex][i] = NULL;
			}
			if (m_clientSoftwareLastCount[branchIndex][softwareIndex] + (MAX_SUB_CLIENT_VERSIONS * softwareIndex - 1) - i == MAX_SUB_CLIENT_VERSIONS / 2 && m_clientSoftwareOther[branchIndex][softwareIndex] != NULL) {
				m_stattree.DeleteItem(m_clientSoftwareOther[branchIndex][softwareIndex]);
				m_clientSoftwareOther[branchIndex][softwareIndex] = NULL;
			}
		}
		m_clientSoftwareLastCount[branchIndex][softwareIndex] = 0;
		return;
	}

	if (!(forceUpdate || m_stattree.IsExpanded(softwareItem) || m_clientSoftwareLastCount[branchIndex][softwareIndex] == 0))
		return;

	std::vector<std::pair<uint32, uint32>> versions;
	for (POSITION pos = versionMap.GetStartPosition(); pos != NULL;) {
		uint32 version = 0;
		uint32 count = 0;
		versionMap.GetNextAssoc(pos, version, count);
		versions.emplace_back(version, count);
	}
	std::sort(versions.begin(), versions.end(), [](const std::pair<uint32, uint32>& lhs, const std::pair<uint32, uint32>& rhs) {
		if (lhs.second != rhs.second)
			return lhs.second > rhs.second;
		return lhs.first > rhs.first;
	});

	uint32 verCount = 0;
	uint32 totalOther = 0;
	size_t limitVersions = versions.size();
	if (limitVersions > static_cast<size_t>(MAX_SUB_CLIENT_VERSIONS))
		limitVersions = static_cast<size_t>(MAX_SUB_CLIENT_VERSIONS);
	for (size_t idx = 0; idx < limitVersions; ++idx) {
		uint32 version = versions[idx].first;
		uint32 count = versions[idx].second;
		double topPer = baseCount > 0 ? static_cast<double>(count) * 100.0 / baseCount : 0.0;
		UINT verMaj = version / (100 * 10 * 100);
		UINT verMin = (version - (verMaj * 100 * 10 * 100)) / (100 * 10);
		UINT verUp = (version - (verMaj * 100 * 10 * 100) - (verMin * 100 * 10)) / 100;
		CString sText;
		if (softwareIndex == 0 && (version >= MAKE_CLIENT_VERSION(0, 40, 0) || verUp != 0)) {
			if (verUp <= _T('z') - _T('a'))
				sText.Format(_T("v%u.%u%c: %u (%1.1f%%)"), verMaj, verMin, _T('a') + verUp, count, topPer);
			else
				sText.Format(_T("v%u.%u.%u: %u (%1.1f%%)"), verMaj, verMin, verUp, count, topPer);
		} else if (softwareIndex != 0 && (version >= MAKE_CLIENT_VERSION(0, 40, 0) || verUp != 0))
			sText.Format(_T("v%u.%u.%u: %u (%1.1f%%)"), verMaj, verMin, verUp, count, topPer);
		else
			sText.Format(_T("v%u.%u: %u (%1.1f%%)"), verMaj, verMin, count, topPer);

		if (idx >= MAX_SUB_CLIENT_VERSIONS / 2)
			totalOther += count;

		if (idx >= static_cast<size_t>(m_clientSoftwareLastCount[branchIndex][softwareIndex])) {
			if (idx == MAX_SUB_CLIENT_VERSIONS / 2)
				m_clientSoftwareOther[branchIndex][softwareIndex] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), softwareItem);
			if (idx >= MAX_SUB_CLIENT_VERSIONS / 2)
				m_clientSoftwareVersions[branchIndex][softwareIndex][idx] = m_stattree.InsertItem(sText, m_clientSoftwareOther[branchIndex][softwareIndex]);
			else
				m_clientSoftwareVersions[branchIndex][softwareIndex][idx] = m_stattree.InsertItem(sText, softwareItem);
		} else
			m_stattree.SetItemText(m_clientSoftwareVersions[branchIndex][softwareIndex][idx], sText);

		++verCount;
	}

	if (verCount > MAX_SUB_CLIENT_VERSIONS / 2) {
		double otherPer = baseCount > 0 ? static_cast<double>(totalOther) * 100.0 / baseCount : 0.0;
		CString otherText;
		otherText.Format(_T("%s: %u (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_MINORCLIENTS")), totalOther, otherPer);
		m_stattree.SetItemText(m_clientSoftwareOther[branchIndex][softwareIndex], otherText);
	} else if (m_clientSoftwareOther[branchIndex][softwareIndex] != NULL) {
		m_stattree.DeleteItem(m_clientSoftwareOther[branchIndex][softwareIndex]);
		m_clientSoftwareOther[branchIndex][softwareIndex] = NULL;
	}

	if (verCount < m_clientSoftwareLastCount[branchIndex][softwareIndex]) {
		for (uint32 idx = verCount; idx < m_clientSoftwareLastCount[branchIndex][softwareIndex]; ++idx) {
			if (m_clientSoftwareVersions[branchIndex][softwareIndex][idx] != NULL) {
				m_stattree.DeleteItem(m_clientSoftwareVersions[branchIndex][softwareIndex][idx]);
				m_clientSoftwareVersions[branchIndex][softwareIndex][idx] = NULL;
			}
		}
		if (verCount <= MAX_SUB_CLIENT_VERSIONS / 2 && m_clientSoftwareOther[branchIndex][softwareIndex] != NULL) {
			m_stattree.DeleteItem(m_clientSoftwareOther[branchIndex][softwareIndex]);
			m_clientSoftwareOther[branchIndex][softwareIndex] = NULL;
		}
	}

	m_clientSoftwareLastCount[branchIndex][softwareIndex] = verCount;
}

void CStatisticsDlg::UpdateClientMods(int branchIndex, const CClientStatsSnapshot& snapshot, bool forceUpdate)
{
	HTREEITEM modsRoot = m_clientModsRoot[branchIndex];
	if (modsRoot == NULL)
		return;

	HTREEITEM child = m_stattree.ItemHasChildren(modsRoot) ? m_stattree.GetChildItem(modsRoot) : NULL;
	CString sText;

	if (snapshot.totalMODs == 0) {
		sText.Format(_T("Mods: 0"));
		m_stattree.SetItemText(modsRoot, sText);
		m_stattree.DeleteRestChildItems(child);
		return;
	}

	if (forceUpdate || m_stattree.IsExpanded(modsRoot)) {
		std::vector<std::pair<CString, uint32>> mods;
		for (POSITION pos = snapshot.modCounts.GetStartPosition(); pos != NULL;) {
			CString modName;
			uint32 count = 0;
			snapshot.modCounts.GetNextAssoc(pos, modName, count);
			mods.emplace_back(modName, count);
		}
		std::sort(mods.begin(), mods.end(), [](const std::pair<CString, uint32>& lhs, const std::pair<CString, uint32>& rhs) {
			if (lhs.second != rhs.second)
				return lhs.second > rhs.second;
			return lhs.first.CompareNoCase(rhs.first) < 0;
		});
		for (size_t idx = 0; idx < mods.size(); ++idx) {
			double share = snapshot.totalMODs > 0 ? static_cast<double>(mods[idx].second) * 100.0 / snapshot.totalMODs : 0.0;
			CString entry;
			entry.Format(_T("%s: %u (%.1f%%)"), (LPCTSTR)mods[idx].first, mods[idx].second, share);
			if (child != NULL) {
				m_stattree.SetItemText(child, entry);
				child = m_stattree.GetNextItem(child, TVGN_NEXT);
			} else
				m_stattree.InsertItem(entry, modsRoot);
		}
	} else {
		if (child != NULL)
			child = m_stattree.GetNextItem(child, TVGN_NEXT);
		else
			m_stattree.InsertItem(EMPTY, modsRoot);
	}

	m_stattree.DeleteRestChildItems(child);
	sText.Format(_T("Mods: %u (%.1f%%)"), snapshot.totalMODs, snapshot.totalClients > 0 ? static_cast<double>(snapshot.totalMODs) * 100.0 / snapshot.totalClients : 0.0);
	m_stattree.SetItemText(modsRoot, sText);
}

void CStatisticsDlg::UpdateClientCountries(int branchIndex, const CClientStatsSnapshot& snapshot, bool forceUpdate)
{
	HTREEITEM countriesRoot = m_clientCountriesRoot[branchIndex];
	if (countriesRoot == NULL)
		return;

	HTREEITEM child = m_stattree.ItemHasChildren(countriesRoot) ? m_stattree.GetChildItem(countriesRoot) : NULL;
	CString sText;

	if (!theApp.geolite2->IsGeoLite2Active()) {
		sText.Format(_T("Countries: <%s>"), GetResString(_T("DISABLED")));
		m_stattree.SetItemText(countriesRoot, sText);
		m_stattree.DeleteRestChildItems(child);
		return;
	}

	if (snapshot.totalClients == 0) {
		sText.Format(_T("Countries: 0"));
		m_stattree.SetItemText(countriesRoot, sText);
		m_stattree.DeleteRestChildItems(child);
		return;
	}

	std::vector<std::pair<CString, uint32>> countries;
	for (POSITION pos = snapshot.countries.GetStartPosition(); pos != NULL;) {
		CString name;
		uint32 count = 0;
		snapshot.countries.GetNextAssoc(pos, name, count);
		countries.emplace_back(name, count);
	}
	std::sort(countries.begin(), countries.end(), [](const std::pair<CString, uint32>& lhs, const std::pair<CString, uint32>& rhs) {
		if (lhs.second != rhs.second)
			return lhs.second > rhs.second;
		return lhs.first.CompareNoCase(rhs.first) < 0;
	});

	if (forceUpdate || m_stattree.IsExpanded(countriesRoot)) {
		for (size_t idx = 0; idx < countries.size(); ++idx) {
			double share = snapshot.totalClients > 0 ? static_cast<double>(countries[idx].second) * 100.0 / snapshot.totalClients : 0.0;
			CString entry;
			entry.Format(_T("%s: %u (%.1f%%)"), (LPCTSTR)countries[idx].first, countries[idx].second, share);
			if (child != NULL) {
				m_stattree.SetItemText(child, entry);
				child = m_stattree.GetNextItem(child, TVGN_NEXT);
			} else
				m_stattree.InsertItem(entry, countriesRoot);
		}
	} else {
		if (child != NULL)
			child = m_stattree.GetNextItem(child, TVGN_NEXT);
		else
			m_stattree.InsertItem(EMPTY, countriesRoot);
	}

	m_stattree.DeleteRestChildItems(child);
	sText.Format(_T("Countries: %u"), static_cast<uint32>(countries.size()));
	m_stattree.SetItemText(countriesRoot, sText);
}

void CStatisticsDlg::UpdateClientBranch(int branchIndex, const CClientStatsSnapshot& snapshot, bool forceUpdate)
{
	HTREEITEM branchRoot = m_clientsPopulation[branchIndex];
	if (branchRoot == NULL)
		return;

	CString sText;
	if (branchRoot != h_clients && branchRoot != NULL) {
		sText.Format(_T("%s (%u)"), (LPCTSTR)GetResString(GetPopulationStringId(branchIndex)), snapshot.totalClients);
		m_stattree.SetItemText(branchRoot, sText);
	}

	uint32 secureTotal = snapshot.stats[12] + snapshot.stats[13];
	double secureIdentPerc = secureTotal > 0 ? (100.0 * snapshot.stats[12]) / secureTotal : 0.0;
	double secureNeededPerc = secureTotal > 0 ? (100.0 * snapshot.stats[13]) / secureTotal : 0.0;
	sText.Format(_T("%s: %i (%.1f%%) : %i (%.1f%%)"), (LPCTSTR)GetResString(_T("STATS_SECUREIDENT")), snapshot.stats[12], secureIdentPerc, snapshot.stats[13], secureNeededPerc);
	m_stattree.SetItemText(m_clientSecureIdent[branchIndex], sText);

	double perc = snapshot.totalClients > 0 ? 100.0 / snapshot.totalClients : 0.0;
	sText.Format(_T("%s: %i (%.1f%%)"), (LPCTSTR)GetResString(_T("IDLOW")), snapshot.stats[14], perc * snapshot.stats[14]);
	m_stattree.SetItemText(m_clientLowId[branchIndex], sText);

	sText.Format(_T("%s: %i (%.1f%%)"), (LPCTSTR)GetResString(_T("STATS_PROBLEMATIC")), snapshot.stats[6], perc * snapshot.stats[6]);
	m_stattree.SetItemText(m_clientProblematic[branchIndex], sText);

	if (forceUpdate || m_stattree.IsExpanded(m_clientSoftwareRoot[branchIndex])) {
		sText.Format(_T("eMule: %i (%1.1f%%)"), snapshot.stats[2], perc * snapshot.stats[2]);
		m_stattree.SetItemText(m_clientSoftware[branchIndex][0], sText);
		sText.Format(_T("eD Hybrid: %i (%1.1f%%)"), snapshot.stats[4], perc * snapshot.stats[4]);
		m_stattree.SetItemText(m_clientSoftware[branchIndex][1], sText);
		sText.Format(_T("eDonkey: %i (%1.1f%%)"), snapshot.stats[1], perc * snapshot.stats[1]);
		m_stattree.SetItemText(m_clientSoftware[branchIndex][2], sText);
		sText.Format(_T("aMule: %i (%1.1f%%)"), snapshot.stats[10], perc * snapshot.stats[10]);
		m_stattree.SetItemText(m_clientSoftware[branchIndex][3], sText);
		sText.Format(_T("MLdonkey: %i (%1.1f%%)"), snapshot.stats[3], perc * snapshot.stats[3]);
		m_stattree.SetItemText(m_clientSoftware[branchIndex][4], sText);
		sText.Format(_T("Shareaza: %i (%1.1f%%)"), snapshot.stats[11], perc * snapshot.stats[11]);
		m_stattree.SetItemText(m_clientSoftware[branchIndex][5], sText);
		sText.Format(_T("eM Compat: %i (%1.1f%%)"), snapshot.stats[5], perc * snapshot.stats[5]);
		m_stattree.SetItemText(m_clientSoftware[branchIndex][6], sText);
		sText.Format(GetResString(_T("STATS_UNKNOWNCLIENT")), snapshot.stats[0]);
		sText.AppendFormat(_T(" (%1.1f%%)"), perc * snapshot.stats[0]);
		m_stattree.SetItemText(m_clientSoftware[branchIndex][7], sText);

		CString aiName = MOD_NAME;
		double aiPerc = snapshot.stats[2] > 0 ? 100.0 * static_cast<double>(snapshot.aiClientTotal) / snapshot.stats[2] : 0.0;
		sText.Format(_T("%s: %u (%1.1f%%)"), (LPCTSTR)aiName, snapshot.aiClientTotal, aiPerc);
		m_stattree.SetItemText(m_clientAiRoot[branchIndex], sText);
		UpdateClientAiBreakdown(branchIndex, snapshot.aiClientTotal, snapshot.aiVersionMap, forceUpdate);

		UpdateClientSoftwareBreakdown(branchIndex, 0, snapshot.clientVersionEMule, snapshot.stats[2], forceUpdate);
		UpdateClientSoftwareBreakdown(branchIndex, 1, snapshot.clientVersionEDonkeyHybrid, snapshot.stats[4], forceUpdate);
		UpdateClientSoftwareBreakdown(branchIndex, 2, snapshot.clientVersionEDonkey, snapshot.stats[1], forceUpdate);
		UpdateClientSoftwareBreakdown(branchIndex, 3, snapshot.clientVersionAMule, snapshot.stats[10], forceUpdate);
	}

	if (forceUpdate || m_stattree.IsExpanded(m_clientNetworkRoot[branchIndex])) {
		sText.Format(_T("eD2K: %i (%.1f%%)"), snapshot.stats[15], perc * snapshot.stats[15]);
		m_stattree.SetItemText(m_clientNetwork[branchIndex][0], sText);
		sText.Format(_T("Kad: %i (%.1f%%)"), snapshot.stats[16], perc * snapshot.stats[16]);
		m_stattree.SetItemText(m_clientNetwork[branchIndex][1], sText);
		sText.Format(_T("eD2K/Kad: %i (%.1f%%)"), snapshot.stats[17], perc * snapshot.stats[17]);
		m_stattree.SetItemText(m_clientNetwork[branchIndex][2], sText);
		sText.Format(_T("%s: %i (%.1f%%)"), (LPCTSTR)GetResString(_T("UNKNOWN")), snapshot.stats[18], perc * snapshot.stats[18]);
		m_stattree.SetItemText(m_clientNetwork[branchIndex][3], sText);
	}

	if (forceUpdate || m_stattree.IsExpanded(m_clientPortRoot[branchIndex])) {
		uint32 totalPort = snapshot.stats[8] + snapshot.stats[9];
		double defPerc = totalPort > 0 ? static_cast<double>(snapshot.stats[8]) * 100.0 / totalPort : 0.0;
		double otherPerc = totalPort > 0 ? static_cast<double>(snapshot.stats[9]) * 100.0 / totalPort : 0.0;
		sText.Format(_T("%s: %i (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_PRTDEF")), snapshot.stats[8], defPerc);
		m_stattree.SetItemText(m_clientPort[branchIndex][0], sText);
		sText.Format(_T("%s: %i (%1.1f%%)"), (LPCTSTR)GetResString(_T("STATS_PRTOTHER")), snapshot.stats[9], otherPerc);
		m_stattree.SetItemText(m_clientPort[branchIndex][1], sText);
	}

	if (forceUpdate || m_stattree.IsExpanded(m_clientFirewalledRoot[branchIndex])) {
		if (!Kademlia::CKademlia::IsRunning() || Kademlia::CUDPFirewallTester::IsFirewalledUDP(true)) {
			sText.Format(_T("UDP: %s"), (LPCTSTR)GetResString(_T("KAD_UNKNOWN")));
			m_stattree.SetItemText(m_clientFirewalled[branchIndex][0], sText);
			sText.Format(_T("TCP: %s"), (LPCTSTR)GetResString(_T("KAD_UNKNOWN")));
			m_stattree.SetItemText(m_clientFirewalled[branchIndex][1], sText);
		} else {
			double udpRatio = Kademlia::CKademlia::GetPrefs()->StatsGetFirewalledRatio(true);
			double tcpRatio = Kademlia::CKademlia::GetPrefs()->StatsGetFirewalledRatio(false);
			sText.Format(_T("UDP: %1.1f%%"), udpRatio > 0 ? udpRatio * 100 : 0.0);
			m_stattree.SetItemText(m_clientFirewalled[branchIndex][0], udpRatio > 0 ? sText : CString(_T("UDP: ")) + GetResString(_T("FSTAT_WAITING")));
			sText.Format(_T("TCP: %1.1f%%"), tcpRatio > 0 ? tcpRatio * 100 : 0.0);
			m_stattree.SetItemText(m_clientFirewalled[branchIndex][1], tcpRatio > 0 ? sText : CString(_T("TCP: ")) + GetResString(_T("FSTAT_WAITING")));
		}
	}

	UpdateClientMods(branchIndex, snapshot, forceUpdate);
	UpdateClientCountries(branchIndex, snapshot, forceUpdate);

	// Update Banned, Bad, and Filtered counts for this branch from snapshot
	sText.Format(_T("%s: %i"), (LPCTSTR)GetResString(_T("BANNED")), snapshot.bannedCount);
	m_stattree.SetItemText(m_clientBanned[branchIndex], sText);
	sText.Format(GetResString(_T("STATS_BAD")), snapshot.badCount);
	m_stattree.SetItemText(m_clientBad[branchIndex], sText);
	// Filtered is only shown for All branch (index 0) since it's not tracked per-population
	if (branchIndex == 0) {
		sText.Format(GetResString(_T("STATS_FILTEREDCLIENTS")), snapshot.filteredCount);
		m_stattree.SetItemText(m_clientFiltered[branchIndex], sText);
	}
}

void CStatisticsDlg::CreateMyTree()
{
	m_stattree.DeleteAllItems();

	// Setup Tree
	h_transfer = m_stattree.InsertItem(GetResString(_T("FSTAT_TRANSFER")), 1, 1);				// Transfers Section
	CString sItem;
	sItem.Format(_T("%s %s"), (LPCTSTR)GetResString(_T("STATS_SRATIO")), (LPCTSTR)GetResString(_T("FSTAT_WAITING"))); // Make It Pretty
	trans[0] = m_stattree.InsertItem(sItem, h_transfer);									// Session Ratio

	sItem.Format(_T("%s %s"), (LPCTSTR)GetResString(_T("STATS_FRATIO")), (LPCTSTR)GetResString(_T("FSTAT_WAITING"))); // Make It Pretty
	trans[1] = m_stattree.InsertItem(sItem, h_transfer);									// Friend Session Ratio

	sItem.Format(_T("%s %s"), (LPCTSTR)GetResString(_T("STATS_CRATIO")), (LPCTSTR)GetResString(_T("FSTAT_WAITING"))); // Make It Pretty
	trans[2] = m_stattree.InsertItem(sItem, h_transfer);									// Cumulative Ratio

	h_upload = m_stattree.InsertItem(GetResString(_T("TW_UPLOADS")), 6, 6, h_transfer);		// Uploads Section

	h_up_session = m_stattree.InsertItem(GetResString(_T("STATS_SESSION")), 8, 8, h_upload);	// Session Section (Uploads)
	for (unsigned i = 0; i < 6; ++i)
		up_S[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), h_up_session);		//MORPH - Added by Yun.SF3, ZZ Upload System
	hup_scb = m_stattree.InsertItem(GetResString(_T("CLIENTS")), up_S[0]);					// Clients Section
	for (unsigned i = 0; i < _countof(up_scb); ++i)
		up_scb[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hup_scb);
	hup_spb = m_stattree.InsertItem(GetResString(_T("PORT")), up_S[0]);						// Ports Section
	for (unsigned i = 0; i < _countof(up_spb); ++i)
		up_spb[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hup_spb);
	hup_ssb = m_stattree.InsertItem(GetResString(_T("STATS_DATASOURCE")), up_S[0]);			// Data Source Section
	for (unsigned i = 0; i < 2; ++i)
		up_ssb[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hup_ssb);
	for (unsigned i = 0; i < 4; ++i)
		up_ssessions[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), up_S[5]);	//MORPH - Added by Yun.SF3, ZZ Upload System
	hup_soh = m_stattree.InsertItem(GetResString(_T("STATS_OVRHD")), h_up_session);			// Upload Overhead (Session)
	for (unsigned i = 0; i < _countof(up_soh); ++i)
		up_soh[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hup_soh);
	h_up_total = m_stattree.InsertItem(GetResString(_T("STATS_CUMULATIVE")), 9, 9, h_upload);	// Cumulative Section (Uploads)
	up_T[0] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), h_up_total);			// Uploaded Data (Total)
	hup_tcb = m_stattree.InsertItem(GetResString(_T("CLIENTS")), up_T[0]);					// Clients Section
	for (unsigned i = 0; i < _countof(up_tcb); ++i)
		up_tcb[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hup_tcb);
	hup_tpb = m_stattree.InsertItem(GetResString(_T("PORT")), up_T[0]);						// Ports Section
	for (unsigned i = 0; i < _countof(up_tpb); ++i)
		up_tpb[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hup_tpb);
	hup_tsb = m_stattree.InsertItem(GetResString(_T("STATS_DATASOURCE")), up_T[0]);			// Data Source Section
	for (unsigned i = 0; i < 2; ++i)
		up_tsb[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hup_tsb);
	up_T[1] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), h_up_total);			// Upload Sessions (Total)
	for (unsigned i = 0; i < 4; ++i)
		up_tsessions[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), up_T[1]);
	hup_toh = m_stattree.InsertItem(GetResString(_T("STATS_OVRHD")), h_up_total);				// Upload Overhead (Total)
	for (unsigned i = 0; i < _countof(up_toh); ++i)
		up_toh[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hup_toh);
	h_download = m_stattree.InsertItem(GetResString(_T("TW_DOWNLOADS")), 7, 7, h_transfer);	// Downloads Section
	h_down_session = m_stattree.InsertItem(GetResString(_T("STATS_SESSION")), 8, 8, h_download); // Session Section (Downloads)
	for (unsigned i = 0; i < 8; ++i)
		down_S[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), h_down_session);
	hdown_scb = m_stattree.InsertItem(GetResString(_T("CLIENTS")), down_S[0]);				// Clients Section
	for (unsigned i = 0; i < _countof(down_scb); ++i)
		down_scb[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hdown_scb);
	hdown_spb = m_stattree.InsertItem(GetResString(_T("PORT")), down_S[0]);					// Ports Section
	for (unsigned i = 0; i < _countof(down_spb); ++i)
		down_spb[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hdown_spb);
	for (unsigned i = 0; i < _countof(down_sources); ++i)
		down_sources[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), down_S[3]);
	for (unsigned i = 0; i < 4; ++i)
		down_ssessions[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), down_S[4]);
	hdown_soh = m_stattree.InsertItem(GetResString(_T("STATS_OVRHD")), h_down_session);		// Downline Overhead (Session)
	for (unsigned i = 0; i < _countof(down_soh); ++i)
		down_soh[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hdown_soh);
	h_down_total = m_stattree.InsertItem(GetResString(_T("STATS_CUMULATIVE")), 9, 9, h_download);// Cumulative Section (Downloads)
	for (unsigned i = 0; i < 6; ++i)
		down_T[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), h_down_total);
	hdown_tcb = m_stattree.InsertItem(GetResString(_T("CLIENTS")), down_T[0]);				// Clients Section
	for (unsigned i = 0; i < _countof(down_tcb); ++i)
		down_tcb[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hdown_tcb);
	hdown_tpb = m_stattree.InsertItem(GetResString(_T("PORT")), down_T[0]);					// Ports Section
	for (unsigned i = 0; i < _countof(down_tpb); ++i)
		down_tpb[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hdown_tpb);
	for (unsigned i = 0; i < 4; ++i)
		down_tsessions[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), down_T[2]);
	hdown_toh = m_stattree.InsertItem(GetResString(_T("STATS_OVRHD")), h_down_total);			// Downline Overhead (Total)
	for (unsigned i = 0; i < _countof(down_toh); ++i)
		down_toh[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hdown_toh);
	h_connection = m_stattree.InsertItem(GetResString(_T("CONNECTIONS")), 2, 2);				// Connection Section
	h_conn_session = m_stattree.InsertItem(GetResString(_T("STATS_SESSION")), 8, 8, h_connection); // Session Section (Connection)
	hconn_sg = m_stattree.InsertItem(GetResString(_T("STATS_GENERAL")), 11, 11, h_conn_session);	// General Section (Session)
	for (unsigned i = 0; i < 5; ++i)
		conn_sg[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hconn_sg);
	hconn_su = m_stattree.InsertItem(GetResString(_T("PW_CON_UPLBL")), 6, 6, h_conn_session);	// Uploads Section (Session)
	for (unsigned i = 0; i < 4; ++i)
		conn_su[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hconn_su);
	hconn_sd = m_stattree.InsertItem(GetResString(_T("PW_CON_DOWNLBL")), 7, 7, h_conn_session); // Downloads Section (Session)
	for (unsigned i = 0; i < 4; ++i)
		conn_sd[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hconn_sd);
	h_conn_total = m_stattree.InsertItem(GetResString(_T("STATS_CUMULATIVE")), 9, 9, h_connection);// Cumulative Section (Connection)
	hconn_tg = m_stattree.InsertItem(GetResString(_T("STATS_GENERAL")), 11, 11, h_conn_total); // General (Total)
	for (unsigned i = 0; i < 4; ++i)
		conn_tg[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hconn_tg);
	hconn_tu = m_stattree.InsertItem(GetResString(_T("PW_CON_UPLBL")), 6, 6, h_conn_total);	// Uploads (Total)
	for (unsigned i = 0; i < 3; ++i)
		conn_tu[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hconn_tu);
	hconn_td = m_stattree.InsertItem(GetResString(_T("PW_CON_DOWNLBL")), 7, 7, h_conn_total);	// Downloads (Total)
	for (unsigned i = 0; i < 3; ++i)
		conn_td[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hconn_td);
	h_time = m_stattree.InsertItem(GetResString(_T("STATS_TIMESTATS")), 12, 12);				// Time Statistics Section
	for (unsigned i = 0; i < 2; ++i)
		tvitime[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), h_time);
	htime_s = m_stattree.InsertItem(GetResString(_T("STATS_SESSION")), 8, 8, h_time);			// Session Section (Time)
	for (unsigned i = 0; i < 4; ++i)
		tvitime_s[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), htime_s);
	for (unsigned i = 0; i < 2; ++i)
		tvitime_st[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), tvitime_s[1]);
	htime_t = m_stattree.InsertItem(GetResString(_T("STATS_CUMULATIVE")), 9, 9, h_time);		// Cumulative Section (Time)
	for (unsigned i = 0; i < 3; ++i)
		tvitime_t[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), htime_t);
	for (unsigned i = 0; i < 2; ++i)
		tvitime_tt[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), tvitime_t[1]);
	htime_aap = m_stattree.InsertItem(GetResString(_T("STATS_AVGANDPROJ")), 13, 13, h_time);	// Projected Averages Section
	time_aaph[0] = m_stattree.InsertItem(GetResString(_T("DAILY")), 14, 14, htime_aap);		// Daily Section
	time_aaph[1] = m_stattree.InsertItem(GetResString(_T("STATS_MONTHLY")), 15, 15, htime_aap); // Monthly Section
	time_aaph[2] = m_stattree.InsertItem(GetResString(_T("STATS_YEARLY")), 16, 16, htime_aap); // Yearly Section
	for (int x = 0; x < 3; ++x) {
		time_aap_hup[x] = m_stattree.InsertItem(GetResString(_T("TW_UPLOADS")), 6, 6, time_aaph[x]); // Upload Section
		for (unsigned i = 0; i < 3; ++i)
			time_aap_up[x][i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), time_aap_hup[x]);
		time_aap_up_hd[x][0] = m_stattree.InsertItem(GetResString(_T("CLIENTS")), time_aap_up[x][0]);							// Clients Section
		for (unsigned i = 0; i < 7; ++i)
			time_aap_up_dc[x][i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), time_aap_up_hd[x][0]);
		time_aap_up_hd[x][1] = m_stattree.InsertItem(GetResString(_T("PORT")), time_aap_up[x][0]);								// Ports Section
		for (unsigned i = 0; i < _countof(time_aap_up_dp[0]); ++i)
			time_aap_up_dp[x][i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), time_aap_up_hd[x][1]);
		time_aap_up_hd[x][2] = m_stattree.InsertItem(GetResString(_T("STATS_DATASOURCE")), time_aap_up[x][0]);					// Data Source Section
		for (unsigned i = 0; i < 2; ++i)
			time_aap_up_ds[x][i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), time_aap_up_hd[x][2]);
		for (unsigned i = 0; i < 2; ++i)
			time_aap_up_s[x][i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), time_aap_up[x][1]);
		for (unsigned i = 0; i < 4; ++i)
			time_aap_up_oh[x][i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), time_aap_up[x][2]);
		time_aap_hdown[x] = m_stattree.InsertItem(GetResString(_T("TW_DOWNLOADS")), 7, 7, time_aaph[x]); // Download Section
		for (unsigned i = 0; i < 7; ++i)
		time_aap_down[x][i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), time_aap_hdown[x]);
	time_aap_down_hd[x][0] = m_stattree.InsertItem(GetResString(_T("CLIENTS")), time_aap_down[x][0]);							// Clients Section
	for (unsigned i = 0; i < 8; ++i)
		time_aap_down_dc[x][i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), time_aap_down_hd[x][0]);
		time_aap_down_hd[x][1] = m_stattree.InsertItem(GetResString(_T("PORT")), time_aap_down[x][0]);								// Ports Section
		for (unsigned i = 0; i < _countof(time_aap_down_dp[0]); ++i)
			time_aap_down_dp[x][i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), time_aap_down_hd[x][1]);
		for (unsigned i = 0; i < 2; ++i)
			time_aap_down_s[x][i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), time_aap_down[x][2]);
		for (unsigned i = 0; i < 4; ++i)
			time_aap_down_oh[x][i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), time_aap_down[x][6]);
	}
	h_clients = m_stattree.InsertItem(GetResString(_T("CLIENTS")), 3, 3);						// Clients Section
	m_useClientHistoryUI = thePrefs.GetClientHistory();
	if (m_useClientHistoryUI) {
		// All, Active, Archived branches directly under Clients
		for (int branch = 0; branch < CLIENT_POPULATION_COUNT; ++branch) {
			CString branchLabel = GetResString(GetPopulationStringId(branch));
			m_clientsPopulation[branch] = m_stattree.InsertItem(branchLabel, h_clients);
			InitializeClientBranch(branch);
		}
	} else {
		// When Client History is disabled, only show Active under Clients
		m_clientsPopulation[ClientPopulationAll] = NULL;
		m_clientsPopulation[ClientPopulationActive] = h_clients;
		m_clientsPopulation[ClientPopulationArchived] = NULL;
		InitializeClientBranch(ClientPopulationActive);
	}
	h_servers = m_stattree.InsertItem(GetResString(_T("FSTAT_SERVERS")), 4, 4);				// Servers section
	for (unsigned i = 0; i < 6; ++i)
		srv[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), h_servers);			// Servers Items
	for (unsigned i = 0; i < 3; ++i)
		srv_w[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), srv[0]);			// Working Servers Items
	hsrv_records = m_stattree.InsertItem(GetResString(_T("STATS_RECORDS")), 10, 10, h_servers); // Servers Records Section
	for (unsigned i = 0; i < 3; ++i)
		srv_r[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hsrv_records);	// Record Items
	h_shared = m_stattree.InsertItem(GetResString(_T("SHAREDFILES")), 5, 5);					// Shared Files Section
	for (unsigned i = 0; i < 4; ++i)
		shar[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), h_shared);
	hshar_records = m_stattree.InsertItem(GetResString(_T("STATS_RECORDS")), 10, 10, h_shared); // Shared Records Section
	for (unsigned i = 0; i < 4; ++i)
		shar_r[i] = m_stattree.InsertItem(GetResString(_T("FSTAT_WAITING")), hshar_records);
	h_total_downloads = m_stattree.InsertItem(GetResString(_T("DWTOT")), 17, 17);
	h_total_num_of_dls = m_stattree.InsertItem(GetResString(_T("DWTOT_NR")), h_total_downloads);
	h_total_size_of_dls = m_stattree.InsertItem(GetResString(_T("DWTOT_TSD")), h_total_downloads);
	h_total_size_dld = m_stattree.InsertItem(GetResString(_T("DWTOT_TCS")), h_total_downloads);
	h_total_size_left_to_dl = m_stattree.InsertItem(GetResString(_T("DWTOT_TSL")), h_total_downloads);
	h_total_size_left_on_drive = m_stattree.InsertItem(GetResString(_T("DWTOT_FS")), h_total_downloads);
	h_total_size_needed = m_stattree.InsertItem(GetResString(_T("DWTOT_TSN")), h_total_downloads);

#ifdef _DEBUG
	if (g_pfnPrevCrtAllocHook && IsDebugStatsVisible()) {
		h_debug = m_stattree.InsertItem(_T("Debug info"));m_stattree.SetItemData(h_debug, 0);
		h_blocks = m_stattree.InsertItem(_T("Blocks"), h_debug);m_stattree.SetItemData(h_blocks, 1);
		debug1 = m_stattree.InsertItem(_T("Free"), h_blocks);m_stattree.SetItemData(debug1, 1);
		debug2 = m_stattree.InsertItem(_T("Normal"), h_blocks);m_stattree.SetItemData(debug2, 1);
		debug3 = m_stattree.InsertItem(_T("CRT"), h_blocks);m_stattree.SetItemData(debug3, 1);
		debug4 = m_stattree.InsertItem(_T("Ignore"), h_blocks);m_stattree.SetItemData(debug4, 1);
		debug5 = m_stattree.InsertItem(_T("Client"), h_blocks);m_stattree.SetItemData(debug5, 1);
		m_stattree.Expand(h_debug, TVE_EXPAND);
		m_stattree.Expand(h_blocks, TVE_EXPAND);
	}
#endif

#ifdef USE_MEM_STATS
	// Optional allocator histogram (only when USE_MEM_STATS is enabled).
	// Note: This feature is not compatible with _DEBUG/_AFXDLL in current codebase.
	{
		for (int i = 0; i < ALLOC_SLOTS; ++i) {
			unsigned uStart, uEnd;
			if (i <= 1) {
				uStart = uEnd = i;
			} else {
				uStart = 1u << (i - 1);
				uEnd = (i == ALLOC_SLOTS - 1) ? UINT_MAX : (uStart << 1) - 1;
			}
			sItem.Format(_T("Block size %08X-%08X: %s (%1.1f%%)"), uStart, uEnd, (LPCTSTR)CastItoIShort(g_aAllocStats[i], false, 2), 0.0);
			h_allocSizes[i] = m_stattree.InsertItem(sItem, h_allocs);
		}
	}
#endif

	// Make section headers bold in order to make the tree easier to view at a glance.
	m_stattree.SetItemState(h_transfer, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(h_connection, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(h_time, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(htime_s, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(htime_t, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(htime_aap, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(h_total_downloads, TVIS_BOLD, TVIS_BOLD);
	for (int i = 0; i < 3; ++i) {
		m_stattree.SetItemState(time_aaph[i], TVIS_BOLD, TVIS_BOLD);
		m_stattree.SetItemState(time_aap_hup[i], TVIS_BOLD, TVIS_BOLD);
		m_stattree.SetItemState(time_aap_hdown[i], TVIS_BOLD, TVIS_BOLD);
	}
	m_stattree.SetItemState(h_clients, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(h_servers, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(h_shared, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(h_upload, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(h_download, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(h_up_session, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(h_up_total, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(h_down_session, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(h_down_total, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(h_conn_session, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(h_conn_total, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(hsrv_records, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(hshar_records, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(hconn_sg, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(hconn_su, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(hconn_sd, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(hconn_tg, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(hconn_tu, TVIS_BOLD, TVIS_BOLD);
	m_stattree.SetItemState(hconn_td, TVIS_BOLD, TVIS_BOLD);

	// Expand our purdy new tree...
	m_stattree.ApplyExpandedMask(thePrefs.GetExpandedTreeItems());

	// Select the top item so that the tree is not scrolled to the bottom when first viewed.
	m_stattree.SelectItem(h_transfer);
	m_stattree.Init();


	// End Tree Setup
}

void CStatisticsDlg::OnStnDblclickScopeD()
{
	theApp.emuledlg->ShowPreferences(IDD_PPG_STATS);
}

void CStatisticsDlg::OnStnDblclickScopeU()
{
	theApp.emuledlg->ShowPreferences(IDD_PPG_STATS);
}

void CStatisticsDlg::OnStnDblclickStatsscope()
{
	theApp.emuledlg->ShowPreferences(IDD_PPG_STATS);
}

LRESULT CStatisticsDlg::OnOscopePositionMsg(WPARAM, LPARAM lParam)
{
	LPCTSTR pszInfo = (LPCTSTR)lParam;
	m_TimeToolTips->UpdateTipText(pszInfo, GetDlgItem(IDC_SCOPE_D));
	m_TimeToolTips->UpdateTipText(pszInfo, GetDlgItem(IDC_SCOPE_U));
	m_TimeToolTips->UpdateTipText(pszInfo, GetDlgItem(IDC_STATSSCOPE));
	m_TimeToolTips->Update();
	return 0;
}

BOOL CStatisticsDlg::PreTranslateMessage(MSG *pMsg)
{
	m_TimeToolTips->RelayEvent(pMsg);

	if (pMsg->message == WM_KEYDOWN) {
		// Don't handle Ctrl+Tab in this window. It will be handled by main window.
		if (pMsg->wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0)
			return FALSE;
	}

	return CDialog::PreTranslateMessage(pMsg);
}

BOOL CStatisticsDlg::OnHelpInfo(HELPINFO*)
{
	theApp.ShowHelp(eMule_FAQ_GUI_Statistics);
	return TRUE;
}

HBRUSH CStatisticsDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = theApp.emuledlg->GetCtlColor(pDC, pWnd, nCtlColor);
	return hbr ? hbr : __super::OnCtlColor(pDC, pWnd, nCtlColor);
}
