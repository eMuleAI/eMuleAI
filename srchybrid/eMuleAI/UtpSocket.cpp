//This file is part of eMule AI
//Copyright (C)2013 David Xanatos ( XanatosDavid (a) gmail.com / http://NeoLoader.to )
//Copyright (C)2026 eMule AI
//

#include "stdafx.h"
#include "UtpSocket.h"
#include "../../libutp/include/libutp/utp.h"
#include "../emule.h"
#include "../ClientUDPSocket.h"
#include "../ListenSocket.h"
#include "../Log.h"
#include "../ClientList.h"
#include "../UpdownClient.h"
#include "../OtherFunctions.h"
#include "../Opcodes.h"
#include <set>
#include <vector>
#include <iphlpapi.h> // dynamic MTU query (GetIpInterfaceEntry)
#include <netioapi.h> // dynamic route query (GetBestRoute2)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> // time helpers

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static CCriticalSection g_utpSocketsLock; // Thread safety for g_UtpSockets set
static CCriticalSection g_utpRuntimeLock; // libutp is driven from UDP callbacks and worker ticks; serialize all runtime API access.
static CCriticalSection g_expectedPeersLock; // Dedicated lock for expected-peers book keeping
static LARGE_INTEGER g_qpcFreq = { 0 }; // High-resolution timing support
// Track configured contexts to allow multiple contexts without re-setting callbacks.
static CCriticalSection g_utpCtxLock;
static std::set<utp_context*> g_utpConfiguredContexts;
static unsigned long long g_randState = 0ULL; // simple LCG state for RNG
static const size_t kMaxUtpSockets = 2048; // Soft cap for active uTP endpoints
static const size_t kUtpInitialAppWriteBufferSize = 64 * 1024;
static const size_t kUtpFirstAdaptiveAppWriteBufferSize = 512 * 1024;
static const size_t kUtpSecondAdaptiveAppWriteBufferSize = 2 * 1024 * 1024;
static const size_t kUtpMaxAdaptiveAppWriteBufferSize = 4 * 1024 * 1024;
static const size_t kUtpDuplexMaxAdaptiveAppWriteBufferSize = kUtpFirstAdaptiveAppWriteBufferSize;
static const uint32 kUtpWriteBufferPromotionBlocks = 64;
static const DWORD kUtpAppWriteBufferIdleShrinkMs = 30000;
static const size_t kUtpMaxWriteAttemptSize = 256 * 1024;
static const UINT kUtpMaxWriteAttemptsPerProcess = 4;
static const size_t kUtpMaxWriteBytesPerProcess = 256 * 1024;
static const DWORD kUtpZeroWriteRetryBaseMs = 20;
static const DWORD kUtpZeroWriteRetryStepMs = 5;
static const DWORD kUtpZeroWriteRetryMaxMs = 125;

// Best-effort dynamic MTU query via IP Helper API (loaded at runtime)
static bool QueryMtuForPeer(const sockaddr* to, socklen_t tolen, uint32& mtu)
{
	if (!to || tolen <= 0)
		return false;

	bool bResult = false;
	__try {
		HMODULE hIpHlp = GetModuleHandleW(L"iphlpapi.dll");
		if (!hIpHlp)
			hIpHlp = LoadLibraryW(L"iphlpapi.dll");

		if (!hIpHlp)
			return false;

		// Resolve required functions dynamically
		typedef DWORD (WINAPI *PFN_GetBestInterfaceEx)(const SOCKADDR*, PDWORD);
		typedef NETIO_STATUS (WINAPI *PFN_GetIpInterfaceEntry)(PMIB_IPINTERFACE_ROW);
		PFN_GetBestInterfaceEx pGetBestInterfaceEx = (PFN_GetBestInterfaceEx)(void*)GetProcAddress(hIpHlp, "GetBestInterfaceEx");
		PFN_GetIpInterfaceEntry pGetIpInterfaceEntry = (PFN_GetIpInterfaceEntry)(void*)GetProcAddress(hIpHlp, "GetIpInterfaceEntry");

		if (!pGetBestInterfaceEx || !pGetIpInterfaceEntry)
			return false;

		SOCKADDR_INET dest = {};
		if (to->sa_family == AF_INET && tolen >= sizeof(sockaddr_in)) {
			dest.si_family = AF_INET;
			memcpy(&dest.Ipv4, to, sizeof(sockaddr_in));
		} else if (to->sa_family == AF_INET6 && tolen >= sizeof(sockaddr_in6)) {
			dest.si_family = AF_INET6;
			memcpy(&dest.Ipv6, to, sizeof(sockaddr_in6));
		} else {
			return false;
		}

		const SOCKADDR* pDestSock = (dest.si_family == AF_INET)
			? reinterpret_cast<const SOCKADDR*>(&dest.Ipv4)
			: reinterpret_cast<const SOCKADDR*>(&dest.Ipv6);

		DWORD ifIndex = 0;
		DWORD dwIfResult = pGetBestInterfaceEx(pDestSock, &ifIndex);
		if (dwIfResult != NO_ERROR)
			return false;

		MIB_IPINTERFACE_ROW row;
		memset(&row, 0, sizeof(row));
		row.Family = dest.si_family;
		row.InterfaceIndex = ifIndex;
		NETIO_STATUS st = pGetIpInterfaceEntry(&row);
		if (st != 0)
			return false;

		if (row.NlMtu == 0)
			return false;

		mtu = (uint32)row.NlMtu;
		bResult = true;
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLogWarning(_T("[NatTraversal][uTP] GetBestRoute2/GetIpInterfaceEntry raised SEH; using default MTU"));
		bResult = false;
	}

	return bResult;
}

// Expected peer tracking for conditional uTP accept (IP:port + TTL)
struct ExpectedPeerEntry {
	sockaddr_storage addr;
	socklen_t len;
	DWORD expires;
	CUtpSocket* owner;
};
static std::vector<ExpectedPeerEntry> g_expectedPeers;
static const DWORD kExpectedPeerTTLms = 20000; // 20s TTL

static bool ExtractIPv4(const sockaddr* sa, uint32& ip, uint16* port)
{
    if (!sa)
        return false;
    if (sa->sa_family == AF_INET) {
        const sockaddr_in* p4 = reinterpret_cast<const sockaddr_in*>(sa);
        ip = ntohl(p4->sin_addr.S_un.S_addr); // Convert to host byte order for numeric comparison
        if (port) *port = ntohs(p4->sin_port);
        return true;
    }
    if (sa->sa_family == AF_INET6) {
        const sockaddr_in6* p6 = reinterpret_cast<const sockaddr_in6*>(sa);
        const uint8* a = reinterpret_cast<const uint8*>(&p6->sin6_addr);
        // Check v4-mapped (::ffff:a.b.c.d)
        static const uint8 v4map[12] = {0,0,0,0,0,0,0,0,0,0,0xFF,0xFF};
        if (memcmp(a, v4map, 12) == 0) {
            uint32 v4;
            memcpy(&v4, a + 12, 4);
            ip = ntohl(*(const uint32*)&v4); // Convert to host byte order for numeric comparison
            if (port) *port = ntohs(p6->sin6_port);
            return true;
        }
    }
    return false;
}

// Helper: Create sockaddr_in from IPv4 address (already in host byte order) and port
static bool MakeSockaddrIPv4(sockaddr_storage& sa, socklen_t& salen, uint32 ip, uint16 port)
{
    if (ip == 0)
        return false;
    sockaddr_in* p4 = reinterpret_cast<sockaddr_in*>(&sa);
    memset(p4, 0, sizeof(*p4));
    p4->sin_family = AF_INET;
    p4->sin_addr.S_un.S_addr = htonl(ip); // ip is in host byte order, convert to network
    p4->sin_port = htons(port);
    salen = sizeof(sockaddr_in);
    return true;
}

static uint16 GetBestLocalUtpPort()
{
	uint16 nPort = thePrefs.GetBestExternalUdpPort();
	if (nPort == 0)
		nPort = thePrefs.GetUDPPort();
	if (nPort == 0)
		nPort = thePrefs.GetPort();
	return nPort;
}

static bool SockaddrEqual(const sockaddr* a, socklen_t alen, const sockaddr* b, socklen_t blen)
{
    if (!a || !b || alen <= 0 || blen <= 0)
        return false;

    if (a->sa_family == b->sa_family) {
        if (a->sa_family == AF_INET) {
            const sockaddr_in* pa = reinterpret_cast<const sockaddr_in*>(a);
            const sockaddr_in* pb = reinterpret_cast<const sockaddr_in*>(b);
            return pa->sin_port == pb->sin_port && pa->sin_addr.S_un.S_addr == pb->sin_addr.S_un.S_addr;
        }
        if (a->sa_family == AF_INET6) {
            const sockaddr_in6* pa6 = reinterpret_cast<const sockaddr_in6*>(a);
            const sockaddr_in6* pb6 = reinterpret_cast<const sockaddr_in6*>(b);
            return pa6->sin6_port == pb6->sin6_port && memcmp(&pa6->sin6_addr, &pb6->sin6_addr, sizeof(in6_addr)) == 0;
        }
        return false;
    }

    // Cross-family compare via v4-mapped equivalence
    uint32 a4, b4; uint16 ap = 0, bp = 0;
    if (ExtractIPv4(a, a4, &ap) && ExtractIPv4(b, b4, &bp))
        return a4 == b4 && ap == bp;
    return false;
}

// Compare only IP address, ignore port (for NAT remap tolerant matching)
static bool SockaddrEqualIPOnly(const sockaddr* a, socklen_t alen, const sockaddr* b, socklen_t blen)
{
    if (!a || !b || alen <= 0 || blen <= 0)
        return false;

    if (a->sa_family == b->sa_family) {
        if (a->sa_family == AF_INET) {
            const sockaddr_in* pa = reinterpret_cast<const sockaddr_in*>(a);
            const sockaddr_in* pb = reinterpret_cast<const sockaddr_in*>(b);
            return pa->sin_addr.S_un.S_addr == pb->sin_addr.S_un.S_addr;
        }
        if (a->sa_family == AF_INET6) {
            const sockaddr_in6* pa6 = reinterpret_cast<const sockaddr_in6*>(a);
            const sockaddr_in6* pb6 = reinterpret_cast<const sockaddr_in6*>(b);
            return memcmp(&pa6->sin6_addr, &pb6->sin6_addr, sizeof(in6_addr)) == 0;
        }
        return false;
    }
    uint32 a4, b4;
    if (ExtractIPv4(a, a4, NULL) && ExtractIPv4(b, b4, NULL))
        return a4 == b4;
    return false;
}

