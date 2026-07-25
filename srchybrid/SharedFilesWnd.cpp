//This file is part of eMule AI
//Copyright (C)2002-2026 Merkur ( devs@emule-project.net / https://www.emule-project.net )
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
#include "SharedFilesWnd.h"
#include "OtherFunctions.h"
#include "SharedFileList.h"
#include "KnownFileList.h"
#include "KnownFile.h"
#include "UserMsgs.h"
#include "HelpIDs.h"
#include "HighColorTab.hpp"
#include "eMuleAI/DarkMode.h"
#include "UploadQueue.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define	SPLITTER_RANGE_MIN		100
#define	SPLITTER_RANGE_MAX		350

#define	SPLITTER_MARGIN			0
#define	SPLITTER_WIDTH			4

#define	DFLT_TOOLBAR_BTN_WIDTH	27
#define	WNDS_BUTTON_XOFF	8
#define	WNDS_BUTTON_WIDTH	220
#define	WNDS_BUTTON_HEIGHT	22	// don't set the height do something different than 22 unless you know exactly what you are doing!
#define	NUMS_WINA_BUTTONS	2

namespace
{
	const int kSharedFilesFilterMinGapFromButtons = 3;
	const int kSharedFilesFilterRightMargin = 4;
	const int kSharedFilesFilterMinWidth = 120;
	const int kSharedFilesButtonGap = 3;
	const WPARAM kAutoReloadDeferredDispatchRequest = 4;

	int GetPushButtonIdealWidth(CWnd* pButton)
	{
		CString strText;
		pButton->GetWindowText(strText);

		CClientDC dc(pButton);
		CFont* pOldFont = NULL;
		if (pButton->GetFont() != NULL)
			pOldFont = dc.SelectObject(pButton->GetFont());
		const CSize sizeText = dc.GetTextExtent(strText);
		TEXTMETRIC textMetric = {};
		dc.GetTextMetrics(&textMetric);
		if (pOldFont != NULL)
			dc.SelectObject(pOldFont);

		return sizeText.cx + max(16, textMetric.tmAveCharWidth * 3);
	}

	enum ESharedFilesAutoReloadFlags
	{
		AutoReloadFiles = 0x01,
		AutoReloadRoots = 0x02,
		AutoReloadWatchedDirectories = 0x04,
		AutoReloadVisibleFileSystem = 0x08
	};

	DWORD AutoReloadRequestToFlags(WPARAM wpRequest)
	{
		switch (wpRequest) {
		case 1:
			return AutoReloadRoots;
		case 2:
			return AutoReloadWatchedDirectories;
		case 3:
			return AutoReloadVisibleFileSystem;
		case kAutoReloadDeferredDispatchRequest:
			return 0;
		default:
			return AutoReloadFiles;
		}
	}

	class CScopedTreeSelectionSyncBlocker
	{
	public:
		explicit CScopedTreeSelectionSyncBlocker(bool& rbFlag)
			: m_rbFlag(rbFlag)
		{
			m_rbFlag = true;
		}

		~CScopedTreeSelectionSyncBlocker()
		{
			m_rbFlag = false;
		}

	private:
		bool& m_rbFlag;
	};

	int GetPageBottomOverflow(CWnd *pPage)
	{
		if (pPage == NULL || !::IsWindow(pPage->GetSafeHwnd()) || !::IsWindowVisible(pPage->GetSafeHwnd()))
			return 0;

		CRect rcClient;
		pPage->GetClientRect(&rcClient);

		int nBottomMost = 0;
		for (CWnd *pChild = pPage->GetWindow(GW_CHILD); pChild != NULL; pChild = pChild->GetNextWindow()) {
			if (!::IsWindowVisible(pChild->GetSafeHwnd()))
				continue;

			CRect rcChild;
			pChild->GetWindowRect(&rcChild);
			pPage->ScreenToClient(&rcChild);
			if (rcChild.bottom > nBottomMost)
				nBottomMost = rcChild.bottom;
		}

		static const int s_iBottomMargin = 2;
		if (nBottomMost + s_iBottomMargin <= rcClient.bottom)
			return 0;
		return nBottomMost + s_iBottomMargin - rcClient.bottom;
	}
}

// CSharedFilesWnd dialog

IMPLEMENT_DYNAMIC(CSharedFilesWnd, CDialog)

BEGIN_MESSAGE_MAP(CSharedFilesWnd, CResizableDialog)
	ON_BN_CLICKED(IDC_RELOADSHAREDFILES, OnBnClickedReloadSharedFiles)
	ON_MESSAGE(UM_DELAYED_EVALUATE, OnChangeFilter)
	ON_NOTIFY(LVN_ITEMACTIVATE, IDC_SFLIST, OnLvnItemActivateSharedFiles)
	ON_NOTIFY(NM_CLICK, IDC_SFLIST, OnNmClickSharedFiles)
	ON_NOTIFY(TVN_SELCHANGED, IDC_SHAREDDIRSTREE, OnTvnSelChangedSharedDirsTree)
	ON_STN_DBLCLK(IDC_FILES_ICO, OnStnDblClickFilesIco)
	ON_WM_CTLCOLOR()
	ON_WM_HELPINFO()
	ON_WM_SIZE()
	ON_WM_SYSCOLORCHANGE()
	ON_BN_CLICKED(IDC_SF_HIDESHOWDETAILS, OnBnClickedSfHideshowdetails)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_SFLIST, OnLvnItemchangedSflist)
	ON_WM_SHOWWINDOW()
	ON_BN_CLICKED(IDC_UPDATE_METADATA, OnBnClickedUpdateMetaData)
	ON_MESSAGE(UM_SHOWFILESCOUNT, OnShowFilesCount)
	ON_MESSAGE(UM_METADATAUPDATED, OnMetadataUpdated)
	ON_MESSAGE(UM_SHARED_FILES_SELECTION_DETAILS, OnSelectedFilesDetails)
	ON_MESSAGE(UM_AUTO_RELOAD_SHARED_FILES, OnAutoReloadSharedFiles) // 0 = file-level change, 1 = roots changed, 2 = watched directory change, 3 = visible file-system branch poll
	ON_MESSAGE(UM_SHARED_FILES_ADJUST_DETAILS_PANEL, OnAdjustDetailsPanelHeight)

END_MESSAGE_MAP()

CSharedFilesWnd::CSharedFilesWnd(CWnd *pParent /*=NULL*/)
	: CResizableDialog(CSharedFilesWnd::IDD, pParent)
	, icon_files(NULL)
	, m_astrFilterTemp()
	, m_nFilterColumnLastApplied()
	, m_bColumnDiff(false)
	, m_strFullFilterExpr()
	, m_nFilterColumn()
	, m_bDetailsVisible(true)
	, m_bDetailsPanelHeightAdjustmentPending(false)
	, m_bDeferredTreeInitPending(true)
	, m_bSuppressTreeSelectionSync(false)
	, m_lShowFilesCountPending(0)
	, m_lMetadataUpdatedPending(0)
	, m_lSelectedFilesDetailsPending(0)
	, m_lSelectedFilesDetailsForce(0)
	, m_dwPendingAutoReloadFlags(0)
	, m_dwDeferredAutoReloadFlags(0)
	, m_lAutoReloadSharedFilesPending(0)
{
}

