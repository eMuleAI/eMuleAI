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
#include <map>
#include "MapKey.h"
#include "SHAHashset.h"
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class CKnownFile;
class CSafeMemFile;
typedef CMap<CCKey, const CCKey&, CKnownFile*, CKnownFile*> CKnownFilesMap;
typedef CMap<CSKey, const CSKey&, int, int> CancelledFilesMap;
typedef CMap<CAICHHash, const CAICHHash&, const CKnownFile*, const CKnownFile*> KnonwFilesByAICHMap;
typedef CMap<CSKey, const CSKey&, uint32, uint32> DuplicateHashCountMap;
typedef std::unordered_multimap<uint64, CKnownFile*> SizeIndexMap;
typedef std::unordered_multimap<uint64, CKnownFile*> KnownFileIdentityIndexMap;
typedef std::unordered_set<uint64> KnownFileIdentityIndexedSizeSet;
typedef std::vector<CKnownFile*> CStartupKnownFilesRecords;
typedef std::vector<CSKey> CStartupCancelledFilesRecords;

class CKnownFileList
{
	friend class CFileDetailDlgStatistics;
	friend class CStatisticFile;
	friend class CSharedFilesCtrl;

	SizeIndexMap m_sizeIndex;
	SizeIndexMap m_dupFileSizeIndex;
	KnownFileIdentityIndexMap m_identityIndex;
	KnownFileIdentityIndexMap m_dupFileIdentityIndex;
	KnownFileIdentityIndexedSizeSet m_identityIndexedSizes;
	KnownFileIdentityIndexedSizeSet m_dupIdentityIndexedSizes;
	DuplicateHashCountMap m_dupHashCounts;

public:
	CKnownFileList(bool bLoadImmediately = true);
	~CKnownFileList();

	bool	SafeAddKFile(CKnownFile *toadd, bool bUpdateDownloadValidator = true, bool bLogDuplicateDetails = true);
	bool	Init();
	bool	LoadStartupKnownFilesRecords(CStartupKnownFilesRecords& knownRecords, CStartupCancelledFilesRecords& cancelledRecords, uint32& dwCancelledFilesSeed, LONG lGeneration = 0, uint64 uCancellationToken = 0);
	bool	LoadStartupKnownFilesForWorker(LONG lGeneration = 0, uint64 uCancellationToken = 0);
	bool	ParseStartupKnownFilesLoadChunk(CStartupKnownFilesRecords* pKnownRecords, std::vector<CKnownFile*>& parsedFiles, std::vector<uint32>* pWorkUnits, uint64* puTotalWorkUnits, size_t& uNextRecord, size_t uMaxRecords);
	bool	AttachStartupKnownFilesLoadChunk(std::vector<CKnownFile*>& parsedFiles, size_t& uNextParsedFile, size_t uMaxFiles, const std::vector<uint32>* pWorkUnits = NULL, uint64* puAppliedWorkUnits = NULL);
	bool	ApplyStartupKnownFilesCompletionChunk(CStartupCancelledFilesRecords* pCancelledRecords, uint32 dwCancelledFilesSeed, bool& bStarted, size_t& uNextCancelledRecord, size_t uMaxRecords, UINT& uApplied, INT_PTR& iRemaining);
	void	CompleteStartupKnownFilesLoadApply(CStartupCancelledFilesRecords* pCancelledRecords, uint32 dwCancelledFilesSeed);
	static void	DeleteStartupKnownFilesRecords(CStartupKnownFilesRecords* pKnownRecords, CStartupCancelledFilesRecords* pCancelledRecords);
	static void	DeleteStartupKnownFilesParsedFiles(std::vector<CKnownFile*>& parsedFiles);
	void	Save();
	bool	ProcessKnownMetSaveJob();
	void	DeferKnownMetSaveJob();
	bool	HasKnownMetSaveJobPending() const { return m_pKnownMetSaveJob != NULL; }
	LONG	NextKnownMetSaveGeneration() { return InterlockedIncrement(&m_lKnownMetSaveGeneration); }
	LONG	GetKnownMetSaveGeneration() const { return InterlockedCompareExchange(const_cast<volatile LONG*>(&m_lKnownMetSaveGeneration), 0, 0); }
	void	Clear();
	void	Process();

	CKnownFile* FindKnownFile(LPCTSTR filename, time_t date, uint64 size);
	CKnownFile* FindKnownFileForSharedScan(LPCTSTR filename, time_t date, uint64 size);
	CKnownFile* FindKnownFileByID(const uchar* hash) const;
	CKnownFile* FindKnownFileByPath(const CString& sFilePath) const;
	bool	IsKnownFile(const CKnownFile *file) const;
	void	ReindexKnownFile(CKnownFile* file, LPCTSTR oldFileName, uint64 oldSize);
	bool	IsFilePtrInList(const CKnownFile *file) const;

	void	AddCancelledFileID(const uchar *hash);
	bool	IsCancelledFileByID(const uchar* hash) const;

	const CKnownFilesMap &GetKnownFiles() const		{ return m_Files_map; }
	void	CopyKnownFileMap(CKnownFilesMap &Files_Map);

	bool	ShouldPurgeAICHHashset(const CAICHHash &rAICHHash) const;
	void	AICHHashChanged(const CAICHHash *pOldAICHHash, const CAICHHash &rNewAICHHash, CKnownFile *pFile);

