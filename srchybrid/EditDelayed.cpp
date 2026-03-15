// EditDelayed.cpp
#include "stdafx.h"
#include "EditDelayed.h"
#include "UserMsgs.h"
#include "emule.h"
#include "MenuCmds.h"
#include "OtherFunctions.h"
#include "eMuleAI/DarkMode.h"
#include "eMuleAI/MenuXP.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define DELAYED_EVALUATE_TIMER_ID	1
#define ICON_SIZE				16
#define ICON_SPACING			2
#define PASTE_ICON_LEFT			0
#define RESET_ICON_LEFT			(PASTE_ICON_LEFT + ICON_SIZE + ICON_SPACING)
#define COLUMN_ICON_LEFT		(RESET_ICON_LEFT + ICON_SIZE + ICON_SPACING)

IMPLEMENT_DYNAMIC(CEditDelayed, CEdit)

BEGIN_MESSAGE_MAP(CEditDelayed, CEdit)
	ON_WM_SETFOCUS()
	ON_WM_KILLFOCUS()
	ON_WM_TIMER()
	ON_CONTROL_REFLECT(EN_CHANGE, OnEnChange)
	ON_WM_DESTROY()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_SETCURSOR()
	ON_WM_MOUSEMOVE()
	ON_WM_SIZE()
	ON_WM_CTLCOLOR_REFLECT()
END_MESSAGE_MAP()

CEditDelayed::CEditDelayed()
	: m_bShuttingDown()
	, m_uTimerResult()
	, m_dwLastModified()
	, m_hCursor()
	, m_bShowResetButton()
	, m_bShowsColumnText()
	, m_nCurrentColumnIdx()
	, m_pctrlColumnHeader()
{
}

void CEditDelayed::OnDestroy()
{
	if (m_uTimerResult != 0)
		VERIFY(KillTimer(DELAYED_EVALUATE_TIMER_ID));
	m_bShuttingDown = true;
	CEdit::OnDestroy();
}

void CEditDelayed::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == DELAYED_EVALUATE_TIMER_ID) {
		const DWORD curTick = ::GetTickCount();
		if (curTick >= m_dwLastModified + 400) {
			DoDelayedEvalute();
			m_dwLastModified = curTick;
		}
	}
	CEdit::OnTimer(nIDEvent);
}

void CEditDelayed::OnSetFocus(CWnd* pOldWnd)
{
	CEdit::OnSetFocus(pOldWnd);
	if (!m_bShuttingDown) {
		ASSERT(m_uTimerResult == 0);
		m_uTimerResult = SetTimer(DELAYED_EVALUATE_TIMER_ID, 100, NULL);
		ASSERT(m_uTimerResult);
		ShowColumnText(false);
	}
}

void CEditDelayed::OnKillFocus(CWnd* pNewWnd)
{
	if (!m_bShuttingDown) {
		ASSERT(m_uTimerResult);
		VERIFY(KillTimer(DELAYED_EVALUATE_TIMER_ID));
		m_uTimerResult = 0;
		DoDelayedEvalute();
		if (GetWindowTextLength() == 0)
			ShowColumnText(true);
	}
	CEdit::OnKillFocus(pNewWnd);
}

void CEditDelayed::OnEnChange()
{
	if (m_uTimerResult != 0) {
		// Edit control contents were changed while the control was active (had focus)
		ASSERT(GetFocus() == this);
		m_dwLastModified = ::GetTickCount();
	} else {
		// Edit control contents were changed while the control was not active (e.g.
		// someone called 'SetWindowText' from within an other window).
		ASSERT(GetFocus() != this);
		DoDelayedEvalute();
	}
}

void CEditDelayed::DoDelayedEvalute(bool bForce)
{
	if (m_bShowsColumnText) {
		ASSERT(0);
		return;
	}
	CString strContent;
	GetWindowText(strContent);
	if (m_strLastEvaluatedContent != strContent || bForce) {
		m_strLastEvaluatedContent = strContent;
		SFilterParam* wParam = new SFilterParam;
		wParam->bForceApply = false;
		wParam->uColumnIndex = (uint32)m_nCurrentColumnIdx;
		GetParent()->SendMessage(UM_DELAYED_EVALUATE, (WPARAM)wParam, (LPARAM)(LPCTSTR)m_strLastEvaluatedContent);
	}
}