CSharedFilesWnd::~CSharedFilesWnd()
{
	m_ctlSharedListHeader.Detach();
	if (icon_files)
		VERIFY(::DestroyIcon(icon_files));
}

void CSharedFilesWnd::DoDataExchange(CDataExchange *pDX)
{
	CResizableDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SFLIST, sharedfilesctrl);
	DDX_Control(pDX, IDC_SHAREDDIRSTREE, m_ctlSharedDirTree);
	DDX_Control(pDX, IDC_SHAREDFILES_FILTER, m_ctlFilter);
}

BOOL CSharedFilesWnd::OnInitDialog()
{
	CResizableDialog::OnInitDialog();
	ModifyStyle(0, WS_CLIPCHILDREN);
	SetAllIcons();

	sharedfilesctrl.Init();
	m_ctlSharedDirTree.Initialize(&sharedfilesctrl);
	if (thePrefs.GetUseSystemFontForMainControls())
		m_ctlSharedDirTree.SendMessage(WM_SETFONT, NULL, FALSE);

	m_ctlSharedListHeader.Attach(sharedfilesctrl.GetHeaderCtrl()->Detach());
	CArray<int, int> aIgnore; // ignored no-text columns for filter edit
	aIgnore.Add(8); // shared parts
	aIgnore.Add(11); // shared ed2k/kad
	aIgnore.Add(22); // spread bar (UL part history)
	m_ctlFilter.OnInit(&m_ctlSharedListHeader, &aIgnore);

	RECT rcSpl;
	m_ctlSharedDirTree.GetWindowRect(&rcSpl);
	ScreenToClient(&rcSpl);

	CRect rcFiles;
	sharedfilesctrl.GetWindowRect(rcFiles);
	ScreenToClient(rcFiles);
	VERIFY(m_dlgDetails.Create(this, DS_CONTROL | DS_SETFONT | WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, WS_EX_CONTROLPARENT));
	m_dlgDetails.SetWindowPos(NULL, rcFiles.left, rcFiles.bottom + 4, rcFiles.Width() + 2, rcSpl.bottom - (rcFiles.bottom + 3), 0);
	AddOrReplaceAnchor(this, m_dlgDetails, BOTTOM_LEFT, BOTTOM_RIGHT);

	rcSpl.left = rcSpl.right + SPLITTER_MARGIN;
	rcSpl.right = rcSpl.left + SPLITTER_WIDTH;
	m_wndSplitter.CreateWnd(WS_CHILD | WS_VISIBLE, rcSpl, this, IDC_SPLITTER_SHAREDFILES);

	AddOrReplaceAnchor(this, IDC_SFLIST, TOP_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, m_ctlSharedDirTree, TOP_LEFT, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, IDC_RELOADSHAREDFILES, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_UPDATE_METADATA, TOP_RIGHT);
	AddAllOtherAnchors();

	int iPosStatInit = rcSpl.left;
	int iPosStatNew = thePrefs.GetSplitterbarPositionShared();
	if (iPosStatNew > SPLITTER_RANGE_MAX)
		iPosStatNew = SPLITTER_RANGE_MAX;
	else if (iPosStatNew < SPLITTER_RANGE_MIN)
		iPosStatNew = SPLITTER_RANGE_MIN;
	rcSpl.left = iPosStatNew;
	rcSpl.right = iPosStatNew + SPLITTER_WIDTH;
	m_wndSplitter.MoveWindow(&rcSpl);
	DoResize(iPosStatNew - iPosStatInit);

	GetDlgItem(IDC_SF_HIDESHOWDETAILS)->SetFont(&theApp.m_fontSymbol);
	GetDlgItem(IDC_SF_HIDESHOWDETAILS)->BringWindowToTop();
	ShowDetailsPanel(thePrefs.GetShowSharedFilesDetails());

	InitWindowStyles(this); //Moved down
	LocalizeInternal(true);
	RequestDetailsPanelHeightAdjustment();
	return TRUE;
}

