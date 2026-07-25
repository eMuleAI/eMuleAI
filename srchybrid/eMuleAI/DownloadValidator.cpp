//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#include "StdAfx.h"
#include <algorithm>
#include <exception>
#include "emule.h"
#include "emuleDlg.h"
#include "DownloadValidator.h"
#include "Log.h"
#include "Preferences.h"
#include "KnownFile.h"
#include "PartFile.h"
#include "DownloadQueue.h"
#include "TransferDlg.h"
#include "DownloadListCtrl.h"
#include "SearchDlg.h"
#include "SearchResultsWnd.h"
#include "SearchList.h"
#include "SharedFilesWnd.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#endif

namespace
{
	struct SDownloadValidatorNumberPart
	{
		int iLength;
		int iValue;
	};

	struct SDownloadValidatorDateTimeMatch
	{
		CString strCompareText;
	};

	struct SDownloadValidatorDateTimeOptions
	{
		int iStartYear;
		int iEndYear;
		bool bUseYearRange;
		bool bRequireSeconds;
		bool bIncludeFollowingNumericValues;
	};

	const uint32 DOWNLOAD_VALIDATOR_FUZZY_MINIMUM_POSTING_FREQUENCY = 64;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_MAXIMUM_POSTING_FREQUENCY = 4096;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_POSTING_RATIO_DIVISOR = 100;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_INACTIVE_REBUILD_MINIMUM = 4096;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_INACTIVE_REBUILD_PERCENT = 10;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX = 100;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_DIFFERENT_NAME_SCORE_MAX = 99;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_AMBIGUITY_MARGIN = 3;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_COVERAGE_WEIGHT = 55;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_JACCARD_WEIGHT = 25;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_EDIT_WEIGHT = 15;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SUBSTRING_BONUS = 5;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_MINIMUM_EDIT_COVERAGE_SCORE = 50;
	const int DOWNLOAD_VALIDATOR_FUZZY_MAXIMUM_EDIT_LENGTH = 256;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_MAXIMUM_EDIT_DISTANCE = 32;
	const UINT DOWNLOAD_VALIDATOR_BACKGROUND_RECORDS_PER_SLICE = 256;
	const UINT DOWNLOAD_VALIDATOR_BACKGROUND_GRAMS_PER_SLICE = 4096;
	const DWORD DOWNLOAD_VALIDATOR_BACKGROUND_SLICE_MS = 8;
	const UINT DOWNLOAD_VALIDATOR_CAPTURE_RECORDS_PER_SLICE = 4096;
	const UINT DOWNLOAD_VALIDATOR_CAPTURE_QUEUE_MAX_CHUNKS = 4;
	const DWORD DOWNLOAD_VALIDATOR_CAPTURE_SLICE_MS = 3;
	const size_t DOWNLOAD_VALIDATOR_FUZZY_CACHE_MAX_ENTRIES = 64;
	const size_t DOWNLOAD_VALIDATOR_FUZZY_CACHE_MAX_CANDIDATES = 8192;
	const size_t DOWNLOAD_VALIDATOR_FUZZY_CACHE_MAX_CANDIDATES_PER_ENTRY = 512;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_FULL_MATCH_SCORE = 96;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_PARTIAL_GROUP_SCORE = 93;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_PARTIAL_YEAR_SCORE = 91;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_WEAK_MATCH_SCORE = 88;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_WEAK_MATCH_MINIMUM_SCORE = 64;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_WEAK_MATCH_BONUS = 12;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_TOKEN_RARITY_LEVEL_1_SCORE = 100;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_TOKEN_RARITY_LEVEL_2_SCORE = 89;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_TOKEN_RARITY_LEVEL_3_SCORE = 81;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_TOKEN_RARITY_LEVEL_4_SCORE = 73;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_TOKEN_RARITY_LEVEL_5_SCORE = 65;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_TOKEN_RARITY_LEVEL_6_SCORE = 57;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_TOKEN_RARITY_LEVEL_7_SCORE = 49;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_TOTAL_COVERAGE_WEIGHT = 50;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_RARITY_COVERAGE_WEIGHT = 30;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_CANDIDATE_CLEANLINESS_WEIGHT = 15;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_GROUP_SUPPORT_WEIGHT = 5;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_MAXIMUM_GROUPS = 3;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_CLEANLINESS_FREE_UNMATCHED_TOKENS = 2;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_TWO_TOKEN_SCORE_MIN = 50;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_TWO_TOKEN_SCORE_MAX = 63;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_THREE_TOKEN_SCORE_MIN = 64;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_THREE_TOKEN_SCORE_MAX = 77;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_FOUR_TOKEN_SCORE_MIN = 78;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_FOUR_TOKEN_SCORE_MAX = 91;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_FIVE_TOKEN_SCORE_MIN = 94;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_FIVE_TOKEN_SCORE_MAX = 97;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_SIX_TOKEN_SCORE_MIN = 98;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_SIX_TOKEN_SCORE_MAX = 99;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_DOMINANT_FIVE_TOKEN_COVERAGE_PERCENT = 50;
	static_assert(DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_TOTAL_COVERAGE_WEIGHT
		+ DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_RARITY_COVERAGE_WEIGHT
		+ DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_CANDIDATE_CLEANLINESS_WEIGHT
		+ DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_GROUP_SUPPORT_WEIGHT == DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX,
		"Download Validator sequence band weights must total 100.");
	static_assert(DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_MAXIMUM_GROUPS > 1,
		"Download Validator sequence group support requires at least two groups.");
	static_assert(DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_TWO_TOKEN_SCORE_MAX < DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_THREE_TOKEN_SCORE_MIN
		&& DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_THREE_TOKEN_SCORE_MAX < DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_FOUR_TOKEN_SCORE_MIN
		&& DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_FOUR_TOKEN_SCORE_MAX < DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_FIVE_TOKEN_SCORE_MIN
		&& DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_FIVE_TOKEN_SCORE_MAX < DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_SIX_TOKEN_SCORE_MIN
		&& DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_SIX_TOKEN_SCORE_MAX == DOWNLOAD_VALIDATOR_FUZZY_DIFFERENT_NAME_SCORE_MAX,
		"Download Validator sequence score bands must be strictly increasing.");
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_MINIMUM_GROUP_FILE_COUNT = 2;
	const uint32 DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_MINIMUM_DISTINCT_IDS = 2;
	const uint8 DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_MAXIMUM_ID_PARTS = 3;

	struct SDownloadValidatorStructuralToken
	{
		int iStart;
		int iLength;
		bool bNumeric;
	};

	struct SDownloadValidatorStructuralTokenList
	{
		enum { MaximumTokens = 64 };

		SDownloadValidatorStructuralTokenList()
			: uCount(0)
		{
		}

		bool Add(const SDownloadValidatorStructuralToken& token)
		{
			if (uCount >= MaximumTokens)
				return false;
			aTokens[uCount++] = token;
			return true;
		}

		bool empty() const { return uCount == 0; }
		size_t size() const { return uCount; }
		const SDownloadValidatorStructuralToken& operator[](size_t uIndex) const { return aTokens[uIndex]; }

		SDownloadValidatorStructuralToken aTokens[MaximumTokens];
		size_t uCount;
	};

	enum EDownloadValidatorBackgroundState
	{
		DownloadValidatorBackgroundIdle = 0,
		DownloadValidatorBackgroundCapture,
		DownloadValidatorBackgroundRecords,
		DownloadValidatorBackgroundPostings
	};

	void QueueDownloadValidatorSearchResultsRefresh()
	{
		if (!theApp.IsClosing())
			theApp.QueueSearchResultsChangedEvent(0, _T("download-validator-refresh"), false);
	}

	void QueueDownloadValidatorSearchResultsSoftRefresh()
	{
		if (!theApp.IsClosing())
			theApp.QueueSearchResultsChangedEvent(0, _T("download-validator-soft-refresh"), false);
	}

	void GetDownloadValidatorReloadSliceLimits(DWORD& dwSliceBudgetMs, UINT& uMaxItemsPerSlice)
	{
		const UINT uQueueStatus = HIWORD(::GetQueueStatus(QS_KEY | QS_MOUSE | QS_PAINT));
		const bool bInputPending = (uQueueStatus & (QS_KEY | QS_MOUSE)) != 0;
		const bool bPaintPending = (uQueueStatus & QS_PAINT) != 0;

		if (bInputPending) {
			dwSliceBudgetMs = 3;
			uMaxItemsPerSlice = 4096;
			return;
		}
		if (bPaintPending) {
			dwSliceBudgetMs = 5;
			uMaxItemsPerSlice = 16384;
			return;
		}
		dwSliceBudgetMs = 24;
		uMaxItemsPerSlice = 65536;
	}

	bool IsDownloadValidatorFileMatch(const CDownloadValidator::FileCandidateType& fileInfo, const uchar* hash, const EMFileSize filesize)
	{
		return fileInfo.uSize == filesize && md4equ(fileInfo.ucHash, hash);
	}

	bool IsDownloadValidatorFuzzyCandidateBetter(const CDownloadValidator::FuzzyCandidateType& first, const CDownloadValidator::FuzzyCandidateType& second)
	{
		if (first.uSimilarityScore != second.uSimilarityScore)
			return first.uSimilarityScore > second.uSimilarityScore;
		if (first.bStructuralIdentityMatch != second.bStructuralIdentityMatch)
			return first.bStructuralIdentityMatch;
		if (first.bExactSubstring != second.bExactSubstring)
			return first.bExactSubstring;
		if (first.uSharedGramCount != second.uSharedGramCount)
			return first.uSharedGramCount > second.uSharedGramCount;
		if (first.uSize != second.uSize)
			return first.uSize > second.uSize;
		return first.uRecordID < second.uRecordID;
	}

	bool IsDownloadValidatorReloadSliceComplete(DWORD dwSliceStart, DWORD dwSliceBudgetMs, UINT uMaxItemsPerSlice, UINT uProcessed)
	{
		return uProcessed >= uMaxItemsPerSlice || ((uProcessed & 0x1F) == 0 && static_cast<DWORD>(::GetTickCount() - dwSliceStart) >= dwSliceBudgetMs);
	}

	bool IsWholeNameRegexPattern(const CString& strPattern)
	{
		const int iLength = strPattern.GetLength();
		if (iLength < 2 || strPattern[0] != _T('^') || strPattern[iLength - 1] != _T('$'))
			return false;

		int iBackslashCount = 0;
		for (int i = iLength - 2; i >= 0 && strPattern[i] == _T('\\'); --i)
			++iBackslashCount;
		return (iBackslashCount & 1) == 0;
	}

	bool ContainsDownloadValidatorDigit(const CString& strText)
	{
		for (int i = 0; i < strText.GetLength(); ++i) {
			if (_istdigit(strText[i]))
				return true;
		}
		return false;
	}

	void SetDownloadValidatorFileInfo(CDownloadValidator::FileCandidateType& fileInfo, const uchar* hash, const CString& filename, const EMFileSize filesize,
		uint32 uMediaLengthSec, CDownloadValidator::EFuzzyMediaLengthSource eMediaLengthSource)
	{
		fileInfo.strName = filename;
		fileInfo.uSize = filesize;
		fileInfo.uMediaLengthSec = uMediaLengthSec;
		fileInfo.eMediaLengthSource = eMediaLengthSource;
		md4cpy(fileInfo.ucHash, hash);
	}

	void MergeDownloadValidatorFileMetadata(CDownloadValidator::FileCandidateType& fileInfo, uint32 uMediaLengthSec, CDownloadValidator::EFuzzyMediaLengthSource eMediaLengthSource)
	{
		if (eMediaLengthSource != CDownloadValidator::FuzzyMediaLengthUnknown && eMediaLengthSource >= fileInfo.eMediaLengthSource) {
			fileInfo.uMediaLengthSec = uMediaLengthSec;
			fileInfo.eMediaLengthSource = eMediaLengthSource;
		}
	}

	uint32 CalculateDownloadValidatorEditSimilarity(const CString& strFirst, const CString& strSecond)
	{
		const int iFirstLength = strFirst.GetLength();
		const int iSecondLength = strSecond.GetLength();
		const int iMaximumLength = (std::max)(iFirstLength, iSecondLength);
		if (iMaximumLength == 0)
			return DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX;
		if (iMaximumLength > DOWNLOAD_VALIDATOR_FUZZY_MAXIMUM_EDIT_LENGTH)
			return 0;

		const uint32 uMaximumDistance = (std::min)(DOWNLOAD_VALIDATOR_FUZZY_MAXIMUM_EDIT_DISTANCE, (std::max)(2u, static_cast<uint32>(iMaximumLength / 4)));
		if ((iFirstLength > iSecondLength ? iFirstLength - iSecondLength : iSecondLength - iFirstLength) > static_cast<int>(uMaximumDistance))
			return 0;

		const uint32 uOutsideBand = uMaximumDistance + 1;
		uint32 previousStorage[DOWNLOAD_VALIDATOR_FUZZY_MAXIMUM_EDIT_LENGTH + 1];
		uint32 currentStorage[DOWNLOAD_VALIDATOR_FUZZY_MAXIMUM_EDIT_LENGTH + 1];
		uint32* previous = previousStorage;
		uint32* current = currentStorage;
		std::fill(previous, previous + iSecondLength + 1, uOutsideBand);
		for (int j = 0; j <= (std::min)(iSecondLength, static_cast<int>(uMaximumDistance)); ++j)
			previous[j] = static_cast<uint32>(j);

		for (int i = 1; i <= iFirstLength; ++i) {
			std::fill(current, current + iSecondLength + 1, uOutsideBand);
			if (i <= static_cast<int>(uMaximumDistance))
				current[0] = static_cast<uint32>(i);
			const int iStart = (std::max)(1, i - static_cast<int>(uMaximumDistance));
			const int iEnd = (std::min)(iSecondLength, i + static_cast<int>(uMaximumDistance));
			uint32 uRowMinimum = uOutsideBand;
			for (int j = iStart; j <= iEnd; ++j) {
				const uint32 uSubstitutionCost = strFirst.GetAt(i - 1) == strSecond.GetAt(j - 1) ? 0 : 1;
				const uint32 uValue = (std::min)((std::min)(previous[j] + 1, current[j - 1] + 1), previous[j - 1] + uSubstitutionCost);
				current[j] = uValue;
				uRowMinimum = (std::min)(uRowMinimum, uValue);
			}
			if (uRowMinimum > uMaximumDistance)
				return 0;
			std::swap(previous, current);
		}

		const uint32 uDistance = previous[iSecondLength];
		if (uDistance > uMaximumDistance)
			return 0;
		return static_cast<uint32>((static_cast<uint64>(iMaximumLength - static_cast<int>(uDistance)) * DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX) / iMaximumLength);
	}

	struct SDownloadValidatorTokenSequenceMatch
	{
		uint32 uLongestRunTokens = 0;
		uint32 uTotalRunTokens = 0;
		uint32 uRunCount = 0;
		std::vector<uint8> queryMatched;
	};

	SDownloadValidatorTokenSequenceMatch CalculateDownloadValidatorTokenSequenceMatch(const std::vector<uint64>& first, const std::vector<uint64>& second)
	{
		SDownloadValidatorTokenSequenceMatch result;
		if (first.size() < 2 || second.size() < 2)
			return result;

		struct STokenSequenceRun
		{
			size_t uFirstStart;
			size_t uSecondStart;
			size_t uLength;
		};
		std::vector<STokenSequenceRun> runs;
		for (size_t uFirst = 0; uFirst < first.size(); ++uFirst) {
			if (first[uFirst] == 0)
				continue;
			for (size_t uSecond = 0; uSecond < second.size(); ++uSecond) {
				if (second[uSecond] == 0 || first[uFirst] != second[uSecond]
					|| (uFirst != 0 && uSecond != 0 && first[uFirst - 1] != 0 && first[uFirst - 1] == second[uSecond - 1]))
					continue;
				size_t uLength = 0;
				while (uFirst + uLength < first.size() && uSecond + uLength < second.size()
					&& first[uFirst + uLength] != 0 && first[uFirst + uLength] == second[uSecond + uLength])
					++uLength;
				if (uLength >= 2) {
					STokenSequenceRun run;
					run.uFirstStart = uFirst;
					run.uSecondStart = uSecond;
					run.uLength = uLength;
					runs.push_back(run);
				}
			}
		}
		if (runs.empty())
			return result;

		std::sort(runs.begin(), runs.end(), [](const STokenSequenceRun& firstRun, const STokenSequenceRun& secondRun) {
			if (firstRun.uLength != secondRun.uLength)
				return firstRun.uLength > secondRun.uLength;
			if (firstRun.uFirstStart != secondRun.uFirstStart)
				return firstRun.uFirstStart < secondRun.uFirstStart;
			return firstRun.uSecondStart < secondRun.uSecondStart;
		});

		std::vector<uint8> firstUsed(first.size(), 0);
		std::vector<uint8> secondUsed(second.size(), 0);
		for (std::vector<STokenSequenceRun>::const_iterator it = runs.begin(); it != runs.end(); ++it) {
			bool bOverlaps = false;
			for (size_t i = 0; i < it->uLength; ++i) {
				if (firstUsed[it->uFirstStart + i] != 0 || secondUsed[it->uSecondStart + i] != 0) {
					bOverlaps = true;
					break;
				}
			}
			if (bOverlaps)
				continue;
			for (size_t i = 0; i < it->uLength; ++i) {
				firstUsed[it->uFirstStart + i] = 1;
				secondUsed[it->uSecondStart + i] = 1;
			}
			result.uLongestRunTokens = (std::max)(result.uLongestRunTokens, static_cast<uint32>(it->uLength));
			result.uTotalRunTokens += static_cast<uint32>(it->uLength);
			++result.uRunCount;
		}
		result.queryMatched.swap(firstUsed);
		return result;
	}

	uint32 CalculateDownloadValidatorLengthSimilarity(const CString& first, const CString& second)
	{
		const uint32 uFirstLength = static_cast<uint32>(first.GetLength());
		const uint32 uSecondLength = static_cast<uint32>(second.GetLength());
		const uint32 uLongerLength = (std::max)(uFirstLength, uSecondLength);
		if (uLongerLength == 0)
			return DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX;
		return static_cast<uint32>((static_cast<uint64>((std::min)(uFirstLength, uSecondLength)) * DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX) / uLongerLength);
	}

	uint64 HashDownloadValidatorTextRange(const CString& strText, int iStart, int iLength)
	{
		uint64 uHash = 1469598103934665603ui64;
		for (int i = 0; i < iLength; ++i) {
			const uint16 uCharacter = static_cast<uint16>(strText.GetAt(iStart + i));
			uHash ^= static_cast<uint8>(uCharacter & 0xFF);
			uHash *= 1099511628211ui64;
			uHash ^= static_cast<uint8>(uCharacter >> 8);
			uHash *= 1099511628211ui64;
		}
		return uHash;
	}

	uint32 CountDownloadValidatorSetBits(uint64 uValue)
	{
		uint32 uCount = 0;
		while (uValue != 0) {
			uValue &= uValue - 1;
			++uCount;
		}
		return uCount;
	}

	bool IsDownloadValidatorOpeningBracket(TCHAR ch)
	{
		return ch == _T('(') || ch == _T('[') || ch == _T('{');
	}

	bool IsDownloadValidatorClosingBracket(TCHAR ch)
	{
		return ch == _T(')') || ch == _T(']') || ch == _T('}');
	}

	bool HasDownloadValidatorHyphenSeparator(const CString& strText, int iStart, int iEnd)
	{
		bool bHasHyphen = false;
		for (int i = iStart; i < iEnd; ++i) {
			const TCHAR ch = strText.GetAt(i);
			if (IsDownloadValidatorOpeningBracket(ch) || IsDownloadValidatorClosingBracket(ch))
				return false;
			if (ch == _T('-'))
				bHasHyphen = true;
		}
		return bHasHyphen;
	}

	bool IsValidatorDigit(TCHAR ch)
	{
		return ch >= _T('0') && ch <= _T('9');
	}

	TCHAR GetDownloadValidatorTagCloseChar(TCHAR chOpen)
	{
		switch (chOpen) {
			case _T('('): return _T(')');
			case _T('['): return _T(']');
			case _T('{'): return _T('}');
			case _T('<'): return _T('>');
		}
		return 0;
	}

	CString RemoveDownloadValidatorStructuralTags(const CString& strInput, bool bPreserveNumericTags)
	{
		CString strOutput;
		strOutput.Preallocate(strInput.GetLength());
		for (int i = 0; i < strInput.GetLength(); ++i) {
			const TCHAR chOpen = strInput.GetAt(i);
			const TCHAR chClose = GetDownloadValidatorTagCloseChar(chOpen);
			if (chClose == 0) {
				strOutput.AppendChar(chOpen);
				continue;
			}

			const int iClose = strInput.Find(chClose, i + 1);
			if (iClose < 0) {
				strOutput.AppendChar(chOpen);
				continue;
			}

			bool bNumericOnly = iClose > i + 1;
			for (int j = i + 1; j < iClose && bNumericOnly; ++j)
				bNumericOnly = IsValidatorDigit(strInput.GetAt(j));
			if (bPreserveNumericTags && bNumericOnly) {
				const bool bPreserveBracket = IsDownloadValidatorOpeningBracket(chOpen);
				if (bPreserveBracket)
					strOutput.AppendChar(chOpen);
				strOutput.Append(strInput.Mid(i + 1, iClose - i - 1));
				if (bPreserveBracket)
					strOutput.AppendChar(chClose);
			}
			i = iClose;
		}
		return strOutput;
	}

	bool IsValidatorDateSeparator(TCHAR ch)
	{
		return !IsValidatorDigit(ch) && !_istalnum(ch);
	}

	bool IsValidatorDateTimeSeparator(TCHAR ch)
	{
		return ch == _T('T') || ch == _T('t') || IsValidatorDateSeparator(ch);
	}

	bool IsDownloadValidatorFollowingNumberSeparator(TCHAR ch)
	{
		switch (ch) {
			case _T(' '):
			case _T('-'):
			case _T('_'):
			case _T(','):
			case _T('.'):
			case _T(';'):
			case _T(':'):
			case _T('+'):
			case _T('%'):
				return true;
		}
		return false;
	}

	int CountValidatorDigitRun(const CString& strText, int iStart)
	{
		int iPos = iStart;
		while (iPos < strText.GetLength() && IsValidatorDigit(strText.GetAt(iPos)))
			++iPos;
		return iPos - iStart;
	}

	int ParseValidatorDigits(const CString& strText, int iStart, int iLength)
	{
		int iValue = 0;
		for (int i = 0; i < iLength; ++i)
			iValue = (iValue * 10) + (strText.GetAt(iStart + i) - _T('0'));
		return iValue;
	}

	bool ReadValidatorNumberPart(const CString& strText, int iStart, int iMinLength, int iMaxLength, SDownloadValidatorNumberPart& part)
	{
		const int iLength = CountValidatorDigitRun(strText, iStart);
		if (iLength < iMinLength || iLength > iMaxLength)
			return false;

		part.iLength = iLength;
		part.iValue = ParseValidatorDigits(strText, iStart, iLength);
		return true;
	}

	void GetDownloadValidatorDateTimeOptions(SDownloadValidatorDateTimeOptions& options)
	{
		options.iStartYear = 1900;
		options.iEndYear = 2099;
		options.bUseYearRange = thePrefs.GetDownloadValidatorDateTimeUseYearRange();
		options.bRequireSeconds = thePrefs.GetDownloadValidatorDateTimeCheckSeconds();
		options.bIncludeFollowingNumericValues = thePrefs.GetDownloadValidatorDateTimeIncludeFollowingNumericValues();
		if (options.bUseYearRange) {
			options.iStartYear = thePrefs.GetDownloadValidatorDateTimeYearStart();
			options.iEndYear = thePrefs.GetDownloadValidatorDateTimeYearEnd();
			if (options.iStartYear > options.iEndYear) {
				const int iTemp = options.iStartYear;
				options.iStartYear = options.iEndYear;
				options.iEndYear = iTemp;
			}
		}
	}

	bool IsDownloadValidatorYearAllowed(const SDownloadValidatorDateTimeOptions& options, int iYear)
	{
		return iYear >= options.iStartYear && iYear <= options.iEndYear;
	}

	bool ResolveDownloadValidatorYear(const SDownloadValidatorDateTimeOptions& options, int iValue, int iLength, int& iYear)
	{
		if (iLength == 4) {
			iYear = iValue;
			return IsDownloadValidatorYearAllowed(options, iYear);
		}
		if (iLength != 2)
			return false;

		const int iYear2000 = 2000 + iValue;
		const int iYear1900 = 1900 + iValue;
		if (options.bUseYearRange) {
			if (IsDownloadValidatorYearAllowed(options, iYear2000)) {
				iYear = iYear2000;
				return true;
			}
			if (IsDownloadValidatorYearAllowed(options, iYear1900)) {
				iYear = iYear1900;
				return true;
			}
			return false;
		}

		iYear = iValue <= 69 ? iYear2000 : iYear1900;
		return IsDownloadValidatorYearAllowed(options, iYear);
	}

	int GetDownloadValidatorDaysInMonth(int iYear, int iMonth)
	{
		static const int s_aiDaysInMonth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
		if (iMonth == 2 && ((iYear % 4 == 0 && iYear % 100 != 0) || iYear % 400 == 0))
			return 29;
		return s_aiDaysInMonth[iMonth - 1];
	}

	bool IsDownloadValidatorDateValid(const SDownloadValidatorDateTimeOptions& options, int iYear, int iMonth, int iDay)
	{
		if (!IsDownloadValidatorYearAllowed(options, iYear) || iMonth < 1 || iMonth > 12 || iDay < 1)
			return false;
		return iDay <= GetDownloadValidatorDaysInMonth(iYear, iMonth);
	}

	bool TryResolveDownloadValidatorMonthDay(const SDownloadValidatorDateTimeOptions& options, int iYear, int iFirst, int iSecond, bool bPreferFirstAsMonth)
	{
		if (iFirst > 12 && iSecond <= 12)
			return IsDownloadValidatorDateValid(options, iYear, iSecond, iFirst);
		if (iSecond > 12 && iFirst <= 12)
			return IsDownloadValidatorDateValid(options, iYear, iFirst, iSecond);

		if (bPreferFirstAsMonth) {
			if (IsDownloadValidatorDateValid(options, iYear, iFirst, iSecond))
				return true;
			return IsDownloadValidatorDateValid(options, iYear, iSecond, iFirst);
		}

		if (IsDownloadValidatorDateValid(options, iYear, iSecond, iFirst))
			return true;
		return IsDownloadValidatorDateValid(options, iYear, iFirst, iSecond);
	}

	bool TryResolveDownloadValidatorDateParts(const SDownloadValidatorDateTimeOptions& options, const SDownloadValidatorNumberPart& part1, const SDownloadValidatorNumberPart& part2, const SDownloadValidatorNumberPart& part3)
	{
		int iYear = 0;
		if (ResolveDownloadValidatorYear(options, part1.iValue, part1.iLength, iYear) && TryResolveDownloadValidatorMonthDay(options, iYear, part2.iValue, part3.iValue, true))
			return true;
		if (ResolveDownloadValidatorYear(options, part3.iValue, part3.iLength, iYear) && TryResolveDownloadValidatorMonthDay(options, iYear, part1.iValue, part2.iValue, true))
			return true;
		return ResolveDownloadValidatorYear(options, part2.iValue, part2.iLength, iYear) && TryResolveDownloadValidatorMonthDay(options, iYear, part1.iValue, part3.iValue, true);
	}

	bool TryResolveDownloadValidatorCompactDate(const SDownloadValidatorDateTimeOptions& options, const CString& strText, int iStart, int iDateLength)
	{
		SDownloadValidatorNumberPart part1;
		SDownloadValidatorNumberPart part2;
		SDownloadValidatorNumberPart part3;

		if (iDateLength == 8) {
			part1.iLength = 4;
			part1.iValue = ParseValidatorDigits(strText, iStart, 4);
			part2.iLength = 2;
			part2.iValue = ParseValidatorDigits(strText, iStart + 4, 2);
			part3.iLength = 2;
			part3.iValue = ParseValidatorDigits(strText, iStart + 6, 2);
			if (TryResolveDownloadValidatorDateParts(options, part1, part2, part3))
				return true;

			part1.iLength = 2;
			part1.iValue = ParseValidatorDigits(strText, iStart, 2);
			part2.iLength = 2;
			part2.iValue = ParseValidatorDigits(strText, iStart + 2, 2);
			part3.iLength = 4;
			part3.iValue = ParseValidatorDigits(strText, iStart + 4, 4);
			return TryResolveDownloadValidatorDateParts(options, part1, part2, part3);
		}

		if (iDateLength == 6) {
			part1.iLength = 2;
			part1.iValue = ParseValidatorDigits(strText, iStart, 2);
			part2.iLength = 2;
			part2.iValue = ParseValidatorDigits(strText, iStart + 2, 2);
			part3.iLength = 2;
			part3.iValue = ParseValidatorDigits(strText, iStart + 4, 2);
			return TryResolveDownloadValidatorDateParts(options, part1, part2, part3);
		}

		return false;
	}

	bool IsDownloadValidatorTimeValid(int iHour, int iMinute, int iSecond)
	{
		return iHour >= 0 && iHour <= 23 && iMinute >= 0 && iMinute <= 59 && iSecond >= 0 && iSecond <= 59;
	}

	bool TryReadDownloadValidatorSeparatedTimeAt(const CString& strText, int iStart, bool bRequireSeconds, int& iCompareEnd, int& iTimeEnd)
	{
		SDownloadValidatorNumberPart hour;
		SDownloadValidatorNumberPart minute;
		SDownloadValidatorNumberPart second;
		if (!ReadValidatorNumberPart(strText, iStart, 1, 2, hour))
			return false;
		int iPos = iStart + hour.iLength;
		if (iPos >= strText.GetLength() || !IsValidatorDateSeparator(strText.GetAt(iPos)))
			return false;
		++iPos;
		if (!ReadValidatorNumberPart(strText, iPos, 1, 2, minute))
			return false;
		iPos += minute.iLength;
		if (!IsDownloadValidatorTimeValid(hour.iValue, minute.iValue, 0))
			return false;

		iCompareEnd = iPos;
		iTimeEnd = iPos;
		bool bHasSeconds = false;
		if (iPos < strText.GetLength() && IsValidatorDateSeparator(strText.GetAt(iPos))) {
			const int iSecondStart = iPos + 1;
			if (ReadValidatorNumberPart(strText, iSecondStart, 1, 2, second) && IsDownloadValidatorTimeValid(hour.iValue, minute.iValue, second.iValue)) {
				bHasSeconds = true;
				iTimeEnd = iSecondStart + second.iLength;
				if (bRequireSeconds)
					iCompareEnd = iTimeEnd;
			}
		}

		return !bRequireSeconds || bHasSeconds;
	}

	bool TryReadDownloadValidatorCompactTimeAt(const CString& strText, int iStart, bool bRequireSeconds, int& iCompareEnd, int& iTimeEnd)
	{
		const int iAvailableDigits = CountValidatorDigitRun(strText, iStart);
		if (iAvailableDigits < 4)
			return false;

		const int iHour = ParseValidatorDigits(strText, iStart, 2);
		const int iMinute = ParseValidatorDigits(strText, iStart + 2, 2);
		if (!IsDownloadValidatorTimeValid(iHour, iMinute, 0))
			return false;

		iCompareEnd = iStart + 4;
		iTimeEnd = iCompareEnd;
		bool bHasSeconds = false;
		if (iAvailableDigits >= 6) {
			const int iSecond = ParseValidatorDigits(strText, iStart + 4, 2);
			if (IsDownloadValidatorTimeValid(iHour, iMinute, iSecond)) {
				bHasSeconds = true;
				iTimeEnd = iStart + 6;
				if (bRequireSeconds)
					iCompareEnd = iTimeEnd;
			}
		}

		return !bRequireSeconds || bHasSeconds;
	}