void CEditDelayed::OnInit(CHeaderCtrl* pColumnHeader, CArray<int, int>* paIgnoredColumns)
{
	RECT rectWindow;
	GetClientRect(&rectWindow);
	m_pctrlColumnHeader = pColumnHeader;
	m_hCursor = ::LoadCursor(NULL, IDC_ARROW);
	m_nCurrentColumnIdx = 0;
	CImageList* pImageList = new CImageList();
	pImageList->Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	pImageList->Add(CTempIconLoader(_T("SEARCHEDIT")));
	m_iwColumn.SetImageList(pImageList);
	m_iwColumn.Create(EMPTY, WS_CHILD | WS_VISIBLE, CRect(COLUMN_ICON_LEFT, 0, COLUMN_ICON_LEFT + ICON_SIZE, rectWindow.bottom), this, 1);
	pImageList = new CImageList();
	pImageList->Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	pImageList->Add(CTempIconLoader(_T("CLEAR")));
	m_iwReset.SetImageList(pImageList);
	m_iwReset.Create(EMPTY, WS_CHILD | WS_VISIBLE, CRect(RESET_ICON_LEFT, 0, RESET_ICON_LEFT + ICON_SIZE, rectWindow.bottom), this, 1);
	pImageList = new CImageList();
	pImageList->Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	pImageList->Add(CTempIconLoader(_T("CLIPBOARD")));
	m_iwPaste.SetImageList(pImageList);
	m_iwPaste.Create(EMPTY, WS_CHILD | WS_VISIBLE, CRect(PASTE_ICON_LEFT, 0, PASTE_ICON_LEFT + ICON_SIZE, rectWindow.bottom), this, 1);
	if (paIgnoredColumns != NULL)
		m_aIgnoredColumns.Copy(*paIgnoredColumns);
	ShowColumnText(true);
}

void CEditDelayed::SetEditRect()
{	
	RECT editRect;
	GetClientRect(&editRect);
	editRect.left += (ICON_SIZE * 3 + ICON_SPACING * 2 + 3);
	editRect.top = 2; // Vertical offset for a better vertical aligment of text
	SetRect(&editRect);
	m_iwPaste.MoveWindow(PASTE_ICON_LEFT, 0, ICON_SIZE, editRect.bottom);
	m_iwReset.MoveWindow(RESET_ICON_LEFT, 0, ICON_SIZE, editRect.bottom);
	m_iwColumn.MoveWindow(COLUMN_ICON_LEFT, 0, ICON_SIZE, editRect.bottom);
}

void CEditDelayed::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (m_pctrlColumnHeader != NULL) {
		if (point.x >= COLUMN_ICON_LEFT && point.x <= COLUMN_ICON_LEFT + ICON_SIZE) {
			CMenuXP menu;
			menu.CreatePopupMenu();
			TCHAR szBuffer[256];
			HDITEM hdi;
			hdi.mask = HDI_TEXT | HDI_WIDTH;
			hdi.pszText = szBuffer;
			hdi.cchTextMax = _countof(szBuffer);
			int nCount = m_pctrlColumnHeader->GetItemCount();
			for (int i = 0; i < nCount; ++i) {
				int nIdx = m_pctrlColumnHeader->OrderToIndex(i);
				if (m_pctrlColumnHeader->GetItem(nIdx, &hdi)) {
					bool bVisible = true;
					for (INT_PTR j = m_aIgnoredColumns.GetCount(); --j >= 0;)
						if (m_aIgnoredColumns[j] == nIdx) {
							bVisible = false;
							break;
						}
					if (hdi.cxy > 0 && bVisible)
						menu.AppendMenu(MF_STRING | ((m_nCurrentColumnIdx == nIdx) ? MF_CHECKED : MF_UNCHECKED), MP_FILTERCOLUMNS + nIdx, hdi.pszText);
				}
			}
			RECT editRect;
			GetClientRect(&editRect);
			POINT pointMenu = { 2, editRect.bottom };
			ClientToScreen(&pointMenu);
			menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pointMenu.x, pointMenu.y, this);
			return;
		}
	}
	RECT editRect;
	GetClientRect(&editRect);
	if (point.x >= RESET_ICON_LEFT && point.x <= RESET_ICON_LEFT + ICON_SIZE) {
		SetWindowText(EMPTY);
		DoDelayedEvalute();
		SetFocus();
		return;
	}
	else if (point.x >= PASTE_ICON_LEFT && point.x <= PASTE_ICON_LEFT + ICON_SIZE) {
		if (::OpenClipboard(GetSafeHwnd())) {
			HGLOBAL hData = ::GetClipboardData(CF_UNICODETEXT);
			if (hData != NULL) {
				LPWSTR lpwstr = (LPWSTR)::GlobalLock(hData);
				if (lpwstr != NULL) {
					SetWindowText(lpwstr);
					::GlobalUnlock(hData);
					DoDelayedEvalute();
				}
			}
			::CloseClipboard();
		}
		SetFocus();
		return;
	}
	CEdit::OnLButtonDown(nFlags, point);
}