void CSharedFilesWnd::DoResize(int iDelta)
{
	CSplitterControl::ChangeWidth(&m_ctlSharedDirTree, iDelta);
	CSplitterControl::ChangeWidth(&m_ctlFilter, iDelta);
	CSplitterControl::ChangePos(&sharedfilesctrl, -iDelta, 0);
	CSplitterControl::ChangeWidth(&sharedfilesctrl, -iDelta);

	bool bAntiFlicker = (m_dlgDetails.IsWindowVisible() != FALSE);
	if (bAntiFlicker)
		m_dlgDetails.SetRedraw(FALSE);
	CSplitterControl::ChangePos(&m_dlgDetails, -iDelta, 0);
	CSplitterControl::ChangeWidth(&m_dlgDetails, -iDelta);
	if (bAntiFlicker)
		m_dlgDetails.SetRedraw(TRUE);

	RECT rcSpl;
	m_wndSplitter.GetWindowRect(&rcSpl);
	ScreenToClient(&rcSpl);
	thePrefs.SetSplitterbarPositionShared(rcSpl.left);

	GetDlgItem(IDC_SF_FICON)->SetWindowPos(NULL, rcSpl.right + 6, rcSpl.bottom - 18, 0, 0, SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
	CWnd *wname = GetDlgItem(IDC_SF_FNAME);
	RECT rcfname;
	wname->GetWindowRect(&rcfname);
	ScreenToClient(&rcfname);
	rcfname.left += iDelta;
	wname->MoveWindow(&rcfname);
	EnsureFilterControlLayout();

	AddOrReplaceAnchor(this, m_ctlFilter, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_UPDATE_METADATA, TOP_RIGHT);
	AddOrReplaceAnchor(this, IDC_RELOADSHAREDFILES, TOP_RIGHT);
	AddOrReplaceAnchor(this, m_ctlSharedDirTree, TOP_LEFT, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, m_wndSplitter, TOP_LEFT, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, IDC_SFLIST, TOP_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, m_dlgDetails, BOTTOM_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_SF_FICON, BOTTOM_LEFT);
	AddOrReplaceAnchor(this, IDC_SF_FNAME, BOTTOM_LEFT, BOTTOM_RIGHT);

	RECT rcWnd;
	GetWindowRect(&rcWnd);
	ScreenToClient(&rcWnd);
	m_wndSplitter.SetRange(rcWnd.left + SPLITTER_RANGE_MIN, rcWnd.left + SPLITTER_RANGE_MAX);

	RequestDetailsPanelHeightAdjustment();
	Invalidate(FALSE);
}

void CSharedFilesWnd::EnsureFilterControlLayout()
{
	if (!::IsWindow(m_hWnd) || !::IsWindow(m_ctlFilter.GetSafeHwnd()))
		return;

	CWnd* pReloadButton = GetDlgItem(IDC_RELOADSHAREDFILES);
	CWnd* pUpdateMetadataButton = GetDlgItem(IDC_UPDATE_METADATA);
	if (pReloadButton == NULL || pUpdateMetadataButton == NULL)
		return;

	CRect rcClient;
	GetClientRect(&rcClient);

	CRect rcFilter;
	m_ctlFilter.GetWindowRect(&rcFilter);
	ScreenToClient(&rcFilter);

	CRect rcReloadButton;
	pReloadButton->GetWindowRect(&rcReloadButton);
	ScreenToClient(&rcReloadButton);

	CRect rcUpdateMetadataButton;
	pUpdateMetadataButton->GetWindowRect(&rcUpdateMetadataButton);
	ScreenToClient(&rcUpdateMetadataButton);

	const int iButtonWidth = max(GetPushButtonIdealWidth(pReloadButton), GetPushButtonIdealWidth(pUpdateMetadataButton));
	const int iReloadLeft = rcReloadButton.right - iButtonWidth;
	const int iUpdateMetadataRight = iReloadLeft - kSharedFilesButtonGap;
	const int iUpdateMetadataLeft = iUpdateMetadataRight - iButtonWidth;
	if (rcReloadButton.left != iReloadLeft || rcReloadButton.Width() != iButtonWidth) {
		pReloadButton->SetWindowPos(NULL, iReloadLeft, rcReloadButton.top, iButtonWidth, rcReloadButton.Height(), SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
		AddOrReplaceAnchor(this, IDC_RELOADSHAREDFILES, TOP_RIGHT);
	}
	if (rcUpdateMetadataButton.left != iUpdateMetadataLeft || rcUpdateMetadataButton.Width() != iButtonWidth) {
		pUpdateMetadataButton->SetWindowPos(NULL, iUpdateMetadataLeft, rcUpdateMetadataButton.top, iButtonWidth, rcUpdateMetadataButton.Height(), SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
		AddOrReplaceAnchor(this, IDC_UPDATE_METADATA, TOP_RIGHT);
	}

	const int iMinLeft = max(rcReloadButton.right, rcUpdateMetadataButton.right) + kSharedFilesFilterMinGapFromButtons;
	const int iMaxRight = rcClient.right - kSharedFilesFilterRightMargin;
	const int iMaxWidth = max(0, iMaxRight - iMinLeft);
	if (iMaxWidth <= 0)
		return;

	const int iNewWidth = max(min(rcFilter.Width(), iMaxWidth), min(kSharedFilesFilterMinWidth, iMaxWidth));
	const int iNewLeft = max(iMinLeft, iMaxRight - iNewWidth);
	if (rcFilter.left == iNewLeft && rcFilter.Width() == iNewWidth)
		return;

	m_ctlFilter.SetWindowPos(NULL, iNewLeft, rcFilter.top, iNewWidth, rcFilter.Height(), SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CSharedFilesWnd::Reload(bool bForceTreeReload, bool bUserForced, LONG lDirWatchGeneration)
{
	// Avoid touching the directory tree on pure file-level changes.
	if (bForceTreeReload) {
		m_bDeferredTreeInitPending = false;
		sharedfilesctrl.SetDirectoryFilter(NULL, false); // Only force a full tree rebuild if the user explicitly requested it. Otherwise, let the control detect structural changes and bypass rebuild when unnecessary.
		m_ctlSharedDirTree.Reload(bUserForced ? true : false);
		sharedfilesctrl.SetDirectoryFilter(m_ctlSharedDirTree.GetSelectedFilter(), false);
	}

	theApp.sharedfiles->Reload(lDirWatchGeneration); // Always refresh the shared files list (fast path for file-only changes).

	if (bForceTreeReload) {
		if (theApp.DirWatchRootsChanged())
			theApp.StartDirWatchTP();

		theApp.SyncDirWatchRootsHash();
	}

	ShowSelectedFilesDetails();
}

void CSharedFilesWnd::OnStnDblClickFilesIco()
{
	theApp.emuledlg->ShowPreferences(IDD_PPG_DIRECTORIES);
}

void CSharedFilesWnd::OnSize(UINT nType, int cx, int cy)
{
	CResizableDialog::OnSize(nType, cx, cy);

	if (nType == SIZE_MINIMIZED || !::IsWindow(m_hWnd) || !::IsWindow(m_dlgDetails.GetSafeHwnd()))
		return;

	if (m_wndSplitter) {
		RECT rcWnd;
		GetWindowRect(&rcWnd);
		ScreenToClient(&rcWnd);
		m_wndSplitter.SetRange(rcWnd.left + SPLITTER_RANGE_MIN, rcWnd.left + SPLITTER_RANGE_MAX);
	}

	EnsureFilterControlLayout();
	RequestDetailsPanelHeightAdjustment();
}


void CSharedFilesWnd::OnBnClickedReloadSharedFiles()
{
	Reload(true, true); // Manual reload: user explicitly requests full tree rebuild
}

void CSharedFilesWnd::OnBnClickedUpdateMetaData()
{
	theApp.sharedfiles->RebuildMetaData();
}

// Coalesced auto-reload from threadpool I/O watcher; keep it cheap and deterministic.
LRESULT CSharedFilesWnd::OnAutoReloadSharedFiles(WPARAM wParam, LPARAM)
{
	DWORD dwReloadFlags = 0;
	{
		CSingleLock lock(&m_autoReloadRequestLock, TRUE);
		dwReloadFlags = m_dwPendingAutoReloadFlags | AutoReloadRequestToFlags(wParam);
		m_dwPendingAutoReloadFlags = 0;
		InterlockedExchange(&m_lAutoReloadSharedFilesPending, 0);
	}

	MSG msg;
	while (::PeekMessage(&msg, m_hWnd, UM_AUTO_RELOAD_SHARED_FILES, UM_AUTO_RELOAD_SHARED_FILES, PM_REMOVE))
		dwReloadFlags |= AutoReloadRequestToFlags(msg.wParam);

	const DWORD dwDeferrableReloadFlags = AutoReloadFiles | AutoReloadWatchedDirectories;
	if ((dwReloadFlags & AutoReloadRoots) == 0 && (dwReloadFlags & dwDeferrableReloadFlags) != 0 && theApp.sharedfiles != NULL && theApp.sharedfiles->HasActiveSharedFilesWork()) {
		const DWORD dwDeferredReloadFlags = dwReloadFlags & dwDeferrableReloadFlags;
		{
			CSingleLock lock(&m_autoReloadRequestLock, TRUE);
			m_dwDeferredAutoReloadFlags |= dwDeferredReloadFlags;
		}
		dwReloadFlags &= ~dwDeferredReloadFlags;
		if (dwReloadFlags == 0)
			return 0;
	}

	const bool bNeedFileReload = (dwReloadFlags & AutoReloadFiles) != 0;
	const bool bNeedRootReload = (dwReloadFlags & AutoReloadRoots) != 0;
	const bool bNeedWatchedDirectoryReload = (dwReloadFlags & AutoReloadWatchedDirectories) != 0;
	const bool bNeedVisibleFileSystemPoll = (dwReloadFlags & AutoReloadVisibleFileSystem) != 0;

	if (bNeedVisibleFileSystemPoll) {
		bool bTreeChanged = false;
		{
			CScopedTreeSelectionSyncBlocker suppressSelectionSync(m_bSuppressTreeSelectionSync);
			bTreeChanged = m_ctlSharedDirTree.RefreshVisibleFileSystemBranchesIfNeeded();
		}

		if (bTreeChanged) {
			ApplySelectedTreeFilter(true);
			ShowSelectedFilesDetails();
		}

		if (!bNeedFileReload && !bNeedRootReload && !bNeedWatchedDirectoryReload)
			return 0;
	}

	// Apply watcher-side deleted-dir pruning before reconcile/reload so the model matches the upcoming tree refresh.
	if (bNeedWatchedDirectoryReload)
		theApp.DrainDeletedAutoSharedDirs();

	CStringArray changedDirs;
	if (bNeedWatchedDirectoryReload) {
		theApp.DrainDirWatchChangedDirectories(changedDirs);
		if (changedDirs.GetCount() > 0) {
			CScopedTreeSelectionSyncBlocker suppressSelectionSync(m_bSuppressTreeSelectionSync);
			m_ctlSharedDirTree.RefreshVisibleSharedBranches(changedDirs);
			m_ctlSharedDirTree.RefreshVisibleFileSystemBranches(changedDirs);
		}

		ApplySelectedTreeFilter(false);
	}

	CStringArray changedFiles;
	theApp.DrainDirWatchChangedFiles(changedFiles);
	if (changedFiles.GetCount() > 0)
		theApp.sharedfiles->ReconcileMovedSharedFiles(changedFiles);

	// Drain auto-added subdirs before rebuilding tree, so model matches view.
	if (bNeedWatchedDirectoryReload)
		theApp.DrainAutoSharedNewDirs();

	const LONG lDirWatchGeneration = (!bNeedRootReload && (bNeedFileReload || bNeedWatchedDirectoryReload)) ? theApp.GetDirWatchChangeGeneration() : 0;
	if (bNeedRootReload)
		Reload(true, true, 0); // Structural root changes must cancel stale pending hash work.
	else if (bNeedWatchedDirectoryReload)
		Reload(true, false, lDirWatchGeneration); // Directory created/removed under a root: let tree detect structural change and rebuild only if needed.
	else if (bNeedFileReload)
		Reload(false, false, lDirWatchGeneration); // File-level change: fast path.

	if (theApp.uploadqueue != NULL && !theApp.sharedfiles->IsReloading())
		theApp.uploadqueue->PruneWaitersForMissingSharedFiles();

	return 0;

}

void CSharedFilesWnd::OnLvnItemActivateSharedFiles(LPNMHDR, LRESULT*)
{
	ShowSelectedFilesDetails();
}

void CSharedFilesWnd::OnNmClickSharedFiles(LPNMHDR pNMHDR, LRESULT *pResult)
{
	OnLvnItemActivateSharedFiles(pNMHDR, pResult);
	*pResult = 0;
}

BOOL CSharedFilesWnd::PreTranslateMessage(MSG *pMsg)
{
	if (theApp.emuledlg->m_pSplashWnd)
		return FALSE;
	switch (pMsg->message) {
	case WM_KEYDOWN:
		// Don't handle Ctrl+Tab in this window. It will be handled by main window.
		if (pMsg->wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0)
			return FALSE;
		if (pMsg->wParam == VK_ESCAPE)
			return FALSE;
		if (pMsg->wParam == VK_RETURN && thePrefs.IsDisableFindAsYouType()) {
			CEditDelayed::SFilterParam* wParam = new CEditDelayed::SFilterParam;
			wParam->bForceApply = true; // We need to force OnChangeFilter to filter+reload listbox
			wParam->uColumnIndex = 0;
			OnChangeFilter(reinterpret_cast<WPARAM>(wParam), NULL); // We dont have lParam at this point, so send dummy null
		}
		break;
	case WM_KEYUP:
		if (pMsg->hwnd == sharedfilesctrl.m_hWnd)
			OnLvnItemActivateSharedFiles(0, 0);
		break;
	case WM_MBUTTONUP:
		{
			CPoint point;
			if (!::GetCursorPos(&point))
				return FALSE;
			m_ToolTip.Activate(TRUE);
			m_ToolTip.RelayEvent(pMsg);
			sharedfilesctrl.ScreenToClient(&point);
			int it = sharedfilesctrl.HitTest(point);
			if (it < 0)
				return FALSE;

			sharedfilesctrl.SetItemState(-1, 0, LVIS_SELECTED);
			sharedfilesctrl.SetItemState(it, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			sharedfilesctrl.SetSelectionMark(it);   // display selection mark correctly!
			sharedfilesctrl.ShowComments(reinterpret_cast<CShareableFile*>(sharedfilesctrl.GetItemObject(it)));
			return TRUE;
		}
		break;
	case WM_UNICHAR: //Probably never entered this case, but added it anyway.
	case WM_CHAR: //This will direct text entries to edit box
	{
		CWnd* pFocus = GetFocus(); // GetFocus can return NULL when our window is not active; guard before IsKindOf.
		if (GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_SPACE) >= 0 && (!pFocus || !pFocus->IsKindOf(RUNTIME_CLASS(CEdit)))) {
			m_ctlFilter.SetFocus();
			m_ctlFilter.SetWindowTextW(EMPTY);
			m_ctlFilter.SendMessage(pMsg->message, pMsg->wParam, pMsg->lParam);
			return true;
		}
	}
	break;
	case WM_MOUSEMOVE:
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	{
		// This will reactivate the tooltip
		m_ToolTip.Activate(TRUE);
		m_ToolTip.RelayEvent(pMsg);
	}
	break;
	}

	return CResizableDialog::PreTranslateMessage(pMsg);
}

void CSharedFilesWnd::OnSysColorChange()
{
	CResizableDialog::OnSysColorChange();
	SetAllIcons();
	m_ctlFilter.ShowColumnText(true); // forces the placeholder text
}

void CSharedFilesWnd::SetAllIcons()
{
	if (icon_files) {
		::DestroyIcon(icon_files);
		icon_files = NULL;
	}

	LPCTSTR pszRes = _T("SharedFilesList");
	switch (sharedfilesctrl.m_eFilter) {
		case FilterType::History:    pszRes = _T("FileHistory"); break;
		case FilterType::Duplicate:  pszRes = _T("Duplicate");   break;
		case FilterType::FileSystem: pszRes = _T("HardDisk");    break;
	}

	icon_files = reinterpret_cast<HICON>(::LoadImage(AfxGetResourceHandle(), pszRes, IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR | LR_CREATEDIBSECTION)); // Private handle, caller must free
	static_cast<CStatic*>(GetDlgItem(IDC_FILES_ICO))->SetIcon(icon_files);
}

void CSharedFilesWnd::ApplySelectedTreeFilter(bool bRefreshList)
{
	CDirectoryItem* pNewDirectoryFilter = m_ctlSharedDirTree.GetSelectedFilter();
	if (pNewDirectoryFilter == NULL)
		return;

	if (m_bDeferredTreeInitPending && (pNewDirectoryFilter->m_eItemType == SDI_DIRECTORY || (pNewDirectoryFilter->m_eItemType == SDI_INCOMING && thePrefs.GetAutoShareSubdirs()))) {
		TryCompleteDeferredTreeInit();
		pNewDirectoryFilter = m_ctlSharedDirTree.GetSelectedFilter();
		if (pNewDirectoryFilter == NULL)
			return;
	}

	if (!sharedfilesctrl)
		return;

	sharedfilesctrl.m_eFilter = FilterType::Shared; // Set default value

	if (pNewDirectoryFilter->m_eItemType == SDI_ALLHISTORY || (m_ctlSharedDirTree.GetSelectedFilterParent() && m_ctlSharedDirTree.GetSelectedFilterParent()->m_eItemType == SDI_ALLHISTORY))
		sharedfilesctrl.m_eFilter = FilterType::History;

	// This part needs to be executed after SetDirectoryFilter since they uses m_eItemType.
	if (sharedfilesctrl.m_eFilter != FilterType::History) {
		if ((m_ctlSharedDirTree.GetSelectedFilter() && m_ctlSharedDirTree.GetSelectedFilter()->m_eItemType == SDI_DUP) ||
			(m_ctlSharedDirTree.GetSelectedFilterParent() && m_ctlSharedDirTree.GetSelectedFilterParent()->m_eItemType == SDI_DUP))
			sharedfilesctrl.m_eFilter = FilterType::Duplicate;
		else if (m_ctlSharedDirTree.GetSelectedFilter() && (m_ctlSharedDirTree.GetSelectedFilter()->m_eItemType == SDI_UNSHAREDDIRECTORY || m_ctlSharedDirTree.GetSelectedFilter()->m_eItemType == SDI_FILESYSTEMPARENT))
			sharedfilesctrl.m_eFilter = FilterType::FileSystem;
	}

	if (sharedfilesctrl.m_eFilter == FilterType::History)
		sharedfilesctrl.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_DOUBLEBUFFER);
	else if (sharedfilesctrl.CheckBoxesEnabled())
		sharedfilesctrl.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);
	else
		sharedfilesctrl.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_DOUBLEBUFFER);

	if (sharedfilesctrl.m_eFilter == FilterType::FileSystem)
		GetDlgItem(IDC_UPDATE_METADATA)->EnableWindow(FALSE);
	else
		GetDlgItem(IDC_UPDATE_METADATA)->EnableWindow(TRUE);

	SetAllIcons();
	sharedfilesctrl.SetDirectoryFilter(pNewDirectoryFilter, bRefreshList);
}

void CSharedFilesWnd::Localize()
{
	LocalizeInternal(false);
}

void CSharedFilesWnd::LocalizeInternal(bool bDeferTreeInit)
{
	sharedfilesctrl.Localize();
	if (bDeferTreeInit || m_bDeferredTreeInitPending)
		m_ctlSharedDirTree.LocalizeSkeleton();
	else
		m_ctlSharedDirTree.Localize();
	m_ctlFilter.ShowColumnText(true);
	sharedfilesctrl.SetDirectoryFilter(NULL, true);
	SetDlgItemText(IDC_RELOADSHAREDFILES, GetResString(_T("SF_RELOAD")));
	SetDlgItemText(IDC_UPDATE_METADATA, GetResString(_T("UPDATE_METADATA")));
	EnsureFilterControlLayout();

	if (m_ToolTip.GetSafeHwnd() == NULL) {
		if (m_ToolTip.Create(this)) {
			m_ToolTip.AddTool(GetDlgItem(IDC_RELOADSHAREDFILES), GetResString(_T("SF_RELOAD_INFO")));
			m_ToolTip.AddTool(GetDlgItem(IDC_UPDATE_METADATA), GetResString(_T("UPDATE_METADATA_INFO")));
			m_ToolTip.Activate(TRUE);
		}
	} else {
		m_ToolTip.UpdateTipText(GetResString(_T("SF_RELOAD_INFO")), GetDlgItem(IDC_RELOADSHAREDFILES));
		m_ToolTip.UpdateTipText(GetResString(_T("UPDATE_METADATA_INFO")), GetDlgItem(IDC_UPDATE_METADATA));
	}

	m_dlgDetails.Localize();
}

void CSharedFilesWnd::TryCompleteDeferredTreeInit()
{
	if (!m_bDeferredTreeInitPending || theApp.IsClosing())
		return;
	if (!::IsWindow(m_hWnd) || !IsWindowVisible())
		return;

	m_bDeferredTreeInitPending = false;
	m_ctlSharedDirTree.Reload(true);
	sharedfilesctrl.SetDirectoryFilter(m_ctlSharedDirTree.GetSelectedFilter(), false);
	ShowSelectedFilesDetails(true);
}

void CSharedFilesWnd::OnTvnSelChangedSharedDirsTree(LPNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	if (m_bSuppressTreeSelectionSync)
		return;

	// We don't want to reload the list when creating the tree, because it will be reloaded after the tree is created anyway.
	ApplySelectedTreeFilter(!m_ctlSharedDirTree.IsCreatingTree());
}

LRESULT CSharedFilesWnd::DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_PAINT:
		if (m_wndSplitter) {
			CRect rcWnd;
			GetWindowRect(rcWnd);
			if (rcWnd.Width() > 0) {
				RECT rcSpl;
				m_ctlSharedDirTree.GetWindowRect(&rcSpl);
				ScreenToClient(&rcSpl);
				rcSpl.left = rcSpl.right + SPLITTER_MARGIN;
				rcSpl.right = rcSpl.left + SPLITTER_WIDTH;

				RECT rcFilter;
				m_ctlFilter.GetWindowRect(&rcFilter);
				ScreenToClient(&rcFilter);
				rcSpl.top = rcFilter.top;
				m_wndSplitter.MoveWindow(&rcSpl, TRUE);
			}
		}
		break;
	case WM_NOTIFY:
		if (wParam == IDC_SPLITTER_SHAREDFILES) {
			SPC_NMHDR *pHdr = reinterpret_cast<SPC_NMHDR*>(lParam);
			DoResize(pHdr->delta);
		}
		break;
	case WM_SIZE:
		if (m_wndSplitter) {
			RECT rcWnd;
			GetWindowRect(&rcWnd);
			ScreenToClient(&rcWnd);
			m_wndSplitter.SetRange(rcWnd.left + SPLITTER_RANGE_MIN, rcWnd.left + SPLITTER_RANGE_MAX);
		}
	}
	return CResizableDialog::DefWindowProc(message, wParam, lParam);
}

