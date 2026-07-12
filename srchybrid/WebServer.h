#pragma once

#include "WebSocket.h"
#include "PartFile.h"
#include "QArray.h"
#include "SearchList.h"
#include "zlib/zlib.h"

#define WEB_GRAPH_HEIGHT		120
#define WEB_GRAPH_WIDTH			500

#define SESSION_TIMEOUT_SECS	300	// 5 minutes session expiration
#define SHORT_LENGTH_MAX		60	// Max size for strings maximum
#define SHORT_LENGTH			40	// Max size for strings
#define SHORT_LENGTH_MIN		30	// Max size for strings minimum

typedef struct
{
	double download;
	double upload;
	long connections;
} UpDown;

typedef struct
{
	CTime	startTime;
	long	lSession;
	int		lastcat;
	bool	admin;
} Session;

struct BadLogin
{
	uint32	datalen;
	DWORD	timestamp;
};

typedef struct
{
	CString	sCategory;
	CString	sED2kLink;
	CString	sFileHash;
	CString	sDownloadBar;
	CString	sFileInfo;
	CString	sFileName;
	CString	sFileNameJS;
	CString	sFileState;
	CString	sFileType;
	double	m_dblCompleted;
	uint64	m_qwFileSize;
	uint64	m_qwFileTransferred;
	uint32	lFileSpeed;
	long	lNotCurrentSourceCount;
	long	lSourceCount;
	long	lTransferringSourceCount;
	bool	bFileAutoPrio;
	int		iComment;
	int		iFileState;
	int		nFilePrio;
	int		nFileStatus;
	bool	bIsComplete;
	bool	bIsGetFLC;
	bool	bIsPreview;
	int		iCategory;
} DownloadFiles;

typedef struct
{
	CString sFileCompletes;
	CString sFileHash;
	CString sFilePriority;
	CString sFileType;
	CString	sED2kLink;
	CString	sFileName;
	CString	sFileState;
	double	dblFileCompletes;
	uint64	m_qwFileSize;
	uint64	nFileAllTimeTransferred;
	uint64	nFileTransferred;
	uint32	nFileAllTimeRequests;
	uint32	nFileAllTimeAccepts;
	UINT	nFileRequests;
	UINT	nFileAccepts;
	byte	nFilePriority;
	bool	bIsPartFile;
	bool	bFileAutoPriority;
	bool	bDownloadable;
	bool	bReleasePriority;
} SharedFiles;

typedef struct
{
	CString	sClientState;
	CString	sUserHash;
	CString	sActive;
	CString sFileInfo;
	CString	sClientExtra;
	CString	sUserName;
	CString	sFileName;
	CString	sClientNameVersion;
	uint64	nTransferredDown;
	uint64	nTransferredUp;
	int		nDataRate;
	TCHAR	sClientSoft[2];
} UploadUsers;

typedef struct
{
	CString	sClientExtra;
	CString	sClientNameVersion;
//	LPCTSTR	sClientSoft;
	CString	sClientSoftSpecial;
	CString	sClientState;
	CString	sClientStateSpecial;
	CString	sFileName;
	CString	sIndex;	//SyruS CQArray-Sorting element
	CString	sUserHash;
	CString	sUserName;
	uint32	nScore;
	TCHAR	sClientSoft[2];
} QueueUsers;

struct WebTransferSnapshot
{
	WebTransferSnapshot();
	WebTransferSnapshot(const WebTransferSnapshot &source)
	{
		CopyFrom(source);
	}
	WebTransferSnapshot& operator=(const WebTransferSnapshot &source)
	{
		if (this != &source)
			CopyFrom(source);
		return *this;
	}
	void CopyFrom(const WebTransferSnapshot &source)
	{
		FilesArray.RemoveAll();
		FilesArray.Append(source.FilesArray);
		UploadArray.RemoveAll();
		UploadArray.Append(source.UploadArray);
		QueueArray.RemoveAll();
		QueueArray.Append(source.QueueArray);
		nCountQueue = source.nCountQueue;
		nCountQueueBanned = source.nCountQueueBanned;
		nCountQueueFriend = source.nCountQueueFriend;
		nCountQueueSecure = source.nCountQueueSecure;
		nCountQueueBannedSecure = source.nCountQueueBannedSecure;
		nCountQueueFriendSecure = source.nCountQueueFriendSecure;
		bStale = source.bStale;
	}

