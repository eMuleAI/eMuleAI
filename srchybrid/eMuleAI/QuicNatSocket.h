//This file is part of eMule AI
//Copyright (C)2026 eMule AI
//
#pragma once

#include "../AsyncSocketExLayer.h"
#include "Buffer.h"

class CUpDownClient;
class CNgTcp2GnuTlsBridge;

class CQuicNatSocket : public CAsyncSocketExLayer
{
	friend class CNgTcp2GnuTlsBridge;

public:
	CQuicNatSocket();
	virtual ~CQuicNatSocket();

	static bool IsRuntimeAvailable();
	static void ProcessAllQuicTimers();
	static bool ProcessQuicPacket(const BYTE* packet, int size, const struct sockaddr* from, socklen_t fromlen);
	static CString GetRoutingDiagnostics(const BYTE* packet, int size, const struct sockaddr* from, socklen_t fromlen);

	void UnregisterFromGlobalSet();
	void ExpectPeer(const struct sockaddr* to, socklen_t tolen);

	virtual void OnReceive(int nErrorCode) override;
	virtual void OnSend(int nErrorCode) override { TriggerEvent(FD_WRITE, nErrorCode, TRUE); }
	virtual void OnConnect(int nErrorCode) override { TriggerEvent(FD_CONNECT, nErrorCode, TRUE); }
	virtual void OnAccept(int nErrorCode) override { ASSERT(0); UNREFERENCED_PARAMETER(nErrorCode); }
	virtual void OnClose(int nErrorCode) override { TriggerEvent(FD_CLOSE, nErrorCode, TRUE); }

	virtual bool Create(UINT nSocketPort = 0, int nSocketType = SOCK_STREAM, long lEvent = FD_DEFAULT, const CString& sSocketAddress = CString(), ADDRESS_FAMILY nFamily = AF_INET, bool reusable = false) override;
	virtual BOOL Connect(const LPSOCKADDR lpSockAddr, int nSockAddrLen) override;
	virtual void Close() override;
	virtual BOOL GetPeerName(SOCKADDR* lpSockAddr, int* lpSockAddrLen) override;
	virtual int Receive(void* lpBuf, int nBufLen, int nFlags = 0) override;
	virtual int Send(const void* lpBuf, int nBufLen, int nFlags = 0) override;
	virtual BOOL ShutDown(int nHow = sends) override;
	virtual BOOL GetSockOpt(int nOptionName, void* lpOptionValue, int* lpOptionLen);
	virtual BOOL SetSockOpt(int nOptionName, const void* lpOptionValue, int nOptionLen);
	virtual bool IsQuicNatLayer() const override { return true; }

	AsyncSocketExState GetState() const { return GetLayerState(); }
	CAsyncSocketEx* GetOwnerSocket() const { return m_pOwnerSocket; }
	CUpDownClient* GetOwnerClient() const { return m_pOwnerClient; }
	void SetOwnerClient(CUpDownClient* client) { m_pOwnerClient = client; }

protected:
	void ResolveOwnerClient();
	void AppendReceivedData(const BYTE* data, size_t len);
	void NotifyConnected();
	void NotifySendReady();
	void NotifyClosed(int nErrorCode);
	bool BuildLocalProof(BYTE* proof, size_t len) const;
	bool ValidateRemoteProof(const BYTE* proof, size_t len) const;
	bool HasValidRemoteHash() const;

	CBuffer	m_ReadBuffer;
	mutable CCriticalSection m_csReadBuffer;
	sockaddr_storage m_PeerAddr;
	socklen_t m_PeerAddrLen;
	uint8	m_ShutDown;
	bool	m_bConnectNotified;
	CUpDownClient* m_pOwnerClient;
	CNgTcp2GnuTlsBridge* m_pQuic;
};
