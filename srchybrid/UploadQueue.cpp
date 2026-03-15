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
#include "KnownFile.h"
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
#include "SearchList.h"
#include "MenuCmds.h"
#include "KadContactListCtrl.h"
#include "KadSearchListCtrl.h"
#include "Kademlia/Routing/RoutingZone.h"
#include "Kademlia/Kademlia/SearchManager.h" // Needed for the relocation of the JumpStart call!!!
#include "ClientUDPSocket.h"
#include "NetworkInfoDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


static uint32 i1sec, i2sec, i5sec, i60sec;
static UINT s_uSaveStatistics = 0;
static uint32 igraph, istats;
static uint32 m_uAutoQuerySFCounter;
static uint32 m_uTenMinCounter;


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
	, m_dwLastResortedUploadSlots()
	, m_bStatisticsWaitingListDirty(true)
	, m_bHasActiveUploads(false)
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
CUpDownClient* CUploadQueue::FindBestClientInQueue()
{
	uint32 bestscore = 0;
	uint32 bestlowscore = 0;
	CUpDownClient *newclient = NULL;
	CUpDownClient *lowclient = NULL;
	const DWORD curTick = ::GetTickCount();

	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		CUpDownClient *cur_client = waitinglist.GetNext(pos);
		//While we are going through this list. Lets check if a client appears to have left the network.
		ASSERT(cur_client->GetLastUpRequest());
		if ((curTick >= cur_client->GetLastUpRequest() + MAX_PURGEQUEUETIME) || !theApp.sharedfiles->GetFileByID(cur_client->GetUploadFileID())) {
			//This client has either not been seen in a long time, or we no longer share the file he wanted any more.
			cur_client->ClearWaitStartTime();
			RemoveFromWaitingQueue(pos2, true);
		} else {
			// finished clearing

			// Once we are here we cleaned up the waiting queue and can proceed sending
			// our ip to every client that did not get it.
			// This function should be called often enough and even if it isn't, waiting
			// clients usually do not get to know our new IP!
			if (cur_client->GetSendIP())
				cur_client->SendIPChange();

			uint32 cur_score = cur_client->GetScore(false);

			if (cur_score > bestscore) {
				// cur_client is more worthy than current best client that is ready to go (connected).
				if (!cur_client->HasLowID() || (cur_client->socket && cur_client->socket->IsConnected())) {
					// this client is a HighID or a lowID client that is ready to go (connected)
					// and it is more worthy
					bestscore = cur_score;
					newclient = cur_client;
				} else if (!cur_client->m_bAddNextConnect) {
					// this client is a lowID client that is not ready to go (not connected)

					// now that we know this client is not ready to go, compare it to the best not ready client
					// the best not ready client may be better than the best ready client, so we need to check
					// against that client
					if (cur_score > bestlowscore) {
						// it is more worthy, keep it
						bestlowscore = cur_score;
						lowclient = cur_client;
					}
				}
			}
		}
	}

	if (lowclient && bestlowscore > bestscore)
		lowclient->m_bAddNextConnect = true;

	return newclient;
}

void CUploadQueue::InsertInUploadingList(CUpDownClient *newclient, bool bNoLocking)
{
	UploadingToClient_Struct *pNewClientUploadStruct = new UploadingToClient_Struct;
	pNewClientUploadStruct->m_pClient = newclient;
	InsertInUploadingList(pNewClientUploadStruct, bNoLocking);
}

void CUploadQueue::InsertInUploadingList(UploadingToClient_Struct *pNewClientUploadStruct, bool bNoLocking)
{
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
}

