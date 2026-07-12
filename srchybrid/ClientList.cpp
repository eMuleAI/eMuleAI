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
#include "ClientList.h"
#include "Kademlia/Kademlia/kademlia.h"
#include "Kademlia/Kademlia/prefs.h"
#include "Kademlia/Kademlia/search.h"
#include "Kademlia/Kademlia/searchmanager.h"
#include "Kademlia/routing/contact.h"
#include "Kademlia/routing/RoutingZone.h"
#include "Kademlia/net/kademliaudplistener.h"
#include "kademlia/kademlia/UDPFirewallTester.h"
#include "kademlia/utils/UInt128.h"
#include "LastCommonRouteFinder.h"
#include "UpDownClient.h"
#include "UploadQueue.h"
#include "DownloadQueue.h"
#include "ClientCredits.h"
#include "ListenSocket.h"
#include "ClientUDPSocket.h"
#include "Opcodes.h"
#include "ServerConnect.h"
#include "emuledlg.h"
#include "TransferDlg.h"
#include "serverwnd.h"
#include "FriendList.h"
#include "Friend.h"
#include <vector>
#include "Log.h"
#include "PartFile.h"
#include "packets.h"
#include "Statistics.h"
#include <io.h>
#include <algorithm>
#include <vector>
#include "SharedFileList.h"
#include "Server.h"
#include "PartFileWriteThread.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	const size_t kLargeMetFileBufferSize = 256 * 1024;

	bool GuardClientListMutation(LPCTSTR pszEntryPoint)
	{
		return theApp.GuardModelMutation(CemuleApp::ModelMutationClientList, pszEntryPoint);
	}

	bool ShouldSkipClientPersistenceForManualLeakDump()
	{
#if defined(_DEBUG) && defined(DEBUGLEAKHELPER)
		TCHAR szManualDump[8] = {};
		const DWORD dwManualDump = GetEnvironmentVariable(_T("EMULE_CRT_FORCE_MANUAL_DUMP"), szManualDump, _countof(szManualDump));
		return dwManualDump > 0 && dwManualDump < _countof(szManualDump) && szManualDump[0] != _T('0');
#else
		return false;
#endif
	}

	Kademlia::CUInt128 MakeKadIDFromRawData(const uchar* pucKadID)
	{
		Kademlia::CUInt128 uKadID;
		if (pucKadID != NULL)
			md4cpy(uKadID.GetDataPtr(), pucKadID);
		return uKadID;
	}

	CUpDownClient* AcquireArchivedClientForRuntimeLinkedUse(CUpDownClient* pClient)
	{
		if (pClient == NULL || pClient->m_bIsArchived || theApp.clientlist == NULL || !thePrefs.GetClientHistory() || !theApp.ClientHistoryReady())
			return NULL;

		CUpDownClient* pArchivedClient = pClient->AcquireArchivedClient();
		if (pArchivedClient == NULL && !IsCorruptOrBadUserHash(pClient->GetUserHash())) {
			pArchivedClient = theApp.clientlist->AcquireArchivedClientByUserHash(pClient->GetUserHash());
			if (pArchivedClient != NULL)
				pClient->SetArchivedClientLink(pArchivedClient);
		}

		if (pArchivedClient == NULL || pArchivedClient == pClient || !pArchivedClient->m_bIsArchived) {
			if (pArchivedClient != NULL)
				pArchivedClient->ReleaseRuntimeReference();
			return NULL;
		}

		return pArchivedClient;
	}

	bool IsPendingDeleteReadyForFinalize(const CUpDownClient* pClient)
	{
		return pClient != NULL
			&& ::InterlockedCompareExchange((LONG*)&pClient->m_lDeletePending, 0, 0) != 0
			&& ::InterlockedCompareExchange((LONG*)&pClient->m_lRuntimeReferenceCount, 0, 0) == 0;
	}

	bool IsPendingDeleteRuntimeMapEntry(const CUpDownClient* pClient)
	{
		return pClient != NULL
			&& ::InterlockedCompareExchange((LONG*)&pClient->m_lDeletePending, 0, 0) != 0
			&& ::InterlockedCompareExchange((LONG*)&pClient->m_lDeleteFinalized, 0, 0) == 0;
	}

	bool IsCurrentED2KServer(uint32 dwServerIP, uint16 nServerPort)
	{
		return theApp.serverconnect != NULL
			&& theApp.serverconnect->IsConnected()
			&& theApp.serverconnect->GetCurrentServer() != NULL
			&& dwServerIP != 0
			&& nServerPort != 0
			&& dwServerIP == theApp.serverconnect->GetCurrentServer()->GetIP()
			&& nServerPort == theApp.serverconnect->GetCurrentServer()->GetPort();
	}

	bool IsEServerBuddyCandidateOnCurrentServer(const CUpDownClient* pClient)
	{
		if (pClient == NULL || theApp.serverconnect == NULL || !theApp.serverconnect->IsConnected() || theApp.serverconnect->GetCurrentServer() == NULL)
			return false;

		const uint32 dwCurrentServerIP = theApp.serverconnect->GetCurrentServer()->GetIP();
		if (pClient->GetServerIP() != 0 && pClient->GetServerPort() != 0)
			return IsCurrentED2KServer(pClient->GetServerIP(), pClient->GetServerPort());
		if (pClient->GetServerIP() != 0 && pClient->GetServerIP() != dwCurrentServerIP)
			return false;

		const uint32 dwReportedServerIP = pClient->GetReportedServerIP();
		if (dwReportedServerIP != 0)
			return dwReportedServerIP == dwCurrentServerIP;
		return pClient->GetServerIP() != 0 && pClient->GetServerIP() == dwCurrentServerIP;
	}

	void RefreshEServerMagicCandidateServerContext(CUpDownClient* pClient, uint32 dwUserID, uint16 nUserPort, uint32 dwServerIP, uint16 nServerPort)
	{
		if (pClient == NULL || dwServerIP == 0 || nServerPort == 0)
			return;

		const uint32 dwOldServerIP = pClient->GetServerIP();
		const uint16 nOldServerPort = pClient->GetServerPort();
		const uint32 dwOldReportedServerIP = pClient->GetReportedServerIP();
		const uint16 nOldUserPort = pClient->GetUserPort();
		const bool bContextChanged = dwOldServerIP != dwServerIP || nOldServerPort != nServerPort || dwOldReportedServerIP != dwServerIP;
		const bool bPortChanged = nUserPort != 0 && nOldUserPort != nUserPort;

		// Do not copy the magic-source user ID into UserIDHybrid here.
		// The magic answer and CUpDownClient store the same IP with different byte-order conventions.
		// Overwriting an existing client can make the next TCP connect target the byte-swapped IP.
		if (nUserPort != 0 && nOldUserPort != nUserPort)
			pClient->SetUserPort(nUserPort);

		pClient->SetServerIP(dwServerIP);
		pClient->SetServerPort(nServerPort);
		pClient->SetReportedServerIP(dwServerIP);
		pClient->SetLastEServerBuddyValidServerPing(0);
		if (bContextChanged || bPortChanged) {
			const DWORD dwRetryAfter = pClient->GetEServerBuddyRetryAfter();
			if (dwRetryAfter != 0) {
				const DWORD dwRetryLimit = ::GetTickCount() + ESERVERBUDDY_GENERIC_REJECT_RETRY + SEC2MS(5);
				if ((INT)(dwRetryAfter - dwRetryLimit) <= 0)
					pClient->ClearEServerBuddyRetryAfter();
			}
		}

		if (thePrefs.GetLogNatTraversalEvents()
			&& (dwOldServerIP != dwServerIP || nOldServerPort != nServerPort || dwOldReportedServerIP != dwServerIP || nOldUserPort != nUserPort))
		{
			DebugLog(_T("[eServerBuddy] Magic candidate refreshed server context: %s oldServer=%s:%u oldReported=%s newServer=%s:%u candidateID=%u oldPort=%u newPort=%u"),
				(LPCTSTR)EscPercent(pClient->DbgGetClientInfo()),
				(LPCTSTR)ipstr(dwOldServerIP), nOldServerPort,
				(LPCTSTR)ipstr(dwOldReportedServerIP),
				(LPCTSTR)ipstr(dwServerIP), nServerPort,
				dwUserID,
				nOldUserPort, nUserPort);
		}
	}
}

#define CLIENT_HISTORY_MET_FILENAME	_T("clienthistory.met")
#define CLIENT_HISTORY_MET_FILENAME_TMP	_T("clienthistory.met.tmp")

CClientList::CClientList()
	: m_pServingBuddy()
	, m_nServingBuddyStatus(Disconnected)
	, m_nEServerBuddyStatus(Disconnected)
	, m_pServingEServerBuddy(NULL)
	, m_dwEServerBuddyServerIP(0)
	, m_nEServerBuddyServerPort(0)
	, m_dwEServerBuddyConnectStart(0)
	, m_nMaxEServerBuddySlots(static_cast<uint8>(thePrefs.GetMaxEServerBuddySlots()))
	, m_dwLastEServerBuddyMagicSearch(0)
	, m_dwLastEServerBuddyMagicSearchSent(0)
	, m_dwLastEServerBuddyMagicSearchServerIP(0)
	, m_nLastEServerBuddyMagicSearchServerPort(0)
	, m_uEServerBuddyMagicInFlightSize(0)
	, m_uEServerBuddyMagicRoundSalt(0)
	, m_bEServerBuddyMagicSearchInFlight(false)
	, m_bEServerBuddyMagicSearchPrimed(false)
	, m_bEServerBuddyMagicInFlightProbeValid(false)
	, m_lClientHistorySaveGeneration(0)
{
	m_tLastBanCleanUp = m_tLastTrackedCleanUp = m_tLastClientCleanUp = time(NULL);
	m_ActiveClientsByRuntimeID.reserve(2048);
	m_ArchivedClientsByRuntimeID.reserve(2048);
	m_bannedList.InitHashTable(331);
	m_trackedClientsMap.InitHashTable(2011);
	m_globDeadSourceList.Init(true);
	m_dwLastEServerBuddyPing = 0;
	m_dwLastEServerBuddyKeepAlive = 0;
	m_dwLastEServerBuddyKadBridgeRequest = 0;
	m_dwLastKadBuddyEServerBridgeRequest = 0;
	m_dwLastKadBuddyDemandCheck = 0;
	m_dwLastKadBuddyDemandSearch = 0;
	md4clr(m_abyEServerBuddyMagicInFlightHash);
}

CClientList::~CClientList()
{
	CancelAutoQuerySharedFilesJob();
	CancelClientHistorySaveJob();
	RemoveAllTrackedClients();
}

CClientStatsSnapshot::CClientStatsSnapshot()
{
	Reset();
}

void CClientStatsSnapshot::Reset()
{
	totalClients = 0;
	for (int index = 0; index < NUM_CLIENTLIST_STATS; ++index)
		stats[index] = 0;
	aiClientTotal = 0;
	clientVersionEDonkey.RemoveAll();
	clientVersionEDonkeyHybrid.RemoveAll();
	clientVersionEMule.RemoveAll();
	aiVersionMap.RemoveAll();
	clientVersionAMule.RemoveAll();
	modCounts.RemoveAll();
	totalMODs = 0;
	countries.RemoveAll();
	bannedCount = 0;
	filteredCount = 0;
	badCount = 0;
}

namespace
{
	void IncrementVersionCount(CClientVersionMap& versionMap, uint32 version)
	{
		uint32 count = 0;
		if (versionMap.Lookup(version, count))
			versionMap.SetAt(version, count + 1);
		else
			versionMap.SetAt(version, 1);
	}

	void IncrementStringCount(CMap<CString, LPCTSTR, uint32, uint32>& mapObject, const CString& key)
	{
		uint32 count = 0;
		if (mapObject.Lookup(key, count))
			mapObject.SetAt(key, count + 1);
		else
			mapObject.SetAt(key, 1);
	}

	enum
	{
		ESERVER_BUDDY_MAGIC_FLAG_BOOTSTRAP = 0x01
	};

	void RefreshConnectionStateIndicators()
	{
		if (theApp.emuledlg == NULL)
			return;
		theApp.emuledlg->ShowConnectionState();
	}
}

void CClientList::CollectClientStatsFromClient(CClientStatsSnapshot& snapshot, const CUpDownClient* curClient) const
{
	if (curClient == NULL)
		return;

	if (curClient->HasLowID())
		++snapshot.stats[14];

	switch (curClient->GetClientSoft()) {
	case SO_EMULE:
	case SO_OLDEMULE:
	{
		++snapshot.stats[2];
		CString aiVersion;
		if (curClient->GetAIModVersionString(aiVersion)) {
			++snapshot.aiClientTotal;
			CString aiVersionLabel;
			aiVersionLabel.Format(_T("v%s"), (LPCTSTR)aiVersion);
			IncrementStringCount(snapshot.aiVersionMap, aiVersionLabel);
		} else
			IncrementVersionCount(snapshot.clientVersionEMule, curClient->GetVersion());

		CString strMODName = curClient->GetClientModVer();
		if (!strMODName.IsEmpty()) {
			int length = strMODName.GetLength();
			int index;
			for (index = 0; index < length; ++index) {
				if (strMODName.GetAt(index) >= _T('0') && strMODName.GetAt(index) <= _T('9'))
					break;
			}
			if (index < length && index > 0)
				strMODName = strMODName.Left(index);
			if (strMODName.Right(1) == _T("v") && strMODName.GetLength() > 2)
				strMODName = strMODName.Left(strMODName.GetLength() - 1);
			strMODName.Trim();
			if (!strMODName.IsEmpty()) {
				IncrementStringCount(snapshot.modCounts, strMODName);
				++snapshot.totalMODs;
			}
		}
		break;
	}
	case SO_EDONKEYHYBRID:
		++snapshot.stats[4];
		IncrementVersionCount(snapshot.clientVersionEDonkeyHybrid, curClient->GetVersion());
		break;
	case SO_AMULE:
		++snapshot.stats[10];
		IncrementVersionCount(snapshot.clientVersionAMule, curClient->GetVersion());
		break;
	case SO_EDONKEY:
		++snapshot.stats[1];
		IncrementVersionCount(snapshot.clientVersionEDonkey, curClient->GetVersion());
		break;
	case SO_MLDONKEY:
		++snapshot.stats[3];
		break;
	case SO_SHAREAZA:
		++snapshot.stats[11];
		break;
	case SO_HYDRANODE:
	case SO_HYDRA:
	case SO_EMULEPLUS:
	case SO_TRUSTYFILES:
	case SO_EASYMULE2:
	case SO_NEOLOADER:
	case SO_KMULE:
	case SO_CDONKEY:
	case SO_XMULE:
	case SO_LPHANT:
		++snapshot.stats[5];
		break;
	default:
		++snapshot.stats[0];
	}

	IncrementStringCount(snapshot.countries, curClient->m_structClientGeolocationData.Country);

	if (curClient->Credits() != NULL) {
		switch (curClient->Credits()->GetCurrentIdentState(curClient->GetIP())) {
		case IS_IDENTIFIED:
			++snapshot.stats[12];
			break;
		case IS_IDFAILED:
		case IS_IDNEEDED:
		case IS_IDBADGUY:
			++snapshot.stats[13];
		}
	}

	if (curClient->GetDownloadState() == DS_ERROR)
		++snapshot.stats[6];

	if (curClient->GetUserPort() == 4662)
		++snapshot.stats[8];
	else
		++snapshot.stats[9];

	if (curClient->GetServerIP() && curClient->GetServerPort()) {
		++snapshot.stats[15];
		if (curClient->GetKadPort()) {
			++snapshot.stats[17];
			++snapshot.stats[16];
		}
	} else if (curClient->GetKadPort())
		++snapshot.stats[16];
	else
		++snapshot.stats[18];
}

void CClientList::GetStatistics(CClientStatsSnapshot& snapshot, EClientPopulation population) const
{
	snapshot.Reset();

	// Collect statistics based on population type
	if (population == ClientPopulationActive) {
		// Active clients only
		for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
			const CUpDownClient* curClient = list.GetNext(pos);
			if (curClient != NULL) {
				++snapshot.totalClients;
				CollectClientStatsFromClient(snapshot, curClient);
				
				// Count Banned and Bad for Active clients
				if (curClient->IsBanned())
					++snapshot.bannedCount;
				if (curClient->IsBadClient())
					++snapshot.badCount;
			}
		}
	} else if (population == ClientPopulationArchived) {
		// Archived clients only
		for (POSITION pos = m_ArchivedClientsMap.GetStartPosition(); pos != NULL;) {
			CString key;
			CUpDownClient* curClient = NULL;
			m_ArchivedClientsMap.GetNextAssoc(pos, key, curClient);
			if (curClient != NULL) {
				++snapshot.totalClients;
				CollectClientStatsFromClient(snapshot, curClient);
				
				// Count Banned and Bad for Archived clients
				if (curClient->IsBanned())
					++snapshot.bannedCount;
				if (curClient->IsBadClient())
					++snapshot.badCount;
			}
		}
	} else if (population == ClientPopulationAll) {
		// All clients: Active + Archived
		// First collect Active clients
		for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
			const CUpDownClient* curClient = list.GetNext(pos);
			if (curClient != NULL) {
				++snapshot.totalClients;
				CollectClientStatsFromClient(snapshot, curClient);
				
				// Count Banned and Bad
				if (curClient->IsBanned())
					++snapshot.bannedCount;
				if (curClient->IsBadClient())
					++snapshot.badCount;
			}
		}
		
		// Then collect Archived clients
		for (POSITION pos = m_ArchivedClientsMap.GetStartPosition(); pos != NULL;) {
			CString key;
			CUpDownClient* curClient = NULL;
			m_ArchivedClientsMap.GetNextAssoc(pos, key, curClient);
			if (curClient != NULL) {
				++snapshot.totalClients;
				CollectClientStatsFromClient(snapshot, curClient);
				
				// Count Banned and Bad
				if (curClient->IsBanned())
					++snapshot.bannedCount;
				if (curClient->IsBadClient())
					++snapshot.badCount;
			}
		}
	}
}

void CClientList::GetStatistics(uint32 &ruTotalClients
	, int stats[NUM_CLIENTLIST_STATS]
	, CClientVersionMap &clientVersionEDonkey
	, CClientVersionMap &clientVersionEDonkeyHybrid
	, CClientVersionMap &clientVersionEMule
	, CClientVersionNameMap &aiVersionMap
	, uint32 &aiClientTotal
	, CClientVersionMap &clientVersionAMule
	, CMap<POSITION, POSITION, uint32, uint32>& MODs
	, uint32& totalMODs
	, CMap<CString, LPCTSTR, uint32, uint32>& pCountries)
{
	CClientStatsSnapshot snapshot;
	GetStatistics(snapshot, ClientPopulationActive);

	ruTotalClients = snapshot.totalClients;
	for (int index = 0; index < NUM_CLIENTLIST_STATS; ++index)
		stats[index] = snapshot.stats[index];

	clientVersionEDonkey.RemoveAll();
	for (POSITION posEDonkey = snapshot.clientVersionEDonkey.GetStartPosition(); posEDonkey != NULL;) {
		uint32 version = 0;
		uint32 count = 0;
		snapshot.clientVersionEDonkey.GetNextAssoc(posEDonkey, version, count);
		clientVersionEDonkey.SetAt(version, count);
	}

	clientVersionEDonkeyHybrid.RemoveAll();
	for (POSITION posHybrid = snapshot.clientVersionEDonkeyHybrid.GetStartPosition(); posHybrid != NULL;) {
		uint32 version = 0;
		uint32 count = 0;
		snapshot.clientVersionEDonkeyHybrid.GetNextAssoc(posHybrid, version, count);
		clientVersionEDonkeyHybrid.SetAt(version, count);
	}

	clientVersionEMule.RemoveAll();
	for (POSITION posEmule = snapshot.clientVersionEMule.GetStartPosition(); posEmule != NULL;) {
		uint32 version = 0;
		uint32 count = 0;
		snapshot.clientVersionEMule.GetNextAssoc(posEmule, version, count);
		clientVersionEMule.SetAt(version, count);
	}

	aiClientTotal = snapshot.aiClientTotal;
	aiVersionMap.RemoveAll();
	for (POSITION posAi = snapshot.aiVersionMap.GetStartPosition(); posAi != NULL;) {
		CString versionLabel;
		uint32 count = 0;
		snapshot.aiVersionMap.GetNextAssoc(posAi, versionLabel, count);
		aiVersionMap.SetAt(versionLabel, count);
	}

	clientVersionAMule.RemoveAll();
	for (POSITION posAMule = snapshot.clientVersionAMule.GetStartPosition(); posAMule != NULL;) {
		uint32 version = 0;
		uint32 count = 0;
		snapshot.clientVersionAMule.GetNextAssoc(posAMule, version, count);
		clientVersionAMule.SetAt(version, count);
	}

	totalMODs = snapshot.totalMODs;
	MODs.RemoveAll();
	static uint32 lastmodlistclean;
	if (::GetTickCount() - lastmodlistclean > HR2MS(6))
	{
		lastmodlistclean = ::GetTickCount();
		liMODsTypes.RemoveAll();
	}

	for (POSITION posMods = snapshot.modCounts.GetStartPosition(); posMods != NULL;) {
		CString modName;
		uint32 count = 0;
		snapshot.modCounts.GetNextAssoc(posMods, modName, count);
		if (modName.IsEmpty())
			continue;
		POSITION listPos = liMODsTypes.Find(modName);
		if (!listPos)
			listPos = liMODsTypes.AddTail(modName);
		MODs.SetAt(listPos, count);
	}

	pCountries.RemoveAll();
	for (POSITION posCountry = snapshot.countries.GetStartPosition(); posCountry != NULL;) {
		CString country;
		uint32 count = 0;
		snapshot.countries.GetNextAssoc(posCountry, country, count);
		pCountries.SetAt(country, count);
	}
}

void CClientList::AddClient(CUpDownClient *toadd, bool bSkipDupTest)
{
	if (!GuardClientListMutation(_T("CClientList::AddClient")))
		return;

	// skipping the check for duplicate list entries is only to be done for optimization purposes, if the calling
	// function has ensured that this client instance is not already within the list -> there are never duplicate
	// client instances in this list.
	if (bSkipDupTest || list.Find(toadd) == NULL) {
		list.AddTail(toadd);
		{
			CSingleLock runtimeMapLock(&m_csTrackedClientRuntimeMaps, TRUE);
			m_ActiveClientsByRuntimeID[toadd->GetRuntimeID()] = toadd;
		}
		theApp.emuledlg->transferwnd->GetClientList()->AddClient(toadd);
	}
}

bool CClientList::GiveClientsForTraceRoute()
{
	// this is a host that lastCommonRouteFinder can use to traceroute
	return theApp.lastCommonRouteFinder->AddHostsToCheck(list);
}

void CClientList::RemoveClient(CUpDownClient *toremove, LPCTSTR pszReason)
{
	if (!GuardClientListMutation(_T("CClientList::RemoveClient")))
		return;

	if (toremove->m_bIsArchived)
		return;

	POSITION pos = list.Find(toremove);
	if (pos) {
		CancelAutoQuerySharedFilesJob();
		theApp.emuledlg->transferwnd->GetClientList()->SaveArchive(toremove);
		theApp.uploadqueue->RemoveFromUploadQueue(toremove, CString(_T("CClientList::RemoveClient: ")) + pszReason);
		theApp.uploadqueue->RemoveFromWaitingQueue(toremove);
		theApp.downloadqueue->RemoveSource(toremove);
		theApp.emuledlg->transferwnd->GetClientList()->RemoveClient(toremove);
		list.RemoveAt(pos);
		{
			CSingleLock runtimeMapLock(&m_csTrackedClientRuntimeMaps, TRUE);
			m_ActiveClientsByRuntimeID.erase(toremove->GetRuntimeID());
		}
	}
	RemoveFromKadList(toremove);
	RemoveServedBuddy(toremove);
	RemoveConnectingClient(toremove);

	// eServer Buddy cleanup: clear serving buddy pointer if it was this client
	if (m_pServingEServerBuddy == toremove)
		SetServingEServerBuddy(NULL, Disconnected);
	// Also remove from served eServer buddies list if present
	RemoveServedEServerBuddy(toremove);
}

void CClientList::DetachClientFromRuntimeMaps(const CUpDownClient* pClient)
{
	if (pClient == NULL)
		return;

	CSingleLock runtimeMapLock(&m_csTrackedClientRuntimeMaps, TRUE);
	const DWORD uRuntimeID = pClient->GetRuntimeID();
	const auto itActive = m_ActiveClientsByRuntimeID.find(uRuntimeID);
	if (itActive != m_ActiveClientsByRuntimeID.end() && itActive->second == pClient)
		m_ActiveClientsByRuntimeID.erase(itActive);

	const auto itArchived = m_ArchivedClientsByRuntimeID.find(uRuntimeID);
	if (itArchived != m_ArchivedClientsByRuntimeID.end() && itArchived->second == pClient)
		m_ArchivedClientsByRuntimeID.erase(itArchived);
}

void CClientList::FinalizeDeletePendingClientByRuntimeID(DWORD uRuntimeID)
{
	if (uRuntimeID == 0)
		return;

	CUpDownClient* pClient = NULL;
	{
		CSingleLock runtimeMapLock(&m_csTrackedClientRuntimeMaps, TRUE);
		const auto itActive = m_ActiveClientsByRuntimeID.find(uRuntimeID);
		if (itActive != m_ActiveClientsByRuntimeID.end() && itActive->second != NULL && itActive->second->TryAcquirePendingDeleteFinalizeHold())
			pClient = itActive->second;
		else {
			const auto itArchived = m_ArchivedClientsByRuntimeID.find(uRuntimeID);
			if (itArchived != m_ArchivedClientsByRuntimeID.end() && itArchived->second != NULL && itArchived->second->TryAcquirePendingDeleteFinalizeHold())
				pClient = itArchived->second;
		}
	}

	if (pClient != NULL)
		pClient->ReleaseRuntimeReference();
}

void CClientList::ServicePendingDeleteClients()
{
	std::vector<DWORD> aPendingDeleteRuntimeIDs;
	{
		CSingleLock runtimeMapLock(&m_csTrackedClientRuntimeMaps, TRUE);
		aPendingDeleteRuntimeIDs.reserve(m_ActiveClientsByRuntimeID.size() + m_ArchivedClientsByRuntimeID.size());

		for (auto it = m_ActiveClientsByRuntimeID.begin(); it != m_ActiveClientsByRuntimeID.end(); ++it) {
			if (IsPendingDeleteReadyForFinalize(it->second))
				aPendingDeleteRuntimeIDs.push_back(it->first);
		}

		for (auto it = m_ArchivedClientsByRuntimeID.begin(); it != m_ArchivedClientsByRuntimeID.end(); ++it) {
			if (IsPendingDeleteReadyForFinalize(it->second))
				aPendingDeleteRuntimeIDs.push_back(it->first);
		}
	}

	for (size_t i = 0; i < aPendingDeleteRuntimeIDs.size(); ++i)
		FinalizeDeletePendingClientByRuntimeID(aPendingDeleteRuntimeIDs[i]);
}

void CClientList::DeleteAll()
{
	CancelAutoQuerySharedFilesJob();
	theApp.uploadqueue->DeleteAll();
	theApp.downloadqueue->DeleteAll();
	while (!list.IsEmpty()) {
		CUpDownClient* pClient = list.RemoveHead();
		theApp.emuledlg->transferwnd->GetClientList()->SaveArchive(pClient);
		CUpDownClient::SafeDelete(pClient); // recursive: this will call RemoveClient
	}
	liMODsTypes.RemoveAll();
	if (thePrefs.GetClientHistory() && theApp.ClientHistoryReady()) {
		CancelClientHistorySaveJob();
		SaveListSynchronously();
	}

	std::vector<CUpDownClient*> aArchivedClients;
	aArchivedClients.reserve(static_cast<size_t>(m_ArchivedClientsMap.GetCount()));
	for (POSITION pos = m_ArchivedClientsMap.GetStartPosition(); pos != NULL;) {
		CString cur_hash;
		CUpDownClient* cur_client = NULL;
		m_ArchivedClientsMap.GetNextAssoc(pos, cur_hash, cur_client);
		if (cur_client != NULL)
			aArchivedClients.push_back(cur_client);
	}
	m_ArchivedClientsMap.RemoveAll();
	for (size_t i = 0; i < aArchivedClients.size(); ++i)
		CUpDownClient::SafeDelete(aArchivedClients[i]);
}

void CClientList::HandleNatTraversalProtocolModeChanged()
{
	std::vector<CUpDownClient*> aClients;
	aClients.reserve(static_cast<size_t>(list.GetCount()));
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;)
		aClients.push_back(list.GetNext(pos));

	for (size_t i = 0; i < aClients.size(); ++i) {
		CUpDownClient* pClient = aClients[i];
		if (pClient == NULL || !IsValidClient(pClient))
			continue;

		pClient->ClearNatRendezvousTransportPin();
		pClient->InvalidateDirectNatTraversalCaps();
		CAddress peerIP = pClient->GetConnectIP();
		uint16 peerPort = pClient->GetKadPort() != 0 ? pClient->GetKadPort() : pClient->GetUDPPort();
		if (theApp.clientudp != NULL && !peerIP.IsNull() && peerPort != 0)
			theApp.clientudp->RequestNatTraversalCaps(pClient, peerIP, peerPort);

		if (pClient->socket != NULL && pClient->socket->HaveNatTraversalLayer() && !pClient->IsCurrentNatTraversalTransportCompatible()) {
			const bool bActiveNatTraversalSession = pClient->socket->IsConnected()
				&& (pClient->GetDownloadState() == DS_DOWNLOADING || pClient->GetUploadState() == US_UPLOADING);
			if (bActiveNatTraversalSession) {
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] Protocol preference changed while transport is connected; preserving current session for %s"),
						(LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
				}
				continue;
			}

			pClient->ResetNatTraversalSocketForTransportChange(_T("NAT-T transport protocol changed"), true);
			if (IsValidClient(pClient) && pClient->GetRequestFile() != NULL)
				pClient->TryToConnect(true, false, NULL, true);
		}
	}
}

bool CClientList::AttachToAlreadyKnown(CUpDownClient **client, CClientReqSocket *sender)
{
	CUpDownClient *tocheck = *client;
	CUpDownClient *found_client = NULL;
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *pclient = list.GetNext(pos);
		if (found_client == NULL && tocheck->Compare(pclient, false)) //matching user hash
			found_client = pclient;

		if (tocheck->Compare(pclient, true)) { //matching IP
			found_client = pclient;
			break;
		}
	}
	if (found_client == NULL)
		return false;
	if (found_client == tocheck) {
		//we found the same client instance (client may have sent more than one OP_HELLO). Do not delete this client!
		return true;
	}

	// Merge essential endpoint and NAT-T related attributes from the transient client to the already known client to avoid losing Kad/NAT traversal context.
	// This is important for LowID-to-LowID NAT traversal where hello/options may not be exchanged yet but we still need Kad endpoint hints.
	if (found_client->GetKadPort() == 0 && tocheck->GetKadPort() != 0)
		found_client->SetKadPort(tocheck->GetKadPort());

	if (found_client->GetUDPPort() == 0 && tocheck->GetUDPPort() != 0)
		found_client->SetUDPPort(tocheck->GetUDPPort());

	if (found_client->GetConnectIP().IsNull() && !tocheck->GetConnectIP().IsNull())
		found_client->SetConnectIP(tocheck->GetConnectIP());

	if (found_client->GetIP().IsNull() && !tocheck->GetIP().IsNull()) {
		CAddress a = tocheck->GetIP();
		found_client->SetIP(a);
	}

	if (!found_client->GetNatTraversalSupport() && tocheck->GetNatTraversalSupport())
		found_client->SetNatTraversalSupport(true);
	if (!found_client->GetNatTraversalQuicSupport() && tocheck->GetNatTraversalQuicSupport())
		found_client->SetNatTraversalQuicSupport(true);

	if (!found_client->SupportsDirectUDPCallback() && tocheck->SupportsDirectUDPCallback())
		found_client->SetDirectUDPCallbackSupport(true);

	if (tocheck->HasValidServingBuddyID()) {
		const bool bIncomingAuthoritative =
			(tocheck->GetSourceFrom() == SF_SERVER || tocheck->GetSourceFrom() == SF_KADEMLIA || tocheck->GetSourceFrom() == SF_SOURCE_EXCHANGE);
		const bool bFoundHasServingBuddyID = found_client->HasValidServingBuddyID();
		const bool bServingBuddyChanged = bFoundHasServingBuddyID && !md4equ(found_client->GetServingBuddyID(), tocheck->GetServingBuddyID());
		if (!bFoundHasServingBuddyID || (bIncomingAuthoritative && bServingBuddyChanged)) {
			found_client->SetServingBuddyID(tocheck->GetServingBuddyID());
			if (bServingBuddyChanged && thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] AttachToAlreadyKnown: refreshed ServingBuddyID for %s"), (LPCTSTR)EscPercent(found_client->DbgGetClientInfo()));
		}
		if (bIncomingAuthoritative || !bFoundHasServingBuddyID) {
			if (!tocheck->GetServingBuddyIP().IsNull())
				found_client->SetServingBuddyIP(tocheck->GetServingBuddyIP());
			if (tocheck->GetServingBuddyPort() != 0)
				found_client->SetServingBuddyPort(tocheck->GetServingBuddyPort());
		}
	}

	if (tocheck->HasValidEServerBuddyKadID() && !found_client->HasValidEServerBuddyKadID())
		found_client->SetEServerBuddyKadID(tocheck->GetEServerBuddyKadID());

	if (sender) {
		if (found_client->socket) {
			if (found_client->socket->IsConnected()
				&& (found_client->GetIP() != tocheck->GetIP() || found_client->GetUserPort() != tocheck->GetUserPort()))
			{
				if (found_client->Credits() && found_client->Credits()->GetCurrentIdentState(found_client->GetIP()) == IS_IDENTIFIED) {
					// if found_client is connected and has the IS_IDENTIFIED, it's safe to say that the other one is a bad guy
					if (thePrefs.GetLogBannedClients())
						AddProtectionLogLine(false, _T("Clients: %s (%s), Ban reason: Userhash invalid"), (LPCTSTR)EscPercent(tocheck->GetUserName()), (LPCTSTR)ipstr(tocheck->GetConnectIP()));
					tocheck->Ban();
				} else if (thePrefs.GetLogBannedClients()) {
					//CLIENTCOL Warning: Found matching client, to a currently connected client: %s (%s) and %s (%s)
					AddProtectionLogLine(false, (LPCTSTR)GetResString(_T("CLIENTCOL"))
						, (LPCTSTR)EscPercent(tocheck->GetUserName()), (LPCTSTR)ipstr(tocheck->GetConnectIP())
						, (LPCTSTR)EscPercent(found_client->GetUserName()), (LPCTSTR)ipstr(found_client->GetConnectIP()));
				}
				return false;
			}
			found_client->socket->client = NULL;
			found_client->socket->Safe_Delete();
		}
		found_client->socket = sender;
		tocheck->socket = NULL;
	}
	*client = NULL;
