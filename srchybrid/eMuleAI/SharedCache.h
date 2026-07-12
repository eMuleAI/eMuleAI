//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#pragma once

#include <map>
#include <vector>

class CKnownFile;

class CSharedCache
{
public:
	struct SSharedFileRecord
	{
		CString strFilePath;
		time_t tUtcFileDate;
		uint64 uFileSize;
		uchar aucFileHash[16];
	};

	CSharedCache();

	bool Load(const CString& strConfigDir, LONG lGeneration = 0, uint64 uCancellationToken = 0);
	void Save(const CString& strConfigDir) const;
	void ClearSharedRecords();
	void ReplaceSharedRecords(const std::vector<SSharedFileRecord>& records);
	void BeginReplaceSharedRecords();
	void AppendReplacementSharedRecords(const std::vector<SSharedFileRecord>& records);
	void CommitReplaceSharedRecords();
	void CancelReplaceSharedRecords();
	void BeginReplaceDuplicateRecords();
	bool CommitReplaceDuplicateRecords();
	void CancelReplaceDuplicateRecords();
	void RememberSharedFile(const CKnownFile* pFile);
	void RememberDuplicatePath(const CString& strFilePath, const uchar* pucFileHash, time_t tUtcFileDate, uint64 uFileSize);
	CKnownFile* FindKnownFileByPath(const CString& strFilePath, time_t tUtcFileDate, uint64 uFileSize) const;
	bool IsDuplicatePath(const CString& strFilePath, time_t tUtcFileDate, uint64 uFileSize, const uchar* pucFileHash) const;

	static CString GetLeafName(const CString& strPath);
	static bool ShouldIgnoreFileName(const CString& strFileName);
	static bool ShouldIgnoreDirectoryName(const CString& strDirectoryName);

private:
	struct CStringNoCaseLess
	{
		bool operator()(const CString& left, const CString& right) const;
	};

	struct SPathRecord
	{
		SPathRecord();
		CString strFilePath;
		time_t tUtcFileDate;
		uint64 uFileSize;
		uchar aucFileHash[16];
	};

	typedef std::map<CString, SPathRecord, CStringNoCaseLess> TPathRecordMap;

	static CString BuildPathKey(const CString& strFilePath);
	static bool MatchesPrefixNoCase(const CString& strValue, LPCTSTR pszPrefix);
	static bool MatchesSuffixNoCase(const CString& strValue, LPCTSTR pszSuffix);
	static bool MatchesAffixNoCase(const CString& strValue, LPCTSTR pszPrefix, LPCTSTR pszSuffix);
	static CString BuildCachePath(const CString& strConfigDir, LPCTSTR pszFileName);
	static bool LoadCombinedRecords(const CString& strFilePath, TPathRecordMap& sharedRecords, TPathRecordMap& duplicateRecords, LONG lGeneration, uint64 uCancellationToken);
	static bool LoadRecordBlock(CFile& file, TPathRecordMap& records, DWORD dwCount, DWORD& dwRecordsRead, DWORD dwTotalRecords, LONG lGeneration, uint64 uCancellationToken);
	static void QueueSaveCombinedRecords(const CString& strFilePath, const TPathRecordMap& sharedRecords, const TPathRecordMap& duplicateRecords, volatile LONG& lSaveGeneration);
	static bool ReadString(CFile& file, CString& strValue);
	static bool ReadRecord(CFile& file, SPathRecord& record);
	static void AppendRecordToChunks(std::vector<std::vector<BYTE> >& chunks, const SPathRecord& record);
	static bool IsSameFileIdentity(const SPathRecord& record, time_t tUtcFileDate, uint64 uFileSize);

	mutable CCriticalSection m_lock;
	mutable volatile LONG m_lSaveGeneration;
	bool m_bReplacingDuplicateRecords;
	TPathRecordMap m_sharedRecords;
	TPathRecordMap m_pendingSharedRecords;
	TPathRecordMap m_duplicateRecords;
	TPathRecordMap m_pendingDuplicateRecords;
};
