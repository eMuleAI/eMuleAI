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
#pragma once
#include "MapKey.h"
#include "FileIdentifier.h"
#include <vector>

class CKnownFileList;
class CServerConnect;
class CPartFile;
class CKnownFile;
class CPublishKeywordList;
class CSafeMemFile;
class CServer;
class CCollection;
typedef CMap<CCKey, const CCKey&, CKnownFile*, CKnownFile*> CKnownFilesMap;
class CSharedFileListSearchThread;

struct UnknownFile_Struct
{
	CString strName;
	CString strDirectory;
	CString strSharedDirectory;
};

// Opens an import source file for read (shared) with long-path awareness. Returns INVALID_HANDLE_VALUE on failure and logs user-facing messages consistently. On success, writes file size into outFileSize.
HANDLE OpenImportSourceLongPath(LPCTSTR path, uint64& outFileSize);

class CSharedFileList
{
	friend class CSharedFilesCtrl;
	friend class CClientReqSocket;
	friend class CKnownFileList;
	friend class CSharedFileListSearchThread;

public:
	explicit CSharedFileList(CServerConnect *in_server);
	~CSharedFileList();
	CSharedFileList(const CSharedFileList&) = delete;
	CSharedFileList& operator=(const CSharedFileList&) = delete;

	void	SendListToServer();
	void	Reload();
	void	Save() const;
	void	Process();
	void	Publish();
	void	RebuildMetaData();
	void	DeletePartFileInstances() const;
	void	PublishNextTurn()						{ m_lastPublishED2KFlag = true; }
	void	ClearED2KPublishInfo();
	void	ClearKadSourcePublishInfo();

	static void	CreateOfferedFilePacket(CKnownFile *cur_file, CSafeMemFile &files, CServer *pServer, CUpDownClient *pClient = NULL);

	bool	SafeAddKFile(CKnownFile *toadd, bool bOnlyAdd = false);
	void	RepublishFile(CKnownFile *pFile);
	void	SetOutputCtrl(CSharedFilesCtrl *in_ctrl);
	bool	RemoveFile(CKnownFile *pFile, bool bDeleted = false, bool bWillReloadListLater = false);	// removes a specific shared file from the list
	void	UpdateFile(CKnownFile *toupdate);
	void	AddFileFromNewlyCreatedCollection(const CString &rstrFilePath)	{ CheckAndAddSingleFile(rstrFilePath); }

	// GUI is not initially updated
	bool	AddSingleSharedFile(const CString &rstrFilePath, bool bNoUpdate = false); // includes updating sharing preferences, calls CheckAndAddSingleSharedFile afterwards
	bool	AddSingleSharedDirectory(const CString &rstrFilePath, bool bNoUpdate = false);
	bool	ExcludeFile(const CString &strFilePath);	// excludes a specific file from being shared and removes it from the list if it exists
	bool	AddExcludedSharedDirectory(const CString &strDirPath);
	void	RemoveExcludedSharedDirectory(const CString &strDirPath, bool bSubDirectories);
	void	ClearExcludedSharedDirectories();
	void	CopyExcludedSharedDirectories(CStringList& liExcludedSharedDirs) const;
	bool	IsExcludedSharedDirectory(const CString &strDirPath) const;
	bool	IsSharedByDirectoryRules(const CString &sDirPath) const;

	void	AddKeywords(CKnownFile *pFile);
	void	RemoveKeywords(CKnownFile *pFile);

	void	CopySharedFileMap(CKnownFilesMap &Files_Map);
	CKnownFile*	GetFileByID(const uchar *hash) const;
	CKnownFile*	GetLiveFileByID(const uchar *hash) const;
	CKnownFile*	GetFileByIdentifier(const CFileIdentifierBase &rFileIdent, bool bStrict = false) const;
	CKnownFile*	GetFileByIndex(INT_PTR index) const; // slow
	CKnownFile*	GetFileNext(POSITION &pos) const;
	CKnownFile*	GetFileByAICH(const CAICHHash &rHash) const; // slow

