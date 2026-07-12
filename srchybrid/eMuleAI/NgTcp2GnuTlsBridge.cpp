//This file is part of eMule AI
//Copyright (C)2026 eMule AI
//

#include "stdafx.h"
#include "NgTcp2GnuTlsBridge.h"
#include "QuicNatSocket.h"
#include "../emule.h"
#include "../ClientUDPSocket.h"
#include "../Log.h"
#include "../Preferences.h"
#include <ngtcp2/ngtcp2_crypto_gnutls.h>
#include <gnutls/crypto.h>
#include <gnutls/x509.h>
#include <time.h>
#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	CCriticalSection g_QuicRuntimeLock;
	bool g_bQuicRuntimeInitialized = false;
	bool g_bQuicRuntimeAvailable = false;
	gnutls_certificate_credentials_t g_ClientCredentials = NULL;
	gnutls_certificate_credentials_t g_ServerCredentials = NULL;
	gnutls_x509_privkey_t g_ServerKey = NULL;
	gnutls_x509_crt_t g_ServerCert = NULL;

	const char QUIC_GNUTLS_PRIORITY[] = "NORMAL:-VERS-ALL:+VERS-TLS1.3:-CIPHER-ALL:+AES-128-GCM:+AES-256-GCM:+CHACHA20-POLY1305:+AES-128-CCM:-GROUP-ALL:+GROUP-SECP256R1:+GROUP-X25519:+GROUP-SECP384R1:%DISABLE_TLS13_COMPAT_MODE";
	const gnutls_datum_t QUIC_ALPN = { reinterpret_cast<unsigned char*>(const_cast<char*>(EMULEAI_QUIC_NATT_ALPN)), (unsigned int)(sizeof(EMULEAI_QUIC_NATT_ALPN) - 1) };
	const DWORD QUIC_NATT_HANDSHAKE_PROBE_INTERVAL_MS = 250;
	const uint8 QUIC_NATT_HANDSHAKE_PROBE_MAX = 12;
	const size_t QUIC_NATT_DEFAULT_WRITE_BUFFER_SIZE = 64 * 1024;
	const size_t QUIC_NATT_CONNECTED_WRITE_BUFFER_SIZE = 2 * 1024 * 1024;
	const size_t QUIC_NATT_ADAPTIVE_WRITE_BUFFER_SIZE = 4 * 1024 * 1024;
	const size_t QUIC_NATT_MAX_WRITE_BUFFER_SIZE = 8 * 1024 * 1024;
	const uint32 QUIC_NATT_WRITE_BUFFER_PROMOTION_BLOCKS = 64;
	const DWORD QUIC_NATT_WRITE_BUFFER_IDLE_SHRINK_MS = SEC2MS(30);
	const int QUIC_NATT_MIN_PUMP_DATAGRAMS = 32;
	const int QUIC_NATT_MAX_PUMP_DATAGRAMS = 64;
	const DWORD QUIC_NATT_KEEPALIVE_TIMEOUT_MS = SEC2MS(25);
	const ngtcp2_cc_algo QUIC_NATT_CONGESTION_CONTROL = NGTCP2_CC_ALGO_BBR;

	void BuildAnyLocalAddress(int family, sockaddr_storage& addr, socklen_t& addrlen)
	{
		memset(&addr, 0, sizeof(addr));
		if (family == AF_INET6) {
			sockaddr_in6* sa = reinterpret_cast<sockaddr_in6*>(&addr);
			sa->sin6_family = AF_INET6;
			sa->sin6_port = htons(thePrefs.GetUDPPort());
			addrlen = sizeof(sockaddr_in6);
		} else {
			sockaddr_in* sa = reinterpret_cast<sockaddr_in*>(&addr);
			sa->sin_family = AF_INET;
			sa->sin_port = htons(thePrefs.GetUDPPort());
			sa->sin_addr.s_addr = INADDR_ANY;
			addrlen = sizeof(sockaddr_in);
		}
	}
}

CNgTcp2GnuTlsBridge::CNgTcp2GnuTlsBridge()
	: m_owner(NULL)
	, m_conn(NULL)
	, m_session(NULL)
	, m_role(QuicNatRole_Client)
	, m_LocalStreamId(-1)
	, m_RemoteStreamId(-1)
	, m_bHandshakeCompleted(false)
	, m_bLocalProofQueued(false)
	, m_bRemoteProofAccepted(false)
	, m_bKeepAliveConfigured(false)
	, m_uWriteBaseOffset(0)
	, m_uWriteSubmittedOffset(0)
	, m_uWriteAckedOffset(0)
	, m_uRequestedWriteBufferSize(QUIC_NATT_CONNECTED_WRITE_BUFFER_SIZE)
	, m_uWriteBufferBlockCountAtCapacity(0)
	, m_dwWriteBufferIdleSinceTick(0)
	, m_LastHandshakePeerAddrLen(0)
	, m_dwNextHandshakeProbeTick(0)
	, m_byHandshakeProbeCount(0)
	, m_bSendReadyPending(false)
	, m_bCloseNotifyPending(false)
	, m_bAppSendBlocked(false)
	, m_nPendingCloseError(0)
	, m_PeerAddrLen(0)
	, m_dwLastPumpTick(0)
{
	memset(&m_connRef, 0, sizeof(m_connRef));
	memset(&m_LastHandshakePeerAddr, 0, sizeof(m_LastHandshakePeerAddr));
	memset(&m_PeerAddr, 0, sizeof(m_PeerAddr));
	m_WriteBuffer.AllocBuffer(QUIC_NATT_DEFAULT_WRITE_BUFFER_SIZE);
	m_ProofBuffer.AllocBuffer(EMULEAI_QUIC_NATT_PROOF_LEN);
	m_PendingAppData.AllocBuffer(64 * 1024);
	m_LastHandshakePacket.AllocBuffer(1500);
}

CNgTcp2GnuTlsBridge::~CNgTcp2GnuTlsBridge()
{
	Close();
}

bool CNgTcp2GnuTlsBridge::IsQuicRuntimeAvailable()
{
#if EMULEAI_ENABLE_QUIC_NATT_RUNTIME
	return EnsureRuntimeInitialized();
#else
	return false;
#endif
}

void CNgTcp2GnuTlsBridge::ProcessTimers()
{
}

bool CNgTcp2GnuTlsBridge::EnsureRuntimeInitialized()
{
#if !EMULEAI_ENABLE_QUIC_NATT_RUNTIME
	return false;
#else
	CSingleLock lock(&g_QuicRuntimeLock, TRUE);
	if (g_bQuicRuntimeInitialized)
		return g_bQuicRuntimeAvailable;

	g_bQuicRuntimeInitialized = true;
	if (gnutls_global_init() != GNUTLS_E_SUCCESS)
		return false;
	if (InitGnuTlsCredentials() != GNUTLS_E_SUCCESS)
		return false;
	g_bQuicRuntimeAvailable = true;
	return true;
#endif
}

