//This file is part of eMule AI
//Copyright (C)2020-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
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
#include <winioctl.h>
#include "OtherFunctions.h"
#include <timeapi.h>
#include "updownclient.h"
#include "PartFileWriteThread.h"
#include "emule.h"
#include "DownloadQueue.h"
#include "partfile.h"
#include "log.h"
#include "preferences.h"
#include "Ini2.h"
#include "Statistics.h"
#include <io.h>
#include "eMuleAI/SourceSaver.h"
#include "emuledlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif
#define RUN_STOP	0
#define RUN_IDLE	1
#define RUN_WORK	2
#define WAKEUP		((ULONG_PTR)(~0))

IMPLEMENT_DYNCREATE(CPartFileWriteThread, CWinThread)

namespace
{
	const DWORD PART_FILE_WRITE_THREAD_STOP_POLL_MS = 50;
	const DWORD PART_FILE_WRITE_THREAD_STOP_SLOW_LOG_MS = 5000;

	bool IsMissingBackupSourceError(DWORD dwError)
	{
		return dwError == ERROR_FILE_NOT_FOUND || dwError == ERROR_PATH_NOT_FOUND;
	}

	bool IsPartMetBackupSourceMissing(const CString& strSourcePath)
	{
		if (::GetFileAttributes(PreparePathForWin32LongPath(strSourcePath)) != INVALID_FILE_ATTRIBUTES)
			return false;
		return IsMissingBackupSourceError(::GetLastError());
	}

	bool IsIniFilePath(const CString& strPath)
	{
		return strPath.GetLength() >= 4 && strPath.Right(4).CompareNoCase(_T(".ini")) == 0;
	}

	bool TryGetNumberedPartFilePathParts(const CString& strPartFilePath, CString& strTempDir, int& iPartNumber)
	{
		int iSlash = strPartFilePath.ReverseFind(_T('\\'));
		const int iAltSlash = strPartFilePath.ReverseFind(_T('/'));
		if (iAltSlash > iSlash)
			iSlash = iAltSlash;

		const CString strFileName = iSlash >= 0 ? strPartFilePath.Mid(iSlash + 1) : strPartFilePath;
		if (strFileName.GetLength() <= 5 || strFileName.Right(5).CompareNoCase(_T(".part")) != 0)
			return false;

		const CString strNumber = strFileName.Left(strFileName.GetLength() - 5);
		if (strNumber.IsEmpty())
			return false;
		for (int i = 0; i < strNumber.GetLength(); ++i) {
			const TCHAR ch = strNumber[i];
			if (ch < _T('0') || ch > _T('9'))
				return false;
		}

		const long lPartNumber = _tcstol(strNumber, NULL, 10);
		if (lPartNumber <= 0 || lPartNumber > 0x7ffffffe)
			return false;

		strTempDir = iSlash >= 0 ? strPartFilePath.Left(iSlash + 1) : CString();
		iPartNumber = static_cast<int>(lPartNumber);
		return true;
	}

	void BuildNumberedPartFilePath(const CString& strTempDir, int iPartNumber, CString& strPartFilePath)
	{
		strPartFilePath.Format(_T("%s%03i.part"), (LPCTSTR)strTempDir, iPartNumber);
	}

	void TraceAsyncDiskWriteResult(LPCTSTR pszName, LONG lGeneration, LPCTSTR pszResult, LPCTSTR pszReason, LPCTSTR pszTempPath, LPCTSTR pszFinalPath, bool bShutdownFallback, DWORD dwLastError = 0)
	{
		AddDebugLogLine(DLP_LOW, false, _T("[AsyncDiskWrite] name=\"%s\" generation=%ld result=%s reason=%s error=%lu shutdownFallback=%u temp=\"%s\" final=\"%s\"\n"),
			pszName != NULL ? pszName : _T(""), lGeneration, pszResult != NULL ? pszResult : _T(""), pszReason != NULL ? pszReason : _T(""), dwLastError,
			bShutdownFallback ? 1U : 0U, pszTempPath != NULL ? pszTempPath : _T(""), pszFinalPath != NULL ? pszFinalPath : _T(""));
		theApp.QueueAsyncDiskWriteResultEvent(pszName, lGeneration, pszResult, pszReason, pszTempPath, pszFinalPath, bShutdownFallback, dwLastError);
	}

	static const INT_PTR kMaxQueuedAsyncDiskWrites = 512;
	static const INT_PTR kMaxDeferredAsyncDiskWrites = 64;
	static const INT_PTR kMaxQueuedPartFileDiskJobs = 512;
	static const LONG kMaxPendingPartFileCreateJobs = 512;
	static const LONG kMaxPendingPartFileDeleteJobs = 512;

	class CInterlockedDecrementOnExit
	{
	public:
		explicit CInterlockedDecrementOnExit(volatile LONG* pValue)
			: m_pValue(pValue)
		{
		}
		~CInterlockedDecrementOnExit()
		{
			if (m_pValue != NULL)
				InterlockedDecrement(m_pValue);
		}
	private:
		CInterlockedDecrementOnExit(const CInterlockedDecrementOnExit&);
		CInterlockedDecrementOnExit& operator=(const CInterlockedDecrementOnExit&);
		volatile LONG* m_pValue;
	};
}

FlushPartMetData::FlushPartMetData()
	: lGeneration()
	, bDontOverrideBak()
	, bDeferredInitialPartMetSave()
	, uPartFileVersion()
{
	md4clr(abyMD4Hash);
}

FlushPartMetData::~FlushPartMetData()
{
	for (std::vector<CTag*>::iterator it = taglist.begin(); it != taglist.end(); ++it)
		delete *it;
	taglist.clear();
}

AsyncDiskWriteData::AsyncDiskWriteData()
	: lGeneration()
	, plGeneration()
	, bShutdownFallback(false)
	, eShutdownPolicy(AsyncDiskWriteShutdownSyncFallback)
	, eConflictPolicy(AsyncDiskWriteConflictOrdered)
	, eReplacePolicy(AsyncDiskWriteReplaceFinal)
{
}

PartFileCreateData::PartFileCreateData()
	: uRuntimeID()
	, bSparsePartFile()
{
	md4clr(abyHash);
}

PartFileCreateResult::PartFileCreateResult()
	: uRuntimeID()
	, hFile(INVALID_HANDLE_VALUE)
	, dwFileAttributes()
	, tCreated()
	, tLastModified()
	, dwError()
{
	md4clr(abyHash);
}

PartFileDeleteData::PartFileDeleteData()
	: uDownloadRemoveSequence(0)
	, uDownloadRemoveCorrelationId(0)
{
}

CPartFileWriteThread::CPartFileWriteThread()
	: m_bVerbose(thePrefs.GetVerbose())
	, m_iCommitFiles(thePrefs.GetCommitFiles())
	, m_eventThreadEnded(FALSE, TRUE)
	, m_hPort()
	, m_Run(RUN_STOP)
	, m_bNewData()
	, m_lPartFileCreateJobsPending()
	, m_lPartFileDeleteJobsPending()
{
		AfxBeginThread(RunProc, (LPVOID)this, THREAD_PRIORITY_BELOW_NORMAL);
}

CPartFileWriteThread::~CPartFileWriteThread()
{
	ASSERT(!m_hPort && !m_Run);

	CSingleLock sFlushListLock(&m_lockFlushList, TRUE);
	while (!m_FlushList.IsEmpty()) {
		ToWrite currItem = m_FlushList.RemoveHead();
		delete currItem.pFlushPartMetData;
		delete currItem.pSaveSourcesData;
		delete currItem.pAsyncDiskWriteData;
		delete currItem.pPartFileCreateData;
		delete currItem.pPartFileDeleteData;
		if (currItem.bOwnsBuffer)
			delete currItem.pBuffer;
	}

	while (!m_listToWrite.IsEmpty()) {
		ToWrite currItem = m_listToWrite.RemoveHead();
		delete currItem.pFlushPartMetData;
		delete currItem.pSaveSourcesData;
		delete currItem.pAsyncDiskWriteData;
		delete currItem.pPartFileCreateData;
		delete currItem.pPartFileDeleteData;
		if (currItem.bOwnsBuffer)
			delete currItem.pBuffer;
	}

	while (!m_deferredAsyncDiskWriteJobs.IsEmpty())
		delete m_deferredAsyncDiskWriteJobs.RemoveHead();

	while (!m_partFileCreateResults.IsEmpty()) {
		PartFileCreateResult* pResult = m_partFileCreateResults.RemoveHead();
		if (pResult != NULL && pResult->hFile != INVALID_HANDLE_VALUE)
			::CloseHandle(pResult->hFile);
		delete pResult;
	}
}

