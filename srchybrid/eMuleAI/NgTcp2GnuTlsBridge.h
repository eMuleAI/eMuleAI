//This file is part of eMule AI
//Copyright (C)2026 eMule AI
//
#pragma once

#include "QuicNatConfig.h"
#include "Buffer.h"
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <gnutls/gnutls.h>
#include <map>
#include <vector>

class CQuicNatSocket;
struct sockaddr;

enum EQuicNatRole
{
	QuicNatRole_Client = 0,
	QuicNatRole_Server
};

class CNgTcp2GnuTlsBridge
{
public:
	CNgTcp2GnuTlsBridge();
	~CNgTcp2GnuTlsBridge();

	static bool IsQuicRuntimeAvailable();
	static void ProcessTimers();

	bool StartClient(CQuicNatSocket* owner, const struct sockaddr* peer, socklen_t peerlen);
	bool StartServer(CQuicNatSocket* owner, const BYTE* firstPacket, int firstPacketSize, const struct sockaddr* peer, socklen_t peerlen);
	bool ProcessPacket(const BYTE* packet, int size, const struct sockaddr* from, socklen_t fromlen);
	void ProcessTimer();
	int SendStreamData(const void* data, int len);
	void Close();
	bool IsStarted() const { return m_conn != NULL; }
	bool IsConnected() const { return m_bHandshakeCompleted && m_LocalStreamId >= 0; }
	bool HasPeer(const struct sockaddr* peer, socklen_t peerlen) const;
	bool MatchesDestinationConnectionId(const BYTE* packet, int size) const;

private:
	static bool EnsureRuntimeInitialized();
	static void CleanupRuntime();
	static void FillRandom(BYTE* data, size_t len);
	static uint64 GetTimestamp();
	static int InitGnuTlsCredentials();

	bool InitGnuTlsSession(EQuicNatRole role);
	bool InitNgTcp2Connection(EQuicNatRole role, const BYTE* firstPacket, int firstPacketSize, const struct sockaddr* peer, socklen_t peerlen);
	void PumpOutput();
	int GetPumpDatagramLimit() const;
	void NotifyConnectedIfReady();
	bool CanNotifyOwnerSendReady() const;
	bool HasWriteBufferRoom() const;
	void QueueSendReadyIfNeeded(bool bWriteRoomOpened);
	int HandleError(int rv, LPCTSTR context);
	bool QueueLocalProofIfNeeded();
	bool ConsumeRemoteProofIfReady();
	bool CanAppendStreamData(size_t len) const;
	bool PromoteWriteBufferIfSafe();
	size_t GetNextWriteBufferCapacity() const;
	void OnWriteBufferBlocked();
	void UpdateWriteBufferIdleState(DWORD nowTick);
	void MaybeShrinkWriteBufferIfIdle(DWORD nowTick);
	void TrimAckedStreamData();
	void OnStreamData(int64_t streamId, uint64 offset, const BYTE* data, size_t len);
	void QueueRemoteStreamData(uint64 offset, const BYTE* data, size_t len);
	void DrainRemoteStreamData();
	void ConsumeOrderedStreamData(const BYTE* data, size_t len);
	void ExtendReceiveFlowControl(int64_t streamId, size_t len);
	void OnStreamWritable();
	void FlushPendingOwnerEvents();
	void FlushPendingAppData();
	void QueueCloseNotification(int nErrorCode);
	void OnHandshakeCompleted();
	void OnStreamClosed(int64_t streamId);
	void OnAckedStreamData(uint64 offset, uint64 datalen);
	bool OpenLocalBidiStream();
	void ResetHandshakeProbe();
	void RememberHandshakeProbe(const BYTE* data, size_t len, const struct sockaddr* peer, socklen_t peerlen);
	void MaybeResendHandshakeProbe(DWORD now);
	void SendDatagram(const BYTE* data, size_t len, const struct sockaddr* peer, socklen_t peerlen, bool bRememberHandshake = true);

	static ngtcp2_conn* GetNgTcp2Conn(ngtcp2_crypto_conn_ref* connRef);
	static int OnHandshakeCompletedCb(ngtcp2_conn* conn, void* userData);
	static int OnRecvStreamDataCb(ngtcp2_conn* conn, uint32_t flags, int64_t streamId, uint64_t offset, const uint8_t* data, size_t datalen, void* userData, void* streamUserData);
	static int OnStreamOpenCb(ngtcp2_conn* conn, int64_t streamId, void* userData);
	static int OnStreamCloseCb(ngtcp2_conn* conn, uint32_t flags, int64_t streamId, uint64_t appErrorCode, void* userData, void* streamUserData);
	static int OnAckedStreamDataCb(ngtcp2_conn* conn, int64_t streamId, uint64_t offset, uint64_t datalen, void* userData, void* streamUserData);
	static int OnExtendMaxLocalStreamsBidiCb(ngtcp2_conn* conn, uint64_t maxStreams, void* userData);
	static int OnExtendMaxStreamDataCb(ngtcp2_conn* conn, int64_t streamId, uint64_t maxData, void* userData, void* streamUserData);
	static void OnRandCb(uint8_t* dest, size_t destlen, const ngtcp2_rand_ctx* randCtx);
	static int OnGetNewConnectionIdCb(ngtcp2_conn* conn, ngtcp2_cid* cid, ngtcp2_stateless_reset_token* token, size_t cidlen, void* userData);

	CQuicNatSocket* m_owner;
	ngtcp2_conn* m_conn;
	ngtcp2_crypto_conn_ref m_connRef;
	gnutls_session_t m_session;
	EQuicNatRole m_role;
	int64_t m_LocalStreamId;
	int64_t m_RemoteStreamId;
	bool m_bHandshakeCompleted;
	bool m_bLocalProofQueued;
	bool m_bRemoteProofAccepted;
	bool m_bKeepAliveConfigured;
	uint64 m_uWriteBaseOffset;
	uint64 m_uWriteSubmittedOffset;
	uint64 m_uWriteAckedOffset;
	size_t m_uRequestedWriteBufferSize;
	uint32 m_uWriteBufferBlockCountAtCapacity;
	DWORD m_dwWriteBufferIdleSinceTick;
	CBuffer m_WriteBuffer;
	CBuffer m_ProofBuffer;
	CBuffer m_PendingAppData;
	uint64 m_uRemoteStreamNextOffset;
	std::map<uint64, std::vector<BYTE> > m_PendingRemoteStreamData;
	CBuffer m_LastHandshakePacket;
	sockaddr_storage m_LastHandshakePeerAddr;
	socklen_t m_LastHandshakePeerAddrLen;
	DWORD m_dwNextHandshakeProbeTick;
	uint8 m_byHandshakeProbeCount;
	bool m_bSendReadyPending;
	bool m_bCloseNotifyPending;
	bool m_bAppSendBlocked;
	int m_nPendingCloseError;
	sockaddr_storage m_PeerAddr;
	socklen_t m_PeerAddrLen;
	DWORD m_dwLastPumpTick;
	mutable CCriticalSection m_csBridge;
};
