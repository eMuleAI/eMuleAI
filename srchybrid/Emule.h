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
#ifndef __AFXWIN_H__
#error include 'stdafx.h' before including this file for PCH
#endif
#include "resource.h"
#include "SearchParams.h"
#include "eMuleAI/ConChecker.h"
#include "eMuleAI/DownloadValidator.h"
#include "eMuleAI/Address.h"
#include "eMuleAI/UtpSocket.h"
#include "kademlia/kademlia/Search.h"
#include <vector>
#include <memory>

#define	DEFAULT_NICK		MOD_REPO_BASE_URL
#define	DEFAULT_TCP_PORT_OLD	4662
#define	DEFAULT_UDP_PORT_OLD	(DEFAULT_TCP_PORT_OLD+10)
#define	BULK_OPERATION_MIN_ITEMS	2

#define PORTTESTURL			_T("https://porttest.emule-project.net/connectiontest.php?tcpport=%i&udpport=%i&lang=%i")

class CSearchList;
class CUploadQueue;
class CListenSocket;
class CDownloadQueue;
class CScheduler;
class UploadBandwidthThrottler;
class LastCommonRouteFinder;
class CemuleDlg;
class CClientList;
class CKnownFileList;
class CServerConnect;
class CServerList;
class CSharedFileList;
class CClientCreditsList;
class CFriendList;
class CClientUDPSocket;
class CIPFilter;
class CWebServer;
class CAbstractFile;
class CMuleListCtrl;
class CUpDownClient;
class CPartFile;
struct ImportOperationContext;
struct ImportPart_Struct;
class CUPnPImplWrapper;
class CUploadDiskIOThread;
class CPartFileWriteThread;
class CIPGeolocation;
class CShield;
class CConChecker;
class CDownloadValidator;

struct SLogItem;
struct SCollectionImportResult;

enum AppState
{
	APP_STATE_STARTING = 0,	//initialization phase
	APP_STATE_RUNNING,
	APP_STATE_ASKCLOSE,		//exit confirmation dialog is active
	APP_STATE_SHUTTINGDOWN,
	APP_STATE_DONE			//shutdown completed
};

class CemuleApp : public CWinApp
{
public:
	explicit CemuleApp(LPCTSTR lpszAppName = NULL);
	bool IsRunning() const;
	bool IsClosing() const;
	bool IsNetworkActivityBlockedByBind() const;
	bool IsNetworkSocketCreationBlockedByBind() const;
	void BeginNetworkBindSocketCreation();
	void EndNetworkBindSocketCreation();
	CString GetNetworkActivityBlockMessage() const;
	void RefreshShutdownPartFlushDiskSpaceCache();
	bool CanShutdownFlushPartFile(LPCTSTR pszPath, bool bForceRefresh = false);
	void RefreshPartMetDiskSpaceCache();
	bool CanWritePartMetFiles(LPCTSTR pszPath, bool bForceRefresh = false);

	UploadBandwidthThrottler *uploadBandwidthThrottler;
	LastCommonRouteFinder *lastCommonRouteFinder;
	CemuleDlg			*emuledlg;
	CClientList			*clientlist;
	CKnownFileList		*knownfiles;
	CServerConnect		*serverconnect;
	CServerList			*serverlist;
	CSharedFileList		*sharedfiles;
	CSearchList			*searchlist;
	CListenSocket		*listensocket;
	CUploadQueue		*uploadqueue;
	CDownloadQueue		*downloadqueue;
	CClientCreditsList	*clientcredits;
	CFriendList			*friendlist;
	CClientUDPSocket	*clientudp;
	CIPFilter			*ipfilter;
	CWebServer			*webserver;
	CScheduler			*scheduler;
	CUPnPImplWrapper	*m_pUPnPFinder;
	CUploadDiskIOThread	*m_pUploadDiskIOThread;
	CPartFileWriteThread *m_pPartFileWriteThread;
	CIPGeolocation			*ipgeolocation;
	CConChecker			*ConChecker;
	CDownloadValidator	*DownloadValidator;

	static const UINT	m_nVersionMjr;
	static const UINT	m_nVersionMin;
	static const UINT	m_nVersionUpd;
	static const UINT	m_nVersionBld;
	static const TCHAR	*m_sPlatform;

	CShield*	shield;
	HANDLE		m_hMutexOneInstance;
	int			m_iDfltImageListColorFlags;
	CFont		m_fontHyperText;
	CFont		m_fontDefaultBold;
	CFont		m_fontSymbol;
	CFont		m_fontLog;
	CFont		m_fontChatEdit;
	CBrush		m_brushBackwardDiagonal;
	DWORD		m_dwProductVersionMS;
	DWORD		m_dwProductVersionLS;
	CString		m_strCurVersionLong;
	CString		m_strCurVersionLongDbg;
	CString		GetAppVersion() const;
	UINT		m_uCurVersionShort;
	UINT		m_uCurVersionCheck;
	ULONGLONG	m_ullComCtrlVer;
	CMutex		hashing_mut;
	CString		m_strPendingLink;
	COPYDATASTRUCT sendstruct;
	AppState	m_app_state; // defines application state

// Implementierung
	virtual BOOL InitInstance();
	virtual int	ExitInstance();
	virtual BOOL IsIdleMessage(MSG *pMsg);

	// ed2k link functions
	enum EDownloadCommandType
	{
		DownloadCommandAddFileLinks,
		DownloadCommandProcessLinks,
		DownloadCommandRemoveItems,
		DownloadCommandChangeState
	};

	struct SDownloadFileSnapshot
	{
		SDownloadFileSnapshot();

		CString m_strFileName;
		CString m_strAICHHash;
		uint64 m_uFileSize;
		uchar m_abyFileHash[16];
	};

	struct SDownloadCommand
	{
		SDownloadCommand();

		EDownloadCommandType m_eType;
		CStringArray m_astrLinks;
		std::shared_ptr<std::vector<SDownloadFileSnapshot> > m_pFileSnapshots;
		CString m_strRawLinks;
		CString m_strTokenDelimiters;
		CStringArray m_astrItemHashes;
		CString m_strActionValue;
		UINT m_uAction;
		int m_iActionValue;
		bool m_bAddToCanceledMet;
		bool m_bDeleteCompletedFile;
		int m_iCat;
	};

	enum EUploadCommandType
	{
		UploadCommandClientRowsChanged,
		UploadCommandClientRowsRemoved,
		UploadCommandQueueListChanged,
		UploadCommandUploadListChanged,
		UploadCommandBandwidthSnapshotChanged,
		UploadCommandDiskIoResult
	};

	enum ESearchCommandType
	{
		SearchCommandStart,
		SearchCommandCancel,
		SearchCommandIngestApply,
		SearchCommandKnownTypeRefresh
	};

	enum EUploadClientUiTargetFlags
	{
		UploadClientUiTargetUploadList = 0x01,
		UploadClientUiTargetQueueList = 0x02,
		UploadClientUiTargetDownloadClients = 0x04,
		UploadClientUiTargetAll = UploadClientUiTargetUploadList | UploadClientUiTargetQueueList | UploadClientUiTargetDownloadClients
	};

	struct SUploadCommand
	{
		SUploadCommand();

		EUploadCommandType m_eType;
		DWORD m_uRuntimeID;
		LONG m_lRuntimeGeneration;
		UINT m_uTargetFlags;
		UINT m_uWaitingCount;
		UINT m_uUploadingCount;
		UINT m_uActiveUploadCount;
		UINT m_uDataRate;
		UINT m_uToNetworkDataRate;
		CString m_strStage;
	};

	struct SSearchCommand
	{
		SSearchCommand();

		ESearchCommandType m_eType;
		SSearchParams m_searchParams;
		uint32 m_uSearchID;
		bool m_bStartupRefresh;
	};

	struct SCollectionCommand
	{
		SCollectionCommand();

		CString m_strFilePath;
	};

	enum EPersistenceCommandType
	{
		PersistenceCommandSaveAppState,
		PersistenceCommandSaveStats,
		PersistenceCommandSaveKnownFiles,
		PersistenceCommandSavePreferences,
		PersistenceCommandSaveFriends,
		PersistenceCommandSaveClientCredits,
		PersistenceCommandSaveServerList,
		PersistenceCommandSaveClientHistory,
		PersistenceCommandSaveSearchStore,
		PersistenceCommandSaveSearchSpam,
		PersistenceCommandSaveSharedFiles,
		PersistenceCommandSaveKadNodes
	};

	struct SPersistenceCommand
	{
		SPersistenceCommand();

		EPersistenceCommandType m_eType;
		bool m_bAutoSave;
		bool m_bWorkRequest;
		CString m_strReason;
	};

	enum ESharedFilesCommandType
	{
		SharedFilesCommandMenuAction,
		SharedFilesCommandSelectionAction,
		SharedFilesCommandReload,
		SharedFilesCommandBulkDelete,
		SharedFilesCommandBulkCancelDownloads,
		SharedFilesCommandBulkUnshare,
		SharedFilesCommandBulkHistoryRemove,
		SharedFilesCommandBulkMetadataUpdate,
		SharedFilesCommandBulkPriority,
		SharedFilesCommandCreateCollection,
		SharedFilesCommandToggleShareStatus
	};

	struct SSharedFilesCommand
	{
		SSharedFilesCommand();

		ESharedFilesCommandType m_eType;
		UINT m_uAction;
		CStringArray m_astrItemHashes;
	};

	enum EBackendCommandType
	{
		BackendCommandDownload,
		BackendCommandUpload,
		BackendCommandSearch,
		BackendCommandCollection,
		BackendCommandPersistence,
		BackendCommandSharedFiles,
		BackendCommandNetworkPacket
	};