//***	found_client->SetSourceFrom(tocheck->GetSourceFrom());
	CUpDownClient::SafeDelete(tocheck);
	*client = found_client;
	return true;
}

//CUpDownClient* CClientList::FindClientByConnIP(uint32 clientip, UINT port) const
CUpDownClient *CClientList::FindClientByConnIP(const CAddress& clientip, UINT port) const
{
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *cur_client = list.GetNext(pos);
		if ((clientip.GetType() == CAddress::IPv6 ? clientip == cur_client->GetConnectIP() : clientip.ToUInt32(false) == cur_client->GetConnectIP().ToUInt32(false)) && cur_client->GetUserPort() == port)
			return cur_client;
	}
	return NULL;
}

//CUpDownClient* CClientList::FindClientByIP(uint32 clientip, UINT port) const
CUpDownClient* CClientList::FindClientByIP(const CAddress& clientip, UINT port) const
{
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *cur_client = list.GetNext(pos);
		if (CompareClientIP(cur_client, clientip) && cur_client->GetUserPort() == port)
			return cur_client;
	}
	return NULL;
}

//CUpDownClient* CClientList::FindClientByUserHash(const uchar* clienthash, uint32 dwIP, uint16 nTCPPort) const
CUpDownClient* CClientList::FindClientByUserHash(const uchar *clienthash, const CAddress& clientip, uint16 nTCPPort) const
{
	CUpDownClient *pFound = NULL;
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *cur_client = list.GetNext(pos);
		if (md4equ(cur_client->GetUserHash(), clienthash)) {
			if ((clientip.IsNull() || CompareClientIP(cur_client, clientip)) && (nTCPPort == 0 || nTCPPort == cur_client->GetUserPort())) //>>> WiZaRd::IPv6 [Xanatos]
				return cur_client;
			if (pFound == NULL)
				pFound = cur_client;
		}
	}
	return pFound;
}

CUpDownClient* CClientList::FindClientByUserHashAndKadEndpoint(const uchar* clienthash, const CAddress& clientip, uint16 nKadPort) const
{
	if (clienthash == NULL || isnulmd4(clienthash) || clientip.IsNull() || nKadPort == 0)
		return NULL;

	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient* cur_client = list.GetNext(pos);
		if (md4equ(cur_client->GetUserHash(), clienthash)
			&& CompareClientIP(cur_client, clientip)
			&& (cur_client->GetKadPort() == nKadPort || cur_client->GetUDPPort() == nKadPort))
			return cur_client;
	}
	return NULL;
}

//CUpDownClient* CClientList::FindClientByIP(uint32 clientip) const
CUpDownClient* CClientList::FindClientByIP(const CAddress& clientip) const
{
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *cur_client = list.GetNext(pos);
		if (CompareClientIP(cur_client, clientip))
			return cur_client;
	}
	return NULL;
}

CUpDownClient* CClientList::FindUniqueClientByIP(const CAddress& clientip) const
{
	CUpDownClient* found = NULL;
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *cur_client = list.GetNext(pos);
		if (!CompareClientIP(cur_client, clientip))
			continue;
		if (found != NULL)
			return NULL;
		found = cur_client;
	}
	return found;
}

CUpDownClient* CClientList::FindClientByIP_UDP(const CAddress& clientip, UINT nUDPport) const
{
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *cur_client = list.GetNext(pos);
		if (CompareClientIP(cur_client, clientip) && cur_client->GetUDPPort() == nUDPport)
			return cur_client;
	}
	return NULL;
}

CUpDownClient* CClientList::FindClientByUserID_KadPort(uint32 clientID, uint16 kadPort) const
{
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *cur_client = list.GetNext(pos);
		if (cur_client->GetUserIDHybrid() == clientID && cur_client->GetKadPort() == kadPort)
			return cur_client;
	}
	return NULL;
}

CUpDownClient* CClientList::FindClientByIP_KadPort(const CAddress& clientip, uint16 port) const
{
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *cur_client = list.GetNext(pos);
		if (CompareClientIP(cur_client, clientip) && cur_client->GetKadPort() == port)
			return cur_client;
	}
	return NULL;
}

bool CClientList::HasQuicNatTraversalLayerForEndpoint(const CUpDownClient* pExcept, const CAddress& clientip, uint16 nUDPport) const
{
	if (clientip.IsNull() || nUDPport == 0)
		return false;

	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient* cur_client = list.GetNext(pos);
		if (cur_client == NULL || cur_client == pExcept || cur_client->socket == NULL || !cur_client->socket->HaveQuicNatLayer())
			continue;
		if (!CompareClientIP(cur_client, clientip))
			continue;
		if (cur_client->GetKadPort() == nUDPport || cur_client->GetUDPPort() == nUDPport)
			return true;
	}
	return false;
}

CUpDownClient* CClientList::FindClientByServerID(uint32 uServerIP, uint32 uED2KUserID) const
{
	uint32 uHybridUserID = ntohl(uED2KUserID);
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *cur_client = list.GetNext(pos);
		if (cur_client->GetServerIP() == uServerIP && cur_client->GetUserIDHybrid() == uHybridUserID)
			return cur_client;
	}
	return NULL;
}


///////////////////////////////////////////////////////////////////////////////
// Banned clients

void CClientList::AddBannedClient(CString strKey, time_t tInserted)
{
	m_bannedList[strKey] = tInserted;
	if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL)
		theApp.emuledlg->transferwnd->InvalidateQueueCount(false);
}

bool CClientList::IsBannedClient(CString strKey) const
{
	time_t m_tBantime;
	return (strKey != _T("00000000000000000000000000000000") || strKey != _T("0.0.0.0") || strKey != _T("0:0:0:0:0:0:0:0")) &&
			m_bannedList.Lookup(strKey, m_tBantime) && (time(NULL) < m_tBantime + thePrefs.GetClientBanTime());
}

void CClientList::RemoveBannedClient(CString strKey)
{
	m_bannedList.RemoveKey(strKey);
	if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL)
		theApp.emuledlg->transferwnd->InvalidateQueueCount(false);
}

INT_PTR CClientList::GetBannedCount() const
{
	INT_PTR iBannedClientCount = 0;
	const time_t tCurrentTime = time(NULL);
	CMap<CString, LPCTSTR, BYTE, BYTE> representedBanKeys;

	auto IsCountableBanKey = [](const CString& strKey) -> bool {
		return !strKey.IsEmpty()
			&& strKey != _T("00000000000000000000000000000000")
			&& strKey != _T("0.0.0.0")
			&& strKey != _T("0:0:0:0:0:0:0:0");
	};

	auto RememberBanKey = [&representedBanKeys, &IsCountableBanKey](const CString& strKey) {
		if (IsCountableBanKey(strKey))
			representedBanKeys[strKey] = 1;
	};

	auto RememberClientBanKeys = [&RememberBanKey](const CUpDownClient* pClient) {
		if (pClient == NULL)
			return;

		if (!isnulmd4(pClient->GetUserHash()))
			RememberBanKey(md4str(pClient->GetUserHash()));
		if (!pClient->GetConnectIP().IsNull())
			RememberBanKey(pClient->GetConnectIP().ToStringC());
	};

	if (thePrefs.GetClientHistory() && theApp.ClientHistoryReady()) {
		for (POSITION pos = m_ArchivedClientsMap.GetStartPosition(); pos != NULL;) {
			CString strHash;
			CUpDownClient* pArchivedClient = NULL;
			m_ArchivedClientsMap.GetNextAssoc(pos, strHash, pArchivedClient);
			if (pArchivedClient != NULL && pArchivedClient->IsBanned()) {
				++iBannedClientCount;
				RememberClientBanKeys(pArchivedClient);
			}
		}
	}

	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient* pClient = list.GetNext(pos);
		if (pClient == NULL || !pClient->IsBanned())
			continue;

		bool bAlreadyCountedByArchivedClient = false;
		if (thePrefs.GetClientHistory() && theApp.ClientHistoryReady() && !IsCorruptOrBadUserHash(pClient->GetUserHash())) {
			CUpDownClient* pArchivedClient = NULL;
			if (m_ArchivedClientsMap.Lookup(md4str(pClient->GetUserHash()), pArchivedClient) && pArchivedClient != NULL && pArchivedClient->IsBanned())
				bAlreadyCountedByArchivedClient = true;
		}

		if (!bAlreadyCountedByArchivedClient)
			++iBannedClientCount;

		RememberClientBanKeys(pClient);
	}

	for (POSITION pos = m_bannedList.GetStartPosition(); pos != NULL;) {
		CString strKey;
		time_t tBanTime = 0;
		BYTE byRemembered = 0;
		m_bannedList.GetNextAssoc(pos, strKey, tBanTime);
		if (IsCountableBanKey(strKey) && tCurrentTime < tBanTime + thePrefs.GetClientBanTime() && !representedBanKeys.Lookup(strKey, byRemembered))
			++iBannedClientCount;
	}

	return iBannedClientCount;
}

void CClientList::RemoveAllBannedClients()
{
	m_bannedList.RemoveAll();
}

///////////////////////////////////////////////////////////////////////////////
// Tracked clients

//void CClientList::AddTrackClient(CUpDownClient* toadd)
void CClientList::AddTrackClient(CUpDownClient *toadd, time_t tInserted)
{
	CDeletedClient *pResult;
	if (m_trackedClientsMap.Lookup(toadd->GetIP().ToStringC(), pResult)) {
		pResult->m_tInserted = tInserted;
		for (INT_PTR i = pResult->m_ItemsList.GetCount(); --i >= 0;)
			if (pResult->m_ItemsList[i].nPort == toadd->GetUserPort()) {
				// already tracked, update
				pResult->m_ItemsList[i].pHash = toadd->Credits();
				return;
			}

		pResult->m_ItemsList.Add(PORTANDHASH{ toadd->GetUserPort(), toadd->Credits() });
	} else
		m_trackedClientsMap[toadd->GetIP().ToStringC()] = new CDeletedClient(toadd);
}

// true = everything OK, hash didn't change
// false = hash changed
bool CClientList::ComparePriorUserhash(CAddress IP, uint16 nPort, const void *pNewHash)
{
	CDeletedClient *pResult;
	if (m_trackedClientsMap.Lookup(IP.ToStringC(), pResult)) {
		for (INT_PTR i = pResult->m_ItemsList.GetCount(); --i >= 0;) {
			if (pResult->m_ItemsList[i].nPort == nPort) {
				if (pResult->m_ItemsList[i].pHash != pNewHash)
					return false;
				break;
			}
		}
	}
	return true;
}

/*
INT_PTR CClientList::GetClientsFromIP(uint32 dwIP) const
{
	const CDeletedClientMap::CPair *pair = m_trackedClientsMap.PLookup(dwIP);
	return pair ? pair->value->m_ItemsList.GetCount() : 0;
}
*/
INT_PTR CClientList::GetClientsFromIP(CAddress IP) const
{
	const CDeletedClientMap::CPair *pair = m_trackedClientsMap.PLookup(IP.ToStringC());
	return pair ? pair->value->m_ItemsList.GetCount() : 0;
}

void CClientList::TrackBadRequest(const CUpDownClient *upcClient, int nIncreaseCounter)
{
	if (upcClient->GetIP().IsNull()) {
		ASSERT(0);
		return;
	}
	CDeletedClient *pResult;
	if (m_trackedClientsMap.Lookup(upcClient->GetIP().ToStringC(), pResult)) {
		pResult->m_tInserted = time(NULL);
		pResult->m_cBadRequest += nIncreaseCounter;
	} else {
		CDeletedClient *ccToAdd = new CDeletedClient(upcClient);
		ccToAdd->m_cBadRequest = nIncreaseCounter;
		m_trackedClientsMap[upcClient->GetIP().ToStringC()] = ccToAdd;
	}
}

uint32 CClientList::GetBadRequests(const CUpDownClient *upcClient) const
{
	if (upcClient->GetIP().IsNull()) {
		ASSERT(false);
		return 0;
	}

	const CDeletedClientMap::CPair* pair = m_trackedClientsMap.PLookup(upcClient->GetIP().ToStringC());
	return pair ? pair->value->m_cBadRequest : 0;
}

void CClientList::RemoveAllTrackedClients()
{
	for (POSITION pos = m_trackedClientsMap.GetStartPosition(); pos != NULL;) {
		//uint32 nKey;
		CString nKey;
		CDeletedClient *pResult;
		m_trackedClientsMap.GetNextAssoc(pos, nKey, pResult);
		m_trackedClientsMap.RemoveKey(nKey);
		delete pResult;
	}
}

void CClientList::Process()
{
	ServicePendingDeleteClients();

	///////////////////////////////////////////////////////////////////////////
	// Cleanup banned client list
	//
	const time_t m_tCurTime = ::time(NULL);
	if (m_tCurTime >= m_tLastBanCleanUp + thePrefs.GetPunishmentCancelationScanPeriod()) {
		m_tLastBanCleanUp = m_tCurTime;

		for (POSITION pos = m_bannedList.GetStartPosition(); pos != NULL;) {
			//uint32 nKey;
			CString nKey;
			time_t m_tBantime;
			m_bannedList.GetNextAssoc(pos, nKey, m_tBantime);
			if (m_tCurTime >= m_tBantime + thePrefs.GetClientBanTime())
				RemoveBannedClient(nKey);
		}

		for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
			CUpDownClient* cur_client = list.GetNext(pos);
			if (cur_client->m_uBadClientCategory && cur_client->m_uPunishment >= P_UPLOADBAN && (m_tCurTime >= cur_client->m_tPunishmentStartTime + thePrefs.GetClientScoreReducingTime()))
				theApp.shield->SetPunishment(cur_client, NULL, PR_NOTBADCLIENT);
		}
	}

	///////////////////////////////////////////////////////////////////////////
	// Cleanup tracked client list
	//
	if (m_tCurTime >= m_tLastTrackedCleanUp + TRACKED_CLEANUP_TIME) {
		m_tLastTrackedCleanUp = m_tCurTime;
		if (thePrefs.GetLogBannedClients())
			AddDebugLogLine(false, _T("Cleaning up TrackedClientList, %u clients on List..."), (unsigned)m_trackedClientsMap.GetCount());
		for (POSITION pos = m_trackedClientsMap.GetStartPosition(); pos != NULL;) {
			//uint32 nKey;
			CString nKey;
			CDeletedClient *pResult;
			m_trackedClientsMap.GetNextAssoc(pos, nKey, pResult);
			if (m_tCurTime >= pResult->m_tInserted + KEEPTRACK_TIME) {
				m_trackedClientsMap.RemoveKey(nKey);
				delete pResult;
			}
		}
		if (thePrefs.GetLogBannedClients())
			AddDebugLogLine(false, _T("...done, %u clients left on list"), (unsigned)m_trackedClientsMap.GetCount());
	}

	///////////////////////////////////////////////////////////////////////////
	// Process Kad client list
	//
	//We need to try to connect to the clients in m_KadList
	//If connected, remove them from the list and send a message back to Kad so we can send an ACK.
	//If we don't connect, we need to remove the client.
	//The sockets timeout should delete this object.

	// servingBuddy is just a flag that is used to make sure we are still connected or connecting to a serving buddy.
	servingBuddyState servingBuddy = Disconnected;
	const DWORD dwKadProcessTick = ::GetTickCount();

	for (POSITION pos = m_KadList.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *cur_client = m_KadList.GetNext(pos);
		if (!Kademlia::CKademlia::IsRunning()) {
			//Clear out this list if we stop running Kad.
			//Setting the Kad state to KS_NONE causes it to be removed in the switch below.
			cur_client->SetKadState(KS_NONE);
		}
		switch (cur_client->GetKadState()) {
		case KS_QUEUED_FWCHECK:
		case KS_QUEUED_FWCHECK_UDP:
			//Another client asked us to try to connect to them to check their firewalled status.
			cur_client->TryToConnect(true, true);
			break;
		case KS_CONNECTING_FWCHECK:
			//Ignore this state as we are just waiting for results.
			break;
		case KS_FWCHECK_UDP:
		case KS_CONNECTING_FWCHECK_UDP:
			// we want a UDP firewall check from this client and are just waiting to get connected to send the request
			break;
		case KS_CONNECTED_FWCHECK:
			//We successfully connected to the client; now send an ack to let them know.
			if (cur_client->GetKadVersion() >= KADEMLIA_VERSION7_49a) {
				// the result is now sent per TCP instead of UDP, because this will fail if our intern UDP port is unreachable.
				// But we want the TCP testresult regardless if UDP is firewalled, the new UDP state and test takes care of the rest
				ASSERT(cur_client->socket != NULL && cur_client->socket->IsConnected());
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugSend("OP_KAD_FWTCPCHECK_ACK", cur_client);
				Packet *pPacket = new Packet(OP_KAD_FWTCPCHECK_ACK, 0, OP_EMULEPROT);
				if (!cur_client->SafeConnectAndSendPacket(pPacket))
					break;
			} else {
				if (thePrefs.GetDebugClientKadUDPLevel() > 0)
					DebugSend("KADEMLIA_FIREWALLED_ACK_RES", cur_client->GetIP(), cur_client->GetKadPort());
				Kademlia::CKademlia::GetUDPListener()->SendNullPacket(KADEMLIA_FIREWALLED_ACK_RES, cur_client->GetIP().ToUInt32(true), cur_client->GetKadPort(), Kademlia::CKadUDPKey(), NULL);
			}
			//We are done with this client. Set Kad status to KS_NONE and it will be removed in the next cycle.
			cur_client->SetKadState(KS_NONE);
			break;
		case KS_INCOMING_SERVED_BUDDY:
			//A firewalled client wants us to be his serving buddy.
			// Admission already reserved the slot. Release it if the expected inbound TCP handshake never arrives.
			if ((cur_client->socket == NULL || !cur_client->socket->IsConnected())
				&& (DWORD)(dwKadProcessTick - cur_client->GetIncomingServedBuddyRequestTick()) >= KAD_SERVED_BUDDY_PENDING_TIMEOUT) {
				if (thePrefs.GetLogNatTraversalEvents())
					AddDebugLogLine(DLP_LOW, false, _T("[Buddy]: Expiring incoming served buddy without TCP connection: %s"), (LPCTSTR)EscPercent(cur_client->DbgGetClientInfo()));
				RemoveServedBuddy(cur_client);
				cur_client->ClearIncomingServedBuddyWait();
				cur_client->SetKadState(KS_NONE);
			}
			break;
		case KS_QUEUED_SERVING_BUDDY:
			//We are firewalled and want to request this client to be a serving buddy.
			//But first we check to make sure we are not already trying another client.
			//If we are not already trying, we try to connect to this client.
			//If we are already connected to a serving buddy, we set this client to KS_NONE and it's removed in the next cycle.
			//If we are trying to connect to a serving buddy, we just ignore as the one we are trying may fail and we can then try this one.
			if (m_nServingBuddyStatus == Disconnected) {
				servingBuddy = Connecting;
				m_nServingBuddyStatus = Connecting;
				cur_client->SetKadState(KS_CONNECTING_SERVING_BUDDY);
				cur_client->TryToConnect(true, true);
				RefreshConnectionStateIndicators();
			} else if (m_nServingBuddyStatus == Connected)
				cur_client->SetKadState(KS_NONE);
			break;
		case KS_CONNECTING_SERVING_BUDDY:
			//We are trying to connect to this client.
			//Although it should NOT happen, we make sure we are not already connected to a serving buddy.
			//If we are we set to KS_NONE and it's removed next cycle.
			//But if we are not already connected, make sure we set the flag to connecting so we know
			//things are working correctly.
			if (m_nServingBuddyStatus == Connected)
				cur_client->SetKadState(KS_NONE);
			else {
				ASSERT(m_nServingBuddyStatus == Connecting);
				servingBuddy = Connecting;
			}
			break;
		case KS_CONNECTED_BUDDY:
			// A potential connected serving buddy client wanting to me in the Kad network.
			// KS_CONNECTED_BUDDY can also belong to served buddies; keep roles separated.
			if (!IsServedBuddy(cur_client)) {
				servingBuddy = Connected;
				//If m_nServingBuddyStatus is not connected already, we set this client as our serving buddy!
				if (m_nServingBuddyStatus != Connected) {
					m_pServingBuddy = cur_client;
					m_nServingBuddyStatus = Connected;
					AddLogLine(false, GetResString(_T("KAD_SERVING_BUDDY_CONNECTED")), (LPCTSTR)EscPercent(cur_client->DbgGetClientInfo()));
					RefreshConnectionStateIndicators();
					m_dwLastKadBuddyEServerBridgeRequest = 0;
					TryRequestEServerBuddyFromKadBuddy(true);
					if (theApp.downloadqueue != NULL)
						theApp.downloadqueue->TriggerPendingNatTraversalDownloads(_T("Kad serving buddy connected."));
				}
				if (m_pServingBuddy == cur_client && theApp.IsFirewalled() && cur_client->SendBuddyPingPong()) {
					if (thePrefs.GetDebugClientTCPLevel() > 0)
						DebugSend("OP_BuddyPing", cur_client);
					Packet *buddyPing = new Packet(OP_BUDDYPING, 0, OP_EMULEPROT);
					theStats.AddUpDataOverheadOther(buddyPing->size);
					VERIFY(cur_client->SendPacket(buddyPing, true));
					cur_client->SetLastBuddyPingPongTime();
				}
			}
			break;
		default:
			RemoveFromKadList(cur_client);
		}
	}

	//We either never had a serving buddy, or lost our serving buddy.
	if (servingBuddy == Disconnected) {
		if (m_nServingBuddyStatus != Disconnected || m_pServingBuddy) {
			if (m_pServingBuddy != NULL)
				AddLogLine(true, GetResString(_T("KAD_SERVING_BUDDY_DISCONNECTED")), (LPCTSTR)EscPercent(m_pServingBuddy->DbgGetClientInfo()));
			if (Kademlia::CKademlia::IsRunning() && theApp.IsFirewalled() && Kademlia::CUDPFirewallTester::IsFirewalledUDP(true)) {
				//We are a lowID client and we just lost our serving buddy.
				//Go ahead and instantly try to find a new serving buddy.
				Kademlia::CKademlia::GetPrefs()->SetFindServingBuddy();
			}
			m_pServingBuddy = NULL;
			m_nServingBuddyStatus = Disconnected;
			RefreshConnectionStateIndicators();
		}
	}

	if (Kademlia::CKademlia::IsConnected()) {
		const DWORD dwNow = ::GetTickCount();
		if (Kademlia::CKademlia::IsFirewalled()
			&& m_nServingBuddyStatus == Disconnected
			&& !thePrefs.IsCryptLayerRequired()
			&& !IsKadBuddySearchActive()
			&& (m_dwLastKadBuddyDemandCheck == 0 || (DWORD)(dwNow - m_dwLastKadBuddyDemandCheck) >= SEC2MS(5)))
		{
			m_dwLastKadBuddyDemandCheck = dwNow;
			if ((m_dwLastKadBuddyDemandSearch == 0 || (DWORD)(dwNow - m_dwLastKadBuddyDemandSearch) >= MIN2MS(5))
				&& theApp.downloadqueue != NULL
				&& theApp.downloadqueue->HasPendingNatTraversalBuddyDemand())
			{
				m_dwLastKadBuddyDemandSearch = dwNow;
				Kademlia::CKademlia::GetPrefs()->SetFindServingBuddy();
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[Buddy] Scheduled bounded Kad serving buddy search for pending NAT-T download"));
			}
		}

		// We can leverage a Kad serving buddy in two cases:
		// 1) Both TCP and UDP are firewalled: Direct UDP callback is unavailable, so a serving buddy is required as the relay for Kad callbacks.
		//    NAT-T/uTP may still be attempted if enabled and outbound UDP is possible, but the serving buddy itself does not need any NAT-T awareness.
		// 2) TCP is firewalled but UDP is open: Even though direct UDP callback works without a serving buddy, we still start a serving buddy lookup to enable
		//    rendezvous/hole-punch so the data path can upgrade to uTP if both endpoints support it. The serving buddy only relays control (callback/rendezvous)
		//    and does not need to support uTP or carry the data stream.
		if (Kademlia::CKademlia::IsFirewalled()) { // Also allow when UDP is open to enable NAT-T rendezvous
			//TODO 0.49b: Kad buddies won't work with RequireCrypt, so it is disabled for now but should (and will)
			//be fixed in later version
			// Update: Buddy connections itself support obfuscation properly since 0.49a (this makes it work fine if our serving buddy uses require crypt),
			// however callback requests don't support it yet so we wouldn't be able to answer callback requests with RequireCrypt, protocol change intended for the next version
			if (m_nServingBuddyStatus == Disconnected && Kademlia::CKademlia::GetPrefs()->GetFindServingBuddy() && !thePrefs.IsCryptLayerRequired()) {
				//We are a firewalled client with no serving buddy. We have also waited a set time
				//to try to avoid a false firewalled status. So lets look for a serving buddy.
				if (!Kademlia::CSearchManager::PrepareLookup(Kademlia::CSearch::FINDSERVINGBUDDY, true, Kademlia::CUInt128(true).Xor(Kademlia::CKademlia::GetPrefs()->GetKadID()))) {
					//This search ID was already going. Most likely reason is that
					//we found and lost our serving buddy very quickly and the last search hadn't
					//had time to be removed yet. Go ahead and set this to happen again
					//next time around.
					Kademlia::CKademlia::GetPrefs()->SetFindServingBuddy();
				}
			}
		} else {
			if (m_pServingBuddy) {
				//Lets make sure that if we have a serving buddy, they are firewalled!
				//If they are also not firewalled, then someone must have fixed their firewall or stopped saturating their line.
				//We just set the state of this serving buddy to KS_NONE and things will be cleared up with the next cycle.
				if (!m_pServingBuddy->HasLowID())
					m_pServingBuddy->SetKadState(KS_NONE);
			}
		}
	} else if (m_pServingBuddy) {
		//We are not connected any more. Just set this serving buddy to KS_NONE and things will be cleared out on next cycle.
		m_pServingBuddy->SetKadState(KS_NONE);
	}

	const bool bKadStopped = !Kademlia::CKademlia::IsRunning();
	const bool bVerifiedUDPFirewalled = Kademlia::CUDPFirewallTester::IsVerified() && Kademlia::CUDPFirewallTester::IsFirewalledUDP(true);
	if ((bKadStopped || bVerifiedUDPFirewalled) && m_ServedBuddyMap.GetCount())
		ClearAllServedBuddies(); // Keep served buddies during transient UDP rechecks; drop them only when Kad stops or UDP firewalled state is verified.

	///////////////////////////////////////////////////////////////////////////
	// Cleanup client list
	//
	CleanUpClientList();

	///////////////////////////////////////////////////////////////////////////
	// Process Direct Callbacks for Timeouts
	//
	ProcessConnectingClientsList();

	if (m_ClientHistorySaveJob.bActive)
		ProcessClientHistorySaveJob();

	if (m_AutoQuerySharedFilesJob.bActive)
		ProcessAutoQuerySharedFilesJob();

	if (thePrefs.GetClientHistory() && theApp.ClientHistoryReady() && time(NULL) >= m_tLastSaved + MIN2S(13)) { // Saves Client History on every 13 minutes.
		if (!m_ClientHistorySaveJob.bActive && (theApp.sharedfiles == NULL || !theApp.sharedfiles->IsReloading()))
			SaveList();
	}

	// eServer Buddy relay maintenance
	CleanupExpiredEServerRelays();
	ProcessEServerBuddyPings();

	// Cleanup pending relay contexts
	CleanupPendingRelayContexts();
}

#ifdef _DEBUG
void CClientList::Debug_SocketDeleted(CClientReqSocket *deleted) const
{
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *cur_client = list.GetNext(pos);
		if (!cur_client)
			AfxDebugBreak();
		if (thePrefs.m_iDbgHeap >= 2)
			ASSERT_VALID(cur_client);
		if (cur_client->socket == deleted)
			AfxDebugBreak();
	}
}
#endif

bool CClientList::IsValidClient(CUpDownClient *tocheck) const
{
	if (tocheck == NULL)
		return false;
	return list.Find(tocheck) != NULL;
}

bool CClientList::IsClientActive(const CUpDownClient *tocheck) const
{
	if (tocheck == NULL)
		return false;
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		if (list.GetNext(pos) == tocheck)
			return true;
	}
	return false;
}

CUpDownClient* CClientList::AcquireTrackedClientByPointer(const CUpDownClient* pClient) const
{
	if (pClient == NULL)
		return NULL;

	CUpDownClient* pDeferredFinalizeClient = NULL;
	{
		CSingleLock runtimeMapLock(&m_csTrackedClientRuntimeMaps, TRUE);
		for (auto it = m_ActiveClientsByRuntimeID.begin(); it != m_ActiveClientsByRuntimeID.end(); ++it) {
			if (it->second != pClient || it->second == NULL)
				continue;
			bool bNeedsFinalize = false;
			if (it->second->TryAcquireRuntimeReference(&bNeedsFinalize))
				return it->second;
			if (pDeferredFinalizeClient == NULL && bNeedsFinalize && it->second->TryAcquirePendingDeleteFinalizeHold())
				pDeferredFinalizeClient = it->second;
			break;
		}

		if (pDeferredFinalizeClient == NULL) {
			for (auto it = m_ArchivedClientsByRuntimeID.begin(); it != m_ArchivedClientsByRuntimeID.end(); ++it) {
				if (it->second != pClient || it->second == NULL)
					continue;
				bool bNeedsFinalize = false;
				if (it->second->TryAcquireRuntimeReference(&bNeedsFinalize))
					return it->second;
				if (bNeedsFinalize && it->second->TryAcquirePendingDeleteFinalizeHold())
					pDeferredFinalizeClient = it->second;
				break;
			}
		}
	}

	if (pDeferredFinalizeClient != NULL)
		pDeferredFinalizeClient->ReleaseRuntimeReference();
	return NULL;
}

