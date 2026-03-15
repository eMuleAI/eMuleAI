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
#include "BarShader.h"
#include "ClientStateDefs.h"
#include "opcodes.h"
#include "OtherFunctions.h"
#include "eMuleAI/GeoLite2.h"
#include "resource.h"
#include "ListenSocket.h"
#include "eMuleAI/Shield.h"

// Diagnostic threshold (milliseconds) for waiting on uTP WRITABLE notifications.
constexpr DWORD NAT_TRAVERSAL_WRITABLE_WARN_MS = 1500;
// Grace window (milliseconds) before allowing a new rendezvous attempt while an existing uTP handshake is active.
constexpr DWORD NAT_TRAVERSAL_HANDSHAKE_GUARD_MS = 4000;
// Global cap for transient uTP errors on eServer relay NAT traversal.
constexpr uint8 ESERVER_RELAY_NATT_TRANSIENT_ERROR_LIMIT = 3;
constexpr DWORD ESERVER_RELAY_NATT_TRANSIENT_WINDOW_MS = 70000;

enum EUtpFrameType : uint8 {
	UTP_FRAMETYPE_DATA = 0,
	UTP_FRAMETYPE_FIN = 1,
	UTP_FRAMETYPE_STATE = 2,
	UTP_FRAMETYPE_RESET = 3,
	UTP_FRAMETYPE_SYN = 4,
	UTP_FRAMETYPE_UNKNOWN = 0xFF
};

class CClientReqSocket;
class CFriend;
class CPartFile;
class CClientCredits;
class CAbstractFile;
class CKnownFile;
class Packet;
struct Requested_Block_Struct;
class CSafeMemFile;
class CEMSocket;
class CAICHHash;
enum EUTF8str : uint8;

class CFileDataIO;

#define	CL_NAME						0x01
#define	CL_CONNECTIP				0x02
#define	CL_USERIP					0x03
#define	CL_SERVERIP					0x04
#define	CL_USERIDHYBRID				0x05
#define	CL_USERPORT					0x06
#define	CL_SERVERPORT				0x07
#define	CL_CLIENTVERSION			0x08
#define	CL_EMULEVERSION				0x09
#define	CL_EMULEPROTOCOL			0x0A
#define	CL_ISHYBRID					0x0B
#define	CL_UDPPORT					0x0C
#define	CL_KADPORT					0x0D
#define	CL_UDPVER					0x0E
#define	CL_COMPATIBLECLIENT			0x0F
#define	CL_ISML						0x10
#define	CL_GPLEVILDOER				0x11
#define	CL_CLIENTSOFTWARE			0x12
#define	CL_MODVERSION				0x13
#define	CL_MESSAGESRECEIVED			0x14
#define	CL_MESSAGESSENT				0x15
#define	CL_KADVERSION				0x16
#define	CL_NOVIEWSHAREDFILES		0x17
#define	CL_FIRSTSEEN				0x18
#define	CL_PUNISHMENT				0x19
#define	CL_BADCLIENTCATEGORY		0x1A
#define	CL_PUNISHMENTSTARTIME		0x1B
#define	CL_PUNISHMENTREASON			0x1C
#define	CL_SHAREDFILESSTATUS		0x1D
#define	CL_SHAREDFILESLASTQUERIEDTIME	0x1F
#define	CL_SHAREDFILESCOUNT			0x20
#define	CL_CLIENTNOTE				0x21
#define	CL_AUTOQUERYSHAREDFILES		0x22

enum ESharedFilesStatusCodes {
	S_NOT_QUERIED = 0,
	S_NO_RESPONSE,
	S_RECEIVED,
	S_ACCESS_DENIED
};

struct Pending_Block_Struct
{
	Requested_Block_Struct	*block;
	struct z_stream_s		*zStream;		// Barry - Used to unzip packets
	UINT					totalUnzipped;	// Barry - This holds the total unzipped bytes for all packets so far
	UINT					fZStreamError : 1,
							fRecovered	  : 1,
							fQueued		  : 3;
};

#pragma pack(push, 1)
struct Requested_File_Struct
{
	uchar	  fileid[MDX_DIGEST_SIZE];
	DWORD	  lastasked;
	uint8	  badrequests;
};
#pragma pack(pop)

struct PartFileStamp
{
	CPartFile	*file;
	DWORD		timestamp;
};

#define	MAKE_CLIENT_VERSION(mjr, min, upd) \
	((((UINT)(mjr)*100U + (UINT)(min))*10U + (UINT)(upd))*100U)

class CUpDownClient : public CObject
{
	DECLARE_DYNAMIC(CUpDownClient)
	friend class CUploadQueue;
	friend class CClientList;
	void	Init();

public:
	// NAT-T/uTP hello resend fallback
	void			ResendHelloIfTimeout();

	///////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Base
	explicit CUpDownClient(CClientReqSocket *sender = NULL);
	CUpDownClient(CPartFile *in_reqfile, const uint16 in_port, const uint32 in_userid, const uint32 in_serverip, const uint16 in_serverport, const bool ed2kID = false, const CAddress& IP = CAddress());
	
	// uTP connection timeout handling
	DWORD		GetUtpConnectionStartTick() const { return m_dwUtpConnectionStartTick; }
	void		SetUtpConnectionStartTick(DWORD tick) { m_dwUtpConnectionStartTick = tick; }
	void		ResetUtpConnectionStartTick() { m_dwUtpConnectionStartTick = 0; }
	bool		IsUtpWritable() const { return m_bUtpWritable; }
	void		SetUtpWritable(bool writable);
	bool		IsUtpHelloQueued() const { return m_bUtpHelloQueued; }
	void		SetUtpHelloQueued(bool queued);
	bool		IsHelloAnswerPending() const { return m_bHelloAnswerPending; }
	void		ClearHelloAnswerPending() { m_bHelloAnswerPending = false; }
	// Pending StartUploadReq (queued when handshake not finished)
	void		SetPendingStartUploadReq(const uchar* hash);
	bool		HasPendingStartUploadReq() const;
	const uchar*	GetPendingUpFileHash() const;
	void		ClearPendingStartUploadReq();
	bool		HasQueuedUtpPackets() const { return !m_WaitingPackets_list.IsEmpty(); }
	void		QueuePacketForUtp(Packet* packet) { m_WaitingPackets_list.AddTail(packet); }
	bool		IsUtpLocalInitiator() const { return m_bUtpLocalInitiator; }
	void		SetUtpLocalInitiator(bool initiated) { m_bUtpLocalInitiator = initiated; }
	bool		IsUtpProactiveHelloSent() const { return m_bUtpProactiveHelloSent; }
	void		SetUtpProactiveHelloSent(bool sent) { m_bUtpProactiveHelloSent = sent; }
	bool		NeedsUtpQueuePurge() const { return m_bUtpNeedsQueuePurge; }
	void		RequestUtpQueuePurge() { m_bUtpNeedsQueuePurge = true; }
	void		ClearUtpQueuePurgeRequest() { m_bUtpNeedsQueuePurge = false; }
	void		ResetUtpFlowControl();
	void		ClearUtpQueuedPackets();
	bool		HasUtpInboundActivity() const { return m_bUtpInboundActivity; }
	EUtpFrameType	GetUtpLastFrameType() const { return m_uUtpLastFrameType; }
	DWORD		GetUtpLastInboundTick() const { return m_dwUtpLastInboundTick; }
	void		RegisterUtpInboundActivity(EUtpFrameType frameType);
	virtual	~CUpDownClient();

