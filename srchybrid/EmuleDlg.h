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
#include <deque>
#include <map>
#include <set>
#include "MeterIcon.h"
#include "TaskbarNotifier.h"
#include "eMuleAI/ToastNotify.h"
#include "eMuleAI/MenuXP.h"
#include "TrayDialog.h"
#include "KademliaWnd.h"
#include "SearchResultsWnd.h"
#include "SharedFilesWnd.h"
#include "ChatWnd.h"
#include "IrcWnd.h"
#include "SplashScreen.h"
#include "eMuleAI/SpeedGraph.h"
#include "KnownFileList.h"
#include "eMuleAI/NetBind.h"
#include "eMuleAI/IpGuard.h"

namespace Kademlia
{
	class CSearch;
	class CContact;
	class CEntry;
	class CUInt128;
};

class CChatWnd;
class CIrcWnd;
class CKademliaWnd;
class CKnownFile;
class CKnownFileList;
class CMainFrameDropTarget;
class CMiniMule;
class CMuleStatusBarCtrl;
class CMuleSystrayDlg;
class CMuleToolbarCtrl;
class CPreferencesDlg;
class CCollection;
class CKnownFileList;
class CPartFile;
class CSearchDlg;
class CServerWnd;
class CSharedFilesWnd;
class CStatisticsDlg;
class CTransferDlg;
class CTitleVersionOverlayWnd;
class CStartupLoadingDlg;
class CShutdownProgressDlg;
struct Status;
struct SSearchParams;

struct SCollectionImportResult
{
	SCollectionImportResult()
		: hNotifyWnd(NULL)
		, pCollection(NULL)
		, bSuccess(false)
		, dwLastError(0)
	{
	}

	HWND hNotifyWnd;
	CCollection* pCollection;
	CString strPath;
	CString strStage;
	bool bSuccess;
	DWORD dwLastError;
};

struct SStartupKnownFilesLoadResult
{
	SStartupKnownFilesLoadResult()
		: pKnownRecords(NULL)
		, pCancelledRecords(NULL)
		, uNextKnownRecord(0)
		, uNextParsedFile(0)
		, uNextCancelledRecord(0)
		, dwCancelledFilesSeed(0)
		, uKnownFileWorkUnitsTotal(0)
		, uKnownFileWorkUnitsApplied(0)
		, lGeneration(0)
		, uCancellationToken(0)
		, bSuccess(false)
		, bApplyStarted(false)
		, bKnownRecordsParsed(false)
		, bCompletionStarted(false)
		, dwLastError(0)
	{
	}

	CStartupKnownFilesRecords* pKnownRecords;
	CStartupCancelledFilesRecords* pCancelledRecords;
	std::vector<CKnownFile*> vecParsedKnownFiles;
	std::vector<uint32> vecParsedKnownFileWorkUnits;
	size_t uNextKnownRecord;
	size_t uNextParsedFile;
	size_t uNextCancelledRecord;
	uint32 dwCancelledFilesSeed;
	uint64 uKnownFileWorkUnitsTotal;
	uint64 uKnownFileWorkUnitsApplied;
	LONG lGeneration;
	uint64 uCancellationToken;
	bool bSuccess;
	bool bApplyStarted;
	bool bKnownRecordsParsed;
	bool bCompletionStarted;
	DWORD dwLastError;
	CString strStage;
};

// emuleapp <-> emuleapp
#define OP_ED2KLINK				12000
#define OP_CLCOMMAND			12001
#define OP_COLLECTION			12002

#define	EMULE_HOTMENU_ACCEL		'x'
#define	EMULSKIN_BASEEXT		_T("eMuleSkin")

class CemuleDlg : public CTrayDialog
{
	friend class CMuleToolbarCtrl;
	friend class CMiniMule;
	friend class CTitleVersionOverlayWnd;
	friend class CBulkOperationExitOverlayWnd;
	friend class CStartupLoadingDlg;
	friend class CShutdownProgressDlg;

	enum
	{
		IDD = IDD_EMULE_DIALOG
	};

	enum
	{
		UWM_POST_INIT_CONTROLS = WM_APP + 101, // defer fragile UI setup until window is live
		UWM_EMULEAI_VERSIONCHECK_RESULT = WM_APP + 102,
		TIMER_TITLE_VERSION_ANIMATION = 0x7A11,
		TIMER_SPECIAL_THANKS_ANIMATION = 0x7A12,
		TIMER_CLOSE_AFTER_BULK_OPERATIONS = 0x7A13,
		TIMER_DOWNLOAD_OVERLAY_COMPLETION_DELAY = 0x7A14,
		TIMER_STARTUP_APPLY_PUMP = 0x7A15,
		TIMER_UI_LOG_FLUSH = 0x7A16,
		TIMER_CHUNKED_DOWNLOAD_ADD = 0x7A17,
		TIMER_IP_GUARD_MONITOR = 0x7A18,
		DOWNLOAD_OVERLAY_COMPLETION_DELAY_MS = 500,
		STARTUP_APPLY_PUMP_INTERVAL_MS = 1,
		STARTUP_OVERLAY_BULK_REFRESH_THROTTLE_MS = 500
	};

