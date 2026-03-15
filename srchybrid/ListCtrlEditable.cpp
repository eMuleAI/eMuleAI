#include "stdafx.h"
#include "ListCtrlEditable.h"
#include "eMuleAI/DarkMode.h"
#include "Emule.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#if (_WIN32_WINNT < 0x501)
typedef struct tagNMLVSCROLL
{
	NMHDR   hdr;
	int     dx;
	int     dy;
} NMLVSCROLL, *LPNMLVSCROLL;

#define LVN_BEGINSCROLL          (LVN_FIRST-80)
#define LVN_ENDSCROLL            (LVN_FIRST-81)
#endif


#define MAX_COLS	2

#define LV_EDIT_CTRL_ID		1001

BEGIN_MESSAGE_MAP(CEditableListCtrl, CListCtrl)
	ON_EN_KILLFOCUS(LV_EDIT_CTRL_ID, OnEnKillFocus)
	ON_NOTIFY_REFLECT(LVN_BEGINSCROLL, OnLvnBeginScroll)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_ENDSCROLL, OnLvnEndScroll)
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnNmCustomDraw)
	ON_WM_DESTROY()
	ON_WM_HSCROLL()
	ON_WM_LBUTTONDOWN()
	ON_WM_SETFOCUS()
	ON_WM_VSCROLL()
	ON_WM_PAINT()
	ON_WM_NCPAINT()
	ON_MESSAGE(WM_THEMECHANGED, OnThemeChanged)
END_MESSAGE_MAP()

CEditableListCtrl::CEditableListCtrl()
	: m_pctrlEdit()
	, m_pctrlComboBox()
	, m_iRow()
	, m_iCol(1)
	, m_iEditRow(-1)
	, m_iEditCol(-1)
{
}

void CEditableListCtrl::ResetTopPosition()
{
	if (m_pctrlComboBox || m_pctrlEdit) {
		CRect rcThis;
		GetWindowRect(rcThis);
		CRect rect;
		if (GetSubItemRect(m_iRow, m_iCol, LVIR_LABEL, rect)) {
			ClientToScreen(rect);
			if (!rcThis.PtInRect(rect.TopLeft()))
				SendMessage(WM_VSCROLL, SB_LINEUP, 0);
		}
	}
}

void CEditableListCtrl::ResetBottomPosition()
{
	if (m_pctrlComboBox || m_pctrlEdit) {
		CRect rcThis;
		GetWindowRect(rcThis);
		CRect rect;
		if (GetSubItemRect(m_iRow, m_iCol, LVIR_LABEL, rect)) {
			ClientToScreen(rect);
			if (!rcThis.PtInRect(rect.BottomRight()))
				SendMessage(WM_VSCROLL, SB_LINEDOWN, 0);
		}
	}
}

BOOL CEditableListCtrl::PreTranslateMessage(MSG *pMsg)
{
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_TAB) {
		CWnd *pWnd = NULL;
		CommitEdit();
		if (::GetKeyState(VK_SHIFT) & 0x8000) { //shift+tab - upwards
			if (--m_iCol < 1) {
				m_iCol = MAX_COLS - 1;
				while (--m_iRow >= 0 && GetItemData(m_iRow) & 1);

				if (m_iRow < 0) {
					// cycle to prev sibling control
					m_iCol = 1;
					m_iRow = 0;
					pWnd = GetWindow(GW_HWNDPREV);
					ASSERT(pWnd);
				}
			}
		} else { //tab - downwards
			if (++m_iCol >= MAX_COLS) {
				m_iCol = 1;
				int iCnt = GetItemCount();
				while (++m_iRow < iCnt && (GetItemData(m_iRow) & 1));

				ResetBottomPosition();
				if (m_iRow >= iCnt) {
					// cycle to next sibling control
					m_iRow = iCnt - 1;
					pWnd = GetWindow(GW_HWNDNEXT);
					ASSERT(pWnd);
				}
			}
		}
		if (!pWnd) {
			EnsureVisible(m_iRow, FALSE);
			ResetTopPosition();
			ResetBottomPosition();

			if (m_iCol == 1)
				ShowEdit();
			return TRUE;
		}

		pWnd->SetFocus(); //leaving CEditableListCtrl
	}
	return CListCtrl::PreTranslateMessage(pMsg);
}

