/*
This file is part of eMule AI

Copyright (C)2003 Barry Dunne (https://www.emule-project.net)
Copyright (C)2004-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
Copyright (C)2026 eMule AI

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

// Note To Mods //
/*
Please do not change anything here and release it.
There is going to be a new forum created just for the Kademlia side of the client.
If you feel there is an error or a way to improve something, please
post it in the forum first and let us look at it. If it is a real improvement,
it will be added to the official client. Changing something without knowing
what all it does, can cause great harm to the network if released in mass form.
Any mod that changes anything within the Kademlia side will not be allowed to advertise
their client on the eMule forum.
*/

#include "stdafx.h"
#include "ClientList.h"
#include "DownloadQueue.h"
#include "emule.h"
#include "emuledlg.h"
#include "kademliawnd.h"
#include "KadSearchListCtrl.h"
#include "Log.h"
#include "Packets.h"
#include "partfile.h"
#include "SearchList.h"
#include "sharedfilelist.h"
#include "UpDownClient.h"
#include "KnownFileList.h"
#include "kademlia/io/ByteIO.h"
#include "kademlia/io/IOException.h"
#include "kademlia/kademlia/Defines.h"
#include "kademlia/kademlia/Entry.h"
#include "kademlia/kademlia/Indexed.h"
#include "kademlia/kademlia/Kademlia.h"
#include "kademlia/kademlia/Prefs.h"
#include "kademlia/kademlia/Search.h"
#include "kademlia/kademlia/SearchManager.h"
#include "kademlia/kademlia/UDPFirewallTester.h"
#include "kademlia/net/KademliaUDPListener.h"
#include "kademlia/routing/RoutingZone.h"
#include "kademlia/utils/KadClientSearcher.h"
#include "kademlia/utils/LookupHistory.h"
#include "eMuleAI/SafeKAD.h" // Node/contact filtering
#include "eMuleAI/FastKAD.h" // Response time estimates

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using namespace Kademlia;

CSearch::CSearch()
	: m_uTarget()
	, m_uClosestDistantFound()
	, m_pSearchTerm()
	, pNodeSpecialSearchRequester()
	, m_pucSearchTermsData()
	, m_uLastResponse(time(NULL))
	, m_tCreated(m_uLastResponse)
	, m_tPolicyLifetime(SEARCH_LIFETIME)
	, m_uPolicyTotalLimit(0)
	, m_uType(_UI32_MAX)
	, m_uAnswers()
	, m_uTotalRequestAnswers()
	, m_uKadPacketSent()
	, m_uTotalLoad()
	, m_uTotalLoadResponses()
	, m_uSearchID(_UI32_MAX)
	, m_uSearchTermsDataSize()
	, m_bStoping()
{
	m_pLookupHistory = new CLookupHistory();
	theApp.emuledlg->kademliawnd->searchList->SearchAdd(this);
}

CSearch::~CSearch()
{
	// remember the closest node we found and tried to contact (if any) during this search
	// for statistical calculations, but only if it's a certain type
	switch (m_uType) {
	case NODECOMPLETE:
	case FILE:
	case KEYWORD:
	case NOTES:
	case STOREFILE:
	case STOREKEYWORD:
	case STORENOTES:
	case FINDSOURCE: // maybe also exclude
		if (m_uClosestDistantFound != 0)
			CKademlia::StatsAddClosestDistance(m_uClosestDistantFound);
	default: // NODE, NODESPECIAL, NODEFWCHECKUDP, FINDSERVINGBUDDY
		break;
	}

	if (pNodeSpecialSearchRequester != NULL) {
		// inform requester that our search failed
		pNodeSpecialSearchRequester->KadSearchIPByNodeIDResult(KCSR_NOTFOUND, 0, 0);
		pNodeSpecialSearchRequester = NULL;
	}

	// Remove search from GUI
	theApp.emuledlg->kademliawnd->searchList->SearchRem(this);

	// delete and dereference search history (will delete itself if not used by the GUI)
	if (m_pLookupHistory != NULL) {
		m_pLookupHistory->SetSearchDeleted();
		m_pLookupHistory = NULL;
	}
	theApp.emuledlg->kademliawnd->UpdateSearchGraph(NULL);

	// Check if a source search is currently being done.
	CPartFile *pPartFile = theApp.downloadqueue->GetFileByKadFileSearchID(GetSearchID());

	// Reset the searchID if a source search is currently being done.
	if (pPartFile)
		pPartFile->SetKadFileSearchID(0);

	if (m_uType == NOTES) {
		CAbstractFile *pAbstractFile = theApp.knownfiles->FindKnownFileByID(CUInt128(GetTarget().GetData()).GetData());
		if (pAbstractFile != NULL)
			pAbstractFile->SetKadCommentSearchRunning(false);

		pAbstractFile = theApp.downloadqueue->GetFileByID(CUInt128(GetTarget().GetData()).GetData());
		if (pAbstractFile != NULL)
			pAbstractFile->SetKadCommentSearchRunning(false);

		theApp.searchlist->SetNotesSearchStatus(CUInt128(GetTarget().GetData()).GetData(), false);
	}

	// Add non-responding contacts to the problematic list
	for (ContactMap::iterator itContactMap = m_mapTried.begin(); itContactMap != m_mapTried.end(); ++itContactMap)
	{
		CUInt128 uDistance = itContactMap->first;
		if (m_mapResponded.count(uDistance) == 0)
		{
			CContact* pContact = itContactMap->second;
			safeKad.TrackProblematicNode(pContact->GetIPAddress(), pContact->GetUDPPort(), pContact->GetClientID());
		}
	}

	// Decrease the use count for any contacts that are in your contact list.
	for (ContactMap::const_iterator itInUseMap = m_mapInUse.begin(); itInUseMap != m_mapInUse.end(); ++itInUseMap)
		itInUseMap->second->DecUse();

	// Delete any temp contacts.
	for (ContactArray::const_iterator itContact = m_listDelete.begin(); itContact != m_listDelete.end(); ++itContact)
		if (!(*itContact)->InUse())
			delete *itContact;

	// Check if this search was contacting an overloaded node and adjust time of next time we use that node.
	if (CKademlia::IsRunning() && GetNodeLoad() > 20 && GetSearchType() == CSearch::STOREKEYWORD)
		Kademlia::CKademlia::GetIndexed()->AddLoad(GetTarget(), time(NULL) + (time_t)(DAY2S(7) * (GetNodeLoad() / 100.0)));
	delete[] m_pucSearchTermsData;

	CKademlia::GetUDPListener()->Free(m_pSearchTerm);
	m_pSearchTerm = NULL;
}

void CSearch::Go()
{
	// Start with a lot of possible contacts, this is a fallback in case search stalls due to dead contacts
	if (m_mapPossible.empty()) {
		CUInt128 uDistance(CKademlia::GetPrefs()->GetKadID());
		uDistance.Xor(m_uTarget);
		CKademlia::GetRoutingZone()->GetClosestTo(3, m_uTarget, uDistance, 50, m_mapPossible, true, true);

		for (ContactMap::const_iterator itPossibleMap = m_mapPossible.begin(); itPossibleMap != m_mapPossible.end(); ++itPossibleMap)
			m_pLookupHistory->ContactReceived(itPossibleMap->second, NULL, itPossibleMap->first, false);
		theApp.emuledlg->kademliawnd->UpdateSearchGraph(m_pLookupHistory);
	}
	if (!m_mapPossible.empty()) {
		//Lets keep our contact list entries in mind to dec the inUse flag.
		for (ContactMap::const_iterator itPossibleMap = m_mapPossible.begin(); itPossibleMap != m_mapPossible.end(); ++itPossibleMap)
			m_mapInUse[itPossibleMap->first] = itPossibleMap->second;

		ASSERT(m_mapPossible.size() == m_mapInUse.size());

		// Take top ALPHA_QUERY to start search with.
		// Skip every second node for better search spread
		int iCount = (m_uType == NODE) ? 1 : min(2*ALPHA_QUERY, (int)m_mapPossible.size());

		ContactMap::const_iterator itPossibleMap = m_mapPossible.begin();
		// Send initial packets to start the search.
		// Skip every second node for better search spread
		for (int iIndex = 0; iIndex < iCount; iIndex++)
		{
			if ((iIndex & 1) == 0)
			{
				CContact* const pContact = itPossibleMap->second;
				// Move to tried
				m_mapTried[itPossibleMap->first] = pContact;
				// Send the KadID so other side can check if I think it has the right KadID. (Safety net)
				// Send request
				SendFindValue(pContact);
				// Keep track of the of contacts being asked and when to time out
				m_mapTimePending[itPossibleMap->first] = clock();
			}
			++itPossibleMap;
		}
	}
	// Update search for the GUI
	theApp.emuledlg->kademliawnd->searchList->SearchRef(this);
}

//If we allow about a 15 sec delay before deleting, we won't miss a lot of delayed returning packets.
void CSearch::PrepareToStop()
{
	// Check if already stopping.
	if (m_bStoping)
		return;

	// Adjust created time so that search will be deleted within 15 seconds.
	// This gives late results time to be processed.
	m_tCreated = time(NULL) - m_tPolicyLifetime + SEC(15);
	m_bStoping = true;

	if (m_pLookupHistory != NULL)
		m_pLookupHistory->SetSearchStopped();

	//Update search within GUI.
	theApp.emuledlg->kademliawnd->searchList->SearchRef(this);
	if (m_pLookupHistory != NULL)
		theApp.emuledlg->kademliawnd->UpdateSearchGraph(m_pLookupHistory);
}