void CNgTcp2GnuTlsBridge::CleanupRuntime()
{
	CSingleLock lock(&g_QuicRuntimeLock, TRUE);
	if (g_ClientCredentials != NULL) {
		gnutls_certificate_free_credentials(g_ClientCredentials);
		g_ClientCredentials = NULL;
	}
	if (g_ServerCredentials != NULL) {
		gnutls_certificate_free_credentials(g_ServerCredentials);
		g_ServerCredentials = NULL;
	}
	if (g_ServerCert != NULL) {
		gnutls_x509_crt_deinit(g_ServerCert);
		g_ServerCert = NULL;
	}
	if (g_ServerKey != NULL) {
		gnutls_x509_privkey_deinit(g_ServerKey);
		g_ServerKey = NULL;
	}
	g_bQuicRuntimeAvailable = false;
	g_bQuicRuntimeInitialized = false;
}

void CNgTcp2GnuTlsBridge::FillRandom(BYTE* data, size_t len)
{
	if (data == NULL || len == 0)
		return;
	if (gnutls_rnd(GNUTLS_RND_RANDOM, data, len) != GNUTLS_E_SUCCESS) {
		for (size_t i = 0; i < len; ++i)
			data[i] = (BYTE)(rand() & 0xFF);
	}
}

uint64 CNgTcp2GnuTlsBridge::GetTimestamp()
{
	return (uint64)::GetTickCount() * NGTCP2_MILLISECONDS;
}

int CNgTcp2GnuTlsBridge::InitGnuTlsCredentials()
{
	int rv = gnutls_certificate_allocate_credentials(&g_ClientCredentials);
	if (rv != GNUTLS_E_SUCCESS)
		return rv;
	rv = gnutls_certificate_allocate_credentials(&g_ServerCredentials);
	if (rv != GNUTLS_E_SUCCESS)
		return rv;
	rv = gnutls_x509_privkey_init(&g_ServerKey);
	if (rv != GNUTLS_E_SUCCESS)
		return rv;
	rv = gnutls_x509_crt_init(&g_ServerCert);
	if (rv != GNUTLS_E_SUCCESS)
		return rv;
	rv = gnutls_x509_privkey_generate(g_ServerKey, GNUTLS_PK_RSA, 2048, 0);
	if (rv != GNUTLS_E_SUCCESS)
		return rv;

	unsigned char serial[8];
	FillRandom(serial, sizeof(serial));
	gnutls_x509_crt_set_version(g_ServerCert, 3);
	gnutls_x509_crt_set_serial(g_ServerCert, serial, sizeof(serial));
	gnutls_x509_crt_set_activation_time(g_ServerCert, (time_t)::time(NULL) - 3600);
	gnutls_x509_crt_set_expiration_time(g_ServerCert, (time_t)::time(NULL) + 60 * 60 * 24 * 365 * 5);
	gnutls_x509_crt_set_dn_by_oid(g_ServerCert, GNUTLS_OID_X520_COMMON_NAME, 0, "eMuleAI QUIC NAT-T", (unsigned int)strlen("eMuleAI QUIC NAT-T"));
	rv = gnutls_x509_crt_set_key(g_ServerCert, g_ServerKey);
	if (rv != GNUTLS_E_SUCCESS)
		return rv;
	rv = gnutls_x509_crt_sign2(g_ServerCert, g_ServerCert, g_ServerKey, GNUTLS_DIG_SHA256, 0);
	if (rv != GNUTLS_E_SUCCESS)
		return rv;
	return gnutls_certificate_set_x509_key(g_ServerCredentials, &g_ServerCert, 1, g_ServerKey);
}

bool CNgTcp2GnuTlsBridge::InitGnuTlsSession(EQuicNatRole role)
{
	int flags = (role == QuicNatRole_Client) ? GNUTLS_CLIENT : GNUTLS_SERVER;
	int rv = gnutls_init(&m_session, flags | GNUTLS_NO_END_OF_EARLY_DATA);
	if (rv != GNUTLS_E_SUCCESS)
		return false;

	rv = (role == QuicNatRole_Client) ? ngtcp2_crypto_gnutls_configure_client_session(m_session) : ngtcp2_crypto_gnutls_configure_server_session(m_session);
	if (rv != 0)
		return false;

	rv = gnutls_priority_set_direct(m_session, QUIC_GNUTLS_PRIORITY, NULL);
	if (rv != GNUTLS_E_SUCCESS)
		return false;

	gnutls_session_set_ptr(m_session, &m_connRef);
	rv = gnutls_credentials_set(m_session, GNUTLS_CRD_CERTIFICATE, role == QuicNatRole_Client ? g_ClientCredentials : g_ServerCredentials);
	if (rv != GNUTLS_E_SUCCESS)
		return false;
	rv = gnutls_alpn_set_protocols(m_session, &QUIC_ALPN, 1, GNUTLS_ALPN_MANDATORY);
	if (rv != GNUTLS_E_SUCCESS)
		return false;
	if (role == QuicNatRole_Client)
		gnutls_server_name_set(m_session, GNUTLS_NAME_DNS, "emuleai-natt", strlen("emuleai-natt"));
	return true;
}

bool CNgTcp2GnuTlsBridge::StartClient(CQuicNatSocket* owner, const struct sockaddr* peer, socklen_t peerlen)
{
	if (owner == NULL || peer == NULL || peerlen <= 0 || !EnsureRuntimeInitialized())
		return false;
	Close();
	m_owner = owner;
	m_role = QuicNatRole_Client;
	memset(&m_PeerAddr, 0, sizeof(m_PeerAddr));
	memcpy(&m_PeerAddr, peer, peerlen);
	m_PeerAddrLen = peerlen;
	if (!InitGnuTlsSession(m_role) || !InitNgTcp2Connection(m_role, NULL, 0, peer, peerlen))
		return false;
	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NAT-T][QUIC] Client handshake started, owner=%p"), owner);
	PumpOutput();
	return true;
}

bool CNgTcp2GnuTlsBridge::StartServer(CQuicNatSocket* owner, const BYTE* firstPacket, int firstPacketSize, const struct sockaddr* peer, socklen_t peerlen)
{
	if (owner == NULL || firstPacket == NULL || firstPacketSize <= 0 || peer == NULL || peerlen <= 0 || !EnsureRuntimeInitialized())
		return false;
	Close();
	m_owner = owner;
	m_role = QuicNatRole_Server;
	memset(&m_PeerAddr, 0, sizeof(m_PeerAddr));
	memcpy(&m_PeerAddr, peer, peerlen);
	m_PeerAddrLen = peerlen;
	if (!InitGnuTlsSession(m_role) || !InitNgTcp2Connection(m_role, firstPacket, firstPacketSize, peer, peerlen))
		return false;
	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NAT-T][QUIC] Server handshake started, owner=%p firstPacket=%d"), owner, firstPacketSize);
	return ProcessPacket(firstPacket, firstPacketSize, peer, peerlen);
}

