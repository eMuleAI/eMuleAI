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
#ifdef _DEBUG
#include "DebugHelpers.h"
#endif
#include "emule.h"
#include "UpDownClient.h"
#include "FriendList.h"
#include "Clientlist.h"
#include "PartFile.h"
#include "ListenSocket.h"
#include "Packets.h"
#include "Opcodes.h"
#include "SafeFile.h"
#include "Preferences.h"
#include <algorithm>
#include "Server.h"
#include "ClientCredits.h"
#include "IPFilter.h"
#include "Friend.h"
#include "Statistics.h"
#include "ServerConnect.h"
#include "DownloadQueue.h"
#include "UploadQueue.h"
#include "SearchFile.h"
#include "SearchList.h"
#include "SharedFileList.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "Kademlia/Kademlia/Search.h"
#include "Kademlia/Kademlia/SearchManager.h"
#include "Kademlia/Kademlia/UDPFirewallTester.h"
#include "Kademlia/routing/RoutingZone.h"
#include "Kademlia/Utils/UInt128.h"
#include "Kademlia/Net/KademliaUDPListener.h"
#include "Kademlia/Kademlia/Prefs.h"
#include "emuledlg.h"
#include "ServerWnd.h"
#include "TransferDlg.h"
#include "ChatWnd.h"
#include "PreviewDlg.h"
#include "Exceptions.h"
#include "ClientUDPSocket.h"
#include "shahashset.h"
#include "MD4.h"
#include "Log.h"
#include "CaptchaGenerator.h"
#include <atlimage.h>
#include "zlib/zlib.h"
#include "eMuleAI/Shield.h"
#include "eMuleAI/AntiNick.h"
#include <regex>
#include "Log.h"
#include "InputBox.h" 
#include "SearchDlg.h"
#include "MuleStatusBarCtrl.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define URLINDICATOR	_T("http:|www.|.de |.net |.com |.org |.to |.tk |.cc |.fr |ftp:|ed2k:|https:|ftp.|.info|.biz|.uk|.eu|.es|.tv|.cn|.tw|.ws|.nu|.jp")

IMPLEMENT_DYNAMIC(CClientException, CException)
IMPLEMENT_DYNAMIC(CUpDownClient, CObject)
CString CUpDownClient::m_strAntiUserNameThiefNick;

static EClientSoftware DetectStoredClientSoftware(const CString& clientSoftware)
{
	CString softwareLower = clientSoftware;
	softwareLower.MakeLower();

	// Archived clients already store a normalized software label.
	if (softwareLower.Find(_T("old emule")) == 0)
		return SO_OLDEMULE;
	if (softwareLower.Find(_T("edonkeyhybrid")) == 0)
		return SO_EDONKEYHYBRID;
	if (softwareLower.Find(_T("edonkey")) == 0)
		return SO_EDONKEY;
	if (softwareLower.Find(_T("amule")) == 0)
		return SO_AMULE;
	if (softwareLower.Find(_T("mldonkey")) == 0)
		return SO_MLDONKEY;
	if (softwareLower.Find(_T("shareaza")) == 0)
		return SO_SHAREAZA;
	if (softwareLower.Find(_T("lphant")) == 0)
		return SO_LPHANT;
	if (softwareLower.Find(_T("emule plus")) == 0)
		return SO_EMULEPLUS;
	if (softwareLower.Find(_T("hydranode")) == 0)
		return SO_HYDRANODE;
	if (softwareLower.Find(_T("hydra")) == 0)
		return SO_HYDRA;
	if (softwareLower.Find(_T("trustyfiles")) == 0)
		return SO_TRUSTYFILES;
	if (softwareLower.Find(_T("easymule")) == 0)
		return SO_EASYMULE2;
	if (softwareLower.Find(_T("neoloader")) == 0)
		return SO_NEOLOADER;
	if (softwareLower.Find(_T("kmule")) == 0)
		return SO_KMULE;
	if (softwareLower.Find(_T("cdonkey")) == 0)
		return SO_CDONKEY;
	if (softwareLower.Find(_T("xmule")) == 0 || softwareLower.Find(_T("emule compat")) == 0)
		return SO_XMULE;
	if (softwareLower.Find(_T("emule")) == 0)
		return SO_EMULE;
	return SO_UNKNOWN;
}

CUpDownClient::CUpDownClient(CClientReqSocket *sender)
	: socket(sender)
	, m_reqfile()
{
	Init();
}

CUpDownClient::CUpDownClient(CPartFile *in_reqfile, const uint16 in_port, const uint32 in_userid, const uint32 in_serverip, const uint16 in_serverport, const bool ed2kID, const CAddress& IP)
	: socket()
	, m_reqfile(in_reqfile)
{
	//Converting to the HybridID system. The ED2K system didn't take into account of IP address ending in 0.
	//All IP addresses ending in 0 were assumed to be a low ID because of the calculations.
	Init();
	m_nUserPort = in_port;

	if (IP.IsNull()) {
		//If this is an ED2K source with highID, convert it to a HyrbidID.
		//Else, it is already in hybrid form.
		m_nUserIDHybrid = (ed2kID && !::IsLowID(in_userid)) ? ntohl(in_userid) : in_userid;

		//If high ID and ED2K source, incoming ID and IP are equal.
		//If high ID and Kad source, incoming ID needs ntohl for the IP
		if (!HasLowID() && ed2kID)
			m_ConnectIP = CAddress(in_userid, false);
		else if (!HasLowID())
			m_ConnectIP = CAddress(in_userid, true);
	} else {
		m_nUserIDHybrid = IP.ToUInt32(true);
		m_ConnectIP = IP;
	}
	m_dwServerIP = in_serverip;
	m_nServerPort = in_serverport;
}

void CUpDownClient::Init()
{
	credits = NULL;
	m_Friend = NULL;
	m_abyUpPartStatus = NULL;
	m_lastPartAsked = _UI16_MAX;
	m_bAddNextConnect = false;
	m_dwLastNatStallRecover = 0;
	m_uNatStallRecoverBurst = 0;
	m_dwLastBlockReceived = 0;
	m_dwLastNatKeepAliveSent = 0;

	m_cFailed = 0; // holds the failed connection attempts

	m_uSharedFilesStatus = S_NOT_QUERIED;
	m_bQueryingSharedFiles = false;
	m_tSharedFilesLastQueriedTime = 0;
	m_uSharedFilesCount = 0;

	m_bAutoQuerySharedFiles = false;

	// Remark: a client will be remove from an upload queue after 2*FILEREASKTIME (~1 hour)
	//         a two small value increases the traffic + causes a banishment if lower than 10 minutes
	//         srand() is already called a few times..
	uint32 SpreadTime = rand() * MIN2S(4) / RAND_MAX; // 0..4 minutes, keep in mind integer overflow
	m_dwSpreadReAskTime = FILEREASKTIME + SEC2MS(SpreadTime) - MIN2MS(2); // -2..+2 minutes, keep the same average overload
	// Result between 27 and 31 this is useful to use TCP-Connection from older clients

	CAddress IP = CAddress(CAddress::IPv4);
	if (socket) {
		SOCKADDR_IN6 sockAddr = { 0 };
		int nSockAddrLen = sizeof(sockAddr);
		socket->GetPeerName((SOCKADDR*)&sockAddr, &nSockAddrLen);
		IP.FromSA((SOCKADDR*)&sockAddr, nSockAddrLen);
	}
	SetIP(IP);
	m_bOpenIPv6 = false;

	m_dwServerIP = 0;
	m_nUserIDHybrid = 0;
	m_nUserPort = 0;
	m_nServerPort = 0;
	m_nClientVersion = 0;

	m_byEmuleVersion = 0;
	m_byDataCompVer = 0;
	m_bEmuleProtocol = false;
	m_bIsHybrid = false;

	m_pszUsername = NULL;
	md4clr(m_achUserHash);
	m_nUDPPort = 0;
	m_nKadPort = 0;

	m_byUDPVer = 0;
	m_bySourceExchange1Ver = 0;
	m_byAcceptCommentVer = 0;
	m_byExtendedRequestsVer = 0;

	m_byCompatibleClient = 0;
	m_bFriendSlot = false;
	m_bCommentDirty = false;
	m_bIsML = false;

	m_bGPLEvildoer = false;
	m_bHelloAnswerPending = false;
	m_bPendingStartUploadReq = false;
	md4clr(m_PendingUpFileHash);
	m_byInfopacketsReceived = IP_NONE;
	m_bySupportSecIdent = 0;

	m_dwLastSourceRequest = 0;
	m_dwLastSourceAnswer = 0;
	m_dwLastAskedForSources = 0;
	m_uSearchID = 0;
	m_iFileListRequested = 0;

	m_uFileRating = 0;
	m_cMessagesReceived = 0;
	m_cMessagesSent = 0;
	m_bMultiPacket = false;

	m_bUnicodeSupport = false;
	m_nServingBuddyPort = 0;

	m_LastCallbackRequesterIP = CAddress();

	m_byKadVersion = 0;
	m_cCaptchasSent = 0;

	SetLastBuddyPingPongTime();
	SetServingBuddyID(NULL);

	// eServer Buddy initialization
	m_bSupportsEServerBuddy = false;
	m_bHasEServerBuddySlot = false;
	m_bSupportsEServerBuddyExtUdpPort = false;
	m_bSupportsEServerBuddyMagicProof = false;
	m_dwReportedServerIP = 0;
	m_dwEServerBuddyRetryAfter = 0;
	m_nEServerBuddyRequestCount = 0;
	m_dwEServerBuddyFirstRequest = 0;
	m_nEServerRelayRequestCount = 0;
	m_dwEServerRelayFirstRequest = 0;
	m_pPendingEServerRelayTarget = NULL;
	m_dwEServerExtPortProbeToken = 0;
	m_dwEServerExtPortProbeSent = 0;
	m_nObservedExternalUdpPort = 0;
	m_dwObservedExternalUdpPortTime = 0;
	m_dwLastEServerBuddyValidServerPing = 0;

	m_clientSoft = SO_UNKNOWN;
	m_eChatstate = MS_NONE;
	m_eKadState = KS_NONE;
	m_SecureIdentState = IS_UNAVAILABLE;
	m_SecureIdentVerifiedIP = CAddress();
	m_bSecureIdentRecheckPending = false;
	m_eUploadState = US_NONE;
	m_eDownloadState = DS_NONE;
	m_eSourceFrom = SF_SERVER;
	m_eChatCaptchaState = CA_NONE;
	m_eConnectingState = CCS_NONE;
	m_uNattRetryLeft = 0;
	m_dwNattNextRetryTick = 0;
	m_dwUtpConnectionStartTick = 0;
	m_dwUtpQueuedPacketsTime = 0;
	m_bUtpWritable = false;
	m_bUtpHelloQueued = false;
	m_dwHelloLastSentTick = 0;
	m_uHelloResendCount = 0;
	m_bDeferredNatConnect = false;
	m_bNatTFatalConnect = false;
	m_DeferredNatIP = CAddress();
	m_uDeferredNatPort = 0;
	m_uDeferredNatPortWindow = 0;
	m_bAwaitingDirectCallback = false;
	m_bAllowRendezvousAfterCallback = false;
	m_dwDirectCallbackAttemptTick = 0;
	m_bEServerRelayNatTGuardActive = false;
	m_dwEServerRelayNatTWindowStart = 0;
	m_uEServerRelayTransientErrors = 0;
	ResetUtpFlowControl();
	ClearUtpQueuedPackets();

	m_nTransferredUp = 0;
	m_dwUploadTime = 0;
	m_cAsked = 0;
	m_dwLastUpRequest = 0;
	m_nCurSessionUp = 0;
	m_nCurSessionDown = 0;
	m_nCurQueueSessionPayloadUp = 0; // PENDING: Is this necessary? ResetSessionUp()...
	m_addedPayloadQueueSession = 0;
	m_nUpPartCount = 0;
	m_nUpCompleteSourcesCount = 0;
	md4clr(requpfileid);
	m_slotNumber = 0;
	m_bCollectionUploadSlot = false;

	m_pReqFileAICHHash = NULL;
	m_cDownAsked = 0;
	m_abyPartStatus = NULL;
	m_nTransferredDown = 0;
	m_nCurSessionPayloadDown = 0;
	m_dwDownStartTime = 0;
	m_nLastBlockOffset = _UI64_MAX;
	m_dwLastBlockReceived = 0;
	m_uNatStallRecoverBurst = 0;
	m_nTotalUDPPackets = 0;
	m_nFailedUDPPackets = 0;
	m_nRemoteQueueRank = 0;

	m_bRemoteQueueFull = false;
	m_bCompleteSource = false;
	m_nPartCount = 0;

	m_cShowDR = 0;
	m_bReaskPending = false;
	m_bUDPPending = false;
	m_bTransferredDownMini = false;

	m_nUpDatarate = 0;
	m_nSumForAvgUpDataRate = 0;

	m_nDownDatarate = 0;
	m_nDownDataRateMS = 0;
	m_nSumForAvgDownDataRate = 0;

	m_lastRefreshedULDisplay = ::GetTickCount();
	m_random_update_wait = (DWORD)(rand() % SEC2MS(1));

	// Init fast-fail backoff for unexpected blocks
	m_dwLastInvalidBlockStateTick = 0;
	m_uInvalidBlockStateBurst = 0;

	// Init backoff for out-of-window data while downloading
	m_dwLastUnexpectedBlockTick = 0;
	m_uUnexpectedBlockBurst = 0;

	m_fHashsetRequestingMD4 = 0;
	m_fSharedDirectories = 0;
	m_fSentCancelTransfer = 0;
	m_fNoViewSharedFiles = 0;
	m_fSupportsPreview = 0;
	m_fPreviewReqPending = 0;
	m_fPreviewAnsPending = 0;
	m_fIsSpammer = 0;
	m_fMessageFiltered = 0;
	m_fPeerCache = 0;
	m_fQueueRankPending = 0;
	m_fUnaskQueueRankRecv = 0;
	m_fFailedFileIdReqs = 0;
	m_fNeedOurPublicIP = 0;
	m_fSupportsAICH = 0;
	m_fAICHRequested = 0;
	m_fSentOutOfPartReqs = 0;
	m_fSupportsLargeFiles = 0;
	m_fExtMultiPacket = 0;
	m_fRequestsCryptLayer = 0;
	m_fSupportsCryptLayer = 0;
	m_fRequiresCryptLayer = 0;
	m_fSupportsSourceEx2 = 0;
	m_fSupportsCaptcha = 0;
	m_fDirectUDPCallback = 0;
	m_fSupportsFileIdent = 0;
	m_fHashsetRequestingAICH = 0;

	m_ModMiscOptions.Bits = 0;
	m_dwLastServingBuddyPullReq = 0;
	m_uServingBuddyPullTries = 0;

	lastSwapForSourceExchangeTick = 0;
	m_dwLastTriedToConnect = m_lastRefreshedULDisplay - MIN2MS(20); // ZZ:DownloadManager
	m_bSourceExchangeSwapped = false; // ZZ:DownloadManager

	m_uBadClientCategory = PR_NOTBADCLIENT;
	uhashsize=16;
	m_uUserNameChangerCounter=0;
	m_dwUserNameChangerIntervalStart = ::GetTickCount();
	m_uiXSReqs = 0;
	m_uiXSAnswer = 0;
	m_fileFNF = NULL; //==sFrQlXeRt=> File Faker Detection [DavidXanatos]
	uiULAskingCounter = 0;
	tFirstSeen = time(NULL);
	m_bAntiUploaderCaseThree = false;
	m_bUploaderPunishmentPreventionActive = false; // => Uploader Punishment Prevention for Punish Donkeys without SUI - sFrQlXeRt
	m_faileddownloads = 0;
	uhashsize = 16;		   // New United Community Detection [Xman] - Stulle
	m_bUnitedComm = false; // New United Community Detection [Xman] - Stulle
	m_uModChangeCounter = 0;
	m_dwModChangerIntervalStart = ::GetTickCount();
	m_bForceRecheckShield = false;
	md4clr(m_achUserHashShield);
	m_uTCPErrorCounter = 0;
	m_dwTCPErrorFlooderIntervalStart = ::GetTickCount();

	m_structClientGeolocationData = theApp.geolite2->QueryGeolocationData(m_UserIP);

	m_abyIncPartStatus = NULL;
	m_uIncompletepartVer = 0;
	m_nIncPartCount = 0;

	m_bIsArchived = false;
	m_ArchivedClient = NULL;
	tLastSeen = time(NULL);

	m_tLastCleanUpCheck = time(NULL);

	m_bSendIP = false;
}

CUpDownClient::~CUpDownClient()
{
	if (IsAICHReqPending()) {
		m_fAICHRequested = false;
		CAICHRecoveryHashSet::ClientAICHRequestFailed(this);
	}

	if (GetFriend() != NULL) {
		if (GetFriend()->IsTryingToConnect())
			GetFriend()->UpdateFriendConnectionState(FCR_DELETED);
		m_Friend->SetLinkedClient(NULL);
	}
	ASSERT(m_eConnectingState == CCS_NONE || theApp.IsClosing());

	theApp.clientlist->RemoveClient(this, _T("Destructing client object"));

	int m_iIndex = -1;
	if (thePrefs.GetClientHistoryLog() && (!isnulmd4(GetUserHash()) || GetUserName() != NULL)) {
		if (!theApp.IsClosing() && theApp.emuledlg->transferwnd->GetClientList()->m_ListedItemsMap.Lookup(this, m_iIndex) && m_iIndex >= 0)
			AddDebugLogLine(false, _T("[CLIENT HISTORY]: Client is being deleted, but still in the m_ListedItemsMap -> History: %i | Hash: %s | Name: %s"), m_bIsArchived, md4str(GetUserHash()), (LPCTSTR)EscPercent(GetUserName()));
		else
			AddDebugLogLine(false, _T("[CLIENT HISTORY]: Client is being deleted -> History: %i | Hash: %s | Name: %s"), m_bIsArchived, md4str(GetUserHash()), (LPCTSTR)EscPercent(GetUserName()));
	}

	if (socket) {
		socket->client = NULL;
		socket->Safe_Delete();
	}

	SetUserName(NULL);
	
	delete[] m_abyIncPartStatus;
	m_abyIncPartStatus = NULL;
	m_nIncPartCount = 0;

	delete[] m_abyPartStatus;
	m_abyPartStatus = NULL;

	delete[] m_abyUpPartStatus;
	m_abyUpPartStatus = NULL;


	while (!m_RequestedFiles_list.IsEmpty())
		delete m_RequestedFiles_list.RemoveHead();

	ClearDownloadBlockRequests();

	while (!m_WaitingPackets_list.IsEmpty())
		delete m_WaitingPackets_list.RemoveHead();

	DEBUG_ONLY(theApp.listensocket->Debug_ClientDeleted(this));
	if (!isnulmd4(requpfileid))
		SetUploadFileID(NULL);

	m_fileReaskTimes.RemoveAll(); // ZZ:DownloadManager (one re-ask timestamp for each file)

	delete m_pReqFileAICHHash;
}

void CUpDownClient::ClearHelloProperties()
{
	m_nUDPPort = 0;
	m_byUDPVer = 0;
	m_byDataCompVer = 0;
	m_byEmuleVersion = 0;
	m_bySourceExchange1Ver = 0;
	m_byAcceptCommentVer = 0;
	m_byExtendedRequestsVer = 0;
	m_byCompatibleClient = 0;
	m_nKadPort = 0;
	m_bySupportSecIdent = 0;
	m_fSupportsPreview = 0;
	m_nClientVersion = 0;
	m_fSharedDirectories = 0;
	m_bMultiPacket = 0;
	m_fPeerCache = 0;
	m_byKadVersion = 0;
	m_fSupportsLargeFiles = 0;
	m_fExtMultiPacket = 0;
	m_fRequestsCryptLayer = 0;
	m_fSupportsCryptLayer = 0;
	m_fRequiresCryptLayer = 0;
	m_fSupportsSourceEx2 = 0;
	m_fSupportsCaptcha = 0;
	m_fDirectUDPCallback = 0;
	m_fSupportsFileIdent = 0;
	m_strModVersion.Empty();
	m_uIncompletepartVer = 0;
}

bool CUpDownClient::ProcessHelloPacket(const uchar *pachPacket, uint32 nSize)
{
	CSafeMemFile data(pachPacket, nSize);
	uhashsize = data.ReadUInt8();
	// reset all client's properties; a client may not send a particular emule tag any longer
	ClearHelloProperties();
	
	// When receiving OP_HELLO (responder role), we will send HELLOANSWER.
	// Do not re-arm pending here; it is driven by our outbound HELLO.
	
	// If client is in stale DS_CONNECTED state (from previous session), reset to DS_CONNECTING
	// to force proper handshake sequence. This handles peer restart scenarios.
	if (GetDownloadState() == DS_CONNECTED) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] ProcessHelloPacket: Resetting stale DS_CONNECTED state for %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		SetDownloadState(DS_CONNECTING);
		m_fQueueRankPending = 0;  // Clear stale request flag
	}
	
	return ProcessHelloTypePacket(data);
}

bool CUpDownClient::ProcessHelloAnswer(const uchar *pachPacket, uint32 nSize)
{
	CSafeMemFile data(pachPacket, nSize);
	bool bIsMule = ProcessHelloTypePacket(data);
	m_bHelloAnswerPending = false;
	// Handshake completed. Clear direct-path fallback bookkeeping.
	m_bAwaitingDirectCallback = false;
	m_bAllowRendezvousAfterCallback = false;
	m_dwDirectCallbackAttemptTick = 0;
	if (thePrefs.GetLogNatTraversalEvents())
		AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][Hello] Received OP_HELLOANSWER from %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));

	// Process queued OP_STARTUPLOADREQ now that handshake is complete
	if (HasPendingStartUploadReq()) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] ProcessHelloAnswer: Processing queued OP_STARTUPLOADREQ for %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		
		const uchar* pendingHash = GetPendingUpFileHash();
		
		// Do NOT increment counters here - AddClientToQueue will do it
		// Incrementing twice causes aggressive ban detection to trigger
		
		// NAT-T/uTP connections naturally involve multiple retry attempts due to:
		// - NAT hole punching process requiring multiple packets
		// - uTP packet loss and retransmission mechanisms
		// - Connection establishment through intermediary peers
		// Therefore, bypass aggressive check entirely for NAT-T/uTP connections
		bool bHasUtpLayer = (socket && socket->HaveUtpLayer());
		bool bHasKadPort = (GetKadPort() != 0);
		bool bIsNatTraversalConnection = bHasUtpLayer || bHasKadPort;
		

		
		// Perform aggressive check only for non-NAT-T connections (same logic as in ListenSocket.cpp OP_STARTUPLOADREQ handler)
		if (thePrefs.IsDetectAgressive() && !bIsNatTraversalConnection && !IsBanned() &&
			uiULAskingCounter > uint16(thePrefs.GetAgressiveCounter()) && 
			((time(NULL) - tLastSeen) / uiULAskingCounter) < uint32(MIN2S(thePrefs.GetAgressiveTime()))) {
			if (thePrefs.IsAgressiveLog()) {
				AddProtectionLogLine(false, _T("ProcessHelloAnswer queued OP_STARTUPLOADREQ: (%s/%u)=%s ==> %s(%s) ASK TO FAST!"),
					CastSecondsToHM(time(NULL) - tLastSeen),
					uiULAskingCounter,
					CastSecondsToHM((time(NULL) - tLastSeen) / uiULAskingCounter),
					(LPCTSTR)EscPercent(GetUserName()),
					(LPCTSTR)EscPercent(DbgGetFullClientSoftVer()));
			}
			theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_AGRESSIVE")), PR_AGGRESSIVE);
		}
		
		if (!IsBanned()) {
			CKnownFile* reqfile = theApp.sharedfiles->GetFileByID(pendingHash);
			if (reqfile) {
				if (!md4equ(GetUploadFileID(), pendingHash))
					SetCommentDirty();
				SetUploadFileID(reqfile);
				SendCommentInfo(reqfile);
				if (thePrefs.GetLogNatTraversalEvents())
					AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] ProcessHelloAnswer: Adding client to upload queue for %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
				theApp.uploadqueue->AddClientToQueue(this);
			} else {
				CheckFailedFileIdReqs(pendingHash);
			}
		}
		ClearPendingStartUploadReq();
	}

	// After hello is complete, trigger initial ED2K file request if we have a request context.
	// This ensures uTP rendezvous flows do not stall before ED2K phase.
	if (m_reqfile != NULL && GetDownloadState() == DS_CONNECTED) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] Hello complete -> calling SendFileRequest for %s via %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (socket && socket->HaveUtpLayer()) ? _T("uTP") : _T("TCP"));
		SendFileRequest();
	}
	return bIsMule;
}
bool CUpDownClient::ProcessHelloTypePacket(CSafeMemFile &data)
{
	bool bDbgInfo = thePrefs.GetUseDebugDevice();
	m_strHelloInfo.Empty();
	// clear hello properties which can be changed _only_ on receiving OP_Hello/OP_HelloAnswer
	m_bIsHybrid = false;
	m_bIsML = false;
	m_fNoViewSharedFiles = 0;
	m_bUnicodeSupport = false;

	uchar m_achUserHashTemp[MDX_DIGEST_SIZE];
	md4clr(m_achUserHashTemp);
	data.ReadHash16(m_achUserHashTemp);
	SetUserHash(m_achUserHashTemp);
	if (bDbgInfo)
		m_strHelloInfo.AppendFormat(_T("Hash=%s (%s)"), (LPCTSTR)md4str(m_achUserHash), DbgGetHashTypeString(m_achUserHash));
	m_nUserIDHybrid = data.ReadUInt32();
	if (bDbgInfo)
		m_strHelloInfo.AppendFormat(_T("  UserID=%u (%s)"), m_nUserIDHybrid, (LPCTSTR)ipstr(m_nUserIDHybrid));
	uint16 nUserPort = data.ReadUInt16(); // hmm clientport is sent twice - why?
	if (bDbgInfo)
		m_strHelloInfo.AppendFormat(_T("  Port=%u"), nUserPort);

	bool m_bUnofficialOpcodes = false; //  Will be used to detect ghost mod
	CString m_strUnknownOpcode; //Xman Anti-Leecher
	bool m_bWrongTagOrder = false; //Xman Anti-Leecher
	uint32 m_uHelloTagOrder = 1; //Xman Anti-Leecher
	bool m_bWasUDPPortSent = false; //zz_fly Fake Shareaza Detection
	bool m_bIsBadShareaza = false; //zz_fly Fake Shareaza Detection
	CString m_strPunishmentReason;

	DWORD dwEmuleTags = 0;
	bool bPrTag = false;
	uint32 tagcount = data.ReadUInt32();
	if (bDbgInfo)
		m_strHelloInfo.AppendFormat(_T("  Tags=%u"), tagcount);
	for (uint32 i = 0; i < tagcount; ++i) {
		CTag temptag(data, true);
		switch (temptag.GetNameID()) {
		case CT_NAME:
			if (temptag.IsStr()) {
			CString m_strOldUserName;
			if (thePrefs.IsDetectUserNameChanger() && m_pszUsername)
				m_strOldUserName = CString(m_pszUsername);

		SetUserName(temptag.GetStr());

				theApp.shield->CheckUserNameChanger(this, m_strOldUserName);

				if (bDbgInfo) {
				if (m_pszUsername) { //filter username for bad chars
					for (TCHAR *psz = m_pszUsername; *psz != _T('\0'); ++psz)
						if (*psz == _T('\n') || *psz == _T('\r'))
							*psz = _T(' ');
					m_strHelloInfo.AppendFormat(_T("\n  Name='%s'"), m_pszUsername);
				}
			}
			if (thePrefs.IsEmulateCommunityNickAddons() && m_pszUsername != NULL && _tcslen(m_pszUsername))
				EmulateUserName(false);
		} else {
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());

				if (m_uHelloTagOrder != 1)
					m_bWrongTagOrder = true;
				m_uHelloTagOrder++;

				if (thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("CT_NAME"), PR_WRONGTAGFORMAT);
			}
			break;
		case CT_VERSION:
			if (temptag.IsInt()) {
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  Version=%u"), temptag.GetInt());
				m_nClientVersion = temptag.GetInt();
			} else {
				if (bDbgInfo)
				m_strHelloInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());

				if (m_uHelloTagOrder != 1)
					m_bWrongTagOrder = true;
				m_uHelloTagOrder++;

				if (thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("CT_VERSION"), PR_WRONGTAGFORMAT);
			}
			break;
		case CT_PORT:
			if (temptag.IsInt()) {
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  Port=%u"), temptag.GetInt());
				nUserPort = (uint16)temptag.GetInt();
			} else {
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
				if (thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("CT_PORT"), PR_WRONGTAGFORMAT);
			}
			break;
		case CT_MOD_VERSION:
			{
				CString m_strOldModVersion;
				if (thePrefs.IsDetectModChanger() && !m_strModVersion.IsEmpty())
					m_strOldModVersion = m_strModVersion;

				if (temptag.IsStr())
					m_strModVersion = temptag.GetStr();
				else if (temptag.IsInt())
					m_strModVersion.Format(_T("ModID=%u"), temptag.GetInt());
				else
					m_strModVersion = _T("ModID=<Unknown>");

				theApp.shield->CheckModChanger(this, m_strOldModVersion);

				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  ModID=%s"), (LPCTSTR)m_strModVersion);
				CheckForGPLEvilDoer();
			}
			break;
		case CT_EMULE_UDPPORTS:
			// 16 KAD Port
			// 16 UDP Port
			if (temptag.IsInt()) {
				m_nKadPort = (uint16)(temptag.GetInt() >> 16);
				m_nUDPPort = (uint16)temptag.GetInt();
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  KadPort=%u  UDPPort=%u"), m_nKadPort, m_nUDPPort);
				dwEmuleTags |= 1;
			} else {
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
				if (thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("CT_EMULE_UDPPORTS"), PR_WRONGTAGFORMAT);
			}
			m_bWasUDPPortSent = true;
			break;
		case CT_EMULE_SERVINGBUDDYUDP:
			// 16 --Reserved for future use--
			// 16 SERVING BUDDY Port
			if (temptag.IsInt()) {
				m_nServingBuddyPort = (uint16)temptag.GetInt();
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  ServingBuddyPort=%u"), m_nServingBuddyPort);
			} else {
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
				if (thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("CT_EMULE_SERVINGBUDDYUDP"), PR_WRONGTAGFORMAT);
			}
			break;
		case CT_EMULE_SERVINGBUDDYIP:
			// 32 SERVING BUDDY IP
			if (temptag.IsInt()) {
				m_ServingBuddyIP = CAddress(temptag.GetInt(), false);
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  ServingBuddyIP=%s"), (LPCTSTR)ipstr(m_ServingBuddyIP));
			} else {
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
				if (thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("CT_EMULE_SERVINGBUDDYIP"), PR_WRONGTAGFORMAT);
			}
			break;
		case CT_EMULE_SERVINGBUDDYID:
			m_bUnofficialOpcodes = true; // Will be used to detect ghost mod
			if (temptag.IsHash())
			{
				SetServingBuddyID(temptag.GetHash());
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal] ProcessHelloTypePacket: ServingBuddyID=%s for %s"), (LPCTSTR)md4str(temptag.GetHash()), (LPCTSTR)EscPercent(DbgGetClientInfo()));
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  ServingBuddyID"));
			} else if (bDbgInfo)
				m_strHelloInfo.AppendFormat(_T("\n  ***UnkType=%s"), temptag.GetFullInfo());
			break;
		case CT_EMULE_MISCOPTIONS1:
			//  3 AICH Version (0 = not supported)
			//  1 Unicode
			//  4 UDP version
			//  4 Data compression version
			//  4 Secure Ident
			//  4 Source Exchange - deprecated
			//  4 Ext. Requests
			//  4 Comments
			//	1 PeerChache supported (deprecated)
			//	1 No 'View Shared Files' supported
			//	1 MultiPacket - deprecated with FileIdentifiers/MultipacketExt2
			//  1 Preview
			if (temptag.IsInt()) {
				m_fSupportsAICH = (temptag.GetInt() >> 29) & 0x07;
				m_bUnicodeSupport = (temptag.GetInt() >> 28) & 0x01;
				m_byUDPVer = (uint8)((temptag.GetInt() >> 24) & 0x0f);
				m_byDataCompVer = (uint8)((temptag.GetInt() >> 20) & 0x0f);
				m_bySupportSecIdent = (uint8)((temptag.GetInt() >> 16) & 0x0f);
				m_bySourceExchange1Ver = (uint8)((temptag.GetInt() >> 12) & 0x0f);
				m_byExtendedRequestsVer = (uint8)((temptag.GetInt() >> 8) & 0x0f);
				m_byAcceptCommentVer = (uint8)((temptag.GetInt() >> 4) & 0x0f);
				m_fPeerCache = (temptag.GetInt() >> 3) & 0x01;
				m_fNoViewSharedFiles = (temptag.GetInt() >> 2) & 0x01;
				m_bMultiPacket = (temptag.GetInt() >> 1) & 0x01;
				m_fSupportsPreview = (temptag.GetInt() >> 0) & 0x01;
				dwEmuleTags |= 2;
				if (bDbgInfo) {
					m_strHelloInfo.AppendFormat(_T("\n  PeerCache=%u  UDPVer=%u  DataComp=%u  SecIdent=%u  SrcExchg=%u")
						_T("  ExtReq=%u  Commnt=%u  Preview=%u  NoViewFiles=%u  Unicode=%u")
						, m_fPeerCache, m_byUDPVer, m_byDataCompVer, m_bySupportSecIdent, m_bySourceExchange1Ver
						, m_byExtendedRequestsVer, m_byAcceptCommentVer, m_fSupportsPreview, m_fNoViewSharedFiles, m_bUnicodeSupport);
				}
			} else {
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
				if (thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("CT_EMULE_MISCOPTIONS1"), PR_WRONGTAGFORMAT);
			}
			if (!m_bWasUDPPortSent && !m_bIsBadShareaza)
				m_bIsBadShareaza = true;
			break;
		case CT_EMULE_MISCOPTIONS2:
			//	18 Reserved
			//   1 Supports new FileIdentifiers/MultipacketExt2
			//   1 Direct UDP Callback supported and available
			//	 1 Supports ChatCaptchas
			//	 1 Supports SourceExachnge2 Packets, ignores SX1 Packet Version
			//	 1 Requires CryptLayer
			//	 1 Requests CryptLayer
			//	 1 Supports CryptLayer
			//	 1 Reserved (ModBit)
			//   1 Ext Multipacket (Hash+Size instead of Hash) - deprecated with FileIdentifiers/MultipacketExt2
			//   1 Large Files (includes support for 64bit tags)
			//   4 Kad Version - will go up to version 15 only (may need to add another field at some point in the future)
			if (temptag.IsInt()) {
				m_fSupportsFileIdent = (temptag.GetInt() >> 13) & 0x01;
				m_fDirectUDPCallback = (temptag.GetInt() >> 12) & 0x01;
				m_fSupportsCaptcha = (temptag.GetInt() >> 11) & 0x01;
				m_fSupportsSourceEx2 = (temptag.GetInt() >> 10) & 0x01;
				m_fRequiresCryptLayer = (temptag.GetInt() >> 9) & 0x01;
				m_fRequestsCryptLayer = (temptag.GetInt() >> 8) & 0x01;
				m_fSupportsCryptLayer = (temptag.GetInt() >> 7) & 0x01;
				// reserved 1
				m_fExtMultiPacket = (temptag.GetInt() >> 5) & 0x01;
				m_fSupportsLargeFiles = (temptag.GetInt() >> 4) & 0x01;
				m_byKadVersion = (uint8)((temptag.GetInt() >> 0) & 0x0f);
				dwEmuleTags |= 8;
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  KadVersion=%u, LargeFiles=%u ExtMultiPacket=%u CryptLayerSupport=%u CryptLayerRequest=%u CryptLayerRequires=%u SupportsSourceEx2=%u SupportsCaptcha=%u DirectUDPCallback=%u"), m_byKadVersion, m_fSupportsLargeFiles, m_fExtMultiPacket, m_fSupportsCryptLayer, m_fRequestsCryptLayer, m_fRequiresCryptLayer, m_fSupportsSourceEx2, m_fSupportsCaptcha, m_fDirectUDPCallback);
				m_fRequestsCryptLayer &= m_fSupportsCryptLayer;
				m_fRequiresCryptLayer &= m_fRequestsCryptLayer;
			} else {
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
				if (thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("CT_EMULE_MISCOPTIONS2"), PR_WRONGTAGFORMAT);
			}

			if (!m_bWasUDPPortSent && !m_bIsBadShareaza)
				m_bIsBadShareaza = true;
			break;
		case CT_EMULE_VERSION:
			//  8 Compatible Client ID
			//  7 Mjr Version (Doesn't really matter...)
			//  7 Min Version (Only need 0-99)
			//  3 Upd Version (Only need 0-5)
			//  7 Bld Version (Only need 0-99) -- currently not used
			if (temptag.IsInt()) {
				m_byCompatibleClient = (uint8)((temptag.GetInt() >> 24));
				m_nClientVersion = temptag.GetInt() & 0x00ffffff;
				m_byEmuleVersion = 0x99;
				m_fSharedDirectories = 1;
				dwEmuleTags |= 4;
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  ClientVer=%u.%u.%u.%u  Comptbl=%u"), (m_nClientVersion >> 17) & 0x7f, (m_nClientVersion >> 10) & 0x7f, (m_nClientVersion >> 7) & 0x07, m_nClientVersion & 0x7f, m_byCompatibleClient);
			} else {
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
				if (thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("CT_EMULE_VERSION"), PR_WRONGTAGFORMAT);
			}
			break;
		case ET_INCOMPLETEPARTS:
			m_bUnofficialOpcodes = true; //  Will be used to detect ghost mod
			if (temptag.IsInt()) {
				m_uIncompletepartVer = (uint8)temptag.GetInt();
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  IncVer=%u"), m_uIncompletepartVer);
			} else if (bDbgInfo)
				m_strHelloInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
			// No wrong tag check implemented here since NeoMule uses a string based ET_INCOMPLETEPARTS for ICS v2
			break;
		case CT_MOD_YOUR_IP:
			if (temptag.IsInt()) {
				CAddress tmpIPv4(temptag.GetInt(), false);
				if (tmpIPv4.IsPublicIP()) {
					theApp.m_LastReceivedIPv4 = tmpIPv4;
					if (theApp.GetPublicIPv4() == 0 && theApp.m_LastReceivedIPv4.IsPublicIP())
						theApp.SetPublicIPv4(theApp.m_LastReceivedIPv4.ToUInt32(false));
				}
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  YOUR_IPv4=%s"), ipstr(tmpIPv4));
			} else if (temptag.IsHash()) {
				CAddress tmpIPv6(temptag.GetHash());
				if (tmpIPv6.IsPublicIP())
					theApp.m_LastReceivedIPv6 = tmpIPv6;
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  YOUR_IPv6=%s"), ipstr(tmpIPv6));
			} else {
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
				if (thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("CT_MOD_YOUR_IP"), PR_WRONGTAGFORMAT);
			}
			break;
		case CT_MOD_IP_V6:
			if (temptag.IsHash()) {
				SetIPv6(CAddress(temptag.GetHash()));
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  IPv6=%s"), ipstr(m_UserIPv6));
			} else {
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
				if (thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("CT_MOD_IP_V6"), PR_WRONGTAGFORMAT);
			}
			break;
		case CT_EMULE_SERVINGBUDDYIPV6:
			if (temptag.IsHash()) {
				m_ServingBuddyIP = CAddress(temptag.GetHash());
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  ServingBuddyIPV6=%s"), (LPCTSTR)ipstr(m_ServingBuddyIP));
			} else {
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
				if (thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("CT_EMULE_SERVINGBUDDYIPV6"), PR_WRONGTAGFORMAT);
			}
			break;
		case CT_MOD_MISCOPTIONS:
			m_bUnofficialOpcodes = true; //  Will be used to detect ghost mod
			if (temptag.IsInt()) {
				m_ModMiscOptions.Bits = temptag.GetInt();
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(L"\n  IPv6=%u  NatTraversal=%u  ExtendedXS=%u", m_ModMiscOptions.Fields.SupportsIPv6, m_ModMiscOptions.Fields.SupportsNatTraversal, m_ModMiscOptions.Fields.SupportsExtendedXS);
				if (thePrefs.GetLogExtendedSXEvents && SupportsExtendedSourceExchange() && m_ModMiscOptions.Fields.SupportsExtendedXS)
					DebugLog(_T("[ExtendedSX] ProcessHelloTypePacket: Client supports Extended Source Exchange, %s"), (LPCTSTR)DbgGetClientInfo());
				if (thePrefs.GetLogNatTraversalEvents() && GetNatTraversalSupport() && m_ModMiscOptions.Fields.SupportsNatTraversal)
					DebugLog(_T("[NatTraversal] ProcessHelloTypePacket: Client supports NatTraversal, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			} else if (bDbgInfo)
				m_strHelloInfo.AppendFormat(_T("\n  ***UnkType=%s"), temptag.GetFullInfo());
			break;
		// RESERVED TAGS FOR THE FUTURE RELEASES OF THIS MOD -->
		case CT_MOD_RESERVED_1:
		case CT_MOD_RESERVED_2:
		case CT_MOD_RESERVED_3:
		case CT_MOD_RESERVED_4:
		case CT_MOD_RESERVED_5:
		case CT_MOD_RESERVED_6:
		case OP_SERVINGBUDDYPULL_REQ:
		case OP_SERVINGBUDDYPULL_RES:
		case CT_MOD_RESERVED_B:
		case OP_CHANGE_CLIENT_IP:
		case CT_MOD_SVR_IP_V6:
		// RESERVED TAGS FOR THE FUTURE RELEASES OF THIS MOD <--
		case WC_TAG_VOODOO: // hello packet tag: magic number == 'WebC' for pre-1.1 (unsupported), 'ARC4' for 1.1
		case WC_TAG_FLAGS:  // webcache flags tag
		case 0xEE: //VeryCD L2L protocol tag
		case 0x3E: //X-Ray L2HAC tag
			m_bUnofficialOpcodes = true; //  Will be used to detect ghost mod
			break;
		case CT_ESERVER_BUDDY_FLAGS:
			m_bUnofficialOpcodes = true;
			if (temptag.IsInt()) {
				uint8 byFlags = static_cast<uint8>(temptag.GetInt());
				m_bSupportsEServerBuddy = (byFlags & 0x01) != 0;
				m_bHasEServerBuddySlot = (byFlags & 0x02) != 0;
				m_bSupportsEServerBuddyExtUdpPort = (byFlags & ESERVERBUDDY_FLAG_EXT_UDP_PORT) != 0;
				m_bSupportsEServerBuddyMagicProof = (byFlags & ESERVERBUDDY_FLAG_MAGIC_PROOF) != 0;
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  EServerBuddy=%u Slot=%u"), m_bSupportsEServerBuddy ? 1 : 0, m_bHasEServerBuddySlot ? 1 : 0);
			}
			break;
		case  CT_EMULECOMPAT_OPTIONS1: // This tag is used by official aMule, so it is safe.
			break;
		default:
			// Since eDonkeyHybrid 1.3 is no longer sending the additional Int32 at the end of
			// the Hello packet, we use the "pr=1" tag to determine them.
			if (temptag.GetName() && temptag.GetName()[0] == 'p' && temptag.GetName()[1] == 'r')
				bPrTag = true;

			if (bDbgInfo)
				m_strHelloInfo.AppendFormat(_T("\n  ***UnkTag=%s"), (LPCTSTR)temptag.GetFullInfo());
			theApp.shield->CheckHelloTag(this, temptag);
			m_strUnknownOpcode.AppendFormat(_T(",%s"), temptag.GetFullInfo()); // unknownopcode will trigger Ghost Mod Detection
		}
	}
	m_nUserPort = nUserPort;
	m_dwServerIP = data.ReadUInt32();
	m_nServerPort = data.ReadUInt16();
	m_dwReportedServerIP = m_dwServerIP;	// Save for eServer Buddy
	if (bDbgInfo)
		m_strHelloInfo.AppendFormat(_T("\n  Server=%s:%u"), (LPCTSTR)ipstr(m_dwServerIP), m_nServerPort);

	// Check for additional data in Hello packet to determine client's software version.
	//
	// *) eDonkeyHybrid 0.40 - 1.2 sends an additional Int32. (Since 1.3 they don't send it any longer.)
	// *) MLdonkey sends an additional Int32
	//
	if (data.GetPosition() < data.GetLength()) {
		UINT uAddHelloDataSize = (UINT)(data.GetLength() - data.GetPosition());
		if (uAddHelloDataSize == sizeof(uint32)) {
			uint32 test = data.ReadUInt32();
			if (test == 'KDLM') {
				m_bIsML = true;
				if (bDbgInfo)
					m_strHelloInfo += _T("\n  ***AddData: \"MLDK\"");
			} else {
				m_bIsHybrid = true;
				if (bDbgInfo)
					m_strHelloInfo.AppendFormat(_T("\n  ***AddData: uint32=%u (0x%08x)"), test, test);
			}
		} else if (bDbgInfo) {
			if (uAddHelloDataSize == sizeof(uint32) + sizeof(uint16)) {
				uint32 dwAddHelloInt32 = data.ReadUInt32();
				uint16 w = data.ReadUInt16();
				m_strHelloInfo.AppendFormat(_T("\n  ***AddData: uint32=%u (0x%08x),  uint16=%u (0x%04x)"), dwAddHelloInt32, dwAddHelloInt32, w, w);
			} else
				m_strHelloInfo.AppendFormat(_T("\n  ***AddData: %u bytes"), uAddHelloDataSize);
		}
	}

	CAddress oldIP = m_UserIP;
	
	CAddress IP;
	SOCKADDR_IN6 sockAddr = { 0 };
	int nSockAddrLen = sizeof(sockAddr);
	socket->GetPeerName((SOCKADDR*)&sockAddr, &nSockAddrLen);
	IP.FromSA((SOCKADDR*)&sockAddr, nSockAddrLen);
	SetIP(IP);

	if (oldIP != m_UserIP)
		m_structClientGeolocationData = theApp.geolite2->QueryGeolocationData(m_UserIP);

	if (thePrefs.GetAddServersFromClients() && m_dwServerIP && m_nServerPort) {
		CServer *addsrv = new CServer(m_nServerPort, ipstr(m_dwServerIP));
		addsrv->SetListName(addsrv->GetAddress());
		addsrv->SetPreference(SRV_PR_LOW);
		if (!theApp.emuledlg->serverwnd->serverlistctrl.AddServer(addsrv, true))
			delete addsrv;
	}

	//(a)If this is a highID user, store the ID in the Hybrid format.
	//(b)Some older clients will not send an ID, these client are HighID users that are not connected to a server.
	//(c)Kad users with a *.*.*.0 IPs will look like a lowID user they are actually a highID user. They can be detected easily
	//because they will send an ID that is the same as their IP.
	if (!m_UserIP.IsNull()) {
		if (!HasLowID() || m_nUserIDHybrid == 0 || m_nUserIDHybrid == m_UserIP.ToUInt32(false))
			m_nUserIDHybrid = m_UserIP.ToUInt32(true);
	}

	CClientCredits *pFoundCredits = theApp.clientcredits->GetCredit(m_achUserHash);
	if (credits == NULL) {
		credits = pFoundCredits;
		if (thePrefs.IsDetectHashChanger() && !theApp.clientlist->ComparePriorUserhash(m_UserIP, m_nUserPort, pFoundCredits)) {
			if (thePrefs.GetLogBannedClients())
				AddProtectionLogLine(false, _T("Clients: %s (%s), Ban reason: Userhash changed (Found in TrackedClientsList)"), (LPCTSTR)EscPercent(GetUserName()), (LPCTSTR)ipstr(GetConnectIP()));
			Ban();
		}
	} else if (credits != pFoundCredits) {
		// userhash change OK, however two hours "waittime" before it can be used
		credits = pFoundCredits;
		if (thePrefs.IsDetectHashChanger()) {
			if (thePrefs.GetLogBannedClients())
				AddProtectionLogLine(false, _T("Clients: %s (%s), Ban reason: Userhash changed"), (LPCTSTR)EscPercent(GetUserName()), (LPCTSTR)ipstr(GetConnectIP()));
				Ban();
		}
	}

	// Activate auto query if conditions are met
	if (!m_bAutoQuerySharedFiles && m_uSharedFilesStatus == S_NOT_QUERIED && GetViewSharedFilesSupport() && !m_bQueryingSharedFiles && credits &&
		((thePrefs.GetRemoteSharedFilesSetAutoQueryDownload() && credits->GetDownloadedTotal() / 1048576 >= thePrefs.GetRemoteSharedFilesSetAutoQueryDownloadThreshold()) ||		//1024*1024
			(thePrefs.GetRemoteSharedFilesSetAutoQueryUpload() && credits->GetUploadedTotal() / 1048576 >= thePrefs.GetRemoteSharedFilesSetAutoQueryUploadThreshold()))) {				//1024*1024
		m_bAutoQuerySharedFiles = true;
		if (m_ArchivedClient)
			m_ArchivedClient->m_bAutoQuerySharedFiles = false;
	}

	if (GetFriend() != NULL && GetFriend()->HasUserhash() && !md4equ(GetFriend()->m_abyUserhash, m_achUserHash))
		// this isn't our friend any more and it will be removed/replaced, tell our friend object about it
		if (GetFriend()->IsTryingToConnect())
			GetFriend()->UpdateFriendConnectionState(FCR_USERHASHFAILED); // this will remove our linked friend
		else
			GetFriend()->SetLinkedClient(NULL);

	// do not replace friend objects which have no userhash, but the fitting ip with another friend object with the
	// fitting user hash (both objects would fit to this instance), as this could lead to unwanted results
	if (GetFriend() == NULL || GetFriend()->HasUserhash() || GetFriend()->m_LastUsedIP != GetConnectIP()
		|| GetFriend()->m_nLastUsedPort != GetUserPort())
	{
		m_Friend = theApp.friendlist->SearchFriend(m_achUserHash, m_UserIP, m_nUserPort);
		if (m_Friend != NULL)
			// Link the friend to that client
			m_Friend->SetLinkedClient(this);
		else
			// avoid that an unwanted client instance keeps a friend slot
			SetFriendSlot(false);
	} else
		// however, copy over our userhash in this case
		md4cpy(GetFriend()->m_abyUserhash, m_achUserHash);

	// m_Friend is set at this point. So we need to cancel any punishment made till now if client is a friend and IsDontPunishFriends is active.
	if ((m_uBadClientCategory || m_uPunishment != P_NOPUNISHMENT) && thePrefs.IsDontPunishFriends() && IsFriend()) {
		CString m_strReason;
		m_strReason.Format(_T("<Friend Punishment Prevention> - Client %s"), DbgGetClientInfo());
		theApp.shield->SetPunishment(this, m_strReason, PR_NOTBADCLIENT);
	}

	// check for known major gpl breaker
	CString strBuffer(m_pszUsername);
	strBuffer.Remove(_T(' '));
	strBuffer.MakeUpper();
//	obsolete mods

	m_byInfopacketsReceived |= IP_EDONKEYPROTPACK;
	// check if at least CT_EMULEVERSION was received, all other tags are optional
	bool bIsMule = (dwEmuleTags & 0x04) == 0x04;
	if (bIsMule) {
		m_bEmuleProtocol = true;
		m_byInfopacketsReceived |= IP_EMULEPROTPACK;
	} else if (bPrTag)
		m_bIsHybrid = true;

	InitClientSoftwareVersion();

	if (m_bIsHybrid)
		m_fSharedDirectories = 1;

	if (thePrefs.GetVerbose() && GetServerIP() == INADDR_NONE)
		AddDebugLogLine(false, _T("Received invalid server IP %s from %s"), (LPCTSTR)ipstr(GetServerIP()), (LPCTSTR)EscPercent(DbgGetClientInfo()));

	if (thePrefs.IsDetectHashThief()) { // IsHarderPunishment isn't necessary here since the cost is low
		if (theApp.GetID() != m_nUserIDHybrid && md4equ(m_achUserHash, thePrefs.GetUserHash())) {
			theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_HASH_THIEF")), PR_HASHTHIEF);
		}
	}

	// New United Community Detection [Xman] - Stulle
	if (uhashsize != 16)
		m_bUnitedComm = true;
	else
		m_bUnitedComm = false;

	if (theApp.shield->UploaderPunishmentPreventionActive(this) || (thePrefs.IsDontPunishFriends() && IsFriend())) // => Don't ban friends - sFrQlXeRt
		return bIsMule;

	//Bad Shareaza detection [zz_fly]: Shareaza like client send UDPPort tag AFTER Misc Options tag. So check if UDP sent after Misc Options tag but pretends to be a mule?
	if (thePrefs.IsDetectFakeEmule()) { // IsHarderPunishment isn't necessary here since the cost is low
		if (m_bIsBadShareaza && m_clientSoft == SO_EMULE) {
			theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_FAKE_EMULE_VERSION")), PR_FAKEMULEVERSION);
		}
	}

	if (thePrefs.IsDetectWrongTag()) { // IsHarderPunishment isn't necessary here since the cost is low
		if (data.GetPosition() < data.GetLength()) {
			UINT uAddHelloDataSize = (UINT)(data.GetLength() - data.GetPosition());
			m_strPunishmentReason.Format(GetResString(_T("PUNISHMENT_REASON_EXTRA_BYTES_IN_HELLO")), uAddHelloDataSize);
			theApp.shield->SetPunishment(this, m_strPunishmentReason, PR_WRONGTAGHELLOSIZE);
		}

		if (m_clientSoft == SO_EMULE || (m_clientSoft == SO_XMULE && m_byCompatibleClient != SO_XMULE)) {
			if (m_bWrongTagOrder)
				theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG_ORDER")), PR_WRONGTAGORDER);
		
			if (uhashsize != 16)
				theApp.shield->SetPunishment(this, (GetResString(_T("PUNISHMENT_REASON_WRONG_HASH_SIZE"))), PR_WRONGTAGHASH);

			if (m_fSupportsAICH > 1 && m_clientSoft == SO_EMULE && m_nClientVersion <= MAKE_CLIENT_VERSION(CemuleApp::m_nVersionMjr, CemuleApp::m_nVersionMin, CemuleApp::m_nVersionUpd))
				theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("Applejuice"), PR_WRONGTAGAICH);

		}
	}

	theApp.shield->CheckClient(this); //test for mod name, nick and thiefs

	if (thePrefs.IsDetectEmcrypt()) { // IsHarderPunishment isn't necessary here since the cost is low
		//Xman remark: I only check for 0.44d. 
		if (m_nClientVersion == MAKE_CLIENT_VERSION(0, 44, 3) && m_strModVersion.IsEmpty() && m_byCompatibleClient == 0 && m_bUnicodeSupport == false && bIsMule)
			theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_EMCRYPT")), PR_EMCRYPT);
	}

	if (thePrefs.IsDetectGhostMod() && m_strModVersion.IsEmpty() &&	((m_bUnofficialOpcodes == true && GetClientSoft() != SO_LPHANT) // IsHarderPunishment isn't necessary here since the cost is low
		|| ((m_strUnknownOpcode.IsEmpty() == false || m_byAcceptCommentVer > 1) && m_clientSoft == SO_EMULE && m_nClientVersion <= MAKE_CLIENT_VERSION(CemuleApp::m_nVersionMjr, CemuleApp::m_nVersionMin, CemuleApp::m_nVersionUpd)))) {
		m_strPunishmentReason = GetResString(_T("PUNISHMENT_REASON_GHOST_MOD"));
		if (m_strUnknownOpcode.IsEmpty() == false)
			m_strPunishmentReason += _T(" ") + m_strUnknownOpcode;
		theApp.shield->SetPunishment(this, m_strPunishmentReason, PR_GHOSTMOD);
	}

	return bIsMule;
}

