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
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "StdAfx.h"
#include "aichsyncthread.h"
#include "shahashset.h"
#include "safefile.h"
#include "knownfile.h"
#include "sha.h"
#include "emule.h"
#include "emuledlg.h"
#include "sharedfilelist.h"
#include "knownfilelist.h"
#include "sharedfileswnd.h"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


/////////////////////////////////////////////////////////////////////////////////////////
///CAICHSyncThread
IMPLEMENT_DYNCREATE(CAICHSyncThread, CWinThread)

BOOL CAICHSyncThread::InitInstance()
{
	DbgSetThreadName("AICHSyncThread");
	return TRUE;
}

bool CAICHSyncThread::RunStartupSync(bool bBuildMissingHashsets, INT_PTR *piPendingHashCount, UINT uMaxHashsetsToBuild, INT_PTR *piProcessedHashCount, std::vector<BYTE> *pvecDeferredAICHFileHashes)
{
	CAICHSyncThread syncThread;
	syncThread.m_bBuildMissingHashsets = bBuildMissingHashsets;
	syncThread.m_uMaxHashsetsToBuild = uMaxHashsetsToBuild;
	syncThread.m_pvecDeferredAICHFileHashes = pvecDeferredAICHFileHashes;
	const bool bSuccess = syncThread.Run() == 0;
	if (piPendingHashCount != NULL)
		*piPendingHashCount = syncThread.m_iPendingHashCount;
	if (piProcessedHashCount != NULL)
		*piProcessedHashCount = syncThread.m_iProcessedHashCount;
	return bSuccess;
}

void CAICHSyncThread::AppendAICHFileHash(const CKnownFile *pFile, std::vector<BYTE>& vecFileHashes)
{
	if (pFile == NULL || pFile->GetFileHash() == NULL)
		return;

	const BYTE *pucHash = reinterpret_cast<const BYTE*>(pFile->GetFileHash());
	vecFileHashes.insert(vecFileHashes.end(), pucHash, pucHash + MDX_DIGEST_SIZE);
}

void CAICHSyncThread::QueueDeferredAICHFileHash(const CKnownFile *pFile) const
{
	if (m_pvecDeferredAICHFileHashes == NULL)
		return;
	AppendAICHFileHash(pFile, *m_pvecDeferredAICHFileHashes);
}

void CAICHSyncThread::QueueMissingAICHFileHash(const CKnownFile *pFile)
{
	AppendAICHFileHash(pFile, m_vecToHashFileHashes);
}

