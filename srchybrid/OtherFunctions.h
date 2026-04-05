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
#pragma once
#include <utility>
#include <list>
#include "eMuleAI/Address.h"
#include "ComboBoxEx2.h"
#include "ResizableLib/ResizableLayout.h"
#include <chrono>
#include <execution>
#include "eMuleAI/TimSort.hpp"

class CAbstractFile;
class CKnownFile;
struct Requested_Block_Struct;
class CUpDownClient;
class CAICHHash;
class CPartFile;
class CSafeMemFile;
class CShareableFile;

enum EFileType : uint8
{
	FILETYPE_UNKNOWN,
	FILETYPE_EXECUTABLE,
	ARCHIVE_ZIP,
	ARCHIVE_RAR,
	ARCHIVE_ACE,
	ARCHIVE_7Z,
	IMAGE_ISO,
	AUDIO_MPEG,
	VIDEO_AVI,
	VIDEO_MPG,
	VIDEO_MP4,
	VIDEO_MKV,
	VIDEO_OGG,
	WM,
	PIC_JPG,
	PIC_PNG,
	PIC_GIF,
	DOCUMENT_PDF
};

extern LPCTSTR const sBadFileNameChar;
extern LPCTSTR const sHiddenPassword;

struct LinkScheme
{
	LPCTSTR pszScheme;
	int iLen;
};
extern const struct LinkScheme s_apszSchemes[];

struct SUnresolvedHostname
{
	SUnresolvedHostname()
		: nPort()
	{
	}
	CStringA strHostname;
	uint16 nPort;
};

struct ImportPart_Struct
{
	uint64	start;
	uint64	end;
	BYTE* data;
};

#define ROUND(x) (floor((float)(x)+0.5f))

template <typename T> inline static T maxi(const T &v0, const T &v1)
{
	return v0 >= v1 ? v0 : v1;
}

template <typename T> inline static T mini(const T &v0, const T &v1)
{
	return v0 <= v1 ? v0 : v1;
}

template <typename T> inline static int sgn(const T &val) //for signed types only!
{
	return (T(0) < val) - (val < T(0));
}

#define ARRSIZE(x)	(sizeof(x)/sizeof(x[0]))

///////////////////////////////////////////////////////////////////////////////
// Low level str
//
inline char* nstrdup(const char *todup)
{
	if (todup == nullptr)
		return nullptr;
	size_t len = strlen(todup) + 1;
	char *ret = new char[len];
	memcpy(ret, todup, len);
	return ret;
}

#define stristr StrStrI //at least since shlwapi.dll version 4.71
//Unlike CString::Tokenize, GetNextString does not skip all delimiters but returns empty string
CString GetNextString(const CString &rstr, LPCTSTR pszTokens, int &riStart);
bool CanSafeDrawImageList(CImageList* pImageList, CDC* pDC, int nImage);
BOOL SafeImageListDraw(CImageList* pImageList, CDC* pDC, int nImage, POINT pt, UINT nStyle);
BOOL SafeImageListDraw(CImageList* pImageList, CDC* pDC, int nImage, CPoint pt, UINT nStyle);
BOOL SafeImageListDrawEx(CImageList* pImageList, CDC* pDC, int nImage, POINT pt, SIZE sz, COLORREF clrBk, COLORREF clrFg, UINT nStyle);
BOOL SafeImageListDrawEx(CImageList* pImageList, CDC* pDC, int nImage, CPoint pt, SIZE sz, COLORREF clrBk, COLORREF clrFg, UINT nStyle);
CString GetNextString(const CString &rstr, TCHAR chToken, int &riStart);