	enum EBackendCommandFamily
	{
		BackendCommandFamilyUnknown,
		BackendCommandFamilyDownloadAddFileLinks,
		BackendCommandFamilyDownloadProcessLinks,
		BackendCommandFamilyDownloadRemoveItems,
		BackendCommandFamilyDownloadChangeState,
		BackendCommandFamilyDownloadChangeStateOwnerSafe,
		BackendCommandFamilyUploadClientRowsChanged,
		BackendCommandFamilySearchStart,
		BackendCommandFamilySearchCancel,
		BackendCommandFamilySearchIngestApply,
		BackendCommandFamilySearchKnownTypeRefresh,
		BackendCommandFamilyCollectionImport,
		BackendCommandFamilyPersistenceSaveAppState,
		BackendCommandFamilyPersistenceSaveStats,
		BackendCommandFamilyPersistenceSaveKnownFiles,
		BackendCommandFamilyPersistenceSavePreferences,
		BackendCommandFamilyPersistenceSaveFriends,
		BackendCommandFamilyPersistenceSaveClientCredits,
		BackendCommandFamilyPersistenceSaveServerList,
		BackendCommandFamilyPersistenceSaveClientHistory,
		BackendCommandFamilyPersistenceSaveSearchStore,
		BackendCommandFamilyPersistenceSaveSearchSpam,
		BackendCommandFamilyPersistenceSaveSharedFiles,
		BackendCommandFamilyPersistenceSaveKadNodes,
		BackendCommandFamilySharedFilesMenuAction,
		BackendCommandFamilySharedFilesSelectionAction,
		BackendCommandFamilySharedFilesReload,
		BackendCommandFamilySharedFilesBulkDelete,
		BackendCommandFamilySharedFilesBulkCancelDownloads,
		BackendCommandFamilySharedFilesBulkUnshare,
		BackendCommandFamilySharedFilesBulkHistoryRemove,
		BackendCommandFamilySharedFilesBulkMetadataUpdate,
		BackendCommandFamilySharedFilesBulkPriority,
		BackendCommandFamilySharedFilesCreateCollection,
		BackendCommandFamilySharedFilesToggleShareStatus,
		BackendCommandFamilyNetworkClientSearchAnswer,
		BackendCommandFamilyNetworkServerSearchAnswer,
		BackendCommandFamilyNetworkDownloadFileStatus,
		BackendCommandFamilyNetworkDownloadHashSet,
		BackendCommandFamilyNetworkDownloadFoundSources,
		BackendCommandFamilyNetworkDownloadSourceExchange,
		BackendCommandFamilyDownloadBlockRequest,
		BackendCommandFamilyDownloadBlockReceive,
		BackendCommandFamilyDownloadCorruptedBlock,
		BackendCommandFamilyDownloadCompletePart,
		BackendCommandFamilyDownloadFileCompletion,
		BackendCommandFamilyDownloadAichVerification,
		BackendCommandFamilyDownloadPartMetSnapshotWrite,
		BackendCommandFamilyUploadQueueListChanged,
		BackendCommandFamilyUploadListChanged,
		BackendCommandFamilyUploadBandwidthSnapshot,
		BackendCommandFamilyUploadDiskIoResult,
		BackendCommandFamilyNetworkServerUdpSearchAnswer,
		BackendCommandFamilyNetworkKadPacket
	};

	// Executor contract:
	// UI owns controls and update event consumption only.
	// Network parser owns bytes-to-DTO parsing only.
	// Backend owner owns domain mutation only.
	// Disk I/O owns blocking disk work only.
	// Queue payloads must not carry backend-owned raw pointers.
	enum EBackendExecutorDomain
	{
		BackendExecutorUi,
		BackendExecutorNetworkParser,
		BackendExecutorBackendOwner,
		BackendExecutorDiskIo
	};

	enum EWorkerTopologyRole
	{
		WorkerTopologyBackendCommand,
		WorkerTopologyNetworkParseCpu,
		WorkerTopologyNetworkUtility,
		WorkerTopologyPartFileDiskIo,
		WorkerTopologyUploadDiskIo,
		WorkerTopologyPersistence,
		WorkerTopologyStartupLoadPrimary,
		WorkerTopologyStartupLoadSecondary,
		WorkerTopologyStartupLoadSearches,
		WorkerTopologyStartupLoadDownloads,
		WorkerTopologyStartupLoadKnown2,
		WorkerTopologyRoleCount
	};

	enum EWorkerTopologyState
	{
		WorkerTopologyStopped,
		WorkerTopologyStarting,
		WorkerTopologyRunning,
		WorkerTopologyDraining,
		WorkerTopologyStopping,
		WorkerTopologyQuarantined
	};

	enum EWorkerTopologyItemType
	{
		WorkerTopologyItemNone,
		WorkerTopologyItemStart,
		WorkerTopologyItemStop,
		WorkerTopologyItemDrain,
		WorkerTopologyItemCancel,
		WorkerTopologyItemPersistenceSave,
		WorkerTopologyItemNetworkUtility,
		WorkerTopologyItemNetworkParseCpu,
		WorkerTopologyItemStartupMetadataLoad,
		WorkerTopologyItemFileSystemReload
	};

	enum EStartupMetadataDomain
	{
		StartupMetadataDownloads,
		StartupMetadataKnownFiles,
		StartupMetadataClientHistory,
		StartupMetadataKnown2Index,
		StartupMetadataStoredSearches,
		StartupMetadataSharedRules,
		StartupMetadataDomainCount
	};

	enum EStartupMetadataState
	{
		StartupMetadataStateNotStarted,
		StartupMetadataStateLoading,
		StartupMetadataStateApplying,
		StartupMetadataStateReady,
		StartupMetadataStateSkipped,
		StartupMetadataStateFailed,
		StartupMetadataStateCancelled
	};

	struct SStartupMetadataLoadState
	{
		SStartupMetadataLoadState();

		bool IsReady() const { return m_eState == StartupMetadataStateReady || m_eState == StartupMetadataStateSkipped; }
		bool IsTerminal() const { return m_eState == StartupMetadataStateReady || m_eState == StartupMetadataStateSkipped || m_eState == StartupMetadataStateFailed || m_eState == StartupMetadataStateCancelled; }

		EStartupMetadataState m_eState;
		LONG m_lGeneration;
		uint64 m_uCancellationToken;
		DWORD m_dwLastError;
		bool m_bCancelRequested;
		UINT m_uProgressDone;
		UINT m_uProgressTotal;
		CString m_strProgressStage;
		CString m_strReason;
	};


	struct SWorkerTopologyItem
	{
		SWorkerTopologyItem();

		EWorkerTopologyRole m_eRole;
		EWorkerTopologyItemType m_eType;
		DWORD m_dwCreatedTick;
		DWORD m_dwDueTick;
		uint64 m_uSequence;
		uint64 m_uCorrelationId;
		uint64 m_uCancellationToken;
		LONG m_lWorkerGeneration;
		UINT m_uFlags;
		int m_iPayloadCursor;
		HWND m_hNotifyWnd;
		CString m_strCoalesceKey;
		CString m_strStage;
		CString m_strPayload;
		std::vector<BYTE> m_vecPayload;
	};



	enum EBackendCommandApplyMode
	{
		BackendCommandApplyUiCompatibilityOnly,
		BackendCommandApplyBackendOwnerSafe
	};

	enum EBackendCommandReadiness
	{
		BackendCommandReadinessBlocked,
		BackendCommandReadinessUiCompatibilityOnly,
		BackendCommandReadinessBackendOwnerReady
	};

	enum EBackendLifecycleState
	{
		BackendLifecycleStarting,
		BackendLifecycleRunning,
		BackendLifecycleStoppingInput,
		BackendLifecycleDrainingParser,
		BackendLifecycleDrainingBackendOwner,
		BackendLifecycleDrainingDiskIo,
		BackendLifecycleStoppingNetwork,
		BackendLifecycleStoppingUiUpdates,
		BackendLifecycleStopped
	};

	enum EBackendCommandSource
	{
		BackendCommandSourceUnknown,
		BackendCommandSourceUi,
		BackendCommandSourceNetworkClient,
		BackendCommandSourceNetworkServer,
		BackendCommandSourceNetworkUdp,
		BackendCommandSourceNetworkKad,
		BackendCommandSourceWebServer,
		BackendCommandSourceTimer,
		BackendCommandSourcePersistence,
		BackendCommandSourceSharedFilesOwner,
		BackendCommandSourceDiskIo,
		BackendCommandSourceShutdown,
		BackendCommandSourceSearchIngest,
		BackendCommandSourceDownloadModel,
		BackendCommandSourceUploadModel
	};

	enum EBackendCommandOrderingScope
	{
		BackendCommandOrderingGlobal,
		BackendCommandOrderingClient,
		BackendCommandOrderingSearch,
		BackendCommandOrderingFileHash,
		BackendCommandOrderingDownloadList,
		BackendCommandOrderingSharedFiles,
		BackendCommandOrderingPersistence,
		BackendCommandOrderingUploadList,
		BackendCommandOrderingServer,
		BackendCommandOrderingKad,
		BackendCommandOrderingWebRequest,
		BackendCommandOrderingDiskIo
	};

	enum EBackendCommandFailureKind
	{
		BackendCommandFailureNone,
		BackendCommandFailureInvalidPayload,
		BackendCommandFailureStaleTarget,
		BackendCommandFailureOwnerGuard,
		BackendCommandFailureShutdown,
		BackendCommandFailureDispatcherUnavailable,
		BackendCommandFailureApplyFailed,
		BackendCommandFailureException
	};

	enum EBackendCommandFailurePolicy
	{
		BackendCommandFailurePolicyUnknown,
		BackendCommandFailurePolicyReport,
		BackendCommandFailurePolicyDropStale,
		BackendCommandFailurePolicyReportAndDropStale
	};

	struct SBackendCommandContract
	{
		EBackendCommandFamily m_eFamily;
		EBackendCommandType m_eType;
		EBackendExecutorDomain m_eExecutorDomain;
		EBackendCommandApplyMode m_eApplyMode;
		EBackendCommandSource m_eDefaultSource;
		EBackendCommandOrderingScope m_eDefaultOrderingScope;
		EBackendCommandFailurePolicy m_eFailurePolicy;
		DWORD m_dwAllowedSourceMask;
		DWORD m_dwAllowedScopeMask;
		bool m_bCancelable;
		bool m_bDropIfStale;
		LPCTSTR m_pszName;
	};

	struct SBackendCommandReadiness
	{
		EBackendCommandFamily m_eFamily;
		EBackendExecutorDomain m_eExecutorDomain;
		EBackendCommandApplyMode m_eApplyMode;
		EBackendCommandReadiness m_eReadiness;
		bool m_bEnableBackendOwnerDispatch;
		LPCTSTR m_pszReason;
	};

	enum ENetworkPacketCommandType
	{
		NetworkPacketCommandClientSearchAnswer,
		NetworkPacketCommandServerSearchAnswer,
		NetworkPacketCommandDownloadFileStatus,
		NetworkPacketCommandDownloadHashSet,
		NetworkPacketCommandDownloadFoundSources,
		NetworkPacketCommandDownloadSourceExchange,
		NetworkPacketCommandDownloadBlockReceive,
		NetworkPacketCommandServerUdpSearchAnswer,
		NetworkPacketCommandKadPacket
	};

	enum ENetworkParseDomain
	{
		NetworkParseSearchAnswer,
		NetworkParseClientTcp,
		NetworkParseClientUdp,
		NetworkParseServerTcp,
		NetworkParseServerUdp,
		NetworkParseKad,
		NetworkParseWebServer,
		NetworkParseHttp,
		NetworkParseCompressedBlock
	};

