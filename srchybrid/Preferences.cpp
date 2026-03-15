//This file is part of eMule AI
//Copyright (C)2002-2026 Merkur ( devs@emule-project.net / https://www.emule-project.net )
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

#include "stdafx.h"
#include <io.h>
#include <share.h>
#include <iphlpapi.h>
#include "emule.h"
#include "Preferences.h"
#include "Opcodes.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "kademlia\kademlia\Prefs.h"
#include "UpDownClient.h"
#include "Ini2.h"
#include "DownloadQueue.h"
#include "UploadQueue.h"
#include "Statistics.h"
#include "MD5Sum.h"
#include "PartFile.h"
#include "ServerConnect.h"
#include "ListenSocket.h"
#include "ServerList.h"
#include "SharedFileList.h"
#include "SafeFile.h"
#include "emuledlg.h"
#include "StatisticsDlg.h"
#include "Log.h"
#include "MuleToolbarCtrl.h"
#include "VistaDefines.h"
#include "cryptopp/osrng.h"
#include "ClientCredits.h"
#include "TransferDlg.h"
#include "ClientList.h"
#include "kademlia\kademlia\Defines.h"
#include "PreferencesDlg.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define SHAREDDIRS _T("shareddir.dat")
LPCTSTR const strPreferencesDat = _T("preferences.dat");
LPCTSTR const strDefaultToolbar = _T("009901020304059907069911131299089909109914");

namespace
{
	const int DEFAULT_MAINWINDOW_WORKAREA_PERCENT = 95;
	const int DEFAULT_MAINWINDOW_ASPECT_WIDTH = 16;
	const int DEFAULT_MAINWINDOW_ASPECT_HEIGHT = 9;
	const int DEFAULT_MAINWINDOW_FALLBACK_LEFT = 10;
	const int DEFAULT_MAINWINDOW_FALLBACK_TOP = 10;
	const int DEFAULT_MAINWINDOW_FALLBACK_WIDTH = 1280;
	const int DEFAULT_MAINWINDOW_FALLBACK_HEIGHT = 720;
	const int DEFAULT_DOWNLOAD_CAPACITY_MBIT = 200;
	const int DEFAULT_UPLOAD_CAPACITY_MBIT = 50;
	const UINT DEFAULT_MIN_FREE_DISK_SPACE = 2000u * 1024u * 1024u;
	LPCTSTR const MISSING_INI_VALUE = _T("__missing__");

	uint32 MbitPerSecToKBytesPerSec(uint32 nMbitPerSec)
	{
		return (uint32)(((uint64)nMbitPerSec * 1000000ull + 4096ull) / 8192ull);
	}

	int KBytesPerSecToRoundedMbitPerSec(uint32 nKBytesPerSec)
	{
		return (int)(((uint64)nKBytesPerSec * 8192ull + 500000ull) / 1000000ull);
	}

	bool DoesExpandedPathExist(LPCTSTR pszPath)
	{
		CString strExpanded(pszPath);
		ExpandEnvironmentStrings(strExpanded);
		return ::PathFileExists(strExpanded);
	}

	CString GetDefaultVideoPlayerPath()
	{
		static LPCTSTR const s_apszVideoPlayers[] = {
			_T("%ProgramFiles%\\QMPlay2\\QMPlay2.exe"),
			_T("%ProgramFiles(x86)%\\QMPlay2\\QMPlay2.exe"),
			_T("%ProgramFiles%\\VideoLAN\\VLC\\vlc.exe"),
			_T("%ProgramFiles(x86)%\\VideoLAN\\VLC\\vlc.exe"),
			_T("%ProgramFiles%\\DAUM\\PotPlayer\\PotPlayerMini64.exe"),
			_T("%ProgramFiles(x86)%\\DAUM\\PotPlayer\\PotPlayerMini.exe")
		};

		for (LPCTSTR pszCandidate : s_apszVideoPlayers) {
			if (DoesExpandedPathExist(pszCandidate))
				return pszCandidate;
		}

		return EMPTY;
	}

	void SetFallbackMainWindowPlacement(WINDOWPLACEMENT& wp)
	{
		::SetRect(&wp.rcNormalPosition,
			DEFAULT_MAINWINDOW_FALLBACK_LEFT,
			DEFAULT_MAINWINDOW_FALLBACK_TOP,
			DEFAULT_MAINWINDOW_FALLBACK_LEFT + DEFAULT_MAINWINDOW_FALLBACK_WIDTH,
			DEFAULT_MAINWINDOW_FALLBACK_TOP + DEFAULT_MAINWINDOW_FALLBACK_HEIGHT);
	}

	bool GetStartupMonitorRects(CRect& rcMonitor, CRect& rcWorkArea)
	{
		rcMonitor.SetRectEmpty();
		rcWorkArea.SetRectEmpty();

		POINT ptCursor = {};
		if (!::GetCursorPos(&ptCursor)) {
			ptCursor.x = 0;
			ptCursor.y = 0;
		}

		const HMONITOR hMonitor = ::MonitorFromPoint(ptCursor, MONITOR_DEFAULTTOPRIMARY);
		MONITORINFO monitorInfo = {};
		monitorInfo.cbSize = sizeof(monitorInfo);
		if (hMonitor != NULL && ::GetMonitorInfo(hMonitor, &monitorInfo)) {
			rcMonitor.CopyRect(&monitorInfo.rcMonitor);
			rcWorkArea.CopyRect(&monitorInfo.rcWork);
			return !rcMonitor.IsRectEmpty() && !rcWorkArea.IsRectEmpty();
		}

		RECT rcPrimaryWorkArea = {};
		if (::SystemParametersInfo(SPI_GETWORKAREA, 0, &rcPrimaryWorkArea, 0)) {
			rcWorkArea.CopyRect(&rcPrimaryWorkArea);
			rcMonitor.CopyRect(&rcPrimaryWorkArea);
			return !rcWorkArea.IsRectEmpty();
		}

		return false;
	}

	void BuildDefaultMainWindowPlacement(WINDOWPLACEMENT& wp)
	{
		memset(&wp, 0, sizeof(wp));
		wp.length = sizeof(wp);
		wp.showCmd = SW_SHOWNORMAL;

		CRect rcMonitor;
		CRect rcWorkArea;
		if (!GetStartupMonitorRects(rcMonitor, rcWorkArea)) {
			SetFallbackMainWindowPlacement(wp);
			return;
		}

		const int nWorkWidth = rcWorkArea.Width();
		const int nWorkHeight = rcWorkArea.Height();
		if (nWorkWidth <= 0 || nWorkHeight <= 0) {
			SetFallbackMainWindowPlacement(wp);
			return;
		}

		int nTargetWidth = max(1, MulDiv(nWorkWidth, DEFAULT_MAINWINDOW_WORKAREA_PERCENT, 100));
		int nTargetHeight = max(1, MulDiv(nWorkHeight, DEFAULT_MAINWINDOW_WORKAREA_PERCENT, 100));

		const bool bIsUltraWide = rcMonitor.Width() > MulDiv(rcMonitor.Height(), DEFAULT_MAINWINDOW_ASPECT_WIDTH, DEFAULT_MAINWINDOW_ASPECT_HEIGHT);
		if (bIsUltraWide) {
			nTargetWidth = MulDiv(nTargetHeight, DEFAULT_MAINWINDOW_ASPECT_WIDTH, DEFAULT_MAINWINDOW_ASPECT_HEIGHT);
			if (nTargetWidth > nWorkWidth) {
				nTargetWidth = nWorkWidth;
				nTargetHeight = max(1, MulDiv(nTargetWidth, DEFAULT_MAINWINDOW_ASPECT_HEIGHT, DEFAULT_MAINWINDOW_ASPECT_WIDTH));
			}
		}

		const int nLeft = rcWorkArea.left + max(0, (nWorkWidth - nTargetWidth) / 2);
		const int nTop = rcWorkArea.top + max(0, (nWorkHeight - nTargetHeight) / 2);
		::SetRect(&wp.rcNormalPosition, nLeft, nTop, nLeft + nTargetWidth, nTop + nTargetHeight);
	}
}

CPreferences thePrefs;

CString CPreferences::m_astrDefaultDirs[EMULE_DIRECTORY_COUNT];
bool	CPreferences::m_abDefaultDirsCreated[EMULE_DIRECTORY_COUNT] = {};
int		CPreferences::m_nCurrentUserDirMode = 2;
int		CPreferences::m_iDbgHeap;
CString	CPreferences::strNick;
uint32	CPreferences::m_minupload;
uint32	CPreferences::m_maxupload;
uint32	CPreferences::m_maxdownload;
LPCSTR	CPreferences::m_pszBindAddrA;
CStringA CPreferences::m_strBindAddrA;
LPCWSTR	CPreferences::m_pszBindAddrW;
CStringW CPreferences::m_strBindAddrW;
uint16	CPreferences::port;
uint16	CPreferences::m_nStartupTcpPortOverride;
uint16	CPreferences::udpport;
uint16	CPreferences::nServerUDPPort;
UINT	CPreferences::maxconnections;
UINT	CPreferences::maxhalfconnections;
bool	CPreferences::m_bConditionalTCPAccept;
bool	CPreferences::reconnect;
bool	CPreferences::m_bUseServerPriorities;
bool	CPreferences::m_bUseUserSortedServerList;
CString	CPreferences::m_strIncomingDir;
CStringArray CPreferences::tempdir;
bool	CPreferences::ICH;
bool	CPreferences::m_bAutoUpdateServerList;

bool	CPreferences::mintotray;
bool	CPreferences::autoconnect;
bool	CPreferences::m_bAutoConnectToStaticServersOnly;
bool	CPreferences::autotakeed2klinks;
bool	CPreferences::addnewfilespaused;
UINT	CPreferences::depth3D;
bool	CPreferences::m_bEnableMiniMule;
int		CPreferences::m_iStraightWindowStyles;
bool	CPreferences::m_bUseSystemFontForMainControls;
bool	CPreferences::m_bRTLWindowsLayout;
CString	CPreferences::m_strSkinProfile;
CString	CPreferences::m_strSkinProfileDir;
bool	CPreferences::m_bAddServersFromServer;
bool	CPreferences::m_bAddServersFromClients;
UINT	CPreferences::maxsourceperfile;
UINT	CPreferences::trafficOMeterInterval;
UINT	CPreferences::statsInterval;
bool	CPreferences::m_bFillGraphs;
uchar	CPreferences::userhash[MDX_DIGEST_SIZE];
WINDOWPLACEMENT CPreferences::EmuleWindowPlacement;
uint32	CPreferences::maxGraphDownloadRate;
uint32	CPreferences::maxGraphUploadRate;
uint32	CPreferences::maxGraphUploadRateEstimated = 0;
bool	CPreferences::beepOnError;
bool	CPreferences::m_bIconflashOnNewMessage;
bool	CPreferences::confirmExit;
DWORD	CPreferences::m_adwStatsColors[15];
bool	CPreferences::m_bHasCustomTaskIconColor;
bool	CPreferences::splashscreen;
bool	CPreferences::filterLANIPs;
bool	CPreferences::m_bAllocLocalHostIP;
bool	CPreferences::onlineSig;
uint64	CPreferences::cumDownOverheadTotal;
uint64	CPreferences::cumDownOverheadFileReq;
uint64	CPreferences::cumDownOverheadSrcEx;
uint64	CPreferences::cumDownOverheadServer;
uint64	CPreferences::cumDownOverheadKad;
uint64	CPreferences::cumDownOverheadTotalPackets;
uint64	CPreferences::cumDownOverheadFileReqPackets;
uint64	CPreferences::cumDownOverheadSrcExPackets;
uint64	CPreferences::cumDownOverheadServerPackets;
uint64	CPreferences::cumDownOverheadKadPackets;
uint64	CPreferences::cumUpOverheadTotal;
uint64	CPreferences::cumUpOverheadFileReq;
uint64	CPreferences::cumUpOverheadSrcEx;
uint64	CPreferences::cumUpOverheadServer;
uint64	CPreferences::cumUpOverheadKad;
uint64	CPreferences::cumUpOverheadTotalPackets;
uint64	CPreferences::cumUpOverheadFileReqPackets;
uint64	CPreferences::cumUpOverheadSrcExPackets;
uint64	CPreferences::cumUpOverheadServerPackets;
uint64	CPreferences::cumUpOverheadKadPackets;
uint32	CPreferences::cumUpSuccessfulSessions;
uint32	CPreferences::cumUpFailedSessions;
uint32	CPreferences::cumUpAvgTime;
uint64	CPreferences::cumUpData_EDONKEY;
uint64	CPreferences::cumUpData_EDONKEYHYBRID;
uint64	CPreferences::cumUpData_EMULE;
uint64	CPreferences::cumUpData_MLDONKEY;
uint64	CPreferences::cumUpData_AMULE;
uint64	CPreferences::cumUpData_EMULECOMPAT;
uint64	CPreferences::cumUpData_SHAREAZA;
uint64	CPreferences::sesUpData_EDONKEY;
uint64	CPreferences::sesUpData_EDONKEYHYBRID;
uint64	CPreferences::sesUpData_EMULE;
uint64	CPreferences::sesUpData_MLDONKEY;
uint64	CPreferences::sesUpData_AMULE;
uint64	CPreferences::sesUpData_EMULECOMPAT;
uint64	CPreferences::sesUpData_SHAREAZA;
uint64	CPreferences::cumUpDataPort_4662;
uint64	CPreferences::cumUpDataPort_OTHER;
uint64	CPreferences::sesUpDataPort_4662;
uint64	CPreferences::sesUpDataPort_OTHER;
uint64	CPreferences::cumUpData_File;
uint64	CPreferences::cumUpData_Partfile;
uint64	CPreferences::sesUpData_File;
uint64	CPreferences::sesUpData_Partfile;
uint32	CPreferences::cumDownCompletedFiles;
uint32	CPreferences::cumDownSuccessfulSessions;
uint32	CPreferences::cumDownFailedSessions;
uint32	CPreferences::cumDownAvgTime;
uint64	CPreferences::cumLostFromCorruption;
uint64	CPreferences::cumSavedFromCompression;
uint32	CPreferences::cumPartsSavedByICH;
uint32	CPreferences::sesDownSuccessfulSessions;
uint32	CPreferences::sesDownFailedSessions;
uint32	CPreferences::sesDownAvgTime;
uint32	CPreferences::sesDownCompletedFiles;
uint64	CPreferences::sesLostFromCorruption;
uint64	CPreferences::sesSavedFromCompression;
uint32	CPreferences::sesPartsSavedByICH;
uint64	CPreferences::cumDownData_EDONKEY;
uint64	CPreferences::cumDownData_EDONKEYHYBRID;
uint64	CPreferences::cumDownData_EMULE;
uint64	CPreferences::cumDownData_MLDONKEY;
uint64	CPreferences::cumDownData_AMULE;
uint64	CPreferences::cumDownData_EMULECOMPAT;
uint64	CPreferences::cumDownData_SHAREAZA;
uint64	CPreferences::cumDownData_URL;
uint64	CPreferences::sesDownData_EDONKEY;
uint64	CPreferences::sesDownData_EDONKEYHYBRID;
uint64	CPreferences::sesDownData_EMULE;
uint64	CPreferences::sesDownData_MLDONKEY;
uint64	CPreferences::sesDownData_AMULE;
uint64	CPreferences::sesDownData_EMULECOMPAT;
uint64	CPreferences::sesDownData_SHAREAZA;
uint64	CPreferences::sesDownData_URL;
uint64	CPreferences::cumDownDataPort_4662;
uint64	CPreferences::cumDownDataPort_OTHER;
uint64	CPreferences::sesDownDataPort_4662;
uint64	CPreferences::sesDownDataPort_OTHER;
float	CPreferences::cumConnAvgDownRate;
float	CPreferences::cumConnMaxAvgDownRate;
float	CPreferences::cumConnMaxDownRate;
float	CPreferences::cumConnAvgUpRate;
float	CPreferences::cumConnMaxAvgUpRate;
float	CPreferences::cumConnMaxUpRate;
time_t	CPreferences::cumConnRunTime;
uint32	CPreferences::cumConnNumReconnects;
uint32	CPreferences::cumConnAvgConnections;
uint32	CPreferences::cumConnMaxConnLimitReached;
uint32	CPreferences::cumConnPeakConnections;
uint32	CPreferences::cumConnTransferTime;
uint32	CPreferences::cumConnDownloadTime;
uint32	CPreferences::cumConnUploadTime;
uint32	CPreferences::cumConnServerDuration;
uint32	CPreferences::cumSrvrsMostWorkingServers;
uint32	CPreferences::cumSrvrsMostUsersOnline;
uint32	CPreferences::cumSrvrsMostFilesAvail;
uint32	CPreferences::cumSharedMostFilesShared;
uint64	CPreferences::cumSharedLargestShareSize;
uint64	CPreferences::cumSharedLargestAvgFileSize;
uint64	CPreferences::cumSharedLargestFileSize;
time_t	CPreferences::stat_datetimeLastReset;
UINT	CPreferences::statsConnectionsGraphRatio;
UINT	CPreferences::statsSaveInterval;
CString	CPreferences::m_strStatsExpandedTreeItems;
bool	CPreferences::m_bShowVerticalHourMarkers;
uint64	CPreferences::totalDownloadedBytes;
uint64	CPreferences::totalUploadedBytes;
LANGID	CPreferences::m_wLanguageID;
CString	CPreferences::m_strUiLanguage;
bool	CPreferences::transferDoubleclick;
EViewSharedFilesAccess CPreferences::m_iSeeShares;
UINT	CPreferences::m_iToolDelayTime;
bool	CPreferences::bringtoforeground;
UINT	CPreferences::splitterbarPosition;
UINT	CPreferences::splitterbarPositionSvr;
UINT	CPreferences::splitterbarPositionStat;
UINT	CPreferences::splitterbarPositionStat_HL;
UINT	CPreferences::splitterbarPositionStat_HR;
UINT	CPreferences::splitterbarPositionFriend;
UINT	CPreferences::splitterbarPositionIRC;
UINT	CPreferences::splitterbarPositionShared;
UINT	CPreferences::m_uTransferWnd1;
UINT	CPreferences::m_uTransferWnd2;
UINT	CPreferences::m_uDeadServerRetries;
uint8	CPreferences::m_nMaxEServerBuddySlots = ESERVERBUDDY_DEFAULT_SLOTS;
uint16	CPreferences::m_nEServerDiscoveredExternalUdpPort = 0;
DWORD	CPreferences::m_dwEServerDiscoveredExternalUdpPortTime = 0;
uint8	CPreferences::m_uEServerDiscoveredExternalUdpPortSource = 0;
DWORD	CPreferences::m_dwServerKeepAliveTimeout;
UINT	CPreferences::statsMax;
UINT	CPreferences::statsAverageMinutes;
CString	CPreferences::notifierConfiguration;
bool	CPreferences::notifierOnDownloadFinished;
bool	CPreferences::notifierOnNewDownload;
bool	CPreferences::notifierOnChat;
bool	CPreferences::notifierOnLog;
bool	CPreferences::notifierOnImportantError;
bool	CPreferences::notifierOnEveryChatMsg;
ENotifierSoundType CPreferences::notifierSoundType = ntfstNoSound;
CString	CPreferences::notifierSoundFile;
CString CPreferences::m_strIRCServer;
CString	CPreferences::m_strIRCNick;
CString	CPreferences::m_strIRCChannelFilter;
bool	CPreferences::m_bIRCAddTimeStamp;
bool	CPreferences::m_bIRCUseChannelFilter;
UINT	CPreferences::m_uIRCChannelUserFilter;
CString	CPreferences::m_strIRCPerformString;
bool	CPreferences::m_bIRCUsePerform;
bool	CPreferences::m_bIRCGetChannelsOnConnect;
bool	CPreferences::m_bIRCAcceptLinks;
bool	CPreferences::m_bIRCAcceptLinksFriendsOnly;
bool	CPreferences::m_bIRCPlaySoundEvents;
bool	CPreferences::m_bIRCIgnoreMiscMessages;
bool	CPreferences::m_bIRCIgnoreJoinMessages;
bool	CPreferences::m_bIRCIgnorePartMessages;
bool	CPreferences::m_bIRCIgnoreQuitMessages;
bool	CPreferences::m_bIRCIgnorePingPongMessages;
bool	CPreferences::m_bIRCIgnoreEmuleAddFriendMsgs;
bool	CPreferences::m_bIRCAllowEmuleAddFriend;
bool	CPreferences::m_bIRCIgnoreEmuleSendLinkMsgs;
bool	CPreferences::m_bIRCJoinHelpChannel;
bool	CPreferences::m_bIRCEnableSmileys;
bool	CPreferences::m_bIRCEnableUTF8;
bool	CPreferences::m_bMessageEnableSmileys;
bool	CPreferences::m_bRemove2bin;
bool	CPreferences::m_bShowCopyEd2kLinkCmd;
bool	CPreferences::m_bpreviewprio;
bool	CPreferences::m_bSmartServerIdCheck;
uint8	CPreferences::smartidstate;
bool	CPreferences::m_bSafeServerConnect;
bool	CPreferences::startMinimized;
bool	CPreferences::m_bAutoStart;
bool	CPreferences::m_bRestoreLastMainWndDlg;
int		CPreferences::m_iLastMainWndDlgID;
bool	CPreferences::m_bRestoreLastLogPane;
int		CPreferences::m_iLastLogPaneID;
UINT	CPreferences::MaxConperFive;
bool	CPreferences::checkDiskspace;
UINT	CPreferences::m_uMinFreeDiskSpace;
uint32	CPreferences::m_uFreeDiskSpaceCheckPeriod;
bool	CPreferences::m_bSparsePartFiles;
bool	CPreferences::m_bImportParts;
CString	CPreferences::m_strYourHostname;
bool	CPreferences::m_bEnableVerboseOptions;
bool	CPreferences::m_bVerbose;
bool	CPreferences::m_bFullVerbose;
bool	CPreferences::m_bDebugSourceExchange;
bool	CPreferences::m_bLogBannedClients;
bool	CPreferences::m_bLogRatingDescReceived;
bool	CPreferences::m_bLogSecureIdent;
bool	CPreferences::m_bLogFilteredIPs;
bool	CPreferences::m_bLogFileSaving;
bool	CPreferences::m_bLogA4AF; // ZZ:DownloadManager
bool	CPreferences::m_bLogUlDlEvents;
bool	CPreferences::m_bLogSpamRating;
bool	CPreferences::m_bLogRetryFailedTcp;
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
bool	CPreferences::m_bUseDebugDevice = true;
#else
bool	CPreferences::m_bUseDebugDevice = false;
#endif
int		CPreferences::m_iDebugServerTCPLevel;
int		CPreferences::m_iDebugServerUDPLevel;
int		CPreferences::m_iDebugServerSourcesLevel;
int		CPreferences::m_iDebugServerSearchesLevel;
int		CPreferences::m_iDebugClientTCPLevel;
int		CPreferences::m_iDebugClientUDPLevel;
int		CPreferences::m_iDebugClientKadUDPLevel;
int		CPreferences::m_iDebugSearchResultDetailLevel;
bool	CPreferences::m_bupdatequeuelist;
bool	CPreferences::m_bManualAddedServersHighPriority;
bool	CPreferences::m_btransferfullchunks;
int		CPreferences::m_istartnextfile;
bool	CPreferences::m_bshowoverhead;
bool	CPreferences::m_bDAP;
bool	CPreferences::m_bUAP;
bool	CPreferences::m_bDisableKnownClientList;
bool	CPreferences::m_bDisableQueueList;
bool	CPreferences::m_bExtControls;
bool	CPreferences::m_bTransflstRemain;

bool	CPreferences::showRatesInTitle;
CString	CPreferences::m_strTxtEditor;
CString	CPreferences::m_strVideoPlayer;
CString CPreferences::m_strVideoPlayerArgs;
bool	CPreferences::m_bMoviePreviewBackup;
int		CPreferences::m_iPreviewSmallBlocks;
bool	CPreferences::m_bPreviewCopiedArchives;
bool	CPreferences::m_bPreviewOnIconDblClk;
bool	CPreferences::m_bCheckFileOpen;
bool	CPreferences::indicateratings;
bool	CPreferences::watchclipboard;
bool	CPreferences::filterserverbyip;
bool	CPreferences::m_bDontFilterPrivateIPs;
bool	CPreferences::m_bFirstStart;
bool	CPreferences::m_bUiLanguagePresent;
bool	CPreferences::m_bMigrationWizardHandled;
bool	CPreferences::m_bMigrationWizardRunOnNextStart;
bool	CPreferences::m_bBetaNaggingDone;
bool	CPreferences::m_bCreditSystem;
bool	CPreferences::log2disk;
bool	CPreferences::debug2disk;
int		CPreferences::iMaxLogBuff;
UINT	CPreferences::uMaxLogFileSize;
ELogFileFormat CPreferences::m_iLogFileFormat = Unicode;
bool	CPreferences::scheduler;
bool	CPreferences::dontcompressavi;
bool	CPreferences::msgonlyfriends;
bool	CPreferences::msgsecure;
bool	CPreferences::m_bUseChatCaptchas;
UINT	CPreferences::filterlevel;
UINT	CPreferences::m_uFileBufferSize;
DWORD	CPreferences::m_uFileBufferTimeLimit;
INT_PTR	CPreferences::m_iQueueSize;
int		CPreferences::m_iCommitFiles;
UINT	CPreferences::maxmsgsessions;
time_t	CPreferences::versioncheckLastAutomatic;
CString	CPreferences::versioncheckLastKnownVersionOnServer;
CString	CPreferences::messageFilter;
CString	CPreferences::commentFilter;
CString	CPreferences::filenameCleanups;
CString	CPreferences::m_strDateTimeFormat;
CString	CPreferences::m_strDateTimeFormat4Log;
CString	CPreferences::m_strDateTimeFormat4Lists;
LOGFONT CPreferences::m_lfHyperText;
LOGFONT CPreferences::m_lfLogText;
COLORREF CPreferences::m_crLogError = RGB(255, 0, 0);
COLORREF CPreferences::m_crLogWarning = RGB(128, 0, 128);
COLORREF CPreferences::m_crLogSuccess = RGB(0, 0, 255);
int		CPreferences::m_iExtractMetaData;
bool	CPreferences::m_bRearrangeKadSearchKeywords;
CString	CPreferences::m_strWebPassword;
CString	CPreferences::m_strWebLowPassword;
CUIntArray CPreferences::m_aAllowedRemoteAccessIPs;
uint16	CPreferences::m_nWebPort;
bool	CPreferences::m_bWebUseUPnP;
bool	CPreferences::m_bWebEnabled;
bool	CPreferences::m_bWebUseGzip;
int		CPreferences::m_nWebPageRefresh;
bool	CPreferences::m_bWebLowEnabled;
int		CPreferences::m_iWebTimeoutMins;
int		CPreferences::m_iWebFileUploadSizeLimitMB;
bool	CPreferences::m_bAllowAdminHiLevFunc;
CString	CPreferences::m_strTemplateFile;
bool	CPreferences::m_bWebUseHttps;
CString	CPreferences::m_sWebHttpsCertificate;
CString	CPreferences::m_sWebHttpsKey;

ProxySettings CPreferences::proxy;
bool	CPreferences::showCatTabInfos;
bool	CPreferences::dontRecreateGraphs;
bool	CPreferences::autofilenamecleanup;
bool	CPreferences::m_bUseAutocompl;
bool	CPreferences::m_bShowDwlPercentage;
bool	CPreferences::m_bRemoveFinishedDownloads;
INT_PTR	CPreferences::m_iMaxChatHistory;
bool	CPreferences::m_bShowActiveDownloadsBold;
int		CPreferences::m_iSearchMethod;
CString	CPreferences::m_strSearchFileType;
bool	CPreferences::m_bAdvancedSpamfilter;
bool	CPreferences::m_bUseSecureIdent;
bool	CPreferences::networkkademlia;
bool	CPreferences::networked2k;
EToolbarLabelType CPreferences::m_nToolbarLabels;
CString	CPreferences::m_sToolbarBitmap;
CString	CPreferences::m_sToolbarBitmapFolder;
CString	CPreferences::m_sToolbarSettings;
bool	CPreferences::m_bReBarToolbar;
CSize	CPreferences::m_sizToolbarIconSize;
bool	CPreferences::m_bPreviewEnabled;
bool	CPreferences::m_bAutomaticArcPreviewStart;
bool	CPreferences::m_bDynUpEnabled;
int		CPreferences::m_iDynUpPingTolerance;
int		CPreferences::m_iDynUpGoingUpDivider;
int		CPreferences::m_iDynUpGoingDownDivider;
int		CPreferences::m_iDynUpNumberOfPings;
int		CPreferences::m_iDynUpPingToleranceMilliseconds;
bool	CPreferences::m_bDynUpUseMillisecondPingTolerance;
bool    CPreferences::m_bAllocFull;
bool	CPreferences::m_bShowSharedFilesDetails;
bool	CPreferences::m_bShowUpDownIconInTaskbar;
bool	CPreferences::m_bShowWin7TaskbarGoodies;
bool	CPreferences::m_bForceSpeedsToKB;
bool	CPreferences::m_bAutoShowLookups;

bool    CPreferences::m_bA4AFSaveCpu;
bool    CPreferences::m_bHighresTimer;
bool	CPreferences::m_bResolveSharedShellLinks;
bool	CPreferences::m_bKeepUnavailableFixedSharedDirs;
CStringList CPreferences::shareddir_list;
CStringList CPreferences::addresses_list;
CString CPreferences::m_strFileCommentsFilePath;
Preferences_Ext_Struct* CPreferences::prefsExt = NULL; // Explicit zero-init for safety
WORD	CPreferences::m_wWinVer;
CArray<Category_Struct*, Category_Struct*> CPreferences::catArr;
UINT	CPreferences::m_nWebMirrorAlertLevel;
bool	CPreferences::m_bRunAsUser;
bool	CPreferences::m_bPreferRestrictedOverUser;
bool	CPreferences::m_bUseOldTimeRemaining;

bool	CPreferences::m_bOpenPortsOnStartUp;
int		CPreferences::m_byLogLevel;
bool	CPreferences::m_bTrustEveryHash;
bool	CPreferences::m_bRememberCancelledFiles;
bool	CPreferences::m_bRememberDownloadedFiles;
bool	CPreferences::m_bPartiallyPurgeOldKnownFiles;

EmailSettings CPreferences::m_email;

bool	CPreferences::m_bWinaTransToolbar;
bool	CPreferences::m_bShowDownloadToolbar;

bool	CPreferences::m_bCryptLayerRequested;
bool	CPreferences::m_bCryptLayerSupported;
bool	CPreferences::m_bCryptLayerRequired;
uint32	CPreferences::m_dwKadUDPKey;
uint8	CPreferences::m_byCryptTCPPaddingLength;

bool	CPreferences::m_bSkipWANIPSetup;
bool	CPreferences::m_bSkipWANPPPSetup;
bool	CPreferences::m_bEnableUPnP;
bool	CPreferences::m_bCloseUPnPOnExit;
bool	CPreferences::m_bIsWinServImplDisabled;
bool	CPreferences::m_bIsMinilibImplDisabled;
int		CPreferences::m_nLastWorkingImpl;

bool	CPreferences::m_bEnableSearchResultFilter;

BOOL	CPreferences::m_bIsRunningAeroGlass;
bool	CPreferences::m_bPreventStandby;
bool	CPreferences::m_bStoreSearches;

bool	CPreferences::m_bDarkModeEnabled;
int		CPreferences::m_iUIDarkMode;
bool	CPreferences::m_bUITweaksSpeedGraph;
bool	CPreferences::m_bDisableFindAsYouType;
int		CPreferences::m_iUITweaksListUpdatePeriod;

int		CPreferences::m_iGeoLite2Mode;
bool	CPreferences::m_bGeoLite2ShowFlag;

bool	CPreferences::m_bConnectionChecker;
CString	CPreferences::m_sConnectionCheckerServer;

bool	CPreferences::m_bEnableNatTraversal;
bool	CPreferences::m_bLogExtendedSXEvents;
bool	CPreferences::m_bLogNatTraversalEvents;
uint16	CPreferences::m_uNatTraversalPortWindow = 512;
uint16	CPreferences::m_uNatTraversalSweepWindow = 16;
uint32	CPreferences::m_uNatTraversalJitterMinMs = 50;
uint32	CPreferences::m_uNatTraversalJitterMaxMs = 150;

uint32	CPreferences::m_uMaxServedBuddies = 5;


bool	CPreferences::m_bRetryFailedTcpConnectionAttempts;

bool	CPreferences::m_breaskSourceAfterIPChange;
bool	CPreferences::m_bInformQueuedClientsAfterIPChange;

uint32	CPreferences::m_uReAskTimeDif;

