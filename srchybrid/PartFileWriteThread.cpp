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
#include "OtherFunctions.h"
#include <timeapi.h>
#include "updownclient.h"
#include "PartFileWriteThread.h"
#include "emule.h"
#include "DownloadQueue.h"
#include "partfile.h"
#include "log.h"
#include "preferences.h"
#include "Statistics.h"
#include <io.h>
#include "eMuleAI/SourceSaver.h"

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

CPartFileWriteThread::CPartFileWriteThread()
	: m_eventThreadEnded(FALSE, TRUE)
	, m_hPort()
	, m_Run(RUN_STOP)
	, m_bNewData()
	, m_bVerbose(thePrefs.GetVerbose())
	, m_iCommitFiles(thePrefs.GetCommitFiles())
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
	}

	while (!m_listToWrite.IsEmpty()) {
		ToWrite currItem = m_listToWrite.RemoveHead();
		delete currItem.pFlushPartMetData;
		delete currItem.pSaveSourcesData;
	}
}

UINT AFX_CDECL CPartFileWriteThread::RunProc(LPVOID pParam)
{
	DbgSetThreadName("PartWriteThread");
	return pParam ? static_cast<CPartFileWriteThread*>(pParam)->RunInternal() : 1;
}

void CPartFileWriteThread::EndThread()
{
	m_Run = RUN_STOP;
	PostQueuedCompletionStatus(m_hPort, 0, 0, NULL);
	m_eventThreadEnded.Lock();
}

UINT CPartFileWriteThread::RunInternal()
{
	m_hPort = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 1);
	if (!m_hPort)
		return ::GetLastError();

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
		if (!m_FlushList.IsEmpty()) {
			CSingleLock sFlushListLock(&m_lockFlushList, TRUE);
			CSingleLock sDeletedFilesListLock(&m_DeletedFilesListLock, TRUE);
			while (!m_FlushList.IsEmpty()) {
				const ToWrite& item = m_FlushList.RemoveHead();
				CPartFile* pFile = item.pFile;
				if (pFile && !m_DeletedFilesList.Find(pFile)) // File is valid and not deleted
					m_listToWrite.AddTail(item);
			}
			m_DeletedFilesList.RemoveAll(); // We used all current items this list, now we can clear whole list.
			InterlockedExchange8(&m_bNewData, 0);
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

		if (!completionKey) //thread termination
			break;
		m_Run = RUN_IDLE;
		if (InterlockedExchange8(&m_bNewData, 0) && m_listPendingIO.IsEmpty())
			PostQueuedCompletionStatus(m_hPort, 0, WAKEUP, NULL);
	}
	m_Run = RUN_STOP;

	//Improper termination of asynchronous I/O follows...
	//close file handles to release I/O completion port
	while (!m_listPendingIO.IsEmpty())
		WriteCompletionRoutine(0, m_listPendingIO.RemoveHead());

	::CloseHandle(m_hPort);
	m_hPort = 0;

	m_eventThreadEnded.SetEvent();
	return 0;
}

