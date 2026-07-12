//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#include "stdafx.h"
#include "SharedCache.h"
#include "../KnownFile.h"
#include "../KnownFileList.h"
#include "../Emule.h"
#include "../Preferences.h"
#include "../OtherFunctions.h"
#include "../PartFileWriteThread.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	const DWORD kSharedCacheMagic = 0x43534941; // AISC.
	const WORD kSharedCacheVersion = 1;
	const WORD kSharedCacheFlags = 0;
	const DWORD kMaxPathRecords = 200000;
	const DWORD kMaxTotalPathRecords = kMaxPathRecords * 2;
	const DWORD kMaxPathChars = 32768;
	const ULONGLONG kSerializedHeaderBytes = sizeof(DWORD) + sizeof(WORD) + sizeof(WORD) + sizeof(DWORD) + sizeof(DWORD);
	const ULONGLONG kMinSerializedRecordBytes = sizeof(DWORD) + sizeof(__int64) + sizeof(uint64) + 16;
	const TCHAR kSharedCacheFileName[] = _T("sharedcache.dat");
	const size_t kAsyncCacheChunkBytes = 64 * 1024;

	template<typename T>
	bool ReadValue(CFile& file, T& value)
	{
		return file.Read(&value, static_cast<UINT>(sizeof(value))) == static_cast<UINT>(sizeof(value));
	}

	void AppendBytesToChunks(std::vector<std::vector<BYTE> >& chunks, const void* pData, size_t uSize)
	{
		if (pData == NULL || uSize == 0)
			return;

		const BYTE* pBytes = static_cast<const BYTE*>(pData);
		while (uSize > 0) {
			if (chunks.empty() || chunks.back().size() >= kAsyncCacheChunkBytes)
				chunks.push_back(std::vector<BYTE>());
			std::vector<BYTE>& chunk = chunks.back();
			const size_t uAvailable = kAsyncCacheChunkBytes - chunk.size();
			const size_t uCopy = min(uAvailable, uSize);
			const size_t uOldSize = chunk.size();
			chunk.resize(uOldSize + uCopy);
			memcpy(&chunk[uOldSize], pBytes, uCopy);
			pBytes += uCopy;
			uSize -= uCopy;
		}
	}

	template<typename T>
	void AppendValueToChunks(std::vector<std::vector<BYTE> >& chunks, const T& value)
	{
		AppendBytesToChunks(chunks, &value, sizeof(value));
	}

	void AppendStringToChunks(std::vector<std::vector<BYTE> >& chunks, const CString& strValue)
	{
		const DWORD dwChars = static_cast<DWORD>(strValue.GetLength());
		AppendValueToChunks(chunks, dwChars);
		if (dwChars != 0)
			AppendBytesToChunks(chunks, static_cast<LPCTSTR>(strValue), static_cast<size_t>(dwChars) * sizeof(TCHAR));
	}

	void PublishSharedCacheLoadProgress(LONG lGeneration, uint64 uCancellationToken, LPCTSTR pszStage, UINT uDone, UINT uTotal)
	{
		if (lGeneration != 0 && uCancellationToken != 0)
			theApp.PublishStartupMetadataLoadProgress(CemuleApp::StartupMetadataSharedRules, lGeneration, uCancellationToken, pszStage, uDone, uTotal);
	}

	bool IsFileHashEmpty(const uchar* pucFileHash)
	{
		if (pucFileHash == NULL)
			return true;
		for (UINT i = 0; i < 16; ++i) {
			if (pucFileHash[i] != 0)
				return false;
		}
		return true;
	}

	bool SameHash(const uchar* left, const uchar* right)
	{
		return left != NULL && right != NULL && memcmp(left, right, 16) == 0;
	}

	CString StripExtendedPathPrefix(const CString& strPath)
	{
		if (strPath.GetLength() >= 8 && strPath.Left(8).CompareNoCase(_T("\\\\?\\UNC\\")) == 0)
			return CString(_T("\\\\")) + strPath.Mid(8);
		if (strPath.GetLength() >= 4 && strPath.Left(4).CompareNoCase(_T("\\\\?\\")) == 0)
			return strPath.Mid(4);
		return strPath;
	}

	void TrimTrailingPathSeparators(CString& strPath)
	{
		while (strPath.GetLength() > 0) {
			const int iLen = strPath.GetLength();
			const TCHAR chLast = strPath[iLen - 1];
			if (chLast != _T('\\') && chLast != _T('/'))
				break;
			if (iLen == 3 && strPath[1] == _T(':'))
				break;
			if (iLen <= 2 && strPath.Left(2) == _T("\\\\"))
				break;
			strPath.Truncate(iLen - 1);
		}
	}

	CString BuildCanonicalPathKey(const CString& strFilePath)
	{
		CString strPath(strFilePath);
		strPath.Replace(_T('/'), _T('\\'));
		strPath = StripExtendedPathPrefix(strPath);

		std::vector<TCHAR> fullPathBuffer(MAX_PATH);
		for (;;) {
			DWORD dwRequired = ::GetFullPathName(strPath, static_cast<DWORD>(fullPathBuffer.size()), &fullPathBuffer[0], NULL);
			if (dwRequired == 0)
				break;
			if (dwRequired < fullPathBuffer.size()) {
				strPath = &fullPathBuffer[0];
				break;
			}
			if (dwRequired > kMaxPathChars)
				break;
			fullPathBuffer.resize(dwRequired + 1);
		}

		strPath.Replace(_T('/'), _T('\\'));
		strPath = StripExtendedPathPrefix(strPath);
		TrimTrailingPathSeparators(strPath);
		strPath.MakeLower();
		return strPath;
	}
}

