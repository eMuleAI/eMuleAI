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
#include "UploadQueue.h"
#include "Packets.h"
#include "opcodes.h"
#include "KnownFile.h"
#include "PartFile.h"
#include "ListenSocket.h"
#include "Exceptions.h"
#include "Scheduler.h"
#include "PerfLog.h"
#include "UploadBandwidthThrottler.h"
#include "ClientList.h"
#include "LastCommonRouteFinder.h"
#include "DownloadQueue.h"
#include "FriendList.h"
#include "Statistics.h"
#include "UpDownClient.h"
#include "SharedFileList.h"
#include "KnownFileList.h"
#include "ServerConnect.h"
#include "ClientCredits.h"
#include "Server.h"
#include "ServerList.h"
#include "WebServer.h"
#include "emuledlg.h"
#include "ServerWnd.h"
#include "TransferDlg.h"
#include "SearchDlg.h"
#include "StatisticsDlg.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "Kademlia/Kademlia/Prefs.h"
#include "Log.h"
#include "collection.h"
#include "eMuleAI/UtpSocket.h"
#include "eMuleAI/QuicNatSocket.h"
#include "SearchList.h"
#include "MenuCmds.h"
#include "KadContactListCtrl.h"
#include "KadSearchListCtrl.h"
#include "Kademlia/Routing/RoutingZone.h"
#include "Kademlia/Kademlia/SearchManager.h" // Needed for the relocation of the JumpStart call!!!
#include "ClientUDPSocket.h"
#include "NetworkInfoDlg.h"
#include <cstring>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	constexpr DWORD kHighBandwidthBaseRefillUnderfillSeconds = 2;
	constexpr DWORD kHighBandwidthElasticUnderfillSeconds = 10;
	constexpr DWORD kUploadRequestAbuseNoRequestWindowMs = SEC2MS(4 * 60 * 60);
	constexpr DWORD kUploadRequestAbuseQueueWindowMs = MIN2MS(10);
	constexpr DWORD kUploadRequestAbuseCleanupIntervalMs = MIN2MS(1);
	constexpr UINT kUploadRequestAbuseNoRequestBanThreshold = 8;
	constexpr UINT kUploadRequestAbuseQueueBanThreshold = 12;
	constexpr UINT kUploadRequestAbuseCounterGuardThreshold = 3;
	constexpr UINT kUploadRequestAbuseHashRotationHashThreshold = 3;
	constexpr UINT kUploadRequestAbuseHashRotationStrikeThreshold = 8;
	constexpr uint32 kHighBandwidthUploadTargetBufferSeconds = 8u;
	constexpr UINT kHighBandwidthUploadMaxBufferBlocks = 64u;

	uint64 ResolveUploadSessionTransferLimitBytes(const CKnownFile *file)
	{
		switch (thePrefs.GetUploadSessionTransferLimitMode()) {
		case ESessionTransferLimitMode::PercentOfFile:
			if (file == NULL)
				return 0;
			return (static_cast<uint64>(file->GetFileSize()) * thePrefs.GetUploadSessionTransferLimitPercent() + 99ui64) / 100ui64;
		case ESessionTransferLimitMode::AbsoluteMiB:
			return static_cast<uint64>(thePrefs.GetUploadSessionTransferLimitMiB()) * 1024ui64 * 1024ui64;
		default:
			return 0;
		}
	}

	bool GuardUploadModelMutation(LPCTSTR pszEntryPoint)
	{
		return theApp.GuardModelMutation(CemuleApp::ModelMutationUploadQueue, pszEntryPoint);
	}

	uint32 GetHighBandwidthProductiveUploadRateThreshold(uint32 uTargetDatarate)
	{
		return max(1024u, static_cast<uint32>(static_cast<float>(uTargetDatarate) * thePrefs.GetHighBandwidthSlowUploadThresholdFactor()));
	}

	bool ShouldRotateLimitedHighBandwidthUploadSession(bool bNeedsReplacement, bool bHighBandwidthUnderfilled, uint32 uUploadRateBytesPerSec, uint32 uProductiveRateThresholdBytesPerSec)
	{
		return bNeedsReplacement && (!bHighBandwidthUnderfilled || uUploadRateBytesPerSec < uProductiveRateThresholdBytesPerSec);
	}

	template <typename TMap, typename TKey>
	void EraseUploadWaitingIndexEntries(TMap& mapIndex, const TKey& key, DWORD uRuntimeID)
	{
		std::pair<typename TMap::iterator, typename TMap::iterator> range = mapIndex.equal_range(key);
		for (typename TMap::iterator it = range.first; it != range.second;) {
			if (it->second == uRuntimeID)
				mapIndex.erase(it++);
			else
				++it;
		}
	}

	bool HasPendingUploadBlockRequests(UploadingToClient_Struct *pUpClientStruct)
	{
		if (pUpClientStruct == NULL)
			return true;

		CSingleLock lockBlockLists(&pUpClientStruct->m_csBlockListsLock, TRUE);
		ASSERT(lockBlockLists.IsLocked());
		// Done blocks are upload history, but pending disk I/O is still local work.
		return !pUpClientStruct->m_BlockRequests_queue.IsEmpty() || ::InterlockedCompareExchange(&pUpClientStruct->m_nPendingIOBlocks, 0, 0) > 0;
	}

	bool HasActiveNatTraversalDownloadContext(const CUpDownClient *client)
	{
		if (client == NULL || client->GetRequestFile() == NULL)
			return false;

		switch (client->GetDownloadState()) {
		case DS_DOWNLOADING:
		case DS_ONQUEUE:
		case DS_CONNECTED:
		case DS_CONNECTING:
		case DS_WAITCALLBACK:
		case DS_WAITCALLBACKKAD:
		case DS_REQHASHSET:
			return true;
		default:
			return false;
		}
	}


	uint32 GetAdaptiveTcpUploadSendBufferBytes(uint32 uTargetPerSlotBytesPerSec)
	{
		const uint32 uMinimumBufferBytes = 1024u * 1024u;
		const uint32 uMaximumBufferBytes = 32u * 1024u * 1024u;
		if (uTargetPerSlotBytesPerSec == 0)
			return uMinimumBufferBytes;
		const uint64 uTargetBytes = static_cast<uint64>(uTargetPerSlotBytesPerSec) * kHighBandwidthUploadTargetBufferSeconds;
		if (uTargetBytes < uMinimumBufferBytes)
			return uMinimumBufferBytes;
		if (uTargetBytes > uMaximumBufferBytes)
			return uMaximumBufferBytes;
		return static_cast<uint32>(uTargetBytes);
	}

	UINT GetHighBandwidthUploadBufferBlockCount(uint32 uTargetPerSlotBytesPerSec, uint32 uClientDatarateBytesPerSec)
	{
		if (uTargetPerSlotBytesPerSec == 0)
			return 1u;

		uint64 uTargetBytes = static_cast<uint64>(uTargetPerSlotBytesPerSec) * kHighBandwidthUploadTargetBufferSeconds;
		UINT uBlocks = static_cast<UINT>((uTargetBytes + EMBLOCKSIZE - 1u) / EMBLOCKSIZE);
		if (uBlocks == 0)
			uBlocks = 1u;
		if (uClientDatarateBytesPerSec >= uTargetPerSlotBytesPerSec && uBlocks < 5u)
			uBlocks = 5u;
		else if (uClientDatarateBytesPerSec >= max(1u, uTargetPerSlotBytesPerSec / 2u) && uBlocks < 3u)
			uBlocks = 3u;
		return min(uBlocks, kHighBandwidthUploadMaxBufferBlocks);
	}


}

static uint32 i1sec, i2sec, i5sec, i60sec;
static UINT s_uSaveStatistics = 0;
static uint32 igraph, istats;
static uint32 m_uAutoQuerySFCounter;
static uint32 m_uTenMinCounter;

CUploadQueue::SUploadRequestAbuseState::SUploadRequestAbuseState()
	: m_dwWindowUntil(0)
	, m_uStrikes(0)
{
}

CUploadQueue::SUploadRequestAbuseIPState::SUploadRequestAbuseIPState()
	: m_dwWindowUntil(0)
	, m_uTotalStrikes(0)
	, m_setHashKeys()
{
}

SClientItemId::SClientItemId()
	: m_uRuntimeID(0)
{
}

void SClientItemId::Clear()
{
	m_uRuntimeID = 0;
}

bool SClientItemId::IsValid() const
{
	return m_uRuntimeID != 0;
}

bool SClientItemId::operator<(const SClientItemId& other) const
{
	return m_uRuntimeID < other.m_uRuntimeID;
}

bool SClientItemId::operator==(const SClientItemId& other) const
{
	return m_uRuntimeID == other.m_uRuntimeID;
}

SUploadBlockRequestKey::SUploadBlockRequestKey()
	: m_uStartOffset(0)
	, m_uEndOffset(0)
{
	std::memset(m_abyFileID, 0, sizeof(m_abyFileID));
}

SUploadBlockRequestKey::SUploadBlockRequestKey(uint64 uStartOffset, uint64 uEndOffset, const uchar *pFileID)
	: m_uStartOffset(uStartOffset)
	, m_uEndOffset(uEndOffset)
{
	if (pFileID != NULL)
		std::memcpy(m_abyFileID, pFileID, sizeof(m_abyFileID));
	else
		std::memset(m_abyFileID, 0, sizeof(m_abyFileID));
}

bool SUploadBlockRequestKey::operator<(const SUploadBlockRequestKey& other) const
{
	const int iHashCompare = std::memcmp(m_abyFileID, other.m_abyFileID, sizeof(m_abyFileID));
	if (iHashCompare != 0)
		return iHashCompare < 0;
	if (m_uStartOffset != other.m_uStartOffset)
		return m_uStartOffset < other.m_uStartOffset;
	return m_uEndOffset < other.m_uEndOffset;
}

CUploadQueue::SUploadTimerMaintenanceJobState::SUploadTimerMaintenanceJobState()
	: m_bPending(false)
	, m_dwQueuedTick(0)
{
}

CUploadQueue::SHighBandwidthUploadRetryCooldownState::SHighBandwidthUploadRetryCooldownState()
	: m_dwCooldownUntil(0)
{
}

CUploadQueue::SWaitingRankRequest::SWaitingRankRequest()
	: m_uRuntimeID(0)
	, m_uScore(0)
	, m_uRank(1)
{
}

CUploadQueue::SUploadWaitingHashKey::SUploadWaitingHashKey()
{
	std::memset(m_abyHash, 0, sizeof(m_abyHash));
}

CUploadQueue::SUploadWaitingHashKey::SUploadWaitingHashKey(const uchar* pHash)
{
	if (pHash != NULL)
		std::memcpy(m_abyHash, pHash, sizeof(m_abyHash));
	else
		std::memset(m_abyHash, 0, sizeof(m_abyHash));
}

bool CUploadQueue::SUploadWaitingHashKey::operator<(const SUploadWaitingHashKey& other) const
{
	return std::memcmp(m_abyHash, other.m_abyHash, sizeof(m_abyHash)) < 0;
}

bool CUploadQueue::SUploadWaitingHashKey::operator==(const SUploadWaitingHashKey& other) const
{
	return std::memcmp(m_abyHash, other.m_abyHash, sizeof(m_abyHash)) == 0;
}

CUploadQueue::SUploadWaitingEndpointKey::SUploadWaitingEndpointKey()
	: m_ip()
	, m_uPort(0)
{
}

CUploadQueue::SUploadWaitingEndpointKey::SUploadWaitingEndpointKey(const CAddress& ip, uint16 uPort)
	: m_ip(ip)
	, m_uPort(uPort)
{
}

bool CUploadQueue::SUploadWaitingEndpointKey::operator<(const SUploadWaitingEndpointKey& other) const
{
	if (m_ip < other.m_ip)
		return true;
	if (other.m_ip < m_ip)
		return false;
	return m_uPort < other.m_uPort;
}

bool CUploadQueue::SUploadWaitingEndpointKey::operator==(const SUploadWaitingEndpointKey& other) const
{
	return m_ip == other.m_ip && m_uPort == other.m_uPort;
}

CUploadQueue::SUploadWaitingIdPortKey::SUploadWaitingIdPortKey()
	: m_uUserID(0)
	, m_uPort(0)
{
}

CUploadQueue::SUploadWaitingIdPortKey::SUploadWaitingIdPortKey(uint32 uUserID, uint16 uPort)
	: m_uUserID(uUserID)
	, m_uPort(uPort)
{
}

bool CUploadQueue::SUploadWaitingIdPortKey::operator<(const SUploadWaitingIdPortKey& other) const
{
	if (m_uUserID != other.m_uUserID)
		return m_uUserID < other.m_uUserID;
	return m_uPort < other.m_uPort;
}

bool CUploadQueue::SUploadWaitingIdPortKey::operator==(const SUploadWaitingIdPortKey& other) const
{
	return m_uUserID == other.m_uUserID && m_uPort == other.m_uPort;
}

CUploadQueue::SUploadWaitingLowIdKey::SUploadWaitingLowIdKey()
	: m_uUserID(0)
	, m_uServerIP(0)
	, m_uServerPort(0)
{
}

CUploadQueue::SUploadWaitingLowIdKey::SUploadWaitingLowIdKey(uint32 uUserID, uint32 uServerIP, uint16 uServerPort)
	: m_uUserID(uUserID)
	, m_uServerIP(uServerIP)
	, m_uServerPort(uServerPort)
{
}

bool CUploadQueue::SUploadWaitingLowIdKey::operator<(const SUploadWaitingLowIdKey& other) const
{
	if (m_uUserID != other.m_uUserID)
		return m_uUserID < other.m_uUserID;
	if (m_uServerIP != other.m_uServerIP)
		return m_uServerIP < other.m_uServerIP;
	return m_uServerPort < other.m_uServerPort;
}

bool CUploadQueue::SUploadWaitingLowIdKey::operator==(const SUploadWaitingLowIdKey& other) const
{
	return m_uUserID == other.m_uUserID && m_uServerIP == other.m_uServerIP && m_uServerPort == other.m_uServerPort;
}

CUploadQueue::SUploadWaitingIndexSnapshot::SUploadWaitingIndexSnapshot()
	: m_bHasHash(false)
	, m_hashKey()
	, m_aEndpointKeys()
	, m_aUDPKeys()
	, m_aIdPortKeys()
	, m_bHasLowId(false)
	, m_lowIdKey()
	, m_aIPKeys()
{
}

bool CUploadQueue::SUploadWaitingIndexSnapshot::operator==(const SUploadWaitingIndexSnapshot& other) const
{
	return m_bHasHash == other.m_bHasHash
		&& (!m_bHasHash || m_hashKey == other.m_hashKey)
		&& m_aEndpointKeys == other.m_aEndpointKeys
		&& m_aUDPKeys == other.m_aUDPKeys
		&& m_aIdPortKeys == other.m_aIdPortKeys
		&& m_bHasLowId == other.m_bHasLowId
		&& (!m_bHasLowId || m_lowIdKey == other.m_lowIdKey)
		&& m_aIPKeys == other.m_aIPKeys;
}


#define HIGHSPEED_UPLOADRATE_START	(500*1024)
#define HIGHSPEED_UPLOADRATE_END	(300*1024)


CUploadQueue::CUploadQueue()
	: datarate()
	, friendDatarate()
	, successfullupcount()
	, failedupcount()
	, totaluploadtime()
	, m_nLastStartUpload()
	, m_dwRemovedClientByScore(::GetTickCount())
	, m_imaxscore()
	, m_dwLastCalculatedAverageCombinedFilePrioAndCredit()
	, m_fAverageCombinedFilePrioAndCredit()
	, m_iHighestNumberOfFullyActivatedSlotsSinceLastCall()
	, m_MaxActiveClients()
	, m_MaxActiveClientsShortTime()
	, m_lastCalculatedDataRateTick()
	, m_average_dr_sum()
	, m_dwLastUploadRequestAbuseCleanupTick()
	, m_dwHighBandwidthUploadUnderfillSince()
	, m_dwLastHighBandwidthSlowRecycleTick()
	, m_dwLastResortedUploadSlots()
	, m_uNextUploadTimerMaintenanceJob(0)
	, m_bStatisticsWaitingListDirty(true)
	, m_bHasActiveUploads(false)
	, m_posMaxClientScoreRecalc(NULL)
	, m_posAverageCombinedFilePrioAndCreditRecalc(NULL)
	, m_uWaitingListGeneration(0)
	, m_uMaxClientScoreRecalcGeneration(0)
	, m_uAverageCombinedFilePrioAndCreditGeneration(0)
	, m_iMaxClientScoreRecalcProcessed(0)
	, m_iAverageCombinedFilePrioAndCreditProcessed(0)
	, m_uMaxClientScoreRecalcMax(0)
	, m_fAverageCombinedFilePrioAndCreditSum(0.0)
	, m_bMaxClientScoreRecalcActive(false)
	, m_bAverageCombinedFilePrioAndCreditRecalcActive(false)
	, m_bAverageCombinedFilePrioAndCreditValid(false)
	, m_posWaitingRankRecalc(NULL)
	, m_uWaitingRankRecalcGeneration(0)
	, m_iWaitingRankRecalcProcessed(0)
	, m_bWaitingRankRecalcActive(false)
	, m_posBestClientRecalc(NULL)
	, m_uBestClientRecalcGeneration(0)
	, m_iBestClientRecalcProcessed(0)
	, m_uBestClientRuntimeID(0)
	, m_uBestLowClientRuntimeID(0)
	, m_dwBestClientCacheTick(0)
	, m_uBestClientScore(0)
	, m_uBestLowClientScore(0)
	, m_uBestCooldownClientRuntimeID(0)
	, m_uBestCooldownClientScore(0)
	, m_bBestClientRecalcActive(false)
	, m_bBestClientCacheValid(false)
	, m_uPruneWaitersForMissingSharedFilesIndex(0)
	, m_bPruneWaitersForMissingSharedFilesActive(false)
{
	VERIFY((h_timer = ::SetTimer(NULL, 0, SEC2MS(1)/10, UploadTimer)) != 0);
	if (thePrefs.GetVerbose() && !h_timer)
		AddDebugLogLine(true, _T("Failed to create 'upload queue' timer - %s"), (LPCTSTR)EscPercent(GetErrorMessage(::GetLastError())));
	i1sec = 0;
	i60sec = 0;
	i2sec = 0;
}

CUploadQueue::~CUploadQueue()
{
	if (h_timer)
		::KillTimer(0, h_timer);
}

/**
 * Find the highest ranking client in the waiting queue, and return it.
 *
 * Low id client are ranked as lowest possible, unless they are currently connected.
 * A low id client that is not connected, but would have been ranked highest if it
 * had been connected, gets a flag set. This flag means that the client should be
 * allowed to get an upload slot immediately once it connects.
 *
 * @return address of the highest ranking client.
 */
CUpDownClient* CUploadQueue::FindBestClientInQueueExact(bool bSkipUploadBanned, bool bAllowCleanup)
{
	uint32 bestscore = 0;
	uint32 bestlowscore = 0;
	uint32 bestcooldownscore = 0;
	CUpDownClient *newclient = NULL;
	CUpDownClient *lowclient = NULL;
	CUpDownClient *cooldownclient = NULL;
	const DWORD curTick = ::GetTickCount();
	const bool bRespectHighBandwidthCooldown = IsHighBandwidthUploadPolicyActive();
	const bool bAllowHighBandwidthCooldownProbe = bRespectHighBandwidthCooldown && ShouldProbeHighBandwidthUploadCooldownCandidate(curTick);

	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		CUpDownClient *cur_client = waitinglist.GetNext(pos);
		ASSERT(cur_client->GetLastUpRequest());
		if ((curTick >= cur_client->GetLastUpRequest() + MAX_PURGEQUEUETIME) || !theApp.sharedfiles->GetFileByID(cur_client->GetUploadFileID())) {
			if (bAllowCleanup) {
				cur_client->ClearWaitStartTime();
				RemoveFromWaitingQueue(pos2, true);
			}
			continue;
		}

		if (cur_client->GetSendIP())
			cur_client->SendIPChange();

		if (bSkipUploadBanned && cur_client->IsBadClient() && cur_client->m_uPunishment == P_UPLOADBAN)
			continue;

		uint32 cur_score = cur_client->GetScore(false);
		const bool bCanStartNow = !cur_client->HasLowID() || (cur_client->socket && cur_client->socket->IsConnected());
		if (bCanStartNow) {
			if (bRespectHighBandwidthCooldown && IsHighBandwidthUploadRetryCooldownActive(cur_client, curTick)) {
				if (bAllowHighBandwidthCooldownProbe && CanProbeHighBandwidthUploadCooldownClient(cur_client, curTick) && cur_score > bestcooldownscore) {
					bestcooldownscore = cur_score;
					cooldownclient = cur_client;
				}
				continue;
			}
			if (cur_score > bestscore) {
				bestscore = cur_score;
				newclient = cur_client;
			}
		} else if (!cur_client->m_bAddNextConnect && cur_score > bestlowscore) {
			bestlowscore = cur_score;
			lowclient = cur_client;
		}
	}

	if (bAllowHighBandwidthCooldownProbe && newclient == NULL && cooldownclient != NULL && (lowclient == NULL || bestcooldownscore >= bestlowscore)) {
		newclient = cooldownclient;
		bestscore = bestcooldownscore;
	}

	if (lowclient && bestlowscore > bestscore)
		lowclient->m_bAddNextConnect = true;

	return newclient;
}

CUpDownClient* CUploadQueue::FindBestClientInQueue()
{
	if (!GuardUploadModelMutation(_T("CUploadQueue::FindBestClientInQueue")))
		return NULL;

	if (waitinglist.GetCount() <= 2048)
		return FindBestClientInQueueExact(true, true);

	if (m_bBestClientCacheValid && m_uBestClientRecalcGeneration == m_uWaitingListGeneration && ::GetTickCount() - m_dwBestClientCacheTick <= SEC2MS(2)) {
		const DWORD curTick = ::GetTickCount();
		CUpDownClient *cached = ResolveWaitingClientRuntimeID(m_uBestClientRuntimeID);
		if (cached != NULL
			&& !(cached->IsBadClient() && cached->m_uPunishment == P_UPLOADBAN)
			&& (!IsHighBandwidthUploadPolicyActive() || !cached->HasLowID() || (cached->socket && cached->socket->IsConnected()))
			&& (!IsHighBandwidthUploadPolicyActive() || !IsHighBandwidthUploadRetryCooldownActive(cached, curTick) || CanProbeHighBandwidthUploadCooldownClient(cached, curTick)))
		{
			return cached;
		}
		m_bBestClientCacheValid = false;
		m_uBestClientRuntimeID = 0;
	}

	UINT uProcessed = 0;
	INT_PTR iRemaining = 0;
	if (!ProcessBestClientRecalculationChunk(256, uProcessed, iRemaining) && iRemaining > 0)
		MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceBestClientInQueue);

	if (m_bBestClientCacheValid && m_uBestClientRecalcGeneration == m_uWaitingListGeneration)
		return ResolveWaitingClientRuntimeID(m_uBestClientRuntimeID);

	return NULL;
}

