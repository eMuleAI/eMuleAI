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
#include "MemDC.h"
#include "MuleListCtrl.h"
#include "Ini2.h"
#include "SharedFilesCtrl.h"
#include "SearchListCtrl.h"
#include "KadContactListCtrl.h"
#include "KadSearchListCtrl.h"
#include "DownloadListCtrl.h"
#include "UploadListCtrl.h"
#include "DownloadClientsCtrl.h"
#include "QueueListCtrl.h"
#include "ClientListCtrl.h"
#include "FriendListCtrl.h"
#include "ServerListCtrl.h"
#include "MenuCmds.h"
#include "OtherFunctions.h"
#include "ListViewSearchDlg.h"
#include <atlimage.h>
#include "Opcodes.h"
#include "UserMsgs.h"
#include "Log.h"
#include <limits.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern UINT g_uMainThreadId;

#define MAX_SORTORDERHISTORY 4
#define MLC_BLEND(A, B, X) (((A) + (B) * ((X)-1) + (((X)+1)/2)) / (X))

#define MLC_RGBBLEND(A, B, X) (                   \
	RGB(MLC_BLEND(GetRValue(A), GetRValue(B), X), \
	MLC_BLEND(GetGValue(A), GetGValue(B), X),     \
	MLC_BLEND(GetBValue(A), GetBValue(B), X))     \
)

#define MLC_IDC_MENU	4875
#define MLC_IDC_UPDATE	(MLC_IDC_MENU - 1)
#ifndef IDC_HAND
#define IDC_HAND MAKEINTRESOURCE(32649)
#endif

//used for very slow assertions
#define MLC_ASSERT(f)	((void)0)

//////////////////////////////////
// CMuleListCtrl

// Be careful with these offsets, they are supposed to match *exactly* the Windows built-in metric.
// If it does not match that value, column auto-sizing (double clicking on header divider) will
// give inaccurate results.
const int CMuleListCtrl::sm_iIconOffset = 4;	// Offset from left window border to icon (of 1st column)
const int CMuleListCtrl::sm_iLabelOffset = 2;	// Offset between right icon border and item text (of 1st column)
const int CMuleListCtrl::sm_iSubItemInset = 4;	// Offset from left and right column border to item text

IMPLEMENT_DYNAMIC(CMuleListCtrl, CListCtrl)

void CMuleListCtrl::SAsyncRedrawRange::Merge(int iFirst, int iLast)
{
	if (iFirst < 0 || iLast < iFirst) {
		MarkFull();
		return;
	}
	if (m_bFull)
		return;
	if (m_iFirst < 0 || iFirst < m_iFirst)
		m_iFirst = iFirst;
	if (iLast > m_iLast)
		m_iLast = iLast;
}

void CMuleListCtrl::BeginListModelGeneration(LONG lModelGeneration, bool /*bClearRows*/)
{
	const LONG lCurrentGeneration = InterlockedCompareExchange(&m_lListModelGeneration, 0, 0);
	if (lModelGeneration != 0) {
		if (lCurrentGeneration != 0 && lModelGeneration < lCurrentGeneration)
			return;
		InterlockedExchange(&m_lListModelGeneration, lModelGeneration);
		return;
	}

	LONG lNewGeneration = InterlockedIncrement(&m_lListModelGeneration);
	if (lNewGeneration <= 0)
		InterlockedExchange(&m_lListModelGeneration, 1);
}


namespace
{
constexpr int MULELISTCTRL_PERSISTENT_INFOTIP_OFFSET_X = 0;
constexpr int MULELISTCTRL_PERSISTENT_INFOTIP_OFFSET_Y = 2;
constexpr UINT_PTR MULELISTCTRL_PERSISTENT_INFOTIP_TOOL_ID = 1;
constexpr DWORD MULELISTCTRL_PERSISTENT_INFOTIP_MIN_VISIBLE_MS = 350;
constexpr UINT_PTR MULELISTCTRL_PERSISTENT_INFOTIP_LEAVE_TIMER_ID = 0x4D4C4950;
constexpr UINT MULELISTCTRL_PERSISTENT_INFOTIP_LEAVE_RECHECK_MS = 75;
constexpr int MULELISTCTRL_OPERATION_OVERLAY_MAX_WIDTH = 680;
constexpr int MULELISTCTRL_OPERATION_OVERLAY_MIN_WIDTH = 420;
constexpr int MULELISTCTRL_OPERATION_OVERLAY_HEIGHT = 76;
constexpr int MULELISTCTRL_OPERATION_OVERLAY_BOTTOM_MARGIN = 18;
constexpr UINT MULELISTCTRL_OPERATION_OVERLAY_IMMEDIATE_MIN_ITEMS = 3;
constexpr DWORD MULELISTCTRL_OPERATION_OVERLAY_DELAY_MS = 1200;
constexpr UINT_PTR MULELISTCTRL_OPERATION_OVERLAY_TIMER_ID = 0x4D4C4F50;
constexpr UINT MULELISTCTRL_OPERATION_OVERLAY_ANIMATION_MS = 250;
}

class COperationOverlayWnd : public CWnd
{
public:
	COperationOverlayWnd()
		: m_pOwner(NULL)
		, m_bAnimationTimerActive(false)
		, m_sizeWindowRgn(0, 0)
	{
	}

	bool CreateOverlay(CMuleListCtrl* pOwner)
	{
		if (pOwner == NULL)
			return false;

		m_pOwner = pOwner;
		if (::IsWindow(m_hWnd))
			return true;

		CString strClassName = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
		return CreateEx(0, strClassName, NULL, WS_CHILD | WS_CLIPSIBLINGS, CRect(0, 0, 0, 0), pOwner, 0) != FALSE;
	}

	void UpdateFromOwner()
	{
		if (m_pOwner == NULL || !::IsWindow(m_hWnd))
			return;

		CRect rcOverlay;
		if (!m_pOwner->GetOperationOverlayRect(rcOverlay)) {
			ShowWindow(SW_HIDE);
			return;
		}

		CRect rcCurrent;
		GetWindowRect(&rcCurrent);
		m_pOwner->ScreenToClient(&rcCurrent);
		const bool bNeedsMove = !IsWindowVisible() || rcCurrent.left != rcOverlay.left || rcCurrent.top != rcOverlay.top || rcCurrent.right != rcOverlay.right || rcCurrent.bottom != rcOverlay.bottom;
		const CSize sizeOverlay(rcOverlay.Width(), rcOverlay.Height());
		const bool bNeedsRgn = m_sizeWindowRgn.cx != sizeOverlay.cx || m_sizeWindowRgn.cy != sizeOverlay.cy;
		if (bNeedsMove)
			SetWindowPos(&CWnd::wndTop, rcOverlay.left, rcOverlay.top, rcOverlay.Width(), rcOverlay.Height(), SWP_NOACTIVATE | SWP_SHOWWINDOW);
		if (bNeedsRgn) {
			CRgn rgnPanel;
			if (rgnPanel.CreateRoundRectRgn(0, 0, rcOverlay.Width() + 1, rcOverlay.Height() + 1, 14, 14)) {
				SetWindowRgn((HRGN)rgnPanel.Detach(), TRUE);
				m_sizeWindowRgn = sizeOverlay;
			}
		}
		if (bNeedsMove || bNeedsRgn)
			Invalidate(FALSE);
	}

	void EnsureAnimationTimer()
	{
		if (!::IsWindow(m_hWnd) || m_bAnimationTimerActive)
			return;

		SetTimer(MULELISTCTRL_OPERATION_OVERLAY_TIMER_ID, MULELISTCTRL_OPERATION_OVERLAY_ANIMATION_MS, NULL);
		m_bAnimationTimerActive = true;
	}

protected:
	DECLARE_MESSAGE_MAP()

	afx_msg void OnPaint()
	{
		CPaintDC dc(this);
		CRect rcClient;
		GetClientRect(rcClient);
		CMemoryDC dcMem(&dc, rcClient);
		if (m_pOwner != NULL) {
			CRect rcOverlay;
			if (m_pOwner->GetOperationOverlayRect(rcOverlay)) {
				dcMem.SetWindowOrg(rcOverlay.left, rcOverlay.top);
				m_pOwner->DrawOperationOverlay(&dcMem);
				dcMem.SetWindowOrg(0, 0);
			}
		}
		dcMem.Flush();
	}

	afx_msg BOOL OnEraseBkgnd(CDC*)
	{
		return TRUE;
	}

	afx_msg void OnTimer(UINT_PTR nIDEvent)
	{
		if (nIDEvent == MULELISTCTRL_OPERATION_OVERLAY_TIMER_ID && m_pOwner != NULL) {
			if (!m_pOwner->m_operationOverlay.bVisible || m_pOwner->m_operationOverlay.bSuppressed) {
				StopAnimationTimer();
				return;
			}
			m_pOwner->m_operationOverlay.uAnimationPhase = (m_pOwner->m_operationOverlay.uAnimationPhase + 24) % 1536;
			Invalidate(FALSE);
			return;
		}

		CWnd::OnTimer(nIDEvent);
	}

	afx_msg void OnDestroy()
	{
		StopAnimationTimer();
		CWnd::OnDestroy();
	}

	afx_msg void OnMouseMove(UINT nFlags, CPoint point)
	{
		UpdateCancelHot(point);
		CWnd::OnMouseMove(nFlags, point);
	}

	afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM)
	{
		if (m_pOwner != NULL && m_pOwner->m_operationOverlay.bCancelHot) {
			m_pOwner->m_operationOverlay.bCancelHot = false;
			Invalidate(FALSE);
		}
		return 0;
	}

	afx_msg void OnLButtonDown(UINT nFlags, CPoint point)
	{
		if (m_pOwner != NULL) {
			CRect rcCancel;
			CRect rcOverlay;
			if (m_pOwner->GetOperationOverlayRect(rcOverlay)) {
				CPoint ptOwner(point.x + rcOverlay.left, point.y + rcOverlay.top);
				if (m_pOwner->GetOperationOverlayCancelRect(rcCancel) && rcCancel.PtInRect(ptOwner)) {
					m_pOwner->OnOperationOverlayCancel();
					return;
				}
			}
		}

		CWnd::OnLButtonDown(nFlags, point);
	}

	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
	{
		CPoint point;
		::GetCursorPos(&point);
		ScreenToClient(&point);
		if (IsCancelHot(point)) {
			::SetCursor(::LoadCursor(NULL, IDC_HAND));
			return TRUE;
		}

		return CWnd::OnSetCursor(pWnd, nHitTest, message);
	}

private:
	bool IsCancelHot(CPoint point) const
	{
		if (m_pOwner == NULL || !m_pOwner->m_operationOverlay.bVisible || !m_pOwner->m_operationOverlay.bCanCancel)
			return false;

		CRect rcOverlay;
		CRect rcCancel;
		if (!m_pOwner->GetOperationOverlayRect(rcOverlay) || !m_pOwner->GetOperationOverlayCancelRect(rcCancel))
			return false;

		CPoint ptOwner(point.x + rcOverlay.left, point.y + rcOverlay.top);
		return rcCancel.PtInRect(ptOwner) != FALSE;
	}

	void UpdateCancelHot(CPoint point)
	{
		if (m_pOwner == NULL || !m_pOwner->m_operationOverlay.bCanCancel)
			return;

		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
		_TrackMouseEvent(&tme);

		const bool bCancelHot = IsCancelHot(point);
		if (m_pOwner->m_operationOverlay.bCancelHot != bCancelHot) {
			m_pOwner->m_operationOverlay.bCancelHot = bCancelHot;
			Invalidate(FALSE);
		}
	}

	void StopAnimationTimer()
	{
		if (!m_bAnimationTimerActive)
			return;

		KillTimer(MULELISTCTRL_OPERATION_OVERLAY_TIMER_ID);
		m_bAnimationTimerActive = false;
	}

	CMuleListCtrl* m_pOwner;
	bool m_bAnimationTimerActive;
	CSize m_sizeWindowRgn;
};

BEGIN_MESSAGE_MAP(COperationOverlayWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_WM_MOUSEMOVE()
	ON_MESSAGE(WM_MOUSELEAVE, OnMouseLeave)
	ON_WM_LBUTTONDOWN()
	ON_WM_SETCURSOR()
END_MESSAGE_MAP()

BEGIN_MESSAGE_MAP(CMuleListCtrl, CListCtrl)
	ON_NOTIFY_REFLECT(LVN_GETINFOTIP, OnLvnGetInfoTip)
	ON_WM_DRAWITEM()
	ON_WM_ERASEBKGND()
	ON_WM_KEYDOWN()
	ON_WM_MEASUREITEM_REFLECT()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_SETCURSOR()
	ON_WM_PAINT()
	ON_WM_SIZE()
	ON_WM_SHOWWINDOW()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_DESTROY()
	ON_WM_TIMER()
	ON_MESSAGE(WM_MOUSEHOVER, OnMouseHover)
	ON_MESSAGE(WM_MOUSELEAVE, OnMouseLeave)
	ON_MESSAGE(WM_MULELISTCTRL_INVALIDATE, OnAsyncInvalidate)
END_MESSAGE_MAP()

CMuleListCtrl::CMuleListCtrl(PFNLVCOMPARE pfnCompare, LPARAM iParamSort)
	: m_SortProc(pfnCompare)
	, m_dwParamSort(iParamSort)
	, m_crWindow()
	, m_crWindowText()
	, m_crWindowTextBk(m_crWindow)
	, m_crHighlight()
	, m_crHighlightText(m_crWindowText)
	, m_crGlow()
	, m_crFocusLine()
	, m_crNoHighlight()
	, m_crNoFocusLine()
	, m_lvcd()
	, m_bCustomDraw()
	, m_uIDAccel(IDR_LISTVIEW)
	, m_hAccel()
	, m_eUpdateMode(lazy)
	, m_iAutoSizeWidth(LVSCW_AUTOSIZE)
	, m_iFindDirection(1)
	, m_iFindColumn()
	, m_bGeneralPurposeFind()
	, m_bCanSearchInAllColumns()
	, m_bFindMatchCase()
	, m_aColumns()
	, m_iColumnsTracked()
	, m_iCurrentSortItem(-1)
	, m_atSortArrow()
	, m_iRedrawCount()
	, m_uBulkInsertDepth()
	, m_Params()
	, m_wndInfoTip()
	, m_wndPersistentInfoTip()
	, m_strPersistentInfoTipText()
	, m_PersistentInfoTipContext()
	, m_PendingInfoTipContext()
	, m_bPersistentInfoTipVisible()
	, m_bTrackingMouseHover()
	, m_bTrackingMouseLeave()
	, m_operationOverlay()
	, m_pOperationOverlayWnd()
	, m_liDefaultHiddenColumns()
	, m_iMaxSortHistory()
	, m_asyncRedrawRange()
	, m_bAsyncRedrawMessagePending(false)
	, m_lListModelGeneration(0)
	, m_dwLastAsyncSortTick(0)

{
}

CMuleListCtrl::~CMuleListCtrl()
{
	BeginListModelGeneration(0, true);
	ShutdownUpdateThread();
	m_wndInfoTip.CleanupWindow();
	HidePersistentInfoTip(true);
	CancelOperationOverlayDelayTimer();
	DestroyOperationOverlayWindow();
	delete[] m_aColumns;
	if (m_imlHeaderCtrl.m_hImageList != NULL)
		m_imlHeaderCtrl.DeleteImageList();
}

void CMuleListCtrl::OnDestroy()
{
	BeginListModelGeneration(0, true);
	ShutdownUpdateThread();
	m_wndInfoTip.CleanupWindow();
	HidePersistentInfoTip(true);
	CancelOperationOverlayDelayTimer();
	DestroyOperationOverlayWindow();

	// Disassociate and destroy header imagelist to avoid GDI leaks and stale handles
	if (m_imlHeaderCtrl.m_hImageList != NULL) {
		CHeaderCtrl* pHeader = GetHeaderCtrl();
		if (pHeader)
			pHeader->SetImageList(NULL); // Header must not keep a pointer to a soon-to-be-destroyed image list
		m_imlHeaderCtrl.DeleteImageList(); // Deterministically release HIMAGELIST
	}
	CListCtrl::OnDestroy();
}

void CMuleListCtrl::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CListCtrl::OnShowWindow(bShow, nStatus);
	if (!bShow) {
		BeginListModelGeneration(0, true);
		DestroyOperationOverlayWindow();
	} else
		UpdateOperationOverlayWindow();
}


void CMuleListCtrl::ShutdownUpdateThread()
{
	CSingleLock lock(&m_asyncRedrawLock, TRUE);
	m_bAsyncRedrawMessagePending = false;
	m_asyncRedrawRange.Clear();
}


// Post to self to guarantee UI-thread execution
void CMuleListCtrl::RequestInvalidateAsync() const
{
	if (::IsWindow(m_hWnd))
		::PostMessage(m_hWnd, WM_MULELISTCTRL_INVALIDATE, 0, 0);
}


CString CMuleListCtrl::MakeListSortKey(const CString& strSortText, bool bUndefined)
{
	CString strKey;
	strKey.Format(_T("%c:%s"), bUndefined ? _T('1') : _T('0'), static_cast<LPCTSTR>(strSortText));
	return strKey;
}


int CMuleListCtrl::CompareListSortKeys(const CString& strSortKey1, const CString& strSortKey2)
{
	const bool bUndefined1 = strSortKey1.GetLength() >= 2 && strSortKey1[0] == _T('1') && strSortKey1[1] == _T(':');
	const bool bUndefined2 = strSortKey2.GetLength() >= 2 && strSortKey2[0] == _T('1') && strSortKey2[1] == _T(':');
	if (bUndefined1 || bUndefined2) {
		if (bUndefined1 && bUndefined2)
			return 5;
		return bUndefined1 ? 6 : 4;
	}
	CString strValue1(strSortKey1);
	CString strValue2(strSortKey2);
	if (strValue1.GetLength() >= 2 && strValue1[1] == _T(':'))
		strValue1 = strValue1.Mid(2);
	if (strValue2.GetLength() >= 2 && strValue2[1] == _T(':'))
		strValue2 = strValue2.Mid(2);
	return sgn(strValue1.CompareNoCase(strValue2));
}