bool CNgTcp2GnuTlsBridge::InitNgTcp2Connection(EQuicNatRole role, const BYTE* firstPacket, int firstPacketSize, const struct sockaddr* peer, socklen_t peerlen)
{
	ngtcp2_callbacks callbacks;
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.client_initial = ngtcp2_crypto_client_initial_cb;
	callbacks.recv_client_initial = ngtcp2_crypto_recv_client_initial_cb;
	callbacks.recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb;
	callbacks.handshake_completed = OnHandshakeCompletedCb;
	callbacks.encrypt = ngtcp2_crypto_encrypt_cb;
	callbacks.decrypt = ngtcp2_crypto_decrypt_cb;
	callbacks.hp_mask = ngtcp2_crypto_hp_mask_cb;
	callbacks.recv_stream_data = OnRecvStreamDataCb;
	callbacks.acked_stream_data_offset = OnAckedStreamDataCb;
	callbacks.stream_open = OnStreamOpenCb;
	callbacks.stream_close = OnStreamCloseCb;
	callbacks.recv_retry = ngtcp2_crypto_recv_retry_cb;
	callbacks.extend_max_local_streams_bidi = OnExtendMaxLocalStreamsBidiCb;
	callbacks.rand = OnRandCb;
	callbacks.update_key = ngtcp2_crypto_update_key_cb;
	callbacks.extend_max_stream_data = OnExtendMaxStreamDataCb;
	callbacks.delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb;
	callbacks.delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
	callbacks.version_negotiation = ngtcp2_crypto_version_negotiation_cb;
	callbacks.get_new_connection_id2 = OnGetNewConnectionIdCb;
	callbacks.get_path_challenge_data2 = ngtcp2_crypto_get_path_challenge_data2_cb;

	ngtcp2_settings settings;
	ngtcp2_settings_default(&settings);
	settings.cc_algo = QUIC_NATT_CONGESTION_CONTROL;
	settings.initial_ts = GetTimestamp();
	settings.max_tx_udp_payload_size = 1200;
	settings.no_tx_udp_payload_size_shaping = 1;

	ngtcp2_transport_params params;
	ngtcp2_transport_params_default(&params);
	params.initial_max_streams_bidi = 1;
	params.initial_max_streams_uni = 0;
	params.initial_max_stream_data_bidi_local = 512 * 1024;
	params.initial_max_stream_data_bidi_remote = 512 * 1024;
	params.initial_max_stream_data_uni = 0;
	params.initial_max_data = 4 * 1024 * 1024;

	sockaddr_storage localAddr;
	socklen_t localAddrLen = 0;
	BuildAnyLocalAddress(peer->sa_family, localAddr, localAddrLen);
	ngtcp2_path_storage ps;
	ngtcp2_path_storage_init(&ps, reinterpret_cast<const ngtcp2_sockaddr*>(&localAddr), localAddrLen, reinterpret_cast<const ngtcp2_sockaddr*>(peer), peerlen, NULL);

	m_connRef.get_conn = GetNgTcp2Conn;
	m_connRef.user_data = this;

	ngtcp2_cid dcid;
	ngtcp2_cid scid;
	memset(&dcid, 0, sizeof(dcid));
	memset(&scid, 0, sizeof(scid));
	FillRandom(scid.data, 8);
	scid.datalen = 8;

	int rv = 0;
	if (role == QuicNatRole_Client) {
		dcid.datalen = NGTCP2_MIN_INITIAL_DCIDLEN;
		FillRandom(dcid.data, dcid.datalen);
		rv = ngtcp2_conn_client_new(&m_conn, &dcid, &scid, &ps.path, NGTCP2_PROTO_VER_V1, &callbacks, &settings, &params, NULL, this);
	} else {
		ngtcp2_version_cid vcid;
		memset(&vcid, 0, sizeof(vcid));
		rv = ngtcp2_pkt_decode_version_cid(&vcid, firstPacket, (size_t)firstPacketSize, NGTCP2_MIN_INITIAL_DCIDLEN);
		if (rv != 0 && rv != NGTCP2_ERR_VERSION_NEGOTIATION)
			return false;
		if (vcid.scid == NULL || vcid.scidlen == 0 || vcid.scidlen > NGTCP2_MAX_CIDLEN)
			return false;
		if (vcid.dcid == NULL || vcid.dcidlen == 0 || vcid.dcidlen > NGTCP2_MAX_CIDLEN)
			return false;
		memcpy(dcid.data, vcid.scid, vcid.scidlen);
		dcid.datalen = vcid.scidlen;
		memcpy(params.original_dcid.data, vcid.dcid, vcid.dcidlen);
		params.original_dcid.datalen = vcid.dcidlen;
		params.original_dcid_present = 1;
		rv = ngtcp2_conn_server_new(&m_conn, &dcid, &scid, &ps.path, vcid.version == 0 ? NGTCP2_PROTO_VER_V1 : vcid.version, &callbacks, &settings, &params, NULL, this);
	}
	if (rv != 0)
		return false;
	ngtcp2_conn_set_tls_native_handle(m_conn, m_session);
	return true;
}

bool CNgTcp2GnuTlsBridge::ProcessPacket(const BYTE* packet, int size, const struct sockaddr* from, socklen_t fromlen)
{
	bool bHandled = false;
	{
		CSingleLock lock(&m_csBridge, TRUE);
		if (m_conn == NULL || packet == NULL || size <= 0 || from == NULL || fromlen <= 0)
			return false;

		sockaddr_storage localAddr;
		socklen_t localAddrLen = 0;
		BuildAnyLocalAddress(from->sa_family, localAddr, localAddrLen);
		ngtcp2_path_storage ps;
		ngtcp2_path_storage_init(&ps, reinterpret_cast<const ngtcp2_sockaddr*>(&localAddr), localAddrLen, reinterpret_cast<const ngtcp2_sockaddr*>(from), fromlen, NULL);
		ngtcp2_pkt_info pi;
		memset(&pi, 0, sizeof(pi));
		const int rv = ngtcp2_conn_read_pkt(m_conn, &ps.path, &pi, packet, (size_t)size, GetTimestamp());
		if (rv != 0)
			bHandled = (HandleError(rv, _T("ngtcp2_conn_read_pkt")) == 0);
		else {
			PumpOutput();
			bHandled = true;
		}
	}
	FlushPendingOwnerEvents();
	return bHandled;
}

void CNgTcp2GnuTlsBridge::ProcessTimer()
{
	{
		CSingleLock lock(&m_csBridge, TRUE);
		if (m_conn == NULL)
			return;
		const DWORD nowTick = ::GetTickCount();
		const ngtcp2_tstamp now = (uint64)nowTick * NGTCP2_MILLISECONDS;
		const ngtcp2_tstamp expiry = ngtcp2_conn_get_expiry2(m_conn);
		if (expiry <= now) {
			const int rv = ngtcp2_conn_handle_expiry(m_conn, now);
			if (rv != 0)
				HandleError(rv, _T("ngtcp2_conn_handle_expiry"));
			else {
				PumpOutput();
				MaybeResendHandshakeProbe(nowTick);
			}
		} else {
			PumpOutput();
			MaybeResendHandshakeProbe(nowTick);
		}
		MaybeShrinkWriteBufferIfIdle(nowTick);
	}
	FlushPendingOwnerEvents();
}

