
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
#include "StdAfx.h"
#include "corruptionblackbox.h"
#include "knownfile.h"
#include "updownclient.h"
#include "log.h"
#include "emule.h"
#include "clientlist.h"
#include "opcodes.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define	 CBB_BANTHRESHOLD	32 //% max corrupted data

CCBBRecord::CCBBRecord(uint64 nStartPos, uint64 nEndPos, CAddress IP, EBBRStatus BBRStatus)
{
	if (nStartPos <= nEndPos) {
		m_nStartPos = nStartPos;
		m_nEndPos = nEndPos;
		m_IP = IP;
		m_BBRStatus = BBRStatus;
	} else
		ASSERT(0);
}

bool CCBBRecord::Merge(uint64 nStartPos, uint64 nEndPos, CAddress IP, EBBRStatus BBRStatus)
{
	if (m_IP != IP || m_BBRStatus != BBRStatus)
		return false;
	if (nStartPos == m_nEndPos + 1)
		m_nEndPos = nEndPos;
	else if (nEndPos + 1 == m_nStartPos)
		m_nStartPos = nStartPos;
	else
		return false;
	return true;
}

bool CCBBRecord::CanMerge(uint64 nStartPos, uint64 nEndPos, CAddress IP, EBBRStatus BBRStatus) const
{
	return m_IP == IP && m_BBRStatus == BBRStatus && (nStartPos == m_nEndPos + 1 || nEndPos + 1 == m_nStartPos);
}

void CCorruptionBlackBox::Init(EMFileSize nFileSize)
{
	m_aaRecords.SetSize((INT_PTR)(((uint64)nFileSize + PARTSIZE - 1) / PARTSIZE));
}

void CCorruptionBlackBox::Free()
{
	m_aaRecords.SetSize(0);
}

void CCorruptionBlackBox::TransferredData(uint64 nStartPos, uint64 nEndPos, const CUpDownClient *pSender)
{
	if (nEndPos - nStartPos >= PARTSIZE || nStartPos > nEndPos) {
		ASSERT(0);
		return;
	}

	if (!pSender) //importing parts
		return;
	CAddress SenderIP = pSender->GetIP();
	// we store records separated for each part, so we don't have to search all entries every time

	// convert pos to relative block pos
	INT_PTR nPart = (INT_PTR)(nStartPos / PARTSIZE);
	const uint64 nStart = nPart * PARTSIZE;
	uint64 nRelStartPos = nStartPos - nStart;
	uint64 nRelEndPos = nEndPos - nStart;
	if (nRelEndPos >= PARTSIZE) {
		// data crosses the part boundary, split it
		nRelEndPos = PARTSIZE - 1;
		TransferredData(nStart + PARTSIZE, nEndPos, pSender);
	}

	INT_PTR posMerge = -1;
	uint64 ndbgRewritten = 0;
	for (INT_PTR i = 0; i < m_aaRecords[nPart].GetCount(); ++i) {
		CCBBRecord &cbbRec(m_aaRecords[nPart][i]);
		if (cbbRec.CanMerge(nRelStartPos, nRelEndPos, SenderIP, BBR_NONE))
			posMerge = i;
		// check if there is already a pending entry and overwrite it
		else if (cbbRec.m_BBRStatus == BBR_NONE) {
			if (cbbRec.m_nStartPos >= nRelStartPos && cbbRec.m_nEndPos <= nRelEndPos) {
				// old one is included into the new one -> delete
				ndbgRewritten += (cbbRec.m_nEndPos - cbbRec.m_nStartPos) + 1;
				m_aaRecords[nPart].RemoveAt(i);
				--i;
			} else if (cbbRec.m_nStartPos < nRelStartPos && cbbRec.m_nEndPos > nRelEndPos) {
				// old one includes the new one
				// check if both old and new have the same ip
				if (SenderIP != cbbRec.m_IP) {
					// different IP; we have to split this into 3 blocks
					// TODO
					// verify that this split will not decrease the amount of the verified data for the old IP,
					// or incorrect total good bytes count for the old IP would be seen in EvaluateData()
					uint64 nOldStartPos = cbbRec.m_nStartPos;
					uint64 nOldEndPos = cbbRec.m_nEndPos;
					CAddress OldIP = cbbRec.m_IP;
					//adjust the original block
					cbbRec.m_nStartPos = nRelStartPos;
					cbbRec.m_nEndPos = nRelEndPos;
					cbbRec.m_IP = SenderIP;

					m_aaRecords[nPart].Add(CCBBRecord(nOldStartPos, nRelStartPos - 1, OldIP));
					//prepare to add one more block
					nRelStartPos = cbbRec.m_nEndPos + 1;
					nRelEndPos = nOldEndPos;
					SenderIP = OldIP;
					ndbgRewritten += nRelEndPos - nRelStartPos + 1;
					break; // done here
				}
			} else if (cbbRec.m_nStartPos >= nRelStartPos && cbbRec.m_nStartPos <= nRelEndPos) {
				// old one overlaps the new one on the right side
				ASSERT(nRelEndPos > cbbRec.m_nStartPos);
				ndbgRewritten += nRelEndPos - cbbRec.m_nStartPos;
				cbbRec.m_nStartPos = nRelEndPos + 1;
			} else if (cbbRec.m_nEndPos >= nRelStartPos && cbbRec.m_nEndPos <= nRelEndPos) {
				// old one overlaps the new one on the left side
				ASSERT(cbbRec.m_nEndPos > nRelStartPos);
				ndbgRewritten += cbbRec.m_nEndPos - nRelStartPos;
				cbbRec.m_nEndPos = nRelStartPos - 1;
			}
		}
	}
	if (posMerge >= 0)
		VERIFY(m_aaRecords[nPart][posMerge].Merge(nRelStartPos, nRelEndPos, SenderIP, BBR_NONE));
	else
		m_aaRecords[nPart].Add(CCBBRecord(nRelStartPos, nRelEndPos, SenderIP, BBR_NONE));

	if (ndbgRewritten > 0)
		DEBUG_ONLY(AddDebugLogLine(DLP_DEFAULT, false, _T("CorruptionBlackBox: Debug: %i bytes were rewritten and records replaced with new stats"), ndbgRewritten));
}