int CMuleListCtrl::ApplyListSortDirection(int iSortResult, bool bDescending)
{
	if (iSortResult > 3)
		return iSortResult - 5;
	return bDescending ? -iSortResult : iSortResult;
}


CMuleListCtrl::SVisibleItemRange CMuleListCtrl::GetVisibleItemRange() const
{
	if (!::IsWindow(m_hWnd))
		return SVisibleItemRange();
	const int iCount = const_cast<CMuleListCtrl*>(this)->GetItemCount();
	if (iCount <= 0)
		return SVisibleItemRange();
	int iFirst = const_cast<CMuleListCtrl*>(this)->GetTopIndex();
	if (iFirst < 0)
		iFirst = 0;
	int iPerPage = const_cast<CMuleListCtrl*>(this)->GetCountPerPage();
	if (iPerPage <= 0)
		iPerPage = iCount;
	int iLast = min(iCount - 1, iFirst + iPerPage);
	return SVisibleItemRange(iFirst, iLast);
}

bool CMuleListCtrl::IsAtBottom() const
{
	if (!::IsWindow(m_hWnd))
		return false;

	const int iItemCount = GetItemCount();
	if (iItemCount <= 0)
		return false;

	const int iTopIndex = const_cast<CMuleListCtrl*>(this)->GetTopIndex();
	if (iTopIndex < 0)
		return false;

	const int iRowsPerPage = const_cast<CMuleListCtrl*>(this)->GetCountPerPage();
	if (iRowsPerPage <= 0 || iItemCount <= iRowsPerPage)
		return false;

	CRect rcClient;
	CRect rcLastItem;
	const_cast<CMuleListCtrl*>(this)->GetClientRect(&rcClient);
	return const_cast<CMuleListCtrl*>(this)->GetItemRect(iItemCount - 1, &rcLastItem, LVIR_BOUNDS)
		&& rcLastItem.bottom > rcClient.top
		&& rcLastItem.top < rcClient.bottom;
}

void CMuleListCtrl::ScrollToTopIndex(int iTopIndex)
{
	const int iItemCount = GetItemCount();
	if (iItemCount <= 0 || !::IsWindow(m_hWnd))
		return;

	iTopIndex = max(0, min(iTopIndex, iItemCount - 1));
	const int iRowsPerPage = max(1, GetCountPerPage());
	const int iBottomIndex = min(iItemCount - 1, iTopIndex + iRowsPerPage - 1);
	EnsureVisible(iBottomIndex, FALSE);

	int iCurrentTop = GetTopIndex();
	if (iCurrentTop == iTopIndex)
		return;

	int iRowHeight = 0;
	CRect rcFirst;
	CRect rcSecond;
	if (iItemCount > 1 && GetItemRect(0, &rcFirst, LVIR_BOUNDS) && GetItemRect(1, &rcSecond, LVIR_BOUNDS))
		iRowHeight = rcSecond.top - rcFirst.top;
	if (iRowHeight <= 0) {
		CClientDC dc(this);
		CFont *pOldFont = GetFont() != NULL ? dc.SelectObject(GetFont()) : NULL;
		TEXTMETRIC tm = {};
		dc.GetTextMetrics(&tm);
		if (pOldFont != NULL)
			dc.SelectObject(pOldFont);
		iRowHeight = max(1, tm.tmHeight + 2);
	}

	if (iCurrentTop < 0)
		iCurrentTop = 0;
	if (iCurrentTop != iTopIndex)
		Scroll(CSize(0, (iTopIndex - iCurrentTop) * iRowHeight));

	if (GetTopIndex() != iTopIndex) {
		SCROLLINFO si = { sizeof(si) };
		if (GetScrollInfo(SB_VERT, &si, SIF_RANGE | SIF_PAGE | SIF_POS)) {
			int iMaxPos = si.nMax;
			if (si.nPage > 0)
				iMaxPos = max(si.nMin, si.nMax - static_cast<int>(si.nPage) + 1);
			const int iPos = max(si.nMin, min(iTopIndex, iMaxPos));
			SendMessage(WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, iPos), 0);
		}
	}
}


void CMuleListCtrl::SetItemCountAndKeepPageFilled(size_t itemCount, DWORD dwFlags)
{
	if (!::IsWindow(m_hWnd))
		return;

	const size_t uClampedCount = itemCount > static_cast<size_t>(INT_MAX) ? static_cast<size_t>(INT_MAX) : itemCount;
	const int iNewItemCount = static_cast<int>(uClampedCount);
	const int iOldItemCount = GetItemCount();
	const int iRowsPerPage = max(1, GetCountPerPage());
	int iDesiredTop = GetTopIndex();
	if (iDesiredTop < 0)
		iDesiredTop = 0;

	if (iNewItemCount <= 0) {
		SetItemCountEx(0, dwFlags);
		return;
	}

	const bool bWasAtBottom = iOldItemCount > 0 && iDesiredTop + iRowsPerPage >= iOldItemCount;
	if (bWasAtBottom || iDesiredTop + iRowsPerPage > iNewItemCount)
		iDesiredTop = max(0, iNewItemCount - iRowsPerPage);
	else if (iDesiredTop >= iNewItemCount)
		iDesiredTop = iNewItemCount - 1;

	SetItemCountEx(iNewItemCount, dwFlags);
	ScrollToTopIndex(iDesiredTop);
}

bool CMuleListCtrl::IsItemIndexVisible(int iIndex) const
{
	const SVisibleItemRange visibleRange = GetVisibleItemRange();
	return visibleRange.IsValid() && iIndex >= visibleRange.m_iFirst && iIndex <= visibleRange.m_iLast;
}

bool CMuleListCtrl::GetNextBackgroundItemRange(int& iNextIndex, int iMaxIndex, int iRowsPerSlice, int& iFirst, int& iLast) const
{
	iFirst = -1;
	iLast = -1;
	if (iRowsPerSlice <= 0 || iNextIndex < 0 || iNextIndex > iMaxIndex)
		return false;

	const SVisibleItemRange visibleRange = GetVisibleItemRange();
	if (visibleRange.IsValid()) {
		const int iVisibleFirst = max(0, min(visibleRange.m_iFirst, iMaxIndex));
		const int iVisibleLast = max(iVisibleFirst, min(visibleRange.m_iLast, iMaxIndex));
		if (iNextIndex >= iVisibleFirst && iNextIndex <= iVisibleLast)
			iNextIndex = iVisibleLast + 1;
		if (iNextIndex > iMaxIndex)
			return false;

		iFirst = iNextIndex;
		iLast = min(iMaxIndex, iFirst + iRowsPerSlice - 1);
		if (iFirst < iVisibleFirst && iLast >= iVisibleFirst)
			iLast = iVisibleFirst - 1;
	} else {
		iFirst = iNextIndex;
		iLast = min(iMaxIndex, iFirst + iRowsPerSlice - 1);
	}

	return iFirst >= 0 && iLast >= iFirst;
}

void CMuleListCtrl::RequestRowRedrawAsync(int iStart, int iEnd)
{
	if (theApp.GetBackendLifecycleState() >= CemuleApp::BackendLifecycleStoppingUiUpdates)
		return;
	if (!::IsWindow(m_hWnd))
		return;
	if (iStart < 0 || iEnd < iStart) {
		RequestFullRedrawAsync();
		return;
	}

	const SVisibleItemRange visibleRange = GetVisibleItemRange();
	if (visibleRange.IsValid()) {
		iStart = max(iStart, visibleRange.m_iFirst);
		iEnd = min(iEnd, visibleRange.m_iLast);
		if (iStart > iEnd)
			return;
	}

	bool bPostMessage = false;
	{
		CSingleLock lock(&m_asyncRedrawLock, TRUE);
		m_asyncRedrawRange.Merge(iStart, iEnd);
		if (!m_bAsyncRedrawMessagePending) {
			m_bAsyncRedrawMessagePending = true;
			bPostMessage = true;
		}
	}
	if (bPostMessage && ::PostMessage(m_hWnd, WM_MULELISTCTRL_INVALIDATE, 0, 0) == FALSE) {
		CSingleLock lock(&m_asyncRedrawLock, TRUE);
		m_bAsyncRedrawMessagePending = false;
	}
}

void CMuleListCtrl::RequestFullRedrawAsync()
{
	if (theApp.GetBackendLifecycleState() >= CemuleApp::BackendLifecycleStoppingUiUpdates)
		return;
	if (!::IsWindow(m_hWnd))
		return;
	bool bPostMessage = false;
	{
		CSingleLock lock(&m_asyncRedrawLock, TRUE);
		m_asyncRedrawRange.MarkFull();
		if (!m_bAsyncRedrawMessagePending) {
			m_bAsyncRedrawMessagePending = true;
			bPostMessage = true;
		}
	}
	if (bPostMessage && ::PostMessage(m_hWnd, WM_MULELISTCTRL_INVALIDATE, 0, 0) == FALSE) {
		CSingleLock lock(&m_asyncRedrawLock, TRUE);
		m_bAsyncRedrawMessagePending = false;
	}
}

void CMuleListCtrl::MaintainSortOrderAfterUpdate()
{
	if (!::IsWindow(m_hWnd) || GetSortItem() == -1 || m_SortProc == NULL || (GetStyle() & LVS_OWNERDATA) != 0)
		return;

	SortItems(static_cast<DWORD>(m_dwParamSort));
}

void CMuleListCtrl::MaintainSortOrderAfterThrottledUpdate()
{
	if (!ShouldMaintainSortOrderOnUpdate())
		return;

	const DWORD dwCurrentTime = GetTickCount();
	if (dwCurrentTime - m_dwLastAsyncSortTick <= static_cast<DWORD>(thePrefs.GetUITweaksListUpdatePeriod()))
		return;

	m_dwLastAsyncSortTick = dwCurrentTime;
	MaintainSortOrderAfterUpdate();
}

// Post a message to UI thread to invalidate safely
LRESULT CMuleListCtrl::OnAsyncInvalidate(WPARAM, LPARAM)
{
	if (theApp.GetBackendLifecycleState() >= CemuleApp::BackendLifecycleStoppingUiUpdates) {
		CSingleLock lock(&m_asyncRedrawLock, TRUE);
		m_bAsyncRedrawMessagePending = false;
		m_asyncRedrawRange.Clear();
		return 0;
	}
	if (!::IsWindow(m_hWnd)) {
		CSingleLock lock(&m_asyncRedrawLock, TRUE);
		m_bAsyncRedrawMessagePending = false;
		m_asyncRedrawRange.Clear();
		return 0;
	}

	bool bFullRedraw = false;
	int iStart = -1;
	int iEnd = -1;
	{
		CSingleLock lock(&m_asyncRedrawLock, TRUE);
		m_bAsyncRedrawMessagePending = false;
		bFullRedraw = m_asyncRedrawRange.m_bFull;
		iStart = m_asyncRedrawRange.m_iFirst;
		iEnd = m_asyncRedrawRange.m_iLast;
		m_asyncRedrawRange.Clear();
	}

	if (!bFullRedraw && iStart >= 0 && iEnd >= iStart) {
		const int iItemCount = GetItemCount();
		if (iItemCount > 0) {
			iEnd = min(iEnd, iItemCount - 1);
			if (iStart <= iEnd) {
				RedrawItems(iStart, iEnd);
				MaintainSortOrderAfterThrottledUpdate();
				return 0;
			}
		}
	}

	Invalidate(FALSE);
	MaintainSortOrderAfterThrottledUpdate();
	return 0;
}

bool CMuleListCtrl::ShouldShowPersistentInfoTip(const SPersistentInfoTipContext& context)
{
	return context.iSubItem == 0 && context.dwItemKey != 0;
}

bool CMuleListCtrl::GetPersistentInfoTipText(const SPersistentInfoTipContext& /*context*/, CString& strText)
{
	strText.Empty();
	return false;
}

int CMuleListCtrl::GetDefaultPersistentInfoTipExtraLeftPadding(const SPersistentInfoTipContext& /*context*/) const
{
	return 0;
}

bool CMuleListCtrl::GetDefaultPersistentInfoTipText(const SPersistentInfoTipContext& context, CString& strText)
{
	strText.Empty();
	if (context.iItem < 0 || context.iSubItem < 0)
		return false;

	strText = GetItemText(context.iItem, context.iSubItem);
	return !strText.IsEmpty() && IsDefaultPersistentInfoTipTruncated(context, strText);
}

bool CMuleListCtrl::TryGetExplicitPersistentInfoTipText(const SPersistentInfoTipContext& context, CString& strText)
{
	strText.Empty();
	if (!UsePersistentInfoTips() || !ShouldShowPersistentInfoTip(context) || !IsPersistentInfoTipContextCurrent(context))
		return false;

	return GetPersistentInfoTipText(context, strText) && !strText.IsEmpty();
}

bool CMuleListCtrl::TryGetPersistentInfoTipForContext(const SPersistentInfoTipContext& context, CString& strText, bool* pbExplicitTip)
{
	strText.Empty();
	if (pbExplicitTip != NULL)
		*pbExplicitTip = false;
	if (!IsPersistentInfoTipContextCurrent(context))
		return false;

	if (TryGetExplicitPersistentInfoTipText(context, strText)) {
		if (pbExplicitTip != NULL)
			*pbExplicitTip = true;
		return true;
	}

	return GetDefaultPersistentInfoTipText(context, strText);
}

bool CMuleListCtrl::GetDefaultPersistentInfoTipRect(const SPersistentInfoTipContext& context, CRect& rcText) const
{
	rcText.SetRectEmpty();
	if (context.iItem < 0 || context.iSubItem < 0)
		return false;

	CMuleListCtrl* pThis = const_cast<CMuleListCtrl*>(this);
	if (context.iSubItem == 0) {
		if (!pThis->GetItemRect(context.iItem, &rcText, LVIR_LABEL))
			return false;
	} else if (!pThis->GetSubItemRect(context.iItem, context.iSubItem, LVIR_LABEL, rcText))
		return false;

	if (context.iSubItem != 0)
		rcText.DeflateRect(sm_iSubItemInset, 0);
	rcText.left += GetDefaultPersistentInfoTipExtraLeftPadding(context);

	return rcText.Width() > 0 && rcText.Height() > 0;
}

bool CMuleListCtrl::IsDefaultPersistentInfoTipTruncated(const SPersistentInfoTipContext& context, const CString& strText)
{
	if (strText.IsEmpty())
		return false;

	CRect rcText;
	if (!GetDefaultPersistentInfoTipRect(context, rcText))
		return false;

	CDC* pDC = GetDC();
	if (pDC == NULL)
		return false;

	CFont* pOldFont = NULL;
	CFont* pFont = GetFont();
	if (pFont != NULL)
		pOldFont = pDC->SelectObject(pFont);

	const CSize sizeText = pDC->GetTextExtent(strText);
	if (pOldFont != NULL)
		pDC->SelectObject(pOldFont);
	ReleaseDC(pDC);

	return sizeText.cx > rcText.Width();
}

bool CMuleListCtrl::TryGetPersistentInfoTipContext(CPoint point, SPersistentInfoTipContext& context)
{
	context = SPersistentInfoTipContext();

	LVHITTESTINFO hti = {};
	hti.pt = point;
	if (SubItemHitTest(&hti) < 0 || hti.iItem < 0)
		return false;
	if (hti.iItem >= GetItemCount())
		return false;

	if (!GetItemRect(hti.iItem, &context.rcItem, LVIR_BOUNDS))
		return false;

	context.iItem = hti.iItem;
	context.iSubItem = hti.iSubItem;
	context.dwItemKey = reinterpret_cast<DWORD_PTR>(GetItemObject(hti.iItem));
	return true;
}

bool CMuleListCtrl::TryGetPersistentInfoTip(CPoint point, SPersistentInfoTipContext& context, CString* pstrText)
{
	SPersistentInfoTipContext resolvedContext;
	if (!TryGetPersistentInfoTipContext(point, resolvedContext))
		return false;

	if (pstrText != NULL) {
		CString strText;
		bool bExplicitTip = false;
		if (!TryGetPersistentInfoTipForContext(resolvedContext, strText, &bExplicitTip))
			return false;
		resolvedContext.bExplicitTip = bExplicitTip;
		*pstrText = strText;
	}

	context = resolvedContext;
	return true;
}

void CMuleListCtrl::EnsureThemeAwareInfoTipCtrl()
{
	if (!::IsWindow(m_hWnd))
		return;

	CToolTipCtrl* pToolTip = GetToolTips();
	if (pToolTip == NULL || pToolTip->GetSafeHwnd() == NULL)
		return;

	CWnd* pPermanentToolTipWnd = CWnd::FromHandlePermanent(pToolTip->GetSafeHwnd());
	if (pPermanentToolTipWnd != NULL && pPermanentToolTipWnd->IsKindOf(RUNTIME_CLASS(CToolTipCtrlX)))
		return;

	if (m_wndInfoTip.GetSafeHwnd() == pToolTip->GetSafeHwnd())
		return;

	m_wndInfoTip.CleanupWindow();
	if (!m_wndInfoTip.SubclassWindow(pToolTip->GetSafeHwnd()))
		return;

	pToolTip->ModifyStyle(0, TTS_NOPREFIX);
	pToolTip->SetDelayTime(TTDT_AUTOPOP, SEC2MS(20));
	pToolTip->SetDelayTime(TTDT_INITIAL, SEC2MS(thePrefs.GetToolTipDelay()));
	pToolTip->SendMessage(TTM_POP);
	pToolTip->Activate(FALSE);
}