CSharedCache::SPathRecord::SPathRecord()
	: tUtcFileDate(static_cast<time_t>(-1))
	, uFileSize(0)
{
	md4clr(aucFileHash);
}

CSharedCache::CSharedCache()
	: m_lSaveGeneration(0)
	, m_bReplacingDuplicateRecords(false)
{
}

bool CSharedCache::CStringNoCaseLess::operator()(const CString& left, const CString& right) const
{
	return left.CompareNoCase(right) < 0;
}

CString CSharedCache::BuildPathKey(const CString& strFilePath)
{
	return BuildCanonicalPathKey(strFilePath);
}

CString CSharedCache::BuildCachePath(const CString& strConfigDir, LPCTSTR pszFileName)
{
	CString strPath(strConfigDir);
	if (!strPath.IsEmpty()) {
		const TCHAR chLast = strPath[strPath.GetLength() - 1];
		if (chLast != _T('\\') && chLast != _T('/'))
			strPath += _T('\\');
	}
	strPath += pszFileName;
	return strPath;
}

CString CSharedCache::GetLeafName(const CString& strPath)
{
	if (strPath.IsEmpty())
		return CString();

	int iEnd = strPath.GetLength();
	while (iEnd > 0 && (strPath[iEnd - 1] == _T('\\') || strPath[iEnd - 1] == _T('/')))
		--iEnd;

	int iLeafStart = iEnd;
	while (iLeafStart > 0 && strPath[iLeafStart - 1] != _T('\\') && strPath[iLeafStart - 1] != _T('/'))
		--iLeafStart;

	return strPath.Mid(iLeafStart, iEnd - iLeafStart);
}

bool CSharedCache::MatchesPrefixNoCase(const CString& strValue, LPCTSTR pszPrefix)
{
	const CString strPrefix(pszPrefix);
	return strValue.GetLength() >= strPrefix.GetLength() && strValue.Left(strPrefix.GetLength()).CompareNoCase(strPrefix) == 0;
}

bool CSharedCache::MatchesSuffixNoCase(const CString& strValue, LPCTSTR pszSuffix)
{
	const CString strSuffix(pszSuffix);
	return strValue.GetLength() >= strSuffix.GetLength() && strValue.Right(strSuffix.GetLength()).CompareNoCase(strSuffix) == 0;
}