void CCorruptionBlackBox::VerifiedData(uint64 nStartPos, uint64 nEndPos)
{
	if (nEndPos >= nStartPos + PARTSIZE) {
		ASSERT(0);
		return;
	}
	// convert pos to relative block pos
	INT_PTR nPart = (INT_PTR)(nStartPos / PARTSIZE);
	uint64 nRelStartPos = nStartPos - nPart * PARTSIZE;
	uint64 nRelEndPos = nEndPos - nPart * PARTSIZE;
	if (nRelEndPos >= PARTSIZE) {
		ASSERT(0);
		return;
	}
#ifdef _DEBUG
	uint64 nDbgVerifiedBytes = 0;
	CMap<CString, LPCTSTR, int, int> mapDebug;
#endif
	for (INT_PTR i = 0; i < m_aaRecords[nPart].GetCount(); ++i) {
		CCBBRecord &cbbRec(m_aaRecords[nPart][i]);
		if (cbbRec.m_BBRStatus == BBR_NONE || cbbRec.m_BBRStatus == BBR_VERIFIED) {
			if (cbbRec.m_nStartPos >= nRelStartPos && cbbRec.m_nEndPos <= nRelEndPos)
				; //all this block is inside the new verified data; only set status to verified
			else if (cbbRec.m_nStartPos < nRelStartPos && cbbRec.m_nEndPos > nRelEndPos) {
				// new data fully within this block; split this block into 3
				uint64 nOldStartPos = cbbRec.m_nStartPos;
				uint64 nOldEndPos = cbbRec.m_nEndPos;
				cbbRec.m_nStartPos = nRelStartPos;
				cbbRec.m_nEndPos = nRelEndPos;
				m_aaRecords[nPart].Add(CCBBRecord(nRelEndPos + 1, nOldEndPos, cbbRec.m_IP, cbbRec.m_BBRStatus));
				m_aaRecords[nPart].Add(CCBBRecord(nOldStartPos, nRelStartPos - 1, cbbRec.m_IP, cbbRec.m_BBRStatus));
			} else if (cbbRec.m_nStartPos >= nRelStartPos && cbbRec.m_nStartPos <= nRelEndPos) {
				// split off the tail of this block
				uint64 nOldEndPos = cbbRec.m_nEndPos;
				cbbRec.m_nEndPos = nRelEndPos;
				m_aaRecords[nPart].Add(CCBBRecord(nRelEndPos + 1, nOldEndPos, cbbRec.m_IP, cbbRec.m_BBRStatus));
			} else if (cbbRec.m_nEndPos >= nRelStartPos && cbbRec.m_nEndPos <= nRelEndPos) {
				// split off the head of this block
				uint64 nOldStartPos = cbbRec.m_nStartPos;
				cbbRec.m_nStartPos = nRelStartPos;
				m_aaRecords[nPart].Add(CCBBRecord(nOldStartPos, nRelStartPos - 1, cbbRec.m_IP, cbbRec.m_BBRStatus));
			} else
				continue;
			cbbRec.m_BBRStatus = BBR_VERIFIED;
#ifdef _DEBUG
			nDbgVerifiedBytes += cbbRec.m_nEndPos - cbbRec.m_nStartPos + 1;
			mapDebug[cbbRec.m_IP.ToStringC()] = 1;
#endif
			}
	}
}