CUpDownClient* CClientList::AcquireTrackedClientByUserHash(const uchar* clienthash, bool bPreferArchived) const
{
	if (clienthash == NULL || isnulmd4(clienthash) || IsCorruptOrBadUserHash(clienthash))
		return NULL;

	const CString strHash = md4str(clienthash);
	CUpDownClient* pDeferredFinalizeClient = NULL;
	{
		CSingleLock runtimeMapLock(&m_csTrackedClientRuntimeMaps, TRUE);
		auto TryAcquireClient = [&pDeferredFinalizeClient](CUpDownClient* pCandidate) -> CUpDownClient*
		{
			if (pCandidate == NULL)
				return NULL;

			bool bNeedsFinalize = false;
			if (pCandidate->TryAcquireRuntimeReference(&bNeedsFinalize))
				return pCandidate;
			if (pDeferredFinalizeClient == NULL && bNeedsFinalize && pCandidate->TryAcquirePendingDeleteFinalizeHold())
				pDeferredFinalizeClient = pCandidate;
			return NULL;
		};

		if (bPreferArchived) {
			CUpDownClient* pArchivedClient = NULL;
			if (m_ArchivedClientsMap.Lookup(strHash, pArchivedClient)) {
				if (CUpDownClient* pTrackedClient = TryAcquireClient(pArchivedClient))
					return pTrackedClient;
			}
		}

		for (auto it = m_ActiveClientsByRuntimeID.begin(); it != m_ActiveClientsByRuntimeID.end(); ++it) {
			CUpDownClient* pCandidate = it->second;
			if (pCandidate == NULL || !md4equ(pCandidate->GetUserHash(), clienthash))
				continue;

			if (CUpDownClient* pTrackedClient = TryAcquireClient(pCandidate))
				return pTrackedClient;
		}

		if (!bPreferArchived) {
			CUpDownClient* pArchivedClient = NULL;
			if (m_ArchivedClientsMap.Lookup(strHash, pArchivedClient)) {
				if (CUpDownClient* pTrackedClient = TryAcquireClient(pArchivedClient))
					return pTrackedClient;
			}
		}
	}

	if (pDeferredFinalizeClient != NULL)
		pDeferredFinalizeClient->ReleaseRuntimeReference();
	return NULL;
}

CUpDownClient* CClientList::AcquireTrackedClientByRuntimeID(DWORD uRuntimeID) const
{
	CUpDownClient* pClient = NULL;
	CUpDownClient* pDeferredFinalizeClient = NULL;
	{
		CSingleLock runtimeMapLock(&m_csTrackedClientRuntimeMaps, TRUE);
		const auto itActive = m_ActiveClientsByRuntimeID.find(uRuntimeID);
		if (itActive != m_ActiveClientsByRuntimeID.end())
			pClient = itActive->second;
		else {
			const auto itArchived = m_ArchivedClientsByRuntimeID.find(uRuntimeID);
			if (itArchived != m_ArchivedClientsByRuntimeID.end())
				pClient = itArchived->second;
		}

		if (pClient != NULL) {
			bool bNeedsFinalize = false;
			if (pClient->TryAcquireRuntimeReference(&bNeedsFinalize))
				return pClient;
			if (bNeedsFinalize && pClient->TryAcquirePendingDeleteFinalizeHold())
				pDeferredFinalizeClient = pClient;
		}
	}

	if (pDeferredFinalizeClient != NULL)
		pDeferredFinalizeClient->ReleaseRuntimeReference();
	return NULL;
}

CUpDownClient* CClientList::AcquireTrackedClientByRuntimeIDAndGeneration(DWORD uRuntimeID, LONG lRuntimeGeneration) const
{
	CUpDownClient* pClient = AcquireTrackedClientByRuntimeID(uRuntimeID);
	if (pClient == NULL)
		return NULL;

	if (pClient->IsRuntimeGenerationCurrent(lRuntimeGeneration))
		return pClient;

	pClient->ReleaseRuntimeReference();
	return NULL;
}

CUpDownClient* CClientList::AcquireArchivedClientByUserHash(const uchar* clienthash) const
{
	if (clienthash == NULL || isnulmd4(clienthash) || IsCorruptOrBadUserHash(clienthash))
		return NULL;

	CUpDownClient* pArchivedClient = NULL;
	CUpDownClient* pDeferredFinalizeClient = NULL;
	{
		CSingleLock runtimeMapLock(&m_csTrackedClientRuntimeMaps, TRUE);
		if (m_ArchivedClientsMap.Lookup(md4str(clienthash), pArchivedClient) && pArchivedClient != NULL) {
			bool bNeedsFinalize = false;
			if (pArchivedClient->TryAcquireRuntimeReference(&bNeedsFinalize))
				return pArchivedClient;
			if (bNeedsFinalize && pArchivedClient->TryAcquirePendingDeleteFinalizeHold())
				pDeferredFinalizeClient = pArchivedClient;
		}
	}

	if (pDeferredFinalizeClient != NULL)
		pDeferredFinalizeClient->ReleaseRuntimeReference();
	return NULL;
}

CUpDownClient* CClientList::FindArchivedClientByUserHash(const uchar* clienthash) const
{
	if (clienthash == NULL || isnulmd4(clienthash) || IsCorruptOrBadUserHash(clienthash))
		return NULL;

	CSingleLock runtimeMapLock(&m_csTrackedClientRuntimeMaps, TRUE);
	CUpDownClient* pArchivedClient = NULL;
	return m_ArchivedClientsMap.Lookup(md4str(clienthash), pArchivedClient) ? pArchivedClient : NULL;
}

void CClientList::RebuildArchivedRuntimeMap() const
{
	CSingleLock runtimeMapLock(&m_csTrackedClientRuntimeMaps, TRUE);
	std::vector<CUpDownClient*> aPendingDeleteClients;
	aPendingDeleteClients.reserve(m_ArchivedClientsByRuntimeID.size());
	for (auto it = m_ArchivedClientsByRuntimeID.begin(); it != m_ArchivedClientsByRuntimeID.end(); ++it) {
		if (IsPendingDeleteRuntimeMapEntry(it->second))
			aPendingDeleteClients.push_back(it->second);
	}

	m_ArchivedClientsByRuntimeID.clear();
	m_ArchivedClientsByRuntimeID.reserve(static_cast<size_t>(m_ArchivedClientsMap.GetCount()) + aPendingDeleteClients.size() + 16);
	for (size_t i = 0; i < aPendingDeleteClients.size(); ++i)
		m_ArchivedClientsByRuntimeID[aPendingDeleteClients[i]->GetRuntimeID()] = aPendingDeleteClients[i];
	for (POSITION pos = m_ArchivedClientsMap.GetStartPosition(); pos != NULL;) {
		CString strHash;
		CUpDownClient* pArchivedClient = NULL;
		m_ArchivedClientsMap.GetNextAssoc(pos, strHash, pArchivedClient);
		if (pArchivedClient != NULL)
			m_ArchivedClientsByRuntimeID[pArchivedClient->GetRuntimeID()] = pArchivedClient;
	}
}

void CClientList::RegisterArchivedClient(CUpDownClient* pArchivedClient)
{
	if (pArchivedClient == NULL)
		return;

	CSingleLock runtimeMapLock(&m_csTrackedClientRuntimeMaps, TRUE);
	m_ArchivedClientsByRuntimeID[pArchivedClient->GetRuntimeID()] = pArchivedClient;
}

void CClientList::UnregisterArchivedClient(const CUpDownClient* pArchivedClient)
{
	if (pArchivedClient == NULL)
		return;

	CSingleLock runtimeMapLock(&m_csTrackedClientRuntimeMaps, TRUE);
	m_ArchivedClientsByRuntimeID.erase(pArchivedClient->GetRuntimeID());
}


///////////////////////////////////////////////////////////////////////////////
// Kad client list

bool CClientList::RequestTCP(Kademlia::CContact *contact, uint8 byConnectOptions)
{
	uint32 nContactIP = contact->GetNetIP();
	// don't connect ourself
	if (theApp.serverconnect->GetLocalIP() == nContactIP && thePrefs.GetPort() == contact->GetTCPPort())
		return false;

	CUpDownClient *pNewClient = FindClientByIP(CAddress(nContactIP, false), contact->GetTCPPort());

	if (!pNewClient)
		pNewClient = new CUpDownClient(NULL, contact->GetTCPPort(), contact->GetIPAddress(), 0, 0, false); // IPv6-TODO: Check this
	else {
		if (pNewClient->GetKadState() != KS_NONE)
			return false; // already busy with this client in some way (probably buddy stuff), don't mess with it
		if (pNewClient->socket != NULL)
			return false; //already existing socket can give false high ID
	}
	//Add client to the lists to be processed.
	pNewClient->SetKadPort(contact->GetUDPPort());
	pNewClient->SetKadState(KS_QUEUED_FWCHECK);
	if (contact->GetClientID() != 0) {
		byte ID[16];
		contact->GetClientID().ToByteArray(ID);
		pNewClient->SetUserHash(ID);
		pNewClient->SetConnectOptions(byConnectOptions, true, false);
	}
	m_KadList.AddTail(pNewClient);
	//This method checks if this is a dup already.
	AddClient(pNewClient);
	return true;
}

void CClientList::RequestServingBuddy(Kademlia::CContact *contact, uint8 byConnectOptions, bool bForce)
{
	uint32 nContactIP = contact->GetNetIP();
	// don't connect to ourself
	if (theApp.serverconnect->GetLocalIP() == nContactIP && thePrefs.GetPort() == contact->GetTCPPort()) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[Buddy] Serving buddy candidate rejected: self endpoint %s:%u"), (LPCTSTR)ipstr(nContactIP), contact->GetTCPPort());
		return;
	}
	if (!bForce && IsKadFirewallCheckIP(CAddress(nContactIP, false))) { // doing a kad firewall check with this IP, abort
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[Buddy] Serving buddy candidate rejected: Kad firewall check collision for IP %s"), (LPCTSTR)ipstr(nContactIP));
		return;
	}
	CUpDownClient *pNewClient = FindClientByIP(CAddress(nContactIP, false), contact->GetTCPPort());
	if (!pNewClient)
		pNewClient = new CUpDownClient(NULL, contact->GetTCPPort(), contact->GetIPAddress(), 0, 0, false); // IPv6-TODO: Check this
	else if (pNewClient->GetKadState() != KS_NONE) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[Buddy] Serving buddy candidate rejected: client busy with Kad state=%u, %s"), (unsigned)pNewClient->GetKadState(), (LPCTSTR)EscPercent(pNewClient->DbgGetClientInfo()));
		return; // already busy with this client in some way (probably fw stuff), don't mess with it
	}

	// Add client to the lists to be processed.
	// Seed connect and verified IP to ensure TCP attach uses the same instance.
	CAddress addrContact(nContactIP, false);
	pNewClient->SetConnectIP(addrContact);
	pNewClient->SetIP(addrContact);
	pNewClient->SetKadPort(contact->GetUDPPort());
	pNewClient->SetKadState(KS_QUEUED_SERVING_BUDDY);
	byte ID[16];
	// Do not set ED2K userhash from KadID here; wait for TCP hello to supply the real ED2K userhash.
	pNewClient->SetConnectOptions(byConnectOptions, true, false);
	
	// Set serving buddy endpoint so served buddy knows where to send callback relay requests
	pNewClient->SetServingBuddyIP(addrContact);
	pNewClient->SetServingBuddyPort(contact->GetUDPPort());
	
	AddToKadList(pNewClient);
	//This method checked already if this is a dup.
	AddClient(pNewClient);
}

bool CClientList::IncomingServedBuddy(Kademlia::CContact *contact, Kademlia::CUInt128 *servedBuddyID)
{
	// Limit the number of served buddy clients.
	if (m_ServedBuddyMap.GetCount() >= thePrefs.GetMaxServedBuddies()) {
		DebugLog(_T("[Buddy]: IncomingServedBuddy: served buddy limit reached\n"));
		return false;
	}
	uint32 nContactIP = contact->GetNetIP();
	CAddress addrContact(nContactIP, false);
	uint16  uTCPPort = contact->GetTCPPort();
	uint16  uUDPPort = contact->GetUDPPort();
#ifndef NATTTESTMODE
	// If eMule already knows this client, abort this except for the eServer buddy bridge.
	// The bridge deliberately reuses the existing served eServer buddy TCP client as a Kad served buddy candidate.
	if (CUpDownClient* pKnown = FindClientByIP(addrContact, uTCPPort)) {
		if (!IsServedEServerBuddy(pKnown))
			return false;
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[eServerBuddy][KadBridge] Reusing served eServer buddy for incoming Kad serving buddy request: %s"), (LPCTSTR)EscPercent(pKnown->DbgGetClientInfo()));
	}
#endif

	// If we are already serving this buddy, ignore duplicate requests.
	CUpDownClient* pDupByKad = NULL;
	byte idBuf[16] = {};
	servedBuddyID->ToByteArray(idBuf);
	Kademlia::CUInt128 uCheck(idBuf);

	// Check with Kad ID, then with IP, Kad UDP.
	if (FindServedBuddyByKadID(uCheck) || FindServedBuddyByAddr(addrContact, uUDPPort))
		return false;

	if (IsKadFirewallCheckIP(addrContact)) { // doing a kad firewall check with this IP, abort
		DebugLogWarning(_T("[Buddy]: KAD TCP Firewall check / Serving buddy request collision for IP %s"), (LPCTSTR)ipstr(nContactIP));
		return false;
	}
	if (theApp.serverconnect->GetLocalIP() == nContactIP && thePrefs.GetPort() == uTCPPort)
		return false; // don't connect ourself

	// Try to reuse an existing client object by IP:TCP if available.
	if (CUpDownClient* pExisting = FindClientByIP(addrContact, uTCPPort)) {
		pExisting->SetKadPort(uUDPPort);
		if (pExisting->socket != NULL && pExisting->socket->IsConnected()) {
			pExisting->ClearIncomingServedBuddyWait();
			pExisting->SetKadState(KS_CONNECTED_BUDDY);
		} else {
			pExisting->StartIncomingServedBuddyWait();
			pExisting->SetKadState(KS_INCOMING_SERVED_BUDDY);
		}
		pExisting->SetIP(addrContact); // Ensure verified IP is set so the TCP socket can attach to the same instance.
		byte idTmp[16];
		servedBuddyID->ToByteArray(idTmp);
		pExisting->SetServingBuddyID(idTmp);

		// Seed served buddy with our own endpoint to allow them to forward callbacks to us
		CAddress myIP = theApp.GetPublicIP();
		if (!myIP.IsNull())
			pExisting->SetServingBuddyIP(myIP);
		// Use our own external Kad port for served buddy to reach us
		uint16 myKadPort = 0;
		if (Kademlia::CKademlia::GetPrefs()->GetUseExternKadPort() && Kademlia::CKademlia::GetPrefs()->GetExternalKadPort() != 0)
			myKadPort = Kademlia::CKademlia::GetPrefs()->GetExternalKadPort();
		else if (Kademlia::CKademlia::GetPrefs()->GetInternKadPort() != 0)
			myKadPort = Kademlia::CKademlia::GetPrefs()->GetInternKadPort();
		else
			myKadPort = thePrefs.GetUDPPort();
		pExisting->SetServingBuddyPort(myKadPort);

		AddToKadList(pExisting);
		AddServedBuddy(pExisting);
		return true;
	}

	// Add client to the lists to be processed.
	CUpDownClient* pNewClient = new CUpDownClient(NULL, uTCPPort, contact->GetIPAddress(), 0, 0, false); // IPv6-TODO: Check this
	pNewClient->SetConnectIP(addrContact); // Seed connect address early to avoid 0.0.0.0 in UI and to allow address-based dedup.
	pNewClient->SetIP(addrContact); // Also set verified IP to allow AttachToAlreadyKnown to match on IP.
	pNewClient->SetKadPort(uUDPPort);
	pNewClient->StartIncomingServedBuddyWait();
	pNewClient->SetKadState(KS_INCOMING_SERVED_BUDDY);
	byte ID[16];
	contact->GetClientID().ToByteArray(ID);
	servedBuddyID->ToByteArray(ID);
	pNewClient->SetServingBuddyID(ID);

	// Seed served buddy with our own endpoint to allow them to forward callbacks to us
	CAddress myIP = theApp.GetPublicIP();
	if (!myIP.IsNull())
		pNewClient->SetServingBuddyIP(myIP);
	// Use our own external Kad port for served buddy to reach us
	uint16 myKadPort = 0;
	if (Kademlia::CKademlia::GetPrefs()->GetUseExternKadPort() && Kademlia::CKademlia::GetPrefs()->GetExternalKadPort() != 0)
		myKadPort = Kademlia::CKademlia::GetPrefs()->GetExternalKadPort();
	else if (Kademlia::CKademlia::GetPrefs()->GetInternKadPort() != 0)
		myKadPort = Kademlia::CKademlia::GetPrefs()->GetInternKadPort();
	else
		myKadPort = thePrefs.GetUDPPort();
	pNewClient->SetServingBuddyPort(myKadPort);

	AddToKadList(pNewClient);
	AddClient(pNewClient);
	AddServedBuddy(pNewClient);
	return true;
}

void CClientList::RemoveFromKadList(CUpDownClient *torem)
{
	POSITION pos = m_KadList.Find(torem);
	if (pos) {
		if (torem == m_pServingBuddy) {
			m_pServingBuddy = NULL;
			theApp.emuledlg->serverwnd->UpdateMyInfo();
		}
		m_KadList.RemoveAt(pos);
	}

	torem->ClearIncomingServedBuddyWait();
	RemoveServedBuddy(torem);
}

// Check if we are already serving this client as buddy.
bool CClientList::IsServedBuddy(const CUpDownClient* pClient) const
{
	// Fast lookup via map
	if (!pClient)
		return false;

	return m_ServedBuddyMap.PLookup(const_cast<CUpDownClient*>(pClient)) != NULL;
}

// Add a client to the served buddy list.
void CClientList::AddServedBuddy(CUpDownClient* pClient)
{
	if (pClient == NULL || IsServedBuddy(pClient))
		return;

	if (m_ServedBuddyMap.GetCount() >= thePrefs.GetMaxServedBuddies()) {
		DebugLog(_T("[Buddy]: Served buddy limit reached; ignoring new served buddy %s\n"), (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
		return;
	}

	m_ServedBuddyMap.SetAt(pClient, pClient);
	
	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[NatTraversal] AddServedBuddy: SUCCESS - added %s (sbid=%s)"), (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()), (LPCTSTR)md4str(pClient->GetServingBuddyID()));
}

// Remove a client from the served buddy list.
void CClientList::RemoveServedBuddy(const CUpDownClient* pClient)
{
	if (pClient == NULL)
		return;

	m_ServedBuddyMap.RemoveKey(const_cast<CUpDownClient*>(pClient));
}

// Clear the entire served buddy list.
void CClientList::ClearAllServedBuddies()
{
	POSITION pos = m_ServedBuddyMap.GetStartPosition();
	while (pos != NULL) {
		CUpDownClient* pKey = NULL;
		CUpDownClient* pClient = NULL;
		m_ServedBuddyMap.GetNextAssoc(pos, pKey, pClient);
		if (pClient == NULL)
			continue;
		pClient->ClearIncomingServedBuddyWait();
		if (pClient->GetKadState() == KS_INCOMING_SERVED_BUDDY || pClient->GetKadState() == KS_CONNECTED_BUDDY)
			pClient->SetKadState(KS_NONE);
	}
	m_ServedBuddyMap.RemoveAll();
}

// Find a served buddy client by its Kad ID.
CUpDownClient* CClientList::FindServedBuddyByKadID(const Kademlia::CUInt128& kadid) const
{
	byte id[16] = {};
	kadid.ToByteArray(id);
	CUpDownClient* pFallback = NULL;
	POSITION pos = m_ServedBuddyMap.GetStartPosition();
	while (pos != NULL) {
		CUpDownClient* pKey = NULL;
		CUpDownClient* pVal = NULL;
		m_ServedBuddyMap.GetNextAssoc(pos, pKey, pVal);
		CUpDownClient* p = pVal;
		if (p != NULL && p->HasValidServingBuddyID()) {
			const uchar* pb = p->GetServingBuddyID();
			if (memcmp(pb, id, sizeof id) == 0) {
				if (p->socket != NULL && p->socket->IsConnected())
					return p;
				if (pFallback == NULL)
					pFallback = p;
			}
		}
	}

	return pFallback;
}

// Find a served buddy by (IP, Kad UDP port) pair.
CUpDownClient* CClientList::FindServedBuddyByAddr(const CAddress& ip, uint16 udpPort) const
{
	POSITION pos = m_ServedBuddyMap.GetStartPosition();
	while (pos != NULL) {
		CUpDownClient* pKey = NULL;
		CUpDownClient* pVal = NULL;
		m_ServedBuddyMap.GetNextAssoc(pos, pKey, pVal);
		CUpDownClient* p = pVal;
		if (p != NULL && p->GetIP() == ip && p->GetKadPort() == udpPort)
			return p;
	}

	return NULL;
}

void CClientList::AddToKadList(CUpDownClient *toadd)
{
	if (toadd && !m_KadList.Find(toadd))
		m_KadList.AddTail(toadd);
}

bool CClientList::DoRequestFirewallCheckUDP(const Kademlia::CContact &contact)
{
	// first make sure we don't know this IP already from somewhere
	if (FindClientByIP(CAddress(contact.GetIPAddress(), true)) != NULL)
		return false;
	// fine, just create the client object, set the state and wait
	// TODO: We don't know the clients user hash, this means we cannot build an obfuscated connection,
	// which again means that the whole check won't work on "Require Obfuscation" setting,
	// which is not a huge problem, but certainly not nice. The only somewhat acceptable way
	// to solve this is to use the KadID instead.
	CUpDownClient *pNewClient = new CUpDownClient(NULL, contact.GetTCPPort(), contact.GetIPAddress(), 0, 0, false); // IPv6-TODO: Check this
	pNewClient->SetKadState(KS_QUEUED_FWCHECK_UDP);
	DebugLog(_T("Selected client for UDP Firewall check: %s"), (LPCTSTR)ipstr(contact.GetNetIP()));
	AddToKadList(pNewClient);
	AddClient(pNewClient);
	ASSERT(!pNewClient->SupportsDirectUDPCallback());
	return true;
}

void CClientList::CleanUpClientList()
{
	// we remove clients which are not needed any more by time
	// this check is also done on CUpDownClient::Disconnected, however it will not catch all
	// cases (if a client changes the state without being connected)
	//
	// Adding this check directly to every point where any state changes would be more effective,
	// though not compatible with the current code, because there are points where a client has
	// no state for some code lines and the code is also not prepared that a client object gets
	// invalid while working with it (aka setting a new state)
	// so this way is just the easy and safe one to go (as long as emule is basically single threaded)
	const time_t m_tCurTime = time(NULL);
	if (m_tCurTime >= m_tLastClientCleanUp + CLIENTLIST_CLEANUP_TIME) {
		m_tLastClientCleanUp = m_tCurTime;
		uint32 cDeleted = 0;
		for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
			CUpDownClient* pCurClient = list.GetNext(pos);
			if ((pCurClient->GetUploadState() == US_NONE || (pCurClient->GetUploadState() == US_BANNED && !pCurClient->IsBanned()))
				&& pCurClient->GetDownloadState() == DS_NONE
				&& pCurClient->GetChatState() == MS_NONE
				&& pCurClient->GetKadState() == KS_NONE
				&& pCurClient->socket == NULL)
			{
				const time_t m_tDeltaTime = time(NULL) - pCurClient->m_tLastCleanUpCheck;
				if (m_tDeltaTime > 7200) { // 2 hours
					if (!pCurClient->m_OtherNoNeeded_list.IsEmpty() || !pCurClient->m_OtherRequests_list.IsEmpty())	{
						AddDebugLogLine(false, _T("CleanUpClientList: Archiving of the client has been postponed by extended clean-up: %s"), (LPCTSTR)EscPercent(pCurClient->DbgGetClientInfo()));
						pCurClient->m_tLastCleanUpCheck = time(NULL);
					} else {
						++cDeleted;
						CUpDownClient::SafeDelete(pCurClient);
						RemoveServedBuddy(pCurClient); // Ensure served buddy unlink before deletion.
					} 
				}
			}
		}
		DEBUG_ONLY(AddDebugLogLine(false, _T("Cleaned ClientList, removed %i non-used known clients"), cDeleted));
	}
}

void CClientList::CleanUp(CPartFile* pDeletedFile) {
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient* cur_client = list.GetNext(pos);
		cur_client->CleanUp(pDeletedFile);
	}
}

CDeletedClient::CDeletedClient(const CUpDownClient *pClient)
{
	m_cBadRequest = 0;
	m_tInserted = time(NULL);
	m_ItemsList.Add(PORTANDHASH{ pClient->GetUserPort(), pClient->Credits() });
}

void CClientList::ProcessA4AFClients() const
{
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient *cur_client = list.GetNext(pos);

		if (cur_client->GetDownloadState() != DS_DOWNLOADING
			&& cur_client->GetDownloadState() != DS_CONNECTED
			&& (!cur_client->m_OtherRequests_list.IsEmpty() || !cur_client->m_OtherNoNeeded_list.IsEmpty()))
		{
			cur_client->SwapToAnotherFile(_T("Periodic A4AF check CClientList::ProcessA4AFClients()"), false, false, false, NULL, true, false);
		}
	}
}

void CClientList::AddKadFirewallRequest(CAddress IP)
{
	const DWORD curTick = ::GetTickCount();
	listFirewallCheckRequests.AddHead(IPANDTICS{ IP, curTick });
	while (!listFirewallCheckRequests.IsEmpty() && curTick >= listFirewallCheckRequests.GetTail().dwInserted + SEC2MS(180))
		listFirewallCheckRequests.RemoveTail();
}

bool CClientList::IsKadFirewallCheckIP(CAddress IP) const
{
	const DWORD curTick = ::GetTickCount();
	for (POSITION pos = listFirewallCheckRequests.GetHeadPosition(); pos != NULL;) {
		const IPANDTICS& iptick = listFirewallCheckRequests.GetNext(pos);
		if (curTick >= iptick.dwInserted + SEC2MS(180))
			break;
		if (iptick.IP == IP)
			return true;
	}
	return false;
}

void CClientList::AddConnectingClient(CUpDownClient *pToAdd)
{
	for (POSITION pos = m_liConnectingClients.GetHeadPosition(); pos != NULL;) {
		CONNECTINGCLIENT& cc = m_liConnectingClients.GetNext(pos);
		if (cc.pClient == pToAdd) {
			cc.dwInserted = ::GetTickCount();
			return;
		}
	}

	ASSERT(pToAdd->GetConnectingState() != CCS_NONE);
	m_liConnectingClients.AddTail(CONNECTINGCLIENT{ pToAdd, ::GetTickCount() });
}

void CClientList::ProcessConnectingClientsList()
{
	// we do check if any connects have timed out by now
	const DWORD curTick = ::GetTickCount();
	for (POSITION pos = m_liConnectingClients.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		const CONNECTINGCLIENT cc = m_liConnectingClients.GetNext(pos);
		if (curTick >= cc.dwInserted + SEC2MS(45)) {
			ASSERT(cc.pClient->GetConnectingState() != CCS_NONE);
			m_liConnectingClients.RemoveAt(pos2);
			if (cc.pClient->Disconnected(_T("Connection try timeout")))
				CUpDownClient::SafeDelete(cc.pClient);
		}
	}
}

void CClientList::RemoveConnectingClient(const CUpDownClient *pToRemove)
{
	for (POSITION pos = m_liConnectingClients.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		if (m_liConnectingClients.GetNext(pos).pClient == pToRemove) {
			m_liConnectingClients.RemoveAt(pos2);
			return;
		}
	}
}

void CClientList::AddTrackCallbackRequests(CAddress IP)
{
	const DWORD curTick = ::GetTickCount();
	listDirectCallbackRequests.AddHead(IPANDTICS{ IP, curTick });
	while (!listDirectCallbackRequests.IsEmpty() && curTick >= listDirectCallbackRequests.GetTail().dwInserted + SEC2MS(180))
		listDirectCallbackRequests.RemoveTail();
}

bool CClientList::AllowCalbackRequest(CAddress IP) const
{
	const DWORD curTick = ::GetTickCount();
	for (POSITION pos = listDirectCallbackRequests.GetHeadPosition(); pos != NULL;) {
		const IPANDTICS& iptick = listDirectCallbackRequests.GetNext(pos);
		if (iptick.IP == IP && curTick < iptick.dwInserted + SEC2MS(180))
			return false;
	}
	return true;
}

void CClientList::ResetIPGeolocation()
{
	CUpDownClient* cur_client;
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		cur_client = list.GetNext(pos);
		if (cur_client != NULL)
			cur_client->ResetIPGeolocation();
	}

	for (POSITION pos = m_ArchivedClientsMap.GetStartPosition(); pos != NULL;) {
		CString cur_hash;
		CUpDownClient* cur_client = NULL;
		m_ArchivedClientsMap.GetNextAssoc(pos, cur_hash, cur_client);
		if (cur_client != NULL)
			cur_client->ResetIPGeolocation();
	}
}

void CClientList::CancelCategoryPunishments(uint8 uBadClientCategory) const
{
	CUpDownClient* cur_client;
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		cur_client = list.GetNext(pos);
		if (cur_client && cur_client->m_uBadClientCategory == uBadClientCategory)
			theApp.shield->SetPunishment(cur_client,NULL, PR_NOTBADCLIENT);
	}
}

void CClientList::CancelFriendPunishments() const
{
	CUpDownClient* cur_client;
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		cur_client = list.GetNext(pos);
		if (cur_client && cur_client->m_uBadClientCategory && cur_client->IsFriend())
			theApp.shield->SetPunishment(cur_client,NULL, PR_NOTBADCLIENT);
	}
}

uint32 CClientList::BadClientCount()
{
	uint32 m_uCount = 0;
	CUpDownClient* cur_client;
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		cur_client = list.GetNext(pos);
		if (cur_client->m_uBadClientCategory)
			m_uCount++;
	}
	return m_uCount;
}

void CClientList::GetModStatistics(CRBMap<uint32, CRBMap<CString, uint32>* >* clientMods) {
	if (!clientMods)
		return;
	clientMods->RemoveAll();

	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient* cur_client = list.GetNext(pos);

		switch (cur_client->GetClientSoft()) {
		case SO_EMULE:
		case SO_OLDEMULE:
			break;
		default:
			continue;
		}

		CString aiVersion;
		if (cur_client->GetAIModVersionString(aiVersion))
			continue;

		CRBMap<CString, uint32>* versionMods;

		if (!clientMods->Lookup(cur_client->GetVersion(), versionMods)) {
			versionMods = new CRBMap<CString, uint32>;
			versionMods->RemoveAll();
			clientMods->SetAt(cur_client->GetVersion(), versionMods);
		}

		uint32 count;

		if (!versionMods->Lookup(cur_client->GetClientModVer(), count))
			count = 1;
		else
			count++;

		versionMods->SetAt(cur_client->GetClientModVer(), count);
	}
}

void CClientList::ReleaseModStatistics(CRBMap<uint32, CRBMap<CString, uint32>* >* clientMods) {
	if (!clientMods)
		return;
	POSITION pos = clientMods->GetHeadPosition();
	while (pos != NULL) {
		uint32 version;
		CRBMap<CString, uint32>* versionMods;
		clientMods->GetNextAssoc(pos, version, versionMods);
		delete versionMods;
	}
	clientMods->RemoveAll();
}

bool CClientList::LoadList()
{
	return LoadList(NULL, true, true);
}

