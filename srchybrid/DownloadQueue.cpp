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
#include <io.h>
#include <algorithm>
#include <vector>
#include "emule.h"
#include "UpDownClient.h"
#include "DownloadQueue.h"
#include "PartFile.h"
#include "PartFileWriteThread.h"
#include "ed2kLink.h"
#include "SearchFile.h"
#include "ClientList.h"
#include "Statistics.h"
#include "SharedFileList.h"
#include "SafeFile.h"
#include "ServerConnect.h"
#include "ServerList.h"
#include "Server.h"
#include "Packets.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "kademlia/kademlia/search.h"
#include "kademlia/kademlia/SearchManager.h"
#include "kademlia/utils/uint128.h"
#include "ipfilter.h"
#include "emuledlg.h"
#include "TransferDlg.h"
#include "SharedFilesWnd.h"
#include "TaskbarNotifier.h"
#include "MenuCmds.h"
#include "Log.h"
#include "KnownFileList.h"
#include "SearchList.h"
#include "eMuleAI/DownloadValidator.h"
#include "OtherFunctions.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern UINT g_uMainThreadId;

static bool GuardDownloadModelMutation(LPCTSTR pszEntryPoint)
{
	return theApp.GuardModelMutation(CemuleApp::ModelMutationDownloadQueue, pszEntryPoint);
}

namespace
{
	const uint64 kAdaptiveDownloadBufferMiB = 1024ui64 * 1024ui64;
	const uint64 kAdaptiveDownloadBufferMinGlobalBudget = 512ui64 * kAdaptiveDownloadBufferMiB;
#ifdef _WIN64
	const uint64 kAdaptiveDownloadBufferMaxGlobalBudget = 4ui64 * 1024ui64 * kAdaptiveDownloadBufferMiB;
#else
	const uint64 kAdaptiveDownloadBufferMaxGlobalBudget = kAdaptiveDownloadBufferMinGlobalBudget;
#endif
	const uint64 kAdaptiveDownloadBufferMinFileShare = 64ui64 * kAdaptiveDownloadBufferMiB;
	const uint64 kAdaptiveDownloadBufferAvailableMemoryPercent = 25;

	uint64 AddSaturated(uint64 uLeft, uint64 uRight)
	{
		if (uLeft > ~0ui64 - uRight)
			return ~0ui64;
		return uLeft + uRight;
	}

	uint64 MulSaturated(uint64 uLeft, uint64 uRight)
	{
		if (uLeft != 0 && uRight > ~0ui64 / uLeft)
			return ~0ui64;
		return uLeft * uRight;
	}

	uint64 BuildAdaptiveDownloadBufferBudgetBytes(uint64 uAvailablePhysBytes)
	{
		uint64 uBudget = kAdaptiveDownloadBufferMinGlobalBudget;
		if (uAvailablePhysBytes != 0) {
			const uint64 uMemoryBudget = uAvailablePhysBytes / 100ui64 * kAdaptiveDownloadBufferAvailableMemoryPercent;
			uBudget = min(kAdaptiveDownloadBufferMaxGlobalBudget, max(kAdaptiveDownloadBufferMinGlobalBudget, uMemoryBudget));
		}
		return uBudget;
	}

	uint64 BuildEffectiveDownloadFileBufferSizeBytes(uint64 uBaseBufferSize, uint64 uCurrentFileBufferedBytes, uint64 uBufferedFileCount, uint64 uGlobalBudget)
	{
		if (uBaseBufferSize == 0 || uBufferedFileCount <= 1 || uGlobalBudget == 0)
			return uBaseBufferSize;

		const uint64 uPerFileBudget = max(kAdaptiveDownloadBufferMinFileShare, uGlobalBudget / uBufferedFileCount);
		const uint64 uDemandBudget = max(uBaseBufferSize, max(uCurrentFileBufferedBytes, uPerFileBudget));
		return min(MulSaturated(uBaseBufferSize, 4ui64), uDemandBudget);
	}

	bool ShouldFlushForAdaptiveDownloadBufferBudget(uint64 uCurrentFileBufferedBytes, uint64 uTotalBufferedBytes, uint64 uLargestBufferedFileBytes, uint64 uGlobalBudget)
	{
		return uGlobalBudget != 0
			&& uTotalBufferedBytes > uGlobalBudget
			&& uCurrentFileBufferedBytes != 0
			&& uCurrentFileBufferedBytes >= uLargestBufferedFileBytes;
	}
}

enum EStartupDownloadFinishStep
{
	StartupDownloadFinishSort = 0,
	StartupDownloadFinishDiskspace,
	StartupDownloadFinishDownloadListUpdate,
	StartupDownloadFinishSharedListUpdate,
	StartupDownloadFinishPartFileCreates,
	StartupDownloadFinishInitialPartMetSaves,
	StartupDownloadFinishComplete
};

static CDownloadListCtrl* GetDownloadListForDownloadQueueUi()
{
	if (!theApp.IsUiThread() || theApp.emuledlg == NULL || theApp.emuledlg->transferwnd == NULL)
		return NULL;
	return theApp.emuledlg->transferwnd->GetDownloadList();
}

static void CleanupRejectedPartFileCreateResult(const PartFileCreateResult& result)
{
	if (result.hFile != INVALID_HANDLE_VALUE)
		::CloseHandle(result.hFile);
	if (result.dwError == ERROR_SUCCESS && !result.strPartFilePath.IsEmpty() && !DeleteFileLongPath(result.strPartFilePath)) {
		const DWORD dwError = ::GetLastError();
		if (dwError != ERROR_FILE_NOT_FOUND && dwError != ERROR_PATH_NOT_FOUND)
			AddDebugLogLine(DLP_LOW, false, _T("Failed to remove rejected deferred part file create result. error=%lu path=%s\n"), dwError, (LPCTSTR)result.strPartFilePath);
	}
}

static CSharedFilesCtrl* GetSharedFilesCtrlForDownloadQueueUi()
{
	if (!theApp.IsUiThread() || theApp.emuledlg == NULL || theApp.emuledlg->sharedfileswnd == NULL)
		return NULL;
	return &theApp.emuledlg->sharedfileswnd->sharedfilesctrl;
}

static void RemoveDeletedCompletedDownloadRowsForNewDownload(const CDownloadQueue *pQueue, const uchar *pFileHash)
{
	if (pQueue == NULL || pFileHash == NULL || isnulmd4(pFileHash))
		return;
	if (theApp.sharedfiles != NULL && theApp.sharedfiles->GetLiveFileByID(pFileHash) != NULL)
		return;
	if (pQueue->GetFileByID(pFileHash) != NULL)
		return;

	std::vector<CString> vecFileHashes;
	vecFileHashes.push_back(md4str(pFileHash));
	CDownloadListCtrl *pDownloadList = GetDownloadListForDownloadQueueUi();
	if (pDownloadList != NULL && ::IsWindow(pDownloadList->GetSafeHwnd()))
		pDownloadList->RemoveDeletedCompletedFilesByHash(vecFileHashes);
	else
		theApp.QueueDownloadListDeletedCompletedRowsRemovedEvent(vecFileHashes);
}

static void RemoveSourceFromDownloadListOrQueueChangeEvent(CDownloadListCtrl *pDownloadList, CUpDownClient *pSource, CPartFile *pOwner, bool &bQueuedListChangedEvent)
{
	if (pDownloadList != NULL) {
		pDownloadList->RemoveSource(pSource, pOwner);
		return;
	}

	if (!bQueuedListChangedEvent) {
		theApp.QueueDownloadListChangedEvent(_T("download-source-removed"));
		bQueuedListChangedEvent = true;
	}
}

template <typename TMap, typename TKey>
static void EraseDownloadSourceIndexEntries(TMap& mapIndex, const TKey& key, DWORD uRuntimeID)
{
	std::pair<typename TMap::iterator, typename TMap::iterator> range = mapIndex.equal_range(key);
	for (typename TMap::iterator it = range.first; it != range.second;) {
		if (it->second == uRuntimeID)
			mapIndex.erase(it++);
		else
			++it;
	}
}


SDownloadItemId::SDownloadItemId()
{
	Clear();
}

void SDownloadItemId::Clear()
{
	md4clr(m_abyFileHash);
	m_uRuntimeID = 0;
}

bool SDownloadItemId::IsValid() const
{
	static const uchar abyEmptyHash[MDX_DIGEST_SIZE] = { 0 };
	return !md4equ(m_abyFileHash, abyEmptyHash);
}

void SDownloadItemId::SetHash(const uchar *pFileHash)
{
	m_uRuntimeID = 0;
	if (pFileHash != NULL)
		md4cpy(m_abyFileHash, pFileHash);
	else
		Clear();
}

void SDownloadItemId::SetFile(const CPartFile *pFile)
{
	Clear();
	if (pFile == NULL)
		return;
	md4cpy(m_abyFileHash, pFile->GetFileHash());
	m_uRuntimeID = pFile->GetRuntimeID();
}

bool SDownloadItemId::EqualsHash(const uchar *pFileHash) const
{
	return pFileHash != NULL && md4equ(m_abyFileHash, pFileHash);
}

bool SDownloadItemId::Equals(const SDownloadItemId &other) const
{
	return m_uRuntimeID == other.m_uRuntimeID && md4equ(m_abyFileHash, other.m_abyFileHash);
}

bool SDownloadItemIdLess::operator()(const SDownloadItemId& lhs, const SDownloadItemId& rhs) const
{
	const int iHashCompare = memcmp(lhs.m_abyFileHash, rhs.m_abyFileHash, sizeof(lhs.m_abyFileHash));
	if (iHashCompare != 0)
		return iHashCompare < 0;
	return lhs.m_uRuntimeID < rhs.m_uRuntimeID;
}

SDownloadSourceId::SDownloadSourceId()
	: m_uClientRuntimeID(0)
{
}

void SDownloadSourceId::Clear()
{
	m_uClientRuntimeID = 0;
}

bool SDownloadSourceId::IsValid() const
{
	return m_uClientRuntimeID != 0;
}

SDownloadCategoryId::SDownloadCategoryId()
	: m_uCategory(0)
{
}

void SDownloadCategoryId::Clear()
{
	m_uCategory = 0;
}

bool SDownloadCategoryId::IsValid() const
{
	return true;
}

CDownloadQueue::SDownloadSourceHashKey::SDownloadSourceHashKey()
{
	memset(m_abyHash, 0, sizeof(m_abyHash));
}

CDownloadQueue::SDownloadSourceHashKey::SDownloadSourceHashKey(const uchar* pHash)
{
	if (pHash != NULL)
		memcpy(m_abyHash, pHash, sizeof(m_abyHash));
	else
		memset(m_abyHash, 0, sizeof(m_abyHash));
}

bool CDownloadQueue::SDownloadSourceHashKey::operator<(const SDownloadSourceHashKey& other) const
{
	return memcmp(m_abyHash, other.m_abyHash, sizeof(m_abyHash)) < 0;
}

bool CDownloadQueue::SDownloadSourceHashKey::operator==(const SDownloadSourceHashKey& other) const
{
	return memcmp(m_abyHash, other.m_abyHash, sizeof(m_abyHash)) == 0;
}

CDownloadQueue::SDownloadSourceEndpointKey::SDownloadSourceEndpointKey()
	: m_ip()
	, m_uPort(0)
{
}

CDownloadQueue::SDownloadSourceEndpointKey::SDownloadSourceEndpointKey(const CAddress& ip, uint16 uPort)
	: m_ip(ip)
	, m_uPort(uPort)
{
}

bool CDownloadQueue::SDownloadSourceEndpointKey::operator<(const SDownloadSourceEndpointKey& other) const
{
	if (m_ip < other.m_ip)
		return true;
	if (other.m_ip < m_ip)
		return false;
	return m_uPort < other.m_uPort;
}

bool CDownloadQueue::SDownloadSourceEndpointKey::operator==(const SDownloadSourceEndpointKey& other) const
{
	return m_ip == other.m_ip && m_uPort == other.m_uPort;
}

CDownloadQueue::SDownloadSourceIdPortKey::SDownloadSourceIdPortKey()
	: m_uUserID(0)
	, m_uPort(0)
{
}

CDownloadQueue::SDownloadSourceIdPortKey::SDownloadSourceIdPortKey(uint32 uUserID, uint16 uPort)
	: m_uUserID(uUserID)
	, m_uPort(uPort)
{
}

bool CDownloadQueue::SDownloadSourceIdPortKey::operator<(const SDownloadSourceIdPortKey& other) const
{
	if (m_uUserID != other.m_uUserID)
		return m_uUserID < other.m_uUserID;
	return m_uPort < other.m_uPort;
}

bool CDownloadQueue::SDownloadSourceIdPortKey::operator==(const SDownloadSourceIdPortKey& other) const
{
	return m_uUserID == other.m_uUserID && m_uPort == other.m_uPort;
}

CDownloadQueue::SDownloadSourceLowIdKey::SDownloadSourceLowIdKey()
	: m_uUserID(0)
	, m_uServerIP(0)
	, m_uServerPort(0)
{
}

CDownloadQueue::SDownloadSourceLowIdKey::SDownloadSourceLowIdKey(uint32 uUserID, uint32 uServerIP, uint16 uServerPort)
	: m_uUserID(uUserID)
	, m_uServerIP(uServerIP)
	, m_uServerPort(uServerPort)
{
}

bool CDownloadQueue::SDownloadSourceLowIdKey::operator<(const SDownloadSourceLowIdKey& other) const
{
	if (m_uUserID != other.m_uUserID)
		return m_uUserID < other.m_uUserID;
	if (m_uServerIP != other.m_uServerIP)
		return m_uServerIP < other.m_uServerIP;
	return m_uServerPort < other.m_uServerPort;
}

bool CDownloadQueue::SDownloadSourceLowIdKey::operator==(const SDownloadSourceLowIdKey& other) const
{
	return m_uUserID == other.m_uUserID && m_uServerIP == other.m_uServerIP && m_uServerPort == other.m_uServerPort;
}

CDownloadQueue::SDownloadSourceIndexSnapshot::SDownloadSourceIndexSnapshot()
	: m_bHasHash(false)
	, m_bHasLowId(false)
{
}

bool CDownloadQueue::SDownloadSourceIndexSnapshot::operator==(const SDownloadSourceIndexSnapshot& other) const
{
	return m_bHasHash == other.m_bHasHash
		&& (!m_bHasHash || m_hashKey == other.m_hashKey)
		&& m_aIPKeys == other.m_aIPKeys
		&& m_aEndpointKeys == other.m_aEndpointKeys
		&& m_aUDPKeys == other.m_aUDPKeys
		&& m_aIdPortKeys == other.m_aIdPortKeys
		&& m_bHasLowId == other.m_bHasLowId
		&& (!m_bHasLowId || m_lowIdKey == other.m_lowIdKey);
}

CDownloadQueue::SDownloadSourceIndexEntry::SDownloadSourceIndexEntry()
	: m_pFile(NULL)
	, m_pClient(NULL)
{
}

namespace
{
	struct SPartMetOverviewRow
	{
		CString strPartFileName;
		CString strFullName;
		CString strED2kLink;
	};

	struct SPartMetOverviewExportTask
	{
		LONG lGeneration;
		bool bSingleTempDir;
		bool bShutdownFallback;
		bool bStartupLoadCompleted;
		CString strFileListPath;
		CString strFormattedTime;
		CString strTempDir;
		std::vector<SPartMetOverviewRow> rows;
	};

	LONG g_lPartMetOverviewExportGeneration = 0;
	CCriticalSection g_partMetOverviewExportLock;
	CCriticalSection g_partMetOverviewPendingLock;
	SPartMetOverviewExportTask *g_pPendingPartMetOverviewExportTask = NULL;

	CString GetPartMetOverviewSiblingPath(const CString& strFileListPath, LPCTSTR pszExtension)
	{
		CString strResult(strFileListPath);
		const int iSlash = max(strResult.ReverseFind(_T('\\')), strResult.ReverseFind(_T('/')));
		const int iDot = strResult.ReverseFind(_T('.'));
		if (iDot > iSlash)
			strResult = strResult.Left(iDot);
		strResult += pszExtension;
		return strResult;
	}

	CString GetPartMetOverviewTempPath(const CString& strFileListPath)
	{
		return GetPartMetOverviewSiblingPath(strFileListPath, _T(".tmp"));
	}

	void TracePartMetOverviewExportResult(const SPartMetOverviewExportTask& task, LPCTSTR pszResult, LPCTSTR pszReason, const CString& strTmpFileListPath, DWORD dwLastError = 0)
	{
		AddDebugLogLine(DLP_LOW, false, _T("[AsyncDiskWrite] name=\"downloads.txt\" generation=%ld result=%s reason=%s error=%lu shutdownFallback=%u temp=\"%s\" final=\"%s\"\n"),
			task.lGeneration, pszResult != NULL ? pszResult : _T(""), pszReason != NULL ? pszReason : _T(""), dwLastError, task.bShutdownFallback ? 1U : 0U,
			(LPCTSTR)strTmpFileListPath, (LPCTSTR)task.strFileListPath);
		if (pszResult != NULL && (_tcsicmp(pszResult, _T("failed")) == 0 || _tcsicmp(pszResult, _T("warning")) == 0))
			theApp.QueueAsyncDiskWriteResultEvent(_T("downloads.txt"), task.lGeneration, pszResult, pszReason, strTmpFileListPath, task.strFileListPath, task.bShutdownFallback, dwLastError);
	}


	bool WritePartMetFilesOverviewSnapshot(const SPartMetOverviewExportTask &task)
	{
		if (task.strFileListPath.IsEmpty()) {
			TracePartMetOverviewExportResult(task, _T("failed"), _T("invalid-path"), CString());
			return false;
		}

		const CString strTmpFileListPath(GetPartMetOverviewTempPath(task.strFileListPath));

		CSafeBufferedFile file;
		CFileException fex;
		if (!file.Open(PreparePathForWin32LongPath(strTmpFileListPath), CFile::modeCreate | CFile::modeWrite | CFile::typeBinary | CFile::shareDenyWrite, &fex)) {
			TracePartMetOverviewExportResult(task, _T("failed"), _T("open-temp"), strTmpFileListPath, static_cast<DWORD>(fex.m_lOsError));
			return false;
		}

		fputwc(u'\xFEFF', file.m_pStream);

		try {
			file.printf(_T("Date:      %s\r\n"), (LPCTSTR)task.strFormattedTime);
			if (task.bSingleTempDir)
				file.printf(_T("Directory: %s\r\n"), (LPCTSTR)task.strTempDir);
			file.printf(_T("\r\n"));
			file.printf(_T("Part file\teD2K link\r\n"));
			file.printf(_T("--------------------------------------------------------------------------------\r\n"));
			for (std::vector<SPartMetOverviewRow>::const_iterator it = task.rows.begin(); it != task.rows.end(); ++it) {
				if (task.bSingleTempDir)
					file.printf(_T("%s\t%s\r\n"), (LPCTSTR)it->strPartFileName, (LPCTSTR)it->strED2kLink);
				else
					file.printf(_T("%s\t%s\r\n"), (LPCTSTR)it->strFullName, (LPCTSTR)it->strED2kLink);
			}

			CommitAndClose(file);
		} catch (CFileException *ex) {
			const DWORD dwLastError = static_cast<DWORD>(ex->m_lOsError);
			ex->Delete();
			file.Abort();
			(void)DeleteFileLongPath(strTmpFileListPath);
			TracePartMetOverviewExportResult(task, _T("failed"), _T("write-temp"), strTmpFileListPath, dwLastError);
			return false;
		} catch (CException *ex) {
			ex->Delete();
			file.Abort();
			(void)DeleteFileLongPath(strTmpFileListPath);
			TracePartMetOverviewExportResult(task, _T("failed"), _T("write-temp"), strTmpFileListPath);
			return false;
		} catch (...) {
			file.Abort();
			(void)DeleteFileLongPath(strTmpFileListPath);
			TracePartMetOverviewExportResult(task, _T("failed"), _T("write-temp"), strTmpFileListPath);
			return false;
		}

		const CString strBakFileListPath(GetPartMetOverviewSiblingPath(task.strFileListPath, _T(".bak")));

		if (!MoveFileExLongPath(task.strFileListPath, strBakFileListPath, MOVEFILE_REPLACE_EXISTING)) {
			const DWORD dwBackupError = ::GetLastError();
			if (dwBackupError != ERROR_FILE_NOT_FOUND && dwBackupError != ERROR_PATH_NOT_FOUND)
				TracePartMetOverviewExportResult(task, _T("warning"), _T("backup-move"), strTmpFileListPath, dwBackupError);
		}
		if (!MoveFileExLongPath(strTmpFileListPath, task.strFileListPath, MOVEFILE_REPLACE_EXISTING)) {
			const DWORD dwPublishError = ::GetLastError();
			(void)DeleteFileLongPath(strTmpFileListPath);
			TracePartMetOverviewExportResult(task, _T("failed"), _T("publish-final"), strTmpFileListPath, dwPublishError);
			return false;
		}

		TracePartMetOverviewExportResult(task, _T("success"), _T("published"), strTmpFileListPath);
		return true;
	}


	bool QueuePartMetOverviewExportTask(SPartMetOverviewExportTask *pTask)
	{
		if (pTask == NULL)
			return false;
		if (theApp.GetWorkerTopologyState(CemuleApp::WorkerTopologyPersistence) == CemuleApp::WorkerTopologyStopped && !theApp.StartPersistenceWorker())
			return false;

		{
			CSingleLock lock(&g_partMetOverviewPendingLock, TRUE);
			delete g_pPendingPartMetOverviewExportTask;
			g_pPendingPartMetOverviewExportTask = pTask;
		}

		CemuleApp::SWorkerTopologyItem item;
		item.m_eRole = CemuleApp::WorkerTopologyPersistence;
		item.m_eType = CemuleApp::WorkerTopologyItemPersistenceSave;
		item.m_strStage = _T("downloads-overview-export");
		item.m_strCoalesceKey = _T("downloads-overview-export");
		item.m_lWorkerGeneration = pTask->lGeneration;
		item.m_dwCreatedTick = ::GetTickCount();
		if (theApp.QueuePersistenceWorkerItem(item))
			return true;

		{
			CSingleLock lock(&g_partMetOverviewPendingLock, TRUE);
			if (g_pPendingPartMetOverviewExportTask == pTask)
				g_pPendingPartMetOverviewExportTask = NULL;
		}
		return false;
	}

	SPartMetOverviewExportTask* PopQueuedPartMetOverviewExportTask()
	{
		CSingleLock lock(&g_partMetOverviewPendingLock, TRUE);
		SPartMetOverviewExportTask *pTask = g_pPendingPartMetOverviewExportTask;
		g_pPendingPartMetOverviewExportTask = NULL;
		return pTask;
	}
}

static bool HasPendingNatTraversalConnect(const CUpDownClient *source)
{
	return source != NULL && source->socket != NULL && source->socket->HaveNatTraversalLayer() && !source->socket->IsConnected();
}

static void PrepareKnownSourceForFreshAsk(CUpDownClient *source, CPartFile *sender, const CPartFile *pPreviousReqFile)
{
	if (source == NULL || sender == NULL || pPreviousReqFile == sender)
		return;

	// Avoid carrying old connect-throttle window to a newly attached file.
	source->TrigNextSafeAskForDownload(sender);

	if (HasPendingNatTraversalConnect(source)) {
		source->MarkNatTRendezvous(2, true);
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] Fresh source moved to %s while NAT-T connect is pending; scheduled immediate retry for %s"), (LPCTSTR)EscPercent(sender->GetFileName()), (LPCTSTR)EscPercent(source->DbgGetClientInfo()));
	}

	// A source reused from a finished file can keep stale NNP state.
	if (source->GetDownloadState() == DS_NONEEDEDPARTS)
		source->SetDownloadState(DS_ONQUEUE, _T("Source moved to a new file; clearing stale NNP state"));
}

static bool IsKnownKadEndpoint(const CAddress& ip, uint16 nKadPort)
{
	return !ip.IsNull() && nKadPort != 0;
}

static bool IsSameKnownKadEndpoint(const CAddress& ip1, uint16 nKadPort1, const CAddress& ip2, uint16 nKadPort2)
{
	return IsKnownKadEndpoint(ip1, nKadPort1) && IsKnownKadEndpoint(ip2, nKadPort2) && ip1 == ip2 && nKadPort1 == nKadPort2;
}

static bool IsSameDownloadClientIP(const CAddress& ip, const CUpDownClient* client)
{
	return client != NULL && !ip.IsNull() && (ip == client->GetIP() || ip == client->GetIPv4() || ip == client->GetIPv6() || ip == client->GetConnectIP());
}

static bool IsSameNonZeroPort(uint16 uLeftPort, uint16 uRightPort)
{
	return uLeftPort != 0 && uLeftPort == uRightPort;
}

static bool HaveMatchingDownloadEndpoint(const CUpDownClient* left, const CUpDownClient* right)
{
	if (left == NULL || right == NULL)
		return false;

	const CAddress* leftIps[] = { &left->GetIP(), &left->GetIPv4(), &left->GetIPv6(), &left->GetConnectIP() };
	const CAddress* rightIps[] = { &right->GetIP(), &right->GetIPv4(), &right->GetIPv6(), &right->GetConnectIP() };
	for (int i = 0; i < ARRSIZE(leftIps); ++i) {
		if (leftIps[i]->IsNull())
			continue;
		for (int j = 0; j < ARRSIZE(rightIps); ++j) {
			if (*leftIps[i] != *rightIps[j])
				continue;
			if (IsSameNonZeroPort(left->GetUserPort(), right->GetUserPort()) || IsSameNonZeroPort(left->GetKadPort(), right->GetKadPort()))
				return true;
		}
	}
	return false;
}

static bool IsSameDownloadSourceIdentity(const CUpDownClient* left, const CUpDownClient* right)
{
	return left != NULL && right != NULL && (left->Compare(right, true) || left->Compare(right, false) || HaveMatchingDownloadEndpoint(left, right));
}

static bool ShouldRefreshDuplicateKadBuddyRoute(const CUpDownClient* target, const CUpDownClient* incoming, ESourceFrom eSourceFrom)
{
	if (eSourceFrom != SF_KADEMLIA || target == NULL || incoming == NULL || !incoming->HasValidServingBuddyID())
		return true;

	const bool bTargetHasEndpoint = IsKnownKadEndpoint(target->GetConnectIP(), target->GetKadPort());
	const bool bIncomingHasEndpoint = IsKnownKadEndpoint(incoming->GetConnectIP(), incoming->GetKadPort());
	if (!bTargetHasEndpoint || !bIncomingHasEndpoint)
		return true;

	return IsSameKnownKadEndpoint(target->GetConnectIP(), target->GetKadPort(), incoming->GetConnectIP(), incoming->GetKadPort());
}

static bool IsCurrentKadServingBuddyRoute(const CAddress& ip, uint16 nPort)
{
	if (theApp.clientlist == NULL || theApp.clientlist->GetServingBuddyStatus() != Connected || ip.IsNull() || nPort == 0)
		return false;

	const CUpDownClient* pBuddy = theApp.clientlist->GetServingBuddy();
	if (pBuddy == NULL)
		return false;

	const CAddress buddyIP = !pBuddy->GetConnectIP().IsNull() ? pBuddy->GetConnectIP() : pBuddy->GetIP();
	if (buddyIP.IsNull() || buddyIP != ip)
		return false;

	return pBuddy->GetKadPort() == nPort || pBuddy->GetUDPPort() == nPort;
}

static bool ShouldAcceptDuplicateKadBuddyRoute(const CUpDownClient* target, const CUpDownClient* incoming, ESourceFrom eSourceFrom)
{
	if (eSourceFrom != SF_KADEMLIA || target == NULL || incoming == NULL || !incoming->HasValidServingBuddyID())
		return true;
	if (!target->HasValidServingBuddyID())
		return true;
	if (target->GetServingBuddyIP() == incoming->GetServingBuddyIP() && target->GetServingBuddyPort() == incoming->GetServingBuddyPort())
		return true;

	const bool bCurrentRouteUsesLocalBuddy = IsCurrentKadServingBuddyRoute(target->GetServingBuddyIP(), target->GetServingBuddyPort());
	const bool bIncomingRouteUsesLocalBuddy = IsCurrentKadServingBuddyRoute(incoming->GetServingBuddyIP(), incoming->GetServingBuddyPort());
	return !bCurrentRouteUsesLocalBuddy || bIncomingRouteUsesLocalBuddy;
}

