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
#include "ToolTipCtrlX.h"
#include "MuleListCtrl.h"
#include "OtherFunctions.h"
#include "emule.h"
#include "log.h"
#include "eMuleAI/DarkMode.h"
#include "eMuleAI/GeoLite2.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define DFLT_DRAWTEXT_FLAGS	(DT_NOPREFIX | DT_EXTERNALLEADING | DT_END_ELLIPSIS)

namespace
{
	constexpr UINT_PTR TOOLTIPCTRLX_DEFERRED_HIDE_TIMER_ID = 0x54545831;

	struct TooltipHeaderIcon
	{
		CImageList* pImageList;
		int iImage;
		CSize size;
	};

	static void DrawTooltipBorder(CDC* pdc, const CRect& rcWnd, COLORREF crBorder)
	{
		CPen pen(PS_SOLID, 1, crBorder);
		CPen* pOldPen = pdc->SelectObject(&pen);
		pdc->MoveTo(rcWnd.left, rcWnd.top);
		pdc->LineTo(rcWnd.right - 1, rcWnd.top);
		pdc->LineTo(rcWnd.right - 1, rcWnd.bottom - 1);
		pdc->LineTo(rcWnd.left, rcWnd.bottom - 1);
		pdc->LineTo(rcWnd.left, rcWnd.top);
		pdc->SelectObject(pOldPen);
	}

	static int ResolveTabToolIndex(CTabCtrl* pTabCtrl, const TOOLINFO& ti)
	{
		if (pTabCtrl == NULL)
			return -1;

		const int iItemCount = pTabCtrl->GetItemCount();
		if (iItemCount <= 0)
			return -1;

		const CRect rcTool(ti.rect);
		for (int i = 0; i < iItemCount; ++i) {
			CRect rcItem;
			if (pTabCtrl->GetItemRect(i, &rcItem) && rcItem == rcTool)
				return i;
		}

		if ((ti.uFlags & TTF_IDISHWND) == 0) {
			if (static_cast<int>(ti.uId) >= 0 && static_cast<int>(ti.uId) < iItemCount)
				return static_cast<int>(ti.uId);
			if (static_cast<int>(ti.uId) > 0 && static_cast<int>(ti.uId - 1) < iItemCount)
				return static_cast<int>(ti.uId - 1);
		}

		return -1;
	}

	static bool TryGetTooltipHeaderIcon(HWND hTooltipWnd, TooltipHeaderIcon& headerIcon)
	{
		memset(&headerIcon, 0, sizeof(headerIcon));
		if (hTooltipWnd == NULL)
			return false;

		TOOLINFO ti = {};
		ti.cbSize = sizeof(ti);
		if (!::SendMessage(hTooltipWnd, TTM_GETCURRENTTOOL, 0, reinterpret_cast<LPARAM>(&ti)) || ti.hwnd == NULL)
			return false;

		CTabCtrl* pTabCtrl = static_cast<CTabCtrl*>(CWnd::FromHandle(ti.hwnd));
		if (pTabCtrl == NULL)
			return false;

		CImageList* pImageList = pTabCtrl->GetImageList();
		if (pImageList == NULL || pImageList->GetSafeHandle() == NULL)
			return false;

		const int iTabIndex = ResolveTabToolIndex(pTabCtrl, ti);
		if (iTabIndex < 0)
			return false;

		TCITEM item = {};
		item.mask = TCIF_IMAGE;
		if (!pTabCtrl->GetItem(iTabIndex, &item) || item.iImage < 0)
			return false;

		IMAGEINFO ii = {};
		if (!pImageList->GetImageInfo(item.iImage, &ii))
			return false;

		headerIcon.pImageList = pImageList;
		headerIcon.iImage = item.iImage;
		headerIcon.size.cx = ii.rcImage.right - ii.rcImage.left;
		headerIcon.size.cy = ii.rcImage.bottom - ii.rcImage.top;
		return (headerIcon.size.cx > 0 && headerIcon.size.cy > 0);
	}

	static void DrawTooltipHeaderIcon(const TooltipHeaderIcon& headerIcon, CDC* pdc, const CRect& rcLine)
	{
		if (headerIcon.pImageList == NULL || pdc == NULL)
			return;

		const int iX = rcLine.left;
		const int iY = rcLine.top + max(0, (rcLine.Height() - headerIcon.size.cy) / 2);
		SafeImageListDraw(headerIcon.pImageList, pdc, headerIcon.iImage, CPoint(iX, iY), ILD_TRANSPARENT);
	}

	static bool ParseFlagMarker(const CString& input, CString& output, int& flagIndex, int& flagPos)
	{
		static const CString kFlagPrefix(_T("<flag="));
		output = input;
		flagIndex = -1;
		flagPos = -1;

		CString lowerInput(input);
		lowerInput.MakeLower();
		const int iStart = lowerInput.Find(kFlagPrefix);
		if (iStart < 0)
			return false;

		const int iEnd = lowerInput.Find(_T('>'), iStart + kFlagPrefix.GetLength());
		if (iEnd <= iStart + kFlagPrefix.GetLength())
			return false;

		const CString strIndex = input.Mid(iStart + kFlagPrefix.GetLength(), iEnd - (iStart + kFlagPrefix.GetLength()));
		flagIndex = _tstoi(strIndex);

		const CString strOutputRaw = input.Left(iStart) + input.Mid(iEnd + 1);
		output = strOutputRaw;
		output.TrimLeft();
		flagPos = iStart - (strOutputRaw.GetLength() - output.GetLength());
		if (flagPos < 0)
			flagPos = 0;
		if (flagPos > output.GetLength())
			flagPos = output.GetLength();
		return true;
	}
}


IMPLEMENT_DYNAMIC(CToolTipCtrlX, CToolTipCtrl)