bool CAICHSyncThread::BuildStartupDeferredAICHHashset(const uchar *pucFileHash)
{
	if (pucFileHash == NULL || theApp.IsClosing() || thePrefs.m_bDisableAICHCreation)
		return false;

	CString strFilePath;
	CString strFileName;
	EMFileSize nFileSize(0ull);
	bool bHasAICHHash = false;
	bool bNeedsPartHashSet = false;
	CAICHHash aichHash;
	{
		CSingleLock sharelock(&theApp.sharedfiles->m_mutWriteList, TRUE);
		CKnownFile *pFile = theApp.sharedfiles->GetFileByID(pucFileHash);
		if (pFile == NULL || pFile->IsPartFile() || !theApp.knownfiles->IsKnownFile(pFile))
			return true;

		strFilePath = pFile->GetFilePath();
		strFileName = pFile->GetFileName();
		nFileSize = pFile->GetFileSize();
		CFileIdentifier &fileid = pFile->GetFileIdentifier();
		bHasAICHHash = fileid.HasAICHHash();
		if (bHasAICHHash) {
			aichHash = fileid.GetAICHHash();
			bNeedsPartHashSet = !fileid.HasExpectedAICHHashCount();
		}
		if (bHasAICHHash && !bNeedsPartHashSet)
			return true;
	}

	if (bHasAICHHash && bNeedsPartHashSet) {
		CKnownFile snapshotFile;
		snapshotFile.SetFileName(strFileName);
		snapshotFile.SetFileSize(nFileSize);
		CAICHRecoveryHashSet tempHashSet(&snapshotFile, nFileSize);
		tempHashSet.SetMasterHash(aichHash, AICH_HASHSETCOMPLETE);
		if (!tempHashSet.LoadHashSet()) {
			ASSERT(0);
			DebugLogError(_T("Failed to load full AICH recovery Hashset - known2.met might be corrupt. Unable to create AICH Part Hashset - %s"), (LPCTSTR)EscPercent(strFileName));
			return true;
		}

		CSingleLock sharelock(&theApp.sharedfiles->m_mutWriteList, TRUE);
		CKnownFile *pFile = theApp.sharedfiles->GetFileByID(pucFileHash);
		if (pFile == NULL || pFile->IsPartFile() || !theApp.knownfiles->IsKnownFile(pFile))
			return true;
		if (pFile->GetFileSize() != nFileSize || pFile->GetFilePath().CompareNoCase(strFilePath) != 0)
			return true;

		CFileIdentifier &fileid = pFile->GetFileIdentifier();
		if (!fileid.HasAICHHash() || fileid.GetAICHHash() != aichHash || fileid.HasExpectedAICHHashCount())
			return true;
		if (!fileid.SetAICHHashSet(tempHashSet)) {
			DebugLogError(_T("Failed to create AICH Part Hashset out of full AICH recovery Hashset - %s"), (LPCTSTR)EscPercent(pFile->GetFileName()));
			ASSERT(0);
		}
		pFile->SetAICHRecoverHashSetAvailable(true);
		return true;
	}

	while (theApp.sharedfiles->GetHashingCount() != 0) {
		if (theApp.IsClosing())
			return false;
		::Sleep(100);
	}

	CSingleLock hashlock(&theApp.hashing_mut, TRUE);
	if (theApp.IsClosing())
		return false;

	CAICHRecoveryHashSet calculatedHashSet(NULL, nFileSize);
	theApp.QueueLogLine(false, GetResString(_T("AICH_CALCFILE")), (LPCTSTR)strFileName);
	if (!CKnownFile::CreateAICHHashSetFromFile(strFilePath, nFileSize, calculatedHashSet)) {
		theApp.QueueDebugLogLine(false, _T("Failed to create AICH Hashset while sync. for file %s"), (LPCTSTR)strFileName);
		return true;
	}
	hashlock.Unlock();

	bool bApplied = false;
	{
		CSingleLock sharelock(&theApp.sharedfiles->m_mutWriteList, TRUE);
		CKnownFile *pCurFile = theApp.sharedfiles->GetFileByID(pucFileHash);
		if (pCurFile == NULL || pCurFile->IsPartFile() || !theApp.knownfiles->IsKnownFile(pCurFile))
			return true;
		if (pCurFile->GetFileSize() != nFileSize || pCurFile->GetFilePath().CompareNoCase(strFilePath) != 0)
			return true;

		calculatedHashSet.SetOwner(pCurFile);
		CFileIdentifier &fileid = pCurFile->GetFileIdentifier();
		const CAICHHash *pHash = (fileid.HasAICHHash() && fileid.GetAICHHash() != calculatedHashSet.GetMasterHash())
			? &fileid.GetAICHHash() : NULL;
		theApp.knownfiles->AICHHashChanged(pHash, calculatedHashSet.GetMasterHash(), pCurFile);
		fileid.SetAICHHash(calculatedHashSet.GetMasterHash());
		if (!fileid.SetAICHHashSet(calculatedHashSet)) {
			ASSERT(0);
			theApp.QueueDebugLogLine(false, _T("Failed to create AICH PartHashSet out of RecoveryHashSet while sync. for file %s"), (LPCTSTR)pCurFile->GetFileName());
		}
		pCurFile->SetAICHRecoverHashSetAvailable(true);
		bApplied = true;
	}

	if (bApplied && !calculatedHashSet.SaveHashSetForFileSize(nFileSize))
		LogError(LOG_STATUSBAR, GetResString(_T("SAVEACFAILED")));
	return true;
}

