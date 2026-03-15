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
#include <io.h>
#include "emule.h"
#include "SharedFileList.h"
#include "KnownFileList.h"
#include "KnownFile.h"
#include "opcodes.h"
#include "Preferences.h"
#include "SafeFile.h"
#include "UpDownClient.h"
#include "DownloadQueue.h"
#include "emuledlg.h"
#include "TransferDlg.h"
#include "Log.h"
#include "packets.h"
#include "MD5Sum.h"
#include "SharedFilesWnd.h"
#include "SharedFilesCtrl.h"
#include "KnownFileList.h"
#include "PartFile.h"
#include "OtherFunctions.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define KNOWN_MET_FILENAME		_T("known.met")
#define KNOWN_MET_FILENAME_TMP	_T("known.met.tmp")
#define CANCELLED_MET_FILENAME	_T("cancelled.met")

#define CANCELLED_HEADER_OLD	MET_HEADER
#define CANCELLED_HEADER		MET_HEADER_I64TAGS
#define CANCELLED_VERSION		0x01

inline bool CKnownFileList::KnownFileMatches(const CKnownFile* file, LPCTSTR filename, time_t date, uint64 size) const 
{ 
	return file && (uint64)file->GetFileSize() == size && IsFileDateEqual(file->GetUtcFileDate(), date) && file->GetFileName().CompareNoCase(filename) == 0;
}

CKnownFileList::CKnownFileList()
	: m_nTransferredTotal()
	, m_nRequestedTotal()
	, m_nAcceptedTotal()
	, transferred()
	, m_dwCancelledFilesSeed()
	, requested()
	, accepted()
{
	m_Files_map.InitHashTable(2063);
	m_mapCancelledFiles.InitHashTable(1031);
	m_nLastSaved = ::GetTickCount();
	Init();
}

CKnownFileList::~CKnownFileList()
{
	Clear();
}

bool CKnownFileList::Init()
{
	return LoadKnownFiles() && LoadCancelledFiles();
}

bool CKnownFileList::LoadKnownFiles()
{
	CSafeBufferedFile file;
	if (!CFileOpen(file
		, thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + KNOWN_MET_FILENAME
		, CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyWrite
		, _T("Failed to load ") KNOWN_MET_FILENAME))
	{
		return false;
	}
	::setvbuf(file.m_pStream, NULL, _IOFBF, 16384);

	CKnownFile *pRecord = NULL;
	try {
		uint8 header = file.ReadUInt8();
		if (header != MET_HEADER && header != MET_HEADER_I64TAGS) {
			file.Close();
			LogError(LOG_STATUSBAR, GetResString(_T("ERR_SERVERMET_BAD")));
			return false;
		}
		AddDebugLogLine(false, _T("Known.met file version is %u (%s support 64-bit tags)"), header, (header == MET_HEADER) ? _T("doesn't") : _T("does"));

		uint32 uRecordsNumber = file.ReadUInt32();
		for (uint32 i = 0; i < uRecordsNumber; ++i) {
			pRecord = new CKnownFile();
			if (!pRecord->LoadFromFile(file)) {
				TRACE(_T("*** Failed to load entry %u (name=%s  hash=%s  size=%I64u  parthashes=%u expected parthashes=%u) from known.met\n")
				, i, (LPCTSTR)pRecord->GetFileName(), (LPCTSTR)md4str(pRecord->GetFileHash()), (uint64)pRecord->GetFileSize()
				, pRecord->GetFileIdentifier().GetAvailableMD4PartHashCount(), pRecord->GetFileIdentifier().GetTheoreticalMD4PartHashCount());
				delete pRecord;
			} else
				SafeAddKFile(pRecord);
			pRecord = NULL;
		}
		file.Close();
	} catch (CFileException *ex) {
		if (ex->m_cause == CFileException::endOfFile)
			LogError(LOG_STATUSBAR, GetResString(_T("ERR_SERVERMET_BAD")));
		else
			LogError(LOG_STATUSBAR, GetResString(_T("ERR_SERVERMET_UNKNOWN")), (LPCTSTR)EscPercent(CExceptionStr(*ex)));
		ex->Delete();
		delete pRecord;
		return false;
	}

	return true;
}