BEGIN_MESSAGE_MAP(CToolTipCtrlX, CToolTipCtrl)
	ON_WM_SYSCOLORCHANGE()
	ON_WM_SETTINGCHANGE()
	ON_WM_TIMER()
	ON_NOTIFY_REFLECT(NM_CUSTOMDRAW, OnNmCustomDraw)
	ON_NOTIFY_REFLECT(NM_THEMECHANGED, OnNmThemeChanged)
	ON_NOTIFY_REFLECT_EX(TTN_POP, OnTTPop)
	ON_NOTIFY_REFLECT_EX(TTN_SHOW, OnTTShow)
END_MESSAGE_MAP()

CToolTipCtrlX::CToolTipCtrlX()
	: m_bCol1Bold(true)
	, m_dwMinimumVisibleTime(0)
	, m_dwVisibleSinceTick(0)
	, m_bDeferVisibleUpdates(true)
	, m_bHasDeferredTextUpdate(false)
	, m_bDeferredTextUsesCallback(false)
	, m_bShowFileIcon()
	, m_bOwnsWindow(false)
	, m_bProcessingDeferredMessage(false)
	, m_uDeferredHideMessage(0)
{
	memset(&m_tiDeferredTextUpdate, 0, sizeof(m_tiDeferredTextUpdate));
	memset(&m_tiDeferredHide, 0, sizeof(m_tiDeferredHide));
	ResetSystemMetrics();
	m_dwCol1DrawTextFlags = m_dwCol2DrawTextFlags = DT_LEFT | DFLT_DRAWTEXT_FLAGS;
}

CToolTipCtrlX::~CToolTipCtrlX()
{
	CleanupWindow();
}

BOOL CToolTipCtrlX::Create(CWnd *pParentWnd, DWORD dwStyle)
{
	if (!__super::Create(pParentWnd, dwStyle))
		return FALSE;
	m_bOwnsWindow = true;
	ApplyTooltipStyles();
	return TRUE;
}

void CToolTipCtrlX::SetCol1DrawTextFlags(DWORD dwFlags)
{
	m_dwCol1DrawTextFlags = DFLT_DRAWTEXT_FLAGS | dwFlags;
}

void CToolTipCtrlX::SetCol2DrawTextFlags(DWORD dwFlags)
{
	m_dwCol2DrawTextFlags = DFLT_DRAWTEXT_FLAGS | dwFlags;
}

BOOL CToolTipCtrlX::SubclassWindow(HWND hWnd)
{
	BOOL bResult = __super::SubclassWindow(hWnd);
	if (bResult) {
		m_bOwnsWindow = false;
		ApplyTooltipStyles();
	}
	return bResult;
}

void CToolTipCtrlX::CleanupWindow()
{
	ResetDeferredHide();
	ResetDeferredTextUpdate();
	m_dwVisibleSinceTick = 0;

	HWND hWnd = GetSafeHwnd();
	if (hWnd == NULL)
		return;

	if (::IsWindow(hWnd)) {
		if (m_bOwnsWindow) {
			if (!DestroyWindow() && GetSafeHwnd() != NULL)
				Detach();
		} else if (UnsubclassWindow() == NULL) {
			Detach();
		}
	} else {
		Detach();
	}

	m_bOwnsWindow = false;
}

bool CToolTipCtrlX::IsTooltipVisible() const
{
	return (GetSafeHwnd() != NULL && ::IsWindowVisible(m_hWnd) != FALSE);
}

bool CToolTipCtrlX::TryGetCurrentToolInfo(TOOLINFO& ti) const
{
	memset(&ti, 0, sizeof(ti));
	ti.cbSize = sizeof(ti);
	return (::IsWindow(m_hWnd) && GetCurrentTool(&ti) != FALSE && ti.hwnd != NULL);
}

bool CToolTipCtrlX::IsSameTool(const TOOLINFO& left, const TOOLINFO& right)
{
	return left.hwnd == right.hwnd && left.uId == right.uId && ((left.uFlags & TTF_IDISHWND) == (right.uFlags & TTF_IDISHWND));
}

void CToolTipCtrlX::ResetDeferredTextUpdate()
{
	memset(&m_tiDeferredTextUpdate, 0, sizeof(m_tiDeferredTextUpdate));
	m_strDeferredTextUpdate.Empty();
	m_bHasDeferredTextUpdate = false;
	m_bDeferredTextUsesCallback = false;
}

void CToolTipCtrlX::StoreDeferredTextUpdate(const TOOLINFO& ti)
{
	m_tiDeferredTextUpdate = ti;
	m_tiDeferredTextUpdate.lpszText = NULL;
	m_bDeferredTextUsesCallback = (ti.lpszText == LPSTR_TEXTCALLBACK);
	if (m_bDeferredTextUsesCallback)
		m_strDeferredTextUpdate.Empty();
	else
		m_strDeferredTextUpdate = ti.lpszText != NULL ? ti.lpszText : EMPTY;
	m_bHasDeferredTextUpdate = true;
}

void CToolTipCtrlX::ApplyDeferredTextUpdate()
{
	if (!m_bHasDeferredTextUpdate || !::IsWindow(m_hWnd))
		return;

	TOOLINFO ti = m_tiDeferredTextUpdate;
	ti.lpszText = m_bDeferredTextUsesCallback ? LPSTR_TEXTCALLBACK : const_cast<LPTSTR>((LPCTSTR)m_strDeferredTextUpdate);

	m_bProcessingDeferredMessage = true;
	__super::WindowProc(TTM_UPDATETIPTEXT, 0, reinterpret_cast<LPARAM>(&ti));
	m_bProcessingDeferredMessage = false;

	ResetDeferredTextUpdate();
}

void CToolTipCtrlX::ResetDeferredHide()
{
	if (::IsWindow(m_hWnd))
		KillTimer(TOOLTIPCTRLX_DEFERRED_HIDE_TIMER_ID);
	memset(&m_tiDeferredHide, 0, sizeof(m_tiDeferredHide));
	m_uDeferredHideMessage = 0;
}