///////////////////////////////////////////////////////////////////////////////
// String conversion
//
CString CastItoXBytes(uint16 count, bool isK = false, bool isPerSec = false, uint32 decimal = 2);
CString CastItoXBytes(uint32 count, bool isK = false, bool isPerSec = false, uint32 decimal = 2);
CString CastItoXBytes(uint64 count, bool isK = false, bool isPerSec = false, uint32 decimal = 2);
CString CastItoXBytes(float count, bool isK = false, bool isPerSec = false, uint32 decimal = 2);
CString CastItoXBytes(double count, bool isK = false, bool isPerSec = false, uint32 decimal = 2);
#if defined(_DEBUG) && defined(USE_DEBUG_EMFILESIZE)
CString CastItoXBytes(EMFileSize count, bool isK = false, bool isPerSec = false, uint32 decimal = 2);
#endif
CString CastItoIShort(uint16 count, bool isK = false, uint32 decimal = 2);
CString CastItoIShort(uint32 count, bool isK = false, uint32 decimal = 2);
CString CastItoIShort(uint64 count, bool isK = false, uint32 decimal = 2);
CString CastItoIShort(float count, bool isK = false, uint32 decimal = 2);
CString CastItoIShort(double count, bool isK = false, uint32 decimal = 2);
CString CastSecondsToHM(time_t seconds);
CString	CastSecondsToLngHM(time_t seconds);
CString GetFormatedUInt(ULONG ulVal);
CString GetFormatedUInt64(ULONGLONG ullVal);
CString SecToTimeLength(UINT uiSec);
bool IsRegExpValid(const CString &regexpr);
bool RegularExpressionMatch(const CString &regexpr, const CString &teststring);

// Print hash in a format which is similar to CertMgr's
CString GetCertHash(const unsigned char *pucHash, int iBytes);
// Print 'integer' in a format which is similar to CertMgr's
CString GetCertInteger(const unsigned char *pucBlob, int cbBlob);

///////////////////////////////////////////////////////////////////////////////
// URL conversion
//
CString URLDecode(const CString &sIn, bool bKeepNewLine = false);
CString URLEncode(const CString &sIn);
CString EncodeURLWC(const CString& value);
CString EncodeURLQueryParam(const CString &rstrQuery);
void DupAmpersand(CString &rstr);
CString	StripInvalidFilenameChars(const CString &strText);


///////////////////////////////////////////////////////////////////////////////
// Hex conversion
//
CString EncodeBase32(const unsigned char *buffer, unsigned int bufLen);
CString EncodeBase16(const unsigned char *buffer, unsigned int bufLen);
inline unsigned DecodeLengthBase16(unsigned base16Length);
bool DecodeBase16(const TCHAR *base16Buffer, unsigned int base16BufLen, byte *buffer, unsigned int bufflen);
uint32 DecodeBase32(LPCTSTR pszInput, uchar *paucOutput, uint32 nBufferLen);
uint32 DecodeBase32(LPCTSTR pszInput, CAICHHash &Hash);

///////////////////////////////////////////////////////////////////////////////
// File/Path string helpers
//
void slosh(CString &path); //add trailing backslash to the path
void unslosh(CString &path); //remove trailing backslash from the path
void canonical(CString &path); //applies PathCanonicalize
void MakeFoldername(CString &path); //removes trailing backslash
CString RemoveFileExtension(const CString &rstrFilePath);
bool EqualPaths(const CString &rstrDir1, const CString &rstrDir2);
CString StringLimit(const CString &in, UINT length);
CString CleanupFilename(const CString &filename, bool bExtension = true);
CString ValidFilename(const CString &filename);
bool ExpandEnvironmentStrings(CString &rstrStrings);
int CompareLocaleString(LPCTSTR psz1, LPCTSTR psz2);
int CompareLocaleStringNoCaseA(LPCSTR psz1, LPCSTR psz2);
int CompareLocaleStringNoCaseW(LPCWSTR psz1, LPCWSTR psz2);
int __cdecl CompareCStringPtrLocaleString(const void *p1, const void *p2);
int __cdecl CompareCStringPtrLocaleStringNoCase(const void *p1, const void *p2);
void Sort(CStringArray &astr, int(__cdecl *pfnCompare)(const void*, const void*) = CompareCStringPtrLocaleStringNoCase);
int __cdecl CompareCStringPtrPtrLocaleString(const void *p1, const void *p2);
int __cdecl CompareCStringPtrPtrLocaleStringNoCase(const void *p1, const void *p2);
void		Sort(CSimpleArray<const CString*> &apstr, int(__cdecl *pfnCompare)(const void*, const void*) = CompareCStringPtrPtrLocaleStringNoCase);
void		StripTrailingColon(CString &rstr);
bool		IsUnicodeFile(LPCTSTR pszFilePath);
uint64		GetFreeTempSpace(INT_PTR tempdirindex);
int			GetPathDriveNumber(const CString &path);
CString		GetShareName(const CString &path);
EFileType	GetFileTypeEx(CShareableFile *kfile, bool checkextention = true, bool checkfileheader = true, bool nocached = false);
CString		GetFileTypeName(EFileType ftype);
bool		ExtensionIs(LPCTSTR pszFilePath, LPCTSTR pszExt);
int			IsExtensionTypeOf(EFileType type, const CString &ext);
uint32		LevenshteinDistance(const CString &str1, const CString &str2);
bool		_tmakepathlimit(LPTSTR path, LPCTSTR drive, LPCTSTR dir, LPCTSTR fname, LPCTSTR ext);
bool		HasSubdirectories(const CString &strDir);
bool		DirAccsess(const CString &strDir);
#ifdef UNICODE
#define CompareLocaleStringNoCase  CompareLocaleStringNoCaseW
#else
#define CompareLocaleStringNoCase	CompareLocaleStringNoCaseA
#endif // !UNICODE
bool IsThumbsDb(const CString &sFilePath, const CString &sFileName);
bool CheckFileOpen(LPCTSTR pszFilePath, LPCTSTR pszFileTitle = NULL);
bool CFileOpen(CFile& file, LPCTSTR lpszFileName, UINT nOpenFlags, LPCTSTR lpszMsg);
void CommitAndClose(CStdioFile& file);

