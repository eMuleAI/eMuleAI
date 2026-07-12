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
#include "eMuleAI/Address.h"
#include <map>
#include <set>
#include <utility>
#include <vector>

struct Requested_Block_Struct;
class CUpDownClient;
typedef CTypedPtrList<CPtrList, CUpDownClient*> CUpDownClientPtrList;


struct SClientItemId
{
	SClientItemId();

	void Clear();
	bool IsValid() const;
	bool operator<(const SClientItemId& other) const;
	bool operator==(const SClientItemId& other) const;

	DWORD m_uRuntimeID;
};


struct SUploadBlockRequestKey
{
	SUploadBlockRequestKey();
	SUploadBlockRequestKey(uint64 uStartOffset, uint64 uEndOffset, const uchar *pFileID);

	bool operator<(const SUploadBlockRequestKey& other) const;

	uint64 m_uStartOffset;
	uint64 m_uEndOffset;
	uchar m_abyFileID[16];
};


struct UploadingToClient_Struct
{
	UploadingToClient_Struct()
		: m_pClient()
		, m_bIOError()
		, m_bDisableCompression()
		, m_bRetired()
		, m_nPendingIOBlocks()
		, m_dwRetiredTick()
		, m_dwLastRetiredPendingIOLogTick()
	{
	}
	~UploadingToClient_Struct();

	CUpDownClient												*m_pClient;
	CTypedPtrList<CPtrList, Requested_Block_Struct*>	m_BlockRequests_queue;
	CTypedPtrList<CPtrList, Requested_Block_Struct*>	m_DoneBlocks_list;
	std::set<SUploadBlockRequestKey>					m_BlockRequests_keys;
	std::set<SUploadBlockRequestKey>					m_DoneBlocks_keys;
	CCriticalSection									m_csBlockListsLock; // don't acquire other locks while having this one in any thread other than UploadDiskIOThread or make sure deadlocks are impossible
	bool												m_bIOError;
	bool												m_bDisableCompression;
	bool												m_bRetired;
	volatile LONG									m_nPendingIOBlocks;
	DWORD										m_dwRetiredTick;
	DWORD										m_dwLastRetiredPendingIOLogTick;
};
typedef CTypedPtrList<CPtrList, UploadingToClient_Struct*> CUploadingPtrList;

class CUploadQueue
{

public:
	CUploadQueue();
	~CUploadQueue();

	void	Process();
	void	AddClientToQueue(CUpDownClient *client, bool bIgnoreTimelimit = false);
	bool	RemoveFromUploadQueue(CUpDownClient *client, LPCTSTR pszReason = NULL, bool updatewindow = true, bool earlyabort = false);
	bool	RemoveFromWaitingQueue(CUpDownClient *client, bool updatewindow = true);
	bool	IsOnUploadQueue(CUpDownClient *client)	const;
	void	RefreshWaitingClient(CUpDownClient *client);
	bool	IsDownloading(const CUpDownClient *client)	const { return (GetUploadingClientStructByClient(client) != NULL); }

	void	UpdateDatarates();
	uint32	GetDatarate() const								{ return datarate; }
	bool	HasActiveUploads() const						{ return m_bHasActiveUploads; }
	uint32	GetToNetworkDatarate() const;

	bool	CheckForTimeOver(CUpDownClient *client, CString *pstrReason = NULL);
	INT_PTR	GetWaitingUserCount() const						{ return waitinglist.GetCount(); }
	INT_PTR	GetUploadQueueLength() const					{ return uploadinglist.GetCount(); }
	INT_PTR	GetActiveUploadsCount()	const					{ return m_MaxActiveClientsShortTime; }
	uint32	GetWaitingUserForFileCount(const CSimpleArray<CObject*> &raFiles, bool bOnlyIfChanged);
	uint32	GetDatarateForFile(const CSimpleArray<CObject*> &raFiles) const;
	uint32	GetTargetClientDataRate(bool bMinDatarate) const;
	UINT	GetHighBandwidthUploadThrottlerSlotLimit() const;
	UINT	GetUploadBufferBlockCount(const CUpDownClient *client) const;

