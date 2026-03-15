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
#include "UpDownClient.h"
#include "PartFile.h"
#include "ListenSocket.h"
#include "Preferences.h"
#include "SafeFile.h"
#include "Packets.h"
#include "Statistics.h"
#include "ClientCredits.h"
#include "DownloadQueue.h"
#include "ClientUDPSocket.h"
#include "emuledlg.h"
#include "TransferDlg.h"
#include "Exceptions.h"
#include "clientlist.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "Kademlia/Kademlia/Prefs.h"
#include "Kademlia/Kademlia/Search.h"
#include "SHAHashSet.h"
#include "SharedFileList.h"
#include "Log.h"
#include "zlib/zlib.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//	members of CUpDownClient
//	which are mainly used for downloading functions
CBarShader CUpDownClient::s_StatusBar(16);
void CUpDownClient::DrawStatusBar(CDC *dc, const CRect &rect, bool onlygreyrect, bool  bFlat) const
{
	if (g_bLowColorDesktop)
		bFlat = true;

	COLORREF crNeither;
	if (bFlat)
		crNeither = g_bLowColorDesktop ? RGB(192, 192, 192) : RGB(224, 224, 224);
	else
		crNeither = RGB(240, 240, 240);

	if (m_reqfile) {
		s_StatusBar.SetFileSize(m_reqfile->GetFileSize());
		s_StatusBar.SetRect(rect);
		s_StatusBar.Fill(crNeither);

		if (!onlygreyrect && (m_abyPartStatus || m_abyIncPartStatus)) {
			COLORREF crBoth;
			COLORREF crClientOnly;
			COLORREF crPending;
			COLORREF crNextPending;
			COLORREF crClientPartial;
			if (g_bLowColorDesktop) {
				crBoth = RGB(0, 0, 0);
				crClientOnly = RGB(0, 0, 255);
				crPending = RGB(0, 255, 0);
				crNextPending = RGB(255, 255, 0);
				crClientPartial = RGB(255, 191, 0);
			} else if (bFlat) {
				crBoth = RGB(0, 0, 0);
				crClientOnly = RGB(0, 100, 255);
				crPending = RGB(0, 150, 0);
				crNextPending = RGB(255, 208, 0);
				crClientPartial = RGB(255, 191, 255);
			} else {
				crBoth = RGB(104, 104, 104);
				crClientOnly = RGB(0, 100, 255);
				crPending = RGB(0, 150, 0);
				crNextPending = RGB(255, 208, 0);
				crClientPartial = RGB(255, 191, 255);
			}

			char *pcNextPendingBlks;
			if (m_eDownloadState == DS_DOWNLOADING) {
				pcNextPendingBlks = new char[m_nPartCount]{};
				for (POSITION pos = m_PendingBlocks_list.GetHeadPosition(); pos != NULL;) {
					UINT uPart = (UINT)(m_PendingBlocks_list.GetNext(pos)->block->StartOffset / PARTSIZE);
					if (uPart < m_nPartCount)
						pcNextPendingBlks[uPart] = 1;
				}
			} else
				pcNextPendingBlks = NULL;

			for (UINT i = 0; i < m_nPartCount; ++i)
				if (m_abyPartStatus[i]) {
					uint64 uBegin = PARTSIZE * i;
					uint64 uEnd = min(uBegin + PARTSIZE, (uint64)m_reqfile->GetFileSize());

					COLORREF colour;
					if (m_reqfile->IsComplete(uBegin, uEnd - 1))
						colour = crBoth;
					else if (m_eDownloadState == DS_DOWNLOADING && GetSessionDown() && m_nLastBlockOffset >= uBegin && m_nLastBlockOffset < uEnd)
						colour = crPending;
					else if (pcNextPendingBlks && pcNextPendingBlks[i])
						colour = crNextPending;
					else
						colour = crClientOnly;
					s_StatusBar.FillRange(uBegin, uEnd, colour);
				}
				else if (m_abyIncPartStatus && m_abyIncPartStatus[i]) {
					uint64 uEnd;
					if ((uint32)PARTSIZE * (uint64)(i + 1) > m_reqfile->GetFileSize())
						uEnd = m_reqfile->GetFileSize();
					else
						uEnd = PARTSIZE * (uint64)(i + 1);

					s_StatusBar.FillRange(PARTSIZE * (uint64)i, uEnd, crClientPartial);
				}

			delete[] pcNextPendingBlks;
		}
	} else
		ASSERT(0);
	s_StatusBar.Draw(dc, rect.left, rect.top, bFlat);
}

bool CUpDownClient::Compare(const CUpDownClient *tocomp, bool bIgnoreUserhash) const
{
	//Compare only the user hash.
	if (!bIgnoreUserhash && HasValidHash() && tocomp->HasValidHash())
		return md4equ(this->GetUserHash(), tocomp->GetUserHash());

	if (HasLowID()) {
		//User is firewalled. Must do two checks.
		if ((!GetIPv4().IsNull() && GetIPv4() == tocomp->GetIPv4()) || (!GetIPv6().IsNull() && GetIPv6() == tocomp->GetIPv6())) {
			//Both IPs match
			if (GetUserPort() != 0 && GetUserPort() == tocomp->GetUserPort())
				//IP-UserPort matches
				return true;
			if (GetKadPort() != 0 && GetKadPort() == tocomp->GetKadPort())
				//IP-KadPort Matches
				return true;
		}
		if (GetUserIDHybrid() != 0 && GetUserIDHybrid() == tocomp->GetUserIDHybrid()
			&& GetServerIP() != 0 && GetServerIP() == tocomp->GetServerIP()
			&& GetServerPort() != 0 && GetServerPort() == tocomp->GetServerPort())
		{ //Both have the same lowID, server IP and port.
			return true;
		}
#if defined(_DEBUG)
		if (HasValidServingBuddyID() && tocomp->HasValidServingBuddyID()) {
			//JOHNTODO: This is for future use to see if this will be needed...
			if (md4equ(GetServingBuddyID(), tocomp->GetServingBuddyID()))
				return true;
		}
#endif
		//Both IP, and Server do not match.
		return false;
	}

	//User is not firewalled.
	if (GetUserPort() != 0 && GetUserPort() == tocomp->GetUserPort()
		|| (GetKadPort() != 0 && GetKadPort() == tocomp->GetKadPort()))
	{
		//User has a Port and/or KAD port, lets check the rest.
		if ((!GetIPv4().IsNull() && GetIPv4() == tocomp->GetIPv4()) || (!GetIPv6().IsNull() && GetIPv6() == tocomp->GetIPv6())) { //Both clients have verified IPs
			if (GetIP() == tocomp->GetIP()) //IP and port match.
				return true;
		} else if (GetUserIDHybrid() == tocomp->GetUserIDHybrid())
			//One of the two clients does not have a verified IP, but ID and port match
			return true;
	}

	//No Matches.
	return false;
}

// Return bool is not if you asked or not.
// false = Client was deleted!
// true = client was not deleted!
bool CUpDownClient::AskForDownload()
{
	if (m_bUDPPending) {
		++m_nFailedUDPPackets;
		theApp.downloadqueue->AddFailedUDPFileReasks();
		m_bUDPPending = false;
	}
	if (socket && socket->IsConnected()) // already connected, skip all the special checks
		SetLastTriedToConnectTime();
	else {
		if (theApp.listensocket->TooManySockets()) {
			if (GetDownloadState() != DS_TOOMANYCONNS)
				SetDownloadState(DS_TOOMANYCONNS);
			return true;
		}
		SetLastTriedToConnectTime();

		if (HasLowID() && GetLastAskedTime() > 0) {
			// if it's a lowid client which is on our queue we may delay the re-ask up to 20 min
			// to give the lowid a chance to connect to us with its own re-ask
			if (GetUploadState() == US_ONUPLOADQUEUE && !m_bReaskPending) {
				SetDownloadState(DS_ONQUEUE);
				m_bReaskPending = true;
				return true;
			}
			// if we are lowid <-> lowid but contacted the source before already, keep it in the hope that we might turn highid again
			if (!theApp.CanDoCallback(this)) {
				if (GetDownloadState() != DS_LOWTOLOWIP)
					SetDownloadState(DS_LOWTOLOWIP);
				m_bReaskPending = true;
				return true;
			}
		}
	}
	SwapToAnotherFile(_T("A4AF check before TCP file re-ask. CUpDownClient::AskForDownload()"), true, false, false, NULL, true, true);
	if (m_cFailed == 0)
		theApp.downloadqueue->IncrementTCPFileReask();
	SetDownloadState(DS_CONNECTING);
	return TryToConnect();
}

bool CUpDownClient::IsSourceRequestAllowed(CPartFile *partfile, bool sourceExchangeCheck) const
{
	if (!m_reqfile)
		return false;

	DWORD dwTicks = ::GetTickCount() + CONNECTION_LATENCY;
	DWORD nTimePassedClient = dwTicks - GetLastAskedForSources(); //was GetLastSrcAnswerTime();
	DWORD nTimePassedFile = dwTicks - partfile->GetLastAnsweredTime();
	bool bNeverAskedBefore = (GetLastAskedForSources() == 0);

	// If there are cached sources available we don't need new XS
	if (partfile->GetSourceCacheAmount() > 0)
		return false;

	UINT uSources = partfile->GetSourceCount();
	UINT uValidSources = partfile->GetValidSourcesCount();

	if (partfile != m_reqfile) {
		++uSources;
		++uValidSources;
	}

	UINT uReqValidSources = m_reqfile->GetValidSourcesCount();

	return //if client has the correct extended protocol
		   ExtProtocolAvailable() && (SupportsSourceExchange2() || GetSourceExchange1Version() > 1)
		//AND if we need more sources
		&& m_reqfile->GetMaxSourcePerFileSoft() > uSources
		//AND if...
		&& (
			//source is not complete and file is very rare
			(!m_bCompleteSource
			  && (bNeverAskedBefore || nTimePassedClient > SOURCECLIENTREASKS)
			  && (uSources <= RARE_FILE / 5)
			  && (!sourceExchangeCheck || partfile == m_reqfile || (uValidSources < uReqValidSources && uReqValidSources > 3))
			)
			||
			//OR source is not complete and file is rare
			(!m_bCompleteSource
			  && (bNeverAskedBefore || nTimePassedClient > SOURCECLIENTREASKS)
			  && (uSources <= RARE_FILE || ((!sourceExchangeCheck || partfile == m_reqfile) && uSources <= RARE_FILE / 2 + uValidSources))
			  && (nTimePassedFile > SOURCECLIENTREASKF)
			  && (!sourceExchangeCheck || partfile == m_reqfile || (uValidSources < SOURCECLIENTREASKS / SOURCECLIENTREASKF && uValidSources < uReqValidSources))
			)
			// OR if file is not rare
			||
			((bNeverAskedBefore || nTimePassedClient > SOURCECLIENTREASKS * MINCOMMONPENALTY)
			  && (nTimePassedFile > SOURCECLIENTREASKF * MINCOMMONPENALTY)
			  && (!sourceExchangeCheck || partfile == m_reqfile || (uValidSources < SOURCECLIENTREASKS / SOURCECLIENTREASKF && uValidSources < uReqValidSources))
			)
		   );
}

void CUpDownClient::TriggerFileRequestIfNeeded()
{
	// Helper function to trigger file request if conditions are met
	// Only send request if: 1) we have a reqfile, 2) DS_CONNECTED, 3) no pending request (or NAT-T bypass)
	
	// For NAT-T/uTP connections, bypass QueueRankPending check - it may be stale from previous connection attempts
	bool bIsUtpConnection = (socket && socket->HaveUtpLayer());
	bool bBypassPendingCheck = bIsUtpConnection && HasLowID();

	// Keep ED2K file request strictly after Hello/HelloAnswer on uTP.
	// Sending OP_STARTUPLOADREQ before handshake completion can lead to connection aborts on simultaneous connects.
	if (bIsUtpConnection && IsHelloAnswerPending()) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] TriggerFileRequestIfNeeded: Waiting for OP_HELLOANSWER before file request, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		return;
	}
	
	// If reqfile is NULL and this is a NAT-T connection, try to find the request file
	if (m_reqfile == NULL && bIsUtpConnection) {
		// Try to resolve request file from download queue by this client's IP/port
		CAddress cip = GetConnectIP().IsNull() ? GetIP() : GetConnectIP();
		uint16 cport = GetKadPort() ? GetKadPort() : GetUDPPort();
		CUpDownClient* pCtx = NULL;
		bool bMulti = false;
		if (!cip.IsNull() && cport != 0)
			pCtx = theApp.downloadqueue->GetDownloadClientByIP_UDP(cip, cport, true, &bMulti);
		if (pCtx == NULL && !cip.IsNull())
			pCtx = theApp.downloadqueue->GetDownloadClientByIP(cip);
		// Fallback: search by user hash
		if (pCtx == NULL && HasValidHash()) {
			CUpDownClient* byHash = theApp.clientlist->FindClientByUserHash(GetUserHash(), CAddress(), 0);
			if (byHash && byHash != this && theApp.clientlist->IsValidClient(byHash))
				pCtx = byHash;
		}
		if (pCtx && pCtx != this && theApp.clientlist->IsValidClient(pCtx) && pCtx->GetRequestFile() != NULL) {
			SetRequestFile(pCtx->GetRequestFile());
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] TriggerFileRequestIfNeeded: Resolved reqfile from context client, file=%s"),
					(LPCTSTR)EscPercent(m_reqfile->GetFileName()));
		}
	}
	
	// Now check if we can send the file request
	bool bCanSend = (m_reqfile != NULL && GetDownloadState() == DS_CONNECTED && 
					 (m_fQueueRankPending == 0 || bBypassPendingCheck));
	
	if (bCanSend) {
		// Clear pending flag for NAT-T if we're bypassing
		if (bBypassPendingCheck && m_fQueueRankPending != 0) {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NatTraversal] TriggerFileRequestIfNeeded: Clearing stale QueueRankPending for uTP connection"));
			m_fQueueRankPending = 0;
		}
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] TriggerFileRequestIfNeeded: Sending file request for %s via %s"),
				(LPCTSTR)EscPercent(DbgGetClientInfo()), 
				bIsUtpConnection ? _T("uTP") : _T("TCP"));
		SendFileRequest();
	} else if (m_reqfile != NULL && m_fQueueRankPending != 0 && !bBypassPendingCheck) {
		if (thePrefs.GetLogNatTraversalEvents())
			DebugLog(_T("[NatTraversal] TriggerFileRequestIfNeeded: Skipping SendFileRequest (already pending, QueueRankPending=%u), %s"),
				(unsigned)m_fQueueRankPending, (LPCTSTR)EscPercent(DbgGetClientInfo()));
	} else if (thePrefs.GetLogNatTraversalEvents()) {
		DebugLog(_T("[NatTraversal] TriggerFileRequestIfNeeded: Conditions not met (state=%d reqfile=%s pending=%u uTP=%d), %s"),
			GetDownloadState(), m_reqfile ? _T("YES") : _T("NULL"), 
			(unsigned)m_fQueueRankPending, bIsUtpConnection ? 1 : 0, (LPCTSTR)EscPercent(DbgGetClientInfo()));
	}
}

