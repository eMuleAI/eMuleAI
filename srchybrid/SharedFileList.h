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
#include "eMuleAI/SharedCache.h"
#include <vector>

class CKnownFileList;
class CServerConnect;
class CPartFile;
class CKnownFile;
class CPublishKeywordList;
class CSafeMemFile;
class CServer;
class CCollection;
class CTag;
struct ImportOperationContext;
struct SharedFileMetaDataThreadContext;
typedef CMap<CCKey, const CCKey&, CKnownFile*, CKnownFile*> CKnownFilesMap;
typedef CMap<CSKey, const CSKey&, CKnownFile*, CKnownFile*> CReloadLookupFilesMap;
class CSharedFileListSearchThread;

struct UnknownFile_Struct
{
	CString strName;
	CString strDirectory;
	CString strSharedDirectory;
	CString strPathKey;
};

struct SharedFileHashResult_Struct
{
	SharedFileHashResult_Struct()
		: pKnownFile(NULL)
	{
	}

	CString strName;
	CString strDirectory;
	CString strPathKey;
	CKnownFile* pKnownFile;
};

struct PartFileHash_Struct
{
	CPartFile* pPartFile;
	DWORD dwRuntimeID;
	uchar abyFileHash[16];
	CKnownFile* pKnownFile;
};

// Opens an import source file for read (shared) with long-path awareness. Returns INVALID_HANDLE_VALUE on failure and logs user-facing messages consistently. On success, writes file size into outFileSize.
HANDLE OpenImportSourceLongPath(LPCTSTR path, uint64& outFileSize);

class CSharedFileList
{
	friend class CSharedFilesCtrl;
	friend class CClientReqSocket;
	friend class CKnownFileList;
	friend class CSharedFileListSearchThread;
	friend class CAddFileThread;

public:
	explicit CSharedFileList(CServerConnect *in_server);
	~CSharedFileList();
	CSharedFileList(const CSharedFileList&) = delete;
	CSharedFileList& operator=(const CSharedFileList&) = delete;

	void	SendListToServer();
	void	Reload(LONG lDirWatchGeneration = 0);
	void	ShutdownSearchThreadForExit();
	bool	LoadSharedCacheForStartup(LONG lGeneration, uint64 uCancellationToken);
	void	StartDeferredStartupScan();
	void	StartDeferredStartupScanAfterKnownFilesFailure();
	bool	IsStartupScanComplete() const { return m_bStartupScanCompleted; }
	void	Save() const;
	void	Process();
	void	Publish();
	void	RebuildMetaData();
	bool	QueueMetaDataUpdateForFile(const CKnownFile* pFile);
	UINT	GetMetaDataUpdateCount() const;
	void	DeletePartFileInstances() const;
	void	PublishNextTurn()						{ m_lastPublishED2KFlag = true; }
	void	ClearED2KPublishInfo();
	void	ClearKadSourcePublishInfo();

	struct SOfferedFilePacketSnapshot
	{
		SOfferedFilePacketSnapshot();
		SOfferedFilePacketSnapshot(const SOfferedFilePacketSnapshot& src);
		~SOfferedFilePacketSnapshot();
		SOfferedFilePacketSnapshot& operator=(const SOfferedFilePacketSnapshot& src);

		void Clear();
		void CopyFrom(const SOfferedFilePacketSnapshot& src);
		const CTag* GetTag(uint8 nName) const;

		uchar abyFileHash[16];
		CString strFileName;
		uint64 uFileSize;
		UINT uFileRating;
		UINT uMetaDataVer;
		bool bLargeFile;
		bool bPartFile;
		std::vector<CTag*> aMetaTags;
	};

	struct SWebSharedFileSnapshot
	{
		SWebSharedFileSnapshot();

