//This file is part of eMule AI
//Copyright (C)2003-2026 Merkur ( devs@emule-project.net / https://www.emule-project.net )
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
#include "emuledlg.h"
#include "FrameGrabThread.h"
#include "OtherFunctions.h"
#include "quantize.h"
#ifndef HAVE_QEDIT_H
// This is a remote feature and not optional, in order to keep to working properly for other clients who want to use it
// Check emule_site_config.h to fix it
#error Missing 'qedit.h', look at "emule_site_config.h" for further information.
#endif

// DirectShow MediaDet
#define MMNOSOUND		// mmsystem: Sound support
#define MMNOMIDI		// mmsystem: MIDI support
#define MMNOAUX			// mmsystem: Auxiliary audio support
#define MMNOMIXER		// mmsystem: Mixer support
#define MMNOTIMER		// mmsystem: Timer support
#define MMNOJOY			// mmsystem: Joystick support
#define MMNOMCI			// mmsystem: MCI support
#define MMNOMMSYSTEM	// mmsystem: General MMSYSTEM functions
// NOTE: If you get a compile error due to missing 'qedit.h', look at "emule_site_config.h" for further information.
#include <qedit.h>
#include <dshow.h>

#if TEST_FRAMEGRABBER
#include <atlimage.h>
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


IMPLEMENT_DYNCREATE(CFrameGrabThread, CWinThread)

BEGIN_MESSAGE_MAP(CFrameGrabThread, CWinThread)
END_MESSAGE_MAP()

CFrameGrabThread::CFrameGrabThread()
	: imgResults()
	, pOwner()
	, pSender()
	, dStartTime()
	, nMaxWidth()
	, nFramesToGrab()
	, bReduceColor()
{
}

BOOL CFrameGrabThread::InitInstance()
{
	DbgSetThreadName("FrameGrabThread");
	return TRUE;
}

BOOL CFrameGrabThread::Run()
{
	imgResults = new HBITMAP[nFramesToGrab]{};
	FrameGrabResult_Struct *result = new FrameGrabResult_Struct;
	(void)::CoInitialize(NULL);
	result->nImagesGrabbed = (uint8)GrabFrames();
	::CoUninitialize();
	result->imgResults = imgResults;
	result->pSender = pSender;
	if (!theApp.emuledlg->PostMessage(TM_FRAMEGRABFINISHED, (WPARAM)pOwner, (LPARAM)result)) {
		for (int i = (int)result->nImagesGrabbed; --i >= 0;)
			if (result->imgResults[i])
				::DeleteObject(result->imgResults[i]);
		delete[] result->imgResults;
		delete result;
	}
	return 0;
}