LRESULT CSharedFilesWnd::OnChangeFilter(WPARAM wParam, LPARAM lParam)
{
	CEditDelayed::SFilterParam* pFilterParam = reinterpret_cast<CEditDelayed::SFilterParam*>(wParam);
	bool m_bForceApplyFilter = false;
	uint32 m_nFilterColumnTemp = 0;

	if (pFilterParam) {
		m_bForceApplyFilter = pFilterParam->bForceApply;
		m_nFilterColumnTemp = pFilterParam->uColumnIndex;
		delete pFilterParam;
		pFilterParam = nullptr;
	}

	if (!m_bForceApplyFilter) {
		//If not forced to apply filter, read parameters as usual.
		m_strFullFilterExpr = (LPCTSTR)lParam;
		m_nFilterColumn = m_nFilterColumnTemp;

		if (thePrefs.IsDisableFindAsYouType())
			return 0;
	} else if (thePrefs.IsDisableFindAsYouType())
		//If forced to apply filter, we need to read current entered text directly since CEditDelayed will delay lParam.
		m_ctlFilter.GetWindowText(m_strFullFilterExpr);

	m_astrFilterTemp.RemoveAll();
	for (int iPos = 0; iPos >= 0;) {
		const CString& strFilter(m_strFullFilterExpr.Tokenize(_T(" "), iPos));
		if (!strFilter.IsEmpty() && strFilter != _T("-"))
			m_astrFilterTemp.Add(strFilter);
	}

	m_bColumnDiff = (m_nFilterColumn != m_nFilterColumnLastApplied);
	m_nFilterColumnLastApplied = m_nFilterColumn;
	bool bFilterDiff = (m_astrFilterTemp.GetCount() != m_astrFilter.GetCount());

	if (!bFilterDiff)
		for (INT_PTR i = m_astrFilterTemp.GetCount(); --i >= 0;)
			if (m_astrFilterTemp[i] != m_astrFilter[i]) {
				bFilterDiff = true;
				break;
			}

	// Added m_bForceApplyFilter to force filtering with enter/return keys
	if (m_bColumnDiff || bFilterDiff || m_bForceApplyFilter) {
		m_astrFilter.RemoveAll();
		m_astrFilter.Append(m_astrFilterTemp);
		sharedfilesctrl.ReloadList(false, LSF_SELECTION);
	}

	return 0;
}