bool CUploadQueue::AddUpNextClient(LPCTSTR pszReason, CUpDownClient *directadd)
{
	CUpDownClient *newclient = directadd;
	// select next client or use given client
	if (newclient == NULL) {
		newclient = FindBestClientInQueue();
		if (newclient == NULL)
			return false;
		// If the best candidate is upload-banned, try to pick another eligible client
		if (newclient->IsBadClient() && newclient->m_uPunishment == P_UPLOADBAN) {
			CUpDownClient *alt = NULL;
			uint32 bestscore = 0;
			for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
				CUpDownClient *cur = waitinglist.GetNext(pos);
				if (cur->IsBadClient() && cur->m_uPunishment == P_UPLOADBAN)
					continue;

				uint32 score = cur->GetScore(false);
				if (score > bestscore) {
					bestscore = score;
					alt = cur;
				}
			}

			if (alt != NULL)
				newclient = alt;
			else
				return false; // all candidates are upload-banned or none available
		}
	}

	// For direct adds, never start upload for upload-banned clients
	if (directadd != NULL && newclient->IsBadClient() && newclient->m_uPunishment == P_UPLOADBAN)
		return false;

	RemoveFromWaitingQueue(newclient, true);
	theApp.emuledlg->transferwnd->ShowQueueCount(waitinglist.GetCount());

	if (!thePrefs.TransferFullChunks())
		UpdateMaxClientScore(); // refresh score caching, now that the highest score is removed

	if (IsDownloading(newclient))
		return false;

	if (pszReason && thePrefs.GetLogUlDlEvents())
		AddDebugLogLine(false, _T("Adding client to upload list: %s Client: %s"), pszReason, (LPCTSTR)EscPercent(newclient->DbgGetClientInfo()));

	if (newclient->HasCollectionUploadSlot() && directadd == NULL) {
		ASSERT(0);
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

	InsertInUploadingList(newclient, false);

	m_nLastStartUpload = ::GetTickCount();

	// statistic
	CKnownFile *reqfile = theApp.sharedfiles->GetFileByID((uchar*)newclient->GetUploadFileID());
	if (reqfile)
		reqfile->statistic.AddAccepted();

	theApp.emuledlg->transferwnd->GetUploadList()->AddClient(newclient);

	return true;
}

void CUploadQueue::UpdateActiveClientsInfo(DWORD curTick)
{
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
	const DWORD curTick = ::GetTickCount();
	const DWORD staleConnectingSlotTimeout = CONNECTION_TIMEOUT + SEC2MS(20);
	bool bHasActiveUploads = false;
	UpdateActiveClientsInfo(curTick);

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
				&& cur_client->socket->HaveUtpLayer()
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

			if (bKeepConnectingSlot)
				continue;

			if (thePrefs.GetLogNatTraversalEvents()) {
				AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][UploadQueue] Dropping stale upload slot (socketReady=%d, state=%d) for %s"),
					bSocketReady ? 1 : 0, (int)cur_client->GetUploadState(), (LPCTSTR)EscPercent(cur_client->DbgGetClientInfo()));
			}

			RemoveFromUploadQueue(cur_client, _T("Uploading to client with stale socket/handshake (CUploadQueue::Process)"), true, true);
			if (cur_client->Disconnected(_T("CUploadQueue::Process stale upload slot")))
				delete cur_client;
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
		if (CheckForTimeOver(cur_client)) {
			RemoveFromUploadQueue(cur_client, _T("Completed transfer"), true);
			cur_client->SendOutOfPartReqsAndAddToWaitingQueue();
			continue;
		}
		// Increase the sockets buffer for fast uploads (was in UpdateUploadingStatisticsData()).
		// This should be done in the throttling thread, but the throttler
		// does not have access to the client's download rate
		if (cur_client->GetUploadDatarate() > 100 * 1024) {
			CEMSocket *sock = cur_client->GetFileUploadSocket();
			if (sock)
				sock->UseBigSendBuffer();
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

	if (curUploadSlots < max(MIN_UP_CLIENTS_ALLOWED, 4))
		return true;
	if (curUploadSlots >= MAX_UP_CLIENTS_ALLOWED)
		return false;

	uint32 MaxSpeed;
	if (thePrefs.IsDynUpEnabled())
		MaxSpeed = theApp.lastCommonRouteFinder->GetUpload() / 1024;
	else
		MaxSpeed = thePrefs.GetMaxUpload();
	uint32 TargetRate = GetTargetClientDataRate(false);

	if (curUploadSlots >= (INT_PTR)(datarate / GetTargetClientDataRate(true)) || curUploadSlots >= (INT_PTR)(MaxSpeed * 1024 / TargetRate))
		return false;

	return MaxSpeed != UNLIMITED
		|| thePrefs.IsDynUpEnabled()
		|| thePrefs.GetMaxGraphUploadRate(true) <= 0
		|| curUploadSlots < (INT_PTR)(thePrefs.GetMaxGraphUploadRate(false) * 1024 / TargetRate);
}

