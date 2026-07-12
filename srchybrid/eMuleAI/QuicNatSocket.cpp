//This file is part of eMule AI
//Copyright (C)2026 eMule AI
//

#include "stdafx.h"
#include "QuicNatSocket.h"
#include "NgTcp2GnuTlsBridge.h"
#include "../emule.h"
#include "../ClientUDPSocket.h"
#include "../ClientList.h"
#include "../ListenSocket.h"
#include "../Log.h"
#include "../OtherFunctions.h"
#include "../Preferences.h"
#include "../UpdownClient.h"
#include <set>
#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	struct SExpectedQuicPeer
	{
		sockaddr_storage PeerAddr;
		socklen_t PeerAddrLen;
		DWORD Expires;
		CQuicNatSocket* Socket;
	};

	CCriticalSection g_QuicNatSocketLock;
	std::set<CQuicNatSocket*> g_QuicNatSockets;
	std::vector<SExpectedQuicPeer> g_ExpectedQuicPeers;
	const DWORD QUIC_NATT_EXPECT_TTL = 20000;

	bool TryReadSockAddrEndpoint(const sockaddr* addr, socklen_t addrLen, CAddress& ip, uint16& port)
	{
		if (addr == NULL || addrLen <= 0)
			return false;
		port = 0;
		ip.FromSA(addr, addrLen, &port);
		return !ip.IsNull() && port != 0;
	}

	bool SockAddrEqual(const sockaddr* lhs, socklen_t lhsLen, const sockaddr* rhs, socklen_t rhsLen)
	{
		CAddress lhsIP;
		CAddress rhsIP;
		uint16 lhsPort = 0;
		uint16 rhsPort = 0;
		if (TryReadSockAddrEndpoint(lhs, lhsLen, lhsIP, lhsPort) && TryReadSockAddrEndpoint(rhs, rhsLen, rhsIP, rhsPort))
			return lhsPort == rhsPort && lhsIP == rhsIP;

		if (lhs == NULL || rhs == NULL || lhsLen <= 0 || rhsLen <= 0 || lhs->sa_family != rhs->sa_family)
			return false;

		return lhsLen == rhsLen && memcmp(lhs, rhs, lhsLen) == 0;
	}

	bool SockAddrSameHost(const sockaddr* lhs, socklen_t lhsLen, const sockaddr* rhs, socklen_t rhsLen)
	{
		CAddress lhsIP;
		CAddress rhsIP;
		uint16 lhsPort = 0;
		uint16 rhsPort = 0;
		if (TryReadSockAddrEndpoint(lhs, lhsLen, lhsIP, lhsPort) && TryReadSockAddrEndpoint(rhs, rhsLen, rhsIP, rhsPort))
			return lhsIP == rhsIP;

		if (lhs == NULL || rhs == NULL || lhsLen <= 0 || rhsLen <= 0 || lhs->sa_family != rhs->sa_family)
			return false;

		if (lhs->sa_family == AF_INET && lhsLen >= sizeof(sockaddr_in) && rhsLen >= sizeof(sockaddr_in)) {
			const sockaddr_in* a = reinterpret_cast<const sockaddr_in*>(lhs);
			const sockaddr_in* b = reinterpret_cast<const sockaddr_in*>(rhs);
			return a->sin_addr.s_addr == b->sin_addr.s_addr;
		}

		if (lhs->sa_family == AF_INET6 && lhsLen >= sizeof(sockaddr_in6) && rhsLen >= sizeof(sockaddr_in6)) {
			const sockaddr_in6* a = reinterpret_cast<const sockaddr_in6*>(lhs);
			const sockaddr_in6* b = reinterpret_cast<const sockaddr_in6*>(rhs);
			return memcmp(&a->sin6_addr, &b->sin6_addr, sizeof(in6_addr)) == 0;
		}

		return false;
	}

	void PruneExpectedPeers(DWORD now)
	{
		for (std::vector<SExpectedQuicPeer>::iterator it = g_ExpectedQuicPeers.begin(); it != g_ExpectedQuicPeers.end(); ) {
			if ((int)(now - it->Expires) >= 0 || it->Socket == NULL || it->Socket->GetState() == closed)
				it = g_ExpectedQuicPeers.erase(it);
			else
				++it;
		}
	}

	void RemoveExpectedPeersForSocket(CQuicNatSocket* socket)
	{
		if (socket == NULL)
			return;
		CSingleLock lock(&g_QuicNatSocketLock, TRUE);
		for (std::vector<SExpectedQuicPeer>::iterator it = g_ExpectedQuicPeers.begin(); it != g_ExpectedQuicPeers.end(); ) {
			if (it->Socket == socket)
				it = g_ExpectedQuicPeers.erase(it);
			else
				++it;
		}
	}

	bool IsLongHeaderPacket(const BYTE* packet, int size)
	{
		return packet != NULL && size > 0 && (packet[0] & 0x80) != 0;
	}
}