bool CSharedCache::MatchesAffixNoCase(const CString& strValue, LPCTSTR pszPrefix, LPCTSTR pszSuffix)
{
	const CString strPrefix(pszPrefix);
	const CString strSuffix(pszSuffix);
	return strValue.GetLength() >= strPrefix.GetLength() + strSuffix.GetLength() && MatchesPrefixNoCase(strValue, pszPrefix) && MatchesSuffixNoCase(strValue, pszSuffix);
}

bool CSharedCache::ShouldIgnoreFileName(const CString& strFileName)
{
	if (strFileName.IsEmpty())
		return false;

	static const LPCTSTR s_apszExactNames[] = {
		_T("ehthumbs.db"),
		_T("desktop.ini"),
		_T(".ds_store"),
		_T(".localized"),
		_T("Icon\r"),
		_T(".directory")
	};
	static const LPCTSTR s_apszPrefixes[] = {
		_T("._"),
		_T("~$"),
		_T(".nfs"),
		_T(".sb-"),
		_T(".syncthing.")
	};
	static const LPCTSTR s_apszSuffixes[] = {
		_T(".part"),
		_T(".crdownload"),
		_T(".download"),
		_T(".tmp"),
		_T(".temp"),
		_T("~")
	};

	for (size_t i = 0; i < _countof(s_apszExactNames); ++i) {
		if (strFileName.CompareNoCase(s_apszExactNames[i]) == 0)
			return true;
	}
	for (size_t i = 0; i < _countof(s_apszPrefixes); ++i) {
		if (MatchesPrefixNoCase(strFileName, s_apszPrefixes[i]))
			return true;
	}
	for (size_t i = 0; i < _countof(s_apszSuffixes); ++i) {
		if (MatchesSuffixNoCase(strFileName, s_apszSuffixes[i]))
			return true;
	}
	return MatchesAffixNoCase(strFileName, _T("~lock."), _T("#"));
}

bool CSharedCache::ShouldIgnoreDirectoryName(const CString& strDirectoryName)
{
	if (strDirectoryName.IsEmpty())
		return false;

	static const LPCTSTR s_apszExactNames[] = {
		_T(".fseventsd"),
		_T(".spotlight-v100"),
		_T(".temporaryitems"),
		_T(".trashes"),
		_T(".git"),
		_T(".svn"),
		_T(".hg"),
		_T("CVS")
	};
	static const LPCTSTR s_apszPrefixes[] = {
		_T("._"),
		_T(".nfs"),
		_T(".sb-"),
		_T(".syncthing.")
	};

	for (size_t i = 0; i < _countof(s_apszExactNames); ++i) {
		if (strDirectoryName.CompareNoCase(s_apszExactNames[i]) == 0)
			return true;
	}
	for (size_t i = 0; i < _countof(s_apszPrefixes); ++i) {
		if (MatchesPrefixNoCase(strDirectoryName, s_apszPrefixes[i]))
			return true;
	}
	return false;
}

bool CSharedCache::ReadString(CFile& file, CString& strValue)
{
	DWORD dwChars = 0;
	if (!ReadValue(file, dwChars) || dwChars > kMaxPathChars)
		return false;

	strValue.Empty();
	if (dwChars == 0)
		return true;

	LPTSTR pszValue = strValue.GetBuffer(static_cast<int>(dwChars));
	const UINT uBytes = static_cast<UINT>(dwChars * sizeof(TCHAR));
	const bool bRead = file.Read(pszValue, uBytes) == uBytes;
	strValue.ReleaseBuffer(bRead ? static_cast<int>(dwChars) : 0);
	return bRead;
}

bool CSharedCache::ReadRecord(CFile& file, SPathRecord& record)
{
	if (!ReadString(file, record.strFilePath))
		return false;

	__int64 iFileDate = -1;
	if (!ReadValue(file, iFileDate) || !ReadValue(file, record.uFileSize) || file.Read(record.aucFileHash, static_cast<UINT>(sizeof(record.aucFileHash))) != static_cast<UINT>(sizeof(record.aucFileHash)))
		return false;

	record.tUtcFileDate = static_cast<time_t>(iFileDate);
	return true;
}

