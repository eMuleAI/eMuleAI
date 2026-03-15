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
#include "DeadSourceList.h"
#include "eMuleAI/GeoLite2.h"
#include "eMuleAI/Address.h"
#include <list>

class CClientReqSocket;
class CUpDownClient;
class CSafeMemFile;
namespace Kademlia
{
	class CContact;
	class CUInt128;
};
typedef CTypedPtrList<CPtrList, CUpDownClient*> CUpDownClientPtrList;

#define	NUM_CLIENTLIST_STATS	19
#define BAN_CLEANUP_TIME		MIN2MS(20)

enum EClientPopulation
{
	ClientPopulationAll = 0,
	ClientPopulationActive,
	ClientPopulationArchived
};

//------------CDeletedClient Class----------------------
// this class / list is a bit overkill, but currently needed to avoid any exploit possibility
// it will keep track of certain clients attributes for 2 hours, while the CUpDownClient object might be deleted already
// currently: IP, Port, UserHash
struct PORTANDHASH
{
	uint16 nPort;
	void *pHash;
};

struct IPANDTICS
{
	//uint32 dwIP;
	CAddress IP;
	DWORD dwInserted;
};
struct CONNECTINGCLIENT
{
	CUpDownClient *pClient;
	DWORD dwInserted;
};


class CDeletedClient
{
public:
	explicit CDeletedClient(const CUpDownClient *pClient);
	CArray<PORTANDHASH> m_ItemsList;
	time_t				m_tInserted;
	uint32				m_cBadRequest;
};

enum servingBuddyState
{
	Disconnected,
	Connecting,
	Connected
};

// eServer Buddy relay request tracking
struct EServerRelayRequest
{
	CUpDownClient* pRequester;		// Client requesting the relay
	uchar targetHash[16];			// Target LowID client's user hash (optional, may be null)
	uint32 dwTargetLowID;			// Target's LowID (primary key for lookup)
	uint32 dwRequesterIP;			// Requester's IP (validated)
	uint16 nRequesterKadPort;		// Requester's KAD port
	uint32 dwTargetPublicIP;		// Target public IP from ACK (optional)
	uint16 nTargetKadPort;			// Target port from ACK (fallback)
	bool bAwaitingTargetExtPort;	// Waiting for external port probe before reply
	DWORD dwRequestTime;			// Request timestamp for timeout
	uchar fileHash[16];				// File hash for download context (similar to KAD Rendezvous)
};

// Context to restore reqfile if client object is recreated/merged during connection
struct PendingRelayContext
{
	uint32 dwIP;
	uint16 nPort;
	uchar fileHash[16];
	DWORD dwInserted;
};

struct EServerBuddyMagicCandidate
{
	uint32 dwUserID;
	uint16 nUserPort;
	DWORD dwNextTry;
	uint8 byAttempts;
};

struct EServerBuddyMagicProbe
{
	uchar aucHash[16];
	uint64 uSize;
	uint32 uEpoch;
	uint32 uBucket;
	uint8 byFlags;		// bit0: bootstrap bucket probe
};

// ----------------------CClientList Class---------------
typedef CMap<uint32, uint32, uint32, uint32> CClientVersionMap;
typedef CMap<CString, LPCTSTR, uint32, uint32> CClientVersionNameMap;

struct CClientStatsSnapshot
{
	CClientStatsSnapshot();
	void Reset();

	uint32 totalClients;
	int stats[NUM_CLIENTLIST_STATS];
	CClientVersionMap clientVersionEDonkey;
	CClientVersionMap clientVersionEDonkeyHybrid;
	CClientVersionMap clientVersionEMule;
	CClientVersionNameMap aiVersionMap;
	uint32 aiClientTotal;
	CClientVersionMap clientVersionAMule;
	CMap<CString, LPCTSTR, uint32, uint32> modCounts;
	uint32 totalMODs;
	CMap<CString, LPCTSTR, uint32, uint32> countries;
	uint32 bannedCount;
	uint32 filteredCount;
	uint32 badCount;
};

class CClientList
{
	friend class CClientListCtrl;

public:
	CClientList();
	~CClientList();