	void			StartDownload();
	void			TriggerFileRequestIfNeeded();
	virtual void	CheckDownloadTimeout();
	virtual void	SendCancelTransfer();
	virtual bool	IsEd2kClient() const							{ return true; }
	virtual bool	Disconnected(LPCTSTR pszReason, bool bFromSocket = false);
	virtual bool	TryToConnect(bool bIgnoreMaxCon = false, bool bNoCallbacks = false, CRuntimeClass* pClassSocket = NULL, bool bUseUTP = false);
	virtual void	Connect();
	virtual void	ConnectionEstablished();
	virtual void	OnSocketConnected(int nErrorCode);
	bool			CheckHandshakeFinished() const;
	void			CheckFailedFileIdReqs(const uchar *aucFileHash);
	uint32			GetUserIDHybrid() const							{ return m_nUserIDHybrid; }
	void			SetUserIDHybrid(uint32 val)						{ m_nUserIDHybrid = val; }
	LPCTSTR			GetUserName() const								{ return m_pszUsername; }
	void			SetUserName(LPCTSTR pszNewName);

	const CAddress& GetIP() const { return m_UserIP; }
	const CAddress& GetIPv4() const { return m_UserIPv4; }
	const CAddress& GetIPv6() const { return m_UserIPv6; }
	bool			IsIPv6Open() const { return m_bOpenIPv6; }
	void			UpdateIP(const CAddress& val);
	void			SetIP(CAddress& val);   //Only use this when you know the real IP or when your clearing it.
	void			SetIPv6(const CAddress& val);
	const CAddress& GetConnectIP() const { return m_ConnectIP; }

	inline bool		HasLowID() const								{ return ::IsLowID(m_nUserIDHybrid); }
	void			SetConnectIP(CAddress val)						{ m_ConnectIP = val; ResetGeoLite2(val); }
	uint16			GetUserPort() const								{ return m_nUserPort; }
	void			SetUserPort(uint16 val)							{ m_nUserPort = val; }
	uint64			GetTransferredUp() const						{ return m_nTransferredUp; }
	uint64			GetTransferredDown() const						{ return m_nTransferredDown; }
	uint32			GetServerIP() const								{ return m_dwServerIP; }
	void			SetServerIP(uint32 nIP)							{ m_dwServerIP = nIP; }
	uint16			GetServerPort() const							{ return m_nServerPort; }
	void			SetServerPort(uint16 nPort)						{ m_nServerPort = nPort; }
	const uchar*	GetUserHash() const								{ return (uchar*)m_achUserHash; }
	void			SetUserHash(const uchar *pucUserHash, bool bLoadArchive = true);
	bool			HasValidHash() const							{ return !isnulmd4(m_achUserHash); }
	int				GetHashType() const;
	const uchar*	GetServingBuddyID() const						{ return (uchar*)m_achServingBuddyID; }
	void			SetServingBuddyID(const uchar *pucServingBuddyID);
	bool			HasValidServingBuddyID() const					{ return m_bServingBuddyIDValid; }
	void			SetServingBuddyIP(const CAddress& val)			{ m_ServingBuddyIP = val; }
	const CAddress&	GetServingBuddyIP() const						{ return m_ServingBuddyIP; }
	void			SetServingBuddyPort(uint16 val)					{ m_nServingBuddyPort = val; }
	uint16			GetServingBuddyPort() const						{ return m_nServingBuddyPort; }
	// NAT-T assist: remember last requester endpoint seen at OP_CALLBACK
	void			SetLastCallbackRequesterIP(const CAddress& ip)	{ m_LastCallbackRequesterIP = ip; }
	const CAddress&	GetLastCallbackRequesterIP() const			{ return m_LastCallbackRequesterIP; }
	EClientSoftware	GetClientSoft() const							{ return m_clientSoft; }
	const CString&	GetClientSoftVer() const						{ return m_strClientSoftware; }
	const CString&	GetClientModVer() const							{ return m_strModVersion; }
	void			SetClientSoftVer(const CString& val)			{ m_strClientSoftware = val; }
	void			SetClientModVer(const CString& val)				{ m_strModVersion = val;}
	void			InitClientSoftwareVersion();
	UINT			GetVersion() const								{ return m_nClientVersion; }
	void			SetVersion(UINT val) 							{ m_nClientVersion = val; }
	uint8			GetMuleVersion() const							{ return m_byEmuleVersion; }
	void			SetMuleVersion(uint8 val)						{ m_byEmuleVersion = val; }
	bool			ExtProtocolAvailable() const					{ return m_bEmuleProtocol; }
	void			SetEmuleProtocol(bool val)						{ m_bEmuleProtocol = val; }
	bool			GetIsHybrid() const								{ return m_bIsHybrid; }
	void			SetIsHybrid(bool val)							{ m_bIsHybrid = val; }
	uint8			GetCompatibleClient() const						{ return m_byCompatibleClient; }
	void			SetCompatibleClient(uint8 val)					{ m_byCompatibleClient = val; }
	bool			GetIsML() const									{ return m_bIsML; }
	void			SetIsML(bool val)								{ m_bIsML = val; }
	bool			GetGPLEvildoer() const							{ return m_bGPLEvildoer; }
	void			SetGPLEvildoer(bool val)						{ m_bGPLEvildoer = val; }
	bool			SupportMultiPacket() const						{ return m_bMultiPacket; }
	bool			SupportExtMultiPacket() const					{ return m_fExtMultiPacket; }
	bool			SupportPeerCache() const						{ return m_fPeerCache; } //false
	bool			SupportsLargeFiles() const						{ return m_fSupportsLargeFiles; }
	bool			SupportsFileIdentifiers() const					{ return m_fSupportsFileIdent; }
	bool			IsEmuleClient() const							{ return m_byEmuleVersion != 0; }
	uint8			GetSourceExchange1Version() const				{ return m_bySourceExchange1Ver; }
	bool			SupportsSourceExchange2() const					{ return m_fSupportsSourceEx2; }
	const bool		SupportsExtendedSourceExchange() const			{ return m_ModMiscOptions.Fields.SupportsExtendedXS; }
	void			WriteExtendedSourceExchangeData(CSafeMemFile& data) const; //by WiZaRd
	CClientCredits*	Credits() const									{ return credits; }
	bool			IsBanned() const;
	const CString&	GetClientFilename() const						{ return m_strClientFilename; }
	void			SetClientFilename(const CString &fileName)		{ m_strClientFilename = fileName; }
	uint16			GetUDPPort() const								{ return m_nUDPPort; }
	void			SetUDPPort(uint16 nPort)						{ m_nUDPPort = nPort; }
	uint8			GetUDPVersion() const							{ return m_byUDPVer; }
	void			SetUDPVersion(uint8 val)						{ m_byUDPVer = val; }
	bool			SupportsUDP() const								{ return GetUDPVersion() != 0 && m_nUDPPort != 0; }
	uint16			GetKadPort() const								{ return m_nKadPort; }
	void			SetKadPort(uint16 nPort)						{ m_nKadPort = nPort; }
	uint8			GetExtendedRequestsVersion() const				{ return m_byExtendedRequestsVer; }
	void			RequestSharedFileList();
	void			ProcessSharedFileList(const uchar *pachPacket, uint32 nSize, LPCTSTR pszDirectory = NULL);
	EConnectingState GetConnectingState() const						{ return m_eConnectingState; }
	void			ResetConnectingState()								{ m_eConnectingState = CCS_NONE; }


