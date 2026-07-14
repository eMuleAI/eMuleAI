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
#include "otherfunctions.h"
#include "eMuleAI/Address.h"
#include "ClientStateDefs.h"
#include "MapKey.h"
#include <map>
#include <set>
#include <vector>

class CSafeMemFile;
class CSearchFile;
class CUpDownClient;
class CServer;
class CPartFile;
class CFileFind;
class CSharedFileList;
class CKnownFile;
class CED2KFileLink;
struct SUnresolvedHostname;

typedef CMap<CCKey, const CCKey&, CPartFile*, CPartFile*> CDownloadFilesByHashMap;

namespace Kademlia
{
	class CUInt128;
};


struct SDownloadItemId
{
	SDownloadItemId();

	void Clear();
	bool IsValid() const;
	void SetHash(const uchar *pFileHash);
	void SetFile(const CPartFile *pFile);
	bool EqualsHash(const uchar *pFileHash) const;
	bool Equals(const SDownloadItemId &other) const;

	uchar m_abyFileHash[MDX_DIGEST_SIZE];
	DWORD m_uRuntimeID;
};

struct SDownloadItemIdLess
{
	bool operator()(const SDownloadItemId& lhs, const SDownloadItemId& rhs) const;
};

struct SDownloadSourceId
{
	SDownloadSourceId();

	void Clear();
	bool IsValid() const;

	DWORD m_uClientRuntimeID;
};

struct SDownloadCategoryId
{
	SDownloadCategoryId();

	void Clear();
	bool IsValid() const;

	UINT m_uCategory;
};

class CSourceHostnameResolveWnd : public CWnd
{
	// Construction
public:
	CSourceHostnameResolveWnd();
	virtual	~CSourceHostnameResolveWnd();

	void AddToResolve(const uchar *fileid, LPCSTR pszHostname, uint16 port);

protected:
	DECLARE_MESSAGE_MAP()
	afx_msg LRESULT OnHostnameResolved(WPARAM, LPARAM lParam);

private:
	struct Hostname_Entry
	{
		uchar fileid[MDX_DIGEST_SIZE];
		CStringA strHostname;
		uint16 port;
		CString strURL;
	};
	CTypedPtrList<CPtrList, Hostname_Entry*> m_toresolve;
	char m_aucHostnameBuffer[MAXGETHOSTSTRUCT];
};


class CDownloadQueue
{
	friend class CAddFileThread;
	friend class CServerSocket;

public:
	CDownloadQueue();
	~CDownloadQueue();

	void	Process();
	void	Init();
	struct SStartupDownloadPartFile
	{
		SStartupDownloadPartFile();

		CPartFile *pFile;
		bool bRecoveredFromBackup;
	};

	struct SStartupDownloadSortItem
	{
		SStartupDownloadSortItem();

		CPartFile *pFile;
		UINT uCategory;
		int iCategoryPriority;
		uint8 uDownPriority;
		bool bAlphabetical;
		CString strFileName;
	};

	struct SStartupDownloadLoadResult
	{
		SStartupDownloadLoadResult();

		LONG lGeneration;
		uint64 uCancellationToken;
		bool bSuccess;
		bool bApplyStarted;
		DWORD dwLastError;
		CString strStage;
		std::vector<SStartupDownloadPartFile> vecPartFiles;
		size_t uNextPartFile;
		UINT uTempDirCount;
		UINT uLoadedCount;
		UINT uFinishStep;
		std::vector<SStartupDownloadSortItem> vecFinishSortItems;
		std::vector<SStartupDownloadSortItem> vecFinishSortScratch;
		POSITION posFinishSortCollectFile;
		POSITION posFinishSortApplyFile;
		size_t uNextFinishSortApply;
		size_t uFinishSortMergeWidth;
		size_t uFinishSortMergeLeft;
		size_t uFinishSortMergeMid;
		size_t uFinishSortMergeRight;
		size_t uFinishSortMergeI;
		size_t uFinishSortMergeJ;
		size_t uFinishSortMergeOut;
		bool bFinishSortCollected;
		bool bFinishSortSorted;
		bool bFinishSortMergeActive;
		POSITION posFinishDiskspaceFile;
		bool bFinishDiskspaceStarted;
		uint64 uFinishDiskspaceMainAvailable;
	};

