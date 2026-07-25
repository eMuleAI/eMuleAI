//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#pragma once
#include <list>
#include <regex>
#include <unordered_map>
#include <vector>
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
		bool ReloadRegexRules();

		enum ERegexRulesError {
			RegexRulesNoError = 0,
			RegexRulesInvalidExpression,
			RegexRulesMissingCaptureGroup,
			RegexRulesFileReadError,
			RegexRulesFileWriteError,
			RegexRulesTextTooLarge
		};

		struct SRegexRulesResult
		{
			ERegexRulesError eError = RegexRulesNoError;
			UINT uLineNumber = 0;
			UINT uRuleCount = 0;
		};

		enum { RegexRulesTextLimit = 4 * 1024 * 1024 };

		bool ReadRegexRulesText(CString& strRulesText, SRegexRulesResult& result) const;
		bool ReloadRegexRules(CString& strRulesText, SRegexRulesResult& result);
		bool ValidateRegexRulesText(const CString& strRulesText, bool bCaseInsensitive, SRegexRulesResult& result) const;
		bool ApplyRegexRulesText(const CString& strRulesText, bool bCaseInsensitive, SRegexRulesResult& result);
		void QueueReloadMap();
		void QueueReloadRegexMap();
		void CancelReloadMap();
		bool ProcessReloadMapSlice(bool bDrainAll = false);
		bool HasPendingReloadMap() const { return m_pReloadMapState != NULL; }
		bool IsMapInitialized() const;
		void BeginStartupKnownFilesMapLoad(UINT uExpectedRecordCount);
		void AddStartupKnownFileToMap(const uchar* hash, const CString& filename, EMFileSize filesize, uint32 uMediaLengthSec);
		void CompleteStartupKnownFilesMapLoad();
		void StartDeferredBackgroundWork();
		bool ProcessDeferredSourceCaptureSlice();
		bool ProcessBackgroundWorkSlice();
		bool GetBackgroundProgress(UINT& uProcessed, UINT& uTotal) const;
		void AddToMap(const uchar* hash, const CString& filename, const EMFileSize filesize, bool bKnownCollectionChanged = false);
		void AddDownloadingFileToMap(const uchar* hash, const CString& filename, const EMFileSize filesize);
		void RemoveFromMap(const uchar* hash, const CString& filename, const EMFileSize filesize);
		const UINT CheckFile(const uchar* hash, const CString& filename, const EMFileSize filesize, const bool bCalledByAddToDownload);

		enum EFuzzyFileSourceFlags : uint8 {
			FuzzyFileSourceUnknown = 0x00,
			FuzzyFileSourceDownloading = 0x01,
			FuzzyFileSourceKnown = 0x02
		};

		enum EFuzzyMediaLengthSource : uint8 {
			FuzzyMediaLengthUnknown = 0,
			FuzzyMediaLengthRemoteMetadata,
			FuzzyMediaLengthLocalMediaInfo
		};

		enum EDownloadValidatorResult {
			OK = 0,
			Known,
			Downloading,
			Cancelled,
			SimilarName,
			ManualBlacklisted,
			AutomaticBlacklisted
		};

		struct FileCandidateType
		{
			CString strName = EMPTY;
			EMFileSize uSize = 0ull;
			uint32 uMediaLengthSec = 0;
			EFuzzyMediaLengthSource eMediaLengthSource = FuzzyMediaLengthUnknown;
			uchar ucHash[MDX_DIGEST_SIZE] = { 0 };
		};

		struct FileInfoType : public FileCandidateType
		{
			std::vector<FileCandidateType> alternateFiles;
		};

		struct FuzzyCandidateType : public FileCandidateType
		{
			uint32 uRecordID = 0;
			uint32 uMediaBitrateKbps = 0;
			CString strMediaArtist = EMPTY;
			CString strMediaAlbum = EMPTY;
			CString strMediaTitle = EMPTY;
			CString strMediaCodec = EMPTY;
			CString strFolder = EMPTY;
			CString strAICHHash = EMPTY;
			CString strNormalizedName = EMPTY;
			CString strBoundaryName = EMPTY;
			uint32 uSharedGramCount = 0;
			uint32 uQueryGramCount = 0;
			uint32 uCandidateGramCount = 0;
			uint32 uSimilarityScore = 0;
			uint32 uCoverageScore = 0;
			uint32 uWeightedJaccardScore = 0;
			uint32 uEditSimilarityScore = 0;
			uint64 uStructuralIdentityKey = 0;
			uint8 uFileType = 0;
			uint8 uSourceFlags = FuzzyFileSourceUnknown;
			bool bExactSubstring = false;
			bool bStructuralIdentityMatch = false;
			bool bFileTypeConflict = false;
		};

		bool FindFuzzyCandidates(const CString& filename, std::vector<FuzzyCandidateType>& candidates, size_t uMaximumCandidates = 0,
			bool bAllowIncompleteIndex = false, SDownloadValidatorFuzzyQueryData* pQueryData = NULL);
		bool FindPossibleKnownCandidates(const uchar* hash, const CString& filename, EMFileSize filesize, uint32 uMediaLengthSec,
			EFuzzyMediaLengthSource eMediaLengthSource, std::vector<FuzzyCandidateType>& candidates, size_t uMaximumCandidates = 0, bool bResolveMetadata = true,
			SDownloadValidatorFuzzyQueryData* pQueryData = NULL, bool bIncludeFuzzy = true, bool bIncludeTrusted = true,
			const std::vector<FuzzyCandidateType>* pPreparedFuzzyCandidates = NULL);
		bool EvaluateSearchResultSimilarity(const uchar* hash, const CString& filename, EMFileSize filesize, uint32 uMediaLengthSec,
			EFuzzyMediaLengthSource eMediaLengthSource, uint32& uRevision, SDownloadValidatorFuzzyQueryData* pQueryData = NULL,
			std::vector<FuzzyCandidateType>* pPreparedFuzzyCandidates = NULL);
		void PopulateFuzzyDisplayMetadata(FuzzyCandidateType& candidate) const;
		void PopulateFuzzyDisplayMetadata(std::vector<FuzzyCandidateType>& candidates) const;
		bool IsFuzzyIndexReady() const { return ::InterlockedCompareExchange(const_cast<LONG*>(&m_lFuzzyIndexReadySnapshot), 0, 0) != 0; }
		bool IsPossibleKnownSearchReady() const;
		uint32 GetPossibleKnownRevision() const;
		uint32 GetCandidateDataRevision() const;
		uint32 GetEvaluationRevision() const;
		void InvalidateEvaluationResults();
		void InvalidatePossibleKnownResults(bool bInvalidateEvaluation);

		typedef CMap<CString, LPCTSTR, FileInfoType, FileInfoType> DownloadValidatorFileMap;

		DownloadValidatorFileMap m_DownloadValidatorMap;
		DownloadValidatorFileMap m_DownloadValidatorDateTimeMap;
		DownloadValidatorFileMap m_DownloadValidatorRegexMap;
	protected:
		typedef uint64 FuzzyGramType;
		typedef uint64 FuzzyTokenType;

		struct SFuzzyRecord : public FileCandidateType
		{
			CString strNormalizedName = EMPTY;
			CString strBoundaryName = EMPTY;
			std::vector<FuzzyTokenType> tokenHashes;
			uint32 uStructuralIdentityIndex = _UI32_MAX;
			bool bKnownTokenFrequencyRegistered = false;
			uint32 uIndexedGramCount = 0;
			uint64 uIndexedGramWeight = 0;
			uint8 uFileType = 0;
			uint8 uSourceFlags = FuzzyFileSourceUnknown;
			bool bActive = true;
		};

		struct SFuzzyGramIndexEntry
		{
			uint32 uDocumentFrequency = 0;
			uint32 uPostingOffset = 0;
			uint32 uBasePostingCount = 0;
		};

		struct SFuzzyCandidateCacheEntry
		{
			uint32 uRevision = 0;
			uint32 uCandidateFingerprint = 0;
			uint64 uStructuralIdentityKey = 0;
			CString strSourceFileName = EMPTY;
			CString strNormalizedName = EMPTY;
			CString strBoundaryName = EMPTY;
			uint8 uFileType = 0;
			bool bHasCandidates = false;
			std::vector<FuzzyCandidateType> candidates;
		};

		struct SFuzzyStructuralGroupStats
		{
			uint32 uFileCount = 0;
			uint64 uIdentitySketch = 0;
		};

		struct SFuzzyStructuralMatch
		{
			uint64 uIdentityKey = 0;
			uint32 uScoreFloor = 0;
			bool bMatch = false;
			bool bWeakMatch = false;
			bool bConflict = false;
		};

		typedef std::unordered_map<FuzzyGramType, SFuzzyGramIndexEntry> FuzzyGramIndex;
		typedef std::unordered_map<FuzzyGramType, std::vector<uint32> > FuzzyDeltaPostingIndex;
		typedef std::unordered_multimap<uint64, uint32> FuzzyIdentityIndex;
		typedef std::unordered_multimap<uint64, uint32> FuzzyStructuralIndex;
		typedef std::unordered_map<FuzzyTokenType, SFuzzyStructuralGroupStats> FuzzyStructuralGroupStats;
		typedef std::unordered_map<FuzzyTokenType, uint32> FuzzyTokenFrequencyIndex;

		struct SReloadMapState;
		struct SFuzzyBuildSource : public FileCandidateType
		{
			uint8 uSourceFlags = FuzzyFileSourceUnknown;
			bool bActive = true;
		};
		struct SFuzzyBuildChunk
		{
			std::vector<SFuzzyBuildSource> sources;
			size_t uNextSource = 0;
		};
		struct SRegexRule
		{
			std::basic_regex<TCHAR> regex;
			bool bCaseInsensitive = false;
			bool bWholeNameMatch = false;
		};
		void PrepareReloadMapStorage(bool bRegexOnly, UINT uExpectedRecordCount = 0, bool bDeferFuzzyStorage = false);
		void AddCurrentRuntimeFilesToMap(bool bAddFuzzy = true);
		void AddCurrentDownloadingFilesToMap(bool bAddFuzzy);
		void AddTrustedToMapInternal(const uchar* hash, const CString& filename, EMFileSize filesize, uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource);
		void AddToMapInternal(const uchar* hash, const CString& filename, EMFileSize filesize, bool bKnownCollectionChanged);
		bool AppendDeferredFuzzySource(std::vector<SFuzzyBuildSource>& sources, const uchar* hash, const CString& filename, EMFileSize filesize, uint8 uSourceFlags, uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource);
		void QueueDeferredFuzzyChunk(SFuzzyBuildChunk* pChunk, bool bIncreaseTotal);
		void QueueDeferredFuzzySource(const uchar* hash, const CString& filename, EMFileSize filesize, uint8 uSourceFlags, uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource);
		void DeactivateDeferredFuzzySource(const uchar* hash, const CString& filename, EMFileSize filesize);
		void CancelDeferredBackgroundWork();
		void QueueBackgroundWorker();
		static void IncrementRevision(volatile LONG* pRevision);
		void TouchEvaluationRevision();
		void TouchPossibleKnownRevision(bool bCandidateDataChanged = true);
		void NotifyIncrementalMapMutation();
		uint32 GetFuzzyCandidateDataRevision() const;
		bool IsFuzzyMatchingEnabled() const;
		void ResetFuzzyIndex(UINT uExpectedRecordCount, bool bReserveStorage = true);
		CString RemoveMojibakeGarbage(const CString& strInput, bool bPreserveRemovedTokenBarrier = false) const;
		CString BuildFuzzyBoundaryName(const CString& filename, bool bPreserveTags = false, bool bPreserveMojibakeBarriers = false) const;
		CString BuildFuzzyNormalizedName(const CString& filename) const;
		void BuildFuzzyGrams(const CString& strNormalizedName, std::vector<FuzzyGramType>& grams) const;
		void BuildFuzzyTokenHashes(const CString& strBoundaryName, std::vector<FuzzyTokenType>& tokens) const;
		void BuildFuzzyOrderedTokenHashes(const CString& strBoundaryName, std::vector<FuzzyTokenType>& tokens) const;
		void RegisterFuzzyKnownTokenFrequencies(SFuzzyRecord& record);
		uint32 CalculateFuzzyTokenRarityScore(FuzzyTokenType token) const;
		uint32 CalculateFuzzyTokenSimilarityScore(const std::vector<FuzzyTokenType>& queryTokens, const std::vector<FuzzyTokenType>& queryOrderedTokens,
			const std::vector<FuzzyTokenType>& candidateTokens, const std::vector<FuzzyTokenType>& candidateOrderedTokens, uint32& uSharedTokenCount, uint32& uTokenCoveragePercent,
			uint32& uSequenceQuality, uint32& uLongestRunTokens, uint32& uLongestRunCoveragePercent, uint32& uTotalRunCoveragePercent) const;
		void BuildFuzzyStructuralIdentity(const CString& filename, SDownloadValidatorFuzzyStructuralIdentity& identity) const;
		uint64 BuildFuzzyStructuralIdentityKey(const SDownloadValidatorFuzzyStructuralIdentity& identity) const;
		uint64 BuildFuzzyStructuralLookupKey(const SDownloadValidatorFuzzyStructuralIdentity& identity, bool bIncludeYear) const;
		void RegisterFuzzyStructuralIdentity(const SDownloadValidatorFuzzyStructuralIdentity& identity);
		void RegisterFuzzyStructuralRecord(const SDownloadValidatorFuzzyStructuralIdentity& identity, uint32 uRecordID);
		void AddFuzzyStructuralCandidates(const SDownloadValidatorFuzzyStructuralIdentity& identity, std::vector<uint32>& recordIDs) const;
		bool IsFuzzyStructuralGroupTokenStrong(FuzzyTokenType token) const;
		SFuzzyStructuralMatch EvaluateFuzzyStructuralIdentity(const SDownloadValidatorFuzzyStructuralIdentity& queryIdentity, const SDownloadValidatorFuzzyStructuralIdentity& candidateIdentity) const;
		uint32 GetFuzzyNormalizationFingerprint() const;
		uint32 GetFuzzyCandidateFingerprint() const;
		bool PrepareFuzzyQueryData(const CString& filename, SDownloadValidatorFuzzyQueryData& queryData) const;
		uint32 CalculateFuzzyGramWeight(uint32 uDocumentFrequency) const;
		uint32 CalculateFuzzySimilarityScore(const CString& strQueryName, const CString& strQueryBoundaryName, uint64 uQueryGramWeight, uint64 uCandidateGramWeight, uint64 uSharedGramWeight, FuzzyCandidateType& candidate) const;
		uint64 BuildFuzzyIdentityKey(const uchar* hash, const CString& filename, EMFileSize filesize) const;
		void ResolveFileMetadata(const uchar* hash, EMFileSize filesize, uint8& uSourceFlags, uint32& uMediaLengthSec, EFuzzyMediaLengthSource& eMediaLengthSource) const;
		void ResolveFuzzyDisplayMetadata(FuzzyCandidateType& candidate) const;
		void AddToMapInternal(const uchar* hash, const CString& filename, EMFileSize filesize, uint8 uSourceFlags, uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource);
		void AddFuzzyRecord(const uchar* hash, const CString& filename, EMFileSize filesize, uint8 uSourceFlags, uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource, const CString* pNormalizedName = NULL);
		void RemoveFuzzyRecord(const uchar* hash, const CString& filename, EMFileSize filesize);
		void StartFuzzyPostingPreparation(SReloadMapState& state);
		bool ProcessFuzzyPostingPreparation(SReloadMapState& state, DWORD dwSliceStart, DWORD dwSliceBudgetMs, UINT uMaxItemsPerSlice, UINT& uProcessed, bool bDrainAll);
		void AddFuzzyRecordToReadyIndex(uint32 uRecordID, const std::vector<FuzzyGramType>& grams);
		void AdjustFuzzyGramRecordWeights(FuzzyGramType gram, const SFuzzyGramIndexEntry& entry, uint32 uOldWeight, uint32 uNewWeight);
		void RemoveFuzzyGramPostings(FuzzyGramType gram, const SFuzzyGramIndexEntry& entry, uint32 uGramWeight);
		bool EvaluateFuzzyCandidatesInternal(const SDownloadValidatorFuzzyQueryData& queryData,
			std::vector<FuzzyCandidateType>* pCandidates, const uchar* hash, EMFileSize filesize, uint32 uMediaLengthSec,
			EFuzzyMediaLengthSource eMediaLengthSource, FuzzyCandidateType* pBestCandidate, FuzzyCandidateType* pCompetingCandidate,
			size_t uMaximumCandidates, bool bAllowIncompleteIndex);
		bool FindFuzzyDecisionCandidateStreaming(const uchar* hash, const CString& filename, EMFileSize filesize,
			uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource, SDownloadValidatorFuzzyQueryData* pQueryData,
			FuzzyCandidateType& candidate);
		bool IsFuzzyCandidateCacheKeyMatch(const SFuzzyCandidateCacheEntry& entry, const SDownloadValidatorFuzzyQueryData& queryData, uint32 uRevision, uint32 uCandidateFingerprint) const;
		std::list<SFuzzyCandidateCacheEntry>::iterator FindFuzzyCandidateCacheEntry(const SDownloadValidatorFuzzyQueryData& queryData, uint32 uRevision, uint32 uCandidateFingerprint);
		void StoreFuzzyCandidateCacheEntry(const SFuzzyCandidateCacheEntry& entry);
		void ClearFuzzyCandidateCache();
		const FileCandidateType* FindEligibleFileCandidate(const FileInfoType& fileInfo, EMFileSize filesize, uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource) const;
		const FuzzyCandidateType* FindFuzzyDecisionCandidate(const uchar* hash, EMFileSize filesize, uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource, const std::vector<FuzzyCandidateType>& candidates) const;
		bool IsMediaLengthCandidateAllowed(uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource, const FileCandidateType& candidate) const;
		bool IsFileSizeCandidateAllowed(EMFileSize filesize, const FileCandidateType& candidate) const;
		void AbortFuzzyIndexBuild();
		CString BuildMapKey(const CString& filename) const;
		CString BuildDateTimeProcessedFileName(const CString& filename) const;
		CString BuildDateTimeMapKey(const CString& filename, const CString& strProcessedFileName) const;
		bool CompileRegexRulesText(const CString& strRulesText, bool bCaseInsensitive, std::vector<SRegexRule>& loadedRules, SRegexRulesResult& result) const;
		bool WriteRegexRulesText(const CString& strRulesText, SRegexRulesResult& result) const;
		bool BuildRegexMapKey(size_t uRuleIndex, const CString& filename, CString& strMapKey) const;
		void AddRegexMatchesToMap(const uchar* hash, const CString& filename, const EMFileSize filesize, uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource);
		void RemoveRegexMatchesFromMap(const uchar* hash, const CString& filename, const EMFileSize filesize);
		const FileInfoType* FindRegexFileInfo(const CString& filename) const;
		void AddPreparedToMap(DownloadValidatorFileMap& map, const CString& strProcessedFileName, const uchar* hash, const CString& filename, const EMFileSize filesize, uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource);
		void RemovePreparedFromMap(DownloadValidatorFileMap& map, const CString& strProcessedFileName, const uchar* hash, const EMFileSize filesize);
		std::vector<SRegexRule> m_regexRules;
		std::vector<SFuzzyRecord> m_fuzzyRecords;
		std::vector<SDownloadValidatorFuzzyStructuralIdentity> m_fuzzyStructuralIdentities;
		FuzzyStructuralGroupStats m_fuzzyStructuralGroupStats;
		FuzzyTokenFrequencyIndex m_fuzzyKnownTokenFrequencies;
		FuzzyGramIndex m_fuzzyGramIndex;
		FuzzyDeltaPostingIndex m_fuzzyDeltaPostingIndex;
		FuzzyIdentityIndex m_fuzzyIdentityIndex;
		FuzzyStructuralIndex m_fuzzyStructuralYearIDIndex;
		FuzzyStructuralIndex m_fuzzyStructuralIDIndex;
		std::vector<uint32> m_fuzzyPostings;
		std::vector<uint32> m_fuzzyCandidateSharedGramCounts;
		std::vector<uint64> m_fuzzyCandidateSharedGramWeights;
		std::vector<uint8> m_fuzzyCandidateTouchedFlags;
		std::vector<uint32> m_fuzzyTouchedRecordIDs;
		std::list<SFuzzyCandidateCacheEntry> m_fuzzyCandidateCache;
		size_t m_uFuzzyCandidateCacheCandidateCount;
		uint32 m_uFuzzyMaximumPostingFrequency;
		uint32 m_uFuzzyWeightRecordCount;
		uint32 m_uFuzzyKnownTokenDocumentCount;
		uint32 m_uFuzzyInactiveRecordCount;
		bool m_bFuzzyIndexReady;
		bool m_bFuzzyIndexAvailable;
		bool m_bFuzzyRebuildRecommended;
		volatile LONG m_lPossibleKnownRevision;
		volatile LONG m_lEvaluationRevision;
		volatile LONG m_lFuzzyCandidateDataRevision;
		volatile LONG m_lRegexReloadActive;
		volatile LONG m_lMapInitialized;
		volatile LONG m_lFuzzyIndexReadySnapshot;
		mutable CCriticalSection m_indexLock;
		bool m_bStartupKnownFilesMapLoadActive;
		SReloadMapState* m_pDeferredFuzzyCaptureState;
		SReloadMapState* m_pBackgroundFuzzyPrepareState;
		SFuzzyBuildChunk* m_pBackgroundFuzzyChunk;
		CTypedPtrList<CPtrList, SFuzzyBuildChunk*> m_deferredFuzzyChunks;
		mutable CCriticalSection m_deferredFuzzyQueueLock;
		volatile LONG m_lDeferredFuzzyCaptureRestartRequired;
		volatile LONG m_lBackgroundFuzzyCaptureComplete;
		volatile LONG m_lBackgroundFuzzyIndexInitialized;
		volatile LONG m_lBackgroundFuzzyState;
		volatile LONG m_lBackgroundOverlayVisible;
		volatile LONG m_lBackgroundFuzzyProcessed;
		volatile LONG m_lBackgroundFuzzyTotal;
		volatile LONG m_lBackgroundWorkQueued;
		volatile LONG m_lBackgroundWorkEnabled;
		SReloadMapState* m_pReloadMapState;
		int 	m_iDataSize;
};
