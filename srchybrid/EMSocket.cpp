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
#include <timeapi.h>
#include "emsocket.h"
#include "AsyncProxySocketLayer.h"
#include "Packets.h"
#include "OtherFunctions.h"
#include "UploadBandwidthThrottler.h"
#include "Preferences.h"
#include "emuleDlg.h"
#include "Log.h"
#include "eMuleAI/UtpSocket.h"
#include "ListenSocket.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


namespace
{
	inline void EMTrace(char *fmt, ...)
	{
#ifdef EMSOCKET_DEBUG
		va_list argptr;
		char bufferline[512];
		va_start(argptr, fmt);
		vsnprintf(bufferline, _countof(bufferline), fmt, argptr);
		va_end(argptr);
		//(Ornis+)
		char osDate[30], osTime[30];
		char temp[1024];
		_strtime(osTime);
		_strdate(osDate);
		int len = snprintf(temp, _countof(temp), "%s %s: %s\r\n", osDate, osTime, bufferline);
		if (len > 0) {
			HANDLE hFile = ::CreateFile(_T("c:\\tmp\\EMSocket.log")	// ensure valid path to a writable location
				, GENERIC_WRITE			// open for writing
				, FILE_SHARE_READ		// share for reading
				, NULL					// no security
				, OPEN_ALWAYS			// open existing or create new
				, FILE_ATTRIBUTE_NORMAL // normal file
				, NULL);				// no template file

			if (hFile != INVALID_HANDLE_VALUE) {
				DWORD nbBytesWritten;
				SetFilePointer(hFile, 0, NULL, FILE_END);
				::WriteFile(hFile	// handle to file
					, temp				// data buffer
					, len				// number of bytes to write
					, &nbBytesWritten	// number of bytes written
					, NULL				// overlapped buffer
				);
				::CloseHandle(hFile);
			}
		}
#else
		//va_list argptr;
		UNREFERENCED_PARAMETER(fmt);
#endif //EMSOCKET_DEBUG
	}
}

IMPLEMENT_DYNAMIC(CEMSocket, CEncryptedStreamSocket)

CEMSocket::CEMSocket()
	: m_pProxyLayer()
	, m_uTimeOut(CONNECTION_TIMEOUT) // default timeout for ed2k sockets
	, byConnected(EMS_NOTCONNECTED)
	, m_bProxyConnectFailed()
	, downloadLimitEnable()
	, pendingOnReceive()
	, m_bInOnReceive()
	, m_bDeferredOnReceive()
	, downloadLimit()
	, pendingPacketSize()
	, pendingPacket()
	, pendingHeaderSize()
	, pendingHeader()
	, sendbuffer()
	, sendblen()
	, sent()
	, m_numberOfSentBytesCompleteFile()
	, m_numberOfSentBytesPartFile()
	, m_numberOfSentBytesControlPacket()
	, lastFinishedStandard()
	, m_actualPayloadSize()
	, m_actualPayloadSizeSent()
	, m_currentPacket_is_controlpacket()
	, m_currentPackageIsFromPartFile()
	, m_bAccelerateUpload()
	, m_bBusy()
	, m_hasSent()
	, m_bUseBigSendBuffers()
	, m_bUseOverlappedSend(true)
	, m_bPendingSendOv()
	, m_pUtpLayer()
{
	lastCalledSend = timeGetTime();
	lastSent = lastCalledSend > SEC2MS(1) ? lastCalledSend - SEC2MS(1) : 0;
}

CEMSocket::~CEMSocket()
{
	EMTrace("CEMSocket::~CEMSocket() on %u", (SOCKET)this);

	// need to be locked here to know that the other methods
	// won't be in the middle of things
	sendLocker.Lock();
	byConnected = EMS_DISCONNECTED;
	CleanUpOverlappedSendOperation(true);
	sendLocker.Unlock();

	// now that we know no other method will keep adding to the queue
	// we can remove ourself from the queue
	theApp.uploadBandwidthThrottler->RemoveFromAllQueues(this);

	ClearQueues();
	CEMSocket::RemoveAllLayers(); // deadlake PROXYSUPPORT
	AsyncSelect(0);
}

CUtpSocket* CEMSocket::InitUtpSupport()
{
	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NATTTESTMODE: InitUtpSupport] Entry: m_pUtpLayer=%p\n"), m_pUtpLayer);
	if (m_pUtpLayer == NULL) {
		m_pUtpLayer = new CUtpSocket;
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NATTTESTMODE: InitUtpSupport] Created CUtpSocket: %p\n"), m_pUtpLayer);
		AddLayer(m_pUtpLayer);
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NATTTESTMODE: InitUtpSupport] AddLayer done: m_pUtpLayer=%p\n"), m_pUtpLayer);
		// Disable overlapped send for uTP as it uses UDP transport with async sends
		m_bUseOverlappedSend = false;
	} else {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NATTTESTMODE: InitUtpSupport] WARNING: m_pUtpLayer already exists=%p\n"), m_pUtpLayer);
		ASSERT(0);
	}
	return m_pUtpLayer;

}

BOOL CEMSocket::HaveUtpLayer(bool bActive)
{
	if (m_pUtpLayer != NULL)
		return TRUE;
	return CEncryptedStreamSocket::HaveUtpLayer(bActive);
}

// deadlake PROXYSUPPORT
// By Maverick: Connection initialization is done by class itself
bool CEMSocket::Connect(const CString &sHostAddress, UINT nHostPort)
{
	// NOTE: InitProxySupport must be called BEFORE checking uTP layer, because
	// proxy layer adds itself to the front of the layer chain, which would bypass uTP layer.
	// However, when uTP is enabled, we should NOT initialize proxy support as it interferes with NAT-T.
	if (m_pUtpLayer == NULL) {
		InitProxySupport();
	}
	
	// If uTP layer is attached, delegate to layer chain
	if (m_pUtpLayer != NULL) {
		if (thePrefs.GetLogNatTraversalEvents())
			AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] CEMSocket::Connect(string): have uTP layer, delegating to chain"));
		return CAsyncSocketEx::Connect(sHostAddress, nHostPort);
	}
	return CEncryptedStreamSocket::Connect(sHostAddress, nHostPort);
}
// end deadlake

// deadlake PROXYSUPPORT
// By Maverick: Connection initialization is done by class itself
BOOL CEMSocket::Connect(const LPSOCKADDR pSockAddr, int iSockAddrLen)
{
	// NOTE: InitProxySupport must be called BEFORE checking uTP layer, because
	// proxy layer adds itself to the front of the layer chain, which would bypass uTP layer.
	// However, when uTP is enabled, we should NOT initialize proxy support as it interferes with NAT-T.
	if (m_pUtpLayer == NULL) {
		InitProxySupport();
	}
	
	// If uTP layer is attached, delegate to layer chain (which will call CUtpSocket::Connect)
	if (m_pUtpLayer != NULL) {
		if (thePrefs.GetLogNatTraversalEvents())
			AddDebugLogLine(DLP_VERYLOW, false, _T("[uTP-DEBUG] CEMSocket::Connect(sockaddr): have uTP layer, delegating to chain"));
		return CAsyncSocketEx::Connect(pSockAddr, iSockAddrLen);
	}
	return CEncryptedStreamSocket::Connect(pSockAddr, iSockAddrLen);
}
// end deadlake