int CAICHSyncThread::Run()
{
	// Note: m_bDisableAICHCreation should only be used for testing purposes. So it can't be set on GUI.
	if (theApp.IsClosing() || thePrefs.m_bDisableAICHCreation)
		return 0;

	const bool bLimitHashsetBuild = m_bBuildMissingHashsets && m_uMaxHashsetsToBuild > 0;

	// we collect all masterhashes which we find in the known2.met and store them in a list
	CArray<CAICHHash> aKnown2Hashes;
	CArray<ULONGLONG> aKnown2HashesFilePos;

	CSafeFile file;

	// we need to keep a lock on this file while the thread is running
	CSingleLock lockKnown2Met(&CAICHRecoveryHashSet::m_mutKnown2File, TRUE);
	bool bJustCreated = ConvertKnown2ToKnown264(file);

	if (!bJustCreated) {
		if (!CFileOpen(file
			, thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + KNOWN2_MET_FILENAME
			, CFile::modeReadWrite | CFile::modeCreate | CFile::modeNoTruncate | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyNone
			, _T("Failed to load ") KNOWN2_MET_FILENAME _T(" file")))
		{
		return 0;
		}
	}
	ULONGLONG nLastVerifiedPos = 0;
	try {
		if (file.GetLength() >= 1) {
			uint8 header = file.ReadUInt8();
			if (header != KNOWN2_MET_VERSION)
				AfxThrowFileException(CFileException::endOfFile, 0, file.GetFileName());

			ULONGLONG nExistingSize = file.GetLength();
			while (file.GetPosition() < nExistingSize) {
				aKnown2HashesFilePos.Add(file.GetPosition());
				aKnown2Hashes.Add(CAICHHash(file));
				uint32 nHashCount = file.ReadUInt32();
				if (file.GetPosition() + nHashCount * (ULONGLONG)CAICHHash::GetHashSize() > nExistingSize)
					AfxThrowFileException(CFileException::endOfFile, 0, file.GetFileName());

				// skip the rest of this hashset
				file.Seek(nHashCount * (LONGLONG)CAICHHash::GetHashSize(), CFile::current);
				nLastVerifiedPos = file.GetPosition();
			}
		} else
			file.WriteUInt8(KNOWN2_MET_VERSION);
	} catch (CFileException *ex) {
		if (ex->m_cause == CFileException::endOfFile) {
			LogError(LOG_STATUSBAR, GetResString(_T("ERR_MET_BAD")), KNOWN2_MET_FILENAME);
			// truncate the file to the last verified valid position
			try {
				file.SetLength(nLastVerifiedPos);
				if (file.GetLength() == 0) {
					file.SeekToBegin();
					file.WriteUInt8(KNOWN2_MET_VERSION);
				}
			} catch (CFileException *ex2) {
				ex2->Delete();
			}
		} else
			LogError(LOG_STATUSBAR, GetResString(_T("ERR_SERVERMET_UNKNOWN")), (LPCTSTR)EscPercent(CExceptionStr(*ex)));
		ex->Delete();
		return 0;
	}

	CMap<CAICHHash, const CAICHHash&, INT_PTR, INT_PTR> mapKnown2Hashes;
	for (INT_PTR i = 0; i < aKnown2Hashes.GetCount(); ++i)
		mapKnown2Hashes[aKnown2Hashes[i]] = i;

	// now we check that all files which are in the shared file list have a corresponding hash in the out list
	// those who don't are added to the hashing list
	CList<CAICHHash> liUsedHashes;
	CMap<CAICHHash, const CAICHHash&, bool, bool> mapUsedHashes;
	bool bDbgMsgCreatingPartHashes = true;

	CSingleLock sharelock(&theApp.sharedfiles->m_mutWriteList, TRUE);
	for (POSITION pos = BEFORE_START_POSITION; pos != NULL;) {
		if (theApp.IsClosing()) // in case of shutdown while still hashing
			return 0;
		CKnownFile *pFile = theApp.sharedfiles->GetFileNext(pos);
		if (pFile != NULL && !pFile->IsPartFile()) {
			CFileIdentifier &fileid = pFile->GetFileIdentifier();
			if (fileid.HasAICHHash()) {
				INT_PTR iKnown2Hash = -1;
				if (mapKnown2Hashes.Lookup(fileid.GetAICHHash(), iKnown2Hash) && iKnown2Hash >= 0 && iKnown2Hash < aKnown2Hashes.GetCount()) {
					const CAICHHash& aichHash = aKnown2Hashes[iKnown2Hash];
					liUsedHashes.AddTail(CAICHHash(aichHash));
					mapUsedHashes[aichHash] = true;
					pFile->SetAICHRecoverHashSetAvailable(true);
					// Has the file the proper AICH Part hashset? If not, probably upgrading, create it
					if (!fileid.HasExpectedAICHHashCount()) {
						if (!m_bBuildMissingHashsets || (bLimitHashsetBuild && m_iProcessedHashCount >= static_cast<INT_PTR>(m_uMaxHashsetsToBuild))) {
							++m_iDeferredPartHashSetCount;
							QueueDeferredAICHFileHash(pFile);
							continue;
						}
						++m_iProcessedHashCount;
						if (bDbgMsgCreatingPartHashes) {
							bDbgMsgCreatingPartHashes = false;
							DebugLogWarning(_T("Missing AICH Part Hashsets for known files - maybe upgrading from earlier version. Creating them out of full AICH recovery Hashsets, shouldn't take too long"));
						}
						CAICHRecoveryHashSet tempHashSet(pFile, pFile->GetFileSize());
						tempHashSet.SetMasterHash(fileid.GetAICHHash(), AICH_HASHSETCOMPLETE);
						if (!tempHashSet.LoadHashSet()) {
							DebugLogError(_T("Failed to load full AICH recovery Hashset - known2.met might be corrupt. Unable to create AICH Part Hashset - %s"), (LPCTSTR)EscPercent(pFile->GetFileName()));
						} else {
							if (!fileid.SetAICHHashSet(tempHashSet))
								DebugLogError(_T("Failed to create AICH Part Hashset out of full AICH recovery Hashset - %s"), (LPCTSTR)EscPercent(pFile->GetFileName()));
							else if (!fileid.HasExpectedAICHHashCount())
								DebugLogError(_T("Created AICH Part Hashset has unexpected part count - %s"), (LPCTSTR)EscPercent(pFile->GetFileName()));
						}
					}
					continue;
				}
			}
			pFile->SetAICHRecoverHashSetAvailable(false);
			QueueMissingAICHFileHash(pFile);
			if (!m_bBuildMissingHashsets || bLimitHashsetBuild)
				QueueDeferredAICHFileHash(pFile);
		}
	}
	sharelock.Unlock();

	// remove all unused AICH hashsets from known2.met
	if (liUsedHashes.GetCount() != aKnown2Hashes.GetCount()
		&& (!thePrefs.IsRememberingDownloadedFiles() ||
			thePrefs.DoPartiallyPurgeOldKnownFiles() ||
			thePrefs.GetCompletlyPurgeOldKnownFiles() ||
			thePrefs.GetRemoveAichImmediately()
			)
		)
	{
		file.SeekToBegin();
		try {
			uint8 header = file.ReadUInt8();
			if (header != KNOWN2_MET_VERSION)
				AfxThrowFileException(CFileException::endOfFile, 0, file.GetFileName());

			ULONGLONG nExistingSize = file.GetLength();
			ULONGLONG posWritePos = file.GetPosition();
			ULONGLONG posReadPos = posWritePos;
			uint32 nPurgeCount = 0;
			uint32 nPurgeBecauseOld = 0;
			uint32 nPurgeDups = 0;
			static const CAICHHash empty; //zero AICH hash
			ULONGLONG nCurrentHashsetPos;
			while ((nCurrentHashsetPos = file.GetPosition()) < nExistingSize) {
				ULONGLONG posTmp = 0; //position of an old duplicate hash
				CAICHHash aichHash(file);
				uint32 nHashCount = file.ReadUInt32();
				if (file.GetPosition() + nHashCount * (ULONGLONG)CAICHHash::GetHashSize() > nExistingSize)
					AfxThrowFileException(CFileException::endOfFile, 0, file.GetFileName());

				bool bUsedHash = false;
				const bool bUsedAICHHash = mapUsedHashes.Lookup(aichHash, bUsedHash) && bUsedHash;
				if (aichHash == empty || (!thePrefs.IsRememberingDownloadedFiles() && !bUsedAICHHash)) {
					// unused hashset skip the rest of this hashset
					file.Seek(nHashCount * (LONGLONG)CAICHHash::GetHashSize(), CFile::current);
					++nPurgeCount;
				} else if (thePrefs.IsRememberingDownloadedFiles() && theApp.knownfiles->ShouldPurgeAICHHashset(aichHash)) {
					ASSERT(thePrefs.DoPartiallyPurgeOldKnownFiles() || thePrefs.GetRemoveAichImmediately());
					file.Seek(nHashCount * (LONGLONG)CAICHHash::GetHashSize(), CFile::current);
					++nPurgeCount;
					++nPurgeBecauseOld;
				} else if (nPurgeCount == 0) {
					// used Hashset, but it does not need to be moved as nothing changed yet
					file.Seek(nHashCount * (LONGLONG)CAICHHash::GetHashSize(), CFile::current);
					posReadPos = posWritePos = file.GetPosition();
					posTmp = CAICHRecoveryHashSet::AddStoredAICHHash(aichHash, nCurrentHashsetPos);
				} else {
					// used Hashset, move position in file
					BYTE *buffer = new BYTE[nHashCount * (size_t)CAICHHash::GetHashSize()];
					file.Read(buffer, nHashCount * CAICHHash::GetHashSize());
					posReadPos = file.GetPosition();
					file.Seek(posWritePos, CFile::begin);
					file.Write(aichHash.GetRawHashC(), CAICHHash::GetHashSize());
					file.WriteUInt32(nHashCount);
					file.Write(buffer, nHashCount * CAICHHash::GetHashSize());
					delete[] buffer;
					posTmp = CAICHRecoveryHashSet::AddStoredAICHHash(aichHash, posWritePos);

					posWritePos = file.GetPosition();
					file.Seek(posReadPos, CFile::begin);
				}
				if (posTmp) {
					file.Seek(posTmp, CFile::begin);
					file.Write(empty.GetRawHashC(), CAICHHash::GetHashSize()); //mark this for purging
					file.Seek(posReadPos, CFile::begin);
					++nPurgeDups;
				}
			}
			posReadPos = file.GetPosition();
			file.SetLength(posWritePos);
			file.Flush();
			file.Close();
			theApp.QueueDebugLogLine(false, _T("Cleaned up known2.met, removed %u hashsets and purged %u hashsets of old known files (%s)")
				, nPurgeCount - nPurgeBecauseOld, nPurgeBecauseOld, (LPCTSTR)CastItoXBytes(posReadPos - posWritePos));
			if (nPurgeDups)
				theApp.QueueDebugLogLine(false, _T("Marked %u duplicate hashsets for purging"), nPurgeDups);
		} catch (CFileException *ex) {
			if (ex->m_cause == CFileException::endOfFile) {
				// we just parsed this file some ms ago, should never happen here
				ASSERT(0);
			} else
				LogError(LOG_STATUSBAR, GetResString(_T("ERR_SERVERMET_UNKNOWN")), (LPCTSTR)EscPercent(CExceptionStr(*ex)));
			ex->Delete();
			return 0;
		}
	} else {
		// remember (/index) all hashes which are stored in the file for faster checking later on
		for (INT_PTR i = 0; i < aKnown2Hashes.GetCount() && !theApp.IsClosing(); ++i)
			CAICHRecoveryHashSet::AddStoredAICHHash(aKnown2Hashes[i], aKnown2HashesFilePos[i]);
	}

#ifdef _DEBUG
	if (m_bBuildMissingHashsets && !bLimitHashsetBuild) {
		for (POSITION pos = liUsedHashes.GetHeadPosition(); pos != NULL && !theApp.IsClosing();) {
			CKnownFile *pFile = theApp.sharedfiles->GetFileByAICH(liUsedHashes.GetNext(pos));
			if (pFile == NULL)
				continue;
			CAICHRecoveryHashSet *pTempHashSet = new CAICHRecoveryHashSet(pFile);
			pTempHashSet->SetFileSize(pFile->GetFileSize());
			pTempHashSet->SetMasterHash(pFile->GetFileIdentifier().GetAICHHash(), AICH_HASHSETCOMPLETE);
			(void)pTempHashSet->LoadHashSet();
			delete pTempHashSet;
		}
	}
#endif

	lockKnown2Met.Unlock();
	if (!m_bBuildMissingHashsets) {
		const INT_PTR iFilesToHash = static_cast<INT_PTR>(m_vecToHashFileHashes.size() / MDX_DIGEST_SIZE);
		m_iPendingHashCount = iFilesToHash + m_iDeferredPartHashSetCount;
		if (m_iPendingHashCount > 0)
			theApp.QueueDebugLogLine(false, _T("Deferred AICH hashset creation until after startup. files=%Id partsets=%Id"), iFilesToHash, m_iDeferredPartHashSetCount);
		return 0;
	}
	m_iPendingHashCount = m_iDeferredPartHashSetCount;
	// warn the user if he just upgraded
	const INT_PTR iFilesToHash = static_cast<INT_PTR>(m_vecToHashFileHashes.size() / MDX_DIGEST_SIZE);
	if (thePrefs.IsFirstStart() && iFilesToHash > 0 && !bJustCreated)
		LogWarning(GetResString(_T("AICH_WARNUSER")));

	if (iFilesToHash > 0) {
		theApp.QueueLogLine(true, GetResString(_T("AICH_SYNCTOTAL")), iFilesToHash);
		theApp.emuledlg->sharedfileswnd->sharedfilesctrl.SetAICHHashing(iFilesToHash);
		theApp.sharedfiles->NotifyShowFilesCount();
		// first let all normal hashing be done before starting out sync hashing
		CSingleLock sLock1(&theApp.hashing_mut); // only one file hash at a time
		while (theApp.sharedfiles->GetHashingCount() != 0) {
			if (theApp.IsClosing())
				return 0;
			::Sleep(100);
		}
		sLock1.Lock();
		INT_PTR cRemainingToHash = iFilesToHash;
		for (INT_PTR iHash = 0; iHash < iFilesToHash; ++iHash) {
			if (theApp.IsClosing()) // in case of shutdown while still hashing
				return 0;
			if (bLimitHashsetBuild && m_iProcessedHashCount >= static_cast<INT_PTR>(m_uMaxHashsetsToBuild)) {
				m_iPendingHashCount = cRemainingToHash + m_iDeferredPartHashSetCount;
				break;
			}

			theApp.emuledlg->sharedfileswnd->sharedfilesctrl.SetAICHHashing(cRemainingToHash);
			theApp.sharedfiles->NotifyShowFilesCount();

			const uchar *pucFileHash = reinterpret_cast<const uchar*>(&m_vecToHashFileHashes[static_cast<size_t>(iHash) * MDX_DIGEST_SIZE]);
			--cRemainingToHash;
			++m_iProcessedHashCount;
			BuildStartupDeferredAICHHashset(pucFileHash);
		}

		theApp.emuledlg->sharedfileswnd->sharedfilesctrl.SetAICHHashing(m_iPendingHashCount);
		theApp.sharedfiles->NotifyShowFilesCount();
		sLock1.Unlock();
	}

	if (m_iPendingHashCount > 0) {
		theApp.QueueDebugLogLine(false, _T("AICHSyncThread deferred remaining hashset work. pending=%Id processed=%Id"), m_iPendingHashCount, m_iProcessedHashCount);
		theApp.emuledlg->sharedfileswnd->sharedfilesctrl.SetAICHHashing(m_iPendingHashCount);
	} else
		theApp.emuledlg->sharedfileswnd->sharedfilesctrl.SetAICHHashing(0);
	theApp.sharedfiles->NotifyShowFilesCount();

	theApp.QueueDebugLogLine(false, _T("AICHSyncThread finished"));
	return 0;
}