	//Client icons for all windows
	CImageList m_IconList;
	CReBarCtrl m_ctlMainTopReBar;
	CTaskbarNotifier m_wndTaskbarNotifier;
	CToastNotify m_wndToastNotifier;
	void SetClientIconList();
public:
	enum
	{
		UWM_EMULEAI_PROCESS_CHUNKED_DOWNLOADS = WM_APP + 103,
		UWM_EMULEAI_PROCESS_CHUNKED_SEARCH_INGEST = WM_APP + 104,
		UWM_EMULEAI_DISPATCH_APPLICATION_EVENT = WM_APP + 105,
		UWM_EMULEAI_PROCESS_BACKEND_COMMANDS = WM_APP + 106,
		UWM_EMULEAI_PROCESS_CHUNKED_DOWNLOAD_PARSE = WM_APP + 107,
		UWM_EMULEAI_COLLECTION_IMPORT_READY = WM_APP + 108,
		UWM_EMULEAI_STARTUP_KNOWNFILES_LOAD_READY = WM_APP + 109,
		UWM_EMULEAI_STARTUP_CLIENTHISTORY_LOAD_READY = WM_APP + 110,
		UWM_EMULEAI_IPFILTER_DOWNLOAD_PROGRESS = WM_APP + 111,
		UWM_EMULEAI_IPFILTER_DOWNLOAD_FINISHED = WM_APP + 112,
		UWM_EMULEAI_STARTUP_STOREDSEARCHES_LOAD_READY = WM_APP + 113,
		UWM_EMULEAI_STARTUP_DOWNLOADS_LOAD_READY = WM_APP + 114,
		UWM_EMULEAI_STARTUP_OVERLAY_REFRESH = WM_APP + 115,
		UWM_EMULEAI_STARTUP_APPLY_PUMP = WM_APP + 116,
		UWM_EMULEAI_IPGEOLOCATION_DOWNLOAD_PROGRESS = WM_APP + 117,
		UWM_EMULEAI_IPGEOLOCATION_DOWNLOAD_FINISHED = WM_APP + 118,
		UWM_EMULEAI_FLUSH_UI_LOG = WM_APP + 119
	};

	bool StartChunkedDownloadAddTimer() { return SetTimer(TIMER_CHUNKED_DOWNLOAD_ADD, 1, NULL) != 0; }
	void FlushQueuedUiLogLines();

	explicit CemuleDlg(CWnd *pParent = NULL);
	~CemuleDlg();

	CImageList& GetClientIconList();
	void ShowConnectionState();
	void ShowNotifier(LPCTSTR pszText, TbnMsg nMsgType, LPCTSTR pszLink = NULL, bool bForceSoundOFF = false);
	void ShowNotificationPopup(LPCTSTR pszText, TbnMsg nMsgType, LPCTSTR pszLink);
	bool ShowTrayBalloonNotification(LPCTSTR pszText, TbnMsg nMsgType, LPCTSTR pszLink);
	void SendNotificationMail(TbnMsg nMsgType, LPCTSTR pszText);
	void HandleNotifierClicked(TbnMsg nMsgType, LPARAM lParam);
	void HandleNotifierClicked(TbnMsg nMsgType, const CString& strLink);
	void ShowUserCount();
	void ShowMessageState(UINT nIcon);
	void SetActiveDialog(CWnd *dlg);
	CWnd* GetActiveDialog() const			{ return activewnd; }
	void ShowTransferRate(bool bForceAll = false);
	void ShowPing();
	void Localize();
	CString GetMainWindowTitleText() const;
	void UpdateIpGuardMonitor(bool bForcePublicIpProbe = true);
	void UpdateVpnGuardMonitor(bool bForcePublicIpProbe = true);
	void ApplyIpGuardNetworkBlock(const CString& strReason, const CString& strOverlayText = CString());
	void ClearIpGuardNetworkBlock(bool bRestartLocalSockets);
	void ClearVpnGuardNetworkBlock(bool bRestartLocalSockets);
	void TryRestoreIpGuardNetworkConnections();
	bool IsIpGuardOverlayVisible() const;
	bool CanUseP2PConnectionCommands() const;
	void LogP2PConnectionCommandBlocked(bool bUserVisible);
	void StopConnectionForNetworkBindChange(bool& rbRestoreEd2k, bool& rbRestoreKad);
	void RestoreConnectionAfterNetworkBindChange(bool bRestoreEd2k, bool bRestoreKad);
	bool IsSessionNetworkBlocked() const		{ return m_bIpGuardStartupBlocked; }
	bool IsVpnGuardNetworkBlockActive() const	{ return m_bVpnGuardNetworkBlockActive; }
	CString GetSessionNetworkBlockReason() const	{ return m_strIpGuardStartupBlockReason; }

#ifdef HAVE_WIN7_SDK_H
	void UpdateStatusBarProgress();
	void UpdateThumbBarButtons(bool initialAddToDlg = false);
	void OnTBBPressed(UINT id);
	void EnableTaskbarGoodies(bool enable);