void CEMSocket::InitProxySupport()
{
	m_bProxyConnectFailed = false;

	// Proxy Initialization
	const ProxySettings &settings = thePrefs.GetProxySettings();
	if (settings.bUseProxy && settings.type != PROXYTYPE_NOPROXY) {
		m_bUseOverlappedSend = false;
		Close();

		m_pProxyLayer = new CAsyncProxySocketLayer;
		switch (settings.type) {
		case PROXYTYPE_SOCKS4:
		case PROXYTYPE_SOCKS4A:
			m_pProxyLayer->SetProxy(settings.type, settings.host, settings.port);
			break;
		case PROXYTYPE_SOCKS5:
		case PROXYTYPE_HTTP10:
		case PROXYTYPE_HTTP11:
			if (settings.bEnablePassword)
				m_pProxyLayer->SetProxy(settings.type, settings.host, settings.port, settings.user, settings.password);
			else
				m_pProxyLayer->SetProxy(settings.type, settings.host, settings.port);
			break;
		default:
			ASSERT(0);
		}
		AddLayer(m_pProxyLayer);

		// Connection Initialization
		Create(0, SOCK_STREAM, FD_DEFAULT, thePrefs.GetBindAddr());
		AsyncSelect(FD_DEFAULT);
	}
}

void CEMSocket::ClearQueues()
{
	EMTrace("CEMSocket::ClearQueues on %u", (SOCKET)this);

	sendLocker.Lock();
	while (!controlpacket_queue.IsEmpty())
		delete controlpacket_queue.RemoveHead();

	while (!standardpacket_queue.IsEmpty())
		delete standardpacket_queue.RemoveHead().packet;
	sendLocker.Unlock();

	// Download (pseudo) rate control
	downloadLimit = 0;
	downloadLimitEnable = false;
	pendingOnReceive = false;

	// Download partial header
	pendingHeaderSize = 0;

	// Download partial packet
	delete pendingPacket;
	pendingPacket = NULL;
	pendingPacketSize = 0;

	// Upload control
	delete[] sendbuffer;
	sendbuffer = NULL;

	sendblen = 0;
	sent = 0;
}

void CEMSocket::OnClose(int nErrorCode)
{
	// need to be locked here to know that the other methods
	// won't be in the middle of things
	SetConState(EMS_DISCONNECTED);

	// now that we know no other method will keep adding to the queue
	// we can remove ourself from the queue
	theApp.uploadBandwidthThrottler->RemoveFromAllQueues(this);

	CEncryptedStreamSocket::OnClose(nErrorCode); // deadlake changed socket to PROXYSUPPORT ( AsyncSocketEx )
	RemoveAllLayers(); // deadlake PROXYSUPPORT
	ClearQueues();
}

BOOL CEMSocket::AsyncSelect(long lEvent)
{
#ifdef EMSOCKET_DEBUG
	if (lEvent & FD_READ)
		EMTrace("  FD_READ");
	if (lEvent & FD_CLOSE)
		EMTrace("  FD_CLOSE");
	if (lEvent & FD_WRITE)
		EMTrace("  FD_WRITE");
#endif
	// deadlake changed to AsyncSocketEx PROXYSUPPORT
	if (m_SocketData.hSocket != INVALID_SOCKET || HaveUtpLayer())
		return CEncryptedStreamSocket::AsyncSelect(lEvent);
	return true;
}