	void			ClearHelloProperties();
	bool			ProcessHelloAnswer(const uchar *pachPacket, uint32 nSize);
	bool			ProcessHelloPacket(const uchar *pachPacket, uint32 nSize);
	void			SendHelloAnswer();
	virtual void	SendHelloPacket();
	void			SendMuleInfoPacket(bool bAnswer);
	void			ProcessMuleInfoPacket(const uchar *pachPacket, uint32 nSize);
	void			ProcessMuleCommentPacket(const uchar *pachPacket, uint32 nSize);
	void			ProcessEmuleQueueRank(const uchar *packet, UINT size);
	void			ProcessEdonkeyQueueRank(const uchar *packet, UINT size);
	void			CheckQueueRankFlood();
	bool			Compare(const CUpDownClient *tocomp, bool bIgnoreUserhash = false) const;
	void			ResetFileStatusInfo();
	DWORD			GetLastSrcReqTime() const						{ return m_dwLastSourceRequest; }
	void			SetLastSrcReqTime()								{ m_dwLastSourceRequest = ::GetTickCount(); }
	DWORD			GetLastSrcAnswerTime() const					{ return m_dwLastSourceAnswer; }
	void			SetLastSrcAnswerTime()							{ m_dwLastSourceAnswer = ::GetTickCount(); }
	DWORD			GetLastAskedForSources() const					{ return m_dwLastAskedForSources; }
	void			SetLastAskedForSources()						{ m_dwLastAskedForSources = ::GetTickCount(); }
	bool			GetFriendSlot() const;
	void			SetFriendSlot(bool bNV)							{ m_bFriendSlot = bNV; }
	bool			IsFriend() const								{ return m_Friend != NULL; }
	CFriend*		GetFriend() const;
	void			SetCommentDirty(bool bDirty = true)				{ m_bCommentDirty = bDirty; }
	bool			GetSentCancelTransfer() const					{ return m_fSentCancelTransfer; }
	void			SetSentCancelTransfer(bool bVal)				{ m_fSentCancelTransfer = bVal; }
	void			ProcessPublicIPAnswer(const BYTE *pbyData, UINT uSize);
	void			SendPublicIPRequest();
	uint8			GetKadVersion()	const							{ return m_byKadVersion; }
	void			SetKadVersion(uint8 val)						{ m_byKadVersion = val; }
	bool			SendBuddyPingPong()								{ return ::GetTickCount() >= m_dwLastBuddyPingPongTime; }
	bool			AllowIncomingBuddyPingPong()					{ return ::GetTickCount() >= m_dwLastBuddyPingPongTime + MIN2MS(3); }
	void			SetLastBuddyPingPongTime()						{ m_dwLastBuddyPingPongTime = ::GetTickCount() + MIN2MS(10); }
	DWORD		GetLastBlockReceived() const					{ return m_dwLastBlockReceived; }
	void		CheckNatTraversalStall();
	void		CheckNatTraversalKeepAlive();	// Uploader side NAT keep-alive
	void		ProcessFirewallCheckUDPRequest(CSafeMemFile &data);
	void			SendSharedDirectories();

	// secure ident
	void			SendPublicKeyPacket();
	void			SendSignaturePacket();
	void			ProcessPublicKeyPacket(const uchar *pachPacket, uint32 nSize);
	void			ProcessSignaturePacket(const uchar *pachPacket, uint32 nSize);
	uint8			GetSecureIdentState() const						{ return (uint8)m_SecureIdentState; }
	void			SendSecIdentStatePacket();
	void			ProcessSecIdentStatePacket(const uchar *pachPacket, uint32 nSize);
	void			ResetSecureIdentState();
	bool			HasSessionSecureIdentSuccess() const				{ return !m_SecureIdentVerifiedIP.IsNull(); }
	const CAddress&	GetSessionSecureIdentIP() const					{ return m_SecureIdentVerifiedIP; }
	bool			IsSecureIdentRecheckPending() const				{ return m_bSecureIdentRecheckPending; }
	uint8			GetInfoPacketsReceived() const					{ return m_byInfopacketsReceived; }
	void			InfoPacketsReceived();
	bool			HasPassedSecureIdent(bool bPassIfUnavailable) const;
	// preview
	void			SendPreviewRequest(const CAbstractFile &rForFile);
	void			SendPreviewAnswer(const CKnownFile *pForFile, HBITMAP *imgFrames, uint8 nCount);
	void			ProcessPreviewReq(const uchar *pachPacket, uint32 nSize);
	void			ProcessPreviewAnswer(const uchar *pachPacket, uint32 nSize);
	bool			GetPreviewSupport() const						{ return m_fSupportsPreview && GetViewSharedFilesSupport(); }
	bool			GetViewSharedFilesSupport() const				{ return m_fNoViewSharedFiles==0; }
	void			SetViewSharedFilesSupport(bool val)				{ m_fNoViewSharedFiles = val ? 0 : 1; }
	bool			SafeConnectAndSendPacket(Packet *packet);
	bool			SendPacket(Packet *packet, bool bVerifyConnection = false);
	void			CheckForGPLEvilDoer();
	// Encryption / Obfuscation / Connect options
	bool			SupportsCryptLayer() const						{ return m_fSupportsCryptLayer; }
	bool			RequestsCryptLayer() const						{ return SupportsCryptLayer() && m_fRequestsCryptLayer; }
	bool			RequiresCryptLayer() const						{ return RequestsCryptLayer() && m_fRequiresCryptLayer; }
	bool			SupportsDirectUDPCallback() const				{ return m_fDirectUDPCallback != 0 && HasValidHash() && GetKadPort() != 0; }
	void			SetCryptLayerSupport(bool bVal)					{ m_fSupportsCryptLayer = static_cast<UINT>(bVal); }
	void			SetCryptLayerRequest(bool bVal)					{ m_fRequestsCryptLayer = static_cast<UINT>(bVal); }
	void			SetCryptLayerRequires(bool bVal)				{ m_fRequiresCryptLayer = static_cast<UINT>(bVal); }
	void			SetDirectUDPCallbackSupport(bool bVal)			{ m_fDirectUDPCallback = static_cast<UINT>(bVal); }
	const bool		GetNatTraversalSupport() const					{ return m_ModMiscOptions.Fields.SupportsNatTraversal; }
	void			SetNatTraversalSupport(bool bVal)				{ m_ModMiscOptions.Fields.SupportsNatTraversal = bVal ? 1 : 0; }
	bool			SupportsIPv6() const							{ return m_ModMiscOptions.Fields.SupportsIPv6; }
	const uint8		GetConnectOptions(const bool bEncryption, const bool bCallback, const bool bNATTraversal) const; // by WiZaRd
	bool			SupportsServingBuddyPull() const				{ return m_ModMiscOptions.Fields.SupportsServingBuddyPull; }
	void			SetServingBuddyPullSupport(bool bVal)			{ m_ModMiscOptions.Fields.SupportsServingBuddyPull = bVal ? 1 : 0; }
	bool			RequestServingBuddyInfo();
	DWORD			m_dwLastServingBuddyPullReq;
	uint8			m_uServingBuddyPullTries;

