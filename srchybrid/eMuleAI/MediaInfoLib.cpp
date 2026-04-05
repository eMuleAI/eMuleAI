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
#include "eMule.h"
#include "MediaInfoLib.h"
#include "OtherFunctions.h"
#include "MediaInfo/MediaInfo.h"
#include "PartFile.h"
#include "Preferences.h"
#include "UserMsgs.h"
#include "SplitterControl.h"
#include "resource.h"
#include "Log.h"
#include <corecrt.h>

// Map known DirectShow/MF HRESULTs to short names
static CString GetHresultName(HRESULT hr)
{
	// Keep minimal, only common cases used here
	switch ((unsigned long)hr) {
	case 0x80040200UL: return _T("VFW_E_INVALIDMEDIATYPE");
	case 0x80040201UL: return _T("VFW_E_INVALIDSUBTYPE");
	case 0x80040202UL: return _T("VFW_E_0x80040202");
	case 0x80040206UL: return _T("VFW_E_NO_TYPES");
	case 0x80040207UL: return _T("VFW_E_NO_ACCEPTABLE_TYPES");
	case 0x80040208UL: return _T("VFW_E_INVALID_DIRECTION");
	case 0x80040216UL: return _T("VFW_E_CANNOT_RENDER");
	case 0x80040217UL: return _T("VFW_E_CANNOT_CONNECT");
	case 0x80040209UL: return _T("VFW_E_NOT_CONNECTED");
	case 0x8004020CUL: return _T("VFW_E_BUFFER_NOTSET");
	case 0x8004020DUL: return _T("VFW_E_BUFFER_OVERFLOW");
	case 0x80040212UL: return _T("VFW_E_SIZENOTSET");
	case 0x80040213UL: return _T("VFW_E_NO_DURATION");
	case 0x8004022AUL: return _T("VFW_E_INVALID_FILE_FORMAT");
	case 0x80040237UL: return _T("VFW_E_UNSUPPORTED_STREAM");
	case 0x80040241UL: return _T("VFW_E_CANNOT_LOAD_SOURCE_FILTER");
	case 0x80040255UL: return _T("VFW_E_NO_DECOMPRESSOR");
	case 0x80040154UL: return _T("REGDB_E_CLASSNOTREG");
	case 0x80040155UL: return _T("REGDB_E_IIDNOTREG");
	case 0x80004001UL: return _T("E_NOTIMPL");
	case 0x80004002UL: return _T("E_NOINTERFACE");
	case 0x80004003UL: return _T("E_POINTER");
	case 0x80004004UL: return _T("E_ABORT");
	case 0x80004005UL: return _T("E_FAIL");
	case 0x800401F0UL: return _T("CO_E_NOTINITIALIZED");
	case 0x80040111UL: return _T("CLASS_E_CLASSNOTAVAILABLE");
	case 0x8007000EUL: return _T("E_OUTOFMEMORY");
	case 0x80070057UL: return _T("E_INVALIDARG");
	case 0x80070005UL: return _T("E_ACCESSDENIED");
	case 0x80070002UL: return _T("ERROR_FILE_NOT_FOUND");
	case 0x80070003UL: return _T("ERROR_PATH_NOT_FOUND");
	case 0x80070020UL: return _T("ERROR_SHARING_VIOLATION");
	case 0x8007000BUL: return _T("ERROR_BAD_FORMAT");
	case 0x8007000DUL: return _T("ERROR_INVALID_DATA");
	case 0x8007001FUL: return _T("ERROR_GEN_FAILURE");
	case 0xC00D36C4UL: return _T("MF_E_UNSUPPORTED_BYTESTREAM_TYPE");
	case 0xC00D36C3UL: return _T("MF_E_INVALID_FILE_FORMAT");
	case 0xC00D36B4UL: return _T("MF_E_UNSUPPORTED_MIME_TYPE");
	case 0xC00D36F5UL: return _T("MF_E_BYTESTREAM_NOT_SEEKABLE");
	default: return _T("UNKNOWN_HRESULT");
	}
}