	struct SNetworkPacketCommand
	{
		SNetworkPacketCommand();

		ENetworkPacketCommandType m_eType;
		ENetworkParseDomain m_eParseDomain;
		std::vector<BYTE> m_packet;
		DWORD m_uClientRuntimeID;
		LONG m_lClientRuntimeGeneration;
		BYTE m_abyClientUserHash[16];
		uint32 m_nClientIP;
		uint16 m_nClientUserPort;
		uint32 m_uSearchID;
		LONG m_lSearchGeneration;
		CString m_strClientHash;
		CString m_strSenderName;
		CString m_strDirectory;
		uint32 m_nClientID;
		uint16 m_nClientPort;
		uint32 m_nClientServerIP;
		uint16 m_nClientServerPort;
		uint8 m_uProtocol;
		uint8 m_uOpcode;
		uint32 m_uTransactionID;
		uint32 m_uContactID;
		LONG m_lSessionGeneration;
		bool m_bValidReceiverKey;
		uint32 m_uSenderUDPKey;
		bool m_bOptUTF8;
		bool m_bClientResponse;
		bool m_bPreviewSupport;
		bool m_bSupportsLargeFiles;
		bool m_bDoSpamRating;
		bool m_bUseKadReloadThrottle;
		bool m_bUdpPacket;
		bool m_bFileIdentifiers;
		bool m_bSourceExchange2;
		bool m_bWithObfuscationAndHash;
		bool m_bCompressedBlock;
		bool m_bI64Offsets;
		uint8 m_uSourceExchangeVersion;
		ULONGLONG m_uPacketPosition;
		DWORD m_uFileRuntimeID;
		BYTE m_abyFileHash[16];
	};

	struct SBackendCommand
	{
		SBackendCommand();

		EBackendCommandType m_eType;
		EBackendCommandFamily m_eFamily;
		EBackendCommandSource m_eSource;
		EBackendCommandOrderingScope m_eOrderingScope;
		EBackendCommandFailurePolicy m_eFailurePolicy;
		CString m_strOrderingKey;
		DWORD m_dwCreatedTick;
		LONG m_lGenerationGuard;
		bool m_bCancelable;
		bool m_bDropIfStale;
		uint64 m_uSequence;
		uint64 m_uCorrelationId;
		uint64 m_uCancellationToken;
		SDownloadCommand m_downloadCommand;
		SUploadCommand m_uploadCommand;
		SSearchCommand m_searchCommand;
		SCollectionCommand m_collectionCommand;
		SPersistenceCommand m_persistenceCommand;
		SSharedFilesCommand m_sharedFilesCommand;
		SNetworkPacketCommand m_networkPacketCommand;
	};

	enum EApplicationEventType
	{
		ApplicationEventDownloadBatchProgress,
		ApplicationEventDownloadBatchCompleted,
		ApplicationEventUploadClientRowsChanged,
		ApplicationEventUploadClientRowsRemoved,
		ApplicationEventCommandFailed,
		ApplicationEventSearchStartRequested,
		ApplicationEventSearchCancelRequested,
		ApplicationEventCollectionImportRequested,
		ApplicationEventDownloadStateProgress,
		ApplicationEventDownloadStateCompleted,
		ApplicationEventDownloadRemoveProgress,
		ApplicationEventDownloadRemoveDiskCleanupCompleted,
		ApplicationEventDownloadRemoveCompleted,
		ApplicationEventDownloadRemoveItemFailed,
		ApplicationEventDownloadStateItemFailed,
		ApplicationEventDownloadProcessLinkRequested,
		ApplicationEventDownloadRemoveRequested,
		ApplicationEventDownloadStateRequested,
		ApplicationEventDownloadListRowsRemoved,
		ApplicationEventDownloadListDeletedCompletedRowsRemoved,
		ApplicationEventDownloadListChanged,
		ApplicationEventBulkOperationOverlayRefresh,
		ApplicationEventSearchResultsChanged,
		ApplicationEventSearchPacketParseProgress,
		ApplicationEventSearchPacketParseCompleted,
		ApplicationEventSearchPacketParseFailed,
		ApplicationEventLocalEd2kSearchEnd,
		ApplicationEventCollectionImportFailed,
		ApplicationEventAsyncDiskWriteResult,
		ApplicationEventPersistenceRequested,
		ApplicationEventPersistenceProgress,
		ApplicationEventPersistenceCompleted,
		ApplicationEventPersistenceFailed,
		ApplicationEventPersistenceWorkRequested,
		ApplicationEventSharedFilesCommandRequested,
		ApplicationEventSharedFilesCommandProgress,
		ApplicationEventSharedFilesCommandCompleted,
		ApplicationEventSharedFilesCommandFailed,
		ApplicationEventSharedFilesCommandItemFailed,
		ApplicationEventSharedFilesListChanged,
		ApplicationEventPartFileOwnerStateChanged,
		ApplicationEventPartFileDiskWriteRequested,
		ApplicationEventPartFileOwnerFailed,
		ApplicationEventClientRowUpdateRequested,
		ApplicationEventUploadQueueListChanged,
		ApplicationEventUploadListChanged,
		ApplicationEventUploadBandwidthSnapshotChanged,
		ApplicationEventUploadDiskIoResult,
		ApplicationEventClientChatMessage,
		ApplicationEventClientChatCloseRequested,
		ApplicationEventClientCaptchaRequested,
		ApplicationEventClientCaptchaResult,
		ApplicationEventClientChatConnectingResult,
		ApplicationEventClientChatConnectionProgress,
		ApplicationEventKadConnectionStateChanged,
		ApplicationEventKadUiStatusRefresh,
		ApplicationEventKadSearchCancelUiRequested,
		ApplicationEventStartupMetadataStateChanged
	};

	enum EKadUiStatusFlags
	{
		KadUiStatusUpnp = 0x0001,
		KadUiStatusUserCount = 0x0002,
		KadUiStatusContactList = 0x0004
	};

	enum EApplicationEventDispatchDomain
	{
		ApplicationEventDispatchTelemetry,
		ApplicationEventDispatchUiNotification,
		ApplicationEventDispatchUiCommandBridge,
		ApplicationEventDispatchBackendResult
	};

	struct SApplicationEvent
	{
		SApplicationEvent();

		EApplicationEventType m_eType;
		EBackendCommandType m_eBackendCommandType;
		EBackendCommandFamily m_eBackendCommandFamily;
		EBackendCommandSource m_eBackendCommandSource;
		EBackendCommandOrderingScope m_eBackendCommandOrderingScope;
		EBackendCommandFailureKind m_eBackendCommandFailureKind;
		EBackendCommandFailurePolicy m_eBackendCommandFailurePolicy;
		LONG m_lBackendCommandGenerationGuard;
		CString m_strBackendCommandOrderingKey;
		EDownloadCommandType m_eDownloadCommandType;
		EUploadCommandType m_eUploadCommandType;
		ESearchCommandType m_eSearchCommandType;
		EPersistenceCommandType m_ePersistenceCommandType;
		ESharedFilesCommandType m_eSharedFilesCommandType;
		EStartupMetadataDomain m_eStartupMetadataDomain;
		EStartupMetadataState m_eStartupMetadataState;
		LONG m_lStartupMetadataGeneration;
		uint64 m_uSequence;
		uint64 m_uCorrelationId;
		uint64 m_uCancellationToken;
		DWORD m_uClientRuntimeID;
		LONG m_lClientRuntimeGeneration;
		UINT m_uUploadTargetFlags;
		UINT m_uUploadWaitingCount;
		UINT m_uUploadUploadingCount;
		UINT m_uUploadActiveCount;
		UINT m_uUploadDataRate;
		UINT m_uUploadToNetworkDataRate;
		UINT m_uAction;
		int m_iActionValue;
		bool m_bAddToCanceledMet;
		bool m_bDeleteCompletedFile;
		bool m_bAutoSave;
		std::vector<CString> m_vecItemHashes;
		UINT m_uProcessed;
		UINT m_uFailed;
		UINT m_uStale;
		UINT m_uTotal;
		DWORD m_dwLastError;
		LONG m_lAsyncGeneration;
		bool m_bAsyncShutdownFallback;
		uint32 m_uSearchID;
		LONG m_lSearchGeneration;
		SSearchParams m_searchParams;
		CString m_strFilePath;
		CString m_strFileHash;
		CString m_strMessage;
		CString m_strAsyncName;
		CString m_strAsyncResult;
		CString m_strAsyncReason;
		CString m_strAsyncTempPath;
		bool m_bUseKadReloadThrottle;
		bool m_bMoreResultsAvailable;
		HBITMAP m_hClientBitmap;
	};