void CEditableListCtrl::OnSetFocus(CWnd *pOldWnd)
{
	CListCtrl::OnSetFocus(pOldWnd);
	if (m_iCol == 1) {
		if (m_pctrlEdit != pOldWnd && m_pctrlComboBox != pOldWnd) //CEditableListCtrl had no focus
		{
			EnsureVisible(m_iRow, FALSE);
			ShowEdit();
		}
	}
}

void CEditableListCtrl::CommitEdit()
{
	if (m_iEditCol >= 0 && m_iEditRow >= 0) {
		CString strItem;
		if (m_pctrlEdit && m_pctrlEdit->IsWindowVisible())
			m_pctrlEdit->GetWindowText(strItem);
		else if (m_pctrlComboBox && m_pctrlComboBox->IsWindowVisible())
			m_pctrlComboBox->GetLBText(m_pctrlComboBox->GetCurSel(), strItem);
		SetItemText(m_iEditRow, m_iEditCol, strItem.Trim());
	}
	m_iEditRow = -1;
	m_iEditCol = -1;
}

void CEditableListCtrl::EndEdit()
{
	CommitEdit();
	if (m_pctrlEdit)
		m_pctrlEdit->ShowWindow(SW_HIDE);
}

void CEditableListCtrl::HideEdit()
{
	if (m_pctrlEdit && m_pctrlEdit->IsWindowVisible())
		m_pctrlEdit->ShowWindow(SW_HIDE);
	if (m_pctrlComboBox && m_pctrlComboBox->IsWindowVisible())
		m_pctrlComboBox->ShowWindow(SW_HIDE);
}

void CEditableListCtrl::ShowEdit()
{
	if (m_iRow >= GetItemCount())
		return;
	if (GetItemData(m_iRow) & 1) {
		if (m_pctrlEdit)
			m_pctrlEdit->ShowWindow(SW_HIDE);
		return;
	}

	CRect rect;
	GetSubItemRect(m_iRow, m_iCol, LVIR_LABEL, rect);
	if (m_pctrlEdit == NULL) {
		m_pctrlEdit = new CEdit;
		m_pctrlEdit->Create(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL, rect, this, LV_EDIT_CTRL_ID);
		ApplyTheme(m_pctrlEdit->GetParent()->GetSafeHwnd());
		m_pctrlEdit->SetFont(GetFont());
		m_pctrlEdit->ShowWindow(SW_SHOW);
	} else
		m_pctrlEdit->SetWindowPos(NULL, rect.left, rect.top, rect.Width(), rect.Height(), SWP_SHOWWINDOW);

	TCHAR szItem[256];
	LVITEM item;
	item.mask = LVIF_TEXT;
	item.iItem = m_iRow;
	item.iSubItem = m_iCol;
	item.cchTextMax = _countof(szItem);
	item.pszText = szItem;
	szItem[GetItem(&item) ? _countof(szItem) - 1 : 0] = _T('\0');

	m_pctrlEdit->SetWindowText(szItem);
	m_pctrlEdit->SetSel(0, -1);
	if (m_pctrlComboBox)
		m_pctrlComboBox->ShowWindow(SW_HIDE);
	m_pctrlEdit->SetFocus();

	m_iEditRow = m_iRow;
	m_iEditCol = m_iCol;
}

void CEditableListCtrl::OnEnKillFocus()
{
	EndEdit();
}

void CEditableListCtrl::ShowComboBoxCtrl()
{
	CRect rect;
	GetSubItemRect(m_iRow, m_iCol, LVIR_LABEL, rect);
	rect.bottom += 100;
	if (!m_pctrlComboBox) {
		m_pctrlComboBox = new CComboBox;
		m_pctrlComboBox->Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | WS_BORDER | CBS_DROPDOWN | WS_VSCROLL | WS_HSCROLL, rect, this, 1002);
		m_pctrlComboBox->ShowWindow(SW_SHOW);
		m_pctrlComboBox->SetHorizontalExtent(800);
		m_pctrlComboBox->SendMessage(CB_SETDROPPEDWIDTH, 600, 0);
	} else {
		m_pctrlComboBox->SetWindowPos(NULL, rect.left, rect.top, rect.Width(), rect.Height(), SWP_SHOWWINDOW);
		m_pctrlComboBox->SetCurSel(0);
	}
	if (m_pctrlEdit)
		m_pctrlEdit->ShowWindow(SW_HIDE);
	m_pctrlComboBox->SetFocus();
}