UINT CFrameGrabThread::GrabFrames()
{
#define TIMEBETWEENFRAMES	50.0 // could be a param later, if needed
	uint32 nFramesGrabbed = 0;
	char *buffer = NULL;
	HDC hdc = 0;
	try {
		CComPtr<IMediaDet> pDet;
		HRESULT hr = pDet.CoCreateInstance(__uuidof(MediaDet));
		if (!SUCCEEDED(hr))
			return 0;

		// Convert the file name to a BSTR.
		CComBSTR bstrFilename(strFileName);
		pDet->put_Filename(bstrFilename);

		bool bFound = false;
		long lStreams;
		pDet->get_OutputStreams(&lStreams);
		for (long i = 0; i < lStreams; ++i) {
			GUID major_type;
			pDet->put_CurrentStream(i);
			pDet->get_StreamType(&major_type);
			if (major_type == MEDIATYPE_Video) {
				bFound = true;
				break;
			}
		}

		if (!bFound)
			return 0;

		double dLength;
		hr = pDet->get_StreamLength(&dLength);
		if (!SUCCEEDED(hr) || dStartTime > dLength)
			dStartTime = 0;

		AM_MEDIA_TYPE mt;
		hr = pDet->get_StreamMediaType(&mt);
		if (!SUCCEEDED(hr) || mt.formattype != FORMAT_VideoInfo)
			return 0; // Should not happen, in theory.

		VIDEOINFOHEADER *pVih = (VIDEOINFOHEADER*)(mt.pbFormat);
		long width = pVih->bmiHeader.biWidth;
		long height = pVih->bmiHeader.biHeight;

		// We want absolute height, don't care about orientation.
		if (height < 0)
			height = -height;

		if (mt.cbFormat != 0) {
			::CoTaskMemFree((PVOID)mt.pbFormat);
			mt.cbFormat = 0;
			mt.pbFormat = NULL;
		}
		if (mt.pUnk != NULL) {
			mt.pUnk->Release();
			mt.pUnk = NULL;
		}

		hdc = ::CreateCompatibleDC(NULL);
		size_t nBufSize = 0;
		byte bmi[sizeof(BITMAPINFOHEADER) + sizeof(RGBQUAD) * 256];
		for (; nFramesGrabbed < nFramesToGrab; ++nFramesGrabbed) {
			long size;
			hr = pDet->GetBitmapBits(dStartTime, &size, NULL, width, height);
			if (SUCCEEDED(hr)) {
				size_t nFrameBitmapLen = sizeof(BITMAPINFOHEADER) + size;
				if (nBufSize < nFrameBitmapLen) {
					delete[] buffer;
					buffer = NULL;
					nBufSize = 0;
				}
				if (!nBufSize) {
					buffer = new char[nFrameBitmapLen];
					nBufSize = nFrameBitmapLen;
				}
				hr = pDet->GetBitmapBits(dStartTime, NULL, buffer, width, height);
				if (FAILED(hr)) {
					ASSERT(0);
					break;
				}

				BITMAPINFOHEADER &bmih = *(BITMAPINFOHEADER*)bmi;
				bmih = *(BITMAPINFOHEADER*)buffer;
				if (bReduceColor) {
					CQuantizer q(256, 8);
					q.ProcessImage(buffer);
					q.SetColorTable(((BITMAPINFO*)bmi)->bmiColors);
					bmih.biClrUsed = 256;
					bmih.biBitCount = 8;
				}
				if (nMaxWidth > 0 && nMaxWidth < (uint32)width) {
					bmih.biWidth = (LONG)nMaxWidth;
					bmih.biHeight = (LONG)(bmih.biHeight * nMaxWidth / width);
				}
				bmih.biSizeImage = 0;

				byte *pBits;
				HBITMAP hFrame = ::CreateDIBSection(NULL, (BITMAPINFO*)bmi, DIB_RGB_COLORS, (void**)&pBits, NULL, 0);
				HBITMAP hBitmapOld = (HBITMAP)::SelectObject(hdc, hFrame);

				// Resize and reduce colours in one pass without error diffusion.
				int step0 = ((BITMAPINFOHEADER*)buffer)->biBitCount / 8;
				int stride0 = ((((width * ((BITMAPINFOHEADER*)buffer)->biBitCount) + 31) & ~31) >> 3);
				int stride1 = ((((bmih.biWidth * bmih.biBitCount) + 31) & ~31) >> 3);
				byte *src = (byte*)&buffer[sizeof(BITMAPINFOHEADER)];
				BGR last;
				int idx = -1;
				for (int i = 0; i < bmih.biHeight; ++i) {
					byte *d = pBits;
					byte *q = &src[i * height / bmih.biHeight * stride0];
					for (int j = 0; j < bmih.biWidth; ++j) {
						int k = j * width / bmih.biWidth * step0;
						if (bReduceColor) {
							if (idx < 0 || memcmp(&last, &q[k], sizeof(BGR))) {
								last = *(BGR*)&q[k];
								idx = bestclr(((BITMAPINFO*)bmi)->bmiColors, last);
							}
							ASSERT(idx >= 0 && (DWORD)idx < bmih.biClrUsed);
							*d++ = (byte)idx;
						} else {
							*(BGR*)d = *(BGR*)&q[k];
							d += sizeof(BGR);
						}
					}
					pBits += stride1;
				}

				::SelectObject(hdc, hBitmapOld);
				imgResults[nFramesGrabbed] = hFrame;
#if TEST_FRAMEGRABBER //see also "case MP_OPEN" in CSharedFilesCtrl::OnContextMenu
				CString TestName;
				TestName.Format(_T("\\tmp\\testframe%i.bmp"), nFramesGrabbed);
				CImage img;
				img.Attach(hFrame, CImage::DIBOR_DEFAULT);
				img.Save(TestName, Gdiplus::ImageFormatBMP);
				img.Detach();
#endif
			}
			dStartTime += TIMEBETWEENFRAMES;
		}
	} catch (...) {
		ASSERT(0);
	}
	::DeleteDC(hdc);
	delete[] buffer;
	return nFramesGrabbed;
}

void CFrameGrabThread::SetValues(const CKnownFile *in_pOwner, const CString &in_strFileName, uint8 in_nFramesToGrab, double in_dStartTime, bool in_bReduceColor, uint16 in_nMaxWidth, void *in_pSender)
{
	strFileName = in_strFileName;
	nFramesToGrab = in_nFramesToGrab;
	dStartTime = in_dStartTime;
	bReduceColor = in_bReduceColor;
	nMaxWidth = in_nMaxWidth;
	pOwner = in_pOwner;
	pSender = in_pSender;
}