///////////////////////////////////////////////////////////////////////////////
// GUI helpers
//
void InstallSkin(LPCTSTR pszSkinPackage);
HINSTANCE BrowserOpen(LPCTSTR lpURL, LPCTSTR lpDirectory);
void ShellOpen(LPCTSTR lpName, LPCTSTR lpParameters);
void ShellOpenFile(LPCTSTR lpName);
void ShellDefaultVerb(LPCTSTR lpName);
bool ShellDeleteFile(LPCTSTR pszFilePath, bool bSucceedWhenFileNotFound = true);
CString ShellGetFolderPath(int iCSIDL);
bool SelectDir(HWND hWnd, LPTSTR pszPath, LPCTSTR pszTitle = NULL, LPCTSTR pszDlgTitle = NULL);
BOOL DialogBrowseFile(CString &rstrPath, LPCTSTR pszFilters, LPCTSTR pszDefaultFileName = NULL, DWORD dwFlags = 0, bool openfilestyle = true);
void AddBuddyButton(HWND hwndEdit, HWND hwndButton);
void DestroyIconsArr(HICON* pIcon, size_t cnt);
bool InitAttachedBrowseButton(HWND hwndButton, HICON &ricoBrowse);
void GetPopupMenuPos(const CListCtrl &lv, CPoint &point);
void GetPopupMenuPos(const CTreeCtrl &tv, CPoint &point);
void InitWindowStyles(CWnd *pWnd, bool bForTheApp = false);
CString GetRateString(UINT rate);
HWND ReplaceRichEditCtrl(CWnd *pwndRE, CWnd *pwndParent, CFont *pFont);
void DisableAutoSelect(CRichEditCtrl &re);
int  FontPointSizeToLogUnits(int nPointSize);
bool CreatePointFont(CFont &rFont, int nPointSize, LPCTSTR lpszFaceName);
bool CreatePointFontIndirect(CFont &rFont, const LOGFONT *lpLogFont);
void SetTabTitle(LPCTSTR key, CPropertyPage *pPage, CPropertySheet *pSheet = NULL);
bool PointInClient(const CWnd &wnd, const CPoint &point);

///////////////////////////////////////////////////////////////////////////////
// Resource strings
CString GetResString(LPCTSTR key);
void ClearTranslationKeyIndex();
int LocMessageBox(LPCTSTR key, UINT nType = MB_OK, UINT nIDHelp = 0);
CString GetResNoAmp(LPCTSTR key);
// Check if LPCTSTR string is null or empty
inline bool IsEmpty(LPCTSTR s) {
	return (s == NULL || *s == _T('\0'));
}