void CSearch::JumpStart(bool force)
{
	// If we ran out of contacts, stop search.
	if (m_mapPossible.empty() && m_mapTimePending.empty()) { // There might popup some new ones if there are some pending requests
		if (TryRequestMoreResultsOnStall())
			return;
		PrepareToStop();
		return;
	}

	if (m_uType == NODE) // Never jump start single node lookups
		return;

	// Clean up contacts being asked after time out
	for (std::map<CUInt128, clock_t>::iterator itTimePending = m_mapTimePending.begin(); itTimePending != m_mapTimePending.end();) {
		clock_t const timeElapsed = clock() - itTimePending->second;
		clock_t	timeMaxPending = fastKad.GetEstMaxResponseTime();
		if (m_uType == STOREFILE || m_uType == STOREKEYWORD || m_uType == STORENOTES)
			timeMaxPending = SEC2MS(3); // Use standard timeout for background store operations
		if (timeElapsed >= timeMaxPending) {
			// If we did time out searching this contact we don't consider it as a candidate anymore!
			if (m_mapResponded.count(itTimePending->first) == 0)
				m_mapCandidates.erase(itTimePending->first);
			m_mapTimePending.erase(itTimePending++); // Reduce the pending count
		} else
			++itTimePending;
	}

	if (m_mapPossible.empty() && m_mapTimePending.empty()) {
		if (TryRequestMoreResultsOnStall())
			return;
		PrepareToStop();
		return;
	}

	// Check if pending responses are less than the amount we want to store then go on with storing
	size_t nStoreCnt = min(ALPHA_QUERY, max(1, (10 - min(10, m_mapStored.size()))));

	// Wait for the pending contacts to become ready, so the store operation can comence
	if (m_mapTimePending.size() >= nStoreCnt)
	{
		if (force && m_mapTimePending.size() < 2 * ALPHA_QUERY)
			nStoreCnt = 1;
		else
			return;
	}
	else
		nStoreCnt -= m_mapTimePending.size();

	bool bTried = false;

	size_t nCount = 0, nPending = 0, nResponded = 0, nStored = 0, nStoring = 0, nFailed = 0;
	CContact* pBestContact = nullptr;
	for (ContactMap::iterator itPossible = m_mapPossible.begin(); itPossible != m_mapPossible.end();)
	{
		CContact* pContact = itPossible->second;
		++nCount;
		if (m_mapTried.count(itPossible->first) > 0)
		{
			if (m_mapTimePending.count(itPossible->first) > 0)
			{
				if (m_mapStoring.count(itPossible->first) > 0)
				{
					if (m_mapCandidates.count(itPossible->first) == 0)
						return; // This is a store operation that is pending and not a candidate so wait it out!
					++nStoring;
				}
				++nPending;
			}
			else
			{
				if (m_mapResponded.count(itPossible->first) > 0)
				{
					if (m_mapStored.count(itPossible->first) > 0)
					{
						++nStored;
					}
					else if (m_mapStoring.count(itPossible->first) > 0)
					{
						++nStoring;
					}
					else if (m_mapFailed.count(itPossible->first) > 0)
					{
						++nFailed;
					}
					else if (m_mapCandidates.count(itPossible->first) > 0)
					{
						// We found a candidate so try storing at once
						if (StorePacket(pContact))
						{
							m_mapStoring[itPossible->first] = pContact;
							m_mapTimePending[itPossible->first] = clock();
							++nStoring;
							++nPending;
							--nStoreCnt;
							bTried = true;
							if (nStoreCnt == 0)
								return;
						}
						else
							m_mapFailed[itPossible->first] = pContact;
					}
					else
					{
						// We found a contact that might be useful for storing
						if (pBestContact == nullptr)
							pBestContact = pContact;
						++nResponded;
					}
				}
			}
		}
		else
		{
			// Check this contact first before storing
			// Send the KadID so other side can check if I think it has the right KadID. (Safety net)
			SendFindValue(pContact);
			m_mapTried[itPossible->first] = pContact;
			m_mapTimePending[itPossible->first] = clock();
			++nPending;
			--nStoreCnt;
			bTried = true;
			if (nStoreCnt == 0)
				return;
		}

		// Has maximum storing count been reached? If so force stop!
		if (m_uType == STOREFILE || m_uType == STOREKEYWORD || m_uType == STORENOTES)
		{
			if (nStored + nStoring + nFailed >= 15)
			{
				m_mapPossible.empty();
				return;
			}
		}
		else
		{
			if (nStored + nStoring + nFailed >= 10)
			{
				m_mapPossible.empty();
				return;
			}
		}

		// Ready for storing?
		if (nResponded >= 3 && nPending < nStoreCnt)
		{
			if (pBestContact != nullptr)
			{
				// We have a contact we may use for storing
				CUInt128 uDistance(pBestContact->GetClientID().Xor(m_uTarget));
				if (StorePacket(pBestContact))
				{
					m_mapStoring[uDistance] = pBestContact;
					m_mapTimePending[uDistance] = clock();
					return;
				}
				else
					m_mapFailed[uDistance] = pBestContact;
			}
		}

		++itPossible;
	}

	// We ran out of nodes so if there isn't any pending we try to store any node we might have found
	if (nPending == 0 && pBestContact != nullptr)
	{
		// We have a contact we may use for storing
		CUInt128 uDistance(pBestContact->GetClientID().Xor(m_uTarget));
		if (StorePacket(pBestContact))
		{
			m_mapStoring[uDistance] = pBestContact;
			m_mapTimePending[uDistance] = clock();
			return;
		}
		else
			m_mapFailed[uDistance] = pBestContact;
	}

	// If out of contacts to try, clear possible to force stop
	if (bTried == false) {
		if (TryRequestMoreResultsOnStall())
			return;
		m_mapPossible.clear();
	}
}

void CSearch::ProcessResponse(uint32 uFromIP, uint16 uFromPort, const ContactArray &rlistResults)
{
	// Remember the contacts to be deleted when finished
	m_listDelete.insert(m_listDelete.end(), rlistResults.begin(), rlistResults.end());


	// Responses with empty contact list is considered problematic
	if (rlistResults.size() == 0)
	{
		DebugLogWarning(_T("Node %s did not send any contacts or they were filtered, ignoring response"), ipstr(ntohl(uFromIP)));
		ProcessInvalidResponse(uFromIP, uFromPort);
		return;
	}

	//Find contact that is responding.
	CUInt128 uFromDistance;
	CContact* pFromContact = nullptr;
	for (ContactMap::const_iterator itTriedMap = m_mapTried.begin(); itTriedMap != m_mapTried.end(); ++itTriedMap) {
		CContact *pTmpContact = itTriedMap->second;
		if ((pTmpContact->GetIPAddress() == uFromIP) && (pTmpContact->GetUDPPort() == uFromPort)) {
			uFromDistance = itTriedMap->first;
			pFromContact = pTmpContact;
			break;
		}
	}

	// Moved to CKademliaUDPListener

	// This is a useful response, so recalculate maximum response time estimate 
	if (pFromContact != nullptr) {
		if (m_mapTimePending.count(uFromDistance) == 1) {
			fastKad.AddResponseTime(uFromIP, clock() - m_mapTimePending[uFromDistance]);
			m_mapTimePending.erase(uFromDistance);
		}
		// Test if this node is bad (can have changed since we sent the request), otherwise we now know that the node likes this ID and will be the only one we know accept
		if (safeKad.IsBadNode(uFromIP, uFromPort, pFromContact->GetClientID(), pFromContact->GetVersion(), true, false))
			return;
	}

	if (m_uType == NODE || m_uType == NODEFWCHECKUDP) {
		// Note we got an answer
		++m_uAnswers;

		if (m_uType == NODE) {
			// Not interested in responses for FIND_NODE.
			// Once we get the results, we stop the search.
			// These contacts are added to contacts by UDPListener.

			// Add contacts to the History for GUI
			for (ContactArray::const_iterator itResultsList = rlistResults.begin(); itResultsList != rlistResults.end(); ++itResultsList) {
				CUInt128 uDistance((*itResultsList)->GetClientID().Xor(m_uTarget));
				m_pLookupHistory->ContactReceived(*itResultsList, pFromContact, uDistance, uDistance < uFromDistance, true);
			}
			theApp.emuledlg->kademliawnd->UpdateSearchGraph(m_pLookupHistory);
			// We clear the possible list to force the search to stop.
			// We do this so the user has time to see the visualised results.
			m_mapPossible.clear();
		} else {
			// Results are not passed to the search and not much point in changing this,
			// but make sure we show on the graph that the contact responded
			m_pLookupHistory->ContactReceived(NULL, pFromContact, CUInt128(), true);
			theApp.emuledlg->kademliawnd->UpdateSearchGraph(m_pLookupHistory);
		}

		// Update search in the GUI.
		theApp.emuledlg->kademliawnd->searchList->SearchRef(this);
		return;
	}

	try {
		if (pFromContact != NULL) {
			bool bProvidedCloserContacts = false;
			std::map<uint32, uint32> mapReceivedIPs;
			std::map<uint32, uint32> mapReceivedSubnets;
			mapReceivedIPs[uFromIP] = 1; // A node is not allowed to answer with contacts to itself
			mapReceivedSubnets[uFromIP & ~0xFF] = 1;
			// Loop through their responses
			for (ContactArray::const_iterator itContact = rlistResults.begin(); itContact != rlistResults.end(); ++itContact) {
				// Get next result
				CContact *pContact = *itContact;

				// Calc distance from this result to the target.
				CUInt128 uDistance(pContact->GetClientID());
				uDistance.Xor(m_uTarget);

				// Make sure we don't count references to self
				if (uDistance == uFromDistance)
					continue;

				if (uDistance < uFromDistance)
					bProvidedCloserContacts = true;

				m_pLookupHistory->ContactReceived(pContact, pFromContact, uDistance, bProvidedCloserContacts);
				theApp.emuledlg->kademliawnd->UpdateSearchGraph(m_pLookupHistory);


				// we only accept unique IPs in the answer, having multiple IDs pointing to one IP in the routing tables
				// is no longer allowed since 0.49a anyway
				if (mapReceivedIPs.find(pContact->GetIPAddress()) != mapReceivedIPs.end()) {
					DebugLogWarning(_T("Multiple KadIDs pointing to same IP (%s) in KADEMLIA(2)_RES answer - ignored, sent by %s")
						, (LPCTSTR)ipstr(pContact->GetNetIP()), (LPCTSTR)ipstr(pFromContact->GetNetIP()));
					continue;
				}

				mapReceivedIPs[pContact->GetIPAddress()] = 1;

				// and no more than 2 IPs from the same /24 block
				const uint32 subnetIP = pContact->GetIPAddress() & ~0xFF;
				std::map<uint32, uint32>::iterator it = IsLANIP(pContact->GetNetIP()) ? mapReceivedSubnets.end() : mapReceivedSubnets.find(subnetIP);
				if (it == mapReceivedSubnets.end())
					mapReceivedSubnets[subnetIP] = 1;
				else {
					if (it->second >= 2) {
						DebugLogWarning(_T("More than 2 KadIDs pointing to same Subnet (%s) in KADEMLIA(2)_RES answer - ignored, sent by %s")
							, (LPCTSTR)ipstr(htonl(subnetIP)), (LPCTSTR)ipstr(pFromContact->GetNetIP()));
						continue;
					}
					++it->second;
				}
				//Wait til the second reference before trying a contact
				if (m_mapPossible.count(uDistance) == 0) {
					if (m_mapTried.count(uDistance) == 0) {
						// Add to possible
						m_mapPossible[uDistance] = pContact;
						m_mapReferer[uDistance] = pFromContact;
					} else
						continue;
				} else if (m_mapCandidates.count(uDistance) == 0) {
					CContact* pPossibleContact = m_mapPossible[uDistance];
					if (!(pPossibleContact->GetIPAddress() == pContact->GetIPAddress() && pPossibleContact->GetUDPPort() == pContact->GetUDPPort())) {
						// Replace the possible contact with the new one as it is possible the first one was bogus!
						m_mapPossible[uDistance] = pContact;
						m_mapReferer[uDistance] = pFromContact;
						continue;
					}
					// Add to the candidates list
					m_mapCandidates[uDistance] = pPossibleContact;
					// Add the contact responding to candidate list as it appears to have valid routing and is likely to be very close to target
					m_mapCandidates[uFromDistance] = pFromContact;
					// Add also the previous contact refering to this candidate to the candidate list for the same reason as above
					if (m_mapReferer.count(uDistance) > 0) {
						CUInt128 uRefererDistance = m_mapReferer[uDistance]->GetClientID();
						uRefererDistance.Xor(m_uTarget);
						m_mapCandidates[uRefererDistance] = m_mapReferer[uDistance];
					}

					// Always try our candidate if it is closer, unless it has already been tried
					if (m_mapTried.count(uDistance) == 0 && uDistance < uFromDistance)
					{
						SendFindValue(pPossibleContact);
						m_mapTried[uDistance] = pPossibleContact;
						m_mapTimePending[uDistance] = clock();
					}
				} else if (m_mapPossible[uDistance]->GetIPAddress() == pContact->GetIPAddress() && m_mapPossible[uDistance]->GetUDPPort() == pContact->GetUDPPort()) {
					// Add the contact responding to candidate list as it appears to have valid routing and is likely to be very close to target
					m_mapCandidates[uFromDistance] = pFromContact;
				}
			}

			// Add to the list of contacts that responded
			m_mapResponded[uFromDistance] = bProvidedCloserContacts;

			// Complete node search, just increase the answers and update the GUI
			if (m_uType == NODECOMPLETE || m_uType == NODESPECIAL) {
				++m_uAnswers;
				theApp.emuledlg->kademliawnd->searchList->SearchRef(this);
			}
		}
	} catch (...) {
		AddDebugLogLine(false, _T("Exception in CSearch::ProcessResponse"));
	}

	JumpStart(true); // netfinity: Try to jumpstart (if needed!)
}