// Helper to compare two sockaddr by IP first (numerically), then PORT.
// Returns: <0 if a<b, 0 if equal, >0 if a>b.
// Handles IPv4, IPv6, and IPv4-mapped-IPv6 cases.
static int CompareSockaddrIPAndPort(const sockaddr* a, socklen_t alen, const sockaddr* b, socklen_t blen)
{
	if (!a || !b || alen <= 0 || blen <= 0)
		return 0; // cannot compare, treat as equal

	uint32 a4 = 0, b4 = 0;
	uint16 ap = 0, bp = 0;
	bool aIsV4 = false, bIsV4 = false;

	// Try extracting IPv4 addresses and ports (handles native IPv4 and IPv4-mapped-IPv6)
	if (ExtractIPv4(a, a4, &ap))
		aIsV4 = true;
	if (ExtractIPv4(b, b4, &bp))
		bIsV4 = true;

	// If both are IPv4 (or IPv4-mapped), compare numerically
	if (aIsV4 && bIsV4) {
		if (a4 < b4) return -1;
		if (a4 > b4) return +1;
		// IP equal, compare ports
		if (ap < bp) return -1;
		if (ap > bp) return +1;
		return 0;
	}

	// If one is IPv4 and the other IPv6 (pure), we can't compare meaningfully.
	// For simplicity, treat pure IPv6 as "larger" than IPv4.
	if (aIsV4 && !bIsV4) return -1;
	if (!aIsV4 && bIsV4) return +1;

	// Both are pure IPv6 - compare byte-by-byte
	if (a->sa_family == AF_INET6 && b->sa_family == AF_INET6) {
		const sockaddr_in6* pa6 = reinterpret_cast<const sockaddr_in6*>(a);
		const sockaddr_in6* pb6 = reinterpret_cast<const sockaddr_in6*>(b);
		int cmp = memcmp(&pa6->sin6_addr, &pb6->sin6_addr, sizeof(in6_addr));
		if (cmp != 0) return cmp;
		// IP equal, compare ports
		uint16 aport = ntohs(pa6->sin6_port);
		uint16 bport = ntohs(pb6->sin6_port);
		if (aport < bport) return -1;
		if (aport > bport) return +1;
		return 0;
	}

	// Fallback (should not reach here if logic is complete)
	return 0;
}

static bool SockaddrEqualIPAndPort(const sockaddr* a, socklen_t alen, const sockaddr* b, socklen_t blen)
{
    if (!a || !b || alen <= 0 || blen <= 0)
        return false;

    if (a->sa_family == b->sa_family) {
        if (a->sa_family == AF_INET) {
            const sockaddr_in* pa = reinterpret_cast<const sockaddr_in*>(a);
            const sockaddr_in* pb = reinterpret_cast<const sockaddr_in*>(b);
            return (pa->sin_addr.S_un.S_addr == pb->sin_addr.S_un.S_addr) && (pa->sin_port == pb->sin_port);
        }
        if (a->sa_family == AF_INET6) {
            const sockaddr_in6* pa6 = reinterpret_cast<const sockaddr_in6*>(a);
            const sockaddr_in6* pb6 = reinterpret_cast<const sockaddr_in6*>(b);
            return (memcmp(&pa6->sin6_addr, &pb6->sin6_addr, sizeof(in6_addr)) == 0) && (pa6->sin6_port == pb6->sin6_port);
        }
        return false;
    }
    // IPv4-mapped IPv6 comparison
    uint32 a4, b4;
    uint16 ap, bp;
    if (ExtractIPv4(a, a4, &ap) && ExtractIPv4(b, b4, &bp))
        return (a4 == b4) && (ap == bp);
    return false;
}

static void RemoveExpectedPeersForOwner(CUtpSocket* owner)
{
	if (!owner)
		return;
	g_expectedPeersLock.Lock();
	for (size_t i = 0; i < g_expectedPeers.size(); ) {
		if (g_expectedPeers[i].owner == owner)
			g_expectedPeers.erase(g_expectedPeers.begin() + i);
		else
			++i;
	}
	g_expectedPeersLock.Unlock();
}

static void AddExpectedPeer(CUtpSocket* owner, const sockaddr* to, socklen_t tolen)
{
	if (!owner || !to || tolen <= 0)
		return;

	// Sweep expired entries
	DWORD now = ::GetTickCount();
	g_expectedPeersLock.Lock();
	for (size_t i = 0; i < g_expectedPeers.size();) {
		if ((int)(now - g_expectedPeers[i].expires) >= 0) {
			g_expectedPeers.erase(g_expectedPeers.begin() + i);
			continue;
		}
		++i;
	}

	ExpectedPeerEntry e = {};
	memcpy(&e.addr, to, (size_t)tolen > sizeof(e.addr) ? sizeof(e.addr) : (size_t)tolen);
	e.len = tolen;
	e.expires = ::GetTickCount() + kExpectedPeerTTLms;
	e.owner = owner;

	// De-dup by address only (regardless of owner) to prevent duplicate uTP sockets
	// Use IP+port comparison to avoid sockaddr struct padding/format differences
	for (size_t i = 0; i < g_expectedPeers.size(); ++i) {
		if (SockaddrEqualIPAndPort((sockaddr*)&g_expectedPeers[i].addr, g_expectedPeers[i].len, to, tolen)) {
			// Already exists - refresh TTL and prefer new owner
			g_expectedPeers[i].expires = e.expires;
			g_expectedPeers[i].owner = owner;
			g_expectedPeersLock.Unlock();
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[uTP][ExpectPeer] Duplicate prevented: entry already exists for this endpoint, refreshing TTL only"));
			return;
		}
	}

	g_expectedPeers.push_back(e);
	g_expectedPeersLock.Unlock();
}

static CUpDownClient* ResolveNatClient(CUtpSocket* pSocket)
{
	if (pSocket == NULL)
		return NULL;
	CUpDownClient* client = pSocket->GetOwnerClient();
	if (client != NULL && !theApp.clientlist->IsClientActive(client))
		client = NULL;
	return client;
}

static bool HasActiveNatTraversalTransfer(const CUpDownClient* pClient)
{
	return pClient != NULL && (pClient->GetDownloadState() == DS_DOWNLOADING || pClient->GetUploadState() == US_UPLOADING);
}

static void RefreshAcceptedUtpUploadHandshakeGuard(CUpDownClient* pClient, LPCTSTR pszReason)
{
	if (pClient == NULL || theApp.clientlist == NULL || !theApp.clientlist->IsValidClient(pClient))
		return;

	if (pClient->GetUploadState() == US_UPLOADING || pClient->GetUploadState() == US_CONNECTING) {
		pClient->SetUploadState(US_CONNECTING);
		pClient->SetUpStartTime();
		if (thePrefs.GetLogNatTraversalEvents()) {
			DebugLog(_T("[NatTraversal][uTP] Refreshed upload handshake guard after accepted transport (%s), %s"),
				pszReason != NULL ? pszReason : _T("accepted uTP"), (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
		}
	}
}

uint64 CUtpSocket::on_utp_state_change(utp_callback_arguments* a)
{
	// Defensive: ignore events until userdata is bound
	CUtpSocket* pSocket = (CUtpSocket*)utp_get_userdata(a->socket);
	if (!pSocket)
		return 0;
	if (pSocket->m_Socket != a->socket)
		return 0;
	CUpDownClient* natClient = ResolveNatClient(pSocket);

	switch (a->state)
	{
	case UTP_STATE_CONNECT:
		pSocket->m_nLayerState = connected;
		if (!pSocket->m_bConnectNotified) {
			pSocket->m_bConnectNotified = true;
			pSocket->OnConnect(0);
		}
		{
			CSingleLock writeLock(&pSocket->m_csWriteBuffer, TRUE);
			pSocket->m_bUtpWriteBlocked = false;
			pSocket->m_bUtpWritableHint = false;
			pSocket->m_dwNextUtpWriteAttemptTick = 0;
		}
		// Don't call OnSend here - ConnectionEstablished will handle queue flushing
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal][uTP] CONNECT state"));
		if (thePrefs.GetVerboseLogPriority() <= DLP_LOW)
			AddDebugLogLine(DLP_LOW, false, _T("[NAT-T][uTP] CONNECT state"));
		if (natClient)
			natClient->SetUtpWritable(false);
		break;
	case UTP_STATE_WRITABLE:
		{
			CSingleLock writeLock(&pSocket->m_csWriteBuffer, TRUE);
			pSocket->m_bUtpWriteBlocked = false;
			pSocket->m_bUtpWritableHint = true;
			pSocket->m_dwNextUtpWriteAttemptTick = 0;
		}
		pSocket->OnSend(0);
		if (!pSocket->m_bConnectNotified) {
			pSocket->m_nLayerState = connected;
			pSocket->m_bConnectNotified = true;
			pSocket->OnConnect(0);
		}
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal][uTP] WRITABLE state"));
		if (thePrefs.GetVerboseLogPriority() <= DLP_LOW)
			AddDebugLogLine(DLP_LOW, false, _T("[NAT-T][uTP] WRITABLE state"));
		if (natClient) {
			natClient->SetUtpWritable(true);
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal][uTP] WRITABLE state confirmed for %s"), (LPCTSTR)EscPercent(natClient->DbgGetClientInfo()));
		}
		break;
	case UTP_STATE_EOF:
		pSocket->m_nLayerState = closed;
		pSocket->OnClose(0);
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal][uTP] EOF state"));
		if (thePrefs.GetVerboseLogPriority() <= DLP_LOW)
			AddDebugLogLine(DLP_LOW, false, _T("[NAT-T][uTP] EOF state"));
		if (natClient)
			natClient->ResetUtpFlowControl();
		break;
	case UTP_STATE_DESTROYING:
		// Do not call utp_close here; libutp is already destroying this socket.
		// Just clear expected peers and local handle to avoid double-close.
		if (natClient)
			natClient->ResetUtpFlowControl();
		RemoveExpectedPeersForOwner(pSocket);
		pSocket->m_Socket = NULL;
		// Remove from active set
		g_utpSocketsLock.Lock();
		{
			std::set<CUtpSocket*>::iterator it = theApp.g_UtpSockets.find(pSocket);
			if (it != theApp.g_UtpSockets.end())
				theApp.g_UtpSockets.erase(it);
		}
		g_utpSocketsLock.Unlock();
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal][uTP] DESTROYING state"));
		if (thePrefs.GetVerboseLogPriority() <= DLP_LOW)
			AddDebugLogLine(DLP_LOW, false, _T("[NAT-T][uTP] DESTROYING state"));
		break;
	default:
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] on_utp_state_change: Unknown state change: CallbackType:%i State:%i"), a->callback_type, a->state);
	}
	return 0;
}

uint64 CUtpSocket::on_utp_read(utp_callback_arguments* a)
{




	CUtpSocket* pSocket = (CUtpSocket*)utp_get_userdata(a->socket);
	if (!pSocket)
		return 0; // not bound yet
	if (pSocket->m_Socket != a->socket)
		return 0;
	if (pSocket->m_ShutDown & 0x01) // socket is shutting down
		return 1;

	{
		CSingleLock readLock(&pSocket->m_csReadBuffer, TRUE);
		pSocket->m_ReadBuffer.AppendData(a->buf, a->len); // data have been received
	}
	pSocket->OnReceive(0);  // Trigger receive notification
	
	// Tell libutp that we've consumed the data from its internal buffer
	// This allows libutp to send ACKs and advance the receive window
	// Without this, sender's congestion window remains closed and writes return 0
	utp_read_drained(a->socket);
	

	return 0;
}

