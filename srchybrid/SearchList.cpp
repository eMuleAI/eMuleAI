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
#include "emule.h"
#include "StringConversion.h"
#include "SearchFile.h"
#include "SearchList.h"
#include "SearchParams.h"
#include "SearchResultsWnd.h"
#include "Packets.h"
#include "Preferences.h"
#include "UpDownClient.h"
#include "SafeFile.h"
#include "SharedFileList.h"
#include "KnownFileList.h"
#include "DownloadQueue.h"
#include "PartFile.h"
#include "kademlia/utils/uint128.h"
#include "Kademlia/Kademlia/Entry.h"
#include "Kademlia/Kademlia/SearchManager.h"
#include "emuledlg.h"
#include "SearchDlg.h"
#include "SearchListCtrl.h"
#include "Log.h"
#include "MediaInfo.h"
#include "PartFileWriteThread.h"
#include "OtherFunctions.h"
#ifdef _DEBUG
#include "eMuleAI\DebugLeakHelper.h"
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	const size_t kLargeMetFileBufferSize = 256 * 1024;
}

SSearchResultId::SSearchResultId()
	: m_nSearchID(0)
	, m_bChild(false)
{
	Clear();
}

void SSearchResultId::Clear()
{
	m_nSearchID = 0;
	md4clr(m_abyFileHash);
	m_bChild = false;
	m_strFileName.Empty();
}

bool SSearchResultId::IsValid() const
{
	static const uchar abyEmptyHash[MDX_DIGEST_SIZE] = { 0 };
	return m_nSearchID != 0 && !md4equ(m_abyFileHash, abyEmptyHash);
}

void SSearchResultId::Set(uint32 nSearchID, const uchar *pFileHash, bool bChild, LPCTSTR pszFileName)
{
	m_nSearchID = nSearchID;
	if (pFileHash != NULL)
		md4cpy(m_abyFileHash, pFileHash);
	else
		md4clr(m_abyFileHash);
	m_bChild = bChild;
	m_strFileName = pszFileName != NULL ? pszFileName : _T("");
}

bool SSearchResultId::Equals(uint32 nSearchID, const uchar *pFileHash) const
{
	return m_nSearchID == nSearchID && pFileHash != NULL && md4equ(m_abyFileHash, pFileHash);
}

bool SSearchResultId::EqualsRow(uint32 nSearchID, const uchar *pFileHash, bool bChild, LPCTSTR pszFileName) const
{
	if (!Equals(nSearchID, pFileHash) || m_bChild != bChild)
		return false;
	if (m_strFileName.IsEmpty())
		return true;
	return pszFileName != NULL && m_strFileName.CompareNoCase(pszFileName) == 0;
}

namespace
{
	const UINT kSearchIngestDeferredReloadDelayMs = 250;

	bool GuardSearchModelMutation(LPCTSTR pszEntryPoint)
	{
		return theApp.GuardModelMutation(CemuleApp::ModelMutationSearchList, pszEntryPoint);
	}

	class CSearchModelMutationLock
	{
	public:
		CSearchModelMutationLock(const CSearchList *pSearchList, LPCTSTR pszEntryPoint)
			: m_pLock(pSearchList != NULL ? pSearchList->GetSearchModelLock() : NULL)
			, m_bLocked(false)
			, m_bAllowed(GuardSearchModelMutation(pszEntryPoint))
		{
			if (m_bAllowed && m_pLock != NULL) {
				m_pLock->Lock();
				m_bLocked = true;
			}
		}

		~CSearchModelMutationLock()
		{
			if (m_bLocked && m_pLock != NULL)
				m_pLock->Unlock();
		}

		operator bool() const { return m_bAllowed; }

	private:
		CCriticalSection *m_pLock;
		bool m_bLocked;
		bool m_bAllowed;
	};

	class CScopedSearchListUpdateDeferral
	{
	public:
		explicit CScopedSearchListUpdateDeferral(bool &bFlag)
			: m_bFlag(bFlag)
			, m_bPreviousValue(bFlag)
		{
			m_bFlag = true;
		}

		~CScopedSearchListUpdateDeferral()
		{
			m_bFlag = m_bPreviousValue;
		}

	private:
		CScopedSearchListUpdateDeferral(const CScopedSearchListUpdateDeferral&);
		CScopedSearchListUpdateDeferral& operator=(const CScopedSearchListUpdateDeferral&);

		bool &m_bFlag;
		bool m_bPreviousValue;
	};

	void TraceLegacyUiNetworkParse(LPCTSTR pszEntryPoint, uint32 uPacketSize, UINT uResultCount)
	{
		if (!theApp.IsUiThread())
			return;
		static DWORD s_dwLastTraceTick = 0;
		const DWORD dwNow = ::GetTickCount();
		if (s_dwLastTraceTick != 0 && static_cast<DWORD>(dwNow - s_dwLastTraceTick) < 5000)
			return;
		s_dwLastTraceTick = dwNow;
		AddDebugLogLine(DLP_LOW, false, _T("Legacy network parser still running on UI thread. entry=%s size=%u results=%u\n"), pszEntryPoint != NULL ? pszEntryPoint : _T("unknown"), uPacketSize, uResultCount);
	}

	bool ShouldCleanUpSearchResultFile(const CSearchFile *pFile)
	{
		return pFile != NULL && (pFile->GetKnownType() != CSearchFile::NotDetermined || ((thePrefs.IsSearchSpamFilterEnabled() || thePrefs.GetBlacklistAutomatic() || thePrefs.GetBlacklistManual()) && pFile->IsConsideredSpam(true)));
	}

	void SkipStoredSearchFileRecord(CFileDataIO &file, bool bOptUTF8)
	{
		uchar abyFileHash[MDX_DIGEST_SIZE];
		file.ReadHash16(abyFileHash);
		file.ReadUInt32();
		file.ReadUInt16();
		const uint32 uTagCount = file.ReadUInt32();
		for (uint32 i = 0; i < uTagCount; ++i) {
			CTag tag(file, bOptUTF8);
		}
	}

	bool CFileOpenD(CFile &file, LPCTSTR lpszFileName, UINT nOpenFlags, LPCTSTR lpszMsg)
	{
		CFileException ex;
		if (!file.Open(lpszFileName, nOpenFlags, &ex)) {
			if (ex.m_cause != CFileException::fileNotFound)
				DebugLogError(_T("%s%s"), lpszMsg, (LPCTSTR)CExceptionStrDash(ex));
			return false;
		}
		return true;
	}

#ifdef _DEBUG
	void __cdecl ClearStoredSearchesBeforeLeakDump()
	{
		if (theApp.emuledlg != NULL && theApp.emuledlg->searchwnd != NULL && ::IsWindow(theApp.emuledlg->searchwnd->m_hWnd)) {
			theApp.emuledlg->searchwnd->DeleteAllSearches();
			return;
		}

		if (theApp.searchlist != NULL) {
			theApp.searchlist->Clear();
		}
	}
#endif
	static bool ShouldSkipSearchPersistenceForManualLeakDump()
	{
#if defined(_DEBUG) && defined(DEBUGLEAKHELPER)
		TCHAR szManualDump[8] = {};
		const DWORD dwManualDump = GetEnvironmentVariable(_T("EMULE_CRT_FORCE_MANUAL_DUMP"), szManualDump, _countof(szManualDump));
		return dwManualDump > 0 && dwManualDump < _countof(szManualDump) && szManualDump[0] != _T('0');
#else
		return false;
#endif
	}
}

#define SPAMFILTER_FILENAME		_T("SearchSpam.met")
#define SPAMFILTER_FILENAME_TMP	 _T("SearchSpam.met.tmp")
#define STOREDSEARCHES_FILENAME	_T("StoredSearches.met")
#define STOREDSEARCHES_FILENAME_TMP	_T("StoredSearches.met.tmp")

#define STOREDSEARCHES_VERSION	103

namespace
{
	const uint32 kMaxStoredSearchRecordSize = 16u * 1024u * 1024u;
	const UINT kStartupStoredSearchLoadProgressUnits = 10000U;

	UINT ScaleStoredSearchLoadByteProgress(ULONGLONG ullPosition, ULONGLONG ullLength)
	{
		if (ullLength == 0)
			return kStartupStoredSearchLoadProgressUnits;
		if (ullPosition >= ullLength)
			return kStartupStoredSearchLoadProgressUnits;
		return static_cast<UINT>((ullPosition * kStartupStoredSearchLoadProgressUnits) / ullLength);
	}

	void CopySearchMemFileToAsyncDiskData(CSafeMemFile& source, AsyncDiskWriteData& target)
	{
		const ULONGLONG ullLength = source.GetLength();
		target.data.clear();
		if (ullLength == 0)
			return;
		if (ullLength > static_cast<ULONGLONG>(UINT_MAX))
			AfxThrowFileException(CFileException::genericException, 0, NULL);
		target.data.resize(static_cast<size_t>(ullLength));
		source.Seek(0, CFile::begin);
		source.Read(&target.data[0], static_cast<UINT>(ullLength));
	}
}
///////////////////////////////////////////////////////////////////////////////
// CSearchList

CSearchList::CSearchList()
	: outputwnd()
	, m_nCurED2KSearchID()
	, m_bSpamFilterLoaded()
	, m_bKadReloadWaiting()
	, m_dwKadLastReloadTick()
	, m_bDeferSearchListUpdates()
	, m_pSearchAnswerParseThread(NULL)
	, m_hSearchAnswerParseEvent(NULL)
	, m_hSearchAnswerParseStopEvent(NULL)
	, m_lSearchAnswerParseGeneration(0)
	, m_lSpamFilterSaveGeneration(0)
	, m_lStoredSearchesSaveGeneration(0)
	, m_lStoredSearchStartupLoadGeneration(0)
	, m_uStoredSearchStartupLoadCancellationToken(0)
	, m_lSearchModelSequence(0)
	, m_bChunkedSearchIngestPending()
	, m_dwChunkedSearchIngestLastProgressTick()
	, m_pStoredSearchStartupLoadParams(NULL)
	, m_nStoredSearchStartupLoadNextSearchID(0)
	, m_uStoredSearchStartupLoadTotalSearches(0)
	, m_uStoredSearchStartupLoadLoadedSearches(0)
	, m_uStoredSearchStartupLoadLoadedFiles(0)
	, m_uStoredSearchStartupLoadCurrentRemainingFiles(0)
	, m_uStoredSearchStartupLoadCurrentTotalFiles(0)
	, m_bStoredSearchStartupLoadActive(false)
	, m_bStoredSearchStartupLoadCompleted(false)
	, m_bStoredSearchStartupLoadLoadedVisibleSearch(false)
	, m_bStoredSearchStartupLoadReloadedVisibleSearch(false)
	, m_bStoredSearchStartupLoadCurrentDeleteParams(false)
	, m_bStoredSearchStartupLoadCurrentIsLastTab(false)
	, m_dwStoredSearchStartupLoadStartedTick(0)
	, m_dwStoredSearchStartupLoadLastProgressTick(0)
{
	m_nLastSaved = ::GetTickCount();
#ifdef _DEBUG
	DebugLeakHelper::RegisterPreDumpHook(&ClearStoredSearchesBeforeLeakDump);
#endif
}

CSearchList::~CSearchList()
{
	CancelStartupLoad();
	StopSearchAnswerParseThread();
	Clear();
	for (POSITION pos = m_mUDPServerRecords.GetStartPosition(); pos != NULL;) {
		uint32 dwIP;
		UDPServerRecord *pRecord;
		m_mUDPServerRecords.GetNextAssoc(pos, dwIP, pRecord);
		delete pRecord;
	}
}

CSearchList::SStartupStoredSearchTab::SStartupStoredSearchTab()
	: pParams(NULL)
	, uStoredFileCount(0)
	, nAssignedSearchID(0)
	, bTabCreated(false)
	, bDeleteParams(false)
	, bLastTab(false)
	, bIngestQueued(false)
	, uQueuedFileCount(0)
{
}

CSearchList::SStartupStoredSearchesLoadResult::SStartupStoredSearchesLoadResult()
	: uNextTab(0)
	, uNextFile(0)
	, uTotalSearches(0)
	, uTotalFiles(0)
	, lGeneration(0)
	, uCancellationToken(0)
	, bSuccess(false)
	, bApplyStarted(false)
	, dwLastError(0)
{
}

CSearchList::SSearchIngestRecord::SSearchIngestRecord()
	: m_nSearchID(0)
	, m_nServerIP(0)
	, m_nServerPort(0)
	, m_uServerAvail(0)
	, m_uKadPublishInfo(0)
	, m_bKademlia(false)
	, m_bServerUDPAnswer(false)
	, m_bPreviewPossible(false)
	, m_bMultipleAICHFound(false)
	, m_bAutomaticBlacklistEvaluated(false)
	, m_bAutomaticBlacklisted(false)
{
}

CSearchList::SChunkedSearchIngestJob::SChunkedSearchIngestJob()
	: m_iNextRecord(0)
	, m_nSearchID(0)
	, m_bClientResponse(false)
	, m_dwFromUDPServerIP(0)
	, m_bDoSpamRating(false)
	, m_bUseKadReloadThrottle(false)
	, m_bNotifyUiOnCompletion(true)
	, m_bNotifyLocalEd2kSearchEnd(false)
	, m_bMoreResultsAvailable(false)
	, m_uProcessed(0)
	, m_uFailed(0)
	, m_dwStartedTick(::GetTickCount())
	, m_lGeneration(0)
	, m_lSearchGeneration(0)
{
}

CSearchList::SChunkedSearchAnswerParseJob::SChunkedSearchAnswerParseJob()
	: m_uResultCount(0)
	, m_uNextResult(0)
	, m_uPacketPosition(0)
	, m_nSearchID(0)
	, m_nClientID(0)
	, m_nClientPort(0)
	, m_nClientServerIP(0)
	, m_nClientServerPort(0)
	, m_bOptUTF8(false)
	, m_bClientResponse(false)
	, m_bPreviewSupport(false)
	, m_bSupportsLargeFiles(false)
	, m_bDoSpamRating(false)
	, m_bUseKadReloadThrottle(false)
	, m_bNotifyLocalEd2kSearchEnd(false)
	, m_bMoreResultsAvailable(false)
	, m_uProcessed(0)
	, m_uFailed(0)
	, m_dwStartedTick(::GetTickCount())
	, m_dwLastProgressTick(0)
	, m_lGeneration(0)
	, m_lSearchGeneration(0)
{
}

CSearchList::SStoredSearchIngestPrepareJob::SStoredSearchIngestPrepareJob()
	: m_iNextRecord(0)
	, m_nSearchID(0)
	, m_bClientResponse(false)
	, m_dwFromUDPServerIP(0)
	, m_bDoSpamRating(false)
	, m_bUseKadReloadThrottle(false)
	, m_bNotifyUiOnCompletion(true)
	, m_uProcessed(0)
	, m_uFailed(0)
	, m_dwStartedTick(::GetTickCount())
	, m_dwLastProgressTick(0)
	, m_lGeneration(0)
	, m_lSearchGeneration(0)
{
}

CSearchList::SChunkedSpamRatingJob::SChunkedSpamRatingJob()
	: m_pAutomaticBlacklistSnapshot(NULL)
	, m_iNextItem(0)
	, m_iNextPrepareItem(0)
	, m_nSearchID(0)
	, m_bExpectHigher(false)
	, m_bExpectLower(false)
	, m_bRecalculateAll(false)
	, m_ePhase(PhaseReset)
	, m_uProcessed(0)
	, m_dwStartedTick(::GetTickCount())
	, m_dwLastProgressTick(0)
	, m_lGeneration(0)
	, m_lSearchGeneration(0)
{
}


CSearchList::SChunkedSpamRatingJob::~SChunkedSpamRatingJob()
{
	CPreferences::DeleteFilenameAutoBlacklistSnapshot(m_pAutomaticBlacklistSnapshot);
	m_pAutomaticBlacklistSnapshot = NULL;
}

CSearchList::SSearchKnownTypeRefreshItem::SSearchKnownTypeRefreshItem()
	: m_eKnownType(CSearchFile::NotDetermined)
{
}

CSearchList::SChunkedSearchKnownTypeRefreshJob::SChunkedSearchKnownTypeRefreshJob()
	: m_iNextResetItem(0)
	, m_iNextItem(0)
	, m_iNextHash(0)
	, m_dwStartedTick(::GetTickCount())
	, m_bStartupRefresh(false)
{
}

CSearchList::SParsedSearchIngestBatch::SParsedSearchIngestBatch()
	: m_nSearchID(0)
	, m_bClientResponse(false)
	, m_dwFromUDPServerIP(0)
	, m_bDoSpamRating(false)
	, m_bUseKadReloadThrottle(false)
	, m_bNotifyUiOnCompletion(false)
	, m_bNotifyLocalEd2kSearchEnd(false)
	, m_bMoreResultsAvailable(false)
	, m_lGeneration(0)
	, m_lSearchGeneration(0)
{
}

LONG CSearchList::GetSearchModelSequence() const
{
	return ::InterlockedCompareExchange(const_cast<LONG*>(&m_lSearchModelSequence), 0, 0);
}

void CSearchList::TouchSearchModelSequence()
{
	::InterlockedIncrement(&m_lSearchModelSequence);
}

bool CSearchList::BuildSearchIngestRecord(const CSearchFile *pFile, SSearchIngestRecord &record, bool bPrecomputeAutoBlacklist)
{
	if (pFile == NULL)
		return false;

	CSafeMemFile data;
	pFile->StoreToFile(data);
	const ULONGLONG uLength = data.GetLength();
	if (uLength == 0 || uLength > static_cast<ULONGLONG>(UINT_MAX))
		return false;

	record = SSearchIngestRecord();
	record.m_nSearchID = pFile->GetSearchID();
	record.m_nServerIP = pFile->GetClientServerIP();
	record.m_nServerPort = pFile->GetClientServerPort();
	record.m_uServerAvail = pFile->GetIntTagValue(FT_SOURCES);
	const CSimpleArray<CSearchFile::SServer> &servers = pFile->GetServers();
	for (int i = 0; i < servers.GetSize(); ++i) {
		if (servers[i].m_nIP == record.m_nServerIP && servers[i].m_nPort == record.m_nServerPort) {
			record.m_uServerAvail = servers[i].m_uAvail;
			break;
		}
	}
	record.m_uKadPublishInfo = pFile->GetKadPublishInfo();
	record.m_bKademlia = pFile->IsKademlia();
	record.m_bServerUDPAnswer = pFile->IsServerUDPAnswer();
	record.m_bPreviewPossible = pFile->IsPreviewPossible();
	record.m_bMultipleAICHFound = pFile->HasFoundMultipleAICH();
	if (bPrecomputeAutoBlacklist && thePrefs.GetBlacklistAutomatic()) {
		record.m_bAutomaticBlacklisted = thePrefs.IsFilenameAutoBlacklisted(pFile->GetFileName(), NULL);
		record.m_bAutomaticBlacklistEvaluated = true;
	}
	record.m_data.resize(static_cast<size_t>(uLength));
	memcpy(&record.m_data[0], data.GetBuffer(), static_cast<size_t>(uLength));
	return true;
}

CSearchFile* CSearchList::CreateSearchFileFromIngestRecord(const SSearchIngestRecord &record)
{
	if (record.m_data.empty() || record.m_nSearchID == 0)
		return NULL;

	CSafeMemFile data(&record.m_data[0], static_cast<UINT>(record.m_data.size()));
	CSearchFile *pFile = NULL;
	try {
		pFile = new CSearchFile(data, true, record.m_nSearchID, 0, 0, NULL, record.m_bKademlia, record.m_bServerUDPAnswer);
		pFile->SetClientServerIP(record.m_nServerIP);
		pFile->SetClientServerPort(record.m_nServerPort);
		if (record.m_nServerIP != 0 && record.m_nServerPort != 0) {
			CSearchFile::SServer server(record.m_nServerIP, record.m_nServerPort, record.m_bServerUDPAnswer);
			server.m_uAvail = record.m_uServerAvail;
			pFile->AddServer(server);
		}
		pFile->SetKadPublishInfo(record.m_uKadPublishInfo);
		if (record.m_bMultipleAICHFound)
			pFile->SetFoundMultipleAICH();
		pFile->SetPreviewPossible(record.m_bPreviewPossible);
		if (record.m_bAutomaticBlacklistEvaluated)
			pFile->SetAutomaticBlacklistEvaluation(true, record.m_bAutomaticBlacklisted);
		return pFile;
	} catch (CException *ex) {
		delete pFile;
		ex->Delete();
		return NULL;
	} catch (...) {
		delete pFile;
		return NULL;
	}
}

bool CSearchList::QueueSearchFileForIngest(CSearchFile *pFile, const CString &strClientHash, bool bClientResponse, uint32 dwFromUDPServerIP, bool bDoSpamRating, bool bUseKadReloadThrottle)
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::QueueSearchFileForIngest"));
	if (!mutationLock) {
		delete pFile;
		return false;
	}

	if (!IsSearchProcessingAcceptingJobs()) {
		delete pFile;
		return false;
	}

	if (pFile == NULL)
		return false;

	SSearchIngestRecord record;
	const uint32 nSearchID = pFile->GetSearchID();
	const bool bRecordReady = BuildSearchIngestRecord(pFile, record);
	delete pFile;

	if (!bRecordReady || nSearchID == 0)
		return false;

	std::vector<SSearchIngestRecord> records;
	records.push_back(record);
	QueueChunkedSearchIngestJob(records, nSearchID, strClientHash, bClientResponse, dwFromUDPServerIP, bDoSpamRating, bUseKadReloadThrottle);
	return true;
}

void CSearchList::ClearChunkedSearchIngestJobs()
{
	CSingleLock searchLock(GetSearchModelLock(), TRUE);
	m_bChunkedSearchIngestPending = false;
	::InterlockedIncrement(&m_lSearchAnswerParseGeneration);
	{
		CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
		while (!m_chunkedSearchAnswerParseJobs.IsEmpty())
			delete m_chunkedSearchAnswerParseJobs.RemoveHead();
		while (!m_storedSearchIngestPrepareJobs.IsEmpty())
			delete m_storedSearchIngestPrepareJobs.RemoveHead();
		while (!m_chunkedSpamRatingPrepareJobs.IsEmpty())
			delete m_chunkedSpamRatingPrepareJobs.RemoveHead();
		m_activeSpamRatingPrepareCounts.RemoveAll();
		m_cancelledSearchAnswerParseIds.RemoveAll();
		m_searchAnswerParseGenerations.RemoveAll();
	}
	{
		CSingleLock lock(&m_parsedSearchIngestBatchLock, TRUE);
		while (!m_parsedSearchIngestBatches.IsEmpty())
			delete m_parsedSearchIngestBatches.RemoveHead();
	}
	ClearKnownTypeRefreshJobsNoLock();
	while (!m_chunkedSearchIngestJobs.IsEmpty())
		delete m_chunkedSearchIngestJobs.RemoveHead();
	while (!m_chunkedSpamRatingJobs.IsEmpty())
		delete m_chunkedSpamRatingJobs.RemoveHead();
	{
		CSingleLock lock(&m_parsedSearchIngestBatchLock, TRUE);
		while (!m_preparedSpamRatingJobs.IsEmpty())
			delete m_preparedSpamRatingJobs.RemoveHead();
	}
	m_bDeferSearchListUpdates = false;
}

bool CSearchList::IsSearchProcessingAcceptingJobs() const
{
	return !theApp.IsBackendLifecycleStopping() && !theApp.IsClosing();
}

void CSearchList::ShutdownSearchProcessingForLifecycle()
{
	CancelStartupLoad();
	StopSearchAnswerParseThread();
	ClearChunkedSearchIngestJobs();
}


void CSearchList::EnforceSearchIngestQueueLimit(uint32 nSearchID)
{
	while (m_chunkedSearchIngestJobs.GetCount() >= 256) {
		POSITION posRemove = NULL;
		for (POSITION pos = m_chunkedSearchIngestJobs.GetHeadPosition(); pos != NULL;) {
			POSITION posCurrent = pos;
			SChunkedSearchIngestJob *pQueuedJob = m_chunkedSearchIngestJobs.GetNext(pos);
			if (pQueuedJob != NULL && pQueuedJob->m_nSearchID == nSearchID) {
				posRemove = posCurrent;
				break;
			}
		}
		if (posRemove == NULL)
			posRemove = m_chunkedSearchIngestJobs.GetHeadPosition();
		if (posRemove == NULL)
			break;
		SChunkedSearchIngestJob *pDroppedJob = m_chunkedSearchIngestJobs.GetAt(posRemove);
		AddDebugLogLine(DLP_HIGH, false, _T("Search ingest queue pressure dropped queued batch. incomingSearch=%u droppedSearch=%u count=%Id\n"), nSearchID, pDroppedJob != NULL ? pDroppedJob->m_nSearchID : 0, m_chunkedSearchIngestJobs.GetCount());
		m_chunkedSearchIngestJobs.RemoveAt(posRemove);
		delete pDroppedJob;
	}
}

void CSearchList::EnforceSearchAnswerParseQueueLimitLocked(uint32 nSearchID)
{
	while (m_chunkedSearchAnswerParseJobs.GetCount() >= 256) {
		POSITION posRemove = NULL;
		for (POSITION pos = m_chunkedSearchAnswerParseJobs.GetHeadPosition(); pos != NULL;) {
			POSITION posCurrent = pos;
			SChunkedSearchAnswerParseJob *pQueuedJob = m_chunkedSearchAnswerParseJobs.GetNext(pos);
			if (pQueuedJob != NULL && (pQueuedJob->m_nSearchID == nSearchID || IsSearchAnswerParseJobStale(pQueuedJob))) {
				posRemove = posCurrent;
				break;
			}
		}
		if (posRemove == NULL)
			posRemove = m_chunkedSearchAnswerParseJobs.GetHeadPosition();
		if (posRemove == NULL)
			break;
		SChunkedSearchAnswerParseJob *pDroppedJob = m_chunkedSearchAnswerParseJobs.GetAt(posRemove);
		AddDebugLogLine(DLP_HIGH, false, _T("Search parser queue pressure dropped queued packet. incomingSearch=%u droppedSearch=%u count=%Id\n"), nSearchID, pDroppedJob != NULL ? pDroppedJob->m_nSearchID : 0, m_chunkedSearchAnswerParseJobs.GetCount());
		m_chunkedSearchAnswerParseJobs.RemoveAt(posRemove);
		delete pDroppedJob;
	}
}

void CSearchList::EnforceStoredSearchPrepareQueueLimitLocked(uint32 nSearchID)
{
	while (m_storedSearchIngestPrepareJobs.GetCount() >= 256) {
		POSITION posRemove = m_storedSearchIngestPrepareJobs.GetHeadPosition();
		if (posRemove == NULL)
			break;
		SStoredSearchIngestPrepareJob *pDroppedJob = m_storedSearchIngestPrepareJobs.GetAt(posRemove);
		AddDebugLogLine(DLP_HIGH, false, _T("Stored search prepare queue pressure dropped queued batch. incomingSearch=%u droppedSearch=%u count=%Id\n"), nSearchID, pDroppedJob != NULL ? pDroppedJob->m_nSearchID : 0, m_storedSearchIngestPrepareJobs.GetCount());
		m_storedSearchIngestPrepareJobs.RemoveAt(posRemove);
		delete pDroppedJob;
	}
}

void CSearchList::EnforceParsedSearchIngestBatchLimitLocked(uint32 nSearchID)
{
	while (m_parsedSearchIngestBatches.GetCount() >= 256) {
		POSITION posRemove = NULL;
		for (POSITION pos = m_parsedSearchIngestBatches.GetHeadPosition(); pos != NULL;) {
			POSITION posCurrent = pos;
			SParsedSearchIngestBatch *pQueuedBatch = m_parsedSearchIngestBatches.GetNext(pos);
			if (pQueuedBatch != NULL && pQueuedBatch->m_nSearchID == nSearchID) {
				posRemove = posCurrent;
				break;
			}
		}
		if (posRemove == NULL)
			posRemove = m_parsedSearchIngestBatches.GetHeadPosition();
		if (posRemove == NULL)
			break;
		SParsedSearchIngestBatch *pDroppedBatch = m_parsedSearchIngestBatches.GetAt(posRemove);
		AddDebugLogLine(DLP_HIGH, false, _T("Parsed search ingest queue pressure dropped queued batch. incomingSearch=%u droppedSearch=%u count=%Id\n"), nSearchID, pDroppedBatch != NULL ? pDroppedBatch->m_nSearchID : 0, m_parsedSearchIngestBatches.GetCount());
		m_parsedSearchIngestBatches.RemoveAt(posRemove);
		delete pDroppedBatch;
	}
}

void CSearchList::EnforceSpamRatingPrepareQueueLimitLocked(uint32 nSearchID)
{
	while (m_chunkedSpamRatingPrepareJobs.GetCount() >= 64) {
		POSITION posRemove = m_chunkedSpamRatingPrepareJobs.GetHeadPosition();
		if (posRemove == NULL)
			break;
		SChunkedSpamRatingJob *pDroppedJob = m_chunkedSpamRatingPrepareJobs.GetAt(posRemove);
		AddDebugLogLine(DLP_HIGH, false, _T("Spam rating prepare queue pressure dropped queued job. incomingSearch=%u droppedSearch=%u count=%Id\n"), nSearchID, pDroppedJob != NULL ? pDroppedJob->m_nSearchID : 0, m_chunkedSpamRatingPrepareJobs.GetCount());
		m_chunkedSpamRatingPrepareJobs.RemoveAt(posRemove);
		delete pDroppedJob;
	}
}