	// Clients
	void	AddClient(CUpDownClient *toadd, bool bSkipDupTest = false);
	void	RemoveClient(CUpDownClient *toremove, LPCTSTR pszReason = NULL);
	void	GetStatistics(CClientStatsSnapshot& snapshot, EClientPopulation population) const;
	void	GetStatistics(uint32& ruTotalClients, int stats[NUM_CLIENTLIST_STATS],
		CClientVersionMap& clientVersionEDonkey,
		CClientVersionMap& clientVersionEDonkeyHybrid,
		CClientVersionMap& clientVersionEMule,
		CClientVersionNameMap& aiVersionMap,
		uint32& aiClientTotal,
		CClientVersionMap& clientVersionAMule,
		CMap<POSITION, POSITION, uint32, uint32>& MODs,
		uint32& totalMODs,
		CMap<CString, LPCTSTR, uint32, uint32>& pCountries);

	INT_PTR	GetClientCount()							{ return list.GetCount(); }
	void	DeleteAll();
	bool	AttachToAlreadyKnown(CUpDownClient **client, CClientReqSocket *sender);
	CUpDownClient* FindClientByConnIP(const CAddress& clientip, UINT port) const;
	CUpDownClient* FindClientByIP(const CAddress& clientip, UINT port) const;
	CUpDownClient* FindClientByUserHash(const uchar* clienthash, const CAddress& clientip = CAddress(), uint16 nTCPPort = 0) const;
	CUpDownClient* FindClientByIP(const CAddress& clientip) const;
	CUpDownClient* FindUniqueClientByIP(const CAddress& clientip) const;
	CUpDownClient* FindClientByIP_UDP(const CAddress& clientip, UINT nUDPport) const;
	CUpDownClient* FindClientByServerID(uint32 uServerIP, uint32 uED2KUserID) const;
	CUpDownClient* FindClientByUserID_KadPort(uint32 clientID, uint16 kadPort) const;
	CUpDownClient* FindClientByIP_KadPort(const CAddress& clientip, uint16 port) const;

	// Banned clients
	void	AddBannedClient(CString strKey, time_t tInserted = time(NULL));
	bool	IsBannedClient(CString strKey) const;
	void	RemoveBannedClient(CString strKey);
	INT_PTR	GetBannedCount() const						{ return m_bannedList.GetCount(); }
	void	RemoveAllBannedClients();

	void CClientList::CancelCategoryPunishments(uint8 uBadClientCategory) const; 
	void CClientList::CancelFriendPunishments() const;
	uint32 CClientList::BadClientCount();

	// Tracked clients
	void AddTrackClient(CUpDownClient* toadd, time_t tInserted = time(NULL));
	bool	ComparePriorUserhash(CAddress IP, uint16 nPort, const void* pNewHash);
	INT_PTR	GetClientsFromIP(CAddress IP) const;
	void	TrackBadRequest(const CUpDownClient *upcClient, int nIncreaseCounter);
	uint32	GetBadRequests(const CUpDownClient *upcClient) const;
	INT_PTR	GetTrackedCount() const						{ return m_trackedClientsMap.GetCount(); }
	void	RemoveAllTrackedClients();

	// Kad client list, buddy handling
	bool	RequestTCP(Kademlia::CContact *contact, uint8 byConnectOptions);
	void	RequestServingBuddy(Kademlia::CContact *contact, uint8 byConnectOptions, bool bForce);
	bool	IncomingServedBuddy(Kademlia::CContact *contact, Kademlia::CUInt128 *servedBuddyID);
	void	RemoveFromKadList(CUpDownClient *torem);
	void	AddToKadList(CUpDownClient *toadd);
	bool	DoRequestFirewallCheckUDP(const Kademlia::CContact &contact);
	uint8	GetServingBuddyStatus() const				{ return m_nServingBuddyStatus; }
	bool	IsKadBuddySearchActive() const;
	CUpDownClient* GetServingBuddy() const				{ return m_pServingBuddy; }
	INT_PTR GetServedBuddyCount() const { return m_ServedBuddyMap.GetCount(); }
	POSITION GetServedBuddyStartPosition() const { return m_ServedBuddyMap.GetStartPosition(); }
	CUpDownClient* GetNextServedBuddy(POSITION& pos) const {
		if (!pos)
			return NULL;
		CUpDownClient* pKey = NULL;
		CUpDownClient* pVal = NULL;
		m_ServedBuddyMap.GetNextAssoc(pos, pKey, pVal);
		return pVal;
	}
	bool IsServedBuddy(const CUpDownClient* pClient) const;
	void AddServedBuddy(CUpDownClient* pClient);
	void RemoveServedBuddy(const CUpDownClient* pClient);
	void ClearAllServedBuddies();
	CUpDownClient* FindServedBuddyByKadID(const Kademlia::CUInt128& kadid) const;
	CUpDownClient* FindServedBuddyByAddr(const CAddress& ip, uint16 udpPort) const;