// Provide a user friendly explanation for known HRESULTs
static CString GetHresultExplanation(HRESULT hr)
{
	switch ((unsigned long)hr) {
	case 0x80040200UL: return _T("The media type is not supported by the selected filter or stream.");
	case 0x80040201UL: return _T("The subtype is invalid or not supported by the filter.");
	case 0x80040202UL: return _T("A DirectShow error occurred (undocumented mapping 0x80040202).");
	case 0x80040206UL: return _T("The filter does not support any media types on this pin.");
	case 0x80040207UL: return _T("No acceptable media types were found during connection negotiation.");
	case 0x80040208UL: return _T("The direction of the pin does not allow this operation.");
	case 0x80040216UL: return _T("The graph builder could not render one or more streams.");
	case 0x80040217UL: return _T("Filters could not connect (incompatible pins or missing intermediate filter).");
	case 0x80040209UL: return _T("The pin is not connected.");
	case 0x8004020CUL: return _T("The buffer was not set before the operation.");
	case 0x8004020DUL: return _T("A buffer overflow occurred during processing.");
	case 0x80040212UL: return _T("A required buffer size was not specified.");
	case 0x80040213UL: return _T("The stream duration is not available.");
	case 0x8004022AUL: return _T("The file has an invalid or unsupported format.");
	case 0x80040237UL: return _T("The stream type is not supported by the filter graph.");
	case 0x80040241UL: return _T("The source filter for this file could not be loaded (missing component or bad registration).");
	case 0x80040255UL: return _T("No suitable decompressor (codec) is available to decode this stream.");
	case 0x80040154UL: return _T("A required COM class is not registered (missing codec/filter).");
	case 0x80040155UL: return _T("The requested interface ID (IID) is not registered.");
	case 0x80004001UL: return _T("The operation is not implemented by the component.");
	case 0x80004002UL: return _T("The object does not support the requested interface.");
	case 0x80004003UL: return _T("An invalid or NULL pointer was passed to the API.");
	case 0x80004004UL: return _T("The operation was aborted by the caller or component.");
	case 0x80004005UL: return _T("Unspecified failure returned by an underlying component.");
	case 0x800401F0UL: return _T("COM library is not initialized on the calling thread.");
	case 0x80040111UL: return _T("The requested COM class is not available from the DLL.");
	case 0x8007000EUL: return _T("The system is out of memory or resources.");
	case 0x80070057UL: return _T("One or more arguments are invalid.");
	case 0x80070005UL: return _T("Access is denied (insufficient permissions or resource in use).");
	case 0x80070002UL: return _T("The system cannot find the file specified.");
	case 0x80070003UL: return _T("The system cannot find the path specified.");
	case 0x80070020UL: return _T("The process cannot access the file because it is being used by another process.");
	case 0x8007000BUL: return _T("The file format is invalid (corrupt or unsupported headers).");
	case 0x8007000DUL: return _T("The data is invalid (corruption or parsing error).");
	case 0x8007001FUL: return _T("A device or system error occurred while accessing the file.");
	case 0xC00D36C4UL: return _T("The bytestream type is not supported by Media Foundation.");
	case 0xC00D36C3UL: return _T("Media Foundation reports an invalid or unsupported file format.");
	case 0xC00D36B4UL: return _T("The MIME type is not supported.");
	case 0xC00D36F5UL: return _T("The bytestream is not seekable; some operations may fail.");
	default: return _T("Unknown error.");
	}
}

// Build a compact classification string for logging
static CString FormatMediaDetError(HRESULT hr, LPCTSTR sysMsg)
{
	CString s;
	s.Format(_T("hr=0x%08lX %s | %s | %s"), (unsigned long)hr, (LPCTSTR)GetHresultName(hr), (LPCTSTR)GetHresultExplanation(hr), (LPCTSTR)sysMsg);
	return s;
}

static CString TrimMediaInfoValue(const CString& strValue)
{
	CString strTrimmed(strValue);
	strTrimmed.Trim();
	return strTrimmed;
}

static CString GetMediaInfoValue(CMediaInfoLIB& mediaInfoLib, void* hMediaInfo, MediaInfo_stream_C streamKind, int iStreamNumber, LPCTSTR pszParameter)
{
	return TrimMediaInfoValue(mediaInfoLib.InfoGet(hMediaInfo, streamKind, iStreamNumber, pszParameter));
}