	// eServer Buddy helper methods
		bool			SupportsEServerBuddy() const					{ return m_bSupportsEServerBuddy; }
		bool			HasEServerBuddySlot() const						{ return m_bHasEServerBuddySlot; }
		bool			SupportsEServerBuddyExternalUdpPort() const		{ return m_bSupportsEServerBuddyExtUdpPort; }
		bool			SupportsEServerBuddyMagicProof() const			{ return m_bSupportsEServerBuddyMagicProof; }
		uint32			GetReportedServerIP() const						{ return m_dwReportedServerIP; }
		void			SetReportedServerIP(uint32 dwServerIP)			{ m_dwReportedServerIP = dwServerIP; }
		DWORD			GetEServerBuddyRetryAfter() const				{ return m_dwEServerBuddyRetryAfter; }
		bool			CanQueryEServerBuddySlot() const;				// Can we request this client as buddy?
		void			OnEServerBuddyRejected();						// Called when buddy request rejected due to slot full
		void			OnEServerBuddyRejectedGeneric();				// Called when buddy request rejected for non-slot reasons
		void			OnEServerBuddyAccepted();						// Called when buddy request accepted
		bool			CanAcceptBuddyRequest();						// Rate limiting: can we accept request from this client?
		bool			CanAcceptEServerRelayRequest();					// Rate limiting: can we accept relay request from this client?
		bool			SendEServerBuddyRequest();						// Send buddy request to this client
		bool			SendEServerRelayRequest(CUpDownClient* pTarget);	// Send relay request via our buddy
		bool			SendEServerUdpProbe();							// UDP probe to discover our external UDP port
		static uint32	GetEServerBuddyMagicEpoch(time_t tNowUtc = 0);
		static uint32	SelectEServerBuddyMagicBucket(bool bBootstrap, uint32 uEpoch, const uchar* pSeedHash, uint32 uRoundSalt = 0);
		static void		BuildEServerBuddyMagicBucketInfo(bool bBootstrap, uint32 uBucket, uint32 uEpoch, uchar aucHash[MDX_DIGEST_SIZE], uint64& uSize, CString& strName);
		static void		BuildEServerBuddyMagicProof(uint32 dwNonce, uchar aucProof[MDX_DIGEST_SIZE]);
		static bool		VerifyEServerBuddyMagicProof(uint32 dwNonce, const uchar* pucProof);
		void			SetEServerExtPortProbeToken(DWORD token)			{ m_dwEServerExtPortProbeToken = token; }
		DWORD			GetEServerExtPortProbeToken() const				{ return m_dwEServerExtPortProbeToken; }
		void			SetObservedExternalUdpPort(uint16 port);
	uint16			GetObservedExternalUdpPort() const				{ return m_nObservedExternalUdpPort; }
	DWORD			GetObservedExternalUdpPortTime() const			{ return m_dwObservedExternalUdpPortTime; }
	bool			HasFreshObservedExternalUdpPort() const;
	void			SetLastEServerBuddyValidServerPing(DWORD tick)	{ m_dwLastEServerBuddyValidServerPing = tick; }
	void			TouchEServerBuddyValidServerPing()				{ m_dwLastEServerBuddyValidServerPing = ::GetTickCount(); }
	DWORD			GetLastEServerBuddyValidServerPing() const		{ return m_dwLastEServerBuddyValidServerPing; }
	void			SetPendingEServerRelayTarget(CUpDownClient* pTarget) { m_pPendingEServerRelayTarget = pTarget; }
	CUpDownClient*	GetPendingEServerRelayTarget() const { return m_pPendingEServerRelayTarget; }
	void			ProcessPendingEServerRelay();	// Called on buddy connect to send pending relay

	void			SetConnectOptions(uint8 byOptions, bool bEncryption = true, bool bCallback = true); // shortcut, sets crypt, callback etc based from the tag value we receive
	const bool		AnyCallbackAvailable() const;
	bool			IsObfuscatedConnectionEstablished() const;
	bool			ShouldReceiveCryptUDPPackets() const;

	void			GetDisplayImage(int &iImage, UINT &uOverlayImage) const;
	///////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Upload
	EUploadState	GetUploadState() const							{ return m_eUploadState; }
	void			SetUploadState(EUploadState eNewState);
	DWORD			GetWaitStartTime() const;
	void			SetWaitStartTime();
	void			ClearWaitStartTime();
	DWORD			GetWaitTime() const								{ return m_dwUploadTime - GetWaitStartTime(); }
	bool			IsDownloading() const							{ return (m_eUploadState == US_UPLOADING); }
	UINT			GetUploadDatarate() const								{ return m_nUpDatarate; }
	const UINT		GetScore(const bool sysvalue, const bool isdownloading = false, const bool onlybasevalue = false);
	void			AddReqBlock(Requested_Block_Struct *reqblock, bool bSignalIOThread);
	DWORD			GetUpStartTime() const							{ return m_dwUploadTime; }
	DWORD			GetUpStartTimeDelay() const						{ return ::GetTickCount() - m_dwUploadTime; }
	void			SetUpStartTime()								{ m_dwUploadTime = ::GetTickCount(); }
	void			SendHashsetPacket(const uchar *pData, uint32 nSize, bool bFileIdentifiers);
	const uchar*	GetUploadFileID() const							{ return requpfileid; }
	void			SetUploadFileID(CKnownFile *newreqfile);
	void			UpdateUploadingStatisticsData();
	void			SendRankingInfo();
	void			SendCommentInfo(/*const */CKnownFile *file);
	void			AddRequestCount(const uchar *fileid);
	void			UnBan();
	const void		Ban(const CString& strReason = EMPTY, const uint8 uBadClientCategory = PR_OFFICIALBAN, const uint8 uPunishment = P_IPUSERHASHBAN, const time_t tBanStartTime = time(NULL));
	UINT			GetAskedCount() const							{ return m_cAsked; }
	void			IncrementAskedCount()							{ ++m_cAsked; }
	void			SetAskedCount(UINT m_cInAsked)					{ m_cAsked = m_cInAsked; }
	void			FlushSendBlocks(); // call this when you stop upload, or the socket might be not able to send
	DWORD			GetLastUpRequest() const						{ return m_dwLastUpRequest; }
	void			SetLastUpRequest()								{ m_dwLastUpRequest = ::GetTickCount(); }
	void			SetCollectionUploadSlot(bool bValue);
	bool			HasCollectionUploadSlot() const					{ return m_bCollectionUploadSlot; }

	uint64			GetSessionUp() const							{ return m_nTransferredUp - m_nCurSessionUp; }
	void			ResetSessionUp() {
						m_nCurSessionUp = m_nTransferredUp;
						m_addedPayloadQueueSession = 0;
						m_nCurQueueSessionPayloadUp = 0;
					}

	uint64			GetSessionDown() const							{ return m_nTransferredDown - m_nCurSessionDown; }
	uint64			GetSessionPayloadDown() const					{ return m_nCurSessionPayloadDown; }
	void			ResetSessionDown()								{ m_nCurSessionDown = m_nTransferredDown; m_nCurSessionPayloadDown = 0; }
	uint64			GetQueueSessionPayloadUp() const				{ return m_nCurQueueSessionPayloadUp; } // Data uploaded/transmitted
	uint64			GetQueueSessionUploadAdded() const				{ return m_addedPayloadQueueSession; } // Data put into upload buffers
	uint64			GetPayloadInBuffer() const						{ return m_addedPayloadQueueSession - m_nCurQueueSessionPayloadUp; }
	void			SetQueueSessionUploadAdded(uint64 uVal)			{ m_addedPayloadQueueSession = uVal; }
	const bool		ProcessExtendedInfo(CSafeMemFile &packet, CKnownFile* tempreqfile, bool isUDP = false);
	uint16			GetUpPartCount() const							{ return m_nUpPartCount; }
	void			DrawUpStatusBar(CDC *dc, const CRect &rect, bool onlygreyrect, bool  bFlat) const;
	bool			IsUpPartAvailable(UINT uPart) const				{ return (m_abyUpPartStatus && uPart < m_nUpPartCount && m_abyUpPartStatus[uPart]);	}
	uint8*			GetUpPartStatus() const							{ return m_abyUpPartStatus; }
	float			GetCombinedFilePrioAndCredit();
	uint8			GetDataCompressionVersion() const { return m_byDataCompVer; }