int CNgTcp2GnuTlsBridge::SendStreamData(const void* data, int len)
{
	CSingleLock lock(&m_csBridge, TRUE);
	if (data == NULL || len <= 0 || m_conn == NULL || (m_owner != NULL && (m_owner->m_ShutDown & 0x02) != 0)) {
		WSASetLastError(WSAEINVAL);
		return SOCKET_ERROR;
	}
	if (!QueueLocalProofIfNeeded()) {
		WSASetLastError(WSAECONNABORTED);
		return SOCKET_ERROR;
	}
	PromoteWriteBufferIfSafe();
	if (!CanAppendStreamData((size_t)len)) {
		OnWriteBufferBlocked();
		WSASetLastError(WSAEWOULDBLOCK);
		return SOCKET_ERROR;
	}
	if (!m_WriteBuffer.AppendData(data, (size_t)len)) {
		WSASetLastError(WSAENOBUFS);
		return SOCKET_ERROR;
	}
	m_bAppSendBlocked = false;
	m_dwWriteBufferIdleSinceTick = 0;
	PumpOutput();
	if (!IsConnected()) {
		WSASetLastError(WSAEWOULDBLOCK);
		return SOCKET_ERROR;
	}
	return len;
}

void CNgTcp2GnuTlsBridge::Close()
{
	CSingleLock lock(&m_csBridge, TRUE);
	if (m_conn != NULL) {
		ngtcp2_conn_del(m_conn);
		m_conn = NULL;
	}
	if (m_session != NULL) {
		gnutls_deinit(m_session);
		m_session = NULL;
	}
	m_owner = NULL;
	m_LocalStreamId = -1;
	m_RemoteStreamId = -1;
	m_bHandshakeCompleted = false;
	m_bLocalProofQueued = false;
	m_bRemoteProofAccepted = false;
	m_bKeepAliveConfigured = false;
	m_uWriteBaseOffset = 0;
	m_uWriteSubmittedOffset = 0;
	m_uWriteAckedOffset = 0;
	m_uRequestedWriteBufferSize = QUIC_NATT_CONNECTED_WRITE_BUFFER_SIZE;
	m_uWriteBufferBlockCountAtCapacity = 0;
	m_dwWriteBufferIdleSinceTick = 0;
	m_WriteBuffer.SetSize(0, true);
	if (m_WriteBuffer.GetLength() != QUIC_NATT_DEFAULT_WRITE_BUFFER_SIZE)
		m_WriteBuffer.AllocBuffer(QUIC_NATT_DEFAULT_WRITE_BUFFER_SIZE);
	m_ProofBuffer.SetSize(0, true);
	m_PendingAppData.SetSize(0, true);
	m_PendingRemoteStreamData.clear();
	m_uRemoteStreamNextOffset = 0;
	m_bSendReadyPending = false;
	m_bCloseNotifyPending = false;
	m_bAppSendBlocked = false;
	m_nPendingCloseError = 0;
	memset(&m_PeerAddr, 0, sizeof(m_PeerAddr));
	m_PeerAddrLen = 0;
	ResetHandshakeProbe();
}

bool CNgTcp2GnuTlsBridge::HasPeer(const struct sockaddr* peer, socklen_t peerlen) const
{
	if (peer == NULL || peerlen <= 0 || m_PeerAddrLen <= 0 || peer->sa_family != reinterpret_cast<const sockaddr*>(&m_PeerAddr)->sa_family)
		return false;
	if (peer->sa_family == AF_INET && peerlen >= sizeof(sockaddr_in) && m_PeerAddrLen >= sizeof(sockaddr_in)) {
		const sockaddr_in* a = reinterpret_cast<const sockaddr_in*>(peer);
		const sockaddr_in* b = reinterpret_cast<const sockaddr_in*>(&m_PeerAddr);
		return a->sin_port == b->sin_port && a->sin_addr.s_addr == b->sin_addr.s_addr;
	}
	if (peer->sa_family == AF_INET6 && peerlen >= sizeof(sockaddr_in6) && m_PeerAddrLen >= sizeof(sockaddr_in6)) {
		const sockaddr_in6* a = reinterpret_cast<const sockaddr_in6*>(peer);
		const sockaddr_in6* b = reinterpret_cast<const sockaddr_in6*>(&m_PeerAddr);
		return a->sin6_port == b->sin6_port && memcmp(&a->sin6_addr, &b->sin6_addr, sizeof(in6_addr)) == 0;
	}
	return false;
}

bool CNgTcp2GnuTlsBridge::MatchesDestinationConnectionId(const BYTE* packet, int size) const
{
	if (m_conn == NULL || packet == NULL || size <= 0)
		return false;

	const size_t nCidCount = ngtcp2_conn_get_scid2(m_conn, NULL);
	if (nCidCount == 0)
		return false;

	std::vector<ngtcp2_cid> localCids(nCidCount);
	memset(&localCids[0], 0, sizeof(ngtcp2_cid) * nCidCount);
	const size_t nLimit = ngtcp2_conn_get_scid2(m_conn, &localCids[0]);
	if (nLimit == 0)
		return false;

	if ((packet[0] & 0x80) != 0) {
		ngtcp2_version_cid vcid;
		memset(&vcid, 0, sizeof(vcid));
		const int rv = ngtcp2_pkt_decode_version_cid(&vcid, packet, static_cast<size_t>(size), NGTCP2_MIN_INITIAL_DCIDLEN);
		if (rv != 0 && rv != NGTCP2_ERR_VERSION_NEGOTIATION)
			return false;
		if (vcid.dcid == NULL || vcid.dcidlen == 0 || vcid.dcidlen > NGTCP2_MAX_CIDLEN)
			return false;

		const ngtcp2_cid* initialDcid = ngtcp2_conn_get_client_initial_dcid2(m_conn);
		if (initialDcid != NULL && initialDcid->datalen == vcid.dcidlen && memcmp(initialDcid->data, vcid.dcid, vcid.dcidlen) == 0)
			return true;

		for (size_t i = 0; i < nLimit; ++i) {
			if (localCids[i].datalen == vcid.dcidlen && memcmp(localCids[i].data, vcid.dcid, vcid.dcidlen) == 0)
				return true;
		}
		return false;
	}

	for (size_t i = 0; i < nLimit; ++i) {
		if (localCids[i].datalen == 0 || size < 1 + static_cast<int>(localCids[i].datalen))
			continue;
		if (memcmp(packet + 1, localCids[i].data, localCids[i].datalen) == 0)
			return true;
	}
	return false;
}