void CUploadQueue::InsertInUploadingList(CUpDownClient *newclient, bool bNoLocking)
{
	if (!GuardUploadModelMutation(_T("CUploadQueue::InsertInUploadingList")))
		return;

	UploadingToClient_Struct *pNewClientUploadStruct = new UploadingToClient_Struct;
	pNewClientUploadStruct->m_pClient = newclient;
	InsertInUploadingList(pNewClientUploadStruct, bNoLocking);
}

void CUploadQueue::InsertInUploadingList(UploadingToClient_Struct *pNewClientUploadStruct, bool bNoLocking)
{
	if (!GuardUploadModelMutation(_T("CUploadQueue::InsertInUploadingListStruct"))) {
		delete pNewClientUploadStruct;
		return;
	}

	//Lets make sure any client that is added to the list has this flag reset!
	pNewClientUploadStruct->m_pClient->m_bAddNextConnect = false;
	// Add it last
	theApp.uploadBandwidthThrottler->AddToStandardList(uploadinglist.GetCount(), pNewClientUploadStruct->m_pClient->GetFileUploadSocket());

	if (!bNoLocking)
		m_csUploadListMainThrdWriteOtherThrdsRead.Lock();
	uploadinglist.AddTail(pNewClientUploadStruct);
	if (!bNoLocking)
		m_csUploadListMainThrdWriteOtherThrdsRead.Unlock();

	pNewClientUploadStruct->m_pClient->SetSlotNumber((UINT)uploadinglist.GetCount());
	theApp.QueueUploadClientRowsChanged(pNewClientUploadStruct->m_pClient, CemuleApp::UploadClientUiTargetUploadList);
	theApp.QueueUploadListChangedEvent(CemuleApp::UploadClientUiTargetUploadList, _T("upload-slot-assignment"));
}

bool CUploadQueue::AddUpNextClient(LPCTSTR pszReason, CUpDownClient *directadd)
{
	if (!GuardUploadModelMutation(_T("CUploadQueue::AddUpNextClient")))
		return false;

	CUpDownClient *newclient = directadd;
	// select next client or use given client
	if (newclient == NULL) {
		newclient = FindBestClientInQueue();
		if (newclient == NULL)
			return false;
	}

	// For direct adds, never start upload for upload-banned clients
	if (directadd != NULL && newclient->IsBadClient() && newclient->m_uPunishment == P_UPLOADBAN)
		return false;

	RemoveFromWaitingQueue(newclient, true);

	if (!thePrefs.TransferFullChunks())
		UpdateMaxClientScore(); // refresh score caching, now that the highest score is removed

	if (IsDownloading(newclient))
		return false;

	if (pszReason && thePrefs.GetLogUlDlEvents())
		AddDebugLogLine(false, _T("Adding client to upload list: %s Client: %s"), pszReason, (LPCTSTR)EscPercent(newclient->DbgGetClientInfo()));

	if (newclient->HasCollectionUploadSlot() && directadd == NULL) {
		if (thePrefs.GetLogNatTraversalEvents())
			AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] Clearing stale collection upload slot before AddUpNextClient for %s"), (LPCTSTR)EscPercent(newclient->DbgGetClientInfo()));
		newclient->SetCollectionUploadSlot(false);
	}

	// tell the client that we are now ready to upload
	if (!newclient->socket || !newclient->socket->IsConnected() || !newclient->CheckHandshakeFinished()) {
		// Keep uploader in CONNECTING until socket and handshake are both ready.
		// Setting US_UPLOADING too early can leave peers in stale On Queue state.
		newclient->SetUploadState(US_CONNECTING);
		if (!newclient->TryToConnect(true))
			return false;
	} else {
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP_AcceptUploadReq", newclient);
		Packet *packet = new Packet(OP_ACCEPTUPLOADREQ, 0);
		theStats.AddUpDataOverheadFileRequest(packet->size);
		newclient->SendPacket(packet);
		newclient->SetUploadState(US_UPLOADING);
	}
	newclient->SetUpStartTime();
	newclient->ResetSessionUp();
	ClearHighBandwidthUploadRetryCooldown(newclient, ::GetTickCount());
	newclient->ResetHighBandwidthSlowUploadTracking();

	InsertInUploadingList(newclient, false);

	m_nLastStartUpload = ::GetTickCount();

	// statistic
	CKnownFile *reqfile = theApp.sharedfiles->GetFileByID((uchar*)newclient->GetUploadFileID());
	if (reqfile)
		reqfile->statistic.AddAccepted();

	theApp.QueueUploadClientRowsChanged(newclient, CemuleApp::UploadClientUiTargetUploadList);
	theApp.QueueUploadBandwidthSnapshotEvent(_T("upload-client-added"));

	return true;
}

bool CUploadQueue::TryAdmitQueuedBlockRequestClient(CUpDownClient *client)
{
	if (!GuardUploadModelMutation(_T("CUploadQueue::TryAdmitQueuedBlockRequestClient")))
		return false;

	const DWORD curTick = ::GetTickCount();
	if (!IsHighBandwidthUploadPolicyActive() || client == NULL || client->GetUploadState() != US_ONUPLOADQUEUE)
		return false;
	if (!IsOnUploadQueue(client) || IsDownloading(client))
		return false;
	if (client->IsBadClient() && client->m_uPunishment == P_UPLOADBAN)
		return false;
	if (!client->CanProbeHighBandwidthSlowUploadCooldown(curTick))
		return false;
	if (client->socket == NULL || !client->socket->IsConnected() || !client->CheckHandshakeFinished())
		return false;

	ClearHighBandwidthUploadRetryCooldown(client, curTick);
	if (!ForceNewClient(true))
		return false;

	return AddUpNextClient(_T("Queued block request"), client) && client->GetUploadState() == US_UPLOADING;
}

void CUploadQueue::UpdateActiveClientsInfo(DWORD curTick)
{
	if (!GuardUploadModelMutation(_T("CUploadQueue::UpdateActiveClientsInfo")))
		return;

	// Save number of active clients for statistics
	INT_PTR tempHighest = theApp.uploadBandwidthThrottler->GetHighestNumberOfFullyActivatedSlotsSinceLastCallAndReset();


	m_iHighestNumberOfFullyActivatedSlotsSinceLastCall = min(tempHighest, uploadinglist.GetCount() + 1);

	// save some data about number of fully active clients
	int tempMaxRemoved = 0;
	while (!activeClients_tick_list.IsEmpty() && !activeClients_list.IsEmpty() && curTick >= activeClients_tick_list.GetHead() + SEC2MS(20)) {
		activeClients_tick_list.RemoveHead();
		int removed = activeClients_list.RemoveHead();

		if (removed > tempMaxRemoved)
			tempMaxRemoved = removed;
	}

	activeClients_list.AddTail((int)m_iHighestNumberOfFullyActivatedSlotsSinceLastCall);
	activeClients_tick_list.AddTail(curTick);

	if (activeClients_tick_list.GetCount() > 1) {
		INT_PTR tempMaxActiveClients = m_iHighestNumberOfFullyActivatedSlotsSinceLastCall;
		INT_PTR tempMaxActiveClientsShortTime = m_iHighestNumberOfFullyActivatedSlotsSinceLastCall;
		POSITION activeClientsTickPos = activeClients_tick_list.GetTailPosition();
		POSITION activeClientsListPos = activeClients_list.GetTailPosition();
		while (activeClientsListPos != NULL && (tempMaxRemoved > tempMaxActiveClients && tempMaxRemoved >= m_MaxActiveClients || curTick < activeClients_tick_list.GetAt(activeClientsTickPos) + SEC2MS(10))) {
			DWORD activeClientsTickSnapshot = activeClients_tick_list.GetAt(activeClientsTickPos);
			int activeClientsSnapshot = activeClients_list.GetAt(activeClientsListPos);

			if (activeClientsSnapshot > tempMaxActiveClients)
				tempMaxActiveClients = activeClientsSnapshot;

			if (activeClientsSnapshot > tempMaxActiveClientsShortTime && curTick < activeClientsTickSnapshot + SEC2MS(10))
				tempMaxActiveClientsShortTime = activeClientsSnapshot;

			activeClients_tick_list.GetPrev(activeClientsTickPos);
			activeClients_list.GetPrev(activeClientsListPos);
		}

		if (tempMaxRemoved >= m_MaxActiveClients || tempMaxActiveClients > m_MaxActiveClients)
			m_MaxActiveClients = tempMaxActiveClients;

		m_MaxActiveClientsShortTime = tempMaxActiveClientsShortTime;
	} else {
		m_MaxActiveClients = m_iHighestNumberOfFullyActivatedSlotsSinceLastCall;
		m_MaxActiveClientsShortTime = m_iHighestNumberOfFullyActivatedSlotsSinceLastCall;
	}
}

/**
 * Maintenance method for the uploading slots. It adds and removes clients to the
 * uploading list. It also makes sure that all the uploading slots' Sockets always have
 * enough packets in their queues, etc.
 *
 * This method is called approximately once every 100 milliseconds.
 */
void CUploadQueue::Process()
{
	if (!GuardUploadModelMutation(_T("CUploadQueue::Process")))
		return;

	const DWORD curTick = ::GetTickCount();
	const DWORD staleConnectingSlotTimeout = CONNECTION_TIMEOUT + SEC2MS(20);
	bool bHasActiveUploads = false;
	ReclaimRetiredUploadClientStructs();
	UpdateActiveClientsInfo(curTick);
	UpdateHighBandwidthUploadUnderfillState(curTick);
	PurgeExpiredHighBandwidthUploadRetryCooldowns(curTick);
	PurgeExpiredUploadRequestAbuseTracking(curTick);

	const bool bHighBandwidthPolicyActive = IsHighBandwidthUploadPolicyActive();
	const bool bHighBandwidthIdleRecycleAllowed = bHighBandwidthPolicyActive
		&& m_dwHighBandwidthUploadUnderfillSince != 0
		&& (int)(curTick - m_dwHighBandwidthUploadUnderfillSince) >= (int)SEC2MS(kHighBandwidthBaseRefillUnderfillSeconds);
	bool bHighBandwidthAdmissionCandidateChecked = false;
	bool bHasHighBandwidthAdmissionCandidate = false;

	if (ForceNewClient())
		// There's not enough open uploads. Open another one.
		AddUpNextClient(_T("Not enough open upload slots for the current speed"));

	// The loop that feeds the upload slots with data.
	for (POSITION pos = uploadinglist.GetHeadPosition(); pos != NULL;) {
		// Get the client. Note! Also updates pos as a side effect.
		UploadingToClient_Struct *pCurClientStruct = uploadinglist.GetNext(pos);
		CUpDownClient *cur_client = pCurClientStruct->m_pClient;
		if (thePrefs.m_iDbgHeap >= 2)
			ASSERT_VALID(cur_client);
		// It seems chatting or friend slots can get stuck at times in upload. This needs to be looked into.
		const bool bSocketConnected = (cur_client->socket != NULL && cur_client->socket->IsConnected());
		const bool bSocketReady = (bSocketConnected && cur_client->CheckHandshakeFinished());
		if (!bSocketReady) {
			bool bKeepConnectingSlot = false;
			const bool bPendingNatRetry = cur_client->HasPendingNatTRetry();
			const bool bUtpBufferedReconnectCandidate =
				(cur_client->socket != NULL
				&& cur_client->socket->HaveNatTraversalLayer()
				&& !bPendingNatRetry
				&& (cur_client->GetUploadState() == US_UPLOADING || cur_client->GetUploadState() == US_NONE)
				&& (cur_client->socket->HasQueues(true)
					|| cur_client->GetQueueSessionUploadAdded() > cur_client->GetQueueSessionPayloadUp()));
			if (bPendingNatRetry && cur_client->GetUploadState() == US_UPLOADING) {
				cur_client->SetUploadState(US_CONNECTING);
				cur_client->SetUpStartTime();
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][UploadQueue] Preserving active upload slot for pending retry: %s"),
						(LPCTSTR)EscPercent(cur_client->DbgGetClientInfo()));
				}
			}
			if (bUtpBufferedReconnectCandidate) {
				cur_client->SetUploadState(US_CONNECTING);
				cur_client->SetUpStartTime();
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(DLP_LOW, false,
						_T("[NatTraversal][UploadQueue] Preserving buffered uTP upload slot for reconnect window (stdQueued=%d, uploadAdded=%I64u, payloadUp=%I64u) for %s"),
						cur_client->socket->DbgGetStdQueueCount(),
						cur_client->GetQueueSessionUploadAdded(),
						cur_client->GetQueueSessionPayloadUp(),
						(LPCTSTR)EscPercent(cur_client->DbgGetClientInfo()));
				}
			}
			if (cur_client->GetUploadState() == US_CONNECTING && (cur_client->socket != NULL || bPendingNatRetry)) {
				const DWORD upStart = cur_client->GetUpStartTime();
				if (upStart == 0) {
					// Guard against legacy paths that may enter US_CONNECTING without upload start time.
					cur_client->SetUpStartTime();
					bKeepConnectingSlot = true;
				} else if ((int)(curTick - upStart) < (int)staleConnectingSlotTimeout) {
					bKeepConnectingSlot = true;
				}
			}
			if (!bKeepConnectingSlot
				&& bSocketConnected
				&& cur_client->socket != NULL
				&& cur_client->socket->HaveNatTraversalLayer()
				&& cur_client->GetUploadState() == US_UPLOADING
				&& HasActiveNatTraversalDownloadContext(cur_client)) {
				const DWORD upStart = cur_client->GetUpStartTime();
				if (upStart == 0) {
					cur_client->SetUpStartTime();
					bKeepConnectingSlot = true;
				} else if ((int)(curTick - upStart) < (int)NAT_TRAVERSAL_HANDSHAKE_GUARD_MS) {
					bKeepConnectingSlot = true;
				}
				if (bKeepConnectingSlot && thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][UploadQueue] Preserving duplex NAT-T upload slot while download handshake settles: %s"),
						(LPCTSTR)EscPercent(cur_client->DbgGetClientInfo()));
				}
			}

			if (bKeepConnectingSlot)
				continue;

			if (thePrefs.GetLogNatTraversalEvents()) {
				AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][UploadQueue] Dropping stale upload slot (socketReady=%d, state=%d) for %s"),
					bSocketReady ? 1 : 0, (int)cur_client->GetUploadState(), (LPCTSTR)EscPercent(cur_client->DbgGetClientInfo()));
			}

			const bool bPreserveDownloadConnection = cur_client->socket != NULL
				&& cur_client->socket->HaveNatTraversalLayer()
				&& HasActiveNatTraversalDownloadContext(cur_client);
			RemoveFromUploadQueue(cur_client, _T("Uploading to client with stale socket/handshake (CUploadQueue::Process)"), true, true);
			if (bPreserveDownloadConnection) {
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][UploadQueue] Preserved active NAT-T download connection while dropping stale upload slot for %s"),
						(LPCTSTR)EscPercent(cur_client->DbgGetClientInfo()));
				}
			} else if (cur_client->Disconnected(_T("CUploadQueue::Process stale upload slot")))
				CUpDownClient::SafeDelete(cur_client);
			continue;
		}

		// Socket and handshake are ready, but upload state can still be CONNECTING
		// if the connect event path raced with UploadTimer.
		if (cur_client->GetUploadState() == US_CONNECTING) {
			if (thePrefs.GetLogNatTraversalEvents()) {
				AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][UploadQueue] Promoting CONNECTING->UPLOADING and sending OP_ACCEPTUPLOADREQ for %s"),
					(LPCTSTR)EscPercent(cur_client->DbgGetClientInfo()));
			}
			cur_client->SetUploadState(US_UPLOADING);
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugSend("OP_AcceptUploadReq", cur_client);
			Packet *packet = new Packet(OP_ACCEPTUPLOADREQ, 0);
			theStats.AddUpDataOverheadFileRequest(packet->size);
			cur_client->SendPacket(packet);
		}

		cur_client->UpdateUploadingStatisticsData();
		if (pCurClientStruct->m_bIOError) {
			RemoveFromUploadQueue(cur_client, _T("IO/Other Error while creating data packet (see earlier log entries)"), true);
			continue;
		}
		CString strSessionEndReason;
		if (CheckForTimeOver(cur_client, &strSessionEndReason)) {
			RemoveFromUploadQueue(cur_client, strSessionEndReason.IsEmpty() ? _T("Completed transfer") : static_cast<LPCTSTR>(strSessionEndReason), true);
			cur_client->SendOutOfPartReqsAndAddToWaitingQueue();
			continue;
		}

		const DWORD upStart = cur_client->GetUpStartTime();
		CEMSocket *pUploadSocket = cur_client->GetFileUploadSocket();
		if (bHighBandwidthIdleRecycleAllowed) {
			const bool bPlainTcpUpload = (pUploadSocket != NULL && !pUploadSocket->HaveNatTraversalLayer());
			INT_PTR iQueuedUploadBlocks = 0;
			LONG nPendingUploadIOBlocks = 0;
			{
				CSingleLock lockBlockLists(&pCurClientStruct->m_csBlockListsLock, TRUE);
				ASSERT(lockBlockLists.IsLocked());
				iQueuedUploadBlocks = pCurClientStruct->m_BlockRequests_queue.GetCount();
				nPendingUploadIOBlocks = ::InterlockedCompareExchange(&pCurClientStruct->m_nPendingIOBlocks, 0, 0);
			}
			const bool bHasPendingUploadIO = nPendingUploadIOBlocks > 0;
			const bool bSocketBacklog = (pUploadSocket != NULL && pUploadSocket->HasQueues(true));
			const bool bLocalSendPipelineEmpty = iQueuedUploadBlocks == 0 && !bHasPendingUploadIO && cur_client->GetPayloadInBuffer() == 0 && !bSocketBacklog;
			const bool bLocalSendPipelineBacklogged = !bHasPendingUploadIO && (iQueuedUploadBlocks > 0 || cur_client->GetPayloadInBuffer() != 0 || bSocketBacklog);
			const bool bSessionProducedPayload = cur_client->GetSessionUp() > 0 || cur_client->GetQueueSessionPayloadUp() > 0 || cur_client->GetQueueSessionUploadAdded() > 0;
			const DWORD lastUpRequest = cur_client->GetLastUpRequest();
			const DWORD idleReferenceTick = (lastUpRequest != 0 && upStart != 0 && (int)(lastUpRequest - upStart) > 0) ? lastUpRequest : upStart;
			const DWORD noRequestRecycleTimeout = SEC2MS(max(static_cast<UINT>(1), thePrefs.GetHighBandwidthZeroUploadGraceSeconds()));
			const DWORD productiveNoRequestRecycleTimeout = SEC2MS(max(thePrefs.GetHighBandwidthSlowUploadWarmupSeconds(), thePrefs.GetHighBandwidthZeroUploadGraceSeconds()));
			const DWORD stalledRecycleTimeout = productiveNoRequestRecycleTimeout;
			bool bRecycleIdleUploadSlot = false;
			LPCTSTR pszRecycleReason = NULL;
			CUpDownClient::EHighBandwidthUploadCooldownReason eCooldownReason = CUpDownClient::HBUCR_None;

			if (cur_client->GetUploadState() == US_UPLOADING
				&& bPlainTcpUpload
				&& !cur_client->GetFriendSlot()
				&& !cur_client->HasCollectionUploadSlot()
				&& upStart != 0)
			{
				if (bLocalSendPipelineEmpty && !bSessionProducedPayload && (int)(curTick - upStart) >= (int)noRequestRecycleTimeout) {
					bRecycleIdleUploadSlot = true;
					pszRecycleReason = _T("No block requests after upload slot activation");
					eCooldownReason = CUpDownClient::HBUCR_NoRequest;
				} else if (bLocalSendPipelineEmpty && bSessionProducedPayload && idleReferenceTick != 0
					&& cur_client->GetUploadDatarate() == 0
					&& (int)(curTick - idleReferenceTick) >= (int)productiveNoRequestRecycleTimeout)
				{
					bRecycleIdleUploadSlot = true;
					pszRecycleReason = _T("No further block requests during upload session");
					eCooldownReason = CUpDownClient::HBUCR_NoRequest;
				} else if (bLocalSendPipelineBacklogged && bSessionProducedPayload
					&& cur_client->GetUploadDatarate() == 0
					&& (int)(curTick - upStart) >= (int)stalledRecycleTimeout)
				{
					bRecycleIdleUploadSlot = true;
					pszRecycleReason = _T("Queued upload data stalled during high bandwidth upload");
					eCooldownReason = CUpDownClient::HBUCR_Stalled;
				}
			}

			if (bRecycleIdleUploadSlot) {
				bool bHasReplacementPressure = eCooldownReason == CUpDownClient::HBUCR_Stalled && uploadinglist.GetCount() < static_cast<INT_PTR>(GetHighBandwidthTargetUploadSlots());
				if (!bHasReplacementPressure) {
					if (!bHighBandwidthAdmissionCandidateChecked) {
						bHasHighBandwidthAdmissionCandidate = HasHighBandwidthUploadAdmissionCandidate(curTick);
						bHighBandwidthAdmissionCandidateChecked = true;
					}
					bHasReplacementPressure = bHasHighBandwidthAdmissionCandidate;
				}
				if (!bHasReplacementPressure)
					continue;
				if (eCooldownReason == CUpDownClient::HBUCR_NoRequest
					&& TrackUploadRequestAbuseEvent(cur_client, cur_client->GetUploadFileID(), curTick, UploadRequestAbuseNoRequestSlot))
				{
					if (IsDownloading(cur_client))
						RemoveFromUploadQueue(cur_client, GetResString(_T("PUNISHMENT_REASON_UPLOAD_REQUEST_ABUSE_NO_REQUEST")), true, true);
					continue;
				}
				SetHighBandwidthUploadRetryCooldown(cur_client, curTick, eCooldownReason);
				if (thePrefs.GetLogUlDlEvents()) {
					AddDebugLogLine(DLP_LOW, false, eCooldownReason == CUpDownClient::HBUCR_Stalled
						? _T("%s: Upload session ended because queued upload data stalled during high bandwidth upload.")
						: _T("%s: Upload session ended because the peer stopped requesting blocks during high bandwidth upload."),
						(LPCTSTR)EscPercent(cur_client->GetUserName()));
				}
				RemoveFromUploadQueue(cur_client, pszRecycleReason, true);
				cur_client->SendOutOfPartReqsAndAddToWaitingQueue();
				cur_client->SetWaitStartTime();
				continue;
			}
		}

		// Increase the sockets buffer for fast uploads (was in UpdateUploadingStatisticsData()).
		// This should be done in the throttling thread, but the throttler
		// does not have access to the client's download rate
		if (ShouldUseUploadSocketSendBuffer(cur_client->GetUploadDatarate())) {
			CEMSocket *sock = cur_client->GetFileUploadSocket();
			if (sock)
				sock->UseBigSendBuffer(GetUploadSocketSendBufferBytes());
		}

		// NAT-T keep-alive for uploader side
		// Maintains NAT mapping during upload idle periods
		if (thePrefs.IsEnableNatTraversal())
			cur_client->CheckNatTraversalKeepAlive();

		if (cur_client->IsDownloading())
			bHasActiveUploads = true;

		// check if the file id of the topmost block request matches the current upload file, otherwise
		// the IO thread will wait for us (only for this client of course) to fix it for cross-thread sync reasons
		CSingleLock lockBlockLists(&pCurClientStruct->m_csBlockListsLock, TRUE);
		ASSERT(lockBlockLists.IsLocked());
		// be careful what functions to call while having locks, RemoveFromUploadQueue could,
		// for example, lead to a deadlock here because it tries to get the uploadlist lock,
		// while the IO thread tries to fetch the uploadlist lock and then the blocklist lock
		if (!pCurClientStruct->m_BlockRequests_queue.IsEmpty()) {
			const Requested_Block_Struct *pHeadBlock = pCurClientStruct->m_BlockRequests_queue.GetHead();
			if (!md4equ(pHeadBlock->FileID, cur_client->GetUploadFileID())) {
				uchar aucNewID[MDX_DIGEST_SIZE];
				md4cpy(aucNewID, pHeadBlock->FileID);

				lockBlockLists.Unlock();

				CKnownFile *pCurrentUploadFile = theApp.sharedfiles->GetFileByID(aucNewID);
				if (pCurrentUploadFile != NULL)
					cur_client->SetUploadFileID(pCurrentUploadFile);
				else
					RemoveFromUploadQueue(cur_client, _T("Requested FileID in block request not found in shared files"), true);
			}
		}
	}

	m_bHasActiveUploads = bHasActiveUploads;

	// Save used bandwidth for speed calculations
	uint64 sentBytes = theApp.uploadBandwidthThrottler->GetNumberOfSentBytesSinceLastCallAndReset();
	average_dr_list.AddTail(sentBytes);
	m_average_dr_sum += sentBytes;

	(void)theApp.uploadBandwidthThrottler->GetNumberOfSentBytesOverheadSinceLastCallAndReset();

	average_friend_dr_list.AddTail(theStats.sessionSentBytesToFriend);

	// Save time between each speed snapshot
	average_tick_list.AddTail(curTick);

	// keep no more than 30 secs of data
	while (average_tick_list.GetCount() > 3 && !average_friend_dr_list.IsEmpty() && curTick >= average_tick_list.GetHead() + SEC2MS(30)) {
		m_average_dr_sum -= average_dr_list.RemoveHead();
		average_friend_dr_list.RemoveHead();
		average_tick_list.RemoveHead();
	}
};

