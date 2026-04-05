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
#include "DebugHelpers.h"
#include "emule.h"
#include "ListenSocket.h"
#include "opcodes.h"
#include "UpDownClient.h"
#include "ClientList.h"
#include "DownloadQueue.h"
#include "Statistics.h"
#include "IPFilter.h"
#include "SharedFileList.h"
#include "PartFile.h"
#include "SafeFile.h"
#include "Packets.h"
#include "OtherFunctions.h"

static void AppendEServerExternalPortTail(CSafeMemFile& data, const CUpDownClient* client)
{
	if (client == NULL || !client->SupportsEServerBuddyExternalUdpPort())
		return;
	if (!client->HasFreshObservedExternalUdpPort())
		return;

	uint16 nObservedPort = client->GetObservedExternalUdpPort();
	if (nObservedPort == 0)
		return;

	data.WriteUInt16(nObservedPort);
	data.WriteUInt8(ESERVER_EXT_UDP_PORT_SRC_ESERVER_BUDDY);
}

static bool AppendEServerPeerInfoProbeToken(CSafeMemFile& data, CUpDownClient* client)
{
	if (client == NULL)
		return false;
	if (client->HasFreshObservedExternalUdpPort())
		return false;

	uint32 dwProbeToken = client->GetEServerExtPortProbeToken();
	if (dwProbeToken == 0) {
		dwProbeToken = GetRandomUInt32();
		if (dwProbeToken == 0)
			dwProbeToken = 1;
		client->SetEServerExtPortProbeToken(dwProbeToken);
	}

	data.WriteUInt32(dwProbeToken);
	return true;
}

#include "UploadQueue.h"
#include "ServerList.h"
#include "Server.h"
#include "ServerConnect.h"
#include "emuledlg.h"
#include "TransferDlg.h"
#include "ClientListCtrl.h"
#include "ChatWnd.h"
#include "Exceptions.h"
#include "Kademlia/Utils/uint128.h"
#include "Kademlia/Kademlia/kademlia.h"
#include "Kademlia/Kademlia/prefs.h"
#include "ClientUDPSocket.h"
#include "SHAHashSet.h"
#include "Log.h"
#include "SearchList.h"

static bool IsClientServerMismatchingCurrentED2KServer(const CUpDownClient* client)
{
	if (client == NULL || theApp.serverconnect == NULL || !theApp.serverconnect->IsConnected() || theApp.serverconnect->GetCurrentServer() == NULL)
		return false;

	const uint32 dwClientServerIP = client->GetServerIP();
	const uint16 nClientServerPort = client->GetServerPort();
	if (dwClientServerIP == 0 || nClientServerPort == 0)
		return false;

	return dwClientServerIP != theApp.serverconnect->GetCurrentServer()->GetIP()
		|| nClientServerPort != theApp.serverconnect->GetCurrentServer()->GetPort();
}

static bool IsClientServerInfoKnown(const CUpDownClient* client)
{
	return client != NULL && client->GetServerIP() != 0 && client->GetServerPort() != 0;
}

static bool IsClientKnownOnCurrentED2KServer(const CUpDownClient* client)
{
	if (!IsClientServerInfoKnown(client))
		return false;

	return !IsClientServerMismatchingCurrentED2KServer(client);
}

static bool IsClientTrustedForPeerInfoFallback(const CUpDownClient* client)
{
	// Accept only the narrow serving-buddy handshake race while we are connecting.
	if (client == NULL || theApp.clientlist == NULL || client->HasLowID() || !client->SupportsEServerBuddy())
		return false;

	if (theApp.clientlist->GetServingEServerBuddy() != client || theApp.clientlist->GetEServerBuddyStatus() != Connecting)
		return false;

	return IsClientKnownOnCurrentED2KServer(client);
}

static const DWORD ESERVER_PENDING_RELAY_OWNER_GRACE_MS = 15000;

static void RegisterEServerProtocolViolation(CUpDownClient* client, LPCTSTR pszContext)
{
	if (client == NULL || pszContext == NULL || theApp.clientlist == NULL)
		return;

	uint32 badRequests = 0;
	if (!client->GetIP().IsNull()) {
		theApp.clientlist->TrackBadRequest(client, 1);
		badRequests = theApp.clientlist->GetBadRequests(client);
	}
	if (thePrefs.GetLogNatTraversalEvents()) {
		DebugLog(_T("[eServerBuddy] Protocol violation (%s) from %s (count=%u)"),
			pszContext,
			(LPCTSTR)EscPercent(client->DbgGetClientInfo()),
			badRequests);
	}
}

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

// CClientReqSocket

IMPLEMENT_DYNCREATE(CClientReqSocket, CEMSocket)

CClientReqSocket::CClientReqSocket(CUpDownClient *in_client)
	: deltimer()
	, m_nOnConnect(SS_Other)
	, deletethis()
	, m_bPortTestCon()
{
	SetClient(in_client);
	theApp.listensocket->AddSocket(this);
	ResetTimeOutTimer();
}

void CClientReqSocket::SetConState(SocketState val)
{
	//If no change, do nothing.
	if ((uint32)val == m_nOnConnect)
		return;
	//Decrease count for the old state
	switch (m_nOnConnect) {
	case SS_Half:
		--theApp.listensocket->m_nHalfOpen;
		break;
	case SS_Complete:
		--theApp.listensocket->m_nComp;
	}
	//Set the new state
	m_nOnConnect = val;
	//Increase count for the new state
	switch (m_nOnConnect) {
	case SS_Half:
		++theApp.listensocket->m_nHalfOpen;
		break;
	case SS_Complete:
		++theApp.listensocket->m_nComp;
	}
}

void CClientReqSocket::WaitForOnConnect()
{
	SetConState(SS_Half);
}

CClientReqSocket::~CClientReqSocket()
{
	//This will update our statistics.
	SetConState(SS_Other);
	if (client)
		client->socket = 0;
	client = NULL;
	theApp.listensocket->RemoveSocket(this);

	DEBUG_ONLY(theApp.clientlist->Debug_SocketDeleted(this));
}

void CClientReqSocket::SetClient(CUpDownClient *pClient)
{
	client = pClient;
	if (client)
		client->socket = this;
}

void CClientReqSocket::ResetTimeOutTimer()
{
	timeout_timer = ::GetTickCount();
}


