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
#include "KnownFile.h"
#include "SearchFile.h"
#include "QArray.h"
#include "Mapkey.h"
#include "SearchParams.h"
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

enum ESearchType : uint8;
struct SFilenameAutoBlacklistSnapshot;


struct SSearchResultId
{
	SSearchResultId();

	void Clear();
	bool IsValid() const;
	void Set(uint32 nSearchID, const uchar *pFileHash, bool bChild = false, LPCTSTR pszFileName = NULL);
	bool Equals(uint32 nSearchID, const uchar *pFileHash) const;
	bool EqualsRow(uint32 nSearchID, const uchar *pFileHash, bool bChild, LPCTSTR pszFileName) const;

	uint32 m_nSearchID;
	uchar m_abyFileHash[MDX_DIGEST_SIZE];
	bool m_bChild;
	CString m_strFileName;
};

typedef struct
{
	CString	m_strFileName;
	CString	m_strFileType;
	CString	m_strFileHash;
	CString	m_strIndex;
	CString	m_strTextColor;
	CString	m_strOverlayImage;
	uint64	m_uFileSize;
	uint32	m_uSourceCount;
	uint32	m_dwCompleteSourceCount;
} SearchFileStruct;

#define WEB_SEARCH_RESULT_SNAPSHOT_MAX	2000

typedef CTypedPtrList<CPtrList, CSearchFile*> SearchList;
typedef CTypedPtrList<CPtrList, CSearchFile*> SearchChildList;

typedef struct
{
	uint32 m_nSearchID;
	SearchList m_listSearchFiles;
	CMap<CSKey, const CSKey&, CSearchFile*, CSearchFile*> m_mapParentsByHash;
	CMapStringToPtr m_mapChildrenByParentAndName;
	CMapPtrToPtr m_mapChildrenByParent;
	LONG m_lDestructiveSequence;
} SearchListsStruct;

typedef struct
{
	uint32	m_nResults;
	uint32	m_nSpamResults;
} UDPServerRecord;



class CFileDataIO;
class CSafeBufferedFile;
class CAbstractFile;
struct SSearchTerm;

class CSearchList
{
	friend class CSearchListCtrl;
	friend class CemuleApp;

public:
	CSearchList();
	~CSearchList();

	void	Clear();
	void	NewSearch(CSearchListCtrl *pWnd, const CString &strResultFileType, SSearchParams *pParams);
	UINT	QueueClientSearchAnswerPacket(const uchar *in_packet, uint32 size, CUpDownClient &sender, LPCTSTR pszDirectory = NULL);
	UINT	QueueClientSearchAnswerPacketSnapshot(const std::vector<BYTE> &packetData, uint32 uSearchID, LONG lSearchGeneration, const CString &strClientHash, const CString &strSenderName,
			uint32 nClientID, uint16 nClientPort, uint32 nClientServerIP, uint16 nClientServerPort, bool bOptUTF8, bool bPreviewSupport, bool bSupportsLargeFiles, LPCTSTR pszDirectory = NULL);
	UINT	QueueServerSearchAnswerPacketSnapshot(const std::vector<BYTE> &packetData, uint32 uSearchID, LONG lSearchGeneration, bool bOptUTF8, uint32 nServerIP, uint16 nServerPort);
	LONG	GetSearchAnswerParseGeneration(uint32 nSearchID);
	UINT	ProcessSearchAnswer(const uchar *in_packet, uint32 size, CUpDownClient &sender, bool *pbMoreResultsAvailable, LPCTSTR pszDirectory = NULL);
	UINT	ProcessSearchAnswer(const uchar *in_packet, uint32 size, bool bOptUTF8, uint32 nServerIP, uint16 nServerPort, bool *pbMoreResultsAvailable);
	UINT	ProcessUDPSearchAnswer(CFileDataIO &packet, bool bOptUTF8, uint32 nServerIP, uint16 nServerPort);
	UINT	GetED2KResultCount() const;
	uint32	GetCurrentED2KSearchID() const		{ return m_nCurED2KSearchID; }
	UINT	GetResultCount(uint32 nSearchID) const;
	void	AddResultCount(uint32 nSearchID, const uchar *hash, UINT nCount, bool bSpam);