bool CClientList::LoadList(CArchivedClientsMap* pTargetArchivedClients, bool bAssignCredits, bool bRebuildRuntimeMap)
{
	CArchivedClientsMap& archivedClients = pTargetArchivedClients != NULL ? *pTargetArchivedClients : m_ArchivedClientsMap;
	const CString& strFileName(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + CLIENT_HISTORY_MET_FILENAME);
	CSafeBufferedFile file;
	CFileException ex;
	if (!file.Open(strFileName, CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyWrite, &ex)) {
		if (bAssignCredits && ex.m_cause != CFileException::fileNotFound) 
			LogError(LOG_STATUSBAR, GetResString(_T("ERR_READEMFRIENDS")), (LPCTSTR)EscPercent(CExceptionStrDash(ex)));
		return false;
	}
	::setvbuf(file.m_pStream, NULL, _IOFBF, kLargeMetFileBufferSize);

	try {
		uint8 header = file.ReadUInt8();
		if (header != MET_HEADER) {
			file.Close();
			return false;
		}

		int count = file.ReadUInt32(); //number of records
		archivedClients.InitHashTable(count + 5000); // TODO: should be prime number and 20% larger.

		for (int i = 0; i < count; ++i) { 
			CUpDownClient* Record = new CUpDownClient();
			Record->LoadFromFile(file);

			if (time(NULL) >= (time_t)(Record->tLastSeen + time_t(thePrefs.GetClientHistoryExpDays() < 1 ? 12960000 : thePrefs.GetClientHistoryExpDays() * 86400))) { // Skip if this is an expired client. 12960000=5x30x24x60x60 (5 months)  86400=24x60x60 (1 day)
				delete Record;
				Record = NULL;
				continue;
			}

			if (!Record->IsBadClient()) { // If record's punishment is expired
				Record->SetGPLEvildoer(false);
				Record->m_uPunishment = P_NOPUNISHMENT;
				Record->m_uBadClientCategory = PR_NOTBADCLIENT;
				Record->m_tPunishmentStartTime = 0;
				Record->m_strPunishmentReason.Empty();
			} else if (bAssignCredits && Record->IsBadClient() && Record->m_uPunishment <= P_USERHASHBAN) // If this client is already marked as IP or user hash banned, we need to set up its punishment and ban status 
				Record->Ban(Record->m_strPunishmentReason, Record->m_uBadClientCategory, Record->m_uPunishment, Record->m_tPunishmentStartTime);

			CString cur_hash = md4str(Record->GetUserHash());
			CUpDownClient* ArchivedClient = NULL;
			if (archivedClients.Lookup(cur_hash, ArchivedClient) && ArchivedClient) { // A copy of this client exists in the history. This is a duplicate entry case and we'll only update selected data conditionally.
				if (Record->tFirstSeen < ArchivedClient->tFirstSeen) // We'll keep the oldest dwFirstSeen
					ArchivedClient->tFirstSeen = Record->tFirstSeen;
				if (Record->tLastSeen > ArchivedClient->tLastSeen) // We'll keep the newest dwLastSeen
					ArchivedClient->tLastSeen = Record->tLastSeen;
				if (Record->IsBadClient() && // If record's punishment isn't expired
					(((Record->m_uPunishment < ArchivedClient->m_uPunishment) || // We'll keep the lowest numeric value of m_iPunishment since it is worse type of client.
					(Record->m_uPunishment == ArchivedClient->m_uPunishment && Record->m_tPunishmentStartTime > ArchivedClient->m_tPunishmentStartTime)))) { // We'll keep last punished time if m_iPunishment is equal. This will shift punishment end time. 
					ArchivedClient->SetGPLEvildoer(Record->GetGPLEvildoer());
					ArchivedClient->m_uPunishment = Record->m_uPunishment;
					ArchivedClient->m_uBadClientCategory = Record->m_uBadClientCategory;
					ArchivedClient->m_tPunishmentStartTime = Record->m_tPunishmentStartTime;
					ArchivedClient->m_strPunishmentReason = Record->m_strPunishmentReason;
				}
				if (bAssignCredits && thePrefs.GetClientHistoryLog())
					AddDebugLogLine(false, _T("[CLIENT HISTORY]: Client in the history updated by LoadList -> Hash: %s | Name: %s"), md4str(ArchivedClient->GetUserHash()), (LPCTSTR)EscPercent(ArchivedClient->GetUserName()));
				// Delete record since it is absolute now.
				delete Record;
				Record = NULL;
				continue;
			} else { // Client doesn't exist in the history. We can now insert Record to the map.
				if (bAssignCredits && theApp.clientcredits != NULL)
					Record->credits = theApp.clientcredits->GetCredit(Record->GetUserHash(), false); // Find and set credits before adding.
				archivedClients[cur_hash] = Record; // Add this client to the history map.
				if (bAssignCredits && thePrefs.GetClientHistoryLog())
					AddDebugLogLine(false, _T("[CLIENT HISTORY]: Client loaded -> Hash: %s | Name: %s"), md4str(Record->GetUserHash()), (LPCTSTR)EscPercent(Record->GetUserName()));
			}

			// Activate auto query if conditions are met
			if (!Record->m_bAutoQuerySharedFiles && Record->m_uSharedFilesStatus == S_NOT_QUERIED && Record->GetViewSharedFilesSupport() && Record->credits &&
				((thePrefs.GetRemoteSharedFilesSetAutoQueryDownload() && Record->credits->GetDownloadedTotal() / 1048576 >= thePrefs.GetRemoteSharedFilesSetAutoQueryDownloadThreshold()) ||		//1024*1024
				(thePrefs.GetRemoteSharedFilesSetAutoQueryUpload() && Record->credits->GetUploadedTotal() / 1048576 >= thePrefs.GetRemoteSharedFilesSetAutoQueryUploadThreshold())))				//1024*1024
				Record->m_bAutoQuerySharedFiles = true;
		}
		file.Close();
		if (bRebuildRuntimeMap)
			RebuildArchivedRuntimeMap();
		if(bAssignCredits && archivedClients.GetCount())
				AddLogLine(false, GetResString(_T("CLIENT_HISTORY_LOADED")), archivedClients.GetCount());
		return true;
	} catch (CFileException* ex) {
		if (bAssignCredits) {
			if (ex->m_cause == CFileException::endOfFile)
				LogError(LOG_STATUSBAR, GetResString(_T("ERR_CLIENTHISTORYINVALID")));
			else
				LogError(LOG_STATUSBAR, GetResString(_T("ERR_READCLIENTHISTORY")), (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
		}
		ex->Delete();
	}
	return false;
}

CClientList::SClientHistoryRecord::SClientHistoryRecord()
	: uLastSeen(0)
	, uFirstSeen(static_cast<uint64>(time(NULL)))
	, dwServerIP(0)
	, nUserIDHybrid(0)
	, nUserPort(0)
	, nServerPort(0)
	, nClientVersion(0)
	, byEmuleVersion(0)
	, bEmuleProtocol(false)
	, bIsHybrid(false)
	, nUDPPort(0)
	, nKadPort(0)
	, byUDPVer(0)
	, byCompatibleClient(0)
	, bIsML(false)
	, bGPLEvildoer(false)
	, cMessagesReceived(0)
	, cMessagesSent(0)
	, byKadVersion(0)
	, bNoViewSharedFiles(false)
	, uPunishment(P_NOPUNISHMENT)
	, uBadClientCategory(PR_NOTBADCLIENT)
	, uPunishmentStartTime(0)
	, uSharedFilesStatus(S_NOT_QUERIED)
	, uSharedFilesLastQueriedTime(0)
	, uSharedFilesCount(0)
	, bAutoQuerySharedFiles(false)
{
	md4clr(userHash);
}

CClientList::SClientHistorySaveSnapshot::SClientHistorySaveSnapshot()
	: lGeneration(0)
	, plGeneration(NULL)
{
}

CClientList::SClientHistorySaveJob::SClientHistorySaveJob()
	: bActive(false)
	, eStage(ClientHistorySaveJobNone)
	, lGeneration(0)
	, uNextActiveClient(0)
	, posNextArchivedClient(NULL)
	, uActiveArchived(0)
	, uRecordsBuilt(0)
{
}

void CClientList::SClientHistorySaveJob::Reset()
{
	for (size_t i = 0; i < activeClients.size(); ++i) {
		if (activeClients[i] != NULL) {
			activeClients[i]->ReleaseRuntimeReference();
			activeClients[i] = NULL;
		}
	}
	bActive = false;
	eStage = ClientHistorySaveJobNone;
	lGeneration = 0;
	strConfigDir.Empty();
	activeClients.clear();
	uNextActiveClient = 0;
	posNextArchivedClient = NULL;
	records.clear();
	uActiveArchived = 0;
	uRecordsBuilt = 0;
}

CClientList::SAutoQuerySharedFilesJob::SAutoQuerySharedFilesJob()
	: bActive(false)
	, bCandidatesSorted(false)
	, eStage(AutoQuerySharedFilesJobNone)
	, posNextArchivedClient(NULL)
	, uNextActiveClient(0)
	, uMaxQueries(0)
	, uQueried(0)
	, tNow(0)
{
}

void CClientList::SAutoQuerySharedFilesJob::Reset()
{
	for (size_t i = 0; i < activeClients.size(); ++i) {
		if (activeClients[i] != NULL)
			activeClients[i]->ReleaseRuntimeReference();
	}
	for (size_t i = 0; i < candidates.size(); ++i) {
		if (candidates[i] != NULL)
			candidates[i]->ReleaseRuntimeReference();
	}
	bActive = false;
	bCandidatesSorted = false;
	eStage = AutoQuerySharedFilesJobNone;
	posNextArchivedClient = NULL;
	activeClients.clear();
	uNextActiveClient = 0;
	candidates.clear();
	uMaxQueries = 0;
	uQueried = 0;
	tNow = 0;
}

bool CClientList::ReadStartupClientHistoryRecord(CFileDataIO& file, SClientHistoryRecord& record)
{
	file.ReadHash16(record.userHash);
	record.uLastSeen = file.ReadUInt64();

	for (uint32 tagcount = file.ReadUInt32(); tagcount > 0; --tagcount) {
		const CTag tag(file, false);
		switch (tag.GetNameID()) {
		case CL_NAME:
			if (tag.IsStr())
				record.strUserName = tag.GetStr();
			break;
		case CL_CONNECTIP:
			if (tag.IsStr())
				record.connectIP.FromString(tag.GetStr(), false);
			break;
		case CL_USERIP:
			if (tag.IsStr())
				record.userIP.FromString(tag.GetStr(), false);
			break;
		case CL_SERVERIP:
			if (tag.IsInt())
				record.dwServerIP = tag.GetInt();
			break;
		case CL_USERIDHYBRID:
			if (tag.IsInt())
				record.nUserIDHybrid = tag.GetInt();
			break;
		case CL_USERPORT:
			if (tag.IsInt())
				record.nUserPort = static_cast<uint16>(tag.GetInt());
			break;
		case CL_SERVERPORT:
			if (tag.IsInt())
				record.nServerPort = static_cast<uint16>(tag.GetInt());
			break;
		case CL_CLIENTVERSION:
			if (tag.IsInt())
				record.nClientVersion = tag.GetInt();
			break;
		case CL_EMULEVERSION:
			if (tag.IsInt())
				record.byEmuleVersion = static_cast<uint8>(tag.GetInt());
			break;
		case CL_EMULEPROTOCOL:
			if (tag.IsInt())
				record.bEmuleProtocol = tag.GetInt() != 0;
			break;
		case CL_ISHYBRID:
			if (tag.IsInt())
				record.bIsHybrid = tag.GetInt() != 0;
			break;
		case CL_UDPPORT:
			if (tag.IsInt())
				record.nUDPPort = static_cast<uint16>(tag.GetInt());
			break;
		case CL_KADPORT:
			if (tag.IsInt())
				record.nKadPort = static_cast<uint16>(tag.GetInt());
			break;
		case CL_UDPVER:
			if (tag.IsInt())
				record.byUDPVer = static_cast<uint8>(tag.GetInt());
			break;
		case CL_COMPATIBLECLIENT:
			if (tag.IsInt())
				record.byCompatibleClient = static_cast<uint8>(tag.GetInt());
			break;
		case CL_ISML:
			if (tag.IsInt())
				record.bIsML = tag.GetInt() != 0;
			break;
		case CL_GPLEVILDOER:
			if (tag.IsInt())
				record.bGPLEvildoer = tag.GetInt() != 0;
			break;
		case CL_CLIENTSOFTWARE:
			if (tag.IsStr())
				record.strClientSoftware = tag.GetStr();
			break;
		case CL_MODVERSION:
			if (tag.IsStr())
				record.strModVersion = tag.GetStr();
			break;
		case CL_MESSAGESRECEIVED:
			if (tag.IsInt())
				record.cMessagesReceived = static_cast<uint8>(tag.GetInt());
			break;
		case CL_MESSAGESSENT:
			if (tag.IsInt())
				record.cMessagesSent = static_cast<uint8>(tag.GetInt());
			break;
		case CL_KADVERSION:
			if (tag.IsInt())
				record.byKadVersion = static_cast<uint8>(tag.GetInt());
			break;
		case CL_NOVIEWSHAREDFILES:
			if (tag.IsInt())
				record.bNoViewSharedFiles = tag.GetInt() != 0;
			break;
		case CL_FIRSTSEEN:
			if (tag.IsInt64())
				record.uFirstSeen = tag.GetInt64();
			break;
		case CL_PUNISHMENT:
			if (tag.IsInt())
				record.uPunishment = static_cast<uint8>(tag.GetInt());
			break;
		case CL_BADCLIENTCATEGORY:
			if (tag.IsInt())
				record.uBadClientCategory = static_cast<uint8>(tag.GetInt());
			break;
		case CL_PUNISHMENTSTARTIME:
			if (tag.IsInt64())
				record.uPunishmentStartTime = tag.GetInt64();
			break;
		case CL_PUNISHMENTREASON:
			if (tag.IsStr())
				record.strPunishmentReason = tag.GetStr();
			break;
		case CL_SHAREDFILESSTATUS:
			if (tag.IsInt())
				record.uSharedFilesStatus = static_cast<uint8>(tag.GetInt());
			break;
		case CL_SHAREDFILESLASTQUERIEDTIME:
			if (tag.IsInt64())
				record.uSharedFilesLastQueriedTime = tag.GetInt64();
			break;
		case CL_SHAREDFILESCOUNT:
			if (tag.IsInt())
				record.uSharedFilesCount = tag.GetInt();
			break;
		case CL_CLIENTNOTE:
			if (tag.IsStr())
				record.strClientNote = tag.GetStr();
			break;
		case CL_AUTOQUERYSHAREDFILES:
			if (tag.IsInt())
				record.bAutoQuerySharedFiles = tag.GetInt() != 0;
			break;
		}
	}

	return true;
}

uint8 CClientList::GetStartupClientHistoryRecordBadCategory(const SClientHistoryRecord& record)
{
	if (record.uBadClientCategory > PR_NOTBADCLIENT) {
		const time_t tCurrTime = time(NULL);
		const time_t tPunishmentStartTime = static_cast<time_t>(record.uPunishmentStartTime);
		if (record.uPunishment <= P_USERHASHBAN) {
			if (tCurrTime >= tPunishmentStartTime + thePrefs.GetClientBanTime())
				return PR_NOTBADCLIENT;
		}
		else if (tCurrTime >= tPunishmentStartTime + thePrefs.GetClientScoreReducingTime())
			return PR_NOTBADCLIENT;
	}
	return record.uBadClientCategory;
}

void CClientList::NormalizeStartupClientHistoryRecordPunishment(SClientHistoryRecord& record)
{
	if (GetStartupClientHistoryRecordBadCategory(record) != PR_NOTBADCLIENT)
		return;
	record.bGPLEvildoer = false;
	record.uPunishment = P_NOPUNISHMENT;
	record.uBadClientCategory = PR_NOTBADCLIENT;
	record.uPunishmentStartTime = 0;
	record.strPunishmentReason.Empty();
}

void CClientList::MergeStartupClientHistoryRecord(SClientHistoryRecord& target, const SClientHistoryRecord& source)
{
	if (source.uFirstSeen < target.uFirstSeen)
		target.uFirstSeen = source.uFirstSeen;
	if (source.uLastSeen > target.uLastSeen)
		target.uLastSeen = source.uLastSeen;
	if (GetStartupClientHistoryRecordBadCategory(source) != PR_NOTBADCLIENT &&
		(((source.uPunishment < target.uPunishment) ||
		(source.uPunishment == target.uPunishment && source.uPunishmentStartTime > target.uPunishmentStartTime)))) {
		target.bGPLEvildoer = source.bGPLEvildoer;
		target.uPunishment = source.uPunishment;
		target.uBadClientCategory = source.uBadClientCategory;
		target.uPunishmentStartTime = source.uPunishmentStartTime;
		target.strPunishmentReason = source.strPunishmentReason;
	}
}

bool CClientList::LoadStartupClientHistoryRecords(CStartupClientHistoryRecords& records, LONG lGeneration, uint64 uCancellationToken) const
{
	records.clear();
	const CString& strFileName(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + CLIENT_HISTORY_MET_FILENAME);
	CSafeBufferedFile file;
	CFileException ex;
	if (!file.Open(strFileName, CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyWrite, &ex))
		return ex.m_cause == CFileException::fileNotFound;

	try {
		uint8 header = file.ReadUInt8();
		if (header != MET_HEADER) {
			file.Close();
			return false;
		}

		const uint32 uCount = file.ReadUInt32();
		records.reserve(static_cast<size_t>(uCount));
		CMap<CString, LPCTSTR, INT_PTR, INT_PTR> recordIndexes;

		if (lGeneration != 0 && uCancellationToken != 0)
			theApp.PublishStartupMetadataLoadProgress(CemuleApp::StartupMetadataClientHistory, lGeneration, uCancellationToken, _T("read-client-history"), 0, static_cast<UINT>(uCount));

		const uint64 uExpireSeconds = static_cast<uint64>(thePrefs.GetClientHistoryExpDays() < 1 ? 12960000 : thePrefs.GetClientHistoryExpDays() * 86400);
		const uint64 uNow = static_cast<uint64>(time(NULL));
		for (uint32 i = 0; i < uCount; ++i) {
			if ((i & 0x3ff) == 0 && lGeneration != 0 && uCancellationToken != 0) {
				theApp.PublishStartupMetadataLoadProgress(CemuleApp::StartupMetadataClientHistory, lGeneration, uCancellationToken, _T("read-client-history"), static_cast<UINT>(i), static_cast<UINT>(uCount));
				if (theApp.IsStartupMetadataLoadCancelled(CemuleApp::StartupMetadataClientHistory, lGeneration, uCancellationToken)) {
					file.Close();
					records.clear();
					return false;
				}
			}
			SClientHistoryRecord record;
			ReadStartupClientHistoryRecord(file, record);

			if (uNow >= record.uLastSeen + uExpireSeconds)
				continue;

			NormalizeStartupClientHistoryRecordPunishment(record);

			CString strHash = md4str(record.userHash);
			INT_PTR iExistingIndex = -1;
			if (recordIndexes.Lookup(strHash, iExistingIndex) && iExistingIndex >= 0 && static_cast<size_t>(iExistingIndex) < records.size()) {
				MergeStartupClientHistoryRecord(records[static_cast<size_t>(iExistingIndex)], record);
				continue;
			}

			records.push_back(record);
			recordIndexes.SetAt(strHash, static_cast<INT_PTR>(records.size() - 1));
		}
		if (lGeneration != 0 && uCancellationToken != 0)
			theApp.PublishStartupMetadataLoadProgress(CemuleApp::StartupMetadataClientHistory, lGeneration, uCancellationToken, _T("read-client-history"), static_cast<UINT>(uCount), static_cast<UINT>(uCount));
		file.Close();
		return true;
	}
	catch (CFileException* ex) {
		ex->Delete();
	}
	records.clear();
	return false;
}

CUpDownClient* CClientList::CreateArchivedClientFromStartupRecord(const SClientHistoryRecord& record)
{
	CUpDownClient* pClient = new CUpDownClient();
	pClient->m_bIsArchived = true;
	md4cpy(pClient->m_achUserHash, record.userHash);
	pClient->tLastSeen = static_cast<time_t>(record.uLastSeen);
	pClient->m_ConnectIP = record.connectIP;
	pClient->m_UserIP = record.userIP;
	pClient->m_dwServerIP = record.dwServerIP;
	pClient->m_nUserIDHybrid = record.nUserIDHybrid;
	pClient->m_nUserPort = record.nUserPort;
	pClient->m_nServerPort = record.nServerPort;
	pClient->m_nClientVersion = record.nClientVersion;
	pClient->m_byEmuleVersion = record.byEmuleVersion;
	pClient->m_bEmuleProtocol = record.bEmuleProtocol;
	pClient->m_bIsHybrid = record.bIsHybrid;
	pClient->m_nUDPPort = record.nUDPPort;
	pClient->m_nKadPort = record.nKadPort;
	pClient->m_byUDPVer = record.byUDPVer;
	pClient->m_byCompatibleClient = record.byCompatibleClient;
	pClient->m_bIsML = record.bIsML;
	pClient->m_bGPLEvildoer = record.bGPLEvildoer;
	if (!record.strUserName.IsEmpty())
		pClient->SetUserName(record.strUserName);
	pClient->m_strClientSoftware = record.strClientSoftware;
	pClient->m_strModVersion = record.strModVersion;
	pClient->m_cMessagesReceived = record.cMessagesReceived;
	pClient->m_cMessagesSent = record.cMessagesSent;
	pClient->m_byKadVersion = record.byKadVersion;
	pClient->m_fNoViewSharedFiles = record.bNoViewSharedFiles ? 1 : 0;
	pClient->tFirstSeen = static_cast<time_t>(record.uFirstSeen);
	pClient->m_uPunishment = record.uPunishment;
	pClient->m_uBadClientCategory = record.uBadClientCategory;
	pClient->m_tPunishmentStartTime = static_cast<time_t>(record.uPunishmentStartTime);
	pClient->m_strPunishmentReason = record.strPunishmentReason;
	pClient->m_uSharedFilesStatus = record.uSharedFilesStatus;
	pClient->m_tSharedFilesLastQueriedTime = static_cast<time_t>(record.uSharedFilesLastQueriedTime);
	pClient->m_uSharedFilesCount = record.uSharedFilesCount;
	pClient->m_strClientNote = record.strClientNote;
	pClient->m_bAutoQuerySharedFiles = record.bAutoQuerySharedFiles;

	CAddress clientIP(!pClient->m_UserIP.IsNull() ? pClient->m_UserIP : pClient->m_ConnectIP);
	pClient->SetIP(clientIP);
	pClient->InitClientSoftwareVersion();
	return pClient;
}

void CClientList::DeleteArchivedClientMap(CArchivedClientsMap* pArchivedClients)
{
	if (pArchivedClients == NULL)
		return;

	std::vector<CUpDownClient*> oldClients;
	oldClients.reserve(static_cast<size_t>(pArchivedClients->GetCount()));
	for (POSITION pos = pArchivedClients->GetStartPosition(); pos != NULL;) {
		CString strHash;
		CUpDownClient* pClient = NULL;
		pArchivedClients->GetNextAssoc(pos, strHash, pClient);
		if (pClient != NULL)
			oldClients.push_back(pClient);
	}
	pArchivedClients->RemoveAll();
	delete pArchivedClients;

	for (size_t i = 0; i < oldClients.size(); ++i)
		CUpDownClient::SafeDelete(oldClients[i]);
}

void CClientList::DeleteStartupClientHistoryRecords(CStartupClientHistoryRecords* pLoadedRecords)
{
	delete pLoadedRecords;
}

bool CClientList::BuildClientHistoryRecordFromClient(const CUpDownClient* pClient, SClientHistoryRecord& record, bool bAllowArchived) const
{
	if (pClient == NULL || (!bAllowArchived && pClient->m_bIsArchived) || IsCorruptOrBadUserHash(pClient->GetUserHash()))
		return false;

	md4cpy(record.userHash, pClient->GetUserHash());
	record.uLastSeen = static_cast<uint64>(pClient->tLastSeen != 0 ? pClient->tLastSeen : time(NULL));
	record.uFirstSeen = static_cast<uint64>(pClient->tFirstSeen != 0 ? pClient->tFirstSeen : static_cast<time_t>(record.uLastSeen));
	record.connectIP = pClient->m_ConnectIP;
	record.userIP = pClient->m_UserIP;
	record.dwServerIP = pClient->GetServerIP();
	record.nUserIDHybrid = pClient->GetUserIDHybrid();
	record.nUserPort = pClient->GetUserPort();
	record.nServerPort = pClient->GetServerPort();
	record.nClientVersion = pClient->GetVersion();
	record.byEmuleVersion = pClient->GetMuleVersion();
	record.bEmuleProtocol = pClient->ExtProtocolAvailable();
	record.bIsHybrid = pClient->GetIsHybrid();
	record.nUDPPort = pClient->GetUDPPort();
	record.nKadPort = pClient->GetKadPort();
	record.byUDPVer = pClient->GetUDPVersion();
	record.byCompatibleClient = pClient->GetCompatibleClient();
	record.bIsML = pClient->GetIsML();
	record.bGPLEvildoer = pClient->GetGPLEvildoer();
	record.strUserName = pClient->GetUserName();
	record.strClientSoftware = pClient->GetClientSoftVer();
	record.strModVersion = pClient->GetClientModVer();
	record.cMessagesReceived = pClient->GetMessagesReceived();
	record.cMessagesSent = pClient->GetMessagesSent();
	record.byKadVersion = pClient->GetKadVersion();
	record.bNoViewSharedFiles = !pClient->GetViewSharedFilesSupport();
	record.uPunishment = pClient->m_uPunishment;
	record.uBadClientCategory = pClient->m_uBadClientCategory;
	record.uPunishmentStartTime = static_cast<uint64>(pClient->m_tPunishmentStartTime);
	record.strPunishmentReason = pClient->m_strPunishmentReason;
	record.uSharedFilesStatus = pClient->m_uSharedFilesStatus;
	record.uSharedFilesLastQueriedTime = static_cast<uint64>(pClient->m_tSharedFilesLastQueriedTime);
	record.uSharedFilesCount = pClient->m_uSharedFilesCount;
	record.strClientNote = pClient->m_strClientNote;
	record.bAutoQuerySharedFiles = pClient->m_bAutoQuerySharedFiles;
	return true;
}

void CClientList::MergePendingClientHistoryRecord(const SClientHistoryRecord& record)
{
	const CString strHash(md4str(record.userHash));
	for (size_t i = 0; i < m_PendingClientHistoryRecords.size(); ++i) {
		SClientHistoryRecord& existingRecord = m_PendingClientHistoryRecords[i];
		if (md4str(existingRecord.userHash) != strHash)
			continue;
		const uint64 uFirstSeen = existingRecord.uFirstSeen < record.uFirstSeen ? existingRecord.uFirstSeen : record.uFirstSeen;
		const uint64 uLastSeen = existingRecord.uLastSeen > record.uLastSeen ? existingRecord.uLastSeen : record.uLastSeen;
		if (record.uLastSeen >= existingRecord.uLastSeen)
			existingRecord = record;
		MergeStartupClientHistoryRecord(existingRecord, record);
		existingRecord.uFirstSeen = uFirstSeen;
		existingRecord.uLastSeen = uLastSeen;
		return;
	}
	m_PendingClientHistoryRecords.push_back(record);
}

void CClientList::QueuePendingClientHistoryRecord(CUpDownClient* pClient)
{
	if (!GuardClientListMutation(_T("CClientList::QueuePendingClientHistoryRecord")))
		return;

	SClientHistoryRecord record;
	if (!BuildClientHistoryRecordFromClient(pClient, record))
		return;
	MergePendingClientHistoryRecord(record);
}

void CClientList::ApplyClientHistoryRecordToArchive(const SClientHistoryRecord& record, std::vector<CUpDownClient*>& discardedClients)
{
	if (IsCorruptOrBadUserHash(record.userHash))
		return;

	CUpDownClient* pLoadedClient = CreateArchivedClientFromStartupRecord(record);
	ApplyArchivedClientToArchive(pLoadedClient, discardedClients);
}

void CClientList::ApplyArchivedClientToArchive(CUpDownClient* pLoadedClient, std::vector<CUpDownClient*>& discardedClients)
{
	if (pLoadedClient == NULL)
		return;
	if (IsCorruptOrBadUserHash(pLoadedClient->GetUserHash())) {
		discardedClients.push_back(pLoadedClient);
		return;
	}

	CString strCanonicalHash(md4str(pLoadedClient->GetUserHash()));

	CUpDownClient* pExistingClient = NULL;
	if (m_ArchivedClientsMap.Lookup(strCanonicalHash, pExistingClient) && pExistingClient != NULL) {
		pExistingClient->m_bIsArchived = true;
		const bool bIncomingSnapshotIsNewer = pLoadedClient->tLastSeen >= pExistingClient->tLastSeen;
		if (pLoadedClient->tFirstSeen < pExistingClient->tFirstSeen)
			pExistingClient->tFirstSeen = pLoadedClient->tFirstSeen;
		if (pLoadedClient->tLastSeen > pExistingClient->tLastSeen)
			pExistingClient->tLastSeen = pLoadedClient->tLastSeen;
		if (bIncomingSnapshotIsNewer) {
			LPCTSTR pszLoadedUserName = pLoadedClient->GetUserName();
			if (pszLoadedUserName != NULL && pszLoadedUserName[0] != _T('\0'))
				pExistingClient->SetUserName(pszLoadedUserName);
			if (!pLoadedClient->m_ConnectIP.IsNull())
				pExistingClient->m_ConnectIP = pLoadedClient->m_ConnectIP;
			if (!pLoadedClient->m_UserIP.IsNull())
				pExistingClient->m_UserIP = pLoadedClient->m_UserIP;
			if (pLoadedClient->GetServerIP())
				pExistingClient->SetServerIP(pLoadedClient->GetServerIP());
			if (pLoadedClient->GetUserIDHybrid())
				pExistingClient->SetUserIDHybrid(pLoadedClient->GetUserIDHybrid());
			if (pLoadedClient->GetUserPort())
				pExistingClient->SetUserPort(pLoadedClient->GetUserPort());
			pExistingClient->SetServerPort(pLoadedClient->GetServerPort());
			pExistingClient->SetVersion(pLoadedClient->GetVersion());
			pExistingClient->SetMuleVersion(pLoadedClient->GetMuleVersion());
			pExistingClient->SetIsHybrid(pLoadedClient->GetIsHybrid());
			if (pLoadedClient->GetUDPPort())
				pExistingClient->SetUDPPort(pLoadedClient->GetUDPPort());
			if (pLoadedClient->GetKadPort())
				pExistingClient->SetKadPort(pLoadedClient->GetKadPort());
			pExistingClient->SetUDPVersion(pLoadedClient->GetUDPVersion());
			pExistingClient->SetCompatibleClient(pLoadedClient->GetCompatibleClient());
			pExistingClient->SetIsML(pLoadedClient->GetIsML());
			pExistingClient->SetEmuleProtocol(pLoadedClient->ExtProtocolAvailable());
			pExistingClient->SetClientSoftVer(pLoadedClient->GetClientSoftVer());
			pExistingClient->SetClientModVer(pLoadedClient->GetClientModVer());
			pExistingClient->InitClientSoftwareVersion();
			pExistingClient->SetMessagesSent(pLoadedClient->GetMessagesSent());
			pExistingClient->SetMessagesReceived(pLoadedClient->GetMessagesReceived());
			pExistingClient->SetKadVersion(pLoadedClient->GetKadVersion());
			pExistingClient->SetViewSharedFilesSupport(pLoadedClient->GetViewSharedFilesSupport());
			pExistingClient->m_uSharedFilesStatus = pLoadedClient->m_uSharedFilesStatus != S_NOT_QUERIED ? pLoadedClient->m_uSharedFilesStatus : pExistingClient->m_uSharedFilesStatus;
			pExistingClient->m_uSharedFilesCount = pLoadedClient->m_uSharedFilesCount ? pLoadedClient->m_uSharedFilesCount : pExistingClient->m_uSharedFilesCount;
			pExistingClient->m_tSharedFilesLastQueriedTime = pLoadedClient->m_tSharedFilesLastQueriedTime > pExistingClient->m_tSharedFilesLastQueriedTime ? pLoadedClient->m_tSharedFilesLastQueriedTime : pExistingClient->m_tSharedFilesLastQueriedTime;
			pExistingClient->m_bAutoQuerySharedFiles = pLoadedClient->m_bAutoQuerySharedFiles;
			pExistingClient->m_strClientNote = pLoadedClient->m_strClientNote.GetLength() ? pLoadedClient->m_strClientNote : pExistingClient->m_strClientNote;
		}
		if (pLoadedClient->IsBadClient() &&
			(((pLoadedClient->m_uPunishment < pExistingClient->m_uPunishment) ||
			(pLoadedClient->m_uPunishment == pExistingClient->m_uPunishment && pLoadedClient->m_tPunishmentStartTime > pExistingClient->m_tPunishmentStartTime)))) {
			pExistingClient->SetGPLEvildoer(pLoadedClient->GetGPLEvildoer());
			pExistingClient->m_uPunishment = pLoadedClient->m_uPunishment;
			pExistingClient->m_uBadClientCategory = pLoadedClient->m_uBadClientCategory;
			pExistingClient->m_tPunishmentStartTime = pLoadedClient->m_tPunishmentStartTime;
			pExistingClient->m_strPunishmentReason = pLoadedClient->m_strPunishmentReason;
		}
		discardedClients.push_back(pLoadedClient);
		return;
	}

	pLoadedClient->m_bIsArchived = true;
	if (pLoadedClient->IsBadClient() && pLoadedClient->m_uPunishment <= P_USERHASHBAN)
		pLoadedClient->Ban(pLoadedClient->m_strPunishmentReason, pLoadedClient->m_uBadClientCategory, pLoadedClient->m_uPunishment, pLoadedClient->m_tPunishmentStartTime);
	if (theApp.clientcredits != NULL)
		pLoadedClient->credits = theApp.clientcredits->GetCredit(pLoadedClient->GetUserHash(), false);
	if (!pLoadedClient->m_bAutoQuerySharedFiles && pLoadedClient->m_uSharedFilesStatus == S_NOT_QUERIED && pLoadedClient->GetViewSharedFilesSupport() && pLoadedClient->credits &&
		((thePrefs.GetRemoteSharedFilesSetAutoQueryDownload() && pLoadedClient->credits->GetDownloadedTotal() / 1048576 >= thePrefs.GetRemoteSharedFilesSetAutoQueryDownloadThreshold()) ||
		(thePrefs.GetRemoteSharedFilesSetAutoQueryUpload() && pLoadedClient->credits->GetUploadedTotal() / 1048576 >= thePrefs.GetRemoteSharedFilesSetAutoQueryUploadThreshold())))
		pLoadedClient->m_bAutoQuerySharedFiles = true;
	if (theApp.friendlist != NULL) {
		pLoadedClient->m_Friend = theApp.friendlist->SearchFriend(pLoadedClient->m_achUserHash, pLoadedClient->m_UserIP, pLoadedClient->m_nUserPort);
		if (pLoadedClient->m_Friend != NULL)
			pLoadedClient->m_Friend->SetLinkedClient(pLoadedClient);
		else
			pLoadedClient->SetFriendSlot(false);
	}
	CancelAutoQuerySharedFilesJob();
	m_ArchivedClientsMap.SetAt(strCanonicalHash, pLoadedClient);
}

void CClientList::ApplyPendingClientHistoryRecords(std::vector<CUpDownClient*>& discardedClients)
{
	for (size_t i = 0; i < m_PendingClientHistoryRecords.size(); ++i)
		ApplyClientHistoryRecordToArchive(m_PendingClientHistoryRecords[i], discardedClients);
	m_PendingClientHistoryRecords.clear();
}

void CClientList::ApplyStartupClientHistoryLoad(CStartupClientHistoryRecords* pLoadedRecords)
{
	if (pLoadedRecords == NULL)
		return;

	size_t uNextRecord = 0;
	if (!ApplyStartupClientHistoryLoadChunk(pLoadedRecords, uNextRecord, static_cast<size_t>(-1))) {
		DeleteStartupClientHistoryRecords(pLoadedRecords);
		return;
	}
	CompleteStartupClientHistoryLoadApply();
	DeleteStartupClientHistoryRecords(pLoadedRecords);
}

bool CClientList::ApplyStartupClientHistoryLoadChunk(CStartupClientHistoryRecords* pLoadedRecords, size_t& uNextRecord, size_t uMaxRecords)
{
	if (pLoadedRecords == NULL || uNextRecord > pLoadedRecords->size())
		return false;

	if (!GuardClientListMutation(_T("CClientList::ApplyStartupClientHistoryLoadChunk")))
		return false;

	std::vector<CUpDownClient*> discardedClients;
	const size_t uRemainingRecords = pLoadedRecords->size() - uNextRecord;
	discardedClients.reserve(uRemainingRecords < uMaxRecords ? uRemainingRecords : uMaxRecords);

	const DWORD dwSliceStart = ::GetTickCount();
	size_t uProcessed = 0;
	while (uNextRecord < pLoadedRecords->size() && uProcessed < uMaxRecords) {
		ApplyClientHistoryRecordToArchive((*pLoadedRecords)[uNextRecord], discardedClients);
		++uNextRecord;
		++uProcessed;
		if (uMaxRecords != static_cast<size_t>(-1) && uProcessed != 0 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetPersistenceSave))
			break;
	}

	for (size_t i = 0; i < discardedClients.size(); ++i)
		CUpDownClient::SafeDelete(discardedClients[i]);
	return true;
}

bool CClientList::ApplyStartupClientHistoryPendingRecordsChunk(size_t& uNextRecord, size_t uMaxRecords, UINT& uApplied, INT_PTR& iRemaining)
{
	uApplied = 0;
	iRemaining = 0;
	if (!GuardClientListMutation(_T("CClientList::ApplyStartupClientHistoryPendingRecordsChunk")))
		return false;
	if (uNextRecord > m_PendingClientHistoryRecords.size())
		return false;

	std::vector<CUpDownClient*> discardedClients;
	const size_t uRemainingRecords = m_PendingClientHistoryRecords.size() - uNextRecord;
	discardedClients.reserve(uRemainingRecords < uMaxRecords ? uRemainingRecords : uMaxRecords);

	const DWORD dwSliceStart = ::GetTickCount();
	size_t uProcessed = 0;
	while (uNextRecord < m_PendingClientHistoryRecords.size() && uProcessed < uMaxRecords) {
		ApplyClientHistoryRecordToArchive(m_PendingClientHistoryRecords[uNextRecord], discardedClients);
		++uNextRecord;
		++uProcessed;
		++uApplied;
		if (uMaxRecords != static_cast<size_t>(-1) && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetStartupApply))
			break;
	}

	for (size_t i = 0; i < discardedClients.size(); ++i)
		CUpDownClient::SafeDelete(discardedClients[i]);

	if (uNextRecord < m_PendingClientHistoryRecords.size()) {
		iRemaining = static_cast<INT_PTR>(m_PendingClientHistoryRecords.size() - uNextRecord);
		return true;
	}

	m_PendingClientHistoryRecords.clear();
	return true;
}