bool CClientReqSocket::CheckTimeOut()
{
	const DWORD curTick = ::GetTickCount();
	if (m_nOnConnect == SS_Half) {
		//This socket is still in a half connection state. Because of SP2, we don't know
		//if this socket is actually failing, or if this socket is just queued in SP2's new
		//protection queue. Therefore we give the socket a chance to either finally report
		//the connection error, or finally make it through SP2's new queued socket system.
		if (curTick < timeout_timer + CEMSocket::GetTimeOut() * 4)
			return false;
		timeout_timer = curTick;
		CString str;
		str.Format(_T("Timeout: State:%u = SS_Half"), m_nOnConnect);
		//Xman: At a test I found out a second retry brings nothing at this state
		if (client)
			client->m_cFailed = 5;
		Disconnect(str);
		return true;
	}
	DWORD uTimeout = GetTimeOut();

	//Note: the eserver may delay every callback request up to 15 seconds, 
	//so a full symmetric connection attempt with callback from both sides
	//may get delayed up to 30 seconds, as the normal socket timeout is 40
	//seconds we must extend it.
	if (HaveUtpLayer())
		uTimeout += SEC2MS(30);

	bool bEServerBuddyLink = false;
	if (client) {
		if (client->GetKadState() == KS_CONNECTED_BUDDY || theApp.clientlist->IsServedBuddy(client))
			uTimeout += MIN2MS(15);
		else if (client->IsDownloading() && curTick < client->GetUpStartTime() + 4 * CONNECTION_TIMEOUT)
			//TCP flow control might need more time to begin throttling for slow peers
			uTimeout += 4 * CONNECTION_TIMEOUT; //2'30" or slightly more
		else if (client->GetChatState() != MS_NONE)
			//We extend the timeout time here to avoid chatting people from disconnecting too fast.
			uTimeout += CONNECTION_TIMEOUT;

		if (theApp.clientlist && (theApp.clientlist->GetServingEServerBuddy() == client || theApp.clientlist->IsServedEServerBuddy(client))) {
			uTimeout += ESERVERBUDDY_SOCKET_TIMEOUT_GRACE;
			bEServerBuddyLink = true;
		}
	}

	if (curTick < timeout_timer + uTimeout)
		return false;
	timeout_timer = curTick;
	CString str;
	str.Format(_T("Timeout: State:%u (0 = SS_Other, 1 = SS_Half, 2 = SS_Complete)"), m_nOnConnect);
	if (bEServerBuddyLink && client && thePrefs.GetLogNatTraversalEvents())
		AddDebugLogLine(false, _T("[eServerBuddy] Socket timeout reached after %u ms for %s"), uTimeout, (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
	Disconnect(str);
	return true;
}

void CClientReqSocket::OnClose(int nErrorCode)
{
	ASSERT(theApp.listensocket->IsValidSocket(this));
	CEMSocket::OnClose(nErrorCode);

	if (nErrorCode)
		Disconnect(thePrefs.GetVerbose() ? GetErrorMessage(nErrorCode, 1) : NULL);
	else
		Disconnect(_T("Close"));
}

void CClientReqSocket::Disconnect(LPCTSTR pszReason)
{
	CEMSocket::SetConState(EMS_DISCONNECTED);
	AsyncSelect(FD_CLOSE);
	if (client) {
		CString sMsg;
		sMsg.Format(_T("CClientReqSocket::Disconnect(): %s"), pszReason);
		if (client->Disconnected(sMsg, true)) {
			const CUpDownClient *temp = client;
			client->socket = NULL;
			client = NULL;
			delete temp;
		} else
			client = NULL;
	}
	Safe_Delete();
}

void CClientReqSocket::Delete_Timed()
{
// it seems that MFC Sockets call socket functions after they are deleted, even if the socket is closed
// and select(0) is set. So we need to wait some time to make sure this doesn't happen
// we currently also rely on this for multithreading; rework synchronization if this ever changes
	if (::GetTickCount() >= deltimer + SEC2MS(10))
		delete this;
}

void CClientReqSocket::Safe_Delete()
{
	ASSERT(theApp.listensocket->IsValidSocket(this));
	CEMSocket::SetConState(EMS_DISCONNECTED);
	AsyncSelect(FD_CLOSE);
	deltimer = ::GetTickCount();
	if (m_SocketData.hSocket != INVALID_SOCKET || HaveUtpLayer()) // deadlake PROXYSUPPORT - changed to AsyncSocketEx
		ShutDown(CAsyncSocket::both);
	if (client) {
		client->socket = NULL;
		client = NULL;
	}
	deletethis = true;
}

bool CClientReqSocket::ProcessPacket(const BYTE *packet, uint32 size, UINT opcode)
{
	try {
		switch (opcode) {
		case OP_HELLOANSWER:
			theStats.AddDownDataOverheadOther(size);
			client->ProcessHelloAnswer(packet, size);
			client->ProcessBanMessage();
			if (thePrefs.GetDebugClientTCPLevel() > 0) {
				DebugRecv("OP_HelloAnswer", client);
				Debug(_T("  %s\n"), (LPCTSTR)client->DbgGetClientInfo());
			}

			// start secure identification, if
			//  - we have received OP_EMULEINFO and OP_HELLOANSWER (old eMule)
			//	- we have received eMule-OP_HELLOANSWER (new eMule)
			if (client->GetInfoPacketsReceived() == IP_BOTH)
				client->InfoPacketsReceived();

				// Add to served buddy map only for true incoming-served-buddy handshake.
				// KS_CONNECTED_BUDDY is shared by multiple buddy roles and must not auto-promote here.
				if (client && client->HasValidServingBuddyID() && client->GetKadState() == KS_INCOMING_SERVED_BUDDY) {
					theApp.clientlist->AddServedBuddy(client);
				}

			if (client) {
				client->ConnectionEstablished();
				theApp.emuledlg->transferwnd->GetClientList()->RefreshClient(client);
			}
			break;
		case OP_HELLO:
		{
			theStats.AddDownDataOverheadOther(size);

			bool bNewClient = !client;
			if (bNewClient)
				// create new client to save standard information
				client = new CUpDownClient(this);

			bool bIsMuleHello;
			try {
				bIsMuleHello = client->ProcessHelloPacket(packet, size);
			}
			catch (...) {
				if (bNewClient) {
					// Don't let CUpDownClient::Disconnected process a client which is not in the list of clients.
					delete client;
					client = NULL;
				}
				throw;
			}

			if (thePrefs.GetDebugClientTCPLevel() > 0) {
				DebugRecv("OP_Hello", client);
				Debug(_T("  %s\n"), (LPCTSTR)client->DbgGetClientInfo());
			}

		// now we check if we know this client already. if yes this socket will
		// be attached to the known client, the new client will be deleted
		// and the var. "client" will point to the known client.
		// if not we keep our new-constructed client ;)
		if (theApp.clientlist->AttachToAlreadyKnown(&client, this))
		{
			// update the old client informations
			bIsMuleHello = client->ProcessHelloPacket(packet, size);
			client->ProcessBanMessage();
		}
		else if (bNewClient) { //Changed by SiRoB, Optimization
			theApp.clientlist->AddClient(client, true); //Changed by SiRoB, Optimization
			client->SetCommentDirty();
			client->ProcessBanMessage();
		}

		// Add to served buddy map only for true incoming-served-buddy handshake.
		// KS_CONNECTED_BUDDY is shared by multiple buddy roles and must not auto-promote here.
		if (client && client->HasValidServingBuddyID() && client->GetKadState() == KS_INCOMING_SERVED_BUDDY) {
			theApp.clientlist->AddServedBuddy(client);
		}

		if (thePrefs.GetLogNatTraversalEvents()) {
			DebugLog(_T("[NatTraversal][OP_HELLO] Ready to answer hello (socket=%s uTP=%d new=%d), %s"),
				(socket != NULL) ? _T("set") : _T("null"),
				(client && client->socket && client->socket->HaveUtpLayer()) ? 1 : 0,
				bNewClient ? 1 : 0,
				client ? (LPCTSTR)EscPercent(client->DbgGetClientInfo()) : _T("<null>"));
		}

		theApp.emuledlg->transferwnd->GetClientList()->RefreshClient(client);			// send a response packet with standard informations
			if (client->GetHashType() == SO_EMULE && !bIsMuleHello)
				client->SendMuleInfoPacket(false);

			client->SendHelloAnswer();

			if (client)
				client->ConnectionEstablished();

			// eServer Buddy: Check if this incoming client is a pending relay target
			// This handles the case where we (HighID buddy) sent a server callback request
			// and the target LowID client is now connecting to us
			if (client && client->HasLowID() && !theApp.serverconnect->IsLowID()) {
				EServerRelayRequest* pReq = theApp.clientlist->FindPendingEServerRelayByLowID(client->GetUserIDHybrid());
				if (pReq && pReq->pRequester && theApp.clientlist->IsServedEServerBuddy(pReq->pRequester)) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_HELLO: Found pending relay for LowID=%u, sending OP_ESERVER_PEER_INFO to %s"),
							client->GetUserIDHybrid(), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
					}
					
					// Send OP_ESERVER_PEER_INFO to this newly connected client with requester's info.
					if (thePrefs.GetDebugClientTCPLevel() > 0)
						DebugSend("OP_EServerPeerInfo (from Hello)", client);
					CSafeMemFile data_forward(38);
					data_forward.WriteUInt32(pReq->dwRequesterIP);
					data_forward.WriteUInt16(pReq->nRequesterKadPort);
					data_forward.WriteHash16(pReq->pRequester->GetUserHash());
					if (!isnulmd4(pReq->fileHash))
						data_forward.WriteHash16(pReq->fileHash);
					AppendEServerPeerInfoProbeToken(data_forward, client);
					Packet* fwdPacket = new Packet(data_forward, OP_EMULEPROT, OP_ESERVER_PEER_INFO);
					theStats.AddUpDataOverheadOther(fwdPacket->size);
					// Relay handshake control packets must not wait behind throttled traffic.
					SendPacket(fwdPacket, true, 0, true);
					
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_HELLO: Sent OP_ESERVER_PEER_INFO to target - RequesterIP=%s RequesterPort=%u"),
							(LPCTSTR)ipstr(htonl(pReq->dwRequesterIP)), pReq->nRequesterKadPort);
					}

					// ACK may be delayed or lost on unstable paths. Prime relay endpoint from this Hello and
					// try to complete final response early so requester can seed NAT expectation immediately.
					if (client->HasFreshObservedExternalUdpPort()) {
						uint16 nObservedPort = client->GetObservedExternalUdpPort();
						if (nObservedPort != 0)
							pReq->nTargetKadPort = nObservedPort;
					}
					if (pReq->nTargetKadPort == 0) {
						uint16 nHelloPort = client->GetKadPort();
						if (nHelloPort == 0)
							nHelloPort = client->GetUDPPort();
						if (nHelloPort != 0)
							pReq->nTargetKadPort = nHelloPort;
					}
					if (pReq->dwTargetPublicIP == 0) {
						const CAddress& targetIP = client->GetIP();
						if (!targetIP.IsNull() && targetIP.IsPublicIP())
							pReq->dwTargetPublicIP = targetIP.ToUInt32(false);
					}
					uint32 dwEarlyFinalIP = pReq->dwTargetPublicIP;
					uint16 nEarlyFinalPort = pReq->nTargetKadPort;
					if (theApp.clientlist->TrySendPendingEServerRelayResponseForTarget(client)) {
						if (thePrefs.GetLogNatTraversalEvents()) {
							CAddress sentIP(dwEarlyFinalIP, false);
							AddDebugLogLine(false, _T("[eServerBuddy] OP_HELLO: Early final relay response sent for LowID=%u Endpoint=%s:%u"),
								client->GetUserIDHybrid(), (LPCTSTR)ipstr(sentIP), (UINT)nEarlyFinalPort);
						}
					}
				} else if (pReq != NULL) {
					const DWORD dwRelayAge = (DWORD)(::GetTickCount() - pReq->dwRequestTime);
					if (dwRelayAge >= ESERVER_PENDING_RELAY_OWNER_GRACE_MS) {
						theApp.clientlist->RemovePendingEServerRelayByLowID(client->GetUserIDHybrid());
						if (thePrefs.GetLogNatTraversalEvents()) {
							AddDebugLogLine(false, _T("[eServerBuddy] OP_HELLO: Dropped stale pending relay for LowID=%u (age=%u ms)"),
								client->GetUserIDHybrid(), (UINT)dwRelayAge);
						}
					} else {
						if (thePrefs.GetLogNatTraversalEvents()) {
							AddDebugLogLine(false, _T("[eServerBuddy] OP_HELLO: Pending relay owner not ready yet for LowID=%u (age=%u ms), keeping"),
								client->GetUserIDHybrid(), (UINT)dwRelayAge);
						}
					}
				}
			}

			ASSERT(client);
			if (client) {
				// start secure identification, if
				//	- we have received eMule-OP_HELLO (new eMule)
				if (client->GetInfoPacketsReceived() == IP_BOTH)
					client->InfoPacketsReceived();

				if (client->GetKadPort() && client->GetKadVersion() >= KADEMLIA_VERSION2_47a)
					Kademlia::CKademlia::Bootstrap(client->GetIPv4().ToUInt32(true), client->GetKadPort());
			}
		}
		break;
		case OP_REQUESTFILENAME:
		{
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_FileRequest", client, (size >= 16) ? packet : NULL);
			theStats.AddDownDataOverheadFileRequest(size);

			if (size >= 16) {
				if (!client->GetWaitStartTime())
					client->SetWaitStartTime();

				CSafeMemFile data_in(packet, size);
				uchar reqfilehash[MDX_DIGEST_SIZE];
				data_in.ReadHash16(reqfilehash);

				CKnownFile* reqfile = theApp.sharedfiles->GetFileByID(reqfilehash);
				if (reqfile == NULL) {
					reqfile = theApp.downloadqueue->GetFileByID(reqfilehash);
					if (reqfile == NULL || (uint64)((CPartFile*)reqfile)->GetCompletedSize() < PARTSIZE) {
						client->CheckFailedFileIdReqs(reqfilehash);
						break;
					}
				}

				if (reqfile->IsLargeFile() && !client->SupportsLargeFiles()) {
					DebugLogWarning(_T("Client without 64bit file support requested large file; %s, File=\"%s\""), (LPCTSTR)EscPercent(client->DbgGetClientInfo()), (LPCTSTR)EscPercent(reqfile->GetFileName()));
					break;
				}

				// check to see if this is a new file they are asking for
				if (!md4equ(client->GetUploadFileID(), reqfilehash))
					client->SetCommentDirty();
				client->SetUploadFileID(reqfile);

				uint64 nRemaining = data_in.GetLength() - data_in.GetPosition();
				if (nRemaining >= sizeof(uint16)) {
					if (!client->ProcessExtendedInfo(data_in, reqfile)) {
						if (thePrefs.GetDebugClientTCPLevel() > 0)
							DebugSend("OP_FileReqAnsNoFil", client, packet);
						Packet* replypacket = new Packet(OP_FILEREQANSNOFIL, 16);
						md4cpy(replypacket->pBuffer, reqfile->GetFileHash());
						theStats.AddUpDataOverheadFileRequest(replypacket->size);
						SendPacket(replypacket);
						DebugLogWarning(_T("Partcount mismatch on requested file, sending FNF; %s, File=\"%s\""), (LPCTSTR)EscPercent(client->DbgGetClientInfo()), (LPCTSTR)EscPercent(reqfile->GetFileName()));
						break;
					}
				} else {
					if (client->GetExtendedRequestsVersion() > 0 && thePrefs.GetLogNatTraversalEvents())
						AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] OP_FILE_REQUEST: missing extended info payload from %s, skipping extended parsing"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
				}

				// if we are downloading this file, this could be a new source
				// no passive adding of files with only one part
				if (reqfile->IsPartFile() && (uint64)reqfile->GetFileSize() > PARTSIZE)
					if (static_cast<CPartFile*>(reqfile)->GetMaxSources() > static_cast<CPartFile*>(reqfile)->GetSourceCount())
						theApp.downloadqueue->CheckAndAddKnownSource(static_cast<CPartFile*>(reqfile), client, true);

				// send filename etc
				CSafeMemFile data_out(128);
				data_out.WriteHash16(reqfile->GetFileHash());
				data_out.WriteString(reqfile->GetFileName(), client->GetUnicodeSupport());
				Packet* packet1 = new Packet(data_out);
				packet1->opcode = OP_REQFILENAMEANSWER;
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugSend("OP_FileReqAnswer", client, reqfile->GetFileHash());
				theStats.AddUpDataOverheadFileRequest(packet1->size);
				SendPacket(packet1);

				client->SendCommentInfo(reqfile);
				break;
			}
		}
		throw GetResString(_T("ERR_WRONGPACKETSIZE"));
		case OP_SETREQFILEID:
		{
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_SetReqFileID", client, (size >= 16) ? packet : NULL);
			theStats.AddDownDataOverheadFileRequest(size);

			if (size == 16) {
				if (!client->GetWaitStartTime())
					client->SetWaitStartTime();

				CKnownFile* reqfile = theApp.sharedfiles->GetFileByID(packet);

				if (!reqfile)
					reqfile = theApp.downloadqueue->GetFileByID(packet);
				if (reqfile && reqfile->GetPartCount() <= 1)
					reqfile = NULL;
				if (!reqfile || (reqfile->IsLargeFile() && !client->SupportsLargeFiles())) {
					if (thePrefs.GetDebugClientTCPLevel() > 0)
						DebugSend("OP_FileReqAnsNoFil", client, packet);
					Packet* replypacket = new Packet(OP_FILEREQANSNOFIL, 16);
					md4cpy(replypacket->pBuffer, packet);
					theStats.AddUpDataOverheadFileRequest(replypacket->size);
					SendPacket(replypacket);
					if (reqfile)
						DebugLogWarning(_T("Client without 64-bit file support requested large file; %s, File=\"%s\""), (LPCTSTR)client->DbgGetClientInfo(), (LPCTSTR)reqfile->GetFileName());
					else
						client->CheckFailedFileIdReqs(packet);
					break;
				}

				// check to see if this is a new file they are asking for
				if (!md4equ(client->GetUploadFileID(), packet))
					client->SetCommentDirty();

				if (thePrefs.IsDetectFileFaker() && reqfile->IsPartFile() && ((CPartFile*)reqfile)->m_DeadSourceList.IsDeadSource(*client)) { // IsHarderPunishment isn't necessary here since the cost is low
					theApp.shield->SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_FILE_FAKER")), PR_FILEFAKER);
					break;
				}

				client->SetUploadFileID(reqfile);

				// send file status
				CSafeMemFile data(16 + 16);
				data.WriteHash16(reqfile->GetFileHash());
				if (reqfile->IsPartFile())
					static_cast<CPartFile*>(reqfile)->WritePartStatus(data);
				else
					data.WriteUInt16(0);
				Packet* packet2 = new Packet(data);
				packet2->opcode = OP_FILESTATUS;
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugSend("OP_FileStatus", client, reqfile->GetFileHash());
				theStats.AddUpDataOverheadFileRequest(packet2->size);
				SendPacket(packet2);
				if (client->GetIncompletePartVersion() && reqfile->IsPartFile()) { // Don't send on complete files
					CSafeMemFile data(32); //16+16
					data.WriteHash16(reqfile->GetFileHash());
					((CPartFile*)reqfile)->WriteIncPartStatus(&data);
					Packet* packet = new Packet(data, OP_EMULEPROT, OP_FILEINCSTATUS);
					theStats.AddUpDataOverheadFileRequest(packet->size);
					SendPacket(packet, true);
				}
				break;
			}
		}
		throw GetResString(_T("ERR_WRONGPACKETSIZE"));
		case OP_FILEREQANSNOFIL:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_FileReqAnsNoFil", client, (size >= 16) ? packet : NULL);
			theStats.AddDownDataOverheadFileRequest(size);
			if (size == 16) {
				CPartFile* rqfile = theApp.downloadqueue->GetFileByID(packet);
				if (!rqfile) {
					client->CheckFailedFileIdReqs(packet);
					break;
				}
				rqfile->m_DeadSourceList.AddDeadSource(*client);

				if (thePrefs.IsDetectFileFaker())
					client->CheckFileNotFound();

				// if that client does not have my file maybe has another different
				// we try to swap to another file ignoring no needed parts files
				switch (client->GetDownloadState()) {
				case DS_CONNECTED:
				case DS_ONQUEUE:
				case DS_NONEEDEDPARTS:
					client->DontSwapTo(rqfile); // ZZ:DownloadManager
					if (!client->SwapToAnotherFile(_T("Source says it doesn't have the file. CClientReqSocket::ProcessPacket()"), true, true, true, NULL, false, false)) // ZZ:DownloadManager
						theApp.downloadqueue->RemoveSource(client);
				}
				break;
			}
			throw GetResString(_T("ERR_WRONGPACKETSIZE"));
		case OP_REQFILENAMEANSWER:
		{
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_FileReqAnswer", client, (size >= 16) ? packet : NULL);
			theStats.AddDownDataOverheadFileRequest(size);

			CSafeMemFile data(packet, size);
			uchar cfilehash[MDX_DIGEST_SIZE];
			data.ReadHash16(cfilehash);
			CPartFile* file = theApp.downloadqueue->GetFileByID(cfilehash);
			if (file == NULL)
				client->CheckFailedFileIdReqs(cfilehash);
			client->ProcessFileInfo(data, file);
		}
		break;
		case OP_FILESTATUS:
		{
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_FileStatus", client, (size >= 16) ? packet : NULL);
			theStats.AddDownDataOverheadFileRequest(size);

			CSafeMemFile data(packet, size);
			uchar cfilehash[MDX_DIGEST_SIZE];
			data.ReadHash16(cfilehash);
			CPartFile* file = theApp.downloadqueue->GetFileByID(cfilehash);
			if (file == NULL)
				client->CheckFailedFileIdReqs(cfilehash);
			client->ProcessFileStatus(false, data, file);
		}
		break;
		case OP_STARTUPLOADREQ:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_StartUpLoadReq", client, (size >= 16) ? packet : NULL);
			theStats.AddDownDataOverheadFileRequest(size);

			// NAT-T/uTP diagnostics: ensure we see why a start-upload request may be ignored
			if (thePrefs.GetLogNatTraversalEvents()) {
				AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] OP_STARTUPLOADREQ received (size=%u) from %s"), size, (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
			}

			// If handshake not finished yet, queue pending request and process after HelloAnswer
			if (!client->CheckHandshakeFinished()) {
				if (size == 16) {
					client->SetPendingStartUploadReq((const uchar*)packet);
					if (thePrefs.GetLogNatTraversalEvents())
						AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] OP_STARTUPLOADREQ queued pending until HelloAnswer: %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
				}
				break;
			}

			client->IncrementAskedCount();
			client->SetLastUpRequest();
			client->uiULAskingCounter++;
			if (client->IsBanned())
				break;

			if (size == 16) {
				// NAT-T/uTP connections naturally involve multiple retry attempts due to:
				// - NAT hole punching process requiring multiple packets
				// - uTP packet loss and retransmission mechanisms
				// - Connection establishment through intermediary peers
				// Therefore, bypass aggressive check entirely for NAT-T/uTP connections
				bool bHasUtpLayer = (client->socket && client->socket->HaveUtpLayer());
				bool bHasKadPort = (client->GetKadPort() != 0);
				bool bIsNatTraversalConnection = bHasUtpLayer || bHasKadPort;
				
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] OP_STARTUPLOADREQ aggressive check: socket=%s uTP=%d KadPort=%u NAT-T=%d counter=%u for %s"),
						client->socket ? _T("YES") : _T("NULL"),
						bHasUtpLayer ? 1 : 0,
						(unsigned)client->GetKadPort(),
						bIsNatTraversalConnection ? 1 : 0,
						(unsigned)client->uiULAskingCounter,
						(LPCTSTR)EscPercent(client->DbgGetClientInfo()));
				}
				
				if (thePrefs.IsDetectAgressive() && !bIsNatTraversalConnection && 
					client->uiULAskingCounter > uint16(thePrefs.GetAgressiveCounter()) && 
					((time(NULL) - client->tLastSeen) / client->uiULAskingCounter) < uint32(MIN2S((thePrefs.GetAgressiveTime())))) { // IsHarderPunishment isn't necessary here since the cost is low
					if (thePrefs.IsAgressiveLog())
					{
						AddProtectionLogLine(false, _T("OP_STARTUPLOADREQ: (%s/%u)=%s ==> %s(%s) ASK TO FAST!"),
							CastSecondsToHM(time(NULL) - client->tLastSeen),
							client->uiULAskingCounter,
							CastSecondsToHM((time(NULL) - client->tLastSeen) / client->uiULAskingCounter),
							(LPCTSTR)EscPercent(client->GetUserName()),
							(LPCTSTR)EscPercent(client->DbgGetFullClientSoftVer()));
					}
					theApp.shield->SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_AGRESSIVE")), PR_AGGRESSIVE);
				}

				CKnownFile* reqfile = theApp.sharedfiles->GetFileByID(packet);
				if (reqfile) {
					if (!md4equ(client->GetUploadFileID(), packet))
						client->SetCommentDirty();
					client->SetUploadFileID(reqfile);
					client->SendCommentInfo(reqfile);
					if (thePrefs.GetLogNatTraversalEvents())
						AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] Uploader: adding client to upload queue for %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
					theApp.uploadqueue->AddClientToQueue(client);

					// Notify downloader of queue status to prevent retry storm
					if (client->IsDownloading()) {
							if (client->socket != NULL && client->socket->IsConnected() && client->CheckHandshakeFinished()) {
								// Client got upload slot - send accept only when socket and handshake are ready.
								Packet *packet = new Packet(OP_ACCEPTUPLOADREQ, 0);
								theStats.AddUpDataOverheadFileRequest(packet->size);
								if (thePrefs.GetDebugClientTCPLevel() > 0)
									DebugSend("OP_AcceptUploadReq", client);
								client->SendPacket(packet, true);
								if (thePrefs.GetLogNatTraversalEvents())
									AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] Uploader: sent OP_ACCEPTUPLOADREQ to %s (IsDownloading=true)"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
							} else if (thePrefs.GetLogNatTraversalEvents()) {
								AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] Uploader: deferred OP_ACCEPTUPLOADREQ (socket/handshake not ready) for %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
							}
						} else {
							// Client in waiting queue - send ranking info
							client->SendRankingInfo();
							if (thePrefs.GetLogNatTraversalEvents())
								AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] Uploader: sent queue rank to %s (IsDownloading=false)"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
						}
				}
				else
					client->CheckFailedFileIdReqs(packet);
			}
			else if (thePrefs.IsDetectWrongTag()) // ban it as 'WrongTag' - sFrQlXeRt // IsHarderPunishment isn't necessary here since the cost is low
				theApp.shield->SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_WRONG_OPSTARTUPLOADREQ_TAG")), PR_WRONGTAGUPLOADREQ);
			break;
		case OP_QUEUERANK:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_QueueRank", client);
			theStats.AddDownDataOverheadFileRequest(size);
			client->ProcessEdonkeyQueueRank(packet, size);
			break;
		case OP_ACCEPTUPLOADREQ:
			if (thePrefs.GetDebugClientTCPLevel() > 0) {
				DebugRecv("OP_AcceptUploadReq", client, (size >= 16) ? packet : NULL);
				if (size > 0)
					Debug(_T("  ***NOTE: Packet contains %u additional bytes\n"), size);
				Debug(_T("  QR=%d\n"), client->IsRemoteQueueFull() ? UINT_MAX : client->GetRemoteQueueRank());
			}
			theStats.AddDownDataOverheadFileRequest(size);
			client->ProcessAcceptUpload();
			break;
		case OP_REQUESTPARTS:
		{
			// see also OP_REQUESTPARTS_I64
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_RequestParts", client, (size >= 16) ? packet : NULL);
			theStats.AddDownDataOverheadFileRequest(size);

			CSafeMemFile data(packet, size);
			uchar reqfilehash[MDX_DIGEST_SIZE];
			data.ReadHash16(reqfilehash);

			uint32 aOffset[3 * 2]; //3 starts, then 3 ends
			for (unsigned i = 0; i < 3 * 2; ++i)
				aOffset[i] = data.ReadUInt32();

			if (thePrefs.GetDebugClientTCPLevel() > 0)
				for (unsigned i = 0; i < 3; ++i)
					Debug(_T("  Start[%u]=%u  End[%u]=%u  Size=%u\n"), i, aOffset[i], i, aOffset[i + 3], aOffset[i + 3] - aOffset[i]);


			if (client->GetUploadState() == US_BANNED) { //just to be sure
				theApp.uploadqueue->RemoveFromUploadQueue(client, GetResString(_T("BANNED_CLIENT_UPLOAD")));
				client->SetUploadFileID(NULL);
				AddProtectionLogLine(false, GetResString(_T("BANNED_CLIENT_UPLOAD")), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
			}

			if (thePrefs.IsDontAllowFileHotSwapping()) {
				// Close Backdoor v2 (idea Maella)
				//after seeing that many official clients swap the file just when they get an uploadslot
				//I decided to allow the upload if the new requested file
				//has same or higher priority
				//
				// Remark: There is a security leak that a leecher mod could exploit here.
				//         A client might send reqblock for another file than the one it 
				//         was granted to download. As long as the file ID in reqblock
				//         is the same in all reqblocks, it won't be rejected.  
				//         With this a client might be in a waiting queue with a high 
				//         priority but download block of a file set to a lower priority.
				CKnownFile* reqfileNr1 = theApp.sharedfiles->GetFileByID(reqfilehash);
				CKnownFile* reqfileNr2 = theApp.sharedfiles->GetFileByID(client->GetUploadFileID());
				if (reqfileNr1 == NULL) {
					//We don't know the requesting file, this can happen when we delete the file during upload
					//the prevent to run in a file exception when creating next block
					//send a cancel and remove client from queue
					Packet* packet = new Packet(OP_OUTOFPARTREQS, 0);
					theStats.AddUpDataOverheadFileRequest(packet->size);
					client->socket->SendPacket(packet, true, true);
					theApp.uploadqueue->RemoveFromUploadQueue(client, _T("Client requested unknown file"), true);
					client->SetUploadFileID(NULL);
					break;
				}

				if (reqfileNr2 != NULL && reqfileNr1->GetUpPriorityEx() < reqfileNr2->GetUpPriorityEx()) {
					if (thePrefs.GetLogUlDlEvents()) {
						AddProtectionLogLine(false, _T("File hot swapping disallowed [ProcessPacket]: (client=%s, expected=%s, asked=%s)"),
							(LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)EscPercent(reqfileNr2->GetFileName()), (LPCTSTR)EscPercent(reqfileNr1->GetFileName()));
					}
					theApp.uploadqueue->RemoveFromUploadQueue(client, _T("wrong file"), true);
					client->SetUploadFileID(reqfileNr1); //Xman Fix!  (needed for see onUploadqueue)
					client->SendOutOfPartReqsAndAddToWaitingQueue();
					client->SetWaitStartTime(); // Penality (soft punishement)
					break;
				}
				
				if (reqfileNr2 != NULL && reqfileNr2 != reqfileNr1) {
					// Safe logging: expected file may be NULL when it was deleted during upload
					const CString sExpectedName = reqfileNr2 ? reqfileNr2->GetFileName() : _T("<NULL>");
					if (thePrefs.GetLogUlDlEvents())
						AddProtectionLogLine(false, _T("File hot swapping allowed [ProcessPacket]: (client=%s, expected=%s, asked=%s)"),
							(LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)EscPercent(sExpectedName), (LPCTSTR)EscPercent(reqfileNr1->GetFileName()));
				}

			}

			for (unsigned i = 0; i < 3; ++i)
				if (aOffset[i] < aOffset[i + 3]) {
					Requested_Block_Struct* reqblock = new Requested_Block_Struct;
					reqblock->StartOffset = aOffset[i];
					reqblock->EndOffset = aOffset[i + 3];
					md4cpy(reqblock->FileID, reqfilehash);
					reqblock->transferred = 0;
					client->AddReqBlock(reqblock, false);
				}
				else if (thePrefs.GetVerbose() && (aOffset[i + 3] != 0 || aOffset[i] != 0))
					DebugLogWarning(_T("Client requests invalid %u. file block %u-%u (%d bytes): %s"), i, aOffset[i], aOffset[i + 3], aOffset[i + 3] - aOffset[i], (LPCTSTR)EscPercent(client->DbgGetClientInfo()));

			client->AddReqBlock(NULL, true);
			client->SetLastUpRequest();
		}
		break;
		case OP_CANCELTRANSFER:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_CancelTransfer", client);
			theStats.AddDownDataOverheadFileRequest(size);
			theApp.uploadqueue->RemoveFromUploadQueue(client, _T("Remote client cancelled transfer."));
			break;
		case OP_END_OF_DOWNLOAD:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_EndOfDownload", client, (size >= 16) ? packet : NULL);
			theStats.AddDownDataOverheadFileRequest(size);
			if (size >= 16 && md4equ(client->GetUploadFileID(), packet))
				theApp.uploadqueue->RemoveFromUploadQueue(client, _T("Remote client ended transfer."));
			else
				client->CheckFailedFileIdReqs(packet);
			break;
		case OP_HASHSETREQUEST:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_HashSetReq", client, (size >= 16) ? packet : NULL);
			theStats.AddDownDataOverheadFileRequest(size);

			if (size != 16)
				throw GetResString(_T("ERR_WRONGHPACKETSIZE"));
			client->SendHashsetPacket(packet, 16, false);
			break;
		case OP_HASHSETANSWER:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_HashSetAnswer", client, (size >= 16) ? packet : NULL);
			theStats.AddDownDataOverheadFileRequest(size);
			client->ProcessHashSet(packet, size, false);
			break;
		case OP_SENDINGPART:
		{
			// see also OP_SENDINGPART_I64
			if (thePrefs.GetDebugClientTCPLevel() > 1)
				DebugRecv("OP_SendingPart", client, (size >= 16) ? packet : NULL);
			theStats.AddDownDataOverheadFileRequest(16 + 2 * 4);
				EDownloadState newDS = DS_NONE;
				const CPartFile* creqfile = client->GetRequestFile();
				if (creqfile) {
					if (!creqfile->IsStopped() && (creqfile->GetStatus() == PS_READY || creqfile->GetStatus() == PS_EMPTY)) {
						client->ProcessBlockPacket(packet, size, false, false);
						if (!creqfile->IsStopped()) {
							UINT uStatus = creqfile->GetStatus();
							if (uStatus == PS_ERROR)
								newDS = DS_ONQUEUE;
							else if (uStatus == PS_PAUSED || uStatus == PS_INSUFFICIENT)
								newDS = DS_NONE;
							else
								newDS = DS_CONNECTED; //any state but DS_NONE or DS_ONQUEUE
						}
					}
				}
				if (newDS != DS_CONNECTED && client) { //client could have been deleted while debugging
					client->SendCancelTransfer();
					client->SetDownloadState(newDS);
				}
			}
		break;
		case OP_OUTOFPARTREQS:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_OutOfPartReqs", client);
			theStats.AddDownDataOverheadFileRequest(size);
			if (client->GetDownloadState() == DS_DOWNLOADING)
				client->SetDownloadState(DS_ONQUEUE, _T("The remote client decided to stop/complete the transfer (got OP_OutOfPartReqs)."));
			break;
		case OP_CHANGE_CLIENT_ID:
		{
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_ChangedClientID", client);
			theStats.AddDownDataOverheadOther(size);

			CSafeMemFile data(packet, size);
			uint32 nNewUserID = data.ReadUInt32();
			uint32 nNewServerIP = data.ReadUInt32();
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				Debug(_T("  NewUserID=%u (%08x, %s)  NewServerIP=%u (%08x, %s)\n"), nNewUserID, nNewUserID, (LPCTSTR)ipstr(nNewUserID), nNewServerIP, nNewServerIP, (LPCTSTR)ipstr(nNewServerIP));
			if (::IsLowID(nNewUserID)) { // client changed server and has a LowID
				CServer* pNewServer = theApp.serverlist->GetServerByIP(nNewServerIP);
				if (pNewServer != NULL) {
					client->SetUserIDHybrid(nNewUserID); // update UserID only if we know the server
					client->SetServerIP(nNewServerIP);
					client->SetServerPort(pNewServer->GetPort());
				}
			}
			else if (nNewUserID == client->GetIP().ToUInt32(false)) {	// client changed server and has a HighID(IP)
				client->SetUserIDHybrid(ntohl(nNewUserID));
				CServer* pNewServer = theApp.serverlist->GetServerByIP(nNewServerIP);
				if (pNewServer != NULL) {
					client->SetServerIP(nNewServerIP);
					client->SetServerPort(pNewServer->GetPort());
				}
			}
			else if (thePrefs.GetDebugClientTCPLevel() > 0)
				Debug(_T("***NOTE: OP_ChangedClientID unknown contents\n"));

			UINT uAddData = (UINT)(data.GetLength() - data.GetPosition());
			if (uAddData > 0 && thePrefs.GetDebugClientTCPLevel() > 0)
				Debug(_T("***NOTE: OP_ChangedClientID contains add. data %s\n"), (LPCTSTR)DbgGetHexDump(packet + data.GetPosition(), uAddData));
		}
		break;
		case OP_CHANGE_CLIENT_IP:
		{
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_ChangedClientIP", client);
			theStats.AddDownDataOverheadOther(size);

			CSafeMemFile data(packet, size);
			byte uIP[MDX_DIGEST_SIZE];
			data.ReadHash16(uIP);
			CAddress IP = CAddress(uIP);

			if (IP.IsPublicIP()) { // Is this a valid public IP?
				client->SetIP(IP);
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					Debug(_T("  NewUserIP=%s\n"), (LPCTSTR)ipstr(IP));
			}
			else if (thePrefs.GetDebugClientTCPLevel() > 0)
				Debug(_T("***NOTE: OP_ChangedClientIP contains an invalid IP: %s\n"), (LPCTSTR)ipstr(IP));

			UINT uAddData = (UINT)(data.GetLength() - data.GetPosition());
			if (uAddData > 0 && thePrefs.GetDebugClientTCPLevel() > 0)
				Debug(_T("***NOTE: OP_ChangedClientIP contains add. data %s\n"), (LPCTSTR)DbgGetHexDump(packet + data.GetPosition(), uAddData));
		}
		break;
		case OP_CHANGE_SLOT:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_ChangeSlot", client, (size >= 16) ? packet : NULL);
			theStats.AddDownDataOverheadFileRequest(size);
			// sometimes sent by Hybrid
			break;
		case OP_MESSAGE:
		{
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_Message", client);
			theStats.AddDownDataOverheadOther(size);

			if (size < 2)
				throwCStr(_T("invalid message packet"));
			CSafeMemFile data(packet, size);
			UINT length = data.ReadUInt16();
			if (length + 2 != size)
				throwCStr(_T("invalid message packet"));

			if (length > MAX_CLIENT_MSG_LEN) {
				if (thePrefs.GetVerbose())
					AddDebugLogLine(false, _T("Message from '%s' (IP:%s) exceeds limit by %u chars, truncated."), (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)ipstr(client->GetConnectIP()), length - MAX_CLIENT_MSG_LEN);
				length = MAX_CLIENT_MSG_LEN;
			}

			client->ProcessChatMessage(data, length);
		}
		break;
		case OP_ASKSHAREDFILES:
		{
			// client wants to know what we have in share, let's see if we allow him to know that
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_AskSharedFiles", client);
			theStats.AddDownDataOverheadOther(size);

			CPtrList list;
			if (thePrefs.CanSeeShares() == vsfaEverybody || (thePrefs.CanSeeShares() == vsfaFriends && client->IsFriend())) {
				for (const CKnownFilesMap::CPair* pair = theApp.sharedfiles->m_Files_map.PGetFirstAssoc(); pair != NULL; pair = theApp.sharedfiles->m_Files_map.PGetNextAssoc(pair))
					if (!pair->value->IsLargeFile() || client->SupportsLargeFiles())
						list.AddTail((void*)pair->value);
				CString m_strTemp = GetResString(_T("REQ_SHAREDFILES"));
				if (client->GetUserName() == NULL || client->GetUserName()[0] == '\0') {
					m_strTemp.Replace(_T(" (%u)"), EMPTY);
					AddLogLine(true, m_strTemp, (LPCTSTR)md4str(client->GetUserHash()), (LPCTSTR)GetResString(_T("ACCEPTED")));
				} else {
					m_strTemp.Replace(_T("%u"), _T("%s"));
					AddLogLine(true, m_strTemp, (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)md4str(client->GetUserHash()), (LPCTSTR)GetResString(_T("ACCEPTED")));
				}
			}
			else
			{
				CString m_strTemp = GetResString(_T("REQ_SHAREDFILES"));
				if (client->GetUserName() == NULL || client->GetUserName()[0] == '\0') {
					m_strTemp.Replace(_T(" (%u)"), EMPTY);
					DebugLog(m_strTemp, (LPCTSTR)md4str(client->GetUserHash()), (LPCTSTR)GetResString(_T("DENIED")));
				} else {
					m_strTemp.Replace(_T("%u"), _T("%s"));
					DebugLog(m_strTemp, client->GetUserName(), (LPCTSTR)md4str(client->GetUserHash()), (LPCTSTR)GetResString(_T("DENIED")));
				}
			}

			// now create the memfile for the packet
			CSafeMemFile tempfile(80);
			tempfile.WriteUInt32((uint32)list.GetCount());
			while (!list.IsEmpty())
				theApp.sharedfiles->CreateOfferedFilePacket(reinterpret_cast<CKnownFile*>(list.RemoveHead()), tempfile, NULL, client);

			// create a packet and send it
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugSend("OP_AskSharedFilesAnswer", client);
			Packet* replypacket = new Packet(tempfile);
			replypacket->opcode = OP_ASKSHAREDFILESANSWER;
			theStats.AddUpDataOverheadOther(replypacket->size);
			SendPacket(replypacket, true);
		}
		break;
		case OP_ASKSHAREDFILESANSWER:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_AskSharedFilesAnswer", client);
			theStats.AddDownDataOverheadOther(size);
			client->ProcessSharedFileList(packet, size);
			break;
		case OP_ASKSHAREDDIRS:
		{
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_AskSharedDirectories", client);
			theStats.AddDownDataOverheadOther(size);

			if (thePrefs.CanSeeShares() == vsfaEverybody || (thePrefs.CanSeeShares() == vsfaFriends && client->IsFriend())) {
				CString m_strTemp = GetResString(_T("SHAREDREQ1"));
				if (client->GetUserName() == NULL || client->GetUserName()[0] == '\0') {
					m_strTemp.Replace(_T(" (%u)"), EMPTY);
					AddLogLine(true, m_strTemp, (LPCTSTR)md4str(client->GetUserHash()), (LPCTSTR)GetResString(_T("ACCEPTED")));
				} else {
					m_strTemp.Replace(_T("%u"), _T("%s"));
					AddLogLine(true, m_strTemp, (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)md4str(client->GetUserHash()), (LPCTSTR)GetResString(_T("ACCEPTED")));
				}
				client->SendSharedDirectories();
			}
			else {
				CString m_strTemp = GetResString(_T("SHAREDREQ1"));
				if (client->GetUserName() == NULL || client->GetUserName()[0] == '\0') {
					m_strTemp.Replace(_T(" (%u)"), EMPTY);
					DebugLog(m_strTemp, (LPCTSTR)md4str(client->GetUserHash()), (LPCTSTR)GetResString(_T("DENIED")));
				} else {
					m_strTemp.Replace(_T("%u"), _T("%s"));
					DebugLog(m_strTemp, client->GetUserName(), (LPCTSTR)md4str(client->GetUserHash()), (LPCTSTR)GetResString(_T("DENIED")));
				}

				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugSend("OP_AskSharedDeniedAnswer", client);
				Packet* replypacket = new Packet(OP_ASKSHAREDDENIEDANS, 0);
				theStats.AddUpDataOverheadOther(replypacket->size);
				SendPacket(replypacket, true);
			}
		}
		break;
		case OP_ASKSHAREDFILESDIR:
		{
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_AskSharedFilesInDirectory", client);
			theStats.AddDownDataOverheadOther(size);

			Packet* replypacket;
			CSafeMemFile data(packet, size);
			CString strReqDir(data.ReadString(client->GetUnicodeSupport() != UTF8strNone));
			if (thePrefs.CanSeeShares() == vsfaEverybody || (thePrefs.CanSeeShares() == vsfaFriends && client->IsFriend())) {
				CString m_strTemp = GetResString(_T("SHAREDREQ2"));
				if (client->GetUserName() == NULL || client->GetUserName()[0] == '\0') {
					m_strTemp.Replace(_T(" (%u)"), EMPTY);
					AddLogLine(true, m_strTemp, (LPCTSTR)md4str(client->GetUserHash()), (LPCTSTR)strReqDir, (LPCTSTR)GetResString(_T("ACCEPTED")));
				} else {
					m_strTemp.Replace(_T("%u"), _T("%s"));
					AddLogLine(true, m_strTemp, (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)md4str(client->GetUserHash()), (LPCTSTR)EscPercent(strReqDir), (LPCTSTR)GetResString(_T("ACCEPTED")));
				}
				ASSERT(data.GetPosition() == data.GetLength());
				CTypedPtrList<CPtrList, CKnownFile*> list;
				const CString strOrgReqDir(strReqDir);
				if (strReqDir == OP_INCOMPLETE_SHARED_FILES) {
					for (POSITION pos = NULL; ;) { // get all shared files from download queue
						CPartFile* pFile = theApp.downloadqueue->GetFileNext(pos);
						if (pFile && pFile->GetStatus(true) == PS_READY && (!pFile->IsLargeFile() || client->SupportsLargeFiles()))
							list.AddTail(pFile);
						if (pos == NULL)
							break;
					}
				}
				else {
					bool bSingleSharedFiles = (strReqDir == OP_OTHER_SHARED_FILES);
					if (!bSingleSharedFiles)
						strReqDir = theApp.sharedfiles->GetDirNameByPseudo(strReqDir);
					if (!strReqDir.IsEmpty()) {
						// get all shared files from requested directory
						for (const CKnownFilesMap::CPair* pair = theApp.sharedfiles->m_Files_map.PGetFirstAssoc(); pair != NULL; pair = theApp.sharedfiles->m_Files_map.PGetNextAssoc(pair)) {
							CKnownFile* cur_file = pair->value;
							// all files not in shared directories have to be single shared files
							if (((!bSingleSharedFiles && EqualPaths(strReqDir, cur_file->GetSharedDirectory()))
								|| (bSingleSharedFiles && !theApp.sharedfiles->ShouldBeShared(cur_file->GetSharedDirectory(), NULL, false))
								)
								&& (!cur_file->IsLargeFile() || client->SupportsLargeFiles()))
							{
								list.AddTail(cur_file);
							}
						}
					}
					else
						DebugLogError(_T("View shared files: Pseudonym for requested Directory (%s) was not found - sending empty result"), (LPCTSTR)EscPercent(strOrgReqDir));
				}

				// Currently we are sending each shared directory, even if it does not contain any files.
				// Because of this we also have to send an empty shared files list.
				CSafeMemFile tempfile(80);
				tempfile.WriteString(strOrgReqDir, client->GetUnicodeSupport());
				tempfile.WriteUInt32((uint32)list.GetCount());
				while (!list.IsEmpty())
					theApp.sharedfiles->CreateOfferedFilePacket(list.RemoveHead(), tempfile, NULL, client);

				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugSend("OP_AskSharedFilesInDirectoryAnswer", client);
				replypacket = new Packet(tempfile);
				replypacket->opcode = OP_ASKSHAREDFILESDIRANS;
			}
			else {
				CString m_strTemp = GetResString(_T("SHAREDREQ2"));
				if (client->GetUserName() == NULL || client->GetUserName()[0] == '\0') {
					m_strTemp.Replace(_T(" (%u)"), EMPTY);
					DebugLog(m_strTemp, (LPCTSTR)md4str(client->GetUserHash()), (LPCTSTR)EscPercent(strReqDir), (LPCTSTR)GetResString(_T("DENIED")));
				} else {
					m_strTemp.Replace(_T("%u"), _T("%s"));
					DebugLog(m_strTemp, client->GetUserName(), (LPCTSTR)md4str(client->GetUserHash()), (LPCTSTR)EscPercent(strReqDir), (LPCTSTR)GetResString(_T("DENIED")));
				}
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugSend("OP_AskSharedDeniedAnswer", client);
				replypacket = new Packet(OP_ASKSHAREDDENIEDANS, 0);
			}
			theStats.AddUpDataOverheadOther(replypacket->size);
			SendPacket(replypacket, true);
		}
		break;
		case OP_ASKSHAREDDIRSANS:
		{
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_AskSharedDirectoriesAnswer", client);
			theStats.AddDownDataOverheadOther(size);
			if (client->GetFileListRequested() == 1) {
				CSafeMemFile data(packet, size);
				uint32 uDirs = data.ReadUInt32();
				for (uint32 i = uDirs; i > 0; --i) {
					const CString& strDir(data.ReadString(client->GetUnicodeSupport() != UTF8strNone));
					// Better send the received and untouched directory string back to that client
					CString m_strTemp = GetResString(_T("SHAREDANSW"));
					if (client->GetUserName() == NULL || client->GetUserName()[0] == '\0') {
						m_strTemp.Replace(_T(" (%u)"), EMPTY);
						AddLogLine(true, m_strTemp, (LPCTSTR)md4str(client->GetUserHash()), (LPCTSTR)EscPercent(strDir));
					} else {
						m_strTemp.Replace(_T("%u"), _T("%s"));
						AddLogLine(true, m_strTemp, (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)md4str(client->GetUserHash()), (LPCTSTR)EscPercent(strDir));
					}

					if (thePrefs.GetDebugClientTCPLevel() > 0)
						DebugSend("OP_AskSharedFilesInDirectory", client);
					CSafeMemFile tempfile(80);
					tempfile.WriteString(strDir, client->GetUnicodeSupport());
					Packet* replypacket = new Packet(tempfile);
					replypacket->opcode = OP_ASKSHAREDFILESDIR;
					theStats.AddUpDataOverheadOther(replypacket->size);
					SendPacket(replypacket, true);
				}

				client->m_uSharedFilesStatus = S_RECEIVED;
				client->m_bAutoQuerySharedFiles = false;
				if (client->m_ArchivedClient)
					client->m_ArchivedClient->m_bAutoQuerySharedFiles = false;
				client->m_bQueryingSharedFiles = false;
				theApp.emuledlg->transferwnd->GetClientList()->RefreshClient(client);
				theApp.emuledlg->transferwnd->GetUploadList()->RefreshClient(client);
				theApp.emuledlg->transferwnd->GetQueueList()->RefreshClient(client);
				theApp.emuledlg->transferwnd->GetDownloadClientsList()->RefreshClient(client);

				ASSERT(data.GetPosition() == data.GetLength());
				client->SetFileListRequested(uDirs);
			}
			else
			{
				CString m_strTemp = GetResString(_T("SHAREDANSW2"));
				if (client->GetUserName() == NULL || client->GetUserName()[0] == '\0') {
					m_strTemp.Replace(_T(" (%u)"), EMPTY);
					AddLogLine(true, m_strTemp, (LPCTSTR)md4str(client->GetUserHash()));
				} else {
					m_strTemp.Replace(_T("%u"), _T("%s"));
					AddLogLine(true, m_strTemp, (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)md4str(client->GetUserHash()));
				}
			}
		}
		break;
		case OP_ASKSHAREDFILESDIRANS:
		{
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_AskSharedFilesInDirectoryAnswer", client);
			theStats.AddDownDataOverheadOther(size);

			CSafeMemFile data(packet, size);
			CString strDir(data.ReadString(client->GetUnicodeSupport() != UTF8strNone));

			if (client->GetFileListRequested() > 0) {
				CString m_strTemp = GetResString(_T("SHAREDINFO1"));
				if (client->GetUserName() == NULL || client->GetUserName()[0] == '\0') {
					m_strTemp.Replace(_T(" (%u)"), EMPTY);
					AddLogLine(true, m_strTemp, (LPCTSTR)md4str(client->GetUserHash()), (LPCTSTR)EscPercent(strDir));
				} else {
					m_strTemp.Replace(_T("%u"), _T("%s"));
					AddLogLine(true, m_strTemp, (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)md4str(client->GetUserHash()), (LPCTSTR)EscPercent(strDir));
				}

				client->ProcessSharedFileList(packet + data.GetPosition(), (uint32)(size - data.GetPosition()), strDir);
				if (client->GetFileListRequested() == 0)
				{
					CString m_strTemp = GetResString(_T("SHAREDINFO2"));
					if (client->GetUserName() == NULL || client->GetUserName()[0] == '\0') {
						m_strTemp.Replace(_T(" (%u)"), EMPTY);
						AddLogLine(true, m_strTemp, (LPCTSTR)md4str(client->GetUserHash()));
					} else {
						m_strTemp.Replace(_T("%u"), _T("%s"));
						AddLogLine(true, m_strTemp, (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)md4str(client->GetUserHash()));
					}
				}

				client->m_uSharedFilesStatus = S_RECEIVED;
				client->m_bAutoQuerySharedFiles = false;
				if (client->m_ArchivedClient)
					client->m_ArchivedClient->m_bAutoQuerySharedFiles = false;
				client->m_bQueryingSharedFiles = false;
				client->m_uSharedFilesCount = theApp.searchlist->GetFoundFiles(client->GetSearchID());
				theApp.emuledlg->transferwnd->GetClientList()->RefreshClient(client);
				theApp.emuledlg->transferwnd->GetUploadList()->RefreshClient(client);
				theApp.emuledlg->transferwnd->GetQueueList()->RefreshClient(client);
				theApp.emuledlg->transferwnd->GetDownloadClientsList()->RefreshClient(client);
			}
			else
			{
				CString m_strTemp = GetResString(_T("SHAREDANSW3"));
				if (client->GetUserName() == NULL || client->GetUserName()[0] == '\0') {
					m_strTemp.Replace(_T(" (%u)"), EMPTY);
					AddLogLine(true, m_strTemp, (LPCTSTR)md4str(client->GetUserHash()), (LPCTSTR)EscPercent(strDir));
				} else {
					m_strTemp.Replace(_T("%u"), _T("%s"));
					AddLogLine(true, m_strTemp, (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)md4str(client->GetUserHash()), (LPCTSTR)EscPercent(strDir));
				}
			}
		}
		break;
		case OP_ASKSHAREDDENIEDANS:
		{
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_AskSharedDeniedAnswer", client);
			theStats.AddDownDataOverheadOther(size);

			CString m_strTemp = GetResString(_T("SHAREDREQDENIED"));
			if (client->GetUserName() == NULL || client->GetUserName()[0] == '\0') {
				m_strTemp.Replace(_T(" (%u)"), EMPTY);
				AddLogLine(true, m_strTemp, (LPCTSTR)md4str(client->GetUserHash()));
			} else {
				m_strTemp.Replace(_T("%u"), _T("%s"));
				AddLogLine(true, m_strTemp, (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)md4str(client->GetUserHash()));
			}

			client->SetFileListRequested(0);

			client->m_uSharedFilesStatus = S_ACCESS_DENIED;
			client->m_bAutoQuerySharedFiles = false;
			if (client->m_ArchivedClient)
				client->m_ArchivedClient->m_bAutoQuerySharedFiles = false;
			client->m_bQueryingSharedFiles = false;
			theApp.emuledlg->transferwnd->GetClientList()->RefreshClient(client);
			theApp.emuledlg->transferwnd->GetUploadList()->RefreshClient(client);
			theApp.emuledlg->transferwnd->GetQueueList()->RefreshClient(client);
			theApp.emuledlg->transferwnd->GetDownloadClientsList()->RefreshClient(client);
		}
		break;
		default:
			theStats.AddDownDataOverheadOther(size);
			PacketToDebugLogLine(_T("eDonkey"), packet, size, opcode);
		}
	} catch (CString ex) {
		CAddress IP;
		SOCKADDR_IN6 sockAddr = { 0 };
		int nSockAddrLen = sizeof sockAddr;
		GetPeerName((LPSOCKADDR)&sockAddr, &nSockAddrLen);
		IP.FromSA((SOCKADDR*)&sockAddr, nSockAddrLen);

		if (thePrefs.GetVerbose()) {
			AddDebugLogLine(false, _T("[ProcessPacket]: Client with IP=%s caused a defined exception, disconnecting client: %s"), IP.ToStringC(), (LPCTSTR)EscPercent(ex.GetBuffer()));
			PacketToDebugLogLine(_T("eMule"), packet, size, opcode);
		}

		if (client)
			client->SetDownloadState(DS_ERROR, _T("[ProcessPacket]: A defined exception occured while processing eDonkey packet: ") + EscPercent(ex.GetBuffer()));

		if (thePrefs.IsBanWrongPackage()) {
			if (client) {
				if (theApp.shield->IsHarderPunishment(client->m_uPunishment, PR_WRONGPACKAGE))
					theApp.shield->SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_WRONG_PACKAGE")), PR_WRONGPACKAGE);
			} else if (!IP.IsNull()) {
				theApp.clientlist->AddBannedClient(IP.ToStringC());
				if (thePrefs.GetVerbose())
					AddDebugLogLine(false, _T("[ProcessPacket]: IP address has been banned: %s"), (LPCTSTR)IP.ToStringC());
			}
		}

		Disconnect(_T("[ProcessPacket]: A defined exception occured while processing eDonkey packet: ") + ex);
		return false;
	} catch (CException* ex) {
		CAddress IP;
		SOCKADDR_IN6 sockAddr = { 0 };
		int nSockAddrLen = sizeof sockAddr;
		GetPeerName((LPSOCKADDR)&sockAddr, &nSockAddrLen);
		IP.FromSA((SOCKADDR*)&sockAddr, nSockAddrLen);

		if (thePrefs.GetVerbose()) {
			AddDebugLogLine(false, _T("[ProcessPacket]: Client with IP=%s caused an exception, disconnecting client: %s"), IP.ToStringC(), (LPCTSTR)EscPercent(CString(CExceptionStr(*ex))));
			PacketToDebugLogLine(_T("eMule"), packet, size, opcode);
		}

		ex->Delete();

		if (client)
			client->SetDownloadState(DS_ERROR, _T("[ProcessPacket]: An exception occured while processing eDonkey packet: ") + EscPercent(CString(CExceptionStr(*ex))));

		if (thePrefs.IsBanWrongPackage()) {
			if (client) {
				if (theApp.shield->IsHarderPunishment(client->m_uPunishment, PR_WRONGPACKAGE))
					theApp.shield->SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_WRONG_PACKAGE")), PR_WRONGPACKAGE);
			} else if (!IP.IsNull()) {
				theApp.clientlist->AddBannedClient(IP.ToStringC());
				if (thePrefs.GetVerbose())
					AddDebugLogLine(false, _T("[ProcessPacket]: IP address has been banned: %s"), (LPCTSTR)IP.ToStringC());
			}
		}
		
		Disconnect(_T("[ProcessPacket]: An exception occured while processing eDonkey packet: ") + EscPercent(CString(CExceptionStr(*ex))));
		return false;
	} catch (...) {
		CAddress IP;
		SOCKADDR_IN6 sockAddr = { 0 };
		int nSockAddrLen = sizeof sockAddr;
		GetPeerName((LPSOCKADDR)&sockAddr, &nSockAddrLen);
		IP.FromSA((SOCKADDR*)&sockAddr, nSockAddrLen);

		if (thePrefs.GetVerbose()) {
			AddDebugLogLine(false, _T("[ProcessPacket]: Client with IP=%s caused an unknown exception, disconnecting client."), IP.ToStringC());
			PacketToDebugLogLine(_T("eMule"), packet, size, opcode);
		}

		if (client)
			client->SetDownloadState(DS_ERROR, _T("[ProcessPacket]: An unknown exception occured while processing eDonkey packet."));

		if (thePrefs.IsBanWrongPackage()) {
			if (client) {
				if (theApp.shield->IsHarderPunishment(client->m_uPunishment, PR_WRONGPACKAGE))
					theApp.shield->SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_WRONG_PACKAGE")), PR_WRONGPACKAGE);
			} else if (!IP.IsNull()) {
				theApp.clientlist->AddBannedClient(IP.ToStringC());
				if (thePrefs.GetVerbose())
					AddDebugLogLine(false, _T("[ProcessPacket]: IP address has been banned: %s"), (LPCTSTR)IP.ToStringC());
			}
		}
		
		Disconnect(_T("[ProcessPacket]: An unknown exception occured while processing eDonkey packet."));
		return false;
	}
	return true;
}