void CUpDownClient::SendHelloPacket()
{
	if (socket == NULL) {
		ASSERT(0);
		return;
	}

	CSafeMemFile data(128);
	data.WriteUInt8(16); // size of userhash
	SendHelloTypePacket(data);
	Packet *packet = new Packet(data);
	packet->opcode = OP_HELLO;
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_Hello", this);
	if (thePrefs.GetLogNatTraversalEvents())
		AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][Hello] Sending OP_HELLO to %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
	theStats.AddUpDataOverheadOther(packet->size);
	SendPacket(packet);

	m_bHelloAnswerPending = true;
	m_dwHelloLastSentTick = ::GetTickCount();
	// Do not reset m_uHelloResendCount here; allow tracking resends across attempts.
}

void CUpDownClient::SendMuleInfoPacket(bool bAnswer)
{
	if (socket == NULL) {
		ASSERT(0);
		return;
	}

	CSafeMemFile data(128);
	data.WriteUInt8((uint8)theApp.m_uCurVersionShort);
	data.WriteUInt8(EMULE_PROTOCOL);
	const bool bSendModVersion = m_strModVersion.GetLength() || m_pszUsername == NULL; //Don't send MOD_VERSION to client that don't support it to reduce overhead

	const bool bSendICS = (bSendModVersion || GetIncompletePartVersion());
	data.WriteUInt32(7 + bSendModVersion + bSendICS); // Number of tags
	CTag tag(ET_COMPRESSION, 1);
	tag.WriteTagToFile(data);
	CTag tag2(ET_UDPVER, 4);
	tag2.WriteTagToFile(data);
	CTag tag3(ET_UDPPORT, thePrefs.GetUDPPort());
	tag3.WriteTagToFile(data);
	CTag tag4(ET_SOURCEEXCHANGE, 3);
	tag4.WriteTagToFile(data);
	CTag tag5(ET_COMMENTS, 1);
	tag5.WriteTagToFile(data);
	CTag tag6(ET_EXTENDEDREQUEST, 2);
	tag6.WriteTagToFile(data);

	uint32 dwTagValue = (theApp.clientcredits->CryptoAvailable() ? 3 : 0);
	if (thePrefs.CanSeeShares() != vsfaNobody) // set 'Preview supported' only if 'View Shared Files' allowed
		dwTagValue |= 0x80;
	CTag tag7(ET_FEATURES, dwTagValue);
	tag7.WriteTagToFile(data);

	if (bSendModVersion){
		CTag tag8(ET_MOD_VERSION, MOD_VERSION);
		tag8.WriteTagToFile(data);
	}

	if (bSendICS) {
		CTag tag9(ET_INCOMPLETEPARTS, 1);
		tag9.WriteTagToFile(data);
	}

	Packet *packet = new Packet(data, OP_EMULEPROT);
	packet->opcode = bAnswer ? OP_EMULEINFOANSWER : OP_EMULEINFO;

	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend(bAnswer ? "OP_EmuleInfoAnswer" : "OP_EmuleInfo", this);
	theStats.AddUpDataOverheadOther(packet->size);
	SendPacket(packet);
}

void CUpDownClient::ProcessMuleInfoPacket(const uchar *pachPacket, uint32 nSize)
{
	bool bDbgInfo = thePrefs.GetUseDebugDevice();
	m_strMuleInfo.Empty();
	bool m_bUnOfficialOpcodes = false;
	CString m_strUnknownOpcode;
	CString m_strPunishmentReason;
	CSafeMemFile data(pachPacket, nSize);
	m_byCompatibleClient = 0;
	m_byEmuleVersion = data.ReadUInt8();
	if (bDbgInfo)
		m_strMuleInfo.AppendFormat(_T("EmuleVer=0x%x"), (UINT)m_byEmuleVersion);
	if (m_byEmuleVersion == 0x2B)
		m_byEmuleVersion = 0x22;
	uint8 protversion = data.ReadUInt8();
	if (bDbgInfo)
		m_strMuleInfo.AppendFormat(_T("  ProtVer=%u"), (UINT)protversion);
	if (protversion != EMULE_PROTOCOL)
		return;

	m_uIncompletepartVer = 0;

	//implicitly supported options by older clients
	//in the future do not use version to guess about new features
	if (m_byEmuleVersion < 0x25 && m_byEmuleVersion > 0x22)
		m_byUDPVer = 1;

	if (m_byEmuleVersion < 0x25 && m_byEmuleVersion > 0x21)
		m_bySourceExchange1Ver = 1;

	if (m_byEmuleVersion == 0x24)
		m_byAcceptCommentVer = 1;

	// Shared directories are requested from eMule 0.28+ because eMule 0.27 has a bug in
	// the OP_ASKSHAREDFILESDIR handler, which does not return the shared files for a
	// directory which has a trailing backslash.
	if (m_byEmuleVersion >= 0x28 && !m_bIsML) // MLdonkey currently does not support shared directories
		m_fSharedDirectories = 1;

	uint32 tagcount = data.ReadUInt32();
	if (bDbgInfo)
		m_strMuleInfo.AppendFormat(_T("  Tags=%u"), tagcount);
	for (uint32 i = 0; i < tagcount; ++i) {
		CTag temptag(data, false);
		switch (temptag.GetNameID()) {
		case ET_COMPRESSION:
			// Bits 31- 8: 0 - reserved
			// Bits  7- 0: data compression version
			if (temptag.IsInt()) {
				m_byDataCompVer = (uint8)temptag.GetInt();
				if (bDbgInfo)
					m_strMuleInfo.AppendFormat(_T("\n  Compr=%u"), temptag.GetInt());
			} else {
				if (bDbgInfo)
					m_strMuleInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
				if (!temptag.IsInt() && thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("ET_COMPRESSION"), PR_WRONGTAGINFOFORMAT);
			}
			break;
		case ET_UDPPORT:
			// Bits 31-16: 0 - reserved
			// Bits 15- 0: UDP port
			if (temptag.IsInt()) {
				m_nUDPPort = (uint16)temptag.GetInt();
				if (bDbgInfo)
					m_strMuleInfo.AppendFormat(_T("\n  UDPPort=%u"), temptag.GetInt());
			} else {
				if (bDbgInfo)
					m_strMuleInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
				if (!temptag.IsInt() && thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("ET_UDPPORT"), PR_WRONGTAGINFOFORMAT);
			}
			break;
		case ET_UDPVER:
			// Bits 31- 8: 0 - reserved
			// Bits  7- 0: UDP protocol version
			if (temptag.IsInt()) {
				m_byUDPVer = (uint8)temptag.GetInt();
				if (bDbgInfo)
					m_strMuleInfo.AppendFormat(_T("\n  UDPVer=%u"), temptag.GetInt());
			} else {
				if (bDbgInfo)
				m_strMuleInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
				if (!temptag.IsInt() && thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("ET_UDPVER"), PR_WRONGTAGINFOFORMAT);
			}
			break;
		case ET_SOURCEEXCHANGE:
			// Bits 31- 8: 0 - reserved
			// Bits  7- 0: source exchange protocol version
			if (temptag.IsInt()) {
				m_bySourceExchange1Ver = (uint8)temptag.GetInt();
				if (bDbgInfo)
					m_strMuleInfo.AppendFormat(_T("\n  SrcExch=%u"), temptag.GetInt());
			} else {
				if (bDbgInfo)
				m_strMuleInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
				if (!temptag.IsInt() && thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("ET_SOURCEEXCHANGE"), PR_WRONGTAGINFOFORMAT);
			}
			break;
		case ET_COMMENTS:
			// Bits 31- 8: 0 - reserved
			// Bits  7- 0: comments version
			if (temptag.IsInt()) {
				m_byAcceptCommentVer = (uint8)temptag.GetInt();
				if (bDbgInfo)
					m_strMuleInfo.AppendFormat(_T("\n  Commnts=%u"), temptag.GetInt());
			} else {
				if (bDbgInfo)
					m_strMuleInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
				if (!temptag.IsInt() && thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("ET_COMMENTS"), PR_WRONGTAGINFOFORMAT);
			}
			break;
		case ET_EXTENDEDREQUEST:
			// Bits 31- 8: 0 - reserved
			// Bits  7- 0: extended requests version
			if (temptag.IsInt()) {
				m_byExtendedRequestsVer = (uint8)temptag.GetInt();
				if (bDbgInfo)
					m_strMuleInfo.AppendFormat(_T("\n  ExtReq=%u"), temptag.GetInt());
			} else {
				if (bDbgInfo)
					m_strMuleInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
				if (!temptag.IsInt() && thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("ET_EXTENDEDREQUEST"), PR_WRONGTAGINFOFORMAT);
			}
			break;
		case ET_COMPATIBLECLIENT:
			// Bits 31- 8: 0 - reserved
			// Bits  7- 0: compatible client ID
			if (temptag.IsInt()) {
				m_byCompatibleClient = (uint8)temptag.GetInt();
				if (bDbgInfo)
					m_strMuleInfo.AppendFormat(_T("\n  Comptbl=%u"), temptag.GetInt());
			} else {
				if (bDbgInfo)
				m_strMuleInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
				if (!temptag.IsInt() && thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("ET_COMPATIBLECLIENT"), PR_WRONGTAGINFOFORMAT);
			}
			break;
		case ET_FEATURES:
			// Bits 31- 8: 0 - reserved
			// Bit	7: Preview
			// Bit  6- 0: secure identification
			if (temptag.IsInt()) {
				m_bySupportSecIdent = (uint8)((temptag.GetInt()) & 3);
				m_fSupportsPreview = (temptag.GetInt() >> 7) & 1;
				if (bDbgInfo)
					m_strMuleInfo.AppendFormat(_T("\n  SecIdent=%u  Preview=%u"), m_bySupportSecIdent, m_fSupportsPreview);
			} else {
				if (bDbgInfo)
					m_strMuleInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
				if (!temptag.IsInt() && thePrefs.IsDetectWrongTag())
					theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_WRONG_TAG")) + _T("ET_FEATURES"), PR_WRONGTAGINFOFORMAT);
			}
			break;
		case ET_MOD_VERSION:
			if (temptag.IsStr())
				m_strModVersion = temptag.GetStr();
			else if (temptag.IsInt())
				m_strModVersion.Format(_T("ModID=%u"), temptag.GetInt());
			else
				m_strModVersion = _T("ModID=<Unknwon>");
			if (bDbgInfo)
				m_strMuleInfo.AppendFormat(_T("\n  ModID=%s"), (LPCTSTR)m_strModVersion);
			CheckForGPLEvilDoer();
			break;
		case ET_INCOMPLETEPARTS:
			m_bUnOfficialOpcodes = true;
			if (temptag.IsInt()) {
				m_uIncompletepartVer = (uint8)temptag.GetInt();
				if (bDbgInfo)
					m_strMuleInfo.AppendFormat(_T("\n  IncVer=%u"), m_uIncompletepartVer);
			} else if (bDbgInfo)
					m_strMuleInfo.AppendFormat(_T("\n  ***UnkType=%s"), (LPCTSTR)temptag.GetFullInfo());
			// No wrong tag check implemented here since NeoMule uses a string based ET_INCOMPLETEPARTS for ICS v2
			break;
		case ET_OS_INFO: // This tag is used by official aMule, so it is safe.
			break;
		default:
			if (bDbgInfo)
				m_strMuleInfo.AppendFormat(_T("\n  ***UnkTag=%s"), (LPCTSTR)temptag.GetFullInfo());
			theApp.shield->CheckInfoTag(this, temptag);
			m_strUnknownOpcode.AppendFormat(_T(",%s"), temptag.GetFullInfo()); // unknownopcode will trigger Ghost Mod Detection
		}
	}
	if (m_byDataCompVer == 0) {
		m_bySourceExchange1Ver = 0;
		m_byExtendedRequestsVer = 0;
		m_byAcceptCommentVer = 0;
		m_nUDPPort = 0;
		m_uIncompletepartVer = 0;
	}
	if (bDbgInfo && data.GetPosition() < data.GetLength())
		m_strMuleInfo.AppendFormat(_T("\n  ***AddData: %I64u bytes"), data.GetLength() - data.GetPosition());

	m_bEmuleProtocol = true;
	m_byInfopacketsReceived |= IP_EMULEPROTPACK;
	InitClientSoftwareVersion();

	if (thePrefs.GetVerbose() && GetServerIP() == INADDR_NONE)
		AddDebugLogLine(false, _T("Received invalid server IP %s from %s"), (LPCTSTR)ipstr(GetServerIP()), (LPCTSTR)EscPercent(DbgGetClientInfo()));

	if (theApp.shield->UploaderPunishmentPreventionActive(this) || (thePrefs.IsDontPunishFriends() && IsFriend())) // => Don't ban friends - sFrQlXeRt
		return;

	if (thePrefs.IsDetectWrongTag() && (m_clientSoft == SO_EMULE || (m_clientSoft == SO_XMULE && m_byCompatibleClient != SO_XMULE)) && data.GetPosition() < data.GetLength()) { // IsHarderPunishment isn't necessary here since the cost is low
		UINT uAddHelloDataSize = (UINT)(data.GetLength() - data.GetPosition());
		m_strPunishmentReason.Format(GetResString(_T("PUNISHMENT_REASON_EXTRA_BYTES_IN_INFO")), uAddHelloDataSize);
		theApp.shield->SetPunishment(this, m_strPunishmentReason, PR_WRONGTAGINFOSIZE);
	}

	theApp.shield->CheckClient(this); //test for mods name (older clients send it with the MuleInfoPacket)

	if (thePrefs.IsDetectGhostMod() && m_strModVersion.IsEmpty() &&	((m_bUnOfficialOpcodes == true && GetClientSoft() != SO_LPHANT) // IsHarderPunishment isn't necessary here since the cost is low
		|| (m_strUnknownOpcode.IsEmpty() == false && m_clientSoft == SO_EMULE && m_nClientVersion <= MAKE_CLIENT_VERSION(CemuleApp::m_nVersionMjr, CemuleApp::m_nVersionMin, CemuleApp::m_nVersionUpd))))
	{
		m_strPunishmentReason = GetResString(_T("PUNISHMENT_REASON_GHOST_MOD"));
		if (m_strUnknownOpcode.IsEmpty() == false)
			m_strPunishmentReason += _T(" ") + m_strUnknownOpcode;
		theApp.shield->SetPunishment(this, m_strPunishmentReason, PR_GHOSTMOD);
	}
}

void CUpDownClient::SendHelloAnswer()
{
	if (socket == NULL) {
		ASSERT(0);
		return;
	}

	if (thePrefs.GetLogNatTraversalEvents()) {
		DebugLog(_T("[NatTraversal][HelloAnswer] SendHelloAnswer invoked (uTP=%d, force=%d, queued=%d), %s"),
			(socket && socket->HaveUtpLayer()) ? 1 : 0,
			theApp.serverconnect->AwaitingTestFromIP(GetConnectIP().ToUInt32(false)) ? 1 : 0,
			IsUtpHelloQueued() ? 1 : 0,
			(LPCTSTR)EscPercent(DbgGetClientInfo()));
	}

	CSafeMemFile data(128);
	SendHelloTypePacket(data);
	Packet *packet = new Packet(data);
	packet->opcode = OP_HELLOANSWER;
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_HelloAnswer", this);
	theStats.AddUpDataOverheadOther(packet->size);

	// Servers send a FIN right in the data packet on check connection, so we need to force the response immediate
	AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][Hello] Sending OP_HELLOANSWER to %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
	bool bForceSend = theApp.serverconnect->AwaitingTestFromIP(GetConnectIP().ToUInt32(false));

	if (socket->HaveUtpLayer() && !bForceSend) {
		if (socket->IsConnected()) {
			SetUtpHelloQueued(false);
			m_dwUtpQueuedPacketsTime = 0;
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] Sending OP_HELLOANSWER immediately on connected uTP socket, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			SendPacket(packet, true);
			if (theApp.clientudp)
				theApp.clientudp->PumpUtpOnce();
		} else {
			// Keep HelloAnswerPending untouched here.
			// It tracks the OP_HELLO we initiated and must only clear when OP_HELLOANSWER is received.
			// Queue HelloAnswer only while the uTP socket is not connected yet.
			m_WaitingPackets_list.AddTail(packet);
			m_dwUtpQueuedPacketsTime = ::GetTickCount();
			SetUtpHelloQueued(true);
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] Queued OP_HELLOANSWER for deferred uTP flush, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			return;
		}
	} else {
		socket->SendPacket(packet, true, 0, bForceSend);
	}

	// If there was a pending StartUploadReq queued before handshake finished, process it now
	if (HasPendingStartUploadReq()) {
		CKnownFile* reqfile = theApp.sharedfiles->GetFileByID(GetPendingUpFileHash());
		if (reqfile != NULL) {
			SetUploadFileID(reqfile);
			SendCommentInfo(reqfile);
			if (thePrefs.GetLogNatTraversalEvents())
				AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] Processing pending OP_STARTUPLOADREQ after HelloAnswer: %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			theApp.uploadqueue->AddClientToQueue(this);
		}
		ClearPendingStartUploadReq();
	}
}

// Pending StartUploadReq helpers
void CUpDownClient::SetPendingStartUploadReq(const uchar* hash)
{
	m_bPendingStartUploadReq = true;
	if (hash)
		md4cpy(m_PendingUpFileHash, hash);
}

bool CUpDownClient::HasPendingStartUploadReq() const { return m_bPendingStartUploadReq; }
const uchar* CUpDownClient::GetPendingUpFileHash() const { return m_PendingUpFileHash; }
void CUpDownClient::ClearPendingStartUploadReq() { m_bPendingStartUploadReq = false; md4clr(m_PendingUpFileHash); }