	void	BeginStartupLoad();
	void	CancelStartupLoad();
	bool	IsStartupLoadActive() const						{ return m_bStartupLoadActive; }
	bool	IsStartupLoadCompleted() const					{ return m_bStartupLoadCompleted; }
	void	GetStartupLoadProgress(UINT& uLoaded, UINT& uTempDirIndex, UINT& uTempDirCount) const;
	void	PublishStartupLoadWorkerProgress(INT_PTR iTempDir, UINT uStagedCount);
	bool	LoadStartupPartFilesForWorker(SStartupDownloadLoadResult &result);
	bool	ApplyStartupDownloadLoadResult(SStartupDownloadLoadResult *pResult, size_t uMaxFiles, UINT &uApplied, INT_PTR &iRemaining);
	static void DeleteStartupDownloadLoadResult(SStartupDownloadLoadResult *pResult);

	// add/remove entries
	void	AddPartFilesToShare();
	class CBulkAddScope
	{
	public:
		explicit CBulkAddScope(CDownloadQueue* pQueue);
		~CBulkAddScope();
	private:
		CBulkAddScope(const CBulkAddScope&);
		CBulkAddScope& operator=(const CBulkAddScope&);
		CDownloadQueue* m_pQueue;
	};

	class CBulkRemoveScope
	{
	public:
		explicit CBulkRemoveScope(CDownloadQueue* pQueue);
		~CBulkRemoveScope();
	private:
		CBulkRemoveScope(const CBulkRemoveScope&);
		CBulkRemoveScope& operator=(const CBulkRemoveScope&);
		CDownloadQueue* m_pQueue;
	};

	void	AddDownload(CPartFile *newfile, bool paused);
	void	BeginBulkAddDownloads(bool bSuppressPerItemListUpdates = false, bool bDeferDownloadValidatorAdds = false);
	void	EndBulkAddDownloads();
	bool	IsBulkAddingDownloads() const						{ return m_uBulkAddDepth != 0; }
	bool	IsBulkAddDownloadValidatorAddsDeferred() const	{ return m_bBulkAddDeferDownloadValidatorAdds; }
	void	QueueDeferredDownloadValidatorAdd(CPartFile *pFile);
	void	RemoveDeferredDownloadValidatorAdd(CPartFile *pFile);
	bool	HasDeferredDownloadValidatorAdds() const			{ return !m_deferredDownloadValidatorAdds.IsEmpty(); }
	bool	ProcessDeferredDownloadValidatorAdds(bool bDrainAll = false);
	void	QueueDeferredInitialPartMetSave(CPartFile *pFile);
	void	RemoveDeferredInitialPartMetSave(CPartFile *pFile);
	void	ProcessDeferredPartFileCreates(bool bDrainAll = false);
	void	ProcessDeferredInitialPartMetSaves(bool bDrainAll = false);
	void	QueueDeferredSourceSaves(bool bForce);
	void	RemoveDeferredSourceSave(CPartFile *pFile);
	void	ProcessDeferredSourceSaves(bool bDrainAll = false);
	void	UpdateBulkAddDiskFinalizationProgress(bool bForceNotify = false);
	void	RequestBulkAddDiskFinalizationProgressUpdate(bool bForceNotify = false);
	bool	HasBulkAddDiskFinalizationProgressUpdate() const;
	void	ProcessBulkAddDiskFinalizationProgressUpdate();
	void	DrainDeferredPartFileDiskWorkForShutdown();
	void	SavePartFilesForShutdown();
	void	BeginBulkRemoveDownloads();
	void	EndBulkRemoveDownloads();
	bool	IsBulkRemovingDownloads() const					{ return m_uBulkRemoveDepth != 0; }
	void	AddSearchToDownload(CSearchFile *toadd, uint8 paused = 2, int cat = 0, bool bBypassDownloadValidator = false, bool bDeferSearchSources = false);
	void	AddSearchToDownload(const CString &link, uint8 paused = 2, int cat = 0);
	void	AddFileSnapshotToDownload(LPCTSTR pszFileName, uint64 uFileSize, const uchar *pFileHash, LPCTSTR pszAICHHash, int cat = 0);
	void	AddFileLinkToDownload(const CED2KFileLink &Link, int cat = 0);
	void	RemoveFile(CPartFile *toremove);
	void	DeleteAll();

	INT_PTR	GetFileCount() const							{ return filelist.GetCount(); }

	UINT	GetDownloadingFileCount() const;
	UINT	GetPausedFileCount() const;