uint32 CUploadQueue::GetTargetClientDataRate(bool bMinDatarate) const
{
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

bool CUploadQueue::ForceNewClient(bool allowEmptyWaitingQueue)
{
	if (!allowEmptyWaitingQueue && waitinglist.IsEmpty())
		return false;

	INT_PTR curUploadSlots = uploadinglist.GetCount();
	if (curUploadSlots < MIN_UP_CLIENTS_ALLOWED)
		return true;

	if (::GetTickCount() < m_nLastStartUpload + SEC2MS(1) && datarate < 102400)
		return false;

	if (!AcceptNewClient(curUploadSlots) || !theApp.lastCommonRouteFinder->AcceptNewClient()) // UploadSpeedSense can veto a new slot if USS enabled
		return false;

	uint32 MaxSpeed;
	if (thePrefs.IsDynUpEnabled())
		MaxSpeed = theApp.lastCommonRouteFinder->GetUpload() / 1024;
	else
		MaxSpeed = thePrefs.GetMaxUpload();

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
	CUpDownClient *pMatchingIPClient = NULL;
	uint32 cMatches = 0;
	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *cur_client = waitinglist.GetNext(pos);
		if ((IP.GetType() == CAddress::IPv6 ? IP == cur_client->GetIPv6() : IP == cur_client->GetIP()) && nUDPPort == cur_client->GetUDPPort())
			return cur_client;
		if ((IP.GetType() == CAddress::IPv6 ? IP == cur_client->GetIPv6() : IP == cur_client->GetIP()) && bIgnorePortOnUniqueIP) {
			pMatchingIPClient = cur_client;
			++cMatches;
		}
	}
	if (pbMultipleIPs != NULL)
		*pbMultipleIPs = cMatches > 1;

	if (pMatchingIPClient != NULL && cMatches == 1)
		return pMatchingIPClient;
	return NULL;
}