void CEMSocket::OnReceive(int nErrorCode)
{
	char* GlobalReadBuffer = m_abyReadBuffer;

	// uTP callbacks can re-enter while packet assembly state is still in use.
	if (m_bInOnReceive) {
		m_bDeferredOnReceive = true;
		if (HaveUtpLayer() && thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal][EMSocket::OnReceive] Re-entrant receive deferred"));
		return;
	}

	m_bInOnReceive = true;

	for (;;) {
		// Safe diagnostic for uTP packet receive investigation
		if (HaveUtpLayer() && thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal][EMSocket::OnReceive] ENTRY: err=%d conn=%d"), nErrorCode, (int)byConnected);

		// Check for an error code
		if (nErrorCode != 0 && nErrorCode != WSAESHUTDOWN) {
			OnError(nErrorCode);
			break;
		}

		// Check current connection state
		if (byConnected == EMS_DISCONNECTED)
			break;

		byConnected = EMS_CONNECTED; // EMS_DISCONNECTED, EMS_NOTCONNECTED, EMS_CONNECTED

		// CPU load improvement
		if (downloadLimitEnable && downloadLimit == 0 && nErrorCode != WSAESHUTDOWN) {
			EMTrace("CEMSocket::OnReceive blocked by limit");
			pendingOnReceive = true;
			break;
		}

		// Remark: an overflow can not occur here
		size_t readMax = sizeof m_abyReadBuffer - pendingHeaderSize;
		if (downloadLimitEnable && readMax > downloadLimit && nErrorCode != WSAESHUTDOWN)
			readMax = downloadLimit;

		// We attempt to read up to 2 megs at a time (minus whatever is in our internal read buffer)
		int ret = Receive(GlobalReadBuffer + pendingHeaderSize, (int)readMax);
		if (ret == SOCKET_ERROR || byConnected == EMS_DISCONNECTED)
			break;

		// Bandwidth control
		if (downloadLimitEnable)
			// Update limit
			downloadLimit -= GetRealReceivedBytes();

		// CPU load improvement
		// Detect if the socket's buffer is empty (or the size did match...)
		pendingOnReceive = m_bFullReceive;

		if (ret != 0) {
			// Copy back the partial header into the global read buffer for processing
			if (pendingHeaderSize > 0) {
				memcpy(GlobalReadBuffer, pendingHeader, pendingHeaderSize);
				ret += (int)pendingHeaderSize;
				pendingHeaderSize = 0;
			}

			if (IsRawDataMode()) {
				DataReceived((BYTE*)GlobalReadBuffer, ret);
			} else {
				char *rptr = GlobalReadBuffer; // floating index initialized with the buffer base
				const char *rend = GlobalReadBuffer + ret; // end of buffer
				// Loop, processing packets until we run out of them
				while (rend >= rptr + PACKET_HEADER_SIZE || (pendingPacket != NULL && rend > rptr)) {
					// Two possibilities here:
					//
					// 1. There is no pending incoming packet
					// 2. There is already a partial pending incoming packet
					//
					// It's important to remember that emule exchanges two kinds of packets
					// - The control packet
					// - The data packet for the transport of the block
					//
					// The biggest part of the traffic is done with the data packets.
					// The default size of one block is 10240 bytes (or less if compressed), but the
					// maximum size for one packet on the network is 1300 bytes.
					// It's the reason why most of the blocks were split before being sent.
					//
					// Conclusion: When the download limit is disabled, this method may be called
					// 8 times (10240/1300) by the lower layer before the split packet was
					// rebuilt and transferred to the above layer for processing.
					//
					// The purpose of this algorithm is to limit the amount of data exchanged between buffers

					if (pendingPacket == NULL) {
						// Bugfix We still need to check for a valid protocol
						// Remark: the default eMule v0.26b had removed this test...
						switch (reinterpret_cast<Header_Struct*>(rptr)->eDonkeyID) {
						case OP_EDONKEYPROT:
						case OP_PACKEDPROT:
						case OP_EMULEPROT:
							break;
						default:
							EMTrace("CEMSocket::OnReceive ERROR Wrong header");
							OnError(ERR_WRONGHEADER);
							ret = SOCKET_ERROR;
							break;
						}

						if (ret == SOCKET_ERROR)
							break;

						// Security: Check for buffer overflow (2MB)
						if (reinterpret_cast<Header_Struct*>(rptr)->packetlength - 1 > EMSOCKET_MAX_PACKET_SIZE) {
							OnError(ERR_TOOBIG);
							ret = SOCKET_ERROR;
							break;
						}
						// Init data buffer
						pendingPacket = new Packet(rptr);	// Create new packet container.
						rptr += PACKET_HEADER_SIZE;			// Only the header is initialized so far
						pendingPacket->pBuffer = new char[pendingPacket->size + 1];
						pendingPacketSize = 0;
					}

					if (ret == SOCKET_ERROR)
						break;

					// Bytes ready to be copied into packet's internal buffer
					ASSERT(rptr <= rend);
					uint32 toCopy = min(pendingPacket->size - pendingPacketSize, (uint32)(rend - rptr));

					// Copy bytes from Global buffer to packet's internal buffer
					memcpy(&pendingPacket->pBuffer[pendingPacketSize], rptr, toCopy);
					pendingPacketSize += toCopy;
					rptr += toCopy;

					// Check if packet is complete
					ASSERT(pendingPacket->size >= pendingPacketSize);
					if (pendingPacket->size == pendingPacketSize) {
#ifdef EMSOCKET_DEBUG
						EMTrace("CEMSocket::PacketReceived on %u, opcode=%X, realSize=%d"
							, (SOCKET)this, pendingPacket->opcode, pendingPacket->GetRealPacketSize());
#endif
						// Process packet
						bool bPacketResult = PacketReceived(pendingPacket);
						delete pendingPacket;
						pendingPacket = NULL;
						pendingPacketSize = 0;

						if (!bPacketResult) {
							ret = SOCKET_ERROR;
							break;
						}
					}
				}

				if (ret == SOCKET_ERROR)
					break;

				// Finally, if there is any data left over, save it for next time
				ASSERT(rptr <= rend);
				ASSERT(rend - rptr < PACKET_HEADER_SIZE);
				if (rptr < rend) {
					// Keep the partial head
					pendingHeaderSize = rend - rptr;
					memcpy(pendingHeader, rptr, pendingHeaderSize);
				}
			}
		}

		if (m_bDeferredOnReceive)
			pendingOnReceive = true;
		if (!pendingOnReceive || (downloadLimitEnable && downloadLimit == 0))
			break;

		pendingOnReceive = false;
		m_bDeferredOnReceive = false;
		nErrorCode = 0;
	}

	m_bDeferredOnReceive = false;
	m_bInOnReceive = false;
}

void CEMSocket::SetDownloadLimit(uint32 limit)
{
	downloadLimit = limit;
	downloadLimitEnable = true;

	// CPU load improvement
	if (limit > 0 && pendingOnReceive)
		OnReceive(0);
}

void CEMSocket::DisableDownloadLimit()
{
	downloadLimitEnable = false;

	// CPU load improvement
	if (pendingOnReceive)
		OnReceive(0);
}

/**
 * Queues up the packet to be sent. Another thread will actually send the packet.
 *
 * If the packet is not a control packet, and if the socket decides that its queue is
 * full and forceAdd is false, then the socket is allowed to refuse to add the packet
 * to its queue. It will then return false and it is up to the calling thread to try
 * to call SendPacket for that packet again at a later time.
 *
 * @param packet address to the packet that should be added to the queue
 *
 * @param controlpacket the packet is a controlpacket
 *
 * @param forceAdd this packet must be added to the queue, even if it is full. If this flag is true
 *					then the method can not refuse to add the packet, and therefore not return false.
 *
 * @return true if the packet was added to the queue, false otherwise
 */
void CEMSocket::SendPacket(Packet *packet, bool controlpacket, uint32 actualPayloadSize, bool bForceImmediateSend)
{

	if (byConnected == EMS_DISCONNECTED) {
		delete packet;
		return;
	}

	sendLocker.Lock();
	bool bUtpNotReady = HaveUtpLayer() && byConnected != EMS_CONNECTED;
	if (bUtpNotReady && thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NatTraversal] CEMSocket::SendPacket: queueing packet while uTP not yet connected (ctrl=%d), socket=%p"), (int)controlpacket, this);

	if (controlpacket) {
		controlpacket_queue.AddTail(packet);

		// queue up for controlpacket
		theApp.uploadBandwidthThrottler->QueueForSendingControlPacket(this, m_hasSent);
	} else {
		bool first = !((sendbuffer && !m_currentPacket_is_controlpacket) || !standardpacket_queue.IsEmpty());
		standardpacket_queue.AddTail(StandardPacketQueueEntry{ packet, actualPayloadSize });

		// reset timeout for the first time
		if (first) {
			lastFinishedStandard = timeGetTime();
			m_bAccelerateUpload = true;	// Always accelerate first packet in a block
		}
	}
	sendLocker.Unlock();

	if (bForceImmediateSend) {
		ASSERT(!controlpacket_queue.IsEmpty());
		SendEM(1024, 0, true);
	}
}

uint64 CEMSocket::GetSentBytesCompleteFileSinceLastCallAndReset()
{
	return (uint64)::InterlockedExchange64((LONG64*)&m_numberOfSentBytesCompleteFile, 0);
}

uint64 CEMSocket::GetSentBytesPartFileSinceLastCallAndReset()
{
	return (uint64)::InterlockedExchange64((LONG64*)&m_numberOfSentBytesPartFile, 0);
}

uint64 CEMSocket::GetSentBytesControlPacketSinceLastCallAndReset()
{
	return (uint64)::InterlockedExchange64((LONG64*)&m_numberOfSentBytesControlPacket, 0);
}

uint32 CEMSocket::GetSentPayloadSinceLastCall(bool bReset)
{
	return bReset ? (uint32)::InterlockedExchange((LONG*)&m_actualPayloadSizeSent, 0) : m_actualPayloadSizeSent;
}