void CSearchList::EnforcePreparedSpamRatingQueueLimitLocked(uint32 nSearchID)
{
	while (m_preparedSpamRatingJobs.GetCount() >= 64) {
		POSITION posRemove = m_preparedSpamRatingJobs.GetHeadPosition();
		if (posRemove == NULL)
			break;
		SChunkedSpamRatingJob *pDroppedJob = m_preparedSpamRatingJobs.GetAt(posRemove);
		AddDebugLogLine(DLP_HIGH, false, _T("Prepared spam rating queue pressure dropped queued job. incomingSearch=%u droppedSearch=%u count=%Id\n"), nSearchID, pDroppedJob != NULL ? pDroppedJob->m_nSearchID : 0, m_preparedSpamRatingJobs.GetCount());
		m_preparedSpamRatingJobs.RemoveAt(posRemove);
		delete pDroppedJob;
	}
}

void CSearchList::EnforceChunkedSpamRatingQueueLimit(uint32 nSearchID)
{
	while (m_chunkedSpamRatingJobs.GetCount() >= 64) {
		POSITION posRemove = m_chunkedSpamRatingJobs.GetHeadPosition();
		if (posRemove == NULL)
			break;
		SChunkedSpamRatingJob *pDroppedJob = m_chunkedSpamRatingJobs.GetAt(posRemove);
		AddDebugLogLine(DLP_HIGH, false, _T("Spam rating queue pressure dropped queued job. incomingSearch=%u droppedSearch=%u count=%Id\n"), nSearchID, pDroppedJob != NULL ? pDroppedJob->m_nSearchID : 0, m_chunkedSpamRatingJobs.GetCount());
		m_chunkedSpamRatingJobs.RemoveAt(posRemove);
		delete pDroppedJob;
	}
}

void CSearchList::QueueChunkedSearchIngestJob(std::vector<SSearchIngestRecord> &records, uint32 nSearchID, const CString &strClientHash, bool bClientResponse, uint32 dwFromUDPServerIP, bool bDoSpamRating, bool bUseKadReloadThrottle)
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::QueueChunkedSearchIngestJob"));
	if (!mutationLock)
		return;

	if (!IsSearchProcessingAcceptingJobs())
		return;

	if (records.empty())
		return;

	SChunkedSearchIngestJob *pJob = new SChunkedSearchIngestJob();
	pJob->m_nSearchID = nSearchID;
	pJob->m_strClientHash = strClientHash;
	pJob->m_bClientResponse = bClientResponse;
	pJob->m_dwFromUDPServerIP = dwFromUDPServerIP;
	pJob->m_bDoSpamRating = bDoSpamRating;
	pJob->m_bUseKadReloadThrottle = bUseKadReloadThrottle;
	pJob->m_lGeneration = ::InterlockedCompareExchange(&m_lSearchAnswerParseGeneration, 0, 0);
	pJob->m_lSearchGeneration = GetSearchAnswerParseGeneration(nSearchID);
	pJob->m_records.swap(records);
	EnforceSearchIngestQueueLimit(nSearchID);
	m_chunkedSearchIngestJobs.AddTail(pJob);
	if (!m_bChunkedSearchIngestPending && !PostChunkedSearchIngestMessage()) {
		AddDebugLogLine(DLP_LOW, false, _T("Chunked search ingest cancelled because the UI message target is unavailable. remainingJobs=%d\n"), static_cast<int>(m_chunkedSearchIngestJobs.GetCount()));
		ClearChunkedSearchIngestJobs();
	}
	theApp.QueueSearchActivityChangedEvent(nSearchID);
}

LONG CSearchList::GetSearchAnswerParseGeneration(uint32 nSearchID)
{
	CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
	LONG lGeneration = 0;
	m_searchAnswerParseGenerations.Lookup(nSearchID, lGeneration);
	return lGeneration;
}

void CSearchList::QueueChunkedSearchAnswerParseJob(SChunkedSearchAnswerParseJob *pJob)
{
	if (pJob == NULL)
		return;

	if (!IsSearchProcessingAcceptingJobs()) {
		delete pJob;
		return;
	}

	{
		CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
		LONG lSearchGeneration = 0;
		m_searchAnswerParseGenerations.Lookup(pJob->m_nSearchID, lSearchGeneration);
		if (pJob->m_lSearchGeneration != lSearchGeneration) {
			AddDebugLogLine(DLP_LOW, false, _T("Stale client search answer parse job dropped before queueing. search=%u generation=%ld current=%ld\n"), pJob->m_nSearchID, pJob->m_lSearchGeneration, lSearchGeneration);
			delete pJob;
			return;
		}
	}

	if (!StartSearchAnswerParseThread()) {
		theApp.QueueSearchPacketParseEvent(CemuleApp::ApplicationEventSearchPacketParseFailed, pJob->m_nSearchID, pJob->m_uProcessed, pJob->m_uFailed + 1, pJob->m_uResultCount, _T("search-answer-parser-thread-unavailable"));
		delete pJob;
		return;
	}

	{
		CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
		LONG lSearchGeneration = 0;
		m_searchAnswerParseGenerations.Lookup(pJob->m_nSearchID, lSearchGeneration);
		if (pJob->m_lSearchGeneration != lSearchGeneration) {
			AddDebugLogLine(DLP_LOW, false, _T("Stale client search answer parse job dropped before queueing. search=%u generation=%ld current=%ld\n"), pJob->m_nSearchID, pJob->m_lSearchGeneration, lSearchGeneration);
			delete pJob;
			return;
		}
		m_cancelledSearchAnswerParseIds.RemoveKey(pJob->m_nSearchID);
		EnforceSearchAnswerParseQueueLimitLocked(pJob->m_nSearchID);
		m_chunkedSearchAnswerParseJobs.AddTail(pJob);
	}

	if (!SignalSearchAnswerParseThread()) {
		theApp.QueueSearchPacketParseEvent(CemuleApp::ApplicationEventSearchPacketParseFailed, pJob->m_nSearchID, pJob->m_uProcessed, pJob->m_uFailed + 1, pJob->m_uResultCount, _T("search-answer-parser-signal-failed"));
		CancelSearchAnswerParseJobs(pJob->m_nSearchID);
	}
}

bool CSearchList::QueueStoredSearchIngestPrepareJob(std::vector<SSearchIngestRecord> &records, uint32 nSearchID, const CString &strClientHash, bool bClientResponse, uint32 dwFromUDPServerIP, bool bDoSpamRating, bool bUseKadReloadThrottle)
{
	if (records.empty())
		return true;

	if (!IsSearchProcessingAcceptingJobs())
		return false;

	if (!StartSearchAnswerParseThread())
		return false;

	SStoredSearchIngestPrepareJob *pJob = new SStoredSearchIngestPrepareJob();
	pJob->m_nSearchID = nSearchID;
	pJob->m_strClientHash = strClientHash;
	pJob->m_bClientResponse = bClientResponse;
	pJob->m_dwFromUDPServerIP = dwFromUDPServerIP;
	pJob->m_bDoSpamRating = bDoSpamRating;
	pJob->m_bUseKadReloadThrottle = bUseKadReloadThrottle;
	pJob->m_lGeneration = ::InterlockedCompareExchange(&m_lSearchAnswerParseGeneration, 0, 0);
	{
		CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
		m_searchAnswerParseGenerations.Lookup(nSearchID, pJob->m_lSearchGeneration);
		m_cancelledSearchAnswerParseIds.RemoveKey(nSearchID);
		pJob->m_records.swap(records);
		EnforceStoredSearchPrepareQueueLimitLocked(nSearchID);
		m_storedSearchIngestPrepareJobs.AddTail(pJob);
	}

	if (!SignalSearchAnswerParseThread()) {
		bool bRemoved = false;
		{
			CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
			for (POSITION pos = m_storedSearchIngestPrepareJobs.GetHeadPosition(); pos != NULL;) {
				POSITION posCurrent = pos;
				SStoredSearchIngestPrepareJob *pQueuedJob = m_storedSearchIngestPrepareJobs.GetNext(pos);
				if (pQueuedJob == pJob) {
					m_storedSearchIngestPrepareJobs.RemoveAt(posCurrent);
					pJob->m_records.swap(records);
					bRemoved = true;
					break;
				}
			}
		}
		if (bRemoved) {
			delete pJob;
			AddDebugLogLine(DLP_HIGH, false, _T("Stored search ingest prepare signal failed. search=%u\n"), nSearchID);
			return false;
		}
		AddDebugLogLine(DLP_HIGH, false, _T("Stored search ingest prepare signal failed after the job left the queue. search=%u\n"), nSearchID);
		return true;
	}
	theApp.QueueSearchActivityChangedEvent(nSearchID);
	return true;
}

bool CSearchList::PostChunkedSearchIngestMessage()
{
	if (m_bChunkedSearchIngestPending || !IsSearchProcessingAcceptingJobs())
		return false;
	m_bChunkedSearchIngestPending = theApp.QueueSearchIngestProcessing();
	return m_bChunkedSearchIngestPending;
}

bool CSearchList::IsStartupLoadActiveForSearch(uint32 nSearchID) const
{
	return nSearchID != 0 && m_bStoredSearchStartupLoadActive && m_pStoredSearchStartupLoadParams != NULL && m_pStoredSearchStartupLoadParams->dwSearchID == nSearchID;
}

bool CSearchList::HasPendingSearchProcessing(uint32 nSearchID)
{
	if (nSearchID == 0)
		return HasPendingSearchProcessing();

	{
		CSingleLock searchLock(GetSearchModelLock(), TRUE);
		for (POSITION pos = m_chunkedSearchIngestJobs.GetHeadPosition(); pos != NULL;) {
			const SChunkedSearchIngestJob *pJob = m_chunkedSearchIngestJobs.GetNext(pos);
			if (pJob != NULL && pJob->m_nSearchID == nSearchID)
				return true;
		}

		for (POSITION pos = m_chunkedSpamRatingJobs.GetHeadPosition(); pos != NULL;) {
			const SChunkedSpamRatingJob *pJob = m_chunkedSpamRatingJobs.GetNext(pos);
			if (pJob != NULL && pJob->m_nSearchID == nSearchID)
				return true;
		}
	}

	{
		CSingleLock lock(&m_parsedSearchIngestBatchLock, TRUE);
		for (POSITION pos = m_parsedSearchIngestBatches.GetHeadPosition(); pos != NULL;) {
			const SParsedSearchIngestBatch *pBatch = m_parsedSearchIngestBatches.GetNext(pos);
			if (pBatch != NULL && pBatch->m_nSearchID == nSearchID)
				return true;
		}
		for (POSITION pos = m_preparedSpamRatingJobs.GetHeadPosition(); pos != NULL;) {
			const SChunkedSpamRatingJob *pJob = m_preparedSpamRatingJobs.GetNext(pos);
			if (pJob != NULL && pJob->m_nSearchID == nSearchID)
				return true;
		}
	}

	{
		CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
		if (HasActiveSpamRatingPrepareJob(nSearchID))
			return true;
		for (POSITION pos = m_chunkedSearchAnswerParseJobs.GetHeadPosition(); pos != NULL;) {
			const SChunkedSearchAnswerParseJob *pJob = m_chunkedSearchAnswerParseJobs.GetNext(pos);
			if (pJob != NULL && pJob->m_nSearchID == nSearchID)
				return true;
		}

		for (POSITION pos = m_storedSearchIngestPrepareJobs.GetHeadPosition(); pos != NULL;) {
			const SStoredSearchIngestPrepareJob *pJob = m_storedSearchIngestPrepareJobs.GetNext(pos);
			if (pJob != NULL && pJob->m_nSearchID == nSearchID)
				return true;
		}
		for (POSITION pos = m_chunkedSpamRatingPrepareJobs.GetHeadPosition(); pos != NULL;) {
			const SChunkedSpamRatingJob *pJob = m_chunkedSpamRatingPrepareJobs.GetNext(pos);
			if (pJob != NULL && pJob->m_nSearchID == nSearchID)
				return true;
		}
	}

	return false;
}

bool CSearchList::HasPendingSearchProcessing() const
{
	{
		CSingleLock searchLock(const_cast<CCriticalSection*>(GetSearchModelLock()), TRUE);
		if (!const_cast<CSearchList*>(this)->m_chunkedSearchIngestJobs.IsEmpty() || !const_cast<CSearchList*>(this)->m_chunkedSpamRatingJobs.IsEmpty())
			return true;
	}
	{
		CSingleLock lock(const_cast<CCriticalSection*>(&m_parsedSearchIngestBatchLock), TRUE);
		if (!const_cast<CSearchList*>(this)->m_parsedSearchIngestBatches.IsEmpty() || !const_cast<CSearchList*>(this)->m_preparedSpamRatingJobs.IsEmpty())
			return true;
	}
	{
		CSingleLock lock(const_cast<CCriticalSection*>(&m_searchAnswerParseQueueLock), TRUE);
		return !const_cast<CSearchList*>(this)->m_chunkedSearchAnswerParseJobs.IsEmpty()
			|| !const_cast<CSearchList*>(this)->m_storedSearchIngestPrepareJobs.IsEmpty()
			|| !const_cast<CSearchList*>(this)->m_chunkedSpamRatingPrepareJobs.IsEmpty()
			|| !const_cast<CSearchList*>(this)->m_activeSpamRatingPrepareCounts.IsEmpty();
	}
}

bool CSearchList::HasPendingSpamRatingRecheck(uint32 nSearchID)
{
	if (nSearchID == 0)
		return false;
	for (POSITION pos = m_chunkedSpamRatingJobs.GetHeadPosition(); pos != NULL;) {
		const SChunkedSpamRatingJob *pJob = m_chunkedSpamRatingJobs.GetNext(pos);
		if (pJob != NULL && pJob->m_nSearchID == nSearchID)
			return true;
	}
	{
		CSingleLock lock(&m_parsedSearchIngestBatchLock, TRUE);
		for (POSITION pos = m_preparedSpamRatingJobs.GetHeadPosition(); pos != NULL;) {
			const SChunkedSpamRatingJob *pJob = m_preparedSpamRatingJobs.GetNext(pos);
			if (pJob != NULL && pJob->m_nSearchID == nSearchID)
				return true;
		}
	}
	{
		CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
		if (HasActiveSpamRatingPrepareJob(nSearchID))
			return true;
		for (POSITION pos = m_chunkedSpamRatingPrepareJobs.GetHeadPosition(); pos != NULL;) {
			const SChunkedSpamRatingJob *pJob = m_chunkedSpamRatingPrepareJobs.GetNext(pos);
			if (pJob != NULL && pJob->m_nSearchID == nSearchID)
				return true;
		}
	}
	return false;
}

void CSearchList::CancelChunkedSearchIngestJobs(uint32 nSearchID)
{
	CancelSearchAnswerParseJobs(nSearchID);
	CancelChunkedSpamRatingJobs(nSearchID);

	for (POSITION pos = m_chunkedSearchIngestJobs.GetHeadPosition(); pos != NULL;) {
		POSITION posCurrent = pos;
		SChunkedSearchIngestJob *pJob = m_chunkedSearchIngestJobs.GetNext(pos);
		if (pJob != NULL && pJob->m_nSearchID == nSearchID) {
			m_chunkedSearchIngestJobs.RemoveAt(posCurrent);
			delete pJob;
		}
	}
	if (m_chunkedSearchIngestJobs.IsEmpty() && m_chunkedSpamRatingJobs.IsEmpty())
		m_bChunkedSearchIngestPending = false;
}


void CSearchList::CancelChunkedSpamRatingJobs(uint32 nSearchID)
{
	for (POSITION pos = m_chunkedSpamRatingJobs.GetHeadPosition(); pos != NULL;) {
		POSITION posCurrent = pos;
		SChunkedSpamRatingJob *pJob = m_chunkedSpamRatingJobs.GetNext(pos);
		if (pJob != NULL && (nSearchID == 0 || pJob->m_nSearchID == nSearchID)) {
			m_chunkedSpamRatingJobs.RemoveAt(posCurrent);
			delete pJob;
		}
	}
	{
		CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
		for (POSITION pos = m_chunkedSpamRatingPrepareJobs.GetHeadPosition(); pos != NULL;) {
			POSITION posCurrent = pos;
			SChunkedSpamRatingJob *pJob = m_chunkedSpamRatingPrepareJobs.GetNext(pos);
			if (pJob != NULL && (nSearchID == 0 || pJob->m_nSearchID == nSearchID)) {
				m_chunkedSpamRatingPrepareJobs.RemoveAt(posCurrent);
				delete pJob;
			}
		}
	}
	{
		CSingleLock lock(&m_parsedSearchIngestBatchLock, TRUE);
		for (POSITION pos = m_preparedSpamRatingJobs.GetHeadPosition(); pos != NULL;) {
			POSITION posCurrent = pos;
			SChunkedSpamRatingJob *pJob = m_preparedSpamRatingJobs.GetNext(pos);
			if (pJob != NULL && (nSearchID == 0 || pJob->m_nSearchID == nSearchID)) {
				m_preparedSpamRatingJobs.RemoveAt(posCurrent);
				delete pJob;
			}
		}
	}
	if (m_chunkedSearchIngestJobs.IsEmpty() && m_chunkedSpamRatingJobs.IsEmpty())
		m_bChunkedSearchIngestPending = false;
}

void CSearchList::QueueChunkedSpamRatingJob(uint32 nSearchID, bool bExpectHigher, bool bExpectLower, bool bRecalculateAll)
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::QueueChunkedSpamRatingJob"));
	if (!mutationLock)
		return;

	if (!IsSearchProcessingAcceptingJobs())
		return;

	ASSERT(!(bExpectHigher && bExpectLower));
	ASSERT(m_bSpamFilterLoaded);

	if (!thePrefs.GetBlacklistAutomatic() && !thePrefs.GetBlacklistManual() && !thePrefs.IsSearchSpamFilterEnabled())
		return;

	SearchList *list = GetSearchListForID(nSearchID);
	if (list == NULL || list->GetCount() == 0)
		return;

	CancelChunkedSpamRatingJobs(nSearchID);

	SChunkedSpamRatingJob *pJob = new SChunkedSpamRatingJob();
	pJob->m_nSearchID = nSearchID;
	pJob->m_bExpectHigher = bExpectHigher;
	pJob->m_bExpectLower = bExpectLower;
	pJob->m_bRecalculateAll = bRecalculateAll;
	pJob->m_ePhase = bRecalculateAll ? SChunkedSpamRatingJob::PhaseReset : SChunkedSpamRatingJob::PhaseParents;
	pJob->m_lGeneration = ::InterlockedCompareExchange(&m_lSearchAnswerParseGeneration, 0, 0);
	pJob->m_lSearchGeneration = GetSearchAnswerParseGeneration(nSearchID);
	const bool bPrepareAutomaticBlacklist = thePrefs.GetBlacklistAutomatic();
	pJob->m_ids.reserve(static_cast<size_t>(list->GetCount()));
	if (bPrepareAutomaticBlacklist)
		pJob->m_astrFileNames.reserve(static_cast<size_t>(list->GetCount()));
	for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
		CSearchFile *pFile = list->GetNext(pos);
		if (pFile != NULL) {
			SSearchResultId id;
			id.Set(pFile->GetSearchID(), pFile->GetFileHash(), pFile->GetListParent() != NULL, pFile->GetFileName());
			pJob->m_ids.push_back(id);
			if (bPrepareAutomaticBlacklist)
				pJob->m_astrFileNames.push_back(pFile->GetFileName());
		}
	}

	if (pJob->m_ids.empty()) {
		delete pJob;
		return;
	}

	if (bPrepareAutomaticBlacklist) {
		pJob->m_pAutomaticBlacklistSnapshot = CPreferences::CreateFilenameAutoBlacklistSnapshot();
		pJob->m_abAutomaticBlacklistEvaluated.assign(pJob->m_ids.size(), 0);
		pJob->m_abAutomaticBlacklisted.assign(pJob->m_ids.size(), 0);
		bool bQueuedForPrepare = false;
		bool bAddedToPrepareQueue = false;
		if (StartSearchAnswerParseThread()) {
			{
				CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
				EnforceSpamRatingPrepareQueueLimitLocked(nSearchID);
				m_chunkedSpamRatingPrepareJobs.AddTail(pJob);
				bAddedToPrepareQueue = true;
			}
			bQueuedForPrepare = SignalSearchAnswerParseThread();
		}
		if (!bQueuedForPrepare) {
			AddDebugLogLine(DLP_HIGH, false, _T("Chunked spam rating automatic blacklist prepare could not be queued. search=%u\n"), nSearchID);
			if (bAddedToPrepareQueue)
				CancelChunkedSpamRatingJobs(nSearchID);
			else
				delete pJob;
			return;
		}
	} else {
		EnforceChunkedSpamRatingQueueLimit(nSearchID);
		m_chunkedSpamRatingJobs.AddTail(pJob);
		if (!m_bChunkedSearchIngestPending && !PostChunkedSearchIngestMessage()) {
			AddDebugLogLine(DLP_LOW, false, _T("Chunked spam rating recheck cancelled because the UI message target is unavailable. search=%u\n"), nSearchID);
			CancelChunkedSpamRatingJobs(nSearchID);
		}
	}

	theApp.QueueSearchActivityChangedEvent(nSearchID);
}

void CSearchList::SetSpamRatingPrepareJobActive(uint32 nSearchID, bool bActive)
{
	if (nSearchID == 0)
		return;

	UINT uCount = 0;
	m_activeSpamRatingPrepareCounts.Lookup(nSearchID, uCount);
	if (bActive) {
		m_activeSpamRatingPrepareCounts[nSearchID] = uCount + 1;
		return;
	}

	if (uCount <= 1)
		m_activeSpamRatingPrepareCounts.RemoveKey(nSearchID);
	else
		m_activeSpamRatingPrepareCounts[nSearchID] = uCount - 1;
}

bool CSearchList::HasActiveSpamRatingPrepareJob(uint32 nSearchID)
{
	UINT uCount = 0;
	return nSearchID != 0 && m_activeSpamRatingPrepareCounts.Lookup(nSearchID, uCount) && uCount != 0;
}

void CSearchList::QueuePreparedChunkedSpamRatingJob(SChunkedSpamRatingJob *pJob)
{
	if (pJob == NULL)
		return;
	if (!IsSearchProcessingAcceptingJobs()) {
		delete pJob;
		return;
	}
	{
		CSingleLock lock(&m_parsedSearchIngestBatchLock, TRUE);
		EnforcePreparedSpamRatingQueueLimitLocked(pJob->m_nSearchID);
		m_preparedSpamRatingJobs.AddTail(pJob);
	}
	theApp.QueueSearchIngestProcessing();
}

void CSearchList::DrainPreparedChunkedSpamRatingJobs()
{
	for (;;) {
		SChunkedSpamRatingJob *pJob = NULL;
		{
			CSingleLock lock(&m_parsedSearchIngestBatchLock, TRUE);
			if (m_preparedSpamRatingJobs.IsEmpty())
				break;
			pJob = m_preparedSpamRatingJobs.RemoveHead();
		}
		if (pJob != NULL) {
			if (IsChunkedSpamRatingJobStale(pJob)) {
				delete pJob;
				continue;
			}
			EnforceChunkedSpamRatingQueueLimit(pJob->m_nSearchID);
			m_chunkedSpamRatingJobs.AddTail(pJob);
		}
	}
}

bool CSearchList::IsChunkedSpamRatingJobStale(const SChunkedSpamRatingJob *pJob)
{
	return pJob == NULL || IsSearchJobStale(pJob->m_nSearchID, pJob->m_lGeneration, pJob->m_lSearchGeneration);
}

void CSearchList::ApplyPreparedAutomaticBlacklistResult(const SChunkedSpamRatingJob &job, size_t uIndex, CSearchFile *pFile)
{
	if (pFile == NULL || uIndex >= job.m_abAutomaticBlacklistEvaluated.size() || !job.m_abAutomaticBlacklistEvaluated[uIndex])
		return;
	pFile->SetAutomaticBlacklistEvaluation(true, uIndex < job.m_abAutomaticBlacklisted.size() && job.m_abAutomaticBlacklisted[uIndex] != 0);
}

void CSearchList::ProcessChunkedSpamRatingPrepareJobsOnParserThread()
{
	const DWORD dwSliceStartTick = ::GetTickCount();
	UINT uProcessedInSlice = 0;
	for (;;) {
		SChunkedSpamRatingJob *pJob = NULL;
		{
			CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
			if (m_chunkedSpamRatingPrepareJobs.IsEmpty())
				break;
			pJob = m_chunkedSpamRatingPrepareJobs.RemoveHead();
			if (pJob != NULL)
				SetSpamRatingPrepareJobActive(pJob->m_nSearchID, true);
		}
		if (pJob == NULL)
			continue;

		if (IsChunkedSpamRatingJobStale(pJob)) {
			{
				CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
				SetSpamRatingPrepareJobActive(pJob->m_nSearchID, false);
			}
			delete pJob;
			continue;
		}

		while (pJob->m_iNextPrepareItem < static_cast<INT_PTR>(pJob->m_ids.size()) && !IsChunkedSpamRatingJobStale(pJob)) {
			const size_t uIndex = static_cast<size_t>(pJob->m_iNextPrepareItem++);
			const bool bMatched = uIndex < pJob->m_astrFileNames.size() && !pJob->m_astrFileNames[uIndex].IsEmpty() && CPreferences::IsFilenameAutoBlacklisted(pJob->m_pAutomaticBlacklistSnapshot, pJob->m_astrFileNames[uIndex], NULL);
			if (uIndex < pJob->m_abAutomaticBlacklistEvaluated.size())
				pJob->m_abAutomaticBlacklistEvaluated[uIndex] = 1;
			if (uIndex < pJob->m_abAutomaticBlacklisted.size())
				pJob->m_abAutomaticBlacklisted[uIndex] = bMatched ? 1 : 0;
			++pJob->m_uProcessed;
			++uProcessedInSlice;

			const DWORD dwNow = ::GetTickCount();
			if (pJob->m_dwLastProgressTick == 0 || static_cast<DWORD>(dwNow - pJob->m_dwLastProgressTick) >= theApp.GetTimeBudgetedProgressTraceMs(CemuleApp::TimeBudgetSearchIngest)) {
				pJob->m_dwLastProgressTick = dwNow;
				AddDebugLogLine(DLP_VERYLOW, false, _T("Chunked spam rating auto-blacklist prepare progress. search=%u processed=%u remaining=%d\n"), pJob->m_nSearchID, pJob->m_uProcessed, static_cast<int>(pJob->m_ids.size() - static_cast<size_t>(pJob->m_iNextPrepareItem)));
			}

			if (theApp.IsTimeBudgetExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchIngest))
				break;
		}

		const bool bStale = IsChunkedSpamRatingJobStale(pJob);
		const bool bFinished = pJob->m_iNextPrepareItem >= static_cast<INT_PTR>(pJob->m_ids.size()) || bStale;
		if (bFinished) {
			if (!bStale) {
				const uint32 nSearchID = pJob->m_nSearchID;
				pJob->m_uProcessed = 0;
				pJob->m_dwLastProgressTick = 0;
				QueuePreparedChunkedSpamRatingJob(pJob);
				{
					CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
					SetSpamRatingPrepareJobActive(nSearchID, false);
				}
			} else {
				{
					CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
					SetSpamRatingPrepareJobActive(pJob->m_nSearchID, false);
				}
				delete pJob;
			}
		} else {
			CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
			m_chunkedSpamRatingPrepareJobs.AddHead(pJob);
			SetSpamRatingPrepareJobActive(pJob->m_nSearchID, false);
		}

		if (uProcessedInSlice != 0 && theApp.IsTimeBudgetExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchIngest))
			break;
	}

	DWORD dwSliceElapsed = 0;
	if (theApp.IsTimeBudgetHardExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchIngest, &dwSliceElapsed)) {
		CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetSearchIngest, _T("ProcessChunkedSpamRatingPrepareJobsOnParserThread"), dwSliceElapsed, uProcessedInSlice, m_chunkedSpamRatingPrepareJobs.GetCount());
	}

	bool bHasMore = false;
	{
		CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
		bHasMore = !m_chunkedSpamRatingPrepareJobs.IsEmpty();
	}
	if (bHasMore)
		SignalSearchAnswerParseThread();
}