	///////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Download
	UINT			GetAskedCountDown() const						{ return m_cDownAsked; }
	void			IncrementAskedCountDown()						{ ++m_cDownAsked; }
	void			SetAskedCountDown(UINT cInDownAsked)			{ m_cDownAsked = cInDownAsked; }
	EDownloadState	GetDownloadState() const						{ return m_eDownloadState; }
	void			SetDownloadState(EDownloadState nNewState, LPCTSTR pszReason = _T("Unspecified"));
	DWORD			GetLastAskedTime(const CPartFile *pFile = NULL) const;
	void			SetLastAskedTime()								{ m_fileReaskTimes[m_reqfile] = ::GetTickCount(); }
	bool			IsPartAvailable(UINT uPart) const				{ return m_abyPartStatus && uPart < m_nPartCount && m_abyPartStatus[uPart]; }
	uint8*			GetPartStatus() const							{ return m_abyPartStatus; }
	uint16			GetPartCount() const							{ return m_nPartCount; }
	UINT			GetDownloadDatarate() const						{ return m_nDownDatarate; }
	UINT			GetRemoteQueueRank() const						{ return m_nRemoteQueueRank; }
	void			SetRemoteQueueRank(UINT nr, bool bUpdateDisplay = false);
	bool			IsRemoteQueueFull() const						{ return m_bRemoteQueueFull; }
	void			SetRemoteQueueFull(bool flag)					{ m_bRemoteQueueFull = flag; }
	void			DrawStatusBar(CDC *dc, const CRect &rect, bool onlygreyrect, bool  bFlat) const;
	bool			AskForDownload();
	virtual void	SendFileRequest();
	void			SendStartupLoadReq();
	void			ProcessFileInfo(CSafeMemFile &data, CPartFile *file);
	void			ProcessFileStatus(bool bUdpPacket, CSafeMemFile &data, CPartFile *file);
	void			ProcessHashSet(const uchar *packet, uint32 size, bool bFileIdentifiers);
	void			ProcessAcceptUpload();
	bool			AddRequestForAnotherFile(CPartFile *file);
	void			CreateBlockRequests(int blockCount);
	virtual void	SendBlockRequests();
	virtual void	ProcessBlockPacket(const uchar *packet, uint32 size, bool packed, bool bI64Offsets);
	void			ClearPendingBlockRequest(const Pending_Block_Struct *pending);
	void			ClearDownloadBlockRequests();
	void			SendOutOfPartReqsAndAddToWaitingQueue();
	UINT			CalculateDownloadRate();
	uint16			GetAvailablePartCount() const;
	bool			SwapToAnotherFile(LPCTSTR reason, bool bIgnoreNoNeeded, bool ignoreSuspensions, bool bRemoveCompletely, CPartFile *toFile = NULL, bool allowSame = true, bool isAboutToAsk = false, bool debug = false); // ZZ:DownloadManager
	void			DontSwapTo(/*const*/ CPartFile *file);
	bool			IsSwapSuspended(const CPartFile *file, const bool allowShortReaskTime = false, const bool fileIsNNP = false) /*const*/; // ZZ:DownloadManager
	DWORD			GetTimeUntilReask() const;
	DWORD			GetTimeUntilReask(const CPartFile *file) const;
	DWORD			GetTimeUntilReask(const CPartFile *file, const bool allowShortReaskTime, const bool useGivenNNP = false, const bool givenNNP = false) const;
	void			UDPReaskACK(uint16 nNewQR);
	void			UDPReaskFNF();
	void			UDPReaskForDownload();
	bool			UDPPacketPending() const						{ return m_bUDPPending; }
	bool			IsSourceRequestAllowed() const					{ return thePrefs.IsDetectXSExploiter() && IsXSExploiter() ? false : IsSourceRequestAllowed(m_reqfile); }
	bool			IsSourceRequestAllowed(CPartFile *partfile, bool sourceExchangeCheck = false) const; // ZZ:DownloadManager

	bool			IsValidSource() const;
	ESourceFrom		GetSourceFrom() const							{ return m_eSourceFrom; }
	void			SetSourceFrom(const ESourceFrom val)			{ m_eSourceFrom = val; }

	void			SetDownStartTime()								{ m_dwDownStartTime = ::GetTickCount(); }
	DWORD			GetDownTimeDifference(boolean clear = true)
					{
						DWORD myTime = m_dwDownStartTime;
						if (clear)
							m_dwDownStartTime = 0;
						return ::GetTickCount() - myTime;
					}
	bool			GetTransferredDownMini() const					{ return m_bTransferredDownMini; }
	void			SetTransferredDownMini()						{ m_bTransferredDownMini = true; }
	void			InitTransferredDownMini()						{ m_bTransferredDownMini = false; }
	UINT			GetA4AFCount() const							{ return static_cast<UINT>(m_OtherRequests_list.GetCount()); }

	uint16			GetUpCompleteSourcesCount() const				{ return m_nUpCompleteSourcesCount; }
	void			SetUpCompleteSourcesCount(uint16 n)				{ m_nUpCompleteSourcesCount = n; }

	///////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Chat
	EChatState		GetChatState() const							{ return m_eChatstate; }
	void			SetChatState(const EChatState nNewS)			{ m_eChatstate = nNewS; }
	EChatCaptchaState GetChatCaptchaState() const					{ return m_eChatCaptchaState; }
	void			SetChatCaptchaState(const EChatCaptchaState nNewS)	{ m_eChatCaptchaState = nNewS; }
	void			ProcessChatMessage(CSafeMemFile &data, uint32 nLength);
	void			SendChatMessage(const CString &strMessage);
	void			ProcessCaptchaRequest(CSafeMemFile &data);
	void			ProcessCaptchaReqRes(uint8 nStatus);
	// message filtering
	uint8			GetMessagesReceived() const						{ return m_cMessagesReceived; }
	void			SetMessagesReceived(uint8 nCount)				{ m_cMessagesReceived = nCount; }
	void			IncMessagesReceived()							{ m_cMessagesReceived < 255 ? ++m_cMessagesReceived : 255; }
	uint8			GetMessagesSent() const							{ return m_cMessagesSent; }
	void			SetMessagesSent(uint8 nCount)					{ m_cMessagesSent = nCount; }
	void			IncMessagesSent()								{ m_cMessagesSent < 255 ? ++m_cMessagesSent : 255; }
	bool			IsSpammer() const								{ return m_fIsSpammer; }
	void			SetSpammer(bool bVal);
	bool			GetMessageFiltered() const						{ return m_fMessageFiltered; }
	void			SetMessageFiltered(bool bVal);


	//KadIPCheck
	EKadState		GetKadState() const								{ return m_eKadState; }
	void			SetKadState(const EKadState nNewS)				{ m_eKadState = nNewS; }

	//File Comment
	bool			HasFileComment() const							{ return !m_strFileComment.IsEmpty(); }
	const CString&	GetFileComment() const							{ return m_strFileComment; }
	void			SetFileComment(LPCTSTR pszComment)				{ m_strFileComment = pszComment; }

	bool			HasFileRating() const							{ return m_uFileRating > 0; }
	uint8			GetFileRating() const							{ return m_uFileRating; }
	void			SetFileRating(uint8 uRating)					{ m_uFileRating = uRating; }

	// Barry - Process zip file as it arrives, don't need to wait until end of block
	int				unzip(Pending_Block_Struct *block, const BYTE *zipped, uint32 lenZipped, BYTE **unzipped, uint32 *lenUnzipped, int iRecursion = 0);
	void			UpdateDisplayedInfo(bool force = false);
	int				GetFileListRequested() const					{ return m_iFileListRequested; }
	void			SetFileListRequested(int iFileListRequested)	{ m_iFileListRequested = iFileListRequested; }
	uint32			GetSearchID() const								{ return m_uSearchID; }
	void			SetSearchID(uint32 uID)							{ m_uSearchID = uID; }

	virtual void	SetRequestFile(CPartFile *pReqFile);
	CPartFile*		GetRequestFile() const							{ return m_reqfile; }

	// AICH Stuff
	void			SetReqFileAICHHash(CAICHHash *val);
	CAICHHash*		GetReqFileAICHHash() const						{ return m_pReqFileAICHHash; }
	bool			IsSupportingAICH() const						{ return m_fSupportsAICH & 0x01; }
	void			SendAICHRequest(CPartFile *pForFile, uint16 nPart);
	bool			IsAICHReqPending() const						{ return m_fAICHRequested; }
	void			ProcessAICHAnswer(const uchar *packet, UINT size);
	void			ProcessAICHRequest(const uchar *packet, UINT size);
	void			ProcessAICHFileHash(CSafeMemFile *data, CPartFile *file, const CAICHHash *pAICHHash);

	EUTF8str		GetUnicodeSupport() const;

	CString			GetDownloadStateDisplayString() const;
	CString			GetUploadStateDisplayString() const;

	LPCTSTR			DbgGetDownloadState() const;
	LPCTSTR			DbgGetUploadState() const;
	LPCTSTR			DbgGetKadState() const;
	CString			DbgGetClientInfo(bool bFormatIP = false) const;
	CString			DbgGetFullClientSoftVer() const;
	bool			GetAIModVersionString(CString& outVersion) const;
	bool			IsAIModClient() const { CString tmp; return GetAIModVersionString(tmp); }
	const CString&	DbgGetHelloInfo() const							{ return m_strHelloInfo; }
	const CString&	DbgGetMuleInfo() const							{ return m_strMuleInfo; }

