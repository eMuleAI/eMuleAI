//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#pragma once
#include "TreeOptionsCtrlEx.h"

class CPPgMod : public CPropertyPage
{
	DECLARE_DYNAMIC(CPPgMod)

	enum { IDD = IDD_PPG_MOD }; // Dialog Data

	void LocalizeItemText(HTREEITEM item, LPCTSTR strid);
	void LocalizeItemInfoText(HTREEITEM item, LPCTSTR strid);
	void LocalizeEditLabel(HTREEITEM item, LPCTSTR strid);

public:
	CPPgMod();
	virtual	~CPPgMod() = default;
	void Localize();

protected:
	CTreeOptionsCtrlEx m_ctrlTreeOptions;
	bool m_bInitializedTreeOpts;

	virtual void DoDataExchange(CDataExchange *pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL OnApply();
	virtual BOOL OnKillActive();
	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnDestroy();
	afx_msg LRESULT OnTreeOptsCtrlNotify(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT DrawTreeItemHelp(WPARAM wParam, LPARAM lParam);
	afx_msg void OnHelp();
	afx_msg BOOL OnHelpInfo(HELPINFO*);
	afx_msg void OnSettingsChange() { SetModified(); }
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);

	HTREEITEM m_htiUITweaks;
	HTREEITEM m_htiUIDarkMode;
	HTREEITEM m_htiUIDarkModeAuto;
	HTREEITEM m_htiUIDarkModeOn;
	HTREEITEM m_htiUIDarkModeOff;
	HTREEITEM m_htiUITweaksSpeedGraph;
	HTREEITEM m_htiShowDownloadCommandsToolbar;
	HTREEITEM m_htiDisableFindAsYouType;
	HTREEITEM m_htiUITweaksListUpdatePeriod;
	HTREEITEM m_htiUITweaksMaxSortHistory;
	HTREEITEM m_htiUITweaksServerMaxSortHistory;
	HTREEITEM m_htiUITweaksSearchMaxSortHistory;
	HTREEITEM m_htiUITweaksFilesMaxSortHistory;
	HTREEITEM m_htiUITweaksDownloadMaxSortHistory;
	HTREEITEM m_htiUITweaksDownloadClientsMaxSortHistory;
	HTREEITEM m_htiUITweaksUploadMaxSortHistory;
	HTREEITEM m_htiUITweaksQueueMaxSortHistory;
	HTREEITEM m_htiUITweaksClientMaxSortHistory;
	HTREEITEM m_htiUITweaksKadContactSortHistory;
	HTREEITEM m_htiUITweaksKadSearchSortHistory;
	int m_iUIDarkMode;
	int m_iUITweaksServerListMaxSortHistory;
	bool m_bUITweaksSpeedGraph;
	bool m_bShowDownloadCommandsToolbar;
	bool m_bDisableFindAsYouType;
	int m_iUITweaksListUpdatePeriod;
	int m_iUITweaksSearchMaxSortHistory;
	int m_iUITweaksFilesMaxSortHistory;
	int m_iUITweaksDownloadMaxSortHistory;
	int m_iUITweaksDownloadClientsMaxSortHistory;
	int m_iUITweaksUploadMaxSortHistory;
	int m_iUITweaksQueueMaxSortHistory;
	int m_iUITweaksClientMaxSortHistory;
	int m_iUITweaksKadContactSortHistory;
	int m_iUITweaksKadSearchSortHistory;

	HTREEITEM m_htiGeoLite2;
	HTREEITEM m_htiGeoLite2NameDisable;
	HTREEITEM m_htiGeoLite2CountryCode;
	HTREEITEM m_htiGeoLite2Country;
	HTREEITEM m_htiGeoLite2CountryCity;
	HTREEITEM m_htiGeoLite2ShowFlag;
	int		m_iGeoLite2Mode;
	bool	m_bGeoLite2ShowFlag;

	HTREEITEM m_htiConTweaks;

	HTREEITEM	m_htiConnectionChecker;
	HTREEITEM	m_htiConnectionCheckerActivate;
	HTREEITEM	m_htiConnectionCheckerServer;
	bool		m_bConnectionChecker;
	CString		m_sConnectionCheckerServer;

	HTREEITEM m_htiEnableNatTraversal;
	bool m_bEnableNatTraversal;
	HTREEITEM m_htiNatTraversalPortWindow;
	HTREEITEM m_htiNatTraversalSweepWindow;
	HTREEITEM m_htiNatTraversalJitterMinMs;
	HTREEITEM m_htiNatTraversalJitterMaxMs;
	int m_iNatTraversalPortWindow;
	int m_iNatTraversalSweepWindow;
	int m_iNatTraversalJitterMinMs;
	int m_iNatTraversalJitterMaxMs;

	HTREEITEM m_htiMaxServedBuddies;
	int m_iMaxServedBuddies;

	HTREEITEM m_htiMaxEServerBuddySlots;
	int m_iMaxEServerBuddySlots;

	HTREEITEM m_htiRetryFailedTcpConnectionAttempts;
	bool m_bRetryFailedTcpConnectionAttempts;

	HTREEITEM m_htiIsreaskSourceAfterIPChange;
	HTREEITEM m_htiInformQueuedClientsAfterIPChange;
	bool m_bIsreaskSourceAfterIPChange;
	bool m_bInformQueuedClientsAfterIPChange;

	HTREEITEM m_htiReAskFileSrc;
	int m_iReAskFileSrc;

	HTREEITEM m_htiDownloadChecker;
	HTREEITEM m_htiDownloadCheckerPassive;
	HTREEITEM m_htiDownloadCheckerAlwaysAsk;
	HTREEITEM m_htiDownloadCheckerReject;
	HTREEITEM m_htiDownloadCheckerAccept;
	HTREEITEM m_htiDownloadCheckerAcceptPercentage;
	HTREEITEM m_htiDownloadCheckerRejectCanceled;
	HTREEITEM m_htiDownloadCheckerRejectSameHash;
	HTREEITEM m_htiDownloadCheckerRejectBlacklisted;
	HTREEITEM m_htiDownloadCheckerCaseInsensitive;
	HTREEITEM m_htiDownloadCheckerIgnoreExtension;
	HTREEITEM m_htiDownloadCheckerIgnoreTags;
	HTREEITEM m_htiDownloadCheckerDontIgnoreNumericTags;
	HTREEITEM m_htiDownloadCheckerIgnoreNonAlphaNumeric;
	HTREEITEM m_htiDownloadCheckerMinimumComparisonLength;
	HTREEITEM m_htiDownloadCheckerSkipIncompleteFileConfirmation;
	HTREEITEM m_htiDownloadCheckerMarkAsBlacklisted;
	HTREEITEM m_htiDownloadCheckerAutoMarkAsBlacklisted;
	int m_iDownloadChecker;
	int m_iDownloadCheckerAcceptPercentage;
	bool m_bDownloadCheckerRejectCanceled;
	bool m_bDownloadCheckerRejectSameHash;
	bool m_bDownloadCheckerRejectBlacklisted;
	bool m_bDownloadCheckerCaseInsensitive;
	bool m_bDownloadCheckerIgnoreExtension;
	bool m_bDownloadCheckerIgnoreTags;
	bool m_bDownloadCheckerDontIgnoreNumericTags;
	bool m_bDownloadCheckerIgnoreNonAlphaNumeric;
	int	 m_iDownloadCheckerMinimumComparisonLength;
	bool m_bDownloadCheckerSkipIncompleteFileConfirmation;
	bool m_bDownloadCheckerMarkAsBlacklisted;
	bool m_bDownloadCheckerAutoMarkAsBlacklisted;

	HTREEITEM m_htiDownloadInspector;
	HTREEITEM m_htiDownloadInspectorDisable;
	HTREEITEM m_htiDownloadInspectorLogOnly;
	HTREEITEM m_htiDownloadInspectorDelete;
	HTREEITEM m_htiDownloadInspectorFake;
	HTREEITEM m_htiDownloadInspectorDRM;
	HTREEITEM m_htiDownloadInspectorCompressionThresholdToBypassZero;
	HTREEITEM m_htiDownloadInspectorCheckPeriod;
	HTREEITEM m_htiDownloadInspectorCompletedThreshold;
	HTREEITEM m_htiDownloadInspectorZeroPercentageThreshold;
	HTREEITEM m_htiDownloadInspectorCompressionThreshold;
	HTREEITEM m_htiDownloadInspectorBypassZeroPercentage;
	HTREEITEM m_htiDownloadInspectorInvalidExt;
	int m_iDownloadInspector;
	bool m_bDownloadInspectorFake;
	bool m_bDownloadInspectorDRM;
	bool m_bDownloadInspectorInvalidExt;
	int m_iDownloadInspectorCheckPeriod;
	int m_iDownloadInspectorCompletedThreshold;
	int m_iDownloadInspectorZeroPercentageThreshold;
	int m_iDownloadInspectorCompressionThreshold;
	bool m_bDownloadInspectorBypassZeroPercentage;
	int m_iDownloadInspectorCompressionThresholdToBypassZero;

	HTREEITEM m_htiSearchTweaksGroup;
	HTREEITEM m_htiGroupKnownAtTheBottom;
	bool	  m_bGroupKnownAtTheBottom;
	HTREEITEM m_htiSpamThreshold;
	int		  m_iSpamThreshold;
	HTREEITEM m_htiKadSearchKeywordTotal;
	int		  m_iKadSearchKeywordTotal;
	HTREEITEM m_htiShowCloseButtonOnSearchTabs;
	bool	  m_bShowCloseButtonOnSearchTabs;

	HTREEITEM	m_htiServerTweaksGroup; 
	HTREEITEM	m_htiRepeatServerList;
	bool		m_bRepeatServerList;
	HTREEITEM	m_htiDontRemoveStaticServers;
	bool		m_bDontRemoveStaticServers;
	HTREEITEM	m_htiDontSavePartOnReconnect;
	bool		m_bDontSavePartOnReconnect;

	HTREEITEM m_htiFileHistory;
	HTREEITEM m_htiFileHistoryShowPart;
	HTREEITEM m_htiFileHistoryShowShared;
	HTREEITEM m_htiFileHistoryShowDuplicate;
	HTREEITEM m_htiAutoShareSubdirs;
	bool m_bFileHistoryShowPart;
	bool m_bFileHistoryShowShared;
	bool m_bFileHistoryShowDuplicate;
	bool m_bAutoShareSubdirs;
	HTREEITEM m_htiDontShareExtensions;
	HTREEITEM m_htiDontShareExtensionsList;
	bool m_bDontShareExtensions;
	CString m_sDontShareExtensionsList;
	HTREEITEM m_htiAdjustNTFSDaylightFileTime;
	bool m_bAdjustNTFSDaylightFileTime; // Official preference
	HTREEITEM m_htiAllowDSTTimeTolerance;
	bool m_bAllowDSTTimeTolerance;

	HTREEITEM m_htiEmulator;
	HTREEITEM m_htiEmulateMLDonkey;
	HTREEITEM m_htiEmulateEdonkey;
	HTREEITEM m_htiEmulateEdonkeyHybrid;
	HTREEITEM m_htiEmulateShareaza;
	HTREEITEM m_htiEmulateLphant;
	HTREEITEM m_htiEmulateCommunity;
	HTREEITEM m_htiEmulateCommunityTagSavingTreshold;
	HTREEITEM m_htiLogEmulator;
	bool m_bEmulateMLDonkey;
	bool m_bEmulateEdonkey;
	bool m_bEmulateEdonkeyHybrid;
	bool m_bEmulateShareaza;
	bool m_bEmulateLphant;
	bool m_bEmulateCommunity;
	int m_iEmulateCommunityTagSavingTreshold;
	bool m_bLogEmulator;

	HTREEITEM m_htiUseIntelligentChunkSelection;
	bool m_bUseIntelligentChunkSelection;

	HTREEITEM m_htiCreditSystem;
	HTREEITEM m_htiOfficialCredit;
	HTREEITEM m_htiLovelaceCredit;
	HTREEITEM m_htiRatioCredit;
	HTREEITEM m_htiPawcioCredit;
	HTREEITEM m_htiESCredit;
	HTREEITEM m_htiMagicAngelCredit;
	HTREEITEM m_htiMagicAngelPlusCredit;
	HTREEITEM m_htiSivkaCredit;
	HTREEITEM m_htiSwatCredit;
	HTREEITEM m_htiTk4Credit;
	HTREEITEM m_htiXtremeCredit;
	HTREEITEM m_htiZzulCredit;
	int m_iCreditSystem;

	HTREEITEM m_htiClientHistory;
	HTREEITEM m_htiClientHistoryActivate;
	HTREEITEM m_htiClientHistoryExpDays;
	HTREEITEM m_htiClientHistoryLog;
	bool m_bClientHistory;
	int m_iClientHistoryExpDays;
	bool m_bClientHistoryLog;

	HTREEITEM m_htiRemoteSharedFiles;
	HTREEITEM m_htiRemoteSharedFilesUserHash;
	HTREEITEM m_htiRemoteSharedFilesClientNote;
	HTREEITEM m_htiRemoteSharedFilesAutoQueryPeriod;
	HTREEITEM m_htiRemoteSharedFilesAutoQueryMaxClients;
	HTREEITEM m_htiRemoteSharedFilesAutoQueryClientPeriod;
	HTREEITEM m_htiRemoteSharedFilesSetAutoQueryDownload;
	HTREEITEM m_htiRemoteSharedFilesSetAutoQueryDownloadThreshold;
	HTREEITEM m_htiRemoteSharedFilesSetAutoQueryUpload;
	HTREEITEM m_htiRemoteSharedFilesSetAutoQueryUploadThreshold;
	bool m_bRemoteSharedFilesUserHash;
	bool m_bRemoteSharedFilesClientNote;
	int m_iRemoteSharedFilesAutoQueryPeriod;
	int m_iRemoteSharedFilesAutoQueryMaxClients;
	int m_iRemoteSharedFilesAutoQueryClientPeriod;
	bool m_bRemoteSharedFilesSetAutoQueryDownload;
	int m_iRemoteSharedFilesSetAutoQueryDownloadThreshold;
	bool m_bRemoteSharedFilesSetAutoQueryUpload;
	int m_iRemoteSharedFilesSetAutoQueryUploadThreshold;

	HTREEITEM m_htiSaveLoadSources;
	HTREEITEM m_htiSaveLoadSourcesActivate;
	HTREEITEM m_htiSaveLoadSourcesMaxSources;
	HTREEITEM m_htiSaveLoadSourcesExpirationDays;
	bool m_bSaveLoadSources;
	int m_iSaveLoadSourcesMaxSources;
	int m_iSaveLoadSourcesExpirationDays;

	HTREEITEM m_htiMetControl;
	HTREEITEM m_htiDontPurge;
	HTREEITEM m_htiPartiallyPurge;
	HTREEITEM m_htiCompletelyPurge;
	HTREEITEM m_htiKnownMet;
	HTREEITEM m_htiRemoveAichImmediately;
	HTREEITEM m_htiClientsExp;
	int m_iPurgeMode;
	int m_iKnownMetDays;
	bool m_bRemoveAichImmediately;
	int m_iClientsExpDays;

	HTREEITEM m_htiBackup;
	HTREEITEM m_htiBackupOnExit;
	HTREEITEM m_htiBackupPeriodic;
	HTREEITEM m_htiBackupPeriod;
	HTREEITEM m_htiBackupMax;
	HTREEITEM m_htiBackupCompressed;
	bool m_bBackupOnExit;
	bool m_bBackupPeriodic;
	int m_iBackupPeriod;
	int m_iBackupMax;
	bool m_bBackupCompressed;

	HTREEITEM m_htiAdvancedPreferences;
	HTREEITEM m_htiAllowedIPs;
	HTREEITEM m_htiBeeper;
	HTREEITEM m_htiCreateCrashDump;
	HTREEITEM m_htiCryptTCPPaddingLength;
	HTREEITEM m_htiDateTimeFormat;
	HTREEITEM m_htiDateTimeFormat4Log;
	HTREEITEM m_htiDateTimeFormat4list;
	HTREEITEM m_htiDebugSearchResultDetailLevel;
	HTREEITEM m_htiDisplay;
	HTREEITEM m_htiDontCompressAvi;
	HTREEITEM m_htiForceSpeedsToKB;
	HTREEITEM m_htiHighresTimer;
	HTREEITEM m_htiICH;
	HTREEITEM m_htiIconflashOnNewMessage;
	HTREEITEM m_htiIgnoreInstances;
	HTREEITEM m_htiInternetSecurityZone;
	HTREEITEM m_htiKeepUnavailableFixedSharedDirs;
	HTREEITEM m_htiLog;
	HTREEITEM m_htiLogFileFormat;
	HTREEITEM m_htiMaxChatHistory;
	HTREEITEM m_htiMaxLogBuff;
	HTREEITEM m_htiMaxMsgSessions;
	HTREEITEM m_htiMiniMule;
	HTREEITEM m_htiMiniMuleAutoClose;
	HTREEITEM m_htiMiniMuleTransparency;
	HTREEITEM m_htiMsgOnlySec;
	HTREEITEM m_htiNotifierMailEncryptCertName;
	HTREEITEM m_htiPreferRestrictedOverUser;
	HTREEITEM m_htiPreviewCopiedArchives;
	HTREEITEM m_htiPreviewOnIconDblClk;
	HTREEITEM m_htiPreviewSmallBlocks;
	HTREEITEM m_htiRTLWindowsLayout;
	HTREEITEM m_htiReBarToolbar;
	HTREEITEM m_htiRearrangeKadSearchKeywords;
	HTREEITEM m_htiRemoveFilesToBin;
	HTREEITEM m_htiRepaint;
	HTREEITEM m_htiRestoreLastLogPane;
	HTREEITEM m_htiRestoreLastMainWndDlg;
	HTREEITEM m_htiServerUDPPort;
	HTREEITEM m_htiShowCopyEd2kLinkCmd;
	HTREEITEM m_htiShowUpDownIconInTaskbar;
	HTREEITEM m_htiShowVerticalHourMarkers;
	HTREEITEM m_htiStraightWindowStyles;
	HTREEITEM m_htiShowActiveDownloadsBold;
	HTREEITEM m_htiTrustEveryHash;
	HTREEITEM m_htiTxtEditor;
	HTREEITEM m_htiUpdateQueue;
	HTREEITEM m_htiUseUserSortedServerList;
	HTREEITEM m_htiWebFileUploadSizeLimitMB;
	bool bMiniMuleAutoClose;
	int iMiniMuleTransparency;
	bool bCreateCrashDump;
	bool bIgnoreInstances;
	CString sNotifierMailEncryptCertName;
	int iMaxLogBuff;
	int m_iMaxChatHistory;
	int m_iPreviewSmallBlocks;
	bool m_bRestoreLastMainWndDlg;
	bool m_bRestoreLastLogPane;
	bool m_bPreviewCopiedArchives;
	int m_iStraightWindowStyles;
	bool m_bShowActiveDownloadsBold;
	int m_iLogFileFormat;
	bool m_bRTLWindowsLayout;
	bool m_bPreviewOnIconDblClk;
	CString sInternetSecurityZone;
	CString sTxtEditor;
	int iServerUDPPort; // really a unsigned int 16
	bool m_bRemoveFilesToBin;
	bool m_bHighresTimer;
	bool m_bTrustEveryHash;
	int  m_umaxmsgsessions;
	bool m_bPreferRestrictedOverUser;
	bool m_bUseUserSortedServerList;
	int m_iWebFileUploadSizeLimitMB;
	CString m_sAllowedIPs;
	int m_iDebugSearchResultDetailLevel;
	int m_iCryptTCPPaddingLength;
	CString m_strDateTimeFormat;
	CString m_strDateTimeFormat4Log;
	CString m_strDateTimeFormat4List;
	bool m_bShowVerticalHourMarkers;
	bool m_bReBarToolbar;
	bool m_bIconflashOnNewMessage;
	bool m_bShowCopyEd2kLinkCmd;
	bool m_dontcompressavi;
	bool m_ICH;
	bool m_bRearrangeKadSearchKeywords;
	bool m_bUpdateQueue;
	bool m_bRepaint;
public: //MORPH leuk_he:run as ntservice v1.. 
	bool m_bBeeper;
protected: //MORPH leuk_he:run as ntservice v1.. 
	bool m_bMsgOnlySec;
	bool m_bShowUpDownIconInTaskbar;
	bool m_bKeepUnavailableFixedSharedDirs;
	bool m_bForceSpeedsToKB;
	bool m_bExtControls;
};