void CSearchList::ProcessChunkedSpamRatingJobs(const DWORD dwSliceStartTick, UINT& uProcessedInSlice)
{
	while (!m_chunkedSpamRatingJobs.IsEmpty() && !theApp.IsTimeBudgetExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchIngest)) {
		SChunkedSpamRatingJob *pJob = m_chunkedSpamRatingJobs.GetHead();
		if (pJob == NULL) {
			m_chunkedSpamRatingJobs.RemoveHead();
			continue;
		}

		if (IsChunkedSpamRatingJobStale(pJob)) {
			delete m_chunkedSpamRatingJobs.RemoveHead();
			continue;
		}

		SearchList *list = GetSearchListForID(pJob->m_nSearchID);
		if (list == NULL || list->GetCount() == 0) {
			delete m_chunkedSpamRatingJobs.RemoveHead();
			continue;
		}

		while (pJob->m_iNextItem < static_cast<INT_PTR>(pJob->m_ids.size())) {
			const size_t uCurrentIndex = static_cast<size_t>(pJob->m_iNextItem++);
			CSearchFile *pCurFile = GetSearchFileByResultId(pJob->m_ids[uCurrentIndex]);
			if (pCurFile != NULL) {
				if (pJob->m_ePhase == SChunkedSpamRatingJob::PhaseReset) {
					if (pCurFile->GetKnownType() == CSearchFile::NotDetermined) {
						pCurFile->SetAutomaticBlacklisted(false);
						pCurFile->ClearAutomaticBlacklistEvaluation();
						ApplyPreparedAutomaticBlacklistResult(*pJob, uCurrentIndex, pCurFile);
						pCurFile->SetSpamRating(0);
					}
				} else if (pJob->m_ePhase == SChunkedSpamRatingJob::PhaseParents) {
					ApplyPreparedAutomaticBlacklistResult(*pJob, uCurrentIndex, pCurFile);
					if (pCurFile->GetListParent() == NULL && pCurFile->GetKnownType() == CSearchFile::NotDetermined && (pJob->m_bRecalculateAll || !(pCurFile->IsConsideredSpam(false) && pJob->m_bExpectHigher) && !(!pCurFile->IsConsideredSpam(false) && pJob->m_bExpectLower)))
						DoSpamRating(pCurFile, false, Calculate, false, 0);
				} else if (pCurFile->GetListParent() != NULL && pCurFile->GetKnownType() == CSearchFile::NotDetermined) {
					ApplyPreparedAutomaticBlacklistResult(*pJob, uCurrentIndex, pCurFile);
					if (thePrefs.GetBlacklistAutomatic() && pCurFile->GetListParent()->GetAutomaticBlacklisted())
						pCurFile->SetAutomaticBlacklisted(true);
					else if (thePrefs.GetBlacklistManual() && pCurFile->GetListParent()->GetManualBlacklisted())
						pCurFile->SetManualBlacklisted(true);
					else if (thePrefs.IsSearchSpamFilterEnabled() && pCurFile->GetListParent()->IsConsideredSpam(false))
						pCurFile->SetSpamRating(pCurFile->GetListParent()->GetSpamRating());
					else
						DoSpamRating(pCurFile, false, Calculate, false, 0);
				}
			}

			++pJob->m_uProcessed;
			++uProcessedInSlice;

			const DWORD dwNow = ::GetTickCount();
			if (pJob->m_dwLastProgressTick == 0 || static_cast<DWORD>(dwNow - pJob->m_dwLastProgressTick) >= theApp.GetTimeBudgetedProgressTraceMs(CemuleApp::TimeBudgetSearchIngest)) {
				pJob->m_dwLastProgressTick = dwNow;
				AddDebugLogLine(DLP_VERYLOW, false, _T("Chunked spam rating recheck progress. search=%u phase=%u processed=%u remaining=%d\n"), pJob->m_nSearchID, static_cast<UINT>(pJob->m_ePhase), pJob->m_uProcessed, static_cast<int>(pJob->m_ids.size() - static_cast<size_t>(pJob->m_iNextItem)));
			}

			if (theApp.IsTimeBudgetExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchIngest))
				break;
		}

		if (pJob->m_iNextItem >= static_cast<INT_PTR>(pJob->m_ids.size())) {
			if (pJob->m_ePhase == SChunkedSpamRatingJob::PhaseReset) {
				pJob->m_ePhase = SChunkedSpamRatingJob::PhaseParents;
				pJob->m_iNextItem = 0;
			} else if (pJob->m_ePhase == SChunkedSpamRatingJob::PhaseParents && pJob->m_bRecalculateAll) {
				pJob->m_ePhase = SChunkedSpamRatingJob::PhaseChildren;
				pJob->m_iNextItem = 0;
			} else {
				const uint32 nSearchID = pJob->m_nSearchID;
				AddDebugLogLine(DLP_LOW, false, _T("Chunked spam rating recheck completed. search=%u processed=%u elapsed=%u\n"), nSearchID, pJob->m_uProcessed, static_cast<DWORD>(::GetTickCount() - pJob->m_dwStartedTick));
				delete m_chunkedSpamRatingJobs.RemoveHead();
				UpdateSearchIngestOutputWnd(nSearchID, EMPTY, false);
				theApp.QueueSearchActivityChangedEvent(nSearchID);
			}
		}

		if (uProcessedInSlice != 0 && theApp.IsTimeBudgetExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchIngest))
			break;
	}
}

void CSearchList::CancelSearchAnswerParseJobs(uint32 nSearchID)
{
	CSingleLock parseLock(&m_searchAnswerParseQueueLock, TRUE);
	LONG lSearchGeneration = 0;
	m_searchAnswerParseGenerations.Lookup(nSearchID, lSearchGeneration);
	m_searchAnswerParseGenerations[nSearchID] = lSearchGeneration + 1;
	m_cancelledSearchAnswerParseIds[nSearchID] = true;
	for (POSITION pos = m_chunkedSearchAnswerParseJobs.GetHeadPosition(); pos != NULL;) {
		POSITION posCurrent = pos;
		SChunkedSearchAnswerParseJob *pJob = m_chunkedSearchAnswerParseJobs.GetNext(pos);
		if (pJob != NULL && pJob->m_nSearchID == nSearchID) {
			m_chunkedSearchAnswerParseJobs.RemoveAt(posCurrent);
			delete pJob;
		}
	}
	for (POSITION pos = m_storedSearchIngestPrepareJobs.GetHeadPosition(); pos != NULL;) {
		POSITION posCurrent = pos;
		SStoredSearchIngestPrepareJob *pJob = m_storedSearchIngestPrepareJobs.GetNext(pos);
		if (pJob != NULL && pJob->m_nSearchID == nSearchID) {
			m_storedSearchIngestPrepareJobs.RemoveAt(posCurrent);
			delete pJob;
		}
	}
	for (POSITION pos = m_chunkedSpamRatingPrepareJobs.GetHeadPosition(); pos != NULL;) {
		POSITION posCurrent = pos;
		SChunkedSpamRatingJob *pJob = m_chunkedSpamRatingPrepareJobs.GetNext(pos);
		if (pJob != NULL && pJob->m_nSearchID == nSearchID) {
			m_chunkedSpamRatingPrepareJobs.RemoveAt(posCurrent);
			delete pJob;
		}
	}
	parseLock.Unlock();

	CSingleLock batchLock(&m_parsedSearchIngestBatchLock, TRUE);
	for (POSITION pos = m_parsedSearchIngestBatches.GetHeadPosition(); pos != NULL;) {
		POSITION posCurrent = pos;
		SParsedSearchIngestBatch *pBatch = m_parsedSearchIngestBatches.GetNext(pos);
		if (pBatch != NULL && pBatch->m_nSearchID == nSearchID) {
			m_parsedSearchIngestBatches.RemoveAt(posCurrent);
			delete pBatch;
		}
	}
	for (POSITION pos = m_preparedSpamRatingJobs.GetHeadPosition(); pos != NULL;) {
		POSITION posCurrent = pos;
		SChunkedSpamRatingJob *pJob = m_preparedSpamRatingJobs.GetNext(pos);
		if (pJob != NULL && pJob->m_nSearchID == nSearchID) {
			m_preparedSpamRatingJobs.RemoveAt(posCurrent);
			delete pJob;
		}
	}
}

bool CSearchList::IsSearchJobStale(uint32 nSearchID, LONG lGeneration, LONG lSearchGeneration)
{
	if (theApp.IsClosing())
		return true;
	const LONG lCurrentGeneration = ::InterlockedCompareExchange(&m_lSearchAnswerParseGeneration, 0, 0);
	if (lGeneration != lCurrentGeneration)
		return true;
	CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
	LONG lCurrentSearchGeneration = 0;
	m_searchAnswerParseGenerations.Lookup(nSearchID, lCurrentSearchGeneration);
	if (lSearchGeneration != lCurrentSearchGeneration)
		return true;
	bool bCancelled = false;
	return m_cancelledSearchAnswerParseIds.Lookup(nSearchID, bCancelled) && bCancelled;
}

bool CSearchList::IsSearchIngestJobStale(const SChunkedSearchIngestJob *pJob)
{
	return pJob == NULL || IsSearchJobStale(pJob->m_nSearchID, pJob->m_lGeneration, pJob->m_lSearchGeneration);
}

bool CSearchList::IsSearchAnswerParseJobStale(const SChunkedSearchAnswerParseJob *pJob)
{
	return pJob == NULL || IsSearchJobStale(pJob->m_nSearchID, pJob->m_lGeneration, pJob->m_lSearchGeneration);
}

bool CSearchList::IsStoredSearchIngestPrepareJobStale(const SStoredSearchIngestPrepareJob *pJob)
{
	return pJob == NULL || IsSearchJobStale(pJob->m_nSearchID, pJob->m_lGeneration, pJob->m_lSearchGeneration);
}

bool CSearchList::IsParsedSearchIngestBatchStale(const SParsedSearchIngestBatch *pBatch)
{
	return pBatch == NULL || IsSearchJobStale(pBatch->m_nSearchID, pBatch->m_lGeneration, pBatch->m_lSearchGeneration);
}

void CSearchList::QueueParsedSearchIngestBatch(SChunkedSearchAnswerParseJob &job, std::vector<SSearchIngestRecord> &records, bool bNotifyUiOnCompletion)
{
	if (!IsSearchProcessingAcceptingJobs() || IsSearchAnswerParseJobStale(&job) || (records.empty() && !bNotifyUiOnCompletion))
		return;

	SParsedSearchIngestBatch *pBatch = new SParsedSearchIngestBatch();
	pBatch->m_nSearchID = job.m_nSearchID;
	pBatch->m_strClientHash = job.m_strClientHash;
	pBatch->m_bClientResponse = job.m_bClientResponse;
	pBatch->m_dwFromUDPServerIP = 0;
	pBatch->m_bDoSpamRating = job.m_bDoSpamRating;
	pBatch->m_bUseKadReloadThrottle = job.m_bUseKadReloadThrottle;
	pBatch->m_bNotifyUiOnCompletion = bNotifyUiOnCompletion;
	pBatch->m_bNotifyLocalEd2kSearchEnd = job.m_bNotifyLocalEd2kSearchEnd && bNotifyUiOnCompletion;
	pBatch->m_bMoreResultsAvailable = job.m_bMoreResultsAvailable;
	pBatch->m_lGeneration = job.m_lGeneration;
	pBatch->m_lSearchGeneration = job.m_lSearchGeneration;
	pBatch->m_records.swap(records);
	{
		CSingleLock lock(&m_parsedSearchIngestBatchLock, TRUE);
		EnforceParsedSearchIngestBatchLimitLocked(pBatch->m_nSearchID);
		m_parsedSearchIngestBatches.AddTail(pBatch);
	}
	theApp.QueueSearchIngestProcessing();
}

void CSearchList::QueueParsedSearchIngestBatch(SStoredSearchIngestPrepareJob &job, std::vector<SSearchIngestRecord> &records, bool bNotifyUiOnCompletion)
{
	if (!IsSearchProcessingAcceptingJobs() || IsStoredSearchIngestPrepareJobStale(&job) || (records.empty() && !bNotifyUiOnCompletion))
		return;

	SParsedSearchIngestBatch *pBatch = new SParsedSearchIngestBatch();
	pBatch->m_nSearchID = job.m_nSearchID;
	pBatch->m_strClientHash = job.m_strClientHash;
	pBatch->m_bClientResponse = job.m_bClientResponse;
	pBatch->m_dwFromUDPServerIP = job.m_dwFromUDPServerIP;
	pBatch->m_bDoSpamRating = job.m_bDoSpamRating;
	pBatch->m_bUseKadReloadThrottle = job.m_bUseKadReloadThrottle;
	pBatch->m_bNotifyUiOnCompletion = bNotifyUiOnCompletion;
	pBatch->m_bNotifyLocalEd2kSearchEnd = false;
	pBatch->m_bMoreResultsAvailable = false;
	pBatch->m_lGeneration = job.m_lGeneration;
	pBatch->m_lSearchGeneration = job.m_lSearchGeneration;
	pBatch->m_records.swap(records);
	{
		CSingleLock lock(&m_parsedSearchIngestBatchLock, TRUE);
		EnforceParsedSearchIngestBatchLimitLocked(pBatch->m_nSearchID);
		m_parsedSearchIngestBatches.AddTail(pBatch);
	}
	theApp.QueueSearchIngestProcessing();
}

void CSearchList::DrainParsedSearchIngestBatches()
{
	for (;;) {
		SParsedSearchIngestBatch *pBatch = NULL;
		{
			CSingleLock lock(&m_parsedSearchIngestBatchLock, TRUE);
			if (m_parsedSearchIngestBatches.IsEmpty())
				break;
			pBatch = m_parsedSearchIngestBatches.RemoveHead();
		}
		if (pBatch == NULL)
			continue;

		if (IsParsedSearchIngestBatchStale(pBatch)) {
			AddDebugLogLine(DLP_LOW, false, _T("Parsed search ingest batch dropped because it is stale. search=%u generation=%ld searchGeneration=%ld\n"), pBatch->m_nSearchID, pBatch->m_lGeneration, pBatch->m_lSearchGeneration);
			delete pBatch;
			continue;
		}

		SChunkedSearchIngestJob *pIngestJob = new SChunkedSearchIngestJob();
		pIngestJob->m_nSearchID = pBatch->m_nSearchID;
		pIngestJob->m_strClientHash = pBatch->m_strClientHash;
		pIngestJob->m_bClientResponse = pBatch->m_bClientResponse;
		pIngestJob->m_dwFromUDPServerIP = pBatch->m_dwFromUDPServerIP;
		pIngestJob->m_bDoSpamRating = pBatch->m_bDoSpamRating;
		pIngestJob->m_bUseKadReloadThrottle = pBatch->m_bUseKadReloadThrottle;
		pIngestJob->m_bNotifyUiOnCompletion = pBatch->m_bNotifyUiOnCompletion;
		pIngestJob->m_bNotifyLocalEd2kSearchEnd = pBatch->m_bNotifyLocalEd2kSearchEnd;
		pIngestJob->m_bMoreResultsAvailable = pBatch->m_bMoreResultsAvailable;
		pIngestJob->m_lGeneration = pBatch->m_lGeneration;
		pIngestJob->m_lSearchGeneration = pBatch->m_lSearchGeneration;
		pIngestJob->m_records.swap(pBatch->m_records);
		EnforceSearchIngestQueueLimit(pIngestJob->m_nSearchID);
		m_chunkedSearchIngestJobs.AddTail(pIngestJob);
		delete pBatch;
	}
}

bool CSearchList::StartSearchAnswerParseThread()
{
	if (!IsSearchProcessingAcceptingJobs())
		return false;
	return theApp.StartNetworkParseCpuWorker();
}

void CSearchList::StopSearchAnswerParseThread()
{
	::InterlockedIncrement(&m_lSearchAnswerParseGeneration);
	{
		CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
		while (!m_chunkedSearchAnswerParseJobs.IsEmpty())
			delete m_chunkedSearchAnswerParseJobs.RemoveHead();
		while (!m_storedSearchIngestPrepareJobs.IsEmpty())
			delete m_storedSearchIngestPrepareJobs.RemoveHead();
		while (!m_chunkedSpamRatingPrepareJobs.IsEmpty())
			delete m_chunkedSpamRatingPrepareJobs.RemoveHead();
	}
}

bool CSearchList::SignalSearchAnswerParseThread()
{
	if (!IsSearchProcessingAcceptingJobs())
		return false;

	CemuleApp::SWorkerTopologyItem item;
	item.m_eRole = CemuleApp::WorkerTopologyNetworkParseCpu;
	item.m_eType = CemuleApp::WorkerTopologyItemNetworkParseCpu;
	item.m_strStage = _T("search-answer-parse");
	item.m_strCoalesceKey = _T("search-answer-parse");
	item.m_dwCreatedTick = ::GetTickCount();
	item.m_dwDueTick = item.m_dwCreatedTick;
	return theApp.QueueNetworkParseCpuWorkerItem(item);
}

void CSearchList::ProcessNetworkParseCpuWorkerJobs()
{
	ProcessSearchAnswerParseJobsOnParserThread();
	ProcessStoredSearchIngestPrepareJobsOnParserThread();
	ProcessChunkedSpamRatingPrepareJobsOnParserThread();
}


void CSearchList::UpdateSearchIngestOutputWnd(uint32 nSearchID, const CString &strClientHash, bool bUseKadReloadThrottle)
{
	if (!theApp.IsUiThread()) {
		theApp.QueueSearchResultsChangedEvent(nSearchID, strClientHash, bUseKadReloadThrottle);
		return;
	}

	UpdateSearchIngestOutputWndFromUiThread(nSearchID, strClientHash, bUseKadReloadThrottle);
}

void CSearchList::UpdateSearchIngestOutputWndFromUiThread(uint32 nSearchID, const CString &strClientHash, bool bUseKadReloadThrottle)
{
	if (outputwnd == NULL || theApp.emuledlg == NULL || theApp.emuledlg->searchwnd == NULL || theApp.emuledlg->searchwnd->m_pwndResults == NULL)
		return;

	if (nSearchID == theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.m_nResultsID) {
		if (bUseKadReloadThrottle) {
			const DWORD dwCurrentTick = ::GetTickCount();
			if (dwCurrentTick < m_dwKadLastReloadTick + KADEMLIASEARCHLISTRELOADELAY) {
				m_bKadReloadWaiting = true;
				return;
			}
			m_bKadReloadWaiting = false;
			m_dwKadLastReloadTick = dwCurrentTick;
		}
		outputwnd->QueueDeferredReload(false, LSF_SELECTION, bUseKadReloadThrottle ? KADEMLIASEARCHLISTRELOADELAY : kSearchIngestDeferredReloadDelayMs);
	} else
		outputwnd->UpdateTabHeader(nSearchID, strClientHash, false);
}


void CSearchList::Clear()
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::Clear"));
	if (!mutationLock)
		return;
	CancelStartupLoad();
	ClearChunkedSearchIngestJobs();
	TouchSearchModelSequence();

	for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
		POSITION posLast = pos;
		SearchListsStruct *listCur = m_listFileLists.GetNext(pos);
		while (!listCur->m_listSearchFiles.IsEmpty())
			delete listCur->m_listSearchFiles.RemoveHead();
		DeleteChildLists(listCur);
		m_listFileLists.RemoveAt(posLast);
		delete listCur;
	}
}

void CSearchList::RemoveResults(uint32 nSearchID)
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::RemoveResults"));
	if (!mutationLock)
		return;
	CancelChunkedSearchIngestJobs(nSearchID);
	CancelChunkedSpamRatingJobs(nSearchID);

	// this will not delete the item from the window, make sure your code does it if you call this
	for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
		POSITION posLast = pos;
		SearchListsStruct *listCur = m_listFileLists.GetNext(pos);
		if (listCur->m_nSearchID == nSearchID) {
			while (!listCur->m_listSearchFiles.IsEmpty())
				delete listCur->m_listSearchFiles.RemoveHead();
			DeleteChildLists(listCur);
			m_listFileLists.RemoveAt(posLast);
			delete listCur;
			m_foundFilesCount.RemoveKey(nSearchID);
			m_originalFoundFilesCount.RemoveKey(nSearchID);
			m_foundSourcesCount.RemoveKey(nSearchID);
			m_ReceivedUDPAnswersCount.RemoveKey(nSearchID);
			m_RequestedUDPAnswersCount.RemoveKey(nSearchID);
			m_mergedSearchHistory.RemoveKey(nSearchID);
			TouchSearchModelSequence();
			return;
		}
	}
}

void CSearchList::RemoveResult(CSearchFile* todel)
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::RemoveResult"));
	if (!mutationLock)
		return;

	if (!todel)
		return;

	SearchListsStruct* listStruct = GetSearchListStructForID(todel->GetSearchID(), false);
	if (!listStruct)
		return;
	SearchList* list = &listStruct->m_listSearchFiles;

	POSITION posParent = list->Find(todel);
	if (!posParent)
		return;

	// SearchFile can be parent item with children (They all have same hashes). When we delete SearchFile we should delete all of these copies.
	if (!todel->GetListParent()) {
		POSITION pos = list->GetHeadPosition();
		while (pos) {
			POSITION posCur = pos;
			CSearchFile* pCurFile = list->GetNext(pos);
			if (pCurFile && pCurFile != todel && md4equ(pCurFile->GetFileHash(), todel->GetFileHash())) {
				list->RemoveAt(posCur); // Remove parent and child items except the one we want to delete
				delete pCurFile;
			}
		}
	}

	list->RemoveAt(posParent); // Remove item
	delete todel;
	RebuildSearchListIndexes(listStruct);
	TouchSearchModelSequence();
}

uint32 CSearchList::RemoveCleanUpSearchResults(uint32 nSearchID, bool *pbRemovedAny)
{
	if (pbRemovedAny != NULL)
		*pbRemovedAny = false;

	CSearchModelMutationLock mutationLock(this, _T("CSearchList::RemoveCleanUpSearchResults"));
	if (!mutationLock)
		return 0;

	SearchListsStruct* listStruct = GetSearchListStructForID(nSearchID, false);
	if (listStruct == NULL)
		return 0;

	CMap<CSKey, const CSKey&, BYTE, BYTE> mapParentHashesToRemove;
	uint32 uDeletedParents = 0;
	bool bHasRemovals = false;
	for (POSITION pos = listStruct->m_listSearchFiles.GetHeadPosition(); pos != NULL;) {
		CSearchFile* pFile = listStruct->m_listSearchFiles.GetNext(pos);
		if (!ShouldCleanUpSearchResultFile(pFile))
			continue;
		bHasRemovals = true;
		if (pFile->GetListParent() == NULL) {
			mapParentHashesToRemove.SetAt(CSKey(pFile->GetFileHash()), static_cast<BYTE>(1));
			++uDeletedParents;
		}
	}

	if (!bHasRemovals)
		return 0;

	bool bRemovedAny = false;
	for (POSITION pos = listStruct->m_listSearchFiles.GetHeadPosition(); pos != NULL;) {
		POSITION posCur = pos;
		CSearchFile* pFile = listStruct->m_listSearchFiles.GetNext(pos);
		if (pFile == NULL)
			continue;

		BYTE byDummy = 0;
		if (!mapParentHashesToRemove.Lookup(CSKey(pFile->GetFileHash()), byDummy) && !ShouldCleanUpSearchResultFile(pFile))
			continue;

		listStruct->m_listSearchFiles.RemoveAt(posCur);
		delete pFile;
		bRemovedAny = true;
	}

	if (bRemovedAny) {
		RebuildSearchListIndexes(listStruct);
		TouchSearchModelSequence();
		if (pbRemovedAny != NULL)
			*pbRemovedAny = true;
	}

	return uDeletedParents;
}

void CSearchList::NewSearch(CSearchListCtrl *pWnd, const CString &strResultFileType, SSearchParams *pParams)
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::NewSearch"));
	if (!mutationLock) {
		delete pParams;
		return;
	}

	if (pWnd)
		outputwnd = pWnd;

	m_strResultFileType = strResultFileType;
	ASSERT(pParams->eType != SearchTypeAutomatic);
	if (pParams->eType == SearchTypeEd2kServer || pParams->eType == SearchTypeEd2kGlobal) {
		m_nCurED2KSearchID = pParams->dwSearchID;
		m_aCurED2KSentRequestsIPs.RemoveAll();
		m_aCurED2KSentReceivedIPs.RemoveAll();
	}
	m_foundFilesCount[pParams->dwSearchID] = 0;
	m_originalFoundFilesCount[pParams->dwSearchID] = 0;
	m_foundSourcesCount[pParams->dwSearchID] = 0;
	m_ReceivedUDPAnswersCount[pParams->dwSearchID] = 0;
	m_RequestedUDPAnswersCount[pParams->dwSearchID] = 0;
	m_mergedSearchHistory[pParams->dwSearchID] = false;
	TouchSearchModelSequence();

	if (pParams->strBooleanExpr.IsEmpty())
		pParams->strBooleanExpr = pParams->strExpression;

	// convert the expression into an array of search keywords which the user has typed in
	// this is used for the spam filter later and not at all semantically equal to
	// the actual search expression any more
	m_astrSpamCheckCurSearchExp.RemoveAll();
	CString sExpr(pParams->strExpression);
	if (_tcsncmp(sExpr.MakeLower(), _T("related:"), 8) != 0) { // ignore special searches
		int nPos, nPos2;
		while ((nPos = sExpr.Find(_T('"'))) >= 0 && (nPos2 = sExpr.Find(_T('"'), nPos + 1)) >= 0) {
			const CString& strQuoted(sExpr.Mid(nPos + 1, (nPos2 - nPos) - 1));
			m_astrSpamCheckCurSearchExp.Add(strQuoted);
			sExpr.Delete(nPos, (nPos2 - nPos) + 1);
		}
		for (int iPos = 0; iPos >= 0;) {
			const CString& sToken(sExpr.Tokenize(_T(".[]()!-'_ "), iPos));
			if (!sToken.IsEmpty() && sToken != "and" && sToken != "or" && sToken != "not")
				m_astrSpamCheckCurSearchExp.Add(sToken);
		}
	}
}

UINT CSearchList::QueueClientSearchAnswerPacket(const uchar *in_packet, uint32 size, CUpDownClient &sender, LPCTSTR pszDirectory)
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::QueueClientSearchAnswerPacket"));
	if (!mutationLock)
		return 0;

	uint32 uSearchID = sender.GetSearchID();
	if (!uSearchID) {
		if (theApp.emuledlg == NULL || theApp.emuledlg->searchwnd == NULL || theApp.emuledlg->searchwnd->m_pwndResults == NULL)
			return 0;
		uSearchID = theApp.emuledlg->searchwnd->m_pwndResults->GetNextSearchID();
		sender.SetSearchID(uSearchID);
	}

	std::vector<BYTE> packetData;
	if (in_packet != NULL && size != 0) {
		packetData.resize(size);
		memcpy(&packetData[0], in_packet, size);
	}

	return QueueClientSearchAnswerPacketSnapshot(packetData, uSearchID, GetSearchAnswerParseGeneration(uSearchID), md4str(sender.GetUserHash()), sender.GetUserName(),
		sender.GetIP().ToUInt32(false), sender.GetUserPort(), sender.GetServerIP(), sender.GetServerPort(), sender.GetUnicodeSupport() != UTF8strNone, sender.GetPreviewSupport(), sender.SupportsLargeFiles(), pszDirectory);
}

UINT CSearchList::QueueClientSearchAnswerPacketSnapshot(const std::vector<BYTE> &packetData, uint32 uSearchID, LONG lSearchGeneration, const CString &strClientHash, const CString &strSenderName,
	uint32 nClientID, uint16 nClientPort, uint32 nClientServerIP, uint16 nClientServerPort, bool bOptUTF8, bool bPreviewSupport, bool bSupportsLargeFiles, LPCTSTR pszDirectory)
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::QueueClientSearchAnswerPacketSnapshot"));
	if (!mutationLock)
		return 0;

	if (uSearchID == 0)
		return 0;
	if (lSearchGeneration != GetSearchAnswerParseGeneration(uSearchID))
		return 0;

	if (theApp.IsUiThread()) {
		SSearchParams *pParams = new SSearchParams;
		pParams->strExpression = strSenderName;
		if (pParams->strExpression.IsEmpty())
			pParams->strExpression = strClientHash;
		pParams->dwSearchID = uSearchID;
		pParams->bClientSharedFiles = true;
		pParams->m_strClientHash = strClientHash;
		if (theApp.emuledlg != NULL && theApp.emuledlg->searchwnd != NULL && theApp.emuledlg->searchwnd->CreateNewTab(pParams)) {
			m_foundFilesCount[uSearchID] = 0;
			m_originalFoundFilesCount[uSearchID] = 0;
			m_foundSourcesCount[uSearchID] = 0;
			m_mergedSearchHistory[uSearchID] = false;
		} else
			delete pParams;
	}

	if (packetData.size() < sizeof(uint32)) {
		theApp.QueueSearchPacketParseEvent(CemuleApp::ApplicationEventSearchPacketParseFailed, uSearchID, 0, 1, 0, _T("empty-client-search-answer"));
		return GetResultCount(uSearchID);
	}

	CSafeMemFile packet(const_cast<BYTE*>(&packetData[0]), static_cast<UINT>(packetData.size()));
	UINT uResultCount = 0;
	try {
		uResultCount = packet.ReadUInt32();
	} catch (CException *ex) {
		ex->Delete();
		theApp.QueueSearchPacketParseEvent(CemuleApp::ApplicationEventSearchPacketParseFailed, uSearchID, 0, 1, 0, _T("invalid-client-search-answer-header"));
		return GetResultCount(uSearchID);
	}

	SChunkedSearchAnswerParseJob *pJob = new SChunkedSearchAnswerParseJob();
	pJob->m_uResultCount = uResultCount;
	pJob->m_uPacketPosition = packet.GetPosition();
	pJob->m_nSearchID = uSearchID;
	pJob->m_strClientHash = strClientHash;
	pJob->m_strSenderName = strSenderName;
	pJob->m_strDirectory = pszDirectory != NULL ? pszDirectory : _T("");
	pJob->m_nClientID = nClientID;
	pJob->m_nClientPort = nClientPort;
	pJob->m_nClientServerIP = nClientServerIP;
	pJob->m_nClientServerPort = nClientServerPort;
	pJob->m_bOptUTF8 = bOptUTF8;
	pJob->m_bClientResponse = true;
	pJob->m_bPreviewSupport = bPreviewSupport;
	pJob->m_bSupportsLargeFiles = bSupportsLargeFiles;
	pJob->m_bDoSpamRating = true;
	pJob->m_bUseKadReloadThrottle = false;
	pJob->m_lGeneration = ::InterlockedCompareExchange(&m_lSearchAnswerParseGeneration, 0, 0);
	pJob->m_lSearchGeneration = lSearchGeneration;
	pJob->m_packet = packetData;
	QueueChunkedSearchAnswerParseJob(pJob);

	return GetResultCount(uSearchID);
}

