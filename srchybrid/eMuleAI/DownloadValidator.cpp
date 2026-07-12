//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#include "StdAfx.h"
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

	void GetDownloadValidatorReloadSliceLimits(DWORD& dwSliceBudgetMs, UINT& uMaxItemsPerSlice)
	{
		const UINT uQueueStatus = HIWORD(::GetQueueStatus(QS_KEY | QS_MOUSE | QS_PAINT | QS_TIMER | QS_POSTMESSAGE));
		const bool bInputPending = (uQueueStatus & (QS_KEY | QS_MOUSE)) != 0;
		const bool bPaintPending = (uQueueStatus & QS_PAINT) != 0;
		const bool bDispatchPending = (uQueueStatus & (QS_TIMER | QS_POSTMESSAGE)) != 0;

		if (bInputPending) {
			dwSliceBudgetMs = 3;
			uMaxItemsPerSlice = 128;
			return;
		}
		if (bPaintPending || bDispatchPending) {
			dwSliceBudgetMs = 5;
			uMaxItemsPerSlice = 512;
			return;
		}
		dwSliceBudgetMs = 12;
		uMaxItemsPerSlice = 4096;
	}

	bool IsValidatorDigit(TCHAR ch)
	{
		return ch >= _T('0') && ch <= _T('9');
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
		PhaseDone
	};

	SReloadMapState()
		: ePhase(PhaseDownloads)
		, uDownloadIndex(0)
		, uKnownIndex(0)
		, uDuplicateIndex(0)
	{
	}

	EPhase ePhase;
	size_t uDownloadIndex;
	size_t uKnownIndex;
	size_t uDuplicateIndex;
};

CDownloadValidator::CDownloadValidator(void)
	: m_pReloadMapState(NULL)
{
	m_iDataSize = -1;
}

CDownloadValidator::~CDownloadValidator(void)
{
	delete m_pReloadMapState;
	m_pReloadMapState = NULL;
}

void CDownloadValidator::PrepareReloadMapStorage()
{
	UINT uHashSize = 10000;
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
	m_DownloadValidatorMap.RemoveAll();
	m_DownloadValidatorDateTimeMap.RemoveAll();
	m_DownloadValidatorMap.InitHashTable(uHashSize);
	if (thePrefs.GetDownloadValidatorDateTimeMatching())
		m_DownloadValidatorDateTimeMap.InitHashTable(uHashSize);
}

CString CDownloadValidator::BuildMapKey(const CString& filename) const
{
	CString strProcessedFileName(filename);

	if (thePrefs.GetDownloadValidatorIgnoreExtension())
		strProcessedFileName = RemoveFileExtension(strProcessedFileName);

	if (thePrefs.GetDownloadValidatorIgnoreTags())
		strProcessedFileName = RemoveTags(strProcessedFileName, thePrefs.GetDownloadValidatorDontIgnoreNumericTags());

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

	if (thePrefs.GetDownloadValidatorCaseInsensitive())
		strProcessedFileName.MakeLower();

	return strProcessedFileName;
}

CString CDownloadValidator::BuildDateTimeMapKey(const CString& filename, const CString& strProcessedFileName) const
{
	if (!thePrefs.GetDownloadValidatorDateTimeMatching())
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

void CDownloadValidator::AddPreparedToMap(DownloadValidatorFileMap& map, const CString& strProcessedFileName, const uchar* hash, const CString& filename, const EMFileSize filesize)
{
	if (strProcessedFileName.IsEmpty() || hash == NULL)
		return;

	FileInfoType fileInfo;
	if (map.Lookup(strProcessedFileName, fileInfo) && fileInfo.uSize >= filesize)
		return;

	fileInfo.strName = filename;
	fileInfo.uSize = filesize;
	md4cpy(fileInfo.ucHash, hash);
	map[strProcessedFileName] = fileInfo;
}

void CDownloadValidator::RemovePreparedFromMap(DownloadValidatorFileMap& map, const CString& strProcessedFileName, const uchar* hash, const EMFileSize filesize)
{
	if (strProcessedFileName.IsEmpty())
		return;

	FileInfoType fileInfo;
	if (!map.Lookup(strProcessedFileName, fileInfo))
		return;
	if (!md4equ(fileInfo.ucHash, hash) || fileInfo.uSize != filesize)
		return;

	map.RemoveKey(strProcessedFileName);
}

void CDownloadValidator::ReloadMap()
{
	if (theApp.IsClosing())
		return;

	delete m_pReloadMapState;
	m_pReloadMapState = NULL;
	PrepareReloadMapStorage();

	// Downloading files
	if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL && theApp.emuledlg->transferwnd->GetDownloadList() != NULL) {
		CDownloadListCtrl* pDownloadList = theApp.emuledlg->transferwnd->GetDownloadList();
		for (CDownloadListCtrl::ListItems::const_iterator it = pDownloadList->m_ListItems.begin(); it != pDownloadList->m_ListItems.end(); ++it) {
			const CtrlItem_Struct* cur_item = it->second;
			if (cur_item == NULL || cur_item->type != FILE_TYPE)
				continue;

			CPartFile* pFile = static_cast<CPartFile*>(cur_item->value);
			if (pFile)
				AddToMap(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize());
		}
	}

	// Known files
	if (theApp.knownfiles != NULL) {
		for (const CKnownFilesMap::CPair* pair = theApp.knownfiles->m_Files_map.PGetFirstAssoc(); pair != NULL; pair = theApp.knownfiles->m_Files_map.PGetNextAssoc(pair)) {
			CKnownFile* pFile = pair->value;
			if (pFile)
				AddToMap(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize());
		}

		// Duplicate files
		CSingleLock slDuplicatesLock(&theApp.knownfiles->m_csDuplicatesLock, TRUE);
		for (CKnownFileList::KnownFileList::const_iterator it = theApp.knownfiles->m_duplicateFileList.begin(); it != theApp.knownfiles->m_duplicateFileList.end(); ++it) {
			CKnownFile* pFile = *it;
			if (pFile)
				AddToMap(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize());
		}
	}
}