void CCorruptionBlackBox::CorruptedData(uint64 nStartPos, uint64 nEndPos)
{
	if (nEndPos - nStartPos >= EMBLOCKSIZE) {
		ASSERT(0);
		return;
	}
	// convert pos to relative block pos
	INT_PTR nPart = (INT_PTR)(nStartPos / PARTSIZE);
	uint64 nRelStartPos = nStartPos - nPart * PARTSIZE;
	uint64 nRelEndPos = nEndPos - nPart * PARTSIZE;
	if (nRelEndPos >= PARTSIZE) {
		ASSERT(0);
		return;
	}
	uint64 nDbgVerifiedBytes = 0;
	for (INT_PTR i = 0; i < m_aaRecords[nPart].GetCount(); ++i) {
		CCBBRecord &cbbRec(m_aaRecords[nPart][i]);
		if (cbbRec.m_BBRStatus == BBR_NONE) {
			if (cbbRec.m_nStartPos >= nRelStartPos && cbbRec.m_nEndPos <= nRelEndPos)
				;
			else if (cbbRec.m_nStartPos < nRelStartPos && cbbRec.m_nEndPos > nRelEndPos) {
				// need to split it 2*
				uint64 nOldStartPos = cbbRec.m_nStartPos;
				uint64 nOldEndPos = cbbRec.m_nEndPos;
				cbbRec.m_nStartPos = nRelStartPos;
				cbbRec.m_nEndPos = nRelEndPos;
				m_aaRecords[nPart].Add(CCBBRecord(nRelEndPos + 1, nOldEndPos, cbbRec.m_IP, cbbRec.m_BBRStatus));
				m_aaRecords[nPart].Add(CCBBRecord(nOldStartPos, nRelStartPos - 1, cbbRec.m_IP, cbbRec.m_BBRStatus));
			} else if (cbbRec.m_nStartPos >= nRelStartPos && cbbRec.m_nStartPos <= nRelEndPos) {
				// need to split it
				uint64 nOldEndPos = cbbRec.m_nEndPos;
				cbbRec.m_nEndPos = nRelEndPos;
				m_aaRecords[nPart].Add(CCBBRecord(nRelEndPos + 1, nOldEndPos, cbbRec.m_IP, cbbRec.m_BBRStatus));
			} else if (cbbRec.m_nEndPos >= nRelStartPos && cbbRec.m_nEndPos <= nRelEndPos) {
				// need to split it
				uint64 nOldStartPos = cbbRec.m_nStartPos;
				cbbRec.m_nStartPos = nRelStartPos;
				m_aaRecords[nPart].Add(CCBBRecord(nOldStartPos, nRelStartPos - 1, cbbRec.m_IP, cbbRec.m_BBRStatus));
			} else
				continue;
			cbbRec.m_BBRStatus = BBR_CORRUPTED;
			nDbgVerifiedBytes += cbbRec.m_nEndPos - cbbRec.m_nStartPos + 1;
		}
	}
	AddDebugLogLine(DLP_HIGH, false, _T("Found and marked %I64u recorded bytes of %I64u as corrupted in the CorruptionBlackBox records"), nDbgVerifiedBytes, (nEndPos - nStartPos) + 1);
}