void CEditableListCtrl::OnLvnColumnClick(LPNMHDR, LRESULT *pResult)
{
	HideEdit();
	*pResult = 0;
}

void CEditableListCtrl::OnLButtonDown(UINT nFlags, CPoint point)
{
	CListCtrl::OnLButtonDown(nFlags, point);

	LVHITTESTINFO hti;
	hti.pt = point;
	if (SubItemHitTest(&hti) < 0)
		return;

	int iItem = hti.iItem;
	int iSubItem = hti.iSubItem;
	if (iItem < 0 || iSubItem < 0)
		return;

	CRect rect;
	GetSubItemRect(iItem, iSubItem, LVIR_LABEL, rect);
	ClientToScreen(rect);

	CRect rc;
	GetWindowRect(rc);
	if (!rc.PtInRect(rect.BottomRight()))
		SendMessage(WM_VSCROLL, SB_LINEDOWN, 0);
	if (!rc.PtInRect(rect.TopLeft()))
		SendMessage(WM_VSCROLL, SB_LINEUP, 0);

	m_iRow = iItem;
	m_iCol = iSubItem;

	switch (m_iCol) {
	case 0:
		m_iCol = 1; //move to column 1
	case 1:
		ShowEdit(); //start editing
	}
}

void CEditableListCtrl::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
{
	if (m_pctrlEdit)
		switch (nSBCode) {
		case SB_ENDSCROLL:
			ShowEdit();
		case SB_LINEDOWN:
		case SB_LINEUP:
			break;
		default:
			EndEdit();
		}

	if (m_pctrlComboBox)
		m_pctrlComboBox->ShowWindow(SW_HIDE);
	CListCtrl::OnVScroll(nSBCode, nPos, pScrollBar);
}

void CEditableListCtrl::OnLvnBeginScroll(LPNMHDR, LRESULT *pResult)
{
	if (m_pctrlEdit)
		EndEdit();
	*pResult = 0;
}

void CEditableListCtrl::OnLvnEndScroll(LPNMHDR, LRESULT *pResult)
{
	if (m_pctrlEdit)
		ShowEdit();
	*pResult = 0;
}

void CEditableListCtrl::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
{
	HideEdit();
	CListCtrl::OnHScroll(nSBCode, nPos, pScrollBar);
}

BOOL CEditableListCtrl::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT *pResult)
{
	switch (reinterpret_cast<LPNMHDR>(lParam)->code) {
	case HDN_BEGINTRACKW:
	case HDN_BEGINTRACKA:
		HideEdit();
		*pResult = 0;
	}

	return CListCtrl::OnNotify(wParam, lParam, pResult);
}

void CEditableListCtrl::OnDestroy()
{
	delete m_pctrlEdit;
	delete m_pctrlComboBox;
	CListCtrl::OnDestroy();
}

void CEditableListCtrl::OnNmCustomDraw(LPNMHDR pNMHDR, LRESULT *pResult)
{
	*pResult = CDRF_DODEFAULT; // Default behavior

	if (!IsDarkModeEnabled() || !pNMHDR) // Run only for dark mode is active and check if pNMHDR is valid
		return;

	LPNMLVCUSTOMDRAW pNMCD = reinterpret_cast<LPNMLVCUSTOMDRAW>(pNMHDR);
	if (!pNMCD) // Check if pNMCD is valid
		return;

	// Determine the drawing stage
	switch (pNMCD->nmcd.dwDrawStage)
	{
	case CDDS_PREPAINT:
		// Notify that we want to draw items
		*pResult = CDRF_NOTIFYITEMDRAW;
		return;

	case CDDS_ITEMPREPAINT:
	{
		FillRect(pNMCD->nmcd.hdc, &pNMCD->nmcd.rc, CDarkMode::m_brDefault); // Fill the rectangle
		pNMCD->clrText = (pNMCD->nmcd.lItemlParam & 1) ? GetCustomSysColor(COLOR_INACTIVECAPTIONTEXT) : GetCustomSysColor(COLOR_WINDOWTEXT); //Bit 0 = disabled
		pNMCD->clrTextBk = GetCustomSysColor(COLOR_WINDOW);
	}
	}
}