BOOL CSharedFilesWnd::OnHelpInfo(HELPINFO*)
{
	theApp.ShowHelp(eMule_FAQ_GUI_SharedFiles);
	return TRUE;
}

HBRUSH CSharedFilesWnd::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = theApp.emuledlg->GetCtlColor(pDC, pWnd, nCtlColor);
	return hbr ? hbr : __super::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CSharedFilesWnd::SetToolTipsDelay(DWORD dwDelay)
{
	sharedfilesctrl.SetToolTipsDelay(dwDelay);
}

void CSharedFilesWnd::PostSelectedFilesDetailsAsync(bool bForce)
{
	if (!::IsWindow(m_hWnd))
		return;
	if (bForce)
		InterlockedExchange(&m_lSelectedFilesDetailsForce, 1);
	if (InterlockedCompareExchange(&m_lSelectedFilesDetailsPending, 1, 0) == 0)
		PostMessage(UM_SHARED_FILES_SELECTION_DETAILS);
}

LRESULT CSharedFilesWnd::OnSelectedFilesDetails(WPARAM, LPARAM)
{
	InterlockedExchange(&m_lSelectedFilesDetailsPending, 0);
	const bool bForce = InterlockedExchange(&m_lSelectedFilesDetailsForce, 0) != 0;
	ShowSelectedFilesDetails(bForce);
	return 0;
}