	CArray<DownloadFiles> FilesArray;
	CArray<UploadUsers> UploadArray;
	CQArray<QueueUsers, QueueUsers> QueueArray;
	int nCountQueue;
	int nCountQueueBanned;
	int nCountQueueFriend;
	int nCountQueueSecure;
	int nCountQueueBannedSecure;
	int nCountQueueFriendSecure;
	bool bStale;
};

struct WebHeaderSnapshot
{
	WebHeaderSnapshot()
		: bServerConnected(false)
		, bServerConnecting(false)
		, bActiveConnectionAttempt(false)
		, bServerLowId(false)
		, dwConnectionTimeout(0)
		, nCurrentServerUsers(0)
		, nCurrentServerMaxUsers(0)
		, nCurrentServerFiles(0)
		, nAllUsers(0)
		, nAllFiles(0)
		, dDownloadDatarate(0.0)
		, dUploadDatarate(0.0)
		, nOpenSockets(0)
		, bKadConnected(false)
		, bKadFirewalled(false)
		, bKadRunning(false)
	{
	}

	bool bServerConnected;
	bool bServerConnecting;
	bool bActiveConnectionAttempt;
	bool bServerLowId;
	DWORD dwConnectionTimeout;
	CString sCurrentServerName;
	uint32 nCurrentServerUsers;
	uint32 nCurrentServerMaxUsers;
	uint32 nCurrentServerFiles;
	uint32 nAllUsers;
	uint32 nAllFiles;
	double dDownloadDatarate;
	double dUploadDatarate;
	UINT nOpenSockets;
	bool bKadConnected;
	bool bKadFirewalled;
	bool bKadRunning;
};

struct WebSharedFileDownloadInfo
{
	WebSharedFileDownloadInfo()
		: qwFileSize(0)
		, bFound(false)
	{
	}

	CString sFileName;
	CString sFilePath;
	uint64 qwFileSize;
	bool bFound;
};

struct SortParams
{
	int eSort;
	bool bReverse;
};

typedef enum
{
	  DOWN_SORT_STATE
	, DOWN_SORT_TYPE
	, DOWN_SORT_NAME
	, DOWN_SORT_SIZE
	, DOWN_SORT_TRANSFERRED
	, DOWN_SORT_SPEED
	, DOWN_SORT_PROGRESS
	, DOWN_SORT_SOURCES
	, DOWN_SORT_PRIORITY
	, DOWN_SORT_CATEGORY
//	, DOWN_SORT_FAKECHECK unused
} DownloadSort;

typedef enum
{
	UP_SORT_CLIENT,
	UP_SORT_USER,
	UP_SORT_VERSION,
	UP_SORT_FILENAME,
	UP_SORT_TRANSFERRED,
	UP_SORT_SPEED
} UploadSort;

typedef enum
{
	QU_SORT_CLIENT,
	QU_SORT_USER,
	QU_SORT_VERSION,
	QU_SORT_FILENAME,
	QU_SORT_SCORE
} QueueSort;

typedef enum
{
	SHARED_SORT_STATE,
	SHARED_SORT_TYPE,
	SHARED_SORT_NAME,
	SHARED_SORT_SIZE,
	SHARED_SORT_TRANSFERRED,
	SHARED_SORT_ALL_TIME_TRANSFERRED,
	SHARED_SORT_REQUESTS,
	SHARED_SORT_ALL_TIME_REQUESTS,
	SHARED_SORT_ACCEPTS,
	SHARED_SORT_ALL_TIME_ACCEPTS,
	SHARED_SORT_COMPLETES,
	SHARED_SORT_PRIORITY
} SharedSort;

typedef struct
{
	CString	sServerDescription;
	CString	sServerFullIP; //for sorting
	CString	sServerIP;
	CString	sServerName;
	CString	sServerPriority;
	CString	sServerState;
	CString	sServerVersion;
	uint32	nServerFiles;
	uint32	nServerHardLimit;
	uint32	nServerMaxUsers;
	uint32	nServerPing;
	uint32	nServerSoftLimit;
	uint32	nServerUsers;
	int		nServerFailed;
	int		nServerPort;
	byte	nServerPriority;
	bool	bServerStatic;
} ServerEntry;