static bool RefreshKnownSourceRouting(CUpDownClient* target, const CUpDownClient* incoming, ESourceFrom eSourceFrom, bool bApplySourceFrom)
{
	if (target == NULL || incoming == NULL || target == incoming)
		return false;

	bool bUpdated = false;
	const bool bRefreshKadBuddyRoute = ShouldRefreshDuplicateKadBuddyRoute(target, incoming, eSourceFrom);
	const bool bAcceptKadBuddyRoute = bRefreshKadBuddyRoute && ShouldAcceptDuplicateKadBuddyRoute(target, incoming, eSourceFrom);
	const bool bIncomingDirectKadEndpoint = eSourceFrom == SF_KADEMLIA
		&& incoming->SupportsDirectUDPCallback()
		&& !incoming->HasValidServingBuddyID()
		&& IsKnownKadEndpoint(incoming->GetConnectIP(), incoming->GetKadPort());
	const bool bCanReplaceDetachedKadEndpoint = target->socket == NULL
		|| (!target->socket->IsConnected() && !target->socket->HaveNatTraversalLayer());
	if (bIncomingDirectKadEndpoint && bCanReplaceDetachedKadEndpoint) {
		if (target->GetConnectIP() != incoming->GetConnectIP()) {
			target->SetConnectIP(incoming->GetConnectIP());
			bUpdated = true;
		}
		if (target->GetIP() != incoming->GetIP() && !incoming->GetIP().IsNull()) {
			CAddress addr(incoming->GetIP());
			target->SetIP(addr);
			bUpdated = true;
		}
		if (target->GetKadPort() != incoming->GetKadPort()) {
			target->SetKadPort(incoming->GetKadPort());
			bUpdated = true;
		}
		if (incoming->GetUDPPort() != 0 && target->GetUDPPort() != incoming->GetUDPPort()) {
			target->SetUDPPort(incoming->GetUDPPort());
			bUpdated = true;
		}
	}

	if (bApplySourceFrom && target->GetSourceFrom() != eSourceFrom) {
		target->SetSourceFrom(eSourceFrom);
		bUpdated = true;
	}

	if (!target->HasValidHash() && incoming->HasValidHash()) {
		target->SetUserHash(incoming->GetUserHash());
		bUpdated = true;
	}

	if (target->GetKadPort() == 0 && incoming->GetKadPort() != 0) {
		target->SetKadPort(incoming->GetKadPort());
		bUpdated = true;
	}

	if (target->GetUDPPort() == 0 && incoming->GetUDPPort() != 0) {
		target->SetUDPPort(incoming->GetUDPPort());
		bUpdated = true;
	}

	if (target->GetConnectIP().IsNull() && !incoming->GetConnectIP().IsNull()) {
		target->SetConnectIP(incoming->GetConnectIP());
		bUpdated = true;
	}

	if (target->GetIP().IsNull() && !incoming->GetIP().IsNull()) {
		CAddress addr(incoming->GetIP());
		target->SetIP(addr);
		bUpdated = true;
	}

	if (!target->GetNatTraversalSupport() && incoming->GetNatTraversalSupport()) {
		target->SetNatTraversalSupport(true);
		bUpdated = true;
	}

	if (!target->GetNatTraversalQuicSupport() && incoming->GetNatTraversalQuicSupport()) {
		target->SetNatTraversalQuicSupport(true);
		bUpdated = true;
	}

	if (!target->SupportsDirectUDPCallback() && incoming->SupportsDirectUDPCallback()) {
		target->SetDirectUDPCallbackSupport(true);
		bUpdated = true;
	}

	if (bApplySourceFrom && incoming->HasValidServingBuddyID()) {
		if (bAcceptKadBuddyRoute) {
			const bool bTargetHasServingBuddyID = target->HasValidServingBuddyID();
			const bool bServingBuddyChanged = bTargetHasServingBuddyID && !md4equ(target->GetServingBuddyID(), incoming->GetServingBuddyID());
			if (!bTargetHasServingBuddyID || bServingBuddyChanged) {
				target->SetServingBuddyID(incoming->GetServingBuddyID());
				bUpdated = true;
			}
			if (!incoming->GetServingBuddyIP().IsNull() && target->GetServingBuddyIP() != incoming->GetServingBuddyIP()) {
				target->SetServingBuddyIP(incoming->GetServingBuddyIP());
				bUpdated = true;
			}
			if (incoming->GetServingBuddyPort() != 0 && target->GetServingBuddyPort() != incoming->GetServingBuddyPort()) {
				target->SetServingBuddyPort(incoming->GetServingBuddyPort());
				bUpdated = true;
			}
		} else if (thePrefs.GetLogNatTraversalEvents()) {
			if (!bRefreshKadBuddyRoute) {
				DebugLog(_T("[NatTraversal] CheckAndAddSource: ignored Kad duplicate buddy route %s:%u for %s because incoming endpoint %s:%u differs from known endpoint %s:%u"),
					(LPCTSTR)ipstr(incoming->GetServingBuddyIP()), incoming->GetServingBuddyPort(),
					(LPCTSTR)EscPercent(target->DbgGetClientInfo()),
					(LPCTSTR)ipstr(incoming->GetConnectIP()), incoming->GetKadPort(),
					(LPCTSTR)ipstr(target->GetConnectIP()), target->GetKadPort());
			} else {
				DebugLog(_T("[NatTraversal] CheckAndAddSource: kept current Kad buddy route %s:%u for %s instead of duplicate route %s:%u"),
					(LPCTSTR)ipstr(target->GetServingBuddyIP()), target->GetServingBuddyPort(),
					(LPCTSTR)EscPercent(target->DbgGetClientInfo()),
					(LPCTSTR)ipstr(incoming->GetServingBuddyIP()), incoming->GetServingBuddyPort());
			}
		}
	}

	return bUpdated;
}

static bool IsEServerBuddyRelayCandidate(const CUpDownClient* source)
{
	if (source == NULL || !source->HasLowID())
		return false;
	if (source->GetServerIP() == 0 || source->GetServerPort() == 0)
		return false;
	if (!thePrefs.IsNatTraversalServiceEnabled() || theApp.clientlist == NULL)
		return false;
	if (theApp.clientlist->GetEServerBuddyStatus() != Connected || theApp.clientlist->GetServingEServerBuddy() == NULL)
		return false;
	if (!theApp.clientlist->IsServingEServerBuddyRelayReady(theApp.clientlist->GetServingEServerBuddy()))
		return false;
	if (theApp.serverconnect == NULL || !theApp.serverconnect->IsConnected() || !theApp.serverconnect->IsLowID())
		return false;

	const CServer* pCurrentServer = theApp.serverconnect->GetCurrentServer();
	return pCurrentServer != NULL
		&& source->GetServerIP() == pCurrentServer->GetIP()
		&& source->GetServerPort() == pCurrentServer->GetPort();
}


CDownloadQueue::CDownloadQueue()
	: cur_udpserver()
	, m_datarateMS()
	, m_lastfile()
	, m_dwLastA4AFtime()
	, m_lastudpsearchtime()
	, m_lastudpstattime()
	, m_dwNextTCPSrcReq()
	, m_udcounter()
	, m_cRequestsSentToServer()
	, m_iSearchedServers()
	, m_nUDPFileReasks()
	, m_nFailedUDPFileReasks()
	, m_datarate()
	, m_uBulkAddDepth()
	, m_uBulkAddedFiles()
	, m_bBulkAddPending()
	, m_bBulkAddSuppressPerItemListUpdates()
	, m_bBulkAddDeferDownloadValidatorAdds()
	, m_bBulkAddOverviewExportDeferred()
	, m_bBulkAddDiskFinalizationActive()
	, m_uBulkAddDiskFinalizationTotal()
	, m_dwLastBulkAddDiskFinalizationNotifyTick()
	, m_lBulkAddDiskFinalizationProgressUpdatePending(0)
	, m_bStartupLoadActive()
	, m_bStartupLoadCompleted()

	, m_iStartupLoadTempDir()
	, m_iStartupLoadCount()

	, m_deferredDownloadValidatorAdds()
	, m_deferredDownloadValidatorAddPositions()
	, m_deferredInitialPartMetSaves()
	, m_posDeferredPartFileCreateQueueFile(NULL)
	, m_bDeferredPartFileCreateQueuePending(false)
	, m_deferredInitialPartMetSaveSet()
	, m_deferredInitialPartMetSavePositions()
	, m_deferredSourceSaves()
	, m_deferredSourceSaveSet()
	, m_bDeferredSourceSavesIncludePaused()
	, m_uBulkRemoveDepth()
	, m_uBulkRemovedFiles()
	, m_bBulkRemovePending()
	, m_bShutdownPartFilesSaved(false)
	, m_uShutdownPartFilesSavedCount(0)
	, m_uShutdownPartFileProgressTotal(0)
	, m_uModelSequence()
	, m_TCPFileReask()
	, m_FailedTCPFileReask()
	, m_uBufferedDownloadBytesSnapshot(0)
	, m_uLargestBufferedDownloadFileBytesSnapshot(0)
	, m_uAdaptiveGlobalDownloadBufferBudgetBytesSnapshot(0)
	, m_uBufferedDownloadFileCountSnapshot(0)
{
	m_mapFilesByHash.InitHashTable(131071);
	SetLastKademliaFileRequest();
}

CDownloadQueue::SStartupDownloadPartFile::SStartupDownloadPartFile()
	: pFile(NULL)
	, bRecoveredFromBackup(false)
{
}

CDownloadQueue::SStartupDownloadSortItem::SStartupDownloadSortItem()
	: pFile(NULL)
	, uCategory(0)
	, iCategoryPriority(0)
	, uDownPriority(0)
	, bAlphabetical(false)
{
}

CDownloadQueue::SStartupDownloadLoadResult::SStartupDownloadLoadResult()
	: lGeneration(0)
	, uCancellationToken(0)
	, bSuccess(false)
	, bApplyStarted(false)
	, dwLastError(0)
	, uNextPartFile(0)
	, uTempDirCount(0)
	, uLoadedCount(0)
	, uFinishStep(StartupDownloadFinishSort)
	, posFinishSortCollectFile(NULL)
	, posFinishSortApplyFile(NULL)
	, uNextFinishSortApply(0)
	, uFinishSortMergeWidth(1)
	, uFinishSortMergeLeft(0)
	, uFinishSortMergeMid(0)
	, uFinishSortMergeRight(0)
	, uFinishSortMergeI(0)
	, uFinishSortMergeJ(0)
	, uFinishSortMergeOut(0)
	, bFinishSortCollected(false)
	, bFinishSortSorted(false)
	, bFinishSortMergeActive(false)
	, posFinishDiskspaceFile(NULL)
	, bFinishDiskspaceStarted(false)
	, uFinishDiskspaceMainAvailable(0)
{
}

CDownloadQueue::SDeferredDownloadValidatorAdd::SDeferredDownloadValidatorAdd()
	: pFile(NULL)
	, uFileSize(0)
{
	memset(abyFileHash, 0, sizeof(abyFileHash));
}

void CDownloadQueue::DeleteStartupDownloadPartFiles(std::vector<SStartupDownloadPartFile> &vecPartFiles)
{
	for (size_t i = 0; i < vecPartFiles.size(); ++i) {
		if (vecPartFiles[i].pFile != NULL)
			vecPartFiles[i].pFile->SetSkipPartFileSaveOnDelete(true);
		delete vecPartFiles[i].pFile;
		vecPartFiles[i].pFile = NULL;
	}
	vecPartFiles.clear();
}

void CDownloadQueue::DeleteStartupDownloadLoadResult(SStartupDownloadLoadResult *pResult)
{
	if (pResult == NULL)
		return;
	for (size_t i = pResult->uNextPartFile; i < pResult->vecPartFiles.size(); ++i) {
		if (pResult->vecPartFiles[i].pFile != NULL)
			pResult->vecPartFiles[i].pFile->SetSkipPartFileSaveOnDelete(true);
		delete pResult->vecPartFiles[i].pFile;
		pResult->vecPartFiles[i].pFile = NULL;
	}
	delete pResult;
}

void CDownloadQueue::AddPartFilesToShare()
{
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *cur_file = filelist.GetNext(pos);
		if (cur_file->GetStatus(true) == PS_READY)
			theApp.sharedfiles->SafeAddKFile(cur_file, true);
	}
}
void CDownloadQueue::IndexDownloadFile(CPartFile *pFile)
{
	if (pFile == NULL)
		return;
	m_mapFilesByHash.SetAt(CCKey(pFile->GetFileHash()), pFile);
}

void CDownloadQueue::UnindexDownloadFile(const CPartFile *pFile)
{
	if (pFile == NULL)
		return;
	m_mapFilesByHash.RemoveKey(CCKey(pFile->GetFileHash()));
}

void CDownloadQueue::BuildDownloadSourceIndexSnapshot(CUpDownClient *client, SDownloadSourceIndexSnapshot& snapshot) const
{
	snapshot = SDownloadSourceIndexSnapshot();
	if (client == NULL)
		return;

	if (client->HasValidHash()) {
		snapshot.m_bHasHash = true;
		snapshot.m_hashKey = SDownloadSourceHashKey(client->GetUserHash());
	}

	CAddress aRegisteredEndpointIPs[4];
	int iRegisteredEndpointIPCount = 0;
	const CAddress* aEndpointIps[] = { &client->GetConnectIP(), &client->GetIP(), &client->GetIPv4(), &client->GetIPv6() };
	for (int i = 0; i < ARRSIZE(aEndpointIps); ++i) {
		if (aEndpointIps[i]->IsNull())
			continue;
		bool bSeen = false;
		for (int j = 0; j < iRegisteredEndpointIPCount; ++j) {
			if (aRegisteredEndpointIPs[j] == *aEndpointIps[i]) {
				bSeen = true;
				break;
			}
		}
		if (bSeen)
			continue;
		aRegisteredEndpointIPs[iRegisteredEndpointIPCount++] = *aEndpointIps[i];
		if (client->GetUserPort() != 0)
			snapshot.m_aEndpointKeys.push_back(SDownloadSourceEndpointKey(*aEndpointIps[i], client->GetUserPort()));
		if (client->GetKadPort() != 0)
			snapshot.m_aEndpointKeys.push_back(SDownloadSourceEndpointKey(*aEndpointIps[i], client->GetKadPort()));
	}

	CAddress aRegisteredIPs[4];
	int iRegisteredIPCount = 0;
	const CAddress* aLookupIps[] = { &client->GetConnectIP(), &client->GetIP(), &client->GetIPv4(), &client->GetIPv6() };
	for (int i = 0; i < ARRSIZE(aLookupIps); ++i) {
		if (aLookupIps[i]->IsNull())
			continue;
		bool bSeen = false;
		for (int j = 0; j < iRegisteredIPCount; ++j) {
			if (aRegisteredIPs[j] == *aLookupIps[i]) {
				bSeen = true;
				break;
			}
		}
		if (bSeen)
			continue;
		aRegisteredIPs[iRegisteredIPCount++] = *aLookupIps[i];
		snapshot.m_aIPKeys.push_back(*aLookupIps[i]);
		if (client->GetUDPPort() != 0)
			snapshot.m_aUDPKeys.push_back(SDownloadSourceEndpointKey(*aLookupIps[i], client->GetUDPPort()));
	}

	if (client->GetUserIDHybrid() != 0) {
		if (client->GetUserPort() != 0)
			snapshot.m_aIdPortKeys.push_back(SDownloadSourceIdPortKey(client->GetUserIDHybrid(), client->GetUserPort()));
		if (client->GetKadPort() != 0)
			snapshot.m_aIdPortKeys.push_back(SDownloadSourceIdPortKey(client->GetUserIDHybrid(), client->GetKadPort()));
	}
	if (client->HasLowID() && client->GetUserIDHybrid() != 0 && client->GetServerIP() != 0 && client->GetServerPort() != 0) {
		snapshot.m_bHasLowId = true;
		snapshot.m_lowIdKey = SDownloadSourceLowIdKey(client->GetUserIDHybrid(), client->GetServerIP(), client->GetServerPort());
	}
}

void CDownloadQueue::EraseDownloadSourceIndexSnapshot(DWORD uRuntimeID, const SDownloadSourceIndexSnapshot& snapshot)
{
	if (uRuntimeID == 0)
		return;
	if (snapshot.m_bHasHash)
		EraseDownloadSourceIndexEntries(m_mapDownloadSourcesByHash, snapshot.m_hashKey, uRuntimeID);
	for (std::vector<CAddress>::const_iterator it = snapshot.m_aIPKeys.begin(); it != snapshot.m_aIPKeys.end(); ++it)
		EraseDownloadSourceIndexEntries(m_mapDownloadSourcesByIP, *it, uRuntimeID);
	for (std::vector<SDownloadSourceEndpointKey>::const_iterator it = snapshot.m_aEndpointKeys.begin(); it != snapshot.m_aEndpointKeys.end(); ++it)
		EraseDownloadSourceIndexEntries(m_mapDownloadSourcesByEndpoint, *it, uRuntimeID);
	for (std::vector<SDownloadSourceEndpointKey>::const_iterator it = snapshot.m_aUDPKeys.begin(); it != snapshot.m_aUDPKeys.end(); ++it)
		EraseDownloadSourceIndexEntries(m_mapDownloadSourcesByUDP, *it, uRuntimeID);
	for (std::vector<SDownloadSourceIdPortKey>::const_iterator it = snapshot.m_aIdPortKeys.begin(); it != snapshot.m_aIdPortKeys.end(); ++it)
		EraseDownloadSourceIndexEntries(m_mapDownloadSourcesByIdPort, *it, uRuntimeID);
	if (snapshot.m_bHasLowId)
		EraseDownloadSourceIndexEntries(m_mapDownloadSourcesByLowId, snapshot.m_lowIdKey, uRuntimeID);
}

bool CDownloadQueue::IsDownloadSourceIndexSnapshotCurrent(CUpDownClient *client, DWORD uRuntimeID) const
{
	if (client == NULL || uRuntimeID == 0)
		return true;
	std::map<DWORD, SDownloadSourceIndexEntry>::const_iterator itEntry = m_mapDownloadSourceEntries.find(uRuntimeID);
	if (itEntry == m_mapDownloadSourceEntries.end())
		return false;
	SDownloadSourceIndexSnapshot snapshot;
	BuildDownloadSourceIndexSnapshot(client, snapshot);
	return snapshot == itEntry->second.m_snapshot;
}

void CDownloadQueue::RegisterDownloadSource(CPartFile *pFile, CUpDownClient *client)
{
	if (pFile == NULL || client == NULL)
		return;
	const DWORD uRuntimeID = client->GetRuntimeID();
	if (uRuntimeID == 0)
		return;

	std::map<DWORD, SDownloadSourceIndexEntry>::const_iterator itExisting = m_mapDownloadSourceEntries.find(uRuntimeID);
	if (itExisting != m_mapDownloadSourceEntries.end() && itExisting->second.m_pFile != pFile)
		TRACE(_T("Download source owner index changed. runtime=%lu\n"), uRuntimeID);
	UnregisterDownloadSource(NULL, client);
	SDownloadSourceIndexEntry entry;
	entry.m_pFile = pFile;
	entry.m_pClient = client;
	BuildDownloadSourceIndexSnapshot(client, entry.m_snapshot);
	if (entry.m_snapshot.m_bHasHash)
		m_mapDownloadSourcesByHash.insert(std::make_pair(entry.m_snapshot.m_hashKey, uRuntimeID));
	for (std::vector<CAddress>::const_iterator it = entry.m_snapshot.m_aIPKeys.begin(); it != entry.m_snapshot.m_aIPKeys.end(); ++it)
		m_mapDownloadSourcesByIP.insert(std::make_pair(*it, uRuntimeID));
	for (std::vector<SDownloadSourceEndpointKey>::const_iterator it = entry.m_snapshot.m_aEndpointKeys.begin(); it != entry.m_snapshot.m_aEndpointKeys.end(); ++it)
		m_mapDownloadSourcesByEndpoint.insert(std::make_pair(*it, uRuntimeID));
	for (std::vector<SDownloadSourceEndpointKey>::const_iterator it = entry.m_snapshot.m_aUDPKeys.begin(); it != entry.m_snapshot.m_aUDPKeys.end(); ++it)
		m_mapDownloadSourcesByUDP.insert(std::make_pair(*it, uRuntimeID));
	for (std::vector<SDownloadSourceIdPortKey>::const_iterator it = entry.m_snapshot.m_aIdPortKeys.begin(); it != entry.m_snapshot.m_aIdPortKeys.end(); ++it)
		m_mapDownloadSourcesByIdPort.insert(std::make_pair(*it, uRuntimeID));
	if (entry.m_snapshot.m_bHasLowId)
		m_mapDownloadSourcesByLowId.insert(std::make_pair(entry.m_snapshot.m_lowIdKey, uRuntimeID));
	m_mapDownloadSourceEntries[uRuntimeID] = entry;
}

void CDownloadQueue::UnregisterDownloadSource(CPartFile *pFile, CUpDownClient *client)
{
	if (client == NULL)
		return;
	const DWORD uRuntimeID = client->GetRuntimeID();
	if (uRuntimeID == 0)
		return;
	std::map<DWORD, SDownloadSourceIndexEntry>::iterator itEntry = m_mapDownloadSourceEntries.find(uRuntimeID);
	if (itEntry == m_mapDownloadSourceEntries.end())
		return;
	if (pFile != NULL && itEntry->second.m_pFile != pFile)
		return;
	EraseDownloadSourceIndexSnapshot(uRuntimeID, itEntry->second.m_snapshot);
	m_mapDownloadSourceEntries.erase(itEntry);
}

void CDownloadQueue::RefreshDownloadSource(CUpDownClient *client)
{
	if (client == NULL)
		return;
	const DWORD uRuntimeID = client->GetRuntimeID();
	if (uRuntimeID == 0)
		return;
	std::map<DWORD, SDownloadSourceIndexEntry>::const_iterator itEntry = m_mapDownloadSourceEntries.find(uRuntimeID);
	if (itEntry == m_mapDownloadSourceEntries.end())
		return;
	if (IsDownloadSourceIndexSnapshotCurrent(client, uRuntimeID))
		return;
	CPartFile *pFile = itEntry->second.m_pFile;
	UnregisterDownloadSource(NULL, client);
	RegisterDownloadSource(pFile, client);
}

CUpDownClient* CDownloadQueue::ResolveDownloadSourceRuntimeID(DWORD uRuntimeID) const
{
	if (uRuntimeID == 0)
		return NULL;
	std::map<DWORD, SDownloadSourceIndexEntry>::const_iterator itEntry = m_mapDownloadSourceEntries.find(uRuntimeID);
	if (itEntry == m_mapDownloadSourceEntries.end())
		return NULL;
	CUpDownClient *pClient = itEntry->second.m_pClient;
	CPartFile *pFile = itEntry->second.m_pFile;
	if (pClient == NULL || pFile == NULL || pClient->GetRuntimeID() != uRuntimeID || pClient->GetRequestFile() != pFile || !IsPartFile(pFile))
		return NULL;
	return pClient;
}

CPartFile* CDownloadQueue::GetDownloadSourceFile(CUpDownClient *client) const
{
	if (client == NULL)
		return NULL;
	const DWORD uRuntimeID = client->GetRuntimeID();
	if (uRuntimeID == 0)
		return NULL;
	std::map<DWORD, SDownloadSourceIndexEntry>::const_iterator itEntry = m_mapDownloadSourceEntries.find(uRuntimeID);
	if (itEntry == m_mapDownloadSourceEntries.end())
		return NULL;
	return ResolveDownloadSourceRuntimeID(uRuntimeID) != NULL ? itEntry->second.m_pFile : NULL;
}

CPartFile* CDownloadQueue::FindDownloadSourceOwnerByScan(CUpDownClient *source) const
{
	if (source == NULL)
		return NULL;
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *cur_file = filelist.GetNext(pos);
		if (cur_file != NULL && cur_file->srclist.Find(source) != NULL)
			return cur_file;
	}
	return NULL;
}

CUpDownClient* CDownloadQueue::FindDownloadDuplicateSourceByScan(CUpDownClient *source, CPartFile*& pOwnerFile) const
{
	pOwnerFile = NULL;
	if (source == NULL)
		return NULL;
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *cur_file = filelist.GetNext(pos);
		if (cur_file == NULL)
			continue;
		for (POSITION pos2 = cur_file->srclist.GetHeadPosition(); pos2 != NULL;) {
			CUpDownClient *cur_client = cur_file->srclist.GetNext(pos2);
			if (cur_client != NULL && cur_client != source && IsSameDownloadSourceIdentity(cur_client, source)) {
				pOwnerFile = cur_file;
				return cur_client;
			}
		}
	}
	return NULL;
}

bool CDownloadQueue::AddAlreadyKnownSourceAsA4AF(CPartFile *sender, CUpDownClient *source, CPartFile *pOwnerFile, LPCTSTR pszContext)
{
	if (sender == NULL || source == NULL || pOwnerFile == NULL || pOwnerFile == sender)
		return false;
	if (!source->AddRequestForAnotherFile(sender))
		return false;
	PrepareKnownSourceForFreshAsk(source, sender, source->GetRequestFile());
	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NatTraversal] CheckAndAddSource: A4AF - source already exists for another file, adding request\n"));
	CDownloadListCtrl *pDownloadList = GetDownloadListForDownloadQueueUi();
	if (pDownloadList != NULL)
		pDownloadList->AddSource(sender, source, true);
	else
		theApp.QueueDownloadListChangedEvent(_T("source-add-a4af"), CemuleApp::BackendCommandSourceNetworkClient);
	if (source->GetDownloadState() != DS_CONNECTED)
		source->SwapToAnotherFile(pszContext, false, false, false, NULL, true, false);
	return true;
}

CUpDownClient* CDownloadQueue::FindDownloadDuplicateSource(CUpDownClient *source, CPartFile*& pOwnerFile) const
{
	pOwnerFile = NULL;
	if (source == NULL)
		return NULL;

	if (source->HasValidHash()) {
		const SDownloadSourceHashKey key(source->GetUserHash());
		std::pair<std::multimap<SDownloadSourceHashKey, DWORD>::const_iterator, std::multimap<SDownloadSourceHashKey, DWORD>::const_iterator> range = m_mapDownloadSourcesByHash.equal_range(key);
		for (std::multimap<SDownloadSourceHashKey, DWORD>::const_iterator it = range.first; it != range.second; ++it) {
			CUpDownClient *pClient = ResolveDownloadSourceRuntimeID(it->second);
			if (pClient != NULL && pClient != source && pClient->Compare(source, false)) {
				pOwnerFile = GetDownloadSourceFile(pClient);
				if (pOwnerFile != NULL)
					return pClient;
			}
		}
	}

	const CAddress* aEndpointIps[] = { &source->GetConnectIP(), &source->GetIP(), &source->GetIPv4(), &source->GetIPv6() };
	for (int i = 0; i < ARRSIZE(aEndpointIps); ++i) {
		if (aEndpointIps[i]->IsNull())
			continue;
		const uint16 aPorts[] = { source->GetUserPort(), source->GetKadPort() };
		for (int j = 0; j < ARRSIZE(aPorts); ++j) {
			if (aPorts[j] == 0)
				continue;
			const SDownloadSourceEndpointKey key(*aEndpointIps[i], aPorts[j]);
			std::pair<std::multimap<SDownloadSourceEndpointKey, DWORD>::const_iterator, std::multimap<SDownloadSourceEndpointKey, DWORD>::const_iterator> range = m_mapDownloadSourcesByEndpoint.equal_range(key);
			for (std::multimap<SDownloadSourceEndpointKey, DWORD>::const_iterator it = range.first; it != range.second; ++it) {
				CUpDownClient *pClient = ResolveDownloadSourceRuntimeID(it->second);
				if (pClient != NULL && pClient != source && IsSameDownloadSourceIdentity(pClient, source)) {
					pOwnerFile = GetDownloadSourceFile(pClient);
					if (pOwnerFile != NULL)
						return pClient;
				}
			}
		}
	}

	if (source->GetUserIDHybrid() != 0) {
		const uint16 aPorts[] = { source->GetUserPort(), source->GetKadPort() };
		for (int i = 0; i < ARRSIZE(aPorts); ++i) {
			if (aPorts[i] == 0)
				continue;
			const SDownloadSourceIdPortKey key(source->GetUserIDHybrid(), aPorts[i]);
			std::pair<std::multimap<SDownloadSourceIdPortKey, DWORD>::const_iterator, std::multimap<SDownloadSourceIdPortKey, DWORD>::const_iterator> range = m_mapDownloadSourcesByIdPort.equal_range(key);
			for (std::multimap<SDownloadSourceIdPortKey, DWORD>::const_iterator it = range.first; it != range.second; ++it) {
				CUpDownClient *pClient = ResolveDownloadSourceRuntimeID(it->second);
				if (pClient != NULL && pClient != source && pClient->Compare(source, true)) {
					pOwnerFile = GetDownloadSourceFile(pClient);
					if (pOwnerFile != NULL)
						return pClient;
				}
			}
		}
	}

	if (source->HasLowID() && source->GetUserIDHybrid() != 0 && source->GetServerIP() != 0 && source->GetServerPort() != 0) {
		const SDownloadSourceLowIdKey key(source->GetUserIDHybrid(), source->GetServerIP(), source->GetServerPort());
		std::pair<std::multimap<SDownloadSourceLowIdKey, DWORD>::const_iterator, std::multimap<SDownloadSourceLowIdKey, DWORD>::const_iterator> range = m_mapDownloadSourcesByLowId.equal_range(key);
		for (std::multimap<SDownloadSourceLowIdKey, DWORD>::const_iterator it = range.first; it != range.second; ++it) {
			CUpDownClient *pClient = ResolveDownloadSourceRuntimeID(it->second);
			if (pClient != NULL && pClient != source && pClient->Compare(source, true)) {
				pOwnerFile = GetDownloadSourceFile(pClient);
				if (pOwnerFile != NULL)
					return pClient;
			}
		}
	}

	return NULL;
}

void CDownloadQueue::ClearDownloadSourceIndexes()
{
	m_mapDownloadSourceEntries.clear();
	m_mapDownloadSourcesByHash.clear();
	m_mapDownloadSourcesByIP.clear();
	m_mapDownloadSourcesByEndpoint.clear();
	m_mapDownloadSourcesByUDP.clear();
	m_mapDownloadSourcesByIdPort.clear();
	m_mapDownloadSourcesByLowId.clear();
}


void CDownloadQueue::Init()
{
	if (!m_bStartupLoadActive && !m_bStartupLoadCompleted)
		theApp.BeginStartupDownloadsLoad();
}

void CDownloadQueue::BeginStartupLoad()
{
	if (m_bStartupLoadActive || m_bStartupLoadCompleted)
		return;

	m_bStartupLoadActive = true;
	m_bStartupLoadCompleted = false;
	m_iStartupLoadTempDir = 0;
	m_iStartupLoadCount = 0;
	m_iStartupLoadStagedCount = 0;
}