UINT CSearchList::QueueServerSearchAnswerPacketSnapshot(const std::vector<BYTE> &packetData, uint32 uSearchID, LONG lSearchGeneration, bool bOptUTF8, uint32 nServerIP, uint16 nServerPort)
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::QueueServerSearchAnswerPacketSnapshot"));
	if (!mutationLock)
		return 0;

	if (uSearchID == 0 || lSearchGeneration != GetSearchAnswerParseGeneration(uSearchID))
		return GetResultCount(uSearchID);

	if (packetData.size() < sizeof(uint32)) {
		theApp.QueueSearchPacketParseEvent(CemuleApp::ApplicationEventSearchPacketParseFailed, uSearchID, 0, 1, 0, _T("empty-server-search-answer"));
		return GetResultCount(uSearchID);
	}

	CSafeMemFile packet(const_cast<BYTE*>(&packetData[0]), static_cast<UINT>(packetData.size()));
	UINT uResultCount = 0;
	try {
		uResultCount = packet.ReadUInt32();
	} catch (CException *ex) {
		ex->Delete();
		theApp.QueueSearchPacketParseEvent(CemuleApp::ApplicationEventSearchPacketParseFailed, uSearchID, 0, 1, 0, _T("invalid-server-search-answer-header"));
		return GetResultCount(uSearchID);
	}

	SChunkedSearchAnswerParseJob *pJob = new SChunkedSearchAnswerParseJob();
	pJob->m_uResultCount = uResultCount;
	pJob->m_uPacketPosition = packet.GetPosition();
	pJob->m_nSearchID = uSearchID;
	pJob->m_nClientServerIP = nServerIP;
	pJob->m_nClientServerPort = nServerPort;
	pJob->m_bOptUTF8 = bOptUTF8;
	pJob->m_bClientResponse = false;
	pJob->m_bPreviewSupport = false;
	pJob->m_bSupportsLargeFiles = true;
	pJob->m_bDoSpamRating = true;
	pJob->m_bUseKadReloadThrottle = false;
	pJob->m_bNotifyLocalEd2kSearchEnd = true;
	pJob->m_lGeneration = ::InterlockedCompareExchange(&m_lSearchAnswerParseGeneration, 0, 0);
	pJob->m_lSearchGeneration = lSearchGeneration;
	pJob->m_packet = packetData;
	QueueChunkedSearchAnswerParseJob(pJob);
	return GetResultCount(uSearchID);
}


UINT CSearchList::ProcessSearchAnswer(const uchar *in_packet, uint32 size
	, CUpDownClient &sender, bool *pbMoreResultsAvailable, LPCTSTR pszDirectory)
{
	if (pbMoreResultsAvailable != NULL)
		*pbMoreResultsAvailable = false;
	if (theApp.QueueClientSearchAnswerNetworkCommand(reinterpret_cast<const BYTE*>(in_packet), size, sender, pszDirectory))
		return GetResultCount(sender.GetSearchID());
	return QueueClientSearchAnswerPacket(in_packet, size, sender, pszDirectory);
}


UINT CSearchList::ProcessSearchAnswer(const uchar *in_packet, uint32 size, bool bOptUTF8
	, uint32 nServerIP, uint16 nServerPort, bool *pbMoreResultsAvailable)
{
	CSafeMemFile packet(in_packet, size);
	std::vector<SSearchIngestRecord> pendingRecords;
	const uint32 uResultCount = packet.ReadUInt32();
	TraceLegacyUiNetworkParse(_T("CSearchList::ProcessSearchAnswer(ServerTCP)"), size, uResultCount);
	for (uint32 i = uResultCount; i > 0; --i) {
		CSearchFile *toadd = new CSearchFile(packet, bOptUTF8, m_nCurED2KSearchID);
		toadd->SetClientServerIP(nServerIP);
		toadd->SetClientServerPort(nServerPort);
		if (nServerIP && nServerPort) {
			CSearchFile::SServer server(nServerIP, nServerPort, false);
			server.m_uAvail = toadd->GetIntTagValue(FT_SOURCES);
			toadd->AddServer(server);
		}
		SSearchIngestRecord record;
		if (BuildSearchIngestRecord(toadd, record))
			pendingRecords.push_back(record);
		delete toadd;
	}
	QueueChunkedSearchIngestJob(pendingRecords, m_nCurED2KSearchID, EMPTY, false, 0, true, false);

	if (pbMoreResultsAvailable)
		*pbMoreResultsAvailable = false;
	int iAddData = (int)(packet.GetLength() - packet.GetPosition());
	if (iAddData == 1) {
		uint8 ucMore = packet.ReadUInt8();
		if (ucMore == 0x00 || ucMore == 0x01) {
			if (pbMoreResultsAvailable)
				*pbMoreResultsAvailable = ucMore != 0;
			if (thePrefs.GetDebugServerTCPLevel() > 0)
				Debug(_T("  Search answer(Server %s:%u): More=%u\n"), (LPCTSTR)ipstr(nServerIP), nServerPort, ucMore);
		} else if (thePrefs.GetDebugServerTCPLevel() > 0)
			Debug(_T("*** NOTE: ProcessSearchAnswer(Server %s:%u): ***AddData: 1 byte: 0x%02x\n"), (LPCTSTR)ipstr(nServerIP), nServerPort, ucMore);
	} else if (iAddData > 0) {
		if (thePrefs.GetDebugServerTCPLevel() > 0) {
			Debug(_T("*** NOTE: ProcessSearchAnswer(Server %s:%u): ***AddData: %u bytes\n"), (LPCTSTR)ipstr(nServerIP), nServerPort, iAddData);
			DebugHexDump(in_packet + packet.GetPosition(), iAddData);
		}
	}

	packet.Close();
	return GetED2KResultCount();
}

UINT CSearchList::ProcessUDPSearchAnswer(CFileDataIO &packet, bool bOptUTF8, uint32 nServerIP, uint16 nServerPort)
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::ProcessUDPSearchAnswer"));
	if (!mutationLock)
		return 0;

	TraceLegacyUiNetworkParse(_T("CSearchList::ProcessUDPSearchAnswer"), 0, 1);
	CSearchFile *toadd = new CSearchFile(packet, bOptUTF8, m_nCurED2KSearchID, nServerIP, nServerPort, NULL, false, true);

	bool bFound = false;
	for (INT_PTR i = m_aCurED2KSentRequestsIPs.GetCount(); --i >= 0;)
		if (m_aCurED2KSentRequestsIPs[i] == nServerIP) {
			bFound = true;
			break;
		}

	if (!bFound) {
		DebugLogError(_T("Unrequested or delayed Server UDP Searchresult received from IP %s, ignoring"), (LPCTSTR)ipstr(nServerIP));
		delete toadd;
		return 0;
	}

	bool bNewResponse = true;
	for (INT_PTR i = m_aCurED2KSentReceivedIPs.GetCount(); --i >= 0;)
		if (m_aCurED2KSentReceivedIPs[i] == nServerIP) {
			bNewResponse = false;
			break;
		}

	if (bNewResponse) {
		uint32 nResponses;
		if (!m_ReceivedUDPAnswersCount.Lookup(m_nCurED2KSearchID, nResponses))
			nResponses = 0;
		m_ReceivedUDPAnswersCount[m_nCurED2KSearchID] = nResponses + 1;
		m_aCurED2KSentReceivedIPs.Add(nServerIP);
	}

	const CUDPServerRecordMap::CPair *pair = m_mUDPServerRecords.PLookup(nServerIP);
	if (pair)
		++pair->value->m_nResults;
	else {
		UDPServerRecord *pRecord = new UDPServerRecord;
		pRecord->m_nResults = 1;
		pRecord->m_nSpamResults = 0;
		m_mUDPServerRecords[nServerIP] = pRecord;
	}

	if (!QueueSearchFileForIngest(toadd, EMPTY, false, nServerIP, true, true))
		AddDebugLogLine(DLP_LOW, false, _T("Server UDP search ingest record was dropped. search=%u server=%s:%u\n"), m_nCurED2KSearchID, (LPCTSTR)ipstr(nServerIP), nServerPort);

	return GetED2KResultCount();
}

UINT CSearchList::GetResultCount(uint32 nSearchID) const
{
	UINT nSources;
	return m_foundSourcesCount.Lookup(nSearchID, nSources) ? nSources : 0;
}

UINT CSearchList::GetED2KResultCount() const
{
	return GetResultCount(m_nCurED2KSearchID);
}

void CSearchList::GetWebList(CQArray<SearchFileStruct, SearchFileStruct> *SearchFileArray, int iSortBy, INT_PTR iMaxRows) const
{
	for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
		SearchListsStruct *listCur = m_listFileLists.GetNext(pos);
		for (POSITION pos2 = listCur->m_listSearchFiles.GetHeadPosition(); pos2 != NULL;) {
			const CSearchFile *pFile = listCur->m_listSearchFiles.GetNext(pos2);
			if (pFile == NULL || pFile->GetListParent() != NULL || !(uint64)pFile->GetFileSize() || pFile->GetFileName().IsEmpty())
				continue;

			SearchFileStruct structFile;
			structFile.m_strFileName = pFile->GetFileName();
			structFile.m_strFileType = pFile->GetFileTypeDisplayStr();
			structFile.m_strFileHash = md4str(pFile->GetFileHash());
			structFile.m_uSourceCount = pFile->GetSourceCount();
			structFile.m_dwCompleteSourceCount = pFile->GetCompleteSourceCount();
			structFile.m_uFileSize = pFile->GetFileSize();

			switch (iSortBy) {
			case 0:
				structFile.m_strIndex = structFile.m_strFileName;
				break;
			case 1:
				structFile.m_strIndex.Format(_T("%10I64u"), structFile.m_uFileSize);
				break;
			case 2:
				structFile.m_strIndex = structFile.m_strFileHash;
				break;
			case 3:
				structFile.m_strIndex.Format(_T("%09u"), structFile.m_uSourceCount);
				break;
			case 4:
				structFile.m_strIndex = structFile.m_strFileType;
				break;
			default:
				structFile.m_strIndex.Empty();
			}
			SearchFileArray->Add(structFile);
			if (iMaxRows > 0 && SearchFileArray->GetCount() >= iMaxRows)
				return;
		}
	}
}

void CSearchList::GetWebDownloadLinksByHashes(const CString &strHashes, CString &strLinks) const
{
	strLinks.Empty();
	CString strRemaining(strHashes);
	for (int iPos = 0; iPos >= 0;) {
		CString strHash(strRemaining.Tokenize(_T("|"), iPos));
		strHash.Trim();
		if (strHash.GetLength() != 32)
			continue;

		uchar hash[MDX_DIGEST_SIZE];
		if (!DecodeBase16(strHash, strHash.GetLength(), hash, _countof(hash)))
			continue;

		for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
			const SearchListsStruct *listCur = m_listFileLists.GetNext(pos);
			CSearchFile *pFile = NULL;
			if (!listCur->m_mapParentsByHash.Lookup(CSKey(hash), pFile) || pFile == NULL || !(uint64)pFile->GetFileSize() || pFile->GetFileName().IsEmpty())
				continue;

			CString strLink;
			strLink.Format(_T("ed2k://|file|%s|%I64u|%s|/"), (LPCTSTR)EncodeUrlUtf8(pFile->GetFileName()), (uint64)pFile->GetFileSize(), (LPCTSTR)md4str(pFile->GetFileHash()));
			if (!strLinks.IsEmpty())
				strLinks += _T("\r\n");
			strLinks += strLink;
			break;
		}
	}
}

void CSearchList::AddFileToDownloadByHash(const uchar *hash, int cat)
{
	for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
		const SearchListsStruct *listCur = m_listFileLists.GetNext(pos);
		CSearchFile *sf = NULL;
		if (listCur->m_mapParentsByHash.Lookup(CSKey(hash), sf) && sf != NULL) {
			theApp.downloadqueue->AddSearchToDownload(sf, 2, cat);
			return;
		}
	}
}

bool CSearchList::AddToList(CSearchFile* toadd, bool bClientResponse, uint32 dwFromUDPServerIP, bool bDoSpamRating)
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::AddToList"));
	if (!mutationLock) {
		delete toadd;
		return false;
	}


	if (!bClientResponse && !m_strResultFileType.IsEmpty() && m_strResultFileType != toadd->GetFileType()) {
		delete toadd;
		return false;
	}
	SearchListsStruct* listStruct = GetSearchListStructForID(toadd->GetSearchID(), true);
	if (!listStruct) {
		delete toadd;
		return false;
	}
	SearchList *list = &listStruct->m_listSearchFiles;

	// Spam filter: Calculate the filename without any used keywords (and separators) for later use
	CString strNameWithoutKeyword;
	CString strName(toadd->GetFileName());
	strName.MakeLower();
	strNameWithoutKeyword.GetBuffer(strName.GetLength());
	strNameWithoutKeyword.ReleaseBuffer(0);
	const LPCTSTR pszName = strName;
	const int nNameLength = strName.GetLength();

	for (int iPos = 0; iPos < nNameLength;) {
		while (iPos < nNameLength && _tcschr(_T(".[]()!-'_ "), pszName[iPos]) != NULL)
			++iPos;
		if (iPos >= nNameLength)
			break;

		const int nTokenStart = iPos;
		while (iPos < nNameLength && _tcschr(_T(".[]()!-'_ "), pszName[iPos]) == NULL)
			++iPos;
		const int nTokenLength = iPos - nTokenStart;
		if (nTokenLength <= 0)
			continue;

		bool bFound = false;
		if (!bClientResponse && toadd->GetSearchID() == m_nCurED2KSearchID) {
			for (INT_PTR i = m_astrSpamCheckCurSearchExp.GetCount(); --i >= 0;) {
				const CString& strSpamToken = m_astrSpamCheckCurSearchExp[i];
				if (strSpamToken.GetLength() == nTokenLength && _tcsncmp((LPCTSTR)strSpamToken, pszName + nTokenStart, nTokenLength) == 0) {
					bFound = true;
					break;
				}
			}
		}
		if (!bFound) {
			if (!strNameWithoutKeyword.IsEmpty())
				strNameWithoutKeyword.AppendChar(_T(' '));
			strNameWithoutKeyword.Append(pszName + nTokenStart, nTokenLength);
		}
	}
	toadd->SetNameWithoutKeyword(strNameWithoutKeyword);

	// search for a 'parent' with same file hash and search-id as the new search result entry
	CSearchFile *parent = NULL;
	if (listStruct->m_mapParentsByHash.Lookup(CSKey(toadd->GetFileHash()), parent) && parent != NULL) {
			// if this parent does not have any child entries yet, create one child entry
			// which is equal to the current parent entry (needed for GUI when expanding the child list).
			if (!parent->GetListChildCount()) {
				CSearchFile *child = new CSearchFile(parent);
				child->m_bNowrite = true; // will not save
				child->SetListParent(parent);
				int iSources = parent->GetSourceCount();
				if (iSources == 0)
					iSources = 1;
				child->SetListChildCount(iSources);
				list->AddTail(child);
				AddChildToIndex(listStruct, child);
				parent->SetListChildCount(1);
			}

			// get the 'Availability' of the new search result entry
			uint32 uAvail = toadd->GetSourceCount();
			if (bClientResponse && !uAvail)
				// If this is a response from a client ("View Shared Files"), we set the "Availability" at least to 1.
				uAvail = 1;

			// get 'Complete Sources' of the new search result entry
			uint32 uCompleteSources = toadd->GetCompleteSourceCount();

			bool bFound = false;
			if (thePrefs.GetDebugSearchResultDetailLevel() >= 1)
				; // for debugging: do not merge search results
			else {
				// check if that parent already has a child with same filename as the new search result entry
				CString strChildKey = GetChildIndexKey(parent, toadd->GetFileName());
				void* pChildValue = NULL;
				if (listStruct->m_mapChildrenByParentAndName.Lookup(strChildKey, pChildValue)) {
					CSearchFile *child = static_cast<CSearchFile*>(pChildValue);
					if (child != NULL && child != toadd && child->GetListParent() == parent && toadd->GetFileName().CompareNoCase(child->GetFileName()) == 0) {
						bFound = true;

						// add properties of new search result entry to the already available child entry (with same filename)
						// ed2k: use the sum of all values, kad: use the max. values
						if (toadd->IsKademlia()) {
							if (uAvail > child->GetListChildCount())
								child->SetListChildCount(uAvail);
						} else
							child->AddListChildCount(uAvail);

						child->AddSources(uAvail);
						child->AddCompleteSources(uCompleteSources);

						// Check AICH Hash - if they differ, clear it (see KademliaSearchKeyword)
						//					 if we don't have a hash yet, take it over
						if (toadd->GetFileIdentifier().HasAICHHash()) {
							if (child->GetFileIdentifier().HasAICHHash()) {
								if (child->GetFileIdentifier().GetAICHHash() != toadd->GetFileIdentifier().GetAICHHash()) {
									DEBUG_ONLY(DebugLogWarning(_T("Kad: SearchList: AddToList: Received searchresult with different AICH hash than existing one, ignoring AICH for result %s"), (LPCTSTR)EscPercent(child->GetFileName())));
									child->SetFoundMultipleAICH();
									child->GetFileIdentifier().ClearAICHHash();
								}
							} else if (!child->HasFoundMultipleAICH()) {
								DEBUG_ONLY(DebugLog(_T("Kad: SearchList: AddToList: Received searchresult with new AICH hash %s, taking over to existing result. Entry: %s"), (LPCTSTR)toadd->GetFileIdentifier().GetAICHHash().GetString(), (LPCTSTR)EscPercent(child->GetFileName())));
								child->GetFileIdentifier().SetAICHHash(toadd->GetFileIdentifier().GetAICHHash());
							}
						}
					}
				}
			}
			if (!bFound) {
				// the parent which we had found does not yet have a child with that new search result's entry name,
				// add the new entry as a new child
				//
				toadd->SetListParent(parent);
				toadd->SetListChildCount(uAvail);
				parent->AddListChildCount(1);
				list->AddTail(toadd);
				AddChildToIndex(listStruct, toadd);
			}

			// copy possible available sources from new search result entry to parent
			if (IsValidSearchResultClientIPPort(toadd->GetClientID(), toadd->GetClientPort())) {
				// pre-filter sources which would be dropped in CPartFile::AddSources
				if (CPartFile::CanAddSource(toadd->GetClientID(), toadd->GetClientPort(), toadd->GetClientServerIP(), toadd->GetClientServerPort())) {
					CSearchFile::SClient client(toadd->GetClientID(), toadd->GetClientPort(), toadd->GetClientServerIP(), toadd->GetClientServerPort());
					if (parent->GetClients().Find(client) < 0)
						parent->AddClient(client);
				}
			} else if (thePrefs.GetDebugServerSearchesLevel() > 1)
				Debug(_T("Filtered source from search result %s:%u\n"), (LPCTSTR)DbgGetClientID(toadd->GetClientID()), toadd->GetClientPort());

			// copy possible available servers from new search result entry to parent
			// will be used in future
			if (toadd->GetClientServerIP() && toadd->GetClientServerPort()) {
				CSearchFile::SServer server(toadd->GetClientServerIP(), toadd->GetClientServerPort(), toadd->IsServerUDPAnswer());
				int iFound = parent->GetServers().Find(server);
				if (iFound == -1) {
					server.m_uAvail = uAvail;
					parent->AddServer(server);
				} else
					parent->GetServerAt(iFound).m_uAvail += uAvail;
			}

			RecalculateParentFromChildren(listStruct, parent);

			// Calculate known state for stored results even when spam rating is deferred or disabled.
			if (!bFound && !bDoSpamRating && toadd->GetKnownType() == CSearchFile::NotDetermined)
				SetSearchItemKnownType(toadd);

			// Calculate spam rating skipping duplicates. Don't calculate spam rating when we are just merging tabs.
			if (!bFound && bDoSpamRating)
				DoSpamRating(toadd, bClientResponse, Calculate, false, dwFromUDPServerIP);

			// add the 'Availability' of the new search result entry to the total search result count for this search
			AddResultCount(parent->GetSearchID(), parent->GetFileHash(), uAvail, parent->IsConsideredSpam());

			TouchSearchModelSequence();

			// update parent in GUI
			if (outputwnd && !m_bDeferSearchListUpdates)
				outputwnd->UpdateSources(parent, false);

			if (bFound) {
				delete toadd;
			}
			return true;
	}

	// no bounded result found yet -> add as parent to list
	toadd->SetListParent(NULL);
	UINT uAvail = toadd->GetSourceCount();
	if (list->AddTail(toadd)) {
		listStruct->m_mapParentsByHash[CSKey(toadd->GetFileHash())] = toadd;
		UINT tempValue;
		if (!m_foundFilesCount.Lookup(toadd->GetSearchID(), tempValue))
			tempValue = 0;
		m_foundFilesCount[toadd->GetSearchID()] = tempValue + 1;

		if (bDoSpamRating) {
			if (!m_originalFoundFilesCount.Lookup(toadd->GetSearchID(), tempValue))
				tempValue = 0;
			m_originalFoundFilesCount[toadd->GetSearchID()] = tempValue + 1;
		}

		// get the 'Availability' of this new search result entry
		if (bClientResponse)
			// If this is a response from a client ("View Shared Files"), we set the "Availability" at least to 1.
			toadd->AddSources(uAvail ? uAvail : 1);
	}

	if (thePrefs.GetDebugSearchResultDetailLevel() >= 1)
		toadd->SetListExpanded(true);

	if (!bDoSpamRating && toadd->GetKnownType() == CSearchFile::NotDetermined)
		SetSearchItemKnownType(toadd);

	if (bDoSpamRating) // Don't calculate spam rating when we are just merging tabs.
		DoSpamRating(toadd, bClientResponse, Calculate, false, dwFromUDPServerIP); // Calculate spam rating

	// add the 'Availability' of this new search result entry to the total search result count for this search
	AddResultCount(toadd->GetSearchID(), toadd->GetFileHash(), uAvail, toadd->IsConsideredSpam());

	// This will be done by ReloadList later
	TouchSearchModelSequence();

	return true;
}

CSearchFile* CSearchList::GetSearchFileByHash(const uchar *hash) const
{
	if (hash == NULL)
		return NULL;

	for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
		const SearchListsStruct *listCur = m_listFileLists.GetNext(pos);
		CSearchFile *sf = NULL;
		if (listCur->m_mapParentsByHash.Lookup(CSKey(hash), sf) && sf != NULL)
			return sf;
	}
	return NULL;
}


bool CSearchList::GetSearchResultId(const CSearchFile *pFile, SSearchResultId &id) const
{
	id.Clear();
	if (pFile == NULL)
		return false;

	id.Set(pFile->GetSearchID(), pFile->GetFileHash(), pFile->GetListParent() != NULL, pFile->GetFileName());
	return id.IsValid();
}

CSearchFile* CSearchList::GetSearchFileByResultId(const SSearchResultId &id) const
{
	if (!id.IsValid())
		return NULL;

	for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
		const SearchListsStruct *pConstList = m_listFileLists.GetNext(pos);
		if (pConstList == NULL || pConstList->m_nSearchID != id.m_nSearchID)
			continue;

		SearchListsStruct *pList = const_cast<SearchListsStruct*>(pConstList);
		CSearchFile *pParent = NULL;
		if (!pList->m_mapParentsByHash.Lookup(CSKey(id.m_abyFileHash), pParent) || pParent == NULL)
			return NULL;

		if (!id.m_bChild)
			return id.EqualsRow(pParent->GetSearchID(), pParent->GetFileHash(), false, pParent->GetFileName()) ? pParent : NULL;

		void *pChildValue = NULL;
		if (!id.m_strFileName.IsEmpty() && pList->m_mapChildrenByParentAndName.Lookup(GetChildIndexKey(pParent, id.m_strFileName), pChildValue)) {
			CSearchFile *pChild = static_cast<CSearchFile*>(pChildValue);
			if (pChild != NULL && id.EqualsRow(pChild->GetSearchID(), pChild->GetFileHash(), true, pChild->GetFileName()))
				return pChild;
		}

		SearchChildList *pChildren = GetChildrenForParent(pList, pParent, false);
		if (pChildren != NULL) {
			for (POSITION posChild = pChildren->GetHeadPosition(); posChild != NULL;) {
				CSearchFile *pChild = pChildren->GetNext(posChild);
				if (pChild != NULL && id.EqualsRow(pChild->GetSearchID(), pChild->GetFileHash(), true, pChild->GetFileName()))
					return pChild;
			}
		}
		return pParent;
	}

	return NULL;
}

CSearchFile* CSearchList::GetSearchFileByResultRow(const SSearchResultId &id, bool bChild, LPCTSTR pszFileName) const
{
	if (!id.IsValid())
		return NULL;

	SearchListsStruct *pList = const_cast<CSearchList*>(this)->GetSearchListStructForID(id.m_nSearchID, false);
	if (pList == NULL)
		return NULL;

	CSearchFile *pParent = NULL;
	if (!pList->m_mapParentsByHash.Lookup(CSKey(id.m_abyFileHash), pParent) || pParent == NULL)
		return NULL;

	const bool bTargetChild = !id.m_strFileName.IsEmpty() ? id.m_bChild : bChild;
	LPCTSTR pszTargetFileName = !id.m_strFileName.IsEmpty() ? static_cast<LPCTSTR>(id.m_strFileName) : pszFileName;
	if (!bTargetChild)
		return pParent;

	void *pChildValue = NULL;
	if (pszTargetFileName != NULL && *pszTargetFileName != _T('\0') && pList->m_mapChildrenByParentAndName.Lookup(GetChildIndexKey(pParent, pszTargetFileName), pChildValue)) {
		CSearchFile *pChild = static_cast<CSearchFile*>(pChildValue);
		if (pChild != NULL && pChild->GetListParent() == pParent)
			return pChild;
	}

	SearchChildList *pChildren = GetChildrenForParent(pList, pParent, false);
	if (pChildren != NULL) {
		for (POSITION posChild = pChildren->GetHeadPosition(); posChild != NULL;) {
			CSearchFile *pChild = pChildren->GetNext(posChild);
			if (pChild != NULL && pChild->GetListParent() == pParent && (pszTargetFileName == NULL || *pszTargetFileName == _T('\0') || pChild->GetFileName().CompareNoCase(pszTargetFileName) == 0))
				return pChild;
		}
	}

	return pParent;
}


void CSearchList::ProcessSearchAnswerParseJobsOnParserThread()
{
	if (!theApp.GuardNetworkParse(CemuleApp::NetworkParseSearchAnswer, _T("CSearchList::ProcessSearchAnswerParseJobsOnParserThread")))
		return;

	const DWORD dwSliceStartTick = ::GetTickCount();
	UINT uProcessedInSlice = 0;
	for (;;) {
		SChunkedSearchAnswerParseJob *pJob = NULL;
		{
			CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
			if (m_chunkedSearchAnswerParseJobs.IsEmpty())
				break;
			pJob = m_chunkedSearchAnswerParseJobs.RemoveHead();
		}
		if (pJob == NULL)
			continue;

		if (IsSearchAnswerParseJobStale(pJob)) {
			delete pJob;
			continue;
		}

		bool bFatalParseError = false;
		std::vector<SSearchIngestRecord> records;
		try {
			if (pJob->m_packet.empty()) {
				++pJob->m_uFailed;
				bFatalParseError = true;
			} else {
				CSafeMemFile packet(&pJob->m_packet[0], static_cast<UINT>(pJob->m_packet.size()));
				packet.Seek(static_cast<LONGLONG>(pJob->m_uPacketPosition), CFile::begin);
				while (pJob->m_uNextResult < pJob->m_uResultCount && !IsSearchAnswerParseJobStale(pJob)) {
					CSearchFile *toadd = NULL;
					try {
						if (pJob->m_bClientResponse) {
							LPCTSTR pszDirectory = pJob->m_strDirectory.IsEmpty() ? NULL : (LPCTSTR)pJob->m_strDirectory;
							toadd = new CSearchFile(packet, pJob->m_bOptUTF8, pJob->m_nSearchID, 0, 0, pszDirectory);
						} else
							toadd = new CSearchFile(packet, pJob->m_bOptUTF8, pJob->m_nSearchID);

						if (toadd->IsLargeFile() && !pJob->m_bSupportsLargeFiles) {
							AddDebugLogLine(DLP_LOW, false, _T("Client offers large file (%s) but did not announce support for it - ignoring file\n"), (LPCTSTR)EscPercent(toadd->GetFileName()));
							++pJob->m_uFailed;
						} else {
							if (pJob->m_bClientResponse) {
								toadd->SetClientID(pJob->m_nClientID);
								toadd->SetClientPort(pJob->m_nClientPort);
								toadd->SetClientServerIP(pJob->m_nClientServerIP);
								toadd->SetClientServerPort(pJob->m_nClientServerPort);
								if (pJob->m_nClientServerIP != 0 && pJob->m_nClientServerPort != 0) {
									CSearchFile::SServer server(pJob->m_nClientServerIP, pJob->m_nClientServerPort, false);
									server.m_uAvail = 1;
									toadd->AddServer(server);
								}
								toadd->SetPreviewPossible(pJob->m_bPreviewSupport && ED2KFT_VIDEO == GetED2KFileTypeID(toadd->GetFileName()));
							} else {
								toadd->SetClientServerIP(pJob->m_nClientServerIP);
								toadd->SetClientServerPort(pJob->m_nClientServerPort);
								if (pJob->m_nClientServerIP != 0 && pJob->m_nClientServerPort != 0) {
									CSearchFile::SServer server(pJob->m_nClientServerIP, pJob->m_nClientServerPort, false);
									server.m_uAvail = toadd->GetIntTagValue(FT_SOURCES);
									toadd->AddServer(server);
								}
							}

							SSearchIngestRecord record;
							if (BuildSearchIngestRecord(toadd, record, true)) {
								records.push_back(record);
								++pJob->m_uProcessed;
							} else
								++pJob->m_uFailed;
						}
						delete toadd;
					} catch (CException *ex) {
						delete toadd;
						ex->Delete();
						++pJob->m_uFailed;
						bFatalParseError = true;
						break;
					} catch (...) {
						delete toadd;
						++pJob->m_uFailed;
						bFatalParseError = true;
						break;
					}

					++pJob->m_uNextResult;
					pJob->m_uPacketPosition = packet.GetPosition();
					++uProcessedInSlice;

					const DWORD dwNow = ::GetTickCount();
					if (pJob->m_dwLastProgressTick == 0 || static_cast<DWORD>(dwNow - pJob->m_dwLastProgressTick) >= theApp.GetTimeBudgetedProgressTraceMs(CemuleApp::TimeBudgetSearchIngest)) {
						pJob->m_dwLastProgressTick = dwNow;
						theApp.QueueSearchPacketParseEvent(CemuleApp::ApplicationEventSearchPacketParseProgress, pJob->m_nSearchID, pJob->m_uProcessed, pJob->m_uFailed, pJob->m_uResultCount, pJob->m_bClientResponse ? _T("client-search-answer-parse") : _T("server-search-answer-parse"));
					}

					if (theApp.IsTimeBudgetExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchIngest))
						break;
				}

				if (!pJob->m_bClientResponse && pJob->m_uNextResult >= pJob->m_uResultCount && !bFatalParseError && !IsSearchAnswerParseJobStale(pJob)) {
					const int iAddData = static_cast<int>(packet.GetLength() - packet.GetPosition());
					if (iAddData == 1) {
						const uint8 ucMore = packet.ReadUInt8();
						if (ucMore == 0x00 || ucMore == 0x01)
							pJob->m_bMoreResultsAvailable = ucMore != 0;
						else if (thePrefs.GetDebugServerTCPLevel() > 0)
							Debug(_T("*** NOTE: ProcessSearchAnswer(Server async): ***AddData: 1 byte: 0x%02x\n"), ucMore);
					} else if (iAddData > 0 && thePrefs.GetDebugServerTCPLevel() > 0) {
						Debug(_T("*** NOTE: ProcessSearchAnswer(Server async): ***AddData: %u bytes\n"), iAddData);
						DebugHexDump(&pJob->m_packet[0] + static_cast<size_t>(packet.GetPosition()), iAddData);
					}
					pJob->m_uPacketPosition = packet.GetPosition();
				}
			}
			} catch (CException *ex) {
				ex->Delete();
				++pJob->m_uFailed;
				bFatalParseError = true;
			} catch (...) {
				++pJob->m_uFailed;
				bFatalParseError = true;
			}

		const bool bStale = IsSearchAnswerParseJobStale(pJob);
		const bool bParseFinished = pJob->m_uNextResult >= pJob->m_uResultCount || bFatalParseError || bStale;
		if (!bStale)
			QueueParsedSearchIngestBatch(*pJob, records, bParseFinished);

		if (bParseFinished) {
			if (!bStale) {
				const bool bCompleted = pJob->m_uNextResult >= pJob->m_uResultCount && !bFatalParseError;
				LPCTSTR pszStage = pJob->m_bClientResponse ? _T("client-search-answer-parse") : _T("server-search-answer-parse");
				LPCTSTR pszErrorStage = pJob->m_bClientResponse ? _T("client-search-answer-parse-error") : _T("server-search-answer-parse-error");
				theApp.QueueSearchPacketParseEvent(bCompleted ? CemuleApp::ApplicationEventSearchPacketParseCompleted : CemuleApp::ApplicationEventSearchPacketParseFailed, pJob->m_nSearchID, pJob->m_uProcessed, pJob->m_uFailed, pJob->m_uResultCount, bCompleted ? pszStage : pszErrorStage);
				AddDebugLogLine(DLP_LOW, false, _T("Chunked search answer parse %s. search=%u processed=%u failed=%u total=%u elapsed=%u\n"), bCompleted ? _T("completed") : _T("failed"), pJob->m_nSearchID, pJob->m_uProcessed, pJob->m_uFailed, pJob->m_uResultCount, static_cast<DWORD>(::GetTickCount() - pJob->m_dwStartedTick));
			}
			delete pJob;
		} else {
			CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
			m_chunkedSearchAnswerParseJobs.AddHead(pJob);
		}

		if (uProcessedInSlice != 0 && theApp.IsTimeBudgetExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchIngest))
			break;
	}

	DWORD dwSliceElapsed = 0;
	if (theApp.IsTimeBudgetHardExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchIngest, &dwSliceElapsed)) {
		CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetSearchIngest, _T("ProcessSearchAnswerParseJobsOnParserThread"), dwSliceElapsed, uProcessedInSlice, m_chunkedSearchAnswerParseJobs.GetCount());
	}

	bool bHasMore = false;
	{
		CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
		bHasMore = !m_chunkedSearchAnswerParseJobs.IsEmpty();
	}
	if (bHasMore)
		SignalSearchAnswerParseThread();
}

