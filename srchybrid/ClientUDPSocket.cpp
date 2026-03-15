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
#include "emule.h"
#include "ClientUDPSocket.h"
#include "Packets.h"
#include "UpDownClient.h"
#include "DownloadQueue.h"
#include "Statistics.h"
#include "PartFile.h"
#include "SharedFileList.h"
#include "UploadQueue.h"
#include "Preferences.h"
#include "ClientList.h"
#include "EncryptedDatagramSocket.h"
#include "IPFilter.h"
#include "Listensocket.h"
#include "Log.h"
#include "OtherFunctions.h"
#include "SafeFile.h"
#include "kademlia/kademlia/Kademlia.h"
#include <algorithm>
#include <functional>
#include <eh.h>
#include "kademlia/kademlia/UDPFirewallTester.h"
#include "kademlia/net/KademliaUDPListener.h"
#include "kademlia/io/IOException.h"
#include "kademlia/kademlia/prefs.h"
#include "kademlia/utils/KadUDPKey.h"
#include "zlib/zlib.h"
#include "eMuleAI/UtpSocket.h"
#include "eMuleAI/Shield.h"
#include "kademlia/routing/RoutingZone.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static const DWORD UDPSOCKET_REBIND_COOLDOWN_MS = 60 * 1000;
static const DWORD NAT_EXPECT_TTL_MS = 30 * 1000;

struct CNatSeException
{
	CNatSeException(unsigned int codeVal, UINT_PTR addressVal)
		: code(codeVal)
		, address(addressVal)
	{
	}

	unsigned int code;
	UINT_PTR address;
};

static void __cdecl NatTraversalSeTranslator(unsigned int code, _EXCEPTION_POINTERS* info)
{
	UINT_PTR address = 0;
	if (info != NULL && info->ExceptionRecord != NULL && info->ExceptionRecord->ExceptionAddress != NULL)
		address = reinterpret_cast<UINT_PTR>(info->ExceptionRecord->ExceptionAddress);
	throw CNatSeException(code, address);
}

class CScopedSeTranslator
{
public:
	explicit CScopedSeTranslator(_se_translator_function translator)
		: m_previous(_set_se_translator(translator))
	{
	}

	~CScopedSeTranslator()
	{
		_set_se_translator(m_previous);
	}

private:
	_se_translator_function m_previous;
};

// CClientUDPSocket

CClientUDPSocket::CClientUDPSocket()
:CUtpSocket()
{
	m_bWouldBlock = false;
	m_port = 0;
	m_dwLastRebindTick = 0;
	m_dwLastRebindPublicIPv4 = 0;
	m_LastRebindPublicIPv6 = CAddress();
	m_dwNextNatExpectCleanup = 0;
	UnregisterFromGlobalSet(); // This class is the UDP transport/context owner, not a uTP endpoint; exclude from endpoint set.

	// Initialize single uTP context bound to UDP transport
	m_pUtpContext = utp_init(2);
	utp_context_set_userdata(m_pUtpContext, this);
	CUtpSocket::EnsureCallbacks(m_pUtpContext); // Ensure callbacks are set on context immediately (inbound-only readiness)
}

CClientUDPSocket::~CClientUDPSocket()
{
	theApp.uploadBandwidthThrottler->RemoveFromAllQueuesLocked(this); // ZZ:UploadBandWithThrottler (UDP)
	while (!controlpacket_queue.IsEmpty()) {
		const UDPPack *p = controlpacket_queue.RemoveHead();
		delete p->packet;
		delete p;
	}
	m_aNatExpectations.clear();

	if (m_pUtpContext) {
		CUtpSocket::OnContextDestroyed(m_pUtpContext); // Clear configured-context tracker before destroying the context
		utp_destroy(m_pUtpContext);
		m_pUtpContext = NULL;
	}
}

void CClientUDPSocket::SeedNatTraversalExpectation(CUpDownClient* client, const CAddress& ip, uint16 port)
{
	RegisterNatExpectation(client, ip, port);
}

void CClientUDPSocket::RegisterNatExpectation(CUpDownClient* client, const CAddress& ip, uint16 port)
{
	if (!thePrefs.IsNatTraversalServiceEnabled())
		return;
	if (client == NULL || ip.IsNull() || port == 0)
		return;
	if (!theApp.clientlist->IsValidClient(client))
		return;
	PruneNatExpectations(false);
	DWORD now = ::GetTickCount();
	const bool bNewHasHash = client->HasValidHash();
	std::vector<SNatTraversalExpectation>::iterator existing = std::find_if(m_aNatExpectations.begin(), m_aNatExpectations.end(),
		[&](const SNatTraversalExpectation& entry) -> bool
		{
			return (entry.IP == ip && entry.Port == port);
		});
	if (existing != m_aNatExpectations.end()) {
		if (existing->Client != NULL && !theApp.clientlist->IsValidClient(existing->Client)) {
			existing = m_aNatExpectations.erase(existing);
		} else if (existing->HasUserHash) {
			// Keep hashed expectation to avoid ambiguous endpoint overwrites.
			if (bNewHasHash && memcmp(existing->UserHash, client->GetUserHash(), 16) == 0)
				existing->Client = client;
			existing->Expires = now + NAT_EXPECT_TTL_MS;
			return;
		} else if (!bNewHasHash) {
			existing->Expires = now + NAT_EXPECT_TTL_MS;
			return;
		} else {
			m_aNatExpectations.erase(existing);
		}
	}
	SNatTraversalExpectation entry;
	entry.IP = ip;
	entry.Port = port;
	entry.Expires = now + NAT_EXPECT_TTL_MS;
	entry.Client = client;
	entry.HasUserHash = bNewHasHash;
	if (entry.HasUserHash)
		memcpy(entry.UserHash, client->GetUserHash(), 16);
	else
		md4clr(entry.UserHash);
	m_aNatExpectations.push_back(entry);
	if (m_aNatExpectations.size() > 64)
		PruneNatExpectations(true);
}

CUpDownClient* CClientUDPSocket::MatchNatExpectation(const CAddress& ip, uint16 port)
{
	if (!thePrefs.IsNatTraversalServiceEnabled())
		return NULL;
	PruneNatExpectations(false);
	uint16 window = thePrefs.GetNatTraversalPortWindow();
	DWORD now = ::GetTickCount();
	auto ResolveExpectation = [&](std::vector<SNatTraversalExpectation>::iterator& it) -> CUpDownClient* {
		SNatTraversalExpectation entry = *it;
		CUpDownClient* client = entry.Client;
		const bool bExpectHasHash = entry.HasUserHash;
		if (client != NULL && !theApp.clientlist->IsValidClient(client))
			client = NULL;
		if (client == NULL && entry.HasUserHash)
			client = theApp.clientlist->FindClientByUserHash(entry.UserHash, ip, 0);
		if (client == NULL)
			client = theApp.clientlist->FindClientByIP_KadPort(ip, port);
		if (client == NULL) {
			bool bMulti = false;
			client = theApp.downloadqueue->GetDownloadClientByIP_UDP(ip, port, true, &bMulti);
			if (client == NULL)
				client = theApp.downloadqueue->GetDownloadClientByIP_UDP(ip, port, true);
		}
		if (client == NULL) {
			bool bUploadMulti = false;
			client = theApp.uploadqueue->GetWaitingClientByIP_UDP(ip, port, true, &bUploadMulti);
			if (client == NULL)
				client = theApp.uploadqueue->GetWaitingClientByIP_UDP(ip, port, true);
		}
		if (client == NULL)
			client = theApp.clientlist->FindUniqueClientByIP(ip);
		if (client != NULL) {
			if (bExpectHasHash) {
				if (client->HasValidHash()) {
					if (memcmp(client->GetUserHash(), entry.UserHash, 16) != 0) {
						++it;
						return NULL;
					}
				} else {
					client->SetUserHash(entry.UserHash, true);
				}
			}
			it->Client = client;
			it->Port = port;
			it->Expires = now + NAT_EXPECT_TTL_MS;
			if (client->HasValidHash()) {
				it->HasUserHash = true;
				memcpy(it->UserHash, client->GetUserHash(), 16);
			} else {
				it->HasUserHash = false;
				md4clr(it->UserHash);
			}

			return client;
		}
		it = m_aNatExpectations.erase(it);
		return NULL;
	};

	// Prefer exact IP:port match to avoid ambiguous mappings on shared public IPs.
	for (std::vector<SNatTraversalExpectation>::iterator it = m_aNatExpectations.begin(); it != m_aNatExpectations.end();) {
		if (!(it->IP == ip) || it->Port != port) {
			++it;
			continue;
		}
		CUpDownClient* client = ResolveExpectation(it);
		if (client != NULL)
			return client;
		if (it == m_aNatExpectations.end())
			break;
	}

	if (window == 0)
		return NULL;

	int bestDiff = 0x7FFFFFFF;
	int matchCount = 0;
	std::vector<SNatTraversalExpectation>::iterator bestIt = m_aNatExpectations.end();
	for (std::vector<SNatTraversalExpectation>::iterator it = m_aNatExpectations.begin(); it != m_aNatExpectations.end(); ++it) {
		if (!(it->IP == ip))
			continue;
		int diff = (int)it->Port - (int)port;
		if (diff < 0)
			diff = -diff;
		if ((uint16)diff <= window) {
			++matchCount;
			if (diff < bestDiff) {
				bestDiff = diff;
				bestIt = it;
			}
		}
	}

	if (matchCount == 0)
		return NULL;
	if (matchCount > 1) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] MatchNatExpectation: Ambiguous port-window match for %s:%u (matches=%d), skipping"), (LPCTSTR)ipstr(ip), (UINT)port, matchCount);
		return NULL;
	}

	return ResolveExpectation(bestIt);
}

void CClientUDPSocket::PruneNatExpectations(bool bForce)
{
	if (m_aNatExpectations.empty())
		return;
	DWORD now = ::GetTickCount();
	if (!bForce && m_dwNextNatExpectCleanup != 0 && (int)(now - m_dwNextNatExpectCleanup) < 0)
		return;
	m_dwNextNatExpectCleanup = now + 1000;
	m_aNatExpectations.erase(std::remove_if(m_aNatExpectations.begin(), m_aNatExpectations.end(),
		[&](const SNatTraversalExpectation& entry) -> bool
		{
			bool validClient = (entry.Client != NULL) && theApp.clientlist->IsValidClient(entry.Client);
			if (validClient)
				return (int)(now - entry.Expires) >= 0;
			if (entry.HasUserHash)
				return (int)(now - entry.Expires) >= 0;
			return true;
		}), m_aNatExpectations.end());
}

void CClientUDPSocket::SetConnectionEncryption(const CAddress& IP, uint16 nPort, bool bEncrypt, const uchar* pTargetClientHash) {
	SIpPort IpPort = { nPort, IP };
	CSingleLock _lkHM(&m_HashMapLock, TRUE);
	std::map<SIpPort, SHash>::iterator I = m_HashMap.find(IpPort);
	if (bEncrypt) {
		if (I == m_HashMap.end()) {
			SHash Hash;
			if (pTargetClientHash)
				md4cpy(Hash.UserHash, pTargetClientHash);
			else
				md4clr(Hash.UserHash);

			I = m_HashMap.insert(std::map<SIpPort, SHash>::value_type(IpPort, Hash)).first;
		}

		I->second.LastUsed = ::GetTickCount();
	} else if (I != m_HashMap.end())
		m_HashMap.erase(I);
}

byte* CClientUDPSocket::GetHashForEncryption(const CAddress& IP, uint16 nPort) {
	SIpPort IpPort = { nPort, IP };
	CSingleLock _lkHM(&m_HashMapLock, TRUE);
	std::map<SIpPort, SHash>::iterator I = m_HashMap.find(IpPort);

	if (I == m_HashMap.end())
		return NULL;

	I->second.LastUsed = ::GetTickCount();
	// Note: if we don't know how to encrypt but have got a incoming encrypted packet, 
	// we use our own hash to encrypt as we expect the remote side to know it and try it.
	return I->second.UserHash;
}

void CClientUDPSocket::SendUtpPacket(const byte* data, size_t len, const struct sockaddr* to, socklen_t tolen)
{
	if (!thePrefs.IsEnableNatTraversal())
		return;
	CAddress IP;
	uint16 nPort;
	IP.FromSA(to, tolen, &nPort);
	byte* pTargetClientHash = GetHashForEncryption(IP, nPort);

	ASSERT(len >= 4);
	// Safe byte-based uTP type extraction: first byte = [version (lower 4 bits)] | [type (upper 4 bits)]
	uint8 b0 = data[0];
	uint8 utp_type = (uint8)((b0 >> 4) & 0x0F);  // Extract upper 4 bits for type
	// On SYN, proactively send our Key Frame once per peer within short TTL
	if (utp_type == 4) { // ST_SYN
		SIpPort key = { nPort, IP };
		DWORD now = ::GetTickCount();
		bool sendAllowed = true;

		CSingleLock _lkKF(&m_KeyFrameLock, TRUE);
		auto it = m_KeyFrameSent.find(key);
		if (it != m_KeyFrameSent.end())
			sendAllowed = (int)(now - it->second) >= 0; // expired
		if (sendAllowed) {
			CSafeMemFile data_out(128);
			data_out.WriteHash16(thePrefs.GetUserHash());
			Packet* packet = new Packet(data_out, OP_UDPRESERVEDPROT2);
			packet->opcode = 0xFF; // Key Frame
			theStats.AddUpDataOverheadOther(packet->size);
			// If we do not yet have a target key, send unencrypted
			SendPacket(packet, IP, nPort, pTargetClientHash != NULL, pTargetClientHash, false, 0);
			m_KeyFrameSent[key] = now + 10000; // 10s TTL
			if (thePrefs.GetLogNatTraversalEvents())
				AddDebugLogLine(DLP_LOW, false, _T("[NAT-T][uTP] Sent KeyFrame to %s:%u"), (LPCTSTR)ipstr(IP), (UINT)nPort);
		}
	}

	Packet* frame = new Packet(OP_UDPRESERVEDPROT2);
	frame->opcode = 0x00; // UTP Frame
	frame->pBuffer = new char[len];
	memcpy(frame->pBuffer, data, len);
	frame->size = len;

	SendPacket(frame, IP, nPort, pTargetClientHash != NULL, pTargetClientHash, false, 0);

}