bool CSharedCache::LoadRecordBlock(CFile& file, TPathRecordMap& records, DWORD dwCount, DWORD& dwRecordsRead, DWORD dwTotalRecords, LONG lGeneration, uint64 uCancellationToken)
{
	for (DWORD i = 0; i < dwCount; ++i) {
		if ((dwRecordsRead & 0x3FF) == 0) {
			if (lGeneration != 0 && uCancellationToken != 0 && theApp.IsStartupMetadataLoadCancelled(CemuleApp::StartupMetadataSharedRules, lGeneration, uCancellationToken)) {
				records.clear();
				return false;
			}
			const UINT uProgress = dwTotalRecords != 0 ? static_cast<UINT>((static_cast<uint64>(dwRecordsRead) * 1000) / dwTotalRecords) : 1000;
			PublishSharedCacheLoadProgress(lGeneration, uCancellationToken, _T("read-shared-cache"), min(1000U, uProgress), 1000);
		}

		SPathRecord record;
		if (!ReadRecord(file, record))
			return false;
		if (!record.strFilePath.IsEmpty() && record.tUtcFileDate > 0 && record.uFileSize > 0 && !IsFileHashEmpty(record.aucFileHash))
			records[BuildPathKey(record.strFilePath)] = record;
		++dwRecordsRead;
	}
	return true;
}

bool CSharedCache::LoadCombinedRecords(const CString& strFilePath, TPathRecordMap& sharedRecords, TPathRecordMap& duplicateRecords, LONG lGeneration, uint64 uCancellationToken)
{
	sharedRecords.clear();
	duplicateRecords.clear();
	const DWORD dwAttributes = ::GetFileAttributes(PreparePathForWin32LongPath(strFilePath));
	if (dwAttributes == INVALID_FILE_ATTRIBUTES) {
		const DWORD dwError = ::GetLastError();
		return dwError == ERROR_FILE_NOT_FOUND || dwError == ERROR_PATH_NOT_FOUND;
	}

	CFile file;
	CFileException ex;
	if (!file.Open(PreparePathForWin32LongPath(strFilePath), CFile::modeRead | CFile::shareDenyWrite | CFile::typeBinary, &ex))
		return false;

	try {
		DWORD dwReadMagic = 0;
		WORD wReadVersion = 0;
		WORD wReadFlags = 0;
		DWORD dwSharedCount = 0;
		DWORD dwDuplicateCount = 0;
		if (!ReadValue(file, dwReadMagic) || !ReadValue(file, wReadVersion) || !ReadValue(file, wReadFlags) || !ReadValue(file, dwSharedCount) || !ReadValue(file, dwDuplicateCount))
			return false;
		if (dwReadMagic != kSharedCacheMagic || wReadVersion != kSharedCacheVersion || wReadFlags != kSharedCacheFlags || dwSharedCount > kMaxPathRecords || dwDuplicateCount > kMaxPathRecords)
			return false;

		const DWORD dwTotalRecords = dwSharedCount + dwDuplicateCount;
		if (dwTotalRecords < dwSharedCount || dwTotalRecords > kMaxTotalPathRecords)
			return false;

		const ULONGLONG uFileLength = file.GetLength();
		if (uFileLength < kSerializedHeaderBytes || static_cast<ULONGLONG>(dwTotalRecords) > (uFileLength - kSerializedHeaderBytes) / kMinSerializedRecordBytes)
			return false;

		PublishSharedCacheLoadProgress(lGeneration, uCancellationToken, _T("read-shared-cache"), 0, 1000);
		DWORD dwRecordsRead = 0;
		if (!LoadRecordBlock(file, sharedRecords, dwSharedCount, dwRecordsRead, dwTotalRecords, lGeneration, uCancellationToken)) {
			sharedRecords.clear();
			duplicateRecords.clear();
			return false;
		}
		if (!LoadRecordBlock(file, duplicateRecords, dwDuplicateCount, dwRecordsRead, dwTotalRecords, lGeneration, uCancellationToken)) {
			sharedRecords.clear();
			duplicateRecords.clear();
			return false;
		}
		if (file.GetPosition() != uFileLength) {
			sharedRecords.clear();
			duplicateRecords.clear();
			return false;
		}

		PublishSharedCacheLoadProgress(lGeneration, uCancellationToken, _T("read-shared-cache"), 1000, 1000);
		return true;
	} catch (...) {
		sharedRecords.clear();
		duplicateRecords.clear();
		return false;
	}
}