	bool		ExecuteDownloadCommand(const SDownloadCommand &command, EBackendCommandSource eSource = BackendCommandSourceUi, EBackendCommandOrderingScope eScope = BackendCommandOrderingDownloadList, LPCTSTR pszOrderingKey = NULL);
	void		ExecuteDownloadListRemoveCommand(const CStringArray &astrItemHashes, bool bAddToCanceledMet, bool bDeleteCompletedFile, EBackendCommandSource eSource = BackendCommandSourceUi, EBackendCommandOrderingScope eScope = BackendCommandOrderingDownloadList, LPCTSTR pszOrderingKey = NULL, bool bPreferUiChunkedRemove = true);
	void		ExecuteDownloadListRemoveHashCommand(LPCTSTR pszItemHash, bool bAddToCanceledMet, bool bDeleteCompletedFile, EBackendCommandSource eSource = BackendCommandSourceUi, EBackendCommandOrderingScope eScope = BackendCommandOrderingDownloadList, LPCTSTR pszOrderingKey = NULL);
	void		ExecuteDownloadListStateCommand(const CStringArray &astrItemHashes, UINT uAction, int iActionValue, EBackendCommandSource eSource = BackendCommandSourceUi, EBackendCommandOrderingScope eScope = BackendCommandOrderingDownloadList, LPCTSTR pszOrderingKey = NULL);
	void		ExecuteDownloadListStateTextCommand(const CStringArray &astrItemHashes, UINT uAction, int iActionValue, LPCTSTR pszActionValue, EBackendCommandSource eSource = BackendCommandSourceUi, EBackendCommandOrderingScope eScope = BackendCommandOrderingDownloadList, LPCTSTR pszOrderingKey = NULL);
	void		ExecuteDownloadListStateHashCommand(LPCTSTR pszItemHash, UINT uAction, int iActionValue, LPCTSTR pszActionValue = NULL, EBackendCommandSource eSource = BackendCommandSourceUi, EBackendCommandOrderingScope eScope = BackendCommandOrderingDownloadList, LPCTSTR pszOrderingKey = NULL);
	void		ExecuteWebServerDownloadActionCommand(LPCTSTR pszItemHash, LPCTSTR pszAction, int iActionValue = 0, LPCTSTR pszActionValue = NULL);
	void		ExecuteWebServerClearCompletedCommand(LPCTSTR pszItemHash, int iCategory);
	void		ExecuteWebServerCategoryPriorityCommand(int iCategory, uint8 uPriority);
	void		ExecuteSearchStartCommand(SSearchParams *pParams, EBackendCommandSource eSource = BackendCommandSourceUi, EBackendCommandOrderingScope eScope = BackendCommandOrderingSearch, LPCTSTR pszOrderingKey = NULL);
	void		ExecuteSearchCancelCommand(uint32 uSearchID, EBackendCommandSource eSource = BackendCommandSourceUi, EBackendCommandOrderingScope eScope = BackendCommandOrderingSearch, LPCTSTR pszOrderingKey = NULL);
	bool		ExecuteSearchKnownTypeRefreshCommand(LPCTSTR pszReason = NULL, bool bStartupRefresh = false);
	bool		WakeSearchKnownTypeRefreshWork();
	void		ExecuteCollectionImportCommand(const CString &strFilePath);
	void		ExecuteSaveAppStateCommand(bool bAutoSave, LPCTSTR pszReason = NULL);
	void		ExecuteSaveStatsCommand(LPCTSTR pszReason = NULL);
	void		ExecuteSavePersistenceFileCommand(EPersistenceCommandType eCommand, LPCTSTR pszReason = NULL);
	void		ExecuteSharedFilesCommand(UINT uAction, const CStringArray &astrItemHashes, EBackendCommandSource eSource = BackendCommandSourceUi, EBackendCommandOrderingScope eScope = BackendCommandOrderingSharedFiles, LPCTSTR pszOrderingKey = NULL);
	bool		QueueClientSearchAnswerNetworkCommand(const BYTE *pPacket, uint32 nSize, CUpDownClient &sender, LPCTSTR pszDirectory = NULL);
	bool		QueueServerSearchAnswerNetworkCommand(const BYTE *pPacket, uint32 nSize, bool bOptUTF8, uint32 nServerIP, uint16 nServerPort);
	bool		QueueServerUdpSearchAnswerNetworkCommand(const BYTE *pPacket, uint32 nSize, bool bOptUTF8, uint32 nServerIP, uint16 nServerPort);
	bool		QueueKadPacketNetworkCommand(const BYTE *pPacket, uint32 nSize, uint32 nIP, uint16 nUDPPort, bool bValidReceiverKey, uint32 nSenderUDPKey);
	bool		QueueDownloadFileStatusNetworkCommand(CUpDownClient *pClient, CPartFile *pFile, const BYTE *pPacket, uint32 nSize, ULONGLONG uPacketPosition, bool bUdpPacket);
	bool		QueueDownloadHashSetNetworkCommand(CUpDownClient *pClient, const BYTE *pPacket, uint32 nSize, bool bFileIdentifiers);
	bool		QueueDownloadFoundSourcesNetworkCommand(CPartFile *pFile, const BYTE *pPacket, uint32 nSize, ULONGLONG uPacketPosition, uint32 nServerIP, uint16 nServerPort, bool bWithObfuscationAndHash);
	bool		QueueDownloadSourceExchangeNetworkCommand(CUpDownClient *pClient, CPartFile *pFile, const BYTE *pPacket, uint32 nSize, ULONGLONG uPacketPosition, uint8 uSourceExchangeVersion, bool bSourceExchange2);
	bool		QueueDownloadBlockReceiveNetworkCommand(CUpDownClient *pClient, CPartFile *pFile, const BYTE *pPacket, uint32 nSize, bool bCompressedBlock, bool bI64Offsets);
	void		QueueDownloadListCommandEvent(EApplicationEventType eType, UINT uAction, UINT uProcessed, UINT uFailed, UINT uStale, UINT uTotal, uint64 uSequence = 0, uint64 uCorrelationId = 0,
				EBackendCommandSource eSource = BackendCommandSourceUi, EBackendCommandOrderingScope eScope = BackendCommandOrderingDownloadList, LPCTSTR pszOrderingKey = NULL, uint64 uCancellationToken = 0);
	void		QueueDownloadListCommandFailureEvent(EApplicationEventType eType, UINT uAction, LPCTSTR pszStage, LPCTSTR pszFilePath, DWORD dwLastError, uint64 uSequence = 0, uint64 uCorrelationId = 0,
				EBackendCommandSource eSource = BackendCommandSourceUi, EBackendCommandOrderingScope eScope = BackendCommandOrderingDownloadList, LPCTSTR pszOrderingKey = NULL, uint64 uCancellationToken = 0);
	void		QueueCollectionImportFailureEvent(LPCTSTR pszFilePath, LPCTSTR pszStage, DWORD dwLastError);
	void		QueuePersistenceCommandEvent(EApplicationEventType eType, EPersistenceCommandType eCommand, bool bAutoSave, LPCTSTR pszStage, DWORD dwLastError, uint64 uSequence = 0, uint64 uCorrelationId = 0);
	bool		QueuePersistenceWorkRequest(LPCTSTR pszReason = NULL);
	bool		QueuePersistenceWorkRequest(EPersistenceCommandType eCommand, LPCTSTR pszReason);
	LONG		BeginStartupMetadataLoad(EStartupMetadataDomain eDomain, uint64 *puCancellationToken, LPCTSTR pszReason = NULL);
	void		SetStartupMetadataStateApplying(EStartupMetadataDomain eDomain, LONG lGeneration, uint64 uCancellationToken, LPCTSTR pszReason = NULL);
	void		CompleteStartupMetadataLoad(EStartupMetadataDomain eDomain, LONG lGeneration, uint64 uCancellationToken, bool bSuccess, DWORD dwLastError, LPCTSTR pszReason = NULL);
	void		PublishStartupMetadataLoadProgress(EStartupMetadataDomain eDomain, LONG lGeneration, uint64 uCancellationToken, LPCTSTR pszStage, UINT uDone, UINT uTotal);
	void		SkipStartupMetadataLoad(EStartupMetadataDomain eDomain, LPCTSTR pszReason = NULL);
	void		CancelStartupMetadataLoads(LPCTSTR pszReason = NULL);
	void		CancelStartupCriticalLoads(LPCTSTR pszReason = NULL);
	bool		IsStartupMetadataLoadCancelled(EStartupMetadataDomain eDomain, LONG lGeneration, uint64 uCancellationToken) const;
	bool		IsStartupMetadataDomainReady(EStartupMetadataDomain eDomain) const;
	bool		KnownFilesReady() const;
	bool		ClientHistoryReady() const;
	bool		Known2IndexReady() const;
	bool		SharedFilesReady() const;
	bool		StartupCriticalMetadataReady() const;
	bool		StartupCriticalMetadataLoadsTerminal() const;
	bool		AllStartupMetadataReady() const;
	bool		BeginStartupCriticalLoads();
	bool		BeginStartupDownloadsLoad();
	bool		BeginStartupKnownFilesLoad();
	bool		BeginStartupClientHistoryLoad();
	bool		BeginStartupStoredSearchesLoad();
	bool		BeginStartupKnown2IndexLoad();
	bool		BeginStartupSharedCacheLoad();
	bool		ProcessStartupDownloadsLoadWorkerItem(const SWorkerTopologyItem &item);
	bool		ProcessStartupKnownFilesLoadWorkerItem(const SWorkerTopologyItem &item);
	bool		ProcessStartupClientHistoryLoadWorkerItem(const SWorkerTopologyItem &item);
	bool		ProcessStartupStoredSearchesLoadWorkerItem(const SWorkerTopologyItem &item);
	bool		ProcessStartupKnown2IndexLoadWorkerItem(const SWorkerTopologyItem &item);
	bool		ProcessStartupSharedCacheLoadWorkerItem(const SWorkerTopologyItem &item);
	SStartupMetadataLoadState GetStartupMetadataLoadState(EStartupMetadataDomain eDomain) const;
	LPCTSTR		GetStartupMetadataDomainName(EStartupMetadataDomain eDomain) const;
	LPCTSTR		GetStartupMetadataStateName(EStartupMetadataState eState) const;
	void		QueueSharedFilesCommandEvent(EApplicationEventType eType, UINT uAction, const std::vector<CString> &vecItemHashes, LPCTSTR pszStage, DWORD dwLastError, uint64 uSequence = 0, uint64 uCorrelationId = 0);
	void		QueueSharedFilesListChangedEvent(LPCTSTR pszStage, uint64 uSequence = 0, uint64 uCorrelationId = 0);
	void		QueueDownloadListChangedEvent(LPCTSTR pszStage, EBackendCommandSource eSource = BackendCommandSourceDownloadModel);
	void		QueueBulkOperationOverlayRefreshEvent(LPCTSTR pszStage = NULL);
	void		QueueDownloadListRowsRemovedEvent(const std::vector<CString>& vecFileHashes, uint64 uSequence = 0, uint64 uCorrelationId = 0);
	void		QueueDownloadListDeletedCompletedRowsRemovedEvent(const std::vector<CString>& vecFileHashes);
	bool		QueueImportPartWrite(ImportPart_Struct *pImportPart);
	bool		QueueImportPartProgress(ImportOperationContext *pContext, WPARAM uProgress);
	bool		QueueImportPartFinished(ImportOperationContext *pContext, bool bAborted);
	void		QueueSharedFilesCommandStatusEvent(EApplicationEventType eType, UINT uAction, UINT uProcessed, UINT uFailed, UINT uStale, UINT uTotal, uint64 uSequence = 0, uint64 uCorrelationId = 0);
	void		QueueSharedFilesCommandFailureEvent(UINT uAction, LPCTSTR pszStage, LPCTSTR pszFilePath, DWORD dwLastError, uint64 uSequence = 0, uint64 uCorrelationId = 0);
	void		QueueAsyncDiskWriteResultEvent(LPCTSTR pszName, LONG lGeneration, LPCTSTR pszResult, LPCTSTR pszReason, LPCTSTR pszTempPath, LPCTSTR pszFinalPath, bool bShutdownFallback, DWORD dwLastError = 0);
	void		QueuePartFileOwnerStateEvent(EApplicationEventType eType, LPCTSTR pszFileHash, DWORD uFileRuntimeID, LPCTSTR pszStage, DWORD dwLastError = 0);
	void		QueueClientRowUpdateEvent(DWORD uClientRuntimeID, LONG lRuntimeGeneration, LPCTSTR pszStage);
	void		QueueKadConnectionStateChangedEvent(LPCTSTR pszStage = NULL);
	void		QueueKadUiStatusRefreshEvent(UINT uStatusFlags, LPCTSTR pszStage = NULL);
	bool		QueueKadSearchCancelUiEvent(uint32 uSearchID, LPCTSTR pszStage = NULL);
	void		QueueClientChatMessageEvent(CUpDownClient *pClient, LPCTSTR pszMessage);
	void		QueueClientChatCloseEvent(CUpDownClient *pClient, LPCTSTR pszStage);
	void		QueueClientCaptchaRequestEvent(CUpDownClient *pClient, HBITMAP hCaptcha);
	void		QueueClientCaptchaResultEvent(CUpDownClient *pClient, LPCTSTR pszResult);
	void		QueueClientChatConnectingResultEvent(CUpDownClient *pClient, bool bSuccess);
	void		QueueClientChatConnectionProgressEvent(CUpDownClient *pClient, LPCTSTR pszProgressDesc, bool bNoTimeStamp);
	void		QueueSearchPacketParseEvent(EApplicationEventType eType, uint32 nSearchID, UINT uProcessed, UINT uFailed, UINT uTotal, LPCTSTR pszStage);
	void		QueueNetworkParserFailureEvent(ENetworkParseDomain eDomain, LPCTSTR pszStage, DWORD dwLastError);
	void		QueueLocalEd2kSearchEndEvent(uint32 nSearchID, UINT uCount, bool bMoreResultsAvailable);
	void		QueueSearchActivityChangedEvent(uint32 nSearchID);
	bool		QueueSearchIngestProcessing();
	void		QueueSearchResultsChangedEvent(uint32 nSearchID, const CString &strClientHash, bool bUseKadReloadThrottle);
	void		QueueUploadClientRowsChanged(const CUpDownClient* pClient, UINT uTargetFlags);
	void		QueueUploadClientUiRemove(const CUpDownClient* pClient, UINT uTargetFlags, LPCTSTR pszStage);
	void		QueueUploadListChangedEvent(UINT uTargetFlags, LPCTSTR pszStage, EBackendCommandSource eSource = BackendCommandSourceUploadModel);
	void		QueueUploadBandwidthSnapshotEvent(LPCTSTR pszStage);
	void		QueueUploadDiskIoResultEvent(const CUpDownClient* pClient, LPCTSTR pszStage, DWORD dwLastError);
	bool		EnqueueBackendCommand(const SBackendCommand &command);
	void		ProcessBackendCommands();
	bool		QueueBackendContinuationProcessing();
	void		StopStartupLoadWorkersIfIdle(LPCTSTR pszReason = NULL);
	bool		GetActiveBackendDownloadListOperationProgress(bool &bRemove, UINT &uDone, UINT &uTotal);
	bool		GetActiveDownloadAddOperationProgress(UINT &uDone, UINT &uTotal, bool *pbSavingToDisk = NULL) const;
	void		SetActiveDownloadAddDiskProgress(UINT uDone, UINT uTotal, bool bActive);
	void		CancelBackendDownloadListOperations();
	void		CancelBackendDownloadAddOperations();
	void		ProcessApplicationEventsFromUiThread();
	void		AddEd2kLinksToDownload(const CString &strLinks, int cat);
	void		AddEd2kLinkArrayToDownload(const CStringArray &astrLinks, int cat);
	void		AddFileSnapshotsToDownload(const std::shared_ptr<std::vector<SDownloadFileSnapshot> > &pFileSnapshots, int cat);
	void		ProcessED2KLinksChunked(const CString &strLinks);
	void		ProcessED2KLinkArrayChunked(const CStringArray &astrLinks);
	void		ProcessChunkedDownloadParseJobs();
	void		ProcessChunkedDownloadJobs(bool *pbYieldRequested = NULL);