typedef enum
{
	SERVER_SORT_STATE,
	SERVER_SORT_NAME,
	SERVER_SORT_IP,
	SERVER_SORT_DESCRIPTION,
	SERVER_SORT_PING,
	SERVER_SORT_USERS,
	SERVER_SORT_FILES,
	SERVER_SORT_PRIORITY,
	SERVER_SORT_FAILED,
	SERVER_SORT_LIMIT,
	SERVER_SORT_VERSION
} ServerSort;

typedef struct
{
	CArray<UpDown>	PointsForWeb;
	CArray<Session>	Sessions;
	CArray<BadLogin> badlogins;	//TransferredData= IP : time

	CString			sETag;
	CString			sLastModified;
	CString			sShowServerIP;		//Purity: Action Buttons
	CString			sShowSharedFile;	//Purity: Action Buttons
	CString			sShowTransferFile;	//Purity: Action Buttons

	uint32			nUsers;
	DownloadSort	DownloadSort;
	QueueSort		QueueSort;
	ServerSort		ServerSort;
	SharedSort		SharedSort;
	UploadSort		UploadSort;
	bool			bDownloadSortReverse;
	bool			bQueueSortReverse;
	bool			bServerSortReverse;
	bool			bSharedSortReverse;
	bool			bShowServerLine;	//Purity: Action Buttons
	bool			bShowSharedLine;	//Purity: Action Buttons
	bool			bShowTransferLine;	//Purity: Action Buttons
	bool			bShowUploadQueue;
	bool			bShowUploadQueueBanned;
	bool			bShowUploadQueueFriend;
	bool			bUploadSortReverse;
} GlobalParams;

typedef struct
{
	CString			sURL;
	void			*pThis;
	CWebSocket		*pSocket;
	in_addr			inadr;
} ThreadData;

class CWebServer;
struct SSearchParams;

enum WebServerCommand
{
	WEB_SERVER_COMMAND_ADD,
	WEB_SERVER_COMMAND_REMOVE,
	WEB_SERVER_COMMAND_ADD_TO_STATIC,
	WEB_SERVER_COMMAND_REMOVE_FROM_STATIC,
	WEB_SERVER_COMMAND_SET_PRIORITY,
	WEB_SERVER_COMMAND_CONNECT,
	WEB_SERVER_COMMAND_CONNECT_ANY,
	WEB_SERVER_COMMAND_STOP_CONNECTING,
	WEB_SERVER_COMMAND_DISCONNECT_ED2K,
	WEB_SERVER_COMMAND_CONNECT_KAD,
	WEB_SERVER_COMMAND_DISCONNECT_KAD,
	WEB_SERVER_COMMAND_BOOTSTRAP_KAD,
	WEB_SERVER_COMMAND_UPDATE_SERVER_MET_FROM_URL,
	WEB_SERVER_COMMAND_RECHECK_KAD_FIREWALL
};

struct SWebAsyncRequest
{
	SWebAsyncRequest()
		: m_dwMagic(0x57454241)
		, m_lRefCount(1)
		, m_hCompleteEvent(NULL)
	{
	}

	virtual ~SWebAsyncRequest()
	{
		m_dwMagic = 0;
		if (m_hCompleteEvent != NULL) {
			::CloseHandle(m_hCompleteEvent);
			m_hCompleteEvent = NULL;
		}
	}

	bool IsValidWebAsyncRequest() const	{ return m_dwMagic == 0x57454241; }
	void AddRef()					{ ::InterlockedIncrement(&m_lRefCount); }
	void ReleaseReference()			{ if (::InterlockedDecrement(&m_lRefCount) == 0) delete this; }
	bool CreateCompletionEvent()		{ if (m_hCompleteEvent == NULL) m_hCompleteEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL); return m_hCompleteEvent != NULL; }
	HANDLE GetCompletionEvent() const	{ return m_hCompleteEvent; }
	void Complete()				{ if (m_hCompleteEvent != NULL) ::SetEvent(m_hCompleteEvent); }
	void Release()					{}

	DWORD m_dwMagic;
	LONG m_lRefCount;
	HANDLE m_hCompleteEvent;
};