void CMuleListCtrl::EnsurePersistentInfoTipCtrl()
{
	if (m_wndPersistentInfoTip.GetSafeHwnd() != NULL || !::IsWindow(m_hWnd))
		return;

	if (!m_wndPersistentInfoTip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX))
		return;

	m_wndPersistentInfoTip.SetFileIconToolTip(true);
	m_wndPersistentInfoTip.SetDelayTime(TTDT_AUTOPOP, SEC2MS(20));
	m_wndPersistentInfoTip.SetMinimumVisibleTime(MULELISTCTRL_PERSISTENT_INFOTIP_MIN_VISIBLE_MS);

	TOOLINFO ti = {};
	ti.cbSize = sizeof(ti);
	ti.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;
	ti.hwnd = m_hWnd;
	ti.uId = MULELISTCTRL_PERSISTENT_INFOTIP_TOOL_ID;
	ti.lpszText = LPSTR_TEXTCALLBACK;
	m_wndPersistentInfoTip.SendMessage(TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&ti));
}

void CMuleListCtrl::StartPersistentInfoTipLeaveTimer()
{
	if (::IsWindow(m_hWnd))
		SetTimer(MULELISTCTRL_PERSISTENT_INFOTIP_LEAVE_TIMER_ID, MULELISTCTRL_PERSISTENT_INFOTIP_LEAVE_RECHECK_MS, NULL);
}

void CMuleListCtrl::StopPersistentInfoTipLeaveTimer()
{
	if (::IsWindow(m_hWnd))
		KillTimer(MULELISTCTRL_PERSISTENT_INFOTIP_LEAVE_TIMER_ID);
}

void CMuleListCtrl::EnsurePersistentInfoTipMouseLeaveTracking()
{
	if (!::IsWindow(m_hWnd) || !m_bPersistentInfoTipVisible || m_bTrackingMouseLeave)
		return;

	TRACKMOUSEEVENT track = {};
	track.cbSize = sizeof(track);
	track.dwFlags = TME_LEAVE;
	track.hwndTrack = m_hWnd;
	if (::TrackMouseEvent(&track))
		m_bTrackingMouseLeave = true;
}

CPoint CMuleListCtrl::GetPersistentInfoTipScreenPosition(const SPersistentInfoTipContext& context) const
{
	CRect rcAnchor = !context.rcHotArea.IsRectEmpty() ? context.rcHotArea : context.rcItem;
	CPoint ptScreen(rcAnchor.left, rcAnchor.bottom);
	ClientToScreen(&ptScreen);
	ptScreen.Offset(MULELISTCTRL_PERSISTENT_INFOTIP_OFFSET_X, MULELISTCTRL_PERSISTENT_INFOTIP_OFFSET_Y);
	return ptScreen;
}

bool CMuleListCtrl::IsSamePersistentInfoTipTarget(const SPersistentInfoTipContext& left, const SPersistentInfoTipContext& right) const
{
	if (left.dwItemKey != 0 || right.dwItemKey != 0) {
		if (left.dwItemKey != right.dwItemKey)
			return false;
	} else if (left.iItem != right.iItem)
		return false;

	if (right.bExplicitTip)
		return true;

	return left.iSubItem == right.iSubItem;
}

bool CMuleListCtrl::IsPersistentInfoTipContextCurrent(const SPersistentInfoTipContext& context) const
{
	if (!::IsWindow(m_hWnd) || context.iItem < 0 || context.iSubItem < 0)
		return false;

	CMuleListCtrl* pThis = const_cast<CMuleListCtrl*>(this);
	if (context.iItem >= pThis->GetItemCount())
		return false;

	const DWORD_PTR dwCurrentItemKey = reinterpret_cast<DWORD_PTR>(pThis->GetItemObject(context.iItem));
	if (context.dwItemKey != 0 || dwCurrentItemKey != 0)
		return context.dwItemKey == dwCurrentItemKey;

	return true;
}

bool CMuleListCtrl::ResolvePersistentInfoTipHotArea(SPersistentInfoTipContext& context) const
{
	if (GetDefaultPersistentInfoTipRect(context, context.rcHotArea))
		return true;

	if (context.bExplicitTip) {
		context.rcHotArea = context.rcItem;
		return !context.rcHotArea.IsRectEmpty();
	}

	return false;
}

bool CMuleListCtrl::IsPointWithinPersistentInfoTipHotArea(CPoint point, const SPersistentInfoTipContext& context) const
{
	SPersistentInfoTipContext resolvedContext = context;
	if (resolvedContext.rcHotArea.IsRectEmpty() && !ResolvePersistentInfoTipHotArea(resolvedContext))
		return false;

	return resolvedContext.rcHotArea.PtInRect(point) != FALSE;
}

bool CMuleListCtrl::IsCursorOverPersistentInfoTipTarget()
{
	if (!m_bPersistentInfoTipVisible)
		return false;

	CPoint ptCursor;
	if (!::GetCursorPos(&ptCursor))
		return false;

	ScreenToClient(&ptCursor);
	return IsPointWithinPersistentInfoTipHotArea(ptCursor, m_PersistentInfoTipContext);
}

bool CMuleListCtrl::IsCursorOverPersistentInfoTipWindow() const
{
	if (!m_bPersistentInfoTipVisible || m_wndPersistentInfoTip.GetSafeHwnd() == NULL)
		return false;

	CPoint ptCursor;
	if (!::GetCursorPos(&ptCursor))
		return false;

	CRect rcToolTip;
	m_wndPersistentInfoTip.GetWindowRect(&rcToolTip);
	return rcToolTip.PtInRect(ptCursor) != FALSE;
}

bool CMuleListCtrl::ShouldKeepPersistentInfoTipVisibleOnMouseLeave()
{
	return IsCursorOverPersistentInfoTipTarget() || IsCursorOverPersistentInfoTipWindow();
}

void CMuleListCtrl::ShowPersistentInfoTip(const SPersistentInfoTipContext& context, const CString& strText)
{
	if (!IsPersistentInfoTipContextCurrent(context))
		return;

	EnsurePersistentInfoTipCtrl();
	if (m_wndPersistentInfoTip.GetSafeHwnd() == NULL)
		return;

	StopPersistentInfoTipLeaveTimer();

	SPersistentInfoTipContext resolvedContext = context;
	if (!ResolvePersistentInfoTipHotArea(resolvedContext))
		resolvedContext.rcHotArea = resolvedContext.rcItem;

	const bool bTooltipWindowVisible = (m_wndPersistentInfoTip.GetSafeHwnd() != NULL && ::IsWindowVisible(m_wndPersistentInfoTip.GetSafeHwnd()) != FALSE);
	const bool bSameTarget = m_bPersistentInfoTipVisible && bTooltipWindowVisible && IsSamePersistentInfoTipTarget(resolvedContext, m_PersistentInfoTipContext);
	if (bSameTarget)
		return;

	m_strPersistentInfoTipText = strText;
	m_wndPersistentInfoTip.UpdateTipText(m_strPersistentInfoTipText, this, MULELISTCTRL_PERSISTENT_INFOTIP_TOOL_ID);

	TOOLINFO ti = {};
	ti.cbSize = sizeof(ti);
	ti.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;
	ti.hwnd = m_hWnd;
	ti.uId = MULELISTCTRL_PERSISTENT_INFOTIP_TOOL_ID;
	ti.lpszText = const_cast<LPTSTR>(static_cast<LPCTSTR>(m_strPersistentInfoTipText));

	const CPoint ptScreen = GetPersistentInfoTipScreenPosition(resolvedContext);
	m_wndPersistentInfoTip.SendMessage(TTM_TRACKPOSITION, 0, MAKELPARAM(ptScreen.x, ptScreen.y));
	if (!bSameTarget)
		m_wndPersistentInfoTip.SendMessage(TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&ti));
	m_wndPersistentInfoTip.SendMessage(TTM_UPDATE);

	EnsurePersistentInfoTipMouseLeaveTracking();

	m_PersistentInfoTipContext = resolvedContext;
	m_bPersistentInfoTipVisible = true;
}

bool CMuleListCtrl::TryGetInfoTipWindowText(HWND hWndInfoTip, CString& strText) const
{
	strText.Empty();
	if (!::IsWindow(hWndInfoTip))
		return false;

	const int iTextLength = ::GetWindowTextLength(hWndInfoTip);
	if (iTextLength <= 0)
		return false;

	LPTSTR pszBuffer = strText.GetBufferSetLength(iTextLength);
	const int iCopied = ::GetWindowText(hWndInfoTip, pszBuffer, iTextLength + 1);
	strText.ReleaseBuffer();
	return iCopied > 0;
}