void CEMSocket::OnSend(int nErrorCode)
{

	if (nErrorCode) {
		OnError(nErrorCode);
		return;
	}

	CEncryptedStreamSocket::OnSend(0);

	m_bBusy = false;

	// stopped sending here.

	if (byConnected == EMS_DISCONNECTED)
		return;

	byConnected = EMS_CONNECTED;

	if (m_currentPacket_is_controlpacket) {
		// queue up for control packet
		theApp.uploadBandwidthThrottler->QueueForSendingControlPacket(this, m_hasSent);
	}

	if (!m_bUseOverlappedSend && (!standardpacket_queue.IsEmpty() || sendbuffer != NULL))
		theApp.uploadBandwidthThrottler->SocketAvailable();
}

SocketSentBytes CEMSocket::SendEM(uint32 maxNumberOfBytesToSend, uint32 minFragSize, bool onlyAllowedToSendControlPacket)
{
	if (byConnected == EMS_DISCONNECTED)
		return SocketSentBytes{};
	if (m_bUseOverlappedSend)
		return SendOv(maxNumberOfBytesToSend, minFragSize, onlyAllowedToSendControlPacket);
	return SendStd(maxNumberOfBytesToSend, minFragSize, onlyAllowedToSendControlPacket);
}

/**
 * Try to put queued up data on the socket.
 *
 * Control packets have higher priority, and will be sent first, if possible.
 * Standard packets can be split up in several package containers. In that case
 * all the parts of a split package must be sent in a row, without any control packet
 * in between.
 *
 * @param maxNumberOfBytesToSend This is the maximum number of bytes that is allowed to be put on the socket
 *								 this call. The actual number of sent bytes will be returned from the method.
 *
 * @param onlyAllowedToSendControlPacket This call we only try to put control packets on the sockets.
 *										 If there's a standard packet "in the way", and we think that this socket
 *										 is no longer an upload slot, then it is OK to send the standard packet to
 *										 get it out of the way. But it is not allowed to pick a new standard packet
 *										 from the queue during this call. Several split packets are counted as one
 *										 standard packet though, so it is OK to finish them all off if necessary.
 *
 * @return the actual number of bytes that were put on the socket.
 */
SocketSentBytes CEMSocket::SendStd(uint32 maxNumberOfBytesToSend, uint32 minFragSize, bool onlyAllowedToSendControlPacket)
{
	SocketSentBytes ret = { 0, 0, true };

	sendLocker.Lock();
	// For uTP, bypass encryption layer ready check as uTP uses UDP transport
	// and eMule's TCP encryption negotiation doesn't apply. uTP has its own encryption.
	bool bUtpBypassEncryption = HaveUtpLayer();
	
	// Debug logging for uTP packet send investigation
	if (bUtpBypassEncryption && thePrefs.GetLogNatTraversalEvents()) {
		DebugLog(_T("[NatTraversal][SendStd] uTP bypass: connected=%d encReady=%d ctrlQ=%d stdQ=%d sendbuf=%p useOvSend=%d"),
			(int)(byConnected == EMS_CONNECTED), (int)IsEncryptionLayerReady(),
			controlpacket_queue.GetCount(), standardpacket_queue.GetCount(), sendbuffer, (int)m_bUseOverlappedSend);
	}
	
	if (byConnected == EMS_CONNECTED && (bUtpBypassEncryption || IsEncryptionLayerReady())) {
		if (minFragSize < 1)
			minFragSize = 1;

		maxNumberOfBytesToSend = GetNextFragSize(maxNumberOfBytesToSend, minFragSize);

		lastCalledSend = timeGetTime();
		bool bWasLongTimeSinceSend = (lastCalledSend >= lastSent + SEC2MS(1));

		uint32 sentBytes = ret.sentBytesStandardPackets + ret.sentBytesControlPackets;
		while (sentBytes < maxNumberOfBytesToSend // don't send more than allowed
			&& ret.success // there should have been no error in earlier loop
			&& (sendbuffer != NULL || !controlpacket_queue.IsEmpty() || (!standardpacket_queue.IsEmpty() && !onlyAllowedToSendControlPacket)) // there must exist something to send
			&& (   !onlyAllowedToSendControlPacket // this means we are allowed to send both types of packets, so proceed
				|| (sendbuffer != NULL && m_currentPacket_is_controlpacket) // We are in the process of sending a control packet. We are always allowed to send those
				|| (sentBytes > 0 && sentBytes % minFragSize != 0) // Once we've started, continue to send until an even minFragsize to minimize packet overhead
				|| (sendbuffer == NULL && !controlpacket_queue.IsEmpty()) // There's a control packet in queue, and we are not currently sending anything, so we will handle the control packet next
				|| (sendbuffer != NULL && !m_currentPacket_is_controlpacket && bWasLongTimeSinceSend && !controlpacket_queue.IsEmpty() && sentBytes < minFragSize) // We have waited too long to clean the current packet (which may be a standard packet that is in the way). Proceed no matter what the value of onlyAllowedToSendControlPacket.
			   )
			)
		{ // If we are not sending a packet currently, we will need to find one to send
			if (sendbuffer == NULL) {
				Packet *curPacket = NULL;
				m_currentPacket_is_controlpacket = !controlpacket_queue.IsEmpty();
				if (m_currentPacket_is_controlpacket)
					// There's a control packet to send
					curPacket = controlpacket_queue.RemoveHead();
				else {
					if (standardpacket_queue.IsEmpty()) {
						// Just to be safe. Shouldn't happen?
						sendLocker.Unlock();

						// if we reach this point, then there's something wrong with the while condition above!
						ASSERT(0);
						theApp.QueueDebugLogLine(true, _T("EMSocket: Couldn't get a new packet! There's an error in the first while condition in EMSocket::Send()"));

						return ret;
					}
					// There's a standard packet to send
					StandardPacketQueueEntry queueEntry = standardpacket_queue.RemoveHead();
					curPacket = queueEntry.packet;
					m_actualPayloadSize = queueEntry.actualPayloadSize;

					// remember this for statistics purposes.
					m_currentPackageIsFromPartFile = curPacket->IsFromPF();
				}

				// We found a package to send. Get the data to send from the
				// package container and dispose of the container.
				sendblen = curPacket->GetRealPacketSize();
				sendbuffer = curPacket->DetachPacket();
				sent = 0;
				delete curPacket;

				// encrypting which cannot be done transparent by base class
				// Skip encryption for uTP as it uses UDP transport with its own encryption
				if (!bUtpBypassEncryption)
					CryptPrepareSendData((uchar*)sendbuffer, sendblen);
			}

			// At this point we've got a packet to send in sendbuffer. Try to send it. Loop until entire packet
			// is sent, or until we reach maximum bytes to send for this call, or until we get an error.
			// NOTE! If send would block (returns WSAEWOULDBLOCK), we will return from this method INSIDE this loop.
			while (sent < sendblen
				&& sentBytes < maxNumberOfBytesToSend
				&& (   !onlyAllowedToSendControlPacket // this means we are allowed to send both types of packets, so proceed
					|| m_currentPacket_is_controlpacket
					|| (bWasLongTimeSinceSend && sentBytes < minFragSize)
					|| sentBytes % minFragSize != 0
				   )
				&& ret.success)
			{
				uint32 tosend = sendblen - sent;
				if (!onlyAllowedToSendControlPacket || m_currentPacket_is_controlpacket) {
					if (tosend > maxNumberOfBytesToSend - sentBytes)
						tosend = maxNumberOfBytesToSend - sentBytes;
				} else if (bWasLongTimeSinceSend && minFragSize > sentBytes) {
					if (tosend > minFragSize - sentBytes)
						tosend = minFragSize - sentBytes;
				} else {
					uint32 nextFragMaxBytesToSent = GetNextFragSize(sentBytes, minFragSize);
					if (nextFragMaxBytesToSent >= sentBytes && tosend > nextFragMaxBytesToSent - sentBytes)
						tosend = nextFragMaxBytesToSent - sentBytes;
				}
				ASSERT(tosend != 0 && tosend <= sendblen - sent);

				lastSent = timeGetTime();

					uint32 result = CEncryptedStreamSocket::Send(sendbuffer + sent, tosend); // deadlake PROXYSUPPORT - changed to AsyncSocketEx
					if (result == (uint32)SOCKET_ERROR) {
						uint32 error = (uint32)CAsyncSocket::GetLastError();
						if (error == WSAEWOULDBLOCK) {
							// uTP can miss extra writable callbacks across congestion transitions.
							// Do not latch file-socket scheduling on m_bBusy for uTP.
							m_bBusy = !bUtpBypassEncryption;

							sendLocker.Unlock();

							// Send() blocked, onsend will be called when ready to send again
							return ret;
						}
						// Send() gave an error
						ret.success = false;
					} else {
						// we managed to send some bytes. Perform bookkeeping.
						m_bBusy = false;
					m_hasSent = true;

					sent += result;
					// Keep uTP sends within the throttler budget for this pass.
					// TCP keeps legacy behavior to avoid changing long-standing flow semantics.
					if (bUtpBypassEncryption)
						sentBytes += result;
					else
						sentBytes = result;
					// Log send bytes in correct class
					if (!m_currentPacket_is_controlpacket) {
						ret.sentBytesStandardPackets += result;

						if (m_currentPackageIsFromPartFile)
							::InterlockedAdd64((LONG64*)&m_numberOfSentBytesPartFile, result);
						else
							::InterlockedAdd64((LONG64*)&m_numberOfSentBytesCompleteFile, result);

					} else {
						ret.sentBytesControlPackets += result;
						::InterlockedAdd64((LONG64*)&m_numberOfSentBytesControlPacket, result);
					}
				}
			}

			if (sent == sendblen) {
				// we are done sending the current package. Delete it and set
				// sendbuffer to NULL so a new packet can be fetched.
				delete[] sendbuffer;
				sendbuffer = NULL;
				sendblen = 0;

				if (!m_currentPacket_is_controlpacket) {
					::InterlockedAdd((LONG*)&m_actualPayloadSizeSent, m_actualPayloadSize);
					m_actualPayloadSize = 0;

					lastFinishedStandard = timeGetTime(); // reset timeout
					m_bAccelerateUpload = false; // Safe until told otherwise
				}

				sent = 0;
			}
		}
	}

	if (onlyAllowedToSendControlPacket && (!controlpacket_queue.IsEmpty() || (sendbuffer != NULL && m_currentPacket_is_controlpacket))) {
		// enter control packet send queue
		// we might enter control packet queue several times for the same package,
		// but that costs very little overhead. Less overhead than trying to make sure
		// that we only enter the queue once.
		theApp.uploadBandwidthThrottler->QueueForSendingControlPacket(this, m_hasSent);
	}

	sendLocker.Unlock();
	return ret;
}