void CEditDelayed::OnLButtonUp(UINT nFlags, CPoint point)
{
	CEdit::OnLButtonUp(nFlags, point);
}

BOOL CEditDelayed::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (nHitTest == HTCLIENT) {
		RECT editRect;
		GetClientRect(&editRect);
		if ((m_pointMousePos.x >= RESET_ICON_LEFT && m_pointMousePos.x <= RESET_ICON_LEFT + ICON_SIZE) ||
			(m_pointMousePos.x >= PASTE_ICON_LEFT && m_pointMousePos.x <= PASTE_ICON_LEFT + ICON_SIZE) ||
			(m_pointMousePos.x >= COLUMN_ICON_LEFT && m_pointMousePos.x <= COLUMN_ICON_LEFT + ICON_SIZE)) {
			::SetCursor(m_hCursor);
			return TRUE;
		}
	}
	return CEdit::OnSetCursor(pWnd, nHitTest, message);
}

void CEditDelayed::OnMouseMove(UINT nFlags, CPoint point)
{
	m_pointMousePos = point;
	CEdit::OnMouseMove(nFlags, point);
}

void CEditDelayed::ShowColumnText(bool bShow)
{
	SetEditRect();

	if (bShow) {
		if (GetWindowTextLength() != 0 && !m_bShowsColumnText)
			return;

		m_bShowsColumnText = true;
		if (m_pctrlColumnHeader != NULL) {
			HDITEM hdi;
			TCHAR szBuffer[256];
			hdi.mask = HDI_TEXT | HDI_WIDTH;
			hdi.pszText = szBuffer;
			hdi.cchTextMax = _countof(szBuffer);
			if (m_pctrlColumnHeader->GetItem(m_nCurrentColumnIdx, &hdi))
				SetWindowText(hdi.pszText);
		} else
			SetWindowText(m_strAlternateText);
	} else if (m_bShowsColumnText) {
		m_bShowsColumnText = false;
		SetWindowText(EMPTY);
	}
}

void CEditDelayed::PreSubclassWindow()
{
	CWnd::PreSubclassWindow();
	SetProp(m_hWnd, _T("IsEditDelayed"), reinterpret_cast<HANDLE>(1)); // mark for DarkMode
	ModifyStyle(0, SS_NOTIFY);
}

HBRUSH CEditDelayed::CtlColor(CDC* pDC, UINT)
{
	HBRUSH hbr = IsDarkModeEnabled() ? CDarkMode::m_brDefault : ::GetSysColorBrush(COLOR_WINDOW);
	pDC->SetTextColor(GetCustomSysColor(m_bShowsColumnText ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT));
	pDC->SetBkColor(GetCustomSysColor(COLOR_WINDOW));
	return hbr;
}

void CEditDelayed::OnNcDestroy()
{
	RemoveProp(m_hWnd, _T("IsEditDelayed")); // remove mark
	CEdit::OnNcDestroy();
}

BOOL CEditDelayed::OnCommand(WPARAM wParam, LPARAM)
{
	wParam = LOWORD(wParam);
	if (wParam >= MP_FILTERCOLUMNS && wParam <= MP_FILTERCOLUMNS + 50) {
		if (m_nCurrentColumnIdx != (int)wParam - MP_FILTERCOLUMNS) {
			m_nCurrentColumnIdx = (int)wParam - MP_FILTERCOLUMNS;
			if (m_bShowsColumnText)
				ShowColumnText(true);
			else if (GetWindowTextLength() != 0)
				DoDelayedEvalute(true);
		}
	}
	return TRUE;
}

void CEditDelayed::OnSize(UINT nType, int cx, int cy)
{
	CEdit::OnSize(nType, cx, cy);
	SetEditRect();
}

BEGIN_MESSAGE_MAP(CIconWnd, CStatic)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CIconWnd::CIconWnd()
	: m_pImageList()
	, m_nCurrentIcon()
{
}

CIconWnd::~CIconWnd()
{
	delete m_pImageList;
}

void CIconWnd::OnPaint()
{
	RECT rect;
	GetClientRect(&rect);
	CPaintDC dc(this);
	dc.FillSolidRect(&rect, GetCustomSysColor(COLOR_WINDOW));
	SafeImageListDraw(m_pImageList, &dc, m_nCurrentIcon, POINT{ 2, (rect.bottom - 16) / 2 }, ILD_NORMAL);
}

BOOL CIconWnd::OnEraseBkgnd(CDC*)
{
	return TRUE;
}