	bool	IsFilePtrInList(const CKnownFile *file) const; // slow
	bool	IsReloading() const						{ return m_bReloadLookupSnapshotActive; }
	bool	IsUnsharedFile(const uchar *auFileHash) const;
	bool	ShouldBeShared(const CString &sDirPath, LPCTSTR const pFilePath, bool bMustBeShared) const;
	bool	ContainsSingleSharedFiles(const CString &strDirectory) const; // includes subdirs
	CString	GetPseudoDirName(const CString &strDirectoryName);
	CString	GetDirNameByPseudo(const CString &strPseudoName) const;

	uint64	GetDatasize(uint64 &pbytesLargest) const;
	INT_PTR	GetCount()								{ return m_Files_map.GetCount(); }
	INT_PTR	GetHashingCount()						{ return waitingforhash_list.GetCount() + currentlyhashing_list.GetCount(); }
	void	NotifyShowFilesCount() const;
	bool	ProbablyHaveSingleSharedFiles() const;

	void	HashFailed(UnknownFile_Struct *hashed);	// SLUGFILLER: SafeHash
	void	FileHashingFinished(CKnownFile *file);

	bool	GetPopularityRank(const CKnownFile *pFile, uint32 &rnOutSession, uint32 &rnOutTotal) const;

	CCriticalSection m_mutWriteList; // don't acquire other locks while having this one in the main thread or make sure deadlocks are impossible
	static uint8 GetRealPrio(uint8 in)				{ return (in < 4) ? in + 1 : 0; }
	void	ResetPseudoDirNames()					{ m_mapPseudoDirNames.RemoveAll(); }
	
	uint64	m_uMetadataUpdatingCount;
	CCriticalSection m_MetadataUpdatingCountLock;

	void OnSharedFilesFound(); 
	bool IsAlreadySharedByPathNoCase(const CString& rstrFilePath);
	void NotifyFoundFilesEvent();
	bool ShouldProcessFoundFilesTick();
	void ReconcileMovedSharedFiles(const CStringArray& changedFiles);

protected:
	bool	AddFile(CKnownFile *pFile);
	void	AddFilesFromDirectory(const CString &rstrDirectory);
	void	FindSharedFiles();

	void	HashNextFile();
	bool	IsHashing(const CString &rstrDirectory, const CString &rstrName);
	void	RemoveFromHashing(CKnownFile *hashed);
	void	LoadSingleSharedFilesList();

	void	CheckAndAddSingleFile(const CFileFind &ff);
	bool	CheckAndAddSingleFile(const CString &rstrFilePath); // add specific files without editing sharing preferences

private:
	static UINT AFX_CDECL RunProc(LPVOID pParam);
	CWinThread* pRebuildMetaDataThread;
	CList<CKnownFile*> m_MetaDataProcessList;

	void StopSearchThread(); // Gracefully stop current search thread without double-delete
	void StartSearchThread(); // Start (or restart) the search thread and reset coalescing flags
	void BeginReloadLookupSnapshot(); // Keep the previous shared map reachable while reload rebuilds the live map.
	void EndReloadLookupSnapshot(); // Drop the temporary lookup snapshot once reload is complete.

	bool m_bInFoundFilesProcessing; // Reentrancy guard: avoid posting tree reloads while scanning
	bool m_bTreeReloadPending;      // Coalesced tree reload request to post after scan
	bool m_bReloadLookupSnapshotActive; // True while GetFileByID may fall back to the previous shared map.

	CSharedFileListSearchThread* m_searchThread; 
	volatile LONG m_lFoundFilesNotify; // Coalesced notifications counter
	bool m_bContinueFoundProcessing; // Continue processing in next tick