void CSharedFilesWnd::ShowSelectedFilesDetails(bool bForce)
{
	CTypedPtrList<CPtrList, CShareableFile*> selectedList;
	UINT nItems = m_dlgDetails.GetItems().GetSize();
	if (m_bDetailsVisible) {
		int i = 0;
		for (POSITION pos = sharedfilesctrl.GetFirstSelectedItemPosition(); pos != NULL;) {
			int index = sharedfilesctrl.GetNextSelectedItem(pos);
			if (index < 0 || static_cast<size_t>(index) >= sharedfilesctrl.m_ListedItemsVector.size())
				continue;
			CKnownFile* cur_file = sharedfilesctrl.m_ListedItemsVector[static_cast<size_t>(index)];
			CShareableFile* file = reinterpret_cast<CShareableFile*>(cur_file);
			if (file != NULL) {
				selectedList.AddTail(file);
				if (nItems <= (UINT)i || m_dlgDetails.GetItems()[i] != file)
					bForce = true;
				++i;
			}
		}
	} else if (GetDlgItem(IDC_SF_FNAME)->IsWindowVisible()) {
		CShareableFile *pFile = NULL;
		if (sharedfilesctrl.GetSelectedCount() == 1) {
			POSITION pos = sharedfilesctrl.GetFirstSelectedItemPosition();
			if (pos) {
				int index = sharedfilesctrl.GetNextSelectedItem(pos);
				if (index >= 0 && static_cast<size_t>(index) < sharedfilesctrl.m_ListedItemsVector.size()) {
					CKnownFile* cur_file = sharedfilesctrl.m_ListedItemsVector[static_cast<size_t>(index)];
					pFile = reinterpret_cast<CShareableFile*>(cur_file);
				}
			}
		}
		static_cast<CStatic*>(GetDlgItem(IDC_SF_FICON))->SetIcon(pFile ? icon_files : NULL);
		const CString& sName(pFile ? pFile->GetFileName() : EMPTY);
		SetDlgItemText(IDC_SF_FNAME, sName);
	}
	if (bForce || nItems != (UINT)selectedList.GetCount())
		m_dlgDetails.SetFiles(selectedList);
}