// Deal with unreachable contacts
void CSearch::ProcessUnreachableResponse(uint32 uFromIP, uint16 uFromPort)
{
	bool bFoundContact = false;

	try
	{
		//Find contact that is responding.
		for (std::map<CUInt128, clock_t>::const_iterator itPendingMap = m_mapTimePending.begin(); itPendingMap != m_mapTimePending.end();)
		{
			CUInt128 uFromDistance(itPendingMap->first);
			CContact* pFromContact = m_mapTried[uFromDistance];

			if ((pFromContact->GetIPAddress() == uFromIP) && (pFromContact->GetUDPPort() == uFromPort))
			{
				DebugLog(_T(__FUNCTION__) _T(": Contact could not be reached %s."), ipstr(ntohl(pFromContact->GetIPAddress()), pFromContact->GetUDPPort()));
				m_mapFailed[uFromDistance] = pFromContact;
				m_mapStoring.erase(uFromDistance);
				m_mapTimePending.erase(itPendingMap++);
				bFoundContact = true;

				safeKad.TrackProblematicNode(pFromContact->GetIPAddress(), pFromContact->GetUDPPort());
				break;
			}
			else
			{
				++itPendingMap;
			}
		}
	}
	catch (...)
	{
		AddDebugLogLine(false, _T("Exception in ") _T(__FUNCTION__));
	}

	// Try to jumpstart (if needed!)
	if (bFoundContact == true)
		JumpStart(true);
}

// Deal with problematic contacts 
void CSearch::ProcessInvalidResponse(uint32 uFromIP, uint16 uFromPort)
{
	bool bFoundContact = false;

	try
	{
		//Find contact that is responding.
		for (std::map<CUInt128, clock_t>::const_iterator itPendingMap = m_mapTimePending.begin(); itPendingMap != m_mapTimePending.end();)
		{
			CUInt128 uFromDistance(itPendingMap->first);
			CContact* pFromContact = m_mapTried[uFromDistance];

			if ((pFromContact->GetIPAddress() == uFromIP) && (pFromContact->GetUDPPort() == uFromPort))
			{
				DebugLog(_T(__FUNCTION__) _T(": Contact was ignored %s."), ipstr(ntohl(pFromContact->GetIPAddress()), pFromContact->GetUDPPort()));
				m_mapFailed[uFromDistance] = pFromContact;
				m_mapStoring.erase(uFromDistance);
				m_mapTimePending.erase(itPendingMap++);
				bFoundContact = true;

				safeKad.TrackProblematicNode(pFromContact->GetIPAddress(), pFromContact->GetUDPPort());
				break;
			}
			else
			{
				++itPendingMap;
			}
		}
	}
	catch (...)
	{
		AddDebugLogLine(false, _T("Exception in ") _T(__FUNCTION__));
	}

	// Try to jumpstart (if needed!)
	if (bFoundContact == true)
		JumpStart(true);
}