UINT AFX_CDECL CPartFileWriteThread::RunProc(LPVOID pParam)
{
	DbgSetThreadName("PartWriteThread");
	return pParam ? static_cast<CPartFileWriteThread*>(pParam)->RunInternal() : 1;
}

void CPartFileWriteThread::EndThread()
{
	if (m_eventThreadEnded.Lock(0))
		return;

	InterlockedExchange8(&m_Run, RUN_STOP);
	const DWORD dwStarted = ::GetTickCount();
	bool bStopPosted = false;
	bool bStopPostFailureLogged = false;
	bool bSlowLogged = false;
	for (;;) {
		HANDLE hPort = m_hPort;
		if (!bStopPosted && hPort != NULL) {
			if (::PostQueuedCompletionStatus(hPort, 0, 0, NULL))
				bStopPosted = true;
			else if (!bStopPostFailureLogged) {
				AddDebugLogLine(DLP_HIGH, false, _T("Part file write thread stop signal failed. error=%lu\n"), ::GetLastError());
				bStopPostFailureLogged = true;
			}
		}
		if (m_eventThreadEnded.Lock(PART_FILE_WRITE_THREAD_STOP_POLL_MS))
			return;
		if (!bSlowLogged && ::GetTickCount() - dwStarted >= PART_FILE_WRITE_THREAD_STOP_SLOW_LOG_MS) {
			AddDebugLogLine(DLP_HIGH, false, _T("Part file write thread shutdown is still waiting. elapsed=%lu run=%d port=%p stopPosted=%u\n"), ::GetTickCount() - dwStarted, (int)m_Run, hPort, bStopPosted ? 1U : 0U);
			bSlowLogged = true;
		}
		InterlockedExchange8(&m_Run, RUN_STOP);
	}
}

UINT CPartFileWriteThread::RunInternal()
{
	m_hPort = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 1);
	if (!m_hPort) {
		const DWORD dwError = ::GetLastError();
		m_Run = RUN_STOP;
		m_eventThreadEnded.SetEvent();
		return dwError;
	}

	DWORD dwWrite = 0;
	ULONG_PTR completionKey = 0;
	OverlappedWrite_Struct* pCurIO = NULL;
	m_Run = RUN_IDLE;
	while (m_Run
		&& ::GetQueuedCompletionStatus(m_hPort, &dwWrite, &completionKey, (LPOVERLAPPED*)&pCurIO, INFINITE)
		&& completionKey)
	{
		m_Run = RUN_WORK;
		//move buffer lists into the local storage
		{
			CSingleLock sFlushListLock(&m_lockFlushList, TRUE);
			if (!m_FlushList.IsEmpty() || !m_deferredAsyncDiskWriteJobs.IsEmpty()) {
				CSingleLock sDeletedFilesListLock(&m_DeletedFilesListLock, TRUE);
				while (!m_FlushList.IsEmpty()) {
					const ToWrite& item = m_FlushList.RemoveHead();
					CPartFile* pFile = item.pFile;
					if (item.pAsyncDiskWriteData || item.pPartFileCreateData || item.pPartFileDeleteData || (pFile && !IsDeletedPartFile(pFile, item.uRuntimeID)))
						m_listToWrite.AddTail(item);
					else
						CleanUp(item, NULL);
				}
				MoveDeferredAsyncDiskWriteJobsToDrainList(m_listToWrite);
				InterlockedExchange8(&m_bNewData, 0);
			}
		}

		//start new I/O
		WriteBuffers();
		//completed I/O
		do {
			if (!completionKey)
				break;
			if (completionKey != WAKEUP) //ignore wakeups
				WriteCompletionRoutine(dwWrite, pCurIO);
		} while (::GetQueuedCompletionStatus(m_hPort, &dwWrite, &completionKey, (LPOVERLAPPED*)&pCurIO, 0));
		PruneDeletedFilesList();

		if (!completionKey) //thread termination
			break;
		m_Run = RUN_IDLE;
		if (InterlockedExchange8(&m_bNewData, 0) && m_listPendingIO.IsEmpty())
			PostQueuedCompletionStatus(m_hPort, 0, WAKEUP, NULL);
	}
	m_Run = RUN_STOP;
	DrainPendingAsyncDiskSnapshotsForShutdown();

	//Improper termination of asynchronous I/O follows...
	//close file handles to release I/O completion port
	while (!m_listPendingIO.IsEmpty())
		WriteCompletionRoutine(0, m_listPendingIO.RemoveHead());

	::CloseHandle(m_hPort);
	m_hPort = 0;

	m_eventThreadEnded.SetEvent();
	return 0;
}

void CPartFileWriteThread::DrainPendingAsyncDiskSnapshotsForShutdown()
{
	CList<ToWrite> jobsToDrain;
	{
		CSingleLock sFlushListLock(&m_lockFlushList, TRUE);
		for (POSITION pos = m_FlushList.GetHeadPosition(); pos != NULL;) {
			POSITION posCurrent = pos;
			ToWrite item = m_FlushList.GetNext(pos);
			if (item.pAsyncDiskWriteData == NULL && item.pPartFileCreateData == NULL && item.pPartFileDeleteData == NULL)
				continue;
			m_FlushList.RemoveAt(posCurrent);
			jobsToDrain.AddTail(item);
		}
		MoveDeferredAsyncDiskWriteJobsToDrainList(jobsToDrain);
	}

	for (POSITION pos = m_listToWrite.GetHeadPosition(); pos != NULL;) {
		POSITION posCurrent = pos;
		ToWrite item = m_listToWrite.GetNext(pos);
		if (item.pAsyncDiskWriteData == NULL && item.pPartFileCreateData == NULL && item.pPartFileDeleteData == NULL)
			continue;
		m_listToWrite.RemoveAt(posCurrent);
		jobsToDrain.AddTail(item);
	}

	while (!jobsToDrain.IsEmpty()) {
		ToWrite item = jobsToDrain.RemoveHead();
		if (item.pAsyncDiskWriteData != NULL) {
			item.pAsyncDiskWriteData->bShutdownFallback = true;
			(void)WriteDiskSnapshotNow(*item.pAsyncDiskWriteData, true);
		} else if (item.pPartFileCreateData != NULL)
			ProcessPartFileCreate(item.pPartFileCreateData);
		else if (item.pPartFileDeleteData != NULL)
			ProcessPartFileDelete(item.pPartFileDeleteData);
		CleanUp(item, NULL);
	}
}