static CString GetFirstMediaInfoValue(CMediaInfoLIB& mediaInfoLib, void* hMediaInfo, MediaInfo_stream_C streamKind, int iStreamNumber, const LPCTSTR* apszParameters, size_t uParameterCount)
{
	for (size_t i = 0; i < uParameterCount; ++i) {
		CString strValue(GetMediaInfoValue(mediaInfoLib, hMediaInfo, streamKind, iStreamNumber, apszParameters[i]));
		if (!strValue.IsEmpty())
			return strValue;
	}
	return CString();
}

static void PopulateCoreGeneralTags(CMediaInfoLIB& mediaInfoLib, void* hMediaInfo, SMediaInfo* mi)
{
	if (mi == NULL || hMediaInfo == NULL)
		return;

	const LPCTSTR apszAuthorParameters[] = { _T("Performer"), _T("Author") };
	const LPCTSTR apszAlbumParameters[] = { _T("Album") };

	mi->strTitle = GetMediaInfoValue(mediaInfoLib, hMediaInfo, MediaInfo_Stream_General, 0, _T("Title"));
	CString strTitleMore(GetMediaInfoValue(mediaInfoLib, hMediaInfo, MediaInfo_Stream_General, 0, _T("Title_More")));
	if (mi->strTitle.IsEmpty())
		mi->strTitle = strTitleMore;
	else if (!strTitleMore.IsEmpty() && strTitleMore != mi->strTitle)
		mi->strTitle.AppendFormat(_T("; %s"), (LPCTSTR)strTitleMore);

	mi->strAuthor = GetFirstMediaInfoValue(mediaInfoLib, hMediaInfo, MediaInfo_Stream_General, 0, apszAuthorParameters, _countof(apszAuthorParameters));
	mi->strAlbum = GetFirstMediaInfoValue(mediaInfoLib, hMediaInfo, MediaInfo_Stream_General, 0, apszAlbumParameters, _countof(apszAlbumParameters));
}

struct CMediaInfoInvalidParameterException
{
};

class CInvalidParameterScope
{
public:
	CInvalidParameterScope()
		: m_prevHandler(_set_thread_local_invalid_parameter_handler(&CInvalidParameterScope::Handle))
	{
	}

	~CInvalidParameterScope()
	{
		_set_thread_local_invalid_parameter_handler(m_prevHandler);
	}

private:
	static void __cdecl Handle(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t)
	{
		throw CMediaInfoInvalidParameterException();
	}

	_invalid_parameter_handler m_prevHandler;
};

CMediaInfoLIB::CMediaInfoLIB()
	: m_handle()
{
}

CMediaInfoLIB::~CMediaInfoLIB()
{
}


void* CMediaInfoLIB::Open(LPCTSTR File)
{
	// Create a new MediaInfo object and open the file
	MediaInfoLib::MediaInfo* pMI = new MediaInfoLib::MediaInfo();
	try {
		if (pMI->Open(MediaInfoLib::String(File)) == 0) {
			delete pMI;
			return NULL;
		}
	} catch (...) {
		// Ensure we don't leak the MediaInfo instance on exceptions.
		delete pMI;
		throw;
	}
	return reinterpret_cast<void*>(pMI);
}

void CMediaInfoLIB::Close(void* Handle)
{
	MediaInfoLib::MediaInfo* pMI = reinterpret_cast<MediaInfoLib::MediaInfo*>(Handle);
	if (pMI) {
		pMI->Close();
		delete pMI;
	}
}

CString CMediaInfoLIB::Get(void* Handle, MediaInfo_stream_C StreamKind, int StreamNumber, LPCTSTR Parameter, MediaInfo_info_C KindOfInfo, MediaInfo_info_C KindOfSearch)
{
	MediaInfoLib::MediaInfo* pMI = reinterpret_cast<MediaInfoLib::MediaInfo*>(Handle);
	if (!pMI)
		return CString();
	MediaInfoLib::String val = pMI->Get(static_cast<MediaInfoLib::stream_t>(StreamKind), static_cast<size_t>(StreamNumber), MediaInfoLib::String(Parameter), static_cast<MediaInfoLib::info_t>(KindOfInfo), static_cast<MediaInfoLib::info_t>(KindOfSearch));
	return CString(val.c_str());
}