void CToolTipCtrlX::ScheduleDeferredHide(UINT uMessage, const TOOLINFO* pToolInfo, DWORD dwDelay)
{
	ResetDeferredHide();
	m_uDeferredHideMessage = uMessage;
	if (pToolInfo != NULL)
		m_tiDeferredHide = *pToolInfo;
	if (::IsWindow(m_hWnd))
		SetTimer(TOOLTIPCTRLX_DEFERRED_HIDE_TIMER_ID, dwDelay > 0 ? dwDelay : 1, NULL);
}

DWORD CToolTipCtrlX::GetRemainingMinimumVisibleTime() const
{
	if (m_dwMinimumVisibleTime == 0 || m_dwVisibleSinceTick == 0)
		return 0;

	const DWORD dwElapsed = GetTickCount() - m_dwVisibleSinceTick;
	return (dwElapsed >= m_dwMinimumVisibleTime) ? 0 : (m_dwMinimumVisibleTime - dwElapsed);
}

LRESULT CToolTipCtrlX::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (!m_bProcessingDeferredMessage) {
		if (message == TTM_TRACKACTIVATE && wParam != FALSE)
			ResetDeferredHide();

		if (message == TTM_UPDATETIPTEXT && m_bDeferVisibleUpdates && IsTooltipVisible()) {
			const TOOLINFO* pToolInfo = reinterpret_cast<const TOOLINFO*>(lParam);
			TOOLINFO tiCurrent = {};
			if (pToolInfo != NULL && TryGetCurrentToolInfo(tiCurrent) && IsSameTool(*pToolInfo, tiCurrent)) {
				StoreDeferredTextUpdate(*pToolInfo);
				return 0;
			}
		}

		if (message == TTM_UPDATE && m_bHasDeferredTextUpdate && IsTooltipVisible())
			return 0;

		if ((message == TTM_POP || (message == TTM_TRACKACTIVATE && wParam == FALSE)) && IsTooltipVisible()) {
			const DWORD dwDelay = GetRemainingMinimumVisibleTime();
			if (dwDelay > 0) {
				const TOOLINFO* pToolInfo = reinterpret_cast<const TOOLINFO*>(lParam);
				ScheduleDeferredHide(message, pToolInfo, dwDelay);
				return 0;
			}
		}
	}

	const LRESULT lResult = __super::WindowProc(message, wParam, lParam);

	if (!m_bProcessingDeferredMessage
		&& m_bHasDeferredTextUpdate
		&& !IsTooltipVisible()
		&& (message == TTM_POP
			|| (message == TTM_TRACKACTIVATE && wParam == FALSE)
			|| (message == WM_SHOWWINDOW && wParam == FALSE)
			|| message == WM_WINDOWPOSCHANGED))
	{
		ApplyDeferredTextUpdate();
	}

	return lResult;
}

void CToolTipCtrlX::ApplyTooltipStyles()
{
	SendMessage(TTM_SETMAXTIPWIDTH, 0, SHRT_MAX);

	// Win98/Win2000: Preventive, turning off tooltip animations should help in getting around
	// some glitches when using customized tooltip drawing. Though, doesn't seem to help a lot.
	if (theApp.m_ullComCtrlVer <= MAKEDLLVERULL(5, 81, 0, 0))
		ModifyStyle(0, TTS_NOANIMATE | TTS_NOFADE);
}

void CToolTipCtrlX::ResetSystemMetrics()
{
	m_fontBold.DeleteObject();
	m_fontNormal.DeleteObject();

	NONCLIENTMETRICS ncm;
	ncm.cbSize = (UINT)sizeof(NONCLIENTMETRICS);
	if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0))
		memcpy(&m_lfNormal, &ncm.lfStatusFont, sizeof(m_lfNormal));
	else
		memset(&m_lfNormal, 0, sizeof m_lfNormal);
	m_rcScreen = CRect(0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN));
	m_iScreenWidth4 = m_rcScreen.Width() / 4;
}

void CToolTipCtrlX::OnNmThemeChanged(LPNMHDR, LRESULT *pResult)
{
	ResetSystemMetrics();
	*pResult = 0;
}

void CToolTipCtrlX::OnSysColorChange()
{
	ResetSystemMetrics();
	CToolTipCtrl::OnSysColorChange();
}

void CToolTipCtrlX::OnSettingChange(UINT uFlags, LPCTSTR lpszSection)
{
	ResetSystemMetrics();
	CToolTipCtrl::OnSettingChange(uFlags, lpszSection);
}