uint64 CUtpSocket::on_utp_sendto(utp_callback_arguments* a)
{
	// Bind context userdata to UDP transport
	CClientUDPSocket* pUDPSocket = (CClientUDPSocket*)utp_context_get_userdata(a->context);
	if (!pUDPSocket)
		return 0; // context not bound (should not happen), drop silently
	if (a->buf == NULL || a->len == 0 || a->address == NULL || a->address_len <= 0)
		return 0; // malformed callback payload, ignore safely

	// Forward the serialized uTP packet through the shared UDP transport.
	pUDPSocket->SendUtpPacket((const byte*)a->buf, a->len, a->address, (socklen_t)a->address_len);

	return 0;
}

uint64 CUtpSocket::on_utp_error(utp_callback_arguments* a)
{
	// Map libutp error codes to proper WSA codes
	int Error;
	switch (a->error_code)
	{
	case UTP_ECONNREFUSED:	Error = WSAECONNREFUSED;	break;
	case UTP_ECONNRESET:	Error = WSAECONNRESET;		break;
	case UTP_ETIMEDOUT:		Error = WSAETIMEDOUT;		break;
	default:				Error = SOCKET_ERROR;		break;
	}

	CUtpSocket* pSocket = (CUtpSocket*)utp_get_userdata(a->socket);
	if (!pSocket)
		return 0;
	if (pSocket->m_Socket != a->socket) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal][uTP] Ignored stale error callback (callbackSocket=%p currentSocket=%p code=%d)"), a->socket, pSocket->m_Socket, a->error_code);
		return 0;
	}
	CUpDownClient* natClient = ResolveNatClient(pSocket);

	sockaddr_storage peer = {};
	socklen_t peerlen = sizeof(peer);
	const bool bHavePeer = (a->socket != NULL && utp_getpeername(a->socket, (sockaddr*)&peer, &peerlen) == 0);
	const bool bTransientNatError = (Error == WSAECONNRESET || Error == WSAETIMEDOUT);

	if (bTransientNatError) {
		CAddress retryIP;
		uint16 retryPort = 0;
		bool bRetryScheduled = false;

		// Keep a short recovery window for simultaneous-connect retries.
		if (bHavePeer && natClient != NULL && theApp.clientudp != NULL) {
			CAddress peerIP;
			uint16 peerPort = 0;
			peerIP.FromSA((sockaddr*)&peer, peerlen, &peerPort);
			if (!peerIP.IsNull() && peerPort != 0) {
				retryIP = peerIP;
				retryPort = peerPort;
				theApp.clientudp->SeedNatTraversalExpectation(natClient, peerIP, peerPort);
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal][uTP] ERROR recovery: re-seeded NAT expectation for %s:%u"), (LPCTSTR)ipstr(peerIP), (UINT)peerPort);
			}
		}

		const bool bHadConnectingState = (natClient != NULL && natClient->GetConnectingState() != CCS_NONE);
		const bool bActiveLowIdUploadRetry = (natClient != NULL
			&& bHavePeer
			&& theApp.IsFirewalled()
			&& natClient->HasLowID()
			&& (natClient->GetUploadState() == US_UPLOADING || natClient->GetUploadState() == US_CONNECTING));
		const bool bNeedNatRetry = (natClient != NULL &&
			(natClient->IsHelloAnswerPending() ||
			 bHadConnectingState ||
			 natClient->GetDownloadState() == DS_WAITCALLBACK ||
			 natClient->GetDownloadState() == DS_CONNECTING ||
			 bActiveLowIdUploadRetry));

		// Transient reset/timeout during simultaneous open should not tear down the client object.
		pSocket->m_nLayerState = unconnected;
		pSocket->m_bConnectNotified = false;
		pSocket->m_bTransportFailed = true;
		pSocket->Destroy();
		CEMSocket* ownerSocket = dynamic_cast<CEMSocket*>(pSocket->GetOwnerSocket());
		if (ownerSocket != NULL)
			ownerSocket->SetConState(EMS_NOTCONNECTED);
		if (natClient != NULL) {
			if (bHadConnectingState) {
				natClient->ResetConnectingState();
				theApp.clientlist->RemoveConnectingClient(natClient);
			}
			natClient->ResetUtpFlowControl();
			bool bRetryAllowed = true;
			if (bNeedNatRetry && natClient->IsEServerRelayNatTGuardActive())
				bRetryAllowed = natClient->RegisterEServerRelayTransientError();
			natClient->ClearHelloAnswerPending();
			if (!bNeedNatRetry)
				natClient->NormalizeEServerRelayNatTGuard();

			if (bNeedNatRetry && bRetryAllowed) {
				if (retryIP.IsNull() || retryPort == 0) {
					retryIP = natClient->GetConnectIP();
					if (retryIP.IsNull())
						retryIP = natClient->GetIP();
					retryPort = natClient->GetKadPort();
					if (retryPort == 0)
						retryPort = natClient->GetUDPPort();
				}
				const bool bHaveRetryEndpoint = (!retryIP.IsNull() && retryPort != 0);
				if (bHaveRetryEndpoint) {
					natClient->QueueDeferredNatConnect(retryIP, retryPort, thePrefs.GetNatTraversalPortWindow());
					bRetryScheduled = natClient->HasPendingNatTRetry();
				} else if (natClient->ShouldAllowNatTRetryReseed()) {
					natClient->MarkNatTRendezvous(1, true);
					bRetryScheduled = natClient->HasPendingNatTRetry();
				}

				// Guarded relay path may consume retry budget between attempts.
				// Keep one immediate retry armed so transient recovery can progress until guard cap kicks in.
				if (!bRetryScheduled && natClient->IsEServerRelayNatTGuardActive()) {
					natClient->MarkNatTRendezvous(1, true);
					if (bHaveRetryEndpoint)
						natClient->QueueDeferredNatConnect(retryIP, retryPort, thePrefs.GetNatTraversalPortWindow());
					bRetryScheduled = natClient->HasPendingNatTRetry();

					if (bRetryScheduled && thePrefs.GetLogNatTraversalEvents()) {
						DebugLog(_T("[NatTraversal][uTP] ERROR recovery: forced single relay retry re-arm for %s"),
							(LPCTSTR)EscPercent(natClient->DbgGetClientInfo()));
					}
				}
			}

			if (bRetryScheduled && (natClient->GetUploadState() == US_UPLOADING || natClient->GetUploadState() == US_CONNECTING)) {
				if (ownerSocket != NULL)
					ownerSocket->ResetTransportStateForReconnect();
				natClient->SetUploadState(US_CONNECTING);
				natClient->SetUpStartTime();
				if (thePrefs.GetLogNatTraversalEvents()) {
					DebugLog(_T("[NatTraversal][uTP] ERROR recovery: preserving upload slot in CONNECTING state for retry, %s"),
						(LPCTSTR)EscPercent(natClient->DbgGetClientInfo()));
				}
			}
		}

		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal][uTP] ERROR transient: kept client alive for retry (code=%d mappedWSA=%d rearm=%d uploadRetry=%d endpoint=%s:%u)"),
				a->error_code, Error, bRetryScheduled ? 1 : 0, bActiveLowIdUploadRetry ? 1 : 0, (LPCTSTR)ipstr(retryIP), (UINT)retryPort);
		return 0;
	}

	RemoveExpectedPeersForOwner(pSocket); // Hard failures should clear stale expected entries.
	pSocket->m_nLayerState = aborted;
	pSocket->m_bTransportFailed = true;
	pSocket->Destroy();
	pSocket->OnClose(Error);
	if (natClient) {
		natClient->ResetUtpFlowControl();
		natClient->ClearHelloAnswerPending();
		// Keep relay guard alive while retry budget is still active.
		// Clearing it here would let transient error recovery re-seed unlimited retries.
		if (!natClient->IsEServerRelayNatTGuardActive() || !natClient->HasPendingNatTRetry())
			natClient->ClearEServerRelayNatTGuard();
	}

	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NatTraversal][uTP] ERROR code=%d mappedWSA=%d"), a->error_code, Error);

	return 0;
}