void CSearchList::ProcessStoredSearchIngestPrepareJobsOnParserThread()
{
	const DWORD dwSliceStartTick = ::GetTickCount();
	UINT uProcessedInSlice = 0;
	for (;;) {
		SStoredSearchIngestPrepareJob *pJob = NULL;
		{
			CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
			if (m_storedSearchIngestPrepareJobs.IsEmpty())
				break;
			pJob = m_storedSearchIngestPrepareJobs.RemoveHead();
		}
		if (pJob == NULL)
			continue;

		if (IsStoredSearchIngestPrepareJobStale(pJob)) {
			delete pJob;
			continue;
		}

		std::vector<SSearchIngestRecord> records;
		while (pJob->m_iNextRecord < static_cast<INT_PTR>(pJob->m_records.size()) && !IsStoredSearchIngestPrepareJobStale(pJob)) {
			CSearchFile *pFile = CreateSearchFileFromIngestRecord(pJob->m_records[static_cast<size_t>(pJob->m_iNextRecord++)]);
			if (pFile != NULL) {
				SSearchIngestRecord record;
				if (BuildSearchIngestRecord(pFile, record, true)) {
					records.push_back(record);
					++pJob->m_uProcessed;
				} else
					++pJob->m_uFailed;
				delete pFile;
			} else
				++pJob->m_uFailed;
			++uProcessedInSlice;

			const DWORD dwNow = ::GetTickCount();
			if (pJob->m_dwLastProgressTick == 0 || static_cast<DWORD>(dwNow - pJob->m_dwLastProgressTick) >= theApp.GetTimeBudgetedProgressTraceMs(CemuleApp::TimeBudgetSearchIngest)) {
				pJob->m_dwLastProgressTick = dwNow;
				AddDebugLogLine(DLP_VERYLOW, false, _T("Stored search ingest prepare progress. search=%u processed=%u failed=%u remaining=%d\n"), pJob->m_nSearchID, pJob->m_uProcessed, pJob->m_uFailed, static_cast<int>(pJob->m_records.size() - static_cast<size_t>(pJob->m_iNextRecord)));
			}

			if (theApp.IsTimeBudgetExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchIngest))
				break;
		}

		const bool bFinished = pJob->m_iNextRecord >= static_cast<INT_PTR>(pJob->m_records.size()) || IsStoredSearchIngestPrepareJobStale(pJob);
		QueueParsedSearchIngestBatch(*pJob, records, bFinished && pJob->m_bNotifyUiOnCompletion);
		if (bFinished) {
			AddDebugLogLine(DLP_LOW, false, _T("Stored search ingest prepare completed. search=%u processed=%u failed=%u elapsed=%u\n"), pJob->m_nSearchID, pJob->m_uProcessed, pJob->m_uFailed, static_cast<DWORD>(::GetTickCount() - pJob->m_dwStartedTick));
			delete pJob;
		} else {
			CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
			m_storedSearchIngestPrepareJobs.AddHead(pJob);
		}

		if (uProcessedInSlice != 0 && theApp.IsTimeBudgetExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchIngest))
			break;
	}

	DWORD dwSliceElapsed = 0;
	if (theApp.IsTimeBudgetHardExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchIngest, &dwSliceElapsed)) {
		CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetSearchIngest, _T("ProcessStoredSearchIngestPrepareJobsOnParserThread"), dwSliceElapsed, uProcessedInSlice, m_storedSearchIngestPrepareJobs.GetCount());
	}

	bool bHasMore = false;
	{
		CSingleLock lock(&m_searchAnswerParseQueueLock, TRUE);
		bHasMore = !m_storedSearchIngestPrepareJobs.IsEmpty();
	}
	if (bHasMore)
		SignalSearchAnswerParseThread();
}

void CSearchList::ProcessChunkedSearchIngestJobs()
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::ProcessChunkedSearchIngestJobs"));
	if (!mutationLock)
		return;
	m_bChunkedSearchIngestPending = false;
	if (theApp.IsClosing()) {
		ClearChunkedSearchIngestJobs();
		return;
	}

	const DWORD dwSliceStartTick = ::GetTickCount();
	UINT uProcessedInSlice = 0;
	DrainParsedSearchIngestBatches();
	DrainPreparedChunkedSpamRatingJobs();
	ProcessChunkedSpamRatingJobs(dwSliceStartTick, uProcessedInSlice);
	while (!m_chunkedSearchIngestJobs.IsEmpty() && !theApp.IsTimeBudgetExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchIngest)) {
		SChunkedSearchIngestJob *pJob = m_chunkedSearchIngestJobs.GetHead();
		if (pJob == NULL) {
			m_chunkedSearchIngestJobs.RemoveHead();
			continue;
		}

		if (IsSearchIngestJobStale(pJob)) {
			AddDebugLogLine(DLP_LOW, false, _T("Chunked search ingest job dropped because it is stale. search=%u generation=%ld searchGeneration=%ld\n"), pJob->m_nSearchID, pJob->m_lGeneration, pJob->m_lSearchGeneration);
			delete m_chunkedSearchIngestJobs.RemoveHead();
			continue;
		}

		{
			CScopedSearchListUpdateDeferral scopedDeferral(m_bDeferSearchListUpdates);
			while (pJob->m_iNextRecord < static_cast<INT_PTR>(pJob->m_records.size())) {
				CSearchFile *pFile = CreateSearchFileFromIngestRecord(pJob->m_records[static_cast<size_t>(pJob->m_iNextRecord++)]);
				if (pFile != NULL && AddToList(pFile, pJob->m_bClientResponse, pJob->m_dwFromUDPServerIP, pJob->m_bDoSpamRating))
					++pJob->m_uProcessed;
				else
					++pJob->m_uFailed;
				++uProcessedInSlice;

				const DWORD dwNow = ::GetTickCount();
				if (m_dwChunkedSearchIngestLastProgressTick == 0 || static_cast<DWORD>(dwNow - m_dwChunkedSearchIngestLastProgressTick) >= theApp.GetTimeBudgetedProgressTraceMs(CemuleApp::TimeBudgetSearchIngest)) {
					m_dwChunkedSearchIngestLastProgressTick = dwNow;
					AddDebugLogLine(DLP_VERYLOW, false, _T("Chunked search ingest progress. search=%u processed=%u failed=%u remaining=%d\n"), pJob->m_nSearchID, pJob->m_uProcessed, pJob->m_uFailed, static_cast<int>(pJob->m_records.size() - static_cast<size_t>(pJob->m_iNextRecord)));
				}

				if (theApp.IsTimeBudgetExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchIngest))
					break;
			}
		}

		if (pJob->m_iNextRecord >= static_cast<INT_PTR>(pJob->m_records.size())) {
			if (pJob->m_bNotifyUiOnCompletion)
				UpdateSearchIngestOutputWnd(pJob->m_nSearchID, pJob->m_strClientHash, pJob->m_bUseKadReloadThrottle);
			if (pJob->m_bNotifyLocalEd2kSearchEnd)
				theApp.QueueLocalEd2kSearchEndEvent(pJob->m_nSearchID, GetResultCount(pJob->m_nSearchID), pJob->m_bMoreResultsAvailable);
			AddDebugLogLine(DLP_LOW, false, _T("Chunked search ingest completed. search=%u processed=%u failed=%u elapsed=%u\n"), pJob->m_nSearchID, pJob->m_uProcessed, pJob->m_uFailed, static_cast<DWORD>(::GetTickCount() - pJob->m_dwStartedTick));
			delete m_chunkedSearchIngestJobs.RemoveHead();
		}

		if (uProcessedInSlice != 0 && theApp.IsTimeBudgetExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchIngest))
			break;
	}

	DWORD dwSliceElapsed = 0;
	if (theApp.IsTimeBudgetHardExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSearchIngest, &dwSliceElapsed))
		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetSearchIngest, _T("ProcessChunkedSearchIngestJobs"), dwSliceElapsed, uProcessedInSlice, m_chunkedSearchIngestJobs.GetCount() + m_chunkedSpamRatingJobs.GetCount());

	bool bHasParsedBatches = false;
	{
		CSingleLock lock(&m_parsedSearchIngestBatchLock, TRUE);
		bHasParsedBatches = !m_parsedSearchIngestBatches.IsEmpty();
	}
	if ((!m_chunkedSearchIngestJobs.IsEmpty() || !m_chunkedSpamRatingJobs.IsEmpty() || bHasParsedBatches) && !m_bChunkedSearchIngestPending) {
		if (!PostChunkedSearchIngestMessage()) {
			AddDebugLogLine(DLP_LOW, false, _T("Chunked search ingest cancelled because the UI message target is unavailable. remainingIngestJobs=%d remainingSpamJobs=%d hasParsedBatches=%u\n"), static_cast<int>(m_chunkedSearchIngestJobs.GetCount()), static_cast<int>(m_chunkedSpamRatingJobs.GetCount()), bHasParsedBatches ? 1U : 0U);
			ClearChunkedSearchIngestJobs();
		}
	}

	theApp.QueueSearchActivityChangedEvent(0);
}


bool CSearchList::AddNotes(const Kademlia::CEntry &cEntry, const uchar *hash)
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::AddNotes"));
	if (!mutationLock)
		return false;

	bool flag = false;
	for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
		const SearchListsStruct *listCur = m_listFileLists.GetNext(pos);
		for (POSITION pos2 = listCur->m_listSearchFiles.GetHeadPosition(); pos2 != NULL;) {
			CSearchFile *sf = listCur->m_listSearchFiles.GetNext(pos2);
			if (md4equ(hash, sf->GetFileHash()) && sf->AddNote(cEntry))
				flag = true;
		}
	}
	return flag;
}

void CSearchList::SetNotesSearchStatus(const uchar *pFileHash, bool bSearchRunning)
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::SetNotesSearchStatus"));
	if (!mutationLock)
		return;

	for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
		const SearchListsStruct *listCur = m_listFileLists.GetNext(pos);
		for (POSITION pos2 = listCur->m_listSearchFiles.GetHeadPosition(); pos2 != NULL;) {
			CSearchFile *sf = listCur->m_listSearchFiles.GetNext(pos2);
			if (md4equ(pFileHash, sf->GetFileHash()))
				sf->SetKadCommentSearchRunning(bSearchRunning);
		}
	}
}

void CSearchList::AddResultCount(uint32 nSearchID, const uchar *hash, UINT nCount, bool bSpam)
{
	// do not count already available or downloading files for the search result limit
	if (theApp.sharedfiles->GetFileByID(hash) || theApp.downloadqueue->GetFileByID(hash))
		return;

	UINT tempValue;
	if (!m_foundSourcesCount.Lookup(nSearchID, tempValue))
		tempValue = 0;

	// spam files count as max 5 availability
	m_foundSourcesCount[nSearchID] = tempValue + ((bSpam && thePrefs.IsSearchSpamFilterEnabled()) ? min(nCount, 5) : nCount);
}

// FIXME LARGE FILES
void CSearchList::KademliaSearchKeyword(uint32 nSearchID, const Kademlia::CUInt128 *pFileID, LPCTSTR name
	, uint64 size, LPCTSTR type, UINT uKadPublishInfo
	, CArray<CAICHHash> &raAICHHashes, CArray<uint8, uint8> &raAICHHashPopularity
	, SSearchTerm *pQueriedSearchTerm, UINT numProperties, ...)
{
	va_list args;
	va_start(args, numProperties);

	EUTF8str eStrEncode = UTF8strRaw;
	Kademlia::CKeyEntry verifierEntry;

	verifierEntry.m_uKeyID.SetValue(*pFileID);
	uchar fileid[16];
	pFileID->ToByteArray(fileid);

	CSafeMemFile temp(250);
	temp.WriteHash16(fileid);
	temp.WriteUInt32(0);	// client IP
	temp.WriteUInt16(0);	// client port

	// write tag list
	UINT uFilePosTagCount = (UINT)temp.GetPosition();
	temp.WriteUInt32(0); // dummy tag count, will be filled later

	uint32 tagcount = 0;
	// standard tags
	CTag tagName(FT_FILENAME, name);
	tagName.WriteTagToFile(temp, eStrEncode);
	++tagcount;
	verifierEntry.SetFileName(Kademlia::CKadTagValueString(name));

	CTag tagSize(FT_FILESIZE, size, true);
	tagSize.WriteTagToFile(temp, eStrEncode);
	++tagcount;
	verifierEntry.m_uSize = size;

	if (type != NULL && type[0] != _T('\0')) {
		CTag tagType(FT_FILETYPE, type);
		tagType.WriteTagToFile(temp, eStrEncode);
		++tagcount;
		verifierEntry.AddTag(new Kademlia::CKadTagStr(TAG_FILETYPE, type));
	}

	// additional tags
	for (; numProperties > 0; --numProperties) {
		UINT uPropType = va_arg(args, UINT);
		LPCSTR pszPropName = va_arg(args, LPCSTR);
		LPVOID pvPropValue = va_arg(args, LPVOID);
		if (uPropType == TAGTYPE_STRING) {
			if ((LPCTSTR)pvPropValue != NULL && ((LPCTSTR)pvPropValue)[0] != _T('\0')) {
				if (strlen(pszPropName) == 1) {
					CTag tagProp((uint8)*pszPropName, (LPCTSTR)pvPropValue);
					tagProp.WriteTagToFile(temp, eStrEncode);
				} else {
					CTag tagProp(pszPropName, (LPCTSTR)pvPropValue);
					tagProp.WriteTagToFile(temp, eStrEncode);
				}
				verifierEntry.AddTag(new Kademlia::CKadTagStr(pszPropName, (LPCTSTR)pvPropValue));
				++tagcount;
			}
		} else if (uPropType == TAGTYPE_UINT32) {
			if ((uint32)pvPropValue != 0) {
				CTag tagProp(pszPropName, (uint32)pvPropValue);
				tagProp.WriteTagToFile(temp, eStrEncode);
				++tagcount;
				verifierEntry.AddTag(new Kademlia::CKadTagUInt(pszPropName, (uint32)pvPropValue));
			}
		} else
			ASSERT(0);
	}
	va_end(args);
	temp.Seek(uFilePosTagCount, CFile::begin);
	temp.WriteUInt32(tagcount);

	if (pQueriedSearchTerm == NULL || verifierEntry.StartSearchTermsMatch(*pQueriedSearchTerm)) {
		temp.SeekToBegin();
		CSearchFile *tempFile = new CSearchFile(temp, eStrEncode == UTF8strRaw, nSearchID, 0, 0, NULL, true);
		tempFile->SetKadPublishInfo(uKadPublishInfo);
		// About the AICH hash: We received a list of possible AICH hashes for this file and now have to decide what to do
		// If it wasn't for backwards compatibility, the choice would be easy: Each different md4+aich+size is its own result,
		// but we can't do this alone for the fact that for the next years we will always have publishers which don't report
		// the AICH hash at all (which would mean having a different entry, which leads to double files in search results).
		// So here is what we do for now:
		// If we have exactly 1 AICH hash and more than 1/3 of the publishers reported it, we set it as verified AICH hash for
		// the file (which is as good as using an ed2k link with an AICH hash attached). If less publishers reported it or if we
		// have multiple AICH hashes, we ignore them and use the MD4 only.
		// This isn't a perfect solution, but it makes sure not to open any new attack vectors (a wrong AICH hash means we cannot
		// download the file successfully) nor to confuse users by requiring them to select an entry out of several equal looking results.
		// Once the majority of nodes in the network publishes AICH hashes, this might get reworked to make the AICH hash more sticky
		if (raAICHHashes.GetCount() == 1 && raAICHHashPopularity.GetCount() == 1) {
			uint8 byPublishers = (uint8)((uKadPublishInfo >> 16) & 0xFF);
			if (byPublishers > 0 && raAICHHashPopularity[0] > 0 && byPublishers / raAICHHashPopularity[0] <= 3) {
				DEBUG_ONLY(DebugLog(_T("Received accepted AICH Hash for search result %s, %u out of %u Publishers, Hash: %s")
					, (LPCTSTR)EscPercent(tempFile->GetFileName()), raAICHHashPopularity[0], byPublishers, (LPCTSTR)raAICHHashes[0].GetString()));
				tempFile->GetFileIdentifier().SetAICHHash(raAICHHashes[0]);
			} else
				DEBUG_ONLY(DebugLog(_T("Received unaccepted AICH Hash for search result %s, %u out of %u Publishers, Hash: %s")
					, (LPCTSTR)EscPercent(tempFile->GetFileName()), raAICHHashPopularity[0], byPublishers, (LPCTSTR)raAICHHashes[0].GetString()));
		} else if (raAICHHashes.GetCount() > 1)
			DEBUG_ONLY(DebugLog(_T("Received multiple (%u) AICH hashes for search result %s, ignoring AICH"), raAICHHashes.GetCount(), (LPCTSTR)EscPercent(tempFile->GetFileName())));
		if (!QueueSearchFileForIngest(tempFile, EMPTY, false, 0, true, true))
			AddDebugLogLine(DLP_LOW, false, _T("Kad search ingest record was dropped. search=%u\n"), nSearchID);
	} else
		DebugLogWarning(_T("Kad Searchresult failed sanitize check against search query, ignoring. (%s)"), (LPCTSTR)EscPercent(name));
}


// default spam threshold = 60
#define SPAM_FILEHASH_HIT					100

#define SPAM_FULLNAME_HIT					80
#define	SPAM_SMALLFULLNAME_HIT				50
#define SPAM_SIMILARNAME_HIT				60
#define SPAM_SMALLSIMILARNAME_HIT			40
#define SPAM_SIMILARNAME_NEARHIT			50
#define SPAM_SIMILARNAME_FARHIT				40

#define SPAM_SIMILARSIZE_HIT				10

#define SPAM_UDPSERVERRES_HIT				21
#define SPAM_UDPSERVERRES_NEARHIT			15
#define SPAM_UDPSERVERRES_FARHIT			10

#define SPAM_ONLYUDPSPAMSERVERS_HIT			30

#define SPAM_SOURCE_HIT						39

#define SPAM_HEURISTIC_BASEHIT				39
#define SPAM_HEURISTIC_MAXHIT				60


#define UDP_SPAMRATIO_THRESHOLD				50