		CString strFileCompletes;
		CString strFileHash;
		CString strFilePriority;
		CString strFileName;
		CString strFilePath;
		double dblFileCompletes;
		uint64 uFileSize;
		uint64 uTransferred;
		uint64 uAllTimeTransferred;
		uint32 uRequests;
		uint32 uAllTimeRequests;
		uint32 uAccepts;
		uint32 uAllTimeAccepts;
		byte nFilePriority;
		bool bPartFile;
		bool bFileAutoPriority;
		bool bDownloadable;
		bool bReleasePriority;
	};

	static bool	BuildOfferedFilePacketSnapshot(CKnownFile *cur_file, SOfferedFilePacketSnapshot& snapshot);
	static void	CreateOfferedFilePacket(const SOfferedFilePacketSnapshot& snapshot, CSafeMemFile &files, CServer *pServer, CUpDownClient *pClient = NULL);
	static void	CreateOfferedFilePacket(CKnownFile *cur_file, CSafeMemFile &files, CServer *pServer, CUpDownClient *pClient = NULL);
	bool	CopyWebSharedFileSnapshot(const CString& strFileHash, SWebSharedFileSnapshot& snapshot) const;
	void	CopyWebSharedFileSnapshots(std::vector<SWebSharedFileSnapshot>& snapshots, size_t uMaxSnapshots = 0) const;

	bool	SafeAddKFile(CKnownFile *toadd, bool bOnlyAdd = false, bool bHashingAlreadyDetached = false);
	void	RepublishFile(CKnownFile *pFile, bool bForce = false);
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

	bool	IsFilePtrInList(const CKnownFile *file) const;
	bool	IsReloading() const;
	bool	HasActiveSharedFilesWork() const;
	LONG	GetSharedFilesModelRevision() const { return ::InterlockedCompareExchange(const_cast<volatile LONG*>(&m_lSharedFilesModelRevision), 0, 0); }
	bool	IsUnsharedFile(const uchar *auFileHash) const;
	bool	ShouldBeShared(const CString &sDirPath, LPCTSTR const pFilePath, bool bMustBeShared) const;
	bool	AreExplicitShareRulesLoaded() const;
	bool	ContainsSingleSharedFiles(const CString &strDirectory) const; // includes subdirs
	CString	GetPseudoDirName(const CString &strDirectoryName);
	CString	GetDirNameByPseudo(const CString &strPseudoName) const;

	uint64	GetDatasize(uint64 &pbytesLargest) const;
	INT_PTR	GetCount()								{ return m_Files_map.GetCount(); }
	INT_PTR	GetHashingCount()						{ return waitingforhash_list.GetCount() + currentlyhashing_list.GetCount(); }
	void	NotifyShowFilesCount() const;
	bool	ProbablyHaveSingleSharedFiles() const;
	bool	CanClientBrowseSharedFile(const CKnownFile *file, const CUpDownClient *client) const;

	void	HashFailed(SharedFileHashResult_Struct *hashed);	// SLUGFILLER: SafeHash
	void	FileHashingFinished(CKnownFile *file, LPCTSTR pszPathKey);

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
	void GetStartupScanProgress(UINT& uSharedFiles, UINT& uQueuedFoundFiles, UINT& uHashingFiles, UINT& uPendingFolders, UINT& uCompletionStep, bool& bScanning, bool& bCompleting);
	void GetSharedFilesLoadProgress(UINT& uDone, UINT& uTotal, CString& strDetail);
	void ReconcileMovedSharedFiles(const CStringArray& changedFiles);

protected:
	bool	AddFile(CKnownFile *pFile);
	void	AddFilesFromDirectory(const CString &rstrDirectory);
	void	FindSharedFiles();

	void	HashNextFile();
	void	FlushOutputBulkAddListUpdateIfIdle();
	void	QueueSharedFilesReloadIfModelChanged(LPCTSTR pszStage);
	bool	IsHashing(const CString &rstrDirectory, const CString &rstrName);
	bool	IsHashingByPathKey(LPCTSTR pszPathKey);
	bool	RemoveFromHashing(CKnownFile *hashed, LPCTSTR pszPathKey);
	bool	RemoveCurrentHashingByPathKey(LPCTSTR pszPathKey, LPCTSTR pszDirectory, LPCTSTR pszName);
	bool	RemoveWaitingFromHashingByPathKey(LPCTSTR pszPathKey);
	void	LoadSingleSharedFilesList();