	bool TryReadDownloadValidatorTimeAt(const CString& strText, int iStart, bool bRequireSeconds, int& iCompareEnd, int& iTimeEnd)
	{
		return TryReadDownloadValidatorSeparatedTimeAt(strText, iStart, bRequireSeconds, iCompareEnd, iTimeEnd) || TryReadDownloadValidatorCompactTimeAt(strText, iStart, bRequireSeconds, iCompareEnd, iTimeEnd);
	}

	bool TryReadDownloadValidatorTimeAfterDate(const CString& strText, int iDateEnd, bool bRequireSeconds, int& iCompareEnd, int& iTimeEnd)
	{
		int iPos = iDateEnd;
		while (iPos < strText.GetLength() && IsValidatorDateTimeSeparator(strText.GetAt(iPos)) && iPos - iDateEnd < 4)
			++iPos;

		if (iPos >= strText.GetLength() || !IsValidatorDigit(strText.GetAt(iPos)))
			return false;
		return TryReadDownloadValidatorTimeAt(strText, iPos, bRequireSeconds, iCompareEnd, iTimeEnd);
	}

	bool GetDownloadValidatorFollowingNumericValueRange(const CString& strText, int iStart, int& iFollowingStart, int& iFollowingEnd)
	{
		const int iLength = strText.GetLength();
		if (iStart >= iLength)
			return false;

		int iPos = iStart;
		if (IsValidatorDigit(strText.GetAt(iPos))) {
			iPos += CountValidatorDigitRun(strText, iPos);
		}
		else {
			if (!IsDownloadValidatorFollowingNumberSeparator(strText.GetAt(iPos)) || iPos + 1 >= iLength || !IsValidatorDigit(strText.GetAt(iPos + 1)))
				return false;
			++iPos;
			iPos += CountValidatorDigitRun(strText, iPos);
		}

		while (iPos < iLength && IsDownloadValidatorFollowingNumberSeparator(strText.GetAt(iPos))) {
			const int iDigitStart = iPos + 1;
			if (iDigitStart >= iLength || !IsValidatorDigit(strText.GetAt(iDigitStart)))
				break;
			iPos = iDigitStart + CountValidatorDigitRun(strText, iDigitStart);
		}

		iFollowingStart = iStart;
		iFollowingEnd = iPos;
		return true;
	}

	CString BuildDownloadValidatorCanonicalDateTimeText(const CString& strText, int iStart, int iEnd)
	{
		CString strCanonical;
		for (int i = iStart; i < iEnd; ++i) {
			const TCHAR ch = strText.GetAt(i);
			if (IsValidatorDigit(ch))
				strCanonical += ch;
		}
		return strCanonical;
	}

	bool BuildDownloadValidatorDateTimeMatch(const SDownloadValidatorDateTimeOptions& options, const CString& strText, int iDateStart, int iDateEnd, SDownloadValidatorDateTimeMatch& match)
	{
		int iTimeCompareEnd = 0;
		int iTimeEnd = 0;
		if (!TryReadDownloadValidatorTimeAfterDate(strText, iDateEnd, options.bRequireSeconds, iTimeCompareEnd, iTimeEnd))
			return false;

		const CString strCanonicalDateTime(BuildDownloadValidatorCanonicalDateTimeText(strText, iDateStart, iTimeCompareEnd));
		if (strCanonicalDateTime.IsEmpty())
			return false;

		if (options.bIncludeFollowingNumericValues) {
			int iFollowingStart = 0;
			int iFollowingEnd = 0;
			if (GetDownloadValidatorFollowingNumericValueRange(strText, iTimeEnd, iFollowingStart, iFollowingEnd)) {
				match.strCompareText = strCanonicalDateTime;
				match.strCompareText += _T("|");
				match.strCompareText += strText.Mid(iFollowingStart, iFollowingEnd - iFollowingStart);
				return true;
			}
		}

		match.strCompareText = strCanonicalDateTime;
		return true;
	}

	bool TryBuildDownloadValidatorSeparatedDateTimeMatch(const SDownloadValidatorDateTimeOptions& options, const CString& strText, int iStart, SDownloadValidatorDateTimeMatch& match)
	{
		SDownloadValidatorNumberPart part1;
		SDownloadValidatorNumberPart part2;
		SDownloadValidatorNumberPart part3;
		if (!ReadValidatorNumberPart(strText, iStart, 1, 4, part1))
			return false;

		int iPos = iStart + part1.iLength;
		if (iPos >= strText.GetLength() || !IsValidatorDateSeparator(strText.GetAt(iPos)))
			return false;
		++iPos;
		if (!ReadValidatorNumberPart(strText, iPos, 1, 4, part2))
			return false;
		iPos += part2.iLength;
		if (iPos >= strText.GetLength() || !IsValidatorDateSeparator(strText.GetAt(iPos)))
			return false;
		++iPos;
		if (!ReadValidatorNumberPart(strText, iPos, 1, 4, part3))
			return false;
		iPos += part3.iLength;

		if (!TryResolveDownloadValidatorDateParts(options, part1, part2, part3))
			return false;
		return BuildDownloadValidatorDateTimeMatch(options, strText, iStart, iPos, match);
	}

	bool TryBuildDownloadValidatorCompactDateTimeMatch(const SDownloadValidatorDateTimeOptions& options, const CString& strText, int iStart, int iAvailableDigits, SDownloadValidatorDateTimeMatch& match)
	{
		if (iAvailableDigits >= 8 && TryResolveDownloadValidatorCompactDate(options, strText, iStart, 8) && BuildDownloadValidatorDateTimeMatch(options, strText, iStart, iStart + 8, match))
			return true;
		return iAvailableDigits >= 6 && TryResolveDownloadValidatorCompactDate(options, strText, iStart, 6) && BuildDownloadValidatorDateTimeMatch(options, strText, iStart, iStart + 6, match);
	}

	bool TryBuildDownloadValidatorDateTimeMatch(const SDownloadValidatorDateTimeOptions& options, const CString& strText, SDownloadValidatorDateTimeMatch& match)
	{
		const int iLength = strText.GetLength();
		for (int i = 0; i < iLength;) {
			if (!IsValidatorDigit(strText.GetAt(i))) {
				++i;
				continue;
			}

			const int iDigitRunLength = CountValidatorDigitRun(strText, i);
			if (iDigitRunLength <= 4 && TryBuildDownloadValidatorSeparatedDateTimeMatch(options, strText, i, match))
				return true;
			if (iDigitRunLength >= 6 && TryBuildDownloadValidatorCompactDateTimeMatch(options, strText, i, iDigitRunLength, match))
				return true;

			i += iDigitRunLength;
		}
		return false;
	}
}

struct CDownloadValidator::SReloadMapState
{
	enum EPhase
	{
		PhaseDownloads,
		PhaseKnownFiles,
		PhaseDuplicateFiles,
		PhaseFuzzyPrepare,
		PhaseDone
	};

	struct SPendingFuzzyRecord
	{
		FileCandidateType fileInfo;
		uint8 uSourceFlags = FuzzyFileSourceUnknown;
	};

	explicit SReloadMapState(bool bRegexOnlyReload)
		: ePhase(PhaseDownloads)
		, uDownloadIndex(0)
		, pKnownPair(NULL)
		, itDuplicate()
		, uFuzzyPostingCount(0)
		, bKnownIterationStarted(false)
		, bDuplicateIterationStarted(false)
		, bFuzzyPrepareStarted(false)
		, bFuzzyRestartRequired(false)
		, bTraversalRestartRequired(false)
		, bRegexOnly(bRegexOnlyReload)
	{
	}

	EPhase ePhase;
	size_t uDownloadIndex;
	const CKnownFilesMap::CPair* pKnownPair;
	CKnownFileList::KnownFileList::const_iterator itDuplicate;
	uint64 uFuzzyPostingCount;
	FuzzyGramIndex::iterator itFuzzyGram;
	std::vector<SPendingFuzzyRecord> pendingFuzzyRecords;
	bool bKnownIterationStarted;
	bool bDuplicateIterationStarted;
	bool bFuzzyPrepareStarted;
	bool bFuzzyRestartRequired;
	bool bTraversalRestartRequired;
	bool bRegexOnly;
};

CDownloadValidator::CDownloadValidator(void)
	: m_uFuzzyCandidateCacheCandidateCount(0)
	, m_uFuzzyMaximumPostingFrequency(0)
	, m_uFuzzyWeightRecordCount(0)
	, m_uFuzzyKnownTokenDocumentCount(0)
	, m_uFuzzyInactiveRecordCount(0)
	, m_bFuzzyIndexReady(false)
	, m_bFuzzyIndexAvailable(false)
	, m_bFuzzyRebuildRecommended(false)
	, m_lPossibleKnownRevision(0)
	, m_lEvaluationRevision(0)
	, m_lFuzzyCandidateDataRevision(0)
	, m_lRegexReloadActive(0)
	, m_lMapInitialized(0)
	, m_lFuzzyIndexReadySnapshot(0)
	, m_bStartupKnownFilesMapLoadActive(false)
	, m_pDeferredFuzzyCaptureState(NULL)
	, m_pBackgroundFuzzyPrepareState(NULL)
	, m_pBackgroundFuzzyChunk(NULL)
	, m_lDeferredFuzzyCaptureRestartRequired(0)
	, m_lBackgroundFuzzyCaptureComplete(0)
	, m_lBackgroundFuzzyIndexInitialized(0)
	, m_lBackgroundFuzzyState(DownloadValidatorBackgroundIdle)
	, m_lBackgroundOverlayVisible(0)
	, m_lBackgroundFuzzyProcessed(0)
	, m_lBackgroundFuzzyTotal(0)
	, m_lBackgroundWorkQueued(0)
	, m_lBackgroundWorkEnabled(0)
	, m_pReloadMapState(NULL)
{
	m_iDataSize = -1;
	ReloadRegexRules();
}

CDownloadValidator::~CDownloadValidator(void)
{
	CancelDeferredBackgroundWork();
	delete m_pReloadMapState;
	m_pReloadMapState = NULL;
}

bool CDownloadValidator::IsMapInitialized() const
{
	return ::InterlockedCompareExchange(const_cast<LONG*>(&m_lMapInitialized), 0, 0) != 0;
}

void CDownloadValidator::IncrementRevision(volatile LONG* pRevision)
{
	ASSERT(pRevision != NULL);
	if (::InterlockedIncrement(pRevision) == 0)
		::InterlockedIncrement(pRevision);
}

void CDownloadValidator::TouchEvaluationRevision()
{
	IncrementRevision(&m_lEvaluationRevision);
}

void CDownloadValidator::TouchPossibleKnownRevision(bool bCandidateDataChanged)
{
	IncrementRevision(&m_lPossibleKnownRevision);
	TouchEvaluationRevision();
	if (bCandidateDataChanged)
		IncrementRevision(&m_lFuzzyCandidateDataRevision);
}

void CDownloadValidator::NotifyIncrementalMapMutation()
{
	if (!IsMapInitialized())
		return;
	TouchEvaluationRevision();
	IncrementRevision(&m_lFuzzyCandidateDataRevision);
	QueueDownloadValidatorSearchResultsSoftRefresh();
}

void CDownloadValidator::InvalidateEvaluationResults()
{
	TouchEvaluationRevision();
}

void CDownloadValidator::InvalidatePossibleKnownResults(bool bInvalidateEvaluation)
{
	IncrementRevision(&m_lPossibleKnownRevision);
	if (bInvalidateEvaluation)
		TouchEvaluationRevision();
}
void CDownloadValidator::PrepareReloadMapStorage(bool bRegexOnly, UINT uExpectedRecordCount, bool bDeferFuzzyStorage)
{
	UINT uHashSize = max(10000U, uExpectedRecordCount);
	if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL && theApp.emuledlg->transferwnd->GetDownloadList() != NULL)
		uHashSize += static_cast<UINT>(theApp.emuledlg->transferwnd->GetDownloadList()->m_ListItems.size());
	if (theApp.knownfiles != NULL) {
		uHashSize += static_cast<UINT>(theApp.knownfiles->m_Files_map.GetCount());
		CSingleLock slDuplicatesLock(&theApp.knownfiles->m_csDuplicatesLock, TRUE);
		uHashSize += static_cast<UINT>(theApp.knownfiles->m_duplicateFileList.size());
	}
	if (uHashSize < 10000)
		uHashSize = 10000;

	m_iDataSize = static_cast<int>(uHashSize);
	if (!bRegexOnly) {
		m_DownloadValidatorMap.RemoveAll();
		m_DownloadValidatorDateTimeMap.RemoveAll();
		m_DownloadValidatorMap.InitHashTable(uHashSize);
		if (thePrefs.GetDownloadValidatorDateTimeMatching())
			m_DownloadValidatorDateTimeMap.InitHashTable(uHashSize);
		ResetFuzzyIndex(uHashSize, !bDeferFuzzyStorage);
	}
	m_DownloadValidatorRegexMap.RemoveAll();
	if (thePrefs.GetDownloadValidatorRegexMatching() && !m_regexRules.empty())
		m_DownloadValidatorRegexMap.InitHashTable(uHashSize);
	::InterlockedExchange(&m_lMapInitialized, 1);
}

void CDownloadValidator::BeginStartupKnownFilesMapLoad(UINT uExpectedRecordCount)
{
	CSingleLock indexLock(&m_indexLock, TRUE);
	if (theApp.IsClosing() || thePrefs.GetDownloadValidator() == 0)
		return;

	CancelDeferredBackgroundWork();
	delete m_pReloadMapState;
	m_pReloadMapState = NULL;
	::InterlockedExchange(&m_lRegexReloadActive, 0);
	PrepareReloadMapStorage(false, uExpectedRecordCount, true);
	m_bStartupKnownFilesMapLoadActive = true;
}

void CDownloadValidator::AddStartupKnownFileToMap(const uchar* hash, const CString& filename, EMFileSize filesize, uint32 uMediaLengthSec)
{
	CSingleLock indexLock(&m_indexLock, TRUE);
	if (!m_bStartupKnownFilesMapLoadActive || hash == NULL || filename.IsEmpty() || filesize == 0ull)
		return;

	const bool bCollectMediaLength = m_bFuzzyIndexAvailable || thePrefs.GetDownloadValidatorMediaLengthMatching();
	AddTrustedToMapInternal(hash, filename, filesize, bCollectMediaLength ? uMediaLengthSec : 0,
		bCollectMediaLength ? FuzzyMediaLengthLocalMediaInfo : FuzzyMediaLengthUnknown);
}

void CDownloadValidator::AddCurrentDownloadingFilesToMap(bool bAddFuzzy)
{
	if (theApp.emuledlg == NULL || theApp.emuledlg->transferwnd == NULL || theApp.emuledlg->transferwnd->GetDownloadList() == NULL)
		return;

	const bool bCollectMediaLength = m_bFuzzyIndexAvailable || thePrefs.GetDownloadValidatorMediaLengthMatching();
	CDownloadListCtrl* pDownloadList = theApp.emuledlg->transferwnd->GetDownloadList();
	for (CDownloadListCtrl::ListItems::const_iterator it = pDownloadList->m_ListItems.begin(); it != pDownloadList->m_ListItems.end(); ++it) {
		const CtrlItem_Struct* cur_item = it->second;
		if (cur_item == NULL || cur_item->type != FILE_TYPE)
			continue;
		CPartFile* pFile = static_cast<CPartFile*>(cur_item->value);
		if (pFile == NULL)
			continue;
		const uint32 uMediaLengthSec = bCollectMediaLength ? pFile->GetIntTagValue(FT_MEDIA_LENGTH) : 0;
		const EFuzzyMediaLengthSource eMediaLengthSource = uMediaLengthSec != 0 ? FuzzyMediaLengthRemoteMetadata : FuzzyMediaLengthUnknown;
		if (bAddFuzzy)
			AddToMapInternal(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize(), m_bFuzzyIndexAvailable ? FuzzyFileSourceDownloading : FuzzyFileSourceUnknown, uMediaLengthSec, eMediaLengthSource);
		else
			AddTrustedToMapInternal(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize(), uMediaLengthSec, eMediaLengthSource);
	}
}

void CDownloadValidator::AddCurrentRuntimeFilesToMap(bool bAddFuzzy)
{
	AddCurrentDownloadingFilesToMap(bAddFuzzy);
	if (theApp.sharedfiles == NULL)
		return;

	const bool bCollectMediaLength = m_bFuzzyIndexAvailable || thePrefs.GetDownloadValidatorMediaLengthMatching();
	CKnownFilesMap sharedFiles;
	theApp.sharedfiles->CopySharedFileMap(sharedFiles);
	for (const CKnownFilesMap::CPair* pair = sharedFiles.PGetFirstAssoc(); pair != NULL; pair = sharedFiles.PGetNextAssoc(pair)) {
		CKnownFile* pFile = pair->value;
		if (pFile == NULL || pFile->IsPartFile())
			continue;
		const uint32 uMediaLengthSec = bCollectMediaLength ? pFile->GetIntTagValue(FT_MEDIA_LENGTH) : 0;
		const EFuzzyMediaLengthSource eMediaLengthSource = bCollectMediaLength ? FuzzyMediaLengthLocalMediaInfo : FuzzyMediaLengthUnknown;
		if (bAddFuzzy)
			AddToMapInternal(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize(), m_bFuzzyIndexAvailable ? FuzzyFileSourceKnown : FuzzyFileSourceUnknown, uMediaLengthSec, eMediaLengthSource);
		else
			AddTrustedToMapInternal(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize(), uMediaLengthSec, eMediaLengthSource);
	}
}

void CDownloadValidator::CompleteStartupKnownFilesMapLoad()
{
	CSingleLock indexLock(&m_indexLock, TRUE);
	if (!m_bStartupKnownFilesMapLoadActive)
		return;

	m_bStartupKnownFilesMapLoadActive = false;
	AddCurrentDownloadingFilesToMap(false);
	delete m_pReloadMapState;
	m_pReloadMapState = NULL;
	::InterlockedExchange(&m_lRegexReloadActive, 0);
	if (m_bFuzzyIndexAvailable) {
		try {
			delete m_pDeferredFuzzyCaptureState;
			m_pDeferredFuzzyCaptureState = new SReloadMapState(false);
			UINT uTotal = 0;
			if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL && theApp.emuledlg->transferwnd->GetDownloadList() != NULL)
				uTotal = static_cast<UINT>((std::min)(theApp.emuledlg->transferwnd->GetDownloadList()->m_ListItems.size(), static_cast<size_t>(_UI32_MAX)));
			if (theApp.knownfiles != NULL) {
				uTotal = static_cast<UINT>((std::min)(static_cast<uint64>(uTotal) + static_cast<uint64>(theApp.knownfiles->m_Files_map.GetCount()), static_cast<uint64>(_UI32_MAX)));
				CSingleLock duplicateLock(&theApp.knownfiles->m_csDuplicatesLock, TRUE);
				uTotal = static_cast<UINT>((std::min)(static_cast<uint64>(uTotal) + static_cast<uint64>(theApp.knownfiles->m_duplicateFileList.size()), static_cast<uint64>(_UI32_MAX)));
			}
			::InterlockedExchange(&m_lDeferredFuzzyCaptureRestartRequired, 0);
			::InterlockedExchange(&m_lBackgroundFuzzyCaptureComplete, 0);
			::InterlockedExchange(&m_lBackgroundFuzzyIndexInitialized, 0);
			::InterlockedExchange(&m_lBackgroundFuzzyProcessed, 0);
			::InterlockedExchange(&m_lBackgroundFuzzyTotal, static_cast<LONG>((std::min)(uTotal, static_cast<UINT>(LONG_MAX - 1))));
			::InterlockedExchange(&m_lBackgroundOverlayVisible, 1);
			::InterlockedExchange(&m_lBackgroundFuzzyState, DownloadValidatorBackgroundCapture);
		} catch (CMemoryException* ex) {
			ex->Delete();
			AbortFuzzyIndexBuild();
			CancelDeferredBackgroundWork();
		} catch (const std::exception&) {
			AbortFuzzyIndexBuild();
			CancelDeferredBackgroundWork();
		}
	} else {
		::InterlockedExchange(&m_lBackgroundOverlayVisible, 0);
		::InterlockedExchange(&m_lBackgroundFuzzyState, DownloadValidatorBackgroundIdle);
	}
	TouchPossibleKnownRevision();
	QueueDownloadValidatorSearchResultsRefresh();
}

void CDownloadValidator::StartDeferredBackgroundWork()
{
	if (theApp.IsClosing() || ::InterlockedCompareExchange(&m_lBackgroundFuzzyState, 0, 0) == DownloadValidatorBackgroundIdle)
		return;
	::InterlockedExchange(&m_lBackgroundWorkEnabled, 1);
	QueueBackgroundWorker();
	if (theApp.emuledlg != NULL)
		theApp.QueueBulkOperationOverlayRefreshEvent();
}

bool CDownloadValidator::AppendDeferredFuzzySource(std::vector<SFuzzyBuildSource>& sources, const uchar* hash, const CString& filename, EMFileSize filesize,
	uint8 uSourceFlags, uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource)
{
	if (!IsFuzzyMatchingEnabled() || !m_bFuzzyIndexAvailable || hash == NULL || filename.IsEmpty()
		|| filename.GetLength() < thePrefs.GetDownloadValidatorMinimumComparisonLength())
		return true;
	try {
		SFuzzyBuildSource source;
		SetDownloadValidatorFileInfo(source, hash, filename, filesize, uMediaLengthSec, eMediaLengthSource);
		source.uSourceFlags = uSourceFlags;
		sources.push_back(source);
		return true;
	} catch (CMemoryException* ex) {
		ex->Delete();
	} catch (const std::exception&) {
	}
	return false;
}

void CDownloadValidator::QueueDeferredFuzzyChunk(SFuzzyBuildChunk* pChunk, bool bIncreaseTotal)
{
	if (pChunk == NULL)
		return;
	if (pChunk->sources.empty()) {
		delete pChunk;
		return;
	}
	const LONG lSourceCount = static_cast<LONG>((std::min)(pChunk->sources.size(), static_cast<size_t>(LONG_MAX - 1)));
	try {
		CSingleLock queueLock(&m_deferredFuzzyQueueLock, TRUE);
		m_deferredFuzzyChunks.AddTail(pChunk);
	} catch (CMemoryException* ex) {
		ex->Delete();
		delete pChunk;
		CSingleLock indexLock(&m_indexLock, TRUE);
		AbortFuzzyIndexBuild();
		CancelDeferredBackgroundWork();
		return;
	} catch (const std::exception&) {
		delete pChunk;
		CSingleLock indexLock(&m_indexLock, TRUE);
		AbortFuzzyIndexBuild();
		CancelDeferredBackgroundWork();
		return;
	}
	if (bIncreaseTotal)
		::InterlockedExchangeAdd(&m_lBackgroundFuzzyTotal, lSourceCount);
	QueueBackgroundWorker();
}

void CDownloadValidator::QueueDeferredFuzzySource(const uchar* hash, const CString& filename, EMFileSize filesize, uint8 uSourceFlags, uint32 uMediaLengthSec,
	EFuzzyMediaLengthSource eMediaLengthSource)
{
	SFuzzyBuildChunk* pChunk = NULL;
	try {
		pChunk = new SFuzzyBuildChunk();
		pChunk->sources.reserve(1);
		if (!AppendDeferredFuzzySource(pChunk->sources, hash, filename, filesize, uSourceFlags, uMediaLengthSec, eMediaLengthSource)) {
			delete pChunk;
			AbortFuzzyIndexBuild();
			CancelDeferredBackgroundWork();
			return;
		}
		QueueDeferredFuzzyChunk(pChunk, true);
	} catch (CMemoryException* ex) {
		ex->Delete();
		delete pChunk;
		AbortFuzzyIndexBuild();
		CancelDeferredBackgroundWork();
	} catch (const std::exception&) {
		delete pChunk;
		AbortFuzzyIndexBuild();
		CancelDeferredBackgroundWork();
	}
}


void CDownloadValidator::DeactivateDeferredFuzzySource(const uchar* hash, const CString& filename, EMFileSize filesize)
{
	if (m_pBackgroundFuzzyChunk != NULL) {
		for (size_t i = m_pBackgroundFuzzyChunk->uNextSource; i < m_pBackgroundFuzzyChunk->sources.size(); ++i) {
			SFuzzyBuildSource& source = m_pBackgroundFuzzyChunk->sources[i];
			if (source.bActive && IsDownloadValidatorFileMatch(source, hash, filesize) && source.strName == filename)
				source.bActive = false;
		}
	}
	CSingleLock queueLock(&m_deferredFuzzyQueueLock, TRUE);
	for (POSITION pos = m_deferredFuzzyChunks.GetHeadPosition(); pos != NULL;) {
		SFuzzyBuildChunk* pChunk = m_deferredFuzzyChunks.GetNext(pos);
		if (pChunk == NULL)
			continue;
		for (size_t i = pChunk->uNextSource; i < pChunk->sources.size(); ++i) {
			SFuzzyBuildSource& source = pChunk->sources[i];
			if (source.bActive && IsDownloadValidatorFileMatch(source, hash, filesize) && source.strName == filename)
				source.bActive = false;
		}
	}
}

bool CDownloadValidator::ProcessDeferredSourceCaptureSlice()
{
	if (theApp.IsClosing() || !theApp.IsUiThread() || ::InterlockedCompareExchange(&m_lBackgroundWorkEnabled, 0, 0) == 0
		|| ::InterlockedCompareExchange(&m_lBackgroundFuzzyState, 0, 0) != DownloadValidatorBackgroundCapture || m_pDeferredFuzzyCaptureState == NULL)
		return false;

	{
		CSingleLock queueLock(&m_deferredFuzzyQueueLock, TRUE);
		if (m_deferredFuzzyChunks.GetCount() >= DOWNLOAD_VALIDATOR_CAPTURE_QUEUE_MAX_CHUNKS) {
			queueLock.Unlock();
			QueueBackgroundWorker();
			return true;
		}
	}
	if (::InterlockedExchange(&m_lDeferredFuzzyCaptureRestartRequired, 0) != 0) {
		SReloadMapState* pRestartState = NULL;
		try {
			pRestartState = new SReloadMapState(false);
		} catch (CMemoryException* ex) {
			ex->Delete();
		} catch (const std::exception&) {
		}
		if (pRestartState == NULL) {
			CSingleLock indexLock(&m_indexLock, TRUE);
			AbortFuzzyIndexBuild();
			CancelDeferredBackgroundWork();
			return false;
		}
		delete m_pDeferredFuzzyCaptureState;
		m_pDeferredFuzzyCaptureState = pRestartState;
	}

	SFuzzyBuildChunk* pChunk = NULL;
	try {
		pChunk = new SFuzzyBuildChunk();
		pChunk->sources.reserve(1024);
	} catch (CMemoryException* ex) {
		ex->Delete();
		delete pChunk;
		CSingleLock indexLock(&m_indexLock, TRUE);
		AbortFuzzyIndexBuild();
		CancelDeferredBackgroundWork();
		return false;
	} catch (const std::exception&) {
		delete pChunk;
		CSingleLock indexLock(&m_indexLock, TRUE);
		AbortFuzzyIndexBuild();
		CancelDeferredBackgroundWork();
		return false;
	}

	const DWORD dwSliceStart = ::GetTickCount();
	UINT uInspected = 0;
	bool bCaptureFailed = false;
	while (m_pDeferredFuzzyCaptureState != NULL && m_pDeferredFuzzyCaptureState->ePhase != SReloadMapState::PhaseDone) {
		if (m_pDeferredFuzzyCaptureState->ePhase == SReloadMapState::PhaseDownloads) {
			CDownloadListCtrl* pDownloadList = theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL ? theApp.emuledlg->transferwnd->GetDownloadList() : NULL;
			if (pDownloadList == NULL) {
				m_pDeferredFuzzyCaptureState->ePhase = SReloadMapState::PhaseKnownFiles;
				continue;
			}
			CDownloadListCtrl::ListItems::const_iterator itDownload = pDownloadList->m_ListItems.begin();
			for (size_t uSkip = 0; itDownload != pDownloadList->m_ListItems.end() && uSkip < m_pDeferredFuzzyCaptureState->uDownloadIndex; ++uSkip)
				++itDownload;
			while (itDownload != pDownloadList->m_ListItems.end()) {
				const CtrlItem_Struct* cur_item = itDownload->second;
				++itDownload;
				++m_pDeferredFuzzyCaptureState->uDownloadIndex;
				if (cur_item != NULL && cur_item->type == FILE_TYPE) {
					CPartFile* pFile = static_cast<CPartFile*>(cur_item->value);
					if (pFile != NULL) {
						const uint32 uMediaLengthSec = pFile->GetIntTagValue(FT_MEDIA_LENGTH);
						bCaptureFailed = !AppendDeferredFuzzySource(pChunk->sources, pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize(), FuzzyFileSourceDownloading,
							uMediaLengthSec, uMediaLengthSec != 0 ? FuzzyMediaLengthRemoteMetadata : FuzzyMediaLengthUnknown);
					}
				}
				++uInspected;
				if (bCaptureFailed || uInspected >= DOWNLOAD_VALIDATOR_CAPTURE_RECORDS_PER_SLICE
					|| ((uInspected & 0x3F) == 0 && static_cast<DWORD>(::GetTickCount() - dwSliceStart) >= DOWNLOAD_VALIDATOR_CAPTURE_SLICE_MS))
					break;
			}
			if (bCaptureFailed || uInspected >= DOWNLOAD_VALIDATOR_CAPTURE_RECORDS_PER_SLICE || static_cast<DWORD>(::GetTickCount() - dwSliceStart) >= DOWNLOAD_VALIDATOR_CAPTURE_SLICE_MS)
				break;
			m_pDeferredFuzzyCaptureState->ePhase = SReloadMapState::PhaseKnownFiles;
			continue;
		}

		if (m_pDeferredFuzzyCaptureState->ePhase == SReloadMapState::PhaseKnownFiles) {
			if (theApp.knownfiles == NULL) {
				m_pDeferredFuzzyCaptureState->ePhase = SReloadMapState::PhaseDuplicateFiles;
				continue;
			}
			if (!m_pDeferredFuzzyCaptureState->bKnownIterationStarted) {
				m_pDeferredFuzzyCaptureState->pKnownPair = theApp.knownfiles->m_Files_map.PGetFirstAssoc();
				m_pDeferredFuzzyCaptureState->bKnownIterationStarted = true;
			}
			while (m_pDeferredFuzzyCaptureState->pKnownPair != NULL) {
				const CKnownFilesMap::CPair* pKnownPair = m_pDeferredFuzzyCaptureState->pKnownPair;
				m_pDeferredFuzzyCaptureState->pKnownPair = theApp.knownfiles->m_Files_map.PGetNextAssoc(pKnownPair);
				CKnownFile* pFile = pKnownPair->value;
				if (pFile != NULL) {
					const uint32 uMediaLengthSec = pFile->GetIntTagValue(FT_MEDIA_LENGTH);
					bCaptureFailed = !AppendDeferredFuzzySource(pChunk->sources, pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize(), FuzzyFileSourceKnown,
						uMediaLengthSec, FuzzyMediaLengthLocalMediaInfo);
				}
				++uInspected;
				if (bCaptureFailed || uInspected >= DOWNLOAD_VALIDATOR_CAPTURE_RECORDS_PER_SLICE
					|| ((uInspected & 0x3F) == 0 && static_cast<DWORD>(::GetTickCount() - dwSliceStart) >= DOWNLOAD_VALIDATOR_CAPTURE_SLICE_MS))
					break;
			}
			if (bCaptureFailed || uInspected >= DOWNLOAD_VALIDATOR_CAPTURE_RECORDS_PER_SLICE || static_cast<DWORD>(::GetTickCount() - dwSliceStart) >= DOWNLOAD_VALIDATOR_CAPTURE_SLICE_MS)
				break;
			m_pDeferredFuzzyCaptureState->ePhase = SReloadMapState::PhaseDuplicateFiles;
			continue;
		}

		if (m_pDeferredFuzzyCaptureState->ePhase == SReloadMapState::PhaseDuplicateFiles) {
			if (theApp.knownfiles == NULL) {
				m_pDeferredFuzzyCaptureState->ePhase = SReloadMapState::PhaseDone;
				continue;
			}
			CSingleLock duplicateLock(&theApp.knownfiles->m_csDuplicatesLock, TRUE);
			if (!m_pDeferredFuzzyCaptureState->bDuplicateIterationStarted) {
				m_pDeferredFuzzyCaptureState->itDuplicate = theApp.knownfiles->m_duplicateFileList.begin();
				m_pDeferredFuzzyCaptureState->bDuplicateIterationStarted = true;
			}
			while (m_pDeferredFuzzyCaptureState->itDuplicate != theApp.knownfiles->m_duplicateFileList.end()) {
				CKnownFile* pFile = *m_pDeferredFuzzyCaptureState->itDuplicate;
				++m_pDeferredFuzzyCaptureState->itDuplicate;
				if (pFile != NULL) {
					const uint32 uMediaLengthSec = pFile->GetIntTagValue(FT_MEDIA_LENGTH);
					bCaptureFailed = !AppendDeferredFuzzySource(pChunk->sources, pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize(), FuzzyFileSourceKnown,
						uMediaLengthSec, FuzzyMediaLengthLocalMediaInfo);
				}
				++uInspected;
				if (bCaptureFailed || uInspected >= DOWNLOAD_VALIDATOR_CAPTURE_RECORDS_PER_SLICE
					|| ((uInspected & 0x3F) == 0 && static_cast<DWORD>(::GetTickCount() - dwSliceStart) >= DOWNLOAD_VALIDATOR_CAPTURE_SLICE_MS))
					break;
			}
			if (bCaptureFailed || uInspected >= DOWNLOAD_VALIDATOR_CAPTURE_RECORDS_PER_SLICE || static_cast<DWORD>(::GetTickCount() - dwSliceStart) >= DOWNLOAD_VALIDATOR_CAPTURE_SLICE_MS)
				break;
			m_pDeferredFuzzyCaptureState->ePhase = SReloadMapState::PhaseDone;
		}
	}

	if (bCaptureFailed) {
		delete pChunk;
		CSingleLock indexLock(&m_indexLock, TRUE);
		AbortFuzzyIndexBuild();
		CancelDeferredBackgroundWork();
		return false;
	}
	const LONG lCaptured = static_cast<LONG>((std::min)(pChunk->sources.size(), static_cast<size_t>(LONG_MAX - 1)));
	if (lCaptured != 0) {
		const LONG lCurrentTotal = ::InterlockedCompareExchange(&m_lBackgroundFuzzyTotal, 0, 0);
		const LONG lProcessed = ::InterlockedCompareExchange(&m_lBackgroundFuzzyProcessed, 0, 0);
		if (lProcessed + lCaptured > lCurrentTotal)
			::InterlockedExchange(&m_lBackgroundFuzzyTotal, lProcessed + lCaptured);
		QueueDeferredFuzzyChunk(pChunk, false);
	} else
		delete pChunk;

	if (m_pDeferredFuzzyCaptureState != NULL && m_pDeferredFuzzyCaptureState->ePhase == SReloadMapState::PhaseDone) {
		delete m_pDeferredFuzzyCaptureState;
		m_pDeferredFuzzyCaptureState = NULL;
		::InterlockedExchange(&m_lBackgroundFuzzyCaptureComplete, 1);
		::InterlockedExchange(&m_lBackgroundFuzzyState, DownloadValidatorBackgroundRecords);
		QueueBackgroundWorker();
		return false;
	}
	return true;
}