	void	AddDirectory(const CString &strDir, CMapStringToPtr &dirset);
	void	CopyExplicitShareRules(CStringList& liSingleSharedFiles, CStringList& liSingleExcludedFiles, CStringList& liExcludedSharedDirs) const;
	void	UpdateSharedPathCache(CKnownFile* pFile, LPCTSTR pOldFilePath);
	bool	TryReconcileMovedSharedFile(const CString& strFilePath);
	CKnownFile* FindUniqueLiveSharedFileByIdentity(LPCTSTR pszFileName, time_t tUtcFileDate, uint64 uFileSize, LPCTSTR pszNewFilePath) const;

	CKnownFilesMap m_Files_map;
	CKnownFilesMap m_ReloadLookupFiles_map;
	CMap<CSKey, const CSKey&, bool, bool>		 m_UnsharedFiles_map;
	CMapStringToString m_mapPseudoDirNames;
	CMapStringToPtr m_mapSharedPathsNoCase; // Lowercased live shared file paths -> dummy
	CPublishKeywordList *m_keywords;
	CTypedPtrList<CPtrList, UnknownFile_Struct*> waitingforhash_list;
	CTypedPtrList<CPtrList, UnknownFile_Struct*> currentlyhashing_list;	// SLUGFILLER: SafeHash
	CServerConnect	 *server;
	CSharedFilesCtrl *output;
	mutable CCriticalSection m_csShareRules;
	CStringList		 m_liSingleSharedFiles;
	CStringList		 m_liSingleExcludedFiles;
	CStringList		 m_liExcludedSharedDirs;
#if defined(_BETA) || defined(_DEVBUILD)
	CString			m_strBetaFileName; //beta test file name
#endif

	INT_PTR	m_currFileSrc;
	INT_PTR	m_currFileNotes;
	time_t	m_lastPublishKadSrc;
	time_t	m_lastPublishKadNotes;
	DWORD	m_lastPublishED2K;
	bool	m_lastPublishED2KFlag;
	bool	bHaveSingleSharedFiles;

	CMapStringToPtr m_mapScanSeen; //Idempotent scan key set to prevent duplicate enqueue for hashing: key -> dummy non-null pointer 
	static CString BuildScanKey(const CString& fullpath, ULONGLONG size, const FILETIME& ftWrite); // Build stable key: fullpath + '#' + size + '#' + mtime (low:high)
	void MarkScanSeen(const CString& key); // Mark key as seen
	void UnmarkScanSeen(const CString& key); // Forget key deterministically when we decide to skip or when known file is inserted
	size_t PendingHashingCount() const; // Helper to query current pending count used for backpressure (maps to seen size)
	static CString NormalizeDirectoryPath(const CString &strDirPath);
	int GetBestDirectoryRuleDepth(const CStringList &liDirs, const CString &sDirPath, bool bIncludeSubdirectories) const;
};

class CAddFileThread : public CWinThread
{
	DECLARE_DYNCREATE(CAddFileThread)
protected:
	CAddFileThread();
public:
	virtual BOOL InitInstance();
	virtual int	Run();
	void	SetValues(CSharedFileList *pOwner, LPCTSTR directory, LPCTSTR filename, LPCTSTR strSharedDir, CPartFile *partfile = NULL);
	bool	ImportParts();
	uint16	SetPartToImport(LPCTSTR import);
private:
	CSharedFileList	*m_pOwner;
	CPartFile	*m_partfile;
	CString		m_strDirectory;
	CString		m_strFilename;
	CString		m_strSharedDir;
	CString		m_strImport;
	CArray<uint16, uint16>	m_PartsToImport;
};

class CSharedFileListSearchThread : public CWinThread
{
	DECLARE_DYNCREATE(CSharedFileListSearchThread)
public:
	enum MessageId
	{
		SFS_EXIT = WM_USER,
		SFS_SEARCH
	};

	struct FoundFile
	{
		FoundFile(CString name_, CString path_, CString dir_, CString linkdir_, time_t date_, ULONGLONG size_) : name(name_), path(path_), dir(dir_), linkdir(linkdir_), date(date_), size(size_) {}
		CString		name;
		CString		path;
		CString		dir;
		CString		linkdir;
		time_t		date;
		ULONGLONG	size;
	};

