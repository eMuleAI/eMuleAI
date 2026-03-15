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
#include "stdafx.h"
#include "SafeFile.h"
#include "Packets.h"
#include "StringConversion.h"
#include "kademlia/utils/UInt128.h"
#include "OtherFunctions.h"
#include <io.h> // For _open_osfhandle/_close
#include <fcntl.h> // For _O_RDONLY/_O_BINARY

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


///////////////////////////////////////////////////////////////////////////////
// CFileDataIO

uint8 CFileDataIO::ReadUInt8()
{
	uint8 nVal;
	Read(&nVal, sizeof nVal);
	return nVal;
}

uint16 CFileDataIO::ReadUInt16()
{
	uint16 nVal;
	Read(&nVal, sizeof nVal);
	return nVal;
}

uint32 CFileDataIO::ReadUInt32()
{
	uint32 nVal;
	Read(&nVal, sizeof nVal);
	return nVal;
}

uint64 CFileDataIO::ReadUInt64()
{
	uint64 nVal;
	Read(&nVal, sizeof nVal);
	return nVal;
}

void CFileDataIO::ReadUInt128(Kademlia::CUInt128 &Val)
{
	Read(Val.GetDataPtr(), 16);
}

void CFileDataIO::ReadHash16(uchar *pVal)
{
	Read(pVal, 16);
}

CString CFileDataIO::ReadString(bool bOptUTF8, UINT uRawSize)
{
	const UINT uMaxShortRawSize = SHORT_RAW_ED2K_UTF8_STR;
	if (uRawSize <= uMaxShortRawSize) {
		char acRaw[uMaxShortRawSize];
		Read(acRaw, uRawSize);
		if (uRawSize >= 3 && acRaw[0] == (char)0xEFU && acRaw[1] == (char)0xBBU && acRaw[2] == (char)0xBFU) {
			WCHAR awc[uMaxShortRawSize];
			int iChars = ByteStreamToWideChar(acRaw + 3, uRawSize - 3, awc, _countof(awc));
			if (iChars >= 0)
				return CStringW(awc, iChars);
		}
		if (bOptUTF8) {
			WCHAR awc[uMaxShortRawSize];
			int iChars = utf8towc(acRaw, uRawSize, awc, _countof(awc));
			if (iChars >= 0)
				return CStringW(awc, iChars);
		}
		return CStringW(acRaw, uRawSize); // use local codepage
	}

	Array<char> acRaw(uRawSize);
	Read(acRaw, uRawSize);
	if (uRawSize >= 3 && acRaw[0] == (char)0xEFU && acRaw[1] == (char)0xBBU && acRaw[2] == (char)0xBFU) {
		Array<WCHAR> awc(uRawSize);
		int iChars = ByteStreamToWideChar(acRaw + 3, uRawSize - 3, awc, uRawSize);
		if (iChars >= 0)
			return CStringW(awc, iChars);
	}
	if (bOptUTF8) {
		Array<WCHAR> awc(uRawSize);
		int iChars = utf8towc(acRaw, uRawSize, awc, uRawSize);
		if (iChars >= 0)
			return CStringW(awc, iChars);
	}
	return CStringW(acRaw, uRawSize); // use local codepage
}

CString CFileDataIO::ReadString(bool bOptUTF8)
{
	UINT uLen = ReadUInt16();
	return ReadString(bOptUTF8, uLen);
}

CStringW CFileDataIO::ReadStringUTF8()
{
	UINT uRawSize = ReadUInt16();
	const UINT uMaxShortRawSize = SHORT_RAW_ED2K_UTF8_STR;
	if (uRawSize <= uMaxShortRawSize) {
		char acRaw[uMaxShortRawSize];
		Read(acRaw, uRawSize);
		WCHAR awc[uMaxShortRawSize];
		int iChars = ByteStreamToWideChar(acRaw, uRawSize, awc, _countof(awc));
		if (iChars >= 0)
			return CStringW(awc, iChars);
		return CStringW(acRaw, uRawSize); // use local codepage
	}

	Array<char> acRaw(uRawSize);
	Read(acRaw, uRawSize);
	Array<WCHAR> awc(uRawSize);
	int iChars = ByteStreamToWideChar(acRaw, uRawSize, awc, uRawSize);
	if (iChars >= 0)
		return CStringW(awc, iChars);
	return CStringW(acRaw, uRawSize); // use local codepage;
}