void CDownloadValidator::QueueBackgroundWorker()
{
	if (theApp.IsClosing() || ::InterlockedCompareExchange(&m_lBackgroundWorkEnabled, 0, 0) == 0
		|| ::InterlockedCompareExchange(&m_lBackgroundWorkQueued, 1, 0) != 0)
		return;
	if (!theApp.QueueDownloadValidatorCpuWork())
		::InterlockedExchange(&m_lBackgroundWorkQueued, 0);
}

bool CDownloadValidator::GetBackgroundProgress(UINT& uProcessed, UINT& uTotal) const
{
	if (::InterlockedCompareExchange(const_cast<LONG*>(&m_lBackgroundOverlayVisible), 0, 0) == 0) {
		uProcessed = 0;
		uTotal = 0;
		return false;
	}
	const LONG lState = ::InterlockedCompareExchange(const_cast<LONG*>(&m_lBackgroundFuzzyState), 0, 0);
	if (lState == DownloadValidatorBackgroundIdle) {
		uProcessed = 0;
		uTotal = 0;
		return false;
	}
	const LONG lProcessed = ::InterlockedCompareExchange(const_cast<LONG*>(&m_lBackgroundFuzzyProcessed), 0, 0);
	const LONG lTotal = ::InterlockedCompareExchange(const_cast<LONG*>(&m_lBackgroundFuzzyTotal), 0, 0);
	uTotal = static_cast<UINT>((std::max)(1L, lTotal));
	uProcessed = static_cast<UINT>((std::min)((std::max)(0L, lProcessed), static_cast<LONG>(uTotal - 1)));
	return true;
}

void CDownloadValidator::CancelDeferredBackgroundWork()
{
	delete m_pDeferredFuzzyCaptureState;
	m_pDeferredFuzzyCaptureState = NULL;
	delete m_pBackgroundFuzzyPrepareState;
	m_pBackgroundFuzzyPrepareState = NULL;
	delete m_pBackgroundFuzzyChunk;
	m_pBackgroundFuzzyChunk = NULL;
	{
		CSingleLock queueLock(&m_deferredFuzzyQueueLock, TRUE);
		while (!m_deferredFuzzyChunks.IsEmpty())
			delete m_deferredFuzzyChunks.RemoveHead();
	}
	::InterlockedExchange(&m_lDeferredFuzzyCaptureRestartRequired, 0);
	::InterlockedExchange(&m_lBackgroundFuzzyCaptureComplete, 0);
	::InterlockedExchange(&m_lBackgroundFuzzyIndexInitialized, 0);
	::InterlockedExchange(&m_lBackgroundOverlayVisible, 0);
	::InterlockedExchange(&m_lBackgroundFuzzyState, DownloadValidatorBackgroundIdle);
	::InterlockedExchange(&m_lBackgroundFuzzyProcessed, 0);
	::InterlockedExchange(&m_lBackgroundFuzzyTotal, 0);
	::InterlockedExchange(&m_lBackgroundWorkQueued, 0);
	::InterlockedExchange(&m_lBackgroundWorkEnabled, 0);
}

bool CDownloadValidator::ProcessBackgroundWorkSlice()
{
	::InterlockedExchange(&m_lBackgroundWorkQueued, 0);
	if (theApp.IsClosing() || ::InterlockedCompareExchange(&m_lBackgroundWorkEnabled, 0, 0) == 0)
		return false;

	bool bHasMore = false;
	bool bCompleted = false;
	{
		CSingleLock indexLock(&m_indexLock, TRUE);
		LONG lState = ::InterlockedCompareExchange(&m_lBackgroundFuzzyState, 0, 0);
		if ((lState == DownloadValidatorBackgroundCapture || lState == DownloadValidatorBackgroundRecords)
			&& ::InterlockedCompareExchange(&m_lBackgroundFuzzyIndexInitialized, 1, 0) == 0) {
			const LONG lExpectedRecordCount = (std::max)(0L, ::InterlockedCompareExchange(&m_lBackgroundFuzzyTotal, 0, 0));
			ResetFuzzyIndex(static_cast<UINT>(lExpectedRecordCount), true);
		}

		const DWORD dwSliceStart = ::GetTickCount();
		UINT uProcessedInSlice = 0;
		if (lState == DownloadValidatorBackgroundCapture || lState == DownloadValidatorBackgroundRecords) {
			for (;;) {
				if (m_pBackgroundFuzzyChunk == NULL) {
					CSingleLock queueLock(&m_deferredFuzzyQueueLock, TRUE);
					if (!m_deferredFuzzyChunks.IsEmpty())
						m_pBackgroundFuzzyChunk = m_deferredFuzzyChunks.RemoveHead();
				}
				if (m_pBackgroundFuzzyChunk == NULL)
					break;
				while (m_pBackgroundFuzzyChunk->uNextSource < m_pBackgroundFuzzyChunk->sources.size()) {
					const SFuzzyBuildSource& source = m_pBackgroundFuzzyChunk->sources[m_pBackgroundFuzzyChunk->uNextSource++];
					if (source.bActive)
						AddFuzzyRecord(source.ucHash, source.strName, source.uSize, source.uSourceFlags, source.uMediaLengthSec, source.eMediaLengthSource);
					++uProcessedInSlice;
					::InterlockedIncrement(&m_lBackgroundFuzzyProcessed);
					if (uProcessedInSlice >= DOWNLOAD_VALIDATOR_BACKGROUND_RECORDS_PER_SLICE
						|| static_cast<DWORD>(::GetTickCount() - dwSliceStart) >= DOWNLOAD_VALIDATOR_BACKGROUND_SLICE_MS)
						break;
				}
				if (m_pBackgroundFuzzyChunk->uNextSource >= m_pBackgroundFuzzyChunk->sources.size()) {
					delete m_pBackgroundFuzzyChunk;
					m_pBackgroundFuzzyChunk = NULL;
				}
				if (uProcessedInSlice >= DOWNLOAD_VALIDATOR_BACKGROUND_RECORDS_PER_SLICE
					|| static_cast<DWORD>(::GetTickCount() - dwSliceStart) >= DOWNLOAD_VALIDATOR_BACKGROUND_SLICE_MS)
					break;
			}

			bool bQueueEmpty = false;
			{
				CSingleLock queueLock(&m_deferredFuzzyQueueLock, TRUE);
				bQueueEmpty = m_deferredFuzzyChunks.IsEmpty();
			}
			if (m_pBackgroundFuzzyChunk == NULL && bQueueEmpty && ::InterlockedCompareExchange(&m_lBackgroundFuzzyCaptureComplete, 0, 0) != 0) {
				delete m_pBackgroundFuzzyPrepareState;
				m_pBackgroundFuzzyPrepareState = NULL;
				try {
					m_pBackgroundFuzzyPrepareState = new SReloadMapState(false);
					m_pBackgroundFuzzyPrepareState->ePhase = SReloadMapState::PhaseFuzzyPrepare;
					const uint64 uPostingWork = m_fuzzyGramIndex.size();
					const uint64 uRecordWork = static_cast<uint64>((std::max)(0L, ::InterlockedCompareExchange(&m_lBackgroundFuzzyProcessed, 0, 0)));
					::InterlockedExchange(&m_lBackgroundFuzzyTotal, static_cast<LONG>((std::min)(uRecordWork + uPostingWork, static_cast<uint64>(LONG_MAX - 1))));
					::InterlockedExchange(&m_lBackgroundFuzzyState, DownloadValidatorBackgroundPostings);
					lState = DownloadValidatorBackgroundPostings;
				} catch (CMemoryException* ex) {
					ex->Delete();
					AbortFuzzyIndexBuild();
					CancelDeferredBackgroundWork();
					lState = DownloadValidatorBackgroundIdle;
				} catch (const std::exception&) {
					AbortFuzzyIndexBuild();
					CancelDeferredBackgroundWork();
					lState = DownloadValidatorBackgroundIdle;
				}
			}
		}

		if (lState == DownloadValidatorBackgroundPostings && m_pBackgroundFuzzyPrepareState != NULL) {
			UINT uPrepared = 0;
			ProcessFuzzyPostingPreparation(*m_pBackgroundFuzzyPrepareState, ::GetTickCount(), DOWNLOAD_VALIDATOR_BACKGROUND_SLICE_MS, DOWNLOAD_VALIDATOR_BACKGROUND_GRAMS_PER_SLICE, uPrepared, false);
			if (uPrepared != 0)
				::InterlockedExchangeAdd(&m_lBackgroundFuzzyProcessed, static_cast<LONG>((std::min)(uPrepared, static_cast<UINT>(LONG_MAX - 1))));
			if (m_pBackgroundFuzzyPrepareState->ePhase == SReloadMapState::PhaseDone) {
				std::vector<SReloadMapState::SPendingFuzzyRecord> pendingFuzzyRecords;
				pendingFuzzyRecords.swap(m_pBackgroundFuzzyPrepareState->pendingFuzzyRecords);
				delete m_pBackgroundFuzzyPrepareState;
				m_pBackgroundFuzzyPrepareState = NULL;
				for (std::vector<SReloadMapState::SPendingFuzzyRecord>::const_iterator it = pendingFuzzyRecords.begin(); it != pendingFuzzyRecords.end(); ++it)
					AddFuzzyRecord(it->fileInfo.ucHash, it->fileInfo.strName, it->fileInfo.uSize, it->uSourceFlags, it->fileInfo.uMediaLengthSec, it->fileInfo.eMediaLengthSource);
				::InterlockedExchange(&m_lBackgroundOverlayVisible, 0);
				::InterlockedExchange(&m_lBackgroundFuzzyState, DownloadValidatorBackgroundIdle);
				::InterlockedExchange(&m_lBackgroundWorkEnabled, 0);
				bCompleted = true;
			}
		}
		lState = ::InterlockedCompareExchange(&m_lBackgroundFuzzyState, 0, 0);
		if (lState == DownloadValidatorBackgroundPostings || lState == DownloadValidatorBackgroundRecords)
			bHasMore = true;
		else if (lState == DownloadValidatorBackgroundCapture) {
			CSingleLock queueLock(&m_deferredFuzzyQueueLock, TRUE);
			bHasMore = m_pBackgroundFuzzyChunk != NULL || !m_deferredFuzzyChunks.IsEmpty();
		}
	}

	if (theApp.emuledlg != NULL)
		theApp.QueueBulkOperationOverlayRefreshEvent();
	if (bCompleted) {
		TouchPossibleKnownRevision();
		QueueDownloadValidatorSearchResultsRefresh();
		if (theApp.searchlist != NULL)
			theApp.searchlist->RequestDownloadValidatorRecheckForAllSearches();
	}
	if (bHasMore)
		QueueBackgroundWorker();
	return bHasMore;
}

bool CDownloadValidator::IsFuzzyMatchingEnabled() const
{
	return thePrefs.GetDownloadValidator() != 0 && thePrefs.GetDownloadValidatorFuzzyMatching();
}

void CDownloadValidator::ResetFuzzyIndex(UINT uExpectedRecordCount, bool bReserveStorage)
{
	m_fuzzyRecords.clear();
	m_fuzzyStructuralIdentities.clear();
	m_fuzzyStructuralGroupStats.clear();
	m_fuzzyKnownTokenFrequencies.clear();
	m_fuzzyGramIndex.clear();
	m_fuzzyDeltaPostingIndex.clear();
	m_fuzzyIdentityIndex.clear();
	m_fuzzyStructuralYearIDIndex.clear();
	m_fuzzyStructuralIDIndex.clear();
	m_fuzzyPostings.clear();
	m_fuzzyCandidateSharedGramCounts.clear();
	m_fuzzyCandidateSharedGramWeights.clear();
	m_fuzzyCandidateTouchedFlags.clear();
	m_fuzzyTouchedRecordIDs.clear();
	ClearFuzzyCandidateCache();
	m_uFuzzyMaximumPostingFrequency = DOWNLOAD_VALIDATOR_FUZZY_MAXIMUM_POSTING_FREQUENCY;
	m_uFuzzyWeightRecordCount = 0;
	m_uFuzzyKnownTokenDocumentCount = 0;
	m_uFuzzyInactiveRecordCount = 0;
	m_bFuzzyIndexReady = false;
	::InterlockedExchange(&m_lFuzzyIndexReadySnapshot, 0);
	m_bFuzzyIndexAvailable = IsFuzzyMatchingEnabled();
	m_bFuzzyRebuildRecommended = false;

	if (!m_bFuzzyIndexAvailable || !bReserveStorage)
		return;

	try {
		m_fuzzyRecords.reserve(uExpectedRecordCount);
		m_fuzzyStructuralIdentities.reserve((std::min)(uExpectedRecordCount, 65536u));
		m_fuzzyStructuralGroupStats.reserve((std::min)(uExpectedRecordCount / 8 + 1, 65536u));
		m_fuzzyKnownTokenFrequencies.reserve((std::min)(uExpectedRecordCount, 262144u));
		m_fuzzyIdentityIndex.reserve(uExpectedRecordCount);
		m_fuzzyStructuralYearIDIndex.reserve(uExpectedRecordCount);
		m_fuzzyStructuralIDIndex.reserve(uExpectedRecordCount);
		m_fuzzyGramIndex.reserve(uExpectedRecordCount);
		m_fuzzyDeltaPostingIndex.reserve(uExpectedRecordCount);
		m_fuzzyCandidateSharedGramCounts.reserve(uExpectedRecordCount);
		m_fuzzyCandidateSharedGramWeights.reserve(uExpectedRecordCount);
		m_fuzzyCandidateTouchedFlags.reserve(uExpectedRecordCount);
		m_fuzzyTouchedRecordIDs.reserve(uExpectedRecordCount < 4096 ? uExpectedRecordCount : 4096);
	} catch (const std::exception&) {
		AbortFuzzyIndexBuild();
	}
}

CString CDownloadValidator::RemoveMojibakeGarbage(const CString& strInput, bool bPreserveRemovedTokenBarrier) const
{
	if (strInput.IsEmpty())
		return strInput;

	const int iLen = strInput.GetLength();
	CString strResult;
	strResult.Preallocate(iLen);

	const auto IsMojibakeArtifact = [bPreserveRemovedTokenBarrier](uint16 uCharacter) {
		return uCharacter == 0x00C2 || uCharacter == 0x00C3 || uCharacter == 0x00D0 || uCharacter == 0x00D1
			|| (uCharacter >= 0x00A0 && uCharacter <= 0x00BF)
			|| (bPreserveRemovedTokenBarrier && ((uCharacter >= 0x0080 && uCharacter <= 0x009F)
				|| uCharacter == 0x00E2 || uCharacter == 0x00E3 || uCharacter == 0x00F0 || uCharacter == 0x00F1));
	};

	int i = 0;
	while (i < iLen) {
		const int iStart = i;
		while (i < iLen && strInput.GetAt(i) != _T(' ') && strInput.GetAt(i) != _T('\t') && strInput.GetAt(i) != _T('.')) {
			++i;
		}

		if (i > iStart) {
			const int iTokenLen = i - iStart;
			int iMojibakeCount = 0;
			int iConsecutiveMojibake = 0;
			int iMaxConsecutiveMojibake = 0;
			bool bHasControlArtifact = false;
			bool bHasRepeatedMojibakePair = false;

			for (int j = iStart; j < i; ++j) {
				const uint16 uChCurrent = static_cast<uint16>(strInput.GetAt(j));
				const bool bIsControlArtifact = bPreserveRemovedTokenBarrier && uChCurrent >= 0x0080 && uChCurrent <= 0x009F;
				const bool bIsArtifact = IsMojibakeArtifact(uChCurrent);

				if (bIsArtifact) {
					bHasControlArtifact = bHasControlArtifact || bIsControlArtifact;
					++iMojibakeCount;
					++iConsecutiveMojibake;
					if (iConsecutiveMojibake > iMaxConsecutiveMojibake)
						iMaxConsecutiveMojibake = iConsecutiveMojibake;

					if (j + 1 < i) {
						const uint16 uChNext = static_cast<uint16>(strInput.GetAt(j + 1));
						const bool bNextIsArtifact = IsMojibakeArtifact(uChNext);
						if (bNextIsArtifact && j + 3 < i) {
							if (uChCurrent == static_cast<uint16>(strInput.GetAt(j + 2)) && uChNext == static_cast<uint16>(strInput.GetAt(j + 3))) {
								bHasRepeatedMojibakePair = true;
							}
						}
					}
				} else {
					iConsecutiveMojibake = 0;
				}
			}

			const bool bIsGarbageToken = bHasControlArtifact || bHasRepeatedMojibakePair
				|| (iMaxConsecutiveMojibake >= 3)
				|| (iTokenLen >= 4 && (iMojibakeCount * 2 >= iTokenLen));

			if (!bIsGarbageToken)
				strResult.Append(strInput.Mid(iStart, iTokenLen));
			else if (bPreserveRemovedTokenBarrier) {
				if (!strResult.IsEmpty() && strResult.GetAt(strResult.GetLength() - 1) != _T(' '))
					strResult.AppendChar(_T(' '));
				strResult.AppendChar(_T('0'));
			}
		}

		while (i < iLen && (strInput.GetAt(i) == _T(' ') || strInput.GetAt(i) == _T('\t') || strInput.GetAt(i) == _T('.'))) {
			strResult.AppendChar(strInput.GetAt(i));
			++i;
		}
	}

	CString strCleaned;
	strCleaned.Preallocate(strResult.GetLength());
	bool bLastWasSpace = false;
	for (int k = 0; k < strResult.GetLength(); ++k) {
		const TCHAR ch = strResult.GetAt(k);
		if (ch == _T(' ')) {
			if (!bLastWasSpace && !strCleaned.IsEmpty()) {
				strCleaned.AppendChar(ch);
				bLastWasSpace = true;
			}
		} else {
			strCleaned.AppendChar(ch);
			bLastWasSpace = false;
		}
	}

	strCleaned.TrimRight(_T(' '));
	return strCleaned.IsEmpty() && !bPreserveRemovedTokenBarrier ? strInput : strCleaned;
}

CString CDownloadValidator::BuildFuzzyBoundaryName(const CString& filename, bool bPreserveTags, bool bPreserveMojibakeBarriers) const
{
	CString strSource(filename);
	if (thePrefs.GetDownloadValidatorIgnoreExtension())
		strSource = RemoveFileExtension(strSource);
	if (thePrefs.GetDownloadValidatorIgnoreTags() && !bPreserveTags)
		strSource = RemoveTags(strSource, thePrefs.GetDownloadValidatorDontIgnoreNumericTags());
	if (thePrefs.GetDownloadValidatorCleanMojibake() || bPreserveMojibakeBarriers)
		strSource = RemoveMojibakeGarbage(strSource, bPreserveMojibakeBarriers);
	if (thePrefs.GetDownloadValidatorCaseInsensitive())
		strSource.MakeLower();

	CString strBoundaryName;
	bool bPendingSeparator = false;
	for (int i = 0; i < strSource.GetLength(); ++i) {
		const TCHAR ch = strSource.GetAt(i);
		if (_istalnum(ch)) {
			if (bPendingSeparator && !strBoundaryName.IsEmpty())
				strBoundaryName.AppendChar(_T(' '));
			strBoundaryName.AppendChar(ch);
			bPendingSeparator = false;
		} else if (!strBoundaryName.IsEmpty()) {
			bPendingSeparator = true;
		}
	}
	return strBoundaryName;
}

CString CDownloadValidator::BuildFuzzyNormalizedName(const CString& filename) const
{
	const CString strBoundaryName(BuildFuzzyBoundaryName(filename));
	return thePrefs.GetDownloadValidatorIgnoreNonAlphaNumeric() ? RemoveNonAlphaNumeric(strBoundaryName) : strBoundaryName;
}

void CDownloadValidator::BuildFuzzyGrams(const CString& strNormalizedName, std::vector<FuzzyGramType>& grams) const
{
	grams.clear();
	if (strNormalizedName.GetLength() < 3)
		return;

	grams.reserve(static_cast<size_t>(strNormalizedName.GetLength() - 2));
	for (int i = 0; i <= strNormalizedName.GetLength() - 3; ++i) {
		const uint64 uFirst = static_cast<uint16>(strNormalizedName.GetAt(i));
		const uint64 uSecond = static_cast<uint16>(strNormalizedName.GetAt(i + 1));
		const uint64 uThird = static_cast<uint16>(strNormalizedName.GetAt(i + 2));
		grams.push_back(uFirst | (uSecond << 16) | (uThird << 32));
	}
	std::sort(grams.begin(), grams.end());
	grams.erase(std::unique(grams.begin(), grams.end()), grams.end());
}

void CDownloadValidator::BuildFuzzyTokenHashes(const CString& strBoundaryName, std::vector<FuzzyTokenType>& tokens) const
{
	tokens.clear();
	const int iLength = strBoundaryName.GetLength();
	for (int i = 0; i < iLength;) {
		while (i < iLength && strBoundaryName.GetAt(i) == _T(' '))
			++i;
		const int iStart = i;
		bool bNumeric = true;
		uint64 uHash = 1469598103934665603ui64;
		while (i < iLength && strBoundaryName.GetAt(i) != _T(' ')) {
			const TCHAR ch = strBoundaryName.GetAt(i++);
			if (!_istdigit(ch))
				bNumeric = false;
			const uint16 uCharacter = static_cast<uint16>(ch);
			uHash ^= static_cast<uint8>(uCharacter & 0xFF);
			uHash *= 1099511628211ui64;
			uHash ^= static_cast<uint8>(uCharacter >> 8);
			uHash *= 1099511628211ui64;
		}
		if (i - iStart >= 2 && !bNumeric)
			tokens.push_back(uHash);
	}
	std::sort(tokens.begin(), tokens.end());
	tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
}

void CDownloadValidator::BuildFuzzyOrderedTokenHashes(const CString& strBoundaryName, std::vector<FuzzyTokenType>& tokens) const
{
	tokens.clear();
	for (int i = 0; i < strBoundaryName.GetLength();) {
		while (i < strBoundaryName.GetLength() && !_istalnum(strBoundaryName.GetAt(i)))
			++i;
		const int iStart = i;
		bool bNumeric = true;
		uint64 uHash = 1469598103934665603ui64;
		while (i < strBoundaryName.GetLength() && _istalnum(strBoundaryName.GetAt(i))) {
			const TCHAR ch = strBoundaryName.GetAt(i);
			if (!_istdigit(ch))
				bNumeric = false;
			const uint16 uCharacter = static_cast<uint16>(ch);
			uHash ^= static_cast<uint8>(uCharacter & 0xFF);
			uHash *= 1099511628211ui64;
			uHash ^= static_cast<uint8>(uCharacter >> 8);
			uHash *= 1099511628211ui64;
			++i;
		}
		if (bNumeric && i != iStart)
			tokens.push_back(0);
		else if (i - iStart >= 2)
			tokens.push_back(uHash);
	}
}

void CDownloadValidator::RegisterFuzzyKnownTokenFrequencies(SFuzzyRecord& record)
{
	if (record.bKnownTokenFrequencyRegistered || (record.uSourceFlags & FuzzyFileSourceKnown) == 0)
		return;
	record.bKnownTokenFrequencyRegistered = true;
	if (m_uFuzzyKnownTokenDocumentCount != _UI32_MAX)
		++m_uFuzzyKnownTokenDocumentCount;
	for (std::vector<FuzzyTokenType>::const_iterator it = record.tokenHashes.begin(); it != record.tokenHashes.end(); ++it) {
		uint32& uDocumentFrequency = m_fuzzyKnownTokenFrequencies[*it];
		if (uDocumentFrequency != _UI32_MAX)
			++uDocumentFrequency;
	}
}

uint32 CDownloadValidator::CalculateFuzzyTokenRarityScore(FuzzyTokenType token) const
{
	if (m_uFuzzyKnownTokenDocumentCount == 0)
		return DOWNLOAD_VALIDATOR_FUZZY_TOKEN_RARITY_LEVEL_7_SCORE;
	FuzzyTokenFrequencyIndex::const_iterator it = m_fuzzyKnownTokenFrequencies.find(token);
	const uint64 uDocumentFrequency = it != m_fuzzyKnownTokenFrequencies.end() ? static_cast<uint64>(it->second) : 0;
	const uint64 uKnownDocumentCount = m_uFuzzyKnownTokenDocumentCount;
	const uint64 uScaledFrequency = uDocumentFrequency * 100;
	if (uScaledFrequency <= uKnownDocumentCount)
		return DOWNLOAD_VALIDATOR_FUZZY_TOKEN_RARITY_LEVEL_1_SCORE;
	if (uScaledFrequency <= uKnownDocumentCount * 2)
		return DOWNLOAD_VALIDATOR_FUZZY_TOKEN_RARITY_LEVEL_2_SCORE;
	if (uScaledFrequency <= uKnownDocumentCount * 3)
		return DOWNLOAD_VALIDATOR_FUZZY_TOKEN_RARITY_LEVEL_3_SCORE;
	if (uScaledFrequency <= uKnownDocumentCount * 4)
		return DOWNLOAD_VALIDATOR_FUZZY_TOKEN_RARITY_LEVEL_4_SCORE;
	if (uScaledFrequency <= uKnownDocumentCount * 5)
		return DOWNLOAD_VALIDATOR_FUZZY_TOKEN_RARITY_LEVEL_5_SCORE;
	if (uScaledFrequency < uKnownDocumentCount * 10)
		return DOWNLOAD_VALIDATOR_FUZZY_TOKEN_RARITY_LEVEL_6_SCORE;
	return DOWNLOAD_VALIDATOR_FUZZY_TOKEN_RARITY_LEVEL_7_SCORE;
}