	void	CheckAndAddSingleFile(const CFileFind &ff);
	bool	CheckAndAddSingleFile(const CString &rstrFilePath); // add specific files without editing sharing preferences

private:
	enum EMetaDataQueueResult
	{
		MetaDataQueueFailed,
		MetaDataQueueUnchanged,
		MetaDataQueueQueued
	};

	volatile LONG m_lRebuildMetaDataThreadActive;
	static UINT AFX_CDECL RunMetaDataUpdateProc(LPVOID pParam);
	bool StartMetaDataUpdateThread();
	void ShutdownMetaDataUpdateThread();
	EMetaDataQueueResult QueueMetaDataUpdate(const CKnownFile* pFile, bool bManualUpdate = false, bool bForceUpdate = false);
	void QueueMetaDataReconciliation();
	void ProcessDeferredMetaDataUpdates();
	SharedFileMetaDataThreadContext* m_pMetaDataThreadContext;
	POSITION m_posMetaDataReconciliation;
	uint32 m_uMetaDataReconciliationPathRevision;
	uint32 m_uSharedPathCacheRevision;
	bool m_bMetaDataReconciliationStarted;

	static SWebSharedFileSnapshot BuildWebSharedFileSnapshot(const CKnownFile *pFile);
	void StoreWebSharedFileSnapshot(const CKnownFile *pFile);
	void RemoveWebSharedFileSnapshot(const uchar *fileHash);
	void ClearWebSharedFileSnapshots();

	void StopSearchThread(); // Gracefully stop current search thread without double-delete
	void StartSearchThread(); // Start (or restart) the search thread and reset coalescing flags
	void BeginReloadLookupSnapshot(); // Keep the previous shared map reachable while reload rebuilds the live map.
	void EndReloadLookupSnapshot(); // Drop the temporary lookup snapshot once reload is complete.
	bool ClearReloadLookupSnapshotChunk(UINT uMaxFiles, UINT& uProcessed, INT_PTR& iRemaining);
	void BeginReloadScan(LONG lDirWatchGeneration);
	void EndReloadScan();
	bool TrackScannedSharedFile(const CString& strFilePath, const CString& strFileName, time_t tUtcFileDate, uint64 uFileSize);
	bool IsReloadFoundFileCurrent(const CKnownFile* pFile) const;
	bool IsReloadFoundFileIdentityCurrent(const CString& strFilePath, uint64 uFileSize, time_t tUtcFileDate) const;
	bool PruneReloadMissingSharedFilesChunk(UINT uMaxFiles, UINT& uProcessed, INT_PTR& iRemaining);
	static CString BuildReloadFileIdentityKey(LPCTSTR pszFilePath, uint64 uFileSize, time_t tUtcFileDate);
	static bool ParseReloadFileIdentityKey(const CString& strIdentity, uint64& ruFileSize, time_t& rtUtcFileDate);
	static bool IsReloadFileIdentityCurrent(const CString& strIdentity, uint64 uFileSize, time_t tUtcFileDate);
	void ResetSharedCacheRefresh();
	bool RefreshSharedCacheChunk(UINT uMaxFiles, UINT& uProcessed, INT_PTR& iRemaining);
	void QueueSharedCachePersistenceSave();
	bool StartSharedFilesCompletion();
	bool ApplySharedFilesCompletionChunk(UINT& uProcessed, INT_PTR& iRemaining);
	void FinishSharedFilesCompletion();

	enum ESharedFilesCompletionStep
	{
		SharedFilesCompletionIdle,
		SharedFilesCompletionPruneMissing,
		SharedFilesCompletionAbortClearReloadSnapshot,
		SharedFilesCompletionLog,
		SharedFilesCompletionPurgeKeywords,
		SharedFilesCompletionHashNextFile,
		SharedFilesCompletionClearReloadSnapshot,
		SharedFilesCompletionQueueListUpdate,
		SharedFilesCompletionPruneWaiters,
		SharedFilesCompletionRefreshSharedCache,
		SharedFilesCompletionFinish
	};