void CEditableListCtrl::OnPaint()
{
	// Call the default paint handler
	CListCtrl::OnPaint();

	if (!IsDarkModeEnabled()) // Do not continue if dark mode is passive
		return;

	// Perform custom drawing after default painting is done
	CDC* pDC = GetDC();
	if (!pDC) // Ensure we have a valid device context
		return;

	CRect rect;
	GetClientRect(&rect);

	// Define a pen for all lines (grid and column separators)
	CPen gridPen(PS_SOLID, 1, GetCustomSysColor(COLOR_TABBORDER)); // Grid pen color
	CPen* pOldPen = pDC->SelectObject(&gridPen);

	// Get row and column counts
	int rowCount = GetItemCount();
	int colCount = GetHeaderCtrl()->GetItemCount();

	// Draw grid lines
	for (int i = 0; i < rowCount; i++) {
		for (int j = 0; j < colCount; j++) {
			CRect cellRect;
			if (GetSubItemRect(i, j, LVIR_BOUNDS, cellRect)) {
				// Draw bottom grid line
				pDC->MoveTo(cellRect.left, cellRect.bottom - 1);
				pDC->LineTo(cellRect.right, cellRect.bottom - 1);
			}
		}
	}

	// Draw column separators using the same grid pen
	for (int j = 1; j < colCount; j++) { // Include last column
		CRect colRect;
		if (GetSubItemRect(0, j, LVIR_BOUNDS, colRect)) {			
			int offset = 0; // Exact positioning for the column separators
			pDC->MoveTo(colRect.left + offset, rect.top);
			pDC->LineTo(colRect.left + offset, rect.bottom);
		}
	}

	// Restore the original pen
	pDC->SelectObject(pOldPen);
	ReleaseDC(pDC);
}

void CEditableListCtrl::OnNcPaint()
{
	if (!IsDarkModeEnabled()) { // Do not continue if dark mode is passive
		CListCtrl::OnNcPaint();
		return;
	}

	// Call default painting to preserve the scrollbar and other elements
	Default();

	// Get the device context for the non-client area
	CWindowDC dc(this);

	// Retrieve the window rectangle, including the non-client area
	CRect windowRect;
	GetWindowRect(&windowRect);
	windowRect.OffsetRect(-windowRect.left, -windowRect.top);

	// Adjust rectangle for precise border alignment
	windowRect.DeflateRect(1, 1, 1, 1); // Correct right misalignment uniformly

	// Define a pen with 2-pixel thickness to draw the border
	CPen greyPen(PS_SOLID, 2, GetCustomSysColor(COLOR_TABBORDER));
	CPen* pOldPen = dc.SelectObject(&greyPen);

	// Draw borders exactly aligned within the rectangle
	dc.MoveTo(windowRect.left, windowRect.top);
	dc.LineTo(windowRect.right, windowRect.top);
	dc.LineTo(windowRect.right, windowRect.bottom);
	dc.LineTo(windowRect.left, windowRect.bottom);
	dc.LineTo(windowRect.left, windowRect.top);

	// Restore the original pen
	dc.SelectObject(pOldPen);
}

LRESULT CEditableListCtrl::OnThemeChanged(WPARAM, LPARAM) {
	DWORD ex = GetExtendedStyle();

	if (IsDarkModeEnabled())
		ex &= ~LVS_EX_GRIDLINES; // Drop gridlines in dark mode
	else
		ex |= LVS_EX_GRIDLINES; // Restore gridlines in light mode

	SetExtendedStyle(ex);
	Invalidate(); // Force redraw
	return 0;
}