uint32 CDownloadValidator::CalculateFuzzyTokenSimilarityScore(const std::vector<FuzzyTokenType>& queryTokens, const std::vector<FuzzyTokenType>& queryOrderedTokens,
	const std::vector<FuzzyTokenType>& candidateTokens, const std::vector<FuzzyTokenType>& candidateOrderedTokens, uint32& uSharedTokenCount, uint32& uTokenCoveragePercent,
	uint32& uSequenceQuality, uint32& uLongestRunTokens, uint32& uLongestRunCoveragePercent, uint32& uTotalRunCoveragePercent) const
{
	uSharedTokenCount = 0;
	uTokenCoveragePercent = 0;
	uSequenceQuality = 0;
	uLongestRunTokens = 0;
	uLongestRunCoveragePercent = 0;
	uTotalRunCoveragePercent = 0;
	if (queryTokens.empty())
		return 0;

	uint64 uRarityScoreTotal = 0;
	size_t uQuery = 0;
	size_t uCandidate = 0;
	while (uQuery < queryTokens.size() && uCandidate < candidateTokens.size()) {
		if (queryTokens[uQuery] == candidateTokens[uCandidate]) {
			++uSharedTokenCount;
			uRarityScoreTotal += CalculateFuzzyTokenRarityScore(queryTokens[uQuery]);
			++uQuery;
			++uCandidate;
		} else if (queryTokens[uQuery] < candidateTokens[uCandidate])
			++uQuery;
		else
			++uCandidate;
	}
	uTokenCoveragePercent = static_cast<uint32>((static_cast<uint64>(uSharedTokenCount) * DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX) / queryTokens.size());
	const uint32 uUnorderedScore = static_cast<uint32>(uRarityScoreTotal / queryTokens.size());

	uint32 uQueryOrderedTokenCount = 0;
	uint32 uCandidateOrderedTokenCount = 0;
	uint64 uQueryOrderedRarityTotal = 0;
	for (std::vector<FuzzyTokenType>::const_iterator it = queryOrderedTokens.begin(); it != queryOrderedTokens.end(); ++it) {
		if (*it != 0) {
			++uQueryOrderedTokenCount;
			uQueryOrderedRarityTotal += CalculateFuzzyTokenRarityScore(*it);
		}
	}
	for (std::vector<FuzzyTokenType>::const_iterator it = candidateOrderedTokens.begin(); it != candidateOrderedTokens.end(); ++it) {
		if (*it != 0)
			++uCandidateOrderedTokenCount;
	}
	if (uQueryOrderedTokenCount == 0)
		return (std::min)(DOWNLOAD_VALIDATOR_FUZZY_DIFFERENT_NAME_SCORE_MAX, uUnorderedScore);

	const SDownloadValidatorTokenSequenceMatch sequenceMatch = CalculateDownloadValidatorTokenSequenceMatch(queryOrderedTokens, candidateOrderedTokens);
	uLongestRunTokens = sequenceMatch.uLongestRunTokens;
	uLongestRunCoveragePercent = static_cast<uint32>((static_cast<uint64>(sequenceMatch.uLongestRunTokens) * DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX)
		/ uQueryOrderedTokenCount);
	uTotalRunCoveragePercent = static_cast<uint32>((static_cast<uint64>(sequenceMatch.uTotalRunTokens) * DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX)
		/ uQueryOrderedTokenCount);
	if (sequenceMatch.uLongestRunTokens < 2)
		return (std::min)(DOWNLOAD_VALIDATOR_FUZZY_DIFFERENT_NAME_SCORE_MAX, uUnorderedScore);

	uint64 uMatchedOrderedRarityTotal = 0;
	for (size_t i = 0; i < queryOrderedTokens.size() && i < sequenceMatch.queryMatched.size(); ++i) {
		if (queryOrderedTokens[i] != 0 && sequenceMatch.queryMatched[i] != 0)
			uMatchedOrderedRarityTotal += CalculateFuzzyTokenRarityScore(queryOrderedTokens[i]);
	}
	const uint32 uRarityCoveragePercent = uQueryOrderedRarityTotal != 0
		? static_cast<uint32>((uMatchedOrderedRarityTotal * DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX) / uQueryOrderedRarityTotal) : 0;
	const uint32 uRelevantCandidateTokenCount = uSharedTokenCount;
	const uint32 uIrrelevantCandidateTokenCount = uCandidateOrderedTokenCount > uRelevantCandidateTokenCount
		? uCandidateOrderedTokenCount - uRelevantCandidateTokenCount : 0;
	const uint32 uPenalizedIrrelevantTokenCount = uIrrelevantCandidateTokenCount > DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_CLEANLINESS_FREE_UNMATCHED_TOKENS
		? uIrrelevantCandidateTokenCount - DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_CLEANLINESS_FREE_UNMATCHED_TOKENS : 0;
	const uint32 uAdjustedCandidateTokenCount = uRelevantCandidateTokenCount + uPenalizedIrrelevantTokenCount;
	const uint32 uCandidateCleanlinessPercent = uAdjustedCandidateTokenCount != 0
		? (std::min)(DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX, static_cast<uint32>((static_cast<uint64>(uRelevantCandidateTokenCount) * DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX)
			/ uAdjustedCandidateTokenCount)) : 0;
	const uint32 uAdditionalGroupCount = sequenceMatch.uRunCount > 1
		? (std::min)(sequenceMatch.uRunCount - 1, DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_MAXIMUM_GROUPS - 1) : 0;
	const uint32 uGroupSupportPercent = DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_MAXIMUM_GROUPS > 1
		? static_cast<uint32>((static_cast<uint64>(uAdditionalGroupCount) * DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX)
			/ (DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_MAXIMUM_GROUPS - 1)) : 0;
	uSequenceQuality = static_cast<uint32>((static_cast<uint64>(uTotalRunCoveragePercent) * DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_TOTAL_COVERAGE_WEIGHT
		+ static_cast<uint64>(uRarityCoveragePercent) * DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_RARITY_COVERAGE_WEIGHT
		+ static_cast<uint64>(uCandidateCleanlinessPercent) * DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_CANDIDATE_CLEANLINESS_WEIGHT
		+ static_cast<uint64>(uGroupSupportPercent) * DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_GROUP_SUPPORT_WEIGHT
		+ DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX / 2) / DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX);

	uint32 uBandMinimum = DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_SIX_TOKEN_SCORE_MIN;
	uint32 uBandMaximum = DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_SIX_TOKEN_SCORE_MAX;
	if (sequenceMatch.uLongestRunTokens == 2) {
		uBandMinimum = DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_TWO_TOKEN_SCORE_MIN;
		uBandMaximum = DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_TWO_TOKEN_SCORE_MAX;
	} else if (sequenceMatch.uLongestRunTokens == 3) {
		uBandMinimum = DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_THREE_TOKEN_SCORE_MIN;
		uBandMaximum = DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_THREE_TOKEN_SCORE_MAX;
	} else if (sequenceMatch.uLongestRunTokens == 4) {
		uBandMinimum = DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_FOUR_TOKEN_SCORE_MIN;
		uBandMaximum = DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_FOUR_TOKEN_SCORE_MAX;
	} else if (sequenceMatch.uLongestRunTokens == 5) {
		uBandMinimum = DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_FIVE_TOKEN_SCORE_MIN;
		uBandMaximum = DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_FIVE_TOKEN_SCORE_MAX;
		if (uLongestRunCoveragePercent >= DOWNLOAD_VALIDATOR_FUZZY_SEQUENCE_DOMINANT_FIVE_TOKEN_COVERAGE_PERCENT)
			uBandMinimum = uBandMaximum;
	}
	const uint32 uBandRange = uBandMaximum - uBandMinimum;
	const uint32 uBandOffset = (std::min)(uBandRange, static_cast<uint32>((static_cast<uint64>(uSequenceQuality) * uBandRange
		+ DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX - 1) / DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX));
	return uBandMinimum + uBandOffset;
}

void CDownloadValidator::BuildFuzzyStructuralIdentity(const CString& filename, SDownloadValidatorFuzzyStructuralIdentity& identity) const
{
	identity.Clear();
	CString strSource(filename);
	if (thePrefs.GetDownloadValidatorIgnoreExtension())
		strSource = RemoveFileExtension(strSource);
	if (thePrefs.GetDownloadValidatorIgnoreTags())
		strSource = RemoveDownloadValidatorStructuralTags(strSource, thePrefs.GetDownloadValidatorDontIgnoreNumericTags());
	if (thePrefs.GetDownloadValidatorCleanMojibake())
		strSource = RemoveMojibakeGarbage(strSource);
	if (thePrefs.GetDownloadValidatorCaseInsensitive())
		strSource.MakeLower();

	SDownloadValidatorStructuralTokenList tokens;
	for (int i = 0; i < strSource.GetLength();) {
		const bool bNumeric = IsValidatorDigit(strSource.GetAt(i));
		const bool bAlpha = _istalpha(strSource.GetAt(i)) != 0;
		if (!bNumeric && !bAlpha) {
			++i;
			continue;
		}
		const int iStart = i++;
		while (i < strSource.GetLength() && (bNumeric ? IsValidatorDigit(strSource.GetAt(i)) : (_istalpha(strSource.GetAt(i)) != 0)))
			++i;
		SDownloadValidatorStructuralToken token;
		token.iStart = iStart;
		token.iLength = i - iStart;
		token.bNumeric = bNumeric;
		if (!tokens.Add(token))
			break;
	}
	if (tokens.empty())
		return;

	const uint32 uMinimumGroupLetters = thePrefs.GetDownloadValidatorFuzzyStructuralMinimumGroupLetters();
	const uint32 uMinimumIDDigits = thePrefs.GetDownloadValidatorFuzzyStructuralMinimumIDDigits();
	SDownloadValidatorDateTimeOptions dateOptions;
	GetDownloadValidatorDateTimeOptions(dateOptions);
	const auto IsYearToken = [&strSource, &dateOptions, &tokens](size_t uTokenIndex) {
		if (uTokenIndex >= tokens.size() || !tokens[uTokenIndex].bNumeric || tokens[uTokenIndex].iLength != 4)
			return false;
		const int iValue = ParseValidatorDigits(strSource, tokens[uTokenIndex].iStart, tokens[uTokenIndex].iLength);
		return IsDownloadValidatorYearAllowed(dateOptions, iValue);
	};
	const auto IsDateComponentToken = [&strSource, &dateOptions, &tokens, &IsYearToken](size_t uTokenIndex) {
		if (uTokenIndex >= tokens.size() || !tokens[uTokenIndex].bNumeric)
			return false;
		const SDownloadValidatorStructuralToken& token = tokens[uTokenIndex];
		if ((token.iLength == 6 || token.iLength == 8) && TryResolveDownloadValidatorCompactDate(dateOptions, strSource, token.iStart, token.iLength))
			return true;
		if (tokens.size() < 3)
			return false;
		const size_t uFirstStart = uTokenIndex > 2 ? uTokenIndex - 2 : 0;
		const size_t uLastStart = (std::min)(uTokenIndex, tokens.size() - 3);
		for (size_t uStart = uFirstStart; uStart <= uLastStart; ++uStart) {
			if (!tokens[uStart].bNumeric || !tokens[uStart + 1].bNumeric || !tokens[uStart + 2].bNumeric)
				continue;
			if (!IsYearToken(uStart) && !IsYearToken(uStart + 1) && !IsYearToken(uStart + 2))
				continue;
			bool bValidSeparators = true;
			for (size_t i = uStart; i < uStart + 2 && bValidSeparators; ++i) {
				const int iSeparatorStart = tokens[i].iStart + tokens[i].iLength;
				if (iSeparatorStart >= tokens[i + 1].iStart) {
					bValidSeparators = false;
					break;
				}
				for (int iSeparator = iSeparatorStart; iSeparator < tokens[i + 1].iStart; ++iSeparator) {
					if (!IsValidatorDateSeparator(strSource.GetAt(iSeparator))) {
						bValidSeparators = false;
						break;
					}
				}
			}
			if (!bValidSeparators)
				continue;
			SDownloadValidatorNumberPart parts[3];
			for (size_t i = 0; i < 3; ++i) {
				parts[i].iLength = tokens[uStart + i].iLength;
				parts[i].iValue = ParseValidatorDigits(strSource, tokens[uStart + i].iStart, tokens[uStart + i].iLength);
			}
			if (TryResolveDownloadValidatorDateParts(dateOptions, parts[0], parts[1], parts[2]))
				return true;
		}
		return false;
	};
	const auto IsFileIDLetterSeparator = [](TCHAR ch) {
		return ch == _T('-') || ch == _T('.') || ch == _T('_');
	};
	const auto HasFileIDNearbyLetter = [&strSource, &IsFileIDLetterSeparator](const SDownloadValidatorStructuralToken& token, bool bBefore) {
		const int iAdjacent = bBefore ? token.iStart - 1 : token.iStart + token.iLength;
		if (iAdjacent < 0 || iAdjacent >= strSource.GetLength())
			return false;
		if (_istalpha(strSource.GetAt(iAdjacent)))
			return true;
		if (!IsFileIDLetterSeparator(strSource.GetAt(iAdjacent)))
			return false;
		const int iBeyondSeparator = bBefore ? iAdjacent - 1 : iAdjacent + 1;
		return iBeyondSeparator >= 0 && iBeyondSeparator < strSource.GetLength() && _istalpha(strSource.GetAt(iBeyondSeparator));
	};
	const auto IsMultipartFileIDToken = [&strSource, &tokens, &HasFileIDNearbyLetter](size_t uTokenIndex) {
		if (uTokenIndex >= tokens.size() || !tokens[uTokenIndex].bNumeric)
			return false;
		const SDownloadValidatorStructuralToken& token = tokens[uTokenIndex];
		const int iAfter = token.iStart + token.iLength;
		if (token.iLength <= 2 && iAfter < strSource.GetLength() && _istalpha(strSource.GetAt(iAfter)))
			return false;
		return !HasFileIDNearbyLetter(token, true) || !HasFileIDNearbyLetter(token, false);
	};
	const auto HasMultipartFileIDNeighbor = [&strSource, &tokens](size_t uTokenIndex) {
		if (uTokenIndex != 0 && tokens[uTokenIndex - 1].bNumeric
			&& HasDownloadValidatorHyphenSeparator(strSource, tokens[uTokenIndex - 1].iStart + tokens[uTokenIndex - 1].iLength, tokens[uTokenIndex].iStart))
			return true;
		return uTokenIndex + 1 < tokens.size() && tokens[uTokenIndex + 1].bNumeric
			&& HasDownloadValidatorHyphenSeparator(strSource, tokens[uTokenIndex].iStart + tokens[uTokenIndex].iLength, tokens[uTokenIndex + 1].iStart);
	};
	const auto HasPreviousMultipartFileIDPart = [&strSource, &tokens, &IsYearToken, &IsDateComponentToken, &IsMultipartFileIDToken](size_t uTokenIndex) {
		if (uTokenIndex == 0 || !tokens[uTokenIndex - 1].bNumeric || IsYearToken(uTokenIndex - 1) || IsDateComponentToken(uTokenIndex - 1)
			|| !IsMultipartFileIDToken(uTokenIndex - 1))
			return false;
		return HasDownloadValidatorHyphenSeparator(strSource, tokens[uTokenIndex - 1].iStart + tokens[uTokenIndex - 1].iLength, tokens[uTokenIndex].iStart);
	};
	const auto IsStructuralTokenBracketed = [&strSource, &tokens](size_t uTokenIndex) {
		if (uTokenIndex >= tokens.size())
			return false;
		int iBefore = tokens[uTokenIndex].iStart - 1;
		while (iBefore >= 0 && _istspace(strSource.GetAt(iBefore)))
			--iBefore;
		int iAfter = tokens[uTokenIndex].iStart + tokens[uTokenIndex].iLength;
		while (iAfter < strSource.GetLength() && _istspace(strSource.GetAt(iAfter)))
			++iAfter;
		return (iBefore >= 0 && IsDownloadValidatorOpeningBracket(strSource.GetAt(iBefore)))
			|| (iAfter < strSource.GetLength() && IsDownloadValidatorClosingBracket(strSource.GetAt(iAfter)));
	};
	const auto IsIDToken = [&strSource, &tokens, &IsYearToken, &IsDateComponentToken, &IsMultipartFileIDToken, &HasMultipartFileIDNeighbor, &HasPreviousMultipartFileIDPart, uMinimumIDDigits](size_t uTokenIndex, bool bAllowYearValue) {
		if (uTokenIndex >= tokens.size() || !tokens[uTokenIndex].bNumeric
			|| static_cast<uint32>(tokens[uTokenIndex].iLength) < uMinimumIDDigits || IsDateComponentToken(uTokenIndex)
			|| (!bAllowYearValue && IsYearToken(uTokenIndex)))
			return false;
		if (HasMultipartFileIDNeighbor(uTokenIndex) && !IsMultipartFileIDToken(uTokenIndex))
			return false;
		if (HasPreviousMultipartFileIDPart(uTokenIndex))
			return false;
		if (uTokenIndex != 0) {
			const SDownloadValidatorStructuralToken& previous = tokens[uTokenIndex - 1];
			if (!previous.bNumeric && previous.iLength == 1 && previous.iStart + previous.iLength == tokens[uTokenIndex].iStart)
				return false;
		}
		if (uTokenIndex + 1 < tokens.size()) {
			const SDownloadValidatorStructuralToken& next = tokens[uTokenIndex + 1];
			if (!next.bNumeric && next.iLength == 1 && tokens[uTokenIndex].iStart + tokens[uTokenIndex].iLength == next.iStart)
				return false;
		}
		return true;
	};

	size_t uYearToken = tokens.size();
	size_t uIDToken = tokens.size();
	int iBestYearIDScore = -1;
	for (size_t i = 0; i < tokens.size(); ++i) {
		if (!IsYearToken(i) || IsDateComponentToken(i))
			continue;
		for (size_t j = i + 1; j < tokens.size() && j <= i + 3; ++j) {
			const bool bBracketedID = IsStructuralTokenBracketed(j);
			if (!IsIDToken(j, bBracketedID || j == i + 1))
				continue;
			const int iScore = 100 + (bBracketedID ? 40 : 0) + (j == i + 1 ? 20 : 0) - static_cast<int>(j - i);
			if (iScore > iBestYearIDScore || (iScore == iBestYearIDScore && j > uIDToken)) {
				iBestYearIDScore = iScore;
				uYearToken = i;
				uIDToken = j;
			}
		}
		size_t j = i;
		while (j != 0 && i - (j - 1) <= 2) {
			--j;
			const bool bBracketedID = IsStructuralTokenBracketed(j);
			if (!IsIDToken(j, bBracketedID || j + 1 == i))
				continue;
			const int iScore = 60 + (bBracketedID ? 40 : 0) + (j + 1 == i ? 20 : 0) - static_cast<int>(i - j);
			if (iScore > iBestYearIDScore || (iScore == iBestYearIDScore && j > uIDToken)) {
				iBestYearIDScore = iScore;
				uYearToken = i;
				uIDToken = j;
			}
		}
	}
	if (uYearToken != tokens.size()) {
		identity.bHasYear = true;
		identity.uYear = static_cast<uint32>(ParseValidatorDigits(strSource, tokens[uYearToken].iStart, tokens[uYearToken].iLength));
	} else {
		int iBestIDScore = -1;
		for (size_t i = 0; i < tokens.size(); ++i) {
			const bool bHasAlphaBefore = i != 0 && !tokens[i - 1].bNumeric;
			const bool bHasAlphaAfter = i + 1 < tokens.size() && !tokens[i + 1].bNumeric;
			const bool bBracketedID = IsStructuralTokenBracketed(i);
			if (!bBracketedID && !bHasAlphaBefore && !bHasAlphaAfter)
				continue;
			const bool bAlphaBeforeContiguous = bHasAlphaBefore
				&& tokens[i - 1].iStart + tokens[i - 1].iLength == tokens[i].iStart;
			const bool bAlphaAfterContiguous = bHasAlphaAfter
				&& tokens[i].iStart + tokens[i].iLength == tokens[i + 1].iStart;
			if (!IsIDToken(i, bBracketedID || bAlphaBeforeContiguous || bAlphaAfterContiguous))
				continue;
			const uint32 uAdjacentLetters = (std::max)(bHasAlphaBefore ? static_cast<uint32>(tokens[i - 1].iLength) : 0u,
				bHasAlphaAfter ? static_cast<uint32>(tokens[i + 1].iLength) : 0u);
			const int iScore = (bBracketedID ? 40 : 0) + ((bAlphaBeforeContiguous || bAlphaAfterContiguous) ? 20 : 0)
				+ static_cast<int>((std::min)(uAdjacentLetters, 32u));
			if (iScore > iBestIDScore || (iScore == iBestIDScore && i > uIDToken)) {
				iBestIDScore = iScore;
				uIDToken = i;
			}
		}
	}
	if (uIDToken == tokens.size())
		return;
	if (uYearToken == tokens.size() && IsStructuralTokenBracketed(uIDToken)) {
		for (size_t i = uIDToken; i != 0;) {
			--i;
			if (!IsYearToken(i))
				continue;
			uYearToken = i;
			identity.bHasYear = true;
			identity.uYear = static_cast<uint32>(ParseValidatorDigits(strSource, tokens[i].iStart, tokens[i].iLength));
			break;
		}
	}

	size_t uLastIDToken = uIDToken;
	identity.uIDPartCount = 1;
	while (uLastIDToken + 1 < tokens.size() && identity.uIDPartCount < DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_MAXIMUM_ID_PARTS) {
		const SDownloadValidatorStructuralToken& current = tokens[uLastIDToken];
		const SDownloadValidatorStructuralToken& next = tokens[uLastIDToken + 1];
		if (!next.bNumeric || IsDateComponentToken(uLastIDToken + 1) || !IsMultipartFileIDToken(uLastIDToken + 1)
			|| !HasDownloadValidatorHyphenSeparator(strSource, current.iStart + current.iLength, next.iStart))
			break;
		++uLastIDToken;
		++identity.uIDPartCount;
	}

	uint64 uIDHash = 1469598103934665603ui64;
	for (size_t i = uIDToken; i <= uLastIDToken; ++i) {
		int iFirstDigit = tokens[i].iStart;
		const int iEndDigit = tokens[i].iStart + tokens[i].iLength;
		while (iFirstDigit < iEndDigit - 1 && strSource.GetAt(iFirstDigit) == _T('0'))
			++iFirstDigit;
		for (int iDigit = iFirstDigit; iDigit < iEndDigit; ++iDigit) {
			uIDHash ^= static_cast<uint8>(strSource.GetAt(iDigit));
			uIDHash *= 1099511628211ui64;
		}
		uIDHash ^= 0xFF;
		uIDHash *= 1099511628211ui64;
	}
	identity.uIDHash = uIDHash;
	identity.bHasID = true;

	int iBeforeID = tokens[uIDToken].iStart - 1;
	while (iBeforeID >= 0 && _istspace(strSource.GetAt(iBeforeID)))
		--iBeforeID;
	int iAfterFirstID = tokens[uIDToken].iStart + tokens[uIDToken].iLength;
	while (iAfterFirstID < strSource.GetLength() && _istspace(strSource.GetAt(iAfterFirstID)))
		++iAfterFirstID;
	identity.bIDBracketed = (iBeforeID >= 0 && IsDownloadValidatorOpeningBracket(strSource.GetAt(iBeforeID)))
		|| (iAfterFirstID < strSource.GetLength() && IsDownloadValidatorClosingBracket(strSource.GetAt(iAfterFirstID)));

	const auto HashGroupTokenRange = [&strSource, &tokens](size_t uFirstToken, size_t uLastToken, uint32& uLetterCount) {
		uint64 uHash = 1469598103934665603ui64;
		uLetterCount = 0;
		for (size_t i = uFirstToken; i <= uLastToken; ++i) {
			for (int j = 0; j < tokens[i].iLength; ++j) {
				const TCHAR ch = strSource.GetAt(tokens[i].iStart + j);
				if (_istalpha(ch))
					++uLetterCount;
				const uint16 uCharacter = static_cast<uint16>(ch);
				uHash ^= static_cast<uint8>(uCharacter & 0xFF);
				uHash *= 1099511628211ui64;
				uHash ^= static_cast<uint8>(uCharacter >> 8);
				uHash *= 1099511628211ui64;
			}
		}
		return uHash;
	};
	const auto AddGroupBefore = [&identity, &tokens, &HashGroupTokenRange, uMinimumIDDigits](size_t uAnchorToken) {
		uint64 auReverseHashes[SDownloadValidatorFuzzyStructuralIdentity::MaximumGroupTokens] = { 0 };
		uint32 auReverseLetters[SDownloadValidatorFuzzyStructuralIdentity::MaximumGroupTokens] = { 0 };
		uint8 uCount = 0;
		size_t uCursor = uAnchorToken;
		while (uCursor != 0 && uCount < SDownloadValidatorFuzzyStructuralIdentity::MaximumGroupTokens) {
			size_t uFirstToken = uCursor - 1;
			size_t uLastToken = uFirstToken;
			if (tokens[uFirstToken].bNumeric) {
				if (static_cast<uint32>(tokens[uFirstToken].iLength) >= uMinimumIDDigits || uFirstToken == 0 || tokens[uFirstToken - 1].bNumeric)
					break;
				--uFirstToken;
			} else if (uLastToken + 1 < uAnchorToken && tokens[uLastToken + 1].bNumeric
				&& static_cast<uint32>(tokens[uLastToken + 1].iLength) < uMinimumIDDigits) {
				++uLastToken;
			}
			auReverseHashes[uCount] = HashGroupTokenRange(uFirstToken, uLastToken, auReverseLetters[uCount]);
			++uCount;
			uCursor = uFirstToken;
		}
		for (uint8 i = 0; i < uCount; ++i) {
			const uint8 uSource = static_cast<uint8>(uCount - i - 1);
			identity.auGroupTokenHashes[i] = auReverseHashes[uSource];
			identity.uGroupLetterCount += auReverseLetters[uSource];
		}
		identity.uGroupTokenCount = uCount;
	};
	const auto AddGroupAfter = [&identity, &tokens, &HashGroupTokenRange, uMinimumIDDigits](size_t uAnchorToken) {
		size_t uCursor = uAnchorToken;
		while (uCursor < tokens.size() && identity.uGroupTokenCount < SDownloadValidatorFuzzyStructuralIdentity::MaximumGroupTokens) {
			size_t uFirstToken = uCursor;
			size_t uLastToken = uFirstToken;
			if (tokens[uFirstToken].bNumeric)
				break;
			if (uLastToken + 1 < tokens.size() && tokens[uLastToken + 1].bNumeric
				&& static_cast<uint32>(tokens[uLastToken + 1].iLength) < uMinimumIDDigits)
				++uLastToken;
			uint32 uLetterCount = 0;
			identity.auGroupTokenHashes[identity.uGroupTokenCount++] = HashGroupTokenRange(uFirstToken, uLastToken, uLetterCount);
			identity.uGroupLetterCount += uLetterCount;
			uCursor = uLastToken + 1;
		}
	};

	AddGroupBefore(uYearToken != tokens.size() ? uYearToken : uIDToken);
	if (identity.uGroupTokenCount == 0 && uYearToken != tokens.size() && uYearToken < uIDToken)
		AddGroupAfter(uYearToken + 1);
	if (identity.uGroupTokenCount == 0)
		AddGroupAfter(uLastIDToken + 1);
	if (identity.uGroupLetterCount < uMinimumGroupLetters) {
		identity.uGroupTokenCount = 0;
		identity.uGroupLetterCount = 0;
		for (int i = 0; i < SDownloadValidatorFuzzyStructuralIdentity::MaximumGroupTokens; ++i)
			identity.auGroupTokenHashes[i] = 0;
		if (!identity.bHasYear)
			identity.Clear();
		return;
	}

	uint64 uGroupSignatureHash = 1469598103934665603ui64;
	for (uint8 i = 0; i < identity.uGroupTokenCount; ++i) {
		const uint64 uTokenHash = identity.auGroupTokenHashes[i];
		for (size_t j = 0; j < sizeof(uTokenHash); ++j) {
			uGroupSignatureHash ^= static_cast<uint8>(uTokenHash >> (j * 8));
			uGroupSignatureHash *= 1099511628211ui64;
		}
	}
	identity.uGroupSignatureHash = uGroupSignatureHash;
}

uint64 CDownloadValidator::BuildFuzzyStructuralIdentityKey(const SDownloadValidatorFuzzyStructuralIdentity& identity) const
{
	if (!identity.bHasID)
		return 0;
	uint64 uHash = 1469598103934665603ui64;
	const auto AddValue = [&uHash](uint64 uValue) {
		for (size_t i = 0; i < sizeof(uValue); ++i) {
			uHash ^= static_cast<uint8>(uValue >> (i * 8));
			uHash *= 1099511628211ui64;
		}
	};
	AddValue(identity.uIDHash);
	AddValue(identity.bHasYear ? identity.uYear : 0);
	AddValue(identity.uGroupSignatureHash);
	return uHash;
}

uint64 CDownloadValidator::BuildFuzzyStructuralLookupKey(const SDownloadValidatorFuzzyStructuralIdentity& identity, bool bIncludeYear) const
{
	if (!identity.bHasID || (bIncludeYear && !identity.bHasYear))
		return 0;
	uint64 uHash = bIncludeYear ? 1099511628211ui64 : 1469598103934665603ui64;
	const auto AddValue = [&uHash](uint64 uValue) {
		for (size_t i = 0; i < sizeof(uValue); ++i) {
			uHash ^= static_cast<uint8>(uValue >> (i * 8));
			uHash *= 1099511628211ui64;
		}
	};
	AddValue(identity.uIDHash);
	if (bIncludeYear)
		AddValue(identity.uYear);
	return uHash;
}

void CDownloadValidator::RegisterFuzzyStructuralIdentity(const SDownloadValidatorFuzzyStructuralIdentity& identity)
{
	if (!identity.bHasID || identity.uGroupSignatureHash == 0)
		return;
	const auto RegisterGroupKey = [this, &identity](FuzzyTokenType uGroupKey) {
		if (uGroupKey == 0)
			return;
		SFuzzyStructuralGroupStats& stats = m_fuzzyStructuralGroupStats[uGroupKey];
		if (stats.uFileCount != _UI32_MAX)
			++stats.uFileCount;
		stats.uIdentitySketch |= 1ui64 << static_cast<uint32>(identity.uIDHash & 63ui64);
	};
	RegisterGroupKey(identity.uGroupSignatureHash);
	for (uint8 i = 0; i < identity.uGroupTokenCount; ++i)
		RegisterGroupKey(identity.auGroupTokenHashes[i]);
}

void CDownloadValidator::RegisterFuzzyStructuralRecord(const SDownloadValidatorFuzzyStructuralIdentity& identity, uint32 uRecordID)
{
	if (!identity.bHasID)
		return;
	const uint64 uIDKey = BuildFuzzyStructuralLookupKey(identity, false);
	if (uIDKey != 0)
		m_fuzzyStructuralIDIndex.emplace(uIDKey, uRecordID);
	const uint64 uYearIDKey = BuildFuzzyStructuralLookupKey(identity, true);
	if (uYearIDKey != 0)
		m_fuzzyStructuralYearIDIndex.emplace(uYearIDKey, uRecordID);
}

void CDownloadValidator::AddFuzzyStructuralCandidates(const SDownloadValidatorFuzzyStructuralIdentity& identity, std::vector<uint32>& recordIDs) const
{
	recordIDs.clear();
	if (!identity.bHasID)
		return;
	const auto AppendRange = [&recordIDs](const FuzzyStructuralIndex& index, uint64 uKey) {
		if (uKey == 0)
			return;
		const std::pair<FuzzyStructuralIndex::const_iterator, FuzzyStructuralIndex::const_iterator> range = index.equal_range(uKey);
		for (FuzzyStructuralIndex::const_iterator it = range.first; it != range.second; ++it)
			recordIDs.push_back(it->second);
	};
	AppendRange(m_fuzzyStructuralYearIDIndex, BuildFuzzyStructuralLookupKey(identity, true));
	AppendRange(m_fuzzyStructuralIDIndex, BuildFuzzyStructuralLookupKey(identity, false));
	std::sort(recordIDs.begin(), recordIDs.end());
	recordIDs.erase(std::unique(recordIDs.begin(), recordIDs.end()), recordIDs.end());
}

bool CDownloadValidator::IsFuzzyStructuralGroupTokenStrong(FuzzyTokenType token) const
{
	FuzzyStructuralGroupStats::const_iterator it = m_fuzzyStructuralGroupStats.find(token);
	return it != m_fuzzyStructuralGroupStats.end() && it->second.uFileCount >= DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_MINIMUM_GROUP_FILE_COUNT
		&& CountDownloadValidatorSetBits(it->second.uIdentitySketch) >= DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_MINIMUM_DISTINCT_IDS;
}