bool CKnownFileList::LoadCancelledFiles()
{
// cancelled.met Format: <Header 1 = CANCELLED_HEADER><Version 1 = CANCELLED_VERSION><Seed 4><Count 4>[<HashHash 16><TagCount 1>[Tags TagCount] Count]
	if (!thePrefs.IsRememberingCancelledFiles())
		return true;
	CSafeBufferedFile file;
	if (!CFileOpen(file
		, thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + CANCELLED_MET_FILENAME
		, CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyWrite
		, _T("Failed to load ") CANCELLED_MET_FILENAME))
	{
		return false;
	}
	::setvbuf(file.m_pStream, NULL, _IOFBF, 16384);
	try {
		bool bOldVersion = false;
		uint8 header = file.ReadUInt8();
		if (header != CANCELLED_HEADER) {
			if (header == CANCELLED_HEADER_OLD) {
				bOldVersion = true;
				DebugLog(_T("Deprecated version of cancelled.met found, converting to new version"));
			} else {
				file.Close();
				return false;
			}
		}
		if (!bOldVersion) {
			if (file.ReadUInt8() > CANCELLED_VERSION) {
				file.Close();
				return false;
			}

			m_dwCancelledFilesSeed = file.ReadUInt32();
		}
		if (m_dwCancelledFilesSeed == 0) {
			ASSERT(bOldVersion || file.GetLength() <= 10);
			m_dwCancelledFilesSeed = (GetRandomUInt32() % 0xFFFFFFFEu) + 1;
		}

		uchar ucHash[MD5_DIGEST_SIZE];
		for (uint32 i = file.ReadUInt32(); i > 0; --i) { //number of records
			file.ReadHash16(ucHash);
			// for compatibility with future versions which may add more data than just the hash
			for (uint8 j = file.ReadUInt8(); j > 0; --j) //number of tags
				CTag tag(file, false);

			if (bOldVersion) {
				// convert old real hash to new hash
				uchar pachSeedHash[20];
				PokeUInt32(pachSeedHash, m_dwCancelledFilesSeed);
				md4cpy(pachSeedHash + 4, ucHash);
				MD5Sum md5(pachSeedHash, sizeof pachSeedHash);
				md4cpy(ucHash, md5.GetRawHash());
			}
			m_mapCancelledFiles[CSKey(ucHash)] = 1;
		}
		file.Close();
		return true;
	} catch (CFileException *ex) {
		if (ex->m_cause == CFileException::endOfFile)
			LogError(LOG_STATUSBAR, GetResString(_T("ERR_CONFIGCORRUPT")), CANCELLED_MET_FILENAME);
		else
			LogError(LOG_STATUSBAR, GetResString(_T("ERR_FAILEDTOLOAD")), CANCELLED_MET_FILENAME, (LPCTSTR)EscPercent(CExceptionStr(*ex)));
		ex->Delete();
	}
	return false;
}