void CToolTipCtrlX::CustomPaint(LPNMTTCUSTOMDRAW pNMCD)
{
	if (pNMCD == nullptr)
		return;

	CWnd *pwnd = CWnd::FromHandle(pNMCD->nmcd.hdr.hwndFrom);
	CDC *pdc = CDC::FromHandle(pNMCD->nmcd.hdc);

	if (pwnd == nullptr || pdc == nullptr)
		return;

	// Windows Vista (General)
	// -----------------------
	// *Need* to use (some aspects) of the 'TOOLTIP' theme to get typical Vista tooltips.
	//
	// Windows Vista *without* SP1
	// ---------------------------
	// The Vista 'TOOLTIP' theme offers a bold version of the standard tooltip font via
	// the TTP_STANDARDTITLE part id. Furthermore TTP_STANDARDTITLE is the same font as
	// the standard tooltip font (TTP_STANDARD). So, the 'TOOLTIP' theme can get used
	// thoroughly.
	//
	// Windows Vista *with* SP1
	// ------------------------
	// The Vista SP1(!) 'TOOLTIP' theme does *not* offer a bold font. Keep
	// in mind that TTP_STANDARDTITLE does not return a bold font. Keep also in mind
	// that TTP_STANDARDTITLE is even a *different* font than TTP_STANDARD!
	// Which means, that TTP_STANDARDTITLE should *not* be used within the same line
	// as TTP_STANDARD. It just looks weird. TTP_STANDARDTITLE could be used for a
	// single line (the title line), but it would though not give us a bold font.
	// So, actually we can use the Vista 'TOOLTIP' theme only for getting the proper
	// tooltip background.
	//
	// Windows XP
	// ----------
	// Can *not* use the 'TOOLTIP' theme at all because it would give us only a (non-bold)
	// black font on a white tooltip window background. Seems that the 'TOOLTIP' theme under
	// WinXP is just using the default Window values (black + white) and does not
	// use any of the tooltip specific Window metrics...
	//
	CString strText;
	pwnd->GetWindowText(strText);

	RECT rcWnd;
	pwnd->GetWindowRect(&rcWnd);

	CFont *pOldDCFont = NULL;

	// If needed, create the standard tooltip font which was queried from the system metrics
	if (m_fontNormal.m_hObject == NULL && m_lfNormal.lfHeight != 0)
		VERIFY(m_fontNormal.CreateFontIndirect(&m_lfNormal));

	// Select the tooltip font
	if (m_fontNormal.m_hObject != NULL) {
		pOldDCFont = pdc->SelectObject(&m_fontNormal);

		// If needed, create the bold version of the tooltip font by deriving it from the standard font
		if (m_bCol1Bold && m_fontBold.m_hObject == NULL) {
			LOGFONT lf;
			m_fontNormal.GetLogFont(&lf);
			lf.lfWeight = FW_BOLD;
			VERIFY(m_fontBold.CreateFontIndirect(&lf));
		}
	}

	const TooltipThemeColors palette = GetTooltipThemeColors();
	TooltipHeaderIcon headerIcon = {};
	const bool bHasHeaderIcon = TryGetTooltipHeaderIcon(GetSafeHwnd(), headerIcon);
	pdc->SetTextColor(palette.crValueText);

	// Auto-format the text only if explicitly requested. Otherwise we would also format
	// single line tooltips for regular list items, and if those list items contain ':'
	// characters they would be shown partially in bold. For performance reasons, the
	// auto-format is to be requested by appending the TOOLTIP_AUTOFORMAT_SUFFIX_CH
	// character. Appending, because we can remove that character efficiently without
	// re-allocating the entire string.
	bool bAutoFormatText = (strText.Right(1)[0] == TOOLTIP_AUTOFORMAT_SUFFIX_CH);
	if (bAutoFormatText)
		strText.Truncate(strText.GetLength() - 1); // truncate the TOOLTIP_AUTOFORMAT_SUFFIX_CH char
	const int iCaptionBreak = bAutoFormatText ? strText.Find(_T("\n<br_head>\n")) : -1;
	const bool bHasCaption = (iCaptionBreak >= 0);
	bool bShowFileIcon = m_bShowFileIcon && bAutoFormatText;
	const bool bShowHeaderIcon = bHasHeaderIcon && !bShowFileIcon;
	const int iHeaderIconSpacing = bShowHeaderIcon ? 8 : 0;
	const int iHeaderIconDrawingWidth = bShowHeaderIcon ? (headerIcon.size.cx + iHeaderIconSpacing) : 0;
	const bool bFlagsAvailable = theApp.geolite2 && theApp.geolite2->ShowCountryFlag()
		&& theApp.geolite2->GetFlagImageList() && theApp.geolite2->GetFlagImageList()->GetSafeHandle() != NULL;
	if (bShowFileIcon) {
		int iPosNL = strText.Find(_T('\n'));
		if (iPosNL > 0) {
			int iPosColon = strText.Find(_T(':'));
			if (iPosColon < iPosNL)
				bShowFileIcon = false; // 1st line does not contain a filename
		}
	}

	int iTextHeight = 0;
	int iMaxCol1Width = 0;
	int iMaxCol2Width = 0;
	int iMaxSingleLineWidth = 0;
	int iCaptionHeight = 0;
	static const int iLineHeightOff = 1;
	const int iCaptionEnd = bHasCaption ? iCaptionBreak : -1;
	const int iIconMinYBorder = bShowFileIcon ? 3 : 0;
	const int iIconWidth = bShowFileIcon ? theApp.GetBigSytemIconSize().cx : 0;
	const int iIconHeight = bShowFileIcon ? theApp.GetBigSytemIconSize().cy : 0;
	const int iIconDrawingWidth = bShowFileIcon ? (iIconWidth + 9) : 0;
	CSize siz, sizText;
	CRect rcExtent;
	static const RECT rcBounding = { 0, 0, 32767, 32767 };
	bool bMainTitleMeasured = false;
	for (int iPos = 0; iPos >= 0;) {
		const int iLineStart = iPos;
		const CString &strLine(GetNextString(strText, _T('\n'), iPos));
		CString strLineText;
		int iFlagIndex = -1;
		int iFlagPos = -1;
		const bool bFlagMarker = ParseFlagMarker(strLine, strLineText, iFlagIndex, iFlagPos);
		const bool bLineHasFlag = bFlagsAvailable && bFlagMarker;
		const int iLineFlagWidth = bLineHasFlag ? (FLAG_WIDTH + 6) : 0;
		const bool bLineInCaption = bHasCaption && iLineStart <= iCaptionEnd && iPos <= iCaptionEnd + strLine.GetLength();
		int iColon = bAutoFormatText ? strLineText.Find(_T(':')) : -1;
		const bool bFlagInCol2 = bLineHasFlag && iColon >= 0 && iFlagPos > iColon;
		const int iCol2FlagWidth = bFlagInCol2 ? iLineFlagWidth : 0;
		if (iColon >= 0) {
			CFont *pOldFont = m_bCol1Bold ? pdc->SelectObject(&m_fontBold) : NULL;
			siz = pdc->GetTextExtent(strLineText, iColon + 1);
			if (pOldFont)
				pdc->SelectObject(pOldFont);
			iMaxCol1Width = maxi(iMaxCol1Width, (int)(siz.cx + ((bShowFileIcon && iPos <= iCaptionEnd + strLine.GetLength()) ? iIconDrawingWidth : 0)));
			const int iLineHeight = max(siz.cy, bLineHasFlag ? FLAG_HEIGHT : 0) + iLineHeightOff;
			iTextHeight = max(iTextHeight, iLineHeight); // Ensure tallest line height is used for key/value rows
			if (bLineInCaption)
				iCaptionHeight += iLineHeight;
			else
				sizText.cy += iLineHeight;

			LPCTSTR pszCol2 = (LPCTSTR)strLineText + iColon + 1;
			while (_istspace(*pszCol2))
				++pszCol2;
			if (*pszCol2 != _T('\0')) {
				siz = pdc->GetTextExtent(pszCol2, (int)(((LPCTSTR)strLineText + strLineText.GetLength()) - pszCol2));
				iMaxCol2Width = max(iMaxCol2Width, siz.cx + iCol2FlagWidth);
			} else if (iCol2FlagWidth > 0) {
				iMaxCol2Width = max(iMaxCol2Width, iCol2FlagWidth);
			}
		} else if (bShowFileIcon && iPos <= iCaptionEnd && iPos == strLine.GetLength() + 1) {
			// file name, printed bold on top without any tabbing or desc
			CFont *pOldFont = m_bCol1Bold ? pdc->SelectObject(&m_fontBold) : NULL;
			siz = pdc->GetTextExtent(strLineText);
			if (pOldFont)
				pdc->SelectObject(pOldFont);
			iMaxSingleLineWidth = max(iMaxSingleLineWidth, siz.cx + iIconDrawingWidth + (bMainTitleMeasured ? 0 : iHeaderIconDrawingWidth));
			iCaptionHeight += siz.cy + iLineHeightOff;
			if (!strLineText.IsEmpty())
				bMainTitleMeasured = true;
		} else if (!strLine.IsEmpty() && strLine.Compare(_T("<br>")) != 0 && strLine.Compare(_T("<br_head>")) != 0) {
			siz = pdc->GetTextExtent(strLineText);
			const int iLineHeight = max(siz.cy, bLineHasFlag ? FLAG_HEIGHT : 0) + iLineHeightOff;
			iMaxSingleLineWidth = maxi(iMaxSingleLineWidth, (int)(siz.cx + iLineFlagWidth + ((bShowFileIcon && iPos <= iCaptionEnd) ? iIconDrawingWidth : 0) + ((bLineInCaption && !bMainTitleMeasured) ? iHeaderIconDrawingWidth : 0)));
			if (bLineInCaption)
				iCaptionHeight += iLineHeight;
			else
				sizText.cy += iLineHeight;
			if (bLineInCaption)
				bMainTitleMeasured = true;
		} else {
			// TODO: Would need to use 'GetTabbedTextExtent' here, but do we actually use 'tabbed' text here at all ??
			siz = pdc->GetTextExtent(_T(" "), 1);
			sizText.cy += siz.cy + iLineHeightOff;
		}
	}
	if (bShowFileIcon && iCaptionEnd > 0)
		iCaptionHeight = maxi(iCaptionHeight, (int)(theApp.GetBigSytemIconSize().cy + (2 * iIconMinYBorder)));
	sizText.cy += iCaptionHeight;

	iMaxCol1Width = min(m_iScreenWidth4, iMaxCol1Width);
	iMaxCol2Width = min(m_iScreenWidth4 * 2, iMaxCol2Width);

	static const int iMiddleMargin = 6;
	iMaxSingleLineWidth = maxi(iMaxSingleLineWidth, iMaxCol1Width + iMiddleMargin + iMaxCol2Width);
	iMaxSingleLineWidth = min(m_iScreenWidth4 * 3, iMaxSingleLineWidth);
	sizText.cx = iMaxSingleLineWidth;

	if (pNMCD->uDrawFlags & DT_CALCRECT) {
		pNMCD->nmcd.rc.left = rcWnd.left;
		pNMCD->nmcd.rc.top = rcWnd.top;
		pNMCD->nmcd.rc.right = rcWnd.left + sizText.cx;
		pNMCD->nmcd.rc.bottom = rcWnd.top + sizText.cy;
	} else {
		pwnd->ScreenToClient(&rcWnd);

		const int iOldBkMode = pdc->SetBkMode(TRANSPARENT);
		const COLORREF crOldBkColor = pdc->SetBkColor(palette.crBackground);
		pdc->FillSolidRect(&rcWnd, palette.crBackground);
		if (iCaptionHeight > 0) {
			CRect rcCaption(rcWnd);
			rcCaption.bottom = min(rcWnd.bottom, rcWnd.top + iCaptionHeight);
			pdc->FillSolidRect(&rcCaption, palette.crCaptionBackground);
		}
		DrawTooltipBorder(pdc, rcWnd, palette.crBorder);

		CPoint ptText(pNMCD->nmcd.rc.left, pNMCD->nmcd.rc.top);
		bool bMainTitleDrawn = false;
		bool bHeaderIconDrawn = false;

		for (int iPos = 0; iPos >= 0;) {
			const int iLineStart = iPos;
			const CString &strLine(GetNextString(strText, _T('\n'), iPos));
		CString strLineText;
		int iFlagIndex = -1;
		int iFlagPos = -1;
		const bool bFlagMarker = ParseFlagMarker(strLine, strLineText, iFlagIndex, iFlagPos);
		const bool bLineHasFlag = bFlagsAvailable && bFlagMarker;
		const int iLineFlagWidth = bLineHasFlag ? (FLAG_WIDTH + 6) : 0;
		const bool bLineInCaption = bHasCaption && iLineStart <= iCaptionEnd && iPos <= iCaptionEnd + strLine.GetLength();
		const bool bIsMainTitleLine = bLineInCaption && !bMainTitleDrawn && !strLine.IsEmpty() && strLine != _T("<br>") && strLine != _T("<br_head>");
		int iColon = bAutoFormatText ? strLineText.Find(_T(':')) : -1;
		const bool bFlagInCol2 = bLineHasFlag && iColon >= 0 && iFlagPos > iColon;
			CRect rcDT;
			if (!bShowFileIcon || (unsigned)iPos > (unsigned)iCaptionEnd + strLine.GetLength())
				rcDT.SetRect(ptText.x, ptText.y, ptText.x + iMaxCol1Width, ptText.y + iTextHeight);
			else
				rcDT.SetRect(ptText.x + iIconDrawingWidth, ptText.y, ptText.x + iMaxCol1Width, ptText.y + iTextHeight);
			if (iColon >= 0) {
				// don't draw empty <col1> strings (they are still handy to use for skipping the <col1> space)
				if (iColon > 0) {
					CFont *pOldFont = m_bCol1Bold ? pdc->SelectObject(&m_fontBold) : NULL;
					pdc->SetTextColor(palette.crKeyText);
					pdc->DrawText(strLineText, iColon + 1, &rcDT, m_dwCol1DrawTextFlags);
					if (pOldFont)
						pdc->SelectObject(pOldFont);
				}

				LPCTSTR pszCol2 = (LPCTSTR)strLineText + iColon + 1;
				while (_istspace(*pszCol2))
					++pszCol2;
				if (*pszCol2 != _T('\0') || bFlagInCol2) {
					rcDT.left = ptText.x + iMaxCol1Width + iMiddleMargin;
					rcDT.right = rcDT.left + iMaxCol2Width;
					pdc->SetTextColor(palette.crValueText);
					if (bFlagInCol2 && theApp.geolite2 && theApp.geolite2->GetFlagImageList() && theApp.geolite2->GetFlagImageList()->GetSafeHandle() != NULL) {
						const int iPosY = ptText.y + max(0, (iTextHeight - FLAG_HEIGHT) / 2);
						::ImageList_Draw(theApp.geolite2->GetFlagImageList()->GetSafeHandle(), iFlagIndex, pdc->GetSafeHdc(), rcDT.left, iPosY, ILD_TRANSPARENT);
						rcDT.left += iLineFlagWidth;
					}
					if (*pszCol2 != _T('\0'))
						pdc->DrawText(pszCol2, (int)(((LPCTSTR)strLineText + strLineText.GetLength()) - pszCol2), &rcDT, m_dwCol2DrawTextFlags);
				}

				ptText.y += iTextHeight;
			} else if (bShowFileIcon && iPos <= iCaptionEnd && iPos == strLine.GetLength() + 1) {
				CRect rect(ptText.x + iIconDrawingWidth, ptText.y, ptText.x + iMaxSingleLineWidth - ((bShowHeaderIcon && !bHeaderIconDrawn) ? iHeaderIconDrawingWidth : 0), ptText.y + iTextHeight);
				// first line on special file icon tab - draw icon and bold filename
				CFont *pOldFont = m_bCol1Bold ? pdc->SelectObject(&m_fontBold) : NULL;
				pdc->SetTextColor(bMainTitleDrawn ? palette.crTitleText : palette.crMainTitleText);
				pdc->DrawText(strLineText, rect, m_dwCol1DrawTextFlags);
				if (pOldFont)
					pdc->SelectObject(pOldFont);
				if (bShowHeaderIcon && !bHeaderIconDrawn) {
					CRect rcIcon(rect);
					rcIcon.left = ptText.x;
					DrawTooltipHeaderIcon(headerIcon, pdc, rcIcon);
					bHeaderIconDrawn = true;
				}
				bMainTitleDrawn = true;

				ptText.y += iTextHeight;
				int iImage = theApp.GetFileTypeSystemImageIdx(strLineText, -1, true);
				if (theApp.GetBigSystemImageList() != NULL) {
					int iPosY = rcDT.top;
					if (iCaptionHeight > iIconHeight)
						iPosY += (iCaptionHeight - iIconHeight) / 2;
					::ImageList_Draw(theApp.GetBigSystemImageList(), iImage, pdc->GetSafeHdc(), ptText.x, iPosY, ILD_TRANSPARENT);
				}
			} else {
				if (bLineHasFlag && strLine != _T("<br>") && strLine != _T("<br_head>")) {
					const int iFlagLineHeight = max(iTextHeight, FLAG_HEIGHT + iLineHeightOff);
					if (theApp.geolite2 && theApp.geolite2->GetFlagImageList() && theApp.geolite2->GetFlagImageList()->GetSafeHandle() != NULL) {
						const int iPosY = ptText.y + max(0, (iFlagLineHeight - FLAG_HEIGHT) / 2);
						::ImageList_Draw(theApp.geolite2->GetFlagImageList()->GetSafeHandle(), iFlagIndex, pdc->GetSafeHdc(), ptText.x, iPosY, ILD_TRANSPARENT);
					}
					CRect rcLine(ptText.x + iLineFlagWidth, ptText.y, ptText.x + iMaxSingleLineWidth, ptText.y + iFlagLineHeight);
					pdc->SetTextColor(palette.crValueText);
					pdc->DrawText(strLineText.IsEmpty() ? _T(" ") : strLineText, rcLine, m_dwCol1DrawTextFlags);
					ptText.y += iFlagLineHeight;
				} else {
					bool bIsBrHeadLine = !bAutoFormatText || (strLine != _T("<br>"));
					if (!bIsBrHeadLine || (bIsBrHeadLine = (strLine == _T("<br_head>"))) == true) {
						CPen pen(PS_SOLID, 1, palette.crSeparator);
						CPen *pOP = pdc->SelectObject(&pen);
						if (bIsBrHeadLine)
							ptText.y = iCaptionHeight;
						pdc->MoveTo(ptText.x, ptText.y + ((iTextHeight - 2) / 2));
						pdc->LineTo(ptText.x + iMaxSingleLineWidth, ptText.y + ((iTextHeight - 2) / 2));
						ptText.y += iTextHeight;
						pdc->SelectObject(pOP);
					} else {
						// Text is written in the currently selected font. If 'nTabPositions' is 0 and 'lpnTabStopPositions' is NULL,
						// tabs are expanded to eight times the average character width.
						if (bLineInCaption) {
							CFont* pOldFont = m_bCol1Bold ? pdc->SelectObject(&m_fontBold) : NULL;
							pdc->SetTextColor(bMainTitleDrawn ? palette.crTitleText : palette.crMainTitleText);
							CRect rcTitle(ptText.x + ((bShowHeaderIcon && !bHeaderIconDrawn && bIsMainTitleLine) ? iHeaderIconDrawingWidth : 0), ptText.y, ptText.x + iMaxSingleLineWidth, ptText.y + iTextHeight);
							pdc->DrawText(strLineText.IsEmpty() ? _T(" ") : strLineText, rcTitle, m_dwCol1DrawTextFlags);
							if (pOldFont)
								pdc->SelectObject(pOldFont);
							if (bShowHeaderIcon && !bHeaderIconDrawn && bIsMainTitleLine) {
								CRect rcIcon(ptText.x, ptText.y, ptText.x + headerIcon.size.cx, ptText.y + iTextHeight);
								DrawTooltipHeaderIcon(headerIcon, pdc, rcIcon);
								bHeaderIconDrawn = true;
							}
							if (!strLineText.IsEmpty())
								bMainTitleDrawn = true;
							ptText.y += iTextHeight;
						} else {
							pdc->SetTextColor(palette.crValueText);
							if (strLineText.IsEmpty()) // Win98: To draw an empty line we need to output at least a space.
								siz = pdc->TabbedTextOut(ptText.x, ptText.y, _T(" "), 1, NULL, 0);
							else
								siz = pdc->TabbedTextOut(ptText.x, ptText.y, strLineText, strLineText.GetLength(), NULL, 0);
							ptText.y += siz.cy + iLineHeightOff;
						}
					}
				}
			}
		}
		if (crOldBkColor != CLR_INVALID)
			pdc->SetBkColor(crOldBkColor);
		if (iOldBkMode != 0)
			pdc->SetBkMode(iOldBkMode);
	}
	if (pOldDCFont)
		pdc->SelectObject(pOldDCFont);
}