// Register accept callback to avoid NULL userdata; reject unexpected sockets
static uint64 on_utp_accept(utp_callback_arguments* a)
{

	
	if (a && a->socket) {
		// Gate acceptance by NAT-T enable and resource limits
		if (!thePrefs.IsEnableNatTraversal()) {
		if (thePrefs.GetLogNatTraversalEvents())
			AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] on_utp_accept: NAT-T disabled, rejecting"));
			utp_close(a->socket);
			return 0;
		}

		// Soft resource cap on active uTP sockets
		size_t active = 0;
		g_utpSocketsLock.Lock();
		active = theApp.g_UtpSockets.size();
		g_utpSocketsLock.Unlock();

		if (active >= kMaxUtpSockets) {
			if (thePrefs.GetLogNatTraversalEvents())
			AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] on_utp_accept: too many active sockets (%u), rejecting"), (UINT)active);
			utp_close(a->socket);
			return 0;
		}



		// Try to match incoming socket against expected peers (IP:port within TTL)
		sockaddr_storage peer = {};
		socklen_t peerlen = sizeof(peer);
		DWORD now = ::GetTickCount();

		if (utp_getpeername(a->socket, (sockaddr*)&peer, &peerlen) == 0) {
			// RFC 793 SIMULTANEOUS CONNECT TIE-BREAKING
			// When both peers initiate uTP connect at the same time, exactly one side must accept the incoming 
			// connection and the other must reject it. We compare local vs remote endpoint tuples numerically:
			// - The side with the SMALLER tuple rejects the incoming SYN (keeps outgoing)
			// - The side with the LARGER tuple accepts the incoming SYN (destroys outgoing)
			// This ensures both peers agree on a single connection without deadlock.
			g_utpSocketsLock.Lock();
			for (std::set<CUtpSocket*>::iterator it = theApp.g_UtpSockets.begin(); it != theApp.g_UtpSockets.end(); ++it) {
				CUtpSocket* existing = *it;
				if (existing) {
					// Check if this existing socket is connecting to the same peer (IP+PORT match)
					sockaddr_storage existingPeer = {};
					socklen_t existingPeerLen = sizeof(existingPeer);
					if (existing->GetPeerName((sockaddr*)&existingPeer, &existingPeerLen)) {
						// Compare IP+PORT to allow multiple connections from same IP (different ports)
						if (SockaddrEqualIPAndPort((sockaddr*)&existingPeer, existingPeerLen, (sockaddr*)&peer, peerlen)) {
							const AsyncSocketExState existingState = existing->GetState();
							CUpDownClient* existingClient = existing->GetOwnerClient();
							if (existingState == connected && !existing->HasTransportFailed() && HasActiveNatTraversalTransfer(existingClient)) {
								g_utpSocketsLock.Unlock();
								if (thePrefs.GetLogNatTraversalEvents())
									DebugLog(_T("[NatTraversal][uTP] Rejecting duplicate incoming SYN for an active uTP transfer, %s"), (LPCTSTR)EscPercent(existingClient->DbgGetClientInfo()));
								utp_close(a->socket);
								return 0;
							}
							if (existingState != connecting && existingState != connected)
								continue;

							// Simultaneous connect detected.
							// Always prefer endpoint tuple compare first so both peers use the same metric.
							// This avoids asymmetric decisions when only one side knows the remote user hash.
							// Fallback to hash compare only when endpoint tuple is unavailable.
							CUpDownClient* adoptClient = existing->GetOwnerClient();
							int cmp = 0;
							bool bCmpReady = false;
							// GetPublicIPv4 returns IPv4 in network byte order.
							// MakeSockaddrIPv4 expects host byte order before it applies htonl.
							uint32 myPublicIPNet = theApp.GetPublicIPv4();
							uint32 myPublicIPHost = (myPublicIPNet != 0) ? ntohl(myPublicIPNet) : 0;
							uint16 myPort = GetBestLocalUtpPort();
							sockaddr_storage localAddr = {};
							socklen_t localAddrLen = 0;
							if (myPublicIPHost != 0 && myPort != 0 && MakeSockaddrIPv4(localAddr, localAddrLen, myPublicIPHost, myPort)) {
								cmp = CompareSockaddrIPAndPort((sockaddr*)&localAddr, localAddrLen, (sockaddr*)&peer, peerlen);
								bCmpReady = true;
								if (thePrefs.GetLogNatTraversalEvents())
									DebugLog(_T("[NatTraversal][uTP] Tie-breaking by endpoint compare (cmp=%d)"), cmp);
							}

							if (!bCmpReady) {
								const uchar* localHash = thePrefs.GetUserHash();
								const uchar* remoteHash = (adoptClient != NULL) ? adoptClient->GetUserHash() : NULL;
								if (localHash != NULL && remoteHash != NULL && !isnulmd4(localHash) && !isnulmd4(remoteHash) && !md4equ(localHash, remoteHash)) {
									cmp = memcmp(localHash, remoteHash, MDX_DIGEST_SIZE);
									bCmpReady = true;
									if (thePrefs.GetLogNatTraversalEvents())
										DebugLog(_T("[NatTraversal][uTP] Tie-breaking fallback by UserHash (cmp=%d), %s"), cmp, adoptClient ? (LPCTSTR)EscPercent(adoptClient->DbgGetClientInfo()) : (LPCTSTR)_T("no-owner"));
								}
							}

							if (!bCmpReady) {
								g_utpSocketsLock.Unlock();
								if (thePrefs.GetLogNatTraversalEvents())
									DebugLog(_T("[NatTraversal][uTP] Tie-breaking: no deterministic metric (endpoint/hash), rejecting incoming"));
								utp_close(a->socket);
								return 0;
							}

							if (cmp < 0) {
								// Local endpoint/hash is lower: keep outgoing, reject incoming.
								g_utpSocketsLock.Unlock();
								if (thePrefs.GetLogNatTraversalEvents())
									DebugLog(_T("[NatTraversal][uTP] Tie-breaking: rejecting incoming SYN (keeping outgoing)"));
								utp_close(a->socket);
								return 0;
							}

							if (cmp > 0) {
								// Local endpoint/hash is higher: adopt incoming, retire outgoing.
								if (thePrefs.GetLogNatTraversalEvents())
									DebugLog(_T("[NatTraversal][uTP] Tie-breaking: accepting incoming SYN (adopting to same owner)"));
								if (adoptClient != NULL) {
									adoptClient->ResetUtpFlowControl();
									adoptClient->RequestUtpQueuePurge();
								}
								existing->Destroy();
								existing->Setup(a->socket, true);
								RemoveExpectedPeersForOwner(existing);
								g_utpSocketsLock.Unlock();
								RefreshAcceptedUtpUploadHandshakeGuard(adoptClient, _T("simultaneous connect adoption"));
								return 0;
							}

							// Equal compare value: reject incoming to avoid dual adoption ambiguity.
							g_utpSocketsLock.Unlock();
							if (thePrefs.GetLogNatTraversalEvents())
								DebugLog(_T("[NatTraversal][uTP] Tie-breaking: equal compare value, rejecting incoming"));
							utp_close(a->socket);
							return 0;
						}
					}
				}
			}
			g_utpSocketsLock.Unlock();

			CUtpSocket* adoptOwner = NULL;

			g_expectedPeersLock.Lock();
			// Sweep expired and search match
			for (size_t i = 0; i < g_expectedPeers.size();) {
				bool expired = (int)(now - g_expectedPeers[i].expires) >= 0;
				if (expired) {
					g_expectedPeers.erase(g_expectedPeers.begin() + i);
					continue;
				}
				if (!adoptOwner && SockaddrEqual((sockaddr*)&g_expectedPeers[i].addr, g_expectedPeers[i].len, (sockaddr*)&peer, peerlen)) {
					adoptOwner = g_expectedPeers[i].owner;
					g_expectedPeers.erase(g_expectedPeers.begin() + i);
					break;
				}
				++i;
			}
			g_expectedPeersLock.Unlock();

			if (adoptOwner) {
				// Validate that adoptOwner is still alive
				bool alive = false;
				g_utpSocketsLock.Lock();
				alive = (theApp.g_UtpSockets.find(adoptOwner) != theApp.g_UtpSockets.end());
				g_utpSocketsLock.Unlock();

				if (!alive) {
					utp_close(a->socket);
					return 0;
				}

				// Adopt this accepted socket into the owning CUtpSocket

				// Close any previous socket and bind the accepted one
				CUpDownClient* adoptClient = adoptOwner->GetOwnerClient();
				if (adoptClient != NULL) {
					adoptClient->ResetUtpFlowControl();
					adoptClient->RequestUtpQueuePurge();
				}
				adoptOwner->Destroy();
				adoptOwner->Setup(a->socket, true);
				RefreshAcceptedUtpUploadHandshakeGuard(adoptClient, _T("expected peer adoption"));

				// Clear any remaining expected entries for this owner to avoid stale records
				RemoveExpectedPeersForOwner(adoptOwner);
				return 0;
			}
		}

		// No exact match -> try IP-only fallback (NAT remap tolerant)
	CUtpSocket* ipOwner = NULL;
	int ipOwnerCount = 0;
	CUtpSocket* bestOwner = NULL;
	uint16 bestExpectedPort = 0;
	int bestAbsDiff = INT_MAX;
	g_expectedPeersLock.Lock();
	for (size_t i = 0; i < g_expectedPeers.size();) {
		bool expired = (int)(now - g_expectedPeers[i].expires) >= 0;
		if (expired) { 
			g_expectedPeers.erase(g_expectedPeers.begin() + i);
			continue;
		}

		if (SockaddrEqualIPOnly((sockaddr*)&g_expectedPeers[i].addr, g_expectedPeers[i].len, (sockaddr*)&peer, peerlen)) {
			++ipOwnerCount;
			ipOwner = g_expectedPeers[i].owner; // last owner for this IP
			uint16 expPort = 0;
			if (g_expectedPeers[i].addr.ss_family == AF_INET)
				expPort = ntohs(reinterpret_cast<const sockaddr_in*>(&g_expectedPeers[i].addr)->sin_port);
			else if (g_expectedPeers[i].addr.ss_family == AF_INET6)
				expPort = ntohs(reinterpret_cast<const sockaddr_in6*>(&g_expectedPeers[i].addr)->sin6_port);
			uint16 peerPort = 0;
			if (peer.ss_family == AF_INET)
				peerPort = ntohs(reinterpret_cast<const sockaddr_in*>(&peer)->sin_port);
			else if (peer.ss_family == AF_INET6)
				peerPort = ntohs(reinterpret_cast<const sockaddr_in6*>(&peer)->sin6_port);
			int diff = (int)peerPort - (int)expPort;
			int ad = diff >= 0 ? diff : -diff;
			if (ad < bestAbsDiff) {
				bestAbsDiff = ad;
				bestOwner = ipOwner;
				bestExpectedPort = expPort;
			}
		}
		++i;
	}
	g_expectedPeersLock.Unlock();

		// Harden IP-only fallback: adopt only if there is a unique owner for this IP,
		// the owner has no active uTP socket, and the observed port is close to the expected port.
		if (bestOwner && ipOwnerCount == 1) {
			// Check if ipOwner already has an active uTP socket by probing GetPeerName
			SOCKADDR_STORAGE tmp = {};
			int tmplen = sizeof(tmp);
			if (bestOwner->GetPeerName((SOCKADDR*)&tmp, &tmplen)) {
				// Already bound to a peer, do not adopt.
			} else {
				uint16 peerPort = 0;
				if (peer.ss_family == AF_INET)
					peerPort = ntohs(reinterpret_cast<const sockaddr_in*>(&peer)->sin_port);
				else if (peer.ss_family == AF_INET6)
					peerPort = ntohs(reinterpret_cast<const sockaddr_in6*>(&peer)->sin6_port);

				uint16 expectedPort = bestExpectedPort;
				const uint16 kPortWindow = thePrefs.GetNatTraversalPortWindow(); // Accept if peer port is within configured window.
				int diff = (int)peerPort - (int)expectedPort;
				if (diff < 0)
					diff = -diff;

				if (expectedPort != 0 && diff <= (int)kPortWindow) {
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal][uTP] ACCEPT fallback by IP-only match (validated, |%u-%u|<=%u)"), (UINT)peerPort, (UINT)expectedPort, (UINT)kPortWindow);

					// Validate that the owner is still alive before adopting.
					bool alive2 = false;
					g_utpSocketsLock.Lock();
					alive2 = (theApp.g_UtpSockets.find(bestOwner) != theApp.g_UtpSockets.end());
					g_utpSocketsLock.Unlock();
					if (!alive2) {
						utp_close(a->socket);
						return 0;
					}

					CUpDownClient* adoptClient2 = bestOwner->GetOwnerClient();
					if (adoptClient2 != NULL) {
						adoptClient2->ResetUtpFlowControl();
						adoptClient2->RequestUtpQueuePurge();
					}
					bestOwner->Destroy();
					bestOwner->Setup(a->socket, true);
					RefreshAcceptedUtpUploadHandshakeGuard(adoptClient2, _T("IP fallback adoption"));

					// Remove all expected entries for this owner to avoid stale records.
					RemoveExpectedPeersForOwner(bestOwner);

					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal][uTP] ACCEPT fallback IP-only: matches=%d peerPort=%u expectedPort=%u window=%u"), ipOwnerCount, (UINT)peerPort, (UINT)expectedPort, (UINT)kPortWindow);
					return 0;
				}

				if (thePrefs.GetLogNatTraversalEvents()) {
					DebugLog(_T("[NatTraversal][uTP] ACCEPT fallback IP-only skipped: peerPort=%u expectedPort=%u window=%u"),
						(UINT)peerPort, (UINT)expectedPort, (UINT)kPortWindow);
				}
			}
		} else if (bestOwner && ipOwnerCount > 1 && thePrefs.GetLogNatTraversalEvents()) {
			DebugLog(_T("[NatTraversal][uTP] ACCEPT fallback IP-only skipped: ambiguous owners=%d"), ipOwnerCount);
		}
		// Final fallback - check m_aNatExpectations (CClientUDPSocket)
		// This handles the "passive uploader" case where:
		// - Instance5 received OP_ESERVER_PEER_INFO and registered NAT expectation
		// - Instance5 did NOT call TryToConnect (passive mode)
		// - Therefore no CUtpSocket exists and g_expectedPeers is empty
		// - But m_aNatExpectations has the client registered
		// We need to create a new socket wrapper and adopt the incoming connection.
		if (theApp.clientudp != NULL) {
			CAddress peerIP;
			uint16 peerPort = 0;
			peerIP.FromSA((sockaddr*)&peer, peerlen, &peerPort);

			CUpDownClient* natClient = theApp.clientudp->MatchNatExpectation(peerIP, peerPort);
			if (natClient != NULL) {
				// Found client via NAT expectation - create socket wrapper for it
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal][uTP] ACCEPT via NatExpectation fallback: found client %s"), (LPCTSTR)EscPercent(natClient->DbgGetClientInfo()));

				// Never mix a delayed uTP accept into an existing QUIC transport.
				if (natClient->socket != NULL && natClient->socket->HaveQuicNatLayer()) {
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal][uTP] ACCEPT via NatExpectation rejected: QUIC layer already exists for %s"), (LPCTSTR)EscPercent(natClient->DbgGetClientInfo()));
					utp_close(a->socket);
					return 0;
				}

				CUtpSocket* currentUtpLayer = natClient->socket != NULL ? natClient->socket->GetUtpLayer() : NULL;
				if (currentUtpLayer != NULL && !currentUtpLayer->HasTransportFailed()
					&& currentUtpLayer->GetState() == connected && HasActiveNatTraversalTransfer(natClient)) {
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal][uTP] ACCEPT via NatExpectation rejected: established uTP transport already exists for %s"), (LPCTSTR)EscPercent(natClient->DbgGetClientInfo()));
					utp_close(a->socket);
					return 0;
				}

				// Client needs a socket to own the uTP layer
				if (natClient->socket == NULL) {
					// Create new CEMSocket that will own the uTP layer
					CClientReqSocket* pNewSocket = new CClientReqSocket(natClient);
					natClient->socket = pNewSocket;
				}

				// Get or create uTP layer on the socket
				CUtpSocket* utpLayer = natClient->socket->GetUtpLayer();
				if (utpLayer == NULL) {
					// Add uTP layer to socket
					utpLayer = natClient->socket->InitUtpSupport();
				}

				if (utpLayer != NULL) {
					// Adopt the incoming connection to this socket layer
					natClient->ResetUtpFlowControl();
					natClient->RequestUtpQueuePurge();
					utpLayer->Setup(a->socket, true);
					RefreshAcceptedUtpUploadHandshakeGuard(natClient, _T("NAT expectation adoption"));
					RemoveExpectedPeersForOwner(utpLayer);
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal][uTP] ACCEPT via NatExpectation: adopted incoming connection for %s"), (LPCTSTR)EscPercent(natClient->DbgGetClientInfo()));
					return 0; // success - don't close
				} else {
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal][uTP] ACCEPT via NatExpectation: failed to create uTP layer for %s"), (LPCTSTR)EscPercent(natClient->DbgGetClientInfo()));
				}
			}
		}
		// No match at all -> reject to avoid NULL userdata deref
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal][uTP] ACCEPT rejected: no matching peer in g_expectedPeers or NatExpectations"));
		utp_close(a->socket);
	}
	return 0;
}