void CClientUDPSocket::ServiceUtp()
{
	CSingleLock runtimeLock(&CUtpSocket::GetRuntimeLock(), TRUE);
	if (m_pUtpContext) {
		utp_issue_deferred_acks(m_pUtpContext);
		utp_check_timeouts(m_pUtpContext);
	}

	// Process pending writes for all uTP sockets
	CUtpSocket::Process();

	// Prune expired Key Frame entries to prevent unbounded growth.
	if (!m_KeyFrameSent.empty()) {
		DWORD now = ::GetTickCount();

		CSingleLock _lkKF(&m_KeyFrameLock, TRUE);
		for (std::map<SIpPort, DWORD>::iterator it = m_KeyFrameSent.begin(); it != m_KeyFrameSent.end(); ) {
			if ((int)(now - it->second) >= 0)
				it = m_KeyFrameSent.erase(it);
			else
				++it;
		}
	}

	// Periodically prune stale encryption hashes to avoid growth
	static const DWORD kHashTtlMs = 10 * 60 * 1000; // 10 minutes
	DWORD now = ::GetTickCount();
	CSingleLock _lkHM(&m_HashMapLock, TRUE);
	for (std::map<SIpPort, SHash>::iterator it = m_HashMap.begin(); it != m_HashMap.end(); ) {
		if ((int)(now - it->second.LastUsed) >= (int)kHashTtlMs)
			it = m_HashMap.erase(it);
		else
			++it;
	}

	// Cleanup expired pending callbacks
	static DWORD dwLastCallbackCleanup = 0;
	if (now - dwLastCallbackCleanup > 30000) { // Every 30 seconds
		CleanupPendingCallbacks();
		dwLastCallbackCleanup = now;
	}
}

void CClientUDPSocket::PumpUtpOnce()
{
	if (!thePrefs.IsEnableNatTraversal())
		return;
	CSingleLock runtimeLock(&CUtpSocket::GetRuntimeLock(), TRUE);
	if (m_pUtpContext) {
		utp_issue_deferred_acks(m_pUtpContext);
		utp_check_timeouts(m_pUtpContext);
	}
	CUtpSocket::Process();
}

void CClientUDPSocket::OnReceive(int nErrorCode)
{
	if (nErrorCode) {
		if (thePrefs.GetVerbose())
			DebugLogError(_T("Error: Client UDP socket, error on receive event: %s"), (LPCTSTR)EscPercent(GetErrorMessage(nErrorCode, 1)));
	}

	BYTE buffer[8192]; //5000 was too low sometimes
	SOCKADDR_IN6 sockAddr = { 0 };
	int iSockAddrLen = sizeof sockAddr;
	int nRealLen = ReceiveFrom(buffer, sizeof buffer, (SOCKADDR*)&sockAddr, &iSockAddrLen);
	CAddress IP;
	uint16 nPort;
	IP.FromSA((SOCKADDR*)&sockAddr, iSockAddrLen, &nPort);
	IP.Convert(CAddress::IPv4); // check if its a maped IPv4 address

	// Debug logging for all received UDP packets to diagnose NAT traversal issues

	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NATTTESTMODE: UDPRecv] from %s:%u len=%d\n"), (LPCTSTR)ipstr(IP), (unsigned)nPort, nRealLen);

	if (nRealLen == SOCKET_ERROR)
	{
		// netfinity: Reordered code so it actually does something!
		DWORD const dwError = WSAGetLastError();
		if (dwError == WSAECONNRESET)
		{
			// Depending on local and remote OS and depending on used local (remote?) router we may receive
			// WSAECONNRESET errors. According some KB articles, this is a special way of winsock to report 
			// that a sent UDP packet was not received by the remote host because it was not listening on 
			// the specified port -> no eMule running there.
			//
			// netfinity: To get some stats!
			CString strClientInfo;
			if (iSockAddrLen > 0 && IP.ToUInt32(false) != 0 && IP.ToUInt32(false) != UINT_MAX)
				strClientInfo.Format(_T(" from %s:%u"), ipstr(IP), nPort);
			DebugLog(_T("Client UDP socket unreachable, ICMP message%s"), (LPCTSTR)EscPercent(strClientInfo));

			// FastKAD: Process ICMP unreachable events
			Kademlia::CKademlia::ProcessICMPUnreachable(IP.ToUInt32(true), nPort);
		} else if (thePrefs.GetVerbose() /*&& dwError != WSAECONNRESET*/) {
			CString strClientInfo;
			if (iSockAddrLen > 0 && IP.ToUInt32(false) != 0 && IP.ToUInt32(false) != UINT_MAX)
				strClientInfo.Format(_T(" from %s:%u"), ipstr(IP), nPort);
			DebugLogError(_T("Error: Client UDP socket, failed to receive data%s: %s"), (LPCTSTR)EscPercent(strClientInfo), (LPCTSTR)EscPercent(GetErrorMessage(dwError, 1)));
		}
		return;
	}

	if (theApp.ipfilter->IsFiltered(IP) || theApp.clientlist->IsBannedClient(IP.ToStringC()))
		return;

	BYTE *pBuffer;
	uint32 nReceiverVerifyKey;
	uint32 nSenderVerifyKey;
	int nPacketLen = DecryptReceivedClient(buffer, nRealLen, &pBuffer, IP, &nReceiverVerifyKey, &nSenderVerifyKey);
	if (nPacketLen > 0) {
		CString strError;
		try {
			switch (pBuffer[0]) {
			case OP_EMULEPROT:
				if (nPacketLen < 2)
					strError = _T("eMule packet too short");
				else
					ProcessPacket(pBuffer + 2, nPacketLen - 2, pBuffer[1], IP, nPort);
				break;
			case OP_KADEMLIAPACKEDPROT:
				theStats.AddDownDataOverheadKad(nPacketLen);
				if (nPacketLen < 2)
					strError = _T("Kad packet (compressed) too short");
				else {
					BYTE* unpack = NULL;
					uLongf unpackedsize = 0;
					uint32 nNewSize = nPacketLen * 10 + 300;
					int iZLibResult = Z_OK;
					do {
						delete[] unpack;
						unpack = new BYTE[nNewSize];
						unpackedsize = nNewSize - 2;
						iZLibResult = uncompress(unpack + 2, &unpackedsize, pBuffer + 2, nPacketLen - 2);
						nNewSize *= 2; // size for the next try if needed
					} while (iZLibResult == Z_BUF_ERROR && nNewSize < 250000);

					if (iZLibResult != Z_OK) {
						delete[] unpack;
						strError.Format(_T("Failed to uncompress Kad packet: zip error: %d (%hs)"), iZLibResult, zError(iZLibResult));
					} else {
						unpack[0] = OP_KADEMLIAHEADER;
						unpack[1] = pBuffer[1];
						try {
							Kademlia::CKademlia::ProcessPacket(unpack, unpackedsize + 2
								, IP.ToUInt32(true), nPort
								, (Kademlia::CPrefs::GetUDPVerifyKey(IP.ToUInt32(false)) == nReceiverVerifyKey)
								, Kademlia::CKadUDPKey(nSenderVerifyKey, theApp.GetPublicIPv4()));
						} catch (...) {
							delete[] unpack;
							throw;
						}
						delete[] unpack;
					}
				}
				break;
			case OP_KADEMLIAHEADER:
				theStats.AddDownDataOverheadKad(nPacketLen);
				//note: emule 0.48a can not send a packet with KADEMLIA_FIREWALLED2_REQ tag
				if(thePrefs.IsDetectFakeEmule()) { // IsHarderPunishment isn't necessary here since the cost is low
					byte byOpcode = pBuffer[1];
					if(byOpcode == KADEMLIA_FIREWALLED2_REQ) {
						CUpDownClient* client = theApp.clientlist->FindClientByIP(IP);
						if(client != NULL && client->GetClientSoft() == SO_EMULE && client->GetVersion() != 0 && client->GetVersion() < MAKE_CLIENT_VERSION(0, 49, 0)) {
							theApp.shield->SetPunishment(client,GetResString(_T("PUNISHMENT_REASON_FAKE_EMULE_VERSION_VAGAA")), PR_FAKEMULEVERSIONVAGAA);
							break;
						}
					}
				}
				if (nPacketLen < 2)
					strError = _T("Kad packet too short");
				else
					Kademlia::CKademlia::ProcessPacket(pBuffer, nPacketLen, IP.ToUInt32(true), nPort
						, (Kademlia::CPrefs::GetUDPVerifyKey(IP.ToUInt32(false)) == nReceiverVerifyKey)
						, Kademlia::CKadUDPKey(nSenderVerifyKey, theApp.GetPublicIPv4()));
				break;
				case OP_UDPRESERVEDPROT2:
				{
					if (!thePrefs.IsNatTraversalServiceEnabled())
						break;

					// Note: here we dont have opcodes, just [uint8 - Prot][n bytes - data]
					if (nPacketLen >= 2) {
					if (pBuffer[1] == 0x00) // UTP Frame
						ProcessUtpPacket(pBuffer+2, nPacketLen-2, (struct sockaddr*)&sockAddr, iSockAddrLen);
					else if (pBuffer[1] == 0xFF) { // Key Frame
						int size = nPacketLen - 2;
						const BYTE* packet = pBuffer + 2;
						if (size < 16)
							throw CString(_T("Key packet too short"));

						theStats.AddUpDataOverheadOther(size);
						SetConnectionEncryption(IP, nPort, true, packet);
					}
				} else
					throw CString(_T("Utp packet too short"));
			}
			break;
			default:
				strError.Format(_T("Unknown protocol 0x%02x"), pBuffer[0]);
			}
			//code above does not need to throw strError
		} catch (CFileException *ex) {
			ex->Delete();
			strError = _T("Invalid packet received");
		} catch (CMemoryException *ex) {
			ex->Delete();
			strError = _T("Memory exception");
		} catch (const CString &ex) {
			strError = ex;
		} catch (Kademlia::CIOException *ex) {
			ex->Delete();
			strError = _T("Invalid packet received");
		} catch (CException *ex) {
			ex->Delete();
			strError = _T("General packet error");
#ifndef _DEBUG
		} catch (...) {
			strError = _T("Unknown exception");
			ASSERT(0);
#endif
		}
		if (thePrefs.GetVerbose() && !strError.IsEmpty()) {
			CString strClientInfo;
			CUpDownClient *client;
			if (pBuffer[0] == OP_EMULEPROT)
				client = theApp.clientlist->FindClientByIP_UDP(IP, nPort);
			else
				client = theApp.clientlist->FindClientByIP_KadPort(IP, nPort);
			if (client)
				strClientInfo = client->DbgGetClientInfo();
			else
				strClientInfo.Format(_T("%s:%hu"), ipstr(IP), nPort);
			DebugLogWarning(_T("Client UDP socket: prot=0x%02x  opcode=0x%02x  sizeaftercrypt=%u realsize=%u  %s: %s"), pBuffer[0], pBuffer[1], nPacketLen, nRealLen, (LPCTSTR)EscPercent(strError), (LPCTSTR)EscPercent(strClientInfo));
		}
	} else if (nPacketLen == SOCKET_ERROR) {
		DWORD dwError = WSAGetLastError();
		if (dwError == WSAECONNRESET) {
			// Depending on local and remote OS and depending on used local (remote?) router we may receive
			// WSAECONNRESET errors. According to some KB articles, this is a special way of winsock to report
			// that a sent UDP packet was not received by the remote host because it was not listening on
			// the specified port -> no eMule running there.
			//
			// TODO: So, actually we should do something with this information and drop the related Kad node
			// or eMule client...
			;
		} else if (thePrefs.GetVerbose()) {
			CString strClientInfo;
			if (iSockAddrLen > 0 && !IP.IsNull())
				strClientInfo.Format(_T(" from %s:%u"), ipstr(IP), nPort);
			DebugLogError(_T("Error: Client UDP socket, failed to receive data%s: %s"), (LPCTSTR)EscPercent(strClientInfo), (LPCTSTR)EscPercent(GetErrorMessage(dwError, 1)));
		}
	}
}