	bool	IsFileExisting(const uchar *fileid, bool bLogWarnings = true) const;
	bool	IsPartFile(const CKnownFile *file) const;
	void	CollectCompletedFileHashes(CStringArray &astrFileHashes, int iCategory) const;

	CPartFile* GetFileByID(const uchar *filehash) const;
	CPartFile* GetFileByItemId(const SDownloadItemId &id) const;
	bool	GetDownloadItemId(const CPartFile *pFile, SDownloadItemId &id) const;
	uint64	GetDownloadModelSequence() const					{ return m_uModelSequence; }
	CPartFile* GetFileNext(POSITION& pos) const; //trivial iterator
	CPartFile* GetFileByKadFileSearchID(uint32 id) const;
	CPartFile* GetFileByRuntimeID(DWORD uRuntimeID) const;

	bool	SortStartupDownloadsByPrioritySlice(SStartupDownloadLoadResult *pResult, UINT &uProcessed, INT_PTR &iRemaining);

	void	StartNextFileIfPrefs(int cat);
	void	StartNextFile(int cat = -1, bool force = false);

	void	RefilterAllComments();

	// sources
	CUpDownClient* GetDownloadClientByIP(const CAddress& IP);
	CUpDownClient* GetDownloadClientByIP_UDP(const CAddress& IP, uint16 nUDPPort, bool bIgnorePortOnUniqueIP, bool* pbMultipleIPs = NULL);
	bool	IsInList(const CUpDownClient* client) const;
	void	RefreshDownloadSource(CUpDownClient *client);
	void	TriggerPendingNatTraversalDownloads(LPCTSTR pszReason);
	void	RestartSourceDiscoveryAfterStop(CPartFile *pFile);
	bool	HasPendingNatTraversalBuddyDemand();
	void	RegisterDownloadSource(CPartFile *pFile, CUpDownClient *client);
	void	UnregisterDownloadSource(CPartFile *pFile, CUpDownClient *client);

	bool	CheckAndAddSource(CPartFile *sender, CUpDownClient *source, ESourceFrom eSourceFrom, bool bSourceFromAuthoritative = true, CUpDownClient** ppResolvedSource = NULL);
	bool	CheckAndAddKnownSource(CPartFile *sender, CUpDownClient *source, bool bIgnoreGlobDeadList = false);
	bool	RebindSourceToServedEServerBuddy(CUpDownClient* pServedBuddy);
	CUpDownClient* CanonicalizeEServerRelaySource(CPartFile* pFile, CUpDownClient* pServerSource, CUpDownClient* pEndpointSource);
	bool	RemoveSource(CUpDownClient *toremove, bool bDoStatsUpdate = true);

	// statistics
	typedef struct
	{
		unsigned a[23];
	} SDownloadStats;
	void	GetDownloadSourcesStats(SDownloadStats &results);
	int		GetDownloadFilesStats(uint64 &rui64TotalFileSize, uint64 &rui64TotalLeftToTransfer, uint64 &rui64TotalAdditionalNeededSpace);
	uint32	GetDatarate() const								{ return m_datarate; }
	uint64	GetBufferedDownloadBytes() const					{ return m_uBufferedDownloadBytesSnapshot; }
	UINT	GetBufferedDownloadFileCount() const				{ return m_uBufferedDownloadFileCountSnapshot; }
	uint64	GetLargestBufferedDownloadFileBytes() const		{ return m_uLargestBufferedDownloadFileBytesSnapshot; }
	uint64	GetAdaptiveGlobalDownloadBufferBudgetBytes() const;
	uint64	GetEffectiveFileBufferSizeBytes(uint64 uCurrentFileBufferedBytes = 0) const;
	bool	ShouldFlushFileForAdaptiveBufferBudget(uint64 uCurrentFileBufferedBytes) const;
	void	RefreshAdaptiveDownloadBufferSnapshot();

	void	AddUDPFileReasks()								{ ++m_nUDPFileReasks; }
	uint32	GetUDPFileReasks() const						{ return m_nUDPFileReasks; }
	void	AddFailedUDPFileReasks()						{ ++m_nFailedUDPFileReasks; }
	uint32	GetFailedUDPFileReasks() const					{ return m_nFailedUDPFileReasks; }