uint64 CUtpSocket::on_utp_get_read_buffer_size(utp_callback_arguments* a)
{
	CUtpSocket* pSocket = (CUtpSocket*)utp_get_userdata(a->socket);
	// Report available free space, not the used size.
	size_t used = 0;
	size_t cap = 0;
	
	if (pSocket && pSocket->m_Socket == a->socket) {
		CSingleLock readLock(&pSocket->m_csReadBuffer, TRUE);
		used = pSocket->m_ReadBuffer.GetSize();
		cap = pSocket->m_ReadBuffer.GetLength();
	}

	size_t freeSpace = (cap > used) ? (cap - used) : 0;
	return (uint64)freeSpace;
}

static uint64 GetUtpUdpOverheadForFamily(int family)
{
	// IP + UDP headers + app framing (prot+opcode) + max obfuscation framing.
	const uint64 kAppFrameOverhead = 2;
	const uint64 kAppObfOverhead = 16;
	if (family == AF_INET6)
		return 48 + kAppFrameOverhead + kAppObfOverhead;
	return 28 + kAppFrameOverhead + kAppObfOverhead;
}

static uint64 GetUtpAppOverhead()
{
	return 2 + 16;
}

static uint64 GetUtpConservativeUdpPayloadCeiling(int family)
{
	const uint64 kUtpPayloadV4 = 1402;
	const uint64 kUtpPayloadV6 = 1232;
	const uint64 kSafePayloadV4 = 1200;
	const uint64 utpPayload = (family == AF_INET6) ? kUtpPayloadV6 : kUtpPayloadV4;
	if (family == AF_INET6)
		return utpPayload;

	const uint64 kadPayload = UDP_KAD_MAXFRAGMENT;
	uint64 safePayload = (kadPayload < utpPayload) ? kadPayload : utpPayload;
	if (kSafePayloadV4 < safePayload)
		safePayload = kSafePayloadV4;
	return safePayload;
}

static uint64 GetUtpIpUdpOverheadForFamily(int family)
{
	return (family == AF_INET6) ? 48 : 28;
}

// Additional uTP callbacks for better behavior
static uint64 on_utp_get_udp_mtu(utp_callback_arguments* a)
{
	// libutp expects UDP payload MTU (no IP/UDP/app framing). Return a safe payload size.
	uint32 baseMtu = 0;
	int family = AF_INET;
	if (a && a->socket) {
		sockaddr_storage sa = {};
		socklen_t sl = sizeof(sa);
		if (utp_getpeername(a->socket, (sockaddr*)&sa, &sl) == 0) {
			family = (sa.ss_family == AF_INET6) ? AF_INET6 : AF_INET;
			if (!QueryMtuForPeer((sockaddr*)&sa, sl, baseMtu) || baseMtu < 576)
				baseMtu = 0;
		}
	}
	if (baseMtu == 0) {
		family = theApp.GetPublicIPv6().IsNull() ? AF_INET : AF_INET6;
		baseMtu = (family == AF_INET6) ? 1280 : 1500;
	}

	const uint64 ipUdpOverhead = GetUtpIpUdpOverheadForFamily(family);
	const uint64 appOverhead = GetUtpAppOverhead();

	uint64 basePayload = 0;
	if (baseMtu > ipUdpOverhead)
		basePayload = (uint64)baseMtu - ipUdpOverhead;

	uint64 safePayload = GetUtpConservativeUdpPayloadCeiling(family);
	if (basePayload > 0 && basePayload < safePayload)
		safePayload = basePayload;

	uint64 adjusted = (safePayload > appOverhead) ? (safePayload - appOverhead) : safePayload;

	const uint64 minBase = (family == AF_INET6) ? 1280 : 576;
	const uint64 minPayload = (minBase > ipUdpOverhead) ? (minBase - ipUdpOverhead) : minBase;
	const uint64 minAdjusted = (minPayload > appOverhead) ? (minPayload - appOverhead) : minPayload;
	if (adjusted < minAdjusted)
		adjusted = minAdjusted;
	return adjusted;
}

// Return UDP/IP overhead based on peer address family if available
static uint64 on_utp_get_udp_overhead(utp_callback_arguments* a)
{
	int family = AF_INET;
	if (a && a->socket) {
		sockaddr_storage sa = {};
		socklen_t sl = sizeof(sa);
		if (utp_getpeername(a->socket, (sockaddr*)&sa, &sl) == 0)
			family = (sa.ss_family == AF_INET6) ? AF_INET6 : AF_INET;
	} else if (!theApp.GetPublicIPv6().IsNull()) {
		family = AF_INET6;
	}

	return GetUtpUdpOverheadForFamily(family);
}

static uint64 on_utp_get_milliseconds(utp_callback_arguments* /*a*/) { return (uint64)::GetTickCount(); }
static uint64 on_utp_get_microseconds(utp_callback_arguments* /*a*/)
{
	if (g_qpcFreq.QuadPart == 0) {
		QueryPerformanceFrequency(&g_qpcFreq);
		if (g_qpcFreq.QuadPart == 0)
			return on_utp_get_milliseconds(NULL) * 1000ULL;
	}

	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return (uint64)((now.QuadPart * 1000000ULL) / g_qpcFreq.QuadPart);
}

static uint64 on_utp_get_random(utp_callback_arguments* /*a*/)
{
	if (g_randState == 0ULL) {
		LARGE_INTEGER seed;
		QueryPerformanceCounter(&seed);
		g_randState = (unsigned long long)seed.QuadPart;
	}

	g_randState = g_randState * 6364136223846793005ULL + 1ULL;
	return (uint64)((g_randState >> 33) & 0x7FFFFFFF);
}

static uint64 on_utp_log(utp_callback_arguments* a)
{
	if (thePrefs.GetLogNatTraversalEvents()) {
		CStringA msg;
		if (a && a->buf && a->len)
			msg.Append((LPCSTR)a->buf, (int)a->len);

		DebugLog(_T("[uTP] %hs"), (LPCSTR)msg);
	}

	return 0;
}
static uint64 on_utp_delay_sample(utp_callback_arguments* /*a*/) { return 0; }
static uint64 on_utp_overhead_stats(utp_callback_arguments* /*a*/) { return 0; }

CUtpSocket::CUtpSocket()
{
	if (thePrefs.GetLogNatTraversalEvents())
		AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] *** CUtpSocket::CUtpSocket() this=%p ***"), this);
	m_Socket = NULL;
	m_Context = NULL;
	m_ShutDown = 0;
	
	// Adaptive buffer strategy: Start with small buffers for handshake, grow dynamically during data transfer
	// Initial 64KB write buffer keeps memory low during connection establishment
	// Grow the application buffer to 512KB/2MB/4MB only after data-path pressure.
	m_ReadBuffer.AllocBuffer(64 * 1024); // Note: UTP can push more data into the buffer than expected
	m_WriteBuffer.AllocBuffer(kUtpInitialAppWriteBufferSize);
	m_uWriteBufferBlockCountAtCapacity = 0;
	m_dwLastSendTime = 0;
	m_bBufferGrown = false;
	
	m_bConnectNotified = false;
	m_bTransportFailed = false;
	m_pOwnerClient = NULL;
	m_uZeroWriteBurst = 0;
	m_bAppSendBlocked = false;
	m_bUtpWriteBlocked = false;
	m_bUtpWritableHint = false;
	m_dwNextUtpWriteAttemptTick = 0;
	m_nLayerState = unconnected; // uTP doesn't need socket creation, ready for Connect
    g_utpSocketsLock.Lock();
    theApp.g_UtpSockets.insert(this);
    g_utpSocketsLock.Unlock();
	if (thePrefs.GetLogNatTraversalEvents())
		AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] CUtpSocket::CUtpSocket() EXIT, state=%d"), (int)m_nLayerState);
}