void CNgTcp2GnuTlsBridge::PumpOutput()
{
	if (m_conn == NULL || m_owner == NULL)
		return;
	if (m_bHandshakeCompleted)
		OpenLocalBidiStream();
	if (m_bHandshakeCompleted && m_LocalStreamId >= 0)
		QueueLocalProofIfNeeded();

	BYTE packet[1200];
	ngtcp2_pkt_info pi;
	ngtcp2_path_storage ps;
	ngtcp2_vec datav;
	const ngtcp2_tstamp ts = GetTimestamp();
	const int nPumpDatagramLimit = GetPumpDatagramLimit();
	bool bWrotePacket = false;
	int loops = 0;
	for (; loops < nPumpDatagramLimit; ++loops) {
		memset(&pi, 0, sizeof(pi));
		ngtcp2_path_storage_zero(&ps);
		ngtcp2_ssize writtenDataLen = 0;
		size_t datavcnt = 0;
		int64_t streamId = -1;
		uint32_t flags = 0;
		if (m_LocalStreamId >= 0 && m_WriteBuffer.GetSize() > 0 && m_uWriteSubmittedOffset >= m_uWriteBaseOffset) {
			const uint64 uWriteBufferEnd = m_uWriteBaseOffset + m_WriteBuffer.GetSize();
			if (m_uWriteSubmittedOffset < uWriteBufferEnd) {
				const size_t uSubmitOffset = (size_t)(m_uWriteSubmittedOffset - m_uWriteBaseOffset);
				datav.base = m_WriteBuffer.GetBuffer() + uSubmitOffset;
				datav.len = m_WriteBuffer.GetSize() - uSubmitOffset;
				datavcnt = 1;
				streamId = m_LocalStreamId;
			} else {
				datav.base = NULL;
				datav.len = 0;
			}
		} else {
			datav.base = NULL;
			datav.len = 0;
		}

		ngtcp2_ssize nwrite = ngtcp2_conn_writev_stream(m_conn, &ps.path, &pi, packet, sizeof(packet), &writtenDataLen, flags, streamId, datavcnt != 0 ? &datav : NULL, datavcnt, ts);
		if (nwrite == NGTCP2_ERR_WRITE_MORE) {
			if (writtenDataLen > 0)
				m_uWriteSubmittedOffset += (uint64)writtenDataLen;
			continue;
		}
		if (nwrite < 0) {
			HandleError((int)nwrite, _T("ngtcp2_conn_writev_stream"));
			return;
		}
		if (nwrite == 0)
			break;
		if (writtenDataLen > 0)
			m_uWriteSubmittedOffset += (uint64)writtenDataLen;

		const sockaddr* remote = ps.path.remote.addr != NULL ? ps.path.remote.addr : reinterpret_cast<const sockaddr*>(&m_PeerAddr);
		const socklen_t remoteLen = ps.path.remote.addrlen != 0 ? ps.path.remote.addrlen : m_PeerAddrLen;
		SendDatagram(packet, (size_t)nwrite, remote, remoteLen);
		bWrotePacket = true;
	}
	if (bWrotePacket)
		ngtcp2_conn_update_pkt_tx_time(m_conn, ts);
	if (m_LocalStreamId >= 0 && m_WriteBuffer.GetSize() == 0)
		QueueSendReadyIfNeeded(false);
}

int CNgTcp2GnuTlsBridge::GetPumpDatagramLimit() const
{
	if (m_conn == NULL)
		return QUIC_NATT_MIN_PUMP_DATAGRAMS;

	const size_t sendQuantum = ngtcp2_conn_get_send_quantum2(m_conn);
	if (sendQuantum == 0)
		return QUIC_NATT_MIN_PUMP_DATAGRAMS;

	const size_t packets = (sendQuantum + 1199) / 1200;
	if (packets <= (size_t)QUIC_NATT_MIN_PUMP_DATAGRAMS)
		return QUIC_NATT_MIN_PUMP_DATAGRAMS;
	if (packets >= (size_t)QUIC_NATT_MAX_PUMP_DATAGRAMS)
		return QUIC_NATT_MAX_PUMP_DATAGRAMS;
	return (int)packets;
}

void CNgTcp2GnuTlsBridge::NotifyConnectedIfReady()
{
	if (m_owner != NULL && IsConnected() && m_bRemoteProofAccepted) {
		if (!m_bKeepAliveConfigured && m_conn != NULL) {
			ngtcp2_conn_set_keep_alive_timeout(m_conn, (ngtcp2_duration)QUIC_NATT_KEEPALIVE_TIMEOUT_MS * NGTCP2_MILLISECONDS);
			m_bKeepAliveConfigured = true;
		}
		const bool bFirstNotify = !m_owner->m_bConnectNotified;
		if (bFirstNotify && thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NAT-T][QUIC] Stream proof accepted; notifying connected, owner=%p"), m_owner);
		m_owner->NotifyConnected();
	}
}

bool CNgTcp2GnuTlsBridge::CanNotifyOwnerSendReady() const
{
	return m_owner != NULL && IsConnected() && m_bRemoteProofAccepted && (m_owner->m_ShutDown & 0x02) == 0;
}

bool CNgTcp2GnuTlsBridge::HasWriteBufferRoom() const
{
	return m_WriteBuffer.GetSize() < m_WriteBuffer.GetLength();
}

void CNgTcp2GnuTlsBridge::QueueSendReadyIfNeeded(bool bWriteRoomOpened)
{
	if (!CanNotifyOwnerSendReady())
		return;
	if (!m_bAppSendBlocked && !bWriteRoomOpened)
		return;

	m_bAppSendBlocked = false;
	m_bSendReadyPending = true;
}

void CNgTcp2GnuTlsBridge::FlushPendingOwnerEvents()
{
	NotifyConnectedIfReady();
	if (m_bSendReadyPending && m_owner != NULL) {
		m_bSendReadyPending = false;
		m_owner->NotifySendReady();
	}
	if (m_bCloseNotifyPending && m_owner != NULL) {
		const int nError = m_nPendingCloseError;
		m_bCloseNotifyPending = false;
		m_nPendingCloseError = 0;
		m_owner->NotifyClosed(nError);
		return;
	}
	// Deliver app data last. Packet handlers may close the owning socket.
	FlushPendingAppData();
}

void CNgTcp2GnuTlsBridge::FlushPendingAppData()
{
	if (m_owner == NULL || m_PendingAppData.GetSize() == 0)
		return;

	std::vector<BYTE> data(m_PendingAppData.GetSize());
	memcpy(&data[0], m_PendingAppData.GetBuffer(), data.size());
	m_PendingAppData.SetSize(0, true);
	m_owner->AppendReceivedData(&data[0], data.size());
}

void CNgTcp2GnuTlsBridge::QueueCloseNotification(int nErrorCode)
{
	m_bCloseNotifyPending = true;
	m_nPendingCloseError = nErrorCode;
}


int CNgTcp2GnuTlsBridge::HandleError(int rv, LPCTSTR context)
{
	if (rv == NGTCP2_ERR_STREAM_DATA_BLOCKED || rv == NGTCP2_ERR_WRITE_MORE)
		return 0;
	if (thePrefs.GetLogNatTraversalEvents())
		DebugLogWarning(_T("[NAT-T][QUIC] %s failed with %d (%hs)"), context, rv, ngtcp2_strerror(rv));
	QueueCloseNotification(WSAECONNABORTED);
	return rv;
}

bool CNgTcp2GnuTlsBridge::QueueLocalProofIfNeeded()
{
	if (m_bLocalProofQueued)
		return true;
	if (m_owner == NULL || !CanAppendStreamData(EMULEAI_QUIC_NATT_PROOF_LEN))
		return false;
	BYTE proof[EMULEAI_QUIC_NATT_PROOF_LEN];
	if (!m_owner->BuildLocalProof(proof, sizeof(proof)))
		return false;
	m_bLocalProofQueued = m_WriteBuffer.AppendData(proof, sizeof(proof));
	return m_bLocalProofQueued;
}