uint32 CUploadQueue::GetHighBandwidthTargetUploadClients() const
{
	return thePrefs.GetHighBandwidthTargetUploadClients();
}

bool CUploadQueue::IsHighBandwidthUploadPolicyActive() const
{
	return thePrefs.IsHighBandwidthUploadPolicyEnabled();
}

UINT CUploadQueue::GetHighBandwidthTargetUploadSlots() const
{
	return static_cast<UINT>(min(GetHighBandwidthTargetUploadClients(), static_cast<uint32>(MAX_UP_CLIENTS_ALLOWED)));
}

UINT CUploadQueue::GetHighBandwidthEffectiveUploadSlotLimit() const
{
	const uint64 uTargetSlots = GetHighBandwidthTargetUploadSlots();
	const uint64 uElasticSlots = (uTargetSlots * thePrefs.GetHighBandwidthUploadSlotElasticPercent() + 99ui64) / 100ui64;
	return static_cast<UINT>(min(uTargetSlots + uElasticSlots, static_cast<uint64>(MAX_UP_CLIENTS_ALLOWED)));
}

UINT CUploadQueue::GetHighBandwidthUploadThrottlerSlotLimit() const
{
	return GetHighBandwidthEffectiveUploadSlotLimit();
}

uint64 CUploadQueue::GetHighBandwidthUploadBudgetBytesPerSec() const
{
	// Keep slot targets aligned with the active upload controller.
	const uint32 uActiveUploadBudgetBytesPerSec = theApp.lastCommonRouteFinder->GetUpload();
	if (uActiveUploadBudgetBytesPerSec != 0 && uActiveUploadBudgetBytesPerSec != UNLIMITED)
		return uActiveUploadBudgetBytesPerSec;

	uint32 uMaxUpload = thePrefs.GetEffectiveMaxUpload();
	if (uMaxUpload == 0 || uMaxUpload == UNLIMITED)
		uMaxUpload = thePrefs.GetMaxGraphUploadRate(true);
	if (uMaxUpload == 0 || uMaxUpload == UNLIMITED)
		return static_cast<uint64>(max(3u * 1024u, GetHighBandwidthTargetUploadClients() * 1024u));
	return static_cast<uint64>(uMaxUpload) * 1024ui64;
}

uint32 CUploadQueue::GetTargetClientDataRateHighBandwidth(bool bMinDatarate) const
{
	const UINT uTargetSlots = max(static_cast<UINT>(1), GetHighBandwidthTargetUploadSlots());
	const uint64 uBudgetBytesPerSec = GetHighBandwidthUploadBudgetBytesPerSec();
	const uint32 uTargetRate = static_cast<uint32>(min(static_cast<uint64>(UPLOAD_CLIENT_MAXDATARATE), max(3ui64 * 1024ui64, uBudgetBytesPerSec / uTargetSlots)));
	return bMinDatarate ? uTargetRate * 3 / 4 : uTargetRate;
}

bool CUploadQueue::IsHighBandwidthUploadUnderfilled(uint64 uUploadBudgetBytesPerSec) const
{
	if (uUploadBudgetBytesPerSec == 0)
		return false;
	return static_cast<uint64>(datarate) * 100ui64 < uUploadBudgetBytesPerSec * 95ui64;
}

void CUploadQueue::UpdateHighBandwidthUploadUnderfillState(DWORD curTick)
{
	if (!IsHighBandwidthUploadPolicyActive()) {
		m_dwHighBandwidthUploadUnderfillSince = 0;
		m_mapHighBandwidthUploadRetryCooldownByAddress.clear();
		return;
	}

	const bool bUnderfilled = IsHighBandwidthUploadUnderfilled(GetHighBandwidthUploadBudgetBytesPerSec());
	if (!bUnderfilled) {
		m_dwHighBandwidthUploadUnderfillSince = 0;
		return;
	}
	if (m_dwHighBandwidthUploadUnderfillSince == 0)
		m_dwHighBandwidthUploadUnderfillSince = curTick;
}

bool CUploadQueue::HasSustainedHighBandwidthUploadUnderfill(DWORD curTick) const
{
	if (m_dwHighBandwidthUploadUnderfillSince == 0)
		return false;
	return (int)(curTick - m_dwHighBandwidthUploadUnderfillSince) >= (int)SEC2MS(thePrefs.GetHighBandwidthSlowUploadGraceSeconds());
}

bool CUploadQueue::HasSustainedElasticHighBandwidthUploadUnderfill(DWORD curTick) const
{
	if (m_dwHighBandwidthUploadUnderfillSince == 0)
		return false;
	return (int)(curTick - m_dwHighBandwidthUploadUnderfillSince) >= (int)SEC2MS(kHighBandwidthElasticUnderfillSeconds);
}

bool CUploadQueue::ShouldProbeHighBandwidthUploadCooldownCandidate(DWORD curTick) const
{
	if (!IsHighBandwidthUploadPolicyActive() || uploadinglist.GetCount() >= static_cast<INT_PTR>(GetHighBandwidthTargetUploadSlots()) || m_dwHighBandwidthUploadUnderfillSince == 0)
		return false;
	return (int)(curTick - m_dwHighBandwidthUploadUnderfillSince) >= (int)SEC2MS(kHighBandwidthBaseRefillUnderfillSeconds);
}

bool CUploadQueue::CanProbeHighBandwidthUploadCooldownClient(const CUpDownClient *client, DWORD curTick) const
{
	return ShouldProbeHighBandwidthUploadCooldownCandidate(curTick) && client != NULL && client->CanProbeHighBandwidthSlowUploadCooldown(curTick);
}

bool CUploadQueue::HasHighBandwidthUploadAdmissionCandidate(DWORD curTick)
{
	if (!IsHighBandwidthUploadPolicyActive() || waitinglist.IsEmpty())
		return false;

	const bool bAllowCooldownProbe = ShouldProbeHighBandwidthUploadCooldownCandidate(curTick);
	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *cur_client = waitinglist.GetNext(pos);
		if (cur_client == NULL)
			continue;
		if (curTick >= cur_client->GetLastUpRequest() + MAX_PURGEQUEUETIME || !theApp.sharedfiles->GetFileByID(cur_client->GetUploadFileID()))
			continue;
		if (cur_client->IsBadClient() && cur_client->m_uPunishment == P_UPLOADBAN)
			continue;
		if (cur_client->HasLowID() && (!cur_client->socket || !cur_client->socket->IsConnected()))
			continue;
		if (IsHighBandwidthUploadRetryCooldownActive(cur_client, curTick) && (!bAllowCooldownProbe || !CanProbeHighBandwidthUploadCooldownClient(cur_client, curTick)))
			continue;
		return true;
	}
	return false;
}

bool CUploadQueue::ShouldRecycleSlowHighBandwidthUpload(CUpDownClient *client, DWORD curTick, CString *pstrReason)
{
	if (!IsHighBandwidthUploadPolicyActive() || client == NULL || waitinglist.IsEmpty() || uploadinglist.GetCount() < static_cast<INT_PTR>(GetHighBandwidthTargetUploadSlots()))
		return false;

	const INT_PTR iRequiredActiveSlots = min(uploadinglist.GetCount(), static_cast<INT_PTR>(GetHighBandwidthTargetUploadSlots()));
	if (m_iHighestNumberOfFullyActivatedSlotsSinceLastCall < iRequiredActiveSlots)
		return false;
	if (m_dwLastHighBandwidthSlowRecycleTick != 0 && (int)(curTick - m_dwLastHighBandwidthSlowRecycleTick) < (int)SEC2MS(1))
		return false;

	const DWORD dwCooldown = SEC2MS(thePrefs.GetHighBandwidthSlowUploadCooldownSeconds());
	if (!HasSustainedHighBandwidthUploadUnderfill(curTick))
		return false;
	if (!HasHighBandwidthUploadAdmissionCandidate(curTick))
		return false;

	UploadingToClient_Struct *pUploadClientStruct = GetUploadingClientStructByClient(client);
	if (pUploadClientStruct == NULL || HasPendingUploadBlockRequests(pUploadClientStruct))
		return false;
	CEMSocket *pUploadSocket = client->GetFileUploadSocket();
	if (client->GetPayloadInBuffer() != 0 || (pUploadSocket != NULL && pUploadSocket->HasQueues(true)))
		return false;

	const DWORD dwSlowGrace = SEC2MS(thePrefs.GetHighBandwidthSlowUploadGraceSeconds());
	const DWORD dwWarmup = SEC2MS(thePrefs.GetHighBandwidthSlowUploadWarmupSeconds());
	const DWORD dwZeroGrace = SEC2MS(thePrefs.GetHighBandwidthZeroUploadGraceSeconds());
	if (!client->ShouldRecycleHighBandwidthSlowUpload(GetTargetClientDataRate(false), curTick, dwSlowGrace, dwWarmup, dwZeroGrace, dwCooldown, thePrefs.GetHighBandwidthSlowUploadThresholdFactor()))
		return false;

	m_dwLastHighBandwidthSlowRecycleTick = curTick;
	if (pstrReason != NULL)
		*pstrReason = client->GetUploadDatarate() == 0 ? _T("Reassigning inactive upload slot") : _T("Reassigning slow upload slot");
	return true;
}

CAddress CUploadQueue::GetHighBandwidthUploadRetryCooldownAddress(const CUpDownClient *client) const
{
	if (client == NULL)
		return CAddress();
	if (!client->GetIP().IsNull() && client->GetIP().IsPublicIP())
		return client->GetIP();
	if (!client->GetConnectIP().IsNull() && client->GetConnectIP().IsPublicIP())
		return client->GetConnectIP();
	return CAddress();
}

bool CUploadQueue::IsHighBandwidthUploadRetryCooldownActive(const CUpDownClient *client, DWORD curTick) const
{
	if (!IsHighBandwidthUploadPolicyActive() || client == NULL)
		return false;
	if (client->IsHighBandwidthSlowUploadCooldownActive(curTick))
		return true;

	const CAddress cooldownAddress = GetHighBandwidthUploadRetryCooldownAddress(client);
	if (cooldownAddress.IsNull())
		return false;
	std::map<CAddress, SHighBandwidthUploadRetryCooldownState>::const_iterator itCooldown = m_mapHighBandwidthUploadRetryCooldownByAddress.find(cooldownAddress);
	return itCooldown != m_mapHighBandwidthUploadRetryCooldownByAddress.end() && (int)(curTick - itCooldown->second.m_dwCooldownUntil) < 0;
}

void CUploadQueue::SetHighBandwidthUploadRetryCooldown(CUpDownClient *client, DWORD curTick, UINT uReason)
{
	if (client == NULL || !IsHighBandwidthUploadPolicyActive())
		return;

	const CUpDownClient::EHighBandwidthUploadCooldownReason eReason = static_cast<CUpDownClient::EHighBandwidthUploadCooldownReason>(uReason);
	const DWORD dwCooldown = SEC2MS(thePrefs.GetHighBandwidthSlowUploadCooldownSeconds());
	client->SetHighBandwidthSlowUploadCooldown(curTick, dwCooldown, eReason);

	if (eReason != CUpDownClient::HBUCR_NoRequest)
		return;

	const CAddress cooldownAddress = GetHighBandwidthUploadRetryCooldownAddress(client);
	if (cooldownAddress.IsNull())
		return;
	const DWORD dwNoRequestAddressCooldown = min(dwCooldown, static_cast<DWORD>(SEC2MS(15)));
	SHighBandwidthUploadRetryCooldownState state;
	state.m_dwCooldownUntil = curTick + dwNoRequestAddressCooldown;
	m_mapHighBandwidthUploadRetryCooldownByAddress[cooldownAddress] = state;
}

void CUploadQueue::ClearHighBandwidthUploadRetryCooldown(CUpDownClient *client, DWORD curTick)
{
	if (client == NULL || !IsHighBandwidthUploadPolicyActive())
		return;

	client->SetHighBandwidthSlowUploadCooldown(curTick, 0);
	const CAddress cooldownAddress = GetHighBandwidthUploadRetryCooldownAddress(client);
	if (!cooldownAddress.IsNull())
		m_mapHighBandwidthUploadRetryCooldownByAddress.erase(cooldownAddress);
}

void CUploadQueue::PurgeExpiredHighBandwidthUploadRetryCooldowns(DWORD curTick)
{
	if (!IsHighBandwidthUploadPolicyActive()) {
		m_mapHighBandwidthUploadRetryCooldownByAddress.clear();
		return;
	}

	for (std::map<CAddress, SHighBandwidthUploadRetryCooldownState>::iterator itCooldown = m_mapHighBandwidthUploadRetryCooldownByAddress.begin(); itCooldown != m_mapHighBandwidthUploadRetryCooldownByAddress.end();) {
		if ((int)(curTick - itCooldown->second.m_dwCooldownUntil) >= 0)
			m_mapHighBandwidthUploadRetryCooldownByAddress.erase(itCooldown++);
		else
			++itCooldown;
	}
}

CString CUploadQueue::BuildUploadRequestAbuseClientKey(const CUpDownClient *client) const
{
	if (client == NULL)
		return CString();
	if (client->HasValidHash()) {
		CString strKey;
		strKey.Format(_T("H:%s"), (LPCTSTR)md4str(client->GetUserHash()));
		return strKey;
	}

	const CAddress address = GetHighBandwidthUploadRetryCooldownAddress(client);
	if (address.IsNull())
		return CString();

	CString strKey;
	strKey.Format(_T("I:%s"), (LPCTSTR)address.ToStringC());
	return strKey;
}

CString CUploadQueue::BuildUploadRequestAbuseFileKey(const uchar *pFileHash) const
{
	if (pFileHash == NULL || isnulmd4(pFileHash))
		return CString();
	return md4str(pFileHash);
}

CString CUploadQueue::BuildUploadRequestAbuseEventKey(EUploadRequestAbuseEvent eEvent, const CUpDownClient *client, const uchar *pFileHash) const
{
	const CString strClientKey = BuildUploadRequestAbuseClientKey(client);
	if (strClientKey.IsEmpty())
		return CString();

	const CString strFileKey = BuildUploadRequestAbuseFileKey(pFileHash);
	CString strKey;
	strKey.Format(_T("%c:%s:%s"), eEvent == UploadRequestAbuseNoRequestSlot ? _T('N') : _T('Q'), (LPCTSTR)strClientKey, (LPCTSTR)strFileKey);
	return strKey;
}

CString CUploadQueue::BuildUploadRequestAbuseIPRotationKey(EUploadRequestAbuseEvent eEvent, const CUpDownClient *client) const
{
	if (client == NULL || !client->HasValidHash())
		return CString();

	const CAddress address = GetHighBandwidthUploadRetryCooldownAddress(client);
	if (address.IsNull())
		return CString();

	CString strKey;
	strKey.Format(_T("%cI:%s"), eEvent == UploadRequestAbuseNoRequestSlot ? _T('N') : _T('Q'), (LPCTSTR)address.ToStringC());
	return strKey;
}

bool CUploadQueue::ApplyUploadRequestAbusePunishment(CUpDownClient *client, LPCTSTR pszReasonKey, bool bForceIPUserHashBan)
{
	if (client == NULL || client->IsBanned())
		return client != NULL && client->IsBanned();

	if (bForceIPUserHashBan) {
		client->Ban(GetResString(pszReasonKey), PR_UPLOADREQUESTABUSE, P_IPUSERHASHBAN);
		client->ProcessBanMessage();
	} else {
		theApp.shield->SetPunishment(client, GetResString(pszReasonKey), PR_UPLOADREQUESTABUSE);
		if (client->IsBanned())
			client->ProcessBanMessage();
	}
	return client->IsBanned() || (client->IsBadClient() && client->m_uPunishment == P_UPLOADBAN);
}

bool CUploadQueue::TrackUploadRequestAbuseEvent(CUpDownClient *client, const uchar *pFileHash, DWORD curTick, EUploadRequestAbuseEvent eEvent, bool *pbSuppressRequestStatistic)
{
	if (pbSuppressRequestStatistic != NULL)
		*pbSuppressRequestStatistic = false;
	if (!thePrefs.IsDetectUploadRequestAbuse() || client == NULL || client->IsFriend() || client->IsBanned())
		return false;
	if (eEvent == UploadRequestAbuseNoRequestSlot && !thePrefs.IsDetectUploadRequestAbuseNoRequestSlots())
		return false;
	if (eEvent == UploadRequestAbuseQueueReaskDrop && !thePrefs.IsDetectUploadRequestAbuseQueueDrops())
		return false;

	const CString strEventKey = BuildUploadRequestAbuseEventKey(eEvent, client, pFileHash);
	if (strEventKey.IsEmpty())
		return false;

	const DWORD dwWindowMs = eEvent == UploadRequestAbuseNoRequestSlot ? kUploadRequestAbuseNoRequestWindowMs : kUploadRequestAbuseQueueWindowMs;
	const UINT uBanThreshold = eEvent == UploadRequestAbuseNoRequestSlot ? kUploadRequestAbuseNoRequestBanThreshold : kUploadRequestAbuseQueueBanThreshold;
	SUploadRequestAbuseState& state = m_mapUploadRequestAbuseByClientFile[strEventKey];
	if (state.m_dwWindowUntil == 0 || (int)(curTick - state.m_dwWindowUntil) >= 0) {
		state.m_dwWindowUntil = curTick + dwWindowMs;
		state.m_uStrikes = 0;
	}
	const UINT uStrikes = ++state.m_uStrikes;

	bool bShouldPunish = uStrikes >= uBanThreshold;
	LPCTSTR pszReasonKey = eEvent == UploadRequestAbuseNoRequestSlot ? _T("PUNISHMENT_REASON_UPLOAD_REQUEST_ABUSE_NO_REQUEST") : _T("PUNISHMENT_REASON_UPLOAD_REQUEST_ABUSE_QUEUE_DROP");
	bool bForceIPUserHashBan = false;
	if (eEvent == UploadRequestAbuseQueueReaskDrop
		&& thePrefs.IsUploadRequestAbuseCounterGuard()
		&& pbSuppressRequestStatistic != NULL
		&& uStrikes >= kUploadRequestAbuseCounterGuardThreshold)
	{
		*pbSuppressRequestStatistic = true;
	}

	if (thePrefs.IsUploadRequestAbuseHashRotationTracking() && client->HasValidHash()) {
		const CString strRotationKey = BuildUploadRequestAbuseIPRotationKey(eEvent, client);
		if (!strRotationKey.IsEmpty()) {
			SUploadRequestAbuseIPState& ipState = m_mapUploadRequestAbuseByIP[strRotationKey];
			if (ipState.m_dwWindowUntil == 0 || (int)(curTick - ipState.m_dwWindowUntil) >= 0) {
				ipState.m_dwWindowUntil = curTick + dwWindowMs;
				ipState.m_uTotalStrikes = 0;
				ipState.m_setHashKeys.clear();
			}
			ipState.m_setHashKeys.insert(md4str(client->GetUserHash()));
			++ipState.m_uTotalStrikes;
			if (ipState.m_setHashKeys.size() >= kUploadRequestAbuseHashRotationHashThreshold
				&& ipState.m_uTotalStrikes >= kUploadRequestAbuseHashRotationStrikeThreshold)
			{
				bShouldPunish = true;
				bForceIPUserHashBan = true;
				pszReasonKey = _T("PUNISHMENT_REASON_UPLOAD_REQUEST_ABUSE_HASH_ROTATION");
			}
		}
	}

	if (!bShouldPunish)
		return false;
	return ApplyUploadRequestAbusePunishment(client, pszReasonKey, bForceIPUserHashBan);
}