void EnsureWindowVisible(const RECT &rcScreen, RECT &rc)
{
	// 1st: Move the window towards the left side, in case it exceeds the desktop width.
	// 2nd: Move the window towards the right side, in case it exceeds the desktop width.
	//
	// This order of actions ensures that in case the window is too large for the desktop,
	// the user will though see the top-left part of the window.
	if (rc.right > rcScreen.right) {
		rc.left -= rc.right - rcScreen.right;
		rc.right = rcScreen.right;
	}
	if (rc.left < rcScreen.left) {
		rc.right += rcScreen.left - rc.left;
		rc.left = rcScreen.left;
	}

	// Same logic for bottom/top
	if (rc.bottom > rcScreen.bottom) {
		rc.top -= rc.bottom - rcScreen.bottom;
		rc.bottom = rcScreen.bottom;
	}
	if (rc.top < rcScreen.top) {
		rc.bottom += rcScreen.top - rc.top;
		rc.top = rcScreen.top;
	}
}

BOOL CToolTipCtrlX::OnTTShow(LPNMHDR pNMHDR, LRESULT *pResult)
{
	ResetDeferredHide();
	ApplyDeferredTextUpdate();
	m_dwVisibleSinceTick = GetTickCount();

	CWnd* pParentWnd = GetParent();
	if (pParentWnd != NULL && pParentWnd->IsKindOf(RUNTIME_CLASS(CMuleListCtrl))) {
		CMuleListCtrl* pListCtrl = static_cast<CMuleListCtrl*>(pParentWnd);
		CToolTipCtrl* pInfoTip = pListCtrl->GetToolTips();
		if (pInfoTip != NULL && pInfoTip->GetSafeHwnd() == m_hWnd) {
			if (pListCtrl->HandleNativeInfoTipShow(m_hWnd, pResult))
				return TRUE;
		}
	}

	// Win98/Win2000: The only chance to resize a tooltip window is to do it within the TTN_SHOW notification.
	if (theApp.m_ullComCtrlVer <= MAKEDLLVERULL(5, 81, 0, 0)) {
		NMTTCUSTOMDRAW nmttcd = {};
		nmttcd.uDrawFlags = DT_NOPREFIX | DT_CALCRECT | DT_EXTERNALLEADING | DT_EXPANDTABS | DT_WORDBREAK;
		nmttcd.nmcd.hdr = *pNMHDR;
		nmttcd.nmcd.dwDrawStage = CDDS_PREPAINT;
		nmttcd.nmcd.hdc = ::GetDC(pNMHDR->hwndFrom);
		CustomPaint(&nmttcd);
		::ReleaseDC(pNMHDR->hwndFrom, nmttcd.nmcd.hdc);

		CRect rcWnd(nmttcd.nmcd.rc);
		AdjustRect(&rcWnd);

		// Win98/Win2000: We have to explicitly ensure that the tooltip window remains within the visible desktop window.
		EnsureWindowVisible(m_rcScreen, rcWnd);

		// Win98/Win2000: The only chance to resize a tooltip window is to do it within the TTN_SHOW notification.
		// Win98/Win2000: Must *not* specify 'SWP_NOZORDER' - some of the tooltip windows may get drawn behind(!) the application window!
		::SetWindowPos(pNMHDR->hwndFrom, NULL, rcWnd.left, rcWnd.top, rcWnd.Width(), rcWnd.Height(), SWP_NOACTIVATE /*| SWP_NOZORDER*/);

		*pResult = TRUE; // Windows API: Suppress default positioning
		return TRUE;	 // MFC API:     Suppress further routing of this message
	}

	// If the TTN_SHOW notification is not sent to the subclassed tooltip control, we would lose the
	// exact positioning of in-place tooltips which is performed by the tooltip control by default.
	// Thus it is important that we tell MFC (not the Windows API in that case) to further route this message.
	*pResult = FALSE;	// Windows API: Perform default positioning
	return FALSE;		// MFC API.     Perform further routing of this message (to the subclassed tooltip control)
}