bool CNgTcp2GnuTlsBridge::CanAppendStreamData(size_t len) const
{
	if (len == 0)
		return true;
	const bool bHasSubmittedData = m_uWriteSubmittedOffset > m_uWriteBaseOffset;
	if (!bHasSubmittedData)
		return true;
	return m_WriteBuffer.GetSize() + len <= m_WriteBuffer.GetLength();
}

bool CNgTcp2GnuTlsBridge::PromoteWriteBufferIfSafe()
{
	if (m_uRequestedWriteBufferSize < QUIC_NATT_CONNECTED_WRITE_BUFFER_SIZE)
		m_uRequestedWriteBufferSize = QUIC_NATT_CONNECTED_WRITE_BUFFER_SIZE;
	if (m_uRequestedWriteBufferSize > QUIC_NATT_MAX_WRITE_BUFFER_SIZE)
		m_uRequestedWriteBufferSize = QUIC_NATT_MAX_WRITE_BUFFER_SIZE;
	if (m_WriteBuffer.GetLength() >= m_uRequestedWriteBufferSize)
		return true;
	if (!IsConnected() || !m_bRemoteProofAccepted)
		return false;
	if (m_WriteBuffer.GetSize() != 0 || m_uWriteSubmittedOffset != m_uWriteBaseOffset || m_uWriteAckedOffset != m_uWriteBaseOffset)
		return false;

	m_WriteBuffer.AllocBuffer(m_uRequestedWriteBufferSize);
	m_uWriteBufferBlockCountAtCapacity = 0;
	return true;
}

size_t CNgTcp2GnuTlsBridge::GetNextWriteBufferCapacity() const
{
	const size_t uCurrent = m_WriteBuffer.GetLength();
	if (uCurrent < QUIC_NATT_CONNECTED_WRITE_BUFFER_SIZE)
		return QUIC_NATT_CONNECTED_WRITE_BUFFER_SIZE;
	if (uCurrent < QUIC_NATT_ADAPTIVE_WRITE_BUFFER_SIZE)
		return QUIC_NATT_ADAPTIVE_WRITE_BUFFER_SIZE;
	if (uCurrent < QUIC_NATT_MAX_WRITE_BUFFER_SIZE)
		return QUIC_NATT_MAX_WRITE_BUFFER_SIZE;
	return uCurrent;
}

void CNgTcp2GnuTlsBridge::OnWriteBufferBlocked()
{
	m_bAppSendBlocked = true;

	if (m_WriteBuffer.GetLength() >= QUIC_NATT_MAX_WRITE_BUFFER_SIZE)
		return;
	if (++m_uWriteBufferBlockCountAtCapacity < QUIC_NATT_WRITE_BUFFER_PROMOTION_BLOCKS)
		return;

	const size_t uNextCapacity = GetNextWriteBufferCapacity();
	if (uNextCapacity > m_uRequestedWriteBufferSize)
		m_uRequestedWriteBufferSize = uNextCapacity;
	PromoteWriteBufferIfSafe();
}

void CNgTcp2GnuTlsBridge::UpdateWriteBufferIdleState(DWORD nowTick)
{
	if (m_WriteBuffer.GetSize() == 0 && m_uWriteSubmittedOffset == m_uWriteBaseOffset && m_uWriteAckedOffset == m_uWriteBaseOffset) {
		if (m_dwWriteBufferIdleSinceTick == 0)
			m_dwWriteBufferIdleSinceTick = nowTick;
	} else {
		m_dwWriteBufferIdleSinceTick = 0;
	}
}

void CNgTcp2GnuTlsBridge::MaybeShrinkWriteBufferIfIdle(DWORD nowTick)
{
	if (m_WriteBuffer.GetLength() <= QUIC_NATT_DEFAULT_WRITE_BUFFER_SIZE || !IsConnected() || !m_bRemoteProofAccepted || m_bCloseNotifyPending)
		return;

	UpdateWriteBufferIdleState(nowTick);
	if (m_dwWriteBufferIdleSinceTick == 0 || nowTick - m_dwWriteBufferIdleSinceTick < QUIC_NATT_WRITE_BUFFER_IDLE_SHRINK_MS)
		return;

	m_WriteBuffer.AllocBuffer(QUIC_NATT_DEFAULT_WRITE_BUFFER_SIZE);
	m_uRequestedWriteBufferSize = QUIC_NATT_CONNECTED_WRITE_BUFFER_SIZE;
	m_uWriteBufferBlockCountAtCapacity = 0;
	m_dwWriteBufferIdleSinceTick = 0;
}

void CNgTcp2GnuTlsBridge::TrimAckedStreamData()
{
	if (m_uWriteAckedOffset < m_uWriteSubmittedOffset || m_uWriteAckedOffset <= m_uWriteBaseOffset)
		return;
	const uint64 uTrim64 = m_uWriteAckedOffset - m_uWriteBaseOffset;
	if (uTrim64 > m_WriteBuffer.GetSize())
		return;
	const size_t uTrim = (size_t)uTrim64;
	if (uTrim > 0)
		m_WriteBuffer.ShiftData(uTrim);
	m_uWriteBaseOffset += uTrim64;
	m_uWriteSubmittedOffset = m_uWriteBaseOffset;
}

bool CNgTcp2GnuTlsBridge::ConsumeRemoteProofIfReady()
{
	if (m_bRemoteProofAccepted)
		return true;
	if (m_owner == NULL || m_ProofBuffer.GetSize() < EMULEAI_QUIC_NATT_PROOF_LEN)
		return false;
	m_bRemoteProofAccepted = m_owner->ValidateRemoteProof(m_ProofBuffer.GetBuffer(), m_ProofBuffer.GetSize());
	m_ProofBuffer.SetSize(0, true);
	if (!m_bRemoteProofAccepted)
		QueueCloseNotification(WSAECONNRESET);
	return m_bRemoteProofAccepted;
}

void CNgTcp2GnuTlsBridge::OnStreamData(int64_t streamId, uint64 offset, const BYTE* data, size_t len)
{
	if (data == NULL || len == 0 || m_owner == NULL)
		return;
	if (m_RemoteStreamId < 0)
		m_RemoteStreamId = streamId;
	if (streamId != m_RemoteStreamId)
		return;

	QueueRemoteStreamData(offset, data, len);
	DrainRemoteStreamData();
}