bool CDownloadQueue::LoadStartupPartFilesForWorker(SStartupDownloadLoadResult &result)
{
	result.bSuccess = false;
	result.dwLastError = 0;
	result.strStage = _T("load-started");
	result.uTempDirCount = static_cast<UINT>(max(0, thePrefs.GetTempDirCount()));

	CMapStringToPtr loadedHashes;
	for (INT_PTR iTempDir = 0; iTempDir < thePrefs.GetTempDirCount(); ++iTempDir) {
		PublishStartupLoadWorkerProgress(iTempDir, result.uLoadedCount);
		const CString strTempDir(thePrefs.GetTempDir(iTempDir));
		for (int iPass = 0; iPass < 2; ++iPass) {
			const bool bBackupPass = iPass != 0;
			CString strSearchPath;
			strSearchPath.Format(_T("%s*.part.met"), (LPCTSTR)strTempDir);
			if (bBackupPass)
				strSearchPath += _T(".backup");

			CFileFind finder;
			BOOL bFindNext = finder.FindFile(strSearchPath);
			while (bFindNext) {
				if (theApp.IsStartupMetadataLoadCancelled(CemuleApp::StartupMetadataDownloads, result.lGeneration, result.uCancellationToken)) {
					finder.Close();
					DeleteStartupDownloadPartFiles(result.vecPartFiles);
					result.dwLastError = ERROR_CANCELLED;
					result.strStage = _T("load-cancelled");
					return false;
				}

				bFindNext = finder.FindNextFile();
				if (finder.IsDirectory())
					continue;

				bool bRecoveredFromBackup = bBackupPass;
				CPartFile *pPartFile = new CPartFile();
				EPartFileLoadResult eResult = pPartFile->LoadPartFile(strTempDir, finder.GetFileName(), NULL, true);
				if (!bBackupPass && eResult == PLR_FAILED_METFILE_CORRUPT) {
					pPartFile->SetSkipPartFileSaveOnDelete(true);
					delete pPartFile;
					pPartFile = new CPartFile();
					eResult = pPartFile->LoadPartFile(strTempDir, finder.GetFileName() + PARTMET_BAK_EXT, NULL, true);
					bRecoveredFromBackup = (eResult == PLR_LOADSUCCESS);
				}

				if (eResult == PLR_LOADSUCCESS) {
					CString strHash(md4str(pPartFile->GetFileHash()));
					void *pExisting = NULL;
					if (loadedHashes.Lookup(strHash, pExisting)) {
						AddDebugLogLine(DLP_LOW, false, _T("Skipping staged startup part.met load because the same hash was already staged. file=%s\n"), (LPCTSTR)pPartFile->GetFileName());
						pPartFile->SetSkipPartFileSaveOnDelete(true);
						delete pPartFile;
						pPartFile = NULL;
					}
					else {
						loadedHashes.SetAt(strHash, pPartFile);
						SStartupDownloadPartFile partFile;
						partFile.pFile = pPartFile;
						partFile.bRecoveredFromBackup = bRecoveredFromBackup;
						result.vecPartFiles.push_back(partFile);
						++result.uLoadedCount;
						if ((result.uLoadedCount & 0x1f) == 0)
							PublishStartupLoadWorkerProgress(iTempDir, result.uLoadedCount);
					}
				}
				else {
					pPartFile->SetSkipPartFileSaveOnDelete(true);
					delete pPartFile;
					pPartFile = NULL;
				}
			}
			finder.Close();
		}
	}
	PublishStartupLoadWorkerProgress(thePrefs.GetTempDirCount() - 1, result.uLoadedCount);

	result.bSuccess = true;
	result.strStage = _T("load-completed");
	return true;
}

bool CDownloadQueue::ApplyStartupDownloadLoadResult(SStartupDownloadLoadResult *pResult, size_t uMaxFiles, UINT &uApplied, INT_PTR &iRemaining)
{
	uApplied = 0;
	iRemaining = 0;
	if (pResult == NULL)
		return true;
	if (!pResult->bSuccess) {
		DeleteStartupDownloadPartFiles(pResult->vecPartFiles);
		m_bStartupLoadActive = false;
		m_bStartupLoadCompleted = true;
		return true;
	}

	const size_t uLimit = uMaxFiles != 0 ? uMaxFiles : static_cast<size_t>(-1);
	const DWORD dwSliceStart = ::GetTickCount();
	while (pResult->uNextPartFile < pResult->vecPartFiles.size() && uApplied < uLimit) {
		SStartupDownloadPartFile &partFile = pResult->vecPartFiles[pResult->uNextPartFile++];
		CPartFile *pFile = partFile.pFile;
		partFile.pFile = NULL;
		if (pFile == NULL)
			continue;

		if (IsFileExisting(pFile->GetFileHash(), false)) {
			AddDebugLogLine(DLP_LOW, false, _T("Skipping startup part.met attach because a live download with the same hash already exists. file=%s\n"), (LPCTSTR)pFile->GetFileName());
			pFile->SetSkipPartFileSaveOnDelete(true);
			delete pFile;
			++uApplied;
			continue;
		}

		if (partFile.bRecoveredFromBackup) {
			pFile->SavePartFile(true);
			AddLogLine(false, GetResString(_T("RECOVERED_PARTMET")), (LPCTSTR)EscPercent(pFile->GetFileName()));
		}

		++m_iStartupLoadCount;
		filelist.AddTail(pFile);
		IndexDownloadFile(pFile);
		TouchDownloadModelSequence();
		if (pFile->GetStatus(true) == PS_READY && theApp.sharedfiles != NULL)
			theApp.sharedfiles->SafeAddKFile(pFile);
		if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL && theApp.emuledlg->transferwnd->GetDownloadList() != NULL)
			theApp.emuledlg->transferwnd->GetDownloadList()->AddFile(pFile, true);
		if (pFile->GetStatus(true) == PS_WAITINGFORHASH)
			pFile->StartPartFileRehash();
		else if (pFile->GetStatus(true) == PS_COMPLETING)
			pFile->StartDeferredCompletionHash();
		++uApplied;
		if (uApplied != 0 && theApp.emuledlg != NULL && theApp.emuledlg->IsStartupLoadingDialogVisible()) {
			const UINT uQueueStatus = HIWORD(::GetQueueStatus(QS_KEY | QS_MOUSE | QS_PAINT));
			if ((uQueueStatus & (QS_KEY | QS_MOUSE | QS_PAINT)) != 0)
				break;
		}
		if (uApplied != 0 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetStartupApply))
			break;
	}

	iRemaining = static_cast<INT_PTR>(pResult->vecPartFiles.size() - min(pResult->uNextPartFile, pResult->vecPartFiles.size()));
	if (pResult->uNextPartFile < pResult->vecPartFiles.size())
		return false;

	UINT uFinishProcessed = 0;
	INT_PTR iFinishRemaining = 0;
	const bool bFinished = FinishStartupLoadStep(pResult, uFinishProcessed, iFinishRemaining);
	uApplied += uFinishProcessed;
	iRemaining = iFinishRemaining;
	return bFinished;
}

static bool StartupDownloadSortItemRightHasHigherPriority(const CDownloadQueue::SStartupDownloadSortItem& left, const CDownloadQueue::SStartupDownloadSortItem& right)
{
	if (right.pFile == NULL)
		return false;
	if (left.pFile == NULL)
		return true;

	return right.iCategoryPriority > left.iCategoryPriority
		|| (right.iCategoryPriority == left.iCategoryPriority
			&& (right.uDownPriority > left.uDownPriority
				|| (right.uDownPriority == left.uDownPriority
					&& right.uCategory != 0 && right.uCategory == left.uCategory
					&& right.bAlphabetical
					&& !right.strFileName.IsEmpty() && !left.strFileName.IsEmpty()
					&& right.strFileName.CompareNoCase(left.strFileName) < 0
				   )
			   )
		   );
}

static bool StartupDownloadSortItemLeftHasHigherPriority(const CDownloadQueue::SStartupDownloadSortItem& left, const CDownloadQueue::SStartupDownloadSortItem& right)
{
	return StartupDownloadSortItemRightHasHigherPriority(right, left);
}

static void BuildStartupDownloadSortItem(CPartFile *pFile, CDownloadQueue::SStartupDownloadSortItem& item)
{
	item = CDownloadQueue::SStartupDownloadSortItem();
	item.pFile = pFile;
	if (pFile == NULL)
		return;

	item.uCategory = pFile->GetCategory();
	item.uDownPriority = pFile->GetDownPriority();
	const Category_Struct *pCategory = thePrefs.GetCategory(item.uCategory);
	if (pCategory != NULL) {
		item.iCategoryPriority = pCategory->prio;
		item.bAlphabetical = pCategory->downloadInAlphabeticalOrder && thePrefs.IsExtControlsEnabled();
	}
	if (item.bAlphabetical)
		item.strFileName = pFile->GetFileName();
}

bool CDownloadQueue::SortStartupDownloadsByPrioritySlice(SStartupDownloadLoadResult *pResult, UINT &uProcessed, INT_PTR &iRemaining)
{
	uProcessed = 0;
	iRemaining = 0;
	if (pResult == NULL)
		return true;

	const DWORD dwSliceStart = ::GetTickCount();
	if (!pResult->bFinishSortCollected) {
		if (pResult->posFinishSortCollectFile == NULL && pResult->vecFinishSortItems.empty()) {
			pResult->posFinishSortCollectFile = filelist.GetHeadPosition();
			const INT_PTR iFileCount = filelist.GetCount();
			pResult->vecFinishSortItems.reserve(static_cast<size_t>(iFileCount > 0 ? iFileCount : 0));
		}

		while (pResult->posFinishSortCollectFile != NULL) {
			SStartupDownloadSortItem item;
			BuildStartupDownloadSortItem(filelist.GetNext(pResult->posFinishSortCollectFile), item);
			pResult->vecFinishSortItems.push_back(item);
			++uProcessed;
			if (theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetStartupApply)) {
				iRemaining = 1;
				return false;
			}
		}
		pResult->bFinishSortCollected = true;
	}

	if (!pResult->bFinishSortSorted) {
		const size_t uSortCount = pResult->vecFinishSortItems.size();
		if (uSortCount <= 1)
			pResult->bFinishSortSorted = true;
		else {
			if (pResult->vecFinishSortScratch.size() != uSortCount)
				pResult->vecFinishSortScratch.resize(uSortCount);
			if (pResult->uFinishSortMergeWidth == 0)
				pResult->uFinishSortMergeWidth = 1;

			while (pResult->uFinishSortMergeWidth < uSortCount) {
				if (!pResult->bFinishSortMergeActive) {
					if (pResult->uFinishSortMergeLeft >= uSortCount) {
						pResult->vecFinishSortItems.swap(pResult->vecFinishSortScratch);
						pResult->uFinishSortMergeWidth *= 2;
						pResult->uFinishSortMergeLeft = 0;
						continue;
					}

					pResult->uFinishSortMergeMid = min(pResult->uFinishSortMergeLeft + pResult->uFinishSortMergeWidth, uSortCount);
					pResult->uFinishSortMergeRight = min(pResult->uFinishSortMergeLeft + pResult->uFinishSortMergeWidth * 2, uSortCount);
					pResult->uFinishSortMergeI = pResult->uFinishSortMergeLeft;
					pResult->uFinishSortMergeJ = pResult->uFinishSortMergeMid;
					pResult->uFinishSortMergeOut = pResult->uFinishSortMergeLeft;
					pResult->bFinishSortMergeActive = true;
				}

				while (pResult->uFinishSortMergeOut < pResult->uFinishSortMergeRight) {
					if (pResult->uFinishSortMergeJ < pResult->uFinishSortMergeRight && (pResult->uFinishSortMergeI >= pResult->uFinishSortMergeMid || StartupDownloadSortItemLeftHasHigherPriority(pResult->vecFinishSortItems[pResult->uFinishSortMergeJ], pResult->vecFinishSortItems[pResult->uFinishSortMergeI])))
						pResult->vecFinishSortScratch[pResult->uFinishSortMergeOut++] = pResult->vecFinishSortItems[pResult->uFinishSortMergeJ++];
					else
						pResult->vecFinishSortScratch[pResult->uFinishSortMergeOut++] = pResult->vecFinishSortItems[pResult->uFinishSortMergeI++];
					++uProcessed;
					if ((uProcessed % 128) == 0 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetStartupApply)) {
						iRemaining = 1;
						return false;
					}
				}

				pResult->bFinishSortMergeActive = false;
				pResult->uFinishSortMergeLeft = pResult->uFinishSortMergeRight;
				if (theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetStartupApply)) {
					iRemaining = 1;
					return false;
				}
			}
			pResult->bFinishSortSorted = true;
		}

		pResult->vecFinishSortScratch.clear();
		pResult->uFinishSortMergeWidth = 1;
		pResult->uFinishSortMergeLeft = 0;
		pResult->uFinishSortMergeMid = 0;
		pResult->uFinishSortMergeRight = 0;
		pResult->uFinishSortMergeI = 0;
		pResult->uFinishSortMergeJ = 0;
		pResult->uFinishSortMergeOut = 0;
		pResult->bFinishSortMergeActive = false;
		pResult->posFinishSortApplyFile = filelist.GetHeadPosition();
		pResult->uNextFinishSortApply = 0;
		iRemaining = pResult->vecFinishSortItems.empty() ? 0 : 1;
		return pResult->vecFinishSortItems.empty();
	}

	while (pResult->posFinishSortApplyFile != NULL && pResult->uNextFinishSortApply < pResult->vecFinishSortItems.size()) {
		POSITION posApply = pResult->posFinishSortApplyFile;
		filelist.GetNext(pResult->posFinishSortApplyFile);
		filelist.SetAt(posApply, pResult->vecFinishSortItems[pResult->uNextFinishSortApply++].pFile);
		++uProcessed;
		if (theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetStartupApply)) {
			iRemaining = 1;
			return false;
		}
	}

	pResult->vecFinishSortItems.clear();
	pResult->posFinishSortCollectFile = NULL;
	pResult->posFinishSortApplyFile = NULL;
	pResult->uNextFinishSortApply = 0;
	return true;
}

bool CDownloadQueue::CheckStartupDiskspaceSlice(SStartupDownloadLoadResult *pResult, UINT &uProcessed, INT_PTR &iRemaining)
{
	uProcessed = 0;
	iRemaining = 0;
	if (pResult == NULL)
		return true;

	if (!pResult->bFinishDiskspaceStarted) {
		pResult->bFinishDiskspaceStarted = true;
		pResult->posFinishDiskspaceFile = filelist.GetHeadPosition();
		pResult->uFinishDiskspaceMainAvailable = thePrefs.IsCheckDiskspaceEnabled() ? GetFreeDiskSpaceX(thePrefs.GetTempDir(), true) : 0;
	}

	const DWORD dwSliceStart = ::GetTickCount();
	while (pResult->posFinishDiskspaceFile != NULL) {
		CPartFile* cur_file = filelist.GetNext(pResult->posFinishDiskspaceFile);
		if (cur_file == NULL)
			continue;

		switch (cur_file->GetStatus()) {
		case PS_PAUSED:
		case PS_ERROR:
		case PS_COMPLETING:
		case PS_COMPLETE:
			break;
		default:
			if (!thePrefs.IsCheckDiskspaceEnabled())
				cur_file->ResumeFileInsufficient();
			else {
				uint64 nTotalAvailableSpace = thePrefs.GetTempDirCount() == 1 ? pResult->uFinishDiskspaceMainAvailable : GetFreeDiskSpaceX(cur_file->GetTmpPath(), true);
				if (thePrefs.GetMinFreeDiskSpace() == 0) {
					if (cur_file->GetNeededSpace() <= nTotalAvailableSpace)
						cur_file->ResumeFileInsufficient();
					else
						cur_file->PauseFile(true);
				} else if (nTotalAvailableSpace < thePrefs.GetMinFreeDiskSpace()) {
					if (!cur_file->IsNormalFile() || cur_file->GetNeededSpace() > 0)
						cur_file->PauseFile(true);
				}
			}
		}

		++uProcessed;
		if (theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetStartupApply))
			break;
	}

	if (pResult->posFinishDiskspaceFile != NULL) {
		iRemaining = 1;
		return false;
	}

	return true;
}

bool CDownloadQueue::FinishStartupLoadStep(SStartupDownloadLoadResult *pResult, UINT &uProcessed, INT_PTR &iRemaining)
{
	uProcessed = 0;
	iRemaining = 0;
	if (pResult == NULL)
		return true;

	if (m_iStartupLoadCount > 0) {
		switch (pResult->uFinishStep) {
		case StartupDownloadFinishSort:
			if (!SortStartupDownloadsByPrioritySlice(pResult, uProcessed, iRemaining))
				return false;
			pResult->uFinishStep = StartupDownloadFinishDiskspace;
			iRemaining = 1;
			return false;
		case StartupDownloadFinishDiskspace:
			if (!CheckStartupDiskspaceSlice(pResult, uProcessed, iRemaining))
				return false;
			pResult->uFinishStep = StartupDownloadFinishDownloadListUpdate;
			iRemaining = 1;
			return false;
		case StartupDownloadFinishDownloadListUpdate:
			if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL && theApp.emuledlg->transferwnd->GetDownloadList() != NULL) {
				CDownloadListCtrl* pDownloadList = theApp.emuledlg->transferwnd->GetDownloadList();
				if (theApp.emuledlg->IsStartupLoadingDialogVisible()) {
					pDownloadList->MarkDeferredReload();
					pDownloadList->RefreshBulkAddDisplayCounts();
				}
				else
					pDownloadList->FlushBulkAddListUpdate(LSF_SELECTION);
			}
			pResult->uFinishStep = StartupDownloadFinishSharedListUpdate;
			iRemaining = 1;
			return false;
		case StartupDownloadFinishSharedListUpdate:
			if (theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL && !theApp.emuledlg->IsStartupLoadingDialogVisible())
				theApp.emuledlg->sharedfileswnd->sharedfilesctrl.FlushBulkAddListUpdate(LSF_SELECTION);
			pResult->uFinishStep = StartupDownloadFinishPartFileCreates;
			iRemaining = 1;
			return false;
		case StartupDownloadFinishPartFileCreates:
			m_bBulkAddOverviewExportDeferred = true;
			ProcessDeferredPartFileCreates(false);
			pResult->uFinishStep = StartupDownloadFinishInitialPartMetSaves;
			iRemaining = 1;
			return false;
		case StartupDownloadFinishInitialPartMetSaves:
			ProcessDeferredInitialPartMetSaves(false);
			pResult->uFinishStep = StartupDownloadFinishComplete;
			iRemaining = 1;
			return false;
		default:
			break;
		}
	}

	if (m_bStartupLoadActive)
		m_bStartupLoadActive = false;
	m_bStartupLoadCompleted = true;
	m_iStartupLoadStagedCount = 0;

	if (m_iStartupLoadCount == 0)
		AddLogLine(false, GetResString(_T("NOPARTSFOUND")));
	else
		AddLogLine(false, GetResString(_T("FOUNDPARTS")), m_iStartupLoadCount);

	VERIFY(m_srcwnd.CreateEx(0, AfxRegisterWndClass(0), _T("eMule Async DNS Resolve Socket Wnd #2"), WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL));

	if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL && theApp.emuledlg->transferwnd->m_pwndTransfer != NULL)
		theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
	return true;
}

void CDownloadQueue::CancelStartupLoad()
{
	if (m_bStartupLoadActive)
		m_bStartupLoadActive = false;
	m_bStartupLoadCompleted = true;
	m_iStartupLoadStagedCount = 0;
}

void CDownloadQueue::GetStartupLoadProgress(UINT& uLoaded, UINT& uTempDirIndex, UINT& uTempDirCount) const
{
	uLoaded = static_cast<UINT>(max(0, max(m_iStartupLoadCount, m_iStartupLoadStagedCount)));
	uTempDirCount = static_cast<UINT>(max(0, thePrefs.GetTempDirCount()));
	const int iVisibleTempDir = min(max(0, m_iStartupLoadTempDir + 1), max(1, thePrefs.GetTempDirCount()));
	uTempDirIndex = static_cast<UINT>(iVisibleTempDir);
}

void CDownloadQueue::PublishStartupLoadWorkerProgress(INT_PTR iTempDir, UINT uStagedCount)
{
	m_iStartupLoadTempDir = iTempDir;
	m_iStartupLoadStagedCount = static_cast<int>(uStagedCount);
	const DWORD dwNow = ::GetTickCount();
	if (theApp.emuledlg != NULL && (m_dwLastStartupLoadDisplayRefreshTick == 0 || dwNow - m_dwLastStartupLoadDisplayRefreshTick >= 150)) {
		m_dwLastStartupLoadDisplayRefreshTick = dwNow;
		theApp.emuledlg->PostStartupOverlayRefresh();
	}
}


CDownloadQueue::~CDownloadQueue()
{
	m_bStartupLoadActive = false;
	ProcessDeferredPartFileCreates(true);
	ProcessDeferredDownloadValidatorAdds(true);
	ProcessDeferredInitialPartMetSaves(true);
	while (!m_deferredDownloadValidatorAdds.IsEmpty())
		delete m_deferredDownloadValidatorAdds.RemoveHead();
	m_deferredDownloadValidatorAddPositions.clear();
	m_deferredInitialPartMetSaves.RemoveAll();
	m_deferredInitialPartMetSaveSet.clear();
	m_deferredInitialPartMetSavePositions.clear();
	m_deferredSourceSaves.clear();
	m_deferredSourceSaveSet.clear();
	m_bDeferredSourceSavesIncludePaused = false;
	m_bBulkAddDiskFinalizationActive = false;
	m_uBulkAddDiskFinalizationTotal = 0;
	m_dwLastBulkAddDiskFinalizationNotifyTick = 0;
	::InterlockedExchange(&m_lBulkAddDiskFinalizationProgressUpdatePending, 0);
	m_mapFilesByHash.RemoveAll();
	const UINT uShutdownFileTotal = static_cast<UINT>(min(static_cast<INT_PTR>(UINT_MAX), filelist.GetCount()));
	const uint64 uShutdownProgressDoubleTotal = static_cast<uint64>(uShutdownFileTotal) * 2ULL;
	UINT uProgressTotal = m_uShutdownPartFileProgressTotal;
	if (uProgressTotal == 0)
		uProgressTotal = max(1U, m_bShutdownPartFilesSaved ? static_cast<UINT>(uShutdownProgressDoubleTotal > UINT_MAX ? UINT_MAX : uShutdownProgressDoubleTotal) : uShutdownFileTotal);
	const UINT uProgressBase = m_bShutdownPartFilesSaved ? min(uProgressTotal, m_uShutdownPartFilesSavedCount) : 0U;
	UINT uShutdownFileDone = 0;
	DWORD dwLastProgressUpdate = ::GetTickCount();
	while (!filelist.IsEmpty()) {
		if (theApp.emuledlg != NULL && static_cast<DWORD>(::GetTickCount() - dwLastProgressUpdate) >= 100) {
			const UINT uProgressDone = static_cast<UINT>(min(static_cast<uint64>(uProgressTotal), static_cast<uint64>(uProgressBase) + uShutdownFileDone));
			theApp.emuledlg->UpdateShutdownProgress(CemuleDlg::ShutdownProgressDownloads, uProgressDone, uProgressTotal, false);
			dwLastProgressUpdate = ::GetTickCount();
		}
		delete filelist.RemoveHead();
		++uShutdownFileDone;
		const DWORD dwNow = ::GetTickCount();
		if (theApp.emuledlg != NULL && (((uShutdownFileDone & 0x0F) == 0) || static_cast<DWORD>(dwNow - dwLastProgressUpdate) >= 100)) {
			const UINT uProgressDone = static_cast<UINT>(min(static_cast<uint64>(uProgressTotal), static_cast<uint64>(uProgressBase) + uShutdownFileDone));
			theApp.emuledlg->UpdateShutdownProgress(CemuleDlg::ShutdownProgressDownloads, uProgressDone, uProgressTotal, false);
			dwLastProgressUpdate = dwNow;
		}
	}
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->UpdateShutdownProgress(CemuleDlg::ShutdownProgressDownloads, uProgressTotal, uProgressTotal, true);
	m_srcwnd.DestroyWindow(); // just to avoid an MFC warning
}

void CDownloadQueue::SavePartFilesForShutdown()
{
	const UINT uShutdownFileTotal = static_cast<UINT>(min(static_cast<INT_PTR>(UINT_MAX), filelist.GetCount()));
	const uint64 uShutdownProgressDoubleTotal = static_cast<uint64>(uShutdownFileTotal) * 2ULL;
	const UINT uProgressTotal = max(1U, static_cast<UINT>(uShutdownProgressDoubleTotal > UINT_MAX ? UINT_MAX : uShutdownProgressDoubleTotal));
	UINT uShutdownFileDone = 0;
	DWORD dwLastProgressUpdate = ::GetTickCount();
	m_bShutdownPartFilesSaved = false;
	m_uShutdownPartFilesSavedCount = 0;
	m_uShutdownPartFileProgressTotal = uProgressTotal;
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->UpdateShutdownProgress(CemuleDlg::ShutdownProgressDownloads, 0, uProgressTotal, true);

	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile* pFile = filelist.GetNext(pos);
		if (theApp.emuledlg != NULL && static_cast<DWORD>(::GetTickCount() - dwLastProgressUpdate) >= 100) {
			theApp.emuledlg->UpdateShutdownProgress(CemuleDlg::ShutdownProgressDownloads, min(uShutdownFileDone, uProgressTotal), uProgressTotal, false);
			dwLastProgressUpdate = ::GetTickCount();
		}
		if (pFile != NULL && pFile->SavePartFile())
			pFile->SetSkipPartFileSaveOnDelete(true);
		++uShutdownFileDone;
		m_uShutdownPartFilesSavedCount = uShutdownFileDone;
		const DWORD dwNow = ::GetTickCount();
		if (theApp.emuledlg != NULL && (((uShutdownFileDone & 0x0F) == 0) || static_cast<DWORD>(dwNow - dwLastProgressUpdate) >= 100)) {
			theApp.emuledlg->UpdateShutdownProgress(CemuleDlg::ShutdownProgressDownloads, min(uShutdownFileDone, uProgressTotal), uProgressTotal, false);
			dwLastProgressUpdate = dwNow;
		}
	}

	m_bShutdownPartFilesSaved = true;
	m_uShutdownPartFilesSavedCount = min(uShutdownFileDone, uProgressTotal);
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->UpdateShutdownProgress(CemuleDlg::ShutdownProgressDownloads, m_uShutdownPartFilesSavedCount, uProgressTotal, true);
}

CDownloadQueue::CBulkAddScope::CBulkAddScope(CDownloadQueue* pQueue)
	: m_pQueue(pQueue)
{
	if (m_pQueue != NULL)
		m_pQueue->BeginBulkAddDownloads();
}

CDownloadQueue::CBulkAddScope::~CBulkAddScope()
{
	if (m_pQueue != NULL)
		m_pQueue->EndBulkAddDownloads();
}

bool CDownloadQueue::QueuePendingPartFileCreates(bool bDrainAll, DWORD dwSliceStart, UINT &uProcessed)
{
	if (!bDrainAll && IsBulkAddingDownloads())
		return false;
	if (!m_bDeferredPartFileCreateQueuePending)
		return false;
	if (m_posDeferredPartFileCreateQueueFile == NULL)
		m_posDeferredPartFileCreateQueueFile = filelist.GetHeadPosition();

	static const UINT kMaxQueuedPartFileCreatesPerSlice = 512;
	bool bQueued = false;
	bool bQueueFailed = false;
	UINT uVisited = 0;
	while (m_posDeferredPartFileCreateQueueFile != NULL) {
		POSITION posCurrent = m_posDeferredPartFileCreateQueueFile;
		CPartFile* pFile = filelist.GetNext(m_posDeferredPartFileCreateQueueFile);
		++uVisited;
		if (pFile != NULL && pFile->HasUnqueuedPartFileDiskCreate()) {
			if (pFile->QueuePendingPartFileDiskCreate())
				bQueued = true;
			else {
				bQueueFailed = true;
				m_posDeferredPartFileCreateQueueFile = posCurrent;
			}
			++uProcessed;
		}
		if (bQueueFailed)
			break;
		if (!bDrainAll && uProcessed >= kMaxQueuedPartFileCreatesPerSlice)
			break;
		if (!bDrainAll && (uProcessed != 0 || (uVisited & 0xFF) == 0) && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetDownloadAdd))
			break;
	}

	if (m_posDeferredPartFileCreateQueueFile == NULL) {
		if (bQueueFailed) {
			m_bDeferredPartFileCreateQueuePending = true;
			m_posDeferredPartFileCreateQueueFile = filelist.GetHeadPosition();
			AddDebugLogLine(DLP_HIGH, false, _T("Deferred bulk part file create queueing will retry after writer queue pressure or unavailable writer.\n"));
		}
		else {
			m_bDeferredPartFileCreateQueuePending = false;
			if (bQueued && thePrefs.GetLogUiResponsivenessEvents())
				AddDebugLogLine(DLP_LOW, false, _T("Deferred bulk part file create jobs queued.\n"));
		}
	}
	return bQueued;
}

UINT CDownloadQueue::ProcessDeferredPartFileCreateResults(bool bDrainAll, DWORD dwSliceStart, UINT &uProcessed, UINT uMaxResults)
{
	CPartFileWriteThread* pThread = theApp.m_pPartFileWriteThread;
	if (pThread == NULL)
		return 0;

	UINT uResults = 0;
	for (;;) {
		PartFileCreateResult* pResult = NULL;
		if (!pThread->PopPartFileCreateResult(pResult))
			break;
		if (pResult == NULL)
			continue;

		CPartFile* pFile = GetFileByID(pResult->abyHash);
		if (pFile != NULL && pFile->GetRuntimeID() == pResult->uRuntimeID) {
			if (pFile->ApplyPartFileDiskCreateResult(*pResult)) {
				pResult->hFile = INVALID_HANDLE_VALUE;
				if (pFile->HasDeferredInitialPartMetSave())
					QueueDeferredInitialPartMetSave(pFile);
			}
			else
				CleanupRejectedPartFileCreateResult(*pResult);
		} else
			CleanupRejectedPartFileCreateResult(*pResult);
		delete pResult;

		++uProcessed;
		++uResults;
		if (uResults >= uMaxResults)
			break;
		if (!bDrainAll && uProcessed != 0 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetDownloadAdd))
			break;
	}
	return uResults;
}