///////////////////////////////////////////////////////////////////////////////
// Error strings, Debugging, Logging
//
int GetSystemErrorString(DWORD dwError, CString &rstrError);
int GetModuleErrorString(DWORD dwError, CString &rstrError, LPCTSTR pszModule);
int GetErrorMessage(DWORD dwError, CString &rstrErrorMsg, DWORD dwFlags = 0);
CString GetErrorMessage(DWORD dwError, DWORD dwFlags = 0);
CString CExceptionStr(CException& ex);
CString CExceptionStrDash(CException& ex);
BOOL GetExceptionMessage(const CException &ex, LPTSTR lpszErrorMsg, UINT nMaxError);
LPCTSTR	GetShellExecuteErrMsg(DWORD dwShellExecError);
CString DbgGetHexDump(const uint8 *data, UINT size); //limited to the first 50 bytes
void DbgSetThreadName(LPCSTR szThreadName, ...);
void Debug(LPCTSTR pszFmtMsg, ...);
void DebugHexDump(const void *data, UINT lenData);
void DebugHexDump(CFile &file);
CString DbgGetFileInfo(const uchar *hash);
CString DbgGetFileStatus(UINT nPartCount, CSafeMemFile& data);
LPCTSTR DbgGetHashTypeString(const uchar *hash);
CString DbgGetClientID(uint32 nClientID);
CString DbgGetDonkeyClientTCPOpcode(UINT opcode);
CString DbgGetMuleClientTCPOpcode(UINT opcode);
CString DbgGetClientTCPOpcode(UINT protocol, UINT opcode);
CString DbgGetClientTCPPacket(UINT protocol, UINT opcode, UINT size);
CString DbgGetBlockInfo(const Requested_Block_Struct *block);
CString DbgGetBlockInfo(uint64 StartOffset, uint64 EndOffset);
CString DbgGetBlockFileInfo(const Requested_Block_Struct *block, const CPartFile *partfile);
CString DbgGetFileMetaTagName(UINT uMetaTagID);
CString DbgGetFileMetaTagName(LPCSTR pszMetaTagID);
CString DbgGetSearchOperatorName(UINT uOperator);
void DebugRecv(LPCSTR pszMsg, const CUpDownClient* client, const uchar* packet, const CAddress& IP);
void DebugRecv(LPCSTR pszOpcode, const CAddress& IP, uint16 port);
void DebugSend(LPCSTR pszOpcode, const CAddress& IP, uint16 port);
void DebugRecv(LPCSTR pszMsg, const CUpDownClient *client, const uchar *packet = NULL, uint32 nIP = 0);
void DebugRecv(LPCSTR pszOpcode, uint32 ip, uint16 port);
void DebugSend(LPCSTR pszMsg, const CUpDownClient *client, const uchar *packet = NULL);
void DebugSend(LPCSTR pszOpcode, uint32 ip, uint16 port);
void DebugSendF(LPCSTR pszOpcode, uint32 ip, uint16 port, LPCTSTR pszMsg, ...);
void DebugHttpHeaders(const CStringAArray &astrHeaders);
void throwCStr(LPCTSTR pStr);


///////////////////////////////////////////////////////////////////////////////
// Win32 specifics
//
bool Ask4RegFix(bool checkOnly, bool dontAsk = false, bool bAutoTakeCollections = false); // Barry - Allow forced update without prompt
void BackupReg(); // Barry - Store previous values
void RevertReg(); // Barry - Restore previous values
bool DoCollectionRegFix(bool checkOnly);
void AddAutoStart();
void RemAutoStart();
void SetAutoStart(bool bOn);
ULONGLONG GetModuleVersion(LPCTSTR pszFilePath);
ULONGLONG GetModuleVersion(HMODULE hModule);


#define _WINVER_95_		0x0400	// 4.0
#define _WINVER_NT4_	0x0401	// 4.1 (baked version)
#define _WINVER_98_		0x040A	// 4.10
#define _WINVER_ME_		0x045A	// 4.90
#define _WINVER_2K_		0x0500	// 5.0
#define _WINVER_XP_		0x0501	// 5.1
#define _WINVER_2003_	0x0502	// 5.2
#define _WINVER_VISTA_	0x0600	// 6.0
#define _WINVER_7_		0x0601	// 6.1
#define	_WINVER_S2008_	0x0601	// 6.1
#define _WINVER_8_		0x0602	// 6.2
#define _WINVER_8_1_	0x0603	// 6.3
#define _WINVER_10_		0x0a00	// 10.0
// Windows 11 reports the same major/minor (10.0) and is distinguished by build >= 22000. Use _WINVER_10_ together with a build check.
#define _WINVER_11_      0x0a01

