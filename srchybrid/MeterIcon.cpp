// Created: 04/02/2001 {mm/dm/yyyyy}
// Written by: Anish Mistry http://am-productions.yi.org/
/* This code is licensed under the GNU GPL.  See License.txt or (https://www.gnu.org/copyleft/gpl.html). */
#include "stdafx.h"
#include "MeterIcon.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CMeterIcon::CMeterIcon()
	: m_sDimensions{ 16, 16 }
	, m_hFrame()
	, m_pLimits()
	, m_pColors()
	, m_crBorderColor(RGB(0, 0, 0))
	, m_nSpacingWidth()
	, m_nMaxVal(100)
	, m_nNumBars(2)
	, m_nEntries()
	, m_bInit()
{
}

CMeterIcon::~CMeterIcon()
{
	// free color list memory
	delete[] m_pLimits;
	delete[] m_pColors;
}

COLORREF CMeterIcon::GetMeterColor(int nLevel) const
// it the nLevel is greater than the values defined in m_pLimits the last value in the array is used
{// begin GetMeterColor
	for (int i = 0; i < m_nEntries; ++i)
		if (nLevel <= m_pLimits[i])
			return m_pColors[i];

	// default to the last entry
	return m_pColors[m_nEntries - 1];
}// end GetMeterColor

HICON CMeterIcon::CreateMeterIcon(const int *pBarData)
// the returned icon must be cleaned up using DestroyIcon()
{// begin CreateMeterIcon
	int cx = m_sDimensions.cx;
	int cy = m_sDimensions.cy;

	BITMAPINFOHEADER bmih = {};
	bmih.biSize = sizeof(BITMAPINFOHEADER);
	bmih.biWidth = cx;
	bmih.biHeight = -cy; // negative = top-down
	bmih.biPlanes = 1;
	bmih.biBitCount = 32;
	bmih.biCompression = BI_RGB;

	HDC hScreenDC = ::GetDC(HWND_DESKTOP);
	if (hScreenDC == NULL)
		return NULL;

	// Output DIB
	UINT32 *pBits = NULL;
	HBITMAP hbmColor = ::CreateDIBSection(hScreenDC, reinterpret_cast<BITMAPINFO*>(&bmih), DIB_RGB_COLORS, reinterpret_cast<void**>(&pBits), NULL, 0);

	// Temp DIB for rendering icon
	UINT32 *pTempBits = NULL;
	HBITMAP hTempBmp = ::CreateDIBSection(hScreenDC, reinterpret_cast<BITMAPINFO*>(&bmih), DIB_RGB_COLORS, reinterpret_cast<void**>(&pTempBits), NULL, 0);

	HBITMAP hbmMask = ::CreateBitmap(cx, cy, 1, 1, NULL);

	::ReleaseDC(NULL, hScreenDC);

	if (hbmColor == NULL || pBits == NULL || hTempBmp == NULL || pTempBits == NULL || hbmMask == NULL) {
		if (hbmColor) ::DeleteObject(hbmColor);
		if (hTempBmp) ::DeleteObject(hTempBmp);
		if (hbmMask) ::DeleteObject(hbmMask);
		return NULL;
	}

	// Render icon to temp DIB
	HDC hTempDC = ::CreateCompatibleDC(NULL);
	HGDIOBJ hOldTemp = ::SelectObject(hTempDC, hTempBmp);
	memset(pTempBits, 0, cx * cy * sizeof(UINT32));
	::DrawIconEx(hTempDC, 0, 0, m_hFrame, cx, cy, 0, NULL, DI_NORMAL);
	::GdiFlush();
	::SelectObject(hTempDC, hOldTemp);
	::DeleteDC(hTempDC);

	// Clear mask and output DIB
	HDC hMaskDC = ::CreateCompatibleDC(NULL);
	HGDIOBJ hOldMask = ::SelectObject(hMaskDC, hbmMask);
	::BitBlt(hMaskDC, 0, 0, cx, cy, NULL, 0, 0, BLACKNESS);
	::SelectObject(hMaskDC, hOldMask);
	::DeleteDC(hMaskDC);
	memset(pBits, 0, cx * cy * sizeof(UINT32));

	// Check if we need bars
	bool bHasBars = false;
	for (int i = 0; i < m_nNumBars; ++i) {
		if (pBarData[i] > 0) { bHasBars = true; break; }
	}

	if (bHasBars) {
		// Find left padding (columns with purely transparent pixels)
		int nLeftPadding = 0;
		for (int x = 0; x < cx; ++x) {
			bool bHasContent = false;
			for (int y = 0; y < cy; ++y) {
				if ((pTempBits[y * cx + x] >> 24) > 0) { bHasContent = true; break; }
			}
			if (bHasContent) break;
			++nLeftPadding;
		}

		// Calculate shift needed for bars + 1px gap (limit to available left padding)
		int nBarAreaWidth = m_nNumBars * 2;
		int nTargetShift = nBarAreaWidth + 1;
		int nShift = min(nLeftPadding, nTargetShift);

		// Copy icon pixels shifted left
		for (int y = 0; y < cy; ++y)
			for (int x = 0; x + nShift < cx; ++x)
				pBits[y * cx + x] = pTempBits[y * cx + x + nShift];

		// Draw bars on output DIB
		for (int i = 0; i < m_nNumBars; ++i)
			DrawMeterBar(pBits, pBarData[i], i);
	} else {
		// No bars, just copy exact icon
		memcpy(pBits, pTempBits, cx * cy * sizeof(UINT32));
	}

	::DeleteObject(hTempBmp);

	ICONINFO iiNewIcon = {};
	iiNewIcon.fIcon = true;
	iiNewIcon.hbmColor = hbmColor;
	iiNewIcon.hbmMask = hbmMask;
	HICON hNewIcon = ::CreateIconIndirect(&iiNewIcon);

	::DeleteObject(hbmColor);
	::DeleteObject(hbmMask);
	return hNewIcon;

}// end CreateMeterIcon