void CUploadQueue::PurgeExpiredUploadRequestAbuseTracking(DWORD curTick)
{
	if (!thePrefs.IsDetectUploadRequestAbuse()) {
		m_mapUploadRequestAbuseByClientFile.clear();
		m_mapUploadRequestAbuseByIP.clear();
		m_dwLastUploadRequestAbuseCleanupTick = 0;
		return;
	}
	if (m_dwLastUploadRequestAbuseCleanupTick != 0 && (int)(curTick - (m_dwLastUploadRequestAbuseCleanupTick + kUploadRequestAbuseCleanupIntervalMs)) < 0)
		return;
	m_dwLastUploadRequestAbuseCleanupTick = curTick;

	for (std::map<CString, SUploadRequestAbuseState, SUploadRequestAbuseStringLess>::iterator itState = m_mapUploadRequestAbuseByClientFile.begin(); itState != m_mapUploadRequestAbuseByClientFile.end();) {
		if ((int)(curTick - itState->second.m_dwWindowUntil) >= 0)
			m_mapUploadRequestAbuseByClientFile.erase(itState++);
		else
			++itState;
	}
	for (std::map<CString, SUploadRequestAbuseIPState, SUploadRequestAbuseStringLess>::iterator itIPState = m_mapUploadRequestAbuseByIP.begin(); itIPState != m_mapUploadRequestAbuseByIP.end();) {
		if ((int)(curTick - itIPState->second.m_dwWindowUntil) >= 0)
			m_mapUploadRequestAbuseByIP.erase(itIPState++);
		else
			++itIPState;
	}
}

UINT CUploadQueue::GetUploadBufferBlockCount(const CUpDownClient *client) const
{
	if (client == NULL)
		return 1;
	if (IsHighBandwidthUploadPolicyActive())
		return GetHighBandwidthUploadBufferBlockCount(GetTargetClientDataRate(false), client->GetUploadDatarate());
	return client->GetUploadDatarate() > 75 * 1024 ? 5 : 1;
}

bool CUploadQueue::ShouldUseUploadSocketSendBuffer(uint32 uClientDatarate) const
{
	if (!IsHighBandwidthUploadPolicyActive())
		return uClientDatarate > 100u * 1024u;

	const uint32 uTargetRate = GetTargetClientDataRate(false);
	const uint32 uMinimumObservedRate = max(uTargetRate / 2u, 3u * 1024u);
	return uTargetRate >= 512u * 1024u || uClientDatarate >= uMinimumObservedRate;
}

uint32 CUploadQueue::GetUploadSocketSendBufferBytes() const
{
	if (IsHighBandwidthUploadPolicyActive())
		return GetAdaptiveTcpUploadSendBufferBytes(GetTargetClientDataRate(false));
	return 128u * 1024u;
}

bool CUploadQueue::AcceptNewClient(bool addOnNextConnect) const
{
	INT_PTR curUploadSlots = uploadinglist.GetCount();

	//We allow ONE extra slot to be created to accommodate lowID users.
	//This is because we skip these users when it was actually their turn
	//to get an upload slot.
	curUploadSlots -= static_cast<INT_PTR>(addOnNextConnect && curUploadSlots > 0);

	return AcceptNewClient(curUploadSlots);
}
// check if we can allow a new client to start downloading from us

bool CUploadQueue::AcceptNewClient(INT_PTR curUploadSlots) const
{

	const INT_PTR iMinimumUploadSlots = max(static_cast<INT_PTR>(MIN_UP_CLIENTS_ALLOWED), static_cast<INT_PTR>(4));
	if (curUploadSlots < iMinimumUploadSlots)
		return true;
	if (curUploadSlots >= static_cast<INT_PTR>(MAX_UP_CLIENTS_ALLOWED))
		return false;
	if (IsHighBandwidthUploadPolicyActive()) {
		const UINT uTargetUploadSlots = GetHighBandwidthTargetUploadSlots();
		if (curUploadSlots < static_cast<INT_PTR>(uTargetUploadSlots))
			return true;
		return curUploadSlots < static_cast<INT_PTR>(GetHighBandwidthEffectiveUploadSlotLimit()) && HasSustainedElasticHighBandwidthUploadUnderfill(::GetTickCount());
	}

	uint32 MaxSpeed;
	if (thePrefs.IsDynUpEnabled())
		MaxSpeed = theApp.lastCommonRouteFinder->GetUpload() / 1024;
	else
		MaxSpeed = thePrefs.GetEffectiveMaxUpload();
	uint32 TargetRate = GetTargetClientDataRate(false);
	const uint64 uMaxSpeedLimit = (MaxSpeed != UNLIMITED) ? static_cast<uint64>(MaxSpeed) : static_cast<uint64>(thePrefs.GetMaxGraphUploadRate(true));

	if (curUploadSlots >= (INT_PTR)(datarate / GetTargetClientDataRate(true)) || (uMaxSpeedLimit > 0 && curUploadSlots >= (INT_PTR)(uMaxSpeedLimit * 1024u / TargetRate)))
		return false;

	return MaxSpeed != UNLIMITED
		|| thePrefs.IsDynUpEnabled()
		|| thePrefs.GetMaxGraphUploadRate(true) <= 0
		|| curUploadSlots < (INT_PTR)(static_cast<uint64>(thePrefs.GetMaxGraphUploadRate(false)) * 1024u / TargetRate);
}

uint32 CUploadQueue::GetTargetClientDataRate(bool bMinDatarate) const
{
	if (IsHighBandwidthUploadPolicyActive())
		return GetTargetClientDataRateHighBandwidth(bMinDatarate);

	uint32 nOpenSlots = (uint32)GetUploadQueueLength();
	// 3 slots or less - 3KiB/s
	// 4 slots or more - linear growth by 1 KiB/s steps, cap off at UPLOAD_CLIENT_MAXDATARATE
	uint32 nResult;
	if (nOpenSlots <= 3)
		nResult = 3 * 1024;
	else
		nResult = min(UPLOAD_CLIENT_MAXDATARATE, nOpenSlots * 1024);

	return bMinDatarate ? nResult * 3 / 4 : nResult;
}

bool CUploadQueue::CanRotateUploadSession() const
{
	if (waitinglist.IsEmpty())
		return false;
	return theApp.lastCommonRouteFinder->AcceptNewClient();
}

bool CUploadQueue::ForceNewClient(bool allowEmptyWaitingQueue)
{
	if (!allowEmptyWaitingQueue && waitinglist.IsEmpty())
		return false;

	INT_PTR curUploadSlots = uploadinglist.GetCount();
	if (curUploadSlots < MIN_UP_CLIENTS_ALLOWED)
		return true;

	const DWORD curTick = ::GetTickCount();
	const bool bHighBandwidthPolicyActive = IsHighBandwidthUploadPolicyActive();
	if ((int)(curTick - m_nLastStartUpload) < (int)SEC2MS(1) && datarate < 102400)
		return false;

	if (!AcceptNewClient(curUploadSlots))
		return false;

	if (bHighBandwidthPolicyActive) {
		if (!theApp.lastCommonRouteFinder->AcceptNewClient()) // UploadSpeedSense can veto a new high bandwidth slot if USS enabled.
			return false;
		return HasHighBandwidthUploadAdmissionCandidate(curTick);
	}

	if (!theApp.lastCommonRouteFinder->AcceptNewClient()) // UploadSpeedSense can veto a new classic slot if USS enabled.
		return false;

	uint32 MaxSpeed;
	if (thePrefs.IsDynUpEnabled())
		MaxSpeed = theApp.lastCommonRouteFinder->GetUpload() / 1024;
	else
		MaxSpeed = thePrefs.GetEffectiveMaxUpload();

	uint32 upPerClient = GetTargetClientDataRate(false);

	// if throttler doesn't require another slot, go with a slightly more restrictive method
	if (MaxSpeed > 49 /*|| MaxSpeed == UNLIMITED */) { //because UNLIMITED > 20
		upPerClient += datarate / 43;
		if (upPerClient > UPLOAD_CLIENT_MAXDATARATE)
			upPerClient = UPLOAD_CLIENT_MAXDATARATE;
	}

	//now the final check
	if (MaxSpeed == UNLIMITED) {
		if ((uint32)curUploadSlots < (datarate / upPerClient))
			return true;
	} else {
		uint32 nMaxSlots;
		if (MaxSpeed > 25)
			nMaxSlots = max((MaxSpeed * 1024) / upPerClient, (uint32)(MIN_UP_CLIENTS_ALLOWED + 3));
		else if (MaxSpeed > 16)
			nMaxSlots = MIN_UP_CLIENTS_ALLOWED + 2;
		else if (MaxSpeed > 9)
			nMaxSlots = MIN_UP_CLIENTS_ALLOWED + 1;
		else
			nMaxSlots = MIN_UP_CLIENTS_ALLOWED;

		if ((uint32)curUploadSlots < nMaxSlots)
			return true;
	}
	return m_iHighestNumberOfFullyActivatedSlotsSinceLastCall > uploadinglist.GetCount();
}

CUpDownClient* CUploadQueue::GetWaitingClientByIP_UDP(const CAddress& IP, uint16 nUDPPort, bool bIgnorePortOnUniqueIP, bool *pbMultipleIPs)
{
	if (pbMultipleIPs != NULL)
		*pbMultipleIPs = false;

	if (!IP.IsNull() && nUDPPort != 0) {
		const SUploadWaitingEndpointKey key(IP, nUDPPort);
		const std::multimap<SUploadWaitingEndpointKey, DWORD>& mapWaitingClientsByUDP = m_mapWaitingClientsByUDP;
		std::pair<std::multimap<SUploadWaitingEndpointKey, DWORD>::const_iterator, std::multimap<SUploadWaitingEndpointKey, DWORD>::const_iterator> range = mapWaitingClientsByUDP.equal_range(key);
		for (std::multimap<SUploadWaitingEndpointKey, DWORD>::const_iterator it = range.first; it != range.second; ++it) {
			CUpDownClient *cur_client = ResolveWaitingClientRuntimeID(it->second);
			if (cur_client != NULL && cur_client->GetUDPPort() == nUDPPort && IsWaitingClientAddressMatch(cur_client, IP))
				return cur_client;
		}
	}

	if (!IP.IsNull() && nUDPPort == 0) {
		const std::multimap<CAddress, DWORD>& mapWaitingClientsByIP = m_mapWaitingClientsByIP;
		std::pair<std::multimap<CAddress, DWORD>::const_iterator, std::multimap<CAddress, DWORD>::const_iterator> range = mapWaitingClientsByIP.equal_range(IP);
		for (std::multimap<CAddress, DWORD>::const_iterator it = range.first; it != range.second; ++it) {
			CUpDownClient *cur_client = ResolveWaitingClientRuntimeID(it->second);
			if (cur_client != NULL && cur_client->GetUDPPort() == 0 && IsWaitingClientAddressMatch(cur_client, IP))
				return cur_client;
		}
	}

	CUpDownClient *pMatchingIPClient = NULL;
	uint32 cMatches = 0;
	if (bIgnorePortOnUniqueIP && !IP.IsNull()) {
		const std::multimap<CAddress, DWORD>& mapWaitingClientsByIP = m_mapWaitingClientsByIP;
		std::pair<std::multimap<CAddress, DWORD>::const_iterator, std::multimap<CAddress, DWORD>::const_iterator> range = mapWaitingClientsByIP.equal_range(IP);
		for (std::multimap<CAddress, DWORD>::const_iterator it = range.first; it != range.second; ++it) {
			CUpDownClient *cur_client = ResolveWaitingClientRuntimeID(it->second);
			if (cur_client != NULL && cur_client != pMatchingIPClient && IsWaitingClientAddressMatch(cur_client, IP)) {
				pMatchingIPClient = cur_client;
				++cMatches;
			}
		}
	}

	if (pbMultipleIPs != NULL)
		*pbMultipleIPs = cMatches > 1;
	if (pMatchingIPClient != NULL && cMatches == 1)
		return pMatchingIPClient;

	if (static_cast<size_t>(waitinglist.GetCount()) != m_mapWaitingClientPositions.size()) {
		pMatchingIPClient = NULL;
		cMatches = 0;
		for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
			CUpDownClient *cur_client = waitinglist.GetNext(pos);
			if (IsWaitingClientAddressMatch(cur_client, IP) && nUDPPort == cur_client->GetUDPPort())
				return cur_client;
			if (IsWaitingClientAddressMatch(cur_client, IP) && bIgnorePortOnUniqueIP && cur_client != pMatchingIPClient) {
				pMatchingIPClient = cur_client;
				++cMatches;
			}
		}
		if (pbMultipleIPs != NULL)
			*pbMultipleIPs = cMatches > 1;
		if (pMatchingIPClient != NULL && cMatches == 1)
			return pMatchingIPClient;
	}
	return NULL;
}

CUpDownClient* CUploadQueue::GetWaitingClientByIP(const CAddress& IP) const
{
	if (!IP.IsNull()) {
		std::pair<std::multimap<CAddress, DWORD>::const_iterator, std::multimap<CAddress, DWORD>::const_iterator> range = m_mapWaitingClientsByIP.equal_range(IP);
		for (std::multimap<CAddress, DWORD>::const_iterator it = range.first; it != range.second; ++it) {
			CUpDownClient *cur_client = ResolveWaitingClientRuntimeID(it->second);
			if (cur_client != NULL && IsWaitingClientAddressMatch(cur_client, IP))
				return cur_client;
		}
	}

	if (static_cast<size_t>(waitinglist.GetCount()) != m_mapWaitingClientPositions.size()) {
		for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
			CUpDownClient *cur_client = waitinglist.GetNext(pos);
			if (IsWaitingClientAddressMatch(cur_client, IP))
				return cur_client;
		}
	}
	return NULL;
}

/**
 * Add a client to the waiting queue for uploads.
 *
 * @param client address of the client that should be added to the waiting queue
 *
 * @param bIgnoreTimelimit don't check time limit to possibly ban the client.
 */
void CUploadQueue::AddClientToQueue(CUpDownClient *client, bool bIgnoreTimelimit)
{
	if (!GuardUploadModelMutation(_T("CUploadQueue::AddClientToQueue")))
		return;
	const DWORD curTick = ::GetTickCount();

	//This is to keep users from abusing the limits we put on lowID callbacks.
	//1)Check if we are connected to any network and that we are a lowID.
	//(Although this check shouldn't matter as they wouldn't have found us.
	// But, maybe I'm missing something, so it's best to check as a precaution.)
	//2)Check if the user is connected to Kad. We do allow all Kad Callbacks.
	//3)Check if the user is in our download list or a friend.
	//We give these users a special pass as they are helping us.
	//4)Are we connected to a server? If we are, is the user on the same server?
	//TCP lowID callbacks are also allowed.
	//5)If the queue is very short, allow anyone in as we want to make sure
	//our upload is always used.
	if (   theApp.IsConnected()
		&& theApp.IsFirewalled()
		&& !client->GetKadPort()
		&& client->GetDownloadState() == DS_NONE
		&& !client->IsFriend()
		&& theApp.serverconnect
		&& !theApp.serverconnect->IsLocalServer(client->GetServerIP(), client->GetServerPort())
		&& GetWaitingUserCount() > 50)
	{
		return;
	}
	client->IncrementAskedCount();
	client->SetLastUpRequest();
	if (!bIgnoreTimelimit)
		client->AddRequestCount(client->GetUploadFileID());
	if (client->IsBanned())
		return;
	// Uploader Punishment Prevention for Punish Donkeys without SUI - sFrQlXeRt
	if (client->Credits() != NULL)
		theApp.shield->UploaderPunishmentPreventionActive(client); // This will set m_bUploaderPunishmentPreventionActive.

	// File Faker Detection [DavidXanatos]
	CKnownFile* uploadReqfile = theApp.sharedfiles->GetFileByID((uchar*)client->GetUploadFileID());
	if (uploadReqfile && thePrefs.IsDetectFileFaker() && client->CheckFileRequest(uploadReqfile))
		return;

	// check for duplicates
	if (IsOnUploadQueue(client)) {
		if (client->m_bAddNextConnect
			&& AcceptNewClient(client->m_bAddNextConnect)
			&& (!IsHighBandwidthUploadPolicyActive() || !IsHighBandwidthUploadRetryCooldownActive(client, curTick) || CanProbeHighBandwidthUploadCooldownClient(client, curTick)))
		{
			//Special care is given to lowID clients that missed their upload slot
			//due to the saving bandwidth on callbacks.
			if (thePrefs.GetLogUlDlEvents())
				AddDebugLogLine(false, _T("Adding ****lowid when reconnecting. Client: %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
			client->m_bAddNextConnect = false;
			RemoveFromWaitingQueue(client, true);
			// statistic values // TODO: Maybe we should change this to count each request for a file only once and ignore re-asks
			bool bSuppressRequestStatistic = false;
			if (!bIgnoreTimelimit && TrackUploadRequestAbuseEvent(client, client->GetUploadFileID(), curTick, UploadRequestAbuseQueueReaskDrop, &bSuppressRequestStatistic))
				return;
			CKnownFile *reqfile = theApp.sharedfiles->GetFileByID((uchar*)client->GetUploadFileID());
			if (reqfile && !bSuppressRequestStatistic)
				reqfile->statistic.AddRequest();
			AddUpNextClient(_T("Adding ****lowid when reconnecting."), client);
			CKnownFile* oldreqfile = theApp.sharedfiles->GetFileByID(client->GetOldUploadFileID());
			if (reqfile != oldreqfile)
				client->SetOldUploadFileID();
		} else {
			client->SendRankingInfo();
			theApp.QueueUploadClientRowsChanged(client, CemuleApp::UploadClientUiTargetQueueList);
		}
		return;
	}

	POSITION posDuplicate = NULL;
	CUpDownClient *cur_client = NULL;
	while ((cur_client = FindWaitingDuplicateClient(client, posDuplicate)) != NULL && posDuplicate != NULL) {
		theApp.clientlist->AddTrackClient(client); // in any case keep track of this client

		// another client with same ip:port or hash
		// this happens only in rare cases, because same userhash / ip:ports are assigned to the right client on connecting in most cases
		if (cur_client->credits != NULL && cur_client->credits->GetCurrentIdentState(cur_client->GetIP()) == IS_IDENTIFIED) {
			//cur_client has a valid secure hash, don't remove him
			if (thePrefs.GetVerbose())
				AddProtectionLogLine(false, (LPCTSTR)GetResString(_T("SAMEUSERHASH")), (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)EscPercent(cur_client->GetUserName()), (LPCTSTR)EscPercent(client->GetUserName()));

			return;
		}
		if (client->credits == NULL || client->credits->GetCurrentIdentState(client->GetIP()) != IS_IDENTIFIED) {
			// remove both since we do not know who the bad one is
			if (thePrefs.GetVerbose())
				AddProtectionLogLine(false, (LPCTSTR)GetResString(_T("SAMEUSERHASH")), (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)EscPercent(cur_client->GetUserName()), _T("Both"));
			RemoveFromWaitingQueue(posDuplicate, true);
			if (!cur_client->socket && cur_client->Disconnected(_T("AddClientToQueue - same userhash 2")))
				CUpDownClient::SafeDelete(cur_client);
			return;
		}
		//client has a valid secure hash, add him and remove the other one
		if (thePrefs.GetVerbose())
			AddProtectionLogLine(false, (LPCTSTR)GetResString(_T("SAMEUSERHASH")), (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)EscPercent(cur_client->GetUserName()), (LPCTSTR)EscPercent(cur_client->GetUserName()));
		RemoveFromWaitingQueue(posDuplicate, true);
		if (!cur_client->socket && cur_client->Disconnected(_T("AddClientToQueue - same userhash 1")))
			CUpDownClient::SafeDelete(cur_client);
		posDuplicate = NULL;
	}

	if (GetWaitingClientIPCount(client->GetIP()) >= 3) {
		// do not accept more than 3 clients from the same IP
		if (thePrefs.GetVerbose())
			DEBUG_ONLY(AddDebugLogLine(false, _T("%s's (%s) request to enter the queue was rejected, because of too many clients with the same IP"), (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)ipstr(client->GetConnectIP())));
		return;
	}
	if (theApp.clientlist->GetClientsFromIP(client->GetIP()) >= 3) {
		if (thePrefs.GetVerbose())
			DEBUG_ONLY(AddDebugLogLine(false, _T("%s's (%s) request to enter the queue was rejected, because of too many clients with the same IP (found in TrackedClientsList)"), (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)ipstr(client->GetConnectIP())));
		return;
	}
	// done

	// statistic values
	// TODO: Maybe we should change this to count each request for a file only once and ignore re-asks
	bool bSuppressRequestStatistic = false;
	if (!bIgnoreTimelimit && TrackUploadRequestAbuseEvent(client, client->GetUploadFileID(), curTick, UploadRequestAbuseQueueReaskDrop, &bSuppressRequestStatistic))
		return;
	CKnownFile *reqfile = theApp.sharedfiles->GetFileByID((uchar*)client->GetUploadFileID());
	if (reqfile && !bSuppressRequestStatistic)
		reqfile->statistic.AddRequest();

	// emule collection will bypass the queue
	if (reqfile != NULL && CCollection::HasCollectionExtention(reqfile->GetFileName()) && reqfile->GetFileSize() < (uint64)MAXPRIORITYCOLL_SIZE
		&& !client->IsDownloading() && client->socket != NULL && client->socket->IsConnected())
	{
		client->SetCollectionUploadSlot(true);
		RemoveFromWaitingQueue(client, true);
		client->SetOldUploadFileID();
		AddUpNextClient(_T("Collection Priority Slot"), client);
		return;
	}

	client->SetCollectionUploadSlot(false);

	// If the client still has an upload slot but the socket is no longer usable,
	// drop stale slot state before queue admission. Upload-list membership must be
	// checked independently from the raw state, because transient cleanup bugs can
	// desync US_* from the actual upload slot ownership.
	const bool bHasUploadSlot = IsDownloading(client);
	if (bHasUploadSlot) {
		const bool bUploadSocketReady = (client->socket != NULL && client->socket->IsConnected() && client->CheckHandshakeFinished());
		if (!bUploadSocketReady) {
			if (thePrefs.GetLogNatTraversalEvents()) {
				AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][UploadQueue] Resetting stale upload state before queue handling for %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
			}
			RemoveFromUploadQueue(client, _T("Reset stale upload state before queue admission"), true, true);
			return;
		}

		if (client->GetUploadState() != US_UPLOADING) {
			if (thePrefs.GetLogNatTraversalEvents()) {
				AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][UploadQueue] Repairing upload-state desync before duplicate queue handling (%s) for %s"),
					client->DbgGetUploadState(), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
			}
			client->SetUploadState(US_UPLOADING);
		}

		// Same-slot multi-file request: do not reject an existing upload slot because the waiting queue is full.
		// The client keeps one real upload slot and receives only another accept for the current file request.
		if (client->IsBadClient() && client->m_uPunishment == P_UPLOADBAN)
			return;
		if (reqfile == NULL)
			return;

		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP_AcceptUploadReq", client);

		if (thePrefs.IsDontAllowFileHotSwapping()) {
			//Xman Close Backdoor v2
			// A downloading client can simply request an other file during downloading this code checks the Up-Priority of the new request.
			CKnownFile* pOldFile = theApp.sharedfiles->GetFileByID((uchar*)client->GetOldUploadFileID());
			uint8 oldUpPrio = pOldFile ? pOldFile->GetUpPriorityEx() : 0;
			uint8 newUpPrio = reqfile->GetUpPriorityEx();
			if (pOldFile != NULL && newUpPrio < oldUpPrio) {
				if (thePrefs.GetLogUlDlEvents()) {
					const CString sExpectedName = pOldFile != NULL ? CString(pOldFile->GetFileName()) : CString(EMPTY);
					AddProtectionLogLine(false, _T("File hot swapping disallowed [AddClientToQueue]: (client=%s, expected=%s, asked=%s)"),
						(LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)EscPercent(sExpectedName), (LPCTSTR)EscPercent(reqfile->GetFileName()));
				}

				RemoveFromUploadQueue(client, _T("wrong file"), true);
				client->SendOutOfPartReqsAndAddToWaitingQueue();
				client->SetWaitStartTime(); // Penality (soft punishement)
				return;
			}
		}

		Packet *packet = new Packet(OP_ACCEPTUPLOADREQ, 0);
		theStats.AddUpDataOverheadFileRequest(packet->size);
		client->SendPacket(packet);
		return;
	}

	// cap the list
	// the queue limit in prefs is only a soft limit. Hard limit is higher up to 25% to accept
	// powershare clients and other high ranking clients after soft limit has been reached
	INT_PTR softQueueLimit = thePrefs.GetQueueSize();
	INT_PTR hardQueueLimit = softQueueLimit + max(softQueueLimit, 800) / 4;

	// if soft queue limit has been reached, only let in high ranking clients
	if (waitinglist.GetCount() >= hardQueueLimit
		|| (waitinglist.GetCount() >= softQueueLimit // soft queue limit is reached
			&& (!client->IsFriend() || !client->GetFriendSlot()) // client is not a friend with friend slot
			&& client->GetCombinedFilePrioAndCredit() < GetAverageCombinedFilePrioAndCredit() // and client has lower credits/wants lower prio file than average client in queue
		   )
	   )
	{
		// block client from getting on queue
		return;
	}
	if (waitinglist.IsEmpty()
		&& ForceNewClient(true)
		&& !(client->IsBadClient() && client->m_uPunishment == P_UPLOADBAN)
		&& (!IsHighBandwidthUploadPolicyActive() || !IsHighBandwidthUploadRetryCooldownActive(client, curTick) || CanProbeHighBandwidthUploadCooldownClient(client, curTick)))
	{
		client->SetOldUploadFileID();
		client->SetWaitStartTime();
		AddUpNextClient(_T("Direct add with empty queue."), client);
	} else {
		m_bStatisticsWaitingListDirty = true;
		POSITION posWaiting = waitinglist.AddTail(client);
		RegisterWaitingClient(client, posWaiting);
		InvalidateMaxClientScoreRecalculation(true);
		client->SetUploadState(US_ONUPLOADQUEUE);
		client->SetWaitStartTime();
		client->SetAskedCount(1);
		theApp.QueueUploadClientRowsChanged(client, CemuleApp::UploadClientUiTargetQueueList);
		theApp.QueueUploadListChangedEvent(CemuleApp::UploadClientUiTargetQueueList, _T("upload-waiting-added"));
		client->SendRankingInfo();
		client->SetOldUploadFileID();
	}
}