void CPartFileWriteThread::WriteBuffers()
{
	//process internal list
	while (!m_listToWrite.IsEmpty() && m_Run) {
		const ToWrite& item = m_listToWrite.RemoveHead();
		PartFileBufferedData* pBuffer = item.pBuffer;

		CPartFile* pFile = item.pFile;
		try {
			CSingleLock sDeletedFilesListLock(&m_DeletedFilesListLock, TRUE);
			if (!pFile || m_DeletedFilesList.Find(pFile)) { // File is invalid or deleted
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
					} else
						continue; // Handle is not valid, skip
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
					pOvWrite->pBuffer = pBuffer;

					static const BYTE zero = 0;
					if (!::WriteFile(pFile->m_hWrite, pBuffer->data ? pBuffer->data : &zero, (DWORD)(pBuffer->end - pBuffer->start + 1), NULL, (LPOVERLAPPED)pOvWrite)) {
						DWORD dwError = ::GetLastError();
						if (dwError != ERROR_IO_PENDING) {
							delete pOvWrite;
							if (item.pBuffer->data) { //check for an allocation request
								item.pBuffer->dwError = dwError;
								item.pBuffer->flushed = PB_ERROR;
							}
							RemFile(pFile);
							return;
						}
					}
					pOvWrite->pos = m_listPendingIO.AddTail(pOvWrite);
					++pFile->m_iWrites;
				} else
					theApp.QueueDebugLogLineEx(LOG_ERROR, _T("WriteBuffers error: CPartFile cannot be written"));
			} 
			else if (item.pFlushPartMetData) { // Flush part met file
				CSingleLock sSavePartFileLock(&pFile->m_SavePartFileLock, TRUE);
				CSingleLock sSavePartFilePrefsLock(&m_lockSavePartFilePrefs, TRUE);
				if (!pFile->PartMetFileData)
					continue;

				if (pFile->PartMetFileData->m_fullname.Right(9) != _T(".part.met")) {
					CleanUp(item, pFile);
					continue;
				}
				// search part file
				const CString& searchpath(RemoveFileExtension(pFile->PartMetFileData->m_fullname));
				CFileFind ff;
				BOOL bFound = ff.FindFile(searchpath);
				if (bFound)
					ff.FindNextFile();
				if (!bFound || ff.IsDirectory()) {
					theApp.QueueLogLine(false, GetResString(_T("ERR_SAVEMET")) + _T(" - %s"), (LPCTSTR)EscPercent(pFile->PartMetFileData->m_partmetfilename), (LPCTSTR)EscPercent(pFile->PartMetFileData->m_strFileName), (LPCTSTR)GetResString(_T("ERR_PART_FNF")));
					CleanUp(item, pFile);
					continue;
				}

				if (!theApp.CanWritePartMetFiles(pFile->GetTmpPath())) {
					CleanUp(item, pFile);
					continue;
				}
				// get file date
				time_t	m_tLastModified = 0;
				time_t	m_tUtcLastModified = 0;
				FILETIME lwtime;
				ff.GetLastWriteTime(&lwtime);
				m_tLastModified = (time_t)FileTimeToUnixTime(lwtime);
				if (m_tLastModified <= 0)
					m_tLastModified = (time_t)-1;
				m_tUtcLastModified = m_tLastModified;
				if (m_tUtcLastModified == (time_t)-1) {
					if (m_bVerbose)
						theApp.QueueDebugLogLine(false, _T("Failed to get file date of \"%s\" (%s)"), (LPCTSTR)EscPercent(pFile->PartMetFileData->m_partmetfilename), (LPCTSTR)EscPercent(pFile->PartMetFileData->m_strFileName));
				} else
					AdjustNTFSDaylightFileTime(m_tUtcLastModified, ff.GetFilePath());
				ff.Close();

				const CString& strTmpFile(pFile->PartMetFileData->m_fullname + PARTMET_TMP_EXT);

				// save file data to part.met file
				CSafeBufferedFile file;
				CFileException fex;
				if (!file.Open(strTmpFile, CFile::modeWrite | CFile::modeCreate | CFile::typeBinary | CFile::shareDenyWrite, &fex)) {
					CString s;
					s.Format(GetResString(_T("ERR_SAVEMET")), (LPCTSTR)pFile->PartMetFileData->m_partmetfilename, (LPCTSTR)EscPercent(pFile->PartMetFileData->m_strFileName));
					theApp.QueueLogLine(false, _T("%s%s"), (LPCTSTR)EscPercent(s), (LPCTSTR)EscPercent(CExceptionStrDash(fex)));
					(void)theApp.CanWritePartMetFiles(pFile->GetTmpPath(), true);
					CleanUp(item, pFile);
					continue;
				}
				::setvbuf(file.m_pStream, NULL, _IOFBF, 16384);

				try {
					//version
					// only use 64 bit tags, when PARTFILE_VERSION_LARGEFILE is set!
					file.WriteUInt8(pFile->PartMetFileData->m_uPartFileVersion);

					//date
					file.WriteUInt32(m_tUtcLastModified);

					//hash
					ASSERT(!isnulmd4(pFile->PartMetFileData->m_abyMD4Hash));
					file.WriteHash16(pFile->PartMetFileData->m_abyMD4Hash);
					UINT uParts = (UINT)pFile->PartMetFileData->m_aMD4HashSet.GetCount();
					file.WriteUInt16((uint16)uParts);
					for (UINT i = 0; i < uParts; ++i)
						file.WriteHash16(pFile->PartMetFileData->m_aMD4HashSet[i]);

					UINT uTagCount = 0;
					ULONG uTagCountFilePos = (ULONG)file.GetPosition();
					file.WriteUInt32(uTagCount);

					CTag nametag(FT_FILENAME, pFile->PartMetFileData->m_strFileName);
					nametag.WriteTagToFile(file, UTF8strOptBOM);
					++uTagCount;

					for (INT_PTR j = 0; j < pFile->PartMetFileData->m_taglist.GetCount(); ++j)
						if (pFile->PartMetFileData->m_taglist[j]->IsStr() || pFile->PartMetFileData->m_taglist[j]->IsInt() || pFile->PartMetFileData->m_taglist[j]->IsInt64() || pFile->PartMetFileData->m_taglist[j]->IsBlob()) {
							pFile->PartMetFileData->m_taglist[j]->WriteTagToFile(file, UTF8strOptBOM);
							++uTagCount;
						}

					file.Seek(uTagCountFilePos, CFile::begin);
					file.WriteUInt32(uTagCount);
					file.SeekToEnd();
					CommitAndClose(file);
				} catch (CFileException* ex) {
					CString strError;
					strError.Format(GetResString(_T("ERR_SAVEMET")), (LPCTSTR)pFile->PartMetFileData->m_partmetfilename, (LPCTSTR)EscPercent(pFile->PartMetFileData->m_strFileName));
					theApp.QueueLogLine(false, _T("%s%s"), (LPCTSTR)EscPercent(strError), (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
					ex->Delete();

					// remove the partially written or otherwise damaged temporary file,
					// need to close the file before removing it.
					file.Abort(); //Call 'Abort' instead of 'Close' to avoid ASSERT.
					(void)_tremove(strTmpFile);
					(void)theApp.CanWritePartMetFiles(pFile->GetTmpPath(), true);
					CleanUp(item, pFile);
					continue;
				}

				// after successfully writing the temporary part.met file...
				DWORD dwReplaceError = ERROR_SUCCESS;
				if (!ReplaceFileAtomically(strTmpFile, pFile->PartMetFileData->m_fullname, &dwReplaceError)) {
					(void)theApp.CanWritePartMetFiles(pFile->GetTmpPath(), true);
					if (m_bVerbose)
						theApp.QueueDebugLogLine(false, _T("Failed to move temporary part.met file \"%s\" to \"%s\" - %s"),
							(LPCTSTR)EscPercent(strTmpFile), (LPCTSTR)EscPercent(pFile->PartMetFileData->m_fullname), (LPCTSTR)EscPercent(GetErrorMessage(dwReplaceError)));

					CString strError;
					strError.Format(GetResString(_T("ERR_SAVEMET")), (LPCTSTR)pFile->PartMetFileData->m_partmetfilename, (LPCTSTR)EscPercent(pFile->PartMetFileData->m_strFileName));
					strError.AppendFormat(_T(" - %s"), (LPCTSTR)EscPercent(GetErrorMessage(dwReplaceError)));
					theApp.QueueLogLine(false, _T("%s"), (LPCTSTR)strError);
					CleanUp(item, pFile);
					continue;
				}

				// create a backup of the successfully written part.met file
				const CString strBakFile(pFile->PartMetFileData->m_fullname + PARTMET_BAK_EXT);
				const CString strBakTmpFile(strBakFile + PARTMET_TMP_EXT);
				DWORD dwBakError = ERROR_SUCCESS;
				if (!CopyFileToTempAndReplace(pFile->PartMetFileData->m_fullname, strBakFile, strBakTmpFile, item.pFlushPartMetData->bDontOverrideBak, &dwBakError)) {
					if (!item.pFlushPartMetData->bDontOverrideBak && theApp.CanWritePartMetFiles(pFile->GetTmpPath(), true)) {
						theApp.QueueDebugLogLine(false, _T("Failed to create backup of %s (%s) - %s"),
							(LPCTSTR)EscPercent(pFile->PartMetFileData->m_fullname), (LPCTSTR)EscPercent(pFile->PartMetFileData->m_strFileName), (LPCTSTR)EscPercent(GetErrorMessage(dwBakError)));
					}
				}
				CleanUp(item, pFile);
			} else if (item.pSaveSourcesData) { // Save sources
				CSingleLock sSaveSourcesLock(&pFile->m_SaveSourcesLock, TRUE);
				CString strLine;
				CStdioFile f;
				if (f.Open(pFile->m_sourcesaver.szslsfilepath, CFile::modeCreate | CFile::modeWrite | CFile::typeText)) {
					f.WriteString(_T("#format: SourceIP/LowID:SourcePort,ExpirationDate(yymmddhhmm),SourceExchangeVersion,ServerIP,ServerPort;\r\n"));
					f.WriteString(_T("#") + pFile->GetED2kLink() + _T("\r\n")); //MORPH - Added by IceCream, Storing ED2K link in Save Source files, To recover corrupted met by skynetman

					while (!pFile->srcstosave.IsEmpty()) {
						CSourceSaver::CSourceData* cur_src = pFile->srcstosave.RemoveHead();
						if (cur_src->sourceID) // Only LowID's are set nonzero. For this case ID is saved and loaded instead of Low ID for this case.
							strLine.Format(_T("%i:%i,%s,%i,%s:%i;\r\n"), cur_src->sourceID, cur_src->sourcePort, cur_src->expiration, cur_src->nSrcExchangeVer, ipstr(cur_src->serverip), cur_src->serverport);
						else // IPv4 or IPv6 is saved and loaded instead of LowID for this case
							strLine.Format(_T("%s:%i,%s,%i,%s:%i;\r\n"), cur_src->sourceIP.ToStringC(), cur_src->sourcePort, cur_src->expiration, cur_src->nSrcExchangeVer, ipstr(cur_src->serverip), cur_src->serverport);
						delete cur_src;
						f.WriteString(strLine);
					}

					f.Close();
				}
				CleanUp(item, pFile);
			}
		} catch (CException *ex) {
			if (m_bVerbose)
				theApp.QueueDebugLogLine(false, _T("CPartFileWriteThread::WriteBuffers - %s"), (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
			ex->Delete();
			CleanUp(item, pFile);
			ASSERT(0);
		} catch (...) {
			if (m_bVerbose)
				theApp.QueueDebugLogLine(false, _T("CPartFileWriteThread::WriteBuffers exception occured"));
			CleanUp(item, pFile);
			ASSERT(0);
		}
	}
}

void CPartFileWriteThread::WriteCompletionRoutine(DWORD dwBytesWritten, const OverlappedWrite_Struct *pOvWrite)
{
	if (pOvWrite == NULL) {
		ASSERT(0);
		return;
	}
	CPartFile *pFile = pOvWrite->pFile;

	try {
		if (m_Run) {
			PartFileBufferedData *pBuffer = pOvWrite->pBuffer;
			const DWORD dwWrite = (DWORD)(pBuffer->end - pBuffer->start + 1);

			ASSERT(pOvWrite->pos);
			m_listPendingIO.RemoveAt(pOvWrite->pos);
			if (dwBytesWritten && dwWrite == dwBytesWritten) {
				if (pFile) {
					--pFile->m_iWrites;
					if (pBuffer->data) { //write data
						ASSERT(pBuffer->flushed = PB_PENDING && pFile->m_iWrites >= 0);
						pBuffer->flushed = PB_WRITTEN;
					} else { //full file allocation
						ASSERT(dwBytesWritten == 1);
						::FlushFileBuffers(pFile->m_hWrite);
						pFile->m_hpartfile.SetLength(pBuffer->start); //truncate the extra byte
						delete pBuffer;
					}
				}
			} else {
				pBuffer->flushed = PB_ERROR; //error code is unknown
				Debug(_T("  Completed write size: expected %lu, written %lu\n"), dwWrite, dwBytesWritten);
			}
		} else if (pFile)
			RemFile(pFile);
	} catch (CException *ex) {
		ex->Delete();
		ASSERT(0);
	} catch (...) {
		ASSERT(0);
	}

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

void CPartFileWriteThread::CleanUp(const ToWrite& item, CPartFile* pFile) {
	if (item.pFlushPartMetData) {
		if (pFile) {
			try {
				if (!m_DeletedFilesList.Find(pFile)) // File is not deleted
					pFile->m_bFlushPartMetInQueue = false; // We should do this to make sure flushing not stuck.
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
				if (!m_DeletedFilesList.Find(pFile)) // File is not deleted
					pFile->m_bSaveSourcesInQueue = false; // We should do this to make sure saving sources not stuck.
			} catch (CException* ex) {
				ex->Delete();
				ASSERT(0);
			} catch (...) {
				ASSERT(0);
			}
		}
		delete item.pSaveSourcesData;
	}
}