CQuicNatSocket::CQuicNatSocket()
	: m_PeerAddrLen(0)
	, m_ShutDown(0)
	, m_bConnectNotified(false)
	, m_pOwnerClient(NULL)
	, m_pQuic(new CNgTcp2GnuTlsBridge)
{
	memset(&m_PeerAddr, 0, sizeof(m_PeerAddr));
	m_ReadBuffer.AllocBuffer(64 * 1024);
	m_nLayerState = unconnected;
	CSingleLock lock(&g_QuicNatSocketLock, TRUE);
	g_QuicNatSockets.insert(this);
}

CQuicNatSocket::~CQuicNatSocket()
{
	UnregisterFromGlobalSet();
	Close();
	delete m_pQuic;
	m_pQuic = NULL;
}

bool CQuicNatSocket::IsRuntimeAvailable()
{
	return CNgTcp2GnuTlsBridge::IsQuicRuntimeAvailable();
}

void CQuicNatSocket::ProcessAllQuicTimers()
{
	std::vector<CQuicNatSocket*> sockets;
	{
		CSingleLock lock(&g_QuicNatSocketLock, TRUE);
		if (g_QuicNatSockets.empty())
			return;
		sockets.reserve(g_QuicNatSockets.size());
		for (std::set<CQuicNatSocket*>::const_iterator it = g_QuicNatSockets.begin(); it != g_QuicNatSockets.end(); ++it)
			sockets.push_back(*it);
	}

	if (!IsRuntimeAvailable())
		return;

	for (std::vector<CQuicNatSocket*>::iterator it = sockets.begin(); it != sockets.end(); ++it) {
		CQuicNatSocket* socket = *it;
		if (socket != NULL && socket->m_pQuic != NULL)
			socket->m_pQuic->ProcessTimer();
	}
	CNgTcp2GnuTlsBridge::ProcessTimers();
}