struct SWebSearchResultsRequest : public SWebAsyncRequest
{
	SWebSearchResultsRequest()
		: m_pThis(NULL)
		, m_pSearchFileArray(&m_SearchFileArray)
		, m_iSortBy(0)
		, m_bResult(false)
	{
	}

	CWebServer* m_pThis;
	CQArray<SearchFileStruct, SearchFileStruct>* m_pSearchFileArray;
	CQArray<SearchFileStruct, SearchFileStruct> m_SearchFileArray;
	int m_iSortBy;
	bool m_bResult;
};

struct SWebServerCommandRequest : public SWebAsyncRequest
{
	SWebServerCommandRequest()
		: m_eCommand(WEB_SERVER_COMMAND_ADD)
		, m_nPort(0)
		, m_bResult(false)
		, m_bStatic(false)
		, m_bAddToStatic(false)
		, m_bConnectNow(false)
		, m_nPriority(0)
	{
	}

	WebServerCommand m_eCommand;
	CString m_strIP;
	CString m_strServerName;
	CString m_strName;
	CString m_strDescription;
	CString m_strMessage;
	int m_nPort;
	bool m_bResult;
	bool m_bStatic;
	bool m_bAddToStatic;
	bool m_bConnectNow;
	int m_nPriority;
};

struct SWebServerListSnapshotRequest : public SWebAsyncRequest
{
	SWebServerListSnapshotRequest()
		: m_pThis(NULL)
		, m_pServerArray(&m_ServerArray)
		, m_bResult(false)
	{
	}

	CWebServer* m_pThis;
	CArray<ServerEntry>* m_pServerArray;
	CArray<ServerEntry> m_ServerArray;
	bool m_bResult;
};

struct SWebTransferSnapshotRequest : public SWebAsyncRequest
{
	SWebTransferSnapshotRequest()
		: m_pThis(NULL)
		, m_pServer(NULL)
		, m_iCategory(0)
		, m_pSnapshot(&m_Snapshot)
		, m_bResult(false)
	{
	}

	CWebServer* m_pThis;
	CWebServer* m_pServer;
	ThreadData m_Data;
	int m_iCategory;
	WebTransferSnapshot m_Snapshot;
	WebTransferSnapshot m_snapshot;
	WebTransferSnapshot* m_pSnapshot;
	bool m_bResult;
};

struct SWebSharedFilesSnapshotRequest : public SWebAsyncRequest
{
	SWebSharedFilesSnapshotRequest()
		: m_pThis(NULL)
		, m_pSharedArray(&m_SharedArray)
		, m_bResult(false)
	{
	}

	CWebServer* m_pThis;
	CArray<SharedFiles>* m_pSharedArray;
	CArray<SharedFiles> m_SharedArray;
	bool m_bResult;
};

struct SWebHeaderSnapshotRequest : public SWebAsyncRequest
{
	SWebHeaderSnapshotRequest()
		: m_pThis(NULL)
		, m_pSnapshot(&m_Snapshot)
		, m_bResult(false)
	{
	}

	CWebServer* m_pThis;
	WebHeaderSnapshot* m_pSnapshot;
	WebHeaderSnapshot m_Snapshot;
	bool m_bResult;
};

struct SWebCommentListRequest : public SWebAsyncRequest
{
	SWebCommentListRequest()
		: m_pThis(NULL)
		, m_pstrCommentList(&m_strCommentList)
		, m_bResult(false)
	{
	}

	CWebServer* m_pThis;
	ThreadData m_Data;
	CString m_strFileHash;
	CString m_strCommentList;
	CString* m_pstrCommentList;
	bool m_bResult;
};

struct SWebFriendCommandRequest : public SWebAsyncRequest
{
	SWebFriendCommandRequest()
		: m_bAdd(false)
		, m_bResult(false)
	{
	}

	CString m_strUserHash;
	bool m_bAdd;
	bool m_bResult;
};

inline WebTransferSnapshot CopyWebTransferSnapshot(const WebTransferSnapshot &source)
{
	return WebTransferSnapshot(source);
}