void CFileDataIO::WriteUInt8(uint8 nVal)
{
	Write(&nVal, sizeof nVal);
}

void CFileDataIO::WriteUInt16(uint16 nVal)
{
	Write(&nVal, sizeof nVal);
}

void CFileDataIO::WriteUInt32(uint32 nVal)
{
	Write(&nVal, sizeof nVal);
}

void CFileDataIO::WriteUInt64(uint64 nVal)
{
	Write(&nVal, sizeof nVal);
}

void CFileDataIO::WriteUInt128(const Kademlia::CUInt128 &Val)
{
	Write(Val.GetData(), 16);
}

void CFileDataIO::WriteHash16(const uchar *pVal)
{
	Write(pVal, 16);
}

void CFileDataIO::WriteString(const CString &rstr, EUTF8str eEncode)
{
#define	WRITE_STR_LEN(n)	WriteUInt16((uint16)(n))
	if (eEncode == UTF8strRaw) {
		CUnicodeToUTF8 utf8(rstr);
		WRITE_STR_LEN(utf8.GetLength());
		Write((LPCSTR)utf8, utf8.GetLength());
	} else if (eEncode == UTF8strOptBOM) {
		if (NeedUTF8String(rstr)) {
			CUnicodeToBOMUTF8 bomutf8(rstr);
			WRITE_STR_LEN(bomutf8.GetLength());
			Write((LPCSTR)bomutf8, bomutf8.GetLength());
		} else {
			CUnicodeToMultiByte mb(rstr);
			WRITE_STR_LEN(mb.GetLength());
			Write((LPCSTR)mb, mb.GetLength());
		}
	} else {
		CUnicodeToMultiByte mb(rstr);
		WRITE_STR_LEN(mb.GetLength());
		Write((LPCSTR)mb, mb.GetLength());
	}
#undef WRITE_STR_LEN
}

void CFileDataIO::WriteString(LPCSTR const psz)
{
	size_t uLen = strlen(psz);
	WriteUInt16((uint16)uLen);
	Write(psz, (UINT)uLen);
}

void CFileDataIO::WriteLongString(const CString &rstr, EUTF8str eEncode)
{
#define	WRITE_STR_LEN(n)	WriteUInt32(n)
	if (eEncode == UTF8strRaw) {
		CUnicodeToUTF8 utf8(rstr);
		WRITE_STR_LEN(utf8.GetLength());
		Write((LPCSTR)utf8, utf8.GetLength());
	} else if (eEncode == UTF8strOptBOM) {
		if (NeedUTF8String(rstr)) {
			CUnicodeToBOMUTF8 bomutf8(rstr);
			WRITE_STR_LEN(bomutf8.GetLength());
			Write((LPCSTR)bomutf8, bomutf8.GetLength());
		} else {
			CUnicodeToMultiByte mb(rstr);
			WRITE_STR_LEN(mb.GetLength());
			Write((LPCSTR)mb, mb.GetLength());
		}
	} else {
		CUnicodeToMultiByte mb(rstr);
		UINT uLen = (UINT)mb.GetLength();
		WRITE_STR_LEN(uLen);
		Write((LPCSTR)mb, uLen);
	}
#undef WRITE_STR_LEN
}

void CFileDataIO::WriteLongString(LPCSTR const psz)
{
	UINT uLen = (UINT)strlen(psz);
	WriteUInt32(uLen);
	Write(psz, uLen);
}

///////////////////////////////////////////////////////////////////////////////
// CSafeFile

UINT CSafeFile::Read(void *lpBuf, UINT nCount)
{
	if (GetPosition() + nCount > GetLength())
		AfxThrowFileException(CFileException::endOfFile, 0, GetFileName());
	return CFile::Read(lpBuf, nCount);
}

void CSafeFile::Write(const void *lpBuf, UINT nCount)
{
	CFile::Write(lpBuf, nCount);
}