void CUpDownClient::SendHelloTypePacket(CSafeMemFile &data)
{
	uchar hash[16];
	memcpy(hash, thePrefs.GetUserHash(), 16);
	if (thePrefs.IsEmulateMLDonkey() && GetClientSoft() == SO_MLDONKEY)
	{
		if (GetHashType() == SO_OLD_MLDONKEY)
		{
			hash[5] = 'M'; //WiZaRd::Proper Hash Fake :P
			hash[14] = 'L'; //WiZaRd::Proper Hash Fake :P
		}
	}
	else if ((thePrefs.IsEmulateEdonkey() && GetClientSoft() == SO_EDONKEY)
		|| (thePrefs.IsEmulateEdonkeyHybrid() && GetClientSoft() == SO_EDONKEYHYBRID))
	{
		uint8 random = (uint8)(rand() % _UI8_MAX); //Spike2, avoid C4244
		hash[5] = random == 14 ? random + 1 : random; //WiZaRd::Avoid eMule Hash
		random = (uint8)(rand() % _UI8_MAX); //Spike2, avoid C4244
		hash[14] = random == 111 ? random + 1 : random; //WiZaRd::Avoid eMule Hash
		if (thePrefs.IsLogEmulator())
		{
			CString buffer;
			if (GetClientSoft() == SO_EDONKEY)
				buffer.Format(_T("[EMULATE EDONKEY] (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			else if (GetClientSoft() == SO_EDONKEYHYBRID)
				buffer.Format(_T("[EMULATE EDONKEY HYBRID] (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			DebugLog(LOG_INFO | DLP_VERYLOW, buffer);
		}
	}
	data.WriteHash16(hash);

	// Note: 1. The eMule Client ID for a high ID user is his IPv4 in host order. GetID() first tries to return this from KAD. If it is firewalled then tries ed2k Client ID.
	//       2. The ed2k Client ID for a high ID user is his IPv4 in network order, a low ID is a value < 16777216 (it would resolve to an invalid IP that looks like * .*.*.0)
	//		 3. Since ID is stored in 32 bit, there is no LowIDv6 for LowID IPv6 users. If there's no IPv4 but the IPv6 isn't firewalled, we'll set the clients ID as 0xFFFFFFFF.

	uint32 clientid = theApp.GetID();
	if (clientid == 0 && socket && socket->HaveUtpLayer())
		// We've already setup UTP socket since we're firewalled. Now we must send a ID != 0, otherwise the remote client would think we have High ID.
		// GetID didn't return a ed2k ID. 1 is the KAD low ID id.
		clientid = 1; 
	else if (clientid == 0 && !GetIPv6().IsNull())
		clientid = UINT_MAX; // HighID IPv6

	data.WriteUInt32(clientid);
	data.WriteUInt16(thePrefs.GetPort());

	// Base tags: 6 standard + 1 CT_MOD_IP (always) + 1 CT_ESERVER_BUDDY_FLAGS (always) = 8
	uint32 tagcount = 8;

	//Don't send MOD_VERSION to client that don't support it to reduce overhead. Also don't send it to clients IP/Hash/Upload banned.
	const bool bSendModVersion = !IsBadClient() || m_uPunishment > P_UPLOADBAN;
	if (bSendModVersion)
		tagcount++;

	const bool bSendICS = (bSendModVersion || GetIncompletePartVersion()); 
	if (bSendICS) // Send ICS tag only if Mod Version tag exists. This will protect us from being banned because of unknown tags by some clients.
		tagcount++;
	
	if (theApp.clientlist->GetServingBuddy() && theApp.IsFirewalled())
		tagcount += 3;

	const bool bSendMiscHelloTag = bSendModVersion || SupportsExtendedSourceExchange() || GetNatTraversalSupport() || SupportsIPv6();
	if (bSendMiscHelloTag)
		++tagcount;

	if (!theApp.GetPublicIPv6().IsNull())
		tagcount += 1;	// CT_MOD_IP_V6 for IPv6


	data.WriteUInt32(tagcount);

	// eD2K Name
	CString m_strGeneratedUserName = thePrefs.GetUserNick(); // Copy our user name as the initial value in case of below conditions are not met.
	if ((IsBadClient() != PR_MODTHIEF && IsBadClient() != PR_USERNAMETHIEF) || ((IsBadClient() == PR_MODTHIEF || IsBadClient() == PR_USERNAMETHIEF) && !thePrefs.IsInformBadClients())) // send Nickaddon only if we're not informing UserNameThiefs/ModThiefs
	{	
		if (thePrefs.IsEmulateCommunityNickAddons() && m_pszUsername != NULL && _tcslen(m_pszUsername))
			m_strGeneratedUserName = EmulateUserName(true);
		else
			m_strGeneratedUserName = GenerateUserName();

		CTag tagName(CT_NAME, m_strGeneratedUserName);
		tagName.WriteTagToFile(data, UTF8strRaw);
	}
	else if (!thePrefs.IsInformBadClients()) // send the standard-nick to all other Leechers
	{
	CTag tagName(CT_NAME, !m_bGPLEvildoer ? (LPCTSTR)thePrefs.GetUserNick() : _T("Please use a GPL-conforming version of eMule"));
	tagName.WriteTagToFile(data, UTF8strRaw);
	}
	else if (thePrefs.IsInformBadClients() && IsBadClient() && IsBadClient() != PR_AGGRESSIVE) {
		CString m_strNick;
		if (thePrefs.GetInformBadClientsText().GetLength() > 0) { // no need to check for this twice - sFrQlXeRt
			// add ':' at the end if it isn't already there - sFrQlXeRt
			CString strInformText = thePrefs.GetInformBadClientsText().Right(1) == _T(':') ? thePrefs.GetInformBadClientsText() : thePrefs.GetInformBadClientsText() + _T(':');
			m_strNick.Format(_T("%s %s (%s \xAB%s\xBB)"), strInformText, m_strPunishmentReason, m_strAntiUserNameThiefNick, MOD_NAME);
		}
		else
			m_strNick.Format(_T("You are banned! Because you're using a bad client: %s (%s \xAB%s\xBB)"), m_strPunishmentReason, m_strAntiUserNameThiefNick, MOD_NAME);
		CTag tagName(CT_NAME, m_strNick); //>>> WiZaRd::ClientAnalyzer
		tagName.WriteTagToFile(data, UTF8strRaw);

	}
	else if (thePrefs.IsInformBadClients() && IsBadClient() == PR_AGGRESSIVE) {
		CString m_strNick;
		m_strNick.Format(_T("You are banned! Because your client is asking too fast (%s \xAB%s\xBB)"), m_strAntiUserNameThiefNick, MOD_NAME);
		CTag tagName(CT_NAME, m_strNick); //>>> WiZaRd::ClientAnalyzer
		tagName.WriteTagToFile(data, UTF8strRaw);
	}

	if (thePrefs.IsEmulateShareaza() && GetClientSoft() == SO_SHAREAZA)
	{
		 CTag tagVersion(CT_VERSION, SHAREAZAEMUVERSION);
		 tagVersion.WriteTagToFile(data);
		 if (thePrefs.IsLogEmulator())
		 {
			 CString buffer;
			 buffer.Format(_T("[EMULATE SHAREAZA] (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			 DebugLog(LOG_INFO | DLP_VERYLOW, buffer);
		 }
	} else {
		// eD2K Version
		CTag tagVersion(CT_VERSION, EDONKEYVERSION);
		tagVersion.WriteTagToFile(data);
	}

	// eMule UDP Ports
	uint16 kadUDPPort;
	if (Kademlia::CKademlia::IsConnected()) {
		if (Kademlia::CKademlia::GetPrefs()->GetExternalKadPort() != 0
			&& Kademlia::CKademlia::GetPrefs()->GetUseExternKadPort()
			&& Kademlia::CUDPFirewallTester::IsVerified())
		{
			kadUDPPort = Kademlia::CKademlia::GetPrefs()->GetExternalKadPort();
		} else
			kadUDPPort = Kademlia::CKademlia::GetPrefs()->GetInternKadPort();
	} else
		kadUDPPort = 0;
	CTag tagUdpPorts(CT_EMULE_UDPPORTS,
				((uint32)kadUDPPort			   << 16) |
				((uint32)thePrefs.GetUDPPort() <<  0)
				);
	tagUdpPorts.WriteTagToFile(data);

	if (theApp.clientlist->GetServingBuddy() && theApp.IsFirewalled()) {
		if (theApp.clientlist->GetServingBuddy()->GetIP().GetType() == CAddress::IPv6) {
			CTag tagBuddyIP(CT_EMULE_SERVINGBUDDYIPV6, theApp.clientlist->GetServingBuddy()->GetIP().Data());
			tagBuddyIP.WriteTagToFile(data);
		} else if (theApp.clientlist->GetServingBuddy()->GetIP().GetType() == CAddress::IPv4) {
			CTag tagBuddyIP(CT_EMULE_SERVINGBUDDYIP, theApp.clientlist->GetServingBuddy()->GetIP().ToUInt32(false));
			tagBuddyIP.WriteTagToFile(data);
		}

		CTag tagBuddyPort(CT_EMULE_SERVINGBUDDYUDP, ((uint32)theApp.clientlist->GetServingBuddy()->GetUDPPort()));
		tagBuddyPort.WriteTagToFile(data);

		// Send OUR OWN KadID so the buddy can store it and match future OP_REASKCALLBACKUDP requests
		// Only send if Kad is running and we have a valid KadID
		if (Kademlia::CKademlia::IsRunning()) {
			Kademlia::CUInt128 myKadID = Kademlia::CKademlia::GetPrefs()->GetKadID();
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal][SendHello] KadID check: IsRunning=%d myKadID=%s (isZero=%d)"), 1, (LPCTSTR)md4str(myKadID.GetData()), (myKadID == 0) ? 1 : 0);
			if (myKadID != 0) {
				CTag tagBuddyID(CT_EMULE_SERVINGBUDDYID, myKadID.GetData());
				tagBuddyID.WriteTagToFile(data);
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal][SendHello] Sending ServingBuddyID tag: myKadID=%s to %s"), (LPCTSTR)md4str(myKadID.GetData()), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			} else {
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal][SendHello] NOT sending ServingBuddyID tag: KadID is ZERO for %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			}
		} else {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal][SendHello] NOT sending ServingBuddyID tag: Kad NOT running for %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		}
	}

	// eMule Misc. Options #1
	const UINT uUdpVer = 4;
	const UINT uDataCompVer = 1;
	const UINT uSupportSecIdent = theApp.clientcredits->CryptoAvailable() ? 3 : 0;
	// ***
	// deprecated - will be set back to 3 with the next release (to allow the new version to spread first),
	// due to a bug in earlier eMule version. Use SupportsSourceEx2 and new opcodes instead
	const UINT uSourceExchange1Ver = 4;
	// ***
	const UINT uExtendedRequestsVer = 2;
	const UINT uAcceptCommentVer = 1;
	const UINT uNoViewSharedFiles = static_cast<int>(thePrefs.CanSeeShares() == vsfaNobody); // for backward compatibility this has to be a 'negative' flag
	const UINT uMultiPacket = 1;
	const UINT uSupportPreview = static_cast<int>(thePrefs.CanSeeShares() != vsfaNobody); // set 'Preview supported' only if 'View Shared Files' allowed
	const UINT uPeerCache = 0;
	const UINT uUnicodeSupport = 1;
	const UINT nAICHVer = 1;
	CTag tagMisOptions1(CT_EMULE_MISCOPTIONS1,
				(nAICHVer			  << 29) |
				(uUnicodeSupport	  << 28) |
				(uUdpVer			  << 24) |
				(uDataCompVer		  << 20) |
				(uSupportSecIdent	  << 16) |
				(uSourceExchange1Ver  << 12) |
				(uExtendedRequestsVer <<  8) |
				(uAcceptCommentVer	  <<  4) |
				(uNoViewSharedFiles	  <<  2) |
				(uMultiPacket		  <<  1) |
				(uSupportPreview	  <<  0)
				);
	tagMisOptions1.WriteTagToFile(data);

	// eMule Misc. Options #2
	const UINT uKadVersion = KADEMLIA_VERSION;
	const UINT uSupportLargeFiles = 1;
	const UINT uExtMultiPacket = 1;
	const UINT uReserved = 0; // mod bit
	const UINT uSupportsCryptLayer = static_cast<int>(thePrefs.IsCryptLayerEnabled());
	const UINT uRequestsCryptLayer = static_cast<int>(thePrefs.IsCryptLayerPreferred());
	const UINT uRequiresCryptLayer = static_cast<int>(thePrefs.IsCryptLayerRequired());
	const UINT uSupportsSourceEx2 = 1;
	const UINT uSupportsCaptcha = 1;
	// direct callback is only possible if connected to kad, TCP firewalled and verified UDP open (for example on a full cone NAT)
	const UINT uDirectUDPCallback = static_cast<int>(Kademlia::CKademlia::IsRunning() && Kademlia::CKademlia::IsFirewalled()
		&& !Kademlia::CUDPFirewallTester::IsFirewalledUDP(true) && Kademlia::CUDPFirewallTester::IsVerified());
	const UINT uFileIdentifiers = 1;

	CTag tagMisOptions2(CT_EMULE_MISCOPTIONS2,
				//(RESERVED				  )
				(uFileIdentifiers	 << 13) |
				(uDirectUDPCallback  << 12) |
				(uSupportsCaptcha	 << 11) |
				(uSupportsSourceEx2  << 10) |
				(uRequiresCryptLayer <<  9) |
				(uRequestsCryptLayer <<  8) |
				(uSupportsCryptLayer <<  7) |
				(uReserved			 <<  6) |
				(uExtMultiPacket	 <<  5) |
				(uSupportLargeFiles	 <<  4) |
				(uKadVersion		 <<  0)
				);
	tagMisOptions2.WriteTagToFile(data);

	if (thePrefs.IsEmulateShareaza() && GetClientSoft() == SO_SHAREAZA)
	{
		CTag tagMuleVersion(CT_EMULE_VERSION,
			(SO_SHAREAZA << 24) |
			(2 << 17) |
			(2 << 10) |
			(1 << 7) |
			(0)
		);
		tagMuleVersion.WriteTagToFile(data);
	}
	else if (thePrefs.IsEmulateLphant() && GetClientSoft() == SO_LPHANT)
	{
		CTag tagMuleVersion(CT_EMULE_VERSION,
			(SO_LPHANT << 24) |
			(2 << 17) |
			(9 << 10) |
			(0 << 7)
		);
		tagMuleVersion.WriteTagToFile(data);
		if (thePrefs.IsLogEmulator())
		{
			CString buffer;
			buffer.Format(_T("[EMULATE LPHANT] (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			DebugLog(LOG_INFO | DLP_VERYLOW, buffer);
		}
	}
	else if (thePrefs.IsEmulateMLDonkey() && GetClientSoft() == SO_MLDONKEY)
	{
		CTag tagMuleVersion(CT_EMULE_VERSION,
			(SO_MLDONKEY << 24) |
			(2 << 17) |
			(7 << 10) |
			(3 << 7)
		);
		tagMuleVersion.WriteTagToFile(data);
		if (thePrefs.IsLogEmulator())
		{
			CString buffer;
			buffer.Format(_T("[EMULATE MLDONKEY] (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			DebugLog(LOG_INFO | DLP_VERYLOW, buffer);
		}
	}
	else if (thePrefs.IsEmulateEdonkey() && GetClientSoft() == SO_EDONKEY)
	{
		CTag tagMuleVersion(CT_EMULE_VERSION,
			(SO_EDONKEY << 24) |
			(10405 << 17)
		);
		tagMuleVersion.WriteTagToFile(data);
	}
	else if (thePrefs.IsEmulateEdonkeyHybrid() && GetClientSoft() == SO_EDONKEYHYBRID)
	{
		CTag tagMuleVersion(CT_EMULE_VERSION,
			(SO_EDONKEYHYBRID << 24) |
			(10405 << 17)
		);
		tagMuleVersion.WriteTagToFile(data);
	}
	else
	{
	// eMule Version
	CTag tagMuleVersion(CT_EMULE_VERSION,
				(CemuleApp::m_nVersionMjr << 17) |
				(CemuleApp::m_nVersionMin << 10) |
				(CemuleApp::m_nVersionUpd <<  7)
				//(RESERVED					   )
				);
	tagMuleVersion.WriteTagToFile(data);
	}

	if (bSendModVersion) {
		CTag tagMODVersion(CT_MOD_VERSION, CString(MOD_VERSION));
		tagMODVersion.WriteTagToFile(data);
	}

	if (bSendICS) {
		CTag tagIncompleteParts(ET_INCOMPLETEPARTS, 1);
		tagIncompleteParts.WriteTagToFile(data);
	}

	if (bSendMiscHelloTag) {
		UModMiscOptions m_ModMiscOptionsToSend;
		m_ModMiscOptionsToSend.Bits = 0;
		m_ModMiscOptionsToSend.Fields.SupportsExtendedXS = 1;
		m_ModMiscOptionsToSend.Fields.SupportsNatTraversal = thePrefs.IsNatTraversalServiceEnabled() ? 1 : 0;
		m_ModMiscOptionsToSend.Fields.SupportsIPv6 = 1;
		m_ModMiscOptionsToSend.Fields.SupportsServingBuddyPull = 1;
		CTag tagModMiscOptions(CT_MOD_MISCOPTIONS, m_ModMiscOptionsToSend.Bits);
		tagModMiscOptions.WriteTagToFile(data);
	}

	if (GetConnectIP().GetType() == CAddress::IPv6)	{
		CTag tagYourIP(CT_MOD_YOUR_IP, GetConnectIP().Data());
		tagYourIP.WriteTagToFile(data);
	} else {
		CTag tagYourIP(CT_MOD_YOUR_IP, GetConnectIP().ToUInt32(false));
		tagYourIP.WriteTagToFile(data);
	}

	if (!theApp.GetPublicIPv6().IsNull()) {
		CTag tagIPv6(CT_MOD_IP_V6, theApp.GetPublicIPv6().Data());
		tagIPv6.WriteTagToFile(data);
	}

	// eServer Buddy capability (LowID relay support)
	// Bit 0: Supports eServer Buddy protocol
	// Bit 1: Has available serving slot
	// Bit 2: Supports external UDP port discovery
	// Bit 3: Supports magic-file proof extension
	{
		uint8 byEServerFlags = 0x01;	// Always set support bit
		byEServerFlags |= ESERVERBUDDY_FLAG_EXT_UDP_PORT;
		byEServerFlags |= ESERVERBUDDY_FLAG_MAGIC_PROOF;
		if (theApp.serverconnect && theApp.serverconnect->IsConnected() && !theApp.serverconnect->IsLowID()) {
			// HighID: check if we have available slots
			if (theApp.clientlist->GetServedEServerBuddyCount() < thePrefs.GetMaxEServerBuddySlots())
				byEServerFlags |= 0x02;	// Slot available
		}
		CTag tagEServerBuddy(CT_ESERVER_BUDDY_FLAGS, byEServerFlags);
		tagEServerBuddy.WriteTagToFile(data);
	}

	uint32 dwIP;
	uint16 nPort;
	if (theApp.serverconnect->IsConnected()) {
		dwIP = theApp.serverconnect->GetCurrentServer()->GetIP();
		nPort = theApp.serverconnect->GetCurrentServer()->GetPort();
#ifdef _DEBUG
		if (dwIP == theApp.serverconnect->GetLocalIP()) {
			dwIP = 0;
			nPort = 0;
		}
#endif
	} else {
		dwIP = 0;
		nPort = 0;
	}
	data.WriteUInt32(dwIP);
	data.WriteUInt16(nPort);
}

void CUpDownClient::ProcessMuleCommentPacket(const uchar *pachPacket, uint32 nSize)
{
	if (m_reqfile && m_reqfile->IsPartFile()) {
		CSafeMemFile data(pachPacket, nSize);
		uint8 uRating = data.ReadUInt8();
		if (thePrefs.GetLogRatingDescReceived() && uRating > 0)
			AddDebugLogLine(false, (LPCTSTR)GetResString(_T("RATINGRECV")), (LPCTSTR)EscPercent(m_strClientFilename), uRating);
		CString strComment;
		UINT uLength = data.ReadUInt32();
		if (uLength > 0) {
			// we have to increase the raw max. allowed file comment len because of possible UTF-8 encoding.
			if (uLength > MAXFILECOMMENTLEN * 4)
				uLength = MAXFILECOMMENTLEN * 4;
			strComment = data.ReadString(GetUnicodeSupport() != UTF8strNone, uLength);

			if (strComment.GetLength() > MAXFILECOMMENTLEN) // enforce the max len on the comment
				strComment.Truncate(MAXFILECOMMENTLEN);

			if (thePrefs.GetLogRatingDescReceived() && !strComment.IsEmpty())
				AddDebugLogLine(false, (LPCTSTR)GetResString(_T("DESCRIPTIONRECV")), (LPCTSTR)EscPercent(m_strClientFilename), (LPCTSTR)EscPercent(strComment));

			const CString &cfilter(thePrefs.GetCommentFilter());
			// test if comment is filtered
			if (!cfilter.IsEmpty()) {
				CString strCommentLower(strComment);
				strCommentLower.MakeLower();

				for (int iPos = 0; iPos >= 0;) {
					const CString &strFilter(cfilter.Tokenize(_T("|"), iPos));
					// comment filters are already in lower case, compare with temp. lower cased received comment
					if (!strFilter.IsEmpty() && strCommentLower.Find(strFilter) >= 0) {
						strComment.Empty();
						uRating = 0;
						SetSpammer(true);
						break;
					}
				}
			}
		}
		if (!strComment.IsEmpty() || uRating > 0) {
			m_strFileComment = strComment;
			m_uFileRating = uRating;
			m_reqfile->UpdateFileRatingCommentAvail();
		}
	}
}

bool CUpDownClient::Disconnected(LPCTSTR pszReason, bool bFromSocket)
{
	ASSERT(theApp.clientlist->IsValidClient(this));


	if (GetKadState() == KS_QUEUED_FWCHECK_UDP || GetKadState() == KS_CONNECTING_FWCHECK_UDP)
		Kademlia::CUDPFirewallTester::SetUDPFWCheckResult(false, true, GetConnectIP().ToUInt32(true), 0); // inform the tester that this test was cancelled
	else if (GetKadState() == KS_FWCHECK_UDP)
		Kademlia::CUDPFirewallTester::SetUDPFWCheckResult(false, false, GetConnectIP().ToUInt32(true), 0); // inform the tester that this test has failed
	else if (GetKadState() == KS_CONNECTED_BUDDY)
		DebugLogWarning(_T("[Buddy]: Buddy client disconnected - %s, %s"), (LPCTSTR)EscPercent(pszReason), (LPCTSTR)EscPercent(DbgGetClientInfo()));
	//If this is a KAD client object, just delete it!
	SetKadState(KS_NONE);
	m_bAwaitingDirectCallback = false;
	m_bAllowRendezvousAfterCallback = false;
	m_dwDirectCallbackAttemptTick = 0;
	ResetUtpFlowControl();
	ClearUtpQueuedPackets();
	ClearEServerRelayNatTGuard();

	theApp.clientlist->RemoveServedBuddy(this);

	if (GetUploadState() == US_UPLOADING || GetUploadState() == US_CONNECTING)
		// sets US_NONE
		theApp.uploadqueue->RemoveFromUploadQueue(this, CString(_T("CUpDownClient::Disconnected: ")) + pszReason);

	if (GetDownloadState() == DS_DOWNLOADING) {
		ASSERT(m_eConnectingState == CCS_NONE);
		const bool bUtpDisconnectWithoutQueue = bFromSocket
			&& socket != NULL
			&& socket->HaveUtpLayer()
			&& GetRemoteQueueRank() == 0
			&& !IsRemoteQueueFull();
		if (bUtpDisconnectWithoutQueue) {
			// No queue answer was ever confirmed. Reconnect instead of fabricating an OnQueue state.
			m_fQueueRankPending = 0;
			m_fUnaskQueueRankRecv = 0;
			SetRemoteQueueFull(false);
			SetRemoteQueueRank(0);
			SetAskedCountDown(0);
			SetDownloadState(DS_CONNECTING);
			if (m_reqfile != NULL)
				TrigNextSafeAskForDownload(m_reqfile);
		} else {
			SetDownloadState(DS_ONQUEUE, CString(_T("Disconnected: ")) + pszReason);
		}
	} else {
		// ensure that all possible block requests are removed from the partfile
		ClearDownloadBlockRequests();
	}

	// we had still an AICH request pending, handle it
	if (IsAICHReqPending()) {
		m_fAICHRequested = false;
		CAICHRecoveryHashSet::ClientAICHRequestFailed(this);
	}

	while (!m_WaitingPackets_list.IsEmpty())
		delete m_WaitingPackets_list.RemoveHead();

	// The remote client might not reply with OP_HASHSETANSWER *immediately*
	// to our OP_HASHSETREQUEST. A (buggy) remote client may instead send us
	// another OP_FILESTATUS which would let us change DL-state to DS_ONQUEUE.
	if (m_reqfile != NULL) {
		if (m_fHashsetRequestingMD4)
			m_reqfile->m_bMD4HashsetNeeded = true;
		if (m_fHashsetRequestingAICH)
			m_reqfile->SetAICHHashSetNeeded(true);
	}
	if (m_iFileListRequested) {
		m_iFileListRequested = 0;
		LogWarning(LOG_STATUSBAR, GetResString(_T("SHAREDFILES_FAILED")), (GetUserName() == NULL || GetUserName()[0] == '\0') ? _T('(') + md4str(GetUserHash()) + _T(')') : GetUserName());
		m_bQueryingSharedFiles = false;
		if (m_uSharedFilesStatus == S_NOT_QUERIED) // Only update this value if we don't have a response in the past. 
			m_uSharedFilesStatus = S_NO_RESPONSE;
	}

	if (IsFriend())
		theApp.friendlist->RefreshFriend(m_Friend);

	ASSERT(theApp.clientlist->IsValidClient(this));

	//check if this client is needed in any way, if not - delete it
	bool bDelete;

	switch (m_eDownloadState) {
	case DS_ONQUEUE:
	case DS_TOOMANYCONNS:
	case DS_NONEEDEDPARTS:
	case DS_LOWTOLOWIP:
		bDelete = false;
		break;
	default:
		bDelete = (m_eUploadState != US_ONUPLOADQUEUE);
	}

	// Dead Source Handling
	//
	// If we failed to connect to that client, it is supposed to be 'dead'. Add the IP
	// to the 'dead sources' lists so we don't waste resources and bandwidth to connect
	// to that client again within the next hour.
	//
	// But, if we were just connecting to a proxy and failed to do so, that client IP
	// is supposed to be valid until the proxy itself tells us that the IP can not be
	// connected to (e.g. 504 Bad Gateway)
	//

	bool bAddDeadSource = true;

	if (m_eUploadState != US_BANNED) {
		switch (m_eDownloadState) {
		case DS_CONNECTING:
		{
			m_cFailed++;
			if (IsBanned()) { //Xman Anti-Leecher
				bDelete = true; //force delete and no retry and no deadsourcelist
				break;
			}

			//Optional retry connection attempts
			if (thePrefs.IsRetryFailedTcpConnectionAttempts() == false)
				m_cFailed = 5; //force deadsourcelist

			//Udppending only 1 retry, and lets wait 70 sec
			if (m_cFailed < 2) { //We don't know this client since this was the first connection attempt. Now arrange second connection attempt.
				//Default waiting time is 20 min to reconnect on other parts of the code.We want to retry after 70 seconds, so we reset this time by -20min and add 70 sec.
				if (thePrefs.GetLogRetryFailedTcp() && !GetConnectIP().IsNull())
					AddDebugLogLine(false, _T("Client with IP=%s failed to connect %u times"), (LPCTSTR)ipstr(GetConnectIP()), m_cFailed);
				m_dwLastTriedToConnect = ::GetTickCount() - MIN2MS(11) + SEC2MS(70); // MIN2MS(11) = MIN_REQUESTTIME + 60000
				SetDownloadState(DS_NONE);
				bDelete = false; //Delete this socket but not this client
				break;
			} else if (m_cFailed < 3 && GetUserName() != NULL && !m_bUDPPending) { //We know the client. Now arrange third connection attempt.
				if (thePrefs.GetLogRetryFailedTcp())
					AddDebugLogLine(false,_T("Client with IP=%s, Version=%s, Name=%s failed to connect %u times"), (LPCTSTR)ipstr(GetConnectIP()), (LPCTSTR)EscPercent(DbgGetFullClientSoftVer()), (LPCTSTR)EscPercent(GetUserName()), m_cFailed);
				//Default waiting time is 20 min to reconnect on other parts of the code.We want to retry after 50 seconds, so we reset this time by -20min and add 50 sec.
				m_dwLastTriedToConnect = ::GetTickCount() - MIN2MS(11) + SEC2MS(50); // MIN2MS(11) = MIN_REQUESTTIME + 60000
				SetDownloadState(DS_NONE);
				bDelete = false; //Delete this socket but not this client
				break;
			} else if (thePrefs.GetLogRetryFailedTcp()) { // Connection attempt limit reached, log this.
				if (GetUserName() != NULL)
					AddDebugLogLine(false, _T("Client with IP=%s, Version=%s, Name=%s failed to connect"), (LPCTSTR)ipstr(GetConnectIP()), (LPCTSTR)EscPercent(DbgGetFullClientSoftVer()), (LPCTSTR)EscPercent(GetUserName()));
				else if (!GetConnectIP().IsNull())
					AddDebugLogLine(false, _T("Client with IP=%s failed to connect"), (LPCTSTR)ipstr(GetConnectIP()));
			}
			//Xman end
			if (socket && socket->GetProxyConnectFailed())
				bAddDeadSource = false;
		}
		case DS_CONNECTED: //Delete non answering clients
		case DS_REQHASHSET:
		case DS_WAITCALLBACK:
			if (bAddDeadSource)
				theApp.clientlist->m_globDeadSourceList.AddDeadSource(*this);
			theApp.downloadqueue->IncrementFailedTCPFileReask(); //Count the failed TCP-connections
			if (m_bUDPPending)
				theApp.downloadqueue->AddFailedUDPFileReasks(); //For correct statistics, if it wasn't counted on connection established //Xman x4 test
		case DS_ERROR: //Xman Xtreme Mod: this clients get IP-Filtered!
			bDelete = true;
		}
	}

	// We keep chat partners in any case
	if (GetChatState() != MS_NONE) {
		bDelete = false;
		if (GetFriend() != NULL) {
			if (GetFriend()->IsTryingToConnect())
				GetFriend()->UpdateFriendConnectionState(FCR_DISCONNECTED); // handled in friend class
			else
				SetChatState(MS_NONE);
		} else
			theApp.emuledlg->chatwnd->chatselector.ConnectingResult(this, false); // other clients update directly
	}

	// Delete Socket
	if (!bFromSocket && socket) {
		ASSERT(theApp.listensocket->IsValidSocket(socket));
		socket->Safe_Delete();
	}
	socket = NULL;
	if (!bDelete)
		theApp.emuledlg->transferwnd->GetClientList()->RefreshClient(this);

	// finally, remove the client from the timeout timer and reset the connecting state
	m_eConnectingState = CCS_NONE;
	theApp.clientlist->RemoveConnectingClient(this);
	m_bAwaitingDirectCallback = false;
	m_bAllowRendezvousAfterCallback = false;
	m_dwDirectCallbackAttemptTick = 0;

	if (bDelete) {
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			Debug(_T("--- Deleted client            %s; Reason=%s\n"), (LPCTSTR)DbgGetClientInfo(true), pszReason);
		return true;
	}

	if (thePrefs.GetDebugClientTCPLevel() > 0)
		Debug(_T("--- Disconnected client       %s; Reason=%s\n"), (LPCTSTR)DbgGetClientInfo(true), pszReason);
	m_fHashsetRequestingMD4 = 0;
	m_fHashsetRequestingAICH = 0;
	SetSentCancelTransfer(0);
	m_bHelloAnswerPending = false;
	m_fQueueRankPending = 0;
	m_fFailedFileIdReqs = 0;
	m_fUnaskQueueRankRecv = 0;
	m_fSentOutOfPartReqs = 0;
	return false;
}

//Returned bool is not about if TryToConnect was successful or not.
//false means the client was deleted!
//true means the client was not deleted!
bool CUpDownClient::TryToConnect(bool bIgnoreMaxCon, bool bNoCallbacks, CRuntimeClass* pClassSocket, bool bUseUTP)
{
	// There are 7 possible ways how we are going to connect in this function, sorted by priority:
	// 1) Already Connected/Connecting
	//		We are already connected or try to connect right now. Abort, no additional Disconnect() call will be done
	// 2) Immediate Fail
	//		Some precheck or precondition failed, or no other way is available, so we do not try to connect at all
	//		but fail right away, possibly deleting the client as it becomes useless
	// 3) Normal Outgoing TCP Connection
	//		Applies to all HighIDs/Open clients: We do a straight forward connection try to the TCP port of the client
	// 4) Direct Callback Connections
	//		Applies to TCP firewalled - UDP open clients: We sent a UDP packet to the client, requesting him to connect
	//		to us. This is pretty easy too and resource-wise nearly on the same level as 3)
	// (* 5) Waiting/Abort
	//		This check is done outside this function.
	//		We want to connect for some download related thing (for example re-asking), but the client has a LowID and
	//		is on our upload queue. So we are smart and saving resources by just waiting until he re-asks us, so we don't
	//		have to do the resource intensive options 6 or 7. *)
	// 6) Server Callback
	//		This client is firewalled, but connected to our server. We sent the server a callback request to forward to
	//		the client and hope for the best
	// 7) Kad Callback
	//		This client is firewalled, but has a Kad serving buddy. We sent the serving buddy a callback request to forward
	//		to the client and hope for the best

	if (GetKadState() == KS_QUEUED_FWCHECK)
		SetKadState(KS_CONNECTING_FWCHECK);
	else if (GetKadState() == KS_QUEUED_FWCHECK_UDP)
		SetKadState(KS_CONNECTING_FWCHECK_UDP);

	////////////////////////////////////////////////////////////
	// Check for 1) Already Connected/Connecting
	if (m_eConnectingState != CCS_NONE) {
		const bool bPromoteCallbackToDirectUtp =
			bUseUTP
			&& socket == NULL
			&& (m_eConnectingState == CCS_SERVERCALLBACK || m_eConnectingState == CCS_KADCALLBACK);
		if (!bPromoteCallbackToDirectUtp)
			return true;

		if (thePrefs.GetLogNatTraversalEvents()) {
			DebugLog(_T("[NatTraversal] TryToConnect: Promoting callback wait state to direct uTP connect for %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		}
		theApp.clientlist->RemoveConnectingClient(this);
		m_eConnectingState = CCS_NONE;
		if (m_reqfile != NULL && (GetDownloadState() == DS_WAITCALLBACK || GetDownloadState() == DS_WAITCALLBACKKAD))
			SetDownloadState(DS_CONNECTING);
	}
	if (socket != NULL) {
		if (socket->IsConnected()) {
			if (CheckHandshakeFinished()) {
				ConnectionEstablished();
				return true;
			}
			if (socket->HaveUtpLayer()) {
				const DWORD now = ::GetTickCount();
				const DWORD kUtpHandshakeReconnectDelayMs = NAT_TRAVERSAL_HANDSHAKE_GUARD_MS;
				DWORD handshakeAge = 0;
				if (m_dwHelloLastSentTick != 0 && (int)(now - m_dwHelloLastSentTick) >= 0)
					handshakeAge = now - m_dwHelloLastSentTick;
				else if (m_dwUtpConnectionStartTick != 0 && (int)(now - m_dwUtpConnectionStartTick) >= 0)
					handshakeAge = now - m_dwUtpConnectionStartTick;

				if (m_dwHelloLastSentTick != 0 && handshakeAge < kUtpHandshakeReconnectDelayMs) {
					if (thePrefs.GetLogNatTraversalEvents()) {
						DebugLog(_T("[NatTraversal] TryToConnect: Keeping connected uTP socket while handshake pending (age=%lu ms), %s"), handshakeAge, (LPCTSTR)EscPercent(DbgGetClientInfo()));
					}
					return true;
				}
			}
			// Stale connection: socket reports connected but handshake never finished.
			// This can happen when previous uTP connection remained open but Hello-HelloAnswer
			// exchange was never completed. Close the stale socket and reconnect.
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLogWarning(_T("[NatTraversal] TryToConnect: Closing stale socket (connected but handshake not finished) for reconnection, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			socket->Safe_Delete();
			socket = NULL;
			// Fall through to create new connection
		} else {
			socket->Safe_Delete();
		}
	}
	m_eConnectingState = CCS_PRECONDITIONS; // We now officially try to connect :)

	////////////////////////////////////////////////////////////
	// Check for 2) Immediate Fail

    if (theApp.listensocket->TooManySockets() && !bIgnoreMaxCon) {
        if (thePrefs.GetLogNatTraversalEvents())
        	DebugLog(_T("[NATTTESTMODE: TryToConnect] too many sockets: %s\n"), (LPCTSTR)DbgGetClientInfo());
		// This is a sanitize check and counts as a "hard failure", so this check should be also done before calling
		// TryToConnect if a special handling, like waiting till there are enough connection available should be fone
		DebugLogWarning(_T("TryToConnect: Too many connections sanitize check (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		if (Disconnected(GetResString(_T("TOOMANYCONNS")))) {
			delete this;
			return false;
		}
		return true;
	}
	// do not try to connect to source which are incompatible with our encryption setting (one requires it, and the other one doesn't support it)
    if ((RequiresCryptLayer() && !thePrefs.IsCryptLayerEnabled()) || (thePrefs.IsCryptLayerRequired() && !SupportsCryptLayer())) {
        if (thePrefs.GetLogNatTraversalEvents())
        	DebugLog(_T("[NATTTESTMODE: TryToConnect] crypt guard hit: req=%d required=%d supp=%d: %s\n"), (int)RequiresCryptLayer(), (int)thePrefs.IsCryptLayerRequired(), (int)SupportsCryptLayer(), (LPCTSTR)DbgGetClientInfo());
		DEBUG_ONLY(AddDebugLogLine(DLP_DEFAULT, false, _T("Rejected outgoing connection because CryptLayer-Setting (Obfuscation) was incompatible %s"), (LPCTSTR)EscPercent(DbgGetClientInfo())));
		if (Disconnected(_T("CryptLayer-Settings (Obfuscation) incompatible"))) {
			delete this;
			return false;
		}
		return true;
	}

	if (m_UserIPv4.IsNull())
		m_UserIPv4 = CAddress((HasLowID() ? 0 : m_nUserIDHybrid), true);

	const bool bUseIPv6 = m_bOpenIPv6 && !theApp.GetPublicIPv6().IsNull();
	if (bUseIPv6)
		UpdateIP(m_UserIPv6);
	else
		UpdateIP(m_UserIPv4);

	CAddress ClientIP = !GetIP().IsNull() ? GetIP() : GetConnectIP();
	if (!ClientIP.IsNull()) {
		// although we filter all received IPs (server sources, source exchange) and all incoming connection attempts,
		// we do have to filter outgoing connection attempts here too, because we may have updated the ip filter list
		if (theApp.ipfilter->IsFiltered(ClientIP)) {
			++theStats.filteredclients;
			if (thePrefs.GetLogFilteredIPs())
				AddDebugLogLine(false, (LPCTSTR)GetResString(_T("IPFILTERED")), (LPCTSTR)ipstr(ClientIP), (LPCTSTR)EscPercent(theApp.ipfilter->GetLastHit()));
			m_cFailed = 5; //force deletion
			if (Disconnected(_T("IPFilter"))) {
				delete this;
				return false;
			}
			return true;
		}

		// for safety: check again whether that IP is banned
		if (theApp.clientlist->IsBannedClient(ClientIP.ToStringC())) {
			if (thePrefs.GetLogBannedClients())
				AddDebugLogLine(false, _T("Refused to connect to banned client %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			m_cFailed = 5; //force deletion
			if (Disconnected(_T("Banned IP"))) {
				delete this;
				return false;
			}
			return true;
		}
	}

	// Kad buddy handshakes are plain TCP control sessions and may involve peers that still advertise LowID.
	// For these states, do not gate the connect attempt behind callback availability checks.
	const bool bBuddyHandshake = (GetKadState() == KS_CONNECTING_SERVING_BUDDY) || (GetKadState() == KS_INCOMING_SERVED_BUDDY);

	// LowID check: Skip if bUseUTP is set (e.g., after receiving relay response with target's IP/port)
	// In NAT traversal scenarios (eServer Buddy relay), we have the target's IP:port and should attempt
	// direct uTP connection instead of going through the callback mechanism.
	// Note: If IPv6 is specified in the hello, that means it is not firewalled, and if we are IPv6 enabled we use it
	if (!bUseUTP && !bUseIPv6 && HasLowID() && GetKadState() != KS_CONNECTING_FWCHECK && !bBuddyHandshake) {

		ASSERT(pClassSocket == NULL);
		if (!theApp.CanDoCallback(this)) { // lowid2lowid check used for the whole function, don't remove
			if (thePrefs.GetLogNatTraversalEvents()) {
				DebugLog(_T("[NatTraversal] TryToConnect: LowID↔LowID without Kad/UDP path; cannot connect: %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			}
			// We cannot reach this client, so we hard fail to connect, if this client should be kept,
			// for example, because we might want to wait a bit and hope we get a high ID,
			// this check has to be done before calling this function
			if (Disconnected(_T("LowID->LowID"))) {
				delete this;
				return false;
			}
			return true;
		}

		// are callbacks disallowed?
		if (bNoCallbacks) {
			DebugLogError(_T("TryToConnect: Would like to do callback on a no-callback client, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			if (Disconnected(_T("LowID: No Callback Option allowed"))) {
				delete this;
				return false;
			}
			return true;
		}

		// Is any callback available?
    if (!AnyCallbackAvailable())
    {
        if (thePrefs.GetLogNatTraversalEvents())
        	DebugLog(_T("[NATTTESTMODE: TryToConnect] no callback option available guard hit: %s\n"), (LPCTSTR)DbgGetClientInfo());
        // Nope
			if (Disconnected(_T("LowID: No Callback Option available"))) {
				delete this;
				return false;
			}
			return true;
		}
	}

	// Prechecks finished, now for the real connecting
	////////////////////////////////////////////////////

	theApp.clientlist->AddConnectingClient(this); // Starts and checks for the timeout, ensures following Disconnect() or ConnectionEstablished() call

	////////////////////////////////////////////////////////////
	// 3) Normal Outgoing TCP Connection
	// Ensure a sane connect endpoint before NAT-T check: if ConnectIP is empty but public User IP is known, use it.
    if (GetConnectIP().IsNull()) {
		const CAddress& user = GetIP();
		if (!user.IsNull() && user.IsPublicIP()) {
			SetConnectIP(user);
			if (thePrefs.GetLogNatTraversalEvents()) {
				DebugLog(_T("[NatTraversal] TryToConnect: ConnectIP was empty; set to UserIP=%s, %s"), (LPCTSTR)ipstr(user), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			}
		}
	}
	// If the client sent hello packet its NATT flag should be true; if hello packet is not sent we can try to continue with the checks
	// NAT traversal via uTP rendezvous does not require remote's DirectUDPCallback capability.
	// Do NOT use uTP layer for Kad buddy handshakes; those are plain TCP.
    // Require: feature enabled, remote advertises (or hello not yet), both sides have UDP/Kad ports, connect endpoint known, we are firewalled, and not in buddy handshake.
    // Only attempt NAT-T/uTP when there is a file context (reqfile) to avoid triggering uTP connects for non-file clients like buddies.
	const DWORD curTick = ::GetTickCount();
	const bool bHadRendezvousFallback = m_bAllowRendezvousAfterCallback;
	if (m_bAwaitingDirectCallback) {
		if ((int)(curTick - m_dwDirectCallbackAttemptTick) >= (int)RENDEZVOUSFALLBACKDELAY) {
			m_bAwaitingDirectCallback = false;
			m_bAllowRendezvousAfterCallback = true;
			if (!bHadRendezvousFallback && thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] TryToConnect: Direct callback timed out; enabling rendezvous fallback, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		}
	} else if (!m_bAllowRendezvousAfterCallback && m_dwDirectCallbackAttemptTick != 0) {
		if ((int)(curTick - m_dwDirectCallbackAttemptTick) >= (int)RENDEZVOUSFALLBACKDELAY) {
			m_bAllowRendezvousAfterCallback = true;
			if (!bHadRendezvousFallback && thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] TryToConnect: Forcing rendezvous after direct attempts, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		}
	}
	// For receiver/passive side (RENDEZVOUS target), we may not have file context yet - it comes in HELLO after uTP connects.
	// Allow uTP if we have either explicit context OR we're in a NAT-T scenario (override flag set).
	const bool bHasFileContextOrNatT = HasNatTraversalFileContext() || bUseUTP;
	bool bUTPPossible = thePrefs.IsEnableNatTraversal()
		&& !bBuddyHandshake
		&& bHasFileContextOrNatT
		&& (m_strHelloInfo.IsEmpty() || GetNatTraversalSupport())
		&& thePrefs.GetUDPPort() != 0 && GetKadPort() != 0
		&& !GetConnectIP().IsNull() && theApp.IsFirewalled();
	if (bUTPPossible && m_bNatTFatalConnect) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLogWarning(_T("[NatTraversal] TryToConnect: Disabling uTP direct path due to previous fatal connect failure, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		bUTPPossible = false;
	}
    // Explicit uTP override (e.g. after RENDEZVOUS): allow direct uTP even if the above "possible" check is not yet true (e.g. hello not exchanged).
    // Do not require our side to be firewalled here; in rendezvous the public side should also initiate uTP towards the NATed peer.
	bool bUseUtpOverride = bUseUTP && thePrefs.IsEnableNatTraversal() && !bBuddyHandshake && GetKadPort() != 0 && !GetConnectIP().IsNull();
	if (bUseUtpOverride && m_bNatTFatalConnect) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLogWarning(_T("[NatTraversal] TryToConnect: Skipping direct uTP override due to previous fatal connect failure, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		bUseUtpOverride = false;
	}
	if (thePrefs.GetLogNatTraversalEvents() && (bUseUTP || bUTPPossible)) {
		CString connectStr = ipstr(GetConnectIP());
		CString ctxStr = DbgGetNatTraversalContext();
		DebugLog(_T("[NatTraversal] TryToConnect gating: override=%d possible=%d buddyHS=%d kadPort=%u udpPort=%u connectIP=%s context=%s"),
		(int)bUseUtpOverride, (int)bUTPPossible, (int)bBuddyHandshake, (UINT)GetKadPort(), (UINT)GetUDPPort(),
		(LPCTSTR)connectStr, ctxStr.IsEmpty() ? _T("<none>") : (LPCTSTR)ctxStr);
	}
	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NATTTESTMODE: UTPCheck] buddyHS=%d useUTP=%d helloEmpty=%d natSupp=%d myUDP=%u theirKad=%u connectIP=%s firewalled=%d reqfile=%p kadState=%s => utp=%d override=%d\n"),
		(int)bBuddyHandshake, (int)bUseUTP, (int)m_strHelloInfo.IsEmpty(), (int)GetNatTraversalSupport(), (unsigned)thePrefs.GetUDPPort(), (unsigned)GetKadPort(), (LPCTSTR)ipstr(GetConnectIP()), (int)theApp.IsFirewalled(), m_reqfile, DbgGetKadState(), (int)bUTPPossible, (int)bUseUtpOverride);
	const bool bKadConnected = Kademlia::CKademlia::IsConnected();
	const bool bKadFirewalled = bKadConnected && Kademlia::CKademlia::IsFirewalled();
	const bool bHasServerHighID = theApp.serverconnect->IsConnected() && !theApp.serverconnect->IsLowID();
	const bool bLocalLow = theApp.IsFirewalled();
	const bool bRemoteLow = HasLowID();
	const bool bLocalNeedsRendezvous = (bLocalLow && bRemoteLow);
	const bool bRendezvousPossible = HasLowID() && HasValidServingBuddyID() && bKadConnected && HasNatTraversalFileContext() && !bUseUtpOverride;
	// LowID ↔ LowID MUST use rendezvous; grace period only applies when at least one side is public.
	const bool bPreferRendezvous = bRendezvousPossible && (bLocalNeedsRendezvous || (m_bAllowRendezvousAfterCallback && !bRemoteLow));
	if (bPreferRendezvous) {
		if (bLocalNeedsRendezvous) {
			if (thePrefs.GetLogNatTraversalEvents())
				AddDebugLogLine(DLP_LOW, false, _T("[NAT-T] TryToConnect: Decision=Rendezvous (LowID↔LowID). %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		} else {
			if (thePrefs.GetLogNatTraversalEvents())
				AddDebugLogLine(DLP_LOW, false, _T("[NAT-T] TryToConnect: Decision=Rendezvous fallback after callback. %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		}
		m_dwDirectCallbackAttemptTick = 0;
	} else if (HasLowID() && HasValidServingBuddyID() && !bKadConnected && thePrefs.GetLogNatTraversalEvents()) {
		// Informative trace to clarify why rendezvous is not selected even though serving buddy data is known.
		DebugLog(_T("[NatTraversal] TryToConnect: Rendezvous skipped (Kad not connected); falling back to direct path checks, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
	} else if (HasLowID() && HasValidServingBuddyID() && !m_bAllowRendezvousAfterCallback && thePrefs.GetLogNatTraversalEvents()) {
		DebugLog(_T("[NatTraversal] TryToConnect: Rendezvous deferred; waiting for direct path grace period, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
	}
	// Direct TCP/uTP connection: Buddy handshakes always use direct TCP.
	// Otherwise, direct is allowed only for HighID or special cases (IPv6, KS_CONNECTING_FWCHECK, uTP override after rendezvous).
	if (!bPreferRendezvous && (bBuddyHandshake || bUseUtpOverride || bUseIPv6 || !HasLowID() || GetKadState() == KS_CONNECTING_FWCHECK)) {
		if (!m_bAllowRendezvousAfterCallback && m_dwDirectCallbackAttemptTick == 0)
			m_dwDirectCallbackAttemptTick = curTick;
		m_eConnectingState = CCS_DIRECTTCP;
		if (pClassSocket == NULL)
			pClassSocket = RUNTIME_CLASS(CClientReqSocket);
		socket = static_cast<CClientReqSocket*>(pClassSocket->CreateObject());
		socket->SetClient(this);
		// Create socket FIRST so its state is correctly initialized before adding uTP layer
		if (!socket->Create()) {
			DWORD dwLastError = ::WSAGetLastError();
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NATTTESTMODE: TryToConnect] socket Create failed: %s\n"), (LPCTSTR)DbgGetClientInfo());
			socket->Safe_Delete();
			// we let the timeout handle the cleanup in this case
			if (bUTPPossible && thePrefs.GetLogNatTraversalEvents())
				DebugLogError(_T("[NatTraversal] TryToConnect: Failed to create socket for outgoing connection, err=%lu, %s"), dwLastError, (LPCTSTR)EscPercent(DbgGetClientInfo()));
			else
				DebugLogError(_T("TryToConnect: Failed to create socket for outgoing connection, err=%lu, %s"), dwLastError, (LPCTSTR)EscPercent(DbgGetClientInfo()));
			AddDebugLogLine(DLP_LOW, false, _T("[NAT-T] TryToConnect: socket Create failed err=%lu for %s"), dwLastError, (LPCTSTR)EscPercent(DbgGetClientInfo()));
			return true;
		}
		// NOW initialize uTP support AFTER Create() so layer state is correctly synchronized
		if (bUseUtpOverride || bUTPPossible) {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NATTTESTMODE: TryToConnect] Before InitUtpSupport: socket=%p, %s\n"), socket, (LPCTSTR)DbgGetClientInfo());
			socket->InitUtpSupport();
			bool bHaveLayer = socket->HaveUtpLayer();
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NATTTESTMODE: TryToConnect] After InitUtpSupport: haveLayer=%d socket=%p, %s\n"), (int)bHaveLayer, socket, (LPCTSTR)DbgGetClientInfo());
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] TryToConnect: UTP support initialized (have=%d) for the socket, %s"), (int)bHaveLayer, (LPCTSTR)EscPercent(DbgGetClientInfo()));
			if ((bUseUtpOverride || bUTPPossible) && !bHaveLayer) {
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NATTTESTMODE: TryToConnect] WARNING: UTP layer NOT created! socket=%p, %s\n"), socket, (LPCTSTR)DbgGetClientInfo());
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLogWarning(_T("[NatTraversal] TryToConnect: UTP layer not attached after InitUtpSupport, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			}
			ResetUtpFlowControl();
			ClearUtpQueuedPackets();
			if (socket->HaveUtpLayer()) {
				SetUtpLocalInitiator(true);
				SetUtpConnectionStartTick(::GetTickCount());
			}
			if (thePrefs.GetLogNatTraversalEvents())
				AddDebugLogLine(DLP_LOW, false, _T("[NAT-T] TryToConnect: Decision=Direct uTP/TCP (no rendezvous). %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		}
		Connect();
		return true;
	}
	////////////////////////////////////////////////////////////
	// 4) Direct Callback Connections
	// Direct UDP Callback is designed for LowID <-> LowID connections only!
	// Both sides must be firewalled (receiver checks CKademlia::IsFirewalled()).
	// If remote is HighID, skip callback - they should connect to us or we use different mechanism.
	if (SupportsDirectUDPCallback() && thePrefs.GetUDPPort() != 0 && !GetConnectIP().IsNull() && HasLowID()) {
		// Set download state to CONNECTING before direct callback path (also handle DS_ONQUEUE)
		if (m_reqfile != NULL && GetDownloadState() != DS_DOWNLOADING && GetDownloadState() != DS_CONNECTED && GetDownloadState() != DS_CONNECTING)
			SetDownloadState(DS_CONNECTING);
		m_eConnectingState = CCS_DIRECTCALLBACK;
		CSafeMemFile data;
		data.WriteUInt16(thePrefs.GetPort()); // needs to know our port
		data.WriteHash16(thePrefs.GetUserHash()); // and userhash
		// our connection settings
		data.WriteUInt8(GetMyConnectOptions(true, false));
		if (thePrefs.GetDebugClientUDPLevel() > 0)
			DebugSend("OP_DIRECTCALLBACKREQ", this);
		Packet *packet = new Packet(data, OP_EMULEPROT);
		packet->opcode = OP_DIRECTCALLBACKREQ;
		theStats.AddUpDataOverheadOther(packet->size);
		theApp.clientudp->SendPacket(packet, GetConnectIP(), GetKadPort(), ShouldReceiveCryptUDPPackets(), GetUserHash(), false, 0);
		m_bAwaitingDirectCallback = true;
		m_bAllowRendezvousAfterCallback = false;
		m_dwDirectCallbackAttemptTick = curTick;
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] TryToConnect: Direct callback requested; awaiting response, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		return true;
	}
	////////////////////////////////////////////////////////////
	// 6) Server Callback + 7) Kad Callback
	if (GetDownloadState() == DS_CONNECTING)
		SetDownloadState(DS_WAITCALLBACK);

	if (GetUploadState() == US_CONNECTING) {
		ASSERT2(0); // We should never try to connect in this case, but wait for the LowID to connect to us
		DebugLogError(_T("LowID and US_CONNECTING (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
	}

	if (theApp.serverconnect->IsLocalServer(m_dwServerIP, m_nServerPort) && !theApp.IsFirewalled()) { // X-TODO: add eserver NAT-T support
		m_eConnectingState = CCS_SERVERCALLBACK;
		Packet* packet = new Packet(OP_CALLBACKREQUEST, 4);
		PokeUInt32(packet->pBuffer, m_nUserIDHybrid);
		if (thePrefs.GetDebugServerTCPLevel() > 0 || thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP_CallbackRequest", this);
		theStats.AddUpDataOverheadServer(packet->size);
		theApp.serverconnect->SendPacket(packet);
		return true;
	}

	////////////////////////////////////////////////////////////
	// 6b) eServer Buddy Relay - LowID-to-LowID via HighID buddy
	// Try eServer buddy relay when we are LowID and target is also LowID on same server
	// This provides an alternative to Kad callback - buddy uses server callback to reach target
	// NOTE: Target does NOT need to support eServer Buddy - our buddy will use server callback to reach them
	if (theApp.serverconnect->IsLowID() && HasLowID()) {
		CUpDownClient* pMyBuddy = theApp.clientlist->GetServingEServerBuddy();
		// Check if target is on the same server as our buddy (prerequisite for server callback)
		bool bTargetOnSameServer = (theApp.serverconnect->IsLocalServer(m_dwServerIP, m_nServerPort));
		
		if (thePrefs.GetLogNatTraversalEvents()) {
			AddDebugLogLine(false, _T("[eServerBuddy] TryToConnect: LowID-to-LowID check - MyBuddy=%p SameServer=%d TargetSrv=%s:%u"),
				pMyBuddy, bTargetOnSameServer ? 1 : 0, (LPCTSTR)ipstr(m_dwServerIP), m_nServerPort);
		}
		
		if (pMyBuddy && bTargetOnSameServer) {
			// Check socket validity - if invalid, reconnect to buddy
			if (!pMyBuddy->socket || !pMyBuddy->socket->IsConnected()) {
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] TryToConnect: Buddy socket invalid (socket=%p connected=%d), reconnecting to buddy for %s"),
						pMyBuddy->socket, pMyBuddy->socket ? pMyBuddy->socket->IsConnected() : 0,
						(LPCTSTR)EscPercent(pMyBuddy->DbgGetClientInfo()));
				}
				
				// Queue the relay request to send when buddy reconnects
				// Save the pending relay target on the buddy client
				pMyBuddy->SetPendingEServerRelayTarget(const_cast<CUpDownClient*>(this));
				
				// Initiate reconnection to buddy - relay will be sent in OnConnect
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] TryToConnect: Initiating reconnection to buddy %s for pending relay"),
						(LPCTSTR)EscPercent(pMyBuddy->DbgGetClientInfo()));
				}
				pMyBuddy->TryToConnect(true, true, NULL, true);
				
				m_eConnectingState = CCS_SERVERCALLBACK;  // Mark as waiting for callback
				return true;
			} else {
				// We have a connected eServer buddy and target is on the same server, try relay
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] TryToConnect: Attempting relay via buddy %s for target %s"),
						(LPCTSTR)EscPercent(pMyBuddy->DbgGetClientInfo()), (LPCTSTR)EscPercent(DbgGetClientInfo()));
				}
				if (SendEServerRelayRequest(const_cast<CUpDownClient*>(this))) {
					m_eConnectingState = CCS_SERVERCALLBACK;  // Reuse server callback state
					if (thePrefs.GetDebugClientTCPLevel() > 0)
						DebugLog(_T("TryToConnect: eServer Buddy relay requested for LowID target %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
					return true;
				} else {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] TryToConnect: Relay request failed for %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
					}
				}
			}
		}
	}

	if (HasValidServingBuddyID() && Kademlia::CKademlia::IsConnected()) { 
		if (!GetServingBuddyIP().IsNull() && GetServingBuddyPort() && m_reqfile != NULL) { // eMule AI: I've catched an exception inside below lines for a client with a null m_reqfile. Arranged this lines to fix this.
			if ((bLocalNeedsRendezvous || m_bAllowRendezvousAfterCallback) && thePrefs.IsEnableNatTraversal()) {
				// Set download state to CONNECTING before rendezvous flow (also handle DS_ONQUEUE)
				if (m_reqfile != NULL && GetDownloadState() != DS_DOWNLOADING && GetDownloadState() != DS_CONNECTED && GetDownloadState() != DS_CONNECTING)
					SetDownloadState(DS_CONNECTING);
				const bool bNeedLocalHolePunch = theApp.IsFirewalled();
				if (GetConnectIP().IsNull()) { // Ensure a valid connect endpoint for hole punch path.
					const CAddress user = GetIP();
					if (!user.IsNull() && user.IsPublicIP()) {
						SetConnectIP(user);
						if (thePrefs.GetLogNatTraversalEvents())
							DebugLog(_T("[NatTraversal] TryToConnect: ConnectIP was empty; set to UserIP=%s, %s"), (LPCTSTR)ipstr(user), (LPCTSTR)EscPercent(DbgGetClientInfo()));
					} else {
						if (thePrefs.GetLogNatTraversalEvents())
							DebugLog(_T("[NatTraversal] TryToConnect: WARNING - ConnectIP still NULL, UserIP=%s, cannot send Rendezvous; %s"), (LPCTSTR)ipstr(user), (LPCTSTR)EscPercent(DbgGetClientInfo()));
					}
				}

				if (GetConnectIP().IsNull() || GetKadPort() == 0) {
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal] TryToConnect: SKIP Rendezvous - ConnectIP=%s KadPort=%u; %s"), (LPCTSTR)ipstr(GetConnectIP()), GetKadPort(), (LPCTSTR)EscPercent(DbgGetClientInfo()));
				}
				if (!GetConnectIP().IsNull() && GetKadPort() != 0)
					theApp.clientudp->SeedNatTraversalExpectation(this, GetConnectIP(), GetKadPort());
				// Proactively send a small burst of hole punch packets towards the target's known Kad endpoint to open our NAT pinhole.
				// We still perform another hole punch from OP_RENDEZVOUS using the buddy-observed endpoint for symmetry.
				if (bNeedLocalHolePunch && !GetConnectIP().IsNull() && GetKadPort() != 0) {
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal] TryToConnect: Pre-Rendezvous hole punch to %s:%u for %s"), (LPCTSTR)ipstr(GetConnectIP()), GetKadPort(), (LPCTSTR)EscPercent(DbgGetClientInfo()));
					for (int i = 0; i < 6; ++i) {
						Packet* hp = new Packet(OP_EMULEPROT);
						hp->opcode = OP_HOLEPUNCH;
						theApp.clientudp->SendPacket(hp, GetConnectIP(), GetKadPort(), false, GetUserHash(), false, 0);
					}
					// Optional: try small port offsets to cope with symmetric NAT re-mapping
					uint16 base = GetKadPort();
					uint16 win = thePrefs.GetNatTraversalPortWindow();
					uint16 sweepWin = thePrefs.GetNatTraversalSweepWindow();
					uint16 span = std::min<uint16>(win, sweepWin);
					for (uint16 off = 1; off <= span; ++off) {
						if (base > off) {
							Packet* hp3 = new Packet(OP_EMULEPROT);
							hp3->opcode = OP_HOLEPUNCH;
							theApp.clientudp->SendPacket(hp3, GetConnectIP(), (uint16)(base - off), false, GetUserHash(), false, 0);
						}
						if (base + off <= 65535) {
							Packet* hp4 = new Packet(OP_EMULEPROT);
							hp4->opcode = OP_HOLEPUNCH;
							theApp.clientudp->SendPacket(hp4, GetConnectIP(), (uint16)(base + off), false, GetUserHash(), false, 0);
						}
			}
		}

		CSafeMemFile data(128);
		
		// NAT-T: Send TARGET's Served Buddy ID so the buddy can match against its served buddy list
		data.WriteHash16(GetServingBuddyID());
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NAT-T][OP_REASKCALLBACKUDP] Sending target's Served Buddy ID: %s"), (LPCTSTR)md4str(GetServingBuddyID()));

		uchar nullMarker[16];
		md4clr(nullMarker);
		data.WriteHash16(nullMarker); // null hash marks custom rendezvous payload

		data.WriteUInt8(OP_RENDEZVOUS);
				data.WriteHash16(thePrefs.GetUserHash());
				data.WriteUInt8(GetMyConnectOptions(true, true));

				bool bHasFileContext = (m_reqfile != NULL && !isnulmd4(m_reqfile->GetFileHash()));
				if (bHasFileContext) {
					data.WriteHash16(m_reqfile->GetFileHash());
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NATTTESTMODE: Rendezvous Request] Embedding file hash %s for file %s\n"), (LPCTSTR)md4str(m_reqfile->GetFileHash()), (LPCTSTR)EscPercent(m_reqfile->GetFileName()));
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal] TryToConnect: Embedding file hash in rendezvous payload for %s"), (LPCTSTR)EscPercent(m_reqfile->GetFileName()));
				} else {
					data.WriteHash16(nullMarker);
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NATTTESTMODE: Rendezvous Request] No file context available; embedding NULL hash placeholder\n"));
				}

				// Append REQUESTER's (our) public IPv4 + UDP/Kad port for buddy forwarding.
				// The buddy needs to know where to forward ephemeral OP_REASKACK (to us, not to target).
				// This is crucial for LowID-to-LowID NAT traversal: requester sends own public endpoint.
				uint32 requesterIP = theApp.GetPublicIPv4();
				if (requesterIP == 0)
					requesterIP = Kademlia::CKademlia::GetIPAddress();
				
				uint16 requesterPort = 0;
				if (Kademlia::CKademlia::GetPrefs()->GetUseExternKadPort())
					requesterPort = Kademlia::CKademlia::GetPrefs()->GetExternalKadPort();
				if (requesterPort == 0)
					requesterPort = Kademlia::CKademlia::GetPrefs()->GetInternKadPort();
				
				if (requesterIP != 0 && requesterPort != 0) {
					data.WriteUInt32(requesterIP);      // REQUESTER's (our) public IPv4 (host order)
					data.WriteUInt16(requesterPort);    // REQUESTER's (our) UDP/Kad port
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal] OP_ReaskCallbackUDP: Including REQUESTER endpoint %s:%u for ephemeral response"), (LPCTSTR)ipstr(requesterIP), requesterPort);
				}

				if (thePrefs.GetDebugClientUDPLevel() > 0)
					DebugSend("OP__ReaskCallbackUDP", this, bHasFileContext ? m_reqfile->GetFileHash() : nullMarker);
				Packet* response = new Packet(data, OP_EMULEPROT);
				response->opcode = OP_REASKCALLBACKUDP;
				theStats.AddUpDataOverheadFileRequest(response->size);
			theApp.downloadqueue->AddUDPFileReasks();
			
		
		// NAT-T: Send OP_REASKCALLBACKUDP to TARGET's buddy (not our own buddy!)
		// 'this' is the target/source client; GetServingBuddyIP/Port returns TARGET's buddy info
		CAddress targetBuddyIP = GetServingBuddyIP();
		uint16 targetBuddyPort = GetServingBuddyPort();
		
		if (targetBuddyIP.IsNull() || targetBuddyPort == 0) {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NAT-T][ERROR] OP_REASKCALLBACKUDP: Target buddy missing! IP=%s Port=%u for %s"), 
					(LPCTSTR)ipstr(targetBuddyIP), targetBuddyPort, (LPCTSTR)EscPercent(DbgGetClientInfo()));
			return false;
		}

		if (thePrefs.GetLogNatTraversalEvents()) {
			DebugLog(_T("[NAT-T][DEBUG] OP_REASKCALLBACKUDP: Sending to TARGET's buddy %s:%u for target %s"), 
				(LPCTSTR)ipstr(targetBuddyIP), targetBuddyPort, (LPCTSTR)EscPercent(DbgGetClientInfo()));
		}

		theApp.clientudp->SendPacket(response, targetBuddyIP, targetBuddyPort, false, NULL, false, 0);

				if (!GetConnectIP().IsNull() && GetKadPort() != 0 && m_reqfile != NULL) {
					theApp.clientudp->RegisterPendingCallback(GetConnectIP(), GetKadPort(), m_reqfile->GetFileHash());
				}

				// Send OP_HOLEPUNCH directly to receiver to open NAT hole for incoming REASKACK
				if (!GetConnectIP().IsNull() && GetKadPort() != 0) {
					Packet *holepunch = new Packet(OP_EMULEPROT);
					holepunch->opcode = OP_HOLEPUNCH;
					theApp.clientudp->SendPacket(holepunch, GetConnectIP(), GetKadPort(), false, NULL, false, 0);
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal][OP_REASKCALLBACKUDP][Requester] Sending OP_HOLEPUNCH to receiver %s:%u for NAT hole opening"), (LPCTSTR)ipstr(GetConnectIP()), GetKadPort());
				}
				
				// Proactively attempt uTP connect after requesting rendezvous to avoid relying on RENDEZVOUS ordering.
				// Reset connecting state first to avoid the early-return guard in TryToConnect.
				// Download state was already set to DS_CONNECTING earlier in this flow, so no need to set again.
				theApp.clientlist->RemoveConnectingClient(this);
				m_eConnectingState = CCS_NONE;
					if (thePrefs.GetLogNatTraversalEvents())
						AddDebugLogLine(DLP_LOW, false, _T("[NAT-T] TryToConnect: Requester scheduling direct uTP after rendezvous. %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
					// Mark a couple of short retries for requester too
					ClearEServerRelayNatTGuard();
					MarkNatTRendezvous(2);
					TryToConnect(true, false, NULL, true);
					return true;
			} else {
				CSafeMemFile bio(34);
				bio.WriteUInt128(Kademlia::CUInt128(GetServingBuddyID()));
				bio.WriteUInt128(Kademlia::CUInt128(m_reqfile->GetFileHash()));
				bio.WriteUInt16(thePrefs.GetPort());
				if (thePrefs.GetDebugClientKadUDPLevel() > 0 || thePrefs.GetDebugClientUDPLevel() > 0)
					DebugSend("KadCallbackReq", this);
				Packet *packet = new Packet(bio, OP_KADEMLIAHEADER);
				packet->opcode = KADEMLIA_CALLBACK_REQ;
				theStats.AddUpDataOverheadKad(packet->size);
			ClearEServerRelayNatTGuard();
			m_eConnectingState = CCS_KADCALLBACK;
		// FIXME: We don't know which kad version the serving buddy has, so we need to send unencrypted
		
		// NAT-T: Send to TARGET's buddy (not our own buddy!)
		CAddress targetBuddyIP = GetServingBuddyIP();
		uint16 targetBuddyPort = GetServingBuddyPort();
		if (targetBuddyIP.IsNull() || targetBuddyPort == 0) {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NAT-T][ERROR] KADEMLIA_CALLBACK_REQ: Target buddy missing!"));
			return false;
		}
			theApp.clientudp->SendPacket(packet, targetBuddyIP, targetBuddyPort, false, NULL, true, 0);
			SetDownloadState(DS_WAITCALLBACKKAD);
			}
		} else if (m_reqfile != NULL) { // We're connected to KAD, we've a serving buddy with unknown IP/Port, but reqfile is set
			// ServingBuddyPull: Try to directly obtain buddy info from target before giving up / falling back to lookup
			if (GetServingBuddyPort() == 0 && GetServingBuddyIP().IsNull()) {
				// If we are the serving buddy for this client, seed with our own endpoint instead of requesting.
				bool bWeServe = theApp.clientlist->IsServedBuddy(this);
				if (!bWeServe && HasValidServingBuddyID()) {
					Kademlia::CUInt128 kid(GetServingBuddyID());
					bWeServe = (theApp.clientlist->FindServedBuddyByKadID(kid) != NULL);
				}

				if (bWeServe) {
					CAddress myIP = theApp.GetPublicIP();

					if (!myIP.IsNull())
						SetServingBuddyIP(myIP);
					uint16 myKadPort = 0;
					if (Kademlia::CKademlia::GetPrefs()->GetUseExternKadPort() && Kademlia::CKademlia::GetPrefs()->GetExternalKadPort() != 0)
						myKadPort = Kademlia::CKademlia::GetPrefs()->GetExternalKadPort();
					else if (Kademlia::CKademlia::GetPrefs()->GetInternKadPort() != 0)
						myKadPort = Kademlia::CKademlia::GetPrefs()->GetInternKadPort();
					else
						myKadPort = thePrefs.GetUDPPort();

					SetServingBuddyPort(myKadPort);
				} else {
					RequestServingBuddyInfo();
				}
			}

			if (theApp.IsFirewalled() && !thePrefs.IsEnableNatTraversal()) {
				// Drop if kad callbacks are not possible and NAT-T is disabled
				if (Disconnected(_T("LowID->LowID"))) {
					delete this;
					return false;
				}
				return true;
			}
			// I don't think we should ever have a serving buddy without its IP (any more), but nevertheless let the functionality in CSearch try to find a serving buddy.
			Kademlia::CSearch *findSource = new Kademlia::CSearch;
			findSource->SetSearchType(Kademlia::CSearch::FINDSOURCE);
			findSource->SetTargetID(Kademlia::CUInt128(GetServingBuddyID()));
			findSource->AddFileID(Kademlia::CUInt128(m_reqfile->GetFileHash()));
			if (Kademlia::CKademlia::GetPrefs()->GetTotalSource() > 0 || Kademlia::CSearchManager::AlreadySearchingFor(Kademlia::CUInt128(GetServingBuddyID()))) {
				// There are too many source lookups or we are already searching this key.
				// bad luck, as lookups aren't supposed to hapen anyway, we just let it fail
				// if we really want to use lookups (so buddies without known IPs), this should be reworked
				// for example by adding a queue system for queries
				DebugLogWarning(_T("[Buddy]: TryToConnect: Serving buddy without known IP, Lookup currently impossible"));
				delete findSource;
				return true;
			}
				if (Kademlia::CSearchManager::StartSearch(findSource)) {
					ClearEServerRelayNatTGuard();
					m_eConnectingState = CCS_KADCALLBACK;
					//Started lookup.
					SetDownloadState(DS_WAITCALLBACKKAD);
				} else
				ASSERT(0); //This should never happen.
		}
	} else {
		ASSERT(0);
		DebugLogError(_T("TryToConnect: Bug: No Callback available despite prechecks"));
	}
	return true;
}

bool CUpDownClient::HasNatTraversalFileContext() const
{
	if (m_reqfile != NULL)
		return true;
	if (!isnulmd4(GetUploadFileID()))
		return true;
	return false;
}

CString CUpDownClient::DbgGetNatTraversalContext() const
{
	if (m_reqfile != NULL)
		return m_reqfile->GetFileName();
	if (!isnulmd4(GetUploadFileID())) {
		const CKnownFile* upFile = theApp.sharedfiles->GetFileByID(GetUploadFileID());
		if (upFile != NULL)
			return upFile->GetFileName();
		return CString(_T("<upload>"));
	}
	return CString();
}

void CUpDownClient::SetUtpWritable(bool writable)
{
	if (m_bUtpWritable != writable) {
	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NatTraversal][Client] SetUtpWritable: %d -> %d, %s"), m_bUtpWritable ? 1 : 0, writable ? 1 : 0, (LPCTSTR)EscPercent(DbgGetClientInfo()));
	}
	m_bUtpWritable = writable;
	if (m_bUtpWritable && m_bUtpHelloDeferred && !IsUtpHelloQueued()) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] SetUtpWritable: Dispatching deferred Hello for %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		m_uHelloResendCount = 0;
		QueueUtpHelloPacket();
	}
}

void CUpDownClient::SetUtpHelloQueued(bool queued)
{
	m_bUtpHelloQueued = queued;
}

void CUpDownClient::ResetUtpFlowControl()
{
	m_bUtpWritable = false;
	m_bUtpHelloQueued = false;
	m_dwUtpQueuedPacketsTime = 0;
	m_dwUtpConnectionStartTick = 0;
	m_bUtpLocalInitiator = false;
	m_bUtpNeedsQueuePurge = false;
	m_bUtpInboundActivity = false;
	m_bUtpHelloDeferred = false;
	m_bUtpProactiveHelloSent = false;
	m_dwHelloLastSentTick = 0;
	m_uHelloResendCount = 0;
	m_dwUtpLastInboundTick = 0;
	m_uUtpLastFrameType = UTP_FRAMETYPE_UNKNOWN;
}

void CUpDownClient::QueueUtpHelloPacket()
{
	CSafeMemFile data(128);
	data.WriteUInt8(16);
	SendHelloTypePacket(data);
	Packet *packet = new Packet(data);
	packet->opcode = OP_HELLO;
	m_bHelloAnswerPending = true;
	m_dwHelloLastSentTick = ::GetTickCount();
	m_bUtpHelloDeferred = false;

	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_Hello (queued)", this);

	if (socket && socket->HaveUtpLayer() && socket->IsConnected()) {
		SetUtpHelloQueued(false);
		m_dwUtpQueuedPacketsTime = 0;
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] QueueUtpHelloPacket: Sending Hello immediately on connected uTP socket, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		SendPacket(packet, true);
		if (theApp.clientudp)
			theApp.clientudp->PumpUtpOnce();
		return;
	}

	SetUtpHelloQueued(true);
	m_WaitingPackets_list.AddTail(packet);
	m_dwUtpQueuedPacketsTime = ::GetTickCount();
	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NatTraversal] QueueUtpHelloPacket: Queued Hello for %s (writable=%d)"), (LPCTSTR)EscPercent(DbgGetClientInfo()), IsUtpWritable() ? 1 : 0);
}

void CUpDownClient::ClearUtpQueuedPackets()
{
	while (!m_WaitingPackets_list.IsEmpty())
		delete m_WaitingPackets_list.RemoveHead();
	m_dwUtpQueuedPacketsTime = 0;
	m_bUtpHelloQueued = false;
	m_bUtpNeedsQueuePurge = false;
	m_bUtpInboundActivity = false;
	m_bUtpHelloDeferred = false;
	m_dwUtpLastInboundTick = 0;
	m_uUtpLastFrameType = UTP_FRAMETYPE_UNKNOWN;
}

void CUpDownClient::RegisterUtpInboundActivity(EUtpFrameType frameType)
{
	m_bUtpInboundActivity = true;
	m_dwUtpLastInboundTick = ::GetTickCount();
	m_uUtpLastFrameType = frameType;
	if (!IsUtpWritable()) {
		if (frameType == UTP_FRAMETYPE_STATE || frameType == UTP_FRAMETYPE_DATA || frameType == UTP_FRAMETYPE_FIN)
			SetUtpWritable(true);
	}
}

void CUpDownClient::ResendHelloIfTimeout()
{
	// Resend OP_HELLO if we still wait for OP_HELLOANSWER for too long on uTP
	if (!socket || !socket->HaveUtpLayer())
		return;
	if (!socket->IsConnected())
		return;
	if (!IsHelloAnswerPending())
		return;
	DWORD now = ::GetTickCount();
	const DWORD HELLO_RESEND_MS = 2500; // conservative; avoid spam
	const uint8 MAX_HELLO_RESENDS = 2; // do at most 2 resends per connection
	if (m_dwHelloLastSentTick == 0)
		return;
	if (m_uHelloResendCount >= MAX_HELLO_RESENDS)
		return;
	if ((int)(now - m_dwHelloLastSentTick) < (int)HELLO_RESEND_MS)
		return;
	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NatTraversal] Resending OP_HELLO (queued) due to pending HelloAnswer (resend=%u), %s"), (unsigned)m_uHelloResendCount + 1U, (LPCTSTR)EscPercent(DbgGetClientInfo()));
	// Queue a fresh OP_HELLO like in ConnectionEstablished (avoid immediate SendPacket on uTP)
	if (m_bUtpHelloDeferred && !IsUtpHelloQueued()) {
		if (IsUtpWritable()) {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] Resend: deferred Hello now queued (socket writable), %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			QueueUtpHelloPacket();
		} else {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] Resend: Hello still deferred (socket not writable), %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			m_dwHelloLastSentTick = now;
			m_uHelloResendCount++;
			return;
		}
	} else if (!IsUtpHelloQueued()) {
		QueueUtpHelloPacket();
	}
	// Update timers and counters
	m_dwHelloLastSentTick = now;
	m_uHelloResendCount++;
}

void CUpDownClient::MarkNatTRendezvous(uint8 retries, bool bForceImmediate)
{
	if (m_bNatTFatalConnect)
		return;
	// Initialize NAT-T retry schedule
	if (retries == 0)
		return;
	if (m_uNattRetryLeft > 0) {
		if (retries > m_uNattRetryLeft)
			m_uNattRetryLeft = retries;
		if (bForceImmediate)
			m_dwNattNextRetryTick = ::GetTickCount();
		return;
	}
	m_uNattRetryLeft = retries;
	DWORD now = ::GetTickCount();
	DWORD jmin = thePrefs.GetNatTraversalJitterMinMs();
	DWORD jmax = thePrefs.GetNatTraversalJitterMaxMs();
	if (jmax < jmin) jmax = jmin;
	DWORD range = (jmax > jmin) ? (jmax - jmin) : 1u;
	DWORD jitter = bForceImmediate ? 0 : (jmin + (now % (range + 1u)));
	m_dwNattNextRetryTick = now + (jitter == 0 ? 1 : jitter);
}

void CUpDownClient::ScheduleNextNatTRetry(DWORD now, DWORD jmin, DWORD jmax)
{
	if (m_uNattRetryLeft == 0)
		return;
	if (jmax < jmin) jmax = jmin;
	DWORD range = (jmax > jmin) ? (jmax - jmin) : 1u;
	DWORD jitter;
	if (m_bDeferredNatConnect)
		jitter = 0;
	else {
		DWORD seed = now ^ (DWORD)(uintptr_t)this;
		jitter = jmin + (seed % (range + 1u));
	}
	m_dwNattNextRetryTick = now + (jitter == 0 ? 1 : jitter);
}

void CUpDownClient::DoNatTRetry()
{
	if (m_bNatTFatalConnect || m_uNattRetryLeft == 0)
		return;
	bool bHandshakeRetryTimeout = false;
	// Once uTP is connected, do not keep immediate deferred retry cadence.
	// Immediate retries are useful while opening NAT pinholes, but harmful after connect/adoption.
	if (socket != NULL && socket->HaveUtpLayer() && socket->IsConnected() && m_bDeferredNatConnect) {
		m_bDeferredNatConnect = false;
		m_DeferredNatIP = CAddress();
		m_uDeferredNatPort = 0;
		m_uDeferredNatPortWindow = 0;
	}

	// Skip retries while handshake is still in progress.
	// Reconnecting too early can close a valid adopted socket before Hello/HelloAnswer completes.
	// If handshake stalls beyond a grace window, retries are re-enabled to recover.
	if (socket != NULL && socket->HaveUtpLayer() && socket->IsConnected() && !CheckHandshakeFinished()) {
		const DWORD now = ::GetTickCount();
		const DWORD kHandshakeRetryGraceMs = 6000;
		if (m_dwHelloLastSentTick == 0 || (int)(now - m_dwHelloLastSentTick) < (int)kHandshakeRetryGraceMs) {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] DoNatTRetry: Skip retry, handshake still pending on connected uTP, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			return;
		}
		bHandshakeRetryTimeout = true;
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLogWarning(_T("[NatTraversal] DoNatTRetry: Handshake pending beyond grace window, retrying connect, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
	}

	// Skip retries only when the uTP path is both connected and writable.
	if (socket != NULL && socket->HaveUtpLayer() && socket->IsConnected()) {
		if (!bHandshakeRetryTimeout && IsUtpWritable()) {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] DoNatTRetry: Skip retry, uTP is connected+writable, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			m_uNattRetryLeft = 0;
			ClearEServerRelayNatTGuard();
			return;
		}
		if (thePrefs.GetLogNatTraversalEvents() && !bHandshakeRetryTimeout)
			DebugLog(_T("[NatTraversal] DoNatTRetry: Keep retry, uTP connected but not writable yet, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
	}
	// Send an extra hole punch burst + retry uTP connect
	CAddress tip = (m_bDeferredNatConnect && !m_DeferredNatIP.IsNull()) ? m_DeferredNatIP : (GetConnectIP().IsNull() ? GetIP() : GetConnectIP());
	uint16 tport = (m_bDeferredNatConnect && m_uDeferredNatPort != 0) ? m_uDeferredNatPort : (GetKadPort() ? GetKadPort() : GetUDPPort());
		if (!tip.IsNull() && tport != 0) {
			for (int i = 0; i < 4; ++i) {
				Packet* hp = new Packet(OP_EMULEPROT);
				hp->opcode = OP_HOLEPUNCH;
				theApp.clientudp->SendPacket(hp, tip, tport, false, GetUserHash(), false, 0);
			}
			const bool bUseExtendedSweep = (m_bDeferredNatConnect && m_uDeferredNatPortWindow != 0);
			uint16 requestedSpan = bUseExtendedSweep ? m_uDeferredNatPortWindow : thePrefs.GetNatTraversalPortWindow();
			uint16 sweepWin = thePrefs.GetNatTraversalSweepWindow();
			uint16 span = std::min<uint16>(requestedSpan, sweepWin);
			for (uint16 off = 1; off <= span; ++off) {
				if (tport > off) {
					Packet* hp3 = new Packet(OP_EMULEPROT);
					hp3->opcode = OP_HOLEPUNCH;
					theApp.clientudp->SendPacket(hp3, tip, (uint16)(tport - off), false, GetUserHash(), false, 0);
			}
			if (tport + off <= 65535) {
				Packet* hp4 = new Packet(OP_EMULEPROT);
				hp4->opcode = OP_HOLEPUNCH;
				theApp.clientudp->SendPacket(hp4, tip, (uint16)(tport + off), false, GetUserHash(), false, 0);
			}
		}
		}
		// After a transient uTP failure in guarded eServer relay flow, refresh relay first.
		if (IsEServerRelayNatTGuardActive() && m_uEServerRelayTransientErrors > 0
			&& theApp.serverconnect->IsLowID() && HasLowID()
			&& theApp.serverconnect->IsLocalServer(m_dwServerIP, m_nServerPort)) {
			CUpDownClient* pMyBuddy = theApp.clientlist->GetServingEServerBuddy();
			if (pMyBuddy != NULL && pMyBuddy->socket != NULL && pMyBuddy->socket->IsConnected()) {
				if (SendEServerRelayRequest(this)) {
					m_eConnectingState = CCS_SERVERCALLBACK;
					if (m_uNattRetryLeft)
						--m_uNattRetryLeft;
					return;
				}
			}
		}
			// Retry connect path. For LowID<->LowID + serving-buddy cases, do not force uTP override here,
			// because override suppresses rendezvous selection and can trap the flow in direct retry loops.
			if (!m_bNatTFatalConnect) {
				// Guard against stale direct-connect states after transient failures with no live socket.
				const EConnectingState eConnState = GetConnectingState();
				const bool bStaleDirectConnectState = (eConnState == CCS_DIRECTTCP || eConnState == CCS_PRECONDITIONS);
				if (bStaleDirectConnectState && socket == NULL) {
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLogWarning(_T("[NatTraversal] DoNatTRetry: Clearing stale connecting state before retry, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
					ResetConnectingState();
					theApp.clientlist->RemoveConnectingClient(this);
				}
				const bool bLowToLowRendezvousRetry =
					theApp.IsFirewalled()
					&& HasLowID()
					&& HasValidServingBuddyID()
					&& HasNatTraversalFileContext()
					&& Kademlia::CKademlia::IsConnected();
				const bool bRetryWithUtpOverride = !bLowToLowRendezvousRetry;
				if (bLowToLowRendezvousRetry && thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal] DoNatTRetry: lowID<->lowID retry uses normal connect path (override disabled) to allow rendezvous, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
				SetLastTriedToConnectTime();
				TryToConnect(true, false, NULL, bRetryWithUtpOverride);
			}
	if (m_uNattRetryLeft)
		--m_uNattRetryLeft;
}

void CUpDownClient::QueueDeferredNatConnect(const CAddress& ip, uint16 port, uint16 window)
{
	if (port == 0 || ip.IsNull())
		return;
	if (m_bNatTFatalConnect)
		return;
	uint8 retries = (m_uNattRetryLeft > 0) ? m_uNattRetryLeft : (ShouldAllowNatTRetryReseed() ? 1 : 0);
	if (retries == 0)
		return;
	m_bDeferredNatConnect = true;
	m_DeferredNatIP = ip;
	m_uDeferredNatPort = port;
	m_uDeferredNatPortWindow = window;
	MarkNatTRendezvous(retries, true);
}

void CUpDownClient::FlagNatTFatalConnectFailure()
{
	m_bNatTFatalConnect = true;
	m_uNattRetryLeft = 0;
	m_bDeferredNatConnect = false;
	m_DeferredNatIP = CAddress();
	m_uDeferredNatPort = 0;
	m_uDeferredNatPortWindow = 0;
	m_dwNattNextRetryTick = 0;
	ClearEServerRelayNatTGuard();
}

void CUpDownClient::ClearNatTFatalConnectFailure()
{
	m_bNatTFatalConnect = false;
}

void CUpDownClient::ArmEServerRelayNatTGuard()
{
	DWORD now = ::GetTickCount();
	if (!m_bEServerRelayNatTGuardActive || (int)(now - m_dwEServerRelayNatTWindowStart) > (int)ESERVER_RELAY_NATT_TRANSIENT_WINDOW_MS) {
		m_dwEServerRelayNatTWindowStart = now;
		m_uEServerRelayTransientErrors = 0;
	}
	m_bEServerRelayNatTGuardActive = true;
}

void CUpDownClient::ClearEServerRelayNatTGuard()
{
	m_bEServerRelayNatTGuardActive = false;
	m_dwEServerRelayNatTWindowStart = 0;
	m_uEServerRelayTransientErrors = 0;
}

void CUpDownClient::NormalizeEServerRelayNatTGuard()
{
	if (!m_bEServerRelayNatTGuardActive)
		return;
	EDownloadState eDlState = GetDownloadState();
	// Guard context is stale when there is no retry budget, no deferred endpoint,
	// no pending hello handshake and no active connect state.
	if (m_uNattRetryLeft != 0 || m_bDeferredNatConnect || m_bHelloAnswerPending || m_eConnectingState != CCS_NONE
		|| eDlState == DS_CONNECTING || eDlState == DS_WAITCALLBACK || eDlState == DS_WAITCALLBACKKAD)
		return;
	ClearEServerRelayNatTGuard();
}

bool CUpDownClient::RegisterEServerRelayTransientError()
{
	if (!m_bEServerRelayNatTGuardActive)
		return true;

	DWORD now = ::GetTickCount();
	if ((int)(now - m_dwEServerRelayNatTWindowStart) > (int)ESERVER_RELAY_NATT_TRANSIENT_WINDOW_MS) {
		m_dwEServerRelayNatTWindowStart = now;
		m_uEServerRelayTransientErrors = 0;
	}

	if (m_uEServerRelayTransientErrors < 0xFF)
		++m_uEServerRelayTransientErrors;

	if (thePrefs.GetLogNatTraversalEvents()) {
		DebugLogWarning(_T("[NatTraversal][eServerRelay] transient uTP error %u/%u in %lu ms window, %s"),
			(UINT)m_uEServerRelayTransientErrors,
			(UINT)ESERVER_RELAY_NATT_TRANSIENT_ERROR_LIMIT,
			(DWORD)ESERVER_RELAY_NATT_TRANSIENT_WINDOW_MS,
			(LPCTSTR)EscPercent(DbgGetClientInfo()));
	}

	if (m_uEServerRelayTransientErrors >= ESERVER_RELAY_NATT_TRANSIENT_ERROR_LIMIT) {
		AbortEServerRelayNatTraversal(_T("Transient uTP error cap reached"));
		return false;
	}

	return true;
}

void CUpDownClient::AbortEServerRelayNatTraversal(LPCTSTR pszReason)
{
	ResetUtpFlowControl();
	ClearUtpQueuedPackets();
	m_bHelloAnswerPending = false;
	m_uNattRetryLeft = 0;
	m_dwNattNextRetryTick = 0;
	m_bDeferredNatConnect = false;
	m_DeferredNatIP = CAddress();
	m_uDeferredNatPort = 0;
	m_uDeferredNatPortWindow = 0;

	if (GetConnectingState() != CCS_NONE) {
		ResetConnectingState();
		theApp.clientlist->RemoveConnectingClient(this);
	}

	EDownloadState eDlState = GetDownloadState();
	bool bDropSourceAfterRetryCap = false;
	if (eDlState == DS_CONNECTING || eDlState == DS_WAITCALLBACK || eDlState == DS_WAITCALLBACKKAD) {
		// No queue confirmation was received, so forcing DS_ONQUEUE here is misleading.
		// Reset queue-tracking flags and fall back to DS_NONE.
		m_fQueueRankPending = 0;
		m_fUnaskQueueRankRecv = 0;
		SetRemoteQueueFull(false);
		SetRemoteQueueRank(0);
		SetDownloadState(DS_NONE, _T("eServer relay NAT-T retry cap reached without queue confirmation"));
		if (m_uEServerRelayTransientErrors >= ESERVER_RELAY_NATT_TRANSIENT_ERROR_LIMIT)
			bDropSourceAfterRetryCap = true;
	}

	if (thePrefs.GetLogNatTraversalEvents()) {
		if (pszReason != NULL && pszReason[0] != _T('\0'))
			DebugLogWarning(_T("[NatTraversal][eServerRelay] Aborting relay retry loop: %s, %s"), pszReason, (LPCTSTR)EscPercent(DbgGetClientInfo()));
		else
			DebugLogWarning(_T("[NatTraversal][eServerRelay] Aborting relay retry loop, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
	}

	if (bDropSourceAfterRetryCap && theApp.downloadqueue != NULL) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLogWarning(_T("[NatTraversal][eServerRelay] Removing source after retry cap (no queue confirmation), %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		theApp.downloadqueue->RemoveSource(this);
	}

	// Keep the guard active when transient-error cap is reached, so relay retries
	// stay blocked within the same protection window.
	if (m_bEServerRelayNatTGuardActive && m_uEServerRelayTransientErrors >= ESERVER_RELAY_NATT_TRANSIENT_ERROR_LIMIT) {
		if (m_dwEServerRelayNatTWindowStart == 0)
			m_dwEServerRelayNatTWindowStart = ::GetTickCount();
		if (m_uEServerRelayTransientErrors < ESERVER_RELAY_NATT_TRANSIENT_ERROR_LIMIT)
			m_uEServerRelayTransientErrors = ESERVER_RELAY_NATT_TRANSIENT_ERROR_LIMIT;
	} else {
		ClearEServerRelayNatTGuard();
	}
}

void CUpDownClient::Connect()
{
	// enable or disable encryption based on our and the remote clients preference
	// NOTE: For uTP connections, encryption state is set but negotiation is bypassed in SendStd()
	// because uTP uses UDP transport and has its own encryption at protocol level
	if (HasValidHash() && SupportsCryptLayer() && thePrefs.IsCryptLayerEnabled() && (RequestsCryptLayer() || thePrefs.IsCryptLayerPreferred())) {
		socket->SetConnectionEncryption(true, GetUserHash(), false);
	} else {
		socket->SetConnectionEncryption(false, NULL, false);
	}

	//Try to always tell the socket to WaitForOnConnect before you call Connect.
	socket->WaitForOnConnect();
	SOCKADDR_IN6 sockAddr = { 0 };
	int nSockAddrLen = sizeof(sockAddr);
	CAddress IP = GetConnectIP();
	// Safety: ensure we have a valid connect endpoint for the chosen transport.
	// For uTP, prefer the remote's public UserIP if ConnectIP is empty; fallback to last callback requester if available.
	if (socket->HaveUtpLayer()) {
		if (IP.IsNull() || !IP.IsPublicIP()) {
			const CAddress &user = GetIP();
			if (!user.IsNull() && user.IsPublicIP()) {
				IP = user;
				if (GetConnectIP().IsNull())
					SetConnectIP(IP);
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal] Connect: Using fallback UserIP=%s for uTP, %s"), (LPCTSTR)ipstr(IP), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			} else if (!GetLastCallbackRequesterIP().IsNull() && GetLastCallbackRequesterIP().IsPublicIP()) {
				IP = GetLastCallbackRequesterIP();
				if (GetConnectIP().IsNull())
					SetConnectIP(IP);
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal] Connect: Using fallback LastCallbackRequesterIP=%s for uTP, %s"), (LPCTSTR)ipstr(IP), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			}
		}
	}
	IP.Convert(CAddress::IPv6); // the socket works with IPv6 adresses only
	if (socket->HaveUtpLayer())
		IP.ToSA((SOCKADDR*)&sockAddr, &nSockAddrLen, GetKadPort() ? GetKadPort() : GetUDPPort());
	else
		IP.ToSA((SOCKADDR*)&sockAddr, &nSockAddrLen, GetUserPort());
	if (socket->Connect((SOCKADDR*)&sockAddr, sizeof sockAddr)) {
		if (socket->HaveUtpLayer() && thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] Connect: UTP socket is connected, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
	} else if (socket->HaveUtpLayer()) {
		DWORD wsaErr = ::WSAGetLastError();
		if (wsaErr == WSAEWOULDBLOCK || wsaErr == WSAEINPROGRESS || wsaErr == WSAEALREADY) {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] Connect: UTP connect pending (err=%lu), %s"), wsaErr, (LPCTSTR)EscPercent(DbgGetClientInfo()));
		} else {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLogError(_T("[NatTraversal] Connect: UTP socket connection attempt has failed (err=%lu), %s"), wsaErr, (LPCTSTR)EscPercent(DbgGetClientInfo()));
			socket->Safe_Delete();
			socket = NULL;
			// Keep current connect state/list registration so global connect-timeout lifecycle
			// can finalize this source through the standard Disconnected() path.
			return;
		}
	}
	// Schedule deferred hello packet for uTP connections to allow NAT hole establishment
	if (socket->HaveUtpLayer()) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] Connect: Scheduling deferred Hello for uTP handshake, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		// Give uTP some time for handshake before sending eMule Hello
		// OnConnect callback will trigger SendHelloPacket when connection is established
		
		// Set connection timeout for uTP handshake (fallback to direct Hello after delay)
		m_dwUtpConnectionStartTick = ::GetTickCount();
	} else {
		SendHelloPacket();
	}
}

void CUpDownClient::ConnectionEstablished()
{
	m_cFailed = 0; // holds the failed connection attempts
	ClearNatTFatalConnectFailure();
	ClearEServerRelayNatTGuard();

	// Try to restore reqfile from pending relay contexts (eServer Buddy / Rendezvous)
	// Handles cases where client object is recreated or merged during uTP connection
	if (m_reqfile == NULL) {
		theApp.clientlist->ApplyPendingRelayContext(this);
	}

	if (m_bUDPPending && !IsRemoteQueueFull()) { // Maybe the client has now place at its queue... Then it's right to not answer an UDPFilereaskping
		m_nFailedUDPPackets++;
		theApp.downloadqueue->AddFailedUDPFileReasks();
	}
	m_bUDPPending = false;

	// OK we have a connection, lets see if we want anything from this client
	// Keep direct-path fallback window alive while uTP hello handshake is still pending.
	// Otherwise repeated transient connects can keep resetting the timer and block rendezvous forever.
	const bool bPreserveDirectFallbackWindow =
		(socket != NULL && socket->HaveUtpLayer() && socket->IsConnected() && !CheckHandshakeFinished());

	m_eConnectingState = CCS_NONE;
	theApp.clientlist->RemoveConnectingClient(this);
	if (!bPreserveDirectFallbackWindow) {
		m_bAwaitingDirectCallback = false;
		m_bAllowRendezvousAfterCallback = false;
		m_dwDirectCallbackAttemptTick = 0;
	}

	// Reset SecureIdent state before sending Hello on uTP connections
	// This ensures fresh SecIdent exchange for every new connection, preventing "Non SUI eMule" penalty
	// When buddy handshake and download connection reuse same CUpDownClient object,
	// stale m_dwLastSignatureIP causes SendSecIdentStatePacket() to skip with "Already identified"
	if (socket && socket->HaveUtpLayer())
		ResetSecureIdentState();

	// For uTP connections, always queue Hello packet instead of sending immediately.
	// uTP socket may report IsConnected()=true but internal handshake may not be complete,
	// causing WSASend 10057 error. Queue it and let OnSend() handle when socket is truly writable.
	if (socket && socket->HaveUtpLayer()) {
		if (socket->IsConnected()) {
			if (m_dwHelloLastSentTick == 0) {
				if (IsUtpLocalInitiator()) {
					if (!IsUtpHelloQueued()) {
						if (thePrefs.GetLogNatTraversalEvents())
							DebugLog(_T("[NatTraversal] ConnectionEstablished: uTP socket connected, queuing Hello for ServiceUtpQueuedPackets(), %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
						m_uHelloResendCount = 0;
						QueueUtpHelloPacket();
					} else if (thePrefs.GetLogNatTraversalEvents()) {
						DebugLog(_T("[NatTraversal] ConnectionEstablished: Skipping duplicate uTP Hello queue, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
					}
				} else {
					// Inbound uTP: After UTP_STATE_CONNECT, socket is implicitly writable per libutp behavior.
					// Send proactive Hello to initiate bidirectional handshake immediately.
					// Only send ONCE to prevent infinite hello loop with peer
					if (!IsUtpProactiveHelloSent() && !IsUtpHelloQueued()) {
						if (thePrefs.GetLogNatTraversalEvents())
							DebugLog(_T("[NatTraversal] ConnectionEstablished: Inbound uTP connection detected; sending proactive Hello, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
						m_uHelloResendCount = 0;
						SetUtpProactiveHelloSent(true);
						QueueUtpHelloPacket();
					} else if (thePrefs.GetLogNatTraversalEvents()) {
						DebugLog(_T("[NatTraversal] ConnectionEstablished: Skipping proactive Hello (already sent or queued: ProactiveHelloSent=%d, HelloQueued=%d), %s"), 
							(int)IsUtpProactiveHelloSent(), (int)IsUtpHelloQueued(), (LPCTSTR)EscPercent(DbgGetClientInfo()));
					}
				}
			}
		} else {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLogWarning(_T("[NatTraversal] ConnectionEstablished: uTP socket NOT connected, deferring Hello, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		}
	}


	switch (GetKadState()) {
	case KS_CONNECTING_FWCHECK:
		SetKadState(KS_CONNECTED_FWCHECK);
		break;
	case KS_CONNECTING_SERVING_BUDDY:
	case KS_INCOMING_SERVED_BUDDY:
		DEBUG_ONLY(DebugLog(_T("[Buddy]: Set KS_CONNECTED_BUDDY for client %s"), (LPCTSTR)EscPercent(DbgGetClientInfo())));
		// Resend Hello if Kad is now running and we have a valid KadID (to include ServingBuddyID tag)
		if (GetKadState() == KS_CONNECTING_SERVING_BUDDY && Kademlia::CKademlia::IsRunning()) {
			Kademlia::CUInt128 myKadID = Kademlia::CKademlia::GetPrefs()->GetKadID();
			if (myKadID != 0 && socket && !IsHelloAnswerPending()) {
				DEBUG_ONLY(DebugLog(_T("[Buddy]: Resending Hello with KadID for %s"), (LPCTSTR)EscPercent(DbgGetClientInfo())));
				SendHelloPacket();
			}
		}
		SetKadState(KS_CONNECTED_BUDDY);
		break;
	case KS_CONNECTING_FWCHECK_UDP:
		SetKadState(KS_FWCHECK_UDP);
		DEBUG_ONLY(DebugLog(_T("Set KS_FWCHECK_UDP for client %s"), (LPCTSTR)EscPercent(DbgGetClientInfo())));
		SendFirewallCheckUDPRequest();
	}

	if (GetChatState() == MS_CONNECTING || GetChatState() == MS_CHATTING)
		if (GetFriend() != NULL) {
			if (GetFriend()->IsTryingToConnect()) {
				GetFriend()->UpdateFriendConnectionState(FCR_ESTABLISHED); // for friends any connection update is handled in the friend class
				if (credits != NULL && credits->GetCurrentIdentState(GetConnectIP()) == IS_IDFAILED)
					GetFriend()->UpdateFriendConnectionState(FCR_SECUREIDENTFAILED);
			} else
				SetChatState(MS_NONE);
		} else
			theApp.emuledlg->chatwnd->chatselector.ConnectingResult(this, true); // other clients update directly

		// Log incoming download state to diagnose intermittent failures
		if (thePrefs.GetLogNatTraversalEvents()) {
			DebugLog(_T("[NatTraversal] ConnectionEstablished: Current download state=%d (%s), reqfile=%s, QueueRankPending=%u for %s"),
				GetDownloadState(),
				GetDownloadState() == DS_CONNECTING ? _T("DS_CONNECTING") :
				GetDownloadState() == DS_WAITCALLBACK ? _T("DS_WAITCALLBACK") :
				GetDownloadState() == DS_WAITCALLBACKKAD ? _T("DS_WAITCALLBACKKAD") :
				GetDownloadState() == DS_ONQUEUE ? _T("DS_ONQUEUE") :
				GetDownloadState() == DS_CONNECTED ? _T("DS_CONNECTED") :
				GetDownloadState() == DS_DOWNLOADING ? _T("DS_DOWNLOADING") :
				_T("OTHER"),
				m_reqfile ? _T("YES") : _T("NULL"),
				(unsigned)m_fQueueRankPending,
				(LPCTSTR)EscPercent(DbgGetClientInfo()));
		}

		switch (GetDownloadState()) {
		case DS_CONNECTING:
		case DS_WAITCALLBACK:
		case DS_WAITCALLBACKKAD:
		case DS_ONQUEUE: { // Also transition from OnQueue to Connected (NAT-T/uTP scenario)
			// If request file is missing (e.g. NAT-T object race), try to resolve from download queue by endpoint.
			if (m_reqfile == NULL) {
				CAddress cip = GetConnectIP().IsNull() ? GetIP() : GetConnectIP();
				uint16 cport = GetKadPort() ? GetKadPort() : GetUDPPort();
				CUpDownClient* pCtx = NULL;
				bool bMulti = false;
				if (!cip.IsNull() && cport != 0)
					pCtx = theApp.downloadqueue->GetDownloadClientByIP_UDP(cip, cport, true, &bMulti);
				if (pCtx == NULL && !cip.IsNull())
					pCtx = theApp.downloadqueue->GetDownloadClientByIP(cip);
				if (pCtx && pCtx != this && theApp.clientlist->IsValidClient(pCtx) && pCtx->GetRequestFile() != NULL)
					SetRequestFile(pCtx->GetRequestFile());
			}

			// For inbound upload-only sessions, keep DS_NONE instead of DS_CONNECTED("Asking").
			if (m_reqfile == NULL && !m_bReaskPending && m_fQueueRankPending == 0) {
				if (GetDownloadState() != DS_NONE)
					SetDownloadState(DS_NONE);
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal] ConnectionEstablished: No download context after connect, keeping DS_NONE for %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
				break;
			}

			m_bReaskPending = false;
			SetDownloadState(DS_CONNECTED);
			// Dispatch initial file request: uTP socket is writable after UTP_STATE_CONNECT, safe to send now.
			// Only call SendFileRequest if OP_STARTUPLOADREQ has not been sent yet (prevent duplicate SendFileRequest after OP_HELLO re-processing).
			TriggerFileRequestIfNeeded();
			break;
		}
		case DS_NONE: {
			// Upload-only inbound sessions can legitimately stay in DS_NONE.
			// Avoid forcing DS_CONNECTED ("Asking") when there is no request file context.
			if (m_reqfile == NULL) {
				CAddress cip = GetConnectIP().IsNull() ? GetIP() : GetConnectIP();
				uint16 cport = GetKadPort() ? GetKadPort() : GetUDPPort();
				CUpDownClient* pCtx = NULL;
				bool bMulti = false;
				if (!cip.IsNull() && cport != 0)
					pCtx = theApp.downloadqueue->GetDownloadClientByIP_UDP(cip, cport, true, &bMulti);
				if (pCtx == NULL && !cip.IsNull())
					pCtx = theApp.downloadqueue->GetDownloadClientByIP(cip);
				if (pCtx && pCtx != this && theApp.clientlist->IsValidClient(pCtx) && pCtx->GetRequestFile() != NULL)
					SetRequestFile(pCtx->GetRequestFile());
			}

			if (m_reqfile != NULL || m_bReaskPending || m_fQueueRankPending != 0) {
				m_bReaskPending = false;
				SetDownloadState(DS_CONNECTED);
				TriggerFileRequestIfNeeded();
			} else if (thePrefs.GetLogNatTraversalEvents()) {
				DebugLog(_T("[NatTraversal] ConnectionEstablished: Keeping DS_NONE (upload-only/no reqfile context) for %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			}
			break;
		}
		default:
			// If already DS_CONNECTED and we have reqfile, send file request only if not already pending.
			// This handles reconnection scenarios where download state persists across connections.
			TriggerFileRequestIfNeeded();
			break;
		}

	if (m_bReaskPending) {
		m_bReaskPending = false;
		if (GetDownloadState() != DS_NONE && GetDownloadState() != DS_DOWNLOADING) {
			SetDownloadState(DS_CONNECTED);
			// If request file is missing (e.g. NAT-T object race), try to resolve from download queue by endpoint.
			if (m_reqfile == NULL) {
				CAddress cip = GetConnectIP().IsNull() ? GetIP() : GetConnectIP();
				uint16 cport = GetKadPort() ? GetKadPort() : GetUDPPort();
				CUpDownClient* pCtx = NULL;
				bool bMulti = false;
				if (!cip.IsNull() && cport != 0)
					pCtx = theApp.downloadqueue->GetDownloadClientByIP_UDP(cip, cport, true, &bMulti);
				if (pCtx == NULL && !cip.IsNull())
					pCtx = theApp.downloadqueue->GetDownloadClientByIP(cip);
				// Fallback: try match by IP only (port-agnostic) as in UDP path
				if (pCtx == NULL && !cip.IsNull())
					pCtx = theApp.downloadqueue->GetDownloadClientByIP_UDP(cip, 0, true, &bMulti);
				// Fallback: try match by user hash in clientlist
				if (pCtx == NULL && HasValidHash()) {
					CUpDownClient* byHash = theApp.clientlist->FindClientByUserHash(GetUserHash(), CAddress(), 0);
					if (byHash && byHash != this && theApp.clientlist->IsValidClient(byHash))
						pCtx = byHash;
				}
				if (pCtx && pCtx != this && theApp.clientlist->IsValidClient(pCtx) && pCtx->GetRequestFile() != NULL)
					SetRequestFile(pCtx->GetRequestFile());
			}
			// Re-ask path: uTP socket is writable after UTP_STATE_CONNECT
			TriggerFileRequestIfNeeded();
		}
	}

	if (GetUploadState() == US_CONNECTING && theApp.uploadqueue->IsDownloading(this)) {
		SetUploadState(US_UPLOADING);
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] AcceptUpload: sending OP_ACCEPTUPLOADREQ via %s for %s"), (socket && socket->HaveUtpLayer()) ? _T("uTP(queue)") : _T("TCP"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP_AcceptUploadReq", this);
		Packet *packet = new Packet(OP_ACCEPTUPLOADREQ, 0);
		theStats.AddUpDataOverheadFileRequest(packet->size);
		// For uTP, queue the packet instead of sending immediately
		if (socket && socket->HaveUtpLayer())
			m_WaitingPackets_list.AddTail(packet);
		else
			SendPacket(packet);
	}

	if (m_iFileListRequested == 1) {
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend(m_fSharedDirectories ? "OP_AskSharedDirs" : "OP_AskSharedFiles", this);
		Packet *packet = new Packet(m_fSharedDirectories ? OP_ASKSHAREDDIRS : OP_ASKSHAREDFILES, 0);
		theStats.AddUpDataOverheadOther(packet->size);
		// For uTP, queue the packet instead of sending immediately
		if (socket && socket->HaveUtpLayer())
			m_WaitingPackets_list.AddTail(packet);
		else
			SendPacket(packet);
	}

	// For uTP connections, DON'T flush queue immediately - socket may not be write-ready yet.
	// Instead, trigger a write attempt to make libutp fire WRITABLE callback, which will flush queue.
	// This avoids WSASend 10057 error that occurs when socket reports connected but isn't ready.
	if (socket && socket->HaveUtpLayer() && !m_WaitingPackets_list.IsEmpty()) {
		m_dwUtpQueuedPacketsTime = ::GetTickCount(); // Set timestamp for delayed flush
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] ConnectionEstablished: Queue has %d packets for uTP, deferring send to avoid premature Send(), %s"), m_WaitingPackets_list.GetCount(), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		// Queue will be flushed by ServiceUtpQueuedPackets after delay
	}

	// Check for pending eServer relay - buddy may have reconnected
	if (m_pPendingEServerRelayTarget != NULL) {
		AddDebugLogLine(false, _T("[eServerBuddy] ConnectionEstablished: Buddy socket now connected, processing pending relay"));
		ProcessPendingEServerRelay();
	}

}

void CUpDownClient::InitClientSoftwareVersion()
{
	if (m_bIsArchived && !m_strClientSoftware.IsEmpty()) {
		m_clientSoft = DetectStoredClientSoftware(m_strClientSoftware);
		return;
	}

	if (m_pszUsername == NULL) {
		m_clientSoft = SO_UNKNOWN;
		return;
	}

	int iHashType = GetHashType();
	if (m_bEmuleProtocol || iHashType == SO_EMULE) {
		LPCTSTR pszSoftware;
		switch (m_byCompatibleClient) {
		case SO_CDONKEY:
			m_clientSoft = SO_CDONKEY;
			pszSoftware = _T("cDonkey");
			break;
		case SO_XMULE:
			m_clientSoft = SO_XMULE;
			pszSoftware = _T("xMule");
			break;
		case SO_AMULE:
			m_clientSoft = SO_AMULE;
			pszSoftware = _T("aMule");
			break;
		case SO_SHAREAZA:
		//case 40:
		case SO_SHAREAZA2:
		case SO_SHAREAZA3:
		case SO_SHAREAZA4:
			m_clientSoft = SO_SHAREAZA;
			pszSoftware = _T("Shareaza");
			break;
		case SO_LPHANT:
			m_clientSoft = SO_LPHANT;
			pszSoftware = _T("lphant");
			break;
		case SO_EMULEPLUS:
			m_clientSoft = SO_EMULEPLUS;
			pszSoftware = _T("eMule Plus");
			break;
		case SO_HYDRANODE:
			m_clientSoft = SO_HYDRANODE;
			pszSoftware = _T("Hydranode");
			break;
		case SO_HYDRA:
			m_clientSoft = SO_HYDRA;
			pszSoftware = _T("Hydra");
			break;
		case SO_TRUSTYFILES:
			m_clientSoft = SO_TRUSTYFILES;
			pszSoftware = _T("TrustyFiles");
			break;
		case SO_EASYMULE2:
			m_clientSoft = SO_EASYMULE2;
			pszSoftware = _T("easyMule");
			break;
		case SO_NEOLOADER:
			m_clientSoft = SO_NEOLOADER;
			pszSoftware = _T("NeoLoader");
			break;
		case SO_KMULE:
			m_clientSoft = SO_KMULE;
			pszSoftware = L"kMule";
			break;
		default:
			if (m_bIsML || m_byCompatibleClient == SO_MLDONKEY || m_byCompatibleClient == SO_MLDONKEY2 || m_byCompatibleClient == SO_MLDONKEY3) {
				m_clientSoft = SO_MLDONKEY;
				pszSoftware = _T("MLdonkey");
			} else if (m_bIsHybrid || m_byCompatibleClient == SO_EDONKEYHYBRID) {
				m_clientSoft = SO_EDONKEYHYBRID;
				pszSoftware = _T("eDonkeyHybrid");
			} else if (m_byCompatibleClient != 0) {
				if (StrStrI(m_pszUsername, _T("shareaza")))	{
					m_clientSoft = SO_SHAREAZA;
					pszSoftware = _T("Shareaza");
				} else if (StrStr(m_strModVersion, _T("Plus 1"))) {
					m_clientSoft = SO_EMULEPLUS;
					pszSoftware = _T("eMule Plus");
				} else {
				m_clientSoft = SO_XMULE; // means: 'eMule Compatible'
				pszSoftware = _T("eMule Compat");
				}
			} else {
				m_clientSoft = SO_EMULE;
				pszSoftware = _T("eMule");
			}
		}

		int iLen;
		TCHAR szSoftware[128];
		if (m_byEmuleVersion == 0) {
			m_nClientVersion = MAKE_CLIENT_VERSION(0, 0, 0);
			iLen = _sntprintf(szSoftware, _countof(szSoftware), _T("%s"), pszSoftware);
		} else if (m_byEmuleVersion != 0x99) {
			UINT nClientMinVersion = (m_byEmuleVersion >> 4) * 10 + (m_byEmuleVersion & 0x0f);
			m_nClientVersion = MAKE_CLIENT_VERSION(0, nClientMinVersion, 0);
			iLen = _sntprintf(szSoftware, _countof(szSoftware), _T("%s v0.%u"), pszSoftware, nClientMinVersion);
		} else {
			UINT nClientMajVersion = (m_nClientVersion >> 17) & 0x7f;
			UINT nClientMinVersion = (m_nClientVersion >> 10) & 0x7f;
			UINT nClientUpVersion = (m_nClientVersion >> 7) & 0x07;
			m_nClientVersion = MAKE_CLIENT_VERSION(nClientMajVersion, nClientMinVersion, nClientUpVersion);
			if (m_clientSoft == SO_EMULE)
				iLen = _sntprintf(szSoftware, _countof(szSoftware), _T("%s v%u.%u%c"), pszSoftware, nClientMajVersion, nClientMinVersion, _T('a') + nClientUpVersion);
			else if (m_clientSoft == SO_EMULEPLUS)
			{
				if (nClientMinVersion == 0) {
					if (nClientUpVersion == 0)
						iLen = _sntprintf(szSoftware, _countof(szSoftware), _T("%s v%u"), pszSoftware, nClientMajVersion);
					else
						iLen = _sntprintf(szSoftware, _countof(szSoftware), _T("%s v%u%c"), pszSoftware, nClientMajVersion, _T('a') + nClientUpVersion - 1);
				} else {
					if (nClientUpVersion == 0)
						iLen = _sntprintf(szSoftware, _countof(szSoftware), _T("%s v%u.%u"), pszSoftware, nClientMajVersion, nClientMinVersion);
					else
						iLen = _sntprintf(szSoftware, _countof(szSoftware), _T("%s v%u.%u%c"), pszSoftware, nClientMajVersion, nClientMinVersion, _T('a') + nClientUpVersion - 1);
				}
			} else if (m_clientSoft == SO_NEOLOADER) {
				if (nClientMinVersion < 10)
					iLen = _sntprintf(szSoftware, ARRSIZE(szSoftware), _T("%s v%u.0%u"), pszSoftware, nClientMajVersion, nClientMinVersion);
				else
					iLen = _sntprintf(szSoftware, ARRSIZE(szSoftware), _T("%s v%u.%u"), pszSoftware, nClientMajVersion, nClientMinVersion);
				if (nClientUpVersion != 0)
					iLen += _sntprintf(szSoftware + iLen, ARRSIZE(szSoftware) - iLen, _T("%c"), _T('a') + nClientUpVersion - 1);
			} else if (m_clientSoft == SO_AMULE || m_clientSoft == SO_EASYMULE2 || m_clientSoft == SO_KMULE || nClientUpVersion != 0)
				iLen = _sntprintf(szSoftware, _countof(szSoftware), _T("%s v%u.%u.%u"), pszSoftware, nClientMajVersion, nClientMinVersion, nClientUpVersion);
			else if (m_clientSoft == SO_LPHANT)
				iLen = _sntprintf(szSoftware, _countof(szSoftware), _T("%s v%u.%02u"), pszSoftware, (nClientMajVersion - 1), nClientMinVersion);
			else
				iLen = _sntprintf(szSoftware, _countof(szSoftware), _T("%s v%u.%u"), pszSoftware, nClientMajVersion, nClientMinVersion);
		}
		if (iLen > 0) {
			memcpy(m_strClientSoftware.GetBuffer(iLen), szSoftware, iLen * sizeof(TCHAR));
			m_strClientSoftware.ReleaseBuffer(iLen);
		}
		return;
	}

	if (m_bIsHybrid || m_byCompatibleClient == SO_EDONKEYHYBRID){
		m_clientSoft = SO_EDONKEYHYBRID;
		// seen:
		// 105010	0.50.10
		// 10501	0.50.1
		// 10300	1.3.0
		// 10201	1.2.1
		// 10103	1.1.3
		// 10102	1.1.2
		// 10100	1.1
		// 1051		0.51.0
		// 1002		1.0.2
		// 1000		1.0
		// 501		0.50.1

		UINT nClientMajVersion;
		UINT nClientMinVersion;
		UINT nClientUpVersion;
		if (m_nClientVersion > 100000) {
			UINT uMaj = m_nClientVersion / 100000;
			nClientMajVersion = uMaj - 1;
			nClientMinVersion = (m_nClientVersion - uMaj * 100000) / 100;
			nClientUpVersion = m_nClientVersion % 100;
		} else if (m_nClientVersion >= 10100 && m_nClientVersion <= 10309) {
			UINT uMaj = m_nClientVersion / 10000;
			nClientMajVersion = uMaj;
			nClientMinVersion = (m_nClientVersion - uMaj * 10000) / 100;
			nClientUpVersion = m_nClientVersion % 10;
		} else if (m_nClientVersion > 10000) {
			UINT uMaj = m_nClientVersion / 10000;
			nClientMajVersion = uMaj - 1;
			nClientMinVersion = (m_nClientVersion - uMaj * 10000) / 10;
			nClientUpVersion = m_nClientVersion % 10;
		} else if (m_nClientVersion >= 1000 && m_nClientVersion < 1020) {
			UINT uMaj = m_nClientVersion / 1000;
			nClientMajVersion = uMaj;
			nClientMinVersion = (m_nClientVersion - uMaj * 1000) / 10;
			nClientUpVersion = m_nClientVersion % 10;
		} else if (m_nClientVersion > 1000) {
			UINT uMaj = m_nClientVersion / 1000;
			nClientMajVersion = uMaj - 1;
			nClientMinVersion = m_nClientVersion - uMaj * 1000;
			nClientUpVersion = 0;
		} else if (m_nClientVersion > 100) {
			UINT uMin = m_nClientVersion / 10;
			nClientMajVersion = 0;
			nClientMinVersion = uMin;
			nClientUpVersion = m_nClientVersion - uMin * 10;
		} else {
			nClientMajVersion = 0;
			nClientMinVersion = m_nClientVersion;
			nClientUpVersion = 0;
		}
		m_nClientVersion = MAKE_CLIENT_VERSION(nClientMajVersion, nClientMinVersion, nClientUpVersion);

		int iLen;
		TCHAR szSoftware[128];
		if (nClientUpVersion)
			iLen = _sntprintf(szSoftware, _countof(szSoftware), _T("eDonkeyHybrid v%u.%u.%u"), nClientMajVersion, nClientMinVersion, nClientUpVersion);
		else
			iLen = _sntprintf(szSoftware, _countof(szSoftware), _T("eDonkeyHybrid v%u.%u"), nClientMajVersion, nClientMinVersion);
		if (iLen > 0) {
			memcpy(m_strClientSoftware.GetBuffer(iLen), szSoftware, iLen * sizeof(TCHAR));
			m_strClientSoftware.ReleaseBuffer(iLen);
		}
		return;
	}

	if (m_bIsML || iHashType == SO_MLDONKEY || iHashType == SO_OLD_MLDONKEY) {
		m_clientSoft = SO_MLDONKEY;
		UINT nClientMinVersion = m_nClientVersion;
		m_nClientVersion = MAKE_CLIENT_VERSION(0, nClientMinVersion, 0);
		TCHAR szSoftware[128];
		int iLen = _sntprintf(szSoftware, _countof(szSoftware), _T("MLdonkey v0.%u"), nClientMinVersion);
		if (iLen > 0) {
			memcpy(m_strClientSoftware.GetBuffer(iLen), szSoftware, iLen * sizeof(TCHAR));
			m_strClientSoftware.ReleaseBuffer(iLen);
		}
		return;
	}

	if (iHashType == SO_OLDEMULE) {
		m_clientSoft = SO_OLDEMULE;
		UINT nClientMinVersion = m_nClientVersion;
		m_nClientVersion = MAKE_CLIENT_VERSION(0, nClientMinVersion, 0);
		TCHAR szSoftware[128];
		int iLen = _sntprintf(szSoftware, _countof(szSoftware), _T("Old eMule v0.%u"), nClientMinVersion);
		if (iLen > 0) {
			memcpy(m_strClientSoftware.GetBuffer(iLen), szSoftware, iLen * sizeof(TCHAR));
			m_strClientSoftware.ReleaseBuffer(iLen);
		}
		return;
	}

	m_clientSoft = SO_EDONKEY;
	UINT nClientMinVersion = m_nClientVersion;
	m_nClientVersion = MAKE_CLIENT_VERSION(0, nClientMinVersion, 0);
	TCHAR szSoftware[128];
	int iLen = _sntprintf(szSoftware, _countof(szSoftware), _T("eDonkey v0.%u"), nClientMinVersion);
	if (iLen > 0) {
		memcpy(m_strClientSoftware.GetBuffer(iLen), szSoftware, iLen * sizeof(TCHAR));
		m_strClientSoftware.ReleaseBuffer(iLen);
	}
}

int CUpDownClient::GetHashType() const
{
	if (m_achUserHash[5] == 13 && m_achUserHash[14] == 110)
		return SO_OLDEMULE;
	if (m_achUserHash[5] == 14 && m_achUserHash[14] == 111)
		return SO_EMULE;
	if (m_achUserHash[5] == 'M' && m_achUserHash[14] == 'L')
		return SO_OLD_MLDONKEY;
	else if (m_achUserHash[5] == 0x0E && m_achUserHash[14] == 0x6F) // Spike2 by Torni - recognize newer MLdonkeys (needed for Enhanced Client Recognization & emulate-Settings!)
		return SO_MLDONKEY;
	return SO_UNKNOWN;
}

void CUpDownClient::SetUserName(LPCTSTR pszNewName)
{
	free(m_pszUsername);
	m_pszUsername = (pszNewName ? _tcsdup(pszNewName) : NULL);
}

void CUpDownClient::RequestSharedFileList()
{
	if (m_iFileListRequested == 0) {
		AddLogLine(true, GetResString(_T("SHAREDFILES_REQUEST")), (GetUserName() == NULL || GetUserName()[0] == '\0') ? _T('(') + md4str(GetUserHash()) + _T(')') : (LPCTSTR)EscPercent(GetUserName()));
		m_bQueryingSharedFiles = true;
		m_tSharedFilesLastQueriedTime = time(NULL);
		theApp.emuledlg->transferwnd->GetClientList()->RefreshClient(this);
		theApp.emuledlg->transferwnd->GetUploadList()->RefreshClient(this);
		theApp.emuledlg->transferwnd->GetQueueList()->RefreshClient(this);
		theApp.emuledlg->transferwnd->GetDownloadClientsList()->RefreshClient(this);
		m_iFileListRequested = 1;
		TryToConnect(true);
	} else
	{
		LogWarning(LOG_STATUSBAR, GetResString(_T("SHAREDFILES_REQUEST_IN_PROGRESS")), (GetUserName() == NULL || GetUserName()[0] == '\0') ? _T('(') + md4str(GetUserHash()) + _T(')') : GetUserName(), GetUserIDHybrid());
	}
}

void CUpDownClient::ProcessSharedFileList(const uchar *pachPacket, uint32 nSize, LPCTSTR pszDirectory)
{
	if (m_iFileListRequested > 0) {
		--m_iFileListRequested;
		theApp.searchlist->ProcessSearchAnswer(pachPacket, nSize, *this, NULL, pszDirectory);
	}
}

void CUpDownClient::SetUserHash(const uchar *pucUserHash, bool bLoadArchive)
{
	if (pucUserHash == NULL || isnulmd4(pucUserHash))
		md4clr(m_achUserHash);
	else if (!md4equ(m_achUserHash, pucUserHash)) { // Update current hash only if it is different
		md4cpy(m_achUserHash, pucUserHash);
		if (thePrefs.GetClientHistory() && !m_bIsArchived) {
			if (bLoadArchive)
				theApp.emuledlg->transferwnd->GetClientList()->LoadArchive(this, _T("SetUserHash"));
			int m_iIndex;
			if (m_ArchivedClient && theApp.emuledlg->transferwnd->GetClientList()->m_ListedItemsMap.Lookup(this, m_iIndex)) // Only try to remove archived client from listCtrl if this active client is listed.
				theApp.emuledlg->transferwnd->GetClientList()->RemoveClient(m_ArchivedClient);
		}
	}
}

void CUpDownClient::SetServingBuddyID(const uchar *pucServingBuddyID)
{
	if (pucServingBuddyID == NULL) {
		md4clr(m_achServingBuddyID);
		m_bServingBuddyIDValid = false;
	} else {
		m_bServingBuddyIDValid = true;
		md4cpy(m_achServingBuddyID, pucServingBuddyID);
	}
}

void CUpDownClient::SendPublicKeyPacket()
{
	// send our public key to the client who requested it
	if (socket == NULL || credits == NULL || m_SecureIdentState != IS_KEYANDSIGNEEDED) {
		ASSERT(0);
		return;
	}
	if (!theApp.clientcredits->CryptoAvailable())
		return;

	Packet *packet = new Packet(OP_PUBLICKEY, theApp.clientcredits->GetPubKeyLen() + 1, OP_EMULEPROT);
	theStats.AddUpDataOverheadOther(packet->size);
	memcpy(packet->pBuffer + 1, theApp.clientcredits->GetPublicKey(), theApp.clientcredits->GetPubKeyLen());
	packet->pBuffer[0] = theApp.clientcredits->GetPubKeyLen();
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_PublicKey", this);
	SendPacket(packet);
	m_SecureIdentState = IS_SIGNATURENEEDED;
}

void CUpDownClient::SendSignaturePacket()
{
	// sign the public key of this client and send it
	if (socket == NULL || credits == NULL || m_SecureIdentState == 0) {
		ASSERT(0);
		return;
	}

	if (!theApp.clientcredits->CryptoAvailable())
		return;
	if (credits->GetSecIDKeyLen() == 0)
		return; // We don't have his public key yet, will be back here later
	// do we have a challenge value received (actually we should if we are in this function)
	if (credits->m_dwCryptRndChallengeFrom == 0) {
		if (thePrefs.GetLogSecureIdent())
			AddDebugLogLine(false, _T("Want to send signature but challenge value is invalid ('%s')"), (LPCTSTR)EscPercent(GetUserName()));
		return;
	}
	// v2
	// we will use v1 as default, except if only v2 is supported
	bool bUseV2 = !(m_bySupportSecIdent & 1);
	uint8 byChaIPKind;
	//uint32 ChallengeIP;
	CAddress ChallengeIP;
	if (bUseV2) {
		if (theApp.serverconnect->GetClientID() == 0 || theApp.serverconnect->IsLowID()) {
			// we cannot do not know for sure our public ip, so use the remote clients one
			ChallengeIP = GetIP();
			byChaIPKind = CRYPT_CIP_REMOTECLIENT;
		} else {
			ChallengeIP = CAddress(theApp.serverconnect->GetClientID(), false);
			byChaIPKind = CRYPT_CIP_LOCALCLIENT;
		}
	} else {
		byChaIPKind = 0;
	}
	//end v2
	uchar achBuffer[250];
	uint8 siglen = theApp.clientcredits->CreateSignature(credits, achBuffer, sizeof achBuffer, ChallengeIP, byChaIPKind);
	if (siglen == 0) {
		ASSERT(0);
		return;
	}
	Packet *packet = new Packet(OP_SIGNATURE, siglen + 1 + static_cast<int>(bUseV2), OP_EMULEPROT);
	theStats.AddUpDataOverheadOther(packet->size);
	memcpy(packet->pBuffer + 1, achBuffer, siglen);
	packet->pBuffer[0] = siglen;
	if (bUseV2)
		packet->pBuffer[1 + siglen] = byChaIPKind;
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_Signature", this);
	SendPacket(packet);
	m_SecureIdentState = IS_ALLREQUESTSSEND;
}

void CUpDownClient::ProcessPublicKeyPacket(const uchar *pachPacket, uint32 nSize)
{
	theApp.clientlist->AddTrackClient(this);

	if (socket == NULL || credits == NULL || pachPacket[0] != nSize - 1 || nSize < 10 || nSize > 250) {
		//ASSERT ( false ); on network malfunction eMule crashed while hanging in this assert's messagebox. Also 451 bytes packet were seen in the wild.
		return;
	}
	if (!theApp.clientcredits->CryptoAvailable())
		return;
	// the function will handle everything (multiple key etc)
	if (credits->SetSecureIdent(pachPacket + 1, pachPacket[0])) {
		// if this client wants a signature, now we can send him one
		if (m_SecureIdentState == IS_SIGNATURENEEDED)
			SendSignaturePacket();
		else if (m_SecureIdentState == IS_KEYANDSIGNEEDED) {
			// something is wrong
			if (thePrefs.GetLogSecureIdent())
				AddDebugLogLine(false, _T("Invalid State error: IS_KEYANDSIGNEEDED in ProcessPublicKeyPacket"));
		}
	} else if (thePrefs.GetLogSecureIdent())
		AddDebugLogLine(false, _T("Failed to use new received public key"));
}

void CUpDownClient::ProcessSignaturePacket(const uchar *pachPacket, uint32 nSize)
{
	// here we spread the good guys from the bad ones ;)

	if (socket == NULL || credits == NULL || nSize > 250 || nSize < 10) {
		//ASSERT ( false ); I have seen size 0x181; just a return should be sufficient
		return;
	}

	uint8 byChaIPKind;
	if (pachPacket[0] == nSize - 1)
		byChaIPKind = 0;
	else if (pachPacket[0] == nSize - 2 && (m_bySupportSecIdent & 2) > 0) //v2
		byChaIPKind = pachPacket[nSize - 1];
	else {
		ASSERT(0);
		return;
	}

	if (!theApp.clientcredits->CryptoAvailable())
		return;

	// we accept only one signature per IP, to avoid floods which need a lot CPU time for crypto-functions
	if (m_dwLastSignatureIP == GetIP()) {
		if (thePrefs.GetLogSecureIdent())
			AddDebugLogLine(false, _T("received multiple signatures from one client"));
		return;
	}

	// also make sure this client has a public key
	if (credits->GetSecIDKeyLen() == 0) {
		if (thePrefs.GetLogSecureIdent())
			AddDebugLogLine(false, _T("received signature for client without public key"));
		return;
	}

	// and one more check: did we ask for a signature and sent a challenge packet?
	if (credits->m_dwCryptRndChallengeFor == 0) {
		if (thePrefs.GetLogSecureIdent())
			AddDebugLogLine(false, _T("received signature for client with invalid challenge value ('%s')"), (LPCTSTR)EscPercent(GetUserName()));
		return;
	}

	if (theApp.clientcredits->VerifyIdent(credits, pachPacket + 1, pachPacket[0], GetIP(), byChaIPKind)) {
		// the result was saved in the function above
		m_SecureIdentVerifiedIP = GetIP();
		m_bSecureIdentRecheckPending = false;
		if (GetFriend() != NULL && GetFriend()->IsTryingToConnect())
			GetFriend()->UpdateFriendConnectionState(FCR_USERHASHVERIFIED);
	} else {
		m_SecureIdentVerifiedIP = CAddress();
		m_bSecureIdentRecheckPending = false;
		if (GetFriend() != NULL && GetFriend()->IsTryingToConnect())
			GetFriend()->UpdateFriendConnectionState(FCR_SECUREIDENTFAILED);
		if (thePrefs.GetLogSecureIdent())
			AddDebugLogLine(false, _T("'%s' has failed the secure identification, V2 State: %i"), (LPCTSTR)EscPercent(GetUserName()), byChaIPKind);
	}
	m_dwLastSignatureIP = GetIP();
}

void CUpDownClient::ResetSecureIdentState()
{
	// Reset SecureIdent state to force re-authentication on new uTP connections
	m_dwLastSignatureIP = CAddress();
	m_SecureIdentState = IS_UNAVAILABLE;
	m_SecureIdentVerifiedIP = CAddress();
	m_bSecureIdentRecheckPending = true;
	if (credits != NULL)
		credits->ResetTransientSecureIdentState();
}

void CUpDownClient::SendSecIdentStatePacket()
{
	// check if we need public key and signature
	if (credits) {
		if (!theApp.clientcredits->CryptoAvailable())
			return;
		uint8 nValue;
		if (credits->GetSecIDKeyLen() == 0)
			nValue = IS_KEYANDSIGNEEDED;
		else if (m_dwLastSignatureIP != GetIP())
			nValue = IS_SIGNATURENEEDED;
		else
			return;

		// crypt: send random data to sign
		uint32 dwRandom = rand() + 1;
		credits->m_dwCryptRndChallengeFor = dwRandom;
		Packet *packet = new Packet(OP_SECIDENTSTATE, 5, OP_EMULEPROT);
		theStats.AddUpDataOverheadOther(packet->size);
		packet->pBuffer[0] = nValue;
		PokeUInt32(&packet->pBuffer[1], dwRandom);
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP_SecIdentState", this);
		SendPacket(packet);
	} else
		ASSERT(0);
}

void CUpDownClient::ProcessSecIdentStatePacket(const uchar *pachPacket, uint32 nSize)
{
	if (nSize != 5)
		return;
	if (!credits) {
		ASSERT(0);
		return;
	}
	switch (pachPacket[0]) {
	case 0:
		m_SecureIdentState = IS_UNAVAILABLE;
		break;
	case 1:
		m_SecureIdentState = IS_SIGNATURENEEDED;
		break;
	case 2:
		m_SecureIdentState = IS_KEYANDSIGNEEDED;
	}
	credits->m_dwCryptRndChallengeFrom = PeekUInt32(pachPacket + 1);
}

void CUpDownClient::InfoPacketsReceived()
{
	// indicates that both Information Packets have been received
	// needed for actions, which process data from both packets
	ASSERT(m_byInfopacketsReceived == IP_BOTH);
	m_byInfopacketsReceived = IP_NONE;

	if (m_bySupportSecIdent)
		SendSecIdentStatePacket();
}

void CUpDownClient::ResetFileStatusInfo()
{
	delete[] m_abyPartStatus;
	m_abyPartStatus = NULL;
	delete[] m_abyIncPartStatus;
	m_abyIncPartStatus = NULL;
	m_nRemoteQueueRank = 0;
	m_nPartCount = 0;
	m_strClientFilename.Empty();
	m_bCompleteSource = false;
	m_uFileRating = 0;
	m_strFileComment.Empty();
	delete m_pReqFileAICHHash;
	m_pReqFileAICHHash = NULL;
}

bool CUpDownClient::IsBanned() const
{
	if (m_eUploadState == US_BANNED)
		return true;

	if (GetConnectIP().IsNull() && isnulmd4(GetUserHash()))
		return false;
	return theApp.clientlist->IsBannedClient(GetConnectIP().ToStringC()) || theApp.clientlist->IsBannedClient(md4str(GetUserHash()));
}

void CUpDownClient::SendPreviewRequest(const CAbstractFile &rForFile)
{
	if (m_fPreviewReqPending == 0) {
		m_fPreviewReqPending = 1;
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP_RequestPreview", this, rForFile.GetFileHash());
		Packet *packet = new Packet(OP_REQUESTPREVIEW, 16, OP_EMULEPROT);
		md4cpy(packet->pBuffer, rForFile.GetFileHash());
		theStats.AddUpDataOverheadOther(packet->size);
		SafeConnectAndSendPacket(packet);
	} else
		LogWarning(LOG_STATUSBAR, GetResString(_T("ERR_PREVIEWALREADY")));
}

void CUpDownClient::SendPreviewAnswer(const CKnownFile *pForFile, HBITMAP *imgFrames, uint8 nCount)
{
	m_fPreviewAnsPending = 0;
	if (nCount > 0 && imgFrames == NULL) {
		ASSERT(0);
		return;
	}
	CSafeMemFile data(1024);
	if (pForFile)
		data.WriteHash16(pForFile->GetFileHash());
	else {
		static const uchar _aucZeroHash[MDX_DIGEST_SIZE] = {};
		data.WriteHash16(_aucZeroHash);
	}
	data.WriteUInt8(nCount);
	bool bSend = true;
	for (int i = 0; i < nCount; ++i) {
		HBITMAP bmp_frame = imgFrames[i];
		if (bmp_frame) {
			if (bSend) {
				size_t nFrameSize;
				byte *byFrameBuffer = bmp2mem(bmp_frame, nFrameSize, Gdiplus::ImageFormatPNG);
				if (byFrameBuffer) {
					data.WriteUInt32((uint32)nFrameSize);
					data.Write(byFrameBuffer, (UINT)nFrameSize);
					delete[] byFrameBuffer;
				} else {
					ASSERT(0);
					bSend = false;
				}
			}
			::DeleteObject(bmp_frame);
			imgFrames[i] = 0;
		} else {
			ASSERT(0);
			bSend = false;
		}
	}
	if (!bSend)
		return;
	Packet *packet = new Packet(data, OP_EMULEPROT);
	packet->opcode = OP_PREVIEWANSWER;
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_PreviewAnswer", this, (uchar*)packet->pBuffer);
	theStats.AddUpDataOverheadOther(packet->size);
	SafeConnectAndSendPacket(packet);
}

void CUpDownClient::ProcessPreviewReq(const uchar *pachPacket, uint32 nSize)
{
	if (nSize < 16)
		throw GetResString(_T("ERR_WRONGPACKETSIZE"));

	if (m_fPreviewAnsPending || thePrefs.CanSeeShares() == vsfaNobody || (thePrefs.CanSeeShares() == vsfaFriends && !IsFriend()))
		return;

	m_fPreviewAnsPending = 1;
	CKnownFile *previewFile = theApp.sharedfiles->GetFileByID(pachPacket);
	if (previewFile == NULL)
		SendPreviewAnswer(NULL, NULL, 0);
	else
		previewFile->GrabImage(4, 15, true, 450, this); //start at 15 seconds; at 0 seconds frame usually were solid black
}

void CUpDownClient::ProcessPreviewAnswer(const uchar *pachPacket, uint32 nSize)
{
	if (m_fPreviewReqPending == 0)
		return;
	m_fPreviewReqPending = 0;
	CSafeMemFile data(pachPacket, nSize);
	uchar Hash[MDX_DIGEST_SIZE];
	data.ReadHash16(Hash);
	uint8 nCount = data.ReadUInt8();
	if (nCount == 0) {
		LogError(LOG_STATUSBAR, GetResString(_T("ERR_PREVIEWFAILED")), GetUserName());
		return;
	}
	CSearchFile *sfile = theApp.searchlist->GetSearchFileByHash(Hash);
	if (sfile == NULL)
		//already deleted
		return;

	byte *pBuffer = NULL;
	HBITMAP image = 0;
	try {
		for (int i = 0; i < nCount; ++i) {
			uint32 nImgSize = data.ReadUInt32();
			if (nImgSize > nSize)
				throwCStr(_T("CUpDownClient::ProcessPreviewAnswer - Provided image size exceeds limit"));
			pBuffer = new byte[nImgSize];
			data.Read(pBuffer, nImgSize);
			image = mem2bmp(pBuffer, nImgSize);
			if (image) {
				sfile->AddPreviewImg(image);
				image = 0;
			}
			delete[] pBuffer;
			pBuffer = NULL;
		}
	} catch (...) {
		if (image)
			::DeleteObject(image);
		delete[] pBuffer;
		throw;
	}
	(new PreviewDlg())->SetFile(sfile);
}

// Sends a packet. If needed, it will establish a connection before.
// Options used: ignore max connections, control packet, delete packet
// !if the functions returns false, that client object was deleted because the connection try failed,
// and the object wasn't needed any more.
bool CUpDownClient::SafeConnectAndSendPacket(Packet *packet)
{
	if (socket != NULL && socket->IsConnected()) {
		socket->SendPacket(packet, true);
		return true;
	}
	m_WaitingPackets_list.AddTail(packet);
	return TryToConnect(true);
}

bool CUpDownClient::SendPacket(Packet *packet, bool bVerifyConnection)
{
	if (socket != NULL && (!bVerifyConnection || socket->IsConnected())) {
		// For uTP, force immediate send to bypass bandwidth throttler queue
		// uTP has its own internal congestion control, so eMule's throttler should not delay handshake packets
		bool bForceImmediate = socket->HaveUtpLayer();
		socket->SendPacket(packet, true, 0, bForceImmediate);
		return true;
	}
	DebugLogError(_T("Outgoing packet (0x%X) discarded because expected socket or connection does not exist %s"), packet->opcode, (LPCTSTR)EscPercent(DbgGetClientInfo()));
	delete packet;
	return false;
}

#ifdef _DEBUG
void CUpDownClient::AssertValid() const
{
	CObject::AssertValid();

	CHECK_OBJ(socket);
	CHECK_PTR(credits);
	CHECK_PTR(m_Friend);
	CHECK_OBJ(m_reqfile);
	(void)m_abyUpPartStatus;
	m_OtherRequests_list.AssertValid();
	m_OtherNoNeeded_list.AssertValid();
	(void)m_lastPartAsked;
	(void)m_cMessagesReceived;
	(void)m_cMessagesSent;
	(void)m_nUserIDHybrid;
	(void)m_nUserPort;
	(void)m_nServerPort;
	(void)m_nClientVersion;
	(void)m_nUpDatarate;
	(void)m_byEmuleVersion;
	(void)m_byDataCompVer;
	CHECK_BOOL(m_bEmuleProtocol);
	CHECK_BOOL(m_bIsHybrid);
	(void)m_pszUsername;
	(void)m_achUserHash;
	(void)m_achServingBuddyID;
	(void)m_nServingBuddyPort;
	(void)m_nUDPPort;
	(void)m_nKadPort;
	(void)m_byUDPVer;
	(void)m_bySourceExchange1Ver;
	(void)m_byAcceptCommentVer;
	(void)m_byExtendedRequestsVer;
	CHECK_BOOL(m_bFriendSlot);
	CHECK_BOOL(m_bCommentDirty);
	CHECK_BOOL(m_bIsML);
	(void)m_strClientSoftware;
	(void)m_dwLastSourceRequest;
	(void)m_dwLastSourceAnswer;
	(void)m_dwLastAskedForSources;
	(void)m_uSearchID;
	(void)m_iFileListRequested;
	(void)m_byCompatibleClient;
	m_WaitingPackets_list.AssertValid();
	m_DontSwap_list.AssertValid();
	ASSERT(m_SecureIdentState >= IS_UNAVAILABLE && m_SecureIdentState <= IS_KEYANDSIGNEEDED);
	ASSERT((m_byInfopacketsReceived & ~IP_BOTH) == 0);
	(void)m_bySupportSecIdent;
	(void)m_nTransferredUp;
	ASSERT(m_eUploadState >= US_UPLOADING && m_eUploadState <= US_NONE);
	(void)m_dwUploadTime;
	(void)m_cAsked;
	(void)m_dwLastUpRequest;
	(void)m_nCurSessionUp;
	(void)m_nCurQueueSessionPayloadUp;
	(void)m_addedPayloadQueueSession;
	(void)m_nUpPartCount;
	(void)m_nUpCompleteSourcesCount;
	(void)requpfileid;
	(void)m_lastRefreshedULDisplay;
	m_AverageUDR_list.AssertValid();
	m_RequestedFiles_list.AssertValid();
	ASSERT(m_eDownloadState >= DS_DOWNLOADING && m_eDownloadState <= DS_NONE);
	(void)m_cDownAsked;
	(void)m_abyPartStatus;
	(void)m_strClientFilename;
	(void)m_nTransferredDown;
	(void)m_nCurSessionPayloadDown;
	(void)m_dwDownStartTime;
	(void)m_nLastBlockOffset;
	(void)m_nDownDatarate;
	(void)m_nDownDataRateMS;
	(void)m_nSumForAvgDownDataRate;
	(void)m_cShowDR;
	(void)m_nRemoteQueueRank;
	(void)m_dwLastBlockReceived;
	(void)m_nPartCount;
	ASSERT(m_eSourceFrom >= SF_SERVER && m_eSourceFrom <= SF_SLS);
	CHECK_BOOL(m_bRemoteQueueFull);
	CHECK_BOOL(m_bCompleteSource);
	CHECK_BOOL(m_bReaskPending);
	CHECK_BOOL(m_bUDPPending);
	CHECK_BOOL(m_bTransferredDownMini);
	CHECK_BOOL(m_bUnicodeSupport);
	ASSERT(m_eKadState >= KS_NONE && m_eKadState <= KS_CONNECTING_FWCHECK_UDP);
	m_AverageDDR_list.AssertValid();
	(void)m_nSumForAvgUpDataRate;
	m_PendingBlocks_list.AssertValid();
	(void)s_StatusBar;
	ASSERT(m_eChatstate >= MS_NONE && m_eChatstate <= MS_UNABLETOCONNECT);
	(void)m_strFileComment;
	(void)m_uFileRating;
	CHECK_BOOL(m_bCollectionUploadSlot);
	(void)uiULAskingCounter;
	(void)uiWaitingPositionRank;
	(void)tFirstSeen;
	CHECK_BOOL(m_bAntiUploaderCaseThree);
	(void)tLastSeen;
	(void)m_abyIncPartStatus;
#undef CHECK_PTR
#undef CHECK_BOOL
}
#endif

#ifdef _DEBUG
void CUpDownClient::Dump(CDumpContext &dc) const
{
	CObject::Dump(dc);
}
#endif

LPCTSTR CUpDownClient::DbgGetDownloadState() const
{
	static LPCTSTR const apszState[] =
	{
		_T("Downloading"),
		_T("OnQueue"),
		_T("Connected"),
		_T("Connecting"),
		_T("WaitCallback"),
		_T("WaitCallbackKad"),
		_T("ReqHashSet"),
		_T("NoNeededParts"),
		_T("TooManyConns"),
		_T("TooManyConnsKad"),
		_T("LowToLowIp"),
		_T("Banned"),
		_T("Error"),
		_T("None"),
		_T("RemoteQueueFull")
	};
	return (GetDownloadState() < _countof(apszState)) ? apszState[GetDownloadState()] : _T("*Unknown*");
}

LPCTSTR CUpDownClient::DbgGetUploadState() const
{
	static LPCTSTR const apszState[] =
	{
		_T("Uploading"),
		_T("OnUploadQueue"),
		_T("Connecting"),
		_T("Banned"),
		_T("None")
	};
	return (GetUploadState() < _countof(apszState)) ? apszState[GetUploadState()] : _T("*Unknown*");
}

LPCTSTR CUpDownClient::DbgGetKadState() const
{
	static LPCTSTR const apszState[] =
	{
		_T("None"),
		_T("FwCheckQueued"),
		_T("FwCheckConnecting"),
		_T("FwCheckConnected"),
		_T("ServingBuddyQueued"),
		_T("ServedBuddyIncoming"),
		_T("ServingBuddyConnecting"),
		_T("ServingBuddyConnected"),
		_T("QueuedFWCheckUDP"),
		_T("FWCheckUDP"),
		_T("FwCheckConnectingUDP")
	};
	return (GetKadState() < _countof(apszState)) ? apszState[GetKadState()] : _T("*Unknown*");

}

CString CUpDownClient::DbgGetFullClientSoftVer() const
{
	CString aiVersion;
	if (GetAIModVersionString(aiVersion)) {
		CString formatted;
		formatted.Format(_T("eMule AI v%s"), (LPCTSTR)aiVersion);
		return formatted;
	}
	if (GetClientModVer().IsEmpty())
		return GetClientSoftVer();
	CString str;
	str.Format(_T("%s [%s]"), (LPCTSTR)GetClientSoftVer(), (LPCTSTR)GetClientModVer());
	return str;
}

bool CUpDownClient::GetAIModVersionString(CString& outVersion) const
{
	const CString& modVersion = GetClientModVer();
	CString modName = MOD_NAME;
	const int prefixLen = modName.GetLength();
	if (modVersion.GetLength() < prefixLen)
		return false;
	if (modVersion.Left(prefixLen).CompareNoCase(modName) != 0)
		return false;
	CString remainder = modVersion.Mid(prefixLen);
	remainder.Trim();
	if (!remainder.IsEmpty() && (remainder[0] == _T('v') || remainder[0] == _T('V'))) {
		remainder = remainder.Mid(1);
		remainder.Trim();
	}
	if (remainder.IsEmpty()) {
		CString fallback;
		fallback.Format(_T("%u.%u"), static_cast<unsigned>(MOD_MAIN_VER), static_cast<unsigned>(MOD_MIN_VER));
		remainder = fallback;
	}
	outVersion = remainder;
	return true;
}

CString CUpDownClient::DbgGetClientInfo(bool bFormatIP) const
{
	CString str;
	try {
		if (HasLowID()) {
			const CAddress& cIP = GetConnectIP();
			uint32 serverIPv4 = GetServerIP();
			CString left;
			if (serverIPv4 != 0)
				left = ipstr(serverIPv4);
			else if (!cIP.IsNull())
				left = ipstr(cIP);
			else {
				CAddress userIP = GetIP();
				left = userIP.IsNull() ? _T("0.0.0.0") : ipstr(userIP);
			}
			if (!cIP.IsNull()) {
				str.Format(_T("%u@%s (%s) '%s' (%s,%s/%s/%s)")
					, GetUserIDHybrid(), (LPCTSTR)left
					, (LPCTSTR)ipstr(cIP)
					, GetUserName()
					, (LPCTSTR)DbgGetFullClientSoftVer()
					, DbgGetDownloadState(), DbgGetUploadState(), DbgGetKadState());
			} else {
				str.Format(_T("%u@%s '%s' (%s,%s/%s/%s)")
					, GetUserIDHybrid(), (LPCTSTR)left
					, GetUserName()
					, (LPCTSTR)DbgGetFullClientSoftVer()
					, DbgGetDownloadState(), DbgGetUploadState(), DbgGetKadState());
			}
		} else {
			CAddress dispIP = GetConnectIP();
			if (dispIP.IsNull())
				dispIP = GetIP();
			CString ipStr = dispIP.IsNull() ? _T("0.0.0.0") : ipstr(dispIP);
			str.Format(bFormatIP ? _T("%-15s '%s' (%s,%s/%s/%s)") : _T("%s '%s' (%s,%s/%s/%s)")
				, (LPCTSTR)ipStr
				, GetUserName()
				, (LPCTSTR)DbgGetFullClientSoftVer()
				, DbgGetDownloadState(), DbgGetUploadState(), DbgGetKadState());
		}
	} catch (...) {
		str.Format(_T("%p - Invalid client instance"), this);
	}
	return str;
}

bool CUpDownClient::CheckHandshakeFinished() const
{
	if (m_bHelloAnswerPending) {
		// 24-Nov-2004 [bc]: The reason for this is that 2 clients are connecting to each other at the same time.
		return false;
	}

	return true;
}

void CUpDownClient::CheckForGPLEvilDoer()
{
	if (!m_strModVersion.IsEmpty()) {
		LPCTSTR pszModVersion = (LPCTSTR)m_strModVersion;

		// skip leading spaces
		while (*pszModVersion == _T(' '))
			++pszModVersion;

		// check for known major gpl breaker
		if (_tcsnicmp(pszModVersion, _T("LH"), 2) == 0 || _tcsnicmp(pszModVersion, _T("LIO"), 3) == 0 || _tcsnicmp(pszModVersion, _T("PLUS PLUS"), 9) == 0)
			m_bGPLEvildoer = true;
	}
}

void CUpDownClient::OnSocketConnected(int /*nErrorCode*/)
{
}

CString CUpDownClient::GetDownloadStateDisplayString() const
{
	LPCTSTR uid;
	switch (GetDownloadState()) {
	case DS_CONNECTING:
		uid = _T("CONNECTING");
		break;
	case DS_CONNECTED:
		uid = _T("ASKING");
		break;
	case DS_WAITCALLBACK:
		uid = _T("CONNVIASERVER");
		break;
	case DS_ONQUEUE:
		uid = IsRemoteQueueFull() ? _T("QUEUEFULL") : _T("ONQUEUE");
		break;
	case DS_DOWNLOADING:
		uid = _T("TRANSFERRING");
		break;
	case DS_REQHASHSET:
		uid = _T("RECHASHSET");
		break;
	case DS_NONEEDEDPARTS:
		uid = _T("NONEEDEDPARTS");
		break;
	case DS_LOWTOLOWIP:
		uid = _T("NOCONNECTLOW2LOW");
		break;
	case DS_TOOMANYCONNS:
		uid = _T("TOOMANYCONNS");
		break;
	case DS_ERROR:
		uid = _T("ERROR");
		break;
	case DS_WAITCALLBACKKAD:
		uid = _T("KAD_WAITCBK");
		break;
	case DS_TOOMANYCONNSKAD:
		uid = _T("KAD_TOOMANDYKADLKPS");
		break;
	default:
		return CString();
	}
	return GetResString(uid);

}

CString CUpDownClient::GetUploadStateDisplayString() const
{
	LPCTSTR uid;
	switch (GetUploadState()) {
	case US_ONUPLOADQUEUE:
		uid = _T("ONQUEUE");
		break;
	case US_BANNED:
		uid = _T("BANNED");
		break;
	case US_CONNECTING:
		uid = _T("CONNECTING");
		break;
	case US_UPLOADING:
		if (thePrefs.IsExtControlsEnabled() && GetPayloadInBuffer() == 0)
			uid = _T("US_STALLEDW4BR");
		else if (GetSlotNumber() <= (UINT)theApp.uploadqueue->GetActiveUploadsCount())
			uid = _T("TRANSFERRING");
		else
			uid = _T("TRICKLING");
		break;
	default:
		return CString();
	}
	return GetResString(uid);
}

void CUpDownClient::SendPublicIPRequest()
{
	if (socket && socket->IsConnected()) {
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP_PublicIPReq", this);
		Packet *packet = new Packet(OP_PUBLICIP_REQ, 0, OP_EMULEPROT);
		theStats.AddUpDataOverheadOther(packet->size);
		SendPacket(packet);
		m_fNeedOurPublicIP = 1;
	}
}

void CUpDownClient::ProcessPublicIPAnswer(const BYTE *pbyData, UINT uSize)
{
	if (uSize != 4 && uSize != 20) // IPv4 [uint32 IPv4] // IPv6 [uint32 -1][16 bytes IPv6]
		throw GetResString(_T("ERR_WRONGPACKETSIZE"));

	if (m_fNeedOurPublicIP == 1) { // did we?
		m_fNeedOurPublicIP = 0;
		uint32 dwIP = PeekUInt32(pbyData);

		if (dwIP != -1) {
			if (theApp.GetPublicIPv4() == 0 && !::IsLowID(dwIP))
				theApp.SetPublicIPv4(dwIP);
		} else {
			byte uIP[16];
			memcpy(uIP, pbyData + 4, 16);
			// IPv6-TODO: add global IPV6 handling
			CAddress IPv6(uIP);
			dwIP = IPv6.ToUInt32(true);
			if (theApp.GetPublicIPv4() == 0 && dwIP != UINT_MAX && !::IsLowID(dwIP))
				theApp.SetPublicIPv4(dwIP);
			if (theApp.GetPublicIPv6().IsNull())
				theApp.SetPublicIPv6(IPv6);
		}
	}
}

void CUpDownClient::CheckFailedFileIdReqs(const uchar *aucFileHash)
{
	if (thePrefs.IsDetectFileScanner() == false)
		return;

	if (aucFileHash != NULL && (theApp.sharedfiles->IsUnsharedFile(aucFileHash) || theApp.downloadqueue->GetFileByID(aucFileHash)))
		return;
	{
		if (m_fFailedFileIdReqs < 6)// NOTE: Do not increase this counter without increasing the bits for 'm_fFailedFileIdReqs'
			++m_fFailedFileIdReqs;
		if (m_fFailedFileIdReqs == 6) {
			if (theApp.clientlist->GetBadRequests(this) < 2)
				theApp.clientlist->TrackBadRequest(this, 1);
			if (theApp.clientlist->GetBadRequests(this) == 2) {
				theApp.clientlist->TrackBadRequest(this, -2); // reset so the client will not be re-banned right after the ban is lifted
				Ban(GetResString(_T("PUNISHMENT_REASON_FILE_REQ_FLOOD")));
			}
			throwCStr(thePrefs.GetLogBannedClients() ? GetResString(_T("PUNISHMENT_REASON_FILE_REQ_FLOOD")) : EMPTY);
		}
	}
}

EUTF8str CUpDownClient::GetUnicodeSupport() const
{
	return m_bUnicodeSupport ? UTF8strRaw : UTF8strNone;
}

void CUpDownClient::SetSpammer(bool bVal)
{
	if (bVal)
		Ban(GetResString(_T("PUNISHMENT_REASON_SPAMMER")));
	else if (IsBanned() && m_fIsSpammer)
		UnBan();
	m_fIsSpammer = static_cast<int>(bVal);
}

void  CUpDownClient::SetMessageFiltered(bool bVal)
{
	m_fMessageFiltered = static_cast<int>(bVal);
}

bool  CUpDownClient::IsObfuscatedConnectionEstablished() const
{
	if (socket != NULL && socket->IsConnected()) {
		if (socket->HaveUtpLayer())
			return theApp.clientudp->IsObfusicating(GetConnectIP(), GetKadPort());
		else
			return socket->IsObfusicating();
	} else
		return false;
}

bool CUpDownClient::ShouldReceiveCryptUDPPackets() const
{
	return thePrefs.IsCryptLayerEnabled() && SupportsCryptLayer() && !theApp.GetPublicIP().IsNull()
		&& HasValidHash() && (thePrefs.IsCryptLayerPreferred() || RequestsCryptLayer());
}

void CUpDownClient::GetDisplayImage(int &iImage, UINT &uOverlayImage) const
{
	if (IsFriend())
		iImage = 4;
	else {
		bool bRatioGt1 = (credits && credits->GetScoreRatio(GetIP()) > 1);
		switch (GetClientSoft()) {
		case SO_EDONKEYHYBRID:
			iImage = bRatioGt1 ? 8 : 7;
			break;
		case SO_MLDONKEY:
			iImage = bRatioGt1 ? 6 : 5;
			break;
		case SO_SHAREAZA:
			iImage = bRatioGt1 ? 10 : 9;
			break;
		case SO_AMULE:
			iImage = bRatioGt1 ? 12 : 11;
			break;
		case SO_LPHANT:
			iImage = bRatioGt1 ? 14 : 13;
			break;
		case SO_URL:
			iImage = 15; //server icon
			break;
		default:
			if (ExtProtocolAvailable())
				iImage = bRatioGt1 ? 3 : 2;
			else
				iImage = bRatioGt1 ? 1 : 0;
		}
	}

	uOverlayImage = static_cast<UINT>((Credits() && Credits()->GetCurrentIdentState(GetIP()) == IS_IDENTIFIED));
	uOverlayImage |= (static_cast<UINT>(IsObfuscatedConnectionEstablished()) << 1);
}

void CUpDownClient::ProcessChatMessage(CSafeMemFile &data, uint32 nLength)
{
	//filter me?
	if ((thePrefs.MsgOnlyFriends() && !IsFriend()) || (thePrefs.MsgOnlySecure() && GetUserName() == NULL || (thePrefs.IsDetectSpam() && (IsBadClient())))) {
		if (!GetMessageFiltered())
			if (thePrefs.GetVerbose())
				AddDebugLogLine(false, _T("Filtered Message from '%s' (IP:%s)"), (LPCTSTR)EscPercent(GetUserName()), (LPCTSTR)ipstr(GetConnectIP()));

		SetMessageFiltered(true);
		return;
	}

	CString strMessage(data.ReadString(GetUnicodeSupport() != UTF8strNone, nLength));
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		Debug(_T("  %s\n"), (LPCTSTR)strMessage);

	// default filtering
	CString strMessageCheck(strMessage);

	if (GetUploadState() == US_BANNED)
		return; //just to be sure

	if (theApp.shield->CheckSpamMessage(this, strMessageCheck)) {
		theApp.emuledlg->chatwnd->chatselector.EndSession(this);
		return;
	}

	strMessageCheck.MakeLower();
	for (int iPos = 0; iPos >= 0;){
		CString sToken(thePrefs.GetMessageFilter().Tokenize(_T("|"), iPos));
		if (!sToken.Trim().IsEmpty() && strMessageCheck.Find(sToken.MakeLower()) >= 0) {
			if (thePrefs.IsAdvSpamfilterEnabled() && !IsFriend() && !GetMessagesSent()) {
				SetSpammer(true);
				theApp.emuledlg->chatwnd->chatselector.EndSession(this);
			}
			return;
		}
	}

	// advanced spam filter check
	if (thePrefs.IsChatCaptchaEnabled() && !IsFriend()) {
		// captcha checks outrank any further checks - if the captcha has been solved, we assume its no spam
		// first check if we need to sent a captcha request to this client
		if (GetMessagesSent() == 0 && GetMessagesReceived() == 0 && GetChatCaptchaState() != CA_CAPTCHASOLVED) {
			// we have never sent a message to this client, and no message from him has ever passed our filters
			if (GetChatCaptchaState() != CA_CHALLENGESENT) {
				// we also aren't currently expecting a captcha response
				if (m_fSupportsCaptcha != NULL) {
					// and he supports captcha, so send him on and store the message (without showing for now)
					if (m_cCaptchasSent < 3) { // no more than 3 tries
						m_strCaptchaPendingMsg = strMessage;
						CSafeMemFile fileAnswer(1024);
						fileAnswer.WriteUInt8(0); // no tags, for future use
						CCaptchaGenerator captcha(4);
						if (captcha.WriteCaptchaImage(fileAnswer)) {
							m_strCaptchaChallenge = captcha.GetCaptchaText();
							m_eChatCaptchaState = CA_CHALLENGESENT;
							++m_cCaptchasSent;
							Packet *packet = new Packet(fileAnswer, OP_EMULEPROT, OP_CHATCAPTCHAREQ);
							theStats.AddUpDataOverheadOther(packet->size);
							if (!SafeConnectAndSendPacket(packet))
								return; // deleted client while connecting
						} else {
							ASSERT(0);
							DebugLogError(_T("Failed to create Captcha for client %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
						}
					}
				} else {
					// client doesn't support captchas, but we require them, tell him that
					// it's not going to work out with an answer message (will not be shown
					// and doesn't count as sent message)
					if (m_cCaptchasSent < 1) { // don't send this notifier more than once
						++m_cCaptchasSent;
						// always send in English
						CString rstrMessage(_T("In order to avoid spam messages, this user requires you to solve a captcha before you can send a message to him. However your client does not support captchas, so you will not be able to chat with this user."));
						DebugLog(_T("Received message from client not supporting captcha, filtered and sent notifier (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
						SendChatMessage(rstrMessage); // may delete client
					} else
						DebugLog(_T("Received message from client not supporting captcha, filtered, didn't send notifier (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
				}
				return;
			}
			// this message must be the answer to the captcha request we send him, let's verify
			ASSERT(!m_strCaptchaChallenge.IsEmpty());
			if (m_strCaptchaChallenge.CompareNoCase(strMessage.Trim().Right(min(strMessage.GetLength(), m_strCaptchaChallenge.GetLength()))) != 0) {
				// wrong, cleanup and ignore
				DebugLogWarning(_T("Captcha answer failed (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
				m_eChatCaptchaState = CA_NONE;
				m_strCaptchaChallenge.Empty();
				m_strCaptchaPendingMsg.Empty();
				Packet *packet = new Packet(OP_CHATCAPTCHARES, 1, OP_EMULEPROT, false);
				packet->pBuffer[0] = (m_cCaptchasSent < 3) ? 1 : 2; // status response
				theStats.AddUpDataOverheadOther(packet->size);
				SafeConnectAndSendPacket(packet);
				return; // nothing more todo
			}

			// alright
			DebugLog(_T("Captcha solved, showing withheld message (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			m_eChatCaptchaState = CA_CAPTCHASOLVED; // this state isn't persistent, but the message counter will be used to determine later if the captcha has been solved
			// replace captcha answer with the withheld message and show it
			strMessage = m_strCaptchaPendingMsg;
			m_cCaptchasSent = 0;
			m_strCaptchaChallenge.Empty();
			Packet *packet = new Packet(OP_CHATCAPTCHARES, 1, OP_EMULEPROT, false);
			packet->pBuffer[0] = 0; // status response
			theStats.AddUpDataOverheadOther(packet->size);
			if (!SafeConnectAndSendPacket(packet)) {
				ASSERT(0); // deleted client while connecting
				return;
			}
		} else
			DEBUG_ONLY(DebugLog(_T("Message passed captcha filter - already solved or not needed (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo())));
	}
	if (thePrefs.IsAdvSpamfilterEnabled() && !IsFriend()) { // friends are never spammers... (but what if two spammers are friends :P )
		bool bIsSpam = IsSpammer();
		if (!bIsSpam) {
			// first fixed criteria: If a client sends me a URL in his first message before I response to him
			// there is a 99,9% chance that it is some poor guy advising his leech mod, or selling you... well you know :P
			if (GetMessagesSent() == 0) {
				const CString sURL(URLINDICATOR);
				for (int iPos = 0; iPos >= 0;) {
					const CString &sToken(sURL.Tokenize(_T("|"), iPos));
					if (!sToken.IsEmpty() && strMessage.Find(sToken) >= 0) {
						bIsSpam = true;
						break;
					}
				}

				// second fixed criteria: he sent me 4 or more messages and I didn't answer him once
				if (GetMessagesReceived() > 3)
					bIsSpam = true;
			}
		}
		if (bIsSpam) {
			if (IsSpammer()) {
				if (thePrefs.GetVerbose())
					AddProtectionLogLine(false, _T("'%s' has been marked as spammer"), (LPCTSTR)EscPercent(GetUserName()));
			}
			SetSpammer(true);
			theApp.emuledlg->chatwnd->chatselector.EndSession(this);
			return;
		}
	}

	theApp.emuledlg->chatwnd->chatselector.ProcessMessage(this, strMessage);
}

void CUpDownClient::ProcessCaptchaRequest(CSafeMemFile &data)
{
	// received a captcha request, check if we actually accept it (only after sending a message ourself to this client)
	if (GetChatCaptchaState() == CA_ACCEPTING && GetChatState() != MS_NONE
		&& theApp.emuledlg->chatwnd->chatselector.GetItemByClient(this) != NULL)
	{
		// read tags (for future use)
		for (uint32 i = data.ReadUInt8(); i > 0; --i)
			CTag tag(data, true);
		// sanitize checks - we want a small captcha not a wallpaper
		uint32 nSize = (uint32)(data.GetLength() - data.GetPosition());
		if (nSize > 128 && nSize < 4096) {
			ULONGLONG pos = data.GetPosition();
			BYTE *byBuffer = data.Detach();
			HBITMAP imgCaptcha = mem2bmp(&byBuffer[pos], nSize);
			BITMAPINFOHEADER *bi = (BITMAPINFOHEADER*)&byBuffer[pos + sizeof(BITMAPFILEHEADER)];
			if (imgCaptcha) {
				if (bi->biWidth > 10 && bi->biWidth < 150 && bi->biHeight > 10 && bi->biHeight < 50)
				{
					m_eChatCaptchaState = CA_CAPTCHARECV;
					theApp.emuledlg->chatwnd->chatselector.ShowCaptchaRequest(this, imgCaptcha);
					::DeleteObject(imgCaptcha);
				} else
					DebugLogWarning(_T("Received captcha request from client, processing image failed or invalid pixel size (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			} else
				DebugLogWarning(_T("Received captcha request from client, Creating bitmap failed (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		} else
			DebugLogWarning(_T("Received captcha request from client, size sanitize check failed (%u) (%s)"), nSize, (LPCTSTR)EscPercent(DbgGetClientInfo()));
	} else
		DebugLogWarning(_T("Received captcha request from client, but don't accepting it at this time (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
}

void CUpDownClient::ProcessCaptchaReqRes(uint8 nStatus)
{
	if (GetChatCaptchaState() == CA_SOLUTIONSENT && GetChatState() != MS_NONE
		&& theApp.emuledlg->chatwnd->chatselector.GetItemByClient(this) != NULL)
	{
		ASSERT(nStatus < 3);
		m_eChatCaptchaState = CA_NONE;
		theApp.emuledlg->chatwnd->chatselector.ShowCaptchaResult(this, GetResString((nStatus == 0) ? _T("CAPTCHASOLVED") : _T("CAPTCHAFAILED")));
	} else {
		m_eChatCaptchaState = CA_NONE;
		DebugLogWarning(_T("Received captcha result from client, but don't accepting it at this time (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
	}
}

CFriend* CUpDownClient::GetFriend() const
{
	if (m_Friend != NULL) {
		if (theApp.friendlist != NULL && theApp.friendlist->IsValid(m_Friend))
			return m_Friend;

		TRACE(_T("CUpDownClient::GetFriend: Clearing stale friend pointer (client=%p, friend=%p, closing=%d)\n"), this, m_Friend, theApp.IsClosing() ? 1 : 0);
		CUpDownClient* pThis = const_cast<CUpDownClient*>(this);
		pThis->m_Friend = NULL;
		pThis->SetFriendSlot(false);
	}
	return NULL;
}

void CUpDownClient::SendChatMessage(const CString &strMessage)
{
	CSafeMemFile data;
	data.WriteString(strMessage, GetUnicodeSupport());
	Packet *packet = new Packet(data, OP_EDONKEYPROT, OP_MESSAGE);
	theStats.AddUpDataOverheadOther(packet->size);
	SafeConnectAndSendPacket(packet);
}

bool CUpDownClient::HasPassedSecureIdent(bool bPassIfUnavailable) const
{
	return credits != NULL
		&& (credits->GetCurrentIdentState(GetConnectIP()) == IS_IDENTIFIED
			|| (credits->GetCurrentIdentState(GetConnectIP()) == IS_NOTAVAILABLE && bPassIfUnavailable));
}

void CUpDownClient::SendFirewallCheckUDPRequest()
{
	ASSERT(GetKadState() == KS_FWCHECK_UDP);
	if (!Kademlia::CKademlia::IsRunning()) {
		SetKadState(KS_NONE);
		return;
	}
	if (GetUploadState() != US_NONE || GetDownloadState() != DS_NONE || GetChatState() != MS_NONE
		|| GetKadVersion() <= KADEMLIA_VERSION5_48a || !GetKadPort())
	{
		Kademlia::CUDPFirewallTester::SetUDPFWCheckResult(false, true, GetIP().ToUInt32(true), 0); // inform the tester that this test was cancelled
		SetKadState(KS_NONE);
		return;
	}
	CSafeMemFile data;
	data.WriteUInt16(Kademlia::CKademlia::GetPrefs()->GetInternKadPort());
	data.WriteUInt16(Kademlia::CKademlia::GetPrefs()->GetExternalKadPort());
	data.WriteUInt32(Kademlia::CKademlia::GetPrefs()->GetUDPVerifyKey(GetConnectIP().ToUInt32(false)));
	Packet *packet = new Packet(data, OP_EMULEPROT, OP_FWCHECKUDPREQ);
	theStats.AddUpDataOverheadKad(packet->size);
	SafeConnectAndSendPacket(packet);
}

void CUpDownClient::ProcessFirewallCheckUDPRequest(CSafeMemFile &data)
{
	if (!Kademlia::CKademlia::IsRunning() || Kademlia::CKademlia::GetUDPListener() == NULL) {
		DebugLogWarning(_T("Ignored Kad Firewall request UDP because Kad is not running (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		return;
	}
	// first search if we know this IP already, if so the result might be biased and we need tell the requester
	bool bErrorAlreadyKnown = GetUploadState() != US_NONE || GetDownloadState() != DS_NONE || GetChatState() != MS_NONE
		|| (Kademlia::CKademlia::GetRoutingZone()->GetContact(GetConnectIP().ToUInt32(true), 0, false) != NULL);

	uint16 nRemoteInternPort = data.ReadUInt16();
	uint16 nRemoteExternPort = data.ReadUInt16();
	uint32 dwSenderKey = data.ReadUInt32();
	if (nRemoteInternPort == 0) {
		DebugLogError(_T("UDP Firewall check requested with Intern Port == 0 (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		return;
	}
	if (dwSenderKey == 0)
		DebugLogWarning(_T("UDP Firewall check requested with SenderKey == 0 (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));

	CSafeMemFile fileTestPacket1;
	fileTestPacket1.WriteUInt8(static_cast<uint8>(bErrorAlreadyKnown));
	fileTestPacket1.WriteUInt16(nRemoteInternPort);
	if (thePrefs.GetDebugClientKadUDPLevel() > 0)
		DebugSend("KADEMLIA2_FIREWALLUDP", GetConnectIP().ToUInt32(true), nRemoteInternPort);
	Kademlia::CKademlia::GetUDPListener()->SendPacket(fileTestPacket1, KADEMLIA2_FIREWALLUDP, GetConnectIP().ToUInt32(true)
		, nRemoteInternPort, Kademlia::CKadUDPKey(dwSenderKey, theApp.GetPublicIPv4()), NULL);

	// if the client has a router with PAT (and therefore a different extern port than intern), test this port too
	if (nRemoteExternPort != 0 && nRemoteExternPort != nRemoteInternPort) {
		CSafeMemFile fileTestPacket2;
		fileTestPacket2.WriteUInt8(static_cast<uint8>(bErrorAlreadyKnown));
		fileTestPacket2.WriteUInt16(nRemoteExternPort);
		if (thePrefs.GetDebugClientKadUDPLevel() > 0)
			DebugSend("KADEMLIA2_FIREWALLUDP", GetConnectIP().ToUInt32(true), nRemoteExternPort);
		Kademlia::CKademlia::GetUDPListener()->SendPacket(fileTestPacket2, KADEMLIA2_FIREWALLUDP, GetConnectIP().ToUInt32(true)
			, nRemoteExternPort, Kademlia::CKadUDPKey(dwSenderKey, theApp.GetPublicIPv4()), NULL);
	}
	DebugLog(_T("Answered UDP Firewall check request (%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
}

const uint8 CUpDownClient::GetConnectOptions(const bool bEncryption, const bool bCallback, const bool bNATTraversal) const  // by WiZaRd
{
	// ConnectSettings - SourceExchange V4
		// 4 Reserved (!)
		// 1 DirectCallback Supported/Available
		// 1 CryptLayer Required
		// 1 CryptLayer Requested
		// 1 CryptLayer Supported
	const uint8 uSupportsCryptLayer = (bEncryption && SupportsCryptLayer()) ? 1 : 0;
	const uint8 uRequestsCryptLayer = (bEncryption && RequestsCryptLayer()) ? 1 : 0;
	const uint8 uRequiresCryptLayer = (bEncryption && RequiresCryptLayer()) ? 1 : 0;
	const uint8 uDirectUDPCallback = (bCallback && SupportsDirectUDPCallback()) ? 1 : 0;
	const uint8 uSupportsNatTraversal = (bNATTraversal && GetNatTraversalSupport()) ? 1 : 0;
	uint8 byCryptOptions = (uSupportsNatTraversal << 7) | (uDirectUDPCallback << 3) | (uRequiresCryptLayer << 2) | (uRequestsCryptLayer << 1) | (uSupportsCryptLayer << 0);
	return byCryptOptions;
}

void CUpDownClient::SetConnectOptions(uint8 byOptions, bool bEncryption, bool bCallback)
{
	SetCryptLayerSupport((byOptions & 0x01) != 0 && bEncryption);
	SetCryptLayerRequest((byOptions & 0x02) != 0 && bEncryption);
	SetCryptLayerRequires((byOptions & 0x04) != 0 && bEncryption);
	SetDirectUDPCallbackSupport((byOptions & 0x08) != 0 && bCallback);
	SetNatTraversalSupport((byOptions & 0x80) != 0 && bCallback);
}

void CUpDownClient::SendSharedDirectories()
{
	//TODO: Don't send empty shared directories
	theApp.sharedfiles->ResetPseudoDirNames(); //purge stale data
	// add shared directories
	CStringArray arFolders;
	CStringList sharedDirs;
	thePrefs.CopySharedDirectoryList(sharedDirs);
	for (POSITION pos = sharedDirs.GetHeadPosition(); pos != NULL;) {
		const CString &strDir(theApp.sharedfiles->GetPseudoDirName(sharedDirs.GetNext(pos)));
		if (!strDir.IsEmpty())
			arFolders.Add(strDir);
	}
	//add categories
	for (INT_PTR iCat = 0; iCat < thePrefs.GetCatCount(); ++iCat) {
		const CString &strDir(theApp.sharedfiles->GetPseudoDirName(thePrefs.GetCategory(iCat)->strIncomingPath));
		if (!strDir.IsEmpty())
			arFolders.Add(strDir);
	}

	// add temporary folder if there are any temp files
	if (theApp.downloadqueue->GetFileCount() > 0)
		arFolders.Add(_T(OP_INCOMPLETE_SHARED_FILES));
	// add "Other" folder (for single shared files) if there are any single shared files
	if (theApp.sharedfiles->ProbablyHaveSingleSharedFiles())
		arFolders.Add(_T(OP_OTHER_SHARED_FILES));

	// build packet
	EUTF8str eUnicode = GetUnicodeSupport();
	CSafeMemFile tempfile(80);
	tempfile.WriteUInt32(static_cast<uint32>(arFolders.GetCount()));
	for (INT_PTR i = 0; i < arFolders.GetCount(); ++i)
		tempfile.WriteString(arFolders[i], eUnicode);

	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_AskSharedDirsAnswer", this);
	Packet *replypacket = new Packet(tempfile);
	replypacket->opcode = OP_ASKSHAREDDIRSANS;
	theStats.AddUpDataOverheadOther(replypacket->size);
	VERIFY(SendPacket(replypacket, true));
}

const CString CUpDownClient::GetGeolocationData(const bool bForceCountryCity) const {
	return theApp.geolite2->GetGeolocationData(m_structClientGeolocationData, bForceCountryCity);
}

int CUpDownClient::GetCountryFlagIndex() const {
	return m_structClientGeolocationData.FlagIndex;
}

void CUpDownClient::ResetGeoLite2() {
	m_structClientGeolocationData = theApp.geolite2->QueryGeolocationData(!m_UserIP.IsNull() ? m_UserIP : m_ConnectIP);
}

void CUpDownClient::ResetGeoLite2(const CAddress& IP) {
	m_structClientGeolocationData = theApp.geolite2->QueryGeolocationData(IP);
}


uint8 CUpDownClient::IsBadClient() const {
	if (m_uBadClientCategory > PR_NOTBADCLIENT) {
		time_t tCurrTime = time(NULL);
		if (m_uPunishment <= P_USERHASHBAN) { // If this is user hash ban or IP ban
			if (tCurrTime >= m_tPunishmentStartTime + thePrefs.GetClientBanTime())
				return PR_NOTBADCLIENT;
		} else if (tCurrTime >= m_tPunishmentStartTime + thePrefs.GetClientScoreReducingTime())
			return PR_NOTBADCLIENT;
	}
	return m_uBadClientCategory;
}

void CUpDownClient::ProcessBanMessage()
{
	if (!m_strPunishmentMessage.IsEmpty())
	{
		AddProtectionLogLine(false, (LPCTSTR)EscPercent(m_strPunishmentMessage));
		theApp.emuledlg->transferwnd->GetQueueList()->RefreshClient(this);
		m_strPunishmentMessage.Empty();
	}
}

void CUpDownClient::CheckFileNotFound() {
	if (m_reqfile && GetUploadState() != US_NONE && m_reqfile->GetFileSize() > PARTSIZE) {
		CKnownFile* upfile = theApp.sharedfiles->GetFileByID(GetUploadFileID());
		if (upfile && upfile == m_reqfile) { // We speak about the same file
			m_fileFNF = m_reqfile; // We just mark the file and don't ban now, the client may have unshared the file or something similar

			if (GetUploadState() != US_UPLOADING)
				theApp.uploadqueue->RemoveFromUploadQueue(this, _T("Source says he doesn't have the file he's downloading!"));

			theApp.uploadqueue->RemoveFromWaitingQueue(this);
		}
		else if (!m_reqfile)
			m_fileFNF = NULL;
	}
}

bool CUpDownClient::CheckFileRequest(CKnownFile* file) {
	// if the client asks again for the file he claimed to can't find but also had asked for it before, he is for sure lying
	if (m_fileFNF == file) { // got you, you'v said you wouldn't have it
		theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_FILE_FAKER")), PR_FILEFAKER);
		return true;
	}

	return false;
}

void CUpDownClient::SetOldUploadFileID() {
	if (requpfileid)
		md4cpy(oldfileid, requpfileid);
}

float CUpDownClient::GetModVersionNumber(CString modversion) const
{
	uint8 m_uModLength = (uint8)(CString(MOD_NAME)).GetLength();
	if (modversion.GetLength() < m_uModLength)
		return 0.0f;

	// remark: when the first letters do not equal the modstring it's allright!
	if (modversion.Left(m_uModLength).CompareNoCase(CString(MOD_NAME)) != 0)
		return 0.0f;

	return (float)_tstof(modversion.Mid(m_uModLength));
}

const CString CUpDownClient::GetPunishmentReason() const
{
	CString sText;
	if (IsBadClient())
		sText.Format(_T("%s"), m_strPunishmentReason);
	else
		sText = _T("-");
	return sText;
}

const CString CUpDownClient::GetPunishmentText() const
{
	CString sText = _T("-");
	if (m_bUploaderPunishmentPreventionActive)
		sText = GetResString(_T("UPLOADER_PUNISHMENT_PREVENTION"));
	else if (thePrefs.IsDontPunishFriends() && IsFriend()) {
		sText = GetResString(_T("FRIEND_PUNISHMENT_PREVENTION"));
	} else if (IsBadClient()) {
		if (m_uPunishment == P_IPUSERHASHBAN)
			sText = GetResString(_T("IP_BAN"));
		else if (m_uPunishment == P_USERHASHBAN)
			sText = GetResString(_T("USER_HASH_BAN"));
		else if (m_uPunishment == P_UPLOADBAN)
			sText = GetResString(_T("UPLOAD_BAN"));
		else if (m_uPunishment == P_SCOREX01)
			sText = GetResString(_T("SCORE_01"));
		else if (m_uPunishment == P_SCOREX02)
			sText = GetResString(_T("SCORE_02"));
		else if (m_uPunishment == P_SCOREX03)
			sText = GetResString(_T("SCORE_03"));
		else if (m_uPunishment == P_SCOREX04)
			sText = GetResString(_T("SCORE_04"));
		else if (m_uPunishment == P_SCOREX05)
			sText = GetResString(_T("SCORE_05"));
		else if (m_uPunishment == P_SCOREX06)
			sText = GetResString(_T("SCORE_06"));
		else if (m_uPunishment == P_SCOREX07)
			sText = GetResString(_T("SCORE_07"));
		else if (m_uPunishment == P_SCOREX08)
			sText = GetResString(_T("SCORE_08"));
		else if (m_uPunishment == P_SCOREX09)
			sText = GetResString(_T("SCORE_09"));
	}
	return sText;
}

const CString CUpDownClient::EmulateUserName(const bool m_bReturnUserName) const
{
	//if we know the remote client's user name, let check his user name and emulate his community tags.
	//Those community clients may have anti "user name copy" feature, so we'll only use tags we've already saved before
	//and learnt by counting them during current session. For the second case, if thecount is more than the defined treshold,
	//we'll emulate the tag. If these conditons aren't met, we'll remove the tag from the user name and use the remaining.
	const CAddress& m_uClientIP = !GetIP().IsNull() ? GetIP() : GetConnectIP();
	if (!m_uClientIP.IsNull() && GetUserName() != NULL && _tcslen(GetUserName()) > 0) { //We need IP to attach tags while counting. Stop processing without a valid IP or user name.
		std::wstring m_usernameToAnalize = GetUserName();
		//Process only if the string includes a tag
		if ((m_usernameToAnalize.find('(') != std::wstring::npos && m_usernameToAnalize.find(')') != std::wstring::npos) ||
			(m_usernameToAnalize.find('[') != std::wstring::npos && m_usernameToAnalize.find(']') != std::wstring::npos) ||
			(m_usernameToAnalize.find('{') != std::wstring::npos && m_usernameToAnalize.find('}') != std::wstring::npos) ||
			(m_usernameToAnalize.find('<') != std::wstring::npos && m_usernameToAnalize.find('>') != std::wstring::npos))
		{
			bool m_bAllTagsStripped = true;
			std::wstring m_usernameStripped = m_usernameToAnalize;
			CString m_strClientIP;
			m_strClientIP.Format(_T("%i"), m_uClientIP);
			CString m_strClientHash = md4str(GetUserHash());

			std::wregex pattern(_T("\\(.*?\\)|\\[.*?\\]|\\{.*?\\}|\\<.*?\\>"));
			auto words_begin = std::wsregex_iterator(m_usernameToAnalize.begin(), m_usernameToAnalize.end(), pattern);
			auto words_end = std::wsregex_iterator();

				for (std::wsregex_iterator i = words_begin; i != words_end; ++i) {
					std::wsmatch m_strMatch = *i;
					CString m_cstrMatch(m_strMatch.str().c_str());
					if (theAntiNickClass.IsReservedAntiUserNameThiefTag(m_cstrMatch)) {
						// Do not learn/emulate anti-thief tags in community emulation.
						if (m_bReturnUserName) {
							std::size_t m_Position = m_usernameStripped.find(m_strMatch.str());
							if (m_Position > 0 && m_usernameStripped[m_Position - 1] == ' ') // Strip leading space, too
								m_usernameStripped.erase(m_Position - 1, m_strMatch.length() + 1);
							else if (m_Position + m_strMatch.length() < m_usernameStripped.length() && m_usernameStripped[m_Position + m_strMatch.length() + 1] == ' ') // Strip trailing space, too
								m_usernameStripped.erase(m_Position, m_strMatch.length() + 1);
							else
								m_usernameStripped.erase(m_Position, m_strMatch.length());
						}
						continue;
					}

					uint32 m_iCount;
					if (thePrefs.m_CommunityTagCounterMap.Lookup(m_cstrMatch, m_iCount) != NULL) {
						if (m_strMatch.str().length() > 2) { //Ignore empty tags: DreaMule can generates these type [] tags and may be it ban/punish whoever copy it?
						if (m_iCount == 0) {
							m_bAllTagsStripped = false;
							continue; // This is already saved/confirmed tag, so we can use it in the user name.
						} else if (thePrefs.m_CommunityTagIpHashSet.find(m_strClientHash) == thePrefs.m_CommunityTagIpHashSet.end() && thePrefs.m_CommunityTagIpHashSet.find(m_strClientIP) == thePrefs.m_CommunityTagIpHashSet.end()) {
							// This is the first time we've seen this tag on this client. So add client hash and IP to the set and increment count in the map.
							thePrefs.m_CommunityTagIpHashSet.insert(m_strClientHash);
							thePrefs.m_CommunityTagIpHashSet.insert(m_strClientIP);
							if (++m_iCount >= thePrefs.GetEmulateCommunityTagSavingTreshold()) { // Increment and check for the treshold
								thePrefs.m_CommunityTagCounterMap[m_cstrMatch] = 0; // We reached to the treshold. Set count to the special value 0 and use this tag in the user name.
								if (thePrefs.IsLogEmulator()) {
									CString buffer;
									buffer.Format(_T("[EMULATE COMMUNITY] New tag was added to the emulation list: %s"), (LPCTSTR)EscPercent(m_cstrMatch));
									DebugLog(LOG_INFO | DLP_VERYLOW, buffer);
								}
								m_bAllTagsStripped = false;
								continue;
							} else
								thePrefs.m_CommunityTagCounterMap[m_cstrMatch] = m_iCount;
						}
					}
				} else {
					// This is the first time we've seen this tag. So add client hash and IP to the set and add tag/count int the map.
					thePrefs.m_CommunityTagIpHashSet.insert(m_strClientHash);
					thePrefs.m_CommunityTagIpHashSet.insert(m_strClientIP);
					thePrefs.m_CommunityTagCounterMap[m_cstrMatch] = 1;
				}

				if (m_bReturnUserName) {
					//We didn't quit loop since the tag isn't a saved one or the treshold isn't reached. So we'll remove the dat from the user name
					std::size_t m_Position = m_usernameStripped.find(m_strMatch.str());
					if (m_Position > 0 && m_usernameStripped[m_Position - 1] == ' ') // Strip leading space, too
						m_usernameStripped.erase(m_Position - 1, m_strMatch.length() + 1);
					else if (m_Position + m_strMatch.length() < m_usernameStripped.length() && m_usernameStripped[m_Position + m_strMatch.length() + 1] == ' ') // Strip trailing space, too
						m_usernameStripped.erase(m_Position, m_strMatch.length() + 1);
					else
						m_usernameStripped.erase(m_Position, m_strMatch.length());
				}
			}

			if (m_bReturnUserName) {
				if 	(m_bAllTagsStripped)
					return GenerateUserName();
				else {
					CString m_cstrEmulatedUserName(m_usernameStripped.c_str());
					if (thePrefs.IsLogEmulator()) { //Write community emulations to the log
						CString buffer;
						buffer.Format(_T("[EMULATE COMMUNITY] Original user name: %s - User name sent: %s"), (LPCTSTR)EscPercent(GetUserName()), (LPCTSTR)EscPercent(m_cstrEmulatedUserName));
						DebugLog(LOG_INFO | DLP_VERYLOW, buffer);
					}
					return m_cstrEmulatedUserName;
				}
			} else
				return NULL;
		}
	}

	if (m_bReturnUserName)
		return GenerateUserName();
	else 
		return NULL;
}

const CString CUpDownClient::GenerateUserName() const
{
	CString m_strGeneratedUserName;
	//always send the "lure" tag! We don't have to check the guy if he is a nick thief since we know he uses a GPLEvildoer
	m_strAntiUserNameThiefNick = theAntiNickClass.GetAntiUserNameThiefNick();// <== AntiUserNameThief [WiZaRd] - Stulle
	m_strGeneratedUserName.Format(_T("%s %s"), thePrefs.GetUserNick(), m_strAntiUserNameThiefNick);
	m_strGeneratedUserName.AppendFormat(_T("\x20\xAB%s\xBB"), MOD_NAME); // <== AntiUserNameThief [WiZaRd] - Stulle
	return m_strGeneratedUserName;
}

void CUpDownClient::LoadFromFile(CFileDataIO& file)
{	
	m_bIsArchived = true;
	file.ReadHash16(m_achUserHash);
	tLastSeen = file.ReadUInt64();

	for (uint32 tagcount = file.ReadUInt32(); tagcount > 0; --tagcount) {
		const CTag* newtag = new CTag(file, false);
		switch (newtag->GetNameID()) {
		case CL_NAME: {
			ASSERT(newtag->IsStr());
			if (newtag->IsStr())
				SetUserName(newtag->GetStr());
			break;
		}
		case CL_CONNECTIP: {
			ASSERT(newtag->IsStr());
			if (newtag->IsStr())
				m_ConnectIP.FromString(newtag->GetStr(), false);
			break;
		}
		case CL_USERIP: {
			ASSERT(newtag->IsStr());
			if (newtag->IsStr())
				m_UserIP.FromString(newtag->GetStr(), false);
			break;
		}
		case CL_SERVERIP: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_dwServerIP = newtag->GetInt();
			break;
		}
		case CL_USERIDHYBRID: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_nUserIDHybrid = newtag->GetInt();
			break;
		}
		case CL_USERPORT: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_nUserPort = newtag->GetInt();
			break;
		}
		case CL_SERVERPORT: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_nServerPort = newtag->GetInt();
			break;
		}
		case CL_CLIENTVERSION: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_nClientVersion = newtag->GetInt();
			break;
		}
		case CL_EMULEVERSION: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_byEmuleVersion = newtag->GetInt();
			break;
		}
		case CL_EMULEPROTOCOL: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_bEmuleProtocol = newtag->GetInt();
			break;
		}
		case CL_ISHYBRID: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_bIsHybrid = newtag->GetInt();
			break;
		}
		case CL_UDPPORT: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_nUDPPort = newtag->GetInt();
			break;
		}
		case CL_KADPORT: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_nKadPort = newtag->GetInt();
			break;
		}
		case CL_UDPVER: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_byUDPVer = newtag->GetInt();
			break;
		}
		case CL_COMPATIBLECLIENT: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_byCompatibleClient = newtag->GetInt();
			break;
		}
		case CL_ISML: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_bIsML = newtag->GetInt();
			break;
		}
		case CL_GPLEVILDOER: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_bGPLEvildoer = newtag->GetInt();
			break;
		}
		case CL_CLIENTSOFTWARE: {
			ASSERT(newtag->IsStr());
			if (newtag->IsStr())
				SetClientSoftVer(newtag->GetStr());
			break;
		}
		case CL_MODVERSION: {
			ASSERT(newtag->IsStr());
			if (newtag->IsStr())
				m_strModVersion = newtag->GetStr();
			break;
		}
		case CL_MESSAGESRECEIVED: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_cMessagesReceived = newtag->GetInt();
			break;
		}
		case CL_MESSAGESSENT: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_cMessagesSent = newtag->GetInt();
			break;
		}
		case CL_KADVERSION: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_byKadVersion = newtag->GetInt();
			break;
		}
		case CL_NOVIEWSHAREDFILES: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_fNoViewSharedFiles = newtag->GetInt();
			break;
		}
		case CL_FIRSTSEEN: {
			ASSERT(newtag->IsInt64());
			if (newtag->IsInt64())
				tFirstSeen = newtag->GetInt64();
			break;
		}
		case CL_PUNISHMENT: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_uPunishment = newtag->GetInt();
			break;
		}
		case CL_BADCLIENTCATEGORY: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_uBadClientCategory = newtag->GetInt();
			break;
		}
		case CL_PUNISHMENTSTARTIME: {
			ASSERT(newtag->IsInt64());
			if (newtag->IsInt64())
				m_tPunishmentStartTime = newtag->GetInt64();
			break;
		}
		case CL_PUNISHMENTREASON: {
			ASSERT(newtag->IsStr());
			if (newtag->IsStr())
				m_strPunishmentReason = newtag->GetStr();
			break;
		}
		case CL_SHAREDFILESSTATUS: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_uSharedFilesStatus = newtag->GetInt();
			break;
		}
		case CL_SHAREDFILESLASTQUERIEDTIME: {
			ASSERT(newtag->IsInt64());
			if (newtag->IsInt64())
				m_tSharedFilesLastQueriedTime = newtag->GetInt64();
			break;
		}
		case CL_SHAREDFILESCOUNT: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_uSharedFilesCount = newtag->GetInt();
			break;
		}
		case CL_CLIENTNOTE: {
			ASSERT(newtag->IsStr());
			if (newtag->IsStr())
				m_strClientNote = newtag->GetStr();
			break;
		}
		case CL_AUTOQUERYSHAREDFILES: {
			ASSERT(newtag->IsInt());
			if (newtag->IsInt())
				m_bAutoQuerySharedFiles = newtag->GetInt();
			break;
		}
		}
		delete newtag;
	}
	
	SetIP(!m_UserIP.IsNull() ? m_UserIP : m_ConnectIP);
	InitClientSoftwareVersion();

	m_Friend = theApp.friendlist->SearchFriend(m_achUserHash, m_UserIP, m_nUserPort);
	if (m_Friend != NULL)
		m_Friend->SetLinkedClient(this); // Link the friend to that client
	else
		SetFriendSlot(false); // avoid that an unwanted client instance keeps a friend slot
}

void CUpDownClient::WriteToFile(CFileDataIO& file)
{
	if (IsCorruptOrBadUserHash(m_achUserHash)) // We only write clients with a valid user hash value.
		return;

	file.WriteHash16(m_achUserHash);
	file.WriteUInt64((uint64)tLastSeen);

	uint32 uTagCount = 0;
	ULONGLONG uTagCountFilePos = file.GetPosition();
	file.WriteUInt32(0);

	if (m_pszUsername != NULL && _tcslen(m_pszUsername)) {
		CString strBuffer(m_pszUsername);
		CTag nametag(CL_NAME, strBuffer);
		nametag.WriteTagToFile(file, UTF8strOptBOM);
		++uTagCount;
	}

	CTag connnectiptag(CL_CONNECTIP, ipstr(m_ConnectIP));
	connnectiptag.WriteTagToFile(file);
	++uTagCount;

	CTag useriptag(CL_USERIP, ipstr(m_UserIP));
	useriptag.WriteTagToFile(file);
	++uTagCount;

	CTag serveriptag(CL_SERVERIP, m_dwServerIP);
	serveriptag.WriteTagToFile(file);
	++uTagCount;
	
	CTag useridhybridtag(CL_USERIDHYBRID, m_nUserIDHybrid);
	useridhybridtag.WriteTagToFile(file);
	++uTagCount;

	CTag userporttag(CL_USERPORT, m_nUserPort);
	userporttag.WriteTagToFile(file);
	++uTagCount;

	CTag serverporttag(CL_SERVERPORT, m_nServerPort);
	serverporttag.WriteTagToFile(file);
	++uTagCount;

	CTag clientversiontag(CL_CLIENTVERSION, m_nClientVersion);
	clientversiontag.WriteTagToFile(file);
	++uTagCount;

	CTag emuleversiontag(CL_EMULEVERSION, m_byEmuleVersion);
	emuleversiontag.WriteTagToFile(file);
	++uTagCount;

	CTag emuleprotocoltag(CL_EMULEPROTOCOL, m_bEmuleProtocol);
	emuleprotocoltag.WriteTagToFile(file);
	++uTagCount;

	CTag ishybridtag(CL_ISHYBRID, m_bIsHybrid);
	ishybridtag.WriteTagToFile(file);
	++uTagCount;

	CTag isudpporttag(CL_UDPPORT, m_nUDPPort);
	isudpporttag.WriteTagToFile(file);
	++uTagCount;

	CTag iskadporttag(CL_KADPORT, m_nKadPort);
	iskadporttag.WriteTagToFile(file);
	++uTagCount;

	CTag udpvertag(CL_UDPVER, m_byUDPVer);
	udpvertag.WriteTagToFile(file);
	++uTagCount;

	CTag compatibleclienttag(CL_COMPATIBLECLIENT, m_byCompatibleClient);
	compatibleclienttag.WriteTagToFile(file);
	++uTagCount;

	CTag ismltag(CL_ISML, m_bIsML);
	ismltag.WriteTagToFile(file);
	++uTagCount;

	CTag gplevildoertag(CL_GPLEVILDOER, m_bGPLEvildoer);
	gplevildoertag.WriteTagToFile(file);
	++uTagCount;

	if (!m_strClientSoftware.IsEmpty()) {
		CTag clientsoftwaretag(CL_CLIENTSOFTWARE, m_strClientSoftware);
		clientsoftwaretag.WriteTagToFile(file, UTF8strOptBOM);
		++uTagCount;
	}

	if (!m_strModVersion.IsEmpty()) {
		CTag modversiontag(CL_MODVERSION, m_strModVersion);
		modversiontag.WriteTagToFile(file, UTF8strOptBOM);
		++uTagCount;
	}

	CTag messagesreceivedtag(CL_MESSAGESRECEIVED, m_cMessagesReceived);
	messagesreceivedtag.WriteTagToFile(file);
	++uTagCount;

	CTag messagessenttag(CL_MESSAGESSENT, m_cMessagesSent);
	messagessenttag.WriteTagToFile(file);
	++uTagCount;

	CTag kadversiontag(CL_KADVERSION, m_byKadVersion);
	kadversiontag.WriteTagToFile(file);
	++uTagCount;

	CTag noviewsharedfilestag(CL_NOVIEWSHAREDFILES, m_fNoViewSharedFiles);
	noviewsharedfilestag.WriteTagToFile(file);
	++uTagCount;

	CTag firstseentag(CL_FIRSTSEEN, tFirstSeen);
	firstseentag.WriteTagToFile(file);
	++uTagCount;

	CTag punishmenttag(CL_PUNISHMENT, m_uPunishment);
	punishmenttag.WriteTagToFile(file);
	++uTagCount;

	CTag badclientcategorytag(CL_BADCLIENTCATEGORY, m_uBadClientCategory);
	badclientcategorytag.WriteTagToFile(file);
	++uTagCount;

	CTag punishedtimetag(CL_PUNISHMENTSTARTIME, m_tPunishmentStartTime);
	punishedtimetag.WriteTagToFile(file);
	++uTagCount;

	if (!m_strPunishmentReason.IsEmpty()) {
		CTag punishmentreasontag(CL_PUNISHMENTREASON, m_strPunishmentReason);
		punishmentreasontag.WriteTagToFile(file, UTF8strOptBOM);
		++uTagCount;
	}

	CTag sharedfilesstatustag(CL_SHAREDFILESSTATUS, m_uSharedFilesStatus);
	sharedfilesstatustag.WriteTagToFile(file);
	++uTagCount;

	CTag sharedfileslastqueriedtimetag(CL_SHAREDFILESLASTQUERIEDTIME, m_tSharedFilesLastQueriedTime);
	sharedfileslastqueriedtimetag.WriteTagToFile(file);
	++uTagCount;

	CTag sharedfilescounttag(CL_SHAREDFILESCOUNT, m_uSharedFilesCount);
	sharedfilescounttag.WriteTagToFile(file);
	++uTagCount;

	if (!m_strClientNote.IsEmpty()) {
		CTag clientnotetag(CL_CLIENTNOTE, m_strClientNote);
		clientnotetag.WriteTagToFile(file, UTF8strOptBOM);
		++uTagCount;
	}

	CTag autoquerysharedfiles(CL_AUTOQUERYSHAREDFILES, m_bAutoQuerySharedFiles);
	autoquerysharedfiles.WriteTagToFile(file);
	++uTagCount;

	file.Seek(uTagCountFilePos, CFile::begin);
	file.WriteUInt32(uTagCount);
	file.Seek(0, CFile::end);
}

void CUpDownClient::SetClientNote()
{
	if (!thePrefs.GetClientHistory())
		if (CDarkMode::MessageBox(GetResString(_T("CLIENT_HISTORY_REQUIRED_MSG")), MB_YESNO | MB_ICONQUESTION) == IDYES)
			thePrefs.SetClientHistory(true);
		else
			return;

	InputBox inputbox;
	CString m_strLabel;
	if (GetUserName() != NULL)
		m_strLabel.Format(GetResString(_T("QL_USERNAME")) + _T(": %s\n") + GetResString(_T("CD_UHASH2")) + _T(": %s"), GetUserName(), md4str(GetUserHash()));
	else
		m_strLabel.Format(GetResString(_T("CD_UHASH2")) + _T(": %s"), md4str(GetUserHash()));

	inputbox.SetLabels(GetResString(_T("EDIT_CLIENT_NOTE")), m_strLabel, m_strClientNote);
	inputbox.DoModal();
	if (!inputbox.WasCancelled() && !inputbox.GetInput().IsEmpty()) {
		m_strClientNote = inputbox.GetInput();
		theApp.emuledlg->transferwnd->GetClientList()->RefreshClient(this);
		theApp.emuledlg->transferwnd->GetUploadList()->RefreshClient(this);
		theApp.emuledlg->transferwnd->GetQueueList()->RefreshClient(this);
		theApp.emuledlg->transferwnd->GetDownloadClientsList()->RefreshClient(this);
		theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.UpdateTabHeader(0, md4str(GetUserHash()), false);
	}
}

void CUpDownClient::SetAutoQuerySharedFiles(const bool bActivate)
{
	if (!thePrefs.GetClientHistory())
		if (CDarkMode::MessageBox(GetResString(_T("CLIENT_HISTORY_REQUIRED_MSG")), MB_YESNO | MB_ICONQUESTION) == IDYES)
			thePrefs.SetClientHistory(true);
		else
			return;

#ifndef TESTMODE
	if (thePrefs.CanSeeShares() != vsfaEverybody)
		if (CDarkMode::MessageBox(GetResString(_T("SEE_MY_SHARED_FILES_REQUIRED_MSG")), MB_YESNO | MB_ICONQUESTION) == IDYES)
			thePrefs.m_iSeeShares = vsfaEverybody;
		else
			return;
#endif

	m_bAutoQuerySharedFiles = bActivate;
	CString m_strStatusText;
	if (bActivate)
		m_strStatusText.Format(GetResString(_T("AUTO_QUERY_ACTIVATED")), md4str(GetUserHash()));
	else
		m_strStatusText.Format(GetResString(_T("AUTO_QUERY_DEACTIVATED")), md4str(GetUserHash()));
	theApp.emuledlg->statusbar->SetText(m_strStatusText, SBarLog, 0);

	theApp.emuledlg->transferwnd->GetClientList()->RefreshClient(this);
	theApp.emuledlg->transferwnd->GetUploadList()->RefreshClient(this);
	theApp.emuledlg->transferwnd->GetQueueList()->RefreshClient(this);
	theApp.emuledlg->transferwnd->GetDownloadClientsList()->RefreshClient(this);
}

void CUpDownClient::SendIPChange()
{
	if (theApp.listensocket->TooManySockets() && !(socket && socket->IsConnected()))
		return;

	if (GetSendIP() == false)
		return;

	Packet* packet = NULL;

	if (IsIPv6Open()) { // This client has IPv6
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP__Change_Client_IP", this);
		packet = new Packet(OP_CHANGE_CLIENT_IP, MDX_DIGEST_SIZE);
		md4cpy(packet->pBuffer, theApp.GetPublicIPv6().Data()); // New IPv6
	} else if (!IsIPv6Open()) { // This client has IPv4
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP__Change_Client_Id", this);
		packet = new Packet(OP_CHANGE_CLIENT_ID, 8);
		PokeUInt32(packet->pBuffer, theApp.GetPublicIPv4()); // New ID
		PokeUInt32(packet->pBuffer + 4, theApp.serverconnect->IsConnected() ? theApp.serverconnect->GetCurrentServer()->GetIP() : 0x00000000); // New Server IP
	}

	theStats.AddUpDataOverheadOther(packet->size);
	SafeConnectAndSendPacket(packet);
	WeSentIP();
}

void CUpDownClient::WriteExtendedSourceExchangeData(CSafeMemFile& data) const //by WiZaRd
{
	CList<CTag*> tagList;

	if (HasLowID())	{
		if (SupportsDirectUDPCallback()) {
			tagList.AddTail(new CTag(CT_EMULE_ADDRESS, GetIPv4().ToUInt32(false)));
			tagList.AddTail(new CTag(CT_EMULE_UDPPORTS,	((UINT)GetKadPort() << 16) | ((UINT)GetUDPPort() << 0)));
		}

		if (!GetServingBuddyIP().IsNull()) {
			if (GetServingBuddyIP().GetType() == CAddress::IPv6)
				tagList.AddTail(new CTag(CT_EMULE_SERVINGBUDDYIPV6, GetServingBuddyIP().Data()));
			else
				tagList.AddTail(new CTag(CT_EMULE_SERVINGBUDDYIP, GetServingBuddyIP().ToUInt32(false)));
			tagList.AddTail(new CTag(CT_EMULE_SERVINGBUDDYUDP,	/* (RESERVED) */ ((UINT)GetServingBuddyPort())));
		}

		if (!isnulmd4(GetServingBuddyID()))
			tagList.AddTail(new CTag(CT_EMULE_SERVINGBUDDYID, GetServingBuddyID()));
	}

	if (GetServerIP() != 0) {
		tagList.AddTail(new CTag(CT_EMULE_SERVERIP, GetServerIP()));
		tagList.AddTail(new CTag(CT_EMULE_SERVERTCP, /* (RESERVED) */ ((UINT)GetServerPort())));
	}

	if (IsIPv6Open())
		tagList.AddTail(new CTag(CT_MOD_IP_V6, GetIPv6().Data()));

	data.WriteUInt8((uint8)tagList.GetCount()); // max 255 tags - plenty of fun for everyone ;)
	while (!tagList.IsEmpty()) {
		CTag* emTag = tagList.RemoveHead();
		if (emTag)
			emTag->WriteNewEd2kTag(data);
		delete emTag;
	}
}

// ServingBuddyPull: request buddy info from target via UDP (IPv6 preferred)
bool CUpDownClient::RequestServingBuddyInfo()
{
	// rate limit: one attempt per 15s, up to 3 tries per client
	DWORD now = ::GetTickCount();
	if (m_dwLastServingBuddyPullReq != 0 && (int)(now - m_dwLastServingBuddyPullReq) < SEC2MS(15))
		return false;

	if (m_uServingBuddyPullTries >= 3)
		return false;

	// Need a destination UDP endpoint
	uint16 targetPort = GetUDPPort() ? GetUDPPort() : GetKadPort();
	if (targetPort == 0)
		return false;

	CAddress targetIP;
	// Prefer global IPv6 if available
	if (!GetIPv6().IsNull())
		targetIP = GetIPv6();
	else if (!GetConnectIP().IsNull())
		targetIP = GetConnectIP();

	if (targetIP.IsNull())
		return false;

	// Build request payload
	CSafeMemFile data(64);
	const uint8 kVersion = 1;
	data.WriteUInt8(kVersion);
	data.WriteUInt8(0); // flags (reserved)
	uint32 nonce = (uint32)((((uint32)rand()) << 16) ^ (uint32)rand());	// Nonce: Number used once
	data.WriteUInt32(nonce);
	Kademlia::CUInt128 myKadID = Kademlia::CKademlia::GetPrefs()->GetKadID(); // Requester's KadID (needed so responder can compute pair-specific ServingBuddyID)
	data.WriteUInt128(myKadID);
	data.WriteUInt8(GetMyConnectOptions(true, true)); // Our connect options (hint)

	Packet* pkt = new Packet(data, OP_EMULEPROT);
	pkt->opcode = OP_SERVINGBUDDYPULL_REQ;
	theStats.AddUpDataOverheadOther(pkt->size);
	theApp.clientudp->SendPacket(pkt, targetIP, targetPort, false, NULL, false, 0); // Send unencrypted if no key yet
	m_dwLastServingBuddyPullReq = now;
	++m_uServingBuddyPullTries;

	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[ServingBuddyPull] Sent request to %s:%u for %s"), (LPCTSTR)ipstr(targetIP), targetPort, (LPCTSTR)EscPercent(DbgGetClientInfo()));

	return true;
}


const bool CUpDownClient::AnyCallbackAvailable() const {
	return 
		((SupportsDirectUDPCallback() && thePrefs.GetUDPPort() != 0 && !GetConnectIP().IsNull()) // Direct Callback
		|| (HasValidServingBuddyID() && Kademlia::CKademlia::IsConnected() && ((!GetServingBuddyIP().IsNull() && GetServingBuddyPort()) || m_reqfile != NULL)) // Kad Callback
		|| theApp.serverconnect->IsLocalServer(GetServerIP(), GetServerPort())); // Server Callback (works for LowID<->LowID on same server)
}

void CUpDownClient::SetIP(CAddress& val)   //Only use this when you know the real IP or when your clearing it. //>>> WiZaRd::IPv6 [Xanatos]
{
	if (val.Convert(CAddress::IPv4)) // Check if the IP is a mapped IPv4
		m_UserIPv4 = val;
	else
		m_UserIPv6 = val;
	UpdateIP(val);
	ResetGeoLite2(val);
}

void CUpDownClient::UpdateIP(const CAddress& val)
{
	m_ConnectIP = val;
	m_UserIP = val;
}

void CUpDownClient::SetIPv6(const CAddress& val)
{
	if (val.GetType() != CAddress::IPv4) {
		m_UserIPv6 = val;
		m_bOpenIPv6 = true;
		ResetGeoLite2(val);
	} else if (val.GetType() == CAddress::IPv4)
		ASSERT(0);
}

///////////////////////////////////////////////////////////////////////////////
// eServer Buddy helper methods
//

void CUpDownClient::SetObservedExternalUdpPort(uint16 port)
{
	if (port == 0) {
		m_nObservedExternalUdpPort = 0;
		m_dwObservedExternalUdpPortTime = 0;
		return;
	}

	m_nObservedExternalUdpPort = port;
	m_dwObservedExternalUdpPortTime = ::GetTickCount();
}

bool CUpDownClient::HasFreshObservedExternalUdpPort() const
{
	if (m_nObservedExternalUdpPort == 0 || m_dwObservedExternalUdpPortTime == 0)
		return false;

	return (DWORD)(::GetTickCount() - m_dwObservedExternalUdpPortTime) <= ESERVER_EXT_UDP_PORT_TTL;
}

namespace
{
	const uchar s_abyEServerBuddyMagicFileHash[MDX_DIGEST_SIZE] = ESERVERBUDDY_MAGIC_FILE_HASH_BYTES;

	static void AddUInt32BEToMD4(CMD4& md4, uint32 value)
	{
		uchar buffer[sizeof(uint32)] = {
			static_cast<uchar>((value >> 24) & 0xFF),
			static_cast<uchar>((value >> 16) & 0xFF),
			static_cast<uchar>((value >> 8) & 0xFF),
			static_cast<uchar>(value & 0xFF)
		};
		md4.Add(buffer, ARRAYSIZE(buffer));
	}

	static void AddUInt64BEToMD4(CMD4& md4, uint64 value)
	{
		uchar buffer[sizeof(uint64)] = {
			static_cast<uchar>((value >> 56) & 0xFF),
			static_cast<uchar>((value >> 48) & 0xFF),
			static_cast<uchar>((value >> 40) & 0xFF),
			static_cast<uchar>((value >> 32) & 0xFF),
			static_cast<uchar>((value >> 24) & 0xFF),
			static_cast<uchar>((value >> 16) & 0xFF),
			static_cast<uchar>((value >> 8) & 0xFF),
			static_cast<uchar>(value & 0xFF)
		};
		md4.Add(buffer, ARRAYSIZE(buffer));
	}

	static void AddMagicFileNameToMD4(CMD4& md4)
	{
		const LPCTSTR pszName = ESERVERBUDDY_MAGIC_FILE_NAME;
		for (const TCHAR* p = pszName; p != NULL && *p != _T('\0'); ++p) {
			const uchar byAscii = static_cast<uchar>((static_cast<uint32>(*p)) & 0x7F);
			md4.Add(&byAscii, sizeof(byAscii));
		}
	}

	static void AddUInt32LEToMD4(CMD4& md4, uint32 value)
	{
		uchar buffer[sizeof(uint32)] = {
			static_cast<uchar>(value & 0xFF),
			static_cast<uchar>((value >> 8) & 0xFF),
			static_cast<uchar>((value >> 16) & 0xFF),
			static_cast<uchar>((value >> 24) & 0xFF)
		};
		md4.Add(buffer, ARRAYSIZE(buffer));
	}

	static uint32 ReadMixedUInt32FromHash(const uchar hash[MDX_DIGEST_SIZE])
	{
		if (hash == NULL)
			return 0;
		return static_cast<uint32>(hash[0])
			| (static_cast<uint32>(hash[5]) << 8)
			| (static_cast<uint32>(hash[10]) << 16)
			| (static_cast<uint32>(hash[15]) << 24);
	}

	static uint32 NormalizeBucketIndex(bool bBootstrap, uint32 uBucket)
	{
		const uint32 uCount = bBootstrap ? ESERVERBUDDY_MAGIC_BOOTSTRAP_BUCKET_COUNT : ESERVERBUDDY_MAGIC_FINE_BUCKET_COUNT;
		if (uCount == 0)
			return 0;
		return uBucket % uCount;
	}
}

uint32 CUpDownClient::GetEServerBuddyMagicEpoch(time_t tNowUtc)
{
	if (tNowUtc == 0)
		tNowUtc = time(NULL);
	if (tNowUtc < 0)
		tNowUtc = 0;
	return static_cast<uint32>((static_cast<uint64>(tNowUtc)) / ESERVERBUDDY_MAGIC_EPOCH_SECONDS);
}

uint32 CUpDownClient::SelectEServerBuddyMagicBucket(bool bBootstrap, uint32 uEpoch, const uchar* pSeedHash, uint32 uRoundSalt)
{
	const uchar* pSeed = pSeedHash;
	if (pSeed == NULL)
		pSeed = thePrefs.GetUserHash();

	CMD4 md4;
	md4.Reset();
	static const char s_szFineSalt[] = "esb.magic.fine.bucket.v1";
	static const char s_szBootSalt[] = "esb.magic.bootstrap.bucket.v1";
	const char* pszSalt = bBootstrap ? s_szBootSalt : s_szFineSalt;
	const uint32 uSaltLen = static_cast<uint32>(bBootstrap ? (ARRAYSIZE(s_szBootSalt) - 1) : (ARRAYSIZE(s_szFineSalt) - 1));
	md4.Add(reinterpret_cast<const uchar*>(pszSalt), uSaltLen);
	AddUInt32LEToMD4(md4, uEpoch);
	AddUInt32LEToMD4(md4, uRoundSalt);
	md4.Add(pSeed, MDX_DIGEST_SIZE);
	md4.Finish();

	const uint32 uRaw = ReadMixedUInt32FromHash(md4.GetHash());
	const uint32 uBucketCount = bBootstrap ? ESERVERBUDDY_MAGIC_BOOTSTRAP_BUCKET_COUNT : ESERVERBUDDY_MAGIC_FINE_BUCKET_COUNT;
	if (uBucketCount == 0)
		return 0;
	return uRaw % uBucketCount;
}

void CUpDownClient::BuildEServerBuddyMagicBucketInfo(bool bBootstrap, uint32 uBucket, uint32 uEpoch, uchar aucHash[MDX_DIGEST_SIZE], uint64& uSize, CString& strName)
{
	uBucket = NormalizeBucketIndex(bBootstrap, uBucket);

	CMD4 md4;
	md4.Reset();
	static const char s_szFineHashSalt[] = "esb.magic.fine.hash.v1";
	static const char s_szBootHashSalt[] = "esb.magic.bootstrap.hash.v1";
	const char* pszHashSalt = bBootstrap ? s_szBootHashSalt : s_szFineHashSalt;
	const uint32 uHashSaltLen = static_cast<uint32>(bBootstrap ? (ARRAYSIZE(s_szBootHashSalt) - 1) : (ARRAYSIZE(s_szFineHashSalt) - 1));
	md4.Add(reinterpret_cast<const uchar*>(pszHashSalt), uHashSaltLen);
	AddUInt32LEToMD4(md4, uEpoch);
	AddUInt32LEToMD4(md4, uBucket);
	md4.Finish();
	if (aucHash != NULL)
		md4cpy(aucHash, md4.GetHash());

	// Keep generated sizes in 32-bit range for broad server compatibility.
	const uint64 uBase = bBootstrap ? 3665000000ui64 : 3895000000ui64;
	const uint64 uEpochPart = static_cast<uint64>(uEpoch % 131072u) * (bBootstrap ? 17ui64 : 29ui64);
	uSize = uBase + uEpochPart + static_cast<uint64>(uBucket);
	if (uSize > 0xFFFFFFFFui64)
		uSize = 0xFFFFFFFFui64 - static_cast<uint64>((uBucket % 65535u) + 1u);

	if (bBootstrap)
		strName.Format(_T("archive_v1_boot_%02u.rar"), uBucket);
	else
		strName.Format(_T("archive_v1_b%04u_e%06u.rar"), uBucket, (uEpoch % 1000000u));
}

void CUpDownClient::BuildEServerBuddyMagicProof(uint32 dwNonce, uchar aucProof[MDX_DIGEST_SIZE])
{
	if (aucProof == NULL)
		return;

	CMD4 md4;
	md4.Reset();
	md4.Add(s_abyEServerBuddyMagicFileHash, MDX_DIGEST_SIZE);
	AddUInt64BEToMD4(md4, ESERVERBUDDY_MAGIC_FILE_SIZE);
	AddMagicFileNameToMD4(md4);
	AddUInt32BEToMD4(md4, dwNonce);
	md4.Finish();
	md4cpy(aucProof, md4.GetHash());
}

bool CUpDownClient::VerifyEServerBuddyMagicProof(uint32 dwNonce, const uchar* pucProof)
{
	if (pucProof == NULL)
		return false;

	uchar aucExpected[MDX_DIGEST_SIZE];
	BuildEServerBuddyMagicProof(dwNonce, aucExpected);
	return md4equ(aucExpected, pucProof);
}

bool CUpDownClient::SendEServerUdpProbe()
{
	if (!theApp.serverconnect || !theApp.serverconnect->IsLowID())
		return false;
	if (!SupportsEServerBuddyExternalUdpPort())
		return false;
	if (theApp.clientudp == NULL)
		return false;

	uint16 nBuddyPort = GetKadPort() ? GetKadPort() : GetUDPPort();
	if (nBuddyPort == 0)
		return false;

	CAddress buddyAddr = !GetIP().IsNull() ? GetIP() : GetConnectIP();
	if (buddyAddr.IsNull())
		return false;

	if (m_dwEServerExtPortProbeToken == 0)
		return false;

	DWORD dwNow = ::GetTickCount();
	if (m_dwEServerExtPortProbeSent != 0
		&& (DWORD)(dwNow - m_dwEServerExtPortProbeSent) < ESERVER_UDP_PROBE_MIN_INTERVAL)
		return false;

	CSafeMemFile data(ESERVER_UDP_PROBE_SIZE);
	data.WriteHash16(thePrefs.GetUserHash());
	data.WriteUInt32(m_dwEServerExtPortProbeToken);
	Packet* packet = new Packet(data, OP_EMULEPROT, OP_ESERVER_UDP_PROBE);
	theStats.AddUpDataOverheadOther(packet->size);
	theApp.clientudp->SendPacket(packet, buddyAddr, nBuddyPort, false, NULL, false, 0);
	m_dwEServerExtPortProbeSent = dwNow;

	return true;
}

// Check if we can request this client as eServer Buddy
bool CUpDownClient::CanQueryEServerBuddySlot() const
{
	// Must support eServer Buddy
	if (!m_bSupportsEServerBuddy)
		return false;

	// Must have available slot
	if (!m_bHasEServerBuddySlot)
		return false;

	// Must be HighID (we only want HighID buddies)
	if (HasLowID())
		return false;

	// Must be on the same server as us
	if (theApp.serverconnect && theApp.serverconnect->IsConnected()) {
		if (m_dwReportedServerIP != theApp.serverconnect->GetCurrentServer()->GetIP())
			return false;
	} else {
		return false;	// We're not connected to any server
	}

	// Check retry timeout from the latest buddy rejection backoff.
	if (::GetTickCount() < m_dwEServerBuddyRetryAfter)
		return false;

	return true;
}

// Called when our buddy request is rejected (slot full)
void CUpDownClient::OnEServerBuddyRejected()
{
	m_dwEServerBuddyRetryAfter = ::GetTickCount() + ESERVERBUDDY_REASK_TIME;
	m_bHasEServerBuddySlot = false;	// Mark as no slot available
}

// Called when our buddy request is rejected for non-slot reasons
void CUpDownClient::OnEServerBuddyRejectedGeneric()
{
	const DWORD dwRetryAfter = ::GetTickCount() + ESERVERBUDDY_GENERIC_REJECT_RETRY;
	if (m_dwEServerBuddyRetryAfter == 0 || (INT)(dwRetryAfter - m_dwEServerBuddyRetryAfter) > 0)
		m_dwEServerBuddyRetryAfter = dwRetryAfter;
}

// Called when our buddy request is accepted
void CUpDownClient::OnEServerBuddyAccepted()
{
	m_dwEServerBuddyRetryAfter = 0;
	m_nEServerBuddyRequestCount = 0;
	m_dwEServerBuddyFirstRequest = 0;

	if (SupportsEServerBuddyExternalUdpPort() && theApp.serverconnect && theApp.serverconnect->IsLowID()
		&& !thePrefs.HasValidExternalUdpPort())
		SendEServerUdpProbe();
}

// Rate limiting: can we accept a buddy request from this client?
bool CUpDownClient::CanAcceptBuddyRequest()
{
	DWORD dwNow = ::GetTickCount();

	// Reset counter if window expired (10 min).
	// Subtraction keeps this robust when GetTickCount wraps around.
	if (m_dwEServerBuddyFirstRequest == 0
		|| (DWORD)(dwNow - m_dwEServerBuddyFirstRequest) >= ESERVERBUDDY_REASK_TIME) {
		m_nEServerBuddyRequestCount = 0;
		m_dwEServerBuddyFirstRequest = dwNow;
	}

	// Allow max 3 requests per 10 min window
	if (m_nEServerBuddyRequestCount >= ESERVERBUDDY_MAX_REQUESTS_PER_WINDOW)
		return false;

	++m_nEServerBuddyRequestCount;
	return true;
}

bool CUpDownClient::CanAcceptEServerRelayRequest()
{
	const DWORD dwNow = ::GetTickCount();

	if (m_dwEServerRelayFirstRequest == 0
		|| (DWORD)(dwNow - m_dwEServerRelayFirstRequest) >= ESERVERBUDDY_RELAY_REQUEST_WINDOW) {
		m_nEServerRelayRequestCount = 0;
		m_dwEServerRelayFirstRequest = dwNow;
	}

	if (m_nEServerRelayRequestCount >= ESERVERBUDDY_MAX_RELAY_REQUESTS_PER_WINDOW)
		return false;

	++m_nEServerRelayRequestCount;
	return true;
}

// Send eServer Buddy request to this client
bool CUpDownClient::SendEServerBuddyRequest()
{
	// Verify we should send
	if (!CanQueryEServerBuddySlot())
		return false;

	// Must have a valid socket connection
	if (!socket || !socket->IsConnected())
		return false;

	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_EServerBuddyRequest", this);

	if (thePrefs.GetLogNatTraversalEvents()) {
		AddDebugLogLine(false, _T("[eServerBuddy] SendEServerBuddyRequest: Sending request to %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
	}

	uint8 byReqFlags = 0;
	uint32 dwCurrentServerIP = 0;
	uint16 nCurrentServerPort = 0;
	if (theApp.serverconnect != NULL && theApp.serverconnect->IsConnected() && theApp.serverconnect->GetCurrentServer() != NULL) {
		dwCurrentServerIP = theApp.serverconnect->GetCurrentServer()->GetIP();
		nCurrentServerPort = theApp.serverconnect->GetCurrentServer()->GetPort();
		if (dwCurrentServerIP != 0 && nCurrentServerPort != 0)
			byReqFlags |= ESERVERBUDDY_REQUEST_FLAG_SERVER_INFO;
	}

	Packet* packet = NULL;
	if (SupportsEServerBuddyMagicProof() || byReqFlags != 0) {
		uint32 dwNonce = 0;
		if (SupportsEServerBuddyMagicProof())
			byReqFlags |= ESERVERBUDDY_REQUEST_FLAG_MAGIC_PROOF;

		UINT uPayloadSize = 1;
		if ((byReqFlags & ESERVERBUDDY_REQUEST_FLAG_MAGIC_PROOF) != 0)
			uPayloadSize += sizeof(uint32) + MDX_DIGEST_SIZE;
		if ((byReqFlags & ESERVERBUDDY_REQUEST_FLAG_SERVER_INFO) != 0)
			uPayloadSize += sizeof(uint32) + sizeof(uint16);

		CSafeMemFile data(uPayloadSize);
		data.WriteUInt8(byReqFlags);

		if ((byReqFlags & ESERVERBUDDY_REQUEST_FLAG_MAGIC_PROOF) != 0) {
			dwNonce = GetRandomUInt32();
			if (dwNonce == 0)
				dwNonce = 1;

			uchar aucProof[MDX_DIGEST_SIZE];
			BuildEServerBuddyMagicProof(dwNonce, aucProof);

			data.WriteUInt32(dwNonce);
			data.WriteHash16(aucProof);
		}

		if ((byReqFlags & ESERVERBUDDY_REQUEST_FLAG_SERVER_INFO) != 0) {
			data.WriteUInt32(dwCurrentServerIP);
			data.WriteUInt16(nCurrentServerPort);
		}

		packet = new Packet(data, OP_EMULEPROT, OP_ESERVER_BUDDY_REQUEST);

		if ((byReqFlags & ESERVERBUDDY_REQUEST_FLAG_MAGIC_PROOF) != 0 && thePrefs.GetLogNatTraversalEvents()) {
			DebugLog(_T("[eServerBuddy] SendEServerBuddyRequest: Attached magic proof extension nonce=%u"), dwNonce);
		}
	} else {
		// Keep legacy empty payload for peers without proof support bit.
		packet = new Packet(OP_ESERVER_BUDDY_REQUEST, 0, OP_EMULEPROT);
	}
	theStats.AddUpDataOverheadOther(packet->size);
	socket->SendPacket(packet);

	return true;
}

// Send relay request via our eServer buddy to reach a target LowID client
bool CUpDownClient::SendEServerRelayRequest(CUpDownClient* pTarget)
{
	// We must be LowID and have a serving buddy
	if (!theApp.serverconnect->IsLowID()) {
		if (thePrefs.GetLogNatTraversalEvents()) {
			AddDebugLogLine(false, _T("[eServerBuddy] SendEServerRelayRequest Failed: We are not LowID"));
		}
		return false;
	}

	CUpDownClient* pBuddy = theApp.clientlist->GetServingEServerBuddy();
	if (!pBuddy || !pBuddy->socket || !pBuddy->socket->IsConnected()) {
		if (thePrefs.GetLogNatTraversalEvents()) {
			AddDebugLogLine(false, _T("[eServerBuddy] SendEServerRelayRequest Failed: No connected buddy (pBuddy=%p)"), pBuddy);
		}
		return false;
	}

	// Target must exist and have a valid LowID (for server callback)
	// Note: We use LowID instead of UserHash because server sources may not have hash yet
	if (!pTarget) {
		if (thePrefs.GetLogNatTraversalEvents()) {
			AddDebugLogLine(false, _T("[eServerBuddy] SendEServerRelayRequest Failed: pTarget is NULL"));
		}
		return false;
	}

	// Target must be LowID - that's the whole point of this relay
	if (!pTarget->HasLowID()) {
		if (thePrefs.GetLogNatTraversalEvents()) {
			AddDebugLogLine(false, _T("[eServerBuddy] SendEServerRelayRequest Failed: Target is not LowID"));
		}
		return false;
	}

	// Get target's LowID (UserIDHybrid in LowID case IS the LowID)
	uint32 dwTargetLowID = pTarget->GetUserIDHybrid();
	if (dwTargetLowID == 0) {
		if (thePrefs.GetLogNatTraversalEvents()) {
			AddDebugLogLine(false, _T("[eServerBuddy] SendEServerRelayRequest Failed: Target has no valid LowID"));
		}
		return false;
	}

	// Get our external IP (from server's perspective)
	// If we are LowID, we might not know our true public IP correctly.
	// We will send what we have (even if 0 or private), and rely on the Buddy to observe our true IP.
	CAddress myAddr = theApp.GetPublicIP();

	// Try to resolve true Public IP from KAD if current IP is missing or private
	// This is critical for VPN users where observed IP is private but KAD knows the true public IP
	if ((myAddr.IsNull() || !myAddr.IsPublicIP()) && Kademlia::CKademlia::IsRunning() && Kademlia::CKademlia::GetPrefs()) {
		uint32 dwKadIP = Kademlia::CKademlia::GetPrefs()->GetIPAddress();
		if (dwKadIP != 0) {
			CAddress kadAddr(dwKadIP, false);
			if (kadAddr.IsPublicIP()) {
				myAddr = kadAddr;
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] SendEServerRelayRequest: Using KAD Public IP %s"), (LPCTSTR)ipstr(myAddr));
				}
			}
		}
	}

	uint32 dwMyIP = 0;
	if (!myAddr.IsNull() && myAddr.GetType() == CAddress::IPv4) {
		dwMyIP = myAddr.ToUInt32(false);
	}

	if (!thePrefs.HasValidExternalUdpPort())
		pBuddy->SendEServerUdpProbe();

	// Get best external UDP port (prefer KAD external, fallback to eServer buddy cache)
	uint16 nMyKadPort = thePrefs.GetBestExternalUdpPort();
	if (nMyKadPort == 0)
		nMyKadPort = thePrefs.GetUDPPort();

	if (nMyKadPort == 0) {
		if (thePrefs.GetLogNatTraversalEvents()) {
			AddDebugLogLine(false, _T("[eServerBuddy] SendEServerRelayRequest Failed: No KAD port available"));
		}
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugLog(_T("SendEServerRelayRequest: No KAD port available"));
		return false;
	}

	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_EServerRelayRequest", pBuddy);

	// Get file hash for download context (similar to KAD Rendezvous)
	// This allows target client to set reqfile correctly
	uchar fileHash[MDX_DIGEST_SIZE];
	md4clr(fileHash);
	bool bHasFileContext = false;
	if (m_reqfile != NULL) {
		md4cpy(fileHash, m_reqfile->GetFileHash());
		bHasFileContext = true;
	}

	// Packet format: targetLowID(4) + myIP(4) + myKadPort(2) + fileHash(16) = 26 bytes
	// Uses LowID (not UserHash) because server callback requires LowID
	CSafeMemFile data(26);
	data.WriteUInt32(dwTargetLowID);
	data.WriteUInt32(dwMyIP);
	data.WriteUInt16(nMyKadPort);
	data.WriteHash16(fileHash);  // File hash for download context

	Packet* packet = new Packet(data, OP_EMULEPROT, OP_ESERVER_RELAY_REQUEST);
	theStats.AddUpDataOverheadOther(packet->size);
	pBuddy->socket->SendPacket(packet);

	if (thePrefs.GetLogNatTraversalEvents()) {
		AddDebugLogLine(false, _T("[eServerBuddy] SendEServerRelayRequest: Sent to buddy %s for target LowID=%u (SentMyIP=%s MyKadPort=%u FileHash=%s)"),
			(LPCTSTR)EscPercent(pBuddy->DbgGetClientInfo()), dwTargetLowID,
			(LPCTSTR)ipstr(htonl(dwMyIP)), nMyKadPort,
			bHasFileContext ? (LPCTSTR)md4str(fileHash) : _T("NULL"));
	}

	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugLog(_T("SendEServerRelayRequest: Sent relay request for %s via buddy %s"),
			(LPCTSTR)EscPercent(pTarget->DbgGetClientInfo()),
			(LPCTSTR)EscPercent(pBuddy->DbgGetClientInfo()));

	return true;
}

// Process pending relay when buddy reconnects
void CUpDownClient::ProcessPendingEServerRelay()
{
	if (!m_pPendingEServerRelayTarget) {
		return;
	}

	CUpDownClient* pTarget = m_pPendingEServerRelayTarget;
	m_pPendingEServerRelayTarget = NULL;  // Clear pending state

	if (thePrefs.GetLogNatTraversalEvents()) {
		AddDebugLogLine(false, _T("[eServerBuddy] ProcessPendingEServerRelay: Buddy reconnected, sending pending relay for %s"),
			(LPCTSTR)EscPercent(pTarget->DbgGetClientInfo()));
	}

	// Make sure we're the serving buddy and socket is valid now
	if (!socket || !socket->IsConnected()) {
		if (thePrefs.GetLogNatTraversalEvents()) {
			AddDebugLogLine(false, _T("[eServerBuddy] ProcessPendingEServerRelay: Socket still invalid after reconnect!"));
		}
		return;
	}

	// Send the relay request
	if (pTarget->SendEServerRelayRequest(pTarget)) {
		if (thePrefs.GetLogNatTraversalEvents()) {
			AddDebugLogLine(false, _T("[eServerBuddy] ProcessPendingEServerRelay: Relay request sent successfully"));
		}
	} else {
		if (thePrefs.GetLogNatTraversalEvents()) {
			AddDebugLogLine(false, _T("[eServerBuddy] ProcessPendingEServerRelay: Relay request failed"));
		}
	}
}