	enum TBBIDS
	{
		TBB_FIRST,
		TBB_CONNECT = TBB_FIRST,
		TBB_DISCONNECT,
		TBB_THROTTLE,
		TBB_UNTHROTTLE,
		TBB_PREFERENCES,
		TBB_LAST = TBB_PREFERENCES
	};
#endif

	// Logging
	void AddLogText(UINT uFlags, LPCTSTR pszText);
	void AddServerMessageLine(UINT uFlags, LPCTSTR pszLine);
	void ResetLog();
	void ResetDebugLog();
	void ResetServerInfo();
	void ResetLeecherLog();
	CString GetLastLogEntry();
	CString	GetLastDebugLogEntry();
	CString	GetAllLogEntries();
	CString	GetAllDebugLogEntries();
	CString GetServerInfoText();

	CString	GetConnectionStateString();
	UINT GetConnectionStateIconIndex() const;
	CString	GetTransferRateString();
	CString	GetUpDatarateString(UINT uUpDatarate = UINT_MAX);
	CString	GetDownDatarateString(UINT uDownDatarate = UINT_MAX);

	void KillMainTimer();
	void StartMainTimer();
	void CheckScheduledTasks();
	void StopTimer();
	void DoVersioncheck(bool manual);
	void CheckCloseAfterBulkOperations();
	void OpenVersionReleasesURL() const;
	void ApplyHyperTextFont(LPLOGFONT pFont);
	void ApplyLogFont(LPLOGFONT pFont);
	void ProcessED2KLink(LPCTSTR pszData);
	bool PostSharedFileListFoundFilesAsync();
	bool PostSharedFilesCtrlUpdateFileAsync(CKnownFile* pFile);
	bool PostFileOpProgressAsync(CKnownFile* pFile, WPARAM uProgress);
	bool PostPartFileOpProgressAsync(CPartFile* pFile, DWORD dwRuntimeID, const uchar* pucFileHash, WPARAM uProgress);
	void ProcessCollectionFile(const CString &strPath);
	void SetStatusBarPartsSize();
	INT_PTR	ShowPreferences(UINT uStartPageID = UINT_MAX);
	bool IsPreferencesDlgOpen() const;
	bool IsTrayIconToFlash()				{ return m_iMsgIcon != 0; }
	void SetToolTipsDelay(UINT uMilliseconds);
	void StartUPnP(bool bReset = true, uint16 nForceTCPPort = 0, uint16 nForceUDPPort = 0);
	void RefreshUPnP(bool bRequestAnswer = false);
	HBRUSH GetCtlColor(CDC*, CWnd*, UINT);

	virtual void OnTrayRButtonUp(CPoint pt);
	virtual void OnTrayLButtonUp();
	virtual void OnTrayBalloonUserClick();
	virtual void TrayMinimizeToTrayChange();
	virtual void RestoreWindow();
	virtual void HtmlHelp(DWORD_PTR dwData, UINT nCmd = 0x000F);

	bool IsInitializing() const;
	enum EBulkOperationProgressSlot
	{
		BulkOperationProgressSearch = 0,
		BulkOperationProgressDownload,
		BulkOperationProgressSharedFiles,
		BulkOperationProgressBackendDownload,
		BulkOperationProgressCount
	};
	void SetBulkOperationProgressState(EBulkOperationProgressSlot eSlot, bool bActive, bool bCanCancel, bool bAdd, bool bDelete, bool bUpdate, UINT uDone, UINT uTotal, bool bListUpdateAfterCompletion = false, bool bHashing = false);
	void ClearBulkOperationProgressState(EBulkOperationProgressSlot eSlot);
	bool GetBulkOperationProgressState(EBulkOperationProgressSlot eSlot, bool& bCanCancel, bool& bAdd, bool& bDelete, bool& bUpdate, UINT& uDone, UINT& uTotal) const;
	void CancelActiveBulkOperations();
	void RefreshActiveBulkOperationOverlays();
	void DeferUiLogFlush(DWORD dwDelayMs);
	void PostStartupOverlayRefresh();
	void NotifyStartupSearchKnownTypesDependencyReady();
	void NotifyStartupSearchKnownTypesRefreshCompleted(bool bCompleted = true);
	bool IsStartupSearchKnownTypesRefreshComplete() const;
	void StartDownloadOverlayCompletionDelay();
	void ShowStartupLoadingDialog();
	void RefreshStartupLoadingDialogProgress(bool bForcePaint);
	void HideStartupLoadingDialog(bool bRestoreOverlays = true);
	bool IsStartupLoadingDialogVisible() const;
	enum EShutdownProgressStage
	{
		ShutdownProgressNetwork = 0,
		ShutdownProgressDiskIo,
		ShutdownProgressSaveData,
		ShutdownProgressDownloads,
		ShutdownProgressCleanup,
		ShutdownProgressStageCount
	};
	void ShowShutdownProgressDialog();
	void UpdateShutdownProgress(UINT uStage, UINT uDone, UINT uTotal, bool bForcePaint = false);
	void ConfigureShutdownProgressEstimates(UINT uDownloadCount, bool bSaveSources);
	void PumpShutdownProgressDialog();
	void HideShutdownProgressDialog();
	bool IsShutdownProgressDialogVisible() const;
	void DeferMainWindowForStartupLoading(const WINDOWPLACEMENT& wpRestore);
	void ShowMainWindowAfterStartupLoading();
	bool ShouldSuppressMainWindowForStartupLoading() const;