// Let the caller choose the contact to ask
bool CSearch::StorePacket(CContact* pFromContact)
{
	ASSERT(pFromContact != nullptr);
	CUInt128 uFromDistance(pFromContact->GetClientID());
	uFromDistance.Xor(m_uTarget);

	if (uFromDistance < m_uClosestDistantFound || m_uClosestDistantFound == 0)
		m_uClosestDistantFound = uFromDistance;
	// Make sure this is a valid Node to store too.
	if (uFromDistance.Get32BitChunk(0) > SEARCHTOLERANCE && !IsLANIP(pFromContact->GetNetIP()))
		return false;
	m_pLookupHistory->ContactAskedKeyword(pFromContact);
	theApp.emuledlg->kademliawnd->UpdateSearchGraph(m_pLookupHistory);
	// What kind of search are we doing?
	switch (m_uType) {
	case FILE:
		{
			CSafeMemFile m_pfileSearchTerms;
			m_pfileSearchTerms.WriteUInt128(m_uTarget);
			if (pFromContact->GetVersion() >= KADEMLIA_VERSION3_47b) {
				// Find file we are storing info about.
				uchar ucharFileid[MDX_DIGEST_SIZE];
				m_uTarget.ToByteArray(ucharFileid);
				CKnownFile *pFile = theApp.downloadqueue->GetFileByID(ucharFileid);
				if (!pFile) {
					PrepareToStop();
					break;
				}
				// JOHNTODO -- Add start position
				// Start Position range (0x0 to 0x7FFF)
				m_pfileSearchTerms.WriteUInt16(0);
				m_pfileSearchTerms.WriteUInt64(pFile->GetFileSize());
				if (thePrefs.GetDebugClientKadUDPLevel() > 0)
					DebugSend("KADEMLIA2_SEARCH_SOURCE_REQ", pFromContact->GetIPAddress(), pFromContact->GetUDPPort());
				if (pFromContact->GetVersion() >= KADEMLIA_VERSION6_49aBETA) {
					CUInt128 uClientID = pFromContact->GetClientID();
					CKademlia::GetUDPListener()->SendPacket(m_pfileSearchTerms, KADEMLIA2_SEARCH_SOURCE_REQ, pFromContact->GetIPAddress(), pFromContact->GetUDPPort(), pFromContact->GetUDPKey(), &uClientID);
				} else {
					CKademlia::GetUDPListener()->SendPacket(m_pfileSearchTerms, KADEMLIA2_SEARCH_SOURCE_REQ, pFromContact->GetIPAddress(), pFromContact->GetUDPPort(), CKadUDPKey(), NULL);
					ASSERT(CKadUDPKey() == pFromContact->GetUDPKey());
				}
			} else {
				m_pfileSearchTerms.WriteUInt8(1);
				if (thePrefs.GetDebugClientKadUDPLevel() > 0)
					DebugSend("KADEMLIA_SEARCH_REQ(File)", pFromContact->GetIPAddress(), pFromContact->GetUDPPort());
				CKademlia::GetUDPListener()->SendPacket(m_pfileSearchTerms, KADEMLIA_SEARCH_REQ, pFromContact->GetIPAddress(), pFromContact->GetUDPPort(), CKadUDPKey(), NULL);
			}
			// Inc total request answers
			++m_uTotalRequestAnswers;
			// Update search in the GUI
			theApp.emuledlg->kademliawnd->searchList->SearchRef(this);
		}
		break;
	case KEYWORD:
		{
			//JOHNTODO -- We cannot pre-create these packets as we do not know
			// beforehand if we are talking to Kad1.0 or Kad2.0
			CSafeMemFile m_pfileSearchTerms;
			m_pfileSearchTerms.WriteUInt128(m_uTarget);
			if (pFromContact->GetVersion() >= KADEMLIA_VERSION3_47b) {
				if (m_uSearchTermsDataSize == 0) {
					// JOHNTODO - Need to add ability to change start position.
					// Start position range (0x0 to 0x7fff)
					m_pfileSearchTerms.WriteUInt16(0x0000ui16);
				} else {
					// JOHNTODO - Need to add ability to change start position.
					// Start position range (0x8000 to 0xffff)
					m_pfileSearchTerms.WriteUInt16(0x8000ui16);
					m_pfileSearchTerms.Write(m_pucSearchTermsData, m_uSearchTermsDataSize);
				}
			} else {
				if (m_uSearchTermsDataSize == 0) {
					m_pfileSearchTerms.WriteUInt8(0);
					// We send this extra byte to flag we handle large files.
					m_pfileSearchTerms.WriteUInt8(0);
				} else {
					// Set to 2 to flag we handle large files.
					m_pfileSearchTerms.WriteUInt8(2);
					m_pfileSearchTerms.Write(m_pucSearchTermsData, m_uSearchTermsDataSize);
				}
			}

			if (pFromContact->GetVersion() >= KADEMLIA_VERSION6_49aBETA) {
				if (thePrefs.GetDebugClientKadUDPLevel() > 0)
					DebugSend("KADEMLIA2_SEARCH_KEY_REQ", pFromContact->GetIPAddress(), pFromContact->GetUDPPort());
				CUInt128 uClientID = pFromContact->GetClientID();
				CKademlia::GetUDPListener()->SendPacket(m_pfileSearchTerms, KADEMLIA2_SEARCH_KEY_REQ, pFromContact->GetIPAddress(), pFromContact->GetUDPPort(), pFromContact->GetUDPKey(), &uClientID);
			} else if (pFromContact->GetVersion() >= KADEMLIA_VERSION3_47b) {
				if (thePrefs.GetDebugClientKadUDPLevel() > 0)
					DebugSend("KADEMLIA2_SEARCH_KEY_REQ", pFromContact->GetIPAddress(), pFromContact->GetUDPPort());
				CKademlia::GetUDPListener()->SendPacket(m_pfileSearchTerms, KADEMLIA2_SEARCH_KEY_REQ, pFromContact->GetIPAddress(), pFromContact->GetUDPPort(), CKadUDPKey(), NULL);
				ASSERT(CKadUDPKey() == pFromContact->GetUDPKey());
			} else {
				if (thePrefs.GetDebugClientKadUDPLevel() > 0)
					DebugSend("KADEMLIA_SEARCH_REQ(KEYWORD)", pFromContact->GetIPAddress(), pFromContact->GetUDPPort());
				CKademlia::GetUDPListener()->SendPacket(m_pfileSearchTerms, KADEMLIA_SEARCH_REQ, pFromContact->GetIPAddress(), pFromContact->GetUDPPort(), CKadUDPKey(), NULL);
			}
			// Inc total request answers
			++m_uTotalRequestAnswers;
			// Update search in the GUI
			theApp.emuledlg->kademliawnd->searchList->SearchRef(this);
		}
		break;
	case NOTES:
		{
			// Write complete packet
			CSafeMemFile m_pfileSearchTerms;
			m_pfileSearchTerms.WriteUInt128(m_uTarget);

			if (pFromContact->GetVersion() >= KADEMLIA_VERSION3_47b) {
				// Find file we are storing info about.
				uchar ucharFileid[MDX_DIGEST_SIZE];
				m_uTarget.ToByteArray(ucharFileid);
				CKnownFile *pFile = theApp.sharedfiles->GetFileByID(ucharFileid);
				if (!pFile) {
					PrepareToStop();
					break;
				}
				m_pfileSearchTerms.WriteUInt64(pFile->GetFileSize());
				if (thePrefs.GetDebugClientKadUDPLevel() > 0)
					DebugSend("KADEMLIA2_SEARCH_NOTES_REQ", pFromContact->GetIPAddress(), pFromContact->GetUDPPort());
				if (pFromContact->GetVersion() >= KADEMLIA_VERSION6_49aBETA) {
					CUInt128 uClientID = pFromContact->GetClientID();
					CKademlia::GetUDPListener()->SendPacket(m_pfileSearchTerms, KADEMLIA2_SEARCH_NOTES_REQ, pFromContact->GetIPAddress(), pFromContact->GetUDPPort(), pFromContact->GetUDPKey(), &uClientID);
				} else {
					CKademlia::GetUDPListener()->SendPacket(m_pfileSearchTerms, KADEMLIA2_SEARCH_NOTES_REQ, pFromContact->GetIPAddress(), pFromContact->GetUDPPort(), CKadUDPKey(), NULL);
					ASSERT(CKadUDPKey() == pFromContact->GetUDPKey());
				}
			} else {
				m_pfileSearchTerms.WriteUInt128(CKademlia::GetPrefs()->GetKadID());
				if (thePrefs.GetDebugClientKadUDPLevel() > 0)
					DebugSend("KADEMLIA_SEARCH_NOTES_REQ", pFromContact->GetIPAddress(), pFromContact->GetUDPPort());
				CKademlia::GetUDPListener()->SendPacket(m_pfileSearchTerms, KADEMLIA_SEARCH_NOTES_REQ, pFromContact->GetIPAddress(), pFromContact->GetUDPPort(), CKadUDPKey(), NULL);
			}
			// Inc total request answers
			++m_uTotalRequestAnswers;
			// Update search in the GUI
			theApp.emuledlg->kademliawnd->searchList->SearchRef(this);
		}
		break;
	case STOREFILE:
		{
			// Try to store yourself as a source to a Node.
			// As a safeguard, check to see if we already stored to the Max Nodes
			if (m_uAnswers > SEARCHSTOREFILE_TOTAL) {
				PrepareToStop();
				break;
			}

			// Find the file we are trying to store as a source too.
			uchar ucharFileid[MDX_DIGEST_SIZE];
			m_uTarget.ToByteArray(ucharFileid);
			CKnownFile *pFile = theApp.sharedfiles->GetFileByID(ucharFileid);
			if (!pFile) {
				PrepareToStop();
				break;
			}

			// We set this mostly for GUI response.
			SetGUIName((CStringW)pFile->GetFileName());

			// Get our clientID for the packet.
			CUInt128 uID(CKademlia::GetPrefs()->GetClientHash());

			//We can use type for different types of sources.
			//1 HighID Sources.
			//2 cannot be used as older clients will not work.
			//3 Firewalled Kad Source.
			//4 >4GB file HighID Source.
			//5 >4GB file Firewalled Kad source.
			//6 Firewalled Source with Direct Callback (supports >4GB)

			TagList listTag;
			if (theApp.IsFirewalled()) {
				bool bDirectCallback = (Kademlia::CKademlia::IsRunning() && !Kademlia::CUDPFirewallTester::IsFirewalledUDP(true) && Kademlia::CUDPFirewallTester::IsVerified());
				if (!bDirectCallback && !theApp.clientlist->GetServingBuddy()) {
					// We are firewalled, no direct callback and no serving buddy. Stop everything.
					PrepareToStop();
					break;
				}

				if (bDirectCallback) {
					// firewalled, but direct UDP callback is possible without buddies
					listTag.push_back(new CKadTagUInt(TAG_SOURCETYPE, 6));
					listTag.push_back(new CKadTagUInt(TAG_SOURCEPORT, thePrefs.GetPort()));
					if (!CKademlia::GetPrefs()->GetUseExternKadPort())
						listTag.push_back(new CKadTagUInt16(TAG_SOURCEUPORT, CKademlia::GetPrefs()->GetInternKadPort()));
					if (pFromContact->GetVersion() >= KADEMLIA_VERSION2_47a)
						listTag.push_back(new CKadTagUInt(TAG_FILESIZE, pFile->GetFileSize()));
				} else { // We are firewalled, but do have a serving buddy.
					// We send the ID to our serving buddy so they can do a callback.
					CUInt128 uServingBuddyID(true);
					uServingBuddyID.Xor(CKademlia::GetPrefs()->GetKadID());
					listTag.push_back(new CKadTagUInt8(TAG_SOURCETYPE, (pFile->GetFileSize() > OLD_MAX_EMULE_FILE_SIZE ? 5 : 3)));
					listTag.push_back(new CKadTagUInt(TAG_SERVERIP, theApp.clientlist->GetServingBuddy()->GetIP().ToUInt32(false)));
					listTag.push_back(new CKadTagUInt(TAG_SERVERPORT, theApp.clientlist->GetServingBuddy()->GetUDPPort()));
					listTag.push_back(new CKadTagStr(TAG_SERVINGBUDDYHASH, (CStringW)md4str(uServingBuddyID.GetData())));
					listTag.push_back(new CKadTagUInt(TAG_SOURCEPORT, thePrefs.GetPort()));
					if (!CKademlia::GetPrefs()->GetUseExternKadPort())
						listTag.push_back(new CKadTagUInt16(TAG_SOURCEUPORT, CKademlia::GetPrefs()->GetInternKadPort()));

					if (pFromContact->GetVersion() >= KADEMLIA_VERSION2_47a)
						listTag.push_back(new CKadTagUInt(TAG_FILESIZE, pFile->GetFileSize()));

					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal: Publish] file=%s, type=%u, buddy=%s:%u, sbid=%s\n"), (LPCTSTR)pFile->GetFileName(), (unsigned)(pFile->GetFileSize() > OLD_MAX_EMULE_FILE_SIZE ? 5 : 3), (LPCTSTR)ipstr(theApp.clientlist->GetServingBuddy()->GetIP()), theApp.clientlist->GetServingBuddy()->GetUDPPort(), (LPCTSTR)md4str(uServingBuddyID.GetData()));
				}
			} else {
				// We are not firewalled.
				if (pFile->GetFileSize() > OLD_MAX_EMULE_FILE_SIZE)
					listTag.push_back(new CKadTagUInt(TAG_SOURCETYPE, 4));
				else
					listTag.push_back(new CKadTagUInt(TAG_SOURCETYPE, 1));
				listTag.push_back(new CKadTagUInt(TAG_SOURCEPORT, thePrefs.GetPort()));
				if (!CKademlia::GetPrefs()->GetUseExternKadPort())
					listTag.push_back(new CKadTagUInt16(TAG_SOURCEUPORT, CKademlia::GetPrefs()->GetInternKadPort()));

				if (pFromContact->GetVersion() >= KADEMLIA_VERSION2_47a)
					listTag.push_back(new CKadTagUInt(TAG_FILESIZE, pFile->GetFileSize()));
			}

			listTag.push_back(new CKadTagUInt8(TAG_ENCRYPTION, CKademlia::GetPrefs()->GetMyConnectOptions(true, true)));

			if (!theApp.GetPublicIPv6().IsNull()) {
				CUInt128 IPv6(theApp.GetPublicIPv6().Data());
				listTag.push_back(new CKadTagStr(TAG_IPV6, IPv6.ToHexString()));
			}

			if (theApp.clientlist->GetServingBuddy() && !theApp.clientlist->GetServingBuddy()->GetIP().IsNull() && theApp.clientlist->GetServingBuddy()->GetIP().GetType() == CAddress::IPv6) {
				CUInt128 IPv6(theApp.clientlist->GetServingBuddy()->GetIP().Data());
				listTag.push_back(new CKadTagStr(TAG_SERVINGBUDDYIPV6, IPv6.ToHexString()));
			}

			// Send packet
			CKademlia::GetUDPListener()->SendPublishSourcePacket(pFromContact, m_uTarget, uID, listTag);
			// Inc total request answers
			++m_uTotalRequestAnswers;
			// Update search in the GUI
			theApp.emuledlg->kademliawnd->searchList->SearchRef(this);
			// Delete all tags.
			deleteTagListEntries(listTag);
		}
		break;
	case STOREKEYWORD:
		{
			// Try to store keywords to a Node.
			INT_PTR iCount = m_listFileIDs.GetSize();
			// As a safeguard, check to see if we already stored to the Max Nodes
			if (iCount <= 0 || m_uAnswers > SEARCHSTOREKEYWORD_TOTAL) {
				PrepareToStop();
				break;
			}
			if (iCount > 150)
				iCount = 150;

			for (INT_PTR i = 0; i < iCount;) {
				uchar ucharFileid[MDX_DIGEST_SIZE];
				uint16 iPacketCount = 0;
				byte byPacket[1024 * 50];
				CByteIO byIO(byPacket, sizeof(byPacket));
				byIO.WriteUInt128(m_uTarget);
				byIO.WriteUInt16(0); // Will be corrected before sending.
				for (; iPacketCount < 50 && i < m_listFileIDs.GetSize(); ++i) {
					const CUInt128 &iID(m_listFileIDs[i]);
					iID.ToByteArray(ucharFileid);
					CKnownFile *pFile = theApp.sharedfiles->GetFileByID(ucharFileid);
					if (pFile) {
						--iCount;
						++iPacketCount;
						byIO.WriteUInt128(iID);
						PreparePacketForTags(&byIO, pFile, pFromContact->GetVersion());
					}
				}

				// Correct file count.
				uint32 current_pos = byIO.GetUsed();
				byIO.Seek(16);
				byIO.WriteUInt16(iPacketCount);
				byIO.Seek(current_pos);

				// Send packet
				if (pFromContact->GetVersion() >= KADEMLIA_VERSION6_49aBETA) {
					if (thePrefs.GetDebugClientKadUDPLevel() > 0)
						DebugSend("KADEMLIA2_PUBLISH_KEY_REQ", pFromContact->GetIPAddress(), pFromContact->GetUDPPort());
					CUInt128 uClientID = pFromContact->GetClientID();
					CKademlia::GetUDPListener()->SendPacket(byPacket, (uint32)(sizeof byPacket - byIO.GetAvailable()), KADEMLIA2_PUBLISH_KEY_REQ, pFromContact->GetIPAddress(), pFromContact->GetUDPPort(), pFromContact->GetUDPKey(), &uClientID);
				} else if (pFromContact->GetVersion() >= KADEMLIA_VERSION2_47a) {
					if (thePrefs.GetDebugClientKadUDPLevel() > 0)
						DebugSend("KADEMLIA2_PUBLISH_KEY_REQ", pFromContact->GetIPAddress(), pFromContact->GetUDPPort());
					CKademlia::GetUDPListener()->SendPacket(byPacket, (uint32)(sizeof byPacket - byIO.GetAvailable()), KADEMLIA2_PUBLISH_KEY_REQ, pFromContact->GetIPAddress(), pFromContact->GetUDPPort(), CKadUDPKey(), NULL);
					ASSERT(CKadUDPKey() == pFromContact->GetUDPKey());
				} else
					ASSERT(0);
			}
			// Inc total request answers
			++m_uTotalRequestAnswers;
			// Update search in the GUI
			theApp.emuledlg->kademliawnd->searchList->SearchRef(this);
		}
		break;
	case STORENOTES:
		{
			// Find file we are storing info about.
			uchar ucharFileid[MDX_DIGEST_SIZE];
			m_uTarget.ToByteArray(ucharFileid);
			CKnownFile *pFile = theApp.sharedfiles->GetFileByID(ucharFileid);
			if (!pFile) {
				PrepareToStop();
				break;
			}
			byte byPacket[1024 * 2];
			CByteIO byIO(byPacket, sizeof byPacket);

			// Send the Hash of the file we are storing info about.
			byIO.WriteUInt128(m_uTarget);
			// Send our ID with the info.
			byIO.WriteUInt128(CKademlia::GetPrefs()->GetKadID());

			// Create our taglist
			TagList listTag;
			listTag.push_back(new CKadTagStr(TAG_FILENAME, pFile->GetFileName()));
			if (pFile->GetFileRating() > 0)
				listTag.push_back(new CKadTagUInt(TAG_FILERATING, pFile->GetFileRating()));
			if (!pFile->GetFileComment().IsEmpty())
				listTag.push_back(new CKadTagStr(TAG_DESCRIPTION, pFile->GetFileComment()));
			if (pFromContact->GetVersion() >= KADEMLIA_VERSION2_47a)
				listTag.push_back(new CKadTagUInt(TAG_FILESIZE, pFile->GetFileSize()));
			byIO.WriteTagList(listTag);

			// Send packet
			if (pFromContact->GetVersion() >= KADEMLIA_VERSION6_49aBETA) {
				if (thePrefs.GetDebugClientKadUDPLevel() > 0)
					DebugSend("KADEMLIA2_PUBLISH_NOTES_REQ", pFromContact->GetIPAddress(), pFromContact->GetUDPPort());
				CUInt128 uClientID = pFromContact->GetClientID();
				CKademlia::GetUDPListener()->SendPacket(byPacket, (uint32)(sizeof byPacket - byIO.GetAvailable()), KADEMLIA2_PUBLISH_NOTES_REQ, pFromContact->GetIPAddress(), pFromContact->GetUDPPort(), pFromContact->GetUDPKey(), &uClientID);
			} else if (pFromContact->GetVersion() >= KADEMLIA_VERSION2_47a) {
				if (thePrefs.GetDebugClientKadUDPLevel() > 0)
					DebugSend("KADEMLIA2_PUBLISH_NOTES_REQ", pFromContact->GetIPAddress(), pFromContact->GetUDPPort());
				CKademlia::GetUDPListener()->SendPacket(byPacket, (uint32)(sizeof byPacket - byIO.GetAvailable()), KADEMLIA2_PUBLISH_NOTES_REQ, pFromContact->GetIPAddress(), pFromContact->GetUDPPort(), CKadUDPKey(), NULL);
				ASSERT(CKadUDPKey() == pFromContact->GetUDPKey());
			} else
				ASSERT(0);
			// Inc total request answers
			++m_uTotalRequestAnswers;
			// Update search in the GUI
			theApp.emuledlg->kademliawnd->searchList->SearchRef(this);
			// Delete all tags.
			deleteTagListEntries(listTag);
		}
		break;
			case FINDSERVINGBUDDY:
			{
				// Send a serving buddy request as we are firewalled.
				// As a safe guard, check to see if we already requested the Max Nodes
				if (m_uAnswers > SEARCHFINDSERVINGBUDDY_TOTAL) {
					PrepareToStop();
					break;
				}

				CSafeMemFile m_pfileSearchTerms;
				// Send the ID we used to find our serving buddy. Used for checks later and allows users to callback someone if they change buddies.
				m_pfileSearchTerms.WriteUInt128(m_uTarget);
				// Send client hash so they can do a callback.
				m_pfileSearchTerms.WriteUInt128(CKademlia::GetPrefs()->GetClientHash());
				// Send client port so they can do a callback
				m_pfileSearchTerms.WriteUInt16(thePrefs.GetPort());
				// Optionally send our connect options (NAT-T bit) so the receiver can early filter. Older nodes ignore extra bytes.
				m_pfileSearchTerms.WriteUInt8(CKademlia::GetPrefs()->GetMyConnectOptions(true, true));

			// Do a keyword/source search request to this Node.
			// Send packet
			if (thePrefs.GetDebugClientKadUDPLevel() > 0)
				DebugSend("KADEMLIA_FINDSERVINGBUDDY_REQ", pFromContact->GetIPAddress(), pFromContact->GetUDPPort());
			if (pFromContact->GetVersion() >= KADEMLIA_VERSION6_49aBETA) {
				CUInt128 uClientID = pFromContact->GetClientID();
				CKademlia::GetUDPListener()->SendPacket(m_pfileSearchTerms, KADEMLIA_FINDSERVINGBUDDY_REQ, pFromContact->GetIPAddress(), pFromContact->GetUDPPort(), pFromContact->GetUDPKey(), &uClientID);
			} else {
				CKademlia::GetUDPListener()->SendPacket(m_pfileSearchTerms, KADEMLIA_FINDSERVINGBUDDY_REQ, pFromContact->GetIPAddress(), pFromContact->GetUDPPort(), CKadUDPKey(), NULL);
				ASSERT(CKadUDPKey() == pFromContact->GetUDPKey());
			}
			// Inc total request answers
			++m_uAnswers;
			// Update search in the GUI
			theApp.emuledlg->kademliawnd->searchList->SearchRef(this);
		}
		break;
	case FINDSOURCE:
		{
			// Try to find if this is a serving buddy to someone we want to contact.
			// As a safe guard, check to see if we already requested the Max Nodes
			if (m_uAnswers > SEARCHFINDSOURCE_TOTAL) {
				PrepareToStop();
				break;
			}

			CSafeMemFile fileIO(34);
			// This is the ID the person we want to contact used to find a served buddy.
			fileIO.WriteUInt128(m_uTarget);
			if (m_listFileIDs.GetSize() != 1)
				throwCStr(_T("Kademlia.CSearch.StorePacket: m_listFileIDs.size() != 1"));
			// Currently, we limit they type of callbacks for sources. We must know a file it person has for it to work.
			fileIO.WriteUInt128(m_listFileIDs[0]);
			// Send our port so the callback works.
			fileIO.WriteUInt16(thePrefs.GetPort());
			// Send packet
			if (thePrefs.GetDebugClientKadUDPLevel() > 0)
				DebugSend("KADEMLIA_CALLBACK_REQ", pFromContact->GetIPAddress(), pFromContact->GetUDPPort());
			if (pFromContact->GetVersion() >= KADEMLIA_VERSION6_49aBETA) {
				CUInt128 uClientID = pFromContact->GetClientID();
				CKademlia::GetUDPListener()->SendPacket(fileIO, KADEMLIA_CALLBACK_REQ, pFromContact->GetIPAddress(), pFromContact->GetUDPPort(), pFromContact->GetUDPKey(), &uClientID);
			} else {
				CKademlia::GetUDPListener()->SendPacket(fileIO, KADEMLIA_CALLBACK_REQ, pFromContact->GetIPAddress(), pFromContact->GetUDPPort(), CKadUDPKey(), NULL);
				ASSERT(CKadUDPKey() == pFromContact->GetUDPKey());
			}
			// Inc total request answers
			++m_uAnswers;
			// Update search in the GUI
			theApp.emuledlg->kademliawnd->searchList->SearchRef(this);
		}
		break;
	case NODESPECIAL:
		// we are looking for the IP of a given nodeid, so we just check if we 0 distance and if so, report the
		// tip to the requester
		if (uFromDistance == CUInt128(0ul)) {
			pNodeSpecialSearchRequester->KadSearchIPByNodeIDResult(KCSR_SUCCEEDED, pFromContact->GetNetIP(), pFromContact->GetTCPPort());
			pNodeSpecialSearchRequester = NULL;
			PrepareToStop();
		}
	}
	return true;
}