void CMeterIcon::DrawMeterBar(UINT32 *pBits, int nLevel, int nPos)
{
	if (nLevel <= 0 || pBits == NULL)
		return;

	// Bar coordinates: thin strip on right edge (2px per bar, stacked right-to-left)
	const int nBarWidth = 2;
	int nBarLeft = m_sDimensions.cx - (m_nNumBars - nPos) * nBarWidth;
	int nBarRight = min(nBarLeft + nBarWidth, (int)m_sDimensions.cx);
	int nBarHeight = (nLevel * (m_sDimensions.cy - 1) / m_nMaxVal) + 1;
	int nBarTop = m_sDimensions.cy - nBarHeight;

	if (nBarLeft < 0) nBarLeft = 0;
	if (nBarTop < 0) nBarTop = 0;

	// Build fully opaque BGRA pixel from meter color
	COLORREF cr = GetMeterColor(nLevel);
	UINT32 pixel = GetBValue(cr) | (GetGValue(cr) << 8) | (GetRValue(cr) << 16) | (0xFFu << 24);

	for (int y = nBarTop; y < m_sDimensions.cy; ++y)
		for (int x = nBarLeft; x < nBarRight; ++x)
			pBits[y * m_sDimensions.cx + x] = pixel;
}

bool CMeterIcon::DrawIconMeter(HDC hDestDC, HDC hDestDCMask, int nLevel, int nPos)
{
	if (nLevel <= 0)
		return true; // Nothing to draw

	// Bar coordinates: thin strip on right edge (2px per bar, stacked right-to-left)
	const int nBarWidth = 2;
	int nBarLeft = m_sDimensions.cx - (m_nNumBars - nPos) * nBarWidth;
	int nBarRight = nBarLeft + nBarWidth;
	int nBarTop = m_sDimensions.cy - ((nLevel * (m_sDimensions.cy - 1) / m_nMaxVal) + 1);
	int nBarBottom = m_sDimensions.cy;

	// Draw meter bar on color DC (use meter color for both fill and border)
	COLORREF crMeter = GetMeterColor(nLevel);
	HBRUSH hBrush = ::CreateSolidBrush(crMeter);
	if (hBrush == NULL)
		return false;
	HGDIOBJ hOldBrush = ::SelectObject(hDestDC, hBrush);
	if (hOldBrush == NULL)
		return false;
	HPEN hPen = ::CreatePen(PS_SOLID, 1, crMeter);
	if (hPen == NULL)
		return false;
	HGDIOBJ hOldPen = ::SelectObject(hDestDC, hPen);
	if (hOldPen == NULL)
		return false;
	if (!::Rectangle(hDestDC, nBarLeft, nBarTop, nBarRight, nBarBottom))
		return false;
	if (!::DeleteObject(::SelectObject(hDestDC, hOldPen)))
		return false;
	if (!::DeleteObject(::SelectObject(hDestDC, hOldBrush)))
		return false;

	// Draw meter mask (make bar area opaque)
	HBRUSH hMaskBrush = ::CreateSolidBrush(RGB(0, 0, 0));
	if (hMaskBrush == NULL)
		return false;
	HGDIOBJ hOldMaskBrush = ::SelectObject(hDestDCMask, hMaskBrush);
	if (hOldMaskBrush == NULL)
		return false;
	HPEN hMaskPen = ::CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
	if (hMaskPen == NULL)
		return false;
	HGDIOBJ hOldMaskPen = ::SelectObject(hDestDCMask, hMaskPen);
	if (hOldMaskPen == NULL)
		return false;
	if (!::Rectangle(hDestDCMask, nBarLeft, nBarTop, nBarRight, nBarBottom))
		return false;
	if (!::DeleteObject(::SelectObject(hDestDCMask, hOldMaskPen)))
		return false;
	return ::DeleteObject(::SelectObject(hDestDCMask, hOldMaskBrush));
}// end DrawIconMeter


