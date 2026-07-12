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
#include "resource.h"
#include "eMule.h"
#include "ExitBox.h"
#include "Preferences.h"
#include "OtherFunctions.h"
#include "eMuleAI/DarkMode.h"
#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


class CExitConfirmationOverlayWnd : public CWnd
{
public:
	explicit CExitConfirmationOverlayWnd(CWnd* pOwner)
		: m_pOwner(pOwner)
		, m_iResult(0)
		, m_uHoverItem(0)
		, m_uAnimationPhase(0)
		, m_bTrackingMouse(false)
		, m_bDoNotAskAgain(false)
		, m_sizeBackground(0, 0)
	{
		m_strBody = GetResString(_T("MAIN_EXIT"));
		m_strYes = GetResString(_T("YES"));
		m_strNo = GetResString(_T("NO"));
		m_strDoNotAskAgain = GetResString(_T("DONOTASKAGAIN"));
	}

	INT_PTR DoModalOverlay()
	{
		if (m_pOwner == NULL || !::IsWindow(m_pOwner->GetSafeHwnd()))
			return IDCANCEL;

		CaptureBackground();

		CString strClass(AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL));
		CRect rcOwner;
		m_pOwner->GetWindowRect(rcOwner);
		if (!CreateEx(WS_EX_TOOLWINDOW, strClass, NULL, WS_POPUP, rcOwner, m_pOwner, 0))
			return IDCANCEL;

		ShowWindow(SW_SHOW);
		SetWindowPos(&wndTop, rcOwner.left, rcOwner.top, rcOwner.Width(), rcOwner.Height(), SWP_SHOWWINDOW);
		SetForegroundWindow();
		SetFocus();
		SetTimer(TimerAnimation, 80, NULL);

		MSG msg;
		while (m_iResult == 0 && ::IsWindow(m_hWnd)) {
			const BOOL bMessage = ::GetMessage(&msg, NULL, 0, 0);
			if (bMessage == -1)
				break;
			if (bMessage == 0) {
				::PostQuitMessage(static_cast<int>(msg.wParam));
				break;
			}
			if (!PreTranslateMessage(&msg)) {
				::TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}
		}

		const INT_PTR iResult = m_iResult != 0 ? m_iResult : IDCANCEL;
		if (::IsWindow(m_hWnd))
			DestroyWindow();
		return iResult;
	}

	bool ShouldDisableConfirmExit() const
	{
		return m_bDoNotAskAgain;
	}