bool CClientUDPSocket::ProcessPacket(const BYTE *packet, UINT size, uint8 opcode, const CAddress& ip, uint16 port)
{
	// Debug logging for HOLEPUNCH and NAT-related packets
	if (opcode == OP_HOLEPUNCH || opcode == OP_REASKCALLBACKUDP || opcode == OP_REASKFILEPING) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] ProcessPacket: opcode=0x%02X from %s:%u size=%u"), opcode, (LPCTSTR)ipstr(ip), (UINT)port, size);
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NATTTESTMODE: ProcessPacket] opcode=0x%02X from %s:%u size=%u\n"), opcode, (LPCTSTR)ipstr(ip), (unsigned)port, (unsigned)size);
	}
	
	switch (opcode) {
	case OP_REASKCALLBACKUDP:
		{
			if (thePrefs.GetDebugClientUDPLevel() > 0)
				DebugRecv("OP_ReaskCallbackUDP", NULL, NULL, ip);
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NAT-T][Buddy-Recv] OP_REASKCALLBACKUDP received from %s:%u size=%u"), (LPCTSTR)ipstr(ip), port, size);

			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NATTTESTMODE: ReaskCB-UDP] from %s:%u size=%u\n"), (LPCTSTR)ipstr(ip), (unsigned)port, (unsigned)size);
			theStats.AddDownDataOverheadOther(size);

			// Accept UDP reask-callbacks for our own serving buddy or any served buddy and forward via TCP.
			if (size < 17) {
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLogWarning(_T("[NAT-T][Buddy-Recv] OP_REASKCALLBACKUDP packet too small: size=%u < 17"), size);
				break;
			}

			CUpDownClient* pForwardTarget = NULL;
			CUpDownClient* pSenderClient = theApp.clientlist->FindClientByIP(ip);
			
			// First, try to match against any served buddy by KadID
			Kademlia::CUInt128 kadid(packet);
			pForwardTarget = theApp.clientlist->FindServedBuddyByKadID(kadid);
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NAT-T][Buddy-Match] FindServedBuddyByKadID(%s) = %p"), (LPCTSTR)md4str(packet), pForwardTarget);
			// Exclude sender from forward target to prevent loop
			if (pForwardTarget != NULL && pForwardTarget == pSenderClient) {
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NAT-T][Buddy-Match] Excluded sender from forward target (loop prevention)"));
				pForwardTarget = NULL;
			}
			if (pForwardTarget == NULL) {
				Kademlia::CUInt128 alt(kadid);
				alt.Xor(Kademlia::CUInt128(true));
				pForwardTarget = theApp.clientlist->FindServedBuddyByKadID(alt);
				if (pForwardTarget != NULL) {
					// Exclude sender from forward target to prevent loop
					if (pForwardTarget == pSenderClient) {
						if (thePrefs.GetLogNatTraversalEvents())
							DebugLog(_T("[NAT-T][Buddy-Match] Excluded sender from forward target (complement, loop prevention)"));
						pForwardTarget = NULL;
					} else if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal] OP_ReaskCallbackUDP: matched sbid via complement for %s"), (LPCTSTR)EscPercent(pForwardTarget->DbgGetClientInfo()));
				}
			}

			// If not a served buddy, fall back to our own serving buddy if it matches
			if (pForwardTarget == NULL) {
				CUpDownClient* ownServingBuddy = theApp.clientlist->GetServingBuddy();
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NAT-T][Buddy-Match] GetServingBuddy() = %p"), ownServingBuddy);
				if (ownServingBuddy && md4equ(packet, ownServingBuddy->GetServingBuddyID())) {
					// Exclude sender from forward target to prevent loop
					if (ownServingBuddy == pSenderClient) {
						if (thePrefs.GetLogNatTraversalEvents())
							DebugLog(_T("[NAT-T][Buddy-Match] Excluded sender from serving buddy forward target (loop prevention)"));
					} else {
						pForwardTarget = ownServingBuddy;
						if (thePrefs.GetLogNatTraversalEvents())
							DebugLog(_T("[NAT-T][Buddy-Match] Matched own serving buddy: %s"), (LPCTSTR)EscPercent(pForwardTarget->DbgGetClientInfo()));
					}
				}
			}

			if (pForwardTarget == NULL) {
				CUpDownClient* byIP = theApp.clientlist->FindClientByIP(ip);
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NAT-T][Buddy-Match] FindClientByIP(%s) = %p, IsServedBuddy=%d"), 
						(LPCTSTR)ipstr(ip), byIP, (byIP && theApp.clientlist->IsServedBuddy(byIP)) ? 1 : 0);
				if (byIP != NULL && theApp.clientlist->IsServedBuddy(byIP)) {
					// This should not happen because we already checked sender above, but keep for safety
					if (byIP != pSenderClient) {
						pForwardTarget = byIP;
						if (thePrefs.GetLogNatTraversalEvents())
							DebugLog(_T("[NatTraversal] OP_ReaskCallbackUDP: matched served buddy by IP %s"), (LPCTSTR)EscPercent(byIP->DbgGetClientInfo()));
					}
				}
			}
			// Require Kad capability on the forward target (compatibility and safety).
			if (pForwardTarget == NULL) {
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal] OP_ReaskCallbackUDP: no served buddy match for sbid=%s from %s:%u"),
						(LPCTSTR)md4str(packet), (LPCTSTR)ipstr(ip), port);
			}
			if (pForwardTarget == NULL || pForwardTarget->GetKadVersion() == 0) {
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLogWarning(_T("[NAT-T][Buddy-Forward] FAILED: forward target missing or KadVersion==0; pForwardTarget=%p KadVer=%d"), 
						pForwardTarget, pForwardTarget ? pForwardTarget->GetKadVersion() : 0);
				if (thePrefs.GetDebugClientUDPLevel() > 0)
					DebugLog(_T("OP_ReaskCallbackUDP: forward target missing or KadVersion==0; dropping."));
				break;
			}

				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NATTTESTMODE: ReaskCB-UDP] forward TCP (compat) to %s\n"), (LPCTSTR)pForwardTarget->DbgGetClientInfo());
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NAT-T][Buddy-Forward] Forwarding via SafeConnectAndSendPacket to %s (socket=%p connected=%d)"),
						(LPCTSTR)EscPercent(pForwardTarget->DbgGetClientInfo()),
						pForwardTarget->socket,
						(pForwardTarget->socket != NULL && pForwardTarget->socket->IsConnected()) ? 1 : 0);

				// Build standard OP_REASKCALLBACKTCP payload: prepend requester endpoint then append custom data.
				if (size >= 16) {
					const UINT kBuddyIdLen = 16;
					const UINT kNullHashLen = 16;
					const UINT kUserHashLen = 16;
					const UINT kCustomMetaLen = kNullHashLen + 1 + kUserHashLen + 1;
					UINT extOffset = kBuddyIdLen + kCustomMetaLen;

					bool bCustomMarker = true;
					if (size >= kBuddyIdLen + kNullHashLen) {
						for (UINT i = 0; i < kNullHashLen; ++i) {
							if (*(packet + kBuddyIdLen + i) != 0) {
								bCustomMarker = false;
								break;
							}
						}
					} else
						bCustomMarker = false;
					if (bCustomMarker && size >= extOffset + MDX_DIGEST_SIZE + 6)
						extOffset += MDX_DIGEST_SIZE;

					// Buddy-observed endpoint (used for backward compatibility as primary endpoint)
					CAddress buddyObservedIP = ip;
					uint16 buddyObservedPort = port;

					// Requester self-reported endpoint (preferred for NAT-T/uTP)
					CAddress requesterIP;
					uint16 requesterPort = 0;
					if (size >= extOffset + 6) {
						uint32 extIPv4 = PeekUInt32(packet + extOffset);
						uint16 extPort = PeekUInt16(packet + extOffset + 4);
						CAddress extAddr(CAddress(extIPv4, false));
						if (!extAddr.IsNull() && extAddr.IsPublicIP() && extPort != 0) {
							requesterIP = extAddr;
							requesterPort = extPort;
						}
					}

					// Use requester self-reported endpoint if available, otherwise buddy-observed
					CAddress forwardIP = !requesterIP.IsNull() ? requesterIP : buddyObservedIP;
					uint16 forwardPort = requesterPort != 0 ? requesterPort : buddyObservedPort;
					if (forwardPort == 0)
						forwardPort = port;
					if (!forwardIP.IsNull())
						pForwardTarget->SetLastCallbackRequesterIP(forwardIP);

					bool bUseIPv6 = (forwardIP.GetType() == CAddress::IPv6);
					UINT headerLen = bUseIPv6 ? (4 + 16 + 2) : 6;
					// Add 6 bytes for requester self-reported endpoint hint
					UINT hintLen = (!requesterIP.IsNull() && requesterPort != 0 && !(requesterIP == forwardIP && requesterPort == forwardPort)) ? 6 : 0;
					Packet* response = new Packet(OP_EMULEPROT);
					response->opcode = OP_REASKCALLBACKTCP;
					response->size = (size - kBuddyIdLen) + headerLen + hintLen;
					response->pBuffer = new char[response->size];
					uchar* out = reinterpret_cast<uchar*>(response->pBuffer);
					if (bUseIPv6) {
						PokeUInt32(out, (uint32)0xFFFFFFFF);
						memcpy(out + 4, forwardIP.Data(), 16);
						PokeUInt16(out + 20, forwardPort);
						memcpy(out + 22, packet + kBuddyIdLen, size - kBuddyIdLen);
						if (hintLen > 0) {
							// Add hint at the end
							PokeUInt32(out + 22 + (size - kBuddyIdLen), requesterIP.ToUInt32(false));
							PokeUInt16(out + 22 + (size - kBuddyIdLen) + 4, requesterPort);
						}
					} else {
						PokeUInt32(out, forwardIP.IsNull() ? 0u : forwardIP.ToUInt32(false));
						PokeUInt16(out + 4, forwardPort);
						memcpy(out + 6, packet + kBuddyIdLen, size - kBuddyIdLen);
						if (hintLen > 0) {
							// Add hint at the end
							PokeUInt32(out + 6 + (size - kBuddyIdLen), requesterIP.ToUInt32(false));
							PokeUInt16(out + 6 + (size - kBuddyIdLen) + 4, requesterPort);
						}
					}

					CString strForwardTarget = pForwardTarget->DbgGetClientInfo();
					if (thePrefs.GetLogNatTraversalEvents()) {
						if (hintLen > 0)
							DebugLog(_T("[NatTraversal] OP_ReaskCallbackUDP: forward to %s:%u (hint: %s:%u) via buddy %s"), (LPCTSTR)ipstr(forwardIP), forwardPort, (LPCTSTR)ipstr(requesterIP), requesterPort, (LPCTSTR)EscPercent(strForwardTarget));
						else
							DebugLog(_T("[NatTraversal] OP_ReaskCallbackUDP: forward to %s:%u via buddy %s"), (LPCTSTR)ipstr(forwardIP), forwardPort, (LPCTSTR)EscPercent(strForwardTarget));
					}
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NATTTESTMODE: ReaskCB-UDP] forward dest %s:%u\n"), (LPCTSTR)ipstr(forwardIP), (unsigned)forwardPort);
					if (thePrefs.GetDebugClientTCPLevel() > 0)
						DebugSend("OP_ReaskCallbackTCP", pForwardTarget);

					const UINT uForwardPacketSize = response->size;
					theStats.AddUpDataOverheadFileRequest(uForwardPacketSize);
					const bool bForwardQueued = pForwardTarget->SafeConnectAndSendPacket(response);
					if (thePrefs.GetLogNatTraversalEvents()) {
						if (bForwardQueued)
							DebugLog(_T("[NatTraversal] OP_REASKCALLBACKTCP forwarded/queued successfully (size=%u) to %s"), uForwardPacketSize, (LPCTSTR)EscPercent(strForwardTarget));
						else
							DebugLogWarning(_T("[NAT-T][Buddy-Forward] FAILED: SafeConnectAndSendPacket rejected callback forward to %s"), (LPCTSTR)EscPercent(strForwardTarget));
					}

					// Also send UDP HOLEPUNCH to requester for NAT hole opening (critical for low-to-low transfers).
					if (!forwardIP.IsNull() && forwardPort != 0) {
						Packet *holepunch = new Packet(OP_EMULEPROT);
						holepunch->opcode = OP_HOLEPUNCH;
						SendPacket(holepunch, forwardIP, forwardPort, false, NULL, false, 0);
						if (thePrefs.GetLogNatTraversalEvents())
							DebugLog(_T("[NatTraversal] Sent OP_HOLEPUNCH to requester %s:%u after callback forward"), (LPCTSTR)ipstr(forwardIP), forwardPort);
					}
				} else if (thePrefs.GetLogNatTraversalEvents()) {
					DebugLogWarning(_T("[NAT-T][Buddy-Forward] OP_REASKCALLBACKUDP payload too small for forward: size=%u"), size);
				}
			}
		break;
	case OP_ESERVER_UDP_PROBE:
		{
			theStats.AddDownDataOverheadOther(size);
			if (size < ESERVER_UDP_PROBE_SIZE || port == 0)
				break;

			CSafeMemFile data_in(packet, size);
			uchar requesterHash[16];
			data_in.ReadHash16(requesterHash);
			uint32 dwToken = data_in.ReadUInt32();
			if (dwToken == 0)
				break;

			CUpDownClient* pRequester = theApp.clientlist->FindServedEServerBuddyByHash(requesterHash);
			if (pRequester == NULL)
				pRequester = theApp.clientlist->FindClientByUserHash(requesterHash);
			if (pRequester == NULL)
				break;
			if (!pRequester->SupportsEServerBuddyExternalUdpPort())
				break;
			if (pRequester->GetEServerExtPortProbeToken() == 0 || pRequester->GetEServerExtPortProbeToken() != dwToken)
				break;
			{
				const CAddress& primaryIP = pRequester->HasLowID() ? pRequester->GetConnectIP() : pRequester->GetIP();
				const CAddress& fallbackIP = pRequester->HasLowID() ? pRequester->GetIP() : pRequester->GetConnectIP();
				bool bIpMatch = false;

				if (!primaryIP.IsNull() && primaryIP.IsPublicIP())
					bIpMatch = (primaryIP == ip);
				else if (!fallbackIP.IsNull() && fallbackIP.IsPublicIP())
					bIpMatch = (fallbackIP == ip);
				else
					bIpMatch = true;

				if (!bIpMatch)
					break;
			}

			if (pRequester->HasFreshObservedExternalUdpPort()) {
				DWORD dwLast = pRequester->GetObservedExternalUdpPortTime();
				if ((DWORD)(::GetTickCount() - dwLast) < ESERVER_UDP_PROBE_MIN_INTERVAL)
					break;
			}

			pRequester->SetObservedExternalUdpPort(port);
			theApp.clientlist->UpdatePendingEServerRelayRequesterPort(pRequester, port);
			theApp.clientlist->TrySendPendingEServerRelayResponseForTarget(pRequester);
			break;
		}
		case OP_RENDEZVOUS:
	{
		CSafeMemFile data(packet, size);
		// Note: we get here via our buddy; 'ip' and 'port' are the requester's public UDP endpoint as observed by the buddy.
		uchar uchUserHash[16];
		data.ReadHash16(uchUserHash);
		uint8 byConnectOptions = data.ReadUInt8();
		// NEW: Read file hash if present (for LowID↔LowID source context)
		uchar reqFileHash[MDX_DIGEST_SIZE];
		md4clr(reqFileHash);
		bool bHasFileContext = false;
		CPartFile* targetFile = NULL;
		CKnownFile* sharedFile = NULL;
		if (data.GetLength() > data.GetPosition() && (data.GetLength() - data.GetPosition()) >= MDX_DIGEST_SIZE) {
			data.ReadHash16(reqFileHash);
			if (!isnulmd4(reqFileHash)) {
				bHasFileContext = true;
				targetFile = theApp.downloadqueue->GetFileByID(reqFileHash);
				if (targetFile == NULL)
					sharedFile = theApp.sharedfiles->GetFileByID(reqFileHash);
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NATTTESTMODE: OP_RENDEZVOUS] Received file hash %s, targetFile=%p sharedFile=%p\n"), (LPCTSTR)md4str(reqFileHash), targetFile, sharedFile);
				if (thePrefs.GetLogNatTraversalEvents()) {
					if (targetFile)
						DebugLog(_T("[NatTraversal] Rendezvous: Received file context for %s (download queue)"), (LPCTSTR)EscPercent(targetFile->GetFileName()));
					else if (sharedFile)
						DebugLog(_T("[NatTraversal] Rendezvous: Resolved shared file context for %s"), (LPCTSTR)EscPercent(sharedFile->GetFileName()));
					else
						DebugLogWarning(_T("[NatTraversal] Rendezvous: Received file hash %s but no local context found"), (LPCTSTR)md4str(reqFileHash));
				}
			}
		}
		CAddress hintedIP;
		uint16 hintedPort = 0;
		if (data.GetLength() > data.GetPosition() && (data.GetLength() - data.GetPosition()) >= 6) {
			uint32 extIPv4 = data.ReadUInt32();
			uint16 extPort = data.ReadUInt16();
			if (extIPv4 != 0 && extPort != 0) {
				hintedIP = CAddress(extIPv4, false);
				hintedPort = extPort;
			}
		}
		auto IsBuddyRelation = [&](CUpDownClient* cand) -> bool
		{
			if (cand == NULL)
				return false;
			if (cand == theApp.clientlist->GetServingBuddy())
				return true;
			return theApp.clientlist->IsServedBuddy(cand);
		};
		auto AcceptDownloadContext = [&](CUpDownClient* cand) -> CUpDownClient*
		{
			if (cand == NULL)
				return NULL;
			if (cand->HasValidHash() && !md4equ(cand->GetUserHash(), uchUserHash))
				return NULL;
			if (IsBuddyRelation(cand))
				return NULL;
			return cand;
		};
		auto ResolveDownloadClient = [&](const CAddress& searchIP, uint16 searchPort) -> CUpDownClient*
		{
			if (searchIP.IsNull())
				return NULL;
			auto AcceptCandidate = [&](CUpDownClient* cand) -> CUpDownClient*
			{
				return AcceptDownloadContext(cand);
			};
			if (searchPort != 0) {
				if (CUpDownClient* exact = AcceptCandidate(theApp.downloadqueue->GetDownloadClientByIP_UDP(searchIP, searchPort, false)))
					return exact;
				bool bMulti = false;
				if (CUpDownClient* relaxed = theApp.downloadqueue->GetDownloadClientByIP_UDP(searchIP, searchPort, true, &bMulti)) {
					if (CUpDownClient* accepted = AcceptCandidate(relaxed))
						return accepted;
				}
				if (CUpDownClient* byUdp = AcceptCandidate(theApp.clientlist->FindClientByIP_UDP(searchIP, searchPort)))
					return byUdp;
				if (CUpDownClient* byKad = AcceptCandidate(theApp.clientlist->FindClientByIP_KadPort(searchIP, searchPort)))
					return byKad;
			}
			if (CUpDownClient* byDlIp = AcceptCandidate(theApp.downloadqueue->GetDownloadClientByIP(searchIP)))
				return byDlIp;
			return AcceptCandidate(theApp.clientlist->FindUniqueClientByIP(searchIP));
		};