void CCorruptionBlackBox::EvaluateData(uint16 nPart)
{
	CArray<CString> aGuiltyClients;
	for (INT_PTR i = m_aaRecords[nPart].GetCount(); --i >= 0;)
		if (m_aaRecords[nPart][i].m_BBRStatus == BBR_CORRUPTED)
			aGuiltyClients.Add(m_aaRecords[nPart][i].m_IP.ToStringC());

	// check if any IPs are already banned, so we can skip the test for those
	for (INT_PTR k = 0; k < aGuiltyClients.GetCount();) {
		// remove duplicates
		for (INT_PTR y = aGuiltyClients.GetCount(); --y > k;)
			if (aGuiltyClients[k] == aGuiltyClients[y])
				aGuiltyClients.RemoveAt(y);

		if (theApp.clientlist->IsBannedClient(aGuiltyClients[k])) {
			AddDebugLogLine(DLP_DEFAULT, false, _T("CorruptionBlackBox: Suspicious IP (%s) is already banned, skipping recheck"), (LPCTSTR)aGuiltyClients[k]);
			aGuiltyClients.RemoveAt(k);
		} else
			++k;
	}

	const INT_PTR iClients = aGuiltyClients.GetCount();
	if (iClients <= 0)
		return;
	// parse all recorded data for this file to produce statistics for the involved clients
	// first init arrays for the statistics
	CArray<uint64> aDataCorrupt, aDataVerified;
	aDataCorrupt.InsertAt(0, 0, iClients);
	aDataVerified.InsertAt(0, 0, iClients);

	// now the parsing
	for (INT_PTR iPart = m_aaRecords.GetCount(); --iPart >= 0;)
		for (INT_PTR i = m_aaRecords[iPart].GetCount(); --i >= 0;)
			for (INT_PTR k = 0; k < iClients; ++k) {
				const CCBBRecord &cbbRec(m_aaRecords[iPart][i]);
				if (cbbRec.m_IP.ToStringC() == aGuiltyClients[k])
					if (cbbRec.m_BBRStatus == BBR_CORRUPTED)
						// corrupted data records are always counted as at least block size (180 KB) or more
						aDataCorrupt[k] += max(cbbRec.m_nEndPos - cbbRec.m_nStartPos + 1, (uint64)EMBLOCKSIZE);
					else if (cbbRec.m_BBRStatus == BBR_VERIFIED)
						aDataVerified[k] += cbbRec.m_nEndPos - cbbRec.m_nStartPos + 1;
			}

	// calculate percentage of corrupted data for each client and ban if over the limit
	for (INT_PTR k = 0; k < iClients; ++k) {
		int nCorruptPercentage;
		if ((aDataVerified[k] + aDataCorrupt[k]) > 0)
			nCorruptPercentage = (int)((aDataCorrupt[k] * 100) / (aDataVerified[k] + aDataCorrupt[k]));
		else {
			AddDebugLogLine(DLP_HIGH, false, _T("CorruptionBlackBox: Program Error: No records for guilty client found!"));
			ASSERT(0);
			nCorruptPercentage = 0;
		}
		CUpDownClient *pClient = theApp.clientlist->FindClientByIP(CAddress(aGuiltyClients[k], false));
		if (nCorruptPercentage > CBB_BANTHRESHOLD && thePrefs.IsDetectCorruptedDataSender()) { //evil client
			if (pClient) {
				theApp.clientlist->AddTrackClient(pClient);
				pClient->Ban(GetResString(_T("PUNISHMENT_REASON_CORRUPT_DATA_SENDER")));
			} else
				theApp.clientlist->AddBannedClient(aGuiltyClients[k]);

			AddProtectionLogLine(false, _T("CorruptionBlackBox: Banning: Found client which sent %s of %s corrupted data, %s")
				, (LPCTSTR)CastItoXBytes(aDataCorrupt[k])
				, (LPCTSTR)CastItoXBytes((aDataVerified[k] + aDataCorrupt[k]))
				, (LPCTSTR)(pClient ? (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()) : aGuiltyClients[k]));
		} else { //suspected client
			if (pClient)
				theApp.clientlist->AddTrackClient(pClient);

			AddProtectionLogLine(false, _T("CorruptionBlackBox: Reporting: Found client which probably sent %s of %s corrupted data, but it is within the acceptable limit, %s")
				, (LPCTSTR)CastItoXBytes(aDataCorrupt[k])
				, (LPCTSTR)CastItoXBytes((aDataVerified[k] + aDataCorrupt[k]))
				, (LPCTSTR)(pClient ? (LPCTSTR)EscPercent(pClient->DbgGetClientInfo()) : aGuiltyClients[k]));
		}
	}
}