ULONGLONG CSafeFile::Seek(LONGLONG lOff, UINT nFrom)
{
	return CFile::Seek(lOff, nFrom);
}

ULONGLONG CSafeFile::GetPosition() const
{
	return CFile::GetPosition();
}

ULONGLONG CSafeFile::GetLength() const
{
	return CFile::GetLength();
}


///////////////////////////////////////////////////////////////////////////////
// CSafeMemFile

UINT CSafeMemFile::Read(void *lpBuf, UINT nCount)
{
	if (m_nPosition + nCount > m_nFileSize)
		AfxThrowFileException(CFileException::endOfFile, 0, GetFileName());
	return CMemFile::Read(lpBuf, nCount);
}

void CSafeMemFile::Write(const void *lpBuf, UINT nCount)
{
	CMemFile::Write(lpBuf, nCount);
}

ULONGLONG CSafeMemFile::Seek(LONGLONG lOff, UINT nFrom)
{
	return CMemFile::Seek(lOff, nFrom);
}

uint8 CSafeMemFile::ReadUInt8()
{
	if (m_nPosition + sizeof(uint8) > m_nFileSize)
		AfxThrowFileException(CFileException::endOfFile, 0, GetFileName());
	return m_lpBuffer[m_nPosition++];
}

uint16 CSafeMemFile::ReadUInt16()
{
	if (m_nPosition + sizeof(uint16) > m_nFileSize)
		AfxThrowFileException(CFileException::endOfFile, 0, GetFileName());
	uint16 nResult = *(uint16*)&m_lpBuffer[m_nPosition];
	m_nPosition += sizeof(uint16);
	return nResult;
}

uint32 CSafeMemFile::ReadUInt32()
{
	if (m_nPosition + sizeof(uint32) > m_nFileSize)
		AfxThrowFileException(CFileException::endOfFile, 0, GetFileName());
	uint32 nResult = *(uint32*)&m_lpBuffer[m_nPosition];
	m_nPosition += sizeof(uint32);
	return nResult;
}

uint64 CSafeMemFile::ReadUInt64()
{
	if (m_nPosition + sizeof(uint64) > m_nFileSize)
		AfxThrowFileException(CFileException::endOfFile, 0, GetFileName());
	uint64 nResult = *(uint64*)&m_lpBuffer[m_nPosition];
	m_nPosition += sizeof(uint64);
	return nResult;
}

void CSafeMemFile::ReadUInt128(Kademlia::CUInt128 &Val)
{
	ReadHash16(Val.GetDataPtr());
}

void CSafeMemFile::ReadHash16(uchar *pVal)
{
	if (m_nPosition + MDX_DIGEST_SIZE > m_nFileSize)
		AfxThrowFileException(CFileException::endOfFile, 0, GetFileName());
	md4cpy(pVal, &m_lpBuffer[m_nPosition]);
	m_nPosition += MDX_DIGEST_SIZE;
}

void CSafeMemFile::WriteUInt8(uint8 nVal)
{
	if (m_nPosition + sizeof(uint8) > m_nBufferSize)
		GrowFile(m_nPosition + sizeof(uint8));
	m_lpBuffer[m_nPosition++] = nVal;
	if (m_nPosition > m_nFileSize)
		m_nFileSize = m_nPosition;
}

void CSafeMemFile::WriteUInt16(uint16 nVal)
{
	if (m_nPosition + sizeof(uint16) > m_nBufferSize)
		GrowFile(m_nPosition + sizeof(uint16));
	*(uint16*)&m_lpBuffer[m_nPosition] = nVal;
	m_nPosition += sizeof(uint16);
	if (m_nPosition > m_nFileSize)
		m_nFileSize = m_nPosition;
}

void CSafeMemFile::WriteUInt32(uint32 nVal)
{
	if (m_nPosition + sizeof(uint32) > m_nBufferSize)
		GrowFile(m_nPosition + sizeof(uint32));
	*(uint32*)&m_lpBuffer[m_nPosition] = nVal;
	m_nPosition += sizeof(uint32);
	if (m_nPosition > m_nFileSize)
		m_nFileSize = m_nPosition;
}