CUpDownClient* CUploadQueue::GetWaitingClientByIP(const CAddress& IP) const
{
	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *cur_client = waitinglist.GetNext(pos);
		if (IP.GetType() == CAddress::IPv6 ? IP == cur_client->GetIPv6() : IP == cur_client->GetIP())
			return cur_client;
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

	uint16 cSameIP = 0;
	// check for duplicates
	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		CUpDownClient *cur_client = waitinglist.GetNext(pos);
		if (cur_client == client) {
			if (client->m_bAddNextConnect && AcceptNewClient(client->m_bAddNextConnect)) {
				//Special care is given to lowID clients that missed their upload slot
				//due to the saving bandwidth on callbacks.
				if (thePrefs.GetLogUlDlEvents())
					AddDebugLogLine(false, _T("Adding ****lowid when reconnecting. Client: %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
				client->m_bAddNextConnect = false;
				RemoveFromWaitingQueue(client, true);
				// statistic values // TODO: Maybe we should change this to count each request for a file only once and ignore re-asks
				CKnownFile *reqfile = theApp.sharedfiles->GetFileByID((uchar*)client->GetUploadFileID());
				if (reqfile)
					reqfile->statistic.AddRequest();
				AddUpNextClient(_T("Adding ****lowid when reconnecting."), client);
				CKnownFile* oldreqfile = theApp.sharedfiles->GetFileByID(client->GetOldUploadFileID());
				if (reqfile != oldreqfile)
					client->SetOldUploadFileID();
			} else {
				client->SendRankingInfo();
				theApp.emuledlg->transferwnd->GetQueueList()->RefreshClient(client);
			}
			return;
		}
		if (client->Compare(cur_client)) {
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
				RemoveFromWaitingQueue(pos2, true);
				if (!cur_client->socket && cur_client->Disconnected(_T("AddClientToQueue - same userhash 2")))
					delete cur_client;
				return;
			}
			//client has a valid secure hash, add him and remove the other one
			if (thePrefs.GetVerbose())
				AddProtectionLogLine(false, (LPCTSTR)GetResString(_T("SAMEUSERHASH")), (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)EscPercent(cur_client->GetUserName()), (LPCTSTR)EscPercent(cur_client->GetUserName()));
			RemoveFromWaitingQueue(pos2, true);
			if (!cur_client->socket && cur_client->Disconnected(_T("AddClientToQueue - same userhash 1")))
				delete cur_client;
		} else if (client->GetIP() == cur_client->GetIP()) {
			// same IP, different port, different userhash
			++cSameIP;
		}
	}
	if (cSameIP >= 3) {
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
	CKnownFile *reqfile = theApp.sharedfiles->GetFileByID((uchar*)client->GetUploadFileID());
	if (reqfile)
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
	if (bHasUploadSlot) {
		if (client->socket == NULL || !client->socket->IsConnected() || !client->CheckHandshakeFinished()) {
			if (thePrefs.GetLogNatTraversalEvents()) {
				AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][UploadQueue] Upload slot exists but socket not ready; deferring OP_ACCEPTUPLOADREQ for %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
			}
			client->SetUploadState(US_CONNECTING);
			client->TryToConnect(true);
			return;
		}

		// he's already downloading and probably only wants another file
		// Do not start additional uploads for upload-banned clients
		if (client->IsBadClient() && client->m_uPunishment == P_UPLOADBAN)
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
					const CString sExpectedName = pOldFile ? pOldFile->GetFileName() : EMPTY;
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
	if (waitinglist.IsEmpty() && ForceNewClient(true) && !(client->IsBadClient() && client->m_uPunishment == P_UPLOADBAN)) {
		client->SetOldUploadFileID();
		client->SetWaitStartTime();
		AddUpNextClient(_T("Direct add with empty queue."), client);
	} else {
		m_bStatisticsWaitingListDirty = true;
		waitinglist.AddTail(client);
		client->SetUploadState(US_ONUPLOADQUEUE);
		theApp.emuledlg->transferwnd->GetQueueList()->AddClient(client, true);
		theApp.emuledlg->transferwnd->ShowQueueCount(waitinglist.GetCount());
		client->SendRankingInfo();
		client->SetOldUploadFileID();
	}
}

float CUploadQueue::GetAverageCombinedFilePrioAndCredit()
{
	const DWORD curTick = ::GetTickCount();

	if (curTick >= m_dwLastCalculatedAverageCombinedFilePrioAndCredit + SEC2MS(5)) {
		m_dwLastCalculatedAverageCombinedFilePrioAndCredit = curTick;

		// TODO: is there a risk of overflow? I don't think so...
		float sum = 0;
		for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;)
			sum += waitinglist.GetNext(pos)->GetCombinedFilePrioAndCredit();

		m_fAverageCombinedFilePrioAndCredit = sum / waitinglist.GetCount();
	}

	return m_fAverageCombinedFilePrioAndCredit;
}

