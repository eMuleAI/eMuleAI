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
#include "UploadBandwidthThrottler.h" // ZZ:UploadBandWithThrottler (UDP)
#include "EncryptedDatagramSocket.h"
#include <map>
#include <vector>
#include "eMuleAI/Address.h"
#include "eMuleAI/UtpSocket.h"

class Packet;
class CUpDownClient;

#pragma pack(push, 1)
struct UDPPack
{
	Packet	*packet;
	//uint32	dwIP;
	CAddress IP;
	uint16	nPort;
	DWORD	dwTime;
	bool	bEncrypt;
	bool	bKad;
	uint32	nReceiverVerifyKey;
	uchar	pachTargetClientHashORKadID[16];
	//uint16 nPriority; We could add a priority system here to force some packets.
};
#pragma pack(pop)

class CClientUDPSocket : public CAsyncSocket, public CEncryptedDatagramSocket, public ThrottledControlSocket, public CUtpSocket // ZZ:UploadBandWithThrottler (UDP)
{
public:
    CClientUDPSocket();
    virtual	~CClientUDPSocket();

	void	SetConnectionEncryption(const CAddress& IP, uint16 nPort, bool bEncrypt, const uchar* pTargetClientHash = NULL);
	byte*	GetHashForEncryption(const CAddress& IP, uint16 nPort);
	bool	IsObfusicating(const CAddress& IP, uint16 nPort) { return GetHashForEncryption(IP, nPort) != NULL; }
	void	SendUtpPacket(const byte* data, size_t len, const struct sockaddr* to, socklen_t tolen);
	void	ServiceUtp();
	void	PumpUtpOnce();
	utp_context* GetUtpContext() const { return m_pUtpContext; }
	void	SeedNatTraversalExpectation(CUpDownClient* client, const CAddress& ip, uint16 port);

	bool	Create();
	bool	Rebind();
	uint16	GetConnectedPort()		{ return m_port; }
	DWORD	GetLastRebindTime() const { return m_dwLastRebindTick; }
	// Last public IPs observed at successful rebind (for cooldown bypass logic)
	uint32	GetLastRebindPublicIPv4() const { return m_dwLastRebindPublicIPv4; }
	const CAddress& GetLastRebindPublicIPv6() const { return m_LastRebindPublicIPv6; }
	bool	SendPacket(Packet* packet, const CAddress& IP, uint16 nPort, bool bEncrypt, const uchar* pachTargetClientHashORKadID, bool bKad, uint32 nReceiverVerifyKey);
	SocketSentBytes  SendControlData(uint32 maxNumberOfBytesToSend, uint32 /*minFragSize*/); // ZZ:UploadBandWithThrottler (UDP)
	bool	ProcessPacket(const BYTE* packet, UINT size, uint8 opcode, const CAddress& IP, uint16 port);
	void	ProcessUtpPacket(const BYTE* packet, int size, const struct sockaddr* from, socklen_t fromlen);
protected:

	virtual void	OnSend(int nErrorCode);
	virtual void	OnReceive(int nErrorCode);

private:
	int		SendTo(uchar *lpBuf, int nBufLen, CAddress IP, uint16 nPort);
	bool	IsBusy() const			{ return m_bWouldBlock; }

	utp_context* m_pUtpContext;
	CTypedPtrList<CPtrList, UDPPack*> controlpacket_queue;
	CCriticalSection sendLocker; // ZZ:UploadBandWithThrottler (UDP)

	uint16	m_port;
	bool	m_bWouldBlock;
	DWORD	m_dwLastRebindTick;
	uint32	m_dwLastRebindPublicIPv4;
	CAddress m_LastRebindPublicIPv6;

	struct SIpPort {
		uint16 nPort;
		CAddress IP;
		bool operator< (const SIpPort& Other) const {
			if (IP.GetType() != Other.IP.GetType())
				return IP.GetType() < Other.IP.GetType();
			if (IP.GetType() == CAddress::IPv6)
			{
				if (int cmp = memcmp(IP.Data(), Other.IP.Data(), 16))
					return cmp < 0;
			} else if (IP.GetType() == CAddress::IPv4) {
				uint32 r = IP.ToUInt32(true);
				uint32 l = Other.IP.ToUInt32(true);
				if (r != l)
					return r < l;
			} else {
				ASSERT(0);
				return false;
			}
			return nPort < Other.nPort;
		}
	};

	struct SHash
	{
		byte	UserHash[16];
		uint32	LastUsed;
	};

	std::map<SIpPort, SHash>		m_HashMap;
	std::map<SIpPort, DWORD>		m_KeyFrameSent; // Track last Key Frame send per peer (to avoid spamming on SYN retransmissions)
	CCriticalSection				m_KeyFrameLock; // Protects m_KeyFrameSent
	CCriticalSection				m_HashMapLock; // Protects m_HashMap

	struct SNatTraversalExpectation
	{
		CAddress	IP;
		uint16	Port;
		DWORD	Expires;
		CUpDownClient* Client;
		bool	HasUserHash;
		uchar	UserHash[16];
	};

	std::vector<SNatTraversalExpectation> m_aNatExpectations;
	DWORD	m_dwNextNatExpectCleanup;
	void	RegisterNatExpectation(CUpDownClient* client, const CAddress& ip, uint16 port);
	void	PruneNatExpectations(bool bForce = false);
public:
	CUpDownClient* MatchNatExpectation(const CAddress& ip, uint16 port);
private:

	struct SPendingCallbackInfo
	{
		uchar	FileHash[MDX_DIGEST_SIZE];
		DWORD	Timestamp;
	};

	std::map<SIpPort, SPendingCallbackInfo> m_PendingCallbacks;
	CCriticalSection m_PendingCallbacksLock;

public:
	void	RegisterPendingCallback(const CAddress& receiverIP, uint16 receiverPort, const uchar* fileHash);
	bool	FindPendingCallback(const CAddress& receiverIP, uint16 receiverPort, uchar* outFileHash);
	void	CleanupPendingCallbacks();
};
