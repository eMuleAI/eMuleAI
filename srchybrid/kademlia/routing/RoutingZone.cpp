/*
This file is part of eMule AI

Copyright (C)2003 Barry Dunne (https://www.emule-project.net)
Copyright (C)2007-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
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


This work is based on the java implementation of the Kademlia protocol.
Kademlia: Peer-to-peer routing based on the XOR metric
Copyright (C) 2002  Petar Maymounkov [petar@post.harvard.edu]
http://kademlia.scs.cs.nyu.edu
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

/**
 * The *Zone* is just a node in a binary tree of *Zone*s.
 * Each zone is either an internal node or a leaf node.
 * Internal nodes have "bin == null" and "subZones[i] != null",
 * leaf nodes have "subZones[i] == null" and "bin != null".
 *
 * All key unique id's are relative to the center (self), which
 * is considered to be 000..000
 */

#include "stdafx.h"
#include <math.h>
#include "emule.h"
#include "emuledlg.h"
#include "ipfilter.h"
#include "KadContactListCtrl.h"
#include "kademliawnd.h"
#include "Log.h"
#include "Opcodes.h"
#include "kademlia/kademlia/Defines.h"
#include "kademlia/kademlia/Kademlia.h"
#include "kademlia/kademlia/Prefs.h"
#include "kademlia/kademlia/SearchManager.h"
#include "kademlia/kademlia/UDPFirewallTester.h"
#include "kademlia/net/KademliaUDPListener.h"
#include "kademlia/routing/RoutingZone.h"
#include "kademlia/routing/RoutingBin.h"
#include "kademlia/utils/MiscUtils.h"
#include "kademlia/utils/KadUDPKey.h"
#include "eMuleAI/SafeKAD.h"
#include "ClientUDPSocket.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using namespace Kademlia;


CString CRoutingZone::m_sFilename;
CUInt128 CRoutingZone::uMe(0ul);
static const DWORD ROUTING_GRACE_PERIOD_MS = 30 * 1000; // 30 seconds