bool CQuicNatSocket::ProcessQuicPacket(const BYTE* packet, int size, const struct sockaddr* from, socklen_t fromlen)
{
	if (!IsRuntimeAvailable() || packet == NULL || size <= 0 || from == NULL || fromlen <= 0)
		return false;

	CQuicNatSocket* target = NULL;
	CQuicNatSocket* passiveExpectedTarget = NULL;
	CQuicNatSocket* passiveRebindTarget = NULL;
	CQuicNatSocket* startedExpectedTarget = NULL;
	CQuicNatSocket* peerFallbackTarget = NULL;
	CQuicNatSocket* connectedPeerFallbackTarget = NULL;
	sockaddr_storage rebindOldAddr = {};
	socklen_t rebindOldAddrLen = 0;
	int nPeerFallbackCandidates = 0;
	int nStartedExpectedCandidates = 0;
	int nConnectedPeerFallbackCandidates = 0;
	int nPassiveRebindCandidates = 0;
	const bool bLongHeaderPacket = IsLongHeaderPacket(packet, size);
	{
		CSingleLock lock(&g_QuicNatSocketLock, TRUE);
		for (std::set<CQuicNatSocket*>::const_iterator it = g_QuicNatSockets.begin(); it != g_QuicNatSockets.end(); ++it) {
			CQuicNatSocket* socket = *it;
			if (socket == NULL || socket->m_nLayerState == closed || socket->m_pQuic == NULL || !socket->m_pQuic->HasPeer(from, fromlen))
				continue;

			if (socket->m_pQuic->IsStarted()) {
				if (socket->m_pQuic->MatchesDestinationConnectionId(packet, size)) {
					target = socket;
					break;
				}
				if (peerFallbackTarget == NULL)
					peerFallbackTarget = socket;
				++nPeerFallbackCandidates;
				if (socket->m_nLayerState == connected) {
					if (connectedPeerFallbackTarget == NULL)
						connectedPeerFallbackTarget = socket;
					++nConnectedPeerFallbackCandidates;
				}
			}
		}

		if (target == NULL && bLongHeaderPacket) {
			const DWORD now = ::GetTickCount();
			PruneExpectedPeers(now);
			for (std::vector<SExpectedQuicPeer>::iterator it = g_ExpectedQuicPeers.begin(); it != g_ExpectedQuicPeers.end(); ++it) {
				if (it->Socket == NULL || it->Socket->m_nLayerState == closed)
					continue;
				const sockaddr* expectedAddr = reinterpret_cast<const sockaddr*>(&it->PeerAddr);
				const bool bExactExpected = SockAddrEqual(expectedAddr, it->PeerAddrLen, from, fromlen);
				if (bExactExpected) {
					if (it->Socket->m_pQuic != NULL && !it->Socket->m_pQuic->IsStarted()) {
						passiveExpectedTarget = it->Socket;
						break;
					}
					if (it->Socket->m_pQuic != NULL && it->Socket->m_pQuic->IsStarted() && it->Socket->m_nLayerState != connected) {
						if (startedExpectedTarget == NULL)
							startedExpectedTarget = it->Socket;
						++nStartedExpectedCandidates;
					}
					continue;
				}
				if (it->Socket->m_pQuic != NULL && !it->Socket->m_pQuic->IsStarted() && SockAddrSameHost(expectedAddr, it->PeerAddrLen, from, fromlen)) {
					if (passiveRebindTarget == NULL) {
						passiveRebindTarget = it->Socket;
						memset(&rebindOldAddr, 0, sizeof(rebindOldAddr));
						memcpy(&rebindOldAddr, &it->PeerAddr, it->PeerAddrLen);
						rebindOldAddrLen = it->PeerAddrLen;
					}
					++nPassiveRebindCandidates;
				}
			}
		}
	}

	bool bRebindPassiveEndpoint = false;
	if (target == NULL && passiveExpectedTarget != NULL)
		target = passiveExpectedTarget;
	else if (target == NULL && nPassiveRebindCandidates == 1) {
		target = passiveRebindTarget;
		bRebindPassiveEndpoint = true;
	} else if (target == NULL && nStartedExpectedCandidates == 1)
		target = startedExpectedTarget;
	else if (target == NULL && nPeerFallbackCandidates == 1)
		target = peerFallbackTarget;
	else if (target == NULL && !bLongHeaderPacket && nConnectedPeerFallbackCandidates == 1)
		target = connectedPeerFallbackTarget;

	if (target == NULL || target->m_pQuic == NULL)
		return false;

	if (bRebindPassiveEndpoint) {
		if (thePrefs.GetLogNatTraversalEvents()) {
			CAddress oldIP;
			uint16 oldPort = 0;
			CAddress newIP;
			uint16 newPort = 0;
			oldIP.FromSA(reinterpret_cast<const sockaddr*>(&rebindOldAddr), rebindOldAddrLen, &oldPort);
			newIP.FromSA(from, fromlen, &newPort);
			DebugLog(_T("[NAT-T][QUIC] Rebinding passive expected endpoint from %s:%u to %s:%u for incoming Initial diag={%s}"),
				(LPCTSTR)ipstr(oldIP), (UINT)oldPort, (LPCTSTR)ipstr(newIP), (UINT)newPort, (LPCTSTR)GetRoutingDiagnostics(packet, size, from, fromlen));
		}
		target->ExpectPeer(from, fromlen);
	}

	if (!target->m_pQuic->IsStarted()) {
		if (!bLongHeaderPacket)
			return false;
		if (thePrefs.GetLogNatTraversalEvents()) {
			CAddress IP;
			uint16 nPort = 0;
			IP.FromSA(from, fromlen, &nPort);
			DebugLog(_T("[NAT-T][QUIC] Routing incoming Initial to passive endpoint from %s:%u"), (LPCTSTR)ipstr(IP), (UINT)nPort);
		}
		if (!target->m_pQuic->StartServer(target, packet, size, from, fromlen)) {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLogWarning(_T("[NAT-T][QUIC] StartServer failed for passive endpoint diag={%s}"), (LPCTSTR)GetRoutingDiagnostics(packet, size, from, fromlen));
			target->ResolveOwnerClient();
			if (target->m_pOwnerClient != NULL) {
				CAddress IP;
				uint16 nPort = 0;
				IP.FromSA(from, fromlen, &nPort);
				target->m_pOwnerClient->MarkNatTraversalQuicFailed(IP, nPort, _T("passive QUIC StartServer failed"));
			}
			return false;
		}
		return true;
	}
	return target->m_pQuic->ProcessPacket(packet, size, from, fromlen);
}