/**
 * Try to put queued up data on the socket with Overlapped methods.
 *
 * Control packets have higher priority, and will be sent first, if possible.
 *
 * @param maxNumberOfBytesToSend This is the maximum number of bytes that is allowed to be put on the socket
 *								 this call. The actual number of sent bytes will be returned from the method.
 *
 * @param onlyAllowedToSendControlPacket This call we only try to put control packets on the sockets.
 *
 * @return the actual number of bytes that were put on the socket.
 */
SocketSentBytes CEMSocket::SendOv(uint32 maxNumberOfBytesToSend, uint32 minFragSize, bool onlyAllowedToSendControlPacket)
{
	ASSERT(m_pProxyLayer == NULL);
	SocketSentBytes ret = {0, 0, true};

	sendLocker.Lock();
	// For uTP, bypass encryption layer ready check as uTP uses UDP transport
	// and eMule's TCP encryption negotiation doesn't apply. uTP has its own encryption.
	bool bUtpBypassEncryption = HaveUtpLayer();
	
	// Debug logging for uTP packet send investigation
	if (bUtpBypassEncryption && thePrefs.GetLogNatTraversalEvents()) {
		DebugLog(_T("[NatTraversal][SendOv] uTP bypass: connected=%d encReady=%d ctrlQ=%d stdQ=%d sendbuf=%p"),
			(int)(byConnected == EMS_CONNECTED), (int)IsEncryptionLayerReady(),
			controlpacket_queue.GetCount(), standardpacket_queue.GetCount(), sendbuffer);
	}
	
	if (byConnected == EMS_CONNECTED && (bUtpBypassEncryption || IsEncryptionLayerReady()) && !IsBusyExtensiveCheck() && maxNumberOfBytesToSend > 0) {
		if (minFragSize < 1)
			minFragSize = 1;

		maxNumberOfBytesToSend = GetNextFragSize(maxNumberOfBytesToSend, minFragSize);
		lastCalledSend = timeGetTime();
		ASSERT(!m_bPendingSendOv && m_aBufferSend.IsEmpty());
		if (sendbuffer != NULL || !controlpacket_queue.IsEmpty() || (!standardpacket_queue.IsEmpty() && !onlyAllowedToSendControlPacket)) {
			// WSASend takes multiple buffers which is quite nice for our case, as we have to call send
			// only once regardless how many packets we want to ship without moving memory.
			// But before we can do this, collect all buffers we want to send in this call

			sint32 nBytesLeft = maxNumberOfBytesToSend;
			// first send the existing sendbuffer (already started packet)
			if (sendbuffer != NULL) {
				WSABUF pCurBuf;
				pCurBuf.len = min(sendblen - sent, (uint32)nBytesLeft);
				pCurBuf.buf = new CHAR[pCurBuf.len];
				memcpy(pCurBuf.buf, sendbuffer + sent, pCurBuf.len);
				sent += pCurBuf.len;
				m_aBufferSend.Add(pCurBuf);
				nBytesLeft -= pCurBuf.len;
				if (sent == sendblen) { //finished the buffer
					delete[] sendbuffer;
					sendbuffer = NULL;
					sendblen = 0;
				}
				ret.sentBytesStandardPackets += pCurBuf.len; // Sendbuffer is always a standard packet in this method
				lastFinishedStandard = timeGetTime();
				m_bAccelerateUpload = false;
				::InterlockedAdd((LONG*)&m_actualPayloadSizeSent, m_actualPayloadSize);
				m_actualPayloadSize = 0;
				if (m_currentPackageIsFromPartFile)
					::InterlockedAdd64((LONG64*)&m_numberOfSentBytesPartFile, pCurBuf.len);
				else
					::InterlockedAdd64((LONG64*)&m_numberOfSentBytesCompleteFile, pCurBuf.len);
			}

			// next send all control packets if there are any and we have bytes left
			while (!controlpacket_queue.IsEmpty() && nBytesLeft > 0) {
				// never split control packets, ignore going over the limit by a few bytes
				WSABUF pCurBuf;
				Packet *curPacket = controlpacket_queue.RemoveHead();
				pCurBuf.len = curPacket->GetRealPacketSize();
				pCurBuf.buf = curPacket->DetachPacket();
				delete curPacket;
				// encrypting which cannot be done transparently in the base class
				// Skip encryption for uTP as it uses UDP transport with its own encryption
				if (!bUtpBypassEncryption)
					CryptPrepareSendData((uchar*)pCurBuf.buf, pCurBuf.len);
				m_aBufferSend.Add(pCurBuf);
				nBytesLeft -= pCurBuf.len;
				ret.sentBytesControlPackets += pCurBuf.len;
			}

			// and now finally the standard packets if there are any, and we have bytes left, and we are allowed to
			if (!onlyAllowedToSendControlPacket)
				while (!standardpacket_queue.IsEmpty() && nBytesLeft > 0) {
					StandardPacketQueueEntry queueEntry = standardpacket_queue.RemoveHead();
					WSABUF pCurBuf;
					Packet *curPacket = queueEntry.packet;
					m_currentPackageIsFromPartFile = curPacket->IsFromPF();

					// can we send it right away or only a part of it?
					if (queueEntry.packet->GetRealPacketSize() <= (uint32)nBytesLeft) {
						// yay
						pCurBuf.len = curPacket->GetRealPacketSize();
						pCurBuf.buf = curPacket->DetachPacket();
						// Skip encryption for uTP as it uses UDP transport with its own encryption
						if (!bUtpBypassEncryption)
							CryptPrepareSendData((uchar*)pCurBuf.buf, pCurBuf.len);// encryption cannot be done transparently in the base class
						::InterlockedAdd((LONG*)&m_actualPayloadSizeSent, queueEntry.actualPayloadSize);
						lastFinishedStandard = timeGetTime();
						m_bAccelerateUpload = false;
					} else {	// aww, well first stuff everything into the sendbuffer and then send what we can of it
						ASSERT(sendbuffer == NULL);
						m_actualPayloadSize = queueEntry.actualPayloadSize;
						sendblen = curPacket->GetRealPacketSize();
						sendbuffer = curPacket->DetachPacket();
						sent = 0;
						// Skip encryption for uTP as it uses UDP transport with its own encryption
						if (!bUtpBypassEncryption)
							CryptPrepareSendData((uchar*)sendbuffer, sendblen); //  encryption cannot be done transparently in the base class
						pCurBuf.len = min(sendblen - sent, (uint32)nBytesLeft);
						pCurBuf.buf = new CHAR[pCurBuf.len];
						memcpy(pCurBuf.buf, sendbuffer, pCurBuf.len);
						sent += pCurBuf.len;
						ASSERT(sent < sendblen);
						m_currentPacket_is_controlpacket = false;
					}
					delete curPacket;
					m_aBufferSend.Add(pCurBuf);
					nBytesLeft -= pCurBuf.len;
					ret.sentBytesStandardPackets += pCurBuf.len;
					if (m_currentPackageIsFromPartFile)
						::InterlockedAdd64((LONG64*)&m_numberOfSentBytesPartFile, pCurBuf.len);
					else
						::InterlockedAdd64((LONG64*)&m_numberOfSentBytesCompleteFile, pCurBuf.len);
				}

			if (m_aBufferSend.GetCount() > 0) {
				// all right, prepare to send our collected buffers
				memset(&m_PendingSendOperation, 0, sizeof WSAOVERLAPPED);
				m_PendingSendOperation.hEvent = theApp.uploadBandwidthThrottler->GetSocketAvailableEvent();
				m_bPendingSendOv = true;
				if (CEncryptedStreamSocket::SendOv(m_aBufferSend, &m_PendingSendOperation) == 0)
					CleanUpOverlappedSendOperation(false);
				else {
					int nError = WSAGetLastError();
					if (nError != WSA_IO_PENDING) {
						ret.success = false;
						theApp.QueueDebugLogLineEx(ERROR, _T("WSASend() Error: %u, %s"), nError, (LPCTSTR)GetErrorMessage(nError));
						CleanUpOverlappedSendOperation(false);
					}
				}
			}
		}
	}

	if (onlyAllowedToSendControlPacket && !controlpacket_queue.IsEmpty()) {
		// enter control packet send queue
		// we might enter control packet queue several times for the same package,
		// but that costs very little overhead. Less overhead than trying to make sure
		// that we only enter the queue once.
		theApp.uploadBandwidthThrottler->QueueForSendingControlPacket(this, m_hasSent);
	}

	sendLocker.Unlock();
	return ret;
}