void CDownloadQueue::ProcessDeferredPartFileCreates(bool bDrainAll)
{
	CPartFileWriteThread* pThread = theApp.m_pPartFileWriteThread;
	if (pThread == NULL) {
		if (bDrainAll && theApp.IsClosing())
			FlushPendingPartFileCreatesSynchronously();
		return;
	}
	if (!bDrainAll && IsBulkAddingDownloads())
		return;

	const DWORD dwSliceStart = ::GetTickCount();
	UINT uProcessed = 0;
	ProcessDeferredPartFileCreateResults(bDrainAll, dwSliceStart, uProcessed, bDrainAll ? UINT_MAX : 64);
	if (!bDrainAll && uProcessed != 0 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetDownloadAdd))
		return;

	const bool bShutdownDiskDrain = bDrainAll && (theApp.IsClosing() || theApp.GetBackendLifecycleState() >= CemuleApp::BackendLifecycleDrainingDiskIo);
	const bool bQueuedCreates = bShutdownDiskDrain ? (FlushPendingPartFileCreatesSynchronously() != 0) : QueuePendingPartFileCreates(bDrainAll, dwSliceStart, uProcessed);
	if (!bShutdownDiskDrain && !bDrainAll && uProcessed != 0 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetDownloadAdd)) {
		if (bQueuedCreates)
			pThread->WakeUpCall();
		return;
	}

	ProcessDeferredPartFileCreateResults(bDrainAll, dwSliceStart, uProcessed, UINT_MAX);
	UpdateBulkAddDiskFinalizationProgress(false);

	if (!bDrainAll && uProcessed != 0 && theApp.IsTimeBudgetHardExceeded(dwSliceStart, CemuleApp::TimeBudgetDownloadAdd))
		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetDownloadAdd, _T("ProcessDeferredPartFileCreates"), ::GetTickCount() - dwSliceStart, uProcessed, 0);
}


UINT CDownloadQueue::CountPendingDeferredPartFileDiskWork()
{
	UINT uPending = 0;
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		const CPartFile* pFile = filelist.GetNext(pos);
		if (pFile != NULL) {
			if (pFile->HasPendingPartFileDiskCreate())
				++uPending;
			if (pFile->HasDeferredInitialPartMetSave())
				++uPending;
			if (pFile->m_bFlushPartMetInQueue)
				++uPending;
		}
	}
	return uPending;
}

UINT CDownloadQueue::CountPendingBulkAddDiskFinalizationFiles()
{
	UINT uPending = 0;
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		const CPartFile* pFile = filelist.GetNext(pos);
		if (pFile == NULL)
			continue;

		if (pFile->HasPendingPartFileDiskCreate() || pFile->HasDeferredInitialPartMetSave() || pFile->m_bFlushPartMetInQueue)
			++uPending;
	}
	return uPending;
}

void CDownloadQueue::StartBulkAddDiskFinalization(UINT uTotal)
{
	m_bBulkAddDiskFinalizationActive = uTotal >= BULK_OPERATION_MIN_ITEMS;
	m_uBulkAddDiskFinalizationTotal = m_bBulkAddDiskFinalizationActive ? uTotal : 0;
	m_dwLastBulkAddDiskFinalizationNotifyTick = 0;
	::InterlockedExchange(&m_lBulkAddDiskFinalizationProgressUpdatePending, m_bBulkAddDiskFinalizationActive ? 1 : 0);
	if (m_bBulkAddDiskFinalizationActive)
		theApp.SetActiveDownloadAddDiskProgress(0, m_uBulkAddDiskFinalizationTotal, true);
}

void CDownloadQueue::UpdateBulkAddDiskFinalizationProgress(bool bForceNotify)
{
	if (!m_bBulkAddDiskFinalizationActive)
		return;
	if (!theApp.IsBackendOwnerThread()) {
		RequestBulkAddDiskFinalizationProgressUpdate();
		return;
	}

	const DWORD dwNow = ::GetTickCount();
	if (!bForceNotify && m_dwLastBulkAddDiskFinalizationNotifyTick != 0 && static_cast<DWORD>(dwNow - m_dwLastBulkAddDiskFinalizationNotifyTick) < 1000) {
		::InterlockedExchange(&m_lBulkAddDiskFinalizationProgressUpdatePending, 0);
		return;
	}

	::InterlockedExchange(&m_lBulkAddDiskFinalizationProgressUpdatePending, 0);
	const UINT uTotal = m_uBulkAddDiskFinalizationTotal;
	const UINT uPending = CountPendingBulkAddDiskFinalizationFiles();
	if (uPending == 0) {
		m_bBulkAddDiskFinalizationActive = false;
		m_uBulkAddDiskFinalizationTotal = 0;
		m_dwLastBulkAddDiskFinalizationNotifyTick = 0;
		theApp.SetActiveDownloadAddDiskProgress(0, 0, false);
		bForceNotify = true;
	} else
		theApp.SetActiveDownloadAddDiskProgress(uPending >= uTotal ? 0 : uTotal - uPending, uTotal, true);

	m_dwLastBulkAddDiskFinalizationNotifyTick = dwNow;

	if (theApp.emuledlg != NULL && theApp.IsUiThread())
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
	else if (!theApp.IsClosing())
		theApp.QueueBulkOperationOverlayRefreshEvent(_T("bulk-add-disk-progress"));
}

void CDownloadQueue::RequestBulkAddDiskFinalizationProgressUpdate()
{
	if (!m_bBulkAddDiskFinalizationActive || theApp.IsClosing())
		return;
	::InterlockedExchange(&m_lBulkAddDiskFinalizationProgressUpdatePending, 1);
	theApp.QueueBackendContinuationProcessing();
}

bool CDownloadQueue::HasBulkAddDiskFinalizationProgressUpdate() const
{
	return ::InterlockedCompareExchange(const_cast<volatile LONG*>(&m_lBulkAddDiskFinalizationProgressUpdatePending), 0, 0) != 0;
}

void CDownloadQueue::ProcessBulkAddDiskFinalizationProgressUpdate()
{
	if (!HasBulkAddDiskFinalizationProgressUpdate())
		return;
	UpdateBulkAddDiskFinalizationProgress(false);
}

UINT CDownloadQueue::FlushQueuedPartFileCreatesSynchronously()
{
	CPartFileWriteThread* pThread = theApp.m_pPartFileWriteThread;
	if (pThread == NULL)
		return 0;

	UINT uFlushed = 0;
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile* pFile = filelist.GetNext(pos);
		if (pFile == NULL || !pFile->HasPendingPartFileDiskCreate())
			continue;

		PartFileCreateData* pData = NULL;
		if (!pThread->TakeQueuedPartFileCreateJob(pFile->GetRuntimeID(), pFile->GetFileHash(), pData))
			continue;

		PartFileCreateResult result;
		(void)CPartFileWriteThread::CreatePartFileDiskSnapshotNow(*pData, result);
		delete pData;

		if (pFile->ApplyPartFileDiskCreateResult(result)) {
			result.hFile = INVALID_HANDLE_VALUE;
			if (pFile->HasDeferredInitialPartMetSave())
				QueueDeferredInitialPartMetSave(pFile);
			++uFlushed;
		} else
			CleanupRejectedPartFileCreateResult(result);
	}
	return uFlushed;
}

UINT CDownloadQueue::FlushPendingPartFileCreatesSynchronously()
{
	UINT uFlushed = 0;
	CPartFileWriteThread* pThread = theApp.m_pPartFileWriteThread;
	DWORD dwLastPump = ::GetTickCount();
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile* pFile = filelist.GetNext(pos);
		if (pFile == NULL || !pFile->HasPendingPartFileDiskCreate())
			continue;

		PartFileCreateData stackData;
		PartFileCreateData* pData = NULL;
		bool bDeleteData = false;
		if (!pFile->HasUnqueuedPartFileDiskCreate()) {
			if (pThread != NULL && pThread->TakeQueuedPartFileCreateJob(pFile->GetRuntimeID(), pFile->GetFileHash(), pData))
				bDeleteData = true;
			else
				continue;
		}
		else {
			const CString& strPartFilePath = pFile->GetFilePath();
			if (strPartFilePath.IsEmpty())
				continue;
			stackData.uRuntimeID = pFile->GetRuntimeID();
			md4cpy(stackData.abyHash, pFile->GetFileHash());
			stackData.strPartFilePath = strPartFilePath;
			stackData.bSparsePartFile = thePrefs.GetSparsePartFiles();
			pData = &stackData;
		}

		PartFileCreateResult result;
		(void)CPartFileWriteThread::CreatePartFileDiskSnapshotNow(*pData, result);
		if (bDeleteData)
			delete pData;

		if (pFile->ApplyPartFileDiskCreateResult(result)) {
			result.hFile = INVALID_HANDLE_VALUE;
			if (pFile->HasDeferredInitialPartMetSave())
				QueueDeferredInitialPartMetSave(pFile);
		} else
			CleanupRejectedPartFileCreateResult(result);
		++uFlushed;

		const DWORD dwNow = ::GetTickCount();
		if (theApp.emuledlg != NULL && static_cast<DWORD>(dwNow - dwLastPump) >= 50) {
			theApp.emuledlg->PumpShutdownProgressDialog();
			dwLastPump = dwNow;
		}
	}

	bool bPendingCreate = false;
	for (POSITION posCheck = filelist.GetHeadPosition(); posCheck != NULL;) {
		const CPartFile* pFile = filelist.GetNext(posCheck);
		if (pFile != NULL && pFile->HasPendingPartFileDiskCreate()) {
			bPendingCreate = true;
			break;
		}
	}
	if (!bPendingCreate) {
		m_bDeferredPartFileCreateQueuePending = false;
		m_posDeferredPartFileCreateQueueFile = NULL;
	}
	return uFlushed;
}

void CDownloadQueue::DrainDeferredPartFileDiskWorkForShutdown()
{
	CPartFileWriteThread* pThread = theApp.m_pPartFileWriteThread;
	if (pThread == NULL) {
		FlushPendingPartFileCreatesSynchronously();
		ProcessDeferredInitialPartMetSaves(true);
		ProcessDeferredSourceSaves(true);
		return;
	}

	const DWORD dwDrainStart = ::GetTickCount();
	DWORD dwLastTrace = dwDrainStart;
	const DWORD dwDrainTimeout = SEC2MS(60);
	for (;;) {
		ProcessDeferredPartFileCreates(true);
		ProcessDeferredInitialPartMetSaves(true);
		ProcessDeferredSourceSaves(true);

		UINT uPendingFiles = CountPendingDeferredPartFileDiskWork();
		bool bPendingWorkerJobs = pThread->HasPendingPartFileDiskJobs();
		if (uPendingFiles == 0 && !bPendingWorkerJobs)
			break;
		if (!pThread->IsRunning()) {
			pThread->EndThread();
			ProcessDeferredPartFileCreates(true);
			ProcessDeferredInitialPartMetSaves(true);
			ProcessDeferredSourceSaves(true);

			uPendingFiles = CountPendingDeferredPartFileDiskWork();
			bPendingWorkerJobs = pThread->HasPendingPartFileDiskJobs();
			if (uPendingFiles == 0 && !bPendingWorkerJobs)
				break;

			const UINT uSyncCreated = FlushQueuedPartFileCreatesSynchronously();
			if (uSyncCreated != 0) {
				AddDebugLogLine(DLP_HIGH, false, _T("Shutdown synchronously created queued part files after writer stop. files=%u\n"), uSyncCreated);
				continue;
			}
			AddDebugLogLine(DLP_HIGH, false, _T("Shutdown cannot drain part-file disk jobs because the writer thread is not running. pendingFiles=%u workerJobs=%u\n"), uPendingFiles, bPendingWorkerJobs ? 1U : 0U);
			break;
		}

		const DWORD dwNow = ::GetTickCount();
		if (static_cast<DWORD>(dwNow - dwDrainStart) >= dwDrainTimeout) {
			const UINT uSyncCreated = FlushQueuedPartFileCreatesSynchronously();
			if (uSyncCreated != 0) {
				AddDebugLogLine(DLP_HIGH, false, _T("Shutdown synchronously created queued part files after drain timeout. files=%u\n"), uSyncCreated);
				continue;
			}

			AddDebugLogLine(DLP_HIGH, false, _T("Shutdown stopping part file writer after drain timeout to finish private disk jobs. pendingFiles=%u workerJobs=%u timeout=%lu\n"), uPendingFiles, bPendingWorkerJobs ? 1U : 0U, dwDrainTimeout);
			pThread->EndThread();
			ProcessDeferredPartFileCreates(true);
			ProcessDeferredInitialPartMetSaves(true);
			ProcessDeferredSourceSaves(true);

			uPendingFiles = CountPendingDeferredPartFileDiskWork();
			bPendingWorkerJobs = pThread->HasPendingPartFileDiskJobs();
			if (uPendingFiles == 0 && !bPendingWorkerJobs) {
				AddDebugLogLine(DLP_HIGH, false, _T("Shutdown drained part-file disk jobs after stopping writer thread.\n"));
				break;
			}

			const UINT uSyncCreatedAfterStop = FlushQueuedPartFileCreatesSynchronously();
			if (uSyncCreatedAfterStop != 0) {
				AddDebugLogLine(DLP_HIGH, false, _T("Shutdown synchronously created queued part files after writer shutdown drain. files=%u\n"), uSyncCreatedAfterStop);
				continue;
			}

			AddDebugLogLine(DLP_HIGH, false, _T("Shutdown timed out while draining part-file disk jobs. pendingFiles=%u workerJobs=%u timeout=%lu\n"), uPendingFiles, bPendingWorkerJobs ? 1U : 0U, dwDrainTimeout);
			if (m_bStartupLoadCompleted) {
				ExportPartMetFilesOverview();
				AddDebugLogLine(DLP_HIGH, false, _T("Shutdown wrote downloads.txt recovery overview after part-file disk drain timeout. pendingFiles=%u workerJobs=%u\n"), uPendingFiles, bPendingWorkerJobs ? 1U : 0U);
			} else
				AddDebugLogLine(DLP_HIGH, false, _T("Shutdown skipped downloads.txt recovery overview after part-file disk drain timeout because startup download load is incomplete. pendingFiles=%u workerJobs=%u loaded=%d\n"), uPendingFiles, bPendingWorkerJobs ? 1U : 0U, m_iStartupLoadCount);
			break;
		}

		if (static_cast<DWORD>(dwNow - dwLastTrace) >= 1000) {
			dwLastTrace = dwNow;
			AddDebugLogLine(DLP_VERYLOW, false, _T("Shutdown waiting for part-file disk jobs. pendingFiles=%u workerJobs=%u\n"), uPendingFiles, bPendingWorkerJobs ? 1U : 0U);
		}
		::Sleep(10);
	}

	ProcessDeferredPartFileCreates(true);
	ProcessDeferredInitialPartMetSaves(true);
	ProcessDeferredSourceSaves(true);
}

void CDownloadQueue::QueueDeferredDownloadValidatorAdd(CPartFile *pFile)
{
	if (pFile == NULL || theApp.DownloadValidator == NULL)
		return;
	if (m_deferredDownloadValidatorAddPositions.find(pFile) != m_deferredDownloadValidatorAddPositions.end())
		return;

	SDeferredDownloadValidatorAdd *pAdd = new SDeferredDownloadValidatorAdd();
	pAdd->pFile = pFile;
	memcpy(pAdd->abyFileHash, pFile->GetFileHash(), sizeof(pAdd->abyFileHash));
	pAdd->strFileName = pFile->GetFileName();
	pAdd->uFileSize = pFile->GetFileSize();
	m_deferredDownloadValidatorAdds.AddTail(pAdd);
	m_deferredDownloadValidatorAddPositions[pFile] = m_deferredDownloadValidatorAdds.GetTailPosition();
}

void CDownloadQueue::RemoveDeferredDownloadValidatorAdd(CPartFile *pFile)
{
	if (pFile == NULL)
		return;

	std::map<CPartFile*, POSITION>::iterator itPos = m_deferredDownloadValidatorAddPositions.find(pFile);
	if (itPos == m_deferredDownloadValidatorAddPositions.end())
		return;

	POSITION pos = itPos->second;
	m_deferredDownloadValidatorAddPositions.erase(itPos);
	if (pos != NULL) {
		SDeferredDownloadValidatorAdd *pAdd = m_deferredDownloadValidatorAdds.GetAt(pos);
		m_deferredDownloadValidatorAdds.RemoveAt(pos);
		delete pAdd;
	}
}

bool CDownloadQueue::ProcessDeferredDownloadValidatorAdds(bool bDrainAll)
{
	if (m_deferredDownloadValidatorAdds.IsEmpty())
		return false;

	static const UINT kMaxUiDeferredValidatorAddsPerSlice = 1;
	static const UINT kMaxBackendDeferredValidatorAddsPerSlice = 64;
	const DWORD dwSliceStart = ::GetTickCount();
	UINT uProcessed = 0;
	while (!m_deferredDownloadValidatorAdds.IsEmpty()) {
		SDeferredDownloadValidatorAdd *pAdd = m_deferredDownloadValidatorAdds.RemoveHead();
		if (pAdd != NULL) {
			m_deferredDownloadValidatorAddPositions.erase(pAdd->pFile);
			CPartFile *pQueuedFile = NULL;
			if (theApp.DownloadValidator != NULL && m_mapFilesByHash.Lookup(CCKey(pAdd->abyFileHash), pQueuedFile) && pQueuedFile == pAdd->pFile)
				theApp.DownloadValidator->AddToMap(pAdd->abyFileHash, pAdd->strFileName, pAdd->uFileSize);
			delete pAdd;
		}
		++uProcessed;
		if (!bDrainAll && theApp.IsUiThread() && uProcessed >= kMaxUiDeferredValidatorAddsPerSlice)
			break;
		if (!bDrainAll && !theApp.IsUiThread() && uProcessed >= kMaxBackendDeferredValidatorAddsPerSlice)
			break;
		if (!bDrainAll && uProcessed != 0 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetDownloadAdd))
			break;
	}

	if (!bDrainAll && theApp.IsTimeBudgetHardExceeded(dwSliceStart, CemuleApp::TimeBudgetDownloadAdd))
		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetDownloadAdd, _T("ProcessDeferredDownloadValidatorAdds"), ::GetTickCount() - dwSliceStart, uProcessed, m_deferredDownloadValidatorAdds.GetCount());
	if (!bDrainAll && !m_deferredDownloadValidatorAdds.IsEmpty())
		theApp.QueueBackendContinuationProcessing();
	return uProcessed != 0;
}

void CDownloadQueue::QueueDeferredInitialPartMetSave(CPartFile *pFile)
{
	if (pFile == NULL || !pFile->HasDeferredInitialPartMetSave())
		return;
	if (!m_deferredInitialPartMetSaveSet.insert(pFile).second)
		return;
	m_deferredInitialPartMetSaves.AddTail(pFile);
	m_deferredInitialPartMetSavePositions[pFile] = m_deferredInitialPartMetSaves.GetTailPosition();
}

void CDownloadQueue::RemoveDeferredInitialPartMetSave(CPartFile *pFile)
{
	if (pFile == NULL)
		return;

	std::map<CPartFile*, POSITION>::iterator itPos = m_deferredInitialPartMetSavePositions.find(pFile);
	if (itPos == m_deferredInitialPartMetSavePositions.end()) {
		m_deferredInitialPartMetSaveSet.erase(pFile);
		return;
	}

	POSITION pos = itPos->second;
	m_deferredInitialPartMetSavePositions.erase(itPos);
	m_deferredInitialPartMetSaveSet.erase(pFile);
	if (pos != NULL)
		m_deferredInitialPartMetSaves.RemoveAt(pos);
}

void CDownloadQueue::ProcessDeferredInitialPartMetSaves(bool bDrainAll)
{
	if (!bDrainAll && IsBulkAddingDownloads())
		return;
	if (m_deferredInitialPartMetSaves.IsEmpty()) {
		if (m_bBulkAddOverviewExportDeferred) {
			CPartFileWriteThread* pThread = theApp.m_pPartFileWriteThread;
			const bool bWorkerJobsPending = pThread != NULL && pThread->HasPendingPartFileDiskJobs();
			if (m_bDeferredPartFileCreateQueuePending || bWorkerJobsPending || CountPendingDeferredPartFileDiskWork() != 0)
				return;
			m_bBulkAddOverviewExportDeferred = false;
			ExportPartMetFilesOverview();
		}
		UpdateBulkAddDiskFinalizationProgress(true);
		return;
	}

	const DWORD dwSliceStart = ::GetTickCount();
	UINT uProcessed = 0;
	while (!m_deferredInitialPartMetSaves.IsEmpty()) {
		CPartFile *pFile = m_deferredInitialPartMetSaves.RemoveHead();
		m_deferredInitialPartMetSaveSet.erase(pFile);
		m_deferredInitialPartMetSavePositions.erase(pFile);
		if (pFile != NULL) {
			if (!pFile->FlushDeferredInitialPartMetSave() && !bDrainAll) {
				QueueDeferredInitialPartMetSave(pFile);
				++uProcessed;
				break;
			}
		}
		++uProcessed;
		if (!bDrainAll && uProcessed != 0 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetDownloadAdd))
			break;
	}

	UpdateBulkAddDiskFinalizationProgress(false);

	if (!bDrainAll && theApp.IsTimeBudgetHardExceeded(dwSliceStart, CemuleApp::TimeBudgetDownloadAdd))
		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetDownloadAdd, _T("ProcessDeferredInitialPartMetSaves"), ::GetTickCount() - dwSliceStart, uProcessed, m_deferredInitialPartMetSaves.GetCount());
}


bool CDownloadQueue::ShouldSaveSourcesForFile(const CPartFile *pFile, bool bIncludePaused) const
{
	if (pFile == NULL || pFile->IsStopped())
		return false;
	if (bIncludePaused)
		return true;
	const EPartFileStatus eStatus = pFile->GetStatus();
	return eStatus == PS_READY || eStatus == PS_EMPTY;
}

void CDownloadQueue::QueueDeferredSourceSaves(bool bForce)
{
	if (!thePrefs.GetSaveLoadSources() || theApp.IsClosing())
		return;

	m_deferredSourceSaves.clear();
	m_deferredSourceSaveSet.clear();
	m_bDeferredSourceSavesIncludePaused = bForce;
	m_deferredSourceSaves.reserve(static_cast<size_t>(filelist.GetCount()));
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *pFile = filelist.GetNext(pos);
		if (ShouldSaveSourcesForFile(pFile, m_bDeferredSourceSavesIncludePaused)) {
			SDownloadItemId id;
			if (GetDownloadItemId(pFile, id) && m_deferredSourceSaveSet.insert(id).second)
				m_deferredSourceSaves.push_back(id);
		}
	}

	if (!m_deferredSourceSaves.empty())
		ProcessDeferredSourceSaves(bForce);
}

void CDownloadQueue::RemoveDeferredSourceSave(CPartFile *pFile)
{
	if (pFile == NULL || m_deferredSourceSaves.empty())
		return;

	SDownloadItemId id;
	if (!GetDownloadItemId(pFile, id) || m_deferredSourceSaveSet.erase(id) == 0)
		return;

	for (std::vector<SDownloadItemId>::iterator it = m_deferredSourceSaves.begin(); it != m_deferredSourceSaves.end();) {
		if (it->Equals(id))
			it = m_deferredSourceSaves.erase(it);
		else
			++it;
	}
}

void CDownloadQueue::ProcessDeferredSourceSaves(bool bDrainAll)
{
	if (m_deferredSourceSaves.empty())
		return;

	const DWORD dwSliceStart = ::GetTickCount();
	UINT uProcessed = 0;
	while (!m_deferredSourceSaves.empty()) {
		const SDownloadItemId id = m_deferredSourceSaves.back();
		m_deferredSourceSaves.pop_back();
		m_deferredSourceSaveSet.erase(id);

		CPartFile *pFile = GetFileByItemId(id);
		if (ShouldSaveSourcesForFile(pFile, m_bDeferredSourceSavesIncludePaused))
			pFile->m_sourcesaver.SaveSources(pFile, true);

		++uProcessed;
		if (!bDrainAll && uProcessed != 0 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetPersistenceSave))
			break;
	}

	if (!bDrainAll && theApp.IsTimeBudgetHardExceeded(dwSliceStart, CemuleApp::TimeBudgetPersistenceSave))
		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetPersistenceSave, _T("ProcessDeferredSourceSaves"), ::GetTickCount() - dwSliceStart, uProcessed, static_cast<UINT>(m_deferredSourceSaves.size()));
}


void CDownloadQueue::BeginBulkAddDownloads(bool bSuppressPerItemListUpdates, bool bDeferDownloadValidatorAdds)
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::BeginBulkAddDownloads")))
		return;
	if (bSuppressPerItemListUpdates)
		m_bBulkAddSuppressPerItemListUpdates = true;
	if (bDeferDownloadValidatorAdds)
		m_bBulkAddDeferDownloadValidatorAdds = true;
	++m_uBulkAddDepth;
}

void CDownloadQueue::EndBulkAddDownloads()
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::EndBulkAddDownloads")))
		return;
	if (m_uBulkAddDepth == 0) {
		if (thePrefs.GetLogUiResponsivenessEvents())
			AddDebugLogLine(DLP_LOW, false, _T("Bulk download add depth underflow ignored.\n"));
		return;
	}
	--m_uBulkAddDepth;
	if (m_uBulkAddDepth == 0)
		FinalizeBulkAddDownloads();
}

void CDownloadQueue::FinalizeBulkAddDownloads()
{
	static const UINT LARGE_BULK_ADD_LIGHT_FINALIZE_THRESHOLD = 1000;
	const bool bSuppressPerItemListUpdates = m_bBulkAddSuppressPerItemListUpdates;
	const bool bDeferDownloadValidatorAdds = m_bBulkAddDeferDownloadValidatorAdds;
	m_bBulkAddSuppressPerItemListUpdates = false;
	m_bBulkAddDeferDownloadValidatorAdds = false;

	if (!m_bBulkAddPending) {
		if (HasDeferredDownloadValidatorAdds())
			theApp.QueueBackendContinuationProcessing();
		return;
	}

	const UINT uAddedFiles = m_uBulkAddedFiles;
	const bool bLightFinalize = uAddedFiles >= LARGE_BULK_ADD_LIGHT_FINALIZE_THRESHOLD;
	m_bBulkAddPending = false;
	m_uBulkAddedFiles = 0;

	if (!bLightFinalize) {
		SortByPriority();
		CheckDiskspace();
	}
	CDownloadListCtrl *pDownloadList = GetDownloadListForDownloadQueueUi();
	if (pDownloadList != NULL) {
		pDownloadList->MarkDeferredReload();
		pDownloadList->FlushBulkAddListUpdate(LSF_SELECTION);
	} else if (bSuppressPerItemListUpdates || bLightFinalize)
		theApp.QueueDownloadListChangedEvent(_T("bulk-add-finalized"));
	StartBulkAddDiskFinalization(uAddedFiles);
	m_bBulkAddOverviewExportDeferred = true;
	if (bDeferDownloadValidatorAdds && HasDeferredDownloadValidatorAdds())
		theApp.QueueBackendContinuationProcessing();
	ProcessDeferredPartFileCreates(false);
	ProcessDeferredInitialPartMetSaves(false);
	UpdateBulkAddDiskFinalizationProgress(true);

	if (!bLightFinalize || thePrefs.GetLogUiResponsivenessEvents())
		AddDebugLogLine(DLP_LOW, false, _T("Bulk download add finalized. Added files=%u light=%u\n"), uAddedFiles, bLightFinalize ? 1U : 0U);
}


CDownloadQueue::CBulkRemoveScope::CBulkRemoveScope(CDownloadQueue* pQueue)
	: m_pQueue(pQueue)
{
	if (m_pQueue != NULL)
		m_pQueue->BeginBulkRemoveDownloads();
}

CDownloadQueue::CBulkRemoveScope::~CBulkRemoveScope()
{
	if (m_pQueue != NULL)
		m_pQueue->EndBulkRemoveDownloads();
}

void CDownloadQueue::BeginBulkRemoveDownloads()
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::BeginBulkRemoveDownloads")))
		return;
	if (m_uBulkRemoveDepth == 0) {
		CSharedFilesCtrl *pSharedFilesCtrl = GetSharedFilesCtrlForDownloadQueueUi();
		if (pSharedFilesCtrl != NULL && ::IsWindow(pSharedFilesCtrl->GetSafeHwnd()))
			pSharedFilesCtrl->BeginDownloadRemoveBatch();
		m_bulkRemoveFilePositions.clear();
		for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
			POSITION posCurrent = pos;
			CPartFile *pFile = filelist.GetNext(pos);
			if (pFile != NULL)
				m_bulkRemoveFilePositions[pFile] = posCurrent;
		}
	}
	++m_uBulkRemoveDepth;
}

void CDownloadQueue::EndBulkRemoveDownloads()
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::EndBulkRemoveDownloads")))
		return;
	if (m_uBulkRemoveDepth == 0) {
		if (thePrefs.GetLogUiResponsivenessEvents())
			AddDebugLogLine(DLP_LOW, false, _T("Bulk download remove depth underflow ignored.\n"));
		return;
	}
	--m_uBulkRemoveDepth;
	if (m_uBulkRemoveDepth == 0) {
		FinalizeBulkRemoveDownloads();
		m_bulkRemoveFilePositions.clear();
		CSharedFilesCtrl *pSharedFilesCtrl = GetSharedFilesCtrlForDownloadQueueUi();
		if (pSharedFilesCtrl != NULL && ::IsWindow(pSharedFilesCtrl->GetSafeHwnd()))
			pSharedFilesCtrl->EndDownloadRemoveBatch();
	}
}

void CDownloadQueue::FinalizeBulkRemoveDownloads()
{
	if (!m_bBulkRemovePending)
		return;

	const UINT uRemovedFiles = m_uBulkRemovedFiles;
	m_bBulkRemovePending = false;
	m_uBulkRemovedFiles = 0;

	CheckDiskspace();
	if (filelist.GetCount() <= 1000)
		ExportPartMetFilesOverview();
	else
		AddDebugLogLine(DLP_LOW, false, _T("Deferred downloads.txt overview export skipped after large bulk remove. files=%d removed=%u\n"), static_cast<int>(filelist.GetCount()), uRemovedFiles);

	AddDebugLogLine(DLP_LOW, false, _T("Bulk download remove finalized. Removed files=%u\n"), uRemovedFiles);
}

void CDownloadQueue::TouchDownloadModelSequence()
{
	++m_uModelSequence;
}