CUpDownClient* target = AcceptDownloadContext(theApp.clientlist->FindClientByUserHash(uchUserHash, CAddress(), 0));
CUpDownClient* pDlCtx = ResolveDownloadClient(ip, port);
CUpDownClient* pUploadCtx = NULL;
		bool haveHint = !hintedIP.IsNull() && hintedIP.IsPublicIP();
		if (pDlCtx == NULL && haveHint)
			pDlCtx = ResolveDownloadClient(hintedIP, hintedPort);
		if (target != NULL && pDlCtx == NULL) {
			uint16 knownKad = target->GetKadPort();
			if (knownKad != 0 && knownKad != port) {
				pDlCtx = ResolveDownloadClient(ip, knownKad);
				if (pDlCtx == NULL && haveHint)
					pDlCtx = ResolveDownloadClient(hintedIP, knownKad);
			}
			if (pDlCtx == NULL) {
				uint16 knownUdp = target->GetUDPPort();
				if (knownUdp != 0 && knownUdp != port && knownUdp != knownKad) {
					pDlCtx = ResolveDownloadClient(ip, knownUdp);
					if (pDlCtx == NULL && haveHint)
						pDlCtx = ResolveDownloadClient(hintedIP, knownUdp);
				}
			}
		}

        if (pDlCtx == NULL)
                pDlCtx = AcceptDownloadContext(theApp.downloadqueue->GetDownloadClientByIP(ip));
        auto ResolveUploadClient = [&](const CAddress& searchIP, uint16 searchPort) -> CUpDownClient*
        {
                if (searchIP.IsNull())
                        return NULL;
                auto AcceptCandidate = [&](CUpDownClient* cand) -> CUpDownClient*
                {
                        return AcceptDownloadContext(cand);
                };
                if (searchPort != 0) {
                        if (CUpDownClient* exact = AcceptCandidate(theApp.uploadqueue->GetWaitingClientByIP_UDP(searchIP, searchPort, false)))
                                return exact;
                        bool bMulti = false;
                        if (CUpDownClient* relaxed = theApp.uploadqueue->GetWaitingClientByIP_UDP(searchIP, searchPort, true, &bMulti)) {
                                if (CUpDownClient* accepted = AcceptCandidate(relaxed))
                                        return accepted;
                        }
                }
                if (CUpDownClient* byIp = AcceptCandidate(theApp.uploadqueue->GetWaitingClientByIP(searchIP)))
                        return byIp;
                return NULL;
        };
		if (pUploadCtx == NULL)
			pUploadCtx = ResolveUploadClient(ip, port);
		if (pUploadCtx == NULL && haveHint)
			pUploadCtx = ResolveUploadClient(hintedIP, hintedPort);
		if (pUploadCtx != NULL && pDlCtx == NULL)
			pDlCtx = pUploadCtx;
		if (target == NULL)
			target = pDlCtx;
		if (target == NULL) {
			target = new CUpDownClient(NULL);
			theApp.clientlist->AddClient(target);
		}
		target->SetUserHash(uchUserHash, false);
		target->SetConnectOptions(byConnectOptions, true, true);
		target->SetNatTraversalSupport(true);
		// NEW: If we have file context from rendezvous, set it now
		if (bHasFileContext && targetFile != NULL && target->GetRequestFile() == NULL) {
			target->SetRequestFile(targetFile);
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NATTTESTMODE: OP_RENDEZVOUS] Set request file: %s for client %s\n"), (LPCTSTR)EscPercent(targetFile->GetFileName()), (LPCTSTR)EscPercent(target->DbgGetClientInfo()));
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] Rendezvous: Set request file %s for source"), (LPCTSTR)EscPercent(targetFile->GetFileName()));
		}
		if (target->GetRequestFile() == NULL && pDlCtx && pDlCtx->GetRequestFile() != NULL) {
			target->SetRequestFile(pDlCtx->GetRequestFile());
		}
		// NEW: If we received file context but source is not yet added to partfile, add it now
		if (bHasFileContext && targetFile != NULL && target->GetRequestFile() != NULL) {
			bool bSourceInList = false;
			for (POSITION pos = targetFile->srclist.GetHeadPosition(); pos != NULL; ) {
				CUpDownClient* cur = targetFile->srclist.GetNext(pos);
				if (cur == target) {
					bSourceInList = true;
					break;
				}
			}
			if (!bSourceInList) {
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NATTTESTMODE: OP_RENDEZVOUS] Source not in partfile srclist, calling CheckAndAddSource\n"));
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal] Rendezvous: Adding source to partfile %s"), (LPCTSTR)EscPercent(targetFile->GetFileName()));
				// Create a copy of the client for CheckAndAddSource (it may delete the pointer)
				CUpDownClient* sourceCandidate = new CUpDownClient(targetFile, target->GetUserPort(), 0, target->GetServerIP(), target->GetServerPort(), false, target->GetIP());
				sourceCandidate->SetUserHash(target->GetUserHash(), target->HasValidHash());
				sourceCandidate->SetConnectOptions(target->GetConnectOptions(true, true, true), true, true);
				sourceCandidate->SetNatTraversalSupport(true);
				sourceCandidate->SetKadPort(target->GetKadPort());
				sourceCandidate->SetUDPPort(target->GetUDPPort());
				sourceCandidate->SetSourceFrom(target->GetSourceFrom());
				ESourceFrom eCandidateSourceFrom = sourceCandidate->GetSourceFrom();
				bool bCandidateSourceFromAuthoritative = false;
				if (sourceCandidate->GetServerIP() != 0 && sourceCandidate->GetServerPort() != 0) {
					eCandidateSourceFrom = SF_SERVER;
					bCandidateSourceFromAuthoritative = true;
				}
				CUpDownClient* pResolvedSource = NULL;
				bool bAddedSource = theApp.downloadqueue->CheckAndAddSource(targetFile, sourceCandidate, eCandidateSourceFrom, bCandidateSourceFromAuthoritative, &pResolvedSource);
				if (pResolvedSource != NULL)
					target = pResolvedSource;
				if (bAddedSource) {
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal] Rendezvous: Successfully added source to %s"), (LPCTSTR)EscPercent(targetFile->GetFileName()));
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NATTTESTMODE: OP_RENDEZVOUS] CheckAndAddSource SUCCESS\n"));
				} else if (pResolvedSource != NULL) {
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NATTTESTMODE: OP_RENDEZVOUS] CheckAndAddSource merged with existing source\n"));
				} else {
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NATTTESTMODE: OP_RENDEZVOUS] CheckAndAddSource FAILED\n"));
				}
			} else {
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NATTTESTMODE: OP_RENDEZVOUS] Source already in partfile srclist\n"));
			}
		}
		if (sharedFile != NULL) {
			target->SetUploadFileID(sharedFile);
		} else if (isnulmd4(target->GetUploadFileID()) && (pUploadCtx || pDlCtx)) {
			CUpDownClient* ctx = pUploadCtx ? pUploadCtx : pDlCtx;
			if (ctx && !isnulmd4(ctx->GetUploadFileID())) {
				CKnownFile* upFile = theApp.sharedfiles->GetFileByID(ctx->GetUploadFileID());
				if (upFile != NULL)
					target->SetUploadFileID(upFile);
			}
		}
		if (haveHint) {
			if (target->GetConnectIP().IsNull() || !target->GetConnectIP().IsPublicIP())
				target->SetConnectIP(hintedIP);
			if (target->GetIP().IsNull() || !target->GetIP().IsPublicIP()) {
				CAddress hintCopy = hintedIP;
				target->SetIP(hintCopy);
			}
		}
		if (target->GetConnectIP().IsNull())
			target->SetConnectIP(ip);
		if (target->GetIP().IsNull()) {
			CAddress tmp = ip; // SetIP expects non-const lvalue
			target->SetIP(tmp);
		}
		uint16 effectivePort = hintedPort != 0 ? hintedPort : port;
		if (effectivePort != 0) {
			if (target->GetKadPort() != effectivePort)
				target->SetKadPort(effectivePort);
			if (target->GetUDPPort() != effectivePort)
				target->SetUDPPort(effectivePort);
		}
		CString targetInfo = EscPercent(target->DbgGetClientInfo());
		bool bHasActiveUtp = (target->socket != NULL) && target->socket->HaveUtpLayer();
		CUtpSocket* activeUtp = bHasActiveUtp ? target->socket->GetUtpLayer() : NULL;
		if (bHasActiveUtp) {
			AsyncSocketExState utpState = activeUtp != NULL ? activeUtp->GetState() : unconnected;
			bool handshakeGuard = false;
			if (utpState == connecting || utpState == connected)
				handshakeGuard = true;
			DWORD handshakeStart = target->GetUtpConnectionStartTick();
			if (!handshakeGuard && handshakeStart != 0 && !target->IsUtpWritable()) {
				DWORD now = ::GetTickCount();
				if ((int)(now - handshakeStart) >= 0 && (int)(now - handshakeStart) < (int)NAT_TRAVERSAL_HANDSHAKE_GUARD_MS)
					handshakeGuard = true;
			}
			if (handshakeGuard) {
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal][OP_RENDEZVOUS] Skipping rendezvous for %s (uTP handshake in progress)"), (LPCTSTR)targetInfo);
				return true;
			}
			if (target->socket->IsConnected()) {
				if (utpState == connected || target->IsUtpWritable()) {
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal][OP_RENDEZVOUS] Skipping duplicate rendezvous for %s (uTP already active)"), (LPCTSTR)targetInfo);
					return true;
				}
			}
		}
		if (thePrefs.GetLogNatTraversalEvents()) {
			CString reqName = target->GetRequestFile() != NULL ? EscPercent(target->GetRequestFile()->GetFileName()) : CString(_T("<none>"));
			CString upName;
			if (!isnulmd4(target->GetUploadFileID())) {
				const CKnownFile* upFile = theApp.sharedfiles->GetFileByID(target->GetUploadFileID());
				if (upFile == NULL)
					upFile = theApp.knownfiles->FindKnownFileByID(target->GetUploadFileID());
				upName = upFile != NULL ? EscPercent(upFile->GetFileName()) : CString(_T("<unknown>"));
			} else
				upName = _T("<none>");
			const CAddress& diagHint = haveHint ? hintedIP : ip;
			uint16 diagPort = haveHint && hintedPort != 0 ? hintedPort : port;
			DebugLog(_T("[NatTraversal][OP_RENDEZVOUS][Diag] target=%s socket=%p reqFile=%s upFile=%s hint=%s:%u"),
				(LPCTSTR)targetInfo, target->socket, (LPCTSTR)reqName, (LPCTSTR)upName, (LPCTSTR)ipstr(diagHint), diagPort);
		}

		auto MakeStageError = [&](LPCTSTR stage, const CString& detail) -> CString
		{
			CString msg;
			if (!detail.IsEmpty())
				msg.Format(_T("OP_RENDEZVOUS stage %s failed: %s"), stage, (LPCTSTR)detail);
			else
				msg.Format(_T("OP_RENDEZVOUS stage %s failed"), stage);
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLogError(_T("[NatTraversal][OP_RENDEZVOUS][Diag] %s (target=%s)"), (LPCTSTR)msg, (LPCTSTR)targetInfo);
			return msg;
		};

		auto StageGuard = [&](LPCTSTR stage, const std::function<void(void)>& fn)
		{
			CScopedSeTranslator seGuard(NatTraversalSeTranslator);
			try {
				fn();
			} catch (CNatSeException& se) {
				CString detail;
				if (se.address != 0) {
					detail.Format(_T("SEH 0x%08X at 0x%p"), se.code, reinterpret_cast<void*>(se.address));
					HMODULE module = NULL;
					if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
						reinterpret_cast<LPCTSTR>(se.address), &module) && module != NULL) {
						TCHAR modulePath[MAX_PATH] = { 0 };
						DWORD len = GetModuleFileName(module, modulePath, _countof(modulePath));
						if (len > 0)
							detail.AppendFormat(_T(" in %s"), modulePath);
					}
				} else
					detail.Format(_T("SEH 0x%08X"), se.code);
				throw MakeStageError(stage, detail);
			} catch (CException* exStage) {
				CString msg = CExceptionStr(*exStage);
				exStage->Delete();
				throw MakeStageError(stage, msg);
			} catch (std::exception& exStageStd) {
				CStringA what(exStageStd.what());
				throw MakeStageError(stage, CString(what));
			} catch (...) {
				throw MakeStageError(stage, CString(_T("unknown exception")));
			}
		};

		StageGuard(_T("SeedExpectationPrimary"), [&]()
		{
			if (!ip.IsNull() && port != 0)
				SeedNatTraversalExpectation(target, ip, port);
			if (haveHint && hintedPort != 0)
				SeedNatTraversalExpectation(target, hintedIP, hintedPort);
			else {
				CAddress fallbackIP = !target->GetConnectIP().IsNull() ? target->GetConnectIP() : target->GetIP();
				uint16 fallbackPort = target->GetKadPort() ? target->GetKadPort() : target->GetUDPPort();
				if (!fallbackIP.IsNull() && fallbackPort != 0)
					SeedNatTraversalExpectation(target, fallbackIP, fallbackPort);
			}
		});

	StageGuard(_T("MarkNatTRendezvous"), [&]()
	{
		target->MarkNatTRendezvous(6);
	});

	// If we are the uploader (sharedFile context), wait for incoming connection - DO NOT initiate outgoing!
	bool bWeAreUploader = (sharedFile != NULL);
	if (!bWeAreUploader) {
		try {
			StageGuard(_T("TryToConnect"), [&]()
			{
				bool alive = target->TryToConnect(true, false, NULL, true);
				if (!alive)
					throw MakeStageError(_T("TryToConnect"), CString(_T("client deleted during connect")));
			});
		} catch (CString& stageError) {
			if (stageError.Find(_T("SEH")) != -1) {
				target->FlagNatTFatalConnectFailure();
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLogWarning(_T("[NatTraversal][OP_RENDEZVOUS][Diag] TryToConnect raised %s; continuing with rendezvous fallback"), (LPCTSTR)stageError);
			} else {
				throw;
			}
		}
	} else {
		// Uploader side: We received OP_RENDEZVOUS from buddy forwarding downloader's request
		// Do NOT initiate outgoing connection - wait for downloader to connect to us via uTP
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal][OP_RENDEZVOUS] Uploader side: creating passive socket, awaiting incoming uTP connection from %s"), (LPCTSTR)targetInfo);
		
		// Create passive socket with uTP layer without initiating outgoing connection
		StageGuard(_T("CreatePassiveSocket"), [&]()
		{
			if (!target->socket) {
				// Create socket using same logic as TryToConnect but without Connect() call
				target->socket = static_cast<CClientReqSocket*>(RUNTIME_CLASS(CClientReqSocket)->CreateObject());
				target->socket->SetClient(target);
				if (!target->socket->Create()) {
					DWORD dwLastError = ::WSAGetLastError();
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLogError(_T("[NatTraversal][OP_RENDEZVOUS] Uploader: Failed to create passive socket, err=%lu, %s"), dwLastError, (LPCTSTR)EscPercent(target->DbgGetClientInfo()));
					target->socket->Safe_Delete();
					throw MakeStageError(_T("CreatePassiveSocket"), CString(_T("socket creation failed")));
				}
				// Initialize uTP layer for incoming connection
				target->socket->InitUtpSupport();
				if (!target->socket->HaveUtpLayer()) {
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLogWarning(_T("[NatTraversal][OP_RENDEZVOUS] Uploader: uTP layer not attached, %s"), (LPCTSTR)EscPercent(target->DbgGetClientInfo()));
				} else {
					target->SetUtpLocalInitiator(false); // Uploader is passive/receiver
					target->ResetUtpFlowControl();
					target->ClearUtpQueuedPackets();
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal][OP_RENDEZVOUS] Uploader: Passive socket created with uTP layer, %s"), (LPCTSTR)EscPercent(target->DbgGetClientInfo()));
				}
			}
		});
	}		if (!theApp.clientlist->IsValidClient(target))
			throw MakeStageError(_T("ValidateClient"), CString(_T("client pointer invalid after TryToConnect")));

		StageGuard(_T("ExpectPeer"), [&]()
		{
			if (target->socket && target->socket->HaveUtpLayer()) {
				CUtpSocket* utp = target->socket->GetUtpLayer();
				auto ExpectEndpoint = [&](const CAddress& expIP, uint16 expPort)
				{
					if (expPort == 0 || expIP.IsNull())
						return;
					SOCKADDR_IN6 sa = { 0 }; int salen = sizeof(sa);
					CAddress exp = expIP;
					exp.Convert(CAddress::IPv6);
					exp.ToSA((SOCKADDR*)&sa, &salen, expPort);
					utp->ExpectPeer((SOCKADDR*)&sa, salen);
				};
				ExpectEndpoint(ip, port);
				if (haveHint)
					ExpectEndpoint(hintedIP, hintedPort);
				else {
					CAddress fallbackIP = !target->GetConnectIP().IsNull() ? target->GetConnectIP() : target->GetIP();
					uint16 fallbackPort = target->GetKadPort() ? target->GetKadPort() : target->GetUDPPort();
					if (!(fallbackIP == ip && fallbackPort == port))
						ExpectEndpoint(fallbackIP, fallbackPort);
				}
			} else if (thePrefs.GetLogNatTraversalEvents()) {
				DebugLogWarning(_T("[NatTraversal] Rendezvous: Missing uTP layer for %s (socket=%p)"), (LPCTSTR)targetInfo, target->socket);
			}
		});

		// Only queue deferred connect for downloader - uploader already has passive socket
		if (!bWeAreUploader) {
			StageGuard(_T("QueueDeferred"), [&]()
			{
				if (haveHint)
					target->QueueDeferredNatConnect(hintedIP, hintedPort);
				else
					target->QueueDeferredNatConnect(ip, port);
			});
		}

		StageGuard(_T("HolePunchPrimary"), [&]()
		{
			for (int i = 0; i < 12; ++i) {
				Packet* hp = new Packet(OP_EMULEPROT);
				hp->opcode = OP_HOLEPUNCH;
				SendPacket(hp, ip, port, false, target->GetUserHash(), false, 0);
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal] Rendezvous: HOLEPUNCH -> %s:%u (buddy-observed #%d)"), (LPCTSTR)ipstr(ip), port, i + 1);
				if (i == 5)
					::Sleep(50);
			}

			uint16 win = thePrefs.GetNatTraversalPortWindow();
			uint16 sweepWin = thePrefs.GetNatTraversalSweepWindow();
			uint16 span = std::min<uint16>(win, sweepWin);
			for (uint16 off = 1; off <= span; ++off) {
				if (port > off) {
					Packet* hp = new Packet(OP_EMULEPROT);
					hp->opcode = OP_HOLEPUNCH;
					SendPacket(hp, ip, (uint16)(port - off), false, target->GetUserHash(), false, 0);
				}
				if (port + off <= 65535) {
					Packet* hp = new Packet(OP_EMULEPROT);
					hp->opcode = OP_HOLEPUNCH;
					SendPacket(hp, ip, (uint16)(port + off), false, target->GetUserHash(), false, 0);
				}
			}
		});

		StageGuard(_T("HolePunchSecondary"), [&]()
		{
			CAddress hpIP = !target->GetConnectIP().IsNull() ? target->GetConnectIP() : target->GetIP();
			uint16 hpPort = target->GetKadPort() ? target->GetKadPort() : target->GetUDPPort();
			if (!hpIP.IsNull() && hpPort != 0) {
				for (int i = 0; i < 12; ++i) {
					Packet* hp2 = new Packet(OP_EMULEPROT);
					hp2->opcode = OP_HOLEPUNCH;
					SendPacket(hp2, hpIP, hpPort, false, target->GetUserHash(), false, 0);
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal] Rendezvous: HOLEPUNCH -> %s:%u (pre-known #%d)"), (LPCTSTR)ipstr(hpIP), hpPort, i + 1);
					if (i == 5)
						::Sleep(50);
					}
					uint16 win = thePrefs.GetNatTraversalPortWindow();
					uint16 sweepWin = thePrefs.GetNatTraversalSweepWindow();
					uint16 span = std::min<uint16>(win, sweepWin);
					for (uint16 off = 1; off <= span; ++off) {
						if (hpPort > off) {
							Packet* hp3 = new Packet(OP_EMULEPROT);
							hp3->opcode = OP_HOLEPUNCH;
							SendPacket(hp3, hpIP, (uint16)(hpPort - off), false, target->GetUserHash(), false, 0);
					}
					if (hpPort + off <= 65535) {
						Packet* hp4 = new Packet(OP_EMULEPROT);
						hp4->opcode = OP_HOLEPUNCH;
						SendPacket(hp4, hpIP, (uint16)(hpPort + off), false, target->GetUserHash(), false, 0);
					}
				}
			}
			});

			// 3) Attempt direct uTP connect with override (owner already created above)
			break;
			}
	case OP_HOLEPUNCH:
	{
		// Receiving a HOLEPUNCH acts as a trigger to try a uTP connect override towards the sender.
		// This helps synchronize both sides during NAT traversal (especially symmetric NATs).
		theStats.AddDownDataOverheadOther(size);
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] HOLEPUNCH received from %s:%u (size=%u)"), (LPCTSTR)ipstr(ip), (UINT)port, size);
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NATTTESTMODE: HOLEPUNCH] Received from %s:%u size=%u\n"), (LPCTSTR)ipstr(ip), (unsigned)port, (unsigned)size);
		if (!thePrefs.IsNatTraversalServiceEnabled()) {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] HOLEPUNCH ignored: NAT traversal service disabled"));
			break;
		}
		// Try to locate a download client by the sender's UDP endpoint first.
		CUpDownClient* pCand = MatchNatExpectation(ip, port);
		bool bMatchedExpectation = (pCand != NULL);
		bool bMultiIp = false;
		if (pCand == NULL)
			pCand = theApp.downloadqueue->GetDownloadClientByIP_UDP(ip, port, true, &bMultiIp);
		if (pCand == NULL) {
			// Retry download queue lookup without the multiple-IP guard.
			pCand = theApp.downloadqueue->GetDownloadClientByIP_UDP(ip, port, true);
		}
		if (pCand == NULL) {
			// Rendezvous targets are often uploaders; search waiting upload queue entries by UDP endpoint.
			bool bUploadMulti = false;
			pCand = theApp.uploadqueue->GetWaitingClientByIP_UDP(ip, port, true, &bUploadMulti);
			if (pCand == NULL)
				pCand = theApp.uploadqueue->GetWaitingClientByIP_UDP(ip, port, true);
			if (pCand == NULL)
				pCand = theApp.uploadqueue->GetWaitingClientByIP(ip);
		}
		if (pCand == NULL) {
			// Final fallback: locate any known client by IP (may map to buddy or placeholder sharing IP space).
			pCand = theApp.clientlist->FindUniqueClientByIP(ip);
		}
		auto IsBuddyRelation = [&](CUpDownClient* cand) -> bool
		{
			if (cand == NULL)
				return false;
			if (cand == theApp.clientlist->GetServingBuddy())
				return true;
			return theApp.clientlist->IsServedBuddy(cand);
		};
			if (pCand && IsBuddyRelation(pCand)) {
				pCand = NULL;
				if (!bMatchedExpectation)
					pCand = MatchNatExpectation(ip, port);
			}
			if (pCand == NULL && !bMatchedExpectation)
				pCand = MatchNatExpectation(ip, port);
			if (pCand == NULL) {
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLogWarning(_T("[NatTraversal] HOLEPUNCH: no client match for %s:%u"), (LPCTSTR)ipstr(ip), (UINT)port);
			}
			if (pCand) {
				// Keep NAT expectation fresh for this observed endpoint so incoming uTP ACCEPT can adopt the right owner.
				RegisterNatExpectation(pCand, ip, port);

				// Adopt file context if this placeholder lacks it
				if (pCand->GetRequestFile() == NULL) {
					bool bM = false;
					CUpDownClient* pCtx = theApp.downloadqueue->GetDownloadClientByIP_UDP(ip, port, true, &bM);
				if (!pCtx)
					pCtx = theApp.downloadqueue->GetDownloadClientByIP_UDP(ip, port, true);
				if (pCtx && IsBuddyRelation(pCtx))
					pCtx = NULL;
				if (pCtx && pCtx->GetRequestFile())
					pCand->SetRequestFile(pCtx->GetRequestFile());
				}
				// Check if client is already in connecting state or connected
				const EConnectingState eConnState = pCand->GetConnectingState();
				bool bAlreadyConnecting = (eConnState != CCS_NONE);
				bool bAlreadyHaveUtp = (pCand->socket != NULL && pCand->socket->HaveUtpLayer());
				bool bSocketConnected = (pCand->socket != NULL && pCand->socket->IsConnected());
				if (thePrefs.GetLogNatTraversalEvents()) {
					DebugLog(_T("[NatTraversal] HOLEPUNCH from %s:%u for %s, state=%u, hasSocket=%d, hasUtp=%d, sockConnected=%d"),
						(LPCTSTR)ipstr(ip), (UINT)port, (LPCTSTR)EscPercent(pCand->DbgGetClientInfo()),
						(UINT)eConnState, pCand->socket != NULL ? 1 : 0, bAlreadyHaveUtp ? 1 : 0, bSocketConnected ? 1 : 0);
				}
				// If socket is already connected (uTP or TCP), skip redundant connection attempts
				if (bSocketConnected) {
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal] HOLEPUNCH: Socket already connected, skipping redundant connect for %s"), (LPCTSTR)EscPercent(pCand->DbgGetClientInfo()));
					// Still register peer expectation if uTP to help with simultaneous connect scenarios
					if (bAlreadyHaveUtp) {
						CUtpSocket* utp = pCand->socket->GetUtpLayer();
						SOCKADDR_IN6 sa = { 0 }; int salen = sizeof(sa);
						CAddress a = ip; a.Convert(CAddress::IPv6);
						a.ToSA((SOCKADDR*)&sa, &salen, port);
						utp->ExpectPeer((SOCKADDR*)&sa, salen);
					}
				} else {
					// Determine role FIRST: if we have no RequestFile, we're likely the uploader awaiting incoming connection
					bool bWeAreUploader = (pCand->GetRequestFile() == NULL);

					// For uploader side in relay scenario, update ConnectIP from observed hole punch source
					// The IP in relay packet may be wrong; only reliable source is incoming UDP packets
					if (bWeAreUploader) {
						// Uploader: always trust the observed hole punch source IP
						if (pCand->GetConnectIP() != ip) {
							if (thePrefs.GetLogNatTraversalEvents())
								DebugLog(_T("[NatTraversal] HOLEPUNCH: Uploader side - updating ConnectIP from %s to observed %s"),
									(LPCTSTR)ipstr(pCand->GetConnectIP()), (LPCTSTR)ipstr(ip));
							pCand->SetConnectIP(ip);
						}
					} else {
						// Downloader: seed connect endpoint if needed (original behavior)
						if (pCand->GetConnectIP().IsNull())
							pCand->SetConnectIP(ip);
					}
					if (pCand->GetKadPort() == 0 && pCand->GetUDPPort() == 0)
						pCand->SetKadPort(port);
					pCand->SetNatTraversalSupport(true);

					// Recover from stale connecting state left behind by a dead socket.
					const bool bStaleDirectConnectState = (eConnState == CCS_DIRECTTCP || eConnState == CCS_PRECONDITIONS);
					if (bAlreadyConnecting && pCand->socket == NULL && bStaleDirectConnectState) {
						if (thePrefs.GetLogNatTraversalEvents())
							DebugLogWarning(_T("[NatTraversal] HOLEPUNCH: Clearing stale connecting state (no socket) before connect for %s"), (LPCTSTR)EscPercent(pCand->DbgGetClientInfo()));
						pCand->ResetConnectingState();
						theApp.clientlist->RemoveConnectingClient(pCand);
						bAlreadyConnecting = false;
					}

					// If already connecting with uTP socket, only register peer expectation - don't restart connection
					if (bAlreadyConnecting && bAlreadyHaveUtp) {
						if (thePrefs.GetLogNatTraversalEvents())
							DebugLog(_T("[NatTraversal] HOLEPUNCH: Client already connecting with uTP, adding peer expectation only for %s"), (LPCTSTR)EscPercent(pCand->DbgGetClientInfo()));
						CUtpSocket* utp = pCand->socket->GetUtpLayer();
						SOCKADDR_IN6 sa = { 0 }; int salen = sizeof(sa);
						CAddress a = ip; a.Convert(CAddress::IPv6);
						a.ToSA((SOCKADDR*)&sa, &salen, port);
						utp->ExpectPeer((SOCKADDR*)&sa, salen);
						// Queue deferred attempt in case current one fails
						pCand->QueueDeferredNatConnect(ip, port);
					} else if (bWeAreUploader) {
						// Uploader side: DON'T initiate outgoing connection, only register peer expectation if socket exists
						if (thePrefs.GetLogNatTraversalEvents())
							DebugLog(_T("[NatTraversal] HOLEPUNCH: Uploader side, awaiting incoming uTP connection from %s"), (LPCTSTR)EscPercent(pCand->DbgGetClientInfo()));
						// DO NOT call MarkNatTRendezvous for uploader
						// It triggers DoNatTRetry timer which calls TryToConnect, causing race conditions
						// Register peer expectation if socket already exists (from Rendezvous processing)
						if (pCand->socket && pCand->socket->HaveUtpLayer()) {
							CUtpSocket* utp = pCand->socket->GetUtpLayer();
							SOCKADDR_IN6 sa = { 0 }; int salen = sizeof(sa);
							CAddress a = ip; a.Convert(CAddress::IPv6);
							a.ToSA((SOCKADDR*)&sa, &salen, port);
							utp->ExpectPeer((SOCKADDR*)&sa, salen);
						}
						// DON'T call QueueDeferredNatConnect for uploader - it would trigger TryToConnect in DoNatTRetry
					} else {
						// Downloader side: initiate outgoing connection
						if (thePrefs.GetLogNatTraversalEvents())
							DebugLog(_T("[NatTraversal] HOLEPUNCH: Downloader side, initiating uTP connection for %s"), (LPCTSTR)EscPercent(pCand->DbgGetClientInfo()));
						pCand->MarkNatTRendezvous(4);
						pCand->SetLastTriedToConnectTime();
						pCand->TryToConnect(true, false, NULL, true);
						// If uTP owner exists after connect attempt, add current sender as expected peer
						if (pCand->socket && pCand->socket->HaveUtpLayer()) {
							CUtpSocket* utp = pCand->socket->GetUtpLayer();
							SOCKADDR_IN6 sa = { 0 }; int salen = sizeof(sa);
							CAddress a = ip; a.Convert(CAddress::IPv6);
							a.ToSA((SOCKADDR*)&sa, &salen, port);
							utp->ExpectPeer((SOCKADDR*)&sa, salen);
						} else if (thePrefs.GetLogNatTraversalEvents()) {
							DebugLogWarning(_T("[NatTraversal] HOLEPUNCH: Missing uTP layer after connect for %s (socket=%p)"), (LPCTSTR)EscPercent(pCand->DbgGetClientInfo()), pCand->socket);
						}
						pCand->QueueDeferredNatConnect(ip, port);
					}
				}
		}
		break;
	}
	case OP_SERVINGBUDDYPULL_REQ:
	{
		// Buddy-info pull request (V2 only); respond with our serving buddy hints
		theStats.AddDownDataOverheadOther(size);
		CSafeMemFile data_in(packet, size);

		if (size < 1 + 1 + 4 + 16 + 1) // ver, flags, nonce, requesterKadID, connectopts
			break;

		uint8 ver = data_in.ReadUInt8();
		(void)data_in.ReadUInt8(); // flags
		uint32 nonce = data_in.ReadUInt32();
		Kademlia::CUInt128 reqKadID; data_in.ReadUInt128(reqKadID);
		(void)data_in.ReadUInt8(); // requester opts (unused)

		// Prepare response
		uint8 flags = 0;
		CUpDownClient* myBuddy = theApp.clientlist->GetServingBuddy();
		bool hasV4 = false, hasV6 = false;
		CAddress v4, v6; uint16 p4 = 0, p6 = 0;

		if (myBuddy && !myBuddy->GetIP().IsNull()) {
			if (myBuddy->GetIP().GetType() == CAddress::IPv4) {
				hasV4 = true; v4 = myBuddy->GetIP();
				p4 = myBuddy->GetKadPort() ? myBuddy->GetKadPort() : myBuddy->GetUDPPort();
			} else if (myBuddy->GetIP().GetType() == CAddress::IPv6) {
				hasV6 = true;
				v6 = myBuddy->GetIP(); p6 = myBuddy->GetKadPort() ? myBuddy->GetKadPort() : myBuddy->GetUDPPort();
			}
		}

		if (hasV4)
			flags |= 0x01;

		if (hasV6)
			flags |= 0x02;

		if (thePrefs.IsNatTraversalServiceEnabled())
			flags |= 0x04; // NAT-T support hint

		// Compute pair-specific ServingBuddyID = myKadID XOR requesterKadID
		Kademlia::CUInt128 sbid = Kademlia::CKademlia::GetPrefs()->GetKadID();
		sbid.Xor(reqKadID);

		CSafeMemFile out(64);
		out.WriteUInt8(ver);
		out.WriteUInt8(flags);
		out.WriteUInt32(nonce);
		out.WriteUInt128(sbid);

		if (hasV4) {
			out.WriteUInt32(v4.ToUInt32(false));
			out.WriteUInt16(p4);
		}

		if (hasV6) {
			out.WriteHash16(v6.Data());
			out.WriteUInt16(p6);
		}

		Packet* res = new Packet(out, OP_EMULEPROT);
		res->opcode = OP_SERVINGBUDDYPULL_RES;
		theStats.AddUpDataOverheadOther(res->size);
		theApp.clientudp->SendPacket(res, ip, port, false, NULL, false, 0);
		break;
	}
	case OP_SERVINGBUDDYPULL_RES:
	{
		// Buddy-info pull response; set on the matching download client and try NAT-T
		theStats.AddDownDataOverheadOther(size);
		CSafeMemFile in(packet, size);

		if (size < 1 + 1 + 4 + 16)
			break; // ver, flags, nonce, sbid

		uint8 ver = in.ReadUInt8();
		uint8 flags = in.ReadUInt8();
		(void)ver; (void)flags; // currently unused metadata
		(void)in.ReadUInt32(); // nonce echo
		uchar sbid[16]; in.ReadHash16(sbid);
		uint32 v4 = 0; uint16 p4 = 0; CAddress v6 = CAddress(CAddress::IPv6); uint16 p6 = 0;

		if (flags & 0x01) {
			v4 = in.ReadUInt32();
			p4 = in.ReadUInt16();
		}

		if (flags & 0x02) { uchar buf[16]; in.ReadHash16(buf); memcpy((void*)v6.Data(), buf, 16); p6 = in.ReadUInt16(); }

		bool bSenderMultipleIpUnknown = false;
		CUpDownClient* sender = theApp.downloadqueue->GetDownloadClientByIP_UDP(ip, port, true, &bSenderMultipleIpUnknown);

		if (!sender)
			sender = theApp.downloadqueue->GetDownloadClientByIP_UDP(ip, port, true); // Try to find by IPv6 if different endpoint mapping

		if (sender) {
			if ((flags & 0x02) && !v6.IsNull() && p6) {
				sender->SetServingBuddyIP(v6);
				sender->SetServingBuddyPort(p6);
			} else if ((flags & 0x01) && v4 && p4) {
				sender->SetServingBuddyIP(CAddress(v4, false));
				sender->SetServingBuddyPort(p4);
			}

			sender->SetServingBuddyID(sbid);

			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[ServingBuddyPull] Received buddy info for %s; trying NAT-T"), (LPCTSTR)EscPercent(sender->DbgGetClientInfo()));

			sender->TryToConnect(true, false, NULL, true);
		}

		break;
	}
	case OP_REASKFILEPING:
		{
			theStats.AddDownDataOverheadFileRequest(size);
			CSafeMemFile data_in(packet, size);
			uchar reqfilehash[MDX_DIGEST_SIZE];
			data_in.ReadHash16(reqfilehash);
			CKnownFile *reqfile = theApp.sharedfiles->GetFileByID(reqfilehash);

			bool bSenderMultipleIpUnknown = false;
			CUpDownClient *sender = theApp.uploadqueue->GetWaitingClientByIP_UDP(ip, port, true, &bSenderMultipleIpUnknown);
			if (!reqfile) {
				if (thePrefs.GetDebugClientUDPLevel() > 0) {
					DebugRecv("OP_ReaskFilePing", NULL, reqfilehash, ip);
					DebugSend("OP_FileNotFound", NULL);
				}

				Packet *response = new Packet(OP_FILENOTFOUND, 0, OP_EMULEPROT);
				theStats.AddUpDataOverheadFileRequest(response->size);
				if (sender != NULL)
					SendPacket(response, ip, port, sender->ShouldReceiveCryptUDPPackets(), sender->GetUserHash(), false, 0);
				else
					SendPacket(response, ip, port, false, NULL, false, 0);
				break;
			}
			if (sender) {
				if (thePrefs.GetDebugClientUDPLevel() > 0)
					DebugRecv("OP_ReaskFilePing", sender, reqfilehash);

				sender->IncrementAskedCount();
				sender->SetLastUpRequest();
				sender->uiULAskingCounter++;
				// IP banned, no answer for this request
				if (sender->IsBanned())
					break;
				if (thePrefs.IsDetectAgressive() && sender->uiULAskingCounter > uint16(thePrefs.GetAgressiveCounter()) && ((time(NULL) - sender->tLastSeen) / sender->uiULAskingCounter) < uint32(MIN2S(thePrefs.GetAgressiveTime()))) { // IsHarderPunishment isn't necessary here since the cost is low
					if (thePrefs.IsAgressiveLog())
					{
						AddProtectionLogLine(false, _T("OP_REASKFILEPING: (%s/%u)=%s ==> %s(%s) ASK TO FAST!"),
						CastSecondsToHM(time(NULL) - sender->tLastSeen),
						sender->uiULAskingCounter,
						CastSecondsToHM((time(NULL) - sender->tLastSeen) / sender->uiULAskingCounter),
						(LPCTSTR)EscPercent(sender->GetUserName()),
						(LPCTSTR)EscPercent(sender->DbgGetFullClientSoftVer()));
					}
					theApp.shield->SetPunishment(sender, GetResString(_T("PUNISHMENT_REASON_AGRESSIVE")), PR_AGGRESSIVE);
					break;
				}

				//Make sure we are still thinking about the same file
				if (md4equ(reqfilehash, sender->GetUploadFileID())) {
					sender->IncrementAskedCount();
					sender->SetLastUpRequest();
					//I messed up when I first added extended info to UDP
					//I should have originally used the entire ProcessExtendedInfo the first time.
					//So now I am forced to check UDPVersion to see if we are sending all the extended info.
					//From now on, we should not have to change anything here if we change
					//something in the extended info data as this will be taken care of in ProcessExtendedInfo()
					//Update extended info.
					if (sender->GetUDPVersion() > 3)
						sender->ProcessExtendedInfo(data_in, reqfile, true);

					//Update our complete source counts.
					else if (sender->GetUDPVersion() > 2) {
						uint16 nCompleteCountLast = sender->GetUpCompleteSourcesCount();
						uint16 nCompleteCountNew = data_in.ReadUInt16();
						sender->SetUpCompleteSourcesCount(nCompleteCountNew);
						if (nCompleteCountLast != nCompleteCountNew)
							reqfile->UpdatePartsInfo();
					}
					CSafeMemFile data_out(128);
					if (sender->GetUDPVersion() > 3) {
						if (reqfile->IsPartFile())
							static_cast<CPartFile*>(reqfile)->WritePartStatus(data_out);
						else
							data_out.WriteUInt16(0);
					}
					data_out.WriteUInt16((uint16)(theApp.uploadqueue->GetWaitingPosition(sender)));
					if (thePrefs.GetDebugClientUDPLevel() > 0)
						DebugSend("OP_ReaskAck", sender);
					Packet *response = new Packet(data_out, OP_EMULEPROT);
					response->opcode = OP_REASKACK;
					theStats.AddUpDataOverheadFileRequest(response->size);
					SendPacket(response, ip, port, sender->ShouldReceiveCryptUDPPackets(), sender->GetUserHash(), false, 0);
				} else
					DebugLogError(_T("Client UDP socket; ReaskFilePing; reqfile does not match.\nm_reqfile:         %s\nsender->GetRequestFile(): %s\n"), (LPCTSTR)DbgGetFileInfo(reqfile->GetFileHash()), sender->GetRequestFile() ? (LPCTSTR)DbgGetFileInfo(sender->GetRequestFile()->GetFileHash()) : _T("(null)"));
			} else {
				if (thePrefs.GetDebugClientUDPLevel() > 0)
					DebugRecv("OP_ReaskFilePing", NULL, reqfilehash, ip);
				// Don't answer him. We probably have him on our queue already, but can't locate him. Force him to establish a TCP connection
				if (!bSenderMultipleIpUnknown) {
					if (theApp.uploadqueue->GetWaitingUserCount() + 50 > thePrefs.GetQueueSize()) {
						if (thePrefs.GetDebugClientUDPLevel() > 0)
							DebugSend("OP_QueueFull", NULL);
						Packet *response = new Packet(OP_QUEUEFULL, 0, OP_EMULEPROT);
						theStats.AddUpDataOverheadFileRequest(response->size);
						SendPacket(response, ip, port, false, NULL, false, 0); // we cannot answer this one encrypted since we don't know this client
					}
				} else
					DebugLogWarning(_T("UDP Packet received - multiple clients with the same IP but different UDP port found. Possible UDP Port mapping problem, enforcing TCP connection. IP: %s, Port: %u"), (LPCTSTR)ipstr(ip), port);
			}
		}
		break;
	case OP_QUEUEFULL:
		{
			theStats.AddDownDataOverheadFileRequest(size);
			CUpDownClient *sender = theApp.downloadqueue->GetDownloadClientByIP_UDP(ip, port, true);
			if (thePrefs.GetDebugClientUDPLevel() > 0)
				DebugRecv("OP_QueueFull", sender, NULL, ip);
			if (sender && sender->UDPPacketPending()) {
				sender->SetRemoteQueueFull(true);
				sender->UDPReaskACK(0);
			} else if (sender != NULL)
				DebugLogError(_T("Received UDP Packet (OP_QUEUEFULL) which was not requested (pendingflag == false); Ignored packet - %s"), (LPCTSTR)EscPercent(sender->DbgGetClientInfo()));
		}
		break;
	case OP_REASKACK:
		{
			theStats.AddDownDataOverheadFileRequest(size);
			CUpDownClient *sender = theApp.downloadqueue->GetDownloadClientByIP_UDP(ip, port, true);
			
			// For NAT-T ephemeral OP_REASKACK, sender might not be found by port due to port mismatch
			// Try to find by IP only if not found by IP:Port
			// Ephemeral OP_REASKACK is 4 bytes (part count 0 + queue pos -1), but encryption may add 2 bytes overhead
			if (sender == NULL && (size == 4 || size == 6)) {
				sender = theApp.downloadqueue->GetDownloadClientByIP_UDP(ip, 0, true);
				if (sender != NULL && thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal] Found sender by IP only (port mismatch) for ephemeral OP_REASKACK: %s:%u"), (LPCTSTR)ipstr(ip), port);
			}
			
			// If still not found, check pending callbacks (for lowid2lowid scenario where client not yet in queue)
			uchar fileHash[MDX_DIGEST_SIZE];
			if (sender == NULL && (size == 4 || size == 6) && FindPendingCallback(ip, port, fileHash)) {
				// Found pending callback, now find or create client from partfile sources
				CPartFile* pPartFile = theApp.downloadqueue->GetFileByID(fileHash);
				if (pPartFile != NULL) {
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal] Found pending callback for %s:%u, file: %s"), (LPCTSTR)ipstr(ip), port, (LPCTSTR)pPartFile->GetFileName());
					
					// Try to find source by IP in this partfile
					CSingleLock listlock(&pPartFile->m_FileCompleteMutex);
					listlock.Lock();
					for (POSITION pos = pPartFile->srclist.GetHeadPosition(); pos != NULL;) {
						CUpDownClient* pClient = pPartFile->srclist.GetNext(pos);
						if (pClient && (pClient->GetIP() == ip || pClient->GetConnectIP() == ip)) {
							sender = pClient;
							if (thePrefs.GetLogNatTraversalEvents())
								DebugLog(_T("[NatTraversal] Found source in partfile for ephemeral REASKACK: %s"), (LPCTSTR)pClient->DbgGetClientInfo());
							break;
						}
					}
					listlock.Unlock();
					
					// If source not found in partfile, try to get it from downloadqueue
					if (sender == NULL) {
						if (thePrefs.GetLogNatTraversalEvents())
							DebugLog(_T("[NatTraversal] No matching source found in partfile for ephemeral REASKACK from %s:%u, checking downloadqueue"), (LPCTSTR)ipstr(ip), port);
						
						// Try downloadqueue (might be in dead list or A4AF)
						sender = theApp.downloadqueue->GetDownloadClientByIP_UDP(ip, port, true);
						if (sender == NULL)
							sender = theApp.downloadqueue->GetDownloadClientByIP_UDP(ip, 0, true); // Try by IP only
						
						// If found in downloadqueue, ensure it's added to this partfile
						if (sender != NULL) {
							if (thePrefs.GetLogNatTraversalEvents())
								DebugLog(_T("[NatTraversal] Found source in downloadqueue for ephemeral REASKACK: %s"), (LPCTSTR)sender->DbgGetClientInfo());
							
							// Check if client is in partfile's source list
							bool bInPartFile = false;
							CSingleLock listlock2(&pPartFile->m_FileCompleteMutex);
							listlock2.Lock();
							for (POSITION pos = pPartFile->srclist.GetHeadPosition(); pos != NULL;) {
								CUpDownClient* pClient = pPartFile->srclist.GetNext(pos);
								if (pClient == sender) {
									bInPartFile = true;
									break;
								}
							}
							listlock2.Unlock();
							
							if (!bInPartFile && thePrefs.GetLogNatTraversalEvents())
								DebugLog(_T("[NatTraversal] Source exists in downloadqueue but not in partfile srclist for ephemeral REASKACK"));
						} else {
							// Source not found anywhere - create it for NAT-T lowID-to-lowID scenario
							if (thePrefs.GetLogNatTraversalEvents())
								DebugLog(_T("[NatTraversal] Source not found - creating new client for ephemeral REASKACK from %s:%u"), (LPCTSTR)ipstr(ip), port);
							
							// Create new client object for this source using CAddress
							CAddress addr(ip);
							sender = new CUpDownClient(pPartFile, port, 0, 0, 0, false, addr);
							sender->SetUDPPort(port);
							sender->SetKadPort(port);
							
							// Set IP and ConnectIP for uTP connection
							sender->SetIP(addr);
							sender->SetConnectIP(addr);
							
							// Mark as KAD source
							sender->SetSourceFrom(SF_KADEMLIA);
							
							// Enable NAT-T support
							sender->SetNatTraversalSupport(true);
							
							// Add to download queue
							if (theApp.downloadqueue->CheckAndAddSource(pPartFile, sender, SF_KADEMLIA)) {
								if (thePrefs.GetLogNatTraversalEvents())
									DebugLog(_T("[NatTraversal] Successfully created and added new source for ephemeral REASKACK: %s"), (LPCTSTR)sender->DbgGetClientInfo());
							} else {
								if (thePrefs.GetLogNatTraversalEvents())
									DebugLog(_T("[NatTraversal] Failed to add new source for ephemeral REASKACK from %s:%u"), (LPCTSTR)ipstr(ip), port);
								delete sender;
								sender = NULL;
							}
						}
					}
				}
			}
			
			if (thePrefs.GetDebugClientUDPLevel() > 0)
				DebugRecv("OP_ReaskAck", sender, NULL, ip);
			
			// Check if this is an ephemeral OP_REASKACK (queue position -1) for NAT-T callback
			bool bEphemeral = false;
			if (sender != NULL) {
				CSafeMemFile peek(packet, size);
				if (sender->GetUDPVersion() > 3) {
					// Skip file status if present
					uint16 nPartCount = peek.ReadUInt16();
					if (nPartCount > 0 && peek.GetLength() - peek.GetPosition() >= (nPartCount + 7) / 8)
						peek.Seek((nPartCount + 7) / 8, SEEK_CUR);
				} else if (size >= 2) {
					// UDP version <= 3: no file status, read part count
					uint16 nPartCount = peek.ReadUInt16();
				}
				
				if (peek.GetLength() - peek.GetPosition() >= 2) {
					uint16 nRank = peek.ReadUInt16();
					bEphemeral = (nRank == (uint16)-1 || nRank == 0xFFFF);
					if (bEphemeral && thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal] Detected ephemeral OP_REASKACK: nRank=%u size=%u"), nRank, size);
				}
			}
			
			if (sender && (sender->UDPPacketPending() || bEphemeral)) {
				CSafeMemFile data_in(packet, size);
				if (sender->GetUDPVersion() > 3)
					sender->ProcessFileStatus(true, data_in, sender->GetRequestFile());

				uint16 nRank = data_in.ReadUInt16();
				
				if (bEphemeral && thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal] Processing ephemeral OP_REASKACK (rank=%u) from %s:%u"), nRank, (LPCTSTR)ipstr(ip), port);
				
				sender->SetRemoteQueueFull(false);
				sender->UDPReaskACK(nRank);
				sender->IncrementAskedCountDown();
				
				// For ephemeral REASKACK in NAT-T rendezvous scenario
				// The hole punch/rendezvous is complete - now ensure uTP connection is initiated
				if (bEphemeral && sender->GetDownloadState() != DS_DOWNLOADING && sender->GetDownloadState() != DS_CONNECTED) {
					// Check if client is already connecting with uTP socket
					// The HOLEPUNCH exchange has already initiated the uTP connection and added peer expectation
					// We should NOT call TryToConnect again as it will delete the existing socket and break the connection
					if (sender->socket && sender->socket->HaveUtpLayer()) {
						// Already connecting with uTP - rendezvous complete, just wait for handshake
						if (thePrefs.GetLogNatTraversalEvents())
							DebugLog(_T("[NatTraversal] Ephemeral REASKACK: Already connecting with uTP socket, rendezvous complete for %s"), (LPCTSTR)sender->DbgGetClientInfo());
						// Do NOT call TryToConnect - it would delete the existing uTP socket!
					} else {
						// No uTP socket yet - initiate fresh uTP connection
						if (thePrefs.GetLogNatTraversalEvents())
							DebugLog(_T("[NatTraversal] Ephemeral REASKACK: Rendezvous complete, calling TryToConnect with uTP for %s"), (LPCTSTR)sender->DbgGetClientInfo());
						
						// Call TryToConnect with bUseUTP=true to initiate uTP connection directly
						// TryToConnect handles all socket management, uTP initialization, etc.
						if (!sender->TryToConnect(false, false, NULL, true)) {
							// Client was deleted by TryToConnect
							if (thePrefs.GetLogNatTraversalEvents())
								DebugLog(_T("[NatTraversal] Ephemeral REASKACK: TryToConnect deleted the client (connection failed)"));
							sender = NULL; // Prevent further access
						}
					}
				}
			} else if (sender != NULL)
				DebugLogError(_T("Received UDP Packet (OP_REASKACK) which was not requested (pendingflag == false); Ignored packet - %s"), (LPCTSTR)EscPercent(sender->DbgGetClientInfo()));
		}
		break;
	case OP_FILENOTFOUND:
		{
			theStats.AddDownDataOverheadFileRequest(size);
			CUpDownClient *sender = theApp.downloadqueue->GetDownloadClientByIP_UDP(ip, port, true);
			if (thePrefs.GetDebugClientUDPLevel() > 0)
				DebugRecv("OP_FileNotFound", sender, NULL, ip);
			if (sender != NULL)
				if (sender->UDPPacketPending())
					sender->UDPReaskFNF(); // may delete 'sender'!
				else
					DebugLogError(_T("Received UDP Packet (OP_FILENOTFOUND) which was not requested (pendingflag == false); Ignored packet - %s"), (LPCTSTR)EscPercent(sender->DbgGetClientInfo()));

			break;
		}
	case OP_PORTTEST:
		if (thePrefs.GetDebugClientUDPLevel() > 0)
			DebugRecv("OP_PortTest", NULL, NULL, ip);
		theStats.AddDownDataOverheadOther(size);
		if (size == 1 && packet[0] == 0x12) {
			bool ret = theApp.listensocket->SendPortTestReply('1', true);
			AddDebugLogLine(true, _T("UDP Port check packet arrived - ACK sent back (status=%i)"), ret);
		}
		break;
	case OP_DIRECTCALLBACKREQ:
		{
			if (thePrefs.GetDebugClientUDPLevel() > 0)
				DebugRecv("OP_DIRECTCALLBACKREQ", NULL, NULL, ip);
			if (!theApp.clientlist->AllowCalbackRequest(ip)) {
				DebugLogWarning(_T("Ignored DirectCallback Request because this IP (%s) has sent too many request within a short time"), (LPCTSTR)ipstr(ip));
				break;
			}
			// do we accept callback requests at all?
			if (Kademlia::CKademlia::IsRunning() && Kademlia::CKademlia::IsFirewalled()) {
				theApp.clientlist->AddTrackCallbackRequests(ip);
				CSafeMemFile data(packet, size);
				uint16 nRemoteTCPPort = data.ReadUInt16();
				uchar uchUserHash[MDX_DIGEST_SIZE];
				data.ReadHash16(uchUserHash);
				uint8 byConnectOptions = data.ReadUInt8();
				CUpDownClient *pRequester = theApp.clientlist->FindClientByUserHash(uchUserHash, ip, nRemoteTCPPort);
				if (pRequester == NULL) {
					pRequester = new CUpDownClient(NULL, nRemoteTCPPort, ip.ToUInt32(true), 0, 0, false, ip);
					pRequester->SetUserHash(uchUserHash);
					theApp.clientlist->AddClient(pRequester);
				} else {
					pRequester->SetConnectIP(ip);
					pRequester->SetUserPort(nRemoteTCPPort);
				}
				pRequester->SetConnectOptions(byConnectOptions, true, false);
				pRequester->SetDirectUDPCallbackSupport(false);
				DEBUG_ONLY(DebugLog(_T("Accepting incoming DirectCallbackRequest from %s"), (LPCTSTR)EscPercent(pRequester->DbgGetClientInfo())));
				pRequester->TryToConnect();
			} else
				DebugLogWarning(_T("Ignored DirectCallback Request because we do not accept DirectCall backs at all (%s)"), (LPCTSTR)ipstr(ip));
		}
		break;