	CTransferDlg	*transferwnd;
	CServerWnd		*serverwnd;
	CPreferencesDlg	*preferenceswnd;
	CSharedFilesWnd	*sharedfileswnd;
	CSearchDlg		*searchwnd;
	CChatWnd		*chatwnd;
	CMuleStatusBarCtrl *statusbar;
	CStatisticsDlg	*statisticswnd;
	CIrcWnd			*ircwnd;
	CMuleToolbarCtrl *toolbar;
	CKademliaWnd	*kademliawnd;
	CSplashScreen	*m_pSplashWnd;
	CWnd			*activewnd;
	uint8			status;
	HWND			m_hWndSearchDlg;
	HWND			m_hWndTransferDlg;

	void ShowSpeedGraph(bool bShow = true); 
	void SetSpeedGraphLimits();
	void ResizeSpeedGraph();
	CSpeedGraph m_UpSpeedGraph;
	CSpeedGraph m_DownSpeedGraph;

protected:
	WINDOWPLACEMENT m_wpFirstRestore;
	HICON			m_hIcon;
	HICON			m_hIconSmall;
	HICON			m_connicons[9];
	HICON			m_contactIcons[5];
	HICON			transicons[4];
	HICON			imicons[3];
	HICON			m_icoSysTrayCurrent;
	HICON			usericon;
	CMeterIcon		m_TrayIcon;
	HICON			m_icoSysTrayConnected;		// do not use this icon for anything but the system tray!!!
	HICON			m_icoSysTrayDisconnected;	// do not use this icon for anything but the system tray!!!
	HICON			m_icoSysTrayLowID;			// do not use this icon for anything but the system tray!!!
	CImageList		imagelist;
	CMenuXP		trayPopup;
	CMuleSystrayDlg	*m_pSystrayDlg;
	CMainFrameDropTarget	*m_pDropTarget;
	CMenu			m_SysMenuOptions;
	CMenu			m_menuUploadCtrl;
	CMenu			m_menuDownloadCtrl;
	int				m_iMsgIcon;
	UINT			m_uLastSysTrayIconCookie;
	uint32			m_uUpDatarate;
	uint32			m_uDownDatarate;
	bool			m_bVersionCheckInProgress;
	bool			m_bNewVersionAvailable;
	bool			m_bStartMinimizedChecked;
	bool			m_bStartMinimized;
	bool			m_bMsgBlinkState;
	bool			m_bConnectRequestDelayedForUPnP;
	bool			m_bKadSuspendDisconnect;
	bool			m_bEd2kSuspendDisconnect;
	bool			m_bInitedCOM;
	bool			m_bSpecialThanksAnimationTimerActive;
#ifdef HAVE_WIN7_SDK_H
	CComPtr<ITaskbarList3>	m_pTaskbarList;
	THUMBBUTTON		m_thbButtons[TBB_LAST + 1];

	TBPFLAG			m_currentTBP_state;
	float			m_prevProgress;
	HICON			m_ovlIcon;
#endif

	struct SBulkOperationProgressState
	{
		SBulkOperationProgressState();
		bool bActive;
		bool bCanCancel;
		bool bHasAdd;
		bool bHasDelete;
		bool bHasUpdate;
		bool bListUpdateAfterCompletion;
		bool bHashing;
		UINT uDone;
		UINT uTotal;
	};

	SBulkOperationProgressState m_bulkOperationProgressStates[BulkOperationProgressCount];