void CDownloadQueue::RestartSourceDiscoveryAfterStop(CPartFile *pFile)
{
	if (pFile == NULL || pFile->IsStopped() || pFile->GetSourceCount() != 0)
		return;

	if (DoKademliaFileRequest())
		StartInitialKadSourceLookup(pFile);

	if (!pFile->m_bLocalSrcReqQueued
		&& theApp.serverconnect != NULL
		&& theApp.serverconnect->IsConnected()
		&& (!pFile->IsLargeFile() || (theApp.serverconnect->GetCurrentServer() != NULL && theApp.serverconnect->GetCurrentServer()->SupportsLargeFilesTCP())))
	{
		pFile->m_bLocalSrcReqQueued = true;
		SendLocalSrcRequest(pFile);
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] Immediate server source rediscovery queued after Stop/Resume for %s"), (LPCTSTR)EscPercent(pFile->GetFileName()));
	}
}

bool CDownloadQueue::HasPendingNatTraversalBuddyDemand()
{
	for (POSITION posFile = filelist.GetHeadPosition(); posFile != NULL;) {
		CPartFile* pFile = filelist.GetNext(posFile);
		if (pFile == NULL || pFile->IsStopped())
			continue;

		for (POSITION posSource = pFile->srclist.GetHeadPosition(); posSource != NULL;) {
			CUpDownClient* pSource = pFile->srclist.GetNext(posSource);
			if (pSource == NULL || !pSource->HasLowID() || !pSource->HasValidServingBuddyID())
				continue;

			const EDownloadState eState = pSource->GetDownloadState();
			if (pSource->HasPendingNatTRetry() || eState == DS_WAITCALLBACKKAD || eState == DS_LOWTOLOWIP)
				return true;
		}
	}
	return false;
}

bool CDownloadQueue::StartInitialKadSourceLookup(CPartFile *pFile)
{
	if (pFile == NULL || pFile->IsStopped() || pFile->GetKadFileSearchID() != 0)
		return false;

	const EPartFileStatus eStatus = pFile->GetStatus(true);
	if (eStatus != PS_READY && eStatus != PS_EMPTY)
		return false;

	if (!Kademlia::CKademlia::IsConnected() || !theApp.IsConnected() || Kademlia::CKademlia::GetTotalFile() >= KADEMLIATOTALFILE)
		return false;

	Kademlia::CSearch *pSearch = Kademlia::CSearchManager::PrepareLookup(Kademlia::CSearch::FILE, true, Kademlia::CUInt128(pFile->GetFileHash()));
	if (pSearch == NULL)
		return false;

	pSearch->SetGUIName((CStringW)pFile->GetFileName());
	pFile->SetKadFileSearchID(pSearch->GetSearchID());
	SetLastKademliaFileRequest();
	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NatTraversal] Initial Kad source lookup started for %s, searchID=%u"), (LPCTSTR)EscPercent(pFile->GetFileName()), pSearch->GetSearchID());
	return true;
}

void CDownloadQueue::AddSearchToDownload(CSearchFile *toadd, uint8 paused, int cat, bool bBypassDownloadValidator, bool bDeferSearchSources)
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::AddSearchToDownload")))
		return;

	if (!(uint64)toadd->GetFileSize() || IsFileExisting(toadd->GetFileHash()))
		return;

	if (toadd->GetFileSize() > OLD_MAX_EMULE_FILE_SIZE && !thePrefs.CanFSHandleLargeFiles(cat)) {
		LogError(LOG_STATUSBAR, GetResString(_T("ERR_FSCANTHANDLEFILE")));
		return;
	}

	if (!bBypassDownloadValidator && thePrefs.GetDownloadValidator()) {
		UINT result = theApp.DownloadValidator->CheckFile(toadd->GetFileHash(), toadd->GetFileName(), toadd->GetFileSize(), true);
		if (result) {
			if (theApp.searchlist != NULL) {
				if (thePrefs.GetBlacklistManual() && thePrefs.GetDownloadValidatorMarkAsBlacklisted() && !toadd->GetManualBlacklisted() && result == theApp.DownloadValidator->EDownloadValidatorResult::SimilarName)
					theApp.searchlist->DoSpamRating(toadd, false, theApp.searchlist->EActionType::MarkAsBlacklisted, true, 0); // Mark as blacklisted.
				else if (result == theApp.DownloadValidator->EDownloadValidatorResult::ManualBlacklisted || result == theApp.DownloadValidator->EDownloadValidatorResult::AutomaticBlacklisted)
					theApp.searchlist->DoSpamRating(toadd, false, theApp.searchlist->EActionType::Calculate, true, 0);
				else if (result == theApp.DownloadValidator->EDownloadValidatorResult::Known || result == theApp.DownloadValidator->EDownloadValidatorResult::Downloading || result == theApp.DownloadValidator->EDownloadValidatorResult::Cancelled)
					theApp.searchlist->SetSearchItemKnownType(toadd);
			}
			return;
		}
	}

	CPartFile *newfile = new CPartFile(toadd, cat, false);
	if (newfile->GetStatus() == PS_ERROR) {
		delete newfile;
		return;
	}

	if (paused == 2)
		paused = (uint8)thePrefs.AddNewFilesPaused();
	AddDownload(newfile, (paused == 1));

	if (!bDeferSearchSources && toadd->IsKademlia() && newfile->GetSourceCount() == 0)
		StartInitialKadSourceLookup(newfile);

	if (bDeferSearchSources)
		return;

	// If the search result is from OP_GLOBSEARCHRES there may also be a source
	if (toadd->GetClientID() && toadd->GetClientPort()) {
		CSafeMemFile sources(1 + 4 + 2);
		try {
			sources.WriteUInt8(1);
			sources.WriteUInt32(toadd->GetClientID());
			sources.WriteUInt16(toadd->GetClientPort());
			sources.SeekToBegin();
			newfile->AddSources(&sources, toadd->GetClientServerIP(), toadd->GetClientServerPort(), false);
		} catch (CFileException *ex) {
			ASSERT(0);
			ex->Delete();
		}
	}

	// Add more sources which were found via global UDP search
	const CSimpleArray<CSearchFile::SClient> &aClients = toadd->GetClients();
	for (int i = 0; i < aClients.GetSize(); ++i) {
		CSafeMemFile sources(1 + 4 + 2);
		try {
			sources.WriteUInt8(1);
			sources.WriteUInt32(aClients[i].m_nIP);
			sources.WriteUInt16(aClients[i].m_nPort);
			sources.SeekToBegin();
			newfile->AddSources(&sources, aClients[i].m_nServerIP, aClients[i].m_nServerPort, false);
		} catch (CFileException *ex) {
			ASSERT(0);
			ex->Delete();
			break;
		}
	}
}

void CDownloadQueue::AddSearchToDownload(const CString &link, uint8 paused, int cat)
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::AddSearchToDownloadLink")))
		return;

	CPartFile *newfile = new CPartFile(link, cat);
	if (newfile->GetStatus() == PS_ERROR) {
		delete newfile;
		return;
	}

	if (paused == 2)
		paused = (uint8)thePrefs.AddNewFilesPaused();
	AddDownload(newfile, (paused == 1));
}

CPartFile* CDownloadQueue::GetFileByRuntimeID(DWORD uRuntimeID) const
{
	if (uRuntimeID == 0)
		return NULL;
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *cur_file = filelist.GetNext(pos);
		if (cur_file != NULL && cur_file->GetRuntimeID() == uRuntimeID)
			return cur_file;
	}
	return NULL;
}


CPartFile* CDownloadQueue::GetFileByItemId(const SDownloadItemId &id) const
{
	if (!id.IsValid())
		return NULL;

	if (id.m_uRuntimeID != 0) {
		for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
			CPartFile *pFile = filelist.GetNext(pos);
			if (pFile != NULL && pFile->GetRuntimeID() == id.m_uRuntimeID && id.EqualsHash(pFile->GetFileHash()))
				return pFile;
		}
		return NULL;
	}

	return GetFileByID(id.m_abyFileHash);
}

bool CDownloadQueue::GetDownloadItemId(const CPartFile *pFile, SDownloadItemId &id) const
{
	id.SetFile(pFile);
	return id.IsValid();
}

void CDownloadQueue::StartNextFileIfPrefs(int cat)
{
	int i = thePrefs.StartNextFile();
	if (i)
		StartNextFile((i > 1 ? cat : -1), (i != 3));
}

void CDownloadQueue::StartNextFile(int cat, bool force)
{
	CPartFile *pfile = NULL;

	if (cat != -1) {
		// try to find in specified category
		for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
			CPartFile *cur_file = filelist.GetNext(pos);
			if (cur_file->GetStatus() == PS_PAUSED
				&& (cur_file->GetCategory() == (UINT)cat
					|| (!cat && !thePrefs.GetCategory(0)->filter && cur_file->GetCategory() > 0)
				   )
				&& CPartFile::RightFileHasHigherPrio(pfile, cur_file)
			   )
			{
				pfile = cur_file;
			}
		}
		if (pfile == NULL && !force)
			return;
	}

	if (cat == -1 || (pfile == NULL && force))
		for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
			CPartFile *cur_file = filelist.GetNext(pos);
			if (cur_file->GetStatus() == PS_PAUSED && CPartFile::RightFileHasHigherPrio(pfile, cur_file))
				// pick first found matching file, since they are sorted in prio order with most important file first.
				pfile = cur_file;
		}

	if (pfile)
		pfile->ResumeFile();
}

void CDownloadQueue::AddFileSnapshotToDownload(LPCTSTR pszFileName, uint64 uFileSize, const uchar *pFileHash, LPCTSTR pszAICHHash, int cat)
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::AddFileSnapshotToDownload")))
		return;

	if (pszFileName == NULL || pszFileName[0] == _T('\0') || pFileHash == NULL || isnulmd4(pFileHash) || uFileSize == 0 || IsFileExisting(pFileHash))
		return;

	if (uFileSize > OLD_MAX_EMULE_FILE_SIZE && !thePrefs.CanFSHandleLargeFiles(cat)) {
		LogError(LOG_STATUSBAR, GetResString(_T("ERR_FSCANTHANDLEFILE")));
		return;
	}

	if (thePrefs.GetDownloadValidator()) {
		UINT result = theApp.DownloadValidator->CheckFile(pFileHash, pszFileName, uFileSize, true);
		if (result) {
			if (thePrefs.GetBlacklistManual() && thePrefs.GetDownloadValidatorMarkAsBlacklisted() && (result == theApp.DownloadValidator->EDownloadValidatorResult::SimilarName)) {
				if (theApp.GuardModelMutation(CemuleApp::ModelMutationSearchList, _T("CDownloadQueue::AddFileSnapshotToDownload::MarkHashAsBlacklisted")))
					theApp.searchlist->MarkHashAsBlacklisted(CSKey(pFileHash));
			}
			return;
		}
	}

	CPartFile *newfile = new CPartFile(pszFileName, uFileSize, pFileHash, pszAICHHash, cat, false, true);
	if (newfile->GetStatus() == PS_ERROR) {
		delete newfile;
		return;
	}
	AddDownload(newfile, thePrefs.AddNewFilesPaused());
}

void CDownloadQueue::AddFileLinkToDownload(const CED2KFileLink &Link, int cat)
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::AddFileLinkToDownload")))
		return;

	if (thePrefs.GetDownloadValidator()) {
		UINT result = theApp.DownloadValidator->CheckFile(Link.GetHashKey(), Link.GetName(), Link.GetSize(), true);
		if (result) {
			if (thePrefs.GetBlacklistManual() && thePrefs.GetDownloadValidatorMarkAsBlacklisted() && (result == theApp.DownloadValidator->EDownloadValidatorResult::SimilarName)) {
				if (theApp.GuardModelMutation(CemuleApp::ModelMutationSearchList, _T("CDownloadQueue::AddFileLinkToDownload::MarkHashAsBlacklisted")))
					theApp.searchlist->MarkHashAsBlacklisted(CSKey(Link.GetHashKey()));
			}
			return;
		}
	}

	CPartFile *newfile = new CPartFile(Link, cat);
	if (newfile->GetStatus() == PS_ERROR) {
		delete newfile;
		newfile = NULL;
	} else
		AddDownload(newfile, thePrefs.AddNewFilesPaused());

	CPartFile *partfile = newfile;
	if (partfile == NULL)
		partfile = GetFileByID(Link.GetHashKey());
	if (partfile) {
		// match the file identifier and only if they are the same add possible sources
		CFileIdentifierSA tmpFileIdent(Link.GetHashKey(), Link.GetSize(), Link.GetAICHHash(), Link.HasValidAICHHash());
		CFileIdentifier &fileid = partfile->GetFileIdentifier();
		if (fileid.CompareRelaxed(tmpFileIdent)) {
			if (Link.HasValidSources())
				partfile->AddClientSources(Link.SourcesList, 1, false);
			if (!fileid.HasAICHHash() && tmpFileIdent.HasAICHHash()) {
				fileid.SetAICHHash(tmpFileIdent.GetAICHHash());
				partfile->GetAICHRecoveryHashSet()->SetMasterHash(tmpFileIdent.GetAICHHash(), AICH_VERIFIED);
				partfile->GetAICHRecoveryHashSet()->FreeHashSet();
			}
		} else
			DebugLogWarning(_T("FileIdentifier mismatch when adding ed2k link to existing download - AICH hash or size might differ, no sources added. File: %s")
							, (LPCTSTR)EscPercent(partfile->GetFileName()));
	}

	if (Link.HasHostnameSources())
		for (POSITION pos = Link.m_HostnameSourcesList.GetHeadPosition(); pos != NULL;) {
			const SUnresolvedHostname *pUnresHost = Link.m_HostnameSourcesList.GetNext(pos);
			m_srcwnd.AddToResolve(Link.GetHashKey(), pUnresHost->strHostname, pUnresHost->nPort);
		}
}

void CDownloadQueue::AddToResolved(CPartFile *pFile, SUnresolvedHostname *pUH)
{
	if (pFile && pUH)
		m_srcwnd.AddToResolve(pFile->GetFileHash(), pUH->strHostname, pUH->nPort);
}

void CDownloadQueue::AddDownload(CPartFile *newfile, bool paused)
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::AddDownload"))) {
		delete newfile;
		return;
	}

	if (newfile == NULL)
		return;

	RemoveDeletedCompletedDownloadRowsForNewDownload(this, newfile->GetFileHash());

	// Barry - Add in paused mode if required
	if (paused)
		newfile->PauseFile();

	SetAutoCat(newfile);// HoaX_69 / Slugfiller: AutoCat
	POSITION posAdded = filelist.AddTail(newfile);
	if (IsBulkRemovingDownloads())
		m_bulkRemoveFilePositions[newfile] = posAdded;
	if (newfile->HasUnqueuedPartFileDiskCreate())
		m_bDeferredPartFileCreateQueuePending = true;
	IndexDownloadFile(newfile);
	TouchDownloadModelSequence();
	if (IsBulkAddingDownloads() && m_bBulkAddDeferDownloadValidatorAdds)
		QueueDeferredDownloadValidatorAdd(newfile);
	else if (theApp.DownloadValidator != NULL)
		theApp.DownloadValidator->AddToMap(newfile->GetFileHash(), newfile->GetFileName(), newfile->GetFileSize());
	QueueDeferredInitialPartMetSave(newfile);
	if (theApp.searchlist != NULL)
		theApp.searchlist->QueueKnownTypeRefreshForHash(newfile->GetFileHash());

	CDownloadListCtrl *pDownloadList = GetDownloadListForDownloadQueueUi();
	if (IsBulkAddingDownloads()) {
		m_bBulkAddPending = true;
		++m_uBulkAddedFiles;
		if (pDownloadList != NULL)
			pDownloadList->MarkDeferredReload();
		if (theApp.IsUiThread())
			AddLogLine(true, GetResString(_T("NEWDOWNLOAD")), (LPCTSTR)EscPercent(newfile->GetFileName()));
		else
			theApp.QueueLogLine(true, GetResString(_T("NEWDOWNLOAD")), (LPCTSTR)EscPercent(newfile->GetFileName()));
		return;
	}

	SortByPriority();
	CheckDiskspace();
	if (pDownloadList != NULL)
		pDownloadList->AddFile(newfile, false);
	else
		theApp.QueueDownloadListChangedEvent(_T("download-add"));
	if (theApp.IsUiThread()) {
		AddLogLine(true, GetResString(_T("NEWDOWNLOAD")), (LPCTSTR)EscPercent(newfile->GetFileName()));
		if (theApp.emuledlg != NULL) {
			CString msgTemp;
			msgTemp.Format(GetResString(_T("NEWDOWNLOAD")), (LPCTSTR)newfile->GetFileName());
			msgTemp += _T('\n');
			theApp.emuledlg->ShowNotifier(msgTemp, TBN_DOWNLOADADDED);
		}
	} else
		AddDebugLogLine(DLP_LOW, false, _T("Download added on backend owner lane. file=%s\n"), (LPCTSTR)EscPercent(newfile->GetFileName()));
	ExportPartMetFilesOverview();
}


bool CDownloadQueue::IsFileExisting(const uchar* fileid, bool bLogWarnings) const
{
	const CKnownFile *file = theApp.sharedfiles->GetLiveFileByID(fileid);
	if (file) {
		if (bLogWarnings) {
			if (file->IsPartFile())
				LogWarning(LOG_STATUSBAR, GetResString(_T("ERR_ALREADY_DOWNLOADING")), (LPCTSTR)file->GetFileName());
			else
				LogWarning(LOG_STATUSBAR, GetResString(_T("ERR_ALREADY_DOWNLOADED")), (LPCTSTR)file->GetFileName());
		}
		return true;
	}
	file = GetFileByID(fileid);
	if (!file)
		return false;
	if (bLogWarnings)
		LogWarning(LOG_STATUSBAR, GetResString(_T("ERR_ALREADY_DOWNLOADING")), (LPCTSTR)file->GetFileName());
	return true;
}

void CDownloadQueue::Process()
{
	if (IsBulkRemovingDownloads())
		return;

	ProcessLocalRequests(); // send src requests to local server

	uint32 downspeed;
	uint64 maxDownload = thePrefs.GetMaxDownloadInBytesPerSec(true);
	if (maxDownload != UNLIMITED * 1024ull && m_datarate > 1500) {
		downspeed = (uint32)(maxDownload * 100 / (m_datarate + 1));
		if (downspeed < 50)
			downspeed = 50;
		else if (downspeed > 200)
			downspeed = 200;
	} else
		downspeed = 0;

	DWORD curTick = ::GetTickCount() - SEC2MS(10);
	while (!average_dr_list.IsEmpty() && curTick >= average_dr_list.GetHead().timestamp)
		m_datarateMS -= average_dr_list.RemoveHead().datalen;

	if (average_dr_list.GetCount() > 1)
		m_datarate = (uint32)(m_datarateMS / average_dr_list.GetCount());
	else
		m_datarate = 0;

	uint32 datarateX = 0;
	++m_udcounter;

	theStats.m_fGlobalDone = 0;
	theStats.m_fGlobalSize = 0;
	theStats.m_dwOverallStatus = 0;
	RefreshAdaptiveDownloadBufferSnapshot();
	//file list is already sorted by prio, therefore I removed all the extra loops.
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *cur_file = filelist.GetNext(pos);

		// maintain global download stats
		theStats.m_fGlobalDone += (uint64)cur_file->GetCompletedSize();
		theStats.m_fGlobalSize += (uint64)cur_file->GetFileSize();

		if (cur_file->GetTransferringSrcCount() > 0)
			theStats.m_dwOverallStatus |= STATE_DOWNLOADING;
		if (cur_file->GetStatus() == PS_ERROR)
			theStats.m_dwOverallStatus |= STATE_ERROROUS;

		if (cur_file->GetStatus() == PS_READY || cur_file->GetStatus() == PS_EMPTY)
		{
			cur_file->ProcessSourceCache();
			datarateX += cur_file->Process(downspeed, m_udcounter);
		}
		else
			//This will ensure we don't keep old sources for paused and stopped files.
			cur_file->StopPausedFile();
	}

	curTick = ::GetTickCount();
	average_dr_list.AddTail(TransferredData{ datarateX, curTick });
	m_datarateMS += datarateX;

	if (m_udcounter == 5) {
		if (theApp.serverconnect->IsUDPSocketAvailable()
			&& (!m_lastudpstattime || curTick >= m_lastudpstattime + UDPSERVERSTATTIME))
		{
			m_lastudpstattime = curTick;
			theApp.serverlist->ServerStats();
		}
	} else if (m_udcounter >= 10) {
		m_udcounter = 0;
		if (theApp.serverconnect->IsUDPSocketAvailable())
			if (!m_lastudpsearchtime || curTick >= m_lastudpsearchtime + UDPSERVERREASKTIME)
				SendNextUDPPacket();
	}

	CheckDiskspaceTimed();
	ProcessDeferredDownloadValidatorAdds(false);
	ProcessDeferredPartFileCreates(false);
	ProcessDeferredInitialPartMetSaves(false);
	ProcessDeferredSourceSaves(false);

	if (!m_dwLastA4AFtime || curTick >= m_dwLastA4AFtime + MIN2MS(8)) {
		theApp.clientlist->ProcessA4AFClients();
		m_dwLastA4AFtime = curTick;
	}
}

CPartFile* CDownloadQueue::GetFileNext(POSITION& pos) const
{
	if (!pos)
		pos = filelist.GetHeadPosition();
	return pos ? filelist.GetNext(pos) : NULL;
}

void CDownloadQueue::RefreshAdaptiveDownloadBufferSnapshot()
{
	if (!thePrefs.IsAutoHighBandwidthDownloadBufferEnabled()) {
		m_uBufferedDownloadBytesSnapshot = 0;
		m_uLargestBufferedDownloadFileBytesSnapshot = 0;
		m_uAdaptiveGlobalDownloadBufferBudgetBytesSnapshot = 0;
		m_uBufferedDownloadFileCountSnapshot = 0;
		return;
	}

	uint64 uTotalBufferedBytes = 0;
	uint64 uLargestBufferedFileBytes = 0;
	UINT uBufferedFileCount = 0;
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		const CPartFile *pFile = filelist.GetNext(pos);
		if (pFile == NULL)
			continue;
		const uint64 uFileBufferedBytes = pFile->GetBufferedDataBytes();
		if (uFileBufferedBytes == 0)
			continue;
		++uBufferedFileCount;
		uTotalBufferedBytes = AddSaturated(uTotalBufferedBytes, uFileBufferedBytes);
		uLargestBufferedFileBytes = max(uLargestBufferedFileBytes, uFileBufferedBytes);
	}

	m_uBufferedDownloadBytesSnapshot = uTotalBufferedBytes;
	m_uLargestBufferedDownloadFileBytesSnapshot = uLargestBufferedFileBytes;
	m_uBufferedDownloadFileCountSnapshot = uBufferedFileCount;

	MEMORYSTATUSEX memoryStatus = {0};
	memoryStatus.dwLength = sizeof(memoryStatus);
	m_uAdaptiveGlobalDownloadBufferBudgetBytesSnapshot = ::GlobalMemoryStatusEx(&memoryStatus) ? BuildAdaptiveDownloadBufferBudgetBytes(memoryStatus.ullAvailPhys) : kAdaptiveDownloadBufferMinGlobalBudget;
}

uint64 CDownloadQueue::GetAdaptiveGlobalDownloadBufferBudgetBytes() const
{
	if (!thePrefs.IsAutoHighBandwidthDownloadBufferEnabled())
		return 0;
	if (m_uAdaptiveGlobalDownloadBufferBudgetBytesSnapshot != 0)
		return m_uAdaptiveGlobalDownloadBufferBudgetBytesSnapshot;

	MEMORYSTATUSEX memoryStatus = {0};
	memoryStatus.dwLength = sizeof(memoryStatus);
	if (!::GlobalMemoryStatusEx(&memoryStatus))
		return kAdaptiveDownloadBufferMinGlobalBudget;
	return BuildAdaptiveDownloadBufferBudgetBytes(memoryStatus.ullAvailPhys);
}

uint64 CDownloadQueue::GetEffectiveFileBufferSizeBytes(uint64 uCurrentFileBufferedBytes) const
{
	const uint64 uBaseBufferSize = thePrefs.GetFileBufferSize();
	if (!thePrefs.IsAutoHighBandwidthDownloadBufferEnabled())
		return uBaseBufferSize;
	return BuildEffectiveDownloadFileBufferSizeBytes(uBaseBufferSize, uCurrentFileBufferedBytes, m_uBufferedDownloadFileCountSnapshot, GetAdaptiveGlobalDownloadBufferBudgetBytes());
}

bool CDownloadQueue::ShouldFlushFileForAdaptiveBufferBudget(uint64 uCurrentFileBufferedBytes) const
{
	if (!thePrefs.IsAutoHighBandwidthDownloadBufferEnabled())
		return false;
	return ShouldFlushForAdaptiveDownloadBufferBudget(uCurrentFileBufferedBytes, m_uBufferedDownloadBytesSnapshot, m_uLargestBufferedDownloadFileBytesSnapshot, GetAdaptiveGlobalDownloadBufferBudgetBytes());
}

CPartFile* CDownloadQueue::GetFileByID(const uchar* filehash) const
{
	if (filehash == NULL)
		return NULL;

	CPartFile *pFile = NULL;
	if (const_cast<CDownloadFilesByHashMap&>(m_mapFilesByHash).Lookup(CCKey(filehash), pFile)) {
		if (pFile != NULL && md4equ(filehash, pFile->GetFileHash()))
			return pFile;
	}

	return NULL;
}

void CDownloadQueue::CollectCompletedFileHashes(CStringArray &astrFileHashes, int iCategory) const
{
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *pFile = filelist.GetNext(pos);
		if (pFile == NULL || pFile->IsPartFile())
			continue;
		if (iCategory > 0 && pFile->GetCategory() != iCategory)
			continue;
		astrFileHashes.Add(md4str(pFile->GetFileHash()));
	}
}

CPartFile* CDownloadQueue::GetFileByKadFileSearchID(uint32 id) const
{
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *cur_file = filelist.GetNext(pos);
		if (id == cur_file->GetKadFileSearchID())
			return cur_file;
	}
	return NULL;
}

bool CDownloadQueue::IsPartFile(const CKnownFile* file) const
{
	if (file == NULL)
		return false;

	CPartFile *pFile = GetFileByID(file->GetFileHash());
	if (pFile != NULL)
		return pFile == file;

	return filelist.Find((void*)file) != NULL;
}


bool CDownloadQueue::CheckAndAddSource(CPartFile *sender, CUpDownClient *source, ESourceFrom eSourceFrom, bool bSourceFromAuthoritative, CUpDownClient** ppResolvedSource)
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::CheckAndAddSource"))) {
		CUpDownClient::SafeDelete(source);
		return false;
	}

	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NatTraversal] CheckAndAddSource: sender=%s source=%s\n"), sender ? (LPCTSTR)EscPercent(sender->GetFileName()) : (LPCTSTR)_T("NULL"), source ? (LPCTSTR)EscPercent(source->DbgGetClientInfo()) : (LPCTSTR)_T("NULL"));
	ASSERT(eSourceFrom >= SF_SERVER && eSourceFrom <= SF_SLS);
	if (ppResolvedSource != NULL)
		*ppResolvedSource = NULL;
	const bool bIsAuthoritativeSourceType = (eSourceFrom == SF_SERVER || eSourceFrom == SF_KADEMLIA || eSourceFrom == SF_SOURCE_EXCHANGE);
	const bool bApplySourceFrom = bSourceFromAuthoritative && bIsAuthoritativeSourceType;

	if (sender->IsStopped()) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] CheckAndAddSource: REJECTED - sender is stopped\n"));
		CUpDownClient::SafeDelete(source);
		return false;
	}

	if (source->HasValidHash() && md4equ(source->GetUserHash(), thePrefs.GetUserHash())) {
		if (thePrefs.GetVerbose())
			AddDebugLogLine(false, _T("Tried to add source with a hash matching your own."));
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] CheckAndAddSource: REJECTED - source hash matches own hash\n"));
		CUpDownClient::SafeDelete(source);
		return false;
	}

	// filter sources which are incompatible with our encryption setting (one requires it, and the other one doesn't support it)
	if ((source->RequiresCryptLayer() && (!thePrefs.IsCryptLayerEnabled() || !source->HasValidHash())) || (thePrefs.IsCryptLayerRequired() && (!source->SupportsCryptLayer() || !source->HasValidHash()))) {
#if defined(_DEBUG) || defined(_BETA) || defined(_DEVBUILD)
		AddDebugLogLine(DLP_DEFAULT, false, _T("Rejected source because CryptLayer-Setting (Obfuscation) was incompatible for file %s : %s"), (LPCTSTR)EscPercent(sender->GetFileName()), (LPCTSTR)EscPercent(source->DbgGetClientInfo()));
#endif
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] CheckAndAddSource: REJECTED - encryption incompatible\n"));
		CUpDownClient::SafeDelete(source);
		return false;
	}

	// "Filter LAN IPs" and/or "IPfilter" is not required here, because it was already done in parent functions

	// uses this only for temp. clients
	CPartFile *pDuplicateFile = NULL;
	CUpDownClient *pDuplicateClient = FindDownloadDuplicateSource(source, pDuplicateFile);
	const bool bNeedsConnectIPScanFallback = pDuplicateClient == NULL
		&& !source->GetConnectIP().IsNull()
		&& source->GetConnectIP() != source->GetIP()
		&& source->GetConnectIP() != source->GetIPv4()
		&& source->GetConnectIP() != source->GetIPv6();
	if (pDuplicateClient == NULL && (m_mapDownloadSourceEntries.empty() || bNeedsConnectIPScanFallback))
		pDuplicateClient = FindDownloadDuplicateSourceByScan(source, pDuplicateFile);
	if (pDuplicateClient != NULL && pDuplicateFile != NULL) {
		const bool bRoutingUpdated = RefreshKnownSourceRouting(pDuplicateClient, source, eSourceFrom, bApplySourceFrom);
		if (bRoutingUpdated)
			RefreshDownloadSource(pDuplicateClient);
		if (bRoutingUpdated && thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] CheckAndAddSource: refreshed duplicate source routing info for %s"), (LPCTSTR)EscPercent(pDuplicateClient->DbgGetClientInfo()));
		if (ppResolvedSource != NULL)
			*ppResolvedSource = pDuplicateClient;
		if (bRoutingUpdated && pDuplicateFile == sender) {
			if (!pDuplicateClient->IsEServerRelayNatTGuardActive()) {
				pDuplicateClient->TrigNextSafeAskForDownload(sender);
				if (pDuplicateClient->HasPendingNatTRetry())
					pDuplicateClient->MarkNatTRendezvous(2, true);
			}
			if (pDuplicateClient->GetDownloadState() == DS_NONE || pDuplicateClient->GetDownloadState() == DS_LOWTOLOWIP)
				pDuplicateClient->SetDownloadState(DS_ONQUEUE, _T("Fresh source routing info received"));
		}
		if (!AddAlreadyKnownSourceAsA4AF(sender, pDuplicateClient, pDuplicateFile, _T("New A4AF source found. CDownloadQueue::CheckAndAddSource()"))) {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] CheckAndAddSource: REJECTED - source already exists in another file's srclist\n"));
		}
		CUpDownClient::SafeDelete(source);
		return false;
	}

	//our new source is really new, but maybe it is already uploading to us?
	//if yes the known client will be attached to the var "source"
	//and the old source client will be deleted
	const bool bAuthoritativeEServerRoute = bApplySourceFrom && eSourceFrom == SF_SERVER && source->HasLowID() && source->GetUserIDHybrid() != 0 && source->GetServerIP() != 0 && source->GetServerPort() != 0;
	const uint32 uAuthoritativeEServerLowID = bAuthoritativeEServerRoute ? source->GetUserIDHybrid() : 0;
	const uint32 uAuthoritativeEServerIP = bAuthoritativeEServerRoute ? source->GetServerIP() : 0;
	const uint16 nAuthoritativeEServerPort = bAuthoritativeEServerRoute ? source->GetServerPort() : 0;
	const CPartFile *pPreviousReqFile = source->GetRequestFile();
	bool bAttachedToKnownClient = false;
	if (source->HasLowID() && source->GetServerIP() != 0 && source->GetUserIDHybrid() != 0) {
		CUpDownClient* pServedBuddy = theApp.clientlist->FindServedEServerBuddyByLowID(source->GetServerIP(), source->GetServerPort(), source->GetUserIDHybrid());
		if (pServedBuddy != NULL && pServedBuddy != source) {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[eServerBuddy] CheckAndAddSource: reusing served eServer buddy as download source instead of transient LowID server source: %s"), (LPCTSTR)EscPercent(pServedBuddy->DbgGetClientInfo()));
			CUpDownClient::SafeDelete(source);
			source = pServedBuddy;
			pPreviousReqFile = source->GetRequestFile();
			bAttachedToKnownClient = true;
		}
	}
	if (!bAttachedToKnownClient && theApp.clientlist->AttachToAlreadyKnown(&source, NULL)) {
		bAttachedToKnownClient = true;
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] CheckAndAddSource: Source attached to already known client\n"));
		pPreviousReqFile = source->GetRequestFile();