void CSharedCache::AppendRecordToChunks(std::vector<std::vector<BYTE> >& chunks, const SPathRecord& record)
{
	AppendStringToChunks(chunks, record.strFilePath);
	const __int64 iFileDate = static_cast<__int64>(record.tUtcFileDate);
	AppendValueToChunks(chunks, iFileDate);
	AppendValueToChunks(chunks, record.uFileSize);
	AppendBytesToChunks(chunks, record.aucFileHash, sizeof(record.aucFileHash));
}

void CSharedCache::QueueSaveCombinedRecords(const CString& strFilePath, const TPathRecordMap& sharedRecords, const TPathRecordMap& duplicateRecords, volatile LONG& lSaveGeneration)
{
	AsyncDiskWriteData* pData = new AsyncDiskWriteData;
	if (pData == NULL)
		return;

	pData->lGeneration = ::InterlockedIncrement(&lSaveGeneration);
	pData->plGeneration = &lSaveGeneration;
	pData->strTempPath = strFilePath + _T(".tmp");
	pData->strFinalPath = strFilePath;
	pData->strLogName = GetLeafName(strFilePath);
	pData->strPayloadName = _T("shared-cache");
	pData->eConflictPolicy = AsyncDiskWriteConflictLastSnapshotWins;
	pData->eReplacePolicy = AsyncDiskWriteReplaceFinal;

	AppendValueToChunks(pData->chunks, kSharedCacheMagic);
	AppendValueToChunks(pData->chunks, kSharedCacheVersion);
	AppendValueToChunks(pData->chunks, kSharedCacheFlags);
	const DWORD dwSharedCount = static_cast<DWORD>(min(sharedRecords.size(), static_cast<size_t>(kMaxPathRecords)));
	const DWORD dwDuplicateCount = static_cast<DWORD>(min(duplicateRecords.size(), static_cast<size_t>(kMaxPathRecords)));
	AppendValueToChunks(pData->chunks, dwSharedCount);
	AppendValueToChunks(pData->chunks, dwDuplicateCount);

	DWORD dwWritten = 0;
	for (TPathRecordMap::const_iterator it = sharedRecords.begin(); it != sharedRecords.end() && dwWritten < dwSharedCount; ++it, ++dwWritten)
		AppendRecordToChunks(pData->chunks, it->second);

	dwWritten = 0;
	for (TPathRecordMap::const_iterator it = duplicateRecords.begin(); it != duplicateRecords.end() && dwWritten < dwDuplicateCount; ++it, ++dwWritten)
		AppendRecordToChunks(pData->chunks, it->second);

	CPartFileWriteThread::QueueOrWriteDiskSnapshot(pData);
}

bool CSharedCache::IsSameFileIdentity(const SPathRecord& record, time_t tUtcFileDate, uint64 uFileSize)
{
	return IsFileDateEqual(record.tUtcFileDate, tUtcFileDate) && record.uFileSize == uFileSize;
}

bool CSharedCache::Load(const CString& strConfigDir, LONG lGeneration, uint64 uCancellationToken)
{
	TPathRecordMap sharedRecords;
	TPathRecordMap duplicateRecords;
	const bool bLoaded = LoadCombinedRecords(BuildCachePath(strConfigDir, kSharedCacheFileName), sharedRecords, duplicateRecords, lGeneration, uCancellationToken);
	if (lGeneration != 0 && uCancellationToken != 0 && theApp.IsStartupMetadataLoadCancelled(CemuleApp::StartupMetadataSharedRules, lGeneration, uCancellationToken))
		return false;

	CSingleLock lock(&m_lock, TRUE);
	m_sharedRecords.swap(sharedRecords);
	m_duplicateRecords.swap(duplicateRecords);
	PublishSharedCacheLoadProgress(lGeneration, uCancellationToken, _T("read-shared-cache"), 1000, 1000);
	return bLoaded;
}