void CDownloadValidator::QueueReloadMap()
{
	if (theApp.IsClosing())
		return;

	delete m_pReloadMapState;
	m_pReloadMapState = NULL;
	PrepareReloadMapStorage();
	m_pReloadMapState = new SReloadMapState();
}

void CDownloadValidator::CancelReloadMap()
{
	delete m_pReloadMapState;
	m_pReloadMapState = NULL;
}

bool CDownloadValidator::ProcessReloadMapSlice(bool bDrainAll)
{
	if (m_pReloadMapState == NULL)
		return false;
	if (!bDrainAll && theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL && theApp.emuledlg->sharedfileswnd->sharedfilesctrl.IsDeleteLikeBulkOperationActive())
		return true;
	if (theApp.IsClosing()) {
		delete m_pReloadMapState;
		m_pReloadMapState = NULL;
		return false;
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
					if (pFile != NULL)
						AddToMap(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize());
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
			const CKnownFilesMap::CPair* pKnownPair = theApp.knownfiles->m_Files_map.PGetFirstAssoc();
			for (size_t uSkip = 0; pKnownPair != NULL && uSkip < m_pReloadMapState->uKnownIndex; ++uSkip)
				pKnownPair = theApp.knownfiles->m_Files_map.PGetNextAssoc(pKnownPair);
			while (pKnownPair != NULL) {
				CKnownFile* pFile = pKnownPair->value;
				pKnownPair = theApp.knownfiles->m_Files_map.PGetNextAssoc(pKnownPair);
				++m_pReloadMapState->uKnownIndex;
				if (pFile != NULL)
					AddToMap(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize());
				++uProcessed;
				if (!bDrainAll && (uProcessed >= uMaxItemsPerSlice || ((uProcessed & 0x1F) == 0 && static_cast<DWORD>(::GetTickCount() - dwSliceStart) >= dwSliceBudgetMs)))
					return true;
			}
			m_pReloadMapState->ePhase = SReloadMapState::PhaseDuplicateFiles;
			continue;
		}

		if (m_pReloadMapState->ePhase == SReloadMapState::PhaseDuplicateFiles) {
			if (theApp.knownfiles == NULL) {
				m_pReloadMapState->ePhase = SReloadMapState::PhaseDone;
				continue;
			}
			CSingleLock slDuplicatesLock(&theApp.knownfiles->m_csDuplicatesLock, TRUE);
			CKnownFileList::KnownFileList::const_iterator itDuplicate = theApp.knownfiles->m_duplicateFileList.begin();
			for (size_t uSkip = 0; itDuplicate != theApp.knownfiles->m_duplicateFileList.end() && uSkip < m_pReloadMapState->uDuplicateIndex; ++uSkip)
				++itDuplicate;
			while (itDuplicate != theApp.knownfiles->m_duplicateFileList.end()) {
				CKnownFile* pFile = *itDuplicate;
				++itDuplicate;
				++m_pReloadMapState->uDuplicateIndex;
				if (pFile != NULL)
					AddToMap(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize());
				++uProcessed;
				if (!bDrainAll && (uProcessed >= uMaxItemsPerSlice || ((uProcessed & 0x1F) == 0 && static_cast<DWORD>(::GetTickCount() - dwSliceStart) >= dwSliceBudgetMs)))
					return true;
			}
			m_pReloadMapState->ePhase = SReloadMapState::PhaseDone;
			continue;
		}
	}

	delete m_pReloadMapState;
	m_pReloadMapState = NULL;
	return uProcessed != 0;
}