BOOL CToolTipCtrlX::OnTTPop(LPNMHDR, LRESULT *pResult)
{
	ResetDeferredHide();
	m_dwVisibleSinceTick = 0;
	*pResult = 0;
	return FALSE;
}

void CToolTipCtrlX::OnNmCustomDraw(LPNMHDR pNMHDR, LRESULT *pResult)
{
	LPNMTTCUSTOMDRAW pNMCD = reinterpret_cast<LPNMTTCUSTOMDRAW>(pNMHDR);

	// For each tooltip which is to be shown Windows invokes the draw function at least 2 times.
	//	1st invocation: to get the drawing rectangle
	//	2nd invocation: to draw the actual tooltip window contents
	//
	// 'DrawText' flags for the 1st and 2nd/3rd call
	// ---------------------------------------------
	// NMTTCUSTOMDRAW 00000e50	DT_NOPREFIX | DT_CALCRECT | DT_EXTERNALLEADING | DT_EXPANDTABS | DT_WORDBREAK
	// TTN_SHOW
	// NMTTCUSTOMDRAW 00000a50	DT_NOPREFIX |               DT_EXTERNALLEADING | DT_EXPANDTABS | DT_WORDBREAK
	// --an additional NMTTCUSTOMDRAW may follow which is identical to the 2nd--
	// NMTTCUSTOMDRAW 00000a50	DT_NOPREFIX |               DT_EXTERNALLEADING | DT_EXPANDTABS | DT_WORDBREAK


	if (theApp.m_ullComCtrlVer <= MAKEDLLVERULL(5, 81, 0, 0)) {
		// Win98/Win2000: Resize and position the tooltip window in TTN_SHOW.
		// Win98/Win2000: Customize the tooltip window in CDDS_POSTPAINT.
		if (pNMCD->nmcd.dwDrawStage == CDDS_PREPAINT) {
			// PROBLEM: Windows will draw the default tooltip during the non-DT_CALCRECT cycle,
			// and if the system is very slow (or when using remote desktop), the default tooltip
			// may be visible for a second. However, we need to draw the customized tooltip after
			// CDDS_POSTPAINT otherwise it won't be visible at all.

			// Cheap solution: Let windows draw the text with the background color, so the glitch
			// is not that much visible.
			SetTextColor(pNMCD->nmcd.hdc, GetCustomSysColor(COLOR_INFOBK));
			*pResult = CDRF_NOTIFYPOSTPAINT;
			return;
		}
		if (pNMCD->nmcd.dwDrawStage == CDDS_POSTPAINT) {
			CustomPaint(pNMCD);
			*pResult = CDRF_SKIPDEFAULT;
			return;
		}
	} else {
		// XP/Vista: Resize, position and customize the tooltip window all in 'CDDS_PREPAINT'.
		if (pNMCD->nmcd.dwDrawStage == CDDS_PREPAINT) {
			CustomPaint(pNMCD);
			*pResult = CDRF_SKIPDEFAULT;
			return;
		}
	}
	*pResult = CDRF_DODEFAULT;
}