	bool m_bInFoundFilesProcessing; // Reentrancy guard: avoid posting tree reloads while scanning
	bool m_bTreeReloadPending;      // Coalesced tree reload request to post after scan
	bool m_bReloadLookupSnapshotActive; // True while GetFileByID may fall back to the previous shared map.
	bool m_bReloadScanActive;
	LONG m_lReloadScanDirWatchGeneration;
	CMapStringToString m_mapReloadFoundFileIdentities; // Lowercased file path -> scan path/size/time identity.
	POSITION m_posReloadPruneCandidate;
	mutable CCriticalSection m_csReloadScan;
	bool m_bSharedFilesCompletionActive;
	bool m_bSharedFilesCompletionPending;
	bool m_bCompletionKeywordPurgeStarted;
	bool m_bSharedFilesModelChangedSinceListUpdate;
	volatile LONG m_lSharedFilesModelRevision;
	bool m_bSharedCacheRefreshStarted;
	bool m_bSharedCacheRefreshCommitted;
	UINT m_uSharedFilesCompletionStep;
	CString m_strCompletionKeywordPurgeCursor;
	std::vector<CSKey> m_aSharedCacheRefreshKeys;
	size_t m_uSharedCacheRefreshIndex;
	bool m_bStartupScanDeferred;
	bool m_bStartupScanCompleted;
	volatile LONG m_lSharedFilesSaveGeneration;
	volatile LONG m_lShareRuleGeneration;

	CSharedFileListSearchThread* m_searchThread; 
	volatile LONG m_lFoundFilesNotify; // Coalesced notifications counter
	bool m_bContinueFoundProcessing; // Continue processing in next tick

	void	AddDirectory(const CString &strDir, CMapStringToPtr &dirset);
	void	InvalidateShareRuleSnapshot();
	LONG	GetShareRuleGeneration() const { return ::InterlockedCompareExchange(const_cast<volatile LONG*>(&m_lShareRuleGeneration), 0, 0); }
	void	CopyExplicitShareRules(CStringList& liSingleSharedFiles, CStringList& liSingleExcludedFiles, CStringList& liExcludedSharedDirs) const;
	void	SetExplicitShareRulesLoaded(bool bLoaded);
	static CString BuildSharedPathCacheKey(const CString& strFilePath);
	void	UpdateSharedPathCache(CKnownFile* pFile, LPCTSTR pOldFilePath);
	void	UpdateSharedPathCacheByPath(LPCTSTR pOldFilePath, LPCTSTR pNewFilePath);
	void	MarkSharedFilesModelChanged();
	void	QueueDeferredHashResult(SharedFileHashResult_Struct* pResult);
	void	ProcessDeferredHashResults();
	void	QueueDeferredPartFileHashResult(PartFileHash_Struct* pResult);
	void	ProcessDeferredPartFileHashResults();
	bool	TryReconcileMovedSharedFile(const CString& strFilePath);
	bool FindUniqueLiveSharedFileByIdentity(LPCTSTR pszFileName, time_t tUtcFileDate, uint64 uFileSize, LPCTSTR pszNewFilePath, uchar aucFileHash[MDX_DIGEST_SIZE]);

	CKnownFile* FindKnownFileFromSharedCache(const CString& strFilePath, time_t tUtcFileDate, uint64 uFileSize) const;
	bool IsCachedDuplicateSharedPath(const CString& strFilePath, time_t tUtcFileDate, uint64 uFileSize, const uchar* pucFileHash) const;
	void RememberDuplicateSharedPath(const CString& strFilePath, const uchar* pucFileHash, time_t tUtcFileDate, uint64 uFileSize);