// Returns false only if processing is completely skipped by the first check
bool CSearchList::DoSpamRating(CSearchFile *pSearchFile, bool bIsClientFile, uint8 uActionType, bool bUpdate, uint32 dwFromUDPServerIP)
{
	/* This spam filter uses two simple approaches to try to identify spam search results:
	1 - detect general characteristics of fake results - not very reliable
		which are (each hit increases the score)
		* high availability from one udp server, but none from others
		* archive or program + size between 0,1 and 10 MB
		* 100% complete sources together with high availability
		Apparently, those characteristics target for current spyware fake results, other fake results like videos
		and so on will not be detectable, because only the first point is more or less common for server fake results,
		which would produce too many false positives

	2 - learn characteristics of files a user has marked as spam
		remembered data is:
		* FileHash (of course, a hit will always lead to a full score rating)
		* Equal filename
		* Equal or similar name after removing the search keywords and separators
			(if search for "emule", "blubby!! emule foo.rar" is remembered as "blubby foo rar")
		* Similar size (+- 5% but max 5MB) as other spam files
		* Equal search source server (UDP only)
		* Equal initial source clients
		* Ratio (Spam / NotSpam) of UDP Servers
	Both detection methods add to the same score rating.

	uActionType == MarkAsNotSpam Will remove all stored characteristics which would add to a positive spam score for this file
	*/

	if (!pSearchFile)
		return false; // No file to process

	if (pSearchFile->GetKnownType() == CSearchFile::NotDetermined) {
		if ((pSearchFile->GetListParent() && pSearchFile->GetListParent()->GetKnownType() != CSearchFile::NotDetermined)) // Check if this is a child item with a parent already marked as a known file
			pSearchFile->SetKnownType(pSearchFile->GetListParent()->GetKnownType());
		else // This is not a child item, so we need to check if the file is a known file
			SetSearchItemKnownType(pSearchFile);

		if (pSearchFile->GetKnownType() != CSearchFile::NotDetermined) {
			if (bUpdate && outputwnd != NULL)
				outputwnd->UpdateSources(pSearchFile->GetListParent() != NULL ? pSearchFile->GetListParent() : pSearchFile, true);
			return true; // If this is a known file, then we don't need to proceed further with spam/blacklist checks
		}
	} else
		return true; // If this is a known file, then we don't need to proceed further with spam/blacklist checks

	// Return if spam/blacklist checks are disabled by user. Also return if if this is a known file. Because marking as known files has a priority over marking as spam/blacklisted. 
	if ((!thePrefs.IsSearchSpamFilterEnabled() && !thePrefs.GetBlacklistManual() && !thePrefs.GetBlacklistAutomatic()))
		return false;

	if (!m_bSpamFilterLoaded)
		LoadSpamFilter();

	int nSpamScore = 0;
	CString strDebug;
	bool bSureNegative = false;
	int nDbgFileHash, nDbgStrings, nDbgSize, nDbgServer, nDbgSources, nDbgHeuristic, nDbgOnlySpamServer;
	nDbgFileHash = nDbgStrings = nDbgSize = nDbgServer = nDbgSources = nDbgHeuristic = nDbgOnlySpamServer = 0;

	CSearchFile* pParent = NULL;
	if (pSearchFile->GetListParent())
		pParent = pSearchFile->GetListParent();
	else if (pSearchFile->GetListChildCount() > 0)
		pParent = pSearchFile;

	if (uActionType == Calculate && thePrefs.GetBlacklistAutomatic()) { // Check if we need to calculate Automatic Blacklist and Spam Rating
		// 0.1- Automatic Blacklist (based on user definitions)
		// We need to bypass this when MarkAsNotSpam is true since it needs to do some important staff including calling UpdateSources and RecalculateSpamRatings.
		const bool bUseCachedAutomaticBlacklist = !bUpdate && pSearchFile->HasAutomaticBlacklistEvaluation();
		const bool bCachedAutomaticBlacklisted = bUseCachedAutomaticBlacklist && pSearchFile->GetAutomaticBlacklisted();
		pSearchFile->SetAutomaticBlacklisted(false); // Reset Automatic Blacklist flag before checking conditions

		// Check if this is a child item with a parent already marked as AutoBlacklisted or a parent item with an automatic blacklist match.
		if ((pSearchFile->GetListParent() && pSearchFile->GetListParent()->GetAutomaticBlacklisted()) || bCachedAutomaticBlacklisted || (!bUseCachedAutomaticBlacklist && IsFilenameAutoBlacklisted(pSearchFile->GetFileName()))) {
			pSearchFile->SetAutomaticBlacklisted(true);
			if (thePrefs.GetBlacklistAutoRemoveFromManual()) {
				pSearchFile->SetManualBlacklisted(false);
				m_mapBlacklistedHashes.RemoveKey(CSKey(pSearchFile->GetFileHash()));
			}
		}
		
		// Mark parent and children as Automatic Blacklisted
		if (pSearchFile->GetAutomaticBlacklisted() && pParent) {
			pParent->SetAutomaticBlacklisted(true); // Mark parent as Automatic Blacklisted
			// Remove parent from Manual Blacklist if it was already there
			if (thePrefs.GetBlacklistAutoRemoveFromManual()) {
				pParent->SetManualBlacklisted(false);
				m_mapBlacklistedHashes.RemoveKey(CSKey(pParent->GetFileHash()));
			}

			// Mark children as Automatic Blacklisted
			const SearchList* list = GetSearchListForID(pParent->GetSearchID());
			for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
				CSearchFile* pCurFile = list->GetNext(pos);
				if (pCurFile->GetListParent() == pParent) {
					pCurFile->SetAutomaticBlacklisted(true);
					// Remove child from Manual Blacklist if it was already there
					if (thePrefs.GetBlacklistAutoRemoveFromManual()) {
						pCurFile->SetManualBlacklisted(false);
						m_mapBlacklistedHashes.RemoveKey(CSKey(pCurFile->GetFileHash()));
					}
				}
			}
		}

		// If file is marked as Automatic Blacklisted, then there's no need to proceed for the spam calculations since this will already be listed as spam unless we're coming here from the context menu
		// or GetBlacklistAutoRemoveFromManual is false. For the second case manual blacklist need to be calculated since it is priority over automatic while showing icon and Known column.
		if (pSearchFile->GetAutomaticBlacklisted()) {
			TouchSearchModelSequence();
			if (!thePrefs.IsSearchSpamFilterEnabled() && bUpdate && outputwnd)
				outputwnd->UpdateSources(pParent ? pParent : pSearchFile, true); // Update pSearchFile and sources in the output window
			return true;
		}

		if (!thePrefs.IsSearchSpamFilterEnabled() && bUpdate && outputwnd)
			outputwnd->UpdateSources(pParent ? pParent : pSearchFile, true); // Update pSearchFile and sources in the output window
	}
	
	if (((uActionType == Calculate || uActionType == MarkAsBlacklisted || uActionType == MarkAsNotBlacklisted)) && thePrefs.GetBlacklistManual()) { // Check if we need to calculate/mark/unmark as Manual Blacklisted and Manual Blacklist is enabled
		// 0.2- Manual Blacklist (based on file hash)
		// Do not perform this checks if file is already automatic blacklisted.
		// We need to bypass this when bMarkAsNotSpam is true since it needs to do some important staff including calling UpdateSources and RecalculateSpamRatings.
		bool m_bDummyVar;

		if (uActionType == MarkAsNotBlacklisted) { // Function is called to remove file from Manual Blacklist
			m_mapBlacklistedHashes.RemoveKey(CSKey(pSearchFile->GetFileHash()));
			pSearchFile->SetManualBlacklisted(false);
		} else if ((pSearchFile->GetListParent() && pSearchFile->GetListParent()->GetManualBlacklisted())) // This is a child item with a parent already marked as ManualBlacklisted (Since this case has a lower cost action, we check this before uActionType == MarkAsBlacklisted)
			pSearchFile->SetManualBlacklisted(true);
		else if (uActionType == MarkAsBlacklisted || m_mapBlacklistedHashes.Lookup(CSKey(pSearchFile->GetFileHash()), m_bDummyVar) || // Function is called to add file to Manual Blacklist OR this is already marked as Manual Blacklisted
				(thePrefs.GetDownloadValidator() && thePrefs.GetDownloadValidatorAutoMarkAsBlacklisted() &&	theApp.DownloadValidator->CheckFile(pSearchFile->GetFileHash(), pSearchFile->GetFileName(), pSearchFile->GetFileSize(), false))) { // OR this is not blacklisted but DownloadValidator marks this file as manuel blacklisted
			pSearchFile->SetManualBlacklisted(true);
			MarkHashAsBlacklisted(CSKey(pSearchFile->GetFileHash()));
		}

		// Mark parent and children as Manual Blacklisted if MarkAsNotBlacklisted is false, otherwise mark them as not Manual Blacklisted.
		// Adding to or removing from the map has been lready done above. Since map is based file hash, a single operation is enough.
		if (pParent) {
			pParent->SetManualBlacklisted(pSearchFile->GetManualBlacklisted()); // Mark parent
			// Mark children
			const SearchList* list = GetSearchListForID(pParent->GetSearchID());
			for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
				CSearchFile* pCurFile = list->GetNext(pos);
				if (pCurFile->GetListParent() == pParent)
					pCurFile->SetManualBlacklisted(pParent->GetManualBlacklisted());
			}
		}

		TouchSearchModelSequence();
		if (bUpdate && outputwnd)
			outputwnd->UpdateSources(pParent ? pParent : pSearchFile, true); // Update pSearchFile and sources in the output window

		if (pSearchFile->GetManualBlacklisted() || uActionType != Calculate)
			return true; // There's no need to proceed for the spam calculations since pSearchFile is calculated as Manual Blacklisted or function is called to MarkAsSpam/MarkAsNotSpam only
	}

	if (!thePrefs.IsSearchSpamFilterEnabled()) // If spam filter is disabled, then there's no need to proceed further
		return true;

	CSearchFile* pTempFile = pSearchFile->GetListParent() ? pSearchFile->GetListParent() : pSearchFile;
	bool bOldSpamStatus = false;

	if (pSearchFile->GetListParent() && pSearchFile->GetListParent()->IsConsideredSpam(false))	// If this is a child item with a parent already marked as spam
		pSearchFile->SetSpamRating(pSearchFile->GetListParent()->GetSpamRating());
	else if (uActionType == MarkAsNotSpam) {
		// Remove file name
		for (int i = m_astrKnownSpamNames.GetSize() - 1; i >= 0; --i)
			if (m_astrKnownSpamNames[i].CompareNoCase(pSearchFile->GetFileName()) == 0) {
				m_astrKnownSpamNames.RemoveAt(i);
				break;
			}

		// Remove file name without keyword
		for (int i = m_astrKnownSimilarSpamNames.GetSize() - 1; i >= 0; --i)
			if (m_astrKnownSimilarSpamNames[i].CompareNoCase(pSearchFile->GetNameWithoutKeyword()) == 0) {
				m_astrKnownSimilarSpamNames.RemoveAt(i);
				break;
			}

		// Remove hash entry
		m_mapKnownSpamHashes.RemoveKey(CSKey(pSearchFile->GetFileHash()));

		// Remove file size
		for (int i = m_aui64KnownSpamSizes.GetSize() - 1; i >= 0; --i)
			if (m_aui64KnownSpamSizes[i] == static_cast<uint64>(pSearchFile->GetFileSize())) {
				m_aui64KnownSpamSizes.RemoveAt(i);
				break;
			}
	} else if (uActionType == MarkAsSpam) {
		m_astrKnownSpamNames.Add(pSearchFile->GetFileName()); // Add file name
		m_astrKnownSimilarSpamNames.Add(pSearchFile->GetNameWithoutKeyword()); // Add file name without keyword
		m_mapKnownSpamHashes[CSKey(pSearchFile->GetFileHash())] = true; // Add hash entry
		m_aui64KnownSpamSizes.Add((uint64)pSearchFile->GetFileSize()); // Add file size

		if (IsValidSearchResultClientIPPort(pSearchFile->GetClientID(), pSearchFile->GetClientPort()) && !::IsLowID(pSearchFile->GetClientID()))
			m_mapKnownSpamSourcesIPs[pSearchFile->GetClientID()] = true;

		for (int i = pSearchFile->GetClients().GetSize(); --i >= 0;)
			if (pSearchFile->GetClients()[i].m_nIP != 0)
				m_mapKnownSpamSourcesIPs[pSearchFile->GetClients()[i].m_nIP] = true;

		for (int i = pSearchFile->GetServers().GetSize(); --i >= 0;)
			if (pSearchFile->GetServers()[i].m_nIP != 0 && pSearchFile->GetServers()[i].m_bUDPAnswer)
				m_mapKnownSpamServerIPs[pSearchFile->GetServers()[i].m_nIP] = true;
	} else {
		// 1- file hash
		bool bSpam;
		if (m_mapKnownSpamHashes.Lookup(CSKey(pSearchFile->GetFileHash()), bSpam)) {
			if (bSpam) {
				nSpamScore += SPAM_FILEHASH_HIT;
				nDbgFileHash = SPAM_FILEHASH_HIT;
			} else
				bSureNegative = true;
		}

		if (bSureNegative) {
			// 2-3 FileNames: Consider also filenames of children / parents / siblings and take the highest rating
			uint32 nHighestRating;
			if (pParent) {
				nHighestRating = GetSpamFilenameRatings(pParent, false);
				const SearchList *list = GetSearchListForID(pParent->GetSearchID());
				for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
					const CSearchFile *pCurFile = list->GetNext(pos);
					if (pCurFile->GetListParent() == pParent) {
						uint32 nRating = GetSpamFilenameRatings(pCurFile, false);
						nHighestRating = max(nHighestRating, nRating);
					}
				}
			} else
				nHighestRating = GetSpamFilenameRatings(pSearchFile, false);
			nSpamScore += nHighestRating;
			nDbgStrings = nHighestRating;

			//4 - Sizes
			for (INT_PTR i = m_aui64KnownSpamSizes.GetCount(); --i >= 0;) {
				uint64 fsize = (uint64)pSearchFile->GetFileSize();
				if (fsize != 0 && _abs64(fsize - m_aui64KnownSpamSizes[i]) < 5242880 && ((_abs64(fsize - m_aui64KnownSpamSizes[i]) * 100) / fsize) < 5)	{
					nSpamScore += SPAM_SIMILARSIZE_HIT;
					nDbgSize = SPAM_SIMILARSIZE_HIT;
					break;
				}
			}
			if (!bIsClientFile) { // only to skip some useless calculations
				const CSimpleArray<CSearchFile::SServer> &aservers = pTempFile->GetServers();
				//5 Servers
				for (int i = 0; i != aservers.GetSize(); ++i) {
					bool bFound = false;
					if (aservers[i].m_nIP != 0 && aservers[i].m_bUDPAnswer && m_mapKnownSpamServerIPs.Lookup(aservers[i].m_nIP, bFound)) {
						strDebug.AppendFormat(_T(" (Serverhit: %s)"), (LPCTSTR)ipstr(aservers[i].m_nIP));
						if (pSearchFile->GetServers().GetSize() == 1 && m_mapKnownSpamServerIPs.GetCount() <= 10) {
							// source only from one server
							nSpamScore += SPAM_UDPSERVERRES_HIT;
							nDbgServer = SPAM_UDPSERVERRES_HIT;
						} else if (pSearchFile->GetServers().GetSize() == 1) {
							// source only from one server but the users seems to be a bit careless with the mark as spam option and has already added a lot UDP servers. To avoid false positives, we give a lower rating
							nSpamScore += SPAM_UDPSERVERRES_NEARHIT;
							nDbgServer = SPAM_UDPSERVERRES_NEARHIT;
						} else {
							// file was given by more than one server, lowest spam rating for server hits
							nSpamScore += SPAM_UDPSERVERRES_FARHIT;
							nDbgServer = SPAM_UDPSERVERRES_FARHIT;
						}
						break;
						m_mapKnownSpamServerIPs.RemoveKey(aservers[i].m_nIP);
					}
				}

				// partial heuristics - only udp spam servers have this file at least one server as origin which is not rated for spam or UDP or not a result from a server at all
				bool bNormalServerWithoutCurrentPresent = (aservers.GetSize() == 0);
				bool bNormalServerPresent = bNormalServerWithoutCurrentPresent;
				for (int i = 0; i < aservers.GetSize(); ++i) {
					UDPServerRecord *pRecord = NULL;
					if (aservers[i].m_bUDPAnswer && m_mUDPServerRecords.Lookup(aservers[i].m_nIP, pRecord) && pRecord != NULL) {
						ASSERT(pRecord->m_nResults >= pRecord->m_nSpamResults);
						if (pRecord->m_nResults >= pRecord->m_nSpamResults && pRecord->m_nResults > 0) {
							int nRatio = (pRecord->m_nSpamResults * 100) / pRecord->m_nResults;
							if (nRatio < 50) {
								bNormalServerWithoutCurrentPresent |= (dwFromUDPServerIP != aservers[i].m_nIP);
								bNormalServerPresent = true;
							}
						}
					} else if (!aservers[i].m_bUDPAnswer) {
						bNormalServerWithoutCurrentPresent = true;
						bNormalServerPresent = true;
						break;
					}
					ASSERT(pRecord);
				}
				if (!bNormalServerPresent) {
					nDbgOnlySpamServer = SPAM_ONLYUDPSPAMSERVERS_HIT;
					nSpamScore += SPAM_ONLYUDPSPAMSERVERS_HIT;
					strDebug += _T(" (AllSpamServers)");
				} else if (!bNormalServerWithoutCurrentPresent)
					strDebug += _T(" (AllSpamServersWoCurrent)");


				// 7 Heuristic (UDP Results)
				uint32 nResponses;
				if (!m_ReceivedUDPAnswersCount.Lookup(pTempFile->GetSearchID(), nResponses))
					nResponses = 0;
				uint32 nRequests;

				if (!m_RequestedUDPAnswersCount.Lookup(pTempFile->GetSearchID(), nRequests))
					nRequests = 0;

				if (!bNormalServerWithoutCurrentPresent	&& (nResponses >= 3 || nRequests >= 5) && pTempFile->GetSourceCount() > 100) {
					// check if the one of the files sources are in the same ip subnet as a udp server
					// which indicates that the server is advertising its own files
					bool bSourceServer = false;
					for (int i = 0; i < aservers.GetSize(); ++i) {
						if (aservers[i].m_nIP != 0) {
							if ((aservers[i].m_nIP & 0x00FFFFFF) == (pTempFile->GetClientID() & 0x00FFFFFF)) {
								bSourceServer = true;
								strDebug.AppendFormat(_T(" (Server: %s - Source: %s Hit)"), (LPCTSTR)ipstr(aservers[i].m_nIP), (LPCTSTR)ipstr(pTempFile->GetClientID()));
								break;
							}

							for (int j = 0; j < pTempFile->GetClients().GetSize(); ++j) {
								if ((aservers[i].m_nIP & 0x00FFFFFF) == (pTempFile->GetClients()[j].m_nIP & 0x00FFFFFF)) {
									bSourceServer = true;
									strDebug.AppendFormat(_T(" (Server: %s - Source: %s Hit)"), (LPCTSTR)ipstr(aservers[i].m_nIP), (LPCTSTR)ipstr(pTempFile->GetClients()[j].m_nIP));
									break;
								}
							}
						}
					}

					if (((GetED2KFileTypeID(pTempFile->GetFileName()) == ED2KFT_PROGRAM || GetED2KFileTypeID(pTempFile->GetFileName()) == ED2KFT_ARCHIVE)
							&& (uint64)pTempFile->GetFileSize() > 102400 && (uint64)pTempFile->GetFileSize() < 10485760)
							|| bSourceServer) {
						nSpamScore += SPAM_HEURISTIC_MAXHIT;
						nDbgHeuristic = SPAM_HEURISTIC_MAXHIT;
					} else {
						nSpamScore += SPAM_HEURISTIC_BASEHIT;
						nDbgHeuristic = SPAM_HEURISTIC_BASEHIT;
					}
				}
			}
			// 6 Sources
			bool bFound = false;
			if (IsValidSearchResultClientIPPort(pTempFile->GetClientID(), pTempFile->GetClientPort())
				&& !::IsLowID(pTempFile->GetClientID())
				&& m_mapKnownSpamSourcesIPs.Lookup(pTempFile->GetClientID(), bFound)) {
				strDebug.AppendFormat(_T(" (Sourceshit: %s)"), (LPCTSTR)ipstr(pTempFile->GetClientID()));
				nSpamScore += SPAM_SOURCE_HIT;
				nDbgSources = SPAM_SOURCE_HIT;
			} else {
				for (int i = 0; i != pTempFile->GetClients().GetSize(); ++i)
					if (pTempFile->GetClients()[i].m_nIP != 0 && m_mapKnownSpamSourcesIPs.Lookup(pTempFile->GetClients()[i].m_nIP, bFound)) {
						strDebug.AppendFormat(_T(" (Sources: %s)"), (LPCTSTR)ipstr(pTempFile->GetClients()[i].m_nIP));
						nSpamScore += SPAM_SOURCE_HIT;
						nDbgSources = SPAM_SOURCE_HIT;
						break;
						m_mapKnownSpamSourcesIPs.RemoveKey(pTempFile->GetClients()[i].m_nIP);
					}
			}
		}
	}

	bOldSpamStatus = pSearchFile->IsConsideredSpam();
	pSearchFile->SetSpamRating(uActionType == MarkAsNotSpam ? 0 : uActionType == MarkAsSpam ? SPAM_FILEHASH_HIT : nSpamScore);
	// If this item is marked as spam, then we need to update the parent and all its childs
	if ((uActionType == MarkAsNotSpam || uActionType == MarkAsSpam || pSearchFile->IsConsideredSpam(false)) && pParent) {
		pParent->SetSpamRating(pSearchFile->GetSpamRating()); // Mark parent as spam
		// Mark all children as spam
		const SearchList* list = GetSearchListForID(pParent->GetSearchID());
		for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
			CSearchFile* pCurFile = list->GetNext(pos);
			if (pCurFile->GetListParent() == pParent)
				pCurFile->SetSpamRating(pParent->GetSpamRating());
		}
	}

	if (uActionType == MarkAsNotSpam) {
		if (nSpamScore > 0)
			if (thePrefs.GetLogSpamRating())
				DebugLog(_T("Spamrating Result: %u. Details: Hash: %u, Name: %u, Size: %u, Server: %u, Sources: %u, Heuristic: %u, OnlySpamServers: %u. %s Filename: %s")
				, bSureNegative ? 0 : nSpamScore, nDbgFileHash, nDbgStrings, nDbgSize, nDbgServer, nDbgSources, nDbgHeuristic, nDbgOnlySpamServer, (LPCTSTR)EscPercent(strDebug), (LPCTSTR)EscPercent(pSearchFile->GetFileName()));
	} else
		DebugLog(_T("Marked file as No Spam, Old Rating: %u."), pSearchFile->GetSpamRating());

	// keep record about ratio of spam in UDP server results
	if (bOldSpamStatus != pSearchFile->IsConsideredSpam()) {
		const CSimpleArray<CSearchFile::SServer> &aservers = pTempFile->GetServers();
		for (int i = 0; i < aservers.GetSize(); ++i) {
			UDPServerRecord *pRecord;
			if (aservers[i].m_bUDPAnswer && m_mUDPServerRecords.Lookup(aservers[i].m_nIP, pRecord) && pRecord) {
				if (pSearchFile->IsConsideredSpam())
					++pRecord->m_nSpamResults;
				else {
					ASSERT(pRecord->m_nSpamResults > 0);
					--pRecord->m_nSpamResults;
				}
			}
		}
	} else if (dwFromUDPServerIP != 0 && pSearchFile->IsConsideredSpam()) {
		// files were a spam already, but server returned it in results - add it to server's spam stats
		const CUDPServerRecordMap::CPair *pair = m_mUDPServerRecords.PLookup(dwFromUDPServerIP);
		if (pair)
			++pair->value->m_nSpamResults;
	}

	TouchSearchModelSequence();
	if (bUpdate && outputwnd)
		outputwnd->UpdateSources(pParent ? pParent : pSearchFile, true);

	return true;
}

uint32 CSearchList::GetSpamFilenameRatings(const CSearchFile *pSearchFile, bool bMarkAsNotSpam)
{
	for (INT_PTR i = m_astrKnownSpamNames.GetCount(); --i >= 0;) {
		if (pSearchFile->GetFileName().CompareNoCase(m_astrKnownSpamNames[i]) == 0) {
			if (!bMarkAsNotSpam)
				return (pSearchFile->GetFileName().GetLength() <= 10) ? SPAM_SMALLFULLNAME_HIT : SPAM_FULLNAME_HIT;

			m_astrKnownSpamNames.RemoveAt(i);
		}
	}

	uint32 nResult = 0;
	if (!m_astrKnownSimilarSpamNames.IsEmpty() && !pSearchFile->GetNameWithoutKeyword().IsEmpty()) {
		const CString &cname(pSearchFile->GetNameWithoutKeyword());
		for (INT_PTR i = m_astrKnownSimilarSpamNames.GetCount(); --i >= 0;) {
			bool bRemove = false;
			if (cname == m_astrKnownSimilarSpamNames[i]) {
				if (!bMarkAsNotSpam)
					return (cname.GetLength() <= 10) ? SPAM_SMALLSIMILARNAME_HIT : SPAM_SIMILARNAME_HIT;

				bRemove = true;
			} else if (cname.GetLength() > 10
				&& (cname.GetLength() == m_astrKnownSimilarSpamNames[i].GetLength()
					|| cname.GetLength() / abs(cname.GetLength() - m_astrKnownSimilarSpamNames[i].GetLength()) >= 3))
			{
				uint32 nStringComp = LevenshteinDistance(cname, m_astrKnownSimilarSpamNames[i]);
				if (nStringComp != 0) {
					nStringComp = cname.GetLength() / nStringComp;
					if (nStringComp >= 3)
						if (bMarkAsNotSpam)
							bRemove = true;
						else if (nStringComp >= 6)
							nResult = SPAM_SIMILARNAME_NEARHIT;
						else
							nResult = max(nResult, SPAM_SIMILARNAME_FARHIT);
				}
			}
			if (bRemove)
				m_astrKnownSimilarSpamNames.RemoveAt(i);
		}
	}
	return nResult;
}

CString CSearchList::GetChildIndexKey(const CSearchFile* pParent, const CString& strFileName)
{
	CString strLowerName(strFileName);
	strLowerName.MakeLower();
	CString strKey;
	strKey.Format(_T("%p|%s"), (const void*)pParent, (LPCTSTR)strLowerName);
	return strKey;
}

SearchChildList* CSearchList::GetChildrenForParent(SearchListsStruct* pList, CSearchFile* pParent, bool bCreate)
{
	if (pList == NULL || pParent == NULL)
		return NULL;

	void* pValue = NULL;
	if (pList->m_mapChildrenByParent.Lookup(pParent, pValue))
		return static_cast<SearchChildList*>(pValue);

	if (!bCreate)
		return NULL;

	SearchChildList* pChildren = new SearchChildList;
	pList->m_mapChildrenByParent.SetAt(pParent, pChildren);
	return pChildren;
}

void CSearchList::DeleteChildLists(SearchListsStruct* pList)
{
	if (pList == NULL)
		return;

	POSITION pos = pList->m_mapChildrenByParent.GetStartPosition();
	while (pos != NULL) {
		void* pKey = NULL;
		void* pValue = NULL;
		pList->m_mapChildrenByParent.GetNextAssoc(pos, pKey, pValue);
		delete static_cast<SearchChildList*>(pValue);
	}
	pList->m_mapChildrenByParent.RemoveAll();
}

void CSearchList::AddChildToIndex(SearchListsStruct* pList, CSearchFile* pChild)
{
	if (pList == NULL || pChild == NULL || pChild->GetListParent() == NULL)
		return;

	CString strChildKey = GetChildIndexKey(pChild->GetListParent(), pChild->GetFileName());
	void* pExistingChild = NULL;
	if (!pList->m_mapChildrenByParentAndName.Lookup(strChildKey, pExistingChild))
		pList->m_mapChildrenByParentAndName.SetAt(strChildKey, pChild);

	SearchChildList* pChildren = GetChildrenForParent(pList, pChild->GetListParent(), true);
	if (pChildren != NULL && pChildren->Find(pChild) == NULL)
		pChildren->AddTail(pChild);
}

void CSearchList::RemoveChildFromIndex(SearchListsStruct* pList, const CSearchFile* pChild)
{
	if (pList == NULL || pChild == NULL || pChild->GetListParent() == NULL)
		return;

	CSearchFile* pParent = pChild->GetListParent();
	pList->m_mapChildrenByParentAndName.RemoveKey(GetChildIndexKey(pParent, pChild->GetFileName()));

	SearchChildList* pChildren = GetChildrenForParent(pList, pParent, false);
	if (pChildren == NULL)
		return;

	POSITION posChild = pChildren->Find(const_cast<CSearchFile*>(pChild));
	if (posChild != NULL)
		pChildren->RemoveAt(posChild);

	if (pChildren->IsEmpty()) {
		pList->m_mapChildrenByParent.RemoveKey(pParent);
		delete pChildren;
	}
}

void CSearchList::RecalculateParentFromChildren(SearchListsStruct* pList, CSearchFile* pParent)
{
	if (pList == NULL || pParent == NULL)
		return;

	SearchChildList* pChildren = GetChildrenForParent(pList, pParent, false);
	if (pChildren == NULL || pChildren->IsEmpty())
		return;

	UINT uAllChildrenSourceCount = 0;			// ED2K: Sum of all sources, Kad: the max. sources found.
	UINT uAllChildrenCompleteSourceCount = 0; // ED2K: Sum of all sources, Kad: the max. sources found.
	UINT uDifferentNames = 0; // Max known different names.
	UINT uPublishersKnown = 0; // Max publishers known, might be changed to median.
	UINT uTrustValue = 0; // Average trust value, might be changed to median.
	uint32 nPublishInfoTags = 0;
	const CSearchFile *bestEntry = NULL;
	bool bHasMultipleAICHHashes = false;
	CAICHHash aichHash;
	bool bAICHHashValid = false;

	for (POSITION pos = pChildren->GetHeadPosition(); pos != NULL;) {
		const CSearchFile *child = pChildren->GetNext(pos);
		if (child == NULL || child->GetListParent() != pParent)
			continue;

		const CFileIdentifier &fileid = child->GetFileIdentifierC();
		// Figure out if the children have different AICH hashes.
		if (fileid.HasAICHHash()) {
			if (bAICHHashValid && aichHash != fileid.GetAICHHash())
				bHasMultipleAICHHashes = true;
			else if (!bAICHHashValid) {
				aichHash = fileid.GetAICHHash();
				bAICHHashValid = true;
			}
		} else if (child->HasFoundMultipleAICH())
			bHasMultipleAICHHashes = true;

		if (pParent->IsKademlia()) {
			if (child->GetListChildCount() > uAllChildrenSourceCount)
				uAllChildrenSourceCount = child->GetListChildCount();
			uint32 u = child->GetKadPublishInfo();
			if (u != 0) {
				++nPublishInfoTags;
				uDifferentNames = max(uDifferentNames, (u >> 24) & 0xFF);
				uPublishersKnown = max(uPublishersKnown, (u >> 16) & 0xFF);
				uTrustValue += u & 0x0000FFFF;
			}
		} else {
			uAllChildrenSourceCount += child->GetListChildCount();
			uAllChildrenCompleteSourceCount += child->GetCompleteSourceCount();
		}

		if (bestEntry == NULL || child->GetListChildCount() > bestEntry->GetListChildCount())
			bestEntry = child;
	}

	if (bestEntry == NULL)
		return;

	pParent->SetFileSize(bestEntry->GetFileSize());
	pParent->SetAFileName(bestEntry->GetFileName());
	pParent->SetFileType(bestEntry->GetFileType());
	pParent->SetSourceCount(uAllChildrenSourceCount);
	pParent->SetCompleteSourceCount(uAllChildrenCompleteSourceCount);
	if (nPublishInfoTags > 0)
		uTrustValue /= nPublishInfoTags;
	pParent->SetKadPublishInfo(((uDifferentNames & 0xff) << 24) | ((uPublishersKnown & 0xff) << 16) | ((uTrustValue & 0xffff) << 0));
	// If all children have the same AICH hash, set the parent hash to it. Otherwise clear it. See KademliaSearchKeyword.
	if (bHasMultipleAICHHashes || !bAICHHashValid)
		pParent->GetFileIdentifier().ClearAICHHash();
	else
		pParent->GetFileIdentifier().SetAICHHash(aichHash);
}

void CSearchList::RebuildSearchListIndexes(SearchListsStruct* pList)
{
	if (pList == NULL)
		return;
	pList->m_mapParentsByHash.RemoveAll();
	pList->m_mapChildrenByParentAndName.RemoveAll();
	DeleteChildLists(pList);
	for (POSITION pos = pList->m_listSearchFiles.GetHeadPosition(); pos != NULL;) {
		CSearchFile* pFile = pList->m_listSearchFiles.GetNext(pos);
		if (pFile != NULL && pFile->GetListParent() == NULL)
			pList->m_mapParentsByHash[CSKey(pFile->GetFileHash())] = pFile;
	}
	for (POSITION pos = pList->m_listSearchFiles.GetHeadPosition(); pos != NULL;) {
		CSearchFile* pFile = pList->m_listSearchFiles.GetNext(pos);
		AddChildToIndex(pList, pFile);
	}
}


SearchListsStruct* CSearchList::GetSearchListStructForID(uint32 nSearchID, bool bCreate)
{
	for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
		SearchListsStruct *list = m_listFileLists.GetNext(pos);
		if (list->m_nSearchID == nSearchID)
			return list;
	}
	if (!bCreate)
		return NULL;
	SearchListsStruct *list = new SearchListsStruct;
	list->m_nSearchID = nSearchID;
	m_listFileLists.AddTail(list);
	return list;
}

SearchList* CSearchList::GetSearchListForID(uint32 nSearchID)
{
	SearchListsStruct *list = GetSearchListStructForID(nSearchID, true);
	return list != NULL ? &list->m_listSearchFiles : NULL;
}

const SearchChildList* CSearchList::GetSearchChildrenForParent(const CSearchFile* pParent)
{
	if (pParent == NULL)
		return NULL;

	SearchListsStruct *list = GetSearchListStructForID(pParent->GetSearchID(), false);
	if (list == NULL)
		return NULL;

	void *pChildren = NULL;
	return list->m_mapChildrenByParent.Lookup(const_cast<CSearchFile*>(pParent), pChildren) ? static_cast<const SearchChildList*>(pChildren) : NULL;
}

void CSearchList::SentUDPRequestNotification(uint32 nSearchID, uint32 dwServerIP)
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::SentUDPRequestNotification"));
	if (!mutationLock)
		return;

	if (nSearchID == m_nCurED2KSearchID)
		m_RequestedUDPAnswersCount[nSearchID] = (uint32)m_aCurED2KSentRequestsIPs.Add(dwServerIP) + 1;
	else
		ASSERT(0);

}


void CSearchList::MarkHashAsBlacklisted(CSKey hash)
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::MarkHashAsBlacklisted"));
	if (!mutationLock)
		return;

	if (!m_bSpamFilterLoaded)
		LoadSpamFilter();

	m_mapBlacklistedHashes[hash] = true;
	TouchSearchModelSequence();
}

bool CSearchList::IsFilenameManualBlacklisted(CSKey hash)
{
	if (!m_bSpamFilterLoaded)
		LoadSpamFilter();

	bool m_bIsBlacklisted = -1;
	if (m_mapBlacklistedHashes.Lookup(hash, m_bIsBlacklisted))
		return m_bIsBlacklisted;

	return false;
}

bool CSearchList::IsFilenameAutoBlacklisted(CString strFilename)
{
	CString strMatchedRule;
	if (!thePrefs.IsFilenameAutoBlacklisted(strFilename, &strMatchedRule))
		return false;

	if (thePrefs.GetBlacklistLog() && thePrefs.GetVerbose())
		AddDebugLogLine(false, _T("[AUTOMATIC BLACKLIST] File \"%s\" blacklisted by definition: %s"), (LPCTSTR)EscPercent(strFilename), (LPCTSTR)EscPercent(strMatchedRule));
	return true;
}