CString CMediaInfoLIB::GetI(void* Handle, MediaInfo_stream_C StreamKind, size_t StreamNumber, size_t iParameter, MediaInfo_info_C KindOfInfo)
{
	MediaInfoLib::MediaInfo* pMI = reinterpret_cast<MediaInfoLib::MediaInfo*>(Handle);
	if (!pMI)
		return CString();
	MediaInfoLib::String val = pMI->Get(static_cast<MediaInfoLib::stream_t>(StreamKind), StreamNumber, iParameter, static_cast<MediaInfoLib::info_t>(KindOfInfo));
	return CString(val.c_str());
}

CString CMediaInfoLIB::InfoGet(MediaInfo_stream_C StreamKind, int StreamNumber, LPCTSTR Parameter)
{
	return Get(m_handle, StreamKind, StreamNumber, Parameter, MediaInfo_Info_Text, MediaInfo_Info_Name);
}

CString CMediaInfoLIB::InfoGet(void* Handle, MediaInfo_stream_C StreamKind, int StreamNumber, LPCTSTR Parameter)
{
	return Get(Handle, StreamKind, StreamNumber, Parameter, MediaInfo_Info_Text, MediaInfo_Info_Name);
}

CString CMediaInfoLIB::InfoGetNameText(MediaInfo_stream_C StreamKind, int StreamNumber, LPCTSTR Parameter)
{
	return Get(m_handle, StreamKind, StreamNumber, Parameter, MediaInfo_Info_Name_Text, MediaInfo_Info_Name);
}

CString CMediaInfoLIB::InfoGetNameText(void* Handle, MediaInfo_stream_C StreamKind, int StreamNumber, LPCTSTR Parameter)
{
	return Get(Handle, StreamKind, StreamNumber, Parameter, MediaInfo_Info_Name_Text, MediaInfo_Info_Name);
}

CString CMediaInfoLIB::InfoGetI(MediaInfo_stream_C StreamKind, int StreamNumber, size_t Parameter, MediaInfo_info_C KindOfInfo)
{
	return GetI(m_handle, StreamKind, StreamNumber, Parameter, KindOfInfo);
}

CString CMediaInfoLIB::InfoGetI(void* Handle, MediaInfo_stream_C StreamKind, int StreamNumber, size_t Parameter, MediaInfo_info_C KindOfInfo)
{
	return GetI(Handle, StreamKind, StreamNumber, Parameter, KindOfInfo);
}