CString CQuicNatSocket::GetRoutingDiagnostics(const BYTE* packet, int size, const struct sockaddr* from, socklen_t fromlen)
{
	CString diag;
	CAddress IP;
	uint16 nPort = 0;
	if (from != NULL && fromlen > 0)
		IP.FromSA(from, fromlen, &nPort);
	const bool bLongHeaderPacket = IsLongHeaderPacket(packet, size);
	const BYTE byFirst = (packet != NULL && size > 0) ? packet[0] : 0;
	DWORD now = ::GetTickCount();
	UINT totalSockets = 0;
	UINT startedPeerMatches = 0;
	UINT connectedPeerMatches = 0;
	UINT expectedTotal = 0;
	UINT expectedExact = 0;
	UINT expectedExactUnstarted = 0;
	UINT expectedSameHost = 0;
	UINT expectedSameHostUnstarted = 0;
	UINT expectedExpired = 0;
	UINT unstartedSockets = 0;
	{
		CSingleLock lock(&g_QuicNatSocketLock, TRUE);
		totalSockets = (UINT)g_QuicNatSockets.size();
		for (std::set<CQuicNatSocket*>::const_iterator it = g_QuicNatSockets.begin(); it != g_QuicNatSockets.end(); ++it) {
			CQuicNatSocket* socket = *it;
			if (socket == NULL || socket->m_pQuic == NULL)
				continue;
			if (!socket->m_pQuic->IsStarted())
				++unstartedSockets;
			if (from != NULL && fromlen > 0 && socket->m_pQuic->HasPeer(from, fromlen)) {
				++startedPeerMatches;
				if (socket->m_nLayerState == connected)
					++connectedPeerMatches;
			}
		}
		PruneExpectedPeers(now);
		expectedTotal = (UINT)g_ExpectedQuicPeers.size();
		for (std::vector<SExpectedQuicPeer>::const_iterator it = g_ExpectedQuicPeers.begin(); it != g_ExpectedQuicPeers.end(); ++it) {
			if ((int)(now - it->Expires) >= 0)
				++expectedExpired;
			if (from != NULL && fromlen > 0 && SockAddrEqual(reinterpret_cast<const sockaddr*>(&it->PeerAddr), it->PeerAddrLen, from, fromlen)) {
				++expectedExact;
				if (it->Socket != NULL && it->Socket->m_pQuic != NULL && !it->Socket->m_pQuic->IsStarted())
					++expectedExactUnstarted;
			} else if (from != NULL && fromlen > 0 && SockAddrSameHost(reinterpret_cast<const sockaddr*>(&it->PeerAddr), it->PeerAddrLen, from, fromlen)) {
				++expectedSameHost;
				if (it->Socket != NULL && it->Socket->m_pQuic != NULL && !it->Socket->m_pQuic->IsStarted())
					++expectedSameHostUnstarted;
			}
		}
	}
	diag.Format(_T("from=%s:%u family=%d len=%d size=%d first=0x%02X long=%d sockets=%u unstarted=%u peerMatches=%u connectedPeerMatches=%u expected=%u expectedExact=%u expectedExactUnstarted=%u expectedSameHost=%u expectedSameHostUnstarted=%u expectedExpired=%u"),
		(LPCTSTR)ipstr(IP), (UINT)nPort, from != NULL ? from->sa_family : 0, (int)fromlen, size, (UINT)byFirst, bLongHeaderPacket ? 1 : 0,
		totalSockets, unstartedSockets, startedPeerMatches, connectedPeerMatches, expectedTotal, expectedExact, expectedExactUnstarted, expectedSameHost, expectedSameHostUnstarted, expectedExpired);
	return diag;
}

void CQuicNatSocket::UnregisterFromGlobalSet()
{
	CSingleLock lock(&g_QuicNatSocketLock, TRUE);
	g_QuicNatSockets.erase(this);
	for (std::vector<SExpectedQuicPeer>::iterator it = g_ExpectedQuicPeers.begin(); it != g_ExpectedQuicPeers.end(); ) {
		if (it->Socket == this)
			it = g_ExpectedQuicPeers.erase(it);
		else
			++it;
	}
}