CDownloadValidator::SFuzzyStructuralMatch CDownloadValidator::EvaluateFuzzyStructuralIdentity(const SDownloadValidatorFuzzyStructuralIdentity& queryIdentity,
	const SDownloadValidatorFuzzyStructuralIdentity& candidateIdentity) const
{
	SFuzzyStructuralMatch result;
	if (!queryIdentity.bHasID || !candidateIdentity.bHasID)
		return result;

	const bool bSameGroupSignature = queryIdentity.uGroupSignatureHash != 0
		&& queryIdentity.uGroupSignatureHash == candidateIdentity.uGroupSignatureHash;
	bool bSameLearnedGroupSuffix = false;
	FuzzyTokenType uGroupEvidenceKey = bSameGroupSignature ? queryIdentity.uGroupSignatureHash : 0;
	if (!bSameGroupSignature && queryIdentity.uGroupTokenCount != 0 && candidateIdentity.uGroupTokenCount != 0
		&& queryIdentity.uGroupTokenCount != candidateIdentity.uGroupTokenCount) {
		const FuzzyTokenType uQuerySuffix = queryIdentity.auGroupTokenHashes[queryIdentity.uGroupTokenCount - 1];
		const FuzzyTokenType uCandidateSuffix = candidateIdentity.auGroupTokenHashes[candidateIdentity.uGroupTokenCount - 1];
		if (uQuerySuffix == uCandidateSuffix && IsFuzzyStructuralGroupTokenStrong(uQuerySuffix)) {
			bSameLearnedGroupSuffix = true;
			uGroupEvidenceKey = uQuerySuffix;
		}
	}
	const bool bCompatibleGroup = bSameGroupSignature || bSameLearnedGroupSuffix;
	const bool bConfidentGroup = bCompatibleGroup && IsFuzzyStructuralGroupTokenStrong(uGroupEvidenceKey);
	const bool bSameID = queryIdentity.uIDHash == candidateIdentity.uIDHash;
	const bool bBothHaveYear = queryIdentity.bHasYear && candidateIdentity.bHasYear;
	const bool bSameYear = bBothHaveYear && queryIdentity.uYear == candidateIdentity.uYear;
	if (bConfidentGroup && (!bSameID || (bBothHaveYear && !bSameYear))) {
		result.bConflict = true;
		return result;
	}
	if (!bSameID)
		return result;

	if (bCompatibleGroup) {
		if (bSameYear)
			result.uScoreFloor = queryIdentity.bIDBracketed && candidateIdentity.bIDBracketed
				? DOWNLOAD_VALIDATOR_FUZZY_DIFFERENT_NAME_SCORE_MAX : DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_FULL_MATCH_SCORE;
		else if (!queryIdentity.bHasYear && !candidateIdentity.bHasYear) {
			result.uScoreFloor = DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_WEAK_MATCH_SCORE;
			result.bWeakMatch = true;
		}
		else if (queryIdentity.bHasYear != candidateIdentity.bHasYear)
			result.uScoreFloor = DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_PARTIAL_YEAR_SCORE;
		else
			return result;
	} else if (bSameYear) {
		if (queryIdentity.uGroupSignatureHash == 0 || candidateIdentity.uGroupSignatureHash == 0)
			result.uScoreFloor = (queryIdentity.bIDBracketed || candidateIdentity.bIDBracketed)
				? DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_PARTIAL_GROUP_SCORE : DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_WEAK_MATCH_SCORE;
	}
	if (result.uScoreFloor == 0)
		return result;
	result.bMatch = true;
	result.uIdentityKey = BuildFuzzyStructuralIdentityKey(candidateIdentity);
	return result;
}

uint32 CDownloadValidator::GetFuzzyNormalizationFingerprint() const
{
	uint32 uFingerprint = 2166136261u;
	const auto AddValue = [&uFingerprint](uint32 uValue) {
		uFingerprint ^= uValue;
		uFingerprint *= 16777619u;
	};
	AddValue(thePrefs.GetDownloadValidatorIgnoreExtension() ? 1u : 0u);
	AddValue(thePrefs.GetDownloadValidatorIgnoreTags() ? 1u : 0u);
	AddValue(thePrefs.GetDownloadValidatorDontIgnoreNumericTags() ? 1u : 0u);
	AddValue(thePrefs.GetDownloadValidatorIgnoreNonAlphaNumeric() ? 1u : 0u);
	AddValue(thePrefs.GetDownloadValidatorCleanMojibake() ? 1u : 0u);
	AddValue(thePrefs.GetDownloadValidatorCaseInsensitive() ? 1u : 0u);
	AddValue(thePrefs.GetDownloadValidatorDateTimeUseYearRange() ? 1u : 0u);
	AddValue(static_cast<uint32>(thePrefs.GetDownloadValidatorDateTimeYearStart()));
	AddValue(static_cast<uint32>(thePrefs.GetDownloadValidatorDateTimeYearEnd()));
	AddValue(static_cast<uint32>(thePrefs.GetDownloadValidatorMinimumComparisonLength()));
	AddValue(thePrefs.GetDownloadValidatorFuzzyStructuralMinimumGroupLetters());
	AddValue(thePrefs.GetDownloadValidatorFuzzyStructuralMinimumIDDigits());
	return uFingerprint != 0 ? uFingerprint : 1u;
}

uint32 CDownloadValidator::GetFuzzyCandidateFingerprint() const
{
	uint32 uFingerprint = GetFuzzyNormalizationFingerprint();
	const auto AddValue = [&uFingerprint](uint32 uValue) {
		uFingerprint ^= uValue;
		uFingerprint *= 16777619u;
	};
	AddValue(thePrefs.GetDownloadValidatorFuzzyMinimumSharedTokens());
	AddValue(thePrefs.GetDownloadValidatorFuzzyMinimumTokenCoveragePercent());
	AddValue(thePrefs.GetDownloadValidatorFuzzyMinimumLengthSimilarityPercent());
	AddValue(thePrefs.GetDownloadValidatorFuzzyMinimumEditSimilarityPercent());
	return uFingerprint != 0 ? uFingerprint : 1u;
}

bool CDownloadValidator::PrepareFuzzyQueryData(const CString& filename, SDownloadValidatorFuzzyQueryData& queryData) const
{
	const uint32 uNormalizationFingerprint = GetFuzzyNormalizationFingerprint();
	if (queryData.bPrepared && queryData.uNormalizationFingerprint == uNormalizationFingerprint && queryData.strSourceFileName == filename)
		return !queryData.grams.empty() || queryData.structuralIdentity.bHasID;

	queryData.Clear();
	queryData.strSourceFileName = filename;
	queryData.uNormalizationFingerprint = uNormalizationFingerprint;
	queryData.strBoundaryName = BuildFuzzyBoundaryName(filename);
	queryData.strNormalizedName = thePrefs.GetDownloadValidatorIgnoreNonAlphaNumeric() ? RemoveNonAlphaNumeric(queryData.strBoundaryName) : queryData.strBoundaryName;
	BuildFuzzyStructuralIdentity(filename, queryData.structuralIdentity);
	queryData.uStructuralIdentityKey = BuildFuzzyStructuralIdentityKey(queryData.structuralIdentity);
	queryData.uFileType = static_cast<uint8>(GetED2KFileTypeID(filename));
	queryData.bPrepared = true;
	if (queryData.strNormalizedName.GetLength() < max(3, thePrefs.GetDownloadValidatorMinimumComparisonLength()))
		return false;
	BuildFuzzyGrams(queryData.strNormalizedName, queryData.grams);
	const CString strLexicalBoundaryName(BuildFuzzyBoundaryName(filename, true));
	BuildFuzzyTokenHashes(strLexicalBoundaryName, queryData.tokenHashes);
	BuildFuzzyOrderedTokenHashes(BuildFuzzyBoundaryName(filename, true, true), queryData.orderedTokenHashes);
	return !queryData.grams.empty() || queryData.structuralIdentity.bHasID;
}

uint32 CDownloadValidator::CalculateFuzzyGramWeight(uint32 uDocumentFrequency) const
{
	if (uDocumentFrequency == 0 || uDocumentFrequency > m_uFuzzyMaximumPostingFrequency)
		return 0;
	const uint64 uRecordCount = (std::max<uint64>)(1, m_uFuzzyWeightRecordCount != 0
		? static_cast<uint64>(m_uFuzzyWeightRecordCount)
		: static_cast<uint64>(m_fuzzyRecords.size()) - m_uFuzzyInactiveRecordCount);
	uint64 uRatio = (uRecordCount + 1) / (static_cast<uint64>(uDocumentFrequency) + 1);
	uint32 uWeight = 1;
	while (uRatio > 1) {
		++uWeight;
		uRatio >>= 1;
	}
	return uWeight;
}

bool CDownloadValidator::IsFuzzyCandidateCacheKeyMatch(const SFuzzyCandidateCacheEntry& entry, const SDownloadValidatorFuzzyQueryData& queryData, uint32 uRevision, uint32 uCandidateFingerprint) const
{
	return entry.uRevision == uRevision && entry.uCandidateFingerprint == uCandidateFingerprint && entry.uFileType == queryData.uFileType
		&& entry.uStructuralIdentityKey == queryData.uStructuralIdentityKey && entry.strSourceFileName == queryData.strSourceFileName
		&& entry.strNormalizedName == queryData.strNormalizedName && entry.strBoundaryName == queryData.strBoundaryName;
}

std::list<CDownloadValidator::SFuzzyCandidateCacheEntry>::iterator CDownloadValidator::FindFuzzyCandidateCacheEntry(const SDownloadValidatorFuzzyQueryData& queryData, uint32 uRevision, uint32 uCandidateFingerprint)
{
	for (std::list<SFuzzyCandidateCacheEntry>::iterator it = m_fuzzyCandidateCache.begin(); it != m_fuzzyCandidateCache.end(); ++it) {
		if (!IsFuzzyCandidateCacheKeyMatch(*it, queryData, uRevision, uCandidateFingerprint))
			continue;
		if (it != m_fuzzyCandidateCache.begin())
			m_fuzzyCandidateCache.splice(m_fuzzyCandidateCache.begin(), m_fuzzyCandidateCache, it);
		return m_fuzzyCandidateCache.begin();
	}
	return m_fuzzyCandidateCache.end();
}

void CDownloadValidator::StoreFuzzyCandidateCacheEntry(const SFuzzyCandidateCacheEntry& entry)
{
	if (entry.candidates.size() > DOWNLOAD_VALIDATOR_FUZZY_CACHE_MAX_CANDIDATES_PER_ENTRY)
		return;
	for (std::list<SFuzzyCandidateCacheEntry>::iterator it = m_fuzzyCandidateCache.begin(); it != m_fuzzyCandidateCache.end(); ++it) {
		if (it->uRevision != entry.uRevision || it->uCandidateFingerprint != entry.uCandidateFingerprint || it->uFileType != entry.uFileType
			|| it->uStructuralIdentityKey != entry.uStructuralIdentityKey || it->strSourceFileName != entry.strSourceFileName
			|| it->strNormalizedName != entry.strNormalizedName || it->strBoundaryName != entry.strBoundaryName)
			continue;
		m_uFuzzyCandidateCacheCandidateCount -= it->candidates.size();
		m_fuzzyCandidateCache.erase(it);
		break;
	}
	while (!m_fuzzyCandidateCache.empty() && (m_fuzzyCandidateCache.size() >= DOWNLOAD_VALIDATOR_FUZZY_CACHE_MAX_ENTRIES
		|| m_uFuzzyCandidateCacheCandidateCount + entry.candidates.size() > DOWNLOAD_VALIDATOR_FUZZY_CACHE_MAX_CANDIDATES)) {
		m_uFuzzyCandidateCacheCandidateCount -= m_fuzzyCandidateCache.back().candidates.size();
		m_fuzzyCandidateCache.pop_back();
	}
	if (entry.candidates.size() > DOWNLOAD_VALIDATOR_FUZZY_CACHE_MAX_CANDIDATES)
		return;
	m_fuzzyCandidateCache.push_front(entry);
	m_uFuzzyCandidateCacheCandidateCount += entry.candidates.size();
}

void CDownloadValidator::ClearFuzzyCandidateCache()
{
	m_fuzzyCandidateCache.clear();
	m_uFuzzyCandidateCacheCandidateCount = 0;
}

uint32 CDownloadValidator::CalculateFuzzySimilarityScore(const CString& strQueryName, const CString& strQueryBoundaryName, uint64 uQueryGramWeight, uint64 uCandidateGramWeight,
	uint64 uSharedGramWeight, FuzzyCandidateType& candidate) const
{
	if (strQueryName == candidate.strNormalizedName) {
		candidate.uCoverageScore = DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX;
		candidate.uWeightedJaccardScore = DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX;
		candidate.uEditSimilarityScore = DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX;
		candidate.bExactSubstring = true;
		return DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX;
	}

	const uint64 uShorterGramWeight = (std::min)(uQueryGramWeight, uCandidateGramWeight);
	if (uShorterGramWeight != 0)
		candidate.uCoverageScore = static_cast<uint32>((std::min<uint64>)(DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX, (uSharedGramWeight * DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX) / uShorterGramWeight));
	const uint64 uUnionGramWeight = uQueryGramWeight + uCandidateGramWeight - (std::min)(uSharedGramWeight, (std::min)(uQueryGramWeight, uCandidateGramWeight));
	if (uUnionGramWeight != 0)
		candidate.uWeightedJaccardScore = static_cast<uint32>((std::min<uint64>)(DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX, (uSharedGramWeight * DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX) / uUnionGramWeight));

	candidate.bExactSubstring = strQueryName.Find(candidate.strNormalizedName) >= 0 || candidate.strNormalizedName.Find(strQueryName) >= 0
		|| strQueryBoundaryName.Find(candidate.strBoundaryName) >= 0 || candidate.strBoundaryName.Find(strQueryBoundaryName) >= 0;
	if (candidate.uEditSimilarityScore == 0 && candidate.uCoverageScore >= DOWNLOAD_VALIDATOR_FUZZY_MINIMUM_EDIT_COVERAGE_SCORE)
		candidate.uEditSimilarityScore = CalculateDownloadValidatorEditSimilarity(strQueryName, candidate.strNormalizedName);

	uint64 uScore = static_cast<uint64>(candidate.uCoverageScore) * DOWNLOAD_VALIDATOR_FUZZY_COVERAGE_WEIGHT;
	uScore += static_cast<uint64>(candidate.uWeightedJaccardScore) * DOWNLOAD_VALIDATOR_FUZZY_JACCARD_WEIGHT;
	uScore += static_cast<uint64>(candidate.uEditSimilarityScore) * DOWNLOAD_VALIDATOR_FUZZY_EDIT_WEIGHT;
	uScore /= DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX;
	if (candidate.bExactSubstring)
		uScore += DOWNLOAD_VALIDATOR_FUZZY_SUBSTRING_BONUS;
	return static_cast<uint32>((std::min<uint64>)(DOWNLOAD_VALIDATOR_FUZZY_DIFFERENT_NAME_SCORE_MAX, uScore));
}

uint64 CDownloadValidator::BuildFuzzyIdentityKey(const uchar* hash, const CString& filename, EMFileSize filesize) const
{
	uint64 uIdentity = 1469598103934665603ui64;
	for (UINT i = 0; i < MDX_DIGEST_SIZE; ++i) {
		uIdentity ^= hash[i];
		uIdentity *= 1099511628211ui64;
	}
	const uint64 uFileSize = static_cast<uint64>(filesize);
	for (size_t i = 0; i < sizeof(uFileSize); ++i) {
		uIdentity ^= static_cast<uint8>(uFileSize >> (i * 8));
		uIdentity *= 1099511628211ui64;
	}
	for (int i = 0; i < filename.GetLength(); ++i) {
		const uint16 uCharacter = static_cast<uint16>(filename.GetAt(i));
		uIdentity ^= static_cast<uint8>(uCharacter & 0xFF);
		uIdentity *= 1099511628211ui64;
		uIdentity ^= static_cast<uint8>(uCharacter >> 8);
		uIdentity *= 1099511628211ui64;
	}
	return uIdentity;
}

void CDownloadValidator::ResolveFileMetadata(const uchar* hash, EMFileSize filesize, uint8& uSourceFlags, uint32& uMediaLengthSec, EFuzzyMediaLengthSource& eMediaLengthSource) const
{
	uSourceFlags = FuzzyFileSourceUnknown;
	uMediaLengthSec = 0;
	eMediaLengthSource = FuzzyMediaLengthUnknown;

	if (theApp.downloadqueue != NULL) {
		CPartFile* pPartFile = theApp.downloadqueue->GetFileByID(hash);
		if (pPartFile != NULL && pPartFile->GetFileSize() == filesize) {
			uSourceFlags = static_cast<uint8>(uSourceFlags | FuzzyFileSourceDownloading);
			const uint32 uLength = pPartFile->GetIntTagValue(FT_MEDIA_LENGTH);
			if (uLength != 0) {
				uMediaLengthSec = uLength;
				eMediaLengthSource = FuzzyMediaLengthRemoteMetadata;
			}
		}
	}

	if (theApp.knownfiles != NULL) {
		CKnownFile* pKnownFile = theApp.knownfiles->FindKnownFileByID(hash);
		if (pKnownFile != NULL && !pKnownFile->IsPartFile() && pKnownFile->GetFileSize() == filesize) {
			uSourceFlags = static_cast<uint8>(uSourceFlags | FuzzyFileSourceKnown);
			uMediaLengthSec = pKnownFile->GetIntTagValue(FT_MEDIA_LENGTH);
			eMediaLengthSource = FuzzyMediaLengthLocalMediaInfo;
		} else if (theApp.knownfiles->DuplicatesCount(hash) != 0) {
			uSourceFlags = static_cast<uint8>(uSourceFlags | FuzzyFileSourceKnown);
		}
	}
}

void CDownloadValidator::PopulateFuzzyDisplayMetadata(FuzzyCandidateType& candidate) const
{
	ResolveFuzzyDisplayMetadata(candidate);
}

void CDownloadValidator::PopulateFuzzyDisplayMetadata(std::vector<FuzzyCandidateType>& candidates) const
{
	for (std::vector<FuzzyCandidateType>::iterator it = candidates.begin(); it != candidates.end(); ++it)
		PopulateFuzzyDisplayMetadata(*it);
}

void CDownloadValidator::ResolveFuzzyDisplayMetadata(FuzzyCandidateType& candidate) const
{
	candidate.uMediaBitrateKbps = 0;
	candidate.strMediaArtist.Empty();
	candidate.strMediaAlbum.Empty();
	candidate.strMediaTitle.Empty();
	candidate.strMediaCodec.Empty();
	candidate.strFolder.Empty();
	candidate.strAICHHash.Empty();

	const CKnownFile* pFile = NULL;
	if (theApp.knownfiles != NULL) {
		CKnownFile* pKnownFile = theApp.knownfiles->FindKnownFileByID(candidate.ucHash);
		if (pKnownFile != NULL && pKnownFile->GetFileSize() == candidate.uSize) {
			pFile = pKnownFile;
			candidate.uSourceFlags = static_cast<uint8>(candidate.uSourceFlags | FuzzyFileSourceKnown);
		}
	}
	if (pFile == NULL && theApp.downloadqueue != NULL) {
		CPartFile* pPartFile = theApp.downloadqueue->GetFileByID(candidate.ucHash);
		if (pPartFile != NULL && pPartFile->GetFileSize() == candidate.uSize) {
			pFile = pPartFile;
			candidate.uSourceFlags = static_cast<uint8>(candidate.uSourceFlags | FuzzyFileSourceDownloading);
		}
	}
	if (pFile == NULL)
		return;

	const uint32 uMediaLengthSec = pFile->GetIntTagValue(FT_MEDIA_LENGTH);
	if (candidate.uMediaLengthSec == 0 && uMediaLengthSec != 0) {
		candidate.uMediaLengthSec = uMediaLengthSec;
		candidate.eMediaLengthSource = FuzzyMediaLengthLocalMediaInfo;
	}
	candidate.uMediaBitrateKbps = pFile->GetIntTagValue(FT_MEDIA_BITRATE);
	candidate.strMediaArtist = pFile->GetStrTagValue(FT_MEDIA_ARTIST);
	candidate.strMediaAlbum = pFile->GetStrTagValue(FT_MEDIA_ALBUM);
	candidate.strMediaTitle = pFile->GetStrTagValue(FT_MEDIA_TITLE);
	candidate.strMediaCodec = pFile->GetStrTagValue(FT_MEDIA_CODEC);
	candidate.strFolder = pFile->GetPath();
	if (pFile->GetFileIdentifierC().HasAICHHash())
		candidate.strAICHHash = pFile->GetFileIdentifierC().GetAICHHash().GetString();
}

void CDownloadValidator::AddTrustedToMapInternal(const uchar* hash, const CString& filename, EMFileSize filesize, uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource)
{
	if (filename.GetLength() < thePrefs.GetDownloadValidatorMinimumComparisonLength())
		return;

	const CString strProcessedFileName(BuildMapKey(filename));
	AddPreparedToMap(m_DownloadValidatorMap, strProcessedFileName, hash, filename, filesize, uMediaLengthSec, eMediaLengthSource);
	if (thePrefs.GetDownloadValidatorDateTimeMatching())
		AddPreparedToMap(m_DownloadValidatorDateTimeMap, BuildDateTimeMapKey(filename, strProcessedFileName), hash, filename, filesize, uMediaLengthSec, eMediaLengthSource);
	AddRegexMatchesToMap(hash, filename, filesize, uMediaLengthSec, eMediaLengthSource);
}

void CDownloadValidator::AddToMapInternal(const uchar* hash, const CString& filename, EMFileSize filesize, uint8 uSourceFlags, uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource)
{
	if (filename.GetLength() < thePrefs.GetDownloadValidatorMinimumComparisonLength())
		return;

	const CString strProcessedFileName(BuildMapKey(filename));
	AddPreparedToMap(m_DownloadValidatorMap, strProcessedFileName, hash, filename, filesize, uMediaLengthSec, eMediaLengthSource);
	if (thePrefs.GetDownloadValidatorDateTimeMatching())
		AddPreparedToMap(m_DownloadValidatorDateTimeMap, BuildDateTimeMapKey(filename, strProcessedFileName), hash, filename, filesize, uMediaLengthSec, eMediaLengthSource);
	AddRegexMatchesToMap(hash, filename, filesize, uMediaLengthSec, eMediaLengthSource);
	const CString* pFuzzyNormalizedName = thePrefs.GetDownloadValidatorIgnoreNonAlphaNumeric() ? &strProcessedFileName : NULL;
	AddFuzzyRecord(hash, filename, filesize, uSourceFlags, uMediaLengthSec, eMediaLengthSource, pFuzzyNormalizedName);
}

void CDownloadValidator::AddFuzzyRecord(const uchar* hash, const CString& filename, EMFileSize filesize, uint8 uSourceFlags, uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource, const CString* pNormalizedName)
{
	if (!IsFuzzyMatchingEnabled() || !m_bFuzzyIndexAvailable || hash == NULL)
		return;
	if (m_pReloadMapState != NULL && !m_pReloadMapState->bRegexOnly
		&& (m_pReloadMapState->ePhase == SReloadMapState::PhaseFuzzyPrepare)) {
		try {
			for (std::vector<SReloadMapState::SPendingFuzzyRecord>::iterator it = m_pReloadMapState->pendingFuzzyRecords.begin(); it != m_pReloadMapState->pendingFuzzyRecords.end(); ++it) {
				if (!IsDownloadValidatorFileMatch(it->fileInfo, hash, filesize) || it->fileInfo.strName != filename)
					continue;
				it->uSourceFlags = static_cast<uint8>(it->uSourceFlags | uSourceFlags);
				MergeDownloadValidatorFileMetadata(it->fileInfo, uMediaLengthSec, eMediaLengthSource);
				return;
			}

			SReloadMapState::SPendingFuzzyRecord pendingRecord;
			SetDownloadValidatorFileInfo(pendingRecord.fileInfo, hash, filename, filesize, uMediaLengthSec, eMediaLengthSource);
			pendingRecord.uSourceFlags = uSourceFlags;
			m_pReloadMapState->pendingFuzzyRecords.push_back(pendingRecord);
		} catch (CMemoryException* ex) {
			ex->Delete();
			m_pReloadMapState->bFuzzyRestartRequired = true;
		} catch (const std::exception&) {
			m_pReloadMapState->bFuzzyRestartRequired = true;
		}
		return;
	}

	try {
		const uint64 uIdentity = BuildFuzzyIdentityKey(hash, filename, filesize);
		const std::pair<FuzzyIdentityIndex::iterator, FuzzyIdentityIndex::iterator> range = m_fuzzyIdentityIndex.equal_range(uIdentity);
		for (FuzzyIdentityIndex::iterator it = range.first; it != range.second; ++it) {
			if (it->second >= m_fuzzyRecords.size())
				continue;
			SFuzzyRecord& record = m_fuzzyRecords[it->second];
			if (!IsDownloadValidatorFileMatch(record, hash, filesize) || record.strName != filename)
				continue;

			record.uSourceFlags = static_cast<uint8>(record.uSourceFlags | uSourceFlags);
			MergeDownloadValidatorFileMetadata(record, uMediaLengthSec, eMediaLengthSource);
			RegisterFuzzyKnownTokenFrequencies(record);
			if (!record.bActive) {
				record.bActive = true;
				if (m_uFuzzyInactiveRecordCount != 0)
					--m_uFuzzyInactiveRecordCount;
			}
			return;
		}

		if (m_fuzzyRecords.size() >= static_cast<size_t>(_UI32_MAX)) {
			m_bFuzzyRebuildRecommended = true;
			return;
		}

		const CString strBoundaryName(BuildFuzzyBoundaryName(filename));
		const CString strNormalizedName(pNormalizedName != NULL ? *pNormalizedName : (thePrefs.GetDownloadValidatorIgnoreNonAlphaNumeric() ? RemoveNonAlphaNumeric(strBoundaryName) : strBoundaryName));
		if (strNormalizedName.GetLength() < max(3, thePrefs.GetDownloadValidatorMinimumComparisonLength()))
			return;

		std::vector<FuzzyGramType> grams;
		BuildFuzzyGrams(strNormalizedName, grams);
		if (grams.empty())
			return;

		SFuzzyRecord record;
		SetDownloadValidatorFileInfo(record, hash, filename, filesize, uMediaLengthSec, eMediaLengthSource);
		record.strNormalizedName = strNormalizedName;
		record.strBoundaryName = strBoundaryName;
		BuildFuzzyTokenHashes(BuildFuzzyBoundaryName(filename, true), record.tokenHashes);
		record.uFileType = static_cast<uint8>(GetED2KFileTypeID(filename));
		record.uSourceFlags = uSourceFlags;
		SDownloadValidatorFuzzyStructuralIdentity structuralIdentity;
		BuildFuzzyStructuralIdentity(filename, structuralIdentity);
		if (structuralIdentity.bHasID && (structuralIdentity.bHasYear || structuralIdentity.uGroupTokenCount != 0))
			record.uStructuralIdentityIndex = static_cast<uint32>(m_fuzzyStructuralIdentities.size());
		const uint32 uRecordID = static_cast<uint32>(m_fuzzyRecords.size());
		m_fuzzyRecords.push_back(record);
		if (record.uStructuralIdentityIndex != _UI32_MAX) {
			m_fuzzyStructuralIdentities.push_back(structuralIdentity);
			RegisterFuzzyStructuralIdentity(structuralIdentity);
			RegisterFuzzyStructuralRecord(structuralIdentity, uRecordID);
		}
		m_fuzzyIdentityIndex.emplace(uIdentity, uRecordID);
		RegisterFuzzyKnownTokenFrequencies(m_fuzzyRecords[uRecordID]);

		for (std::vector<FuzzyGramType>::const_iterator it = grams.begin(); it != grams.end(); ++it) {
			SFuzzyGramIndexEntry& entry = m_fuzzyGramIndex[*it];
			if (entry.uDocumentFrequency != _UI32_MAX)
				++entry.uDocumentFrequency;
			if (m_bFuzzyIndexReady)
				continue;
			if (entry.uDocumentFrequency <= m_uFuzzyMaximumPostingFrequency) {
				m_fuzzyDeltaPostingIndex[*it].push_back(uRecordID);
				++m_fuzzyRecords[uRecordID].uIndexedGramCount;
			} else if (entry.uDocumentFrequency == m_uFuzzyMaximumPostingFrequency + 1) {
				RemoveFuzzyGramPostings(*it, entry, CalculateFuzzyGramWeight(m_uFuzzyMaximumPostingFrequency));
			}
		}

		if (m_bFuzzyIndexReady)
			AddFuzzyRecordToReadyIndex(uRecordID, grams);
	} catch (CMemoryException* ex) {
		ex->Delete();
		AbortFuzzyIndexBuild();
	} catch (const std::exception&) {
		AbortFuzzyIndexBuild();
	}
}

void CDownloadValidator::AddFuzzyRecordToReadyIndex(uint32 uRecordID, const std::vector<FuzzyGramType>& grams)
{
	if (uRecordID >= m_fuzzyRecords.size())
		return;

	SFuzzyRecord& record = m_fuzzyRecords[uRecordID];
	record.uIndexedGramCount = 0;
	record.uIndexedGramWeight = 0;
	for (std::vector<FuzzyGramType>::const_iterator it = grams.begin(); it != grams.end(); ++it) {
		FuzzyGramIndex::iterator itEntry = m_fuzzyGramIndex.find(*it);
		if (itEntry == m_fuzzyGramIndex.end())
			continue;
		SFuzzyGramIndexEntry& entry = itEntry->second;
		const uint32 uOldDocumentFrequency = entry.uDocumentFrequency != 0 ? entry.uDocumentFrequency - 1 : 0;
		const uint32 uOldWeight = CalculateFuzzyGramWeight(uOldDocumentFrequency);
		const uint32 uNewWeight = CalculateFuzzyGramWeight(entry.uDocumentFrequency);
		if (uNewWeight == 0) {
			if (uOldWeight != 0)
				RemoveFuzzyGramPostings(*it, entry, uOldWeight);
			continue;
		}
		if (uOldWeight != 0 && uOldWeight != uNewWeight)
			AdjustFuzzyGramRecordWeights(*it, entry, uOldWeight, uNewWeight);
		m_fuzzyDeltaPostingIndex[*it].push_back(uRecordID);
		++record.uIndexedGramCount;
		record.uIndexedGramWeight += uNewWeight;
	}
	if (m_fuzzyCandidateSharedGramCounts.size() < m_fuzzyRecords.size())
		m_fuzzyCandidateSharedGramCounts.resize(m_fuzzyRecords.size(), 0);
	if (m_fuzzyCandidateSharedGramWeights.size() < m_fuzzyRecords.size())
		m_fuzzyCandidateSharedGramWeights.resize(m_fuzzyRecords.size(), 0);
	if (m_fuzzyCandidateTouchedFlags.size() < m_fuzzyRecords.size())
		m_fuzzyCandidateTouchedFlags.resize(m_fuzzyRecords.size(), 0);
}

void CDownloadValidator::AdjustFuzzyGramRecordWeights(FuzzyGramType gram, const SFuzzyGramIndexEntry& entry, uint32 uOldWeight, uint32 uNewWeight)
{
	if (uOldWeight == uNewWeight)
		return;
	const auto AdjustRecord = [this, uOldWeight, uNewWeight](uint32 uRecordID) {
		if (uRecordID >= m_fuzzyRecords.size())
			return;
		SFuzzyRecord& record = m_fuzzyRecords[uRecordID];
		if (record.uIndexedGramWeight >= uOldWeight)
			record.uIndexedGramWeight = record.uIndexedGramWeight - uOldWeight + uNewWeight;
	};
	for (uint32 i = 0; i < entry.uBasePostingCount; ++i) {
		const size_t uPostingIndex = static_cast<size_t>(entry.uPostingOffset) + i;
		if (uPostingIndex >= m_fuzzyPostings.size())
			break;
		AdjustRecord(m_fuzzyPostings[uPostingIndex]);
	}
	FuzzyDeltaPostingIndex::const_iterator itDelta = m_fuzzyDeltaPostingIndex.find(gram);
	if (itDelta != m_fuzzyDeltaPostingIndex.end()) {
		for (std::vector<uint32>::const_iterator it = itDelta->second.begin(); it != itDelta->second.end(); ++it)
			AdjustRecord(*it);
	}
}

void CDownloadValidator::RemoveFuzzyGramPostings(FuzzyGramType gram, const SFuzzyGramIndexEntry& entry, uint32 uGramWeight)
{
	const auto RemoveRecordGram = [this, uGramWeight](uint32 uRecordID) {
		if (uRecordID >= m_fuzzyRecords.size())
			return;
		SFuzzyRecord& record = m_fuzzyRecords[uRecordID];
		if (record.uIndexedGramCount != 0)
			--record.uIndexedGramCount;
		if (record.uIndexedGramWeight >= uGramWeight)
			record.uIndexedGramWeight -= uGramWeight;
		else
			record.uIndexedGramWeight = 0;
	};
	for (uint32 i = 0; i < entry.uBasePostingCount; ++i) {
		const size_t uPostingIndex = static_cast<size_t>(entry.uPostingOffset) + i;
		if (uPostingIndex >= m_fuzzyPostings.size())
			break;
		RemoveRecordGram(m_fuzzyPostings[uPostingIndex]);
	}

	FuzzyDeltaPostingIndex::iterator itDelta = m_fuzzyDeltaPostingIndex.find(gram);
	if (itDelta == m_fuzzyDeltaPostingIndex.end())
		return;
	for (std::vector<uint32>::const_iterator it = itDelta->second.begin(); it != itDelta->second.end(); ++it)
		RemoveRecordGram(*it);
	m_fuzzyDeltaPostingIndex.erase(itDelta);
}