WORD		DetectWinVersion();
bool		IsRunningXPSP2();
bool		IsRunningXPSP2OrHigher();
uint64		GetFreeDiskSpaceX(LPCTSTR pDirectory, bool bUpdateTime = false);
bool		GetVolumeRootPath(LPCTSTR pszFilePath, CString& strRootPath);
bool		ReplaceFileAtomically(const CString& strSourcePath, const CString& strTargetPath, DWORD* pdwLastError = NULL);
bool		CopyFileToTempAndReplace(const CString& strSourcePath, const CString& strTargetPath, const CString& strTempTargetPath, bool bDontOverrideTarget, DWORD* pdwLastError = NULL);
ULONGLONG	GetDiskFileSize(LPCTSTR pszFilePath);
bool		HasNodesDatContacts();
int			GetAppImageListColorFlag();
int			GetDesktopColorDepth();
bool		IsFileOnFATVolume(LPCTSTR pszFilePath);
void		ClearVolumeInfoCache(int iDrive = -1);

// Greyed icons.
typedef struct {
	byte b, g, r;
} BGR;
int			bestclr(const RGBQUAD *const palette, const BGR c);
byte*		bmp2mem(HBITMAP hbmp, size_t &size, REFGUID imgfmt);
HBITMAP		mem2bmp(const byte *inbuf, const size_t size);
int			AddIconGreyedToImageList(CImageList &rList, HICON hIcon);
HICON		ReplaceIconGreyedInImageList(CImageList &rList, int nPos);


///////////////////////////////////////////////////////////////////////////////
// MD4/MD5 helpers
//
#define MDX_BLOCK_SIZE	64 //both MD4 and MD5
#define MDX_DIGEST_SIZE	16

inline BYTE toHex(const BYTE x)
{
	return x + (x > 9 ? 'A' - 10 : '0');
}

// md4equ - replacement for memcmp(hash1,hash2,16) == 0
inline bool md4equ(const void *hash1, const void *hash2)
{
	return memcmp(hash1, hash2, MDX_DIGEST_SIZE) == 0;
}

inline bool isnulmd4(const void *hash)
{
	return !static_cast<const uint64*>(hash)[0] && !static_cast<const uint64*>(hash)[1];
}

inline bool isbadhash(const void *hash)
{
	return !(static_cast<const uint64*>(hash)[0] & 0xffff00ffffffffffull) && !(static_cast<const uint64*>(hash)[1] & 0xff00ffffffffffffull);
}

// md4clr - replacement for memset(hash,0,16)
inline void md4clr(const void *hash)
{
	memset(const_cast<void*>(hash), 0, MDX_DIGEST_SIZE);
}

// md4cpy - replacement for memcpy(dst,src,16)
inline void md4cpy(void *dst, const void *src)
{
	memcpy(dst, src, MDX_DIGEST_SIZE);
}

#define	MAX_HASHSTR_SIZE (MDX_DIGEST_SIZE*2+1)
CString md4str(const byte *hash);
void md4str(const byte *hash, TCHAR *pszHash);
bool strmd4(const char *pszHash, byte *hash);
bool strmd4(const CString &rstr, byte *hash);


///////////////////////////////////////////////////////////////////////////////
// Compare helpers (three-way)
//

//Designed for unsigned integral types because sgn(v0-v1) would fail.
template <typename T> inline static int CompareUnsigned(const T v0, const T v1)
{
#if _MSVC_LANG == 202002L
	return v0 <=> v1;
#else
	return (v0 < v1) ? -1 : static_cast<int>(v0 > v1);
#endif
}

inline int CompareUnsignedUndefinedAtBottom(uint32 uSize1, uint32 uSize2, bool bSortAscending)
{
	if (uSize1 == 0) {
		if (uSize2 == 0)
			return 0;
		return bSortAscending ? 1 : -1;
	}
	if (uSize2 == 0)
		return bSortAscending ? -1 : 1;
	return CompareUnsigned(uSize1, uSize2);
}

inline int CompareUnsigned64(uint64 uSize1, uint64 uSize2)
{
	return (uSize1 < uSize2) ? -1 : static_cast<int>(uSize1 > uSize2);
}