void CQuicNatSocket::ExpectPeer(const struct sockaddr* to, socklen_t tolen)
{
	if (to == NULL || tolen <= 0 || (size_t)tolen > sizeof(m_PeerAddr) || m_nLayerState == closed)
		return;
	ResolveOwnerClient();
	memset(&m_PeerAddr, 0, sizeof(m_PeerAddr));
	memcpy(&m_PeerAddr, to, tolen);
	m_PeerAddrLen = tolen;

	if (theApp.clientudp != NULL)
		theApp.clientudp->SendQuicNatKeyFrame(to, tolen);

	CSingleLock lock(&g_QuicNatSocketLock, TRUE);
	PruneExpectedPeers(::GetTickCount());
	for (std::vector<SExpectedQuicPeer>::iterator it = g_ExpectedQuicPeers.begin(); it != g_ExpectedQuicPeers.end(); ++it) {
		if (it->Socket == this) {
			const bool bEndpointChanged = !SockAddrEqual(reinterpret_cast<const sockaddr*>(&it->PeerAddr), it->PeerAddrLen, to, tolen);
			if (bEndpointChanged && thePrefs.GetLogNatTraversalEvents()) {
				CAddress oldIP;
				uint16 oldPort = 0;
				CAddress newIP;
				uint16 newPort = 0;
				oldIP.FromSA(reinterpret_cast<const sockaddr*>(&it->PeerAddr), it->PeerAddrLen, &oldPort);
				newIP.FromSA(to, tolen, &newPort);
				DebugLog(_T("[NAT-T][QUIC] Refresh expected endpoint %s:%u -> %s:%u socket=%p owner=%p started=%d state=%d"),
					(LPCTSTR)ipstr(oldIP), (UINT)oldPort, (LPCTSTR)ipstr(newIP), (UINT)newPort, this, m_pOwnerClient, m_pQuic != NULL && m_pQuic->IsStarted() ? 1 : 0, (int)m_nLayerState);
			}
			memset(&it->PeerAddr, 0, sizeof(it->PeerAddr));
			memcpy(&it->PeerAddr, to, tolen);
			it->PeerAddrLen = tolen;
			it->Expires = ::GetTickCount() + QUIC_NATT_EXPECT_TTL;
			return;
		}
	}

	SExpectedQuicPeer expected;
	memset(&expected, 0, sizeof(expected));
	memcpy(&expected.PeerAddr, to, tolen);
	expected.PeerAddrLen = tolen;
	expected.Expires = ::GetTickCount() + QUIC_NATT_EXPECT_TTL;
	expected.Socket = this;
	g_ExpectedQuicPeers.push_back(expected);

	if (thePrefs.GetLogNatTraversalEvents()) {
		CAddress IP;
		uint16 nPort = 0;
		IP.FromSA(to, tolen, &nPort);
		DebugLog(_T("[NAT-T][QUIC] ExpectPeer endpoint=%s:%u socket=%p owner=%p started=%d state=%d"),
			(LPCTSTR)ipstr(IP), (UINT)nPort, this, m_pOwnerClient, m_pQuic != NULL && m_pQuic->IsStarted() ? 1 : 0, (int)m_nLayerState);
	}
}

void CQuicNatSocket::OnReceive(int nErrorCode)
{
	if (m_pOwnerSocket)
		m_pOwnerSocket->OnReceive(nErrorCode);
}

bool CQuicNatSocket::Create(UINT nSocketPort, int nSocketType, long lEvent, const CString& sSocketAddress, ADDRESS_FAMILY nFamily, bool reusable)
{
	UNREFERENCED_PARAMETER(nSocketPort);
	UNREFERENCED_PARAMETER(nSocketType);
	UNREFERENCED_PARAMETER(lEvent);
	UNREFERENCED_PARAMETER(sSocketAddress);
	UNREFERENCED_PARAMETER(nFamily);
	UNREFERENCED_PARAMETER(reusable);
	m_ShutDown = 0;
	m_bConnectNotified = false;
	m_nLayerState = unconnected;
	return true;
}

