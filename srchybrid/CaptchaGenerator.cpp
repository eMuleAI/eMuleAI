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
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "StdAfx.h"
#include <atlimage.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include "CaptchaGenerator.h"
#include "OtherFunctions.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define LETTERSIZE  32
#define CROWDEDSIZE 23

// fairly simple captcha generator, might be improved if spammers think it's really worth it to solve captchas in eMule

static TCHAR const sCaptchaCharSet[] = _T("ABCDEFGHIJKLMNPQRSTUVWXYZ123456789");

CCaptchaGenerator::CCaptchaGenerator(uint32 nLetterCount)
	: m_hbmpCaptcha()
{
	ReGenerateCaptcha(nLetterCount);
}

void CCaptchaGenerator::ReGenerateCaptcha(uint32 nLetterCount)
{
	Clear();
	// CUpDownClient::ProcessCaptchaRequest verifies height and width ranges.
	int nWidth = nLetterCount > 1 ? ((LETTERSIZE)+nLetterCount * (CROWDEDSIZE)) : (LETTERSIZE);
	int nHeight = 48;
	struct {
		BITMAPINFOHEADER bmiHeader;
		RGBQUAD bmiColors[2];
	} bmiMono = { {0}, { {255, 255, 255} } };
	bmiMono.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmiMono.bmiHeader.biWidth = nWidth;
	bmiMono.bmiHeader.biHeight = nHeight;
	bmiMono.bmiHeader.biPlanes = 1;
	bmiMono.bmiHeader.biBitCount = 1;
	bmiMono.bmiHeader.biCompression = BI_RGB;
	void *pv;
	m_hbmpCaptcha = ::CreateDIBSection(NULL, (BITMAPINFO*)&bmiMono, DIB_RGB_COLORS, &pv, NULL, 0);
	bmiMono.bmiHeader.biWidth = LETTERSIZE;
	HBITMAP hBitMem = ::CreateDIBSection(NULL, (BITMAPINFO*)&bmiMono, DIB_RGB_COLORS, &pv, NULL, 0);

	int nFontSize = 40;
	LOGFONT m_LF = { 0 };
	m_LF.lfHeight = nFontSize;
	m_LF.lfWeight = FW_HEAVY;
	_tcsncpy(m_LF.lfFaceName, _T("Arial"), LF_FACESIZE - 1);
	HFONT hFont = ::CreateFontIndirect(&m_LF);

	HDC hdc = ::CreateCompatibleDC(NULL);
	HDC hdcMem = ::CreateCompatibleDC(NULL);
	HBITMAP hBitmapOld = (HBITMAP)::SelectObject(hdc, m_hbmpCaptcha);
	HBITMAP hBitMemOld = (HBITMAP)::SelectObject(hdcMem, hBitMem);
	HFONT hFontOld = (HFONT)::SelectObject(hdcMem, hFont);
	ASSERT(hdc && hdcMem && m_hbmpCaptcha && hBitMem && hFont);

	WCHAR wT[2] = { 0 };
	int xOff = (CROWDEDSIZE) / 2;
	for (uint32 n = 0; n < nLetterCount; ++n) {
		*wT = sCaptchaCharSet[rand() % (_countof(sCaptchaCharSet) - 1)];
		m_strCaptchaText += *wT;
		RECT r{ 0, 0, (LETTERSIZE), (LETTERSIZE) };
		::DrawText(hdcMem, wT, 1, &r, DT_TOP | DT_LEFT | DT_CALCRECT);
		::DrawText(hdcMem, wT, 1, &r, DT_TOP | DT_LEFT);
		float scale = (nFontSize - (rand() % 10)) / (float)nFontSize;
		float angle = (35 - (rand() % 71)) * (float)M_PI / 180;
		float co = cosf(angle);
		float si = sinf(angle);
		LONG x2 = (r.right - r.left) / 2;
		LONG y2 = (r.bottom - r.top) / 2;
		RECT r2{ r.left - x2, r.top - y2, r.right - x2, r.bottom - y2 };
		POINT ap[3];
		x2 += xOff;
		y2 += rand() & 8;
		ap[0].x = (LONG)(x2 + scale * (r2.left * co - r2.top * si));
		ap[0].y = (LONG)(y2 + scale * (r2.left * si + r2.top * co));
		ap[1].x = (LONG)(x2 + scale * (r2.right * co - r2.top * si));
		ap[1].y = (LONG)(y2 + scale * (r2.right * si + r2.top * co));
		ap[2].x = (LONG)(x2 + scale * (r2.left * co - r2.bottom * si));
		ap[2].y = (LONG)(y2 + scale * (r2.left * si + r2.bottom * co));
		::PlgBlt(hdc, ap, hdcMem, 0, 0, r.right - r.left, r.bottom - r.top, NULL, 0, 0);
		xOff += CROWDEDSIZE;
	}
	for (int j = nWidth * nHeight / 4; --j > 0;)
		::SetPixel(hdc, rand() % nWidth, rand() % nHeight, RGB(0, 0, 0));

	::SelectObject(hdcMem, hFontOld);
	::DeleteObject(hFont);
	::SelectObject(hdcMem, hBitMemOld);
	::DeleteObject(hBitMem);
	::DeleteDC(hdcMem);
	::SelectObject(hdc, hBitmapOld);
	::DeleteDC(hdc);
#if TEST_FRAMEGRABBER // Reusing macro from FrameGrabThread.
	CImage captcha;
	captcha.Attach(m_hbmpCaptcha);
	captcha.Save(_T("\\tmp\\CaptchaTest.bmp"), Gdiplus::ImageFormatBMP);
	captcha.Detach();
#endif
}

void CCaptchaGenerator::Clear()
{
	if (m_hbmpCaptcha) {
		::DeleteObject(m_hbmpCaptcha);
		m_hbmpCaptcha = 0;
	}
	m_strCaptchaText.Empty();
}

bool CCaptchaGenerator::WriteCaptchaImage(CFileDataIO &file) const
{
	if (m_hbmpCaptcha) {
		size_t size;
		byte *buf = bmp2mem(m_hbmpCaptcha, size, Gdiplus::ImageFormatBMP);
		if (buf) {
			file.Write(buf, (UINT)size);
			delete[] buf;
			return true;
		}
	}
	return false;
}