	CSharedFileListSearchThread() : m_notify(true), m_busy(false), m_lSearchGeneration(0), m_lSnapshotGeneration(-1), m_lExitRequested(0), m_owner(NULL) {}
	virtual	~CSharedFileListSearchThread()
	{
		PostThreadMessageW(SFS_EXIT, 0, 0);
		CSingleLock lock(&m_running, TRUE);
	}

	virtual	BOOL InitInstance()
	{
		DbgSetThreadName("SharedFilesListSearchThread");
		SetThreadPriority(THREAD_PRIORITY_LOWEST);
		m_bAutoDelete = FALSE;
		return TRUE;
	}

	virtual int Run();
	void BeginSearch(CString searchPath);
	bool IsBusy() { return m_busy; }
	bool HasQueuedFoundFiles();
	void ResetWork(); // Reset pending work and transient state without stopping the thread. Empties queued paths and found files, clears dedup maps and releases busy/notify flags.
	void PrepareForShutdown();
	void InvalidateShareRuleSnapshot() { InterlockedExchange(&m_lSnapshotGeneration, -1); }

	// Pop will unmark the item from the in-queue set to prevent duplicates.
		FoundFile* PopFoundFile()
		{
			CSingleLock lock(&m_mutex, TRUE);
			if (!m_foundFiles.IsEmpty()) {
				FoundFile* f = m_foundFiles.RemoveHead();
				CString queueKey(f->path);
				queueKey.MakeLower();
				m_inQueue.RemoveKey(queueKey); // Unmark: no longer queued
				// If the queue becomes empty after this pop, allow next enqueue to notify again.
				if (m_foundFiles.IsEmpty())
					m_notify = true;
				return f;
			}
			
			return NULL;
		}

	void SetOwner(CSharedFileList* owner) { m_owner = owner; }

private:
	struct ShareRuleSnapshot
	{
		ShareRuleSnapshot() : bAutoShareSubdirs(false) {}

		void Clear()
		{
			bAutoShareSubdirs = false;
			sIncoming.Empty();
			liCategoryIncoming.RemoveAll();
			liSharedDirs.RemoveAll();
			liExcludedSharedDirs.RemoveAll();
		}

		bool		bAutoShareSubdirs;
		CString		sIncoming;
		CStringList	liCategoryIncoming;
		CStringList	liSharedDirs;
		CStringList	liExcludedSharedDirs;
	};

	void CheckSingleFile(const WIN32_FIND_DATA& wfd, const CString& rootDir, LONG lGeneration);
	bool ShouldAbortWork(LONG lGeneration) const;
	void CaptureShareRuleSnapshotLocked();
	bool ShouldShareDirectoryBySnapshotLocked(const CString& sDirPath) const;
	LONG GetSearchGeneration() const { return InterlockedCompareExchange((LONG*)&m_lSearchGeneration, 0, 0); }
	bool IsExitRequested() const { return InterlockedCompareExchange((LONG*)&m_lExitRequested, 0, 0) != 0; }

	// Deduplication helpers to avoid infinite growth and re-adding the same file.
	CMapStringToPtr m_seenDuringSearch; // Paths already seen in this scan session
	CMapStringToPtr m_inQueue;          // Lowercased paths currently enqueued (awaiting Pop)

	CList<CString>				m_searchPaths;
	CList<FoundFile*>			m_foundFiles;
	CCriticalSection			m_running;
	CCriticalSection			m_mutex;
	ShareRuleSnapshot			m_shareRuleSnapshot;
	volatile bool				m_notify;
	volatile bool				m_busy;
	volatile LONG				m_lSearchGeneration;
	volatile LONG				m_lSnapshotGeneration;
	volatile LONG				m_lExitRequested;
	CSharedFileList*			m_owner;
};
