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

#pragma once

#include <vector>
#include "OtherFunctions.h"
#include "eMuleAI/Address.h"

class CTag;
struct PartFileBufferedData;

struct SPartMetHashSnapshot
{
	uchar abyHash[MDX_DIGEST_SIZE];
};

struct FlushPartMetData
{
	FlushPartMetData();
	~FlushPartMetData();
	FlushPartMetData(const FlushPartMetData&) = delete;
	FlushPartMetData& operator=(const FlushPartMetData&) = delete;

	LONG lGeneration;
	bool bDontOverrideBak;
	bool bDeferredInitialPartMetSave;
	CString strFullName;
	CString strPartMetFileName;
	CString strFileName;
	CString strTmpPath;
	uint8 uPartFileVersion;
	uchar abyMD4Hash[MDX_DIGEST_SIZE];
	std::vector<SPartMetHashSnapshot> aMD4HashSet;
	std::vector<CTag*> taglist;
};

struct SSaveSourceSnapshotRow
{
	CAddress sourceIP;
	uint32 sourceID;
	uint16 sourcePort;
	uint32 serverip;
	uint16 serverport;
	CString expiration;
	uint8 nSrcExchangeVer;
};

struct SaveSourcesData
{
	LONG lGeneration;
	CString strSourcesFilePath;
	CString strED2kLink;
	std::vector<SSaveSourceSnapshotRow> rows;
};

struct PartFileCreateData
{
	PartFileCreateData();
	DWORD uRuntimeID;
	uchar abyHash[MDX_DIGEST_SIZE];
	CString strPartFilePath;
	bool bSparsePartFile;
};

struct PartFileCreateResult
{
	PartFileCreateResult();
	DWORD uRuntimeID;
	uchar abyHash[MDX_DIGEST_SIZE];
	CString strPartFilePath;
	HANDLE hFile;
	DWORD dwFileAttributes;
	time_t tCreated;
	time_t tLastModified;
	DWORD dwError;
};

struct PartFileDeleteData
{
	PartFileDeleteData();
	CString strFullName;
	CString strPartFilePath;
	CString strBackupPath;
	CString strTmpPath;
	CString strSourceCachePath;
	CString strFileName;
	CString strED2kLink;
	uint64 uDownloadRemoveSequence;
	uint64 uDownloadRemoveCorrelationId;
};

enum EAsyncDiskWriteShutdownPolicy
{
	AsyncDiskWriteShutdownSyncFallback,
	AsyncDiskWriteShutdownAbort
};

enum EAsyncDiskWriteConflictPolicy
{
	AsyncDiskWriteConflictOrdered,
	AsyncDiskWriteConflictLastSnapshotWins
};

enum EAsyncDiskWriteReplacePolicy
{
	AsyncDiskWriteReplaceFinal,
	AsyncDiskWriteBackupThenReplace
};

struct AsyncDiskWriteData
{
	AsyncDiskWriteData();
	LONG lGeneration;
	volatile LONG* plGeneration;
	CString strTempPath;
	CString strFinalPath;
	CString strBackupPath;
	CString strLogName;
	CString strPayloadName;
	bool bShutdownFallback;
	EAsyncDiskWriteShutdownPolicy eShutdownPolicy;
	EAsyncDiskWriteConflictPolicy eConflictPolicy;
	EAsyncDiskWriteReplacePolicy eReplacePolicy;
	std::vector<BYTE> data;
	std::vector<std::vector<BYTE> > chunks;
};

struct DeletedPartFile
{
	CPartFile* pFile;
	PartFileRuntimeID uRuntimeID;
};

struct ToWrite
{
	CPartFile *pFile;
	PartFileRuntimeID uRuntimeID;
	PartFileBufferedData *pBuffer;
	FlushPartMetData* pFlushPartMetData;
	SaveSourcesData* pSaveSourcesData;
	AsyncDiskWriteData* pAsyncDiskWriteData;
	PartFileCreateData* pPartFileCreateData;
	PartFileDeleteData* pPartFileDeleteData;
	bool bOwnsBuffer;
};

