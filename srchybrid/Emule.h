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
#include "eMuleAI/ConChecker.h"
#include "eMuleAI/DownloadChecker.h"
#include "eMuleAI/Address.h"
#include "eMuleAI/UtpSocket.h"

#define	DEFAULT_NICK		MOD_REPO_BASE_URL
#define	DEFAULT_TCP_PORT_OLD	4662
#define	DEFAULT_UDP_PORT_OLD	(DEFAULT_TCP_PORT_OLD+10)

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
class CUpDownClient;
class CFirewallOpener;
class CUPnPImplWrapper;
class CUploadDiskIOThread;
class CPartFileWriteThread;
class CGeoLite2;
class CShield;
class CConChecker;
class CDownloadChecker;

struct SLogItem;

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
	CFirewallOpener		*m_pFirewallOpener;
	CUPnPImplWrapper	*m_pUPnPFinder;
	CUploadDiskIOThread	*m_pUploadDiskIOThread;
	CPartFileWriteThread *m_pPartFileWriteThread;
	CGeoLite2			*geolite2;
	CConChecker			*ConChecker;
	CDownloadChecker	*DownloadChecker;

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
	void		AddEd2kLinksToDownload(const CString &strLinks, int cat);
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
	bool MediaInfoLibHintGiven;

	void StartDirWatchTP();
	void StopDirWatchTP();
	void SyncDirWatchRootsHash();
	void DrainAutoSharedNewDirs();

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

	HIMAGELIST	m_hBigSystemImageList;
	CMapStringToPtr m_aBigExtToSysImgIdx;
	CSize		m_sizBigSystemIcon;

	CString		m_strDefaultFontFaceName;
	CString		m_strLastClipboardContents;

	// thread safe log calls
	CCriticalSection m_queueLock;
	CTypedPtrList<CPtrList, SLogItem*> m_QueueDebugLog;
	CTypedPtrList<CPtrList, SLogItem*> m_QueueLog;

	WSADATA		m_wsaData;
	uint32		m_dwPublicIP;
	bool		m_bGuardClipboardPrompt;
	bool		m_bAutoStart;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnHelp();

private:
	UINT		m_wTimerRes;
	bool		m_bStandbyOff;
	uint8			m_nConnectionState;
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
	static UINT AFX_CDECL RunProc(LPVOID pParam);
	CWinThread* pBackupThread;

private:
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