void CClientList::RebuildStartupClientHistoryRuntimeMap()
{
	RebuildArchivedRuntimeMap();
}

bool CClientList::RebuildStartupClientHistoryRuntimeMapChunk(std::vector<CUpDownClient*>& aPendingDeleteClients, size_t& uNextPendingDeleteClient, POSITION& posNextArchivedClient, bool& bStarted, UINT& uApplied, INT_PTR& iRemaining)
{
	uApplied = 0;
	iRemaining = 0;
	if (!GuardClientListMutation(_T("CClientList::RebuildStartupClientHistoryRuntimeMapChunk")))
		return false;

	const DWORD dwSliceStart = ::GetTickCount();
	CSingleLock runtimeMapLock(&m_csTrackedClientRuntimeMaps, TRUE);
	if (!bStarted) {
		aPendingDeleteClients.clear();
		aPendingDeleteClients.reserve(m_ArchivedClientsByRuntimeID.size());
		for (auto it = m_ArchivedClientsByRuntimeID.begin(); it != m_ArchivedClientsByRuntimeID.end(); ++it) {
			if (IsPendingDeleteRuntimeMapEntry(it->second))
				aPendingDeleteClients.push_back(it->second);
		}
		m_ArchivedClientsByRuntimeID.clear();
		m_ArchivedClientsByRuntimeID.reserve(static_cast<size_t>(m_ArchivedClientsMap.GetCount()) + aPendingDeleteClients.size() + 16);
		uNextPendingDeleteClient = 0;
		posNextArchivedClient = m_ArchivedClientsMap.GetStartPosition();
		bStarted = true;
	}

	while (uNextPendingDeleteClient < aPendingDeleteClients.size()) {
		CUpDownClient* pPendingDeleteClient = aPendingDeleteClients[uNextPendingDeleteClient++];
		if (pPendingDeleteClient != NULL)
			m_ArchivedClientsByRuntimeID[pPendingDeleteClient->GetRuntimeID()] = pPendingDeleteClient;
		++uApplied;
		if (theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetStartupApply)) {
			iRemaining = 1;
			return true;
		}
	}

	while (posNextArchivedClient != NULL) {
		CString strHash;
		CUpDownClient* pArchivedClient = NULL;
		m_ArchivedClientsMap.GetNextAssoc(posNextArchivedClient, strHash, pArchivedClient);
		if (pArchivedClient != NULL)
			m_ArchivedClientsByRuntimeID[pArchivedClient->GetRuntimeID()] = pArchivedClient;
		++uApplied;
		if (theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetStartupApply)) {
			iRemaining = 1;
			return true;
		}
	}

	aPendingDeleteClients.clear();
	return true;
}

bool CClientList::ApplyStartupClientHistoryActiveLinksChunk(POSITION& posNextClient, size_t uMaxClients, UINT& uApplied, INT_PTR& iRemaining)
{
	uApplied = 0;
	iRemaining = 0;
	if (!GuardClientListMutation(_T("CClientList::ApplyStartupClientHistoryActiveLinksChunk")))
		return false;

	if (posNextClient == NULL)
		posNextClient = list.GetHeadPosition();

	const DWORD dwSliceStart = ::GetTickCount();
	while (posNextClient != NULL && uApplied < uMaxClients) {
		CUpDownClient* pClient = list.GetNext(posNextClient);
		if (pClient == NULL)
			continue;
		if (IsCorruptOrBadUserHash(pClient->GetUserHash())) {
			pClient->ClearArchivedClientLink();
			++uApplied;
			continue;
		}
		CUpDownClient* pArchivedClient = NULL;
		if (m_ArchivedClientsMap.Lookup(md4str(pClient->GetUserHash()), pArchivedClient) && pArchivedClient != NULL)
			pClient->SetArchivedClientLink(pArchivedClient);
		else
			pClient->ClearArchivedClientLink();
		++uApplied;
		if (uMaxClients != static_cast<size_t>(-1) && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetStartupApply))
			break;
	}

	if (posNextClient != NULL) {
		iRemaining = 1;
		return true;
	}

	return true;
}

void CClientList::FinalizeStartupClientHistoryLoadApply()
{
	m_tLastSaved = time(NULL);
}

void CClientList::CompleteStartupClientHistoryLoadApply()
{
	size_t uNextPendingRecord = 0;
	UINT uApplied = 0;
	INT_PTR iRemaining = 0;
	do {
		if (!ApplyStartupClientHistoryPendingRecordsChunk(uNextPendingRecord, static_cast<size_t>(-1), uApplied, iRemaining))
			return;
	} while (iRemaining > 0);
	RebuildArchivedRuntimeMap();
	POSITION posNextClient = list.GetHeadPosition();
	do {
		if (!ApplyStartupClientHistoryActiveLinksChunk(posNextClient, static_cast<size_t>(-1), uApplied, iRemaining))
			return;
	} while (iRemaining > 0);
	FinalizeStartupClientHistoryLoadApply();
}

void CClientList::SaveListSynchronously()
{
	CancelAutoQuerySharedFilesJob();

	if (ShouldSkipClientPersistenceForManualLeakDump())
		return;
	if (!theApp.ClientHistoryReady()) {
		AddDebugLogLine(DLP_LOW, false, _T("Skipping client history save before startup client history load is ready.\n"));
		return;
	}

	if (thePrefs.GetLogFileSaving())
		AddDebugLogLine(false, _T("Saving client history file \"%s\""), CLIENT_HISTORY_MET_FILENAME);
	m_tLastSaved = time(NULL);

	const CString& sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	const LONG lGeneration = ::InterlockedIncrement(&m_lClientHistorySaveGeneration);
	const UINT uClientHistoryGrowBytes = 1024 * 1024;
	CSafeMemFile file(uClientHistoryGrowBytes);

	try {
		file.WriteUInt8(MET_HEADER);

		// Save archive versions for all active clients.
		for (POSITION pos = list.GetHeadPosition(); pos != NULL;)
			theApp.emuledlg->transferwnd->GetClientList()->SaveArchive(list.GetNext(pos));

		// Make sure we have a cleaned up "m_ClientHistoryMap". After this we can set valid count.
		for (POSITION pos = m_ArchivedClientsMap.GetStartPosition(); pos != NULL;) {
			CString cur_hash;
			CUpDownClient* cur_client = NULL;
			POSITION oldpos = pos;
			m_ArchivedClientsMap.GetNextAssoc(pos, cur_hash, cur_client);

			// Unexpected case since we already handle this statuses on other places. But remove from the map if found any.
			if (cur_client == NULL || IsCorruptOrBadUserHash(cur_client->m_achUserHash)) {
				theApp.clientlist->m_ArchivedClientsMap.RemoveKey(cur_hash);
				CUpDownClient::SafeDelete(cur_client);
				pos = oldpos; // If removal is successful restore old position.
			}
		}
		RebuildArchivedRuntimeMap();

		// Write archived clients count to the file.
		uint32 count = m_ArchivedClientsMap.GetCount();
		file.WriteUInt32(count);

		// Write archived clients from "m_ClientHistoryMap" items directly since we already cleaned up this map.
		for (const CArchivedClientsMap::CPair* pair = m_ArchivedClientsMap.PGetFirstAssoc(); pair != NULL; pair = m_ArchivedClientsMap.PGetNextAssoc(pair))
			pair->value->WriteToFile(file);

		AsyncDiskWriteData* pData = new AsyncDiskWriteData;
		pData->lGeneration = lGeneration;
		pData->plGeneration = &m_lClientHistorySaveGeneration;
		pData->strTempPath = sConfDir + CLIENT_HISTORY_MET_FILENAME_TMP;
		pData->strFinalPath = sConfDir + CLIENT_HISTORY_MET_FILENAME;
		pData->strLogName = CLIENT_HISTORY_MET_FILENAME;
		pData->strPayloadName = _T("client-history");
		pData->eConflictPolicy = AsyncDiskWriteConflictLastSnapshotWins;
		pData->eReplacePolicy = AsyncDiskWriteReplaceFinal;
		const ULONGLONG uLength = file.GetLength();
		if (uLength != 0)
			pData->data.assign(file.GetBuffer(), file.GetBuffer() + static_cast<size_t>(uLength));
		CPartFileWriteThread::QueueOrWriteDiskSnapshot(pData);
		if (m_ArchivedClientsMap.GetCount())
			AddDebugLogLine(false, _T("%lu clients saved to client history file"), count);
	} catch (CFileException *ex) {
			LogError(LOG_STATUSBAR, GetResString(_T("FAILED_TO_SAVE_FILE")), CLIENT_HISTORY_MET_FILENAME, (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
		ex->Delete();
	}
}


void CClientList::WriteClientHistoryRecordToFile(CFileDataIO& file, const SClientHistoryRecord& record)
{
	if (IsCorruptOrBadUserHash(record.userHash))
		return;

	file.WriteHash16(record.userHash);
	file.WriteUInt64(record.uLastSeen);

	uint32 uTagCount = 0;
	ULONGLONG uTagCountFilePos = file.GetPosition();
	file.WriteUInt32(0);

	if (!record.strUserName.IsEmpty()) {
		CTag nametag(CL_NAME, record.strUserName);
		nametag.WriteTagToFile(file, UTF8strOptBOM);
		++uTagCount;
	}

	CTag connnectiptag(CL_CONNECTIP, ipstr(record.connectIP));
	connnectiptag.WriteTagToFile(file);
	++uTagCount;

	CTag useriptag(CL_USERIP, ipstr(record.userIP));
	useriptag.WriteTagToFile(file);
	++uTagCount;

	CTag serveriptag(CL_SERVERIP, record.dwServerIP);
	serveriptag.WriteTagToFile(file);
	++uTagCount;

	CTag useridhybridtag(CL_USERIDHYBRID, record.nUserIDHybrid);
	useridhybridtag.WriteTagToFile(file);
	++uTagCount;

	CTag userporttag(CL_USERPORT, record.nUserPort);
	userporttag.WriteTagToFile(file);
	++uTagCount;

	CTag serverporttag(CL_SERVERPORT, record.nServerPort);
	serverporttag.WriteTagToFile(file);
	++uTagCount;

	CTag clientversiontag(CL_CLIENTVERSION, record.nClientVersion);
	clientversiontag.WriteTagToFile(file);
	++uTagCount;

	CTag emuleversiontag(CL_EMULEVERSION, record.byEmuleVersion);
	emuleversiontag.WriteTagToFile(file);
	++uTagCount;

	CTag emuleprotocoltag(CL_EMULEPROTOCOL, record.bEmuleProtocol);
	emuleprotocoltag.WriteTagToFile(file);
	++uTagCount;

	CTag ishybridtag(CL_ISHYBRID, record.bIsHybrid);
	ishybridtag.WriteTagToFile(file);
	++uTagCount;

	CTag isudpporttag(CL_UDPPORT, record.nUDPPort);
	isudpporttag.WriteTagToFile(file);
	++uTagCount;

	CTag iskadporttag(CL_KADPORT, record.nKadPort);
	iskadporttag.WriteTagToFile(file);
	++uTagCount;

	CTag udpvertag(CL_UDPVER, record.byUDPVer);
	udpvertag.WriteTagToFile(file);
	++uTagCount;

	CTag compatibleclienttag(CL_COMPATIBLECLIENT, record.byCompatibleClient);
	compatibleclienttag.WriteTagToFile(file);
	++uTagCount;

	CTag ismltag(CL_ISML, record.bIsML);
	ismltag.WriteTagToFile(file);
	++uTagCount;

	CTag gplevildoertag(CL_GPLEVILDOER, record.bGPLEvildoer);
	gplevildoertag.WriteTagToFile(file);
	++uTagCount;

	if (!record.strClientSoftware.IsEmpty()) {
		CTag clientsoftwaretag(CL_CLIENTSOFTWARE, record.strClientSoftware);
		clientsoftwaretag.WriteTagToFile(file, UTF8strOptBOM);
		++uTagCount;
	}

	if (!record.strModVersion.IsEmpty()) {
		CTag modversiontag(CL_MODVERSION, record.strModVersion);
		modversiontag.WriteTagToFile(file, UTF8strOptBOM);
		++uTagCount;
	}

	CTag messagesreceivedtag(CL_MESSAGESRECEIVED, record.cMessagesReceived);
	messagesreceivedtag.WriteTagToFile(file);
	++uTagCount;

	CTag messagessenttag(CL_MESSAGESSENT, record.cMessagesSent);
	messagessenttag.WriteTagToFile(file);
	++uTagCount;

	CTag kadversiontag(CL_KADVERSION, record.byKadVersion);
	kadversiontag.WriteTagToFile(file);
	++uTagCount;

	CTag noviewsharedfilestag(CL_NOVIEWSHAREDFILES, record.bNoViewSharedFiles);
	noviewsharedfilestag.WriteTagToFile(file);
	++uTagCount;

	CTag firstseentag(CL_FIRSTSEEN, record.uFirstSeen);
	firstseentag.WriteTagToFile(file);
	++uTagCount;

	CTag punishmenttag(CL_PUNISHMENT, record.uPunishment);
	punishmenttag.WriteTagToFile(file);
	++uTagCount;

	CTag badclientcategorytag(CL_BADCLIENTCATEGORY, record.uBadClientCategory);
	badclientcategorytag.WriteTagToFile(file);
	++uTagCount;

	CTag punishedtimetag(CL_PUNISHMENTSTARTIME, record.uPunishmentStartTime);
	punishedtimetag.WriteTagToFile(file);
	++uTagCount;

	if (!record.strPunishmentReason.IsEmpty()) {
		CTag punishmentreasontag(CL_PUNISHMENTREASON, record.strPunishmentReason);
		punishmentreasontag.WriteTagToFile(file, UTF8strOptBOM);
		++uTagCount;
	}

	CTag sharedfilesstatustag(CL_SHAREDFILESSTATUS, record.uSharedFilesStatus);
	sharedfilesstatustag.WriteTagToFile(file);
	++uTagCount;

	CTag sharedfileslastqueriedtimetag(CL_SHAREDFILESLASTQUERIEDTIME, record.uSharedFilesLastQueriedTime);
	sharedfileslastqueriedtimetag.WriteTagToFile(file);
	++uTagCount;

	CTag sharedfilescounttag(CL_SHAREDFILESCOUNT, record.uSharedFilesCount);
	sharedfilescounttag.WriteTagToFile(file);
	++uTagCount;

	if (!record.strClientNote.IsEmpty()) {
		CTag clientnotetag(CL_CLIENTNOTE, record.strClientNote);
		clientnotetag.WriteTagToFile(file, UTF8strOptBOM);
		++uTagCount;
	}

	CTag autoquerysharedfiles(CL_AUTOQUERYSHAREDFILES, record.bAutoQuerySharedFiles);
	autoquerysharedfiles.WriteTagToFile(file);
	++uTagCount;

	file.Seek(uTagCountFilePos, CFile::begin);
	file.WriteUInt32(uTagCount);
	file.Seek(0, CFile::end);
}

bool CClientList::SerializeClientHistorySnapshot(const SClientHistorySaveSnapshot& snapshot)
{
	if (snapshot.plGeneration != NULL && snapshot.lGeneration != InterlockedCompareExchange(snapshot.plGeneration, 0, 0))
		return false;

	const UINT uClientHistoryGrowBytes = 1024 * 1024;
	CSafeMemFile file(uClientHistoryGrowBytes);

	try {
		file.WriteUInt8(MET_HEADER);
		file.WriteUInt32(static_cast<uint32>(snapshot.records.size()));

		for (size_t i = 0; i < snapshot.records.size(); ++i) {
			if ((i & 0x3FF) == 0 && snapshot.plGeneration != NULL && snapshot.lGeneration != InterlockedCompareExchange(snapshot.plGeneration, 0, 0))
				return false;
			WriteClientHistoryRecordToFile(file, snapshot.records[i]);
		}

		AsyncDiskWriteData* pData = new AsyncDiskWriteData;
		pData->lGeneration = snapshot.lGeneration;
		pData->plGeneration = snapshot.plGeneration;
		pData->strTempPath = snapshot.strConfigDir + CLIENT_HISTORY_MET_FILENAME_TMP;
		pData->strFinalPath = snapshot.strConfigDir + CLIENT_HISTORY_MET_FILENAME;
		pData->strLogName = CLIENT_HISTORY_MET_FILENAME;
		pData->strPayloadName = _T("client-history");
		pData->eConflictPolicy = AsyncDiskWriteConflictLastSnapshotWins;
		pData->eReplacePolicy = AsyncDiskWriteReplaceFinal;
		const ULONGLONG uLength = file.GetLength();
		if (uLength != 0)
			pData->data.assign(file.GetBuffer(), file.GetBuffer() + static_cast<size_t>(uLength));
		return CPartFileWriteThread::QueueOrWriteDiskSnapshot(pData);
	} catch (CFileException *ex) {
		theApp.QueueLogLine(false, _T("Failed to save %s: %s"), CLIENT_HISTORY_MET_FILENAME, (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
		ex->Delete();
	} catch (...) {
		theApp.QueueLogLine(false, _T("Failed to save %s"), CLIENT_HISTORY_MET_FILENAME);
	}
	return false;
}

UINT AFX_CDECL CClientList::ClientHistorySaveThreadProc(LPVOID pParam)
{
	DbgSetThreadName("ClientHistorySave");
	SClientHistorySaveSnapshot* pSnapshot = static_cast<SClientHistorySaveSnapshot*>(pParam);
	if (pSnapshot == NULL)
		return 1;
	const bool bResult = SerializeClientHistorySnapshot(*pSnapshot);
	delete pSnapshot;
	return bResult ? 0 : 1;
}

bool CClientList::QueueClientHistorySnapshotSave(SClientHistorySaveSnapshot* pSnapshot)
{
	if (pSnapshot == NULL)
		return false;
	if (theApp.IsClosing())
		return false;
	CWinThread* pThread = AfxBeginThread(ClientHistorySaveThreadProc, pSnapshot, THREAD_PRIORITY_BELOW_NORMAL);
	return pThread != NULL;
}

bool CClientList::StartClientHistorySaveJob()
{
	CancelAutoQuerySharedFilesJob();

	if (ShouldSkipClientPersistenceForManualLeakDump())
		return false;
	if (!theApp.ClientHistoryReady()) {
		AddDebugLogLine(DLP_LOW, false, _T("Skipping client history save before startup client history load is ready.\n"));
		return false;
	}
	if (m_ClientHistorySaveJob.bActive) {
		m_tLastSaved = time(NULL);
		return true;
	}

	if (thePrefs.GetLogFileSaving())
		AddDebugLogLine(false, _T("Queueing client history file \"%s\" save"), CLIENT_HISTORY_MET_FILENAME);
	m_tLastSaved = time(NULL);

	m_ClientHistorySaveJob.Reset();
	m_ClientHistorySaveJob.bActive = true;
	m_ClientHistorySaveJob.eStage = ClientHistorySaveJobActiveClients;
	m_ClientHistorySaveJob.lGeneration = ::InterlockedIncrement(&m_lClientHistorySaveGeneration);
	m_ClientHistorySaveJob.strConfigDir = thePrefs.GetMuleDirectory(EMULE_CONFIGDIR);
	m_ClientHistorySaveJob.activeClients.reserve(static_cast<size_t>(list.GetCount()));
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient* pClient = list.GetNext(pos);
		if (pClient != NULL && pClient->TryAcquireRuntimeReference())
			m_ClientHistorySaveJob.activeClients.push_back(pClient);
	}

	const INT_PTR iArchiveCount = m_ArchivedClientsMap.GetCount();
	if (iArchiveCount > 0) {
		const INT_PTR iReserveCount = iArchiveCount < 4096 ? iArchiveCount : 4096;
		m_ClientHistorySaveJob.records.reserve(static_cast<size_t>(iReserveCount));
	}
	return ProcessClientHistorySaveJob();
}

bool CClientList::ProcessClientHistorySaveJob()
{
	if (!m_ClientHistorySaveJob.bActive)
		return false;

	const DWORD dwSliceStart = ::GetTickCount();
	while (m_ClientHistorySaveJob.bActive) {
		switch (m_ClientHistorySaveJob.eStage) {
		case ClientHistorySaveJobActiveClients:
			while (m_ClientHistorySaveJob.uNextActiveClient < m_ClientHistorySaveJob.activeClients.size()) {
				CUpDownClient* pClient = m_ClientHistorySaveJob.activeClients[m_ClientHistorySaveJob.uNextActiveClient];
				m_ClientHistorySaveJob.activeClients[m_ClientHistorySaveJob.uNextActiveClient] = NULL;
				++m_ClientHistorySaveJob.uNextActiveClient;
				if (pClient != NULL) {
					if (!pClient->m_bIsArchived && !IsCorruptOrBadUserHash(pClient->GetUserHash())) {
						theApp.emuledlg->transferwnd->GetClientList()->SaveArchive(pClient);
						++m_ClientHistorySaveJob.uActiveArchived;
					}
					pClient->ReleaseRuntimeReference();
				}
				if (theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetPersistenceSave))
					return true;
			}
			m_ClientHistorySaveJob.activeClients.clear();
			m_ClientHistorySaveJob.posNextArchivedClient = m_ArchivedClientsMap.GetStartPosition();
			m_ClientHistorySaveJob.eStage = ClientHistorySaveJobArchivedClients;
			break;

		case ClientHistorySaveJobArchivedClients:
			while (m_ClientHistorySaveJob.posNextArchivedClient != NULL) {
				CString strHash;
				CUpDownClient* pArchivedClient = NULL;
				m_ArchivedClientsMap.GetNextAssoc(m_ClientHistorySaveJob.posNextArchivedClient, strHash, pArchivedClient);
				SClientHistoryRecord record;
				if (BuildClientHistoryRecordFromClient(pArchivedClient, record, true)) {
					m_ClientHistorySaveJob.records.push_back(record);
					++m_ClientHistorySaveJob.uRecordsBuilt;
				}
				if (theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetPersistenceSave))
					return true;
			}
			m_ClientHistorySaveJob.eStage = ClientHistorySaveJobQueueWorker;
			break;

		case ClientHistorySaveJobQueueWorker: {
			SClientHistorySaveSnapshot* pSnapshot = new SClientHistorySaveSnapshot;
			pSnapshot->strConfigDir = m_ClientHistorySaveJob.strConfigDir;
			pSnapshot->lGeneration = m_ClientHistorySaveJob.lGeneration;
			pSnapshot->plGeneration = &m_lClientHistorySaveGeneration;
			pSnapshot->records.swap(m_ClientHistorySaveJob.records);
			const size_t uSnapshotRecordCount = pSnapshot->records.size();
			m_ClientHistorySaveJob.Reset();
			if (!QueueClientHistorySnapshotSave(pSnapshot)) {
				SClientHistorySaveSnapshot* pSyncSnapshot = pSnapshot;
				(void)SerializeClientHistorySnapshot(*pSyncSnapshot);
				delete pSyncSnapshot;
			}
			if (uSnapshotRecordCount != 0)
				AddDebugLogLine(false, _T("%Iu clients queued for client history file save"), uSnapshotRecordCount);
			return false;
		}

		case ClientHistorySaveJobNone:
		default:
			m_ClientHistorySaveJob.Reset();
			return false;
		}
	}
	return false;
}

void CClientList::CancelClientHistorySaveJob()
{
	if (m_ClientHistorySaveJob.bActive)
		::InterlockedIncrement(&m_lClientHistorySaveGeneration);
	m_ClientHistorySaveJob.Reset();
}

void CClientList::SaveList()
{
	if (theApp.IsClosing()) {
		CancelClientHistorySaveJob();
		SaveListSynchronously();
		return;
	}
	StartClientHistorySaveJob();
}

void CClientList::ClearAutoQuerySharedFilesActiveSnapshot()
{
	for (size_t i = 0; i < m_AutoQuerySharedFilesJob.activeClients.size(); ++i) {
		if (m_AutoQuerySharedFilesJob.activeClients[i] != NULL) {
			m_AutoQuerySharedFilesJob.activeClients[i]->ReleaseRuntimeReference();
			m_AutoQuerySharedFilesJob.activeClients[i] = NULL;
		}
	}
	m_AutoQuerySharedFilesJob.activeClients.clear();
	m_AutoQuerySharedFilesJob.uNextActiveClient = 0;
}

void CClientList::BeginAutoQuerySharedFilesActiveStage()
{
	ClearAutoQuerySharedFilesActiveSnapshot();
	m_AutoQuerySharedFilesJob.eStage = AutoQuerySharedFilesJobActiveClients;
	m_AutoQuerySharedFilesJob.activeClients.reserve(static_cast<size_t>(list.GetCount()));
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient* pClient = list.GetNext(pos);
		if (pClient != NULL && pClient->TryAcquireRuntimeReference())
			m_AutoQuerySharedFilesJob.activeClients.push_back(pClient);
	}
}

bool CClientList::IsAutoQuerySharedFilesThresholdReached(const CUpDownClient* pClient) const
{
	return pClient != NULL && pClient->credits != NULL
		&& ((thePrefs.GetRemoteSharedFilesSetAutoQueryDownload() && pClient->credits->GetDownloadedTotal() / 1048576 >= thePrefs.GetRemoteSharedFilesSetAutoQueryDownloadThreshold()) ||
			(thePrefs.GetRemoteSharedFilesSetAutoQueryUpload() && pClient->credits->GetUploadedTotal() / 1048576 >= thePrefs.GetRemoteSharedFilesSetAutoQueryUploadThreshold()));
}

void CClientList::AddAutoQuerySharedFilesCandidate(CUpDownClient* pClient)
{
	if (pClient == NULL || m_AutoQuerySharedFilesJob.uMaxQueries == 0)
		return;
	if (std::find(m_AutoQuerySharedFilesJob.candidates.begin(), m_AutoQuerySharedFilesJob.candidates.end(), pClient) != m_AutoQuerySharedFilesJob.candidates.end())
		return;

	if (m_AutoQuerySharedFilesJob.candidates.size() < m_AutoQuerySharedFilesJob.uMaxQueries) {
		if (pClient->TryAcquireRuntimeReference())
			m_AutoQuerySharedFilesJob.candidates.push_back(pClient);
		return;
	}

	size_t uWorstCandidate = 0;
	for (size_t i = 1; i < m_AutoQuerySharedFilesJob.candidates.size(); ++i) {
		if (SortFunc(m_AutoQuerySharedFilesJob.candidates[uWorstCandidate], m_AutoQuerySharedFilesJob.candidates[i]))
			uWorstCandidate = i;
	}

	if (!SortFunc(pClient, m_AutoQuerySharedFilesJob.candidates[uWorstCandidate]))
		return;
	if (!pClient->TryAcquireRuntimeReference())
		return;

	m_AutoQuerySharedFilesJob.candidates[uWorstCandidate]->ReleaseRuntimeReference();
	m_AutoQuerySharedFilesJob.candidates[uWorstCandidate] = pClient;
	m_AutoQuerySharedFilesJob.bCandidatesSorted = false;
}

void CClientList::RemoveAutoQuerySharedFilesCandidate(CUpDownClient* pClient)
{
	if (pClient == NULL || m_AutoQuerySharedFilesJob.candidates.empty())
		return;

	for (std::vector<CUpDownClient*>::iterator it = m_AutoQuerySharedFilesJob.candidates.begin(); it != m_AutoQuerySharedFilesJob.candidates.end(); ++it) {
		if (*it == pClient) {
			(*it)->ReleaseRuntimeReference();
			m_AutoQuerySharedFilesJob.candidates.erase(it);
			return;
		}
	}
}