CUtpSocket::~CUtpSocket()
{
	{
		CSingleLock runtimeLock(&g_utpRuntimeLock, TRUE);
		RemoveExpectedPeersForOwner(this); // Ensure no stale expected entries remain for this owner
	}
	Destroy();
	g_utpSocketsLock.Lock();
	theApp.g_UtpSockets.erase(this);
	g_utpSocketsLock.Unlock();
}

CCriticalSection& CUtpSocket::GetRuntimeLock()
{
	return g_utpRuntimeLock;
}

void CUtpSocket::OnReceive(int nErrorCode)
{
	// For uTP sockets, bypass the FD_READ check in CAsyncSocketExLayer::OnReceive
	// CAsyncSocketExLayer::OnReceive checks m_lEvent & FD_READ which is not set for uTP (UDP-based)
	// Since all OnReceive methods in the layer chain are protected, we directly call the owner socket's OnReceive
	// The owner socket (CEMSocket) has a public OnReceive method that will process the received data
	if (m_pOwnerSocket)
		m_pOwnerSocket->OnReceive(nErrorCode);
}

void CUtpSocket::UnregisterFromGlobalSet()
{
    g_utpSocketsLock.Lock();
    std::set<CUtpSocket*>::iterator it = theApp.g_UtpSockets.find(this);
    if (it != theApp.g_UtpSockets.end())
        theApp.g_UtpSockets.erase(it);
    g_utpSocketsLock.Unlock();
}

bool CUtpSocket::IsDuplexTransferLikely() const
{
	CUpDownClient* pClient = m_pOwnerClient;
	if (pClient == NULL || theApp.clientlist == NULL || !theApp.clientlist->IsValidClient(pClient))
		return false;

	const EUploadState eUploadState = pClient->GetUploadState();
	const bool bHasUploadContext = eUploadState == US_UPLOADING || eUploadState == US_CONNECTING;
	const EDownloadState eDownloadState = pClient->GetDownloadState();
	const bool bHasDownloadContext = pClient->GetRequestFile() != NULL
		&& eDownloadState != DS_NONE
		&& eDownloadState != DS_ERROR
		&& eDownloadState != DS_BANNED;

	return bHasUploadContext && bHasDownloadContext;
}

size_t CUtpSocket::GetMaxWriteBufferCapacity() const
{
	return IsDuplexTransferLikely() ? kUtpDuplexMaxAdaptiveAppWriteBufferSize : kUtpMaxAdaptiveAppWriteBufferSize;
}

bool CUtpSocket::GrowWriteBufferTo(size_t uNewCapacity, LPCTSTR pszReason)
{
	UNREFERENCED_PARAMETER(pszReason);
	const size_t uCurrentCapacity = m_WriteBuffer.GetLength();
	const size_t uCurrentSize = m_WriteBuffer.GetSize();
	const size_t uMaxCapacity = GetMaxWriteBufferCapacity();
	if (uNewCapacity > uMaxCapacity)
		uNewCapacity = uMaxCapacity;
	if (uNewCapacity <= uCurrentCapacity)
		return true;
	if (uCurrentSize >= uNewCapacity)
		return false;

	bool bResult = false;
	if (uCurrentSize == 0) {
		m_WriteBuffer.AllocBuffer(uNewCapacity);
		bResult = true;
	} else {
		const size_t uGrowSize = uCurrentSize + 1;
		const size_t uPreAlloc = uNewCapacity - uGrowSize;
		bResult = m_WriteBuffer.SetSize(uGrowSize, true, uPreAlloc) && m_WriteBuffer.SetSize(uCurrentSize, false);
	}
	if (!bResult)
		return false;

	m_bBufferGrown = uNewCapacity > kUtpInitialAppWriteBufferSize;
	m_uWriteBufferBlockCountAtCapacity = 0;
	return true;
}

bool CUtpSocket::ShrinkWriteBufferTo(size_t uNewCapacity, LPCTSTR pszReason)
{
	UNREFERENCED_PARAMETER(pszReason);
	const size_t uCurrentCapacity = m_WriteBuffer.GetLength();
	const size_t uCurrentSize = m_WriteBuffer.GetSize();
	if (uNewCapacity >= uCurrentCapacity)
		return true;
	if (uCurrentSize > uNewCapacity)
		return false;

	std::vector<byte> oldData;
	if (uCurrentSize != 0) {
		oldData.resize(uCurrentSize);
		memcpy(&oldData[0], m_WriteBuffer.GetBuffer(), uCurrentSize);
	}
	m_WriteBuffer.AllocBuffer(uNewCapacity);
	if (!oldData.empty())
		m_WriteBuffer.AppendData(&oldData[0], oldData.size());

	m_bBufferGrown = uNewCapacity > kUtpInitialAppWriteBufferSize;
	m_uWriteBufferBlockCountAtCapacity = 0;
	return true;
}

void CUtpSocket::TrimWriteBufferForDuplexIfPossible(LPCTSTR pszReason)
{
	if (IsDuplexTransferLikely() && m_WriteBuffer.GetLength() > kUtpDuplexMaxAdaptiveAppWriteBufferSize && m_WriteBuffer.GetSize() <= kUtpDuplexMaxAdaptiveAppWriteBufferSize)
		ShrinkWriteBufferTo(kUtpDuplexMaxAdaptiveAppWriteBufferSize, pszReason);
}

size_t CUtpSocket::GetNextWriteBufferCapacity() const
{
	const size_t uCurrentCapacity = m_WriteBuffer.GetLength();
	const size_t uMaxCapacity = GetMaxWriteBufferCapacity();
	if (uCurrentCapacity >= uMaxCapacity)
		return uCurrentCapacity;
	if (uCurrentCapacity < kUtpFirstAdaptiveAppWriteBufferSize)
		return min(kUtpFirstAdaptiveAppWriteBufferSize, uMaxCapacity);
	if (uCurrentCapacity < kUtpSecondAdaptiveAppWriteBufferSize)
		return min(kUtpSecondAdaptiveAppWriteBufferSize, uMaxCapacity);
	return min(kUtpMaxAdaptiveAppWriteBufferSize, uMaxCapacity);
}

void CUtpSocket::OnWriteBufferBlocked()
{
	TrimWriteBufferForDuplexIfPossible(_T("duplex-block"));
	if (m_WriteBuffer.GetLength() >= GetMaxWriteBufferCapacity())
		return;

	const bool bFirstAdaptivePromotion = m_WriteBuffer.GetLength() < kUtpFirstAdaptiveAppWriteBufferSize;
	if (!bFirstAdaptivePromotion && ++m_uWriteBufferBlockCountAtCapacity < kUtpWriteBufferPromotionBlocks)
		return;

	const size_t uRequestedWriteBufferSize = GetNextWriteBufferCapacity();
	GrowWriteBufferTo(uRequestedWriteBufferSize, bFirstAdaptivePromotion ? _T("first-block") : _T("repeated-block"));
}

void CUtpSocket::Process()
{
	CSingleLock runtimeLock(&g_utpRuntimeLock, TRUE);
	// Take a snapshot to minimize lock hold time during writes
	std::vector<CUtpSocket*> sockets;
	g_utpSocketsLock.Lock();
	sockets.reserve(theApp.g_UtpSockets.size());
	for (std::set<CUtpSocket*>::iterator it = theApp.g_UtpSockets.begin(); it != theApp.g_UtpSockets.end(); ++it)
		sockets.push_back(*it);
	g_utpSocketsLock.Unlock();

	for (size_t i = 0; i < sockets.size(); ++i) {
		CUtpSocket* cursocket = sockets[i];
		if (!cursocket)
			continue;

		if (cursocket->m_ShutDown & 0x02)
			continue;

		UINT uWriteAttemptsThisPass = 0;
		size_t uWriteBytesThisPass = 0;
		std::vector<byte> writeSnapshot;
		while (uWriteAttemptsThisPass < kUtpMaxWriteAttemptsPerProcess && uWriteBytesThisPass < kUtpMaxWriteBytesPerProcess) {
			writeSnapshot.clear();
			bool bSkipWriteAttempt = false;
			const DWORD nowTick = ::GetTickCount();
			{
				CSingleLock writeLock(&cursocket->m_csWriteBuffer, TRUE);
				if (cursocket->m_Socket && cursocket->m_WriteBuffer.GetSize() != 0) {
					if (cursocket->m_bUtpWriteBlocked && !cursocket->m_bUtpWritableHint && cursocket->m_dwNextUtpWriteAttemptTick != 0
						&& (int)(nowTick - cursocket->m_dwNextUtpWriteAttemptTick) < 0) {
						bSkipWriteAttempt = true;
					} else {
						cursocket->m_bUtpWritableHint = false;
						const size_t uRemainingBudget = kUtpMaxWriteBytesPerProcess - uWriteBytesThisPass;
						const size_t uSnapshotSize = min(min(cursocket->m_WriteBuffer.GetSize(), kUtpMaxWriteAttemptSize), uRemainingBudget);
						writeSnapshot.resize(uSnapshotSize);
						memcpy(&writeSnapshot[0], cursocket->m_WriteBuffer.GetBuffer(), writeSnapshot.size());
					}
				}
			}

			if (bSkipWriteAttempt || writeSnapshot.empty())
				break;

			++uWriteAttemptsThisPass;
			// Drain only within the per-socket attempt and byte budget.
			const ssize_t written = utp_write(cursocket->m_Socket, &writeSnapshot[0], writeSnapshot.size());

			if (written > 0) {
				bool bWakeOwnerSend = false;
				UINT uRemaining = 0;
				{
					CSingleLock writeLock(&cursocket->m_csWriteBuffer, TRUE);
					cursocket->m_WriteBuffer.ShiftData((size_t)written);
					cursocket->m_uZeroWriteBurst = 0;
					const size_t uUsableCapacity = min(cursocket->m_WriteBuffer.GetLength(), cursocket->GetMaxWriteBufferCapacity());
					if (cursocket->m_bAppSendBlocked && cursocket->m_WriteBuffer.GetSize() < uUsableCapacity) {
						cursocket->m_bAppSendBlocked = false;
						bWakeOwnerSend = true;
					}
					cursocket->m_bUtpWriteBlocked = false;
					cursocket->m_bUtpWritableHint = false;
					cursocket->m_dwNextUtpWriteAttemptTick = 0;
					uRemaining = (UINT)cursocket->m_WriteBuffer.GetSize();
					if (uRemaining == 0)
						cursocket->TrimWriteBufferForDuplexIfPossible(_T("duplex-drained"));
				}
				uWriteBytesThisPass += (size_t)written;


				if (cursocket->m_Context != NULL) {
					utp_issue_deferred_acks(cursocket->m_Context);
					utp_check_timeouts(cursocket->m_Context);
				}

				// Release the upper socket only when application buffer capacity reopened.
				if (bWakeOwnerSend && cursocket->m_nLayerState == connected)
					cursocket->OnSend(0);
				if (uRemaining == 0)
					break;
				continue;
			}

			if (cursocket->m_Context != NULL) {
				utp_issue_deferred_acks(cursocket->m_Context);
				utp_check_timeouts(cursocket->m_Context);
			}

			if (written == 0) {
				CSingleLock writeLock(&cursocket->m_csWriteBuffer, TRUE);
				if (cursocket->m_uZeroWriteBurst < 0xFF)
					++cursocket->m_uZeroWriteBurst;
				cursocket->m_bUtpWriteBlocked = true;
				cursocket->m_bUtpWritableHint = false;
				DWORD retryDelay = kUtpZeroWriteRetryBaseMs + ((DWORD)cursocket->m_uZeroWriteBurst * kUtpZeroWriteRetryStepMs);
				if (retryDelay > kUtpZeroWriteRetryMaxMs)
					retryDelay = kUtpZeroWriteRetryMaxMs;
				cursocket->m_dwNextUtpWriteAttemptTick = nowTick + retryDelay;
			} else {
				UINT uRemain = 0;
				{
					CSingleLock writeLock(&cursocket->m_csWriteBuffer, TRUE);
					cursocket->m_uZeroWriteBurst = 0;
					cursocket->m_bUtpWriteBlocked = false;
					cursocket->m_bUtpWritableHint = false;
					cursocket->m_dwNextUtpWriteAttemptTick = 0;
					uRemain = (UINT)cursocket->m_WriteBuffer.GetSize();
				}
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLogError(_T("[NatTraversal][uTP] WRITE failed (err=%d remain=%u)"), (int)written, uRemain);
			}
			break;
		}

		// Adaptive application buffer shrinking: If buffer has been grown but is now idle for 30s, shrink back to save memory
		// NOTE: Only application buffer shrinks. libutp buffer stays at 256KB (required for protocol reliability).
		{
			CSingleLock writeLock(&cursocket->m_csWriteBuffer, TRUE);
			if (cursocket->m_bBufferGrown &&
				cursocket->m_WriteBuffer.GetSize() == 0 &&
				cursocket->m_dwLastSendTime > 0 &&
				(GetTickCount() - cursocket->m_dwLastSendTime) > kUtpAppWriteBufferIdleShrinkMs) {

				const size_t newCapacity = kUtpInitialAppWriteBufferSize;

				// Shrink application buffer back to 64KB
				cursocket->m_WriteBuffer.AllocBuffer(newCapacity);
				cursocket->m_uWriteBufferBlockCountAtCapacity = 0;
				cursocket->m_bBufferGrown = false;
				cursocket->m_bUtpWriteBlocked = false;
				cursocket->m_bUtpWritableHint = false;
				cursocket->m_dwNextUtpWriteAttemptTick = 0;
				cursocket->m_dwLastSendTime = 0; // Reset timer

			}
		}

		// eMule AI: timers are serviced centrally by CClientUDPSocket::ServiceUtp
	}
}