	// eServer Buddy handling (LowID relay)
	uint8	GetEServerBuddyStatus() const				{ return m_nEServerBuddyStatus; }
	bool	IsEServerBuddySearchActive() const;
	CUpDownClient* GetServingEServerBuddy() const		{ return m_pServingEServerBuddy; }
	uint32	GetEServerBuddyServerIP() const				{ return m_dwEServerBuddyServerIP; }
	uint8	GetMaxEServerBuddySlots() const				{ return m_nMaxEServerBuddySlots; }
	INT_PTR	GetServedEServerBuddyCount() const			{ return m_lstServedEServerBuddies.GetCount(); }
	POSITION GetServedEServerBuddyStartPosition() const	{ return m_lstServedEServerBuddies.GetHeadPosition(); }
	CUpDownClient* GetNextServedEServerBuddy(POSITION& pos) const { return m_lstServedEServerBuddies.GetNext(pos); }
	CUpDownClient* FindEServerBuddy();					// Find suitable HighID buddy for LowID relay
	CUpDownClient* FindServedEServerBuddyByHash(const uchar* pHash) const;	// Find served buddy by hash
	CUpDownClient* FindServedEServerBuddyByLowID(uint32 dwServerIP, uint32 dwTargetLowID) const;
	void	SetServingEServerBuddy(CUpDownClient* pBuddy, uint8 nStatus);
	void	AddServedEServerBuddy(CUpDownClient* pClient);
	void	RemoveServedEServerBuddy(CUpDownClient* pClient);
	bool	IsServedEServerBuddy(const CUpDownClient* pClient) const;
	bool	HasEServerBuddySlotAvailable() const		{ return GetServedEServerBuddyCount() < m_nMaxEServerBuddySlots; }
	void	TryRequestEServerBuddy();					// Try to find and request a buddy if we need one
	void	ClearAllServedEServerBuddies();				// Clear all served buddies (for server disconnect)

	// eServer Buddy relay request tracking
	void	AddPendingEServerRelay(CUpDownClient* pRequester, const uchar* targetHash, uint32 dwIP, uint16 nPort);
	void	AddPendingEServerRelayByLowID(CUpDownClient* pRequester, uint32 dwTargetLowID, uint32 dwIP, uint16 nPort, const uchar* pFileHash = NULL);
	void	UpdatePendingEServerRelayRequesterPort(const CUpDownClient* pRequester, uint16 nPort);
	bool	TrySendPendingEServerRelayResponseForTarget(CUpDownClient* pTarget);
	EServerRelayRequest* FindPendingEServerRelay(const uchar* targetHash);
	EServerRelayRequest* FindPendingEServerRelayByLowID(uint32 dwTargetLowID);
	void	RemovePendingEServerRelay(const uchar* targetHash);
	void	RemovePendingEServerRelayByLowID(uint32 dwTargetLowID);
	void	CleanupExpiredEServerRelays();
	void	ProcessEServerBuddyPings();					// Periodic ping and cleanup
	bool	ProcessEServerBuddyMagicSourceAnswer(const uchar* pHash, CSafeMemFile& sources, bool bWithObfuscationAndHash, uint32 dwServerIP, uint16 nServerPort);
	
	// Pending relay context (for ConnectionEstablished)
	void	AddPendingRelayContext(uint32 dwIP, uint16 nPort, const uchar* pFileHash);
	bool	ApplyPendingRelayContext(CUpDownClient* pClient);
	void	CleanupPendingRelayContexts();



	void	AddKadFirewallRequest(CAddress IP);
	bool	IsKadFirewallCheckIP(CAddress IP) const;

	// Direct Callback List
	void	AddTrackCallbackRequests(CAddress IP);
	bool	AllowCalbackRequest(CAddress IP) const;

	// Connecting Clients
	void	AddConnectingClient(CUpDownClient *pToAdd);
	void	RemoveConnectingClient(const CUpDownClient *pToRemove);

	void	Process();
	bool	IsValidClient(CUpDownClient *tocheck) const;
	bool	IsClientActive(const CUpDownClient *tocheck) const;
	void	Debug_SocketDeleted(CClientReqSocket *deleted) const;

	void	CleanUp(CPartFile* pDeletedFile);
	void	CleanUpClientList();

	bool GiveClientsForTraceRoute();

	void	ProcessA4AFClients() const; // ZZ:DownloadManager
	CDeadSourceList	m_globDeadSourceList;

	void	ResetGeoLite2();

	void	GetModStatistics(CRBMap<uint32, CRBMap<CString, uint32>* >* clientMods);
	void	ReleaseModStatistics(CRBMap<uint32, CRBMap<CString, uint32>* >* clientMods);