bool CClientList::StartAutoQuerySharedFilesJob()
{
#ifdef TESTMODE
	const bool bCanRun = thePrefs.GetClientHistory();
#else
	const bool bCanRun = thePrefs.GetClientHistory() && thePrefs.CanSeeShares() == vsfaEverybody;
#endif
	if (!bCanRun)
		return false;

	if (m_ClientHistorySaveJob.bActive) {
		CancelAutoQuerySharedFilesJob();
		return false;
	}

	if (m_AutoQuerySharedFilesJob.bActive)
		return ProcessAutoQuerySharedFilesJob();

	const UINT uMaxQueries = thePrefs.GetRemoteSharedFilesAutoQueryMaxClients();
	if (uMaxQueries == 0)
		return false;

	m_AutoQuerySharedFilesJob.Reset();
	m_AutoQuerySharedFilesJob.bActive = true;
	m_AutoQuerySharedFilesJob.uMaxQueries = uMaxQueries;
	m_AutoQuerySharedFilesJob.tNow = time(NULL);
	m_AutoQuerySharedFilesJob.candidates.reserve(static_cast<size_t>(uMaxQueries));
	if (thePrefs.GetClientHistory() && theApp.ClientHistoryReady()) {
		m_AutoQuerySharedFilesJob.eStage = AutoQuerySharedFilesJobArchivedClients;
		m_AutoQuerySharedFilesJob.posNextArchivedClient = m_ArchivedClientsMap.GetStartPosition();
	}
	else
		BeginAutoQuerySharedFilesActiveStage();
	return ProcessAutoQuerySharedFilesJob();
}

bool CClientList::ProcessAutoQuerySharedFilesJob()
{
	if (!m_AutoQuerySharedFilesJob.bActive)
		return false;
	if (theApp.IsClosing() || m_ClientHistorySaveJob.bActive) {
		m_AutoQuerySharedFilesJob.Reset();
		return false;
	}

	const DWORD dwSliceStart = ::GetTickCount();
	while (m_AutoQuerySharedFilesJob.bActive) {
		switch (m_AutoQuerySharedFilesJob.eStage) {
		case AutoQuerySharedFilesJobArchivedClients:
			while (m_AutoQuerySharedFilesJob.posNextArchivedClient != NULL) {
				CString cur_hash;
				CUpDownClient* cur_client = NULL;
				m_ArchivedClientsMap.GetNextAssoc(m_AutoQuerySharedFilesJob.posNextArchivedClient, cur_hash, cur_client);
				if (cur_client != NULL) {
					if (!cur_client->m_bAutoQuerySharedFiles && cur_client->m_uSharedFilesStatus == S_NOT_QUERIED && cur_client->GetViewSharedFilesSupport() && IsAutoQuerySharedFilesThresholdReached(cur_client))
						cur_client->m_bAutoQuerySharedFiles = true;
					if (cur_client->m_bAutoQuerySharedFiles && (m_AutoQuerySharedFilesJob.tNow - cur_client->m_tSharedFilesLastQueriedTime > thePrefs.GetRemoteSharedFilesAutoQueryClientPeriod() * 60))
						AddAutoQuerySharedFilesCandidate(cur_client);
				}
				if (theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetUploadTimerMaintenance))
					return true;
			}
			BeginAutoQuerySharedFilesActiveStage();
			break;

		case AutoQuerySharedFilesJobActiveClients:
			while (m_AutoQuerySharedFilesJob.uNextActiveClient < m_AutoQuerySharedFilesJob.activeClients.size()) {
				CUpDownClient* cur_client = m_AutoQuerySharedFilesJob.activeClients[m_AutoQuerySharedFilesJob.uNextActiveClient];
				m_AutoQuerySharedFilesJob.activeClients[m_AutoQuerySharedFilesJob.uNextActiveClient] = NULL;
				++m_AutoQuerySharedFilesJob.uNextActiveClient;
				if (cur_client != NULL && !cur_client->m_bQueryingSharedFiles) {
					CUpDownClient* pArchivedClient = AcquireArchivedClientForRuntimeLinkedUse(cur_client);
					if (pArchivedClient != NULL) {
						RemoveAutoQuerySharedFilesCandidate(pArchivedClient);
						pArchivedClient->ReleaseRuntimeReference();
					}

					if (!cur_client->m_bAutoQuerySharedFiles && cur_client->m_uSharedFilesStatus == S_NOT_QUERIED && cur_client->GetViewSharedFilesSupport() && IsAutoQuerySharedFilesThresholdReached(cur_client)) {
						cur_client->m_bAutoQuerySharedFiles = true;
						pArchivedClient = AcquireArchivedClientForRuntimeLinkedUse(cur_client);
						if (pArchivedClient != NULL) {
							pArchivedClient->m_bAutoQuerySharedFiles = false;
							RemoveAutoQuerySharedFilesCandidate(pArchivedClient);
							pArchivedClient->ReleaseRuntimeReference();
						}
					}

					if (cur_client->m_bAutoQuerySharedFiles && (m_AutoQuerySharedFilesJob.tNow - cur_client->m_tSharedFilesLastQueriedTime > thePrefs.GetRemoteSharedFilesAutoQueryClientPeriod() * 60))
						AddAutoQuerySharedFilesCandidate(cur_client);
				}
				if (cur_client != NULL)
					cur_client->ReleaseRuntimeReference();
				if (theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetUploadTimerMaintenance))
					return true;
			}
			m_AutoQuerySharedFilesJob.activeClients.clear();
			m_AutoQuerySharedFilesJob.uNextActiveClient = 0;
			m_AutoQuerySharedFilesJob.eStage = AutoQuerySharedFilesJobQueryCandidates;
			break;

		case AutoQuerySharedFilesJobQueryCandidates:
			if (!m_AutoQuerySharedFilesJob.bCandidatesSorted) {
				CombinedSort(m_AutoQuerySharedFilesJob.candidates.begin(), m_AutoQuerySharedFilesJob.candidates.end(), SortFunc);
				m_AutoQuerySharedFilesJob.bCandidatesSorted = true;
			}
			while (!m_AutoQuerySharedFilesJob.candidates.empty() && m_AutoQuerySharedFilesJob.uQueried < m_AutoQuerySharedFilesJob.uMaxQueries) {
				CUpDownClient* cur_client = m_AutoQuerySharedFilesJob.candidates.front();
				m_AutoQuerySharedFilesJob.candidates.erase(m_AutoQuerySharedFilesJob.candidates.begin());
				if (theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL && theApp.emuledlg->transferwnd->GetClientList() != NULL) {
					CUpDownClient* pNewClient = theApp.emuledlg->transferwnd->GetClientList()->ArchivedToActive(cur_client);
					if (pNewClient != NULL && (cur_client == pNewClient || IsValidClient(pNewClient))) {
						pNewClient->RequestSharedFileList();
						++m_AutoQuerySharedFilesJob.uQueried;
					}
				}
				cur_client->ReleaseRuntimeReference();
				if (theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetUploadTimerMaintenance))
					return true;
			}
			m_AutoQuerySharedFilesJob.Reset();
			return false;

		case AutoQuerySharedFilesJobNone:
		default:
			m_AutoQuerySharedFilesJob.Reset();
			return false;
		}
	}
	return false;
}

void CClientList::CancelAutoQuerySharedFilesJob()
{
	m_AutoQuerySharedFilesJob.Reset();
}

void CClientList::AutoQuerySharedFiles()
{
	StartAutoQuerySharedFilesJob();
}

const bool CClientList::SortFunc(const CUpDownClient* first, const CUpDownClient* second)
{
	if (first == NULL || second == NULL)
		return first != NULL;
	if (!first->m_bIsArchived && second->m_bIsArchived)
		return true;
	if (first->m_bIsArchived && !second->m_bIsArchived)
		return false;

	const int iLastQueriedCompare = CompareUnsigned(first->m_tSharedFilesLastQueriedTime, second->m_tSharedFilesLastQueriedTime);
	if (iLastQueriedCompare != 0)
		return iLastQueriedCompare < 0;
	return CompareUnsigned(first->tLastSeen, second->tLastSeen) > 0;
}

// This feature also contains ideas and code from: Maella, Xman, Spike2, Xanatos
void CClientList::TrigReask(bool bIPv6Change) {
	const DWORD dwCurTick = ::GetTickCount();
	bool lowID = false;
	if ((theApp.serverconnect->IsConnected() && theApp.serverconnect->IsLowID()) ||
		(Kademlia::CKademlia::IsConnected() && Kademlia::CKademlia::IsFirewalled())) // we will not inform but do a complete reask, if we are on LowID
		lowID = true;

	AddLogLine(false, GetResString(thePrefs.IsRASAIC() && thePrefs.IsIQCAOC() ? _T("ALL_SOURCES_WILL_BE_REASKED_QUEUED_WILL_BE_INFORMED") :
		thePrefs.IsRASAIC() ? _T("ALL_SOURCES_WILL_BE_REASKED") : _T("QUEUED_WILL_BE_INFORMED")), bIPv6Change ? _T("IPv6") : _T("IPv4"));

	for (POSITION pos = list.GetHeadPosition(); pos;) {
		CUpDownClient* cur_client = list.GetNext(pos);

		if (!cur_client || cur_client->m_bOpenIPv6 != bIPv6Change) 
			continue; // Client has IPv4 and our IPv6 changed OR client has IPv6 and our IPv4 changed

		DWORD ReAskTime = cur_client->GetSpreadReAskTime() + thePrefs.GetReAskTimeDif();
		CKnownFile* currequpfile = theApp.sharedfiles->GetFileByID(cur_client->GetUploadFileID());
		
		if (thePrefs.IsRASAIC() && cur_client->GetRequestFile()) { // we request something
			// early abort if reask soon!
			if (cur_client->GetTimeUntilReask() <= MIN2MS(2))
				continue;
			/*
			** Let's add some pseudo code here:
			** I'm setting the last asked time to a value that the client will be reasked
			** real soon
			**
			** a = time now
			** b = ReAsk time
			** c = time till MIN_REQUESTTIME
			**
			** SetLastAskedTime(a-(b-c))
			**
			** The result should be that the client will be reasked either immediatly
			** or as soon as the MIN_REQUESTTIME expired.
			*/
			if ((cur_client->GetClientSoft() == SO_EMULE || cur_client->GetClientSoft() == SO_EDONKEY || cur_client->GetClientSoft() == SO_EDONKEYHYBRID) && !lowID) {
				cur_client->DoSendIP();
				cur_client->SendIPChange();
			} else
				cur_client->SetLastAskedTime(cur_client->GetRequestFile(), dwCurTick - (ReAskTime - cur_client->GetTimeUntilReask(cur_client->GetRequestFile(), true, false, false)));
		} else if ((thePrefs.IsIQCAOC() && currequpfile) && // he requests something
					((cur_client->GetClientSoft() == SO_EMULE || cur_client->GetClientSoft() == SO_EDONKEY || cur_client->GetClientSoft() == SO_EDONKEYHYBRID) && !lowID)) {
			cur_client->DoSendIP();
			cur_client->SendIPChange();
		}
	}

	return;
}

void CClientList::ServiceNatTraversalRetries()
{
	if (!thePrefs.IsEnableNatTraversal())
		return;
	DWORD now = ::GetTickCount();
	DWORD jmin = thePrefs.GetNatTraversalJitterMinMs();
	DWORD jmax = thePrefs.GetNatTraversalJitterMaxMs();
	POSITION pos = list.GetHeadPosition();
	while (pos != NULL) {
		CUpDownClient* c = list.GetNext(pos);
		if (!c)
			continue;
		if (c->IsNatTRetryDue(now)) {
			c->DoNatTRetry();
			if (c->IsNatTRetryDue(now))
				c->ScheduleNextNatTRetry(now, jmin, jmax);
		}
	}
}