	uint64	m_nTransferredTotal;
	uint32	m_nRequestedTotal;
	uint32	m_nAcceptedTotal;

	bool	RemoveKnownFile(CKnownFile* toRemove, bool bNotifySharedFilesList = true);
	int		GetCount() { return m_Files_map.GetCount(); }
	void	ClearHistory();

	uint32	DuplicatesCount(const uchar* hash);
	void	CollectDuplicateFilePathsByIdentity(const uchar* hash, LPCTSTR filename, uint64 size, std::vector<CString>& filePaths);
	CKnownFile*	PromoteDuplicateForSharedFile(CKnownFile* pOldPrimary);
	CKnownFile*	IsOnDuplicatesForSharedScan(const LPCTSTR filename, time_t in_date, uint64 in_size);
	CKnownFile*	IsOnDuplicates(const LPCTSTR filename, time_t in_date, uint64 in_size);
	bool	IsDuplicatePathForSharedScan(LPCTSTR pszFileName, time_t tUtcFileDate, uint64 uFileSize, LPCTSTR pszPathKey);
	void PurgeDuplicateFile(CKnownFile* file);

	typedef			 std::list<CKnownFile*> KnownFileList;
	KnownFileList	 m_duplicateFileList;
	CCriticalSection m_csDuplicatesLock;

	CCriticalSection m_csSizeIndexLock;

	CKnownFilesMap		m_Files_map; // Moved to public
private:
	bool	LoadKnownFiles();
	bool	LoadCancelledFiles();
	uint64	transferred;
	CancelledFilesMap	m_mapCancelledFiles;
	// map of files is indexed by AICH-hash for faster access,
	// not guaranteed to be complete at this point (!)
	// (files which got AICH hashed later will not be added yet, because we don't need them,
	// make sure to change this if needed)
	KnonwFilesByAICHMap m_mapKnownFilesByAICH;
	uint32	m_dwCancelledFilesSeed;
	DWORD	m_nLastSaved;
	uint16	requested;
	uint16	accepted;
	volatile LONG m_lKnownMetSaveGeneration;
	volatile LONG m_lCancelledMetSaveGeneration;

	struct SKnownMetFileIdentity
	{
		SKnownMetFileIdentity();

		CSKey m_hash;
		CString m_strFileName;
		time_t m_tUtcFileDate;
		uint64 m_uFileSize;
	};

	struct SKnownMetSaveJob
	{
		SKnownMetSaveJob();

		LONG m_lGeneration;
		CString m_strConfDir;
		std::vector<CSKey> m_vecKnownHashes;
		std::vector<SKnownMetFileIdentity> m_vecDuplicateFiles;
		size_t m_uNextKnownIndex;
		size_t m_uNextDuplicateIndex;
		INT_PTR m_iRecordsNumber;
		DWORD m_dwStartedTick;
		DWORD m_dwLastProgressTick;
		std::vector<BYTE> m_vecHeader;
		std::vector<std::vector<BYTE> > m_vecChunks;
	};

	SKnownMetSaveJob* m_pKnownMetSaveJob;
	bool m_bKnownMetSaveRerunRequested;
	bool m_bKnownMetSaveEventPending;
	bool m_bClearingKnownFiles;

	LONG NextCancelledMetSaveGeneration() { return InterlockedIncrement(&m_lCancelledMetSaveGeneration); }
	void StartKnownMetSaveJob();
	void QueueKnownMetSaveSlice();
	void ClearKnownMetSaveJob();
	bool WriteKnownMetRecord(const CSKey& hash, CSafeMemFile& chunkData, INT_PTR& iRecordsNumber);
	bool WriteKnownMetDuplicateRecord(const SKnownMetFileIdentity& identity, CSafeMemFile& chunkData, INT_PTR& iRecordsNumber);
	bool FinishKnownMetSaveJob();
	void SaveCancelledFiles();

	uint64 BuildIdentityIndexKey(LPCTSTR filename, uint64 size) const;
	void PrepareKnownFileLoadCapacity(uint32 uRecordsNumber);
	void AddSizeIndex(CKnownFile* file);
	void RemoveSizeIndex(CKnownFile* file);
	bool KnownFileMatches(const CKnownFile* file, LPCTSTR filename, time_t date, uint64 size) const;
	CKnownFile* FindKnownFileInIndex(const KnownFileIdentityIndexMap& index, LPCTSTR filename, time_t date, uint64 size) const;
	CKnownFile* FindKnownFileInSizeIndex(const SizeIndexMap& index, LPCTSTR filename, time_t date, uint64 size) const;
	CKnownFile* FindKnownFileInSizeIndexLimited(const SizeIndexMap& index, LPCTSTR filename, time_t date, uint64 size, size_t uMaxCandidates) const;
	void BuildIdentityIndexForSizeLocked(uint64 size);
	void BuildDuplicateIdentityIndexForSize(uint64 size);

	void AddDupSizeIndex(CKnownFile* file);
	void RemoveDupSizeIndex(CKnownFile* file);
	void IncrementDuplicateHashCount(const uchar* hash);
	void DecrementDuplicateHashCount(const uchar* hash);
};