	POSITION GetFirstFromUploadList() const					{ return uploadinglist.GetHeadPosition(); }
	CUpDownClient* GetNextFromUploadList(POSITION &curpos) const { return static_cast<UploadingToClient_Struct*>(uploadinglist.GetNext(curpos))->m_pClient; }
	CUpDownClient* GetQueueClientAt(POSITION &curpos) const	{ return static_cast<UploadingToClient_Struct*>(uploadinglist.GetAt(curpos))->m_pClient; }

	POSITION GetFirstFromWaitingList() const				{ return waitinglist.GetHeadPosition(); }
	CUpDownClient* GetNextFromWaitingList(POSITION &curpos) const { return waitinglist.GetNext(curpos); }
	CUpDownClient* GetWaitClientAt(POSITION &curpos) const	{ return waitinglist.GetAt(curpos); }

	CUpDownClient* GetWaitingClientByIP_UDP(const CAddress& IP, uint16 nUDPPort, bool bIgnorePortOnUniqueIP, bool* pbMultipleIPs = NULL);
	CUpDownClient* GetWaitingClientByIP(const CAddress& IP) const;

	static bool GetClientItemId(const CUpDownClient* pClient, SClientItemId& id);
	CUpDownClient* AcquireClientByItemId(const SClientItemId& id) const;

	void PruneWaitersForMissingSharedFiles();
	bool PruneWaitersForMissingSharedFilesChunk(UINT uMaxClients, UINT& uProcessed, INT_PTR& iRemaining);

	CUpDownClient* GetNextClient(const CUpDownClient *lastclient) const;

	UploadingToClient_Struct* GetUploadingClientStructByClient(const CUpDownClient *pClient) const;
	bool HasUploadClientStructForDiskIO(const UploadingToClient_Struct *pUploadClientStruct, bool& bActive, bool& bRetired) const;

	const CUploadingPtrList& GetUploadListTS(CCriticalSection **outUploadListReadLock);


	void	DeleteAll();
	UINT	GetWaitingPosition(CUpDownClient *client);

	uint32	GetSuccessfullUpCount() const					{ return successfullupcount; }
	uint32	GetFailedUpCount() const						{ return failedupcount; }
	uint32	GetAverageUpTime() const;

	CUpDownClient* FindBestClientInQueue();
	CUpDownClient* FindBestClientInQueueExact(bool bSkipUploadBanned, bool bAllowCleanup);
	bool TryAdmitQueuedBlockRequestClient(CUpDownClient *client);

	CUpDownClientPtrList waitinglist;

	void SaveAppState(bool bAutoSave);
	void SaveClientCreditList();
protected:
	void		RemoveFromWaitingQueue(POSITION pos, bool updatewindow);
	bool		AcceptNewClient(bool addOnNextConnect = false) const;
	bool		AcceptNewClient(INT_PTR curUploadSlots) const;
	bool		ForceNewClient(bool allowEmptyWaitingQueue = false);
	uint32		GetHighBandwidthTargetUploadClients() const;
	bool		IsHighBandwidthUploadPolicyActive() const;
	UINT		GetHighBandwidthTargetUploadSlots() const;
	UINT		GetHighBandwidthEffectiveUploadSlotLimit() const;
	bool		HasSustainedElasticHighBandwidthUploadUnderfill(DWORD curTick) const;
	uint64		GetHighBandwidthUploadBudgetBytesPerSec() const;
	uint32		GetTargetClientDataRateHighBandwidth(bool bMinDatarate) const;
	bool		IsHighBandwidthUploadUnderfilled(uint64 uUploadBudgetBytesPerSec) const;
	void		UpdateHighBandwidthUploadUnderfillState(DWORD curTick);
	bool		HasSustainedHighBandwidthUploadUnderfill(DWORD curTick) const;
	bool		ShouldProbeHighBandwidthUploadCooldownCandidate(DWORD curTick) const;
	bool		CanProbeHighBandwidthUploadCooldownClient(const CUpDownClient *client, DWORD curTick) const;
	bool		HasHighBandwidthUploadAdmissionCandidate(DWORD curTick);
	bool		ShouldRecycleSlowHighBandwidthUpload(CUpDownClient *client, DWORD curTick, CString *pstrReason = NULL);
	CAddress	GetHighBandwidthUploadRetryCooldownAddress(const CUpDownClient *client) const;
	bool		IsHighBandwidthUploadRetryCooldownActive(const CUpDownClient *client, DWORD curTick) const;
	void		SetHighBandwidthUploadRetryCooldown(CUpDownClient *client, DWORD curTick, UINT uReason);
	void		ClearHighBandwidthUploadRetryCooldown(CUpDownClient *client, DWORD curTick);
	void		PurgeExpiredHighBandwidthUploadRetryCooldowns(DWORD curTick);
	enum EUploadRequestAbuseEvent
	{
		UploadRequestAbuseNoRequestSlot,
		UploadRequestAbuseQueueReaskDrop
	};
	struct SUploadRequestAbuseStringLess
	{
		bool operator()(const CString& left, const CString& right) const { return left.Compare(right) < 0; }
	};
	struct SUploadRequestAbuseState
	{
		SUploadRequestAbuseState();