bool CAICHSyncThread::ConvertKnown2ToKnown264(CSafeFile &TargetFile)
{
	// converting known2.met to known2_64.met to support large files
	// changing hashcount from uint16 to uint32

	// there still exists a lock on known2_64.met and it should be not opened at this point
	const CString &sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	const CString &oldfullpath(sConfDir + OLD_KNOWN2_MET_FILENAME);
	const CString &newfullpath(sConfDir + KNOWN2_MET_FILENAME);

	// continue only if the old file does exist, and the new file does not
	if (::PathFileExists(newfullpath) || !::PathFileExists(oldfullpath))
		return false;

	CSafeFile oldfile;
	if (!CFileOpen(oldfile, oldfullpath
		, CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyNone
		, _T("Failed to load ") OLD_KNOWN2_MET_FILENAME _T(" file")))
	{
		// known2.met also doesn't exist, so nothing to convert
		return false;
	}
	if (!CFileOpen(TargetFile, newfullpath

		, CFile::modeReadWrite | CFile::modeCreate | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyNone
		, _T("Failed to load ") KNOWN2_MET_FILENAME _T(" file")))
	{
		return false;
	}

	theApp.QueueLogLine(false, GetResString(_T("CONVERTINGKNOWN2MET")), OLD_KNOWN2_MET_FILENAME, KNOWN2_MET_FILENAME);

	try {
		TargetFile.WriteUInt8(KNOWN2_MET_VERSION);
		while (oldfile.GetPosition() < oldfile.GetLength()) {
			CAICHHash aichHash(oldfile);
			uint32 nHashCount = oldfile.ReadUInt16();
			if (oldfile.GetPosition() + nHashCount * (ULONGLONG)CAICHHash::GetHashSize() > oldfile.GetLength())
				AfxThrowFileException(CFileException::endOfFile, 0, oldfile.GetFileName());

			BYTE *buffer = new BYTE[nHashCount * (size_t)CAICHHash::GetHashSize()];
			oldfile.Read(buffer, nHashCount * CAICHHash::GetHashSize());
			TargetFile.Write(aichHash.GetRawHash(), CAICHHash::GetHashSize());
			TargetFile.WriteUInt32(nHashCount);
			TargetFile.Write(buffer, nHashCount * CAICHHash::GetHashSize());
			delete[] buffer;
		}
		TargetFile.Flush();
		oldfile.Close();
	} catch (CFileException *ex) {
		if (ex->m_cause == CFileException::endOfFile) {
			LogError(LOG_STATUSBAR, GetResString(_T("ERR_MET_BAD")), OLD_KNOWN2_MET_FILENAME);
			ASSERT(0);
		} else
			LogError(LOG_STATUSBAR, GetResString(_T("ERR_SERVERMET_UNKNOWN")), (LPCTSTR)EscPercent(CExceptionStr(*ex)));
		ex->Delete();
		theApp.QueueLogLine(false, GetResString(_T("CONVERTINGKNOWN2FAILED")));
		TargetFile.Close();
		return false;
	}
	theApp.QueueLogLine(false, GetResString(_T("CONVERTINGKNOWN2DONE")));

	// FIXME LARGE FILES (uncomment)
	TargetFile.SeekToBegin();
	return true;
}