void CSearch::ProcessResult(const CUInt128 &uAnswer, TagList &rlistInfo, uint32 uFromIP, uint16 uFromPort)
{
	// We received a result, process it based on type.
	uint32 iAnswerBefore = m_uAnswers;
	switch (m_uType) {
	case FILE:
		ProcessResultFile(uAnswer, rlistInfo);
		break;
	case KEYWORD:
		ProcessResultKeyword(uAnswer, rlistInfo, uFromIP, uFromPort);
		break;
	case NOTES:
		ProcessResultNotes(uAnswer, rlistInfo);
	}
	if (iAnswerBefore < m_uAnswers) {
		m_pLookupHistory->ContactRespondedKeyword(uFromIP, uFromPort, m_uAnswers - iAnswerBefore);
		theApp.emuledlg->kademliawnd->UpdateSearchGraph(m_pLookupHistory);
	}
	// Update search for the GUI
	theApp.emuledlg->kademliawnd->searchList->SearchRef(this);

	// Mark contact as stored and jump start
	CUInt128 uFromDistance(uAnswer);
	uFromDistance.Xor(m_uTarget);

	m_mapStored[uFromDistance] = m_mapStoring[uFromDistance];
	m_mapStoring.erase(uFromDistance);
	m_mapTimePending.erase(uFromDistance);

	JumpStart(false);
}