void CKnownFileList::Save()
{
	if (thePrefs.GetLogFileSaving())
		AddDebugLogLine(false, _T("Saving known files list in \"%s\""), KNOWN_MET_FILENAME);
	m_nLastSaved = ::GetTickCount();
	const CString &sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	CSafeBufferedFile file;
	if (CFileOpen(file
		, sConfDir + KNOWN_MET_FILENAME_TMP
		, CFile::modeWrite | CFile::modeCreate | CFile::typeBinary | CFile::shareDenyWrite
		, _T("Failed to save ") KNOWN_MET_FILENAME_TMP))
	{
		::setvbuf(file.m_pStream, NULL, _IOFBF, 16384);

		try {
			file.WriteUInt8(MET_HEADER_I64TAGS);
			file.WriteUInt32(0); // the number will be rewritten

			INT_PTR iRecordsNumber = 0;

			// Duplicates handling. Duplicates needs to be saved first, since it is the last entry that gets used.
			CSingleLock slDuplicatesLock(&m_csDuplicatesLock, TRUE);
			KnownFileList::iterator itDup = m_duplicateFileList.begin();
			for (; itDup != m_duplicateFileList.end(); ++itDup) {
				(*itDup)->SetLastSeen(); // Files in duplicates list have been seen this sesssion.
				if (!(*itDup)->ShouldCompletelyPurgeFile()) {
					(*itDup)->WriteToFile(file);
					++iRecordsNumber;
				}
			}
			slDuplicatesLock.Unlock();

			for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair)) {
				CKnownFile *pFile = pair->value;
				if (thePrefs.IsRememberingDownloadedFiles() || theApp.sharedfiles->IsFilePtrInList(pFile)) {
					if (theApp.sharedfiles->GetFileByID(pFile->GetFileHash()) == pFile)
						pFile->SetLastSeen();
					if (!pFile->ShouldCompletelyPurgeFile()) {
						pFile->WriteToFile(file);
						++iRecordsNumber;
					}
				}
			}

				file.Seek(1, CFile::begin);
				file.WriteUInt32((uint32)iRecordsNumber);
			CommitAndClose(file);
			MoveFileEx(sConfDir + KNOWN_MET_FILENAME_TMP, sConfDir + KNOWN_MET_FILENAME, MOVEFILE_REPLACE_EXISTING);
		} catch (CFileException *ex) {
			LogError(LOG_STATUSBAR, _T("%s %s%s"), (LPCTSTR)GetResString(_T("ERROR_SAVEFILE")), KNOWN_MET_FILENAME, (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
			ex->Delete();
		}
	}


	if (thePrefs.GetLogFileSaving())
		AddDebugLogLine(false, _T("Saving cancelled files list in \"%s\""), CANCELLED_MET_FILENAME);
	if (CFileOpen(file
		, sConfDir + CANCELLED_MET_FILENAME
		, CFile::modeWrite | CFile::modeCreate | CFile::typeBinary | CFile::shareDenyWrite
		, _T("Failed to save ") CANCELLED_MET_FILENAME))
	{
		::setvbuf(file.m_pStream, NULL, _IOFBF, 16384);

		try {
			file.WriteUInt8(CANCELLED_HEADER);
			file.WriteUInt8(CANCELLED_VERSION);
			file.WriteUInt32(m_dwCancelledFilesSeed);
			if (!thePrefs.IsRememberingCancelledFiles())
				file.WriteUInt32(0);
			else {
				file.WriteUInt32((uint32)m_mapCancelledFiles.GetCount());
				for (const CancelledFilesMap::CPair *pair = m_mapCancelledFiles.PGetFirstAssoc(); pair != NULL; pair = m_mapCancelledFiles.PGetNextAssoc(pair)) {
					file.WriteHash16(pair->key.m_key);
					file.WriteUInt8(0); //number of tags
			}

			}
			CommitAndClose(file);
		} catch (CFileException *ex) {
			LogError(LOG_STATUSBAR, _T("%s %s%s"), (LPCTSTR)GetResString(_T("ERROR_SAVEFILE")), CANCELLED_MET_FILENAME, (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
			ex->Delete();
		}
	}
}

void CKnownFileList::Clear()
{
	m_mapKnownFilesByAICH.RemoveAll();

	// Clear auxiliary indices first to avoid dangling pointers
	CSingleLock slSize(&m_csSizeIndexLock, TRUE);
	m_sizeIndex.clear();
	slSize.Unlock();
	m_dupFileSizeIndex.clear();

	CCKey key;
	for (POSITION pos = m_Files_map.GetStartPosition(); pos != NULL;) {
		CKnownFile* pFile;
		m_Files_map.GetNextAssoc(pos, key, pFile);
		delete pFile;
	}
	m_Files_map.RemoveAll();

	CSingleLock slDuplicatesLock(&m_csDuplicatesLock, TRUE);
	while (!m_duplicateFileList.empty()) {
		CKnownFile* duplicateFile = m_duplicateFileList.front();
		m_duplicateFileList.pop_front();
		slDuplicatesLock.Unlock();
		delete duplicateFile; // CKnownFile destructor purges indexes
		slDuplicatesLock.Lock();
	}
	slDuplicatesLock.Unlock();
}

void CKnownFileList::Process()
{
	if (::GetTickCount() >= m_nLastSaved + MIN2MS(11))
		Save();
}

bool CKnownFileList::SafeAddKFile(CKnownFile *toadd)
{
    bool bRemovedDuplicateSharedFile = false;
    CCKey key(toadd->GetFileHash());
    CKnownFile* pFileInMap = NULL;
    CKnownFile* pFileInDuplicatesList = IsOnDuplicates(toadd->GetFileName(), toadd->GetUtcFileDate(), toadd->GetFileSize());
	if (m_Files_map.Lookup(key, pFileInMap) && pFileInMap != NULL) {
		if (pFileInDuplicatesList != NULL && pFileInMap != pFileInDuplicatesList) { // Same file as in duplicate file list
			TRACE2(_T("%hs: File is already in duplicates list:   %s %I64u \"%s\"\n"), __FUNCTION__, (LPCTSTR)md4str(pFileInDuplicatesList->GetFileHash()), (uint64)pFileInDuplicatesList->GetFileSize(), (LPCTSTR)pFileInDuplicatesList->GetFileName());
			return false;
		}

		// If this is same file, don't add it again.
		if (toadd == pFileInMap) {
			TRACE2(_T("%hs: File is already in known list: %s %I64u \"%s\"\n"), __FUNCTION__, (LPCTSTR)md4str(pFileInMap->GetFileHash()), (uint64)pFileInMap->GetFileSize(), (LPCTSTR)pFileInMap->GetFileName());
			return false;
		}

		// If this file has same hash and same path, replace it in pFileInMap, but don't add it to duplicates list.
		if ((!pFileInMap->GetFilePath().IsEmpty() && !toadd->GetFilePath().IsEmpty() && pFileInMap->GetFilePath().CompareNoCase(toadd->GetFilePath()) == 0)) {
			TRACE2(_T("%hs: File is already in known list: %s %I64u \"%s\"\n"), __FUNCTION__, (LPCTSTR)md4str(pFileInMap->GetFileHash()), (uint64)pFileInMap->GetFileSize(), (LPCTSTR)pFileInMap->GetFileName());
			theApp.emuledlg->sharedfileswnd->sharedfilesctrl.RemoveFromHistory(pFileInMap, false); // Remove known file
			RemoveSizeIndex(toadd); // Sync size index
			m_Files_map[key] = toadd; // Replace pFileInMap with toadd in map
			AddSizeIndex(toadd); // Sync size index
			if (toadd->GetFileIdentifier().HasAICHHash())
				m_mapKnownFilesByAICH[toadd->GetFileIdentifier().GetAICHHash()] = toadd;

			return true;
		}

		// If toadd exist and pFileInMap doesn't exist in drive we need to replace pFileInMap in map and add it to duplicates list. This way we'll prioritize adding existing files to shared file list.
		bool m_bToaddExists = (!toadd->GetFilePath().IsEmpty() && ::PathFileExists(toadd->GetFilePath()));
		if (m_bToaddExists && (pFileInMap->GetFilePath().IsEmpty() || !::PathFileExists(pFileInMap->GetFilePath()))) {
			CSingleLock slDuplicatesLock(&m_csDuplicatesLock, TRUE);
			m_duplicateFileList.remove(toadd);
			RemoveDupSizeIndex(toadd); // Sync duplicate index
			RemoveSizeIndex(toadd); // Sync size index

			pFileInMap->SetFilePath(NULL); // Remove file path from pFileInMap since this file is not on disk.
			m_duplicateFileList.push_back(pFileInMap);
            AddDupSizeIndex(pFileInMap); // Sync duplicate index
			RemoveSizeIndex(pFileInMap); // Sync size index
			slDuplicatesLock.Unlock();
			
			m_Files_map[key] = toadd;
			AddSizeIndex(toadd); // Sync size index
			if (toadd->GetFileIdentifier().HasAICHHash())
				m_mapKnownFilesByAICH[toadd->GetFileIdentifier().GetAICHHash()] = toadd;

			theApp.DownloadChecker->AddToMap(toadd->GetFileHash(), toadd->GetFileName(), toadd->GetFileSize());

			TRACE2(_T("%hs: File is removed from duplicate list: %s %I64u \"%s\"\n"), __FUNCTION__, (LPCTSTR)md4str(toadd->GetFileHash()), (uint64)toadd->GetFileSize(), (LPCTSTR)toadd->GetFileName());
			TRACE2(_T("%hs: File is added to duplicate list: %s %I64u \"%s\"\n"), __FUNCTION__, (LPCTSTR)md4str(pFileInMap->GetFileHash()), (uint64)pFileInMap->GetFileSize(), (LPCTSTR)pFileInMap->GetFileName());

			return true;
		}

		if (!m_bToaddExists)
			toadd->SetFilePath(NULL); // Remove file path from toadd since this file is not on disk.

		TRACE2(_T("%hs: File is already in known list: %s %I64u \"%s\"\n"), __FUNCTION__, (LPCTSTR)md4str(pFileInMap->GetFileHash()), (uint64)pFileInMap->GetFileSize(), (LPCTSTR)pFileInMap->GetFileName());
		TRACE2(_T("%hs: File is added to duplicate list: %s %I64u \"%s\"\n"), __FUNCTION__, (LPCTSTR)md4str(toadd->GetFileHash()), (uint64)toadd->GetFileSize(), (LPCTSTR)toadd->GetFileName());

		// We need to add toadd to m_duplicateFileList.
		CSingleLock slDuplicatesLock(&m_csDuplicatesLock, TRUE);
		m_duplicateFileList.push_back(toadd);
		AddDupSizeIndex(toadd); // Sync duplicate index
		slDuplicatesLock.Unlock();
		return false;
	}

	// This is not an expected case, doing this check just to be sure.
	// toadd will be added to the known file list below. We need to remove toadd from the duplicate list, if it is there. 
	if (pFileInDuplicatesList != NULL) {
		TRACE2(_T("%hs: File is already in duplicates list, removing it: %s %I64u \"%s\"\n"), __FUNCTION__, (LPCTSTR)md4str(toadd->GetFileHash()), (uint64)toadd->GetFileSize(), (LPCTSTR)toadd->GetFileName());
		CSingleLock slDuplicatesLock(&m_csDuplicatesLock, TRUE);
		m_duplicateFileList.remove(toadd);
		RemoveDupSizeIndex(toadd); // Sync duplicate index
		slDuplicatesLock.Unlock();
	}

	// This is a new file. We'll add it.
	m_Files_map[key] = toadd;
	AddSizeIndex(toadd); // Sync size index
	if (toadd->GetFileIdentifier().HasAICHHash())
		m_mapKnownFilesByAICH[toadd->GetFileIdentifier().GetAICHHash()] = toadd;

	if (theApp.DownloadChecker) // We don't want to run this on init
		theApp.DownloadChecker->AddToMap(toadd->GetFileHash(), toadd->GetFileName(), toadd->GetFileSize());

	return true;
}

CKnownFile* CKnownFileList::FindKnownFile(LPCTSTR filename, time_t date, uint64 size)
{
	CSingleLock sl(&m_csSizeIndexLock, TRUE);
	auto range = m_sizeIndex.equal_range(size);
	for (auto it = range.first; it != range.second; ++it)
		if (KnownFileMatches(it->second, filename, date, size))
			return it->second;
	sl.Unlock();

	return IsOnDuplicates(filename, date, size);
}

CKnownFile* CKnownFileList::FindKnownFileByPath(const CString& sFilePath) const
{
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		if (pair->value->GetFilePath().CompareNoCase(sFilePath) == 0)
			return pair->value;

	return NULL;
}

CKnownFile* CKnownFileList::FindKnownFileByID(const uchar* hash) const
{
	if (hash) {
		const CKnownFilesMap::CPair *pair = m_Files_map.PLookup(CCKey(hash));
		if (pair)
			return pair->value;
	}
	return NULL;
}

bool CKnownFileList::IsKnownFile(const CKnownFile* file) const
{
	return file && (FindKnownFileByID(file->GetFileHash()) != NULL);
}

bool CKnownFileList::IsFilePtrInList(const CKnownFile *file) const
{
	// Plookup for the file hash (which is fast) if the map has it and return true if found.
	if (file && file == FindKnownFileByID(file->GetFileHash()))
		return true;

	if (file)
		for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
			if (file == pair->value)
				return true;

	return false;
}

void CKnownFileList::AddCancelledFileID(const uchar *hash)
{
	if (thePrefs.IsRememberingCancelledFiles()) {
		if (m_dwCancelledFilesSeed == 0)
			m_dwCancelledFilesSeed = (GetRandomUInt32() % 0xFFFFFFFE) + 1;

		uchar pachSeedHash[20];
		PokeUInt32(pachSeedHash, m_dwCancelledFilesSeed);
		md4cpy(pachSeedHash + 4, hash);
		MD5Sum md5(pachSeedHash, sizeof pachSeedHash);
		md4cpy(pachSeedHash, md5.GetRawHash());
		m_mapCancelledFiles[CSKey(pachSeedHash)] = 1;
	}
}

bool CKnownFileList::IsCancelledFileByID(const uchar* hash) const
{
	if (thePrefs.IsRememberingCancelledFiles()) {
		uchar pachSeedHash[20];
		PokeUInt32(pachSeedHash, m_dwCancelledFilesSeed);
		md4cpy(pachSeedHash + 4, hash);
		MD5Sum md5(pachSeedHash, sizeof pachSeedHash);
		md4cpy(pachSeedHash, md5.GetRawHash());
		return m_mapCancelledFiles.PLookup(CSKey(pachSeedHash)) != NULL;
	}
	return false;
}

void CKnownFileList::CopyKnownFileMap(CKnownFilesMap &Files_Map)
{
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		Files_Map[pair->key] = pair->value;
}

bool CKnownFileList::RemoveKnownFile(CKnownFile* toRemove)
{
	if (!toRemove)
		return false;

	// If exactly this file is still shared, skip removal
	CShareableFile* pShared = theApp.sharedfiles->GetFileByID(toRemove->GetFileHash());
	if (pShared == toRemove)
		return false;

	// Remove from duplicate list if present
	CSingleLock slDuplicatesLock(&m_csDuplicatesLock, TRUE);
	if (IsOnDuplicates(toRemove->GetFileName(), toRemove->GetUtcFileDate(), toRemove->GetFileSize())) {
		m_duplicateFileList.remove(toRemove);
		RemoveDupSizeIndex(toRemove); // Keep duplicate size index in sync
		theApp.DownloadChecker->RemoveFromMap(toRemove->GetFileHash(), toRemove->GetFileName(), toRemove->GetFileSize());
		delete toRemove;
		return true;
	}
	slDuplicatesLock.Unlock();

	// Otherwise remove from m_Files_map
	if (m_Files_map.RemoveKey(CCKey(toRemove->GetFileHash()))) {
		RemoveSizeIndex(toRemove); // Keep size index in sync

		if (toRemove->GetFileIdentifier().HasAICHHash()) // Maintain AICH index
			m_mapKnownFilesByAICH.RemoveKey(toRemove->GetFileIdentifier().GetAICHHash());

		theApp.DownloadChecker->RemoveFromMap(toRemove->GetFileHash(), toRemove->GetFileName(), toRemove->GetFileSize());
		delete toRemove;
		return true;
	}

	return false;
}

bool CKnownFileList::ShouldPurgeAICHHashset(const CAICHHash &rAICHHash) const
{
	const CKnownFile *pFile;
	if (m_mapKnownFilesByAICH.Lookup(rAICHHash, pFile)) {
		if (thePrefs.GetRemoveAichImmediately()) {
			if (!pFile->IsPartFile() && // this is neither a download
				(theApp.sharedfiles && theApp.sharedfiles->GetFileByID(pFile->GetFileHash()) == NULL)) // and nor shared
				return true; // so purge it immediatly
		}

		if (!pFile->ShouldPartiallyPurgeFile())
			return false;
	}
	else
		ASSERT2(0);

	return true;
}

void CKnownFileList::AICHHashChanged(const CAICHHash *pOldAICHHash, const CAICHHash &rNewAICHHash, CKnownFile *pFile)
{
	if (pOldAICHHash != NULL)
		m_mapKnownFilesByAICH.RemoveKey(*pOldAICHHash);
	m_mapKnownFilesByAICH[rNewAICHHash] = pFile;
}
void CKnownFileList::ClearHistory() {
	POSITION pos = m_Files_map.GetStartPosition();
	while (pos) {
		CKnownFile* cur_file;
		CCKey key;
		m_Files_map.GetNextAssoc(pos, key, cur_file);
		if (theApp.sharedfiles->GetFileByID(cur_file->GetFileHash()) == NULL) {
			RemoveSizeIndex(cur_file); // Keep size index in sync
			RemoveDupSizeIndex(cur_file); // Keep duplicate index in sync

			// Also remove from duplicate file list
			CSingleLock slDup(&m_csDuplicatesLock, TRUE);
			m_duplicateFileList.remove(cur_file);
			slDup.Unlock();

			m_Files_map.RemoveKey(key);
			// Also remove it from transfer window
			if (cur_file->IsKindOf(RUNTIME_CLASS(CPartFile)))
				theApp.emuledlg->transferwnd->GetDownloadList()->ClearCompleted(static_cast<CPartFile*>(cur_file));

			delete cur_file;
		}
	}

	// Handle duplicate file list as well (entries which are not shared anymore)
	CSingleLock slDuplicatesLock(&m_csDuplicatesLock, TRUE);
	for (KnownFileList::iterator it = m_duplicateFileList.begin(); it != m_duplicateFileList.end(); ) {
		CKnownFile* cur_file = *it;
		if (theApp.sharedfiles->GetFileByID(cur_file->GetFileHash()) == NULL) {
			it = m_duplicateFileList.erase(it); // Remove from duplicates list while locked
			slDuplicatesLock.Unlock();

			RemoveDupSizeIndex(cur_file); // Keep duplicate index in sync
			RemoveSizeIndex(cur_file); // Keep size index in sync (safe no-op if not indexed)

			// Do NOT remove from m_Files_map here; duplicates are not the primary entry
			if (cur_file->IsKindOf(RUNTIME_CLASS(CPartFile)))
				theApp.emuledlg->transferwnd->GetDownloadList()->ClearCompleted(static_cast<CPartFile*>(cur_file));

			delete cur_file;
			slDuplicatesLock.Lock();
		} else
			++it;
	}
	slDuplicatesLock.Unlock();

	theApp.DownloadChecker->ReloadMap();
}

inline void CKnownFileList::AddSizeIndex(CKnownFile* file)
{
	if (!file)
		return;

	CSingleLock sl(&m_csSizeIndexLock, TRUE);
	m_sizeIndex.emplace(file->GetFileSize(), file);
}

inline void CKnownFileList::RemoveSizeIndex(CKnownFile* file)
{
	if (!file)
		return;

	CSingleLock sl(&m_csSizeIndexLock, TRUE);
	auto range = m_sizeIndex.equal_range(file->GetFileSize());
	for (auto it = range.first; it != range.second; ++it)
		if (it->second == file) {
			m_sizeIndex.erase(it); 
			break; 
		}
}

void CKnownFileList::AddDupSizeIndex(CKnownFile* file)
{
	if (!file)
		return;

	m_dupFileSizeIndex.emplace(file->GetFileSize(), file);
}

void CKnownFileList::RemoveDupSizeIndex(CKnownFile* file)
{
	if (!file)
		return;

	auto range = m_dupFileSizeIndex.equal_range(file->GetFileSize());
	for (auto it = range.first; it != range.second; ++it)
		if (it->second == file) {
			m_dupFileSizeIndex.erase(it);
			break;
		}
}

CKnownFile* CKnownFileList::IsOnDuplicates(const LPCTSTR filename, time_t in_date, uint64 in_size)
{
    CSingleLock sl(&m_csDuplicatesLock, TRUE);

        auto range = m_dupFileSizeIndex.equal_range(in_size);
	for (auto it = range.first; it != range.second; ++it)
		if (KnownFileMatches(it->second, filename, in_date, in_size))
			return it->second;

        return nullptr;
}

uint32 CKnownFileList::DuplicatesCount(const uchar* hash)
{
	uint32 m_uCount = 0;
	CSingleLock slDuplicatesLock(&m_csDuplicatesLock, TRUE);
        for (KnownFileList::const_iterator it = m_duplicateFileList.begin(); it != m_duplicateFileList.end(); ++it) {
                CKnownFile* cur_file = *it;
                if (md4equ(cur_file->GetFileHash(), hash))
                        m_uCount++;
        }

        return m_uCount;
}

void CKnownFileList::PurgeDuplicateFile(CKnownFile* file)
{
	if (!file)
		return;

	CSingleLock sl(&m_csDuplicatesLock, TRUE);
	m_duplicateFileList.remove(file);
	for (auto it = m_dupFileSizeIndex.begin(); it != m_dupFileSizeIndex.end();)
		if (it->second == file)
			it = m_dupFileSizeIndex.erase(it);
		else
			++it;
		sl.Unlock();
}