void CSearchList::RecalculateSpamRatings(uint32 nSearchID, bool bExpectHigher, bool bExpectLower, bool bRecalculateAll)
{
	if (bRecalculateAll) {
		QueueChunkedSpamRatingJob(nSearchID, bExpectHigher, bExpectLower, bRecalculateAll);
		return;
	}

	CSearchModelMutationLock mutationLock(this, _T("CSearchList::RecalculateSpamRatings"));
	if (!mutationLock)
		return;

	ASSERT(!(bExpectHigher && bExpectLower));
	ASSERT(m_bSpamFilterLoaded);

	if (!thePrefs.GetBlacklistAutomatic() && !thePrefs.GetBlacklistManual() && !thePrefs.IsSearchSpamFilterEnabled())
		return; // No need to recalculate spam ratings if blacklists or spam filter is not enabled.

	const SearchList *list = GetSearchListForID(nSearchID);
	if (!list || !list->GetCount()) // No list found or list is empty
		return;

	if (bRecalculateAll) {
		// As a first step, reset status of all items to prepare a clean recalculation.
		for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
			CSearchFile* pCurFile = list->GetNext(pos);
			// Skip if known. Because marking as known files has a priority over marking as spam/blacklisted. 
			if (pCurFile->GetKnownType() == CSearchFile::NotDetermined) {
				pCurFile->SetAutomaticBlacklisted(false);
				pCurFile->ClearAutomaticBlacklistEvaluation();
				pCurFile->SetSpamRating(0);
			}
		}

		// When bRecalculateAll is set, we need to check parents before child items as Automatic Blacklist logic needs.
		for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
			CSearchFile* pCurFile = list->GetNext(pos);
			if (!pCurFile->GetListParent() && pCurFile->GetKnownType() == CSearchFile::NotDetermined)
				DoSpamRating(pCurFile, false, Calculate, false, 0);
		}

		// Check child items
		for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
			CSearchFile* pCurFile = list->GetNext(pos);
			if (pCurFile->GetListParent() != NULL && pCurFile->GetKnownType() == CSearchFile::NotDetermined) {
				// If parent is marked as Automatic or Manual Blacklisted, copy this status and dont call DoSpamRating since this will already be listed as spam.
				if (thePrefs.GetBlacklistAutomatic() && pCurFile->GetListParent()->GetAutomaticBlacklisted())
					pCurFile->SetAutomaticBlacklisted(true);
				else if (thePrefs.GetBlacklistManual() && pCurFile->GetListParent()->GetManualBlacklisted())
					pCurFile->SetManualBlacklisted(true);
				else if (thePrefs.IsSearchSpamFilterEnabled() && pCurFile->GetListParent()->IsConsideredSpam(false))
					pCurFile->SetSpamRating(pCurFile->GetListParent()->GetSpamRating());
				else
					DoSpamRating(pCurFile, false, Calculate, false, 0);
			}
		}
	} else {
		for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
			CSearchFile* pCurFile = list->GetNext(pos);
			// Check only parents and only if we expect a status change
			if (((pCurFile->GetListParent() == NULL && !(pCurFile->IsConsideredSpam(false) && bExpectHigher) && !(!pCurFile->IsConsideredSpam(false) && bExpectLower))))
				DoSpamRating(pCurFile, false, Calculate, false, 0);
		}
	}

	theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.ReloadList(true, LSF_SELECTION);
}

void CSearchList::LoadSpamFilter()
{
	m_astrKnownSpamNames.RemoveAll();
	m_astrKnownSimilarSpamNames.RemoveAll();
	m_mapKnownSpamServerIPs.RemoveAll();
	m_mapKnownSpamSourcesIPs.RemoveAll();
	m_mapKnownSpamHashes.RemoveAll();
	m_mapBlacklistedHashes.RemoveAll();
	m_aui64KnownSpamSizes.RemoveAll();

	m_bSpamFilterLoaded = true;

	CSafeBufferedFile file;
	if (!CFileOpenD(file
		, thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + SPAMFILTER_FILENAME
		, CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyWrite
		, _T("Failed to load ") SPAMFILTER_FILENAME))
	{
		return;
	}
	::setvbuf(file.m_pStream, NULL, _IOFBF, kLargeMetFileBufferSize);

	try {
		uint8 header = file.ReadUInt8();
		if (header != MET_HEADER_I64TAGS) {
			file.Close();
			DebugLogError(_T("Failed to load searchspam.met, invalid first byte"));
			return;
		}
		unsigned nDbgFileHashPos = 0;

		for (uint32 i = file.ReadUInt32(); i > 0; --i) { //number of records
			CTag tag(file, false);
			switch (tag.GetNameID()) {
			case SP_FILEHASHSPAM:
				ASSERT(tag.IsHash());
				if (tag.IsHash())
					m_mapKnownSpamHashes[CSKey(tag.GetHash())] = true;
				break;
			case SP_FILEHASHNOSPAM:
				ASSERT(tag.IsHash());
				if (tag.IsHash()) {
					m_mapKnownSpamHashes[CSKey(tag.GetHash())] = false;
					++nDbgFileHashPos;
				}
				break;
			case SP_FILEFULLNAME:
				ASSERT(tag.IsStr());
				if (tag.IsStr())
					m_astrKnownSpamNames.Add(tag.GetStr());
				break;
			case SP_FILESIMILARNAME:
				ASSERT(tag.IsStr());
				if (tag.IsStr())
					m_astrKnownSimilarSpamNames.Add(tag.GetStr());
				break;
			case SP_FILESOURCEIP:
				ASSERT(tag.IsInt());
				if (tag.IsInt())
					m_mapKnownSpamSourcesIPs[tag.GetInt()] = true;
				break;
			case SP_FILESERVERIP:
				ASSERT(tag.IsInt());
				if (tag.IsInt())
					m_mapKnownSpamServerIPs[tag.GetInt()] = true;
				break;
			case SP_FILESIZE:
				ASSERT(tag.IsInt64());
				if (tag.IsInt64())
					m_aui64KnownSpamSizes.Add(tag.GetInt64());
				break;
			case SP_UDPSERVERSPAMRATIO:
				ASSERT(tag.IsBlob() && tag.GetBlobSize() == 12);
				if (tag.IsBlob() && tag.GetBlobSize() == 12) {
					const BYTE *pBuffer = tag.GetBlob();
					UDPServerRecord *pRecord = new UDPServerRecord;
					pRecord->m_nResults = PeekUInt32(&pBuffer[4]);
					pRecord->m_nSpamResults = PeekUInt32(&pBuffer[8]);
					m_mUDPServerRecords[PeekUInt32(&pBuffer[0])] = pRecord;
					int nRatio;
					if (pRecord->m_nResults >= pRecord->m_nSpamResults && pRecord->m_nResults > 0)
						nRatio = (pRecord->m_nSpamResults * 100) / pRecord->m_nResults;
					else
						nRatio = 100;
					DEBUG_ONLY(DebugLog(_T("UDP Server Spam Record: IP: %s, Results: %u, SpamResults: %u, Ratio: %u")
						, (LPCTSTR)ipstr(PeekUInt32(&pBuffer[0])), pRecord->m_nResults, pRecord->m_nSpamResults, nRatio));
				}
				break;
			case SP_BLACKLISTED:
				ASSERT(tag.IsHash());
				if (tag.IsHash())
					m_mapBlacklistedHashes[CSKey(tag.GetHash())] = true;
				break;
			default:
				ASSERT(0);
			}
		}
		file.Close();

		DebugLog(_T("Loaded search Spam Filter. Entries - ServerIPs: %u, SourceIPs, %u, hashes: %u, PositiveHashes: %i, FileSizes: %u, FullNames: %u, SimilarNames: %u, Blacklisted hashes: %u")
			, (unsigned)m_mapKnownSpamSourcesIPs.GetCount()
			, (unsigned)m_mapKnownSpamServerIPs.GetCount()
			, (unsigned)m_mapKnownSpamHashes.GetCount() - nDbgFileHashPos
			, nDbgFileHashPos
			, (unsigned)m_aui64KnownSpamSizes.GetCount()
			, (unsigned)m_astrKnownSpamNames.GetCount()
			, (unsigned)m_astrKnownSimilarSpamNames.GetCount()
			, (unsigned)m_mapBlacklistedHashes.GetCount());
	} catch (CFileException *ex) {
		if (ex->m_cause == CFileException::endOfFile)
			DebugLogError(_T("Failed to load searchspam.met, file is corrupt or has a different version"));
		else
			DebugLogError(_T("Failed to load searchspam.met%s"), (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
		ex->Delete();
	}
}

void CSearchList::SaveSpamFilter()
{
	if (ShouldSkipSearchPersistenceForManualLeakDump())
		return;

	if (!m_bSpamFilterLoaded)
		return;

	m_nLastSaved = ::GetTickCount();

	const CString& sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	const LONG lGeneration = ::InterlockedIncrement(&m_lSpamFilterSaveGeneration);
	try {
		CSafeMemFile file;
		uint32 nCount = 0;
		file.WriteUInt8(MET_HEADER_I64TAGS);
		file.WriteUInt32(nCount);

		for (INT_PTR i = 0; i < m_astrKnownSpamNames.GetCount(); ++i) {
			CTag tag(SP_FILEFULLNAME, m_astrKnownSpamNames[i]);
			tag.WriteNewEd2kTag(file, UTF8strOptBOM);
			++nCount;
		}

		for (INT_PTR i = 0; i < m_astrKnownSimilarSpamNames.GetCount(); ++i) {
			CTag tag(SP_FILESIMILARNAME, m_astrKnownSimilarSpamNames[i]);
			tag.WriteNewEd2kTag(file, UTF8strOptBOM);
			++nCount;
		}

		for (INT_PTR i = 0; i < m_aui64KnownSpamSizes.GetCount(); ++i) {
			CTag tag(SP_FILESIZE, m_aui64KnownSpamSizes[i], true);
			tag.WriteNewEd2kTag(file);
			++nCount;
		}

		for (const CMap<CSKey, const CSKey&, bool, bool>::CPair *pair = m_mapKnownSpamHashes.PGetFirstAssoc(); pair != NULL; pair = m_mapKnownSpamHashes.PGetNextAssoc(pair)) {
			CTag tag((pair->value ? SP_FILEHASHSPAM : SP_FILEHASHNOSPAM), (BYTE*)pair->key.m_key);
			tag.WriteNewEd2kTag(file);
			++nCount;
		}

		for (const CSpammerIPMap::CPair *pair = m_mapKnownSpamServerIPs.PGetFirstAssoc(); pair != NULL; pair = m_mapKnownSpamServerIPs.PGetNextAssoc(pair)) {
			CTag tag(SP_FILESERVERIP, pair->key);
			tag.WriteNewEd2kTag(file);
			++nCount;
		}

		for (const CSpammerIPMap::CPair *pair = m_mapKnownSpamSourcesIPs.PGetFirstAssoc(); pair != NULL; pair = m_mapKnownSpamSourcesIPs.PGetNextAssoc(pair)) {
			CTag tag(SP_FILESOURCEIP, pair->key);
			tag.WriteNewEd2kTag(file);
			++nCount;
		}

		for (const CUDPServerRecordMap::CPair *pair = m_mUDPServerRecords.PGetFirstAssoc(); pair != NULL; pair = m_mUDPServerRecords.PGetNextAssoc(pair)) {
			const uint32 buf[3] = { pair->key, pair->value->m_nResults, pair->value->m_nSpamResults };
			CTag tag(SP_UDPSERVERSPAMRATIO, sizeof(buf), (const BYTE*)buf);
			tag.WriteNewEd2kTag(file);
			++nCount;
		}

		for (const CMap<CSKey, const CSKey&, bool, bool>::CPair* pair = m_mapBlacklistedHashes.PGetFirstAssoc(); pair != NULL; pair = m_mapBlacklistedHashes.PGetNextAssoc(pair)) {
			CTag tag(SP_BLACKLISTED, (BYTE*)pair->key.m_key);
			tag.WriteNewEd2kTag(file);
			++nCount;
		}

		file.Seek(1ull, CFile::begin);
		file.WriteUInt32(nCount);

		AsyncDiskWriteData* pData = new AsyncDiskWriteData;
		pData->lGeneration = lGeneration;
		pData->plGeneration = &m_lSpamFilterSaveGeneration;
		pData->strTempPath = sConfDir + SPAMFILTER_FILENAME_TMP;
		pData->strFinalPath = sConfDir + SPAMFILTER_FILENAME;
		pData->strLogName = SPAMFILTER_FILENAME;
		pData->strPayloadName = _T("search-spam-filter");
		pData->eConflictPolicy = AsyncDiskWriteConflictLastSnapshotWins;
		pData->eReplacePolicy = AsyncDiskWriteReplaceFinal;
		CopySearchMemFileToAsyncDiskData(file, *pData);
		CPartFileWriteThread::QueueOrWriteDiskSnapshot(pData);
		DebugLog(_T("Queued searchspam.met save, wrote %u records"), nCount);
	} catch (CFileException *ex) {
		DebugLogError(_T("Failed to save searchspam.met%s"), (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
		ex->Delete();
	} catch (...) {
		DebugLogError(_T("Failed to save searchspam.met"));
	}
}

void CSearchList::StoreSearches()
{
	if (ShouldSkipSearchPersistenceForManualLeakDump())
		return;
	if (!m_bStoredSearchStartupLoadCompleted) {
		AddDebugLogLine(DLP_LOW, false, _T("Skipping stored searches save before startup load completed. active=%u\n"), m_bStoredSearchStartupLoadActive ? 1U : 0U);
		return;
	}

	m_nLastSaved = ::GetTickCount();

	// Store a snapshot. The disk writer keeps the last complete snapshot and does not overwrite startup data before load completion.
	const CString& sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	CSafeMemFile file;
	try {
		file.WriteUInt8(MET_HEADER_I64TAGS);
		file.WriteUInt8(STOREDSEARCHES_VERSION);
		// count how many (if any) open searches we have which are GUI related
		uint16 nCount = 0;
		for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
			const SearchListsStruct *pSl = m_listFileLists.GetNext(pos);
			const SSearchParams *pParams = theApp.emuledlg->searchwnd->GetSearchParamsBySearchID(pSl->m_nSearchID);
			nCount += static_cast<uint16>(pParams != NULL);
		}
		file.WriteUInt16(nCount);
		if (nCount > 0)
			for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
				const SearchListsStruct *pSl = m_listFileLists.GetNext(pos);
				const SSearchParams *pParams = theApp.emuledlg->searchwnd->GetSearchParamsBySearchID(pSl->m_nSearchID);
				if (pParams != NULL) {
					pParams->StorePartially(file);

					uint32 uCount = 0;
					for (POSITION pos2 = pSl->m_listSearchFiles.GetHeadPosition(); pos2 != NULL;)
						uCount += static_cast<uint32>(!pSl->m_listSearchFiles.GetNext(pos2)->m_bNowrite);

					file.WriteUInt32(uCount);
					for (POSITION pos2 = pSl->m_listSearchFiles.GetHeadPosition(); pos2 != NULL;) {
						CSearchFile *sf = pSl->m_listSearchFiles.GetNext(pos2);
						if (sf->m_bNowrite)
							continue;

						if (pParams->bClientSharedFiles) {
							CSafeMemFile recordFile;
							sf->StoreToFile(recordFile);
							const uint32 uRecordSize = static_cast<uint32>(recordFile.GetLength());
							file.WriteUInt32(uRecordSize);
							if (uRecordSize != 0)
								file.Write(recordFile.GetBuffer(), uRecordSize);
						} else
							sf->StoreToFile(file);
					}
				}
			}

		AsyncDiskWriteData* pData = new AsyncDiskWriteData;
		pData->lGeneration = ::InterlockedIncrement(&m_lStoredSearchesSaveGeneration);
		pData->plGeneration = &m_lStoredSearchesSaveGeneration;
		pData->strTempPath = sConfDir + STOREDSEARCHES_FILENAME_TMP;
		pData->strFinalPath = sConfDir + STOREDSEARCHES_FILENAME;
		pData->strLogName = STOREDSEARCHES_FILENAME;
		pData->strPayloadName = _T("stored-searches");
		pData->eConflictPolicy = AsyncDiskWriteConflictLastSnapshotWins;
		pData->eReplacePolicy = AsyncDiskWriteReplaceFinal;
		CopySearchMemFileToAsyncDiskData(file, *pData);
		CPartFileWriteThread::QueueOrWriteDiskSnapshot(pData);
		DebugLog(_T("Queued %u open search(es) for restoring on next start"), nCount);
	} catch (CFileException *ex) {
		DebugLogError(_T("Failed to save %s%s"), STOREDSEARCHES_FILENAME, (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
		ex->Delete();
	} catch (...) {
		DebugLogError(_T("Failed to save %s"), STOREDSEARCHES_FILENAME);
	}
}

void CSearchList::CancelStartupLoad()
{
	if (m_pStoredSearchStartupLoadParams != NULL && m_bStoredSearchStartupLoadCurrentDeleteParams)
		delete m_pStoredSearchStartupLoadParams;
	m_pStoredSearchStartupLoadParams = NULL;
	m_bDeferSearchListUpdates = false;
	m_uStoredSearchStartupLoadCurrentRemainingFiles = 0;
	m_uStoredSearchStartupLoadCurrentTotalFiles = 0;
	m_bStoredSearchStartupLoadCurrentDeleteParams = false;
	m_bStoredSearchStartupLoadCurrentIsLastTab = false;

	m_bStoredSearchStartupLoadActive = false;
	m_bStoredSearchStartupLoadCompleted = true;
	m_nStoredSearchStartupLoadNextSearchID = 0;
	m_uStoredSearchStartupLoadTotalSearches = 0;
	m_uStoredSearchStartupLoadLoadedSearches = 0;
	m_uStoredSearchStartupLoadLoadedFiles = 0;
	m_bStoredSearchStartupLoadLoadedVisibleSearch = false;
	m_bStoredSearchStartupLoadReloadedVisibleSearch = false;
	m_dwStoredSearchStartupLoadStartedTick = 0;
	m_dwStoredSearchStartupLoadLastProgressTick = 0;
	m_lStoredSearchStartupLoadGeneration = 0;
	m_uStoredSearchStartupLoadCancellationToken = 0;
}

void CSearchList::FinishStartupLoad()
{
	if (m_pStoredSearchStartupLoadParams != NULL && m_bStoredSearchStartupLoadCurrentDeleteParams)
		delete m_pStoredSearchStartupLoadParams;
	m_pStoredSearchStartupLoadParams = NULL;
	m_bDeferSearchListUpdates = false;
	m_uStoredSearchStartupLoadCurrentRemainingFiles = 0;
	m_uStoredSearchStartupLoadCurrentTotalFiles = 0;
	m_bStoredSearchStartupLoadCurrentDeleteParams = false;
	m_bStoredSearchStartupLoadCurrentIsLastTab = false;

	if (outputwnd && m_bStoredSearchStartupLoadLoadedVisibleSearch && !m_bStoredSearchStartupLoadReloadedVisibleSearch) {
		const bool bWaitForStartupKnownTypes = theApp.emuledlg != NULL && !theApp.emuledlg->IsStartupSearchKnownTypesRefreshComplete();
		if (!bWaitForStartupKnownTypes)
			outputwnd->ReloadList(false, LSF_SELECTION);
	}

	const uint32 nNextSearchID = m_nStoredSearchStartupLoadNextSearchID + 1;
	Kademlia::CSearchManager::SetNextSearchID(nNextSearchID);
	if (theApp.emuledlg != NULL && theApp.emuledlg->searchwnd != NULL)
		theApp.emuledlg->searchwnd->SetNextSearchID(0x80000000u + nNextSearchID);

	m_bStoredSearchStartupLoadActive = false;
	m_bStoredSearchStartupLoadCompleted = true;
	m_lStoredSearchStartupLoadGeneration = 0;
	m_uStoredSearchStartupLoadCancellationToken = 0;

	AddDebugLogLine(DLP_LOW, false, _T("Stored searches startup load completed. searches=%u files=%u elapsed=%u\n"), m_uStoredSearchStartupLoadLoadedSearches, m_uStoredSearchStartupLoadLoadedFiles, static_cast<DWORD>(::GetTickCount() - m_dwStoredSearchStartupLoadStartedTick));
}

bool CSearchList::QueueStartupStoredSearchesLoadWorker()
{
	if (theApp.IsStartupMetadataDomainReady(CemuleApp::StartupMetadataStoredSearches)) {
		m_bStoredSearchStartupLoadActive = false;
		m_bStoredSearchStartupLoadCompleted = true;
		return true;
	}
	return theApp.BeginStartupStoredSearchesLoad();
}

bool CSearchList::LoadStartupStoredSearchesForWorker(SStartupStoredSearchesLoadResult &result)
{
	result.bSuccess = false;
	result.dwLastError = 0;
	result.strStage = _T("load-started");

	const CString strFileName(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + STOREDSEARCHES_FILENAME);
	CFileStatus status;
	if (!CFile::GetStatus(strFileName, status)) {
		result.bSuccess = true;
		result.strStage = _T("file-not-found");
		return true;
	}

	CSafeBufferedFile file;
	if (!CFileOpenD(file, strFileName, CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyWrite, _T("Failed to load ") STOREDSEARCHES_FILENAME)) {
		result.dwLastError = ::GetLastError();
		result.strStage = _T("open-failed");
		return false;
	}

	::setvbuf(file.m_pStream, NULL, _IOFBF, kLargeMetFileBufferSize);

	try {
		const uint8 header = file.ReadUInt8();
		if (header != MET_HEADER_I64TAGS) {
			result.dwLastError = ERROR_INVALID_DATA;
			result.strStage = _T("invalid-header");
			return false;
		}

		const uint8 version = file.ReadUInt8();
		if (version < 101 || version > STOREDSEARCHES_VERSION) {
			result.dwLastError = ERROR_INVALID_DATA;
			result.strStage = _T("invalid-version");
			return false;
		}

		const ULONGLONG ullStoredSearchesFileLength = file.GetLength();
		result.uTotalSearches = file.ReadUInt16();
		PublishStartupLoadWorkerProgress(ScaleStoredSearchLoadByteProgress(file.GetPosition(), ullStoredSearchesFileLength), kStartupStoredSearchLoadProgressUnits, 0);
		for (UINT uSearch = 0; uSearch < result.uTotalSearches; ++uSearch) {
			if (theApp.IsClosing() || (result.lGeneration != 0 && result.uCancellationToken != 0 && theApp.IsStartupMetadataLoadCancelled(CemuleApp::StartupMetadataStoredSearches, result.lGeneration, result.uCancellationToken))) {
				result.dwLastError = ERROR_CANCELLED;
				result.strStage = _T("cancelled");
				return false;
			}
			SStartupStoredSearchTab *pTab = new SStartupStoredSearchTab();
			result.vecTabs.push_back(pTab);
			pTab->pParams = new SSearchParams(file, version);
			pTab->uStoredFileCount = file.ReadUInt32();
			pTab->bLastTab = (uSearch + 1u) >= result.uTotalSearches;
			if (pTab->uStoredFileCount != 0)
				pTab->vecFiles.reserve(static_cast<size_t>(pTab->uStoredFileCount < 4096 ? pTab->uStoredFileCount : 4096));

			for (uint32 uFile = 0; uFile < pTab->uStoredFileCount; ++uFile) {
				if ((uFile & 0xff) == 0 && (theApp.IsClosing() || (result.lGeneration != 0 && result.uCancellationToken != 0 && theApp.IsStartupMetadataLoadCancelled(CemuleApp::StartupMetadataStoredSearches, result.lGeneration, result.uCancellationToken)))) {
					result.dwLastError = ERROR_CANCELLED;
					result.strStage = _T("cancelled");
					return false;
				}
				CSearchFile *pFile = NULL;
				try {
					if (pTab->pParams->bClientSharedFiles && version >= 103) {
						const uint32 uRecordSize = file.ReadUInt32();
						if (uRecordSize == 0 || uRecordSize > kMaxStoredSearchRecordSize)
							AfxThrowFileException(CFileException::endOfFile, 0, (LPCTSTR)strFileName);
						std::vector<BYTE> recordData;
						recordData.resize(uRecordSize);
						file.Read(&recordData[0], uRecordSize);
						CSafeMemFile recordFile(&recordData[0], uRecordSize);
						pFile = new CSearchFile(recordFile, true, 0, 0, 0, NULL, pTab->pParams->eType == SearchTypeKademlia);
					}
					else
						pFile = new CSearchFile(file, true, 0, 0, 0, NULL, pTab->pParams->eType == SearchTypeKademlia);

					SSearchIngestRecord record;
					if (!BuildSearchIngestRecord(pFile, record, true))
						AfxThrowFileException(CFileException::endOfFile, 0, (LPCTSTR)strFileName);
					pTab->vecFiles.push_back(record);
					delete pFile;
					pFile = NULL;
				}
				catch (...) {
					delete pFile;
					throw;
				}
				++result.uTotalFiles;
				if ((result.uTotalFiles & 0xff) == 0)
					PublishStartupLoadWorkerProgress(ScaleStoredSearchLoadByteProgress(file.GetPosition(), ullStoredSearchesFileLength), kStartupStoredSearchLoadProgressUnits, result.uTotalFiles);
			}
			PublishStartupLoadWorkerProgress(ScaleStoredSearchLoadByteProgress(file.GetPosition(), ullStoredSearchesFileLength), kStartupStoredSearchLoadProgressUnits, result.uTotalFiles);

		}

		PublishStartupLoadWorkerProgress(kStartupStoredSearchLoadProgressUnits, kStartupStoredSearchLoadProgressUnits, result.uTotalFiles);
		result.bSuccess = true;
		result.dwLastError = 0;
		result.strStage = _T("stage-completed");
		return true;
	}
	catch (CFileException *ex) {
		result.dwLastError = (ex != NULL && ex->m_cause == CFileException::endOfFile) ? ERROR_HANDLE_EOF : ERROR_INVALID_DATA;
		result.strStage = _T("parse-failed");
		if (ex != NULL)
			ex->Delete();
	}
	catch (...) {
		result.dwLastError = ERROR_INVALID_DATA;
		result.strStage = _T("parse-failed");
	}
	return false;
}

void CSearchList::DeleteStartupStoredSearchesLoadResult(SStartupStoredSearchesLoadResult *pResult)
{
	if (pResult == NULL)
		return;
	for (size_t i = 0; i < pResult->vecTabs.size(); ++i) {
		SStartupStoredSearchTab *pTab = pResult->vecTabs[i];
		if (pTab == NULL)
			continue;
		pTab->vecFiles.clear();
		if (pTab->pParams != NULL && (!pTab->bTabCreated || pTab->bDeleteParams))
			delete pTab->pParams;
		pTab->pParams = NULL;
		delete pTab;
	}
	pResult->vecTabs.clear();
	delete pResult;
}

bool CSearchList::StartStartupStoredSearchApplyTab(SStartupStoredSearchesLoadResult &result, SStartupStoredSearchTab &tab)
{
	if (tab.pParams == NULL)
		return false;
	if (theApp.emuledlg == NULL || theApp.emuledlg->searchwnd == NULL)
		return false;
	if (!theApp.GuardModelMutation(CemuleApp::ModelMutationSearchList, _T("CSearchList::StartStartupStoredSearchApplyTab")))
		return false;

	{
		CSingleLock searchLock(GetSearchModelLock(), TRUE);
		uint32 nCandidateSearchID = ++m_nStoredSearchStartupLoadNextSearchID;
		while (GetSearchListStructForID(nCandidateSearchID, false) != NULL || theApp.emuledlg->searchwnd->GetSearchParamsBySearchID(nCandidateSearchID) != NULL)
			nCandidateSearchID = ++m_nStoredSearchStartupLoadNextSearchID;

		tab.nAssignedSearchID = nCandidateSearchID;
		tab.pParams->dwSearchID = nCandidateSearchID;
		tab.bLastTab = (result.uNextTab + 1u) >= result.vecTabs.size();
		m_bStoredSearchStartupLoadLoadedVisibleSearch = true;

		const CString &strResultType(tab.pParams->strFileType);
		NewSearch(NULL, (strResultType == _T(ED2KFTSTR_PROGRAM) ? CString() : strResultType), tab.pParams);
	}

	tab.bDeleteParams = !theApp.emuledlg->searchwnd->CreateNewTab(tab.pParams, false, tab.bLastTab);
	tab.bTabCreated = true;
	m_pStoredSearchStartupLoadParams = tab.pParams;
	m_bStoredSearchStartupLoadCurrentDeleteParams = tab.bDeleteParams;
	m_bStoredSearchStartupLoadCurrentIsLastTab = tab.bLastTab;
	m_uStoredSearchStartupLoadCurrentRemainingFiles = static_cast<uint32>(tab.vecFiles.size());
	m_uStoredSearchStartupLoadCurrentTotalFiles = static_cast<uint32>(tab.vecFiles.size());

	if (theApp.emuledlg->searchwnd->m_pwndResults != NULL)
		theApp.emuledlg->searchwnd->m_pwndResults->RefreshSearchTabActivityAnimation();
	if (!tab.bDeleteParams) {
		CSingleLock searchLock(GetSearchModelLock(), TRUE);
		m_foundFilesCount[tab.pParams->dwSearchID] = 0;
		m_foundSourcesCount[tab.pParams->dwSearchID] = 0;
		GetSearchListForID(tab.pParams->dwSearchID);
	}
	else
		ASSERT(0);

	return true;
}

bool CSearchList::QueueStartupStoredSearchTabIngest(SStartupStoredSearchTab &tab)
{
	if (tab.bDeleteParams || tab.pParams == NULL || tab.vecFiles.empty())
		return true;

	const uint32 nSearchID = tab.pParams->dwSearchID;
	if (nSearchID == 0)
		return false;

	for (size_t i = 0; i < tab.vecFiles.size(); ++i)
		tab.vecFiles[i].m_nSearchID = nSearchID;

	std::vector<SSearchIngestRecord> records;
	records.swap(tab.vecFiles);
	const uint32 uQueued = static_cast<uint32>(records.size());
	if (!QueueStoredSearchIngestPrepareJob(records, nSearchID, CString(), tab.pParams->bClientSharedFiles, 0, true, true)) {
		tab.vecFiles.swap(records);
		return false;
	}

	tab.bIngestQueued = true;
	tab.uQueuedFileCount = uQueued;
	m_uStoredSearchStartupLoadCurrentRemainingFiles = 0;
	m_uStoredSearchStartupLoadCurrentTotalFiles = uQueued;
	return true;
}

void CSearchList::CompleteStartupStoredSearchApplyTab(SStartupStoredSearchTab &tab)
{
	SSearchParams *pParams = tab.pParams;
	const bool bDeleteParams = tab.bDeleteParams;

	if (!bDeleteParams && pParams != NULL && outputwnd) {
		if (tab.bLastTab) {
			const bool bWaitForStartupKnownTypes = theApp.emuledlg != NULL && !theApp.emuledlg->IsStartupSearchKnownTypesRefreshComplete();
			if (bWaitForStartupKnownTypes)
				outputwnd->UpdateTabHeader(pParams->dwSearchID, EMPTY, false);
			else if (theApp.emuledlg != NULL && theApp.emuledlg->IsStartupLoadingDialogVisible())
				outputwnd->QueueDeferredReload(false, LSF_SELECTION, 500);
			else
				outputwnd->ReloadList(false, LSF_SELECTION);
			m_bStoredSearchStartupLoadReloadedVisibleSearch = !bWaitForStartupKnownTypes;
		}
		else
			outputwnd->UpdateTabHeader(pParams->dwSearchID, EMPTY, false);
	}

	tab.vecFiles.clear();
	m_pStoredSearchStartupLoadParams = NULL;
	m_uStoredSearchStartupLoadCurrentRemainingFiles = 0;
	m_uStoredSearchStartupLoadCurrentTotalFiles = 0;
	m_bStoredSearchStartupLoadCurrentDeleteParams = false;
	m_bStoredSearchStartupLoadCurrentIsLastTab = false;
	++m_uStoredSearchStartupLoadLoadedSearches;

	if (theApp.emuledlg != NULL && theApp.emuledlg->searchwnd != NULL && theApp.emuledlg->searchwnd->m_pwndResults != NULL)
		theApp.emuledlg->searchwnd->m_pwndResults->RefreshSearchTabActivityAnimation();

	if (bDeleteParams)
		delete pParams;
	tab.pParams = NULL;
}

bool CSearchList::ApplyStartupStoredSearchesLoadResult(SStartupStoredSearchesLoadResult *pResult, size_t uMaxFilesPerSlice, UINT& uAppliedInSlice, INT_PTR& iRemaining)
{
	uAppliedInSlice = 0;
	iRemaining = 0;
	if (pResult == NULL)
		return true;

	if (!pResult->bSuccess) {
		m_uStoredSearchStartupLoadTotalSearches = pResult->uTotalSearches;
		m_uStoredSearchStartupLoadLoadedSearches = 0;
		m_uStoredSearchStartupLoadLoadedFiles = 0;
		m_bStoredSearchStartupLoadActive = false;
		m_bStoredSearchStartupLoadCompleted = true;
		m_lStoredSearchStartupLoadGeneration = 0;
		m_uStoredSearchStartupLoadCancellationToken = 0;
		if (pResult->lGeneration != 0 && pResult->uCancellationToken != 0)
			theApp.CompleteStartupMetadataLoad(CemuleApp::StartupMetadataStoredSearches, pResult->lGeneration, pResult->uCancellationToken, false, pResult->dwLastError, pResult->strStage.IsEmpty() ? _T("async-stored-searches-load-failed") : (LPCTSTR)pResult->strStage);
		return true;
	}

	if (!pResult->bApplyStarted) {
		if (pResult->lGeneration != 0 && pResult->uCancellationToken != 0)
			theApp.SetStartupMetadataStateApplying(CemuleApp::StartupMetadataStoredSearches, pResult->lGeneration, pResult->uCancellationToken, pResult->strStage);
		m_bStoredSearchStartupLoadActive = true;
		m_bStoredSearchStartupLoadCompleted = false;
		m_lStoredSearchStartupLoadGeneration = pResult->lGeneration;
		m_uStoredSearchStartupLoadCancellationToken = pResult->uCancellationToken;
		m_uStoredSearchStartupLoadTotalSearches = pResult->uTotalSearches;
		m_uStoredSearchStartupLoadLoadedSearches = 0;
		m_uStoredSearchStartupLoadLoadedFiles = 0;
		m_bStoredSearchStartupLoadLoadedVisibleSearch = false;
		m_bStoredSearchStartupLoadReloadedVisibleSearch = false;
		m_dwStoredSearchStartupLoadLastProgressTick = 0;
		if (m_dwStoredSearchStartupLoadStartedTick == 0)
			m_dwStoredSearchStartupLoadStartedTick = ::GetTickCount();
		pResult->bApplyStarted = true;
	}

	const size_t uBudget = uMaxFilesPerSlice == 0 ? static_cast<size_t>(-1) : uMaxFilesPerSlice;
	const DWORD dwSliceStart = ::GetTickCount();
	while (pResult->uNextTab < pResult->vecTabs.size()) {
		SStartupStoredSearchTab *pTab = pResult->vecTabs[pResult->uNextTab];
		if (pTab == NULL) {
			++pResult->uNextTab;
			pResult->uNextFile = 0;
			continue;
		}

		if (!pTab->bTabCreated && !StartStartupStoredSearchApplyTab(*pResult, *pTab)) {
			m_bStoredSearchStartupLoadActive = false;
			m_bStoredSearchStartupLoadCompleted = true;
			m_lStoredSearchStartupLoadGeneration = 0;
			m_uStoredSearchStartupLoadCancellationToken = 0;
			if (pResult->lGeneration != 0 && pResult->uCancellationToken != 0)
				theApp.CompleteStartupMetadataLoad(CemuleApp::StartupMetadataStoredSearches, pResult->lGeneration, pResult->uCancellationToken, false, ERROR_INVALID_HANDLE, _T("async-stored-searches-apply-tab"));
			return true;
		}

		if (!pTab->bIngestQueued && pResult->uNextFile == 0 && !pTab->bDeleteParams && !pTab->vecFiles.empty()) {
			if (QueueStartupStoredSearchTabIngest(*pTab)) {
				pResult->uNextFile = pTab->uQueuedFileCount;
				m_uStoredSearchStartupLoadLoadedFiles += pTab->uQueuedFileCount;
				uAppliedInSlice += pTab->uQueuedFileCount;
			}
		}

		if (pTab->bIngestQueued) {
			if (HasPendingSearchProcessing(pTab->nAssignedSearchID)) {
				iRemaining = 1;
				for (size_t i = pResult->uNextTab + 1; i < pResult->vecTabs.size(); ++i) {
					const SStartupStoredSearchTab *pRemainingTab = pResult->vecTabs[i];
					if (pRemainingTab != NULL)
						iRemaining += static_cast<INT_PTR>(pRemainingTab->vecFiles.size());
				}
				m_uStoredSearchStartupLoadCurrentRemainingFiles = 1;
				return false;
			}
		}
		else {
			CScopedSearchListUpdateDeferral deferUpdates(m_bDeferSearchListUpdates);
			while (pResult->uNextFile < pTab->vecFiles.size()) {
				if (!pTab->bDeleteParams) {
					SSearchIngestRecord record(pTab->vecFiles[pResult->uNextFile]);
					record.m_nSearchID = pTab->pParams->dwSearchID;
					CSearchFile *pFile = CreateSearchFileFromIngestRecord(record);
					if (pFile != NULL)
						AddToList(pFile, pTab->pParams->bClientSharedFiles, 0, true);
				}

				++pResult->uNextFile;
				++m_uStoredSearchStartupLoadLoadedFiles;
				++uAppliedInSlice;
				if (uAppliedInSlice >= uBudget || (uAppliedInSlice != 0 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetStartupApply))) {
					m_uStoredSearchStartupLoadCurrentRemainingFiles = static_cast<uint32>(pTab->vecFiles.size() - pResult->uNextFile);
					for (size_t i = pResult->uNextTab; i < pResult->vecTabs.size(); ++i) {
						const SStartupStoredSearchTab *pRemainingTab = pResult->vecTabs[i];
						if (pRemainingTab == NULL)
							continue;
						iRemaining += static_cast<INT_PTR>(pRemainingTab->vecFiles.size() - (i == pResult->uNextTab ? pResult->uNextFile : 0));
					}
					return false;
				}
			}
		}

		CompleteStartupStoredSearchApplyTab(*pTab);
		delete pTab;
		pResult->vecTabs[pResult->uNextTab] = NULL;
		++pResult->uNextTab;
		pResult->uNextFile = 0;
	}

	FinishStartupLoad();
	if (pResult->lGeneration != 0 && pResult->uCancellationToken != 0)
		theApp.CompleteStartupMetadataLoad(CemuleApp::StartupMetadataStoredSearches, pResult->lGeneration, pResult->uCancellationToken, true, 0, pResult->strStage);
	return true;
}

void CSearchList::BeginStartupLoad()
{
	if (m_bStoredSearchStartupLoadActive || m_bStoredSearchStartupLoadCompleted)
		return;
	if (!theApp.IsStartupMetadataDomainReady(CemuleApp::StartupMetadataDownloads) || !theApp.KnownFilesReady() || !theApp.IsStartupMetadataDomainReady(CemuleApp::StartupMetadataSharedRules))
		return;
	if (thePrefs.GetDownloadValidator() && theApp.DownloadValidator != NULL)
		theApp.DownloadValidator->QueueReloadMap();

	const bool bHasLiveSearches = !m_listFileLists.IsEmpty();
	if (bHasLiveSearches)
		AddDebugLogLine(DLP_LOW, false, _T("Stored searches startup load will merge into an already populated search model. liveSearches=%u\n"), static_cast<UINT>(m_listFileLists.GetCount()));

	m_nStoredSearchStartupLoadNextSearchID = 0;
	m_uStoredSearchStartupLoadTotalSearches = 0;
	m_uStoredSearchStartupLoadLoadedSearches = 0;
	m_uStoredSearchStartupLoadLoadedFiles = 0;
	m_bStoredSearchStartupLoadLoadedVisibleSearch = false;
	m_bStoredSearchStartupLoadReloadedVisibleSearch = false;
	m_dwStoredSearchStartupLoadStartedTick = ::GetTickCount();
	m_dwStoredSearchStartupLoadLastProgressTick = 0;
	m_bStoredSearchStartupLoadActive = true;
	m_bStoredSearchStartupLoadCompleted = false;

	if (!QueueStartupStoredSearchesLoadWorker()) {
		m_bStoredSearchStartupLoadActive = false;
		m_bStoredSearchStartupLoadCompleted = true;
	}
}

void CSearchList::PublishStartupLoadWorkerProgress(UINT uLoadedSearches, UINT uTotalSearches, UINT uLoadedFiles)
{
	bool bPostRefresh = false;
	{
		CSingleLock progressLock(&m_storedSearchStartupProgressLock, TRUE);
		m_uStoredSearchStartupLoadTotalSearches = uTotalSearches;
		m_uStoredSearchStartupLoadLoadedSearches = uLoadedSearches;
		m_uStoredSearchStartupLoadLoadedFiles = uLoadedFiles;
		const DWORD dwNow = ::GetTickCount();
		if (m_dwStoredSearchStartupLoadLastProgressTick == 0 || dwNow - m_dwStoredSearchStartupLoadLastProgressTick >= 150) {
			m_dwStoredSearchStartupLoadLastProgressTick = dwNow;
			bPostRefresh = true;
		}
	}
	if (bPostRefresh && theApp.emuledlg != NULL)
		theApp.emuledlg->PostStartupOverlayRefresh();
}

void CSearchList::GetStartupLoadProgress(UINT& uLoadedSearches, UINT& uTotalSearches, UINT& uLoadedFiles) const
{
	CSingleLock progressLock(&m_storedSearchStartupProgressLock, TRUE);
	uLoadedSearches = m_uStoredSearchStartupLoadLoadedSearches;
	uTotalSearches = m_uStoredSearchStartupLoadTotalSearches;
	uLoadedFiles = m_uStoredSearchStartupLoadLoadedFiles;
}

void CSearchList::Process()
{
	if (::GetTickCount() >= m_nLastSaved + MIN2MS(12)) {
		if (m_bStoredSearchStartupLoadCompleted) {
			if (thePrefs.IsStoringSearchesEnabled())
				theApp.searchlist->StoreSearches();

			SaveSpamFilter();
		}
	}
}
void CSearchList::ReorderSearches() {
	if (m_listFileLists.IsEmpty())
		return;

	CTypedPtrList<CPtrList, SearchListsStruct*> m_listFileListsTemp;
	CClosableTabCtrl& searchselect = theApp.emuledlg->searchwnd->GetSearchSelector();
	for (int i = 0; i < searchselect.GetItemCount();) {
		TCITEM item;
		item.mask = TCIF_PARAM;
		if (searchselect.GetItem(i, &item) && item.lParam != NULL) {
			for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
				SearchListsStruct* list = m_listFileLists.GetNext(pos);
				if (reinterpret_cast<SSearchParams*>(item.lParam)->dwSearchID == list->m_nSearchID)
					m_listFileListsTemp.AddTail(list);
			}
		} 
		i++;
	}
	
	m_listFileLists.RemoveAll();
	for (POSITION pos = m_listFileListsTemp.GetHeadPosition(); pos != NULL;)
		m_listFileLists.AddTail(m_listFileListsTemp.GetNext(pos));
}

uint32 CSearchList::GetParentItemCount(uint32 nResultsID)
{
	const SearchList* list = GetSearchListForID(nResultsID);
	if (!list)
		return 0;

	uint32 nParents = 0;
	POSITION pos = list->GetHeadPosition();
	while (pos) {
		POSITION posCur = pos;
		const CSearchFile* cur_result = list->GetNext(pos);
		if (cur_result && !cur_result->GetListParent())
			nParents++;
	}

	return nParents;
}

void CSearchList::MarkSearchAsMerged(uint32 nSearchID)
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::MarkSearchAsMerged"));
	if (!mutationLock)
		return;

	m_mergedSearchHistory[nSearchID] = true;
}

void CSearchList::SetSearchItemKnownType(CSearchFile* src)
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::SetSearchItemKnownType"));
	if (!mutationLock)
		return;
	SetSearchItemKnownTypeNoLock(src);
}