default:
		theStats.AddDownDataOverheadOther(size);
		if (thePrefs.GetDebugClientUDPLevel() > 0) {
			CUpDownClient *sender = theApp.downloadqueue->GetDownloadClientByIP_UDP(ip, port, true);
			Debug(_T("Unknown client UDP packet: host=%s:%u (%s) opcode=0x%02x  size=%u\n"), (LPCTSTR)ipstr(ip), port, sender ? (LPCTSTR)sender->DbgGetClientInfo() : EMPTY, opcode, size);
		}
		return false;
	}
	return true;
}

void CClientUDPSocket::OnSend(int nErrorCode)
{
	if (nErrorCode) {
		if (thePrefs.GetVerbose())
			DebugLogError(_T("Error: Client UDP socket, error on send event: %s"), (LPCTSTR)EscPercent(GetErrorMessage(nErrorCode, 1)));
		return;
	}

	sendLocker.Lock();
	m_bWouldBlock = false;

	if (!controlpacket_queue.IsEmpty())
		theApp.uploadBandwidthThrottler->QueueForSendingControlPacket(this);
	sendLocker.Unlock();
}

SocketSentBytes CClientUDPSocket::SendControlData(uint32 maxNumberOfBytesToSend, uint32 /*minFragSize*/)
{
// NOTE: *** This function is invoked from a *different* thread!
	uint32 sentBytes = 0;
	DWORD curTick;

	sendLocker.Lock();
	curTick = ::GetTickCount();
	while (!controlpacket_queue.IsEmpty() && !IsBusy() && sentBytes < maxNumberOfBytesToSend) { // ZZ:UploadBandWithThrottler (UDP)
		UDPPack *cur_packet = controlpacket_queue.RemoveHead();
		if (curTick < cur_packet->dwTime + UDPMAXQUEUETIME) {
			int nLen = (int)cur_packet->packet->size + 2;
			int iLen = cur_packet->bEncrypt && ((cur_packet->IP.GetType() == CAddress::IPv6 ? !theApp.GetPublicIPv6().IsNull() : theApp.GetPublicIPv4() != 0) || cur_packet->bKad)
				? EncryptOverheadSize(cur_packet->bKad) : 0;
			uchar *sendbuffer = new uchar[nLen + iLen];
			memcpy(sendbuffer + iLen, cur_packet->packet->GetUDPHeader(), 2);
			memcpy(sendbuffer + iLen + 2, cur_packet->packet->pBuffer, cur_packet->packet->size);

			if (iLen) {
				nLen = EncryptSendClient(sendbuffer, nLen, cur_packet->pachTargetClientHashORKadID, cur_packet->bKad, cur_packet->nReceiverVerifyKey, (cur_packet->bKad ? Kademlia::CPrefs::GetUDPVerifyKey(cur_packet->IP.ToUInt32(false)) : (uint16)0), cur_packet->IP.GetType() == CAddress::IPv6);
			}
			iLen = SendTo(sendbuffer, nLen, cur_packet->IP, cur_packet->nPort);
			if (iLen >= 0) {
				sentBytes += iLen; // ZZ:UploadBandWithThrottler (UDP)
				delete cur_packet->packet;
				delete cur_packet;
			} else {
				controlpacket_queue.AddHead(cur_packet); //try to resend
				::Sleep(20);
				curTick = ::GetTickCount();
			}
			delete[] sendbuffer;
		} else {
			delete cur_packet->packet;
			delete cur_packet;
		}
	}

	if (!IsBusy() && !controlpacket_queue.IsEmpty())
		theApp.uploadBandwidthThrottler->QueueForSendingControlPacket(this);

	sendLocker.Unlock();

	return SocketSentBytes{ 0, sentBytes, true };
}

