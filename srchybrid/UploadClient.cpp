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
#include "Opcodes.h"
#include "Packets.h"
#include "UploadQueue.h"
#include "Statistics.h"
#include "ClientList.h"
#include "ClientUDPSocket.h"
#include "SharedFileList.h"
#include "KnownFileList.h"
#include "PartFile.h"
#include "ClientCredits.h"
#include "ListenSocket.h"
#include "ServerConnect.h"
#include "SafeFile.h"
#include "DownloadQueue.h"
#include "emuledlg.h"
#include "TransferDlg.h"
#include "Log.h"
#include "Collection.h"
#include "UploadDiskIOThread.h"
#include "eMuleAI/Shield.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

static bool ResolveSecureIdentForUpload(const CUpDownClient* client, EIdentState& identState, CAddress& scoreIP)
{
	scoreIP = client->GetIP();
	identState = client->Credits()->GetCurrentIdentState(scoreIP);

	if (!(client->socket && client->socket->HaveUtpLayer(true)))
		return false;

	const CAddress& verifiedIP = client->GetSessionSecureIdentIP();
	if (verifiedIP.IsNull())
		return false;

	if (client->Credits()->GetCurrentIdentState(verifiedIP) != IS_IDENTIFIED)
		return false;

	scoreIP = verifiedIP;
	identState = IS_IDENTIFIED;
	return true;
}

static bool IsNonSuiPunishment(const uint8 badClientCategory)
{
	switch (badClientCategory) {
	case PR_NONSUIMLDONKEY:
	case PR_NONSUIEDONKEY:
	case PR_NONSUIEDONKEYHYBRID:
	case PR_NONSUISHAREAZA:
	case PR_NONSUILPHANT:
	case PR_NONSUIAMULE:
	case PR_NONSUIEMULE:
		return true;
	}
	return false;
}


//	members of CUpDownClient
//	which are mainly used for uploading functions

CBarShader CUpDownClient::s_UpStatusBar(16);

void CUpDownClient::ReleaseBarShaders() noexcept
{
	s_StatusBar.ReleaseBuffers();
	s_UpStatusBar.ReleaseBuffers();
}

void CUpDownClient::DrawUpStatusBar(CDC *dc, const CRect &rect, bool onlygreyrect, bool  bFlat) const
{
	COLORREF crNeither, crNextSending, crBoth, crSending;

	if (GetSlotNumber() <= (UINT)theApp.uploadqueue->GetActiveUploadsCount()
		|| (GetUploadState() != US_UPLOADING && GetUploadState() != US_CONNECTING))
	{
		crNeither = RGB(224, 224, 224); //light grey
		crNextSending = RGB(255, 208, 0); //dark yellow
		crBoth = bFlat ? RGB(0, 0, 0) : RGB(104, 104, 104); //black : very dark gray
		crSending = RGB(0, 150, 0); //dark green
	} else {
		// grayed out
		crNeither = RGB(248, 248, 248); //very light grey
		crNextSending = RGB(255, 244, 191); //pale yellow
		crBoth = /*bFlat ? RGB(191, 191, 191) :*/ RGB(191, 191, 191); //mid-grey
		crSending = RGB(191, 229, 191); //pale green
	}

	// wistily: UpStatusFix
	CKnownFile *currequpfile = theApp.sharedfiles->GetFileByID(requpfileid);
	EMFileSize filesize = currequpfile ? currequpfile->GetFileSize() : PARTSIZE * m_nUpPartCount;
	// wistily: UpStatusFix

	if (filesize > 0ull) {
		s_UpStatusBar.SetFileSize(filesize);
		s_UpStatusBar.SetHeight(rect.Height());
		s_UpStatusBar.SetWidth(rect.Width());
		s_UpStatusBar.Fill(crNeither);
		if (!onlygreyrect && m_abyUpPartStatus)
			for (UINT i = 0; i < m_nUpPartCount; ++i)
				if (m_abyUpPartStatus[i])
					s_UpStatusBar.FillRange(i * PARTSIZE, i * PARTSIZE + PARTSIZE, crBoth);

		UploadingToClient_Struct *pUpClientStruct = theApp.uploadqueue->GetUploadingClientStructByClient(this);
		if (pUpClientStruct != NULL) {
			CSingleLock lockBlockLists(&pUpClientStruct->m_csBlockListsLock, TRUE);
			ASSERT(lockBlockLists.IsLocked());
			const Requested_Block_Struct *block;
			if (!pUpClientStruct->m_BlockRequests_queue.IsEmpty()) {
				block = pUpClientStruct->m_BlockRequests_queue.GetHead();
				if (block) {
					uint64 start = (block->StartOffset / PARTSIZE) * PARTSIZE;
					s_UpStatusBar.FillRange(start, start + PARTSIZE, crNextSending);
				}
			}
			if (!pUpClientStruct->m_DoneBlocks_list.IsEmpty()) {
				block = pUpClientStruct->m_DoneBlocks_list.GetHead();
				if (block) {
					uint64 start = (block->StartOffset / PARTSIZE) * PARTSIZE;
					s_UpStatusBar.FillRange(start, start + PARTSIZE, crNextSending);
				}
				for (POSITION pos = pUpClientStruct->m_DoneBlocks_list.GetHeadPosition();pos != 0;) {
					block = pUpClientStruct->m_DoneBlocks_list.GetNext(pos);
					s_UpStatusBar.FillRange(block->StartOffset, block->EndOffset + 1, crSending);
				}
			}
			lockBlockLists.Unlock();
		}
		s_UpStatusBar.Draw(dc, rect.left, rect.top, bFlat);
	}
}

void CUpDownClient::SetUploadState(EUploadState eNewState)
{
	if (eNewState != m_eUploadState) {
		if (m_eUploadState == US_UPLOADING) {
			// Reset upload data rate computation
			m_nUpDatarate = 0;
			m_nSumForAvgUpDataRate = 0;
			m_AverageUDR_list.RemoveAll();
		}
		if (eNewState == US_UPLOADING) {
			if (socket != NULL && socket->DropQueuedControlPacket(OP_OUTOFPARTREQS, OP_EDONKEYPROT) && thePrefs.GetLogNatTraversalEvents()) {
				AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal][UploadQueue] Dropped stale queued OP_OutOfPartReqs before starting a new upload session for %s"),
					(LPCTSTR)EscPercent(DbgGetClientInfo()));
			}
			m_fSentOutOfPartReqs = 0;
		}
		//Xman remove banned. remark: this method is called recursive
		if(eNewState == US_BANNED && (m_eUploadState == US_UPLOADING || m_eUploadState == US_CONNECTING))
		{
			theApp.uploadqueue->RemoveFromUploadQueue(this, _T("banned client"));
		}

		// don't add any final cleanups for US_NONE here
		m_eUploadState = eNewState;
		theApp.emuledlg->transferwnd->GetClientList()->RefreshClient(this);
	}
}