uint32 CEMSocket::GetNextFragSize(uint32 current, uint32 minFragSize)
{
	return (min(_I32_MAX, current + minFragSize - 1) / minFragSize) * minFragSize;
}

/**
 * Decides the (minimum) amount the socket needs to send to prevent timeout.
 *
 * @author SlugFiller
 */
uint32 CEMSocket::GetNeededBytes()
{
	sendLocker.Lock();
	if (byConnected == EMS_DISCONNECTED) {
		sendLocker.Unlock();
		return 0;
	}

	bool isControlpacket = (sendbuffer == NULL) || m_currentPacket_is_controlpacket;
	if (isControlpacket && standardpacket_queue.IsEmpty()) {
		// No standard packet to send. Even if data needs to be sent to prevent timeout, there's nothing to send.
		sendLocker.Unlock();
		return 0;
	}
	if (!isControlpacket && !controlpacket_queue.IsEmpty())
		m_bAccelerateUpload = true;	// We might be trying to send a block request, accelerate packet

	DWORD sendgap = timeGetTime();
	DWORD timeleft = sendgap - lastFinishedStandard;
	sendgap -= lastCalledSend;
	DWORD timetotal = SEC2MS(m_bAccelerateUpload ? 45 : 90);
	uint64 sizeleft, sizetotal;
	if (!isControlpacket) {
		sizeleft = sendblen - sent;
		sizetotal = sendblen;
	} else
		sizeleft = sizetotal = standardpacket_queue.GetHead().packet->GetRealPacketSize();
	sendLocker.Unlock();

	if (timeleft >= timetotal)
		return (uint32)sizeleft;
	timeleft = timetotal - timeleft;
	// don't use 'GetTimeOut' here in case the timeout value is high,
	if (timeleft * sizetotal >= timetotal * sizeleft) {
		// Don't let the socket itself to time out - Might happen when switching from spread (non-focus) slot to trickle slot
		return static_cast<uint32>(sendgap >= SEC2MS(20));
	}
	uint64 decval = timeleft * sizetotal / timetotal;
	if (!decval)
		return (uint32)sizeleft;
	if (decval < sizeleft)
		return (uint32)(sizeleft - decval + 1);	// Round up
	return 1;
}