void CNgTcp2GnuTlsBridge::QueueRemoteStreamData(uint64 offset, const BYTE* data, size_t len)
{
	if (data == NULL || len == 0)
		return;

	uint64 end = offset + (uint64)len;
	if (end <= m_uRemoteStreamNextOffset)
		return;

	if (offset < m_uRemoteStreamNextOffset) {
		const size_t skip = (size_t)(m_uRemoteStreamNextOffset - offset);
		data += skip;
		len -= skip;
		offset = m_uRemoteStreamNextOffset;
		end = offset + (uint64)len;
	}

	std::map<uint64, std::vector<BYTE> >::iterator itNext = m_PendingRemoteStreamData.lower_bound(offset);
	if (itNext != m_PendingRemoteStreamData.begin()) {
		std::map<uint64, std::vector<BYTE> >::iterator itPrev = itNext;
		--itPrev;
		const uint64 prevEnd = itPrev->first + (uint64)itPrev->second.size();
		if (prevEnd >= end)
			return;
		if (prevEnd > offset) {
			const size_t skip = (size_t)(prevEnd - offset);
			data += skip;
			len -= skip;
			offset = prevEnd;
		}
	}

	end = offset + (uint64)len;
	while (itNext != m_PendingRemoteStreamData.end() && itNext->first < end) {
		const uint64 nextEnd = itNext->first + (uint64)itNext->second.size();
		if (nextEnd <= end) {
			std::map<uint64, std::vector<BYTE> >::iterator itErase = itNext++;
			m_PendingRemoteStreamData.erase(itErase);
			continue;
		}
		len = (size_t)(itNext->first - offset);
		end = offset + (uint64)len;
		break;
	}

	if (len == 0)
		return;

	std::vector<BYTE> chunk(len);
	memcpy(&chunk[0], data, len);
	m_PendingRemoteStreamData[offset].swap(chunk);
}

void CNgTcp2GnuTlsBridge::DrainRemoteStreamData()
{
	while (!m_PendingRemoteStreamData.empty()) {
		std::map<uint64, std::vector<BYTE> >::iterator it = m_PendingRemoteStreamData.begin();
		if (it->first > m_uRemoteStreamNextOffset)
			break;

		std::vector<BYTE> chunk;
		if (it->first < m_uRemoteStreamNextOffset) {
			const size_t skip = (size_t)(m_uRemoteStreamNextOffset - it->first);
			if (skip >= it->second.size()) {
				m_PendingRemoteStreamData.erase(it);
				continue;
			}
			chunk.assign(it->second.begin() + skip, it->second.end());
		} else {
			if (it->first != m_uRemoteStreamNextOffset)
				break;
			chunk.swap(it->second);
		}
		m_PendingRemoteStreamData.erase(it);
		if (!chunk.empty()) {
			const size_t uConsumed = chunk.size();
			ConsumeOrderedStreamData(&chunk[0], uConsumed);
			m_uRemoteStreamNextOffset += (uint64)uConsumed;
			ExtendReceiveFlowControl(m_RemoteStreamId, uConsumed);
		}
	}
}

void CNgTcp2GnuTlsBridge::ExtendReceiveFlowControl(int64_t streamId, size_t len)
{
	if (m_conn == NULL || streamId < 0 || len == 0)
		return;

	const uint64_t datalen = (uint64_t)len;
	const int rv = ngtcp2_conn_extend_max_stream_offset(m_conn, streamId, datalen);
	if (rv != 0) {
		HandleError(rv, _T("ngtcp2_conn_extend_max_stream_offset"));
		return;
	}
	ngtcp2_conn_extend_max_offset(m_conn, datalen);
}

void CNgTcp2GnuTlsBridge::ConsumeOrderedStreamData(const BYTE* data, size_t len)
{
	if (data == NULL || len == 0)
		return;

	size_t offset = 0;
	if (!m_bRemoteProofAccepted) {
		const size_t need = EMULEAI_QUIC_NATT_PROOF_LEN - m_ProofBuffer.GetSize();
		const size_t copy = need < len ? need : len;
		m_ProofBuffer.AppendData(data, copy);
		offset += copy;
		if (!ConsumeRemoteProofIfReady())
			return;
		QueueLocalProofIfNeeded();
	}
	if (offset < len)
		m_PendingAppData.AppendData(data + offset, len - offset);
}

void CNgTcp2GnuTlsBridge::OnStreamWritable()
{
	QueueSendReadyIfNeeded(false);
}

void CNgTcp2GnuTlsBridge::OnHandshakeCompleted()
{
	m_bHandshakeCompleted = true;
	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NAT-T][QUIC] TLS handshake completed, role=%d owner=%p"), (int)m_role, m_owner);
	ResetHandshakeProbe();
	OpenLocalBidiStream();
	QueueLocalProofIfNeeded();
}

void CNgTcp2GnuTlsBridge::OnStreamClosed(int64_t streamId)
{
	if (streamId == m_LocalStreamId || streamId == m_RemoteStreamId)
		QueueCloseNotification(0);
}

void CNgTcp2GnuTlsBridge::OnAckedStreamData(uint64 offset, uint64 datalen)
{
	const bool bHadNoWriteRoom = !HasWriteBufferRoom();
	const uint64 uAckEnd = offset + datalen;
	if (offset <= m_uWriteAckedOffset && uAckEnd > m_uWriteAckedOffset) {
		m_uWriteAckedOffset = uAckEnd;
	}
	TrimAckedStreamData();
	UpdateWriteBufferIdleState(::GetTickCount());
	PromoteWriteBufferIfSafe();
	QueueSendReadyIfNeeded(bHadNoWriteRoom && HasWriteBufferRoom());
}

bool CNgTcp2GnuTlsBridge::OpenLocalBidiStream()
{
	if (m_conn == NULL || m_LocalStreamId >= 0)
		return m_LocalStreamId >= 0;
	int64_t streamId = -1;
	const int rv = ngtcp2_conn_open_bidi_stream(m_conn, &streamId, NULL);
	if (rv != 0)
		return false;
	m_LocalStreamId = streamId;
	QueueLocalProofIfNeeded();
	return true;
}

void CNgTcp2GnuTlsBridge::ResetHandshakeProbe()
{
	m_LastHandshakePacket.SetSize(0, true);
	memset(&m_LastHandshakePeerAddr, 0, sizeof(m_LastHandshakePeerAddr));
	m_LastHandshakePeerAddrLen = 0;
	m_dwNextHandshakeProbeTick = 0;
	m_byHandshakeProbeCount = 0;
}

void CNgTcp2GnuTlsBridge::RememberHandshakeProbe(const BYTE* data, size_t len, const struct sockaddr* peer, socklen_t peerlen)
{
	if (data == NULL || len == 0 || peer == NULL || peerlen <= 0 || (data[0] & 0x80) == 0 || m_bHandshakeCompleted || peerlen > sizeof(m_LastHandshakePeerAddr))
		return;

	m_LastHandshakePacket.SetSize(0, true);
	if (!m_LastHandshakePacket.AppendData(data, len))
		return;
	memset(&m_LastHandshakePeerAddr, 0, sizeof(m_LastHandshakePeerAddr));
	memcpy(&m_LastHandshakePeerAddr, peer, peerlen);
	m_LastHandshakePeerAddrLen = peerlen;
	m_byHandshakeProbeCount = QUIC_NATT_HANDSHAKE_PROBE_MAX;
	m_dwNextHandshakeProbeTick = ::GetTickCount() + QUIC_NATT_HANDSHAKE_PROBE_INTERVAL_MS;

	if (thePrefs.GetLogNatTraversalEvents()) {
		CAddress IP;
		uint16 nPort = 0;
		IP.FromSA(peer, peerlen, &nPort);
		DebugLog(_T("[NAT-T][QUIC] Handshake datagram queued, role=%d size=%u peer=%s:%u"), (int)m_role, (UINT)len, (LPCTSTR)ipstr(IP), (UINT)nPort);
	}
}