float CUploadQueue::GetAverageCombinedFilePrioAndCredit()
{
	if (waitinglist.IsEmpty()) {
		m_fAverageCombinedFilePrioAndCredit = 0.0F;
		m_bAverageCombinedFilePrioAndCreditValid = false;
		m_bAverageCombinedFilePrioAndCreditRecalcActive = false;
		m_posAverageCombinedFilePrioAndCreditRecalc = NULL;
		return m_fAverageCombinedFilePrioAndCredit;
	}

	const DWORD curTick = ::GetTickCount();
	if (m_bAverageCombinedFilePrioAndCreditRecalcActive
		|| !m_bAverageCombinedFilePrioAndCreditValid
		|| m_uAverageCombinedFilePrioAndCreditGeneration != m_uWaitingListGeneration
		|| curTick >= m_dwLastCalculatedAverageCombinedFilePrioAndCredit + SEC2MS(5)) {
		UINT uProcessed = 0;
		INT_PTR iRemaining = 0;
		if (!ProcessAverageCombinedFilePrioAndCreditChunk(128, uProcessed, iRemaining) && iRemaining > 0)
			MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceAverageCombinedFilePrioAndCredit);
	}

	return m_fAverageCombinedFilePrioAndCredit;
}

void CUploadQueue::InvalidateUploadClientStruct(UploadingToClient_Struct *pUploadClientStruct, CUpDownClient *pClient)
{
	ASSERT(pUploadClientStruct != NULL);
	ASSERT(pClient != NULL);
	if (pUploadClientStruct == NULL || pClient == NULL)
		return;

	pClient->FlushSendBlocks();
	CSingleLock lockBlockLists(&pUploadClientStruct->m_csBlockListsLock, TRUE);
	ASSERT(lockBlockLists.IsLocked());
	while (!pUploadClientStruct->m_BlockRequests_queue.IsEmpty())
		delete pUploadClientStruct->m_BlockRequests_queue.RemoveHead();
	while (!pUploadClientStruct->m_DoneBlocks_list.IsEmpty())
		delete pUploadClientStruct->m_DoneBlocks_list.RemoveHead();
	pUploadClientStruct->m_BlockRequests_keys.clear();
	pUploadClientStruct->m_DoneBlocks_keys.clear();
	pUploadClientStruct->m_pClient = NULL;
}

void CUploadQueue::ReclaimRetiredUploadClientStructs()
{
	CSingleLock lockUploadList(&m_csUploadListMainThrdWriteOtherThrdsRead, TRUE);
	ASSERT(lockUploadList.IsLocked());
	const DWORD dwCurrentTick = ::GetTickCount();
	for (POSITION pos = m_retiredUploadingList.GetHeadPosition(); pos != NULL;) {
		POSITION posRemove = pos;
		UploadingToClient_Struct *pUploadClientStruct = m_retiredUploadingList.GetNext(pos);
		const LONG nPendingIOBlocks = ::InterlockedCompareExchange(&pUploadClientStruct->m_nPendingIOBlocks, 0, 0);
		if (nPendingIOBlocks <= 0) {
			m_retiredUploadingList.RemoveAt(posRemove);
			delete pUploadClientStruct;
		} else if (dwCurrentTick - pUploadClientStruct->m_dwRetiredTick >= SEC2MS(30)
			&& dwCurrentTick - pUploadClientStruct->m_dwLastRetiredPendingIOLogTick >= SEC2MS(30))
		{
			pUploadClientStruct->m_dwLastRetiredPendingIOLogTick = dwCurrentTick;
			AddDebugLogLine(DLP_HIGH, false, _T("UploadQueue: retired upload entry is still waiting for %ld pending disk I/O block(s); delaying reclaim"), nPendingIOBlocks);
		}
	}
}

bool CUploadQueue::HasUploadClientStructForDiskIO(const UploadingToClient_Struct *pUploadClientStruct, bool& bActive, bool& bRetired) const
{
	bActive = false;
	bRetired = false;
	if (pUploadClientStruct == NULL)
		return false;

	bActive = uploadinglist.Find(const_cast<UploadingToClient_Struct*>(pUploadClientStruct)) != NULL;
	bRetired = !bActive && m_retiredUploadingList.Find(const_cast<UploadingToClient_Struct*>(pUploadClientStruct)) != NULL;
	return bActive || bRetired;
}

bool CUploadQueue::RemoveFromUploadQueue(CUpDownClient *client, LPCTSTR pszReason, bool updatewindow, bool earlyabort)
{
	if (!GuardUploadModelMutation(_T("CUploadQueue::RemoveFromUploadQueue")))
		return false;

	bool result = false;
	uint32 slotCounter = 1;
	for (POSITION pos = uploadinglist.GetHeadPosition(); pos != NULL;) {
		POSITION curPos = pos;
		UploadingToClient_Struct *curClientStruct = uploadinglist.GetNext(pos);
		if (client == curClientStruct->m_pClient) {

			const CString strReason = pszReason != NULL ? CString(EscPercent(pszReason)) : CString(EMPTY);
			CString strUploadFileName(EMPTY);
			const CKnownFile* pUploadFile = theApp.sharedfiles != NULL ? theApp.sharedfiles->GetFileByID(client->GetUploadFileID()) : NULL;
			if (pUploadFile != NULL)
				strUploadFileName = EscPercent(pUploadFile->GetFileName());

			if (thePrefs.GetLogUlDlEvents()) {
				AddDebugLogLine(DLP_DEFAULT, false, _T("Removing client from upload list: %s Client: %s Transferred: %s SessionUp: %s QueueSessionPayload: %s In buffer: %s Req blocks: %i File: %s")
					, (LPCTSTR)strReason
					, (LPCTSTR)client->DbgGetClientInfo()
					, (LPCTSTR)CastSecondsToHM(client->GetUpStartTimeDelay() / SEC2MS(1))
					, (LPCTSTR)CastItoXBytes(client->GetSessionUp())
					, (LPCTSTR)CastItoXBytes(client->GetQueueSessionPayloadUp())
					, (LPCTSTR)CastItoXBytes(client->GetPayloadInBuffer()), curClientStruct->m_BlockRequests_queue.GetCount()
					, (LPCTSTR)strUploadFileName);
			}
			if (thePrefs.GetLogNatTraversalEvents() && client->socket != NULL && client->socket->HaveNatTraversalLayer()) {
				AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] Removing NAT-T upload slot: %s Client: %s Transferred: %s SessionUp: %s QueueSessionPayload: %s In buffer: %s Req blocks: %i File: %s")
					, (LPCTSTR)strReason
					, (LPCTSTR)EscPercent(client->DbgGetClientInfo())
					, (LPCTSTR)CastSecondsToHM(client->GetUpStartTimeDelay() / SEC2MS(1))
					, (LPCTSTR)CastItoXBytes(client->GetSessionUp())
					, (LPCTSTR)CastItoXBytes(client->GetQueueSessionPayloadUp())
					, (LPCTSTR)CastItoXBytes(client->GetPayloadInBuffer()), curClientStruct->m_BlockRequests_queue.GetCount()
					, (LPCTSTR)strUploadFileName);
			}
			client->m_bAddNextConnect = false;

			LONG nPendingIOBlocks = 0;
			{
				CSingleLock lockUploadList(&m_csUploadListMainThrdWriteOtherThrdsRead, TRUE);
				ASSERT(lockUploadList.IsLocked());
				nPendingIOBlocks = ::InterlockedCompareExchange(&curClientStruct->m_nPendingIOBlocks, 0, 0);
				if (nPendingIOBlocks > 0) {
					m_retiredUploadingList.AddTail(curClientStruct);
					curClientStruct->m_bRetired = true;
					curClientStruct->m_dwRetiredTick = ::GetTickCount();
					curClientStruct->m_dwLastRetiredPendingIOLogTick = 0;
				}
				uploadinglist.RemoveAt(curPos);
			}
			if (nPendingIOBlocks > 0)
				InvalidateUploadClientStruct(curClientStruct, client);
			else
				delete curClientStruct;

			theApp.uploadBandwidthThrottler->RemoveFromStandardList(client->socket);

			if (client->GetSessionUp() > 0) {
				++successfullupcount;
				totaluploadtime += client->GetUpStartTimeDelay() / SEC2MS(1);
			} else
				failedupcount += static_cast<uint32>(!earlyabort);

			CKnownFile *requestedFile = theApp.sharedfiles->GetFileByID(client->GetUploadFileID());
			if (requestedFile != NULL)
				requestedFile->UpdatePartsInfo();

			theApp.clientlist->AddTrackClient(client); // Keep track of this client
			client->SetUploadState(US_NONE);
			client->SetCollectionUploadSlot(false);
			if (updatewindow) {
				theApp.QueueUploadClientUiRemove(client, CemuleApp::UploadClientUiTargetUploadList, _T("upload-client-removed"));
				if (IsOnUploadQueue(client))
					theApp.QueueUploadClientRowsChanged(client, CemuleApp::UploadClientUiTargetQueueList);
				theApp.QueueUploadClientRowsChanged(client, CemuleApp::UploadClientUiTargetDownloadClients);
			}
			theApp.QueueUploadListChangedEvent(CemuleApp::UploadClientUiTargetUploadList, _T("upload-client-removed"));

			m_iHighestNumberOfFullyActivatedSlotsSinceLastCall = 0;

			result = true;
		} else {
			curClientStruct->m_pClient->SetSlotNumber(slotCounter);
			++slotCounter;
		}
	}
	return result;
}

uint32 CUploadQueue::GetAverageUpTime() const
{
	return successfullupcount ? (totaluploadtime / successfullupcount) : 0;
}

bool CUploadQueue::RemoveFromWaitingQueue(CUpDownClient *client, bool updatewindow)
{
	if (!GuardUploadModelMutation(_T("CUploadQueue::RemoveFromWaitingQueue")))
		return false;

	POSITION pos = NULL;
	if (GetWaitingClientPosition(client, pos)) {
		RemoveFromWaitingQueue(pos, updatewindow);
		return true;
	}
	return false;
}

void CUploadQueue::RemoveFromWaitingQueue(POSITION pos, bool updatewindow)
{
	if (!GuardUploadModelMutation(_T("CUploadQueue::RemoveFromWaitingQueuePos")))
		return;

	m_bStatisticsWaitingListDirty = true;
	CUpDownClient *todelete = waitinglist.GetAt(pos);
	const bool bStillHasUploadSlot = IsDownloading(todelete);
	UnregisterWaitingClient(todelete);
	waitinglist.RemoveAt(pos);
	InvalidateMaxClientScoreRecalculation(true);
	if (updatewindow) {
		theApp.QueueUploadClientUiRemove(todelete, CemuleApp::UploadClientUiTargetQueueList, _T("upload-waiting-removed"));
		theApp.QueueUploadListChangedEvent(CemuleApp::UploadClientUiTargetQueueList, _T("upload-waiting-removed"));
	}
	todelete->m_bAddNextConnect = false;
	if (!bStillHasUploadSlot) {
		todelete->SetUploadState(US_NONE);
	} else if (thePrefs.GetLogNatTraversalEvents()) {
		AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][UploadQueue] Preserving upload state while removing wait-queue entry for active uploader %s"),
			(LPCTSTR)EscPercent(todelete->DbgGetClientInfo()));
	}
}

bool CUploadQueue::GetClientItemId(const CUpDownClient* pClient, SClientItemId& id)
{
	id.Clear();
	if (pClient == NULL)
		return false;

	id.m_uRuntimeID = pClient->GetRuntimeID();
	return id.IsValid();
}

CUpDownClient* CUploadQueue::AcquireClientByItemId(const SClientItemId& id) const
{
	if (!id.IsValid() || theApp.clientlist == NULL)
		return NULL;
	return theApp.clientlist->AcquireTrackedClientByRuntimeID(id.m_uRuntimeID);
}

// Remove clients waiting for files no longer shared
void CUploadQueue::PruneWaitersForMissingSharedFiles()
{
	FinishPruneWaitersForMissingSharedFilesSnapshot();

	// Drop waiting clients whose requested file is no longer shared. This is called from GUI thread after auto-reload of shared files.
	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL; ) {
		POSITION pos2 = pos;
		CUpDownClient* cur = waitinglist.GetNext(pos);
		if (cur != NULL && !theApp.sharedfiles->GetFileByID(cur->GetUploadFileID())) {
			cur->ClearWaitStartTime();
			RemoveFromWaitingQueue(pos2, true);
		}
	}
}

void CUploadQueue::StartPruneWaitersForMissingSharedFilesSnapshot()
{
	m_aPruneWaitersForMissingSharedFilesRuntimeIDs.clear();
	m_aPruneWaitersForMissingSharedFilesRuntimeIDs.reserve(static_cast<size_t>(waitinglist.GetCount()));
	m_uPruneWaitersForMissingSharedFilesIndex = 0;
	m_bPruneWaitersForMissingSharedFilesActive = true;

	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
		CUpDownClient* cur = waitinglist.GetNext(pos);
		SClientItemId id;
		if (GetClientItemId(cur, id))
			m_aPruneWaitersForMissingSharedFilesRuntimeIDs.push_back(id.m_uRuntimeID);
	}
}

void CUploadQueue::FinishPruneWaitersForMissingSharedFilesSnapshot()
{
	m_aPruneWaitersForMissingSharedFilesRuntimeIDs.clear();
	m_uPruneWaitersForMissingSharedFilesIndex = 0;
	m_bPruneWaitersForMissingSharedFilesActive = false;
}

bool CUploadQueue::PruneWaitersForMissingSharedFilesChunk(UINT uMaxClients, UINT& uProcessed, INT_PTR& iRemaining)
{
	uProcessed = 0;
	iRemaining = 0;

	if (theApp.IsClosing() || theApp.sharedfiles == NULL || theApp.clientlist == NULL) {
		FinishPruneWaitersForMissingSharedFilesSnapshot();
		return true;
	}

	if (!m_bPruneWaitersForMissingSharedFilesActive)
		StartPruneWaitersForMissingSharedFilesSnapshot();

	while (m_uPruneWaitersForMissingSharedFilesIndex < m_aPruneWaitersForMissingSharedFilesRuntimeIDs.size() && (uMaxClients == 0 || uProcessed < uMaxClients)) {
		const DWORD uRuntimeID = m_aPruneWaitersForMissingSharedFilesRuntimeIDs[m_uPruneWaitersForMissingSharedFilesIndex++];
		++uProcessed;

		CUpDownClient* cur = theApp.clientlist->AcquireTrackedClientByRuntimeID(uRuntimeID);
		if (cur == NULL)
			continue;

		if (!theApp.sharedfiles->GetFileByID(cur->GetUploadFileID())) {
			POSITION posRemove = NULL;
			if (GetWaitingClientPosition(cur, posRemove)) {
				cur->ClearWaitStartTime();
				RemoveFromWaitingQueue(posRemove, true);
			}
		}

		cur->ReleaseRuntimeReference();
	}

	if (m_uPruneWaitersForMissingSharedFilesIndex < m_aPruneWaitersForMissingSharedFilesRuntimeIDs.size()) {
		iRemaining = static_cast<INT_PTR>(m_aPruneWaitersForMissingSharedFilesRuntimeIDs.size() - m_uPruneWaitersForMissingSharedFilesIndex);
		return true;
	}

	FinishPruneWaitersForMissingSharedFilesSnapshot();
	return true;
}

void CUploadQueue::RestartAverageCombinedFilePrioAndCreditRecalculation()
{
	m_uAverageCombinedFilePrioAndCreditGeneration = m_uWaitingListGeneration;
	m_iAverageCombinedFilePrioAndCreditProcessed = 0;
	m_fAverageCombinedFilePrioAndCreditSum = 0.0;
	m_dwLastCalculatedAverageCombinedFilePrioAndCredit = ::GetTickCount();
	m_posAverageCombinedFilePrioAndCreditRecalc = waitinglist.GetHeadPosition();
	m_bAverageCombinedFilePrioAndCreditRecalcActive = m_posAverageCombinedFilePrioAndCreditRecalc != NULL;
	if (!m_bAverageCombinedFilePrioAndCreditRecalcActive) {
		m_fAverageCombinedFilePrioAndCredit = 0.0F;
		m_bAverageCombinedFilePrioAndCreditValid = false;
	}
}

bool CUploadQueue::ProcessAverageCombinedFilePrioAndCreditChunk(UINT uMaxClients, UINT& uProcessed, INT_PTR& iRemaining)
{
	uProcessed = 0;
	iRemaining = 0;

	if (!m_bAverageCombinedFilePrioAndCreditRecalcActive || m_uAverageCombinedFilePrioAndCreditGeneration != m_uWaitingListGeneration)
		RestartAverageCombinedFilePrioAndCreditRecalculation();

	if (!m_bAverageCombinedFilePrioAndCreditRecalcActive)
		return true;

	const DWORD dwSliceStart = ::GetTickCount();
	while (m_posAverageCombinedFilePrioAndCreditRecalc != NULL) {
		if (m_uAverageCombinedFilePrioAndCreditGeneration != m_uWaitingListGeneration) {
			RestartAverageCombinedFilePrioAndCreditRecalculation();
			break;
		}

		CUpDownClient* pClient = waitinglist.GetNext(m_posAverageCombinedFilePrioAndCreditRecalc);
		++uProcessed;
		++m_iAverageCombinedFilePrioAndCreditProcessed;
		if (pClient != NULL)
			m_fAverageCombinedFilePrioAndCreditSum += pClient->GetCombinedFilePrioAndCredit();

		if (uMaxClients != 0 && uProcessed >= uMaxClients)
			break;
		if (uProcessed >= 16 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetUploadTimerMaintenance))
			break;
	}

	if (m_iAverageCombinedFilePrioAndCreditProcessed > 0) {
		m_fAverageCombinedFilePrioAndCredit = static_cast<float>(m_fAverageCombinedFilePrioAndCreditSum / static_cast<double>(m_iAverageCombinedFilePrioAndCreditProcessed));
		m_bAverageCombinedFilePrioAndCreditValid = true;
	}

	if (m_posAverageCombinedFilePrioAndCreditRecalc != NULL) {
		const INT_PTR iCount = waitinglist.GetCount();
		iRemaining = iCount > m_iAverageCombinedFilePrioAndCreditProcessed ? iCount - m_iAverageCombinedFilePrioAndCreditProcessed : 0;
		return false;
	}

	m_bAverageCombinedFilePrioAndCreditRecalcActive = false;
	m_iAverageCombinedFilePrioAndCreditProcessed = 0;
	m_fAverageCombinedFilePrioAndCreditSum = 0.0;
	return true;
}


void CUploadQueue::RegisterWaitingClient(CUpDownClient *client, POSITION pos)
{
	if (client == NULL)
		return;
	const DWORD uRuntimeID = client->GetRuntimeID();
	if (uRuntimeID != 0) {
		m_setWaitingClientRuntimeIDs.insert(uRuntimeID);
		m_mapWaitingClientPositions[uRuntimeID] = pos;
		RegisterWaitingClientIndexes(client, uRuntimeID);
	}
}

void CUploadQueue::UnregisterWaitingClient(CUpDownClient *client)
{
	if (client == NULL)
		return;
	const DWORD uRuntimeID = client->GetRuntimeID();
	if (uRuntimeID != 0) {
		UnregisterWaitingClientIndexes(client, uRuntimeID);
		m_setWaitingClientRuntimeIDs.erase(uRuntimeID);
		m_mapWaitingClientPositions.erase(uRuntimeID);
		m_mapWaitingRankCache.erase(uRuntimeID);
		m_mapWaitingRankRequests.erase(uRuntimeID);
	}
}