// pach2:
// written this overriden Receive to handle transparently FIN notifications coming from calls to recv()
// This was maybe(??) the cause of a lot of socket error, notably after a brutal close from peer
// also added trace so that we can debug after the fact.
int CEMSocket::Receive(void *lpBuf, int nBufLen, int nFlags)
{
	int recvRetCode = CEncryptedStreamSocket::Receive(lpBuf, nBufLen, nFlags); // deadlake PROXYSUPPORT - changed to AsyncSocketEx
	switch (recvRetCode) {
	case 0:
		if (GetRealReceivedBytes() <= 0) { // we received data but it was for the underlying encryption layer - all fine
			// FIN received on socket // Connection is being closed by peer
			//ASSERT (false);
			if (!AsyncSelect(FD_CLOSE | FD_WRITE)) { // no more READ notifications...
				ASSERT(0);
			}
		}
		break;
	case SOCKET_ERROR:
		char *p = NULL;
		switch (CAsyncSocket::GetLastError()) {
		case WSANOTINITIALISED:
			ASSERT(0);
			EMTrace("%sA successful AfxSocketInit must occur before using this API.");
			break;
		case WSAENETDOWN:
			ASSERT(true);
			p = "%sThe socket %u received a net down error";
			break;
		case WSAENOTCONN:
			p = "%sThe socket %u is not connected";
			break;
		case WSAEINPROGRESS:   // A blocking Windows Sockets operation is in progress.
			p = "%sThe socket %u is blocked";
			break;
		case WSAEWOULDBLOCK:   // The socket is marked as nonblocking and the Receive operation would block.
			p = "%sThe socket %u would block";
			break;
		case WSAENOTSOCK:   // The descriptor is not a socket.
			p = "%sThe descriptor %u is not a socket (may have been closed or never created)";
			break;
		case WSAEOPNOTSUPP:  // MSG_OOB was specified, but the socket is not of type SOCK_STREAM.
			break;
		case WSAESHUTDOWN:		// The socket has been shut down; it is impossible to call Receive on a socket after ShutDown(0) or ShutDown(2) has been invoked.
			p = "%sThe socket %u has been shut down";
			break;
		case WSAEMSGSIZE:   // The datagram was too large to fit into the specified buffer and was truncated.
			p = "%sThe datagram was too large to fit and was truncated (socket %u)";
			break;
		case WSAEINVAL:   // The socket has not been bound with Bind.
		case WSAECONNABORTED:   // The virtual circuit was aborted due to timeout or other failure.
		case WSAECONNRESET:   // The virtual circuit was reset by the remote side.
			p = "%sThe socket %u has not been bound";
			break;
		default:
			EMTrace("CEMSocket::OnReceive: Unexpected socket error %x on socket %u", CAsyncSocket::GetLastError(), (SOCKET)this);
		}
		if (p)
			EMTrace(p, "CEMSocket::OnReceive: ", (SOCKET)this);
//		break;
//	default:
	}
	return recvRetCode;
}

void CEMSocket::RemoveAllLayers()
{
	CEncryptedStreamSocket::RemoveAllLayers();
	delete m_pUtpLayer;
	m_pUtpLayer = NULL;
	delete m_pProxyLayer;
	m_pProxyLayer = NULL;
}

int CEMSocket::OnLayerCallback(std::vector<t_callbackMsg> &callbacks)
{
	for (std::vector<t_callbackMsg>::const_iterator iter = callbacks.begin(); iter != callbacks.end(); ++iter) {
		if (iter->nType == LAYERCALLBACK_LAYERSPECIFIC) {
			if (iter->pLayer == m_pProxyLayer) {
				m_strLastProxyError = GetProxyError((int)iter->wParam);
				switch (iter->wParam) {
				case PROXYERROR_NOCONN:
					// We failed to connect to the proxy.
					m_bProxyConnectFailed = true;
				case PROXYERROR_REQUESTFAILED:
					// We are connected to the proxy but it failed to connect to the peer.
					if (thePrefs.GetVerbose() && iter->str && iter->str[0] != '\0')
						m_strLastProxyError.AppendFormat(_T(" - %hs"), iter->str);
				}
				LogWarning(LOG_DEFAULT, _T("Proxy Error: %s"), (LPCTSTR)m_strLastProxyError);
			}
		}
		delete[] iter->str;
	}
	return 0;
}

/**
 * Removes all packets from the standard queue that don't have to be sent for the socket to be able to send a control packet.
 *
 * Before a socket can send a new packet, the current packet has to be finished. If the current packet is part of
 * a split packet, then all parts of that split packet must be sent before the socket can send a control packet.
 *
 * This method keeps in standard queue only those packets that must be sent (rest of split packet), and removes everything
 * after it. The method doesn't touch the control packet queue.
 */
void CEMSocket::TruncateQueues()
{
	sendLocker.Lock();

	// Clear the standard queue totally
	// Please note! There may still be a standardpacket in the sendbuffer variable!
	while (!standardpacket_queue.IsEmpty())
		delete standardpacket_queue.RemoveHead().packet;

	sendLocker.Unlock();
}

bool CEMSocket::DropQueuedControlPacket(uint8 opcode, uint8 protocol)
{
	bool bDropped = false;

	sendLocker.Lock();

	for (POSITION pos = controlpacket_queue.GetHeadPosition(); pos != NULL;) {
		POSITION posRemove = pos;
		Packet* pPacket = controlpacket_queue.GetNext(pos);
		if (pPacket != NULL && pPacket->opcode == opcode && (protocol == 0x00 || pPacket->prot == protocol)) {
			controlpacket_queue.RemoveAt(posRemove);
			delete pPacket;
			bDropped = true;
		}
	}

	if (sendbuffer != NULL
		&& m_currentPacket_is_controlpacket
		&& sent == 0
		&& sendblen >= PACKET_HEADER_SIZE)
	{
		const Header_Struct* pHeader = reinterpret_cast<const Header_Struct*>(sendbuffer);
		if (pHeader->command == opcode && (protocol == 0x00 || pHeader->eDonkeyID == protocol)) {
			delete[] sendbuffer;
			sendbuffer = NULL;
			sendblen = 0;
			sent = 0;
			m_currentPacket_is_controlpacket = false;
			bDropped = true;
		}
	}

	sendLocker.Unlock();
	return bDropped;
}