#ifdef _DEBUG
		const CPartFile *srcfile = source->GetRequestFile();
		if (thePrefs.GetVerbose() && srcfile) {
			// if a client sent us wrong sources (sources for some other file for which we asked but which we are also
			// downloading) we may get a little in trouble here when "moving" this source to some other partfile without
			// further checks and updates.
			if (!md4equ(srcfile->GetFileHash(), sender->GetFileHash()))
				AddDebugLogLine(false, _T("*** CDownloadQueue::CheckAndAddSource -- added potentially wrong source (%u)(diff. filehash) to file \"%s\""), source->GetUserIDHybrid(), (LPCTSTR)EscPercent(sender->GetFileName()));
			if (srcfile->GetPartCount() > 0 && srcfile->GetPartCount() != sender->GetPartCount())
				AddDebugLogLine(false, _T("*** CDownloadQueue::CheckAndAddSource -- added potentially wrong source (%u)(diff. partcount) to file \"%s\""), source->GetUserIDHybrid(), (LPCTSTR)EscPercent(sender->GetFileName()));
		}
#endif
	}

	if (bAttachedToKnownClient) {
		CPartFile *pAttachedOwnerFile = GetDownloadSourceFile(source);
		if (pAttachedOwnerFile == NULL)
			pAttachedOwnerFile = FindDownloadSourceOwnerByScan(source);

		if (bAuthoritativeEServerRoute) {
			const bool bHasStoredKadRoute = source->HasValidServingBuddyID() || !source->GetServingBuddyIP().IsNull() || source->GetServingBuddyPort() != 0
				|| (!source->GetConnectIP().IsNull() && (source->GetKadPort() != 0 || source->GetUDPPort() != 0));
			const bool bDetachedKadRoute = bHasStoredKadRoute && pAttachedOwnerFile == NULL && source->GetRequestFile() == NULL
				&& source->GetDownloadState() == DS_NONE && source->GetUploadState() == US_NONE && (source->socket == NULL || !source->socket->IsConnected());
			if (bDetachedKadRoute) {
				source->AbortNatTRendezvousAttempt(_T("Detached Kad route replaced by authoritative eServer source."));
				source->SetServingBuddyID(NULL);
				source->SetServingBuddyIP(CAddress());
				source->SetServingBuddyPort(0);
				source->SetConnectIP(CAddress());
				source->SetKadPort(0);
				source->SetUDPPort(0);
				source->InvalidateDirectNatTraversalCaps();
				source->ClearNatTraversalQuicFailure();
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[eServerBuddy] CheckAndAddSource: cleared detached Kad route before reusing authoritative eServer source: %s"), (LPCTSTR)EscPercent(source->DbgGetClientInfo()));
			}

			source->SetUserIDHybrid(uAuthoritativeEServerLowID);
			source->SetServerIP(uAuthoritativeEServerIP);
			source->SetServerPort(nAuthoritativeEServerPort);
		}

		if (pAttachedOwnerFile != NULL) {
			if (bApplySourceFrom)
				source->SetSourceFrom(eSourceFrom);
			if (ppResolvedSource != NULL)
				*ppResolvedSource = source;
			if (pAttachedOwnerFile == sender) {
				if (!source->IsEServerRelayNatTGuardActive()) {
					source->TrigNextSafeAskForDownload(sender);
					if (source->HasPendingNatTRetry())
						source->MarkNatTRendezvous(2, true);
				}
				if (source->GetDownloadState() == DS_NONE || source->GetDownloadState() == DS_LOWTOLOWIP)
					source->SetDownloadState(DS_ONQUEUE, _T("Fresh source routing info received"));
			} else if (!AddAlreadyKnownSourceAsA4AF(sender, source, pAttachedOwnerFile, _T("New A4AF source found. CDownloadQueue::CheckAndAddSource()"))) {
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal] CheckAndAddSource: REJECTED - attached source already belongs to another file\n"));
			}
			return false;
		}
	}

	// Filter sources which are known to be temporarily dead/useless.
	// NAT-T rendezvous and eServer buddy relay candidates are ephemeral and may become
	// reachable again on a fresh attempt. Also, a stale global dead-source entry must
	// not overrule a client that is already connected and alive in our client list.
	const bool bNatTraversalSource =
		(source->GetServerIP() == 0 && source->GetServerPort() == 0
			&& source->GetKadPort() != 0 && source->GetConnectIP().IsPublicIP());
	const bool bEServerBuddyRelayCandidate = IsEServerBuddyRelayCandidate(source);
	const bool bSourceInGlobalDeadList = theApp.clientlist->m_globDeadSourceList.IsDeadSource(*source);
	const bool bSourceInFileDeadList = sender->m_DeadSourceList.IsDeadSource(*source);
	const bool bKnownLiveSource =
		bAttachedToKnownClient && source != NULL && source->socket != NULL && source->socket->IsConnected();

	if (bSourceInFileDeadList || (bSourceInGlobalDeadList && !bNatTraversalSource && !bEServerBuddyRelayCandidate && !bKnownLiveSource)) {
		if (thePrefs.GetLogNatTraversalEvents()) {
			if (bSourceInFileDeadList)
				DebugLog(_T("[NatTraversal] CheckAndAddSource: REJECTED - source is in file dead source list\n"));
			else
				DebugLog(_T("[NatTraversal] CheckAndAddSource: REJECTED - source is in global dead source list\n"));
		}
		if (!bAttachedToKnownClient)
			CUpDownClient::SafeDelete(source);
		return false;
	}

	if (bSourceInGlobalDeadList) {
		if (bKnownLiveSource) {
			theApp.clientlist->m_globDeadSourceList.RemoveDeadSource(*source);
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] CheckAndAddSource: removed stale global dead-source entry for live known client: %s"), (LPCTSTR)EscPercent(source->DbgGetClientInfo()));
		} else if (bNatTraversalSource) {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] CheckAndAddSource: NAT-T source bypassing global dead list: %s"), (LPCTSTR)EscPercent(source->DbgGetClientInfo()));
		} else if (bEServerBuddyRelayCandidate) {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] CheckAndAddSource: eServer buddy relay candidate bypassing global dead list: %s"), (LPCTSTR)EscPercent(source->DbgGetClientInfo()));
		}
	}

	// Keep source type in sync only when caller says discovery path is authoritative.
	if (bApplySourceFrom)
		source->SetSourceFrom(eSourceFrom);

	if (bAttachedToKnownClient) {
		source->SetRequestFile(sender);
		if (ppResolvedSource != NULL)
			*ppResolvedSource = source;
	} else {
		// here we know that the client instance 'source' is a new created client instance (see callers)
		// which is therefore not already in the client list, we can avoid the check for duplicate
		// client list entries when adding this client
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] CheckAndAddSource: Adding new source to clientlist\n"));
		theApp.clientlist->AddClient(source, true);
		if (ppResolvedSource != NULL)
			*ppResolvedSource = source;
	}

#ifdef _DEBUG
	if (thePrefs.GetVerbose() && source->GetPartCount() > 0 && source->GetPartCount() != sender->GetPartCount())
		DEBUG_ONLY(AddDebugLogLine(false, _T("*** CDownloadQueue::CheckAndAddSource -- New added source (%u, %s) had still value in partcount"), source->GetUserIDHybrid(), (LPCTSTR)EscPercent(sender->GetFileName())));
#endif

	sender->srclist.AddTail(source);
	RegisterDownloadSource(sender, source);
	PrepareKnownSourceForFreshAsk(source, sender, pPreviousReqFile);
	CDownloadListCtrl *pDownloadList = GetDownloadListForDownloadQueueUi();
	if (pDownloadList != NULL)
		pDownloadList->AddSource(sender, source, false);
	else
		theApp.QueueDownloadListChangedEvent(_T("source-add"), CemuleApp::BackendCommandSourceNetworkClient);
	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NatTraversal] CheckAndAddSource: SUCCESS - Source added to sender srclist and download list update requested\n"));

	// DEBUG: Minimal log - source count only
	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[SourceAdded] total=%u"), sender->GetSourceCount());

	return true;
}

bool CDownloadQueue::CheckAndAddKnownSource(CPartFile *sender, CUpDownClient *source, bool bIgnoreGlobDeadList)
{
	if (sender->IsStopped())
		return false;

	// filter sources which are known to be temporarily dead/useless
	if ((theApp.clientlist->m_globDeadSourceList.IsDeadSource(*source) && !bIgnoreGlobDeadList) || sender->m_DeadSourceList.IsDeadSource(*source)) {
		return false;
	}

	// filter sources which are incompatible with our encryption setting (one requires it, and the other one doesn't support it)
	if ((source->RequiresCryptLayer() && (!thePrefs.IsCryptLayerEnabled() || !source->HasValidHash())) || (thePrefs.IsCryptLayerRequired() && (!source->SupportsCryptLayer() || !source->HasValidHash()))) {
#if defined(_DEBUG) || defined(_BETA) || defined(_DEVBUILD)
		AddDebugLogLine(DLP_DEFAULT, false, _T("Rejected source because CryptLayer-Setting (Obfuscation) was incompatible for file %s : %s"), (LPCTSTR)EscPercent(sender->GetFileName()), (LPCTSTR)EscPercent(source->DbgGetClientInfo()));
#endif
		return false;
	}

	// "Filter LAN IPs" -- this may be needed here in case we are connected to the internet and are also connected
	// to a LAN and some client from within the LAN connected to us. Though this situation may be supported in future
	// by adding that client to the source list and filtering that client's LAN IP when sending sources to
	// a client within the internet.
	//
	// IP filter is not needed here, because that "known" client was already filtered when receiving OP_HELLO.
	if (!source->HasLowID()) {
		uint32 nClientIP = htonl(source->GetUserIDHybrid());
		if (!IsGoodIP(nClientIP)) { // check for 0-IP, localhost and LAN addresses
			return false;
		}
	}

	// use this for client which are already known (downloading for example)
	CPartFile *pCurrentSourceFile = GetDownloadSourceFile(source);
	if (pCurrentSourceFile == NULL)
		pCurrentSourceFile = FindDownloadSourceOwnerByScan(source);
	if (pCurrentSourceFile != NULL) {
		if (pCurrentSourceFile == sender)
			return false;
		if (source->AddRequestForAnotherFile(sender)) {
			CDownloadListCtrl *pDownloadList = GetDownloadListForDownloadQueueUi();
			if (pDownloadList != NULL)
				pDownloadList->AddSource(sender, source, true);
			else
				theApp.QueueDownloadListChangedEvent(_T("known-source-add-a4af"), CemuleApp::BackendCommandSourceNetworkClient);
		}
		if (source->GetDownloadState() != DS_CONNECTED)
			source->SwapToAnotherFile(_T("New A4AF source found. CDownloadQueue::CheckAndAddKnownSource()"), false, false, false, NULL, true, false); // ZZ:DownloadManager

		return false;
	}
	else if (source->GetRuntimeID() == 0 || m_mapDownloadSourceEntries.empty()) {
		for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
			const CPartFile *cur_file = filelist.GetNext(pos);
			if (cur_file->srclist.Find(source)) {
				if (cur_file == sender)
					return false;
				if (source->AddRequestForAnotherFile(sender)) {
					CDownloadListCtrl *pDownloadList = GetDownloadListForDownloadQueueUi();
					if (pDownloadList != NULL)
						pDownloadList->AddSource(sender, source, true);
					else
						theApp.QueueDownloadListChangedEvent(_T("known-source-add-a4af"), CemuleApp::BackendCommandSourceNetworkClient);
				}
				if (source->GetDownloadState() != DS_CONNECTED)
					source->SwapToAnotherFile(_T("New A4AF source found. CDownloadQueue::CheckAndAddKnownSource()"), false, false, false, NULL, true, false); // ZZ:DownloadManager

				return false;
			}
		}
	}

#ifdef _DEBUG
	const CPartFile *srcfile = source->GetRequestFile();
	if (thePrefs.GetVerbose() && srcfile) {
		// if a client sent us wrong sources (sources for some other file for which we asked but which we are also
		// downloading) we may get a little in trouble here when "moving" this source to some other partfile without
		// further checks and updates.
		if (!md4equ(srcfile->GetFileHash(), sender->GetFileHash()))
			AddDebugLogLine(false, _T("*** CDownloadQueue::CheckAndAddKnownSource -- added potential wrong source (%u)(diff. filehash) to file \"%s\""), source->GetUserIDHybrid(), (LPCTSTR)EscPercent(sender->GetFileName()));
		if (srcfile->GetPartCount() > 0 && srcfile->GetPartCount() != sender->GetPartCount())
			AddDebugLogLine(false, _T("*** CDownloadQueue::CheckAndAddKnownSource -- added potential wrong source (%u)(diff. partcount) to file \"%s\""), source->GetUserIDHybrid(), (LPCTSTR)EscPercent(sender->GetFileName()));
	}
#endif
	if (sender->srclist.Find(source) != NULL)
		return false;

	const CPartFile *pPreviousReqFile = source->GetRequestFile();
	source->SetRequestFile(sender);
	sender->srclist.AddTail(source);
	RegisterDownloadSource(sender, source);
	PrepareKnownSourceForFreshAsk(source, sender, pPreviousReqFile);
	source->SetSourceFrom(SF_PASSIVE);
	if (thePrefs.GetDebugSourceExchange())
		AddDebugLogLine(false, _T("SXRecv: Passively added source; %s, File=\"%s\""), (LPCTSTR)EscPercent(source->DbgGetClientInfo()), (LPCTSTR)EscPercent(sender->GetFileName()));
#ifdef _DEBUG
	if (thePrefs.GetVerbose() && source->GetPartCount() > 0 && source->GetPartCount() != sender->GetPartCount())
		DEBUG_ONLY(AddDebugLogLine(false, _T("*** CDownloadQueue::CheckAndAddKnownSource -- New added source (%u, %s) had still value in partcount"), source->GetUserIDHybrid(), (LPCTSTR)EscPercent(sender->GetFileName())));
#endif

	CDownloadListCtrl *pDownloadList = GetDownloadListForDownloadQueueUi();
	if (pDownloadList != NULL)
		pDownloadList->AddSource(sender, source, false);
	else
		theApp.QueueDownloadListChangedEvent(_T("known-source-add"), CemuleApp::BackendCommandSourceNetworkClient);
	return true;
}

bool CDownloadQueue::RebindSourceToServedEServerBuddy(CUpDownClient* pServedBuddy)
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::RebindSourceToServedEServerBuddy")))
		return false;
	if (pServedBuddy == NULL || !pServedBuddy->HasLowID() || pServedBuddy->GetUserIDHybrid() == 0 || pServedBuddy->GetServerIP() == 0)
		return false;
	if (theApp.clientlist == NULL || !theApp.clientlist->IsServedEServerBuddy(pServedBuddy))
		return false;

	struct SRebindCandidate
	{
		CPartFile* pFile;
		CUpDownClient* pStaleSource;
	};
	std::vector<SRebindCandidate> candidates;
	const uint32 dwServerIP = pServedBuddy->GetServerIP();
	const uint32 dwLowID = pServedBuddy->GetUserIDHybrid();
	const uint32 dwLowIDAlt = htonl(dwLowID);

	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile* pFile = filelist.GetNext(pos);
		if (pFile == NULL || pFile->srclist.Find(pServedBuddy) != NULL)
			continue;
		for (POSITION posSrc = pFile->srclist.GetHeadPosition(); posSrc != NULL;) {
			CUpDownClient* pSource = pFile->srclist.GetNext(posSrc);
			if (pSource == NULL || pSource == pServedBuddy || !pSource->HasLowID())
				continue;
			const uint32 dwSourceID = pSource->GetUserIDHybrid();
			if (dwSourceID != dwLowID && dwSourceID != dwLowIDAlt)
				continue;
			if (pSource->GetServerIP() != 0 && pSource->GetServerIP() != dwServerIP)
				continue;
			if (pSource->HasValidHash() && pServedBuddy->HasValidHash() && !md4equ(pSource->GetUserHash(), pServedBuddy->GetUserHash()))
				continue;
			SRebindCandidate candidate;
			candidate.pFile = pFile;
			candidate.pStaleSource = pSource;
			candidates.push_back(candidate);
		}
	}

	if (candidates.empty())
		return false;

	bool bServedBuddyPrimaryBound = false;
	for (POSITION posFile = filelist.GetHeadPosition(); posFile != NULL;) {
		CPartFile* pFile = filelist.GetNext(posFile);
		if (pFile != NULL && pFile->srclist.Find(pServedBuddy) != NULL) {
			bServedBuddyPrimaryBound = true;
			break;
		}
	}

	bool bChanged = false;
	CDownloadListCtrl* pDownloadList = GetDownloadListForDownloadQueueUi();
	for (size_t i = 0; i < candidates.size(); ++i) {
		CPartFile* pFile = candidates[i].pFile;
		CUpDownClient* pStaleSource = candidates[i].pStaleSource;
		if (pFile == NULL || pStaleSource == NULL)
			continue;
		bool bBoundThisFile = false;
		if (!bServedBuddyPrimaryBound && pFile->srclist.Find(pServedBuddy) == NULL) {
			const CPartFile* pPreviousReqFile = pServedBuddy->GetRequestFile();
			pServedBuddy->SetRequestFile(pFile);
			pFile->srclist.AddTail(pServedBuddy);
			RegisterDownloadSource(pFile, pServedBuddy);
			PrepareKnownSourceForFreshAsk(pServedBuddy, pFile, pPreviousReqFile);
			if (pDownloadList != NULL)
				pDownloadList->AddSource(pFile, pServedBuddy, false);
			else
				theApp.QueueDownloadListChangedEvent(_T("source-rebind-eserver-buddy"), CemuleApp::BackendCommandSourceNetworkClient);
			if (pServedBuddy->GetDownloadState() == DS_NONE || pServedBuddy->GetDownloadState() == DS_LOWTOLOWIP || pServedBuddy->GetDownloadState() == DS_WAITCALLBACK)
				pServedBuddy->SetDownloadState(DS_ONQUEUE, _T("Reused served eServer buddy as download source."));
			pServedBuddy->TrigNextSafeAskForDownload(pFile);
			bServedBuddyPrimaryBound = true;
			bBoundThisFile = true;
			bChanged = true;
		}
		if (bBoundThisFile) {
			RemoveSource(pStaleSource, false);
			if (thePrefs.GetLogNatTraversalEvents()) {
				DebugLog(_T("[eServerBuddy] Rebound transient LowID server source to served eServer buddy for file %s: %s"),
					(LPCTSTR)EscPercent(pFile->GetFileName()), (LPCTSTR)EscPercent(pServedBuddy->DbgGetClientInfo()));
			}
		} else if (thePrefs.GetLogNatTraversalEvents()) {
			DebugLog(_T("[eServerBuddy] Skipped served buddy rebind for file %s because the client already has a primary file: %s"),
				(LPCTSTR)EscPercent(pFile->GetFileName()), (LPCTSTR)EscPercent(pServedBuddy->DbgGetClientInfo()));
		}
	}

	return bChanged;
}

CUpDownClient* CDownloadQueue::CanonicalizeEServerRelaySource(CPartFile* pFile, CUpDownClient* pServerSource, CUpDownClient* pEndpointSource)
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::CanonicalizeEServerRelaySource")))
		return pServerSource;
	if (pFile == NULL || pServerSource == NULL || pEndpointSource == NULL || pServerSource == pEndpointSource)
		return pServerSource;
	if (pFile->srclist.Find(pServerSource) == NULL || pFile->srclist.Find(pEndpointSource) == NULL)
		return pServerSource;
	if (pServerSource->HasValidHash() && pEndpointSource->HasValidHash() && !md4equ(pServerSource->GetUserHash(), pEndpointSource->GetUserHash()))
		return pServerSource;
	if (pServerSource->GetA4AFCount() != 0 || !pServerSource->m_OtherNoNeeded_list.IsEmpty())
		return pServerSource;
	const EDownloadState eServerDownloadState = pServerSource->GetDownloadState();
	if (eServerDownloadState != DS_NONE && eServerDownloadState != DS_CONNECTING && eServerDownloadState != DS_WAITCALLBACK
		&& eServerDownloadState != DS_WAITCALLBACKKAD && eServerDownloadState != DS_ONQUEUE && eServerDownloadState != DS_LOWTOLOWIP)
		return pServerSource;
	if (pServerSource->GetUploadState() != US_NONE)
		return pServerSource;
	if (pServerSource->socket != NULL && (pServerSource->socket->IsConnected() || !pServerSource->socket->HaveNatTraversalLayer()))
		return pServerSource;

	const uint32 uServerLowID = pServerSource->GetUserIDHybrid();
	const uint32 uEndpointID = pEndpointSource->GetUserIDHybrid();
	if (uServerLowID == 0 || (uEndpointID != 0 && uEndpointID != 1 && uEndpointID != uServerLowID && uEndpointID != htonl(uServerLowID)))
		return pServerSource;
	if (pEndpointSource->GetServerIP() != 0 && pServerSource->GetServerIP() != 0 && pEndpointSource->GetServerIP() != pServerSource->GetServerIP())
		return pServerSource;
	if (pEndpointSource->GetServerPort() != 0 && pServerSource->GetServerPort() != 0 && pEndpointSource->GetServerPort() != pServerSource->GetServerPort())
		return pServerSource;

	if (pServerSource->socket != NULL) {
		pServerSource->socket->Safe_Delete();
		pServerSource->socket = NULL;
	}
	pServerSource->ResetUtpFlowControl();
	pServerSource->ClearUtpQueuedPackets();
	pServerSource->ResetConnectingState();
	theApp.clientlist->RemoveConnectingClient(pServerSource);

	if (!pEndpointSource->HasValidHash() && pServerSource->HasValidHash())
		pEndpointSource->SetUserHash(pServerSource->GetUserHash(), true);
	pEndpointSource->SetUserIDHybrid(uServerLowID);
	if (pServerSource->GetServerIP() != 0)
		pEndpointSource->SetServerIP(pServerSource->GetServerIP());
	if (pServerSource->GetServerPort() != 0)
		pEndpointSource->SetServerPort(pServerSource->GetServerPort());
	pEndpointSource->SetSourceFrom(SF_SERVER);

	RemoveSource(pServerSource);
	pServerSource->SetUserHash(NULL, false);
	pServerSource->SetUserIDHybrid(0);
	pServerSource->SetServerIP(0);
	pServerSource->SetServerPort(0);
	pServerSource->SetConnectIP(CAddress());
	pServerSource->SetUserPort(0);
	pServerSource->SetKadPort(0);
	pServerSource->SetUDPPort(0);
	pServerSource->SetServingBuddyID(NULL);
	RefreshDownloadSource(pEndpointSource);
	pEndpointSource->TrigNextSafeAskForDownload(pFile);
	if (pEndpointSource->GetDownloadState() == DS_NONE || pEndpointSource->GetDownloadState() == DS_LOWTOLOWIP || pEndpointSource->GetDownloadState() == DS_WAITCALLBACK)
		pEndpointSource->SetDownloadState(DS_ONQUEUE, _T("Canonicalized eServer relay source with live NAT-T endpoint."));

	if (thePrefs.GetLogNatTraversalEvents()) {
		DebugLog(_T("[eServerBuddy] Canonicalized LowID server source with endpoint source for file %s: %s"),
			(LPCTSTR)EscPercent(pFile->GetFileName()), (LPCTSTR)EscPercent(pEndpointSource->DbgGetClientInfo()));
	}
	return pEndpointSource;
}

bool CDownloadQueue::RemoveSource(CUpDownClient *toremove, bool bDoStatsUpdate)
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::RemoveSource")))
		return false;

	CDownloadListCtrl *pDownloadList = GetDownloadListForDownloadQueueUi();
	bool bQueuedListChangedEvent = false;
	bool bRemovedSrcFromPartFile = false;
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *cur_file = filelist.GetNext(pos);
		bool bRemovedSrcFromThisFile = false;
		for (POSITION pos2 = cur_file->srclist.Find(toremove); pos2 != NULL; pos2 = cur_file->srclist.Find(toremove)) {
			if (!bRemovedSrcFromThisFile)
				UnregisterDownloadSource(cur_file, toremove);
			cur_file->srclist.RemoveAt(pos2);
			bRemovedSrcFromThisFile = true;
		}
		if (bRemovedSrcFromThisFile) {
			cur_file->RemoveSourceFileName(toremove);
			bRemovedSrcFromPartFile = true;
			if (bDoStatsUpdate) {
				cur_file->RemoveDownloadingSource(toremove);
				cur_file->UpdatePartsInfo();
				cur_file->UpdateAvailablePartsCount();
			}
		}
	}

	// remove this source on all files in the download queue who link this source
	// pretty slow but no way around, maybe using a Map is better, but that's slower on other parts
	for (POSITION pos = toremove->m_OtherRequests_list.GetHeadPosition(); pos != NULL;) {
		const POSITION pos1 = pos;
		CPartFile *pfile = toremove->m_OtherRequests_list.GetNext(pos);
		POSITION pos2 = pfile->A4AFsrclist.Find(toremove);
		if (pos2) {
			pfile->A4AFsrclist.RemoveAt(pos2);
			RemoveSourceFromDownloadListOrQueueChangeEvent(pDownloadList, toremove, pfile, bQueuedListChangedEvent);
			toremove->m_OtherRequests_list.RemoveAt(pos1);
		}
	}
	for (POSITION pos = toremove->m_OtherNoNeeded_list.GetHeadPosition(); pos != NULL;) {
		const POSITION pos1 = pos;
		CPartFile *pfile = toremove->m_OtherNoNeeded_list.GetNext(pos);
		POSITION pos2 = pfile->A4AFsrclist.Find(toremove);
		if (pos2) {
			pfile->A4AFsrclist.RemoveAt(pos2);
			RemoveSourceFromDownloadListOrQueueChangeEvent(pDownloadList, toremove, pfile, bQueuedListChangedEvent);
			toremove->m_OtherNoNeeded_list.RemoveAt(pos1);
		}
	}

	if (bRemovedSrcFromPartFile && (toremove->HasFileRating() || !toremove->GetFileComment().IsEmpty())) {
		CPartFile *pFile = toremove->GetRequestFile();
		if (pFile)
			pFile->UpdateFileRatingCommentAvail();
	}

	toremove->SetDownloadState(DS_NONE);
	RemoveSourceFromDownloadListOrQueueChangeEvent(pDownloadList, toremove, 0, bQueuedListChangedEvent);
	toremove->SetRequestFile(NULL);
	return bRemovedSrcFromPartFile;
}

void CDownloadQueue::RemoveFile(CPartFile *toremove)
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::RemoveFile")))
		return;

	RemoveLocalServerRequest(toremove);
	RemoveDeferredDownloadValidatorAdd(toremove);
	RemoveDeferredInitialPartMetSave(toremove);
	RemoveDeferredSourceSave(toremove);
	m_posDeferredPartFileCreateQueueFile = NULL;
	if (toremove != NULL && toremove->HasUnqueuedPartFileDiskCreate())
		m_bDeferredPartFileCreateQueuePending = true;

	POSITION pos = NULL;
	std::map<CPartFile*, POSITION>::iterator itBulkRemovePos = m_bulkRemoveFilePositions.end();
	if (IsBulkRemovingDownloads()) {
		itBulkRemovePos = m_bulkRemoveFilePositions.find(toremove);
		if (itBulkRemovePos != m_bulkRemoveFilePositions.end())
			pos = itBulkRemovePos->second;
	}
	if (pos == NULL)
		pos = filelist.Find(toremove);
	if (pos != NULL)
	{
		for (POSITION posSource = toremove->srclist.GetHeadPosition(); posSource != NULL;)
			UnregisterDownloadSource(toremove, toremove->srclist.GetNext(posSource));
		filelist.RemoveAt(pos);
		if (itBulkRemovePos != m_bulkRemoveFilePositions.end())
			m_bulkRemoveFilePositions.erase(itBulkRemovePos);
		UnindexDownloadFile(toremove);
		TouchDownloadModelSequence();
		theApp.DownloadValidator->RemoveFromMap(toremove->GetFileHash(), toremove->GetFileName(), toremove->GetFileSize());
		if (theApp.searchlist != NULL)
			theApp.searchlist->QueueKnownTypeRefreshForHash(toremove->GetFileHash());
		
		// Remove from active download cache when file is removed from queue
		if (theApp.emuledlg && theApp.emuledlg->transferwnd)
			theApp.emuledlg->transferwnd->RemoveActiveDownload(toremove);
	}

	if (IsBulkRemovingDownloads()) {
		m_bBulkRemovePending = true;
		++m_uBulkRemovedFiles;
		return;
	}

	ExportPartMetFilesOverview();
}