int CClientUDPSocket::SendTo(uchar *lpBuf, int nBufLen, CAddress IP, uint16 nPort)
{
	// NOTE: *** This function is invoked from a *different* thread!
	//Currently called only locally; sendLocker must be locked by the caller
	CAddress dwIP = IP;
	dwIP.Convert(CAddress::IPv6);
	SOCKADDR_IN6 sockAddr = { 0 };
	int iSockAddrLen = sizeof(sockAddr);
	dwIP.ToSA((SOCKADDR*)&sockAddr, &iSockAddrLen, nPort);
	int result = CAsyncSocket::SendTo(lpBuf, nBufLen, (SOCKADDR*)&sockAddr, sizeof sockAddr);

	if (result == SOCKET_ERROR) {
		DWORD dwError = (DWORD)CAsyncSocket::GetLastError();
		if (dwError == WSAEWOULDBLOCK) {
			m_bWouldBlock = true;
			return -1; //blocked
		}
		if (thePrefs.GetVerbose())
			DebugLogError(_T("Error: Client UDP socket, failed to send data to %s:%u: %s"), (LPCTSTR)ipstr(dwIP), nPort, (LPCTSTR)EscPercent(GetErrorMessage(dwError, 1)));
		return 0; //error
	}
	return result; //success
}