		DWORD m_dwWindowUntil;
		UINT m_uStrikes;
	};
	struct SUploadRequestAbuseIPState
	{
		SUploadRequestAbuseIPState();

		DWORD m_dwWindowUntil;
		UINT m_uTotalStrikes;
		std::set<CString, SUploadRequestAbuseStringLess> m_setHashKeys;
	};
	CString		BuildUploadRequestAbuseClientKey(const CUpDownClient *client) const;
	CString		BuildUploadRequestAbuseFileKey(const uchar *pFileHash) const;
	CString		BuildUploadRequestAbuseEventKey(EUploadRequestAbuseEvent eEvent, const CUpDownClient *client, const uchar *pFileHash) const;
	CString		BuildUploadRequestAbuseIPRotationKey(EUploadRequestAbuseEvent eEvent, const CUpDownClient *client) const;
	bool		TrackUploadRequestAbuseEvent(CUpDownClient *client, const uchar *pFileHash, DWORD curTick, EUploadRequestAbuseEvent eEvent, bool *pbSuppressRequestStatistic = NULL);
	bool		ApplyUploadRequestAbusePunishment(CUpDownClient *client, LPCTSTR pszReasonKey, bool bForceIPUserHashBan = false);
	void		PurgeExpiredUploadRequestAbuseTracking(DWORD curTick);
	bool		ShouldUseUploadSocketSendBuffer(uint32 uClientDatarate) const;
	uint32		GetUploadSocketSendBufferBytes() const;
	bool		CanRotateUploadSession() const;
	bool		AddUpNextClient(LPCTSTR pszReason, CUpDownClient *directadd = NULL);

	static VOID CALLBACK UploadTimer(HWND hWnd, UINT nMsg, UINT_PTR nId, DWORD dwTime) noexcept;

private:
	struct SUploadWaitingIndexSnapshot;