typedef struct
{
	CString	sAddServerBox;
	CString	sBootstrapLine;
	CString	sCatArrow;
	CString	sCommentList;
	CString	sCommentListLine;
	CString	sDebugLog;
	CString	sDownArrow;
	CString	sDownDoubleArrow;
	CString	sFooter;
	CString	sGraphs;
	CString	sHeader;
	CString	sHeaderStylesheet;
	CString	sKad;
	CString	sLog;
	CString	sLogin;
	CString	sMyInfoLog;
	CString	sPreferences;
	CString	sProgressbarImgs;
	CString	sProgressbarImgsPercent;
	CString	sSearch;
	CString	sSearchHeader;
	CString	sSearchResultLine;
	CString	sServerInfo;
	CString	sServerLine;
	CString	sServerList;
	CString	sSharedLine;
	CString	sSharedList;
	CString	sStats;
	CString	sTransferDownFooter;
	CString	sTransferDownHeader;
	CString	sTransferDownLine;
	CString	sTransferImages;
	CString	sTransferList;
	CString	sTransferUpFooter;
	CString	sTransferUpHeader;
	CString	sTransferUpLine;
	CString	sTransferUpQueueBannedHide;
	CString	sTransferUpQueueBannedLine;
	CString	sTransferUpQueueBannedShow;
	CString	sTransferUpQueueFriendHide;
	CString	sTransferUpQueueFriendLine;
	CString	sTransferUpQueueFriendShow;
	CString	sTransferUpQueueHide;
	CString	sTransferUpQueueLine;
	CString	sTransferUpQueueShow;
	CString	sUpArrow;
	CString	sUpDoubleArrow;
	uint16	iProgressbarWidth;
} WebTemplates;

struct SWebTransferRenamePreview
{
	CString m_strFileHash;
	CString m_strFileName;
	DWORD m_dwTick;
};

class CWebServer
{
	friend class CWebSocket;

public:
	CWebServer();
	~CWebServer();

	inline void SetIP(u_long ip)				{ m_uCurIP = ip; } //practically not used
	INT_PTR UpdateSessionCount();
	void StartServer();
	void StopServer();
	void RestartSockets();
	void AddStatsLine(const UpDown &line);
	bool ReloadTemplates();
	INT_PTR GetSessionCount()					{ return m_Params.Sessions.GetCount(); }
	bool IsRunning() const						{ return m_bServerWorking; }