bool CMuleListCtrl::HandleNativeInfoTipShow(HWND hWndInfoTip, LRESULT* pResult)
{
	if (m_bPersistentInfoTipVisible && IsCursorOverPersistentInfoTipWindow()) {
		::SetWindowPos(hWndInfoTip, NULL, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
		if (pResult != NULL)
			*pResult = TRUE;
		return true;
	}

	CPoint ptCursor;
	if (!::GetCursorPos(&ptCursor))
		return false;

	ScreenToClient(&ptCursor);

	SPersistentInfoTipContext context;
	if (!TryGetPersistentInfoTipContext(ptCursor, context))
		return false;
	if (!IsPersistentInfoTipContextCurrent(context))
		return false;

	CString strText;
	if (!TryGetExplicitPersistentInfoTipText(context, strText)) {
		if (!TryGetInfoTipWindowText(hWndInfoTip, strText) && !GetDefaultPersistentInfoTipText(context, strText))
			return false;
	} else
		context.bExplicitTip = true;

	ShowPersistentInfoTip(context, strText);
	::SetWindowPos(hWndInfoTip, NULL, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
	if (pResult != NULL)
		*pResult = TRUE;
	return true;
}

void CMuleListCtrl::ResetPersistentInfoTipState()
{
	m_PersistentInfoTipContext = SPersistentInfoTipContext();
	m_strPersistentInfoTipText.Empty();
}

void CMuleListCtrl::ResetPendingPersistentInfoTip()
{
	m_PendingInfoTipContext = SPersistentInfoTipContext();
	m_bTrackingMouseHover = false;
}

void CMuleListCtrl::HidePersistentInfoTip(bool bClearPendingState)
{
	StopPersistentInfoTipLeaveTimer();

	if (m_wndPersistentInfoTip.GetSafeHwnd() != NULL && m_bPersistentInfoTipVisible) {
		TOOLINFO ti = {};
		ti.cbSize = sizeof(ti);
		ti.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;
		ti.hwnd = m_hWnd;
		ti.uId = MULELISTCTRL_PERSISTENT_INFOTIP_TOOL_ID;
		m_wndPersistentInfoTip.SendMessage(TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&ti));
		m_wndPersistentInfoTip.SendMessage(TTM_POP);
	}

	m_bPersistentInfoTipVisible = false;
	if (bClearPendingState) {
		ResetPersistentInfoTipState();
		ResetPendingPersistentInfoTip();
	}
}

void CMuleListCtrl::RefreshPersistentInfoTipFromPoint(CPoint point)
{
	if (!::IsWindow(m_hWnd) || !m_bPersistentInfoTipVisible)
		return;

	if (IsCursorOverPersistentInfoTipWindow())
		return;

	if (IsPointWithinPersistentInfoTipHotArea(point, m_PersistentInfoTipContext))
		return;

	HidePersistentInfoTip(true);
}

void CMuleListCtrl::UpdatePersistentInfoTipTracking(CPoint point)
{
	if (!::IsWindow(m_hWnd))
		return;

	if (m_bPersistentInfoTipVisible) {
		if (IsCursorOverPersistentInfoTipWindow() || IsPointWithinPersistentInfoTipHotArea(point, m_PersistentInfoTipContext)) {
			ResetPendingPersistentInfoTip();
			return;
		}

		HidePersistentInfoTip(true);
	}

	SPersistentInfoTipContext context;
	if (!TryGetPersistentInfoTipContext(point, context)) {
		ResetPendingPersistentInfoTip();
		return;
	}

	if (m_bTrackingMouseHover && IsSamePersistentInfoTipTarget(context, m_PendingInfoTipContext))
		return;

	CString strText;
	bool bExplicitTip = false;
	if (!TryGetPersistentInfoTipForContext(context, strText, &bExplicitTip)) {
		ResetPendingPersistentInfoTip();
		if (m_bPersistentInfoTipVisible)
			HidePersistentInfoTip(true);
		return;
	}
	context.bExplicitTip = bExplicitTip;
	if (!bExplicitTip && !IsPointWithinPersistentInfoTipHotArea(point, context)) {
		ResetPendingPersistentInfoTip();
		return;
	}

	TRACKMOUSEEVENT track = {};
	track.cbSize = sizeof(track);
	track.dwFlags = TME_HOVER | TME_LEAVE;
	track.hwndTrack = m_hWnd;
	track.dwHoverTime = SEC2MS(thePrefs.GetToolTipDelay());
	if (!::TrackMouseEvent(&track)) {
		ResetPendingPersistentInfoTip();
		return;
	}

	m_PendingInfoTipContext = context;
	m_bTrackingMouseHover = true;
	m_bTrackingMouseLeave = true;
}

bool CMuleListCtrl::ShouldSuppressDefaultInfoTip(LPNMLVGETINFOTIP pGetInfoTip)
{
	if (pGetInfoTip == NULL)
		return false;

	if (m_bPersistentInfoTipVisible && IsCursorOverPersistentInfoTipWindow())
		return true;

	CPoint ptCursor;
	if (!::GetCursorPos(&ptCursor))
		return false;

	ScreenToClient(&ptCursor);

	SPersistentInfoTipContext context;
	if (!TryGetPersistentInfoTipContext(ptCursor, context))
		return false;
	if (context.iItem != pGetInfoTip->iItem || context.iSubItem != pGetInfoTip->iSubItem)
		return false;

	CString strText;
	if (!TryGetExplicitPersistentInfoTipText(context, strText))
		return false;
	context.bExplicitTip = true;

	ShowPersistentInfoTip(context, strText);
	return true;
}

int CALLBACK CMuleListCtrl::SortProc(LPARAM /*lParam1*/, LPARAM /*lParam2*/, LPARAM /*lParamSort*/)
{
	return 0;
}

void CMuleListCtrl::PreSubclassWindow()
{
	SetColors();
	CListCtrl::PreSubclassWindow();
	// Win98: Explicitly set to Unicode to receive Unicode notifications.
	SendMessage(CCM_SETUNICODEFORMAT, TRUE);
	SetExtendedStyle(LVS_EX_HEADERDRAGDROP);

	// Vista: Reduce flickering in header control
	ModifyStyle(0, WS_CLIPCHILDREN);

	ModifyStyle(0, LVS_SHAREIMAGELISTS);

	// If we want to handle the VK_RETURN key, we have to do that via accelerators!
	if (m_uIDAccel != UINT_MAX) {
		m_hAccel = ::LoadAccelerators(AfxGetResourceHandle(), MAKEINTRESOURCE(m_uIDAccel));
		ASSERT(m_hAccel);
	}

	// DEFAULT_GUI_FONT: Vista: "MS Shell Dlg" with 8 pts (regardless of system applet settings !!!)
	// SYSTEM_FONT:		 Vista: Good old Windows 3.11 System Font
	// NULL				 Vista: Font ('Symbol') with the face and size which is configured in System applet.
	if (thePrefs.GetUseSystemFontForMainControls())
		SendMessage(WM_SETFONT, NULL, FALSE);
}

int CMuleListCtrl::IndexToOrder(CHeaderCtrl *pHeader, int iIndex)
{
	int iCount = pHeader->GetItemCount();
	int *piArray = new int[iCount];
	Header_GetOrderArray(pHeader->m_hWnd, iCount, piArray);
	while (--iCount >= 0)
		if (piArray[iCount] == iIndex)
			break;
	delete[] piArray;
	return iCount;
}

void CMuleListCtrl::HideColumn(int iColumn)
{
	CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	int iCount = pHeaderCtrl->GetItemCount();
	if (iColumn < 1 || iColumn >= iCount || m_aColumns[iColumn].bHidden)
		return;

	//stop it from redrawing
	SetRedraw(false);

	//shrink width to 0
	HDITEM item;
	item.mask = HDI_WIDTH;
	pHeaderCtrl->GetItem(iColumn, &item);
	m_aColumns[iColumn].iWidth = item.cxy;
	item.cxy = 0;
	pHeaderCtrl->SetItem(iColumn, &item);

	//move to front of list
	INT *piArray = new INT[m_iColumnsTracked];
	pHeaderCtrl->GetOrderArray(piArray, m_iColumnsTracked);

	int iFrom = m_aColumns[iColumn].iLocation;
	for (int i = 0; i < m_iColumnsTracked; ++i)
		iFrom += static_cast<int>(m_aColumns[i].iLocation > m_aColumns[iColumn].iLocation && m_aColumns[i].bHidden);

	for (; iFrom > 0; --iFrom)
		piArray[iFrom] = piArray[iFrom - 1];
	piArray[0] = iColumn;
	pHeaderCtrl->SetOrderArray(m_iColumnsTracked, piArray);
	delete[] piArray;

	//update entry
	m_aColumns[iColumn].bHidden = true;

	//redraw
	SetRedraw(true);
	Invalidate(FALSE);
}

void CMuleListCtrl::ShowColumn(int iColumn)
{
	CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	int iCount = pHeaderCtrl->GetItemCount();
	if (iColumn < 1 || iColumn >= iCount || !m_aColumns[iColumn].bHidden)
		return;

	//stop it from redrawing
	SetRedraw(false);

	//restore position in list
	INT *piArray = new INT[m_iColumnsTracked];
	pHeaderCtrl->GetOrderArray(piArray, m_iColumnsTracked);
	int iCurrent = IndexToOrder(pHeaderCtrl, iColumn);

	for (; iCurrent < m_iColumnsTracked - 1 && iCurrent < IndexToOrder(pHeaderCtrl, 0); ++iCurrent)
		piArray[iCurrent] = piArray[iCurrent + 1];
	for (; iCurrent < m_iColumnsTracked - 1 &&
		m_aColumns[iColumn].iLocation > m_aColumns[pHeaderCtrl->OrderToIndex(iCurrent + 1)].iLocation; ++iCurrent)
	{
		piArray[iCurrent] = piArray[iCurrent + 1];
	}
	piArray[iCurrent] = iColumn;
	pHeaderCtrl->SetOrderArray(m_iColumnsTracked, piArray);
	delete[] piArray;

	//and THEN restore original width
	HDITEM item;
	item.mask = HDI_WIDTH;
	item.cxy = m_aColumns[iColumn].iWidth;
	pHeaderCtrl->SetItem(iColumn, &item);

	//update entry
	m_aColumns[iColumn].bHidden = false;

	//redraw
	SetRedraw(true);
	Invalidate(FALSE);
}

void CMuleListCtrl::SaveSettings(const bool bCalledBySaveAppState)
{
	ASSERT(!m_Name.IsEmpty());

	ASSERT(GetHeaderCtrl()->GetItemCount() == m_iColumnsTracked);

	if (m_Name.IsEmpty() || GetHeaderCtrl()->GetItemCount() != m_iColumnsTracked)
		return;

	CIni ini(thePrefs.GetConfigFile(), _T("ListControlSetup"));

	if (bCalledBySaveAppState) {
		if (GetMaxSortHistory()) // GetUITweaksMaxSortHistory == 0 means unlimited sort order history
			while (m_liSortHistory.GetSize() && m_liSortHistory.GetCount() > GetMaxSortHistory())
				m_liSortHistory.RemoveTail();
	} else
		ShowWindow(SW_HIDE);

	int i;
	CString strSortHist;
	POSITION pos = m_liSortHistory.GetTailPosition();
	if (pos != NULL) {
		strSortHist.Format(_T("%d"), m_liSortHistory.GetPrev(pos));
		while (pos != NULL) {
			strSortHist.AppendChar(_T(','));
			strSortHist.AppendFormat(_T("%d"), m_liSortHistory.GetPrev(pos));
		}
	}
	ini.WriteString(m_Name + _T("SortHistory"), strSortHist);
	// store additional settings
	ini.WriteInt(m_Name + _T("MaxSortHistory"), GetMaxSortHistory());
	ini.WriteInt(m_Name + _T("TableSortItem"), GetSortItem());
	ini.WriteInt(m_Name + _T("TableSortAscending"), GetSortType(m_atSortArrow));

	int *piColWidths = new int[m_iColumnsTracked];
	int *piColHidden = new int[m_iColumnsTracked];
	for (i = 0; i < m_iColumnsTracked; ++i) {
		piColWidths[i] = GetColumnWidth(i);
		piColHidden[i] = IsColumnHidden(i);
		if (!bCalledBySaveAppState)
			ShowColumn(i);
	}

	int *piColOrders = new int[m_iColumnsTracked];
	if (bCalledBySaveAppState) {
		std::vector<bool> abUsedColumns(static_cast<size_t>(m_iColumnsTracked), false);
		int iNextFallbackLocation = 0;
		for (i = 0; i < m_iColumnsTracked; ++i)
			piColOrders[i] = i;
		for (i = 0; i < m_iColumnsTracked; ++i) {
			int iLocation = m_aColumns[i].iLocation;
			if (iLocation < 0 || iLocation >= m_iColumnsTracked || abUsedColumns[static_cast<size_t>(iLocation)]) {
				while (iNextFallbackLocation < m_iColumnsTracked && abUsedColumns[static_cast<size_t>(iNextFallbackLocation)])
					++iNextFallbackLocation;
				iLocation = iNextFallbackLocation < m_iColumnsTracked ? iNextFallbackLocation : i;
			}
			piColOrders[iLocation] = i;
			abUsedColumns[static_cast<size_t>(iLocation)] = true;
		}
	} else
		GetHeaderCtrl()->GetOrderArray(piColOrders, m_iColumnsTracked);

	ini.SerGet(false, piColWidths, m_iColumnsTracked, m_Name + _T("ColumnWidths"));
	ini.SerGet(false, piColHidden, m_iColumnsTracked, m_Name + _T("ColumnHidden"));
	ini.SerGet(false, piColOrders, m_iColumnsTracked, m_Name + _T("ColumnOrders"));

	if (!bCalledBySaveAppState) {
		for (i = 0; i < m_iColumnsTracked; ++i)
			if (piColHidden[i])
				HideColumn(i);

		ShowWindow(SW_SHOW);
	}

	delete[] piColOrders;
	delete[] piColWidths;
	delete[] piColHidden;
}

int CMuleListCtrl::GetSortType(ArrowType at)
{
	switch (at) {
	case arrowDown:
	default:
		return 0;
	case arrowUp:
		return 1;
	case arrowDoubleDown:
		return 2;
	case arrowDoubleUp:
		return 3;
	}
}

CMuleListCtrl::ArrowType CMuleListCtrl::GetArrowType(int iat)
{
	static const CMuleListCtrl::ArrowType art[] = {arrowDown, arrowUp, arrowDoubleDown, arrowDoubleUp};

	return art[(iat >= 0 && iat < _countof(art)) ? iat : 0];
}

// Default list control settings (used when preferences.ini has no saved values)
struct ListCtrlDefaults
{
	LPCTSTR pszName;
	LPCTSTR pszSortHistory;
	int iSortItem;
	int iSortAscending;
	int iMaxSortHistory;
	LPCTSTR pszColumnWidths;
	LPCTSTR pszColumnHidden;
	LPCTSTR pszColumnOrders;
};

static const ListCtrlDefaults s_aListDefaults[] =
{
	{ _T("SearchListCtrl"),
		_T("2,11,0,65537"), 1, 0, 4,
		_T("848,84,59,66,137,104,75,116,79,114,228,587,100,161,213,220,65"),
		_T("0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1"),
		_T("0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16") },
	{ _T("DownloadListCtrl"),
		_T("65547,65537"), 1, 0, 2,
		_T("713,76,79,84,87,445,90,67,81,136,155,125,65,125,118,70,94"),
		_T("0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0"),
		_T("0,1,3,15,16,12,9,2,8,4,6,7,5,11,10,13,14") },
	{ _T("ClientListCtrl"),
		_T("65554"), 18, 0, 1,
		_T("296,99,105,115,119,216,94,217,137,221,125,76,108,64,67,129,90,79,80,86"),
		_T("0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0"),
		_T("0,7,8,9,5,14,6,13,10,11,12,4,2,3,1,15,16,17,18,19") },
	{ _T("QueueListCtrl"),
		_T("22"), 22, 1, 4,
		_T("298,208,86,65,59,61,97,106,79,175,215,134,133,125,76,108,62,66,107,90,80,82,89,60,60"),
		_T("0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0"),
		_T("0,10,11,12,9,17,16,13,14,15,1,2,3,4,5,6,7,18,19,20,21,8,22,23,24") },
	{ _T("UploadListCtrl"),
		_T("19"), 19, 1, 4,
		_T("298,230,71,135,68,80,74,100,185,215,138,136,78,108,92,72,140,164,80,82,132,59,60,60,60"),
		_T("0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0"),
		_T("0,9,10,11,20,8,14,12,13,15,16,17,1,2,3,4,5,6,7,21,18,19,22,23,24") },
	{ _T("DownloadClientsCtrl"),
		_T("65554"), 18, 0, 4,
		_T("298,186,228,71,130,121,106,122,215,131,104,78,109,59,70,141,165,81,81,132,100"),
		_T("0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0"),
		_T("0,8,9,10,19,1,13,11,12,14,15,16,2,3,4,5,6,7,17,18,20") },
	{ _T("ServerListCtrl"),
		_T("0,65540,65542"), 6, 0, 4,
		_T("174,150,238,50,61,70,66,86,50,50,78,83,50,51,75,55"),
		_T("0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1"),
		_T("0,2,1,3,6,4,13,5,8,7,9,10,11,12,15,14") },
	{ _T("SharedFilesCtrl"),
		_T("65537"), 1, 0, 1,
		_T("964,76,50,70,220,100,118,120,100,190,100,104,96,101,94,73,79,209,50,50"),
		_T("0,0,0,1,0,1,1,1,1,0,1,1,0,0,0,0,0,0,0,0"),
		_T("0,1,2,15,16,17,12,13,14,9,4,10,8,7,3,5,6,11,18,19") },
	{ _T("FriendListCtrl"),
		_T("0"), 0, 1, 4,
		_T("339"),
		_T("0"),
		_T("0") },
	{ _T("KadSearchListCtrl"),
		_T("65536"), 0, 0, 4,
		_T("106,232,115,607,96,100,100,100"),
		_T("0,0,0,0,0,0,0,0"),
		_T("0,1,2,3,4,5,6,7") },
	{ _T("ONContactListCtrl"),
		_T("0,3"), 3, 1, 4,
		_T("125,214,59,885,246"),
		_T("0,0,0,0,0"),
		_T("0,4,2,1,3") },
	{ _T("IrcNickListCtrl"),
		_T("0"), 0, 1, 4,
		_T("177"),
		_T("0"),
		_T("0") },
	{ _T("IrcChannelListCtrl"),
		_T("0"), 0, 1, 4,
		_T("203,50,350"),
		_T("0,0,0"),
		_T("0,1,2") },
	{ _T("FileDetailDlgName"),
		_T("0"), 0, 1, 4,
		_T("450,60"),
		_T("0,0"),
		_T("0,1") },
	{ _T("CommentListCtrl"),
		_T("0"), 0, 1, 4,
		_T("80,340,260,150,80"),
		_T("0,0,0,0,0"),
		_T("0,1,2,3,4") },
	{ _T("CollectionListCtrlCollectionView"),
		_T("65537,0"), 0, 1, 4,
		_T("860,65,150"),
		_T("0,0,0"),
		_T("0,1,2") },
	{ _T("CollectionListCtrlCollectionCreateR"),
		_T("0"), 0, 1, 4,
		_T("260,65,220"),
		_T("0,0,0"),
		_T("0,1,2") },
	{ _T("CollectionListCtrlCollectionCreateL"),
		_T("0"), 0, 1, 4,
		_T("260,65,220"),
		_T("0,0,0"),
		_T("0,1,2") },
};

static const ListCtrlDefaults* FindListDefaults(const CString &strName)
{
	for (int i = 0; i < _countof(s_aListDefaults); ++i)
		if (strName == s_aListDefaults[i].pszName)
			return &s_aListDefaults[i];
	return NULL;
}

// Parse a comma-separated int string into an array
static void ParseIntArray(const CString &strData, int *ar, int nCount, int iDefault)
{
	CString strTemp;
	int nOffset = 0;
	for (int i = 0; i < nCount; ++i) {
		nOffset = CIni::Parse(strData, nOffset, strTemp);
		ar[i] = strTemp.IsEmpty() ? iDefault : _tstoi(strTemp);
	}
}

void CMuleListCtrl::LoadSettings()
{
	if (m_Name.IsEmpty()) {
		ASSERT(0);
		return;
	}

	CIni ini(thePrefs.GetConfigFile(), _T("ListControlSetup"));

	// Look up hardcoded defaults for this list control
	const ListCtrlDefaults *pDef = FindListDefaults(m_Name);

	// sort history
	CString strSortHist = ini.GetString(m_Name + _T("SortHistory"));
	if (strSortHist.IsEmpty() && pDef)
		strSortHist = pDef->pszSortHistory;

	m_iMaxSortHistory = ini.GetInt(m_Name + _T("MaxSortHistory"), pDef ? pDef->iMaxSortHistory : 4);

	int nOffset = 0;
	CString strTemp;
	nOffset = ini.Parse(strSortHist, nOffset, strTemp);
	while (!strTemp.IsEmpty()) {
		UpdateSortHistory((int)_tstoi(strTemp));
		nOffset = ini.Parse(strSortHist, nOffset, strTemp);
	}

	m_iCurrentSortItem = ini.GetInt(m_Name + _T("TableSortItem"), pDef ? pDef->iSortItem : 0);
	m_atSortArrow = GetArrowType(ini.GetInt(m_Name + _T("TableSortAscending"), pDef ? pDef->iSortAscending : 1));
	if (m_liSortHistory.IsEmpty())
		m_liSortHistory.AddTail(m_iCurrentSortItem);

	// columns settings
	int *piColWidths = new int[m_iColumnsTracked];
	int *piColHidden = new int[m_iColumnsTracked];
	int *piColOrders = new int[m_iColumnsTracked];

	// Read column data from INI; if not saved, use hardcoded defaults
	CString strColWidths = ini.GetString(m_Name + _T("ColumnWidths"));
	if (strColWidths.IsEmpty() && pDef)
		strColWidths = pDef->pszColumnWidths;
	ParseIntArray(strColWidths, piColWidths, m_iColumnsTracked, 0);

	CString strColHidden = ini.GetString(m_Name + _T("ColumnHidden"));
	if (strColHidden.IsEmpty() && pDef)
		strColHidden = pDef->pszColumnHidden;
	ParseIntArray(strColHidden, piColHidden, m_iColumnsTracked, -1);

	CString strColOrders = ini.GetString(m_Name + _T("ColumnOrders"));
	if (strColOrders.IsEmpty() && pDef)
		strColOrders = pDef->pszColumnOrders;
	ParseIntArray(strColOrders, piColOrders, m_iColumnsTracked, 0);

	// apply column widths and verify sort order
	int *piArray = new int[m_iColumnsTracked];
	for (int i = 0; i < m_iColumnsTracked; ++i) {
		piArray[i] = i;

		if (piColWidths[i] >= 2) // don't allow column widths of 0 and 1 -- just because it looks very confusing in GUI
			SetColumnWidth(i, piColWidths[i]);

		int iOrder = piColOrders[i];
		if (i > 0 && iOrder > 0 && iOrder < m_iColumnsTracked && iOrder != i)
			piArray[i] = iOrder;
		m_aColumns[i].iLocation = piArray[i];
	}
	piArray[0] = 0;

	for (int i = m_iColumnsTracked; --i >= 0;)
		m_aColumns[piArray[i]].iLocation = i;
	GetHeaderCtrl()->SetOrderArray(m_iColumnsTracked, piArray);

	for (int i = 1; i < m_iColumnsTracked; ++i)
		if (piColHidden[i] > 0 || (piColHidden[i] < 0 && m_liDefaultHiddenColumns.Find(i) != NULL))
			HideColumn(i);

	delete[] piArray;
	delete[] piColOrders;
	delete[] piColWidths;
	delete[] piColHidden;
}

HBITMAP LoadImageAsPARGB(LPCTSTR pszPath)
{
	// NOTE: Do *NOT* forget to specify /DELAYLOAD:gdiplus.dll as link parameter.
	HBITMAP hbmPARGB = NULL;
	ULONG_PTR gdiplusToken = 0;
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) == Gdiplus::Ok) {
		Gdiplus::Bitmap bmp(pszPath);
#if 0
		Gdiplus::Rect rc(0, 0, bmp.GetWidth(), bmp.GetHeight());
		// For PNGs with RGBA, it does not make any difference whether the pixel format is specified as:
		//
		//	PixelFormat32bppPARGB	(the supposed correct one)
		//	PixelFormat32bppARGB	(could also work)
		//	PixelFormat32bppRGB		(should not work)
		//	PixelFormat24bppRGB		(should not work at all)
		//
		// The returned bitmap always contains a correct alpha channel !?
		//
		// For ICOs with RGBA, it also does not make any difference what the pixel format is set to, the
		// returned bitmap is always *wrong* (no alpha).
		//
		Gdiplus::Bitmap *pBmpPARGB = bmp.Clone(rc, PixelFormat32bppPARGB);
		if (pBmpPARGB) {
			// Regardless whether a PNG or ICO was loaded and regardless what pixel format was specified,
			// the pixel format here is always 'PixelFormat32bppARGB' !?
			Gdiplus::PixelFormat pf = pBmpPARGB->GetPixelFormat();
			ASSERT(pf == PixelFormat32bppARGB);

			pBmpPARGB->GetHBITMAP(NULL, &hbmPARGB);
			delete pBmpPARGB;
		}
#else
		bmp.GetHBITMAP(NULL, &hbmPARGB);
#endif
	}
	Gdiplus::GdiplusShutdown(gdiplusToken);
	return hbmPARGB;
}