	bool bPrevKadState;
	bool bPrevEd2kState;
	CRect m_rcTitleVersionLink;
	CFont m_fontTitleVersionLink;
	CFont m_fontIpGuardOverlay;
	UINT m_uTitleVersionAnimationHue;
	bool m_bTitleVersionAnimationTimerActive;
	bool m_bCloseAfterBulkOperations;
	CTitleVersionOverlayWnd* m_pTitleVersionOverlay;
	CStartupLoadingDlg* m_pStartupLoadingDlg;
	CShutdownProgressDlg* m_pShutdownProgressDlg;
	bool m_bStartupLoadingMainWindowDeferred;
	bool m_bStartupLoadingSuppressMainWindow;
	bool m_bStartupLoadingExitRequested;
	WINDOWPLACEMENT m_wpStartupLoadingRestorePlacement;
	void* m_pPendingStartupDownloadsLoadResult;
	void* m_pPendingStartupKnownFilesLoadResult;
	void* m_pPendingStartupClientHistoryLoadResult;
	void* m_pPendingStartupStoredSearchesLoadResult;
	bool m_bStartupSearchKnownTypesRefreshed;
	bool m_bStartupSearchKnownTypesRefreshQueued;
	bool m_bStartupSearchKnownTypesReloadPending;
	UINT m_uStartupApplyPumpNextDomain;
	bool m_bStartupApplyPumpTimerActive;
	bool m_bStartupApplyPumpPostPending;
	bool m_bStartupApplyPumpRunning;
	volatile LONG m_lStartupOverlayRefreshPending;
	DWORD m_dwLastStartupOverlayBulkRefreshTick;

	enum EQueuedUiLogTarget
	{
		QueuedUiLogTargetNone = 0,
		QueuedUiLogTargetLog,
		QueuedUiLogTargetDebug,
		QueuedUiLogTargetLeecher
	};

	struct SQueuedUiLogLine
	{
		SQueuedUiLogLine()
			: iLineLen(0)
			, uFlags(0)
			, eTarget(QueuedUiLogTargetNone)
			, bStatusBar(false)
			, bNotify(false)
		{
		}

		CString strPlainText;
		CString strFormattedLine;
		int iLineLen;
		UINT uFlags;
		EQueuedUiLogTarget eTarget;
		bool bStatusBar;
		bool bNotify;
	};

	std::deque<SQueuedUiLogLine> m_queuedUiLogLines;
	size_t m_uDroppedQueuedUiLogLines;
	DWORD m_dwLastUiLogBacklogTrace;
	DWORD m_dwUiLogFlushDeferredUntil;
	bool m_bUiLogFlushTimerActive;
	bool m_bUiLogFlushMessagePending;
	HANDLE m_hIpGuardInterfaceNotification;
	HANDLE m_hIpGuardAddressNotification;
	bool m_bIpGuardStartupBlocked;
	bool m_bIpGuardNetworkBlockActive;
	bool m_bVpnGuardNetworkBlockActive;
	bool m_bIpGuardMonitorActive;
	bool m_bVpnGuardMonitorActive;
	bool m_bIpGuardStartupProbePending;
	bool m_bIpGuardStartupApproved;
	bool m_bIpGuardRuntimeProbePending;
	bool m_bVpnGuardProbePending;
	bool m_bVpnGuardStartupApproved;
	int m_iVpnGuardPendingProbeCount;
	bool m_bVpnGuardProbeHadSuccess;
	bool m_bIpGuardRestoreEd2kConnection;
	bool m_bIpGuardRestoreKadConnection;
	uint32_t m_uIpGuardProbeGeneration;
	DWORD m_dwLastIpGuardRuntimeProbeTick;
	DWORD m_dwLastVpnGuardRuntimeProbeTick;
	CString m_strIpGuardStartupBlockReason;
	CString m_strIpGuardOverlayText;
	CString m_strIpGuardBlockReason;
	CString m_strIpGuardBlockOverlayText;
	CString m_strVpnGuardBlockReason;
	CString m_strVpnGuardBlockOverlayText;
	CStringA m_strVpnGuardLastPublicAddress;
	SIpGuardPublicIpProbeResult m_vpnGuardLastFailureResult;
	CString m_strIpGuardExpectedBindAddress;
	CString m_strIpGuardExpectedBindTarget;
	LONG m_lSharedFileListFoundFilesPendingMessage;
	LONG m_lSharedFilesCtrlUpdatePendingMessage;
	CCriticalSection m_sharedFilesCtrlUpdateLock;
	std::set<CKnownFile*> m_pendingSharedFilesCtrlUpdateFiles;
	LONG m_lFileOpProgressPendingMessage;
	CCriticalSection m_fileOpProgressLock;
	struct SPartFileOpProgressKey
	{
		bool operator<(const SPartFileOpProgressKey& other) const;

		CPartFile* pPartFile;
		DWORD dwRuntimeID;
		uchar abyFileHash[16];
	};
	std::map<CKnownFile*, WPARAM> m_pendingFileOpProgress;
	std::map<SPartFileOpProgressKey, WPARAM> m_pendingPartFileOpProgress;

	// Splash screen
	DWORD m_dwSplashTime;
	void ShowSplash(bool bAutoClose = true, CSplashScreen::DisplayMode eDisplayMode = CSplashScreen::DisplayModeSplash);
	void ShowSpecialThanks();
	void DestroySplash();

	// Mini Mule
	CMiniMule	*m_pMiniMule;
	void DestroyMiniMule();