	void	SetOutputWnd(CSearchListCtrl *in_wnd)		{ outputwnd = in_wnd; }
	void	RemoveResults(uint32 nSearchID);
	void	RemoveResult(CSearchFile* todel);
	uint32	RemoveCleanUpSearchResults(uint32 nSearchID, bool *pbRemovedAny = NULL);
	void	GetWebList(CQArray<SearchFileStruct, SearchFileStruct> *SearchFileArray, int iSortBy, INT_PTR iMaxRows = WEB_SEARCH_RESULT_SNAPSHOT_MAX) const;
	void	GetWebDownloadLinksByHashes(const CString &strHashes, CString &strLinks) const;

	void	AddFileToDownloadByHash(const uchar *hash)	{ AddFileToDownloadByHash(hash, 0); }
	void	AddFileToDownloadByHash(const uchar *hash, int cat);
	bool	AddToList(CSearchFile* toadd, bool bClientRespons, uint32 dwFromUDPServerIP, bool bDoSpamRating);
	CSearchFile* GetSearchFileByHash(const uchar *hash) const;
	bool	GetSearchResultId(const CSearchFile *pFile, SSearchResultId &id) const;
	CSearchFile* GetSearchFileByResultId(const SSearchResultId &id) const;
	CSearchFile* GetSearchFileByResultRow(const SSearchResultId &id, bool bChild, LPCTSTR pszFileName) const;
	LONG	GetSearchModelSequence() const;
	void	ProcessChunkedSearchIngestJobs();
	void	ProcessNetworkParseCpuWorkerJobs();
	bool	ProcessDownloadValidatorWorkerJobs();
	void	RequestDownloadValidatorRecheckForAllSearches();
	void	RequestDownloadValidatorRecheckIfDirty(uint32 nSearchID);
	void	SetDownloadValidatorPrioritySearch(uint32 nSearchID);
	bool	GetDownloadValidatorSearchProgress(uint32 nSearchID, UINT& uProcessed, UINT& uTotal);
	bool	QueuePossibleKnownQuery(UINT_PTR uParentToken, uint32 nSearchID, const uchar* pHash, const CString& strFileName, const std::vector<CString>& astrFileNames, EMFileSize uFileSize, uint32 uMediaLengthSec, uint32 uAliasFingerprint, bool bLoadRows, uint32 uRevision, uint32 uCandidateDataRevision, bool bReplaceRows, const SDownloadValidatorFuzzyQueryData* pQueryData = NULL);
	bool	BuildPossibleKnownAliasNames(const CSearchFile* pParent, std::vector<CString>& astrFileNames, uint32& uAliasFingerprint);
	void	ClearChunkedSearchIngestJobs();
	void	ShutdownSearchProcessingForLifecycle();
	void	UpdateSearchIngestOutputWndFromUiThread(uint32 nSearchID, const CString &strClientHash, bool bUseKadReloadThrottle);
	void	KademliaSearchKeyword(uint32 nSearchID, const Kademlia::CUInt128 *pFileID, LPCTSTR name, uint64 size, LPCTSTR type, UINT uKadPublishInfo, CArray<CAICHHash> &raAICHHashes, CArray<uint8, uint8> &raAICHHashPopularity, SSearchTerm *pQueriedSearchTerm, UINT numProperties, ...);
	bool	AddNotes(const Kademlia::CEntry &cEntry, const uchar *hash);
	void	SetNotesSearchStatus(const uchar *pFileHash, bool bSearchRunning);
	void	SentUDPRequestNotification(uint32 nSearchID, uint32 dwServerIP);

	void	StoreSearches();
	void	BeginStartupLoad();
	void	CancelStartupLoad();

	struct SSearchIngestRecord
	{
		SSearchIngestRecord();

		std::vector<BYTE> m_data;
		SSearchResultId m_resultId;
		EMFileSize m_uFileSize;
		uint32 m_uMediaLengthSec;
		uint32 m_nSearchID;
		uint32 m_nServerIP;
		uint16 m_nServerPort;
		UINT m_uServerAvail;
		UINT m_uKadPublishInfo;
		bool m_bKademlia;
		bool m_bServerUDPAnswer;
		bool m_bPreviewPossible;
		bool m_bMultipleAICHFound;
		bool m_bAutomaticBlacklistEvaluated;
		bool m_bAutomaticBlacklisted;
		bool m_bDownloadValidatorEvaluated;
		bool m_bDownloadValidatorSimilar;
		uint32 m_uDownloadValidatorRevision;
	};