bool CUploadQueue::RemoveFromUploadQueue(CUpDownClient *client, LPCTSTR pszReason, bool updatewindow, bool earlyabort)
{
	bool result = false;
	uint32 slotCounter = 1;
	for (POSITION pos = uploadinglist.GetHeadPosition(); pos != NULL;) {
		POSITION curPos = pos;
		UploadingToClient_Struct *curClientStruct = uploadinglist.GetNext(pos);
		if (client == curClientStruct->m_pClient) {
			if (updatewindow)
				theApp.emuledlg->transferwnd->GetUploadList()->RemoveClient(client);

			if (thePrefs.GetLogUlDlEvents()) {
				AddDebugLogLine(DLP_DEFAULT, false, _T("Removing client from upload list: %s Client: %s Transferred: %s SessionUp: %s QueueSessionPayload: %s In buffer: %s Req blocks: %i File: %s")
					, pszReason == NULL ? EMPTY : (LPCTSTR)EscPercent(pszReason)
					, (LPCTSTR)client->DbgGetClientInfo()
					, (LPCTSTR)CastSecondsToHM(client->GetUpStartTimeDelay() / SEC2MS(1))
					, (LPCTSTR)CastItoXBytes(client->GetSessionUp())
					, (LPCTSTR)CastItoXBytes(client->GetQueueSessionPayloadUp())
					, (LPCTSTR)CastItoXBytes(client->GetPayloadInBuffer()), curClientStruct->m_BlockRequests_queue.GetCount()
					, theApp.sharedfiles->GetFileByID(client->GetUploadFileID()) ? (LPCTSTR)EscPercent(theApp.sharedfiles->GetFileByID(client->GetUploadFileID())->GetFileName()) : EMPTY);
			}
			client->m_bAddNextConnect = false;

			m_csUploadListMainThrdWriteOtherThrdsRead.Lock();
			uploadinglist.RemoveAt(curPos);
			m_csUploadListMainThrdWriteOtherThrdsRead.Unlock();
			delete curClientStruct; // m_csBlockListsLock.Lock();

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
	POSITION pos = waitinglist.Find(client);
	if (pos) {
		RemoveFromWaitingQueue(pos, updatewindow);
		return true;
	}
	return false;
}

void CUploadQueue::RemoveFromWaitingQueue(POSITION pos, bool updatewindow)
{
	m_bStatisticsWaitingListDirty = true;
	CUpDownClient *todelete = waitinglist.GetAt(pos);
	const bool bStillHasUploadSlot = IsDownloading(todelete);
	waitinglist.RemoveAt(pos);
	if (updatewindow) {
		theApp.emuledlg->transferwnd->GetQueueList()->RemoveClient(todelete);
		theApp.emuledlg->transferwnd->ShowQueueCount(waitinglist.GetCount());
	}
	todelete->m_bAddNextConnect = false;
	if (!bStillHasUploadSlot) {
		todelete->SetUploadState(US_NONE);
	} else if (thePrefs.GetLogNatTraversalEvents()) {
		AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][UploadQueue] Preserving upload state while removing wait-queue entry for active uploader %s"),
			(LPCTSTR)EscPercent(todelete->DbgGetClientInfo()));
	}
}

// Remove clients waiting for files no longer shared
void CUploadQueue::PruneWaitersForMissingSharedFiles()
{
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

void CUploadQueue::UpdateMaxClientScore()
{
	m_imaxscore = 0;
	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;) {
		uint32 score = waitinglist.GetNext(pos)->GetScore(true, false);
		if (score > m_imaxscore)
			m_imaxscore = score;
	}
}

