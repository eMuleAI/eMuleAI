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
#include "SearchList.h"
#include "OtherFunctions.h"
#include "PartFileWriteThread.h"

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

namespace
{
	const UINT kKnownMetSaveChunkGrowBytes = 64 * 1024;
	const UINT kKnownFileDefaultHashTableSize = 2063;
	const UINT kCancelledFileDefaultHashTableSize = 1031;
	const UINT kMaxStartupHashTableBuckets = 16 * 1024 * 1024 - 1;
	const size_t kLargeMetFileBufferSize = 256 * 1024;
	const size_t kIdentityIndexSizeBucketThreshold = 16;
	const UINT kStartupKnownFilesProgressUnits = 10000;

	UINT GetKnownMetReadProgress(CSafeBufferedFile& file, ULONGLONG uFileLength)
	{
		if (uFileLength == 0)
			return 0;
		ULONGLONG uDone = (file.GetPosition() * kStartupKnownFilesProgressUnits) / uFileLength;
		if (uDone > kStartupKnownFilesProgressUnits)
			uDone = kStartupKnownFilesProgressUnits;
		return static_cast<UINT>(uDone);
	}

	UINT CalculateStartupHashTableSize(uint32 uRecordsNumber, UINT uMinimum)
	{
		uint64 uDesired = static_cast<uint64>(uRecordsNumber) * 2u + 1u;
		if (uDesired < uMinimum)
			return uMinimum;
		if (uDesired > kMaxStartupHashTableBuckets)
			return kMaxStartupHashTableBuckets;
		UINT uHashSize = static_cast<UINT>(uDesired);
		if ((uHashSize & 1u) == 0)
			++uHashSize;
		return uHashSize;
	}

	uint32 GetStartupKnownFileWorkUnits(const CKnownFile* pFile)
	{
		if (pFile == NULL)
			return 1;

		uint32 uParts = pFile->GetFileIdentifierC().GetAvailableMD4PartHashCount();
		if (uParts == 0)
			uParts = pFile->GetFileIdentifierC().GetTheoreticalMD4PartHashCount();
		return 1 + uParts;
	}

	void CopyMemFileToByteVector(CSafeMemFile& source, std::vector<BYTE>& target)
	{
		const ULONGLONG uLength = source.GetLength();
		target.clear();
		if (uLength == 0)
			return;
		const size_t uSize = static_cast<size_t>(uLength);
		if (static_cast<ULONGLONG>(uSize) != uLength)
			return;
		target.assign(source.GetBuffer(), source.GetBuffer() + uSize);
	}

	void AppendMemFileAsChunk(CSafeMemFile& source, std::vector<std::vector<BYTE> >& chunks)
	{
		const ULONGLONG uLength = source.GetLength();
		if (uLength == 0)
			return;
		const size_t uSize = static_cast<size_t>(uLength);
		if (static_cast<ULONGLONG>(uSize) != uLength)
			return;
		chunks.push_back(std::vector<BYTE>());
		chunks.back().assign(source.GetBuffer(), source.GetBuffer() + uSize);
	}

	void RemoveKnownFileFromVisibleControlsBeforeDelete(CKnownFile* pFile, bool bWillReloadSharedFilesListLater = false)
	{
		if (pFile == NULL || !theApp.IsUiThread() || theApp.emuledlg == NULL)
			return;

		if (pFile->IsKindOf(RUNTIME_CLASS(CPartFile)) && theApp.emuledlg->transferwnd != NULL && theApp.emuledlg->transferwnd->GetDownloadList() != NULL && ::IsWindow(theApp.emuledlg->transferwnd->GetDownloadList()->GetSafeHwnd()))
			theApp.emuledlg->transferwnd->GetDownloadList()->RemoveFile(static_cast<CPartFile*>(pFile));

		if (theApp.emuledlg->sharedfileswnd != NULL && ::IsWindow(theApp.emuledlg->sharedfileswnd->sharedfilesctrl.GetSafeHwnd()))
			theApp.emuledlg->sharedfileswnd->sharedfilesctrl.RemoveFile(pFile, true, bWillReloadSharedFilesListLater);
	}
}

inline bool CKnownFileList::KnownFileMatches(const CKnownFile* file, LPCTSTR filename, time_t date, uint64 size) const 
{ 
	return file && (uint64)file->GetFileSize() == size && IsFileDateEqual(file->GetUtcFileDate(), date) && file->GetFileName().CompareNoCase(filename) == 0;
}

CKnownFileList::CKnownFileList(bool bLoadImmediately)
	: m_nTransferredTotal()
	, m_nRequestedTotal()
	, m_nAcceptedTotal()
	, transferred()
	, m_dwCancelledFilesSeed()
	, requested()
	, accepted()
	, m_lKnownMetSaveGeneration()
	, m_lCancelledMetSaveGeneration()
	, m_pKnownMetSaveJob(NULL)
	, m_bKnownMetSaveRerunRequested(false)
	, m_bKnownMetSaveEventPending(false)
	, m_bClearingKnownFiles(false)
{
	m_Files_map.InitHashTable(kKnownFileDefaultHashTableSize);
	m_mapKnownFilesByAICH.InitHashTable(kKnownFileDefaultHashTableSize);
	m_mapCancelledFiles.InitHashTable(kCancelledFileDefaultHashTableSize);
	m_nLastSaved = ::GetTickCount();
	if (bLoadImmediately)
		Init();
}

CKnownFileList::~CKnownFileList()
{
	ClearKnownMetSaveJob();
	Clear();
}

CKnownFileList::SKnownMetFileIdentity::SKnownMetFileIdentity()
	: m_tUtcFileDate()
	, m_uFileSize()
{
}

CKnownFileList::SKnownMetSaveJob::SKnownMetSaveJob()
	: m_lGeneration()
	, m_uNextKnownIndex()
	, m_uNextDuplicateIndex()
	, m_iRecordsNumber()
	, m_dwStartedTick()
	, m_dwLastProgressTick()
{
}

bool CKnownFileList::Init()
{
	return LoadKnownFiles() && LoadCancelledFiles();
}

void CKnownFileList::PrepareKnownFileLoadCapacity(uint32 uRecordsNumber)
{
	if (uRecordsNumber == 0)
		return;

	const UINT uLookupHashSize = CalculateStartupHashTableSize(uRecordsNumber, kKnownFileDefaultHashTableSize);
	if (m_Files_map.GetCount() == 0)
		m_Files_map.InitHashTable(kKnownFileDefaultHashTableSize);
	if (m_mapKnownFilesByAICH.GetCount() == 0)
		m_mapKnownFilesByAICH.InitHashTable(uLookupHashSize);

	CSingleLock sl(&m_csSizeIndexLock, TRUE);
	m_sizeIndex.reserve(m_sizeIndex.size() + uRecordsNumber);
}