BOOL CQuicNatSocket::Connect(const LPSOCKADDR lpSockAddr, int nSockAddrLen)
{
	if (!IsRuntimeAvailable() || lpSockAddr == NULL || nSockAddrLen <= 0) {
		WSASetLastError(WSAEOPNOTSUPP);
		return FALSE;
	}

	ResolveOwnerClient();
	m_ShutDown = 0;
	m_bConnectNotified = false;
	m_nLayerState = connecting;
	ExpectPeer(lpSockAddr, (socklen_t)nSockAddrLen);
	if (!m_pQuic->StartClient(this, lpSockAddr, (socklen_t)nSockAddrLen)) {
		m_nLayerState = closed;
		WSASetLastError(WSAECONNREFUSED);
		return FALSE;
	}
	WSASetLastError(WSAEWOULDBLOCK);
	return FALSE;
}

void CQuicNatSocket::Close()
{
	m_ShutDown = 0x03;
	RemoveExpectedPeersForSocket(this);
	if (m_pQuic != NULL)
		m_pQuic->Close();
	{
		CSingleLock lock(&m_csReadBuffer, TRUE);
		m_ReadBuffer.SetSize(0, true);
	}
	memset(&m_PeerAddr, 0, sizeof(m_PeerAddr));
	m_PeerAddrLen = 0;
	m_nLayerState = closed;
}

BOOL CQuicNatSocket::GetPeerName(SOCKADDR* lpSockAddr, int* lpSockAddrLen)
{
	if (!lpSockAddr || !lpSockAddrLen || m_PeerAddrLen <= 0 || *lpSockAddrLen < (int)m_PeerAddrLen) {
		WSASetLastError(WSAEINVAL);
		return FALSE;
	}
	memcpy(lpSockAddr, &m_PeerAddr, m_PeerAddrLen);
	*lpSockAddrLen = (int)m_PeerAddrLen;
	return TRUE;
}

int CQuicNatSocket::Receive(void* lpBuf, int nBufLen, int nFlags)
{
	UNREFERENCED_PARAMETER(nFlags);
	if (!lpBuf || nBufLen <= 0) {
		WSASetLastError(WSAEINVAL);
		return SOCKET_ERROR;
	}

	CSingleLock lock(&m_csReadBuffer, TRUE);
	const size_t uAvailable = m_ReadBuffer.GetSize();
	if (uAvailable == 0) {
		WSASetLastError(WSAEWOULDBLOCK);
		return SOCKET_ERROR;
	}

	const size_t uCopy = min((size_t)nBufLen, uAvailable);
	memcpy(lpBuf, m_ReadBuffer.GetBuffer(), uCopy);
	m_ReadBuffer.ShiftData(uCopy);
	return (int)uCopy;
}

int CQuicNatSocket::Send(const void* lpBuf, int nBufLen, int nFlags)
{
	UNREFERENCED_PARAMETER(nFlags);
	if (!IsRuntimeAvailable() || lpBuf == NULL || nBufLen <= 0 || m_pQuic == NULL) {
		WSASetLastError(WSAEINVAL);
		return SOCKET_ERROR;
	}
	return m_pQuic->SendStreamData(lpBuf, nBufLen);
}

BOOL CQuicNatSocket::ShutDown(int nHow)
{
	if (nHow == sends || nHow == both)
		m_ShutDown |= 0x02;
	if (nHow == receives || nHow == both)
		m_ShutDown |= 0x01;
	if (nHow == both) {
		m_pOwnerClient = NULL;
		Close();
	}
	return TRUE;
}

BOOL CQuicNatSocket::GetSockOpt(int nOptionName, void* lpOptionValue, int* lpOptionLen)
{
	UNREFERENCED_PARAMETER(nOptionName);
	UNREFERENCED_PARAMETER(lpOptionValue);
	UNREFERENCED_PARAMETER(lpOptionLen);
	WSASetLastError(WSAENOPROTOOPT);
	return FALSE;
}

BOOL CQuicNatSocket::SetSockOpt(int nOptionName, const void* lpOptionValue, int nOptionLen)
{
	UNREFERENCED_PARAMETER(nOptionName);
	UNREFERENCED_PARAMETER(lpOptionValue);
	UNREFERENCED_PARAMETER(nOptionLen);
	return TRUE;
}

void CQuicNatSocket::ResolveOwnerClient()
{
	if (m_pOwnerClient != NULL && theApp.clientlist != NULL && !theApp.clientlist->IsValidClient(m_pOwnerClient))
		m_pOwnerClient = NULL;
	if (m_pOwnerClient != NULL || m_pOwnerSocket == NULL)
		return;
	CClientReqSocket* clientSocket = DYNAMIC_DOWNCAST(CClientReqSocket, m_pOwnerSocket);
	if (clientSocket != NULL)
		m_pOwnerClient = clientSocket->client;
}