bool CUploadQueue::CheckForTimeOver(CUpDownClient *client)
{
	//If we have nobody in the queue, do NOT remove the current uploads.
	//This will save some bandwidth and some unneeded swapping from upload/queue/upload.
	if (waitinglist.IsEmpty() || client->GetFriendSlot())
		return false;

	if (client->HasCollectionUploadSlot()) {
		const CKnownFile *pDownloadingFile = theApp.sharedfiles->GetFileByID(client->requpfileid);
		if (pDownloadingFile == NULL)
			return true;
		if (CCollection::HasCollectionExtention(pDownloadingFile->GetFileName()) && pDownloadingFile->GetFileSize() < (uint64)MAXPRIORITYCOLL_SIZE)
			return false;
		if (thePrefs.GetLogUlDlEvents())
			AddDebugLogLine(DLP_HIGH, false, _T("%s: Upload session ended - client with Collection Slot tried to request blocks from another file"), client->GetUserName());
		return true;
	}

	if (thePrefs.TransferFullChunks()) {
		// Allow the client to download a specified amount per session; but keep going if no one needs this slot
		if (client->GetQueueSessionPayloadUp() > SESSIONMAXTRANS && !ForceNewClient()) {
			if (thePrefs.GetLogUlDlEvents())
				AddDebugLogLine(DLP_DEFAULT, false, _T("%s: Upload session ended due to max transferred amount (%s)"), (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)CastItoXBytes(SESSIONMAXTRANS));
			return true;
		}
	} else {
		// Try to keep the clients from downloading forever; but keep going if no one needs this slot
		if (client->GetUpStartTimeDelay() > SESSIONMAXTIME && !ForceNewClient()) {
			if (thePrefs.GetLogUlDlEvents())
				AddDebugLogLine(DLP_LOW, false, _T("%s: Upload session ended due to max time %s."), (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)CastSecondsToHM(SESSIONMAXTIME / SEC2MS(1)));
			return true;
		}

		// Check if another client has a higher score than the current client
		if (client->GetScore(true, true) < GetMaxClientScore()) {
			const DWORD curTick = ::GetTickCount();
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
	m_csUploadListMainThrdWriteOtherThrdsRead.Lock();
	while (!uploadinglist.IsEmpty())
		delete uploadinglist.RemoveHead();
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

	UINT rank = 1;
	UINT myscore = client->GetScore(false);
	for (POSITION pos = waitinglist.GetHeadPosition(); pos != NULL;)
		rank += static_cast<UINT>(waitinglist.GetNext(pos)->GetScore(false) > myscore);

	return rank;
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
		cur.uMaxUpload = (thePrefs.GetMaxUpload() ? thePrefs.GetMaxUpload() : thePrefs.GetMaxGraphUploadRate(false));
		cur.uPingToleranceMilliseconds = thePrefs.GetDynUpPingToleranceMilliseconds();
		cur.uGoingUpDivider = thePrefs.GetDynUpGoingUpDivider();
		cur.uGoingDownDivider = thePrefs.GetDynUpGoingDownDivider();
		cur.uNumberOfPingsForAverage = thePrefs.GetDynUpNumberOfPings();
		cur.uLowestInitialPingAllowed = 20; // PENDING: Hard coded min pLowestPingAllowed
		cur.bUseMillisecondPingTolerance = thePrefs.IsDynUpUseMillisecondPingTolerance();
		cur.bEnabled = thePrefs.IsDynUpEnabled();

		if (theApp.lastCommonRouteFinder->SetPrefs(cur))
			theApp.emuledlg->SetStatusBarPartsSize();

		// Service uTP write buffers and timers with slight jitter to avoid sync bursts
		if (thePrefs.IsEnableNatTraversal()) {
			static DWORD s_dwNextUtpServiceTick = 0;
			static DWORD s_rand = 0;
			DWORD nowTick = ::GetTickCount();
			if ((int)(nowTick - s_dwNextUtpServiceTick) >= 0) {
				CUtpSocket::Process();
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
		if (thePrefs.ShowOverhead()) {
			theStats.CompUpDatarateOverhead();
			theStats.CompDownDatarateOverhead();
		}

		static bool s_bHadActiveUploads = false;
		static bool s_bHadActiveDownloads = false;
		const bool bHasActiveUploads = theApp.uploadqueue->HasActiveUploads();
		const bool bHasActiveDownloads = (theStats.m_dwOverallStatus & STATE_DOWNLOADING) != 0;
		if ((s_bHadActiveUploads != bHasActiveUploads) || (s_bHadActiveDownloads != bHasActiveDownloads))
			theApp.emuledlg->ShowTransferRate(true);
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

			if (theApp.searchlist->m_bKadReloadWaiting) {
				const DWORD curTick = ::GetTickCount();
				if (curTick >= theApp.searchlist->m_dwKadLastReloadTick + KADEMLIASEARCHLISTRELOADELAY) {
					theApp.searchlist->m_dwKadLastReloadTick = curTick;
					theApp.searchlist->m_bKadReloadWaiting = false;
					theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.ReloadList(false, LSF_SELECTION);
				}
			}

			// try to use different time intervals here to avoid disk I/O congestion by saving all files at once
			theApp.clientcredits->Process();	// 13 minutes
			theApp.serverlist->Process();		// 17 minutes
			theApp.knownfiles->Process();		// 11 minutes
			theApp.friendlist->Process();		// 19 minutes
			theApp.clientlist->Process();
			theApp.sharedfiles->Process();
			theApp.searchlist->Process();		// 12 minutes
			if (Kademlia::CKademlia::IsRunning()) {
				Kademlia::CKademlia::Process();
				if (Kademlia::CKademlia::GetPrefs()->HasLostConnection()) {
					Kademlia::CKademlia::Stop();
					theApp.emuledlg->ShowConnectionState();
				}
			}
			if (theApp.serverconnect->IsConnecting() && !theApp.serverconnect->IsSingleConnect())
				theApp.serverconnect->TryAnotherConnectionRequest();

			theApp.listensocket->UpdateConnectionsStatus();
			if (thePrefs.WatchClipboard4ED2KLinks() && !theApp.emuledlg->m_pSplashWnd) {
				// TODO: Remove this from here. This has to be done with a clipboard chain
				// and *not* with a timer!!
				theApp.SearchClipboard();
			}

			if (theApp.serverconnect->IsConnecting())
				theApp.serverconnect->CheckForTimeout();

			// 2 seconds
			if (++i2sec >= 2) {
				i2sec = 0;

				// Update connection stats...
				theStats.UpdateConnectionStats(theApp.uploadqueue->GetDatarate() / 1024.0f, theApp.downloadqueue->GetDatarate() / 1024.0f);

#ifdef HAVE_WIN7_SDK_H
				if (thePrefs.IsWin7TaskbarGoodiesEnabled())
					theApp.emuledlg->UpdateStatusBarProgress();
#endif
				theApp.clientlist->CleanUpClientList();
			}

			// display graphs
			if (thePrefs.GetTrafficOMeterInterval() > 0 && ++igraph >= (uint32)thePrefs.GetTrafficOMeterInterval()) {
				igraph = 0;
				theApp.emuledlg->statisticswnd->SetCurrentRate(theApp.uploadqueue->GetDatarate() / 1024.0f, theApp.downloadqueue->GetDatarate() / 1024.0f);
			}
			if (theApp.emuledlg->activewnd == theApp.emuledlg->statisticswnd && theApp.emuledlg->IsWindowVisible())
				// display stats
				if (thePrefs.GetStatsInterval() > 0 && ++istats >= (uint32)thePrefs.GetStatsInterval()) {
					istats = 0;
					theApp.emuledlg->statisticswnd->ShowStatistics();
				}

			theApp.uploadqueue->UpdateDatarates();

			//save rates every second
			theStats.RecordRate();

			if (thePrefs.GetUITweaksSpeedGraph())
				theApp.emuledlg->SetSpeedGraphLimits();

			theApp.emuledlg->ShowPing();

			bool gotEnoughHosts = theApp.clientlist->GiveClientsForTraceRoute();
			if (!gotEnoughHosts)
				theApp.serverlist->GiveServersForTraceRoute();


			if (theApp.emuledlg->IsTrayIconToFlash())
			theApp.emuledlg->ShowTransferRate(true);

			// *** 5 seconds **********************************************
			if (++i5sec >= 5) {
#ifdef _DEBUG
				if (thePrefs.m_iDbgHeap > 0 && !AfxCheckMemory())
					AfxDebugBreak();
#endif
				i5sec = 0;
				theApp.listensocket->Process();
				theApp.OnlineSig(); // Added By Bouc7
				if (!theApp.emuledlg->IsTrayIconToFlash())
					theApp.emuledlg->ShowTransferRate();

				theApp.OnUploadTick_5s_DirWatch();

				thePrefs.EstimateMaxUploadCap(theApp.uploadqueue->GetDatarate() / 1024);

				if (!thePrefs.TransferFullChunks())
					theApp.uploadqueue->UpdateMaxClientScore();

				// update cat-titles with downloads info only when needed
				if (thePrefs.ShowCatTabInfos() && theApp.emuledlg->activewnd == theApp.emuledlg->transferwnd && theApp.emuledlg->IsWindowVisible())
					theApp.emuledlg->transferwnd->UpdateCatTabTitlesIfDirty(false);

				if (thePrefs.IsSchedulerEnabled())
					theApp.scheduler->Check();

				theApp.emuledlg->transferwnd->m_pwndTransfer->UpdateListCount();

				// Run buddy matchmaking frequently so we can request right after TCP/Hello is ready.
				theApp.clientlist->TryRequestEServerBuddy();
				}

			// *** 60 seconds *********************************************
			if (++i60sec >= 60) {
				i60sec = 0;

				if (thePrefs.GetWSIsEnabled())
					theApp.webserver->UpdateSessionCount();

				theApp.serverconnect->KeepConnectionAlive();

				if (thePrefs.GetPreventStandby())
					theApp.ResetStandByIdleTimer(); // Reset Windows idle standby timer if necessary

				if (thePrefs.GetFileInspector() > 0 || thePrefs.IsFileInspectorInvalidExt())
					theApp.emuledlg->transferwnd->GetDownloadList()->FileInspector();

#ifdef TESTMODE
				if (thePrefs.GetClientHistory() && ++m_uAutoQuerySFCounter >= thePrefs.GetRemoteSharedFilesAutoQueryPeriod()) {
#else
				if (thePrefs.GetClientHistory() && thePrefs.CanSeeShares() == vsfaEverybody && ++m_uAutoQuerySFCounter >= thePrefs.GetRemoteSharedFilesAutoQueryPeriod()) {
#endif
					m_uAutoQuerySFCounter = 0;
					theApp.clientlist->AutoQuerySharedFiles();
				}

				// *** 10 minutes *********************************************
				if (++m_uTenMinCounter >= 10) {
					m_uTenMinCounter = 0;
					theApp.uploadqueue->SaveAppState(true);

					// Process buddy pings and cleanup (less frequent)
					theApp.clientlist->ProcessEServerBuddyPings();
				}

				if (thePrefs.GetBackupPeriodic()) {
					if (!theApp.tLastBackupTime) // Initialize tLastBackupTime reading from folder name
						theApp.tLastBackupTime = theApp.GetLastBackupTime();

					if (time(NULL) >= theApp.tLastBackupTime + HR2S(thePrefs.GetBackupPeriod()))
						theApp.Backup(false);
				}
			}

			if (++s_uSaveStatistics >= thePrefs.GetStatsSaveInterval()) {
				s_uSaveStatistics = 0;
				thePrefs.SaveStats();
			}
		}

		// Coalesced shared files found processing moved to central tick
		if (theApp.sharedfiles && theApp.sharedfiles->ShouldProcessFoundFilesTick())
			theApp.sharedfiles->OnSharedFilesFound();

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
	POSITION pos = waitinglist.Find(const_cast<CUpDownClient*>(lastclient));
	if (!pos) {
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
			datarate = (uint32)(SEC2MS(m_average_dr_sum - average_dr_list.GetHead()) / duration);
			friendDatarate = (uint32)(SEC2MS(average_friend_dr_list.GetTail() - average_friend_dr_list.GetHead()) / duration);
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
	m_pClient->FlushSendBlocks();

	m_csBlockListsLock.Lock();
	while (!m_BlockRequests_queue.IsEmpty())
		delete m_BlockRequests_queue.RemoveHead();

	while (!m_DoneBlocks_list.IsEmpty())
		delete m_DoneBlocks_list.RemoveHead();
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
	if (Kademlia::CKademlia::m_pInstance)
		Kademlia::CKademlia::m_pInstance->m_pRoutingZone->WriteFile();

	if (thePrefs.GetSaveLoadSources()) {
		for (POSITION pos = theApp.downloadqueue->filelist.GetHeadPosition(); pos != 0;) {
			CPartFile* file = theApp.downloadqueue->filelist.GetNext(pos);
			if (file && !file->IsStopped())
				file->m_sourcesaver.SaveSources(file, true);
		}
	}

	if (bAutoSave)
		return; // Below lines are already being called by CUploadQueue::UploadTimer

	theApp.clientcredits->SaveList();
	theApp.serverlist->SaveServermetToFile();
	theApp.knownfiles->Save();
	theApp.friendlist->SaveList();
	if (thePrefs.GetClientHistory())
		theApp.clientlist->SaveList();
	theApp.searchlist->StoreSearches();
	theApp.searchlist->SaveSpamFilter();

	theApp.QueueLogLine(true, GetResString(_T("APP_STATE_SAVED")));
}