	bool			IsInNoNeededList(const CPartFile *fileToCheck) const;
	bool			SwapToRightFile(CPartFile *SwapTo, CPartFile *cur_file, bool ignoreSuspensions, bool SwapToIsNNPFile, bool curFileisNNPFile, bool &wasSkippedDueToSourceExchange, bool doAgressiveSwapping = false, bool debug = false);
	DWORD			GetLastTriedToConnectTime() const				{ return m_dwLastTriedToConnect; }
	void			SetLastTriedToConnectTime()						{ m_dwLastTriedToConnect = ::GetTickCount(); }

	const CString	GetGeolocationData(const bool bForceCountryCity = false) const;
	int				GetCountryFlagIndex() const;
	void			ResetGeoLite2();
	void			ResetGeoLite2(const CAddress& dwIP);

#ifdef _DEBUG
	// Diagnostic Support
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext &dc) const;
#endif

	CClientReqSocket *socket;
	CClientCredits	*credits;
	CFriend			*m_Friend;
	uint8			*m_abyUpPartStatus;
	CTypedPtrList<CPtrList, CPartFile*> m_OtherRequests_list;
	CTypedPtrList<CPtrList, CPartFile*> m_OtherNoNeeded_list;
	uint16			m_lastPartAsked;
	bool			m_bAddNextConnect;

	void			SetSlotNumber(UINT newValue)					{ m_slotNumber = newValue; }
	UINT			GetSlotNumber() const							{ return m_slotNumber; }
	CEMSocket*		GetFileUploadSocket(bool bLog = false);

	uint8	m_cFailed; // holds the failed connection attempts

	const CString GetClientStatus() const
	{
		if (m_bIsArchived)
			return GetResString(_T("ARCHIVED"));
		else if (socket && socket->IsConnected())
			return GetResString(_T("CONNECTED"));
		else
			return GetResString(_T("DISCONNECTED"));
	}

	const CString GetSharedFilesStatusText() const {
		if (m_bQueryingSharedFiles)
			return GetResString(_T("SFS_QUERYING"));
		else if (m_bAutoQuerySharedFiles)
			return GetResString(_T("SFS_AUTO_QUERY"));
		else if (!GetViewSharedFilesSupport())
			return GetResString(_T("DISABLED"));
		else if (m_uSharedFilesStatus == S_NOT_QUERIED)
			return GetResString(_T("SFS_NOT_QUERIED"));
		else if (m_uSharedFilesStatus == S_NO_RESPONSE)
			return GetResString(_T("SFS_NO_RESPONSE"));
		else if (m_uSharedFilesStatus == S_RECEIVED)
			return GetResString(_T("SFS_RECEIVED"));
		else if (m_uSharedFilesStatus == S_ACCESS_DENIED)
			return GetResString(_T("SFS_ACCESS_DENIED"));

		return GetResString(_T("SFS_NOT_QUERIED")); // Returning this as the default value
	}

	uint8 m_uSharedFilesStatus;
	time_t	m_tSharedFilesLastQueriedTime;
	UINT m_uSharedFilesCount;
	bool m_bQueryingSharedFiles;
	bool m_bAutoQuerySharedFiles;

	CString m_strClientNote;

	uint8	IsBadClient()	const;
	void	ProcessBanMessage();
	void	IncXSAnswer() { m_uiXSAnswer++; } const
	void	IncXSReqs() { m_uiXSReqs++; } const
	const UINT GetXSReqs() const { return m_uiXSReqs; }
	const UINT GetXSAnswers() const { return m_uiXSAnswer; }
	const bool IsXSExploiter() const { return m_uiXSReqs > 2 && ((float)(m_uiXSAnswer + 1)) / m_uiXSReqs < 0.5f; }
	const CString GetPunishmentReason() const;
	const CString GetPunishmentText() const;
	void	SetOldUploadFileID();
	uchar*	GetOldUploadFileID() { return oldfileid; }
	uchar	oldfileid[16];
	uint8	m_uBadClientCategory;
	CString m_pszUsernameShield;
	uchar	m_achUserHashShield[MDX_DIGEST_SIZE];
	CString m_strModVersionShield;
	CString m_strClientSoftwareShield;
	bool	m_bForceRecheckShield;
	CString m_strPunishmentMessage; //hold the message temporary
	uint8	uhashsize;		// New United Community Detection [Xman] - Stulle
	bool	m_bUnitedComm;	// New United Community Detection [Xman] - Stulle
	UINT	m_uUserNameChangerCounter;	  //Xman Anti-Nick-Changer
	DWORD	m_dwUserNameChangerIntervalStart; //Xman Anti-Nick-Changer
	UINT	m_uModChangeCounter;
	DWORD	m_dwModChangerIntervalStart;
	uint32	m_uiXSAnswer;
	uint32	m_uiXSReqs;
	static	CString m_strAntiUserNameThiefNick;
	float	GetModVersionNumber(CString modversion) const;
	UINT	m_uTCPErrorCounter;
	DWORD	m_dwTCPErrorFlooderIntervalStart;

	void	CheckFileNotFound();
	bool	CheckFileRequest(CKnownFile* file);
	uint16	uiULAskingCounter;
	uint16	uiWaitingPositionRank;
	CString	m_strPunishmentReason;
	uint8	m_uPunishment = P_NOPUNISHMENT; //Set initial value
	time_t	m_tPunishmentStartTime = 0; //Set initial value to "no ban"
	bool	GetAntiUploaderCaseThree() { return m_bAntiUploaderCaseThree; }
	bool	m_bUploaderPunishmentPreventionActive;
	time_t	tFirstSeen;

	void SetClientNote();
	void SetAutoQuerySharedFiles(const bool bActivate);
	bool m_bAntiUploaderCaseThree;
protected:
	CPartFile* m_fileFNF;
	uint8 m_faileddownloads; 
public:
	time_t	tLastSeen;
	void	LoadFromFile(CFileDataIO& file);
	void	WriteToFile(CFileDataIO& file);
	bool	m_bIsArchived;
	CUpDownClient* m_ArchivedClient;
	/*
	uint32	m_nConnectIP;	// holds the supposed IP or (after we had a connection) the real IP
	uint32	m_dwUserIP;		// holds 0 (real IP not yet available) or the real IP (after we had a connection)
	*/
	CAddress m_ConnectIP;	// holds the supposed IP or (after we had a connection) the real IP
	CAddress m_UserIP;		// holds 0 (real IP not yet available) or the real IP (after we had a connection)
	CAddress m_UserIPv6;
	bool	 m_bOpenIPv6;
	CAddress m_UserIPv4;
private:
	uint8	m_uIncompletepartVer;
	uint8* m_abyIncPartStatus;
	uint16	m_nIncPartCount; // Actual length of m_abyIncPartStatus
public:
	uint8	GetIncompletePartVersion() const { return m_uIncompletepartVer; }
	void	ProcessFileIncStatus(CSafeMemFile* data, CPartFile* pReqFile, const bool bReadHash);
	bool	IsIncPartAvailable(UINT uPart) const { return m_abyIncPartStatus && uPart < m_nIncPartCount && m_abyIncPartStatus[uPart]; } // Guard with actual ICS array length to avoid OOB if m_nPartCount changes asynchronously.
	bool	IsXtremeBasedMod() const { return (m_strModVersion && (_tcscmp(m_strModVersion, L"Xtreme") == 0 || _tcscmp(m_strModVersion, L"DreaMule ") == 0 || _tcscmp(m_strModVersion, L"ScarAngel") == 0 || _tcscmp(m_strModVersion, L"Mephisto v") == 0)); }
	GeolocationData_Struct m_structClientGeolocationData;
	void CleanUp(const CPartFile* pDeletedFile);
	time_t m_tLastCleanUpCheck;