void CSafeMemFile::WriteUInt64(uint64 nVal)
{
	if (m_nPosition + sizeof(uint64) > m_nBufferSize)
		GrowFile(m_nPosition + sizeof(uint64));
	*(uint64*)&m_lpBuffer[m_nPosition] = nVal;
	m_nPosition += sizeof(uint64);
	if (m_nPosition > m_nFileSize)
		m_nFileSize = m_nPosition;
}

void CSafeMemFile::WriteUInt128(const Kademlia::CUInt128 &Val)
{
	WriteHash16(Val.GetData());
}

void CSafeMemFile::WriteHash16(const uchar *pVal)
{
	if (m_nPosition + MDX_DIGEST_SIZE > m_nBufferSize)
		GrowFile(m_nPosition + MDX_DIGEST_SIZE);
	md4cpy(&m_lpBuffer[m_nPosition], pVal);
	m_nPosition += MDX_DIGEST_SIZE;
	if (m_nPosition > m_nFileSize)
		m_nFileSize = m_nPosition;
}

ULONGLONG CSafeMemFile::GetPosition() const
{
	return CMemFile::GetPosition();
}

ULONGLONG CSafeMemFile::GetLength() const
{
	return CMemFile::GetLength();
}


///////////////////////////////////////////////////////////////////////////////
// CSafeBufferedFile
bool CSafeBufferedFile::OpenLongPath(LPCTSTR lpszFileName, UINT nOpenFlags)
{
	// Only read mode is supported for this helper.
	ASSERT((nOpenFlags & (CFile::modeRead | CFile::modeWrite | CFile::modeReadWrite)) == CFile::modeRead);
	CString prepared = PreparePathForWin32LongPath(lpszFileName);
	
	DWORD flags = FILE_ATTRIBUTE_NORMAL;
	if ((nOpenFlags & CFile::osSequentialScan) != 0)
		flags |= FILE_FLAG_SEQUENTIAL_SCAN;

	HANDLE h = ::CreateFile(prepared, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, flags, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return false;

	int fd = _open_osfhandle((intptr_t)h, _O_RDONLY | _O_BINARY);
	if (fd == -1) {
		::CloseHandle(h);
		return false;
	}

	FILE* fp = _fdopen(fd, "rb");
	if (fp == NULL) {
		_close(fd);
		return false;
	}

	// Attach to CStdioFile/CFile internals
	m_pStream = fp;
	// Normalize m_hFile assignment for different MFC types (HANDLE vs. UINT_PTR). Obtain the OS handle back from the CRT file descriptor to keep both in sync.
	HANDLE osHandleFromFd = (HANDLE)_get_osfhandle(_fileno(fp));
	m_hFile = (decltype(m_hFile))osHandleFromFd;
	m_bCloseOnDelete = TRUE;
	return true;
}


UINT CSafeBufferedFile::Read(void *lpBuf, UINT nCount)
{
	// that's terrible slow
	UINT uRead = CStdioFile::Read(lpBuf, nCount);
	if (uRead != nCount)
		AfxThrowFileException(CFileException::endOfFile, 0, GetFileName());
	return uRead;
}

void CSafeBufferedFile::Write(const void *lpBuf, UINT nCount)
{
	CStdioFile::Write(lpBuf, nCount);
}

ULONGLONG CSafeBufferedFile::Seek(LONGLONG lOff, UINT nFrom)
{
	return CStdioFile::Seek(lOff, nFrom);
}

ULONGLONG CSafeBufferedFile::GetPosition() const
{
	return CStdioFile::GetPosition();
}

ULONGLONG CSafeBufferedFile::GetLength() const
{
	return CStdioFile::GetLength();
}

int CSafeBufferedFile::printf(LPCTSTR pszFmt, ...)
{
	va_list args;
	va_start(args, pszFmt);
	int iResult = _vftprintf(m_pStream, pszFmt, args);
	va_end(args);
	if (iResult < 0)
		AfxThrowFileException(CFileException::genericException, _doserrno, m_strFileName);
	return iResult;
}