void CSearch::ProcessResultFile(const CUInt128 &uAnswer, TagList &rlistInfo)
{
	// Process a possible source to a file.
	// Set of data we could receive from the result.
	uint8 uType = 0;
	uint32 uIP = 0;
	uint16 uTCPPort = 0;
	uint16 uUDPPort = 0;
	uint32 uServingBuddyIP = 0;
	uint16 uServingBuddyPort = 0;
	CUInt128 uServingBuddy;
	uint8 byCryptOptions = 0; // 0 = not supported
	CUInt128 IPv6;
	CUInt128 ServingBuddyIPv6;
	bool bIPv6 = false;
	bool bServingBuddyIPv6 = false;

	for (TagList::const_iterator itInfoList = rlistInfo.begin(); itInfoList != rlistInfo.end(); ++itInfoList) {
		const CKadTag &cTag(**itInfoList);
		if (cTag.m_name == TAG_SOURCETYPE)
			uType = (uint8)cTag.GetInt();
		else if (cTag.m_name == TAG_SOURCEIP)
			uIP = (uint32)cTag.GetInt();
		else if (cTag.m_name == TAG_SOURCEPORT)
			uTCPPort = (uint16)cTag.GetInt();
		else if (cTag.m_name == TAG_SOURCEUPORT)
			uUDPPort = (uint16)cTag.GetInt();
		else if (cTag.m_name == TAG_SERVERIP)
			uServingBuddyIP = (uint32)cTag.GetInt();
		else if (cTag.m_name == TAG_SERVERPORT)
			uServingBuddyPort = (uint16)cTag.GetInt();
		else if (cTag.m_name == TAG_SERVINGBUDDYHASH) {
			uchar ucharServingBuddyHash[MDX_DIGEST_SIZE];
			if (cTag.IsStr() && strmd4(cTag.GetStr(), ucharServingBuddyHash))
				md4cpy(uServingBuddy.GetDataPtr(), ucharServingBuddyHash);
			else
				AddDebugLogLine(DLP_DEFAULT, _T("+++ Invalid TAG_SERVINGBUDDYHASH tag in search result"));
		} else if (cTag.m_name == TAG_ENCRYPTION)
			byCryptOptions = (uint8)cTag.GetInt();
		else if (!cTag.m_name.Compare(TAG_IPV6)) {
			uchar ucharIPv6[16];
			if (cTag.IsStr() && strmd4(cTag.GetStr(), ucharIPv6)) {
				IPv6 = CUInt128(ucharIPv6);
				bIPv6 = true;
			} else
				AddDebugLogLine(DLP_DEFAULT, _T("+++ Invalid TAG_IPV6 tag in search result"));
		} else if (!cTag.m_name.Compare(TAG_SERVINGBUDDYIPV6)) {
			uchar ucharServingBuddyIPv6[16];
			if (cTag.IsStr() && strmd4(cTag.GetStr(), ucharServingBuddyIPv6)) {
				ServingBuddyIPv6 = CUInt128(ucharServingBuddyIPv6);
				bServingBuddyIPv6 = true;
			} else
				AddDebugLogLine(DLP_DEFAULT, _T("+++ Invalid TAG_SERVINGBUDDYIPV6 tag in search result"));
		}
	}

	// Process source based on its type. Currently only one method is needed to process all types.
	switch (uType) {
	case 1:
	case 3:
	case 4:
	case 5:
	case 6:
		++m_uAnswers;
		theApp.emuledlg->kademliawnd->searchList->SearchRef(this);
		theApp.downloadqueue->KademliaSearchFile(m_uSearchID, &uAnswer, &uServingBuddy, uType, uIP, uTCPPort, uUDPPort, uServingBuddyIP, uServingBuddyPort, byCryptOptions, bIPv6 ? &IPv6 : NULL , bServingBuddyIPv6 ? &ServingBuddyIPv6 : NULL);
		break;
	}
}

void CSearch::ProcessResultNotes(const CUInt128 &uAnswer, TagList &rlistInfo)
{
	// Process a received Note to a file.
	// Create a Note and set the ID's.
	CEntry cEntry;
	cEntry.m_uKeyID.SetValue(m_uTarget);
	cEntry.m_uSourceID.SetValue(uAnswer);

	// Loop through tags and pull wanted into. Currently we only keep Filename, Rating, Comment.
	for (TagList::iterator itInfoList = rlistInfo.begin(); itInfoList != rlistInfo.end(); ++itInfoList) {
		const CKadTag &cTag(**itInfoList);
		if (cTag.m_name == TAG_SOURCEIP)
			cEntry.m_uIP = (uint32)cTag.GetInt();
		else if (cTag.m_name == TAG_SOURCEPORT)
			cEntry.m_uTCPPort = (uint16)cTag.GetInt();
		else if (cTag.m_name == TAG_FILENAME || cTag.m_name == TAG_DESCRIPTION) {
			const CString &cfilter(thePrefs.GetCommentFilter());
			// Run the filter against the comment as well as against the filename since both values could be misused
			if (!cfilter.IsEmpty()) {
				CString strCommentLower(cTag.GetStr());
				// Verified Locale Dependency: Locale dependent string conversion (OK)
				strCommentLower.MakeLower();

				for (int iPos = 0; iPos >= 0;) {
					const CString &strFilter(cfilter.Tokenize(_T("|"), iPos));
					// comment filters are already in lower case, compare with temp. lower cased received comment
					if (!strFilter.IsEmpty() && strCommentLower.Find(strFilter) >= 0)
						return;
				}
			}
			if (cTag.m_name == TAG_FILENAME)
				cEntry.SetFileName(cTag.GetStr());
			else {
				ASSERT(cTag.m_name == TAG_DESCRIPTION);
				if (cTag.GetStr().GetLength() <= MAXFILECOMMENTLEN) {
					cEntry.AddTag(*itInfoList); //move pointer
					*itInfoList = NULL; //do not delete this tag
				} else
					cEntry.AddTag(new CKadTagStr(cTag.m_name, cTag.GetStr().Left(MAXFILECOMMENTLEN)));
			}
		} else if (cTag.m_name == TAG_FILERATING) {
			cEntry.AddTag(*itInfoList); //move pointer
			*itInfoList = NULL; //do not delete this tag
		}
	}

	uchar ucharFileid[MDX_DIGEST_SIZE];
	m_uTarget.ToByteArray(ucharFileid);

	// Add notes to any searches we have done.
	bool bAddedToSearches = theApp.searchlist->AddNotes(cEntry, ucharFileid);

	// Check if this hash is in our shared files.
	CAbstractFile *pFile = static_cast<CAbstractFile*>(theApp.sharedfiles->GetFileByID(ucharFileid));

	// If we didn't find a file in the shares, check in our download queue.
	if (!pFile)
		pFile = static_cast<CAbstractFile*>(theApp.downloadqueue->GetFileByID(ucharFileid));

	// If we have found a file, try to add the Note to the file -
	if ((pFile && pFile->AddNote(cEntry)) || bAddedToSearches) {
		// Inc the number of answers.
		++m_uAnswers;
		// Update the search in the GUI
		theApp.emuledlg->kademliawnd->searchList->SearchRef(this);
	}
}

void CSearch::ProcessResultKeyword(const CUInt128 &uAnswer, TagList &rlistInfo, uint32 uFromIP, uint16 uFromPort)
{
	// Find the contact who sent the answer - we need to know its protocol version
	// Special publish answer tags need to be filtered based on its remote protocol version,
	// because if an old node is not aware of those special tags, it doesn't know
	// it is not supposed to accept and store such tags on publish request, so a malicious
	// publisher could fake them and our remote node would relay them in answers
	CContact *pFromContact = NULL;
	for (ContactMap::const_iterator itTriedMap = m_mapTried.begin(); itTriedMap != m_mapTried.end(); ++itTriedMap) {
		CContact *pTmpContact = itTriedMap->second;
		if (pTmpContact->GetIPAddress() == uFromIP && pTmpContact->GetUDPPort() == uFromPort) {
			pFromContact = pTmpContact;
			break;
		}
	}
	uint8 uFromKadVersion;
	if (pFromContact == NULL) {
		uFromKadVersion = 0;
		DebugLogWarning(_T("Unable to find answering contact in ProcessResultKeyword - %s"), (LPCTSTR)ipstr(htonl(uFromIP)));
	} else
		uFromKadVersion = pFromContact->GetVersion();
	// Process the received keyword
	// Set of data we can use for the keyword result
	uint64 uSize = 0;
	CKadTagValueString sName;
	CKadTagValueString sType;
	CKadTagValueString sFormat;
	CKadTagValueString sArtist;
	CKadTagValueString sAlbum;
	CKadTagValueString sTitle;
	CKadTagValueString sCodec;
	uint32 uLength = 0;
	uint32 uBitrate = 0;
	uint32 uAvailability = 0;
	uint32 uPublishInfo = 0;
	CArray<CAICHHash> aAICHHashes;
	CArray<uint8, uint8> aAICHHashPopularity;
	// Flags that are set if we want this keyword.
	bool bFileName = false;
	bool bFileSize = false;

	for (TagList::const_iterator itInfoList = rlistInfo.begin(); itInfoList != rlistInfo.end(); ++itInfoList) {
		const CKadTag &cTag(**itInfoList);
		if (cTag.m_name == TAG_FILENAME) {
			// Set flag based on last tag we saw.
			sName = cTag.GetStr();
			bFileName = !sName.IsEmpty();
		} else if (cTag.m_name == TAG_FILESIZE) {
			if (cTag.IsBsob() && cTag.GetBsobSize() == 8)
				uSize = *((uint64*)cTag.GetBsob());
			else
				uSize = cTag.GetInt();

			// Set flag based on last tag we saw.
			bFileSize = (uSize > 0);
		} else if (cTag.m_name == TAG_FILETYPE)
			sType = cTag.GetStr();
		else if (cTag.m_name == TAG_FILEFORMAT)
			sFormat = cTag.GetStr();
		else if (cTag.m_name == TAG_MEDIA_ARTIST)
			sArtist = cTag.GetStr();
		else if (cTag.m_name == TAG_MEDIA_ALBUM)
			sAlbum = cTag.GetStr();
		else if (cTag.m_name == TAG_MEDIA_TITLE)
			sTitle = cTag.GetStr();
		else if (cTag.m_name == TAG_MEDIA_LENGTH)
			uLength = (uint32)cTag.GetInt();
		else if (cTag.m_name == TAG_MEDIA_BITRATE)
			uBitrate = (uint32)cTag.GetInt();
		else if (cTag.m_name == TAG_MEDIA_CODEC)
			sCodec = cTag.GetStr();
		else if (cTag.m_name == TAG_SOURCES) {
			// Some rouge client was setting an invalid availability, just set it to 0
			uAvailability = (uint32)cTag.GetInt();
			if (uAvailability > 65500)
				uAvailability = 0;
		} else if (cTag.m_name == TAG_PUBLISHINFO) {
			if (uFromKadVersion >= KADEMLIA_VERSION6_49aBETA) {
				// we don't keep this as a tag, but as a member property of the search file,
				// because we only need its information in the search list and don't want to carry
				// the tag over when downloading the file (and maybe even wrongly publishing it)
				uPublishInfo = (uint32)cTag.GetInt();
			} else
				DebugLogWarning(_T("ProcessResultKeyword: Received special publish tag (TAG_PUBLISHINFO) from node (version %u, ip: %s) which is not aware of it, filtering")
					, uFromKadVersion, (LPCTSTR)ipstr(htonl(uFromIP)));
		} else if (cTag.m_name == TAG_KADAICHHASHRESULT) {
			if (uFromKadVersion >= KADEMLIA_VERSION9_50a && cTag.IsBsob()) {
				CSafeMemFile fileAICHTag(cTag.GetBsob(), cTag.GetBsobSize());
				try {
					for (uint8 byCount = fileAICHTag.ReadUInt8(); byCount > 0; --byCount) {
						uint8 byPopularity = fileAICHTag.ReadUInt8();
						if (byPopularity > 0) {
							aAICHHashPopularity.Add(byPopularity);
							aAICHHashes.Add(CAICHHash(fileAICHTag));
						}
					}
				} catch (CFileException *ex) {
					DebugLogError(_T("ProcessResultKeyword: Corrupt or invalid TAG_KADAICHHASHRESULT received - ip: %s)"), (LPCTSTR)ipstr(htonl(uFromIP)));
					ex->Delete();
					aAICHHashPopularity.RemoveAll();
					aAICHHashes.RemoveAll();
				}
			} else
				DebugLogWarning(_T("ProcessResultKeyword: Received special publish tag (TAG_KADAICHHASHRESULT) from node (version %u, ip: %s) which is not aware of it, filtering")
					, uFromKadVersion, (LPCTSTR)ipstr(htonl(uFromIP)));
		}
	}

	// If we don't have a valid filename or file size, drop this keyword.
	if (!bFileName || !bFileSize)
		return;

	// Check that this result matches the original criteria
	WordList listTestWords;
	CSearchManager::GetWords(sName, listTestWords);
	CKadTagValueString keyword;
	for (WordList::const_iterator itWordListWords = m_listWords.begin(); itWordListWords != m_listWords.end(); ++itWordListWords) {
		keyword = *itWordListWords;
		bool bInterested = false;
		for (WordList::const_iterator itWordListTestWords = listTestWords.begin(); itWordListTestWords != listTestWords.end(); ++itWordListTestWords)
			if (EqualKadTagStr(keyword, *itWordListTestWords)) {
				bInterested = true;
				break;
			}

		if (!bInterested)
			return;
	}

	if (m_pSearchTerm == NULL && m_pucSearchTermsData != NULL && m_uSearchTermsDataSize != 0) {
		// we create this to pass on to the search list, which will check it against the result to filter bad ones
		CSafeMemFile tmpFile(m_pucSearchTermsData, m_uSearchTermsDataSize);
		m_pSearchTerm = CKademliaUDPListener::CreateSearchExpressionTree(tmpFile, 0);
		ASSERT(m_pSearchTerm != NULL);
	}

	// Inc the number of answers.
	++m_uAnswers;
	// Update the search in the GUI
	theApp.emuledlg->kademliawnd->searchList->SearchRef(this);
	// Send the keyword to search list for processing.
	// This method is still legacy from the multithreaded Kad, maybe this can be changed for better handling.
	theApp.searchlist->KademliaSearchKeyword(m_uSearchID, &uAnswer, sName, uSize, sType, uPublishInfo
		, aAICHHashes, aAICHHashPopularity, m_pSearchTerm
		, 8
		, TAGTYPE_STRING, TAG_FILEFORMAT, (LPCTSTR)sFormat
		, TAGTYPE_STRING, TAG_MEDIA_ARTIST, (LPCTSTR)sArtist
		, TAGTYPE_STRING, TAG_MEDIA_ALBUM, (LPCTSTR)sAlbum
		, TAGTYPE_STRING, TAG_MEDIA_TITLE, (LPCTSTR)sTitle
		, TAGTYPE_UINT32, TAG_MEDIA_LENGTH, uLength
		, TAGTYPE_UINT32, TAG_MEDIA_BITRATE, uBitrate
		, TAGTYPE_STRING, TAG_MEDIA_CODEC, (LPCTSTR)sCodec
		, TAGTYPE_UINT32, TAG_SOURCES, uAvailability);
}