	struct SStartupStoredSearchTab
	{
		SStartupStoredSearchTab();

		SSearchParams *pParams;
		std::vector<SSearchIngestRecord> vecFiles;
		uint32 uStoredFileCount;
		uint32 nAssignedSearchID;
		bool bTabCreated;
		bool bDeleteParams;
		bool bLastTab;
		bool bIngestQueued;
		uint32 uQueuedFileCount;
	};

	struct SStartupStoredSearchesLoadResult
	{
		SStartupStoredSearchesLoadResult();

		std::vector<SStartupStoredSearchTab*> vecTabs;
		size_t uNextTab;
		size_t uNextFile;
		UINT uTotalSearches;
		UINT uTotalFiles;
		LONG lGeneration;
		uint64 uCancellationToken;
		bool bSuccess;
		bool bApplyStarted;
		DWORD dwLastError;
		CString strStage;
	};

	bool	LoadStartupStoredSearchesForWorker(SStartupStoredSearchesLoadResult &result);
	bool	ApplyStartupStoredSearchesLoadResult(SStartupStoredSearchesLoadResult *pResult, size_t uMaxFilesPerSlice, UINT& uAppliedInSlice, INT_PTR& iRemaining);
	static void	DeleteStartupStoredSearchesLoadResult(SStartupStoredSearchesLoadResult *pResult);
	bool	IsStartupLoadActive() const					{ return m_bStoredSearchStartupLoadActive; }
	bool	IsStartupLoadActiveForSearch(uint32 nSearchID) const;
	bool	IsStartupLoadCompleted() const				{ return m_bStoredSearchStartupLoadCompleted; }
	void	GetStartupLoadProgress(UINT& uLoadedSearches, UINT& uTotalSearches, UINT& uLoadedFiles) const;
	void	PublishStartupLoadWorkerProgress(UINT uLoadedSearches, UINT uTotalSearches, UINT uLoadedFiles);
	bool	HasPendingSearchProcessing(uint32 nSearchID);
	bool	HasPendingSearchProcessing() const;
	bool	HasPendingPossibleKnownPreparation(uint32 nSearchID);

	enum EActionType
	{
		Calculate,
		MarkAsSpam,
		MarkAsNotSpam,
		MarkAsBlacklisted,
		MarkAsNotBlacklisted,
	};

	bool	DoSpamRating(CSearchFile* pSearchFile, bool bIsClientFile, uint8 uActionType, bool bUpdate, uint32 dwFromUDPServerIP);
	void	MarkHashAsBlacklisted(CSKey hash);
	bool	IsFilenameManualBlacklisted(CSKey hash);
	static bool	IsFilenameAutoBlacklisted(CString strFilename);
	void	MarkFileAsNotSpam(CSearchFile *pSpamFile) {	DoSpamRating(pSpamFile, false, MarkAsSpam, true, 0); }
	void	RecalculateSpamRatings(uint32 nSearchID, bool bExpectHigher, bool bExpectLower, bool bRecalculateAll);
	bool	HasPendingSpamRatingRecheck(uint32 nSearchID);

	void	SetSearchItemKnownType(CSearchFile* src);
	bool	RefreshSearchResultKnownType(const SSearchResultId& id);
	bool	QueueKnownTypeRefreshForHash(const uchar* pFileHash);
	bool	RefreshKnownTypesForAllSearches();
	bool	QueueKnownTypeRefreshForAllSearches(bool bStartupRefresh = false);
	bool	HasKnownTypeRefreshWork() const;
	bool	ProcessKnownTypeRefreshWork(DWORD dwSliceStart);

	void	SaveSpamFilter();

	UINT	GetFoundFiles(uint32 nSearchID) const
	{
		UINT returnVal;
		return m_foundFilesCount.Lookup(nSearchID, returnVal) ? returnVal : 0;
	}

	UINT	GetOriginalFoundFiles(uint32 nSearchID) const
	{
		UINT returnVal;
		return m_originalFoundFilesCount.Lookup(nSearchID, returnVal) ? returnVal : 0;
	}

	bool	HasMergedSearchHistory(uint32 nSearchID) const
	{
		bool bMerged = false;
		return m_mergedSearchHistory.Lookup(nSearchID, bMerged) && bMerged;
	}