void CUpDownClient::SendFileRequest()
{
	// normally asktime has already been reset here, and SwapToAnotherFile will return without much work, so check to make sure
	SwapToAnotherFile(_T("A4AF check before TCP file re-ask. CUpDownClient::SendFileRequest()"), true, false, false, NULL, true, true);

	if (!m_reqfile) {
		ASSERT(0);
		return;
	}
	IncrementAskedCountDown();
	const bool bCanUseExtendedInfo = (GetExtendedRequestsVersion() > 0);
	const bool bCanUseExtCompleteSources = (bCanUseExtendedInfo && GetExtendedRequestsVersion() > 1);

	if (SupportMultiPacket() || SupportsFileIdentifiers()) {
		CSafeMemFile dataFileReq(96);
		LPCSTR pDebug;
		if (SupportsFileIdentifiers()) {
			m_reqfile->GetFileIdentifier().WriteIdentifier(dataFileReq);
			pDebug = "OP_MultiPacket_Ext2";
		} else {
			dataFileReq.WriteHash16(m_reqfile->GetFileHash());
			if (SupportExtMultiPacket()) {
				dataFileReq.WriteUInt64(m_reqfile->GetFileSize());
				pDebug = "OP_MultiPacket_Ext";
			} else
				pDebug = "OP_MultiPacket";
		}
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend(pDebug, this, m_reqfile->GetFileHash());

		// OP_REQUESTFILENAME + ExtInfo
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP_MPReqFileName", this, m_reqfile->GetFileHash());
		dataFileReq.WriteUInt8(OP_REQUESTFILENAME);
		if (bCanUseExtendedInfo) {
			m_reqfile->WritePartStatus(dataFileReq);
			if (bCanUseExtCompleteSources)
				m_reqfile->WriteCompleteSourcesCount(dataFileReq);
		}

		// OP_SETREQFILEID
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP_MPSetReqFileID", this, m_reqfile->GetFileHash());
		if (m_reqfile->GetPartCount() > 1)
			dataFileReq.WriteUInt8(OP_SETREQFILEID);

		if (IsEmuleClient()) {
			SetRemoteQueueFull(true);
			SetRemoteQueueRank(0);
		}

		// OP_REQUESTSOURCES // OP_REQUESTSOURCES2
		if (IsSourceRequestAllowed()) {
			if (thePrefs.GetDebugClientTCPLevel() > 0) {
				DebugSend("OP_MPReqSources", this, m_reqfile->GetFileHash());
				if (GetLastAskedForSources() == 0)
					Debug(_T("  first source request\n"));
				else
					Debug(_T("  last source request was before %s\n"), (LPCTSTR)CastSecondsToHM((::GetTickCount() - GetLastAskedForSources()) / SEC2MS(1)));
			}

			//Xman because official client has a bug, only count the XSReqs under special conditions
			//remark: this fails for complete sources the first time.. but this doesn't hurt
			if ((m_bCompleteSource || !md4equ(GetUploadFileID(), m_reqfile->GetFileHash())) //complete source of we want the same file
				&& m_reqfile->GetValidSourcesCount() > 10) //if we know at least 10 good sources, the remote client should know at least one
				IncXSReqs();

			if (SupportsSourceExchange2()) {
				dataFileReq.WriteUInt8(OP_REQUESTSOURCES2);
				dataFileReq.WriteUInt8(SupportsExtendedSourceExchange() ? SOURCEEXCHANGEEXT_VERSION : SOURCEEXCHANGE2_VERSION);
				const uint16 nOptions = 0; // 16 ... Reserved
				dataFileReq.WriteUInt16(nOptions);
			} else
				dataFileReq.WriteUInt8(OP_REQUESTSOURCES);

			m_reqfile->SetLastAnsweredTimeTimeout();
			SetLastAskedForSources();
			if (thePrefs.GetDebugSourceExchange())
				AddDebugLogLine(false, _T("SXSend (%s): Client source request; %s, File=\"%s\""), SupportsSourceExchange2() ? _T("Version 2") : _T("Version 1"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (LPCTSTR)EscPercent(m_reqfile->GetFileName()));
		}

		// OP_AICHFILEHASHREQ - deprecated with file identifiers
		if (IsSupportingAICH() && !SupportsFileIdentifiers()) {
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugSend("OP_MPAichFileHashReq", this, m_reqfile->GetFileHash());
			dataFileReq.WriteUInt8(OP_AICHFILEHASHREQ);
		}

		Packet *packet = new Packet(dataFileReq, OP_EMULEPROT);
		if (SupportsFileIdentifiers())
			packet->opcode = OP_MULTIPACKET_EXT2;
		else if (SupportExtMultiPacket())
			packet->opcode = OP_MULTIPACKET_EXT;
		else
			packet->opcode = OP_MULTIPACKET;
		theStats.AddUpDataOverheadFileRequest(packet->size);
		SendPacket(packet);
	} else {
		CSafeMemFile dataFileReq(96);
		dataFileReq.WriteHash16(m_reqfile->GetFileHash());
		//This is extended information
		if (bCanUseExtendedInfo) {
			m_reqfile->WritePartStatus(dataFileReq);
			if (bCanUseExtCompleteSources)
				m_reqfile->WriteCompleteSourcesCount(dataFileReq);
		}
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP_FileRequest", this, m_reqfile->GetFileHash());
		Packet *packet = new Packet(dataFileReq);
		packet->opcode = OP_REQUESTFILENAME;
		theStats.AddUpDataOverheadFileRequest(packet->size);
		SendPacket(packet);

		// 26-Jul-2003: removed requesting the file status for files <= PARTSIZE for better compatibility
		// with ed2k protocol (eDonkeyHybrid). if the remote client answers the OP_REQUESTFILENAME
		// with OP_REQFILENAMEANSWER the file is shared by the remote client. if we know that the file
		// is shared, we know also that the file is complete and don't need to request the file status.
		if (m_reqfile->GetPartCount() > 1) {
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugSend("OP_SetReqFileID", this, m_reqfile->GetFileHash());
			CSafeMemFile dataSetReqFileID(16);
			dataSetReqFileID.WriteHash16(m_reqfile->GetFileHash());
			packet = new Packet(dataSetReqFileID);
			packet->opcode = OP_SETREQFILEID;
			theStats.AddUpDataOverheadFileRequest(packet->size);
			SendPacket(packet);
		}

		if (IsEmuleClient()) {
			SetRemoteQueueFull(true);
			SetRemoteQueueRank(0);
		}

		if (IsSourceRequestAllowed()) {
			if (thePrefs.GetDebugClientTCPLevel() > 0) {
				DebugSend("OP_RequestSources", this, m_reqfile->GetFileHash());
				if (GetLastAskedForSources() == 0)
					Debug(_T("  first source request\n"));
				else
					Debug(_T("  last source request was before %s\n"), (LPCTSTR)CastSecondsToHM((::GetTickCount() - GetLastAskedForSources()) / SEC2MS(1)));
			}

			//Xman because official client has a bug, only count the XSReqs under special conditions
			//remark: this fails for complete sources the first time.. but this doesn't hurt
			if ((m_bCompleteSource || !md4equ(GetUploadFileID(), m_reqfile->GetFileHash())) //complete source of we want the same file
				&& m_reqfile->GetValidSourcesCount() > 10) //if we know at least 10 good sources, the remote client should know at least one
				IncXSReqs();

			m_reqfile->SetLastAnsweredTimeTimeout();

			Packet *packet1;
			if (SupportsSourceExchange2()) {
				packet1 = new Packet(OP_REQUESTSOURCES2, 19, OP_EMULEPROT);
				PokeUInt8(&packet->pBuffer[0], SupportsExtendedSourceExchange() ? SOURCEEXCHANGEEXT_VERSION : SOURCEEXCHANGE2_VERSION);
				const uint16 nOptions = 0; // 16 ... Reserved
				PokeUInt16(&packet1->pBuffer[1], nOptions);
				md4cpy(&packet1->pBuffer[3], m_reqfile->GetFileHash());
			} else {
				packet1 = new Packet(OP_REQUESTSOURCES, 16, OP_EMULEPROT);
				md4cpy(packet1->pBuffer, m_reqfile->GetFileHash());
			}

			theStats.AddUpDataOverheadSourceExchange(packet1->size);
			SendPacket(packet1);
			SetLastAskedForSources();
			if (thePrefs.GetDebugSourceExchange())
				AddDebugLogLine(false, _T("SXSend (%s): Client source request; %s, File=\"%s\""), SupportsSourceExchange2() ? _T("Version 2") : _T("Version 1"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (LPCTSTR)EscPercent(m_reqfile->GetFileName()));
		}

		if (IsSupportingAICH()) {
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugSend("OP_AichFileHashReq", this, m_reqfile->GetFileHash());
			Packet *packet2 = new Packet(OP_AICHFILEHASHREQ, 16, OP_EMULEPROT);
			md4cpy(packet2->pBuffer, m_reqfile->GetFileHash());
			theStats.AddUpDataOverheadFileRequest(packet2->size);
			SendPacket(packet2);
		}
	}
	SetLastAskedTime();
	if (thePrefs.GetLogNatTraversalEvents()) {
		DebugLog(_T("[NatTraversal] SendFileRequest dispatched to %s via %s (reqfile=%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (socket && socket->HaveUtpLayer()) ? _T("uTP") : _T("TCP"), m_reqfile ? (LPCTSTR)EscPercent(m_reqfile->GetFileName()) : _T("<null>"));
	}
}

void CUpDownClient::SendStartupLoadReq()
{
	if (socket == NULL || m_reqfile == NULL) {
		ASSERT(0);
		return;
	}
	m_fQueueRankPending = 1;
	m_fUnaskQueueRankRecv = 0;
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_StartupLoadReq", this);
	if (thePrefs.GetLogNatTraversalEvents()) {
		DebugLog(_T("[NatTraversal] Sending OP_STARTUPLOADREQ to %s (file=%s)"), (LPCTSTR)EscPercent(DbgGetClientInfo()), m_reqfile ? (LPCTSTR)EscPercent(m_reqfile->GetFileName()) : _T("<null>"));
	}
	CSafeMemFile dataStartupLoadReq(16);
	dataStartupLoadReq.WriteHash16(m_reqfile->GetFileHash());
	Packet *packet = new Packet(dataStartupLoadReq);
	packet->opcode = OP_STARTUPLOADREQ;
	theStats.AddUpDataOverheadFileRequest(packet->size);
	SetDownloadState(DS_ONQUEUE);
	SendPacket(packet);
}

void CUpDownClient::ProcessFileInfo(CSafeMemFile &data, CPartFile *file)
{
	LPCTSTR p;
	if (file == NULL)
		p = _T(" (ProcessFileInfo; file==NULL)");
	else if (m_reqfile == NULL)
		p = _T(" (ProcessFileInfo; reqfile==NULL)");
	else if (file != m_reqfile)
		p = _T(" (ProcessFileInfo; reqfile!=file)");
	else
		p = NULL;
	if (p)
		throw GetResString(_T("ERR_WRONGFILEID")) + p;

	m_strClientFilename = data.ReadString(GetUnicodeSupport() != UTF8strNone);
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		Debug(_T("  Filename=\"%s\"\n"), (LPCTSTR)m_strClientFilename);
	// 26-Jul-2003: removed requesting the file status for files <= PARTSIZE for better compatibility with
	// ed2k protocol (eDonkeyHybrid). If the remote client answers the OP_REQUESTFILENAME with OP_REQFILENAMEANSWER
	// the file is shared by the remote client. If the file is shared, we know also that the file
	// is complete and don't need to request the file status.
	if (m_reqfile->GetPartCount() == 1) {
		delete[] m_abyPartStatus;
		m_abyPartStatus = NULL;
		delete[] m_abyIncPartStatus;
		m_abyIncPartStatus = NULL;
		m_nIncPartCount = 0;
		m_nPartCount = m_reqfile->GetPartCount();
		m_abyPartStatus = new uint8[m_nPartCount];
		memset(m_abyPartStatus, 1, m_nPartCount);
		m_bCompleteSource = true;

		if (thePrefs.GetDebugClientTCPLevel() > 0) {
			int iNeeded = 0;
			char *psz = new char[m_nPartCount + 1];
			for (UINT i = 0; i < m_nPartCount; ++i) {
				iNeeded += static_cast<int>(!m_reqfile->IsComplete(i));
				psz[i] = m_abyPartStatus[i] ? '#' : '.';
			}
			psz[m_nPartCount] = '\0';
			Debug(_T("  Parts=%u  %hs  Needed=%u\n"), m_nPartCount, psz, iNeeded);
			delete[] psz;
		}
		UpdateDisplayedInfo();
		m_reqfile->UpdateAvailablePartsCount();
		// even if the file is <= PARTSIZE, we _may_ need the hashset for that file (if the file size == PARTSIZE)
		if (m_reqfile->m_bMD4HashsetNeeded || (m_reqfile->IsAICHPartHashSetNeeded() && SupportsFileIdentifiers()
			&& GetReqFileAICHHash() != NULL && *GetReqFileAICHHash() == m_reqfile->GetFileIdentifier().GetAICHHash()))
		{
			SendHashSetRequest();
			} else
				SendStartupLoadReq();
			if (thePrefs.GetLogNatTraversalEvents()) {
				DebugLog(_T("[NatTraversal] ProcessFileInfo: parts=%u complete=1 -> sending STARTUPLOADREQ to %s for %s"), m_nPartCount, (LPCTSTR)EscPercent(DbgGetClientInfo()), (LPCTSTR)EscPercent(m_reqfile->GetFileName()));
			}
			m_reqfile->UpdatePartsInfo();
		}
}

void CUpDownClient::ProcessFileStatus(bool bUdpPacket, CSafeMemFile &data, CPartFile *file)
{
	if (!m_reqfile || file != m_reqfile) {
		CString str;
		str.Format(_T("%s (ProcessFileStatus; reqfile==%s)")
			, (LPCTSTR)GetResString(_T("ERR_WRONGFILEID"))
			, m_reqfile ? _T("==NULL") : _T("!=file"));
		throw str;
	}

	if (file->GetStatus() == PS_COMPLETE || file->GetStatus() == PS_COMPLETING)
		return;

	delete[] m_abyPartStatus;
	m_abyPartStatus = NULL;

	uint16 nED2KPartCount = data.ReadUInt16();
	bool bPartsNeeded = m_bCompleteSource = !nED2KPartCount;
	int iNeeded = 0;
	if (bPartsNeeded) {
		m_nPartCount = m_reqfile->GetPartCount();
		if (!m_nPartCount)
			return;
		m_abyPartStatus = new uint8[m_nPartCount];
		memset(m_abyPartStatus, 1, m_nPartCount);
		if ((bUdpPacket ? thePrefs.GetDebugClientUDPLevel() : thePrefs.GetDebugClientTCPLevel()) > 0)
			for (UINT i = 0; i < m_nPartCount; ++i)
				iNeeded += static_cast<int>(!m_reqfile->IsComplete(i));
	} else {
		if (m_reqfile->GetED2KPartCount() != nED2KPartCount) {
			if (thePrefs.GetVerbose()) {
				DebugLogWarning(_T("FileName: \"%s\""), (LPCTSTR)EscPercent(m_strClientFilename));
				DebugLogWarning(_T("FileStatus: %s"), (LPCTSTR)EscPercent(DbgGetFileStatus(nED2KPartCount, data)));
			}
			CString strError;
			strError.Format(_T("ProcessFileStatus - wrong part number recv=%u  expected=%u  %s"), nED2KPartCount, m_reqfile->GetED2KPartCount(), (LPCTSTR)DbgGetFileInfo(m_reqfile->GetFileHash()));
			m_nPartCount = 0;
			throw strError;
		}

		m_nPartCount = m_reqfile->GetPartCount();
		m_abyPartStatus = new uint8[m_nPartCount];
		for (UINT done = 0; done < m_nPartCount;) {
			uint8 toread = data.ReadUInt8();
			for (UINT i = 0; i < 8 && done < m_nPartCount; ++i) {
				m_abyPartStatus[done] = (toread >> i) & 1;
				iNeeded += static_cast<int>(m_abyPartStatus[done] && !m_reqfile->IsComplete(done));
				++done;
			}
		}
		bPartsNeeded = (iNeeded > 0);
	}

	// NOTE: This function is invoked for TCP and UDP sockets!
	if ((bUdpPacket ? thePrefs.GetDebugClientUDPLevel() : thePrefs.GetDebugClientTCPLevel()) > 0) {
		TCHAR *psz = new TCHAR[m_nPartCount + 1];
		for (UINT i = 0; i < m_nPartCount; ++i)
			psz[i] = m_abyPartStatus[i] ? _T('#') : _T('.');
		psz[m_nPartCount] = _T('\0');
		Debug(_T("  Parts=%hu  %s  Needed=%i\n"), m_nPartCount, psz, iNeeded);
		delete[] psz;
	}

	UpdateDisplayedInfo(bUdpPacket);
	m_reqfile->UpdateAvailablePartsCount();

	if (bUdpPacket) {
		if (!bPartsNeeded && thePrefs.GetLogNatTraversalEvents())
			AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][Cause] ProcessFileStatus: source reported no needed parts via UDP, %s, file=\"%s\", parts=%u"), (LPCTSTR)EscPercent(DbgGetClientInfo()), m_reqfile ? (LPCTSTR)EscPercent(m_reqfile->GetFileName()) : _T(""), m_nPartCount);
		SetDownloadState(bPartsNeeded ? DS_ONQUEUE : DS_NONEEDEDPARTS);
	} else {
		if (!bPartsNeeded) {
			if (thePrefs.GetLogNatTraversalEvents())
				AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][Cause] ProcessFileStatus: source reported no needed parts via TCP, %s, file=\"%s\", parts=%u"), (LPCTSTR)EscPercent(DbgGetClientInfo()), m_reqfile ? (LPCTSTR)EscPercent(m_reqfile->GetFileName()) : _T(""), m_nPartCount);
			SetDownloadState(DS_NONEEDEDPARTS);
			SwapToAnotherFile(_T("A4AF for NNP file. CUpDownClient::ProcessFileStatus() TCP"), true, false, false, NULL, true, true);
		} else if (m_reqfile->m_bMD4HashsetNeeded || (m_reqfile->IsAICHPartHashSetNeeded() && SupportsFileIdentifiers()
			&& GetReqFileAICHHash() != NULL && *GetReqFileAICHHash() == m_reqfile->GetFileIdentifier().GetAICHHash())) //If we are using the eMule file request packets, this is taken care of in the Multipacket!
		{
			SendHashSetRequest();
			} else
				SendStartupLoadReq();
		}
		m_reqfile->UpdatePartsInfo();
}

