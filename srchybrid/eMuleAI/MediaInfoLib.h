//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#pragma once
#include "MediaInfo.h"
#include "ShareableFile.h"

class CKnownFile;
struct SMediaInfo;

// MediaInfoLib
/** @brief Kinds of Stream */
typedef enum MediaInfo_stream_t
{
	MediaInfo_Stream_General,
	MediaInfo_Stream_Video,
	MediaInfo_Stream_Audio,
	MediaInfo_Stream_Text,
	MediaInfo_Stream_Other,
	MediaInfo_Stream_Image,
	MediaInfo_Stream_Menu,
	MediaInfo_Stream_Max
} MediaInfo_stream_C;

/** @brief Kinds of Info */
typedef enum MediaInfo_info_t
{
	MediaInfo_Info_Name,
	MediaInfo_Info_Text,
	MediaInfo_Info_Measure,
	MediaInfo_Info_Options,
	MediaInfo_Info_Name_Text,
	MediaInfo_Info_Measure_Text,
	MediaInfo_Info_Info,
	MediaInfo_Info_HowTo,
	MediaInfo_Info_Max
} MediaInfo_info_C;

/////////////////////////////////////////////////////////////////////////////
// CMediaInfoLib

class CMediaInfoLIB
{
public:
	CMediaInfoLIB();
	~CMediaInfoLIB();
	bool Initialize();
	void* Open(LPCTSTR File);
	void Close(void* Handle);
	bool GetMediaInfo(const CShareableFile* pFile, SMediaInfo* mi);

	// Expose getters for UI formatting needs
	CString InfoGet(void* Handle, MediaInfo_stream_C StreamKind, int StreamNumber, LPCTSTR Parameter);
	CString InfoGet(MediaInfo_stream_C StreamKind, int StreamNumber, LPCTSTR Parameter);
	CString InfoGetNameText(void* Handle, MediaInfo_stream_C StreamKind, int StreamNumber, LPCTSTR Parameter);
	CString InfoGetNameText(MediaInfo_stream_C StreamKind, int StreamNumber, LPCTSTR Parameter);
	CString InfoGetI(void* Handle, MediaInfo_stream_C StreamKind, int StreamNumber, size_t Parameter, MediaInfo_info_C KindOfInfo);
	CString InfoGetI(MediaInfo_stream_C StreamKind, int StreamNumber, size_t Parameter, MediaInfo_info_C KindOfInfo);

protected:
	// Static MediaInfoLib (C++ API) usage
	CString Get(void* Handle, MediaInfo_stream_C StreamKind, int StreamNumber, LPCTSTR Parameter, MediaInfo_info_C KindOfInfo, MediaInfo_info_C KindOfSearch);
	CString GetI(void* Handle, MediaInfo_stream_C StreamKind, size_t StreamNumber, size_t iParameter, MediaInfo_info_C KindOfInfo);
	HANDLE m_handle;
};