void CSearch::SendFindValue(CContact *pContact, bool bReAskMore)
{
	// Found a Node that we think has contacts closer to our target.
	try {
		// Make sure we are not in the process of stopping.
		if (m_bStoping)
			return;
		CSafeMemFile fileIO(33);
		// The number of returned contacts is based on the type of search.
		uint8 byContactCount = GetRequestContactCount();
		if (bReAskMore) {
			if (byContactCount != KADEMLIA_FIND_VALUE) {
				ASSERT(0);
				return;
			}
			m_mapRequestedMoreNodes[pContact->GetClientID()] = true;
			byContactCount = KADEMLIA_FIND_VALUE_MORE;
		}

		if (byContactCount <= 0)
			return;

		fileIO.WriteUInt8(byContactCount);
		// Put the target we want into the packet.
		fileIO.WriteUInt128(m_uTarget);
		// Add the ID of the contact we are contacting for sanity checks on the other end.
		fileIO.WriteUInt128(pContact->GetClientID());
		// Inc the number of packets sent.
		++m_uKadPacketSent;
		// Update the search for the GUI.
		theApp.emuledlg->kademliawnd->searchList->SearchRef(this);

		if (pContact->GetVersion() >= KADEMLIA_VERSION2_47a) {
			m_pLookupHistory->ContactAskedKad(pContact);
			theApp.emuledlg->kademliawnd->UpdateSearchGraph(m_pLookupHistory);
			if (pContact->GetVersion() >= KADEMLIA_VERSION6_49aBETA) {
				CUInt128 uClientID = pContact->GetClientID();
				CKademlia::GetUDPListener()->SendPacket(fileIO, KADEMLIA2_REQ, pContact->GetIPAddress(), pContact->GetUDPPort(), pContact->GetUDPKey(), &uClientID);
			} else {
				CKademlia::GetUDPListener()->SendPacket(fileIO, KADEMLIA2_REQ, pContact->GetIPAddress(), pContact->GetUDPPort(), CKadUDPKey(), NULL);
				ASSERT(CKadUDPKey() == pContact->GetUDPKey());
			}
			if (thePrefs.GetDebugClientKadUDPLevel() > 0) {
				LPCSTR pszOp;
				switch (m_uType) {
				case NODE:
					pszOp = "KADEMLIA2_REQ(NODE)";
					break;
				case NODECOMPLETE:
					pszOp = "KADEMLIA2_REQ(NODECOMPLETE)";
					break;
				case NODESPECIAL:
					pszOp = "KADEMLIA2_REQ(NODESPECIAL)";
					break;
				case NODEFWCHECKUDP:
					pszOp = "KADEMLIA2_REQ(NODEFWCHECKUDP)";
					break;
				case FILE:
					pszOp = "KADEMLIA2_REQ(FILE)";
					break;
				case KEYWORD:
					pszOp = "KADEMLIA2_REQ(KEYWORD)";
					break;
				case STOREFILE:
					pszOp = "KADEMLIA2_REQ(STOREFILE)";
					break;
				case STOREKEYWORD:
					pszOp = "KADEMLIA2_REQ(STOREKEYWORD)";
					break;
				case STORENOTES:
					pszOp = "KADEMLIA2_REQ(STORENOTES)";
					break;
				case NOTES:
					pszOp = "KADEMLIA2_REQ(NOTES)";
					break;
				default:
					pszOp = "KADEMLIA2_REQ()";
				}
				DebugSend(pszOp, pContact->GetIPAddress(), pContact->GetUDPPort());
			}
		} else
			ASSERT(0);
	} catch (CIOException *ex) {
		AddDebugLogLine(false, _T("Exception in CSearch::SendFindValue (IO error(%i))"), ex->m_iCause);
		ex->Delete();
	} catch (...) {
		AddDebugLogLine(false, _T("Exception in CSearch::SendFindValue"));
	}
}

CContact* CSearch::GetMoreResultsContact(CUInt128 *puDistance) const
{
	for (int iPass = 0; iPass < 2; ++iPass) {
		const bool bRequireCloserContacts = iPass == 0;
		for (std::map<Kademlia::CUInt128, bool>::const_iterator itResponded = m_mapResponded.begin(); itResponded != m_mapResponded.end(); ++itResponded) {
			if (bRequireCloserContacts && !itResponded->second)
				continue;

			ContactMap::const_iterator itTried = m_mapTried.find(itResponded->first);
			if (itTried == m_mapTried.end() || itTried->second == NULL)
				continue;

			CContact *pContact = itTried->second;
			if (m_mapRequestedMoreNodes.find(pContact->GetClientID()) != m_mapRequestedMoreNodes.end())
				continue;

			if (puDistance != NULL)
				*puDistance = itResponded->first;
			return pContact;
		}
	}

	return NULL;
}

bool CSearch::CanRequestMoreResults() const
{
	if (m_bStoping || GetRequestContactCount() != KADEMLIA_FIND_VALUE || m_mapRequestedMoreNodes.size() >= static_cast<size_t>(KADEMLIA_FIND_VALUE_MORE_REASKS))
		return false;

	return GetMoreResultsContact(NULL) != NULL;
}

bool CSearch::RequestMoreResults()
{
	if (m_bStoping || GetRequestContactCount() != KADEMLIA_FIND_VALUE || m_mapRequestedMoreNodes.size() >= static_cast<size_t>(KADEMLIA_FIND_VALUE_MORE_REASKS))
		return false;

	CUInt128 uDistance;
	CContact *pContact = GetMoreResultsContact(&uDistance);
	if (pContact == NULL)
		return false;

	SendFindValue(pContact, true);
	m_mapTimePending[uDistance] = clock();
	return true;
}

bool CSearch::TryRequestMoreResultsOnStall()
{
	if (!m_mapTimePending.empty() || !CanRequestMoreResults())
		return false;

	return RequestMoreResults();
}

void CSearch::AddFileID(const CUInt128 &uID)
{
	// Add a file hash to the search list.
	// This is used mainly for storing keywords, but was also reused for storing notes.
	m_listFileIDs.Add(uID);
}

static INT_PTR GetMetaDataWords(CStringArray &rastrWords, const CString &rstrData)
{
	// Create a list of the 'words' found in 'data'. This is somewhat similar
	// to the 'CSearchManager::GetWords' function which needs to follow some other rules.
	for (int iPos = 0; iPos >= 0;) {
		const CString &strWord(rstrData.Tokenize(g_aszInvKadKeywordChars, iPos));
		if (!strWord.IsEmpty())
			rastrWords.Add(strWord);
	}
	return rastrWords.GetCount();
}

static bool IsRedundantMetaData(const CStringArray &rastrFileNameWords, const CString &rstrMetaData)
{
	// Verify if the meta data string 'rstrMetaData' is already contained within the filename.
	if (rstrMetaData.IsEmpty())
		return true;

	int iMetaDataWords = 0;
	int iFoundInFileName = 0;
	for (int iPos = 0; iPos >= 0;) {
		const CString &strMetaDataWord(rstrMetaData.Tokenize(g_aszInvKadKeywordChars, iPos));
		if (strMetaDataWord.IsEmpty())
			break;
		++iMetaDataWords;
		for (INT_PTR i = rastrFileNameWords.GetCount(); --i >= 0;) {
			// Verified Locale Dependency: Locale dependent string comparison (OK)
			if (rastrFileNameWords[i].CompareNoCase(strMetaDataWord) == 0) {
				++iFoundInFileName;
				break;
			}
		}
		if (iFoundInFileName < iMetaDataWords)
			return false;
	}
	return (iMetaDataWords == 0 || iMetaDataWords == iFoundInFileName);
}