void CMuleListCtrl::SetColors()
{	
	m_crWindow = GetCustomSysColor(COLOR_WINDOW);
	m_crWindowText = GetCustomSysColor(COLOR_WINDOWTEXT);
	m_crWindowTextBk = m_crWindow;

	COLORREF crHighlight = GetCustomSysColor(COLOR_HIGHLIGHT);

	CString strBkImage;
	const CString &sSkinProfile(thePrefs.GetSkinProfile());
	if (!sSkinProfile.IsEmpty()) {
		const CString strKey(m_strSkinKey.IsEmpty() ? CString(_T("DefLv")) : m_strSkinKey);

		if (theApp.LoadSkinColorAlt(strKey + _T("Bk"), _T("DefLvBk"), m_crWindow))
			m_crWindowTextBk = m_crWindow;
		theApp.LoadSkinColorAlt(strKey + _T("Fg"), _T("DefLvFg"), m_crWindowText);
		theApp.LoadSkinColorAlt(strKey + _T("Hl"), _T("DefLvHl"), crHighlight);

		TCHAR szColor[MAX_PATH];
		GetPrivateProfileString(_T("Colors"), strKey + _T("BkImg"), (LPCTSTR)NULL, szColor, _countof(szColor), sSkinProfile);
		if (*szColor == _T('\0'))
			GetPrivateProfileString(_T("Colors"), _T("DefLvBkImg"), (LPCTSTR)NULL, szColor, _countof(szColor), sSkinProfile);
		if (*szColor != _T('\0'))
			strBkImage = szColor;
	}

	SetBkColor(m_crWindow);
	SetTextBkColor(m_crWindowTextBk);
	SetTextColor(m_crWindowText);

	// Must explicitly set a NULL watermark bitmap, to clear any already set watermark bitmap.
	LVBKIMAGE lvimg = {};
	lvimg.ulFlags = LVBKIF_TYPE_WATERMARK;
	SetBkImage(&lvimg);

	if (!strBkImage.IsEmpty() && !g_bLowColorDesktop) {
		// expand any optional available environment strings
		TCHAR szExpSkinRes[MAX_PATH];
		if (::ExpandEnvironmentStrings(strBkImage, szExpSkinRes, _countof(szExpSkinRes)) != 0)
			strBkImage = szExpSkinRes;

		// create absolute path to icon resource file
		TCHAR szFullResPath[MAX_PATH];
		if (::PathIsRelative(strBkImage)) {
			TCHAR szSkinResFolder[MAX_PATH];
			_tcsncpy(szSkinResFolder, sSkinProfile, _countof(szSkinResFolder));
			szSkinResFolder[_countof(szSkinResFolder) - 1] = _T('\0');
			::PathRemoveFileSpec(szSkinResFolder);
			_tmakepathlimit(szFullResPath, NULL, szSkinResFolder, strBkImage, NULL);
		} else {
			_tcsncpy(szFullResPath, strBkImage, _countof(szFullResPath));
			szFullResPath[_countof(szFullResPath) - 1] = _T('\0');
		}

#if 0
		// Explicitly check if the file exists, because 'SetBkImage' will return TRUE even if the file does not exist.
		if (::PathFileExists(szFullResPath)) {
			// This places the bitmap near the bottom-right border of the client area. But due to that
			// the position is specified via percentages, the bitmap is never exactly at the bottom
			// right border, it depends on the window's height. Apart from that, the bitmap gets
			// scrolled(!) with the window contents.
			CString strUrl(_T("file:///"));
			strUrl += szFullResPath;
			if (SetBkImage(const_cast<LPTSTR>((LPCTSTR)strUrl), FALSE, 100, 92)) {
				m_crWindowTextBk = CLR_NONE;
				SetTextBkColor(m_crWindowTextBk);
			}
		}
#else
		HBITMAP hbm = LoadImageAsPARGB(szFullResPath);
		if (hbm) {
			LVBKIMAGE lvbkimg = {};
			lvbkimg.ulFlags = LVBKIF_TYPE_WATERMARK;
			lvbkimg.ulFlags |= LVBKIF_FLAG_ALPHABLEND;
			lvbkimg.hbm = hbm;
			if (SetBkImage(&lvbkimg)) {
				m_crWindowTextBk = CLR_NONE;
				SetTextBkColor(m_crWindowTextBk);
			} else
				::DeleteObject(lvbkimg.hbm);
		}
#endif
	}


	m_crFocusLine = crHighlight;
	if (g_bLowColorDesktop) {
		m_crNoHighlight = crHighlight;
		m_crNoFocusLine = crHighlight;
		m_crHighlight = crHighlight;
		m_crHighlightText = GetCustomSysColor(COLOR_HIGHLIGHTTEXT);
		m_crGlow = crHighlight;
	} else {
		m_crNoHighlight = MLC_RGBBLEND(crHighlight, m_crWindow, 8);
		m_crNoFocusLine = MLC_RGBBLEND(crHighlight, m_crWindow, 2);
		m_crHighlight = MLC_RGBBLEND(crHighlight, m_crWindow, 4);
		m_crHighlightText = m_crWindowText;
		m_crGlow = MLC_RGBBLEND(crHighlight, m_crWindow, 3);
	}
}

void CMuleListCtrl::RefreshThemeColors()
{
	if (!::IsWindow(m_hWnd))
		return;

	SetColors();

	// Rebuild the header sort marker with the current theme colors.
	if (m_iCurrentSortItem >= 0)
		SetSortArrow(m_iCurrentSortItem, (ArrowType)m_atSortArrow);
}

void CMuleListCtrl::SetSortArrow(int iColumn, ArrowType atType)
{
	HDITEM headerItem;
	headerItem.mask = HDI_FORMAT;
	CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();

	if (iColumn != m_iCurrentSortItem) {
		pHeaderCtrl->GetItem(m_iCurrentSortItem, &headerItem);
		headerItem.fmt &= ~(HDF_IMAGE | HDF_BITMAP_ON_RIGHT);
		pHeaderCtrl->SetItem(m_iCurrentSortItem, &headerItem);
		m_iCurrentSortItem = iColumn;
		m_imlHeaderCtrl.DeleteImageList();
	}

	//place new arrow unless we were given an invalid column
	if (iColumn >= 0 && pHeaderCtrl->GetItem(iColumn, &headerItem)) {
		m_atSortArrow = atType;

		HINSTANCE hInstRes = AfxFindResourceHandle(MAKEINTRESOURCE(m_atSortArrow), RT_BITMAP);
		if (hInstRes != NULL) {
			HBITMAP hbmSortStates = (HBITMAP)::LoadImage(hInstRes, MAKEINTRESOURCE(m_atSortArrow), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION | LR_LOADMAP3DCOLORS);
			if (hbmSortStates != NULL) {
				CBitmap bmSortStates;
				bmSortStates.Attach(hbmSortStates);

				CImageList imlSortStates;
				if (imlSortStates.Create(14, 14, theApp.m_iDfltImageListColorFlags | ILC_MASK, 1, 0)) {
					VERIFY(imlSortStates.Add(&bmSortStates, RGB(255, 0, 255)) >= 0);

					// To avoid drawing problems (which occur only with an image list *with* a mask) while
					// resizing list view columns which have the header control bitmap right aligned, set
					// the background color of the image list.
					if (theApp.m_ullComCtrlVer < MAKEDLLVERULL(6, 0, 0, 0))
						imlSortStates.SetBkColor(GetCustomSysColor(COLOR_BTNFACE));

					// When setting the image list for the header control for the first time we'll get
					// the image list of the listview control!! So, better store the header control image list separate.
					(void)pHeaderCtrl->SetImageList(&imlSortStates);
					m_imlHeaderCtrl.DeleteImageList();
					m_imlHeaderCtrl.Attach(imlSortStates.Detach());

					// Use smaller bitmap margins -- this saves some pixels which may be required for
					// rather small column titles.
					if (theApp.m_ullComCtrlVer >= MAKEDLLVERULL(5, 8, 0, 0)) {
						int iBmpMargin = pHeaderCtrl->GetBitmapMargin();
						int iNewBmpMargin = ::GetSystemMetrics(SM_CXEDGE) + ::GetSystemMetrics(SM_CXEDGE) / 2;
						if (iNewBmpMargin < iBmpMargin)
							pHeaderCtrl->SetBitmapMargin(iNewBmpMargin);
					}
				}
			}
		}
		headerItem.mask |= HDI_IMAGE;
		headerItem.fmt |= HDF_IMAGE | HDF_BITMAP_ON_RIGHT;
		headerItem.iImage = 0;
		pHeaderCtrl->SetItem(iColumn, &headerItem);
	}
}

// move item in list, returns index of new item
int CMuleListCtrl::MoveItem(int iOldIndex, int iNewIndex)
{
	if (iNewIndex > iOldIndex)
		--iNewIndex;

	// copy item
	LVITEM lvi;
	TCHAR szText[256];
	lvi.mask = LVIF_TEXT | LVIF_STATE | LVIF_PARAM | LVIF_INDENT | LVIF_IMAGE | LVIF_NORECOMPUTE;
	lvi.stateMask = UINT_MAX;
	lvi.iItem = iOldIndex;
	lvi.iSubItem = 0;
	lvi.pszText = szText;
	lvi.cchTextMax = _countof(szText);
	lvi.iIndent = 0;
	if (!GetItem(&lvi))
		return -1;

	// copy strings of sub items
	CSimpleArray<void*> aSubItems;
	DWORD Style = GetStyle();
	if ((Style & LVS_OWNERDATA) == 0) {
		TCHAR szText1[256];
		LVITEM lvi1;
		lvi1.mask = LVIF_TEXT | LVIF_NORECOMPUTE;
		lvi1.iItem = iOldIndex;
		lvi1.cchTextMax = _countof(szText1);
		for (int i = 1; i < m_iColumnsTracked; ++i) {
			void *pstrSubItem;
			lvi1.iSubItem = i;
			lvi1.pszText = szText1;
			if (GetItem(&lvi1))
				if (lvi1.pszText == LPSTR_TEXTCALLBACK)
					pstrSubItem = LPSTR_TEXTCALLBACK;
				else
					pstrSubItem = new CString(szText1);
			else
				pstrSubItem = NULL;
			aSubItems.Add(pstrSubItem);
		}
	}

	// do the move
	SetRedraw(false);
	DeleteItem(iOldIndex);
	lvi.iItem = iNewIndex;
	iNewIndex = InsertItem(&lvi);

	// restore strings of sub items
	if ((Style & LVS_OWNERDATA) == 0) {
		for (int i = 1; i < m_iColumnsTracked; ++i) {
			LVITEM lvi1;
			lvi1.iSubItem = i;
			void *pstrSubItem = aSubItems[i - 1];
			if (pstrSubItem != NULL) {
				if (pstrSubItem == LPSTR_TEXTCALLBACK)
					lvi1.pszText = LPSTR_TEXTCALLBACK;
				else
					lvi1.pszText = const_cast<LPTSTR>((LPCTSTR)(*(CString*)pstrSubItem));
				DefWindowProc(LVM_SETITEMTEXT, iNewIndex, (LPARAM)&lvi1);
				if (pstrSubItem != LPSTR_TEXTCALLBACK)
					delete (CString*)pstrSubItem;
			}
		}
	}

	SetRedraw(true);

	return iNewIndex;
}

int CMuleListCtrl::UpdateLocation(int iItem)
{
	// Skip reordering for virtual lists
	if (GetStyle() & LVS_OWNERDATA)
		return iItem;

	int iItemCount = GetItemCount();
	if (iItem >= iItemCount || iItem < 0)
		return iItem;

	BOOL notLast = iItem + 1 < iItemCount;
	BOOL notFirst = iItem > 0;

	DWORD_PTR dwpItemData = GetItemData(iItem);
	if (dwpItemData == NULL)
		return iItem;

	if (notFirst) {
		int iNewIndex = iItem - 1;
		POSITION pos = m_Params.FindIndex(iNewIndex);
		int iResult = MultiSortProc(dwpItemData, GetParamAt(pos, iNewIndex));
		if (iResult < 0) {
			POSITION posPrev = pos;
			int iDist = iNewIndex / 2;
			while (iDist > 1) {
				for (int i = 0; i < iDist; ++i)
					m_Params.GetPrev(posPrev);

				if (MultiSortProc(dwpItemData, GetParamAt(posPrev, iNewIndex - iDist)) < 0) {
					iNewIndex = iNewIndex - iDist;
					pos = posPrev;
				} else
					posPrev = pos;

				iDist /= 2;
			}
			while (--iNewIndex >= 0) {
				m_Params.GetPrev(pos);
				if (MultiSortProc(dwpItemData, GetParamAt(pos, iNewIndex)) >= 0)
					break;
			}
			MoveItem(iItem, iNewIndex + 1);
			return iNewIndex + 1;
		}
	}

	if (notLast) {
		int iNewIndex = iItem + 1;
		POSITION pos = m_Params.FindIndex(iNewIndex);
		int iResult = MultiSortProc(dwpItemData, GetParamAt(pos, iNewIndex));
		if (iResult > 0) {
			POSITION posNext = pos;
			int iDist = (GetItemCount() - iNewIndex) / 2;
			while (iDist > 1) {
				for (int i = 0; i < iDist; ++i)
					m_Params.GetNext(posNext);

				if (MultiSortProc(dwpItemData, GetParamAt(posNext, iNewIndex + iDist)) > 0) {
					iNewIndex = iNewIndex + iDist;
					pos = posNext;
				} else
					posNext = pos;

				iDist /= 2;
			}
			while (++iNewIndex < iItemCount) {
				m_Params.GetNext(pos);
				if (MultiSortProc(dwpItemData, GetParamAt(pos, iNewIndex)) <= 0)
					break;
			}
			MoveItem(iItem, iNewIndex);
			return iNewIndex;
		}
	}

	return iItem;
}

DWORD_PTR CMuleListCtrl::GetItemData(int iItem)
{
	if (GetStyle() & LVS_OWNERDATA)
		return GetVirtualItemData(iItem);

	POSITION pos = m_Params.FindIndex(iItem);
	if (pos == NULL)
		return 0;
	LPARAM lParam = GetParamAt(pos, iItem);
	MLC_ASSERT(lParam == CListCtrl::GetItemData(iItem));
	return lParam;
}