void CDownloadQueue::DeleteAll()
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::DeleteAll")))
		return;

	ClearDownloadSourceIndexes();
	m_posDeferredPartFileCreateQueueFile = NULL;
	m_bDeferredPartFileCreateQueuePending = false;
	m_bBulkAddDiskFinalizationActive = false;
	m_uBulkAddDiskFinalizationTotal = 0;
	m_dwLastBulkAddDiskFinalizationNotifyTick = 0;
	::InterlockedExchange(&m_lBulkAddDiskFinalizationProgressUpdatePending, 0);
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *cur_file = filelist.GetNext(pos);
		cur_file->srclist.RemoveAll();
		// Barry - Should also remove all requested blocks
		// Don't worry about deleting the blocks, that gets handled
		// when CUpDownClient is deleted in CClientList::DeleteAll()
		cur_file->RemoveAllRequestedBlocks();
	}
}

// Max. file IDs per UDP packet
// ----------------------------
// 576 - 30 bytes of header (28 for UDP, 2 for "E3 9A" edonkey proto) = 546 bytes
// 546 / 16 = 34
#define MAX_UDP_PACKET_DATA				510
#define BYTES_PER_FILE_G1				16
#define BYTES_PER_FILE_G2				20
#define ADDITIONAL_BYTES_PER_LARGEFILE	8
#define MAX_REQUESTS_PER_SERVER			68	// Increased to 68 (was 35). Safely doubles UDP search throughput within eserver's anti-flood limits (max 3 packets/sec).

bool CDownloadQueue::IsMaxFilesPerUDPServerPacketReached(uint32 nFiles, uint32 nIncludedLargeFiles) const
{
	if (cur_udpserver && cur_udpserver->GetUDPFlags() & SRV_UDPFLG_EXT_GETSOURCES) {

		const int nBytesPerNormalFile = ((cur_udpserver->GetUDPFlags() & SRV_UDPFLG_EXT_GETSOURCES2) > 0) ? BYTES_PER_FILE_G2 : BYTES_PER_FILE_G1;
		const int nUsedBytes = nFiles * nBytesPerNormalFile + nIncludedLargeFiles * ADDITIONAL_BYTES_PER_LARGEFILE;
		if (nIncludedLargeFiles > 0) {
			ASSERT(cur_udpserver->SupportsLargeFilesUDP());
			ASSERT(cur_udpserver->GetUDPFlags() & SRV_UDPFLG_EXT_GETSOURCES2);
		}
		return (m_cRequestsSentToServer >= MAX_REQUESTS_PER_SERVER) || (nUsedBytes >= MAX_UDP_PACKET_DATA);
	}
	ASSERT(nIncludedLargeFiles == 0);
	return nFiles != 0;
}

bool CDownloadQueue::SendGlobGetSourcesUDPPacket(CSafeMemFile &data, bool bExt2Packet, uint32 nFiles, uint32 nIncludedLargeFiles)
{
	bool bSentPacket = false;

	if (cur_udpserver) {
#ifdef _DEBUG
		int iPacketSize = (int)data.GetLength();
#endif
		Packet packet(data);
		if (bExt2Packet) {
			ASSERT(iPacketSize > 0 && (uint32)iPacketSize == nFiles * 20 + nIncludedLargeFiles * 8);
			packet.opcode = OP_GLOBGETSOURCES2;
		} else {
			ASSERT(iPacketSize > 0 && (uint32)iPacketSize == nFiles * 16 && nIncludedLargeFiles == 0);
			packet.opcode = OP_GLOBGETSOURCES;
		}
		if (thePrefs.GetDebugServerUDPLevel() > 0)
			Debug(_T(">>> Sending %s to server %-21s (%3i of %3u); FileIDs=%u(%u large)\n")
				, (packet.opcode == OP_GLOBGETSOURCES2) ? _T("OP_GlobGetSources2") : _T("OP_GlobGetSources1")
				, (LPCTSTR)ipstr(cur_udpserver->GetAddress(), cur_udpserver->GetPort())
				, m_iSearchedServers + 1
				, (unsigned)theApp.serverlist->GetServerCount()
				, nFiles
				, nIncludedLargeFiles);

		theStats.AddUpDataOverheadServer(packet.size);
		theApp.serverconnect->SendUDPPacket(&packet, cur_udpserver, false);

		m_cRequestsSentToServer += nFiles;
		bSentPacket = true;
	}

	return bSentPacket;
}

bool CDownloadQueue::SendNextUDPPacket()
{
	if (filelist.IsEmpty()
		|| !theApp.serverconnect->IsUDPSocketAvailable()
		|| !theApp.serverconnect->IsConnected()
		|| thePrefs.IsCryptLayerRequired()) // we cannot use sources received without user hash, so don't ask
	{
		return false;
	}

	CServer *pConnectedServer = theApp.serverconnect->GetCurrentServer();
	if (pConnectedServer)
		pConnectedServer = theApp.serverlist->GetServerByAddress(pConnectedServer->GetAddress(), pConnectedServer->GetPort());

	if (!cur_udpserver) {
		m_cRequestsSentToServer = 0;
		do {
			cur_udpserver = theApp.serverlist->GetSuccServer(cur_udpserver);
			if (cur_udpserver == NULL) {
				StopUDPRequests();
				return false;
			}
		} while (cur_udpserver == pConnectedServer || cur_udpserver->GetFailedCount() >= thePrefs.GetDeadServerRetries());
	}

	bool bGetSources2Packet = (cur_udpserver->GetUDPFlags() & SRV_UDPFLG_EXT_GETSOURCES2) > 0;
	bool bServerSupportsLargeFiles = cur_udpserver->SupportsLargeFilesUDP();

	// loop until the packet is filled, or a packet was sent
	bool bSentPacket = false;
	CSafeMemFile dataGlobGetSources(20);
	int iFiles = 0;
	int iLargeFiles = 0;
	while (!IsMaxFilesPerUDPServerPacketReached(iFiles, iLargeFiles) && !bSentPacket) {
		// get the next file to search sources for
		CPartFile *nextfile = NULL;
		while (!bSentPacket && !(nextfile && (nextfile->GetStatus() == PS_READY || nextfile->GetStatus() == PS_EMPTY))) {
			if (m_lastfile == NULL) // we just started the global source searching or have switched the server
				// get the first file to search sources for
				nextfile = filelist.GetHead();
			else {
				POSITION pos = filelist.Find(m_lastfile);
				if (pos == NULL) // the last file is no longer in the DL-list (may have been finished or cancelled)
					// get the first file to search sources for
					nextfile = filelist.GetHead();
				else {
					filelist.GetNext(pos);
					if (pos == 0) { // finished asking the current server for all files
						// if there are pending requests for the current server, send them
						if (dataGlobGetSources.GetLength() > 0) {
							if (SendGlobGetSourcesUDPPacket(dataGlobGetSources, bGetSources2Packet, iFiles, iLargeFiles))
								bSentPacket = true;
							dataGlobGetSources.SetLength(0);
							iFiles = 0;
							iLargeFiles = 0;
						}

						m_cRequestsSentToServer = 0;
						// get next server to ask
						do {
							cur_udpserver = theApp.serverlist->GetSuccServer(cur_udpserver);
							if (cur_udpserver == NULL) {
								// finished asking all servers for all files
								if (thePrefs.GetDebugServerUDPLevel() > 0 && thePrefs.GetDebugServerSourcesLevel() > 0)
									Debug(_T("Finished UDP search processing for all servers (%u)\n"), (unsigned)theApp.serverlist->GetServerCount());
								StopUDPRequests();
								return false; // finished (processed all file & all servers)
							}
						} while (cur_udpserver == pConnectedServer || cur_udpserver->GetFailedCount() >= thePrefs.GetDeadServerRetries());
						++m_iSearchedServers;

						// if we already sent a packet, switch to the next file at next function call
						if (bSentPacket) {
							m_lastfile = NULL;
							break;
						}

						bGetSources2Packet = (cur_udpserver->GetUDPFlags() & SRV_UDPFLG_EXT_GETSOURCES2) > 0;
						bServerSupportsLargeFiles = cur_udpserver->SupportsLargeFilesUDP();

						// have selected a new server; get the first file to search sources for
						nextfile = filelist.GetHead();
					} else
						nextfile = filelist.GetAt(pos);
				}
			}
			m_lastfile = nextfile;
		}

		if (!bSentPacket && nextfile && nextfile->GetSourceCount() < nextfile->GetMaxSourcePerFileUDP() && (bServerSupportsLargeFiles || !nextfile->IsLargeFile())) {
			// GETSOURCES Packet (<HASH_16> *)
			dataGlobGetSources.WriteHash16(nextfile->GetFileHash());
			if (bGetSources2Packet)
				if (nextfile->IsLargeFile()) {
					// GETSOURCES2 Packet Large File (<HASH_16><IND_4 = 0><SIZE_8> *)
					++iLargeFiles;
					dataGlobGetSources.WriteUInt32(0);
					dataGlobGetSources.WriteUInt64(nextfile->GetFileSize());
				} else {
					// GETSOURCES2 Packet (<HASH_16><SIZE_4> *)
					dataGlobGetSources.WriteUInt32((uint32)(uint64)nextfile->GetFileSize());
				}

			++iFiles;
			if (thePrefs.GetDebugServerUDPLevel() > 0 && thePrefs.GetDebugServerSourcesLevel() > 0)
				Debug(_T(">>> Queued  %s to server %-21s (%3i of %3u); Buff  %u(%u)=%s\n"), bGetSources2Packet ? _T("OP_GlobGetSources2") : _T("OP_GlobGetSources1"), (LPCTSTR)ipstr(cur_udpserver->GetAddress(), cur_udpserver->GetPort()), m_iSearchedServers + 1, (unsigned)theApp.serverlist->GetServerCount(), iFiles, iLargeFiles, (LPCTSTR)DbgGetFileInfo(nextfile->GetFileHash()));
		}
	}

	ASSERT(dataGlobGetSources.GetLength() == 0 || !bSentPacket);

	if (!bSentPacket && dataGlobGetSources.GetLength() > 0)
		SendGlobGetSourcesUDPPacket(dataGlobGetSources, bGetSources2Packet, iFiles, iLargeFiles);

	// send max 35 UDP request to one server per interval
	// if we have more than 35 files, we rotate the list and use it as a queue
	if (m_cRequestsSentToServer >= MAX_REQUESTS_PER_SERVER) {
		if (thePrefs.GetDebugServerUDPLevel() > 0 && thePrefs.GetDebugServerSourcesLevel() > 0)
			Debug(_T("Rotating file list\n"));

		// move the last 35 files to the head
		if (filelist.GetCount() > MAX_REQUESTS_PER_SERVER)
			for (int i = MAX_REQUESTS_PER_SERVER; --i >= 0;)
				filelist.AddHead(filelist.RemoveTail());

		m_cRequestsSentToServer = 0;
		// and next server
		do {
			cur_udpserver = theApp.serverlist->GetSuccServer(cur_udpserver);
			if (cur_udpserver == NULL) {
				if (thePrefs.GetDebugServerUDPLevel() > 0 && thePrefs.GetDebugServerSourcesLevel() > 0)
					Debug(_T("Finished UDP search processing for all servers (%u)\n"), (unsigned)theApp.serverlist->GetServerCount());
				StopUDPRequests();
				return false; // finished (processed all file & all servers)
			}
		} while (cur_udpserver == pConnectedServer || cur_udpserver->GetFailedCount() >= thePrefs.GetDeadServerRetries());
		++m_iSearchedServers;
		m_lastfile = NULL;
	}

	return true;
}

void CDownloadQueue::StopUDPRequests()
{
	cur_udpserver = NULL;
	m_lastudpsearchtime = ::GetTickCount();
	m_lastfile = NULL;
	m_iSearchedServers = 0;
}


void CDownloadQueue::SortByPriority()
{
	if (!filelist.GetCount())
		return;

	struct SSortRow
	{
		CPartFile *pFile;
		UINT uCategory;
		int iCategoryPriority;
		uint8 uDownloadPriority;
		bool bAlphabetical;
		CString strFileName;
	};

	std::vector<SSortRow> vec;
	vec.reserve(static_cast<size_t>(filelist.GetCount()));
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *pFile = filelist.GetNext(pos);
		if (pFile == NULL)
			continue;
		SSortRow row;
		row.pFile = pFile;
		row.uCategory = pFile->GetCategory();
		const Category_Struct *pCategory = thePrefs.GetCategory(row.uCategory);
		row.iCategoryPriority = pCategory != NULL ? pCategory->prio : 0;
		row.uDownloadPriority = pFile->GetDownPriority();
		row.bAlphabetical = row.uCategory != 0 && pCategory != NULL && pCategory->downloadInAlphabeticalOrder && thePrefs.IsExtControlsEnabled();
		if (row.bAlphabetical)
			row.strFileName = pFile->GetFileName();
		vec.push_back(row);
	}

	CombinedSort(vec.begin(), vec.end(), [](const SSortRow& left, const SSortRow& right) -> bool {
		const bool bRightHasHigherPriority = right.iCategoryPriority > left.iCategoryPriority
			|| (right.iCategoryPriority == left.iCategoryPriority
				&& (right.uDownloadPriority > left.uDownloadPriority
					|| (right.uDownloadPriority == left.uDownloadPriority
						&& right.uCategory == left.uCategory
						&& right.bAlphabetical
						&& !right.strFileName.IsEmpty() && !left.strFileName.IsEmpty()
						&& right.strFileName.CompareNoCase(left.strFileName) < 0)));
		return !bRightHasHigherPriority;
	});

	POSITION pos = filelist.GetHeadPosition();
	for (auto& row : vec) {
		filelist.SetAt(pos, row.pFile);
		filelist.GetNext(pos);
	}
}

void CDownloadQueue::CheckDiskspaceTimed()
{
	if (time(NULL) >= theApp.m_tLastDiskSpaceCheckTime + thePrefs.GetFreeDiskSpaceCheckPeriod())
		CheckDiskspace();
}

void CDownloadQueue::CheckDiskspace(bool bNotEnoughSpaceLeft)
{

	// sorting the list could be done here, but I prefer to "see" that function call in the calling functions.

	// If disabled, resume any previously paused files
	if (!thePrefs.IsCheckDiskspaceEnabled()) {
		if (!bNotEnoughSpaceLeft) // avoid the worst case, if we already had 'disk full'
			for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
				CPartFile *cur_file = filelist.GetNext(pos);
				switch (cur_file->GetStatus()) {
				case PS_PAUSED:
				case PS_ERROR:
				case PS_COMPLETING:
				case PS_COMPLETE:
					break;
				default:
					cur_file->ResumeFileInsufficient();
				}
			}
		return;
	}

	// 'bNotEnoughSpaceLeft' - avoid worse case of having already 'disk full'
	uint64 nTotalAvailableSpaceMain = bNotEnoughSpaceLeft ? 0 : GetFreeDiskSpaceX(thePrefs.GetTempDir(), true);

	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile* cur_file = filelist.GetNext(pos);

		switch (cur_file->GetStatus()) {
		case PS_PAUSED:
		case PS_ERROR:
		case PS_COMPLETING:
		case PS_COMPLETE:
			break;
		default:
			uint64 nTotalAvailableSpace = bNotEnoughSpaceLeft ? 0 :
				((thePrefs.GetTempDirCount() == 1) ? nTotalAvailableSpaceMain : GetFreeDiskSpaceX(cur_file->GetTmpPath(), true));
			if (thePrefs.GetMinFreeDiskSpace() == 0) {
				// Pause the file only if it would grow in size and would exceed the currently available free space
				if (cur_file->GetNeededSpace() <= nTotalAvailableSpace)
					cur_file->ResumeFileInsufficient();
				else
					cur_file->PauseFile(true);
			} else if (nTotalAvailableSpace < thePrefs.GetMinFreeDiskSpace()) {
				// Compressed/sparse files: always pause the file
				// Normal files: pause the file only if it would still grow
				if (!cur_file->IsNormalFile() || cur_file->GetNeededSpace() > 0)
					cur_file->PauseFile(true);
			} else {
				// Doesn't work this way. Resuming the file without checking if there is a chance to successfully
				// flush any available buffered file data will pause the file right after it was resumed and disturb
				// the StopPausedFile function.
			}
		}
	}
}

void CDownloadQueue::GetDownloadSourcesStats(SDownloadStats &results)
{
	memset(&results, 0, sizeof results);
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		const CPartFile *cur_file = filelist.GetNext(pos);

		results.a[0] += cur_file->GetSourceCount();
		results.a[1] += cur_file->GetTransferringSrcCount();
		results.a[2] += cur_file->GetSrcStatisticsValue(DS_ONQUEUE);
		results.a[3] += cur_file->GetSrcStatisticsValue(DS_REMOTEQUEUEFULL);
		results.a[4] += cur_file->GetSrcStatisticsValue(DS_NONEEDEDPARTS);
		results.a[5] += cur_file->GetSrcStatisticsValue(DS_CONNECTED);
		results.a[6] += cur_file->GetSrcStatisticsValue(DS_REQHASHSET);
		results.a[7] += cur_file->GetSrcStatisticsValue(DS_CONNECTING);
		results.a[8] += cur_file->GetSrcStatisticsValue(DS_WAITCALLBACK);
		results.a[8] += cur_file->GetSrcStatisticsValue(DS_WAITCALLBACKKAD);
		results.a[9] += cur_file->GetSrcStatisticsValue(DS_TOOMANYCONNS);
		results.a[9] += cur_file->GetSrcStatisticsValue(DS_TOOMANYCONNSKAD);
		results.a[10] += cur_file->GetSrcStatisticsValue(DS_LOWTOLOWIP);
		results.a[11] += cur_file->GetSrcStatisticsValue(DS_NONE);
		results.a[12] += cur_file->GetSrcStatisticsValue(DS_ERROR);
		results.a[13] += cur_file->GetSrcStatisticsValue(DS_BANNED);
		results.a[14] += cur_file->src_stats[3];
		results.a[15] += cur_file->GetSrcA4AFCount();
		results.a[16] += cur_file->src_stats[0];
		results.a[17] += cur_file->src_stats[1];
		results.a[18] += cur_file->src_stats[2];
		results.a[19] += cur_file->net_stats[0];
		results.a[20] += cur_file->net_stats[1];
		results.a[21] += cur_file->net_stats[2];
		results.a[22] += static_cast<unsigned>(cur_file->m_DeadSourceList.GetDeadSourcesCount());
	}
}

CUpDownClient* CDownloadQueue::GetDownloadClientByIP(const CAddress& IP)
{
	std::pair<std::multimap<CAddress, DWORD>::const_iterator, std::multimap<CAddress, DWORD>::const_iterator> range = m_mapDownloadSourcesByIP.equal_range(IP);
	for (std::multimap<CAddress, DWORD>::const_iterator it = range.first; it != range.second; ++it) {
		CUpDownClient *pClient = ResolveDownloadSourceRuntimeID(it->second);
		if (IsSameDownloadClientIP(IP, pClient))
			return pClient;
	}

	if (m_mapDownloadSourceEntries.empty()) {
		for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
			const CPartFile *cur_file = filelist.GetNext(pos);
			for (POSITION pos2 = cur_file->srclist.GetHeadPosition(); pos2 != NULL;) {
				CUpDownClient *cur_client = cur_file->srclist.GetNext(pos2);
				if (IsSameDownloadClientIP(IP, cur_client))
					return cur_client;
			}
		}
	}
	return NULL;
}

CUpDownClient* CDownloadQueue::GetDownloadClientByIP_UDP(const CAddress& IP, uint16 nUDPPort, bool bIgnorePortOnUniqueIP, bool* pbMultipleIPs)
{
	if (pbMultipleIPs != NULL)
		*pbMultipleIPs = false;

	if (nUDPPort != 0) {
		const SDownloadSourceEndpointKey udpKey(IP, nUDPPort);
		std::pair<std::multimap<SDownloadSourceEndpointKey, DWORD>::const_iterator, std::multimap<SDownloadSourceEndpointKey, DWORD>::const_iterator> udpRange = m_mapDownloadSourcesByUDP.equal_range(udpKey);
		for (std::multimap<SDownloadSourceEndpointKey, DWORD>::const_iterator it = udpRange.first; it != udpRange.second; ++it) {
			CUpDownClient *pClient = ResolveDownloadSourceRuntimeID(it->second);
			if (pClient != NULL && pClient->GetUDPPort() == nUDPPort && IsSameDownloadClientIP(IP, pClient))
				return pClient;
		}
	}

	if (nUDPPort == 0 && !bIgnorePortOnUniqueIP) {
		std::pair<std::multimap<CAddress, DWORD>::const_iterator, std::multimap<CAddress, DWORD>::const_iterator> range = m_mapDownloadSourcesByIP.equal_range(IP);
		for (std::multimap<CAddress, DWORD>::const_iterator it = range.first; it != range.second; ++it) {
			CUpDownClient *pClient = ResolveDownloadSourceRuntimeID(it->second);
			if (pClient != NULL && pClient->GetUDPPort() == 0 && IsSameDownloadClientIP(IP, pClient))
				return pClient;
		}
	}

	CUpDownClient *pMatchingIPClient = NULL;
	uint32 cMatches = 0;
	if (bIgnorePortOnUniqueIP) {
		std::pair<std::multimap<CAddress, DWORD>::const_iterator, std::multimap<CAddress, DWORD>::const_iterator> range = m_mapDownloadSourcesByIP.equal_range(IP);
		for (std::multimap<CAddress, DWORD>::const_iterator it = range.first; it != range.second; ++it) {
			CUpDownClient *pClient = ResolveDownloadSourceRuntimeID(it->second);
			if (IsSameDownloadClientIP(IP, pClient)) {
				if (pClient != pMatchingIPClient) {
					pMatchingIPClient = pClient;
					++cMatches;
				}
			}
		}
	}

	if (pbMultipleIPs != NULL)
		*pbMultipleIPs = cMatches > 1;
	if (pMatchingIPClient != NULL && cMatches == 1)
		return pMatchingIPClient;

	if (m_mapDownloadSourceEntries.empty()) {
		pMatchingIPClient = NULL;
		cMatches = 0;
		for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
			const CPartFile *cur_file = filelist.GetNext(pos);
			for (POSITION pos2 = cur_file->srclist.GetHeadPosition(); pos2 != NULL;) {
				CUpDownClient *cur_client = cur_file->srclist.GetNext(pos2);
				if (IsSameDownloadClientIP(IP, cur_client) && nUDPPort == cur_client->GetUDPPort())
					return cur_client;
				if (IsSameDownloadClientIP(IP, cur_client) && bIgnorePortOnUniqueIP && cur_client != pMatchingIPClient) {
					pMatchingIPClient = cur_client;
					++cMatches;
				}
			}
		}
		if (pbMultipleIPs != NULL)
			*pbMultipleIPs = cMatches > 1;
		if (pMatchingIPClient != NULL && cMatches == 1)
			return pMatchingIPClient;
	}
	return NULL;
}

void CDownloadQueue::TriggerPendingNatTraversalDownloads(LPCTSTR pszReason)
{
	if (!thePrefs.IsNatTraversalServiceEnabled())
		return;
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::TriggerPendingNatTraversalDownloads")))
		return;

	UINT uTriggered = 0;
	for (POSITION posFile = filelist.GetHeadPosition(); posFile != NULL;) {
		CPartFile* pFile = filelist.GetNext(posFile);
		if (pFile == NULL || pFile->IsStopped() || (pFile->GetStatus() != PS_READY && pFile->GetStatus() != PS_EMPTY))
			continue;

		for (POSITION posSource = pFile->srclist.GetHeadPosition(); posSource != NULL;) {
			CUpDownClient* pSource = pFile->srclist.GetNext(posSource);
			if (pSource == NULL || !pSource->HasLowID() || !pSource->HasValidServingBuddyID())
				continue;
			if (pSource->GetServingBuddyIP().IsNull() || pSource->GetServingBuddyPort() == 0)
				continue;
			if (pSource->GetRequestFile() != NULL && pSource->GetRequestFile() != pFile)
				continue;
			if (pSource->socket != NULL && pSource->socket->IsConnected() && pSource->GetDownloadState() == DS_DOWNLOADING)
				continue;

			switch (pSource->GetDownloadState()) {
				case DS_ONQUEUE:
				case DS_CONNECTING:
				case DS_WAITCALLBACK:
				case DS_WAITCALLBACKKAD:
				case DS_LOWTOLOWIP:
				case DS_NONE:
				case DS_TOOMANYCONNS:
				case DS_TOOMANYCONNSKAD:
					break;
				default:
					continue;
			}

			if (pSource->GetDownloadState() == DS_LOWTOLOWIP)
				pSource->SetDownloadState(DS_ONQUEUE, pszReason != NULL ? pszReason : _T("Local serving buddy became ready."));

			pSource->TrigNextSafeAskForDownload(pFile);
			if (pSource->HasPendingNatTRetry())
				pSource->MarkNatTRendezvous(2, true);
			++uTriggered;
		}
	}

	if (uTriggered != 0 && thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NatTraversal] TriggerPendingNatTraversalDownloads: scheduled %u pending source(s), reason=%s"), uTriggered, pszReason != NULL ? pszReason : _T(""));
}

bool CDownloadQueue::IsInList(const CUpDownClient* client) const
{
	if (client == NULL)
		return false;
	if (client->GetRuntimeID() != 0 && m_mapDownloadSourceEntries.find(client->GetRuntimeID()) != m_mapDownloadSourceEntries.end())
		return true;
	if (client->GetRuntimeID() != 0 && !m_mapDownloadSourceEntries.empty())
		return false;
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;)
		if (filelist.GetNext(pos)->srclist.Find(const_cast<CUpDownClient*>(client)))
			return true;
	return false;
}

void CDownloadQueue::ResetCatParts(UINT cat)
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::ResetCatParts")))
		return;

	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *cur_file = filelist.GetNext(pos);

		if (cur_file->GetCategory() == cat)
			cur_file->SetCategory(0);
		else if (cur_file->GetCategory() > cat)
			cur_file->SetCategory(cur_file->GetCategory() - 1);
	}
}

void CDownloadQueue::SetCatPrio(UINT cat, uint8 newprio)
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::SetCatPrio")))
		return;

	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *cur_file = filelist.GetNext(pos);
		if (cat == 0 || cur_file->GetCategory() == cat)
			if (newprio == PR_AUTO) {
				cur_file->SetAutoDownPriority(true);
				cur_file->SetDownPriority(PR_HIGH, false);
			} else {
				cur_file->SetAutoDownPriority(false);
				cur_file->SetDownPriority(newprio, false);
			}
	}

	theApp.downloadqueue->SortByPriority();
	theApp.downloadqueue->CheckDiskspaceTimed();
}

void CDownloadQueue::RemoveAutoPrioInCat(UINT cat, uint8 newprio)
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::RemoveAutoPrioInCat")))
		return;

	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *cur_file = filelist.GetNext(pos);
		if (cur_file->IsAutoDownPriority() && (cat == 0 || cur_file->GetCategory() == cat)) {
			cur_file->SetAutoDownPriority(false);
			cur_file->SetDownPriority(newprio, false);
		}
	}

	theApp.downloadqueue->SortByPriority();
	theApp.downloadqueue->CheckDiskspaceTimed();
}

void CDownloadQueue::SetCatStatus(UINT cat, int newstatus)
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::SetCatStatus")))
		return;

	bool reset = false;
	bool resort = false;

	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *cur_file = filelist.GetNext(pos);
		if (!cur_file)
			continue;

		if (cat == (UINT)-1
			|| (cat == (UINT)-2 && cur_file->GetCategory() == 0)
			|| (cat == 0 && cur_file->CheckShowItemInGivenCat(cat))
			|| (cat > 0 && cat == cur_file->GetCategory()))
		{
			switch (newstatus) {
			case MP_CANCEL:
				cur_file->DeletePartFile();
				reset = true;
				break;
			case MP_PAUSE:
				cur_file->PauseFile(false);
				resort = true;
				break;
			case MP_STOP:
				cur_file->StopFile(false);
				resort = true;
				break;
			case MP_RESUME:
				if (cur_file->CanResumeFile()) {
					if (cur_file->GetStatus() == PS_INSUFFICIENT)
						cur_file->ResumeFileInsufficient();
					else {
						cur_file->ResumeFile(false);
						resort = true;
					}
				}
			}
		}
		if (reset) {
			reset = false;
			pos = filelist.GetHeadPosition();
		}
	}

	if (resort) {
		theApp.downloadqueue->SortByPriority();
		theApp.downloadqueue->CheckDiskspace();
	}
}

void CDownloadQueue::MoveCat(UINT from, UINT to)
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::MoveCat")))
		return;

	to -= static_cast<UINT>(from < to);
	const UINT cmin = min(from, to);
	const UINT cmax = max(from, to);
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *cur_file = filelist.GetNext(pos);
		if (cur_file) {
			UINT mycat = cur_file->GetCategory();
			if (mycat >= cmin && mycat <= cmax)
				if (mycat == from)
					mycat = to;
				else
					mycat += (from < to ? -1 : 1);
			cur_file->SetCategory(mycat);
		}
	}
}

