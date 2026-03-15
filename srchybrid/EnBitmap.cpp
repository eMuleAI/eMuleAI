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
#include "EnBitmap.h"
#include <atlimage.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


const int HIMETRIC_INCH = 2540;

static IStream* CreateStreamFromBuffer(BYTE* pBuff, int nSize)
{
	IStream* pStream = NULL;
	HGLOBAL hGlobal = ::GlobalAlloc(GMEM_MOVEABLE, nSize);
	if (hGlobal == NULL)
		return NULL;

	void* pData = ::GlobalLock(hGlobal);
	if (pData == NULL) {
		::GlobalFree(hGlobal);
		return NULL;
	}

	memcpy(pData, pBuff, nSize);
	::GlobalUnlock(hGlobal);

	if (CreateStreamOnHGlobal(hGlobal, TRUE, &pStream) != S_OK) {
		::GlobalFree(hGlobal);
		return NULL;
	}

	return pStream;
}

static BOOL AttachImage(CBitmap& bitmap, CImage& image, COLORREF crBack)
{
	ASSERT(bitmap.m_hObject == NULL);

	if (bitmap.m_hObject != NULL)
		return FALSE;

	const int nWidth = image.GetWidth();
	const int nHeight = image.GetHeight();
	if (nWidth <= 0 || nHeight <= 0)
		return FALSE;

	CDC* pDC = CWnd::GetDesktopWindow()->GetDC();
	if (pDC == NULL)
		return FALSE;

	BOOL bResult = FALSE;
	CDC dcMem;
	if (dcMem.CreateCompatibleDC(pDC)) {
		CBitmap bmMem;
		if (bmMem.CreateCompatibleBitmap(pDC, nWidth, nHeight)) {
			CBitmap* pOldBM = dcMem.SelectObject(&bmMem);

			if (crBack != CLR_NONE)
				dcMem.FillSolidRect(0, 0, nWidth, nHeight, crBack);

			if (image.Draw(dcMem, 0, 0, nWidth, nHeight))
				bResult = bitmap.Attach(bmMem.Detach());

			dcMem.SelectObject(pOldBM);
		}
	}

	CWnd::GetDesktopWindow()->ReleaseDC(pDC);
	return bResult;
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

BOOL CEnBitmap::LoadImage(UINT uIDRes, LPCTSTR pszResourceType, HMODULE hInst, COLORREF crBack)
{
	return LoadImage(MAKEINTRESOURCE(uIDRes), pszResourceType, hInst, crBack);
}

BOOL CEnBitmap::LoadImage(LPCTSTR lpszResourceName, LPCTSTR szResourceType, HMODULE hInst, COLORREF crBack)
{
	if (m_hObject != NULL) { // only attach once, detach on destroy
		ASSERT(0);
		return FALSE;
	}

	BOOL bResult = FALSE;

	// first call is to get buffer size
	int nSize;
	if (GetResource(lpszResourceName, szResourceType, hInst, 0, nSize)) {
		if (nSize > 0) {
			BYTE *pBuff = new BYTE[nSize];
			// this loads it
			if (GetResource(lpszResourceName, szResourceType, hInst, pBuff, nSize)) {
				IStream* pStream = CreateStreamFromBuffer(pBuff, nSize);
				if (pStream != NULL) {
					CImage image;
					if (SUCCEEDED(image.Load(pStream)))
						bResult = AttachImage(*this, image, crBack);
					pStream->Release();
				}

				if (!bResult) {
					IPicture *pPicture = LoadFromBuffer(pBuff, nSize);
					if (pPicture) {
						bResult = Attach(pPicture, crBack);
						pPicture->Release();
					}
				}
			}

			delete[] pBuff;
		}
	}

	return bResult;
}

BOOL CEnBitmap::LoadImage(LPCTSTR szImagePath, COLORREF crBack)
{
	if (m_hObject != NULL) { // only attach once, detach on destroy
		ASSERT(0);
		return FALSE;
	}

	CImage img;
	if (SUCCEEDED(img.Load(szImagePath)))
		return CBitmap::Attach(img.Detach());

	BOOL bResult = FALSE;
	CFileException ex;
	CFile cFile;
	if (cFile.Open(szImagePath, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, &ex)) {
		int nSize = (int)cFile.GetLength();
		BYTE *pBuff = new BYTE[nSize];
		if (cFile.Read(pBuff, nSize) > 0) {
			IPicture *pPicture = LoadFromBuffer(pBuff, nSize);
			if (pPicture) {
				bResult = Attach(pPicture, crBack);
				pPicture->Release();
			}
		}
		delete[] pBuff;
	}
	return bResult;
}

IPicture* CEnBitmap::LoadFromBuffer(BYTE *pBuff, int nSize)
{
	IPicture *pPicture = NULL;

	IStream* pStream = CreateStreamFromBuffer(pBuff, nSize);
	if (pStream != NULL) {
		// Not sure what the 'KeepOriginalFormat' property is really used for. But if 'OleLoadPicture'
		// is invoked with 'fRunmode=FALSE' the function always creates a temporary file which even
		// does not get deleted when all COM pointers were released. It eventually gets deleted only
		// seem to have any other side effects.
		VERIFY(OleLoadPicture(pStream, nSize, TRUE/*FALSE*/, IID_IPicture, (LPVOID*)&pPicture) == S_OK);
		pStream->Release();
	}

	return pPicture; // caller releases
}

BOOL CEnBitmap::GetResource(LPCTSTR lpName, LPCTSTR lpType, HMODULE hInst, void *pResource, int &nBufSize)
{
	// Find the resource
	HRSRC hResInfo = FindResource(hInst, lpName, lpType);
	if (hResInfo == NULL)
		return FALSE;

	// Load the resource
	HANDLE hRes = LoadResource(hInst, hResInfo);
	if (hRes == NULL)
		return FALSE;

	bool bResult = FALSE;

	// Lock the resource
	LPCSTR lpRes = (LPCSTR)LockResource(hRes);
	if (lpRes != NULL) {
		if (pResource == NULL) {
			nBufSize = SizeofResource(hInst, hResInfo);
			bResult = TRUE;
		} else if (nBufSize >= (int)SizeofResource(hInst, hResInfo)) {
			memcpy(pResource, lpRes, nBufSize);
			bResult = TRUE;
		}

		UnlockResource(hRes);
	}

	// Free the resource
	FreeResource(hRes);

	return bResult;
}

BOOL CEnBitmap::Attach(IPicture *pPicture, COLORREF crBack)
{
	ASSERT(m_hObject == NULL);      // only attach once, detach on destroy

	if (m_hObject != NULL)
		return FALSE;

	ASSERT(pPicture);

	if (!pPicture)
		return FALSE;

	BOOL bResult = FALSE;

	CDC *pDC = CWnd::GetDesktopWindow()->GetDC();
	CDC dcMem;
	if (dcMem.CreateCompatibleDC(pDC)) {
		long hmWidth;
		long hmHeight;
		pPicture->get_Width(&hmWidth);
		pPicture->get_Height(&hmHeight);

		int nWidth = ::MulDiv(hmWidth, pDC->GetDeviceCaps(LOGPIXELSX), HIMETRIC_INCH);
		int nHeight = ::MulDiv(hmHeight, pDC->GetDeviceCaps(LOGPIXELSY), HIMETRIC_INCH);

		CBitmap bmMem;
		if (bmMem.CreateCompatibleBitmap(pDC, nWidth, nHeight)) {
			CBitmap *pOldBM = dcMem.SelectObject(&bmMem);

			if (crBack != CLR_NONE)
				dcMem.FillSolidRect(0, 0, nWidth, nHeight, crBack);

			HRESULT hr = pPicture->Render(dcMem, 0, 0, nWidth, nHeight, 0, hmHeight, hmWidth, -hmHeight, NULL);
			dcMem.SelectObject(pOldBM);

			if (hr == S_OK)
				bResult = CBitmap::Attach(bmMem.Detach());
		}
	}

	CWnd::GetDesktopWindow()->ReleaseDC(pDC);

	return bResult;
}