	void	MarkSearchAsMerged(uint32 nSearchID);

	uint32 GetParentItemCount(uint32 nResultsID);

	void	Process();

	void	ReorderSearches();
	CCriticalSection* GetSearchModelLock() const		{ return const_cast<CCriticalSection*>(&m_searchModelLock); }

	bool m_bKadReloadWaiting;
	DWORD m_dwKadLastReloadTick;
	bool m_bDeferSearchListUpdates;

	SearchList* GetSearchListForID(uint32 nSearchID); // Moved to public
	const SearchChildList* GetSearchChildrenForParent(const CSearchFile* pParent);
protected:
	uint32	GetSpamFilenameRatings(const CSearchFile *pSearchFile, bool bMarkAsNotSpam);
	void	LoadSpamFilter();

private:
	SearchListsStruct* GetSearchListStructForID(uint32 nSearchID, bool bCreate);
	static CString GetChildIndexKey(const CSearchFile* pParent, const CString& strFileName);
	static SearchChildList* GetChildrenForParent(SearchListsStruct* pList, CSearchFile* pParent, bool bCreate);
	static void DeleteChildLists(SearchListsStruct* pList);
	static void RemoveChildFromIndex(SearchListsStruct* pList, const CSearchFile* pChild);
	static void AddChildToIndex(SearchListsStruct* pList, CSearchFile* pChild);
	static void RecalculateParentFromChildren(SearchListsStruct* pList, CSearchFile* pParent);
	static void RebuildSearchListIndexes(SearchListsStruct* pList);
	void TouchSearchModelSequence();
	void SetSearchItemKnownTypeNoLock(CSearchFile* src);
	bool ApplySearchItemKnownTypeNoLock(CSearchFile* src, CSearchFile::EKnownType eKnownType);
	bool ApplyKnownTypeForHashNoLock(const uchar* pFileHash, CSearchFile::EKnownType eKnownType, std::set<uint32>* pTouchedSearchIDs);
	CSearchFile::EKnownType ResolveKnownTypeForHashNoLock(const uchar *pFileHash) const;
	bool AreKnownTypeDependenciesReady() const;
	bool ShouldDeferKnownTypeResolutionNoLock() const;
	void ClearKnownTypeRefreshJobsNoLock();

	struct SChunkedSearchIngestJob
	{
		SChunkedSearchIngestJob();

		std::vector<SSearchIngestRecord> m_records;
		INT_PTR m_iNextRecord;
		uint32 m_nSearchID;
		CString m_strClientHash;
		bool m_bClientResponse;
		uint32 m_dwFromUDPServerIP;
		bool m_bDoSpamRating;
		bool m_bUseKadReloadThrottle;
		bool m_bStartupStoredSearch;
		bool m_bNotifyUiOnCompletion;
		bool m_bNotifyLocalEd2kSearchEnd;
		bool m_bMoreResultsAvailable;
		UINT m_uProcessed;
		UINT m_uFailed;
		DWORD m_dwStartedTick;
		LONG m_lGeneration;
		LONG m_lSearchGeneration;
	};

	struct SChunkedSearchAnswerParseJob
	{
		SChunkedSearchAnswerParseJob();

		std::vector<BYTE> m_packet;
		UINT m_uResultCount;
		UINT m_uNextResult;
		ULONGLONG m_uPacketPosition;
		uint32 m_nSearchID;
		CString m_strClientHash;
		CString m_strSenderName;
		CString m_strDirectory;
		uint32 m_nClientID;
		uint16 m_nClientPort;
		uint32 m_nClientServerIP;
		uint16 m_nClientServerPort;
		bool m_bOptUTF8;
		bool m_bClientResponse;
		bool m_bPreviewSupport;
		bool m_bSupportsLargeFiles;
		bool m_bDoSpamRating;
		bool m_bUseKadReloadThrottle;
		bool m_bNotifyLocalEd2kSearchEnd;
		bool m_bMoreResultsAvailable;
		UINT m_uProcessed;
		UINT m_uFailed;
		DWORD m_dwStartedTick;
		DWORD m_dwLastProgressTick;
		LONG m_lGeneration;
		LONG m_lSearchGeneration;
	};

	struct SStoredSearchIngestPrepareJob
	{
		SStoredSearchIngestPrepareJob();