void CDownloadValidator::RemoveFuzzyRecord(const uchar* hash, const CString& filename, EMFileSize filesize)
{
	if (!IsFuzzyMatchingEnabled() || hash == NULL || m_fuzzyRecords.empty())
		return;

	const uint64 uIdentity = BuildFuzzyIdentityKey(hash, filename, filesize);
	const std::pair<FuzzyIdentityIndex::iterator, FuzzyIdentityIndex::iterator> range = m_fuzzyIdentityIndex.equal_range(uIdentity);
	for (FuzzyIdentityIndex::iterator it = range.first; it != range.second; ++it) {
		if (it->second >= m_fuzzyRecords.size())
			continue;
		SFuzzyRecord& record = m_fuzzyRecords[it->second];
		if (!record.bActive || !IsDownloadValidatorFileMatch(record, hash, filesize) || record.strName != filename)
			continue;
		record.bActive = false;
		++m_uFuzzyInactiveRecordCount;
	}

	if (!m_bFuzzyRebuildRecommended && m_uFuzzyInactiveRecordCount >= DOWNLOAD_VALIDATOR_FUZZY_INACTIVE_REBUILD_MINIMUM
		&& static_cast<uint64>(m_uFuzzyInactiveRecordCount) * 100 >= static_cast<uint64>(m_fuzzyRecords.size()) * DOWNLOAD_VALIDATOR_FUZZY_INACTIVE_REBUILD_PERCENT) {
		m_bFuzzyRebuildRecommended = true;
		AddDebugLogLine(DLP_LOW, false, _T("Download Validator fuzzy index will be compacted during the next full map reload."));
	}
}

void CDownloadValidator::StartFuzzyPostingPreparation(SReloadMapState& state)
{
	const uint32 uRecordCount = static_cast<uint32>(m_fuzzyRecords.size());
	uint32 uRatioLimit = uRecordCount / DOWNLOAD_VALIDATOR_FUZZY_POSTING_RATIO_DIVISOR;
	uRatioLimit = max(uRatioLimit, DOWNLOAD_VALIDATOR_FUZZY_MINIMUM_POSTING_FREQUENCY);
	m_uFuzzyMaximumPostingFrequency = min(uRatioLimit, DOWNLOAD_VALIDATOR_FUZZY_MAXIMUM_POSTING_FREQUENCY);
	m_uFuzzyWeightRecordCount = uRecordCount >= m_uFuzzyInactiveRecordCount ? uRecordCount - m_uFuzzyInactiveRecordCount : 0;
	m_fuzzyPostings.clear();
	for (std::vector<SFuzzyRecord>::iterator it = m_fuzzyRecords.begin(); it != m_fuzzyRecords.end(); ++it) {
		it->uIndexedGramCount = 0;
		it->uIndexedGramWeight = 0;
	}
	state.uFuzzyPostingCount = 0;
	state.itFuzzyGram = m_fuzzyGramIndex.begin();
	state.bFuzzyPrepareStarted = true;
}

bool CDownloadValidator::ProcessFuzzyPostingPreparation(SReloadMapState& state, DWORD dwSliceStart, DWORD dwSliceBudgetMs, UINT uMaxItemsPerSlice, UINT& uProcessed, bool bDrainAll)
{
	if (!state.bFuzzyPrepareStarted)
		StartFuzzyPostingPreparation(state);

	while (state.itFuzzyGram != m_fuzzyGramIndex.end()) {
		const FuzzyGramType gram = state.itFuzzyGram->first;
		SFuzzyGramIndexEntry& entry = state.itFuzzyGram->second;
		entry.uPostingOffset = 0;
		entry.uBasePostingCount = 0;
		const uint32 uGramWeight = CalculateFuzzyGramWeight(entry.uDocumentFrequency);
		if (uGramWeight == 0)
			RemoveFuzzyGramPostings(gram, entry, CalculateFuzzyGramWeight(m_uFuzzyMaximumPostingFrequency));
		else {
			FuzzyDeltaPostingIndex::const_iterator itPosting = m_fuzzyDeltaPostingIndex.find(gram);
			if (itPosting != m_fuzzyDeltaPostingIndex.end()) {
				state.uFuzzyPostingCount += itPosting->second.size();
				for (std::vector<uint32>::const_iterator itRecord = itPosting->second.begin(); itRecord != itPosting->second.end(); ++itRecord) {
					if (*itRecord >= m_fuzzyRecords.size())
						continue;
					++m_fuzzyRecords[*itRecord].uIndexedGramCount;
					m_fuzzyRecords[*itRecord].uIndexedGramWeight += uGramWeight;
				}
			}
		}
		++state.itFuzzyGram;
		++uProcessed;
		if (!bDrainAll && IsDownloadValidatorReloadSliceComplete(dwSliceStart, dwSliceBudgetMs, uMaxItemsPerSlice, uProcessed))
			return true;
	}

	try {
		m_fuzzyCandidateSharedGramCounts.assign(m_fuzzyRecords.size(), 0);
		m_fuzzyCandidateSharedGramWeights.assign(m_fuzzyRecords.size(), 0);
		m_fuzzyCandidateTouchedFlags.assign(m_fuzzyRecords.size(), 0);
	} catch (const std::exception&) {
		AbortFuzzyIndexBuild();
		state.ePhase = SReloadMapState::PhaseDone;
		return false;
	}

	m_bFuzzyIndexReady = true;
	::InterlockedExchange(&m_lFuzzyIndexReadySnapshot, 1);
	m_fuzzyTouchedRecordIDs.clear();
	AddDebugLogLine(DLP_LOW, false, _T("Download Validator fuzzy index prepared %u records and %I64u postings."), static_cast<UINT>(m_fuzzyRecords.size()), state.uFuzzyPostingCount);
	state.ePhase = SReloadMapState::PhaseDone;
	return false;
}

void CDownloadValidator::AbortFuzzyIndexBuild()
{
	m_fuzzyRecords.clear();
	m_fuzzyStructuralIdentities.clear();
	m_fuzzyStructuralGroupStats.clear();
	m_fuzzyKnownTokenFrequencies.clear();
	m_fuzzyGramIndex.clear();
	m_fuzzyDeltaPostingIndex.clear();
	m_fuzzyIdentityIndex.clear();
	m_fuzzyStructuralYearIDIndex.clear();
	m_fuzzyStructuralIDIndex.clear();
	m_fuzzyPostings.clear();
	m_fuzzyCandidateSharedGramCounts.clear();
	m_fuzzyCandidateSharedGramWeights.clear();
	m_fuzzyCandidateTouchedFlags.clear();
	m_fuzzyTouchedRecordIDs.clear();
	ClearFuzzyCandidateCache();
	m_uFuzzyKnownTokenDocumentCount = 0;
	m_bFuzzyIndexReady = false;
	::InterlockedExchange(&m_lFuzzyIndexReadySnapshot, 0);
	m_bFuzzyIndexAvailable = false;
	m_bFuzzyRebuildRecommended = true;
	AddDebugLogLine(DLP_HIGH, false, _T("Download Validator fuzzy index preparation failed and the feature was disabled until the next full map reload."));
}

bool CDownloadValidator::EvaluateFuzzyCandidatesInternal(const SDownloadValidatorFuzzyQueryData& queryData, std::vector<FuzzyCandidateType>* pCandidates,
	const uchar* hash, EMFileSize filesize, uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource, FuzzyCandidateType* pBestCandidate,
	FuzzyCandidateType* pCompetingCandidate, size_t uMaximumCandidates, bool bAllowIncompleteIndex)
{
	if (!IsFuzzyMatchingEnabled() || !m_bFuzzyIndexAvailable || (!m_bFuzzyIndexReady && !bAllowIncompleteIndex) || !queryData.bPrepared
		|| (queryData.grams.empty() && !queryData.structuralIdentity.bHasID))
		return false;

	for (std::vector<uint32>::const_iterator it = m_fuzzyTouchedRecordIDs.begin(); it != m_fuzzyTouchedRecordIDs.end(); ++it) {
		if (*it < m_fuzzyCandidateSharedGramCounts.size())
			m_fuzzyCandidateSharedGramCounts[*it] = 0;
		if (*it < m_fuzzyCandidateSharedGramWeights.size())
			m_fuzzyCandidateSharedGramWeights[*it] = 0;
		if (*it < m_fuzzyCandidateTouchedFlags.size())
			m_fuzzyCandidateTouchedFlags[*it] = 0;
	}
	m_fuzzyTouchedRecordIDs.clear();
	if (m_fuzzyCandidateSharedGramCounts.size() < m_fuzzyRecords.size())
		m_fuzzyCandidateSharedGramCounts.resize(m_fuzzyRecords.size(), 0);
	if (m_fuzzyCandidateSharedGramWeights.size() < m_fuzzyRecords.size())
		m_fuzzyCandidateSharedGramWeights.resize(m_fuzzyRecords.size(), 0);
	if (m_fuzzyCandidateTouchedFlags.size() < m_fuzzyRecords.size())
		m_fuzzyCandidateTouchedFlags.resize(m_fuzzyRecords.size(), 0);

	const auto TouchRecord = [this](uint32 uRecordID) {
		if (uRecordID >= m_fuzzyCandidateTouchedFlags.size() || m_fuzzyCandidateTouchedFlags[uRecordID] != 0)
			return;
		m_fuzzyCandidateTouchedFlags[uRecordID] = 1;
		m_fuzzyTouchedRecordIDs.push_back(uRecordID);
	};
	const auto AccumulateRecord = [this, &TouchRecord](uint32 uRecordID, uint32 uGramWeight) {
		if (uRecordID >= m_fuzzyCandidateSharedGramCounts.size() || uRecordID >= m_fuzzyCandidateSharedGramWeights.size())
			return;
		TouchRecord(uRecordID);
		++m_fuzzyCandidateSharedGramCounts[uRecordID];
		m_fuzzyCandidateSharedGramWeights[uRecordID] += uGramWeight;
	};

	std::vector<uint32> structuralRecordIDs;
	AddFuzzyStructuralCandidates(queryData.structuralIdentity, structuralRecordIDs);
	for (std::vector<uint32>::const_iterator it = structuralRecordIDs.begin(); it != structuralRecordIDs.end(); ++it)
		TouchRecord(*it);

	uint32 uQueryIndexedGramCount = 0;
	uint64 uQueryIndexedGramWeight = 0;
	for (std::vector<uint64>::const_iterator itGram = queryData.grams.begin(); itGram != queryData.grams.end(); ++itGram) {
		FuzzyGramIndex::const_iterator itEntry = m_fuzzyGramIndex.find(*itGram);
		if (itEntry == m_fuzzyGramIndex.end() || itEntry->second.uDocumentFrequency > m_uFuzzyMaximumPostingFrequency)
			continue;

		const uint32 uGramWeight = CalculateFuzzyGramWeight(itEntry->second.uDocumentFrequency);
		if (uGramWeight == 0)
			continue;
		bool bHasPosting = false;
		const SFuzzyGramIndexEntry& entry = itEntry->second;
		for (uint32 i = 0; i < entry.uBasePostingCount; ++i) {
			const size_t uPostingIndex = static_cast<size_t>(entry.uPostingOffset) + i;
			if (uPostingIndex >= m_fuzzyPostings.size())
				break;
			AccumulateRecord(m_fuzzyPostings[uPostingIndex], uGramWeight);
			bHasPosting = true;
		}
		FuzzyDeltaPostingIndex::const_iterator itDelta = m_fuzzyDeltaPostingIndex.find(*itGram);
		if (itDelta != m_fuzzyDeltaPostingIndex.end()) {
			for (std::vector<uint32>::const_iterator itRecord = itDelta->second.begin(); itRecord != itDelta->second.end(); ++itRecord)
				AccumulateRecord(*itRecord, uGramWeight);
			bHasPosting = bHasPosting || !itDelta->second.empty();
		}
		if (bHasPosting) {
			++uQueryIndexedGramCount;
			uQueryIndexedGramWeight += uGramWeight;
		}
	}

	bool bFoundCandidate = false;
	std::vector<FuzzyTokenType> orderedCandidateTokens;
	const uint32 uDecisionThreshold = thePrefs.GetDownloadValidatorFuzzySimilarityThreshold();
	for (std::vector<uint32>::const_iterator it = m_fuzzyTouchedRecordIDs.begin(); it != m_fuzzyTouchedRecordIDs.end(); ++it) {
		const uint32 uRecordID = *it;
		if (uRecordID >= m_fuzzyRecords.size())
			continue;
		const uint32 uSharedGramCount = m_fuzzyCandidateSharedGramCounts[uRecordID];
		const uint64 uSharedGramWeight = m_fuzzyCandidateSharedGramWeights[uRecordID];
		m_fuzzyCandidateSharedGramCounts[uRecordID] = 0;
		m_fuzzyCandidateSharedGramWeights[uRecordID] = 0;
		const SFuzzyRecord& record = m_fuzzyRecords[uRecordID];
		if (!record.bActive)
			continue;

		SFuzzyStructuralMatch structuralMatch;
		if (record.uStructuralIdentityIndex < m_fuzzyStructuralIdentities.size())
			structuralMatch = EvaluateFuzzyStructuralIdentity(queryData.structuralIdentity, m_fuzzyStructuralIdentities[record.uStructuralIdentityIndex]);
		if (structuralMatch.bConflict)
			continue;

		const bool bExactSubstring = (!record.strNormalizedName.IsEmpty() && (queryData.strNormalizedName.Find(record.strNormalizedName) >= 0 || record.strNormalizedName.Find(queryData.strNormalizedName) >= 0))
			|| (!record.strBoundaryName.IsEmpty() && (queryData.strBoundaryName.Find(record.strBoundaryName) >= 0 || record.strBoundaryName.Find(queryData.strBoundaryName) >= 0));
		orderedCandidateTokens.clear();
		uint32 uSharedTokenCount = 0;
		uint32 uTokenCoveragePercent = 0;
		uint32 uSequenceQuality = 0;
		uint32 uLongestRunTokens = 0;
		uint32 uLongestRunCoveragePercent = 0;
		uint32 uTotalRunCoveragePercent = 0;
		uint32 uTokenSimilarityScore = CalculateFuzzyTokenSimilarityScore(queryData.tokenHashes, queryData.orderedTokenHashes, record.tokenHashes, orderedCandidateTokens,
			uSharedTokenCount, uTokenCoveragePercent, uSequenceQuality, uLongestRunTokens, uLongestRunCoveragePercent, uTotalRunCoveragePercent);
		if (uSharedTokenCount >= 2 && queryData.orderedTokenHashes.size() >= 2) {
			BuildFuzzyOrderedTokenHashes(BuildFuzzyBoundaryName(record.strName, true, true), orderedCandidateTokens);
			uTokenSimilarityScore = CalculateFuzzyTokenSimilarityScore(queryData.tokenHashes, queryData.orderedTokenHashes, record.tokenHashes, orderedCandidateTokens,
				uSharedTokenCount, uTokenCoveragePercent, uSequenceQuality, uLongestRunTokens, uLongestRunCoveragePercent, uTotalRunCoveragePercent);
		}
		const bool bTokenEvidence = uSharedTokenCount >= thePrefs.GetDownloadValidatorFuzzyMinimumSharedTokens()
			&& uTokenCoveragePercent >= thePrefs.GetDownloadValidatorFuzzyMinimumTokenCoveragePercent();
		const bool bTokenSequenceEvidence = uSharedTokenCount >= 2 && uSequenceQuality != 0;
		const bool bLexicalTokenScoreApplicable = queryData.tokenHashes.size() >= 2;
		const uint32 uLengthSimilarity = CalculateDownloadValidatorLengthSimilarity(queryData.strNormalizedName, record.strNormalizedName);
		uint32 uEditSimilarity = 0;
		if (uLengthSimilarity >= thePrefs.GetDownloadValidatorFuzzyMinimumLengthSimilarityPercent())
			uEditSimilarity = CalculateDownloadValidatorEditSimilarity(queryData.strNormalizedName, record.strNormalizedName);
		const bool bEditEvidence = uLengthSimilarity >= thePrefs.GetDownloadValidatorFuzzyMinimumLengthSimilarityPercent()
			&& uEditSimilarity >= thePrefs.GetDownloadValidatorFuzzyMinimumEditSimilarityPercent();
		const bool bEditFallbackEvidence = !bLexicalTokenScoreApplicable && bEditEvidence;
		if (!bExactSubstring && !bTokenEvidence && !bTokenSequenceEvidence && !bEditFallbackEvidence && !structuralMatch.bMatch)
			continue;
		if ((record.uIndexedGramWeight == 0 || uQueryIndexedGramWeight == 0) && !structuralMatch.bMatch)
			continue;

		FuzzyCandidateType candidate;
		SetDownloadValidatorFileInfo(candidate, record.ucHash, record.strName, record.uSize, record.uMediaLengthSec, record.eMediaLengthSource);
		candidate.uRecordID = uRecordID;
		candidate.strNormalizedName = record.strNormalizedName;
		candidate.strBoundaryName = record.strBoundaryName;
		candidate.uSharedGramCount = uSharedGramCount;
		candidate.uQueryGramCount = uQueryIndexedGramCount;
		candidate.uCandidateGramCount = record.uIndexedGramCount;
		candidate.uFileType = record.uFileType;
		candidate.uSourceFlags = record.uSourceFlags;
		candidate.uStructuralIdentityKey = structuralMatch.uIdentityKey;
		candidate.bExactSubstring = bExactSubstring;
		candidate.bStructuralIdentityMatch = structuralMatch.bMatch;
		candidate.uEditSimilarityScore = uEditSimilarity;
		candidate.bFileTypeConflict = queryData.uFileType != static_cast<uint8>(ED2KFT_ANY) && candidate.uFileType != static_cast<uint8>(ED2KFT_ANY) && queryData.uFileType != candidate.uFileType;
		if (record.uIndexedGramWeight != 0 && uQueryIndexedGramWeight != 0)
			candidate.uSimilarityScore = CalculateFuzzySimilarityScore(queryData.strNormalizedName, queryData.strBoundaryName, uQueryIndexedGramWeight, record.uIndexedGramWeight, uSharedGramWeight, candidate);
		if (queryData.strNormalizedName != record.strNormalizedName && bLexicalTokenScoreApplicable)
			candidate.uSimilarityScore = uTokenSimilarityScore;
		else if (!candidate.bStructuralIdentityMatch && queryData.strNormalizedName != record.strNormalizedName && !candidate.bExactSubstring)
			candidate.uSimilarityScore = (std::min)(candidate.uSimilarityScore, uEditSimilarity);
		if (structuralMatch.bMatch) {
			uint32 uStructuralScoreFloor = structuralMatch.uScoreFloor;
			if (structuralMatch.bWeakMatch) {
				uStructuralScoreFloor = (std::min)(DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_WEAK_MATCH_SCORE, uTokenSimilarityScore + DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_WEAK_MATCH_BONUS);
				uStructuralScoreFloor = (std::max)(DOWNLOAD_VALIDATOR_FUZZY_STRUCTURAL_WEAK_MATCH_MINIMUM_SCORE, uStructuralScoreFloor);
			}
			candidate.uSimilarityScore = (std::max)(candidate.uSimilarityScore, uStructuralScoreFloor);
		}
		bFoundCandidate = true;

		if (pCandidates != NULL) {
			pCandidates->push_back(candidate);
			if (uMaximumCandidates != 0 && pCandidates->size() >= uMaximumCandidates)
				break;
		}

		if (pBestCandidate == NULL || pCompetingCandidate == NULL || candidate.uSimilarityScore < uDecisionThreshold || candidate.bFileTypeConflict
			|| (hash != NULL && IsDownloadValidatorFileMatch(candidate, hash, filesize))
			|| !IsMediaLengthCandidateAllowed(uMediaLengthSec, eMediaLengthSource, candidate) || !IsFileSizeCandidateAllowed(filesize, candidate))
			continue;
		if (pBestCandidate->strName.IsEmpty()) {
			*pBestCandidate = candidate;
			continue;
		}
		if (candidate.strNormalizedName == pBestCandidate->strNormalizedName
			|| (candidate.bStructuralIdentityMatch && pBestCandidate->bStructuralIdentityMatch && candidate.uStructuralIdentityKey != 0
			&& candidate.uStructuralIdentityKey == pBestCandidate->uStructuralIdentityKey)) {
			if (IsDownloadValidatorFuzzyCandidateBetter(candidate, *pBestCandidate))
				*pBestCandidate = candidate;
			continue;
		}
		if (IsDownloadValidatorFuzzyCandidateBetter(candidate, *pBestCandidate)) {
			const FuzzyCandidateType previousBest = *pBestCandidate;
			*pBestCandidate = candidate;
			*pCompetingCandidate = previousBest;
			continue;
		}
		if (pCompetingCandidate->strName.IsEmpty() || IsDownloadValidatorFuzzyCandidateBetter(candidate, *pCompetingCandidate))
			*pCompetingCandidate = candidate;
	}

	for (std::vector<uint32>::const_iterator it = m_fuzzyTouchedRecordIDs.begin(); it != m_fuzzyTouchedRecordIDs.end(); ++it) {
		if (*it < m_fuzzyCandidateSharedGramCounts.size())
			m_fuzzyCandidateSharedGramCounts[*it] = 0;
		if (*it < m_fuzzyCandidateSharedGramWeights.size())
			m_fuzzyCandidateSharedGramWeights[*it] = 0;
		if (*it < m_fuzzyCandidateTouchedFlags.size())
			m_fuzzyCandidateTouchedFlags[*it] = 0;
	}
	m_fuzzyTouchedRecordIDs.clear();
	return bFoundCandidate;
}

bool CDownloadValidator::FindFuzzyCandidates(const CString& filename, std::vector<FuzzyCandidateType>& candidates, size_t uMaximumCandidates, bool bAllowIncompleteIndex,
	SDownloadValidatorFuzzyQueryData* pQueryData)
{
	candidates.clear();
	SDownloadValidatorFuzzyQueryData localQueryData;
	SDownloadValidatorFuzzyQueryData& queryData = pQueryData != NULL ? *pQueryData : localQueryData;
	if (filename.IsEmpty() || !PrepareFuzzyQueryData(filename, queryData))
		return false;

	CSingleLock indexLock(&m_indexLock, TRUE);
	try {
		const uint32 uRevision = GetFuzzyCandidateDataRevision();
		const uint32 uCandidateFingerprint = GetFuzzyCandidateFingerprint();
		if (!bAllowIncompleteIndex) {
			std::list<SFuzzyCandidateCacheEntry>::iterator itCache = FindFuzzyCandidateCacheEntry(queryData, uRevision, uCandidateFingerprint);
			if (itCache != m_fuzzyCandidateCache.end()) {
				if (!itCache->bHasCandidates)
					return false;
				candidates = itCache->candidates;
				if (uMaximumCandidates != 0 && candidates.size() > uMaximumCandidates)
					candidates.resize(uMaximumCandidates);
				return !candidates.empty();
			}
		}

		const bool bHasCandidates = EvaluateFuzzyCandidatesInternal(queryData, &candidates, NULL, static_cast<uint64>(0), 0,
			FuzzyMediaLengthUnknown, NULL, NULL, uMaximumCandidates, bAllowIncompleteIndex);
		if (bHasCandidates)
			std::sort(candidates.begin(), candidates.end(), IsDownloadValidatorFuzzyCandidateBetter);
		if (!bAllowIncompleteIndex && m_bFuzzyIndexReady && (!bHasCandidates || uMaximumCandidates == 0)) {
			SFuzzyCandidateCacheEntry entry;
			entry.uRevision = uRevision;
			entry.uCandidateFingerprint = uCandidateFingerprint;
			entry.uStructuralIdentityKey = queryData.uStructuralIdentityKey;
			entry.strSourceFileName = queryData.strSourceFileName;
			entry.strNormalizedName = queryData.strNormalizedName;
			entry.strBoundaryName = queryData.strBoundaryName;
			entry.uFileType = queryData.uFileType;
			entry.bHasCandidates = bHasCandidates;
			entry.candidates = candidates;
			StoreFuzzyCandidateCacheEntry(entry);
		}
		return bHasCandidates;
	} catch (CMemoryException* ex) {
		ex->Delete();
	} catch (const std::exception&) {
	}

	for (std::vector<uint32>::const_iterator it = m_fuzzyTouchedRecordIDs.begin(); it != m_fuzzyTouchedRecordIDs.end(); ++it) {
		if (*it < m_fuzzyCandidateSharedGramCounts.size())
			m_fuzzyCandidateSharedGramCounts[*it] = 0;
		if (*it < m_fuzzyCandidateSharedGramWeights.size())
			m_fuzzyCandidateSharedGramWeights[*it] = 0;
	}
	m_fuzzyTouchedRecordIDs.clear();
	candidates.clear();
	return false;
}

bool CDownloadValidator::FindFuzzyDecisionCandidateStreaming(const uchar* hash, const CString& filename, EMFileSize filesize, uint32 uMediaLengthSec,
	EFuzzyMediaLengthSource eMediaLengthSource, SDownloadValidatorFuzzyQueryData* pQueryData, FuzzyCandidateType& candidate)
{
	SDownloadValidatorFuzzyQueryData localQueryData;
	SDownloadValidatorFuzzyQueryData& queryData = pQueryData != NULL ? *pQueryData : localQueryData;
	if (filename.IsEmpty() || !PrepareFuzzyQueryData(filename, queryData))
		return false;

	CSingleLock indexLock(&m_indexLock, TRUE);
	try {
		const uint32 uRevision = GetFuzzyCandidateDataRevision();
		const uint32 uCandidateFingerprint = GetFuzzyCandidateFingerprint();
		std::list<SFuzzyCandidateCacheEntry>::iterator itCache = FindFuzzyCandidateCacheEntry(queryData, uRevision, uCandidateFingerprint);
		if (itCache != m_fuzzyCandidateCache.end()) {
			if (!itCache->bHasCandidates)
				return false;
			const FuzzyCandidateType* pCachedCandidate = FindFuzzyDecisionCandidate(hash, filesize, uMediaLengthSec, eMediaLengthSource, itCache->candidates);
			if (pCachedCandidate == NULL)
				return false;
			candidate = *pCachedCandidate;
			return true;
		}

		FuzzyCandidateType bestCandidate;
		FuzzyCandidateType competingCandidate;
		if (!EvaluateFuzzyCandidatesInternal(queryData, NULL, hash, filesize, uMediaLengthSec, eMediaLengthSource, &bestCandidate, &competingCandidate, 0, false)) {
			if (m_bFuzzyIndexReady) {
				SFuzzyCandidateCacheEntry entry;
				entry.uRevision = uRevision;
				entry.uCandidateFingerprint = uCandidateFingerprint;
				entry.uStructuralIdentityKey = queryData.uStructuralIdentityKey;
				entry.strSourceFileName = queryData.strSourceFileName;
				entry.strNormalizedName = queryData.strNormalizedName;
				entry.strBoundaryName = queryData.strBoundaryName;
				entry.uFileType = queryData.uFileType;
				entry.bHasCandidates = false;
				StoreFuzzyCandidateCacheEntry(entry);
			}
			return false;
		}
		if (bestCandidate.strName.IsEmpty())
			return false;
		if (!competingCandidate.strName.IsEmpty() && bestCandidate.uSimilarityScore - competingCandidate.uSimilarityScore <= DOWNLOAD_VALIDATOR_FUZZY_AMBIGUITY_MARGIN)
			return false;
		candidate = bestCandidate;
		return true;
	} catch (CMemoryException* ex) {
		ex->Delete();
	} catch (const std::exception&) {
	}
	return false;
}


bool CDownloadValidator::IsPossibleKnownSearchReady() const
{
	return IsMapInitialized() && ::InterlockedCompareExchange(const_cast<LONG*>(&m_lRegexReloadActive), 0, 0) == 0;
}

uint32 CDownloadValidator::GetPossibleKnownRevision() const
{
	return static_cast<uint32>(::InterlockedCompareExchange(const_cast<LONG*>(&m_lPossibleKnownRevision), 0, 0));
}

uint32 CDownloadValidator::GetCandidateDataRevision() const
{
	return GetFuzzyCandidateDataRevision();
}

uint32 CDownloadValidator::GetEvaluationRevision() const
{
	return static_cast<uint32>(::InterlockedCompareExchange(const_cast<LONG*>(&m_lEvaluationRevision), 0, 0));
}

uint32 CDownloadValidator::GetFuzzyCandidateDataRevision() const
{
	return static_cast<uint32>(::InterlockedCompareExchange(const_cast<LONG*>(&m_lFuzzyCandidateDataRevision), 0, 0));
}

bool CDownloadValidator::EvaluateSearchResultSimilarity(const uchar* hash, const CString& filename, EMFileSize filesize, uint32 uMediaLengthSec,
	EFuzzyMediaLengthSource eMediaLengthSource, uint32& uRevision, SDownloadValidatorFuzzyQueryData* pQueryData, std::vector<FuzzyCandidateType>* pPreparedFuzzyCandidates)
{
	CSingleLock indexLock(&m_indexLock, TRUE);
	uRevision = GetEvaluationRevision();
	if (pPreparedFuzzyCandidates != NULL)
		pPreparedFuzzyCandidates->clear();
	const int iDownloadValidatorMode = thePrefs.GetDownloadValidator();
	if ((iDownloadValidatorMode != 2 && iDownloadValidatorMode != 3) || filename.IsEmpty() || !IsMapInitialized() || filename.GetLength() < thePrefs.GetDownloadValidatorMinimumComparisonLength())
		return false;

	const FileInfoType* pTrustedFileInfo = NULL;
	const CString strProcessedFileName(BuildMapKey(filename));
	const DownloadValidatorFileMap::CPair* pFilePair = m_DownloadValidatorMap.PLookup(strProcessedFileName);
	if (pFilePair != NULL)
		pTrustedFileInfo = &pFilePair->value;

	if (pTrustedFileInfo == NULL && thePrefs.GetDownloadValidatorDateTimeMatching()) {
		const CString strDateTimeMapKey(BuildDateTimeMapKey(filename, strProcessedFileName));
		if (!strDateTimeMapKey.IsEmpty()) {
			pFilePair = m_DownloadValidatorDateTimeMap.PLookup(strDateTimeMapKey);
			if (pFilePair != NULL)
				pTrustedFileInfo = &pFilePair->value;
		}
	}

	if (pTrustedFileInfo == NULL)
		pTrustedFileInfo = FindRegexFileInfo(filename);
	const bool bTrustedSimilar = pTrustedFileInfo != NULL && FindEligibleFileCandidate(*pTrustedFileInfo, filesize, uMediaLengthSec, eMediaLengthSource) != NULL;
	if (pTrustedFileInfo != NULL && pPreparedFuzzyCandidates == NULL)
		return bTrustedSimilar;

	SDownloadValidatorFuzzyQueryData localQueryData;
	SDownloadValidatorFuzzyQueryData* pPreparedQueryData = pQueryData != NULL ? pQueryData : &localQueryData;
	if (!IsFuzzyMatchingEnabled() || !PrepareFuzzyQueryData(filename, *pPreparedQueryData))
		return bTrustedSimilar;
	if (pPreparedFuzzyCandidates != NULL) {
		if (!FindFuzzyCandidates(filename, *pPreparedFuzzyCandidates, 0, false, pPreparedQueryData))
			return bTrustedSimilar;
		if (pTrustedFileInfo != NULL)
			return bTrustedSimilar;
		return FindFuzzyDecisionCandidate(hash, filesize, uMediaLengthSec, eMediaLengthSource, *pPreparedFuzzyCandidates) != NULL;
	}
	FuzzyCandidateType candidate;
	return FindFuzzyDecisionCandidateStreaming(hash, filename, filesize, uMediaLengthSec, eMediaLengthSource, pPreparedQueryData, candidate);
}