bool CUtpSocket::ProcessUtpPacket(const byte* data, size_t len, const struct sockaddr* from, socklen_t fromlen)
{
	CSingleLock runtimeLock(&g_utpRuntimeLock, TRUE);
  // Process a UDP packet from the network. This will process a packet for an existing connection, or create
  // a new connection. Returns 1 if the packet was processed in some way, 0 if the packet did not appear to be uTP.
  utp_context* ctx = m_Context ? m_Context : (theApp.clientudp ? theApp.clientudp->GetUtpContext() : NULL);
  if (!ctx)
  	return false; // no context available: ignore

 
  CUtpSocket::EnsureCallbacks(ctx); // Ensure callbacks are configured even if this is the first inbound packet
  return utp_process_udp(ctx, data, len, from, fromlen);
}

void CUtpSocket::ExpectPeer(const struct sockaddr* to, socklen_t tolen)
{
	CSingleLock runtimeLock(&g_utpRuntimeLock, TRUE);
	// Ensure context exists to keep semantics similar to Connect
	if (!m_Context && theApp.clientudp)
		m_Context = theApp.clientudp->GetUtpContext();
    if (!m_Context)
        return;
    AddExpectedPeer(this, to, tolen);
}

BOOL CUtpSocket::Create(UINT /*nSocketPort*/, int /*nSocketType*/, long lEvent, LPCSTR /*lpszSocketAddress*/)
{
	if (theApp.IsNetworkActivityBlockedByBind())
		return FALSE;

	m_pOwnerSocket->AsyncSelect(lEvent);
	return TRUE;
}

BOOL CUtpSocket::Connect(const LPSOCKADDR lpSockAddr, int nSockAddrLen)
{
	if (theApp.IsNetworkActivityBlockedByBind())
		return FALSE;

	CSingleLock runtimeLock(&g_utpRuntimeLock, TRUE);
	if (thePrefs.GetLogNatTraversalEvents()) {
		AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] *** CUtpSocket::Connect CALLED! this=%p state=%d ***"), this, (int)m_nLayerState);
		AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] CUtpSocket::Connect: m_Socket=%p m_Context=%p"), m_Socket, m_Context);
	}
	
	// Accept both notsock (initial state after creation) and unconnected states
	if (m_nLayerState == notsock || m_nLayerState == unconnected) {
		if (thePrefs.GetLogNatTraversalEvents())
			AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] CUtpSocket::Connect: state OK, getting context..."));
		// Ensure context exists before creating the socket
		if (!m_Context && theApp.clientudp)
			m_Context = theApp.clientudp->GetUtpContext();
		if (!m_Context) {
			if (thePrefs.GetLogNatTraversalEvents())
				AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] CUtpSocket::Connect: NO CONTEXT! returning FALSE"));
			return FALSE; // no context available
		}

		if (thePrefs.GetLogNatTraversalEvents())
			AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] CUtpSocket::Connect: Context OK, adding expected peer..."));
		// Track expected peer for conditional accept (IP:port)
		AddExpectedPeer(this, (const sockaddr*)lpSockAddr, (socklen_t)nSockAddrLen);

		if (thePrefs.GetLogNatTraversalEvents())
			AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] CUtpSocket::Connect: Creating utp socket..."));
		int ret;
		utp_socket* socket = utp_create_socket(m_Context);
		if (!socket)
		{
			if (thePrefs.GetLogNatTraversalEvents())
				AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] CUtpSocket::Connect: utp_create_socket FAILED!"));
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLogError(_T("[NatTraversal][uTP] CONNECT create failed"));
			AddDebugLogLine(DLP_LOW, false, _T("[NAT-T][uTP] Connect create failed"));
			return FALSE;
		}
		if (thePrefs.GetLogNatTraversalEvents())
			AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] CUtpSocket::Connect: Socket created, calling Setup..."));
		m_nLayerState = connecting;
		Setup(socket);

		if (thePrefs.GetLogNatTraversalEvents())
			AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] CUtpSocket::Connect: Setup done, calling utp_connect..."));
		CAddress dest;
		uint16 destPort = 0;
		dest.FromSA(lpSockAddr, nSockAddrLen, &destPort);
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal][uTP] CONNECT attempt -> %s:%u"), (LPCTSTR)ipstr(dest), (UINT)destPort);


		ret = utp_connect(socket, (const sockaddr*)lpSockAddr, (socklen_t)nSockAddrLen);
		if (thePrefs.GetLogNatTraversalEvents())
			AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] CUtpSocket::Connect: utp_connect returned %d"), ret);
		if (ret == 0) {
			if (thePrefs.GetLogNatTraversalEvents())
				AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] CUtpSocket::Connect: SUCCESS!"));
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal][uTP] CONNECT started -> %s:%u"), (LPCTSTR)ipstr(dest), (UINT)destPort);
			return TRUE;
		}
		DWORD wsaErr = ::WSAGetLastError();
		if (thePrefs.GetLogNatTraversalEvents())
			AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] CUtpSocket::Connect: FAILED! ret=%d WSAError=%lu"), ret, wsaErr);
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLogError(_T("[NatTraversal][uTP] CONNECT failed ret=%d err=%lu -> %s:%u"), ret, wsaErr, (LPCTSTR)ipstr(dest), (UINT)destPort);

	} else {
		if (thePrefs.GetLogNatTraversalEvents())
			AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] CUtpSocket::Connect REJECTED: state=%d (not unconnected)"), (int)m_nLayerState);
	}

	return FALSE;
}

void CUtpSocket::Setup(utp_socket* Socket, bool bAcceptedSocket)
{
	CSingleLock runtimeLock(&g_utpRuntimeLock, TRUE);
	if (thePrefs.GetLogNatTraversalEvents())
		AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] CUtpSocket::Setup ENTRY, Socket=%p bAccepted=%d"), Socket, (int)bAcceptedSocket);
	if (!Socket)
		return;

	// Retire a previous handle before adoption.
	if (m_Socket != NULL && m_Socket != Socket)
		Destroy();
	ASSERT(m_Socket == NULL);
	m_Socket = Socket;
	m_bConnectNotified = bAcceptedSocket;
	m_bTransportFailed = false;
	m_pOwnerClient = NULL;
	CClientReqSocket* ownerSocket = dynamic_cast<CClientReqSocket*>(m_pOwnerSocket);
	if (ownerSocket) {
		m_pOwnerClient = ownerSocket->client;
		const EStreamCryptState cryptState = ownerSocket->GetStreamCryptState();
		if (cryptState == ECS_UNKNOWN || cryptState == ECS_NONE) {
			ownerSocket->SetConnectionEncryption(false, NULL, false); // Disable TCP obfuscation for uTP transport
		} else {
			ownerSocket->ResetStreamCryptLayer();
			ownerSocket->SetConnectionEncryption(false, NULL, false);
			if (thePrefs.GetLogNatTraversalEvents() && m_pOwnerClient != NULL) {
				DebugLog(_T("[NatTraversal][uTP][Setup] Reset encryption state during adoption (state=%u), %s"),
					(unsigned)cryptState, (LPCTSTR)EscPercent(m_pOwnerClient->DbgGetClientInfo()));
			}
		}
		
		// NOTE: SecureIdent state reset moved to ConnectionEstablished() 
		// Resetting here (before Hello exchange) was too early and ineffective
		// See BaseClient.cpp ConnectionEstablished() for proper placement
	}

	// libutp buffer strategy: 256 KB proven necessary for reliable handshake
	// 16KB/128KB cause layerState=4 (congestion) during Hello exchange, blocking connection setup
	// 256 KB allows libutp to accept initial control packets (Hello/HelloAnswer) without congestion
	// Application buffer (m_WriteBuffer) uses adaptive growth independently.
	// This separation allows: small app buffer for memory efficiency + large libutp buffer for protocol reliability
	const int initialSndbuf = 256 * 1024; // 256 KB libutp send buffer (protocol requirement)
	const int initialRcvbuf = 256 * 1024; // 256 KB libutp receive buffer
	utp_setsockopt(m_Socket, UTP_SNDBUF, initialSndbuf);
	utp_setsockopt(m_Socket, UTP_RCVBUF, initialRcvbuf);

	m_ShutDown = 0;
	{
		CSingleLock writeLock(&m_csWriteBuffer, TRUE);
		m_bUtpWriteBlocked = false;
		m_bUtpWritableHint = false;
		m_dwNextUtpWriteAttemptTick = 0;
			}

	// Use single global UDP context managed by CClientUDPSocket
	m_Context = theApp.clientudp ? theApp.clientudp->GetUtpContext() : NULL;
	utp_set_userdata(m_Socket, this);
    CUtpSocket::EnsureCallbacks(m_Context);

    // Track active uTP endpoints for accept adoption checks
    g_utpSocketsLock.Lock();
    theApp.g_UtpSockets.insert(this);
    g_utpSocketsLock.Unlock();

	// IMPORTANT: When adopting an accepted socket from UTP_ON_ACCEPT, the socket is already connected.
	// libutp does not re-trigger UTP_STATE_CONNECT after utp_set_userdata, so we must manually trigger OnConnect.
	// For outgoing connections, libutp will call the state change callback when connect succeeds.
	if (bAcceptedSocket) {
		if (thePrefs.GetLogNatTraversalEvents())
			AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] CUtpSocket::Setup: Accepted socket, triggering OnConnect"));
		m_nLayerState = connected;
		OnConnect(0);
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal][uTP][Setup] Adopted accepted socket, triggering OnConnect manually"));
	} else {
		if (thePrefs.GetLogNatTraversalEvents())
			AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] CUtpSocket::Setup: Outgoing connection, waiting for callback"));
	}
}