void CSharedFilesWnd::UpdateDetailsPanelLayout()
{
	CRect rcFiles;
	sharedfilesctrl.GetWindowRect(rcFiles);
	ScreenToClient(rcFiles);

	CRect rcDetailDlg;
	m_dlgDetails.GetWindowRect(rcDetailDlg);

	CWnd &button = *GetDlgItem(IDC_SF_HIDESHOWDETAILS);
	CRect rcButton;
	button.GetWindowRect(rcButton);

	RECT rcSpl;
	m_wndSplitter.GetWindowRect(&rcSpl);
	ScreenToClient(&rcSpl);

	if (m_bDetailsVisible) {
		sharedfilesctrl.SetWindowPos(NULL, 0, 0, rcFiles.Width(), rcSpl.bottom - rcFiles.top - rcDetailDlg.Height() - 2, SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
		m_dlgDetails.ShowWindow(SW_SHOW);
		GetDlgItem(IDC_SF_FICON)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_SF_FNAME)->ShowWindow(SW_HIDE);
		button.SetWindowPos(NULL, rcFiles.right - rcButton.Width() + 1, rcSpl.bottom - rcDetailDlg.Height() + 2, 0, 0, SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
		button.SetWindowText(_T("6"));
	} else {
		m_dlgDetails.ShowWindow(SW_HIDE);
		sharedfilesctrl.SetWindowPos(NULL, 0, 0, rcFiles.Width(), rcSpl.bottom - rcFiles.top - rcButton.Height() + 1, SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
		GetDlgItem(IDC_SF_FICON)->ShowWindow(SW_SHOW);
		GetDlgItem(IDC_SF_FNAME)->ShowWindow(SW_SHOW);
		button.SetWindowPos(NULL, rcFiles.right - rcButton.Width() + 1, rcSpl.bottom - rcButton.Height() + 1, 0, 0, SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
		button.SetWindowText(_T("5"));
	}

	AddOrReplaceAnchor(this, sharedfilesctrl, TOP_LEFT, BOTTOM_RIGHT);
	AddOrReplaceAnchor(this, IDC_SF_HIDESHOWDETAILS, BOTTOM_RIGHT);
}

void CSharedFilesWnd::ShowDetailsPanel(bool bShow)
{
	m_bDetailsVisible = bShow;
	thePrefs.SetShowSharedFilesDetails(bShow);

	UpdateDetailsPanelLayout();
	ShowSelectedFilesDetails();
	RedrawDetailsPanelArea();
	sharedfilesctrl.SetFocus();
	if (m_bDetailsVisible)
		RequestDetailsPanelHeightAdjustment();
}

void CSharedFilesWnd::RedrawDetailsPanelArea()
{
	if (!::IsWindow(m_hWnd))
		return;

	CRect rcRedraw;
	sharedfilesctrl.GetWindowRect(&rcRedraw);
	ScreenToClient(&rcRedraw);

	CRect rcChild;
	if (::IsWindow(m_dlgDetails.GetSafeHwnd())) {
		m_dlgDetails.GetWindowRect(&rcChild);
		ScreenToClient(&rcChild);
		rcRedraw.UnionRect(&rcRedraw, &rcChild);
	}

	CWnd *pName = GetDlgItem(IDC_SF_FNAME);
	if (pName != NULL) {
		pName->GetWindowRect(&rcChild);
		ScreenToClient(&rcChild);
		rcRedraw.UnionRect(&rcRedraw, &rcChild);
	}

	CWnd *pIcon = GetDlgItem(IDC_SF_FICON);
	if (pIcon != NULL) {
		pIcon->GetWindowRect(&rcChild);
		ScreenToClient(&rcChild);
		rcRedraw.UnionRect(&rcRedraw, &rcChild);
	}

	CWnd *pButton = GetDlgItem(IDC_SF_HIDESHOWDETAILS);
	if (pButton != NULL) {
		pButton->GetWindowRect(&rcChild);
		ScreenToClient(&rcChild);
		rcRedraw.UnionRect(&rcRedraw, &rcChild);
	}

	RedrawWindow(&rcRedraw, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
	if (::IsWindow(m_dlgDetails.GetSafeHwnd()) && m_dlgDetails.IsWindowVisible())
		m_dlgDetails.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void CSharedFilesWnd::RequestDetailsPanelHeightAdjustment()
{
	if (!m_bDetailsVisible || m_bDetailsPanelHeightAdjustmentPending || !::IsWindow(m_hWnd))
		return;

	m_bDetailsPanelHeightAdjustmentPending = true;
	PostMessage(UM_SHARED_FILES_ADJUST_DETAILS_PANEL);
}

void CSharedFilesWnd::PostShowFilesCountAsync()
{
	if (!::IsWindow(m_hWnd))
		return;
	if (InterlockedCompareExchange(&m_lShowFilesCountPending, 1, 0) != 0)
		return;
	if (!PostMessage(UM_SHOWFILESCOUNT, 0, 0))
		InterlockedExchange(&m_lShowFilesCountPending, 0);
}

void CSharedFilesWnd::PostMetadataUpdatedAsync()
{
	if (!::IsWindow(m_hWnd))
		return;
	if (InterlockedCompareExchange(&m_lMetadataUpdatedPending, 1, 0) != 0)
		return;
	if (!PostMessage(UM_METADATAUPDATED, 0, 0))
		InterlockedExchange(&m_lMetadataUpdatedPending, 0);
}

bool CSharedFilesWnd::PostAutoReloadSharedFilesAsync(WPARAM wpRequest)
{
	if (!::IsWindow(m_hWnd))
		return false;

	bool bPostMessage = false;
	{
		CSingleLock lock(&m_autoReloadRequestLock, TRUE);
		m_dwPendingAutoReloadFlags |= AutoReloadRequestToFlags(wpRequest);
		bPostMessage = InterlockedCompareExchange(&m_lAutoReloadSharedFilesPending, 1, 0) == 0;
	}
	if (bPostMessage && !PostMessage(UM_AUTO_RELOAD_SHARED_FILES, wpRequest, 0)) {
		InterlockedExchange(&m_lAutoReloadSharedFilesPending, 0);
		return false;
	}
	return true;
}

void CSharedFilesWnd::PostDeferredAutoReloadSharedFilesIfIdle()
{
	if (!::IsWindow(m_hWnd) || theApp.IsClosing() || theApp.sharedfiles == NULL || theApp.sharedfiles->HasActiveSharedFilesWork())
		return;

	DWORD dwDeferredReloadFlags = 0;
	bool bPostMessage = false;
	{
		CSingleLock lock(&m_autoReloadRequestLock, TRUE);
		dwDeferredReloadFlags = m_dwDeferredAutoReloadFlags;
		if (dwDeferredReloadFlags == 0)
			return;

		m_dwDeferredAutoReloadFlags = 0;
		m_dwPendingAutoReloadFlags |= dwDeferredReloadFlags;
		bPostMessage = InterlockedCompareExchange(&m_lAutoReloadSharedFilesPending, 1, 0) == 0;
	}

	if (bPostMessage && !PostMessage(UM_AUTO_RELOAD_SHARED_FILES, kAutoReloadDeferredDispatchRequest, 0)) {
		CSingleLock lock(&m_autoReloadRequestLock, TRUE);
		m_dwDeferredAutoReloadFlags |= dwDeferredReloadFlags;
		InterlockedExchange(&m_lAutoReloadSharedFilesPending, 0);
	}
}

void CSharedFilesWnd::AdjustDetailsPanelHeightForPageOverflow()
{
	if (!m_bDetailsVisible || !::IsWindow(m_dlgDetails.GetSafeHwnd()) || !::IsWindow(sharedfilesctrl.GetSafeHwnd()) || !::IsWindow(m_wndSplitter.GetSafeHwnd()) || GetDlgItem(IDC_SF_HIDESHOWDETAILS) == NULL)
		return;

	static const int s_iMaxAdjustmentPasses = 6;
	bool bAdjusted = false;
	for (int iPass = 0; iPass < s_iMaxAdjustmentPasses; ++iPass) {
		const int iOverflow = m_dlgDetails.GetRequiredHeightCompensation();
		if (iOverflow <= 0)
			break;

		CRect rcDetailDlg;
		m_dlgDetails.GetWindowRect(&rcDetailDlg);
		ScreenToClient(&rcDetailDlg);

		const int iMoveUp = min(iOverflow, rcDetailDlg.top);
		if (iMoveUp <= 0)
			break;

		rcDetailDlg.top -= iMoveUp;
		m_dlgDetails.MoveWindow(&rcDetailDlg, FALSE);
		UpdateDetailsPanelLayout();
		m_dlgDetails.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
		bAdjusted = true;
	}

	if (bAdjusted) {
		AddOrReplaceAnchor(this, m_dlgDetails, BOTTOM_LEFT, BOTTOM_RIGHT);
		RedrawDetailsPanelArea();
	}
}

void CSharedFilesWnd::OnBnClickedSfHideshowdetails()
{
	ShowDetailsPanel(!m_bDetailsVisible);
}

void CSharedFilesWnd::OnLvnItemchangedSflist(LPNMHDR, LRESULT *pResult)
{
	if (!sharedfilesctrl.IsSelectionRestoreInProgress()) {
		if (sharedfilesctrl.GetSelectedCount() > 1)
			PostSelectedFilesDetailsAsync();
		else
			ShowSelectedFilesDetails();
	}
	*pResult = 0;
}

void CSharedFilesWnd::OnShowWindow(BOOL bShow, UINT)
{
	if (bShow) {
		ShowSelectedFilesDetails(true);
		RequestDetailsPanelHeightAdjustment();
		PostAutoReloadSharedFilesAsync(3);
	}
}

LRESULT CSharedFilesWnd::OnAdjustDetailsPanelHeight(WPARAM, LPARAM)
{
	m_bDetailsPanelHeightAdjustmentPending = false;
	AdjustDetailsPanelHeightForPageOverflow();
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// CSharedFileDetailsModelessSheet
IMPLEMENT_DYNAMIC(CSharedFileDetailsModelessSheet, CListViewPropertySheet)

BEGIN_MESSAGE_MAP(CSharedFileDetailsModelessSheet, CListViewPropertySheet)
	ON_MESSAGE(UM_DATA_CHANGED, OnDataChanged)
	ON_WM_CREATE()
END_MESSAGE_MAP()

int CSharedFileDetailsModelessSheet::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	// skip CResizableSheet::OnCreate because we don't need the styles and stuff which are set there
	return CPropertySheet::OnCreate(lpCreateStruct);
}

bool NeedArchiveInfoPage(const CSimpleArray<CObject*> *paItems);
void UpdateFileDetailsPages(CListViewPropertySheet *pSheet, CResizablePage *pArchiveInfo
		, CResizablePage *pMediaInfo, CResizablePage *pFileLink);

CSharedFileDetailsModelessSheet::CSharedFileDetailsModelessSheet()
{
	m_psh.dwFlags &= ~PSH_HASHELP;
	m_psh.dwFlags |= PSH_MODELESS;

	m_wndStatistics.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndStatistics.m_psp.dwFlags |= PSP_USEICONID;
	m_wndStatistics.m_psp.pszIcon = _T("StatsDetail");
	m_wndStatistics.SetFiles(&m_aItems);
	AddPage(&m_wndStatistics);

	m_wndArchiveInfo.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndArchiveInfo.m_psp.dwFlags |= PSP_USEICONID;
	m_wndArchiveInfo.m_psp.pszIcon = _T("ARCHIVE_PREVIEW");
	m_wndArchiveInfo.SetReducedDialog();
	m_wndArchiveInfo.SetFiles(&m_aItems);

	m_wndMediaInfo.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndMediaInfo.m_psp.dwFlags |= PSP_USEICONID;
	m_wndMediaInfo.m_psp.pszIcon = _T("MEDIAINFO");
	m_wndMediaInfo.SetReducedDialog();
	m_wndMediaInfo.SetFiles(&m_aItems);
	if (NeedArchiveInfoPage(&m_aItems))
		AddPage(&m_wndArchiveInfo);
	else
		AddPage(&m_wndMediaInfo);

	m_wndFileLink.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndFileLink.m_psp.dwFlags |= PSP_USEICONID;
	m_wndFileLink.m_psp.pszIcon = _T("ED2KLINK");
	m_wndFileLink.SetReducedDialog();
	m_wndFileLink.SetFiles(&m_aItems);
	AddPage(&m_wndFileLink);

	m_wndMetaData.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndMetaData.m_psp.dwFlags |= PSP_USEICONID;
	m_wndMetaData.m_psp.pszIcon = _T("METADATA");
	m_wndMetaData.SetFiles(&m_aItems);
	if (thePrefs.IsExtControlsEnabled())
		AddPage(&m_wndMetaData);

}

BOOL CSharedFileDetailsModelessSheet::OnInitDialog()
{
	EnableStackedTabs(FALSE);
	BOOL bResult = CListViewPropertySheet::OnInitDialog();
	ModifyStyle(0, WS_CLIPCHILDREN);
	HighColorTab::UpdateImageList(*this);
	InitWindowStyles(this);

	m_tabDark.m_bClosable = false;
	m_tabDark.m_bAllowTabReordering = false;

	EnsureDarkModeTabControl();

	return bResult;
}

void CSharedFileDetailsModelessSheet::EnsureDarkModeTabControl()
{
	if (!IsDarkModeEnabled() || !::IsWindow(GetSafeHwnd()))
		return;

	HWND hTab = PropSheet_GetTabControl(m_hWnd);
	if (hTab == NULL)
		return;

	::SetWindowTheme(hTab, _T(""), _T(""));
	if (m_tabDark.GetSafeHwnd() == NULL)
		m_tabDark.SubclassWindow(hTab);

	::RedrawWindow(hTab, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
}

int CSharedFileDetailsModelessSheet::GetRequiredHeightCompensation()
{
	CPropertyPage *pActivePage = GetActivePage();
	int iOverflow = 0;

	// The archive preview is a resizable list page. Do not let its bottom anchored controls grow the host panel; the v1.4 compensation is only for fixed label/groupbox pages which can overflow with CJK or large fonts.
	if (pActivePage != &m_wndArchiveInfo) {
		iOverflow = GetPageBottomOverflow(pActivePage);
		if (iOverflow > 0)
			return iOverflow;
	}

	for (int iPage = 0; iPage < GetPageCount(); ++iPage) {
		CPropertyPage *pPage = GetPage(iPage);
		if (pPage == pActivePage || pPage == &m_wndArchiveInfo)
			continue;
		const int iPageOverflow = GetPageBottomOverflow(pPage);
		if (iPageOverflow > iOverflow)
			iOverflow = iPageOverflow;
	}
	return iOverflow;
}

void  CSharedFileDetailsModelessSheet::SetFiles(CTypedPtrList<CPtrList, CShareableFile*> &aFiles)
{
	m_aItems.RemoveAll();
	for (POSITION pos = aFiles.GetHeadPosition(); pos != NULL;)
		m_aItems.Add(aFiles.GetNext(pos));
	ChangedData();
}

void CSharedFileDetailsModelessSheet::Localize()
{
	m_wndStatistics.Localize();
	SetTabTitle(_T("SF_STATISTICS"), &m_wndStatistics, this);
	m_wndFileLink.Localize();
	SetTabTitle(_T("SW_LINK"), &m_wndFileLink, this);
	m_wndArchiveInfo.Localize();
	SetTabTitle(_T("CONTENT_INFO"), &m_wndArchiveInfo, this);
	m_wndMediaInfo.Localize();
	SetTabTitle(_T("CONTENT_INFO"), &m_wndMediaInfo, this);
	m_wndMetaData.Localize();
	SetTabTitle(_T("META_DATA"), &m_wndMetaData, this);
}

LRESULT CSharedFileDetailsModelessSheet::OnDataChanged(WPARAM, LPARAM)
{
	//When using up/down keys in shared files list, "Content" tab grabs focus on archives
	CWnd *pFocused = GetFocus();
	UpdateFileDetailsPages(this, &m_wndArchiveInfo, &m_wndMediaInfo, &m_wndFileLink);
	CSharedFilesWnd *pParent = DYNAMIC_DOWNCAST(CSharedFilesWnd, GetParent());
	if (pParent != NULL)
		pParent->RequestDetailsPanelHeightAdjustment();
	if (pFocused) //try to stay in file list
		pFocused->SetFocus();
	return TRUE;
}

void CSharedFilesWnd::OnNMClickHistorylist(NMHDR* pNMHDR, LRESULT* pResult) {
	OnLvnItemActivateHistorylist(pNMHDR, pResult);
	*pResult = 0;
}

void CSharedFilesWnd::OnLvnItemActivateHistorylist(NMHDR* /*pNMHDR*/, LRESULT* /*pResult*/)
{
	ShowSelectedFilesDetails(false);
}

void CSharedFilesWnd::OnLvnItemchangedHlist(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	ShowSelectedFilesDetails(false);
	*pResult = 0;
}

LRESULT CSharedFilesWnd::OnShowFilesCount(WPARAM, LPARAM)
{
	MSG msg;
	while (::PeekMessage(&msg, m_hWnd, UM_SHOWFILESCOUNT, UM_SHOWFILESCOUNT, PM_REMOVE)) {
	}
	InterlockedExchange(&m_lShowFilesCountPending, 0);
	sharedfilesctrl.ShowFilesCount();
	return 0;
}

LRESULT CSharedFilesWnd::OnMetadataUpdated(WPARAM, LPARAM)
{
	MSG msg;
	while (::PeekMessage(&msg, m_hWnd, UM_METADATAUPDATED, UM_METADATAUPDATED, PM_REMOVE)) {
	}
	InterlockedExchange(&m_lMetadataUpdatedPending, 0);
	sharedfilesctrl.ReloadList(true, LSF_SELECTION);
	return 0;
}