		std::vector<SSearchIngestRecord> m_records;
		INT_PTR m_iNextRecord;
		uint32 m_nSearchID;
		CString m_strClientHash;
		bool m_bClientResponse;
		uint32 m_dwFromUDPServerIP;
		bool m_bDoSpamRating;
		bool m_bUseKadReloadThrottle;
		bool m_bStartupStoredSearch;
		bool m_bNotifyUiOnCompletion;
		UINT m_uProcessed;
		UINT m_uFailed;
		DWORD m_dwStartedTick;
		DWORD m_dwLastProgressTick;
		LONG m_lGeneration;
		LONG m_lSearchGeneration;
	};

	struct SChunkedSpamRatingJob
	{
		SChunkedSpamRatingJob();
		~SChunkedSpamRatingJob();

		enum EPhase
		{
			PhaseCapture,
			PhaseReset,
			PhaseParents,
			PhaseChildren
		};

		std::vector<SSearchResultId> m_ids;
		std::vector<CString> m_astrFileNames;
		std::vector<EMFileSize> m_auFileSizes;
		std::vector<uint32> m_auMediaLengthSecs;
		SFilenameAutoBlacklistSnapshot* m_pAutomaticBlacklistSnapshot;
		std::vector<BYTE> m_abAutomaticBlacklistEvaluated;
		std::vector<BYTE> m_abAutomaticBlacklisted;
		std::vector<BYTE> m_abDownloadValidatorEvaluated;
		std::vector<BYTE> m_abDownloadValidatorSimilar;
		std::vector<uint32> m_auDownloadValidatorRevisions;
		std::vector<SDownloadValidatorFuzzyQueryData> m_aDownloadValidatorQueryData;
		std::vector<size_t> m_aiDownloadValidatorSourceIndices;
		std::vector<size_t> m_aiDownloadValidatorRootIndices;
		std::map<CString, size_t> m_downloadValidatorEvaluationSources;
		std::vector<INT_PTR> m_aiPossibleKnownCacheIndices;
		std::vector<INT_PTR> m_aiPossibleKnownAliasCacheIndices;
		std::vector<size_t> m_aiPossibleKnownSourceIndices;
		std::vector<uint32> m_auPossibleKnownAliasFingerprints;
		std::vector<SSearchFilePossibleKnownCache> m_aPossibleKnownCaches;
		std::vector<std::unordered_multimap<uint64, size_t> > m_aPossibleKnownRowIdentityIndices;
		INT_PTR m_iNextItem;
		INT_PTR m_iNextPrepareItem;
		INT_PTR m_iNextPossibleKnownItem;
		size_t m_uPossibleKnownAliasCount;
		size_t m_uProcessedPossibleKnownAliases;
		POSITION m_posCapture;
		INT_PTR m_iCaptureIndex;
		LONG m_lCaptureDestructiveSequence;
		uint32 m_nSearchID;
		bool m_bExpectHigher;
		bool m_bExpectLower;
		bool m_bRecalculateAll;
		bool m_bPrepareAutomaticBlacklist;
		bool m_bPrepareDownloadValidator;
		bool m_bPreparePossibleKnown;
		bool m_bDownloadValidatorGroupsAggregated;
		bool m_bShowDownloadValidatorOverlay;
		uint32 m_uPossibleKnownRevision;
		uint32 m_uPossibleKnownCandidateDataRevision;
		EPhase m_ePhase;
		UINT m_uProcessed;
		DWORD m_dwStartedTick;
		DWORD m_dwLastProgressTick;
		LONG m_lGeneration;
		LONG m_lSearchGeneration;
	};
	struct SSearchKnownTypeRefreshItem
	{
		SSearchKnownTypeRefreshItem();

		SSearchResultId m_id;
		CSearchFile::EKnownType m_eKnownType;
	};

	struct SChunkedSearchKnownTypeRefreshJob
	{
		SChunkedSearchKnownTypeRefreshJob();

		std::vector<SSearchKnownTypeRefreshItem> m_items;
		std::vector<CSKey> m_hashes;
		std::set<uint32> m_touchedSearchIDs;
		INT_PTR m_iNextResetItem;
		INT_PTR m_iNextItem;
		INT_PTR m_iNextHash;
		DWORD m_dwStartedTick;
		bool m_bStartupRefresh;
	};