	CMap<UINT, UINT, LPCTSTR, LPCTSTR> m_mapTbarCmdToIcon;
	void CreateToolbarCmdIconMap();
	LPCTSTR GetIconFromCmdId(UINT uId);

	// Main timer reused for startup and scheduled maintenance.
	UINT_PTR m_hTimer;
	static void CALLBACK StartupTimer(HWND hwnd, UINT uiMsg, UINT_PTR idEvent, DWORD dwTime) noexcept;
	static void CALLBACK MainTimer(HWND hwnd, UINT uiMsg, UINT_PTR idEvent, DWORD dwTime) noexcept;

	// UPnP TimeOutTimer
	UINT_PTR m_hUPnPTimeOutTimer;
	static void CALLBACK UPnPTimeOutTimer(HWND hwnd, UINT uiMsg, UINT_PTR idEvent, DWORD dwTime) noexcept;
	
	void StartConnection();
	void StartConnection(bool bUserInitiated);
	void AutoConnectIfNeeded();
	void CloseConnection();
	CString FormatBindResolveFailure(ENetBindResolveResult eResult, const CString& strInterfaceName, const CString& strAddress) const;
	bool ShouldBlockNetworkingForIpGuard(CString& strReason) const;
	void RegisterIpGuardNotifications();
	void UnregisterIpGuardNotifications();
	bool IsIpGuardMonitorConfigured(CString& strReason);
	void CheckIpGuardRuntimeBind();
	void CheckIpGuardPublicIpMonitor(bool bForce);
	bool StartIpGuardPublicIpProbe(const CString& strPurpose);
	void HandleIpGuardPublicIpProbeResult(const SIpGuardPublicIpProbeResult& result);
	CString FormatIpGuardPublicIpMessage(bool bRuntime, const SIpGuardPublicIpProbeResult& result) const;
	void ApplyVpnGuardNetworkBlock(const CString& strReason, const CString& strOverlayText = CString());
	void ApplySessionNetworkBlock(const CString& strReason, const CString& strOverlayText);
	void RefreshSessionNetworkBlock(bool bRestartLocalSockets);
	bool GetRequestedSessionNetworkBlock(CString& strReason, CString& strOverlayText) const;
	void CheckVpnGuardPublicIpMonitor(bool bForce);
	bool StartVpnGuardPublicIpProbe(const CString& strPurpose);
	void HandleVpnGuardPublicIpProbeResult(const SIpGuardPublicIpProbeResult& result);
	void ApproveVpnGuardPublicIpProbe();
	CString FormatVpnGuardPublicIpMessage(const SIpGuardPublicIpProbeResult& result, const CString& strCountryCode, const CString& strCountryName, bool bCountryUnknown) const;
	CString GetVpnGuardCountryName(const CString& strCountryCode) const;
	void MinimizeWindow();
	void PostStartupMinimized();
	void UpdateTrayIcon(int iPercent);
	void ShowConnectionStateIcon();
	void ShowTransferStateIcon();
	void ShowUserStateIcon();
	void AddSpeedSelectorMenus(CMenu *addToMenu);
	int  GetRecMaxUpload();
	void LoadNotifier(const CString &configuration);
	void ClearTrayBalloonNotificationPayload();
	bool notifierenabled;
	bool m_bNotifierRuntimeActive;
	TbnMsg m_nTrayBalloonMsgType;
	CString m_strTrayBalloonLink;
	void ShowToolPopup(bool toolsonly = false);
	void ShowEmuleAIPopup();
	void SetAllIcons();
	void ApplyMainWindowIcons();
	bool CanClose();
	bool GetActiveBulkOperationCloseInfo(CString& strTitle, CString& strBody, CString& strCancelAndExit, CString& strWaitAndExit, UINT& uDone, UINT& uTotal, bool* pbCanCancel = NULL) const;
	void SetActiveBulkOperationOverlaysSuppressed(bool bSuppress);
	void ScheduleUiLogFlush();
	void QueueUiLogLine(const SQueuedUiLogLine& line);
	bool TryAppendQueuedUiLogLine(const SQueuedUiLogLine& line);
	void ScheduleStartupApplyPump();
	void ClearStartupApplyPumpState();
	bool HasPendingStartupApplyWork() const;
	bool ProcessStartupApplyPump();
	bool ProcessStartupDownloadsApplySlice();
	bool ProcessStartupKnownFilesApplySlice();
	bool ProcessStartupClientHistoryApplySlice();
	bool ProcessStartupStoredSearchesApplySlice();
	void TryRefreshStartupSearchKnownTypes();
	void RefreshSearchResultsAfterStartupKnownTypes();
	int ConfirmCloseWithActiveBulkOperations(const CString& strTitle, const CString& strBody, const CString& strCancelAndExit, const CString& strWaitAndExit);
	void ScheduleCloseAfterBulkOperations();
	int MapWindowToToolbarButton(CWnd *pWnd) const;
	CWnd* MapToolbarButtonToWindow(int iButtonID) const;
	int GetNextWindowToolbarButton(int iButtonID, int iDirection = 1) const;
	bool IsWindowToolbarButton(int iButtonID) const;
	void SetTaskbarIconColor();