#ifdef _DEBUG
void CEMSocket::AssertValid() const
{
	CEncryptedStreamSocket::AssertValid();

	const_cast<CEMSocket*>(this)->sendLocker.Lock();

	ASSERT(byConnected == EMS_DISCONNECTED || byConnected == EMS_NOTCONNECTED || byConnected == EMS_CONNECTED);
	CHECK_PTR(m_pUtpLayer);
	CHECK_BOOL(m_bProxyConnectFailed);
	CHECK_PTR(m_pProxyLayer);
	(void)downloadLimit;
	CHECK_BOOL(downloadLimitEnable);
	CHECK_BOOL(pendingOnReceive);
	pendingHeaderSize;
	CHECK_PTR(pendingPacket);
	(void)pendingPacketSize;
	CHECK_ARR(sendbuffer, sendblen);
	(void)sent;
	controlpacket_queue.AssertValid();
	standardpacket_queue.AssertValid();
	CHECK_BOOL(m_currentPacket_is_controlpacket);
	(void)m_numberOfSentBytesCompleteFile;
	(void)m_numberOfSentBytesPartFile;
	(void)m_numberOfSentBytesControlPacket;
	CHECK_BOOL(m_currentPackageIsFromPartFile);
	(void)lastCalledSend;
	(void)m_actualPayloadSize;
	(void)m_actualPayloadSizeSent;

	const_cast<CEMSocket*>(this)->sendLocker.Unlock();
}
#endif

#ifdef _DEBUG
void CEMSocket::Dump(CDumpContext &dc) const
{
	CEncryptedStreamSocket::Dump(dc);
}
#endif

void CEMSocket::DataReceived(const BYTE*, UINT)
{
	ASSERT(0);
}



CString CEMSocket::GetFullErrorMessage(DWORD dwError)
{
	CString strError;

	// Proxy error
	if (!GetLastProxyError().IsEmpty()) {
		strError = GetLastProxyError();
		// If we had a proxy error and the socket error is WSAECONNABORTED, we just 'aborted'
		// the TCP connection ourself - no need to add that self-created error too.
		if (dwError == WSAECONNABORTED)
			return strError;
	}
	// Winsock error
	if (dwError) {
		if (!strError.IsEmpty())
			strError += _T(": ");
		strError += GetErrorMessage(dwError, 1);
	}

	return strError;
}

// increases the send buffer to a bigger size
bool CEMSocket::UseBigSendBuffer()
{
#define BIGSIZE (128 * 1024)

	if (!m_bUseBigSendBuffers) {
		int val = BIGSIZE;
		int oldval;
		int vallen = sizeof oldval;
		if (GetSockOpt(SO_SNDBUF, &oldval, &vallen))
			if (BIGSIZE > oldval) {
				SetSockOpt(SO_SNDBUF, &val, sizeof val);
				vallen = sizeof val;
				m_bUseBigSendBuffers = (GetSockOpt(SO_SNDBUF, &val, &vallen) && val >= BIGSIZE);
#if defined(_DEBUG) || defined(_BETA) || defined(_DEVBUILD)
				if (m_bUseBigSendBuffers)
					theApp.QueueDebugLogLine(false, _T("Increased Sendbuffer for uploading socket from %u KiB to %u KiB"), oldval / 1024, val / 1024);
				else
					theApp.QueueDebugLogLine(false, _T("Failed to increase Sendbuffer for uploading socket, stays at %u KiB"), oldval / 1024);
#endif
			} else
				m_bUseBigSendBuffers = true;
	}
	return m_bUseBigSendBuffers;
}

bool CEMSocket::IsBusyExtensiveCheck()
{
	if (!m_bUseOverlappedSend)
		return m_bBusy;

	CSingleLock lockSend(&sendLocker, TRUE);
	if (!m_bPendingSendOv)
		return false;
	DWORD dwTransferred, dwFlags;
	if (WSAGetOverlappedResult(GetSocketHandle(), &m_PendingSendOperation, &dwTransferred, FALSE, &dwFlags)) {
		CleanUpOverlappedSendOperation(false);
		OnSend(0);
		return false;
	}
	int nError = WSAGetLastError();
	if (nError == WSA_IO_INCOMPLETE)
		return true;
	CleanUpOverlappedSendOperation(true);
	theApp.QueueDebugLogLineEx(LOG_ERROR, _T("WSAGetOverlappedResult return error: %s"), (LPCTSTR)GetErrorMessage(nError));
	return false;
}

// won't always deliver the proper result (sometimes reports busy even if it isn't any more
// and thread related errors) but doesn't need locks or function calls
bool CEMSocket::IsBusyQuickCheck() const
{
	if (!m_bUseOverlappedSend && m_pUtpLayer != NULL)
		return false;
	return m_bUseOverlappedSend ? m_bPendingSendOv : m_bBusy;
}

void CEMSocket::CleanUpOverlappedSendOperation(bool bCancel)
{
//sendLock must be locked by the caller!
	if (m_bPendingSendOv) {
		m_bPendingSendOv = false;
		if (bCancel && CancelIo((HANDLE)GetSocketHandle()))
			for (int i = 5; --i >= 0;) { //use counter and sleep(), because this may loop forever
				DWORD dwTransferred, dwFlags;
				if (WSAGetOverlappedResult(GetSocketHandle(), &m_PendingSendOperation, &dwTransferred, FALSE, &dwFlags))
					break;
				if (WSAGetLastError() != WSA_IO_INCOMPLETE)
					break;
				::Sleep(20);
			};

		for (INT_PTR i = m_aBufferSend.GetCount(); --i >= 0;)
			delete[] m_aBufferSend[i].buf;
		m_aBufferSend.RemoveAll();
	}
}

bool CEMSocket::HasQueues(bool bOnlyStandardPackets) const
{
	// not trustworthy threaded? but it's OK if we don't get the correct result now and then
	return sendbuffer != NULL || !standardpacket_queue.IsEmpty() || (!controlpacket_queue.IsEmpty() && !bOnlyStandardPackets);
}

bool CEMSocket::IsEnoughFileDataQueued(uint32 nMinFilePayloadBytes) const
{
	// check we have at least nMinFilePayloadBytes Payload data in our standardqueue
	for (POSITION pos = standardpacket_queue.GetHeadPosition(); pos != NULL;) {
		uint32 actualsize = standardpacket_queue.GetNext(pos).actualPayloadSize;
		if (actualsize > nMinFilePayloadBytes)
			return true;
		nMinFilePayloadBytes -= actualsize;
	}
	return false;
}

bool CEMSocket::IsEncryptionLayerReady()
{
	// Guard uTP traffic until the initial Hello/HelloAnswer exchange completes.
	if (HaveUtpLayer()) {
		CClientReqSocket* pReqSock = DYNAMIC_DOWNCAST(CClientReqSocket, this);
		CUpDownClient* pClient = (pReqSock != NULL) ? pReqSock->client : NULL;
		if (pClient != NULL && pClient->IsHelloAnswerPending() && !pClient->IsUtpHelloQueued())
			return false;
		return true;
	}
	
	// For TCP connections, use standard encryption check
	return CEncryptedStreamSocket::IsEncryptionLayerReady();
}