int		CPreferences::m_iDownloadChecker;
int		CPreferences::m_iDownloadCheckerAcceptPercentage;
bool	CPreferences::m_bDownloadCheckerRejectCanceled;
bool	CPreferences::m_bDownloadCheckerRejectSameHash;
bool	CPreferences::m_bDownloadCheckerRejectBlacklisted;
bool	CPreferences::m_bDownloadCheckerCaseInsensitive;
bool	CPreferences::m_bDownloadCheckerIgnoreExtension;
bool	CPreferences::m_bDownloadCheckerIgnoreTags;
bool	CPreferences::m_bDownloadCheckerDontIgnoreNumericTags;
bool	CPreferences::m_bDownloadCheckerIgnoreNonAlphaNumeric;
int		CPreferences::m_iDownloadCheckerMinimumComparisonLength;
bool	CPreferences::m_bDownloadCheckerSkipIncompleteFileConfirmation;
bool	CPreferences::m_bDownloadCheckerMarkAsBlacklisted;
bool	CPreferences::m_bDownloadCheckerAutoMarkAsBlacklisted;

int		CPreferences::m_iFileInspector;
bool	CPreferences::m_bFileInspectorFake;
bool	CPreferences::m_bFileInspectorDRM;
bool	CPreferences::m_bFileInspectorInvalidExt;
int		CPreferences::m_iFileInspectorCheckPeriod;
int		CPreferences::m_iFileInspectorCompletedThreshold;
int		CPreferences::m_iFileInspectorZeroPercentageThreshold;
int		CPreferences::m_iFileInspectorCompressionThreshold;
bool	CPreferences::m_bFileInspectorBypassZeroPercentage;
int		CPreferences::m_iFileInspectorCompressionThresholdToBypassZero;

bool	 CPreferences::m_bGroupKnownAtTheBottom;
int		 CPreferences::m_iSpamThreshold;
int		 CPreferences::m_iKadSearchKeywordTotal;
bool	 CPreferences::m_bShowCloseButtonOnSearchTabs;

bool	CPreferences::m_bRepeatServerList;
bool	CPreferences::m_bDontRemoveStaticServers;
bool	CPreferences::m_bDontSavePartOnReconnect;

bool	CPreferences::m_bFileHistoryShowPart;
bool	CPreferences::m_bFileHistoryShowShared;
bool	CPreferences::m_bFileHistoryShowDuplicate;
std::atomic_bool CPreferences::m_bAutoShareSubdirs{ false };
std::atomic_bool CPreferences::m_bDontShareExtensions{ true };
CCriticalSection CPreferences::m_csDontShareExtList;
CString CPreferences::m_sDontShareExtensionsList;
bool	CPreferences::m_bAdjustNTFSDaylightFileTime = false; // Official preference: 'true' causes rehashing in XP and above when DST switches on/off
bool	CPreferences::m_bAllowDSTTimeTolerance;

bool	CPreferences::m_bEmulateMLDonkey;
bool	CPreferences::m_bEmulateEdonkey;
bool	CPreferences::m_bEmulateEdonkeyHybrid;
bool	CPreferences::m_bEmulateShareaza;
bool    CPreferences::m_bEmulateLphant;
bool    CPreferences::m_bEmulateCommunity;
int		CPreferences::m_iEmulateCommunityTagSavingTreshold;
bool	CPreferences::m_bLogEmulator;

bool	CPreferences::m_bUseIntelligentChunkSelection;

int		CPreferences::creditSystemMode;

bool	CPreferences::m_bClientHistory;
int		CPreferences::m_iClientHistoryExpDays;
bool	CPreferences::m_bClientHistoryLog;

bool	CPreferences::m_bRemoteSharedFilesUserHash;
bool	CPreferences::m_bRemoteSharedFilesClientNote;
int		CPreferences::m_iRemoteSharedFilesAutoQueryPeriod;
int		CPreferences::m_iRemoteSharedFilesAutoQueryMaxClients;
int		CPreferences::m_iRemoteSharedFilesAutoQueryClientPeriod;
bool	CPreferences::m_bRemoteSharedFilesSetAutoQueryDownload;
int		CPreferences::m_iRemoteSharedFilesSetAutoQueryDownloadThreshold;
bool	CPreferences::m_bRemoteSharedFilesSetAutoQueryUpload;
int		CPreferences::m_iRemoteSharedFilesSetAutoQueryUploadThreshold;

bool	CPreferences::m_bSaveLoadSources;
int		CPreferences::m_iSaveLoadSourcesMaxSources;
int		CPreferences::m_iSaveLoadSourcesExpirationDays;

int		CPreferences::m_iKnownMetDays;
bool	CPreferences::m_bCompletlyPurgeOldKnownFiles;
bool	CPreferences::m_bRemoveAichImmediately;
int		CPreferences::m_iClientsExpDays;

bool 	CPreferences::m_bBackupOnExit;
bool 	CPreferences::m_bBackupPeriodic;
int 	CPreferences::m_iBackupPeriod;
int 	CPreferences::m_iBackupMax;
bool 	CPreferences::m_bBackupCompressed;

CString	CPreferences::m_strFileTypeSelected;
uint8	CPreferences::m_uPreviewCheckState;
bool	CPreferences::m_bFreezeChecked;
uint8	CPreferences::m_uArchivedCheckState;
uint8	CPreferences::m_uConnectedCheckState;
uint8	CPreferences::m_uQueryableCheckState;
uint8	CPreferences::m_uNotQueriedCheckState;
uint8	CPreferences::m_uValidIPCheckState;
uint8	CPreferences::m_uHighIdCheckState;
uint8	CPreferences::m_uBadClientCheckState;

uint8	CPreferences::m_uCompleteCheckState;

int		CPreferences::m_iBootstrapSelection;

bool CPreferences::bMiniMuleAutoClose;
int  CPreferences::iMiniMuleTransparency;
bool CPreferences::bCreateCrashDump;
bool CPreferences::bIgnoreInstances;
CString CPreferences::sNotifierMailEncryptCertName;
CString CPreferences::sInternetSecurityZone;

bool CPreferences::m_bDisableAICHCreation;

int		CPreferences::m_iPunishmentCancelationScanPeriod; // adjust ClientBanTime - Stulle
time_t	CPreferences::m_tClientBanTime; // adjust ClientBanTime - Stulle
time_t	CPreferences::m_tClientScoreReducingTime;
bool	CPreferences::m_bInformBadClients;
CString	CPreferences::m_strInformBadClientsText;
bool	CPreferences::m_bDontPunishFriends;
bool	CPreferences::m_bDontAllowFileHotSwapping;
bool	CPreferences::m_bAntiUploadProtection;
uint16	CPreferences::m_iAntiUploadProtectionLimit;
bool	CPreferences::m_bUploaderPunishmentPrevention;
uint16  CPreferences::m_iUploaderPunishmentPreventionLimit;
uint8	CPreferences::m_iUploaderPunishmentPreventionCase;
bool	CPreferences::m_bDetectModNames;
bool	CPreferences::m_bDetectUserNames;
bool	CPreferences::m_bDetectUserHashes;
uint8	CPreferences::m_uHardLeecherPunishment;
uint8	CPreferences::m_uSoftLeecherPunishment;
bool	CPreferences::m_bBanBadKadNodes;
bool	CPreferences::m_bBanWrongPackage;
bool	CPreferences::m_bDetectAntiP2PBots;
uint8	CPreferences::m_uAntiP2PBotsPunishment;
bool	CPreferences::m_bDetectWrongTag;
uint8	CPreferences::m_uWrongTagPunishment;
bool	CPreferences::m_bDetectUnknownTag;
uint8	CPreferences::m_uUnknownTagPunishment;
bool	CPreferences::m_bDetectHashThief;
uint8	CPreferences::m_uHashThiefPunishment;
bool	CPreferences::m_bDetectModThief;
uint8	CPreferences::m_uModThiefPunishment;
bool	CPreferences::m_bDetectUserNameThief;
uint8	CPreferences::m_uUserNameThiefPunishment;
bool	CPreferences::m_bDetectModChanger;
int 	CPreferences::m_iModChangerInterval;
int 	CPreferences::m_iModChangerThreshold;
uint8	CPreferences::m_uModChangerPunishment;
bool	CPreferences::m_bDetectUserNameChanger;
int 	CPreferences::m_iUserNameChangerInterval;
int 	CPreferences::m_iUserNameChangerThreshold;
uint8	CPreferences::m_uUserNameChangerPunishment;
bool	CPreferences::m_bDetectTCPErrorFlooder;
int 	CPreferences::m_iTCPErrorFlooderInterval;
int 	CPreferences::m_iTCPErrorFlooderThreshold;
uint8	CPreferences::m_uTCPErrorFlooderPunishment;
bool	CPreferences::m_bDetectEmptyUserNameEmule;
uint8	CPreferences::m_uEmptyUserNameEmulePunishment;
bool	CPreferences::m_bDetectCommunity;
uint8	CPreferences::m_uCommunityPunishment;
bool	CPreferences::m_bDetectFakeEmule;
uint8	CPreferences::m_uFakeEmulePunishment;
bool	CPreferences::m_bDetectHexModName;
uint8	CPreferences::m_uHexModNamePunishment;
bool	CPreferences::m_bDetectGhostMod;
uint8	CPreferences::m_uGhostModPunishment;
bool	CPreferences::m_bDetectSpam;
uint8	CPreferences::m_uSpamPunishment;
bool	CPreferences::m_bDetectEmcrypt;
uint8	CPreferences::m_uEmcryptPunishment;
bool	CPreferences::m_bDetectXSExploiter;
uint8	CPreferences::m_uXSExploiterPunishment;
bool	CPreferences::m_bDetectFileFaker;
uint8	CPreferences::m_uFileFakerPunishment;
bool	CPreferences::m_bDetectUploadFaker;
uint8	CPreferences::m_uUploadFakerPunishment;
bool	CPreferences::m_bDetectAgressive;
uint16  CPreferences::m_iAgressiveTime;
uint16  CPreferences::m_iAgressiveCounter;
bool	CPreferences::m_bAgressiveLog;
uint8	CPreferences::m_uAgressivePunishment;
bool	CPreferences::m_bPunishNonSuiMlDonkey;
bool	CPreferences::m_bPunishNonSuiEdonkey;
bool	CPreferences::m_bPunishNonSuiEdonkeyHybrid;
bool	CPreferences::m_bPunishNonSuiShareaza;
bool	CPreferences::m_bPunishNonSuiLphant;
bool	CPreferences::m_bPunishNonSuiAmule;
bool	CPreferences::m_bPunishNonSuiEmule;
uint8	CPreferences::m_uNonSuiPunishment;
bool	CPreferences::m_bDetectCorruptedDataSender;
bool	CPreferences::m_bDetectHashChanger;
bool	CPreferences::m_bDetectFileScanner;
bool	CPreferences::m_bDetectRankFlooder;
bool	CPreferences::m_bDetectKadRequestFlooder;
int		CPreferences::m_iKadRequestFloodBanTreshold;

bool	 CPreferences::m_bBlacklistAutomatic;
bool	 CPreferences::m_bBlacklistManual;
bool	 CPreferences::m_bBlacklistAutoRemoveFromManual;
bool	 CPreferences::m_bBlacklistLog;
CStringList CPreferences::blacklist_list;

CPreferences::CPreferences()
{
#ifdef _DEBUG
	m_iDbgHeap = 1;
#endif
	m_CommunityTagCounterMap.InitHashTable(1031);
}

CPreferences::~CPreferences()
{
	delete prefsExt;
	prefsExt = NULL; // Avoid dangling pointer
}

void CPreferences::ReleaseExtStruct() noexcept
{
	delete prefsExt;
	prefsExt = NULL;
	shareddir_list.RemoveAll();
	addresses_list.RemoveAll();
	blacklist_list.RemoveAll();
	tempdir.RemoveAll();
}

LPCTSTR CPreferences::GetConfigFile()
{
	return theApp.m_pszProfileName;
}
void CPreferences::MovePreferences(EDefaultDirectory eSrc, LPCTSTR const sFile, const CString &dst)
{
	const CString &src(GetMuleDirectory(eSrc));
	const CString &pathTxt(src + sFile);
	if (::PathFileExists(pathTxt))
		::MoveFile(pathTxt, dst + sFile);
}

void CPreferences::Init()
{

	ASSERT(prefsExt == NULL); // Ensure single allocation in process lifetime
	prefsExt = new Preferences_Ext_Struct{};

	const CString& sConfDir(GetMuleDirectory(EMULE_CONFIGDIR));
	m_strFileCommentsFilePath.Format(_T("%sfileinfo.ini"), (LPCTSTR)sConfDir);

	///////////////////////////////////////////////////////////////////////////
	// Move *.log files from application directory into 'log' directory
	//
	CFileFind ff;
	for (BOOL bFound = ff.FindFile(GetMuleDirectory(EMULE_EXECUTABLEDIR) + _T("eMule*.log")); bFound;) {
		bFound = ff.FindNextFile();
		if (!ff.IsDirectory() && !ff.IsSystem() && !ff.IsHidden())
			::MoveFile(ff.GetFilePath(), GetMuleDirectory(EMULE_LOGDIR) + ff.GetFileName());
	}
	ff.Close();

	///////////////////////////////////////////////////////////////////////////
	// Move 'downloads.txt/bak' files from application and/or database directory
	// into 'config' directory
	//
	static LPCTSTR const strDownloadsTxt = _T("downloads.txt");
	static LPCTSTR const strDownloadsBak = _T("downloads.bak");
	MovePreferences(EMULE_DATABASEDIR, strDownloadsTxt, sConfDir);
	MovePreferences(EMULE_EXECUTABLEDIR, strDownloadsTxt, sConfDir);
	MovePreferences(EMULE_DATABASEDIR, strDownloadsBak, sConfDir);
	MovePreferences(EMULE_EXECUTABLEDIR, strDownloadsBak, sConfDir);

	// load preferences.dat or set standard values
	CString strFullPath(sConfDir + strPreferencesDat);
	FILE* preffile = _tfsopen(strFullPath, _T("rb"), _SH_DENYWR);

	LoadPreferences();

	if (!preffile || fread(prefsExt, sizeof(Preferences_Ext_Struct), 1, preffile) != 1 || ferror(preffile))
		SetStandardValues();
	else {
		md4cpy(userhash, prefsExt->userhash);
		EmuleWindowPlacement = prefsExt->EmuleWindowPlacement;

		fclose(preffile);
		smartidstate = 0;
	}
	CreateUserHash();

		// shared directories
		strFullPath.Format(_T("%s") SHAREDDIRS, (LPCTSTR)sConfDir);
	bool bIsUnicodeFile = IsUnicodeFile(strFullPath); // check for BOM
	// open the text file either as ANSI (text) or Unicode (binary),
	// this way we can read old and new files with almost the same code.
	CStdioFile sdirfile;
	if (sdirfile.Open(strFullPath, CFile::modeRead | CFile::shareDenyWrite | (bIsUnicodeFile ? CFile::typeBinary : 0))) {
		try {
			if (bIsUnicodeFile)
				sdirfile.Seek(sizeof(WORD), CFile::begin); // skip BOM

			CString toadd;
			while (sdirfile.ReadString(toadd)) {
				toadd.Trim(_T(" \t\r\n")); // need to trim '\r' in binary mode
				if (!toadd.IsEmpty()) {
					MakeFoldername(toadd);
					// skip non-shareable directories
					// maybe skip non-existing directories on fixed disks only
					if (IsShareableDirectory(toadd) && (m_bKeepUnavailableFixedSharedDirs || DirAccsess(toadd)))
						shareddir_list.AddTail(toadd);
				}
			}
		}
		catch (CFileException* ex) {
			ASSERT(0);
			ex->Delete();
		}
			sdirfile.Close();
		}

			// Do not expand shareddir_list at runtime; recursion is handled by search logic and tree builder.

	// server list addresses
	strFullPath.Format(_T("%s") _T("addresses.dat"), (LPCTSTR)sConfDir);
	bIsUnicodeFile = IsUnicodeFile(strFullPath);
	if (sdirfile.Open(strFullPath, CFile::modeRead | CFile::shareDenyWrite | (bIsUnicodeFile ? CFile::typeBinary : 0))) {
		try {
			if (bIsUnicodeFile)
				sdirfile.Seek(sizeof(WORD), CFile::current); // skip BOM

			CString toadd;
			while (sdirfile.ReadString(toadd)) {
				toadd.Trim(_T(" \t\r\n")); // need to trim '\r' in binary mode
				if (!toadd.IsEmpty())
					addresses_list.AddTail(toadd);
			}
		}
		catch (CFileException* ex) {
			ASSERT(0);
			ex->Delete();
		}
		sdirfile.Close();
	}

	// Explicitly inform the user about errors with incoming/temp folders!
	if (!::PathFileExists(GetMuleDirectory(EMULE_INCOMINGDIR)) && !::CreateDirectory(GetMuleDirectory(EMULE_INCOMINGDIR), 0)) {
		CString strError;
		strError.Format(GetResString(_T("ERR_CREATE_DIR")), (LPCTSTR)GetResString(_T("PW_INCOMING")), (LPCTSTR)GetMuleDirectory(EMULE_INCOMINGDIR), (LPCTSTR)GetErrorMessage(::GetLastError()));
		CDarkMode::MessageBox(strError, MB_ICONERROR);

		m_strIncomingDir = GetDefaultDirectory(EMULE_INCOMINGDIR, true); // will also try to create it if needed
		if (!::PathFileExists(GetMuleDirectory(EMULE_INCOMINGDIR))) {
			strError.Format(GetResString(_T("ERR_CREATE_DIR")), (LPCTSTR)GetResString(_T("PW_INCOMING")), (LPCTSTR)GetMuleDirectory(EMULE_INCOMINGDIR), (LPCTSTR)GetErrorMessage(::GetLastError()));
			CDarkMode::MessageBox(strError, MB_ICONERROR);
		}
	}
	if (!::PathFileExists(GetTempDir()) && !::CreateDirectory(GetTempDir(), 0)) {
		CString strError;
		strError.Format(GetResString(_T("ERR_CREATE_DIR")), (LPCTSTR)GetResString(_T("PW_TEMP")), GetTempDir(), (LPCTSTR)GetErrorMessage(::GetLastError()));
		CDarkMode::MessageBox(strError, MB_ICONERROR);

		tempdir[0] = GetDefaultDirectory(EMULE_TEMPDIR, true); // will also try to create it if needed;
		if (!::PathFileExists(GetTempDir())) {
			strError.Format(GetResString(_T("ERR_CREATE_DIR")), (LPCTSTR)GetResString(_T("PW_TEMP")), GetTempDir(), (LPCTSTR)GetErrorMessage(::GetLastError()));
			CDarkMode::MessageBox(strError, MB_ICONERROR);
		}
	}
	for (int i = 0; i < tempdir.GetCount(); i++) { // leuk_he: multiple temp dirs for save sources. 
		CString sSourcesPath = CString(thePrefs.GetTempDir(i));
		if (GetSaveLoadSources() && !PathFileExists(sSourcesPath.GetBuffer()) && !::CreateDirectory(sSourcesPath.GetBuffer(), 0)) {
			CString strError;
			strError.Format(_T("Failed to create sources directory \"%s\" - %s"), sSourcesPath, GetErrorMessage(GetLastError()));
			CDarkMode::MessageBox(strError, MB_ICONERROR);
		}
	}

	// Create 'skins' directory
	if (!::PathFileExists(GetMuleDirectory(EMULE_SKINDIR)) && !::CreateDirectory(GetMuleDirectory(EMULE_SKINDIR), 0))
		m_strSkinProfileDir = GetDefaultDirectory(EMULE_SKINDIR, true); // will also try to create it if needed

	// Create 'toolbars' directory
	if (!::PathFileExists(GetMuleDirectory(EMULE_TOOLBARDIR)) && !::CreateDirectory(GetMuleDirectory(EMULE_TOOLBARDIR), 0))
		m_sToolbarBitmapFolder = GetDefaultDirectory(EMULE_TOOLBARDIR, true); // will also try to create it if needed;
}

void CPreferences::LoadBlacklistFile() {
	blacklist_list.RemoveAll(); // Clean list first since this member function will be called on load of settings screen after init.
	const CString& sConfDir(GetMuleDirectory(EMULE_CONFIGDIR));
	CString strFullPath;
	strFullPath.Format(_T("%s") BLACKLISTFILE, (LPCTSTR)sConfDir);

	if (!::PathFileExists(strFullPath)) {
		LogWarning(GetResString(_T("FILE_NOT_FOUND")), (LPCTSTR)BLACKLISTFILE);
		return;
	}

	CStdioFile blacklistFile;
	// check for BOM open the text file either as ANSI (text) or Unicode (binary), this way we can read old and new files with almost the same code.
	if (blacklistFile.Open(strFullPath, CFile::modeRead | CFile::shareDenyWrite | (IsUnicodeFile(strFullPath) ? CFile::typeBinary : 0))) {
		try {
			if (IsUnicodeFile(strFullPath)) // check for BOM
				blacklistFile.Seek(sizeof(WORD), CFile::begin); // skip BOM

			CString m_strCurrentLine = NULL;
			while (blacklistFile.ReadString(m_strCurrentLine)) {
				m_strCurrentLine.Trim(_T("\t\r\n"));
				if (m_strCurrentLine[0] != _T('#') && m_strCurrentLine[0] != _T('\\')) // If this is not a comment or regex definition line, convert it to lower case for case insensitive comparisons.
					m_strCurrentLine.MakeLower();
				if (!m_strCurrentLine.IsEmpty()) // need to trim '\r' in binary mode and then make it lower for case insensitive comparisons.
					blacklist_list.AddTail(m_strCurrentLine);
			}

			Log(GetResString(_T("BLACKLIST_LOADED")), blacklist_list.GetCount());
		} catch (CFileException* ex) {
			ASSERT(0);
			ex->Delete();
			blacklistFile.Close();
			return;
		}
		blacklistFile.Close();
	}

	SaveBlacklistFile();
}