bool CClientUDPSocket::SendPacket(Packet *packet, const CAddress& IP, uint16 nPort, bool bEncrypt, const uchar *pachTargetClientHashORKadID, bool bKad, uint32 nReceiverVerifyKey)
{
	UDPPack *newpending = new UDPPack;
	newpending->IP = IP;
	newpending->nPort = nPort;
	newpending->packet = packet;
	newpending->dwTime = ::GetTickCount();
	newpending->bEncrypt = bEncrypt && (pachTargetClientHashORKadID != NULL || (bKad && nReceiverVerifyKey != 0));
	newpending->bKad = bKad;
	newpending->nReceiverVerifyKey = nReceiverVerifyKey;

#ifdef _DEBUG
	if (newpending->packet->size > UDP_KAD_MAXFRAGMENT)
		DebugLogWarning(_T("Sending UDP packet > UDP_KAD_MAXFRAGMENT, opcode: %X, size: %u"), packet->opcode, packet->size);
#endif

	if (newpending->bEncrypt && pachTargetClientHashORKadID != NULL)
		md4cpy(newpending->pachTargetClientHashORKadID, pachTargetClientHashORKadID);
	else
		md4clr(newpending->pachTargetClientHashORKadID);
	sendLocker.Lock();
	controlpacket_queue.AddTail(newpending);
	sendLocker.Unlock();

	theApp.uploadBandwidthThrottler->QueueForSendingControlPacket(this);
	return true;
}