bool CUpDownClient::AddRequestForAnotherFile(CPartFile *file)
{
	if (m_OtherNoNeeded_list.Find(file) || m_OtherRequests_list.Find(file))
		return false;

	m_OtherRequests_list.AddTail(file);
	file->A4AFsrclist.AddTail(this); // [enkeyDEV(Ottavio84) -A4AF-]
	return true;
}

void CUpDownClient::ClearPendingBlockRequest(const Pending_Block_Struct *pending)
{
	if (m_reqfile)
		m_reqfile->RemoveBlockFromList(pending->block->StartOffset, pending->block->EndOffset);

	delete pending->block;
	// Not always allocated
	if (pending->zStream) {
		inflateEnd(pending->zStream);
		delete pending->zStream;
	}
	delete pending;
}

void CUpDownClient::ClearDownloadBlockRequests()
{
	while (!m_PendingBlocks_list.IsEmpty())
		ClearPendingBlockRequest(m_PendingBlocks_list.RemoveHead());
}

// Fast cleanup and burst backoff when receiving blocks in an invalid state
void CUpDownClient::OnUnexpectedBlockWhileNotDownloading(uint32 /*uSize*/)
{
	// 1) Deterministic cleanup to avoid buffer/IO leaks
	if (!m_PendingBlocks_list.IsEmpty())
		ClearDownloadBlockRequests();

	// 2) Burst backoff (collapse floods)
	DWORD now = ::GetTickCount();
	if (now - m_dwLastInvalidBlockStateTick > SEC2MS(10))
		m_uInvalidBlockStateBurst = 0;
	m_dwLastInvalidBlockStateTick = now;
	if (m_uInvalidBlockStateBurst < 0x7FFFFFFF)
		++m_uInvalidBlockStateBurst;

	// 3) Politely cancel after small burst to stop repeated sends
	if (m_uInvalidBlockStateBurst >= 3 && !m_fSentCancelTransfer)
		SendCancelTransfer();
}

void CUpDownClient::OnUnexpectedBlockWhileDownloading(uint64 /*nStartPos*/, uint64 /*nEndPos*/, uint32 /*uSize*/)
{
	// Burst window to collapse floods (10s window).
	DWORD now = ::GetTickCount();
	if (now - m_dwLastUnexpectedBlockTick > SEC2MS(10))
		m_uUnexpectedBlockBurst = 0;
	m_dwLastUnexpectedBlockTick = now;
	if (m_uUnexpectedBlockBurst < 0x7FFFFFFF)
		++m_uUnexpectedBlockBurst;

	// After small burst, politely cancel to stop sender and resync our requests.
	if (m_uUnexpectedBlockBurst >= 3) {
		if (!m_fSentCancelTransfer)
			SendCancelTransfer();
		if (GetDownloadState() == DS_DOWNLOADING && m_PendingBlocks_list.IsEmpty())
			SendBlockRequests();
	}
}

static LPCTSTR NatTraversalDbgDownloadStateName(EDownloadState eState)
{
	switch (eState) {
		case DS_DOWNLOADING: return _T("Downloading");
		case DS_ONQUEUE: return _T("OnQueue");
		case DS_CONNECTED: return _T("Connected");
		case DS_CONNECTING: return _T("Connecting");
		case DS_WAITCALLBACK: return _T("WaitCallback");
		case DS_WAITCALLBACKKAD: return _T("WaitCallbackKad");
		case DS_REQHASHSET: return _T("ReqHashSet");
		case DS_NONEEDEDPARTS: return _T("NoNeededParts");
		case DS_TOOMANYCONNS: return _T("TooManyConns");
		case DS_TOOMANYCONNSKAD: return _T("TooManyConnsKad");
		case DS_LOWTOLOWIP: return _T("LowToLowIp");
		case DS_BANNED: return _T("Banned");
		case DS_ERROR: return _T("Error");
		case DS_NONE: return _T("None");
		case DS_REMOTEQUEUEFULL: return _T("RemoteQueueFull");
		default: return _T("Unknown");
	}
}

void CUpDownClient::SetDownloadState(EDownloadState nNewState, LPCTSTR pszReason)
{
	if (m_eDownloadState != nNewState) {
		const EDownloadState eOldState = m_eDownloadState;
		if (thePrefs.GetLogNatTraversalEvents()) {
			switch (nNewState) {
				case DS_ONQUEUE:
				case DS_NONEEDEDPARTS:
				case DS_WAITCALLBACK:
				case DS_WAITCALLBACKKAD:
				case DS_CONNECTING:
					AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][DLState] %s: %s -> %s, reason=\"%s\", file=\"%s\", pending=%u, qr=%u, qfull=%d"),
						(LPCTSTR)DbgGetClientInfo(),
						NatTraversalDbgDownloadStateName(eOldState),
						NatTraversalDbgDownloadStateName(nNewState),
						pszReason != NULL ? pszReason : _T(""),
						m_reqfile != NULL ? (LPCTSTR)m_reqfile->GetFileName() : _T(""),
						static_cast<UINT>(m_PendingBlocks_list.GetCount()),
						static_cast<UINT>(GetRemoteQueueRank()),
						IsRemoteQueueFull() ? 1 : 0);
					break;
				default:
					break;
			}
		}

		switch (nNewState) {
		case DS_CONNECTING:
			SetLastTriedToConnectTime();
			break;
		case DS_TOOMANYCONNSKAD:
			//This client had already been set to DS_CONNECTING.
			//So we reset this time so it isn't stuck at TOOMANYCONNS for 20 mins.
			//Default waiting time is 20 min to reconnect on other parts of the code.We want to retry after 60 seconds, so we reset this time by -20min and add 60 sec.
			m_dwLastTriedToConnect = ::GetTickCount() - MIN2MS(11) + SEC2MS(60); // MIN2MS(11) = MIN_REQUESTTIME + 60000
			break;
		case DS_WAITCALLBACKKAD:
		case DS_WAITCALLBACK:
			break;
		case DS_NONE:
		case DS_ERROR:
			if (m_eConnectingState != CCS_NONE) {
				m_eConnectingState = CCS_NONE;
				theApp.clientlist->RemoveConnectingClient(this);
			}
			break;
		case DS_NONEEDEDPARTS:
			// Since TCP asks never set re-ask time if the result is DS_NONEEDEDPARTS
			// If we set this, we will not re-ask for that file until some time has passed.
			SetLastAskedTime();
		}

		if (m_reqfile) {
			if (nNewState == DS_DOWNLOADING) {
				if (thePrefs.GetLogUlDlEvents())
					AddDebugLogLine(DLP_VERYLOW, false, _T("Download session started. User: %s in SetDownloadState(). New State: %i"), (LPCTSTR)EscPercent(DbgGetClientInfo()), nNewState);

				m_reqfile->AddDownloadingSource(this);
			} else if (m_eDownloadState == DS_DOWNLOADING)
				m_reqfile->RemoveDownloadingSource(this);
		}

		if (nNewState == DS_DOWNLOADING && socket)
			socket->SetTimeOut(CONNECTION_TIMEOUT * 4);

		if (m_eDownloadState == DS_DOWNLOADING) {
			if (socket)
				socket->SetTimeOut(CONNECTION_TIMEOUT);

			if (thePrefs.GetLogUlDlEvents()) {
				if (nNewState == DS_NONEEDEDPARTS)
					pszReason = _T("NNP. You don't need any parts from this client.");

				AddDebugLogLine(DLP_VERYLOW, false
					, _T("Download session ended: %s User: %s in SetDownloadState(). New State: %i, Length: %s, Payload: %s, Transferred: %s, Req blocks not yet completed: %i.")
					, (LPCTSTR)EscPercent(pszReason)
					, (LPCTSTR)EscPercent(DbgGetClientInfo())
					, nNewState
					, (LPCTSTR)CastSecondsToHM(GetDownTimeDifference(false) / SEC2MS(1))
					, (LPCTSTR)CastItoXBytes(GetSessionPayloadDown())
					, (LPCTSTR)CastItoXBytes(GetSessionDown())
					, m_PendingBlocks_list.GetCount());
			}

			if (thePrefs.IsDetectUploadFaker()) { // IsHarderPunishment isn't necessary here since the cost is low
				//Xman filter clients with failed downloads
				if (GetSessionDown() < 12288) {  //12KB*1024=12288 byte 
					m_faileddownloads++;
					if (m_faileddownloads > 3)
						theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_UPLOAD_FAKER")), PR_UPLOADFAKER);
				}
				else
					m_faileddownloads = 0;
			}

			ResetSessionDown();

			if (m_bTransferredDownMini && nNewState != DS_ERROR)
				thePrefs.Add2DownSuccessfulSessions(); // Increment our counters for successful sessions (Cumulative AND Session)
			else
				thePrefs.Add2DownFailedSessions(); // Increment our counters failed sessions (Cumulative AND Session)
			thePrefs.Add2DownSAvgTime(GetDownTimeDifference() / SEC2MS(1));

			m_eDownloadState = nNewState;

			ClearDownloadBlockRequests();

			m_nDownDatarate = 0;
			m_AverageDDR_list.RemoveAll();
			m_nSumForAvgDownDataRate = 0;

			if (nNewState == DS_NONE) {
				delete[] m_abyPartStatus;
				m_abyPartStatus = NULL;
				m_nPartCount = 0;
				delete[] m_abyIncPartStatus;
				m_abyIncPartStatus = NULL;
				m_nIncPartCount = 0;
			}
			if (socket && nNewState != DS_ERROR)
				socket->DisableDownloadLimit();
		} else
			m_eDownloadState = nNewState;

		if (GetDownloadState() == DS_DOWNLOADING) {
			m_uNatStallRecoverBurst = 0;
			m_dwLastNatStallRecover = 0;
			if (IsEmuleClient())
				SetRemoteQueueFull(false);
			SetRemoteQueueRank(0);
			SetAskedCountDown(0);
		}
		UpdateDisplayedInfo(true);
	}
}