	static bool		ExecuteServerCommandForWebThread(WebServerCommand eCommand, const CString &sIP, int nPort);
	static bool		ExecuteServerCommandForWebThread(WebServerCommand eCommand, const CString &sIP, int nPort, int nPriority);
	static bool		ExecuteServerCommandForWebThread(WebServerCommand eCommand, const CString &sIP, int nPort, const CString &sName, const CString &sDescription);
	static bool		ExecuteServerCommandForWebThread(WebServerCommand eCommand, const CString &sIP, int nPort, const CString &sName, const CString &sDescription, bool bStatic);
	static bool		ExecuteServerCommandForWebThread(WebServerCommand eCommand, const CString &sIP, int nPort, const CString &sName, const CString &sDescription, bool bStatic, int nPriority);
	static bool		ExecuteServerCommandForWebThread(WebServerCommand eCommand, const CString &sIP, int nPort, int nPriority, const CString &sName, bool bAddToStatic, bool bConnectNow);
	static bool		ExecuteServerCommandForWebThread(WebServerCommand eCommand, const CString &sIP, int nPort, const CString &sName, const CString &sDescription, bool bStatic, int nPriority, bool bAddToStatic, bool bConnectNow);
	static void		AnnotateWebSearchSnapshot(CQArray<SearchFileStruct, SearchFileStruct> &searchFileArray);
	static bool		BuildHeaderSnapshotForWebThread(WebHeaderSnapshot &snapshot);
	static bool		BuildSharedFileDownloadInfoForWebThread(const CString &strFileHash, WebSharedFileDownloadInfo &info);
	static CString	BuildCommentListForWebThread(const ThreadData &Data);
	bool				BuildTransferSnapshotForWebThread(const ThreadData &Data, int cat, WebTransferSnapshot &snapshot);
	bool				BuildTransferSnapshotForWebThread(CWebServer *pThis, const ThreadData &Data, int cat, WebTransferSnapshot &snapshot);
	static bool		BuildSharedFilesSnapshotForWebThread(CArray<SharedFiles> &SharedArray);
	static bool		BuildServerListSnapshotForWebThread(CArray<ServerEntry> &ServerArray);
	void			StoreWebSearchSnapshot(int iSortBy, const CQArray<SearchFileStruct, SearchFileStruct> &searchFileArray);
	bool			CopyWebSearchSnapshot(int iSortBy, CQArray<SearchFileStruct, SearchFileStruct> &searchFileArray) const;
	bool			BuildWebSearchDownloadLinksFromSnapshot(const CString &strHashes, CString &strLinks) const;
	void			StoreWebHeaderSnapshot(const WebHeaderSnapshot &snapshot);
	bool			CopyWebHeaderSnapshot(WebHeaderSnapshot &snapshot) const;
	void			StoreWebServerListSnapshot(const CArray<ServerEntry> &serverArray);
	bool			CopyWebServerListSnapshot(CArray<ServerEntry> &serverArray) const;
	void			StoreWebTransferSnapshot(int iCategory, const WebTransferSnapshot &snapshot);
	bool			CopyWebTransferSnapshot(int iCategory, WebTransferSnapshot &snapshot) const;
	void			RememberWebTransferRenamePreview(const CString &strFileHash, const CString &strFileName);
	void			ApplyWebTransferRenamePreviews(WebTransferSnapshot &snapshot) const;
	void			StoreWebSharedFilesSnapshot(const CArray<SharedFiles> &sharedArray);
	bool			CopyWebSharedFilesSnapshot(CArray<SharedFiles> &sharedArray) const;
	void			StoreWebCommentList(const CString &strFileHash, const CString &strCommentList);
	bool			CopyWebCommentList(const CString &strFileHash, CString &strCommentList) const;
protected:
	//all static method names have an underscore prefix
	static void		_ProcessURL(const ThreadData &Data);
	static void		_ProcessFileReq(const ThreadData &Data);

private:
	static CString	_GetHeader(const ThreadData &Data, long lSession);
	static const CString _GetFooter(const ThreadData &Data);
	static CString	_GetServerList(const ThreadData &Data);
	static CString	_GetTransferList(const ThreadData &Data);
	static CString	_GetSharedFilesList(const ThreadData &Data);
	static CString	_GetGraphs(const ThreadData &Data);
	static CString	_GetLog(const ThreadData &Data);
	static CString	_GetServerInfo(const ThreadData &Data);
	static CString	_GetDebugLog(const ThreadData &Data);
	static CString	_GetStats(const ThreadData &Data);
	static CString  _GetKadDlg(const ThreadData &Data);
	static CString	_GetPreferences(const ThreadData &Data);
	static CString	_GetLoginScreen(const ThreadData &Data);
	static CString	_GetAddServerBox(const ThreadData &Data);
	static CString	_GetCommentlist(const ThreadData &Data);
	static void		_RemoveServer(const CString &sIP, int nPort);
	static void		_AddToStatic(const CString &sIP, int nPort);
	static void		_RemoveFromStatic(const CString &sIP, int nPort);

	static CString	_GetSearch(const ThreadData &Data);