/*
 * Gets the queue score multiplier for this client, taking into consideration client's credits
 * and the requested file's priority.
 */
float CUpDownClient::GetCombinedFilePrioAndCredit()
{
	if (!credits) {
		//zz_fly :: in the Optimized on ClientCredits, banned client has no credits
		ASSERT (GetUploadState()==US_BANNED);
		return 0.0F;
	}

	return 10.0f * credits->GetScoreRatio(GetIP()) * GetFilePrioAsNumber();
}

/*
 * Gets the file multiplier for the file this client has requested.
 */
int CUpDownClient::GetFilePrioAsNumber() const
{
	const CKnownFile *currequpfile = theApp.sharedfiles->GetFileByID(requpfileid);
	if (!currequpfile)
		return 0;

	// TODO coded by tecxx & herbert, one yet unsolved problem here:
	// sometimes a client asks for 2 files and there is no way to decide, which file the
	// client finally gets. so it could happen that he is queued first because of a
	// high prio file, but then asks for something completely different.
	switch (currequpfile->GetUpPriority()) {
	case PR_VERYHIGH:
		return 18;
	case PR_HIGH:
		return 9;
	case PR_LOW:
		return 6;
	case PR_VERYLOW:
		return 2;
	//case PR_NORMAL:
	//default:
	//	break;
	}
	return 7;
}

/*
 * Gets the current waiting score for this client, taking into consideration
 * waiting time, priority of requested file, and the client's credits.
 */
const uint32 CUpDownClient::GetScore(const bool sysvalue, const bool isdownloading, const bool onlybasevalue)
{
	if (m_pszUsername == NULL || GetUploadFileID() == NULL)
		return 0;

	if (!credits)
		return 0;

	EIdentState identState = IS_NOTAVAILABLE;
	CAddress scoreIP;
	const bool bTrustedUtpSecureIdent = ResolveSecureIdentForUpload(this, identState, scoreIP);
	const bool bPendingUtpSecureIdent = (socket && socket->HaveUtpLayer(true) && IsSecureIdentRecheckPending() && !bTrustedUtpSecureIdent);

	// bad clients (see note in function)
	if (!bPendingUtpSecureIdent && !bTrustedUtpSecureIdent && identState == IS_IDBADGUY)
		return 0;

	if (!theApp.sharedfiles->GetFileByID(requpfileid)) //is any file requested?
		return 0;

	// friend slot
	if (IsFriend() && GetFriendSlot() && !HasLowID())
		return 0x0FFFFFFFu;

	if (IsBanned() || m_bGPLEvildoer)
		return 0;

	if (sysvalue && HasLowID() && !(socket && socket->IsConnected()))
		return 0;

	// calculate score, based on waiting time and other factors
	DWORD dwBaseValue;
	if (onlybasevalue)
		dwBaseValue = SEC2MS(100);
	else if (!isdownloading)
		dwBaseValue = ::GetTickCount() - GetWaitStartTime();
	else {
		// we don't want one client to download forever
		// the first 15 min download time counts as 15 min waiting time and you get
		// a 15 min bonus while you are in the first 15 min :)
		// (to avoid 20 sec downloads) after this the score won't rise any more
		dwBaseValue = m_dwUploadTime - GetWaitStartTime();
		dwBaseValue += MIN2MS(::GetTickCount() >= m_dwUploadTime + MIN2MS(15) ? 15 : 30);
	}
	float fBaseValue = dwBaseValue / SEC2MS(1.0f);
	if (thePrefs.UseCreditSystem() && !bPendingUtpSecureIdent)
		fBaseValue *= credits->GetScoreRatio(scoreIP);

	if (!onlybasevalue)
		fBaseValue *= GetFilePrioAsNumber() / 10.0f;

	if (!m_bUploaderPunishmentPreventionActive && !(thePrefs.IsDontPunishFriends() && IsFriend())) {
		// Check if uTP connection - SecureIdent works differently for uTP due to IP changes during NAT traversal
		bool bIsUtpConnection = (socket && socket->HaveUtpLayer(true));

		if (thePrefs.GetVerbose() && bIsUtpConnection && credits) {
			TRACE(_T("[uTP-SUI] GetScore check: %s - IsUtp=%d IdentState=%d Trusted=%d Pending=%d ScoreIP=%s CurrentIP=%s\n"),
				(LPCTSTR)GetUserName(), bIsUtpConnection, identState, bTrustedUtpSecureIdent, bPendingUtpSecureIdent,
				(LPCTSTR)scoreIP.ToStringC(), (LPCTSTR)GetIP().ToStringC());
		}

		// A fresh uTP re-auth should not inherit stale non-SUI penalties from the previous transport session.
		if (!bPendingUtpSecureIdent) {
			// ==> Punish Clients without SUI - sFrQlXeRt // IsHarderPunishment isn't necessary here since the cost is low
			if (thePrefs.IsPunishNonSuiMlDonkey() && GetClientSoft() == SO_MLDONKEY & identState != IS_IDENTIFIED)
				theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_NON_SUI_MLDONKEY")), PR_NONSUIMLDONKEY);
			else if (thePrefs.IsPunishNonSuiEdonkey() && GetClientSoft() == SO_EDONKEY & identState != IS_IDENTIFIED)
				theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_NON_SUI_EDONKEY")), PR_NONSUIEDONKEY);
			else if (thePrefs.IsPunishNonSuiEdonkeyHybrid() && GetClientSoft() == SO_EDONKEYHYBRID & identState != IS_IDENTIFIED)
				theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_NON_SUI_EDONKEY_HYBRID")), PR_NONSUIEDONKEYHYBRID);
			else if (thePrefs.IsPunishNonSuiShareaza() && GetClientSoft() == SO_SHAREAZA & identState != IS_IDENTIFIED)
				theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_NON_SUI_SHAREAZA")), PR_NONSUISHAREAZA);
			else if (thePrefs.IsPunishNonSuiLphant() && GetClientSoft() == SO_LPHANT & identState != IS_IDENTIFIED)
				theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_NON_SUI_LPHANT")), PR_NONSUILPHANT);
			else if (thePrefs.IsPunishNonSuiAmule() && GetClientSoft() == SO_AMULE & identState != IS_IDENTIFIED)
				theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_NON_SUI_AMULE")), PR_NONSUIAMULE);
			else if (thePrefs.IsPunishNonSuiEmule() && GetClientSoft() == SO_EMULE & identState != IS_IDENTIFIED)
				theApp.shield->SetPunishment(this, GetResString(_T("PUNISHMENT_REASON_NON_SUI_EMULE")), PR_NONSUIEMULE);
			else {
				//If punished for non SUI before but the identification is successfull now, then cancel punishment.
				if (IsNonSuiPunishment(IsBadClient()))
					theApp.shield->SetPunishment(this, EMPTY, PR_NOTBADCLIENT);

				//Official: If not punished for non SUI already, punish eMule v0.19 and older versions
				if ((IsEmuleClient() || GetClientSoft() < 10) && m_byEmuleVersion <= 0x19)
					fBaseValue *= 0.5f;
			}
		} else if (IsNonSuiPunishment(IsBadClient())) {
			theApp.shield->SetPunishment(this, EMPTY, PR_NOTBADCLIENT);
		}
	
		if (IsBadClient()) {
			if (bPendingUtpSecureIdent && IsNonSuiPunishment(IsBadClient())) {
				// Ignore stale non-SUI punishment until the current uTP session settles its own SecureIdent result.
			} else if (m_uPunishment <= P_UPLOADBAN)
				fBaseValue = 0;
			else
				fBaseValue *= (float)(m_uPunishment - 2) / 10; // "- 2" normalizes the value to 0.1, 0.2, 0.3, etc.
		}
	}

	return (uint32)fBaseValue;
}