void CNgTcp2GnuTlsBridge::MaybeResendHandshakeProbe(DWORD now)
{
	if (m_bHandshakeCompleted || m_byHandshakeProbeCount == 0 || m_LastHandshakePacket.GetSize() == 0 || m_LastHandshakePeerAddrLen <= 0)
		return;
	if ((int)(now - m_dwNextHandshakeProbeTick) < 0)
		return;

	--m_byHandshakeProbeCount;
	m_dwNextHandshakeProbeTick = now + QUIC_NATT_HANDSHAKE_PROBE_INTERVAL_MS;
	SendDatagram(m_LastHandshakePacket.GetBuffer(), m_LastHandshakePacket.GetSize(), reinterpret_cast<const sockaddr*>(&m_LastHandshakePeerAddr), m_LastHandshakePeerAddrLen, false);
	if (thePrefs.GetLogNatTraversalEvents()) {
		CAddress IP;
		uint16 nPort = 0;
		IP.FromSA(reinterpret_cast<const sockaddr*>(&m_LastHandshakePeerAddr), m_LastHandshakePeerAddrLen, &nPort);
		DebugLog(_T("[NAT-T][QUIC] Handshake probe resent, role=%d remaining=%u peer=%s:%u"), (int)m_role, (UINT)m_byHandshakeProbeCount, (LPCTSTR)ipstr(IP), (UINT)nPort);
	}
}

void CNgTcp2GnuTlsBridge::SendDatagram(const BYTE* data, size_t len, const struct sockaddr* peer, socklen_t peerlen, bool bRememberHandshake)
{
	if (data == NULL || len == 0 || peer == NULL || peerlen <= 0 || theApp.clientudp == NULL)
		return;
	if (bRememberHandshake)
		RememberHandshakeProbe(data, len, peer, peerlen);
	theApp.clientudp->SendQuicNatPacket(data, len, peer, peerlen);
}

ngtcp2_conn* CNgTcp2GnuTlsBridge::GetNgTcp2Conn(ngtcp2_crypto_conn_ref* connRef)
{
	if (connRef == NULL)
		return NULL;
	CNgTcp2GnuTlsBridge* bridge = reinterpret_cast<CNgTcp2GnuTlsBridge*>(connRef->user_data);
	return bridge != NULL ? bridge->m_conn : NULL;
}

int CNgTcp2GnuTlsBridge::OnHandshakeCompletedCb(ngtcp2_conn* conn, void* userData)
{
	UNREFERENCED_PARAMETER(conn);
	CNgTcp2GnuTlsBridge* bridge = reinterpret_cast<CNgTcp2GnuTlsBridge*>(userData);
	if (bridge != NULL)
		bridge->OnHandshakeCompleted();
	return 0;
}

int CNgTcp2GnuTlsBridge::OnRecvStreamDataCb(ngtcp2_conn* conn, uint32_t flags, int64_t streamId, uint64_t offset, const uint8_t* data, size_t datalen, void* userData, void* streamUserData)
{
	UNREFERENCED_PARAMETER(conn);
	UNREFERENCED_PARAMETER(flags);
	UNREFERENCED_PARAMETER(streamUserData);
	CNgTcp2GnuTlsBridge* bridge = reinterpret_cast<CNgTcp2GnuTlsBridge*>(userData);
	if (bridge != NULL)
		bridge->OnStreamData(streamId, offset, data, datalen);
	return 0;
}

int CNgTcp2GnuTlsBridge::OnStreamOpenCb(ngtcp2_conn* conn, int64_t streamId, void* userData)
{
	UNREFERENCED_PARAMETER(conn);
	CNgTcp2GnuTlsBridge* bridge = reinterpret_cast<CNgTcp2GnuTlsBridge*>(userData);
	if (bridge != NULL && bridge->m_RemoteStreamId < 0)
		bridge->m_RemoteStreamId = streamId;
	return 0;
}

int CNgTcp2GnuTlsBridge::OnStreamCloseCb(ngtcp2_conn* conn, uint32_t flags, int64_t streamId, uint64_t appErrorCode, void* userData, void* streamUserData)
{
	UNREFERENCED_PARAMETER(conn);
	UNREFERENCED_PARAMETER(flags);
	UNREFERENCED_PARAMETER(appErrorCode);
	UNREFERENCED_PARAMETER(streamUserData);
	CNgTcp2GnuTlsBridge* bridge = reinterpret_cast<CNgTcp2GnuTlsBridge*>(userData);
	if (bridge != NULL)
		bridge->OnStreamClosed(streamId);
	return 0;
}

int CNgTcp2GnuTlsBridge::OnAckedStreamDataCb(ngtcp2_conn* conn, int64_t streamId, uint64_t offset, uint64_t datalen, void* userData, void* streamUserData)
{
	UNREFERENCED_PARAMETER(conn);
	UNREFERENCED_PARAMETER(streamUserData);
	CNgTcp2GnuTlsBridge* bridge = reinterpret_cast<CNgTcp2GnuTlsBridge*>(userData);
	if (bridge != NULL && streamId == bridge->m_LocalStreamId)
		bridge->OnAckedStreamData(offset, datalen);
	return 0;
}

int CNgTcp2GnuTlsBridge::OnExtendMaxLocalStreamsBidiCb(ngtcp2_conn* conn, uint64_t maxStreams, void* userData)
{
	UNREFERENCED_PARAMETER(conn);
	UNREFERENCED_PARAMETER(maxStreams);
	CNgTcp2GnuTlsBridge* bridge = reinterpret_cast<CNgTcp2GnuTlsBridge*>(userData);
	if (bridge != NULL)
		bridge->OpenLocalBidiStream();
	return 0;
}

int CNgTcp2GnuTlsBridge::OnExtendMaxStreamDataCb(ngtcp2_conn* conn, int64_t streamId, uint64_t maxData, void* userData, void* streamUserData)
{
	UNREFERENCED_PARAMETER(conn);
	UNREFERENCED_PARAMETER(maxData);
	UNREFERENCED_PARAMETER(streamUserData);
	CNgTcp2GnuTlsBridge* bridge = reinterpret_cast<CNgTcp2GnuTlsBridge*>(userData);
	if (bridge != NULL && streamId == bridge->m_LocalStreamId)
		bridge->OnStreamWritable();
	return 0;
}

void CNgTcp2GnuTlsBridge::OnRandCb(uint8_t* dest, size_t destlen, const ngtcp2_rand_ctx* randCtx)
{
	UNREFERENCED_PARAMETER(randCtx);
	FillRandom(dest, destlen);
}

int CNgTcp2GnuTlsBridge::OnGetNewConnectionIdCb(ngtcp2_conn* conn, ngtcp2_cid* cid, ngtcp2_stateless_reset_token* token, size_t cidlen, void* userData)
{
	UNREFERENCED_PARAMETER(conn);
	UNREFERENCED_PARAMETER(userData);
	if (cid == NULL || token == NULL || cidlen > NGTCP2_MAX_CIDLEN)
		return NGTCP2_ERR_CALLBACK_FAILURE;
	FillRandom(cid->data, cidlen);
	cid->datalen = cidlen;
	FillRandom(token->data, sizeof(token->data));
	return 0;
}