	virtual void DoDataExchange(CDataExchange *pDX);
	virtual BOOL OnInitDialog();
	virtual void OnCancel();
	virtual void OnOK();
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
	virtual BOOL PreTranslateMessage(MSG *pMsg);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnMoving(UINT fwSide, LPRECT pRect);
	afx_msg void OnSizing(UINT fwSide, LPRECT pRect);
	afx_msg void OnWindowPosChanging(WINDOWPOS* lpwndpos);
	afx_msg void OnWindowPosChanged(WINDOWPOS* lpwndpos);
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnBnClickedConnect();
	afx_msg void OnCommandConnect();
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnBnClickedHotmenu();
	afx_msg LRESULT OnMenuChar(UINT nChar, UINT nFlags, CMenu *pMenu);
	afx_msg void OnSysColorChange();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnSettingChange(UINT uFlags, LPCTSTR lpszSection);
	afx_msg BOOL OnDeviceChange(UINT nEventType, DWORD_PTR dwData);
	afx_msg BOOL OnQueryEndSession();
	afx_msg void OnEndSession(BOOL bEnding);
	afx_msg LRESULT OnUserChanged(WPARAM, LPARAM);
	afx_msg LRESULT OnKickIdle(WPARAM, LPARAM lIdleCount);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg BOOL OnChevronPushed(UINT id, LPNMHDR pNMHDR, LRESULT *plResult);
	afx_msg LRESULT OnPowerBroadcast(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnDisplayChange(WPARAM, LPARAM);
	afx_msg LRESULT OnConChecker(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnPostInitControls(WPARAM wParam, LPARAM lParam);

	// quick-speed changer -- based on xrmb
	afx_msg void QuickSpeedUpload(UINT nID);
	afx_msg void QuickSpeedDownload(UINT nID);
	afx_msg void QuickSpeedOther(UINT nID);
	// end of quick-speed changer

	afx_msg LRESULT OnTaskbarNotifierClicked(WPARAM, LPARAM lParam);
	afx_msg LRESULT OnToastNotificationClicked(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnStartupLoadingCancelExit(WPARAM, LPARAM);
	afx_msg LRESULT OnBindAddressChanged(WPARAM, LPARAM);
	afx_msg LRESULT OnIpGuardProbeResult(WPARAM, LPARAM);
	afx_msg LRESULT OnWMData(WPARAM, LPARAM lParam);
	afx_msg LRESULT OnFileHashed(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnPartFileHashed(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnHashFailed(WPARAM, LPARAM lParam);
	afx_msg LRESULT OnPartFileHashFailed(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnFileAllocExc(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnFileCompleted(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnFileOpProgress(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnImportPart(WPARAM wParam,LPARAM lParam);
	afx_msg LRESULT OnImportPartProgress(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnImportPartFinished(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnFinalizeDeletePendingClient(WPARAM wParam, LPARAM lParam);

	//Frame grabbing
	afx_msg LRESULT OnFrameGrabFinished(WPARAM wParam, LPARAM lParam);

	afx_msg LRESULT OnAreYouEmule(WPARAM, LPARAM);

#ifdef HAVE_WIN7_SDK_H
	afx_msg LRESULT OnTaskbarBtnCreated(WPARAM, LPARAM);
#endif

	//Web Interface
	afx_msg LRESULT OnWebGUIInteraction(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnWebGetSearchResults(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnWebServerCommand(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnWebGetTransferSnapshot(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnWebGetSharedFilesSnapshot(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnWebGetServerListSnapshot(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnWebGetHeaderSnapshot(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnWebGetCommentList(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnWebFriendCommand(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnEmuleAIVersionCheckResult(WPARAM, LPARAM lParam);
	afx_msg LRESULT OnProcessChunkedDownloads(WPARAM, LPARAM);
	afx_msg LRESULT OnProcessChunkedDownloadParse(WPARAM, LPARAM);
	afx_msg LRESULT OnProcessChunkedSearchIngest(WPARAM, LPARAM);
	afx_msg LRESULT OnCollectionImportReady(WPARAM, LPARAM);
	afx_msg LRESULT OnStartupDownloadsLoadReady(WPARAM, LPARAM);
	afx_msg LRESULT OnStartupKnownFilesLoadReady(WPARAM, LPARAM);
	afx_msg LRESULT OnStartupClientHistoryLoadReady(WPARAM, LPARAM);
	afx_msg LRESULT OnStartupStoredSearchesLoadReady(WPARAM, LPARAM);
	afx_msg LRESULT OnStartupOverlayRefresh(WPARAM, LPARAM);
	afx_msg LRESULT OnStartupApplyPump(WPARAM, LPARAM);
	afx_msg LRESULT OnIPFilterDownloadProgress(WPARAM, LPARAM lParam);
	afx_msg LRESULT OnIPFilterDownloadFinished(WPARAM, LPARAM lParam);
	afx_msg LRESULT OnIPGeolocationDownloadProgress(WPARAM, LPARAM lParam);
	afx_msg LRESULT OnIPGeolocationDownloadFinished(WPARAM, LPARAM lParam);
	afx_msg LRESULT OnFlushUiLog(WPARAM, LPARAM);
	afx_msg LRESULT OnDispatchApplicationEvent(WPARAM, LPARAM);
	afx_msg LRESULT OnProcessBackendCommands(WPARAM, LPARAM);
	// MiniMule
	afx_msg LRESULT OnCloseMiniMule(WPARAM wParam, LPARAM);
	// Terminal Services
	afx_msg LRESULT OnConsoleThreadEvent(WPARAM wParam, LPARAM lParam);
	// UPnP
	afx_msg LRESULT OnUPnPResult(WPARAM wParam, LPARAM lParam);

	afx_msg LRESULT OnSharedFileListFoundFiles(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnSharedFilesCtrlUpdateFile(WPARAM wParam, LPARAM lParam);

	afx_msg LRESULT OnDarkModeSwitch(WPARAM wParam, LPARAM lParam);
	afx_msg void OnRebarCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnNcPaint();
	afx_msg void OnNcLButtonDown(UINT nHitTest, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void OnTimer(UINT_PTR nIDEvent);

	static UINT AFX_CDECL EmuleAIVersionCheckThread(LPVOID pParam);
	void EnsureTitleVersionLinkFont();
	void EnsureIpGuardOverlayFont();
	CString GetTitleVersionLinkText() const;
	bool TryBuildTitleVersionLinkRect(CDC& dc, const CRect& rcWindow, CRect& rcOut);
	void UpdateTitleVersionLinkRect(CDC& dc);
	void DrawTitleVersionLink(CDC& dc);
	COLORREF GetRainbowColor(int nHueDegrees) const;
	bool IsTitleVersionLinkVisible() const;
	void InvalidateTitleVersionFrame();
	void PaintTitleVersionLinkNow();
	void DrawTitleVersionOverlay(CDC& dc, const CRect& rcClient);
	void EnsureTitleVersionOverlayWindow();
	void DestroyTitleVersionOverlayWindow();
	void HideTitleVersionOverlayWindow();
	void ApplyTitleVersionOverlayRect();
	void UpdateTitleVersionOverlayWindowForRect(const CRect& rcWindow);
	void UpdateTitleVersionOverlayWindow();
	void StartTitleVersionAnimation();
	void StopTitleVersionAnimation();
};
#ifdef _DEBUG
///////////////////////////////////////////////////////////////////////////////
// Suppress null document warning in Output (CFrameWnd::Create)
//
class CFrameDoc : public CDocument
{
public:
	CFrameDoc() = default;
	BOOL OnNewDocument() { return CDocument::OnNewDocument(); }
};
#endif

enum EEMuleAppMsgs
{
	//thread messages
	TM_FINISHEDHASHING = WM_APP + 10,
	TM_HASHFAILED,
	TM_IMPORTPART,
	TM_IMPORTPARTPROGRESS,
	TM_IMPORTPARTFINISHED,
	TM_FRAMEGRABFINISHED,
	TM_FILEALLOCEXC,
	TM_FILECOMPLETED,
	TM_FILEOPPROGRESS,
	TM_CONSOLETHREADEVENT,
	TM_SHAREDFILELISTFOUNDFILES,
	TM_SHAREDFILESCTRLUPDATEFILE,
	TM_FINISHEDPARTFILEHASHING,
	TM_PARTFILEHASHFAILED
};

enum EWebinterfaceOrders
{
	WEBGUIIA_UPDATEMYINFO = 1,
	WEBGUIIA_WINFUNC,
	WEBGUIIA_UPD_CATTABS,
	WEBGUIIA_UPD_SFUPDATE,
	WEBGUIIA_UPDATESERVER,
	WEBGUIIA_STOPCONNECTING,
	WEBGUIIA_CONNECTTOSERVER,
	WEBGUIIA_DISCONNECT,
	WEBGUIIA_SERVER_REMOVE,
	WEBGUIIA_SHARED_FILES_RELOAD,
	WEBGUIIA_ADD_TO_STATIC,
	WEBGUIIA_REMOVE_FROM_STATIC,
	WEBGUIIA_UPDATESERVERMETFROMURL,
	WEBGUIIA_SHOWSTATISTICS,
	WEBGUIIA_DELETEALLSEARCHES,
	WEBGUIIA_KAD_BOOTSTRAP,
	WEBGUIIA_KAD_START,
	WEBGUIIA_KAD_STOP,
	WEBGUIIA_KAD_RCFW
};