void CSharedCache::Save(const CString& strConfigDir) const
{
	TPathRecordMap sharedRecords;
	TPathRecordMap duplicateRecords;
	{
		CSingleLock lock(&m_lock, TRUE);
		sharedRecords = m_sharedRecords;
		duplicateRecords = m_duplicateRecords;
	}
	QueueSaveCombinedRecords(BuildCachePath(strConfigDir, kSharedCacheFileName), sharedRecords, duplicateRecords, m_lSaveGeneration);
}

void CSharedCache::ClearSharedRecords()
{
	CSingleLock lock(&m_lock, TRUE);
	m_sharedRecords.clear();
}

void CSharedCache::ReplaceSharedRecords(const std::vector<SSharedFileRecord>& records)
{
	TPathRecordMap sharedRecords;
	for (std::vector<SSharedFileRecord>::const_iterator it = records.begin(); it != records.end(); ++it) {
		SPathRecord record;
		record.strFilePath = it->strFilePath;
		record.tUtcFileDate = it->tUtcFileDate;
		record.uFileSize = it->uFileSize;
		md4cpy(record.aucFileHash, it->aucFileHash);
		if (!record.strFilePath.IsEmpty() && record.tUtcFileDate > 0 && record.uFileSize > 0 && !IsFileHashEmpty(record.aucFileHash))
			sharedRecords[BuildPathKey(record.strFilePath)] = record;
	}

	CSingleLock lock(&m_lock, TRUE);
	m_sharedRecords.swap(sharedRecords);
}

void CSharedCache::BeginReplaceSharedRecords()
{
	CSingleLock lock(&m_lock, TRUE);
	m_pendingSharedRecords.clear();
}

void CSharedCache::AppendReplacementSharedRecords(const std::vector<SSharedFileRecord>& records)
{
	TPathRecordMap sharedRecords;
	for (std::vector<SSharedFileRecord>::const_iterator it = records.begin(); it != records.end(); ++it) {
		SPathRecord record;
		record.strFilePath = it->strFilePath;
		record.tUtcFileDate = it->tUtcFileDate;
		record.uFileSize = it->uFileSize;
		md4cpy(record.aucFileHash, it->aucFileHash);
		if (!record.strFilePath.IsEmpty() && record.tUtcFileDate > 0 && record.uFileSize > 0 && !IsFileHashEmpty(record.aucFileHash))
			sharedRecords[BuildPathKey(record.strFilePath)] = record;
	}

	if (sharedRecords.empty())
		return;

	CSingleLock lock(&m_lock, TRUE);
	for (TPathRecordMap::const_iterator it = sharedRecords.begin(); it != sharedRecords.end(); ++it)
		m_pendingSharedRecords[it->first] = it->second;
}

void CSharedCache::CommitReplaceSharedRecords()
{
	CSingleLock lock(&m_lock, TRUE);
	m_sharedRecords.swap(m_pendingSharedRecords);
	m_pendingSharedRecords.clear();
}

void CSharedCache::CancelReplaceSharedRecords()
{
	CSingleLock lock(&m_lock, TRUE);
	m_pendingSharedRecords.clear();
}

void CSharedCache::BeginReplaceDuplicateRecords()
{
	CSingleLock lock(&m_lock, TRUE);
	m_pendingDuplicateRecords.clear();
	m_bReplacingDuplicateRecords = true;
}

bool CSharedCache::CommitReplaceDuplicateRecords()
{
	CSingleLock lock(&m_lock, TRUE);
	if (!m_bReplacingDuplicateRecords)
		return false;
	m_duplicateRecords.swap(m_pendingDuplicateRecords);
	m_pendingDuplicateRecords.clear();
	m_bReplacingDuplicateRecords = false;
	return true;
}

void CSharedCache::CancelReplaceDuplicateRecords()
{
	CSingleLock lock(&m_lock, TRUE);
	m_pendingDuplicateRecords.clear();
	m_bReplacingDuplicateRecords = false;
}