//lower level than everything else so poorly overriden functions don't break us
BOOL CMuleListCtrl::OnWndMsg(UINT message, WPARAM wParam, LPARAM lParam, LRESULT *pResult)
{
//lets look for the important messages that are essential to handle
	switch (message) {
	case WM_NOTIFY:
		if (wParam)
			break;
		switch (reinterpret_cast<LPNMHDR>(lParam)->code) {
		case NM_RCLICK: //handle right click on headers and show column menu
			{
				POINT point;
				::GetCursorPos(&point);

				CMenuXP tmColumnMenu;
				tmColumnMenu.CreatePopupMenu();

				CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
				int iCount = pHeaderCtrl->GetItemCount();
				for (int iCurrent = 1; iCurrent < iCount; ++iCurrent) {
					HDITEM item;
					TCHAR text[255];
					item.pszText = text;
					item.mask = HDI_TEXT;
					item.cchTextMax = _countof(text);
					pHeaderCtrl->GetItem(iCurrent, &item);

					tmColumnMenu.AppendMenu(MF_STRING | (m_aColumns[iCurrent].bHidden ? 0 : MF_CHECKED)
						, MLC_IDC_MENU + iCurrent, item.pszText);
				}
				tmColumnMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
				VERIFY(tmColumnMenu.DestroyMenu());
			}
			return (BOOL)(*pResult = TRUE);
		case HDN_BEGINTRACKA: //forbid changing the size of anything "before" the first column
		case HDN_BEGINTRACKW:
			if (m_aColumns[reinterpret_cast<HD_NOTIFY*>(lParam)->iItem].bHidden)
				return (BOOL)(*pResult = TRUE);
			break;
		case HDN_ENDDRAG: //forbid moving the first column
			{
				LPNMHEADER pHeader = reinterpret_cast<LPNMHEADER>(lParam);
				if (pHeader->iItem != 0 && pHeader->pitem->iOrder != 0) {
					int iNewLoc = pHeader->pitem->iOrder - GetHiddenColumnCount();
					if (iNewLoc > 0) {
						int iOldLoc = m_aColumns[pHeader->iItem].iLocation;
						if (iOldLoc != iNewLoc) {
							if (iOldLoc > iNewLoc) {
								int iMax = iOldLoc;
								int iMin = iNewLoc;
								for (int i = 0; i < m_iColumnsTracked; ++i) {
									if (m_aColumns[i].iLocation >= iMin && m_aColumns[i].iLocation < iMax)
										++m_aColumns[i].iLocation;
								}
							} else { //iOldLoc < iNewLoc
								int iMin = iOldLoc;
								int iMax = iNewLoc;
								for (int i = 0; i < m_iColumnsTracked; ++i) {
									if (m_aColumns[i].iLocation > iMin && m_aColumns[i].iLocation <= iMax)
										--m_aColumns[i].iLocation;
								}
							}
							m_aColumns[pHeader->iItem].iLocation = iNewLoc;

							Invalidate(FALSE);
							break;
						}
					}
				}
			}
			return (BOOL)(*pResult = TRUE);
		case HDN_DIVIDERDBLCLICKA:
		case HDN_DIVIDERDBLCLICKW:
			// The effect of LVSCW_AUTOSIZE_USEHEADER is as follows:
			//	If the listview control can query for all the items in a column, it is
			//	capable of computing the minimal width needed to display the item with
			//	the largest width. However, if the width of the header label is larger
			//	then the largest width of the items in a column, the width of the header label
			//	will overrule the width which would be needed for the items in the column.
			//	In practice this means, that the column could get larger than really needed
			//	for the items in the column (just because the width gets adjusted for
			//	showing the header label also).
			//	This is a good solution for some of our listviews which do not (yet) provide
			//	the according functions which would give the listview control the chance to
			//	query for all items in a column. This flag will thus lead to sizing the
			//	column at least to the width of the header label. That's at least better
			//	than resizing the column to zero width (which would be the alternative).
			//
			// Though, a few of our listviews are already capable of providing all the
			// information which is needed by the listview control to properly auto size
			// a column. Those listviews can set the 'm_iAutoSizeWidth' to 'LVSCW_AUTOSIZE'
			// which will lead to standard Windows behaviour.
			//
			if (GetStyle() & LVS_OWNERDRAWFIXED) {
				LPNMHEADER pHeader = reinterpret_cast<LPNMHEADER>(lParam);
				// If the listview is empty, the LVSCW_AUTOSIZE_USEHEADER is more appropriate, even if
				// some listview has requested LVSCW_AUTOSIZE.
				SetColumnWidth(pHeader->iItem, GetItemCount() == 0 ? LVSCW_AUTOSIZE_USEHEADER : m_iAutoSizeWidth);
				return (BOOL)(*pResult = TRUE);
			}
		}
		break;
	case WM_COMMAND:
		//deal with menu clicks
		if (wParam == MLC_IDC_UPDATE) {
			UpdateLocation((int)lParam);
			return (BOOL)(*pResult = TRUE);
		}
		if (wParam >= MLC_IDC_MENU) {
			int iCount = GetHeaderCtrl()->GetItemCount();
			int iToggle = (int)(wParam - MLC_IDC_MENU);
			if (iToggle < iCount) {
				if (m_aColumns[iToggle].bHidden)
					ShowColumn(iToggle);
				else
					HideColumn(iToggle);

				return (BOOL)(*pResult = TRUE);
			}
		}
		break;
	case LVM_DELETECOLUMN:
		if (m_aColumns != NULL) {
			for (int i = 0; i < m_iColumnsTracked; ++i)
				if (m_aColumns[i].bHidden)
					ShowColumn(i);

			delete[] m_aColumns;
			m_aColumns = NULL; // 'new' may throw an exception
		}
		m_aColumns = new MULE_COLUMN[--m_iColumnsTracked];
		for (int i = 0; i < m_iColumnsTracked; ++i) {
			m_aColumns[i].iLocation = i;
			m_aColumns[i].bHidden = false;
		}
		break;
	case LVM_INSERTCOLUMNA:
	case LVM_INSERTCOLUMNW:
		if (m_aColumns != NULL) {
			for (int i = 0; i < m_iColumnsTracked; ++i)
				if (m_aColumns[i].bHidden)
					ShowColumn(i);

			delete[] m_aColumns;
			m_aColumns = NULL; // 'new' may throw an exception
		}
		m_aColumns = new MULE_COLUMN[++m_iColumnsTracked];
		for (int i = 0; i < m_iColumnsTracked; ++i) {
			m_aColumns[i].iLocation = i;
			m_aColumns[i].bHidden = false;
		}
		break;
	case LVM_SETITEM:
		{
			POSITION pos = m_Params.FindIndex(reinterpret_cast<LPLVITEM>(lParam)->iItem);
			if (pos) {
				m_Params.SetAt(pos, MLC_MAGIC);
				if (m_eUpdateMode == lazy)
					PostMessage(LVM_UPDATE, reinterpret_cast<LPLVITEM>(lParam)->iItem);
				else if (m_eUpdateMode == direct)
					UpdateLocation(reinterpret_cast<LPLVITEM>(lParam)->iItem);
			}
		}
		break;
	case LVN_KEYDOWN:
		break;
	case LVM_SETITEMTEXT:
		//need to check for movement
		*pResult = DefWindowProc(message, wParam, lParam);
		if (*pResult) {
			if (m_eUpdateMode == lazy)
				PostMessage(WM_COMMAND, MLC_IDC_UPDATE, wParam);
			else if (m_eUpdateMode == direct)
				UpdateLocation((int)wParam);
		}
		return *pResult != 0;
	case LVM_SORTITEMS:
		m_dwParamSort = (LPARAM)wParam;
		UpdateSortHistory(m_dwParamSort);
		m_SortProc = (PFNLVCOMPARE)lParam;

		// Hook our own callback for automatic layered sorting
		lParam = (LPARAM)MultiSortCallback;
		wParam = (WPARAM)this;

		for (POSITION pos = m_Params.GetHeadPosition(); pos != NULL; m_Params.GetNext(pos))
			m_Params.SetAt(pos, MLC_MAGIC);
		break;
	case LVM_DELETEALLITEMS:
		BeginListModelGeneration(0, true);
		if (!CListCtrl::OnWndMsg(message, wParam, lParam, pResult) && DefWindowProc(message, wParam, lParam))
			m_Params.RemoveAll();
		return (BOOL)(*pResult = TRUE);
	case LVM_DELETEITEM:
		BeginListModelGeneration(0, true);
		MLC_ASSERT(m_Params.GetAt(m_Params.FindIndex(wParam)) == CListCtrl::GetItemData(wParam));
		if (!CListCtrl::OnWndMsg(message, wParam, lParam, pResult) && DefWindowProc(message, wParam, lParam))
			m_Params.RemoveAt(m_Params.FindIndex(wParam));
		return (BOOL)(*pResult = TRUE);
	case LVM_INSERTITEMA:
	case LVM_INSERTITEMW:
		//try to fix position of inserted items
		{
			LPLVITEM pItem = reinterpret_cast<LPLVITEM>(lParam);
			if (m_uBulkInsertDepth > 0) {
				LRESULT lResult = DefWindowProc(message, wParam, lParam);
				if (lResult != -1) {
					if (lResult <= 0)
						m_Params.AddHead(pItem->lParam);
					else if (lResult >= static_cast<LRESULT>(m_Params.GetCount()))
						m_Params.AddTail(pItem->lParam);
					else {
						POSITION posPrev = m_Params.FindIndex(static_cast<int>(lResult) - 1);
						if (posPrev != NULL)
							m_Params.InsertAfter(posPrev, pItem->lParam);
						else {
							ASSERT(0);
							m_Params.AddTail(pItem->lParam);
						}
					}
				}
				*pResult = lResult;
				return TRUE;
			}

			int iItem = pItem->iItem;
			int iItemCount = GetItemCount();
			BOOL notLast = iItem < iItemCount;
			BOOL notFirst = iItem > 0;

			if (notFirst) {
				int iNewIndex = iItem - 1;
				POSITION pos = m_Params.FindIndex(iNewIndex);
				int iResult = MultiSortProc(pItem->lParam, GetParamAt(pos, iNewIndex));
				if (iResult < 0) {
					POSITION posPrev = pos;
					for (int iDist = iNewIndex / 2; iDist > 1;) {
						for (int i = 0; i < iDist; ++i)
							m_Params.GetPrev(posPrev);

						if (MultiSortProc(pItem->lParam, GetParamAt(posPrev, iNewIndex - iDist)) < 0) {
							iNewIndex = iNewIndex - iDist;
							pos = posPrev;
						} else
							posPrev = pos;

						iDist /= 2;
					}
					while (--iNewIndex >= 0) {
						m_Params.GetPrev(pos);
						if (MultiSortProc(pItem->lParam, GetParamAt(pos, iNewIndex)) >= 0)
							break;
					}
					pItem->iItem = iNewIndex + 1;
					notLast = false;
				}
			}

			if (notLast) {
				int iNewIndex = iItem;
				POSITION pos = m_Params.FindIndex(iNewIndex);
				int iResult = MultiSortProc(pItem->lParam, GetParamAt(pos, iNewIndex));
				if (iResult > 0) {
					POSITION posNext = pos;
					int iDist = (GetItemCount() - iNewIndex) / 2;
					while (iDist > 1) {
						for (int i = 0; i < iDist; ++i)
							m_Params.GetNext(posNext);

						if (MultiSortProc(pItem->lParam, GetParamAt(posNext, iNewIndex + iDist)) > 0) {
							iNewIndex = iNewIndex + iDist;
							pos = posNext;
						} else
							posNext = pos;

						iDist /= 2;
					}
					while (++iNewIndex < iItemCount) {
						m_Params.GetNext(pos);
						if (MultiSortProc(pItem->lParam, GetParamAt(pos, iNewIndex)) <= 0)
							break;
					}
					pItem->iItem = iNewIndex;
				}
			}

			if (pItem->iItem == 0) {
				m_Params.AddHead(pItem->lParam);
				return FALSE;
			}

			LRESULT lResult = DefWindowProc(message, wParam, lParam);
			if (lResult != -1) {
				if (lResult >= GetItemCount())
					m_Params.AddTail(pItem->lParam);
				else if (lResult == 0)
					m_Params.AddHead(pItem->lParam);
				else
					m_Params.InsertAfter(m_Params.FindIndex(lResult - 1), pItem->lParam);
			}
			return (BOOL)(*pResult = lResult);
		}
		case WM_DESTROY:
			HidePersistentInfoTip(true);
			SaveSettings();
			break;
		case WM_CANCELMODE:
		case WM_CAPTURECHANGED:
		case WM_KILLFOCUS:
		case WM_MOUSEWHEEL:
		case WM_VSCROLL:
		case WM_HSCROLL:
			HidePersistentInfoTip(true);
			break;
		case LVM_UPDATE:
			//better fix for old problem... normally Update(int) causes entire list to redraw
			if ((int)wParam == UpdateLocation((int)wParam)) { //no need to invalidate rect if item moved
				RECT rcItem;
			BOOL bResult = GetItemRect((int)wParam, &rcItem, LVIR_BOUNDS);
			if (bResult)
				InvalidateRect(&rcItem, FALSE);
			return (BOOL)(*pResult = bResult);
		}
		return (BOOL)(*pResult = TRUE);
		case WM_CONTEXTMENU:
			HidePersistentInfoTip(true);
			// If the context menu is opened with the _mouse_ and if it was opened _outside_
			// the client area of the list view, let Windows handle that message.
		// Otherwise we would prevent the context menu for e.g. scrollbars to be invoked.
		if ((HWND)wParam == m_hWnd) {
			CPoint ptMouse(lParam);
			if (ptMouse.x != -1 || ptMouse.y != -1) {
				ScreenToClient(&ptMouse);
				CRect rcClient;
				GetClientRect(&rcClient);
				if (!rcClient.PtInRect(ptMouse))
					return DefWindowProc(message, wParam, lParam) != 0;
			}
		}
	}

	return CListCtrl::OnWndMsg(message, wParam, lParam, pResult);
}

void CMuleListCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	switch(nChar){
	case VK_DELETE:
		PostMessage(WM_COMMAND, MPG_DELETE, 0);
		break;
	case VK_F2:
		PostMessage(WM_COMMAND, MPG_F2, 0);
		break;
	case VK_F3:
		if (m_bGeneralPurposeFind)
			if (GetKeyState(VK_SHIFT) & 0x8000)
				// Shift+F3: Search previous
				OnFindPrev();
			else
				// F3: Search next
				OnFindNext();
		break;
	case VK_F5:
		ReloadList(false, LSF_SELECTION);
		break;
	default:
		if (GetKeyState(VK_CONTROL) < 0)
			switch (nChar) {
			case 'A': // Ctrl+A: Select all items
				LVITEM theItem;
				theItem.mask = LVIF_STATE;
				theItem.iItem = -1;
				theItem.iSubItem = 0;
				theItem.state = LVIS_SELECTED;
				theItem.stateMask = 2;
				SetItemState(-1, &theItem);
				break;
			case 'C': // Ctrl+C: Copy key combo
				SendMessage(WM_COMMAND, MP_COPYSELECTED);
				break;
			case 'F': // Ctrl+F: Search item
				if (m_bGeneralPurposeFind)
					OnFindStart();
				break;
			case 'V': // Ctrl+V: Paste key combo
				SendMessage(WM_COMMAND, MP_PASTE);
				break;
			case 'X': // Ctrl+X: Cut key combo
				SendMessage(WM_COMMAND, MP_CUT);
			}
	}

	return CListCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

BOOL CMuleListCtrl::OnChildNotify(UINT message, WPARAM wParam, LPARAM lParam, LRESULT *pResult)
{
	if (message != WM_DRAWITEM) {
		if (message == WM_NOTIFY) {
			const LPNMHDR pNMHDR = reinterpret_cast<LPNMHDR>(lParam);
			CToolTipCtrl* pToolTip = GetToolTips();
			if (pNMHDR != NULL && pToolTip != NULL && pNMHDR->hwndFrom == pToolTip->GetSafeHwnd() && pNMHDR->code == TTN_SHOW) {
				if (HandleNativeInfoTipShow(pNMHDR->hwndFrom, pResult))
					return TRUE;
			}
		}

		//catch the prepaint and copy struct
		if (message == WM_NOTIFY && reinterpret_cast<LPNMHDR>(lParam)->code == NM_CUSTOMDRAW
			&& reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam)->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
		{
			m_bCustomDraw = CListCtrl::OnChildNotify(message, wParam, lParam, pResult);
			if (m_bCustomDraw)
				m_lvcd = *reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam);

			return m_bCustomDraw;
		}

		return CListCtrl::OnChildNotify(message, wParam, lParam, pResult);
	}

	ASSERT(pResult == NULL); // no return value expected

	DrawItem(reinterpret_cast<LPDRAWITEMSTRUCT>(lParam));
	return TRUE;
}

//Causes virtual artifacts when scrolling a virtual list

void CMuleListCtrl::InitItemMemDC(CMemoryDC *dc, LPDRAWITEMSTRUCT lpDrawItemStruct, BOOL &bCtrlFocused)
{
	bCtrlFocused = ((GetFocus() == this) || (GetStyle() & LVS_SHOWSELALWAYS));

	if (lpDrawItemStruct->itemState & ODS_SELECTED)
		dc->FillBackground(bCtrlFocused ? m_crHighlight : m_crNoHighlight);
	else {
		if (m_crWindowTextBk == CLR_NONE) {
			DefWindowProc(WM_ERASEBKGND, (WPARAM)(HDC)*dc, 0);
			dc->SetBkMode(TRANSPARENT);
		} else {
			ASSERT(m_crWindowTextBk == GetBkColor());
			dc->FillBackground(m_crWindowTextBk);
		}
	}

	dc->SetTextColor((lpDrawItemStruct->itemState & ODS_SELECTED) ? m_crHighlightText : m_crWindowText);
	dc->SetFont(GetFont());
}

void CMuleListCtrl::LocaliseHeaderCtrl(const LPCTSTR*const uids, size_t cnt)
{
	CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	HDITEM hdi;
	hdi.mask = HDI_TEXT;
	for (size_t i = 0; i < cnt; ++i)
		if (uids[i]) {
			const CString &sText(GetResString(uids[i]));
			hdi.pszText = const_cast<LPTSTR>((LPCTSTR)sText);
			pHeaderCtrl->SetItem((int)i, &hdi);
		}
}

void CMuleListCtrl::ShowOperationOverlay(const CString& strTitle, const CString& strDetail, UINT uDone, UINT uTotal, bool bCanCancel, const CString& strCancelText)
{
	const DWORD dwNow = ::GetTickCount();
	const bool bOverlayContentUnchanged = m_operationOverlay.strTitle == strTitle
		&& m_operationOverlay.strDetail == strDetail
		&& m_operationOverlay.strCancelText == strCancelText
		&& m_operationOverlay.uDone == uDone
		&& m_operationOverlay.uTotal == uTotal
		&& m_operationOverlay.bCanCancel == bCanCancel;
	if (m_operationOverlay.bVisible && bOverlayContentUnchanged)
		return;
	if (m_operationOverlay.bDelayedShow && bOverlayContentUnchanged && !IsOperationOverlayDelayElapsed(dwNow))
		return;

	const bool bWasVisible = m_operationOverlay.bVisible;
	if (!bWasVisible && !m_operationOverlay.bDelayedShow)
		m_operationOverlay.dwFirstUpdateTick = dwNow;
	else if (m_operationOverlay.dwFirstUpdateTick == 0)
		m_operationOverlay.dwFirstUpdateTick = dwNow;

	m_operationOverlay.strTitle = strTitle;
	m_operationOverlay.strDetail = strDetail;
	m_operationOverlay.strCancelText = strCancelText;
	m_operationOverlay.uDone = uDone;
	m_operationOverlay.uTotal = uTotal;
	m_operationOverlay.bCanCancel = bCanCancel;
	ApplyOperationOverlayVisibilityPolicy(bWasVisible, dwNow);
	if (!bWasVisible && m_operationOverlay.bVisible)
		m_operationOverlay.uAnimationPhase = 0;

	UpdateOperationOverlayWindow();
	InvalidateOperationOverlay();
}

void CMuleListCtrl::UpdateOperationOverlay(const CString& strTitle, const CString& strDetail, UINT uDone, UINT uTotal, bool bCanCancel)
{
	if (!m_operationOverlay.bVisible) {
		ShowOperationOverlay(strTitle, strDetail, uDone, uTotal, bCanCancel, GetResString(_T("BULKOP_CANCEL_REMAINING")));
		return;
	}

	if (m_operationOverlay.strTitle == strTitle && m_operationOverlay.strDetail == strDetail && m_operationOverlay.uDone == uDone && m_operationOverlay.uTotal == uTotal && m_operationOverlay.bCanCancel == bCanCancel)
		return;

	const DWORD dwNow = ::GetTickCount();
	const bool bWasVisible = m_operationOverlay.bVisible;
	m_operationOverlay.strTitle = strTitle;
	m_operationOverlay.strDetail = strDetail;
	m_operationOverlay.uDone = uDone;
	m_operationOverlay.uTotal = uTotal;
	m_operationOverlay.bCanCancel = bCanCancel;
	ApplyOperationOverlayVisibilityPolicy(bWasVisible, dwNow);
	UpdateOperationOverlayWindow();
	InvalidateOperationOverlay();
}

void CMuleListCtrl::HideOperationOverlay()
{
	CancelOperationOverlayDelayTimer();
	if (!m_operationOverlay.bVisible && !m_operationOverlay.bDelayedShow) {
		DestroyOperationOverlayWindow();
		return;
	}

	CRect rcInvalidate = m_operationOverlay.rcLastOverlay;
	CRect rcOverlay;
	if (GetOperationOverlayRect(rcOverlay))
		rcInvalidate = rcOverlay;

	m_operationOverlay = SOperationOverlayState();
	DestroyOperationOverlayWindow();
	if (!rcInvalidate.IsRectEmpty()) {
		rcInvalidate.InflateRect(4, 4);
		RedrawOperationOverlayArea(rcInvalidate, true);
	}
}

bool CMuleListCtrl::IsOperationOverlayVisible() const
{
	return m_operationOverlay.bVisible && !m_operationOverlay.bSuppressed;
}

void CMuleListCtrl::SetOperationOverlaySuppressed(bool bSuppressed)
{
	if (m_operationOverlay.bSuppressed == bSuppressed)
		return;

	CRect rcInvalidate = m_operationOverlay.rcLastOverlay;
	CRect rcOverlay;
	if (GetOperationOverlayRect(rcOverlay))
		rcInvalidate = rcOverlay;

	m_operationOverlay.bSuppressed = bSuppressed;
	UpdateOperationOverlayWindow();
	if (bSuppressed && !rcInvalidate.IsRectEmpty()) {
		rcInvalidate.InflateRect(4, 4);
		RedrawOperationOverlayArea(rcInvalidate, true);
	} else
		InvalidateOperationOverlay();
}

bool CMuleListCtrl::IsOperationOverlaySuppressed() const
{
	return m_operationOverlay.bSuppressed;
}

bool CMuleListCtrl::IsOperationOverlayDelayEligible(UINT uTotal) const
{
	return uTotal > 0 && uTotal < MULELISTCTRL_OPERATION_OVERLAY_IMMEDIATE_MIN_ITEMS;
}

bool CMuleListCtrl::IsOperationOverlayDelayElapsed(DWORD dwNow) const
{
	return m_operationOverlay.dwFirstUpdateTick != 0 && static_cast<DWORD>(dwNow - m_operationOverlay.dwFirstUpdateTick) >= MULELISTCTRL_OPERATION_OVERLAY_DELAY_MS;
}

void CMuleListCtrl::ApplyOperationOverlayVisibilityPolicy(bool bWasVisible, DWORD dwNow)
{
	if (bWasVisible || !IsOperationOverlayDelayEligible(m_operationOverlay.uTotal) || IsOperationOverlayDelayElapsed(dwNow)) {
		m_operationOverlay.bDelayedShow = false;
		m_operationOverlay.bVisible = true;
		CancelOperationOverlayDelayTimer();
		return;
	}

	m_operationOverlay.bVisible = false;
	m_operationOverlay.bDelayedShow = true;
	DestroyOperationOverlayWindow();
	ScheduleOperationOverlayDelayTimer(dwNow);
}