bool CKnownFileList::LoadStartupKnownFilesRecords(CStartupKnownFilesRecords& knownRecords, CStartupCancelledFilesRecords& cancelledRecords, uint32& dwCancelledFilesSeed, LONG lGeneration, uint64 uCancellationToken)
{
	knownRecords.clear();
	cancelledRecords.clear();
	dwCancelledFilesSeed = m_dwCancelledFilesSeed;

	CSafeBufferedFile file;
	if (CFileOpen(file, thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + KNOWN_MET_FILENAME, CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyWrite, _T("Failed to load ") KNOWN_MET_FILENAME)) {
		::setvbuf(file.m_pStream, NULL, _IOFBF, kLargeMetFileBufferSize);
		CKnownFile *pRecord = NULL;
		try {
			uint8 header = file.ReadUInt8();
			if (header != MET_HEADER && header != MET_HEADER_I64TAGS) {
				file.Close();
				LogError(LOG_STATUSBAR, GetResString(_T("ERR_SERVERMET_BAD")));
				return false;
			}
			AddDebugLogLine(false, _T("Known.met file version is %u (%s support 64-bit tags)"), header, (header == MET_HEADER) ? _T("doesn't") : _T("does"));
			const uint32 uRecordsNumber = file.ReadUInt32();
			const ULONGLONG uKnownMetFileLength = file.GetLength();
			knownRecords.reserve(uRecordsNumber);
			if (lGeneration != 0 && uCancellationToken != 0)
				theApp.PublishStartupMetadataLoadProgress(CemuleApp::StartupMetadataKnownFiles, lGeneration, uCancellationToken, _T("read-known-files"), 0, kStartupKnownFilesProgressUnits);
			for (uint32 i = 0; i < uRecordsNumber; ++i) {
				if ((i & 0x3ff) == 0 && lGeneration != 0 && uCancellationToken != 0) {
					theApp.PublishStartupMetadataLoadProgress(CemuleApp::StartupMetadataKnownFiles, lGeneration, uCancellationToken, _T("read-known-files"), GetKnownMetReadProgress(file, uKnownMetFileLength), kStartupKnownFilesProgressUnits);
					if (theApp.IsStartupMetadataLoadCancelled(CemuleApp::StartupMetadataKnownFiles, lGeneration, uCancellationToken)) {
						file.Close();
						for (size_t uCleanup = 0; uCleanup < knownRecords.size(); ++uCleanup)
							delete knownRecords[uCleanup];
						knownRecords.clear();
						return false;
					}
				}
				pRecord = new CKnownFile();
				if (!pRecord->LoadFromFile(file, false)) {
					AddDebugLogLine(DLP_LOW, false, _T("Failed to load entry %u (name=%s  hash=%s  size=%I64u  parthashes=%u expected parthashes=%u) from known.met"), i, (LPCTSTR)pRecord->GetFileName(), (LPCTSTR)md4str(pRecord->GetFileHash()), (uint64)pRecord->GetFileSize(), pRecord->GetFileIdentifier().GetAvailableMD4PartHashCount(), pRecord->GetFileIdentifier().GetTheoreticalMD4PartHashCount());
					delete pRecord;
				}
				else
					knownRecords.push_back(pRecord);
				pRecord = NULL;
			}
			if (lGeneration != 0 && uCancellationToken != 0)
				theApp.PublishStartupMetadataLoadProgress(CemuleApp::StartupMetadataKnownFiles, lGeneration, uCancellationToken, _T("read-known-files"), kStartupKnownFilesProgressUnits, kStartupKnownFilesProgressUnits);
			file.Close();
		} catch (CFileException *ex) {
			if (ex->m_cause == CFileException::endOfFile)
				LogError(LOG_STATUSBAR, GetResString(_T("ERR_SERVERMET_BAD")));
			else
				LogError(LOG_STATUSBAR, GetResString(_T("ERR_SERVERMET_UNKNOWN")), (LPCTSTR)EscPercent(CExceptionStr(*ex)));
			ex->Delete();
			delete pRecord;
			for (size_t uCleanup = 0; uCleanup < knownRecords.size(); ++uCleanup)
				delete knownRecords[uCleanup];
			knownRecords.clear();
			return false;
		}
	}

	if (thePrefs.IsRememberingCancelledFiles()) {
		CSafeBufferedFile cancelledFile;
		if (CFileOpen(cancelledFile, thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + CANCELLED_MET_FILENAME, CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyWrite, _T("Failed to load ") CANCELLED_MET_FILENAME)) {
			::setvbuf(cancelledFile.m_pStream, NULL, _IOFBF, kLargeMetFileBufferSize);
			try {
				bool bOldVersion = false;
				uint8 header = cancelledFile.ReadUInt8();
				if (header != CANCELLED_HEADER) {
					if (header == CANCELLED_HEADER_OLD) {
						bOldVersion = true;
						DebugLog(_T("Deprecated version of cancelled.met found, converting to new version"));
					} else {
						cancelledFile.Close();
						return false;
					}
				}
				if (!bOldVersion) {
					if (cancelledFile.ReadUInt8() > CANCELLED_VERSION) {
						cancelledFile.Close();
						return false;
					}
					dwCancelledFilesSeed = cancelledFile.ReadUInt32();
				}
				if (dwCancelledFilesSeed == 0)
					dwCancelledFilesSeed = (GetRandomUInt32() % 0xFFFFFFFEu) + 1;
				uchar ucHash[MD5_DIGEST_SIZE];
				const uint32 uCancelledCount = cancelledFile.ReadUInt32();
				cancelledRecords.reserve(uCancelledCount);
				for (uint32 i = uCancelledCount; i > 0; --i) {
					cancelledFile.ReadHash16(ucHash);
					for (uint8 j = cancelledFile.ReadUInt8(); j > 0; --j)
						CTag tag(cancelledFile, false);
					if (bOldVersion) {
						uchar pachSeedHash[20];
						PokeUInt32(pachSeedHash, dwCancelledFilesSeed);
						md4cpy(pachSeedHash + 4, ucHash);
						MD5Sum md5(pachSeedHash, sizeof pachSeedHash);
						md4cpy(ucHash, md5.GetRawHash());
					}
					cancelledRecords.push_back(CSKey(ucHash));
				}
				cancelledFile.Close();
			} catch (CFileException *ex) {
				if (ex->m_cause == CFileException::endOfFile)
					LogError(LOG_STATUSBAR, GetResString(_T("ERR_CONFIGCORRUPT")), CANCELLED_MET_FILENAME);
				else
					LogError(LOG_STATUSBAR, GetResString(_T("ERR_FAILEDTOLOAD")), CANCELLED_MET_FILENAME, (LPCTSTR)EscPercent(CExceptionStr(*ex)));
				ex->Delete();
				for (size_t uCleanup = 0; uCleanup < knownRecords.size(); ++uCleanup)
				delete knownRecords[uCleanup];
			knownRecords.clear();
				cancelledRecords.clear();
				return false;
			}
		}
	}

	return true;
}


bool CKnownFileList::LoadStartupKnownFilesForWorker(LONG lGeneration, uint64 uCancellationToken)
{
	CSafeBufferedFile file;
	if (CFileOpen(file, thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + KNOWN_MET_FILENAME, CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyWrite, _T("Failed to load ") KNOWN_MET_FILENAME)) {
		::setvbuf(file.m_pStream, NULL, _IOFBF, kLargeMetFileBufferSize);
		CKnownFile *pRecord = NULL;
		try {
			uint8 header = file.ReadUInt8();
			if (header != MET_HEADER && header != MET_HEADER_I64TAGS) {
				file.Close();
				LogError(LOG_STATUSBAR, GetResString(_T("ERR_SERVERMET_BAD")));
				return false;
			}
			AddDebugLogLine(false, _T("Known.met file version is %u (%s support 64-bit tags)"), header, (header == MET_HEADER) ? _T("doesn't") : _T("does"));
			const uint32 uRecordsNumber = file.ReadUInt32();
			const ULONGLONG uKnownMetFileLength = file.GetLength();
			PrepareKnownFileLoadCapacity(uRecordsNumber);
			if (lGeneration != 0 && uCancellationToken != 0)
				theApp.PublishStartupMetadataLoadProgress(CemuleApp::StartupMetadataKnownFiles, lGeneration, uCancellationToken, _T("read-known-files"), 0, kStartupKnownFilesProgressUnits);
			for (uint32 i = 0; i < uRecordsNumber; ++i) {
				if ((i & 0x3ff) == 0 && lGeneration != 0 && uCancellationToken != 0) {
					theApp.PublishStartupMetadataLoadProgress(CemuleApp::StartupMetadataKnownFiles, lGeneration, uCancellationToken, _T("read-known-files"), GetKnownMetReadProgress(file, uKnownMetFileLength), kStartupKnownFilesProgressUnits);
					if (theApp.IsStartupMetadataLoadCancelled(CemuleApp::StartupMetadataKnownFiles, lGeneration, uCancellationToken)) {
						file.Close();
						return false;
					}
				}
				pRecord = new CKnownFile();
				if (!pRecord->LoadFromFile(file, false)) {
					AddDebugLogLine(DLP_LOW, false, _T("Failed to load entry %u (name=%s  hash=%s  size=%I64u  parthashes=%u expected parthashes=%u) from known.met"), i, (LPCTSTR)pRecord->GetFileName(), (LPCTSTR)md4str(pRecord->GetFileHash()), (uint64)pRecord->GetFileSize(), pRecord->GetFileIdentifier().GetAvailableMD4PartHashCount(), pRecord->GetFileIdentifier().GetTheoreticalMD4PartHashCount());
					delete pRecord;
				}
				else if (!SafeAddKFile(pRecord, false, false))
					delete pRecord;
				pRecord = NULL;
			}
			if (lGeneration != 0 && uCancellationToken != 0)
				theApp.PublishStartupMetadataLoadProgress(CemuleApp::StartupMetadataKnownFiles, lGeneration, uCancellationToken, _T("read-known-files"), kStartupKnownFilesProgressUnits, kStartupKnownFilesProgressUnits);
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
	}

	if (thePrefs.IsRememberingCancelledFiles()) {
		CSafeBufferedFile cancelledFile;
		if (CFileOpen(cancelledFile, thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + CANCELLED_MET_FILENAME, CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyWrite, _T("Failed to load ") CANCELLED_MET_FILENAME)) {
			::setvbuf(cancelledFile.m_pStream, NULL, _IOFBF, kLargeMetFileBufferSize);
			try {
				bool bOldVersion = false;
				uint8 header = cancelledFile.ReadUInt8();
				if (header != CANCELLED_HEADER) {
					if (header == CANCELLED_HEADER_OLD) {
						bOldVersion = true;
						DebugLog(_T("Deprecated version of cancelled.met found, converting to new version"));
					} else {
						cancelledFile.Close();
						return false;
					}
				}
				if (!bOldVersion) {
					if (cancelledFile.ReadUInt8() > CANCELLED_VERSION) {
						cancelledFile.Close();
						return false;
					}
					m_dwCancelledFilesSeed = cancelledFile.ReadUInt32();
				}
				if (m_dwCancelledFilesSeed == 0)
					m_dwCancelledFilesSeed = (GetRandomUInt32() % 0xFFFFFFFEu) + 1;
				m_mapCancelledFiles.RemoveAll();
				uchar ucHash[MD5_DIGEST_SIZE];
				const uint32 uCancelledCount = cancelledFile.ReadUInt32();
				const UINT uCancelledHashSize = CalculateStartupHashTableSize(uCancelledCount, kCancelledFileDefaultHashTableSize);
				m_mapCancelledFiles.InitHashTable(uCancelledHashSize);
				for (uint32 i = uCancelledCount; i > 0; --i) {
					cancelledFile.ReadHash16(ucHash);
					for (uint8 j = cancelledFile.ReadUInt8(); j > 0; --j)
						CTag tag(cancelledFile, false);
					if (bOldVersion) {
						uchar pachSeedHash[20];
						PokeUInt32(pachSeedHash, m_dwCancelledFilesSeed);
						md4cpy(pachSeedHash + 4, ucHash);
						MD5Sum md5(pachSeedHash, sizeof pachSeedHash);
						md4cpy(ucHash, md5.GetRawHash());
					}
					m_mapCancelledFiles[CSKey(ucHash)] = 1;
				}
				cancelledFile.Close();
			} catch (CFileException *ex) {
				if (ex->m_cause == CFileException::endOfFile)
					LogError(LOG_STATUSBAR, GetResString(_T("ERR_CONFIGCORRUPT")), CANCELLED_MET_FILENAME);
				else
					LogError(LOG_STATUSBAR, GetResString(_T("ERR_FAILEDTOLOAD")), CANCELLED_MET_FILENAME, (LPCTSTR)EscPercent(CExceptionStr(*ex)));
				ex->Delete();
				return false;
			}
		}
	}

	return true;
}

bool CKnownFileList::ParseStartupKnownFilesLoadChunk(CStartupKnownFilesRecords* pKnownRecords, std::vector<CKnownFile*>& parsedFiles, std::vector<uint32>* pWorkUnits, uint64* puTotalWorkUnits, size_t& uNextRecord, size_t uMaxRecords)
{
	if (pKnownRecords == NULL || uNextRecord > pKnownRecords->size())
		return false;
	const size_t uLimit = uMaxRecords == 0 ? static_cast<size_t>(-1) : uMaxRecords;
	size_t uProcessed = 0;
	while (uNextRecord < pKnownRecords->size() && uProcessed < uLimit) {
		CKnownFile* pFile = (*pKnownRecords)[uNextRecord];
		(*pKnownRecords)[uNextRecord] = NULL;
		if (pFile != NULL) {
			const uint32 uWorkUnits = GetStartupKnownFileWorkUnits(pFile);
			parsedFiles.push_back(pFile);
			if (pWorkUnits != NULL)
				pWorkUnits->push_back(uWorkUnits);
			if (puTotalWorkUnits != NULL)
				*puTotalWorkUnits += uWorkUnits;
		}
		++uNextRecord;
		++uProcessed;
	}
	return true;
}

bool CKnownFileList::AttachStartupKnownFilesLoadChunk(std::vector<CKnownFile*>& parsedFiles, size_t& uNextParsedFile, size_t uMaxFiles, const std::vector<uint32>* pWorkUnits, uint64* puAppliedWorkUnits)
{
	if (uNextParsedFile > parsedFiles.size())
		return false;
	const size_t uLimit = uMaxFiles == 0 ? static_cast<size_t>(-1) : uMaxFiles;
	if (uNextParsedFile == 0 && !parsedFiles.empty())
		PrepareKnownFileLoadCapacity(static_cast<uint32>(parsedFiles.size()));
	const DWORD dwSliceStart = ::GetTickCount();
	const size_t uTimeCheckGranularity = 64;
	size_t uProcessed = 0;
	while (uNextParsedFile < parsedFiles.size() && uProcessed < uLimit) {
		const size_t uCurrentIndex = uNextParsedFile;
		CKnownFile* pFile = parsedFiles[uCurrentIndex];
		parsedFiles[uCurrentIndex] = NULL;
		if (pFile != NULL && !SafeAddKFile(pFile, false, false))
			delete pFile;
		if (puAppliedWorkUnits != NULL)
			*puAppliedWorkUnits += (pWorkUnits != NULL && uCurrentIndex < pWorkUnits->size()) ? (*pWorkUnits)[uCurrentIndex] : 1;
		++uNextParsedFile;
		++uProcessed;
		if ((uProcessed % uTimeCheckGranularity) == 0 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetStartupApply))
			break;
	}
	return true;
}