bool CUploadQueue::GetWaitingClientPosition(const CUpDownClient *client, POSITION& pos) const
{
	pos = NULL;
	if (client == NULL)
		return false;

	const DWORD uRuntimeID = client->GetRuntimeID();
	if (uRuntimeID != 0) {
		std::map<DWORD, POSITION>::const_iterator it = m_mapWaitingClientPositions.find(uRuntimeID);
		if (it != m_mapWaitingClientPositions.end() && it->second != NULL) {
			pos = it->second;
			return true;
		}
	}

	if (uRuntimeID == 0 || static_cast<size_t>(waitinglist.GetCount()) != m_mapWaitingClientPositions.size()) {
		pos = waitinglist.Find(const_cast<CUpDownClient*>(client));
		return pos != NULL;
	}
	return false;
}

CUpDownClient* CUploadQueue::ResolveWaitingClientRuntimeID(DWORD uRuntimeID) const
{
	if (uRuntimeID == 0)
		return NULL;
	std::map<DWORD, POSITION>::const_iterator it = m_mapWaitingClientPositions.find(uRuntimeID);
	if (it == m_mapWaitingClientPositions.end() || it->second == NULL)
		return NULL;
	CUpDownClient* pClient = waitinglist.GetAt(it->second);
	return pClient != NULL && pClient->GetRuntimeID() == uRuntimeID ? pClient : NULL;
}

bool CUploadQueue::IsWaitingClientAddressMatch(const CUpDownClient *client, const CAddress& ip) const
{
	if (client == NULL)
		return false;
	return ip.GetType() == CAddress::IPv6 ? ip == client->GetIPv6() : ip == client->GetIP();
}

CUpDownClient* CUploadQueue::FindWaitingDuplicateClient(CUpDownClient *client, POSITION& posDuplicate) const
{
	posDuplicate = NULL;
	if (client == NULL)
		return NULL;

	CUpDownClient* pCandidate = NULL;
	if (client->HasValidHash()) {
		const SUploadWaitingHashKey key(client->GetUserHash());
		std::pair<std::multimap<SUploadWaitingHashKey, DWORD>::const_iterator, std::multimap<SUploadWaitingHashKey, DWORD>::const_iterator> range = m_mapWaitingClientsByHash.equal_range(key);
		for (std::multimap<SUploadWaitingHashKey, DWORD>::const_iterator it = range.first; it != range.second; ++it) {
			pCandidate = ResolveWaitingClientRuntimeID(it->second);
			if (pCandidate != NULL && pCandidate != client && client->Compare(pCandidate) && GetWaitingClientPosition(pCandidate, posDuplicate))
				return pCandidate;
		}
	}

	const CAddress* aIps[] = { &client->GetIPv4(), &client->GetIPv6() };
	for (int i = 0; i < 2; ++i) {
		if (aIps[i]->IsNull())
			continue;
		const uint16 aPorts[] = { client->GetUserPort(), client->GetKadPort() };
		for (int j = 0; j < 2; ++j) {
			if (aPorts[j] == 0)
				continue;
			const SUploadWaitingEndpointKey key(*aIps[i], aPorts[j]);
			std::pair<std::multimap<SUploadWaitingEndpointKey, DWORD>::const_iterator, std::multimap<SUploadWaitingEndpointKey, DWORD>::const_iterator> range = m_mapWaitingClientsByEndpoint.equal_range(key);
			for (std::multimap<SUploadWaitingEndpointKey, DWORD>::const_iterator it = range.first; it != range.second; ++it) {
				pCandidate = ResolveWaitingClientRuntimeID(it->second);
				if (pCandidate != NULL && pCandidate != client && client->Compare(pCandidate) && GetWaitingClientPosition(pCandidate, posDuplicate))
					return pCandidate;
			}
		}
	}

	if (client->GetUserIDHybrid() != 0) {
		const uint16 aPorts[] = { client->GetUserPort(), client->GetKadPort() };
		for (int i = 0; i < 2; ++i) {
			if (aPorts[i] == 0)
				continue;
			const SUploadWaitingIdPortKey key(client->GetUserIDHybrid(), aPorts[i]);
			std::pair<std::multimap<SUploadWaitingIdPortKey, DWORD>::const_iterator, std::multimap<SUploadWaitingIdPortKey, DWORD>::const_iterator> range = m_mapWaitingClientsByIdPort.equal_range(key);
			for (std::multimap<SUploadWaitingIdPortKey, DWORD>::const_iterator it = range.first; it != range.second; ++it) {
				pCandidate = ResolveWaitingClientRuntimeID(it->second);
				if (pCandidate != NULL && pCandidate != client && client->Compare(pCandidate) && GetWaitingClientPosition(pCandidate, posDuplicate))
					return pCandidate;
			}
		}
	}

	if (client->HasLowID() && client->GetUserIDHybrid() != 0 && client->GetServerIP() != 0 && client->GetServerPort() != 0) {
		const SUploadWaitingLowIdKey key(client->GetUserIDHybrid(), client->GetServerIP(), client->GetServerPort());
		std::pair<std::multimap<SUploadWaitingLowIdKey, DWORD>::const_iterator, std::multimap<SUploadWaitingLowIdKey, DWORD>::const_iterator> range = m_mapWaitingClientsByLowId.equal_range(key);
		for (std::multimap<SUploadWaitingLowIdKey, DWORD>::const_iterator it = range.first; it != range.second; ++it) {
			pCandidate = ResolveWaitingClientRuntimeID(it->second);
			if (pCandidate != NULL && pCandidate != client && client->Compare(pCandidate) && GetWaitingClientPosition(pCandidate, posDuplicate))
				return pCandidate;
		}
	}

	return NULL;
}

UINT CUploadQueue::GetWaitingClientIPCount(const CAddress& ip) const
{
	UINT uCount = 0;
	std::pair<std::multimap<CAddress, DWORD>::const_iterator, std::multimap<CAddress, DWORD>::const_iterator> range = m_mapWaitingClientsByIP.equal_range(ip);
	for (std::multimap<CAddress, DWORD>::const_iterator it = range.first; it != range.second; ++it) {
		CUpDownClient* pClient = ResolveWaitingClientRuntimeID(it->second);
		if (pClient != NULL && pClient->GetIP() == ip)
			++uCount;
	}
	return uCount;
}

void CUploadQueue::BuildWaitingClientIndexSnapshot(CUpDownClient *client, SUploadWaitingIndexSnapshot& snapshot) const
{
	snapshot = SUploadWaitingIndexSnapshot();
	if (client == NULL)
		return;

	if (client->HasValidHash()) {
		snapshot.m_bHasHash = true;
		snapshot.m_hashKey = SUploadWaitingHashKey(client->GetUserHash());
	}

	const CAddress* aEndpointIps[] = { &client->GetIPv4(), &client->GetIPv6() };
	for (int i = 0; i < 2; ++i) {
		if (aEndpointIps[i]->IsNull())
			continue;
		if (client->GetUserPort() != 0)
			snapshot.m_aEndpointKeys.push_back(SUploadWaitingEndpointKey(*aEndpointIps[i], client->GetUserPort()));
		if (client->GetKadPort() != 0)
			snapshot.m_aEndpointKeys.push_back(SUploadWaitingEndpointKey(*aEndpointIps[i], client->GetKadPort()));
	}

	CAddress aRegisteredIPs[3];
	int iRegisteredIPCount = 0;
	const CAddress* aLookupIps[] = { &client->GetIP(), &client->GetIPv4(), &client->GetIPv6() };
	for (int i = 0; i < 3; ++i) {
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
			snapshot.m_aUDPKeys.push_back(SUploadWaitingEndpointKey(*aLookupIps[i], client->GetUDPPort()));
	}

	if (client->GetUserIDHybrid() != 0) {
		if (client->GetUserPort() != 0)
			snapshot.m_aIdPortKeys.push_back(SUploadWaitingIdPortKey(client->GetUserIDHybrid(), client->GetUserPort()));
		if (client->GetKadPort() != 0)
			snapshot.m_aIdPortKeys.push_back(SUploadWaitingIdPortKey(client->GetUserIDHybrid(), client->GetKadPort()));
	}
	if (client->HasLowID() && client->GetUserIDHybrid() != 0 && client->GetServerIP() != 0 && client->GetServerPort() != 0) {
		snapshot.m_bHasLowId = true;
		snapshot.m_lowIdKey = SUploadWaitingLowIdKey(client->GetUserIDHybrid(), client->GetServerIP(), client->GetServerPort());
	}
}

void CUploadQueue::EraseWaitingClientIndexSnapshot(DWORD uRuntimeID, const SUploadWaitingIndexSnapshot& snapshot)
{
	if (uRuntimeID == 0)
		return;
	if (snapshot.m_bHasHash)
		EraseUploadWaitingIndexEntries(m_mapWaitingClientsByHash, snapshot.m_hashKey, uRuntimeID);
	for (std::vector<SUploadWaitingEndpointKey>::const_iterator it = snapshot.m_aEndpointKeys.begin(); it != snapshot.m_aEndpointKeys.end(); ++it)
		EraseUploadWaitingIndexEntries(m_mapWaitingClientsByEndpoint, *it, uRuntimeID);
	for (std::vector<SUploadWaitingEndpointKey>::const_iterator it = snapshot.m_aUDPKeys.begin(); it != snapshot.m_aUDPKeys.end(); ++it)
		EraseUploadWaitingIndexEntries(m_mapWaitingClientsByUDP, *it, uRuntimeID);
	for (std::vector<SUploadWaitingIdPortKey>::const_iterator it = snapshot.m_aIdPortKeys.begin(); it != snapshot.m_aIdPortKeys.end(); ++it)
		EraseUploadWaitingIndexEntries(m_mapWaitingClientsByIdPort, *it, uRuntimeID);
	if (snapshot.m_bHasLowId)
		EraseUploadWaitingIndexEntries(m_mapWaitingClientsByLowId, snapshot.m_lowIdKey, uRuntimeID);
	for (std::vector<CAddress>::const_iterator it = snapshot.m_aIPKeys.begin(); it != snapshot.m_aIPKeys.end(); ++it)
		EraseUploadWaitingIndexEntries(m_mapWaitingClientsByIP, *it, uRuntimeID);
}

void CUploadQueue::RegisterWaitingClientIndexes(CUpDownClient *client, DWORD uRuntimeID)
{
	if (client == NULL || uRuntimeID == 0)
		return;

	SUploadWaitingIndexSnapshot snapshot;
	BuildWaitingClientIndexSnapshot(client, snapshot);
	if (snapshot.m_bHasHash)
		m_mapWaitingClientsByHash.insert(std::make_pair(snapshot.m_hashKey, uRuntimeID));
	for (std::vector<SUploadWaitingEndpointKey>::const_iterator it = snapshot.m_aEndpointKeys.begin(); it != snapshot.m_aEndpointKeys.end(); ++it)
		m_mapWaitingClientsByEndpoint.insert(std::make_pair(*it, uRuntimeID));
	for (std::vector<SUploadWaitingEndpointKey>::const_iterator it = snapshot.m_aUDPKeys.begin(); it != snapshot.m_aUDPKeys.end(); ++it)
		m_mapWaitingClientsByUDP.insert(std::make_pair(*it, uRuntimeID));
	for (std::vector<SUploadWaitingIdPortKey>::const_iterator it = snapshot.m_aIdPortKeys.begin(); it != snapshot.m_aIdPortKeys.end(); ++it)
		m_mapWaitingClientsByIdPort.insert(std::make_pair(*it, uRuntimeID));
	if (snapshot.m_bHasLowId)
		m_mapWaitingClientsByLowId.insert(std::make_pair(snapshot.m_lowIdKey, uRuntimeID));
	for (std::vector<CAddress>::const_iterator it = snapshot.m_aIPKeys.begin(); it != snapshot.m_aIPKeys.end(); ++it)
		m_mapWaitingClientsByIP.insert(std::make_pair(*it, uRuntimeID));
	m_mapWaitingClientIndexSnapshots[uRuntimeID] = snapshot;
}

void CUploadQueue::UnregisterWaitingClientIndexes(CUpDownClient *client, DWORD uRuntimeID)
{
	if (uRuntimeID == 0)
		return;

	std::map<DWORD, SUploadWaitingIndexSnapshot>::iterator itSnapshot = m_mapWaitingClientIndexSnapshots.find(uRuntimeID);
	if (itSnapshot != m_mapWaitingClientIndexSnapshots.end()) {
		EraseWaitingClientIndexSnapshot(uRuntimeID, itSnapshot->second);
		m_mapWaitingClientIndexSnapshots.erase(itSnapshot);
		return;
	}

	if (client == NULL)
		return;
	SUploadWaitingIndexSnapshot snapshot;
	BuildWaitingClientIndexSnapshot(client, snapshot);
	EraseWaitingClientIndexSnapshot(uRuntimeID, snapshot);
}

bool CUploadQueue::IsWaitingClientIndexSnapshotCurrent(CUpDownClient *client, DWORD uRuntimeID) const
{
	if (client == NULL || uRuntimeID == 0)
		return true;

	std::map<DWORD, SUploadWaitingIndexSnapshot>::const_iterator itSnapshot = m_mapWaitingClientIndexSnapshots.find(uRuntimeID);
	if (itSnapshot == m_mapWaitingClientIndexSnapshots.end())
		return false;

	SUploadWaitingIndexSnapshot snapshot;
	BuildWaitingClientIndexSnapshot(client, snapshot);
	return snapshot == itSnapshot->second;
}

void CUploadQueue::RefreshWaitingClient(CUpDownClient *client)
{
	if (client == NULL)
		return;
	const DWORD uRuntimeID = client->GetRuntimeID();
	if (uRuntimeID == 0)
		return;
	if (m_setWaitingClientRuntimeIDs.find(uRuntimeID) == m_setWaitingClientRuntimeIDs.end())
		return;
	if (IsWaitingClientIndexSnapshotCurrent(client, uRuntimeID))
		return;

	UnregisterWaitingClientIndexes(client, uRuntimeID);
	RegisterWaitingClientIndexes(client, uRuntimeID);
}

bool CUploadQueue::IsOnUploadQueue(CUpDownClient *client) const
{
	if (client == NULL)
		return false;
	const DWORD uRuntimeID = client->GetRuntimeID();
	if (uRuntimeID != 0 && m_setWaitingClientRuntimeIDs.find(uRuntimeID) != m_setWaitingClientRuntimeIDs.end())
		return true;

	// Keep legacy safety if the runtime index was not populated by an older path.
	if (uRuntimeID == 0 || static_cast<size_t>(waitinglist.GetCount()) != m_mapWaitingClientPositions.size())
		return waitinglist.Find(client) != NULL;
	return false;
}

UINT CUploadQueue::GetExactWaitingPosition(CUpDownClient *client) const
{
	if (client == NULL)
		return 0;

	UINT rank = 1;
	const UINT myscore = client->GetScore(false);
	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;)
		rank += static_cast<UINT>(waitinglist.GetNext(pos)->GetScore(false) > myscore);
	return min(rank, (UINT)65534u);
}

void CUploadQueue::RestartWaitingRankRecalculation()
{
	m_uWaitingRankRecalcGeneration = m_uWaitingListGeneration;
	m_iWaitingRankRecalcProcessed = 0;
	m_posWaitingRankRecalc = waitinglist.GetHeadPosition();
	m_bWaitingRankRecalcActive = m_posWaitingRankRecalc != NULL && !m_mapWaitingRankRequests.empty();
	for (std::map<DWORD, SWaitingRankRequest>::iterator it = m_mapWaitingRankRequests.begin(); it != m_mapWaitingRankRequests.end(); ++it)
		it->second.m_uRank = 1;
}

bool CUploadQueue::ProcessWaitingRankRecalculationChunk(UINT uMaxClients, UINT& uProcessed, INT_PTR& iRemaining)
{
	uProcessed = 0;
	iRemaining = 0;

	if (m_mapWaitingRankRequests.empty()) {
		m_bWaitingRankRecalcActive = false;
		m_posWaitingRankRecalc = NULL;
		m_iWaitingRankRecalcProcessed = 0;
		return true;
	}

	if (!m_bWaitingRankRecalcActive || m_uWaitingRankRecalcGeneration != m_uWaitingListGeneration)
		RestartWaitingRankRecalculation();

	if (!m_bWaitingRankRecalcActive)
		return true;

	const DWORD dwSliceStart = ::GetTickCount();
	while (m_posWaitingRankRecalc != NULL) {
		if (m_uWaitingRankRecalcGeneration != m_uWaitingListGeneration) {
			RestartWaitingRankRecalculation();
			break;
		}

		CUpDownClient* pClient = waitinglist.GetNext(m_posWaitingRankRecalc);
		++uProcessed;
		++m_iWaitingRankRecalcProcessed;
		if (pClient != NULL) {
			const DWORD uRuntimeID = pClient->GetRuntimeID();
			const uint32 uScore = pClient->GetScore(false);
			for (std::map<DWORD, SWaitingRankRequest>::iterator it = m_mapWaitingRankRequests.begin(); it != m_mapWaitingRankRequests.end(); ++it) {
				if (it->first != uRuntimeID && uScore > it->second.m_uScore && it->second.m_uRank < 65534u)
					++it->second.m_uRank;
			}
		}

		if (uMaxClients != 0 && uProcessed >= uMaxClients)
			break;
		if (uProcessed >= 16 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetUploadTimerMaintenance))
			break;
	}

	if (m_posWaitingRankRecalc != NULL) {
		const INT_PTR iCount = waitinglist.GetCount();
		iRemaining = iCount > m_iWaitingRankRecalcProcessed ? iCount - m_iWaitingRankRecalcProcessed : 0;
		return false;
	}

	for (std::map<DWORD, SWaitingRankRequest>::const_iterator it = m_mapWaitingRankRequests.begin(); it != m_mapWaitingRankRequests.end(); ++it)
		m_mapWaitingRankCache[it->first] = min(it->second.m_uRank, (UINT)65534u);
	m_mapWaitingRankRequests.clear();
	m_bWaitingRankRecalcActive = false;
	m_iWaitingRankRecalcProcessed = 0;
	return true;
}

void CUploadQueue::RestartBestClientRecalculation()
{
	m_uBestClientRecalcGeneration = m_uWaitingListGeneration;
	m_iBestClientRecalcProcessed = 0;
	m_uBestClientRuntimeID = 0;
	m_uBestLowClientRuntimeID = 0;
	m_dwBestClientCacheTick = 0;
	m_uBestClientScore = 0;
	m_uBestLowClientScore = 0;
	m_uBestCooldownClientRuntimeID = 0;
	m_uBestCooldownClientScore = 0;
	m_bBestClientCacheValid = false;
	m_posBestClientRecalc = waitinglist.GetHeadPosition();
	m_bBestClientRecalcActive = m_posBestClientRecalc != NULL;
}

bool CUploadQueue::ProcessBestClientRecalculationChunk(UINT uMaxClients, UINT& uProcessed, INT_PTR& iRemaining)
{
	uProcessed = 0;
	iRemaining = 0;

	if (!m_bBestClientRecalcActive || m_uBestClientRecalcGeneration != m_uWaitingListGeneration)
		RestartBestClientRecalculation();

	if (!m_bBestClientRecalcActive)
		return true;

	const DWORD dwSliceStart = ::GetTickCount();
	const DWORD curTick = dwSliceStart;
	const bool bHighBandwidthPolicyActive = IsHighBandwidthUploadPolicyActive();
	while (m_posBestClientRecalc != NULL) {
		if (m_uBestClientRecalcGeneration != m_uWaitingListGeneration) {
			RestartBestClientRecalculation();
			break;
		}

		CUpDownClient* cur_client = waitinglist.GetNext(m_posBestClientRecalc);
		++uProcessed;
		++m_iBestClientRecalcProcessed;
		if (cur_client != NULL) {
			if ((curTick < cur_client->GetLastUpRequest() + MAX_PURGEQUEUETIME) && theApp.sharedfiles->GetFileByID(cur_client->GetUploadFileID()) != NULL) {
				if (cur_client->GetSendIP())
					cur_client->SendIPChange();
				if (!(cur_client->IsBadClient() && cur_client->m_uPunishment == P_UPLOADBAN)) {
					const uint32 cur_score = cur_client->GetScore(false);
					const DWORD uRuntimeID = cur_client->GetRuntimeID();
					if (!cur_client->HasLowID() || (cur_client->socket && cur_client->socket->IsConnected())) {
						if (bHighBandwidthPolicyActive && IsHighBandwidthUploadRetryCooldownActive(cur_client, curTick)) {
							if (CanProbeHighBandwidthUploadCooldownClient(cur_client, curTick) && cur_score > m_uBestCooldownClientScore) {
								m_uBestCooldownClientScore = cur_score;
								m_uBestCooldownClientRuntimeID = uRuntimeID;
							}
						} else if (cur_score > m_uBestClientScore) {
							m_uBestClientScore = cur_score;
							m_uBestClientRuntimeID = uRuntimeID;
						}
					} else if (!cur_client->m_bAddNextConnect && cur_score > m_uBestLowClientScore) {
						m_uBestLowClientScore = cur_score;
						m_uBestLowClientRuntimeID = uRuntimeID;
					}
				}
			}
		}

		if (uMaxClients != 0 && uProcessed >= uMaxClients)
			break;
		if (uProcessed >= 16 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetUploadTimerMaintenance))
			break;
	}

	if (m_posBestClientRecalc != NULL) {
		const INT_PTR iCount = waitinglist.GetCount();
		iRemaining = iCount > m_iBestClientRecalcProcessed ? iCount - m_iBestClientRecalcProcessed : 0;
		return false;
	}

	const bool bAllowHighBandwidthCooldownProbe = ShouldProbeHighBandwidthUploadCooldownCandidate(curTick);
	uint32 uSelectableScore = m_uBestClientScore;
	if (bAllowHighBandwidthCooldownProbe && m_uBestClientRuntimeID == 0 && m_uBestCooldownClientRuntimeID != 0)
		uSelectableScore = m_uBestCooldownClientScore;

	if (m_uBestLowClientRuntimeID != 0 && m_uBestLowClientScore > uSelectableScore) {
		CUpDownClient *lowclient = ResolveWaitingClientRuntimeID(m_uBestLowClientRuntimeID);
		if (lowclient != NULL)
			lowclient->m_bAddNextConnect = true;
	} else if (bAllowHighBandwidthCooldownProbe && m_uBestClientRuntimeID == 0 && m_uBestCooldownClientRuntimeID != 0) {
		m_uBestClientRuntimeID = m_uBestCooldownClientRuntimeID;
		m_uBestClientScore = m_uBestCooldownClientScore;
	}
	m_bBestClientCacheValid = m_uBestClientRuntimeID != 0;
	m_dwBestClientCacheTick = ::GetTickCount();
	m_bBestClientRecalcActive = false;
	m_iBestClientRecalcProcessed = 0;
	return true;
}