void CToolTipCtrlX::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TOOLTIPCTRLX_DEFERRED_HIDE_TIMER_ID) {
		const UINT uDeferredHideMessage = m_uDeferredHideMessage;
		const TOOLINFO tiDeferredHide = m_tiDeferredHide;
		ResetDeferredHide();

		m_bProcessingDeferredMessage = true;
		if (uDeferredHideMessage == TTM_TRACKACTIVATE)
			__super::WindowProc(TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tiDeferredHide));
		else if (uDeferredHideMessage == TTM_POP)
			__super::WindowProc(TTM_POP, 0, 0);
		m_bProcessingDeferredMessage = false;

		if (!IsTooltipVisible())
			ApplyDeferredTextUpdate();
		return;
	}

	CToolTipCtrl::OnTimer(nIDEvent);
}

void EnsureMfcThreadToolTipCtrlX(CWnd *pOwner)
{
	if (pOwner == NULL || !::IsWindow(pOwner->GetSafeHwnd()))
		return;

	AFX_MODULE_THREAD_STATE* pThreadState = AfxGetModuleThreadState();
	if (pThreadState == NULL)
		return;

	CToolTipCtrl* pToolTip = pThreadState->m_pToolTip;
	if (pToolTip != NULL && pToolTip->GetOwner() != pOwner) {
		pToolTip->DestroyWindow();
		delete pToolTip;
		pThreadState->m_pToolTip = NULL;
		pToolTip = NULL;
	}

	if (pToolTip != NULL && pToolTip->IsKindOf(RUNTIME_CLASS(CToolTipCtrlX)))
		return;

	if (pToolTip != NULL) {
		pToolTip->DestroyWindow();
		delete pToolTip;
		pThreadState->m_pToolTip = NULL;
	}

	CToolTipCtrlX *pToolTipX = new CToolTipCtrlX();
	if (!pToolTipX->Create(pOwner, TTS_ALWAYSTIP)) {
		delete pToolTipX;
		return;
	}

	pToolTipX->SendMessage(TTM_ACTIVATE, FALSE);
	pThreadState->m_pToolTip = pToolTipX;
}
