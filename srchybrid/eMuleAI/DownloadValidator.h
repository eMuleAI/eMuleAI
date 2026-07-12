//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#pragma once
#include "Preferences.h"
#include "KnownFileList.h"
#include "SharedFileList.h"
#include "SearchFile.h"

class CDownloadValidator
{
	public:
		CDownloadValidator(void);
		~CDownloadValidator(void);

		void ReloadMap();
		void QueueReloadMap();
		void CancelReloadMap();
		bool ProcessReloadMapSlice(bool bDrainAll = false);
		bool HasPendingReloadMap() const { return m_pReloadMapState != NULL; }
		bool IsMapInitialized() const { return m_iDataSize != -1; }
		void AddToMap(const uchar* hash, const CString& filename, const EMFileSize filesize);
		void RemoveFromMap(const uchar* hash, const CString& filename, const EMFileSize filesize);
		const UINT CheckFile(const uchar* hash, const CString& filename, const EMFileSize filesize, const bool bCalledByAddToDownload);

		enum EDownloadValidatorResult {
			OK = 0,
			Known,
			Downloading,
			Cancelled,
			SimilarName,
			ManualBlacklisted,
			AutomaticBlacklisted
		};

		struct FileInfoType
		{
			CString strName = EMPTY;
			EMFileSize uSize = 0ull;
			uchar ucHash[MDX_DIGEST_SIZE] = { 0 };
		};

		typedef CMap<CString, LPCTSTR, FileInfoType, FileInfoType> DownloadValidatorFileMap;

		DownloadValidatorFileMap m_DownloadValidatorMap;
		DownloadValidatorFileMap m_DownloadValidatorDateTimeMap;
	protected:
		struct SReloadMapState;
		void PrepareReloadMapStorage();
		CString BuildMapKey(const CString& filename) const;
		CString BuildDateTimeProcessedFileName(const CString& filename) const;
		CString BuildDateTimeMapKey(const CString& filename, const CString& strProcessedFileName) const;
		void AddPreparedToMap(DownloadValidatorFileMap& map, const CString& strProcessedFileName, const uchar* hash, const CString& filename, const EMFileSize filesize);
		void RemovePreparedFromMap(DownloadValidatorFileMap& map, const CString& strProcessedFileName, const uchar* hash, const EMFileSize filesize);
		SReloadMapState* m_pReloadMapState;
		int 	m_iDataSize;
};