bool CKnownFileList::ApplyStartupKnownFilesCompletionChunk(CStartupCancelledFilesRecords* pCancelledRecords, uint32 dwCancelledFilesSeed, bool& bStarted, size_t& uNextCancelledRecord, size_t uMaxRecords, UINT& uApplied, INT_PTR& iRemaining)
{
	uApplied = 0;
	iRemaining = 0;

	if (!bStarted) {
		m_dwCancelledFilesSeed = dwCancelledFilesSeed;
		m_mapCancelledFiles.RemoveAll();
		m_mapCancelledFiles.InitHashTable(CalculateStartupHashTableSize(pCancelledRecords != NULL ? static_cast<uint32>(pCancelledRecords->size()) : 0, kCancelledFileDefaultHashTableSize));
		bStarted = true;
	}

	if (pCancelledRecords == NULL)
		return true;
	if (uNextCancelledRecord > pCancelledRecords->size())
		return false;

	const size_t uLimit = uMaxRecords == 0 ? static_cast<size_t>(-1) : uMaxRecords;
	const DWORD dwSliceStart = ::GetTickCount();
	while (uNextCancelledRecord < pCancelledRecords->size() && static_cast<size_t>(uApplied) < uLimit) {
		m_mapCancelledFiles[(*pCancelledRecords)[uNextCancelledRecord]] = 1;
		++uNextCancelledRecord;
		++uApplied;
		if (uApplied != 0 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetStartupApply))
			break;
	}

	iRemaining = static_cast<INT_PTR>(pCancelledRecords->size() - uNextCancelledRecord);
	return true;
}

void CKnownFileList::CompleteStartupKnownFilesLoadApply(CStartupCancelledFilesRecords* pCancelledRecords, uint32 dwCancelledFilesSeed)
{
	bool bStarted = false;
	size_t uNextCancelledRecord = 0;
	UINT uApplied = 0;
	INT_PTR iRemaining = 0;
	do {
		uApplied = 0;
		iRemaining = 0;
		if (!ApplyStartupKnownFilesCompletionChunk(pCancelledRecords, dwCancelledFilesSeed, bStarted, uNextCancelledRecord, static_cast<size_t>(-1), uApplied, iRemaining))
			break;
	} while (iRemaining > 0);
}

void CKnownFileList::DeleteStartupKnownFilesRecords(CStartupKnownFilesRecords* pKnownRecords, CStartupCancelledFilesRecords* pCancelledRecords)
{
	if (pKnownRecords != NULL) {
		for (size_t i = 0; i < pKnownRecords->size(); ++i)
			delete (*pKnownRecords)[i];
		pKnownRecords->clear();
		delete pKnownRecords;
	}
	if (pCancelledRecords != NULL)
		delete pCancelledRecords;
}