void CQuicNatSocket::AppendReceivedData(const BYTE* data, size_t len)
{
	if (data == NULL || len == 0 || (m_ShutDown & 0x01) != 0)
		return;
	{
		CSingleLock lock(&m_csReadBuffer, TRUE);
		m_ReadBuffer.AppendData(data, len);
	}
	OnReceive(0);
}

void CQuicNatSocket::NotifyConnected()
{
	ResolveOwnerClient();
	m_nLayerState = connected;
	if (!m_bConnectNotified) {
		m_bConnectNotified = true;
		if (m_pOwnerSocket != NULL)
			m_pOwnerSocket->OnConnect(0);
	}
	ResolveOwnerClient();
	if (m_pOwnerClient != NULL) {
		m_pOwnerClient->ClearNatTraversalQuicFailure();
		m_pOwnerClient->SetUtpConnectionStartTick(::GetTickCount());
		m_pOwnerClient->SetUtpWritable(true);
	}
	NotifySendReady();
}

void CQuicNatSocket::NotifySendReady()
{
	ResolveOwnerClient();
	if (m_pOwnerClient != NULL)
		m_pOwnerClient->SetUtpWritable(true);
	OnSend(0);
}

void CQuicNatSocket::NotifyClosed(int nErrorCode)
{
	if (m_nLayerState == closed)
		return;
	const bool bClosedBeforeConnect = (m_nLayerState != connected);
	RemoveExpectedPeersForSocket(this);
	ResolveOwnerClient();
	if (m_pOwnerClient != NULL) {
		if (bClosedBeforeConnect && m_PeerAddrLen > 0) {
			CAddress IP;
			uint16 nPort = 0;
			IP.FromSA(reinterpret_cast<const sockaddr*>(&m_PeerAddr), m_PeerAddrLen, &nPort);
			m_pOwnerClient->MarkNatTraversalQuicFailed(IP, nPort, _T("QUIC layer closed before connect"));
		}
		m_pOwnerClient->ResetUtpFlowControl();
	}
	memset(&m_PeerAddr, 0, sizeof(m_PeerAddr));
	m_PeerAddrLen = 0;
	m_nLayerState = closed;
	OnClose(nErrorCode);
}

bool CQuicNatSocket::BuildLocalProof(BYTE* proof, size_t len) const
{
	const_cast<CQuicNatSocket*>(this)->ResolveOwnerClient();
	if (proof == NULL || len < EMULEAI_QUIC_NATT_PROOF_LEN)
		return false;
	memset(proof, 0, len);
	memcpy(proof, EMULEAI_QUIC_NATT_PROOF_MAGIC, 5);
	memcpy(proof + 5, thePrefs.GetUserHash(), 16);
	if (m_pOwnerClient != NULL && m_pOwnerClient->HasValidHash())
		memcpy(proof + 21, m_pOwnerClient->GetUserHash(), 16);
	return true;
}

bool CQuicNatSocket::ValidateRemoteProof(const BYTE* proof, size_t len) const
{
	const_cast<CQuicNatSocket*>(this)->ResolveOwnerClient();
	if (proof == NULL || len < EMULEAI_QUIC_NATT_PROOF_LEN || memcmp(proof, EMULEAI_QUIC_NATT_PROOF_MAGIC, 5) != 0)
		return false;

	const bool bTargetHashMatches = memcmp(proof + 21, thePrefs.GetUserHash(), 16) == 0 || isnulmd4(proof + 21);
	if (!bTargetHashMatches)
		return false;

	if (m_pOwnerClient != NULL && m_pOwnerClient->HasValidHash() && memcmp(proof + 5, m_pOwnerClient->GetUserHash(), 16) != 0) {
		const bool bTrustedNatEndpoint = m_pOwnerClient->HasDirectNatTraversalCaps();
		if (!bTrustedNatEndpoint)
			return false;
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLogWarning(_T("[NAT-T][QUIC] Accepting proof with refreshed peer hash for direct NAT-T endpoint socket=%p client=%s"), this, (LPCTSTR)EscPercent(m_pOwnerClient->DbgGetClientInfo()));
	}
	return true;
}

bool CQuicNatSocket::HasValidRemoteHash() const
{
	return m_pOwnerClient != NULL && m_pOwnerClient->HasValidHash();
}