struct OverlappedWrite_Struct
{
	OVERLAPPED				oOverlap; // must be the first member
	CPartFile				*pFile;
	PartFileRuntimeID		uRuntimeID;
	PartFileBufferedData	*pBuffer;
	POSITION				pos; // in m_listPendingIO
	bool					bOwnsBuffer;
};

class CPartFileWriteThread : public CWinThread
{
	DECLARE_DYNCREATE(CPartFileWriteThread)
public:
	CPartFileWriteThread();
	~CPartFileWriteThread();
	CPartFileWriteThread(const CPartFileWriteThread&) = delete;
	CPartFileWriteThread& operator=(const CPartFileWriteThread&) = delete;

	void	EndThread();	//completionkey == 0
	void	WakeUpCall();	//completionkey == -1
	bool	IsRunning() const							{ return m_Run > 0; }
	bool	AddFile(CPartFile *pFile);
	bool	AddDiskWriteJob(AsyncDiskWriteData* pData, bool* pbRejectedByQueuePressure = NULL);
	bool	AddPartFileCreateJob(PartFileCreateData* pData);
	bool	AddPartFileDeleteJob(PartFileDeleteData* pData);
	bool	PopPartFileCreateResult(PartFileCreateResult*& pResult);
	bool	TakeQueuedPartFileCreateJob(DWORD uRuntimeID, const uchar* pucHash, PartFileCreateData*& pData);
	bool	HasPendingPartFileDiskJobs();
	bool	IsDeletedPartFile(const CPartFile* pFile, PartFileRuntimeID uRuntimeID) const;
	static bool	QueueOrWriteDiskSnapshot(AsyncDiskWriteData* pData);
	static bool	WriteDiskSnapshotNow(const AsyncDiskWriteData& data, bool bCheckGeneration = true);
	static bool	DeletePartFileDiskSnapshotNow(const PartFileDeleteData& data);
	static bool	CreatePartFileDiskSnapshotNow(const PartFileCreateData& data, PartFileCreateResult& result);
	static void	RemFile(CPartFile *pFile);

	CCriticalSection m_lockFlushList;
	CList<ToWrite> m_FlushList;

	CList<DeletedPartFile> m_DeletedFilesList;
	CCriticalSection m_DeletedFilesListLock;
	CCriticalSection m_lockSavePartFilePrefs;
	bool m_bVerbose;
	int m_iCommitFiles;
private:
	static UINT AFX_CDECL RunProc(LPVOID pParam);
	UINT	RunInternal();

	void	WriteBuffers();
	bool	HasOutstandingPartFileWork(const CPartFile* pFile, PartFileRuntimeID uRuntimeID) const;
	void	PruneDeletedFilesList();
	bool	WriteDiskSnapshot(AsyncDiskWriteData* pData);
	void	DrainPendingAsyncDiskSnapshotsForShutdown();
	void	ProcessPartFileCreate(PartFileCreateData* pData);
	void	ProcessPartFileDelete(PartFileDeleteData* pData);
	void	WriteCompletionRoutine(DWORD dwBytesWritten, const OverlappedWrite_Struct *pOvWrite);
	bool	AddDeferredAsyncDiskWriteJob(AsyncDiskWriteData* pData);
	void	RemoveDeferredAsyncDiskWriteJobsByFinalPath(const CString& strFinalPath);
	void	MoveDeferredAsyncDiskWriteJobsToDrainList(CList<ToWrite>& jobsToDrain);
	void	CleanUpAfterException(const ToWrite& item);
	void	CleanUp(const ToWrite& item, CPartFile* pFile);

	CList<ToWrite>	m_listToWrite;
	CList<AsyncDiskWriteData*>	m_deferredAsyncDiskWriteJobs;
	CTypedPtrList<CPtrList, OverlappedWrite_Struct*>	m_listPendingIO;
	CList<PartFileCreateResult*> m_partFileCreateResults;
	CCriticalSection m_partFileCreateResultsLock;

	CEvent	m_eventThreadEnded;
	HANDLE	m_hPort;
	volatile char m_Run; //0 - not running; 1 - idle; 2 - processing
	volatile char m_bNewData;
	volatile LONG m_lPartFileCreateJobsPending;
	volatile LONG m_lPartFileDeleteJobsPending;
};