	enum ETimeBudgetedSliceKind
	{
		TimeBudgetBackendCommandDispatch,
		TimeBudgetApplicationEventDispatch,
		TimeBudgetDownloadParse,
		TimeBudgetDownloadAdd,
		TimeBudgetSearchResultDownload,
		TimeBudgetSearchIngest,
		TimeBudgetSearchResultRemove,
		TimeBudgetSearchRedraw,
		TimeBudgetUiNotificationApply,
		TimeBudgetStartupApply,
		TimeBudgetDownloadRemove,
		TimeBudgetDownloadState,
		TimeBudgetPersistenceSave,
		TimeBudgetSharedFilesReload,
		TimeBudgetSharedFilesFound,
		TimeBudgetSharedFilesBulk,
		TimeBudgetUploadTimerMaintenance
	};

	DWORD		GetTimeBudgetedSliceBudgetMs(ETimeBudgetedSliceKind eKind) const;
	DWORD		GetTimeBudgetedSliceHardBudgetMs(ETimeBudgetedSliceKind eKind) const;
	DWORD		GetTimeBudgetedProgressTraceMs(ETimeBudgetedSliceKind eKind) const;
	bool		IsTimeBudgetExceeded(DWORD dwSliceStartTick, ETimeBudgetedSliceKind eKind) const;
	bool		IsTimeBudgetHardExceeded(DWORD dwSliceStartTick, ETimeBudgetedSliceKind eKind, DWORD *pdwElapsed = NULL) const;
	void		TraceTimeBudgetSlice(ETimeBudgetedSliceKind eKind, LPCTSTR pszContext, DWORD dwElapsed, UINT uProcessed, INT_PTR iRemaining) const;

	enum EModelMutationDomain
	{
		ModelMutationDownloadQueue,
		ModelMutationSearchList,
		ModelMutationUploadQueue,
		ModelMutationClientList,
		ModelMutationPartFile,
		ModelMutationSearchFile,
		ModelMutationUpDownClient,
		ModelMutationSharedFiles,
		ModelMutationKnownFiles,
		ModelMutationPreferences,
		ModelMutationFriendList,
		ModelMutationServerList,
		ModelMutationKad,
		ModelMutationWebServer
	};

	void		SetBackendOwnerThreadId(DWORD dwThreadId);
	DWORD		GetBackendOwnerThreadId() const						{ return m_dwBackendOwnerThreadId; }
	void		SetNetworkParserOwnerThreadId(DWORD dwThreadId);
	DWORD		GetNetworkParserOwnerThreadId() const					{ return m_dwNetworkParserOwnerThreadId; }
	bool		IsUiThread() const;
	bool		IsBackendOwnerThread() const;
	bool		IsNetworkParserThread() const;
	bool		IsPersistenceWorkerThread() const;
	bool		IsModelMutationAllowed(EModelMutationDomain eDomain) const;
	bool		IsNetworkParseAllowed(ENetworkParseDomain eDomain) const;
	bool		GuardModelMutation(EModelMutationDomain eDomain, LPCTSTR pszEntryPoint);
	bool		GuardNetworkParse(ENetworkParseDomain eDomain, LPCTSTR pszEntryPoint);
	bool		IsBackendCommandApplyThreadAllowed() const;
	bool		IsBackendCommandEligibleForBackendOwnerThread(const SBackendCommand &command) const;
	bool		UseBackendCommandDispatcher() const;
	bool		UseAsyncBackendCommandExecution() const;
	bool		UseAsyncBackendCommandExecution(const SBackendCommand &command) const;
	bool		CanStartAsyncBackendOwnerExecutor() const;
	LPCTSTR		GetBackendCommandSourceName(EBackendCommandSource eSource) const;
	LPCTSTR		GetBackendCommandOrderingScopeName(EBackendCommandOrderingScope eScope) const;
	LPCTSTR		GetBackendCommandFailureName(EBackendCommandFailureKind eFailure) const;
	LPCTSTR		GetBackendCommandFailurePolicyName(EBackendCommandFailurePolicy ePolicy) const;
	LPCTSTR		GetBackendCommandFamilyName(EBackendCommandFamily eFamily) const;
	LPCTSTR		GetBackendExecutorDomainName(EBackendExecutorDomain eExecutor) const;
	LPCTSTR		GetBackendCommandApplyModeName(EBackendCommandApplyMode eMode) const;
	LPCTSTR		GetBackendCommandReadinessName(EBackendCommandReadiness eReadiness) const;
	bool		IsBackendCommandFamilyReadyForBackendOwnerThread(EBackendCommandFamily eFamily) const;
	LPCTSTR		GetBackendCommandReadinessReason(EBackendCommandFamily eFamily) const;
	EBackendCommandFamily GetBackendCommandFamily(const SBackendCommand &command) const;
	const SBackendCommandContract* GetBackendCommandContract(const SBackendCommand &command) const;
	EApplicationEventDispatchDomain GetApplicationEventDispatchDomain(EApplicationEventType eType) const;
	LPCTSTR		GetApplicationEventDispatchDomainName(EApplicationEventDispatchDomain eDomain) const;
	bool		IsApplicationEventDispatchAllowed(const SApplicationEvent &event) const;
	EBackendExecutorDomain GetBackendCommandExecutorDomain(EBackendCommandType eType) const;
	EBackendLifecycleState GetBackendLifecycleState() const;
	LPCTSTR		GetBackendLifecycleStateName(EBackendLifecycleState eState) const;
	bool		IsBackendLifecycleStopping() const;
	void		SetBackendLifecyclePhase(EBackendLifecycleState eState, LPCTSTR pszReason = NULL);
	void		BeginBackendShutdownLifecycle(LPCTSTR pszReason = NULL);
	void		PrepareBackendShutdownForDiskIo(LPCTSTR pszReason = NULL);
	LPCTSTR		GetWorkerTopologyRoleName(EWorkerTopologyRole eRole) const;
	LPCTSTR		GetWorkerTopologyStateName(EWorkerTopologyState eState) const;
	EWorkerTopologyState GetWorkerTopologyState(EWorkerTopologyRole eRole) const;
	bool		StartWorkerTopology(LPCTSTR pszReason = NULL);
	void		StopWorkerTopology(LPCTSTR pszReason = NULL);
	bool		DrainWorkerTopology(DWORD dwTimeoutMs);
	void		CancelWorkerTopology(LPCTSTR pszReason = NULL);
	bool		StartPersistenceWorker();
	void		StopPersistenceWorker();
	bool		DrainPersistenceWorker(DWORD dwTimeoutMs);
	void		CancelPersistenceWorker();
	bool		QueuePersistenceWorkerItem(const SWorkerTopologyItem &item);
	bool		StartStartupLoadWorker(EStartupMetadataDomain eDomain);
	bool		QueueStartupLoadWorkerItem(EStartupMetadataDomain eDomain, const SWorkerTopologyItem &item);
	bool		StartNetworkUtilityWorker();
	void		StopNetworkUtilityWorker();
	bool		DrainNetworkUtilityWorker(DWORD dwTimeoutMs);
	void		CancelNetworkUtilityWorker();
	bool		QueueNetworkUtilityWorkerItem(const SWorkerTopologyItem &item);
	bool		StartNetworkParseCpuWorker();
	void		StopNetworkParseCpuWorker();
	bool		DrainNetworkParseCpuWorker(DWORD dwTimeoutMs);
	void		CancelNetworkParseCpuWorker();
	bool		QueueNetworkParseCpuWorkerItem(const SWorkerTopologyItem &item);
	bool		QueueCollectionImportWorkerJob(HWND hNotifyWnd, const CString &strPath);
	bool		QueueCollectionImportResult(HWND hNotifyWnd, SCollectionImportResult *pResult);
	SCollectionImportResult* PopCollectionImportResult(HWND hNotifyWnd);
	void		ClearCollectionImportResults(HWND hNotifyWnd);
	bool		BeginSharedFilesFileSystemReload(HWND hNotifyWnd, LONG lGeneration, uint64 *puReloadToken);
	bool		IsSharedFilesFileSystemReloadActive(HWND hNotifyWnd);
	bool		CompleteSharedFilesFileSystemReload(HWND hNotifyWnd, LONG lGeneration, uint64 uReloadToken);
	void		CancelSharedFilesFileSystemReload(HWND hNotifyWnd);
	bool		QueueSharedFilesFileSystemReloadWorkerJob(HWND hNotifyWnd, LONG lGeneration, uint64 uReloadToken, const CString &strDirectory);