	// categories
	void	ResetCatParts(UINT cat);
	void	SetCatPrio(UINT cat, uint8 newprio);
	void	RemoveAutoPrioInCat(UINT cat, uint8 newprio); // ZZ:DownloadManager
	void	SetCatStatus(UINT cat, int newstatus);
	void	MoveCat(UINT from, UINT to);
	static void	SetAutoCat(CPartFile *newfile);

	// searching on local server
	void	SendLocalSrcRequest(CPartFile *sender);
	void	RemoveLocalServerRequest(CPartFile *pFile);
	void	ResetLocalServerRequests();

	// searching in Kad
	void	SetLastKademliaFileRequest()					{ m_lastkademliafilerequest = ::GetTickCount(); }
	bool	DoKademliaFileRequest() const;
	void	KademliaSearchFile(uint32 nSearchID, const Kademlia::CUInt128* pcontactID, const Kademlia::CUInt128* pbuddyID, uint8 type, uint32 ip, uint16 tcp, uint16 udp, uint32 dwServingBuddyIP, uint16 dwServingBuddyPort, uint8 byCryptOptions, const Kademlia::CUInt128* pIPv6, const Kademlia::CUInt128* pBuddyIPv6);

	// searching on global servers
	void	StopUDPRequests();

	// check disk space
	void	SortByPriority();
	void	CheckDiskspace(bool bNotEnoughSpaceLeft = false);
	void	CheckDiskspaceTimed();

	void	ExportPartMetFilesOverview();
	bool	ProcessQueuedPartMetFilesOverviewExport();
	void	OnConnectionState(bool bConnected);

	void	AddToResolved(CPartFile *pFile, SUnresolvedHostname *pUH);

	CString	GetOptimalTempDir(UINT nCat, EMFileSize nFileSize);

	CServer	*cur_udpserver;

	uint32	m_TCPFileReask;
	uint32	m_FailedTCPFileReask;
	void	IncrementTCPFileReask() { m_TCPFileReask++; }
	uint32	GetTCPFileReasks() const { return m_TCPFileReask; }
	void	IncrementFailedTCPFileReask() { m_FailedTCPFileReask++; }
	uint32	GetFailedTCPFileReasks() const { return m_FailedTCPFileReask; }
protected:
	bool	SendNextUDPPacket();
	void	ProcessLocalRequests();
	bool	IsMaxFilesPerUDPServerPacketReached(uint32 nFiles, uint32 nIncludedLargeFiles) const;
	bool	SendGlobGetSourcesUDPPacket(CSafeMemFile &data, bool bExt2Packet, uint32 nFiles, uint32 nIncludedLargeFiles);

private:
	void	FinalizeBulkAddDownloads();
	void	FinalizeBulkRemoveDownloads();
	void	TouchDownloadModelSequence();
	bool	StartInitialKadSourceLookup(CPartFile *pFile);
	void	IndexDownloadFile(CPartFile *pFile);
	void	UnindexDownloadFile(const CPartFile *pFile);
	struct SDownloadSourceHashKey;
	struct SDownloadSourceEndpointKey;
	struct SDownloadSourceIdPortKey;
	struct SDownloadSourceLowIdKey;
	struct SDownloadSourceIndexSnapshot;
	struct SDownloadSourceIndexEntry;
	void	BuildDownloadSourceIndexSnapshot(CUpDownClient *client, SDownloadSourceIndexSnapshot& snapshot) const;
	void	EraseDownloadSourceIndexSnapshot(DWORD uRuntimeID, const SDownloadSourceIndexSnapshot& snapshot);
	bool	IsDownloadSourceIndexSnapshotCurrent(CUpDownClient *client, DWORD uRuntimeID) const;
	CUpDownClient* ResolveDownloadSourceRuntimeID(DWORD uRuntimeID) const;
	CPartFile* GetDownloadSourceFile(CUpDownClient *client) const;
	CPartFile* FindDownloadSourceOwnerByScan(CUpDownClient *source) const;
	CUpDownClient* FindDownloadDuplicateSourceByScan(CUpDownClient *source, CPartFile*& pOwnerFile) const;
	bool	AddAlreadyKnownSourceAsA4AF(CPartFile *sender, CUpDownClient *source, CPartFile *pOwnerFile, LPCTSTR pszContext);
	CUpDownClient* FindDownloadDuplicateSource(CUpDownClient *source, CPartFile*& pOwnerFile) const;
	void	ClearDownloadSourceIndexes();
	bool	FinishStartupLoadStep(SStartupDownloadLoadResult *pResult, UINT &uProcessed, INT_PTR &iRemaining);
	bool	CheckStartupDiskspaceSlice(SStartupDownloadLoadResult *pResult, UINT &uProcessed, INT_PTR &iRemaining);
	static void	DeleteStartupDownloadPartFiles(std::vector<SStartupDownloadPartFile> &vecPartFiles);
	void	RefreshStartupLoadDisplayCounts(bool bForce);

public:
	CTypedPtrList<CPtrList, CPartFile*> filelist;
private:
	CTypedPtrList<CPtrList, CPartFile*> m_localServerReqQueue;