	static CString	_ParseURL(const CString &URL, const CString &fieldname);
	static CString	_ParseURLArray(CString URL, CString fieldname);
	static bool		_IsTransferCommandRequest(const CString &URL);
	static bool		_ApplyWebTransferRenamePreview(WebTransferSnapshot &snapshot, const CString &strFileHash, const CString &strFileName);
	void			ApplyWebTransferRenamePreviewsLocked(WebTransferSnapshot &snapshot) const;
	static void		_ConnectToServer(const CString &sIP, int nPort);
	static bool		_IsLoggedIn(const ThreadData &Data, long lSession);
	static void		_RemoveTimeOuts(const ThreadData &Data);
	static bool		_RemoveSession(const ThreadData &Data, long lSession);
	static CString	_SpecialChars(const CString &cstr, bool noquote = true);
	static CString	_GetPlainResString(LPCTSTR nID, bool noquote = true);
	static void		_GetPlainResString(CString &rstrOut, LPCTSTR nID, bool noquote = true);
	static int		_GzipCompress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen, int level);
	static CString	_LoadTemplate(const CString &sAll, const CString &sTemplateName);
	static Session	_GetSessionByID(const ThreadData &Data, long sessionID);
	static bool		_IsSessionAdmin(const ThreadData &Data, long sessionID);
	static bool		_IsSessionAdmin(const ThreadData &Data, const CString &strSsessionID);
	static CString	_GetPermissionDenied();
	static CString	_GetDownloadGraph(const ThreadData &Data, const CPartFile *pPartFile);
	static void		_InsertCatBox(CString &Out, int preselect, LPCTSTR boxlabel, bool jump, bool extraCats, const CString &sSession, const CString &sFileHash, bool ed2kbox = false, int iFileCategory = -1);
	static CString	_GetSubCatLabel(int cat);
	static CString  _GetRemoteLinkAddedOk(const ThreadData &Data);
	static CString  _GetRemoteLinkAddedFailed(const ThreadData &Data);
	static void		_SetLastUserCat(const ThreadData &Data, long lSession, int cat);
	static int		_GetLastUserCat(const ThreadData &Data, long lSession);
	static void		_MakeTransferList(CString &Out, CWebServer *pThis, const ThreadData &Data, const WebTransferSnapshot &snapshot, bool bAdmin);
	static bool		_GetServerListSnapshot(CWebServer *pThis, CArray<ServerEntry> &ServerArray);
	static bool		_GetTransferSnapshot(CWebServer *pThis, const ThreadData &Data, int cat, WebTransferSnapshot &snapshot);
	static bool		_GetSharedFilesSnapshot(CWebServer *pThis, CArray<SharedFiles> &SharedArray);
	static void		_SetBoolean(bool &var, const CString &URL, LPCTSTR pFieldname);

	static bool		_BuildServerListSnapshot(CArray<ServerEntry> &ServerArray);
	static void		_SaveWIConfigArray(BOOL *array, int size, LPCTSTR key);
	static CString	_GetWebImageNameForFileType(const CString &filename);
	static CString  _GetClientSummary(const CUpDownClient &client, const CString &strUploadFileName);
	static CString	_GetMyInfo(const ThreadData &Data);
	static void		_GetClientversionImage(const CUpDownClient &client, TCHAR pSoft[2]);

	bool			_GetIsTempDisabled() const	{ return m_bIsTempDisabled; } //never used

	//comparators for quick sort
	static int AFX_CDECL _DownloadCmp(void *prm, void const *pv1, void const *pv2);
	static int AFX_CDECL _ServerCmp(void *prm, void const *pv1, void const *pv2);
	static int AFX_CDECL _SharedCmp(void *prm, void const *pv1, void const *pv2);
	static int AFX_CDECL _UploadCmp(void *prm, void const *pv1, void const *pv2);

	// Web snapshot cache
	mutable CCriticalSection m_WebSnapshotCacheLock;
	bool			m_bHasCachedSearchSnapshot;
	int			m_iCachedSearchSortBy;
	CQArray<SearchFileStruct, SearchFileStruct> m_CachedSearchFileArray;
	bool			m_bHasCachedHeaderSnapshot;
	WebHeaderSnapshot m_CachedHeaderSnapshot;
	bool			m_bHasCachedServerListSnapshot;
	CArray<ServerEntry> m_CachedServerArray;
	bool			m_bHasCachedTransferSnapshot;
	int			m_iCachedTransferCategory;
	WebTransferSnapshot m_CachedTransferSnapshot;
	mutable CArray<SWebTransferRenamePreview, const SWebTransferRenamePreview&> m_PendingTransferRenames;
	bool			m_bHasCachedSharedFilesSnapshot;
	CArray<SharedFiles> m_CachedSharedArray;
	bool			m_bHasCachedCommentList;
	CString		m_strCachedCommentFileHash;
	CString		m_strCachedCommentList;

	// Common data
	GlobalParams	m_Params;
	WebTemplates	m_Templates;
	u_long			m_uCurIP;
	int				m_iSearchSortby;
	uint16			m_nIntruderDetect;
	bool			m_bServerWorking;
	bool			m_bSearchAsc;
	bool			m_bIsTempDisabled;
};