bool CDownloadValidator::FindPossibleKnownCandidates(const uchar* hash, const CString& filename, EMFileSize filesize, uint32 uMediaLengthSec,
	EFuzzyMediaLengthSource eMediaLengthSource, std::vector<FuzzyCandidateType>& candidates, size_t uMaximumCandidates, bool bResolveMetadata,
	SDownloadValidatorFuzzyQueryData* pQueryData, bool bIncludeFuzzy, bool bIncludeTrusted, const std::vector<FuzzyCandidateType>* pPreparedFuzzyCandidates)
{
	candidates.clear();
	if (thePrefs.GetDownloadValidator() == 0 || filename.IsEmpty() || !IsMapInitialized() || filename.GetLength() < thePrefs.GetDownloadValidatorMinimumComparisonLength())
		return false;

	SDownloadValidatorFuzzyQueryData localQueryData;
	SDownloadValidatorFuzzyQueryData* pPreparedQueryData = pQueryData != NULL ? pQueryData : &localQueryData;
	const bool bFuzzyQueryPrepared = bIncludeFuzzy && IsFuzzyMatchingEnabled()
		&& (pPreparedFuzzyCandidates != NULL || PrepareFuzzyQueryData(filename, *pPreparedQueryData));
	CSingleLock indexLock(&m_indexLock, TRUE);
	try {
		std::unordered_multimap<uint64, size_t> candidateIdentities;
		const auto AddUniqueCandidate = [this, &candidates, &candidateIdentities, uMediaLengthSec, eMediaLengthSource](const FuzzyCandidateType& candidate) {
			if (!IsMediaLengthCandidateAllowed(uMediaLengthSec, eMediaLengthSource, candidate))
				return;
			const uint64 uIdentity = BuildFuzzyIdentityKey(candidate.ucHash, candidate.strName, candidate.uSize);
			const auto range = candidateIdentities.equal_range(uIdentity);
			for (std::unordered_multimap<uint64, size_t>::const_iterator it = range.first; it != range.second; ++it) {
				if (it->second < candidates.size() && IsDownloadValidatorFileMatch(candidates[it->second], candidate.ucHash, candidate.uSize) && candidates[it->second].strName == candidate.strName)
					return;
			}
			candidateIdentities.emplace(uIdentity, candidates.size());
			candidates.push_back(candidate);
		};
		const auto AddTrustedCandidate = [this, &AddUniqueCandidate, bResolveMetadata](const FileCandidateType& fileInfo) {
			FuzzyCandidateType candidate;
			SetDownloadValidatorFileInfo(candidate, fileInfo.ucHash, fileInfo.strName, fileInfo.uSize, fileInfo.uMediaLengthSec, fileInfo.eMediaLengthSource);
			uint8 uSourceFlags = FuzzyFileSourceUnknown;
			if (bResolveMetadata || uSourceFlags == FuzzyFileSourceUnknown) {
				uint32 uMediaLengthSec = 0;
				EFuzzyMediaLengthSource eMediaLengthSource = FuzzyMediaLengthUnknown;
				ResolveFileMetadata(fileInfo.ucHash, fileInfo.uSize, uSourceFlags, uMediaLengthSec, eMediaLengthSource);
				MergeDownloadValidatorFileMetadata(candidate, uMediaLengthSec, eMediaLengthSource);
			}
			candidate.strNormalizedName = BuildFuzzyNormalizedName(fileInfo.strName);
			candidate.strBoundaryName = BuildFuzzyBoundaryName(fileInfo.strName);
			candidate.uSimilarityScore = DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX;
			candidate.uCoverageScore = DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX;
			candidate.uWeightedJaccardScore = DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX;
			candidate.uEditSimilarityScore = DOWNLOAD_VALIDATOR_FUZZY_SCORE_MAX;
			candidate.uFileType = static_cast<uint8>(GetED2KFileTypeID(fileInfo.strName));
			candidate.uSourceFlags = uSourceFlags;
			candidate.bExactSubstring = true;
			AddUniqueCandidate(candidate);
		};
		const auto AddTrustedFileInfo = [&AddTrustedCandidate](const FileInfoType& fileInfo) {
			AddTrustedCandidate(fileInfo);
			for (std::vector<FileCandidateType>::const_iterator it = fileInfo.alternateFiles.begin(); it != fileInfo.alternateFiles.end(); ++it)
				AddTrustedCandidate(*it);
		};

		if (bIncludeTrusted) {
				if (hash != NULL && thePrefs.GetDownloadValidatorRejectSameHash() && theApp.knownfiles != NULL) {
				CKnownFile* pKnownFile = theApp.knownfiles->FindKnownFileByID(hash);
				if (pKnownFile != NULL && pKnownFile->GetFileSize() == filesize && !pKnownFile->IsPartFile()) {
					FileCandidateType sameHashCandidate;
					SetDownloadValidatorFileInfo(sameHashCandidate, pKnownFile->GetFileHash(), pKnownFile->GetFileName(), pKnownFile->GetFileSize(),
						pKnownFile->GetIntTagValue(FT_MEDIA_LENGTH), FuzzyMediaLengthLocalMediaInfo);
					AddTrustedCandidate(sameHashCandidate);
				}
			}

			const CString strProcessedFileName(BuildMapKey(filename));
			const DownloadValidatorFileMap::CPair* pPair = m_DownloadValidatorMap.PLookup(strProcessedFileName);
			if (pPair != NULL)
				AddTrustedFileInfo(pPair->value);

			if (thePrefs.GetDownloadValidatorDateTimeMatching()) {
				const CString strDateTimeMapKey(BuildDateTimeMapKey(filename, strProcessedFileName));
				if (!strDateTimeMapKey.IsEmpty()) {
					pPair = m_DownloadValidatorDateTimeMap.PLookup(strDateTimeMapKey);
					if (pPair != NULL)
						AddTrustedFileInfo(pPair->value);
				}
			}

			if (thePrefs.GetDownloadValidatorRegexMatching()) {
				std::vector<const FileInfoType*> regexFileInfos;
				const FileInfoType* pFirstRegexFileInfo = NULL;
				bool bRegexMatchAmbiguous = false;
				for (size_t uRuleIndex = 0; uRuleIndex < m_regexRules.size(); ++uRuleIndex) {
					CString strMapKey;
					if (!BuildRegexMapKey(uRuleIndex, filename, strMapKey))
						continue;
					pPair = m_DownloadValidatorRegexMap.PLookup(strMapKey);
					if (pPair == NULL)
						continue;
					if (pFirstRegexFileInfo != NULL && !IsDownloadValidatorFileMatch(*pFirstRegexFileInfo, pPair->value.ucHash, pPair->value.uSize)) {
						bRegexMatchAmbiguous = true;
						break;
					}
					pFirstRegexFileInfo = &pPair->value;
					regexFileInfos.push_back(&pPair->value);
				}
				if (!bRegexMatchAmbiguous) {
					for (std::vector<const FileInfoType*>::const_iterator it = regexFileInfos.begin(); it != regexFileInfos.end(); ++it)
						AddTrustedFileInfo(**it);
				}
			}

		}
		if (uMaximumCandidates != 0 && candidates.size() >= uMaximumCandidates) {
			candidates.resize(uMaximumCandidates);
			return true;
		}

		const bool bCanUseFuzzyDisplayIndex = IsFuzzyIndexReady();
		if (bFuzzyQueryPrepared && bCanUseFuzzyDisplayIndex) {
			std::vector<FuzzyCandidateType> fuzzyCandidates;
			const std::vector<FuzzyCandidateType>* pFuzzyCandidates = pPreparedFuzzyCandidates;
			if (pFuzzyCandidates != NULL || FindFuzzyCandidates(filename, fuzzyCandidates, 0, false, pPreparedQueryData)) {
				if (pFuzzyCandidates == NULL)
					pFuzzyCandidates = &fuzzyCandidates;
				const uint32 uDisplayThreshold = thePrefs.GetDownloadValidatorFuzzyDisplayThresholdPercent();
				for (std::vector<FuzzyCandidateType>::const_iterator it = pFuzzyCandidates->begin(); it != pFuzzyCandidates->end(); ++it) {
					if (it->uSimilarityScore < uDisplayThreshold)
						continue;
					AddUniqueCandidate(*it);
					if (uMaximumCandidates != 0 && candidates.size() >= uMaximumCandidates)
						break;
				}
			}
		}

		std::sort(candidates.begin(), candidates.end(), [](const FuzzyCandidateType& first, const FuzzyCandidateType& second) {
			if (first.uSimilarityScore != second.uSimilarityScore)
				return first.uSimilarityScore > second.uSimilarityScore;
			if (first.bStructuralIdentityMatch != second.bStructuralIdentityMatch)
				return first.bStructuralIdentityMatch;
			if (first.bExactSubstring != second.bExactSubstring)
				return first.bExactSubstring;
			if (first.uSharedGramCount != second.uSharedGramCount)
				return first.uSharedGramCount > second.uSharedGramCount;
			if (first.uSize != second.uSize)
				return first.uSize > second.uSize;
			const int iNameCompare = first.strName.CompareNoCase(second.strName);
			return iNameCompare != 0 ? iNameCompare < 0 : memcmp(first.ucHash, second.ucHash, MDX_DIGEST_SIZE) < 0;
		});
		if (uMaximumCandidates != 0 && candidates.size() > uMaximumCandidates)
			candidates.resize(uMaximumCandidates);
		return !candidates.empty();
	} catch (CMemoryException* ex) {
		ex->Delete();
	} catch (const std::exception&) {
	}

	candidates.clear();
	return false;
}

bool CDownloadValidator::IsMediaLengthCandidateAllowed(uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource, const FileCandidateType& candidate) const
{
	if (!thePrefs.GetDownloadValidatorMediaLengthMatching())
		return true;
	if (uMediaLengthSec == 0 || eMediaLengthSource == FuzzyMediaLengthUnknown)
		return true;

	uint32 uCandidateMediaLengthSec = candidate.uMediaLengthSec;
	bool bCandidateMediaLengthComparable = candidate.eMediaLengthSource == FuzzyMediaLengthLocalMediaInfo && uCandidateMediaLengthSec != 0;
	if (!bCandidateMediaLengthComparable && theApp.knownfiles != NULL) {
		CKnownFile* pKnownFile = theApp.knownfiles->FindKnownFileByID(candidate.ucHash);
		if (pKnownFile != NULL && !pKnownFile->IsPartFile() && pKnownFile->GetFileSize() == candidate.uSize) {
			uCandidateMediaLengthSec = pKnownFile->GetIntTagValue(FT_MEDIA_LENGTH);
			bCandidateMediaLengthComparable = uCandidateMediaLengthSec != 0;
		}
	}
	if (!bCandidateMediaLengthComparable && theApp.downloadqueue != NULL) {
		CPartFile* pPartFile = theApp.downloadqueue->GetFileByID(candidate.ucHash);
		if (pPartFile != NULL && pPartFile->GetFileSize() == candidate.uSize) {
			uCandidateMediaLengthSec = pPartFile->GetIntTagValue(FT_MEDIA_LENGTH);
			bCandidateMediaLengthComparable = uCandidateMediaLengthSec != 0;
		}
	}
	if (!bCandidateMediaLengthComparable)
		return true;

	const uint32 uDifference = uMediaLengthSec > uCandidateMediaLengthSec ? uMediaLengthSec - uCandidateMediaLengthSec : uCandidateMediaLengthSec - uMediaLengthSec;
	return uDifference <= thePrefs.GetDownloadValidatorMediaLengthToleranceSec();
}

bool CDownloadValidator::IsFileSizeCandidateAllowed(EMFileSize filesize, const FileCandidateType& candidate) const
{
	if (thePrefs.GetDownloadValidator() != 3)
		return true;
	return static_cast<double>(filesize) < static_cast<double>(candidate.uSize) * 0.01 * (100 + thePrefs.GetDownloadValidatorAcceptPercentage());
}

const CDownloadValidator::FileCandidateType* CDownloadValidator::FindEligibleFileCandidate(const FileInfoType& fileInfo, EMFileSize filesize, uint32 uMediaLengthSec,
	EFuzzyMediaLengthSource eMediaLengthSource) const
{
	const FileCandidateType* pBestCandidate = NULL;
	if (IsMediaLengthCandidateAllowed(uMediaLengthSec, eMediaLengthSource, fileInfo) && IsFileSizeCandidateAllowed(filesize, fileInfo))
		pBestCandidate = &fileInfo;

	for (std::vector<FileCandidateType>::const_iterator it = fileInfo.alternateFiles.begin(); it != fileInfo.alternateFiles.end(); ++it) {
		if (!IsMediaLengthCandidateAllowed(uMediaLengthSec, eMediaLengthSource, *it) || !IsFileSizeCandidateAllowed(filesize, *it))
			continue;
		if (pBestCandidate == NULL || it->uSize > pBestCandidate->uSize)
			pBestCandidate = &*it;
	}
	return pBestCandidate;
}

const CDownloadValidator::FuzzyCandidateType* CDownloadValidator::FindFuzzyDecisionCandidate(const uchar* hash, EMFileSize filesize, uint32 uMediaLengthSec,
	EFuzzyMediaLengthSource eMediaLengthSource, const std::vector<FuzzyCandidateType>& candidates) const
{
	const uint32 uThreshold = thePrefs.GetDownloadValidatorFuzzySimilarityThreshold();
	const FuzzyCandidateType* pBestCandidate = NULL;
	const FuzzyCandidateType* pCompetingCandidate = NULL;
	for (std::vector<FuzzyCandidateType>::const_iterator it = candidates.begin(); it != candidates.end(); ++it) {
		if (it->uSimilarityScore < uThreshold)
			break;
		if (it->bFileTypeConflict || (hash != NULL && IsDownloadValidatorFileMatch(*it, hash, filesize)))
			continue;
		if (!IsMediaLengthCandidateAllowed(uMediaLengthSec, eMediaLengthSource, *it) || !IsFileSizeCandidateAllowed(filesize, *it))
			continue;

		if (pBestCandidate == NULL) {
			pBestCandidate = &*it;
			continue;
		}
		const bool bSameLogicalIdentity = it->strNormalizedName == pBestCandidate->strNormalizedName
			|| (it->bStructuralIdentityMatch && pBestCandidate->bStructuralIdentityMatch && it->uStructuralIdentityKey != 0
			&& it->uStructuralIdentityKey == pBestCandidate->uStructuralIdentityKey);
		if (!bSameLogicalIdentity) {
			pCompetingCandidate = &*it;
			break;
		}
	}

	if (pBestCandidate != NULL && pCompetingCandidate != NULL && pBestCandidate->uSimilarityScore - pCompetingCandidate->uSimilarityScore <= DOWNLOAD_VALIDATOR_FUZZY_AMBIGUITY_MARGIN)
		return NULL;
	return pBestCandidate;
}

CString CDownloadValidator::BuildMapKey(const CString& filename) const
{
	CString strProcessedFileName(filename);

	if (thePrefs.GetDownloadValidatorIgnoreExtension())
		strProcessedFileName = RemoveFileExtension(strProcessedFileName);

	if (thePrefs.GetDownloadValidatorIgnoreTags())
		strProcessedFileName = RemoveTags(strProcessedFileName, thePrefs.GetDownloadValidatorDontIgnoreNumericTags());

	if (thePrefs.GetDownloadValidatorCleanMojibake())
		strProcessedFileName = RemoveMojibakeGarbage(strProcessedFileName);

	if (thePrefs.GetDownloadValidatorIgnoreNonAlphaNumeric())
		strProcessedFileName = RemoveNonAlphaNumeric(strProcessedFileName);

	if (thePrefs.GetDownloadValidatorCaseInsensitive())
		strProcessedFileName.MakeLower();

	return strProcessedFileName;
}

CString CDownloadValidator::BuildDateTimeProcessedFileName(const CString& filename) const
{
	CString strProcessedFileName(filename);

	if (thePrefs.GetDownloadValidatorIgnoreExtension())
		strProcessedFileName = RemoveFileExtension(strProcessedFileName);

	if (thePrefs.GetDownloadValidatorIgnoreTags())
		strProcessedFileName = RemoveTags(strProcessedFileName, thePrefs.GetDownloadValidatorDontIgnoreNumericTags());

	if (thePrefs.GetDownloadValidatorCleanMojibake())
		strProcessedFileName = RemoveMojibakeGarbage(strProcessedFileName);

	if (thePrefs.GetDownloadValidatorCaseInsensitive())
		strProcessedFileName.MakeLower();

	return strProcessedFileName;
}

CString CDownloadValidator::BuildDateTimeMapKey(const CString& filename, const CString& strProcessedFileName) const
{
	if (!thePrefs.GetDownloadValidatorDateTimeMatching() || !ContainsDownloadValidatorDigit(filename))
		return EMPTY;

	CString strDateTimeProcessedFileName;
	const CString* pDateTimeProcessedFileName = &strProcessedFileName;
	if (thePrefs.GetDownloadValidatorDateTimeIncludeFollowingNumericValues() && thePrefs.GetDownloadValidatorIgnoreNonAlphaNumeric()) {
		strDateTimeProcessedFileName = BuildDateTimeProcessedFileName(filename);
		pDateTimeProcessedFileName = &strDateTimeProcessedFileName;
	}

	SDownloadValidatorDateTimeOptions options;
	GetDownloadValidatorDateTimeOptions(options);

	SDownloadValidatorDateTimeMatch match;
	if (!TryBuildDownloadValidatorDateTimeMatch(options, *pDateTimeProcessedFileName, match))
		return EMPTY;

	return match.strCompareText;
}

bool CDownloadValidator::ReadRegexRulesText(CString& strRulesText, SRegexRulesResult& result) const
{
	result = SRegexRulesResult();
	strRulesText.Empty();

	const CString strFullPath(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + DOWNLOADVALIDATORFILE);
	const CString strOpenPath(PreparePathForWin32LongPath(strFullPath));
	const DWORD dwAttributes = ::GetFileAttributes(strOpenPath);
	if (dwAttributes == INVALID_FILE_ATTRIBUTES) {
		const DWORD dwError = ::GetLastError();
		if (dwError == ERROR_FILE_NOT_FOUND || dwError == ERROR_PATH_NOT_FOUND)
			return true;
		result.eError = RegexRulesFileReadError;
		return false;
	}

	CStdioFile regexFile;
	const bool bUnicodeFile = IsUnicodeFile(strOpenPath);
	if (!regexFile.Open(strOpenPath, CFile::modeRead | CFile::shareDenyWrite | (bUnicodeFile ? CFile::typeBinary : 0))) {
		result.eError = RegexRulesFileReadError;
		return false;
	}

	try {
		const ULONGLONG uMaximumFileSize = static_cast<ULONGLONG>(RegexRulesTextLimit) * sizeof(TCHAR) + sizeof(WORD);
		if (regexFile.GetLength() > uMaximumFileSize) {
			regexFile.Abort();
			result.eError = RegexRulesTextTooLarge;
			return false;
		}
		if (bUnicodeFile)
			regexFile.Seek(sizeof(WORD), CFile::begin);

		CString strLine;
		bool bFirstLine = true;
		while (regexFile.ReadString(strLine)) {
			strLine.TrimRight(_T("\r\n"));
			if (!bFirstLine)
				strRulesText += _T("\r\n");
			strRulesText += strLine;
			bFirstLine = false;
			if (strRulesText.GetLength() > RegexRulesTextLimit) {
				regexFile.Abort();
				strRulesText.Empty();
				result.eError = RegexRulesTextTooLarge;
				return false;
			}
		}
	} catch (CFileException* ex) {
		ex->Delete();
		regexFile.Abort();
		strRulesText.Empty();
		result.eError = RegexRulesFileReadError;
		return false;
	}
	regexFile.Abort();
	return true;
}

bool CDownloadValidator::CompileRegexRulesText(const CString& strRulesText, bool bCaseInsensitive, std::vector<SRegexRule>& loadedRules, SRegexRulesResult& result) const
{
	result = SRegexRulesResult();
	loadedRules.clear();
	if (strRulesText.GetLength() > RegexRulesTextLimit) {
		result.eError = RegexRulesTextTooLarge;
		return false;
	}

	int iLineStart = 0;
	UINT uLineNumber = 1;
	for (;;) {
		const int iLineEnd = strRulesText.Find(_T('\n'), iLineStart);
		CString strLine = iLineEnd >= 0 ? strRulesText.Mid(iLineStart, iLineEnd - iLineStart) : strRulesText.Mid(iLineStart);
		if (!strLine.IsEmpty() && strLine[strLine.GetLength() - 1] == _T('\r'))
			strLine.Truncate(strLine.GetLength() - 1);

		CString strTrimmedLine(strLine);
		strTrimmedLine.Trim();
		if (!strTrimmedLine.IsEmpty() && strTrimmedLine[0] != _T('#')) {
			SRegexRule rule;
			rule.bCaseInsensitive = bCaseInsensitive;
			rule.bWholeNameMatch = IsWholeNameRegexPattern(strLine);
			try {
				std::regex_constants::syntax_option_type flags = std::regex_constants::ECMAScript | std::regex_constants::optimize;
				if (rule.bCaseInsensitive)
					flags |= std::regex_constants::icase;
				rule.regex = std::basic_regex<TCHAR>((LPCTSTR)strLine, flags);
			} catch (const std::regex_error&) {
				result.eError = RegexRulesInvalidExpression;
				result.uLineNumber = uLineNumber;
				return false;
			}

			if (rule.regex.mark_count() == 0) {
				result.eError = RegexRulesMissingCaptureGroup;
				result.uLineNumber = uLineNumber;
				return false;
			}
			loadedRules.push_back(rule);
		}

		if (iLineEnd < 0)
			break;
		iLineStart = iLineEnd + 1;
		++uLineNumber;
	}

	result.uRuleCount = static_cast<UINT>(loadedRules.size());
	return true;
}

bool CDownloadValidator::ValidateRegexRulesText(const CString& strRulesText, bool bCaseInsensitive, SRegexRulesResult& result) const
{
	std::vector<SRegexRule> loadedRules;
	return CompileRegexRulesText(strRulesText, bCaseInsensitive, loadedRules, result);
}

bool CDownloadValidator::WriteRegexRulesText(const CString& strRulesText, SRegexRulesResult& result) const
{
	const CString strFullPath(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + DOWNLOADVALIDATORFILE);
	const CString strTempPath(strFullPath + _T(".tmp"));
	const CString strOpenPath(PreparePathForWin32LongPath(strFullPath));
	const CString strOpenTempPath(PreparePathForWin32LongPath(strTempPath));
	::DeleteFile(strOpenTempPath);

	CFile regexFile;
	CFileException fileException;
	if (!regexFile.Open(strOpenTempPath, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive | CFile::typeBinary | CFile::osSequentialScan, &fileException)) {
		result.eError = RegexRulesFileWriteError;
		return false;
	}

	try {
#ifdef _UNICODE
		const WORD uBom = 0xFEFF;
		regexFile.Write(&uBom, sizeof(uBom));
		if (!strRulesText.IsEmpty())
			regexFile.Write((LPCTSTR)strRulesText, static_cast<UINT>(strRulesText.GetLength() * sizeof(TCHAR)));
#else
		if (!strRulesText.IsEmpty())
			regexFile.Write((LPCTSTR)strRulesText, static_cast<UINT>(strRulesText.GetLength()));
#endif
		regexFile.Flush();
		regexFile.Close();
	} catch (CFileException* ex) {
		ex->Delete();
		regexFile.Abort();
		::DeleteFile(strOpenTempPath);
		result.eError = RegexRulesFileWriteError;
		return false;
	}

	BOOL bReplaced = FALSE;
	if (::GetFileAttributes(strOpenPath) != INVALID_FILE_ATTRIBUTES)
		bReplaced = ::ReplaceFile(strOpenPath, strOpenTempPath, NULL, REPLACEFILE_WRITE_THROUGH, NULL, NULL);
	if (!bReplaced)
		bReplaced = ::MoveFileEx(strOpenTempPath, strOpenPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
	if (!bReplaced) {
		::DeleteFile(strOpenTempPath);
		result.eError = RegexRulesFileWriteError;
		return false;
	}
	return true;
}

bool CDownloadValidator::ApplyRegexRulesText(const CString& strRulesText, bool bCaseInsensitive, SRegexRulesResult& result)
{
	CSingleLock indexLock(&m_indexLock, TRUE);
	std::vector<SRegexRule> loadedRules;
	if (!CompileRegexRulesText(strRulesText, bCaseInsensitive, loadedRules, result))
		return false;
	if (!WriteRegexRulesText(strRulesText, result))
		return false;

	m_regexRules.swap(loadedRules);
	AddDebugLogLine(DLP_LOW, false, _T("Download Validator loaded %u regex rules from %s."), static_cast<UINT>(m_regexRules.size()), (LPCTSTR)DOWNLOADVALIDATORFILE);
	return true;
}

bool CDownloadValidator::ReloadRegexRules(CString& strRulesText, SRegexRulesResult& result)
{
	CSingleLock indexLock(&m_indexLock, TRUE);
	if (!ReadRegexRulesText(strRulesText, result))
		return false;

	std::vector<SRegexRule> loadedRules;
	if (!CompileRegexRulesText(strRulesText, thePrefs.GetDownloadValidatorCaseInsensitive(), loadedRules, result))
		return false;

	m_regexRules.swap(loadedRules);
	const CString strFullPath(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + DOWNLOADVALIDATORFILE);
	if (::GetFileAttributes(PreparePathForWin32LongPath(strFullPath)) != INVALID_FILE_ATTRIBUTES || !m_regexRules.empty())
		AddDebugLogLine(DLP_LOW, false, _T("Download Validator loaded %u regex rules from %s."), static_cast<UINT>(m_regexRules.size()), (LPCTSTR)DOWNLOADVALIDATORFILE);
	return true;
}

bool CDownloadValidator::ReloadRegexRules()
{
	CString strRulesText;
	SRegexRulesResult result;
	if (ReloadRegexRules(strRulesText, result))
		return true;

	if (result.eError == RegexRulesInvalidExpression)
		AddDebugLogLine(DLP_HIGH, false, _T("Download Validator regex rule on line %u is invalid."), result.uLineNumber);
	else if (result.eError == RegexRulesMissingCaptureGroup)
		AddDebugLogLine(DLP_HIGH, false, _T("Download Validator regex rule on line %u has no capture group."), result.uLineNumber);
	else if (result.eError == RegexRulesTextTooLarge)
		AddDebugLogLine(DLP_HIGH, false, _T("Download Validator regex rules exceed the supported text size."));
	else
		AddDebugLogLine(DLP_HIGH, false, _T("Download Validator could not read %s."), (LPCTSTR)EscPercent(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + DOWNLOADVALIDATORFILE));
	return false;
}

bool CDownloadValidator::BuildRegexMapKey(size_t uRuleIndex, const CString& filename, CString& strMapKey) const
{
	strMapKey.Empty();
	if (uRuleIndex >= m_regexRules.size())
		return false;

	CString strTargetFilename(filename);
	if (thePrefs.GetDownloadValidatorCleanMojibake())
		strTargetFilename = RemoveMojibakeGarbage(strTargetFilename);

	try {
		const TCHAR* pszBegin = strTargetFilename;
		const TCHAR* pszEnd = pszBegin + strTargetFilename.GetLength();
		std::match_results<const TCHAR*> match;
		if (m_regexRules[uRuleIndex].bWholeNameMatch) {
			if (!std::regex_match(pszBegin, pszEnd, match, m_regexRules[uRuleIndex].regex))
				return false;
		} else {
			typedef std::regex_iterator<const TCHAR*> TRegexIterator;
			TRegexIterator itMatch(pszBegin, pszEnd, m_regexRules[uRuleIndex].regex);
			const TRegexIterator itEnd;
			if (itMatch == itEnd)
				return false;
			match = *itMatch;
			++itMatch;
			if (itMatch != itEnd)
				return false;
		}

		strMapKey.Format(_T("%u:"), static_cast<UINT>(uRuleIndex));
		for (size_t uCapture = 1; uCapture < match.size(); ++uCapture) {
			if (!match[uCapture].matched || match[uCapture].length() == 0) {
				strMapKey.Empty();
				return false;
			}

			CString strCapture(match[uCapture].first, static_cast<int>(match[uCapture].length()));
			if (m_regexRules[uRuleIndex].bCaseInsensitive)
				strCapture.MakeLower();
			CString strLength;
			strLength.Format(_T("%u:"), static_cast<UINT>(strCapture.GetLength()));
			strMapKey += strLength;
			strMapKey += strCapture;
		}
	} catch (const std::regex_error&) {
		strMapKey.Empty();
		return false;
	}

	return !strMapKey.IsEmpty();
}

void CDownloadValidator::AddRegexMatchesToMap(const uchar* hash, const CString& filename, const EMFileSize filesize, uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource)
{
	if (!thePrefs.GetDownloadValidatorRegexMatching())
		return;

	for (size_t uRuleIndex = 0; uRuleIndex < m_regexRules.size(); ++uRuleIndex) {
		CString strMapKey;
		if (BuildRegexMapKey(uRuleIndex, filename, strMapKey))
			AddPreparedToMap(m_DownloadValidatorRegexMap, strMapKey, hash, filename, filesize, uMediaLengthSec, eMediaLengthSource);
	}
}

void CDownloadValidator::RemoveRegexMatchesFromMap(const uchar* hash, const CString& filename, const EMFileSize filesize)
{
	if (!thePrefs.GetDownloadValidatorRegexMatching())
		return;

	for (size_t uRuleIndex = 0; uRuleIndex < m_regexRules.size(); ++uRuleIndex) {
		CString strMapKey;
		if (BuildRegexMapKey(uRuleIndex, filename, strMapKey))
			RemovePreparedFromMap(m_DownloadValidatorRegexMap, strMapKey, hash, filesize);
	}
}

const CDownloadValidator::FileInfoType* CDownloadValidator::FindRegexFileInfo(const CString& filename) const
{
	if (!thePrefs.GetDownloadValidatorRegexMatching())
		return NULL;

	const FileInfoType* pMatchedFileInfo = NULL;
	for (size_t uRuleIndex = 0; uRuleIndex < m_regexRules.size(); ++uRuleIndex) {
		CString strMapKey;
		if (!BuildRegexMapKey(uRuleIndex, filename, strMapKey))
			continue;

		const DownloadValidatorFileMap::CPair* pPair = m_DownloadValidatorRegexMap.PLookup(strMapKey);
		if (pPair == NULL)
			continue;
		if (pMatchedFileInfo != NULL && !IsDownloadValidatorFileMatch(*pMatchedFileInfo, pPair->value.ucHash, pPair->value.uSize))
			return NULL;
		pMatchedFileInfo = &pPair->value;
	}
	return pMatchedFileInfo;
}

void CDownloadValidator::AddPreparedToMap(DownloadValidatorFileMap& map, const CString& strProcessedFileName, const uchar* hash, const CString& filename, const EMFileSize filesize,
	uint32 uMediaLengthSec, EFuzzyMediaLengthSource eMediaLengthSource)
{
	if (strProcessedFileName.IsEmpty() || hash == NULL)
		return;

	DownloadValidatorFileMap::CPair* pPair = map.PLookup(strProcessedFileName);
	if (pPair == NULL) {
		FileInfoType fileInfo;
		SetDownloadValidatorFileInfo(fileInfo, hash, filename, filesize, uMediaLengthSec, eMediaLengthSource);
		map[strProcessedFileName] = fileInfo;
		return;
	}

	FileInfoType& fileInfo = pPair->value;
	if (IsDownloadValidatorFileMatch(fileInfo, hash, filesize)) {
		MergeDownloadValidatorFileMetadata(fileInfo, uMediaLengthSec, eMediaLengthSource);
		return;
	}
	for (std::vector<FileCandidateType>::iterator it = fileInfo.alternateFiles.begin(); it != fileInfo.alternateFiles.end(); ++it) {
		if (IsDownloadValidatorFileMatch(*it, hash, filesize)) {
			MergeDownloadValidatorFileMetadata(*it, uMediaLengthSec, eMediaLengthSource);
			return;
		}
	}

	FileCandidateType newFileInfo;
	SetDownloadValidatorFileInfo(newFileInfo, hash, filename, filesize, uMediaLengthSec, eMediaLengthSource);
	if (fileInfo.uSize >= filesize) {
		fileInfo.alternateFiles.push_back(newFileInfo);
		return;
	}

	FileCandidateType previousFileInfo(static_cast<const FileCandidateType&>(fileInfo));
	fileInfo.alternateFiles.push_back(previousFileInfo);
	static_cast<FileCandidateType&>(fileInfo) = newFileInfo;
}

void CDownloadValidator::RemovePreparedFromMap(DownloadValidatorFileMap& map, const CString& strProcessedFileName, const uchar* hash, const EMFileSize filesize)
{
	if (strProcessedFileName.IsEmpty() || hash == NULL)
		return;

	DownloadValidatorFileMap::CPair* pPair = map.PLookup(strProcessedFileName);
	if (pPair == NULL)
		return;

	FileInfoType& fileInfo = pPair->value;
	if (IsDownloadValidatorFileMatch(fileInfo, hash, filesize)) {
		if (fileInfo.alternateFiles.empty()) {
			map.RemoveKey(strProcessedFileName);
			return;
		}

		std::vector<FileCandidateType>::iterator itReplacement = fileInfo.alternateFiles.begin();
		for (std::vector<FileCandidateType>::iterator it = itReplacement + 1; it != fileInfo.alternateFiles.end(); ++it) {
			if (it->uSize > itReplacement->uSize)
				itReplacement = it;
		}
		static_cast<FileCandidateType&>(fileInfo) = *itReplacement;
		fileInfo.alternateFiles.erase(itReplacement);
		return;
	}

	for (std::vector<FileCandidateType>::iterator it = fileInfo.alternateFiles.begin(); it != fileInfo.alternateFiles.end(); ++it) {
		if (IsDownloadValidatorFileMatch(*it, hash, filesize)) {
			fileInfo.alternateFiles.erase(it);
			return;
		}
	}
}

void CDownloadValidator::ReloadMap()
{
	CSingleLock indexLock(&m_indexLock, TRUE);
	CancelDeferredBackgroundWork();
	if (theApp.IsClosing())
		return;

	m_bStartupKnownFilesMapLoadActive = false;

	delete m_pReloadMapState;
	m_pReloadMapState = NULL;
	::InterlockedExchange(&m_lRegexReloadActive, 0);
	PrepareReloadMapStorage(false);
	const bool bBuildFuzzyIndex = m_bFuzzyIndexAvailable;
	const bool bCollectMediaLength = bBuildFuzzyIndex || thePrefs.GetDownloadValidatorMediaLengthMatching();

	// Downloading files
	if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL && theApp.emuledlg->transferwnd->GetDownloadList() != NULL) {
		CDownloadListCtrl* pDownloadList = theApp.emuledlg->transferwnd->GetDownloadList();
		for (CDownloadListCtrl::ListItems::const_iterator it = pDownloadList->m_ListItems.begin(); it != pDownloadList->m_ListItems.end(); ++it) {
			const CtrlItem_Struct* cur_item = it->second;
			if (cur_item == NULL || cur_item->type != FILE_TYPE)
				continue;

			CPartFile* pFile = static_cast<CPartFile*>(cur_item->value);
			if (pFile != NULL) {
				const uint32 uMediaLengthSec = bCollectMediaLength ? pFile->GetIntTagValue(FT_MEDIA_LENGTH) : 0;
				AddToMapInternal(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize(), bBuildFuzzyIndex ? FuzzyFileSourceDownloading : FuzzyFileSourceUnknown, uMediaLengthSec,
					uMediaLengthSec != 0 ? FuzzyMediaLengthRemoteMetadata : FuzzyMediaLengthUnknown);
			}
		}
	}

	// Known files
	if (theApp.knownfiles != NULL) {
		for (const CKnownFilesMap::CPair* pair = theApp.knownfiles->m_Files_map.PGetFirstAssoc(); pair != NULL; pair = theApp.knownfiles->m_Files_map.PGetNextAssoc(pair)) {
			CKnownFile* pFile = pair->value;
			if (pFile != NULL) {
				const uint32 uMediaLengthSec = bCollectMediaLength ? pFile->GetIntTagValue(FT_MEDIA_LENGTH) : 0;
				AddToMapInternal(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize(), bBuildFuzzyIndex ? FuzzyFileSourceKnown : FuzzyFileSourceUnknown, uMediaLengthSec,
					bCollectMediaLength ? FuzzyMediaLengthLocalMediaInfo : FuzzyMediaLengthUnknown);
			}
		}

		// Duplicate files
		CSingleLock slDuplicatesLock(&theApp.knownfiles->m_csDuplicatesLock, TRUE);
		for (CKnownFileList::KnownFileList::const_iterator it = theApp.knownfiles->m_duplicateFileList.begin(); it != theApp.knownfiles->m_duplicateFileList.end(); ++it) {
			CKnownFile* pFile = *it;
			if (pFile != NULL) {
				const uint32 uMediaLengthSec = bCollectMediaLength ? pFile->GetIntTagValue(FT_MEDIA_LENGTH) : 0;
				AddToMapInternal(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize(), bBuildFuzzyIndex ? FuzzyFileSourceKnown : FuzzyFileSourceUnknown, uMediaLengthSec,
					bCollectMediaLength ? FuzzyMediaLengthLocalMediaInfo : FuzzyMediaLengthUnknown);
			}
		}
	}

	if (m_bFuzzyIndexAvailable) {
		SReloadMapState state(false);
		state.ePhase = SReloadMapState::PhaseFuzzyPrepare;
		UINT uProcessed = 0;
		while (state.ePhase != SReloadMapState::PhaseDone)
			ProcessFuzzyPostingPreparation(state, ::GetTickCount(), 0, _UI32_MAX, uProcessed, true);
	}
	TouchPossibleKnownRevision();
	if (theApp.searchlist != NULL)
		theApp.searchlist->RequestDownloadValidatorRecheckForAllSearches();
}