	void		SearchClipboard();
	void		IgnoreClipboardLinks(const CString &strLinks)	{ m_strLastClipboardContents = strLinks; }
	void		PasteClipboard(int cat = 0);
	bool		IsEd2kFileLinkInClipboard();
	bool		IsEd2kServerLinkInClipboard();
	bool		IsEd2kLinkInClipboard(LPCSTR pszLinkType, int iLinkTypeLen);
	LPCTSTR		GetProfileFile()								{ return m_pszProfileName; }

	CString		CreateKadSourceLink(const CAbstractFile *f);

	// clipboard (text)
	bool		CopyTextToClipboard(const CString &strText);
	CString		CopyTextFromClipboard();

	void		OnlineSig();
	void		UpdateDisplayedTransferRates();
	void		GetDisplayedTransferRates(UINT& ruUploadDatarate, UINT& ruDownloadDatarate) const;
	void		UpdateReceivedBytes(uint32 bytesToAdd);
	void		UpdateSentBytes(uint32 bytesToAdd, bool sentToFriend = false);
	int			GetFileTypeSystemImageIdx(LPCTSTR pszFilePath, int iLength = -1, bool bNormalsSize = false);
	HIMAGELIST	GetSystemImageList()							{ return m_hSystemImageList; }
	HIMAGELIST	GetBigSystemImageList()							{ return m_hBigSystemImageList; }
	CSize		GetSmallSytemIconSize()							{ return m_sizSmallSystemIcon; }
	CSize		GetBigSytemIconSize()							{ return m_sizBigSystemIcon; }
	void		CreateBackwardDiagonalBrush();
	void		CreateAllFonts();
	const CString& GetDefaultFontFaceName();
	bool		IsPortchangeAllowed();
	bool		EnsureWindowsFirewallListenPortRules(bool bAllowElevation);
	bool		ApplyWindowsFirewallListenPortRules(bool bAllowElevation);
	bool		RemoveWindowsFirewallListenPortRules(bool bAllowElevation);
	bool		IsConnected(bool bIgnoreEd2k = false, bool bIgnoreKad = false);
	bool		IsFirewalled();
	bool		CanDoCallback(CUpDownClient *client);
	uint32		GetID();
	uint32		GetED2KPublicIPv4() const;	// return current (valid) public IP or 0 if unknown (ignore KAD connection)
	uint32		GetPublicIPv4() const;	// return current (valid) public IP or 0 if unknown
	void		SetPublicIPv4(const uint32 dwIP);
	CAddress	GetPublicIP() { return !GetPublicIPv6().IsNull() ? GetPublicIPv6() : CAddress(GetPublicIPv4(), false); };
	void		ResetStandByIdleTimer();

	// because nearly all icons we are loading are 16x16, the default size is specified as 16 and not as 32 nor LR_DEFAULTSIZE
	HICON		LoadIcon(LPCTSTR lpszResourceName, int cx = 16, int cy = 16, UINT uFlags = LR_DEFAULTCOLOR) const;
	HICON		LoadIcon(UINT nIDResource) const;
	HBITMAP		LoadImage(LPCTSTR lpszResourceName, LPCTSTR pszResourceType) const;
	bool		LoadSkinColor(LPCTSTR pszKey, COLORREF &crColor) const;
	bool		LoadSkinColorAlt(LPCTSTR pszKey, LPCTSTR pszAlternateKey, COLORREF &crColor) const;
	CString		GetSkinFileItem(LPCTSTR lpszResourceName, LPCTSTR pszResourceType) const;
	void		ApplySkin(LPCTSTR pszSkinProfile);
	void		EnableRTLWindowsLayout();
	void		DisableRTLWindowsLayout();
	void		UpdateDesktopColorDepth();
	void		UpdateLargeIconSize();

	bool		GetLangHelpFilePath(CString &strResult);
	void		SetHelpFilePath(LPCTSTR pszHelpFilePath);
	void		ShowHelp(UINT uTopic, UINT uCmd = HELP_CONTEXT);
	bool		ShowWebHelp(UINT uTopic);

	// thread safe log calls
	void		QueueDebugLogLine(bool bAddToStatusBar, LPCTSTR line, ...);
	void		QueueDebugLogLineEx(UINT uFlags, LPCTSTR line, ...);
	void		HandleDebugLogQueue();
	void		ClearDebugLogQueue(bool bDebugPendingMsgs = false);

	void		QueueLogLine(bool bAddToStatusBar, LPCTSTR line, ...);
	void		QueueLogLineEx(UINT uFlags, LPCTSTR line, ...);
	void		HandleLogQueue();
	void		ClearLogQueue(bool bDebugPendingMsgs = false);

	bool		DidWeAutoStart() { return m_bAutoStart; };
	void		ResetStandbyOff()								{ m_bStandbyOff = false; }
	std::set<CUtpSocket*> g_UtpSockets;
	time_t m_tLastDiskSpaceCheckTime;

	void StartDirWatchTP();
	void StopDirWatchTP(bool bWaitForCallbacks = true);
	bool DirWatchRootsChanged() const;
	LONG GetDirWatchChangeGeneration() const;
	void SyncDirWatchRootsHash();
	void QueueStartupDirWatchInit();
	void DrainDeletedAutoSharedDirs();
	void DrainAutoSharedNewDirs();
	void DrainDirWatchChangedDirectories(CStringArray& outDirs);
	void DrainDirWatchChangedFiles(CStringArray& outFiles);

	// UploadTimer bridges
	void OnUploadTick_100ms_DirWatch() noexcept;
	void OnUploadTick_1s_DirWatch() noexcept;
	void OnUploadTick_5s_DirWatch() noexcept;
protected:
	bool ProcessCommandline();
	void SetTimeOnTransfer();
	static BOOL CALLBACK SearchEmuleWindow(HWND hWnd, LPARAM lParam) noexcept;

	HIMAGELIST	m_hSystemImageList;
	CMapStringToPtr m_aExtToSysImgIdx;
	CSize		m_sizSmallSystemIcon;
	CCriticalSection m_fileTypeSystemImageLock;

	HIMAGELIST	m_hBigSystemImageList;
	CMapStringToPtr m_aBigExtToSysImgIdx;
	CSize		m_sizBigSystemIcon;

	CString		m_strDefaultFontFaceName;
	CString		m_strLastClipboardContents;

	// thread safe log calls
	CCriticalSection m_queueLock;
	CCriticalSection m_shutdownPartFlushDiskSpaceLock;
	CMap<CString, LPCTSTR, BYTE, BYTE> m_mapShutdownPartFlushDiskSpaceState;
	CCriticalSection m_partMetDiskSpaceLock;
	CMap<CString, LPCTSTR, BYTE, BYTE> m_mapPartMetDiskSpaceState;
	CTypedPtrList<CPtrList, SLogItem*> m_QueueDebugLog;
	CTypedPtrList<CPtrList, SLogItem*> m_QueueLog;

	WSADATA		m_wsaData;
	uint32		m_dwPublicIP;
	UINT		m_uDisplayedUploadDatarate;
	UINT		m_uDisplayedDownloadDatarate;
	bool		m_bGuardClipboardPrompt;
	bool		m_bAutoStart;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnHelp();

private:
	struct SChunkedDownloadParseJob
	{
		SChunkedDownloadParseJob();

		SBackendCommand m_command;
		CString m_strRawLinks;
		CString m_strTokenDelimiters;
		int m_iNextParsePos;
		UINT m_uParsed;
		DWORD m_dwStartedTick;
		DWORD m_dwLastProgressTick;
	};

	struct SDownloadLinkParseThreadParam
	{
		SDownloadLinkParseThreadParam();

		CemuleApp *m_pApp;
		SBackendCommand m_command;
		CString m_strRawLinks;
		CString m_strTokenDelimiters;
	};

	struct SWorkerTopologyThreadParam
	{
		SWorkerTopologyThreadParam();

		CemuleApp *m_pApp;
		EWorkerTopologyRole m_eRole;
	};

	struct SChunkedDownloadJob
	{
		SChunkedDownloadJob();

		SDownloadCommand m_command;
		uint64 m_uSequence;
		uint64 m_uCorrelationId;
		uint64 m_uCancellationToken;
		EBackendCommandSource m_eSource;
		EBackendCommandOrderingScope m_eOrderingScope;
		CString m_strOrderingKey;
		INT_PTR m_iNextIndex;
		UINT m_uProcessed;
		UINT m_uFailed;
		bool m_bBackendOwnerSafe;
		DWORD m_dwStartedTick;
		DWORD m_dwLastProgressTick;
		bool m_bBulkAddActive;
	};

	struct SBackendDownloadListJob
	{
		SBackendDownloadListJob();

		EDownloadCommandType m_eType;
		std::vector<CString> m_vecItemHashes;
		INT_PTR m_iNextIndex;
		UINT m_uAction;
		int m_iActionValue;
		CString m_strActionValue;
		bool m_bAddToCanceledMet;
		bool m_bDeleteCompletedFile;
		bool m_bBulkRemoveActive;
		bool m_bListUpdateBatchActive;
		bool m_bBackendOwnerSafe;
		bool m_bWaitingForDiskCleanup;
		UINT m_uPendingDiskDeletes;
		std::vector<CString> m_vecPendingUiRemovedHashes;
		std::vector<UINT> m_vecStartNextCategories;
		UINT m_uProcessed;
		UINT m_uFailed;
		UINT m_uStale;
		uint64 m_uSequence;
		uint64 m_uCorrelationId;
		uint64 m_uCancellationToken;
		EBackendCommandSource m_eSource;
		EBackendCommandOrderingScope m_eOrderingScope;
		CString m_strOrderingKey;
		DWORD m_dwStartedTick;
		DWORD m_dwLastProgressTick;
	};