protected:
	enum { TimerAnimation = 1 };

	virtual BOOL PreTranslateMessage(MSG* pMsg)
	{
		if (pMsg != NULL && pMsg->message == WM_KEYDOWN) {
			if (pMsg->wParam == VK_ESCAPE || pMsg->wParam == VK_RETURN) {
				Finish(IDCANCEL);
				return TRUE;
			}
			if (pMsg->wParam == VK_SPACE && m_uHoverItem == IDC_DONOTASKAGAIN) {
				m_bDoNotAskAgain = !m_bDoNotAskAgain;
				Invalidate(FALSE);
				return TRUE;
			}
		}
		return CWnd::PreTranslateMessage(pMsg);
	}

	afx_msg void OnPaint()
	{
		CPaintDC dcPaint(this);
		CRect rcClient;
		GetClientRect(rcClient);

		CDC dcMem;
		dcMem.CreateCompatibleDC(&dcPaint);
		CBitmap bmpMem;
		bmpMem.CreateCompatibleBitmap(&dcPaint, max(1, rcClient.Width()), max(1, rcClient.Height()));
		CBitmap* pOldBitmap = dcMem.SelectObject(&bmpMem);
		DrawOverlay(&dcMem, rcClient);
		dcPaint.BitBlt(0, 0, rcClient.Width(), rcClient.Height(), &dcMem, 0, 0, SRCCOPY);
		dcMem.SelectObject(pOldBitmap);
	}

	afx_msg BOOL OnEraseBkgnd(CDC*)
	{
		return TRUE;
	}

	afx_msg void OnTimer(UINT_PTR nIDEvent)
	{
		if (nIDEvent == TimerAnimation) {
			if (theApp.IsBackendLifecycleStopping()) {
				Finish(IDOK);
				return;
			}
			m_uAnimationPhase = (m_uAnimationPhase + 24) % 1536;
			Invalidate(FALSE);
			return;
		}
		CWnd::OnTimer(nIDEvent);
	}

	afx_msg void OnLButtonUp(UINT, CPoint point)
	{
		const UINT uHit = HitTest(point);
		if (uHit == IDOK || uHit == IDCANCEL) {
			Finish(uHit);
			return;
		}
		if (uHit == IDC_DONOTASKAGAIN) {
			m_bDoNotAskAgain = !m_bDoNotAskAgain;
			Invalidate(FALSE);
		}
	}

	afx_msg void OnMouseMove(UINT nFlags, CPoint point)
	{
		const UINT uHit = HitTest(point);
		if (m_uHoverItem != uHit) {
			m_uHoverItem = uHit;
			Invalidate(FALSE);
		}

		if (!m_bTrackingMouse) {
			TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hWnd, 0 };
			m_bTrackingMouse = ::TrackMouseEvent(&tme) != FALSE;
		}
		CWnd::OnMouseMove(nFlags, point);
	}

	afx_msg void OnMouseLeave()
	{
		m_bTrackingMouse = false;
		if (m_uHoverItem != 0) {
			m_uHoverItem = 0;
			Invalidate(FALSE);
		}
	}

	afx_msg BOOL OnSetCursor(CWnd*, UINT, UINT)
	{
		CPoint point;
		::GetCursorPos(&point);
		ScreenToClient(&point);
		if (HitTest(point) != 0) {
			::SetCursor(::LoadCursor(NULL, IDC_HAND));
			return TRUE;
		}
		::SetCursor(::LoadCursor(NULL, IDC_ARROW));
		return TRUE;
	}

	DECLARE_MESSAGE_MAP()