UINT CDownloadQueue::GetDownloadingFileCount() const
{
	UINT result = 0;
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		const EPartFileStatus uStatus = filelist.GetNext(pos)->GetStatus();
		if (uStatus == PS_READY || uStatus == PS_EMPTY)
			++result;
	}
	return result;
}

UINT CDownloadQueue::GetPausedFileCount() const
{
	UINT result = 0;
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;)
		if (filelist.GetNext(pos)->GetStatus() == PS_PAUSED)
			++result;
	return result;
}

void CDownloadQueue::SetAutoCat(CPartFile *newfile)
{
	if (thePrefs.GetCatCount() < 2 || newfile->GetCategory() > 0)
		return;

	bool bFound = false;
	for (INT_PTR i = thePrefs.GetCatCount(); --i > 0;) {
		CString catExt(thePrefs.GetCategory(i)->autocat);
		if (catExt.IsEmpty())
			continue;

		if (thePrefs.GetCategory(i)->ac_regexpeval)
			bFound = RegularExpressionMatch(catExt, newfile->GetFileName()); // regular expression evaluation
		else {
			CString fullname(newfile->GetFileName());
			fullname.MakeLower();
			catExt.MakeLower();
			for (int iPos = 0; iPos >= 0;) {
				const CString &cmpExt(catExt.Tokenize(_T("|"), iPos));
				if (!cmpExt.IsEmpty())
					break;
				// HoaX_69: Allow wildcards in autocat string
				// thanks to: bluecow, khaos and SlugFiller
				if ((cmpExt.FindOneOf(_T("*?")) && ::PathMatchSpec(fullname, cmpExt)) // Use wildcards
					|| fullname.Find(cmpExt) >= 0) //simple string comparison
				{
					bFound = true;
					break;
				}
			}
		}
		if (bFound) {
			newfile->SetCategory((UINT)i);
			return;
		}
	}
}

void CDownloadQueue::ResetLocalServerRequests()
{
	m_dwNextTCPSrcReq = 0;
	m_localServerReqQueue.RemoveAll();

	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *pFile = filelist.GetNext(pos);
		EPartFileStatus uState = pFile->GetStatus();
		if (uState == PS_READY || uState == PS_EMPTY)
		{
			if (thePrefs.m_bDontSavePartOnReconnect)
				pFile->ResumeFile(false, false);
			else
				pFile->ResumeFile(false);
		}
		pFile->m_bLocalSrcReqQueued = false;
	}
}

void CDownloadQueue::RemoveLocalServerRequest(CPartFile *pFile)
{
	POSITION pos = m_localServerReqQueue.Find(pFile);
	if (pos) {
		m_localServerReqQueue.RemoveAt(pos);
		pFile->m_bLocalSrcReqQueued = false;
	}
}

void CDownloadQueue::ProcessLocalRequests()
{
	const DWORD curTick = ::GetTickCount();
	if (!m_localServerReqQueue.IsEmpty() && curTick >= m_dwNextTCPSrcReq) {
		CSafeMemFile dataTcpFrame(22);
		const int iMaxFilesPerTcpFrame = 15;
		int iFiles = 0;
		while (!m_localServerReqQueue.IsEmpty() && iFiles < iMaxFilesPerTcpFrame) {
			// find the file with the longest waiting time
			DWORD dwBestWaitTime = _UI32_MAX;
			POSITION posNextRequest = NULL;
			for (POSITION pos = m_localServerReqQueue.GetHeadPosition(); pos != NULL;) {
				POSITION pos2 = pos;
				CPartFile *cur_file = m_localServerReqQueue.GetNext(pos);
				if (cur_file->GetStatus() == PS_READY || cur_file->GetStatus() == PS_EMPTY) {
					uint8 nPriority = cur_file->GetDownPriority();
					if (nPriority > PR_HIGH) {
						ASSERT(0);
						nPriority = PR_HIGH;
					}

					if (cur_file->m_LastSearchTime + (PR_HIGH - nPriority) < dwBestWaitTime) {
						dwBestWaitTime = cur_file->m_LastSearchTime + (PR_HIGH - nPriority);
						posNextRequest = pos2;
					}
				} else {
					m_localServerReqQueue.RemoveAt(pos2);
					cur_file->m_bLocalSrcReqQueued = false;
					if (thePrefs.GetDebugSourceExchange())
						AddDebugLogLine(false, _T("SXSend: Local server source request for file \"%s\" not sent because of status '%s'"), (LPCTSTR)EscPercent(cur_file->GetFileName()), (LPCTSTR)EscPercent(cur_file->getPartfileStatus()));
				}
			}

			if (posNextRequest != NULL) {
				CPartFile *cur_file = m_localServerReqQueue.GetAt(posNextRequest);
				cur_file->m_bLocalSrcReqQueued = false;
				cur_file->m_LastSearchTime = curTick;
				m_localServerReqQueue.RemoveAt(posNextRequest);

				if (cur_file->IsLargeFile() && (theApp.serverconnect->GetCurrentServer() == NULL || !theApp.serverconnect->GetCurrentServer()->SupportsLargeFilesTCP())) {
					ASSERT(0);
					DebugLogError(_T("Large file (%s) on local request queue for server without support for large files"), (LPCTSTR)EscPercent(cur_file->GetFileName()));
					continue;
				}

				++iFiles;

				// create request packet
				CSafeMemFile smPacket;
				smPacket.WriteHash16(cur_file->GetFileHash());
				if (!cur_file->IsLargeFile())
					smPacket.WriteUInt32((uint32)(uint64)cur_file->GetFileSize());
				else {
					smPacket.WriteUInt32(0); // indicates that this is a large file and a uint64 follows
					smPacket.WriteUInt64(cur_file->GetFileSize());
				}

				uint8 byOpcode;
				if (thePrefs.IsCryptLayerEnabled() && theApp.serverconnect->GetCurrentServer() != NULL && theApp.serverconnect->GetCurrentServer()->SupportsGetSourcesObfuscation())
					byOpcode = OP_GETSOURCES_OBFU;
				else
					byOpcode = OP_GETSOURCES;

				Packet packet(smPacket, OP_EDONKEYPROT, byOpcode);
				if (thePrefs.GetDebugServerTCPLevel() > 0)
					Debug(_T(">>> Sending OP_GetSources%s(%2u/%2u); %s\n"), (byOpcode == OP_GETSOURCES) ? (LPCTSTR)EMPTY : (LPCTSTR)_T("_OBFU"), iFiles, iMaxFilesPerTcpFrame, (LPCTSTR)DbgGetFileInfo(cur_file->GetFileHash()));
				dataTcpFrame.Write(packet.GetPacket(), packet.GetRealPacketSize());

				if (thePrefs.GetDebugSourceExchange())
					AddDebugLogLine(false, _T("SXSend: Local server source request; File=\"%s\""), (LPCTSTR)EscPercent(cur_file->GetFileName()));
			}
		}

		int iSize = (int)dataTcpFrame.GetLength();
		if (iSize > 0) {
			// create one 'packet' which contains all buffered OP_GETSOURCES eD2K packets to be sent with one TCP frame
			// server credits: 16 * iMaxFilesPerTcpFrame + 1 = 241
			Packet *packet = new Packet(new char[iSize], (uint32)dataTcpFrame.GetLength(), true, false);
			dataTcpFrame.Seek(0, CFile::begin);
			dataTcpFrame.Read(packet->GetPacket(), iSize);
			theStats.AddUpDataOverheadServer(packet->size);
			theApp.serverconnect->SendPacket(packet);
		}

		// next TCP frame with up to 15 source requests is allowed to be sent in
		m_dwNextTCPSrcReq = curTick + SEC2MS(iMaxFilesPerTcpFrame * (16 + 4));
	}
}

void CDownloadQueue::SendLocalSrcRequest(CPartFile *sender)
{
	ASSERT(!m_localServerReqQueue.Find(sender));
	m_localServerReqQueue.AddTail(sender);
}

int CDownloadQueue::GetDownloadFilesStats(uint64 &rui64TotalFileSize
	, uint64 &rui64TotalLeftToTransfer
	, uint64 &rui64TotalAdditionalNeededSpace)
{
	int iActiveFiles = 0;
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		const CPartFile *cur_file = filelist.GetNext(pos);
		EPartFileStatus uState = cur_file->GetStatus();
		if (uState == PS_READY || uState == PS_EMPTY) {
			uint64 ui64LeftToTransfer = 0;
			uint64 ui64AdditionalNeededSpace = 0;
			cur_file->GetLeftToTransferAndAdditionalNeededSpace(ui64LeftToTransfer, ui64AdditionalNeededSpace);
			rui64TotalFileSize += (uint64)cur_file->GetFileSize();
			rui64TotalLeftToTransfer += ui64LeftToTransfer;
			rui64TotalAdditionalNeededSpace += ui64AdditionalNeededSpace;
			++iActiveFiles;
		}
	}
	return iActiveFiles;
}

///////////////////////////////////////////////////////////////////////////////
// CSourceHostnameResolveWnd

//It is safer to keep all message codes different (see also AsyncSocketEx.h and UserMsgs.h)
#define WM_HOSTNAMERESOLVED		(WM_USER+0x105)	// does not need to be placed in "UserMsgs.h"

BEGIN_MESSAGE_MAP(CSourceHostnameResolveWnd, CWnd)
	ON_MESSAGE(WM_HOSTNAMERESOLVED, OnHostnameResolved)
END_MESSAGE_MAP()

CSourceHostnameResolveWnd::CSourceHostnameResolveWnd()
	: m_aucHostnameBuffer()
{
}

CSourceHostnameResolveWnd::~CSourceHostnameResolveWnd()
{
	while (!m_toresolve.IsEmpty())
		delete m_toresolve.RemoveHead();
}

void CSourceHostnameResolveWnd::AddToResolve(const uchar *fileid, LPCSTR pszHostname, uint16 port)
{
	// double checking
	if (!theApp.downloadqueue->GetFileByID(fileid))
		return;

	bool bResolving = !m_toresolve.IsEmpty();

	Hostname_Entry *entry = new Hostname_Entry;
	md4cpy(entry->fileid, fileid);
	entry->strHostname = pszHostname;
	entry->port = port;
	m_toresolve.AddTail(entry);

	if (bResolving)
		return;

	memset(m_aucHostnameBuffer, 0, sizeof m_aucHostnameBuffer);
	if (!WSAAsyncGetHostByName(m_hWnd, WM_HOSTNAMERESOLVED, entry->strHostname, m_aucHostnameBuffer, sizeof m_aucHostnameBuffer)) {
		m_toresolve.RemoveTail();
		delete entry;
	}
}

LRESULT CSourceHostnameResolveWnd::OnHostnameResolved(WPARAM, LPARAM lParam)
{
	if (m_toresolve.IsEmpty())
		return TRUE;
	Hostname_Entry *resolved = m_toresolve.RemoveHead();
	if (WSAGETASYNCERROR(lParam) == 0) {
		unsigned iBufLen = WSAGETASYNCBUFLEN(lParam);
		if (iBufLen >= sizeof(HOSTENT)) {
			LPHOSTENT pHost = (LPHOSTENT)m_aucHostnameBuffer;
			if (pHost->h_length == 4 && pHost->h_addr_list && pHost->h_addr_list[0]) {
				uint32 nIP = ((LPIN_ADDR)(pHost->h_addr_list[0]))->s_addr;

				CPartFile *file = theApp.downloadqueue->GetFileByID(resolved->fileid);
				if (file) {
					CSafeMemFile sources(1 + 4 + 2);
					sources.WriteUInt8(1);
					sources.WriteUInt32(nIP);
					sources.WriteUInt16(resolved->port);
					sources.SeekToBegin();
					file->AddSources(&sources, 0, 0, false);
				}
			}
		}
	}
	delete resolved;

	while (!m_toresolve.IsEmpty()) {
		Hostname_Entry *entry = m_toresolve.GetHead();
		memset(m_aucHostnameBuffer, 0, sizeof m_aucHostnameBuffer);
		if (WSAAsyncGetHostByName(m_hWnd, WM_HOSTNAMERESOLVED, entry->strHostname, m_aucHostnameBuffer, sizeof m_aucHostnameBuffer) != 0)
			break;
		m_toresolve.RemoveHead();
		delete entry;
	}
	return TRUE;
}

bool CDownloadQueue::DoKademliaFileRequest() const
{
	return (::GetTickCount() >= m_lastkademliafilerequest + KADEMLIAASKTIME);
}

void CDownloadQueue::KademliaSearchFile(uint32 nSearchID, const Kademlia::CUInt128 *pcontactID, const Kademlia::CUInt128 *pServingBuddyID, uint8 type, uint32 ip, uint16 tcp, uint16 udp, uint32 dwServingBuddyIP, uint16 dwServingBuddyPort, uint8 byCryptOptions, const Kademlia::CUInt128* pIPv6, const Kademlia::CUInt128* pServingBuddyIPv6)
{
	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NatTraversal] KademliaSearchFile: searchID=%u, type=%u, ip=%s:%u, buddy=%s:%u\n"), nSearchID, (unsigned)type, (LPCTSTR)ipstr(CAddress(ip, false)), (unsigned)tcp, dwServingBuddyIP ? (LPCTSTR)ipstr(CAddress(dwServingBuddyIP, false)) : (LPCTSTR)_T("none"), (unsigned)dwServingBuddyPort);

	// DEBUG: Minimal log to track if this is called
	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[KadSearch] searchID=%u type=%u"), nSearchID, (unsigned)type);

	//Safety measure to make sure we are looking for these sources
	CPartFile *temp = GetFileByKadFileSearchID(nSearchID);
	if (!temp) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] KademliaSearchFile: PartFile NOT found for searchID=%u - REJECTED\n"), nSearchID);
		return;
	}
	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NatTraversal] KademliaSearchFile: PartFile found: %s\n"), (LPCTSTR)EscPercent(temp->GetFileName()));
	//Do we need more sources?
	if (temp->IsStopped() || temp->GetMaxSources() <= temp->GetSourceCount())
		return;

	uint32 ED2Kip = htonl(ip);
	if (theApp.ipfilter->IsFiltered(ED2Kip)) {
		if (thePrefs.GetLogFilteredIPs())
			AddDebugLogLine(false, _T("IPfiltered source IP=%s (%s) received from Kademlia"), (LPCTSTR)ipstr(ED2Kip), (LPCTSTR)EscPercent(theApp.ipfilter->GetLastHit()));
		return;
	}
	if ((ip == Kademlia::CKademlia::GetIPAddress() || ED2Kip == theApp.serverconnect->GetClientID()) && tcp == thePrefs.GetPort())
		return;
	CUpDownClient* ctemp = NULL;
	switch (type) {
	case 4:
	case 1:
		{
			//NonFirewalled users
			if (!tcp) {
				if (thePrefs.GetVerbose())
					AddDebugLogLine(false, _T("Ignored source (IP=%s) received from Kademlia, no TCP port received"), (LPCTSTR)ipstr(ip));
				return;
			}
			ctemp = new CUpDownClient(temp, tcp, ip, 0, 0, false); // IPv6-TODO: Check this
			ctemp->SetSourceFrom(SF_KADEMLIA);
			// not actually sent or needed for HighID sources
			ctemp->SetKadPort(udp);
			byte cID[16];
			pcontactID->ToByteArray(cID);
			ctemp->SetUserHash(cID);
		}
		break;
	case 2:
		//Don't use this type... Some clients will process it wrong.
		break;
	case 5:
	case 3:
			//This will be a firewalled client connected to Kad only.
		// if we are firewalled ourself and don't support NAT traversal, the source is useless to us
		if (theApp.IsFirewalled() && !thePrefs.IsEnableNatTraversal())
			break;
		if (pServingBuddyIPv6 != NULL && *pServingBuddyIPv6 != 0) {
			CAddress BuddyIPv6 = CAddress(*pServingBuddyIPv6, false);
			if (theApp.ipfilter->IsFiltered(BuddyIPv6)) {
				AddDebugLogLine(false, _T("Source with an IP-filtered serving buddy IP=%s (%s) received from Kademlia"), (LPCTSTR)ipstr(BuddyIPv6), (LPCTSTR)EscPercent(theApp.ipfilter->GetLastHit()));
				break;
			}
		} else if (theApp.ipfilter->IsFiltered(dwServingBuddyIP)) {
			if (thePrefs.GetLogFilteredIPs())
				AddDebugLogLine(false, _T("Source with an IP-filtered serving buddy IP=%s (%s) received from Kademlia"), (LPCTSTR)ipstr(dwServingBuddyIP), (LPCTSTR)EscPercent(theApp.ipfilter->GetLastHit()));
			break;
		}
		if (theApp.clientlist->IsBannedClient(ipstr(dwServingBuddyIP))) {
			if (thePrefs.GetLogBannedClients())
				AddDebugLogLine(false, _T("Source with a Banned serving buddy IP=%s received from Kademlia"), (LPCTSTR)ipstr(dwServingBuddyIP));
		} else {
			// Use a dummy LowID (1) for Kad-only firewalled sources. It marks "no ED2K ID" and keeps LowID semantics.
			ctemp = new CUpDownClient(temp, tcp, 1, 0, 0, false); // IPv6-TODO: Check this
			//The only reason we set the real IP is for when we get a callback
			//from this firewalled source, the compare method will match them.
			ctemp->SetSourceFrom(SF_KADEMLIA);
			ctemp->SetKadPort(udp);
			// Seed both user and connect endpoints for NAT-T/uTP rendezvous
			CAddress addr(CAddress(ED2Kip, false));
			ctemp->SetIP(addr);
			ctemp->SetConnectIP(addr); // Set ConnectIP so TryToConnect can send OP_REASKCALLBACKUDP to buddy
			// Do not force-enable NAT-T or direct UDP callback in test mode; learn from hello/options for natural flow
			ctemp->SetUDPPort(udp); // Track sender UDP/Kad port for pull response matching
			byte cID[16];
			pcontactID->ToByteArray(cID);
			ctemp->SetUserHash(cID);
			pServingBuddyID->ToByteArray(cID);
			ctemp->SetServingBuddyID(cID);
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal: KadResult] type=%u, buddy=%s:%u, sbid=%s, targetIP=%s\n"), (unsigned)type, (LPCTSTR)ipstr(CAddress(dwServingBuddyIP, false)), (unsigned)dwServingBuddyPort, (LPCTSTR)md4str(cID), (LPCTSTR)ipstr(ED2Kip));
			ctemp->SetServingBuddyIP(CAddress(dwServingBuddyIP, false));
			ctemp->SetServingBuddyPort(dwServingBuddyPort);
		}
		break;
	case 6:
		// firewalled source which supports direct UDP callback
		// If we are firewalled ourself and don't support NAT traversal, the source is useless to us
		if (theApp.IsFirewalled() && !thePrefs.IsEnableNatTraversal())
			break;

		if ((byCryptOptions & 0x08) == 0)
			DebugLogWarning(_T("Received Kad source type 6 (direct callback) which has the direct callback flag not set (%s)"), (LPCTSTR)ipstr(ED2Kip));
		else {
			// Use dummy LowID (1) for Kad-only direct-callback sources.
			ctemp = new CUpDownClient(temp, tcp, 1, 0, 0, false); // IPv6-TODO: Check this
			ctemp->SetSourceFrom(SF_KADEMLIA);
			ctemp->SetKadPort(udp);
			ctemp->SetUDPPort(udp); // Use remote UDP (Kad) port for matching pull responses
			// Seed both user and connect endpoints from ED2Kip (Kad contact's v4)
			CAddress addr(CAddress(ED2Kip, false));
			ctemp->SetIP(addr);
			ctemp->SetConnectIP(addr);
			// Do not force-enable NAT-T or direct UDP callback in test mode; learn from hello/options for natural flow
			byte cID[16];
			pcontactID->ToByteArray(cID);
			ctemp->SetUserHash(cID);
		}
	}

	if (ctemp != NULL) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] KadSearchResult: Created source object for type=%u, calling CheckAndAddSource\n"), (unsigned)type);
		if (pIPv6 != NULL && *pIPv6 != 0) {
			CAddress IPv6(CAddress::IPv6);
			pIPv6->ToByteArray((byte*)IPv6.Data());
			ctemp->SetIPv6(IPv6);
		}

		if (pServingBuddyIPv6 != NULL && *pServingBuddyIPv6 != 0) {
			CAddress BuddyIPv6(CAddress::IPv6);
			pServingBuddyIPv6->ToByteArray((byte*)BuddyIPv6.Data());
			ctemp->SetServingBuddyIP(BuddyIPv6);
		}
		// add encryption settings
		ctemp->SetConnectOptions(byCryptOptions);
		CheckAndAddSource(temp, ctemp, SF_KADEMLIA);
	} else {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] KadSearchResult: Source object NOT created (ctemp=NULL) for type=%u\n"), (unsigned)type);
	}
}

void CDownloadQueue::ExportPartMetFilesOverview()
{
	if (!GuardDownloadModelMutation(_T("CDownloadQueue::ExportPartMetFilesOverview")))
		return;

	if (!m_bStartupLoadCompleted) {
		AddDebugLogLine(DLP_LOW, false, _T("Skipping downloads.txt overview export before startup download load completed. active=%u loaded=%d tempDir=%Id/%Id\n"), m_bStartupLoadActive ? 1U : 0U, m_iStartupLoadCount, m_iStartupLoadTempDir, thePrefs.GetTempDirCount());
		return;
	}

	SPartMetOverviewExportTask *pTask = new SPartMetOverviewExportTask();
	pTask->lGeneration = InterlockedIncrement(&g_lPartMetOverviewExportGeneration);
	pTask->bSingleTempDir = thePrefs.GetTempDirCount() == 1;
	pTask->bShutdownFallback = false;
	pTask->bStartupLoadCompleted = m_bStartupLoadCompleted;
	pTask->strFileListPath = thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + _T("downloads.txt");
	pTask->strFormattedTime = CTime::GetCurrentTime().Format(_T("%c"));
	if (pTask->bSingleTempDir)
		pTask->strTempDir = thePrefs.GetTempDir();

	pTask->rows.reserve(static_cast<size_t>(filelist.GetCount()));
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		const CPartFile *pPartFile = filelist.GetNext(pos);
		if (pPartFile == NULL || pPartFile->GetStatus(true) == PS_COMPLETE)
			continue;

		SPartMetOverviewRow row;
		const CString &strPartFilePath(pPartFile->GetFilePath());
		TCHAR szNam[_MAX_FNAME];
		TCHAR szExt[_MAX_EXT];
		_tsplitpath(strPartFilePath, NULL, NULL, szNam, szExt);
		row.strPartFileName.Format(_T("%s%s"), szNam, szExt);
		row.strFullName = pPartFile->GetFullName();
		row.strED2kLink = pPartFile->GetED2kLink();
		pTask->rows.push_back(row);
	}

	if (theApp.IsClosing()) {
		pTask->bShutdownFallback = true;
		CSingleLock lock(&g_partMetOverviewExportLock, TRUE);
		WritePartMetFilesOverviewSnapshot(*pTask);
		delete pTask;
		return;
	}

	if (!QueuePartMetOverviewExportTask(pTask)) {
		pTask->bShutdownFallback = theApp.IsClosing();
		CSingleLock lock(&g_partMetOverviewExportLock, TRUE);
		WritePartMetFilesOverviewSnapshot(*pTask);
		delete pTask;
	}
}

bool CDownloadQueue::ProcessQueuedPartMetFilesOverviewExport()
{
	SPartMetOverviewExportTask *pTask = PopQueuedPartMetOverviewExportTask();
	if (pTask == NULL)
		return false;

	{
		CSingleLock lock(&g_partMetOverviewExportLock, TRUE);
		if (!pTask->bStartupLoadCompleted) {
			const CString strTmpFileListPath(GetPartMetOverviewTempPath(pTask->strFileListPath));
			(void)DeleteFileLongPath(strTmpFileListPath);
			TracePartMetOverviewExportResult(*pTask, _T("skipped"), _T("startup-load-incomplete"), strTmpFileListPath);
		} else if (pTask->lGeneration == InterlockedCompareExchange(&g_lPartMetOverviewExportGeneration, 0, 0))
			WritePartMetFilesOverviewSnapshot(*pTask);
		else {
			const CString strTmpFileListPath(GetPartMetOverviewTempPath(pTask->strFileListPath));
			(void)DeleteFileLongPath(strTmpFileListPath);
			TracePartMetOverviewExportResult(*pTask, _T("skipped"), _T("stale-generation"), strTmpFileListPath);
		}
	}

	delete pTask;
	return true;
}

void CDownloadQueue::OnConnectionState(bool bConnected)
{
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile *pPartFile = filelist.GetNext(pos);
		if (pPartFile->GetStatus() == PS_READY || pPartFile->GetStatus() == PS_EMPTY)
			pPartFile->SetActive(bConnected);
	}
}

CString CDownloadQueue::GetOptimalTempDir(UINT nCat, EMFileSize nFileSize)
{
	const INT_PTR iTempDirCnt = thePrefs.GetTempDirCount();
	// shortcut
	if (iTempDirCnt == 1)
		return thePrefs.GetTempDir();

	struct tmpDir
	{
		INT_PTR iDrive;		//-1 for UNC paths; 0 to 25 for drives from a: to z:
							//-2 for skipping the entry
		CString sShare;		//when iDrive is -1, this is a share name (\\server\share\)
		sint64 llFreeSpace;	//free space - (reserved minimum) - (collected space to complete all files on the drive)
	};
	CArray<tmpDir> aDrive;
	aDrive.SetSize(iTempDirCnt);

	// Step 1: collect free space on drives
	sint64 llHighestFreeSpace = 0;
	INT_PTR	nHighestFreeSpaceDrive = -1;
	for (INT_PTR i = 0; i < iTempDirCnt; ++i) {
		const CString &sDir(thePrefs.GetTempDir(i));
		INT_PTR iDrive = GetPathDriveNumber(sDir);
		ASSERT(iDrive >= 0 || ::PathIsUNC(sDir));
		if (iDrive < 0) //UNC path
			aDrive[i].sShare = GetShareName(sDir).MakeLower();
		//Free space is calculated per drive (or share), but several temp directories may be on one drive
		INT_PTR j;
		for (j = 0; j < i; ++j)
			if (iDrive == aDrive[j].iDrive && (iDrive >= 0 || aDrive[i].sShare == aDrive[j].sShare))
				break;

		if (i >= j) {
			aDrive[i].iDrive = iDrive;
			sint64 llSpace = GetFreeDiskSpaceX(sDir) - thePrefs.GetMinFreeDiskSpace();
			if (llSpace > llHighestFreeSpace) {
				nHighestFreeSpaceDrive = i;
				llHighestFreeSpace = llSpace;
			}
			aDrive[i].llFreeSpace = llSpace;
		} else
			aDrive[i].iDrive = -2; //data for this drive is already known
	}

	// Step 2: collect the space we need to download all files in the current queue
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;) {
		const CPartFile *pCurFile = filelist.GetNext(pos);
		switch (pCurFile->GetStatus(false)) {
		case PS_READY:
		case PS_EMPTY:
		case PS_WAITINGFORHASH:
		case PS_INSUFFICIENT:
			{
				sint64 llSpace = (uint64)pCurFile->GetFileSize() - (uint64)pCurFile->GetRealFileSize();
				if (llSpace > 0) {
					const CString &sPath(pCurFile->GetTmpPath());
					INT_PTR iDrive = GetPathDriveNumber(sPath);
					ASSERT(iDrive >= 0 || ::PathIsUNC(sPath));
					CString sUNC;
					if (iDrive < 0)
						sUNC = GetShareName(sPath).MakeLower();

					for (INT_PTR i = 0; i < iTempDirCnt; ++i) //look up for the same drive or share
						if (iDrive == aDrive[i].iDrive && (iDrive >= 0 || sUNC == aDrive[i].sShare)) {
							aDrive[i].llFreeSpace -= llSpace;
							break;
						}
				}
			}
		}
	}

	sint64 llHighestTotalSpace = 0;
	INT_PTR	nHighestTotalSpaceDir = -1;
	INT_PTR	nHighestFreeSpaceDir = -1;
	INT_PTR	nAnyAvailableDir = -1;
	// first round (0): on the same drive as incoming and enough space for all downloading
	// second round (1): enough space for all downloading
	// third round (2): largest actual free space
	for (INT_PTR i = 0; i < iTempDirCnt; ++i) {
		if (aDrive[i].iDrive == -2)
			continue;
		const sint64 llAvailableSpace = aDrive[i].llFreeSpace;

		// no condition can be met for a large file on a FAT volume
		if (nFileSize <= OLD_MAX_EMULE_FILE_SIZE || !IsFileOnFATVolume(thePrefs.GetTempDir(i))) {
			if (llAvailableSpace >= (sint64)(uint64)nFileSize) {
				// condition 0
				// needs to be the same drive and enough space
				if (GetPathDriveNumber(thePrefs.GetCatPath(nCat)) == aDrive[i].iDrive)
					return thePrefs.GetTempDir(i);	//this one is perfect

				// condition 1
				// needs to have enough space for downloading
				if (llAvailableSpace > llHighestTotalSpace) {
					llHighestTotalSpace = llAvailableSpace;
					nHighestTotalSpaceDir = i;
				}
			}
			// condition 2
			// the first one with the highest actually free space (see Step 1)
			if (i == nHighestFreeSpaceDrive && nHighestFreeSpaceDir < 0)
				nHighestFreeSpaceDir = i;
			// condition 3
			// any directory which can be used for this file (aka not FAT for large files)
			if (nAnyAvailableDir < 0)
				nAnyAvailableDir = i;
		}
	}

	if (nHighestTotalSpaceDir >= 0) // condition 0 was apparently too strong, take 1
		return thePrefs.GetTempDir(nHighestTotalSpaceDir);

	if (nHighestFreeSpaceDir >= 0) // condition 1 could not be met too, take 2
		return thePrefs.GetTempDir(nHighestFreeSpaceDir);

	// so was condition 2 and 3, take 4... wait there is no 3 - this must be a bug
	ASSERT(nAnyAvailableDir >= 0);
	return thePrefs.GetTempDir(max(nAnyAvailableDir, 0));
}

void CDownloadQueue::RefilterAllComments()
{
	for (POSITION pos = filelist.GetHeadPosition(); pos != NULL;)
		filelist.GetNext(pos)->RefilterFileComments();
}