	void	UpdateMaxClientScore();
	void	RestartMaxClientScoreRecalculation();
	bool	ProcessMaxClientScoreRecalculationChunk(UINT uMaxClients, UINT& uProcessed, INT_PTR& iRemaining);
	void	RestartAverageCombinedFilePrioAndCreditRecalculation();
	bool	ProcessAverageCombinedFilePrioAndCreditChunk(UINT uMaxClients, UINT& uProcessed, INT_PTR& iRemaining);
	void	RestartWaitingRankRecalculation();
	bool	ProcessWaitingRankRecalculationChunk(UINT uMaxClients, UINT& uProcessed, INT_PTR& iRemaining);
	void	RestartBestClientRecalculation();
	bool	ProcessBestClientRecalculationChunk(UINT uMaxClients, UINT& uProcessed, INT_PTR& iRemaining);
	void	RegisterWaitingClient(CUpDownClient *client, POSITION pos);
	void	UnregisterWaitingClient(CUpDownClient *client);
	bool	GetWaitingClientPosition(const CUpDownClient *client, POSITION& pos) const;
	CUpDownClient* ResolveWaitingClientRuntimeID(DWORD uRuntimeID) const;
	bool	IsWaitingClientAddressMatch(const CUpDownClient *client, const CAddress& ip) const;
	CUpDownClient* FindWaitingDuplicateClient(CUpDownClient *client, POSITION& posDuplicate) const;
	UINT	GetWaitingClientIPCount(const CAddress& ip) const;
	void	RegisterWaitingClientIndexes(CUpDownClient *client, DWORD uRuntimeID);
	void	UnregisterWaitingClientIndexes(CUpDownClient *client, DWORD uRuntimeID);
	void	BuildWaitingClientIndexSnapshot(CUpDownClient *client, SUploadWaitingIndexSnapshot& snapshot) const;
	void	EraseWaitingClientIndexSnapshot(DWORD uRuntimeID, const SUploadWaitingIndexSnapshot& snapshot);
	bool	IsWaitingClientIndexSnapshotCurrent(CUpDownClient *client, DWORD uRuntimeID) const;
	UINT	GetExactWaitingPosition(CUpDownClient *client) const;
	void	InvalidateMaxClientScoreRecalculation(bool bQueueRefresh);
	void	StartPruneWaitersForMissingSharedFilesSnapshot();
	void	FinishPruneWaitersForMissingSharedFilesSnapshot();
	uint32	GetMaxClientScore() const						{ return m_imaxscore; }
	void	UpdateActiveClientsInfo(DWORD curTick);

	enum EUploadTimerMaintenanceJob
	{
		UploadTimerMaintenanceKadSearchReload,
		UploadTimerMaintenanceClientCredits,
		UploadTimerMaintenanceServerList,
		UploadTimerMaintenanceKnownFiles,
		UploadTimerMaintenanceFriendList,
		UploadTimerMaintenanceClientList,
		UploadTimerMaintenanceSharedFiles,
		UploadTimerMaintenanceSearchList,
		UploadTimerMaintenanceKad,
		UploadTimerMaintenanceServerConnectNext,
		UploadTimerMaintenanceListenSocketStatus,
		UploadTimerMaintenanceClipboard,
		UploadTimerMaintenanceServerConnectTimeout,
		UploadTimerMaintenanceClientListCleanup,
		UploadTimerMaintenanceListenSocketProcess,
		UploadTimerMaintenanceMaxClientScore,
		UploadTimerMaintenanceAverageCombinedFilePrioAndCredit,
		UploadTimerMaintenanceWaitingRankCache,
		UploadTimerMaintenanceBestClientInQueue,
		UploadTimerMaintenanceScheduler,
		UploadTimerMaintenanceTransferListCount,
		UploadTimerMaintenanceBuddyMatchmaking,
		UploadTimerMaintenanceWebServerSessions,
		UploadTimerMaintenanceKeepAlive,
		UploadTimerMaintenancePreventStandby,
		UploadTimerMaintenanceDownloadInspector,
		UploadTimerMaintenanceAutoQuerySharedFiles,
		UploadTimerMaintenanceBackup,
		UploadTimerMaintenanceBuddyPings,
		UploadTimerMaintenanceKnownMetSaveJob,
		UploadTimerMaintenanceSharedFilesFound,
		UploadTimerMaintenanceCount
	};

	struct SUploadTimerMaintenanceJobState
	{
		SUploadTimerMaintenanceJobState();

		bool m_bPending;
		DWORD m_dwQueuedTick;
	};

	void MarkUploadTimerMaintenanceJob(EUploadTimerMaintenanceJob eJob);
	void ProcessUploadTimerMaintenanceSlice();
	void RunUploadTimerMaintenanceJob(EUploadTimerMaintenanceJob eJob);
	LPCTSTR GetUploadTimerMaintenanceJobName(EUploadTimerMaintenanceJob eJob) const;

	void InsertInUploadingList(CUpDownClient *newclient, bool bNoLocking);
	void InsertInUploadingList(UploadingToClient_Struct *pNewClientUploadStruct, bool bNoLocking);
	void InvalidateUploadClientStruct(UploadingToClient_Struct *pUploadClientStruct, CUpDownClient *pClient);
	void ReclaimRetiredUploadClientStructs();
	float GetAverageCombinedFilePrioAndCredit();