protected:
	const CString EmulateUserName(const bool m_bReturnUserName) const;
	const CString GenerateUserName() const;

	// base
	bool	ProcessHelloTypePacket(CSafeMemFile &data);
	void	SendHelloTypePacket(CSafeMemFile &data);
	void	QueueUtpHelloPacket();
	void	SendFirewallCheckUDPRequest();
	void	SendHashSetRequest();

	bool	DoSwap(CPartFile *SwapTo, bool bRemoveCompletely, LPCTSTR reason); // ZZ:DownloadManager
	bool	RecentlySwappedForSourceExchange()		{ return ::GetTickCount() < lastSwapForSourceExchangeTick + SEC2MS(30); } // ZZ:DownloadManager
	void	SetSwapForSourceExchangeTick()			{ lastSwapForSourceExchangeTick = ::GetTickCount(); } // ZZ:DownloadManager

	uint32	m_dwServerIP;
	uint32	m_nUserIDHybrid;
	uint16	m_nUserPort;
	uint16	m_nServerPort;
	UINT	m_nClientVersion;
	//--group aligned to int32
	uint8	m_byEmuleVersion;
	uint8	m_byDataCompVer;
	bool	m_bEmuleProtocol;
	bool	m_bIsHybrid;
	//--group aligned to int32
	TCHAR	*m_pszUsername;
	public:
	uchar	m_achUserHash[MDX_DIGEST_SIZE];
	protected:
	uint16	m_nUDPPort;
	uint16	m_nKadPort;
	//--group aligned to int32
	uint8	m_byUDPVer;
	uint8	m_bySourceExchange1Ver;
	uint8	m_byAcceptCommentVer;
	uint8	m_byExtendedRequestsVer;
	//--group aligned to int32
	uint8	m_byCompatibleClient;
	bool	m_bFriendSlot;
	bool	m_bCommentDirty;
	bool	m_bIsML;
	//--group aligned to int32
	bool	m_bGPLEvildoer;
	bool	m_bHelloAnswerPending;
	bool	m_bPendingStartUploadReq;
	uchar	m_PendingUpFileHash[16];
	uint8	m_byInfopacketsReceived; // have we received the edonkeyprot and emuleprot packet already (see InfoPacketsReceived() )
public:
	uint8	m_bySupportSecIdent;
protected:
	//--group aligned to int32
	//uint32	m_dwLastSignatureIP;
	CAddress m_dwLastSignatureIP;
	DWORD	m_dwLastSourceRequest;
	DWORD	m_dwLastSourceAnswer;
	DWORD	m_dwLastAskedForSources;
	uint32	m_uSearchID;
	int	m_iFileListRequested;

	CString m_strClientSoftware;
	CString m_strModVersion;
	CString	m_strFileComment;
	//--group aligned to int32
	uint8	m_uFileRating;
	uint8	m_cMessagesReceived;	// count of chatmessages he sent to me
	uint8	m_cMessagesSent;		// count of chatmessages I sent to him
	uint8	m_cCaptchasSent;
	//--group aligned to int32
	uint16	m_nServingBuddyPort;
	bool	m_bServingBuddyIDValid;
	bool	m_bUnicodeSupport;
	//--group aligned to int32

	//uint32	m_nBuddyIP;
	CAddress m_ServingBuddyIP;
	CAddress m_LastCallbackRequesterIP;
	DWORD	m_dwLastBuddyPingPongTime;
	uchar	m_achServingBuddyID[MDX_DIGEST_SIZE];
	CString m_strHelloInfo;
	CString m_strMuleInfo;
	CString m_strCaptchaChallenge;
	CString m_strCaptchaPendingMsg;

	// eServer Buddy (LowID relay for LowID-to-LowID transfers)
	bool	m_bSupportsEServerBuddy;		// eMuleAI + eServer Buddy support (from Hello)
	bool	m_bHasEServerBuddySlot;			// Has available serving slot (from Hello)
	bool	m_bSupportsEServerBuddyExtUdpPort;	// Supports external UDP port discovery (from Hello)
	bool	m_bSupportsEServerBuddyMagicProof;	// Supports buddy-request magic-file proof (from Hello)
	uint32	m_dwReportedServerIP;			// Server IP reported by this client
	DWORD	m_dwEServerBuddyRetryAfter;		// Don't retry buddy request until this time
	UINT	m_nEServerBuddyRequestCount;	// Rate limiting: request count in window
	DWORD	m_dwEServerBuddyFirstRequest;	// Rate limiting: window start time
	UINT	m_nEServerRelayRequestCount;	// Rate limiting: relay request count in window
	DWORD	m_dwEServerRelayFirstRequest;	// Rate limiting: relay window start time
	CUpDownClient* m_pPendingEServerRelayTarget;	// Target waiting for buddy reconnect to send relay
	DWORD	m_dwEServerExtPortProbeToken;	// Token for UDP probe verification (buddy -> served)
	DWORD	m_dwEServerExtPortProbeSent;	// Last UDP probe sent tick
	uint16	m_nObservedExternalUdpPort;		// Observed external UDP port (on buddy)
	DWORD	m_dwObservedExternalUdpPortTime;	// Observation time tick
	DWORD	m_dwLastEServerBuddyValidServerPing;	// Last keep-alive that confirmed peer is HighID on a server

	// NAT-T rendezvous retry scheduling
	DWORD	m_dwNattNextRetryTick;
	DWORD	m_dwUtpConnectionStartTick;  // Track uTP connection start for timeout handling
	DWORD	m_dwUtpQueuedPacketsTime;    // Track when packets were queued for delayed flush
	bool	m_bUtpWritable;
	bool	m_bUtpHelloQueued;
	bool	m_bUtpLocalInitiator;
	bool	m_bUtpNeedsQueuePurge;
	bool	m_bUtpInboundActivity;
	bool	m_bUtpHelloDeferred;
	bool	m_bUtpProactiveHelloSent;  // Track if proactive hello already sent for inbound uTP
	DWORD	m_dwUtpLastInboundTick;
	EUtpFrameType	m_uUtpLastFrameType;
	// Hello resend tracking for uTP when HelloAnswer is pending
	DWORD	m_dwHelloLastSentTick;
	uint8	m_uHelloResendCount;
	uint8	m_uNattRetryLeft;
	bool	m_bDeferredNatConnect;
	bool	m_bNatTFatalConnect;
	CAddress m_DeferredNatIP;
	uint16	m_uDeferredNatPort;
	uint16	m_uDeferredNatPortWindow;
	// Direct callback fallback timing
	bool	m_bAwaitingDirectCallback;
	bool	m_bAllowRendezvousAfterCallback;
	DWORD	m_dwDirectCallbackAttemptTick;
	bool	m_bEServerRelayNatTGuardActive;
	DWORD	m_dwEServerRelayNatTWindowStart;
	uint8	m_uEServerRelayTransientErrors;

	CTypedPtrList<CPtrList, Packet*> m_WaitingPackets_list;
	CList<PartFileStamp> m_DontSwap_list;

	uint8	m_byKadVersion;
	bool	m_bMultiPacket;

	// States
	EClientSoftware		m_clientSoft;
	EChatState			m_eChatstate;
	EKadState			m_eKadState;
	ESecureIdentState	m_SecureIdentState;
	CAddress			m_SecureIdentVerifiedIP;
	bool				m_bSecureIdentRecheckPending;
	EUploadState		m_eUploadState;
	EDownloadState		m_eDownloadState;
	ESourceFrom			m_eSourceFrom;
	EChatCaptchaState	m_eChatCaptchaState;
	EConnectingState	m_eConnectingState;


	////////////////////////////////////////////////////////////////////////
	// Upload
	//
	int GetFilePrioAsNumber() const;
	bool		m_bCollectionUploadSlot;
	uint16		m_nUpPartCount;
	uint16		m_nUpCompleteSourcesCount;

	uint64		m_nTransferredUp;
	uint64		m_nCurSessionUp;
	uint64		m_nCurSessionDown;
	uint64		m_nCurQueueSessionPayloadUp;
	uint64		m_addedPayloadQueueSession;
	DWORD		m_dwUploadTime;
	DWORD		m_dwLastUpRequest;
	UINT		m_cAsked;
	UINT		m_slotNumber;
	uchar		requpfileid[MDX_DIGEST_SIZE];

	typedef struct
	{
		uint32	datalen;
		DWORD	timestamp;
	} TransferredData;