void CPreferences::SaveBlacklistFile()
{
	// Write definitions to the text box
	CString m_strBlacklistDefinitions;
	bool m_bFirstLine = true;
	for (POSITION pos = thePrefs.blacklist_list.GetHeadPosition(); pos != NULL;) {
		CString m_strBlacklistLine = thePrefs.blacklist_list.GetNext(pos);
		if (m_strBlacklistLine.IsEmpty())
			continue;
		if (m_bFirstLine) {
			m_strBlacklistDefinitions.Append(m_strBlacklistLine);
			m_bFirstLine = false;
		} else
			m_strBlacklistDefinitions.Append(_T("\r\n") + m_strBlacklistLine);
	}
	if (theApp.emuledlg && theApp.emuledlg->preferenceswnd && theApp.emuledlg->preferenceswnd->m_wndBlacklistPanel)
		theApp.emuledlg->preferenceswnd->m_wndBlacklistPanel.SetDlgItemText(IDC_BLACKLIST_DEFINITIONS_TEXTBOX, m_strBlacklistDefinitions);

	// Write definitions to the conf file
	static LPCTSTR const stmp = _T(".tmp");
	const CString& sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	const CString& strFullPath(sConfDir + BLACKLISTFILE);
	CStdioFile blacklistFile;
	if (blacklistFile.Open(strFullPath + stmp, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite | CFile::typeBinary)) {
		try {
			// write Unicode byte order mark 0xFEFF
			static const WORD wBOM = u'\xFEFF'; //UTF-16LE
			blacklistFile.Write(&wBOM, sizeof wBOM);
			bool m_bFirstLine = true;
			for (POSITION pos = thePrefs.blacklist_list.GetHeadPosition(); pos != NULL;) {
				if (!m_bFirstLine)
					blacklistFile.Write(_T("\r\n"), 2 * sizeof(TCHAR));
				else
					m_bFirstLine = false;
				blacklistFile.WriteString(thePrefs.blacklist_list.GetNext(pos));
			}
			blacklistFile.Close();
			if (MoveFileEx(strFullPath + stmp, strFullPath, MOVEFILE_REPLACE_EXISTING) == 0)
				AddDebugLogLine(false, _T("Failed to move %s to %s"), (LPCTSTR)EscPercent(strFullPath + stmp), (LPCTSTR)EscPercent(strFullPath));
		} catch (CFileException* ex) {
			if (thePrefs.GetVerbose())
				AddDebugLogLine(true, _T("Failed to move %s to %s: %s"), (LPCTSTR)EscPercent(strFullPath + stmp), (LPCTSTR)EscPercent(strFullPath), (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
			ex->Delete();
		}
	} else
		AddDebugLogLine(false, _T("Failed to open %s"), (LPCTSTR)EscPercent(strFullPath + stmp));
}

void CPreferences::Uninit()
{
	for (INT_PTR i = catArr.GetCount(); --i >= 0;) {
		delete catArr[i];
		catArr.RemoveAt(i);
	}
	
	catArr.RemoveAll(); // Release CArray internal storage to avoid CPlex leaks

	// Free dynamically allocated preference structures before CRT leak detection kicks in
	thePrefs.ReleaseExtStruct();
}

void CPreferences::SetStandardValues()
{
	WINDOWPLACEMENT defaultWPM;
	BuildDefaultMainWindowPlacement(defaultWPM);
	EmuleWindowPlacement = defaultWPM;
	versioncheckLastAutomatic = 0;
	versioncheckLastKnownVersionOnServer.Empty();
}

bool CPreferences::IsTempFile(const CString& rstrDirectory, const CString& rstrName)
{
	bool bFound = false;
	for (INT_PTR i = tempdir.GetCount(); --i >= 0;)
		if (EqualPaths(rstrDirectory, GetTempDir(i))) {
			bFound = true; //OK, found a directory
			break;
		}

	if (!bFound) //not found - not a tempfile...
		return false;

	// do not share a file from the temp directory, if it matches one of the following patterns
	CString strNameLower(rstrName);
	strNameLower.MakeLower();
	strNameLower += _T('|'); // append an EOS character which we can query for
	static LPCTSTR const _apszNotSharedExts[] = {
		_T("%u.part") _T("%c"),
		_T("%u.part.met") _T("%c"),
		_T("%u.part.met") PARTMET_BAK_EXT _T("%c"),
		_T("%u.part.met") PARTMET_TMP_EXT _T("%c")
	};
	for (unsigned i = 0; i < _countof(_apszNotSharedExts); ++i) {
		UINT uNum;
		TCHAR iChar;
		// "misuse" the 'scanf' function for a very simple pattern scanning.
		if (_stscanf(strNameLower, _apszNotSharedExts[i], &uNum, &iChar) == 2 && iChar == _T('|'))
			return true;
	}

	return false;
}

uint32 CPreferences::GetMaxDownload()
{
	return (uint32)(GetMaxDownloadInBytesPerSec() / 1024);
}

uint64 CPreferences::GetMaxDownloadInBytesPerSec(bool dynamic)
{
	//don't be a Lam3r :)
	uint64 maxup;
	if (dynamic && thePrefs.IsDynUpEnabled() && theApp.uploadqueue->GetWaitingUserCount() > 0 && theApp.uploadqueue->GetDatarate() > 0)
		maxup = theApp.uploadqueue->GetDatarate();
	else
		maxup = GetMaxUpload() * 1024ull;

	uint64 maxdown = m_maxdownload * 1024ull;
	if (maxup >= 20 * 1024)
		return maxdown;

	uint32 coef;
	if (maxup < 4 * 1024)
		coef = 3;
	else if (maxup < 10 * 1024)
		coef = 4;
	else
		coef = 5;

	return min(coef * maxup, maxdown);
}

void CPreferences::SaveStats(int bBackUp)
{
	// This function saves all of the new statistics in my addon.  It is also used to
	// save backups for the Reset Stats function, and the Restore Stats function (Which is actually LoadStats)
	// bBackUp = 0: DEFAULT; save to statistics.ini
	// bBackUp = 1: Save to statbkup.ini, which is used to restore after a reset
	// bBackUp = 2: Save to statbkuptmp.ini, which is temporarily created during a restore and then renamed to statbkup.ini

	LPCTSTR p;
	if (bBackUp == 1)
		p = _T("statbkup.ini");
	else if (bBackUp == 2)
		p = _T("statbkuptmp.ini");
	else
		p = _T("statistics.ini");
	const CString& strFullPath(GetMuleDirectory(EMULE_CONFIGDIR) + p);

	CIni ini(strFullPath, _T("Statistics"));

	// Save cumulative statistics to statistics.ini, going in the order they appear in CStatisticsDlg::ShowStatistics.
	// We do NOT SET the values in prefs struct here.

	// Save Cum Down Data
	ini.WriteUInt64(_T("TotalDownloadedBytes"), theStats.sessionReceivedBytes + GetTotalDownloaded());
	ini.WriteInt(_T("DownSuccessfulSessions"), cumDownSuccessfulSessions);
	ini.WriteInt(_T("DownFailedSessions"), cumDownFailedSessions);
	ini.WriteInt(_T("DownAvgTime"), GetDownC_AvgTime()); //never needed this
	ini.WriteUInt64(_T("LostFromCorruption"), cumLostFromCorruption + sesLostFromCorruption);
	ini.WriteUInt64(_T("SavedFromCompression"), sesSavedFromCompression + cumSavedFromCompression);
	ini.WriteInt(_T("PartsSavedByICH"), cumPartsSavedByICH + sesPartsSavedByICH);

	ini.WriteUInt64(_T("DownData_EDONKEY"), GetCumDownData_EDONKEY());
	ini.WriteUInt64(_T("DownData_EDONKEYHYBRID"), GetCumDownData_EDONKEYHYBRID());
	ini.WriteUInt64(_T("DownData_EMULE"), GetCumDownData_EMULE());
	ini.WriteUInt64(_T("DownData_MLDONKEY"), GetCumDownData_MLDONKEY());
	ini.WriteUInt64(_T("DownData_LMULE"), GetCumDownData_EMULECOMPAT());
	ini.WriteUInt64(_T("DownData_AMULE"), GetCumDownData_AMULE());
	ini.WriteUInt64(_T("DownData_SHAREAZA"), GetCumDownData_SHAREAZA());
	ini.WriteUInt64(_T("DownData_URL"), GetCumDownData_URL());
	ini.WriteUInt64(_T("DownDataPort_4662"), GetCumDownDataPort_4662());
	ini.WriteUInt64(_T("DownDataPort_OTHER"), GetCumDownDataPort_OTHER());

	ini.WriteUInt64(_T("DownOverheadTotal"), theStats.GetDownDataOverheadFileRequest()
		+ theStats.GetDownDataOverheadSourceExchange()
		+ theStats.GetDownDataOverheadServer()
		+ theStats.GetDownDataOverheadKad()
		+ theStats.GetDownDataOverheadOther()
		+ GetDownOverheadTotal());
	ini.WriteUInt64(_T("DownOverheadFileReq"), theStats.GetDownDataOverheadFileRequest() + GetDownOverheadFileReq());
	ini.WriteUInt64(_T("DownOverheadSrcEx"), theStats.GetDownDataOverheadSourceExchange() + GetDownOverheadSrcEx());
	ini.WriteUInt64(_T("DownOverheadServer"), theStats.GetDownDataOverheadServer() + GetDownOverheadServer());
	ini.WriteUInt64(_T("DownOverheadKad"), theStats.GetDownDataOverheadKad() + GetDownOverheadKad());

	ini.WriteUInt64(_T("DownOverheadTotalPackets"), theStats.GetDownDataOverheadFileRequestPackets()
		+ theStats.GetDownDataOverheadSourceExchangePackets()
		+ theStats.GetDownDataOverheadServerPackets()
		+ theStats.GetDownDataOverheadKadPackets()
		+ theStats.GetDownDataOverheadOtherPackets()
		+ GetDownOverheadTotalPackets());
	ini.WriteUInt64(_T("DownOverheadFileReqPackets"), theStats.GetDownDataOverheadFileRequestPackets() + GetDownOverheadFileReqPackets());
	ini.WriteUInt64(_T("DownOverheadSrcExPackets"), theStats.GetDownDataOverheadSourceExchangePackets() + GetDownOverheadSrcExPackets());
	ini.WriteUInt64(_T("DownOverheadServerPackets"), theStats.GetDownDataOverheadServerPackets() + GetDownOverheadServerPackets());
	ini.WriteUInt64(_T("DownOverheadKadPackets"), theStats.GetDownDataOverheadKadPackets() + GetDownOverheadKadPackets());

	// Save Cumulative Upline Statistics
	ini.WriteUInt64(_T("TotalUploadedBytes"), theStats.sessionSentBytes + GetTotalUploaded());
	ini.WriteInt(_T("UpSuccessfulSessions"), theApp.uploadqueue->GetSuccessfullUpCount() + GetUpSuccessfulSessions());
	ini.WriteInt(_T("UpFailedSessions"), theApp.uploadqueue->GetFailedUpCount() + GetUpFailedSessions());
	ini.WriteInt(_T("UpAvgTime"), GetUpAvgTime()); //never needed this
	ini.WriteUInt64(_T("UpData_EDONKEY"), GetCumUpData_EDONKEY());
	ini.WriteUInt64(_T("UpData_EDONKEYHYBRID"), GetCumUpData_EDONKEYHYBRID());
	ini.WriteUInt64(_T("UpData_EMULE"), GetCumUpData_EMULE());
	ini.WriteUInt64(_T("UpData_MLDONKEY"), GetCumUpData_MLDONKEY());
	ini.WriteUInt64(_T("UpData_LMULE"), GetCumUpData_EMULECOMPAT());
	ini.WriteUInt64(_T("UpData_AMULE"), GetCumUpData_AMULE());
	ini.WriteUInt64(_T("UpData_SHAREAZA"), GetCumUpData_SHAREAZA());
	ini.WriteUInt64(_T("UpDataPort_4662"), GetCumUpDataPort_4662());
	ini.WriteUInt64(_T("UpDataPort_OTHER"), GetCumUpDataPort_OTHER());
	ini.WriteUInt64(_T("UpData_File"), GetCumUpData_File());
	ini.WriteUInt64(_T("UpData_Partfile"), GetCumUpData_Partfile());

	ini.WriteUInt64(_T("UpOverheadTotal"), theStats.GetUpDataOverheadFileRequest()
		+ theStats.GetUpDataOverheadSourceExchange()
		+ theStats.GetUpDataOverheadServer()
		+ theStats.GetUpDataOverheadKad()
		+ theStats.GetUpDataOverheadOther()
		+ GetUpOverheadTotal());
	ini.WriteUInt64(_T("UpOverheadFileReq"), theStats.GetUpDataOverheadFileRequest() + GetUpOverheadFileReq());
	ini.WriteUInt64(_T("UpOverheadSrcEx"), theStats.GetUpDataOverheadSourceExchange() + GetUpOverheadSrcEx());
	ini.WriteUInt64(_T("UpOverheadServer"), theStats.GetUpDataOverheadServer() + GetUpOverheadServer());
	ini.WriteUInt64(_T("UpOverheadKad"), theStats.GetUpDataOverheadKad() + GetUpOverheadKad());

	ini.WriteUInt64(_T("UpOverheadTotalPackets"), theStats.GetUpDataOverheadFileRequestPackets()
		+ theStats.GetUpDataOverheadSourceExchangePackets()
		+ theStats.GetUpDataOverheadServerPackets()
		+ theStats.GetUpDataOverheadKadPackets()
		+ theStats.GetUpDataOverheadOtherPackets()
		+ GetUpOverheadTotalPackets());
	ini.WriteUInt64(_T("UpOverheadFileReqPackets"), theStats.GetUpDataOverheadFileRequestPackets() + GetUpOverheadFileReqPackets());
	ini.WriteUInt64(_T("UpOverheadSrcExPackets"), theStats.GetUpDataOverheadSourceExchangePackets() + GetUpOverheadSrcExPackets());
	ini.WriteUInt64(_T("UpOverheadServerPackets"), theStats.GetUpDataOverheadServerPackets() + GetUpOverheadServerPackets());
	ini.WriteUInt64(_T("UpOverheadKadPackets"), theStats.GetUpDataOverheadKadPackets() + GetUpOverheadKadPackets());

	// Save Cumulative Connection Statistics

	// Download Rate Average
	float tempRate = theStats.GetAvgDownloadRate(AVG_TOTAL);
	ini.WriteFloat(_T("ConnAvgDownRate"), tempRate);

	// Max Download Rate Average
	if (tempRate > GetConnMaxAvgDownRate())
		SetConnMaxAvgDownRate(tempRate);
	ini.WriteFloat(_T("ConnMaxAvgDownRate"), GetConnMaxAvgDownRate());

	// Max Download Rate
	tempRate = theApp.downloadqueue->GetDatarate() / 1024.0f;
	if (tempRate > GetConnMaxDownRate())
		SetConnMaxDownRate(tempRate);
	ini.WriteFloat(_T("ConnMaxDownRate"), GetConnMaxDownRate());

	// Upload Rate Average
	tempRate = theStats.GetAvgUploadRate(AVG_TOTAL);
	ini.WriteFloat(_T("ConnAvgUpRate"), tempRate);

	// Max Upload Rate Average
	if (tempRate > GetConnMaxAvgUpRate())
		SetConnMaxAvgUpRate(tempRate);
	ini.WriteFloat(_T("ConnMaxAvgUpRate"), GetConnMaxAvgUpRate());

	// Max Upload Rate
	tempRate = theApp.uploadqueue->GetDatarate() / 1024.0f;
	if (tempRate > GetConnMaxUpRate())
		SetConnMaxUpRate(tempRate);
	ini.WriteFloat(_T("ConnMaxUpRate"), GetConnMaxUpRate());

	// Overall Run Time
	ini.WriteInt(_T("ConnRunTime"), (UINT)((::GetTickCount() - theStats.starttime) / SEC2MS(1) + GetConnRunTime()));

	// Number of Reconnects
	ini.WriteInt(_T("ConnNumReconnects"), GetConnNumReconnects() + theStats.reconnects - static_cast<uint32>(theStats.reconnects > 0));

	// Average Connections
	if (theApp.serverconnect->IsConnected())
		ini.WriteInt(_T("ConnAvgConnections"), (UINT)((theApp.listensocket->GetAverageConnections() + cumConnAvgConnections) / 2));

	// Peak Connections
	if (theApp.listensocket->GetPeakConnections() > cumConnPeakConnections)
		cumConnPeakConnections = theApp.listensocket->GetPeakConnections();
	ini.WriteInt(_T("ConnPeakConnections"), cumConnPeakConnections);

	// Max Connection Limit Reached
	if (theApp.listensocket->GetMaxConnectionReached() > 0)
		ini.WriteInt(_T("ConnMaxConnLimitReached"), theApp.listensocket->GetMaxConnectionReached() + cumConnMaxConnLimitReached);

	// Time Stuff...
	ini.WriteInt(_T("ConnTransferTime"), GetConnTransferTime() + theStats.GetTransferTime());
	ini.WriteInt(_T("ConnUploadTime"), GetConnUploadTime() + theStats.GetUploadTime());
	ini.WriteInt(_T("ConnDownloadTime"), GetConnDownloadTime() + theStats.GetDownloadTime());
	ini.WriteInt(_T("ConnServerDuration"), GetConnServerDuration() + theStats.GetServerDuration());

	// Compare and Save Server Records
	uint32 servtotal, servfail, servuser, servfile, servlowiduser, servtuser, servtfile;
	float servocc;
	theApp.serverlist->GetStatus(servtotal, servfail, servuser, servfile, servlowiduser, servtuser, servtfile, servocc);

	if (servtotal - servfail > cumSrvrsMostWorkingServers)
		cumSrvrsMostWorkingServers = servtotal - servfail;
	ini.WriteInt(_T("SrvrsMostWorkingServers"), cumSrvrsMostWorkingServers);

	if (servtuser > cumSrvrsMostUsersOnline)
		cumSrvrsMostUsersOnline = servtuser;
	ini.WriteInt(_T("SrvrsMostUsersOnline"), cumSrvrsMostUsersOnline);

	if (servtfile > cumSrvrsMostFilesAvail)
		cumSrvrsMostFilesAvail = servtfile;
	ini.WriteInt(_T("SrvrsMostFilesAvail"), cumSrvrsMostFilesAvail);

	// Compare and Save Shared File Records
	if ((uint32)theApp.sharedfiles->GetCount() > cumSharedMostFilesShared)
		cumSharedMostFilesShared = (uint32)theApp.sharedfiles->GetCount();
	ini.WriteInt(_T("SharedMostFilesShared"), cumSharedMostFilesShared);

	uint64 bytesLargestFile = 0;
	uint64 allsize = theApp.sharedfiles->GetDatasize(bytesLargestFile);
	if (allsize > cumSharedLargestShareSize)
		cumSharedLargestShareSize = allsize;
	ini.WriteUInt64(_T("SharedLargestShareSize"), cumSharedLargestShareSize);
	if (bytesLargestFile > cumSharedLargestFileSize)
		cumSharedLargestFileSize = bytesLargestFile;
	ini.WriteUInt64(_T("SharedLargestFileSize"), cumSharedLargestFileSize);

	if (theApp.sharedfiles->GetCount() != 0) {
		uint64 tempint = allsize / theApp.sharedfiles->GetCount();
		if (tempint > cumSharedLargestAvgFileSize)
			cumSharedLargestAvgFileSize = tempint;
	}

	ini.WriteUInt64(_T("SharedLargestAvgFileSize"), cumSharedLargestAvgFileSize);
	ini.WriteInt(_T("statsDateTimeLastReset"), (int)stat_datetimeLastReset);

	// If we are saving a back-up or a temporary back-up, return now.
	//	return;
}

bool CPreferences::SaveSharedFolders()
{
	const CString& sConfDir(GetMuleDirectory(EMULE_CONFIGDIR));
	const CString& strSharesPath(sConfDir + SHAREDDIRS);
	static LPCTSTR const stmp = _T(".tmp");
	bool bError = false;

	// Collapse into minimal roots before persisting
	CollapseSharedDirsToRoots();

	CStdioFile file;
	if (file.Open(strSharesPath + stmp, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite | CFile::typeBinary)) {
		try {
			// write UTF-16LE byte order mark 0xFEFF
			static const WORD wBOM = u'\xFEFF';
			file.Write(&wBOM, sizeof wBOM);
			for (POSITION pos = shareddir_list.GetHeadPosition(); pos != NULL;) {
				file.WriteString(shareddir_list.GetNext(pos));
				file.Write(_T("\r\n"), 2 * sizeof(TCHAR));
			}
			file.Close();
			bError = (MoveFileEx(strSharesPath + stmp, strSharesPath, MOVEFILE_REPLACE_EXISTING) == 0);
		} catch (CFileException *ex) {
			if (thePrefs.GetVerbose())
				AddDebugLogLine(true, _T("Failed to save %s%s"), (LPCTSTR)EscPercent(strSharesPath), (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
			ex->Delete();
		}
	} else
		bError = true;

	return bError;
}

// Build a minimal set of roots by removing entries which are subdirectories of others (in-memory only).
void CPreferences::CollapseSharedDirsToRoots()
{
	CStringList collapsed;
	for (POSITION p = shareddir_list.GetHeadPosition(); p != NULL;) {
		const CString cur = shareddir_list.GetNext(p);
		bool isChild = false;
		for (POSITION q = shareddir_list.GetHeadPosition(); q != NULL;) {
			const CString other = shareddir_list.GetNext(q);
			if (!EqualPaths(cur, other) && IsSubDirectoryOf(cur, other)) { isChild = true; break; }
		}
		if (!isChild)
			collapsed.AddTail(cur);
	}
	shareddir_list.RemoveAll();
	shareddir_list.AddTail(&collapsed);
}

// Long-path aware recursive expansion of shared roots for UI usage (in-memory only, not persisted).
void CPreferences::ExpandSharedDirsForUI()
{
	if (shareddir_list.IsEmpty())
		return;

	CStringList addList;
	for (POSITION pos = shareddir_list.GetHeadPosition(); pos != NULL;) {
		CString root = shareddir_list.GetNext(pos);
		MakeFoldername(root);
		CList<CString, const CString&> stack;
		stack.AddTail(root);
		while (!stack.IsEmpty()) {
			CString cur = stack.RemoveHead();
			const CString pattern = PreparePathForWin32LongPath(cur + _T("*"));
			WIN32_FIND_DATA wfd = {};
			HANDLE hFind = ::FindFirstFileExW(pattern, FindExInfoBasic, &wfd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
			if (hFind == INVALID_HANDLE_VALUE)
				continue;
			do {
				const DWORD attr = wfd.dwFileAttributes;
				if ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
					const CString name(wfd.cFileName);
					if (name != _T(".") && name != _T("..")) {
						CString sub = cur + name;
						MakeFoldername(sub);
						if (IsShareableDirectory(sub)) {
							bool exists = false;
							for (POSITION chk = shareddir_list.GetHeadPosition(); chk != NULL; )
								if (EqualPaths(shareddir_list.GetNext(chk), sub)) { exists = true; break; }
							if (!exists)
								addList.AddTail(sub);
							stack.AddTail(sub);
						}
					}
				}
			} while (::FindNextFileW(hFind, &wfd));
			::FindClose(hFind);
		}
	}
	if (!addList.IsEmpty())
		shareddir_list.AddTail(&addList);
}

void CPreferences::SetRecordStructMembers()
{
	// The purpose of this function is to be called from CStatisticsDlg::ShowStatistics()
	// This was easier than making a bunch of functions to interface with the record
	// members of the prefs struct from ShowStatistics.

	// This function is going to compare current values with previously saved records, and if
	// the current values are greater, the corresponding member of prefs will be updated.
	// We will not write to INI here, because this code is going to be called a lot more often
	// than SaveStats()  - Khaos

	// Servers
	uint32 servtotal, servfail, servuser, servfile, servlowiduser, servtuser, servtfile;
	float servocc;
	theApp.serverlist->GetStatus(servtotal, servfail, servuser, servfile, servlowiduser, servtuser, servtfile, servocc);
	if ((servtotal - servfail) > cumSrvrsMostWorkingServers)
		cumSrvrsMostWorkingServers = (servtotal - servfail);
	if (servtuser > cumSrvrsMostUsersOnline)
		cumSrvrsMostUsersOnline = servtuser;
	if (servtfile > cumSrvrsMostFilesAvail)
		cumSrvrsMostFilesAvail = servtfile;

	// Shared Files
	if ((uint32)theApp.sharedfiles->GetCount() > cumSharedMostFilesShared)
		cumSharedMostFilesShared = (uint32)theApp.sharedfiles->GetCount();
	uint64 bytesLargestFile = 0;
	uint64 allsize = theApp.sharedfiles->GetDatasize(bytesLargestFile);
	if (allsize > cumSharedLargestShareSize)
		cumSharedLargestShareSize = allsize;
	if (bytesLargestFile > cumSharedLargestFileSize)
		cumSharedLargestFileSize = bytesLargestFile;
	if (theApp.sharedfiles->GetCount() != 0) {
		uint64 tempint = allsize / theApp.sharedfiles->GetCount();
		if (tempint > cumSharedLargestAvgFileSize)
			cumSharedLargestAvgFileSize = tempint;
	}
} // SetRecordStructMembers()

void CPreferences::SaveCompletedDownloadsStat()
{
	// This function saves the values for the completed
	// download members to INI.  It is called from

	CIni ini(GetMuleDirectory(EMULE_CONFIGDIR) + _T("statistics.ini"), _T("Statistics"));

	ini.WriteInt(_T("DownCompletedFiles"), GetDownCompletedFiles());
	ini.WriteInt(_T("DownSessionCompletedFiles"), GetDownSessionCompletedFiles());
} // SaveCompletedDownloadsStat()

void CPreferences::Add2SessionTransferData(UINT uClientID, UINT uClientPort, BOOL bFromPF,
	BOOL bUpDown, uint32 bytes, bool sentToFriend)
{
	//	This function adds the transferred bytes to the appropriate variables,
	//	as well as to the totals for all clients. - Khaos
	//	PARAMETERS:
	//	uClientID - The identifier for which client software sent or received this data, e.g. SO_EMULE
	//	uClientPort - The remote port of the client that sent or received this data, e.g. 4662
	//	bFromPF - Applies only to uploads.  True is from partfile, False is from non-partfile.
	//	bUpDown - True is Up, False is Down
	//	bytes - Number of bytes sent by the client.  Subtract header before calling.

	if (bUpDown) {
		//	Upline Data
		switch (uClientID) {
			// Update session client breakdown stats for sent bytes...
		case SO_EMULE:
		case SO_OLDEMULE:
			sesUpData_EMULE += bytes;
			break;
		case SO_EDONKEYHYBRID:
			sesUpData_EDONKEYHYBRID += bytes;
			break;
		case SO_EDONKEY:
			sesUpData_EDONKEY += bytes;
			break;
		case SO_MLDONKEY:
			sesUpData_MLDONKEY += bytes;
			break;
		case SO_AMULE:
			sesUpData_AMULE += bytes;
			break;
		case SO_SHAREAZA:
			sesUpData_SHAREAZA += bytes;
			break;
		case SO_HYDRANODE:
		case SO_HYDRA:
		case SO_EMULEPLUS:
		case SO_TRUSTYFILES:
		case SO_EASYMULE2:
		case SO_NEOLOADER:
		case SO_KMULE:
		case SO_CDONKEY:
		case SO_LPHANT:
		case SO_XMULE:
			sesUpData_EMULECOMPAT += bytes;
		}

		switch (uClientPort) {
			// Update session port breakdown stats for sent bytes...
		case 4662:
			sesUpDataPort_4662 += bytes;
			break;
		default:
			sesUpDataPort_OTHER += bytes;
		}

		if (bFromPF)
			sesUpData_Partfile += bytes;
		else
			sesUpData_File += bytes;

		//	Add to our total for sent bytes...
		theApp.UpdateSentBytes(bytes, sentToFriend);
	}
	else {
		// Downline Data
		switch (uClientID) {
			// Update session client breakdown stats for received bytes...
		case SO_EMULE:
		case SO_OLDEMULE:
			sesDownData_EMULE += bytes;
			break;
		case SO_EDONKEYHYBRID:
			sesDownData_EDONKEYHYBRID += bytes;
			break;
		case SO_EDONKEY:
			sesDownData_EDONKEY += bytes;
			break;
		case SO_MLDONKEY:
			sesDownData_MLDONKEY += bytes;
			break;
		case SO_AMULE:
			sesDownData_AMULE += bytes;
			break;
		case SO_SHAREAZA:
			sesDownData_SHAREAZA += bytes;
			break;
		case SO_HYDRANODE:
		case SO_HYDRA:
		case SO_EMULEPLUS:
		case SO_TRUSTYFILES:
		case SO_EASYMULE2:
		case SO_NEOLOADER:
		case SO_KMULE:
		case SO_CDONKEY:
		case SO_LPHANT:
		case SO_XMULE:
			sesDownData_EMULECOMPAT += bytes;
			break;
		case SO_URL:
			sesDownData_URL += bytes;
		}

		switch (uClientPort) {
			// Update session port breakdown stats for received bytes...
			// For now we are only going to break it down by default and non-default.
			// A statistical analysis of all data sent from every single port/domain is
			// beyond the scope of this add-on.
		case 4662:
			sesDownDataPort_4662 += bytes;
			break;
		default:
			sesDownDataPort_OTHER += bytes;
			break;
		}

		//	Add to our total for received bytes...
		theApp.UpdateReceivedBytes(bytes);
	}
}

// Reset Statistics by Khaos

void CPreferences::ResetCumulativeStatistics()
{

	// Save a backup so that we can undo this action
	SaveStats(1);

	// SET ALL CUMULATIVE STAT VALUES TO 0  :'-(

	totalDownloadedBytes = 0;
	totalUploadedBytes = 0;
	cumDownOverheadTotal = 0;
	cumDownOverheadFileReq = 0;
	cumDownOverheadSrcEx = 0;
	cumDownOverheadServer = 0;
	cumDownOverheadKad = 0;
	cumDownOverheadTotalPackets = 0;
	cumDownOverheadFileReqPackets = 0;
	cumDownOverheadSrcExPackets = 0;
	cumDownOverheadServerPackets = 0;
	cumDownOverheadKadPackets = 0;
	cumUpOverheadTotal = 0;
	cumUpOverheadFileReq = 0;
	cumUpOverheadSrcEx = 0;
	cumUpOverheadServer = 0;
	cumUpOverheadKad = 0;
	cumUpOverheadTotalPackets = 0;
	cumUpOverheadFileReqPackets = 0;
	cumUpOverheadSrcExPackets = 0;
	cumUpOverheadServerPackets = 0;
	cumUpOverheadKadPackets = 0;
	cumUpSuccessfulSessions = 0;
	cumUpFailedSessions = 0;
	cumUpAvgTime = 0;
	cumUpData_EDONKEY = 0;
	cumUpData_EDONKEYHYBRID = 0;
	cumUpData_EMULE = 0;
	cumUpData_MLDONKEY = 0;
	cumUpData_AMULE = 0;
	cumUpData_EMULECOMPAT = 0;
	cumUpData_SHAREAZA = 0;
	cumUpDataPort_4662 = 0;
	cumUpDataPort_OTHER = 0;
	cumDownCompletedFiles = 0;
	cumDownSuccessfulSessions = 0;
	cumDownFailedSessions = 0;
	cumDownAvgTime = 0;
	cumLostFromCorruption = 0;
	cumSavedFromCompression = 0;
	cumPartsSavedByICH = 0;
	cumDownData_EDONKEY = 0;
	cumDownData_EDONKEYHYBRID = 0;
	cumDownData_EMULE = 0;
	cumDownData_MLDONKEY = 0;
	cumDownData_AMULE = 0;
	cumDownData_EMULECOMPAT = 0;
	cumDownData_SHAREAZA = 0;
	cumDownData_URL = 0;
	cumDownDataPort_4662 = 0;
	cumDownDataPort_OTHER = 0;
	cumConnAvgDownRate = 0;
	cumConnMaxAvgDownRate = 0;
	cumConnMaxDownRate = 0;
	cumConnAvgUpRate = 0;
	cumConnRunTime = 0;
	cumConnNumReconnects = 0;
	cumConnAvgConnections = 0;
	cumConnMaxConnLimitReached = 0;
	cumConnPeakConnections = 0;
	cumConnDownloadTime = 0;
	cumConnUploadTime = 0;
	cumConnTransferTime = 0;
	cumConnServerDuration = 0;
	cumConnMaxAvgUpRate = 0;
	cumConnMaxUpRate = 0;
	cumSrvrsMostWorkingServers = 0;
	cumSrvrsMostUsersOnline = 0;
	cumSrvrsMostFilesAvail = 0;
	cumSharedMostFilesShared = 0;
	cumSharedLargestShareSize = 0;
	cumSharedLargestAvgFileSize = 0;

	// Set the time of last reset...
	time_t timeNow;
	time(&timeNow);
	stat_datetimeLastReset = timeNow;

	// Save the reset stats
	SaveStats();
	theApp.emuledlg->statisticswnd->ShowStatistics(true);
}


// Load Statistics
// This used to be integrated in LoadPreferences, but it has been altered
// so that it can be used to load the backup created when the stats are reset.
// Last Modified: 2-22-03 by Khaos
bool CPreferences::LoadStats(int loadBackUp)
{
	// loadBackUp is 0 by default
	// loadBackUp = 0: Load the stats normally like we used to do in LoadPreferences
	// loadBackUp = 1: Load the stats from statbkup.ini and create a backup of the current stats.  Also, do not initialize session variables.
	const CString& sConfDir(GetMuleDirectory(EMULE_CONFIGDIR));
	CFileFind findBackUp;

	CString sINI(sConfDir);
	switch (loadBackUp) {
	case 1:
		sINI += _T("statbkup.ini");
		if (!findBackUp.FindFile(sINI))
			return false;
		SaveStats(2); // Save our temp backup of current values to statbkuptmp.ini, we will be renaming it at the end of this function.
		break;
	case 0:
	default:
		// for transition...
		if (::PathFileExists(sINI + _T("statistics.ini")))
			sINI += _T("statistics.ini");
		else
			sINI += _T("preferences.ini");
	}

	BOOL fileex = ::PathFileExists(sINI);
	CIni ini(sINI, _T("Statistics"));

	totalDownloadedBytes = ini.GetUInt64(_T("TotalDownloadedBytes"));
	totalUploadedBytes = ini.GetUInt64(_T("TotalUploadedBytes"));

	// Load stats for cumulative downline overhead
	cumDownOverheadTotal = ini.GetUInt64(_T("DownOverheadTotal"));
	cumDownOverheadFileReq = ini.GetUInt64(_T("DownOverheadFileReq"));
	cumDownOverheadSrcEx = ini.GetUInt64(_T("DownOverheadSrcEx"));
	cumDownOverheadServer = ini.GetUInt64(_T("DownOverheadServer"));
	cumDownOverheadKad = ini.GetUInt64(_T("DownOverheadKad"));
	cumDownOverheadTotalPackets = ini.GetUInt64(_T("DownOverheadTotalPackets"));
	cumDownOverheadFileReqPackets = ini.GetUInt64(_T("DownOverheadFileReqPackets"));
	cumDownOverheadSrcExPackets = ini.GetUInt64(_T("DownOverheadSrcExPackets"));
	cumDownOverheadServerPackets = ini.GetUInt64(_T("DownOverheadServerPackets"));
	cumDownOverheadKadPackets = ini.GetUInt64(_T("DownOverheadKadPackets"));

	// Load stats for cumulative upline overhead
	cumUpOverheadTotal = ini.GetUInt64(_T("UpOverHeadTotal"));
	cumUpOverheadFileReq = ini.GetUInt64(_T("UpOverheadFileReq"));
	cumUpOverheadSrcEx = ini.GetUInt64(_T("UpOverheadSrcEx"));
	cumUpOverheadServer = ini.GetUInt64(_T("UpOverheadServer"));
	cumUpOverheadKad = ini.GetUInt64(_T("UpOverheadKad"));
	cumUpOverheadTotalPackets = ini.GetUInt64(_T("UpOverHeadTotalPackets"));
	cumUpOverheadFileReqPackets = ini.GetUInt64(_T("UpOverheadFileReqPackets"));
	cumUpOverheadSrcExPackets = ini.GetUInt64(_T("UpOverheadSrcExPackets"));
	cumUpOverheadServerPackets = ini.GetUInt64(_T("UpOverheadServerPackets"));
	cumUpOverheadKadPackets = ini.GetUInt64(_T("UpOverheadKadPackets"));

	// Load stats for cumulative upline data
	cumUpSuccessfulSessions = ini.GetInt(_T("UpSuccessfulSessions"));
	cumUpFailedSessions = ini.GetInt(_T("UpFailedSessions"));
	cumUpAvgTime = ini.GetInt(_T("UpAvgTime"));

	// Load cumulative client breakdown stats for sent bytes
	cumUpData_EDONKEY = ini.GetUInt64(_T("UpData_EDONKEY"));
	cumUpData_EDONKEYHYBRID = ini.GetUInt64(_T("UpData_EDONKEYHYBRID"));
	cumUpData_EMULE = ini.GetUInt64(_T("UpData_EMULE"));
	cumUpData_MLDONKEY = ini.GetUInt64(_T("UpData_MLDONKEY"));
	cumUpData_EMULECOMPAT = ini.GetUInt64(_T("UpData_LMULE"));
	cumUpData_AMULE = ini.GetUInt64(_T("UpData_AMULE"));
	cumUpData_SHAREAZA = ini.GetUInt64(_T("UpData_SHAREAZA"));

	// Load cumulative port breakdown stats for sent bytes
	cumUpDataPort_4662 = ini.GetUInt64(_T("UpDataPort_4662"));
	cumUpDataPort_OTHER = ini.GetUInt64(_T("UpDataPort_OTHER"));

	// Load cumulative source breakdown stats for sent bytes
	cumUpData_File = ini.GetUInt64(_T("UpData_File"));
	cumUpData_Partfile = ini.GetUInt64(_T("UpData_Partfile"));

	// Load stats for cumulative downline data
	cumDownCompletedFiles = ini.GetInt(_T("DownCompletedFiles"));
	cumDownSuccessfulSessions = ini.GetInt(_T("DownSuccessfulSessions"));
	cumDownFailedSessions = ini.GetInt(_T("DownFailedSessions"));
	cumDownAvgTime = ini.GetInt(_T("DownAvgTime")); //never needed this

	// Cumulative statistics for saved due to compression/lost due to corruption
	cumLostFromCorruption = ini.GetUInt64(_T("LostFromCorruption"));
	cumSavedFromCompression = ini.GetUInt64(_T("SavedFromCompression"));
	cumPartsSavedByICH = ini.GetInt(_T("PartsSavedByICH"));

	// Load cumulative client breakdown stats for received bytes
	cumDownData_EDONKEY = ini.GetUInt64(_T("DownData_EDONKEY"));
	cumDownData_EDONKEYHYBRID = ini.GetUInt64(_T("DownData_EDONKEYHYBRID"));
	cumDownData_EMULE = ini.GetUInt64(_T("DownData_EMULE"));
	cumDownData_MLDONKEY = ini.GetUInt64(_T("DownData_MLDONKEY"));
	cumDownData_EMULECOMPAT = ini.GetUInt64(_T("DownData_LMULE"));
	cumDownData_AMULE = ini.GetUInt64(_T("DownData_AMULE"));
	cumDownData_SHAREAZA = ini.GetUInt64(_T("DownData_SHAREAZA"));
	cumDownData_URL = ini.GetUInt64(_T("DownData_URL"));

	// Load cumulative port breakdown stats for received bytes
	cumDownDataPort_4662 = ini.GetUInt64(_T("DownDataPort_4662"));
	cumDownDataPort_OTHER = ini.GetUInt64(_T("DownDataPort_OTHER"));

	// Load stats for cumulative connection data
	cumConnAvgDownRate = ini.GetFloat(_T("ConnAvgDownRate"));
	cumConnMaxAvgDownRate = ini.GetFloat(_T("ConnMaxAvgDownRate"));
	cumConnMaxDownRate = ini.GetFloat(_T("ConnMaxDownRate"));
	cumConnAvgUpRate = ini.GetFloat(_T("ConnAvgUpRate"));
	cumConnMaxAvgUpRate = ini.GetFloat(_T("ConnMaxAvgUpRate"));
	cumConnMaxUpRate = ini.GetFloat(_T("ConnMaxUpRate"));
	cumConnRunTime = ini.GetInt(_T("ConnRunTime"));
	cumConnTransferTime = ini.GetInt(_T("ConnTransferTime"));
	cumConnDownloadTime = ini.GetInt(_T("ConnDownloadTime"));
	cumConnUploadTime = ini.GetInt(_T("ConnUploadTime"));
	cumConnServerDuration = ini.GetInt(_T("ConnServerDuration"));
	cumConnNumReconnects = ini.GetInt(_T("ConnNumReconnects"));
	cumConnAvgConnections = ini.GetInt(_T("ConnAvgConnections"));
	cumConnMaxConnLimitReached = ini.GetInt(_T("ConnMaxConnLimitReached"));
	cumConnPeakConnections = ini.GetInt(_T("ConnPeakConnections"));

	// Load date/time of last reset
	stat_datetimeLastReset = ini.GetInt(_T("statsDateTimeLastReset"));

	// Smart Load For Restores - Don't overwrite records that are greater than the backed up ones
	if (loadBackUp == 1) {
		// Load records for servers / network
		if ((uint32)ini.GetInt(_T("SrvrsMostWorkingServers")) > cumSrvrsMostWorkingServers)
			cumSrvrsMostWorkingServers = ini.GetInt(_T("SrvrsMostWorkingServers"));

		if ((uint32)ini.GetInt(_T("SrvrsMostUsersOnline")) > cumSrvrsMostUsersOnline)
			cumSrvrsMostUsersOnline = ini.GetInt(_T("SrvrsMostUsersOnline"));

		if ((uint32)ini.GetInt(_T("SrvrsMostFilesAvail")) > cumSrvrsMostFilesAvail)
			cumSrvrsMostFilesAvail = ini.GetInt(_T("SrvrsMostFilesAvail"));

		// Load records for shared files
		if ((uint32)ini.GetInt(_T("SharedMostFilesShared")) > cumSharedMostFilesShared)
			cumSharedMostFilesShared = ini.GetInt(_T("SharedMostFilesShared"));

		uint64 temp64 = ini.GetUInt64(_T("SharedLargestShareSize"));
		if (temp64 > cumSharedLargestShareSize)
			cumSharedLargestShareSize = temp64;

		temp64 = ini.GetUInt64(_T("SharedLargestAvgFileSize"));
		if (temp64 > cumSharedLargestAvgFileSize)
			cumSharedLargestAvgFileSize = temp64;

		temp64 = ini.GetUInt64(_T("SharedLargestFileSize"));
		if (temp64 > cumSharedLargestFileSize)
			cumSharedLargestFileSize = temp64;

		// Check to make sure the backup of the values we just overwrote exists.  If so, rename it to the backup file.
		// This allows us to undo a restore, so to speak, just in case we don't like the restored values...
		CString sINIBackUp(sConfDir + _T("statbkuptmp.ini"));
		if (findBackUp.FindFile(sINIBackUp)) {
			::DeleteFile(sINI);				// Remove the backup that we just restored from
			::MoveFile(sINIBackUp, sINI);	// Rename our temporary backup to the normal statbkup.ini filename.
		}

		// Since we know this is a restore, now we should call ShowStatistics to update the data items to the new ones we just loaded.
		// Otherwise user is left waiting around for the tick counter to reach the next automatic update (Depending on setting in prefs)
		theApp.emuledlg->statisticswnd->ShowStatistics();
	}
	else {
		// Stupid Load -> Just load the values.
		// Load records for servers / network
		cumSrvrsMostWorkingServers = ini.GetInt(_T("SrvrsMostWorkingServers"));
		cumSrvrsMostUsersOnline = ini.GetInt(_T("SrvrsMostUsersOnline"));
		cumSrvrsMostFilesAvail = ini.GetInt(_T("SrvrsMostFilesAvail"));

		// Load records for shared files
		cumSharedMostFilesShared = ini.GetInt(_T("SharedMostFilesShared"));
		cumSharedLargestShareSize = ini.GetUInt64(_T("SharedLargestShareSize"));
		cumSharedLargestAvgFileSize = ini.GetUInt64(_T("SharedLargestAvgFileSize"));
		cumSharedLargestFileSize = ini.GetUInt64(_T("SharedLargestFileSize"));

		// Initialize new session statistic variables...
		sesDownCompletedFiles = 0;

		sesUpData_EDONKEY = 0;
		sesUpData_EDONKEYHYBRID = 0;
		sesUpData_EMULE = 0;
		sesUpData_MLDONKEY = 0;
		sesUpData_AMULE = 0;
		sesUpData_EMULECOMPAT = 0;
		sesUpData_SHAREAZA = 0;
		sesUpDataPort_4662 = 0;
		sesUpDataPort_OTHER = 0;

		sesDownData_EDONKEY = 0;
		sesDownData_EDONKEYHYBRID = 0;
		sesDownData_EMULE = 0;
		sesDownData_MLDONKEY = 0;
		sesDownData_AMULE = 0;
		sesDownData_EMULECOMPAT = 0;
		sesDownData_SHAREAZA = 0;
		sesDownData_URL = 0;
		sesDownDataPort_4662 = 0;
		sesDownDataPort_OTHER = 0;

		sesDownSuccessfulSessions = 0;
		sesDownFailedSessions = 0;
		sesPartsSavedByICH = 0;
	}

	if (!fileex || (stat_datetimeLastReset == 0 && totalDownloadedBytes == 0 && totalUploadedBytes == 0)) {
		time_t timeNow;
		time(&timeNow);
		stat_datetimeLastReset = timeNow;
	}

	return true;
}

// This formats the UTC long value that is saved for stat_datetimeLastReset
// If this value is 0 (Never reset), then it returns Unknown.
CString CPreferences::GetStatsLastResetStr(bool formatLong)
{
	// The format of the returned string depends on formatLong.
	// true: DateTime format from the .ini
	// false: DateTime format from the .ini for the log
	if (GetStatsLastResetLng()) {
		tm* statsReset;
		TCHAR szDateReset[128];
		time_t lastResetDateTime = GetStatsLastResetLng();
		statsReset = localtime(&lastResetDateTime);
		if (statsReset) {
			_tcsftime(szDateReset, _countof(szDateReset), formatLong ? (LPCTSTR)GetDateTimeFormat() : _T("%c"), statsReset);
			if (*szDateReset)
				return CString(szDateReset);
		}
	}
	return GetResString(_T("UNKNOWN"));
}


bool CPreferences::Save()
{
	static LPCTSTR const stmp = _T(".tmp");
	const CString& sConfDir(GetMuleDirectory(EMULE_CONFIGDIR));
	const CString &strPrefPath(sConfDir + strPreferencesDat);
	bool error;

	FILE* preffile = _tfsopen(strPrefPath + stmp, _T("wb"), _SH_DENYWR); //keep contents
	if (preffile) {
		prefsExt->version = PREFFILE_VERSION;
		md4cpy(prefsExt->userhash, userhash);
		prefsExt->EmuleWindowPlacement = EmuleWindowPlacement;
		error = (fwrite(prefsExt, sizeof(Preferences_Ext_Struct), 1, preffile) != 1);
		error |= (fclose(preffile) != 0);
		if (!error)
			error = !MoveFileEx(strPrefPath + stmp, strPrefPath, MOVEFILE_REPLACE_EXISTING);
	} else
		error = true;

	SavePreferences();
	SaveStats();

	error = SaveSharedFolders();

	::CreateDirectory(GetMuleDirectory(EMULE_INCOMINGDIR), 0);
	::CreateDirectory(GetTempDir(), 0);
	return error;
}

void CPreferences::CreateUserHash()
{
	while (isbadhash(userhash)) {
		CryptoPP::AutoSeededRandomPool rng;
		rng.GenerateBlock(userhash, sizeof userhash);
	}
	// mark as emule client. this will be needed in later version
	userhash[5] = 14;	//0x0e
	userhash[14] = 111;	//0x6f
}

void CPreferences::SetMigrationWizardHandled(bool bHandled)
{
	m_bMigrationWizardHandled = bHandled;

	CIni ini(GetConfigFile(), _T("eMule"));
	ini.WriteBool(_T("MigrationWizardHandled"), m_bMigrationWizardHandled);
}

void CPreferences::SetMigrationWizardRunOnNextStart(bool bRunOnNextStart)
{
	m_bMigrationWizardRunOnNextStart = bRunOnNextStart;

	CIni ini(GetConfigFile(), _T("eMule"));
	ini.WriteBool(_T("MigrationWizardRunOnNextStart"), m_bMigrationWizardRunOnNextStart);
}

void CPreferences::ReloadStartupStateAfterMigration()
{
	if (prefsExt == NULL)
		return;

	const CString &sConfDir(GetMuleDirectory(EMULE_CONFIGDIR));
	CString strFullPath(sConfDir + strPreferencesDat);
	FILE *preffile = _tfsopen(strFullPath, _T("rb"), _SH_DENYWR);
	bool bLoadedPrefsDat = false;
	if (preffile != NULL) {
		bLoadedPrefsDat = (fread(prefsExt, sizeof(Preferences_Ext_Struct), 1, preffile) == 1 && !ferror(preffile));
		fclose(preffile);
	}

	if (!bLoadedPrefsDat) {
		SetStandardValues();
	}
	else {
		md4cpy(userhash, prefsExt->userhash);
		EmuleWindowPlacement = prefsExt->EmuleWindowPlacement;
		smartidstate = 0;
	}
	CreateUserHash();

	shareddir_list.RemoveAll();
	addresses_list.RemoveAll();

	CStdioFile sdirfile;
	strFullPath.Format(_T("%s") SHAREDDIRS, (LPCTSTR)sConfDir);
	bool bIsUnicodeFile = IsUnicodeFile(strFullPath);
	if (sdirfile.Open(strFullPath, CFile::modeRead | CFile::shareDenyWrite | (bIsUnicodeFile ? CFile::typeBinary : 0))) {
		try {
			if (bIsUnicodeFile)
				sdirfile.Seek(sizeof(WORD), CFile::begin);

			CString toadd;
			while (sdirfile.ReadString(toadd)) {
				toadd.Trim(_T(" \t\r\n"));
				if (!toadd.IsEmpty()) {
					MakeFoldername(toadd);
					if (IsShareableDirectory(toadd) && (m_bKeepUnavailableFixedSharedDirs || DirAccsess(toadd)))
						shareddir_list.AddTail(toadd);
				}
			}
		}
		catch (CFileException *ex) {
			ASSERT(0);
			ex->Delete();
		}
		sdirfile.Close();
	}

	strFullPath.Format(_T("%s") _T("addresses.dat"), (LPCTSTR)sConfDir);
	bIsUnicodeFile = IsUnicodeFile(strFullPath);
	if (sdirfile.Open(strFullPath, CFile::modeRead | CFile::shareDenyWrite | (bIsUnicodeFile ? CFile::typeBinary : 0))) {
		try {
			if (bIsUnicodeFile)
				sdirfile.Seek(sizeof(WORD), CFile::current);

			CString toadd;
			while (sdirfile.ReadString(toadd)) {
				toadd.Trim(_T(" \t\r\n"));
				if (!toadd.IsEmpty())
					addresses_list.AddTail(toadd);
			}
		}
		catch (CFileException *ex) {
			ASSERT(0);
			ex->Delete();
		}
		sdirfile.Close();
	}
}

void CPreferences::ImportLegacyPreferencesIniForMigration(LPCTSTR pszLegacyConfigDir)
{
	if (pszLegacyConfigDir == NULL || pszLegacyConfigDir[0] == _T('\0'))
		return;

	CString strLegacyConfigDir(pszLegacyConfigDir);
	MakeFoldername(strLegacyConfigDir);

	const CString strLegacyIniPath(strLegacyConfigDir + _T("preferences.ini"));
	if (!::PathFileExists(strLegacyIniPath))
		return;

	CIni ini(strLegacyIniPath, _T("eMule"));
	static LPCTSTR const kMissingStringSentinel = _T("\v§MIGRATION_MISSING§\v");
	static const int kMissingIntSentinel = INT_MIN;

	CString incomingDir(ini.GetString(_T("IncomingDir"), kMissingStringSentinel));
	if (incomingDir != kMissingStringSentinel && !incomingDir.IsEmpty()) {
		MakeFoldername(incomingDir);
		if (::PathFileExists(incomingDir) || ::CreateDirectory(incomingDir, NULL))
			m_strIncomingDir = incomingDir;
	}

	CString primaryTempDir(ini.GetString(_T("TempDir"), kMissingStringSentinel));
	CString additionalTempDirs(ini.GetString(_T("TempDirs"), kMissingStringSentinel));
	if (primaryTempDir != kMissingStringSentinel || additionalTempDirs != kMissingStringSentinel) {
		CString importedTempDirsRaw;
		if (primaryTempDir != kMissingStringSentinel && !primaryTempDir.IsEmpty())
			importedTempDirsRaw = primaryTempDir;
		if (additionalTempDirs != kMissingStringSentinel && !additionalTempDirs.IsEmpty()) {
			if (!importedTempDirsRaw.IsEmpty())
				importedTempDirsRaw += _T('|');
			importedTempDirsRaw += additionalTempDirs;
		}

		CStringArray importedTempDirs;
		for (int iPos = 0; iPos >= 0;) {
			CString tempPath(importedTempDirsRaw.Tokenize(_T("|"), iPos));
			if (tempPath.Trim().IsEmpty())
				continue;

			MakeFoldername(tempPath);
			bool bDup = false;
			for (INT_PTR i = importedTempDirs.GetCount(); --i >= 0;) {
				if (tempPath.CompareNoCase(importedTempDirs[i]) == 0) {
					bDup = true;
					break;
				}
			}

			if (!bDup && (::PathFileExists(tempPath) || ::CreateDirectory(tempPath, NULL)))
				importedTempDirs.Add(tempPath);
		}

		if (!importedTempDirs.IsEmpty()) {
			tempdir.RemoveAll();
			for (INT_PTR i = 0; i < importedTempDirs.GetCount(); ++i)
				tempdir.Add(importedTempDirs[i]);
		}
	}

	int value = ini.GetInt(_T("Port"), kMissingIntSentinel);
	if (value != kMissingIntSentinel && value > 0 && value <= USHRT_MAX)
		port = (uint16)value;

	value = ini.GetInt(_T("UDPPort"), kMissingIntSentinel);
	if (value != kMissingIntSentinel && value >= 0 && value <= USHRT_MAX)
		udpport = (uint16)value;

	value = ini.GetInt(_T("ServerUDPPort"), kMissingIntSentinel);
	if (value != kMissingIntSentinel && value >= -1 && value <= USHRT_MAX)
		nServerUDPPort = (uint16)value;

	CString videoPlayer(ini.GetString(_T("VideoPlayer"), kMissingStringSentinel));
	if (videoPlayer != kMissingStringSentinel) {
		m_strVideoPlayer = videoPlayer;
		if (m_strVideoPlayer.IsEmpty() || !DoesExpandedPathExist(m_strVideoPlayer))
			m_strVideoPlayer = GetDefaultVideoPlayerPath();
	}
}

UINT CPreferences::GetRecommendedMaxConnections()
{
	return 1000;
}

void CPreferences::SavePreferences()
{
	CIni ini(GetConfigFile(), _T("eMule"));
	//---
	ini.WriteString(_T("AppVersion"), theApp.GetAppVersion());
	//---
#ifdef _BETA
	if (m_bBetaNaggingDone)
		ini.WriteString(_T("BetaVersionNotified"), theApp.GetAppVersion().Mid(6));
#endif
#ifdef _DEBUG
	ini.WriteInt(_T("DebugHeap"), m_iDbgHeap);
#endif

	ini.WriteStringUTF8(_T("Nick"), strNick);
	ini.WriteString(_T("IncomingDir"), m_strIncomingDir);

	ini.WriteString(_T("TempDir"), tempdir[0]);
	CString tempdirs;
	for (INT_PTR i = 1; i < tempdir.GetCount(); ++i) {
		if (i > 1)
			tempdirs += _T('|');
		tempdirs += tempdir[i];
	}
	ini.WriteString(_T("TempDirs"), tempdirs);

	ini.WriteInt(_T("MinUpload"), m_minupload);
	ini.WriteInt(_T("MaxUpload"), m_maxupload);
	ini.WriteInt(_T("MaxDownload"), m_maxdownload);
	ini.WriteInt(_T("MaxConnections"), maxconnections);
	ini.WriteInt(_T("MaxHalfConnections"), maxhalfconnections);
	ini.WriteBool(_T("ConditionalTCPAccept"), m_bConditionalTCPAccept);
	ini.WriteInt(_T("Port"), port);
	ini.WriteInt(_T("UDPPort"), udpport);
	ini.WriteInt(_T("ServerUDPPort"), nServerUDPPort);
	ini.WriteInt(_T("MaxSourcesPerFile"), maxsourceperfile);
	ini.WriteString(_T("Ui.Language"), m_strUiLanguage.IsEmpty() ? _T("system") : (LPCTSTR)m_strUiLanguage);
	ini.WriteBool(_T("MigrationWizardHandled"), m_bMigrationWizardHandled);
	ini.WriteBool(_T("MigrationWizardRunOnNextStart"), m_bMigrationWizardRunOnNextStart);
	ini.WriteInt(_T("SeeShare"), m_iSeeShares);
	ini.WriteInt(_T("ToolTipDelay"), m_iToolDelayTime);
	ini.WriteInt(_T("StatGraphsInterval"), trafficOMeterInterval);
	ini.WriteInt(_T("StatsInterval"), statsInterval);
	ini.WriteBool(_T("StatsFillGraphs"), m_bFillGraphs);
	ini.WriteInt(_T("DownloadCapacityMbit"), KBytesPerSecToRoundedMbitPerSec(maxGraphDownloadRate));
	ini.WriteInt(_T("UploadCapacityMbit"), maxGraphUploadRate == UNLIMITED ? 0 : KBytesPerSecToRoundedMbitPerSec(maxGraphUploadRate));
	ini.DeleteKey(_T("DownloadCapacity"));
	ini.DeleteKey(_T("UploadCapacityNew"));
	ini.DeleteKey(_T("UploadCapacity"));
	ini.WriteInt(_T("DeadServerRetry"), m_uDeadServerRetries);
	ini.WriteInt(_T("ServerKeepAliveTimeout"), m_dwServerKeepAliveTimeout);
	ini.WriteInt(_T("SplitterbarPosition"), splitterbarPosition);
	ini.WriteInt(_T("SplitterbarPositionServer"), splitterbarPositionSvr);
	ini.WriteInt(_T("SplitterbarPositionStat"), splitterbarPositionStat + 1);
	ini.WriteInt(_T("SplitterbarPositionStat_HL"), splitterbarPositionStat_HL + 1);
	ini.WriteInt(_T("SplitterbarPositionStat_HR"), splitterbarPositionStat_HR + 1);
	ini.WriteInt(_T("SplitterbarPositionFriend"), splitterbarPositionFriend);
	ini.WriteInt(_T("SplitterbarPositionIRC"), splitterbarPositionIRC);
	ini.WriteInt(_T("SplitterbarPositionShared"), splitterbarPositionShared);
	ini.WriteInt(_T("TransferWnd1"), m_uTransferWnd1);
	ini.WriteInt(_T("TransferWnd2"), m_uTransferWnd2);
	ini.WriteInt(_T("VariousStatisticsMaxValue"), statsMax);
	ini.WriteInt(_T("StatsAverageMinutes"), statsAverageMinutes);
	ini.WriteInt(_T("MaxConnectionsPerFiveSeconds"), MaxConperFive);

	ini.WriteBool(_T("Reconnect"), reconnect);
	ini.WriteBool(_T("Scoresystem"), m_bUseServerPriorities);
	ini.WriteBool(_T("Serverlist"), m_bAutoUpdateServerList);
	if (IsRunningAeroGlassTheme())
		ini.WriteBool(_T("MinToTray_Aero"), mintotray);
	else
		ini.WriteBool(_T("MinToTray"), mintotray);
	ini.WriteBool(_T("PreventStandby"), m_bPreventStandby);
	ini.WriteBool(_T("StoreSearches"), m_bStoreSearches);
	ini.WriteBool(_T("AddServersFromServer"), m_bAddServersFromServer);
	ini.WriteBool(_T("AddServersFromClient"), m_bAddServersFromClients);
	ini.WriteBool(_T("Splashscreen"), splashscreen);
	ini.WriteBool(_T("BringToFront"), bringtoforeground);
	ini.WriteBool(_T("TransferDoubleClick"), transferDoubleclick);
	ini.WriteBool(_T("ConfirmExit"), confirmExit);
	ini.WriteBool(_T("FilterBadIPs"), filterLANIPs);
	ini.WriteBool(_T("Autoconnect"), autoconnect);
	ini.WriteBool(_T("OnlineSignature"), onlineSig);
	ini.WriteBool(_T("StartupMinimized"), startMinimized);
	ini.WriteBool(_T("AutoStart"), m_bAutoStart);
	ini.WriteInt(_T("LastMainWndDlgID"), m_iLastMainWndDlgID);
	ini.WriteInt(_T("LastLogPaneID"), m_iLastLogPaneID);
	ini.WriteBool(_T("SafeServerConnect"), m_bSafeServerConnect);
	ini.WriteBool(_T("ShowRatesOnTitle"), showRatesInTitle);
	ini.WriteBool(_T("IndicateRatings"), indicateratings);
	ini.WriteBool(_T("WatchClipboard4ED2kFilelinks"), watchclipboard);
	ini.WriteInt(_T("SearchMethod"), m_iSearchMethod);
	ini.WriteString(_T("SearchFileType"), m_strSearchFileType);
	ini.WriteBool(_T("CheckDiskspace"), checkDiskspace);
	ini.WriteInt(_T("MinFreeDiskSpace"), m_uMinFreeDiskSpace);
	ini.WriteInt(_T("FreeDiskSpaceCheckPeriod"), SEC2MIN(m_uFreeDiskSpaceCheckPeriod));
	ini.WriteBool(_T("SparsePartFiles"), m_bSparsePartFiles);
	ini.WriteBool(_T("ResolveSharedShellLinks"), m_bResolveSharedShellLinks);
	ini.WriteString(_T("YourHostname"), m_strYourHostname);
	ini.WriteBool(_T("CheckFileOpen"), m_bCheckFileOpen);
	ini.WriteBool(_T("ShowWin7TaskbarGoodies"), m_bShowWin7TaskbarGoodies);

	// Barry - New properties...
	ini.WriteBool(_T("AutoConnectStaticOnly"), m_bAutoConnectToStaticServersOnly);
	ini.WriteBool(_T("AutoTakeED2KLinks"), autotakeed2klinks);
	ini.WriteBool(_T("AddNewFilesPaused"), addnewfilespaused);
	ini.WriteInt(_T("3DDepth"), depth3D);
	ini.WriteBool(_T("MiniMule"), m_bEnableMiniMule);

	ini.WriteString(_T("NotifierConfiguration"), notifierConfiguration);
	ini.WriteBool(_T("NotifyOnDownload"), notifierOnDownloadFinished);
	ini.WriteBool(_T("NotifyOnNewDownload"), notifierOnNewDownload);
	ini.WriteBool(_T("NotifyOnChat"), notifierOnChat);
	ini.WriteBool(_T("NotifyOnLog"), notifierOnLog);
	ini.WriteBool(_T("NotifyOnImportantError"), notifierOnImportantError);
	ini.WriteBool(_T("NotifierPopEveryChatMessage"), notifierOnEveryChatMsg);
	ini.WriteInt(_T("NotifierUseSound"), (int)notifierSoundType);
	ini.WriteString(_T("NotifierSoundPath"), notifierSoundFile);

	ini.WriteString(_T("TxtEditor"), m_strTxtEditor);
	ini.WriteString(_T("VideoPlayer"), m_strVideoPlayer);
	ini.WriteString(_T("VideoPlayerArgs"), m_strVideoPlayerArgs);
	ini.WriteString(_T("MessageFilter"), messageFilter);
	ini.WriteString(_T("CommentFilter"), commentFilter);
	ini.WriteString(_T("DateTimeFormat"), GetDateTimeFormat());
	ini.WriteString(_T("DateTimeFormat4Log"), GetDateTimeFormat4Log());
	ini.WriteString(_T("WebTemplateFile"), m_strTemplateFile);
	ini.WriteString(_T("FilenameCleanups"), filenameCleanups);
	ini.WriteInt(_T("ExtractMetaData"), m_iExtractMetaData);

	ini.WriteString(_T("DefaultIRCServerNew"), m_strIRCServer);
	ini.WriteString(_T("IRCNick"), m_strIRCNick);
	ini.WriteBool(_T("IRCAddTimestamp"), m_bIRCAddTimeStamp);
	ini.WriteString(_T("IRCFilterName"), m_strIRCChannelFilter);
	ini.WriteInt(_T("IRCFilterUser"), m_uIRCChannelUserFilter);
	ini.WriteBool(_T("IRCUseFilter"), m_bIRCUseChannelFilter);
	ini.WriteString(_T("IRCPerformString"), m_strIRCPerformString);
	ini.WriteBool(_T("IRCUsePerform"), m_bIRCUsePerform);
	ini.WriteBool(_T("IRCListOnConnect"), m_bIRCGetChannelsOnConnect);
	ini.WriteBool(_T("IRCAcceptLink"), m_bIRCAcceptLinks);
	ini.WriteBool(_T("IRCAcceptLinkFriends"), m_bIRCAcceptLinksFriendsOnly);
	ini.WriteBool(_T("IRCSoundEvents"), m_bIRCPlaySoundEvents);
	ini.WriteBool(_T("IRCIgnoreMiscMessages"), m_bIRCIgnoreMiscMessages);
	ini.WriteBool(_T("IRCIgnoreJoinMessages"), m_bIRCIgnoreJoinMessages);
	ini.WriteBool(_T("IRCIgnorePartMessages"), m_bIRCIgnorePartMessages);
	ini.WriteBool(_T("IRCIgnoreQuitMessages"), m_bIRCIgnoreQuitMessages);
	ini.WriteBool(_T("IRCIgnorePingPongMessages"), m_bIRCIgnorePingPongMessages);
	ini.WriteBool(_T("IRCIgnoreEmuleAddFriendMsgs"), m_bIRCIgnoreEmuleAddFriendMsgs);
	ini.WriteBool(_T("IRCAllowEmuleAddFriend"), m_bIRCAllowEmuleAddFriend);
	ini.WriteBool(_T("IRCIgnoreEmuleSendLinkMsgs"), m_bIRCIgnoreEmuleSendLinkMsgs);
	ini.WriteBool(_T("IRCHelpChannel"), m_bIRCJoinHelpChannel);
	ini.WriteBool(_T("IRCEnableSmileys"), m_bIRCEnableSmileys);
	ini.WriteBool(_T("MessageEnableSmileys"), m_bMessageEnableSmileys);
	ini.WriteBool(_T("IRCEnableUTF8"), m_bIRCEnableUTF8);

	ini.WriteBool(_T("SmartIdCheck"), m_bSmartServerIdCheck);
	ini.WriteBool(_T("Verbose"), m_bVerbose);
	ini.WriteBool(_T("DebugSourceExchange"), m_bDebugSourceExchange);	// do *not* use the according 'Get...' function here!
	ini.WriteBool(_T("LogBannedClients"), m_bLogBannedClients);			// do *not* use the according 'Get...' function here!
	ini.WriteBool(_T("LogRatingDescReceived"), m_bLogRatingDescReceived);// do *not* use the according 'Get...' function here!
	ini.WriteBool(_T("LogSecureIdent"), m_bLogSecureIdent);				// do *not* use the according 'Get...' function here!
	ini.WriteBool(_T("LogFilteredIPs"), m_bLogFilteredIPs);				// do *not* use the according 'Get...' function here!
	ini.WriteBool(_T("LogFileSaving"), m_bLogFileSaving);				// do *not* use the according 'Get...' function here!
	ini.WriteBool(_T("LogA4AF"), m_bLogA4AF);                           // do *not* use the according 'Get...' function here!
	ini.WriteBool(_T("LogUlDlEvents"), m_bLogUlDlEvents);
	ini.WriteBool(L"LogSpamRating", m_bLogSpamRating);
	ini.WriteBool(L"LogRetryFailedTcp", m_bLogRetryFailedTcp);
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
	// following options are for debugging or when using an external debug device viewer only.
	ini.WriteInt(_T("DebugServerTCP"), m_iDebugServerTCPLevel);
	ini.WriteInt(_T("DebugServerUDP"), m_iDebugServerUDPLevel);
	ini.WriteInt(_T("DebugServerSources"), m_iDebugServerSourcesLevel);
	ini.WriteInt(_T("DebugServerSearches"), m_iDebugServerSearchesLevel);
	ini.WriteInt(_T("DebugClientTCP"), m_iDebugClientTCPLevel);
	ini.WriteInt(_T("DebugClientUDP"), m_iDebugClientUDPLevel);
	ini.WriteInt(_T("DebugClientKadUDP"), m_iDebugClientKadUDPLevel);
#endif
	ini.WriteBool(_T("PreviewPrio"), m_bpreviewprio);
	ini.WriteBool(_T("ManualHighPrio"), m_bManualAddedServersHighPriority);
	ini.WriteBool(_T("FullChunkTransfers"), m_btransferfullchunks);
	ini.WriteBool(_T("ShowOverhead"), m_bshowoverhead);
	ini.WriteBool(_T("VideoPreviewBackupped"), m_bMoviePreviewBackup);
	ini.WriteInt(_T("StartNextFile"), m_istartnextfile);

	ini.DeleteKey(_T("FileBufferSizePref")); // delete old 'file buff size' setting
	ini.WriteInt(_T("FileBufferSize"), m_uFileBufferSize);

	ini.DeleteKey(_T("QueueSizePref")); // delete old 'queue size' setting
	ini.WriteInt(_T("QueueSize"), (int)m_iQueueSize);

	ini.WriteInt(_T("CommitFiles"), m_iCommitFiles);
	ini.WriteBool(_T("DAPPref"), m_bDAP);
	ini.WriteBool(_T("UAPPref"), m_bUAP);
	ini.WriteBool(_T("FilterServersByIP"), filterserverbyip);
	ini.WriteBool(_T("DontFilterPrivateIPs"), m_bDontFilterPrivateIPs);
	ini.WriteBool(_T("DisableKnownClientList"), m_bDisableKnownClientList);
	ini.WriteBool(_T("DisableQueueList"), m_bDisableQueueList);
	ini.WriteBool(_T("UseCreditSystem"), m_bCreditSystem);
	ini.WriteBool(_T("SaveLogToDisk"), log2disk);
	ini.WriteBool(_T("SaveDebugToDisk"), debug2disk);
	ini.WriteBool(_T("EnableScheduler"), scheduler);
	ini.WriteBool(_T("MessagesFromFriendsOnly"), msgonlyfriends);
	ini.WriteBool(_T("MessageUseCaptchas"), m_bUseChatCaptchas);
	ini.WriteBool(_T("ShowInfoOnCatTabs"), showCatTabInfos);
	ini.WriteBool(_T("AutoFilenameCleanup"), autofilenamecleanup);
	ini.WriteBool(_T("ShowExtControls"), m_bExtControls);
	ini.WriteBool(_T("UseAutocompletion"), m_bUseAutocompl);
	ini.WriteBool(_T("NetworkKademlia"), networkkademlia);
	ini.WriteBool(_T("NetworkED2K"), networked2k);
	ini.WriteBool(_T("AutoClearCompleted"), m_bRemoveFinishedDownloads);
	ini.WriteBool(_T("TransflstRemainOrder"), m_bTransflstRemain);
	ini.WriteBool(_T("UseSimpleTimeRemainingcomputation"), m_bUseOldTimeRemaining);
	ini.WriteBool(_T("AllocateFullFile"), m_bAllocFull);
	ini.WriteBool(_T("ShowSharedFilesDetails"), m_bShowSharedFilesDetails);
	ini.WriteBool(_T("AutoShowLookups"), m_bAutoShowLookups);

	ini.WriteInt(_T("VersionCheckLastAutomatic"), (int)versioncheckLastAutomatic);
	ini.WriteString(_T("VersionCheckLastKnownVersionOnServer"), versioncheckLastKnownVersionOnServer);
	ini.WriteInt(_T("FilterLevel"), filterlevel);

	ini.WriteBool(_T("SecureIdent"), m_bUseSecureIdent);// change the name in future version to enable it by default
	ini.WriteBool(_T("AdvancedSpamFilter"), m_bAdvancedSpamfilter);
	ini.WriteBool(_T("ShowDwlPercentage"), m_bShowDwlPercentage);
	ini.WriteBool(_T("RemoveFilesToBin"), m_bRemove2bin);
	ini.WriteBool(_T("AutoArchivePreviewStart"), m_bAutomaticArcPreviewStart);

	// Toolbar
	ini.WriteString(_T("ToolbarSetting"), m_sToolbarSettings);
	ini.WriteString(_T("ToolbarBitmap"), m_sToolbarBitmap);
	ini.WriteString(_T("ToolbarBitmapFolder"), m_sToolbarBitmapFolder);
	ini.WriteInt(_T("ToolbarLabels"), m_nToolbarLabels);
	ini.WriteInt(_T("ToolbarIconSize"), m_sizToolbarIconSize.cx);
	ini.WriteString(_T("SkinProfile"), m_strSkinProfile);
	ini.WriteString(_T("SkinProfileDir"), m_strSkinProfileDir);

	ini.WriteBinary(_T("HyperTextFont"), (LPBYTE)&m_lfHyperText, sizeof m_lfHyperText);
	ini.WriteBinary(_T("LogTextFont"), (LPBYTE)&m_lfLogText, sizeof m_lfLogText);

	ini.WriteBool(_T("USSEnabled"), m_bDynUpEnabled);
	ini.WriteBool(_T("USSUseMillisecondPingTolerance"), m_bDynUpUseMillisecondPingTolerance);
	ini.WriteInt(_T("USSPingTolerance"), m_iDynUpPingTolerance);
	ini.WriteInt(_T("USSPingToleranceMilliseconds"), m_iDynUpPingToleranceMilliseconds); // EastShare - Add by TAHO, USS limit
	ini.WriteInt(_T("USSGoingUpDivider"), m_iDynUpGoingUpDivider);
	ini.WriteInt(_T("USSGoingDownDivider"), m_iDynUpGoingDownDivider);
	ini.WriteInt(_T("USSNumberOfPings"), m_iDynUpNumberOfPings);

	ini.WriteBool(_T("A4AFSaveCpu"), m_bA4AFSaveCpu); // ZZ:DownloadManager
	ini.WriteBool(_T("HighresTimer"), m_bHighresTimer);
	ini.WriteInt(_T("WebMirrorAlertLevel"), m_nWebMirrorAlertLevel);
	ini.WriteBool(_T("RunAsUnprivilegedUser"), m_bRunAsUser);
	ini.WriteBool(_T("OpenPortsOnStartUp"), m_bOpenPortsOnStartUp);
	ini.WriteInt(_T("DebugLogLevel"), m_byLogLevel);
	ini.WriteInt(_T("WinXPSP2OrHigher"), static_cast<int>(IsRunningXPSP2OrHigher()));
	ini.WriteBool(_T("RememberCancelledFiles"), m_bRememberCancelledFiles);
	ini.WriteBool(_T("RememberDownloadedFiles"), m_bRememberDownloadedFiles);

	ini.WriteBool(_T("NotifierSendMail"), m_email.bSendMail);
	ini.WriteInt(_T("NotifierMailAuth"), m_email.uAuth);
	ini.WriteInt(_T("NotifierMailTLS"), m_email.uTLS);
	ini.WriteString(_T("NotifierMailSender"), m_email.sFrom);
	ini.WriteString(_T("NotifierMailServer"), m_email.sServer);
	ini.WriteInt(_T("NotifierMailPort"), m_email.uPort);
	ini.WriteString(_T("NotifierMailRecipient"), m_email.sTo);
	ini.WriteString(_T("NotifierMailLogin"), m_email.sUser);
	ini.WriteString(_T("NotifierMailPassword"), m_email.sPass);

	ini.WriteBool(_T("WinaTransToolbar"), m_bWinaTransToolbar);
	ini.WriteBool(_T("ShowDownloadToolbar"), m_bShowDownloadToolbar);

	ini.WriteBool(_T("CryptLayerRequested"), m_bCryptLayerRequested);
	ini.WriteBool(_T("CryptLayerRequired"), m_bCryptLayerRequired);
	ini.WriteBool(_T("CryptLayerSupported"), m_bCryptLayerSupported);
	ini.WriteInt(_T("KadUDPKey"), m_dwKadUDPKey);

	ini.WriteBool(_T("EnableSearchResultSpamFilter"), m_bEnableSearchResultFilter);


	///////////////////////////////////////////////////////////////////////////
	// Section: "Proxy"
	//
	ini.WriteBool(_T("ProxyEnablePassword"), proxy.bEnablePassword, _T("Proxy"));
	ini.WriteBool(_T("ProxyEnableProxy"), proxy.bUseProxy, _T("Proxy"));
	ini.WriteString(_T("ProxyName"), proxy.host, _T("Proxy"));
	ini.WriteString(_T("ProxyPassword"), proxy.password, _T("Proxy"));
	ini.WriteString(_T("ProxyUser"), proxy.user, _T("Proxy"));
	ini.WriteInt(_T("ProxyPort"), proxy.port, _T("Proxy"));
	ini.WriteInt(_T("ProxyType"), proxy.type, _T("Proxy"));


	///////////////////////////////////////////////////////////////////////////
	// Section: "Statistics"
	//
	ini.WriteInt(_T("statsConnectionsGraphRatio"), statsConnectionsGraphRatio, _T("Statistics"));
	ini.WriteString(_T("statsExpandedTreeItems"), m_strStatsExpandedTreeItems);
	CString sValue, sKey;
	for (int i = 0; i < 15; ++i) {
		sValue.Format(_T("0x%06lx"), GetStatsColor(i));
		sKey.Format(_T("StatColor%i"), i);
		ini.WriteString(sKey, sValue, _T("Statistics"));
	}
	ini.WriteBool(_T("HasCustomTaskIconColor"), m_bHasCustomTaskIconColor, _T("Statistics"));


	///////////////////////////////////////////////////////////////////////////
	// Section: "WebServer"
	//
	ini.WriteString(_T("Password"), GetWSPass(), _T("WebServer"));
	ini.WriteString(_T("PasswordLow"), GetWSLowPass());
	ini.WriteInt(_T("Port"), m_nWebPort);
	ini.WriteBool(_T("WebUseUPnP"), m_bWebUseUPnP);
	ini.WriteBool(_T("Enabled"), m_bWebEnabled);
	ini.WriteBool(_T("UseGzip"), m_bWebUseGzip);
	ini.WriteInt(_T("PageRefreshTime"), m_nWebPageRefresh);
	ini.WriteBool(_T("UseLowRightsUser"), m_bWebLowEnabled);
	ini.WriteBool(_T("AllowAdminHiLevelFunc"), m_bAllowAdminHiLevFunc);
	ini.WriteInt(_T("WebTimeoutMins"), m_iWebTimeoutMins);
	ini.WriteBool(_T("UseHTTPS"), m_bWebUseHttps);
	ini.WriteString(_T("HTTPSCertificate"), m_sWebHttpsCertificate);
	ini.WriteString(_T("HTTPSKey"), m_sWebHttpsKey);

	///////////////////////////////////////////////////////////////////////////
	// Section: "UPnP"
	//
	ini.WriteBool(_T("EnableUPnP"), m_bEnableUPnP, _T("UPnP"));
	ini.WriteBool(_T("SkipWANIPSetup"), m_bSkipWANIPSetup);
	ini.WriteBool(_T("SkipWANPPPSetup"), m_bSkipWANPPPSetup);
	ini.WriteBool(_T("CloseUPnPOnExit"), m_bCloseUPnPOnExit);
	ini.WriteInt(_T("LastWorkingImplementation"), m_nLastWorkingImpl);

	///////////////////////////////////////////////////////////////////////////
	// Section: "eMuleAI"
	//
	CIni iniAI(GetConfigFile(), _T("eMuleAI"));
	uint32 m_uTemp = 0;
	uint32 m_uTemp2 = 0;
	iniAI.WriteInt(_T("MaxEServerBuddySlots"), m_nMaxEServerBuddySlots);
	ini.DeleteKey(_T("MaxEServerBuddySlots"));
	ini.WriteInt(L"UIDarkMode", m_iUIDarkMode);
	ini.WriteBool(_T("UITweaksSpeedGraph"), m_bUITweaksSpeedGraph);
	ini.WriteBool(L"DisableFindAsYouType", m_bDisableFindAsYouType);
	ini.WriteInt(L"UITweaksListUpdatePeriod", m_iUITweaksListUpdatePeriod);
	ini.WriteInt(_T("GeoLite2Mode"), m_iGeoLite2Mode, _T("eMuleAI"));
	ini.WriteBool(_T("GeoLite2ShowFlag"), m_bGeoLite2ShowFlag);
	ini.WriteBool(L"ConnectionChecker", m_bConnectionChecker);
	ini.WriteString(L"ConnectionCheckerServer", m_sConnectionCheckerServer);
	ini.WriteBool(L"EnableNatTraversal", m_bEnableNatTraversal);
	ini.WriteBool(L"LogExtendedSXEvents", m_bLogExtendedSXEvents);
	ini.WriteBool(L"LogNatTraversalEvents", m_bLogNatTraversalEvents);
	ini.WriteInt(L"NatTraversalPortWindow", (int)m_uNatTraversalPortWindow);
	ini.WriteInt(L"NatTraversalSweepWindow", (int)m_uNatTraversalSweepWindow);
	ini.WriteInt(L"NatTraversalJitterMinMs", (int)m_uNatTraversalJitterMinMs);
	ini.WriteInt(L"NatTraversalJitterMaxMs", (int)m_uNatTraversalJitterMaxMs);
	ini.WriteInt(L"MaxServedBuddies", (int)m_uMaxServedBuddies);

	ini.WriteBool(L"RetryFailedTcpConnectionAttempts", m_bRetryFailedTcpConnectionAttempts);
	ini.WriteBool(L"ReaskSourcesAfterIPChange", m_breaskSourceAfterIPChange);
	ini.WriteBool(L"InformQueuedClientsAfterIPChange", m_bInformQueuedClientsAfterIPChange);
	m_uTemp = (uint8)(MS2MIN((m_uReAskTimeDif + FILEREASKTIME)));
	m_uTemp2 = MS2MIN(FILEREASKTIME);
	m_uTemp = (m_uTemp >= m_uTemp2 && m_uTemp <= 55) ? m_uTemp : m_uTemp2;
	ini.WriteInt(L"ReAskTime", m_uTemp);
	ini.WriteInt(L"DownloadChecker", m_iDownloadChecker);
	ini.WriteInt(L"DownloadCheckerAcceptPercentage", m_iDownloadCheckerAcceptPercentage);
	ini.WriteBool(L"DownloadCheckerRejectCanceled", m_bDownloadCheckerRejectCanceled);
	ini.WriteBool(L"DownloadCheckerRejectSameHash", m_bDownloadCheckerRejectSameHash);
	ini.WriteBool(L"DownloadCheckerRejectBlacklisted", m_bDownloadCheckerRejectBlacklisted);
	ini.WriteBool(L"DownloadCheckerCaseInsensitive", m_bDownloadCheckerCaseInsensitive);
	ini.WriteBool(L"DownloadCheckerIgnoreExtension", m_bDownloadCheckerIgnoreExtension);
	ini.WriteBool(L"DownloadCheckerIgnoreTags", m_bDownloadCheckerIgnoreTags);
	ini.WriteBool(L"DownloadCheckerDontIgnoreNumericTags", m_bDownloadCheckerDontIgnoreNumericTags);
	ini.WriteBool(L"DownloadCheckerIgnoreNonAlphaNumeric", m_bDownloadCheckerIgnoreNonAlphaNumeric);
	ini.WriteInt(L"DownloadCheckerMinimumComparisonLength", m_iDownloadCheckerMinimumComparisonLength);
	ini.WriteBool(L"DownloadCheckerSkipIncompleteFileConfirmation", m_bDownloadCheckerSkipIncompleteFileConfirmation);
	ini.WriteBool(L"DownloadCheckerMarkAsBlacklisted", m_bDownloadCheckerMarkAsBlacklisted);
	ini.WriteBool(L"DownloadCheckerAutoMarkAsBlacklisted", m_bDownloadCheckerAutoMarkAsBlacklisted);
	m_iFileInspector = (m_bFileInspectorFake || m_bFileInspectorFake) ? m_iFileInspector : 0; // If none of these checkboxes selected, force 0;
	ini.WriteInt(L"FileInspector", m_iFileInspector);
	ini.WriteBool(L"FileInspectorFake", m_bFileInspectorFake);
	ini.WriteBool(L"FileInspectorDRM", m_bFileInspectorDRM);
	ini.WriteBool(L"FileInspectorInvalidExt", m_bFileInspectorInvalidExt);
	ini.WriteInt(L"FileInspectorCheckPeriod", m_iFileInspectorCheckPeriod);
	ini.WriteInt(L"FileInspectorCompletedThreshold", m_iFileInspectorCompletedThreshold);
	ini.WriteInt(L"FileInspectorZeroPercentageThreshold", m_iFileInspectorZeroPercentageThreshold);
	ini.WriteInt(L"FileInspectorCompressionThreshold", m_iFileInspectorCompressionThreshold);
	ini.WriteBool(L"FileInspectorBypassZeroPercentage", m_bFileInspectorBypassZeroPercentage);
	ini.WriteInt(L"FileInspectorCompressionThresholdToBypassZero", m_iFileInspectorCompressionThresholdToBypassZero);
	ini.WriteBool(L"GroupKnownAtTheBottom", m_bGroupKnownAtTheBottom);
	ini.WriteInt(L"SpamThreshold", m_iSpamThreshold);
	ini.WriteInt(L"KadSearchKeywordTotal", m_iKadSearchKeywordTotal);
	ini.WriteBool(L"ShowCloseButtonOnSearchTabs", m_bShowCloseButtonOnSearchTabs);
	ini.WriteBool(L"RepeatServerList", m_bRepeatServerList);
	ini.WriteBool(L"DontRemoveStaticServers", m_bDontRemoveStaticServers);
	ini.WriteBool(L"DontSavePartOnReconnect", m_bDontSavePartOnReconnect);
	ini.WriteBool(L"FileHistoryShowPart", m_bFileHistoryShowPart);
	ini.WriteBool(L"FileHistoryShowShared", m_bFileHistoryShowShared);
	ini.WriteBool(L"FileHistoryShowDuplicate", m_bFileHistoryShowDuplicate);
	ini.WriteBool(L"AutoShareSubdirs", static_cast<bool>(m_bAutoShareSubdirs.load(std::memory_order_relaxed)));
	ini.WriteBool(L"DontShareExtensions", static_cast<bool>(m_bDontShareExtensions.load(std::memory_order_relaxed)));
	ini.WriteString(L"DontShareExtensionsList", m_sDontShareExtensionsList);
	ini.WriteBool(L"AdjustNTFSDaylightFileTime", m_bAdjustNTFSDaylightFileTime); // Official preference
	ini.WriteBool(L"AllowDSTTimeTolerance", m_bAllowDSTTimeTolerance);
	ini.WriteBool(L"EmulateMLDonkey", m_bEmulateMLDonkey);
	ini.WriteBool(L"EmulateEdonkey", m_bEmulateEdonkey);
	ini.WriteBool(L"EmulateEdonkeyHybrid", m_bEmulateEdonkeyHybrid);
	ini.WriteBool(L"EmulateShareaza", m_bEmulateShareaza);
	ini.WriteBool(L"EmulateLphant", m_bEmulateLphant);
	ini.WriteBool(L"EmulateCommunity", m_bEmulateCommunity);
	ini.WriteInt(L"EmulateCommunityTagSavingTreshold", m_iEmulateCommunityTagSavingTreshold);
	ini.WriteBool(L"LogEmulator", m_bLogEmulator);
	ini.WriteBool(L"UseIntelligentChunkSelection", m_bUseIntelligentChunkSelection);
	ini.WriteInt(L"CreditSystemMode", creditSystemMode);
	ini.WriteBool(L"ClientHistory", m_bClientHistory);
	ini.WriteInt(L"ClientHistoryExpDays", m_iClientHistoryExpDays);
	ini.WriteBool(L"ClientHistoryLog", m_bClientHistoryLog);
	ini.WriteBool(L"RemoteSharedFilesUserHash", m_bRemoteSharedFilesUserHash);
	ini.WriteBool(L"RemoteSharedFilesClientNote", m_bRemoteSharedFilesClientNote);
	ini.WriteInt(L"RemoteSharedFilesAutoQueryPeriod", m_iRemoteSharedFilesAutoQueryPeriod);
	ini.WriteInt(L"RemoteSharedFilesAutoQueryMaxClients", m_iRemoteSharedFilesAutoQueryMaxClients);
	ini.WriteInt(L"RemoteSharedFilesAutoQueryClientPeriod", m_iRemoteSharedFilesAutoQueryClientPeriod);
	ini.WriteBool(L"RemoteSharedFilesSetAutoQueryDownload", m_bRemoteSharedFilesSetAutoQueryDownload);
	ini.WriteInt(L"RemoteSharedFilesSetAutoQueryDownloadThreshold", m_iRemoteSharedFilesSetAutoQueryDownloadThreshold);
	ini.WriteBool(L"RemoteSharedFilesSetAutoQueryUpload", m_bRemoteSharedFilesSetAutoQueryUpload);
	ini.WriteInt(L"RemoteSharedFilesSetAutoQueryUploadThreshold", m_iRemoteSharedFilesSetAutoQueryUploadThreshold);
	ini.WriteBool(L"SaveLoadSources", m_bSaveLoadSources);
	ini.WriteInt(L"SaveLoadSourcesMaxSources", m_iSaveLoadSourcesMaxSources);
	ini.WriteInt(L"SaveLoadSourcesExpirationMinute", m_iSaveLoadSourcesExpirationDays);
	ini.WriteBool(L"PartiallyPurgeOldKnownFiles", m_bPartiallyPurgeOldKnownFiles, _T("eMule")); // This is official and have GetBool in eMule section
	ini.WriteInt(L"KnownMetDays", m_iKnownMetDays, _T("eMuleAI")); // Switch back to eMuleAI section
	ini.WriteBool(L"CompletlyPurgeOldKnownFiles", m_bCompletlyPurgeOldKnownFiles);
	ini.WriteBool(L"RemoveAichImmediately", m_bRemoveAichImmediately);
	ini.WriteInt(L"ClientsExpDays", m_iClientsExpDays);

	ini.WriteBool(L"BackupOnExit", m_bBackupOnExit);
	ini.WriteBool(L"BackupPeriodic", m_bBackupPeriodic);
	ini.WriteInt(L"BackupPeriod", m_iBackupPeriod);
	ini.WriteInt(L"BackupMax", m_iBackupMax);
	ini.WriteBool(L"BackupCompressed", m_bBackupCompressed);

	ini.WriteString(L"FileTypeSelected", m_strFileTypeSelected);
	ini.WriteInt(L"PreviewChecked", m_uPreviewCheckState);
	ini.WriteBool(L"FreezeChecked", m_bFreezeChecked);
	ini.WriteInt(L"ArchivedChecked", m_uArchivedCheckState);
	ini.WriteInt(L"ConnectedChecked", m_uConnectedCheckState);
	ini.WriteInt(L"QueryableChecked", m_uQueryableCheckState);
	ini.WriteInt(L"QueriedChecked", m_uNotQueriedCheckState);
	ini.WriteInt(L"ValidIPChecked", m_uValidIPCheckState);
	ini.WriteInt(L"HighIdChecked", m_uHighIdCheckState);
	ini.WriteInt(L"BadClientChecked", m_uBadClientCheckState);

	ini.WriteInt(L"CompleteChecked", m_uCompleteCheckState);

	ini.WriteInt(L"BootstrapSelection", m_iBootstrapSelection);

	ini.WriteInt(L"FileBufferTimeLimit", m_uFileBufferTimeLimit / 1000, _T("eMule")); // This is official and have GetInt in eMule section

	///////////////////////////////////////////////////////////////////////////
	// Section: "EmulateCommunityTag"
	//
	int m_iTagIndex = 0;
	for (POSITION pos = thePrefs.m_CommunityTagCounterMap.GetStartPosition(); pos != NULL;) {
		CString m_strTemp;
		CString m_strTag;
		uint32 m_iTagCount;
		thePrefs.m_CommunityTagCounterMap.GetNextAssoc(pos, m_strTag, m_iTagCount);
		if (m_iTagCount == 0) {
			m_strTemp.Format(_T("EmulateCommunityTag%i"), m_iTagIndex++);
			ini.WriteString(m_strTemp, m_strTag, _T("EmulateCommunityTag"));
		}
	}

	///////////////////////////////////////////////////////////////////////////
	// Section: "eMule"
	// 
	ini.WriteBool(L"MiniMuleAutoClose", bMiniMuleAutoClose, _T("eMule"));
	ini.WriteInt(L"MiniMuleTransparency", iMiniMuleTransparency);
	ini.WriteBool(L"CreateCrashDump", bCreateCrashDump);
	ini.WriteBool(L"IgnoreInstance", bIgnoreInstances);
	ini.WriteString(L"NotifierMailEncryptCertName", sNotifierMailEncryptCertName);
	ini.WriteInt(L"MaxLogBuff", iMaxLogBuff / 1024);
	ini.WriteInt(L"MaxChatHistoryLines", m_iMaxChatHistory);
	ini.WriteInt(L"PreviewSmallBlocks", m_iPreviewSmallBlocks);
	ini.WriteBool(L"RestoreLastMainWndDlg", m_bRestoreLastMainWndDlg);
	ini.WriteBool(L"RestoreLastLogPane", m_bRestoreLastLogPane);
	ini.WriteBool(L"PreviewCopiedArchives", m_bPreviewCopiedArchives);
	ini.WriteInt(L"StraightWindowStyles", m_iStraightWindowStyles);
	ini.WriteInt(L"LogFileFormat", m_iLogFileFormat);
	ini.WriteBool(L"RTLWindowsLayout", m_bRTLWindowsLayout);
	ini.WriteBool(L"ShowActiveDownloadsBold", m_bShowActiveDownloadsBold);
	ini.WriteBool(L"PreviewOnIconDblClk", m_bPreviewOnIconDblClk);
	ini.WriteString(L"InternetSecurityZone", sInternetSecurityZone);
	ini.WriteInt(L"MaxMessageSessions", maxmsgsessions);
	ini.WriteBool(L"PreferRestrictedOverUser", m_bPreferRestrictedOverUser);
	ini.WriteBool(L"UserSortedServerList", m_bUseUserSortedServerList);
	ini.WriteInt(L"CryptTCPPaddingLength", m_byCryptTCPPaddingLength);
	ini.WriteBool(L"DontCompressAvi", dontcompressavi);
	ini.WriteBool(L"ShowCopyEd2kLinkCmd", m_bShowCopyEd2kLinkCmd);
	ini.WriteBool(L"IconflashOnNewMessage", m_bIconflashOnNewMessage);
	ini.WriteBool(L"ReBarToolbar", m_bReBarToolbar);
	ini.WriteBool(L"ICH", IsICHEnabled());	// 10.5
	ini.WriteBool(L"RearrangeKadSearchKeywords", m_bRearrangeKadSearchKeywords);
	ini.WriteBool(L"UpdateQueueListPref", m_bupdatequeuelist);
	ini.WriteBool(L"DontRecreateStatGraphsOnResize", dontRecreateGraphs);
	ini.WriteBool(L"BeepOnError", beepOnError);
	ini.WriteBool(L"MessageFromValidSourcesOnly", msgsecure);
	ini.WriteBool(L"ShowUpDownIconInTaskbar", m_bShowUpDownIconInTaskbar);
	ini.WriteBool(L"ForceSpeedsToKB", m_bForceSpeedsToKB);
	ini.DeleteKey(L"ExtraPreviewWithMenu");
	ini.WriteBool(L"KeepUnavailableFixedSharedDirs", m_bKeepUnavailableFixedSharedDirs);
	ini.WriteInt(L"MaxFileUploadSizeMB", m_iWebFileUploadSizeLimitMB, L"WebServer");			//Section WEBSERVER
	CString WriteAllowedIPs;
	if (GetAllowedRemoteAccessIPs().GetCount() > 0)
		for (int i = 0; i < GetAllowedRemoteAccessIPs().GetCount(); i++)
			WriteAllowedIPs = WriteAllowedIPs + L";" + ipstr(GetAllowedRemoteAccessIPs()[i]);
	ini.WriteString(L"AllowedIPs", WriteAllowedIPs, L"WebServer");								// Section WEBSERVER
	ini.WriteBool(L"ShowVerticalHourMarkers", m_bShowVerticalHourMarkers, L"Statistics");		// Section Statistics

	///////////////////////////////////////////////////////////////////////////
	// Section: "ProtectionPanel"
	// 
	ini.WriteInt(L"PunishmentCancelationScanPeriod", MS2MIN(m_iPunishmentCancelationScanPeriod), _T("ProtectionPanel")); //Miliseconds to minutes
	ini.WriteInt(L"ClientBanTime", (uint32)(m_tClientBanTime / 3600)); //Seconds to hours // <== adjust ClientBanTime - Stulle
	ini.WriteInt(L"ClientScoreReducingTime", (uint32)(m_tClientBanTime / 3600)); //Seconds to hours
	ini.WriteBool(L"InformBadClients", m_bInformBadClients);
	ini.WriteString(L"InformBadClientsText", m_strInformBadClientsText);
	ini.WriteBool(L"DontPunishFriends", m_bDontPunishFriends);
	ini.WriteBool(L"DontAllowFileHotSwapping", m_bDontAllowFileHotSwapping);
	ini.WriteBool(L"AntiUploadProtection", m_bAntiUploadProtection);
	ini.WriteInt(L"AntiUploadProtectionLimit", m_iAntiUploadProtectionLimit);
	ini.WriteBool(L"UploaderPunishmentPrevention", m_bUploaderPunishmentPrevention);
	ini.WriteInt(L"UploaderPunishmentPreventionLimit", m_iUploaderPunishmentPreventionLimit);
	ini.WriteInt(L"UploaderPunishmentPreventionCase", m_iUploaderPunishmentPreventionCase);
	ini.WriteBool(L"DetectModNames", m_bDetectModNames);
	ini.WriteBool(L"DetectUserNames", m_bDetectUserNames);
	ini.WriteBool(L"DetectUserHashes", m_bDetectUserHashes);
	ini.WriteInt(L"HardLeecherPunishment", m_uHardLeecherPunishment);
	ini.WriteInt(L"SoftLeecherPunishment", m_uSoftLeecherPunishment);
	ini.WriteBool(L"BanBadKadNodes", m_bBanBadKadNodes);
	ini.WriteBool(L"BanWrongPackage", m_bBanWrongPackage);
	ini.WriteBool(L"DetectAntiP2PBots", m_bDetectAntiP2PBots);
	ini.WriteInt(L"AntiP2PBotsPunishment", m_uAntiP2PBotsPunishment);
	ini.WriteBool(L"DetectWrongTag", m_bDetectWrongTag);
	ini.WriteInt(L"WrongTagPunishment", m_uWrongTagPunishment);
	ini.WriteBool(L"DetectUnknownTag", m_bDetectUnknownTag);
	ini.WriteInt(L"UnknownTagPunishment", m_uUnknownTagPunishment);
	ini.WriteBool(L"DetectHashThief", m_bDetectHashThief);
	ini.WriteInt(L"HashThiefPunishment", m_uHashThiefPunishment);
	ini.WriteBool(L"DetectModThief", m_bDetectModThief);
	ini.WriteInt(L"ModThiefPunishment", m_uModThiefPunishment);
	ini.WriteBool(L"DetectUserNameThief", m_bDetectUserNameThief);
	ini.WriteInt(L"UserNameThiefPunishment", m_uUserNameThiefPunishment);
	ini.WriteBool(L"DetectModChanger", m_bDetectModChanger);
	ini.WriteInt(L"ModChangerInterval", MS2MIN(m_iModChangerInterval)); //Minutes to miliseconds
	ini.WriteInt(L"ModChangerThreshold", m_iModChangerThreshold);
	ini.WriteInt(L"ModChangerPunishment", m_uModChangerPunishment);
	ini.WriteBool(L"DetectUserNameChanger", m_bDetectUserNameChanger);
	ini.WriteInt(L"UserNameChangerInterval", MS2MIN(m_iUserNameChangerInterval)); //Minutes to miliseconds
	ini.WriteInt(L"UserNameChangerThreshold", m_iUserNameChangerThreshold);
	ini.WriteInt(L"UserNameChangerPunishment", m_uUserNameChangerPunishment);
	ini.WriteBool(L"DetectTCPErrorFlooder", m_bDetectTCPErrorFlooder);
	ini.WriteInt(L"TCPErrorFlooderInterval", MS2MIN(m_iTCPErrorFlooderInterval)); //Minutes to miliseconds
	ini.WriteInt(L"TCPErrorFlooderThreshold", m_iTCPErrorFlooderThreshold);
	ini.WriteInt(L"TCPErrorFlooderPunishment", m_uTCPErrorFlooderPunishment);
	ini.WriteBool(L"DetectEmptyUserNameEmule", m_bDetectEmptyUserNameEmule);
	ini.WriteInt(L"EmptyUserNameEmulePunishment", m_uEmptyUserNameEmulePunishment);
	ini.WriteBool(L"DetectCommunity", m_bDetectCommunity);
	ini.WriteInt(L"CommunityPunishment", m_uCommunityPunishment);
	ini.WriteBool(L"DetectFakeEmule", m_bDetectFakeEmule);
	ini.WriteInt(L"FakeEmulePunishment", m_uFakeEmulePunishment);
	ini.WriteBool(L"DetectHexModName", m_bDetectHexModName);
	ini.WriteInt(L"HexModNamePunishment", m_uHexModNamePunishment);
	ini.WriteBool(L"DetectGhostMod", m_bDetectGhostMod);
	ini.WriteInt(L"GhostModPunishment", m_uGhostModPunishment);
	ini.WriteBool(L"DetectSpam", m_bDetectSpam);
	ini.WriteInt(L"SpamPunishment", m_uSpamPunishment);
	ini.WriteBool(L"DetectEmcrypt", m_bDetectEmcrypt);
	ini.WriteInt(L"EmcryptPunishment", m_uEmcryptPunishment);
	ini.WriteBool(L"DetectXSExploiter", m_bDetectXSExploiter);
	ini.WriteInt(L"XSExploiterPunishment", m_uXSExploiterPunishment);
	ini.WriteBool(L"DetectFileFaker", m_bDetectFileFaker);
	ini.WriteInt(L"FileFakerPunishment", m_uFileFakerPunishment);
	ini.WriteBool(L"DetectUploadFaker", m_bDetectUploadFaker);
	ini.WriteInt(L"UploadFakerPunishment", m_uUploadFakerPunishment);
	ini.WriteBool(L"DetectAgressive", m_bDetectAgressive);
	ini.WriteInt(L"AgressiveTime", m_iAgressiveTime);
	ini.WriteInt(L"AgressiveCounter", m_iAgressiveCounter);
	ini.WriteBool(L"AgressiveLog", m_bAgressiveLog);
	ini.WriteInt(L"AgressivePunishment", m_uAgressivePunishment);
	ini.WriteBool(L"PunishNonSuiMlDonkey", m_bPunishNonSuiMlDonkey);
	ini.WriteBool(L"PunishNonSuiEdonkey", m_bPunishNonSuiEdonkey);
	ini.WriteBool(L"PunishNonSuiEdonkeyHybrid", m_bPunishNonSuiEdonkeyHybrid);
	ini.WriteBool(L"PunishNonSuiShareaza", m_bPunishNonSuiShareaza);
	ini.WriteBool(L"PunishNonSuiLphant", m_bPunishNonSuiLphant);
	ini.WriteBool(L"PunishNonSuiAmule", m_bPunishNonSuiAmule);
	ini.WriteBool(L"PunishNonSuiEmule", m_bPunishNonSuiEmule);
	ini.WriteInt(L"NonSuiPunishment", m_uNonSuiPunishment);
	ini.WriteBool(L"DetectCorruptedDataSender", m_bDetectCorruptedDataSender);
	ini.WriteBool(L"DetectHashChanger", m_bDetectHashChanger);
	ini.WriteBool(L"DetectFileScanner", m_bDetectFileScanner);
	ini.WriteBool(L"DetectRankFlooder", m_bDetectRankFlooder);
	ini.WriteBool(L"DetectKadRequestFlooder", m_bDetectKadRequestFlooder);
	ini.WriteInt(L"KadRequestFloodBanTreshold", m_iKadRequestFloodBanTreshold);

	///////////////////////////////////////////////////////////////////////////
	// Section: "BlacklistPanel"
	// 
	ini.WriteBool(L"BlacklistAutomatic", m_bBlacklistAutomatic, _T("BlacklistPanel"));
	ini.WriteBool(L"BlacklistManual", m_bBlacklistManual);
	ini.WriteBool(L"BlacklistAutoRemoveFromManual", m_bBlacklistAutoRemoveFromManual);
	ini.WriteBool(L"BlacklistLog", m_bBlacklistLog);
}

void CPreferences::ResetStatsColor(int index)
{
	static const COLORREF defcol[15] =
	{
		RGB(0, 0, 64),		RGB(192, 192, 255),	RGB(128, 255, 128),	RGB(0, 210, 0),		RGB(0, 128, 0),
		RGB(255, 128, 128),	RGB(200, 0, 0),		RGB(140, 0, 0),		RGB(150, 150, 255),	RGB(192, 0, 192),
		RGB(255, 255, 128),	RGB(0, 255, 0), /**/	RGB(255, 255, 255),	RGB(255, 255, 255),	RGB(255, 190, 190)
	};
	if (index >= 0 && index < _countof(defcol)) {
		m_adwStatsColors[index] = defcol[index];
		if (index == 11) /**/
			m_bHasCustomTaskIconColor = false;
	}
}

void CPreferences::GetAllStatsColors(int iCount, LPDWORD pdwColors)
{
	const size_t cnt = iCount * sizeof(*pdwColors);
	memcpy(pdwColors, m_adwStatsColors, min(sizeof m_adwStatsColors, cnt));
	if (cnt > sizeof m_adwStatsColors)
		memset(&pdwColors[sizeof m_adwStatsColors], 0, cnt - sizeof m_adwStatsColors);
}

bool CPreferences::SetAllStatsColors(int iCount, const LPDWORD pdwColors)
{
	bool bModified = false;
	int iMin = min((int)_countof(m_adwStatsColors), iCount);
	for (int i = 0; i < iMin; ++i)
		if (m_adwStatsColors[i] != pdwColors[i]) {
			m_adwStatsColors[i] = pdwColors[i];
			bModified = true;
			if (i == 11)
				m_bHasCustomTaskIconColor = true;
		}

	return bModified;
}

void CPreferences::IniCopy(const CString& si, const CString& di)
{
	CIni ini(GetConfigFile(), _T("eMule"));
	const CString& sValue(ini.GetString(si));
	// Do NOT write empty settings, this will mess up reading of default settings in case
	// there were no settings available at all (fresh emule install)!
	if (!sValue.IsEmpty())
		ini.WriteString(di, sValue, _T("ListControlSetup"));
}

void CPreferences::LoadPreferences()
{
	GetSystemDarkModeStatus();

	uint32 m_uTemp = 0;
	uint32 m_uTemp2 = 0;

	CIni ini(GetConfigFile(), _T("eMule"));
	ini.SetSection(_T("eMule"));

	m_bFirstStart = ini.GetString(_T("AppVersion")).IsEmpty();

#ifdef _BETA
	CString strCurrVersion(theApp.GetAppVersion().Mid(6));
	m_bBetaNaggingDone = (ini.GetString(_T("BetaVersionNotified"), EMPTY) == strCurrVersion);
#endif

#ifdef _DEBUG
	m_iDbgHeap = ini.GetInt(_T("DebugHeap"), 1);
#else
	m_iDbgHeap = 0;
#endif

	m_nWebMirrorAlertLevel = ini.GetInt(_T("WebMirrorAlertLevel"), 0);


	SetUserNick(ini.GetStringUTF8(_T("Nick"), DEFAULT_NICK));
	if (strNick.IsEmpty() || IsDefaultNick(strNick))
		SetUserNick(DEFAULT_NICK);

	m_strIncomingDir = ini.GetString(_T("IncomingDir"), EMPTY);
	if (m_strIncomingDir.IsEmpty()) // We want GetDefaultDirectory to also create the folder, so we have to know if we use the default or not
		m_strIncomingDir = GetDefaultDirectory(EMULE_INCOMINGDIR, true);
	MakeFoldername(m_strIncomingDir);

	// load tempdir(s) setting
	CString sTempdirs(ini.GetString(_T("TempDir"), EMPTY));
	if (sTempdirs.IsEmpty()) // We want GetDefaultDirectory to also create the folder, so we have to know if we use the default or not
		sTempdirs = GetDefaultDirectory(EMULE_TEMPDIR, true);
	sTempdirs.AppendFormat(_T("|%s"), (LPCTSTR)ini.GetString(_T("TempDirs")));

	for (int iPos = 0; iPos >= 0;) {
		CString sTmp(sTempdirs.Tokenize(_T("|"), iPos));
		if (sTmp.Trim().IsEmpty())
			continue;
		MakeFoldername(sTmp);
		bool bDup = false;
		for (INT_PTR i = tempdir.GetCount(); --i >= 0;)	// avoid duplicate tempdirs
			if (sTmp.CompareNoCase(GetTempDir(i)) == 0) {
				bDup = true;
				break;
			}

		if (!bDup && (::PathFileExists(sTmp) || ::CreateDirectory(sTmp, NULL)) || tempdir.IsEmpty())
			tempdir.Add(sTmp);
	}

	const CString strDownloadCapacityMbit(ini.GetString(_T("DownloadCapacityMbit"), MISSING_INI_VALUE));
	if (strDownloadCapacityMbit != MISSING_INI_VALUE) {
		SetMaxGraphDownloadRate(MbitPerSecToKBytesPerSec((uint32)_tstoi(strDownloadCapacityMbit)));
		ini.DeleteKey(_T("DownloadCapacity"));
	}
	else {
		const CString strLegacyDownloadCapacity(ini.GetString(_T("DownloadCapacity"), MISSING_INI_VALUE));
		if (strLegacyDownloadCapacity != MISSING_INI_VALUE) {
			SetMaxGraphDownloadRate((uint32)_tstoi(strLegacyDownloadCapacity));
			ini.WriteInt(_T("DownloadCapacityMbit"), KBytesPerSecToRoundedMbitPerSec(maxGraphDownloadRate));
			ini.DeleteKey(_T("DownloadCapacity"));
		}
		else {
			SetMaxGraphDownloadRate(MbitPerSecToKBytesPerSec(DEFAULT_DOWNLOAD_CAPACITY_MBIT));
		}
	}

	const CString strUploadCapacityMbit(ini.GetString(_T("UploadCapacityMbit"), MISSING_INI_VALUE));
	if (strUploadCapacityMbit != MISSING_INI_VALUE) {
		SetMaxGraphUploadRate(MbitPerSecToKBytesPerSec((uint32)_tstoi(strUploadCapacityMbit)));
		ini.DeleteKey(_T("UploadCapacityNew"));
		ini.DeleteKey(_T("UploadCapacity"));
	}
	else {
		const CString strLegacyUploadCapacityNew(ini.GetString(_T("UploadCapacityNew"), MISSING_INI_VALUE));
		if (strLegacyUploadCapacityNew != MISSING_INI_VALUE) {
			SetMaxGraphUploadRate((uint32)_tstoi(strLegacyUploadCapacityNew));
			ini.WriteInt(_T("UploadCapacityMbit"), maxGraphUploadRate == UNLIMITED ? 0 : KBytesPerSecToRoundedMbitPerSec(maxGraphUploadRate));
			ini.DeleteKey(_T("UploadCapacityNew"));
		}
		else {
			const CString strLegacyUploadCapacity(ini.GetString(_T("UploadCapacity"), MISSING_INI_VALUE));
			if (strLegacyUploadCapacity != MISSING_INI_VALUE) {
				const int nOldUploadCapacity = _tstoi(strLegacyUploadCapacity);
				if (nOldUploadCapacity == 16 && ini.GetInt(_T("MaxUpload"), 12) == 12) {
					// Preserve the old migration path for legacy default profiles.
					SetMaxGraphUploadRate(0);
					ini.WriteInt(_T("MaxUpload"), 100, _T("eMule"));
				}
				else {
					SetMaxGraphUploadRate((uint32)nOldUploadCapacity);
				}
				ini.WriteInt(_T("UploadCapacityMbit"), maxGraphUploadRate == UNLIMITED ? 0 : KBytesPerSecToRoundedMbitPerSec(maxGraphUploadRate));
				ini.DeleteKey(_T("UploadCapacity"));
			}
			else {
				SetMaxGraphUploadRate(MbitPerSecToKBytesPerSec(DEFAULT_UPLOAD_CAPACITY_MBIT));
			}
		}
	}

	m_minupload = (uint32)ini.GetInt(_T("MinUpload"), 1);
	if (m_minupload < 1)
		m_minupload = 1;
	m_maxupload = (uint32)ini.GetInt(_T("MaxUpload"), -1);
	if (m_maxupload > maxGraphUploadRate && m_maxupload != UNLIMITED)
		m_maxupload = maxGraphUploadRate * 4 / 5;

	m_maxdownload = (uint32)ini.GetInt(_T("MaxDownload"), -1);
	if (m_maxdownload > maxGraphDownloadRate && m_maxdownload != UNLIMITED)
		m_maxdownload = maxGraphDownloadRate * 9 / 10;
	maxconnections = ini.GetInt(_T("MaxConnections"), GetRecommendedMaxConnections());
	maxhalfconnections = ini.GetInt(_T("MaxHalfConnections"), 50);
	m_bConditionalTCPAccept = ini.GetBool(_T("ConditionalTCPAccept"), false);

	m_strBindAddrW = ini.GetString(_T("BindAddr")).Trim();
	m_pszBindAddrW = m_strBindAddrW.IsEmpty() ? NULL : (LPCWSTR)m_strBindAddrW;
	m_strBindAddrA = m_strBindAddrW;
	m_pszBindAddrA = m_strBindAddrA.IsEmpty() ? NULL : (LPCSTR)m_strBindAddrA;

	port = (uint16)ini.GetInt(_T("Port"), 0);
	if (port == 0) {
		port = (m_nStartupTcpPortOverride != 0) ? m_nStartupTcpPortOverride : thePrefs.GetRandomTCPPort();
		m_nStartupTcpPortOverride = 0;
	}

	// 0 is a valid value for the UDP port setting, as it is used for disabling it.
	int iPort = ini.GetInt(_T("UDPPort"), INT_MAX/*invalid port value*/);
	udpport = (iPort == INT_MAX) ? thePrefs.GetRandomUDPPort() : (uint16)iPort;

	nServerUDPPort = (uint16)ini.GetInt(_T("ServerUDPPort"), -1); // 0 = Don't use UDP port for servers, -1 = use a random port (for backward compatibility)
	maxsourceperfile = ini.GetInt(_T("MaxSourcesPerFile"), 400);
	{
		// Detect presence without ambiguity: use a sentinel default and compare
		static LPCTSTR const kMissingSentinel = _T("\v§MISSING§\v");
		CString val = ini.GetString(_T("Ui.Language"), kMissingSentinel);
		m_bUiLanguagePresent = (val != kMissingSentinel);
		m_strUiLanguage = (m_bUiLanguagePresent ? (LPCTSTR)val : _T("system"));
	}
	m_bMigrationWizardHandled = ini.GetBool(_T("MigrationWizardHandled"), false);
	m_bMigrationWizardRunOnNextStart = ini.GetBool(_T("MigrationWizardRunOnNextStart"), false);
	m_iSeeShares = (EViewSharedFilesAccess)ini.GetInt(_T("SeeShare"), vsfaNobody);
	m_iToolDelayTime = ini.GetInt(_T("ToolTipDelay"), 1);
	trafficOMeterInterval = ini.GetInt(_T("StatGraphsInterval"), 3);
	statsInterval = ini.GetInt(_T("statsInterval"), 5);
	m_bFillGraphs = ini.GetBool(_T("StatsFillGraphs"));
	dontcompressavi = ini.GetBool(_T("DontCompressAvi"), false);

	m_uDeadServerRetries = ini.GetInt(_T("DeadServerRetry"), 10);
	if (m_uDeadServerRetries > MAX_SERVERFAILCOUNT)
		m_uDeadServerRetries = MAX_SERVERFAILCOUNT;
	CIni iniAI(GetConfigFile(), _T("eMuleAI"));
	const CString strMaxEServerBuddySlotsAI(iniAI.GetString(_T("MaxEServerBuddySlots"), MISSING_INI_VALUE));
	if (strMaxEServerBuddySlotsAI != MISSING_INI_VALUE) {
		m_nMaxEServerBuddySlots = (uint8)_tstoi(strMaxEServerBuddySlotsAI);
		ini.DeleteKey(_T("MaxEServerBuddySlots"));
	}
	else {
		const CString strLegacyMaxEServerBuddySlots(ini.GetString(_T("MaxEServerBuddySlots"), MISSING_INI_VALUE));
		if (strLegacyMaxEServerBuddySlots != MISSING_INI_VALUE) {
			m_nMaxEServerBuddySlots = (uint8)_tstoi(strLegacyMaxEServerBuddySlots);
			iniAI.WriteInt(_T("MaxEServerBuddySlots"), m_nMaxEServerBuddySlots);
			ini.DeleteKey(_T("MaxEServerBuddySlots"));
		}
		else
			m_nMaxEServerBuddySlots = ESERVERBUDDY_DEFAULT_SLOTS;
	}
	if (m_nMaxEServerBuddySlots < ESERVERBUDDY_MIN_SLOTS) m_nMaxEServerBuddySlots = ESERVERBUDDY_MIN_SLOTS;
	if (m_nMaxEServerBuddySlots > ESERVERBUDDY_MAX_SLOTS) m_nMaxEServerBuddySlots = ESERVERBUDDY_MAX_SLOTS;
	m_dwServerKeepAliveTimeout = ini.GetInt(_T("ServerKeepAliveTimeout"), 0);
	splitterbarPosition = ini.GetInt(_T("SplitterbarPosition"), 75);
	if (splitterbarPosition < 9)
		splitterbarPosition = 9;
	else if (splitterbarPosition > 93)
		splitterbarPosition = 93;
	splitterbarPositionStat = ini.GetInt(_T("SplitterbarPositionStat"), 30);
	splitterbarPositionStat_HL = ini.GetInt(_T("SplitterbarPositionStat_HL"), 66);
	splitterbarPositionStat_HR = ini.GetInt(_T("SplitterbarPositionStat_HR"), 33);
	if (splitterbarPositionStat_HR + 1 >= splitterbarPositionStat_HL) {
		splitterbarPositionStat_HL = 66;
		splitterbarPositionStat_HR = 33;
	}
	splitterbarPositionFriend = ini.GetInt(_T("SplitterbarPositionFriend"), 170);
	splitterbarPositionShared = ini.GetInt(_T("SplitterbarPositionShared"), 179);
	splitterbarPositionIRC = ini.GetInt(_T("SplitterbarPositionIRC"), 170);
	splitterbarPositionSvr = ini.GetInt(_T("SplitterbarPositionServer"), 75);
	if (splitterbarPositionSvr > 90 || splitterbarPositionSvr < 10)
		splitterbarPositionSvr = 75;

	m_uTransferWnd1 = ini.GetInt(_T("TransferWnd1"), 0);
	m_uTransferWnd2 = ini.GetInt(_T("TransferWnd2"), 1);

	statsMax = ini.GetInt(_T("VariousStatisticsMaxValue"), 100);
	statsAverageMinutes = ini.GetInt(_T("StatsAverageMinutes"), 5);
	MaxConperFive = ini.GetInt(_T("MaxConnectionsPerFiveSeconds"), GetDefaultMaxConperFive());

	reconnect = ini.GetBool(_T("Reconnect"), true);
	m_bUseServerPriorities = ini.GetBool(_T("Scoresystem"), true);
	m_bUseUserSortedServerList = ini.GetBool(_T("UserSortedServerList"), true);
	ICH = ini.GetBool(_T("ICH"), true);
	m_bAutoUpdateServerList = ini.GetBool(_T("Serverlist"), false);

	// since the minimize to tray button is not working under Aero (at least not at this point),
	// we enable map the minimize to tray on the minimize button by default if Aero is running
	if (IsRunningAeroGlassTheme())
		mintotray = ini.GetBool(_T("MinToTray_Aero"), true);
	else
		mintotray = ini.GetBool(_T("MinToTray"), false);

	m_bPreventStandby = ini.GetBool(_T("PreventStandby"), false);
	m_bStoreSearches = ini.GetBool(_T("StoreSearches"), true);
	m_bAddServersFromServer = ini.GetBool(_T("AddServersFromServer"), false);
	m_bAddServersFromClients = ini.GetBool(_T("AddServersFromClient"), false);
	splashscreen = ini.GetBool(_T("Splashscreen"), true);
	bringtoforeground = ini.GetBool(_T("BringToFront"), true);
	transferDoubleclick = ini.GetBool(_T("TransferDoubleClick"), false);
	beepOnError = ini.GetBool(_T("BeepOnError"), true);
	confirmExit = ini.GetBool(_T("ConfirmExit"), true);
	filterLANIPs = ini.GetBool(_T("FilterBadIPs"), true);
	m_bAllocLocalHostIP = ini.GetBool(_T("AllowLocalHostIP"), false);
	autoconnect = ini.GetBool(_T("Autoconnect"), false);
	showRatesInTitle = ini.GetBool(_T("ShowRatesOnTitle"), true);
	m_bIconflashOnNewMessage = ini.GetBool(_T("IconflashOnNewMessage"), true);

	onlineSig = ini.GetBool(_T("OnlineSignature"), false);
	startMinimized = ini.GetBool(_T("StartupMinimized"), false);
	m_bAutoStart = ini.GetBool(_T("AutoStart"), false);
	m_bRestoreLastMainWndDlg = ini.GetBool(_T("RestoreLastMainWndDlg"), false);
	m_iLastMainWndDlgID = ini.GetInt(_T("LastMainWndDlgID"), 0);
	m_bRestoreLastLogPane = ini.GetBool(_T("RestoreLastLogPane"), false);
	m_iLastLogPaneID = ini.GetInt(_T("LastLogPaneID"), 0);
	m_bSafeServerConnect = ini.GetBool(_T("SafeServerConnect"), false);

	m_bTransflstRemain = ini.GetBool(_T("TransflstRemainOrder"), false);
	filterserverbyip = ini.GetBool(_T("FilterServersByIP"), false);
	m_bDontFilterPrivateIPs = ini.GetBool(_T("DontFilterPrivateIPs"), true);
	filterlevel = ini.GetInt(_T("FilterLevel"), 127);
	checkDiskspace = ini.GetBool(_T("CheckDiskspace"), true);
	m_uMinFreeDiskSpace = (UINT)ini.GetInt(_T("MinFreeDiskSpace"), (int)DEFAULT_MIN_FREE_DISK_SPACE);
	m_uTemp = ini.GetInt(_T("FreeDiskSpaceCheckPeriod"), DISKSPACERECHECKTIME);
	if (m_uTemp < 1 || m_uTemp > 60)
		m_uTemp = DISKSPACERECHECKTIME;
	m_uFreeDiskSpaceCheckPeriod = MIN2S(m_uTemp);
	m_bSparsePartFiles = ini.GetBool(_T("SparsePartFiles"), false);
	m_bResolveSharedShellLinks = ini.GetBool(_T("ResolveSharedShellLinks"), false);
	m_bKeepUnavailableFixedSharedDirs = ini.GetBool(_T("KeepUnavailableFixedSharedDirs"), false);
	m_strYourHostname = ini.GetString(_T("YourHostname"), EMPTY);
	m_bImportParts = false; //enable on demand for the current session only

	// Barry - New properties...
	m_bAutoConnectToStaticServersOnly = ini.GetBool(_T("AutoConnectStaticOnly"), false);
	autotakeed2klinks = ini.GetBool(_T("AutoTakeED2KLinks"), true);
	addnewfilespaused = ini.GetBool(_T("AddNewFilesPaused"), false);
	depth3D = ini.GetInt(_T("3DDepth"), 5);
	m_bEnableMiniMule = ini.GetBool(_T("MiniMule"), true);

	// Notifier
	notifierConfiguration = ini.GetString(_T("NotifierConfiguration"), GetMuleDirectory(EMULE_CONFIGDIR) + _T("Notifier.ini"));
	notifierOnDownloadFinished = ini.GetBool(_T("NotifyOnDownload"));
	notifierOnNewDownload = ini.GetBool(_T("NotifyOnNewDownload"));
	notifierOnChat = ini.GetBool(_T("NotifyOnChat"));
	notifierOnLog = ini.GetBool(_T("NotifyOnLog"));
	notifierOnImportantError = ini.GetBool(_T("NotifyOnImportantError"));
	notifierOnEveryChatMsg = ini.GetBool(_T("NotifierPopEveryChatMessage"));
	notifierSoundType = (ENotifierSoundType)ini.GetInt(_T("NotifierUseSound"), ntfstNoSound);
	notifierSoundFile = ini.GetString(_T("NotifierSoundPath"));

	m_strDateTimeFormat = ini.GetString(_T("DateTimeFormat"), _T("%c"));
	m_strDateTimeFormat4Log = ini.GetString(_T("DateTimeFormat4Log"), _T("%c"));
	m_strDateTimeFormat4Lists = ini.GetString(_T("DateTimeFormat4Lists"), _T("%c"));

	m_strIRCServer = ini.GetString(_T("DefaultIRCServerNew"), _T("ircchat.emule-project.net"));
	m_strIRCNick = ini.GetString(_T("IRCNick"));
	m_bIRCAddTimeStamp = ini.GetBool(_T("IRCAddTimestamp"), true);
	m_bIRCUseChannelFilter = ini.GetBool(_T("IRCUseFilter"), true);
	m_strIRCChannelFilter = ini.GetString(_T("IRCFilterName"), EMPTY);
	if (m_strIRCChannelFilter.IsEmpty())
		m_bIRCUseChannelFilter = false;
	m_uIRCChannelUserFilter = ini.GetInt(_T("IRCFilterUser"), 0);
	m_strIRCPerformString = ini.GetString(_T("IRCPerformString"));
	m_bIRCUsePerform = ini.GetBool(_T("IRCUsePerform"), false);
	m_bIRCGetChannelsOnConnect = ini.GetBool(_T("IRCListOnConnect"), true);
	m_bIRCAcceptLinks = ini.GetBool(_T("IRCAcceptLink"), true);
	m_bIRCAcceptLinksFriendsOnly = ini.GetBool(_T("IRCAcceptLinkFriends"), true);
	m_bIRCPlaySoundEvents = ini.GetBool(_T("IRCSoundEvents"), false);
	m_bIRCIgnoreMiscMessages = ini.GetBool(_T("IRCIgnoreMiscMessages"), false);
	m_bIRCIgnoreJoinMessages = ini.GetBool(_T("IRCIgnoreJoinMessages"), true);
	m_bIRCIgnorePartMessages = ini.GetBool(_T("IRCIgnorePartMessages"), true);
	m_bIRCIgnoreQuitMessages = ini.GetBool(_T("IRCIgnoreQuitMessages"), true);
	m_bIRCIgnorePingPongMessages = ini.GetBool(_T("IRCIgnorePingPongMessages"), false);
	m_bIRCIgnoreEmuleAddFriendMsgs = ini.GetBool(_T("IRCIgnoreEmuleAddFriendMsgs"), false);
	m_bIRCAllowEmuleAddFriend = ini.GetBool(_T("IRCAllowEmuleAddFriend"), true);
	m_bIRCIgnoreEmuleSendLinkMsgs = ini.GetBool(_T("IRCIgnoreEmuleSendLinkMsgs"), false);
	m_bIRCJoinHelpChannel = ini.GetBool(_T("IRCHelpChannel"), true);
	m_bIRCEnableSmileys = ini.GetBool(_T("IRCEnableSmileys"), true);
	m_bMessageEnableSmileys = ini.GetBool(_T("MessageEnableSmileys"), true);
	m_bIRCEnableUTF8 = ini.GetBool(_T("IRCEnableUTF8"), true);

	m_bSmartServerIdCheck = ini.GetBool(_T("SmartIdCheck"), true);
	log2disk = ini.GetBool(_T("SaveLogToDisk"), false);
	uMaxLogFileSize = ini.GetInt(_T("MaxLogFileSize"), 1024 * 1024);
	iMaxLogBuff = ini.GetInt(_T("MaxLogBuff"), 64) * 1024;
	m_iLogFileFormat = (ELogFileFormat)ini.GetInt(_T("LogFileFormat"), Unicode);
	m_bEnableVerboseOptions = ini.GetBool(_T("VerboseOptions"), true);
    if (m_bEnableVerboseOptions) {
		m_bVerbose = ini.GetBool(_T("Verbose"), false);
		m_bFullVerbose = ini.GetBool(_T("FullVerbose"), false);
		debug2disk = ini.GetBool(_T("SaveDebugToDisk"), false);
		m_bDebugSourceExchange = ini.GetBool(_T("DebugSourceExchange"), false);
		m_bLogBannedClients = ini.GetBool(_T("LogBannedClients"), true);
		m_bLogRatingDescReceived = ini.GetBool(_T("LogRatingDescReceived"), true);
		m_bLogSecureIdent = ini.GetBool(_T("LogSecureIdent"), true);
		m_bLogFilteredIPs = ini.GetBool(_T("LogFilteredIPs"), true);
		m_bLogFileSaving = ini.GetBool(_T("LogFileSaving"), false);
		m_bLogA4AF = ini.GetBool(_T("LogA4AF"), false); // ZZ:DownloadManager
		m_bLogUlDlEvents = ini.GetBool(_T("LogUlDlEvents"), true);
		m_bLogSpamRating = ini.GetBool(_T("LogSpamRating"), false);
		m_bLogRetryFailedTcp = ini.GetBool(_T("LogRetryFailedTcp"), false);
        m_bLogExtendedSXEvents = ini.GetBool(_T("LogExtendedSXEvents"), false);
        m_bLogNatTraversalEvents = ini.GetBool(_T("LogNatTraversalEvents"), false);
    } else {
		if (m_bRestoreLastLogPane && m_iLastLogPaneID >= 2)
			m_iLastLogPaneID = 1;
	}

#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
	// following options are for debugging or when using an external debug device viewer only.
	m_iDebugServerTCPLevel = ini.GetInt(_T("DebugServerTCP"), 0);
	m_iDebugServerUDPLevel = ini.GetInt(_T("DebugServerUDP"), 0);
	m_iDebugServerSourcesLevel = ini.GetInt(_T("DebugServerSources"), 0);
	m_iDebugServerSearchesLevel = ini.GetInt(_T("DebugServerSearches"), 0);
	m_iDebugClientTCPLevel = ini.GetInt(_T("DebugClientTCP"), 0);
	m_iDebugClientUDPLevel = ini.GetInt(_T("DebugClientUDP"), 0);
	m_iDebugClientKadUDPLevel = ini.GetInt(_T("DebugClientKadUDP"), 0);
	m_iDebugSearchResultDetailLevel = ini.GetInt(_T("DebugSearchResultDetailLevel"), 0);
#else
	// for normal release builds ensure that all these options are turned off
	m_iDebugServerTCPLevel = 0;
	m_iDebugServerUDPLevel = 0;
	m_iDebugServerSourcesLevel = 0;
	m_iDebugServerSearchesLevel = 0;
	m_iDebugClientTCPLevel = 0;
	m_iDebugClientUDPLevel = 0;
	m_iDebugClientKadUDPLevel = 0;
	m_iDebugSearchResultDetailLevel = 0;
#endif

	m_bpreviewprio = ini.GetBool(_T("PreviewPrio"), true);
	m_bupdatequeuelist = ini.GetBool(_T("UpdateQueueListPref"), false);
	m_bManualAddedServersHighPriority = ini.GetBool(_T("ManualHighPrio"), false);
	m_btransferfullchunks = ini.GetBool(_T("FullChunkTransfers"), true);
	m_istartnextfile = ini.GetInt(_T("StartNextFile"), 0);
	m_bshowoverhead = ini.GetBool(_T("ShowOverhead"), false);
	m_bMoviePreviewBackup = ini.GetBool(_T("VideoPreviewBackupped"), false);
	m_iPreviewSmallBlocks = ini.GetInt(_T("PreviewSmallBlocks"), 1);
	m_bPreviewCopiedArchives = ini.GetBool(_T("PreviewCopiedArchives"), false);
	m_bAllocFull = ini.GetBool(_T("AllocateFullFile"), 0);
	m_bAutomaticArcPreviewStart = ini.GetBool(_T("AutoArchivePreviewStart"), true);
	m_bShowSharedFilesDetails = ini.GetBool(_T("ShowSharedFilesDetails"), true);
	m_bAutoShowLookups = ini.GetBool(_T("AutoShowLookups"), true);
	m_bShowUpDownIconInTaskbar = ini.GetBool(_T("ShowUpDownIconInTaskbar"), false);
	m_bShowWin7TaskbarGoodies = ini.GetBool(_T("ShowWin7TaskbarGoodies"), true);
	m_bForceSpeedsToKB = ini.GetBool(_T("ForceSpeedsToKB"), false);

	// Get file buffer size (with backward compatibility)
	m_uFileBufferSize = ini.GetInt(_T("FileBufferSizePref"), 0); // old setting
	if (m_uFileBufferSize == 0)
		m_uFileBufferSize = 20 * 1024 * 1024;
	else
		m_uFileBufferSize = ((m_uFileBufferSize * 15000 + 512) / 1024) * 1024;
	m_uFileBufferSize = ini.GetInt(_T("FileBufferSize"), m_uFileBufferSize);
	m_uFileBufferTimeLimit = SEC2MS(ini.GetInt(_T("FileBufferTimeLimit"), 600));

	// Get queue size (with backward compatibility)
	m_iQueueSize = (INT_PTR)ini.GetInt(_T("QueueSizePref"), 200) * 100; // old setting
	m_iQueueSize = ini.GetInt(_T("QueueSize"), (int)m_iQueueSize);

	m_iCommitFiles = ini.GetInt(_T("CommitFiles"), 2); // 1 = "commit" on application shutdown; 2 = "commit" on each file saving
	m_bDAP = ini.GetBool(_T("DAPPref"), true);
	m_bUAP = ini.GetBool(_T("UAPPref"), true);
	m_bPreviewOnIconDblClk = ini.GetBool(_T("PreviewOnIconDblClk"), true);
	m_bCheckFileOpen = ini.GetBool(_T("CheckFileOpen"), true);
	indicateratings = ini.GetBool(_T("IndicateRatings"), true);
	watchclipboard = ini.GetBool(_T("WatchClipboard4ED2kFilelinks"), true);
	m_iSearchMethod = ini.GetInt(_T("SearchMethod"), 0);
	m_strSearchFileType = ini.GetString(_T("SearchFileType"), _T(ED2KFTSTR_ANY));
	showCatTabInfos = ini.GetBool(_T("ShowInfoOnCatTabs"), true);
	dontRecreateGraphs = ini.GetBool(_T("DontRecreateStatGraphsOnResize"), false);
	m_bExtControls = ini.GetBool(_T("ShowExtControls"), true);

	versioncheckLastAutomatic = ini.GetInt(_T("VersionCheckLastAutomatic"), 0);
	versioncheckLastKnownVersionOnServer = ini.GetString(_T("VersionCheckLastKnownVersionOnServer"), EMPTY);
	versioncheckLastKnownVersionOnServer.Trim();
	m_bDisableKnownClientList = ini.GetBool(_T("DisableKnownClientList"), false);
	m_bDisableQueueList = ini.GetBool(_T("DisableQueueList"), false);
	m_bCreditSystem = ini.GetBool(_T("UseCreditSystem"), true);
	scheduler = ini.GetBool(_T("EnableScheduler"), false);
	msgonlyfriends = ini.GetBool(_T("MessagesFromFriendsOnly"), false);
	msgsecure = ini.GetBool(_T("MessageFromValidSourcesOnly"), true);
	m_bUseChatCaptchas = ini.GetBool(_T("MessageUseCaptchas"), true);
	autofilenamecleanup = ini.GetBool(_T("AutoFilenameCleanup"), false);
	m_bUseAutocompl = ini.GetBool(_T("UseAutocompletion"), true);
	m_bShowDwlPercentage = ini.GetBool(_T("ShowDwlPercentage"), true);
	networkkademlia = ini.GetBool(_T("NetworkKademlia"), true);
	networked2k = ini.GetBool(_T("NetworkED2K"), true);
	m_bRemove2bin = ini.GetBool(_T("RemoveFilesToBin"), true);
	m_bShowCopyEd2kLinkCmd = ini.GetBool(_T("ShowCopyEd2kLinkCmd"), true);

	m_iMaxChatHistory = ini.GetInt(_T("MaxChatHistoryLines"), 100);
	if (m_iMaxChatHistory < 1)
		m_iMaxChatHistory = 100;
	maxmsgsessions = ini.GetInt(_T("MaxMessageSessions"), 50);

	m_bShowActiveDownloadsBold = ini.GetBool(_T("ShowActiveDownloadsBold"), true);

	m_strTxtEditor = ini.GetString(_T("TxtEditor"), _T("notepad.exe"));
	m_strVideoPlayer = ini.GetString(_T("VideoPlayer"), EMPTY);
	if (m_strVideoPlayer.IsEmpty())
		m_strVideoPlayer = GetDefaultVideoPlayerPath();
	m_strVideoPlayerArgs = ini.GetString(_T("VideoPlayerArgs"), EMPTY);

	m_strTemplateFile = ini.GetString(_T("WebTemplateFile"), GetMuleDirectory(EMULE_EXECUTABLEDIR) + _T("eMule.tmpl"));
	// if emule is using the default, check if the file is in the config folder, as it used to be val prior version
	// and might be wanted by the user when switching to a personalized template
	if (m_strTemplateFile.Compare(GetMuleDirectory(EMULE_EXECUTABLEDIR) + _T("eMule.tmpl")) == 0)
		if (::PathFileExists(GetMuleDirectory(EMULE_CONFIGDIR) + _T("eMule.tmpl")))
			m_strTemplateFile = GetMuleDirectory(EMULE_CONFIGDIR) + _T("eMule.tmpl");

	messageFilter = ini.GetStringLong(_T("MessageFilter"), _T("Your client has an infinite queue|Your client is connecting too fast|fastest download speed|ZamBoR|DI-Emule|Join the L33cher|eMule FX|FXeMule|Ketamine|robot from RIAA|stolen client hashes"));
	commentFilter = ini.GetStringLong(_T("CommentFilter"), _T("http://|https://|ftp://|www.|ftp."));
	commentFilter.MakeLower();
	filenameCleanups = ini.GetStringLong(_T("FilenameCleanups"), _T("http|www.|.com|.de|.org|.net|shared|powered|sponsored|sharelive|filedonkey|"));
	m_iExtractMetaData = ini.GetInt(_T("ExtractMetaData"), 1); // 0=disable, 1=mp3, 2=MediaDet
	if (m_iExtractMetaData > 1)
		m_iExtractMetaData = 1;
	m_bRearrangeKadSearchKeywords = ini.GetBool(_T("RearrangeKadSearchKeywords"), true);

	m_bUseSecureIdent = ini.GetBool(_T("SecureIdent"), true);
	m_bAdvancedSpamfilter = ini.GetBool(_T("AdvancedSpamFilter"), true);
	m_bRemoveFinishedDownloads = ini.GetBool(_T("AutoClearCompleted"), false);
	m_bUseOldTimeRemaining = ini.GetBool(L"UseSimpleTimeRemainingcomputation", true);

	// Toolbar
	m_sToolbarSettings = ini.GetString(_T("ToolbarSetting"), strDefaultToolbar);
	m_sToolbarBitmap = ini.GetString(_T("ToolbarBitmap"), EMPTY);
	m_sToolbarBitmapFolder = ini.GetString(_T("ToolbarBitmapFolder"), EMPTY);
	if (m_sToolbarBitmapFolder.IsEmpty()) // We want GetDefaultDirectory to also create the folder, so we have to know if we use the default or not
		m_sToolbarBitmapFolder = GetDefaultDirectory(EMULE_TOOLBARDIR, true);
	else
		slosh(m_sToolbarBitmapFolder);
	m_nToolbarLabels = (EToolbarLabelType)ini.GetInt(_T("ToolbarLabels"), CMuleToolbarCtrl::GetDefaultLabelType());
	m_bReBarToolbar = ini.GetBool(_T("ReBarToolbar"), 1);
	m_sizToolbarIconSize.cx = m_sizToolbarIconSize.cy = ini.GetInt(_T("ToolbarIconSize"), 32);
	m_iStraightWindowStyles = ini.GetInt(_T("StraightWindowStyles"), 0);
	m_bUseSystemFontForMainControls = ini.GetBool(_T("UseSystemFontForMainControls"), 0);
	m_bRTLWindowsLayout = ini.GetBool(_T("RTLWindowsLayout"));
	m_strSkinProfile = ini.GetString(_T("SkinProfile"), EMPTY);
	m_strSkinProfileDir = ini.GetString(_T("SkinProfileDir"), EMPTY);
	if (m_strSkinProfileDir.IsEmpty()) // We want GetDefaultDirectory to also create the folder, so we have to know if we use the default or not
		m_strSkinProfileDir = GetDefaultDirectory(EMULE_SKINDIR, true);
	else
		slosh(m_strSkinProfileDir);

	LPBYTE pData = NULL;
	UINT uSize = sizeof m_lfHyperText;
	if (ini.GetBinary(_T("HyperTextFont"), &pData, &uSize) && uSize == sizeof m_lfHyperText)
		memcpy(&m_lfHyperText, pData, sizeof m_lfHyperText);
	else
		memset(&m_lfHyperText, 0, sizeof m_lfHyperText);
	delete[] pData;

	pData = NULL;
	uSize = sizeof m_lfLogText;
	if (ini.GetBinary(_T("LogTextFont"), &pData, &uSize) && uSize == sizeof m_lfLogText)
		memcpy(&m_lfLogText, pData, sizeof m_lfLogText);
	else
		memset(&m_lfLogText, 0, sizeof m_lfLogText);
	delete[] pData;

	m_crLogError = ini.GetColRef(_T("LogErrorColor"), m_crLogError);
	m_crLogWarning = ini.GetColRef(_T("LogWarningColor"), m_crLogWarning);
	m_crLogSuccess = ini.GetColRef(_T("LogSuccessColor"), m_crLogSuccess);
	if (IsDarkModeEnabled()) {
		if (m_crLogError == RGB(255, 0, 0))
			m_crLogError = RGB(255, 102, 102);	// Light Red
		if (m_crLogWarning == RGB(128, 0, 128))
			m_crLogWarning = RGB(186, 85, 211);	// Light Purple (Orchid)
		if (m_crLogSuccess == RGB(0, 0, 255))
			m_crLogSuccess = RGB(173, 216, 255); // Very Light Blue
	} else {
		if (thePrefs.m_crLogError == RGB(255, 102, 102))	// Light Red
			thePrefs.m_crLogError = RGB(255, 0, 0);
		if (thePrefs.m_crLogWarning == RGB(186, 85, 211))	// Light Purple (Orchid)
			thePrefs.m_crLogWarning = RGB(128, 0, 128);
		if (thePrefs.m_crLogSuccess == RGB(173, 216, 255))	// Very Light Blue
			thePrefs.m_crLogSuccess = RGB(0, 0, 255);
	}

	if (statsAverageMinutes < 1)
		statsAverageMinutes = 5;

	m_bDynUpEnabled = ini.GetBool(_T("USSEnabled"), false);
	m_bDynUpUseMillisecondPingTolerance = ini.GetBool(_T("USSUseMillisecondPingTolerance"), false);
	m_iDynUpPingTolerance = ini.GetInt(_T("USSPingTolerance"), 500);
	m_iDynUpPingToleranceMilliseconds = ini.GetInt(_T("USSPingToleranceMilliseconds"), 200);
	m_iDynUpGoingUpDivider = ini.GetInt(_T("USSGoingUpDivider"), 1000);
	m_iDynUpGoingDownDivider = ini.GetInt(_T("USSGoingDownDivider"), 1000);
	m_iDynUpNumberOfPings = ini.GetInt(_T("USSNumberOfPings"), 1);

	m_bA4AFSaveCpu = ini.GetBool(_T("A4AFSaveCpu"), false); // ZZ:DownloadManager
	m_bHighresTimer = ini.GetBool(_T("HighresTimer"), true);
	m_bRunAsUser = ini.GetBool(_T("RunAsUnprivilegedUser"), false);
	m_bPreferRestrictedOverUser = ini.GetBool(_T("PreferRestrictedOverUser"), false);
	m_bOpenPortsOnStartUp = ini.GetBool(_T("OpenPortsOnStartUp"), false);
	m_byLogLevel = ini.GetInt(_T("DebugLogLevel"), DLP_VERYLOW);
	m_bTrustEveryHash = ini.GetBool(_T("AICHTrustEveryHash"), false);
	m_bRememberCancelledFiles = ini.GetBool(_T("RememberCancelledFiles"), true);
	m_bRememberDownloadedFiles = ini.GetBool(_T("RememberDownloadedFiles"), true);
	m_bPartiallyPurgeOldKnownFiles = ini.GetBool(_T("PartiallyPurgeOldKnownFiles"), false);

	m_email.bSendMail = IsRunningXPSP2OrHigher() && ini.GetBool(_T("NotifierSendMail"), false);
	m_email.uAuth = static_cast<SMTPauth>(ini.GetInt(_T("NotifierMailAuth"), 0));
	m_email.uTLS = static_cast<TLSmode>(ini.GetInt(_T("NotifierMailTLS"), 0));
	m_email.sFrom = ini.GetString(_T("NotifierMailSender"), EMPTY);
	m_email.sServer = ini.GetString(_T("NotifierMailServer"), EMPTY);
	m_email.uPort = static_cast<uint16>(ini.GetInt(_T("NotifierMailPort"), 0));
	m_email.sTo = ini.GetString(_T("NotifierMailRecipient"), EMPTY);
	m_email.sUser = ini.GetString(_T("NotifierMailLogin"), EMPTY);
	m_email.sPass = ini.GetString(_T("NotifierMailPassword"), EMPTY);

	m_bWinaTransToolbar = ini.GetBool(_T("WinaTransToolbar"), true);
	m_bShowDownloadToolbar = ini.GetBool(_T("ShowDownloadToolbar"), true);

	m_bCryptLayerRequested = ini.GetBool(_T("CryptLayerRequested"), true);
	m_bCryptLayerRequired = ini.GetBool(_T("CryptLayerRequired"), false);
	m_bCryptLayerSupported = ini.GetBool(_T("CryptLayerSupported"), true);
	m_dwKadUDPKey = ini.GetInt(_T("KadUDPKey"), GetRandomUInt32());

	uint32 nTmp = ini.GetInt(_T("CryptTCPPaddingLength"), 128);
	m_byCryptTCPPaddingLength = (uint8)min(nTmp, 254);

	m_bEnableSearchResultFilter = ini.GetBool(_T("EnableSearchResultSpamFilter"), true);

	///////////////////////////////////////////////////////////////////////////
	// Section: "Proxy"
	//
	proxy.bEnablePassword = ini.GetBool(_T("ProxyEnablePassword"), false, _T("Proxy"));
	proxy.bUseProxy = ini.GetBool(_T("ProxyEnableProxy"), false);
	proxy.host = ini.GetString(_T("ProxyName"), EMPTY);
	proxy.user = ini.GetString(_T("ProxyUser"), EMPTY);
	proxy.password = ini.GetString(_T("ProxyPassword"), EMPTY);
	proxy.port = (uint16)ini.GetInt(_T("ProxyPort"), 1080);
	proxy.type = (uint16)ini.GetInt(_T("ProxyType"), PROXYTYPE_NOPROXY);


	///////////////////////////////////////////////////////////////////////////
	// Section: "Statistics"
	//
	statsSaveInterval = ini.GetInt(_T("SaveInterval"), 60, _T("Statistics"));
	statsConnectionsGraphRatio = ini.GetInt(_T("statsConnectionsGraphRatio"), 3);
	m_strStatsExpandedTreeItems = ini.GetString(_T("statsExpandedTreeItems"), _T("11111111111111111111111111111111111111"));
	CString buffer;
	for (unsigned i = 0; i < _countof(m_adwStatsColors); ++i) {
		buffer.Format(_T("StatColor%u"), i);
		m_adwStatsColors[i] = 0;
		if (_stscanf(ini.GetString(buffer, EMPTY), _T("%li"), (long*)&m_adwStatsColors[i]) != 1)
			ResetStatsColor(i);
	}
	m_bHasCustomTaskIconColor = ini.GetBool(_T("HasCustomTaskIconColor"), false);
	m_bShowVerticalHourMarkers = ini.GetBool(_T("ShowVerticalHourMarkers"), true);

	// I changed this to a separate function because it is now also used
	// to load the stats backup and to load stats from preferences.ini.old.
	LoadStats();

	///////////////////////////////////////////////////////////////////////////
	// Section: "WebServer"
	//
	m_strWebPassword = ini.GetString(_T("Password"), EMPTY, _T("WebServer"));
	m_strWebLowPassword = ini.GetString(_T("PasswordLow"), EMPTY);
	m_nWebPort = (uint16)ini.GetInt(_T("Port"), 4711);
	m_bWebUseUPnP = ini.GetBool(_T("WebUseUPnP"), false);
	m_bWebEnabled = ini.GetBool(_T("Enabled"), false);
	m_bWebUseGzip = ini.GetBool(_T("UseGzip"), true);
	m_bWebLowEnabled = ini.GetBool(_T("UseLowRightsUser"), false);
	m_nWebPageRefresh = ini.GetInt(_T("PageRefreshTime"), 120);
	m_iWebTimeoutMins = ini.GetInt(_T("WebTimeoutMins"), 5);
	m_iWebFileUploadSizeLimitMB = ini.GetInt(_T("MaxFileUploadSizeMB"), 5);
	m_bAllowAdminHiLevFunc = ini.GetBool(_T("AllowAdminHiLevelFunc"), false);

	buffer = ini.GetString(_T("AllowedIPs"));
	for (int iPos = 0; iPos >= 0;) {
		const CString& strIP(buffer.Tokenize(_T(";"), iPos));
		if (!strIP.IsEmpty()) {
			u_long nIP = inet_addr((CStringA)strIP);
			if (nIP != INADDR_ANY && nIP != INADDR_NONE)
				m_aAllowedRemoteAccessIPs.Add(nIP);
		}
	}
	m_bWebUseHttps = ini.GetBool(_T("UseHTTPS"), false);
	m_sWebHttpsCertificate = ini.GetString(_T("HTTPSCertificate"), EMPTY);
	m_sWebHttpsKey = ini.GetString(_T("HTTPSKey"), EMPTY);

	///////////////////////////////////////////////////////////////////////////
	// Section: "UPnP"
	//
	m_bEnableUPnP = ini.GetBool(_T("EnableUPnP"), false, _T("UPnP"));
	m_bSkipWANIPSetup = ini.GetBool(_T("SkipWANIPSetup"), false);
	m_bSkipWANPPPSetup = ini.GetBool(_T("SkipWANPPPSetup"), false);
	m_bCloseUPnPOnExit = ini.GetBool(_T("CloseUPnPOnExit"), true);
	m_nLastWorkingImpl = ini.GetInt(_T("LastWorkingImplementation"), 1 /*MiniUPnPLib*/);
	m_bIsMinilibImplDisabled = ini.GetBool(_T("DisableMiniUPNPLibImpl"), false);
	m_bIsWinServImplDisabled = ini.GetBool(_T("DisableWinServImpl"), false);

	///////////////////////////////////////////////////////////////////////////
	// Section: "eMuleAI"
	//
	m_iUIDarkMode = ini.GetInt(L"UIDarkMode", 0);
	m_bUITweaksSpeedGraph = ini.GetBool(L"UITweaksSpeedGraph", true);
	m_bDisableFindAsYouType = ini.GetBool(L"DisableFindAsYouType", false);
	m_iUITweaksListUpdatePeriod = max(ini.GetInt(L"UITweaksListUpdatePeriod", 500), 100); // Minimum 100ms to prevent performance issues
	m_iGeoLite2Mode = ini.GetInt(_T("GeoLite2Mode"), 3, _T("eMuleAI"));
	m_bGeoLite2ShowFlag = ini.GetInt(_T("GeoLite2ShowFlag", 1));
	m_bConnectionChecker = ini.GetBool(_T("ConnectionChecker"), true);
	m_sConnectionCheckerServer = ini.GetString(_T("ConnectionCheckerServer"), _T("https://www.google.com"));
	m_bEnableNatTraversal = ini.GetBool(L"EnableNatTraversal", true);
	int iNatTraversalPortWindow = ini.GetInt(L"NatTraversalPortWindow", 512);
	if (iNatTraversalPortWindow < 0 || iNatTraversalPortWindow > 65535)
		iNatTraversalPortWindow = 512;
	m_uNatTraversalPortWindow = (uint16)iNatTraversalPortWindow;
	int iNatTraversalSweepWindow = ini.GetInt(L"NatTraversalSweepWindow", 16);
	if (iNatTraversalSweepWindow < 0 || iNatTraversalSweepWindow > 65535)
		iNatTraversalSweepWindow = 16;
	m_uNatTraversalSweepWindow = (uint16)iNatTraversalSweepWindow;
	m_uNatTraversalJitterMinMs = (uint32)ini.GetInt(L"NatTraversalJitterMinMs", 50);
	m_uNatTraversalJitterMaxMs = (uint32)ini.GetInt(L"NatTraversalJitterMaxMs", 150);
	if (m_uNatTraversalJitterMinMs < 1) m_uNatTraversalJitterMinMs = 50;
	if (m_uNatTraversalJitterMaxMs < m_uNatTraversalJitterMinMs) m_uNatTraversalJitterMaxMs = m_uNatTraversalJitterMinMs;
	if (m_uNatTraversalJitterMaxMs > 5000) m_uNatTraversalJitterMaxMs = 5000;
	m_uMaxServedBuddies = (uint32)ini.GetInt(L"MaxServedBuddies", 5);
	if (m_uMaxServedBuddies < 5) m_uMaxServedBuddies = 5;
	if (m_uMaxServedBuddies > 100) m_uMaxServedBuddies = 100;

	m_bRetryFailedTcpConnectionAttempts = ini.GetBool(L"RetryFailedTcpConnectionAttempts", true);
	m_breaskSourceAfterIPChange = ini.GetBool(L"ReaskSourcesAfterIPChange", true);
	m_bInformQueuedClientsAfterIPChange = ini.GetBool(L"InformQueuedClientsAfterIPChange", true);
	m_uTemp = ini.GetInt(L"ReAskTime", FILEREASKTIME);
	m_uTemp2 = MS2MIN(FILEREASKTIME);
	m_uTemp = (m_uTemp >= m_uTemp2 && m_uTemp <= 55) ? m_uTemp : m_uTemp2;
	m_uReAskTimeDif = MIN2MS((m_uTemp - m_uTemp2));
	m_iDownloadChecker = ini.GetInt(L"DownloadChecker", 0);
	m_iDownloadCheckerAcceptPercentage = ini.GetInt(L"DownloadCheckerAcceptPercentage", 10);
	m_bDownloadCheckerRejectCanceled = ini.GetBool(L"DownloadCheckerRejectCanceled", true);
	m_bDownloadCheckerRejectSameHash = ini.GetBool(L"DownloadCheckerRejectSameHash", true);
	m_bDownloadCheckerRejectBlacklisted = ini.GetBool(L"DownloadCheckerRejectBlacklisted", true);
	m_bDownloadCheckerCaseInsensitive = ini.GetBool(L"DownloadCheckerCaseInsensitive", true);
	m_bDownloadCheckerIgnoreExtension = ini.GetBool(L"DownloadCheckerIgnoreExtension", true);
	m_bDownloadCheckerIgnoreTags = ini.GetBool(L"DownloadCheckerIgnoreTags", true);
	m_bDownloadCheckerDontIgnoreNumericTags = ini.GetBool(L"DownloadCheckerDontIgnoreNumericTags", true);
	m_bDownloadCheckerIgnoreNonAlphaNumeric = ini.GetBool(L"DownloadCheckerIgnoreNonAlphaNumeric", true);
	m_iDownloadCheckerMinimumComparisonLength = ini.GetInt(L"DownloadCheckerMinimumComparisonLength", 8);
	if (m_iDownloadCheckerMinimumComparisonLength < 4)
		m_iDownloadCheckerMinimumComparisonLength = 8;
	m_bDownloadCheckerSkipIncompleteFileConfirmation = ini.GetBool(L"DownloadCheckerSkipIncompleteFileConfirmation", false);
	m_bDownloadCheckerMarkAsBlacklisted = ini.GetBool(L"DownloadCheckerMarkAsBlacklisted", true);
	m_bDownloadCheckerAutoMarkAsBlacklisted = ini.GetBool(L"DownloadCheckerAutoMarkAsBlacklisted", true);
	m_bFileInspectorFake = ini.GetBool(L"FileInspectorFake", true);
	m_bFileInspectorDRM = ini.GetBool(L"FileInspectorDRM", true);
	m_bFileInspectorInvalidExt = ini.GetBool(L"FileInspectorInvalidExt", true);
	m_iFileInspector = ini.GetInt(L"FileInspector", 2);
	m_iFileInspector = (m_bFileInspectorFake || m_bFileInspectorFake) ? m_iFileInspector : 0; // If none of these checkboxes selected, force 0;
	m_iFileInspectorCheckPeriod = ini.GetInt(L"FileInspectorCheckPeriod", 30);
	if (m_iFileInspectorCheckPeriod < 5)
		m_iFileInspectorCheckPeriod = 30;
	m_iFileInspectorCompletedThreshold = ini.GetInt(L"FileInspectorCompletedThreshold", 1024);
	if (m_iFileInspectorCompletedThreshold < 1)
		m_iFileInspectorCompletedThreshold = 1024;
	m_iFileInspectorZeroPercentageThreshold = ini.GetInt(L"FileInspectorZeroPercentageThreshold", 1);
	if (m_iFileInspectorZeroPercentageThreshold < 1 || m_iFileInspectorZeroPercentageThreshold > 100)
		m_iFileInspectorZeroPercentageThreshold = 90;
	m_iFileInspectorCompressionThreshold = ini.GetInt(L"FileInspectorCompressionThreshold", 100);
	if (m_iFileInspectorCompressionThreshold < 100)
		m_iFileInspectorCompressionThreshold = 100;
	m_bFileInspectorBypassZeroPercentage = ini.GetBool(L"FileInspectorBypassZeroPercentage", true);
	m_iFileInspectorCompressionThresholdToBypassZero = ini.GetInt(L"FileInspectorCompressionThresholdToBypassZero", m_iFileInspectorCompressionThreshold * 10 < 10000 ? 10000 : m_iFileInspectorCompressionThreshold * 10);
	if (m_iFileInspectorCompressionThresholdToBypassZero <= m_iFileInspectorCompressionThreshold)
		m_iFileInspectorCompressionThresholdToBypassZero = m_iFileInspectorCompressionThreshold * 10 < 10000 ? 10000 : m_iFileInspectorCompressionThreshold * 10;
	m_bGroupKnownAtTheBottom = ini.GetBool(_T("GroupKnownAtTheBottom"), true);
	m_iSpamThreshold = ini.GetInt(L"SpamThreshold ", SEARCH_SPAM_THRESHOLD);
	m_iKadSearchKeywordTotal = ini.GetInt(L"KadSearchKeywordTotal ", SEARCHKEYWORD_TOTAL);
	m_bShowCloseButtonOnSearchTabs = ini.GetBool(_T("ShowCloseButtonOnSearchTabs"), true);
	m_bRepeatServerList = ini.GetBool(_T("RepeatServerList"), true);
	m_bDontRemoveStaticServers = ini.GetBool(_T("DontRemoveStaticServers"), true);
	m_bDontSavePartOnReconnect = ini.GetBool(_T("DontSavePartOnReconnect"), true);
	m_bFileHistoryShowPart = ini.GetBool(L"FileHistoryShowPart", true);
	m_bFileHistoryShowShared = ini.GetBool(L"FileHistoryShowShared", true);
	m_bFileHistoryShowDuplicate = ini.GetBool(L"FileHistoryShowDuplicate", true);
	m_bAutoShareSubdirs.store(ini.GetBool(L"AutoShareSubdirs", true), std::memory_order_relaxed);
	m_bDontShareExtensions.store(ini.GetBool(L"DontShareExtensions", true), std::memory_order_relaxed);
	m_sDontShareExtensionsList = ini.GetString(L"DontShareExtensionsList", L".bt!,.jc!,.bc!,.part,.met,.bak");
	m_bAdjustNTFSDaylightFileTime = ini.GetBool(_T("AdjustNTFSDaylightFileTime"), true); // Official preference
	m_bAllowDSTTimeTolerance = ini.GetBool(_T("AllowDSTTimeTolerance"), true);
	m_bEmulateMLDonkey = ini.GetBool(L"EmulateMLDonkey", true);
	m_bEmulateEdonkey = ini.GetBool(L"EmulateEdonkey", true);
	m_bEmulateEdonkeyHybrid = ini.GetBool(L"EmulateEdonkeyHybrid", true);
	m_bEmulateShareaza = ini.GetBool(L"EmulateShareaza", true);
	m_bEmulateLphant = ini.GetBool(L"EmulateLphant", true);
	m_bEmulateCommunity = ini.GetBool(L"EmulateCommunity", true);
	m_iEmulateCommunityTagSavingTreshold = ini.GetInt(L"EmulateCommunityTagSavingTreshold", 3);
	m_bLogEmulator = ini.GetBool(L"LogEmulator", true);
	m_bUseIntelligentChunkSelection = ini.GetBool(L"UseIntelligentChunkSelection", true);
	int iCsModeOriginal = ini.GetInt(L"CreditSystemMode", -1);
	int iCsModeStulle = ini.GetInt(L"CreditSystemMode", -1);
	if (iCsModeStulle != -1)
		creditSystemMode = iCsModeStulle;
	else if (iCsModeOriginal != -1)
		creditSystemMode = iCsModeOriginal;
	else
		creditSystemMode = CS_LOVELACE;
	if ((creditSystemMode < CS_OFFICIAL) || (creditSystemMode > CS_ZZUL))
		creditSystemMode = CS_LOVELACE;
	m_bClientHistory = ini.GetBool(L"ClientHistory", true);
	m_iClientHistoryExpDays = ini.GetInt(L"ClientHistoryExpDays", 150);
	if (m_iClientHistoryExpDays < 1)
		m_iClientHistoryExpDays = 150;
	m_bClientHistoryLog = ini.GetBool(L"ClientHistoryLog", false);
	m_bRemoteSharedFilesUserHash = ini.GetBool(L"RemoteSharedFilesUserHash", true);
	m_bRemoteSharedFilesClientNote = ini.GetBool(L"RemoteSharedFilesClientNote", true);
	m_iRemoteSharedFilesAutoQueryPeriod = ini.GetInt(L"RemoteSharedFilesAutoQueryPeriod", 1);
	if (m_iRemoteSharedFilesAutoQueryPeriod < 1)
		m_iRemoteSharedFilesAutoQueryPeriod = 1;
	m_iRemoteSharedFilesAutoQueryMaxClients = ini.GetInt(L"RemoteSharedFilesAutoQueryMaxClients", 3);
	if (m_iRemoteSharedFilesAutoQueryMaxClients < 1)
		m_iRemoteSharedFilesAutoQueryMaxClients = 3;
	m_iRemoteSharedFilesAutoQueryClientPeriod = ini.GetInt(L"RemoteSharedFilesAutoQueryClientPeriod", 30);
	if (m_iRemoteSharedFilesAutoQueryMaxClients < 1)
		m_iRemoteSharedFilesAutoQueryMaxClients = 30;
	m_bRemoteSharedFilesSetAutoQueryDownload = ini.GetBool(L"RemoteSharedFilesSetAutoQueryDownload", false);
	m_iRemoteSharedFilesSetAutoQueryDownloadThreshold = ini.GetInt(L"RemoteSharedFilesSetAutoQueryDownloadThreshold", 100);
	if (m_iRemoteSharedFilesSetAutoQueryDownloadThreshold < 1)
		m_iRemoteSharedFilesSetAutoQueryDownloadThreshold = 100;
	m_bRemoteSharedFilesSetAutoQueryUpload = ini.GetBool(L"RemoteSharedFilesSetAutoQueryUpload", false);
	m_iRemoteSharedFilesSetAutoQueryUploadThreshold = ini.GetInt(L"RemoteSharedFilesSetAutoQueryUploadThreshold", 100);
	if (m_iRemoteSharedFilesSetAutoQueryUploadThreshold < 1)
		m_iRemoteSharedFilesSetAutoQueryUploadThreshold = 100;
	m_bSaveLoadSources = ini.GetBool(L"SaveLoadSources", true);
	m_iSaveLoadSourcesMaxSources = ini.GetInt(L"SaveLoadSourcesMaxSources", 100);
	if (m_iSaveLoadSourcesMaxSources < 1)
		m_iSaveLoadSourcesMaxSources = 100;
	m_iSaveLoadSourcesExpirationDays = ini.GetInt(L"SaveLoadSourcesExpirationDays", 7);
	if (m_iSaveLoadSourcesExpirationDays < 1)
		m_iSaveLoadSourcesExpirationDays = 14400;
	m_iKnownMetDays = ini.GetInt(L"KnownMetDays", 180);
	if (m_iKnownMetDays < 1)
		m_iKnownMetDays = 180;
	m_bCompletlyPurgeOldKnownFiles = ini.GetBool(L"CompletlyPurgeOldKnownFiles", false);
	if (m_bPartiallyPurgeOldKnownFiles && m_bCompletlyPurgeOldKnownFiles)
		m_bCompletlyPurgeOldKnownFiles = false;
	m_bRemoveAichImmediately = ini.GetBool(L"RemoveAichImmediately", true);
	m_iClientsExpDays = ini.GetInt(L"ClientsExpDays", 180);
	if (m_iClientsExpDays < 1)
		m_iClientsExpDays = 180;

	m_bBackupOnExit = ini.GetBool(L"BackupOnExit", false);
	m_bBackupPeriodic = ini.GetBool(L"BackupPeriodic", true);
	m_iBackupPeriod = ini.GetInt(L"BackupPeriod", 12);
	if (m_iBackupPeriod < 1)
		m_iBackupPeriod = 12;
	m_iBackupMax = ini.GetInt(L"BackupMax", 10);
	if (m_iBackupMax < 1 || m_iBackupMax > 999)
		m_iBackupMax = 10;
	m_bBackupCompressed = ini.GetBool(L"BackupCompressed", true);

	m_strFileTypeSelected = ini.GetString(L"FileTypeSelected", _T(ED2KFTSTR_ANY));
	m_uPreviewCheckState = ini.GetInt(L"PreviewChecked", 0);
	m_bFreezeChecked = ini.GetBool(L"FreezeChecked", false);
	m_uArchivedCheckState = ini.GetInt(L"ArchivedChecked", 0);
	m_uConnectedCheckState = ini.GetInt(L"ConnectedChecked", 0);
	m_uQueryableCheckState = ini.GetInt(L"QueryableChecked", 0);
	m_uNotQueriedCheckState = ini.GetInt(L"QueriedChecked", 0);
	m_uValidIPCheckState = ini.GetInt(L"ValidIPChecked", 0);
	m_uHighIdCheckState = ini.GetInt(L"HighIdChecked", 0);
	m_uBadClientCheckState = ini.GetInt(L"BadClientChecked", 0);

	m_uCompleteCheckState = ini.GetInt(L"CompleteChecked", 0);

	m_iBootstrapSelection = ini.GetInt(L"BootstrapSelection", B_URL);

	///////////////////////////////////////////////////////////////////////////
	// Section: "EmulateCommunityTag"
	//
	for (int m_iTagIndex = 0; true; m_iTagIndex++) {
		CString m_strTemp;
		m_strTemp.Format(_T("EmulateCommunityTag%i"), m_iTagIndex);
		m_strTemp = ini.GetString(m_strTemp, L"", _T("EmulateCommunityTag"));
		if (m_strTemp == "")
			break;
		else
			thePrefs.m_CommunityTagCounterMap[m_strTemp] = 0; // Add predefined values as "0" which is special value and will not be compared to EmulateCommunityTagSavingTreshold
	}
	if (thePrefs.m_CommunityTagCounterMap.IsEmpty()) {
		thePrefs.m_CommunityTagCounterMap[_T("(Lphant.com)")] = 0;
		thePrefs.m_CommunityTagCounterMap[_T("[ita]")] = 0;
		thePrefs.m_CommunityTagCounterMap[_T("[ITA]")] = 0;
		thePrefs.m_CommunityTagCounterMap[_T("[easyMule]")] = 0;
		thePrefs.m_CommunityTagCounterMap[_T("[DreaMule]")] = 0;
		thePrefs.m_CommunityTagCounterMap[_T("(rus)")] = 0;
		thePrefs.m_CommunityTagCounterMap[_T("[ePlus]")] = 0;
		thePrefs.m_CommunityTagCounterMap[_T("(heb)")] = 0;
		thePrefs.m_CommunityTagCounterMap[_T("[CHN]")] = 0;
		thePrefs.m_CommunityTagCounterMap[_T("[TLF]")] = 0;
		thePrefs.m_CommunityTagCounterMap[_T("[eDtoon]")] = 0;
		thePrefs.m_CommunityTagCounterMap[_T("(Jtdmf)")] = 0;
		thePrefs.m_CommunityTagCounterMap[_T("[VeryCD]")] = 0;
	}

	///////////////////////////////////////////////////////////////////////////
	// Section: "eMule"
	// 
	if (iMaxLogBuff < 64 * 1024)  iMaxLogBuff = 64 * 1024;
	if (iMaxLogBuff > 512 * 1024) iMaxLogBuff = 512 * 1024;
	m_iMaxChatHistory = ini.GetInt(L"MaxChatHistoryLines", 100, _T("eMule"));
	if (m_iMaxChatHistory < 1)
		m_iMaxChatHistory = 100;
	if (m_iMaxChatHistory > 2048)  m_iMaxChatHistory = 2048;
	maxmsgsessions = ini.GetInt(L"MaxMessageSessions", 50);
	if (maxmsgsessions > 6000)  maxmsgsessions = 6000;
	if (maxmsgsessions < 0)  maxmsgsessions = 0;
	bMiniMuleAutoClose = ini.GetBool(L"MiniMuleAutoClose", 0);
	iMiniMuleTransparency = min(ini.GetInt(L"MiniMuleTransparency", 0), 100); // range 0..100
	bCreateCrashDump = ini.GetBool(L"CreateCrashDump", 0);
	bIgnoreInstances = ini.GetBool(L"IgnoreInstance", false);
	sNotifierMailEncryptCertName = ini.GetString(L"NotifierMailEncryptCertName", L"");
	sInternetSecurityZone = ini.GetString(L"InternetSecurityZone", L"Untrusted");
	m_iDebugSearchResultDetailLevel = ini.GetInt(L"DebugSearchResultDetailLevel", 0); // NOTE: this variable is also initialized to 0 above!

    m_bDisableAICHCreation = ini.GetBool(_T("DisableAICHCreation"), false);

	///////////////////////////////////////////////////////////////////////////
	// Section: "ProtectionPanel"
	// 
	m_uTemp = ini.GetInt(L"PunishmentCancelationScanPeriod", 5, _T("ProtectionPanel"));
	m_iPunishmentCancelationScanPeriod = MIN2MS(((m_uTemp >= 2 && m_uTemp <= 20) ? m_uTemp : 5)); //Minutes to miliseconds
	m_uTemp = ini.GetInt(L"ClientBanTime", 48);
	m_tClientBanTime = 3600 * ((m_uTemp >= 1 && m_uTemp <= 720) ? m_uTemp : 48); //720->24 hours * 30 days  &  3600->Hours to seconds
	m_uTemp = ini.GetInt(L"ClientScoreReducingTime", 48);
	m_tClientScoreReducingTime = 3600 * ((m_uTemp >= 1 && m_uTemp <= 720) ? m_uTemp : 48); //720->24 hours * 30 days  &  3600->Hours to seconds
	m_bInformBadClients = ini.GetBool(L"InformBadClients", false);
	m_strInformBadClientsText = ini.GetString(L"InformBadClientsText", NULL);
	m_bDontPunishFriends = ini.GetBool(L"DontPunishFriends", true);
	m_bDontAllowFileHotSwapping = ini.GetBool(L"DontAllowFileHotSwapping", true);
	m_bAntiUploadProtection = ini.GetBool(L"AntiUploadProtection", true);
	m_iAntiUploadProtectionLimit = (uint16)ini.GetInt(L"AntiUploadProtectionLimit", 1800);
	m_iAntiUploadProtectionLimit = (m_iAntiUploadProtectionLimit >= 1000 && m_iAntiUploadProtectionLimit <= 2800) ? m_iAntiUploadProtectionLimit : 1800;
	m_bUploaderPunishmentPrevention = ini.GetBool(L"UploaderPunishmentPreventionBool", true);
	m_iUploaderPunishmentPreventionLimit = (uint16)ini.GetInt(L"UploaderPunishmentPreventionLimit", 1000);
	m_iUploaderPunishmentPreventionLimit = m_iUploaderPunishmentPreventionLimit >= 1 ? m_iUploaderPunishmentPreventionLimit : 1;
	m_iUploaderPunishmentPreventionCase = (uint8)ini.GetInt(L"UploaderPunishmentPreventionCase", 0);
	m_bDetectModNames = ini.GetBool(L"DetectModNames", true);
	m_bDetectUserNames = ini.GetBool(L"DetectUserNames", true);
	m_bDetectUserHashes = ini.GetBool(L"DetectUserHashes", true);
	m_uHardLeecherPunishment = ini.GetInt(L"HardLeecherPunishment", 2);
	m_uSoftLeecherPunishment = ini.GetInt(L"SoftLeecherPunishment", 7);
	m_bBanBadKadNodes = ini.GetBool(L"BanBadKadNodes", true);
	m_bBanWrongPackage = ini.GetBool(L"BanWrongPackage", true);
	m_bDetectAntiP2PBots = ini.GetBool(L"DetectAntiP2PBots", true);
	m_uAntiP2PBotsPunishment = ini.GetInt(L"AntiP2PBotsPunishment", 1);
	m_bDetectWrongTag = ini.GetBool(L"DetectWrongTag", true);
	m_uWrongTagPunishment = ini.GetInt(L"WrongTagPunishment", 9);
	m_bDetectUnknownTag = ini.GetBool(L"DetectUnknownTag", true);
	m_uUnknownTagPunishment = ini.GetInt(L"UnknownTagPunishment", 9);
	m_bDetectHashThief = ini.GetBool(L"DetectHashThief", true);
	m_uHashThiefPunishment = ini.GetInt(L"HashThiefPunishment", 1);
	m_bDetectModThief = ini.GetBool(L"DetectModThief", true);
	m_uModThiefPunishment = ini.GetInt(L"ModThiefPunishment", 6);
	m_bDetectUserNameThief = ini.GetBool(L"DetectUserNameThief", true);
	m_uUserNameThiefPunishment = ini.GetInt(L"UserNameThiefPunishment", 6);
	m_bDetectModChanger = ini.GetBool(L"DetectModChanger", true);
	m_uTemp = ini.GetInt(L"ModChangerInterval", 60);
	m_iModChangerInterval = MIN2MS(((m_uTemp >= 30 && m_uTemp <= 1440) ? m_uTemp : 60));
	m_iModChangerThreshold = ini.GetInt(L"ModChangerThreshold", 4);
	m_uModChangerPunishment = ini.GetInt(L"ModChangerPunishment", 9);
	m_bDetectUserNameChanger = ini.GetBool(L"DetectUserNameChanger", true);
	m_uTemp = ini.GetInt(L"UserNameChangerInterval", 180);
	m_iUserNameChangerInterval = MIN2MS(((m_uTemp >= 30 && m_uTemp <= 1440) ? m_uTemp : 180));
	m_iUserNameChangerThreshold = ini.GetInt(L"UserNameChangerThreshold", 3);
	m_uUserNameChangerPunishment = ini.GetInt(L"UserNameChangerPunishment", 9);
	m_bDetectTCPErrorFlooder = ini.GetBool(L"DetectTCPErrorFlooder", true);
	m_uTemp = ini.GetInt(L"TCPErrorFlooderInterval", 60);
	m_iTCPErrorFlooderInterval = MIN2MS(((m_uTemp >= 30 && m_uTemp <= 1440) ? m_uTemp : 60));
	m_iTCPErrorFlooderThreshold = ini.GetInt(L"TCPErrorFlooderThreshold", 7);
	m_uTCPErrorFlooderPunishment = ini.GetInt(L"TCPErrorFlooderPunishment", 0);
	m_bDetectEmptyUserNameEmule = ini.GetBool(L"DetectEmptyUserNameEmule", true);
	m_uEmptyUserNameEmulePunishment = ini.GetInt(L"EmptyUserNameEmulePunishment", 9);
	m_bDetectCommunity = ini.GetBool(L"DetectCommunity", true);
	m_uCommunityPunishment = ini.GetInt(L"CommunityPunishment", 5);
	m_bDetectFakeEmule = ini.GetBool(L"DetectFakeEmule", true);
	m_uFakeEmulePunishment = ini.GetInt(L"FakeEmulePunishment", 3);
	m_bDetectHexModName = ini.GetBool(L"DetectHexModName", true);
	m_uHexModNamePunishment = ini.GetInt(L"HexModNamePunishment", 7);
	m_bDetectGhostMod = ini.GetBool(L"DetectGhostMod", true);
	m_uGhostModPunishment = ini.GetInt(L"GhostModPunishment", 9);
	m_bDetectSpam = ini.GetBool(L"DetectSpam", true);
	m_uSpamPunishment = ini.GetInt(L"SpamPunishment", 3);
	m_bDetectEmcrypt = ini.GetBool(L"DetectEmcrypt", true);
	m_uEmcryptPunishment = ini.GetInt(L"EmcryptPunishment", 2);
	m_bDetectXSExploiter = ini.GetBool(L"DetectXSExploiter", true);
	m_uXSExploiterPunishment = ini.GetInt(L"XSExploiterPunishment", 1);
	m_bDetectFileFaker = ini.GetBool(L"DetectFileFaker", true);
	m_uFileFakerPunishment = ini.GetInt(L"FileFakerPunishment", 1);
	m_bDetectUploadFaker = ini.GetBool(L"DetectUploadFaker", true);
	m_uUploadFakerPunishment = ini.GetInt(L"UploadFakerPunishment", 1);
	m_bDetectAgressive = ini.GetBool(L"DetectAgressive", true);
	m_iAgressiveTime = (uint16)ini.GetInt(L"AgressiveTime", 10);
	m_iAgressiveCounter = (uint16)ini.GetInt(L"AgressiveCounter", 5);
	m_bAgressiveLog = ini.GetBool(L"AgressiveLog", true);
	m_uAgressivePunishment = ini.GetInt(L"AgressivePunishment", 5);
	m_bPunishNonSuiMlDonkey = ini.GetBool(L"PunishNonSuiMlDonkey", true);
	m_bPunishNonSuiEdonkey = ini.GetBool(L"PunishNonSuiEdonkey", true);
	m_bPunishNonSuiEdonkeyHybrid = ini.GetBool(L"PunishNonSuiEdonkeyHybrid", true);
	m_bPunishNonSuiShareaza = ini.GetBool(L"PunishNonSuiShareaza", true);
	m_bPunishNonSuiLphant = ini.GetBool(L"PunishNonSuiLphant", true);
	m_bPunishNonSuiAmule = ini.GetBool(L"PunishNonSuiAmule", true);
	m_bPunishNonSuiEmule = ini.GetBool(L"PunishNonSuiEmule", true);
	m_uNonSuiPunishment = ini.GetInt(L"NonSuiPunishment", 0);
	m_bDetectCorruptedDataSender = ini.GetBool(L"DetectCorruptedDataSender", true);
	m_bDetectHashChanger = ini.GetBool(L"DetectHashChanger", true);
	m_bDetectFileScanner = ini.GetBool(L"DetectFileScanner", true);
	m_bDetectRankFlooder = ini.GetBool(L"DetectRankFlooder", true);
	m_bDetectKadRequestFlooder = ini.GetBool(L"DetectKadRequestFlooder", true);
	m_iKadRequestFloodBanTreshold = ini.GetInt(L"KadRequestFloodBanTreshold", 4);

	///////////////////////////////////////////////////////////////////////////
	// Section: "BlacklistPanel"
	// 
	m_bBlacklistAutomatic = ini.GetBool(_T("BlacklistAutomatic"), true, _T("BlacklistPanel"));
	m_bBlacklistManual = ini.GetBool(_T("BlacklistManual"), true);
	m_bBlacklistAutoRemoveFromManual = ini.GetBool(_T("BlacklistAutoRemoveFromManual"), true);
	m_bBlacklistLog = ini.GetBool(_T("BlacklistLog"), false);

	LoadCats();
	SetLanguage();
}

WORD CPreferences::GetWindowsVersion()
{
	static bool bWinVerAlreadyDetected = false;
	if (!bWinVerAlreadyDetected) {
		bWinVerAlreadyDetected = true;
		m_wWinVer = DetectWinVersion();
	}
	return m_wWinVer;
}

UINT CPreferences::GetDefaultMaxConperFive()
{
	switch (GetWindowsVersion()) {
	case _WINVER_98_:
		return 5;
	case _WINVER_95_:
	case _WINVER_ME_:
		return MAXCON5WIN9X;
	default:
		return MAXCONPER5SEC;
	}
}

//////////////////////////////////////////////////////////
// category implementations
//////////////////////////////////////////////////////////

void CPreferences::SaveCats()
{
	CString strCatIniFilePath;
	strCatIniFilePath.Format(_T("%s") _T("Category.ini"), (LPCTSTR)GetMuleDirectory(EMULE_CONFIGDIR));
	(void)_tremove(strCatIniFilePath);
	CIni ini(strCatIniFilePath);
	ini.WriteInt(_T("Count"), (int)catArr.GetCount() - 1, _T("General"));
	for (INT_PTR i = 0; i < catArr.GetCount(); ++i) {
		CString strSection;
		strSection.Format(_T("Cat#%i"), (int)i);
		ini.SetSection(strSection);

		const Category_Struct* cmap = catArr[i];

		ini.WriteStringUTF8(_T("Title"), cmap->strTitle);
		ini.WriteStringUTF8(_T("Incoming"), cmap->strIncomingPath);
		ini.WriteStringUTF8(_T("Comment"), cmap->strComment);
		ini.WriteStringUTF8(_T("RegularExpression"), cmap->regexp);
		ini.WriteInt(_T("Color"), (int)cmap->color);
		ini.WriteInt(_T("a4afPriority"), cmap->prio); // ZZ:DownloadManager
		ini.WriteStringUTF8(_T("AutoCat"), cmap->autocat);
		ini.WriteInt(_T("Filter"), cmap->filter);
		ini.WriteBool(_T("FilterNegator"), cmap->filterNeg);
		ini.WriteBool(_T("AutoCatAsRegularExpression"), cmap->ac_regexpeval);
		ini.WriteBool(_T("downloadInAlphabeticalOrder"), cmap->downloadInAlphabeticalOrder != FALSE);
		ini.WriteBool(_T("Care4All"), cmap->care4all);
	}
}

void CPreferences::LoadCats()
{
	CString strCatIniFilePath;
	strCatIniFilePath.Format(_T("%s") _T("Category.ini"), (LPCTSTR)GetMuleDirectory(EMULE_CONFIGDIR));
	CIni ini(strCatIniFilePath);
	int iNumCategories = ini.GetInt(_T("Count"), 0, _T("General"));
	for (INT_PTR i = 0; i <= iNumCategories; ++i) {
		CString strSection;
		strSection.Format(_T("Cat#%i"), (int)i);
		ini.SetSection(strSection);

		Category_Struct* newcat = new Category_Struct;
		newcat->filter = 0;
		newcat->strTitle = ini.GetStringUTF8(_T("Title"));
		if (i != 0) { // All category
			newcat->strIncomingPath = ini.GetStringUTF8(_T("Incoming"));
			MakeFoldername(newcat->strIncomingPath);
			if (!IsShareableDirectory(newcat->strIncomingPath)
				|| (!::PathFileExists(newcat->strIncomingPath) && !::CreateDirectory(newcat->strIncomingPath, 0)))
			{
				newcat->strIncomingPath = GetMuleDirectory(EMULE_INCOMINGDIR);
			}
		}
		else
			newcat->strIncomingPath.Empty();
		newcat->strComment = ini.GetStringUTF8(_T("Comment"));
		newcat->prio = ini.GetInt(_T("a4afPriority"), PR_NORMAL); // ZZ:DownloadManager
		newcat->filter = ini.GetInt(_T("Filter"), 0);
		newcat->filterNeg = ini.GetBool(_T("FilterNegator"), FALSE);
		newcat->ac_regexpeval = ini.GetBool(_T("AutoCatAsRegularExpression"), FALSE);
		newcat->care4all = ini.GetBool(_T("Care4All"), FALSE);
		newcat->regexp = ini.GetStringUTF8(_T("RegularExpression"));
		newcat->autocat = ini.GetStringUTF8(_T("Autocat"));
		newcat->downloadInAlphabeticalOrder = ini.GetBool(_T("downloadInAlphabeticalOrder"), FALSE); // ZZ:DownloadManager
		newcat->color = (COLORREF)ini.GetInt(_T("Color"), -1);
		AddCat(newcat);
	}
}

void CPreferences::RemoveCat(INT_PTR index)
{
	if (index >= 0 && index < catArr.GetCount()) {
		const Category_Struct* delcat = catArr[index];
		catArr.RemoveAt(index);
		delete delcat;
	}
}

bool CPreferences::SetCatFilter(INT_PTR index, int filter)
{
	if (index >= 0 && index < catArr.GetCount()) {
		catArr[index]->filter = filter;
		return true;
	}
	return false;
}

int CPreferences::GetCatFilter(INT_PTR index)
{
	if (index >= 0 && index < catArr.GetCount())
		return catArr[index]->filter;
	return 0;
}

bool CPreferences::GetCatFilterNeg(INT_PTR index)
{
	if (index >= 0 && index < catArr.GetCount())
		return catArr[index]->filterNeg;
	return false;
}

void CPreferences::SetCatFilterNeg(INT_PTR index, bool val)
{
	if (index >= 0 && index < catArr.GetCount())
		catArr[index]->filterNeg = val;
}

bool CPreferences::MoveCat(INT_PTR from, INT_PTR to)
{
	if (from >= catArr.GetCount() || to >= catArr.GetCount() + 1 || from == to)
		return false;

	Category_Struct* tomove = catArr[from];
	if (from < to) {
		catArr.RemoveAt(from);
		catArr.InsertAt(to - 1, tomove);
	}
	else {
		catArr.InsertAt(to, tomove);
		catArr.RemoveAt(from + 1);
	}
	SaveCats();
	return true;
}

DWORD CPreferences::GetCatColor(INT_PTR index, int nDefault)
{
	if (index >= 0 && index < catArr.GetCount()) {
		const COLORREF c = catArr[index]->color;
		if (c && c != CLR_NONE)
			return c;
	}

	return GetCustomSysColor(nDefault);
}


///////////////////////////////////////////////////////

bool CPreferences::IsInstallationDirectory(const CString& rstrDir)
{
	// skip sharing of several special eMule folders
	return EqualPaths(rstrDir, GetMuleDirectory(EMULE_EXECUTABLEDIR))
		|| EqualPaths(rstrDir, GetMuleDirectory(EMULE_CONFIGDIR))
		|| EqualPaths(rstrDir, GetMuleDirectory(EMULE_WEBSERVERDIR))
		|| EqualPaths(rstrDir, GetMuleDirectory(EMULE_LOGDIR));
}

bool CPreferences::IsShareableDirectory(const CString& rstrDir)
{
	// skip sharing of several special eMule folders
	if (IsInstallationDirectory(rstrDir))
		return false;
	if (EqualPaths(rstrDir, GetMuleDirectory(EMULE_INCOMINGDIR)))
		return false;
	for (INT_PTR i = GetTempDirCount(); --i >= 0;)
		if (EqualPaths(rstrDir, GetTempDir(i)))		// ".\eMule\temp"
			return false;

	return true;
}

void CPreferences::UpdateLastVC()
{
	struct tm tmTemp;
	versioncheckLastAutomatic = safe_mktime(CTime::GetCurrentTime().GetLocalTm(&tmTemp));
}

void CPreferences::SetWSPass(const CString& strNewPass)
{
	m_strWebPassword = MD5Sum(strNewPass).GetHashString();
}

void CPreferences::SetWSLowPass(const CString& strNewPass)
{
	m_strWebLowPassword = MD5Sum(strNewPass).GetHashString();
}

void CPreferences::SetMaxUpload(uint32 val)
{
	m_maxupload = val ? val : UNLIMITED;
}

void CPreferences::SetMaxDownload(uint32 val)
{
	m_maxdownload = val ? val : UNLIMITED;
}

void CPreferences::SetNetworkKademlia(bool val)
{
	networkkademlia = val;
}

CString CPreferences::GetHomepageBaseURLForLevel(int nLevel)
{
	CString tmp;
	if (nLevel == 0)
		tmp = _T("https://emule-project.net");
	else if (nLevel == 1)
		tmp = _T("https://www.emule-project.org");
	else if (nLevel == 2)
		tmp = _T("https://www.emule-project.com");
	else if (nLevel < 100)
		tmp.Format(_T("https://www%i.emule-project.net"), nLevel - 2);
	else if (nLevel < 150)
		tmp.Format(_T("https://www%i.emule-project.org"), nLevel);
	else if (nLevel < 200)
		tmp.Format(_T("https://www%i.emule-project.com"), nLevel);
	else if (nLevel == 200)
		tmp = _T("https://emule.sf.net");
	else if (nLevel == 201)
		tmp = _T("https://www.emuleproject.net");
	else if (nLevel == 202)
		tmp = _T("https://sourceforge.net/projects/emule/");
	else
		tmp = _T("https://www.emule-project.net");
	return tmp;
}

CString CPreferences::GetVersionCheckBaseURL()
{
	return MOD_REPO_BASE_URL;
}

CString CPreferences::GetVersionCheckURL()
{
	return thePrefs.GetVersionCheckBaseURL() + _T("/releases");
}

CString CPreferences::GetVersionCheckRawURL()
{
	CString strVersionCheckRawUrl(MOD_PAGES_BASE_URL);
	if (!strVersionCheckRawUrl.IsEmpty() && strVersionCheckRawUrl.Right(1) != _T("/"))
		strVersionCheckRawUrl += _T("/");
	strVersionCheckRawUrl += _T("current_version.txt");
	return strVersionCheckRawUrl;
}

bool CPreferences::IsDefaultNick(const CString& strCheck)
{
	// not fast, but this function is not called often
	for (int i = 0; i < 255; ++i)
		if (GetHomepageBaseURLForLevel(i) == strCheck)
			return true;

	return strCheck == _T("http://emule-project.net") || strCheck == _T("https://emule-project.net");
}

void CPreferences::SetUserNick(LPCTSTR pszNick)
{
	strNick = pszNick;
}

UINT CPreferences::GetWebMirrorAlertLevel()
{
	// Known upcoming DDoS Attacks
	if (m_nWebMirrorAlertLevel == 0) {
		// no threats known at this time
	}
	return m_nWebMirrorAlertLevel;
}

bool CPreferences::IsRunAsUserEnabled()
{
	switch (GetWindowsVersion()) {
	case _WINVER_2K_:
	case _WINVER_XP_:
	case _WINVER_2003_:
		return m_bRunAsUser && m_nCurrentUserDirMode == 2;
	}
	return false;
}

bool CPreferences::GetUseReBarToolbar()
{
	return GetReBarToolbar() && theApp.m_ullComCtrlVer >= MAKEDLLVERULL(5, 8, 0, 0);
}

uint32 CPreferences::GetMaxGraphUploadRate(bool bEstimateIfUnlimited)
{
	if (maxGraphUploadRate != UNLIMITED || !bEstimateIfUnlimited)
		return maxGraphUploadRate;
	return (maxGraphUploadRateEstimated != 0) ? maxGraphUploadRateEstimated + 4 : 16u;
}

void CPreferences::EstimateMaxUploadCap(uint32 nCurrentUpload)
{
	if (maxGraphUploadRateEstimated + 1 < nCurrentUpload) {
		maxGraphUploadRateEstimated = nCurrentUpload;
		if (maxGraphUploadRate == UNLIMITED && theApp.emuledlg->statisticswnd)
			theApp.emuledlg->statisticswnd->SetARange(false, thePrefs.GetMaxGraphUploadRate(true));
	}
}

void CPreferences::SetMaxGraphUploadRate(uint32 in)
{
	maxGraphUploadRate = (in ? in : UNLIMITED);
}

bool CPreferences::IsDynUpEnabled()
{
	return m_bDynUpEnabled || maxGraphUploadRate == UNLIMITED;
}

bool CPreferences::CanFSHandleLargeFiles(int nForCat)
{
	for (INT_PTR i = tempdir.GetCount(); --i >= 0;)
		if (!IsFileOnFATVolume(tempdir[i]))
			return !IsFileOnFATVolume((nForCat > 0) ? GetCatPath(nForCat) : GetMuleDirectory(EMULE_INCOMINGDIR));

	return false;
}

void CPreferences::SetEServerDiscoveredExternalUdpPort(uint16 port, uint8 source)
{
	if (port == 0) {
		ClearEServerDiscoveredExternalUdpPort();
		return;
	}

	m_nEServerDiscoveredExternalUdpPort = port;
	m_dwEServerDiscoveredExternalUdpPortTime = ::GetTickCount();
	m_uEServerDiscoveredExternalUdpPortSource = source;
}

void CPreferences::ClearEServerDiscoveredExternalUdpPort()
{
	m_nEServerDiscoveredExternalUdpPort = 0;
	m_dwEServerDiscoveredExternalUdpPortTime = 0;
	m_uEServerDiscoveredExternalUdpPortSource = 0;
}

bool CPreferences::HasValidExternalUdpPort()
{
	if (Kademlia::CKademlia::GetPrefs() != NULL
		&& Kademlia::CKademlia::GetPrefs()->GetUseExternKadPort()
		&& Kademlia::CKademlia::GetPrefs()->GetExternalKadPort() != 0)
		return true;

	if (m_nEServerDiscoveredExternalUdpPort == 0 || m_dwEServerDiscoveredExternalUdpPortTime == 0)
		return false;

	if ((DWORD)(::GetTickCount() - m_dwEServerDiscoveredExternalUdpPortTime) > ESERVER_EXT_UDP_PORT_TTL) {
		ClearEServerDiscoveredExternalUdpPort();
		return false;
	}

	return true;
}

uint16 CPreferences::GetBestExternalUdpPort()
{
	if (Kademlia::CKademlia::GetPrefs() != NULL
		&& Kademlia::CKademlia::GetPrefs()->GetUseExternKadPort()
		&& Kademlia::CKademlia::GetPrefs()->GetExternalKadPort() != 0)
		return Kademlia::CKademlia::GetPrefs()->GetExternalKadPort();

	if (m_nEServerDiscoveredExternalUdpPort == 0 || m_dwEServerDiscoveredExternalUdpPortTime == 0)
		return 0;

	if ((DWORD)(::GetTickCount() - m_dwEServerDiscoveredExternalUdpPortTime) > ESERVER_EXT_UDP_PORT_TTL) {
		ClearEServerDiscoveredExternalUdpPort();
		return 0;
	}

	return m_nEServerDiscoveredExternalUdpPort;
}

uint16 CPreferences::GetRandomTCPPort()
{
	// Get table of currently used TCP ports.
	PMIB_TCPTABLE pTCPTab = NULL;
	ULONG dwSize = 0;
	if (GetTcpTable(pTCPTab, &dwSize, FALSE) == ERROR_INSUFFICIENT_BUFFER) {
		// Allocate more memory in case the number of TCP entries increased
		// between the function calls.
		dwSize += sizeof(MIB_TCPROW) * 50;
		pTCPTab = (PMIB_TCPTABLE)malloc(dwSize);
		if (pTCPTab && GetTcpTable(pTCPTab, &dwSize, TRUE) != ERROR_SUCCESS) {
			free(pTCPTab);
			pTCPTab = NULL;
		}
	}

	static const UINT uValidPortRange = 61000;
	int iMaxTests = uValidPortRange; // just in case, avoid endless loop
	uint16 nPort;
	bool bPortIsFree;
	do {
		// Get random port
		nPort = 4096 + (GetRandomUInt16() % uValidPortRange);

		// The port is assumed to be available by default. If we got a table of currently
		// used TCP ports, verify that the port is not in this table.
		bPortIsFree = true;
		if (pTCPTab) {
			uint16 nPortBE = htons(nPort);
			for (DWORD e = pTCPTab->dwNumEntries; e-- > 0;) {
				// If there is a TCP entry in the table (regardless of its state), the port
				// is treated as unavailable.
				if (pTCPTab->table[e].dwLocalPort == nPortBE) {
					bPortIsFree = false;
					break;
				}
			}
		}
	} while (!bPortIsFree && --iMaxTests > 0);
	free(pTCPTab);
	return nPort;
}

uint16 CPreferences::GetRandomUDPPort()
{
	// Get table of currently used UDP ports.
	PMIB_UDPTABLE pUDPTab = NULL;
	ULONG dwSize = 0;
	if (GetUdpTable(NULL, &dwSize, FALSE) == ERROR_INSUFFICIENT_BUFFER) {
		// Allocate more memory in case the number of UDP entries increased
		// between the function calls.
		dwSize += sizeof(MIB_UDPROW) * 50;
		pUDPTab = (PMIB_UDPTABLE)malloc(dwSize);
		if (pUDPTab && GetUdpTable(pUDPTab, &dwSize, TRUE) != ERROR_SUCCESS) {
			free(pUDPTab);
			pUDPTab = NULL;
		}
	}

	static const UINT uValidPortRange = 61000;
	int iMaxTests = uValidPortRange; // just in case, avoid endless loop
	uint16 nPort;
	bool bPortIsFree;
	do {
		// Get random port
		nPort = 4096 + (GetRandomUInt16() % uValidPortRange);

		// The port is assumed to be available by default. If we got a table of currently
		// used UDP ports, verify that the port is not in this table.
		bPortIsFree = true;
		if (pUDPTab) {
			uint16 nPortBE = htons(nPort);
			for (DWORD e = pUDPTab->dwNumEntries; e-- > 0;) {
				if (pUDPTab->table[e].dwLocalPort == nPortBE) {
					bPortIsFree = false;
					break;
				}
			}
		}
	} while (!bPortIsFree && --iMaxTests > 0);
	free(pUDPTab);
	return nPort;
}

// eMule AI always stores config and data next to the executable directory.
// Windows profile, ProgramData and registry-based user-dir selection are not used.
CString CPreferences::GetDefaultDirectory(EDefaultDirectory eDirectory, bool bCreate)
{
	if (m_astrDefaultDirs[0].IsEmpty()) { // already have all directories fetched and stored?
		TCHAR tchBuffer[MAX_PATH];
		::GetModuleFileName(NULL, tchBuffer, _countof(tchBuffer));
		LPTSTR pszFileName = _tcsrchr(tchBuffer, _T('\\')) + 1;
		*pszFileName = _T('\0');
		m_astrDefaultDirs[EMULE_EXECUTABLEDIR] = tchBuffer;

		CString strSelectedDataBaseDirectory(m_astrDefaultDirs[EMULE_EXECUTABLEDIR]);
		CString strSelectedConfigBaseDirectory(m_astrDefaultDirs[EMULE_EXECUTABLEDIR]);
		CString strSelectedExpansionBaseDirectory(m_astrDefaultDirs[EMULE_EXECUTABLEDIR]);
		m_nCurrentUserDirMode = 2;

		// All the directories (categories also) should have a trailing backslash
		m_astrDefaultDirs[EMULE_CONFIGDIR] = strSelectedConfigBaseDirectory + CONFIGFOLDER;
		m_astrDefaultDirs[EMULE_TEMPDIR] = strSelectedDataBaseDirectory + _T("Temp\\");
		m_astrDefaultDirs[EMULE_INCOMINGDIR] = strSelectedDataBaseDirectory + _T("Incoming\\");
		m_astrDefaultDirs[EMULE_LOGDIR] = strSelectedConfigBaseDirectory + _T("logs\\");
		m_astrDefaultDirs[EMULE_WEBSERVERDIR] = m_astrDefaultDirs[EMULE_EXECUTABLEDIR] + _T("webserver\\");
		m_astrDefaultDirs[EMULE_SKINDIR] = strSelectedExpansionBaseDirectory + _T("skins\\");
		m_astrDefaultDirs[EMULE_DATABASEDIR] = strSelectedDataBaseDirectory;
		m_astrDefaultDirs[EMULE_CONFIGBASEDIR] = strSelectedConfigBaseDirectory;
		//               [EMULE_EXECUTABLEDIR] - has been set already
		m_astrDefaultDirs[EMULE_TOOLBARDIR] = m_astrDefaultDirs[EMULE_SKINDIR];
		m_astrDefaultDirs[EMULE_EXPANSIONDIR] = strSelectedExpansionBaseDirectory;

	}
	if (bCreate && !m_abDefaultDirsCreated[eDirectory]) {
		switch (eDirectory) { // create the underlying directory first - be sure to adjust this if changing default directories
		case EMULE_CONFIGDIR:
		case EMULE_LOGDIR:
			::CreateDirectory(m_astrDefaultDirs[EMULE_CONFIGBASEDIR], NULL);
			break;
		case EMULE_TEMPDIR:
		case EMULE_INCOMINGDIR:
			::CreateDirectory(m_astrDefaultDirs[EMULE_DATABASEDIR], NULL);
			break;
		case EMULE_SKINDIR:
		case EMULE_TOOLBARDIR:
			::CreateDirectory(m_astrDefaultDirs[EMULE_EXPANSIONDIR], NULL);
		}
		::CreateDirectory(m_astrDefaultDirs[eDirectory], NULL);
		m_abDefaultDirsCreated[eDirectory] = true;
	}
	return m_astrDefaultDirs[eDirectory];
}

CString CPreferences::GetMuleDirectory(EDefaultDirectory eDirectory, bool bCreate)
{
	switch (eDirectory) {
	case EMULE_INCOMINGDIR:
		return m_strIncomingDir;
	case EMULE_TEMPDIR:
		ASSERT(0); // This function can return only the first temp. directory. Use GetTempDir() instead!
		return GetTempDir(0);
	case EMULE_SKINDIR:
		return m_strSkinProfileDir;
	case EMULE_TOOLBARDIR:
		return m_sToolbarBitmapFolder;
	}
	return GetDefaultDirectory(eDirectory, bCreate);
}

void CPreferences::SetMuleDirectory(EDefaultDirectory eDirectory, const CString& strNewDir)
{
	switch (eDirectory) {
	case EMULE_INCOMINGDIR:
		m_strIncomingDir = strNewDir;
		break;
	case EMULE_SKINDIR:
		m_strSkinProfileDir = strNewDir;
		slosh(m_strSkinProfileDir);
		break;
	case EMULE_TOOLBARDIR:
		m_sToolbarBitmapFolder = strNewDir;
		slosh(m_sToolbarBitmapFolder);
		break;
	default:
		ASSERT(0);
	}
}

bool CPreferences::GetSparsePartFiles()
{
	// Vista's Sparse File implementation seems to be buggy as far as i can see
	// If a sparse file exceeds a given limit of write I/O operations in a certain order
	// (or i.e. end to beginning) in its lifetime, it will at some point throw out
	// a FILE_SYSTEM_LIMITATION error and deny any writing to this file.
	// It was suggested that Vista might limit the data runs, which would lead to such behaviour,
	// but wouldn't make much sense for a sparse file implementation nevertheless.
	// Due to the fact that eMule writes a lot of small blocks into sparse files and flushes them
	// every 6 seconds, this problem pops up sooner or later for all big files.
	// I don't see any way to walk around this for now
	// Update: This problem seems to be fixed on Win7, possibly on earlier Vista ServicePacks too
	//		   In any case, we allow sparse files for versions earlier and later than Vista
	return m_bSparsePartFiles && (GetWindowsVersion() != _WINVER_VISTA_);
}

bool CPreferences::IsRunningAeroGlassTheme()
{
	// This is important for all functions which need to draw in the NC-Area (glass style)
	// Aero by default does not allow this, any drawing will not be visible. This can be turned off,
	// but Vista will not deliver the Glass style then as background when calling the default draw
	// function. In other words, draw all or nothing yourself - eMule chooses currently nothing
	static bool bAeroAlreadyDetected = false;
	if (!bAeroAlreadyDetected) {
		bAeroAlreadyDetected = true;
		m_bIsRunningAeroGlass = FALSE;
		if (GetWindowsVersion() >= _WINVER_VISTA_) {
			HMODULE hDWMAPI = LoadLibrary(_T("dwmapi.dll"));
			if (hDWMAPI) {
				HRESULT(WINAPI * pfnDwmIsCompositionEnabled)(BOOL*);
				(FARPROC&)pfnDwmIsCompositionEnabled = GetProcAddress(hDWMAPI, "DwmIsCompositionEnabled");
				if (pfnDwmIsCompositionEnabled != NULL)
					pfnDwmIsCompositionEnabled(&m_bIsRunningAeroGlass);
				FreeLibrary(hDWMAPI);
			}
		}
	}
	return (m_bIsRunningAeroGlass != FALSE);
}

void CPreferences::SetClientHistory(bool bClientHistory)
{
	if (m_bClientHistory && !bClientHistory) {
		m_bClientHistory = bClientHistory;
		theApp.emuledlg->transferwnd->GetClientList()->ReloadList(false, LSF_SELECTION);
	} else if (!m_bClientHistory && bClientHistory) {
		m_bClientHistory = bClientHistory;
		theApp.clientlist->LoadList();
		theApp.clientlist->m_tLastSaved = time(NULL); // Set initial value here.
		theApp.emuledlg->transferwnd->GetClientList()->ReloadList(false, LSF_SELECTION);
	} else
		m_bClientHistory = bClientHistory;
}

bool CPreferences::IsNatTraversalServiceEnabled() {
	// Service override: Enabled when user enabled NAT-T or we are HighID (not firewalled).
	return m_bEnableNatTraversal || !theApp.IsFirewalled();
}