	enum EImportPartWorkType
	{
		ImportPartWorkWrite,
		ImportPartWorkProgress,
		ImportPartWorkFinished
	};

	struct SImportPartWorkItem
	{
		SImportPartWorkItem();

		EImportPartWorkType m_eType;
		ImportPart_Struct *m_pImportPart;
		ImportOperationContext *m_pContext;
		WPARAM m_uProgress;
		bool m_bAborted;
	};

	void		QueueChunkedDownloadParseJob(const SBackendCommand &command);
	void		QueueChunkedDownloadParseFallbackJob(const SBackendCommand &command, LPCTSTR pszRawLinks, LPCTSTR pszTokenDelimiters);
	void		ClearDownloadLinkParseQueue();
	void		ProcessDownloadLinkParseJobsOnParserThread();
	void		PostChunkedDownloadParseJobMessage();
	void		ClearChunkedDownloadParseJobs();
	void		FailChunkedDownloadParseJobs(LPCTSTR pszMessage);
	void		QueueChunkedDownloadJob(const SBackendCommand &command);
	void		PostChunkedDownloadJobMessage();
	void		SetActiveDownloadAddOperationProgress(UINT uDone, UINT uTotal, bool bActive);
	void		ClearActiveDownloadAddOperationProgress();
	bool		HasBackendCommandThreadSignalTarget() const;
	void		BeginChunkedDownloadJobBulkAdd(SChunkedDownloadJob &job);
	void		EndChunkedDownloadJobBulkAdd(SChunkedDownloadJob &job);
	void		ClearChunkedDownloadJobs();
	bool		ProcessChunkedDownloadItem(SChunkedDownloadJob &job, const CString &strLink);
	bool		ProcessChunkedDownloadSnapshotItem(SChunkedDownloadJob &job, const SDownloadFileSnapshot &snapshot);
	UINT		GetChunkedDownloadJobItemCount(const SChunkedDownloadJob &job) const;
	bool		IsChunkedDownloadJobRunnable(const SChunkedDownloadJob *pJob) const;
	bool		CanProcessChunkedDownloadJobOnCurrentThread(const SChunkedDownloadJob *pJob) const;
	bool		ChunkedDownloadJobNeedsUiCompatibility(const SChunkedDownloadJob *pJob) const;
	bool		ShouldUseChunkedDownloadUiTimer(const SChunkedDownloadJob *pJob) const;
	void		FailChunkedDownloadJobs(LPCTSTR pszMessage);
	void		QueueBackendDownloadListJob(const SBackendCommand &command);
	void		ProcessBackendDownloadListJobsOnCurrentThread();
	void		ProcessSearchIngestJobsOnCurrentThread();
	void		ClearSearchIngestProcessing();
	bool		IsSearchIngestProcessingPending() const;
	void		ClearBackendDownloadListJobs();
	bool		IsBackendDownloadListJobRunnable(const SBackendDownloadListJob *pJob) const;
	bool		CanProcessBackendDownloadListJobOnCurrentThread(const SBackendDownloadListJob *pJob) const;
	bool		BackendDownloadListJobNeedsUiCompatibility(const SBackendDownloadListJob *pJob) const;
	bool		ProcessBackendDownloadListJobItem(SBackendDownloadListJob &job, LPCTSTR pszHash);
	bool		ProcessBackendDownloadRemoveJobItem(SBackendDownloadListJob &job, LPCTSTR pszHash);
	bool		ProcessBackendDownloadStateJobItem(SBackendDownloadListJob &job, LPCTSTR pszHash);
	void		QueueBackendDownloadStartNextCategory(SBackendDownloadListJob &job, UINT uCategory);
	void		QueueBackendDownloadRemoveRows(SBackendDownloadListJob &job, LPCTSTR pszHash);
	void		FlushBackendDownloadRemoveRows(SBackendDownloadListJob &job);
	bool		FinishBackendDownloadListJob(SBackendDownloadListJob &job);
	bool		CanProcessImportPartWorkItemsOnCurrentThread() const;
	void		ProcessImportPartWorkItemsOnCurrentThread(DWORD dwSliceStart);
	void		ProcessImportPartWorkItem(SImportPartWorkItem *pItem);
	void		ClearImportPartWorkItems();
	bool		QueueImportPartWorkItem(SImportPartWorkItem *pItem);
	void		ReleaseImportPartWorkItem(SImportPartWorkItem *pItem, bool bAbort);
	bool		CompleteBackendDownloadRemoveDiskCleanup(uint64 uSequence, uint64 uCorrelationId, UINT uCompletedCount, UINT uFailedCount);
	void		QueueBackendDownloadListFailureEvent(const SBackendDownloadListJob &job, LPCTSTR pszHash, LPCTSTR pszStage, LPCTSTR pszFilePath, DWORD dwError);
	void		PrepareBackendCommandEnvelope(SBackendCommand &command, EBackendCommandSource eSource, EBackendCommandOrderingScope eScope, LPCTSTR pszOrderingKey);
	void		EnsureBackendCommandEnvelope(SBackendCommand &command) const;
	void		AssignBackendCommandCancellationToken(SBackendCommand &command);
	CString		BuildBackendCommandOrderingKey(const SBackendCommand &command) const;
	bool		ValidateBackendCommandContract(const SBackendCommand &command, CString *pstrStage = NULL) const;
	bool		ValidateBackendCommandEnvelope(const SBackendCommand &command, CString *pstrStage = NULL) const;
	bool		ShouldAcceptBackendCommand(const SBackendCommand &command) const;
	bool		ShouldAcceptApplicationEvent(const SApplicationEvent &event) const;
	bool		IsBackendCommandDrainingForShutdown() const;
	bool		QueuePersistenceBackendCommand(const SBackendCommand &command, DWORD dwDelayMs = 0);
	bool		QueuePersistenceCommandOwnerEvent(const SBackendCommand &command);
	bool		PopPersistenceBackendCommand(SBackendCommand &command);
	void		ClearPersistenceBackendCommandQueue();
	bool		HasBackendWorkForShutdown();
	void		DrainBackendWorkForShutdown();
	bool		IsBackendCommandDroppableUnderPressure(const SBackendCommand &command) const;
	bool		CoalesceBackendCommandLocked(const SBackendCommand &command);
	bool		TrimBackendCommandQueueForPressureLocked(const SBackendCommand &command, INT_PTR iTargetCount);
	bool		IsApplicationEventDroppableUnderPressure(const SApplicationEvent &event) const;
	bool		TrimApplicationEventQueueForPressureLocked(const SApplicationEvent &event, INT_PTR iTargetCount);
	bool		StartBackendCommandThread();
	void		StopBackendCommandThread();
	bool		SignalBackendCommandThread();
	static UINT AFX_CDECL BackendCommandThreadProc(LPVOID pParam);
	UINT		RunBackendCommandThread();
	void		ProcessBackendCommandsOnCurrentThread();
	bool		HasBackendContinuationWork() const;
	bool		ProcessBackendContinuationSlice(DWORD dwSliceStart, bool *pbYieldRequested = NULL);
	bool		PostBackendCommandMessage();
	bool		PostBackendCommandUiMessage();
	void		ClearBackendCommandQueue();
	void		ClearBackendWorkQueues();
	bool		IsWorkerTopologyRoleValid(EWorkerTopologyRole eRole) const;
	bool		IsStartupLoadWorkerRole(EWorkerTopologyRole eRole) const;
	bool		AllStartupMetadataLoadsTerminal() const;
	bool		IsWorkerTopologyThreadRole(EWorkerTopologyRole eRole) const;
	UINT		GetWorkerTopologyQueueLimit(EWorkerTopologyRole eRole) const;
	bool		ShouldCoalesceWorkerTopologyRole(EWorkerTopologyRole eRole) const;
	bool		ShouldDropOldestWorkerTopologyItem(EWorkerTopologyRole eRole) const;
	bool		ShouldDrainWorkerTopologyRole(EWorkerTopologyRole eRole) const;
	bool		ShouldCancelWorkerTopologyRole(EWorkerTopologyRole eRole) const;
	bool		ShouldAcceptWorkerTopologyItem(EWorkerTopologyRole eRole) const;
	bool		CleanupStoppedWorkerTopologyRoleLocked(EWorkerTopologyRole eRole);
	bool		StartWorkerTopologyRole(EWorkerTopologyRole eRole, LPCTSTR pszReason);
	void		StopWorkerTopologyRole(EWorkerTopologyRole eRole, DWORD dwTimeoutMs, LPCTSTR pszReason);
	bool		DrainWorkerTopologyRole(EWorkerTopologyRole eRole, DWORD dwTimeoutMs);
	void		CancelWorkerTopologyRole(EWorkerTopologyRole eRole, LPCTSTR pszReason);
	bool		QueueWorkerTopologyItem(EWorkerTopologyRole eRole, const SWorkerTopologyItem &item);
	bool		PopWorkerTopologyItem(EWorkerTopologyRole eRole, SWorkerTopologyItem &item);
	void		ClearWorkerTopologyQueue(EWorkerTopologyRole eRole);
	bool		IsWorkerTopologyQueueEmpty(EWorkerTopologyRole eRole);
	bool		IsWorkerTopologyRoleIdle(EWorkerTopologyRole eRole);
	bool		CoalesceWorkerTopologyItemLocked(EWorkerTopologyRole eRole, const SWorkerTopologyItem &item);
	static UINT AFX_CDECL WorkerTopologyThreadProc(LPVOID pParam);
	UINT		RunWorkerTopologyThread(EWorkerTopologyRole eRole);
	void		ProcessWorkerTopologyItem(const SWorkerTopologyItem &item);
	bool		WaitWorkerTopologyItemDueTime(EWorkerTopologyRole eRole, DWORD dwDueTick);
	bool		ProcessNetworkUtilityWorkerItem(const SWorkerTopologyItem &item);
	bool		ProcessNetworkParseCpuWorkerItem(const SWorkerTopologyItem &item);
	bool		ProcessCollectionImportWorkerItem(const SWorkerTopologyItem &item);
	bool		ProcessPersistenceWorkerItem(const SWorkerTopologyItem &item);
	bool		ProcessStartupLoadWorkerItem(const SWorkerTopologyItem &item);
	bool		IsStartupMetadataDomainValid(EStartupMetadataDomain eDomain) const;
	EWorkerTopologyRole GetStartupMetadataWorkerRole(EStartupMetadataDomain eDomain) const;
	void		SetStartupMetadataState(EStartupMetadataDomain eDomain, EStartupMetadataState eState, LONG lGeneration, uint64 uCancellationToken, DWORD dwLastError, bool bCancelRequested, LPCTSTR pszReason);
	void		QueueStartupMetadataStateChangedEvent(EStartupMetadataDomain eDomain, EStartupMetadataState eState, LONG lGeneration, uint64 uCancellationToken, DWORD dwLastError, LPCTSTR pszReason);
	bool		IsStartupMetadataSaveAllowed(EPersistenceCommandType eCommand, CString *pstrReason = NULL) const;
	bool		RejectStartupMetadataPersistenceCommand(const SBackendCommand &command, LPCTSTR pszStage, DWORD dwLastError);
	bool		ValidateNetworkPacketCommandSnapshot(const SNetworkPacketCommand &command, CString *pstrStage = NULL) const;
	bool		EnqueueNetworkPacketBackendCommand(SBackendCommand &command);
	void		ExecuteBackendCommand(const SBackendCommand &command);
	void		ExecuteBackendCommandUiCompatibilityApply(const SBackendCommand &command);
	void		ExecuteBackendCommandBackendOwnerApply(const SBackendCommand &command);
	bool		IsBackendCommandAllowedForCurrentApplyMode(const SBackendCommand &command, CString *pstrStage = NULL) const;
	void		ExecuteNetworkPacketCommand(const SBackendCommand &command);
	bool		ApplyClientSearchAnswerNetworkCommand(const SNetworkPacketCommand &command, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError);
	bool		ApplyServerSearchAnswerNetworkCommand(const SNetworkPacketCommand &command, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError);
	bool		ValidateSearchOwnerCommandTarget(const SNetworkPacketCommand &command, LPCTSTR pszEntryPoint, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError);
	bool		ResolveDownloadNetworkFileTarget(const SNetworkPacketCommand &command, CPartFile **ppFile, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError);
	bool		ApplyDownloadFileStatusNetworkCommand(const SNetworkPacketCommand &command, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError);
	bool		ApplyDownloadHashSetNetworkCommand(const SNetworkPacketCommand &command, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError);
	bool		ApplyDownloadFoundSourcesNetworkCommand(const SNetworkPacketCommand &command, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError);
	bool		ApplyDownloadSourceExchangeNetworkCommand(const SNetworkPacketCommand &command, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError);
	bool		ApplyDownloadBlockReceiveNetworkCommand(const SNetworkPacketCommand &command, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError);
	bool		ApplyKadPacketNetworkCommand(const SNetworkPacketCommand &command, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError);
	void		ExecutePersistenceCommandOnCurrentThread(const SBackendCommand &command);
	bool		GuardPersistenceCommandMutation(const SBackendCommand &command, LPCTSTR pszStage);
	void		QueueBackendCommandFailedEvent(const SBackendCommand &command, LPCTSTR pszMessage);
	void		QueueBackendCommandFailedEventEx(const SBackendCommand &command, EBackendCommandFailureKind eFailure, LPCTSTR pszStage, DWORD dwLastError);
	bool		QueueApplicationEvent(const SApplicationEvent &event);
	bool		QueueApplicationEvent(const SApplicationEvent &event, bool *pbEventOwnedByQueue);
	void		EnsureApplicationEventEnvelope(SApplicationEvent &event) const;
	bool		ValidateApplicationEventEnvelope(const SApplicationEvent &event, CString *pstrStage = NULL) const;
	bool		PostApplicationEventMessage();
	void		ClearApplicationEventQueue();
	bool		CoalesceApplicationEventLocked(const SApplicationEvent &event);
	void		DispatchApplicationEvent(const SApplicationEvent &event);
	void		DispatchTelemetryApplicationEvent(const SApplicationEvent &event);
	void		DispatchUiNotificationApplicationEvent(const SApplicationEvent &event);
	void		DispatchUiCommandBridgeApplicationEvent(const SApplicationEvent &event);
	void		DispatchBackendResultApplicationEvent(const SApplicationEvent &event);
	LPCTSTR		GetModelMutationDomainName(EModelMutationDomain eDomain) const;
	LPCTSTR		GetNetworkParseDomainName(ENetworkParseDomain eDomain) const;

private:
	UINT		m_wTimerRes;
	bool		m_bStandbyOff;
	uint8			m_nConnectionState;
	CCriticalSection	m_downloadLinkParseQueueLock;
	CTypedPtrList<CPtrList, SDownloadLinkParseThreadParam*> m_downloadLinkParseQueue;
	bool			m_bDownloadLinkParseWorkerActive;
	CTypedPtrList<CPtrList, SChunkedDownloadParseJob*> m_chunkedDownloadParseJobs;
	bool			m_bChunkedDownloadParseMessagePending;
	CTypedPtrList<CPtrList, SChunkedDownloadJob*> m_chunkedDownloadJobs;
	bool			m_bChunkedDownloadMessagePending;
	mutable CCriticalSection m_activeDownloadAddOperationLock;
	bool			m_bActiveDownloadAddOperation;
	bool			m_bActiveDownloadAddSavingToDisk;
	UINT			m_uActiveDownloadAddDone;
	UINT			m_uActiveDownloadAddTotal;
	CTypedPtrList<CPtrList, SBackendDownloadListJob*> m_backendDownloadListJobs;
	CTypedPtrList<CPtrList, SImportPartWorkItem*> m_importPartWorkItems;
	CCriticalSection	m_importPartWorkQueueLock;
	CTypedPtrList<CPtrList, SBackendCommand*> m_backendCommandQueue;
	CCriticalSection	m_backendCommandQueueLock;
	volatile LONG	m_lBackendCommandMessagePending;
	volatile LONG	m_lBackendCommandDispatching;
	volatile LONG	m_lBackendCommandReentryTraceTick;
	CTypedPtrList<CPtrList, SBackendCommand*> m_persistenceCommandQueue;
	CCriticalSection	m_persistenceCommandQueueLock;
	CList<SApplicationEvent, const SApplicationEvent&> m_applicationEventQueue;
	CCriticalSection	m_applicationEventQueueLock;
	CTypedPtrList<CPtrList, SCollectionImportResult*> m_collectionImportResults;
	CCriticalSection	m_collectionImportResultLock;
	CMap<HWND, HWND, LONG, LONG> m_sharedFilesFileSystemReloadGenerations;
	CMap<HWND, HWND, uint64, uint64> m_sharedFilesFileSystemReloadTokens;
	CCriticalSection	m_sharedFilesFileSystemReloadStateLock;
	uint64		m_uNextSharedFilesFileSystemReloadToken;
	CCriticalSection	m_startupMetadataStateLock;
	SStartupMetadataLoadState m_startupMetadataStates[StartupMetadataDomainCount];
	uint64		m_uNextStartupMetadataCancellationToken;
	volatile LONG	m_lApplicationEventMessagePending;
	bool			m_bApplicationEventDispatching;
	CList<SWorkerTopologyItem, const SWorkerTopologyItem&> m_workerTopologyQueues[WorkerTopologyRoleCount];
	CCriticalSection	m_workerTopologyQueueLocks[WorkerTopologyRoleCount];
	CCriticalSection	m_workerTopologySequenceLock;
	CWinThread*	m_pWorkerTopologyThreads[WorkerTopologyRoleCount];
	HANDLE		m_hWorkerTopologyEvents[WorkerTopologyRoleCount];
	HANDLE		m_hWorkerTopologyStopEvents[WorkerTopologyRoleCount];
	DWORD		m_dwWorkerTopologyThreadIds[WorkerTopologyRoleCount];
	bool			m_bWorkerTopologyAccepting[WorkerTopologyRoleCount];
	volatile LONG m_alWorkerTopologyStates[WorkerTopologyRoleCount];
	volatile LONG m_alWorkerTopologyInFlight[WorkerTopologyRoleCount];
	uint64		m_uNextWorkerTopologySequence;
	uint64		m_uWorkerTopologyCancellationTokens[WorkerTopologyRoleCount];
	volatile LONG	m_lSearchIngestProcessingPending;
	uint64		m_uNextBackendCommandSequence;
	uint64		m_uNextBackendCommandCancellationToken;
	DWORD		m_dwBackendOwnerThreadId;
	DWORD		m_dwNetworkParserOwnerThreadId;
	volatile LONG	m_lBackendLifecycleState;
	CWinThread*	m_pBackendCommandThread;
	HANDLE		m_hBackendCommandEvent;
	HANDLE		m_hBackendCommandStopEvent;
	DWORD		m_dwBackendCommandThreadId;
public:
	uint8			GetConnectionState() { return m_nConnectionState; }
	void			SetConnectionState(uint8 state) { m_nConnectionState = state; }

public:
	bool			IsEd2kFriendLinkInClipboard();

public:
	void Backup(bool bOnExit);
	void BackupMain();
	time_t GetLastBackupTime();
	time_t tLastBackupTime;
private:
	volatile LONG m_lBackupWorkerActive;

private:
	volatile LONG	m_lNetworkBindSocketCreationPermit;
	uint32			m_dwLastValidIPv4;
	CAddress		m_LastValidIPv6;

public:
	const CAddress& GetPublicIPv6() const { return m_PublicIPv6; }
	void			SetPublicIPv6(const CAddress& IP);
	void			UpdatePublicIPv6();
	CAddress		m_LastReceivedIPv4;
	CAddress		m_LastReceivedIPv6;
private:
	CAddress		m_PublicIPv6;
	bool			m_bFirstIPv4;
	bool			m_bFirstIPv6;
};

extern CemuleApp theApp;


//////////////////////////////////////////////////////////////////////////////
// CTempIconLoader

class CTempIconLoader
{
	HICON m_hIcon;

public:
	// because nearly all icons we are loading are 16x16, the default size is specified as 16 and not as 32 nor LR_DEFAULTSIZE
	explicit CTempIconLoader(LPCTSTR pszResourceID, int cx = 16, int cy = 16, UINT uFlags = LR_DEFAULTCOLOR);
	explicit CTempIconLoader(UINT uResourceID, int cx = 16, int cy = 16, UINT uFlags = LR_DEFAULTCOLOR);
	~CTempIconLoader();

	operator HICON() const										{ return m_hIcon; }
};
#ifdef _DEBUG
int CrtDebugReportCB(int reportType, char* message, int* returnValue) noexcept;
#endif