	// By BadWolf - Accurate Speed Measurement
	typedef struct
	{
		uint32	datalen;
		DWORD	timestamp; //tick count
	} TransferredData;
	CList<TransferredData> average_dr_list;
	// END By BadWolf - Accurate Speed Measurement

	CSourceHostnameResolveWnd m_srcwnd;
	uint64	m_datarateMS;
	CPartFile *m_lastfile;
	DWORD	m_dwLastA4AFtime; // ZZ:DownloadManager
	UINT	CountPendingDeferredPartFileDiskWork();
	UINT	CountPendingBulkAddDiskFinalizationFiles();
	bool	IsBulkAddDiskFinalizationActive() const;
	UINT	FlushQueuedPartFileCreatesSynchronously();
	UINT	FlushPendingPartFileCreatesSynchronously();
	UINT	ProcessDeferredPartFileCreateResults(bool bDrainAll, DWORD dwSliceStart, UINT &uProcessed, UINT uMaxResults);
	bool	QueuePendingPartFileCreates(bool bDrainAll, DWORD dwSliceStart, UINT &uProcessed);
	void	StartBulkAddDiskFinalization(UINT uTotal);
	bool	ShouldSaveSourcesForFile(const CPartFile *pFile, bool bIncludePaused) const;

	DWORD	m_lastudpsearchtime;
	DWORD	m_lastudpstattime;
	DWORD	m_lastkademliafilerequest;
	DWORD	m_dwNextTCPSrcReq;
	UINT	m_udcounter;
	UINT	m_cRequestsSentToServer;
	int		m_iSearchedServers;

	struct SDeferredDownloadValidatorAdd
	{
		SDeferredDownloadValidatorAdd();

		CPartFile *pFile;
		uchar abyFileHash[MDX_DIGEST_SIZE];
		CString strFileName;
		uint64 uFileSize;
	};

	uint32	m_nUDPFileReasks;
	uint32	m_nFailedUDPFileReasks;
	uint32	m_datarate;
	UINT	m_uBulkAddDepth;
	UINT	m_uBulkAddedFiles;
	bool	m_bBulkAddPending;
	bool	m_bBulkAddSuppressPerItemListUpdates;
	bool	m_bBulkAddDeferDownloadValidatorAdds;
	bool	m_bBulkAddOverviewExportDeferred;
	std::vector<SDownloadItemId> m_bulkAddedDownloadIds;
	std::vector<SDownloadItemId> m_bulkAddDiskFinalizationIds;
	volatile LONG	m_lBulkAddDiskFinalizationActive;
	UINT	m_uBulkAddDiskFinalizationTotal;
	DWORD	m_dwLastBulkAddDiskFinalizationNotifyTick;
	volatile LONG	m_lBulkAddDiskFinalizationProgressUpdatePending;
	volatile LONG	m_lBulkAddDiskFinalizationForceNotifyPending;
	bool	m_bStartupLoadActive;
	bool	m_bStartupLoadCompleted;
	INT_PTR	m_iStartupLoadTempDir;
	int		m_iStartupLoadCount;
	int		m_iStartupLoadStagedCount;
	DWORD	m_dwLastStartupLoadDisplayRefreshTick;
	CTypedPtrList<CPtrList, SDeferredDownloadValidatorAdd*> m_deferredDownloadValidatorAdds;
	std::map<CPartFile*, POSITION> m_deferredDownloadValidatorAddPositions;
	CTypedPtrList<CPtrList, CPartFile*> m_deferredInitialPartMetSaves;
	POSITION m_posDeferredPartFileCreateQueueFile;
	bool m_bDeferredPartFileCreateQueuePending;
	std::set<CPartFile*> m_deferredInitialPartMetSaveSet;
	std::map<CPartFile*, POSITION> m_deferredInitialPartMetSavePositions;
	std::vector<SDownloadItemId> m_deferredSourceSaves;
	std::set<SDownloadItemId, SDownloadItemIdLess> m_deferredSourceSaveSet;
	bool m_bDeferredSourceSavesIncludePaused;
	UINT	m_uBulkRemoveDepth;
	UINT	m_uBulkRemovedFiles;
	bool	m_bBulkRemovePending;
	bool	m_bShutdownPartFilesSaved;
	UINT	m_uShutdownPartFilesSavedCount;
	UINT	m_uShutdownPartFileProgressTotal;
	std::map<CPartFile*, POSITION>	m_bulkRemoveFilePositions;
	CDownloadFilesByHashMap m_mapFilesByHash;
	struct SDownloadSourceHashKey
	{
		SDownloadSourceHashKey();
		explicit SDownloadSourceHashKey(const uchar* pHash);