void CDownloadValidator::AddToMap(const uchar* hash, const CString& filename, const EMFileSize filesize)
{
	if (theApp.IsClosing() || filename.IsEmpty() || filesize == 0ull || !theApp.DownloadValidator)
		return;

	const CString strProcessedFileName(BuildMapKey(filename));
	AddPreparedToMap(m_DownloadValidatorMap, strProcessedFileName, hash, filename, filesize);
	if (thePrefs.GetDownloadValidatorDateTimeMatching())
		AddPreparedToMap(m_DownloadValidatorDateTimeMap, BuildDateTimeMapKey(filename, strProcessedFileName), hash, filename, filesize);
}

void CDownloadValidator::RemoveFromMap(const uchar* hash, const CString& filename, const EMFileSize filesize)
{
	if (theApp.IsClosing() || filename.IsEmpty() || filesize == 0ull || hash == NULL || !theApp.DownloadValidator)
		return;

	if (theApp.downloadqueue != NULL && theApp.downloadqueue->GetFileByID(hash) != NULL)
		return;
	if (theApp.knownfiles != NULL && theApp.knownfiles->FindKnownFileByID(hash) != NULL)
		return;
	if (theApp.knownfiles != NULL && theApp.knownfiles->DuplicatesCount(hash) != 0)
		return;

	const CString strProcessedFileName(BuildMapKey(filename));
	RemovePreparedFromMap(m_DownloadValidatorMap, strProcessedFileName, hash, filesize);
	if (thePrefs.GetDownloadValidatorDateTimeMatching())
		RemovePreparedFromMap(m_DownloadValidatorDateTimeMap, BuildDateTimeMapKey(filename, strProcessedFileName), hash, filesize);
}