inline int CompareOptLocaleStringNoCase(LPCTSTR psz1, LPCTSTR psz2)
{
	if (psz1 && psz2)
		return CompareLocaleStringNoCase(psz1, psz2);
	return psz1 ? -1 : static_cast<int>(psz2 != NULL);
}

inline int CompareOptLocaleStringNoCaseUndefinedAtBottom(const CString &str1, const CString &str2, bool bSortAscending)
{
	if (str1.IsEmpty()) {
		if (str2.IsEmpty())
			return 0;
		return bSortAscending ? 1 : -1;
	}
	if (str2.IsEmpty())
		return bSortAscending ? -1 : 1;
	return CompareOptLocaleStringNoCase(str1, str2);
}

inline int CompareDWORD(DWORD dwSize1, DWORD dwSize2)
{
	return (dwSize1 < dwSize2) ? -1 : static_cast<int>(dwSize1 > dwSize2);
}

///////////////////////////////////////////////////////////////////////////////
// ED2K File Type
//
enum EED2KFileType
{
	ED2KFT_OTHER = -1, // GUI-only filter sentinel for “Other” (types not matching any known ED2K group)
	ED2KFT_ANY = 0,
	ED2KFT_AUDIO = 1,	// ED2K protocol value (eserver 17.6+)
	ED2KFT_VIDEO = 2,	// ED2K protocol value (eserver 17.6+)
	ED2KFT_IMAGE = 3,	// ED2K protocol value (eserver 17.6+)
	ED2KFT_PROGRAM = 4,	// ED2K protocol value (eserver 17.6+)
	ED2KFT_DOCUMENT = 5, // ED2K protocol value (eserver 17.6+)
	ED2KFT_ARCHIVE = 6,	// ED2K protocol value (eserver 17.6+)
	ED2KFT_CDIMAGE = 7,	// ED2K protocol value (eserver 17.6+)
	ED2KFT_EMULECOLLECTION = 8
};

CString GetFileTypeByName(LPCTSTR pszFileName);
CString GetFileTypeDisplayStrFromED2KFileType(LPCTSTR pszED2KFileType);
LPCTSTR GetED2KFileTypeSearchTerm(EED2KFileType iFileID, bool bSearchTypes);
EED2KFileType GetED2KFileTypeSearchID(EED2KFileType iFileID);
EED2KFileType GetED2KFileTypeID(LPCTSTR pszFileName);
bool gotostring(CFile &file, const uchar *find, LONGLONG plen);

///////////////////////////////////////////////////////////////////////////////
// IP/UserID
//
void TriggerPortTest(uint16 tcp, uint16 udp);
bool IsGoodIP(uint32 nIP, bool forceCheck = false);
bool IsGoodIPPort(uint32 nIP, uint16 nPort);
bool IsLANIP(uint32 nIP);
uint8 GetMyConnectOptions(bool bEncryption = true, bool bCallback = true);
//No longer need separate lowID checks as we now know the servers just give *.*.*.0 users a lowID
inline bool IsLowID(uint32 id)
{
	return (id < 16777216u); //0x01000000u or 2^25
}

CString ipstr(const CAddress& IP);
CString ipstr(uint32 nIP);
CString ipstr(uint32 nIP, uint16 nPort);
CString ipstr(LPCTSTR pszAddress, uint16 nPort);
CStringA ipstrA(uint32 nIP);
void ipstrA(CHAR *pszAddress, int iMaxAddress, uint32 nIP);
inline CString ipstr(in_addr nIP)
{
	return ipstr(*(uint32*)&nIP);
}

inline CStringA ipstrA(in_addr nIP)
{
	return ipstrA(*(uint32*)&nIP);
}

///////////////////////////////////////////////////////////////////////////////
// Date/Time
//
time_t safe_mktime(struct tm *ptm);
bool IsFileDateEqual(time_t a, time_t b);
bool AdjustNTFSDaylightFileTime(time_t &ruFileDate, LPCTSTR pszFilePath);
//MS have broken stat functions in XP builds of VS 2015+, and refused to fix it properly.
//Return UTC time and file size in _stat64 structure; all time fields are in UTC.
__time64_t FileTimeToUnixTime(const FILETIME &ft);
int statUTC(LPCTSTR pName, struct _stat64 &ft);
int statUTC(HANDLE hFile, struct _stat64 &ft);