	// By BadWolf - Accurate Speed Measurement
	typedef struct
	{
		uint32	datalen;
		DWORD	timestamp;
	} TransferredData;

	CUploadingPtrList	uploadinglist;
	CUploadingPtrList	m_retiredUploadingList;
	// this lock ensures that only the main thread writes the uploading list, other threads need to fetch the lock if they want to read (but are not allowed to write)
	CCriticalSection	m_csUploadListMainThrdWriteOtherThrdsRead; // don't acquire other locks while having this one in any thread other than UploadDiskIOThread or make sure deadlocks are impossible

	CList<uint64> average_dr_list;
	CList<uint64> average_friend_dr_list;
	CList<DWORD, DWORD> average_tick_list;
	CList<int, int> activeClients_list;
	CList<DWORD, DWORD> activeClients_tick_list;
	uint32	datarate;   //data rate sent to network (including friends)
	uint32  friendDatarate; // data rate of sent to friends (included in above total)
	// By BadWolf - Accurate Speed Measurement

	UINT_PTR h_timer;
	uint32	successfullupcount;
	uint32	failedupcount;
	uint32	totaluploadtime;
	DWORD	m_nLastStartUpload;
	uint32	m_dwRemovedClientByScore;
	uint32	m_imaxscore;

	DWORD	m_dwLastCalculatedAverageCombinedFilePrioAndCredit;
	float	m_fAverageCombinedFilePrioAndCredit;
	INT_PTR	m_iHighestNumberOfFullyActivatedSlotsSinceLastCall;
	INT_PTR	m_MaxActiveClients;
	INT_PTR	m_MaxActiveClientsShortTime;

	DWORD	m_lastCalculatedDataRateTick;
	uint64	m_average_dr_sum;
	struct SHighBandwidthUploadRetryCooldownState
	{
		SHighBandwidthUploadRetryCooldownState();

		DWORD m_dwCooldownUntil;
	};

	std::map<CAddress, SHighBandwidthUploadRetryCooldownState> m_mapHighBandwidthUploadRetryCooldownByAddress;
	std::map<CString, SUploadRequestAbuseState, SUploadRequestAbuseStringLess> m_mapUploadRequestAbuseByClientFile;
	std::map<CString, SUploadRequestAbuseIPState, SUploadRequestAbuseStringLess> m_mapUploadRequestAbuseByIP;
	DWORD	m_dwLastUploadRequestAbuseCleanupTick;
	DWORD	m_dwHighBandwidthUploadUnderfillSince;
	DWORD	m_dwLastHighBandwidthSlowRecycleTick;

	DWORD	m_dwLastResortedUploadSlots;
	SUploadTimerMaintenanceJobState m_aUploadTimerMaintenanceJobs[UploadTimerMaintenanceCount];
	UINT m_uNextUploadTimerMaintenanceJob;
	bool	m_bStatisticsWaitingListDirty;
	bool	m_bHasActiveUploads;
	POSITION m_posMaxClientScoreRecalc;
	POSITION m_posAverageCombinedFilePrioAndCreditRecalc;
	UINT m_uWaitingListGeneration;
	UINT m_uMaxClientScoreRecalcGeneration;
	UINT m_uAverageCombinedFilePrioAndCreditGeneration;
	INT_PTR m_iMaxClientScoreRecalcProcessed;
	INT_PTR m_iAverageCombinedFilePrioAndCreditProcessed;
	uint32 m_uMaxClientScoreRecalcMax;
	double m_fAverageCombinedFilePrioAndCreditSum;
	bool m_bMaxClientScoreRecalcActive;
	bool m_bAverageCombinedFilePrioAndCreditRecalcActive;
	bool m_bAverageCombinedFilePrioAndCreditValid;
	struct SWaitingRankRequest
	{
		SWaitingRankRequest();

		DWORD m_uRuntimeID;
		uint32 m_uScore;
		UINT m_uRank;
	};
	struct SUploadWaitingHashKey
	{
		SUploadWaitingHashKey();
		explicit SUploadWaitingHashKey(const uchar* pHash);

		bool operator<(const SUploadWaitingHashKey& other) const;
		bool operator==(const SUploadWaitingHashKey& other) const;