CRoutingZone::CRoutingZone()
{
	// Can only create routing zone after prefs
	// Set our KadID for creating the contact tree
	CKademlia::GetPrefs()->GetKadID(uMe);
	// Set the nodes file name.
	m_sFilename.Format(_T("%s") _T("nodes.dat"), (LPCTSTR)thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	// Init our root node.
	Init(NULL, 0, CUInt128(0ul));
}

CRoutingZone::CRoutingZone(LPCSTR szFilename)
{
	// Can only create routing zone after prefs
	// Set our KadID for creating the contact tree
	CKademlia::GetPrefs()->GetKadID(uMe);
	m_sFilename = szFilename;
	// Init our root node.
	Init(NULL, 0, CUInt128(0ul));
}

CRoutingZone::CRoutingZone(CRoutingZone *pSuper_zone, int iLevel, const CUInt128 &uZone_index)
{
	// Create a new leaf.
	Init(pSuper_zone, iLevel, uZone_index);
}

void CRoutingZone::Init(CRoutingZone *pSuper_zone, int iLevel, const CUInt128 &uZone_index)
{
	// Init all Zone vars
	// Set this zones parent
	m_pSuperZone = pSuper_zone;
	// Set this zones level
	m_uLevel = iLevel;
	// Set this zones CUInt128 Index
	m_uZoneIndex = uZone_index;
	// Mark this zone has having now leafs.
	m_pSubZones[0] = NULL;
	m_pSubZones[1] = NULL;
	// Create a new contact bin as this is a leaf.
	m_pBin = new CRoutingBin();

	// Set timer so that zones closer to the root are processed earlier.
	m_tNextSmallTimer = time(NULL) + m_uZoneIndex.Get32BitChunk(3);

	// Start this zone.
	StartTimer();

#ifdef _BOOTSTRAPNODESDAT
	if (m_pSuperZone == NULL)
		Kademlia::CKademlia::m_pInstance->m_pRoutingZone = this; //otherwise it will fail in CanSplit()
#endif
	// If we are initializing the root node, read in our saved contact list.
	if (m_pSuperZone == NULL && !m_sFilename.IsEmpty())
		ReadFile();
}

CRoutingZone::~CRoutingZone()
{
	// Root node is processed first so that we can write our contact list and delete all branches.
	if (m_pSuperZone == NULL && !m_sFilename.IsEmpty()) {
		// Hide contacts in the GUI
		theApp.emuledlg->kademliawnd->StopUpdateContacts();
#ifndef _BOOTSTRAPNODESDAT
		WriteFile();
#else
		DbgWriteBootstrapFile();
#endif
	}
	// If this zone is a leaf, delete our contact bin.
	if (IsLeaf())
		delete m_pBin;
	else {
		// If this zone is branch, delete its leafs.
		delete m_pSubZones[0];
		delete m_pSubZones[1];
	}

	// All branches are deleted, show the contact list in the GUI.
	if (m_pSuperZone == NULL)
		theApp.emuledlg->kademliawnd->StartUpdateContacts();
}

void CRoutingZone::ReadFile(const CString &strSpecialNodesdate)
{
	if (m_pSuperZone != NULL || (m_sFilename.IsEmpty() && strSpecialNodesdate.IsEmpty())) {
		ASSERT(0);
		return;
	}
	CSafeBufferedFile file;
	if (!file.Open(strSpecialNodesdate.IsEmpty() ? m_sFilename : strSpecialNodesdate, CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyWrite, NULL)) {
		DebugLogWarning(_T("Unable to read Kad file: %s"), (LPCTSTR)m_sFilename);
		return;
	}
	// Read in the saved contact list.
	try {
			::setvbuf(file.m_pStream, NULL, _IOFBF, 32768);

			// Get how many contacts in the saved list.
			// NOTE: Older clients put the number of contacts here.
			//       Newer clients always have 0 here to prevent older clients from reading it.
			uint32 uNumContacts = file.ReadUInt32();
			uint32 uVersion = 0;
			if (uNumContacts == 0) {
				if (file.GetLength() >= 8) {
					uVersion = file.ReadUInt32();
					if (uVersion == 3) {
						uint32 nBoostrapEdition = file.ReadUInt32();
						if (nBoostrapEdition == 1) {
							// this is a special bootstrap-only nodes.dat, handle it in a separate reading function
							ReadBootstrapNodesDat(file);
							file.Close();
							return;
						}
					}
					if (uVersion >= 1 && uVersion <= 3) // those version we know, others we ignore
						uNumContacts = file.ReadUInt32();
				} else
					AddDebugLogLine(false, GetResString(_T("ERR_KADCONTACTS")));
			}
			if (uNumContacts != 0 && uNumContacts * 25ull <= (file.GetLength() - file.GetPosition())) {
				// Hide contact list in the GUI
				theApp.emuledlg->kademliawnd->StopUpdateContacts();

				bool bDoHaveVerifiedContacts = false;
				uint32 uValidContacts = 0;
				CUInt128 uID;
				while (uNumContacts--) {
					file.ReadUInt128(uID);
					uint32 uIP = file.ReadUInt32();
					uint16 uUDPPort = file.ReadUInt16();
					uint16 uTCPPort = file.ReadUInt16();
				byte byType;

				uint8 uContactVersion;
				if (uVersion >= 1) {
					byType = 0;
						uContactVersion = file.ReadUInt8();
				} else {
						byType = file.ReadUInt8();
					uContactVersion = 0;
				}
				bool bVerified;

					CKadUDPKey kadUDPKey;
					if (uVersion >= 2) {
						kadUDPKey.ReadFromFile(file);
						bVerified = file.ReadUInt8() != 0;
						if (bVerified)
							bDoHaveVerifiedContacts = true;
				} else
					bVerified = false;
					// IP Appears valid
					if (byType < 4) {
						uint32 uhostIP = htonl(uIP);
						if (IsGoodIPPort(uhostIP, uUDPPort)) {
							if (theApp.ipfilter->IsFiltered(uhostIP)) {
								if (thePrefs.GetLogFilteredIPs())
									AddDebugLogLine(false, _T("Ignored kad contact (IP=%s:%u)--read nodes.dat -- - IP filter (%s)"), (LPCTSTR)ipstr(uhostIP), uUDPPort, (LPCTSTR)EscPercent(theApp.ipfilter->GetLastHit()));
							} else if (uUDPPort == 53 && uContactVersion <= KADEMLIA_VERSION5_48a) { /*No DNS Port without encryption*/
								if (thePrefs.GetLogFilteredIPs())
									AddDebugLogLine(false, _T("Ignored kad contact (IP=%s:%u)--read nodes.dat"), (LPCTSTR)ipstr(uhostIP), uUDPPort);
							} else {
								// This was not a dead contact, Inc counter if add was successful
								if (AddUnfiltered(uID, uIP, uUDPPort, uTCPPort, uContactVersion, kadUDPKey, bVerified, false, true, false))
									++uValidContacts;
							}
						}
					}
				}
				AddLogLine(false, GetResString(_T("KADCONTACTSREAD")), uValidContacts);
				if (!bDoHaveVerifiedContacts) {
					DebugLogWarning(_T("No verified contacts found in nodes.dat - might be an old file version. Setting all contacts verified for this time to speed up Kad bootstrapping"));
					SetAllContactsVerified();
				}
			}
			file.Close();
	} catch (CFileException *ex) {
		ex->Delete();
		DebugLogError(_T("CFileException in CRoutingZone::readFile"));
	}
	// Show contact list in GUI
	theApp.emuledlg->kademliawnd->StartUpdateContacts();
}

void CRoutingZone::ReadBootstrapNodesDat(CFileDataIO &file)
{
	// Bootstrap versions of nodes.dat files, are in the style of version 1 nodes.dats. The difference is that
	// they will contain more contacts 500-1000 instead 50, and those contacts are not added into the routing table
	// but used to sent Bootstrap packets too. The advantage is that on a list with a high ratio of dead nodes,
	// we will be able to bootstrap faster than on a normal nodes.dat and more important, if we would deliver
	// a normal nodes.dat with eMule, those 50 nodes would be kinda DDOSed because everyone adds them to their routing
	// table, while with this style, we don't actually add any of the contacts to our routing table in the end and we
	// ask only one of those 1000 contacts one time (well or more until we find an alive one).
	if (!CKademlia::s_liBootstrapList.IsEmpty()) {
		ASSERT(0);
		return;
	}
	uint32 uNumContacts = file.ReadUInt32();
	if (uNumContacts != 0 && uNumContacts * 25ull == (file.GetLength() - file.GetPosition())) {
		uint32 uValidContacts = 0;
		CUInt128 uID;
		while (uNumContacts--) {
			file.ReadUInt128(uID);
			uint32 uIP = file.ReadUInt32();
			uint16 uUDPPort = file.ReadUInt16();
			uint16 uTCPPort = file.ReadUInt16();
			uint8 uContactVersion = file.ReadUInt8();

			uint32 uhostIP = htonl(uIP);
			if (IsGoodIPPort(uhostIP, uUDPPort)) {
				if (theApp.ipfilter->IsFiltered(uhostIP)) {
					if (thePrefs.GetLogFilteredIPs())
						AddDebugLogLine(false, _T("Ignored kad contact (IP=%s:%u)--read known.dat -- - IP filter (%s)"), (LPCTSTR)ipstr(uhostIP), uUDPPort, (LPCTSTR)EscPercent(theApp.ipfilter->GetLastHit()));
				} else if (uUDPPort == 53 && uContactVersion <= KADEMLIA_VERSION5_48a) {
					if (thePrefs.GetLogFilteredIPs())
						AddDebugLogLine(false, _T("Ignored kad contact (IP=%s:%u)--read known.dat"), (LPCTSTR)ipstr(uhostIP), uUDPPort);
				} else if (uContactVersion > 1) { // only kad2 nodes
					// we want 50 nodes closest to our own ID (provides randomness between different users and
					// gives good chances to bootstrap with close Nodes as a nice start for our routing table)
					CUInt128 uDistance(uMe);
					uDistance.Xor(uID);
					++uValidContacts;
					// don't bother if we already have 50 and the farthest distance is smaller than this contact
					if (CKademlia::s_liBootstrapList.GetCount() < 50 || CKademlia::s_liBootstrapList.GetTail()->GetDistance() > uDistance) {
						// look for the proper position where to insert this contact
						bool bInserted = false;
						CContact *pContact = new CContact(uID, uIP, uUDPPort, uTCPPort, uMe, uContactVersion, CKadUDPKey(), false);
						pContact->SetBootstrapContact();
						for (POSITION pos = CKademlia::s_liBootstrapList.GetHeadPosition(); pos != NULL;) {
							POSITION pos2 = pos;
							if (CKademlia::s_liBootstrapList.GetNext(pos)->GetDistance() > uDistance) {
								CKademlia::s_liBootstrapList.InsertBefore(pos2, pContact);
								bInserted = true;
								break;
							}
						}
						if (!bInserted) {
							ASSERT(CKademlia::s_liBootstrapList.GetCount() < 50);
							CKademlia::s_liBootstrapList.AddTail(pContact);
						} else if (CKademlia::s_liBootstrapList.GetCount() > 50)
							delete CKademlia::s_liBootstrapList.RemoveTail();
					}
				}
			}
		}

		theApp.emuledlg->kademliawnd->StopUpdateContacts();
		theApp.emuledlg->kademliawnd->SetBootstrapListMode();
		for (POSITION pos = CKademlia::s_liBootstrapList.GetHeadPosition(); pos != NULL;) {
			CContact *pContact = CKademlia::s_liBootstrapList.GetNext(pos);
			pContact->SetGuiRefs(true);
			theApp.emuledlg->kademliawnd->ContactAdd(pContact);
		}
		theApp.emuledlg->kademliawnd->StartUpdateContacts();

		AddLogLine(false, GetResString(_T("KADCONTACTSREAD")), CKademlia::s_liBootstrapList.GetCount());
		DebugLog(_T("Loaded Bootstrap nodes.dat, selected %u out of %u valid contacts"), CKademlia::s_liBootstrapList.GetCount(), uValidContacts);
	}
}

void CRoutingZone::WriteFile()
{
	// Don't overwrite a bootstrap nodes.dat if we recently had a network change and might not have
	if (GetNumContacts() == 0 && theApp.clientudp != NULL && ::GetTickCount() - theApp.clientudp->GetLastRebindTime() < ROUTING_GRACE_PERIOD_MS) {
		DebugLogWarning(_T("Skipped storing nodes.dat due to recent network change"));
		return;
	}

	// don't overwrite a bootstrap nodes.dat with an empty one, if we didn't finish probing
	if (!CKademlia::s_liBootstrapList.IsEmpty() && GetNumContacts() == 0) {
		DebugLogWarning(_T("Skipped storing nodes.dat, because we have an unfinished bootstrap of the nodes.dat version and no contacts in our routing table"));
		return;
	}

	CString m_sTempFilename = m_sFilename + _T(".tmp");
	CSafeBufferedFile file;
	if (!file.Open(m_sTempFilename, CFile::modeWrite | CFile::modeCreate | CFile::typeBinary | CFile::shareDenyWrite, NULL)) {
		DebugLogError(_T("Unable to store Kad file: %s"), (LPCTSTR)m_sTempFilename);
		return;
	}

	// Write the saved contact list.
	try {
		::setvbuf(file.m_pStream, NULL, _IOFBF, 32768);

		// The bootstrap method gets a very nice sample of contacts to save.
		ContactArray listContacts;
		GetBootstrapContacts(listContacts, 200);
		// Start file with 0 to prevent older clients from reading it.
		file.WriteUInt32(0);
		// Now tag it with a version which happens to be 2 (1 till 0.48a).
		file.WriteUInt32(2);
		file.WriteUInt32((uint32)listContacts.size());
		for (ContactArray::const_iterator itContact = listContacts.begin(); itContact != listContacts.end(); ++itContact) {
			const CContact &contact(**itContact);
			file.WriteUInt128(contact.GetClientID());
			file.WriteUInt32(contact.GetIPAddress());
			file.WriteUInt16(contact.GetUDPPort());
			file.WriteUInt16(contact.GetTCPPort());
			file.WriteUInt8(contact.GetVersion());
			contact.GetUDPKey().StoreToFile(file);
			file.WriteUInt8(static_cast<uint8>(contact.IsIpVerified()));
		}
		file.Close();
		MoveFileEx(m_sTempFilename, m_sFilename, MOVEFILE_REPLACE_EXISTING);
		AddDebugLogLine(false, _T("Wrote %ld contact%s to file."), listContacts.size(), ((listContacts.size() == 1) ? EMPTY : _T("s")));
	} catch (CFileException *ex) {
		ex->Delete();
		AddDebugLogLine(false, _T("CFileException in CRoutingZone::writeFile"));
	}
}

void CRoutingZone::DbgWriteBootstrapFile()
{
#ifdef _BOOTSTRAPNODESDAT
	DebugLogWarning(_T("Writing special bootstrap nodes.dat - not intended for normal use"));
		CSafeBufferedFile file;
	if (!file.Open(m_sFilename, CFile::modeWrite | CFile::modeCreate | CFile::typeBinary | CFile::shareDenyWrite, NULL)) {
		DebugLogError(_T("Unable to store bootstrap file: %s"), (LPCTSTR)EscPercent(m_sFilename));
		return;
	}
	// Write the saved contact list.
	try {
			::setvbuf(file.m_pStream, NULL, _IOFBF, 32768);

			ContactMap mapContacts;
		// filter out Kad1 nodes and null IDs
			for (ContactMap::const_iterator itContactMap = mapContacts.begin(); itContactMap != mapContacts.end();) {
				ContactMap::const_iterator itCurContactMap = itContactMap++;
				const CContact *pContact = itCurContactMap->second;
			if (pContact->GetClientID() == 0 || pContact->GetVersion() < KADEMLIA_VERSION2_47a)
					mapContacts.erase(itCurContactMap);
			}
		// The bootstrap method gets a very nice sample of contacts to save.
		CUInt128 uRandom(CUInt128(0ul), 0);
		CUInt128 uDistance = uRandom;
		uDistance.Xor(uMe);
		GetClosestTo(2, uRandom, uDistance, 1200, mapContacts, false, false);
			// Start file with 0 to prevent older clients from reading it.
			file.WriteUInt32(0);
			// Now tag it with a version which happens to be 2 (1 till 0.48a).
			file.WriteUInt32(3);
			file.WriteUInt32(1); // if we would use version >=3, this would mean that this is not a normal nodes.dat
			file.WriteUInt32((uint32)mapContacts.size());
			for (ContactMap::const_iterator itContactMap = mapContacts.begin(); itContactMap != mapContacts.end(); ++itContactMap) {
			const CContact &contact = *itContactMap->second;
			file.WriteUInt128(contact.GetClientID());
			file.WriteUInt32(contact.GetIPAddress());
			file.WriteUInt16(contact.GetUDPPort());
			file.WriteUInt16(contact.GetTCPPort());
			file.WriteUInt8(contact.GetVersion());
			}
			file.Close();
			AddDebugLogLine(false, _T("Wrote %ld contact to bootstrap file."), mapContacts.size());
	} catch (CFileException *ex) {
		ex->Delete();
		AddDebugLogLine(false, _T("CFileException in CRoutingZone::writeFile"));
	}
#endif
}

bool CRoutingZone::CanSplit() const
{
#ifdef _BOOTSTRAPNODESDAT
	if (Kademlia::CKademlia::GetRoutingZone()->GetNumContacts() < 2000)
		return true;
#endif

	// Max levels allowed.
	if (m_uLevel >= 127)
		return false;

	// Check if this zone is allowed to split.
	return ((m_uZoneIndex < KK || m_uLevel < KBASE) && m_pBin->GetSize() == K);
}

// Returns true if a contact was added or updated, false if the routing table was not touched
bool CRoutingZone::Add(const CUInt128 &uID, uint32 uIP, uint16 uUDPPort, uint16 uTCPPort, uint8 uVersion, const CKadUDPKey &cUDPKey, bool &bIPVerified, bool bUpdate, bool bFromNodesDat, bool bFromHello)
{
	uint32 uhostIP = htonl(uIP);
	if (IsGoodIPPort(uhostIP, uUDPPort)) {
		if (!theApp.ipfilter->IsFiltered(uhostIP) && !(uUDPPort == 53 && uVersion <= KADEMLIA_VERSION5_48a)  /*No DNS Port without encryption*/)
			return AddUnfiltered(uID, uIP, uUDPPort, uTCPPort, uVersion, cUDPKey, bIPVerified, bUpdate, bFromNodesDat, bFromHello);

		if (thePrefs.GetLogFilteredIPs())
			if (uUDPPort != 53 || uVersion > KADEMLIA_VERSION5_48a)
				AddDebugLogLine(false, _T("Ignored kad contact (IP=%s:%u) - IP filter (%s)"), (LPCTSTR)ipstr(uhostIP), uUDPPort, (LPCTSTR)EscPercent(theApp.ipfilter->GetLastHit()));
			else
				AddDebugLogLine(false, _T("Ignored kad contact (IP=%s:%u)"), (LPCTSTR)ipstr(uhostIP), uUDPPort);

	} else if (thePrefs.GetLogFilteredIPs())
		AddDebugLogLine(false, _T("Ignored kad contact (IP=%s:%u) - Bad IP or port"), (LPCTSTR)ipstr(uhostIP), uUDPPort);
	return false;
}

// Returns true if a contact was added or updated, false if the routing table was not touched
bool CRoutingZone::AddUnfiltered(const CUInt128 &uID, uint32 uIP, uint16 uUDPPort, uint16 uTCPPort, uint8 uVersion, const CKadUDPKey &cUDPKey, bool &bIPVerified, bool bUpdate, bool /*bFromNodesDat*/, bool bFromHello)
{
	if (uID != uMe && uVersion > 1) {
		CContact *pContact = new CContact(uID, uIP, uUDPPort, uTCPPort, uVersion, cUDPKey, bIPVerified);
		if (bFromHello)
			pContact->SetReceivedHelloPacket();

		if (Add(pContact, bUpdate, bIPVerified)) {
			ASSERT(!bUpdate);
			return true;
		}
		delete pContact;
		return bUpdate;
	}
	return false;
}

bool CRoutingZone::Add(CContact *pContact, bool &bUpdate, bool &bOutIPVerified)
{
	// If we are not a leaf, call add on the correct branch.
	if (!IsLeaf())
		return m_pSubZones[pContact->GetDistance().GetBitNumber(m_uLevel)]->Add(pContact, bUpdate, bOutIPVerified);

	// Do we already have a contact with this KadID?
	CContact *pContactUpdate = m_pBin->GetContact(pContact->GetClientID());
	if (pContactUpdate) {
		if (bUpdate) {
			if (pContactUpdate->GetUDPKey().GetKeyValue(theApp.GetPublicIPv4()) != 0
				&& pContactUpdate->GetUDPKey().GetKeyValue(theApp.GetPublicIPv4()) != pContact->GetUDPKey().GetKeyValue(theApp.GetPublicIPv4()))
			{
				// if our existing contact has a UDPSender-Key (which should be the case for all > = 0.49a clients)
				// except if our IP has changed recently, we demand that the key is the same as the key we received
				// from the packet which wants to update this contact in order to make sure this is not a try to
				// hijack this entry
				DebugLogWarning(_T("Kad: Sender (%s) tried to update contact entry but failed to provide the proper sender key (Sent Empty: %s) for the entry (%s) - denying update")
					, (LPCTSTR)ipstr(pContact->GetNetIP())
					, pContact->GetUDPKey().GetKeyValue(theApp.GetPublicIPv4()) == 0 ? _T("Yes") : _T("No")
					, (LPCTSTR)ipstr(pContactUpdate->GetNetIP()));
				bUpdate = false;
			} else if (pContactUpdate->GetVersion() >= KADEMLIA_VERSION1_46c && pContactUpdate->GetVersion() < KADEMLIA_VERSION6_49aBETA
				&& pContactUpdate->GetReceivedHelloPacket())
			{
				// legacy kad2 contacts are allowed only to update their RefreshTimer to avoid having them hijacked/corrupted by an attacker
				// (kad1 contacts do not have this restriction as they might turn out as kad2 later on)
				// only exception is if we didn't receive a HELLO packet from this client yet
				if (pContactUpdate->GetIPAddress() == pContact->GetIPAddress() && pContactUpdate->GetTCPPort() == pContact->GetTCPPort()
					&& pContactUpdate->GetVersion() == pContact->GetVersion() && pContactUpdate->GetUDPPort() == pContact->GetUDPPort())
				{
					ASSERT(!pContact->IsIpVerified()); // legacy kad2 nodes should be unable to verify their IP on a HELLO
					bOutIPVerified = pContactUpdate->IsIpVerified();
					m_pBin->SetAlive(pContactUpdate);
					theApp.emuledlg->kademliawnd->ContactRef(pContactUpdate);
					DEBUG_ONLY(AddDebugLogLine(DLP_VERYLOW, false, _T("Updated kad contact refresh timer only for legacy kad2 contact (%s, %u)")
						, (LPCTSTR)ipstr(pContactUpdate->GetNetIP()), pContactUpdate->GetVersion()));
				} else {
					AddDebugLogLine(DLP_DEFAULT, false, _T("Rejected value update for legacy kad2 contact (%s -> %s, %u -> %u)")
						, (LPCTSTR)ipstr(pContactUpdate->GetNetIP()), (LPCTSTR)ipstr(pContact->GetNetIP()), pContactUpdate->GetVersion(), pContact->GetVersion());
					bUpdate = false;
				}

			} else {
				if (pContact->GetUDPKey().GetKeyValue(theApp.GetPublicIPv4()) == 0) {
					if (pContact->GetVersion() >= KADEMLIA_VERSION6_49aBETA && pContact->GetType() < 2)
						AddDebugLogLine(DLP_LOW, false, _T("Updating > 0.49a + type < 2 contact without valid key stored %s"), (LPCTSTR)ipstr(pContact->GetNetIP()));
				} else
					AddDebugLogLine(DLP_LOW, false, _T("Updating contact, passed key check %s"), (LPCTSTR)ipstr(pContact->GetNetIP()));

				if (pContactUpdate->GetVersion() >= KADEMLIA_VERSION1_46c && pContactUpdate->GetVersion() < KADEMLIA_VERSION6_49aBETA) {
					ASSERT(!pContactUpdate->GetReceivedHelloPacket());
					AddDebugLogLine(DLP_LOW, false, _T("Accepted update for legacy kad2 contact, because of first HELLO (%s -> %s, %u -> %u)")
						, (LPCTSTR)ipstr(pContactUpdate->GetNetIP()), (LPCTSTR)ipstr(pContact->GetNetIP()), pContactUpdate->GetVersion(), pContact->GetVersion());
				}
				// All other nodes (Kad1, Kad2 > 0.49a with UDPKey checked or not set, first hello updates) are allowed to do full updates
				if (m_pBin->ChangeContactIPAddress(pContactUpdate, pContact->GetIPAddress())
					&& pContact->GetVersion() >= pContactUpdate->GetVersion()) // do not let Kad1 responses overwrite Kad2 ones
				{
					pContactUpdate->SetUDPPort(pContact->GetUDPPort());
					pContactUpdate->SetTCPPort(pContact->GetTCPPort());
					pContactUpdate->SetVersion(pContact->GetVersion());
					pContactUpdate->SetUDPKey(pContact->GetUDPKey());
					if (!pContactUpdate->IsIpVerified()) // don't unset the verified flag (will clear itself on ip change)
						pContactUpdate->SetIpVerified(pContact->IsIpVerified());
					bOutIPVerified = pContactUpdate->IsIpVerified();
					m_pBin->SetAlive(pContactUpdate);
					theApp.emuledlg->kademliawnd->ContactRef(pContactUpdate);
					if (pContact->GetReceivedHelloPacket())
						pContactUpdate->SetReceivedHelloPacket();
				} else
					bUpdate = false;
			}
		}
		return false;
	}

	if (m_pBin->GetRemaining()) {
		bUpdate = false;
		// This bin is not full, so add the new contact.
		if (m_pBin->AddContact(pContact)) {
			// Add was successful, add to the GUI and let the contact know it's listed in the GUI.
			if (theApp.emuledlg->kademliawnd->ContactAdd(pContact))
				pContact->SetGuiRefs(true);
			return true;
		}
		return false;
	}

	if (CanSplit()) {
		// This bin was full and split, call add on the correct branch.
		Split();
		return m_pSubZones[pContact->GetDistance().GetBitNumber(m_uLevel)]->Add(pContact, bUpdate, bOutIPVerified);
	}
	bUpdate = false;
	return false;
}

CContact* CRoutingZone::GetContact(const CUInt128 &uID) const
{
	if (IsLeaf())
		return m_pBin->GetContact(uID);

	CUInt128 uDistance;
	CKademlia::GetPrefs()->GetKadID(uDistance);
	uDistance.Xor(uID);
	return m_pSubZones[uDistance.GetBitNumber(m_uLevel)]->GetContact(uID);
}

CContact* CRoutingZone::GetContact(uint32 uIP, uint16 nPort, bool bTCPPort) const
{
	if (IsLeaf())
		return m_pBin->GetContact(uIP, nPort, bTCPPort);

	CContact *pContact = m_pSubZones[0]->GetContact(uIP, nPort, bTCPPort);
	return (pContact != NULL) ? pContact : m_pSubZones[1]->GetContact(uIP, nPort, bTCPPort);
}

CContact* CRoutingZone::GetRandomContact(uint32 nMaxType, uint32 nMinKadVersion) const
{
	if (IsLeaf())
		return m_pBin->GetRandomContact(nMaxType, nMinKadVersion);

	int nZone = rand() & 1;
	CContact *pContact = m_pSubZones[nZone]->GetRandomContact(nMaxType, nMinKadVersion);
	return (pContact != NULL) ? pContact : m_pSubZones[nZone ^ 1]->GetRandomContact(nMaxType, nMinKadVersion);
}

void CRoutingZone::GetClosestTo(uint32 uMaxType, const CUInt128 &uTarget, const CUInt128 &uDistance, uint32 uMaxRequired, ContactMap &rmapResult, bool bEmptyFirst, bool bInUse) const
{
	// If leaf zone, do it here
	if (IsLeaf()) {
		m_pBin->GetClosestTo(uMaxType, uTarget, uMaxRequired, rmapResult, bEmptyFirst, bInUse);
		return;
	}

	// otherwise, recurse in the closer-to-the-target subzone first
	int iCloser = uDistance.GetBitNumber(m_uLevel);
	m_pSubZones[iCloser]->GetClosestTo(uMaxType, uTarget, uDistance, uMaxRequired, rmapResult, bEmptyFirst, bInUse);

	// if still not enough tokens found, recurse in the other subzone too
	if (rmapResult.size() < uMaxRequired)
		m_pSubZones[1 - iCloser]->GetClosestTo(uMaxType, uTarget, uDistance, uMaxRequired, rmapResult, false, bInUse);
}

void CRoutingZone::GetAllEntries(ContactArray &listResult, bool bEmptyFirst)
{
	if (IsLeaf())
		m_pBin->GetEntries(listResult, bEmptyFirst);
	else {
		m_pSubZones[0]->GetAllEntries(listResult, bEmptyFirst);
		m_pSubZones[1]->GetAllEntries(listResult, false);
	}
}

void CRoutingZone::TopDepth(int iDepth, ContactArray &listResult, bool bEmptyFirst)
{
	if (IsLeaf())
		m_pBin->GetEntries(listResult, bEmptyFirst);
	else if (iDepth <= 0)
		RandomBin(listResult, bEmptyFirst);
	else {
		m_pSubZones[0]->TopDepth(iDepth - 1, listResult, bEmptyFirst);
		m_pSubZones[1]->TopDepth(iDepth - 1, listResult, false);
	}
}

void CRoutingZone::RandomBin(ContactArray &listResult, bool bEmptyFirst)
{
	if (IsLeaf())
		m_pBin->GetEntries(listResult, bEmptyFirst);
	else
		m_pSubZones[rand() & 1]->RandomBin(listResult, bEmptyFirst);
}

uint32 CRoutingZone::GetMaxDepth() const
{
	return IsLeaf() ? 0 : 1 + max(m_pSubZones[0]->GetMaxDepth(), m_pSubZones[1]->GetMaxDepth());
}

void CRoutingZone::Split()
{
	StopTimer();

	m_pSubZones[0] = GenSubZone(0);
	m_pSubZones[1] = GenSubZone(1);

	ContactArray listEntries;
	m_pBin->GetEntries(listEntries);
	m_pBin->m_bDontDeleteContacts = true;
	delete m_pBin;
	m_pBin = NULL;

	for (ContactArray::const_iterator itContact = listEntries.begin(); itContact != listEntries.end(); ++itContact) {
		int iSuperZone = (*itContact)->m_uDistance.GetBitNumber(m_uLevel);
		if (!m_pSubZones[iSuperZone]->m_pBin->AddContact(*itContact))
			delete *itContact;
	}
}

uint32 CRoutingZone::Consolidate()
{
	if (IsLeaf())
		return 0;
	ASSERT(m_pBin == NULL);
	uint32 uMergeCount = 0;
	if (!m_pSubZones[0]->IsLeaf())
		uMergeCount += m_pSubZones[0]->Consolidate();
	if (!m_pSubZones[1]->IsLeaf())
		uMergeCount += m_pSubZones[1]->Consolidate();
	if (m_pSubZones[0]->IsLeaf() && m_pSubZones[1]->IsLeaf() && GetNumContacts() < K / 2) {
		m_pBin = new CRoutingBin();
		m_pSubZones[0]->StopTimer();
		m_pSubZones[1]->StopTimer();

		ContactArray list0;
		ContactArray list1;
		m_pSubZones[0]->m_pBin->GetEntries(list0);
		m_pSubZones[1]->m_pBin->GetEntries(list1);

		m_pSubZones[0]->m_pBin->m_bDontDeleteContacts = true;
		m_pSubZones[1]->m_pBin->m_bDontDeleteContacts = true;
		delete m_pSubZones[0];
		delete m_pSubZones[1];
		m_pSubZones[0] = NULL;
		m_pSubZones[1] = NULL;

		for (ContactArray::const_iterator itContact = list0.begin(); itContact != list0.end(); ++itContact)
			if (!m_pBin->AddContact(*itContact))
				delete *itContact;

		for (ContactArray::const_iterator itContact = list1.begin(); itContact != list1.end(); ++itContact)
			if (!m_pBin->AddContact(*itContact))
				delete *itContact;

		StartTimer();
		++uMergeCount;
	}
	return uMergeCount;
}


CRoutingZone* CRoutingZone::GenSubZone(int iSide)
{
	CUInt128 uNewIndex(m_uZoneIndex);
	uNewIndex.ShiftLeft(1);
	if (iSide != 0)
		uNewIndex.Add(1);
	return new CRoutingZone(this, m_uLevel + 1, uNewIndex);
}

void CRoutingZone::StartTimer()
{
	// Start filling the tree, closest bins first.
	m_tNextBigTimer = time(NULL) + SEC(10);
	CKademlia::AddEvent(this);
}


bool CRoutingZone::OnBigTimer()
{
	if (IsLeaf() && (m_uZoneIndex < KK || m_uLevel < KBASE || m_pBin->GetRemaining() >= (K*.8))) {
		RandomLookup();
		return true;
	}

	return false;
}

//This is used when we find a leaf and want to know what this sample looks like.
//We fall back two levels and take a sample to try to minimize any areas of the
//tree that will give very bad results.
uint32 CRoutingZone::EstimateCount()
{
	if (!IsLeaf())
		return 0;
	if (m_uLevel < KBASE)
		return (uint32)(pow(2.0f, (int)m_uLevel) * K);
	CRoutingZone *pCurZone = m_pSuperZone->m_pSuperZone->m_pSuperZone;
	// Find out how full this part of the tree is.
	float fModify = pCurZone->GetNumContacts() / (K * 2.0f);
	// First calculate users assuming the tree is full.
	// Modify count by bin size.
	// Modify count by how full the tree is.

	// LowIDModififier
	// Modify count by assuming 20% of the users are firewalled and can't be a contact for < 0.49b nodes
	// Modify count by actual statistics of Firewalled ratio for >= 0.49b if we are not firewalled ourself
	// Modify count by 40% for >= 0.49b if we are firewalled ourself (the actual Firewalled count at this date on kad is 35-55%)
	const float fFirewalledModifyOld = 1.20F;
	float fFirewalledModifyNew;
	if (CUDPFirewallTester::IsFirewalledUDP(true))
		fFirewalledModifyNew = 1.40F; // we are firewalled and to get the real statistic, assume 40% firewalled >=0.49b nodes
	else if (CKademlia::GetPrefs()->StatsGetFirewalledRatio(true) > 0) {
		fFirewalledModifyNew = 1.0F + (CKademlia::GetPrefs()->StatsGetFirewalledRatio(true)); // apply the firewalled ratio to the modify
		ASSERT(fFirewalledModifyNew > 1.0F && fFirewalledModifyNew < 1.90F);
	} else
		fFirewalledModifyNew = 0;
	float fNewRatio = CKademlia::GetPrefs()->StatsGetKadV8Ratio();
	float fFirewalledModifyTotal;
	if (fNewRatio > 0 && fFirewalledModifyNew > 0) // weigh the old and the new modifier based on how many new contacts we have
		fFirewalledModifyTotal = (fNewRatio * fFirewalledModifyNew) + ((1 - fNewRatio) * fFirewalledModifyOld);
	else
		fFirewalledModifyTotal = fFirewalledModifyOld;
	ASSERT(fFirewalledModifyTotal > 1.0F && fFirewalledModifyTotal < 1.90F);

	return (uint32)(pow(2.0F, (int)m_uLevel - 2) * K * fModify * fFirewalledModifyTotal);
}

void CRoutingZone::OnSmallTimer()
{
	if (!IsLeaf())
		return;

	// If we recently rebound AND already have contacts, temporarily pause probing to avoid stale IP pings
	if (theApp.clientudp != NULL && GetNumContacts() > 0 && (::GetTickCount() - theApp.clientudp->GetLastRebindTime()) < ROUTING_GRACE_PERIOD_MS)
		return; // Grace pause only affects established tables

	CContact *pContact = NULL;
	time_t tNow = time(NULL);
	ContactArray listEntries;
	// Remove dead entries
	m_pBin->GetEntries(listEntries);
	for (ContactArray::const_iterator itContact = listEntries.begin(); itContact != listEntries.end(); ++itContact) {
		pContact = *itContact;
		bool const bBanned = safeKad.IsBanned(pContact->GetIPAddress()); // Remove contacts for which we have banned (due to bad behaviour) the IP address
		if (pContact->GetType() == 4 || bBanned) {
			if (((pContact->m_tExpires > 0) && (pContact->m_tExpires <= tNow)) || bBanned) {
				if (!pContact->InUse()) {
					m_pBin->RemoveContact(pContact);
					delete pContact;
				}
				continue;
			}
		}
		if (pContact->m_tExpires == 0)
			pContact->m_tExpires = tNow;
	}
	pContact = m_pBin->GetOldest();
	if (pContact != NULL)
		if (pContact->m_tExpires >= tNow || pContact->GetType() == 4) {
			m_pBin->PushToBottom(pContact);
			pContact = NULL;
		}
	if (pContact != NULL) {
		pContact->CheckingType();
		if (pContact->GetVersion() >= KADEMLIA_VERSION6_49aBETA) {
			if (thePrefs.GetDebugClientKadUDPLevel() > 0)
				DebugSend("KADEMLIA2_HELLO_REQ", pContact->GetIPAddress(), pContact->GetUDPPort());
			CUInt128 uClientID = pContact->GetClientID();
			CKademlia::GetUDPListener()->SendMyDetails(KADEMLIA2_HELLO_REQ, pContact->GetIPAddress(), pContact->GetUDPPort(), pContact->GetVersion(), pContact->GetUDPKey(), &uClientID, false);
			if (pContact->GetVersion() >= KADEMLIA_VERSION8_49b) {
				// FIXME:
				// This is a bit of a work around for statistic values. Normally we only count values from
				// incoming HELLO_REQs for the firewalled statistics in order to get numbers from nodes
				// which have us on their routing table, however if we send a HELLO due to the timer,
				// the remote node won't send a HELLO_REQ itself any more (but a HELLO_RES which we don't count),
				// so count those statistics here. This isn't really accurate, but it should do fair enough.
				// Maybe improve it later for example by putting a flag into the contact and make the answer count
				CKademlia::GetPrefs()->StatsIncUDPFirewalledNodes(false);
				CKademlia::GetPrefs()->StatsIncTCPFirewalledNodes(false);
			}
		} else if (pContact->GetVersion() >= KADEMLIA_VERSION2_47a) {
			if (thePrefs.GetDebugClientKadUDPLevel() > 0)
				DebugSend("KADEMLIA2_HELLO_REQ", pContact->GetIPAddress(), pContact->GetUDPPort());
			CKademlia::GetUDPListener()->SendMyDetails(KADEMLIA2_HELLO_REQ, pContact->GetIPAddress(), pContact->GetUDPPort(), pContact->GetVersion(), CKadUDPKey(), NULL, false);
			ASSERT(CKadUDPKey() == pContact->GetUDPKey());
		} else
			ASSERT(0);
	}
}

void CRoutingZone::RandomLookup()
{
	// Look-up a random client in this zone
	CUInt128 uPrefix(m_uZoneIndex);
	uPrefix.ShiftLeft(128 - m_uLevel);
	CUInt128 uRandom(uPrefix, m_uLevel);
	uRandom.Xor(uMe);
	CSearchManager::FindNode(uRandom, false);
}

uint32 CRoutingZone::GetNumContacts() const
{
	if (IsLeaf())
		return m_pBin->GetSize();
	return m_pSubZones[0]->GetNumContacts() + m_pSubZones[1]->GetNumContacts();
}

void CRoutingZone::GetNumContacts(uint32 &nInOutContacts, uint32 &nInOutFilteredContacts, uint8 byMinVersion) const
{
	if (IsLeaf())
		m_pBin->GetNumContacts(nInOutContacts, nInOutFilteredContacts, byMinVersion);
	else {
		m_pSubZones[0]->GetNumContacts(nInOutContacts, nInOutFilteredContacts, byMinVersion);
		m_pSubZones[1]->GetNumContacts(nInOutContacts, nInOutFilteredContacts, byMinVersion);
	}
}

uint32 CRoutingZone::GetBootstrapContacts(ContactArray &rlistResult, uint32 uMaxRequired)
{
	ASSERT(m_pSuperZone == NULL);
	rlistResult.clear();
	uint32 uRetVal = 0;
	try {
		ContactArray top;
		TopDepth(LOG_BASE_EXPONENT, top);
		if (!top.empty())
			for (ContactArray::const_iterator itContact = top.begin(); uRetVal < uMaxRequired && itContact != top.end(); ++itContact) {
				rlistResult.push_back(*itContact);
				++uRetVal;
		}
	} catch (...) {
		AddDebugLogLine(false, _T("Exception in CRoutingZone::getBoostStrapContacts"));
	}
	return uRetVal;
}

bool CRoutingZone::VerifyContact(const CUInt128 &uID, uint32 uIP) const
{
	CContact *pContact = GetContact(uID);
	if (pContact == NULL || uIP != pContact->GetIPAddress())
		return false;

	if (pContact->IsIpVerified())
		DebugLogWarning(_T("Kad: VerifyContact: Sender already verified (sender: %s)"), (LPCTSTR)ipstr(htonl(uIP)));
	else {
		pContact->SetIpVerified(true);
		theApp.emuledlg->kademliawnd->ContactRef(pContact);
	}
	return true;
}

void CRoutingZone::SetAllContactsVerified()
{
	if (IsLeaf())
		m_pBin->SetAllContactsVerified();
	else {
		m_pSubZones[0]->SetAllContactsVerified();
		m_pSubZones[1]->SetAllContactsVerified();
	}
}

bool CRoutingZone::IsAcceptableContact(const CContact *pToCheck) const
{
	// Check if we know a contact with the same ID or IP but not matching IP/ID and other limitations
	// This is similar to adding a node to the table except alloweding duplicates
	// We use this to check KADEMLIA_RES routing answers on searches
	if (pToCheck->GetVersion() < KADEMLIA_VERSION2_47a)	// No Kad1 Contacts allowed
		return false;
	CContact *pDuplicate = GetContact(pToCheck->GetClientID());
	if (pDuplicate != NULL) {
		//false - a verified node with different IP exists
		//true - node exists already in our routing table, that's fine
		return !pDuplicate->IsIpVerified()
			|| (pDuplicate->GetIPAddress() == pToCheck->GetIPAddress() && pDuplicate->GetUDPPort() == pToCheck->GetUDPPort());
	}
	// if the node is not yet known, check if IP limitations would hit
#ifdef _DEBUG
	return CRoutingBin::CheckGlobalIPLimits(pToCheck->GetIPAddress(), pToCheck->GetUDPPort(), true);
#else
	return CRoutingBin::CheckGlobalIPLimits(pToCheck->GetIPAddress(), pToCheck->GetUDPPort(), false);
#endif
}

bool CRoutingZone::HasOnlyLANNodes() const
{
	if (IsLeaf())
		return m_pBin->HasOnlyLANNodes();
	return m_pSubZones[0]->HasOnlyLANNodes() && m_pSubZones[1]->HasOnlyLANNodes();
}