void CSearch::PreparePacketForTags(CByteIO *byIO, CKnownFile *pFile, uint8 byTargetKadVersion)
{
	// We are going to publish a keyword, setup the tag list.
	TagList listTag;
	try {
		if (pFile && byIO) {
			// Name, Size
			listTag.push_back(new CKadTagStr(TAG_FILENAME, pFile->GetFileName()));
			//	// TODO: As soon as we drop Kad1 support, we should switch to Int64 tags (we could do now already for kad2 nodes only but no advantage in that)
			//} else
			listTag.push_back(new CKadTagUInt(TAG_FILESIZE, (uint64)pFile->GetFileSize()));

			listTag.push_back(new CKadTagUInt(TAG_SOURCES, pFile->m_nCompleteSourcesCount));

			if (byTargetKadVersion >= KADEMLIA_VERSION9_50a && pFile->GetFileIdentifier().HasAICHHash())
				listTag.push_back(new CKadTagBsob(TAG_KADAICHHASHPUB, pFile->GetFileIdentifier().GetAICHHash().GetRawHashC()
					, (uint8)CAICHHash::GetHashSize()));

			// eD2K file type (Audio, Video, ...)
			// NOTE: Archives and CD-Images are published with file type "Pro"
			const CString &strED2KFileType(GetED2KFileTypeSearchTerm(GetED2KFileTypeID(pFile->GetFileName()), true));
			if (!strED2KFileType.IsEmpty())
				listTag.push_back(new CKadTagStr(TAG_FILETYPE, strED2KFileType));

			// file format (filename extension)
			// 21-Sep-2006 []: TAG_FILEFORMAT is no longer explicitly published nor stored as
			// it is already a part of the filename.

			// additional meta data (Artist, Album, Codec, Length, ...)
			// only send verified meta data to nodes
			if (pFile->GetMetaDataVer() > 0) {
				static const struct
				{
					uint8 uName;
					uint8 uType;
				}
				_aMetaTags[] =
				{
					{ FT_MEDIA_ARTIST,  TAGTYPE_STRING },
					{ FT_MEDIA_ALBUM,   TAGTYPE_STRING },
					{ FT_MEDIA_TITLE,   TAGTYPE_STRING },
					{ FT_MEDIA_LENGTH,  TAGTYPE_UINT32 },
					{ FT_MEDIA_BITRATE, TAGTYPE_UINT32 },
					{ FT_MEDIA_CODEC,   TAGTYPE_STRING }
				};
				CStringArray astrFileNameWords;
				for (unsigned iIndex = 0; iIndex < _countof(_aMetaTags); ++iIndex) {
					const CTag *pTag = pFile->GetTag(_aMetaTags[iIndex].uName, _aMetaTags[iIndex].uType);
					if (pTag) {
						// skip string tags with empty string values
						if (pTag->IsStr() && pTag->GetStr().IsEmpty())
							continue;
						// skip integer tags with '0' values
						if (pTag->IsInt() && pTag->GetInt() == 0)
							continue;
						char szKadTagName[2];
						szKadTagName[0] = (char)pTag->GetNameID();
						szKadTagName[1] = '\0';
						if (pTag->IsStr()) {
							bool bIsRedundant = false;
							switch (pTag->GetNameID()) {
							case FT_MEDIA_ARTIST:
							case FT_MEDIA_ALBUM:
							case FT_MEDIA_TITLE:
								if (astrFileNameWords.IsEmpty())
									GetMetaDataWords(astrFileNameWords, pFile->GetFileName());
								bIsRedundant = IsRedundantMetaData(astrFileNameWords, pTag->GetStr());
							}
							if (!bIsRedundant)
								listTag.push_back(new CKadTagStr(szKadTagName, pTag->GetStr()));
						} else
							listTag.push_back(new CKadTagUInt(szKadTagName, pTag->GetInt()));
					}
				}
			}
			byIO->WriteTagList(listTag);
		} else {
			//If we get here, bad things happened. Will fix this later if it is a real issue.
			ASSERT(0);
		}
	} catch (CIOException *ex) {
		AddDebugLogLine(false, _T("Exception in CSearch::PreparePacketForTags (IO error(%i))"), ex->m_iCause);
		ex->Delete();
	} catch (...) {
		AddDebugLogLine(false, _T("Exception in CSearch::PreparePacketForTags"));
	}
	deleteTagListEntries(listTag);
}

uint32 CSearch::GetNodeLoad() const
{
	// Node load is the average of all node load responses.
	return m_uTotalLoadResponses ? m_uTotalLoad / m_uTotalLoadResponses : 0;
}

void CSearch::SetSearchType(uint32 uVal)
{
	m_uType = uVal;
	switch (m_uType) {
	case NODE:
	case NODECOMPLETE:
	case NODESPECIAL:
	case NODEFWCHECKUDP:
		m_tPolicyLifetime = SEARCHNODE_LIFETIME;
		m_uPolicyTotalLimit = 0;
		break;
	case FILE:
		m_tPolicyLifetime = thePrefs.GetKadFileSearchLifetime();
		m_uPolicyTotalLimit = static_cast<uint32>(thePrefs.GetKadFileSearchTotal());
		break;
	case KEYWORD:
		m_tPolicyLifetime = thePrefs.GetKadSearchKeywordLifetime();
		m_uPolicyTotalLimit = static_cast<uint32>(thePrefs.GetKadSearchKeywordTotal());
		break;
	case NOTES:
		m_tPolicyLifetime = SEARCHNOTES_LIFETIME;
		m_uPolicyTotalLimit = SEARCHNOTES_TOTAL;
		break;
	case STOREFILE:
		m_tPolicyLifetime = SEARCHSTOREFILE_LIFETIME;
		m_uPolicyTotalLimit = SEARCHSTOREFILE_TOTAL;
		break;
	case STOREKEYWORD:
		m_tPolicyLifetime = SEARCHSTOREKEYWORD_LIFETIME;
		m_uPolicyTotalLimit = SEARCHSTOREKEYWORD_TOTAL;
		break;
	case STORENOTES:
		m_tPolicyLifetime = SEARCHSTORENOTES_LIFETIME;
		m_uPolicyTotalLimit = SEARCHSTORENOTES_TOTAL;
		break;
	case FINDSERVINGBUDDY:
		m_tPolicyLifetime = SEARCHFINDSERVINGBUDDY_LIFETIME;
		m_uPolicyTotalLimit = SEARCHFINDSERVINGBUDDY_TOTAL;
		break;
	case FINDSOURCE:
		m_tPolicyLifetime = SEARCHFINDSOURCE_LIFETIME;
		m_uPolicyTotalLimit = SEARCHFINDSOURCE_TOTAL;
		break;
	default:
		m_tPolicyLifetime = SEARCH_LIFETIME;
		m_uPolicyTotalLimit = 0;
	}
	m_pLookupHistory->SetSearchType(uVal);
}

uint32 CSearch::GetAnswers() const
{
	if (m_listFileIDs.IsEmpty())
		return m_uAnswers;
	// If we sent more then one packet per node, we have to average the answers for the real count.
	return (uint32)(m_uAnswers / ((m_listFileIDs.GetSize() + 49) / 50));
}

void CSearch::UpdateNodeLoad(uint8 uLoad)
{
	// Since all nodes do not return a load value, keep track of total responses and total load.
	m_uTotalLoad += uLoad;
	++m_uTotalLoadResponses;
}

const CStringW& Kademlia::CSearch::GetGUIName() const
{
	return m_pLookupHistory->GetGUIName();
}

void Kademlia::CSearch::SetGUIName(LPCWSTR sGUIName)
{
	m_pLookupHistory->SetGUIName(sGUIName);
}

void CSearch::SetSearchTermData(uint32 uSearchTermDataSize, LPBYTE pucSearchTermsData)
{
	m_uSearchTermsDataSize = uSearchTermDataSize;
	m_pucSearchTermsData = new BYTE[uSearchTermDataSize];
	memcpy(m_pucSearchTermsData, pucSearchTermsData, uSearchTermDataSize);
}

CString CSearch::GetTypeName(uint32 uType)
{
	LPCTSTR uid;
	switch (uType) {
	case CSearch::FILE:
		uid = _T("KAD_SEARCHSRC");
		break;
	case CSearch::KEYWORD:
		uid = _T("KAD_SEARCHKW");
		break;
	case CSearch::NODE:
	case CSearch::NODECOMPLETE:
	case CSearch::NODESPECIAL:
	case CSearch::NODEFWCHECKUDP:
		uid = _T("KAD_NODE");
		break;
	case CSearch::STOREFILE:
		uid = _T("KAD_STOREFILE");
		break;
	case CSearch::STOREKEYWORD:
		uid = _T("KAD_STOREKW");
		break;
	case CSearch::FINDSERVINGBUDDY:
		uid = _T("FINDSERVINGBUDDY");
		break;
	case CSearch::STORENOTES:
		uid = _T("STORENOTES");
		break;
	case CSearch::NOTES:
		uid = _T("NOTES");
		break;
	default:
		uid = _T("KAD_UNKNOWN");
	}
	return GetResString(uid);
}

uint8 CSearch::GetRequestContactCount() const
{
	// Returns the amount of contacts we request on routing queries based on the search type
	switch (m_uType) {
	case NODE:
	case NODECOMPLETE:
	case NODESPECIAL:
	case NODEFWCHECKUDP:
		return KADEMLIA_FIND_NODE;
	case FILE:
	case KEYWORD:
	case FINDSOURCE:
	case NOTES:
		return KADEMLIA_FIND_VALUE;
	case FINDSERVINGBUDDY:
	case STOREFILE:
	case STOREKEYWORD:
	case STORENOTES:
		return KADEMLIA_STORE;
	default:
		DebugLogError(LOG_DEFAULT, _T("Invalid search type. (CSearch::GetRequestContactCount())"));
		ASSERT(0);
	}
	return 0;
}

uint8 CSearch::GetExpectedResponseContactCount(uint32 uFromIP, uint16 uFromPort) const
{
	const uint8 byDefaultContactCount = GetRequestContactCount();
	if (byDefaultContactCount != KADEMLIA_FIND_VALUE)
		return byDefaultContactCount;

	for (ContactMap::const_iterator itTriedMap = m_mapTried.begin(); itTriedMap != m_mapTried.end(); ++itTriedMap) {
		const CContact *pContact = itTriedMap->second;
		if (pContact != NULL && pContact->GetIPAddress() == uFromIP && pContact->GetUDPPort() == uFromPort
			&& m_mapRequestedMoreNodes.find(pContact->GetClientID()) != m_mapRequestedMoreNodes.end())
		{
			return KADEMLIA_FIND_VALUE_MORE;
		}
	}

	return byDefaultContactCount;
}