bool CSearchList::RefreshSearchResultKnownType(const SSearchResultId& id)
{
	CSearchModelMutationLock mutationLock(this, _T("CSearchList::RefreshSearchResultKnownType"));
	if (!mutationLock || !id.IsValid())
		return false;

	if (ShouldDeferKnownTypeResolutionNoLock() && (theApp.downloadqueue == NULL || theApp.downloadqueue->GetFileByID(id.m_abyFileHash) == NULL))
		return false;

	const CSearchFile::EKnownType eKnownType = ResolveKnownTypeForHashNoLock(id.m_abyFileHash);
	return ApplyKnownTypeForHashNoLock(id.m_abyFileHash, eKnownType, NULL);
}

bool CSearchList::QueueKnownTypeRefreshForHash(const uchar* pFileHash)
{
	if (pFileHash == NULL || !IsSearchProcessingAcceptingJobs())
		return false;

	bool bQueued = false;
	{
		CSearchModelMutationLock mutationLock(this, _T("CSearchList::QueueKnownTypeRefreshForHash"));
		if (!mutationLock)
			return false;
		if (ShouldDeferKnownTypeResolutionNoLock() && (theApp.downloadqueue == NULL || theApp.downloadqueue->GetFileByID(pFileHash) == NULL))
			return false;

		SChunkedSearchKnownTypeRefreshJob* pAppendJob = NULL;
		for (POSITION pos = m_chunkedSearchKnownTypeRefreshJobs.GetHeadPosition(); pos != NULL;) {
			SChunkedSearchKnownTypeRefreshJob* pJob = m_chunkedSearchKnownTypeRefreshJobs.GetNext(pos);
			if (pJob == NULL)
				continue;
			if (!pJob->m_items.empty())
				continue;

			for (size_t i = 0; i < pJob->m_hashes.size(); ++i) {
				if (md4equ(pJob->m_hashes[i].m_key, pFileHash))
					return true;
			}
			if (pAppendJob == NULL && !pJob->m_bStartupRefresh)
				pAppendJob = pJob;
		}

		if (pAppendJob == NULL) {
			pAppendJob = new SChunkedSearchKnownTypeRefreshJob();
			m_chunkedSearchKnownTypeRefreshJobs.AddTail(pAppendJob);
		}
		pAppendJob->m_hashes.push_back(CSKey(pFileHash));
		bQueued = true;
	}

	if (bQueued)
		theApp.WakeSearchKnownTypeRefreshWork();
	return true;
}

bool CSearchList::AreKnownTypeDependenciesReady() const
{
	return theApp.IsStartupMetadataDomainReady(CemuleApp::StartupMetadataDownloads) && theApp.KnownFilesReady() && theApp.SharedFilesReady();
}

bool CSearchList::ShouldDeferKnownTypeResolutionNoLock() const
{
	return !AreKnownTypeDependenciesReady();
}

void CSearchList::SetSearchItemKnownTypeNoLock(CSearchFile* src)
{
	if (src == NULL)
		return;

	const CSearchFile::EKnownType eKnownType = ResolveKnownTypeForHashNoLock(src->GetFileHash());
	if (eKnownType != CSearchFile::NotDetermined || !ShouldDeferKnownTypeResolutionNoLock())
		ApplySearchItemKnownTypeNoLock(src, eKnownType);
}

bool CSearchList::ApplySearchItemKnownTypeNoLock(CSearchFile* src, CSearchFile::EKnownType eKnownType)
{
	if (src == NULL)
		return false;

	bool bChanged = src->GetKnownType() != eKnownType;
	src->SetKnownType(eKnownType);
	CSearchFile* pParent = NULL;
	if (src->GetListParent() != NULL)
		pParent = src->GetListParent();
	else if (src->GetListChildCount() > 0)
		pParent = src;

	if (pParent != NULL) {
		bChanged = bChanged || pParent->GetKnownType() != eKnownType;
		pParent->SetKnownType(eKnownType);
		const SearchList* list = GetSearchListForID(pParent->GetSearchID());
		if (list != NULL) {
			for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
				CSearchFile* pCurFile = list->GetNext(pos);
				if (pCurFile != NULL && pCurFile->GetListParent() == pParent) {
					bChanged = bChanged || pCurFile->GetKnownType() != eKnownType;
					pCurFile->SetKnownType(eKnownType);
				}
			}
		}
	}
	if (bChanged)
		TouchSearchModelSequence();
	return bChanged;
}

bool CSearchList::ApplyKnownTypeForHashNoLock(const uchar* pFileHash, CSearchFile::EKnownType eKnownType, std::set<uint32>* pTouchedSearchIDs)
{
	if (pFileHash == NULL)
		return false;

	bool bChanged = false;
	const CSKey hashKey(pFileHash);
	for (POSITION posList = m_listFileLists.GetHeadPosition(); posList != NULL;) {
		SearchListsStruct* pList = m_listFileLists.GetNext(posList);
		if (pList == NULL)
			continue;

		CSearchFile* pParent = NULL;
		if (!pList->m_mapParentsByHash.Lookup(hashKey, pParent) || pParent == NULL)
			continue;

		if (ApplySearchItemKnownTypeNoLock(pParent, eKnownType)) {
			bChanged = true;
			if (pTouchedSearchIDs != NULL)
				pTouchedSearchIDs->insert(pParent->GetSearchID());
		}
	}
	return bChanged;
}

CSearchFile::EKnownType CSearchList::ResolveKnownTypeForHashNoLock(const uchar *pFileHash) const
{
	if (pFileHash == NULL)
		return CSearchFile::NotDetermined;

	if (theApp.downloadqueue != NULL) {
		const CKnownFile* pFile = theApp.downloadqueue->GetFileByID(pFileHash);
		if (pFile != NULL)
			return pFile->IsPartFile() ? CSearchFile::Downloading : CSearchFile::Shared;
	}

	if (theApp.sharedfiles != NULL) {
		const CKnownFile* pFile = theApp.sharedfiles->GetFileByID(pFileHash);
		if (pFile != NULL)
			return CSearchFile::Shared;
	}

	if (theApp.knownfiles != NULL && theApp.KnownFilesReady()) {
		if (theApp.knownfiles->FindKnownFileByID(pFileHash) != NULL)
			return CSearchFile::Downloaded;
		if (theApp.knownfiles->IsCancelledFileByID(pFileHash))
			return CSearchFile::Cancelled;
	}

	return CSearchFile::NotDetermined;
}

void CSearchList::ClearKnownTypeRefreshJobsNoLock()
{
	while (!m_chunkedSearchKnownTypeRefreshJobs.IsEmpty())
		delete m_chunkedSearchKnownTypeRefreshJobs.RemoveHead();
}

bool CSearchList::RefreshKnownTypesForAllSearches()
{
	bool bTouched = false;
	{
		CSearchModelMutationLock mutationLock(this, _T("CSearchList::RefreshKnownTypesForAllSearches"));
		if (!mutationLock)
			return false;

		for (POSITION posList = m_listFileLists.GetHeadPosition(); posList != NULL;) {
			SearchListsStruct* pList = m_listFileLists.GetNext(posList);
			if (pList == NULL)
				continue;
			for (POSITION posFile = pList->m_listSearchFiles.GetHeadPosition(); posFile != NULL;) {
				CSearchFile* pCurFile = pList->m_listSearchFiles.GetNext(posFile);
				if (pCurFile != NULL) {
					pCurFile->SetKnownType(CSearchFile::NotDetermined);
					bTouched = true;
				}
			}
		}

		if (!bTouched)
			return true;

		for (POSITION posList = m_listFileLists.GetHeadPosition(); posList != NULL;) {
			SearchListsStruct* pList = m_listFileLists.GetNext(posList);
			if (pList == NULL)
				continue;
			for (POSITION posFile = pList->m_listSearchFiles.GetHeadPosition(); posFile != NULL;) {
				CSearchFile* pCurFile = pList->m_listSearchFiles.GetNext(posFile);
				if (pCurFile != NULL && pCurFile->GetKnownType() == CSearchFile::NotDetermined)
					SetSearchItemKnownTypeNoLock(pCurFile);
			}
		}

		TouchSearchModelSequence();
	}

	if (bTouched && outputwnd != NULL && theApp.emuledlg != NULL && theApp.emuledlg->searchwnd != NULL && theApp.emuledlg->searchwnd->m_pwndResults != NULL)
		UpdateSearchIngestOutputWndFromUiThread(theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.m_nResultsID, CString(), false);
	return true;
}

bool CSearchList::QueueKnownTypeRefreshForAllSearches(bool bStartupRefresh)
{
	if (!IsSearchProcessingAcceptingJobs())
		return false;

	SChunkedSearchKnownTypeRefreshJob *pJob = new SChunkedSearchKnownTypeRefreshJob();
	pJob->m_bStartupRefresh = bStartupRefresh;
	{
		CSearchModelMutationLock mutationLock(this, _T("CSearchList::QueueKnownTypeRefreshForAllSearches"));
		if (!mutationLock) {
			delete pJob;
			return false;
		}

		ClearKnownTypeRefreshJobsNoLock();
		for (POSITION posList = m_listFileLists.GetHeadPosition(); posList != NULL;) {
			SearchListsStruct* pList = m_listFileLists.GetNext(posList);
			if (pList == NULL)
				continue;
			for (POSITION posFile = pList->m_listSearchFiles.GetHeadPosition(); posFile != NULL;) {
				CSearchFile* pCurFile = pList->m_listSearchFiles.GetNext(posFile);
				if (pCurFile == NULL)
					continue;
				SSearchKnownTypeRefreshItem item;
				item.m_id.Set(pCurFile->GetSearchID(), pCurFile->GetFileHash(), pCurFile->GetListParent() != NULL, pCurFile->GetFileName());
				pJob->m_items.push_back(item);
			}
		}

		if (pJob->m_items.empty()) {
			const bool bStartupRefresh = pJob->m_bStartupRefresh;
			delete pJob;
			if (bStartupRefresh && theApp.emuledlg != NULL)
				theApp.emuledlg->NotifyStartupSearchKnownTypesRefreshCompleted();
			return true;
		}
		// Startup restore can compute partial known state before all metadata domains are ready.
		// Recalculate from a clean state so the startup pass is equivalent to a later F5 recheck.
		m_chunkedSearchKnownTypeRefreshJobs.AddTail(pJob);
	}

	return true;
}

bool CSearchList::HasKnownTypeRefreshWork() const
{
	CSingleLock lock(const_cast<CCriticalSection*>(GetSearchModelLock()), TRUE);
	return !const_cast<CSearchList*>(this)->m_chunkedSearchKnownTypeRefreshJobs.IsEmpty();
}

bool CSearchList::ProcessKnownTypeRefreshWork(DWORD dwSliceStart)
{
	if (!IsSearchProcessingAcceptingJobs()) {
		CSearchModelMutationLock mutationLock(this, _T("CSearchList::ProcessKnownTypeRefreshWork::cancel"));
		if (mutationLock)
			ClearKnownTypeRefreshJobsNoLock();
		return false;
	}

	bool bProcessed = false;
	std::set<uint32> touchedSearchIDs;
	{
		CSearchModelMutationLock mutationLock(this, _T("CSearchList::ProcessKnownTypeRefreshWork"));
		if (!mutationLock)
			return false;

		while (!m_chunkedSearchKnownTypeRefreshJobs.IsEmpty() && !theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetBackendCommandDispatch)) {
			SChunkedSearchKnownTypeRefreshJob *pJob = m_chunkedSearchKnownTypeRefreshJobs.GetHead();
			if (pJob == NULL) {
				m_chunkedSearchKnownTypeRefreshJobs.RemoveHead();
				continue;
			}

			UINT uAppliedInSlice = 0;
			if (!pJob->m_hashes.empty()) {
				while (pJob->m_iNextHash < static_cast<INT_PTR>(pJob->m_hashes.size()) && !theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetBackendCommandDispatch)) {
					const CSKey &hash = pJob->m_hashes[static_cast<size_t>(pJob->m_iNextHash++)];
					const CSearchFile::EKnownType eKnownType = ResolveKnownTypeForHashNoLock(hash.m_key);
					ApplyKnownTypeForHashNoLock(hash.m_key, eKnownType, &pJob->m_touchedSearchIDs);
					++uAppliedInSlice;
					bProcessed = true;
				}

				if (pJob->m_iNextHash >= static_cast<INT_PTR>(pJob->m_hashes.size())) {
					for (std::set<uint32>::const_iterator it = pJob->m_touchedSearchIDs.begin(); it != pJob->m_touchedSearchIDs.end(); ++it)
						touchedSearchIDs.insert(*it);
					m_chunkedSearchKnownTypeRefreshJobs.RemoveHead();
					delete pJob;
				}
				else
					break;
				continue;
			}

			if (!pJob->m_bStartupRefresh) {
				while (pJob->m_iNextResetItem < static_cast<INT_PTR>(pJob->m_items.size()) && !theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetBackendCommandDispatch)) {
					const SSearchKnownTypeRefreshItem &item = pJob->m_items[static_cast<size_t>(pJob->m_iNextResetItem++)];
					CSearchFile *pFile = GetSearchFileByResultId(item.m_id);
					if (pFile != NULL && pFile->GetKnownType() != CSearchFile::NotDetermined) {
						pFile->SetKnownType(CSearchFile::NotDetermined);
						pJob->m_touchedSearchIDs.insert(pFile->GetSearchID());
					}
					++uAppliedInSlice;
					bProcessed = true;
				}

				if (pJob->m_iNextResetItem < static_cast<INT_PTR>(pJob->m_items.size())) {
					if (uAppliedInSlice != 0)
						TouchSearchModelSequence();
					break;
				}
			}
			else
				pJob->m_iNextResetItem = static_cast<INT_PTR>(pJob->m_items.size());

			while (pJob->m_iNextItem < static_cast<INT_PTR>(pJob->m_items.size()) && !theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetBackendCommandDispatch)) {
				SSearchKnownTypeRefreshItem &item = pJob->m_items[static_cast<size_t>(pJob->m_iNextItem++)];
				const CSearchFile::EKnownType eKnownType = ResolveKnownTypeForHashNoLock(item.m_id.m_abyFileHash);
				if (pJob->m_bStartupRefresh)
					item.m_eKnownType = eKnownType;
				else if (eKnownType != CSearchFile::NotDetermined) {
					CSearchFile *pFile = GetSearchFileByResultId(item.m_id);
					if (pFile != NULL) {
						ApplySearchItemKnownTypeNoLock(pFile, eKnownType);
						pJob->m_touchedSearchIDs.insert(pFile->GetSearchID());
						touchedSearchIDs.insert(pFile->GetSearchID());
					}
				}
				++uAppliedInSlice;
				bProcessed = true;
			}

			if (!pJob->m_bStartupRefresh && uAppliedInSlice != 0)
				TouchSearchModelSequence();

			if (pJob->m_iNextItem >= static_cast<INT_PTR>(pJob->m_items.size())) {
				const bool bStartupRefresh = pJob->m_bStartupRefresh;
				if (bStartupRefresh) {
					for (size_t i = 0; i < pJob->m_items.size(); ++i) {
						const SSearchKnownTypeRefreshItem &item = pJob->m_items[i];
						CSearchFile *pFile = GetSearchFileByResultId(item.m_id);
						if (pFile != NULL) {
							pFile->SetKnownType(CSearchFile::NotDetermined);
							pJob->m_touchedSearchIDs.insert(pFile->GetSearchID());
						}
					}
					for (size_t i = 0; i < pJob->m_items.size(); ++i) {
						const SSearchKnownTypeRefreshItem &item = pJob->m_items[i];
						if (item.m_eKnownType == CSearchFile::NotDetermined)
							continue;
						CSearchFile *pFile = GetSearchFileByResultId(item.m_id);
						if (pFile != NULL)
							ApplySearchItemKnownTypeNoLock(pFile, item.m_eKnownType);
					}
					TouchSearchModelSequence();
				}
				for (std::set<uint32>::const_iterator it = pJob->m_touchedSearchIDs.begin(); it != pJob->m_touchedSearchIDs.end(); ++it)
					touchedSearchIDs.insert(*it);
				m_chunkedSearchKnownTypeRefreshJobs.RemoveHead();
				delete pJob;
				if (bStartupRefresh && theApp.emuledlg != NULL)
					theApp.emuledlg->NotifyStartupSearchKnownTypesRefreshCompleted();
			}
			else
				break;
		}

	}

	for (std::set<uint32>::const_iterator it = touchedSearchIDs.begin(); it != touchedSearchIDs.end(); ++it)
		UpdateSearchIngestOutputWnd(*it, CString(), false);
	return bProcessed;
}