	CString GetMODType(POSITION pos_in)
	{
		if (pos_in != 0)
			return liMODsTypes.GetAt(pos_in);
		else
			return EMPTY;
	}

	typedef	CMap<CString, LPCTSTR, CUpDownClient*, CUpDownClient*> CArchivedClientsMap;
	CArchivedClientsMap m_ArchivedClientsMap; // This map is used hold client history
	void	SaveList();
	bool	LoadList();
	time_t	m_tLastSaved;

	void AutoQuerySharedFiles();
	const static bool SortFunc(const CUpDownClient* first, const CUpDownClient* second);

	void TrigReask(bool bIPv6Change);
	CUpDownClientPtrList list;
public:
	void	ServiceNatTraversalRetries();
	void	ServiceUtpConnectionTimeouts();  // Check for stuck uTP handshakes
	void	ServiceUtpQueuedPackets();       // Flush queued packets for uTP connections
protected:
	void	ProcessConnectingClientsList();
	void	CollectClientStatsFromClient(CClientStatsSnapshot& snapshot, const CUpDownClient* curClient) const;
	CList<CString, CString&> liMODsTypes;

private:
	CMap<CUpDownClient*, CUpDownClient*, CUpDownClient*, CUpDownClient*> m_ServedBuddyMap;
	CUpDownClientPtrList m_KadList;
	//CMap<uint32, uint32, DWORD, DWORD> m_bannedList;
	CMap<CString, LPCTSTR, time_t, time_t> m_bannedList; // By this change, this map now stores banned user hashes in addition to banned IP addresses 
	//typedef CMap<uint32, uint32, CDeletedClient*, CDeletedClient*> CDeletedClientMap;
	typedef CMap<CString, LPCTSTR, CDeletedClient*, CDeletedClient*> CDeletedClientMap;
	CDeletedClientMap m_trackedClientsMap;
	CUpDownClient *m_pServingBuddy;
	CList<IPANDTICS> listFirewallCheckRequests;
	CList<IPANDTICS> listDirectCallbackRequests;
	CList<CONNECTINGCLIENT> m_liConnectingClients;
	time_t	m_tLastBanCleanUp;
	time_t	m_tLastTrackedCleanUp;
	time_t	m_tLastClientCleanUp;
	uint8	m_nServingBuddyStatus;

	// eServer Buddy state (LowID relay for LowID-to-LowID transfers)
	uint8	m_nEServerBuddyStatus;				// Disconnected/Connecting/Connected
	CUpDownClient* m_pServingEServerBuddy;		// HighID buddy serving us
	uint32	m_dwEServerBuddyServerIP;			// Server IP our buddy is connected to
	CTypedPtrList<CPtrList, CUpDownClient*> m_lstServedEServerBuddies;	// LowID clients we serve
	uint8	m_nMaxEServerBuddySlots;			// Max slots for serving (configurable)
	std::list<EServerRelayRequest> m_lstPendingEServerRelays;	// Pending relay requests
	std::list<PendingRelayContext> m_lstPendingRelayContexts;   // Pending contexts for ConnectionEstablished
	std::list<EServerBuddyMagicCandidate> m_lstEServerBuddyMagicCandidates;
	std::list<EServerBuddyMagicProbe> m_lstEServerBuddyMagicProbes;
	DWORD	m_dwLastEServerBuddyPing;			// Last ping time
	DWORD	m_dwLastEServerBuddyKeepAlive;		// Last short keep-alive time
	DWORD	m_dwLastEServerBuddyMagicSearch;
	DWORD	m_dwLastEServerBuddyMagicSearchSent;
	uint32	m_dwLastEServerBuddyMagicSearchServerIP;
	uint16	m_nLastEServerBuddyMagicSearchServerPort;
	uchar	m_abyEServerBuddyMagicInFlightHash[16];
	uint64	m_uEServerBuddyMagicInFlightSize;
	uint32	m_uEServerBuddyMagicRoundSalt;
	bool	m_bEServerBuddyMagicSearchInFlight;
	bool	m_bEServerBuddyMagicSearchPrimed;
	bool	m_bEServerBuddyMagicInFlightProbeValid;

	void	ResetEServerBuddyMagicSearchState();
	void	PrepareEServerBuddyMagicProbeRound();
	bool	TrySendNextEServerBuddyMagicProbe();
	void	StartEServerBuddyMagicSearch();
	bool	TryRequestEServerBuddyFromMagicCandidates();
};