		bool operator<(const SDownloadSourceHashKey& other) const;
		bool operator==(const SDownloadSourceHashKey& other) const;

		uchar m_abyHash[16];
	};
	struct SDownloadSourceEndpointKey
	{
		SDownloadSourceEndpointKey();
		SDownloadSourceEndpointKey(const CAddress& ip, uint16 uPort);

		bool operator<(const SDownloadSourceEndpointKey& other) const;
		bool operator==(const SDownloadSourceEndpointKey& other) const;

		CAddress m_ip;
		uint16 m_uPort;
	};
	struct SDownloadSourceIdPortKey
	{
		SDownloadSourceIdPortKey();
		SDownloadSourceIdPortKey(uint32 uUserID, uint16 uPort);

		bool operator<(const SDownloadSourceIdPortKey& other) const;
		bool operator==(const SDownloadSourceIdPortKey& other) const;

		uint32 m_uUserID;
		uint16 m_uPort;
	};
	struct SDownloadSourceLowIdKey
	{
		SDownloadSourceLowIdKey();
		SDownloadSourceLowIdKey(uint32 uUserID, uint32 uServerIP, uint16 uServerPort);

		bool operator<(const SDownloadSourceLowIdKey& other) const;
		bool operator==(const SDownloadSourceLowIdKey& other) const;

		uint32 m_uUserID;
		uint32 m_uServerIP;
		uint16 m_uServerPort;
	};
	struct SDownloadSourceIndexSnapshot
	{
		SDownloadSourceIndexSnapshot();

		bool operator==(const SDownloadSourceIndexSnapshot& other) const;

		bool m_bHasHash;
		SDownloadSourceHashKey m_hashKey;
		std::vector<CAddress> m_aIPKeys;
		std::vector<SDownloadSourceEndpointKey> m_aEndpointKeys;
		std::vector<SDownloadSourceEndpointKey> m_aUDPKeys;
		std::vector<SDownloadSourceIdPortKey> m_aIdPortKeys;
		bool m_bHasLowId;
		SDownloadSourceLowIdKey m_lowIdKey;
	};
	struct SDownloadSourceIndexEntry
	{
		SDownloadSourceIndexEntry();

		CPartFile *m_pFile;
		CUpDownClient *m_pClient;
		SDownloadSourceIndexSnapshot m_snapshot;
	};
	std::map<DWORD, SDownloadSourceIndexEntry> m_mapDownloadSourceEntries;
	std::multimap<SDownloadSourceHashKey, DWORD> m_mapDownloadSourcesByHash;
	std::multimap<CAddress, DWORD> m_mapDownloadSourcesByIP;
	std::multimap<SDownloadSourceEndpointKey, DWORD> m_mapDownloadSourcesByEndpoint;
	std::multimap<SDownloadSourceEndpointKey, DWORD> m_mapDownloadSourcesByUDP;
	std::multimap<SDownloadSourceIdPortKey, DWORD> m_mapDownloadSourcesByIdPort;
	std::multimap<SDownloadSourceLowIdKey, DWORD> m_mapDownloadSourcesByLowId;
	uint64	m_uModelSequence;
	uint64	m_uBufferedDownloadBytesSnapshot;
	uint64	m_uLargestBufferedDownloadFileBytesSnapshot;
	uint64	m_uAdaptiveGlobalDownloadBufferBudgetBytesSnapshot;
	UINT	m_uBufferedDownloadFileCountSnapshot;
};
