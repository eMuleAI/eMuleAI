//This file is part of eMule AI
//Copyright (C)2013 David Xanatos ( XanatosDavid (a) gmail.com / http://NeoLoader.to )
//Copyright (C)2026 eMule AI
//

#pragma once

#include "../AsyncSocketExLayer.h"
#include "Buffer.h"
#include "../../libutp/include/libutp/utp.h"

class CUpDownClient;

class CUtpSocket : public CAsyncSocketExLayer
{
  public:
	CUtpSocket();
	virtual ~CUtpSocket();
	void UnregisterFromGlobalSet();
	static void Process();
	static void EnsureCallbacks(utp_context* ctx); // Ensure libutp callbacks are configured on the given context once.
	static void OnContextDestroyed(utp_context* ctx); // Notify that a context is being destroyed to clear internal tracking.
	static CCriticalSection& GetRuntimeLock();
	bool ProcessUtpPacket(const byte *data, size_t len, const struct sockaddr *from, socklen_t fromlen);
	// Hint an expected peer endpoint for inbound accept matching (owner, addr, TTL handled internally)
	void ExpectPeer(const struct sockaddr* to, socklen_t tolen);

	//Notification event handlers
	/*
	!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	!!! Note: we must ensure the layer wont be deleted when triggerign an event !!!
	!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	*/
	virtual void OnReceive(int nErrorCode) override;  // Override to bypass FD_READ check
	virtual void OnSend(int nErrorCode) { TriggerEvent(FD_WRITE, nErrorCode, TRUE); }
	virtual void OnConnect(int nErrorCode) { TriggerEvent(FD_CONNECT, nErrorCode, TRUE); }
	/**/virtual void OnAccept(int nErrorCode) { ASSERT(0); /*TriggerEvent(FD_ACCEPT, nErrorCode, TRUE);*/ } // shell never be called
	virtual void OnClose(int nErrorCode) { TriggerEvent(FD_CLOSE, nErrorCode, TRUE); }

	//Operations
	virtual BOOL Create(UINT nSocketPort = 0, int nSocketType = SOCK_STREAM, long lEvent = FD_READ | FD_WRITE | FD_OOB | FD_ACCEPT | FD_CONNECT | FD_CLOSE, LPCSTR lpszSocketAddress = NULL);
	/**/virtual BOOL Connect(LPCSTR /*lpszHostAddress*/, UINT /*nHostPort*/) { ASSERT(0); return FALSE; } // shell never be called
	virtual BOOL Connect(const LPSOCKADDR lpSockAddr, int nSockAddrLen) override;
	/**/virtual BOOL Listen(int /*nConnectionBacklog*/) { ASSERT(0); return FALSE; } // shell never be called
	/**/virtual BOOL Accept(CAsyncSocketEx& /*rConnectedSocket*/, SOCKADDR* /*lpSockAddr*/ = NULL, int* /*lpSockAddrLen*/ = NULL) { ASSERT(0); return FALSE; }	// shell never be called
	virtual void Close();
	virtual BOOL GetPeerName(SOCKADDR* lpSockAddr, int* lpSockAddrLen);
#ifdef _AFX
	/**/virtual bool GetPeerName(CString& /*rPeerAddress*/, UINT& /*rPeerPort*/) { ASSERT(0); return FALSE; } // not implemented
#endif
	CAsyncSocketEx* GetOwnerSocket() const { return m_pOwnerSocket; }
	CUpDownClient* GetOwnerClient() const { return m_pOwnerClient; }
	virtual int Receive(void* lpBuf, int nBufLen, int nFlags = 0);
	virtual int Send(const void* lpBuf, int nBufLen, int nFlags = 0);
	virtual BOOL ShutDown(int nHow = sends);
	virtual bool IsUtpLayer() const override { return true; }
	void Setup(utp_socket* Socket, bool bAcceptedSocket = false);
	void Destroy();
	AsyncSocketExState GetState() const { return GetLayerState(); }
	bool HasTransportFailed() const { return m_bTransportFailed; }
	//Attributes
	BOOL GetSockOpt(int nOptionName, void* lpOptionValue, int* lpOptionLen);
	BOOL SetSockOpt(int nOptionName, const void* lpOptionValue, int nOptionLen);

  protected:
	static uint64 on_utp_state_change(utp_callback_arguments* a);
	static uint64 on_utp_read(utp_callback_arguments* a);
	static uint64 on_utp_sendto(utp_callback_arguments* a);
	static uint64 on_utp_error(utp_callback_arguments* a);
	static uint64 on_utp_get_read_buffer_size (utp_callback_arguments* a);

	bool IsDuplexTransferLikely() const;
	size_t GetMaxWriteBufferCapacity() const;
	bool GrowWriteBufferTo(size_t uNewCapacity, LPCTSTR pszReason);
	bool ShrinkWriteBufferTo(size_t uNewCapacity, LPCTSTR pszReason);
	void TrimWriteBufferForDuplexIfPossible(LPCTSTR pszReason);
	size_t GetNextWriteBufferCapacity() const;
	void OnWriteBufferBlocked();

	struct UTPSocket*	m_Socket;
	utp_context*		m_Context;
	uint8				m_ShutDown;
	CBuffer				m_ReadBuffer;
	CBuffer				m_WriteBuffer;
	mutable CCriticalSection m_csReadBuffer;
	mutable CCriticalSection m_csWriteBuffer;
	bool				m_bConnectNotified;
	bool				m_bTransportFailed;
	CUpDownClient*		m_pOwnerClient;
	uint8				m_uZeroWriteBurst;
	bool				m_bAppSendBlocked;
	bool				m_bUtpWriteBlocked;
	bool				m_bUtpWritableHint;
	DWORD				m_dwNextUtpWriteAttemptTick;
	DWORD				m_dwLastSendTime;		// Tick count of last successful Send()
	uint32				m_uWriteBufferBlockCountAtCapacity;
	bool				m_bBufferGrown;			// Flag to track if buffer has been grown
};