		uchar m_abyHash[16];
	};
	struct SUploadWaitingEndpointKey
	{
		SUploadWaitingEndpointKey();
		SUploadWaitingEndpointKey(const CAddress& ip, uint16 uPort);

		bool operator<(const SUploadWaitingEndpointKey& other) const;
		bool operator==(const SUploadWaitingEndpointKey& other) const;

		CAddress m_ip;
		uint16 m_uPort;
	};
	struct SUploadWaitingIdPortKey
	{
		SUploadWaitingIdPortKey();
		SUploadWaitingIdPortKey(uint32 uUserID, uint16 uPort);

		bool operator<(const SUploadWaitingIdPortKey& other) const;
		bool operator==(const SUploadWaitingIdPortKey& other) const;

		uint32 m_uUserID;
		uint16 m_uPort;
	};
	struct SUploadWaitingLowIdKey
	{
		SUploadWaitingLowIdKey();
		SUploadWaitingLowIdKey(uint32 uUserID, uint32 uServerIP, uint16 uServerPort);

		bool operator<(const SUploadWaitingLowIdKey& other) const;
		bool operator==(const SUploadWaitingLowIdKey& other) const;

		uint32 m_uUserID;
		uint32 m_uServerIP;
		uint16 m_uServerPort;
	};
	struct SUploadWaitingIndexSnapshot
	{
		SUploadWaitingIndexSnapshot();

		bool operator==(const SUploadWaitingIndexSnapshot& other) const;

		bool m_bHasHash;
		SUploadWaitingHashKey m_hashKey;
		std::vector<SUploadWaitingEndpointKey> m_aEndpointKeys;
		std::vector<SUploadWaitingEndpointKey> m_aUDPKeys;
		std::vector<SUploadWaitingIdPortKey> m_aIdPortKeys;
		bool m_bHasLowId;
		SUploadWaitingLowIdKey m_lowIdKey;
		std::vector<CAddress> m_aIPKeys;
	};

	std::set<DWORD> m_setWaitingClientRuntimeIDs;
	std::map<DWORD, POSITION> m_mapWaitingClientPositions;
	std::multimap<SUploadWaitingHashKey, DWORD> m_mapWaitingClientsByHash;
	std::multimap<SUploadWaitingEndpointKey, DWORD> m_mapWaitingClientsByEndpoint;
	std::multimap<SUploadWaitingEndpointKey, DWORD> m_mapWaitingClientsByUDP;
	std::multimap<SUploadWaitingIdPortKey, DWORD> m_mapWaitingClientsByIdPort;
	std::multimap<SUploadWaitingLowIdKey, DWORD> m_mapWaitingClientsByLowId;
	std::multimap<CAddress, DWORD> m_mapWaitingClientsByIP;
	std::map<DWORD, SUploadWaitingIndexSnapshot> m_mapWaitingClientIndexSnapshots;
	std::map<DWORD, UINT> m_mapWaitingRankCache;
	std::map<DWORD, SWaitingRankRequest> m_mapWaitingRankRequests;
	POSITION m_posWaitingRankRecalc;
	UINT m_uWaitingRankRecalcGeneration;
	INT_PTR m_iWaitingRankRecalcProcessed;
	bool m_bWaitingRankRecalcActive;
	POSITION m_posBestClientRecalc;
	UINT m_uBestClientRecalcGeneration;
	INT_PTR m_iBestClientRecalcProcessed;
	DWORD m_uBestClientRuntimeID;
	DWORD m_uBestLowClientRuntimeID;
	DWORD m_dwBestClientCacheTick;
	uint32 m_uBestClientScore;
	uint32 m_uBestLowClientScore;
	DWORD m_uBestCooldownClientRuntimeID;
	uint32 m_uBestCooldownClientScore;
	bool m_bBestClientRecalcActive;
	bool m_bBestClientCacheValid;
	std::vector<DWORD> m_aPruneWaitersForMissingSharedFilesRuntimeIDs;
	size_t m_uPruneWaitersForMissingSharedFilesIndex;
	bool m_bPruneWaitersForMissingSharedFilesActive;
};