void CClientList::ServiceUtpConnectionTimeouts()
{
	// Monitor uTP handshakes for debugging - don't force connections on timeout
	// Let uTP layer handle connection establishment naturally through its callbacks
	const DWORD now = ::GetTickCount();
	const DWORD utpTimeoutMs = 5000; // Monitor threshold (increased to 5 seconds)
	const DWORD orphanConnectTimeoutMs = SEC2MS(45);
	
	POSITION pos = list.GetHeadPosition();
	while (pos) {
		CUpDownClient* pClient = list.GetNext(pos);
		if (pClient == NULL)
			continue;
		if (pClient->socket && pClient->socket->HaveNatTraversalLayer() &&
			pClient->GetUtpConnectionStartTick() != 0) {
			
			// Check if uTP handshake is taking unusually long (for logging only)
			DWORD elapsed = now - pClient->GetUtpConnectionStartTick();
			if ((int)elapsed >= (int)utpTimeoutMs && !pClient->socket->HaveQuicNatLayer()) {
				if (thePrefs.GetLogNatTraversalEvents()) {
					DebugLog(_T("[NatTraversal] NAT-T handshake in progress for %lu ms (monitoring), %s"), 
						elapsed, (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
				}
				
				// Don't force - let uTP layer handle connection naturally
				// Socket will trigger OnReceive/OnSend when connection is truly established
				pClient->ResetUtpConnectionStartTick(); // Reset to avoid spam logging
			}
		}
		const EDownloadState eDlState = pClient->GetDownloadState();
		// Treat only live NAT-T handshake/connected paths as active recovery.
		// Pending retry budget alone is not enough and may keep sources in pseudo-connecting forever.
		const bool bHasActiveNatTraversalRecovery =
			(pClient->socket && pClient->socket->HaveNatTraversalLayer()
				&& (pClient->socket->IsConnected() || pClient->IsHelloAnswerPending()));
		const bool bExhaustedEServerRelay =
			pClient->IsEServerRelayNatTGuardActive()
			&& !pClient->HasPendingNatTRetry()
			&& pClient->GetConnectingState() == CCS_SERVERCALLBACK
			&& pClient->socket == NULL
			&& !bHasActiveNatTraversalRecovery
			&& (eDlState == DS_CONNECTING || eDlState == DS_WAITCALLBACK)
			&& (int)(now - pClient->GetLastTriedToConnectTime()) >= (int)orphanConnectTimeoutMs;
		if (bExhaustedEServerRelay) {
			pClient->AbortEServerRelayNatTraversal(_T("Relay retry budget exhausted"));
			continue;
		}

		const bool bOrphanConnectingState =
			(eDlState == DS_CONNECTING || eDlState == DS_WAITCALLBACK || eDlState == DS_WAITCALLBACKKAD)
			&& pClient->GetConnectingState() == CCS_NONE
			&& pClient->socket == NULL
			&& !bHasActiveNatTraversalRecovery
			&& (int)(now - pClient->GetLastTriedToConnectTime()) >= (int)orphanConnectTimeoutMs;
		if (bOrphanConnectingState) {
			if (pClient->Disconnected(_T("Connecting lifecycle timeout"))) {
				CUpDownClient::SafeDelete(pClient);
				continue;
			}
		}
		// While servicing timeouts, also handle Hello resend fallback for NAT-T connections
		if (pClient->socket && pClient->socket->HaveNatTraversalLayer()) {
			pClient->ResendHelloIfTimeout();
		}
	}
}

void CClientList::ServiceUtpQueuedPackets()
{
	// Service queued packets for NAT-T connections - flush queue when socket is write-ready
	// Called from UploadQueue::UploadTimer every 100ms
	// Each client connection is handled independently based on socket state
	
	POSITION pos = list.GetHeadPosition();
	while (pos) {
		CUpDownClient* pClient = list.GetNext(pos);
		if (!pClient || !pClient->socket || !pClient->socket->HaveNatTraversalLayer())
			continue;
			
		if (!pClient->socket->IsConnected())
			continue;
			
			// Handle deferred adoption markers.
			// Do not purge here: adoption can happen right before we queue mandatory OP_HELLO/OP_HELLOANSWER/file request packets.
			// Dropping the queue at this point can stall LowID-to-LowID startup.
			if (pClient->NeedsUtpQueuePurge()) {
				if (pClient->HasQueuedUtpPackets() && thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal] ServiceUtpQueuedPackets: Preserving %d queued packets after inbound uTP adoption, %s"),
						pClient->m_WaitingPackets_list.GetCount(), (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
				pClient->ClearUtpQueuePurgeRequest();
			}

		if (!pClient->HasQueuedUtpPackets())
			continue;
			
		// Only flush if we have queued packets waiting
		if (pClient->m_dwUtpQueuedPacketsTime == 0)
			continue;
		
		// Check if socket is in connected state - this is more reliable than timestamp
		// For NAT-T, IsConnected() returns true only when connection is fully established
		// and socket is ready for data transfer
		const DWORD now = ::GetTickCount();
		const DWORD elapsed = now - pClient->m_dwUtpQueuedPacketsTime;

		// Avoid flushing non-handshake packets until Hello/HelloAnswer exchange completes,
		// but do not block indefinitely. After a short grace window, allow flush to prevent stalls.
		if (pClient->IsHelloAnswerPending() && !pClient->IsUtpHelloQueued()) {
			if ((int)elapsed < 250) {
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal] ServiceUtpQueuedPackets: Pending HelloAnswer, deferring flush (elapsed %lu ms), %s"),
						elapsed, (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
				continue;
			}
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] ServiceUtpQueuedPackets: Grace window passed (%lu ms) while waiting HelloAnswer; flushing to avoid stall, %s"),
					elapsed, (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
		}
		
		// Minimum safety delay: allow at least one UploadTimer cycle (100ms)
		// This ensures socket has completed state transition from CONNECT to WRITABLE
		// Without this, we might try to send before NAT-T transport buffers are ready
		if ((int)elapsed < 100) {
			continue; // Wait for next cycle
		}
		
		if (!pClient->IsUtpWritable()) {
			const EUtpFrameType lastFrame = pClient->GetUtpLastFrameType();
			const bool hasInbound = pClient->HasUtpInboundActivity();
			if (!hasInbound || lastFrame == UTP_FRAMETYPE_SYN) {
				if ((int)elapsed < NAT_TRAVERSAL_WRITABLE_WARN_MS)
					continue;
				DWORD handshakeElapsed = 0;
				if (pClient->GetUtpConnectionStartTick() != 0 && now >= pClient->GetUtpConnectionStartTick())
					handshakeElapsed = now - pClient->GetUtpConnectionStartTick();
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal] ServiceUtpQueuedPackets: Fallback unlocking uTP queue after %lu ms (handshake %lu ms, inbound=%d lastFrame=%u), %s"),
						elapsed, handshakeElapsed, hasInbound ? 1 : 0, (unsigned)lastFrame, (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
			} else {
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[NatTraversal] ServiceUtpQueuedPackets: Unlocking uTP queue after inbound activity (frame=%u, elapsed %lu ms), %s"),
						(unsigned)lastFrame, elapsed, (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
			}
			pClient->SetUtpWritable(true);
		}
		
		// Socket is connected and minimum delay passed - flush queued packets
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] ServiceUtpQueuedPackets: Flushing %d queued packets (elapsed %lu ms), %s"), 
				pClient->m_WaitingPackets_list.GetCount(), elapsed, (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
		
		pClient->m_dwUtpQueuedPacketsTime = 0; // Reset - queue will be flushed now
		
		while (!pClient->m_WaitingPackets_list.IsEmpty()) {
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugSend("Buffered Packet (deferred)", pClient);
			Packet* packet = pClient->m_WaitingPackets_list.RemoveHead();
			pClient->SendPacket(packet);
			// Pump uTP after each packet to ensure deferred ACKs and retransmissions are sent immediately
			if (theApp.clientudp)
				theApp.clientudp->PumpUtpOnce();
		}

		pClient->SetUtpHelloQueued(false);
		pClient->ClearUtpQueuePurgeRequest();
	}
}

///////////////////////////////////////////////////////////////////////////////
// eServer Buddy functions (LowID relay support)
//

void CClientList::ResetEServerBuddyMagicSearchState()
{
	m_lstEServerBuddyMagicCandidates.clear();
	m_lstEServerBuddyMagicProbes.clear();
	m_dwLastEServerBuddyMagicSearch = 0;
	m_dwLastEServerBuddyMagicSearchSent = 0;
	m_dwLastEServerBuddyMagicSearchServerIP = 0;
	m_nLastEServerBuddyMagicSearchServerPort = 0;
	md4clr(m_abyEServerBuddyMagicInFlightHash);
	m_uEServerBuddyMagicInFlightSize = 0;
	m_uEServerBuddyMagicRoundSalt = 0;
	m_bEServerBuddyMagicSearchInFlight = false;
	m_bEServerBuddyMagicSearchPrimed = false;
	m_bEServerBuddyMagicInFlightProbeValid = false;
}

void CClientList::PrepareEServerBuddyMagicProbeRound()
{
	m_lstEServerBuddyMagicProbes.clear();

	if (theApp.serverconnect == NULL || !theApp.serverconnect->IsConnected() || !theApp.serverconnect->IsLowID())
		return;

	const uint32 uEpoch = CUpDownClient::GetEServerBuddyMagicEpoch();
	const uchar* pLocalHash = thePrefs.GetUserHash();
	const uint32 uRoundSalt = ++m_uEServerBuddyMagicRoundSalt;

	auto AddProbe = [this](bool bBootstrap, uint32 uBucket, uint32 uProbeEpoch) {
		EServerBuddyMagicProbe probe = {};
		probe.uEpoch = uProbeEpoch;
		probe.uBucket = uBucket;
		probe.byFlags = bBootstrap ? ESERVER_BUDDY_MAGIC_FLAG_BOOTSTRAP : 0;
		CString strMagicName;
		CUpDownClient::BuildEServerBuddyMagicBucketInfo(bBootstrap, uBucket, uProbeEpoch, probe.aucHash, probe.uSize, strMagicName);
		for (const auto& queuedProbe : m_lstEServerBuddyMagicProbes) {
			if (md4equ(queuedProbe.aucHash, probe.aucHash))
				return;
		}
		m_lstEServerBuddyMagicProbes.push_back(probe);
	};

	const uint32 uFineBucket = CUpDownClient::SelectEServerBuddyMagicBucket(false, uEpoch, pLocalHash, uRoundSalt);
	AddProbe(false, uFineBucket, uEpoch);

	if (ESERVERBUDDY_MAGIC_MAX_PROBES_PER_ROUND > 1) {
		const uint32 uPrevEpoch = (uEpoch > 0) ? (uEpoch - 1) : 0;
		const uint32 uPrevFineBucket = CUpDownClient::SelectEServerBuddyMagicBucket(false, uPrevEpoch, pLocalHash, uRoundSalt + 1);
		AddProbe(false, uPrevFineBucket, uPrevEpoch);
	}

	if (ESERVERBUDDY_MAGIC_MAX_PROBES_PER_ROUND > 2 && ESERVERBUDDY_MAGIC_BOOTSTRAP_BUCKET_COUNT > 0) {
		const uint32 uBootstrapBucket = uEpoch % ESERVERBUDDY_MAGIC_BOOTSTRAP_BUCKET_COUNT;
		AddProbe(true, uBootstrapBucket, uEpoch);
	}
}

bool CClientList::TrySendNextEServerBuddyMagicProbe()
{
	if (m_bEServerBuddyMagicSearchInFlight || m_lstEServerBuddyMagicProbes.empty())
		return false;

	if (theApp.serverconnect == NULL || !theApp.serverconnect->IsConnected() || !theApp.serverconnect->IsLowID())
		return false;

	CServer* pCurrentServer = theApp.serverconnect->GetCurrentServer();
	if (pCurrentServer == NULL)
		return false;

	EServerBuddyMagicProbe probe = m_lstEServerBuddyMagicProbes.front();
	m_lstEServerBuddyMagicProbes.pop_front();

	CSafeMemFile data(MDX_DIGEST_SIZE + sizeof(uint32) + sizeof(uint64));
	data.WriteHash16(probe.aucHash);
	if (probe.uSize <= 0xFFFFFFFFui64) {
		data.WriteUInt32(static_cast<uint32>(probe.uSize));
	} else {
		data.WriteUInt32(0);
		data.WriteUInt64(probe.uSize);
	}

	uint8 byOpcode = OP_GETSOURCES;
	if (thePrefs.IsCryptLayerEnabled() && pCurrentServer->SupportsGetSourcesObfuscation())
		byOpcode = OP_GETSOURCES_OBFU;

	Packet* packet = new Packet(data, OP_EDONKEYPROT, byOpcode);
	if (!theApp.serverconnect->SendPacket(packet)) {
		m_lstEServerBuddyMagicProbes.push_front(probe);
		return false;
	}

	m_dwLastEServerBuddyMagicSearchSent = ::GetTickCount();
	m_bEServerBuddyMagicSearchInFlight = true;
	m_bEServerBuddyMagicInFlightProbeValid = true;
	m_uEServerBuddyMagicInFlightSize = probe.uSize;
	md4cpy(m_abyEServerBuddyMagicInFlightHash, probe.aucHash);

	if (thePrefs.GetLogNatTraversalEvents()) {
		DebugLog(_T("[eServerBuddy] Magic source probe sent: hash=%s epoch=%u bucket=%u bootstrap=%u pending=%Iu"),
			(LPCTSTR)md4str(probe.aucHash),
			probe.uEpoch,
			probe.uBucket,
			(probe.byFlags & ESERVER_BUDDY_MAGIC_FLAG_BOOTSTRAP) != 0 ? 1u : 0u,
			m_lstEServerBuddyMagicProbes.size());
	}

	return true;
}

void CClientList::StartEServerBuddyMagicSearch()
{
	if (theApp.serverconnect == NULL || !theApp.serverconnect->IsConnected() || !theApp.serverconnect->IsLowID())
		return;

	if (m_bEServerBuddyMagicSearchInFlight)
		return;

	CServer* pCurrentServer = theApp.serverconnect->GetCurrentServer();
	if (pCurrentServer == NULL)
		return;

	const DWORD dwNow = ::GetTickCount();
	const uint32 dwCurrentServerIP = pCurrentServer->GetIP();
	const uint16 nCurrentServerPort = pCurrentServer->GetPort();
	const bool bSameServerAsLastSearch = (m_dwLastEServerBuddyMagicSearchServerIP == dwCurrentServerIP
		&& m_nLastEServerBuddyMagicSearchServerPort == nCurrentServerPort);
	if (m_lstEServerBuddyMagicProbes.empty()) {
		if (bSameServerAsLastSearch
			&& m_dwLastEServerBuddyMagicSearch != 0
			&& (DWORD)(dwNow - m_dwLastEServerBuddyMagicSearch) < ESERVERBUDDY_MAGIC_SEARCH_REASK_TIME)
		{
			return;
		}

		PrepareEServerBuddyMagicProbeRound();
		if (m_lstEServerBuddyMagicProbes.empty())
			return;

		m_dwLastEServerBuddyMagicSearch = dwNow;
		m_dwLastEServerBuddyMagicSearchServerIP = dwCurrentServerIP;
		m_nLastEServerBuddyMagicSearchServerPort = nCurrentServerPort;
		m_bEServerBuddyMagicSearchPrimed = true;

		AddLogLine(false, GetResString(_T("ESERVER_BUDDY_SEARCH_STARTED")));
	}

	TrySendNextEServerBuddyMagicProbe();
}

bool CClientList::ProcessEServerBuddyMagicSourceAnswer(const uchar* pHash, CSafeMemFile& sources, bool bWithObfuscationAndHash, uint32 dwServerIP, uint16 nServerPort)
{
	if (pHash == NULL)
		return false;

	bool bKnownMagicHash = (m_bEServerBuddyMagicInFlightProbeValid && md4equ(pHash, m_abyEServerBuddyMagicInFlightHash));
	if (!bKnownMagicHash) {
		for (const auto& queuedProbe : m_lstEServerBuddyMagicProbes) {
			if (md4equ(queuedProbe.aucHash, pHash)) {
				bKnownMagicHash = true;
				break;
			}
		}
	}
	if (!bKnownMagicHash)
		return false;

	if (theApp.serverconnect == NULL || !theApp.serverconnect->IsConnected() || !theApp.serverconnect->IsLowID())
		return true;

	CServer* pCurrentServer = theApp.serverconnect->GetCurrentServer();
	if (pCurrentServer == NULL || pCurrentServer->GetIP() != dwServerIP || pCurrentServer->GetPort() != nServerPort)
		return true;

	m_bEServerBuddyMagicSearchInFlight = false;
	m_dwLastEServerBuddyMagicSearchSent = 0;
	m_bEServerBuddyMagicSearchPrimed = true;
	m_bEServerBuddyMagicInFlightProbeValid = false;
	m_uEServerBuddyMagicInFlightSize = 0;
	md4clr(m_abyEServerBuddyMagicInFlightHash);

	for (auto it = m_lstEServerBuddyMagicProbes.begin(); it != m_lstEServerBuddyMagicProbes.end();) {
		if (md4equ(it->aucHash, pHash))
			it = m_lstEServerBuddyMagicProbes.erase(it);
		else
			++it;
	}

	const ULONGLONG uRemaining = sources.GetLength() - sources.GetPosition();
	if (uRemaining < 1)
		return true;

	const uint8 nCount = sources.ReadUInt8();
	std::vector<EServerBuddyMagicCandidate> candidates;
	candidates.reserve(nCount);

	for (uint8 i = 0; i < nCount; ++i) {
		if ((sources.GetLength() - sources.GetPosition()) < 6)
			break;

		const uint32 dwUserID = sources.ReadUInt32();
		const uint16 nUserPort = sources.ReadUInt16();

		if (bWithObfuscationAndHash) {
			if ((sources.GetLength() - sources.GetPosition()) < 1)
				break;
			const uint8 byCryptOptions = sources.ReadUInt8();
			if ((byCryptOptions & 0x80) != 0) {
				if ((sources.GetLength() - sources.GetPosition()) < MDX_DIGEST_SIZE)
					break;
				uchar achUserHash[MDX_DIGEST_SIZE];
				sources.ReadHash16(achUserHash);
			}
		}

		if (::IsLowID(dwUserID) || nUserPort == 0 || !IsGoodIP(dwUserID))
			continue;

		bool bDuplicate = false;
		for (const auto& candidate : candidates) {
			if (candidate.dwUserID == dwUserID && candidate.nUserPort == nUserPort) {
				bDuplicate = true;
				break;
			}
		}
		if (!bDuplicate) {
			for (const auto& existingCandidate : m_lstEServerBuddyMagicCandidates) {
				if (existingCandidate.dwUserID == dwUserID && existingCandidate.nUserPort == nUserPort) {
					bDuplicate = true;
					break;
				}
			}
		}
		if (bDuplicate)
			continue;

		EServerBuddyMagicCandidate candidate = {};
		candidate.dwUserID = dwUserID;
		candidate.nUserPort = nUserPort;
		candidate.dwNextTry = 0;
		candidate.byAttempts = 0;
		candidates.push_back(candidate);
	}

	for (size_t i = candidates.size(); i > 1; --i) {
		const size_t j = GetRandomUInt32() % i;
		if (j != (i - 1))
			std::swap(candidates[i - 1], candidates[j]);
	}

	for (const auto& candidate : candidates)
		m_lstEServerBuddyMagicCandidates.push_back(candidate);

	if (thePrefs.GetLogNatTraversalEvents()) {
		DebugLog(_T("[eServerBuddy] Magic source answer processed: server=%s:%u count=%u usable=%Iu pendingProbes=%Iu"),
			(LPCTSTR)ipstr(dwServerIP), nServerPort, (unsigned)nCount, m_lstEServerBuddyMagicCandidates.size(), m_lstEServerBuddyMagicProbes.size());
	}
	return true;
}

bool CClientList::TryRequestEServerBuddyFromMagicCandidates()
{
	if (m_lstEServerBuddyMagicCandidates.empty())
		return false;

	if (theApp.serverconnect == NULL || !theApp.serverconnect->IsConnected() || !theApp.serverconnect->IsLowID())
		return false;

	CServer* pCurrentServer = theApp.serverconnect->GetCurrentServer();
	if (pCurrentServer == NULL)
		return false;

	const DWORD dwNow = ::GetTickCount();
	const uint32 dwServerIP = pCurrentServer->GetIP();
	const uint16 nServerPort = pCurrentServer->GetPort();
	const size_t uPassLimit = m_lstEServerBuddyMagicCandidates.size();

	for (size_t i = 0; i < uPassLimit && !m_lstEServerBuddyMagicCandidates.empty(); ++i) {
		EServerBuddyMagicCandidate candidate = m_lstEServerBuddyMagicCandidates.front();
		m_lstEServerBuddyMagicCandidates.pop_front();

		if (candidate.dwNextTry != 0 && dwNow < candidate.dwNextTry) {
			m_lstEServerBuddyMagicCandidates.push_back(candidate);
			continue;
		}

		CAddress addr(candidate.dwUserID, false);
		CUpDownClient* pCandidate = FindClientByIP(addr, candidate.nUserPort);
		if (pCandidate == NULL)
			pCandidate = FindClientByServerID(dwServerIP, candidate.dwUserID);
		if (pCandidate == NULL) {
			pCandidate = new CUpDownClient(NULL, candidate.nUserPort, candidate.dwUserID, dwServerIP, nServerPort, true);
			AddClient(pCandidate);
		}

		RefreshEServerMagicCandidateServerContext(pCandidate, candidate.dwUserID, candidate.nUserPort, dwServerIP, nServerPort);

		if (pCandidate->HasLowID())
			continue;

		if (!IsEServerBuddyCandidateOnCurrentServer(pCandidate))
			continue;

		const DWORD dwRetryAfter = pCandidate->GetEServerBuddyRetryAfter();
		if (dwRetryAfter != 0 && dwNow < dwRetryAfter) {
			candidate.dwNextTry = dwRetryAfter;
			m_lstEServerBuddyMagicCandidates.push_back(candidate);
			continue;
		}

			if (pCandidate->socket == NULL || !pCandidate->socket->IsConnected()) {
				// Magic candidates are server-discovered helper peers. Do not let a stale explicit bind prevent the hello probe.
				pCandidate->SetAllowAnyBindOnNextServerCallbackConnect(true);
				pCandidate->TryToConnect(true, true);
				++candidate.byAttempts;
				if (candidate.byAttempts < ESERVERBUDDY_MAGIC_MAX_CANDIDATE_RETRIES) {
					candidate.dwNextTry = dwNow + ESERVERBUDDY_KEEPALIVE_TIME;
					m_lstEServerBuddyMagicCandidates.push_back(candidate);
				}
				continue;
			}

			if (!pCandidate->SupportsEServerBuddyMagicProof()) {
				if (pCandidate->GetInfoPacketsReceived() != IP_BOTH) {
					// Hello handshake is not complete yet, so proof capability is still unknown.
					++candidate.byAttempts;
					if (candidate.byAttempts < ESERVERBUDDY_MAGIC_MAX_CANDIDATE_RETRIES) {
						candidate.dwNextTry = dwNow + ESERVERBUDDY_KEEPALIVE_TIME;
						m_lstEServerBuddyMagicCandidates.push_back(candidate);
					}

					if (thePrefs.GetLogNatTraversalEvents()) {
						DebugLog(_T("[eServerBuddy] Magic candidate pending handshake, delaying proof decision: %s (attempt=%u/%u)"),
							(LPCTSTR)EscPercent(pCandidate->DbgGetClientInfo()),
							(UINT)candidate.byAttempts,
							(UINT)ESERVERBUDDY_MAGIC_MAX_CANDIDATE_RETRIES);
					}
					continue;
				}

				pCandidate->OnEServerBuddyProofRejected();
				if (thePrefs.GetLogNatTraversalEvents()) {
					DebugLog(_T("[eServerBuddy] Magic candidate skipped for 1 day (Old client with no proof support): %s"),
						(LPCTSTR)EscPercent(pCandidate->DbgGetClientInfo()));
				}
				continue;
			}

			if (!pCandidate->HasEServerBuddySlot() && thePrefs.GetLogNatTraversalEvents()) {
				DebugLog(_T("[eServerBuddy] Magic candidate has stale cached no-slot flag, probing anyway: %s"),
					(LPCTSTR)EscPercent(pCandidate->DbgGetClientInfo()));
			}

			if (pCandidate->SendEServerBuddyRequest(true)) {
				SetServingEServerBuddy(pCandidate, Connecting);
					AddLogLine(true, GetResString(_T("ESERVER_BUDDY_REQUEST_SENT_TO")), (LPCTSTR)EscPercent(pCandidate->DbgGetClientInfo()));
			return true;
		}

		++candidate.byAttempts;
		if (candidate.byAttempts >= ESERVERBUDDY_MAGIC_MAX_CANDIDATE_RETRIES)
			continue;

		if (!pCandidate->HasEServerBuddySlot())
			candidate.dwNextTry = dwNow + ESERVERBUDDY_REASK_TIME;
		else
			candidate.dwNextTry = dwNow + ESERVERBUDDY_KEEPALIVE_TIME;
		m_lstEServerBuddyMagicCandidates.push_back(candidate);
	}

	return false;
}

// Find a suitable HighID client on the same server to act as our buddy
CUpDownClient* CClientList::FindEServerBuddy()
{
	// Must be connected to a server and have LowID
	if (!theApp.serverconnect || !theApp.serverconnect->IsConnected()) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[eServerBuddy] FindEServerBuddy: Not connected to server"));
		return NULL;
	}

	if (!theApp.serverconnect->IsLowID()) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[eServerBuddy] FindEServerBuddy: We have HighID, no buddy needed"));
		return NULL;	// We're HighID, no buddy needed
	}

	uint32 dwOurServerIP = theApp.serverconnect->GetCurrentServer()->GetIP();
	INT_PTR nTotalClients = list.GetCount();
	int nChecked = 0, nNoSupport = 0, nNoProof = 0, nNoSlot = 0, nWrongServer = 0, nLowID = 0, nRetryWait = 0, nNoSocket = 0;

	CUpDownClient* pBestCandidate = NULL;

	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient* pClient = list.GetNext(pos);
		++nChecked;

		// Detailed diagnostics for each rejection reason
		if (!pClient->SupportsEServerBuddy()) {
			++nNoSupport;
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[eServerBuddy] Candidate rejected: protocol unsupported, %s"), (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
			continue;
		}
		if (!pClient->SupportsEServerBuddyMagicProof()) {
			++nNoProof;
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[eServerBuddy] Candidate rejected: magic proof unavailable, %s"), (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
			if (pClient->GetInfoPacketsReceived() == IP_BOTH)
				pClient->OnEServerBuddyProofRejected();
			continue;
		}
		if (!pClient->HasEServerBuddySlot()) {
			++nNoSlot;
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[eServerBuddy] Candidate rejected: no serving slot, %s"), (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
			continue;
		}
		if (pClient->HasLowID()) {
			++nLowID;
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[eServerBuddy] Candidate rejected: LowID peer, %s"), (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
			continue;
		}
		if (!IsEServerBuddyCandidateOnCurrentServer(pClient)) {
			++nWrongServer;
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[eServerBuddy] Candidate rejected: different server, %s"), (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
			continue;
		}
		if (::GetTickCount() < pClient->GetEServerBuddyRetryAfter()) {
			++nRetryWait;
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[eServerBuddy] Candidate rejected: retry cooldown active, %s"), (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
			continue;
		}
		if (!pClient->socket || !pClient->socket->IsConnected()) {
			++nNoSocket;
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[eServerBuddy] Candidate rejected: no connected socket, %s"), (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
			continue;
		}

		// Found a candidate
		pBestCandidate = pClient;
		break;	// Take first valid candidate
	}

	if (thePrefs.GetLogNatTraversalEvents()) {
		if (pBestCandidate) {
			DebugLog(_T("[eServerBuddy] FindEServerBuddy: Found candidate %s on server %s"),
				(LPCTSTR)EscPercent(pBestCandidate->DbgGetClientInfo()), (LPCTSTR)ipstr(dwOurServerIP));
		} else {
			DebugLog(_T("[eServerBuddy] FindEServerBuddy: No candidate found. Total=%d Checked=%d NoSupport=%d NoProof=%d NoSlot=%d LowID=%d WrongServer=%d RetryWait=%d NoSocket=%d OurServer=%s"),
				(int)nTotalClients, nChecked, nNoSupport, nNoProof, nNoSlot, nLowID, nWrongServer, nRetryWait, nNoSocket, (LPCTSTR)ipstr(dwOurServerIP));
		}
	}

	return pBestCandidate;
}

// Set our serving buddy (the HighID client helping us receive callbacks)
void CClientList::SetServingEServerBuddy(CUpDownClient* pBuddy, uint8 nStatus)
{
	CUpDownClient* pOldBuddy = m_pServingEServerBuddy;
	const uint8 nOldStatus = m_nEServerBuddyStatus;
	const bool bBuddyChanged = (pOldBuddy != pBuddy) || (nOldStatus != nStatus);

	if (nOldStatus == Connected && (nStatus != Connected || pBuddy == NULL) && pOldBuddy != NULL)
		AddLogLine(true, GetResString(_T("ESERVER_BUDDY_DISCONNECTED")), (LPCTSTR)EscPercent(pOldBuddy->DbgGetClientInfo()));
	if (nOldStatus == Connected && nStatus != Connected)
		ResetEServerBuddyMagicSearchState();
	if (pOldBuddy != pBuddy || pBuddy == NULL || nStatus == Disconnected)
		thePrefs.ClearEServerDiscoveredExternalUdpPort();

	m_pServingEServerBuddy = pBuddy;
	m_nEServerBuddyStatus = nStatus;

	if (pBuddy && theApp.serverconnect && theApp.serverconnect->IsConnected() && theApp.serverconnect->GetCurrentServer() != NULL) {
		m_dwEServerBuddyServerIP = theApp.serverconnect->GetCurrentServer()->GetIP();
		m_nEServerBuddyServerPort = theApp.serverconnect->GetCurrentServer()->GetPort();
	} else {
		m_dwEServerBuddyServerIP = 0;
		m_nEServerBuddyServerPort = 0;
	}

	if (pBuddy != NULL && nStatus != Disconnected)
		m_dwEServerBuddyConnectStart = ::GetTickCount();
	else
		m_dwEServerBuddyConnectStart = 0;

	if (nStatus == Connecting && pBuddy != NULL)
		pBuddy->SetLastEServerBuddyValidServerPing(0);

	if (nStatus == Connected && pBuddy != NULL && (nOldStatus != Connected || pOldBuddy != pBuddy)) {
		AddLogLine(true, GetResString(_T("ESERVER_BUDDY_CONNECTED")), (LPCTSTR)EscPercent(pBuddy->DbgGetClientInfo()));
		ResetEServerBuddyMagicSearchState();
		m_dwLastEServerBuddyKadBridgeRequest = 0;
		TryRequestKadBuddyFromEServerBuddy(true);
		if (theApp.downloadqueue != NULL)
			theApp.downloadqueue->TriggerPendingNatTraversalDownloads(_T("eServer serving buddy connected."));
	}

	if (bBuddyChanged)
		RefreshConnectionStateIndicators();
}

bool CClientList::IsServingEServerBuddyRelayReady(const CUpDownClient* pBuddy) const
{
	if (pBuddy == NULL || pBuddy != m_pServingEServerBuddy || m_nEServerBuddyStatus != Connected)
		return false;
	if (pBuddy->socket == NULL || !pBuddy->socket->IsConnected())
		return false;
	if (theApp.serverconnect == NULL || !theApp.serverconnect->IsConnected() || theApp.serverconnect->GetCurrentServer() == NULL)
		return false;

	const uint32 dwCurrentServerIP = theApp.serverconnect->GetCurrentServer()->GetIP();
	const uint16 nCurrentServerPort = theApp.serverconnect->GetCurrentServer()->GetPort();
	if (m_dwEServerBuddyServerIP != dwCurrentServerIP || m_nEServerBuddyServerPort != nCurrentServerPort)
		return false;

	const DWORD dwLastValidServerPing = pBuddy->GetLastEServerBuddyValidServerPing();
	if (dwLastValidServerPing == 0 || (DWORD)(::GetTickCount() - dwLastValidServerPing) >= ESERVERBUDDY_STALE_SERVER_GRACE_TIME)
		return false;

	if ((pBuddy->GetServerIP() != 0 || pBuddy->GetServerPort() != 0)
		&& (pBuddy->GetServerIP() != dwCurrentServerIP || pBuddy->GetServerPort() != nCurrentServerPort))
		return false;
	if (pBuddy->GetReportedServerIP() != 0 && pBuddy->GetReportedServerIP() != dwCurrentServerIP)
		return false;

	return true;
}

bool CClientList::TryRequestKadBuddyFromEServerBuddy(bool bForce)
{
	if (m_pServingEServerBuddy == NULL || m_nEServerBuddyStatus != Connected)
		return false;
	if (!IsServingEServerBuddyRelayReady(m_pServingEServerBuddy))
		return false;
	if (m_pServingBuddy != NULL || m_nServingBuddyStatus != Disconnected)
		return false;
	if (thePrefs.IsCryptLayerRequired())
		return false;
	if (!theApp.IsFirewalled() || !Kademlia::CKademlia::IsRunning() || !Kademlia::CKademlia::IsConnected() || !Kademlia::CKademlia::IsFirewalled())
		return false;

	Kademlia::CPrefs* pKadPrefs = Kademlia::CKademlia::GetPrefs();
	Kademlia::CKademliaUDPListener* pKadUDP = Kademlia::CKademlia::TryGetUDPListener();
	Kademlia::CRoutingZone* pRoutingZone = Kademlia::CKademlia::TryGetRoutingZone();
	if (pKadPrefs == NULL || pKadUDP == NULL)
		return false;

	CUpDownClient* pBuddy = m_pServingEServerBuddy;
	if (pBuddy->HasLowID() || pBuddy->GetKadPort() == 0 || pBuddy->GetKadVersion() < KADEMLIA_VERSION2_47a)
		return false;
	if (pBuddy->socket == NULL || !pBuddy->socket->IsConnected())
		return false;

	CAddress addrBuddy = pBuddy->GetIP();
	if (addrBuddy.IsNull())
		addrBuddy = pBuddy->GetConnectIP();
	if (addrBuddy.IsNull() || addrBuddy.GetType() != CAddress::IPv4)
		return false;

	const DWORD dwNow = ::GetTickCount();
	if (!bForce && m_dwLastEServerBuddyKadBridgeRequest != 0 && (DWORD)(dwNow - m_dwLastEServerBuddyKadBridgeRequest) < ESERVERBUDDY_KAD_BRIDGE_RETRY)
		return false;

	CSafeMemFile fileIO(35);
	Kademlia::CUInt128 servedBuddyID(pKadPrefs->GetKadID());
	servedBuddyID.Xor(Kademlia::CUInt128(true));
	fileIO.WriteUInt128(servedBuddyID);
	fileIO.WriteUInt128(pKadPrefs->GetClientHash());
	fileIO.WriteUInt16(thePrefs.GetPort());
	fileIO.WriteUInt8(pKadPrefs->GetMyConnectOptions(true, true));

	uint32 uDstIP = addrBuddy.ToUInt32(true);
	uint16 uDstUDPPort = pBuddy->GetKadPort();
	Kademlia::CContact* pKadContact = NULL;
	Kademlia::CUInt128 uBuddyKadID;
	const bool bHasBuddyKadID = pBuddy->HasValidEServerBuddyKadID() || pBuddy->HasValidServingBuddyID();
	if (pBuddy->HasValidEServerBuddyKadID())
		uBuddyKadID = MakeKadIDFromRawData(pBuddy->GetEServerBuddyKadID());
	else if (pBuddy->HasValidServingBuddyID())
		uBuddyKadID = MakeKadIDFromRawData(pBuddy->GetServingBuddyID());
	if (pRoutingZone != NULL) {
		if (bHasBuddyKadID)
			pKadContact = pRoutingZone->GetContact(uBuddyKadID);
		if (pKadContact == NULL)
			pKadContact = pRoutingZone->GetContact(uDstIP, uDstUDPPort, false);
		if (pKadContact == NULL && pBuddy->GetUserPort() != 0)
			pKadContact = pRoutingZone->GetContact(uDstIP, pBuddy->GetUserPort(), true);
	}

	Kademlia::CUInt128 cryptTarget;
	const Kademlia::CUInt128* pCryptTarget = NULL;
	Kademlia::CKadUDPKey udpKey;
	LPCTSTR pszSendMode = _T("legacy direct");
	if (pKadContact != NULL && pKadContact->GetUDPPort() != 0) {
		uDstIP = pKadContact->GetIPAddress();
		uDstUDPPort = pKadContact->GetUDPPort();
		if (pKadContact->GetVersion() >= KADEMLIA_VERSION6_49aBETA) {
			udpKey = pKadContact->GetUDPKey();
			cryptTarget = pKadContact->GetClientID();
			pCryptTarget = &cryptTarget;
		}
		pszSendMode = _T("Kad contact");
	}
	else if (pBuddy->GetKadVersion() >= KADEMLIA_VERSION6_49aBETA) {
		if (!bHasBuddyKadID) {
			m_dwLastEServerBuddyKadBridgeRequest = dwNow;
			if (thePrefs.GetLogNatTraversalEvents()) {
				DebugLog(_T("[eServerBuddy][KadBridge] Delaying Kad buddy shortcut for connected eServer buddy without KadID/routing contact: %s:%u (%s)"),
					(LPCTSTR)ipstr(htonl(uDstIP)), uDstUDPPort, (LPCTSTR)EscPercent(pBuddy->DbgGetClientInfo()));
			}
			return false;
		}
		cryptTarget = uBuddyKadID;
		pCryptTarget = &cryptTarget;
		pszSendMode = _T("KadID direct");
	}

	pKadUDP->SendPacket(fileIO, KADEMLIA_FINDSERVINGBUDDY_REQ, uDstIP, uDstUDPPort, udpKey, pCryptTarget);
	pKadPrefs->SetFindServingBuddy(true);
	m_dwLastEServerBuddyKadBridgeRequest = dwNow;

	if (thePrefs.GetLogNatTraversalEvents()) {
		DebugLog(_T("[eServerBuddy][KadBridge] Sent Kad serving buddy request via connected eServer buddy %s:%u (%s, %s)"),
			(LPCTSTR)ipstr(htonl(uDstIP)), uDstUDPPort, pszSendMode, (LPCTSTR)EscPercent(pBuddy->DbgGetClientInfo()));
	}

	return true;
}

bool CClientList::TryRequestEServerBuddyFromKadBuddy(bool bForce)
{
	if (m_pServingBuddy == NULL || m_nServingBuddyStatus != Connected)
		return false;
	if (m_pServingEServerBuddy != NULL || m_nEServerBuddyStatus != Disconnected)
		return false;
	if (!theApp.serverconnect || !theApp.serverconnect->IsConnected() || !theApp.serverconnect->IsLowID())
		return false;
	if (!thePrefs.IsNatTraversalServiceEnabled() || thePrefs.IsCryptLayerRequired())
		return false;
	if (!Kademlia::CKademlia::IsRunning() || !Kademlia::CKademlia::IsConnected())
		return false;

	CUpDownClient* pBuddy = m_pServingBuddy;
	if (pBuddy->HasLowID() || !pBuddy->SupportsEServerBuddy() || !pBuddy->SupportsEServerBuddyMagicProof() || !pBuddy->HasEServerBuddySlot())
		return false;
	if (pBuddy->socket == NULL || !pBuddy->socket->IsConnected())
		return false;
	if (!pBuddy->CanQueryEServerBuddySlot())
		return false;

	const DWORD dwNow = ::GetTickCount();
	if (!bForce && m_dwLastKadBuddyEServerBridgeRequest != 0 && (DWORD)(dwNow - m_dwLastKadBuddyEServerBridgeRequest) < ESERVERBUDDY_KAD_BRIDGE_RETRY)
		return false;

	m_dwLastKadBuddyEServerBridgeRequest = dwNow;
	if (!pBuddy->SendEServerBuddyRequest()) {
		if (thePrefs.GetLogNatTraversalEvents()) {
			DebugLog(_T("[eServerBuddy][KadBridge] Kad buddy was not accepted as an eServer buddy candidate: %s"),
				(LPCTSTR)EscPercent(pBuddy->DbgGetClientInfo()));
		}
		return false;
	}

	SetServingEServerBuddy(pBuddy, Connecting);
	AddLogLine(true, GetResString(_T("ESERVER_BUDDY_REQUEST_SENT_TO")), (LPCTSTR)EscPercent(pBuddy->DbgGetClientInfo()));
	if (thePrefs.GetLogNatTraversalEvents()) {
		DebugLog(_T("[eServerBuddy][KadBridge] Sent eServer buddy request via connected Kad buddy %s"),
			(LPCTSTR)EscPercent(pBuddy->DbgGetClientInfo()));
	}

	return true;
}

// Add a LowID client we're serving as buddy
void CClientList::AddServedEServerBuddy(CUpDownClient* pClient)
{
	if (!pClient || IsServedEServerBuddy(pClient))
		return;

	if (!HasEServerBuddySlotAvailable())
		return;

	pClient->TouchEServerBuddyValidServerPing();
	m_lstServedEServerBuddies.AddTail(pClient);
	if (theApp.downloadqueue != NULL)
		theApp.downloadqueue->RebindSourceToServedEServerBuddy(pClient);
}

// Remove a LowID client from our served list
void CClientList::RemoveServedEServerBuddy(CUpDownClient* pClient)
{
	POSITION pos = m_lstServedEServerBuddies.Find(pClient);
	if (pos)
		m_lstServedEServerBuddies.RemoveAt(pos);
}

// Check if we're serving this client as buddy
bool CClientList::IsServedEServerBuddy(const CUpDownClient* pClient) const
{
	return m_lstServedEServerBuddies.Find(const_cast<CUpDownClient*>(pClient)) != NULL;
}

// Clear all served eServer buddies (for server disconnect)
void CClientList::ClearAllServedEServerBuddies()
{
	// Notify all served buddies before clearing
	for (POSITION pos = m_lstServedEServerBuddies.GetHeadPosition(); pos != NULL;) {
		CUpDownClient* pServed = m_lstServedEServerBuddies.GetNext(pos);
		if (pServed && pServed->socket && pServed->socket->IsConnected()) {
			Packet* disconn = new Packet(OP_ESERVER_BUDDY_DISCONNECT, 0, OP_EMULEPROT);
			theStats.AddUpDataOverheadOther(disconn->size);
			pServed->socket->SendPacket(disconn);
		}
	}
	m_lstServedEServerBuddies.RemoveAll();
	ResetEServerBuddyMagicSearchState();
}

// Try to find and request a buddy if we need one (LowID client)
void CClientList::TryRequestEServerBuddy()
{
	// Already have a connected buddy?
	if (m_pServingEServerBuddy && m_nEServerBuddyStatus == Connected) {
		// Validate that the buddy's socket is still connected
		if (!m_pServingEServerBuddy->socket || !m_pServingEServerBuddy->socket->IsConnected()) {
			// Socket disconnected - keep buddy for reconnect attempts
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[eServerBuddy] TryRequestEServerBuddy: Buddy socket disconnected, keeping buddy for reconnect: %s"),
					(LPCTSTR)EscPercent(m_pServingEServerBuddy->DbgGetClientInfo()));
			SetServingEServerBuddy(m_pServingEServerBuddy, Connecting);
			return;
		}
		if (IsServingEServerBuddyRelayReady(m_pServingEServerBuddy))
			return;
		if (m_dwEServerBuddyConnectStart != 0 && (DWORD)(::GetTickCount() - m_dwEServerBuddyConnectStart) < ESERVERBUDDY_STALE_SERVER_GRACE_TIME)
			return;
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[eServerBuddy] TryRequestEServerBuddy: Connected buddy is not server-verified, retrying search: %s"),
				(LPCTSTR)EscPercent(m_pServingEServerBuddy->DbgGetClientInfo()));
		SetServingEServerBuddy(NULL, Disconnected);
	}

	// Are we LowID and connected to server?
	if (!theApp.serverconnect || !theApp.serverconnect->IsConnected()) {
		ResetEServerBuddyMagicSearchState();
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[eServerBuddy] TryRequestEServerBuddy: Not connected to server"));
		return;
	}

	// HighID clients don't need a buddy.
	if (!theApp.serverconnect->IsLowID()) {
		ResetEServerBuddyMagicSearchState();
		return;
	}

	// uTP/NAT-T must be enabled and locally reachable for eServer Buddy to be useful.
	if (!thePrefs.IsNatTraversalServiceEnabled()) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[eServerBuddy] TryRequestEServerBuddy: uTP/NAT-T disabled, skipping buddy search"));
		return;
	}
	if (theApp.clientudp == NULL || !theApp.clientudp->EnsureNatTraversalEndpointReady(_T("eServer buddy search"))) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[eServerBuddy] TryRequestEServerBuddy: local UDP endpoint is not ready, skipping buddy search"));
		return;
	}

	if (m_nEServerBuddyStatus == Connecting && m_pServingEServerBuddy != NULL) {
		const bool bAckTimedOut = m_dwEServerBuddyConnectStart != 0
			&& (DWORD)(::GetTickCount() - m_dwEServerBuddyConnectStart) >= ESERVERBUDDY_CONNECTING_TIMEOUT;
		const bool bServerChanged = theApp.serverconnect != NULL && theApp.serverconnect->IsConnected() && theApp.serverconnect->GetCurrentServer() != NULL
			&& m_dwEServerBuddyServerIP != 0
			&& (m_dwEServerBuddyServerIP != theApp.serverconnect->GetCurrentServer()->GetIP()
				|| m_nEServerBuddyServerPort != theApp.serverconnect->GetCurrentServer()->GetPort());
		if (!bAckTimedOut && !bServerChanged && m_pServingEServerBuddy->socket != NULL && m_pServingEServerBuddy->socket->IsConnected())
			return;
		if (bAckTimedOut || bServerChanged) {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[eServerBuddy] TryRequestEServerBuddy: Connecting buddy expired or server changed (timeout=%u serverChanged=%u): %s"),
					bAckTimedOut ? 1u : 0u, bServerChanged ? 1u : 0u, (LPCTSTR)EscPercent(m_pServingEServerBuddy->DbgGetClientInfo()));
			m_pServingEServerBuddy->OnEServerBuddyRejectedGeneric();
			SetServingEServerBuddy(NULL, Disconnected);
		}
	}

	if (TryRequestEServerBuddyFromKadBuddy(false))
		return;

	const DWORD dwNow = ::GetTickCount();
	CServer* pCurrentServer = theApp.serverconnect->GetCurrentServer();
	if (pCurrentServer != NULL && m_dwLastEServerBuddyMagicSearchServerIP != 0) {
		if (m_dwLastEServerBuddyMagicSearchServerIP != pCurrentServer->GetIP()
			|| m_nLastEServerBuddyMagicSearchServerPort != pCurrentServer->GetPort())
		{
			if (m_bEServerBuddyMagicSearchInFlight || m_bEServerBuddyMagicSearchPrimed
				|| !m_lstEServerBuddyMagicCandidates.empty() || !m_lstEServerBuddyMagicProbes.empty())
			{
				ResetEServerBuddyMagicSearchState();
				if (thePrefs.GetLogNatTraversalEvents())
					DebugLog(_T("[eServerBuddy] TryRequestEServerBuddy: Server changed, resetting magic search state"));
			}
		}
	}

	if (m_bEServerBuddyMagicSearchInFlight
		&& (DWORD)(dwNow - m_dwLastEServerBuddyMagicSearchSent) >= ESERVERBUDDY_MAGIC_SEARCH_TIMEOUT)
	{
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[eServerBuddy] TryRequestEServerBuddy: Magic source search timed out"));
		m_bEServerBuddyMagicSearchInFlight = false;
		m_dwLastEServerBuddyMagicSearchSent = 0;
		m_bEServerBuddyMagicInFlightProbeValid = false;
		m_uEServerBuddyMagicInFlightSize = 0;
		md4clr(m_abyEServerBuddyMagicInFlightHash);
	}

	if (!m_bEServerBuddyMagicSearchPrimed) {
		StartEServerBuddyMagicSearch();
		if (m_bEServerBuddyMagicSearchInFlight)
			return;
	}

	if (TryRequestEServerBuddyFromMagicCandidates())
		return;

	if (m_bEServerBuddyMagicSearchInFlight)
		return;

	// Keep magic-first flow active while we still have pending candidates.
	if (!m_lstEServerBuddyMagicCandidates.empty())
		return;

	if (!m_lstEServerBuddyMagicProbes.empty()) {
		TrySendNextEServerBuddyMagicProbe();
		if (m_bEServerBuddyMagicSearchInFlight)
			return;
	}

	if (thePrefs.GetLogNatTraversalEvents())
		DebugLog(_T("[eServerBuddy] TryRequestEServerBuddy: Magic candidates exhausted, trying fallback known-client method"));

	CUpDownClient* pCandidate = FindEServerBuddy();
	if (!pCandidate) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[eServerBuddy] TryRequestEServerBuddy: Fallback known-client method found no candidate"));
		StartEServerBuddyMagicSearch();
		return;
	}

	if (pCandidate->SendEServerBuddyRequest()) {
		SetServingEServerBuddy(pCandidate, Connecting);
			AddLogLine(true, GetResString(_T("ESERVER_BUDDY_REQUEST_SENT_TO")), (LPCTSTR)EscPercent(pCandidate->DbgGetClientInfo()));
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[eServerBuddy] TryRequestEServerBuddy: Fallback request sent successfully"));
	} else {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[eServerBuddy] TryRequestEServerBuddy: Fallback SendEServerBuddyRequest failed for %s"),
				(LPCTSTR)EscPercent(pCandidate->DbgGetClientInfo()));
		StartEServerBuddyMagicSearch();
	}
}

// Find a served eServer buddy by user hash
CUpDownClient* CClientList::FindServedEServerBuddyByHash(const uchar* pHash) const
{
	if (!pHash)
		return NULL;

	for (POSITION pos = m_lstServedEServerBuddies.GetHeadPosition(); pos != NULL;) {
		CUpDownClient* pClient = m_lstServedEServerBuddies.GetNext(pos);
		if (pClient && pClient->HasValidHash() && md4equ(pClient->GetUserHash(), pHash))
			return pClient;
	}
	return NULL;
}

CUpDownClient* CClientList::FindServedEServerBuddyByLowID(uint32 dwServerIP, uint16 nServerPort, uint32 dwTargetLowID) const
{
	if (dwTargetLowID == 0)
		return NULL;

	const uint32 dwHybridLowID = htonl(dwTargetLowID);
	CUpDownClient* pFallbackUnknownServer = NULL;
	for (POSITION pos = m_lstServedEServerBuddies.GetHeadPosition(); pos != NULL;) {
		CUpDownClient* pClient = m_lstServedEServerBuddies.GetNext(pos);
		if (pClient == NULL)
			continue;
		const uint32 dwClientLowID = pClient->GetUserIDHybrid();
		if (dwClientLowID != dwHybridLowID && dwClientLowID != dwTargetLowID)
			continue;

		const bool bClientServerKnown = pClient->GetServerIP() != 0 && pClient->GetServerPort() != 0;
		if (dwServerIP != 0 && nServerPort != 0) {
			if (bClientServerKnown) {
				if (pClient->GetServerIP() == dwServerIP && pClient->GetServerPort() == nServerPort)
					return pClient;
				continue;
			}
			if (pFallbackUnknownServer == NULL)
				pFallbackUnknownServer = pClient;
			continue;
		}

		if (dwServerIP != 0 && pClient->GetServerIP() == dwServerIP)
			return pClient;
		if (dwServerIP == 0 && pFallbackUnknownServer == NULL)
			pFallbackUnknownServer = pClient;
	}

	return pFallbackUnknownServer;
}

static void GetCurrentEServerRelayServerContext(uint32& dwServerIP, uint16& nServerPort)
{
	dwServerIP = 0;
	nServerPort = 0;
	if (theApp.serverconnect != NULL && theApp.serverconnect->IsConnected() && theApp.serverconnect->GetCurrentServer() != NULL) {
		dwServerIP = theApp.serverconnect->GetCurrentServer()->GetIP();
		nServerPort = theApp.serverconnect->GetCurrentServer()->GetPort();
	}
}

static void InitEServerRelayRequest(EServerRelayRequest& req, CUpDownClient* pRequester, uint32 dwTargetLowID, uint32 dwIP, uint16 nPort, const uchar* pFileHash)
{
	req.pRequester = pRequester;
	if (pRequester != NULL && pRequester->HasValidHash())
		md4cpy(req.requesterHash, pRequester->GetUserHash());
	else
		md4clr(req.requesterHash);
	md4clr(req.targetHash);
	req.dwTargetLowID = dwTargetLowID;
	GetCurrentEServerRelayServerContext(req.dwTargetServerIP, req.nTargetServerPort);
	req.dwRequesterIP = dwIP;
	req.nRequesterKadPort = nPort;
	req.dwTargetPublicIP = 0;
	req.nTargetKadPort = 0;
	req.bAwaitingTargetExtPort = false;
	req.dwTargetExtPortWaitStart = 0;
	req.dwRequestTime = ::GetTickCount();
	if (pFileHash != NULL && !isnulmd4(pFileHash))
		md4cpy(req.fileHash, pFileHash);
	else
		md4clr(req.fileHash);
}

static bool EServerRelayOptionalHashEquals(const uchar* pStoredHash, const uchar* pIncomingHash)
{
	const bool bStoredEmpty = pStoredHash == NULL || isnulmd4(pStoredHash);
	const bool bIncomingEmpty = pIncomingHash == NULL || isnulmd4(pIncomingHash);
	if (bStoredEmpty || bIncomingEmpty)
		return bStoredEmpty && bIncomingEmpty;
	return md4equ(pStoredHash, pIncomingHash);
}