private:
	static DWORD MakeDibColor(int iRed, int iGreen, int iBlue)
	{
		return (static_cast<DWORD>(iRed) << 16) | (static_cast<DWORD>(iGreen) << 8) | static_cast<DWORD>(iBlue);
	}

	static int GetDibRed(DWORD dwColor)
	{
		return static_cast<int>((dwColor >> 16) & 0xFF);
	}

	static int GetDibGreen(DWORD dwColor)
	{
		return static_cast<int>((dwColor >> 8) & 0xFF);
	}

	static int GetDibBlue(DWORD dwColor)
	{
		return static_cast<int>(dwColor & 0xFF);
	}

	static DWORD BlendDibColor(DWORD dwFrom, COLORREF crTo, int iToPercent)
	{
		const int iFromPercent = 100 - iToPercent;
		return MakeDibColor((GetDibRed(dwFrom) * iFromPercent + GetRValue(crTo) * iToPercent) / 100,
			(GetDibGreen(dwFrom) * iFromPercent + GetGValue(crTo) * iToPercent) / 100,
			(GetDibBlue(dwFrom) * iFromPercent + GetBValue(crTo) * iToPercent) / 100);
	}

	static void BlurPixels(DWORD* pPixels, int iWidth, int iHeight)
	{
		if (pPixels == NULL || iWidth <= 2 || iHeight <= 2)
			return;

		const int iRadius = 5;
		const size_t uPixelCount = static_cast<size_t>(iWidth) * static_cast<size_t>(iHeight);
		std::vector<DWORD> temp;
		try {
			temp.resize(uPixelCount);
		} catch (...) {
			return;
		}

		for (int y = 0; y < iHeight; ++y) {
			for (int x = 0; x < iWidth; ++x) {
				int iRed = 0;
				int iGreen = 0;
				int iBlue = 0;
				int iCount = 0;
				for (int dx = -iRadius; dx <= iRadius; ++dx) {
					const int xx = x + dx;
					if (xx < 0 || xx >= iWidth)
						continue;
					const DWORD dwColor = pPixels[y * iWidth + xx];
					iRed += GetDibRed(dwColor);
					iGreen += GetDibGreen(dwColor);
					iBlue += GetDibBlue(dwColor);
					++iCount;
				}
				temp[y * iWidth + x] = MakeDibColor(iRed / iCount, iGreen / iCount, iBlue / iCount);
			}
		}

		for (int y = 0; y < iHeight; ++y) {
			for (int x = 0; x < iWidth; ++x) {
				int iRed = 0;
				int iGreen = 0;
				int iBlue = 0;
				int iCount = 0;
				for (int dy = -iRadius; dy <= iRadius; ++dy) {
					const int yy = y + dy;
					if (yy < 0 || yy >= iHeight)
						continue;
					const DWORD dwColor = temp[yy * iWidth + x];
					iRed += GetDibRed(dwColor);
					iGreen += GetDibGreen(dwColor);
					iBlue += GetDibBlue(dwColor);
					++iCount;
				}
				pPixels[y * iWidth + x] = MakeDibColor(iRed / iCount, iGreen / iCount, iBlue / iCount);
			}
		}
	}

	void CaptureBackground()
	{
		if (m_pOwner == NULL || !::IsWindow(m_pOwner->GetSafeHwnd()))
			return;

		CRect rcOwner;
		m_pOwner->GetWindowRect(rcOwner);
		const int iWidth = rcOwner.Width();
		const int iHeight = rcOwner.Height();
		if (iWidth <= 0 || iHeight <= 0)
			return;

		HDC hdcScreen = ::GetDC(NULL);
		if (hdcScreen == NULL)
			return;

		BITMAPINFO bi;
		ZeroMemory(&bi, sizeof(bi));
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = iWidth;
		bi.bmiHeader.biHeight = -iHeight;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;

		void* pBits = NULL;
		HBITMAP hBitmap = ::CreateDIBSection(hdcScreen, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
		if (hBitmap != NULL && pBits != NULL) {
			CDC dcMem;
			dcMem.CreateCompatibleDC(CDC::FromHandle(hdcScreen));
			HGDIOBJ hOldBitmap = ::SelectObject(dcMem.GetSafeHdc(), hBitmap);
			BOOL bCaptured = ::PrintWindow(m_pOwner->GetSafeHwnd(), dcMem.GetSafeHdc(), 0);
			if (!bCaptured)
				bCaptured = ::BitBlt(dcMem.GetSafeHdc(), 0, 0, iWidth, iHeight, hdcScreen, rcOwner.left, rcOwner.top, SRCCOPY);
			::SelectObject(dcMem.GetSafeHdc(), hOldBitmap);

			if (bCaptured) {
				DWORD* pPixels = static_cast<DWORD*>(pBits);
				BlurPixels(pPixels, iWidth, iHeight);
				const bool bDark = IsDarkModeEnabled();
				const COLORREF crTint = bDark ? RGB(0, 0, 0) : RGB(246, 248, 252);
				const int iTintPercent = bDark ? 44 : 32;
				const size_t uPixelCount = static_cast<size_t>(iWidth) * static_cast<size_t>(iHeight);
				for (size_t i = 0; i < uPixelCount; ++i)
					pPixels[i] = BlendDibColor(pPixels[i], crTint, iTintPercent);

				m_bmpBackground.DeleteObject();
				m_bmpBackground.Attach(hBitmap);
				m_sizeBackground = CSize(iWidth, iHeight);
				hBitmap = NULL;
			}
		}

		if (hBitmap != NULL)
			::DeleteObject(hBitmap);
		::ReleaseDC(NULL, hdcScreen);
	}

	void DrawOverlay(CDC* pDC, const CRect& rcClient)
	{
		const bool bDark = IsDarkModeEnabled();
		const COLORREF crFallback = bDark ? RGB(22, 24, 30) : RGB(235, 240, 248);
		pDC->FillSolidRect(rcClient, crFallback);
		if (m_bmpBackground.GetSafeHandle() != NULL && m_sizeBackground.cx > 0 && m_sizeBackground.cy > 0) {
			CDC dcBitmap;
			dcBitmap.CreateCompatibleDC(pDC);
			CBitmap* pOldBitmap = dcBitmap.SelectObject(&m_bmpBackground);
			pDC->StretchBlt(0, 0, rcClient.Width(), rcClient.Height(), &dcBitmap, 0, 0, m_sizeBackground.cx, m_sizeBackground.cy, SRCCOPY);
			dcBitmap.SelectObject(pOldBitmap);
		}

		const int iPanelWidth = min(340, max(292, rcClient.Width() - 96));
		const int iPanelHeight = min(160, max(140, rcClient.Height() - 48));
		CRect rcPanel(rcClient.left + (rcClient.Width() - iPanelWidth) / 2, rcClient.top + (rcClient.Height() - iPanelHeight) / 2,
			rcClient.left + (rcClient.Width() + iPanelWidth) / 2, rcClient.top + (rcClient.Height() + iPanelHeight) / 2);

		const COLORREF crPanel = bDark ? RGB(30, 33, 42) : RGB(250, 252, 255);
		const COLORREF crPanelEdge = bDark ? RGB(75, 82, 102) : RGB(196, 207, 224);
		const COLORREF crText = bDark ? RGB(218, 224, 240) : RGB(42, 52, 72);
		const COLORREF crSoft = bDark ? RGB(186, 196, 218) : RGB(88, 101, 124);

		CRgn rgnPanel;
		rgnPanel.CreateRoundRectRgn(rcPanel.left, rcPanel.top, rcPanel.right + 1, rcPanel.bottom + 1, 14, 14);
		CBrush brPanel(crPanel);
		pDC->FillRgn(&rgnPanel, &brPanel);
		CPen penEdge(PS_SOLID, 1, crPanelEdge);
		CPen* pOldPen = pDC->SelectObject(&penEdge);
		CGdiObject* pOldBrush = pDC->SelectStockObject(NULL_BRUSH);
		pDC->RoundRect(rcPanel, CPoint(14, 14));
		DrawAnimatedRainbowBorder(pDC, rcPanel, m_uAnimationPhase, 2, 14);

		CFont* pFont = m_pOwner != NULL ? m_pOwner->GetFont() : NULL;
		if (pFont == NULL)
			pFont = CFont::FromHandle(static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT)));
		CFont* pOldFont = pDC->SelectObject(pFont);
		pDC->SetBkMode(TRANSPARENT);

		LayoutButtons(rcPanel);

		CRect rcBody(rcPanel.left + 22, rcPanel.top + 30, rcPanel.right - 22, rcPanel.top + 78);

		pDC->SetTextColor(crText);
		pDC->DrawText(m_strBody, rcBody, DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);

		DrawButton(pDC, m_rcYes, m_strYes, IDOK, false, bDark);
		DrawButton(pDC, m_rcNo, m_strNo, IDCANCEL, true, bDark);
		DrawCheckBox(pDC, crText, crSoft, bDark);

		pDC->SelectObject(pOldFont);
		pDC->SelectObject(pOldPen);
		pDC->SelectObject(pOldBrush);
	}

	void DrawButton(CDC* pDC, const CRect& rcButton, const CString& strText, UINT uButton, bool bDefault, bool bDark)
	{
		const bool bHover = (m_uHoverItem == uButton);
		const COLORREF crButton = bDefault ? (bHover ? RGB(63, 132, 245) : RGB(76, 132, 232)) : (bDark ? (bHover ? RGB(61, 67, 84) : RGB(47, 52, 66)) : (bHover ? RGB(235, 241, 252) : RGB(246, 248, 252)));
		const COLORREF crEdge = bDefault ? RGB(76, 132, 232) : (bDark ? RGB(78, 86, 106) : RGB(195, 205, 222));
		const COLORREF crText = bDefault ? RGB(255, 255, 255) : (bDark ? RGB(225, 231, 245) : RGB(30, 42, 66));
		CBrush brButton(crButton);
		CPen penButton(PS_SOLID, 1, crEdge);
		CPen* pOldPen = pDC->SelectObject(&penButton);
		CBrush* pOldBrush = pDC->SelectObject(&brButton);
		pDC->RoundRect(rcButton, CPoint(8, 8));
		pDC->SetTextColor(crText);
		CRect rcText(rcButton);
		pDC->DrawText(strText, rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		pDC->SelectObject(pOldPen);
		pDC->SelectObject(pOldBrush);
	}

	void DrawCheckBox(CDC* pDC, COLORREF crText, COLORREF crSoft, bool bDark)
	{
		const bool bHover = (m_uHoverItem == IDC_DONOTASKAGAIN);
		const COLORREF crBox = bHover ? (bDark ? RGB(48, 54, 68) : RGB(240, 245, 255)) : (bDark ? RGB(35, 39, 50) : RGB(252, 253, 255));
		const COLORREF crEdge = bHover ? RGB(76, 132, 232) : crSoft;
		CBrush brBox(crBox);
		CPen penBox(PS_SOLID, 1, crEdge);
		CPen* pOldPen = pDC->SelectObject(&penBox);
		CBrush* pOldBrush = pDC->SelectObject(&brBox);
		pDC->Rectangle(m_rcCheck);
		if (m_bDoNotAskAgain) {
			CPen penCheck(PS_SOLID, 2, RGB(76, 132, 232));
			pDC->SelectObject(&penCheck);
			pDC->MoveTo(m_rcCheck.left + 3, m_rcCheck.top + 7);
			pDC->LineTo(m_rcCheck.left + 6, m_rcCheck.bottom - 3);
			pDC->LineTo(m_rcCheck.right - 3, m_rcCheck.top + 3);
		}
		pDC->SelectObject(pOldPen);
		pDC->SelectObject(pOldBrush);

		pDC->SetTextColor(crText);
		CRect rcText(m_rcCheckText);
		pDC->DrawText(m_strDoNotAskAgain, rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	}

	void LayoutButtons(const CRect& rcPanel)
	{
		const int iButtonWidth = 88;
		const int iButtonHeight = 28;
		const int iGap = 12;
		const int iTop = rcPanel.bottom - 69;
		const int iLeft = rcPanel.left + (rcPanel.Width() - iButtonWidth * 2 - iGap) / 2;
		m_rcYes.SetRect(iLeft, iTop, iLeft + iButtonWidth, iTop + iButtonHeight);
		m_rcNo.SetRect(m_rcYes.right + iGap, iTop, m_rcYes.right + iGap + iButtonWidth, iTop + iButtonHeight);

		m_rcCheck.SetRect(rcPanel.left + 20, rcPanel.bottom - 28, rcPanel.left + 34, rcPanel.bottom - 14);
		m_rcCheckText.SetRect(m_rcCheck.right + 8, rcPanel.bottom - 32, rcPanel.right - 20, rcPanel.bottom - 10);
		m_rcCheckClick.SetRect(m_rcCheck.left - 4, m_rcCheck.top - 4, m_rcCheckText.right, m_rcCheck.bottom + 6);
	}

	UINT HitTest(CPoint point) const
	{
		if (m_rcYes.PtInRect(point))
			return IDOK;
		if (m_rcNo.PtInRect(point))
			return IDCANCEL;
		if (m_rcCheckClick.PtInRect(point))
			return IDC_DONOTASKAGAIN;
		return 0;
	}

	void Finish(INT_PTR iResult)
	{
		m_iResult = iResult;
		if (::IsWindow(m_hWnd))
			DestroyWindow();
	}

	CWnd* m_pOwner;
	CString m_strBody;
	CString m_strYes;
	CString m_strNo;
	CString m_strDoNotAskAgain;
	INT_PTR m_iResult;
	UINT m_uHoverItem;
	UINT m_uAnimationPhase;
	bool m_bTrackingMouse;
	bool m_bDoNotAskAgain;
	CRect m_rcYes;
	CRect m_rcNo;
	CRect m_rcCheck;
	CRect m_rcCheckText;
	CRect m_rcCheckClick;
	CBitmap m_bmpBackground;
	CSize m_sizeBackground;
};

BEGIN_MESSAGE_MAP(CExitConfirmationOverlayWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_TIMER()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_SETCURSOR()
END_MESSAGE_MAP()


IMPLEMENT_DYNAMIC(ExitBox, CDialog)

BEGIN_MESSAGE_MAP(ExitBox, CDialog)
	ON_WM_CTLCOLOR()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

ExitBox::ExitBox(CWnd *pParent)
	: CDialog(ExitBox::IDD, pParent)
	, m_cancel(true)
	, m_pOverlayParent(pParent)
{
	m_brush.CreateSolidBrush(GetCustomSysColor(COLOR_WINDOW));
}

ExitBox::~ExitBox()
{
    m_brush.DeleteObject(); // Delete the brush
}

INT_PTR ExitBox::DoModal()
{
	m_cancel = true;
	CWnd* pOwner = m_pOverlayParent;
	if (pOwner == NULL || !::IsWindow(pOwner->GetSafeHwnd()))
		pOwner = AfxGetMainWnd();

	if (pOwner == NULL || !::IsWindow(pOwner->GetSafeHwnd()))
		return CDialog::DoModal();

	CExitConfirmationOverlayWnd wndOverlay(pOwner);
	const INT_PTR iResult = wndOverlay.DoModalOverlay();
	m_cancel = iResult != IDOK;
	if (!m_cancel && wndOverlay.ShouldDisableConfirmExit())
		thePrefs.SetConfirmExit(false);
	return iResult;
}

void ExitBox::DoDataExchange(CDataExchange *pDX)
{
	CDialog::DoDataExchange(pDX);
}

void ExitBox::OnOK()
{
	CDialog::OnOK();
	m_cancel = false;
	if (IsDlgButtonChecked(IDC_DONOTASKAGAIN))
		thePrefs.SetConfirmExit(false);
}

BOOL ExitBox::OnInitDialog()
{
	CDialog::OnInitDialog();
	InitWindowStyles(this);
	CStatic *pic = static_cast<CStatic*>(GetDlgItem(IDC_STATIC));
	pic->SetIcon(::LoadIcon(NULL, IDI_QUESTION));

	SetWindowText(GetResString(_T("CLOSEEMULE")));
	SetDlgItemText(IDC_MAIN_EXIT, GetResString(_T("MAIN_EXIT")));
	SetDlgItemText(IDOK, GetResString(_T("YES")) );
	SetDlgItemText(IDCANCEL, GetResString(_T("NO")));
	SetDlgItemText(IDC_DONOTASKAGAIN, GetResString(_T("DONOTASKAGAIN")));

	PostMessage(WM_NEXTDLGCTL, (WPARAM)GetDlgItem(IDCANCEL)->GetSafeHwnd(), TRUE);
	return TRUE;
}

BOOL ExitBox::OnEraseBkgnd(CDC* pDC)
{
	CDialog::OnEraseBkgnd(pDC);
	// get clipping rectangle
	CRect rcClip;
	pDC->GetClipBox(rcClip);
	rcClip.bottom /= 2;
	// fill rectangle using a given brush
	pDC->FillRect(rcClip, &m_brush);
	return TRUE; // returns non-zero to prevent further erasing
}

HBRUSH ExitBox::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	int id = pWnd->GetDlgCtrlID();
	if (id != IDC_MAIN_EXIT && id != IDC_STATIC)
		return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
	pDC->SetBkColor(::GetSysColor(COLOR_WINDOW));
	return m_brush;
}