public:
	// NAT-T rendezvous retry helpers
	void		MarkNatTRendezvous(uint8 retries, bool bForceImmediate = false);
	bool		IsNatTRetryDue(DWORD now) const { return !m_bNatTFatalConnect && m_uNattRetryLeft > 0 && (int)(now - m_dwNattNextRetryTick) >= 0; }
	void		ScheduleNextNatTRetry(DWORD now, DWORD jmin, DWORD jmax);
	void		DoNatTRetry();
	void		QueueDeferredNatConnect(const CAddress& ip, uint16 port, uint16 window = 0);
	void		FlagNatTFatalConnectFailure();
	void		ClearNatTFatalConnectFailure();
	void		ArmEServerRelayNatTGuard();
	void		ClearEServerRelayNatTGuard();
	void		NormalizeEServerRelayNatTGuard();
	bool		RegisterEServerRelayTransientError();
	void		AbortEServerRelayNatTraversal(LPCTSTR pszReason = NULL);
	bool		HasNatTFatalConnectFailure() const { return m_bNatTFatalConnect; }
	bool		IsEServerRelayNatTGuardActive() const { return m_bEServerRelayNatTGuardActive; }
	bool		ShouldAllowNatTRetryReseed() const { return !m_bEServerRelayNatTGuardActive || m_uNattRetryLeft > 0; }
	bool		HasPendingNatTRetry() const { return m_uNattRetryLeft > 0; }
	bool		HasNatTraversalFileContext() const;
	CString	DbgGetNatTraversalContext() const;
	CTypedPtrList<CPtrList, Requested_File_Struct*>	 m_RequestedFiles_list;

	//////////////////////////////////////////////////////////
	// Download
	//
	CPartFile	*m_reqfile;
	CAICHHash	*m_pReqFileAICHHash;
	uint8		*m_abyPartStatus;
	CString		m_strClientFilename;
	uint64		m_nTransferredDown;
	uint64		m_nCurSessionPayloadDown;
	uint64		m_nLastBlockOffset;
	DWORD		m_dwDownStartTime;
	DWORD		m_dwLastBlockReceived;
	DWORD		m_dwLastNatStallRecover;
	UINT		m_uNatStallRecoverBurst;
	DWORD		m_dwLastNatKeepAliveSent;	// Uploader side NAT keep-alive timestamp
	UINT		m_cDownAsked;
	UINT		m_nTotalUDPPackets;
	UINT		m_nFailedUDPPackets;
	UINT		m_nRemoteQueueRank;
	//--group aligned to int32
	bool		m_bRemoteQueueFull;
	bool		m_bCompleteSource;
	uint16		m_nPartCount;
	//--group aligned to int32
	uint16		m_cShowDR;
	bool		m_bReaskPending;
	bool		m_bUDPPending;
	bool		m_bTransferredDownMini;

	//////////////////////////////////////////////////////////
	// Upload data rate computation
	//
	UINT		m_nUpDatarate;
	uint64		m_nSumForAvgUpDataRate;
	CList<TransferredData> m_AverageUDR_list;

	//////////////////////////////////////////////////////////
	// Download data rate computation
	//
	uint64		m_nSumForAvgDownDataRate;
	CList<TransferredData> m_AverageDDR_list;
	UINT		m_nDownDatarate;
	UINT		m_nDownDataRateMS;

	// Download from URL
	CStringA	m_strUrlPath;
	uint64		m_uReqStart;
	uint64		m_uReqEnd;
	uint64		m_nUrlStartPos;

	//////////////////////////////////////////////////////////
	// GUI helpers
	//
	static CBarShader s_StatusBar;
	static CBarShader s_UpStatusBar;
	static void ReleaseBarShaders() noexcept;
	CTypedPtrList<CPtrList, Pending_Block_Struct*> m_PendingBlocks_list;
	typedef CMap<const CPartFile*, const CPartFile*, DWORD, DWORD> CFileReaskTimesMap;
	CFileReaskTimesMap m_fileReaskTimes; // ZZ:DownloadManager (one re-ask timestamp for each file)
	DWORD   lastSwapForSourceExchangeTick;	// ZZ:DownloadManaager
	DWORD   m_dwLastTriedToConnect;			// ZZ:DownloadManager (one re-ask timestamp for each file)
	DWORD		m_lastRefreshedULDisplay;
	DWORD		m_random_update_wait;

	// Fail-fast backoff for unexpected blocks
	DWORD		m_dwLastInvalidBlockStateTick; // ms tick of last invalid-state block
	UINT		m_uInvalidBlockStateBurst;     // burst counter within window
	void		OnUnexpectedBlockWhileNotDownloading(uint32 uSize); // fast cleanup + backoff

	DWORD		m_dwLastUnexpectedBlockTick; // ms tick of last out-of-window block
	UINT		m_uUnexpectedBlockBurst;     // burst counter within window
	void		OnUnexpectedBlockWhileDownloading(uint64 nStartPos, uint64 nEndPos, uint32 uSize); // resync & backoff

	// using bit fields for less important flags, to save some bytes
	UINT m_fHashsetRequestingMD4 : 1, // we have sent a hashset request to this client in the current connection
		 m_fSharedDirectories : 1, // client supports OP_ASKSHAREDIRS opcodes
		 m_fSentCancelTransfer: 1, // we have sent an OP_CANCELTRANSFER in the current connection
		 m_fNoViewSharedFiles : 1, // client has disabled the 'View Shared Files' feature, if this flag is not set, we just know that we don't know for sure if it is enabled
		 m_fSupportsPreview   : 1,
		 m_fPreviewReqPending : 1,
		 m_fPreviewAnsPending : 1,
		 m_fIsSpammer		  : 1,
		 m_fMessageFiltered   : 1,
		 m_fPeerCache		  : 1,
		 m_fQueueRankPending  : 1,
		 m_fUnaskQueueRankRecv: 2,
		 m_fFailedFileIdReqs  : 4, // nr. of failed file-id related requests per connection
		 m_fNeedOurPublicIP	  : 1, // we requested our IP from this client
		 m_fSupportsAICH	  : 3,
		 m_fAICHRequested	  : 1,
		 m_fSentOutOfPartReqs : 1,
		 m_fSupportsLargeFiles: 1,
		 m_fExtMultiPacket	  : 1,
		 m_fRequestsCryptLayer: 1,
		 m_fSupportsCryptLayer: 1,
		 m_fRequiresCryptLayer: 1,
		 m_fSupportsSourceEx2 : 1,
		 m_fSupportsCaptcha	  : 1,
		 m_fDirectUDPCallback : 1,
		 m_fSupportsFileIdent : 1; // 0 bits left
	UINT m_fHashsetRequestingAICH : 1; // 31 bits left

	bool	m_bSourceExchangeSwapped; // ZZ:DownloadManager

	UModMiscOptions m_ModMiscOptions;

public:
	const DWORD	GetSpreadReAskTime() const { return m_dwSpreadReAskTime; }
private:
	DWORD	m_dwSpreadReAskTime;

public:
		void SetLastAskedTime(const CPartFile* partFile, const uint32& in) { m_fileReaskTimes.SetAt(partFile, in); } // thx 4 this code fly out to WiZaRd
		void DoSendIP() { m_bSendIP = true; }
		void WeSentIP() { m_bSendIP = false; }
		bool GetSendIP() const { return m_bSendIP; }
		void SendIPChange();
protected:
		bool m_bSendIP;
public:
	void TrigNextSafeAskForDownload(CPartFile* pFile);
};