void CKnownFileList::DeleteStartupKnownFilesParsedFiles(std::vector<CKnownFile*>& parsedFiles)
{
	for (size_t i = 0; i < parsedFiles.size(); ++i)
		delete parsedFiles[i];
	parsedFiles.clear();
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
	::setvbuf(file.m_pStream, NULL, _IOFBF, kLargeMetFileBufferSize);

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
		PrepareKnownFileLoadCapacity(uRecordsNumber);
		for (uint32 i = 0; i < uRecordsNumber; ++i) {
			pRecord = new CKnownFile();
			if (!pRecord->LoadFromFile(file)) {
				AddDebugLogLine(DLP_LOW, false, _T("Failed to load entry %u (name=%s  hash=%s  size=%I64u  parthashes=%u expected parthashes=%u) from known.met")
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
	::setvbuf(file.m_pStream, NULL, _IOFBF, kLargeMetFileBufferSize);
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
		m_mapCancelledFiles.RemoveAll();
		const uint32 uCount = file.ReadUInt32();
		m_mapCancelledFiles.InitHashTable(CalculateStartupHashTableSize(uCount, kCancelledFileDefaultHashTableSize));
		for (uint32 i = uCount; i > 0; --i) { //number of records
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

static void CopyMemFileToAsyncDiskData(CSafeMemFile& source, AsyncDiskWriteData& target)
{
	CopyMemFileToByteVector(source, target.data);
}

static bool QueueOrWriteAsyncDiskData(AsyncDiskWriteData* pData)
{
	return CPartFileWriteThread::QueueOrWriteDiskSnapshot(pData);
}

void CKnownFileList::Save()
{
	m_nLastSaved = ::GetTickCount();
	StartKnownMetSaveJob();
	if (theApp.IsClosing()) {
		DWORD dwLastPump = ::GetTickCount();
		while (m_pKnownMetSaveJob != NULL) {
			ProcessKnownMetSaveJob();
			const DWORD dwNow = ::GetTickCount();
			if (theApp.emuledlg != NULL && static_cast<DWORD>(dwNow - dwLastPump) >= 50) {
				theApp.emuledlg->PumpShutdownProgressDialog();
				dwLastPump = dwNow;
			}
		}
	}
	SaveCancelledFiles();
}

void CKnownFileList::StartKnownMetSaveJob()
{
	if (m_pKnownMetSaveJob != NULL) {
		m_bKnownMetSaveRerunRequested = true;
		NextKnownMetSaveGeneration();
		QueueKnownMetSaveSlice();
		return;
	}

	if (thePrefs.GetLogFileSaving())
		AddDebugLogLine(false, _T("Saving known files list in \"%s\""), KNOWN_MET_FILENAME);

	SKnownMetSaveJob* pJob = new SKnownMetSaveJob();
	pJob->m_lGeneration = NextKnownMetSaveGeneration();
	pJob->m_strConfDir = thePrefs.GetMuleDirectory(EMULE_CONFIGDIR);
	pJob->m_dwStartedTick = ::GetTickCount();
	pJob->m_dwLastProgressTick = pJob->m_dwStartedTick;

	CSafeMemFile headerData(16);
	headerData.WriteUInt8(MET_HEADER_I64TAGS);
	headerData.WriteUInt32(0);
	CopyMemFileToByteVector(headerData, pJob->m_vecHeader);

	const INT_PTR iKnownCount = m_Files_map.GetCount();
	if (iKnownCount > 0)
		pJob->m_vecKnownHashes.reserve(static_cast<size_t>(iKnownCount));
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair)) {
		if (pair->key.m_key != NULL)
			pJob->m_vecKnownHashes.push_back(CSKey(pair->key.m_key));
	}

	CSingleLock slDuplicatesLock(&m_csDuplicatesLock, TRUE);
	pJob->m_vecDuplicateFiles.reserve(m_duplicateFileList.size());
	for (KnownFileList::const_iterator itDup = m_duplicateFileList.begin(); itDup != m_duplicateFileList.end(); ++itDup) {
		const CKnownFile* pFile = *itDup;
		if (pFile == NULL)
			continue;
		SKnownMetFileIdentity identity;
		identity.m_hash = CSKey(pFile->GetFileHash());
		identity.m_strFileName = pFile->GetFileName();
		identity.m_tUtcFileDate = pFile->GetUtcFileDate();
		identity.m_uFileSize = (uint64)pFile->GetFileSize();
		pJob->m_vecDuplicateFiles.push_back(identity);
	}
	slDuplicatesLock.Unlock();

	m_pKnownMetSaveJob = pJob;
	m_bKnownMetSaveRerunRequested = false;
	QueueKnownMetSaveSlice();
}

void CKnownFileList::QueueKnownMetSaveSlice()
{
	if (m_pKnownMetSaveJob == NULL || m_bKnownMetSaveEventPending || theApp.IsClosing())
		return;

	m_bKnownMetSaveEventPending = true;
	if (!theApp.QueuePersistenceWorkRequest(KNOWN_MET_FILENAME))
		m_bKnownMetSaveEventPending = false;
}

void CKnownFileList::ClearKnownMetSaveJob()
{
	delete m_pKnownMetSaveJob;
	m_pKnownMetSaveJob = NULL;
	m_bKnownMetSaveRerunRequested = false;
	m_bKnownMetSaveEventPending = false;
}

bool CKnownFileList::WriteKnownMetRecord(const CSKey& hash, CSafeMemFile& chunkData, INT_PTR& iRecordsNumber)
{
	CKnownFile* pFile = NULL;
	if (!m_Files_map.Lookup(CCKey(hash.m_key), pFile) || pFile == NULL)
		return false;

	CKnownFile* pLiveSharedFile = theApp.sharedfiles != NULL ? theApp.sharedfiles->GetLiveFileByID(pFile->GetFileHash()) : NULL;
	const bool bIsLiveSharedFile = pLiveSharedFile == pFile;
	if (!thePrefs.IsRememberingDownloadedFiles() && !bIsLiveSharedFile)
		return false;

	if (bIsLiveSharedFile || (theApp.sharedfiles != NULL && theApp.sharedfiles->GetFileByID(pFile->GetFileHash()) == pFile))
		pFile->SetLastSeen();
	if (pFile->ShouldCompletelyPurgeFile())
		return false;

	pFile->WriteToFile(chunkData);
	++iRecordsNumber;
	return true;
}

bool CKnownFileList::WriteKnownMetDuplicateRecord(const SKnownMetFileIdentity& identity, CSafeMemFile& chunkData, INT_PTR& iRecordsNumber)
{
	CSingleLock slDuplicatesLock(&m_csDuplicatesLock, TRUE);
	for (KnownFileList::iterator itDup = m_duplicateFileList.begin(); itDup != m_duplicateFileList.end(); ++itDup) {
		CKnownFile* pFile = *itDup;
		if (pFile == NULL)
			continue;
		if (!md4equ(pFile->GetFileHash(), identity.m_hash.m_key))
			continue;
		if ((uint64)pFile->GetFileSize() != identity.m_uFileSize || !IsFileDateEqual(pFile->GetUtcFileDate(), identity.m_tUtcFileDate) || pFile->GetFileName().CompareNoCase(identity.m_strFileName) != 0)
			continue;

		pFile->SetLastSeen();
		if (!pFile->ShouldCompletelyPurgeFile()) {
			pFile->WriteToFile(chunkData);
			++iRecordsNumber;
		}
		return true;
	}
	return false;
}

void CKnownFileList::DeferKnownMetSaveJob()
{
	m_bKnownMetSaveEventPending = false;
}

bool CKnownFileList::ProcessKnownMetSaveJob()
{
	m_bKnownMetSaveEventPending = false;
	if (m_pKnownMetSaveJob == NULL)
		return false;

	if (m_pKnownMetSaveJob->m_lGeneration != GetKnownMetSaveGeneration()) {
		const bool bRerun = m_bKnownMetSaveRerunRequested;
		ClearKnownMetSaveJob();
		if (bRerun)
			StartKnownMetSaveJob();
		return m_pKnownMetSaveJob != NULL;
	}

	const DWORD dwSliceStart = ::GetTickCount();
	UINT uProcessedInSlice = 0;
	CSafeMemFile chunkData(kKnownMetSaveChunkGrowBytes);
	try {
		while (m_pKnownMetSaveJob->m_uNextKnownIndex < m_pKnownMetSaveJob->m_vecKnownHashes.size()) {
			const CSKey hash(m_pKnownMetSaveJob->m_vecKnownHashes[m_pKnownMetSaveJob->m_uNextKnownIndex++]);
			WriteKnownMetRecord(hash, chunkData, m_pKnownMetSaveJob->m_iRecordsNumber);
			++uProcessedInSlice;
			if (uProcessedInSlice != 0 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetPersistenceSave))
				break;
		}

		while (m_pKnownMetSaveJob->m_uNextKnownIndex >= m_pKnownMetSaveJob->m_vecKnownHashes.size() && m_pKnownMetSaveJob->m_uNextDuplicateIndex < m_pKnownMetSaveJob->m_vecDuplicateFiles.size()) {
			const SKnownMetFileIdentity identity(m_pKnownMetSaveJob->m_vecDuplicateFiles[m_pKnownMetSaveJob->m_uNextDuplicateIndex++]);
			WriteKnownMetDuplicateRecord(identity, chunkData, m_pKnownMetSaveJob->m_iRecordsNumber);
			++uProcessedInSlice;
			if (uProcessedInSlice != 0 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetPersistenceSave))
				break;
		}

		AppendMemFileAsChunk(chunkData, m_pKnownMetSaveJob->m_vecChunks);
	} catch (CFileException *ex) {
		LogError(LOG_STATUSBAR, _T("%s %s%s"), (LPCTSTR)GetResString(_T("ERROR_SAVEFILE")), KNOWN_MET_FILENAME, (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
		ex->Delete();
		ClearKnownMetSaveJob();
		return false;
	} catch (...) {
		LogError(LOG_STATUSBAR, _T("%s %s"), (LPCTSTR)GetResString(_T("ERROR_SAVEFILE")), KNOWN_MET_FILENAME);
		ClearKnownMetSaveJob();
		return false;
	}

	const size_t uTotal = m_pKnownMetSaveJob->m_vecKnownHashes.size() + m_pKnownMetSaveJob->m_vecDuplicateFiles.size();
	const size_t uDone = m_pKnownMetSaveJob->m_uNextKnownIndex + m_pKnownMetSaveJob->m_uNextDuplicateIndex;
	const DWORD dwNow = ::GetTickCount();
	if (static_cast<DWORD>(dwNow - m_pKnownMetSaveJob->m_dwLastProgressTick) >= theApp.GetTimeBudgetedProgressTraceMs(CemuleApp::TimeBudgetPersistenceSave)) {
		m_pKnownMetSaveJob->m_dwLastProgressTick = dwNow;
		if (thePrefs.GetLogUiResponsivenessEvents())
			AddDebugLogLine(DLP_VERYLOW, false, _T("[Persistence] known.met snapshot progress. processed=%u total=%u records=%d generation=%ld\n"), static_cast<UINT>(uDone), static_cast<UINT>(uTotal), static_cast<int>(m_pKnownMetSaveJob->m_iRecordsNumber), m_pKnownMetSaveJob->m_lGeneration);
	}

	if (m_pKnownMetSaveJob->m_uNextKnownIndex < m_pKnownMetSaveJob->m_vecKnownHashes.size() || m_pKnownMetSaveJob->m_uNextDuplicateIndex < m_pKnownMetSaveJob->m_vecDuplicateFiles.size()) {
		QueueKnownMetSaveSlice();
		return true;
	}

	return FinishKnownMetSaveJob();
}

bool CKnownFileList::FinishKnownMetSaveJob()
{
	if (m_pKnownMetSaveJob == NULL)
		return false;

	SKnownMetSaveJob* pJob = m_pKnownMetSaveJob;
	m_pKnownMetSaveJob = NULL;
	m_bKnownMetSaveEventPending = false;

	if (pJob->m_lGeneration != GetKnownMetSaveGeneration()) {
		const bool bRerun = m_bKnownMetSaveRerunRequested;
		m_bKnownMetSaveRerunRequested = false;
		delete pJob;
		if (bRerun)
			StartKnownMetSaveJob();
		return m_pKnownMetSaveJob != NULL;
	}

	if (pJob->m_vecHeader.size() >= 5)
		PokeUInt32(&pJob->m_vecHeader[1], (uint32)pJob->m_iRecordsNumber);

	AsyncDiskWriteData* pKnownData = new AsyncDiskWriteData;
	pKnownData->lGeneration = pJob->m_lGeneration;
	pKnownData->plGeneration = &m_lKnownMetSaveGeneration;
	pKnownData->strTempPath = pJob->m_strConfDir + KNOWN_MET_FILENAME_TMP;
	pKnownData->strFinalPath = pJob->m_strConfDir + KNOWN_MET_FILENAME;
	pKnownData->strLogName = KNOWN_MET_FILENAME;
	pKnownData->eConflictPolicy = AsyncDiskWriteConflictLastSnapshotWins;
	pKnownData->data.swap(pJob->m_vecHeader);
	pKnownData->chunks.swap(pJob->m_vecChunks);
	QueueOrWriteAsyncDiskData(pKnownData);

	const bool bRerun = m_bKnownMetSaveRerunRequested;
	m_bKnownMetSaveRerunRequested = false;
	delete pJob;
	if (bRerun)
		StartKnownMetSaveJob();
	return m_pKnownMetSaveJob != NULL;
}

void CKnownFileList::SaveCancelledFiles()
{
	if (thePrefs.GetLogFileSaving())
		AddDebugLogLine(false, _T("Saving cancelled files list in \"%s\""), CANCELLED_MET_FILENAME);

	const CString &sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	const LONG lGeneration = NextCancelledMetSaveGeneration();
	try {
		CSafeMemFile cancelledFileData;
		cancelledFileData.WriteUInt8(CANCELLED_HEADER);
		cancelledFileData.WriteUInt8(CANCELLED_VERSION);
		cancelledFileData.WriteUInt32(m_dwCancelledFilesSeed);
		if (!thePrefs.IsRememberingCancelledFiles())
			cancelledFileData.WriteUInt32(0);
		else {
			cancelledFileData.WriteUInt32((uint32)m_mapCancelledFiles.GetCount());
			for (const CancelledFilesMap::CPair *pair = m_mapCancelledFiles.PGetFirstAssoc(); pair != NULL; pair = m_mapCancelledFiles.PGetNextAssoc(pair)) {
				cancelledFileData.WriteHash16(pair->key.m_key);
				cancelledFileData.WriteUInt8(0); //number of tags
			}
		}

		AsyncDiskWriteData* pCancelledData = new AsyncDiskWriteData;
		pCancelledData->lGeneration = lGeneration;
		pCancelledData->plGeneration = &m_lCancelledMetSaveGeneration;
		pCancelledData->strTempPath = sConfDir + CANCELLED_MET_FILENAME + _T(".tmp");
		pCancelledData->strFinalPath = sConfDir + CANCELLED_MET_FILENAME;
		pCancelledData->strLogName = CANCELLED_MET_FILENAME;
		pCancelledData->eConflictPolicy = AsyncDiskWriteConflictLastSnapshotWins;
		CopyMemFileToAsyncDiskData(cancelledFileData, *pCancelledData);
		QueueOrWriteAsyncDiskData(pCancelledData);
	} catch (CFileException *ex) {
		LogError(LOG_STATUSBAR, _T("%s %s%s"), (LPCTSTR)GetResString(_T("ERROR_SAVEFILE")), CANCELLED_MET_FILENAME, (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
		ex->Delete();
	} catch (...) {
		LogError(LOG_STATUSBAR, _T("%s %s"), (LPCTSTR)GetResString(_T("ERROR_SAVEFILE")), CANCELLED_MET_FILENAME);
	}
}

void CKnownFileList::Clear()
{
	m_bClearingKnownFiles = true;
	m_mapKnownFilesByAICH.RemoveAll();

	// Clear auxiliary indices first to avoid dangling pointers.
	CSingleLock slSize(&m_csSizeIndexLock, TRUE);
	m_sizeIndex.clear();
	m_identityIndex.clear();
	m_identityIndexedSizes.clear();
	slSize.Unlock();
	m_dupFileSizeIndex.clear();
	m_dupFileIdentityIndex.clear();
	m_dupIdentityIndexedSizes.clear();
	m_dupHashCounts.RemoveAll();

	UINT uDeleted = 0;
	CCKey key;
	for (POSITION pos = m_Files_map.GetStartPosition(); pos != NULL;) {
		CKnownFile* pFile;
		m_Files_map.GetNextAssoc(pos, key, pFile);
		delete pFile;
		if (theApp.emuledlg != NULL && ((++uDeleted & 0x3F) == 0))
			theApp.emuledlg->PumpShutdownProgressDialog();
	}
	m_Files_map.RemoveAll();

	CSingleLock slDuplicatesLock(&m_csDuplicatesLock, TRUE);
	while (!m_duplicateFileList.empty()) {
		CKnownFile* duplicateFile = m_duplicateFileList.front();
		m_duplicateFileList.pop_front();
		slDuplicatesLock.Unlock();
		delete duplicateFile;
		if (theApp.emuledlg != NULL && ((++uDeleted & 0x3F) == 0))
			theApp.emuledlg->PumpShutdownProgressDialog();
		slDuplicatesLock.Lock();
	}
	slDuplicatesLock.Unlock();
	m_bClearingKnownFiles = false;
}

void CKnownFileList::Process()
{
	if (::GetTickCount() >= m_nLastSaved + MIN2MS(11))
		Save();
}

bool CKnownFileList::SafeAddKFile(CKnownFile *toadd, bool bUpdateDownloadValidator, bool bLogDuplicateDetails)
{
    bool bRemovedDuplicateSharedFile = false;
    CCKey key(toadd->GetFileHash());
    CKnownFile* pFileInMap = NULL;
    CKnownFile* pFileInDuplicatesList = IsOnDuplicates(toadd->GetFileName(), toadd->GetUtcFileDate(), toadd->GetFileSize());
	if (m_Files_map.Lookup(key, pFileInMap) && pFileInMap != NULL) {
		if (pFileInDuplicatesList != NULL && pFileInMap != pFileInDuplicatesList) { // Same file as in duplicate file list
			if (bLogDuplicateDetails)
				AddDebugLogLine(DLP_VERYLOW, false, _T("%hs: File is already in duplicates list:   %s %I64u \"%s\""), __FUNCTION__, (LPCTSTR)md4str(pFileInDuplicatesList->GetFileHash()), (uint64)pFileInDuplicatesList->GetFileSize(), (LPCTSTR)EscPercent(pFileInDuplicatesList->GetFileName()));
			return false;
		}

		// If this is same file, don't add it again.
		if (toadd == pFileInMap) {
			if (bLogDuplicateDetails)
				AddDebugLogLine(DLP_VERYLOW, false, _T("%hs: File is already in known list: %s %I64u \"%s\""), __FUNCTION__, (LPCTSTR)md4str(pFileInMap->GetFileHash()), (uint64)pFileInMap->GetFileSize(), (LPCTSTR)EscPercent(pFileInMap->GetFileName()));
			return false;
		}

		// If this file has same hash and same path, replace it in pFileInMap, but don't add it to duplicates list.
		if ((!pFileInMap->GetFilePath().IsEmpty() && !toadd->GetFilePath().IsEmpty() && pFileInMap->GetFilePath().CompareNoCase(toadd->GetFilePath()) == 0)) {
			if (bLogDuplicateDetails)
				AddDebugLogLine(DLP_VERYLOW, false, _T("%hs: File is already in known list: %s %I64u \"%s\""), __FUNCTION__, (LPCTSTR)md4str(pFileInMap->GetFileHash()), (uint64)pFileInMap->GetFileSize(), (LPCTSTR)EscPercent(pFileInMap->GetFileName()));

			CPartFile* pCompletedFile = pFileInMap->IsKindOf(RUNTIME_CLASS(CPartFile)) ? static_cast<CPartFile*>(pFileInMap) : NULL;
			CDownloadListCtrl* pDownloadList = theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL ? theApp.emuledlg->transferwnd->GetDownloadList() : NULL;
			// Keep the completed object while it owns a visible download row.
			if (theApp.IsUiThread() && pCompletedFile != NULL && pCompletedFile->GetStatus() == PS_COMPLETE && !toadd->IsKindOf(RUNTIME_CLASS(CPartFile))
				&& theApp.sharedfiles != NULL && theApp.sharedfiles->GetLiveFileByID(pCompletedFile->GetFileHash()) == pCompletedFile
				&& pDownloadList != NULL && pDownloadList->m_ListItems.find(pCompletedFile) != pDownloadList->m_ListItems.end())
				return false;

			const CString strOldSharedFilePath(pFileInMap->GetFilePath());
			RemoveKnownFileFromVisibleControlsBeforeDelete(pFileInMap);

			RemoveSizeIndex(pFileInMap);
			if (pFileInMap->GetFileIdentifier().HasAICHHash())
				m_mapKnownFilesByAICH.RemoveKey(pFileInMap->GetFileIdentifier().GetAICHHash());
			m_Files_map[key] = toadd;
			AddSizeIndex(toadd);
			if (toadd->GetFileIdentifier().HasAICHHash())
				m_mapKnownFilesByAICH[toadd->GetFileIdentifier().GetAICHHash()] = toadd;
			if (theApp.sharedfiles != NULL) {
				bool bSharedMapReplaced = false;
				{
					CSingleLock listlock(&theApp.sharedfiles->m_mutWriteList, TRUE);
					CKnownFile* pSharedFileInMap = NULL;
					if (theApp.sharedfiles->m_Files_map.Lookup(key, pSharedFileInMap) && pSharedFileInMap == pFileInMap) {
						theApp.sharedfiles->m_Files_map[key] = toadd;
						bSharedMapReplaced = true;
					}
				}
				if (bSharedMapReplaced) {
					theApp.sharedfiles->UpdateSharedPathCache(toadd, strOldSharedFilePath);
					theApp.sharedfiles->RemoveKeywords(pFileInMap);
					theApp.sharedfiles->AddKeywords(toadd);
					theApp.sharedfiles->StoreWebSharedFileSnapshot(toadd);
					theApp.sharedfiles->m_bSharedFilesModelChangedSinceListUpdate = true;
				}
			}
			delete pFileInMap;
			theApp.QueueSharedFilesListChangedEvent(_T("known-file-replaced"));
			if (theApp.searchlist != NULL)
				theApp.searchlist->QueueKnownTypeRefreshForHash(toadd->GetFileHash());
			return true;
		}

		// If toadd exist and pFileInMap doesn't exist in drive we need to replace pFileInMap in map and add it to duplicates list. This way we'll prioritize adding existing files to shared file list.
		bool m_bToaddExists = (!toadd->GetFilePath().IsEmpty() && ::PathFileExists(toadd->GetFilePath()));
		if (m_bToaddExists && (pFileInMap->GetFilePath().IsEmpty() || !::PathFileExists(pFileInMap->GetFilePath()))) {
			CSingleLock slDuplicatesLock(&m_csDuplicatesLock, TRUE);
			m_duplicateFileList.remove(toadd);
			RemoveDupSizeIndex(toadd); // Sync duplicate index
			RemoveSizeIndex(toadd); // Sync size index

			const CString strOldFilePath = pFileInMap->GetFilePath();
			pFileInMap->SetFilePath(NULL); // Remove file path from pFileInMap since this file is not on disk.
			if (!strOldFilePath.IsEmpty() && theApp.sharedfiles != NULL)
				theApp.sharedfiles->UpdateSharedPathCache(pFileInMap, strOldFilePath);
			m_duplicateFileList.push_back(pFileInMap);
            AddDupSizeIndex(pFileInMap); // Sync duplicate index
			RemoveSizeIndex(pFileInMap); // Sync size index
			slDuplicatesLock.Unlock();
			
			m_Files_map[key] = toadd;
			AddSizeIndex(toadd); // Sync size index
			if (toadd->GetFileIdentifier().HasAICHHash())
				m_mapKnownFilesByAICH[toadd->GetFileIdentifier().GetAICHHash()] = toadd;

			if (bUpdateDownloadValidator && theApp.DownloadValidator != NULL)
				theApp.DownloadValidator->AddToMap(toadd->GetFileHash(), toadd->GetFileName(), toadd->GetFileSize());

			if (bLogDuplicateDetails) {
				AddDebugLogLine(DLP_VERYLOW, false, _T("%hs: File is removed from duplicate list: %s %I64u \"%s\""), __FUNCTION__, (LPCTSTR)md4str(toadd->GetFileHash()), (uint64)toadd->GetFileSize(), (LPCTSTR)EscPercent(toadd->GetFileName()));
				AddDebugLogLine(DLP_VERYLOW, false, _T("%hs: File is added to duplicate list: %s %I64u \"%s\""), __FUNCTION__, (LPCTSTR)md4str(pFileInMap->GetFileHash()), (uint64)pFileInMap->GetFileSize(), (LPCTSTR)EscPercent(pFileInMap->GetFileName()));
			}

			if (theApp.searchlist != NULL)
				theApp.searchlist->QueueKnownTypeRefreshForHash(toadd->GetFileHash());
			return true;
		}

		if (!m_bToaddExists)
			toadd->SetFilePath(NULL); // Remove file path from toadd since this file is not on disk.

		if (bLogDuplicateDetails) {
			AddDebugLogLine(DLP_VERYLOW, false, _T("%hs: File is already in known list: %s %I64u \"%s\""), __FUNCTION__, (LPCTSTR)md4str(pFileInMap->GetFileHash()), (uint64)pFileInMap->GetFileSize(), (LPCTSTR)EscPercent(pFileInMap->GetFileName()));
			AddDebugLogLine(DLP_VERYLOW, false, _T("%hs: File is added to duplicate list: %s %I64u \"%s\""), __FUNCTION__, (LPCTSTR)md4str(toadd->GetFileHash()), (uint64)toadd->GetFileSize(), (LPCTSTR)EscPercent(toadd->GetFileName()));
		}

		// We need to add toadd to m_duplicateFileList.
		CSingleLock slDuplicatesLock(&m_csDuplicatesLock, TRUE);
		m_duplicateFileList.push_back(toadd);
		AddDupSizeIndex(toadd); // Sync duplicate index
		slDuplicatesLock.Unlock();
		if (theApp.searchlist != NULL)
			theApp.searchlist->QueueKnownTypeRefreshForHash(toadd->GetFileHash());
		return true;
	}

	// This is not an expected case, doing this check just to be sure.
	// toadd will be added to the known file list below. We need to remove toadd from the duplicate list, if it is there. 
	if (pFileInDuplicatesList != NULL) {
		if (bLogDuplicateDetails)
			AddDebugLogLine(DLP_VERYLOW, false, _T("%hs: File is already in duplicates list, removing it: %s %I64u \"%s\""), __FUNCTION__, (LPCTSTR)md4str(toadd->GetFileHash()), (uint64)toadd->GetFileSize(), (LPCTSTR)EscPercent(toadd->GetFileName()));
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

	if (bUpdateDownloadValidator && theApp.DownloadValidator != NULL) // Startup load rebuilds the validator map once after known files are ready.
		theApp.DownloadValidator->AddToMap(toadd->GetFileHash(), toadd->GetFileName(), toadd->GetFileSize());
	if (theApp.searchlist != NULL)
		theApp.searchlist->QueueKnownTypeRefreshForHash(toadd->GetFileHash());

	return true;
}

CKnownFile* CKnownFileList::PromoteDuplicateForSharedFile(CKnownFile* pOldPrimary)
{
	if (pOldPrimary == NULL || theApp.sharedfiles == NULL)
		return NULL;

	CKnownFile* pPromotedFile = NULL;
	{
		CSingleLock slDuplicatesLock(&m_csDuplicatesLock, TRUE);
		for (KnownFileList::iterator it = m_duplicateFileList.begin(); it != m_duplicateFileList.end(); ++it) {
			CKnownFile* pCandidate = *it;
			if (pCandidate == NULL || pCandidate->IsPartFile() || !md4equ(pCandidate->GetFileHash(), pOldPrimary->GetFileHash()))
				continue;

			const CString strCandidatePath(pCandidate->GetFilePath());
			if (strCandidatePath.IsEmpty())
				continue;

			WIN32_FILE_ATTRIBUTE_DATA fad = {};
			if (!::GetFileAttributesEx(PreparePathForWin32LongPath(strCandidatePath), GetFileExInfoStandard, &fad))
				continue;
			if ((fad.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY)) != 0)
				continue;

			const uint64 uCandidateSize = (static_cast<uint64>(fad.nFileSizeHigh) << 32) | static_cast<uint64>(fad.nFileSizeLow);
			if (uCandidateSize != static_cast<uint64>(pCandidate->GetFileSize()))
				continue;

			CString strSharedDirectory(pCandidate->GetSharedDirectory());
			if (strSharedDirectory.IsEmpty())
				strSharedDirectory = pCandidate->GetPath();
			if (!theApp.sharedfiles->ShouldBeShared(strSharedDirectory, strCandidatePath, false))
				continue;

			pPromotedFile = pCandidate;
			m_duplicateFileList.erase(it);
			RemoveDupSizeIndex(pPromotedFile);
			break;
		}
	}

	if (pPromotedFile == NULL)
		return NULL;

	const CCKey key(pOldPrimary->GetFileHash());
	CKnownFile* pCurrentPrimary = NULL;
	const bool bHadPrimary = m_Files_map.Lookup(key, pCurrentPrimary) != FALSE && pCurrentPrimary != NULL;
	if (bHadPrimary && pCurrentPrimary != pPromotedFile) {
		RemoveSizeIndex(pCurrentPrimary);
		if (pCurrentPrimary->GetFileIdentifier().HasAICHHash())
			m_mapKnownFilesByAICH.RemoveKey(pCurrentPrimary->GetFileIdentifier().GetAICHHash());
	}

	m_Files_map[key] = pPromotedFile;
	AddSizeIndex(pPromotedFile);
	if (pPromotedFile->GetFileIdentifier().HasAICHHash())
		m_mapKnownFilesByAICH[pPromotedFile->GetFileIdentifier().GetAICHHash()] = pPromotedFile;

	if (bHadPrimary && pCurrentPrimary != pPromotedFile) {
		pCurrentPrimary->SetFilePath(NULL);
		CSingleLock slDuplicatesLock(&m_csDuplicatesLock, TRUE);
		m_duplicateFileList.push_back(pCurrentPrimary);
		AddDupSizeIndex(pCurrentPrimary);
	}

	if (theApp.DownloadValidator != NULL)
		theApp.DownloadValidator->AddToMap(pPromotedFile->GetFileHash(), pPromotedFile->GetFileName(), pPromotedFile->GetFileSize());
	if (theApp.searchlist != NULL)
		theApp.searchlist->QueueKnownTypeRefreshForHash(pPromotedFile->GetFileHash());
	theApp.QueueSharedFilesListChangedEvent(_T("known-duplicate-promoted"));
	return pPromotedFile;
}

CKnownFile* CKnownFileList::FindKnownFile(LPCTSTR filename, time_t date, uint64 size)
{
	{
		CSingleLock sl(&m_csSizeIndexLock, TRUE);
		CKnownFile* pFile = FindKnownFileInIndex(m_identityIndex, filename, date, size);
		if (pFile != NULL)
			return pFile;

		// Keep the fast identity index as the primary path, but fall back to the
		// legacy size bucket if an index entry was missed by an older mutation path.
		pFile = FindKnownFileInSizeIndex(m_sizeIndex, filename, date, size);
		if (pFile != NULL)
			return pFile;
	}

	return IsOnDuplicates(filename, date, size);
}

CKnownFile* CKnownFileList::FindKnownFileForSharedScan(LPCTSTR filename, time_t date, uint64 size)
{
	const size_t uMaxLegacyFallbackCandidates = 64;
	{
		CSingleLock sl(&m_csSizeIndexLock, TRUE);
		CKnownFile* pFile = FindKnownFileInIndex(m_identityIndex, filename, date, size);
		if (pFile != NULL)
			return pFile;

		pFile = FindKnownFileInSizeIndexLimited(m_sizeIndex, filename, date, size, uMaxLegacyFallbackCandidates);
		if (pFile != NULL)
			return pFile;
	}

	return IsOnDuplicatesForSharedScan(filename, date, size);
}

CKnownFile* CKnownFileList::FindKnownFileByPath(const CString& sFilePath) const
{
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		if (pair->value->GetFilePath().CompareNoCase(sFilePath) == 0)
			return pair->value;

	return NULL;
}


void CKnownFileList::ReindexKnownFile(CKnownFile* file, LPCTSTR oldFileName, uint64 oldSize)
{
	if (!file || !IsKnownFile(file))
		return;

	CSingleLock sl(&m_csSizeIndexLock, TRUE);
	auto oldSizeRange = m_sizeIndex.equal_range(oldSize);
	for (auto it = oldSizeRange.first; it != oldSizeRange.second; ++it)
		if (it->second == file) {
			m_sizeIndex.erase(it);
			break;
		}

	if (m_identityIndexedSizes.find(oldSize) != m_identityIndexedSizes.end()) {
		auto oldIdentityRange = m_identityIndex.equal_range(BuildIdentityIndexKey(oldFileName, oldSize));
		for (auto it = oldIdentityRange.first; it != oldIdentityRange.second; ++it)
			if (it->second == file) {
				m_identityIndex.erase(it);
				break;
			}
	}

	m_sizeIndex.emplace(file->GetFileSize(), file);
	if (m_identityIndexedSizes.find(file->GetFileSize()) != m_identityIndexedSizes.end())
		m_identityIndex.emplace(BuildIdentityIndexKey(file->GetFileName(), file->GetFileSize()), file);
	else if (m_sizeIndex.count(file->GetFileSize()) >= kIdentityIndexSizeBucketThreshold)
		BuildIdentityIndexForSizeLocked(file->GetFileSize());
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

	if (file) {
		for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
			if (file == pair->value)
				return true;

		CSingleLock slDuplicatesLock(const_cast<CCriticalSection*>(&m_csDuplicatesLock), TRUE);
		for (KnownFileList::const_iterator it = m_duplicateFileList.begin(); it != m_duplicateFileList.end(); ++it)
			if (file == *it)
				return true;
	}

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
		if (theApp.searchlist != NULL)
			theApp.searchlist->QueueKnownTypeRefreshForHash(hash);
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

bool CKnownFileList::RemoveKnownFile(CKnownFile* toRemove, bool bNotifySharedFilesList)
{
	if (!toRemove)
		return false;

	// If exactly this file is still shared, skip removal
	CShareableFile* pShared = theApp.sharedfiles->GetFileByID(toRemove->GetFileHash());
	if (pShared == toRemove)
		return false;

	// Remove from duplicate list only if this exact object is queued there.
	CSingleLock slDuplicatesLock(&m_csDuplicatesLock, TRUE);
	bool bDuplicatePointer = false;
	for (KnownFileList::const_iterator it = m_duplicateFileList.begin(); it != m_duplicateFileList.end(); ++it) {
		if (*it == toRemove) {
			bDuplicatePointer = true;
			break;
		}
	}
	if (bDuplicatePointer) {
		m_duplicateFileList.remove(toRemove);
		RemoveDupSizeIndex(toRemove); // Keep duplicate size index in sync
		CKnownFile* pFileInMap = NULL;
		if (m_Files_map.Lookup(CCKey(toRemove->GetFileHash()), pFileInMap) && pFileInMap == toRemove) {
			m_Files_map.RemoveKey(CCKey(toRemove->GetFileHash()));
			RemoveSizeIndex(toRemove);
			if (toRemove->GetFileIdentifier().HasAICHHash())
				m_mapKnownFilesByAICH.RemoveKey(toRemove->GetFileIdentifier().GetAICHHash());
		}
		theApp.DownloadValidator->RemoveFromMap(toRemove->GetFileHash(), toRemove->GetFileName(), toRemove->GetFileSize());
		delete toRemove;
		if (bNotifySharedFilesList)
			theApp.QueueSharedFilesListChangedEvent(_T("known-file-removed"));
		return true;
	}
	slDuplicatesLock.Unlock();

	// Otherwise remove only if this exact object is the primary known-file map value.
	CKnownFile* pPrimaryFile = NULL;
	if (m_Files_map.Lookup(CCKey(toRemove->GetFileHash()), pPrimaryFile) && pPrimaryFile == toRemove && m_Files_map.RemoveKey(CCKey(toRemove->GetFileHash()))) {
		RemoveSizeIndex(toRemove); // Keep size index in sync

		if (toRemove->GetFileIdentifier().HasAICHHash()) // Maintain AICH index
			m_mapKnownFilesByAICH.RemoveKey(toRemove->GetFileIdentifier().GetAICHHash());

		theApp.DownloadValidator->RemoveFromMap(toRemove->GetFileHash(), toRemove->GetFileName(), toRemove->GetFileSize());
		delete toRemove;
		if (bNotifySharedFilesList)
			theApp.QueueSharedFilesListChangedEvent(_T("known-file-removed"));
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
	bool bSharedFilesListChanged = false;
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

			RemoveKnownFileFromVisibleControlsBeforeDelete(cur_file, true);
			delete cur_file;
			bSharedFilesListChanged = true;
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

			RemoveKnownFileFromVisibleControlsBeforeDelete(cur_file, true);
			delete cur_file;
			bSharedFilesListChanged = true;
			slDuplicatesLock.Lock();
		} else
			++it;
	}
	slDuplicatesLock.Unlock();

	if (theApp.DownloadValidator != NULL)
		theApp.DownloadValidator->QueueReloadMap();
	if (bSharedFilesListChanged)
		theApp.QueueSharedFilesListChangedEvent(_T("known-history-cleared"));
}

uint64 CKnownFileList::BuildIdentityIndexKey(LPCTSTR filename, uint64 size) const
{
	uint64 key = 1469598103934665603ui64;
	for (int i = 0; i < 8; ++i) {
		key ^= (size >> (i * 8)) & 0xFF;
		key *= 1099511628211ui64;
	}
	if (filename != NULL) {
		for (LPCTSTR psz = filename; *psz != _T('\0'); ++psz) {
			key ^= static_cast<uint64>(_totlower(*psz));
			key *= 1099511628211ui64;
		}
	}
	return key;
}

CKnownFile* CKnownFileList::FindKnownFileInIndex(const KnownFileIdentityIndexMap& index, LPCTSTR filename, time_t date, uint64 size) const
{
	if (filename == NULL || *filename == _T('\0'))
		return NULL;

	auto range = index.equal_range(BuildIdentityIndexKey(filename, size));
	for (auto it = range.first; it != range.second; ++it)
		if (KnownFileMatches(it->second, filename, date, size))
			return it->second;
	return NULL;
}

CKnownFile* CKnownFileList::FindKnownFileInSizeIndex(const SizeIndexMap& index, LPCTSTR filename, time_t date, uint64 size) const
{
	if (filename == NULL || *filename == _T('\0'))
		return NULL;

	auto range = index.equal_range(size);
	for (auto it = range.first; it != range.second; ++it)
		if (KnownFileMatches(it->second, filename, date, size))
			return it->second;
	return NULL;
}

CKnownFile* CKnownFileList::FindKnownFileInSizeIndexLimited(const SizeIndexMap& index, LPCTSTR filename, time_t date, uint64 size, size_t uMaxCandidates) const
{
	if (filename == NULL || *filename == _T('\0') || uMaxCandidates == 0)
		return NULL;

	size_t uChecked = 0;
	auto range = index.equal_range(size);
	for (auto it = range.first; it != range.second && uChecked < uMaxCandidates; ++it, ++uChecked)
		if (KnownFileMatches(it->second, filename, date, size))
			return it->second;
	return NULL;
}

void CKnownFileList::BuildIdentityIndexForSizeLocked(uint64 size)
{
	if (m_identityIndexedSizes.find(size) != m_identityIndexedSizes.end())
		return;

	auto range = m_sizeIndex.equal_range(size);
	for (auto it = range.first; it != range.second; ++it) {
		CKnownFile* file = it->second;
		if (file != NULL)
			m_identityIndex.emplace(BuildIdentityIndexKey(file->GetFileName(), file->GetFileSize()), file);
	}
	m_identityIndexedSizes.insert(size);
}

void CKnownFileList::BuildDuplicateIdentityIndexForSize(uint64 size)
{
	if (m_dupIdentityIndexedSizes.find(size) != m_dupIdentityIndexedSizes.end())
		return;

	auto range = m_dupFileSizeIndex.equal_range(size);
	for (auto it = range.first; it != range.second; ++it) {
		CKnownFile* file = it->second;
		if (file != NULL)
			m_dupFileIdentityIndex.emplace(BuildIdentityIndexKey(file->GetFileName(), file->GetFileSize()), file);
	}
	m_dupIdentityIndexedSizes.insert(size);
}

inline void CKnownFileList::AddSizeIndex(CKnownFile* file)
{
	if (!file)
		return;

	CSingleLock sl(&m_csSizeIndexLock, TRUE);
	const uint64 size = file->GetFileSize();
	m_sizeIndex.emplace(size, file);
	if (m_identityIndexedSizes.find(size) != m_identityIndexedSizes.end())
		m_identityIndex.emplace(BuildIdentityIndexKey(file->GetFileName(), size), file);
	else if (m_sizeIndex.count(size) >= kIdentityIndexSizeBucketThreshold)
		BuildIdentityIndexForSizeLocked(size);
}

inline void CKnownFileList::RemoveSizeIndex(CKnownFile* file)
{
	if (!file)
		return;

	CSingleLock sl(&m_csSizeIndexLock, TRUE);
	const uint64 size = file->GetFileSize();
	auto range = m_sizeIndex.equal_range(size);
	for (auto it = range.first; it != range.second; ++it)
		if (it->second == file) {
			m_sizeIndex.erase(it); 
			break; 
		}

	if (m_identityIndexedSizes.find(size) != m_identityIndexedSizes.end()) {
		auto identityRange = m_identityIndex.equal_range(BuildIdentityIndexKey(file->GetFileName(), size));
		for (auto it = identityRange.first; it != identityRange.second; ++it)
			if (it->second == file) {
				m_identityIndex.erase(it);
				break;
			}
		if (m_sizeIndex.find(size) == m_sizeIndex.end())
			m_identityIndexedSizes.erase(size);
	}
}

void CKnownFileList::AddDupSizeIndex(CKnownFile* file)
{
	if (!file)
		return;

	IncrementDuplicateHashCount(file->GetFileHash());
	const uint64 size = file->GetFileSize();
	m_dupFileSizeIndex.emplace(size, file);
	if (m_dupIdentityIndexedSizes.find(size) != m_dupIdentityIndexedSizes.end())
		m_dupFileIdentityIndex.emplace(BuildIdentityIndexKey(file->GetFileName(), size), file);
	else if (m_dupFileSizeIndex.count(size) >= kIdentityIndexSizeBucketThreshold)
		BuildDuplicateIdentityIndexForSize(size);
}

void CKnownFileList::RemoveDupSizeIndex(CKnownFile* file)
{
	if (!file)
		return;

	bool bRemovedFromDuplicateIndex = false;
	const uint64 size = file->GetFileSize();
	auto range = m_dupFileSizeIndex.equal_range(size);
	for (auto it = range.first; it != range.second; ++it)
		if (it->second == file) {
			m_dupFileSizeIndex.erase(it);
			bRemovedFromDuplicateIndex = true;
			break;
		}

	if (m_dupIdentityIndexedSizes.find(size) != m_dupIdentityIndexedSizes.end()) {
		auto identityRange = m_dupFileIdentityIndex.equal_range(BuildIdentityIndexKey(file->GetFileName(), size));
		for (auto it = identityRange.first; it != identityRange.second; ++it)
			if (it->second == file) {
				m_dupFileIdentityIndex.erase(it);
				break;
			}
		if (m_dupFileSizeIndex.find(size) == m_dupFileSizeIndex.end())
			m_dupIdentityIndexedSizes.erase(size);
	}
	if (bRemovedFromDuplicateIndex)
		DecrementDuplicateHashCount(file->GetFileHash());
}

CKnownFile* CKnownFileList::IsOnDuplicatesForSharedScan(const LPCTSTR filename, time_t in_date, uint64 in_size)
{
	const size_t uMaxLegacyFallbackCandidates = 64;
	CSingleLock sl(&m_csDuplicatesLock, TRUE);
	CKnownFile* pFile = FindKnownFileInIndex(m_dupFileIdentityIndex, filename, in_date, in_size);
	if (pFile != NULL)
		return pFile;
	return FindKnownFileInSizeIndexLimited(m_dupFileSizeIndex, filename, in_date, in_size, uMaxLegacyFallbackCandidates);
}

CKnownFile* CKnownFileList::IsOnDuplicates(const LPCTSTR filename, time_t in_date, uint64 in_size)
{
	CSingleLock sl(&m_csDuplicatesLock, TRUE);
	CKnownFile* pFile = FindKnownFileInIndex(m_dupFileIdentityIndex, filename, in_date, in_size);
	if (pFile != NULL)
		return pFile;
	return FindKnownFileInSizeIndex(m_dupFileSizeIndex, filename, in_date, in_size);
}

uint32 CKnownFileList::DuplicatesCount(const uchar* hash)
{
	if (hash == NULL)
		return 0;
	CSingleLock slDuplicatesLock(&m_csDuplicatesLock, TRUE);
	uint32 uCount = 0;
	m_dupHashCounts.Lookup(CSKey(hash), uCount);
	return uCount;
}

void CKnownFileList::IncrementDuplicateHashCount(const uchar* hash)
{
	if (hash == NULL)
		return;
	const CSKey key(hash);
	uint32 uCount = 0;
	m_dupHashCounts.Lookup(key, uCount);
	m_dupHashCounts.SetAt(key, uCount + 1);
}

void CKnownFileList::DecrementDuplicateHashCount(const uchar* hash)
{
	if (hash == NULL)
		return;
	const CSKey key(hash);
	uint32 uCount = 0;
	if (!m_dupHashCounts.Lookup(key, uCount))
		return;
	if (uCount <= 1)
		m_dupHashCounts.RemoveKey(key);
	else
		m_dupHashCounts.SetAt(key, uCount - 1);
}

void CKnownFileList::PurgeDuplicateFile(CKnownFile* file)
{
	if (!file || m_bClearingKnownFiles)
		return;

	bool bRemovedFromDuplicateIndex = false;
	CSingleLock sl(&m_csDuplicatesLock, TRUE);
	m_duplicateFileList.remove(file);
	for (auto it = m_dupFileSizeIndex.begin(); it != m_dupFileSizeIndex.end();)
		if (it->second == file) {
			it = m_dupFileSizeIndex.erase(it);
			bRemovedFromDuplicateIndex = true;
		}
		else
			++it;
	for (auto it = m_dupFileIdentityIndex.begin(); it != m_dupFileIdentityIndex.end();)
		if (it->second == file)
			it = m_dupFileIdentityIndex.erase(it);
		else
			++it;
	if (bRemovedFromDuplicateIndex)
		DecrementDuplicateHashCount(file->GetFileHash());
	sl.Unlock();
}