bool CClientUDPSocket::Create()
{
	if (thePrefs.GetUDPPort()) {
		CAsyncSocket::Socket(SOCK_DGRAM, FD_READ | FD_WRITE, 0, PF_INET6);

		int iOptVal = 0; // Enable this socket to accept IPv4 and IPv6 packets at the same time
		CAsyncSocket::SetSockOpt(IPV6_V6ONLY, &iOptVal, sizeof iOptVal, IPPROTO_IPV6);

		sockaddr_in6 us = { 0 };
		memset(&us, 0, sizeof(us));
		us.sin6_family = AF_INET6;
		us.sin6_port = htons(thePrefs.GetUDPPort());
		us.sin6_flowinfo = NULL;

		// Convert the IPv6 address to the sin6_addr structure
		struct sockaddr_storage ss;
		int sslen = sizeof(ss);
		if (thePrefs.GetBindAddrA() != NULL && WSAStringToAddressA((char*)thePrefs.GetBindAddrA(), AF_INET6, NULL, (struct sockaddr*)&ss, &sslen) == 0)
			us.sin6_addr = ((struct sockaddr_in6*)&ss)->sin6_addr;
		else
			us.sin6_addr = in6addr_any;

		if (CAsyncSocket::Bind((const SOCKADDR*)&us, sizeof(us)) != FALSE) {
			m_port = thePrefs.GetUDPPort();
			// the default socket size seems to be insufficient for this UDP socket
			// because we tend to drop packets if several arrived at the same time
			int val = 65536; //64*1024
			if (!CAsyncSocket::SetSockOpt(SO_RCVBUF, &val, sizeof val))
				DebugLogError(_T("Failed to increase socket size on UDP socket"));
		}
	} else
		m_port = 0;
	return true;
}

bool CClientUDPSocket::Rebind()
{
	DWORD now = ::GetTickCount();
	uint32 curV4 = theApp.GetPublicIPv4();
	CAddress curV6 = theApp.GetPublicIPv6();
	bool bIPv4Changed = (curV4 != 0 && curV4 != m_dwLastRebindPublicIPv4);
	bool bIPv6Changed = (!curV6.IsNull() && curV6 != m_LastRebindPublicIPv6);
	bool bAnyIpChanged = bIPv4Changed || bIPv6Changed;

	// Apply cooldown: only if the port is the same and the IP hasn't changed
	if (!bAnyIpChanged && thePrefs.GetUDPPort() == m_port && now - m_dwLastRebindTick < UDPSOCKET_REBIND_COOLDOWN_MS)
		return false; // No need to rebind yet

	DWORD prevTick = m_dwLastRebindTick; // Keep previous for rollback if needed
	m_dwLastRebindTick = now; // Tentatively set

	CAsyncSocket::Close();
	bool bOK = Create();
	if (!bOK) {
		// Rollback tick if failed so we can retry soon
		m_dwLastRebindTick = prevTick;
		return false;
	}

	// Update last successful IP snapshot (only on success)
	if (curV4 != 0)
		m_dwLastRebindPublicIPv4 = curV4;

	if (!curV6.IsNull())
		m_LastRebindPublicIPv6 = curV6;

	// If the routing table is empty, trigger a few early Process calls to speed up bootstrap.
	if (Kademlia::CKademlia::IsRunning() && Kademlia::CKademlia::GetRoutingZone() && Kademlia::CKademlia::GetRoutingZone()->GetNumContacts() == 0)
		Kademlia::CKademlia::Process(); // Immediate attempt

	return true;
}

void CClientUDPSocket::RegisterPendingCallback(const CAddress& receiverIP, uint16 receiverPort, const uchar* fileHash)
{
	if (receiverIP.IsNull() || receiverPort == 0 || fileHash == NULL || isnulmd4(fileHash))
		return;

	CSingleLock lock(&m_PendingCallbacksLock, TRUE);
	SIpPort key;
	key.IP = receiverIP;
	key.nPort = receiverPort;

	SPendingCallbackInfo info;
	md4cpy(info.FileHash, fileHash);
	info.Timestamp = ::GetTickCount();

	m_PendingCallbacks[key] = info;

	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NatTraversal] RegisterPendingCallback: %s:%u for file %s"), (LPCTSTR)ipstr(receiverIP), receiverPort, (LPCTSTR)md4str(fileHash));
}

bool CClientUDPSocket::FindPendingCallback(const CAddress& receiverIP, uint16 receiverPort, uchar* outFileHash)
{
	if (receiverIP.IsNull() || receiverPort == 0 || outFileHash == NULL)
		return false;

	CSingleLock lock(&m_PendingCallbacksLock, TRUE);
	SIpPort key;
	key.IP = receiverIP;
	key.nPort = receiverPort;

	auto it = m_PendingCallbacks.find(key);
	if (it != m_PendingCallbacks.end()) {
		md4cpy(outFileHash, it->second.FileHash);
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] FindPendingCallback: Found %s:%u for file %s"), (LPCTSTR)ipstr(receiverIP), receiverPort, (LPCTSTR)md4str(outFileHash));
		// Remove from map after finding (one-time use)
		m_PendingCallbacks.erase(it);
		return true;
	}

	return false;
}

void CClientUDPSocket::CleanupPendingCallbacks()
{
	CSingleLock lock(&m_PendingCallbacksLock, TRUE);
	DWORD now = ::GetTickCount();
	const DWORD TIMEOUT_MS = 60000; // 60 seconds

	for (auto it = m_PendingCallbacks.begin(); it != m_PendingCallbacks.end();) {
		if (now - it->second.Timestamp > TIMEOUT_MS) {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] CleanupPendingCallbacks: Removing expired entry for %s:%u"), (LPCTSTR)ipstr(it->first.IP), it->first.nPort);
			it = m_PendingCallbacks.erase(it);
		} else
			++it;
	}
}

void CClientUDPSocket::ProcessUtpPacket(const BYTE* packet, int size, const struct sockaddr* from, socklen_t fromlen)
{
	if (!m_pUtpContext || !packet || size <= 0 || !from) {
		return;
	}
	CSingleLock runtimeLock(&CUtpSocket::GetRuntimeLock(), TRUE);

	// Extract IP and port for logging
	CAddress IP;
	uint16 nPort = 0;
	IP.FromSA(from, fromlen, &nPort);

	if (thePrefs.GetLogNatTraversalEvents()) {
		DebugLog(_T("[NatTraversal] ProcessUtpPacket: Received uTP packet from %s:%u, size=%d"), (LPCTSTR)ipstr(IP), (UINT)nPort, size);
	}

	// Feed the packet to uTP context for processing
	utp_process_udp(m_pUtpContext, packet, size, from, fromlen);

	// Issue deferred ACKs if any
	utp_issue_deferred_acks(m_pUtpContext);

	// Track inbound uTP frames for diagnostics; writable fallback is handled in ServiceUtpQueuedPackets().
	if (size > 0) {
		bool bMatchedExpectation = false;
		CUpDownClient* pClient = MatchNatExpectation(IP, nPort);
		if (pClient != NULL) {
			bMatchedExpectation = true;
		} else {
			bool bMulti = false;
			pClient = theApp.downloadqueue->GetDownloadClientByIP_UDP(IP, nPort, true, &bMulti);
			if (pClient == NULL)
				pClient = theApp.downloadqueue->GetDownloadClientByIP_UDP(IP, nPort, true);
			if (pClient == NULL) {
				bool bUploadMulti = false;
				pClient = theApp.uploadqueue->GetWaitingClientByIP_UDP(IP, nPort, true, &bUploadMulti);
				if (pClient == NULL)
					pClient = theApp.uploadqueue->GetWaitingClientByIP_UDP(IP, nPort, true);
			}
			if (pClient == NULL)
				pClient = theApp.clientlist->FindUniqueClientByIP(IP);
		}
		if (pClient) {
			EUtpFrameType frameType = UTP_FRAMETYPE_UNKNOWN;
			if (size > 0) {
				uint8 header = packet[0];
				frameType = static_cast<EUtpFrameType>((header >> 4) & 0x0F);
				if (frameType > UTP_FRAMETYPE_SYN)
					frameType = UTP_FRAMETYPE_UNKNOWN;
			}
			pClient->RegisterUtpInboundActivity(frameType);
			const bool bBuddyRelation =
				(pClient == theApp.clientlist->GetServingBuddy())
				|| (pClient == theApp.clientlist->GetServingEServerBuddy())
				|| theApp.clientlist->IsServedBuddy(pClient)
				|| pClient->GetKadState() == KS_CONNECTING_SERVING_BUDDY
				|| pClient->GetKadState() == KS_INCOMING_SERVED_BUDDY;
			if (bMatchedExpectation
				&& !bBuddyRelation
				&& pClient->HasLowID()
				&& IP.IsPublicIP()
				&& nPort != 0)
			{
				bool bLearnedEndpoint = false;
				if (pClient->GetConnectIP().IsNull()) {
					pClient->SetConnectIP(IP);
					bLearnedEndpoint = true;
				}
				if (pClient->GetIP().IsNull()) {
					pClient->SetIP(IP);
					bLearnedEndpoint = true;
				}
				if (pClient->GetKadPort() == 0) {
					pClient->SetKadPort(nPort);
					bLearnedEndpoint = true;
				}
				if (pClient->GetUDPPort() == 0) {
					pClient->SetUDPPort(nPort);
					bLearnedEndpoint = true;
				}
				if (!pClient->GetNatTraversalSupport()) {
					pClient->SetNatTraversalSupport(true);
					bLearnedEndpoint = true;
				}
				if (bLearnedEndpoint && thePrefs.GetLogNatTraversalEvents()) {
					DebugLog(_T("[NatTraversal] ProcessUtpPacket: Learned reusable NAT endpoint %s:%u for %s"),
						(LPCTSTR)ipstr(IP), (UINT)nPort, (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
				}
			}
			if (thePrefs.GetLogNatTraversalEvents()) {
				LPCTSTR pathHint = bMatchedExpectation ? _T("expectation") : _T("resolved endpoint");
				DebugLog(_T("[NatTraversal] ProcessUtpPacket: Observed inbound uTP frame via %s (type=%u size=%d), %s"),
					pathHint, (unsigned)frameType, size, (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
			}
		}
	}
}