bool CClientReqSocket::ProcessExtPacket(const BYTE *packet, uint32 size, UINT opcode, UINT uRawSize)
{
	try {
		switch (opcode) {
		case OP_MULTIPACKET: // deprecated
		case OP_MULTIPACKET_EXT: // deprecated
		case OP_MULTIPACKET_EXT2:
			{
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					if (opcode == OP_MULTIPACKET)
						DebugRecv("OP_MultiPacket", client, (size >= 16) ? packet : NULL);
					else
						DebugRecv((opcode == OP_MULTIPACKET_EXT2 ? "OP_MultiPacket_Ext2" : "OP_MultiPacket_Ext"), client, (size >= 24) ? packet : NULL);

				theStats.AddDownDataOverheadFileRequest(uRawSize);

				if (client->GetKadPort() && client->GetKadVersion() >= KADEMLIA_VERSION2_47a)
					Kademlia::CKademlia::Bootstrap(client->GetIPv4().ToUInt32(true), client->GetKadPort());

				CSafeMemFile data_in(packet, size);
				CKnownFile *reqfile;
				bool bNotFound = false;
				uchar reqfilehash[MDX_DIGEST_SIZE];
				if (opcode == OP_MULTIPACKET_EXT2) { // file identifier support
					CFileIdentifierSA fileIdent;
					if (!fileIdent.ReadIdentifier(data_in)) {
						DebugLogWarning(_T("Error while reading file identifier from MultiPacket_Ext2 - %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
						break;
					}
					md4cpy(reqfilehash, fileIdent.GetMD4Hash()); // need this in case we want to sent a FNF
					reqfile = theApp.sharedfiles->GetFileByID(fileIdent.GetMD4Hash());
					if (reqfile == NULL) {
						reqfile = theApp.downloadqueue->GetFileByID(fileIdent.GetMD4Hash());
						if (reqfile == NULL || (uint64)((CPartFile*)reqfile)->GetCompletedSize() < PARTSIZE) {
							bNotFound = true;
							client->CheckFailedFileIdReqs(fileIdent.GetMD4Hash());
						}
					}
					if (!bNotFound && !reqfile->GetFileIdentifier().CompareRelaxed(fileIdent)) {
						bNotFound = true;
						DebugLogWarning(_T("FileIdentifier Mismatch on requested file, sending FNF; %s, File=\"%s\", Local Ident: %s, Received Ident: %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo())
							, (LPCTSTR)EscPercent(reqfile->GetFileName()), (LPCTSTR)EscPercent(reqfile->GetFileIdentifier().DbgInfo()), (LPCTSTR)EscPercent(fileIdent.DbgInfo()));
					}
				} else { // no file identifier
					data_in.ReadHash16(reqfilehash);
					uint64 nSize = (opcode == OP_MULTIPACKET_EXT) ? data_in.ReadUInt64() : 0;
					reqfile = theApp.sharedfiles->GetFileByID(reqfilehash);
					if (reqfile == NULL) {
						reqfile = theApp.downloadqueue->GetFileByID(reqfilehash);
						if (reqfile == NULL || (uint64)((CPartFile*)reqfile)->GetCompletedSize() < PARTSIZE) {
							bNotFound = true;
							client->CheckFailedFileIdReqs(reqfilehash);
						}
					}
					if (!bNotFound && nSize != 0 && nSize != reqfile->GetFileSize()) {
						bNotFound = true;
						DebugLogWarning(_T("Size Mismatch on requested file, sending FNF; %s, File=\"%s\""), (LPCTSTR)EscPercent(client->DbgGetClientInfo()), (LPCTSTR)EscPercent(reqfile->GetFileName()));
					}
				}

				if (!bNotFound && reqfile->IsLargeFile() && !client->SupportsLargeFiles()) {
					bNotFound = true;
					DebugLogWarning(_T("Client without 64bit file support requested large file; %s, File=\"%s\""), (LPCTSTR)EscPercent(client->DbgGetClientInfo()), (LPCTSTR)EscPercent(reqfile->GetFileName()));
				}
				if (bNotFound) {
					// send file request answer - no such file packet (0x48)
					if (thePrefs.GetDebugClientTCPLevel() > 0)
						DebugSend("OP_FileReqAnsNoFil", client, packet);
					Packet *replypacket = new Packet(OP_FILEREQANSNOFIL, 16);
					md4cpy(replypacket->pBuffer, reqfilehash);
					theStats.AddUpDataOverheadFileRequest(replypacket->size);
					SendPacket(replypacket);
					break;
				}

				if (!client->GetWaitStartTime())
					client->SetWaitStartTime();

				// if we are downloading this file, this could be a new source
				// no passive adding of files with only one part
				if (reqfile->IsPartFile() && (uint64)reqfile->GetFileSize() > PARTSIZE)
					if (static_cast<CPartFile*>(reqfile)->GetMaxSources() > static_cast<CPartFile*>(reqfile)->GetSourceCount())
						theApp.downloadqueue->CheckAndAddKnownSource(static_cast<CPartFile*>(reqfile), client, true);

				// check to see if this is a new file they are asking for
				if (!md4equ(client->GetUploadFileID(), reqfile->GetFileHash()))
					client->SetCommentDirty();

				client->SetUploadFileID(reqfile);

				CSafeMemFile data_out(128);
				if (opcode == OP_MULTIPACKET_EXT2) // file identifier support
					reqfile->GetFileIdentifierC().WriteIdentifier(data_out);
				else
					data_out.WriteHash16(reqfile->GetFileHash());
				bool bAnswerFNF = false;
				while (data_in.GetLength() > data_in.GetPosition() && !bAnswerFNF) {
					uint8 opcode_in = data_in.ReadUInt8();
					switch (opcode_in) {
					case OP_REQUESTFILENAME:
						if (thePrefs.GetDebugClientTCPLevel() > 0)
							DebugRecv("OP_MPReqFileName", client, packet);

						if (!client->ProcessExtendedInfo(data_in, reqfile)) {
							if (thePrefs.GetDebugClientTCPLevel() > 0)
								DebugSend("OP_FileReqAnsNoFil", client, packet);
							Packet *replypacket = new Packet(OP_FILEREQANSNOFIL, 16);
							md4cpy(replypacket->pBuffer, reqfile->GetFileHash());
							theStats.AddUpDataOverheadFileRequest(replypacket->size);
							SendPacket(replypacket);
							DebugLogWarning(_T("Partcount mismatch on requested file, sending FNF; %s, File=\"%s\""), (LPCTSTR)EscPercent(client->DbgGetClientInfo()), (LPCTSTR)EscPercent(reqfile->GetFileName()));
							bAnswerFNF = true;
						} else {
							data_out.WriteUInt8(OP_REQFILENAMEANSWER);
							data_out.WriteString(reqfile->GetFileName(), client->GetUnicodeSupport());
						}
						break;
					case OP_AICHFILEHASHREQ:
						if (thePrefs.GetDebugClientTCPLevel() > 0)
							DebugRecv("OP_MPAichFileHashReq", client, packet);

						if (client->SupportsFileIdentifiers() || opcode == OP_MULTIPACKET_EXT2) // not allowed any more with file idents supported
							DebugLogWarning(_T("Client requested AICH Hash packet, but supports FileIdentifiers, ignored - %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
						else if (client->IsSupportingAICH() && reqfile->GetFileIdentifier().HasAICHHash()) {
							data_out.WriteUInt8(OP_AICHFILEHASHANS);
							reqfile->GetFileIdentifier().GetAICHHash().Write(data_out);
						}
						break;
					case OP_SETREQFILEID:
						if (thePrefs.GetDebugClientTCPLevel() > 0)
							DebugRecv("OP_MPSetReqFileID", client, packet);

						if (thePrefs.IsDetectFileFaker() && reqfile->IsPartFile() && ((CPartFile*)reqfile)->m_DeadSourceList.IsDeadSource(*client))	{ // IsHarderPunishment isn't necessary here since the cost is low
							theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_FILE_FAKER")), PR_FILEFAKER);
							bAnswerFNF = true; //will skip to answer
							break;
						}

						data_out.WriteUInt8(OP_FILESTATUS);
						if (reqfile->IsPartFile())
							static_cast<CPartFile*>(reqfile)->WritePartStatus(data_out);
						else
							data_out.WriteUInt16(0);

						if (client->GetIncompletePartVersion() && reqfile->IsPartFile()) { // Don't send on complete files
							data_out.WriteUInt8(OP_FILEINCSTATUS);
							((CPartFile*)reqfile)->WriteIncPartStatus(&data_out);
						}

						break;
					//We still send the source packet separately.
					case OP_REQUESTSOURCES2:
					case OP_REQUESTSOURCES:
						{
							if (thePrefs.GetDebugClientTCPLevel() > 0)
								DebugRecv(opcode_in == OP_REQUESTSOURCES ? "OP_MPReqSources2" : "OP_MPReqSources", client, packet);

							if (thePrefs.GetDebugSourceExchange())
								AddDebugLogLine(false, _T("SXRecv: Client source request; %s, File=\"%s\""), (LPCTSTR)EscPercent(client->DbgGetClientInfo()), (LPCTSTR)EscPercent(reqfile->GetFileName()));

								// Anti-XS-Exploit
								if (thePrefs.IsDetectXSExploiter() && client->IsXSExploiter()) { // IsHarderPunishment isn't necessary here since the cost is low
									CString strPunishmentReason;
									strPunishmentReason.Format(GetResString(_T("PUNISHMENT_REASON_XS_XPLOITER")), client->GetXSReqs(), client->GetXSAnswers());
									theApp.shield->SetPunishment(client,strPunishmentReason, PR_XSEXPLOITER);
									break; //no answer
								}

							uint8 byRequestedVersion = 0;
							uint16 byRequestedOptions = 0;
							if (opcode_in == OP_REQUESTSOURCES2) { // SX2 requests contains additional data
								byRequestedVersion = data_in.ReadUInt8();
								byRequestedOptions = data_in.ReadUInt16();
							}
							//Although this shouldn't happen, it's just in case for any Mods that mess with version numbers.
							if (byRequestedVersion > 0 || client->GetSourceExchange1Version() > 1) {
								DWORD dwTimePassed = ::GetTickCount() - client->GetLastSrcReqTime() + CONNECTION_LATENCY;
								bool bNeverAskedBefore = client->GetLastSrcReqTime() == 0;
								if ( //if not complete and file is rare
									(reqfile->IsPartFile()
										&& (bNeverAskedBefore || dwTimePassed > SOURCECLIENTREASKS)
										&& static_cast<CPartFile*>(reqfile)->GetSourceCount() <= RARE_FILE
									)
										//OR if file is not rare or is complete
									|| bNeverAskedBefore || dwTimePassed > SOURCECLIENTREASKS * MINCOMMONPENALTY
								   )
								{
									client->SetLastSrcReqTime();
									Packet *tosend = reqfile->CreateSrcInfoPacket(client, byRequestedVersion, byRequestedOptions);
									if (tosend) {
										if (thePrefs.GetDebugClientTCPLevel() > 0)
											DebugSend("OP_AnswerSources", client, reqfile->GetFileHash());
										theStats.AddUpDataOverheadSourceExchange(tosend->size);
										SendPacket(tosend);
									}
								}/* else if (thePrefs.GetVerbose())
									AddDebugLogLine(false, _T("RCV: Source Request too fast. (This is testing the new timers to see how much older client will not receive this)"));
								*/
							}
						}
						break;
					default:
						{
							CString strError;
							strError.Format(_T("Invalid sub opcode 0x%02x received"), opcode_in);
							throw strError;
						}
					}
				}
				if (data_out.GetLength() > 16 && !bAnswerFNF) {
					if (thePrefs.GetDebugClientTCPLevel() > 0)
						DebugSend("OP_MultiPacketAns", client, reqfile->GetFileHash());
					Packet *reply = new Packet(data_out, OP_EMULEPROT);
					reply->opcode = (opcode == OP_MULTIPACKET_EXT2) ? OP_MULTIPACKETANSWER_EXT2 : OP_MULTIPACKETANSWER;
					theStats.AddUpDataOverheadFileRequest(reply->size);
					SendPacket(reply);
				}
			}
			break;
		case OP_MULTIPACKETANSWER:
		case OP_MULTIPACKETANSWER_EXT2:
			{
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_MultiPacketAns", client, (size >= 16) ? packet : NULL);
				theStats.AddDownDataOverheadFileRequest(uRawSize);

				if (client->GetKadPort() && client->GetKadVersion() >= KADEMLIA_VERSION2_47a)
					Kademlia::CKademlia::Bootstrap(client->GetIPv4().ToUInt32(true), client->GetKadPort());

				CSafeMemFile data_in(packet, size);

				CPartFile *reqfile = NULL;
				CFileIdentifierSA fileIdent;
				uchar reqfilehash[16];
				if (opcode == OP_MULTIPACKETANSWER_EXT2) {
					if (!fileIdent.ReadIdentifier(data_in))
						throw GetResString(_T("ERR_WRONGFILEID")) + _T(" (OP_MULTIPACKETANSWER_EXT2; ReadIdentifier() failed)");
					reqfile = theApp.downloadqueue->GetFileByID(fileIdent.GetMD4Hash());
					if (reqfile == NULL) {
						client->CheckFailedFileIdReqs(fileIdent.GetMD4Hash());
						//following situation: we are downloading a cue file (100 bytes) from many sources
						//this file will be finished earlier than our sources are answering
						//throwing an exception will filter good sources
						//swapping can't be done at this point, so here we let it just timeout.
						CKnownFile* reqfiletocheck = theApp.sharedfiles->GetFileByID(fileIdent.GetMD4Hash());
						if (reqfiletocheck != NULL)	{
							AddDebugLogLine(false, _T("Client sent NULL reqfile: %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
							break;
						} else
							throw GetResString(_T("ERR_WRONGFILEID")) + _T(" (OP_MULTIPACKETANSWER_EXT2; reqfile==NULL)");
					}
					if (!reqfile->GetFileIdentifier().CompareRelaxed(fileIdent))
						throw GetResString(_T("ERR_WRONGFILEID")) + _T(" (OP_MULTIPACKETANSWER_EXT2; FileIdentifier mismatch)");
					if (fileIdent.HasAICHHash())
						client->ProcessAICHFileHash(NULL, reqfile, &fileIdent.GetAICHHash());
				} else {
					data_in.ReadHash16(reqfilehash);
					reqfile = theApp.downloadqueue->GetFileByID(reqfilehash);
					//Make sure we are downloading this file.
					if (reqfile == NULL) {
						client->CheckFailedFileIdReqs(reqfilehash);
						//following situation: we are downloading a cue file (100 bytes) from many sources
						//this file will be finished earlier than our sources are answering
						//throwing an exception will filter good sources
						//swapping can't be done at this point, so I let it just timeout.
						CKnownFile* reqfiletocheck = theApp.sharedfiles->GetFileByID(reqfilehash);
						if (reqfiletocheck != NULL)	{
							AddDebugLogLine(false, _T("Client sent NULL reqfile: %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
							break;
						} else
							throw GetResString(_T("ERR_WRONGFILEID")) + _T(" (OP_MULTIPACKETANSWER; reqfile==NULL)");
					}
				}
				if (client->GetRequestFile() == NULL)
					throw GetResString(_T("ERR_WRONGFILEID")) + _T(" (OP_MULTIPACKETANSWER; client->GetRequestFile()==NULL)");
				if (reqfile != client->GetRequestFile())
				{
					client->CheckFailedFileIdReqs((opcode == OP_MULTIPACKETANSWER_EXT2) ? fileIdent.GetMD4Hash() : reqfilehash);
					//can happen with a late answer after swapping -->break!
					break;
				}

				while (data_in.GetLength() > data_in.GetPosition()) {
					uint8 opcode_in = data_in.ReadUInt8();
					switch (opcode_in) {
					case OP_REQFILENAMEANSWER:
						if (thePrefs.GetDebugClientTCPLevel() > 0)
							DebugRecv("OP_MPReqFileNameAns", client, packet);

						client->ProcessFileInfo(data_in, reqfile);
						break;
					case OP_FILESTATUS:
						if (thePrefs.GetDebugClientTCPLevel() > 0)
							DebugRecv("OP_MPFileStatus", client, packet);

						client->ProcessFileStatus(false, data_in, reqfile);
						break;
					case OP_AICHFILEHASHANS:
						if (thePrefs.GetDebugClientTCPLevel() > 0)
							DebugRecv("OP_MPAichFileHashAns", client);

						client->ProcessAICHFileHash(&data_in, reqfile, NULL);
						break;
					case OP_FILEINCSTATUS:
					{
						theStats.AddDownDataOverheadFileRequest(size);
						client->ProcessFileIncStatus(&data_in, reqfile, false);
						break;
					}
					default:
						{
							CString strError;
							strError.Format(_T("Invalid sub opcode 0x%02x received"), opcode_in);
							throw strError;
						}
					}
				}
			}
			break;
		case OP_EMULEINFO:
			theStats.AddDownDataOverheadOther(uRawSize);
			client->ProcessMuleInfoPacket(packet, size);
			client->ProcessBanMessage();
			if (thePrefs.GetDebugClientTCPLevel() > 0) {
				DebugRecv("OP_EmuleInfo", client);
				Debug(_T("  %s\n"), (LPCTSTR)client->DbgGetMuleInfo());
			}

			// start secure identification, if
			//  - we have received eD2K and eMule info (old eMule)
			if (client->GetInfoPacketsReceived() == IP_BOTH)
				client->InfoPacketsReceived();

			client->SendMuleInfoPacket(true);
			break;
		case OP_EMULEINFOANSWER:
			theStats.AddDownDataOverheadOther(uRawSize);
			client->ProcessMuleInfoPacket(packet, size);
			client->ProcessBanMessage();
			if (thePrefs.GetDebugClientTCPLevel() > 0) {
				DebugRecv("OP_EmuleInfoAnswer", client);
				Debug(_T("  %s\n"), (LPCTSTR)client->DbgGetMuleInfo());
			}

			// start secure identification, if
			//  - we have received eD2K and eMule info (old eMule)
			if (client->GetInfoPacketsReceived() == IP_BOTH)
				client->InfoPacketsReceived();
			break;
		case OP_SECIDENTSTATE:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_SecIdentState", client);
			theStats.AddDownDataOverheadOther(uRawSize);

			client->ProcessSecIdentStatePacket(packet, size);
			if (client->GetSecureIdentState() == IS_SIGNATURENEEDED)
				client->SendSignaturePacket();
			else if (client->GetSecureIdentState() == IS_KEYANDSIGNEEDED) {
				client->SendPublicKeyPacket();
				client->SendSignaturePacket();
			}
			break;
		case OP_PUBLICKEY:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_PublicKey", client);
			theStats.AddDownDataOverheadOther(uRawSize);

			client->ProcessPublicKeyPacket(packet, size);
			break;
		case OP_SIGNATURE:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_Signature", client);
			theStats.AddDownDataOverheadOther(uRawSize);

			client->ProcessSignaturePacket(packet, size);
			break;
		case OP_QUEUERANKING:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_QueueRanking", client);
			theStats.AddDownDataOverheadFileRequest(uRawSize);

			client->ProcessEmuleQueueRank(packet, size);
			break;
		case OP_REQUESTSOURCES:
		case OP_REQUESTSOURCES2:
			{
				CSafeMemFile data(packet, size);
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv(opcode == OP_REQUESTSOURCES2 ? "OP_MPReqSources2" : "OP_MPReqSources", client, (size >= 16) ? packet : NULL);

				theStats.AddDownDataOverheadSourceExchange(uRawSize);

				// Anti-XS-Exploit
				if (thePrefs.IsDetectXSExploiter() && client->IsXSExploiter()) { // IsHarderPunishment isn't necessary here since the cost is low
					CString strPunishmentReason;
					strPunishmentReason.Format(GetResString(_T("PUNISHMENT_REASON_XS_XPLOITER")), client->GetXSReqs(), client->GetXSAnswers());
					theApp.shield->SetPunishment(client,strPunishmentReason, PR_XSEXPLOITER);
					break; //no answer
				}

				uint8 byRequestedVersion = 0;
				uint16 byRequestedOptions = 0;
				if (opcode == OP_REQUESTSOURCES2) { // SX2 requests contains additional data
					byRequestedVersion = data.ReadUInt8();
					byRequestedOptions = data.ReadUInt16();
				}
				//Although this shouldn't happen, it's just in case to any Mods that mess with version numbers.
				if (byRequestedVersion > 0 || client->GetSourceExchange1Version() > 1) {
					if (size < 16)
						throw GetResString(_T("ERR_BADSIZE"));

					if (thePrefs.GetDebugSourceExchange())
						AddDebugLogLine(false, _T("SXRecv: Client source request; %s, %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()), (LPCTSTR)EscPercent(DbgGetFileInfo(packet)));

					//first check shared file list, then download list
					uchar ucHash[MDX_DIGEST_SIZE];
					data.ReadHash16(ucHash);
					CKnownFile *reqfile = theApp.sharedfiles->GetFileByID(ucHash);
					if (!reqfile)
						reqfile = theApp.downloadqueue->GetFileByID(ucHash);
					if (reqfile) {
						// There are some clients which do not follow the correct protocol procedure of sending
						// the sequence OP_REQUESTFILENAME, OP_SETREQFILEID, OP_REQUESTSOURCES. If those clients
						// are doing this, they will not get the optimal set of sources which we could offer if
						// the would follow the above noted protocol sequence. They better to it the right way
						// or they will get just a random set of sources because we do not know their download
						// part status which may get cleared with the call of 'SetUploadFileID'.
						client->SetUploadFileID(reqfile);

						DWORD dwTimePassed = ::GetTickCount() - client->GetLastSrcReqTime() + CONNECTION_LATENCY;
						bool bNeverAskedBefore = (client->GetLastSrcReqTime() == 0);
						if ( //if not complete and file is rare
							(reqfile->IsPartFile()
								&& (bNeverAskedBefore || dwTimePassed > SOURCECLIENTREASKS)
								&& static_cast<CPartFile*>(reqfile)->GetSourceCount() <= RARE_FILE
							)
								//OR if file is not rare or is complete
							|| bNeverAskedBefore || dwTimePassed > SOURCECLIENTREASKS * MINCOMMONPENALTY
						   )
						{
							client->SetLastSrcReqTime();
							Packet *tosend = reqfile->CreateSrcInfoPacket(client, byRequestedVersion, byRequestedOptions);
							if (tosend) {
								if (thePrefs.GetDebugClientTCPLevel() > 0)
									DebugSend("OP_AnswerSources", client, reqfile->GetFileHash());
								theStats.AddUpDataOverheadSourceExchange(tosend->size);
								SendPacket(tosend, true);
							}
						}
					} else
						client->CheckFailedFileIdReqs(ucHash);
				}
			}
			break;
		case OP_ANSWERSOURCES:
			{
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_AnswerSources", client, (size >= 16) ? packet : NULL);
				theStats.AddDownDataOverheadSourceExchange(uRawSize);
				client->IncXSAnswer();

				CSafeMemFile data(packet, size);
				uchar hash[MDX_DIGEST_SIZE];
				data.ReadHash16(hash);
				CKnownFile *file = theApp.downloadqueue->GetFileByID(hash);
				if (file == NULL)
					client->CheckFailedFileIdReqs(hash);
				else if (file->IsPartFile()) {
					//set the client's answer time
					client->SetLastSrcAnswerTime();
					//and set the file's last answer time
					static_cast<CPartFile*>(file)->SetLastAnsweredTime();
					static_cast<CPartFile*>(file)->AddClientSources(&data, client->GetSourceExchange1Version(), false, client);
				}
			}
			break;
		case OP_ANSWERSOURCES2:
			{
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_AnswerSources2", client, (size >= 17) ? packet : NULL);
				theStats.AddDownDataOverheadSourceExchange(uRawSize);
				client->IncXSAnswer();

				CSafeMemFile data(packet, size);
				uint8 byVersion = data.ReadUInt8();
				uchar hash[MDX_DIGEST_SIZE];
				data.ReadHash16(hash);
				CKnownFile *file = theApp.downloadqueue->GetFileByID(hash);
				if (file == NULL)
					client->CheckFailedFileIdReqs(hash);
				else if (file->IsPartFile()) {
					//set the client's answer time
					client->SetLastSrcAnswerTime();
					//and set the file's last answer time
					static_cast<CPartFile*>(file)->SetLastAnsweredTime();
					static_cast<CPartFile*>(file)->AddClientSources(&data, byVersion, true, client);
				}
			}
			break;
		case OP_FILEDESC:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_FileDesc", client);
			theStats.AddDownDataOverheadFileRequest(uRawSize);

			client->ProcessMuleCommentPacket(packet, size);
			break;
		case OP_REQUESTPREVIEW:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_RequestPreView", client, (size >= 16) ? packet : NULL);
			theStats.AddDownDataOverheadOther(uRawSize);

			if (thePrefs.CanSeeShares() == vsfaEverybody || (thePrefs.CanSeeShares() == vsfaFriends && client->IsFriend())) {
				if (thePrefs.GetVerbose())
					AddDebugLogLine(false, _T("Client '%s' (%s) requested Preview - accepted"), (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)ipstr(client->GetConnectIP()));
				client->ProcessPreviewReq(packet, size);
			} else {
				// we don't send any answer here, because the client should know that he was not allowed to ask
				if (thePrefs.GetVerbose())
					AddDebugLogLine(false, _T("Client '%s' (%s) requested Preview - denied"), (LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)ipstr(client->GetConnectIP()));
			}
			break;
		case OP_PREVIEWANSWER:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_PreviewAnswer", client, (size >= 16) ? packet : NULL);
			theStats.AddDownDataOverheadOther(uRawSize);

			client->ProcessPreviewAnswer(packet, size);
			break;
		case OP_PEERCACHE_QUERY:
		case OP_PEERCACHE_ANSWER:
		case OP_PEERCACHE_ACK:
			theStats.AddDownDataOverheadFileRequest(uRawSize);
			break;
		case OP_PUBLICIP_ANSWER:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_PublicIPAns", client);
			theStats.AddDownDataOverheadOther(uRawSize);

			client->ProcessPublicIPAnswer(packet, size);
			break;
		case OP_PUBLICIP_REQ:
			{
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_PublicIPReq", client);
				theStats.AddDownDataOverheadOther(uRawSize);

				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugSend("OP_PublicIPAns", client);
				Packet* pPacket = new Packet(OP_PUBLICIP_ANSWER, client->GetIP().GetType() == CAddress::IPv4 ? 4 : 20, OP_EMULEPROT);
				if (client->GetIP().GetType() == CAddress::IPv4)
					PokeUInt32(pPacket->pBuffer, client->GetIP().ToUInt32(false));
				else {
					PokeUInt32(pPacket->pBuffer, -1);
					memcpy(pPacket->pBuffer, client->GetIP().Data(), 16);
				}
				theStats.AddUpDataOverheadOther(pPacket->size);
				SendPacket(pPacket);
			}
			break;
		case OP_PORTTEST:
			{
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_PortTest", client);
				theStats.AddDownDataOverheadOther(uRawSize);

				m_bPortTestCon = true;
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugSend("OP_PortTest", client);
				Packet *replypacket = new Packet(OP_PORTTEST, 1);
				replypacket->pBuffer[0] = 0x12;
				theStats.AddUpDataOverheadOther(replypacket->size);
				SendPacket(replypacket);
			}
			break;
		case OP_CALLBACK:
			{
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_Callback", client);

				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NATTTESTMODE: OP_CALLBACK] target=%s size=%u\n"), (LPCTSTR)client->DbgGetClientInfo(), (unsigned)uRawSize);
				theStats.AddDownDataOverheadFileRequest(uRawSize);

				if (!Kademlia::CKademlia::IsRunning())
					break;
				CSafeMemFile data(packet, size);
				Kademlia::CUInt128 check;
				data.ReadUInt128(check);
				check.Xor(Kademlia::CUInt128(true));
				if (check == Kademlia::CKademlia::GetPrefs()->GetKadID()) {
					Kademlia::CUInt128 fileid;
					data.ReadUInt128(fileid);
					uchar fileid2[MDX_DIGEST_SIZE];
					fileid.ToByteArray(fileid2);
					if (theApp.sharedfiles->GetFileByID(fileid2) == NULL) {
						if (theApp.downloadqueue->GetFileByID(fileid2) == NULL) {
							client->CheckFailedFileIdReqs(fileid2);
							break;
						}
					}

                    uint32 ip = data.ReadUInt32();
                    uint16 tcp = data.ReadUInt16();
					// OP_CALLBACK carries IPv4 in host order; do not reverse here
					CUpDownClient *callback = theApp.clientlist->FindClientByConnIP(CAddress(ip, false), tcp);
                    if (callback == NULL) {
                        callback = new CUpDownClient(NULL, tcp, ip, 0, 0); // IPv6-TODO: Check this
                        // Ensure endpoints are seeded for NAT-T/uTP
                        CAddress addr(CAddress(ip, false));
                        callback->SetIP(addr);
                        theApp.clientlist->AddClient(callback);
                    } else {
                        // Refresh connect endpoint if missing
                        if (callback->GetConnectIP().IsNull() || callback->GetIP().IsNull()) {
                            CAddress addr(CAddress(ip, false));
                            callback->SetIP(addr);
                        }
                    }
					callback->TryToConnect(true, false, NULL, true); // Prefer uTP connect to perform hole punching
				}
			}
			break;
		case OP_BUDDYPING:
			{
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_BuddyPing", client);
				theStats.AddDownDataOverheadOther(uRawSize);
				CUpDownClient *servingBuddy = theApp.clientlist->GetServingBuddy();
				// Check that ping was from our own buddy or any served buddy, correct version, and not too soon
				bool bOwnServingBuddy = (servingBuddy == client);
				bool bServedBuddy = theApp.clientlist->IsServedBuddy(client);
				if ((!bOwnServingBuddy && !bServedBuddy) || !client->GetKadVersion() || !client->AllowIncomingBuddyPingPong())
					break; // ignore otherwise
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugSend("OP_BuddyPong", client);
				Packet *replypacket = new Packet(OP_BUDDYPONG, 0, OP_EMULEPROT);
				theStats.AddDownDataOverheadOther(replypacket->size);
				SendPacket(replypacket);
				client->SetLastBuddyPingPongTime();
			}
			break;
		case OP_BUDDYPONG:
			{
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_BuddyPong", client);
				theStats.AddDownDataOverheadOther(uRawSize);

				CUpDownClient *servingBuddy = theApp.clientlist->GetServingBuddy();
				if (servingBuddy != client || !client->GetKadVersion())
					//This pong was not from our serving buddy or wrong version. Ignore
					break;
				client->SetLastBuddyPingPongTime();
				//All this is for is to reset our socket timeout.
			}
			break;

		///////////////////////////////////////////////////////////////////////////
		// eServer Buddy Protocol (LowID relay support)
		//
			case OP_ESERVER_BUDDY_REQUEST:
				{
					// LowID client requesting us to be their buddy
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_BUDDY_REQUEST received from %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
					}
					if (thePrefs.GetDebugClientTCPLevel() > 0)
						DebugRecv("OP_EServerBuddyRequest", client);
					theStats.AddDownDataOverheadOther(uRawSize);

					bool bRequestMalformed = false;
					bool bMagicProofSeen = false;
					bool bMagicProofValid = false;
					bool bRequestServerInfoSeen = false;
					uint32 dwRequestServerIP = 0;
					uint16 nRequestServerPort = 0;
					if (size > 0) {
						CSafeMemFile requestData(packet, size);
						if ((requestData.GetLength() - requestData.GetPosition()) < sizeof(uint8)) {
							bRequestMalformed = true;
						} else {
							const uint8 byReqFlags = requestData.ReadUInt8();
							if ((byReqFlags & ESERVERBUDDY_REQUEST_FLAG_MAGIC_PROOF) != 0) {
								bMagicProofSeen = true;
								if ((requestData.GetLength() - requestData.GetPosition()) < (sizeof(uint32) + MDX_DIGEST_SIZE)) {
									bRequestMalformed = true;
								} else {
									const uint32 dwMagicNonce = requestData.ReadUInt32();
									uchar aucMagicProof[MDX_DIGEST_SIZE];
									requestData.ReadHash16(aucMagicProof);
									bMagicProofValid = CUpDownClient::VerifyEServerBuddyMagicProof(dwMagicNonce, aucMagicProof);
								}
							}
							if (!bRequestMalformed && (byReqFlags & ESERVERBUDDY_REQUEST_FLAG_SERVER_INFO) != 0) {
								bRequestServerInfoSeen = true;
								if ((requestData.GetLength() - requestData.GetPosition()) < (sizeof(uint32) + sizeof(uint16))) {
									bRequestMalformed = true;
								} else {
									dwRequestServerIP = requestData.ReadUInt32();
									nRequestServerPort = requestData.ReadUInt16();
									if (dwRequestServerIP == 0 || nRequestServerPort == 0)
										bRequestMalformed = true;
								}
							}
						}
					}

					if (bRequestMalformed || (bMagicProofSeen && !bMagicProofValid)) {
						if (thePrefs.GetLogNatTraversalEvents()) {
							DebugLog(_T("[eServerBuddy] OP_ESERVER_BUDDY_REQUEST rejected due to invalid/malformed magic proof from %s (peerSupports=%u seen=%u valid=%u malformed=%u size=%u)"),
								(LPCTSTR)EscPercent(client->DbgGetClientInfo()),
								client->SupportsEServerBuddyMagicProof() ? 1 : 0,
								bMagicProofSeen ? 1 : 0,
								bMagicProofValid ? 1 : 0,
								bRequestMalformed ? 1 : 0,
								size);
						}
						if (thePrefs.GetDebugClientTCPLevel() > 0)
							DebugSend("OP_EServerBuddyAck (rejected-magicproof)", client);
						CSafeMemFile data_reject(1);
						data_reject.WriteUInt8(ESERVERBUDDY_STATUS_REJECTED);
						Packet* reply = new Packet(data_reject, OP_EMULEPROT, OP_ESERVER_BUDDY_ACK);
						theStats.AddUpDataOverheadOther(reply->size);
						SendPacket(reply);
						break;
					}

					if (!bMagicProofSeen && client->SupportsEServerBuddyMagicProof() && thePrefs.GetLogNatTraversalEvents()) {
						DebugLog(_T("[eServerBuddy] OP_ESERVER_BUDDY_REQUEST from %s has no magic proof extension; accepting for compatibility"),
							(LPCTSTR)EscPercent(client->DbgGetClientInfo()));
					}

					if (bRequestServerInfoSeen) {
						client->SetServerIP(dwRequestServerIP);
						client->SetServerPort(nRequestServerPort);
						client->SetReportedServerIP(dwRequestServerIP);
					}

					// Diagnostic: Log all pre-checks
					const bool bWeAreLowID = theApp.serverconnect ? theApp.serverconnect->IsLowID() : true;
					const bool bClientLowID = client->HasLowID();
					const bool bClientServerKnown = IsClientServerInfoKnown(client);
					const bool bClientSameServer = bClientServerKnown && !IsClientServerMismatchingCurrentED2KServer(client);
					bool bCanAccept = false;
					if (bClientLowID && bClientSameServer)
						bCanAccept = client->CanAcceptBuddyRequest();
					const bool bSlotAvailable = theApp.clientlist ? theApp.clientlist->HasEServerBuddySlotAvailable() : false;
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] Pre-checks: WeAreLowID=%d ClientLowID=%d ClientSrvKnown=%d ClientSameSrv=%d CanAccept=%d SlotAvail=%d"),
							bWeAreLowID ? 1 : 0,
							bClientLowID ? 1 : 0,
							bClientServerKnown ? 1 : 0,
							bClientSameServer ? 1 : 0,
							bCanAccept ? 1 : 0,
							bSlotAvailable ? 1 : 0);
					}

					// Served eServer buddy must be LowID on the same current server.
					if (!bClientLowID || !bClientSameServer) {
						if (thePrefs.GetLogNatTraversalEvents()) {
							AddDebugLogLine(false, _T("[eServerBuddy] Rejected: Invalid buddy requester identity/server (LowID=%d SameServer=%d)"),
								bClientLowID ? 1 : 0,
								bClientSameServer ? 1 : 0);
						}
						RegisterEServerProtocolViolation(client, !bClientLowID ? _T("BuddyRequestNonLowID") : _T("BuddyRequestWrongServer"));
						if (thePrefs.GetDebugClientTCPLevel() > 0)
							DebugSend("OP_EServerBuddyAck (rejected-identity)", client);
						CSafeMemFile data_reject(1);
						data_reject.WriteUInt8(ESERVERBUDDY_STATUS_REJECTED);
						Packet* reply = new Packet(data_reject, OP_EMULEPROT, OP_ESERVER_BUDDY_ACK);
						theStats.AddUpDataOverheadOther(reply->size);
						SendPacket(reply);
						break;
					}

				// We must be HighID to serve as buddy
				if (bWeAreLowID) {
					// Send rejection - we're also LowID
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] Rejected: We are LowID, sending rejection ACK"));
					}
					if (thePrefs.GetDebugClientTCPLevel() > 0)
						DebugSend("OP_EServerBuddyAck (rejected-lowid)", client);
					CSafeMemFile data_reject(1);
					data_reject.WriteUInt8(ESERVERBUDDY_STATUS_REJECTED);
					Packet* reply = new Packet(data_reject, OP_EMULEPROT, OP_ESERVER_BUDDY_ACK);
					theStats.AddUpDataOverheadOther(reply->size);
					SendPacket(reply);
					break;
				}

				// Rate limiting check - NOTE: CanAcceptBuddyRequest was already called above, result cached
				if (!bCanAccept) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] Rejected: Rate limited (silent ignore)"));
					}
					if (thePrefs.GetDebugClientTCPLevel() > 0)
						DebugLog(_T("OP_EServerBuddyRequest: Rate limited, %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
					break;	// Silently ignore
				}

				// Check if we have available slots
				if (!bSlotAvailable) {
					// Send rejection - slots full
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] Rejected: No slots available, sending rejection ACK"));
					}
					if (thePrefs.GetDebugClientTCPLevel() > 0)
						DebugSend("OP_EServerBuddyAck (rejected-full)", client);
					CSafeMemFile data_full(1);
					data_full.WriteUInt8(ESERVERBUDDY_STATUS_SLOTSFULL);
					Packet* reply = new Packet(data_full, OP_EMULEPROT, OP_ESERVER_BUDDY_ACK);
					theStats.AddUpDataOverheadOther(reply->size);
					SendPacket(reply);
					break;
				}

				// Accept the buddy request
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] Accepted buddy request from %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
				}
				theApp.clientlist->AddServedEServerBuddy(client);

				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugSend("OP_EServerBuddyAck (accepted)", client);

				// Send acceptance with our server info
				uint32 dwProbeToken = 0;
				if (client->SupportsEServerBuddyExternalUdpPort()) {
					dwProbeToken = GetRandomUInt32();
					if (dwProbeToken == 0)
						dwProbeToken = 1;
					client->SetEServerExtPortProbeToken(dwProbeToken);
				} else {
					client->SetEServerExtPortProbeToken(0);
				}

				CSafeMemFile data_out(16);
				data_out.WriteUInt8(ESERVERBUDDY_STATUS_ACCEPTED);
				if (theApp.serverconnect && theApp.serverconnect->IsConnected() && theApp.serverconnect->GetCurrentServer()) {
					data_out.WriteUInt32(theApp.serverconnect->GetCurrentServer()->GetIP());
					data_out.WriteUInt16(theApp.serverconnect->GetCurrentServer()->GetPort());
				} else {
					data_out.WriteUInt32(0);
					data_out.WriteUInt16(0);
				}
				if (dwProbeToken != 0)
					data_out.WriteUInt32(dwProbeToken);
				Packet* reply = new Packet(data_out, OP_EMULEPROT, OP_ESERVER_BUDDY_ACK);
				theStats.AddUpDataOverheadOther(reply->size);
				SendPacket(reply);

				// Check for pending relay requests for this newly connected buddy client
				// If a relay was queued before this client connected, forward PEER_INFO now
					if (client && client->HasLowID()) {
						EServerRelayRequest* pReq = theApp.clientlist->FindPendingEServerRelayByLowID(client->GetUserIDHybrid());
						if (pReq && pReq->pRequester && theApp.clientlist->IsServedEServerBuddy(pReq->pRequester)) {
							if (thePrefs.GetLogNatTraversalEvents()) {
								AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_BUDDY_REQUEST: Found pending relay for LowID=%u, sending OP_ESERVER_PEER_INFO to %s"),
									client->GetUserIDHybrid(), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
							}
						
						// Send OP_ESERVER_PEER_INFO to this buddy client with requester's info
						CSafeMemFile data_forward(38); // 4+2+16+16 = IP + Port + RequesterHash + FileHash
						data_forward.WriteUInt32(pReq->dwRequesterIP);
						data_forward.WriteUInt16(pReq->nRequesterKadPort);
						data_forward.WriteHash16(pReq->pRequester->GetUserHash());
						// Include file hash for download context
						if (!isnulmd4(pReq->fileHash))
							data_forward.WriteHash16(pReq->fileHash);
						AppendEServerPeerInfoProbeToken(data_forward, client);
						Packet* fwdPacket = new Packet(data_forward, OP_EMULEPROT, OP_ESERVER_PEER_INFO);
						theStats.AddUpDataOverheadOther(fwdPacket->size);
						// Relay handshake control packets must not wait behind throttled traffic.
						SendPacket(fwdPacket, true, 0, true);
						
						if (thePrefs.GetLogNatTraversalEvents()) {
							AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_BUDDY_REQUEST: Sent OP_ESERVER_PEER_INFO to target - RequesterIP=%s RequesterPort=%u"),
								(LPCTSTR)ipstr(htonl(pReq->dwRequesterIP)), pReq->nRequesterKadPort);
						}
						} else if (pReq != NULL) {
							const DWORD dwRelayAge = (DWORD)(::GetTickCount() - pReq->dwRequestTime);
							if (dwRelayAge >= ESERVER_PENDING_RELAY_OWNER_GRACE_MS) {
								theApp.clientlist->RemovePendingEServerRelayByLowID(client->GetUserIDHybrid());
								if (thePrefs.GetLogNatTraversalEvents()) {
									AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_BUDDY_REQUEST: Dropped stale pending relay for LowID=%u (age=%u ms)"),
										client->GetUserIDHybrid(), (UINT)dwRelayAge);
								}
							} else {
								if (thePrefs.GetLogNatTraversalEvents()) {
									AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_BUDDY_REQUEST: Pending relay owner not ready yet for LowID=%u (age=%u ms), keeping"),
										client->GetUserIDHybrid(), (UINT)dwRelayAge);
								}
							}
						}
					}
				}
			break;

		case OP_ESERVER_BUDDY_ACK:
			{
				// Response to our buddy request
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_BUDDY_ACK received status=%u from %s"), (size >= 1) ? packet[0] : 255u, (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
				}
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_EServerBuddyAck", client);
				theStats.AddDownDataOverheadOther(uRawSize);

				if (size < 1)
					break;

				const bool bExpectedAck = theApp.clientlist->GetServingEServerBuddy() == client
					&& theApp.clientlist->GetEServerBuddyStatus() == Connecting;
				if (!bExpectedAck) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_BUDDY_ACK ignored from unexpected sender/state: %s"),
							(LPCTSTR)EscPercent(client->DbgGetClientInfo()));
					}
					break;
				}

				uint8 byStatus = packet[0];

					if (byStatus == ESERVERBUDDY_STATUS_ACCEPTED) {
						// Accepted!
						uint32 dwServerIP = 0;
						uint16 nServerPort = 0;
					uint32 dwProbeToken = 0;

					// Read server info if present
					if (size >= 7) {
						CSafeMemFile data_in(packet + 1, size - 1);
						dwServerIP = data_in.ReadUInt32();
						nServerPort = data_in.ReadUInt16();
						UNREFERENCED_PARAMETER(dwServerIP);
						UNREFERENCED_PARAMETER(nServerPort);

						if ((data_in.GetLength() - data_in.GetPosition()) >= sizeof(uint32)) {
							dwProbeToken = data_in.ReadUInt32();
						}
					}
					if (dwProbeToken != 0)
						client->SetEServerExtPortProbeToken(dwProbeToken);
					else
						client->SetEServerExtPortProbeToken(0);

						client->OnEServerBuddyAccepted();
						theApp.clientlist->SetServingEServerBuddy(client, Connected);
					} else if (byStatus == ESERVERBUDDY_STATUS_SLOTSFULL) {
						client->OnEServerBuddyRejected();
						AddLogLine(true, _T("eServer Buddy slots full: %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
					} else {
						client->OnEServerBuddyRejectedGeneric();
						AddLogLine(true, _T("eServer Buddy rejected: %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));

						if (thePrefs.GetLogNatTraversalEvents())
							DebugLog(_T("[eServerBuddy] OP_ESERVER_BUDDY_ACK generic reject status=%u from %s"), byStatus, (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
					}
				}
				break;

		case OP_ESERVER_BUDDY_PING:
		{
			// Keepalive ping from buddy
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_EServerBuddyPing", client);
			theStats.AddDownDataOverheadOther(uRawSize);

			// Verify this is from our buddy or a served buddy
			bool bServingBuddy = (theApp.clientlist->GetServingEServerBuddy() == client);
			bool bServedBuddy = theApp.clientlist->IsServedEServerBuddy(client);

			if (!bServingBuddy && !bServedBuddy)
				break;

			auto DisconnectBuddyByPingRule = [&]() {
				if (bServingBuddy)
					theApp.clientlist->SetServingEServerBuddy(NULL, Disconnected);
				if (bServedBuddy)
					theApp.clientlist->RemoveServedEServerBuddy(client);

				Packet* disconn = new Packet(OP_ESERVER_BUDDY_DISCONNECT, 0, OP_EMULEPROT);
				theStats.AddUpDataOverheadOther(disconn->size);
				SendPacket(disconn);
			};

			uint32 dwBuddyServerIP = 0;
			bool bBuddyIsHighID = true; // Backward compatibility with old keepalive format (4-byte payload)
			if (size >= 4) {
				CSafeMemFile data_in(packet, size);
				dwBuddyServerIP = data_in.ReadUInt32();
				if ((data_in.GetLength() - data_in.GetPosition()) >= sizeof(uint8)) {
					const uint8 byPingFlags = data_in.ReadUInt8();
					bBuddyIsHighID = (byPingFlags & ESERVERBUDDY_PING_FLAG_HIGHID) != 0;
				}
			}

			if (dwBuddyServerIP != 0) {
				if (theApp.serverconnect && theApp.serverconnect->IsConnected() && theApp.serverconnect->GetCurrentServer()) {
					const uint32 dwOurServerIP = theApp.serverconnect->GetCurrentServer()->GetIP();
					if (dwBuddyServerIP != dwOurServerIP) {
						if (thePrefs.GetDebugClientTCPLevel() > 0)
							DebugLog(_T("OP_EServerBuddyPing: Server mismatch, disconnecting buddy"));
						DisconnectBuddyByPingRule();
						break;
					}
				}

				if (bServingBuddy && !bBuddyIsHighID) {
					if (thePrefs.GetDebugClientTCPLevel() > 0)
						DebugLog(_T("OP_EServerBuddyPing: Buddy is now LowID on same server, disconnecting"));
					DisconnectBuddyByPingRule();
					break;
				}

				if (bServingBuddy)
					client->TouchEServerBuddyValidServerPing();
			}
			else {
				if (bServingBuddy) {
					const DWORD dwLastValid = client->GetLastEServerBuddyValidServerPing();
					if (dwLastValid != 0 && (DWORD)(::GetTickCount() - dwLastValid) >= ESERVERBUDDY_STALE_SERVER_GRACE_TIME) {
						if (thePrefs.GetDebugClientTCPLevel() > 0)
							DebugLog(_T("OP_EServerBuddyPing: Buddy has no server for too long, disconnecting"));
						DisconnectBuddyByPingRule();
						break;
					}
				}
			}

			// Send pong response with our server IP
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugSend("OP_EServerBuddyPing (pong)", client);
			CSafeMemFile data_pong(5);
			uint8 byPongFlags = 0;
			if (theApp.serverconnect && theApp.serverconnect->IsConnected() && theApp.serverconnect->GetCurrentServer())
				data_pong.WriteUInt32(theApp.serverconnect->GetCurrentServer()->GetIP());
			else
				data_pong.WriteUInt32(0);
			if (theApp.serverconnect && theApp.serverconnect->IsConnected() && !theApp.serverconnect->IsLowID())
				byPongFlags |= ESERVERBUDDY_PING_FLAG_HIGHID;
			data_pong.WriteUInt8(byPongFlags);
			Packet* pong = new Packet(data_pong, OP_EMULEPROT, OP_ESERVER_BUDDY_PING);
			theStats.AddUpDataOverheadOther(pong->size);
			SendPacket(pong);
		}
		break;

		case OP_ESERVER_BUDDY_DISCONNECT:
			{
				// Buddy disconnecting
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_EServerBuddyDisconnect", client);
				theStats.AddDownDataOverheadOther(uRawSize);

				// Remove from served list if present
				theApp.clientlist->RemoveServedEServerBuddy(client);

				// Clear serving buddy if it was this client
				if (theApp.clientlist->GetServingEServerBuddy() == client)
					theApp.clientlist->SetServingEServerBuddy(NULL, Disconnected);
			}
			break;

		case OP_ESERVER_RELAY_REQUEST:
			{
				// LowID client wants us (HighID buddy) to help reach another LowID client
				// We will request a server callback on their behalf
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_RELAY_REQUEST received from %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
				}
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_EServerRelayRequest", client);
				theStats.AddDownDataOverheadOther(uRawSize);

				if (!theApp.clientlist->IsServedEServerBuddy(client)) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] Relay rejected: sender is not in served buddy list (%s)"),
							(LPCTSTR)EscPercent(client->DbgGetClientInfo()));
					}
					RegisterEServerProtocolViolation(client, _T("RelayRequestFromNonServedBuddy"));
					break;
				}
				if (!IsClientKnownOnCurrentED2KServer(client)) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] Relay rejected: sender server mismatch/unknown (%s)"),
							(LPCTSTR)EscPercent(client->DbgGetClientInfo()));
					}
					RegisterEServerProtocolViolation(client, _T("RelayRequestWrongServer"));
					break;
				}
				if (!client->CanAcceptEServerRelayRequest()) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] Relay rejected: sender relay request rate limited (%s)"),
							(LPCTSTR)EscPercent(client->DbgGetClientInfo()));
					}
					RegisterEServerProtocolViolation(client, _T("RelayRequestRateLimited"));
					break;
				}

				// We must be HighID to relay
				if (theApp.serverconnect->IsLowID()) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] Relay rejected: we are LowID"));
					}
					break;
				}

				// Packet format: TargetLowID(4) + RequesterIP(4) + RequesterPort(2) + [FileHash(16)] = 10-26 bytes
				// FileHash is optional for backward compatibility
				if (size < 10) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] Relay rejected: packet too small (size=%u, need 10)"), size);
					}
					break;
				}

				CSafeMemFile data_in(packet, size);
				uint32 dwTargetLowID = data_in.ReadUInt32();
				uint32 dwRequesterIP = data_in.ReadUInt32();
				uint16 nRequesterPort = data_in.ReadUInt16();
				if (client->HasFreshObservedExternalUdpPort()) {
					uint16 nObservedPort = client->GetObservedExternalUdpPort();
					if (nObservedPort != 0)
						nRequesterPort = nObservedPort;
				}

				// Read optional file hash for download context (similar to KAD Rendezvous)
				uchar fileHash[MDX_DIGEST_SIZE];
				md4clr(fileHash);
				bool bHasFileContext = false;
				if (size >= 26 && (size - data_in.GetPosition()) >= MDX_DIGEST_SIZE) {
					data_in.ReadHash16(fileHash);
					if (!isnulmd4(fileHash)) {
						bHasFileContext = true;
						if (thePrefs.GetLogNatTraversalEvents()) {
							AddDebugLogLine(false, _T("[eServerBuddy] File context received: %s"), (LPCTSTR)md4str(fileHash));
						}
					}
				}

				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] Relay request details: targetLowID=%u requesterIP=%s requesterPort=%u FileHash=%s"),
						dwTargetLowID, (LPCTSTR)ipstr(htonl(dwRequesterIP)), nRequesterPort,
						bHasFileContext ? (LPCTSTR)md4str(fileHash) : _T("NULL"));
				}

				// Choose requester IP with anti-spoof preference:
				// 1) Prefer public observed endpoint from the TCP session.
				// 2) Fallback to requester's declared public IP for legacy edge cases.
				uint32 dwObservedIP = client->GetIP().ToUInt32(false);  // Network byte order
				CAddress requesterAddr(dwRequesterIP, false);
				CAddress observedAddr(dwObservedIP, false);

				const bool bRequesterIsPublic = (dwRequesterIP != 0 && requesterAddr.IsPublicIP());
				const bool bObservedIsPublic = (dwObservedIP != 0 && observedAddr.IsPublicIP());

				if (bObservedIsPublic) {
					dwRequesterIP = dwObservedIP;
					if (bRequesterIsPublic && dwRequesterIP != requesterAddr.ToUInt32(false)) {
						if (thePrefs.GetLogNatTraversalEvents()) {
							AddDebugLogLine(false, _T("[eServerBuddy] Relay requester IP mismatch. Using observed endpoint %s instead of declared %s"),
								(LPCTSTR)ipstr(observedAddr), (LPCTSTR)ipstr(requesterAddr));
						}
					}
				} else if (bRequesterIsPublic) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] Relay using declared requester IP (observed not public): %s"),
							(LPCTSTR)ipstr(requesterAddr));
					}
				} else {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] Relay rejected: requester has no usable public IP (declared=%s observed=%s)"),
							(LPCTSTR)ipstr(requesterAddr), (LPCTSTR)ipstr(observedAddr));
					}
					RegisterEServerProtocolViolation(client, _T("RelayRequestInvalidRequesterIP"));
					break;
				}


				// Validate port is non-zero
				if (nRequesterPort == 0) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] Relay rejected: Invalid port 0"));
					}
					break;
				}

				// Validate target LowID is non-zero
				if (dwTargetLowID == 0) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] Relay rejected: Invalid target LowID 0"));
					}
					break;
				}

				// Find the target by LowID (ServerID) since we're on the same server
				// Note: dwTargetLowID is already in LowID format, we need to convert to hybrid format
				uint32 dwOurServerIP = 0;
				if (theApp.serverconnect && theApp.serverconnect->IsConnected() && theApp.serverconnect->GetCurrentServer())
					dwOurServerIP = theApp.serverconnect->GetCurrentServer()->GetIP();
				
				CUpDownClient* pTarget = theApp.clientlist->FindServedEServerBuddyByLowID(dwOurServerIP, dwTargetLowID);
				const bool bServedTargetServerInfoStale = (pTarget != NULL && dwOurServerIP != 0 && pTarget->GetServerIP() != dwOurServerIP);
				if (bServedTargetServerInfoStale && theApp.serverconnect && theApp.serverconnect->GetCurrentServer()) {
					pTarget->SetServerIP(dwOurServerIP);
					pTarget->SetServerPort(theApp.serverconnect->GetCurrentServer()->GetPort());
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] Relay target matched served buddy by LowID despite stale server metadata, refreshing to %s:%u for %s"),
							(LPCTSTR)ipstr(dwOurServerIP),
							theApp.serverconnect->GetCurrentServer()->GetPort(),
							(LPCTSTR)EscPercent(pTarget->DbgGetClientInfo()));
					}
				}
				if (pTarget == NULL)
					pTarget = theApp.clientlist->FindClientByServerID(dwOurServerIP, htonl(dwTargetLowID));

				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] Target lookup by LowID=%u result: pTarget=%p servedBuddy=%u"), dwTargetLowID, pTarget,
						(pTarget != NULL && theApp.clientlist->IsServedEServerBuddy(pTarget)) ? 1u : 0u);
				}

					if (pTarget && theApp.clientlist->IsServedEServerBuddy(pTarget) && pTarget->socket && pTarget->socket->IsConnected()) {
						// Target is already connected to us, forward directly via TCP
						if (thePrefs.GetLogNatTraversalEvents()) {
								AddDebugLogLine(false, _T("[eServerBuddy] Target is connected via TCP, forwarding directly info to %s"),
								(LPCTSTR)EscPercent(pTarget->DbgGetClientInfo()));
						}

						// Store/update pending relay before forwarding.
						// ACK may return very quickly and must find relay context.
						theApp.clientlist->AddPendingEServerRelayByLowID(client, dwTargetLowID, dwRequesterIP, nRequesterPort, fileHash);
						
						// Send OP_ESERVER_PEER_INFO with requester info and file context
						if (thePrefs.GetDebugClientTCPLevel() > 0)
							DebugSend("OP_EServerPeerInfo", pTarget);
					CSafeMemFile data_forward(38);  // IP(4) + Port(2) + RequesterHash(16) + FileHash(16)
					data_forward.WriteUInt32(dwRequesterIP);
					data_forward.WriteUInt16(nRequesterPort);
					data_forward.WriteHash16(client->GetUserHash()); // Requester's hash
					data_forward.WriteHash16(fileHash);  // File hash for download context
					AppendEServerPeerInfoProbeToken(data_forward, pTarget);
					Packet* fwdPacket = new Packet(data_forward, OP_EMULEPROT, OP_ESERVER_PEER_INFO);
					theStats.AddUpDataOverheadOther(fwdPacket->size);
					// Relay handshake control packets must not wait behind throttled traffic.
					pTarget->socket->SendPacket(fwdPacket, true, 0, true);

					// Send "forwarding" response to requester
					if (thePrefs.GetDebugClientTCPLevel() > 0)
						DebugSend("OP_EServerRelayResponse (forwarding TCP)", client);
					CSafeMemFile data_resp(7);
					data_resp.WriteUInt32(0); 
					data_resp.WriteUInt16(0); 
					data_resp.WriteUInt8(ESERVERBUDDY_STATUS_FORWARDING);
					AppendEServerExternalPortTail(data_resp, client);
					Packet* reply = new Packet(data_resp, OP_EMULEPROT, OP_ESERVER_RELAY_RESPONSE);
					theStats.AddUpDataOverheadOther(reply->size);
					SendPacket(reply, true, 0, true);
				} else {
					// Target not connected to us - we need to use SERVER CALLBACK
					// Since we are HighID and should be on the same server, we can request a callback
					if (pTarget != NULL && !theApp.clientlist->IsServedEServerBuddy(pTarget)) {
						if (thePrefs.GetLogNatTraversalEvents()) {
							AddDebugLogLine(false, _T("[eServerBuddy] Target match is not an active served buddy, skipping direct forward for LowID=%u"),
								dwTargetLowID);
						}
					}
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] Target not connected, attempting server callback for LowID=%u"), dwTargetLowID);
					}
					
					if (theApp.serverconnect && theApp.serverconnect->IsConnected()) {
						if (thePrefs.GetLogNatTraversalEvents()) {
							AddDebugLogLine(false, _T("[eServerBuddy] Requesting Server Callback for Target LowID=%u"), dwTargetLowID);
						}
						
						// Request server to callback the target
						Packet* cbPacket = new Packet(OP_CALLBACKREQUEST, 4);
						PokeUInt32(cbPacket->pBuffer, dwTargetLowID);
						if (thePrefs.GetDebugServerTCPLevel() > 0)
							DebugSend("OP_CallbackRequest (relay)", pTarget);
						theStats.AddUpDataOverheadServer(cbPacket->size);
						theApp.serverconnect->SendPacket(cbPacket);
						
						// Store pending relay info so we can complete it when/if target connects to us
						// Note: Server callback makes target connect to US (HighID) via TCP
						// Store by LowID since we don't have the hash yet
						theApp.clientlist->AddPendingEServerRelayByLowID(client, dwTargetLowID, dwRequesterIP, nRequesterPort, fileHash);
						
						// Send "forwarding" response to requester
						if (thePrefs.GetDebugClientTCPLevel() > 0)
							DebugSend("OP_EServerRelayResponse (forwarding ServerCB)", client);
						CSafeMemFile data_resp(7);
						data_resp.WriteUInt32(0);
						data_resp.WriteUInt16(0);
						data_resp.WriteUInt8(ESERVERBUDDY_STATUS_FORWARDING);
						AppendEServerExternalPortTail(data_resp, client);
						Packet* reply = new Packet(data_resp, OP_EMULEPROT, OP_ESERVER_RELAY_RESPONSE);
						theStats.AddUpDataOverheadOther(reply->size);
						SendPacket(reply, true, 0, true);
					} else {
						// Cannot relay
						if (thePrefs.GetLogNatTraversalEvents()) {
							AddDebugLogLine(false, _T("[eServerBuddy] Relay failed: Server not connected"));
						}
							
						if (thePrefs.GetDebugClientTCPLevel() > 0)
							DebugSend("OP_EServerRelayResponse (failed)", client);
						CSafeMemFile data_resp(7);
						data_resp.WriteUInt32(0);
						data_resp.WriteUInt16(0);
						data_resp.WriteUInt8(ESERVERBUDDY_STATUS_REJECTED);
						AppendEServerExternalPortTail(data_resp, client);
						Packet* reply = new Packet(data_resp, OP_EMULEPROT, OP_ESERVER_RELAY_RESPONSE);
						theStats.AddUpDataOverheadOther(reply->size);
						SendPacket(reply, true, 0, true);
					}
				}
			}
			break;

		case OP_ESERVER_RELAY_RESPONSE:
			{
				// Response to our relay request
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_RELAY_RESPONSE received from %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
				}
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_EServerRelayResponse", client);
				theStats.AddDownDataOverheadOther(uRawSize);

				// Minimum format: IP(4) + Port(2) + Status(1) = 7 bytes
				// Extended format: IP(4) + Port(2) + Status(1) + LowID(4) = 11 bytes
				if (size < 7)
					break;

				if (theApp.clientlist->GetServingEServerBuddy() != client
					|| theApp.clientlist->GetEServerBuddyStatus() != Connected) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_RELAY_RESPONSE ignored from non-serving buddy: %s"),
							(LPCTSTR)EscPercent(client->DbgGetClientInfo()));
					}
					break;
				}

				CSafeMemFile data_in(packet, size);
				uint32 dwTargetIP = data_in.ReadUInt32();
				uint16 nTargetKadPort = data_in.ReadUInt16();
				uint8 byStatus = data_in.ReadUInt8();
				
				// Read optional LowID (extended format)
				uint32 dwTargetLowID = 0;
				if (size >= 11) {
					dwTargetLowID = data_in.ReadUInt32();
				}

				// Read optional file hash for download context (similar to KAD Rendezvous)
				uchar fileHash[MDX_DIGEST_SIZE];
				md4clr(fileHash);
				bool bHasFileContext = false;
				CPartFile* pTargetFile = NULL;
				if (size >= 27 && (size - data_in.GetPosition()) >= MDX_DIGEST_SIZE) {
					data_in.ReadHash16(fileHash);
					if (!isnulmd4(fileHash)) {
						bHasFileContext = true;
						pTargetFile = theApp.downloadqueue->GetFileByID(fileHash);
						if (thePrefs.GetLogNatTraversalEvents()) {
							AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_RELAY_RESPONSE: FileHash=%s (partfile=%s)"),
								(LPCTSTR)md4str(fileHash),
								pTargetFile ? (LPCTSTR)EscPercent(pTargetFile->GetFileName()) : _T("NOT FOUND"));
						}
					}
				}

				if ((data_in.GetLength() - data_in.GetPosition()) >= ESERVER_EXT_UDP_PORT_TAIL_SIZE) {
					uint16 nExternalPort = data_in.ReadUInt16();
					uint8 byExternalSrc = data_in.ReadUInt8();
					if (nExternalPort != 0
						&& (byExternalSrc == ESERVER_EXT_UDP_PORT_SRC_KAD || byExternalSrc == ESERVER_EXT_UDP_PORT_SRC_ESERVER_BUDDY))
						thePrefs.SetEServerDiscoveredExternalUdpPort(nExternalPort, byExternalSrc);
				}

				if (byStatus == ESERVERBUDDY_STATUS_FORWARDING) {
					// Intermediate response - relay is in progress
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_RELAY_RESPONSE: Relay forwarded, awaiting target"));
					}
				} else if (byStatus == ESERVERBUDDY_STATUS_ACCEPTED && dwTargetIP != 0) {
					// Final response with target info - start hole punching
					CAddress targetAddr(dwTargetIP, false);
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_RELAY_RESPONSE: Relay success - Target=%s:%u LowID=%u FileHash=%s"),
							(LPCTSTR)ipstr(targetAddr), nTargetKadPort, dwTargetLowID,
							bHasFileContext ? (LPCTSTR)md4str(fileHash) : _T("NULL"));
					}

					// Register pending relay context to handle client object merging/recreation
					// This ensures reqfile is restored when uTP connection is established
					if (bHasFileContext) {
						theApp.clientlist->AddPendingRelayContext(dwTargetIP, nTargetKadPort, fileHash);
					}

					// First try to find the existing source client by LowID.
					// This path preserves request-file context and avoids ambiguous same-IP matches.
					CUpDownClient* pTarget = NULL;
					CServer* pCurrentServer = (theApp.serverconnect != NULL) ? theApp.serverconnect->GetCurrentServer() : NULL;
					uint32 dwOurServerIP = (pCurrentServer != NULL) ? pCurrentServer->GetIP() : 0;
					uint16 nOurServerPort = (pCurrentServer != NULL) ? pCurrentServer->GetPort() : 0;

					if (dwTargetLowID != 0 && dwOurServerIP != 0) {
						pTarget = theApp.clientlist->FindClientByServerID(dwOurServerIP, htonl(dwTargetLowID));
						if (pTarget == NULL)
							pTarget = theApp.clientlist->FindClientByServerID(dwOurServerIP, dwTargetLowID);

						if (pTarget != NULL) {
							if (thePrefs.GetLogNatTraversalEvents()) {
								AddDebugLogLine(false, _T("[eServerBuddy] Found existing source client by LowID=%u: %s (reqfile=%s)"),
									dwTargetLowID,
									(LPCTSTR)EscPercent(pTarget->DbgGetClientInfo()),
									pTarget->GetRequestFile() ? _T("SET") : _T("NULL"));
							}
						}
					}

					// Fallback by endpoint must use UDP/Kad port matching.
					// Using FindClientByIP(ip, port) here is incorrect because it compares TCP port.
					if (pTarget == NULL && nTargetKadPort != 0) {
						pTarget = theApp.clientlist->FindClientByIP_KadPort(targetAddr, nTargetKadPort);
						if (pTarget == NULL)
							pTarget = theApp.clientlist->FindClientByIP_UDP(targetAddr, nTargetKadPort);

						if (pTarget != NULL) {
							if (thePrefs.GetLogNatTraversalEvents()) {
								AddDebugLogLine(false, _T("[eServerBuddy] Found source client by endpoint %s:%u (reqfile=%s)"),
									(LPCTSTR)ipstr(targetAddr),
									nTargetKadPort,
									pTarget->GetRequestFile() ? _T("SET") : _T("NULL"));
							}
						}
					}

					CUpDownClient* pServingBuddy = theApp.clientlist->GetServingEServerBuddy();
					if (pTarget != NULL && pTarget == pServingBuddy) {
						if (thePrefs.GetLogNatTraversalEvents()) {
							AddDebugLogLine(false, _T("[eServerBuddy] Ignoring serving buddy as relay target candidate: %s"),
								(LPCTSTR)EscPercent(pTarget->DbgGetClientInfo()));
						}
						pTarget = NULL;
					}

					// Last resort: create a placeholder client using LowID identity.
					if (pTarget == NULL) {
						uint32 dwCtorUserID = (dwTargetLowID != 0) ? dwTargetLowID : dwTargetIP;
						pTarget = new CUpDownClient(NULL, 0, dwCtorUserID, dwOurServerIP, nOurServerPort, true);
						theApp.clientlist->AddClient(pTarget);
						if (thePrefs.GetLogNatTraversalEvents()) {
							AddDebugLogLine(false, _T("[eServerBuddy] Created relay target placeholder for %s:%u (LowID=%u)"),
								(LPCTSTR)ipstr(targetAddr), nTargetKadPort, dwTargetLowID);
						}
					}

					if (pTarget->GetRequestFile() == NULL && bHasFileContext && pTargetFile != NULL) {
						pTarget->SetRequestFile(pTargetFile);
						if (thePrefs.GetLogNatTraversalEvents()) {
							AddDebugLogLine(false, _T("[eServerBuddy] SetRequestFile from relay context: %s"),
								(LPCTSTR)EscPercent(pTargetFile->GetFileName()));
						}
					}
					
					// Update client with NAT traversal info - the public IP we just learned
				pTarget->SetIP(targetAddr);
				pTarget->SetKadPort(nTargetKadPort);
				pTarget->SetUDPPort(nTargetKadPort);
				pTarget->SetNatTraversalSupport(true);

					// Register NAT expectation for requester side fallback.
					// If initial uTP socket resets, on_utp_accept can still recover via MatchNatExpectation.
					if (theApp.clientudp) {
						theApp.clientudp->SeedNatTraversalExpectation(pTarget, targetAddr, nTargetKadPort);
						if (thePrefs.GetLogNatTraversalEvents()) {
							AddDebugLogLine(false, _T("[eServerBuddy] Registered NAT expectation for target %s:%u"),
								(LPCTSTR)ipstr(targetAddr), nTargetKadPort);
						}
					}

					// Send hole punch burst for NAT traversal (12 packets - same as KAD Rendezvous)
					// Both sides must punch holes simultaneously for Port Restricted NAT
					uint16 buddyWin = thePrefs.GetNatTraversalPortWindow();
					uint16 buddySweepWin = thePrefs.GetNatTraversalSweepWindow();
					uint16 buddySweepSpan = std::min<uint16>(buddyWin, buddySweepWin);

					if (theApp.clientudp) {
						for (int i = 0; i < 12; ++i) {
							Packet* hp = new Packet(OP_EMULEPROT);
							hp->opcode = OP_HOLEPUNCH;
							theApp.clientudp->SendPacket(hp, targetAddr, nTargetKadPort, false, NULL, false, 0);
						}
						uint16 span = buddySweepSpan;
						for (uint16 off = 1; off <= span; ++off) {
							if (nTargetKadPort > off) {
								Packet* hp = new Packet(OP_EMULEPROT);
								hp->opcode = OP_HOLEPUNCH;
								theApp.clientudp->SendPacket(hp, targetAddr, (uint16)(nTargetKadPort - off), false, NULL, false, 0);
						}
						if (nTargetKadPort + off <= 65535) {
							Packet* hp = new Packet(OP_EMULEPROT);
							hp->opcode = OP_HOLEPUNCH;
							theApp.clientudp->SendPacket(hp, targetAddr, (uint16)(nTargetKadPort + off), false, NULL, false, 0);
						}
					}
					if (thePrefs.GetLogNatTraversalEvents()) {
							AddDebugLogLine(false, _T("[eServerBuddy] Sent OP_HOLEPUNCH burst (12 packets) to target %s:%u"),
							(LPCTSTR)ipstr(targetAddr), nTargetKadPort);
					}
					
					// Proceed immediately to uTP connect - Waiting causes NAT mapping TTL to expire
				}

				// Reset connecting state so TryToConnect can proceed
				// Client may be in WaitCallback state from initial relay request
				pTarget->ResetConnectingState();
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] Reset connecting state for TryToConnect"));
				}

						const uint8 kBuddyRelayRetries = 2;
						pTarget->MarkNatTRendezvous(kBuddyRelayRetries, true);
						pTarget->QueueDeferredNatConnect(targetAddr, nTargetKadPort, buddySweepSpan);
						pTarget->ArmEServerRelayNatTGuard();

				// Try to connect to peer via uTP if available
				// If pTarget has m_reqfile set (found by LowID), file request will be triggered!
				pTarget->TryToConnect(true, false, NULL, true);

				} else {
					// Relay failed
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_RELAY_RESPONSE: Relay failed (status=%u)"), byStatus);
					}
				}
			}

			break;

		case OP_ESERVER_PEER_INFO:
			{
				// HighID buddy is telling us (LowID) about another LowID client that wants to connect
				// This is received after server callback - we may not have established buddy relationship yet
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(DLP_DEFAULT, false, _T("[eServerBuddy] *** OP_ESERVER_PEER_INFO HANDLER ENTERED *** from %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
					AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO received from %s"), 
						(LPCTSTR)EscPercent(client->DbgGetClientInfo()));
				}
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_EServerPeerInfo", client);
				theStats.AddDownDataOverheadOther(uRawSize);

				const bool bFromServingBuddy = theApp.clientlist->GetServingEServerBuddy() == client
					&& (theApp.clientlist->GetEServerBuddyStatus() == Connected || theApp.clientlist->GetEServerBuddyStatus() == Connecting);
				const bool bFallbackTrustedSender = !bFromServingBuddy
					&& IsClientTrustedForPeerInfoFallback(client);
				if (!bFromServingBuddy && !bFallbackTrustedSender) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO rejected from unauthorized sender: %s"),
							(LPCTSTR)EscPercent(client->DbgGetClientInfo()));
					}
					RegisterEServerProtocolViolation(client, _T("PeerInfoUnauthorizedSender"));
					break;
				}

				if (size < 6)	// 4 (IP) + 2 (port)
					break;

				CSafeMemFile data_in(packet, size);
				uint32 dwPeerIP = data_in.ReadUInt32();
				uint16 nPeerPort = data_in.ReadUInt16();
				
				// Optionally read requester's hash if present
				uchar requesterHash[16];
				md4clr(requesterHash);
				if (size >= 22) {
					data_in.ReadHash16(requesterHash);
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO: RequesterHash=%s"), (LPCTSTR)md4str(requesterHash));
					}
				}
				const bool bHasRequesterHash = !isnulmd4(requesterHash);
				if (!bHasRequesterHash && !bFromServingBuddy) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO rejected (missing requester hash) from %s"),
							(LPCTSTR)EscPercent(client->DbgGetClientInfo()));
					}
					RegisterEServerProtocolViolation(client, _T("PeerInfoMissingRequesterHash"));
					break;
				}

				// Read optional file hash for download context (similar to KAD Rendezvous)
				uchar fileHash[MDX_DIGEST_SIZE];
				md4clr(fileHash);
				bool bHasFileContext = false;
				if (size >= 38 && (size - data_in.GetPosition()) >= MDX_DIGEST_SIZE) {
					data_in.ReadHash16(fileHash);
					if (!isnulmd4(fileHash)) {
						bHasFileContext = true;
						if (thePrefs.GetLogNatTraversalEvents()) {
							AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO: FileHash=%s (download context)"), (LPCTSTR)md4str(fileHash));
						}
					}
				}

				uint32 dwProbeToken = 0;
				if ((data_in.GetLength() - data_in.GetPosition()) >= sizeof(uint32)) {
					dwProbeToken = data_in.ReadUInt32();
					if (dwProbeToken != 0)
						client->SetEServerExtPortProbeToken(dwProbeToken);
				}
				if (!thePrefs.HasValidExternalUdpPort() && client->GetEServerExtPortProbeToken() != 0)
					client->SendEServerUdpProbe();

				// NOTE: dwPeerIP is in network byte order (from GetIP().ToUInt32(false))
				// WriteUInt32/ReadUInt32 preserve the original byte order value
				// CAddress constructor with 'false' expects network byte order
				CAddress peerAddr(dwPeerIP, false);
				if (!peerAddr.IsPublicIP() || nPeerPort == 0) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO rejected invalid endpoint %s:%u"),
							(LPCTSTR)ipstr(peerAddr), nPeerPort);
					}
					RegisterEServerProtocolViolation(client, _T("PeerInfoInvalidEndpoint"));
					break;
				}
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO: Peer info received - IP=%s Port=%u (rawIP=0x%08X)"),
						(LPCTSTR)ipstr(peerAddr), nPeerPort, dwPeerIP);
				}

				// Send ACK to the buddy (sender of this packet) with our external KAD port
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugSend("OP_EServerPeerInfoAck", client);

				// Prefer external UDP port for NAT traversal, fallback to local UDP port
				uint16 nMyKadPort = thePrefs.GetBestExternalUdpPort();
				if (nMyKadPort == 0)
					nMyKadPort = thePrefs.GetUDPPort();

				// Get our public IP to send to the buddy
				// The buddy will forward this to the requester for hole punching
				CAddress myPublicAddr = theApp.GetPublicIP();
				uint32 dwMyPublicIP = 0;
				if (!myPublicAddr.IsNull() && myPublicAddr.GetType() == CAddress::IPv4) {
					dwMyPublicIP = myPublicAddr.ToUInt32(false);  // Network byte order
				}

				// ACK format: KadPort(2) + PublicIP(4) = 6 bytes
				CSafeMemFile data_ack(6);
				data_ack.WriteUInt16(nMyKadPort);
				data_ack.WriteUInt32(dwMyPublicIP);  // Add our public IP
				Packet* ackPacket = new Packet(data_ack, OP_EMULEPROT, OP_ESERVER_PEER_INFO_ACK);
				theStats.AddUpDataOverheadOther(ackPacket->size);
				SendPacket(ackPacket, true, 0, true);
				
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO: Sent ACK with MyKadPort=%u MyPublicIP=%s"), 
						nMyKadPort, (LPCTSTR)ipstr(myPublicAddr));
				}


				// Now we need to initiate hole punch and uTP connection to the requester
				// This is similar to what happens in OP_RENDEZVOUS handler
				
				// Find or create client object for the requester
				// We must distinguish by BOTH IP AND PORT, not just IP!
				// The sender (Buddy) and the requester may share the same public IP
				// but use different ports (NAT scenario with Instance1 and Instance4 both behind 46.197.1.119)
				CUpDownClient* pPeer = NULL;
				
				// First, try to find by user hash if available
				if (bHasRequesterHash)
					pPeer = theApp.clientlist->FindClientByUserHash(requesterHash, peerAddr, nPeerPort);
				
				// Then, try to find by IP AND KAD Port (both must match!)
				// Use FindClientByIP_KadPort because nPeerPort is a UDP port!
				// FindClientByIP expects a TCP port, which would fail or return the wrong client.
				if (!pPeer)
					pPeer = theApp.clientlist->FindClientByIP_KadPort(peerAddr, nPeerPort);
				
				// Safety check: Never bind to the sender or an unrelated hash.
				// Sender may be the buddy before we establish the serving buddy relationship.
				if (pPeer && pPeer == client) {
					if (thePrefs.GetLogNatTraversalEvents())
						AddDebugLogLine(DLP_LOW, false, _T("[eServerBuddy] WARNING: Found sender instead of requester. Forcing new client creation."));
					pPeer = NULL;
				}

				if (pPeer && bHasRequesterHash && pPeer->HasValidHash() && memcmp(pPeer->GetUserHash(), requesterHash, 16) != 0) {
					if (thePrefs.GetLogNatTraversalEvents())
						AddDebugLogLine(DLP_LOW, false, _T("[eServerBuddy] WARNING: Hash mismatch for requester. Forcing new client creation."));
					pPeer = NULL;
				}

				// Safety check: Ensure we didn't find our own Buddy!
				// In NAT loopback scenarios (Instance1 and Instance4 on same IP), we might accidentally find Buddy.
				// But Buddy's KadPort should be different from Requester's KadPort.
				CUpDownClient* pServingBuddy = theApp.clientlist->GetServingEServerBuddy();
				if (pPeer && pPeer == pServingBuddy) {
					if (thePrefs.GetLogNatTraversalEvents())
						AddDebugLogLine(DLP_LOW, false, _T("[eServerBuddy] WARNING: Found Buddy instead of Requester! Forcing new client creation."));
					pPeer = NULL; 
				} else if (pPeer) {
					if (thePrefs.GetLogNatTraversalEvents())
						AddDebugLogLine(DLP_DEFAULT, false, _T("[eServerBuddy] Found existing client by IP+KadPort: %s"), (LPCTSTR)EscPercent(pPeer->DbgGetClientInfo()));
				}

				// Always create a new client for the requester if not found by hash or IP+KadPort
				if (!pPeer) {
					pPeer = new CUpDownClient(NULL, 0, dwPeerIP, 0, 0, true);
					if (bHasRequesterHash)
						pPeer->SetUserHash(requesterHash, true);
					theApp.clientlist->AddClient(pPeer);
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO: Created NEW client for requester %s:%u"),
							(LPCTSTR)ipstr(peerAddr), nPeerPort);
					}
					if (thePrefs.GetLogNatTraversalEvents())
						AddDebugLogLine(DLP_DEFAULT, false, _T("[eServerBuddy] Created NEW client for requester %s:%u"), (LPCTSTR)ipstr(peerAddr), nPeerPort);
				} else {
					if (bHasRequesterHash && !pPeer->HasValidHash())
						pPeer->SetUserHash(requesterHash, true);
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO: Found existing client for requester %s:%u"),
							(LPCTSTR)ipstr(peerAddr), nPeerPort);
					}
				}

					bool bWeAreUploader = false;
					if (bHasFileContext) {
						CKnownFile* sharedFile = theApp.sharedfiles->GetFileByID(fileHash);
						if (sharedFile != NULL) {
							if (isnulmd4(pPeer->GetUploadFileID()))
								pPeer->SetUploadFileID(sharedFile);
							bWeAreUploader = true;
						} else {
							CPartFile* reqFile = theApp.downloadqueue->GetFileByID(fileHash);
							if (reqFile != NULL && pPeer->GetRequestFile() == NULL)
								pPeer->SetRequestFile(reqFile);
						}
					}

				// Set NAT traversal info
				pPeer->SetKadPort(nPeerPort);
				pPeer->SetUDPPort(nPeerPort);
				pPeer->SetNatTraversalSupport(true);
				CAddress peerAddrCopy = peerAddr;
				pPeer->SetIP(peerAddrCopy);

				// Register NAT expectation so OP_HOLEPUNCH handler finds the correct client
				// This ensures incoming hole punch packets are matched to this peer, not the buddy
				if (theApp.clientudp) {
					theApp.clientudp->SeedNatTraversalExpectation(pPeer, peerAddr, nPeerPort);
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO: Registered NAT expectation for %s:%u"),
							(LPCTSTR)ipstr(peerAddr), nPeerPort);
					}
				}
				
				// Send hole punch burst to requester - this opens our NAT for incoming uTP
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO: Sending hole punch burst to %s:%u"),
						(LPCTSTR)ipstr(peerAddr), nPeerPort);
				}
					uint16 win = thePrefs.GetNatTraversalPortWindow();
					uint16 sweepWin = thePrefs.GetNatTraversalSweepWindow();
					uint16 span = std::min<uint16>(win, sweepWin);
					if (theApp.clientudp) {
						// Keep packet count aligned with rendezvous bursts.
							for (int i = 0; i < 12; ++i) {
								Packet* hp = new Packet(OP_EMULEPROT);
								hp->opcode = OP_HOLEPUNCH;
								theApp.clientudp->SendPacket(hp, peerAddr, nPeerPort, false, NULL, false, 0);
							}
							for (uint16 off = 1; off <= span; ++off) {
								if (nPeerPort > off) {
									Packet* hp = new Packet(OP_EMULEPROT);
									hp->opcode = OP_HOLEPUNCH;
								theApp.clientudp->SendPacket(hp, peerAddr, (uint16)(nPeerPort - off), false, NULL, false, 0);
						}
						if (nPeerPort + off <= 65535) {
							Packet* hp = new Packet(OP_EMULEPROT);
							hp->opcode = OP_HOLEPUNCH;
							theApp.clientudp->SendPacket(hp, peerAddr, (uint16)(nPeerPort + off), false, NULL, false, 0);
						}
					}
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO: Sent 12 OP_HOLEPUNCH packets"));
					}
				}

					// Keep dual-active behavior for relay data channel.
					// Passive-only uploader mode can stall after initial payload on some NAT paths.
					if (bWeAreUploader) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO: Uploader side detected, using dual-active uTP connect for relay stability to %s:%u"),
							(LPCTSTR)ipstr(peerAddr), nPeerPort);
					}
					}
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO: Initiating ACTIVE uTP connect to %s:%u"),
							(LPCTSTR)ipstr(peerAddr), nPeerPort);
					}
					const uint8 kPeerRelayRetries = 2;
					pPeer->MarkNatTRendezvous(kPeerRelayRetries, true);
					pPeer->QueueDeferredNatConnect(peerAddr, nPeerPort, span);
					pPeer->ArmEServerRelayNatTGuard();
					pPeer->TryToConnect(true, false, NULL, true);

				}
				break;

		case OP_ESERVER_PEER_INFO_ACK:
			{
				// Target LowID responded with its KAD port
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_EServerPeerInfoAck", client);
				theStats.AddDownDataOverheadOther(uRawSize);

				// We must be HighID serving as buddy
				if (theApp.serverconnect->IsLowID()) {
					if (thePrefs.GetDebugClientTCPLevel() > 0)
						DebugLog(_T("OP_EServerPeerInfoAck ignored: we're LowID"));
					break;
				}
				if (IsClientServerMismatchingCurrentED2KServer(client)) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO_ACK ignored from client on different server: %s"),
							(LPCTSTR)EscPercent(client->DbgGetClientInfo()));
					}
					break;
				}
				if (!client->HasLowID()) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO_ACK ignored from HighID client: %s"),
							(LPCTSTR)EscPercent(client->DbgGetClientInfo()));
					}
					break;
				}

				if (size < 2)  // Minimum: KadPort (uint16)
					break;

				CSafeMemFile data_in(packet, size);
				uint16 nTargetKadPort = data_in.ReadUInt16();
				if (client->HasFreshObservedExternalUdpPort()) {
					uint16 nObservedPort = client->GetObservedExternalUdpPort();
					if (nObservedPort != 0)
						nTargetKadPort = nObservedPort;
				}
				
				// Read target's public IP if present (new extended ACK format: 6 bytes)
				uint32 dwTargetPublicIP = 0;
				if (size >= 6) {
					dwTargetPublicIP = data_in.ReadUInt32();
				}

				// Find pending relay request for this target using LowID (must match AddPendingEServerRelayByLowID)
				uint32 dwTargetLowID = client->GetUserIDHybrid();
				if (dwTargetLowID == 0) {
					if (thePrefs.GetDebugClientTCPLevel() > 0)
						DebugLog(_T("OP_EServerPeerInfoAck: Client has no LowID, ignoring"));
					break;
				}

				EServerRelayRequest* pReq = theApp.clientlist->FindPendingEServerRelayByLowID(dwTargetLowID);
				if (!pReq) {
					if (thePrefs.GetDebugClientTCPLevel() > 0)
						DebugLog(_T("OP_EServerPeerInfoAck: No pending relay found for LowID=%u (%s)"), dwTargetLowID, (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
					break;
				}

				if (pReq->pRequester == NULL || !theApp.clientlist->IsServedEServerBuddy(pReq->pRequester)) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO_ACK rejected: pending requester no longer served (LowID=%u)"),
							dwTargetLowID);
					}
					theApp.clientlist->RemovePendingEServerRelayByLowID(dwTargetLowID);
					RegisterEServerProtocolViolation(client, _T("PeerInfoAckRequesterNotServed"));
					break;
				}

				if (pReq->pRequester == client) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO_ACK rejected: requester/target collision for LowID=%u"),
							dwTargetLowID);
					}
					theApp.clientlist->RemovePendingEServerRelayByLowID(dwTargetLowID);
					RegisterEServerProtocolViolation(client, _T("PeerInfoAckRequesterCollision"));
					break;
				}

				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO_ACK: Found pending relay for LowID=%u"), dwTargetLowID);
				}
				pReq->dwTargetPublicIP = dwTargetPublicIP;
				pReq->nTargetKadPort = nTargetKadPort;
				pReq->bAwaitingTargetExtPort = false;

				// Try immediate final response first using ACK/fallback ports.
				// External UDP probe is a best-effort optimization and must not block relay completion.
				if (theApp.clientlist->TrySendPendingEServerRelayResponseForTarget(client))
					break;

				if (!client->HasFreshObservedExternalUdpPort() && client->GetEServerExtPortProbeToken() != 0) {
					pReq->bAwaitingTargetExtPort = true;
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO_ACK: Immediate final response failed, waiting for external port probe from target"));
					}
					break;
				}

				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] OP_ESERVER_PEER_INFO_ACK: Failed to send final relay response for LowID=%u"), dwTargetLowID);
				}
			}
			break;

	case OP_REASKCALLBACKTCP:
			{
				theStats.AddDownDataOverheadFileRequest(uRawSize);
				CUpDownClient *buddy = theApp.clientlist->GetServingBuddy();
				// Accept callback forwards primarily from our serving buddy and any served buddy we host.
				bool bServingBuddy = (buddy == client);
				bool bServedBuddy = theApp.clientlist->IsServedBuddy(client);
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal] OP_REASKCALLBACKTCP received from %s buddy: %s (size=%u)"), bServingBuddy ? _T("serving") : _T("served"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()), size);
				CSafeMemFile data_in(packet, size);
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugLog(_T("OP_ReaskCallbackTCP accepted from %s buddy: %s"), bServingBuddy ? _T("serving") : _T("served"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
				int Offset = 4;
				CAddress destip;
				uint32 dwIP = data_in.ReadUInt32();
				if (dwIP == -1)
				{
					Offset += MDX_DIGEST_SIZE;
					byte uIP[MDX_DIGEST_SIZE];
					data_in.ReadHash16(uIP);
					destip = CAddress(uIP);
				} else
					destip = CAddress(dwIP, false);
				uint16 destport = data_in.ReadUInt16();
				if ((destip.IsNull() || !destip.IsPublicIP()) && client != NULL) {
					const CAddress& memo = client->GetLastCallbackRequesterIP();
					if (!memo.IsNull() && memo.IsPublicIP())
						destip = memo;
				}
				if (destport == 0 && client != NULL) {
					uint16 hint = client->GetKadPort();
					if (hint == 0)
						hint = client->GetUDPPort();
					if (hint != 0)
						destport = hint;
				}
				uchar reqfilehash[MDX_DIGEST_SIZE];
				data_in.ReadHash16(reqfilehash);
				
				// Try to read hint endpoint (requester self-reported endpoint) from end of packet
				CAddress hintIP;
				uint16 hintPort = 0;
				UINT consumed = (UINT)data_in.GetPosition();
				if (size > consumed && (size - consumed) >= 6) {
					// Peek last 6 bytes to check if it's a hint endpoint
					UINT hintOffset = size - 6;
					if (hintOffset >= consumed) {
						uint32 hintIPv4 = PeekUInt32(packet + hintOffset);
						uint16 hintPortValue = PeekUInt16(packet + hintOffset + 4);
						if (hintIPv4 != 0 && hintPortValue != 0) {
							hintIP = CAddress(hintIPv4, false);
							hintPort = hintPortValue;
							if (thePrefs.GetLogNatTraversalEvents())
								DebugLog(_T("[NatTraversal] OP_REASKCALLBACKTCP: Read hint endpoint %s:%u (buddy-observed: %s:%u)"), (LPCTSTR)ipstr(hintIP), hintPort, (LPCTSTR)ipstr(destip), destport);
						}
					}
				}
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_ReaskCallbackTCP", client, reqfilehash);

				// If not from our buddy, consider third-party forward for NAT-T custom payloads only.
				if (!bServingBuddy && !bServedBuddy && !isnulmd4(reqfilehash)) {
					// Non-custom payloads are only accepted from our buddy relations.
					break;
				}

				if (isnulmd4(reqfilehash)) {
					// Custom payload forwarded via OP_REASKCALLBACKTCP:
					// After the destination header (Offset + 2 bytes port) and 16 bytes zero-filehash,
					// next byte is the embedded UDP opcode. Compute the exact consumed length from the stream.
					// Position so far: 4 (IPv4) or 20 (IPv6) -> 'Offset', then +2 (destport) +16 (zero hash) -> current pos.
					uint8 embeddedOpcode = data_in.ReadUInt8();
					UINT payloadPos = (UINT)data_in.GetPosition();
					if (!bServingBuddy && !bServedBuddy) {
						const bool bFromServingEServerBuddy = theApp.clientlist != NULL
							&& theApp.clientlist->GetServingEServerBuddy() == client
							&& theApp.clientlist->GetEServerBuddyStatus() == Connected;
						const bool bAllowedEmbeddedOpcode = (embeddedOpcode == OP_RENDEZVOUS)
							|| (bFromServingEServerBuddy && (embeddedOpcode == OP_HOLEPUNCH || embeddedOpcode == OP_REASKACK));
						if (!bAllowedEmbeddedOpcode) {
							if (thePrefs.GetDebugClientTCPLevel() > 0)
								DebugLog(_T("OP_ReaskCallbackTCP: third-party forward dropped due to unsupported embedded opcode=0x%02x"), embeddedOpcode);
							break;
						}
					}
					// Third-party forward validation: ensure this destination matches a known download client when not from buddy
					if (!bServingBuddy && !bServedBuddy) {
						bool multi = false;
						CUpDownClient* dlc = theApp.downloadqueue->GetDownloadClientByIP_UDP(destip, destport, true, &multi);
						if (dlc == NULL && !multi) {
							if (thePrefs.GetDebugClientTCPLevel() > 0)
								DebugLog(_T("OP_ReaskCallbackTCP: third-party forward dropped; no matching download client for %s:%u"), (LPCTSTR)ipstr(destip), destport);
							break;
						}
					}
					if (payloadPos <= (UINT)size)
						theApp.clientudp->ProcessPacket(packet + payloadPos, size - payloadPos, embeddedOpcode, destip, destport);
					break;
				}

				CKnownFile *reqfile = theApp.sharedfiles->GetFileByID(reqfilehash);

				bool bSenderMultipleIpUnknown = false;
				CUpDownClient *sender = theApp.uploadqueue->GetWaitingClientByIP_UDP(destip, destport, true, &bSenderMultipleIpUnknown);
				if (!reqfile) {
					if (thePrefs.GetDebugClientUDPLevel() > 0)
						DebugSend("OP_FileNotFound", NULL);
					Packet *response = new Packet(OP_FILENOTFOUND, 0, OP_EMULEPROT);
					theStats.AddUpDataOverheadFileRequest(response->size);
					if (sender != NULL)
						theApp.clientudp->SendPacket(response, destip, destport, sender->ShouldReceiveCryptUDPPackets(), sender->GetUserHash(), false, 0);
					else
						theApp.clientudp->SendPacket(response, destip, destport, false, NULL, false, 0);
					break;
				}

				if (sender) {
					//Make sure we are still thinking about the same file
					if (md4equ(reqfilehash, sender->GetUploadFileID())) {
						sender->IncrementAskedCount();
						sender->SetLastUpRequest();
						//I messed up when I first added extended info to UDP
						//I should have originally used the entire ProcessExtenedInfo the first time.
						//So now I am forced to check UDPVersion to see if we are sending all the extended info.
						//For now on, we should not have to change anything here if we change
						//anything to the extended info data as this will be taken care of in ProcessExtendedInfo()
						//Update extended info.
						if (sender->GetUDPVersion() > 3)
							sender->ProcessExtendedInfo(data_in, reqfile);
							//Update our complete source counts.
						else if (sender->GetUDPVersion() > 2) {
							uint16 nCompleteCountLast = sender->GetUpCompleteSourcesCount();
							uint16 nCompleteCountNew = data_in.ReadUInt16();
							sender->SetUpCompleteSourcesCount(nCompleteCountNew);
							if (nCompleteCountLast != nCompleteCountNew)
								reqfile->UpdatePartsInfo();
						}
						CSafeMemFile data_out(128);
						if (sender->GetUDPVersion() > 3)
							if (reqfile->IsPartFile())
								static_cast<CPartFile*>(reqfile)->WritePartStatus(data_out);
							else
								data_out.WriteUInt16(0);

						data_out.WriteUInt16((uint16)theApp.uploadqueue->GetWaitingPosition(sender));
						if (thePrefs.GetDebugClientUDPLevel() > 0)
							DebugSend("OP_ReaskAck", sender);
						Packet *response = new Packet(data_out, OP_EMULEPROT);
						response->opcode = OP_REASKACK;
						theStats.AddUpDataOverheadFileRequest(response->size);
						theApp.clientudp->SendPacket(response, destip, destport, sender->ShouldReceiveCryptUDPPackets(), sender->GetUserHash(), false, 0);
					} else {
						DebugLogWarning(_T("Client UDP socket; OP_REASKCALLBACKTCP; reqfile does not match"));
						TRACE(_T("reqfile:         %s\n"), (LPCTSTR)DbgGetFileInfo(reqfile->GetFileHash()));
						TRACE(_T("sender->GetRequestFile(): %s\n"), sender->GetRequestFile() ? (LPCTSTR)DbgGetFileInfo(sender->GetRequestFile()->GetFileHash()) : _T("(null)"));
					}
					} else {
						// sender not in upload queue - this is a NAT-T callback request
						if (!bSenderMultipleIpUnknown) {
							if (theApp.uploadqueue->GetWaitingUserCount() + 50 > thePrefs.GetQueueSize()) {
								if (thePrefs.GetDebugClientUDPLevel() > 0)
									DebugSend("OP_QueueFull", NULL);
								Packet *response = new Packet(OP_QUEUEFULL, 0, OP_EMULEPROT);
								theStats.AddUpDataOverheadFileRequest(response->size);
								theApp.clientudp->SendPacket(response, destip, destport, false, NULL, false, 0);
							} else {
								// Send OP_HOLEPUNCH for NAT traversal (hole punching from receiver side)
								Packet *holepunch = new Packet(OP_EMULEPROT);
								holepunch->opcode = OP_HOLEPUNCH;
								theApp.clientudp->SendPacket(holepunch, destip, destport, false, NULL, false, 0);
								if (thePrefs.GetLogNatTraversalEvents())
									DebugLog(_T("[NatTraversal][OP_REASKCALLBACKTCP][Receiver] Sending OP_HOLEPUNCH to %s:%u for NAT traversal"), (LPCTSTR)ipstr(destip), destport);
								
								// Send ephemeral OP_REASKACK to enable NAT-T/uTP connection establishment
								CSafeMemFile data_out(128);
								data_out.WriteUInt16(0); // no part status for ephemeral response
								data_out.WriteUInt16((uint16)-1); // queue position -1 indicates ephemeral response
								if (thePrefs.GetDebugClientUDPLevel() > 0)
									DebugSend("OP_ReaskAck (ephemeral for NAT-T)", NULL);
								if (thePrefs.GetLogNatTraversalEvents())
									DebugLog(_T("[NatTraversal][OP_REASKCALLBACKTCP][Receiver] Sending ephemeral OP_REASKACK to %s:%u (hash=%s)"), (LPCTSTR)ipstr(destip), destport, (LPCTSTR)md4str(reqfilehash));
								Packet *response = new Packet(data_out, OP_EMULEPROT);
								response->opcode = OP_REASKACK;
								theStats.AddUpDataOverheadFileRequest(response->size);
								theApp.clientudp->SendPacket(response, destip, destport, false, NULL, false, 0);

								// Initialize uTP layer to expect incoming connection from the requester
								if (theApp.clientudp->GetUtpContext() != NULL) {
									// Helper lambda to add expected peer endpoint
									auto AddExpectedPeer = [&](const CAddress& peerIP, uint16 peerPort) {
										if (peerIP.IsNull() || peerPort == 0)
											return;
										sockaddr_storage saddr = {0};
										socklen_t slen = 0;
										if (peerIP.GetType() == CAddress::IPv4) {
											sockaddr_in* sa4 = (sockaddr_in*)&saddr;
											sa4->sin_family = AF_INET;
											sa4->sin_port = htons(peerPort);
											sa4->sin_addr.s_addr = htonl(peerIP.ToUInt32(false));
											slen = sizeof(sockaddr_in);
										} else if (peerIP.GetType() == CAddress::IPv6) {
											sockaddr_in6* sa6 = (sockaddr_in6*)&saddr;
											sa6->sin6_family = AF_INET6;
											sa6->sin6_port = htons(peerPort);
											memcpy(&sa6->sin6_addr, peerIP.Data(), 16);
											slen = sizeof(sockaddr_in6);
										}
										if (slen > 0) {
											theApp.clientudp->ExpectPeer((const sockaddr*)&saddr, slen);
											if (thePrefs.GetLogNatTraversalEvents())
												DebugLog(_T("[NatTraversal][OP_REASKCALLBACKTCP][Receiver] uTP layer ready, expecting peer from %s:%u"), (LPCTSTR)ipstr(peerIP), peerPort);
										}
									};
									
									// Add hint endpoint first (requester self-reported, preferred for NAT-T)
									if (!hintIP.IsNull() && hintPort != 0)
										AddExpectedPeer(hintIP, hintPort);
									
									// Add buddy-observed endpoint as fallback
									AddExpectedPeer(destip, destport);
								}
								
								// Receiver side: NAT-T expectation registered, uTP layer ready to accept incoming connection.
								// No need to create download client - requester will initiate the uTP connection.
								// When requester connects via uTP, normal upload queue processing will handle it.
								if (thePrefs.GetLogNatTraversalEvents())
									DebugLog(_T("[NatTraversal][OP_REASKCALLBACKTCP][Receiver] NAT-T setup complete, awaiting uTP connection from %s:%u"), (LPCTSTR)ipstr(destip), destport);
							}
					} else {
						DebugLogWarning(_T("OP_REASKCALLBACKTCP Packet received - multiple clients with the same IP but different UDP port found. Possible UDP Port mapping problem, enforcing TCP connection. IP: %s, Port: %u"), (LPCTSTR)ipstr(destip), destport);
					}
				}
			}
			break;
		case OP_AICHANSWER:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_AichAnswer", client, (size >= 16) ? packet : NULL);
			theStats.AddDownDataOverheadFileRequest(uRawSize);

			client->ProcessAICHAnswer(packet, size);
			break;
		case OP_AICHREQUEST:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_AichRequest", client, (size >= 16) ? packet : NULL);
			theStats.AddDownDataOverheadFileRequest(uRawSize);

			client->ProcessAICHRequest(packet, size);
			break;
		case OP_AICHFILEHASHANS:
			{
				// those should not be received normally, since we should only get those in MULTIPACKET
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_AichFileHashAns", client, (size >= 16) ? packet : NULL);
				theStats.AddDownDataOverheadFileRequest(uRawSize);

				CSafeMemFile data(packet, size);
				client->ProcessAICHFileHash(&data, NULL, NULL);
			}
			break;
		case OP_AICHFILEHASHREQ:
			{
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_AichFileHashReq", client, (size >= 16) ? packet : NULL);
				theStats.AddDownDataOverheadFileRequest(uRawSize);

				// those should not be received normally, since we should only get those in MULTIPACKET
				CSafeMemFile data(packet, size);
				uchar abyHash[MDX_DIGEST_SIZE];
				data.ReadHash16(abyHash);
				CKnownFile *pPartFile = theApp.sharedfiles->GetFileByID(abyHash);
				if (pPartFile == NULL) {
					client->CheckFailedFileIdReqs(abyHash);
					break;
				}
				if (client->IsSupportingAICH() && pPartFile->GetFileIdentifier().HasAICHHash()) {
					if (thePrefs.GetDebugClientTCPLevel() > 0)
						DebugSend("OP_AichFileHashAns", client, abyHash);
					CSafeMemFile data_out;
					data_out.WriteHash16(abyHash);
					pPartFile->GetFileIdentifier().GetAICHHash().Write(data_out);
					Packet *response = new Packet(data_out, OP_EMULEPROT, OP_AICHFILEHASHANS);
					theStats.AddUpDataOverheadFileRequest(response->size);
					SendPacket(response);
				}
			}
			break;
		case OP_REQUESTPARTS_I64:
			{
				// see also OP_REQUESTPARTS
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_RequestParts_I64", client, (size >= 16) ? packet : NULL);
				theStats.AddDownDataOverheadFileRequest(size);

				CSafeMemFile data(packet, size);
				uchar reqfilehash[MDX_DIGEST_SIZE];
				data.ReadHash16(reqfilehash);

				uint64 aOffset[3 * 2]; //3 starts, then 3 ends
				for (unsigned i = 0; i < 3 * 2; ++i)
					aOffset[i] = data.ReadUInt64();
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					for (unsigned i = 0; i < 3; ++i)
						Debug(_T("  Start[%u]=%I64u  End[%u]=%I64u  Size=%I64u\n"), i, aOffset[i], i, aOffset[i + 3], aOffset[i + 3] - aOffset[i]);

				if(client->GetUploadState()==US_BANNED) //just to be sure
				{
					theApp.uploadqueue->RemoveFromUploadQueue(client,_T("banned client detected during upload")); 
					client->SetUploadFileID(NULL); 
					AddProtectionLogLine(false,_T("Banned client was in upload: %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
				}

				if (thePrefs.IsDontAllowFileHotSwapping()) {
					// Close Backdoor v2 (idea Maella)
					//after seeing that many official clients swap the file just when they get an uploadslot
					//I decided to allow the upload if the new requested file
					//has same or higher priority
					//
					// Remark: There is a security leak that a leecher mod could exploit here.
					//         A client might send reqblock for another file than the one it 
					//         was granted to download. As long as the file ID in reqblock
					//         is the same in all reqblocks, it won't be rejected.  
					//         With this a client might be in a waiting queue with a high 
					//         priority but download block of a file set to a lower priority.
					CKnownFile* reqfileNr1 = theApp.sharedfiles->GetFileByID(reqfilehash);
					CKnownFile* reqfileNr2 = theApp.sharedfiles->GetFileByID(client->GetUploadFileID());
					if (reqfileNr1 == NULL) {
						//We don't know the requesting file, this can happen when we delete the file during upload
						//the prevent to run in a file exception when creating next block
						//send a cancel and remove client from queue
						Packet* packet = new Packet(OP_OUTOFPARTREQS, 0);
						theStats.AddUpDataOverheadFileRequest(packet->size);
						client->socket->SendPacket(packet, true, true);
						theApp.uploadqueue->RemoveFromUploadQueue(client, _T("Client requested unknown file"), true);
						client->SetUploadFileID(NULL);
						break;
					}

					if (reqfileNr2 != NULL && reqfileNr1->GetUpPriorityEx() < reqfileNr2->GetUpPriorityEx()) {
						if (thePrefs.GetLogUlDlEvents()) {
							AddProtectionLogLine(false, _T("File hot swapping disallowed [ProcessExtPacket]: (client=%s, expected=%s, asked=%s)"),
								(LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)EscPercent(reqfileNr2->GetFileName()), (LPCTSTR)EscPercent(reqfileNr1->GetFileName()));
						}
						theApp.uploadqueue->RemoveFromUploadQueue(client, _T("wrong file"), true);
						client->SetUploadFileID(reqfileNr1); //Xman Fix!  (needed for see onUploadqueue)
						client->SendOutOfPartReqsAndAddToWaitingQueue();
						client->SetWaitStartTime(); // Penality (soft punishement)
						break;
					}

					if (reqfileNr2 != NULL && reqfileNr2 != reqfileNr1) {
						// Safe logging: expected file may be NULL when it was deleted during upload
						const CString sExpectedName = reqfileNr2 ? reqfileNr2->GetFileName() : _T("<NULL>");
						if (thePrefs.GetLogUlDlEvents()) {
							AddProtectionLogLine(false, _T("File hot swapping allowed [ProcessExtPacket]: (client=%s, expected=%s, asked=%s)"),
								(LPCTSTR)EscPercent(client->GetUserName()), (LPCTSTR)EscPercent(sExpectedName), (LPCTSTR)EscPercent(reqfileNr1->GetFileName()));
						}
					}
				}

					for (unsigned i = 0; i < 3; ++i)
						if (aOffset[i] < aOffset[i + 3]) {
							Requested_Block_Struct *reqblock = new Requested_Block_Struct;
							reqblock->StartOffset = aOffset[i];
							reqblock->EndOffset = aOffset[i + 3];
							md4cpy(reqblock->FileID, reqfilehash);
							reqblock->transferred = 0;
							client->AddReqBlock(reqblock, false);
						} else if (thePrefs.GetVerbose() && (aOffset[i + 3] != 0 || aOffset[i] != 0))
							DebugLogWarning(_T("Client requests invalid %u. file block %I64u-%I64u (%I64d bytes): %s"), i, aOffset[i], aOffset[i + 3], aOffset[i + 3] - aOffset[i], (LPCTSTR)EscPercent(client->DbgGetClientInfo()));

					client->AddReqBlock(NULL, true);
					client->SetLastUpRequest();
				}
				break;
		case OP_COMPRESSEDPART:
		case OP_SENDINGPART_I64:
		case OP_COMPRESSEDPART_I64:
			{
				// see also OP_SENDINGPART
				if (thePrefs.GetDebugClientTCPLevel() > 1) {
					LPCSTR sOp;
					switch (opcode) {
					case OP_COMPRESSEDPART:
						sOp = "OP_CompressedPart";
						break;
					case OP_SENDINGPART_I64:
						sOp = "OP_SendingPart_I64";
						break;
					default: //OP_COMPRESSEDPART_I64
						sOp = "OP_CompressedPart_I64";
					}
					DebugRecv(sOp, client, (size >= 16) ? packet : NULL);
				}

				bool bCompress = (opcode != OP_SENDINGPART_I64);
				bool b64 = (opcode != OP_COMPRESSEDPART);
				theStats.AddDownDataOverheadFileRequest(16 + (b64 ? 8 : 4) + (bCompress ? 4 : 8));
				EDownloadState newDS = DS_NONE;
				const CPartFile *creqfile = client->GetRequestFile();
				if (creqfile) {
					if (!creqfile->IsStopped() && (creqfile->GetStatus() == PS_READY || creqfile->GetStatus() == PS_EMPTY)) {
						client->ProcessBlockPacket(packet, size, bCompress, b64);
						if (!creqfile->IsStopped()) {
							UINT uStatus = creqfile->GetStatus();
							if (uStatus == PS_ERROR)
								newDS = DS_ONQUEUE;
							else if (uStatus == PS_PAUSED || uStatus == PS_INSUFFICIENT)
								newDS = DS_NONE;
							else
								newDS = DS_CONNECTED; //any state but DS_NONE or DS_ONQUEUE
						}
					}
				}
					if (newDS != DS_CONNECTED && client) { //client could have been deleted while debugging
						client->SendCancelTransfer();
						client->SetDownloadState(newDS);
					}
				}
			break;
		case OP_CHATCAPTCHAREQ:
			{
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_CHATCAPTCHAREQ", client);
				theStats.AddDownDataOverheadOther(uRawSize);
				CSafeMemFile data(packet, size);
				client->ProcessCaptchaRequest(data);
			}
			break;
		case OP_CHATCAPTCHARES:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_CHATCAPTCHARES", client);
			theStats.AddDownDataOverheadOther(uRawSize);
			if (size < 1)
				throw GetResString(_T("ERR_BADSIZE"));
			client->ProcessCaptchaReqRes(packet[0]);
			break;
		case OP_FWCHECKUDPREQ: //*Support required for Kad version >= 6
			{
				// Kad related packet
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_FWCHECKUDPREQ", client);
				theStats.AddDownDataOverheadOther(uRawSize);
				CSafeMemFile data(packet, size);
				client->ProcessFirewallCheckUDPRequest(data);
			}
			break;
		case OP_KAD_FWTCPCHECK_ACK: //*Support required for Kad version >= 7
			// Kad related packet, replaces KADEMLIA_FIREWALLED_ACK_RES
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_KAD_FWTCPCHECK_ACK", client);
			if (theApp.clientlist->IsKadFirewallCheckIP(client->GetIP())) {
				if (Kademlia::CKademlia::IsRunning())
					Kademlia::CKademlia::GetPrefs()->IncFirewalled();
			} else
				DebugLogWarning(_T("Unrequested OP_KAD_FWTCPCHECK_ACK packet from client %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
			break;
		case OP_HASHSETANSWER2:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_HashSetAnswer2", client);
			theStats.AddDownDataOverheadFileRequest(size);
			client->ProcessHashSet(packet, size, true);
			break;
		case OP_HASHSETREQUEST2:
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugRecv("OP_HashSetReq2", client);
			theStats.AddDownDataOverheadFileRequest(size);
			client->SendHashsetPacket(packet, size, true);
			break;
		case OP_FILEINCSTATUS:
			{
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugRecv("OP_FileIncStatus", client, (size >= 16) ? packet : NULL);
				theStats.AddDownDataOverheadFileRequest(size);
				CSafeMemFile data(packet, size);
				client->ProcessFileIncStatus(&data, NULL, true);
				break;
			}
		default:
			theStats.AddDownDataOverheadOther(uRawSize);
			PacketToDebugLogLine(_T("eMule"), packet, size, opcode);
		}
	} catch (CString ex) {
		CAddress IP;
		SOCKADDR_IN6 sockAddr = { 0 };
		int nSockAddrLen = sizeof sockAddr;
		GetPeerName((LPSOCKADDR)&sockAddr, &nSockAddrLen);
		IP.FromSA((SOCKADDR*)&sockAddr, nSockAddrLen);

		if (thePrefs.GetVerbose()) {
			AddDebugLogLine(false, _T("[ProcessExtPacket]: Client with IP=%s caused a defined exception, disconnecting client: %s"), IP.ToStringC(), (LPCTSTR)EscPercent(ex.GetBuffer()));
			PacketToDebugLogLine(_T("eMule"), packet, size, opcode);
		}

		if (client)
			client->SetDownloadState(DS_ERROR, _T("[ProcessExtPacket]: A defined exception occured while processing eDonkey packet: ") + EscPercent(ex.GetBuffer()));

		if (thePrefs.IsBanWrongPackage()) {
			if (client) {
				if (theApp.shield->IsHarderPunishment(client->m_uPunishment, PR_WRONGPACKAGE))
					theApp.shield->SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_WRONG_PACKAGE")), PR_WRONGPACKAGE);
			} else if (!IP.IsNull()) {
				theApp.clientlist->AddBannedClient(IP.ToStringC());
				if (thePrefs.GetVerbose())
					AddDebugLogLine(false, _T("[ProcessExtPacket]: IP address has been banned: %s"), (LPCTSTR)IP.ToStringC());
			}
		}

		Disconnect(_T("[ProcessExtPacket]: A defined exception occured while processing eDonkey packet: ") + ex);
		return false;
	} catch (CException* ex) {
		CAddress IP;
		SOCKADDR_IN6 sockAddr = { 0 };
		int nSockAddrLen = sizeof sockAddr;
		GetPeerName((LPSOCKADDR)&sockAddr, &nSockAddrLen);
		IP.FromSA((SOCKADDR*)&sockAddr, nSockAddrLen);

		if (thePrefs.GetVerbose()) {
			AddDebugLogLine(false, _T("[ProcessExtPacket]: Client with IP=%s caused an exception, disconnecting client: %s"), IP.ToStringC(), (LPCTSTR)EscPercent(CString(CExceptionStr(*ex))));
			PacketToDebugLogLine(_T("eMule"), packet, size, opcode);
		}

		ex->Delete();

		if (client)
			client->SetDownloadState(DS_ERROR, _T("[ProcessExtPacket]: An exception occured while processing eDonkey packet: ") + EscPercent(CString(CExceptionStr(*ex))));

		if (thePrefs.IsBanWrongPackage()) {
			if (client) {
				if (theApp.shield->IsHarderPunishment(client->m_uPunishment, PR_WRONGPACKAGE))
					theApp.shield->SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_WRONG_PACKAGE")), PR_WRONGPACKAGE);
			} else if (!IP.IsNull()) {
				theApp.clientlist->AddBannedClient(IP.ToStringC());
				if (thePrefs.GetVerbose())
					AddDebugLogLine(false, _T("[ProcessExtPacket]: IP address has been banned: %s"), (LPCTSTR)IP.ToStringC());
			}
		}
		
		Disconnect(_T("[ProcessExtPacket]: An exception occured while processing eDonkey packet: ") + EscPercent(CString(CExceptionStr(*ex))));
		return false;
	} catch (...) {
		CAddress IP;
		SOCKADDR_IN6 sockAddr = { 0 };
		int nSockAddrLen = sizeof sockAddr;
		GetPeerName((LPSOCKADDR)&sockAddr, &nSockAddrLen);
		IP.FromSA((SOCKADDR*)&sockAddr, nSockAddrLen);

		if (thePrefs.GetVerbose()) {
			AddDebugLogLine(false, _T("[ProcessExtPacket]: Client with IP=%s caused an unknown exception, disconnecting client."), IP.ToStringC());
			PacketToDebugLogLine(_T("eMule"), packet, size, opcode);
		}

		if (client)
			client->SetDownloadState(DS_ERROR, _T("[ProcessExtPacket]: An unknown exception occured while processing eDonkey packet."));

		if (thePrefs.IsBanWrongPackage()) {
			if (client) {
				if (theApp.shield->IsHarderPunishment(client->m_uPunishment, PR_WRONGPACKAGE))
					theApp.shield->SetPunishment(client, GetResString(_T("PUNISHMENT_REASON_WRONG_PACKAGE")), PR_WRONGPACKAGE);
			} else if (!IP.IsNull()) {
				theApp.clientlist->AddBannedClient(IP.ToStringC());
				if (thePrefs.GetVerbose())
					AddDebugLogLine(false, _T("[ProcessExtPacket]: IP address has been banned: %s"), (LPCTSTR)IP.ToStringC());
			}
		}
		
		Disconnect(_T("[ProcessExtPacket]: An unknown exception occured while processing eDonkey packet."));
		return false;
	}

	return true;
}

void CClientReqSocket::PacketToDebugLogLine(LPCTSTR protocol, const uchar *packet, uint32 size, UINT opcode)
{
	if (thePrefs.GetVerbose()) {
		CString buffer;
		buffer.Format(_T("Unknown %s protocol Opcode: 0x%02x, Size=%u, Data=["), protocol, opcode, size);
		UINT i;
		for (i = 0; i < size && i < 50; ++i)
			buffer.AppendFormat(*(&_T(" %02x")[static_cast<int>(i > 0)]), packet[i]);

		buffer += (i < size) ? _T("... ]") : _T(" ]");
		DbgAppendClientInfo(buffer);
		DebugLogWarning(_T("%s"), (LPCTSTR)EscPercent(buffer));
	}
}

CString CClientReqSocket::DbgGetClientInfo()
{
	CString str;
	CAddress IP;
	SOCKADDR_IN6 sockAddr = { 0 };
	int nSockAddrLen = sizeof sockAddr;
	GetPeerName((LPSOCKADDR)&sockAddr, &nSockAddrLen);
	IP.FromSA((SOCKADDR*)&sockAddr, nSockAddrLen);
	if (!IP.IsNull() && (client == NULL || IP != client->GetIP()))
		str.Format(_T("IP=%s"), ipstr(IP));
	if (client)
		str.AppendFormat(&_T("; Client=%s")[str.IsEmpty() ? 2 : 0], (LPCTSTR)client->DbgGetClientInfo());
	return str;
}

void CClientReqSocket::DbgAppendClientInfo(CString &str)
{
	CString strClientInfo(DbgGetClientInfo());
	if (!strClientInfo.IsEmpty())
		str.AppendFormat(&_T("; %s")[str.IsEmpty() ? 2 : 0], (LPCTSTR)strClientInfo);
}

void CClientReqSocket::OnConnect(int nErrorCode)
{
	SetConState(SS_Complete);
	CEMSocket::OnConnect(nErrorCode);
	if (nErrorCode) {
		if (thePrefs.GetVerbose()) {
			const CString &strTCPError(GetFullErrorMessage(nErrorCode));
			if ((nErrorCode != WSAECONNREFUSED && nErrorCode != WSAETIMEDOUT) || !GetLastProxyError().IsEmpty())
			{
				//Xman:Don't show this "error" because it's too normal and I only saw:
				//WSAENETUNREACH and WSAEHOSTUNREACH
				//In such a case don't give the clients more connection-retrys
				if (client)
					client->m_cFailed = 5;
			}
			Disconnect(strTCPError);
		} else
			Disconnect(EMPTY);
	} else {
		//This socket may have been delayed by SP2 protection, lets make sure it doesn't time out instantly.
		ResetTimeOutTimer();
		
		// For uTP connections, trigger full connection establishment when socket connects
		// (for TCP, ConnectionEstablished is called from OnReceive after first packet)
		if (client && HaveUtpLayer()) {
			CEMSocket::SetConState(EMS_CONNECTED);
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] OnConnect: uTP connection established, triggering ConnectionEstablished, %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
			client->ConnectionEstablished();
		}
	}
}

void CClientReqSocket::OnSend(int nErrorCode)
{
	ResetTimeOutTimer();
	CEMSocket::OnSend(nErrorCode);
}

void CClientReqSocket::OnError(int nErrorCode)
{
	CString strTCPError;
	const bool bUtpWrongHeader = (nErrorCode == ERR_WRONGHEADER && HaveUtpLayer() && client != NULL);
	if (thePrefs.GetVerbose()) {
		if (nErrorCode == ERR_WRONGHEADER)
			strTCPError = _T("Error: Wrong header");
		else if (nErrorCode == ERR_TOOBIG)
			strTCPError = _T("Error: Too much data sent");
		else if (nErrorCode == ERR_ENCRYPTION)
			strTCPError = _T("Error: Encryption layer error");
		else if (nErrorCode == ERR_ENCRYPTION_NOTALLOWED)
			strTCPError = _T("Error: Unencrypted Connection when Encryption was required");
		else
			strTCPError = GetErrorMessage(nErrorCode);
		DebugLogWarning(_T("Client TCP socket: %s; %s"), (LPCTSTR)EscPercent(strTCPError), (LPCTSTR)EscPercent(DbgGetClientInfo()));
	}
	// Xman: In such a case don't give the clients more connection-retrys.
	// For active uTP links, wrong-header can be a transient TCP side effect. Keep retry path alive.
	if (client) {
		if (bUtpWrongHeader) {
			if (thePrefs.GetLogNatTraversalEvents()) {
				DebugLogWarning(_T("[NatTraversal][uTP] Treating ERR_WRONGHEADER as transient; preserving reconnect path, %s"),
					(LPCTSTR)EscPercent(client->DbgGetClientInfo()));
			}
			const EDownloadState eDlState = client->GetDownloadState();
			if (eDlState == DS_DOWNLOADING || eDlState == DS_ONQUEUE || eDlState == DS_CONNECTED || eDlState == DS_REQHASHSET) {
				client->SetRemoteQueueRank(0);
				client->SetAskedCountDown(0);
				client->SetDownloadState(DS_CONNECTING);
			}
		} else {
			client->m_cFailed = 5;
			theApp.shield->CheckTCPErrorFlooder(client);
		}
	}

	Disconnect(strTCPError);
}

bool CClientReqSocket::PacketReceived(Packet *packet)
{
	CString *psErr;
	bool bDelClient;
	const uint8 opcode = packet->opcode;
	const UINT uRawSize = packet->size;
	try {
		try {
			switch (packet->prot) {
			case OP_EDONKEYPROT:
				if (!client && opcode != OP_HELLO) {
					theStats.AddDownDataOverheadOther(packet->size);
					throw GetResString(_T("ERR_NOHELLO"));
				}

				return ProcessPacket((BYTE*)packet->pBuffer, uRawSize, opcode);
			case OP_PACKEDPROT:
				if (!packet->UnPackPacket()) {
					if (thePrefs.GetVerbose())
						DebugLogError(_T("Failed to decompress client TCP packet; %s; %s"), (LPCTSTR)EscPercent(DbgGetClientTCPPacket(packet->prot, packet->opcode, packet->size)), (LPCTSTR)EscPercent(DbgGetClientInfo()));
					break;
				}
			case OP_EMULEPROT:
				if (opcode != OP_PORTTEST) {
					if (!client) {
						theStats.AddDownDataOverheadOther(uRawSize);
						throw GetResString(_T("ERR_UNKNOWNCLIENTACTION"));
					}
					if (thePrefs.m_iDbgHeap >= 2)
						ASSERT_VALID(client);
				}

				return ProcessExtPacket((BYTE*)packet->pBuffer, packet->size, packet->opcode, uRawSize);
			default:
				theStats.AddDownDataOverheadOther(uRawSize);
				if (thePrefs.GetVerbose())
					DebugLogWarning(_T("Received unknown client TCP packet; %s; %s"), (LPCTSTR)EscPercent(DbgGetClientTCPPacket(packet->prot, packet->opcode, packet->size)), (LPCTSTR)EscPercent(DbgGetClientInfo()));

				if (client)
					client->SetDownloadState(DS_ERROR, _T("Unknown protocol"));
				Disconnect(_T("Unknown protocol"));
			}
		} catch (CFileException *ex) {
			ex->Delete();
			throw GetResString(_T("ERR_INVALIDPACKET"));
		} catch (CMemoryException *ex) {
			ex->Delete();
			throwCStr(_T("Memory exception"));
		} catch (const CString& error) {
			throwCStr(error);
		} catch (...) { //trying to catch "Unspecified error"
			throwCStr(_T("Unhandled exception"));
		}
		return true;
	} catch (CClientException *ex) { // similar to 'CString&' exception but client deletion is optional
		bDelClient = ex->m_bDelete;
		psErr = new CString(ex->m_strMsg);
		ex->Delete();
	} catch (const CString &ex) {
		bDelClient = true;
		psErr = new CString(ex);
	}
	//Error handling
	bool bIsDonkey = (packet->prot == OP_EDONKEYPROT);
	LPCTSTR sProtocol = bIsDonkey ? _T("eDonkey") : _T("eMule");
	if (thePrefs.GetVerbose())
		DebugLogWarning(_T("Error: '%s' while processing %s packet: opcode=%s  size=%u; %s")
			//, (LPCTSTR)psErr 
			, (psErr == NULL ? (LPCTSTR)L"" : (LPCTSTR)EscPercent(*psErr))
			, sProtocol
			, (LPCTSTR)(bIsDonkey ? (LPCTSTR)EscPercent(DbgGetDonkeyClientTCPOpcode(opcode)) : (LPCTSTR)EscPercent(DbgGetMuleClientTCPOpcode(opcode)))
			, uRawSize
			, (LPCTSTR)EscPercent(DbgGetClientInfo()));

	CString sErr2;
	sErr2.Format(_T("Error while processing %s packet:  %s"), (LPCTSTR)sProtocol, (LPCTSTR)*psErr);
	if (bDelClient && client)
		client->SetDownloadState(DS_ERROR, sErr2);
	Disconnect(sErr2);
	delete psErr;
	return false;
}

void CClientReqSocket::OnReceive(int nErrorCode)
{
	ResetTimeOutTimer();
	CEMSocket::OnReceive(nErrorCode);
}

bool CClientReqSocket::Create()
{
	theApp.listensocket->AddConnection();
	return (CAsyncSocketEx::Create(0, SOCK_STREAM, FD_WRITE | FD_READ | FD_CLOSE | FD_CONNECT, thePrefs.GetBindAddr(), AF_INET6));
}

SocketSentBytes CClientReqSocket::SendControlData(uint32 maxNumberOfBytesToSend, uint32 overchargeMaxBytesToSend)
{
	SocketSentBytes returnStatus = CEMSocket::SendControlData(maxNumberOfBytesToSend, overchargeMaxBytesToSend);
	if (returnStatus.success && (returnStatus.sentBytesControlPackets > 0 || returnStatus.sentBytesStandardPackets > 0))
		ResetTimeOutTimer();
	return returnStatus;
}

SocketSentBytes CClientReqSocket::SendFileAndControlData(uint32 maxNumberOfBytesToSend, uint32 overchargeMaxBytesToSend)
{
	SocketSentBytes returnStatus = CEMSocket::SendFileAndControlData(maxNumberOfBytesToSend, overchargeMaxBytesToSend);
	if (returnStatus.success && (returnStatus.sentBytesControlPackets > 0 || returnStatus.sentBytesStandardPackets > 0))
		ResetTimeOutTimer();
	return returnStatus;
}

void CClientReqSocket::SendPacket(Packet *packet, bool controlpacket, uint32 actualPayloadSize, bool bForceImmediateSend)
{
	ResetTimeOutTimer();
	CEMSocket::SendPacket(packet, controlpacket, actualPayloadSize, bForceImmediateSend);
}

bool CListenSocket::SendPortTestReply(char result, bool disconnect)
{
	for (POSITION pos = socket_list.GetHeadPosition(); pos != NULL;) {
		CClientReqSocket *cur_sock = socket_list.GetNext(pos);
		if (cur_sock->m_bPortTestCon) {
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugSend("OP_PortTest", cur_sock->client);
			Packet *replypacket = new Packet(OP_PORTTEST, 1);
			replypacket->pBuffer[0] = result;
			theStats.AddUpDataOverheadOther(replypacket->size);
			cur_sock->SendPacket(replypacket);
			if (disconnect)
				cur_sock->m_bPortTestCon = false;
			return true;
		}
	}
	return false;
}

CListenSocket::CListenSocket()
	: bListening()
	, m_OpenSocketsInterval()
	, maxconnectionreached()
	, m_ConnectionStates()
	, m_nPendingConnections()
	, peakconnections()
	, totalconnectionchecks()
	, averageconnections()
	, activeconnections()
	, m_port()
	, m_nHalfOpen()
	, m_nComp()
{
}

CListenSocket::~CListenSocket()
{
	CListenSocket::Close();
	KillAllSockets();
}

bool CListenSocket::Rebind()
{
	if (thePrefs.GetPort() == m_port)
		return false;

	Close();
	KillAllSockets();

	return StartListening();
}

bool CListenSocket::StartListening()
{
	bListening = true;

	// Creating the socket with SO_REUSEADDR may solve LowID issues if emule was restarted
	// quickly or started after a crash, but(!) it will also create another problem. If the
	// socket is already used by some other application (e.g. a 2nd emule), we though bind
	// to that socket leading to the situation that 2 applications are listening on the same
	// port!
	if (!Create(thePrefs.GetPort(), SOCK_STREAM, FD_ACCEPT, thePrefs.GetBindAddr(), AF_INET6))
		return false;

	// Rejecting a connection with conditional WSAAccept and not using SO_CONDITIONAL_ACCEPT
	// -------------------------------------------------------------------------------------
	// recv: SYN
	// send: SYN ACK (!)
	// recv: ACK
	// send: ACK RST
	// recv: PSH ACK + OP_HELLO packet
	// send: RST
	// --- 455 total bytes (depending on OP_HELLO packet)
	// In case SO_CONDITIONAL_ACCEPT is not used, the TCP/IP stack establishes the connection
	// before WSAAccept has a chance to reject it. That's why the remote peer starts to send
	// its first data packet.
	// ---
	// Not using SO_CONDITIONAL_ACCEPT gives us 6 TCP packets and the OP_HELLO data. We
	// have to lookup the IP only 1 time. This is still way less traffic than rejecting the
	// connection by closing it after the 'Accept'.

	// Rejecting a connection with conditional WSAAccept and using SO_CONDITIONAL_ACCEPT
	// ---------------------------------------------------------------------------------
	// recv: SYN
	// send: ACK RST
	// recv: SYN
	// send: ACK RST
	// recv: SYN
	// send: ACK RST
	// --- 348 total bytes
	// The TCP/IP stack tries to establish the connection 3 times until it gives up.
	// Furthermore the remote peer experiences a total timeout of ~ 1 minute which is
	// supposed to be the default TCP/IP connection timeout (as noted in MSDN).
	// ---
	// Although we get a total of 6 TCP packets in case of using SO_CONDITIONAL_ACCEPT,
	// it's still less than not using SO_CONDITIONAL_ACCEPT. But, we have to lookup
	// the IP 3 times instead of 1 time.


	if (!Listen())
		return false;

	m_port = thePrefs.GetPort();
	return true;
}

void CListenSocket::ReStartListening()
{
	bListening = true;

	ASSERT(m_nPendingConnections >= 0);
	if (m_nPendingConnections > 0) {
		--m_nPendingConnections;
		OnAccept(0);
	}
}

void CListenSocket::StopListening()
{
	bListening = false;
	++maxconnectionreached;
}

static int s_iAcceptConnectionCondRejected;

//int CALLBACK AcceptConnectionCond(LPWSABUF lpCallerId, LPWSABUF /*lpCallerData*/, LPQOS /*lpSQOS*/, LPQOS /*lpGQOS*/,
//LPWSABUF /*lpCalleeId*/, LPWSABUF /*lpCalleeData*/, GROUP FAR* /*g*/, DWORD_PTR /*dwCallbackData*/) noexcept
int CALLBACK AcceptConnectionCond(LPWSABUF lpCallerId, LPWSABUF /*lpCallerData*/, LPQOS /*lpSQOS*/, LPQOS /*lpGQOS*/,
	LPWSABUF /*lpCalleeId*/, LPWSABUF /*lpCalleeData*/, GROUP FAR* /*g*/, DWORD_PTR /*dwCallbackData*/) noexcept
{
	if (lpCallerId && lpCallerId->buf) {
		CAddress IP;
		if (lpCallerId->len >= sizeof(SOCKADDR_IN6)) { //IPv6
			LPSOCKADDR_IN6 pSockAddr6 = (LPSOCKADDR_IN6)lpCallerId->buf;
			pSockAddr6->sin6_family = AF_INET6;
			IP.FromSA((SOCKADDR*)pSockAddr6, sizeof SOCKADDR_IN6);
		} else if (lpCallerId->len >= sizeof SOCKADDR_IN) { //IPv4
			LPSOCKADDR_IN pSockAddr = (LPSOCKADDR_IN)lpCallerId->buf;
			pSockAddr->sin_family = AF_INET;
			IP.FromSA((SOCKADDR*)pSockAddr, sizeof SOCKADDR_IN);
		} else { // Unexpected address length
			if (thePrefs.GetVerbose())
				DebugLogError(_T("[AcceptConnectionCond]: Unexpected lpCallerId length for client TCP socket"));
			return CF_ACCEPT;
		}

		if (theApp.ipfilter->IsFiltered(IP)) {
			if (thePrefs.GetLogFilteredIPs())
				AddDebugLogLine(false, _T("[AcceptConnectionCond]: Rejecting connection attempt (IP=%s) - IP filter (%s)"), (LPCTSTR)ipstr(IP), (LPCTSTR)EscPercent(theApp.ipfilter->GetLastHit()));
			s_iAcceptConnectionCondRejected = 1;
			return CF_REJECT;
		}

		if (theApp.clientlist->IsBannedClient(IP.ToStringC())) {
			if (thePrefs.GetLogBannedClients()) {
				CUpDownClient* pClient = theApp.clientlist->FindClientByIP(IP);
				AddDebugLogLine(false, _T("[AcceptConnectionCond]: Rejecting connection attempt of banned client %s %s"), (LPCTSTR)ipstr(IP), pClient ? (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()) : (LPCTSTR)EMPTY);
			}
			s_iAcceptConnectionCondRejected = 2;
			return CF_REJECT;
		}
	} else if (thePrefs.GetVerbose())
		DebugLogError(_T("[AcceptConnectionCond]: Unexpected lpCallerId length for client TCP socket"));

	return CF_ACCEPT;
}

void CListenSocket::OnAccept(int nErrorCode)
{
	if (!nErrorCode) {
		if (++m_nPendingConnections < 1) {
			ASSERT(0);
			m_nPendingConnections = 1;
		}

		if (TooManySockets(true) && !theApp.serverconnect->IsConnecting()) {
			StopListening();
			return;
		}
		if (!bListening)
			ReStartListening(); //If the client is still at maxconnections, this will allow it to go above it. But if you don't, you will get a low ID on all servers.

		uint32 nFataErrors = 0;
		while (m_nPendingConnections > 0) {
			--m_nPendingConnections;

			CClientReqSocket *newclient;
			SOCKADDR_IN6 SockAddr = { 0 };
			int iSockAddrLen = sizeof SockAddr;
			if (thePrefs.GetConditionalTCPAccept() && !thePrefs.GetProxySettings().bUseProxy) {
				s_iAcceptConnectionCondRejected = 0;
				SOCKET sNew = WSAAccept(m_SocketData.hSocket, (LPSOCKADDR)&SockAddr, &iSockAddrLen, AcceptConnectionCond, 0);
				if (sNew == INVALID_SOCKET) {
					DWORD nError = CAsyncSocket::GetLastError();
					if (nError == WSAEWOULDBLOCK) {
						DebugLogError(LOG_STATUSBAR, _T("%hs: Backlog counter says %u connections waiting, Accept() says WSAEWOULDBLOCK - setting counter to zero!"), __FUNCTION__, m_nPendingConnections);
						m_nPendingConnections = 0;
						break;
					}

					if (nError != WSAECONNREFUSED || s_iAcceptConnectionCondRejected == 0) {
						DebugLogError(LOG_STATUSBAR, _T("%hs: Backlog counter says %u connections waiting, Accept() says %s - setting counter to zero!"), __FUNCTION__, m_nPendingConnections, (LPCTSTR)EscPercent(GetErrorMessage(nError, 1)));
						if (++nFataErrors > 10) {
							// the question is what todo on an error. We can't just ignore it because then the backlog will fill up
							// and lock everything. We cannot also just endlessly try to repeat it because this will lock up eMule
							// this should basically never happen anyway
							// however if we are in such position, try to reinitialize the socket.
							DebugLogError(LOG_STATUSBAR, _T("%hs: Accept() Error Loop, recreating socket"), __FUNCTION__);
							Close();
							StartListening();
							m_nPendingConnections = 0;
							break;
						}
					} else if (s_iAcceptConnectionCondRejected == 1)
						++theStats.filteredclients;

					continue;
				}
				newclient = new CClientReqSocket;
				VERIFY(newclient->InitAsyncSocketExInstance());
				newclient->m_SocketData.hSocket = sNew;
				newclient->AttachHandle();

				AddConnection();
			} else {
				newclient = new CClientReqSocket;
				if (!Accept(*newclient, (LPSOCKADDR)&SockAddr, &iSockAddrLen)) {
					newclient->Safe_Delete();
					DWORD nError = CAsyncSocket::GetLastError();
					if (nError == WSAEWOULDBLOCK) {
						DebugLogError(LOG_STATUSBAR, _T("%hs: Backlog counter says %u connections waiting, Accept() says WSAEWOULDBLOCK - setting counter to zero!"), __FUNCTION__, m_nPendingConnections);
						m_nPendingConnections = 0;
						break;
					}
					DebugLogError(LOG_STATUSBAR, _T("%hs: Backlog counter says %u connections waiting, Accept() says %s - setting counter to zero!"), __FUNCTION__, m_nPendingConnections, (LPCTSTR)EscPercent(GetErrorMessage(nError, 1)));
					if (++nFataErrors > 10) {
						// the question is what to do on an error. We can't just ignore it because then the backlog will fill up
						// and lock everything. We cannot also just endlessly try to repeat it because this will lock up eMule
						// this should basically never happen anyway
						// however if we are in such a position, try to reinitialize the socket.
						DebugLogError(LOG_STATUSBAR, _T("%hs: Accept() Error Loop, recreating socket"), __FUNCTION__);
						Close();
						StartListening();
						m_nPendingConnections = 0;
						break;
					}
					continue;
				}

				AddConnection();

				if (memcmp(&SockAddr.sin6_addr, &in6addr_any, sizeof in6addr_any) == 0) { // for safety..
					iSockAddrLen = (int)sizeof SockAddr;
					newclient->GetPeerName((LPSOCKADDR)&SockAddr, &iSockAddrLen);
					CAddress address;
					address.FromSA((SOCKADDR*)&SockAddr, iSockAddrLen);
					DebugLogWarning(_T("SockAddr.sin_addr.s_addr == 0;  GetPeerName returned %s"), (LPCTSTR)ipstr(address));
				}

				CAddress IP;
				IP.FromSA((SOCKADDR*)&SockAddr, iSockAddrLen);
				ASSERT(!IP.IsNull());

				if (theApp.ipfilter->IsFiltered(IP)) {
					if (thePrefs.GetLogFilteredIPs())
						AddDebugLogLine(false, _T("Rejecting connection attempt (IP=%s) - IP filter (%s)"), (LPCTSTR)IP.ToStringC(), (LPCTSTR)EscPercent(theApp.ipfilter->GetLastHit()));
					newclient->Safe_Delete();
					++theStats.filteredclients;
					continue;
				}

				if (theApp.clientlist->IsBannedClient(IP.ToStringC())) {
					if (thePrefs.GetLogBannedClients()) {
						CUpDownClient* pClient = theApp.clientlist->FindClientByIP(IP);
						AddDebugLogLine(false, _T("Rejecting connection attempt of banned client %s %s"), (LPCTSTR)IP.ToStringC(), pClient ? (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()) : (LPCTSTR)EMPTY);
					}
					newclient->Safe_Delete();
					continue;
				}
			}
			newclient->AsyncSelect(FD_WRITE | FD_READ | FD_CLOSE);
		}

		ASSERT(m_nPendingConnections >= 0);
	}
}

void CListenSocket::Process()
{
	m_OpenSocketsInterval = 0;
	for (POSITION pos = socket_list.GetHeadPosition(); pos != NULL;) {
		CClientReqSocket *cur_sock = socket_list.GetNext(pos);
		if (cur_sock->deletethis) {
			if (cur_sock->m_SocketData.hSocket != INVALID_SOCKET || cur_sock->HaveUtpLayer(true))
				cur_sock->Close();			// calls 'closesocket'
			else
				cur_sock->Delete_Timed();	// may delete 'cur_sock'
		} else
			cur_sock->CheckTimeOut();		// may call 'shutdown'
	}

	if ((GetOpenSockets() + 5 < thePrefs.GetMaxConnections() || theApp.serverconnect->IsConnecting()) && !bListening)
		ReStartListening();
}

void CListenSocket::RecalculateStats()
{
	memset(m_ConnectionStates, 0, sizeof m_ConnectionStates);
	for (POSITION pos = socket_list.GetHeadPosition(); pos != NULL;)
		switch (socket_list.GetNext(pos)->GetConState()) {
		case EMS_DISCONNECTED:
			++m_ConnectionStates[0];
			break;
		case EMS_NOTCONNECTED:
			++m_ConnectionStates[1];
			break;
		case EMS_CONNECTED:
			++m_ConnectionStates[2];
		}
}

void CListenSocket::AddSocket(CClientReqSocket *toadd)
{
	socket_list.AddTail(toadd);
}

void CListenSocket::RemoveSocket(CClientReqSocket *todel)
{
	POSITION pos = socket_list.Find(todel);
	if (pos != NULL)
		socket_list.RemoveAt(pos);
}

void CListenSocket::KillAllSockets()
{
	while (!socket_list.IsEmpty()) {
		const CClientReqSocket *cur_socket = socket_list.GetHead();
		if (cur_socket->client)
			delete cur_socket->client;
		else
			delete cur_socket;
	}
}

void CListenSocket::AddConnection()
{
	++m_OpenSocketsInterval;
}

bool CListenSocket::TooManySockets(bool bIgnoreInterval)
{
	return GetOpenSockets() > thePrefs.GetMaxConnections()
		|| (m_OpenSocketsInterval > thePrefs.GetMaxConperFive() * GetMaxConperFiveModifier() && !bIgnoreInterval)
		|| (m_nHalfOpen >= thePrefs.GetMaxHalfConnections() && !bIgnoreInterval);
}

bool CListenSocket::IsValidSocket(CClientReqSocket *totest)
{
	return socket_list.Find(totest) != NULL;
}

#ifdef _DEBUG
void CListenSocket::Debug_ClientDeleted(CUpDownClient *deleted)
{
	for (POSITION pos = socket_list.GetHeadPosition(); pos != NULL;) {
		CClientReqSocket *cur_sock = socket_list.GetNext(pos);
		if (!cur_sock)
			AfxDebugBreak();
		if (thePrefs.m_iDbgHeap >= 2)
			ASSERT_VALID(cur_sock);
		if (cur_sock->client == deleted)
			AfxDebugBreak();
	}
}
#endif

void CListenSocket::UpdateConnectionsStatus()
{
	activeconnections = GetOpenSockets();

	// Update statistics for 'peak connections'
	if (peakconnections < activeconnections)
		peakconnections = activeconnections;
	if (peakconnections > thePrefs.GetConnPeakConnections())
		thePrefs.SetConnPeakConnections(peakconnections);

	if (theApp.IsConnected()) {
		if (++totalconnectionchecks == 0)
			 // wrap around occurred, avoid division by zero
			totalconnectionchecks = 100;

		// Get a weight for the 'avg. connections' value. The longer we run the higher
		// gets the weight (the percent of 'avg. connections' we use).
		float fPercent = (totalconnectionchecks - 1) / (float)totalconnectionchecks;
		if (fPercent > 0.99f)
			fPercent = 0.99f;

		// The longer we run the more we use the 'avg. connections' value and the less we
		// use the 'active connections' value. However, if we are running quite some time
		// without any connections (except the server connection) we will eventually create
		// a floating point underflow exception.
		averageconnections = averageconnections * fPercent + activeconnections * (1.0f - fPercent);
		if (averageconnections < 0.001f)
			averageconnections = 0.001f;	// avoid floating point underflow
	}
}

float CListenSocket::GetMaxConperFiveModifier()
{
	float SpikeSize = max(1.0f, GetOpenSockets() - averageconnections);
	float SpikeTolerance = 25.0f * thePrefs.GetMaxConperFive() / 10.0f;

	return (SpikeSize > SpikeTolerance) ? 0.0f : 1.0f - SpikeSize / SpikeTolerance;
}