void CMuleListCtrl::ScheduleOperationOverlayDelayTimer(DWORD dwNow)
{
	if (!::IsWindow(m_hWnd) || !m_operationOverlay.bDelayedShow)
		return;

	DWORD dwDelay = MULELISTCTRL_OPERATION_OVERLAY_DELAY_MS;
	if (m_operationOverlay.dwFirstUpdateTick != 0) {
		const DWORD dwElapsed = static_cast<DWORD>(dwNow - m_operationOverlay.dwFirstUpdateTick);
		dwDelay = dwElapsed < MULELISTCTRL_OPERATION_OVERLAY_DELAY_MS ? MULELISTCTRL_OPERATION_OVERLAY_DELAY_MS - dwElapsed : 1;
	}
	SetTimer(MULELISTCTRL_OPERATION_OVERLAY_TIMER_ID, dwDelay != 0 ? static_cast<UINT>(dwDelay) : 1, NULL);
}

void CMuleListCtrl::CancelOperationOverlayDelayTimer()
{
	if (::IsWindow(m_hWnd))
		KillTimer(MULELISTCTRL_OPERATION_OVERLAY_TIMER_ID);
}

void CMuleListCtrl::TryShowDelayedOperationOverlay(DWORD dwNow)
{
	if (!m_operationOverlay.bDelayedShow)
		return;

	if (!IsOperationOverlayDelayElapsed(dwNow)) {
		ScheduleOperationOverlayDelayTimer(dwNow);
		return;
	}

	m_operationOverlay.bDelayedShow = false;
	m_operationOverlay.bVisible = true;
	m_operationOverlay.uAnimationPhase = 0;
	CancelOperationOverlayDelayTimer();
	UpdateOperationOverlayWindow();
	InvalidateOperationOverlay();
}

void CMuleListCtrl::EnsureOperationOverlayWindow()
{
	if (!::IsWindow(m_hWnd) || !m_operationOverlay.bVisible || m_operationOverlay.bSuppressed)
		return;

	ModifyStyle(0, WS_CLIPCHILDREN);
	if (m_pOperationOverlayWnd == NULL)
		m_pOperationOverlayWnd = new COperationOverlayWnd();

	if (m_pOperationOverlayWnd != NULL && m_pOperationOverlayWnd->CreateOverlay(this)) {
		m_pOperationOverlayWnd->EnsureAnimationTimer();
		m_pOperationOverlayWnd->UpdateFromOwner();
	}
}

void CMuleListCtrl::DestroyOperationOverlayWindow()
{
	if (m_pOperationOverlayWnd == NULL)
		return;

	if (::IsWindow(m_pOperationOverlayWnd->GetSafeHwnd()))
		m_pOperationOverlayWnd->DestroyWindow();
	delete m_pOperationOverlayWnd;
	m_pOperationOverlayWnd = NULL;
}

void CMuleListCtrl::UpdateOperationOverlayWindow()
{
	if (!::IsWindow(m_hWnd))
		return;

	if (!m_operationOverlay.bVisible || m_operationOverlay.bSuppressed) {
		DestroyOperationOverlayWindow();
		return;
	}

	EnsureOperationOverlayWindow();
}

void CMuleListCtrl::OnOperationOverlayCancel()
{
}

bool CMuleListCtrl::GetOperationOverlayRect(CRect& rcOverlay) const
{
	if (!m_operationOverlay.bVisible || m_operationOverlay.bSuppressed || !::IsWindow(m_hWnd))
		return false;

	CRect rcClient;
	GetClientRect(rcClient);
	if (rcClient.Width() < 180 || rcClient.Height() < 90)
		return false;

	const int iWidthByClient = rcClient.Width() - 32;
	int iWidth = min(MULELISTCTRL_OPERATION_OVERLAY_MAX_WIDTH, max(MULELISTCTRL_OPERATION_OVERLAY_MIN_WIDTH, (rcClient.Width() * 3) / 4));
	iWidth = min(iWidth, iWidthByClient);
	const int iHeight = min(MULELISTCTRL_OPERATION_OVERLAY_HEIGHT, rcClient.Height() - 16);
	const int iLeft = rcClient.left + (rcClient.Width() - iWidth) / 2;
	const int iBottom = rcClient.bottom - min(MULELISTCTRL_OPERATION_OVERLAY_BOTTOM_MARGIN, max(6, rcClient.Height() / 10));
	rcOverlay.SetRect(iLeft, iBottom - iHeight, iLeft + iWidth, iBottom);
	return !rcOverlay.IsRectEmpty();
}

bool CMuleListCtrl::GetOperationOverlayCancelRect(CRect& rcCancel) const
{
	CRect rcOverlay;
	if (!m_operationOverlay.bVisible || !m_operationOverlay.bCanCancel || !GetOperationOverlayRect(rcOverlay))
		return false;

	rcCancel = rcOverlay;
	rcCancel.DeflateRect(14, 12);
	rcCancel.left = max(rcCancel.left, rcCancel.right - 138);
	rcCancel.top = rcCancel.bottom - 24;
	return !rcCancel.IsRectEmpty();
}

void CMuleListCtrl::InvalidateOperationOverlay()
{
	if (!::IsWindow(m_hWnd))
		return;

	CRect rcOverlay;
	if (GetOperationOverlayRect(rcOverlay))
		m_operationOverlay.rcLastOverlay = rcOverlay;

	if (m_pOperationOverlayWnd != NULL && ::IsWindow(m_pOperationOverlayWnd->GetSafeHwnd()) && m_pOperationOverlayWnd->IsWindowVisible()) {
		m_pOperationOverlayWnd->Invalidate(FALSE);
		return;
	}

	CRect rcInvalidate = m_operationOverlay.rcLastOverlay;
	if (!rcInvalidate.IsRectEmpty()) {
		rcInvalidate.InflateRect(4, 4);
		RedrawOperationOverlayArea(rcInvalidate, false);
	}
}

void CMuleListCtrl::RedrawOperationOverlayArea(const CRect& rcInvalidate, bool bErase)
{
	if (!::IsWindow(m_hWnd) || rcInvalidate.IsRectEmpty())
		return;

	const UINT uFlags = RDW_INVALIDATE | RDW_NOCHILDREN | (bErase ? RDW_ERASE : 0);
	RedrawWindow(rcInvalidate, NULL, uFlags);
}

void CMuleListCtrl::DrawOperationOverlay(CDC *pDC)
{
	if (pDC == NULL || !m_operationOverlay.bVisible)
		return;

	CRect rcOverlay;
	if (!GetOperationOverlayRect(rcOverlay))
		return;

	const bool bDark = ((GetRValue(m_crWindow) + GetGValue(m_crWindow) + GetBValue(m_crWindow)) < 384);
	const COLORREF crPanel = bDark ? RGB(31, 34, 42) : RGB(248, 250, 255);
	const COLORREF crPanelEdge = bDark ? RGB(68, 73, 88) : RGB(206, 214, 230);
	const COLORREF crText = bDark ? RGB(224, 231, 250) : RGB(32, 48, 92);
	const COLORREF crTextSoft = bDark ? RGB(174, 188, 224) : RGB(55, 76, 145);
	const COLORREF crProgressBack = bDark ? RGB(53, 58, 70) : RGB(228, 233, 244);
	const COLORREF crProgressFill = RGB(76, 132, 232);
	const COLORREF crProgressFill2 = bDark ? RGB(111, 159, 255) : RGB(115, 164, 255);

	pDC->SetBkMode(TRANSPARENT);

	CRgn rgnPanel;
	rgnPanel.CreateRoundRectRgn(rcOverlay.left, rcOverlay.top, rcOverlay.right + 1, rcOverlay.bottom + 1, 14, 14);
	CBrush brPanel(crPanel);
	pDC->FillRgn(&rgnPanel, &brPanel);

	CPen penEdge(PS_SOLID, 1, crPanelEdge);
	CPen *pOldPen = pDC->SelectObject(&penEdge);
	CGdiObject *pOldBrush = pDC->SelectStockObject(NULL_BRUSH);
	pDC->RoundRect(rcOverlay, CPoint(14, 14));

	DrawAnimatedRainbowBorder(pDC, rcOverlay, m_operationOverlay.uAnimationPhase, 2, 14);

	CRect rcContent(rcOverlay);
	rcContent.DeflateRect(14, 10);
	CRect rcCancel;
	const bool bHasCancel = GetOperationOverlayCancelRect(rcCancel);
	CRect rcText(rcContent);
	if (bHasCancel)
		rcText.right = rcCancel.left - 12;

	CFont *pOldFont = pDC->SelectObject(GetFont());
	pDC->SetTextColor(crText);
	CRect rcTitle(rcText.left, rcText.top, rcText.right, rcText.top + 18);
	pDC->DrawText(m_operationOverlay.strTitle, rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

	pDC->SetTextColor(crTextSoft);
	CRect rcDetail(rcText.left, rcTitle.bottom + 1, rcText.right, rcTitle.bottom + 18);
	pDC->DrawText(m_operationOverlay.strDetail, rcDetail, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

	CRect rcProgress(rcText.left, rcDetail.bottom + 8, rcText.right, rcDetail.bottom + 20);
	if (rcProgress.Width() > 20) {
		pDC->FillSolidRect(rcProgress, crProgressBack);
		CPen penProgressEdge(PS_SOLID, 1, crPanelEdge);
		pDC->SelectObject(&penProgressEdge);
		pDC->MoveTo(rcProgress.left, rcProgress.top);
		pDC->LineTo(rcProgress.right, rcProgress.top);
		pDC->LineTo(rcProgress.right, rcProgress.bottom);
		pDC->LineTo(rcProgress.left, rcProgress.bottom);
		pDC->LineTo(rcProgress.left, rcProgress.top);

		if (m_operationOverlay.uTotal > 0) {
			const int iFillWidth = min(rcProgress.Width(), max(0, MulDiv(rcProgress.Width(), min(m_operationOverlay.uDone, m_operationOverlay.uTotal), m_operationOverlay.uTotal)));
			if (iFillWidth > 0) {
				CRect rcFill(rcProgress);
				rcFill.DeflateRect(1, 1);
				rcFill.right = rcFill.left + max(0, iFillWidth - 2);
				pDC->FillSolidRect(rcFill, crProgressFill);
				CRect rcFillTop(rcFill.left, rcFill.top, rcFill.right, rcFill.top + max(1, rcFill.Height() / 2));
				pDC->FillSolidRect(rcFillTop, crProgressFill2);
			}
		}
	}

	if (bHasCancel) {
		const COLORREF crButton = m_operationOverlay.bCancelHot ? (bDark ? RGB(64, 72, 96) : RGB(232, 238, 252)) : (bDark ? RGB(47, 52, 64) : RGB(242, 246, 255));
		const COLORREF crButtonEdge = m_operationOverlay.bCancelHot ? RGB(76, 132, 232) : crPanelEdge;
		CBrush brButton(crButton);
		CPen penButton(PS_SOLID, 1, crButtonEdge);
		pDC->SelectObject(&penButton);
		pDC->SelectObject(&brButton);
		pDC->RoundRect(rcCancel, CPoint(8, 8));
		pDC->SetTextColor(crText);
		pDC->DrawText(m_operationOverlay.strCancelText, rcCancel, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	}

	pDC->SelectObject(pOldFont);
	pDC->SelectObject(pOldPen);
	pDC->SelectObject(pOldBrush);
}


void CMuleListCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	//set up our flicker free drawing
	CRect rcItem(lpDrawItemStruct->rcItem);
	CRect rcClientFullRow;
	GetClientRect(&rcClientFullRow);
	CRect rcPaint(rcClientFullRow.left, rcItem.top, rcClientFullRow.right, rcItem.bottom);
	CMemoryDC pDC(CDC::FromHandle(lpDrawItemStruct->hDC), &rcPaint, m_crWindow);
	CFont *pOldFont = pDC->SelectObject(GetFont());
	RECT rcClient;
	GetClientRect(&rcClient);

	int iItem = lpDrawItemStruct->itemID;
	const int iItemCount = GetItemCount();
	if (iItem < 0 || iItem >= iItemCount) {
		pDC->FillSolidRect(&rcPaint, m_crWindowTextBk != CLR_NONE ? m_crWindowTextBk : m_crWindow);
		pDC->Flush();
		pDC->SelectObject(pOldFont);
		return;
	}

	//gets the item image and state info
	LVITEM lvi;
	lvi.mask = LVIF_IMAGE | LVIF_STATE;
	lvi.iItem = iItem;
	lvi.iSubItem = 0;
	lvi.stateMask = LVIS_DROPHILITED | LVIS_FOCUSED | LVIS_SELECTED | LVIS_GLOW;
	GetItem(&lvi);

	//see if the item is highlighted
	BOOL bHighlight = ((lvi.state & LVIS_DROPHILITED) || (lvi.state & LVIS_SELECTED));
	BOOL bCtrlFocused = ((GetFocus() == this) || (GetStyle() & LVS_SHOWSELALWAYS));
	BOOL bGlowing = (lvi.state & LVIS_GLOW);

	COLORREF clr;
	if (m_bCustomDraw)
		clr = (bHighlight && g_bLowColorDesktop) ? m_crHighlightText : m_lvcd.clrText;
	else
		clr = bHighlight ? m_crHighlightText : m_crWindowText;
	pDC->SetTextColor(clr);

	//get rectangles for drawing
	RECT rcBounds, rcLabel, rcIcon;
	GetItemRect(iItem, &rcBounds, LVIR_BOUNDS);
	GetItemRect(iItem, &rcLabel, LVIR_LABEL);
	GetItemRect(iItem, &rcIcon, LVIR_ICON);
	CRect rcCol(rcBounds);

		//draw the background color
	if (!bHighlight && !bGlowing && m_crWindowTextBk == CLR_NONE)
		DefWindowProc(WM_ERASEBKGND, (WPARAM)pDC->m_hDC, 0);
	else {
		if (bHighlight)
			if (bCtrlFocused)
				clr = GetCustomSysColor(COLOR_HIGHLIGHT);
			else
				clr = bGlowing ? m_crGlow : m_crNoHighlight;
		else
			clr = bGlowing ? m_crGlow : m_crWindow;
		pDC->FillSolidRect(&rcBounds, clr);
	}

	//update column
	rcCol.right = rcCol.left + GetColumnWidth(0);

	//draw state icon
	if (lvi.state & LVIS_STATEIMAGEMASK) {
		int nImage = ((lvi.state & LVIS_STATEIMAGEMASK) >> 12) - 1;
		CImageList *pImageList = GetImageList(LVSIL_STATE);
		if (pImageList)
			SafeImageListDraw(pImageList, pDC, nImage, rcCol.TopLeft(), ILD_TRANSPARENT);
	}

	//draw the item's icon
	CImageList *pImageList = GetImageList(LVSIL_SMALL);
	if (pImageList) {
		int iIconY = max((rcItem.Height() - 16) / 2, 0);
		const POINT point = { rcItem.left, rcItem.top + iIconY };
		SafeImageListDraw(pImageList, pDC, lvi.iImage, point, ILD_TRANSPARENT);
	}

	if (m_crWindowTextBk == CLR_NONE)
		pDC->SetBkMode(TRANSPARENT);

	//the label!
	const CString &sLabel0(GetItemText(iItem, 0));
	//labels are offset by a certain amount related to the width of a space character

	//draw item label (column 0)
	rcLabel.left += sm_iLabelOffset;
	rcLabel.right -= sm_iLabelOffset;
	pDC->DrawText(sLabel0, &rcLabel, MLC_DT_TEXT | DT_NOCLIP);

	//draw labels for remaining columns
	LVCOLUMN lvc;
	lvc.mask = LVCF_FMT | LVCF_WIDTH;

	const CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	int iCount = pHeaderCtrl->GetItemCount();
	for (int iCurrent = 1; iCurrent < iCount; ++iCurrent) {
		int iColumn = pHeaderCtrl->OrderToIndex(iCurrent);
		//don't draw column 0 again
		if (iColumn == 0)
			continue;

		GetColumn(iColumn, &lvc);
		//don't draw anything with 0 width
		if (lvc.cx <= 0)
			continue;

		rcCol.left = rcCol.right;
		rcCol.right += lvc.cx;

		if (HaveIntersection(rcClient, rcCol)) {
			const CString &sLabel(GetItemText(iItem, iColumn));
			if (sLabel.IsEmpty())
				continue;

			//get the text justification
			UINT nJustify;
			switch (lvc.fmt & LVCFMT_JUSTIFYMASK) {
			case LVCFMT_RIGHT:
				nJustify = DT_RIGHT;
				break;
			case LVCFMT_CENTER:
				nJustify = DT_CENTER;
				break;
			case LVCFMT_LEFT:
			default:
				nJustify = DT_LEFT;
			}

			//label text
			rcCol.left += sm_iLabelOffset + sm_iSubItemInset;
			rcCol.right -= sm_iLabelOffset + sm_iSubItemInset;
			pDC->SetBkColor(clr);
			pDC->DrawText(sLabel, &rcCol, MLC_DT_TEXT | nJustify);
			rcCol.right += sm_iLabelOffset + sm_iSubItemInset; //restore the position
		}
	}

	DrawFocusRect(pDC, &rcBounds, lvi.state & LVIS_FOCUSED, bCtrlFocused, lvi.state & LVIS_SELECTED);

	pDC->Flush();
	pDC->SelectObject(pOldFont);
}

void CMuleListCtrl::DrawFocusRect(CDC *pDC, LPCRECT rcItem, BOOL bItemFocused, BOOL bCtrlFocused, BOOL bItemSelected)
{
	//draw focus rectangle if the item has focus
	if (bItemFocused && (bCtrlFocused || bItemSelected)) {
		CBrush brush(bCtrlFocused && bItemSelected ? m_crFocusLine : m_crNoFocusLine);
		pDC->FrameRect(rcItem, &brush);
	}
}

BOOL CMuleListCtrl::OnEraseBkgnd(CDC *pDC)
{
	if (m_crWindowTextBk != CLR_NONE) {
		FillUnusedClientArea(pDC);
		return TRUE;
	}

	return CListCtrl::OnEraseBkgnd(pDC);
}

void CMuleListCtrl::FillUnusedClientArea(CDC *pDC)
{
	if (pDC == NULL || m_crWindowTextBk == CLR_NONE)
		return;

	CRect rcClient;
	GetClientRect(&rcClient);
	if (rcClient.IsRectEmpty())
		return;

	const COLORREF crBack = m_crWindowTextBk != CLR_NONE ? m_crWindowTextBk : m_crWindow;
	const int iItemCount = GetItemCount();
	if (iItemCount <= 0) {
		pDC->FillSolidRect(&rcClient, crBack);
		return;
	}

	const int iTopIndex = GetTopIndex();
	if (iTopIndex < 0 || iTopIndex >= iItemCount) {
		pDC->FillSolidRect(&rcClient, crBack);
		return;
	}

	int iLastVisibleBottom = rcClient.top;
	CRect rcFirstItem;
	if (GetItemRect(iTopIndex, &rcFirstItem, LVIR_BOUNDS) && rcFirstItem.top > rcClient.top) {
		CRect rcTop(rcClient.left, rcClient.top, rcClient.right, min(rcFirstItem.top, rcClient.bottom));
		if (!rcTop.IsRectEmpty())
			pDC->FillSolidRect(&rcTop, crBack);
	}

	const int iRowsPerPage = max(1, GetCountPerPage());
	const int iLastCandidate = min(iItemCount - 1, iTopIndex + iRowsPerPage + 2);
	for (int iItem = iTopIndex; iItem <= iLastCandidate; ++iItem) {
		CRect rcItem;
		if (!GetItemRect(iItem, &rcItem, LVIR_BOUNDS))
			continue;
		if (rcItem.bottom <= rcClient.top || rcItem.top >= rcClient.bottom)
			continue;
		iLastVisibleBottom = max(iLastVisibleBottom, min(rcItem.bottom, rcClient.bottom));
	}

	if (iLastVisibleBottom < rcClient.bottom) {
		CRect rcBottom(rcClient.left, max(rcClient.top, iLastVisibleBottom), rcClient.right, rcClient.bottom);
		if (!rcBottom.IsRectEmpty())
			pDC->FillSolidRect(&rcBottom, crBack);
	}

	CHeaderCtrl* pHeader = GetHeaderCtrl();
	if (pHeader != NULL && ::IsWindow(pHeader->GetSafeHwnd())) {
		int iColumnsRight = rcClient.left;
		const int iColumnCount = pHeader->GetItemCount();
		for (int iColumn = 0; iColumn < iColumnCount; ++iColumn)
			iColumnsRight += CListCtrl::GetColumnWidth(iColumn);
		if (iColumnsRight < rcClient.right) {
			CRect rcRight(max(rcClient.left, iColumnsRight), rcClient.top, rcClient.right, rcClient.bottom);
			if (!rcRight.IsRectEmpty())
				pDC->FillSolidRect(&rcRight, crBack);
		}
	}
}

void CMuleListCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_operationOverlay.bVisible && m_operationOverlay.bCanCancel) {
		CRect rcCancel;
		const bool bCancelHot = GetOperationOverlayCancelRect(rcCancel) && rcCancel.PtInRect(point);
		if (m_operationOverlay.bCancelHot != bCancelHot) {
			m_operationOverlay.bCancelHot = bCancelHot;
			InvalidateOperationOverlay();
		}
	}

	EnsureThemeAwareInfoTipCtrl();
	StopPersistentInfoTipLeaveTimer();
	EnsurePersistentInfoTipMouseLeaveTracking();

	if (m_bPersistentInfoTipVisible)
		RefreshPersistentInfoTipFromPoint(point);

	UpdatePersistentInfoTipTracking(point);

	CListCtrl::OnMouseMove(nFlags, point);
}

void CMuleListCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (m_operationOverlay.bVisible) {
		CRect rcCancel;
		if (GetOperationOverlayCancelRect(rcCancel) && rcCancel.PtInRect(point)) {
			OnOperationOverlayCancel();
			return;
		}

		CRect rcOverlay;
		if (GetOperationOverlayRect(rcOverlay) && rcOverlay.PtInRect(point))
			return;
	}

	CListCtrl::OnLButtonDown(nFlags, point);
}

BOOL CMuleListCtrl::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (m_operationOverlay.bVisible && m_operationOverlay.bCanCancel) {
		CPoint point;
		::GetCursorPos(&point);
		ScreenToClient(&point);
		CRect rcCancel;
		if (GetOperationOverlayCancelRect(rcCancel) && rcCancel.PtInRect(point)) {
			::SetCursor(::LoadCursor(NULL, IDC_HAND));
			return TRUE;
		}
	}

	return CListCtrl::OnSetCursor(pWnd, nHitTest, message);
}

void CMuleListCtrl::OnPaint()
{
	CPaintDC dc(this);
	DefWindowProc(WM_PAINT, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);
	FillUnusedClientArea(&dc);
	UpdateOperationOverlayWindow();
}

void CMuleListCtrl::OnSize(UINT nType, int cx, int cy)
{
	CListCtrl::OnSize(nType, cx, cy);
	UpdateOperationOverlayWindow();
}

void CMuleListCtrl::OnSysColorChange()
{
	//adjust colors
	CListCtrl::OnSysColorChange();
	RefreshThemeColors();

	if (thePrefs.GetUseSystemFontForMainControls()) {
		// Send a (useless) WM_WINDOWPOSCHANGED to the listview control to trigger a
		// WM_MEASUREITEM message which is needed to set the new item height in case
		// there was a font changed in the Windows System settings.
		//
		// Though it does not work as expected. Although we get the WM_MEASUREITEM and although
		// we return the correct (new) item height, the listview control does not redraw the
		// items with the new height until the control really gets resized.
		CRect rc;
		GetWindowRect(rc);
		WINDOWPOS wp;
		wp.hwnd = m_hWnd;
		wp.cx = rc.Width();
		wp.cy = rc.Height();
		wp.flags = SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_FRAMECHANGED;
		SendMessage(WM_WINDOWPOSCHANGED, 0, (LPARAM)&wp);
	}
}

LRESULT CMuleListCtrl::OnMouseLeave(WPARAM, LPARAM)
{
	if (m_operationOverlay.bCancelHot) {
		m_operationOverlay.bCancelHot = false;
		InvalidateOperationOverlay();
	}

	ResetPendingPersistentInfoTip();
	m_bTrackingMouseLeave = false;

	if (m_bPersistentInfoTipVisible && ShouldKeepPersistentInfoTipVisibleOnMouseLeave()) {
		StartPersistentInfoTipLeaveTimer();
		return 0;
	}

	HidePersistentInfoTip(true);
	return 0;
}

LRESULT CMuleListCtrl::OnMouseHover(WPARAM, LPARAM lParam)
{
	m_bTrackingMouseHover = false;

	const CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	SPersistentInfoTipContext context;
	CString strText;
	if (!TryGetPersistentInfoTip(point, context, &strText))
		return 0;

	if (!IsSamePersistentInfoTipTarget(context, m_PendingInfoTipContext))
		return 0;
	if (!context.bExplicitTip && !IsPointWithinPersistentInfoTipHotArea(point, context)) {
		ResetPendingPersistentInfoTip();
		return 0;
	}

	ShowPersistentInfoTip(context, strText);
	ResetPendingPersistentInfoTip();
	return 0;
}

void CMuleListCtrl::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == MULELISTCTRL_PERSISTENT_INFOTIP_LEAVE_TIMER_ID) {
		if (!m_bPersistentInfoTipVisible || !ShouldKeepPersistentInfoTipVisibleOnMouseLeave()) {
			HidePersistentInfoTip(true);
			return;
		}
		return;
	}

	if (nIDEvent == MULELISTCTRL_OPERATION_OVERLAY_TIMER_ID) {
		TryShowDelayedOperationOverlay(::GetTickCount());
		return;
	}

	CListCtrl::OnTimer(nIDEvent);
}

void CMuleListCtrl::MeasureItem(LPMEASUREITEMSTRUCT lpMeasureItemStruct)
{
	Default();

	if (thePrefs.GetUseSystemFontForMainControls()) {
		CDC *pDC = GetDC();
		if (pDC) {
			CFont *pFont = GetFont();
			if (pFont) {
				CFont *pFontOld = pDC->SelectObject(pFont);
				TEXTMETRIC tm;
				pDC->GetTextMetrics(&tm);
				int iNewHeight = tm.tmHeight + tm.tmExternalLeading + 1;
				lpMeasureItemStruct->itemHeight = max(18, iNewHeight);
				pDC->SelectObject(pFontOld);
			}
			ReleaseDC(pDC);
		}
	}
}

HIMAGELIST CMuleListCtrl::ApplyImageList(HIMAGELIST himl)
{
	HIMAGELIST himlOld = (HIMAGELIST)SendMessage(LVM_SETIMAGELIST, LVSIL_SMALL, (LPARAM)himl);
	if (m_imlHeaderCtrl.m_hImageList != NULL) {
		// Must *again* set the image list for the header control, because LVM_SETIMAGELIST
		// always resets any already specified header control image lists!
		GetHeaderCtrl()->SetImageList(&m_imlHeaderCtrl);
	}
	return himlOld;
}

void CMuleListCtrl::DoFind(int iStartItem, int iDirection /*1 = down, -1 = up*/, BOOL bShowError)
{
	if (iStartItem < 0) {
		MessageBeep(MB_OK);
		return;
	}

	const int iNumItems = (iDirection > 0) ? GetItemCount() : 0;
	for (int iItem = iStartItem; ((iDirection > 0) ? iItem < iNumItems : iItem >= 0);) {
		const CString &strItemText(GetItemText(iItem, m_iFindColumn));
		if (!strItemText.IsEmpty()) {
			if ((m_bFindMatchCase ? _tcsstr(strItemText, m_strFindText) : stristr(strItemText, m_strFindText)) != NULL) {
				// Deselect all listview entries
				SetItemState(-1, 0, LVIS_SELECTED);

				// Select the found listview entry
				SetItemState(iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
				SetSelectionMark(iItem);
				EnsureVisible(iItem, FALSE/*bPartialOK*/);
				SetFocus();

				return;
			}
		}

		iItem += iDirection;
	}

	if (bShowError)
		LocMessageBox(_T("SEARCH_NORESULT"), MB_ICONINFORMATION, 0);
	else
		MessageBeep(MB_OK);
}

void CMuleListCtrl::OnFindStart()
{
	if (GetItemCount() == 0) {
		MessageBeep(MB_OK);
		return;
	}

	CListViewSearchDlg dlg;
	dlg.m_pListView = this;
	dlg.m_strFindText = m_strFindText;
	dlg.m_bCanSearchInAllColumns = m_bCanSearchInAllColumns;
	dlg.m_iSearchColumn = m_iFindColumn;
	if (dlg.DoModal() == IDOK && !dlg.m_strFindText.IsEmpty()) {
		m_strFindText = dlg.m_strFindText;
		m_iFindColumn = dlg.m_iSearchColumn;
		DoFindNext(TRUE/*bShowError*/);
	}
}

void CMuleListCtrl::OnFindNext()
{
	if (GetItemCount() == 0) {
		MessageBeep(MB_OK);
		return;
	}

	DoFindNext(FALSE/*bShowError*/);
}

void CMuleListCtrl::DoFindNext(BOOL bShowError)
{
	int iStartItem = GetNextItem(-1, LVNI_SELECTED | LVNI_FOCUSED);
	if (iStartItem < 0)
		iStartItem = 0;
	else
		iStartItem += m_iFindDirection;
	DoFind(iStartItem, m_iFindDirection, bShowError);
}

void CMuleListCtrl::OnFindPrev()
{
	if (GetItemCount() == 0) {
		MessageBeep(MB_OK);
		return;
	}

	int iStartItem = GetNextItem(-1, LVNI_SELECTED | LVNI_FOCUSED);
	if (iStartItem < 0)
		iStartItem = 0;
	else
		iStartItem -= m_iFindDirection;

	DoFind(iStartItem, -m_iFindDirection, FALSE/*bShowError*/);
}

BOOL CMuleListCtrl::PreTranslateMessage(MSG *pMsg)
{
	if (pMsg->message == WM_SYSKEYDOWN && pMsg->wParam == VK_RETURN && GetKeyState(VK_MENU) < 0) {
		PostMessage(WM_COMMAND, MPG_ALTENTER, 0);
		return TRUE;
	}

	if (m_hAccel != NULL && pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST)
		// If we want to handle the VK_RETURN key, we have to do that via accelerators!
		if (TranslateAccelerator(m_hWnd, m_hAccel, pMsg))
			return TRUE;

	// Catch the "Ctrl+<NumPad_Plus_Key>" shortcut. CMuleListCtrl can not handle this.
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_ADD && GetKeyState(VK_CONTROL) < 0)
		return TRUE;

	return CListCtrl::PreTranslateMessage(pMsg);
}

void CMuleListCtrl::AutoSelectItem()
{
	if (GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED) < 0) {
		int iItem = GetNextItem(-1, LVIS_FOCUSED);
		if (iItem >= 0) {
			SetItemState(iItem, LVIS_SELECTED, LVIS_SELECTED);
			SetSelectionMark(iItem);
		}
	}
}

// Parameter is used as a LONG value
// LOWORD(dwNewOrder) - sort item, HIWORD(dwNewOrder) - sort direction
// HIWORD(dwNewOrder) != 0 means inverse order (descending usually)
void CMuleListCtrl::UpdateSortHistory(LPARAM dwNewOrder)
{
	// delete all history for the item, for both direct and inverse order
	for (POSITION pos = m_liSortHistory.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		if (LOWORD(m_liSortHistory.GetNext(pos)) == LOWORD(dwNewOrder))
			m_liSortHistory.RemoveAt(pos2);
	}
	m_liSortHistory.AddHead((LONG)dwNewOrder);
	if (GetMaxSortHistory()) // GetUITweaksMaxSortHistory == 0 means unlimited sort order history
		while (m_liSortHistory.GetCount() > GetMaxSortHistory())
			m_liSortHistory.RemoveTail();
}

LPARAM CMuleListCtrl::GetNextSortOrder(LPARAM iCurrentSortOrder) const
{
	POSITION pos = m_liSortHistory.Find((LONG)iCurrentSortOrder);
	if (pos) {
		m_liSortHistory.GetNext(pos);
		if (pos)
			return m_liSortHistory.GetAt(pos);
	}
	return -1; // there are no more stored sort orders
}

CMuleListCtrl::EUpdateMode CMuleListCtrl::SetUpdateMode(EUpdateMode eUpdateMode)
{
	EUpdateMode eCurUpdateMode = m_eUpdateMode;
	m_eUpdateMode = eUpdateMode;
	return eCurUpdateMode;
}

void CMuleListCtrl::BeginBulkInsert()
{
	++m_uBulkInsertDepth;
}

void CMuleListCtrl::EndBulkInsert()
{
	if (m_uBulkInsertDepth > 0)
		--m_uBulkInsertDepth;
	else
		ASSERT(0);
}

void CMuleListCtrl::OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult)
{
	// NOTE: Using 'Info Tips' for owner drawn list view controls (like almost all instances
	// of the CMuleListCtrl) gives potentially *wrong* results. One may and will experience
	// several situations where a tooltip should be shown and none will be shown. This is
	// because the Windows list view control code does not know anything about what the
	// owner drawn list view control was actually drawing. So, the Windows list view control
	// code is just *assuming* that the owner drawn list view control instance is using the
	// same drawing metrics as the Windows control. Because our owner drawn list view controls
	// almost always draw an additional icon before the actual item text and because the
	// Windows control does not know that, the calculations performed by the Windows control
	// regarding folded/unfolded items are in couple of cases wrong. E.g. because the Windows
	// control does not know about the additional icon and thus about the reduced space used
	// for drawing the item text, we may show folded item texts while the Windows control is
	// still assuming that we show the full text -> thus we will not receive a precomputed
	// info tip which contains the unfolded item text. Result: We would have to implement
	// our own info tip processing.
	LPNMLVGETINFOTIP pGetInfoTip = reinterpret_cast<LPNMLVGETINFOTIP>(pNMHDR);
	LVHITTESTINFO hti;
	if (pGetInfoTip->iSubItem == 0 && ::GetCursorPos(&hti.pt)) {
		ScreenToClient(&hti.pt);
		if (SubItemHitTest(&hti) < 0 || hti.iItem != pGetInfoTip->iItem || hti.iSubItem != 0) {
			// Don't show the default label tip for the main item, if the mouse is not over
			// the main item.
			if ((pGetInfoTip->dwFlags & LVGIT_UNFOLDED) == 0 && pGetInfoTip->cchTextMax > 0 && pGetInfoTip->pszText[0] != _T('\0')) {
				// For any reason this does not work with Win98 (COMCTL32 v5.8). Even when
				// the info tip text is explicitly set to empty, the list view control may
				// display the unfolded text for the 1st item. It works for WinXP though.
				pGetInfoTip->pszText[0] = _T('\0');
			}
			return;
		}
	}

	if (ShouldSuppressDefaultInfoTip(pGetInfoTip) && pGetInfoTip->cchTextMax > 0 && pGetInfoTip->pszText != NULL)
		pGetInfoTip->pszText[0] = _T('\0');
	*pResult = 0;
}


int CMuleListCtrl::InsertColumn(int nCol, LPCTSTR lpszColumnHeading, int nFormat, int nWidth, int nSubItem, bool bHiddenByDefault)
{
	if (bHiddenByDefault)
		m_liDefaultHiddenColumns.AddTail(nCol);
	return CListCtrl::InsertColumn(nCol, lpszColumnHeading, nFormat, nWidth, nSubItem);
}