///////////////////////////////////////////////////////////////////////////////
// Random Numbers
//
uint16 GetRandomUInt16();
uint32 GetRandomUInt32();

///////////////////////////////////////////////////////////////////////////////
// RC4 Encryption
//
struct RC4_Key_Struct
{
	uint8 abyState[256];
	uint8 byX;
	uint8 byY;
};

RC4_Key_Struct* RC4CreateKey(const uchar *pachKeyData, uint32 nLen, RC4_Key_Struct *key = NULL, bool bSkipDiscard = false);
void RC4Crypt(const uchar *pachIn, uchar *pachOut, uint32 nLen, RC4_Key_Struct *key);
void RC4Crypt(uchar *pach, uint32 nLen, RC4_Key_Struct *key); //in-place
const bool isExtInList(LPCTSTR extlist, const LPCTSTR ext);
const bool isExtBanned(const LPCTSTR extlist, const LPCTSTR filename);

const CString EscPercent(const CString c_strInput);

const CString RemoveTags(const CString inputString, const bool dontRemoveNumericOnlyTags);
const CString RemoveNonAlphaNumeric(const CString s);

const CString ReplaceNonAlphaNumeric(const CString in);
const int CharacterCount(const CString strInput, const LPCTSTR pszChar);

template <typename Type>
const CString ToCString(const Type value)
{
	return std::to_wstring(value).data();
}

const bool IsCorruptUserHash(const uchar* m_achUserHash);
const bool IsBadUserHash(const uchar* m_achUserHash);
const bool IsCorruptOrBadUserHash(const uchar* m_achUserHash);

const bool CompareClientIP(const CUpDownClient* client, const CAddress& dwIP);
const int CompareIP(const CAddress& dwIP1, const CAddress& dwIP2);

const std::string epoch2str(const time_t epoch);
const CStringA UTF16To8(const CStringW& utf16);
const CStringW UTF8To16(const CStringA& utf8);

bool SetFileOrDirectoryCompression(LPCTSTR path, bool bCompress); // Function to set or remove compression on a file or directory
bool SetDirectoryCompression(LPCTSTR dirPath, bool bCompress); // Recursive function to compress or decompress a directory and all its contents

class SFileTypeCbEntry
{
public:
	SFileTypeCbEntry()
		: m_pszItemData()
		, m_iImage()
	{
	}

	SFileTypeCbEntry(const CString& strLabel, LPCTSTR pszItemData, int iImage)
		: m_strLabel(strLabel)
		, m_pszItemData(pszItemData)
		, m_iImage(iImage)
	{
	}

	bool operator<(const SFileTypeCbEntry& e) const
	{
		return (m_strLabel.Compare(e.m_strLabel) < 0);
	}

	CString m_strLabel;
	LPCTSTR m_pszItemData;
	int m_iImage;
};

void InitFileTypesCtrl(CComboBoxEx2* combo, CString strSelected, bool bAddOther);

static bool IsPrime(int value);
int NextPrime(int value); 

bool IsSubDirectoryOf(const CString& sDir, const CString& sRoot);

// eMule AI: Idempotent anchor wrapper (owner-first).
// Usage examples:
#ifdef AddOrReplaceAnchor
#undef AddOrReplaceAnchor
#endif
#define AddOrReplaceAnchor(owner, target, ...) do { (owner)->RemoveAnchor(target); (owner)->AddAnchor(target, __VA_ARGS__); } while (0)

// Generic RAII guard for MediaInfo-like handles. It calls dll.Close(handle) in destructor and nulls the handle.
template<typename TDll, typename TH>
struct MediaInfoHandleGuard {
	TDll& dll;
	TH& handle;
	MediaInfoHandleGuard(TDll& d, TH& h) : dll(d), handle(h) {}
	~MediaInfoHandleGuard() {
		if (handle) {
			dll.Close(handle);
			handle = NULL;
		}
	}
	MediaInfoHandleGuard(const MediaInfoHandleGuard&) = delete;
	MediaInfoHandleGuard& operator=(const MediaInfoHandleGuard&) = delete;
};

template<typename TDll, typename TH>
inline MediaInfoHandleGuard<TDll, TH> MakeMediaInfoHandleGuard(TDll& dll, TH& handle) {
	return MediaInfoHandleGuard<TDll, TH>(dll, handle);
}