void CPartFileWriteThread::WriteBuffers()
{
	//process internal list
	while (!m_listToWrite.IsEmpty() && m_Run) {
		const ToWrite& item = m_listToWrite.RemoveHead();
		PartFileBufferedData* pBuffer = item.pBuffer;

		CPartFile* pFile = item.pFile;
		try {
			if (item.pAsyncDiskWriteData) {
				WriteDiskSnapshot(item.pAsyncDiskWriteData);
				CleanUp(item, NULL);
				continue;
			}
			if (item.pPartFileCreateData) {
				ProcessPartFileCreate(item.pPartFileCreateData);
				CleanUp(item, NULL);
				continue;
			}
			if (item.pPartFileDeleteData) {
				ProcessPartFileDelete(item.pPartFileDeleteData);
				CleanUp(item, NULL);
				continue;
			}
			CSingleLock sDeletedFilesListLock(&m_DeletedFilesListLock, TRUE);
			if (!pFile || IsDeletedPartFile(pFile, item.uRuntimeID)) { // File is invalid or deleted
				CleanUp(item, NULL); // Since file isn't valid, we pass NULL here not to make unnecessary file checks again.
				continue;
			}
			CSingleLock sPartFileDeleteLock(&pFile->m_PartFileDeleteLock, TRUE); // Lock part file to protect it being deleted by the mail thread. Otherwise it would cause exception in this thread.
			sDeletedFilesListLock.Unlock();

			CSingleLock sPartStatusLock(&pFile->m_PartStatusLock, TRUE);
			if (theApp.IsClosing() || pFile->status == PS_WAITINGFORHASH || pFile->status == PS_HASHING || pFile->status == PS_COMPLETE || pFile->status == PS_COMPLETING) { // App closing or invalid file state
				CleanUp(item, pFile);
				continue;
			}
			sPartStatusLock.Unlock();

			if (pBuffer) { // Write part file
				try {
					CSingleLock sHPartFileLock(&pFile->m_HPartFileLock, TRUE);
					if (pFile->m_hpartfile && (HANDLE)pFile->m_hpartfile != INVALID_HANDLE_VALUE) {
						if (pFile->m_iAllocationSize && pFile->m_iAllocationSize != pFile->m_hpartfile.GetLength())
							pFile->m_hpartfile.SetLength(pFile->m_iAllocationSize); // may throw 'diskFull'
					} else {
						CleanUp(item, pFile);
						continue; // Handle is not valid, skip
					}
				} catch (CException *ex) {
					ex->Delete();
					ASSERT(0);
				} catch (...) {
					ASSERT(0);
				}

				if (AddFile(pFile)) {
					//initiate write
					OverlappedWrite_Struct *pOvWrite = new OverlappedWrite_Struct;
					pOvWrite->oOverlap.Internal = 0;
					pOvWrite->oOverlap.InternalHigh = 0;
					*(uint64*)&pOvWrite->oOverlap.Offset = pBuffer->start;
					pOvWrite->oOverlap.hEvent = 0;
					pOvWrite->pFile = pFile;
					pOvWrite->uRuntimeID = item.uRuntimeID;
					pOvWrite->pBuffer = pBuffer;
					pOvWrite->pos = NULL;
					pOvWrite->bOwnsBuffer = item.bOwnsBuffer;
					try {
						pOvWrite->pos = m_listPendingIO.AddTail(pOvWrite);
					} catch (...) {
						delete pOvWrite;
						throw;
					}

					static const BYTE zero = 0;
					if (!::WriteFile(pFile->m_hWrite, pBuffer->data ? pBuffer->data : &zero, (DWORD)(pBuffer->end - pBuffer->start + 1), NULL, (LPOVERLAPPED)pOvWrite)) {
						DWORD dwError = ::GetLastError();
						if (dwError != ERROR_IO_PENDING) {
							m_listPendingIO.RemoveAt(pOvWrite->pos);
							delete pOvWrite;
							if (item.bOwnsBuffer)
								delete item.pBuffer;
							else if (item.pBuffer->data) { //check for an allocation request
								item.pBuffer->dwError = dwError;
								item.pBuffer->flushed = PB_ERROR;
							}
							RemFile(pFile);
							return;
						}
					}
					++pFile->m_iWrites;
				} else {
					theApp.QueueDebugLogLineEx(LOG_ERROR, _T("WriteBuffers error: CPartFile cannot be written"));
					CleanUp(item, pFile);
				}
			} 
			else if (item.pFlushPartMetData) { // Flush part met file
				FlushPartMetData* pData = item.pFlushPartMetData;
				CSingleLock sSavePartFileLock(&pFile->m_SavePartFileLock, TRUE);
				CSingleLock sSavePartFilePrefsLock(&m_lockSavePartFilePrefs, TRUE);
				if (pData == NULL || pData->lGeneration != pFile->GetPartMetSaveGeneration()) {
					CleanUp(item, pFile);
					continue;
				}

				if (pData->strFullName.Right(9) != _T(".part.met")) {
					CleanUp(item, pFile);
					continue;
				}
				const CString searchpath(RemoveFileExtension(pData->strFullName));
				CFileFind ff;
				BOOL bFound = ff.FindFile(searchpath);
				if (bFound)
					ff.FindNextFile();
				if (!bFound || ff.IsDirectory()) {
					theApp.QueueLogLine(false, GetResString(_T("ERR_SAVEMET")) + _T(" - %s"), (LPCTSTR)EscPercent(pData->strPartMetFileName), (LPCTSTR)EscPercent(pData->strFileName), (LPCTSTR)GetResString(_T("ERR_PART_FNF")));
					CleanUp(item, pFile);
					continue;
				}

				if (!theApp.CanWritePartMetFiles(pData->strTmpPath)) {
					CleanUp(item, pFile);
					continue;
				}

				time_t m_tLastModified = 0;
				time_t m_tUtcLastModified = 0;
				FILETIME lwtime;
				ff.GetLastWriteTime(&lwtime);
				m_tLastModified = (time_t)FileTimeToUnixTime(lwtime);
				if (m_tLastModified <= 0)
					m_tLastModified = (time_t)-1;
				m_tUtcLastModified = m_tLastModified;
				if (m_tUtcLastModified == (time_t)-1) {
					if (m_bVerbose)
						theApp.QueueDebugLogLine(false, _T("Failed to get file date of \"%s\" (%s)"), (LPCTSTR)EscPercent(pData->strPartMetFileName), (LPCTSTR)EscPercent(pData->strFileName));
				} else
					AdjustNTFSDaylightFileTime(m_tUtcLastModified, ff.GetFilePath());
				ff.Close();

				const CString strTmpFile(pData->strFullName + PARTMET_TMP_EXT);

				CSafeBufferedFile file;
				CFileException fex;
				if (!file.Open(strTmpFile, CFile::modeWrite | CFile::modeCreate | CFile::typeBinary | CFile::shareDenyWrite, &fex)) {
					CString s;
					s.Format(GetResString(_T("ERR_SAVEMET")), (LPCTSTR)pData->strPartMetFileName, (LPCTSTR)EscPercent(pData->strFileName));
					theApp.QueueLogLine(false, _T("%s%s"), (LPCTSTR)EscPercent(s), (LPCTSTR)EscPercent(CExceptionStrDash(fex)));
					(void)theApp.CanWritePartMetFiles(pData->strTmpPath, true);
					CleanUp(item, pFile);
					continue;
				}
				::setvbuf(file.m_pStream, NULL, _IOFBF, 16384);

				try {
					file.WriteUInt8(pData->uPartFileVersion);
					file.WriteUInt32((uint32)m_tUtcLastModified);
					file.WriteHash16(pData->abyMD4Hash);
					const UINT uParts = (UINT)pData->aMD4HashSet.size();
					file.WriteUInt16((uint16)uParts);
					for (UINT i = 0; i < uParts; ++i)
						file.WriteHash16(pData->aMD4HashSet[i].abyHash);

					UINT uTagCount = 0;
					ULONG uTagCountFilePos = (ULONG)file.GetPosition();
					file.WriteUInt32(uTagCount);

					CTag nametag(FT_FILENAME, pData->strFileName);
					nametag.WriteTagToFile(file, UTF8strOptBOM);
					++uTagCount;

					for (std::vector<CTag*>::const_iterator it = pData->taglist.begin(); it != pData->taglist.end(); ++it) {
						CTag* pTag = *it;
						if (pTag != NULL && (pTag->IsStr() || pTag->IsInt() || pTag->IsInt64() || pTag->IsBlob())) {
							pTag->WriteTagToFile(file, UTF8strOptBOM);
							++uTagCount;
						}
					}

					file.Seek(uTagCountFilePos, CFile::begin);
					file.WriteUInt32(uTagCount);
					file.SeekToEnd();
					CommitAndClose(file);
				} catch (CFileException* ex) {
					CString strError;
					strError.Format(GetResString(_T("ERR_SAVEMET")), (LPCTSTR)pData->strPartMetFileName, (LPCTSTR)EscPercent(pData->strFileName));
					theApp.QueueLogLine(false, _T("%s%s"), (LPCTSTR)EscPercent(strError), (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
					ex->Delete();
					file.Abort();
					(void)DeleteFileLongPath(strTmpFile);
					(void)theApp.CanWritePartMetFiles(pData->strTmpPath, true);
					CleanUp(item, pFile);
					continue;
				} catch (...) {
					file.Abort();
					(void)DeleteFileLongPath(strTmpFile);
					(void)theApp.CanWritePartMetFiles(pData->strTmpPath, true);
					CleanUp(item, pFile);
					continue;
				}

				if (pData->lGeneration != pFile->GetPartMetSaveGeneration()) {
					(void)DeleteFileLongPath(strTmpFile);
					CleanUp(item, pFile);
					continue;
				}

				const CString strBakFile(pData->strFullName + PARTMET_BAK_EXT);
				const CString strBakTmpFile(strBakFile + PARTMET_TMP_EXT);
				DWORD dwBakError = ERROR_SUCCESS;
				if (!IsPartMetBackupSourceMissing(pData->strFullName) && !CopyFileToTempAndReplace(pData->strFullName, strBakFile, strBakTmpFile, pData->bDontOverrideBak, &dwBakError)) {
					if (!pData->bDontOverrideBak && theApp.CanWritePartMetFiles(pData->strTmpPath, true)) {
						theApp.QueueDebugLogLine(false, _T("Failed to create backup of %s (%s) - %s"),
							(LPCTSTR)EscPercent(pData->strFullName), (LPCTSTR)EscPercent(pData->strFileName), (LPCTSTR)EscPercent(GetErrorMessage(dwBakError)));
					}
				}

				DWORD dwReplaceError = ERROR_SUCCESS;
				if (!ReplaceFileAtomically(strTmpFile, pData->strFullName, &dwReplaceError)) {
					(void)theApp.CanWritePartMetFiles(pData->strTmpPath, true);
					if (m_bVerbose)
						theApp.QueueDebugLogLine(false, _T("Failed to move temporary part.met file \"%s\" to \"%s\" - %s"),
							(LPCTSTR)EscPercent(strTmpFile), (LPCTSTR)EscPercent(pData->strFullName), (LPCTSTR)EscPercent(GetErrorMessage(dwReplaceError)));

					CString strError;
					strError.Format(GetResString(_T("ERR_SAVEMET")), (LPCTSTR)pData->strPartMetFileName, (LPCTSTR)EscPercent(pData->strFileName));
					strError.AppendFormat(_T(" - %s"), (LPCTSTR)EscPercent(GetErrorMessage(dwReplaceError)));
					theApp.QueueLogLine(false, _T("%s"), (LPCTSTR)strError);
					CleanUp(item, pFile);
					continue;
				}
				CleanUp(item, pFile);
			} else if (item.pSaveSourcesData) { // Save sources
				SaveSourcesData* pData = item.pSaveSourcesData;
				CSingleLock sSaveSourcesLock(&pFile->m_SaveSourcesLock, TRUE);
				if (pData == NULL) {
					CleanUp(item, pFile);
					continue;
				}

				const CString strTmpSourcesFilePath(pData->strSourcesFilePath + _T(".tmp"));
				if (pData->lGeneration != pFile->GetSaveSourcesGeneration()) {
					(void)DeleteFileLongPath(strTmpSourcesFilePath);
					TraceAsyncDiskWriteResult(_T("SLS"), pData->lGeneration, _T("skipped"), _T("stale-generation"), strTmpSourcesFilePath, pData->strSourcesFilePath, false);
					CleanUp(item, pFile);
					continue;
				}

				CString strLine;
				CStdioFile f;
				CFileException fex;
				if (!f.Open(PreparePathForWin32LongPath(strTmpSourcesFilePath), CFile::modeCreate | CFile::modeWrite | CFile::typeText, &fex)) {
					TraceAsyncDiskWriteResult(_T("SLS"), pData->lGeneration, _T("failed"), _T("open-temp"), strTmpSourcesFilePath, pData->strSourcesFilePath, false);
					CleanUp(item, pFile);
					continue;
				}

				try {
					f.WriteString(_T("#format: SourceIP/LowID:SourcePort,ExpirationDate(yymmddhhmm),SourceExchangeVersion,ServerIP,ServerPort;\r\n"));
					f.WriteString(_T("#") + pData->strED2kLink + _T("\r\n"));

					for (std::vector<SSaveSourceSnapshotRow>::const_iterator it = pData->rows.begin(); it != pData->rows.end(); ++it) {
						if (it->sourceID)
							strLine.Format(_T("%i:%i,%s,%i,%s:%i;\r\n"), it->sourceID, it->sourcePort, it->expiration, it->nSrcExchangeVer, ipstr(it->serverip), it->serverport);
						else
							strLine.Format(_T("%s:%i,%s,%i,%s:%i;\r\n"), it->sourceIP.ToStringC(), it->sourcePort, it->expiration, it->nSrcExchangeVer, ipstr(it->serverip), it->serverport);
						f.WriteString(strLine);
					}

					f.Close();
				} catch (CFileException *ex) {
					ex->Delete();
					f.Abort();
					(void)DeleteFileLongPath(strTmpSourcesFilePath);
					TraceAsyncDiskWriteResult(_T("SLS"), pData->lGeneration, _T("failed"), _T("write-temp"), strTmpSourcesFilePath, pData->strSourcesFilePath, false);
					CleanUp(item, pFile);
					continue;
				} catch (...) {
					f.Abort();
					(void)DeleteFileLongPath(strTmpSourcesFilePath);
					TraceAsyncDiskWriteResult(_T("SLS"), pData->lGeneration, _T("failed"), _T("write-temp"), strTmpSourcesFilePath, pData->strSourcesFilePath, false);
					CleanUp(item, pFile);
					continue;
				}

				if (pData->lGeneration != pFile->GetSaveSourcesGeneration()) {
					(void)DeleteFileLongPath(strTmpSourcesFilePath);
					TraceAsyncDiskWriteResult(_T("SLS"), pData->lGeneration, _T("skipped"), _T("stale-generation"), strTmpSourcesFilePath, pData->strSourcesFilePath, false);
					CleanUp(item, pFile);
					continue;
				}
				if (!MoveFileExLongPath(strTmpSourcesFilePath, pData->strSourcesFilePath, MOVEFILE_REPLACE_EXISTING)) {
					(void)DeleteFileLongPath(strTmpSourcesFilePath);
					TraceAsyncDiskWriteResult(_T("SLS"), pData->lGeneration, _T("failed"), _T("publish-final"), strTmpSourcesFilePath, pData->strSourcesFilePath, false);
					CleanUp(item, pFile);
					continue;
				}
				TraceAsyncDiskWriteResult(_T("SLS"), pData->lGeneration, _T("success"), _T("published"), strTmpSourcesFilePath, pData->strSourcesFilePath, false);
				CleanUp(item, pFile);
			}

		} catch (CException *ex) {
			if (m_bVerbose)
				theApp.QueueDebugLogLine(false, _T("CPartFileWriteThread::WriteBuffers - %s"), (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
			ex->Delete();
			CleanUpAfterException(item);
			ASSERT(0);
		} catch (...) {
			if (m_bVerbose)
				theApp.QueueDebugLogLine(false, _T("CPartFileWriteThread::WriteBuffers exception occured"));
			CleanUpAfterException(item);
			ASSERT(0);
		}
	}
}


bool CPartFileWriteThread::IsDeletedPartFile(const CPartFile* pFile, PartFileRuntimeID uRuntimeID) const
{
	for (POSITION pos = m_DeletedFilesList.GetHeadPosition(); pos != NULL;) {
		const DeletedPartFile deletedFile = m_DeletedFilesList.GetNext(pos);
		if (deletedFile.pFile == pFile && deletedFile.uRuntimeID == uRuntimeID)
			return true;
	}
	return false;
}

bool CPartFileWriteThread::HasOutstandingPartFileWork(const CPartFile* pFile, PartFileRuntimeID uRuntimeID) const
{
	if (pFile == NULL)
		return false;
	for (POSITION pos = m_FlushList.GetHeadPosition(); pos != NULL;) {
		const ToWrite item = m_FlushList.GetNext(pos);
		if (item.pFile == pFile && item.uRuntimeID == uRuntimeID)
			return true;
	}
	for (POSITION pos = m_listToWrite.GetHeadPosition(); pos != NULL;) {
		const ToWrite item = m_listToWrite.GetNext(pos);
		if (item.pFile == pFile && item.uRuntimeID == uRuntimeID)
			return true;
	}
	for (POSITION pos = m_listPendingIO.GetHeadPosition(); pos != NULL;) {
		const OverlappedWrite_Struct* pPendingIO = m_listPendingIO.GetNext(pos);
		if (pPendingIO != NULL && pPendingIO->pFile == pFile && pPendingIO->uRuntimeID == uRuntimeID)
			return true;
	}
	return false;
}

void CPartFileWriteThread::PruneDeletedFilesList()
{
	CSingleLock sFlushListLock(&m_lockFlushList, TRUE);
	CSingleLock sDeletedFilesListLock(&m_DeletedFilesListLock, TRUE);
	for (POSITION pos = m_DeletedFilesList.GetHeadPosition(); pos != NULL;) {
		POSITION posCurrent = pos;
		const DeletedPartFile deletedFile = m_DeletedFilesList.GetNext(pos);
		if (!HasOutstandingPartFileWork(deletedFile.pFile, deletedFile.uRuntimeID))
			m_DeletedFilesList.RemoveAt(posCurrent);
	}
}

bool CPartFileWriteThread::AddDiskWriteJob(AsyncDiskWriteData* pData, bool* pbRejectedByQueuePressure)
{
	if (pbRejectedByQueuePressure != NULL)
		*pbRejectedByQueuePressure = false;
	if (pData == NULL)
		return false;
	if (!IsRunning() || theApp.IsClosing() || theApp.GetBackendLifecycleState() >= CemuleApp::BackendLifecycleDrainingDiskIo)
		return false;

	CSingleLock sFlushListLock(&m_lockFlushList, TRUE);
	if (pData->eConflictPolicy == AsyncDiskWriteConflictLastSnapshotWins && !pData->strFinalPath.IsEmpty()) {
		for (POSITION pos = m_FlushList.GetHeadPosition(); pos != NULL;) {
			POSITION posCurrent = pos;
			ToWrite& item = m_FlushList.GetNext(pos);
			AsyncDiskWriteData* pQueuedData = item.pAsyncDiskWriteData;
			if (pQueuedData != NULL && pQueuedData->eConflictPolicy == AsyncDiskWriteConflictLastSnapshotWins && pQueuedData->strFinalPath.CompareNoCase(pData->strFinalPath) == 0) {
				delete pQueuedData;
				m_FlushList.RemoveAt(posCurrent);
			}
		}
		RemoveDeferredAsyncDiskWriteJobsByFinalPath(pData->strFinalPath);
	}
	if (m_FlushList.GetCount() >= kMaxQueuedAsyncDiskWrites) {
		if (AddDeferredAsyncDiskWriteJob(pData)) {
			WakeUpCall();
			return true;
		}
		if (pbRejectedByQueuePressure != NULL)
			*pbRejectedByQueuePressure = true;
		AddDebugLogLine(DLP_HIGH, false, _T("Async disk write queue pressure rejected snapshot without dropping unrelated targets. count=%Id deferred=%Id\n"), m_FlushList.GetCount(), m_deferredAsyncDiskWriteJobs.GetCount());
		return false;
	}
	m_FlushList.AddTail(ToWrite{ NULL, 0, NULL, NULL, NULL, pData, NULL, NULL, false });
	WakeUpCall();
	return true;
}

bool CPartFileWriteThread::AddPartFileCreateJob(PartFileCreateData* pData)
{
	if (pData == NULL)
		return false;
	if (!IsRunning() || theApp.IsClosing() || theApp.GetBackendLifecycleState() >= CemuleApp::BackendLifecycleDrainingDiskIo)
		return false;

	CSingleLock sFlushListLock(&m_lockFlushList, TRUE);
	const LONG lPendingCreateJobs = InterlockedCompareExchange(&m_lPartFileCreateJobsPending, 0, 0);
	if (lPendingCreateJobs >= kMaxPendingPartFileCreateJobs) {
		AddDebugLogLine(DLP_HIGH, false, _T("Part-file create queue rejected job under pressure. count=%Id pending=%ld\n"), m_FlushList.GetCount(), lPendingCreateJobs);
		return false;
	}
	if (m_FlushList.GetCount() >= kMaxQueuedPartFileDiskJobs) {
		AddDebugLogLine(DLP_HIGH, false, _T("Part-file create queue rejected job under pressure. count=%Id pending=%ld\n"), m_FlushList.GetCount(), lPendingCreateJobs);
		return false;
	}
	m_FlushList.AddTail(ToWrite{ NULL, 0, NULL, NULL, NULL, NULL, pData, NULL, false });
	InterlockedIncrement(&m_lPartFileCreateJobsPending);
	WakeUpCall();
	return true;
}

bool CPartFileWriteThread::AddPartFileDeleteJob(PartFileDeleteData* pData)
{
	if (pData == NULL)
		return false;
	if (!IsRunning() || theApp.IsClosing() || theApp.GetBackendLifecycleState() >= CemuleApp::BackendLifecycleDrainingDiskIo)
		return false;

	CSingleLock sFlushListLock(&m_lockFlushList, TRUE);
	const LONG lPendingDeleteJobs = InterlockedCompareExchange(&m_lPartFileDeleteJobsPending, 0, 0);
	if (lPendingDeleteJobs >= kMaxPendingPartFileDeleteJobs) {
		AddDebugLogLine(DLP_HIGH, false, _T("Part-file delete queue rejected job under pressure. count=%Id pending=%ld\n"), m_FlushList.GetCount(), lPendingDeleteJobs);
		return false;
	}
	if (m_FlushList.GetCount() >= kMaxQueuedPartFileDiskJobs) {
		AddDebugLogLine(DLP_HIGH, false, _T("Part-file delete queue rejected job under pressure. count=%Id pending=%ld\n"), m_FlushList.GetCount(), lPendingDeleteJobs);
		return false;
	}
	m_FlushList.AddTail(ToWrite{ NULL, 0, NULL, NULL, NULL, NULL, NULL, pData, false });
	InterlockedIncrement(&m_lPartFileDeleteJobsPending);
	WakeUpCall();
	return true;
}

bool CPartFileWriteThread::AddDeferredAsyncDiskWriteJob(AsyncDiskWriteData* pData)
{
	if (pData == NULL || pData->eConflictPolicy != AsyncDiskWriteConflictLastSnapshotWins || pData->strFinalPath.IsEmpty())
		return false;
	if (m_deferredAsyncDiskWriteJobs.GetCount() >= kMaxDeferredAsyncDiskWrites) {
		AddDebugLogLine(DLP_HIGH, false, _T("Async disk write deferred retry queue rejected snapshot under pressure. deferred=%Id final=\"%s\"\n"), m_deferredAsyncDiskWriteJobs.GetCount(), (LPCTSTR)pData->strFinalPath);
		return false;
	}
	m_deferredAsyncDiskWriteJobs.AddTail(pData);
	AddDebugLogLine(DLP_LOW, false, _T("Async disk write snapshot deferred under queue pressure. deferred=%Id final=\"%s\"\n"), m_deferredAsyncDiskWriteJobs.GetCount(), (LPCTSTR)pData->strFinalPath);
	return true;
}

bool CPartFileWriteThread::PopPartFileCreateResult(PartFileCreateResult*& pResult)
{
	pResult = NULL;
	CSingleLock lock(&m_partFileCreateResultsLock, TRUE);
	if (m_partFileCreateResults.IsEmpty())
		return false;
	pResult = m_partFileCreateResults.RemoveHead();
	return pResult != NULL;
}

bool CPartFileWriteThread::TakeQueuedPartFileCreateJob(DWORD uRuntimeID, const uchar* pucHash, PartFileCreateData*& pData)
{
	pData = NULL;
	if (pucHash == NULL)
		return false;

	CSingleLock sFlushListLock(&m_lockFlushList, TRUE);
	for (POSITION pos = m_FlushList.GetHeadPosition(); pos != NULL;) {
		POSITION posCurrent = pos;
		ToWrite& item = m_FlushList.GetNext(pos);
		PartFileCreateData* pQueuedData = item.pPartFileCreateData;
		if (pQueuedData != NULL && pQueuedData->uRuntimeID == uRuntimeID && md4equ(pQueuedData->abyHash, pucHash)) {
			pData = pQueuedData;
			item.pPartFileCreateData = NULL;
			m_FlushList.RemoveAt(posCurrent);
			InterlockedDecrement(&m_lPartFileCreateJobsPending);
			return true;
		}
	}
	return false;
}

bool CPartFileWriteThread::HasPendingPartFileDiskJobs()
{
	if (InterlockedCompareExchange(&m_lPartFileCreateJobsPending, 0, 0) != 0 || InterlockedCompareExchange(&m_lPartFileDeleteJobsPending, 0, 0) != 0)
		return true;
	CSingleLock lock(&m_partFileCreateResultsLock, TRUE);
	return !m_partFileCreateResults.IsEmpty();
}

void CPartFileWriteThread::RemoveDeferredAsyncDiskWriteJobsByFinalPath(const CString& strFinalPath)
{
	if (strFinalPath.IsEmpty())
		return;

	for (POSITION pos = m_deferredAsyncDiskWriteJobs.GetHeadPosition(); pos != NULL;) {
		POSITION posCurrent = pos;
		AsyncDiskWriteData* pDeferredData = m_deferredAsyncDiskWriteJobs.GetNext(pos);
		if (pDeferredData != NULL && pDeferredData->strFinalPath.CompareNoCase(strFinalPath) == 0) {
			delete pDeferredData;
			m_deferredAsyncDiskWriteJobs.RemoveAt(posCurrent);
		}
	}
}

void CPartFileWriteThread::MoveDeferredAsyncDiskWriteJobsToDrainList(CList<ToWrite>& jobsToDrain)
{
	while (!m_deferredAsyncDiskWriteJobs.IsEmpty())
		jobsToDrain.AddTail(ToWrite{ NULL, 0, NULL, NULL, NULL, m_deferredAsyncDiskWriteJobs.RemoveHead(), NULL, NULL, false });
}

bool CPartFileWriteThread::QueueOrWriteDiskSnapshot(AsyncDiskWriteData* pData)
{
	if (pData == NULL)
		return false;

	const bool bClosing = theApp.IsClosing();
	CPartFileWriteThread* pThread = theApp.m_pPartFileWriteThread;
	bool bRejectedByQueuePressure = false;
	if (!bClosing && pThread != NULL && pThread->IsRunning()) {
		if (pThread->AddDiskWriteJob(pData, &bRejectedByQueuePressure))
			return true;
		if (bRejectedByQueuePressure) {
			TraceAsyncDiskWriteResult(pData->strLogName.IsEmpty() ? _T("unknown") : (LPCTSTR)pData->strLogName, pData->lGeneration, _T("skipped"), _T("queue-pressure"), pData->strTempPath, pData->strFinalPath, false);
			delete pData;
			return false;
		}
	}

	if (bClosing && pData->eShutdownPolicy == AsyncDiskWriteShutdownAbort) {
		TraceAsyncDiskWriteResult(pData->strLogName.IsEmpty() ? _T("unknown") : (LPCTSTR)pData->strLogName, pData->lGeneration, _T("skipped"), _T("shutdown-abort"), pData->strTempPath, pData->strFinalPath, true);
		delete pData;
		return false;
	}

	pData->bShutdownFallback = bClosing;
	const bool bResult = WriteDiskSnapshotNow(*pData, true);
	delete pData;
	return bResult;
}

bool CPartFileWriteThread::WriteDiskSnapshotNow(const AsyncDiskWriteData& data, bool bCheckGeneration)
{
	const LPCTSTR pszLogName = data.strLogName.IsEmpty() ? _T("unknown") : (LPCTSTR)data.strLogName;
	if (data.strTempPath.IsEmpty() || data.strFinalPath.IsEmpty()) {
		TraceAsyncDiskWriteResult(pszLogName, data.lGeneration, _T("failed"), _T("invalid-path"), data.strTempPath, data.strFinalPath, data.bShutdownFallback, ERROR_INVALID_PARAMETER);
		return false;
	}

	const CString strLongTempPath = PreparePathForWin32LongPath(data.strTempPath);
	const CString strLongFinalPath = PreparePathForWin32LongPath(data.strFinalPath);
	const CString strLongBackupPath = data.strBackupPath.IsEmpty() ? CString() : PreparePathForWin32LongPath(data.strBackupPath);

	if (bCheckGeneration && data.plGeneration != NULL && data.lGeneration != InterlockedCompareExchange(const_cast<volatile LONG*>(data.plGeneration), 0, 0)) {
		(void)::DeleteFile(strLongTempPath);
		TraceAsyncDiskWriteResult(pszLogName, data.lGeneration, _T("skipped"), _T("stale-generation"), data.strTempPath, data.strFinalPath, data.bShutdownFallback);
		return false;
	}

	CFile file;
	CFileException fex;
	if (!file.Open(strLongTempPath, CFile::modeWrite | CFile::modeCreate | CFile::typeBinary | CFile::shareDenyWrite, &fex)) {
		if (!data.strLogName.IsEmpty())
			theApp.QueueDebugLogLine(false, _T("Failed to open temporary file for %s - %s"), (LPCTSTR)data.strLogName, (LPCTSTR)EscPercent(CExceptionStrDash(fex)));
		TraceAsyncDiskWriteResult(pszLogName, data.lGeneration, _T("failed"), _T("open-temp"), data.strTempPath, data.strFinalPath, data.bShutdownFallback, static_cast<DWORD>(fex.m_lOsError));
		return false;
	}

	try {
		DWORD dwLastShutdownPump = ::GetTickCount();
		if (!data.data.empty())
			file.Write(&data.data[0], (UINT)data.data.size());
		for (std::vector<std::vector<BYTE> >::const_iterator it = data.chunks.begin(); it != data.chunks.end(); ++it) {
			if (!it->empty())
				file.Write(&(*it)[0], (UINT)it->size());
			if (data.bShutdownFallback && theApp.emuledlg != NULL) {
				const DWORD dwNow = ::GetTickCount();
				if (static_cast<DWORD>(dwNow - dwLastShutdownPump) >= 50) {
					theApp.emuledlg->PumpShutdownProgressDialog();
					dwLastShutdownPump = dwNow;
				}
			}
		}
		file.Flush();
		if (data.bShutdownFallback && theApp.emuledlg != NULL)
			theApp.emuledlg->PumpShutdownProgressDialog();
		file.Close();
	} catch (CFileException* ex) {
		const DWORD dwWriteError = static_cast<DWORD>(ex->m_lOsError);
		if (!data.strLogName.IsEmpty())
			theApp.QueueDebugLogLine(false, _T("Failed to write %s - %s"), (LPCTSTR)data.strLogName, (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
		ex->Delete();
		file.Abort();
		(void)::DeleteFile(strLongTempPath);
		TraceAsyncDiskWriteResult(pszLogName, data.lGeneration, _T("failed"), _T("write-temp"), data.strTempPath, data.strFinalPath, data.bShutdownFallback, dwWriteError);
		return false;
	} catch (...) {
		file.Abort();
		(void)::DeleteFile(strLongTempPath);
		TraceAsyncDiskWriteResult(pszLogName, data.lGeneration, _T("failed"), _T("write-temp"), data.strTempPath, data.strFinalPath, data.bShutdownFallback, ERROR_WRITE_FAULT);
		return false;
	}

	if (bCheckGeneration && data.plGeneration != NULL && data.lGeneration != InterlockedCompareExchange(const_cast<volatile LONG*>(data.plGeneration), 0, 0)) {
		(void)::DeleteFile(strLongTempPath);
		TraceAsyncDiskWriteResult(pszLogName, data.lGeneration, _T("skipped"), _T("stale-generation"), data.strTempPath, data.strFinalPath, data.bShutdownFallback);
		return false;
	}

	CSingleLock iniWriteLock(&GetIniFileWriteLock());
	if (IsIniFilePath(data.strFinalPath))
		iniWriteLock.Lock();

	if (data.eReplacePolicy == AsyncDiskWriteBackupThenReplace && !data.strBackupPath.IsEmpty() && !MoveFileEx(strLongFinalPath, strLongBackupPath, MOVEFILE_REPLACE_EXISTING)) {
		const DWORD dwBackupError = ::GetLastError();
		if (dwBackupError != ERROR_FILE_NOT_FOUND && dwBackupError != ERROR_PATH_NOT_FOUND)
			TraceAsyncDiskWriteResult(pszLogName, data.lGeneration, _T("warning"), _T("backup-move"), data.strTempPath, data.strFinalPath, data.bShutdownFallback, dwBackupError);
	}

	if (!MoveFileEx(strLongTempPath, strLongFinalPath, MOVEFILE_REPLACE_EXISTING)) {
		const DWORD dwPublishError = ::GetLastError();
		if (!data.strLogName.IsEmpty())
			theApp.QueueDebugLogLine(false, _T("Failed to publish %s - %s"), (LPCTSTR)data.strLogName, (LPCTSTR)EscPercent(GetErrorMessage(dwPublishError)));
		(void)::DeleteFile(strLongTempPath);
		TraceAsyncDiskWriteResult(pszLogName, data.lGeneration, _T("failed"), _T("publish-final"), data.strTempPath, data.strFinalPath, data.bShutdownFallback, dwPublishError);
		return false;
	}
	TraceAsyncDiskWriteResult(pszLogName, data.lGeneration, _T("success"), _T("published"), data.strTempPath, data.strFinalPath, data.bShutdownFallback);
	return true;
}


bool CPartFileWriteThread::DeletePartFileDiskSnapshotNow(const PartFileDeleteData& data)
{
	static LPCTSTR const pszErrfmt = _T(" - %s");
	bool bSucceeded = true;
	if (!data.strFullName.IsEmpty() && !DeleteFileLongPath(data.strFullName)) {
		const DWORD dwError = ::GetLastError();
		if (dwError != ERROR_FILE_NOT_FOUND && dwError != ERROR_PATH_NOT_FOUND) {
			bSucceeded = false;
			CString sFmt(GetResString(_T("ERR_DELETE")));
			sFmt.AppendFormat(pszErrfmt, (LPCTSTR)GetErrorMessage(dwError));
			theApp.QueueLogLine(false, _T("%s: %s"), (LPCTSTR)sFmt, (LPCTSTR)data.strFullName);
		}
	}

	if (!data.strPartFilePath.IsEmpty() && !DeleteFileLongPath(data.strPartFilePath)) {
		const DWORD dwError = ::GetLastError();
		if (dwError != ERROR_FILE_NOT_FOUND && dwError != ERROR_PATH_NOT_FOUND) {
			bSucceeded = false;
			CString sFmt(GetResString(_T("ERR_DELETE")));
			sFmt.AppendFormat(pszErrfmt, (LPCTSTR)GetErrorMessage(dwError));
			theApp.QueueLogLine(false, _T("%s: %s"), (LPCTSTR)sFmt, (LPCTSTR)data.strPartFilePath);
		}
	}

	if (!data.strBackupPath.IsEmpty() && PathFileExistsLongPath(data.strBackupPath) && !DeleteFileLongPath(data.strBackupPath)) {
		bSucceeded = false;
		CString sFmt(GetResString(_T("ERR_DELETE")));
		sFmt.AppendFormat(pszErrfmt, (LPCTSTR)GetErrorMessage(::GetLastError()));
		theApp.QueueLogLine(false, _T("%s: %s"), (LPCTSTR)sFmt, (LPCTSTR)data.strBackupPath);
	}

	if (!data.strSourceCachePath.IsEmpty() && !DeleteFileLongPath(data.strSourceCachePath)) {
		const DWORD dwError = ::GetLastError();
		if (dwError != ERROR_FILE_NOT_FOUND && dwError != ERROR_PATH_NOT_FOUND) {
			bSucceeded = false;
				theApp.QueueLogLine(true, GetResString(_T("FAILED_TO_DELETE_FILE_MANUALLY")), (LPCTSTR)data.strSourceCachePath);
		}
	}

	if (!data.strTmpPath.IsEmpty() && PathFileExistsLongPath(data.strTmpPath) && !DeleteFileLongPath(data.strTmpPath)) {
		bSucceeded = false;
		CString sFmt(GetResString(_T("ERR_DELETE")));
		sFmt.AppendFormat(pszErrfmt, (LPCTSTR)GetErrorMessage(::GetLastError()));
		theApp.QueueLogLine(false, _T("%s: %s"), (LPCTSTR)sFmt, (LPCTSTR)data.strTmpPath);
	}

	if (!data.strFileName.IsEmpty())
		theApp.QueueLogLine(false, GetResString(_T("REMOVEDDOWNLOAD")), (LPCTSTR)EscPercent(data.strFileName), (LPCTSTR)EscPercent(data.strED2kLink));
	return bSucceeded;
}

bool CPartFileWriteThread::CreatePartFileDiskSnapshotNow(const PartFileCreateData& data, PartFileCreateResult& result)
{
	result.uRuntimeID = data.uRuntimeID;
	md4cpy(result.abyHash, data.abyHash);
	result.strPartFilePath = data.strPartFilePath;
	result.hFile = INVALID_HANDLE_VALUE;
	result.dwError = ERROR_INVALID_PARAMETER;

	if (data.strPartFilePath.IsEmpty())
		return false;

	CString strTempDir;
	int iPartNumber = 0;
	const bool bCanRetryWithNextPartNumber = TryGetNumberedPartFilePathParts(data.strPartFilePath, strTempDir, iPartNumber);
	CString strCreatePartFilePath(data.strPartFilePath);
	HANDLE hFile = INVALID_HANDLE_VALUE;
	for (;;) {
		const CString longPath = PreparePathForWin32LongPath(strCreatePartFilePath);
		hFile = ::CreateFile(longPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (hFile != INVALID_HANDLE_VALUE)
			break;

		result.dwError = ::GetLastError();
		if (!bCanRetryWithNextPartNumber || (result.dwError != ERROR_FILE_EXISTS && result.dwError != ERROR_ALREADY_EXISTS) || iPartNumber >= 0x7ffffffe)
			return false;
		BuildNumberedPartFilePath(strTempDir, ++iPartNumber, strCreatePartFilePath);
	}
	result.strPartFilePath = strCreatePartFilePath;

	if (data.bSparsePartFile) {
		DWORD dwReturnedBytes = 0;
		if (!DeviceIoControl(hFile, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &dwReturnedBytes, NULL)) {
			const DWORD dwError = ::GetLastError();
			if (dwError != ERROR_INVALID_FUNCTION && thePrefs.GetVerboseLogPriority() <= DLP_VERYLOW)
				theApp.QueueDebugLogLine(false, _T("Failed to apply NTFS sparse file attribute to file \"%s\" - %s"), (LPCTSTR)EscPercent(result.strPartFilePath), (LPCTSTR)EscPercent(GetErrorMessage(dwError, 1)));
		}
	}

	FILETIME ft_ctime, ft_mtime;
	if (GetFileTime(hFile, &ft_ctime, (LPFILETIME)NULL, &ft_mtime)) {
		result.tCreated = (time_t)FileTimeToUnixTime(ft_ctime);
		result.tLastModified = (time_t)FileTimeToUnixTime(ft_mtime);
		if (result.tLastModified - result.tCreated > 1) {
			result.tCreated = result.tLastModified;
			VERIFY(SetFileTime(hFile, &ft_mtime, (LPFILETIME)NULL, (LPFILETIME)NULL));
		}
	} else {
		result.tCreated = result.tLastModified = time(NULL);
		if (thePrefs.GetVerbose())
			theApp.QueueDebugLogLine(false, _T("Failed to get file date for \"%s\" - %s"), (LPCTSTR)EscPercent(result.strPartFilePath), (LPCTSTR)GetErrorMessage(::GetLastError(), 1));
	}

	result.dwFileAttributes = ::GetFileAttributes(PreparePathForWin32LongPath(result.strPartFilePath));
	if (result.dwFileAttributes == INVALID_FILE_ATTRIBUTES)
		result.dwFileAttributes = 0;
	result.dwError = ERROR_SUCCESS;
	result.hFile = hFile;
	return true;
}

void CPartFileWriteThread::ProcessPartFileCreate(PartFileCreateData* pData)
{
	CInterlockedDecrementOnExit pendingCounter(&m_lPartFileCreateJobsPending);
	if (pData == NULL)
		return;

	PartFileCreateResult* pResult = new PartFileCreateResult;
	(void)CreatePartFileDiskSnapshotNow(*pData, *pResult);
	CSingleLock lock(&m_partFileCreateResultsLock, TRUE);
	m_partFileCreateResults.AddTail(pResult);
}

void CPartFileWriteThread::ProcessPartFileDelete(PartFileDeleteData* pData)
{
	CInterlockedDecrementOnExit pendingCounter(&m_lPartFileDeleteJobsPending);
	if (pData != NULL) {
		const bool bDeleted = DeletePartFileDiskSnapshotNow(*pData);
		if (pData->uDownloadRemoveSequence != 0 || pData->uDownloadRemoveCorrelationId != 0)
			theApp.QueueDownloadListCommandEvent(CemuleApp::ApplicationEventDownloadRemoveDiskCleanupCompleted, 0, bDeleted ? 1U : 0U, bDeleted ? 0U : 1U, 0, 1, pData->uDownloadRemoveSequence, pData->uDownloadRemoveCorrelationId, CemuleApp::BackendCommandSourceDiskIo, CemuleApp::BackendCommandOrderingDownloadList, _T("download-list:remove-disk-cleanup"));
	}
}

bool CPartFileWriteThread::WriteDiskSnapshot(AsyncDiskWriteData* pData)
{
	return pData != NULL && WriteDiskSnapshotNow(*pData, true);
}

void CPartFileWriteThread::WriteCompletionRoutine(DWORD dwBytesWritten, const OverlappedWrite_Struct *pOvWrite)
{
	if (pOvWrite == NULL) {
		ASSERT(0);
		return;
	}
	CPartFile *pFile = pOvWrite->pFile;
	PartFileBufferedData* pOwnedBuffer = pOvWrite->bOwnsBuffer ? pOvWrite->pBuffer : NULL;

	try {
		if (m_Run) {
			POSITION posPendingIO = m_listPendingIO.Find(const_cast<OverlappedWrite_Struct*>(pOvWrite));
			if (posPendingIO == NULL) {
				AddDebugLogLine(DLP_HIGH, false, _T("Completed part-file write was not found in the pending I/O list. file=%p buffer=%p"), pOvWrite->pFile, pOvWrite->pBuffer);
				delete pOwnedBuffer;
				delete pOvWrite;
				return;
			}
			m_listPendingIO.RemoveAt(posPendingIO);
			CSingleLock sDeletedFilesListLock(&m_DeletedFilesListLock, TRUE);
			if (pFile == NULL || IsDeletedPartFile(pFile, pOvWrite->uRuntimeID)) {
				delete pOwnedBuffer;
				delete pOvWrite;
				return;
			}
			CSingleLock sPartFileDeleteLock(&pFile->m_PartFileDeleteLock, TRUE);
			sDeletedFilesListLock.Unlock();
			PartFileBufferedData *pBuffer = pOvWrite->pBuffer;
			if (pBuffer == NULL) {
				if (pFile) {
					--pFile->m_iWrites;
					ASSERT(pFile->m_iWrites >= 0);
				}
				AddDebugLogLine(DLP_HIGH, false, _T("Completed part-file write has no buffer. file=%p"), pFile);
				delete pOvWrite;
				return;
			}
			const DWORD dwWrite = (DWORD)(pBuffer->end - pBuffer->start + 1);
			if (pFile) {
				--pFile->m_iWrites;
				ASSERT(pFile->m_iWrites >= 0);
			}

			if (dwBytesWritten && dwWrite == dwBytesWritten) {
				if (pFile) {
					if (!pOvWrite->bOwnsBuffer) { //write data
						ASSERT(pBuffer->flushed == PB_PENDING);
						pBuffer->flushed = PB_WRITTEN;
					} else { //full file allocation
						ASSERT(dwBytesWritten == 1);
						::FlushFileBuffers(pFile->m_hWrite);
						pFile->m_hpartfile.SetLength(pBuffer->start); //truncate the extra byte
						delete pOwnedBuffer;
						pOwnedBuffer = NULL;
					}
				}
			} else {
				if (!pOvWrite->bOwnsBuffer)
					pBuffer->flushed = PB_ERROR; //error code is unknown
				else {
					delete pOwnedBuffer;
					pOwnedBuffer = NULL;
				}
				Debug(_T("  Completed write size: expected %lu, written %lu\n"), dwWrite, dwBytesWritten);
			}
		} else {
			if (pFile) {
				CSingleLock sDeletedFilesListLock(&m_DeletedFilesListLock, TRUE);
				if (!IsDeletedPartFile(pFile, pOvWrite->uRuntimeID)) {
					CSingleLock sPartFileDeleteLock(&pFile->m_PartFileDeleteLock, TRUE);
					sDeletedFilesListLock.Unlock();
					RemFile(pFile);
				}
			}
			delete pOwnedBuffer;
			pOwnedBuffer = NULL;
		}
	} catch (CException *ex) {
		ex->Delete();
		ASSERT(0);
	} catch (...) {
		ASSERT(0);
	}

	delete pOwnedBuffer;
	delete pOvWrite;
}

bool CPartFileWriteThread::AddFile(CPartFile *pFile)
{
	ASSERT(m_hPort && m_Run);
	if (pFile && pFile->m_hWrite == INVALID_HANDLE_VALUE) {
		const CString sPartFile(RemoveFileExtension(pFile->GetFullName()));
		const CString longPath = PreparePathForWin32LongPath(sPartFile);
		pFile->m_hWrite = ::CreateFile(longPath, GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		if (pFile->m_hWrite == INVALID_HANDLE_VALUE) {
			theApp.QueueDebugLogLineEx(LOG_ERROR,_T("Failed to open \"%s\" for overlapped write: %s"), (LPCTSTR)EscPercent(sPartFile), (LPCTSTR)EscPercent(GetErrorMessage(::GetLastError(), 1)));
			pFile->SetStatus(PS_ERROR);
			return false;
		}
		if (m_hPort != ::CreateIoCompletionPort(pFile->m_hWrite, m_hPort, (ULONG_PTR)pFile, 0)) {
			theApp.QueueDebugLogLineEx(LOG_ERROR,_T("Failed to associate \"%s\" with IOCP: %s"), (LPCTSTR)EscPercent(sPartFile), (LPCTSTR)EscPercent(GetErrorMessage(::GetLastError(), 1)));
			RemFile(pFile);
			pFile->SetStatus(PS_ERROR);
			return false;
		}
	}
	return true;
}

void CPartFileWriteThread::RemFile(CPartFile *pFile)
{
	ASSERT(pFile);
	if (pFile->m_hWrite != INVALID_HANDLE_VALUE) {
		VERIFY(::CloseHandle(pFile->m_hWrite));
		pFile->m_hWrite = INVALID_HANDLE_VALUE;
	}
}

void CPartFileWriteThread::WakeUpCall()
{
	//pending I/O makes posting unnecessary
	if (m_Run == RUN_IDLE && m_listPendingIO.IsEmpty())
		PostQueuedCompletionStatus(m_hPort, 0, WAKEUP, NULL);
	else
		InterlockedExchange8(&m_bNewData, 1);
}

void CPartFileWriteThread::CleanUpAfterException(const ToWrite& item)
{
	CPartFile* pFile = item.pFile;
	if (pFile == NULL) {
		CleanUp(item, NULL);
		return;
	}

	CSingleLock sDeletedFilesListLock(&m_DeletedFilesListLock, TRUE);
	if (IsDeletedPartFile(pFile, item.uRuntimeID)) {
		CleanUp(item, NULL);
		return;
	}
	CSingleLock sPartFileDeleteLock(&pFile->m_PartFileDeleteLock, TRUE);
	sDeletedFilesListLock.Unlock();
	CleanUp(item, pFile);
}

void CPartFileWriteThread::CleanUp(const ToWrite& item, CPartFile* pFile) {
	if (item.pFlushPartMetData) {
		if (pFile) {
			try {
				pFile->m_bFlushPartMetInQueue = false; // We should do this to make sure flushing not stuck.
				if (item.pFlushPartMetData->bDeferredInitialPartMetSave) {
					pFile->ClearDeferredInitialPartMetSaveWritePending();
					if (theApp.downloadqueue != NULL)
						theApp.downloadqueue->RequestBulkAddDiskFinalizationProgressUpdate(true);
				}
			} catch (CException* ex) {
				ex->Delete();
				ASSERT(0);
			} catch (...) {
				ASSERT(0);
			}
		}
		delete item.pFlushPartMetData;
	} else if (item.pSaveSourcesData) {
		if (pFile) {
			try {
				pFile->m_bSaveSourcesInQueue = false; // We should do this to make sure saving sources not stuck.
			} catch (CException* ex) {
				ex->Delete();
				ASSERT(0);
			} catch (...) {
				ASSERT(0);
			}
		}
		delete item.pSaveSourcesData;
	} else if (item.pAsyncDiskWriteData)
		delete item.pAsyncDiskWriteData;
	else if (item.pPartFileCreateData)
		delete item.pPartFileCreateData;
	else if (item.pPartFileDeleteData)
		delete item.pPartFileDeleteData;
	if (item.bOwnsBuffer)
		delete item.pBuffer;
}