	mutable CCriticalSection m_csWebSharedFileSnapshots;
	std::vector<SWebSharedFileSnapshot> m_webSharedFileSnapshots;
	CMapStringToPtr m_mapWebSharedFileSnapshotIndexes;
	CKnownFilesMap m_Files_map;
	CReloadLookupFilesMap m_ReloadLookupFiles_map;
	CMap<CSKey, const CSKey&, bool, bool>		 m_UnsharedFiles_map;
	CMapStringToString m_mapPseudoDirNames;
	CMapStringToPtr m_mapSharedPathsNoCase; // Lowercased live shared file paths -> CKnownFile pointer or legacy dummy.
	CMapStringToPtr m_mapHashingPathsNoCase; // Lowercased waiting/current hashing file paths -> dummy
	CSharedCache m_sharedCache;
	CPublishKeywordList *m_keywords;
	CTypedPtrList<CPtrList, UnknownFile_Struct*> waitingforhash_list;
	CTypedPtrList<CPtrList, UnknownFile_Struct*> currentlyhashing_list;	// SLUGFILLER: SafeHash
	CCriticalSection m_csDeferredHashResults;
	SharedFileHashResult_Struct* m_pDeferredHashResult;
	CCriticalSection m_csDeferredPartFileHashResults;
	CTypedPtrList<CPtrList, PartFileHash_Struct*> m_deferredPartFileHashResults;
	CServerConnect	 *server;
	CSharedFilesCtrl *output;
	mutable CCriticalSection m_csShareRules;
	CStringList		 m_liSingleSharedFiles;
	CStringList		 m_liSingleExcludedFiles;
	CStringList		 m_liExcludedSharedDirs;
	bool			 m_bExplicitShareRulesLoaded;
#if defined(_BETA) || defined(_DEVBUILD)
	CString			m_strBetaFileName; //beta test file name
#endif

	INT_PTR	m_currFileSrc;
	INT_PTR	m_currFileNotes;
	time_t	m_lastPublishKadSrc;
	time_t	m_lastPublishKadNotes;
	DWORD	m_lastPublishED2K;
	bool	m_lastPublishED2KFlag;
	uint32	m_uLastEServerBuddyMagicAnnounceEpoch;
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
	virtual ~CAddFileThread();
	virtual BOOL InitInstance();
	virtual int	Run();
	void	SetValues(CSharedFileList *pOwner, LPCTSTR directory, LPCTSTR filename, LPCTSTR strSharedDir, CPartFile *partfile = NULL, bool bRequireStableHashSource = false);
	void	SetSharedHashResult(SharedFileHashResult_Struct* pResult);
	void	SetImportOperationContext(ImportOperationContext* pContext);
	bool	ImportParts();
	uint16	SetPartToImport(LPCTSTR import);
private:
	int		RunInternal();
	void	CompleteSharedHashResult(CKnownFile* pKnownFile);
	CSharedFileList	*m_pOwner;
	CPartFile	*m_partfile;
	ImportOperationContext* m_pImportOperationContext;
	CString		m_strDirectory;
	CString		m_strFilename;
	CString		m_strSharedDir;
	CString		m_strImport;
	CString		m_strPartFileName;
	DWORD		m_dwPartFileRuntimeID;
	uchar		m_abyPartFileHash[16];
	bool		m_bPartFileHashTokenValid;
	bool		m_bRequireStableHashSource;
	SharedFileHashResult_Struct* m_pSharedHashResult;
	CArray<uint16, uint16>	m_PartsToImport;
};

class CSharedFileListSearchThread : public CWinThread
{
	DECLARE_DYNCREATE(CSharedFileListSearchThread)
public:
	enum MessageId
	{
		SFS_EXIT = WM_USER,
		SFS_SEARCH,
		SFS_CLEANUP
	};