template<typename Iter> struct is_random_access { static constexpr bool value = std::is_same<typename std::iterator_traits<Iter>::iterator_category, std::random_access_iterator_tag>::value; };

// Combined, adaptive sorter. Chooses between TimSort and parallel std::sort by sampling run structure.
template<typename Iter, typename Compare> inline void CombinedSort(Iter begin, Iter end, Compare comp) {
	const size_t n = static_cast<size_t>(std::distance(begin, end));
	if (n < 5000 || begin == end) {
		gfx::timsort(begin, end, comp);
		return;
	}

	// --- Original lightweight sampling (runs + equality) ---
	const size_t kTargetChecks = 2048;
	size_t checks = 0, nondecreasing = 0, equalcnt = 0, runlen = 1;

	if constexpr (is_random_access<Iter>::value) {
		size_t step = std::max<size_t>(1, n / kTargetChecks);
		size_t i = 0;
		auto prev = begin;
		while (i + step < n && checks < kTargetChecks) {
			i += step;
			auto cur = begin; std::advance(cur, static_cast<std::ptrdiff_t>(i));
			const bool ab = comp(*prev, *cur);
			const bool ba = comp(*cur, *prev);
			const bool eq = !ab && !ba;
			nondecreasing += (ab || eq) ? 1 : 0;
			equalcnt += eq ? 1 : 0;
			++checks;

			if (ab || eq)
				++runlen;
			else
				runlen = 1;

			prev = cur;
		}
	} else {
		auto prev = begin;
		for (auto it = std::next(begin); it != end && checks < kTargetChecks; ++it) {
			const bool ab = comp(*prev, *it);
			const bool ba = comp(*it, *prev);
			const bool eq = !ab && !ba;
			nondecreasing += (ab || eq) ? 1 : 0;
			equalcnt += eq ? 1 : 0;
			++checks;

			if (ab || eq)
				++runlen;
			else
				runlen = 1;

			prev = it;
		}
	}

	if (checks == 0) {
		gfx::timsort(begin, end, comp);
		return;
	}

	const double sortedness = static_cast<double>(nondecreasing) / static_cast<double>(checks);
	const double equalratio = static_cast<double>(equalcnt) / static_cast<double>(checks);

	// Fast decision path (unchanged from v1)
	if (sortedness >= 0.55 || equalratio >= 0.05) { 
		gfx::timsort(begin, end, comp);
		return;
	}

	// --- Pilot on a small sampled subset when heuristic is inconclusive ---
	// Use a pilot sample only for reasonably large lists (n >= 10k). Take at most 500 elements or n/100, whichever is smaller. 
	// This keeps pilot overhead very low (~1% at 1020k, <0.5% at 100k+) while still giving a representative subset.
	const size_t sampleN = (std::min<size_t>(500, n / 100));
	using T = typename std::iterator_traits<Iter>::value_type;
	std::vector<T> sampleA; sampleA.reserve(sampleN);
	const size_t step = std::max<size_t>(1, n / sampleN);
	for (size_t i = 0; i < sampleN; ++i) {
		auto it = begin;
		std::advance(it, static_cast<std::ptrdiff_t>(i * step));
		sampleA.push_back(*it);
	}

	auto sampleB = sampleA; // copy
	const auto t0 = std::chrono::steady_clock::now();
	gfx::timsort(sampleA.begin(), sampleA.end(), comp);
	const auto t1 = std::chrono::steady_clock::now();
	std::sort(std::execution::par, sampleB.begin(), sampleB.end(), comp);
	const auto t2 = std::chrono::steady_clock::now();
	const auto tim_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
	const auto par_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();

	if (tim_ns <= par_ns)
		gfx::timsort(begin, end, comp);
	else
		std::sort(std::execution::par, begin, end, comp);
}

// Helpers for Windows long path support detection and path preparation.
bool IsWin32LongPathsEnabled();
void DetectWin32LongPathsSupportAtStartup();
CString PreparePathForWin32LongPath(const CString& path);
FILE* OpenFileStreamSharedReadLongPath(const CString& path, bool bTextMode);
int OpenCrtReadOnlyLongPath(const CString& path); // Returns CRT fd; -1 on failure