bool CMediaInfoLIB::GetMediaInfo(const CShareableFile* pFile, SMediaInfo* mi)
{
	if (!pFile || pFile->GetFilePath().IsEmpty())
		return false;
	ASSERT(!pFile->GetFilePath().IsEmpty());

	bool bHasDRM = false;
	if (!pFile->IsPartFile() || static_cast<const CPartFile*>(pFile)->IsCompleteBDSafe(0, 1024)) {
		GetMimeType(pFile->GetFilePath(), mi->strMimeType);
		bHasDRM = GetDRM(pFile->GetFilePath());
	}

	mi->ulFileSize = pFile->GetFileSize();

	bool bFoundHeader = false;
	if (pFile->IsPartFile()) {
		// Do *not* pass a part file which does not have the beginning of file to the following code.
		//	- Header based parsers will not work without the beginning of the file.
		//
		//	- Most metadata probes also need the header range to be present.
		if (!static_cast<const CPartFile*>(pFile)->IsCompleteSafe(0, 16 * 1024))
			return bFoundHeader || !mi->strMimeType.IsEmpty();
	}

	CString szExt(::PathFindExtension(pFile->GetFileName()));
	szExt.MakeLower();

	// Prefer MediaInfoLib for all formats; legacy per-extension probes removed.

	// starting the MediaDet object takes a noticeable amount of time. Avoid starting that object
	// for files which are not expected to contain any Audio/Video data.
	// note also: MediaDet does not work well for too short files (e.g. 16K)
	//
	// same applies for MediaInfoLib, its even slower than MediaDet -> avoid calling for non AV files.
	//
	// since we have a thread here, this should not be a performance problem any longer.

	/////////////////////////////////////////////////////////////////////////////
	// Try MediaInfo lib
	//
	// Use MediaInfo for all supported formats first.
	//
	try {
		CInvalidParameterScope invalidParameterGuard; // Guard CRT aborts from invalid parameter callbacks
		m_handle = Open(pFile->GetFilePath());
		if (m_handle) {
			auto _miGuard = MakeMediaInfoHandleGuard(*this, m_handle); // RAII: always Close on exit
			LPCTSTR	pCodec = _T("Format");
			LPCTSTR pCodecInfo = _T("Format/Info");
			LPCTSTR pCodecString = _T("Format/String");

			mi->strFileFormat = InfoGet(MediaInfo_Stream_General, 0, _T("Format"));
			PopulateCoreGeneralTags(*this, m_handle, mi);
			CString str(InfoGet(MediaInfo_Stream_General, 0, _T("Format/String")));
			if (!str.IsEmpty() && str != mi->strFileFormat)
				mi->strFileFormat.AppendFormat(_T(" (%s)"), (LPCTSTR)str);

			if (szExt[0] == _T('.') && szExt[1] != _T('\0')) {
				str = InfoGet(MediaInfo_Stream_General, 0, _T("Format/Extensions"));
				if (!str.IsEmpty()) {
					// minor bug in MediaInfo lib: some file extension lists have a ')' character in there.
					str.Remove(_T(')'));
					str.Remove(_T('('));
					str.MakeLower();

					bool bFoundExt = false;
					for (int iPos = 0; iPos >= 0;) {
						const CString& strFmtExt(str.Tokenize(_T(" "), iPos));
						if (!strFmtExt.IsEmpty() && strFmtExt == CPTR(szExt, 1)) {
							bFoundExt = true;
							break;
						}
					}
				}
			}
							
			str = InfoGet(MediaInfo_Stream_General, 0, _T("Duration"));
			if (str.IsEmpty())
				str = InfoGet(MediaInfo_Stream_General, 0, _T("PlayTime")); //deprecated
			float fFileLengthSec = _tstoi(str) / 1000.0F;
			UINT uAllBitrates = 0;

			str = InfoGet(MediaInfo_Stream_General, 0, _T("VideoCount"));
			int iVideoStreams = _tstoi(str);
			if (iVideoStreams > 0) {
				mi->iVideoStreams = iVideoStreams;
				mi->fVideoLengthSec = fFileLengthSec;

				str = InfoGet(MediaInfo_Stream_Video, 0, pCodec);
				mi->strVideoFormat = str;
				if (!str.IsEmpty()) {
					CStringA strCodecA(str);
					if (!strCodecA.IsEmpty())
						mi->video.bmiHeader.biCompression = *(LPDWORD)(LPCSTR)strCodecA;
				}
				str = InfoGet(MediaInfo_Stream_Video, 0, pCodecString);
				if (!str.IsEmpty() && str != mi->strVideoFormat)
					mi->strVideoFormat.AppendFormat(_T(" (%s)"), (LPCTSTR)str);

				str = InfoGet(MediaInfo_Stream_Video, 0, _T("Width"));
				mi->video.bmiHeader.biWidth = _tstoi(str);

				str = InfoGet(MediaInfo_Stream_Video, 0, _T("Height"));
				mi->video.bmiHeader.biHeight = _tstoi(str);

				str = InfoGet(MediaInfo_Stream_Video, 0, _T("FrameRate"));
				mi->fVideoFrameRate = _tstof(str);

				str = InfoGet(MediaInfo_Stream_Video, 0, _T("BitRate_Mode"));
				if (str.CompareNoCase(_T("VBR")) == 0) {
					mi->video.dwBitRate = _UI32_MAX;
					uAllBitrates = _UI32_MAX;
				} else {
					str = InfoGet(MediaInfo_Stream_Video, 0, _T("BitRate"));
					int iBitrate = _tstoi(str);
					mi->video.dwBitRate = iBitrate == -1 ? -1 : iBitrate;
					if (iBitrate == -1)
						uAllBitrates = _UI32_MAX;
					else //if (uAllBitrates != _UI32_MAX) always true
						uAllBitrates += iBitrate;
				}

				str = InfoGet(MediaInfo_Stream_Video, 0, _T("AspectRatio"));
				mi->fVideoAspectRatio = _tstof(str);

				for (int s = 1; s < iVideoStreams; ++s) {
					CString strVideoFormat(InfoGet(MediaInfo_Stream_Video, s, pCodec));
					str = InfoGet(MediaInfo_Stream_Video, s, pCodecString);
					if (!str.IsEmpty() && str != strVideoFormat)
						strVideoFormat.AppendFormat(_T(" (%s)"), (LPCTSTR)str);

					str = InfoGet(MediaInfo_Stream_Video, s, _T("BitRate_Mode"));
					if (str.CompareNoCase(_T("VBR")) == 0) {
						uAllBitrates = _UI32_MAX;
					} else {
						str = InfoGet(MediaInfo_Stream_Video, s, _T("BitRate"));
						int iBitrate = _tstoi(str);
						if (iBitrate != 0) {
							if (iBitrate == -1) {
								uAllBitrates = _UI32_MAX;
							} else {
								if (uAllBitrates != _UI32_MAX)
									uAllBitrates += iBitrate;
							}
						}
					}
				}

				bFoundHeader = true;
			}

			str = InfoGet(MediaInfo_Stream_General, 0, _T("AudioCount"));
			int iAudioStreams = _tstoi(str);
			if (iAudioStreams > 0) {
				mi->iAudioStreams = iAudioStreams;
				mi->fAudioLengthSec = fFileLengthSec;

				str = InfoGet(MediaInfo_Stream_Audio, 0, pCodec);
				if (_stscanf(str, _T("%hx"), &mi->audio.wFormatTag) != 1) {
					mi->strAudioFormat = str;
					str = InfoGet(MediaInfo_Stream_Audio, 0, pCodecString);
				} else {
					mi->strAudioFormat = InfoGet(MediaInfo_Stream_Audio, 0, pCodecString);
					str = InfoGet(MediaInfo_Stream_Audio, 0, pCodecInfo);
				}
				if (!str.IsEmpty() && str != mi->strAudioFormat)
					mi->strAudioFormat.AppendFormat(_T(" (%s)"), (LPCTSTR)str);

				str = InfoGet(MediaInfo_Stream_Audio, 0, _T("Channel(s)"));
				mi->audio.nChannels = (WORD)_tstoi(str);

				str = InfoGet(MediaInfo_Stream_Audio, 0, _T("SamplingRate"));
				mi->audio.nSamplesPerSec = _tstoi(str);

				str = InfoGet(MediaInfo_Stream_Audio, 0, _T("BitRate_Mode"));
				if (str.CompareNoCase(_T("VBR")) == 0) {
					mi->audio.nAvgBytesPerSec = _UI32_MAX;
					uAllBitrates = _UI32_MAX;
				} else {
					str = InfoGet(MediaInfo_Stream_Audio, 0, _T("BitRate"));
					int iBitrate = _tstoi(str);
					mi->audio.nAvgBytesPerSec = iBitrate == -1 ? -1 : iBitrate / 8;
					if (iBitrate == -1)
						uAllBitrates = _UI32_MAX;
					else if (uAllBitrates != _UI32_MAX)
						uAllBitrates += iBitrate;
				}

				mi->strAudioLanguage = InfoGet(MediaInfo_Stream_Audio, 0, _T("Language/String"));
				if (mi->strAudioLanguage.IsEmpty())
					mi->strAudioLanguage = InfoGet(MediaInfo_Stream_Audio, 0, _T("Language"));

				for (int s = 1; s < iAudioStreams; ++s) {

					str = InfoGet(MediaInfo_Stream_Audio, s, pCodecString);
					WORD wFormatTag;
					if (_stscanf(str, _T("%hx"), &wFormatTag) == 1)
						str = InfoGet(MediaInfo_Stream_Audio, s, pCodecInfo);


					str = InfoGet(MediaInfo_Stream_Audio, s, _T("BitRate_Mode"));
					if (str.CompareNoCase(_T("VBR")) == 0) {
						uAllBitrates = _UI32_MAX;
					} else {
						str = InfoGet(MediaInfo_Stream_Audio, s, _T("BitRate"));
						int iBitrate = _tstoi(str);
						if (iBitrate != 0) {
							if (iBitrate == -1)
								uAllBitrates = _UI32_MAX;
							else if (uAllBitrates != _UI32_MAX)
									uAllBitrates += iBitrate;
						}
					}
				}

				bFoundHeader = true;
			}

			Close(m_handle);
			m_handle = NULL;

			// MediaInfoLib does not handle MPEG files correctly in regards of
			// play length property -- even for completed files (applies also for
			// v0.7.2.1). So, we try to calculate the play length by using the
			// various bit rates. We could do this only for part files which are
			// still not having the final file length, but MediaInfoLib also
			// fails to determine play length for completed files (Hint: one can
			// not use GOPs to determine the play length (properly)).
			//
			//"MPEG 1"		v0.7.0.0
			//"MPEG-1 PS"	v0.7.2.1
			if (mi->strFileFormat.Find(_T("MPEG")) == 0) {	/* MPEG container? */
				if (uAllBitrates != 0				/* do we have any bit rates? */
					&& uAllBitrates != _UI32_MAX)	/* do we have CBR only? */
				{
					// Though, it's not that easy to calculate the real play length
					// without including the container's overhead. The value we
					// calculate with this simple formula is slightly too large!
					// But, its still better than using GOP-derived values which are
					// sometimes completely wrong.
					fFileLengthSec = (uint64)pFile->GetFileSize() * 8.0f / uAllBitrates;

					if (mi->iVideoStreams > 0) {
						// Try to compensate the error from above by estimating the overhead
						if (mi->fVideoFrameRate > 0) {
							ULONGLONG uFrames = (ULONGLONG)(fFileLengthSec * mi->fVideoFrameRate);
							fFileLengthSec = ((uint64)pFile->GetFileSize() - uFrames * 24) * 8.0f / uAllBitrates;
						}
						mi->fVideoLengthSec = fFileLengthSec;
					}
					if (mi->iAudioStreams > 0)
						mi->fAudioLengthSec = fFileLengthSec;
				}
				// set the 'estimated' flags in case of any VBR stream
				mi->bVideoLengthEstimated |= (mi->iVideoStreams > 0);
				mi->bAudioLengthEstimated |= (mi->iAudioStreams > 0);
			}

			if (bFoundHeader) {
				mi->InitFileLength();
				return true;
			}
		}
	} catch (const CMediaInfoInvalidParameterException&) {
		theApp.QueueDebugLogLine(false, _T("[MediaInfo] Invalid parameter while probing \"%s\"\n"), (LPCTSTR)pFile->GetFilePath());
	} catch (...) {
		theApp.QueueDebugLogLine(false, _T("[MediaInfo] Exception occured while probing \"%s\"\n"), (LPCTSTR)pFile->GetFilePath());
	}

	/////////////////////////////////////////////////////////////////////////////
	// Try MediaDet object
	//
	// Avoid processing of some file types which are known to crash due to bugged DirectShow filters.
#ifdef HAVE_QEDIT_H
	if (!bFoundHeader) {
		try {
			CComPtr<IMediaDet> pMediaDet;
			HRESULT hr = pMediaDet.CoCreateInstance(__uuidof(MediaDet));
			if (SUCCEEDED(hr)) {
				hr = pMediaDet->put_Filename(CComBSTR(pFile->GetFilePath()));
				if (SUCCEEDED(hr)) {
					long lStreams;
					if (SUCCEEDED(pMediaDet->get_OutputStreams(&lStreams))) {
						for (long i = 0; i < lStreams; ++i) {
							if (SUCCEEDED(pMediaDet->put_CurrentStream(i))) {
								GUID major_type;
								if (SUCCEEDED(pMediaDet->get_StreamType(&major_type))) {
									if (major_type == MEDIATYPE_Video) {
										++mi->iVideoStreams;

										AM_MEDIA_TYPE mt;
										if (SUCCEEDED(pMediaDet->get_StreamMediaType(&mt))) {
											if (mt.formattype == FORMAT_VideoInfo) {
												VIDEOINFOHEADER* pVIH = (VIDEOINFOHEADER*)mt.pbFormat;
												if (mi->iVideoStreams == 1) {
													mi->video = *pVIH;
													if (mi->video.bmiHeader.biWidth && mi->video.bmiHeader.biHeight)
														mi->fVideoAspectRatio = abs(mi->video.bmiHeader.biWidth) / (double)abs(mi->video.bmiHeader.biHeight);
													mi->video.dwBitRate = 0; // don't use this value
													mi->strVideoFormat = GetVideoFormatName(mi->video.bmiHeader.biCompression);
													pMediaDet->get_FrameRate(&mi->fVideoFrameRate);
													bFoundHeader = true;
												}
											}
										}

										double fLength;
										if (SUCCEEDED(pMediaDet->get_StreamLength(&fLength)) && fLength)
											if (mi->iVideoStreams == 1)
												mi->fVideoLengthSec = fLength;

										if (mt.pUnk != NULL)
											mt.pUnk->Release();
										::CoTaskMemFree(mt.pbFormat);
									}
									else if (major_type == MEDIATYPE_Audio) {
										++mi->iAudioStreams;
										AM_MEDIA_TYPE mt;
										if (SUCCEEDED(pMediaDet->get_StreamMediaType(&mt))) {
											if (mt.formattype == FORMAT_WaveFormatEx) {
												WAVEFORMATEX* wfx = (WAVEFORMATEX*)mt.pbFormat;

												// Try to determine if the stream is VBR.
												//
												// MediaDet seems to only look at the AVI stream headers to get a hint
												// about CBR/VBR. If the stream headers are looking "odd", MediaDet
												// reports "VBR". Typically, files muxed with Nandub get identified as
												// VBR (just because of the stream headers) even if they are CBR. Also,
												// real VBR MP3 files still get reported as CBR. Though, basically it's
												// better to report VBR even if it's CBR. The other way round is even
												// more ugly.
												if (!mt.bFixedSizeSamples)
													wfx->nAvgBytesPerSec = _UI32_MAX;

												if (mi->iAudioStreams == 1) {
													mi->audio = *(WAVEFORMAT*)wfx;
													mi->strAudioFormat = GetAudioFormatName(wfx->wFormatTag);
												}

												bFoundHeader = true;
											}
										}

										double fLength;
										if (SUCCEEDED(pMediaDet->get_StreamLength(&fLength)) && fLength)
											if (mi->iAudioStreams == 1)
												mi->fAudioLengthSec = fLength;

										if (mt.pUnk != NULL)
											mt.pUnk->Release();
										::CoTaskMemFree(mt.pbFormat);
									}
								}
							}
						}

						if (bFoundHeader)
							mi->InitFileLength();
					}
				} else
					theApp.QueueDebugLogLine(false, _T("[MediaInfo] Open failed for \"%s\": %s\n"), (LPCTSTR)pFile->GetFilePath(), (LPCTSTR)FormatMediaDetError(hr, GetErrorMessage(hr, 1))); // Provide more context for common DirectShow errors
			}
		} catch (...) {
			theApp.QueueDebugLogLine(false, _T("[MediaInfo] Exception while probing \"%s\"\n"), (LPCTSTR)pFile->GetFilePath()); // Classify for diagnostics
		}
	}
#else//HAVE_QEDIT_H
#pragma message("WARNING: Missing 'qedit.h' header file - some features will get disabled. See the file 'emule_site_config.h' for more information.")
#endif//HAVE_QEDIT_H

	return bFoundHeader || !mi->strTitle.IsEmpty() || !mi->strAuthor.IsEmpty() || !mi->strAlbum.IsEmpty() || !mi->strMimeType.IsEmpty() || bHasDRM;
}