// Returns EDownloadValidatorResult
const UINT CDownloadValidator::CheckFile(const uchar* hash, const CString& filename, const EMFileSize filesize, const bool bCalledByAddToDownload)
{
	CString cLogMsg;
	FileInfoType m_FileInfo;
	CString m_strProcessedFileName;

	if (bCalledByAddToDownload) {
		if (theApp.downloadqueue->IsFileExisting(hash, bCalledByAddToDownload))
			return EDownloadValidatorResult::Downloading;

		// Check if a file with the same hash and size exists
		if (thePrefs.GetDownloadValidatorRejectSameHash()) {
			CKnownFile* curFile = theApp.knownfiles->FindKnownFileByID(hash);
			if (curFile && (curFile->GetFileSize() == filesize) && !curFile->IsPartFile()) {
				cLogMsg.Format(GetResString(_T("DOWNLOAD_VALIDATOR_REJECTED_MESSAGE")), GetResString(_T("DOWNLOAD_VALIDATOR_REJECT_REASON_HASH")), filename, curFile->GetFileName());
				AddLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
				return EDownloadValidatorResult::Known; // A file with same hash and size is found and user wants to reject it automatically without any further checks 
			}
		}

		// Check if the file has been canceled before
		if (thePrefs.GetDownloadValidatorRejectCanceled() && theApp.knownfiles->IsCancelledFileByID(hash)) {
			cLogMsg.Format(GetResString(_T("DOWNLOAD_VALIDATOR_REJECTED_MESSAGE2")), GetResString(_T("DOWNLOAD_VALIDATOR_REJECT_REASON_CANCELED")), filename);
			AddLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
			return EDownloadValidatorResult::Cancelled; // A canceled file with same hash found and user wants to reject it automatically without any further checks
		}

		// Check if the file name is blacklisted
		if (thePrefs.GetDownloadValidatorRejectBlacklisted()) {
			// Check if the file name is manually blacklisted
			if (thePrefs.GetBlacklistManual() && theApp.searchlist->IsFilenameManualBlacklisted(CSKey(hash))) {
				cLogMsg.Format(GetResString(_T("DOWNLOAD_VALIDATOR_REJECTED_MESSAGE2")), GetResString(_T("DOWNLOAD_VALIDATOR_REJECT_REASON_MANUAL_BLACKLISTED")), filename);
				AddLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
				return EDownloadValidatorResult::ManualBlacklisted; // File name is manually blacklisted and user wants to reject it automatically without any further checks
			}

			// Check if the file name is automatically blacklisted
			if (thePrefs.GetBlacklistAutomatic() && CSearchList::IsFilenameAutoBlacklisted(filename)) {
				cLogMsg.Format(GetResString(_T("DOWNLOAD_VALIDATOR_REJECTED_MESSAGE2")), GetResString(_T("DOWNLOAD_VALIDATOR_REJECT_REASON_AUTOMATIC_BLACKLISTED")), filename);
				AddLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
				return EDownloadValidatorResult::AutomaticBlacklisted; // File name is automatically blacklisted and user wants to reject it automatically without any further checks
			}
		}
	}

	// Check if a file with the same name exists in the map
	m_strProcessedFileName = filename;

	if (m_strProcessedFileName.GetLength() < thePrefs.GetDownloadValidatorMinimumComparisonLength())
		return EDownloadValidatorResult::OK; // Stop comparison if file name size doesn't meet the condition

	if (thePrefs.GetDownloadValidatorIgnoreExtension())
		m_strProcessedFileName = RemoveFileExtension(m_strProcessedFileName);

	if (thePrefs.GetDownloadValidatorIgnoreTags())
		m_strProcessedFileName = RemoveTags(m_strProcessedFileName, thePrefs.GetDownloadValidatorDontIgnoreNumericTags());

	if (thePrefs.GetDownloadValidatorIgnoreNonAlphaNumeric())
		m_strProcessedFileName = RemoveNonAlphaNumeric(m_strProcessedFileName);

	if (thePrefs.GetDownloadValidatorCaseInsensitive())
		m_strProcessedFileName.MakeLower();

	if (!m_DownloadValidatorMap.Lookup(m_strProcessedFileName, m_FileInfo)) {
		if (!thePrefs.GetDownloadValidatorDateTimeMatching())
			return EDownloadValidatorResult::OK; // Key not found, stop here.
		const CString strDateTimeMapKey(BuildDateTimeMapKey(filename, m_strProcessedFileName));
		if (strDateTimeMapKey.IsEmpty() || !m_DownloadValidatorDateTimeMap.Lookup(strDateTimeMapKey, m_FileInfo))
			return EDownloadValidatorResult::OK; // Key not found, stop here.
	}

	if (thePrefs.GetDownloadValidator() == 2) { // if a file with the same name found, user wants to reject it automatically without any further checks
		if (bCalledByAddToDownload) {
			cLogMsg.Empty();
			cLogMsg.Format(GetResString(_T("DOWNLOAD_VALIDATOR_REJECTED_MESSAGE")), GetResString(_T("DOWNLOAD_VALIDATOR_REJECT_REASON_KNOWN")), filename, m_FileInfo.strName);
			AddLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
		}
		return EDownloadValidatorResult::SimilarName;
	}

	if ((thePrefs.GetDownloadValidator() == 3) && (double(filesize) < (double(m_FileInfo.uSize)) * 0.01 * (100 + thePrefs.GetDownloadValidatorAcceptPercentage()))) { // Reject if new file size is smaller then old file + defined percentage
		if (bCalledByAddToDownload) {
			cLogMsg.Empty();
			cLogMsg.Format(GetResString(_T("DOWNLOAD_VALIDATOR_REJECTED_MESSAGE3")), GetResString(_T("DOWNLOAD_VALIDATOR_REJECT_REASON_KNOWN")), filesize, filename, m_FileInfo.uSize, m_FileInfo.strName);
			AddLogLine(true, (LPCTSTR)EscPercent(cLogMsg));
		}
		return EDownloadValidatorResult::SimilarName;
	}

	if (thePrefs.GetDownloadValidator() == 2 || thePrefs.GetDownloadValidator() == 3) // Since conditions are not met till now, we can return here.
		return EDownloadValidatorResult::OK;

	// Construct message
	if (bCalledByAddToDownload) {
		CString msg;
		CString sData;
		msg.Format(GetResString(_T("DOWNHISTORY_CHECK2")));
		msg += GetResString(_T("DOWNHISTORY_CHECK3"));
		msg += _T("\n\n") + GetResString(_T("DOWNHISTORY_CHECK6")) + _T("\n------------------------\n");
		msg += filename + _T("\n");
		sData.Format(GetResString(_T("DL_SIZE")) + _T(": %I64u\n"), filesize);
		msg += sData;
		sData.Format(GetResString(_T("ID2")) + _T(" %s\n"), EncodeBase16(hash, 16));
		msg += sData;
		msg += _T("\n") + GetResString(_T("DOWNHISTORY_CHECK7")) + _T("\n------------------------\n");
		msg += m_FileInfo.strName + _T("\n");
		sData.Format(GetResString(_T("DL_SIZE")) + _T(": %I64u\n"), m_FileInfo.uSize);
		msg += sData;
		sData.Format(GetResString(_T("ID2")) + _T(" %s\n"), EncodeBase16(m_FileInfo.ucHash, 16));
		msg += (sData + _T("\n\n"));

		if (CDarkMode::MessageBox(msg, MB_YESNO | MB_ICONQUESTION) == IDYES)
			return EDownloadValidatorResult::OK;
		else
			return EDownloadValidatorResult::SimilarName;
	}

	return EDownloadValidatorResult::OK; // This shouldn't be reached normally, just in case.
}