	struct SParsedSearchIngestBatch
	{
		SParsedSearchIngestBatch();

		std::vector<SSearchIngestRecord> m_records;
		uint32 m_nSearchID;
		CString m_strClientHash;
		bool m_bClientResponse;
		uint32 m_dwFromUDPServerIP;
		bool m_bDoSpamRating;
		bool m_bUseKadReloadThrottle;
		bool m_bStartupStoredSearch;
		bool m_bNotifyUiOnCompletion;
		bool m_bNotifyLocalEd2kSearchEnd;
		bool m_bMoreResultsAvailable;
		LONG m_lGeneration;
		LONG m_lSearchGeneration;
	};

	struct SPossibleKnownQueryJob;
	struct SPossibleKnownQueryResult;
	void ProcessPossibleKnownQueryJobsOnDownloadValidatorThread();
	void DrainPossibleKnownQueryResults();
	void ClearPossibleKnownQueryJobs(uint32 nSearchID = 0);
	static bool BuildSearchIngestRecord(const CSearchFile *pFile, SSearchIngestRecord &record, bool bPrecomputeAutoBlacklist = false);
	static CSearchFile* CreateSearchFileFromIngestRecord(const SSearchIngestRecord &record);
	bool QueueSearchFileForIngest(CSearchFile *pFile, const CString &strClientHash, bool bClientResponse, uint32 dwFromUDPServerIP, bool bDoSpamRating, bool bUseKadReloadThrottle);
	void QueueChunkedSearchIngestJob(std::vector<SSearchIngestRecord> &records, uint32 nSearchID, const CString &strClientHash, bool bClientResponse, uint32 dwFromUDPServerIP, bool bDoSpamRating, bool bUseKadReloadThrottle);
	void QueueChunkedSearchAnswerParseJob(SChunkedSearchAnswerParseJob *pJob);
	bool QueueChunkedSpamRatingJob(uint32 nSearchID, bool bExpectHigher, bool bExpectLower, bool bRecalculateAll, bool bShowDownloadValidatorOverlay = false);
	void CancelChunkedSpamRatingJobs(uint32 nSearchID);
	void ProcessChunkedSpamRatingJobs(const DWORD dwSliceStartTick, UINT& uProcessedInSlice);
	void ProcessChunkedSpamRatingPrepareJobsOnDownloadValidatorThread();
	void RequestDownloadValidatorRecheck(uint32 nSearchID, DWORD dwDelayMs = 500);
	void RequestStartupDownloadValidatorCheck(uint32 nSearchID);
	bool QueueIncrementalDownloadValidatorJob(const std::vector<SSearchIngestRecord>& records, uint32 nSearchID, LONG lGeneration, LONG lSearchGeneration, bool bShowOverlay);
	void ProcessPendingDownloadValidatorRechecks();
	void SetSpamRatingPrepareJobActive(uint32 nSearchID, bool bActive);
	bool HasActiveSpamRatingPrepareJob(uint32 nSearchID);
	void QueuePreparedChunkedSpamRatingJob(SChunkedSpamRatingJob *pJob);
	void DrainPreparedChunkedSpamRatingJobs();
	bool IsChunkedSpamRatingJobStale(const SChunkedSpamRatingJob *pJob);
	void ApplyPreparedSearchFilterResults(const SChunkedSpamRatingJob &job, size_t uIndex, CSearchFile *pFile);
	bool AggregatePreparedDownloadValidatorResults(SChunkedSpamRatingJob &job);
	bool QueueStoredSearchIngestPrepareJob(std::vector<SSearchIngestRecord> &records, uint32 nSearchID, const CString &strClientHash,
		bool bClientResponse, uint32 dwFromUDPServerIP, bool bDoSpamRating, bool bUseKadReloadThrottle, bool bStartupStoredSearch);
	void DrainParsedSearchIngestBatches();
	void EnforceSearchIngestQueueLimit(uint32 nSearchID);
	void EnforceSearchAnswerParseQueueLimitLocked(uint32 nSearchID);
	void EnforceStoredSearchPrepareQueueLimitLocked(uint32 nSearchID);
	void EnforceParsedSearchIngestBatchLimitLocked(uint32 nSearchID);
	void EnforceSpamRatingPrepareQueueLimitLocked(uint32 nSearchID);
	void EnforcePreparedSpamRatingQueueLimitLocked(uint32 nSearchID, std::vector<uint32>* pDroppedSearchIDs = NULL);
	void EnforceChunkedSpamRatingQueueLimit(uint32 nSearchID);
	void ProcessSearchAnswerParseJobsOnParserThread();
	void ProcessStoredSearchIngestPrepareJobsOnParserThread();
	bool PostChunkedSearchIngestMessage();
	void CancelChunkedSearchIngestJobs(uint32 nSearchID);
	void CancelSearchAnswerParseJobs(uint32 nSearchID);
	bool IsSearchJobStale(uint32 nSearchID, LONG lGeneration, LONG lSearchGeneration);
	bool IsSearchIngestJobStale(const SChunkedSearchIngestJob *pJob);
	bool IsSearchAnswerParseJobStale(const SChunkedSearchAnswerParseJob *pJob);
	bool IsStoredSearchIngestPrepareJobStale(const SStoredSearchIngestPrepareJob *pJob);
	bool IsParsedSearchIngestBatchStale(const SParsedSearchIngestBatch *pBatch);
	void QueueParsedSearchIngestBatch(SChunkedSearchAnswerParseJob &job, std::vector<SSearchIngestRecord> &records, bool bNotifyUiOnCompletion);
	void QueueParsedSearchIngestBatch(SStoredSearchIngestPrepareJob &job, std::vector<SSearchIngestRecord> &records, bool bNotifyUiOnCompletion);
	bool IsSearchProcessingAcceptingJobs() const;
	void FinishStartupLoad();
	bool QueueStartupStoredSearchesLoadWorker();
	bool StartStartupStoredSearchApplyTab(SStartupStoredSearchesLoadResult &result, SStartupStoredSearchTab &tab);
	bool QueueStartupStoredSearchTabIngest(SStartupStoredSearchTab &tab);
	bool HasPendingStartupStoredSearchIngest(uint32 nSearchID);
	void CompleteStartupStoredSearchApplyTab(SStartupStoredSearchTab &tab);
	bool StartSearchAnswerParseThread();
	void StopSearchAnswerParseThread();
	bool SignalSearchAnswerParseThread();
	void UpdateSearchIngestOutputWnd(uint32 nSearchID, const CString &strClientHash, bool bUseKadReloadThrottle);
	CTypedPtrList<CPtrList, SChunkedSearchKnownTypeRefreshJob*> m_chunkedSearchKnownTypeRefreshJobs;
	CTypedPtrList<CPtrList, SChunkedSearchIngestJob*> m_chunkedSearchIngestJobs;
	CTypedPtrList<CPtrList, SChunkedSearchAnswerParseJob*> m_chunkedSearchAnswerParseJobs;
	CTypedPtrList<CPtrList, SStoredSearchIngestPrepareJob*> m_storedSearchIngestPrepareJobs;
	CTypedPtrList<CPtrList, SChunkedSpamRatingJob*> m_chunkedSpamRatingJobs;
	CTypedPtrList<CPtrList, SChunkedSpamRatingJob*> m_chunkedSpamRatingPrepareJobs;
	CTypedPtrList<CPtrList, SChunkedSpamRatingJob*> m_preparedSpamRatingJobs;
	CMap<uint32, uint32, UINT, UINT> m_activeSpamRatingPrepareCounts;
	CMap<uint32, uint32, UINT, UINT> m_downloadValidatorProgressProcessed;
	CMap<uint32, uint32, UINT, UINT> m_downloadValidatorProgressTotal;
	CMap<uint32, uint32, BYTE, BYTE> m_downloadValidatorOverlayProgress;
	CMap<uint32, uint32, DWORD, DWORD> m_downloadValidatorRecheckDue;
	CMap<uint32, uint32, BYTE, BYTE> m_downloadValidatorDirtySearches;
	CMap<uint32, uint32, BYTE, BYTE> m_downloadValidatorStartupOverlaySearches;
	bool m_bDownloadValidatorRecheckAllPending;
	CTypedPtrList<CPtrList, SParsedSearchIngestBatch*> m_parsedSearchIngestBatches;
	CTypedPtrList<CPtrList, SPossibleKnownQueryJob*> m_possibleKnownQueryJobs;
	CTypedPtrList<CPtrList, SPossibleKnownQueryResult*> m_possibleKnownQueryResults;
	mutable CCriticalSection m_searchModelLock;
	CCriticalSection m_searchAnswerParseQueueLock;
	CCriticalSection m_parsedSearchIngestBatchLock;
	CCriticalSection m_possibleKnownQueryResultLock;
	CMap<uint32, uint32, bool, bool> m_cancelledSearchAnswerParseIds;
	CMap<uint32, uint32, LONG, LONG> m_searchAnswerParseGenerations;
	CWinThread *m_pSearchAnswerParseThread;
	HANDLE m_hSearchAnswerParseEvent;
	HANDLE m_hSearchAnswerParseStopEvent;
	LONG m_lSearchAnswerParseGeneration;
	LONG m_lSpamFilterSaveGeneration;
	volatile LONG m_lStoredSearchesSaveGeneration;
	LONG m_lStoredSearchStartupLoadGeneration;
	uint64 m_uStoredSearchStartupLoadCancellationToken;
	mutable CCriticalSection m_storedSearchStartupProgressLock;
	LONG m_lSearchModelSequence;
	volatile LONG m_lDownloadValidatorPrioritySearchID;
	bool m_bChunkedSearchIngestPending;
	DWORD m_dwChunkedSearchIngestLastProgressTick;