static bool EServerRelayRequesterMatches(const EServerRelayRequest& req, const CUpDownClient* pRequester)
{
	if (req.pRequester == pRequester)
		return true;
	if (pRequester == NULL || !pRequester->HasValidHash() || isnulmd4(req.requesterHash))
		return false;
	return md4equ(req.requesterHash, pRequester->GetUserHash());
}

static bool EServerRelayServerContextMatches(const EServerRelayRequest& req, uint32 dwServerIP, uint16 nServerPort)
{
	if (req.dwTargetServerIP == 0 || req.nTargetServerPort == 0 || dwServerIP == 0 || nServerPort == 0)
		return true;
	return req.dwTargetServerIP == dwServerIP && req.nTargetServerPort == nServerPort;
}

// Add a pending relay request (using hash)
void CClientList::AddPendingEServerRelay(CUpDownClient* pRequester, const uchar* targetHash, uint32 dwIP, uint16 nPort)
{
	// Limit pending relays to prevent memory exhaustion
	if (m_lstPendingEServerRelays.size() >= ESERVERBUDDY_MAX_PENDING_RELAYS) {
		// Remove oldest request
		m_lstPendingEServerRelays.pop_front();
	}

	EServerRelayRequest req;
	InitEServerRelayRequest(req, pRequester, 0, dwIP, nPort, NULL);
	if (targetHash != NULL)
		md4cpy(req.targetHash, targetHash);

	m_lstPendingEServerRelays.push_back(req);
}

// Add a pending relay request (using LowID - preferred for server callback flow)
bool CClientList::AddPendingEServerRelayByLowID(CUpDownClient* pRequester, uint32 dwTargetLowID, uint32 dwIP, uint16 nPort, const uchar* pFileHash)
{
	uint32 dwTargetServerIP = 0;
	uint16 nTargetServerPort = 0;
	GetCurrentEServerRelayServerContext(dwTargetServerIP, nTargetServerPort);

	for (auto& req : m_lstPendingEServerRelays) {
		if (req.dwTargetLowID != dwTargetLowID || dwTargetLowID == 0)
			continue;
		if (!EServerRelayServerContextMatches(req, dwTargetServerIP, nTargetServerPort))
			continue;

		const bool bRequesterInClientList = req.pRequester != NULL && list.Find(req.pRequester) != NULL;
		const bool bExistingRequesterUsable = bRequesterInClientList
			&& IsServedEServerBuddy(req.pRequester)
			&& req.pRequester->socket != NULL
			&& req.pRequester->socket->IsConnected();
		const bool bSameRequester = EServerRelayRequesterMatches(req, pRequester);
		const bool bSameFile = EServerRelayOptionalHashEquals(req.fileHash, pFileHash);

		if (bExistingRequesterUsable && (!bSameRequester || !bSameFile)) {
			AddDebugLogLine(false, _T("[eServerBuddy] AddPendingEServerRelayByLowID: Rejecting overlapping relay for LowID=%u (existing=%s incoming=%s existingFile=%s incomingFile=%s)"),
				dwTargetLowID,
				req.pRequester != NULL ? (LPCTSTR)EscPercent(req.pRequester->DbgGetClientInfo()) : _T("NULL"),
				pRequester != NULL ? (LPCTSTR)EscPercent(pRequester->DbgGetClientInfo()) : _T("NULL"),
				!isnulmd4(req.fileHash) ? (LPCTSTR)md4str(req.fileHash) : _T("NULL"),
				(pFileHash != NULL && !isnulmd4(pFileHash)) ? (LPCTSTR)md4str(pFileHash) : _T("NULL"));
			return false;
		}
		if (!bExistingRequesterUsable) {
			AddDebugLogLine(false, _T("[eServerBuddy] AddPendingEServerRelayByLowID: Replacing stale relay owner for LowID=%u"),
				dwTargetLowID);
		}

		InitEServerRelayRequest(req, pRequester, dwTargetLowID, dwIP, nPort, pFileHash);
		AddDebugLogLine(false, _T("[eServerBuddy] AddPendingEServerRelayByLowID: Updated relay for LowID=%u Server=%s:%u IP=%s Port=%u FileHash=%s"),
			dwTargetLowID, (LPCTSTR)ipstr(req.dwTargetServerIP), req.nTargetServerPort, (LPCTSTR)ipstr(dwIP), nPort,
			!isnulmd4(req.fileHash) ? (LPCTSTR)md4str(req.fileHash) : _T("NULL"));
		return true;
	}

	// Limit pending relays to prevent memory exhaustion
	if (m_lstPendingEServerRelays.size() >= ESERVERBUDDY_MAX_PENDING_RELAYS) {
		// Remove oldest request
		m_lstPendingEServerRelays.pop_front();
	}

	EServerRelayRequest req;
	InitEServerRelayRequest(req, pRequester, dwTargetLowID, dwIP, nPort, pFileHash);

	AddDebugLogLine(false, _T("[eServerBuddy] AddPendingEServerRelayByLowID: Stored relay for LowID=%u Server=%s:%u IP=%s Port=%u FileHash=%s"),
		dwTargetLowID, (LPCTSTR)ipstr(req.dwTargetServerIP), req.nTargetServerPort, (LPCTSTR)ipstr(dwIP), nPort,
		!isnulmd4(req.fileHash) ? (LPCTSTR)md4str(req.fileHash) : _T("NULL"));

	m_lstPendingEServerRelays.push_back(req);
	return true;
}

void CClientList::UpdatePendingEServerRelayRequesterPort(const CUpDownClient* pRequester, uint16 nPort)
{
	if (pRequester == NULL || nPort == 0)
		return;

	for (auto& req : m_lstPendingEServerRelays) {
		if (req.pRequester == pRequester)
			req.nRequesterKadPort = nPort;
	}
}

static void AppendEServerExternalPortTailForRequester(CSafeMemFile& data, const CUpDownClient* requester)
{
	if (requester == NULL || !requester->SupportsEServerBuddyExternalUdpPort())
		return;
	if (!requester->HasFreshObservedExternalUdpPort())
		return;

	uint16 nObservedPort = requester->GetObservedExternalUdpPort();
	if (nObservedPort == 0)
		return;

	data.WriteUInt16(nObservedPort);
	data.WriteUInt8(ESERVER_EXT_UDP_PORT_SRC_ESERVER_BUDDY);
}

bool CClientList::TrySendPendingEServerRelayResponseForTarget(CUpDownClient* pTarget)
{
	if (pTarget == NULL)
		return false;

	uint32 dwTargetLowID = pTarget->GetUserIDHybrid();
	if (dwTargetLowID == 0)
		return false;

	EServerRelayRequest* pReq = FindPendingEServerRelayForTarget(pTarget);
	if (pReq == NULL)
		return false;
	if (pReq->pRequester == NULL) {
		RemovePendingEServerRelayRequest(pReq);
		return false;
	}
	if (!IsServedEServerBuddy(pReq->pRequester)) {
		const bool bRequesterInClientList = (list.Find(pReq->pRequester) != NULL);
		const DWORD dwRelayAge = (DWORD)(::GetTickCount() - pReq->dwRequestTime);
		static const DWORD kPendingRelayOwnerGraceMs = 15000;
		if (!bRequesterInClientList || dwRelayAge >= kPendingRelayOwnerGraceMs) {
			AddDebugLogLine(false, _T("[eServerBuddy] TrySendPendingEServerRelayResponseForTarget: Dropping stale relay (inList=%u age=%u ms) for LowID=%u"),
				bRequesterInClientList ? 1u : 0u,
				(UINT)dwRelayAge,
				dwTargetLowID);
			RemovePendingEServerRelayRequest(pReq);
		} else {
			AddDebugLogLine(false, _T("[eServerBuddy] TrySendPendingEServerRelayResponseForTarget: Requester temporarily not served (age=%u ms), keeping relay for LowID=%u"),
				(UINT)dwRelayAge,
				dwTargetLowID);
		}
		return false;
	}

	if (pReq->bAwaitingTargetExtPort && !pTarget->HasFreshObservedExternalUdpPort()) {
		if (pReq->dwTargetExtPortWaitStart == 0)
			pReq->dwTargetExtPortWaitStart = pReq->dwRequestTime;
		const DWORD dwWaitAge = (DWORD)(::GetTickCount() - pReq->dwTargetExtPortWaitStart);
		if (dwWaitAge < ESERVER_EXT_UDP_PORT_WAIT_TIME) {
			if (thePrefs.GetLogNatTraversalEvents()) {
				AddDebugLogLine(false, _T("[eServerBuddy] Waiting for target external UDP port before final relay response (LowID=%u age=%u/%u ms)"),
					dwTargetLowID, (UINT)dwWaitAge, (UINT)ESERVER_EXT_UDP_PORT_WAIT_TIME);
			}
			return false;
		}
		pReq->bAwaitingTargetExtPort = false;
		pReq->dwTargetExtPortWaitStart = 0;
		if (thePrefs.GetLogNatTraversalEvents()) {
			AddDebugLogLine(false, _T("[eServerBuddy] Falling back to target reported/local UDP port after external probe wait expired (LowID=%u wait=%u ms)"),
				dwTargetLowID, (UINT)dwWaitAge);
		}
	} else if (pReq->bAwaitingTargetExtPort) {
		pReq->bAwaitingTargetExtPort = false;
		pReq->dwTargetExtPortWaitStart = 0;
		if (thePrefs.GetLogNatTraversalEvents()) {
			AddDebugLogLine(false, _T("[eServerBuddy] Target external UDP probe received before fallback (LowID=%u port=%u)"),
				dwTargetLowID, (UINT)pTarget->GetObservedExternalUdpPort());
		}
	}

	if (pReq->nTargetKadPort == 0 && pReq->dwTargetPublicIP == 0)
		return false;

	if (pReq->pRequester->socket == NULL || !pReq->pRequester->socket->IsConnected())
		return false;

	uint16 nTargetPort = 0;
	if (pTarget->HasFreshObservedExternalUdpPort())
		nTargetPort = pTarget->GetObservedExternalUdpPort();
	if (nTargetPort == 0)
		nTargetPort = pReq->nTargetKadPort;
	if (nTargetPort == 0)
		nTargetPort = pTarget->GetKadPort() ? pTarget->GetKadPort() : pTarget->GetUDPPort();
	if (nTargetPort == 0)
		return false;

	uint32 dwFinalTargetIP = pReq->dwTargetPublicIP;
	CAddress ackAddr(dwFinalTargetIP, false);
	CAddress observedAddr = pTarget->GetIP();

	if (dwFinalTargetIP == 0 || !ackAddr.IsPublicIP()) {
		if (!observedAddr.IsNull() && observedAddr.IsPublicIP())
			dwFinalTargetIP = observedAddr.ToUInt32(false);
		else if (!pTarget->GetConnectIP().IsNull() && pTarget->GetConnectIP().IsPublicIP())
			dwFinalTargetIP = pTarget->GetConnectIP().ToUInt32(false);
	}
	if (dwFinalTargetIP == 0)
		return false;

	CAddress finalAddr(dwFinalTargetIP, false);
	if (finalAddr.IsNull() || !finalAddr.IsPublicIP()) {
		if (thePrefs.GetLogNatTraversalEvents()) {
			AddDebugLogLine(false, _T("[eServerBuddy] TrySendPendingEServerRelayResponseForTarget: rejecting invalid final endpoint %s:%u for LowID=%u"),
				(LPCTSTR)ipstr(finalAddr), nTargetPort, dwTargetLowID);
		}
		return false;
	}

	const uint8 byTargetConnectOptions = pTarget->GetConnectOptions(true, true, true);
	CSafeMemFile data_resp(27);
	data_resp.WriteUInt32(dwFinalTargetIP);                       // Target Public IP
	data_resp.WriteUInt16(nTargetPort);                           // Target KadPort
	data_resp.WriteUInt8(ESERVERBUDDY_STATUS_ACCEPTED);           // Status: Success
	data_resp.WriteUInt32(dwTargetLowID);                         // Target LowID (for source lookup)
	data_resp.WriteHash16(pReq->fileHash);                        // File hash for download context
	AppendEServerExternalPortTailForRequester(data_resp, pReq->pRequester);

	Packet* reply = new Packet(data_resp, OP_EMULEPROT, OP_ESERVER_RELAY_RESPONSE);
	theStats.AddUpDataOverheadOther(reply->size);
	pReq->pRequester->socket->SendPacket(reply, true, 0, true);

	if (thePrefs.GetLogNatTraversalEvents()) {
		AddDebugLogLine(false, _T("[eServerBuddy] Final relay response (deferred) - Target=%s:%u LowID=%u FileHash=%s TargetOptions=0x%02X Wire=legacy direct-caps"),
			(LPCTSTR)ipstr(finalAddr), nTargetPort, dwTargetLowID,
			!isnulmd4(pReq->fileHash) ? (LPCTSTR)md4str(pReq->fileHash) : _T("NULL"), byTargetConnectOptions);
	}

	RemovePendingEServerRelayRequest(pReq);
	return true;
}

// Find pending relay request by target hash
EServerRelayRequest* CClientList::FindPendingEServerRelay(const uchar* targetHash)
{
	if (targetHash == NULL)
		return NULL;
	for (auto& req : m_lstPendingEServerRelays) {
		if (md4equ(req.targetHash, targetHash))
			return &req;
	}
	return NULL;
}

// Find pending relay request by target LowID
EServerRelayRequest* CClientList::FindPendingEServerRelayByLowID(uint32 dwTargetLowID)
{
	for (auto& req : m_lstPendingEServerRelays) {
		if (req.dwTargetLowID == dwTargetLowID && dwTargetLowID != 0)
			return &req;
	}
	return NULL;
}

EServerRelayRequest* CClientList::FindPendingEServerRelayForTarget(const CUpDownClient* pTarget)
{
	if (pTarget == NULL || !pTarget->HasLowID())
		return NULL;

	const uint32 dwTargetLowID = pTarget->GetUserIDHybrid();
	if (dwTargetLowID == 0)
		return NULL;

	uint32 dwTargetServerIP = pTarget->GetServerIP();
	uint16 nTargetServerPort = pTarget->GetServerPort();
	if (dwTargetServerIP == 0 || nTargetServerPort == 0)
		GetCurrentEServerRelayServerContext(dwTargetServerIP, nTargetServerPort);

	EServerRelayRequest* pFallbackUnknownServer = NULL;
	for (auto& req : m_lstPendingEServerRelays) {
		if (req.dwTargetLowID != dwTargetLowID)
			continue;
		if (req.dwTargetServerIP != 0 && req.nTargetServerPort != 0 && dwTargetServerIP != 0 && nTargetServerPort != 0) {
			if (req.dwTargetServerIP == dwTargetServerIP && req.nTargetServerPort == nTargetServerPort)
				return &req;
			continue;
		}
		if (pFallbackUnknownServer == NULL)
			pFallbackUnknownServer = &req;
	}
	return pFallbackUnknownServer;
}

// Find the only recent outbound relay target for a legacy response without identity.
CUpDownClient* CClientList::FindUniqueRecentEServerRelayTargetForBuddy(const CUpDownClient* pBuddy, bool* pbAmbiguous)
{
	if (pbAmbiguous != NULL)
		*pbAmbiguous = false;
	if (pBuddy == NULL)
		return NULL;

	const DWORD kResponseMatchWindow = ESERVERBUDDY_RELAY_TIMEOUT + ESERVERBUDDY_RELAY_REASK_TIME;
	CUpDownClient* pMatch = NULL;
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		CUpDownClient* pClient = list.GetNext(pos);
		if (pClient == NULL || pClient == pBuddy || !pClient->HasRecentEServerRelayRequestToBuddy(pBuddy, kResponseMatchWindow))
			continue;
		if (pMatch != NULL && pMatch != pClient) {
			if (pbAmbiguous != NULL)
				*pbAmbiguous = true;
			return NULL;
		}
		pMatch = pClient;
	}
	return pMatch;
}

// Remove pending relay request by hash
void CClientList::RemovePendingEServerRelay(const uchar* targetHash)
{
	if (targetHash == NULL)
		return;
	for (auto it = m_lstPendingEServerRelays.begin(); it != m_lstPendingEServerRelays.end(); ++it) {
		if (md4equ(it->targetHash, targetHash)) {
			m_lstPendingEServerRelays.erase(it);
			break;
		}
	}
}

// Remove pending relay request by LowID
void CClientList::RemovePendingEServerRelayByLowID(uint32 dwTargetLowID)
{
	for (auto it = m_lstPendingEServerRelays.begin(); it != m_lstPendingEServerRelays.end(); ++it) {
		if (it->dwTargetLowID == dwTargetLowID && dwTargetLowID != 0) {
			m_lstPendingEServerRelays.erase(it);
			break;
		}
	}
}

void CClientList::RemovePendingEServerRelayRequest(const EServerRelayRequest* pReq)
{
	if (pReq == NULL)
		return;
	for (auto it = m_lstPendingEServerRelays.begin(); it != m_lstPendingEServerRelays.end(); ++it) {
		if (&(*it) == pReq) {
			m_lstPendingEServerRelays.erase(it);
			break;
		}
	}
}

static bool ClientMatchesPendingRelayTarget(const CUpDownClient* pClient, const EServerRelayRequest& req)
{
	if (pClient == NULL || !pClient->HasLowID() || req.dwTargetLowID == 0)
		return false;
	if (pClient->GetUserIDHybrid() != req.dwTargetLowID)
		return false;
	if (req.dwTargetServerIP == 0 || req.nTargetServerPort == 0)
		return true;
	if (pClient->GetServerIP() == 0 || pClient->GetServerPort() == 0)
		return true;
	return pClient->GetServerIP() == req.dwTargetServerIP && pClient->GetServerPort() == req.nTargetServerPort;
}

void CClientList::ProcessPendingEServerRelayExtPortWaits()
{
	struct ReadyRelayKey
	{
		uint32 dwLowID;
		uint32 dwServerIP;
		uint16 nServerPort;
	};
	std::vector<ReadyRelayKey> aReadyRelays;
	const DWORD dwNow = ::GetTickCount();

	for (auto& req : m_lstPendingEServerRelays) {
		if (!req.bAwaitingTargetExtPort || req.dwTargetLowID == 0)
			continue;
		if (req.dwTargetExtPortWaitStart == 0)
			req.dwTargetExtPortWaitStart = req.dwRequestTime;
		if ((DWORD)(dwNow - req.dwTargetExtPortWaitStart) >= ESERVER_EXT_UDP_PORT_WAIT_TIME) {
			ReadyRelayKey key = { req.dwTargetLowID, req.dwTargetServerIP, req.nTargetServerPort };
			aReadyRelays.push_back(key);
		}
	}

	for (const ReadyRelayKey& key : aReadyRelays) {
		EServerRelayRequest* pReq = NULL;
		for (auto& req : m_lstPendingEServerRelays) {
			if (req.dwTargetLowID == key.dwLowID
				&& req.dwTargetServerIP == key.dwServerIP
				&& req.nTargetServerPort == key.nServerPort) {
				pReq = &req;
				break;
			}
		}
		if (pReq == NULL || !pReq->bAwaitingTargetExtPort)
			continue;

		CUpDownClient* pTarget = NULL;
		for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
			CUpDownClient* pClient = list.GetNext(pos);
			if (ClientMatchesPendingRelayTarget(pClient, *pReq)) {
				pTarget = pClient;
				break;
			}
		}

		if (pTarget != NULL)
			TrySendPendingEServerRelayResponseForTarget(pTarget);
	}
}

static bool NotifyExpiredEServerRelayRequester(const EServerRelayRequest& req)
{
	CUpDownClient* pRequester = req.pRequester;
	if (pRequester == NULL || pRequester->socket == NULL || !pRequester->socket->IsConnected())
		return false;
	if (theApp.clientlist == NULL || !theApp.clientlist->IsServedEServerBuddy(pRequester))
		return false;

	CSafeMemFile data_resp(7);
	data_resp.WriteUInt32(0);
	data_resp.WriteUInt16(0);
	data_resp.WriteUInt8(ESERVERBUDDY_STATUS_REJECTED);
	AppendEServerExternalPortTailForRequester(data_resp, pRequester);
	Packet* reply = new Packet(data_resp, OP_EMULEPROT, OP_ESERVER_RELAY_RESPONSE);
	theStats.AddUpDataOverheadOther(reply->size);
	pRequester->socket->SendPacket(reply, true, 0, true);
	return true;
}

// Cleanup expired relay requests
void CClientList::CleanupExpiredEServerRelays()
{
	ProcessPendingEServerRelayExtPortWaits();

	DWORD dwNow = ::GetTickCount();
	for (auto it = m_lstPendingEServerRelays.begin(); it != m_lstPendingEServerRelays.end();) {
		if (dwNow - it->dwRequestTime > ESERVERBUDDY_RELAY_TIMEOUT) {
			const bool bNotified = NotifyExpiredEServerRelayRequester(*it);
			if (thePrefs.GetLogNatTraversalEvents()) {
				AddDebugLogLine(false, _T("[eServerBuddy] Pending relay expired for LowID=%u Server=%s:%u notified=%u"),
					it->dwTargetLowID, (LPCTSTR)ipstr(it->dwTargetServerIP), it->nTargetServerPort, bNotified ? 1u : 0u);
			}
			it = m_lstPendingEServerRelays.erase(it);
		} else {
			++it;
		}
	}
}

// Process periodic buddy pings and cleanup
void CClientList::ProcessEServerBuddyPings()
{
	DWORD dwNow = ::GetTickCount();
	uint32 dwOurServerIP = 0;
	uint16 nOurServerPort = 0;
	if (theApp.serverconnect && theApp.serverconnect->IsConnected() && theApp.serverconnect->GetCurrentServer()) {
		dwOurServerIP = theApp.serverconnect->GetCurrentServer()->GetIP();
		nOurServerPort = theApp.serverconnect->GetCurrentServer()->GetPort();
	}

	auto SendKeepAlivePing = [&](CUpDownClient* pClient, LPCTSTR pszRole) {
		if (pClient == NULL || pClient->socket == NULL || !pClient->socket->IsConnected())
			return;

		CSafeMemFile data(7);
		uint8 byPingFlags = 0;
		if (theApp.serverconnect && theApp.serverconnect->IsConnected() && !theApp.serverconnect->IsLowID())
			byPingFlags |= ESERVERBUDDY_PING_FLAG_HIGHID;
		data.WriteUInt32(dwOurServerIP);
		data.WriteUInt8(byPingFlags);
		data.WriteUInt16(nOurServerPort);

		Packet* ping = new Packet(data, OP_EMULEPROT, OP_ESERVER_BUDDY_PING);
		theStats.AddUpDataOverheadOther(ping->size);
		pClient->socket->SendPacket(ping);

		if (thePrefs.GetLogNatTraversalEvents()) {
			DebugLog(_T("[eServerBuddy] Keep-alive ping sent to %s (%s)"),
				(LPCTSTR)EscPercent(pClient->DbgGetClientInfo()), pszRole);
		}
	};

	if (dwNow - m_dwLastEServerBuddyKeepAlive >= ESERVERBUDDY_KEEPALIVE_TIME) {
		m_dwLastEServerBuddyKeepAlive = dwNow;

		// Send keep-alive to our serving buddy (if we're LowID).
		if (m_pServingEServerBuddy && m_nEServerBuddyStatus == Connected) {
			SendKeepAlivePing(m_pServingEServerBuddy, _T("serving"));
			// Refresh the observed external UDP port while the buddy link is idle.
			// This keeps same-server relay requests from falling back to the stale local UDP port
			// after long waits between transfers.
			m_pServingEServerBuddy->SendEServerUdpProbe();
		}

		// Send keep-alive to all served buddies (if we're HighID).
		for (POSITION pos = m_lstServedEServerBuddies.GetHeadPosition(); pos != NULL;) {
			CUpDownClient* pClient = m_lstServedEServerBuddies.GetNext(pos);
			SendKeepAlivePing(pClient, _T("served"));
		}
	}

	TryRequestKadBuddyFromEServerBuddy(false);
	TryRequestEServerBuddyFromKadBuddy(false);

	// Keep cleanup expensive operations on the old long interval.
	if (dwNow - m_dwLastEServerBuddyPing < ESERVERBUDDY_PING_TIME)
		return;

	m_dwLastEServerBuddyPing = dwNow;
	CleanupExpiredEServerRelays();
}


static bool PendingRelayContextHasIdentity(const PendingRelayContext& ctx)
{
	return ctx.dwTargetLowID != 0 || !isnulmd4(ctx.targetHash);
}

static bool PendingRelayContextMatchesIdentity(const PendingRelayContext& ctx, const CUpDownClient* pClient)
{
	if (pClient == NULL)
		return false;

	if (!isnulmd4(ctx.targetHash)) {
		if (!pClient->HasValidHash() || !md4equ(pClient->GetUserHash(), ctx.targetHash))
			return false;
	}

	if (ctx.dwTargetLowID != 0) {
		if (!pClient->HasLowID())
			return false;
		const uint32 dwClientLowID = pClient->GetUserIDHybrid();
		if (dwClientLowID != ctx.dwTargetLowID && dwClientLowID != htonl(ctx.dwTargetLowID))
			return false;
	}

	if (ctx.dwTargetServerIP != 0 && ctx.nTargetServerPort != 0 && pClient->GetServerIP() != 0 && pClient->GetServerPort() != 0) {
		if (pClient->GetServerIP() != ctx.dwTargetServerIP || pClient->GetServerPort() != ctx.nTargetServerPort)
			return false;
	}

	return true;
}

// Add pending relay context (to restore reqfile if client recreated during connection)
void CClientList::AddPendingRelayContext(uint32 dwIP, uint16 nPort, const uchar* pFileHash, uint32 dwTargetLowID, uint32 dwTargetServerIP, uint16 nTargetServerPort, const uchar* pTargetHash)
{
	PendingRelayContext ctx;
	ctx.dwIP = dwIP;
	ctx.nPort = nPort;
	if (pFileHash)
		md4cpy(ctx.fileHash, pFileHash);
	else
		md4clr(ctx.fileHash);
	ctx.dwTargetLowID = dwTargetLowID;
	ctx.dwTargetServerIP = dwTargetServerIP;
	ctx.nTargetServerPort = nTargetServerPort;
	if (pTargetHash)
		md4cpy(ctx.targetHash, pTargetHash);
	else
		md4clr(ctx.targetHash);
	ctx.dwInserted = ::GetTickCount();

	m_lstPendingRelayContexts.push_back(ctx);

	if (thePrefs.GetLogNatTraversalEvents()) {
		AddDebugLogLine(false, _T("[eServerBuddy] AddPendingRelayContext: IP=%s Port=%u FileHash=%s"),
			(LPCTSTR)ipstr(dwIP), nPort, !isnulmd4(ctx.fileHash) ? (LPCTSTR)md4str(ctx.fileHash) : _T("NULL"));
	}

	// Check immediately if a matching client already exists (handles race condition)
	// Check Main List
	CAddress addr(dwIP, false);
	CUpDownClient* pClient = FindClientByIP_KadPort(addr, nPort);
	if (pClient == NULL)
		pClient = FindClientByIP_UDP(addr, nPort);
	if (pClient) {
	if (thePrefs.GetLogNatTraversalEvents()) {
		AddDebugLogLine(false, _T("[eServerBuddy] Found existing client in Main List: %s"), (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
	}
		if (ApplyPendingRelayContext(pClient)) return;
	}
	
	// Check Connecting List
	for (POSITION pos = m_liConnectingClients.GetHeadPosition(); pos != NULL;) {
		CONNECTINGCLIENT& cc = m_liConnectingClients.GetNext(pos);
		if (cc.pClient) {
			uint32 ccIP = cc.pClient->GetIP().ToUInt32(false);
			if (ccIP == dwIP) {
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] Found existing client in Connecting List: %s"), (LPCTSTR)EscPercent(cc.pClient->DbgGetClientInfo()));
				}

				if (!PendingRelayContextHasIdentity(ctx) || PendingRelayContextMatchesIdentity(ctx, cc.pClient)) {
					// Update UDP/Kad port only after identity is compatible with the pending relay context.
					if (thePrefs.GetLogNatTraversalEvents())
						AddDebugLogLine(false, _T("[eServerBuddy] Updating client UDP/Kad port to %u for IP=0x%08X"), nPort, dwIP);
					cc.pClient->SetKadPort(nPort);
					cc.pClient->SetUDPPort(nPort);
				} else if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] Pending relay context skipped connecting client due to identity mismatch: %s"), (LPCTSTR)EscPercent(cc.pClient->DbgGetClientInfo()));
				}

				if (ApplyPendingRelayContext(cc.pClient)) return;
			}
		}
	}
}

bool CClientList::ApplyPendingRelayContext(CUpDownClient* pClient)
{
	if (!pClient) return false;

	// Check if we have a context matching this client's IP/Port
	uint32 dwClientIP = pClient->GetIP().ToUInt32(false); // Network byte order
	uint16 nUserPort = pClient->GetUserPort();
	uint16 nKadPort = pClient->GetKadPort();
	uint16 nUDPPort = pClient->GetUDPPort();
	
	// Trace matching for debugging
	if (thePrefs.GetLogNatTraversalEvents()) {
		if (!m_lstPendingRelayContexts.empty()) {
			AddDebugLogLine(false, _T("[eServerBuddy] DEBUG: ApplyPendingRelayContext checking ClientIP=0x%08X (%s) UserPort=%u KadPort=%u UdpPort=%u. Contexts=%Iu"),
				dwClientIP, (LPCTSTR)ipstr(pClient->GetIP()), nUserPort, nKadPort, nUDPPort, m_lstPendingRelayContexts.size());
		}
	}

	// Try to match against either KadPort or UserPort
	for (auto it = m_lstPendingRelayContexts.begin(); it != m_lstPendingRelayContexts.end(); ++it) {
		if (it->dwIP == dwClientIP) {
			if (PendingRelayContextHasIdentity(*it) && !PendingRelayContextMatchesIdentity(*it, pClient)) {
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] DEBUG: IP Matched (0x%08X) but pending relay identity mismatched for %s"),
						dwClientIP, (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
				}
				continue;
			}
			// IP Matches. Check Ports.
			const bool bNoUdpInfo = (nKadPort == 0 && nUDPPort == 0);
			const bool bPortMatch = (it->nPort == nKadPort) || (nUDPPort != 0 && it->nPort == nUDPPort) || (bNoUdpInfo && it->nPort == nUserPort);
			if (bPortMatch) {
				if (bNoUdpInfo && it->nPort == nUserPort) {
					pClient->SetKadPort(it->nPort);
					pClient->SetUDPPort(it->nPort);
				}
				// Match found!
				if (!isnulmd4(it->fileHash)) {
					CPartFile* pFile = theApp.downloadqueue->GetFileByID(it->fileHash);
					if (pFile) {
						pClient->SetRequestFile(pFile);
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] ApplyPendingRelayContext: Restored reqfile %s for %s"),
							(LPCTSTR)EscPercent(pFile->GetFileName()), (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()));
					}
						
						// Remove context after use
						m_lstPendingRelayContexts.erase(it);
						return true;
					} else {
					if (thePrefs.GetLogNatTraversalEvents()) {
						AddDebugLogLine(false, _T("[eServerBuddy] DEBUG: Context matched but File not found in DownloadQueue! Hash=%s"), (LPCTSTR)md4str(it->fileHash));
					}
					}
				}
				// Context found but no file or empty hash - remove it anyway
				m_lstPendingRelayContexts.erase(it);
				return false;
			} else {
				// IP Matched, Port Mismatch
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(false, _T("[eServerBuddy] DEBUG: IP Matched (0x%08X) but Port Mismatch! ClientUserPort=%u ClientKadPort=%u ClientUdpPort=%u CtxPort=%u"),
						dwClientIP, nUserPort, nKadPort, nUDPPort, it->nPort);
				}
			}
		}
	}
	return false;
}

void CClientList::CleanupPendingRelayContexts()
{
	DWORD dwNow = ::GetTickCount();
	for (auto it = m_lstPendingRelayContexts.begin(); it != m_lstPendingRelayContexts.end();) {
		if (dwNow - it->dwInserted > 30000) { // 30 seconds timeout
			it = m_lstPendingRelayContexts.erase(it);
		} else {
			++it;
		}
	}
}

bool CClientList::IsKadBuddySearchActive() const
{
	// Check if a lookup for FINDSERVINGBUDDY is currently active in the Kademlia search manager.
	return Kademlia::CKademlia::IsRunning() && 
		Kademlia::CSearchManager::AlreadySearchingFor(Kademlia::CUInt128(true).Xor(Kademlia::CKademlia::GetPrefs()->GetKadID()));
}

bool CClientList::IsEServerBuddySearchActive() const
{
	// m_bEServerBuddyMagicSearchPrimed is set to true when the magic source probe cycle starts
	// and reset when a buddy is connected or the state is cleared.
	return m_bEServerBuddyMagicSearchPrimed || m_bEServerBuddyMagicSearchInFlight;
}