void CDownloadValidator::QueueReloadMap()
{
	CSingleLock indexLock(&m_indexLock, TRUE);
	CancelDeferredBackgroundWork();
	if (theApp.IsClosing())
		return;

	m_bStartupKnownFilesMapLoadActive = false;

	delete m_pReloadMapState;
	m_pReloadMapState = NULL;
	::InterlockedExchange(&m_lRegexReloadActive, 0);
	PrepareReloadMapStorage(false);
	m_pReloadMapState = new SReloadMapState(false);
	TouchPossibleKnownRevision();
	QueueDownloadValidatorSearchResultsRefresh();
}

void CDownloadValidator::QueueReloadRegexMap()
{
	CSingleLock indexLock(&m_indexLock, TRUE);
	if (theApp.IsClosing())
		return;

	m_bStartupKnownFilesMapLoadActive = false;

	const bool bRegexOnly = IsMapInitialized() && (m_pReloadMapState == NULL || m_pReloadMapState->bRegexOnly);
	delete m_pReloadMapState;
	m_pReloadMapState = NULL;
	::InterlockedExchange(&m_lRegexReloadActive, bRegexOnly ? 1 : 0);
	PrepareReloadMapStorage(bRegexOnly);
	if (bRegexOnly && (!thePrefs.GetDownloadValidatorRegexMatching() || m_regexRules.empty())) {
		::InterlockedExchange(&m_lRegexReloadActive, 0);
		TouchPossibleKnownRevision(false);
		QueueDownloadValidatorSearchResultsRefresh();
		if (theApp.searchlist != NULL)
			theApp.searchlist->RequestDownloadValidatorRecheckForAllSearches();
		return;
	}
	m_pReloadMapState = new SReloadMapState(bRegexOnly);
}

void CDownloadValidator::CancelReloadMap()
{
	CSingleLock indexLock(&m_indexLock, TRUE);
	CancelDeferredBackgroundWork();
	m_bStartupKnownFilesMapLoadActive = false;
	delete m_pReloadMapState;
	m_pReloadMapState = NULL;
	::InterlockedExchange(&m_lRegexReloadActive, 0);
}

bool CDownloadValidator::ProcessReloadMapSlice(bool bDrainAll)
{
	CSingleLock indexLock(&m_indexLock, TRUE);
	if (m_pReloadMapState == NULL)
		return false;
	if (theApp.IsClosing()) {
		delete m_pReloadMapState;
		m_pReloadMapState = NULL;
		::InterlockedExchange(&m_lRegexReloadActive, 0);
		return false;
	}
	if (!bDrainAll && theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL && theApp.emuledlg->sharedfileswnd->sharedfilesctrl.IsDeleteLikeBulkOperationActive())
		return true;
	if (m_pReloadMapState->bTraversalRestartRequired) {
		QueueReloadMap();
		return true;
	}

	DWORD dwSliceBudgetMs = 12;
	UINT uMaxItemsPerSlice = 4096;
	if (!bDrainAll)
		GetDownloadValidatorReloadSliceLimits(dwSliceBudgetMs, uMaxItemsPerSlice);
	const DWORD dwSliceStart = ::GetTickCount();
	UINT uProcessed = 0;

	while (m_pReloadMapState != NULL && m_pReloadMapState->ePhase != SReloadMapState::PhaseDone) {
		if (m_pReloadMapState->ePhase == SReloadMapState::PhaseDownloads) {
			CDownloadListCtrl* pDownloadList = NULL;
			if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL)
				pDownloadList = theApp.emuledlg->transferwnd->GetDownloadList();
			if (pDownloadList == NULL) {
				m_pReloadMapState->ePhase = SReloadMapState::PhaseKnownFiles;
				continue;
			}
			CDownloadListCtrl::ListItems::const_iterator itDownload = pDownloadList->m_ListItems.begin();
			for (size_t uSkip = 0; itDownload != pDownloadList->m_ListItems.end() && uSkip < m_pReloadMapState->uDownloadIndex; ++uSkip)
				++itDownload;
			while (itDownload != pDownloadList->m_ListItems.end()) {
				const CtrlItem_Struct* cur_item = itDownload->second;
				++itDownload;
				++m_pReloadMapState->uDownloadIndex;
				if (cur_item != NULL && cur_item->type == FILE_TYPE) {
					CPartFile* pFile = static_cast<CPartFile*>(cur_item->value);
					if (pFile != NULL) {
						if (m_pReloadMapState->bRegexOnly) {
							const bool bCollectMediaLength = m_bFuzzyIndexAvailable || thePrefs.GetDownloadValidatorMediaLengthMatching();
							const uint32 uMediaLengthSec = bCollectMediaLength ? pFile->GetIntTagValue(FT_MEDIA_LENGTH) : 0;
							AddRegexMatchesToMap(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize(), uMediaLengthSec,
								uMediaLengthSec != 0 ? FuzzyMediaLengthRemoteMetadata : FuzzyMediaLengthUnknown);
						} else {
							const uint32 uMediaLengthSec = m_bFuzzyIndexAvailable || thePrefs.GetDownloadValidatorMediaLengthMatching() ? pFile->GetIntTagValue(FT_MEDIA_LENGTH) : 0;
							AddToMapInternal(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize(), m_bFuzzyIndexAvailable ? FuzzyFileSourceDownloading : FuzzyFileSourceUnknown, uMediaLengthSec,
								uMediaLengthSec != 0 ? FuzzyMediaLengthRemoteMetadata : FuzzyMediaLengthUnknown);
						}
					}
				}
				++uProcessed;
				if (!bDrainAll && (uProcessed >= uMaxItemsPerSlice || ((uProcessed & 0x1F) == 0 && static_cast<DWORD>(::GetTickCount() - dwSliceStart) >= dwSliceBudgetMs)))
					return true;
			}
			m_pReloadMapState->ePhase = SReloadMapState::PhaseKnownFiles;
			continue;
		}

		if (m_pReloadMapState->ePhase == SReloadMapState::PhaseKnownFiles) {
			if (theApp.knownfiles == NULL) {
				m_pReloadMapState->ePhase = SReloadMapState::PhaseDuplicateFiles;
				continue;
			}
			if (!m_pReloadMapState->bKnownIterationStarted) {
				m_pReloadMapState->pKnownPair = theApp.knownfiles->m_Files_map.PGetFirstAssoc();
				m_pReloadMapState->bKnownIterationStarted = true;
			}
			while (m_pReloadMapState->pKnownPair != NULL) {
				const CKnownFilesMap::CPair* pKnownPair = m_pReloadMapState->pKnownPair;
				m_pReloadMapState->pKnownPair = theApp.knownfiles->m_Files_map.PGetNextAssoc(pKnownPair);
				CKnownFile* pFile = pKnownPair->value;
				if (pFile != NULL) {
					const bool bCollectMediaLength = m_bFuzzyIndexAvailable || thePrefs.GetDownloadValidatorMediaLengthMatching();
					const uint32 uMediaLengthSec = bCollectMediaLength ? pFile->GetIntTagValue(FT_MEDIA_LENGTH) : 0;
					if (m_pReloadMapState->bRegexOnly) {
						AddRegexMatchesToMap(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize(), uMediaLengthSec,
							bCollectMediaLength ? FuzzyMediaLengthLocalMediaInfo : FuzzyMediaLengthUnknown);
					} else {
						AddToMapInternal(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize(), m_bFuzzyIndexAvailable ? FuzzyFileSourceKnown : FuzzyFileSourceUnknown, uMediaLengthSec,
							bCollectMediaLength ? FuzzyMediaLengthLocalMediaInfo : FuzzyMediaLengthUnknown);
					}
				}
				++uProcessed;
				if (!bDrainAll && (uProcessed >= uMaxItemsPerSlice || ((uProcessed & 0x1F) == 0 && static_cast<DWORD>(::GetTickCount() - dwSliceStart) >= dwSliceBudgetMs)))
					return true;
			}
			m_pReloadMapState->ePhase = SReloadMapState::PhaseDuplicateFiles;
			continue;
		}

		if (m_pReloadMapState->ePhase == SReloadMapState::PhaseDuplicateFiles) {
			if (theApp.knownfiles == NULL) {
				m_pReloadMapState->ePhase = !m_pReloadMapState->bRegexOnly && m_bFuzzyIndexAvailable ? SReloadMapState::PhaseFuzzyPrepare : SReloadMapState::PhaseDone;
				continue;
			}
			CSingleLock slDuplicatesLock(&theApp.knownfiles->m_csDuplicatesLock, TRUE);
			if (!m_pReloadMapState->bDuplicateIterationStarted) {
				m_pReloadMapState->itDuplicate = theApp.knownfiles->m_duplicateFileList.begin();
				m_pReloadMapState->bDuplicateIterationStarted = true;
			}
			while (m_pReloadMapState->itDuplicate != theApp.knownfiles->m_duplicateFileList.end()) {
				CKnownFile* pFile = *m_pReloadMapState->itDuplicate;
				++m_pReloadMapState->itDuplicate;
				if (pFile != NULL) {
					const bool bCollectMediaLength = m_bFuzzyIndexAvailable || thePrefs.GetDownloadValidatorMediaLengthMatching();
					const uint32 uMediaLengthSec = bCollectMediaLength ? pFile->GetIntTagValue(FT_MEDIA_LENGTH) : 0;
					if (m_pReloadMapState->bRegexOnly) {
						AddRegexMatchesToMap(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize(), uMediaLengthSec,
							bCollectMediaLength ? FuzzyMediaLengthLocalMediaInfo : FuzzyMediaLengthUnknown);
					} else {
						AddToMapInternal(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize(), m_bFuzzyIndexAvailable ? FuzzyFileSourceKnown : FuzzyFileSourceUnknown, uMediaLengthSec,
							bCollectMediaLength ? FuzzyMediaLengthLocalMediaInfo : FuzzyMediaLengthUnknown);
					}
				}
				++uProcessed;
				if (!bDrainAll && (uProcessed >= uMaxItemsPerSlice || ((uProcessed & 0x1F) == 0 && static_cast<DWORD>(::GetTickCount() - dwSliceStart) >= dwSliceBudgetMs)))
					return true;
			}
			m_pReloadMapState->ePhase = !m_pReloadMapState->bRegexOnly && m_bFuzzyIndexAvailable ? SReloadMapState::PhaseFuzzyPrepare : SReloadMapState::PhaseDone;
			continue;
		}

		if (m_pReloadMapState->ePhase == SReloadMapState::PhaseFuzzyPrepare) {
			if (ProcessFuzzyPostingPreparation(*m_pReloadMapState, dwSliceStart, dwSliceBudgetMs, uMaxItemsPerSlice, uProcessed, bDrainAll))
				return true;
			continue;
		}

	}

	const bool bFuzzyRestartRequired = m_pReloadMapState->bFuzzyRestartRequired;
	const bool bRegexOnly = m_pReloadMapState->bRegexOnly;
	std::vector<SReloadMapState::SPendingFuzzyRecord> pendingFuzzyRecords;
	if (!bFuzzyRestartRequired)
		pendingFuzzyRecords.swap(m_pReloadMapState->pendingFuzzyRecords);
	delete m_pReloadMapState;
	m_pReloadMapState = NULL;
	::InterlockedExchange(&m_lRegexReloadActive, 0);
	if (bFuzzyRestartRequired && m_bFuzzyIndexAvailable && !theApp.IsClosing()) {
		QueueReloadMap();
		return true;
	}
	for (std::vector<SReloadMapState::SPendingFuzzyRecord>::const_iterator it = pendingFuzzyRecords.begin(); it != pendingFuzzyRecords.end(); ++it)
		AddFuzzyRecord(it->fileInfo.ucHash, it->fileInfo.strName, it->fileInfo.uSize, it->uSourceFlags, it->fileInfo.uMediaLengthSec, it->fileInfo.eMediaLengthSource);
	TouchPossibleKnownRevision(!bRegexOnly);
	QueueDownloadValidatorSearchResultsRefresh();
	if (theApp.searchlist != NULL)
		theApp.searchlist->RequestDownloadValidatorRecheckForAllSearches();
	return uProcessed != 0;
}

void CDownloadValidator::AddToMap(const uchar* hash, const CString& filename, const EMFileSize filesize, bool bKnownCollectionChanged)
{
	AddToMapInternal(hash, filename, filesize, bKnownCollectionChanged);
}

void CDownloadValidator::AddDownloadingFileToMap(const uchar* hash, const CString& filename, const EMFileSize filesize)
{
	AddToMapInternal(hash, filename, filesize, false);
}

void CDownloadValidator::AddToMapInternal(const uchar* hash, const CString& filename, EMFileSize filesize, bool bKnownCollectionChanged)
{
	CSingleLock indexLock(&m_indexLock, TRUE);
	if (theApp.IsClosing() || filename.IsEmpty() || filesize == 0ull || hash == NULL || !theApp.DownloadValidator)
		return;

	if (bKnownCollectionChanged) {
		if (m_pReloadMapState != NULL && !m_pReloadMapState->bRegexOnly && m_pReloadMapState->ePhase <= SReloadMapState::PhaseDuplicateFiles)
			m_pReloadMapState->bTraversalRestartRequired = true;
		if (::InterlockedCompareExchange(&m_lBackgroundFuzzyState, 0, 0) == DownloadValidatorBackgroundCapture)
			::InterlockedExchange(&m_lDeferredFuzzyCaptureRestartRequired, 1);
	}

	uint8 uSourceFlags = FuzzyFileSourceUnknown;
	uint32 uMediaLengthSec = 0;
	EFuzzyMediaLengthSource eMediaLengthSource = FuzzyMediaLengthUnknown;
	if (IsFuzzyMatchingEnabled() || thePrefs.GetDownloadValidatorMediaLengthMatching())
		ResolveFileMetadata(hash, filesize, uSourceFlags, uMediaLengthSec, eMediaLengthSource);
	AddTrustedToMapInternal(hash, filename, filesize, uMediaLengthSec, eMediaLengthSource);
	const LONG lBackgroundState = ::InterlockedCompareExchange(&m_lBackgroundFuzzyState, 0, 0);
	if (lBackgroundState == DownloadValidatorBackgroundCapture || lBackgroundState == DownloadValidatorBackgroundRecords) {
		QueueDeferredFuzzySource(hash, filename, filesize, uSourceFlags, uMediaLengthSec, eMediaLengthSource);
	} else if (lBackgroundState == DownloadValidatorBackgroundPostings && m_pBackgroundFuzzyPrepareState != NULL) {
		try {
			bool bMerged = false;
			for (std::vector<SReloadMapState::SPendingFuzzyRecord>::iterator it = m_pBackgroundFuzzyPrepareState->pendingFuzzyRecords.begin(); it != m_pBackgroundFuzzyPrepareState->pendingFuzzyRecords.end(); ++it) {
				if (!IsDownloadValidatorFileMatch(it->fileInfo, hash, filesize) || it->fileInfo.strName != filename)
					continue;
				it->uSourceFlags = static_cast<uint8>(it->uSourceFlags | uSourceFlags);
				MergeDownloadValidatorFileMetadata(it->fileInfo, uMediaLengthSec, eMediaLengthSource);
				bMerged = true;
				break;
			}
			if (!bMerged) {
				SReloadMapState::SPendingFuzzyRecord pendingRecord;
				SetDownloadValidatorFileInfo(pendingRecord.fileInfo, hash, filename, filesize, uMediaLengthSec, eMediaLengthSource);
				pendingRecord.uSourceFlags = uSourceFlags;
				m_pBackgroundFuzzyPrepareState->pendingFuzzyRecords.push_back(pendingRecord);
			}
		} catch (CMemoryException* ex) {
			ex->Delete();
			AbortFuzzyIndexBuild();
			CancelDeferredBackgroundWork();
		} catch (const std::exception&) {
			AbortFuzzyIndexBuild();
			CancelDeferredBackgroundWork();
		}
	} else
		AddFuzzyRecord(hash, filename, filesize, uSourceFlags, uMediaLengthSec, eMediaLengthSource);
	NotifyIncrementalMapMutation();
}

void CDownloadValidator::RemoveFromMap(const uchar* hash, const CString& filename, const EMFileSize filesize)
{
	CSingleLock indexLock(&m_indexLock, TRUE);
	if (theApp.IsClosing() || filename.IsEmpty() || filesize == 0ull || hash == NULL || !theApp.DownloadValidator)
		return;

	if (m_pReloadMapState != NULL && !m_pReloadMapState->bRegexOnly && m_pReloadMapState->ePhase <= SReloadMapState::PhaseDuplicateFiles)
		m_pReloadMapState->bTraversalRestartRequired = true;
	if (::InterlockedCompareExchange(&m_lBackgroundFuzzyState, 0, 0) == DownloadValidatorBackgroundCapture)
		::InterlockedExchange(&m_lDeferredFuzzyCaptureRestartRequired, 1);

	if (theApp.downloadqueue != NULL && theApp.downloadqueue->GetFileByID(hash) != NULL)
		return;
	if (theApp.knownfiles != NULL && theApp.knownfiles->FindKnownFileByID(hash) != NULL)
		return;
	if (theApp.knownfiles != NULL && theApp.knownfiles->DuplicatesCount(hash) != 0)
		return;

	if (m_pReloadMapState != NULL && !m_pReloadMapState->bRegexOnly
		&& (m_pReloadMapState->ePhase == SReloadMapState::PhaseFuzzyPrepare)) {
		for (std::vector<SReloadMapState::SPendingFuzzyRecord>::iterator it = m_pReloadMapState->pendingFuzzyRecords.begin(); it != m_pReloadMapState->pendingFuzzyRecords.end(); ++it) {
			if (!IsDownloadValidatorFileMatch(it->fileInfo, hash, filesize) || it->fileInfo.strName != filename)
				continue;
			m_pReloadMapState->pendingFuzzyRecords.erase(it);
			break;
		}
	}
	DeactivateDeferredFuzzySource(hash, filename, filesize);

	const CString strProcessedFileName(BuildMapKey(filename));
	RemovePreparedFromMap(m_DownloadValidatorMap, strProcessedFileName, hash, filesize);
	if (thePrefs.GetDownloadValidatorDateTimeMatching())
		RemovePreparedFromMap(m_DownloadValidatorDateTimeMap, BuildDateTimeMapKey(filename, strProcessedFileName), hash, filesize);
	RemoveRegexMatchesFromMap(hash, filename, filesize);
	RemoveFuzzyRecord(hash, filename, filesize);
	NotifyIncrementalMapMutation();
}

// Returns EDownloadValidatorResult
const UINT CDownloadValidator::CheckFile(const uchar* hash, const CString& filename, const EMFileSize filesize, const bool bCalledByAddToDownload)
{
	CString cLogMsg;
	CSingleLock indexLock(&m_indexLock, TRUE);

	if (bCalledByAddToDownload) {
		if (theApp.downloadqueue->IsFileExisting(hash, bCalledByAddToDownload))
			return EDownloadValidatorResult::Downloading;

		// Check if a file with the same hash and size exists
		if (thePrefs.GetDownloadValidatorRejectSameHash()) {
			CKnownFile* curFile = theApp.knownfiles->FindKnownFileByID(hash);
			if (curFile && curFile->GetFileSize() == filesize && !curFile->IsPartFile()) {
				cLogMsg.Format(GetResString(_T("DOWNLOAD_VALIDATOR_REJECTED_MESSAGE")), GetResString(_T("DOWNLOAD_VALIDATOR_REJECT_REASON_HASH")), filename, curFile->GetFileName());
				AddLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
				return EDownloadValidatorResult::Known;
			}
		}

		// Check if the file has been canceled before
		if (thePrefs.GetDownloadValidatorRejectCanceled() && theApp.knownfiles->IsCancelledFileByID(hash)) {
			cLogMsg.Format(GetResString(_T("DOWNLOAD_VALIDATOR_REJECTED_MESSAGE2")), GetResString(_T("DOWNLOAD_VALIDATOR_REJECT_REASON_CANCELED")), filename);
			AddLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
			return EDownloadValidatorResult::Cancelled;
		}

		// Check if the file name is blacklisted
		if (thePrefs.GetDownloadValidatorRejectBlacklisted()) {
			if (thePrefs.GetBlacklistManual() && theApp.searchlist->IsFilenameManualBlacklisted(CSKey(hash))) {
				cLogMsg.Format(GetResString(_T("DOWNLOAD_VALIDATOR_REJECTED_MESSAGE2")), GetResString(_T("MANUAL_BLACKLISTED")), filename);
				AddLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
				return EDownloadValidatorResult::ManualBlacklisted;
			}

			if (thePrefs.GetBlacklistAutomatic() && CSearchList::IsFilenameAutoBlacklisted(filename)) {
				cLogMsg.Format(GetResString(_T("DOWNLOAD_VALIDATOR_REJECTED_MESSAGE2")), GetResString(_T("AUTOMATIC_BLACKLISTED")), filename);
				AddLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
				return EDownloadValidatorResult::AutomaticBlacklisted;
			}
		}
	}

	if (filename.GetLength() < thePrefs.GetDownloadValidatorMinimumComparisonLength())
		return EDownloadValidatorResult::OK;

	uint8 uSourceFlags = FuzzyFileSourceUnknown;
	uint32 uMediaLengthSec = 0;
	EFuzzyMediaLengthSource eMediaLengthSource = FuzzyMediaLengthUnknown;
	if (thePrefs.GetDownloadValidatorMediaLengthMatching())
		ResolveFileMetadata(hash, filesize, uSourceFlags, uMediaLengthSec, eMediaLengthSource);

	const FileInfoType* pTrustedFileInfo = NULL;
	const CString strProcessedFileName(BuildMapKey(filename));
	const DownloadValidatorFileMap::CPair* pFilePair = m_DownloadValidatorMap.PLookup(strProcessedFileName);
	if (pFilePair != NULL)
		pTrustedFileInfo = &pFilePair->value;

	if (pTrustedFileInfo == NULL && thePrefs.GetDownloadValidatorDateTimeMatching()) {
		const CString strDateTimeMapKey(BuildDateTimeMapKey(filename, strProcessedFileName));
		if (!strDateTimeMapKey.IsEmpty()) {
			pFilePair = m_DownloadValidatorDateTimeMap.PLookup(strDateTimeMapKey);
			if (pFilePair != NULL)
				pTrustedFileInfo = &pFilePair->value;
		}
	}

	if (pTrustedFileInfo == NULL)
		pTrustedFileInfo = FindRegexFileInfo(filename);

	const FileCandidateType* pCandidate = NULL;
	FuzzyCandidateType fuzzyCandidate;
	if (pTrustedFileInfo != NULL)
		pCandidate = FindEligibleFileCandidate(*pTrustedFileInfo, filesize, uMediaLengthSec, eMediaLengthSource);
	else {
		SDownloadValidatorFuzzyQueryData fuzzyQueryData;
		if (IsFuzzyMatchingEnabled() && PrepareFuzzyQueryData(filename, fuzzyQueryData)
			&& FindFuzzyDecisionCandidateStreaming(hash, filename, filesize, uMediaLengthSec, eMediaLengthSource, &fuzzyQueryData, fuzzyCandidate))
			pCandidate = &fuzzyCandidate;
	}
	if (pCandidate == NULL)
		return EDownloadValidatorResult::OK;

	if (thePrefs.GetDownloadValidator() == 2) {
		if (bCalledByAddToDownload) {
			cLogMsg.Format(GetResString(_T("DOWNLOAD_VALIDATOR_REJECTED_MESSAGE")), GetResString(_T("DOWNLOAD_VALIDATOR_REJECT_REASON_KNOWN")), filename, pCandidate->strName);
			AddLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
		}
		return EDownloadValidatorResult::SimilarName;
	}

	if (thePrefs.GetDownloadValidator() == 3) {
		if (bCalledByAddToDownload) {
			cLogMsg.Format(GetResString(_T("DOWNLOAD_VALIDATOR_REJECTED_MESSAGE3")), GetResString(_T("DOWNLOAD_VALIDATOR_REJECT_REASON_KNOWN")), filesize, filename, pCandidate->uSize, pCandidate->strName);
			AddLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
		}
		return EDownloadValidatorResult::SimilarName;
	}

	if (bCalledByAddToDownload) {
		CString msg;
		CString sData;
		msg.Format(GetResString(_T("DOWNHISTORY_CHECK2")));
		msg += GetResString(_T("DOWNHISTORY_CHECK3"));
		msg += _T("\n\n") + GetResString(_T("DOWNHISTORY_CHECK6")) + _T("\n------------------------\n");
		msg += filename + _T("\n");
		sData.Format(GetResString(_T("DL_SIZE")) + _T(": %I64u\n"), filesize);
		msg += sData;
		sData.Format(GetResStringWithColon(_T("ID")) + _T(" %s\n"), EncodeBase16(hash, 16));
		msg += sData;
		msg += _T("\n") + GetResString(_T("DOWNHISTORY_CHECK7")) + _T("\n------------------------\n");
		msg += pCandidate->strName + _T("\n");
		sData.Format(GetResString(_T("DL_SIZE")) + _T(": %I64u\n"), pCandidate->uSize);
		msg += sData;
		sData.Format(GetResStringWithColon(_T("ID")) + _T(" %s\n"), EncodeBase16(pCandidate->ucHash, 16));
		msg += sData + _T("\n\n");

		return CDarkMode::MessageBox(msg, MB_YESNO | MB_ICONQUESTION) == IDYES ? EDownloadValidatorResult::OK : EDownloadValidatorResult::SimilarName;
	}

	return EDownloadValidatorResult::OK;
}