	SSearchParams *m_pStoredSearchStartupLoadParams;
	uint32 m_nStoredSearchStartupLoadNextSearchID;
	UINT m_uStoredSearchStartupLoadTotalSearches;
	UINT m_uStoredSearchStartupLoadLoadedSearches;
	UINT m_uStoredSearchStartupLoadLoadedFiles;
	uint32 m_uStoredSearchStartupLoadCurrentRemainingFiles;
	uint32 m_uStoredSearchStartupLoadCurrentTotalFiles;
	bool m_bStoredSearchStartupLoadActive;
	bool m_bStoredSearchStartupLoadCompleted;
	bool m_bStoredSearchStartupLoadLoadedVisibleSearch;
	bool m_bStoredSearchStartupLoadReloadedVisibleSearch;
	bool m_bStoredSearchStartupLoadCurrentDeleteParams;
	bool m_bStoredSearchStartupLoadCurrentIsLastTab;
	DWORD m_dwStoredSearchStartupLoadStartedTick;
	DWORD m_dwStoredSearchStartupLoadLastProgressTick;


	CTypedPtrList<CPtrList, SearchListsStruct*> m_listFileLists;
	CMap<uint32, uint32, UINT, UINT> m_foundFilesCount;
	CMap<uint32, uint32, UINT, UINT> m_originalFoundFilesCount;
	CMap<uint32, uint32, UINT, UINT> m_foundSourcesCount;
	CMap<uint32, uint32, UINT, UINT> m_ReceivedUDPAnswersCount;
	CMap<uint32, uint32, UINT, UINT> m_RequestedUDPAnswersCount;
	CMap<uint32, uint32, bool, bool> m_mergedSearchHistory;
	CSearchListCtrl *outputwnd;
	CString	m_strResultFileType;


	// spam filter
	typedef CMap<uint32, uint32, bool, bool> CSpammerIPMap;
	typedef CMap<uint32, uint32, UDPServerRecord*, UDPServerRecord*> CUDPServerRecordMap;
	CMap<CSKey, const CSKey&, bool, bool> m_mapKnownSpamHashes;
	CSpammerIPMap			m_mapKnownSpamServerIPs;
	CSpammerIPMap			m_mapKnownSpamSourcesIPs;
	CUDPServerRecordMap		m_mUDPServerRecords;
	CStringArray							m_astrSpamCheckCurSearchExp;
	CStringArray							m_astrKnownSpamNames;
	CStringArray							m_astrKnownSimilarSpamNames;
	CArray<uint64>							m_aui64KnownSpamSizes;
	CArray<uint32, uint32>					m_aCurED2KSentRequestsIPs;
	CArray<uint32, uint32>					m_aCurED2KSentReceivedIPs;
	uint32	m_nCurED2KSearchID;
	bool									m_bSpamFilterLoaded;
	CMap<CSKey, const CSKey&, bool, bool>	m_mapBlacklistedHashes;
	uint32	m_nLastSaved;
};