HICON CMeterIcon::SetFrame(HICON hIcon)
// return the old frame icon
{// begin SetFrame
	HICON hOld = m_hFrame;
	m_hFrame = hIcon;
	return hOld;
}// end SetFrame

HICON CMeterIcon::Create(const int *pBarData)
// must call init once before calling
{
	return m_bInit ? CreateMeterIcon(pBarData) : NULL;
}

bool CMeterIcon::Init(HICON hFrame, int nMaxVal, int nNumBars, int nSpacingWidth, int nWidth, int nHeight, COLORREF crColor)
// nWidth & nHeight are the dimensions of the icon that you want created
// nSpacingWidth is the space between the bars
// hFrame is the overlay for the bars
// crColor is the outline color for the bars
{// begin Init
	SetFrame(hFrame);
	SetWidth(nSpacingWidth);
	SetMaxValue(nMaxVal);
	SetDimensions(nWidth, nHeight);
	SetNumBars(nNumBars);
	SetBorderColor(crColor);
	m_bInit = true;
	return m_bInit;
}// end Init

SIZE CMeterIcon::SetDimensions(int nWidth, int nHeight)
// return the previous dimension
{// begin SetDimensions
	SIZE sOld = m_sDimensions;
	m_sDimensions.cx = nWidth;
	m_sDimensions.cy = nHeight;
	return sOld;
}// end SetDimensions

int CMeterIcon::SetNumBars(int nNum)
{// begin SetNumBars
	int nOld = m_nNumBars;
	m_nNumBars = nNum;
	return nOld;
}// end SetNumBars

int CMeterIcon::SetWidth(int nWidth)
{// begin SetWidth
	int nOld = m_nSpacingWidth;
	m_nSpacingWidth = nWidth;
	return nOld;
}// end SetWidth

int CMeterIcon::SetMaxValue(int nVal)
{// begin SetMaxValue
	int nOld = m_nMaxVal;
	m_nMaxVal = nVal;
	return nOld;
}// end SetMaxValue

COLORREF CMeterIcon::SetBorderColor(COLORREF crColor)
{// begin SetBorderColor
	COLORREF crOld = m_crBorderColor;
	m_crBorderColor = crColor;
	return crOld;
}// end SetBorderColor

bool CMeterIcon::SetColorLevels(const int *pLimits, const COLORREF *pColors, int nEntries)
// pLimits is an array of int that contain the upper limit for the corresponding color
{// begin SetColorLevels
	// free existing memory
	delete[] m_pLimits;
	m_pLimits = NULL; // 'new' may throw an exception
	delete[] m_pColors;
	m_pColors = NULL; // 'new' may throw an exception

	// allocate new memory
	m_pLimits = new int[nEntries];
	m_pColors = new COLORREF[nEntries];
	// copy values
	memcpy(m_pLimits, pLimits, nEntries * sizeof(*pLimits));
	memcpy(m_pColors, pColors, nEntries * sizeof(*pColors));

	m_nEntries = nEntries;
	return true;
}// end SetColorLevels