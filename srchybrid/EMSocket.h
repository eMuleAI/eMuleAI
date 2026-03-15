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
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#pragma once
#include "packets.h"
#include "EncryptedStreamSocket.h"
#include "ThrottledSocket.h" // ZZ:UploadBandWithThrottler (UDP)

class CUtpSocket;

class CAsyncProxySocketLayer;

#define EMS_DISCONNECTED	0xFF
#define EMS_NOTCONNECTED	0x00
#define EMS_CONNECTED		0x01

struct StandardPacketQueueEntry
{
	Packet *packet;
	uint32 actualPayloadSize;
};

class CEMSocket : public CEncryptedStreamSocket, public ThrottledFileSocket // ZZ:UploadBandWithThrottler
{
	DECLARE_DYNAMIC(CEMSocket)
public:
	CEMSocket();
	~CEMSocket();

	virtual void SendPacket(Packet *packet, bool controlpacket = true, uint32 actualPayloadSize = 0, bool bForceImmediateSend = false);
	bool	IsConnected() const				{ return byConnected == EMS_CONNECTED; }
	uint8	GetConState() const				{ return byConnected; }
	void	SetConState(uint8 val)			{ sendLocker.Lock(); byConnected = val; sendLocker.Unlock(); }
	virtual bool IsRawDataMode() const		{ return false; }
	void	SetDownloadLimit(uint32 limit);
	void	DisableDownloadLimit();
	BOOL	AsyncSelect(long lEvent);
	virtual bool IsBusyExtensiveCheck();
	virtual bool IsBusyQuickCheck() const;
	virtual bool HasQueues(bool bOnlyStandardPackets = false) const;
	virtual bool IsEnoughFileDataQueued(uint32 nMinFilePayloadBytes) const;
	virtual bool UseBigSendBuffer();
	INT_PTR	DbgGetStdQueueCount() const		{ return standardpacket_queue.GetCount(); }

	virtual DWORD GetTimeOut() const		{ return m_uTimeOut; }
	virtual void SetTimeOut(DWORD uTimeOut) { m_uTimeOut = uTimeOut; }

	virtual bool Connect(const CString &sHostAddress, UINT nHostPort);
	virtual BOOL Connect(const LPSOCKADDR pSockAddr, int iSockAddrLen);
	virtual int Receive(void *lpBuf, int nBufLen, int nFlags = 0);

	virtual void	OnClose(int nErrorCode);
	virtual void	OnSend(int nErrorCode);
	virtual void	OnReceive(int nErrorCode);

	CUtpSocket* InitUtpSupport();
	CUtpSocket* GetUtpLayer() { return m_pUtpLayer; }
	BOOL	HaveUtpLayer(bool bActive = false);
	
	// Override to bypass encryption check for uTP connections
	virtual bool IsEncryptionLayerReady();

	void InitProxySupport();
	virtual void RemoveAllLayers();
	const CString GetLastProxyError() const	{ return m_strLastProxyError; }
	bool GetProxyConnectFailed() const		{ return m_bProxyConnectFailed; }

	CString GetFullErrorMessage(DWORD dwError);

	DWORD GetLastCalledSend() const			{ return lastCalledSend; }
	uint64 GetSentBytesCompleteFileSinceLastCallAndReset();
	uint64 GetSentBytesPartFileSinceLastCallAndReset();
	uint64 GetSentBytesControlPacketSinceLastCallAndReset();
	uint32 GetSentPayloadSinceLastCall(bool bReset);
	void TruncateQueues();
	bool DropQueuedControlPacket(uint8 opcode, uint8 protocol = 0x00);

	virtual SocketSentBytes SendControlData(uint32 maxNumberOfBytesToSend, uint32 minFragSize)			{ return SendEM(maxNumberOfBytesToSend, minFragSize, true); }
	virtual SocketSentBytes SendFileAndControlData(uint32 maxNumberOfBytesToSend, uint32 minFragSize)	{ return SendEM(maxNumberOfBytesToSend, minFragSize, false); }

	uint32	GetNeededBytes();
#ifdef _DEBUG
	// Diagnostic Support
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext &dc) const;
#endif

protected:
	virtual int	OnLayerCallback(std::vector<t_callbackMsg> &callbacks);

	virtual void	DataReceived(const BYTE *pcData, UINT uSize);
	virtual bool	PacketReceived(Packet *packet) = 0;
	virtual void	OnError(int nErrorCode) = 0;
	CAsyncProxySocketLayer *m_pProxyLayer;
	CString m_strLastProxyError;
	UINT	m_uTimeOut;
	uint8	byConnected;
	bool	m_bProxyConnectFailed;
	CUtpSocket* m_pUtpLayer;

private:
	virtual SocketSentBytes SendEM(uint32 maxNumberOfBytesToSend, uint32 minFragSize, bool onlyAllowedToSendControlPacket);
	SocketSentBytes SendStd(uint32 maxNumberOfBytesToSend, uint32 minFragSize, bool onlyAllowedToSendControlPacket);
	SocketSentBytes SendOv(uint32 maxNumberOfBytesToSend, uint32 minFragSize, bool onlyAllowedToSendControlPacket);
	void	ClearQueues();
	void	CleanUpOverlappedSendOperation(bool bCancel);

	static uint32 GetNextFragSize(uint32 current, uint32 minFragSize);
	enum { EMSOCKET_READBUFFER_SIZE = 64 * 1024, EMSOCKET_MAX_PACKET_SIZE = 2000000 };

	// Download (pseudo) rate control
	bool	downloadLimitEnable;
	bool	pendingOnReceive;
	bool	m_bInOnReceive;
	bool	m_bDeferredOnReceive;

	uint32	downloadLimit;
	char	m_abyReadBuffer[EMSOCKET_READBUFFER_SIZE];

	// Download partial packet
	uint32	pendingPacketSize;
	Packet	*pendingPacket;
	// Download partial header
	size_t	pendingHeaderSize;
	char	pendingHeader[PACKET_HEADER_SIZE];	// actually, this holds only 'PACKET_HEADER_SIZE-1' bytes.

	// Upload control
	char	*sendbuffer;
	uint32	sendblen; //packet length in sendbuffer
	uint32	sent;
	WSAOVERLAPPED m_PendingSendOperation;
	CArray<WSABUF> m_aBufferSend;

	CTypedPtrList<CPtrList, Packet*> controlpacket_queue;
	CList<StandardPacketQueueEntry> standardpacket_queue;
	CCriticalSection sendLocker;
	uint64	m_numberOfSentBytesCompleteFile;
	uint64	m_numberOfSentBytesPartFile;
	uint64	m_numberOfSentBytesControlPacket;
	DWORD	lastCalledSend;
	DWORD	lastSent;
	DWORD	lastFinishedStandard;
	uint32	m_actualPayloadSize;			// Payloadsize of the data currently in sendbuffer
	uint32	m_actualPayloadSizeSent;
	bool	m_currentPacket_is_controlpacket;
	bool	m_currentPackageIsFromPartFile;
	bool	m_bAccelerateUpload;
	bool	m_bBusy;
	bool	m_hasSent;
	bool	m_bUseBigSendBuffers;
	bool	m_bUseOverlappedSend;
	bool	m_bPendingSendOv;
};