void CSharedCache::RememberSharedFile(const CKnownFile* pFile)
{
	if (pFile == NULL || pFile->IsPartFile())
		return;

	SPathRecord record;
	record.strFilePath = pFile->GetFilePath();
	record.tUtcFileDate = pFile->GetUtcFileDate();
	record.uFileSize = static_cast<uint64>(pFile->GetFileSize());
	md4cpy(record.aucFileHash, pFile->GetFileHash());
	if (record.strFilePath.IsEmpty() || record.tUtcFileDate <= 0 || record.uFileSize == 0 || IsFileHashEmpty(record.aucFileHash))
		return;

	CSingleLock lock(&m_lock, TRUE);
	m_sharedRecords[BuildPathKey(record.strFilePath)] = record;
}

void CSharedCache::RememberDuplicatePath(const CString& strFilePath, const uchar* pucFileHash, time_t tUtcFileDate, uint64 uFileSize)
{
	if (strFilePath.IsEmpty() || tUtcFileDate <= 0 || uFileSize == 0 || IsFileHashEmpty(pucFileHash))
		return;

	SPathRecord record;
	record.strFilePath = strFilePath;
	record.tUtcFileDate = tUtcFileDate;
	record.uFileSize = uFileSize;
	md4cpy(record.aucFileHash, pucFileHash);

	CSingleLock lock(&m_lock, TRUE);
	if (m_bReplacingDuplicateRecords)
		m_pendingDuplicateRecords[BuildPathKey(strFilePath)] = record;
	else
		m_duplicateRecords[BuildPathKey(strFilePath)] = record;
}

CKnownFile* CSharedCache::FindKnownFileByPath(const CString& strFilePath, time_t tUtcFileDate, uint64 uFileSize) const
{
	SPathRecord record;
	{
		CSingleLock lock(&m_lock, TRUE);
		TPathRecordMap::const_iterator it = m_sharedRecords.find(BuildPathKey(strFilePath));
		if (it == m_sharedRecords.end())
			return NULL;
		record = it->second;
	}

	if (!IsSameFileIdentity(record, tUtcFileDate, uFileSize) || theApp.knownfiles == NULL)
		return NULL;

	CKnownFile* pKnownFile = theApp.knownfiles->FindKnownFileForSharedScan(GetLeafName(record.strFilePath), tUtcFileDate, uFileSize);
	if (pKnownFile == NULL || pKnownFile->IsPartFile())
		return NULL;
	if (static_cast<uint64>(pKnownFile->GetFileSize()) != uFileSize || !IsFileDateEqual(pKnownFile->GetUtcFileDate(), tUtcFileDate) || !SameHash(pKnownFile->GetFileHash(), record.aucFileHash))
		return NULL;
	return pKnownFile;
}

bool CSharedCache::IsDuplicatePath(const CString& strFilePath, time_t tUtcFileDate, uint64 uFileSize, const uchar* pucFileHash) const
{
	if (IsFileHashEmpty(pucFileHash))
		return false;

	SPathRecord activeRecord;
	SPathRecord pendingRecord;
	bool bFoundActive = false;
	bool bFoundPending = false;
	const CString strPathKey(BuildPathKey(strFilePath));
	{
		CSingleLock lock(&m_lock, TRUE);
		TPathRecordMap::const_iterator it = m_duplicateRecords.find(strPathKey);
		if (it != m_duplicateRecords.end()) {
			activeRecord = it->second;
			bFoundActive = true;
		}
		if (m_bReplacingDuplicateRecords) {
			it = m_pendingDuplicateRecords.find(strPathKey);
			if (it != m_pendingDuplicateRecords.end()) {
				pendingRecord = it->second;
				bFoundPending = true;
			}
		}
	}
	return (bFoundActive && IsSameFileIdentity(activeRecord, tUtcFileDate, uFileSize) && SameHash(activeRecord.aucFileHash, pucFileHash))
		|| (bFoundPending && IsSameFileIdentity(pendingRecord, tUtcFileDate, uFileSize) && SameHash(pendingRecord.aucFileHash, pucFileHash));
}