const bool CUpDownClient::ProcessExtendedInfo(CSafeMemFile &data, CKnownFile *tempreqfile, bool isUDP)
{
	delete[] m_abyUpPartStatus;
	m_abyUpPartStatus = NULL;
	m_nUpPartCount = 0;
	m_nUpCompleteSourcesCount = 0;
	if (GetExtendedRequestsVersion() == 0)
		return true;

	bool bPartsNeeded = false;
	bool shouldbechecked = isUDP && tempreqfile->IsPartFile()
		&& (((CPartFile*)tempreqfile)->GetStatus() == PS_EMPTY || ((CPartFile*)tempreqfile)->GetStatus() == PS_READY)
		&& !(GetDownloadState() == DS_ONQUEUE && m_reqfile == tempreqfile);

	uint16 nED2KUpPartCount = data.ReadUInt16();
	if (!nED2KUpPartCount) {
		m_nUpPartCount = tempreqfile->GetPartCount();
		if (!m_nUpPartCount)
			return false;
		m_abyUpPartStatus = new uint8[m_nUpPartCount]{};
	} else {
		if (tempreqfile->GetED2KPartCount() != nED2KUpPartCount) {
			//We already checked if we are talking about the same file. So if we get here, something really strange happened!
			m_nUpPartCount = 0;
			return false;
		}
		m_nUpPartCount = tempreqfile->GetPartCount();
		m_abyUpPartStatus = new uint8[m_nUpPartCount];
		for (UINT done = 0; done < m_nUpPartCount;) {
			uint8 toread = data.ReadUInt8();
			for (UINT i = 0; i < 8; ++i) {
				m_abyUpPartStatus[done] = (toread >> i) & 1;
				//We may want to use this for another feature.
					// Use safe range to avoid end beyond file size for the last (partial) chunk
					if (shouldbechecked && bPartsNeeded == false && m_abyUpPartStatus[done]
						&& !((CPartFile*)tempreqfile)->IsCompleteSafe((uint64)done * PARTSIZE, ((uint64)(done + 1) * PARTSIZE) - 1))
					bPartsNeeded = true;
				if (++done >= m_nUpPartCount)
					break;
			}
		}
	}
	if (GetExtendedRequestsVersion() > 1) {
		uint16 nCompleteCountLast = GetUpCompleteSourcesCount();
		uint16 nCompleteCountNew = data.ReadUInt16();
		SetUpCompleteSourcesCount(nCompleteCountNew);
		if (nCompleteCountLast != nCompleteCountNew)
			tempreqfile->UpdatePartsInfo();
	}
	theApp.emuledlg->transferwnd->GetQueueList()->RefreshClient(this);

	//problem is: if a client just began to download a file, we receive an FNF
	//later, if it has some chunks we don't find it via passive source finding because 
	//that works only on TCP-reask but not via UDP
	if (bPartsNeeded) {
		//the client was a NNS but isn't any more
		if (GetDownloadState() == DS_NONEEDEDPARTS && m_reqfile == tempreqfile)
			TrigNextSafeAskForDownload(m_reqfile);
		else if (GetDownloadState() != DS_ONQUEUE) {
			//the client maybe isn't in our downloadqueue.. let's look if we should add the client
			if (((credits && credits->GetMyScoreRatio(GetIP()) >= 1.8f && ((CPartFile*)tempreqfile)->GetSourceCount() < ((CPartFile*)tempreqfile)->GetMaxSources())
				  || ((CPartFile*)tempreqfile)->GetSourceCount() < ((CPartFile*)tempreqfile)->GetMaxSources() * 0.8f + 1)
				&& (theApp.downloadqueue->CheckAndAddKnownSource((CPartFile*)tempreqfile, this, true)))
					AddDebugLogLine(false, _T("->found new source on reask-ping: %s, file: %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (LPCTSTR)EscPercent(tempreqfile->GetFileName()));
		} else if (AddRequestForAnotherFile((CPartFile*)tempreqfile)) {
			theApp.emuledlg->transferwnd->GetDownloadList()->AddSource((CPartFile*)tempreqfile, this, true);
			AddDebugLogLine(false, _T("->found new A4AF source on reask-ping: %s, file: %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (LPCTSTR)EscPercent(tempreqfile->GetFileName()));
		}
	}

	return true;
}

void CUpDownClient::SetUploadFileID(CKnownFile *newreqfile)
{
	CKnownFile *oldreqfile = theApp.downloadqueue->GetFileByID(requpfileid);
	//We use the knownfile list because we may have unshared the file.
	//But we always check the download list first because that person may re-download
	//this file, which will replace the object in the knownfile list if completed.
	if (oldreqfile == NULL)
		oldreqfile = theApp.knownfiles->FindKnownFileByID(requpfileid);
	else {
		// In some _very_ rare cases it is possible that we have different files with the same hash
		// in the downloads list as well as in the shared list (re-downloading an unshared file,
		// then re-sharing it before the first part has been downloaded)
		// to make sure that in no case a deleted client object remains on the list, we do double check
		// TODO: Fix the whole issue properly
		CKnownFile *pCheck = theApp.sharedfiles->GetFileByID(requpfileid);
		if (pCheck != NULL && pCheck != oldreqfile) {
			AddDebugLogLine(DLP_HIGH, false, _T("SetUploadFileID: Client had different files with same hash in download and shared list. Client: %s, DownloadListFile: %s, SharedListFile: %s"),
				(LPCTSTR)EscPercent(DbgGetClientInfo()), (LPCTSTR)EscPercent(oldreqfile->GetFileName()), (LPCTSTR)EscPercent(pCheck->GetFileName()));
			pCheck->RemoveUploadingClient(this);
		}
	}

	if (newreqfile == oldreqfile)
		return;

	// clear old status
	delete[] m_abyUpPartStatus;
	m_abyUpPartStatus = NULL;
	m_nUpPartCount = 0;
	m_nUpCompleteSourcesCount = 0;

	if (newreqfile) {
		newreqfile->AddUploadingClient(this);
		md4cpy(requpfileid, newreqfile->GetFileHash());
	} else
		md4clr(requpfileid);

	if (oldreqfile)
		oldreqfile->RemoveUploadingClient(this);
}

static INT_PTR dbgLastQueueCount = 0;
void CUpDownClient::AddReqBlock(Requested_Block_Struct *reqblock, bool bSignalIOThread)
{
	// do _all_ sanity checks on the requested block here, than put it on the block list for the client
	// UploadDiskIOThread will handle those later on

	if (thePrefs.GetLogNatTraversalEvents() && reqblock != NULL)
		AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] AddReqBlock: Entry, Start=%I64u, End=%I64u, client=%s"), reqblock->StartOffset, reqblock->EndOffset, (LPCTSTR)EscPercent(DbgGetClientInfo()));

	if (reqblock != NULL) {
		if (GetUploadState() != US_UPLOADING) {
			const bool bCanRepairUtpUploadState =
				socket != NULL
				&& socket->HaveUtpLayer()
				&& socket->IsConnected()
				&& CheckHandshakeFinished()
				&& theApp.uploadqueue != NULL
				&& theApp.uploadqueue->IsDownloading(this);
			if (bCanRepairUtpUploadState) {
				if (thePrefs.GetLogNatTraversalEvents()) {
					AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] AddReqBlock: repairing upload-state desync (%s) for %s"),
						DbgGetUploadState(), (LPCTSTR)EscPercent(DbgGetClientInfo()));
				}
				SetUploadState(US_UPLOADING);
			}
		}

		if (GetUploadState() != US_UPLOADING) {
			if (thePrefs.GetLogUlDlEvents())
				AddDebugLogLine(DLP_LOW, false, _T("UploadClient: Client tried to add req block when not in upload slot! Prevented req blocks from being added. %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			if (thePrefs.GetLogNatTraversalEvents())
				AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] AddReqBlock: REJECTED - Not in upload slot (%s) for %s"), DbgGetUploadState(), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			delete reqblock;
			return;
		}

		if (HasCollectionUploadSlot()) {
			CKnownFile *pDownloadingFile = theApp.sharedfiles->GetFileByID(reqblock->FileID);
			if (pDownloadingFile != NULL) {
				if (!CCollection::HasCollectionExtention(pDownloadingFile->GetFileName()) || pDownloadingFile->GetFileSize() > (uint64)MAXPRIORITYCOLL_SIZE) {
					AddDebugLogLine(DLP_HIGH, false, _T("UploadClient: Client tried to add req block for non-collection while having a collection slot! Prevented req blocks from being added. %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()));
					delete reqblock;
					return;
				}
			} else
				ASSERT(0);
		}

		CKnownFile *srcfile = theApp.sharedfiles->GetFileByID(reqblock->FileID);
		if (srcfile == NULL) {
			DebugLogWarning(GetResString(_T("ERR_REQ_FNF")));
			delete reqblock;
			return;
		}

		UploadingToClient_Struct *pUploadingClientStruct = theApp.uploadqueue->GetUploadingClientStructByClient(this);
		if (pUploadingClientStruct == NULL) {
			DebugLogError(_T("AddReqBlock: Uploading client not found in Uploadlist, %s, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (LPCTSTR)EscPercent(srcfile->GetFileName()));
			delete reqblock;
			return;
		}

		if (pUploadingClientStruct->m_bIOError) {
			DebugLogWarning(_T("AddReqBlock: Uploading client has pending IO Error, %s, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (LPCTSTR)EscPercent(srcfile->GetFileName()));
			delete reqblock;
			return;
		}

		if (srcfile->IsPartFile() && !static_cast<CPartFile*>(srcfile)->IsCompleteBDSafe(reqblock->StartOffset, reqblock->EndOffset - 1)) {
			DebugLogWarning(_T("AddReqBlock: %s, %s"), (LPCTSTR)GetResString(_T("ERR_INCOMPLETEBLOCK")), (LPCTSTR)EscPercent(DbgGetClientInfo()), (LPCTSTR)EscPercent(srcfile->GetFileName()));
			delete reqblock;
			return;
		}

		if (reqblock->StartOffset >= reqblock->EndOffset || reqblock->EndOffset > srcfile->GetFileSize()) {
			DebugLogError(_T("AddReqBlock: Invalid Block requests (negative or bytes to read, read after EOF), %s, %s"), (LPCTSTR)EscPercent(DbgGetClientInfo()), (LPCTSTR)EscPercent(srcfile->GetFileName()));
			delete reqblock;
			return;
		}

		if (reqblock->EndOffset - reqblock->StartOffset > EMBLOCKSIZE * 3) {
			DebugLogWarning(_T("AddReqBlock: %s, %s"), (LPCTSTR)GetResString(_T("ERR_LARGEREQBLOCK")), (LPCTSTR)EscPercent(DbgGetClientInfo()), (LPCTSTR)EscPercent(srcfile->GetFileName()));
			delete reqblock;
			return;
		}

		CSingleLock lockBlockLists(&pUploadingClientStruct->m_csBlockListsLock, TRUE);
		if (!lockBlockLists.IsLocked()) {
			ASSERT(0);
			delete reqblock;
			return;
		}

		// uTP stall recovery:
		// - Recovery is armed only on no-progress gap with no buffered payload.
		// - Long re-ask gap is kept for diagnostics to detect delayed re-asks.
		const DWORD dwNow = ::GetTickCount();
		const DWORD kUtpStallReaskGapMs = SEC2MS(20);
		const DWORD kUtpNoProgressGapMs = SEC2MS(5);
		const DWORD dwLastUpRequest = GetLastUpRequest();
		const DWORD dwReaskGapMs = (dwLastUpRequest != 0) ? (dwNow - dwLastUpRequest) : 0;
		const bool bLongReaskGap = (dwLastUpRequest != 0 && dwReaskGapMs >= kUtpStallReaskGapMs);
		const bool bIsUtpConnection = (socket != NULL && socket->HaveUtpLayer());

		uint64 uSentPayloadSnapshot = GetQueueSessionPayloadUp();
		if (socket != NULL)
			uSentPayloadSnapshot += socket->GetSentPayloadSinceLastCall(false);
		const uint64 uUploadAddedSnapshot = GetQueueSessionUploadAdded();
		const uint64 uBufferedPayloadEstimate = (uUploadAddedSnapshot > uSentPayloadSnapshot) ? (uUploadAddedSnapshot - uSentPayloadSnapshot) : 0;
		const bool bNoBufferedPayload = (uBufferedPayloadEstimate == 0);
		// Payload counters can temporarily diverge on compressed transfers.
		// Also treat an empty standard file queue as no buffered payload.
		const bool bNoQueuedFilePackets = (socket == NULL) || !socket->HasQueues(true);
		const bool bNoProgressGap = (dwLastUpRequest != 0 && dwReaskGapMs >= kUtpNoProgressGapMs && (bNoBufferedPayload || bNoQueuedFilePackets));
		// Keep long re-ask gap for diagnostics only.
		// Destructive recovery must be armed strictly by no-progress signal.
		const bool bRecoveryArmed = bIsUtpConnection && bNoProgressGap;
		const bool bStaleUtpBufferAccounting = bIsUtpConnection && bLongReaskGap && bNoQueuedFilePackets && uUploadAddedSnapshot > uSentPayloadSnapshot;

		bool bResetUploadWindow = false;
		bool bNeedFlushSendBlocks = false;
		bool bClearedDoneBlocks = false;
		bool bResetStaleBufferedAccounting = false;
		UINT uRemovedStaleQueued = 0;
		uint64 uUploadAddedBeforeReset = uUploadAddedSnapshot;
		uint64 uUploadAddedAfterReset = uUploadAddedSnapshot;

		if (bStaleUtpBufferAccounting) {
			bResetUploadWindow = true;
			bResetStaleBufferedAccounting = true;
		}

		bool bDuplicateDoneBlock = false;
		for (POSITION pos = pUploadingClientStruct->m_DoneBlocks_list.GetHeadPosition(); pos != NULL;) {
			const Requested_Block_Struct *cur_reqblock = pUploadingClientStruct->m_DoneBlocks_list.GetNext(pos);
			if (reqblock->StartOffset == cur_reqblock->StartOffset
				&& reqblock->EndOffset == cur_reqblock->EndOffset
				&& md4equ(reqblock->FileID, cur_reqblock->FileID))
			{
				bDuplicateDoneBlock = true;
				break;
			}
		}

		if (bDuplicateDoneBlock) {
			const bool bCanRecoverDoneDuplicate = bRecoveryArmed || bStaleUtpBufferAccounting;
			if (!bCanRecoverDoneDuplicate) {
				delete reqblock;
				return;
			}

			while (!pUploadingClientStruct->m_DoneBlocks_list.IsEmpty())
				delete pUploadingClientStruct->m_DoneBlocks_list.RemoveHead();

			bResetUploadWindow = true;
			bNeedFlushSendBlocks = true;
			bClearedDoneBlocks = true;
		}

		for (POSITION pos = pUploadingClientStruct->m_BlockRequests_queue.GetHeadPosition(); pos != NULL;) {
			const Requested_Block_Struct *cur_reqblock = pUploadingClientStruct->m_BlockRequests_queue.GetNext(pos);
			if (reqblock->StartOffset == cur_reqblock->StartOffset
				&& reqblock->EndOffset == cur_reqblock->EndOffset
				&& md4equ(reqblock->FileID, cur_reqblock->FileID))
			{
				// For armed uTP recovery, queued duplicates are considered stale.
				// Replace stale duplicate entries with the fresh request.
				if (bRecoveryArmed) {
					for (POSITION posScan = pUploadingClientStruct->m_BlockRequests_queue.GetHeadPosition(); posScan != NULL;) {
						POSITION posRemove = posScan;
						Requested_Block_Struct *queuedReq = pUploadingClientStruct->m_BlockRequests_queue.GetNext(posScan);
						if (queuedReq->StartOffset == reqblock->StartOffset
							&& queuedReq->EndOffset == reqblock->EndOffset
							&& md4equ(queuedReq->FileID, reqblock->FileID))
						{
							pUploadingClientStruct->m_BlockRequests_queue.RemoveAt(posRemove);
							delete queuedReq;
							++uRemovedStaleQueued;
						}
					}
					if (uRemovedStaleQueued > 0) {
						bResetUploadWindow = true;
						bNeedFlushSendBlocks = true;
						break;
					}
				}

				delete reqblock;
				return;
			}
		}

		pUploadingClientStruct->m_BlockRequests_queue.AddTail(reqblock);
		dbgLastQueueCount = pUploadingClientStruct->m_BlockRequests_queue.GetCount();

		if (bResetUploadWindow) {
			uUploadAddedBeforeReset = GetQueueSessionUploadAdded();
			if (uUploadAddedBeforeReset > uSentPayloadSnapshot)
				SetQueueSessionUploadAdded(uSentPayloadSnapshot);
			uUploadAddedAfterReset = GetQueueSessionUploadAdded();
		}

		lockBlockLists.Unlock();

		const int nStdQueuedBeforeFlush = (socket != NULL) ? (int)socket->DbgGetStdQueueCount() : 0;
		if (bNeedFlushSendBlocks && socket != NULL)
			FlushSendBlocks();

		if (thePrefs.GetLogNatTraversalEvents() && (bClearedDoneBlocks || uRemovedStaleQueued > 0 || bResetUploadWindow)) {
			AddDebugLogLine(DLP_LOW, false,
				_T("[NatTraversal][uTP] Stall recovery: gap=%ums, longGap=%d, noProgressGap=%d, bufferedEstimate=%I64u, staleBuffered=%d, clearedDone=%d, removedQueued=%u, ")
				_T("flushedStd=%d, stdQueuedBefore=%d, addedBefore=%I64u, addedAfter=%I64u for %s"),
				dwReaskGapMs,
				bLongReaskGap ? 1 : 0,
				bNoProgressGap ? 1 : 0,
				uBufferedPayloadEstimate,
				bResetStaleBufferedAccounting ? 1 : 0,
				bClearedDoneBlocks ? 1 : 0,
				uRemovedStaleQueued,
				(bNeedFlushSendBlocks && socket != NULL) ? 1 : 0,
				nStdQueuedBeforeFlush,
				uUploadAddedBeforeReset,
				uUploadAddedAfterReset,
				(LPCTSTR)EscPercent(DbgGetClientInfo()));
		}
	}

	if (bSignalIOThread && theApp.m_pUploadDiskIOThread != NULL)
		theApp.m_pUploadDiskIOThread->WakeUpCall();
}

void CUpDownClient::UpdateUploadingStatisticsData()
{
	const DWORD curTick = ::GetTickCount();

	uint32 sentBytesCompleteFile = 0;
	uint32 sentBytesPartFile = 0;

	CEMSocket *sock = GetFileUploadSocket();
	if (sock) {

		// Extended statistics information based on which client software and which port we sent this data to...
		// This also updates the grand total for sent bytes, etc.  And where this data came from.
		sentBytesCompleteFile = (uint32)sock->GetSentBytesCompleteFileSinceLastCallAndReset();
		sentBytesPartFile = (uint32)sock->GetSentBytesPartFileSinceLastCallAndReset();
		thePrefs.Add2SessionTransferData(GetClientSoft(), GetUserPort(), false, true, sentBytesCompleteFile, (IsFriend() && GetFriendSlot()));
		thePrefs.Add2SessionTransferData(GetClientSoft(), GetUserPort(), true, true, sentBytesPartFile, (IsFriend() && GetFriendSlot()));

		m_nTransferredUp += sentBytesCompleteFile + sentBytesPartFile;
		credits->AddUploaded(sentBytesCompleteFile + sentBytesPartFile, GetIP());

		uint32 sentBytesPayload = sock->GetSentPayloadSinceLastCall(true);
		m_nCurQueueSessionPayloadUp += sentBytesPayload;

		// on some rare cases (namely switching upload files while still data is in the send queue),
		// we count some bytes for the wrong file, but fixing it (and not counting data only based on
		// what was put into the queue and not sent yet) isn't really worth it
		CKnownFile *pCurrentUploadFile = theApp.sharedfiles->GetFileByID(GetUploadFileID());
		if (pCurrentUploadFile != NULL)
			pCurrentUploadFile->statistic.AddTransferred(sentBytesPayload);
		//else
	}

	const uint32 sentBytesFile = sentBytesCompleteFile + sentBytesPartFile;
	if (sentBytesFile > 0 || m_AverageUDR_list.IsEmpty() || curTick >= m_AverageUDR_list.GetTail().timestamp + SEC2MS(1)) {
		// Store how much data we've transferred in this round,
		// to be able to calculate average speed later
		// keep up to date the sum of all values in the list
		TransferredData newitem = {sentBytesFile, curTick};
		m_AverageUDR_list.AddTail(newitem);
		m_nSumForAvgUpDataRate += sentBytesFile;
	}

	// remove old entries from the list and adjust the sum of all values
	while (!m_AverageUDR_list.IsEmpty() && curTick >= m_AverageUDR_list.GetHead().timestamp + SEC2MS(10))
		m_nSumForAvgUpDataRate -= m_AverageUDR_list.RemoveHead().datalen;

	// Calculate average speed for this slot
	if (!m_AverageUDR_list.IsEmpty() && curTick > m_AverageUDR_list.GetHead().timestamp && GetUpStartTimeDelay() > SEC2MS(2))
		m_nUpDatarate = (UINT)(SEC2MS(m_nSumForAvgUpDataRate) / (curTick - m_AverageUDR_list.GetHead().timestamp));
	else
		m_nUpDatarate = 0; // not enough data to calculate trustworthy speed

	theApp.emuledlg->transferwnd->GetUploadList()->RefreshClient(this);
	theApp.emuledlg->transferwnd->GetClientList()->RefreshClient(this);
}

void CUpDownClient::SendOutOfPartReqsAndAddToWaitingQueue()
{
	//OP_OUTOFPARTREQS will tell the downloading client to go back to OnQueue.
	//The main reason for this is that if we put the client back on queue and it goes
	//back to the upload before the socket times out... We get a situation where the
	//downloader thinks it already sent the requested blocks and the uploader thinks
	//the downloader didn't send any block requests. Then the connection times out.
	//I did some tests with eDonkey also and it seems to work well with them also.
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_OutOfPartReqs", this);
	Packet *pPacket = new Packet(OP_OUTOFPARTREQS, 0);
	theStats.AddUpDataOverheadFileRequest(pPacket->size);
	SendPacket(pPacket);
	m_fSentOutOfPartReqs = 1;
	theApp.uploadqueue->AddClientToQueue(this, true);
}

/*
 * See description for CEMSocket::TruncateQueues().
 */
void CUpDownClient::FlushSendBlocks() // call this when you stop upload, or the socket might be not able to send
{
	if (socket) //socket may be NULL...
		socket->TruncateQueues();
}

void CUpDownClient::SendHashsetPacket(const uchar *pData, uint32 nSize, bool bFileIdentifiers)
{
	Packet *packet;
	CSafeMemFile fileResponse(1024);
	if (bFileIdentifiers) {
		CSafeMemFile data(pData, nSize);
		CFileIdentifierSA fileIdent;
		if (!fileIdent.ReadIdentifier(data))
			throw _T("Bad FileIdentifier (OP_HASHSETREQUEST2)");
		CKnownFile *file = theApp.sharedfiles->GetFileByIdentifier(fileIdent, false);
		if (file == NULL) {
			CheckFailedFileIdReqs(fileIdent.GetMD4Hash());
			throw GetResString(_T("ERR_REQ_FNF")) + _T(" (SendHashsetPacket2)");
		}
		uint8 byOptions = data.ReadUInt8();
		bool bMD4 = (byOptions & 0x01) > 0;
		bool bAICH = (byOptions & 0x02) > 0;
		if (!bMD4 && !bAICH) {
			DebugLogWarning(_T("Client sent HashSet request with none or unknown HashSet type requested (%u) - file: %s, client %s")
				, byOptions, (LPCTSTR)EscPercent(file->GetFileName()), (LPCTSTR)EscPercent(DbgGetClientInfo()));
			return;
		}
		const CFileIdentifier &fileid = file->GetFileIdentifier();
		fileid.WriteIdentifier(fileResponse);
		// even if we don't happen to have an AICH hashset yet for some reason we send a proper (possibly empty) response
		fileid.WriteHashSetsToPacket(fileResponse, bMD4, bAICH);
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP_HashSetAnswer", this, fileid.GetMD4Hash());
		packet = new Packet(fileResponse, OP_EMULEPROT, OP_HASHSETANSWER2);
	} else {
		if (nSize != 16) {
			ASSERT(0);
			return;
		}
		CKnownFile *file = theApp.sharedfiles->GetFileByID(pData);
		if (!file) {
			CheckFailedFileIdReqs(pData);
			throw GetResString(_T("ERR_REQ_FNF")) + _T(" (SendHashsetPacket)");
		}
		file->GetFileIdentifier().WriteMD4HashsetToFile(fileResponse);
		if (thePrefs.GetDebugClientTCPLevel() > 0)
			DebugSend("OP_HashSetAnswer", this, pData);
		packet = new Packet(fileResponse, OP_EDONKEYPROT, OP_HASHSETANSWER);
	}
	theStats.AddUpDataOverheadFileRequest(packet->size);
	SendPacket(packet);
}

void CUpDownClient::SendRankingInfo()
{
	if (!ExtProtocolAvailable())
		return;
	UINT nRank = theApp.uploadqueue->GetWaitingPosition(this);
	if (!nRank)
		return;
	Packet *packet = new Packet(OP_QUEUERANKING, 12, OP_EMULEPROT);
	PokeUInt16(packet->pBuffer, (uint16)nRank);
	memset(packet->pBuffer + 2, 0, 10);
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_QueueRank", this);
	theStats.AddUpDataOverheadFileRequest(packet->size);
	SendPacket(packet);
}

void CUpDownClient::SendCommentInfo(/*const */CKnownFile *file)
{
	if (!m_bCommentDirty || file == NULL || !ExtProtocolAvailable() || m_byAcceptCommentVer < 1)
		return;
	m_bCommentDirty = false;

	UINT rating = file->GetFileRating();
	const CString &desc(file->GetFileComment());
	if (rating == 0 && desc.IsEmpty())
		return;

	CSafeMemFile data(256);
	data.WriteUInt8((uint8)rating);
	data.WriteLongString(desc, GetUnicodeSupport());
	if (thePrefs.GetDebugClientTCPLevel() > 0)
		DebugSend("OP_FileDesc", this, file->GetFileHash());
	Packet *packet = new Packet(data, OP_EMULEPROT);
	packet->opcode = OP_FILEDESC;
	theStats.AddUpDataOverheadFileRequest(packet->size);
	SendPacket(packet);
}

void CUpDownClient::AddRequestCount(const uchar *fileid)
{
	const DWORD curTick = ::GetTickCount();

	for (POSITION pos = m_RequestedFiles_list.GetHeadPosition(); pos != NULL;) {
		Requested_File_Struct *cur_struct = m_RequestedFiles_list.GetNext(pos);
		if (md4equ(cur_struct->fileid, fileid)) {
			// NAT-T/uTP connections naturally involve multiple retry attempts due to:
			// - NAT hole punching process requiring multiple packets
			// - uTP packet loss and retransmission mechanisms
			// - Connection establishment through intermediary peers
			// Therefore, bypass request time limit check entirely for NAT-T/uTP connections
			bool bHasUtpLayer = (socket && socket->HaveUtpLayer());
			bool bHasKadPort = (GetKadPort() != 0);
			bool bIsNatTraversalConnection = bHasUtpLayer || bHasKadPort;
			
			if (curTick < cur_struct->lastasked + MIN_REQUESTTIME && !GetFriendSlot() && !bIsNatTraversalConnection) {
				cur_struct->badrequests += static_cast<uint8>(GetDownloadState() != DS_DOWNLOADING);
				if (cur_struct->badrequests == BADCLIENTBAN)
					Ban();
			} else
				cur_struct->badrequests -= static_cast<uint8>(cur_struct->badrequests > 0);

			cur_struct->lastasked = curTick;
			return;
		}
	}
	Requested_File_Struct *new_struct = new Requested_File_Struct;
	md4cpy(new_struct->fileid, fileid);
	new_struct->lastasked = curTick;
	new_struct->badrequests = 0;
	m_RequestedFiles_list.AddHead(new_struct);
}

void  CUpDownClient::UnBan()
{
	if (GetConnectIP().IsNull() && isnulmd4(GetUserHash()))
		return;

	uiULAskingCounter = 0;
	tLastSeen = time(NULL);

	if (m_uBadClientCategory > PR_NOTBADCLIENT) {
		m_uPunishment = P_NOPUNISHMENT;
		m_tPunishmentStartTime = 0;
		AddProtectionLogLine(false, _T("<Ban Cancellation - %u> - Client %s"), m_uBadClientCategory, (LPCTSTR)EscPercent(DbgGetClientInfo()));
		m_uBadClientCategory = PR_NOTBADCLIENT;
		m_strPunishmentReason.Empty();
		m_strPunishmentMessage.Empty();
	}
	
	if (!isnulmd4(GetUserHash())) // Remove user hash from banned list if it exist there
		theApp.clientlist->RemoveBannedClient(md4str(GetUserHash()));

	theApp.clientlist->AddTrackClient(this);
	theApp.clientlist->RemoveBannedClient(GetConnectIP().ToStringC());
	SetUploadState(US_NONE);
	ClearWaitStartTime();
	theApp.emuledlg->transferwnd->ShowQueueCount(theApp.uploadqueue->GetWaitingUserCount());
	for (POSITION pos = m_RequestedFiles_list.GetHeadPosition(); pos != NULL;) {
		Requested_File_Struct *cur_struct = m_RequestedFiles_list.GetNext(pos);
		cur_struct->badrequests = 0;
		cur_struct->lastasked = 0;
	}
}

const void CUpDownClient::Ban(const CString& strReason, const uint8 uBadClientCategory, const uint8 uPunishment, const time_t tBanStartTime)
{
	m_strPunishmentReason = strReason.IsEmpty() ? GetResString(_T("PUNISHMENT_REASON_AGGRESSIVE_BEHAVIOUR")) : strReason;

	if (uPunishment == P_USERHASHBAN && isnulmd4(GetUserHash()))
		return; //Null user hashes cannot be banned.
	else if (GetConnectIP().IsNull() && isnulmd4(GetUserHash()))
		return; //Null user hashes or IP addresses cannot be banned.
	else if (thePrefs.GetLogBannedClients() && !IsBanned() || uPunishment < m_uPunishment) // Only log if this is not a recurring ban with same level.
		AddProtectionLogLine(false, _T("<Ban> [%s] - %s"), m_strPunishmentReason, (LPCTSTR)EscPercent((DbgGetClientInfo())));

	m_uPunishment = uPunishment;
	m_uBadClientCategory = uBadClientCategory;
	m_tPunishmentStartTime = time(NULL);
	m_strPunishmentMessage.Empty();

	SetChatState(MS_NONE);
	theApp.clientlist->AddTrackClient(this, tBanStartTime);
	theApp.clientlist->AddBannedClient(md4str(GetUserHash()), tBanStartTime); // Add user hash to m_bannedList list.
	if (uPunishment == P_IPUSERHASHBAN)
		theApp.clientlist->AddBannedClient(GetConnectIP().ToStringC());

	SetUploadState(US_BANNED);
	theApp.emuledlg->transferwnd->ShowQueueCount(theApp.uploadqueue->GetWaitingUserCount());
	theApp.emuledlg->transferwnd->GetQueueList()->RefreshClient(this);
	if (socket != NULL && socket->IsConnected())
		socket->ShutDown(CAsyncSocket::receives); // let the socket timeout, since we don't want to risk to delete the client right now. This isn't actually perfect, could be changed later
}

DWORD CUpDownClient::GetWaitStartTime() const
{
	if (credits == NULL) {
		// Credits may not be initialized yet during early uTP connection stages (before Hello exchange)
		return 0;
	}
	DWORD dwResult = credits->GetSecureWaitStartTime(GetIP());
	if (dwResult > m_dwUploadTime && IsDownloading()) {
		//this happens only if two clients with invalid securehash are in the queue - if at all
		dwResult = m_dwUploadTime - 1;

		if (thePrefs.GetVerbose())
			DEBUG_ONLY(AddDebugLogLine(false, _T("Warning: CUpDownClient::GetWaitStartTime() waittime Collision (%s)"), (LPCTSTR)EscPercent(GetUserName())));
	}
	return dwResult;
}

void CUpDownClient::SetWaitStartTime()
{
	if (credits != NULL)
		credits->SetSecWaitStartTime(GetIP());
}

void CUpDownClient::ClearWaitStartTime()
{
	if (credits != NULL)
		credits->ClearWaitStartTime();
}

bool CUpDownClient::GetFriendSlot() const
{
	if (credits && theApp.clientcredits->CryptoAvailable())
		switch (credits->GetCurrentIdentState(GetIP())) {
		case IS_IDFAILED:
		case IS_IDNEEDED:
		case IS_IDBADGUY:
			return false;
		}

	return m_bFriendSlot;
}

CEMSocket* CUpDownClient::GetFileUploadSocket(bool bLog)
{
	if (bLog && thePrefs.GetVerbose())
		AddDebugLogLine(false, _T("%s got normal socket."), (LPCTSTR)EscPercent(DbgGetClientInfo()));
	return socket;
}

void CUpDownClient::SetCollectionUploadSlot(bool bValue)
{
	ASSERT(!IsDownloading() || bValue == m_bCollectionUploadSlot);
	m_bCollectionUploadSlot = bValue;
}

// NAT-T keep-alive for uploader side
// This function is called from UploadQueue::Process() to maintain NAT mapping
// for uTP connections. Without this, the uploader's NAT mapping may timeout
// during idle periods, causing downloader's re-ask packets to fail.
void CUpDownClient::CheckNatTraversalKeepAlive()
{
	// 1. Is Uploading?
	if (GetUploadState() != US_UPLOADING) return;

	// 2. Is this a NAT-T/uTP connection?
	if (!socket || !socket->HaveUtpLayer()) return;

	// 3. Send keep-alive every 10 seconds of inactivity
	DWORD dwNow = ::GetTickCount();
	if (dwNow - m_dwLastNatKeepAliveSent > SEC2MS(10)) {
		// Trigger uTP keep-alive - this sends a minimal ack or ping to keep NAT mapping alive
		if (thePrefs.GetLogNatTraversalEvents())
			AddDebugLogLine(DLP_LOW, false, _T("[NatTraversal] Uploader keep-alive: Sending NAT ping for %s"),
				(LPCTSTR)EscPercent(DbgGetClientInfo()));

		// Send a keep-alive by triggering socket activity
		// This will cause uTP to send an ACK or similar minimal packet
		socket->TriggerEvent(FD_WRITE);
		
		m_dwLastNatKeepAliveSent = dwNow;
	}
}