	struct FoundFile
	{
		FoundFile(CString name_, CString path_, CString dir_, CString linkdir_, time_t date_, ULONGLONG size_, CString pathKey_, LONG ruleGeneration_, CKnownFile* knownFile_, CKnownFile* duplicateFile_)
			: name(name_), path(path_), dir(dir_), linkdir(linkdir_), pathKey(pathKey_), date(date_), size(size_), ruleGeneration(ruleGeneration_), knownFile(knownFile_), duplicateFile(duplicateFile_) {}
		CString		name;
		CString		path;
		CString		dir;
		CString		linkdir;
		CString		pathKey;
		time_t		date;
		ULONGLONG	size;
		LONG		ruleGeneration;
		CKnownFile*	knownFile;
		CKnownFile*	duplicateFile;
	};

	CSharedFileListSearchThread() : m_notify(true), m_busy(false), m_lSearchGeneration(0), m_lSnapshotGeneration(-1), m_lExitRequested(0), m_lQueuedFoundFiles(0), m_lPendingSearchPaths(0), m_owner(NULL)
	{
		m_seenDuringSearch.InitHashTable(32771);
		m_inQueue.InitHashTable(65537);
	}
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
	void GetProgressCounts(UINT& uPendingFolders, UINT& uQueuedFoundFiles, bool& bBusy);
	void ResetWork(); // Reset pending work and transient state without stopping the thread. Empties queued paths and found files, clears dedup maps and releases busy/notify flags.
	void PrepareForShutdown();
	void InvalidateShareRuleSnapshot() { InterlockedExchange(&m_lSnapshotGeneration, -1); }

	// UI consumers must not block on the scanner lock.
	bool TryPopFoundFile(FoundFile*& rpFoundFile)
	{
		rpFoundFile = NULL;

		CRITICAL_SECTION* pSection = static_cast<CRITICAL_SECTION*>(m_mutex);
		if (::TryEnterCriticalSection(pSection) == FALSE)
			return false;

		bool bPostCleanup = false;
		if (!m_foundFiles.IsEmpty()) {
			rpFoundFile = m_foundFiles.RemoveHead();
			InterlockedDecrement(&m_lQueuedFoundFiles);
			m_inQueue.RemoveKey(rpFoundFile->pathKey);
			if (m_foundFiles.IsEmpty()) {
				m_notify = true;
				bPostCleanup = !m_busy;
			}
		}

		::LeaveCriticalSection(pSection);

		if (bPostCleanup)
			PostThreadMessageW(SFS_CLEANUP, 0, 0);
		return true;
	}

	// Worker-side drain. UI code must use TryPopFoundFile.
	FoundFile* PopFoundFile()
	{
		CSingleLock lock(&m_mutex, TRUE);
		if (!m_foundFiles.IsEmpty()) {
			FoundFile* f = m_foundFiles.RemoveHead();
			InterlockedDecrement(&m_lQueuedFoundFiles);
			m_inQueue.RemoveKey(f->pathKey);
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
		ShareRuleSnapshot() : bAutoShareSubdirs(false), bHasSingleExcludedFiles(false), lRuleGeneration(0)
		{
			mapSingleExcludedFiles.InitHashTable(257);
		}

		void Clear()
		{
			bAutoShareSubdirs = false;
			bHasSingleExcludedFiles = false;
			lRuleGeneration = 0;
			sIncoming.Empty();
			liCategoryIncoming.RemoveAll();
			liSharedDirs.RemoveAll();
			liExcludedSharedDirs.RemoveAll();
			mapSingleExcludedFiles.RemoveAll();
		}

		bool		bAutoShareSubdirs;
		bool		bHasSingleExcludedFiles;
		LONG		lRuleGeneration;
		CString		sIncoming;
		CStringList	liCategoryIncoming;
		CStringList	liSharedDirs;
		CStringList	liExcludedSharedDirs;
		CMapStringToPtr mapSingleExcludedFiles;
	};

	void CheckSingleFile(const WIN32_FIND_DATA& wfd, const CString& rootDir, LONG lGeneration);
	bool ShouldAbortWork(LONG lGeneration) const;
	bool WaitForFoundFileQueueRoom(LONG lGeneration);
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
	volatile LONG				m_lQueuedFoundFiles;
	volatile LONG				m_lPendingSearchPaths;
	CSharedFileList*			m_owner;
};