void CUtpSocket::Destroy()
{
	CSingleLock runtimeLock(&g_utpRuntimeLock, TRUE);
	if (!m_Socket)
		return;

	// Detach userdata before closing to avoid callbacks dereferencing deleted sockets
	utp_set_userdata(m_Socket, NULL);

	utp_close(m_Socket);
    // Do not destroy shared context here; it is owned by CClientUDPSocket
    m_Socket = NULL;
    m_pOwnerClient = NULL;

    // Remove from active set
    g_utpSocketsLock.Lock();
    std::set<CUtpSocket*>::iterator it = theApp.g_UtpSockets.find(this);
    if (it != theApp.g_UtpSockets.end())
        theApp.g_UtpSockets.erase(it);
    g_utpSocketsLock.Unlock();
}

// Configure libutp callbacks once per context. Safe to call repeatedly.
void CUtpSocket::EnsureCallbacks(utp_context* ctx)
{
    if (!ctx)
        return;

    // Configure callbacks once per context
    g_utpCtxLock.Lock();
    if (g_utpConfiguredContexts.find(ctx) == g_utpConfiguredContexts.end()) {
        utp_set_callback(ctx, UTP_ON_STATE_CHANGE, &on_utp_state_change);
        utp_set_callback(ctx, UTP_ON_READ, &on_utp_read);
        utp_set_callback(ctx, UTP_SENDTO, &on_utp_sendto);
        utp_set_callback(ctx, UTP_ON_ERROR, &on_utp_error);
        utp_set_callback(ctx, UTP_ON_ACCEPT, &on_utp_accept);
        utp_set_callback(ctx, UTP_GET_READ_BUFFER_SIZE, &on_utp_get_read_buffer_size);
        utp_set_callback(ctx, UTP_GET_UDP_MTU, &on_utp_get_udp_mtu);
        utp_set_callback(ctx, UTP_GET_UDP_OVERHEAD, &on_utp_get_udp_overhead);
        utp_set_callback(ctx, UTP_GET_MILLISECONDS, &on_utp_get_milliseconds);
        utp_set_callback(ctx, UTP_GET_MICROSECONDS, &on_utp_get_microseconds);
        utp_set_callback(ctx, UTP_GET_RANDOM, &on_utp_get_random);
        utp_set_callback(ctx, UTP_LOG, &on_utp_log);
        utp_set_callback(ctx, UTP_ON_DELAY_SAMPLE, &on_utp_delay_sample);
        utp_set_callback(ctx, UTP_ON_OVERHEAD_STATISTICS, &on_utp_overhead_stats);
        g_utpConfiguredContexts.insert(ctx);
    }
    g_utpCtxLock.Unlock();
}

// Clear configured-context tracker if the given context is being destroyed.
void CUtpSocket::OnContextDestroyed(utp_context* ctx)
{
    if (!ctx)
        return;
    g_utpCtxLock.Lock();
    std::set<utp_context*>::iterator it = g_utpConfiguredContexts.find(ctx);

    if (it != g_utpConfiguredContexts.end())
        g_utpConfiguredContexts.erase(it);

    g_utpCtxLock.Unlock();
}

void CUtpSocket::Close()
{
	if (m_nLayerState == connected)	{
		m_nLayerState = closed;
		Destroy();
		OnClose(0);
	}
}

int CUtpSocket::Receive(void* lpBuf, int nBufLen, int /*nFlags*/)
{
	if (m_nLayerState != connected) {
		WSASetLastError(WSAENOTCONN);
		return SOCKET_ERROR;
	}

	CSingleLock readLock(&m_csReadBuffer, TRUE);
	if (m_ReadBuffer.GetSize() == 0) { // buffer is empty
		WSASetLastError(WSAEWOULDBLOCK);
		return SOCKET_ERROR;
	}

	size_t ToGo = min(m_ReadBuffer.GetSize(), (size_t)nBufLen);
	if (ToGo > 0) {
		memcpy(lpBuf, m_ReadBuffer.GetBuffer(), ToGo);
		m_ReadBuffer.ShiftData(ToGo);
	}

	return ToGo;
}

int CUtpSocket::Send(const void* lpBuf, int nBufLen, int /*nFlags*/)
{
	if (m_nLayerState != connected) {
		WSASetLastError(WSAENOTCONN);
		return SOCKET_ERROR;
	}

	CSingleLock writeLock(&m_csWriteBuffer, TRUE);
	TrimWriteBufferForDuplexIfPossible(_T("duplex-send"));

	size_t currentSize = m_WriteBuffer.GetSize();
	size_t currentCapacity = min(m_WriteBuffer.GetLength(), GetMaxWriteBufferCapacity());
	if (currentSize >= currentCapacity) {
		OnWriteBufferBlocked();
		currentSize = m_WriteBuffer.GetSize();
		currentCapacity = min(m_WriteBuffer.GetLength(), GetMaxWriteBufferCapacity());
		if (currentSize >= currentCapacity) {
			m_bAppSendBlocked = true;
			WSASetLastError(WSAEWOULDBLOCK);
			return SOCKET_ERROR;
		}
	}

	size_t ToGo = min(currentCapacity - currentSize, (size_t)nBufLen);
	if (ToGo > 0) {
		m_WriteBuffer.AppendData(lpBuf, ToGo);
		m_bAppSendBlocked = false;
		m_dwLastSendTime = GetTickCount();
	}

	return ToGo;
}

BOOL CUtpSocket::ShutDown(int nHow)
{
	m_ShutDown |= (uint8)(nHow + 1);
	// receives = 0 -> 1 = 10
	// sends = 1    -> 2 = 01
	// both = 2	    -> 3 = 11
	return TRUE;
}

BOOL CUtpSocket::GetPeerName(SOCKADDR* lpSockAddr, int* lpSockAddrLen)
{
	CSingleLock runtimeLock(&g_utpRuntimeLock, TRUE);
	if (!m_Socket)
		return FALSE;

	utp_getpeername(m_Socket, lpSockAddr, lpSockAddrLen);
	return TRUE;
}

BOOL CUtpSocket::GetSockOpt(int nOptionName, void* lpOptionValue, int* lpOptionLen)
{
	if (!m_Socket)
		return FALSE;

	switch (nOptionName)
	{
	case TCP_NODELAY:
		if (lpOptionLen)
			*((char*)lpOptionValue) = 0; // This function is yet not implemented, and besides we don't need it anyway
		return TRUE;
	case SO_RCVBUF:
		{
			CSingleLock readLock(&m_csReadBuffer, TRUE);
			if (*lpOptionLen != sizeof(int))
				return FALSE;
			*((int*)lpOptionValue) = m_ReadBuffer.GetLength();
			return TRUE;
		}
	case SO_SNDBUF:
		{
			CSingleLock writeLock(&m_csWriteBuffer, TRUE);
			if (*lpOptionLen != sizeof(int))
				return FALSE;
			*((int*)lpOptionValue) = m_WriteBuffer.GetLength();
			return TRUE;
		}
	default:
		return FALSE;
	}
}

BOOL CUtpSocket::SetSockOpt(int nOptionName, const void* lpOptionValue, int nOptionLen)
{
	CSingleLock runtimeLock(&g_utpRuntimeLock, TRUE);
	if (!m_Socket)
		return FALSE;

	switch (nOptionName)
	{
	case TCP_NODELAY:
		// I havn't implement the nagle Alghorytm becouse we don't need it, we usualy send's larger packets.
		// If you think we need this funtion anyway, fill free to implement it, it isn't so complicated.
		return FALSE;
	case SO_RCVBUF:
	{
		CSingleLock readLock(&m_csReadBuffer, TRUE);
		if (nOptionLen != sizeof(int))
			return FALSE;
		int PreAlloc = *((int*)lpOptionValue);
		if ((int)m_ReadBuffer.GetSize() < PreAlloc)
			PreAlloc = 0; // we can not set less than we have in buffer
		else
			PreAlloc -= m_ReadBuffer.GetSize();
		m_ReadBuffer.SetSize(m_ReadBuffer.GetSize(), true, PreAlloc);
		utp_setsockopt(m_Socket, UTP_RCVBUF, (int)m_ReadBuffer.GetLength());
		return TRUE;
	}
	case SO_SNDBUF:
	{
		CSingleLock writeLock(&m_csWriteBuffer, TRUE);
		if (nOptionLen != sizeof(int))
			return FALSE;
		int PreAlloc = *((int*)lpOptionValue);
		if ((int)m_WriteBuffer.GetSize() < PreAlloc)
			PreAlloc = 0; // we can not set less than we have in buffer
		else
			PreAlloc -= m_WriteBuffer.GetSize();
		m_WriteBuffer.SetSize(m_WriteBuffer.GetSize(), true, PreAlloc);
		utp_setsockopt(m_Socket, UTP_SNDBUF, (int)m_WriteBuffer.GetLength());
		return TRUE;
	}
	default:
		return FALSE;
	}
}