void CUpDownClient::ProcessHashSet(const uchar *packet, uint32 size, bool bFileIdentifiers)
{
	// NAT-T/uTP connections may drop and reconnect, clearing request flags while hashset response packets are still in flight.
	// Instead of throwing exceptions (which cause crashes), gracefully ignore unsolicited hashset packets.
	if (!m_fHashsetRequestingMD4) {
		if (!bFileIdentifiers) {
			DebugLogWarning(_T("Received unrequested MD4 hashset from %s (possible reconnection), ignoring"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			return;
		}
		if (!m_fHashsetRequestingAICH) {
			DebugLogWarning(_T("Received unrequested AICH hashset2 from %s (possible NAT-T reconnection), ignoring"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			return;
		}
	}

	CSafeMemFile data(packet, size);
	if (bFileIdentifiers) {
		CFileIdentifierSA fileIdent;
		if (!fileIdent.ReadIdentifier(data))
			throwCStr(_T("Invalid FileIdentifier"));
		if (m_reqfile == NULL || !m_reqfile->GetFileIdentifier().CompareRelaxed(fileIdent)) {
			CheckFailedFileIdReqs(packet);
			throw GetResString(_T("ERR_WRONGFILEID")) + _T(" (ProcessHashSet2)");
		}
		bool bMD4 = m_fHashsetRequestingMD4 != 0;
		bool bAICH = m_fHashsetRequestingAICH != 0;
		if (!m_reqfile->GetFileIdentifier().ReadHashSetsFromPacket(data, bMD4, bAICH)) {
			if (m_fHashsetRequestingMD4)
				m_reqfile->m_bMD4HashsetNeeded = true;
			if (m_fHashsetRequestingAICH)
				m_reqfile->SetAICHHashSetNeeded(true);
			m_fHashsetRequestingMD4 = 0;
			m_fHashsetRequestingAICH = 0;
			throw GetResString(_T("ERR_BADHASHSET"));
		}
		if (m_fHashsetRequestingMD4)
			if (bMD4)
				DebugLog(_T("Received valid MD4 Hashset (FileIdentifiers) from %s, file: %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (LPCTSTR)EscPercent(m_reqfile->GetFileName()));
			else {
				DebugLogWarning(_T("Client was unable to deliver requested MD4 hashset (shouldn't happen) - %s, file: %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (LPCTSTR)EscPercent(m_reqfile->GetFileName()));
				m_reqfile->m_bMD4HashsetNeeded = true;
			}

		if (m_fHashsetRequestingAICH)
			if (bAICH)
				DebugLog(_T("Received valid AICH Part Hashset from %s, file: %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (LPCTSTR)EscPercent(m_reqfile->GetFileName()));
			else {
				DebugLogWarning(_T("Client was unable to deliver requested AICH part hashset, asking other clients - %s, file: %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (LPCTSTR)EscPercent(m_reqfile->GetFileName()));
				m_reqfile->SetAICHHashSetNeeded(true);
			}
	} else {
		if (!m_reqfile || !md4equ(packet, m_reqfile->GetFileHash())) {
			CheckFailedFileIdReqs(packet);
			throw GetResString(_T("ERR_WRONGFILEID")) + _T(" (ProcessHashSet)");
		}
		if (!m_reqfile->GetFileIdentifier().LoadMD4HashsetFromFile(data, true)) {
			m_reqfile->m_bMD4HashsetNeeded = true;
			throw GetResString(_T("ERR_BADHASHSET"));
		}
	}
	m_fHashsetRequestingMD4 = 0;
	m_fHashsetRequestingAICH = 0;
	SendStartupLoadReq();
}

void CUpDownClient::CreateBlockRequests(int blockCount)
{
	ASSERT(blockCount > 0 && blockCount <= 9);
	//prevent uncontrolled growth
	if ((int)m_PendingBlocks_list.GetCount() > 2 * blockCount)
		return;

	//count out unprocessed blocks
	for (POSITION pos = m_PendingBlocks_list.GetHeadPosition(); pos != NULL;)
		blockCount -= static_cast<int>(m_PendingBlocks_list.GetNext(pos)->fQueued == 0);

	if (blockCount <= 0)
		return;

	Requested_Block_Struct **toadd = new Requested_Block_Struct*[blockCount];
	if (m_reqfile->GetNextRequestedBlock(this, toadd, blockCount))
		for (int i = 0; i < blockCount; ++i)
			m_PendingBlocks_list.AddTail(new Pending_Block_Struct{ toadd[i] });

	delete[] toadd;
}

void CUpDownClient::SendBlockRequests()
{
	m_dwLastBlockReceived = ::GetTickCount();
	if (!m_reqfile)
		return;

	// Fast uploader/slow downloader combination requires special treatment.
	//
	// For example, getting 360 KB (2 blocks) at 9 KB/s rate takes 40 seconds.
	// An uploader with 100 Mbit/s connection delivers the data in a fraction of a second
	// while downloader would be receiving the data long after.
	// Should it be longer than 40 s, uploader will disconnect on time out.

	// Restrict the number of requested blocks when we are on a slow/standby/trickle slot or
	// download is limited on our side.
	int blockCount; // max pending block requests
	if (IsEmuleClient() && m_byCompatibleClient == 0 && GetDownloadDatarate() < 9 * 1024)
		blockCount = (GetDownloadDatarate() < 4 * 1024) ? 1 : 2;
	else if (GetDownloadDatarate() > 75 * 1024)
		blockCount = (GetDownloadDatarate() > 150 * 1024) ? 9 : 6;
	else
		blockCount = 3;

	CreateBlockRequests(blockCount);
	if (m_PendingBlocks_list.IsEmpty()) {
		if (thePrefs.GetLogNatTraversalEvents())
			AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][Cause] SendBlockRequests: no pending blocks were generated, %s, file=\"%s\""), (LPCTSTR)EscPercent(DbgGetClientInfo()), m_reqfile ? (LPCTSTR)EscPercent(m_reqfile->GetFileName()) : _T(""));
		SendCancelTransfer();
		SetDownloadState(DS_NONEEDEDPARTS);
		SwapToAnotherFile(_T("A4AF for NNP file. CUpDownClient::SendBlockRequests()"), true, false, false, NULL, true, true);
		return;
	}

	CTypedPtrList<CPtrList, Pending_Block_Struct*> listToRequest;
	bool bI64Offsets = false;
	for (POSITION pos = m_PendingBlocks_list.GetHeadPosition(); pos != NULL;) {
		Pending_Block_Struct *pending = m_PendingBlocks_list.GetNext(pos);
		if (pending->fQueued)
			continue;
		ASSERT(pending->block->StartOffset <= pending->block->EndOffset);
		if (pending->block->EndOffset >= _UI32_MAX) {
			if (!SupportsLargeFiles()) {
				ASSERT(0);
				SendCancelTransfer();
				SetDownloadState(DS_ERROR);
				return;
			}
			bI64Offsets = true;
		}
		listToRequest.AddTail(pending);
		if (listToRequest.GetCount() >= 3)
			break;
	}

	if (!IsEmuleClient() && listToRequest.GetCount() < 3) {
		for (POSITION pos = m_PendingBlocks_list.GetHeadPosition(); pos != NULL;) {
			Pending_Block_Struct *pending = m_PendingBlocks_list.GetNext(pos);
			if (!pending->fQueued)
				continue;
			ASSERT(pending->block->StartOffset <= pending->block->EndOffset);
			if (pending->block->EndOffset >= _UI32_MAX) {
				if (!SupportsLargeFiles()) {
					ASSERT(0);
					SendCancelTransfer();
					SetDownloadState(DS_ERROR);
					return;
				}
				bI64Offsets = true;
			}
			listToRequest.AddTail(pending);
			if (listToRequest.GetCount() >= 3)
				break;
		}
	} else if (listToRequest.IsEmpty()) {
		// do not re-request blocks, at least eMule clients need not expect this to work properly
		// so it's just overhead (and it's not protocol standard, but we used to do so since forever)
		// by adding a range of min to max pending blocks
		// this means we do not send a request after every received packet any more
		return;
	}

	//Use this array to write all offsets in one pass
	uint64 aOffs[3 * 2]; //0..2 - start points, 3..5 - end points

	POSITION pos = listToRequest.GetHeadPosition();
	for (int i = 0; i < 3; ++i)
		if (pos) {
			Pending_Block_Struct *pending = listToRequest.GetNext(pos);
			ASSERT(pending->block->StartOffset <= pending->block->EndOffset);
			pending->fZStreamError = 0;
			pending->fRecovered = 0;
			pending->fQueued = 1;
			aOffs[i] = pending->block->StartOffset;
			aOffs[i + 3] = pending->block->EndOffset + 1;
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				Debug(_T("  Block request %d: %s, Complete=%s, PureGap=%s, AlreadyReq=%s\n")
					, i
					, (LPCTSTR)DbgGetBlockInfo(pending->block)
					, m_reqfile->IsComplete(aOffs[i], aOffs[i + 3] - 1) ? _T("Yes(NOTE:)") : _T("No")
					, m_reqfile->IsPureGap(aOffs[i], aOffs[i + 3] - 1) ? _T("Yes") : _T("No(NOTE:)")
					, m_reqfile->IsAlreadyRequested(aOffs[i], aOffs[i + 3] - 1) ? _T("Yes") : _T("No(NOTE:)"));
		} else {
			aOffs[i] = aOffs[i + 3] = 0;
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				Debug(_T("  Block request %d: <empty>\n"), i);
		}

	Packet *packet;
	if (bI64Offsets)
		packet = new Packet(OP_REQUESTPARTS_I64, 16 + (3 * 8) + (3 * 8), OP_EMULEPROT); //size 64
	else
		packet = new Packet(OP_REQUESTPARTS, 16 + (3 * 4) + (3 * 4)); //size 40
	CSafeMemFile data((BYTE*)packet->pBuffer, packet->size);
	data.WriteHash16(m_reqfile->GetFileHash());
	for (int i = 0; i < 3 * 2; ++i)
		if (bI64Offsets)
			data.WriteUInt64(aOffs[i]);
		else
			data.WriteUInt32((uint32)aOffs[i]);

	theStats.AddUpDataOverheadFileRequest(packet->size);
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_RequestParts", this, m_reqfile->GetFileHash());
	
	if (thePrefs.GetLogNatTraversalEvents())
		AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] Sending OP_REQUESTPARTS to %s, File=%s"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (LPCTSTR)EscPercent(m_reqfile->GetFileName()));

	SendPacket(packet, true);
	// we want this packet to get out ASAP, especially for high-speed downloads
	// so wake up the throttler if it was idle and fell asleep
	theApp.uploadBandwidthThrottler->NewUploadDataAvailable();
}

void CUpDownClient::CheckNatTraversalStall()
{
	static const DWORD kNatStallSilenceMs = SEC2MS(20);
	static const DWORD kNatStallRetryIntervalStep1Ms = SEC2MS(20);
	static const DWORD kNatStallRetryIntervalStep2Ms = SEC2MS(30);
	static const DWORD kNatStallRetryIntervalStep3Ms = SEC2MS(45);
	static const UINT kNatStallQueuedResetLimit = 3;
	static const UINT kNatStallRecoverBurstMax = 2;

	// 1. Is Downloading ?
	if (GetDownloadState() != DS_DOWNLOADING) return;

	// 2. Is LowID and NAT-T/uTP client ?
	// Access socket only if safe.
	if (!HasLowID() || !socket || !socket->HaveUtpLayer()) return;

	// 3. Last block time logic
	// If data flow stops, we assume NAT tunnel might be closing or packet loss occurred.
	// We need to re-trigger the request to wake up the connection.
	DWORD dwNow = ::GetTickCount();
	DWORD dwTimeSinceLastBlock = dwNow - m_dwLastBlockReceived;
	
	if (dwTimeSinceLastBlock <= kNatStallSilenceMs)
		return;

	DWORD dwRetryInterval = kNatStallRetryIntervalStep1Ms;
	if (m_uNatStallRecoverBurst >= 2)
		dwRetryInterval = kNatStallRetryIntervalStep3Ms;
	else if (m_uNatStallRecoverBurst == 1)
		dwRetryInterval = kNatStallRetryIntervalStep2Ms;

	if (dwNow - m_dwLastNatStallRecover <= dwRetryInterval)
		return;

	if (thePrefs.GetLogNatTraversalEvents())
		AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] Stall detected (%ums ago, burst=%u, retry=%ums), triggering re-ask for %s"), dwTimeSinceLastBlock, m_uNatStallRecoverBurst, dwRetryInterval, (LPCTSTR)EscPercent(DbgGetClientInfo()));

	m_dwLastNatStallRecover = dwNow;
	if (m_uNatStallRecoverBurst < kNatStallRecoverBurstMax)
		++m_uNatStallRecoverBurst;

	bool bHasUnqueuedPending = false;
	for (POSITION posScan = m_PendingBlocks_list.GetHeadPosition(); posScan != NULL;) {
		const Pending_Block_Struct *pPending = m_PendingBlocks_list.GetNext(posScan);
		if (pPending->fQueued == 0) {
			bHasUnqueuedPending = true;
			break;
		}
	}

	UINT uResetQueuedBlocks = 0;
	if (!bHasUnqueuedPending) {
		for (POSITION pos = m_PendingBlocks_list.GetHeadPosition(); pos != NULL && uResetQueuedBlocks < kNatStallQueuedResetLimit;) {
			Pending_Block_Struct *pPending = m_PendingBlocks_list.GetNext(pos);
			if (pPending->fQueued != 0) {
				pPending->fQueued = 0;
				++uResetQueuedBlocks;
			}
		}
	}

	if (thePrefs.GetLogNatTraversalEvents()) {
		if (bHasUnqueuedPending)
			AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] Stall recovery: queued reset skipped (already has unqueued pending block) for %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		else
			AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] Stall recovery: reset %u queued pending block(s) before re-ask for %s"), uResetQueuedBlocks, (LPCTSTR)EscPercent(DbgGetClientInfo()));
	}

	// Re-send block requests. This sends an OP_REQUESTPARTS packet.
	// This acts as a keep-alive/hole-punch packet and tells the uploader we are still waiting.
	SendBlockRequests();
}

/* Barry - Originally, this method wrote to disk only when a full 180k block
		   had been received from a client, and asked for data only by
		   180k blocks.

		   It means that on average 90k was lost for every connection
		   to a client data source. That is a lot of wasted data.

		   To reduce data loss, packets are now written to buffers
		   and flushed to disk regularly regardless of size downloaded.
		   This includes compressed packets.

		   Data is also requested only where the gaps are, not in 180k blocks.
		   The requests will still not exceed 180k, but may be smaller to
		   fill a gap.
*/
void CUpDownClient::ProcessBlockPacket(const uchar *packet, uint32 size, bool packed, bool bI64Offsets)
{
	if (thePrefs.GetLogNatTraversalEvents())
		AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] ProcessBlockPacket: Entry, size=%u, packed=%d, client=%s"), size, packed, (LPCTSTR)EscPercent(DbgGetClientInfo()));

	if (!bI64Offsets) {
		uint32 nDbgStartPos = *(uint32*)&packet[16];
		if (thePrefs.GetDebugClientTCPLevel() > 1) {
			if (packed)
				Debug(_T("  Start=%u  BlockSize=%u  Size=%u  %s\n"), nDbgStartPos, *(uint32*)&packet[16 + 4], size - 24, (LPCTSTR)DbgGetFileInfo(packet));
			else
				Debug(_T("  Start=%u  End=%u  Size=%u  %s\n"), nDbgStartPos, *(uint32*)&packet[16 + 4], *(uint32*)&packet[16 + 4] - nDbgStartPos, (LPCTSTR)DbgGetFileInfo(packet));
		}
	}

	// Ignore if no data required
	if (GetDownloadState() != DS_DOWNLOADING && GetDownloadState() != DS_NONEEDEDPARTS) {
		AddDebugLogLine(DLP_LOW, _T("%s - Invalid download state\n"), __FUNCTION__);
		OnUnexpectedBlockWhileNotDownloading(size); // fail-fast: cleanup + backoff
		return;
	}

	// Update stats
	m_dwLastBlockReceived = ::GetTickCount();
	if (m_uNatStallRecoverBurst != 0)
		m_uNatStallRecoverBurst = 0;

	// Read data from packet
	CSafeMemFile data(packet, size);
	uchar fileID[MDX_DIGEST_SIZE];
	data.ReadHash16(fileID);
	int nHeaderSize = MDX_DIGEST_SIZE;

	// Check that this data is for the correct file
	if (!m_reqfile || !md4equ(packet, m_reqfile->GetFileHash())) {
		ClearDownloadBlockRequests(); // release pending blocks on protocol mismatch
		throw GetResString(_T("ERR_WRONGFILEID")) + _T(" (ProcessBlockPacket)");
	}

	// Find the start & end positions, and size of this chunk of data
	uint64 nStartPos, nEndPos;

	if (bI64Offsets) {
		nStartPos = data.ReadUInt64();
		nHeaderSize += 8;
	} else {
		nStartPos = data.ReadUInt32();
		nHeaderSize += 4;
	}
	if (packed) {
		data.Seek((LONGLONG)sizeof(uint32), CFile::current); //skip size
		nHeaderSize += 4;
		nEndPos = nStartPos + (size - nHeaderSize);
	} else if (bI64Offsets) {
		nEndPos = data.ReadUInt64();
		nHeaderSize += 8;
	} else {
		nEndPos = data.ReadUInt32();
		nHeaderSize += 4;
	}
	uint32 uTransferredFileDataSize = size - nHeaderSize;

	// Check that packet size matches the declared data size + header size (24)
	if (nEndPos <= nStartPos || size != ((nEndPos - nStartPos) + nHeaderSize)) {
		ClearDownloadBlockRequests(); // release any reserved blocks on bad data
		throw GetResString(_T("ERR_BADDATABLOCK")) + _T(" (ProcessBlockPacket)");
	}

	// Extended statistics information based on which client and remote port sent this data.
	// The new function adds the bytes to the grand total as well as the given client/port.
	// bFromPF is not relevant to downloaded data.  It is purely an uploads statistic.
	thePrefs.Add2SessionTransferData(GetClientSoft(), GetUserPort(), false, false, uTransferredFileDataSize, false);

	m_nDownDataRateMS += uTransferredFileDataSize;
	if (credits)
		credits->AddDownloaded(uTransferredFileDataSize, GetIP());

	// Move end back by one, should be inclusive
	--nEndPos;

	// Loop through to find the reserved block that this is within
	for (POSITION pos = m_PendingBlocks_list.GetHeadPosition(); pos != NULL;) {
		POSITION posLast = pos;
		Pending_Block_Struct *cur_block = m_PendingBlocks_list.GetNext(pos);
		if (cur_block->block->StartOffset > nStartPos || cur_block->block->EndOffset < nStartPos)
			continue;

		// Found the reserved block
		if (cur_block->fZStreamError) {
			if (thePrefs.GetVerbose())
				AddDebugLogLine(false, _T("PrcBlkPkt: Ignoring %u bytes of block starting at %I64u because of erroneous zstream state for file \"%s\" - %s"), uTransferredFileDataSize, nStartPos, (LPCTSTR)EscPercent(m_reqfile->GetFileName()), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			m_reqfile->RemoveBlockFromList(cur_block->block->StartOffset, cur_block->block->EndOffset);
			return;
		}

		// Remember this start pos, used to draw part downloading in list
		m_nLastBlockOffset = nStartPos;

		// Occasionally packets are duplicated, no point writing it twice
		// This will be 0 in these cases, or the length written otherwise
		uint32 lenWritten = 0;

		// Handle differently depending on whether packed or not
		if (!packed) {
			// security sanitize check
			if (nEndPos > cur_block->block->EndOffset) {
				DebugLogError(_T("Received Blockpacket exceeds requested boundaries (requested end: %I64u, Part %u, received end  %I64u, Part %u), file %s, client %s"), cur_block->block->EndOffset
					, (uint32)(cur_block->block->EndOffset / PARTSIZE), nEndPos, (uint32)(nEndPos / PARTSIZE), (LPCTSTR)m_reqfile->GetFileName(), (LPCTSTR)DbgGetClientInfo());
				m_reqfile->RemoveBlockFromList(cur_block->block->StartOffset, cur_block->block->EndOffset);
				return;
			}
			// Write to disk (will be buffered in part file class)
			lenWritten = m_reqfile->WriteToBuffer(uTransferredFileDataSize
					, &packet[nHeaderSize]
					, nStartPos
					, nEndPos
					, cur_block->block
					, this
					, true); //copy data to a new buffer
		} else { // Packed
			ASSERT((int)size > 0);
			// Create space to store unzipped data, the size is only an initial guess, will be resized in unzip() if not big enough
			// Don't get too big
			uint32 lenUnzipped = min(size * 2, EMBLOCKSIZE + 300);
			BYTE *unzipped = new BYTE[lenUnzipped];

			// Try to unzip the packet
			int result = unzip(cur_block, &packet[nHeaderSize], uTransferredFileDataSize, &unzipped, &lenUnzipped);
			// no block can be uncompressed to >2GB, 'lenUnzipped' is obviously erroneous.
			if (result == Z_OK && (int)lenUnzipped >= 0) {
				if (lenUnzipped > 0) { // Write any unzipped data to disk
					ASSERT((int)lenUnzipped > 0);

					// Use the current start and end positions for the uncompressed data
					nStartPos = cur_block->block->StartOffset + cur_block->totalUnzipped - lenUnzipped;
					nEndPos = cur_block->block->StartOffset + cur_block->totalUnzipped - 1;

					if (nStartPos > cur_block->block->EndOffset || nEndPos > cur_block->block->EndOffset) {
						DebugLogError(_T("PrcBlkPkt: ") + GetResString(_T("ERR_CORRUPTCOMPRPKG")), (LPCTSTR)EscPercent(m_reqfile->GetFileName()), 666);
						m_reqfile->RemoveBlockFromList(cur_block->block->StartOffset, cur_block->block->EndOffset);
						// There is no chance to recover from this error
					} else {
						// Write uncompressed data to file
						lenWritten = m_reqfile->WriteToBuffer(uTransferredFileDataSize
							, unzipped
							, nStartPos
							, nEndPos
							, cur_block->block
							, this
							, false); //use the given buffer, no copy
						if (lenWritten)
							unzipped = NULL; //do not delete the buffer
					}
				}
			} else {
				if (thePrefs.GetVerbose()) {
					CString strZipError;
					if (cur_block->zStream && cur_block->zStream->msg)
						strZipError.Format(_T(" - %hs"), cur_block->zStream->msg);
					if (result == Z_OK && (int)lenUnzipped < 0) {
						ASSERT(0);
						strZipError.AppendFormat(_T("; Z_OK,lenUnzipped=%u"), lenUnzipped);
					}
					DebugLogError(_T("PrcBlkPkt: ") + GetResString(_T("ERR_CORRUPTCOMPRPKG")) + strZipError, (LPCTSTR)EscPercent(m_reqfile->GetFileName()), result);
				}
				m_reqfile->RemoveBlockFromList(cur_block->block->StartOffset, cur_block->block->EndOffset);

				// If we had a zstream error, there is no chance that we could recover from it,
				// nor that we could use the current zstream (which is in error state) any longer.
				if (cur_block->zStream) {
					inflateEnd(cur_block->zStream);
					delete cur_block->zStream;
					cur_block->zStream = NULL;
				}

				// Although we can't further use the current zstream, there is no need to disconnect the sending
				// client because the next zstream (a series of 10K-blocks which build a 180K-block) could be
				// valid again. Just ignore all further blocks for the current zstream.
				cur_block->fZStreamError = 1;
				cur_block->totalUnzipped = 0;
			}
			delete[] unzipped;
		}

		// These checks only need to be done if any data was written
		// Also, asynchronous writing allows tricks such as disconnecting from client while
		// file data is being in buffers.
		// Hence additional checks.
		if (lenWritten > 0 && !m_PendingBlocks_list.IsEmpty() && cur_block->block) {
			m_nTransferredDown += uTransferredFileDataSize;
			m_nCurSessionPayloadDown += lenWritten;
			cur_block->block->transferred += lenWritten; //cur_block->block was invalid!
			SetTransferredDownMini();

			// If finished reserved block
			if (nEndPos == cur_block->block->EndOffset) {
				m_PendingBlocks_list.RemoveAt(posLast);
				ClearPendingBlockRequest(cur_block);

				// Request next block
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugSend("More block requests", this);
				SendBlockRequests();
			}
		}

		// Stop looping and exit
		return;
	}

	OnUnexpectedBlockWhileDownloading(nStartPos, nEndPos, uTransferredFileDataSize);
}

int CUpDownClient::unzip(Pending_Block_Struct *block, const BYTE *zipped, uint32 lenZipped, BYTE **unzipped, uint32 *lenUnzipped, int iRecursion)
{
#define TRACE_UNZIP	__noop
	TRACE_UNZIP("unzip: Zipd=%6u Unzd=%6u Rcrs=%d", lenZipped, *lenUnzipped, iRecursion);
	int err = Z_DATA_ERROR;
	try {
		// Save some typing
		z_stream *zS = block->zStream;

		// Is this the first time this block has been unzipped
		if (zS == NULL) {
			// Create stream
			block->zStream = new z_stream;
			zS = block->zStream;

			// Initialise stream values
			zS->zalloc = (alloc_func)NULL;
			zS->zfree = (free_func)NULL;
			zS->opaque = (voidpf)NULL;

			// Set output data streams, do this here to avoid overwriting on recursive calls
			zS->next_out = *unzipped;
			zS->avail_out = *lenUnzipped;

			// Initialise the z_stream
			err = inflateInit(zS);
			if (err != Z_OK) {
				TRACE_UNZIP("; Error: new stream failed: %d\n", err);
				return err;
			}

			ASSERT(block->totalUnzipped == 0);
		}

		// Use whatever input is provided
		zS->next_in = const_cast<Bytef*>(zipped);
		zS->avail_in = lenZipped;

		// Only set the output if not being called recursively
		if (iRecursion == 0) {
			zS->next_out = *unzipped;
			zS->avail_out = *lenUnzipped;
		}

		// Try to unzip the data
		TRACE_UNZIP("; inflate(ain=%6u tin=%6u aout=%6u tout=%6u)", zS->avail_in, zS->total_in, zS->avail_out, zS->total_out);
		err = inflate(zS, Z_SYNC_FLUSH);

		// Is zip finished reading all currently available input and writing all generated output
		if (err == Z_STREAM_END) {
			// Finish up
			err = inflateEnd(zS);
			if (err != Z_OK) {
				TRACE_UNZIP("; Error: end stream failed: %d\n", err);
				return err;
			}
			TRACE_UNZIP("; Z_STREAM_END\n");

			// Got a good result, set the size to the amount unzipped in this call (including all recursive calls)
			*lenUnzipped = zS->total_out - block->totalUnzipped;
			block->totalUnzipped = zS->total_out;
		} else if ((err == Z_OK) && (zS->avail_out == 0) && (zS->avail_in != 0)) {
			// Output array was not big enough, call recursively until there is enough space
			TRACE_UNZIP("; output array not big enough (ain=%u)\n", zS->avail_in);

			// What size should we try next
			*lenUnzipped *= 2;
			uint32 newLength = *lenUnzipped;
			if (newLength == 0)
				newLength = lenZipped * 2;

			// Copy any data that was successfully unzipped to new array
			BYTE *temp = new BYTE[newLength];
			ASSERT(zS->total_out - block->totalUnzipped <= newLength);
			memcpy(temp, *unzipped, zS->total_out - block->totalUnzipped);
			delete[] *unzipped;
			*unzipped = temp;
			*lenUnzipped = newLength;

			// Position stream output to correct place in new array
			zS->next_out = *unzipped + (zS->total_out - block->totalUnzipped);
			zS->avail_out = *lenUnzipped - (zS->total_out - block->totalUnzipped);

			// Try again
			err = unzip(block, zS->next_in, zS->avail_in, unzipped, lenUnzipped, iRecursion + 1);
		} else if ((err == Z_OK) && (zS->avail_in == 0)) {
			TRACE_UNZIP("; all input processed\n");
			// All available input has been processed, everything OK.
			// Set the size to the amount unzipped in this call (including all recursive calls)
			*lenUnzipped = zS->total_out - block->totalUnzipped;
			block->totalUnzipped = zS->total_out;
		} else {
			// Should not get here unless input data is corrupt
			if (thePrefs.GetVerbose()) {
				CString strZipError;
				if (zS->msg || err != Z_OK)
					strZipError.Format(_T(" %d: '%hs'"), err, zS->msg ? zS->msg : zError(err));
				TRACE_UNZIP("; Error: %s\n", strZipError);
				DebugLogError(_T("Unexpected zip error%s in file \"%s\""), (LPCTSTR)EscPercent(strZipError), m_reqfile ? (LPCTSTR)EscPercent(m_reqfile->GetFileName()) : EMPTY);
			}
		}

		if (err != Z_OK)
			*lenUnzipped = 0;
	} catch (...) {
		if (thePrefs.GetVerbose())
			DebugLogError(_T("Unknown exception in %hs: file \"%s\""), __FUNCTION__, m_reqfile ? (LPCTSTR)EscPercent(m_reqfile->GetFileName()) : EMPTY);
		err = Z_DATA_ERROR;
		ASSERT(0);
	}

	return err;
}

uint32 CUpDownClient::CalculateDownloadRate()
{
	// Patch By BadWolf - Accurate data rate Calculation
	const DWORD curTick = ::GetTickCount();
	m_AverageDDR_list.AddTail(TransferredData{ m_nDownDataRateMS, curTick });
	m_nSumForAvgDownDataRate += m_nDownDataRateMS;
	m_nDownDataRateMS = 0;

	while (m_AverageDDR_list.GetCount() > 500)
		m_nSumForAvgDownDataRate -= m_AverageDDR_list.RemoveHead().datalen;

	if (m_AverageDDR_list.GetCount() > 1 && curTick > m_AverageDDR_list.GetHead().timestamp)
		m_nDownDatarate = (UINT)(SEC2MS(m_nSumForAvgDownDataRate) / (curTick - m_AverageDDR_list.GetHead().timestamp));
	else
		m_nDownDatarate = 0;
	// END Patch By BadWolf

	UpdateDisplayedInfo();

	return m_nDownDatarate;
}

void CUpDownClient::CheckDownloadTimeout()
{
	DWORD now = ::GetTickCount();
	if (socket && socket->IsConnected() && socket->HaveUtpLayer() && GetDownloadState() == DS_DOWNLOADING && !m_PendingBlocks_list.IsEmpty()) {
		const DWORD kUtpReaskTimeout = DOWNLOADTIMEOUT / 2;
		if (now >= m_dwLastBlockReceived + kUtpReaskTimeout) {
			// Re-ask pending blocks after a stalled uTP receive window to avoid premature queue fallback.
			for (POSITION pos = m_PendingBlocks_list.GetHeadPosition(); pos != NULL;)
				m_PendingBlocks_list.GetNext(pos)->fQueued = 0;
			SendBlockRequests();
			return;
		}
	}
	if (now >= m_dwLastBlockReceived + DOWNLOADTIMEOUT) {
		const bool bActiveUtpTransfer = (socket != NULL && socket->IsConnected() && socket->HaveUtpLayer() && GetDownloadState() == DS_DOWNLOADING);
		if (socket != NULL && !socket->IsRawDataMode())
			SendCancelTransfer();
		else
			ASSERT(0);

		if (bActiveUtpTransfer) {
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLogWarning(_T("[NatTraversal] Download timeout on active uTP transfer; forcing reconnect, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			SetRemoteQueueRank(0);
			SetAskedCountDown(0);
			socket->Safe_Delete();
			socket = NULL;
			SetDownloadState(DS_CONNECTING, _T("Timeout. More than 100 seconds since last complete block was received."));
			if (m_reqfile != NULL)
				TrigNextSafeAskForDownload(m_reqfile);
			return;
		}

		SetDownloadState(DS_ONQUEUE, _T("Timeout. More than 100 seconds since last complete block was received."));
	}
}

uint16 CUpDownClient::GetAvailablePartCount() const
{
	UINT result = 0;
	for (UINT i = 0; i < m_nPartCount; ++i)
		result += static_cast<UINT>(IsPartAvailable(i));

	return (uint16)result;
}

void CUpDownClient::SetRemoteQueueRank(UINT nr, bool bUpdateDisplay)
{
	m_nRemoteQueueRank = nr;
	UpdateDisplayedInfo(bUpdateDisplay);
}

void CUpDownClient::UDPReaskACK(uint16 nNewQR)
{
	m_bUDPPending = false;
	SetRemoteQueueRank(nNewQR, true);
	SetLastAskedTime();
}

void CUpDownClient::UDPReaskFNF()
{
	m_bUDPPending = false;
	if (GetDownloadState() != DS_DOWNLOADING) { // avoid premature deletion of 'this' client
		if (thePrefs.GetVerbose())
			AddDebugLogLine(DLP_LOW, false, _T("UDP FNF-Answer: %s - %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (LPCTSTR)EscPercent(DbgGetFileInfo(m_reqfile ? m_reqfile->GetFileHash() : NULL)));

		if (thePrefs.IsDetectFileFaker())
			CheckFileNotFound();

		if (m_reqfile)
			m_reqfile->m_DeadSourceList.AddDeadSource(*this);
		switch (GetDownloadState()) {
		case DS_ONQUEUE:
		case DS_NONEEDEDPARTS:
			DontSwapTo(m_reqfile);
			if (SwapToAnotherFile(_T("Source says it doesn't have the file. CUpDownClient::UDPReaskFNF()"), true, true, true, NULL, false, false))
				break;
			/*fall through*/
		default:
			theApp.downloadqueue->RemoveSource(this);
			if (!socket && Disconnected(_T("UDPReaskFNF socket=NULL")))
				delete this;
		}
	} else if (thePrefs.GetVerbose())
		DebugLogWarning(_T("UDP FNF-Answer: %s - did not remove client because of current download state"), (LPCTSTR)EscPercent(GetUserName()));
}

void CUpDownClient::UDPReaskForDownload()
{
	ASSERT(m_reqfile);
	if (!m_reqfile || m_bUDPPending)
		return;

	//TODO: This should be changed to determine if the last 4 UDP packets failed, not the total one.
	if (m_nTotalUDPPackets > 3 && (m_nFailedUDPPackets / (float)m_nTotalUDPPackets > .3))
		return;

	if (GetUDPPort() != 0 && GetUDPVersion() != 0 && thePrefs.GetUDPPort() != 0
		&& !theApp.IsFirewalled() && !(socket && socket->IsConnected()) && !thePrefs.GetProxySettings().bUseProxy)
	{
		if (!HasLowID()) {
			//don't use udp to ask for sources
			if (IsSourceRequestAllowed())
				return;

			if (SwapToAnotherFile(_T("A4AF check before OP_ReaskFilePing. CUpDownClient::UDPReaskForDownload()"), true, false, false, NULL, true, true))
				return; // we swapped, so need to go to TCP

			m_bUDPPending = true;
			CSafeMemFile data(128);
			data.WriteHash16(m_reqfile->GetFileHash());
			if (GetUDPVersion() > 2) {
				if (GetUDPVersion() > 3)
					if (m_reqfile->IsPartFile())
						reinterpret_cast<CPartFile*>(m_reqfile)->WritePartStatus(data);
					else
						data.WriteUInt16(0);

				data.WriteUInt16(m_reqfile->m_nCompleteSourcesCount);
			}
			if (thePrefs.GetDebugClientUDPLevel() > 0)
				DebugSend("OP_ReaskFilePing", this, m_reqfile->GetFileHash());
			Packet *response = new Packet(data, OP_EMULEPROT);
			response->opcode = OP_REASKFILEPING;
			theStats.AddUpDataOverheadFileRequest(response->size);
			theApp.downloadqueue->AddUDPFileReasks();
			theApp.clientudp->SendPacket(response, GetIP(), GetUDPPort(), ShouldReceiveCryptUDPPackets(), GetUserHash(), false, 0);
			++m_nTotalUDPPackets;
		} else if (HasLowID() && !GetServingBuddyIP().IsNull() && GetServingBuddyPort() && HasValidServingBuddyID()) {
			m_bUDPPending = true;
			CSafeMemFile data(128);
			data.WriteHash16(GetServingBuddyID());
			data.WriteHash16(m_reqfile->GetFileHash());
			if (GetUDPVersion() > 2) {
				if (GetUDPVersion() > 3)
					if (m_reqfile->IsPartFile())
						reinterpret_cast<CPartFile*>(m_reqfile)->WritePartStatus(data);
					else
						data.WriteUInt16(0);

				data.WriteUInt16(m_reqfile->m_nCompleteSourcesCount);
			}

			// Get actual buddy IP and UDP port from the connected serving buddy client
			CAddress buddyIP = GetServingBuddyIP(); // fallback to stored value
			uint16 buddyPort = GetServingBuddyPort(); // fallback to stored value
			CUpDownClient* buddyClient = theApp.clientlist->GetServingBuddy();
			if (buddyClient != NULL) {
				if (!buddyClient->GetIP().IsNull())
					buddyIP = buddyClient->GetIP(); // use real-time IP from connected buddy
				if (buddyClient->GetUDPPort() != 0)
					buddyPort = buddyClient->GetUDPPort(); // use real-time port from connected buddy
			}

			if (thePrefs.GetDebugClientUDPLevel() > 0)
				DebugSend("OP_ReaskCallbackUDP", this, m_reqfile->GetFileHash());
			if (thePrefs.GetLogNatTraversalEvents())
				DebugLog(_T("[NAT-T][LowID-Reask] Sending OP_REASKCALLBACKUDP to serving buddy %s:%u for file %s, target source %s"), 
					(LPCTSTR)ipstr(buddyIP), buddyPort, 
					(LPCTSTR)EscPercent(m_reqfile->GetFileName()), 
					(LPCTSTR)EscPercent(DbgGetClientInfo()));
			Packet *response = new Packet(data, OP_EMULEPROT);
			response->opcode = OP_REASKCALLBACKUDP;
			theStats.AddUpDataOverheadFileRequest(response->size);
			theApp.downloadqueue->AddUDPFileReasks();
			// FIXME: We don't know which kad version the buddy has, so we need to send unencrypted
			theApp.clientudp->SendPacket(response, buddyIP, buddyPort, false, NULL, true, 0);
			++m_nTotalUDPPackets;
		}
	}
}

void CUpDownClient::UpdateDisplayedInfo(bool force)
{
	theApp.emuledlg->transferwnd->GetDownloadList()->UpdateItem(this);
	theApp.emuledlg->transferwnd->GetClientList()->RefreshClient(this);
	theApp.emuledlg->transferwnd->GetDownloadClientsList()->RefreshClient(this);
}

bool CUpDownClient::IsInNoNeededList(const CPartFile *fileToCheck) const
{
	return m_OtherNoNeeded_list.Find(const_cast<CPartFile*>(fileToCheck)) != NULL;
}

bool CUpDownClient::SwapToRightFile(CPartFile *SwapTo, CPartFile *cur_file, bool ignoreSuspensions, bool SwapToIsNNPFile, bool curFileisNNPFile, bool &wasSkippedDueToSourceExchange, bool doAgressiveSwapping, bool debug)
{
	bool printDebug = debug && thePrefs.GetLogA4AF();

	if (printDebug) {
		AddDebugLogLine(DLP_LOW, false, _T("oooo Debug: SwapToRightFile. Start compare SwapTo: %s and cur_file %s"), SwapTo ? (LPCTSTR)EscPercent(SwapTo->GetFileName()) : _T("null"), (LPCTSTR)EscPercent(cur_file->GetFileName()));
		AddDebugLogLine(DLP_LOW, false, _T("oooo Debug: doAgressiveSwapping: %s"), doAgressiveSwapping ? _T("true") : _T("false"));
	}

	if (!SwapTo)
		return true;

	if ((!curFileisNNPFile && cur_file->GetSourceCount() < cur_file->GetMaxSources())
		|| (curFileisNNPFile && cur_file->GetSourceCount() < cur_file->GetMaxSources() * 4 / 5))
	{
		if (printDebug)
			AddDebugLogLine(DLP_VERYLOW, false, _T("oooo Debug: cur_file probably does not have too many sources."));

		if (SwapTo->GetSourceCount() > SwapTo->GetMaxSources()
			|| (SwapTo->GetSourceCount() >= SwapTo->GetMaxSources() * 4 / 5
				&& SwapTo == m_reqfile
				&& (GetDownloadState() == DS_LOWTOLOWIP || GetDownloadState() == DS_REMOTEQUEUEFULL)
			   )
		   )
		{
			if (printDebug)
				AddDebugLogLine(DLP_VERYLOW, false, _T("oooo Debug: SwapTo is about to be deleted due to too many sources on that file, so we can steal it."));
			return true;
		}

		if (ignoreSuspensions || !IsSwapSuspended(cur_file, doAgressiveSwapping, curFileisNNPFile)) {
			if (printDebug)
				AddDebugLogLine(DLP_VERYLOW, false, _T("oooo Debug: No suspend block."));

			DWORD curTick = ::GetTickCount();
			DWORD curAsked = GetLastAskedTime(cur_file);
			DWORD swapAsked = GetLastAskedTime(SwapTo);
			bool rightFileHasHigherPrio = CPartFile::RightFileHasHigherPrio(SwapTo, cur_file);
			// wait two re-ask interval for each nnp file before re-asking an nnp file
			DWORD allNnpReaskTime = (DWORD)(FILEREASKTIME * 2 * (m_OtherNoNeeded_list.GetCount() + static_cast<int>(GetDownloadState() == DS_NONEEDEDPARTS)));
			DWORD curReask = curAsked + allNnpReaskTime;

			if (!SwapToIsNNPFile && (!curFileisNNPFile || curAsked == 0 || curTick >= curReask) && rightFileHasHigherPrio
				|| SwapToIsNNPFile && curFileisNNPFile &&
				(
					swapAsked != 0
					&& (
						curAsked == 0
						|| (swapAsked > curAsked && (curTick >= curReask || (rightFileHasHigherPrio && curTick < swapAsked + allNnpReaskTime)))
					)
					|| rightFileHasHigherPrio && swapAsked == 0 && curAsked == 0
				)
				|| SwapToIsNNPFile && !curFileisNNPFile)
			{
				if (printDebug)
					if (!SwapToIsNNPFile && !curFileisNNPFile && rightFileHasHigherPrio)
						AddDebugLogLine(DLP_VERYLOW, false, _T("oooo Debug: Higher prio."));
					else if (!SwapToIsNNPFile && (curAsked == 0 || curTick >= curReask) && rightFileHasHigherPrio)
						AddDebugLogLine(DLP_VERYLOW, false, _T("oooo Debug: Time to re-ask nnp and it had higher prio."));
					else if (swapAsked != 0
							&& (
								curAsked == 0
								|| (swapAsked > curAsked && (curTick >= curReask || (rightFileHasHigherPrio && curTick < swapAsked + allNnpReaskTime)))
								)
							)
					{
						AddDebugLogLine(DLP_VERYLOW, false, _T("oooo Debug: Both nnp and cur_file has longer time since re-asked."));
					}
					else if (SwapToIsNNPFile && !curFileisNNPFile)
						AddDebugLogLine(DLP_VERYLOW, false, _T("oooo Debug: SwapToIsNNPFile && !curFileisNNPFile"));
					else
						AddDebugLogLine(DLP_VERYLOW, false, _T("oooo Debug: Higher prio for unknown reason!"));

				if (IsSourceRequestAllowed(cur_file) && (cur_file->AllowSwapForSourceExchange(curTick) || (cur_file == m_reqfile && RecentlySwappedForSourceExchange()))
					|| !(IsSourceRequestAllowed(SwapTo) && (SwapTo->AllowSwapForSourceExchange(curTick) || (SwapTo == m_reqfile && RecentlySwappedForSourceExchange())))
					|| (GetDownloadState() == DS_ONQUEUE && GetRemoteQueueRank() <= 50))
				{
					if (printDebug)
						AddDebugLogLine(DLP_LOW, false, _T("oooo Debug: Source Request check OK."));
					return true;
				}

				if (printDebug)
					AddDebugLogLine(DLP_VERYLOW, false, _T("oooo Debug: Source Request check failed."));
				wasSkippedDueToSourceExchange = true;
			}

			if (IsSourceRequestAllowed(cur_file, true) && (cur_file->AllowSwapForSourceExchange(curTick) || (cur_file == m_reqfile && RecentlySwappedForSourceExchange()))
				&& !(IsSourceRequestAllowed(SwapTo, true) && (SwapTo->AllowSwapForSourceExchange(curTick) || (SwapTo == m_reqfile && RecentlySwappedForSourceExchange())))
				&& (GetDownloadState() != DS_ONQUEUE || GetRemoteQueueRank() > 50))
			{
				wasSkippedDueToSourceExchange = true;

				if (printDebug)
					AddDebugLogLine(DLP_LOW, false, _T("oooo Debug: Source Exchange."));
				return true;
			}
		} else if (printDebug)
			AddDebugLogLine(DLP_VERYLOW, false, _T("oooo Debug: Suspend block."));

	} else if (printDebug)
		AddDebugLogLine(DLP_VERYLOW, false, _T("oooo Debug: cur_file probably has too many sources."));

	if (printDebug)
		AddDebugLogLine(DLP_LOW, false, _T("oooo Debug: Return false"));

	return false;
}

bool CUpDownClient::SwapToAnotherFile(LPCTSTR reason, bool bIgnoreNoNeeded, bool ignoreSuspensions, bool bRemoveCompletely, CPartFile *toFile, bool allowSame, bool isAboutToAsk, bool debug)
{
	bool printDebug = debug && thePrefs.GetLogA4AF();

	if (printDebug)
		AddDebugLogLine(DLP_LOW, false, _T("ooo Debug: Switching source %s Remove = %s; bIgnoreNoNeeded = %s; allowSame = %s; Reason = \"%s\""), (LPCTSTR)EscPercent(DbgGetClientInfo()), (bRemoveCompletely ? _T("Yes") : _T("No")), (bIgnoreNoNeeded ? _T("Yes") : _T("No")), (allowSame ? _T("Yes") : _T("No")), reason);

	if (!bRemoveCompletely && allowSame && thePrefs.GetA4AFSaveCpu()) {
		// Only swap if we can't keep the old source
		if (printDebug)
			AddDebugLogLine(DLP_LOW, false, _T("ooo Debug: return false since prefs setting to save CPU is enabled."));
		return false;
	}

	bool doAgressiveSwapping = (bRemoveCompletely || !allowSame || isAboutToAsk);
	if (printDebug)
		AddDebugLogLine(DLP_LOW, false, _T("ooo Debug: doAgressiveSwapping: %s"), doAgressiveSwapping ? _T("true") : _T("false"));

	if (!bRemoveCompletely && !ignoreSuspensions && allowSame && GetTimeUntilReask(m_reqfile, doAgressiveSwapping, true, false) > 0 && (GetDownloadState() != DS_NONEEDEDPARTS || m_OtherRequests_list.IsEmpty())) {
		if (printDebug)
			AddDebugLogLine(DLP_LOW, false, _T("ooo Debug: return false due to not reached re-ask time: GetTimeUntilReask(...) > 0"));

		return false;
	}

	if (!bRemoveCompletely) {
		if (allowSame && m_OtherRequests_list.IsEmpty() && (/* !bIgnoreNoNeeded ||*/ m_OtherNoNeeded_list.IsEmpty())) {
			// no file to swap too, and it's OK to keep it
			if (printDebug)
				AddDebugLogLine(DLP_LOW, false, _T("ooo Debug: return false due to no file to swap too, and it's OK to keep it."));
			return false;
		}
		switch (GetDownloadState()) {
		case DS_ONQUEUE:
		case DS_NONEEDEDPARTS:
		case DS_REMOTEQUEUEFULL:
		case DS_CONNECTED:
			break;
		default:
			if (printDebug)
				AddDebugLogLine(DLP_LOW, false, _T("ooo Debug: return false due to wrong state."));
			return false;
		}
	}

	CPartFile *SwapTo = NULL;
	POSITION finalpos = NULL;
	CTypedPtrList<CPtrList, CPartFile*> *usedList = NULL;

	if (allowSame && !bRemoveCompletely) {
		SwapTo = m_reqfile;
		if (printDebug)
			AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: allowSame: File %s SourceReq: %s"), (LPCTSTR)EscPercent(m_reqfile->GetFileName()), IsSourceRequestAllowed(m_reqfile) ? _T("true") : _T("false"));
	}

	bool SwapToIsNNP = (SwapTo != NULL && SwapTo == m_reqfile && GetDownloadState() == DS_NONEEDEDPARTS);

	CPartFile *skippedDueToSourceExchange = NULL;
	bool skippedIsNNP = false;

	if (!m_OtherRequests_list.IsEmpty()) {
		if (printDebug)
			AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: m_OtherRequests_list"));

		for (POSITION pos = m_OtherRequests_list.GetHeadPosition(); pos != NULL;) {
			POSITION pos2 = pos;
			CPartFile *cur_file = m_OtherRequests_list.GetNext(pos);

			if (printDebug)
				AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Checking file: %s SoureReq: %s"), (LPCTSTR)EscPercent(cur_file->GetFileName()), IsSourceRequestAllowed(cur_file) ? _T("true") : _T("false"));

			if (!bRemoveCompletely && !ignoreSuspensions && allowSame && IsSwapSuspended(cur_file, doAgressiveSwapping, false)) {
				if (printDebug)
					AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: continue due to IsSwapSuspended(file) == true"));
				continue;
			}

			if (cur_file != m_reqfile && theApp.downloadqueue->IsPartFile(cur_file) && !cur_file->IsStopped()
				&& (cur_file->GetStatus(false) == PS_READY || cur_file->GetStatus(false) == PS_EMPTY))
			{
				if (printDebug)
					AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: It's a partfile, not stopped, etc."));

				if (toFile != NULL) {
					if (cur_file == toFile) {
						if (printDebug)
							AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Found toFile."));

						SwapTo = cur_file;
						SwapToIsNNP = false;
						usedList = &m_OtherRequests_list;
						finalpos = pos2;
						break;
					}
				} else {
					bool wasSkippedDueToSourceExchange = false;
					if (SwapToRightFile(SwapTo, cur_file, ignoreSuspensions, SwapToIsNNP, false, wasSkippedDueToSourceExchange, doAgressiveSwapping, debug)) {
						if (printDebug)
							AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Swapping to file %s"), (LPCTSTR)EscPercent(cur_file->GetFileName()));

						if (SwapTo && wasSkippedDueToSourceExchange) {
							if (debug && thePrefs.GetLogA4AF())
								AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Swapped due to source exchange possibility"));
							bool discardSkipped = false;
							if (SwapToRightFile(skippedDueToSourceExchange, SwapTo, ignoreSuspensions, skippedIsNNP, SwapToIsNNP, discardSkipped, doAgressiveSwapping, debug)) {
								skippedDueToSourceExchange = SwapTo;
								skippedIsNNP = skippedIsNNP ? true : (SwapTo == m_reqfile && GetDownloadState() == DS_NONEEDEDPARTS);
								if (printDebug)
									AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Skipped file was better than last skipped file."));
							}
						}

						SwapTo = cur_file;
						SwapToIsNNP = false;
						usedList = &m_OtherRequests_list;
						finalpos = pos2;
					} else {
						if (printDebug) //SwapToRightFile ensured that SwapTo != NULL
							AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Keeping file %s"), (LPCTSTR)EscPercent(SwapTo->GetFileName()));
						if (wasSkippedDueToSourceExchange) {
							if (printDebug)
								AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Kept the file due to source exchange possibility"));
							bool discardSkipped = false;
							if (SwapToRightFile(skippedDueToSourceExchange, cur_file, ignoreSuspensions, skippedIsNNP, false, discardSkipped, doAgressiveSwapping, debug)) {
								skippedDueToSourceExchange = cur_file;
								skippedIsNNP = false;
								if (printDebug)
									AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Skipped file was better than last skipped file."));
							}
						}
					}
				}
			}
		}
	}

	if (printDebug)
		AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: m_OtherNoNeeded_list"));

	for (POSITION pos = m_OtherNoNeeded_list.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		CPartFile *cur_file = m_OtherNoNeeded_list.GetNext(pos);

		if (printDebug)
			AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Checking file: %s "), (LPCTSTR)EscPercent(cur_file->GetFileName()));

		if (!bRemoveCompletely && !ignoreSuspensions && allowSame && IsSwapSuspended(cur_file, doAgressiveSwapping, true)) {
			if (printDebug)
				AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: continue due to !IsSwapSuspended(file) == true"));
			continue;
		}

		if (cur_file != m_reqfile && theApp.downloadqueue->IsPartFile(cur_file) && !cur_file->IsStopped()
			&& (cur_file->GetStatus(false) == PS_READY || cur_file->GetStatus(false) == PS_EMPTY))
		{
			if (printDebug)
				AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: It's a partfile, not stopped, etc."));

			if (toFile != NULL) {
				if (cur_file == toFile) {
					if (printDebug)
						AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Found toFile."));

					SwapTo = cur_file;
					usedList = &m_OtherNoNeeded_list;
					finalpos = pos2;
					break;
				}
			} else {
				bool wasSkippedDueToSourceExchange = false;
				if (SwapToRightFile(SwapTo, cur_file, ignoreSuspensions, SwapToIsNNP, true, wasSkippedDueToSourceExchange, doAgressiveSwapping, debug)) {
					if (printDebug)
						AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Swapping to file %s"), (LPCTSTR)EscPercent(cur_file->GetFileName()));

					if (SwapTo && wasSkippedDueToSourceExchange) {
						if (printDebug)
							AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Swapped due to source exchange possibility"));
						bool discardSkipped = false;
						if (SwapToRightFile(skippedDueToSourceExchange, SwapTo, ignoreSuspensions, skippedIsNNP, SwapToIsNNP, discardSkipped, doAgressiveSwapping, debug)) {
							skippedDueToSourceExchange = SwapTo;
							skippedIsNNP = skippedIsNNP ? true : (SwapTo == m_reqfile && GetDownloadState() == DS_NONEEDEDPARTS);
							if (printDebug)
								AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Skipped file was better than last skipped file."));
						}
					}

					SwapTo = cur_file;
					SwapToIsNNP = true;
					usedList = &m_OtherNoNeeded_list;
					finalpos = pos2;
				} else {
					if (printDebug) //SwapToRightFile ensured that SwapTo != NULL
						AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Keeping file %s"), (LPCTSTR)EscPercent(SwapTo->GetFileName()));
					if (wasSkippedDueToSourceExchange) {
						if (debug && thePrefs.GetVerbose())
							AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Kept the file due to source exchange possibility"));
						bool discardSkipped = false;
						if (SwapToRightFile(skippedDueToSourceExchange, cur_file, ignoreSuspensions, skippedIsNNP, true, discardSkipped, doAgressiveSwapping, debug)) {
							skippedDueToSourceExchange = cur_file;
							skippedIsNNP = true;
							if (printDebug)
								AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Skipped file was better than last skipped file."));
						}
					}
				}
			}
		}
	}

	if (SwapTo) {
		if (printDebug)
			if (SwapTo != m_reqfile)
				AddDebugLogLine(DLP_LOW, false, _T("ooo Debug: Found file to swap to %s"), (LPCTSTR)EscPercent(SwapTo->GetFileName()));
			else
				AddDebugLogLine(DLP_LOW, false, _T("ooo Debug: Will keep current file. %s"), (LPCTSTR)EscPercent(SwapTo->GetFileName()));

		CString strInfo(reason);
		if (skippedDueToSourceExchange) {
			bool wasSkippedDueToSourceExchange = false;
			bool skippedIsBetter = SwapToRightFile(SwapTo, skippedDueToSourceExchange, ignoreSuspensions, SwapToIsNNP, skippedIsNNP, wasSkippedDueToSourceExchange, doAgressiveSwapping, debug);
			if (skippedIsBetter || wasSkippedDueToSourceExchange) {
				SwapTo->SetSwapForSourceExchangeTick();
				SetSwapForSourceExchangeTick();

				strInfo.Insert(0, _T("******SourceExchange-Swap****** "));
				if (printDebug)
					AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Due to sourceExchange."));
				else if (thePrefs.GetLogA4AF() && m_reqfile == SwapTo) //m_reqfile != NULL here
					AddDebugLogLine(DLP_LOW, false, _T("ooo Didn't swap source due to source exchange possibility. %s Remove = %s '%s' Reason: %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (bRemoveCompletely ? _T("Yes") : _T("No")), (LPCTSTR)EscPercent(m_reqfile->GetFileName()), (LPCTSTR)EscPercent(strInfo));

			} else if (printDebug)
				AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Normal. SwapTo better than skippedDueToSourceExchange."));
		} else if (printDebug)
			AddDebugLogLine(DLP_VERYLOW, false, _T("ooo Debug: Normal. skippedDueToSourceExchange == NULL"));

		if (SwapTo != m_reqfile && DoSwap(SwapTo, bRemoveCompletely, strInfo)) {
			if (debug && thePrefs.GetLogA4AF())
				AddDebugLogLine(DLP_LOW, false, _T("ooo Debug: Swap successful."));
			if (usedList && finalpos)
				usedList->RemoveAt(finalpos);
			return true;
		}
		if (printDebug)
			AddDebugLogLine(DLP_LOW, false, _T("ooo Debug: Swap didn't happen."));
	}

	if (printDebug)
		AddDebugLogLine(DLP_LOW, false, _T("ooo Debug: Done %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));

	return false;
}

bool CUpDownClient::DoSwap(CPartFile* SwapTo, bool bRemoveCompletely, LPCTSTR reason)
{
	if (m_reqfile) {
		if (thePrefs.GetLogA4AF())
			AddDebugLogLine(DLP_LOW, false, _T("ooo Swapped source %s Remove = %s '%s'   -->   %s Reason: %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (bRemoveCompletely ? _T("Yes") : _T("No")), (LPCTSTR)EscPercent(m_reqfile->GetFileName()), (LPCTSTR)EscPercent(SwapTo->GetFileName()), (LPCTSTR)EscPercent(reason));

		// 17-Dez-2003 [bc]: This "m_reqfile->srclists[sourcesslot].Find(this)" was the only place where
		// the usage of the "CPartFile::srclists[100]" is more efficient than using one list. If this
		// function here is still (again) a performance problem, there is a more efficient way to handle
		// the 'Find' situation. Hint: usage of a node ptr which is stored in the CUpDownClient.
		POSITION pos = m_reqfile->srclist.Find(this);
		if (pos)
			m_reqfile->srclist.RemoveAt(pos);
		else
			AddDebugLogLine(DLP_HIGH, false, _T("o-o Unsync between partfile->srclist and client otherfiles list. Swapping client where client has file as reqfile, but file doesn't have client in srclist. %s Remove = %s '%s'   -->   '%s'  SwapReason: %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (bRemoveCompletely ? _T("Yes") : _T("No")), m_reqfile ? (LPCTSTR)EscPercent(m_reqfile->GetFileName()) : (LPCTSTR)EMPTY, (LPCTSTR)EscPercent(SwapTo->GetFileName()), (LPCTSTR)EscPercent(reason));

		m_reqfile->RemoveDownloadingSource(this);

		if (!bRemoveCompletely) {
			m_reqfile->A4AFsrclist.AddTail(this);
			if (GetDownloadState() == DS_NONEEDEDPARTS)
				m_OtherNoNeeded_list.AddTail(m_reqfile);
			else
				m_OtherRequests_list.AddTail(m_reqfile);

			theApp.emuledlg->transferwnd->GetDownloadList()->AddSource(m_reqfile, this, true);
		}
		else
			m_fileReaskTimes.RemoveKey(m_reqfile);

		SetDownloadState(DS_NONE);
		CPartFile* pOldRequestFile = m_reqfile;
		pOldRequestFile->UpdatePartsInfo();
		pOldRequestFile->UpdateAvailablePartsCount();
	}

	// remove this client from the A4AF list of our new reqfile
	POSITION pos2 = SwapTo->A4AFsrclist.Find(this);
	if (pos2)
		SwapTo->A4AFsrclist.RemoveAt(pos2);
	else
		AddDebugLogLine(DLP_HIGH, false, _T("o-o Unsync between partfile->srclist and client otherfiles list. Swapping client where client has file in another list, but file doesn't have client in a4af srclist. %s Remove = %s '%s'   -->   '%s'  SwapReason: %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (bRemoveCompletely ? _T("Yes") : _T("No")), (LPCTSTR)EscPercent(m_reqfile->GetFileName()), (LPCTSTR)EscPercent(SwapTo->GetFileName()), (LPCTSTR)EscPercent(reason));

	theApp.emuledlg->transferwnd->GetDownloadList()->RemoveSource(this, SwapTo);
	SetRequestFile(SwapTo);
	SwapTo->srclist.AddTail(this);
	theApp.emuledlg->transferwnd->GetDownloadList()->AddSource(SwapTo, this, false);
	return true;
}

void CUpDownClient::DontSwapTo(/*const*/ CPartFile *file)
{
	const DWORD curTick = ::GetTickCount();

	for (POSITION pos = m_DontSwap_list.GetHeadPosition(); pos != NULL;) {
		PartFileStamp &pfs = m_DontSwap_list.GetNext(pos);
		if (pfs.file == file) {
			pfs.timestamp = curTick;
			return;
		}
	}
	m_DontSwap_list.AddHead(PartFileStamp{ file, curTick });
}

bool CUpDownClient::IsSwapSuspended(const CPartFile *file, const bool allowShortReaskTime, const bool fileIsNNP)
{
	if (file == m_reqfile)
		return false;

	// Don't swap if we have re-asked this client very short time ago
	if (GetTimeUntilReask(file, allowShortReaskTime, true, fileIsNNP) > 0)
		return true;

	if (m_DontSwap_list.IsEmpty())
		return false;

	for (POSITION pos = m_DontSwap_list.GetHeadPosition(); pos != 0 && !m_DontSwap_list.IsEmpty();) {
		POSITION pos2 = pos;
		const PartFileStamp &pfs = m_DontSwap_list.GetNext(pos);
		if (pfs.file == file) {
			if (::GetTickCount() >= pfs.timestamp + PURGESOURCESWAPSTOP) {
				m_DontSwap_list.RemoveAt(pos2);
				return false;
			}
			return true;
		}
		if (pfs.file == NULL) // in which case should this happen?
			m_DontSwap_list.RemoveAt(pos2);
	}

	return false;
}

DWORD CUpDownClient::GetTimeUntilReask(const CPartFile *file, const bool allowShortReaskTime, const bool useGivenNNP, const bool givenNNP) const
{
	DWORD lastAskedTimeTick = GetLastAskedTime(file);
	if (lastAskedTimeTick > 0) {
		DWORD reaskTime;
		if (allowShortReaskTime || (file == m_reqfile && GetDownloadState() == DS_NONE))
			reaskTime = MIN_REQUESTTIME;
		//seeing that MLDonkey has a reask which is ~3 times as fast as eMules and MLDonkey downloads ~3 times as much from each other as an eMule client
		//this seems to be a sensible approach, though hammering is NOT a good solution... 
		else if (GetClientSoft() == SO_MLDONKEY && thePrefs.IsEmulateMLDonkey()) {
			reaskTime = MIN_REQUESTTIME;
		// Shareaza allows its users to lower the ReaskTime down to 20 Minutes !! Okay, so do we.... but hardcoded.
		} else if (GetClientSoft() == SO_SHAREAZA && thePrefs.IsEmulateShareaza()) {
			reaskTime = MIN_REQUESTTIME*2;
		// no change for eMule Plus as they do use the same system like eMule
		}
		else if (  (useGivenNNP && givenNNP)
				|| (file == m_reqfile && GetDownloadState() == DS_NONEEDEDPARTS)
				|| (file != m_reqfile && IsInNoNeededList(file)))
		{
			reaskTime = FILEREASKTIME * 2;
		} else if (StrStrI(m_strModVersion, _T("Xtreme")) != 0 || StrStrI(m_strModVersion, _T("ScarAngel")) != 0) {
			if(GetSpreadReAskTime() > FILEREASKTIME) // try to spread if possible
				reaskTime = FILEREASKTIME;
			else
				reaskTime = GetSpreadReAskTime();
		} else
			reaskTime = GetSpreadReAskTime() + thePrefs.GetReAskTimeDif();

		const DWORD curTick = ::GetTickCount();
		if (curTick < lastAskedTimeTick + reaskTime)
			return reaskTime - (curTick - lastAskedTimeTick);
	}
	return 0;
}

DWORD CUpDownClient::GetTimeUntilReask(const CPartFile *file) const
{
	return GetTimeUntilReask(file, false);
}

DWORD CUpDownClient::GetTimeUntilReask() const
{
	return GetTimeUntilReask(m_reqfile);
}

bool CUpDownClient::IsValidSource() const
{
	switch (GetDownloadState()) {
	case DS_DOWNLOADING:
	case DS_ONQUEUE:
	case DS_CONNECTED:
	case DS_NONEEDEDPARTS:
	case DS_REMOTEQUEUEFULL:
	case DS_REQHASHSET:
		return IsEd2kClient();
	}
	return false;
}

void CUpDownClient::StartDownload()
{
	SetDownloadState(DS_DOWNLOADING);
	InitTransferredDownMini();
	SetDownStartTime();
	m_lastPartAsked = _UI16_MAX;
	SendBlockRequests();
}

void CUpDownClient::SendCancelTransfer()
{
	if (socket == NULL || !IsEd2kClient()) {
		ASSERT(0);
		return;
	}

	if (!GetSentCancelTransfer()) {
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP_CancelTransfer", this);

		Packet *pCancelTransferPacket = new Packet(OP_CANCELTRANSFER, 0);
		theStats.AddUpDataOverheadFileRequest(pCancelTransferPacket->size);
		socket->SendPacket(pCancelTransferPacket);
		SetSentCancelTransfer(true);
	}
}

void CUpDownClient::SetRequestFile(CPartFile *pReqFile)
{
	if (pReqFile != m_reqfile || m_reqfile == NULL)
		ResetFileStatusInfo();
	m_reqfile = pReqFile;
}

void CUpDownClient::ProcessAcceptUpload()
{
    if (thePrefs.GetLogNatTraversalEvents()) {
        DebugLog(_T("[NatTraversal] OP_ACCEPTUPLOADREQ received from %s (file=%s), entering download path"), (LPCTSTR)EscPercent(DbgGetClientInfo()), m_reqfile ? (LPCTSTR)EscPercent(m_reqfile->GetFileName()) : _T("<null>"));
    }
	// Upload slot is accepted, queue-rank request is no longer pending.
	m_fQueueRankPending = 0;
	m_fUnaskQueueRankRecv = 0;
	if (m_reqfile && !m_reqfile->IsStopped() && (m_reqfile->GetStatus() == PS_READY || m_reqfile->GetStatus() == PS_EMPTY)) {
		SetSentCancelTransfer(0);
		// Accept upload slot for both DS_ONQUEUE and DS_CONNECTED states
		// DS_CONNECTED occurs when handshake completes before queue response arrives (NAT-T/uTP timing)
		if (GetDownloadState() == DS_ONQUEUE || GetDownloadState() == DS_CONNECTED) {
			StartDownload();
		}
	} else {
		SendCancelTransfer();
		SetDownloadState((m_reqfile == NULL || m_reqfile->IsStopped()) ? DS_NONE : DS_ONQUEUE);
	}
}

void CUpDownClient::ProcessEdonkeyQueueRank(const uchar *packet, UINT size)
{
	CSafeMemFile data(packet, size);
	uint32 rank = data.ReadUInt32();
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		Debug(_T("  QR=%u (prev. %d)\n"), rank, IsRemoteQueueFull() ? UINT_MAX : (UINT)GetRemoteQueueRank());
	SetRemoteQueueRank(rank, GetDownloadState() == DS_ONQUEUE);
	if (thePrefs.IsDetectRankFlooder())
		CheckQueueRankFlood();
}

void CUpDownClient::ProcessEmuleQueueRank(const uchar *packet, UINT size)
{
	if (size != 12)
		throw GetResString(_T("ERR_BADSIZE"));
	uint16 rank = PeekUInt16(packet);
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		Debug(_T("  QR=%u\n"), rank); // no prev. QR available for eMule clients
	SetRemoteQueueFull(false);
	SetRemoteQueueRank(rank, GetDownloadState() == DS_ONQUEUE);
	if (thePrefs.IsDetectRankFlooder())
		CheckQueueRankFlood();
}

void CUpDownClient::CheckQueueRankFlood()
{
	if (m_fQueueRankPending == 0) {
		if (GetDownloadState() != DS_DOWNLOADING) {
			if (m_fUnaskQueueRankRecv < 3) // NOTE: Do not increase this nr. without increasing the bits for 'm_fUnaskQueueRankRecv'
				++m_fUnaskQueueRankRecv;
			if (m_fUnaskQueueRankRecv == 3) {
				if (theApp.clientlist->GetBadRequests(this) < 2)
					theApp.clientlist->TrackBadRequest(this, 1);
				if (theApp.clientlist->GetBadRequests(this) == 2) {
					theApp.clientlist->TrackBadRequest(this, -2); // reset so the client will not be re-banned right after the ban is lifted
					Ban(GetResString(_T("PUNISHMENT_REASON_QR_FLOOD")));
				}
				throwCStr(thePrefs.GetLogBannedClients() ? GetResString(_T("PUNISHMENT_REASON_QR_FLOOD")) : EMPTY);
			}
		}
	} else {
		m_fQueueRankPending = 0;
		m_fUnaskQueueRankRecv = 0;
	}
}

DWORD CUpDownClient::GetLastAskedTime(const CPartFile *pFile) const
{
	const CFileReaskTimesMap::CPair *pair = m_fileReaskTimes.PLookup(pFile ? pFile : m_reqfile);
	return pair ? pair->value : 0;
}

// TODO fileident optimize to save some memory
void CUpDownClient::SetReqFileAICHHash(CAICHHash *val)
{
	if (m_pReqFileAICHHash != NULL && m_pReqFileAICHHash != val)
		delete m_pReqFileAICHHash;
	m_pReqFileAICHHash = val;
}

void CUpDownClient::SendAICHRequest(CPartFile *pForFile, uint16 nPart)
{
	CAICHRequestedData request;
	request.m_nPart = nPart;
	request.m_pClient = this;
	request.m_pPartFile = pForFile;
	CAICHRecoveryHashSet::m_liRequestedData.AddTail(request);
	m_fAICHRequested = true;
	CSafeMemFile data;
	data.WriteHash16(pForFile->GetFileHash());
	data.WriteUInt16(nPart);
	pForFile->GetAICHRecoveryHashSet()->GetMasterHash().Write(data);
	Packet *packet = new Packet(data, OP_EMULEPROT, OP_AICHREQUEST);
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_AichRequest", this, (uchar*)packet->pBuffer);
	theStats.AddUpDataOverheadFileRequest(packet->size);
	SafeConnectAndSendPacket(packet);
}

void CUpDownClient::ProcessAICHAnswer(const uchar *packet, UINT size)
{
	if (!m_fAICHRequested)
		throwCStr(_T("Received unrequested AICH Packet"));

	m_fAICHRequested = false;

	CSafeMemFile data(packet, size);
	if (size <= 16) {
		CAICHRecoveryHashSet::ClientAICHRequestFailed(this);
		return;
	}
	uchar abyHash[MDX_DIGEST_SIZE];
	data.ReadHash16(abyHash);
	CPartFile *pPartFile = theApp.downloadqueue->GetFileByID(abyHash);
	CAICHRequestedData request = CAICHRecoveryHashSet::GetAICHReqDetails(this);
	uint16 nPart = data.ReadUInt16();
	if (pPartFile != NULL && request.m_pPartFile == pPartFile && request.m_pClient == this && nPart == request.m_nPart) {
		CAICHHash ahMasterHash(data);
		CAICHRecoveryHashSet *rhashset = pPartFile->GetAICHRecoveryHashSet();
		if ((rhashset->GetStatus() == AICH_TRUSTED || rhashset->GetStatus() == AICH_VERIFIED)
			&& ahMasterHash == rhashset->GetMasterHash())
		{
			if (rhashset->ReadRecoveryData(request.m_nPart * PARTSIZE, data)) {
				// finally all checks passed, everything seem to be fine
				AddDebugLogLine(DLP_DEFAULT, false, _T("AICH Packet Answer: Succeeded to read and validate received recovery data"));
				CAICHRecoveryHashSet::RemoveClientAICHRequest(this);
				pPartFile->AICHRecoveryDataAvailable(request.m_nPart);
				// After AICH recovery, newly reopened gaps can leave a source in stale NNP state.
				// Immediately re-queue OP_STARTUPLOADREQ to avoid waiting full NNP re-ask timeout.
				if (m_reqfile == pPartFile
					&& GetDownloadState() == DS_NONEEDEDPARTS
					&& socket != NULL
					&& socket->IsConnected()
					&& !pPartFile->IsComplete(request.m_nPart))
				{
					if (thePrefs.GetLogNatTraversalEvents())
						DebugLog(_T("[NatTraversal] AICH recovery reopened gaps; re-requesting upload slot from NNP source %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
					SendStartupLoadReq();
				}
				return;
			}
			DebugLogError(_T("AICH Packet Answer: Failed to read and validate received recovery data"));
		} else
			AddDebugLogLine(DLP_HIGH, false, _T("AICH Packet Answer: Masterhash differs from packet hash or hashset has no trusted Masterhash"));
	} else
		AddDebugLogLine(DLP_HIGH, false, _T("AICH Packet Answer: requested values differ from values in packet"));

	CAICHRecoveryHashSet::ClientAICHRequestFailed(this);
}

void CUpDownClient::ProcessAICHRequest(const uchar *packet, UINT size)
{
	if (size != (16u + 2u + CAICHHash::GetHashSize()))
		throwCStr(_T("Received AICH Request Packet with wrong size"));

	CSafeMemFile data(packet, size);
	uchar abyHash[MDX_DIGEST_SIZE];
	data.ReadHash16(abyHash);
	uint16 nPart = data.ReadUInt16();
	CAICHHash ahMasterHash(data);
	CKnownFile *pKnownFile = theApp.sharedfiles->GetFileByID(abyHash);
	if (pKnownFile != NULL) {
		const CFileIdentifier &fileid = pKnownFile->GetFileIdentifier();
		if (pKnownFile->IsAICHRecoverHashSetAvailable()
			&& fileid.HasAICHHash()
			&& fileid.GetAICHHash() == ahMasterHash
			&& pKnownFile->GetPartCount() > nPart
			&& (uint64)pKnownFile->GetFileSize() > PARTSIZE * nPart + EMBLOCKSIZE)
		{
			CSafeMemFile fileResponse;
			fileResponse.WriteHash16(pKnownFile->GetFileHash());
			fileResponse.WriteUInt16(nPart);
			fileid.GetAICHHash().Write(fileResponse);
			CAICHRecoveryHashSet recHashSet(pKnownFile, pKnownFile->GetFileSize());
			recHashSet.SetMasterHash(fileid.GetAICHHash(), AICH_HASHSETCOMPLETE);
			if (recHashSet.CreatePartRecoveryData(nPart * PARTSIZE, fileResponse)) {
				AddDebugLogLine(DLP_HIGH, false, _T("AICH Packet Request: Successfully created and send recovery data for %s to %s"), (LPCTSTR)EscPercent(pKnownFile->GetFileName()), (LPCTSTR)EscPercent(DbgGetClientInfo()));
				if (thePrefs.GetDebugClientTCPLevel() > 0)
					DebugSend("OP_AichAnswer", this, pKnownFile->GetFileHash());
				Packet *packAnswer = new Packet(fileResponse, OP_EMULEPROT, OP_AICHANSWER);
				theStats.AddUpDataOverheadFileRequest(packAnswer->size);
				SafeConnectAndSendPacket(packAnswer);
				return;
			} else
				AddDebugLogLine(DLP_HIGH, false, _T("AICH Packet Request: Failed to create recovery data for %s to %s"), (LPCTSTR)EscPercent(pKnownFile->GetFileName()), (LPCTSTR)EscPercent(DbgGetClientInfo()));
		} else
			AddDebugLogLine(DLP_HIGH, false, _T("AICH Packet Request: Failed to create recovery data - Hashset not ready or requested Hash differs from Masterhash for %s to %s"), (LPCTSTR)EscPercent(pKnownFile->GetFileName()), (LPCTSTR)EscPercent(DbgGetClientInfo()));
	} else
		AddDebugLogLine(DLP_HIGH, false, _T("AICH Packet Request: Failed to find requested shared file -  %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));

	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_AichAnswer", this, abyHash);
	Packet *packAnswer = new Packet(OP_AICHANSWER, 16, OP_EMULEPROT);
	md4cpy(packAnswer->pBuffer, abyHash);
	theStats.AddUpDataOverheadFileRequest(packAnswer->size);
	SafeConnectAndSendPacket(packAnswer);
}

void CUpDownClient::ProcessAICHFileHash(CSafeMemFile *data, CPartFile *file, const CAICHHash *pAICHHash)
{
	CPartFile *pPartFile = file;
	if (pPartFile == NULL && data != NULL) {
		uchar abyHash[MDX_DIGEST_SIZE];
		data->ReadHash16(abyHash);
		pPartFile = theApp.downloadqueue->GetFileByID(abyHash);
	}
	CAICHHash ahMasterHash;
	if (pAICHHash == NULL) {
		if (data)
			ahMasterHash.Read(*data);
	} else
		ahMasterHash = *pAICHHash;
	if (pPartFile != NULL && pPartFile == GetRequestFile()) {
		SetReqFileAICHHash(new CAICHHash(ahMasterHash));
		pPartFile->GetAICHRecoveryHashSet()->UntrustedHashReceived(ahMasterHash, GetConnectIP());

		if (pPartFile->GetFileIdentifierC().HasAICHHash() && pPartFile->GetFileIdentifierC().GetAICHHash() != ahMasterHash) {
			// this a legacy client and he sent us a hash different from our verified one, which means
			// the file identifiers are different. We handle this just like a FNF-Answer to our download request
			// and remove the client from our sourcelist, because we sure don't want to download from him
			pPartFile->m_DeadSourceList.AddDeadSource(*this);
			DebugLogWarning(_T("Client answered with different AICH hash than local verified on in ProcessAICHFileHash, removing source. File %s, client %s"), (LPCTSTR)EscPercent(pPartFile->GetFileName()), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			// if this client does not have my file but may hava a different one
			// we try to swap to other file while ignoring no needed parts files
			switch (GetDownloadState()) {
			case DS_REQHASHSET:
				// for the love of eMule, don't accept a hash set from him :)
				if (m_fHashsetRequestingMD4) {
					DebugLogWarning(_T("... also cancelled hash set request from client due to AICH mismatch"));
					pPartFile->m_bMD4HashsetNeeded = true;
					m_fHashsetRequestingMD4 = false;
				}
				if (m_fHashsetRequestingAICH) {
					ASSERT(0);
					pPartFile->SetAICHHashSetNeeded(true);
					m_fHashsetRequestingAICH = false;
				}
			case DS_CONNECTED:
			case DS_ONQUEUE:
			case DS_NONEEDEDPARTS:
			case DS_DOWNLOADING:
				DontSwapTo(pPartFile); // ZZ:DownloadManager
				if (!SwapToAnotherFile(_T("Source says it doesn't have the file (AICH mismatch). CUpDownClient::ProcessAICHFileHash"), true, true, true, NULL, false, false)) // ZZ:DownloadManager
					theApp.downloadqueue->RemoveSource(this);
			}
		}
	} else
		AddDebugLogLine(DLP_HIGH, false, _T("ProcessAICHFileHash(): PartFile not found or Partfile differs from requested file, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
}

void CUpDownClient::SendHashSetRequest()
{
	if (socket && socket->IsConnected()) {
		Packet *packet = NULL;
		if (SupportsFileIdentifiers()) {
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugSend("OP_HashSetRequest2", this, m_reqfile->GetFileHash());
			CSafeMemFile filePacket(60);
			m_reqfile->GetFileIdentifier().WriteIdentifier(filePacket);
			// 6 Request Options - RESERVED
			// 1 Request AICH HashSet
			// 1 Request MD4 HashSet
			uint8 byOptions = 0;
			if (m_reqfile->m_bMD4HashsetNeeded) {
				m_fHashsetRequestingMD4 = 1;
				byOptions |= 0x01;
				m_reqfile->m_bMD4HashsetNeeded = false;
			}
			if (m_reqfile->IsAICHPartHashSetNeeded() && GetReqFileAICHHash() != NULL && *GetReqFileAICHHash() == m_reqfile->GetFileIdentifier().GetAICHHash()) {
				m_fHashsetRequestingAICH = 1;
				byOptions |= 0x02;
				m_reqfile->SetAICHHashSetNeeded(false);
			}
			if (byOptions == 0) {
				ASSERT(0);
				return;
			}
			DEBUG_ONLY(DebugLog(_T("Sending HashSet Request: MD4 %s, AICH %s to client %s"), m_fHashsetRequestingMD4 ? _T("Yes") : _T("No")
				, m_fHashsetRequestingAICH ? _T("Yes") : _T("No"), (LPCTSTR)DbgGetClientInfo()));
			filePacket.WriteUInt8(byOptions);
			packet = new Packet(filePacket, OP_EMULEPROT, OP_HASHSETREQUEST2);
		} else {
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				DebugSend("OP_HashSetRequest", this, m_reqfile->GetFileHash());
			packet = new Packet(OP_HASHSETREQUEST, 16);
			md4cpy(packet->pBuffer, m_reqfile->GetFileHash());
			m_fHashsetRequestingMD4 = 1;
			m_reqfile->m_bMD4HashsetNeeded = false;
		}
		theStats.AddUpDataOverheadFileRequest(packet->size);
		SendPacket(packet);
		SetDownloadState(DS_REQHASHSET);
	} else
		ASSERT(0);
}
void CUpDownClient::ProcessFileIncStatus(CSafeMemFile* data, CPartFile* pReqFile, const bool bReadHash)
{
	CPartFile* m_pReqFile = pReqFile;

	if (bReadHash) {
		uchar cfilehash[16];
		data->ReadHash16(cfilehash);
		m_pReqFile = theApp.downloadqueue->GetFileByID(cfilehash);
		if (m_pReqFile == NULL)
			CheckFailedFileIdReqs(cfilehash);

		if (m_pReqFile == NULL)
			throw _T("ProcessFileIncStatus: Client sent ICS query for an unrequested file.");

		if (!md4equ(cfilehash, m_pReqFile->GetFileHash()))
			throw _T("ProcessFileIncStatus: Client sent ICS query with an unmatched hash value for the requested file.");

	} else if (m_pReqFile == NULL)
		throw _T("ProcessFileIncStatus: Client sent ICS query for an unrequested file.");

	uint16 nED2KPartCount = data->ReadUInt16();

	if (nED2KPartCount && m_pReqFile->GetED2KPartCount() != nED2KPartCount) {
		CString strError;
		strError.Format(_T("ProcessFileIncStatus: Client sent ICS query with an unmatched part count for the requested file: Received=%u  Expected=%u  %s"), nED2KPartCount, m_pReqFile->GetED2KPartCount(), (LPCTSTR)EscPercent(DbgGetFileInfo(m_pReqFile->GetFileHash())));
		throw strError;
	}

delete[] m_abyIncPartStatus;
m_abyIncPartStatus = NULL;
m_nIncPartCount = 0;

	if (nED2KPartCount) {
		m_abyIncPartStatus = new uint8[nED2KPartCount];
		memset(m_abyIncPartStatus, 0, nED2KPartCount);
		m_nIncPartCount = nED2KPartCount;
		uint16 done = 0;
		while (done != nED2KPartCount) {
			const uint8 toread = data->ReadUInt8();
			for (uint8 i = 0; i != 8; ++i) {
				m_abyIncPartStatus[done] = ((toread >> i) & 1) ? 1 : 0;
				++done;
				if (done == nED2KPartCount)
					break;
			}
		}
	}

	m_pReqFile->NewSrcIncPartsInfo();
}

void CUpDownClient::CleanUp(const CPartFile* pDeletedFile) {
	// Check if all pointers to the delete file have been removed
	if (m_reqfile == pDeletedFile) {
		m_reqfile = NULL;
	}

	//Xman x4.1
	for (POSITION pos = m_DontSwap_list.GetHeadPosition(); pos != 0; ) {
		POSITION cur_pos = pos;
		CPartFile* cur_file = m_DontSwap_list.GetNext(pos).file;
		if (cur_file == pDeletedFile)
			m_DontSwap_list.RemoveAt(cur_pos);
	}

	for (POSITION pos = m_OtherRequests_list.GetHeadPosition(); pos != 0; ) {
		POSITION cur_pos = pos;
		CPartFile* cur_file = m_OtherRequests_list.GetNext(pos);
		if (cur_file == pDeletedFile) {
			m_OtherRequests_list.RemoveAt(cur_pos);
		}
	}

	for (POSITION pos = m_OtherNoNeeded_list.GetHeadPosition(); pos != 0; ) {
		POSITION cur_pos = pos;
		CPartFile* cur_file = m_OtherNoNeeded_list.GetNext(pos);
		if (cur_file == pDeletedFile) {
			m_OtherNoNeeded_list.RemoveAt(cur_pos);
		}
	}

	// Drop per-file reask timestamp for the deleted file to avoid stale pointer reuse.
	m_fileReaskTimes.RemoveKey(pDeletedFile);
}

void CUpDownClient::TrigNextSafeAskForDownload(CPartFile* pFile) {
	// Check when the specified file has been asked for the last time and  define when the file can be asked again without risking to be banished.
	if (pFile == NULL)
		return;

	DWORD m_dwTimeUntilReask = GetTimeUntilReask(pFile);
	// Check GetTimeUntilReask to  reask without risk of Ban() (=> 11 minutes)
	if (m_dwTimeUntilReask == 0) // File has never been asked before
		m_dwLastTriedToConnect = ::GetTickCount() - MIN2MS(11); // Safe immediate reask: MIN2MS(11) = MIN_REQUESTTIME + 60000
	else
		m_dwLastTriedToConnect = ::GetTickCount() - MIN2MS(11) + m_dwTimeUntilReask; // Schedule reask time: MIN2MS(11) = MIN_REQUESTTIME + 60000
}