void CUploadQueue::InvalidateMaxClientScoreRecalculation(bool bQueueRefresh)
{
	++m_uWaitingListGeneration;
	m_bMaxClientScoreRecalcActive = false;
	m_posMaxClientScoreRecalc = NULL;
	m_uMaxClientScoreRecalcGeneration = m_uWaitingListGeneration;
	m_iMaxClientScoreRecalcProcessed = 0;
	m_uMaxClientScoreRecalcMax = 0;
	m_bAverageCombinedFilePrioAndCreditRecalcActive = false;
	m_posAverageCombinedFilePrioAndCreditRecalc = NULL;
	m_uAverageCombinedFilePrioAndCreditGeneration = m_uWaitingListGeneration;
	m_iAverageCombinedFilePrioAndCreditProcessed = 0;
	m_fAverageCombinedFilePrioAndCreditSum = 0.0;
	m_mapWaitingRankCache.clear();
	m_bWaitingRankRecalcActive = false;
	m_posWaitingRankRecalc = NULL;
	m_uWaitingRankRecalcGeneration = m_uWaitingListGeneration;
	m_iWaitingRankRecalcProcessed = 0;
	m_bBestClientRecalcActive = false;
	m_bBestClientCacheValid = false;
	m_posBestClientRecalc = NULL;
	m_uBestClientRecalcGeneration = m_uWaitingListGeneration;
	m_iBestClientRecalcProcessed = 0;
	m_uBestClientRuntimeID = 0;
	m_uBestLowClientRuntimeID = 0;
	m_dwBestClientCacheTick = 0;
	m_uBestClientScore = 0;
	m_uBestLowClientScore = 0;
	m_uBestCooldownClientRuntimeID = 0;
	m_uBestCooldownClientScore = 0;
	if (waitinglist.IsEmpty()) {
		m_fAverageCombinedFilePrioAndCredit = 0.0F;
		m_bAverageCombinedFilePrioAndCreditValid = false;
	}
	if (bQueueRefresh) {
		MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceAverageCombinedFilePrioAndCredit);
		if (!thePrefs.TransferFullChunks())
			MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceMaxClientScore);
		if (!m_mapWaitingRankRequests.empty())
			MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceWaitingRankCache);
		if (waitinglist.GetCount() > 2048)
			MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceBestClientInQueue);
	}
}

void CUploadQueue::RestartMaxClientScoreRecalculation()
{
	m_uMaxClientScoreRecalcGeneration = m_uWaitingListGeneration;
	m_iMaxClientScoreRecalcProcessed = 0;
	m_uMaxClientScoreRecalcMax = 0;
	m_imaxscore = 0;
	m_posMaxClientScoreRecalc = waitinglist.GetHeadPosition();
	m_bMaxClientScoreRecalcActive = m_posMaxClientScoreRecalc != NULL;
}

bool CUploadQueue::ProcessMaxClientScoreRecalculationChunk(UINT uMaxClients, UINT& uProcessed, INT_PTR& iRemaining)
{
	uProcessed = 0;
	iRemaining = 0;

	if (thePrefs.TransferFullChunks()) {
		m_bMaxClientScoreRecalcActive = false;
		m_posMaxClientScoreRecalc = NULL;
		m_imaxscore = 0;
		return true;
	}

	if (!m_bMaxClientScoreRecalcActive || m_uMaxClientScoreRecalcGeneration != m_uWaitingListGeneration)
		RestartMaxClientScoreRecalculation();

	if (!m_bMaxClientScoreRecalcActive)
		return true;

	const DWORD dwSliceStart = ::GetTickCount();
	while (m_posMaxClientScoreRecalc != NULL) {
		if (m_uMaxClientScoreRecalcGeneration != m_uWaitingListGeneration) {
			RestartMaxClientScoreRecalculation();
			break;
		}

		CUpDownClient* pClient = waitinglist.GetNext(m_posMaxClientScoreRecalc);
		++uProcessed;
		++m_iMaxClientScoreRecalcProcessed;
		if (pClient != NULL) {
			const uint32 uScore = pClient->GetScore(true, false);
			if (uScore > m_uMaxClientScoreRecalcMax)
				m_uMaxClientScoreRecalcMax = uScore;
		}

		if (uMaxClients != 0 && uProcessed >= uMaxClients)
			break;
		if (uProcessed >= 16 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetUploadTimerMaintenance))
			break;
	}

	if (m_posMaxClientScoreRecalc != NULL) {
		const INT_PTR iCount = waitinglist.GetCount();
		iRemaining = iCount > m_iMaxClientScoreRecalcProcessed ? iCount - m_iMaxClientScoreRecalcProcessed : 0;
		return false;
	}

	m_imaxscore = m_uMaxClientScoreRecalcMax;
	m_bMaxClientScoreRecalcActive = false;
	m_iMaxClientScoreRecalcProcessed = 0;
	m_uMaxClientScoreRecalcMax = 0;
	return true;
}

void CUploadQueue::UpdateMaxClientScore()
{
	RestartMaxClientScoreRecalculation();
	UINT uProcessed = 0;
	INT_PTR iRemaining = 0;
	if (!ProcessMaxClientScoreRecalculationChunk(256, uProcessed, iRemaining) && iRemaining > 0)
		MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceMaxClientScore);
}

bool CUploadQueue::CheckForTimeOver(CUpDownClient *client, CString *pstrReason)
{
	//If we have nobody in the queue, do NOT remove the current uploads.
	//This will save some bandwidth and some unneeded swapping from upload/queue/upload.
	if (waitinglist.IsEmpty() || client->GetFriendSlot())
		return false;

	if (client->HasCollectionUploadSlot()) {
		const CKnownFile *pDownloadingFile = theApp.sharedfiles->GetFileByID(client->requpfileid);
		if (pDownloadingFile == NULL) {
			if (pstrReason != NULL)
				*pstrReason = _T("Collection upload file is no longer shared");
			return true;
		}
		if (CCollection::HasCollectionExtention(pDownloadingFile->GetFileName()) && pDownloadingFile->GetFileSize() < (uint64)MAXPRIORITYCOLL_SIZE)
			return false;
		if (thePrefs.GetLogUlDlEvents())
			AddDebugLogLine(DLP_HIGH, false, _T("%s: Upload session ended - client with Collection Slot tried to request blocks from another file"), client->GetUserName());
		if (pstrReason != NULL)
			*pstrReason = _T("Collection upload requested another file");
		return true;
	}

	const DWORD curTick = ::GetTickCount();
	if (ShouldRecycleSlowHighBandwidthUpload(client, curTick, pstrReason)) {
		if (thePrefs.GetLogUlDlEvents())
			AddDebugLogLine(DLP_LOW, false, _T("%s: Upload session ended to improve upload slot utilization."), (LPCTSTR)EscPercent(client->GetUserName()));
		return true;
	}

	const bool bHighBandwidthPolicyActive = IsHighBandwidthUploadPolicyActive();
	const bool bHighBandwidthUnderfilled = bHighBandwidthPolicyActive && IsHighBandwidthUploadUnderfilled(GetHighBandwidthUploadBudgetBytesPerSec());
	const uint32 uHighBandwidthProductiveThreshold = bHighBandwidthPolicyActive ? GetHighBandwidthProductiveUploadRateThreshold(GetTargetClientDataRate(false)) : 0;
	const CKnownFile *pUploadingFile = theApp.sharedfiles->GetFileByID(client->GetUploadFileID());
	const uint64 uSessionTransferLimit = ResolveUploadSessionTransferLimitBytes(pUploadingFile);
	if (uSessionTransferLimit > 0 && client->GetQueueSessionPayloadUp() > uSessionTransferLimit) {
		const bool bRotateSession = bHighBandwidthPolicyActive
			? ShouldRotateLimitedHighBandwidthUploadSession(ForceNewClient(), bHighBandwidthUnderfilled, client->GetUploadDatarate(), uHighBandwidthProductiveThreshold)
			: CanRotateUploadSession();
		if (bRotateSession) {
			if (thePrefs.GetLogUlDlEvents())
				AddDebugLogLine(DLP_DEFAULT, false, _T("%s: Upload session ended due to session transfer limit (%s)."), (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)CastItoXBytes(uSessionTransferLimit));
			if (pstrReason != NULL)
				*pstrReason = _T("Session transfer limit");
			return true;
		}
	}

	const UINT uSessionTimeLimitSeconds = thePrefs.GetUploadSessionTimeLimitSeconds();
	if (uSessionTimeLimitSeconds > 0 && client->GetUpStartTimeDelay() > SEC2MS(uSessionTimeLimitSeconds)) {
		const bool bRotateSession = bHighBandwidthPolicyActive
			? ShouldRotateLimitedHighBandwidthUploadSession(ForceNewClient(), bHighBandwidthUnderfilled, client->GetUploadDatarate(), uHighBandwidthProductiveThreshold)
			: CanRotateUploadSession();
		if (bRotateSession) {
			if (thePrefs.GetLogUlDlEvents())
				AddDebugLogLine(DLP_LOW, false, _T("%s: Upload session ended due to session time limit %s."), (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)CastSecondsToHM(uSessionTimeLimitSeconds));
			if (pstrReason != NULL)
				*pstrReason = _T("Session time limit");
			return true;
		}
	}

	if (thePrefs.TransferFullChunks()) {
		// Allow the client to download a specified amount per session; but keep a productive high-bandwidth slot during underfill.
		if (client->GetQueueSessionPayloadUp() > SESSIONMAXTRANS) {
			const bool bRotateSession = bHighBandwidthPolicyActive
				? ShouldRotateLimitedHighBandwidthUploadSession(ForceNewClient(), bHighBandwidthUnderfilled, client->GetUploadDatarate(), uHighBandwidthProductiveThreshold)
				: !ForceNewClient();
			if (bRotateSession) {
				if (thePrefs.GetLogUlDlEvents())
					AddDebugLogLine(DLP_DEFAULT, false, _T("%s: Upload session ended due to max transferred amount (%s)"), (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)CastItoXBytes(SESSIONMAXTRANS));
				return true;
			}
		}
	} else {
		// Try to keep the clients from downloading forever; but keep a productive high-bandwidth slot during underfill.
		if (client->GetUpStartTimeDelay() > SESSIONMAXTIME) {
			const bool bRotateSession = bHighBandwidthPolicyActive
				? ShouldRotateLimitedHighBandwidthUploadSession(ForceNewClient(), bHighBandwidthUnderfilled, client->GetUploadDatarate(), uHighBandwidthProductiveThreshold)
				: !ForceNewClient();
			if (bRotateSession) {
				if (thePrefs.GetLogUlDlEvents())
					AddDebugLogLine(DLP_LOW, false, _T("%s: Upload session ended due to max time %s."), (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)CastSecondsToHM(SESSIONMAXTIME / SEC2MS(1)));
				return true;
			}
		}

		// Check if another client has a higher score than the current client
		if (client->GetScore(true, true) < GetMaxClientScore()) {
			if (curTick >= m_dwRemovedClientByScore) {
				if (thePrefs.GetLogUlDlEvents())
					AddDebugLogLine(DLP_VERYLOW, false, _T("%s: Upload session ended due to score."), (LPCTSTR)EscPercent(client->GetUserName()));
				//Set timer to prevent too many upload slots getting kicked due to score.
				//Upload slots are delayed by at least 1 sec, and the max score is reset every 5 sec.
				//So, I choose 6 secs to make sure the max score was updated before doing this again.
				m_dwRemovedClientByScore = curTick + SEC2MS(6);
				return true;
			}
		}
	}

	return false;
}

void CUploadQueue::DeleteAll()
{
	waitinglist.RemoveAll();
	m_setWaitingClientRuntimeIDs.clear();
	m_mapWaitingClientPositions.clear();
	m_mapWaitingClientsByHash.clear();
	m_mapWaitingClientsByEndpoint.clear();
	m_mapWaitingClientsByUDP.clear();
	m_mapWaitingClientsByIdPort.clear();
	m_mapWaitingClientsByLowId.clear();
	m_mapWaitingClientsByIP.clear();
	m_mapWaitingClientIndexSnapshots.clear();
	m_mapUploadRequestAbuseByClientFile.clear();
	m_mapUploadRequestAbuseByIP.clear();
	m_dwLastUploadRequestAbuseCleanupTick = 0;
	m_mapWaitingRankCache.clear();
	m_mapWaitingRankRequests.clear();
	m_bBestClientCacheValid = false;
	m_uBestClientRuntimeID = 0;
	m_dwBestClientCacheTick = 0;
	InvalidateMaxClientScoreRecalculation(false);
	m_csUploadListMainThrdWriteOtherThrdsRead.Lock();
	while (!uploadinglist.IsEmpty())
		delete uploadinglist.RemoveHead();
	while (!m_retiredUploadingList.IsEmpty())
		delete m_retiredUploadingList.RemoveHead();
	m_csUploadListMainThrdWriteOtherThrdsRead.Unlock();
	// PENDING: Remove from UploadBandwidthThrottler as well!
}

UINT CUploadQueue::GetWaitingPosition(CUpDownClient *client)
{
	if (!IsOnUploadQueue(client))
		return 0;

	// Report a rank beyond configured queue size for upload-banned clients
	if (client->IsBadClient() && client->m_uPunishment == P_UPLOADBAN)
		return min((UINT)thePrefs.GetQueueSize() + 1u, (UINT)65534u);

	const INT_PTR iWaitingCount = waitinglist.GetCount();
	if (iWaitingCount <= 2048)
		return GetExactWaitingPosition(client);

	const DWORD uRuntimeID = client->GetRuntimeID();
	if (uRuntimeID != 0) {
		std::map<DWORD, UINT>::const_iterator itCached = m_mapWaitingRankCache.find(uRuntimeID);
		if (itCached != m_mapWaitingRankCache.end())
			return itCached->second;

		if (m_mapWaitingRankRequests.find(uRuntimeID) == m_mapWaitingRankRequests.end()) {
			SWaitingRankRequest request;
			request.m_uRuntimeID = uRuntimeID;
			request.m_uScore = client->GetScore(false);
			m_mapWaitingRankRequests[uRuntimeID] = request;
			RestartWaitingRankRecalculation();
		}

		UINT uProcessed = 0;
		INT_PTR iRemaining = 0;
		if (!ProcessWaitingRankRecalculationChunk(128, uProcessed, iRemaining) && iRemaining > 0)
			MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceWaitingRankCache);

		itCached = m_mapWaitingRankCache.find(uRuntimeID);
		if (itCached != m_mapWaitingRankCache.end())
			return itCached->second;
	}

	return min(static_cast<UINT>(iWaitingCount), (UINT)65534u);
}


void CUploadQueue::MarkUploadTimerMaintenanceJob(EUploadTimerMaintenanceJob eJob)
{
	const int iJob = static_cast<int>(eJob);
	if (iJob < 0 || iJob >= static_cast<int>(UploadTimerMaintenanceCount))
		return;
	m_aUploadTimerMaintenanceJobs[iJob].m_bPending = true;
	m_aUploadTimerMaintenanceJobs[iJob].m_dwQueuedTick = ::GetTickCount();
}

void CUploadQueue::ProcessUploadTimerMaintenanceSlice()
{
	if (theApp.IsClosing())
		return;

	const DWORD dwSliceStart = ::GetTickCount();
	UINT uProcessed = 0;
	const UINT uJobCount = static_cast<UINT>(UploadTimerMaintenanceCount);
	UINT uChecked = 0;
	while (uChecked < uJobCount) {
		const UINT uIndex = m_uNextUploadTimerMaintenanceJob;
		m_uNextUploadTimerMaintenanceJob = (m_uNextUploadTimerMaintenanceJob + 1) % uJobCount;
		++uChecked;

		SUploadTimerMaintenanceJobState &job = m_aUploadTimerMaintenanceJobs[uIndex];
		if (!job.m_bPending)
			continue;

		job.m_bPending = false;
		const DWORD dwJobStart = ::GetTickCount();
		RunUploadTimerMaintenanceJob(static_cast<EUploadTimerMaintenanceJob>(uIndex));
		++uProcessed;

		DWORD dwJobElapsed = 0;
		if (theApp.IsTimeBudgetHardExceeded(dwJobStart, CemuleApp::TimeBudgetUploadTimerMaintenance, &dwJobElapsed) && thePrefs.GetLogUiResponsivenessEvents())
			AddDebugLogLine(DLP_HIGH, false, _T("UploadTimer maintenance job exceeded hard budget. job=%s elapsed=%u\n"), GetUploadTimerMaintenanceJobName(static_cast<EUploadTimerMaintenanceJob>(uIndex)), dwJobElapsed);

		if (uProcessed != 0 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetUploadTimerMaintenance))
			break;
	}

	DWORD dwSliceElapsed = 0;
	if (theApp.IsTimeBudgetHardExceeded(dwSliceStart, CemuleApp::TimeBudgetUploadTimerMaintenance, &dwSliceElapsed))
		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetUploadTimerMaintenance, _T("CUploadQueue::ProcessUploadTimerMaintenanceSlice"), dwSliceElapsed, uProcessed, 0);
}

void CUploadQueue::RunUploadTimerMaintenanceJob(EUploadTimerMaintenanceJob eJob)
{
	switch (eJob) {
	case UploadTimerMaintenanceKadSearchReload:
		if (theApp.searchlist != NULL && theApp.searchlist->m_bKadReloadWaiting) {
			const DWORD curTick = ::GetTickCount();
			if (curTick >= theApp.searchlist->m_dwKadLastReloadTick + KADEMLIASEARCHLISTRELOADELAY) {
				theApp.searchlist->m_dwKadLastReloadTick = curTick;
				theApp.searchlist->m_bKadReloadWaiting = false;
				if (theApp.emuledlg != NULL && theApp.emuledlg->searchwnd != NULL && theApp.emuledlg->searchwnd->m_pwndResults != NULL)
					theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.ReloadList(false, LSF_SELECTION);
			}
		}
		break;
	case UploadTimerMaintenanceClientCredits:
		if (theApp.clientcredits != NULL)
			theApp.clientcredits->Process();
		break;
	case UploadTimerMaintenanceServerList:
		if (theApp.serverlist != NULL)
			theApp.serverlist->Process();
		break;
	case UploadTimerMaintenanceKnownFiles:
		if (theApp.knownfiles != NULL)
			theApp.knownfiles->Process();
		break;
	case UploadTimerMaintenanceFriendList:
		if (theApp.friendlist != NULL)
			theApp.friendlist->Process();
		break;
	case UploadTimerMaintenanceClientList:
		if (theApp.clientlist != NULL)
			theApp.clientlist->Process();
		break;
	case UploadTimerMaintenanceSharedFiles:
		if (theApp.sharedfiles != NULL)
			theApp.sharedfiles->Process();
		break;
	case UploadTimerMaintenanceSearchList:
		if (theApp.searchlist != NULL)
			theApp.searchlist->Process();
		break;
	case UploadTimerMaintenanceKad:
		if (Kademlia::CKademlia::IsRunning()) {
			Kademlia::CKademlia::Process();
			if (Kademlia::CKademlia::GetPrefs()->HasLostConnection()) {
				Kademlia::CKademlia::Stop();
				if (theApp.emuledlg != NULL)
					theApp.emuledlg->ShowConnectionState();
			}
		}
		break;
	case UploadTimerMaintenanceServerConnectNext:
		if (theApp.serverconnect != NULL && theApp.serverconnect->IsConnecting() && !theApp.serverconnect->IsSingleConnect())
			theApp.serverconnect->TryAnotherConnectionRequest();
		break;
	case UploadTimerMaintenanceListenSocketStatus:
		if (theApp.listensocket != NULL)
			theApp.listensocket->UpdateConnectionsStatus();
		break;
	case UploadTimerMaintenanceClipboard:
		if (thePrefs.WatchClipboard4ED2KLinks() && theApp.emuledlg != NULL && !theApp.emuledlg->m_pSplashWnd)
			theApp.SearchClipboard();
		break;
	case UploadTimerMaintenanceServerConnectTimeout:
		if (theApp.serverconnect != NULL && theApp.serverconnect->IsConnecting())
			theApp.serverconnect->CheckForTimeout();
		break;
	case UploadTimerMaintenanceClientListCleanup:
		if (theApp.clientlist != NULL)
			theApp.clientlist->CleanUpClientList();
		break;
	case UploadTimerMaintenanceListenSocketProcess:
		if (theApp.listensocket != NULL)
			theApp.listensocket->Process();
		break;
	case UploadTimerMaintenanceMaxClientScore:
		if (!thePrefs.TransferFullChunks()) {
			UINT uProcessed = 0;
			INT_PTR iRemaining = 0;
			if (!ProcessMaxClientScoreRecalculationChunk(0, uProcessed, iRemaining) && iRemaining > 0)
				MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceMaxClientScore);
		}
		break;
	case UploadTimerMaintenanceAverageCombinedFilePrioAndCredit:
		{
			UINT uProcessed = 0;
			INT_PTR iRemaining = 0;
			if (!ProcessAverageCombinedFilePrioAndCreditChunk(0, uProcessed, iRemaining) && iRemaining > 0)
				MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceAverageCombinedFilePrioAndCredit);
		}
		break;
	case UploadTimerMaintenanceWaitingRankCache:
		{
			UINT uProcessed = 0;
			INT_PTR iRemaining = 0;
			if (!ProcessWaitingRankRecalculationChunk(0, uProcessed, iRemaining) && iRemaining > 0)
				MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceWaitingRankCache);
		}
		break;
	case UploadTimerMaintenanceBestClientInQueue:
		{
			UINT uProcessed = 0;
			INT_PTR iRemaining = 0;
			if (!ProcessBestClientRecalculationChunk(0, uProcessed, iRemaining) && iRemaining > 0)
				MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceBestClientInQueue);
		}
		break;
	case UploadTimerMaintenanceScheduler:
		if (thePrefs.IsSchedulerEnabled() && theApp.scheduler != NULL)
			theApp.scheduler->Check();
		break;
	case UploadTimerMaintenanceTransferListCount:
		if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL && theApp.emuledlg->transferwnd->m_pwndTransfer != NULL)
			theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();
		break;
	case UploadTimerMaintenanceBuddyMatchmaking:
		if (theApp.clientlist != NULL)
			theApp.clientlist->TryRequestEServerBuddy();
		break;
	case UploadTimerMaintenanceWebServerSessions:
		if (thePrefs.GetWSIsEnabled() && theApp.webserver != NULL)
			theApp.webserver->UpdateSessionCount();
		break;
	case UploadTimerMaintenanceKeepAlive:
		if (theApp.serverconnect != NULL)
			theApp.serverconnect->KeepConnectionAlive();
		break;
	case UploadTimerMaintenancePreventStandby:
		if (thePrefs.GetPreventStandby())
			theApp.ResetStandByIdleTimer();
		break;
	case UploadTimerMaintenanceDownloadInspector:
		if ((thePrefs.GetDownloadInspector() > 0 || thePrefs.IsDownloadInspectorInvalidExt()) && theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL && theApp.emuledlg->transferwnd->GetDownloadList() != NULL)
			theApp.emuledlg->transferwnd->GetDownloadList()->DownloadInspector();
		break;
	case UploadTimerMaintenanceAutoQuerySharedFiles:
#ifdef TESTMODE
		if (thePrefs.GetClientHistory() && ++m_uAutoQuerySFCounter >= thePrefs.GetRemoteSharedFilesAutoQueryPeriod()) {
#else
		if (thePrefs.GetClientHistory() && thePrefs.CanSeeShares() == vsfaEverybody && ++m_uAutoQuerySFCounter >= thePrefs.GetRemoteSharedFilesAutoQueryPeriod()) {
#endif
			m_uAutoQuerySFCounter = 0;
			if (theApp.clientlist != NULL)
				theApp.clientlist->AutoQuerySharedFiles();
		}
		break;
	case UploadTimerMaintenanceBackup:
		if (thePrefs.GetBackupPeriodic()) {
			if (!theApp.tLastBackupTime)
				theApp.tLastBackupTime = theApp.GetLastBackupTime();
			if (time(NULL) >= theApp.tLastBackupTime + HR2S(thePrefs.GetBackupPeriod()))
				theApp.Backup(false);
		}
		break;
	case UploadTimerMaintenanceBuddyPings:
		if (theApp.clientlist != NULL)
			theApp.clientlist->ProcessEServerBuddyPings();
		break;
	case UploadTimerMaintenanceKnownMetSaveJob:
		if (theApp.knownfiles != NULL && theApp.knownfiles->HasKnownMetSaveJobPending())
			theApp.QueuePersistenceWorkRequest(CemuleApp::PersistenceCommandSaveKnownFiles, _T("upload-timer-known-met-save-job"));
		break;
	case UploadTimerMaintenanceSharedFilesFound:
		if (theApp.sharedfiles != NULL && theApp.sharedfiles->ShouldProcessFoundFilesTick())
			theApp.sharedfiles->OnSharedFilesFound();
		break;
	case UploadTimerMaintenanceCount:
		break;
	}
}

LPCTSTR CUploadQueue::GetUploadTimerMaintenanceJobName(EUploadTimerMaintenanceJob eJob) const
{
	switch (eJob) {
	case UploadTimerMaintenanceKadSearchReload: return _T("kad-search-reload");
	case UploadTimerMaintenanceClientCredits: return _T("client-credits");
	case UploadTimerMaintenanceServerList: return _T("server-list");
	case UploadTimerMaintenanceKnownFiles: return _T("known-files");
	case UploadTimerMaintenanceFriendList: return _T("friend-list");
	case UploadTimerMaintenanceClientList: return _T("client-list");
	case UploadTimerMaintenanceSharedFiles: return _T("shared-files");
	case UploadTimerMaintenanceSearchList: return _T("search-list");
	case UploadTimerMaintenanceKad: return _T("kad");
	case UploadTimerMaintenanceServerConnectNext: return _T("server-connect-next");
	case UploadTimerMaintenanceListenSocketStatus: return _T("listen-socket-status");
	case UploadTimerMaintenanceClipboard: return _T("clipboard");
	case UploadTimerMaintenanceServerConnectTimeout: return _T("server-connect-timeout");
	case UploadTimerMaintenanceClientListCleanup: return _T("client-list-cleanup");
	case UploadTimerMaintenanceListenSocketProcess: return _T("listen-socket-process");
	case UploadTimerMaintenanceMaxClientScore: return _T("max-client-score");
	case UploadTimerMaintenanceAverageCombinedFilePrioAndCredit: return _T("average-combined-file-prio-credit");
	case UploadTimerMaintenanceWaitingRankCache: return _T("waiting-rank-cache");
	case UploadTimerMaintenanceBestClientInQueue: return _T("best-client-in-queue");
	case UploadTimerMaintenanceScheduler: return _T("scheduler");
	case UploadTimerMaintenanceTransferListCount: return _T("transfer-list-count");
	case UploadTimerMaintenanceBuddyMatchmaking: return _T("buddy-matchmaking");
	case UploadTimerMaintenanceWebServerSessions: return _T("webserver-sessions");
	case UploadTimerMaintenanceKeepAlive: return _T("keep-alive");
	case UploadTimerMaintenancePreventStandby: return _T("prevent-standby");
	case UploadTimerMaintenanceDownloadInspector: return _T("download-inspector");
	case UploadTimerMaintenanceAutoQuerySharedFiles: return _T("auto-query-shared-files");
	case UploadTimerMaintenanceBackup: return _T("backup");
	case UploadTimerMaintenanceBuddyPings: return _T("buddy-pings");
	case UploadTimerMaintenanceKnownMetSaveJob: return _T("known-met-save-job");
	case UploadTimerMaintenanceSharedFilesFound: return _T("shared-files-found");
	case UploadTimerMaintenanceCount: break;
	}
	return _T("unknown");
}

VOID CALLBACK CUploadQueue::UploadTimer(HWND /*hwnd*/, UINT /*uMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/) noexcept
{
	// NOTE: Always handle all type of MFC exceptions in TimerProcs - otherwise we'll get mem leaks
	try {
		// Barry - Don't do anything if the app is shutting down - can cause unhandled exceptions
		if (theApp.IsClosing())
			return;

		// 100ms

		// other threads may have queued up log lines. This prints them.
		theApp.HandleDebugLogQueue();
		theApp.HandleLogQueue();

		theApp.OnUploadTick_100ms_DirWatch();

		struct CurrentParamStruct cur;
		cur.dPingTolerance = (thePrefs.GetDynUpPingTolerance() > 100) ? ((thePrefs.GetDynUpPingTolerance() - 100) / 100.0) : 0;
		cur.uCurUpload = theApp.uploadqueue->GetDatarate();
		cur.uMinUpload = thePrefs.GetMinUpload();
		cur.uMaxUpload = thePrefs.GetEffectiveMaxUpload();
		cur.uPingToleranceMilliseconds = thePrefs.GetDynUpPingToleranceMilliseconds();
		cur.uGoingUpDivider = thePrefs.GetDynUpGoingUpDivider();
		cur.uGoingDownDivider = thePrefs.GetDynUpGoingDownDivider();
		cur.uNumberOfPingsForAverage = thePrefs.GetDynUpNumberOfPings();
		cur.uLowestInitialPingAllowed = 20; // PENDING: Hard coded min pLowestPingAllowed
		cur.bUseMillisecondPingTolerance = thePrefs.IsDynUpUseMillisecondPingTolerance();
		cur.bEnabled = thePrefs.IsDynUpEnabled();

		if (theApp.lastCommonRouteFinder->SetPrefs(cur))
			theApp.emuledlg->SetStatusBarPartsSize();

		// QUIC BBR pacing must not depend on randomized uTP service jitter.
		if (thePrefs.IsEnableNatTraversal())
			CQuicNatSocket::ProcessAllQuicTimers();

		// Service active uTP write buffers on every upload timer tick.
		if (thePrefs.IsEnableNatTraversal())
			CUtpSocket::Process();

		// Keep the existing jittered global uTP timer and maintenance service.
		if (thePrefs.IsEnableNatTraversal()) {
			static DWORD s_dwNextUtpServiceTick = 0;
			static DWORD s_rand = 0;
			DWORD nowTick = ::GetTickCount();
			if ((int)(nowTick - s_dwNextUtpServiceTick) >= 0) {
				if (theApp.clientudp)
					theApp.clientudp->ServiceUtp();
				// Schedule next service in [Min..Max] ms (preferences)
				if (s_rand == 0)
					s_rand = nowTick | 1;
				s_rand = s_rand * 1103515245u + 12345u; // simple LCG
				DWORD jmin = thePrefs.GetNatTraversalJitterMinMs();
				DWORD jmax = thePrefs.GetNatTraversalJitterMaxMs();
				if (jmax < jmin)
					jmax = jmin;
				DWORD range = (jmax > jmin) ? (jmax - jmin) : 1u;
				DWORD jitter = jmin + (s_rand % (range + 1u));
				s_dwNextUtpServiceTick = nowTick + jitter;
			}
		}

		// NAT-T: service rendezvous retry scheduling (lightweight)
		if (thePrefs.IsEnableNatTraversal()) {
			theApp.clientlist->ServiceNatTraversalRetries();
			
			// Service uTP connection timeouts - check for stuck handshakes
			theApp.clientlist->ServiceUtpConnectionTimeouts();
			
			// Service uTP queued packets - flush when sockets are write-ready
			// Called every 100ms tick - each client connection is handled independently
			// This supports multiple concurrent uTP connections reliably
			theApp.clientlist->ServiceUtpQueuedPackets();
		}

		theApp.uploadqueue->Process();
		theApp.downloadqueue->Process();
		if (theApp.DownloadValidator != NULL)
			theApp.DownloadValidator->ProcessReloadMapSlice(false);
		if (thePrefs.ShowOverhead()) {
			theStats.CompUpDatarateOverhead();
			theStats.CompDownDatarateOverhead();
		}

		static bool s_bHadActiveUploads = false;
		static bool s_bHadActiveDownloads = false;
		const bool bHasActiveUploads = theApp.uploadqueue->HasActiveUploads();
		const bool bHasActiveDownloads = (theStats.m_dwOverallStatus & STATE_DOWNLOADING) != 0;
		if ((s_bHadActiveUploads != bHasActiveUploads) || (s_bHadActiveDownloads != bHasActiveDownloads)) {
			theApp.UpdateDisplayedTransferRates();
			theApp.emuledlg->ShowTransferRate(true);
		}
		s_bHadActiveUploads = bHasActiveUploads;
		s_bHadActiveDownloads = bHasActiveDownloads;

		// JumpStart very often!!!
		if (Kademlia::CKademlia::IsRunning())
			Kademlia::CSearchManager::JumpStart();

		// one second
		if (++i1sec >= 10) {
			i1sec = 0;

			theApp.UpdatePublicIPv6();

			theApp.OnUploadTick_1s_DirWatch();

			if (CNetworkInfoDlg::GetActiveInstance()) {
				CNetworkInfoDlg* pDlg = CNetworkInfoDlg::GetActiveInstance();
				if (pDlg->IsWindowVisible() && pDlg->IsAutoRefreshEnabled())
					pDlg->RefreshInfo();
			}

			theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceKadSearchReload);

			// Maintenance jobs are scheduled here and processed by a bounded slice below.
			theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceClientCredits);
			theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceServerList);
			theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceKnownFiles);
			theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceFriendList);
			theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceClientList);
			theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceSharedFiles);
			theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceSearchList);
			theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceKad);
			theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceServerConnectNext);
			theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceListenSocketStatus);
			theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceClipboard);
			theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceServerConnectTimeout);

			theApp.uploadqueue->UpdateDatarates();
			theApp.UpdateDisplayedTransferRates();
			UINT uDisplayedUploadDatarate = 0;
			UINT uDisplayedDownloadDatarate = 0;
			theApp.GetDisplayedTransferRates(uDisplayedUploadDatarate, uDisplayedDownloadDatarate);

			// 2 seconds
			if (++i2sec >= 2) {
				i2sec = 0;

				// Update connection stats...
				theStats.UpdateConnectionStats(uDisplayedUploadDatarate / 1024.0f, uDisplayedDownloadDatarate / 1024.0f);

#ifdef HAVE_WIN7_SDK_H
				if (thePrefs.IsWin7TaskbarGoodiesEnabled())
					theApp.emuledlg->UpdateStatusBarProgress();
#endif
				theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceClientListCleanup);
			}

			// display graphs
			if (thePrefs.GetTrafficOMeterInterval() > 0 && ++igraph >= (uint32)thePrefs.GetTrafficOMeterInterval()) {
				igraph = 0;
				theApp.emuledlg->statisticswnd->SetCurrentRate(uDisplayedUploadDatarate / 1024.0f, uDisplayedDownloadDatarate / 1024.0f);
			}
			if (theApp.emuledlg->activewnd == theApp.emuledlg->statisticswnd && theApp.emuledlg->IsWindowVisible())
				// display stats
				if (thePrefs.GetStatsInterval() > 0 && ++istats >= (uint32)thePrefs.GetStatsInterval()) {
					istats = 0;
					theApp.emuledlg->statisticswnd->ShowStatistics();
				}

			// Refresh the cached queue text at a bounded rate when the transfers window is visible.
			if (theApp.emuledlg->activewnd == theApp.emuledlg->transferwnd && theApp.emuledlg->IsWindowVisible())
				theApp.emuledlg->transferwnd->InvalidateQueueCount(false);

			//save rates every second
			theStats.RecordRate();

			if (thePrefs.GetUITweaksSpeedGraph())
				theApp.emuledlg->SetSpeedGraphLimits();

			theApp.emuledlg->ShowPing();

			bool gotEnoughHosts = theApp.clientlist->GiveClientsForTraceRoute();
			if (!gotEnoughHosts)
				theApp.serverlist->GiveServersForTraceRoute();


			theApp.emuledlg->ShowTransferRate(theApp.emuledlg->IsTrayIconToFlash());

			// *** 5 seconds **********************************************
			if (++i5sec >= 5) {
#ifdef _DEBUG
				if (thePrefs.m_iDbgHeap > 0 && !AfxCheckMemory())
					AfxDebugBreak();
#endif
				i5sec = 0;
				theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceListenSocketProcess);
				theApp.OnlineSig(); // Added By Bouc7

				theApp.OnUploadTick_5s_DirWatch();

				thePrefs.EstimateMaxUploadCap(theApp.uploadqueue->GetDatarate() / 1024);

				if (!thePrefs.TransferFullChunks())
					theApp.uploadqueue->UpdateMaxClientScore();

				// update cat-titles with downloads info only when needed
				if (thePrefs.ShowCatTabInfos() && theApp.emuledlg->activewnd == theApp.emuledlg->transferwnd && theApp.emuledlg->IsWindowVisible())
					theApp.emuledlg->transferwnd->UpdateCatTabTitlesIfDirty(false);

				theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceScheduler);
				theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceTransferListCount);
				theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceBuddyMatchmaking);
				}

			// *** 60 seconds *********************************************
			if (++i60sec >= 60) {
				i60sec = 0;

				theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceWebServerSessions);
				theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceKeepAlive);
				theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenancePreventStandby);
				theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceDownloadInspector);
				theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceAutoQuerySharedFiles);

				// *** 10 minutes *********************************************
				if (++m_uTenMinCounter >= 10) {
					m_uTenMinCounter = 0;
					theApp.ExecuteSaveAppStateCommand(true, _T("UploadTimer"));

					theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceBuddyPings);
				}

				theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceBackup);
			}

			if (++s_uSaveStatistics >= thePrefs.GetStatsSaveInterval()) {
				s_uSaveStatistics = 0;
				theApp.ExecuteSaveStatsCommand(_T("UploadTimer"));
			}
		}

		if (theApp.knownfiles && theApp.knownfiles->HasKnownMetSaveJobPending())
			theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceKnownMetSaveJob);

		if (theApp.sharedfiles && theApp.sharedfiles->ShouldProcessFoundFilesTick())
			theApp.uploadqueue->MarkUploadTimerMaintenanceJob(UploadTimerMaintenanceSharedFilesFound);

		theApp.uploadqueue->ProcessUploadTimerMaintenanceSlice();

		// need more accuracy here; do not rely on the 'i5sec' and 'i60sec' helpers.
		thePerfLog.LogSamples();
	}
	CATCH_DFLT_EXCEPTIONS(_T("CUploadQueue::UploadTimer"))
}

CUpDownClient* CUploadQueue::GetNextClient(const CUpDownClient *lastclient) const
{
	if (waitinglist.IsEmpty())
		return NULL;
	if (!lastclient)
		return waitinglist.GetHead();
	POSITION pos = NULL;
	if (!GetWaitingClientPosition(lastclient, pos)) {
		TRACE("Error: CUploadQueue::GetNextClient");
		return waitinglist.GetHead();
	}
	waitinglist.GetNext(pos);
	return pos ? waitinglist.GetAt(pos) : NULL;
}

void CUploadQueue::UpdateDatarates()
{
	// Calculate average data rate
	const DWORD curTick = ::GetTickCount();
	if (curTick >= m_lastCalculatedDataRateTick + (SEC2MS(1) / 2)) {
		m_lastCalculatedDataRateTick = curTick;

		if (average_dr_list.GetCount() >= 2 && average_tick_list.GetTail() > average_tick_list.GetHead()) {
			DWORD duration = average_tick_list.GetTail() - average_tick_list.GetHead();
			const uint32 uOldDatarate = datarate;
			const uint32 uOldFriendDatarate = friendDatarate;
			datarate = (uint32)(SEC2MS(m_average_dr_sum - average_dr_list.GetHead()) / duration);
			friendDatarate = (uint32)(SEC2MS(average_friend_dr_list.GetTail() - average_friend_dr_list.GetHead()) / duration);
			if (uOldDatarate != datarate || uOldFriendDatarate != friendDatarate)
				theApp.QueueUploadBandwidthSnapshotEvent(_T("upload-datarate-updated"));
		}
	}
}

uint32 CUploadQueue::GetToNetworkDatarate() const
{
	return (datarate > friendDatarate) ? datarate - friendDatarate : 0;
}










uint32 CUploadQueue::GetWaitingUserForFileCount(const CSimpleArray<CObject*> &raFiles, bool bOnlyIfChanged)
{
	if (bOnlyIfChanged && !m_bStatisticsWaitingListDirty)
		return _UI32_MAX;

	m_bStatisticsWaitingListDirty = false;
	uint32 nResult = 0;
	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
		const CUpDownClient *cur_client = waitinglist.GetNext(pos);
		for (int i = raFiles.GetSize(); --i >= 0;)
			nResult += static_cast<uint32>(md4equ(static_cast<CKnownFile*>(raFiles[i])->GetFileHash(), cur_client->GetUploadFileID()));
	}
	return nResult;
}

uint32 CUploadQueue::GetDatarateForFile(const CSimpleArray<CObject*> &raFiles) const
{
	uint32 nResult = 0;
	for (POSITION pos = uploadinglist.GetHeadPosition(); pos != NULL;) {
		const CUpDownClient *cur_client = uploadinglist.GetNext(pos)->m_pClient;
		for (int i = raFiles.GetSize(); --i >= 0;)
			if (md4equ(static_cast<CKnownFile*>(raFiles[i])->GetFileHash(), cur_client->GetUploadFileID()))
				nResult += cur_client->GetUploadDatarate();
	}
	return nResult;
}

const CUploadingPtrList& CUploadQueue::GetUploadListTS(CCriticalSection **outUploadListReadLock)
{
	ASSERT(*outUploadListReadLock == NULL);
	*outUploadListReadLock = &m_csUploadListMainThrdWriteOtherThrdsRead;
	return uploadinglist;
}

UploadingToClient_Struct* CUploadQueue::GetUploadingClientStructByClient(const CUpDownClient *pClient) const
{
	//TODO: Check if this function is too slow for its usage (esp. when rendering the GUI bars)
	//		if necessary we will have to speed it up with an additional map
	for (POSITION pos = uploadinglist.GetHeadPosition(); pos != NULL;) {
		UploadingToClient_Struct *pCurClientStruct = uploadinglist.GetNext(pos);
		if (pCurClientStruct->m_pClient == pClient)
			return pCurClientStruct;
	}
	return NULL;
}

UploadingToClient_Struct::~UploadingToClient_Struct()
{
	if (m_pClient != NULL)
		m_pClient->FlushSendBlocks();

	m_csBlockListsLock.Lock();
	while (!m_BlockRequests_queue.IsEmpty())
		delete m_BlockRequests_queue.RemoveHead();

	while (!m_DoneBlocks_list.IsEmpty())
		delete m_DoneBlocks_list.RemoveHead();
	m_BlockRequests_keys.clear();
	m_DoneBlocks_keys.clear();
	m_csBlockListsLock.Unlock();
}
void CUploadQueue::SaveAppState(bool bAutoSave)
{
	thePrefs.Save();
	theApp.emuledlg->serverwnd->serverlistctrl.SaveSettings(true);
	theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.SaveSettings(true);
	theApp.emuledlg->sharedfileswnd->sharedfilesctrl.SaveSettings(true);
	theApp.emuledlg->transferwnd->m_pwndTransfer->uploadlistctrl.SaveSettings(true);
	theApp.emuledlg->transferwnd->m_pwndTransfer->downloadlistctrl.SaveSettings(true);
	theApp.emuledlg->transferwnd->m_pwndTransfer->queuelistctrl.SaveSettings(true);
	theApp.emuledlg->transferwnd->m_pwndTransfer->clientlistctrl.SaveSettings(true);
	theApp.emuledlg->transferwnd->m_pwndTransfer->downloadclientsctrl.SaveSettings(true);
	theApp.emuledlg->kademliawnd->m_contactListCtrl->SaveSettings(true);
	theApp.emuledlg->kademliawnd->searchList->SaveSettings(true);
	theApp.emuledlg->chatwnd->m_FriendListCtrl.SaveSettings(true);
	theApp.emuledlg->ircwnd->m_wndNicks.SaveSettings(true);
	theApp.emuledlg->ircwnd->m_wndChanList.SaveSettings(true);
	theApp.emuledlg->searchwnd->SaveAllSettings();
	theApp.emuledlg->serverwnd->SaveAllSettings();
	theApp.emuledlg->kademliawnd->SaveAllSettings();
	if (bAutoSave && Kademlia::CKademlia::m_pInstance)
		Kademlia::CKademlia::m_pInstance->m_pRoutingZone->WriteFile();

	if (thePrefs.GetSaveLoadSources() && theApp.downloadqueue != NULL)
		theApp.downloadqueue->QueueDeferredSourceSaves(!bAutoSave);

	if (bAutoSave)
		return; // Below lines are already being called by CUploadQueue::UploadTimer

	static const CemuleApp::EPersistenceCommandType s_aManualAppStatePersistenceCommands[] =
	{
		CemuleApp::PersistenceCommandSaveClientCredits,
		CemuleApp::PersistenceCommandSaveServerList,
		CemuleApp::PersistenceCommandSaveKnownFiles,
		CemuleApp::PersistenceCommandSaveFriends,
		CemuleApp::PersistenceCommandSaveClientHistory,
		CemuleApp::PersistenceCommandSaveSearchStore,
		CemuleApp::PersistenceCommandSaveSearchSpam,
		CemuleApp::PersistenceCommandSaveKadNodes
	};
	for (int i = 0; i < _countof(s_aManualAppStatePersistenceCommands); ++i) {
		if (s_aManualAppStatePersistenceCommands[i] == CemuleApp::PersistenceCommandSaveClientHistory && !thePrefs.GetClientHistory())
			continue;
		theApp.ExecuteSavePersistenceFileCommand(s_aManualAppStatePersistenceCommands[i], _T("UploadQueue::SaveAppState"));
	}

	theApp.QueueLogLine(true, GetResString(_T("APP_STATE_SAVED")));
}

void CUploadQueue::SaveClientCreditList()
{
	if (theApp.clientcredits != NULL)
		theApp.clientcredits->SaveList();
}
