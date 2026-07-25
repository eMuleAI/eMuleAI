#include "stdafx.h"
#include <locale.h>
#include <math.h>
#include <algorithm>
#include "emule.h"
#include "StringConversion.h"
#include "WebServer.h"
#include "ClientCredits.h"
#include "ClientList.h"
#include "DownloadQueue.h"
#include "ED2KLink.h"
#include "emuledlg.h"
#include "FriendList.h"
#include "MD5Sum.h"
#include "ini2.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "KademliaWnd.h"
#include "KadSearchListCtrl.h"
#include "kademlia/kademlia/Entry.h"
#include "KnownFileList.h"
#include "ListenSocket.h"
#include "Log.h"
#include "MenuCmds.h"
#include "Preferences.h"
#include "Server.h"
#include "ServerList.h"
#include "ServerWnd.h"
#include "SearchList.h"
#include "SearchDlg.h"
#include "SearchParams.h"
#include "SharedFileList.h"
#include "ServerConnect.h"
#include "StatisticsDlg.h"
#include "Opcodes.h"
#include "QArray.h"
#include "TransferDlg.h"
#include "UploadQueue.h"
#include "UpDownClient.h"
#include "UserMsgs.h"
#include "eMuleAI/DarkMode.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#ifdef UNICODE
#define _tcreate_locale  _wcreate_locale
#else
#define _tcreate_locale  _create_locale
#endif // !UNICODE

#define HTTPInit "Server: eMule\r\nConnection: close\r\nContent-Type: text/html\r\n"
#define HTTPInitGZ HTTPInit "Content-Encoding: gzip\r\n"
#define HTTPENCODING _T("utf-8")

#define WEB_SERVER_TEMPLATES_VERSION	7

static const INT_PTR WEB_MAX_TABLE_ROWS = 2000;
static const DWORD WEB_TRANSFER_SNAPSHOT_SYNC_WAIT_MS = 5000;
static const DWORD WEB_TRANSFER_COMMAND_SNAPSHOT_SYNC_WAIT_MS = 5000;
static const DWORD WEB_SHARED_FILES_SNAPSHOT_SYNC_WAIT_MS = 5000;
static const DWORD WEB_TRANSFER_RENAME_PREVIEW_TTL_MS = 15000;

static CString _WebSelectString(bool bCondition, LPCTSTR pszTrue, LPCTSTR pszFalse)
{
	return CString(bCondition ? (pszTrue != NULL ? pszTrue : _T("")) : (pszFalse != NULL ? pszFalse : _T("")));
}

static double WebKBytesPerSecToMbitPerSec(uint32 nKBytesPerSec)
{
	return (double)nKBytesPerSec * 8192.0 / 1000000.0;
}

static bool TryParseWebDisplayDouble(CString strValue, double& rfValue)
{
	strValue.Trim();
	if (strValue.IsEmpty())
		return false;
	strValue.Replace(_T(','), _T('.'));
	LPCTSTR pszText = strValue;
	LPTSTR pszEnd = NULL;
	_locale_t locale = _create_locale(LC_NUMERIC, "C");
	const double fValue = locale != NULL ? _tcstod_l(pszText, &pszEnd, locale) : _tcstod(pszText, &pszEnd);
	if (locale != NULL)
		_free_locale(locale);
	if (pszEnd == pszText || !_finite(fValue))
		return false;
	CString strTail(pszEnd);
	strTail.Trim();
	if (!strTail.IsEmpty())
		return false;
	rfValue = fValue;
	return true;
}

static uint32 WebNumericDisplayToKBytesPerSec(double fValue)
{
	if (fValue <= 0.0)
		return 0;
	if (!thePrefs.GetForceSpeedsToKB())
		fValue = fValue * 1000000.0 / 8192.0;
	if (fValue >= (double)UNLIMITED)
		return UNLIMITED - 1;
	return (uint32)(fValue + 0.5);
}

static CString FormatWebSpeedPreferenceValue(uint32 nKBytesPerSec)
{
	CString strValue;
	if (thePrefs.GetForceSpeedsToKB())
		strValue.Format(_T("%u"), nKBytesPerSec);
	else {
		const double fMbitPerSec = WebKBytesPerSecToMbitPerSec(nKBytesPerSec);
		const double fRounded = floor(fMbitPerSec + 0.5);
		if (fabs(fMbitPerSec - fRounded) < 0.05)
			strValue.Format(_T("%.0f"), fMbitPerSec);
		else
			strValue.Format(_T("%.1f"), fMbitPerSec);
	}
	return strValue;
}

static uint32 ParseWebSpeedPreferenceValue(const CString& rstrValue)
{
	double fValue = 0.0;
	return TryParseWebDisplayDouble(rstrValue, fValue) ? WebNumericDisplayToKBytesPerSec(fValue) : 0;
}

//SyruS CQArray-Sorting operators
bool operator > (QueueUsers &first, QueueUsers &second)
{
	return (first.sIndex.CompareNoCase(second.sIndex) > 0);
}
bool operator < (QueueUsers &first, QueueUsers &second)
{
	return (first.sIndex.CompareNoCase(second.sIndex) < 0);
}

bool operator > (SearchFileStruct &first, SearchFileStruct &second)
{
	return (first.m_strIndex.CompareNoCase(second.m_strIndex) > 0);
}
bool operator < (SearchFileStruct &first, SearchFileStruct &second)
{
	return (first.m_strIndex.CompareNoCase(second.m_strIndex) < 0);
}

static BOOL	WSdownloadColumnHidden[8];
static BOOL	WSuploadColumnHidden[5];
static BOOL	WSqueueColumnHidden[4];

static void QueueWebDownloadLinks(const CString &strLinks, int iCategory)
{
	if (strLinks.IsEmpty())
		return;

	CemuleApp::SDownloadCommand command;
	command.m_eType = CemuleApp::DownloadCommandAddFileLinks;
	command.m_iCat = iCategory;
	command.m_strRawLinks = strLinks;
	command.m_strTokenDelimiters = _T(" \t\r\n");
	CString strOrderingKey;
	strOrderingKey.Format(_T("webserver:add-downloads:%d"), iCategory);
	theApp.ExecuteDownloadCommand(command, CemuleApp::BackendCommandSourceWebServer, CemuleApp::BackendCommandOrderingWebRequest, strOrderingKey);
}

static bool PostWebUiRequestNoWait(UINT uMessage, WPARAM wParam, SWebAsyncRequest *pRequest)
{
	if (pRequest == NULL || theApp.IsClosing() || theApp.emuledlg == NULL || !::IsWindow(theApp.emuledlg->m_hWnd))
		return false;

	pRequest->AddRef();
	if (!::PostMessage(theApp.emuledlg->m_hWnd, uMessage, wParam, reinterpret_cast<LPARAM>(pRequest))) {
		pRequest->ReleaseReference();
		return false;
	}

	return true;
}

static void QueueWebUiSnapshotRefresh(UINT uMessage, WPARAM wParam, SWebAsyncRequest *pRequest)
{
	if (pRequest == NULL)
		return;
	PostWebUiRequestNoWait(uMessage, wParam, pRequest);
	pRequest->ReleaseReference();
}

static bool PostWebUiRequestAndWait(UINT uMessage, WPARAM wParam, SWebAsyncRequest *pRequest, DWORD dwTimeout)
{
	if (pRequest == NULL || theApp.IsUiThread() || !pRequest->CreateCompletionEvent())
		return false;
	if (!PostWebUiRequestNoWait(uMessage, wParam, pRequest))
		return false;
	return ::WaitForSingleObject(pRequest->GetCompletionEvent(), dwTimeout) == WAIT_OBJECT_0;
}

static bool QueueWebSearchStartCommand(const SSearchParams &params, CString &strResponse)
{
	SSearchParams *pParams = new SSearchParams(params);
	CString strOrderingKey;
	strOrderingKey.Format(_T("webserver:search:%lu"), ::GetTickCount());
	theApp.ExecuteSearchStartCommand(pParams, CemuleApp::BackendCommandSourceWebServer, CemuleApp::BackendCommandOrderingSearch, strOrderingKey);
	strResponse = GetResString(_T("SW_SEARCHINGINFO"));
	strResponse.Remove(_T('&'));
	return true;
}

bool CWebServer::_IsTransferCommandRequest(const CString &URL)
{
	const CString sOp(_ParseURL(URL, _T("op")));
	if (!sOp.IsEmpty()) {
		if (sOp == _T("cancel") || sOp == _T("stop") || sOp == _T("pause") || sOp == _T("resume") || sOp == _T("priolow") || sOp == _T("prionormal") || sOp == _T("priohigh") || sOp == _T("prioauto") || sOp == _T("setcat") || sOp == _T("rename") || sOp == _T("getflc")) {
			uchar abyFileHash[MDX_DIGEST_SIZE];
			return strmd4(_ParseURL(URL, _T("file")), abyFileHash);
		}
		if (sOp == _T("addfriend") || sOp == _T("removefriend"))
			return !_ParseURL(URL, _T("userhash")).IsEmpty();
	}
	return !_ParseURL(URL, _T("clearcompleted")).IsEmpty() || !_ParseURL(URL, _T("ed2k")).IsEmpty();
}

bool CWebServer::_ApplyWebTransferRenamePreview(WebTransferSnapshot &snapshot, const CString &strFileHash, const CString &strFileName)
{
	if (strFileHash.IsEmpty() || strFileName.IsEmpty())
		return false;

	const CString strDisplayName(_SpecialChars(strFileName));
	bool bUpdated = false;
	for (INT_PTR i = 0; i < snapshot.FilesArray.GetCount(); ++i) {
		DownloadFiles &file = snapshot.FilesArray[i];
		if (file.sFileHash.CompareNoCase(strFileHash) != 0)
			continue;
		file.sFileName = strDisplayName;
		file.sFileNameJS = strDisplayName;
		file.sFileType = _GetWebImageNameForFileType(strDisplayName);
		file.sED2kLink.Format(_T("ed2k://|file|%s|%I64u|%s|/"), (LPCTSTR)EncodeUrlUtf8(strFileName), file.m_qwFileSize, (LPCTSTR)file.sFileHash);
		bUpdated = true;
	}
	return bUpdated;
}

void CWebServer::AnnotateWebSearchSnapshot(CQArray<SearchFileStruct, SearchFileStruct> &searchFileArray)
{
	for (INT_PTR i = 0; i < searchFileArray.GetCount(); ++i) {
		SearchFileStruct &searchFile = searchFileArray[i];
		searchFile.m_strOverlayImage = _T("none");
		searchFile.m_strTextColor = _T("#ffffff");

		uchar aFileHash[MDX_DIGEST_SIZE];
		if (searchFile.m_strFileHash.GetLength() != 32 || !DecodeBase16(searchFile.m_strFileHash, 32, aFileHash, _countof(aFileHash)))
			continue;

		if (theApp.downloadqueue != NULL && theApp.downloadqueue->GetFileByID(aFileHash) != NULL)
			searchFile.m_strTextColor = _T("#8080ff");
		else {
			bool bSameFile = false;
			if (theApp.sharedfiles != NULL) {
				CSharedFileList::SWebSharedFileSnapshot webSharedSnapshot;
				bSameFile = theApp.sharedfiles->CopyWebSharedFileSnapshot(searchFile.m_strFileHash, webSharedSnapshot);
			}
			if (!bSameFile && theApp.knownfiles != NULL)
				bSameFile = (theApp.knownfiles->FindKnownFileByID(aFileHash) != NULL);
			if (bSameFile)
				searchFile.m_strTextColor = _T("#ff80ff");
		}
	}
}

static void GetWebSearchList(CWebServer *pThis, CQArray<SearchFileStruct, SearchFileStruct> &searchFileArray, int iSortBy)
{
	searchFileArray.RemoveAll();
	if (pThis == NULL || theApp.searchlist == NULL)
		return;
	if (theApp.IsUiThread()) {
		theApp.searchlist->GetWebList(&searchFileArray, iSortBy, WEB_MAX_TABLE_ROWS);
		CWebServer::AnnotateWebSearchSnapshot(searchFileArray);
		pThis->StoreWebSearchSnapshot(iSortBy, searchFileArray);
		return;
	}

	const bool bHasCachedSnapshot = pThis->CopyWebSearchSnapshot(iSortBy, searchFileArray);
	SWebSearchResultsRequest *pRequest = new SWebSearchResultsRequest();
	pRequest->m_pThis = pThis;
	pRequest->m_iSortBy = iSortBy;
	QueueWebUiSnapshotRefresh(WEB_GET_SEARCH_RESULTS, static_cast<WPARAM>(iSortBy), pRequest);
	if (!bHasCachedSnapshot)
		searchFileArray.RemoveAll();
}

static void GetWebSearchDownloadLinks(CWebServer *pThis, const CString &strDownloads, CString &strLinks)
{
	strLinks.Empty();
	if (pThis == NULL || strDownloads.IsEmpty())
		return;
	if (theApp.IsUiThread() && theApp.searchlist != NULL) {
		theApp.searchlist->GetWebDownloadLinksByHashes(strDownloads, strLinks);
		return;
	}
	pThis->BuildWebSearchDownloadLinksFromSnapshot(strDownloads, strLinks);
}

static bool GetWebHeaderSnapshot(CWebServer *pThis, WebHeaderSnapshot &snapshot)
{
	snapshot = WebHeaderSnapshot();
	if (pThis == NULL)
		return false;
	if (theApp.IsUiThread()) {
		const bool bResult = CWebServer::BuildHeaderSnapshotForWebThread(snapshot);
		if (bResult)
			pThis->StoreWebHeaderSnapshot(snapshot);
		return bResult;
	}

	const bool bHasCachedSnapshot = pThis->CopyWebHeaderSnapshot(snapshot);
	SWebHeaderSnapshotRequest *pRequest = new SWebHeaderSnapshotRequest();
	pRequest->m_pThis = pThis;
	QueueWebUiSnapshotRefresh(WEB_GET_HEADER_SNAPSHOT, 0, pRequest);
	return bHasCachedSnapshot;
}

static bool GetWebSharedFileDownloadInfo(const CString &strFileHash, WebSharedFileDownloadInfo &info)
{
	info = WebSharedFileDownloadInfo();
	if (strFileHash.IsEmpty())
		return false;
	return CWebServer::BuildSharedFileDownloadInfoForWebThread(strFileHash, info);
}

static bool ExecuteWebFriendCommand(const CString &strUserHash, bool bAdd)
{
	if (strUserHash.IsEmpty())
		return false;
	if (theApp.IsUiThread()) {
		uchar UserHash[MDX_DIGEST_SIZE];
		if (!strmd4(strUserHash, UserHash))
			return false;
		if (bAdd) {
			CUpDownClient *cur_client = theApp.clientlist != NULL ? theApp.clientlist->FindClientByUserHash(UserHash) : NULL;
			if (cur_client == NULL || theApp.friendlist == NULL)
				return false;
			theApp.friendlist->AddFriend(cur_client);
			return true;
		}

		CFriend *f = theApp.friendlist != NULL ? theApp.friendlist->SearchFriend(UserHash, CAddress(), 0) : NULL;
		if (f == NULL)
			return false;
		theApp.friendlist->RemoveFriend(f);
		return true;
	}

	SWebFriendCommandRequest *pRequest = new SWebFriendCommandRequest();
	pRequest->m_strUserHash = strUserHash;
	pRequest->m_bAdd = bAdd;
	const bool bQueued = PostWebUiRequestNoWait(WEB_FRIEND_COMMAND, 0, pRequest);
	pRequest->ReleaseReference();
	return bQueued;
}

static UINT GetWebSharedFilesPriorityAction(const CString &strPriority)
{
	if (strPriority == _T("verylow"))
		return MP_PRIOVERYLOW;
	if (strPriority == _T("low"))
		return MP_PRIOLOW;
	if (strPriority == _T("normal"))
		return MP_PRIONORMAL;
	if (strPriority == _T("high"))
		return MP_PRIOHIGH;
	if (strPriority == _T("release"))
		return MP_PRIOVERYHIGH;
	return MP_PRIOAUTO;
}

static void QueueWebSharedFilesPriority(const CString &strHash, UINT uAction)
{
	if (strHash.GetLength() != 32 || uAction == 0)
		return;

	uchar fileid[MDX_DIGEST_SIZE];
	if (!DecodeBase16(strHash, strHash.GetLength(), fileid, _countof(fileid)))
		return;

	CStringArray astrHashes;
	astrHashes.Add(md4str(fileid));

	CString strOrderingKey;
	strOrderingKey.Format(_T("webserver:shared-priority:%s"), (LPCTSTR)astrHashes.GetAt(0));
	theApp.ExecuteSharedFilesCommand(uAction, astrHashes, CemuleApp::BackendCommandSourceWebServer, CemuleApp::BackendCommandOrderingSharedFiles, strOrderingKey);
}

static void QueueWebSharedFilesReload()
{
	CStringArray astrHashes;
	theApp.ExecuteSharedFilesCommand(MP_VIEWSHAREDFILES, astrHashes, CemuleApp::BackendCommandSourceWebServer, CemuleApp::BackendCommandOrderingSharedFiles, _T("webserver:shared:reload"));
}

static BOOL	WSsharedColumnHidden[7];
static BOOL	WSserverColumnHidden[10];
static BOOL	WSsearchColumnHidden[4];

WebTransferSnapshot::WebTransferSnapshot()
	: nCountQueue(0)
	, nCountQueueBanned(0)
	, nCountQueueFriend(0)
	, nCountQueueSecure(0)
	, nCountQueueBannedSecure(0)
	, nCountQueueFriendSecure(0)
	, bStale(false)
{
}

CWebServer::CWebServer()
	: m_WebSnapshotCacheLock()
	, m_bHasCachedSearchSnapshot(false)
	, m_iCachedSearchSortBy(0)
	, m_CachedSearchFileArray()
	, m_bHasCachedHeaderSnapshot(false)
	, m_CachedHeaderSnapshot()
	, m_bHasCachedServerListSnapshot(false)
	, m_CachedServerArray()
	, m_bHasCachedTransferSnapshot(false)
	, m_iCachedTransferCategory(0)
	, m_CachedTransferSnapshot()
	, m_bHasCachedSharedFilesSnapshot(false)
	, m_CachedSharedArray()
	, m_bHasCachedCommentList(false)
	, m_strCachedCommentFileHash()
	, m_strCachedCommentList()
	, m_Templates()
	, m_uCurIP()
	, m_iSearchSortby(3)
	, m_nIntruderDetect()
	, m_bServerWorking()
	, m_bSearchAsc()
	, m_bIsTempDisabled()
{
	CIni ini(thePrefs.GetConfigFile(), _T("WebServer"));

	ini.SerGet(true, WSdownloadColumnHidden, _countof(WSdownloadColumnHidden), _T("downloadColumnHidden"));
	ini.SerGet(true, WSuploadColumnHidden, _countof(WSuploadColumnHidden), _T("uploadColumnHidden"));
	ini.SerGet(true, WSqueueColumnHidden, _countof(WSqueueColumnHidden), _T("queueColumnHidden"));
	ini.SerGet(true, WSsearchColumnHidden, _countof(WSsearchColumnHidden), _T("searchColumnHidden"));
	ini.SerGet(true, WSsharedColumnHidden, _countof(WSsharedColumnHidden), _T("sharedColumnHidden"));
	ini.SerGet(true, WSserverColumnHidden, _countof(WSserverColumnHidden), _T("serverColumnHidden"));

	m_Params.bShowUploadQueue = ini.GetBool(_T("ShowUploadQueue"), false);
	m_Params.bShowUploadQueueBanned = ini.GetBool(_T("ShowUploadQueueBanned"), false);
	m_Params.bShowUploadQueueFriend = ini.GetBool(_T("ShowUploadQueueFriend"), false);

	m_Params.bDownloadSortReverse = ini.GetBool(_T("DownloadSortReverse"), true);
	m_Params.bUploadSortReverse = ini.GetBool(_T("UploadSortReverse"), true);
	m_Params.bQueueSortReverse = ini.GetBool(_T("QueueSortReverse"), true);
	m_Params.bServerSortReverse = ini.GetBool(_T("ServerSortReverse"), true);
	m_Params.bSharedSortReverse = ini.GetBool(_T("SharedSortReverse"), true);

	m_Params.DownloadSort = (DownloadSort)ini.GetInt(_T("DownloadSort"), DOWN_SORT_NAME);
	m_Params.UploadSort = (UploadSort)ini.GetInt(_T("UploadSort"), UP_SORT_FILENAME);
	m_Params.QueueSort = (QueueSort)ini.GetInt(_T("QueueSort"), QU_SORT_FILENAME);
	m_Params.ServerSort = (ServerSort)ini.GetInt(_T("ServerSort"), SERVER_SORT_NAME);
	m_Params.SharedSort = (SharedSort)ini.GetInt(_T("SharedSort"), SHARED_SORT_NAME);
}

CWebServer::~CWebServer()
{
	// save layout settings
	CIni ini(thePrefs.GetConfigFile(), _T("WebServer"));

	ini.WriteBool(_T("ShowUploadQueue"), m_Params.bShowUploadQueue);
	ini.WriteBool(_T("ShowUploadQueueBanned"), m_Params.bShowUploadQueueBanned);
	ini.WriteBool(_T("ShowUploadQueueFriend"), m_Params.bShowUploadQueueFriend);

	ini.WriteBool(_T("DownloadSortReverse"), m_Params.bDownloadSortReverse);
	ini.WriteBool(_T("UploadSortReverse"), m_Params.bUploadSortReverse);
	ini.WriteBool(_T("QueueSortReverse"), m_Params.bQueueSortReverse);
	ini.WriteBool(_T("ServerSortReverse"), m_Params.bServerSortReverse);
	ini.WriteBool(_T("SharedSortReverse"), m_Params.bSharedSortReverse);

	ini.WriteInt(_T("DownloadSort"), m_Params.DownloadSort);
	ini.WriteInt(_T("UploadSort"), m_Params.UploadSort);
	ini.WriteInt(_T("QueueSort"), m_Params.QueueSort);
	ini.WriteInt(_T("ServerSort"), m_Params.ServerSort);
	ini.WriteInt(_T("SharedSort"), m_Params.SharedSort);

	if (m_bServerWorking)
		StopSockets();
}


void CWebServer::StoreWebSearchSnapshot(int iSortBy, const CQArray<SearchFileStruct, SearchFileStruct> &searchFileArray)
{
	CSingleLock lock(&m_WebSnapshotCacheLock, TRUE);
	m_iCachedSearchSortBy = iSortBy;
	m_CachedSearchFileArray.Copy(searchFileArray);
	m_bHasCachedSearchSnapshot = true;
}

bool CWebServer::CopyWebSearchSnapshot(int iSortBy, CQArray<SearchFileStruct, SearchFileStruct> &searchFileArray) const
{
	CSingleLock lock(&m_WebSnapshotCacheLock, TRUE);
	if (!m_bHasCachedSearchSnapshot || m_iCachedSearchSortBy != iSortBy)
		return false;
	searchFileArray.Copy(m_CachedSearchFileArray);
	return true;
}

bool CWebServer::BuildWebSearchDownloadLinksFromSnapshot(const CString &strHashes, CString &strLinks) const
{
	strLinks.Empty();
	CQArray<SearchFileStruct, SearchFileStruct> searchFileArray;
	{
		CSingleLock lock(&m_WebSnapshotCacheLock, TRUE);
		if (!m_bHasCachedSearchSnapshot)
			return false;
		searchFileArray.Copy(m_CachedSearchFileArray);
	}

	CString strRemaining(strHashes);
	for (int iPos = 0; iPos >= 0;) {
		CString strHash(strRemaining.Tokenize(_T("|"), iPos));
		strHash.Trim();
		if (strHash.GetLength() != 32)
			continue;

		for (INT_PTR i = 0; i < searchFileArray.GetCount(); ++i) {
			const SearchFileStruct &searchFile = searchFileArray[i];
			if (searchFile.m_strFileHash.CompareNoCase(strHash) != 0 || searchFile.m_uFileSize == 0 || searchFile.m_strFileName.IsEmpty())
				continue;

			CString strLink;
			strLink.Format(_T("ed2k://|file|%s|%I64u|%s|/"), (LPCTSTR)EncodeUrlUtf8(searchFile.m_strFileName), static_cast<uint64>(searchFile.m_uFileSize), (LPCTSTR)searchFile.m_strFileHash);
			if (!strLinks.IsEmpty())
				strLinks += _T("\r\n");
			strLinks += strLink;
			break;
		}
	}
	return !strLinks.IsEmpty();
}

void CWebServer::StoreWebHeaderSnapshot(const WebHeaderSnapshot &snapshot)
{
	CSingleLock lock(&m_WebSnapshotCacheLock, TRUE);
	m_CachedHeaderSnapshot = snapshot;
	m_bHasCachedHeaderSnapshot = true;
}

bool CWebServer::CopyWebHeaderSnapshot(WebHeaderSnapshot &snapshot) const
{
	CSingleLock lock(&m_WebSnapshotCacheLock, TRUE);
	if (!m_bHasCachedHeaderSnapshot)
		return false;
	snapshot = m_CachedHeaderSnapshot;
	return true;
}

void CWebServer::StoreWebServerListSnapshot(const CArray<ServerEntry> &serverArray)
{
	CSingleLock lock(&m_WebSnapshotCacheLock, TRUE);
	m_CachedServerArray.RemoveAll();
	m_CachedServerArray.Append(serverArray);
	m_bHasCachedServerListSnapshot = true;
}

bool CWebServer::CopyWebServerListSnapshot(CArray<ServerEntry> &serverArray) const
{
	CSingleLock lock(&m_WebSnapshotCacheLock, TRUE);
	if (!m_bHasCachedServerListSnapshot)
		return false;
	serverArray.RemoveAll();
	serverArray.Append(m_CachedServerArray);
	return true;
}

void CWebServer::StoreWebTransferSnapshot(int iCategory, const WebTransferSnapshot &snapshot)
{
	CSingleLock lock(&m_WebSnapshotCacheLock, TRUE);
	m_iCachedTransferCategory = iCategory;
	m_CachedTransferSnapshot = snapshot;
	ApplyWebTransferRenamePreviewsLocked(m_CachedTransferSnapshot);
	m_bHasCachedTransferSnapshot = true;
}

bool CWebServer::CopyWebTransferSnapshot(int iCategory, WebTransferSnapshot &snapshot) const
{
	CSingleLock lock(&m_WebSnapshotCacheLock, TRUE);
	if (!m_bHasCachedTransferSnapshot || m_iCachedTransferCategory != iCategory)
		return false;
	snapshot = m_CachedTransferSnapshot;
	ApplyWebTransferRenamePreviewsLocked(snapshot);
	snapshot.bStale = true;
	return true;
}

void CWebServer::RememberWebTransferRenamePreview(const CString &strFileHash, const CString &strFileName)
{
	if (strFileHash.IsEmpty() || strFileName.IsEmpty())
		return;

	CSingleLock lock(&m_WebSnapshotCacheLock, TRUE);
	for (INT_PTR i = m_PendingTransferRenames.GetSize() - 1; i >= 0; --i) {
		if (m_PendingTransferRenames[i].m_strFileHash.CompareNoCase(strFileHash) == 0)
			m_PendingTransferRenames.RemoveAt(i);
	}

	SWebTransferRenamePreview preview;
	preview.m_strFileHash = strFileHash;
	preview.m_strFileName = strFileName;
	preview.m_dwTick = ::GetTickCount();
	m_PendingTransferRenames.Add(preview);
	if (m_bHasCachedTransferSnapshot)
		_ApplyWebTransferRenamePreview(m_CachedTransferSnapshot, strFileHash, strFileName);
}

void CWebServer::ApplyWebTransferRenamePreviews(WebTransferSnapshot &snapshot) const
{
	CSingleLock lock(&m_WebSnapshotCacheLock, TRUE);
	ApplyWebTransferRenamePreviewsLocked(snapshot);
}

void CWebServer::ApplyWebTransferRenamePreviewsLocked(WebTransferSnapshot &snapshot) const
{
	const DWORD dwNow = ::GetTickCount();
	for (INT_PTR i = m_PendingTransferRenames.GetSize() - 1; i >= 0; --i) {
		const SWebTransferRenamePreview &preview = m_PendingTransferRenames[i];
		if (static_cast<DWORD>(dwNow - preview.m_dwTick) > WEB_TRANSFER_RENAME_PREVIEW_TTL_MS) {
			m_PendingTransferRenames.RemoveAt(i);
			continue;
		}
		_ApplyWebTransferRenamePreview(snapshot, preview.m_strFileHash, preview.m_strFileName);
	}
}

void CWebServer::StoreWebSharedFilesSnapshot(const CArray<SharedFiles> &sharedArray)
{
	CSingleLock lock(&m_WebSnapshotCacheLock, TRUE);
	m_CachedSharedArray.RemoveAll();
	m_CachedSharedArray.Append(sharedArray);
	m_bHasCachedSharedFilesSnapshot = true;
}

bool CWebServer::CopyWebSharedFilesSnapshot(CArray<SharedFiles> &sharedArray) const
{
	CSingleLock lock(&m_WebSnapshotCacheLock, TRUE);
	if (!m_bHasCachedSharedFilesSnapshot)
		return false;
	sharedArray.RemoveAll();
	sharedArray.Append(m_CachedSharedArray);
	return true;
}

void CWebServer::StoreWebCommentList(const CString &strFileHash, const CString &strCommentList)
{
	CSingleLock lock(&m_WebSnapshotCacheLock, TRUE);
	m_strCachedCommentFileHash = strFileHash;
	m_strCachedCommentList = strCommentList;
	m_bHasCachedCommentList = !strFileHash.IsEmpty();
}

bool CWebServer::CopyWebCommentList(const CString &strFileHash, CString &strCommentList) const
{
	CSingleLock lock(&m_WebSnapshotCacheLock, TRUE);
	if (!m_bHasCachedCommentList || m_strCachedCommentFileHash.CompareNoCase(strFileHash) != 0)
		return false;
	strCommentList = m_strCachedCommentList;
	return true;
}

void CWebServer::_SaveWIConfigArray(BOOL *array, int size, LPCTSTR key)
{
	CIni ini(thePrefs.GetConfigFile(), _T("WebServer"));
	ini.SerGet(false, array, size, key);
}

bool CWebServer::ReloadTemplates()
{
	//Last-Modified: <day-name>, <day> <month-name> <year> <hour>:<minute>:<second> GMT
	//Day and month names must be 3 English letters, 30 characters total.
	_locale_t locale = _tcreate_locale(LC_TIME, _T("en-US"));
	TCHAR szTime[32];
	time_t t = time(NULL);
	if (!_tcsftime_l(szTime, _countof(szTime), _T("%a, %d %b %Y %H:%M:%S GMT"), gmtime(&t), locale))
		*szTime = _T('\0');
	m_Params.sLastModified = szTime;
	m_Params.sETag = MD5Sum(m_Params.sLastModified).GetHashString();

	const CString &sFile(thePrefs.GetTemplate());
	CStdioFile file;
	if (file.Open(sFile, CFile::modeRead | CFile::shareDenyWrite | CFile::typeText)) {
		CString sAll, sLine;
		while (file.ReadString(sLine))
			sAll.AppendFormat(_T("%s\n"), (LPCTSTR)sLine);
		file.Close();

		const CString &sVersion(_LoadTemplate(sAll, _T("TMPL_VERSION")));
		if (_tstol(sVersion) >= WEB_SERVER_TEMPLATES_VERSION) {
			m_Templates.sHeader = _LoadTemplate(sAll, _T("TMPL_HEADER"));
			m_Templates.sHeaderStylesheet = _LoadTemplate(sAll, _T("TMPL_HEADER_STYLESHEET"));
			m_Templates.sFooter = _LoadTemplate(sAll, _T("TMPL_FOOTER"));
			m_Templates.sServerList = _LoadTemplate(sAll, _T("TMPL_SERVER_LIST"));
			m_Templates.sServerLine = _LoadTemplate(sAll, _T("TMPL_SERVER_LINE"));
			m_Templates.sTransferImages = _LoadTemplate(sAll, _T("TMPL_TRANSFER_IMAGES"));
			m_Templates.sTransferList = _LoadTemplate(sAll, _T("TMPL_TRANSFER_LIST"));
			m_Templates.sTransferDownHeader = _LoadTemplate(sAll, _T("TMPL_TRANSFER_DOWN_HEADER"));
			m_Templates.sTransferDownFooter = _LoadTemplate(sAll, _T("TMPL_TRANSFER_DOWN_FOOTER"));
			m_Templates.sTransferDownLine = _LoadTemplate(sAll, _T("TMPL_TRANSFER_DOWN_LINE"));
			m_Templates.sTransferUpHeader = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_HEADER"));
			m_Templates.sTransferUpFooter = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_FOOTER"));
			m_Templates.sTransferUpLine = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_LINE"));
			m_Templates.sTransferUpQueueShow = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_QUEUE_SHOW"));
			m_Templates.sTransferUpQueueHide = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_QUEUE_HIDE"));
			m_Templates.sTransferUpQueueLine = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_QUEUE_LINE"));
			m_Templates.sTransferUpQueueBannedShow = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_QUEUE_BANNED_SHOW"));
			m_Templates.sTransferUpQueueBannedHide = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_QUEUE_BANNED_HIDE"));
			m_Templates.sTransferUpQueueBannedLine = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_QUEUE_BANNED_LINE"));
			m_Templates.sTransferUpQueueFriendShow = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_QUEUE_FRIEND_SHOW"));
			m_Templates.sTransferUpQueueFriendHide = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_QUEUE_FRIEND_HIDE"));
			m_Templates.sTransferUpQueueFriendLine = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_QUEUE_FRIEND_LINE"));
			m_Templates.sSharedList = _LoadTemplate(sAll, _T("TMPL_SHARED_LIST"));
			m_Templates.sSharedLine = _LoadTemplate(sAll, _T("TMPL_SHARED_LINE"));
			m_Templates.sGraphs = _LoadTemplate(sAll, _T("TMPL_GRAPHS"));
			m_Templates.sLog = _LoadTemplate(sAll, _T("TMPL_LOG"));
			m_Templates.sServerInfo = _LoadTemplate(sAll, _T("TMPL_SERVERINFO"));
			m_Templates.sDebugLog = _LoadTemplate(sAll, _T("TMPL_DEBUGLOG"));
			m_Templates.sStats = _LoadTemplate(sAll, _T("TMPL_STATS"));
			m_Templates.sPreferences = _LoadTemplate(sAll, _T("TMPL_PREFERENCES"));
			m_Templates.sLogin = _LoadTemplate(sAll, _T("TMPL_LOGIN"));
			m_Templates.sAddServerBox = _LoadTemplate(sAll, _T("TMPL_ADDSERVERBOX"));
			m_Templates.sSearch = _LoadTemplate(sAll, _T("TMPL_SEARCH"));
			m_Templates.iProgressbarWidth = (uint16)_tstoi(_LoadTemplate(sAll, _T("PROGRESSBARWIDTH")));
			m_Templates.sSearchHeader = _LoadTemplate(sAll, _T("TMPL_SEARCH_RESULT_HEADER"));
			m_Templates.sSearchResultLine = _LoadTemplate(sAll, _T("TMPL_SEARCH_RESULT_LINE"));
			m_Templates.sProgressbarImgs = _LoadTemplate(sAll, _T("PROGRESSBARIMGS"));
			m_Templates.sProgressbarImgsPercent = _LoadTemplate(sAll, _T("PROGRESSBARPERCENTIMG"));
			m_Templates.sCatArrow = _LoadTemplate(sAll, _T("TMPL_CATARROW"));
			m_Templates.sDownArrow = _LoadTemplate(sAll, _T("TMPL_DOWNARROW"));
			m_Templates.sUpArrow = _LoadTemplate(sAll, _T("TMPL_UPARROW"));
			m_Templates.sDownDoubleArrow = _LoadTemplate(sAll, _T("TMPL_DNDOUBLEARROW"));
			m_Templates.sUpDoubleArrow = _LoadTemplate(sAll, _T("TMPL_UPDOUBLEARROW"));
			m_Templates.sKad = _LoadTemplate(sAll, _T("TMPL_KADDLG"));
			m_Templates.sBootstrapLine = _LoadTemplate(sAll, _T("TMPL_BOOTSTRAPLINE"));
			m_Templates.sMyInfoLog = _LoadTemplate(sAll, _T("TMPL_MYINFO"));
			m_Templates.sCommentList = _LoadTemplate(sAll, _T("TMPL_COMMENTLIST"));
			m_Templates.sCommentListLine = _LoadTemplate(sAll, _T("TMPL_COMMENTLIST_LINE"));

			m_Templates.sProgressbarImgsPercent.Replace(_T("[PROGRESSGIFNAME]"), _T("%s"));
			m_Templates.sProgressbarImgsPercent.Replace(_T("[PROGRESSGIFINTERNAL]"), _T("%i"));
			m_Templates.sProgressbarImgs.Replace(_T("[PROGRESSGIFNAME]"), _T("%s"));
			m_Templates.sProgressbarImgs.Replace(_T("[PROGRESSGIFINTERNAL]"), _T("%i"));
			return true;
		}
		if (thePrefs.GetWSIsEnabled() || m_bServerWorking) {
			CString buffer;
			buffer.Format(GetResString(_T("WS_ERR_LOADTEMPLATE")), (LPCTSTR)sFile);
			AddLogLine(true, (LPCTSTR)EscPercent(buffer));
			CDarkMode::MessageBox(buffer, MB_OK);
			StopServer();
		}
	} else if (m_bServerWorking) {
		AddLogLine(true, GetResString(_T("WEB_ERR_CANTLOAD")), (LPCTSTR)EscPercent(sFile));
		StopServer();
	}
	return false;
}

CString CWebServer::_LoadTemplate(const CString &sAll, const CString &sTemplateName)
{
	int len = sTemplateName.GetLength();
	CString sTemplate;
	sTemplate.Format(_T("<--%s-->"), (LPCTSTR)sTemplateName);
	int nStart = sAll.Find(sTemplate);
	sTemplate.Insert(len + 3, _T("_END"));
	int nEnd = sAll.Find(sTemplate);
	if (nStart >= 0 && nStart < nEnd) {
		nStart += len + 7;
		return sAll.Mid(nStart, nEnd - nStart - 1);
	}
	if (sTemplateName == _T("TMPL_VERSION"))
		AddLogLine(true, (LPCTSTR)GetResString(_T("WS_ERR_LOADTEMPLATE")), (LPCTSTR)EscPercent(sTemplateName));
	if (nStart == -1)
		AddLogLine(false, (LPCTSTR)GetResString(_T("WEB_ERR_CANTLOAD")), (LPCTSTR)EscPercent(sTemplateName));
	return CString();
}

//Cax2 - restarts the server with the new port settings
void CWebServer::RestartSockets()
{
	StopSockets();
	if (m_bServerWorking)
		StartSockets(this);
}

void CWebServer::StartServer()
{
	if (theApp.IsNetworkActivityBlockedByBind())
		return;

	if (m_bServerWorking == thePrefs.GetWSIsEnabled())
		return;
	m_bServerWorking = thePrefs.GetWSIsEnabled();
	if (m_bServerWorking) {
		ReloadTemplates();
		if (m_bServerWorking) {
			StartSockets(this);
			m_nIntruderDetect = 0;
			m_bIsTempDisabled = false;
		}
	} else
		StopSockets();

	const bool bEnabled = thePrefs.GetWSIsEnabled() && m_bServerWorking;
	LPCTSTR statusId = bEnabled ? _T("ENABLED") : _T("DISABLED");
	CString status = _GetPlainResString(statusId);
	status.MakeLower();
	AddLogLine(false, _T("%s: %s%s")
		, (LPCTSTR)_GetPlainResString(_T("PW_WS"))
		, (LPCTSTR)status
		, (LPCTSTR)_WebSelectString(bEnabled && thePrefs.GetWebUseHttps(), _T(" (HTTPS)"), _T("")));
}

void CWebServer::StopServer()
{
	if (m_bServerWorking) {
		StopSockets();
		m_bServerWorking = false;
	}
	thePrefs.SetWSIsEnabled(false);
}

void CWebServer::_RemoveServer(const CString &sIP, int nPort)
{
	ExecuteServerCommandForWebThread(WEB_SERVER_COMMAND_REMOVE, sIP, nPort);
}

void CWebServer::_AddToStatic(const CString &sIP, int nPort)
{
	ExecuteServerCommandForWebThread(WEB_SERVER_COMMAND_ADD_TO_STATIC, sIP, nPort);
}

void CWebServer::_RemoveFromStatic(const CString &sIP, int nPort)
{
	ExecuteServerCommandForWebThread(WEB_SERVER_COMMAND_REMOVE_FROM_STATIC, sIP, nPort);
}


void CWebServer::AddStatsLine(const UpDown &line)
{
	m_Params.PointsForWeb.Add(line);
	if (m_Params.PointsForWeb.GetCount() > WEB_GRAPH_WIDTH)
		m_Params.PointsForWeb.RemoveAt(0);
}

CString CWebServer::_SpecialChars(const CString &cstr, bool noquote /*=false*/)
{
	CString str(cstr);
	str.Replace(_T("&"), _T("&amp;"));
	str.Replace(_T("<"), _T("&lt;"));
	str.Replace(_T(">"), _T("&gt;"));
	str.Replace(_T("\""), _T("&quot;"));
	if (noquote) {
		str.Replace(_T("'"), _T("&#8217;"));
		str.Replace(_T("\n"), _T("\\n"));
	}
	return str;
}

void CWebServer::_ConnectToServer(const CString &sIP, int nPort)
{
	ExecuteServerCommandForWebThread(WEB_SERVER_COMMAND_CONNECT, sIP, nPort);
}

static int NormalizeWebServerPreference(int nPriority)
{
	switch (nPriority) {
	case SRV_PR_LOW:
	case SRV_PR_NORMAL:
	case SRV_PR_HIGH:
		return nPriority;
	default:
		return SRV_PR_NORMAL;
	}
}

static bool ExecuteServerCommandForWebThreadImpl(WebServerCommand eCommand, const CString &sIP, int nPort, const CString &sName, const CString &sDescription, bool bStatic, int nPriority, bool bAddToStatic, bool bConnectNow)
{
	if (theApp.emuledlg == NULL || !::IsWindow(theApp.emuledlg->m_hWnd))
		return false;

	CString strIP(sIP);
	strIP.Trim();

	switch (eCommand) {
	case WEB_SERVER_COMMAND_ADD:
	{
		if (strIP.IsEmpty() || nPort <= 0 || nPort > 65535 || theApp.emuledlg->serverwnd == NULL)
			return false;

		CString strName(sName);
		strName.Trim();
		if (strName.IsEmpty())
			strName = strIP;

		CServer *srv = new CServer((uint16)nPort, strIP);
		srv->SetListName(strName);
		if (!sDescription.IsEmpty())
			srv->SetDescription(sDescription);
		srv->SetPreference(NormalizeWebServerPreference(nPriority));

		if (!theApp.emuledlg->serverwnd->serverlistctrl.AddServer(srv, true)) {
			delete srv;
			return false;
		}

		theApp.emuledlg->serverwnd->serverlistctrl.RefreshServer(srv);
		if (bStatic || bAddToStatic) {
			CServer *server = theApp.serverlist != NULL ? theApp.serverlist->GetServerByAddress(strIP, (uint16)nPort) : NULL;
			if (server != NULL)
				theApp.emuledlg->serverwnd->serverlistctrl.StaticServerFileAppend(server);
		}
		if (bConnectNow) {
			if (theApp.emuledlg != NULL && !theApp.emuledlg->CanUseP2PConnectionCommands())
				theApp.emuledlg->LogP2PConnectionCommandBlocked(false);
			else {
				CServer *server = theApp.serverlist != NULL ? theApp.serverlist->GetServerByAddress(strIP, (uint16)nPort) : NULL;
				if (server != NULL && theApp.serverconnect != NULL)
					theApp.serverconnect->ConnectToServer(server, false, false, true);
			}
		}
		return true;
	}
	case WEB_SERVER_COMMAND_REMOVE:
		if (strIP.IsEmpty() || nPort <= 0 || theApp.emuledlg->serverwnd == NULL)
			return false;
	{
		CServer *server = theApp.serverlist != NULL ? theApp.serverlist->GetServerByAddress(strIP, (uint16)nPort) : NULL;
		if (server == NULL)
			return false;
		theApp.emuledlg->serverwnd->serverlistctrl.RemoveServer(server);
		return true;
	}
	case WEB_SERVER_COMMAND_ADD_TO_STATIC:
		if (strIP.IsEmpty() || nPort <= 0 || theApp.emuledlg->serverwnd == NULL)
			return false;
	{
		CServer *server = theApp.serverlist != NULL ? theApp.serverlist->GetServerByAddress(strIP, (uint16)nPort) : NULL;
		if (server == NULL)
			return false;
		theApp.emuledlg->serverwnd->serverlistctrl.StaticServerFileAppend(server);
		return true;
	}
	case WEB_SERVER_COMMAND_REMOVE_FROM_STATIC:
		if (strIP.IsEmpty() || nPort <= 0 || theApp.emuledlg->serverwnd == NULL)
			return false;
	{
		CServer *server = theApp.serverlist != NULL ? theApp.serverlist->GetServerByAddress(strIP, (uint16)nPort) : NULL;
		if (server == NULL)
			return false;
		theApp.emuledlg->serverwnd->serverlistctrl.StaticServerFileRemove(server);
		return true;
	}
	case WEB_SERVER_COMMAND_SET_PRIORITY:
	{
		if (strIP.IsEmpty() || nPort <= 0 || theApp.serverlist == NULL || theApp.emuledlg->serverwnd == NULL)
			return false;
		CServer *server = theApp.serverlist->GetServerByAddress(strIP, (uint16)nPort);
		if (server == NULL)
			return false;
		server->SetPreference(NormalizeWebServerPreference(nPriority));
		theApp.emuledlg->serverwnd->serverlistctrl.RefreshServer(server);
		return true;
	}
	case WEB_SERVER_COMMAND_CONNECT:
		if (theApp.serverconnect == NULL)
			return false;
		if (theApp.emuledlg != NULL && !theApp.emuledlg->CanUseP2PConnectionCommands()) {
			theApp.emuledlg->LogP2PConnectionCommandBlocked(false);
			return false;
		}
		if (strIP.IsEmpty())
			theApp.serverconnect->ConnectToAnyServer();
		else if (nPort > 0) {
			CServer *server = theApp.serverlist != NULL ? theApp.serverlist->GetServerByAddress(strIP, (uint16)nPort) : NULL;
			if (server == NULL)
				return false;
			theApp.serverconnect->ConnectToServer(server, false, false, true);
		} else
			return false;
		return true;
	case WEB_SERVER_COMMAND_CONNECT_ANY:
		if (theApp.serverconnect == NULL)
			return false;
		if (theApp.emuledlg != NULL && !theApp.emuledlg->CanUseP2PConnectionCommands()) {
			theApp.emuledlg->LogP2PConnectionCommandBlocked(false);
			return false;
		}
		theApp.serverconnect->ConnectToAnyServer();
		return true;
	case WEB_SERVER_COMMAND_STOP_CONNECTING:
		if (theApp.serverconnect == NULL)
			return false;
		theApp.serverconnect->StopConnectionTry();
		return true;
	case WEB_SERVER_COMMAND_DISCONNECT_ED2K:
		if (theApp.serverconnect != NULL && theApp.serverconnect->IsConnecting())
			theApp.serverconnect->StopConnectionTry();
		else if (theApp.serverconnect != NULL)
			theApp.serverconnect->Disconnect();
		else
			return false;
		return true;
	case WEB_SERVER_COMMAND_CONNECT_KAD:
		if (theApp.emuledlg != NULL && !theApp.emuledlg->CanUseP2PConnectionCommands()) {
			theApp.emuledlg->LogP2PConnectionCommandBlocked(false);
			return false;
		}
		Kademlia::CKademlia::Start();
		return true;
	case WEB_SERVER_COMMAND_DISCONNECT_KAD:
		Kademlia::CKademlia::Stop();
		return true;
	case WEB_SERVER_COMMAND_RECHECK_KAD_FIREWALL:
		Kademlia::CKademlia::RecheckFirewalled();
		return true;
		case WEB_SERVER_COMMAND_BOOTSTRAP_KAD:
		{
			if (theApp.emuledlg != NULL && !theApp.emuledlg->CanUseP2PConnectionCommands()) {
				theApp.emuledlg->LogP2PConnectionCommandBlocked(false);
				return false;
			}
			if (strIP.IsEmpty())
				return false;
			CString strEndpoint(strIP);
		uint16 uPort = static_cast<uint16>(nPort > 0 ? nPort : 0);
		if (uPort == 0) {
			const int iSeparator = strEndpoint.Find(_T(':'));
			if (iSeparator < 0)
				return false;
			uPort = static_cast<uint16>(_tstoi(CPTR(strEndpoint, iSeparator + 1)));
			strEndpoint.Truncate(iSeparator);
		}
		if (strEndpoint.IsEmpty() || uPort == 0)
			return false;
			Kademlia::CKademlia::Bootstrap(strEndpoint, uPort);
			return true;
		}
		case WEB_SERVER_COMMAND_UPDATE_SERVER_MET_FROM_URL:
			if (strIP.IsEmpty() || theApp.emuledlg->serverwnd == NULL)
				return false;
			if (theApp.emuledlg != NULL && !theApp.emuledlg->CanUseP2PConnectionCommands()) {
				theApp.emuledlg->LogP2PConnectionCommandBlocked(false);
				return false;
			}
			return theApp.emuledlg->serverwnd->UpdateServerMetFromURL(strIP);
		default:
			return false;
		}
	}

static bool QueueServerCommandForUiThread(WebServerCommand eCommand, const CString &sIP, int nPort, const CString &sName, const CString &sDescription, bool bStatic, int nPriority, bool bAddToStatic, bool bConnectNow)
{
	if (theApp.emuledlg == NULL || !::IsWindow(theApp.emuledlg->m_hWnd))
		return false;
	if (theApp.IsUiThread())
		return ExecuteServerCommandForWebThreadImpl(eCommand, sIP, nPort, sName, sDescription, bStatic, nPriority, bAddToStatic, bConnectNow);

	SWebServerCommandRequest *pRequest = new SWebServerCommandRequest();
	pRequest->m_eCommand = eCommand;
	pRequest->m_strIP = sIP;
	pRequest->m_strName = sName;
	pRequest->m_strServerName = sName;
	pRequest->m_strDescription = sDescription;
	pRequest->m_nPort = nPort;
	pRequest->m_bStatic = bStatic;
	pRequest->m_bAddToStatic = bAddToStatic;
	pRequest->m_bConnectNow = bConnectNow;
	pRequest->m_nPriority = nPriority;
	pRequest->AddRef();
	if (!::PostMessage(theApp.emuledlg->m_hWnd, WEB_SERVER_COMMAND, 0, reinterpret_cast<LPARAM>(pRequest))) {
		pRequest->ReleaseReference();
		pRequest->ReleaseReference();
		return false;
	}
	pRequest->ReleaseReference();
	return true;
}

bool CWebServer::ExecuteServerCommandForWebThread(WebServerCommand eCommand, const CString &sIP, int nPort)
{
	return ExecuteServerCommandForWebThread(eCommand, sIP, nPort, CString(), CString(), false, SRV_PR_NORMAL, false, false);
}

// This overload must stay defined out-of-line because WebServer.cpp queues it by exact symbol name.
bool CWebServer::ExecuteServerCommandForWebThread(WebServerCommand eCommand, const CString &sIP, int nPort, int nPriority)
{
	return ExecuteServerCommandForWebThread(eCommand, sIP, nPort, CString(), CString(), false, nPriority, false, false);
}

bool CWebServer::ExecuteServerCommandForWebThread(WebServerCommand eCommand, const CString &sIP, int nPort, const CString &sName, const CString &sDescription)
{
	return ExecuteServerCommandForWebThread(eCommand, sIP, nPort, sName, sDescription, false, SRV_PR_NORMAL, false, false);
}

bool CWebServer::ExecuteServerCommandForWebThread(WebServerCommand eCommand, const CString &sIP, int nPort, const CString &sName, const CString &sDescription, bool bStatic)
{
	return ExecuteServerCommandForWebThread(eCommand, sIP, nPort, sName, sDescription, bStatic, SRV_PR_NORMAL, false, false);
}

bool CWebServer::ExecuteServerCommandForWebThread(WebServerCommand eCommand, const CString &sIP, int nPort, const CString &sName, const CString &sDescription, bool bStatic, int nPriority)
{
	return ExecuteServerCommandForWebThread(eCommand, sIP, nPort, sName, sDescription, bStatic, nPriority, false, false);
}

bool CWebServer::ExecuteServerCommandForWebThread(WebServerCommand eCommand, const CString &sIP, int nPort, int nPriority, const CString &sName, bool bAddToStatic, bool bConnectNow)
{
	return ExecuteServerCommandForWebThread(eCommand, sIP, nPort, sName, CString(), false, nPriority, bAddToStatic, bConnectNow);
}

bool CWebServer::ExecuteServerCommandForWebThread(WebServerCommand eCommand, const CString &sIP, int nPort, const CString &sName, const CString &sDescription, bool bStatic, int nPriority, bool bAddToStatic, bool bConnectNow)
{
	return QueueServerCommandForUiThread(eCommand, sIP, nPort, sName, sDescription, bStatic, nPriority, bAddToStatic, bConnectNow);
}

void CWebServer::_ProcessURL(const ThreadData &Data)
{
	if (!theApp.IsRunning())
		return;

	CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return;

	//(0.29b)//////////////////////////////////////////////////////////////////
	// Here we are in real trouble! We are accessing the entire emule main thread
	// data without any synchronization!! Either we use the message pump for m_pdlgEmule
	// or use some hundreds of critical sections... For now, an exception handler
	// should avoid the worse things.
	//////////////////////////////////////////////////////////////////////////
	(void)CoInitialize(NULL);

#ifndef _DEBUG
	try {
#endif
		bool isUseGzip = thePrefs.GetWebUseGzip();

		srand((unsigned)time(NULL));

		uint32 myip = inet_addr(ipstrA(Data.inadr));

		// check for being banned
		int myfaults = 0;
		const DWORD curTick = ::GetTickCount();
		for (INT_PTR i = pThis->m_Params.badlogins.GetCount(); --i >= 0;) {
			if (curTick >= pThis->m_Params.badlogins[i].timestamp + MIN2MS(15))
				pThis->m_Params.badlogins.RemoveAt(i); // remove outdated entries
			else
				if (pThis->m_Params.badlogins[i].datalen == myip)
					++myfaults;
		}
		if (myfaults > 4) {
			Data.pSocket->SendContent(HTTPInit, _GetPlainResString(_T("SFS_ACCESS_DENIED")));
			CoUninitialize();
			return;
		}

		bool justAddLink = false;
		bool login = false;
		CString sSession(_ParseURL(Data.sURL, _T("ses")));
		long lSession = _tstol(sSession);

		if (_ParseURL(Data.sURL, _T("w")) == _T("password")) {
			const CString &ip(ipstr(Data.inadr));

			if (!_ParseURL(Data.sURL, _T("c")).IsEmpty())
				// just sent password to add link remotely. Don't start a session.
				justAddLink = true;

			if (MD5Sum(_ParseURL(Data.sURL, _T("p"))).GetHashString() == thePrefs.GetWSPass()) {
				if (!justAddLink) {
					// user wants to login
					Session ses;
					ses.admin = true;
					ses.startTime = CTime::GetCurrentTime();
					ses.lSession = lSession = (long)(rand() >> 1);
					ses.lastcat = -thePrefs.GetCatFilter(0);
					pThis->m_Params.Sessions.Add(ses);
				}

				::PostMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_UPDATEMYINFO, 0);

				AddLogLine(true, GetResString(_T("WEB_ADMINLOGIN")) + _T(" (%s)"), (LPCTSTR)ip);
				login = true;
			} else if (thePrefs.GetWSIsLowUserEnabled() && !thePrefs.GetWSLowPass().IsEmpty() && MD5Sum(_ParseURL(Data.sURL, _T("p"))).GetHashString() == thePrefs.GetWSLowPass()) {
				Session ses;
				ses.admin = false;
				ses.startTime = CTime::GetCurrentTime();
				ses.lSession = lSession = (long)(rand() >> 1);
				pThis->m_Params.Sessions.Add(ses);

				::PostMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_UPDATEMYINFO, 0);

				AddLogLine(true, GetResString(_T("WEB_GUESTLOGIN")) + _T(" (%s)"), (LPCTSTR)ip);
				login = true;
			} else {
				LogWarning(LOG_STATUSBAR, GetResString(_T("WEB_BADLOGINATTEMPT")) + _T(" (%s)"), (LPCTSTR)ip);

				BadLogin newban = {myip, curTick};	// save failed attempt (ip,time)
				pThis->m_Params.badlogins.Add(newban);
				if (++myfaults > 4) {
					Data.pSocket->SendContent(HTTPInit, _GetPlainResString(_T("SFS_ACCESS_DENIED")));
					CoUninitialize();
					return;
				}
			}
			isUseGzip = false; // [Julien]
			if (login)	// on login, forget previous failed attempts
				for (INT_PTR i = pThis->m_Params.badlogins.GetCount(); --i >= 0;)
					if (pThis->m_Params.badlogins[i].datalen == myip)
						pThis->m_Params.badlogins.RemoveAt(i);
		}

		sSession.Format(_T("%ld"), lSession);
		if (_ParseURL(Data.sURL, _T("w")) == _T("logout"))
			_RemoveSession(Data, lSession);

		TCHAR *gzipOut = NULL;
		DWORD gzipLen = 0;
		CString Out;
		if (_IsLoggedIn(Data, lSession)) {
			bool bAdmin = _IsSessionAdmin(Data, sSession);
			if (_ParseURL(Data.sURL, _T("w")) == _T("close") && bAdmin && thePrefs.GetWebAdminAllowedHiLevFunc()) {
				theApp.BeginBackendShutdownLifecycle(_T("webserver-close"));
				_RemoveSession(Data, lSession);

				// send answer...
				Out += _GetLoginScreen(Data);
				Data.pSocket->SendContent(HTTPInit, Out);

				::PostMessage(theApp.emuledlg->m_hWnd, WM_CLOSE, 0, 0);

				CoUninitialize();
				return;
			}

			if (_ParseURL(Data.sURL, _T("w")) == _T("shutdown") && bAdmin) {
				_RemoveSession(Data, lSession);
				// send answer...
				Out += _GetLoginScreen(Data);
				Data.pSocket->SendContent(HTTPInit, Out);

				::PostMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_WINFUNC, 1);

				CoUninitialize();
				return;
			}

			if (_ParseURL(Data.sURL, _T("w")) == _T("reboot") && bAdmin) {
				_RemoveSession(Data, lSession);

				// send answer...
				Out += _GetLoginScreen(Data);
				Data.pSocket->SendContent(HTTPInit, Out);

				::PostMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_WINFUNC, 2);

				CoUninitialize();
				return;
			}

			if (_ParseURL(Data.sURL, _T("w")) == _T("commentlist")) {
				const CString &Out1(_GetCommentlist(Data));

				if (!Out1.IsEmpty()) {
					Data.pSocket->SendContent(HTTPInit, Out1);

					CoUninitialize();
					return;
				}
			} else if (_ParseURL(Data.sURL, _T("w")) == _T("getfile") && bAdmin) {
				WebSharedFileDownloadInfo fileInfo;
				if (GetWebSharedFileDownloadInfo(_ParseURL(Data.sURL, _T("filehash")), fileInfo) && fileInfo.bFound) {
					if (thePrefs.GetMaxWebUploadFileSizeMB() != 0 && fileInfo.qwFileSize > (uint64)thePrefs.GetMaxWebUploadFileSizeMB() * 1024 * 1024) {
						Data.pSocket->SendReply("HTTP/1.1 403 Forbidden\r\n");

						CoUninitialize();
						return;
					}

					CFile file;
					if (file.Open(fileInfo.sFilePath, CFile::modeRead | CFile::shareDenyWrite | CFile::typeBinary)) {
						uint64 qwRemaining = fileInfo.qwFileSize;

#define SENDFILEBUFSIZE 2048
						char *buffer = (char*)malloc(SENDFILEBUFSIZE);
						if (!buffer) {
							Data.pSocket->SendReply("HTTP/1.1 500 Internal Server Error\r\n");
							CoUninitialize();
							return;
						}

						CStringA fname(fileInfo.sFileName);
						CStringA szBuf;
						szBuf.Format("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
							"Content-Description: \"%s\"\r\n"
							"Content-Disposition: attachment; filename=\"%s\";\r\n"
							"Content-Transfer-Encoding: binary\r\n"
							"Content-Length: %I64u\r\n\r\n"
							, (LPCSTR)fname, (LPCSTR)fname, fileInfo.qwFileSize);
						Data.pSocket->SendData(szBuf, szBuf.GetLength());

						for (UINT r = 1; qwRemaining > 0 && r;) {
							const UINT uReadSize = static_cast<UINT>((qwRemaining < SENDFILEBUFSIZE) ? qwRemaining : SENDFILEBUFSIZE);
							r = file.Read(buffer, uReadSize);
							qwRemaining -= r;
							Data.pSocket->SendData(buffer, r);
						}
						file.Close();

						free(buffer);
					} else
						Data.pSocket->SendReply("HTTP/1.1 404 File not found\r\n");
					CoUninitialize();
					return;
				}
			}

			Out += _GetHeader(Data, lSession);
			CString sPage(_ParseURL(Data.sURL, _T("w")));
			if (sPage.IsEmpty() && _IsTransferCommandRequest(Data.sURL))
				sPage = _T("transfer");
			if (sPage == _T("server"))
				Out += _GetServerList(Data);
			else if (sPage == _T("shared"))
				Out += _GetSharedFilesList(Data);
			else if (sPage == _T("transfer"))
				Out += _GetTransferList(Data);
			else if (sPage == _T("search"))
				Out += _GetSearch(Data);
			else if (sPage == _T("graphs"))
				Out += _GetGraphs(Data);
			else if (sPage == _T("log"))
				Out += _GetLog(Data);
			else if (sPage == _T("sinfo"))
				Out += _GetServerInfo(Data);
			else if (sPage == _T("debuglog"))
				Out += _GetDebugLog(Data);
			else if (sPage == _T("myinfo"))
				Out += _GetMyInfo(Data);
			else if (sPage == _T("stats"))
				Out += _GetStats(Data);
			else if (sPage == _T("kad"))
				Out += _GetKadDlg(Data);
			else if (sPage == _T("options")) {
				isUseGzip = false;
				Out += _GetPreferences(Data);
			} else if (sPage.IsEmpty())
				isUseGzip = false;

			Out += _GetFooter(Data);

			if (isUseGzip) {
				bool bOk = false;
				try {
					CStringA strA(wc2utf8(Out));
					uLongf destLen = strA.GetLength() + 1024;
					gzipOut = new TCHAR[destLen];
					if (_GzipCompress((Bytef*)gzipOut, &destLen, (Bytef*)(LPCSTR)strA, strA.GetLength(), Z_DEFAULT_COMPRESSION) == Z_OK) {
						bOk = true;
						gzipLen = destLen;
					}
				} catch (...) {
					ASSERT(0);
				}
				if (!bOk) {
					isUseGzip = false;
					delete[] gzipOut;
					gzipOut = NULL;
				}
			}
		} else if (justAddLink && login)
			Out += _GetRemoteLinkAddedOk(Data);
		else {
			isUseGzip = false;
			Out += justAddLink ? _GetRemoteLinkAddedFailed(Data) : _GetLoginScreen(Data);
		}

		CStringA strHttpInit(HTTPInit);
		if (_IsLoggedIn(Data, lSession) && _ParseURL(Data.sURL, _T("w")) == _T("server")) {
			WebHeaderSnapshot headerSnapshot;
			GetWebHeaderSnapshot(pThis, headerSnapshot);
			const CString &sCmd(_ParseURL(Data.sURL, _T("c")));
			const UINT uRefreshRound = static_cast<UINT>(_tstol(_ParseURL(Data.sURL, _T("wr"))));
			const CString strRefreshStartTick(_ParseURL(Data.sURL, _T("wrt")));
			const DWORD dwNow = ::GetTickCount();
			const DWORD dwRefreshStartTick = strRefreshStartTick.IsEmpty() ? dwNow : static_cast<DWORD>(_tcstoul(strRefreshStartTick, NULL, 10));
			const bool bServerCommandIssued = sCmd == _T("connect") || sCmd == _T("disconnect");
			const bool bWithinConnectionTimeout = headerSnapshot.dwConnectionTimeout != 0 && static_cast<DWORD>(dwNow - dwRefreshStartTick) < headerSnapshot.dwConnectionTimeout;
			const bool bContinueServerRefresh = !headerSnapshot.bServerConnected && headerSnapshot.bServerConnecting && headerSnapshot.bActiveConnectionAttempt && bWithinConnectionTimeout;
			if (bServerCommandIssued || bContinueServerRefresh) {
				CString strTarget;
				strTarget.Format(_T("?ses=%s&w=server"), (LPCTSTR)sSession);
				const CString &strCat(_ParseURL(Data.sURL, _T("cat")));
				if (!strCat.IsEmpty())
					strTarget.AppendFormat(_T("&cat=%s"), (LPCTSTR)strCat);
				strTarget.AppendFormat(_T("&wr=%u&wrt=%u&dummy=%u"), uRefreshRound + 1U, dwRefreshStartTick, static_cast<UINT>(rand()));
				const CStringA strTargetA(wc2utf8(strTarget));
				strHttpInit.AppendFormat("Refresh: 7.5; url=%s\r\n", (LPCSTR)strTargetA);
			}
		}

		CStringA strHttpInitGZ(strHttpInit);
		strHttpInitGZ += "Content-Encoding: gzip\r\n";

		// send answer...
		if (!isUseGzip)
			Data.pSocket->SendContent(strHttpInit, Out);
		else
			Data.pSocket->SendContent(strHttpInitGZ, gzipOut, gzipLen);

		delete[] gzipOut;

#ifndef _DEBUG
	} catch (...) {
		AddDebugLogLine(DLP_VERYHIGH, false, _T("*** Unknown exception in CWebServer::ProcessURL"));
		ASSERT(0);
	}
#endif

	CoUninitialize();
}

CString CWebServer::_ParseURLArray(CString URL, CString fieldname)
{
	URL.MakeLower();
	fieldname.MakeLower();
	CString res;
	while (!URL.IsEmpty()) {
		int pos = URL.Find(fieldname + _T('='));
		if (pos < 0)
			break;
		const CString &temp(_ParseURL(URL, fieldname));
		if (temp.IsEmpty())
			break;
		res.AppendFormat(_T("%s|"), (LPCTSTR)temp);
		URL.Delete(pos, 10);
	}
	return res;
}

CString CWebServer::_ParseURL(const CString &URL, const CString &fieldname)
{
	int findPos = URL.Find(_T('?'));
	if (findPos >= 0) {
		CString Parameter(URL.Mid(findPos + 1, URL.GetLength() - findPos - 1));
		Parameter.Replace(_T("&amp;"), _T("&"));
		Parameter.Replace(_T("&AMP;"), _T("&"));

		int findLength;
		// search the field name beginning / middle and strip the rest...
		findPos = Parameter.Find(fieldname + _T('='));
		findLength = !findPos ? fieldname.GetLength() + 1 : 0;
		int iPos = Parameter.Find(_T('&') + fieldname + _T('='));
		if (iPos >= 0) {
			findPos = iPos;
			findLength = fieldname.GetLength() + 2;
		}
		if (findPos >= 0) {
			Parameter.Delete(0, findPos + findLength);
			iPos = Parameter.Find(_T('&'));
			if (iPos >= 0)
				Parameter.Truncate(iPos);
			Parameter.Replace(_T('+'), _T(' '));
			// decode value...
			return OptUtf8ToStr(URLDecode(Parameter, true));
		}
	}
	return CString();
}

CString CWebServer::_GetHeader(const ThreadData &Data, long lSession)
{
	CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	CString sSession;
	sSession.Format(_T("%ld"), lSession);

	CString Out(pThis->m_Templates.sHeader);
	Out.Replace(_T("[CharSet]"), HTTPENCODING);

	//	Auto-refresh code
	const CString &sPage(_ParseURL(Data.sURL, _T("w")));
	bool bAdmin = _IsSessionAdmin(Data, lSession);

	CString sRefresh;
	if (sPage == _T("options") || sPage == _T("stats") || sPage == _T("password"))
		sRefresh += _T('0');
	else
		sRefresh.Format(_T("%d"), SEC2MS(thePrefs.GetWebPageRefresh()));

	CString swCommand(_ParseURL(Data.sURL, _T("w")));
	swCommand.AppendFormat(_T("&amp;cat=%s&amp;dummy=%d"), (LPCTSTR)_ParseURL(Data.sURL, _T("cat")), rand());

	Out.Replace(_T("[admin]"), _WebSelectString(bAdmin && thePrefs.GetWebAdminAllowedHiLevFunc(), _T("admin"), _T("")));
	Out.Replace(_T("[Session]"), sSession);
	Out.Replace(_T("[RefreshVal]"), sRefresh);
	Out.Replace(_T("[wCommand]"), swCommand);
	Out.Replace(_T("[eMuleAppName]"), _T("eMule"));
	Out.Replace(_T("[version]"), theApp.GetAppVersion().Mid(6));
	Out.Replace(_T("[StyleSheet]"), pThis->m_Templates.sHeaderStylesheet);
	Out.Replace(_T("[WebControl]"), _GetPlainResString(_T("PW_WS")));
	Out.Replace(_T("[Transfer]"), _GetPlainResString(_T("CD_TRANS")));
	Out.Replace(_T("[Server]"), _GetPlainResString(_T("FSTAT_SERVERS")));
	Out.Replace(_T("[Shared]"), _GetPlainResString(_T("SF_FILES")));
	Out.Replace(_T("[Graphs]"), _GetPlainResString(_T("GRAPHS")));
	Out.Replace(_T("[Log]"), _GetPlainResString(_T("SV_LOG")));
	Out.Replace(_T("[ServerInfo]"), _GetPlainResString(_T("SV_SERVERINFO")));
	Out.Replace(_T("[DebugLog]"), _GetPlainResString(_T("SV_DEBUGLOG")));
	Out.Replace(_T("[MyInfo]"), _GetPlainResString(_T("MYINFO")));
	Out.Replace(_T("[Stats]"), _GetPlainResString(_T("SF_STATISTICS")));
	Out.Replace(_T("[Options]"), _GetPlainResString(_T("OPTIONS")));
	Out.Replace(_T("[Ed2klink]"), _GetPlainResString(_T("SW_LINK")));
	Out.Replace(_T("[Close]"), _GetPlainResString(_T("WEB_SHUTDOWN")));
	Out.Replace(_T("[Reboot]"), _GetPlainResString(_T("WEB_REBOOT")));
	Out.Replace(_T("[Shutdown]"), _GetPlainResString(_T("WEB_SHUTDOWNSYSTEM")));
	Out.Replace(_T("[WebOptions]"), _GetPlainResString(_T("WEB_ADMINMENU")));
	Out.Replace(_T("[Logout]"), _GetPlainResString(_T("WEB_LOGOUT")));
	Out.Replace(_T("[Search]"), _GetPlainResString(_T("SW_SEARCHBOX")));
	Out.Replace(_T("[Download]"), _GetPlainResString(_T("DOWNLOAD")));
	Out.Replace(_T("[Start]"), _GetPlainResString(_T("START_NOUN")));
	Out.Replace(_T("[Version]"), _GetPlainResString(_T("VERSION")));
	Out.Replace(_T("[VersionCheck]"), thePrefs.GetVersionCheckURL());
	Out.Replace(_T("[Kad]"), _GetPlainResString(_T("KADEMLIA")));

	Out.Replace(_T("[FileIsHashing]"), _GetPlainResString(_T("HASHING")));
	Out.Replace(_T("[FileIsErroneous]"), _GetPlainResString(_T("ERRORLIKE")));
	Out.Replace(_T("[FileIsCompleting]"), _GetPlainResString(_T("COMPLETING")));
	Out.Replace(_T("[FileDetails]"), _GetPlainResString(_T("FD_TITLE")));
	Out.Replace(_T("[FileComments]"), _GetPlainResString(_T("COMMENT")));
	Out.Replace(_T("[ClearCompleted]"), _GetPlainResString(_T("DL_CLEAR")));
	Out.Replace(_T("[RunFile]"), _GetPlainResString(_T("DOWNLOAD")));
	Out.Replace(_T("[Resume]"), _GetPlainResString(_T("DL_RESUME")));
	Out.Replace(_T("[Stop]"), _GetPlainResString(_T("DL_STOP")));
	Out.Replace(_T("[Pause]"), _GetPlainResString(_T("DL_PAUSE")));
	Out.Replace(_T("[ConfirmCancel]"), _GetPlainResString(_T("Q_CANCELDL2")));
	Out.Replace(_T("[Cancel]"), _GetPlainResString(_T("CANCEL")));
	Out.Replace(_T("[GetFLC]"), _GetPlainResString(_T("DOWNLOADMOVIECHUNKS")));
	Out.Replace(_T("[Rename]"), _GetPlainResString(_T("RENAME")));
	Out.Replace(_T("[Connect]"), _GetPlainResString(_T("IRC_CONNECT")));
	Out.Replace(_T("[ConfirmRemove]"), _GetPlainResString(_T("WEB_CONFIRM_REMOVE_SERVER")));
	Out.Replace(_T("[ConfirmClose]"), _GetPlainResString(_T("MAIN_EXIT")));
	Out.Replace(_T("[ConfirmReboot]"), _GetPlainResString(_T("WEB_MAIN_REBOOT")));
	Out.Replace(_T("[ConfirmShutdown]"), _GetPlainResString(_T("WEB_MAIN_SHUTDOWN")));
	Out.Replace(_T("[RemoveServer]"), _GetPlainResString(_T("REMOVE")));
	Out.Replace(_T("[StaticServer]"), _GetPlainResString(_T("STATICSERVER")));
	Out.Replace(_T("[Friend]"), _GetPlainResString(_T("CW_FRIENDS")));

	Out.Replace(_T("[PriorityVeryLow]"), _GetPlainResString(_T("PRIOVERYLOW")));
	Out.Replace(_T("[PriorityLow]"), _GetPlainResString(_T("PRIOLOW")));
	Out.Replace(_T("[PriorityNormal]"), _GetPlainResString(_T("PRIONORMAL")));
	Out.Replace(_T("[PriorityHigh]"), _GetPlainResString(_T("PRIOHIGH")));
	Out.Replace(_T("[PriorityRelease]"), _GetPlainResString(_T("PRIORELEASE")));
	Out.Replace(_T("[PriorityAuto]"), _GetPlainResString(_T("PRIOAUTO")));

	WebHeaderSnapshot headerSnapshot;
	GetWebHeaderSnapshot(pThis, headerSnapshot);

	CString HTTPHelpU(_T('0'));
	CString HTTPHelpM(_T('0'));
	CString HTTPHelpV(_T('0'));
	CString HTTPHelpF(_T('0'));
	const CString &sCmd(_ParseURL(Data.sURL, _T("c")));
	bool disconnectissued = (sCmd == _T("disconnect"));
	bool connectissued = (sCmd == _T("connect"));
	const bool bActiveConnectionAttempt = headerSnapshot.bActiveConnectionAttempt;

	CString HTTPConState, HTTPConText, HTTPHelp;
	if ((bActiveConnectionAttempt && !disconnectissued) || connectissued) {
		HTTPConState = _T("connecting");
		HTTPConText = _GetPlainResString(_T("CONNECTING"));
	} else if (headerSnapshot.bServerConnected && !disconnectissued) {
		HTTPConState = headerSnapshot.bServerLowId ? _T("low") : _T("high");
		HTTPConText = headerSnapshot.sCurrentServerName;
		if (HTTPConText.IsEmpty())
			HTTPConText = _GetPlainResString(_T("CONNECTED"));
		else {
			if (HTTPConText.GetLength() > SHORT_LENGTH)
				HTTPConText = HTTPConText.Left(SHORT_LENGTH - 3) + _T("...");

			if (bAdmin)
				HTTPConText.AppendFormat(_T(" (<a href=\"?ses=%s&amp;w=server&amp;c=disconnect\">%s</a>)"), (LPCTSTR)sSession, (LPCTSTR)_GetPlainResString(_T("IRC_DISCONNECT")));

			HTTPHelpU = CastItoIShort(headerSnapshot.nCurrentServerUsers);
			HTTPHelpM = CastItoIShort(headerSnapshot.nCurrentServerMaxUsers);
			HTTPHelpF = CastItoIShort(headerSnapshot.nCurrentServerFiles);
			if (headerSnapshot.nCurrentServerMaxUsers > 0)
				HTTPHelpV.Format(_T("%.0f"), (100.0 * headerSnapshot.nCurrentServerUsers) / headerSnapshot.nCurrentServerMaxUsers);
			else
				HTTPHelpV = _T("0");
		}

	} else {
		HTTPConState = _T("disconnected");
		HTTPConText = _GetPlainResString(_T("DISCONNECTED"));
		if (bAdmin)
			HTTPConText.AppendFormat(_T(" (<a href=\"?ses=%s&amp;w=server&amp;c=connect\">%s</a>)"), (LPCTSTR)sSession, (LPCTSTR)_GetPlainResString(_T("CONNECTTOANYSERVER")));
	}
	Out.Replace(_T("[AllUsers]"), CastItoIShort(headerSnapshot.nAllUsers));
	Out.Replace(_T("[AllFiles]"), CastItoIShort(headerSnapshot.nAllFiles));
	Out.Replace(_T("[ConState]"), HTTPConState);
	Out.Replace(_T("[ConText]"), HTTPConText);

	// kad status
	if (headerSnapshot.bKadConnected) {
		if (headerSnapshot.bKadFirewalled) {
			HTTPConText = GetResString(_T("FIREWALLED"));
			HTTPConText.AppendFormat(_T(" (<a href=\"?ses=%s&amp;w=kad&amp;c=rcfirewall\">%s</a>"), (LPCTSTR)sSession, (LPCTSTR)GetResString(_T("KAD_RECHECKFW")));
			HTTPConText.AppendFormat(_T(", <a href=\"?ses=%s&amp;w=kad&amp;c=disconnect\">%s</a>)"), (LPCTSTR)sSession, (LPCTSTR)GetResString(_T("IRC_DISCONNECT")));
		} else {
			HTTPConText = GetResString(_T("CONNECTED"));
			HTTPConText.AppendFormat(_T(" (<a href=\"?ses=%s&amp;w=kad&amp;c=disconnect\">%s</a>)"), (LPCTSTR)sSession, (LPCTSTR)GetResString(_T("IRC_DISCONNECT")));
		}
	} else {
		if (headerSnapshot.bKadRunning)
			HTTPConText = GetResString(_T("CONNECTING"));
		else {
			HTTPConText = GetResString(_T("DISCONNECTED"));
			HTTPConText.AppendFormat(_T(" (<a href=\"?ses=%s&amp;w=kad&amp;c=connect\">%s</a>)"), (LPCTSTR)sSession, (LPCTSTR)GetResString(_T("IRC_CONNECT")));
		}
	}
	Out.Replace(_T("[KadConText]"), HTTPConText);

	TCHAR HTTPHeader[100];
	//100/1024 equals to 1/10.24
	if (thePrefs.GetMaxDownload() == UNLIMITED)
		_stprintf(HTTPHeader, _T("%.0f"), headerSnapshot.dDownloadDatarate / 10.24 / thePrefs.GetMaxGraphDownloadRate());
	else
		_stprintf(HTTPHeader, _T("%.0f"), headerSnapshot.dDownloadDatarate / 10.24 / thePrefs.GetMaxDownload());
	Out.Replace(_T("[DownloadValue]"), HTTPHeader);

	if (thePrefs.GetMaxUpload() == UNLIMITED)
		_stprintf(HTTPHeader, _T("%.0f"), headerSnapshot.dUploadDatarate / 10.24 / thePrefs.GetMaxGraphUploadRate(true));
	else
		_stprintf(HTTPHeader, _T("%.0f"), headerSnapshot.dUploadDatarate / 10.24 / thePrefs.GetMaxUpload());
	Out.Replace(_T("[UploadValue]"), HTTPHeader);

	_stprintf(HTTPHeader, _T("%.0f"), (100.0 * headerSnapshot.nOpenSockets) / thePrefs.GetMaxConnections());
	Out.Replace(_T("[ConnectionValue]"), HTTPHeader);
	_stprintf(HTTPHeader, _T("%.1f"), headerSnapshot.dUploadDatarate / 1024.0);
	Out.Replace(_T("[CurUpload]"), HTTPHeader);
	_stprintf(HTTPHeader, _T("%.1f"), headerSnapshot.dDownloadDatarate / 1024.0);
	Out.Replace(_T("[CurDownload]"), HTTPHeader);
	_stprintf(HTTPHeader, _T("%u.0"), headerSnapshot.nOpenSockets);
	Out.Replace(_T("[CurConnection]"), HTTPHeader);

	uint32 dwMax = thePrefs.GetMaxUpload();
	if (dwMax == UNLIMITED)
		HTTPHelp = GetResString(_T("PW_UNLIMITED"));
	else
		HTTPHelp.Format(_T("%u"), dwMax);
	Out.Replace(_T("[MaxUpload]"), HTTPHelp);

	dwMax = thePrefs.GetMaxDownload();
	if (dwMax == UNLIMITED)
		HTTPHelp = GetResString(_T("PW_UNLIMITED"));
	else
		HTTPHelp.Format(_T("%u"), dwMax);
	Out.Replace(_T("[MaxDownload]"), HTTPHelp);

	dwMax = thePrefs.GetMaxConnections();
	if (dwMax == UNLIMITED)
		HTTPHelp = GetResString(_T("PW_UNLIMITED"));
	else
		HTTPHelp.Format(_T("%u"), dwMax);
	Out.Replace(_T("[MaxConnection]"), HTTPHelp);
	Out.Replace(_T("[UserValue]"), HTTPHelpV);
	Out.Replace(_T("[MaxUsers]"), HTTPHelpM);
	Out.Replace(_T("[CurUsers]"), HTTPHelpU);
	Out.Replace(_T("[CurFiles]"), HTTPHelpF);
	Out.Replace(_T("[Connection]"), _GetPlainResString(_T("CONNECTION")));
	Out.Replace(_T("[QuickStats]"), _GetPlainResString(_T("STATUS")));

	Out.Replace(_T("[Users]"), _GetPlainResString(_T("UUSERS")));
	Out.Replace(_T("[Files]"), _GetPlainResString(_T("FILES")));
	Out.Replace(_T("[Con]"), _GetPlainResString(_T("SP_ACTCON")));
	Out.Replace(_T("[Up]"), _GetPlainResString(_T("PW_CON_UPLBL")));
	Out.Replace(_T("[Down]"), _GetPlainResString(_T("DOWNLOAD")));

	if (thePrefs.GetCatCount() > 1)
		_InsertCatBox(Out, 0, pThis->m_Templates.sCatArrow, false, false, sSession, _T(""), true);
	else
		Out.Replace(_T("[CATBOXED2K]"), _T(""));

	return Out;
}

const CString CWebServer::_GetFooter(const ThreadData &Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	return (pThis == NULL) ? CString() : pThis->m_Templates.sFooter;
}

bool CWebServer::BuildHeaderSnapshotForWebThread(WebHeaderSnapshot &snapshot)
{
	snapshot = WebHeaderSnapshot();
	if (theApp.serverconnect != NULL) {
		snapshot.bServerConnected = theApp.serverconnect->IsConnected();
		snapshot.bServerConnecting = theApp.serverconnect->IsConnecting();
		snapshot.bActiveConnectionAttempt = theApp.serverconnect->HasActiveConnectionAttempts();
		snapshot.bServerLowId = snapshot.bServerConnected && theApp.serverconnect->IsLowID();
		snapshot.dwConnectionTimeout = theApp.serverconnect->GetConnectionAttemptTimeoutMs();

		const CServer *pCurrentServer = theApp.serverconnect->GetCurrentServer();
		if (pCurrentServer != NULL) {
			const CServer *pServerDetails = pCurrentServer;
			if (theApp.serverlist != NULL) {
				CServer *pListedServer = theApp.serverlist->GetServerByAddress(pCurrentServer->GetAddress(), pCurrentServer->GetPort());
				if (pListedServer != NULL)
					pServerDetails = pListedServer;
			}
			snapshot.sCurrentServerName = pServerDetails->GetListName();
			snapshot.nCurrentServerUsers = pServerDetails->GetUsers();
			snapshot.nCurrentServerMaxUsers = pServerDetails->GetMaxUsers();
			snapshot.nCurrentServerFiles = pServerDetails->GetFiles();
		}
	}

	if (theApp.serverlist != NULL) {
		for (INT_PTR sc = theApp.serverlist->GetServerCount(); --sc >= 0;) {
			const CServer *cur_server = theApp.serverlist->GetServerAt(sc);
			if (cur_server == NULL)
				continue;
			snapshot.nAllUsers += cur_server->GetUsers();
			snapshot.nAllFiles += cur_server->GetFiles();
		}
	}

	UINT uDisplayedUploadDatarate = 0;
	UINT uDisplayedDownloadDatarate = 0;
	theApp.GetDisplayedTransferRates(uDisplayedUploadDatarate, uDisplayedDownloadDatarate);
	snapshot.dDownloadDatarate = uDisplayedDownloadDatarate;
	snapshot.dUploadDatarate = uDisplayedUploadDatarate;
	if (theApp.listensocket != NULL)
		snapshot.nOpenSockets = theApp.listensocket->GetOpenSockets();

	snapshot.bKadConnected = Kademlia::CKademlia::IsConnected();
	snapshot.bKadFirewalled = snapshot.bKadConnected && Kademlia::CKademlia::IsFirewalled();
	snapshot.bKadRunning = Kademlia::CKademlia::IsRunning();
	return true;
}

bool CWebServer::BuildSharedFileDownloadInfoForWebThread(const CString &strFileHash, WebSharedFileDownloadInfo &info)
{
	info = WebSharedFileDownloadInfo();
	if (strFileHash.IsEmpty() || theApp.sharedfiles == NULL)
		return false;

	CSharedFileList::SWebSharedFileSnapshot snapshot;
	if (!theApp.sharedfiles->CopyWebSharedFileSnapshot(strFileHash, snapshot))
		return false;
	info.sFileName = snapshot.strFileName;
	info.sFilePath = snapshot.strFilePath;
	info.qwFileSize = snapshot.uFileSize;
	info.bFound = !info.sFilePath.IsEmpty();
	return info.bFound;
}

bool CWebServer::_GetServerListSnapshot(CWebServer *pThis, CArray<ServerEntry> &ServerArray)
{
	ServerArray.RemoveAll();
	if (pThis == NULL)
		return false;
	if (theApp.IsUiThread()) {
		const bool bResult = BuildServerListSnapshotForWebThread(ServerArray);
		if (bResult)
			pThis->StoreWebServerListSnapshot(ServerArray);
		return bResult;
	}

	const bool bHasCachedSnapshot = pThis->CopyWebServerListSnapshot(ServerArray);
	SWebServerListSnapshotRequest *pRequest = new SWebServerListSnapshotRequest();
	pRequest->m_pThis = pThis;
	QueueWebUiSnapshotRefresh(WEB_GET_SERVER_LIST_SNAPSHOT, 0, pRequest);
	return bHasCachedSnapshot;
}

bool CWebServer::_BuildServerListSnapshot(CArray<ServerEntry> &ServerArray)
{
	ServerArray.RemoveAll();
	if (theApp.serverlist == NULL)
		return false;

	for (INT_PTR sc = theApp.serverlist->GetServerCount(); --sc >= 0;) {
		CServer *pServer = theApp.serverlist->GetServerAt(sc);
		if (pServer == NULL)
			continue;
		const CServer &cur_serv = *pServer;
		ServerEntry Entry;
		Entry.sServerName = _SpecialChars(cur_serv.GetListName());
		Entry.sServerIP = cur_serv.GetAddress();
		Entry.nServerPort = cur_serv.GetPort();
		Entry.sServerDescription = _SpecialChars(cur_serv.GetDescription());
		Entry.nServerPing = cur_serv.GetPing();
		Entry.nServerUsers = cur_serv.GetUsers();
		Entry.nServerMaxUsers = cur_serv.GetMaxUsers();
		Entry.nServerFiles = cur_serv.GetFiles();
		Entry.bServerStatic = cur_serv.IsStaticMember();
		LPCTSTR uid;
		switch (cur_serv.GetPreference()) {
		case SRV_PR_HIGH:
			uid = _T("PRIOHIGH");
			Entry.nServerPriority = 2;
			break;
		case SRV_PR_NORMAL:
			uid = _T("PRIONORMAL");
			Entry.nServerPriority = 1;
			break;
		case SRV_PR_LOW:
			uid = _T("PRIOLOW");
			Entry.nServerPriority = 0;
			break;
		default:
			uid = _T("");
			Entry.nServerPriority = 0;
		}
		if (uid)
			Entry.sServerPriority = _GetPlainResString(uid);
		Entry.nServerFailed = cur_serv.GetFailedCount();
		Entry.nServerSoftLimit = cur_serv.GetSoftFiles();
		Entry.nServerHardLimit = cur_serv.GetHardFiles();
		Entry.sServerVersion = cur_serv.GetVersion();
		if (inet_addr((CStringA)Entry.sServerIP) != INADDR_NONE) {
			CString &newip(Entry.sServerFullIP);
			for (int j = 0, iPos = 0; j < 4 && iPos >= 0; ++j) {
				const CString &temp(Entry.sServerIP.Tokenize(_T("."), iPos));
				newip.AppendFormat(&_T("000%s")[min(temp.GetLength(), 3)], (LPCTSTR)temp);
			}
		} else
			Entry.sServerFullIP = Entry.sServerIP;
		Entry.sServerState = cur_serv.GetFailedCount() ? _T("failed") : _T("disconnected");

		if (theApp.serverconnect != NULL && theApp.serverconnect->IsConnecting() && theApp.serverconnect->AwaitingConnectionToServer(pServer))
			Entry.sServerState = _T("connecting");
		else if (theApp.serverconnect != NULL && theApp.serverconnect->IsConnected() && theApp.serverconnect->GetCurrentServer() != NULL) {
			if (theApp.serverconnect->GetCurrentServer()->GetFullIP() == cur_serv.GetFullIP())
				Entry.sServerState = theApp.serverconnect->IsLowID() ? _T("low") : _T("high");
		}
		ServerArray.Add(Entry);
		if (ServerArray.GetCount() >= WEB_MAX_TABLE_ROWS)
			break;
	}
	return true;
}

bool CWebServer::BuildServerListSnapshotForWebThread(CArray<ServerEntry> &ServerArray)
{
	return _BuildServerListSnapshot(ServerArray);
}



static CString GetWebSharedSnapshotFileNameByHash(const uchar *fileHash)
{
	if (fileHash == NULL || theApp.sharedfiles == NULL)
		return CString();
	CSharedFileList::SWebSharedFileSnapshot snapshot;
	if (theApp.sharedfiles->CopyWebSharedFileSnapshot(md4str(fileHash), snapshot))
		return snapshot.strFileName;
	return CString();
}

static CString BuildWebSharedFileEd2kLink(const CSharedFileList::SWebSharedFileSnapshot &snapshot)
{
	if (theApp.sharedfiles == NULL || snapshot.strFileHash.GetLength() != 32)
		return CString();

	uchar fileHash[MDX_DIGEST_SIZE];
	if (!DecodeBase16(snapshot.strFileHash, snapshot.strFileHash.GetLength(), fileHash, _countof(fileHash)))
		return CString();

	const CKnownFile *pFile = theApp.sharedfiles->GetFileByID(fileHash);
	if (pFile != NULL)
		return pFile->GetED2kLink();

	CString strED2kLink;
	strED2kLink.Format(_T("ed2k://|file|%s|%I64u|%s|/"), (LPCTSTR)EncodeUrlUtf8(snapshot.strFileName), snapshot.uFileSize, (LPCTSTR)snapshot.strFileHash);
	return strED2kLink;
}

bool CWebServer::_GetTransferSnapshot(CWebServer *pThis, const ThreadData &Data, int cat, WebTransferSnapshot &snapshot)
{
	snapshot.FilesArray.RemoveAll();
	snapshot.UploadArray.RemoveAll();
	snapshot.QueueArray.RemoveAll();
	snapshot.nCountQueue = 0;
	snapshot.nCountQueueBanned = 0;
	snapshot.nCountQueueFriend = 0;
	snapshot.nCountQueueSecure = 0;
	snapshot.nCountQueueBannedSecure = 0;
	snapshot.nCountQueueFriendSecure = 0;
	snapshot.bStale = false;
	if (pThis == NULL)
		return false;
	if (theApp.IsUiThread()) {
		const bool bResult = pThis->BuildTransferSnapshotForWebThread(pThis, Data, cat, snapshot);
		if (bResult) {
			pThis->StoreWebTransferSnapshot(cat, snapshot);
			pThis->ApplyWebTransferRenamePreviews(snapshot);
		}
		return bResult;
	}

	const bool bTransferCommand = _IsTransferCommandRequest(Data.sURL);
	const bool bHasCachedSnapshot = pThis->CopyWebTransferSnapshot(cat, snapshot);
	SWebTransferSnapshotRequest *pRequest = new SWebTransferSnapshotRequest();
	pRequest->m_pThis = pThis;
	pRequest->m_pServer = pThis;
	pRequest->m_Data = Data;
	pRequest->m_iCategory = cat;
	if (!bHasCachedSnapshot || bTransferCommand) {
		const DWORD dwWait = bTransferCommand ? WEB_TRANSFER_COMMAND_SNAPSHOT_SYNC_WAIT_MS : WEB_TRANSFER_SNAPSHOT_SYNC_WAIT_MS;
		const bool bCompleted = PostWebUiRequestAndWait(WEB_GET_TRANSFER_SNAPSHOT, 0, pRequest, dwWait);
		if (bCompleted && pRequest->m_bResult && pRequest->m_pSnapshot != NULL) {
			snapshot = *pRequest->m_pSnapshot;
			pThis->ApplyWebTransferRenamePreviews(snapshot);
			pRequest->ReleaseReference();
			return true;
		}
		pRequest->ReleaseReference();
		return bHasCachedSnapshot;
	}

	QueueWebUiSnapshotRefresh(WEB_GET_TRANSFER_SNAPSHOT, 0, pRequest);
	return bHasCachedSnapshot;
}

bool CWebServer::BuildTransferSnapshotForWebThread(const ThreadData &Data, int cat, WebTransferSnapshot &snapshot)
{
	return BuildTransferSnapshotForWebThread(this, Data, cat, snapshot);
}

bool CWebServer::BuildTransferSnapshotForWebThread(CWebServer *pThis, const ThreadData &Data, int cat, WebTransferSnapshot &snapshot)
{
	snapshot.FilesArray.RemoveAll();
	snapshot.UploadArray.RemoveAll();
	snapshot.QueueArray.RemoveAll();
	snapshot.nCountQueue = 0;
	snapshot.nCountQueueBanned = 0;
	snapshot.nCountQueueFriend = 0;
	snapshot.nCountQueueSecure = 0;
	snapshot.nCountQueueBannedSecure = 0;
	snapshot.nCountQueueFriendSecure = 0;
	snapshot.bStale = false;
	if (pThis == NULL || theApp.downloadqueue == NULL || theApp.uploadqueue == NULL)
		return false;

	for (POSITION pos = NULL; ;) {
		CPartFile *pPartFile = theApp.downloadqueue->GetFileNext(pos);
		if (pPartFile != NULL) {
			bool bInclude = true;
			if (cat < 0) {
				switch (cat) {
				case -1:
					bInclude = (pPartFile->GetCategory() == 0);
					break;
				case -2:
					bInclude = pPartFile->IsPartFile();
					break;
				case -3:
					bInclude = !pPartFile->IsPartFile();
					break;
				case -4:
					bInclude = (pPartFile->GetStatus() == PS_READY || pPartFile->GetStatus() == PS_EMPTY) && pPartFile->GetTransferringSrcCount() == 0;
					break;
				case -5:
					bInclude = (pPartFile->GetStatus() == PS_READY || pPartFile->GetStatus() == PS_EMPTY) && pPartFile->GetTransferringSrcCount() > 0;
					break;
				case -6:
					bInclude = (pPartFile->GetStatus() == PS_ERROR);
					break;
				case -7:
					bInclude = (pPartFile->GetStatus() == PS_PAUSED || pPartFile->IsStopped());
					break;
				case -8:
					bInclude = (pPartFile->lastseencomplete != 0);
					break;
				case -9:
					bInclude = pPartFile->IsMovie();
					break;
				case -10:
					bInclude = (ED2KFT_AUDIO == GetED2KFileTypeID(pPartFile->GetFileName()));
					break;
				case -11:
					bInclude = pPartFile->IsArchive();
					break;
				case -12:
					bInclude = (ED2KFT_CDIMAGE == GetED2KFileTypeID(pPartFile->GetFileName()));
					break;
				case -13:
					bInclude = (ED2KFT_DOCUMENT == GetED2KFileTypeID(pPartFile->GetFileName()));
					break;
				case -14:
					bInclude = (ED2KFT_IMAGE == GetED2KFileTypeID(pPartFile->GetFileName()));
					break;
				case -15:
					bInclude = (ED2KFT_PROGRAM == GetED2KFileTypeID(pPartFile->GetFileName()));
					break;
				case -16:
					bInclude = (ED2KFT_EMULECOLLECTION == GetED2KFileTypeID(pPartFile->GetFileName()));
					break;
				default:
					bInclude = false;
				}
			} else if (cat > 0)
				bInclude = (pPartFile->GetCategory() == (UINT)cat);

			if (bInclude) {
				DownloadFiles dFile;
				dFile.sFileName = _SpecialChars(pPartFile->GetFileName());
				dFile.sFileType = _GetWebImageNameForFileType(dFile.sFileName);
				dFile.sFileNameJS = _SpecialChars(pPartFile->GetFileName());
				dFile.m_qwFileSize = (uint64)pPartFile->GetFileSize();
				dFile.m_qwFileTransferred = (uint64)pPartFile->GetCompletedSize();
				dFile.m_dblCompleted = pPartFile->GetPercentCompleted();
				dFile.lFileSpeed = pPartFile->GetDatarate();
				dFile.iComment = (pPartFile->HasComment() || pPartFile->HasRating()) ? (pPartFile->HasBadRating() ? 2 : 1) : 0;
				dFile.iFileState = pPartFile->getPartfileStatusRank();

				LPCTSTR pFileState;
				switch (pPartFile->GetStatus()) {
				case PS_HASHING:
					pFileState = _T("hashing");
					break;
				case PS_WAITINGFORHASH:
					pFileState = _T("waitinghash");
					break;
				case PS_ERROR:
					pFileState = _T("error");
					break;
				case PS_COMPLETING:
					pFileState = _T("completing");
					break;
				case PS_COMPLETE:
					pFileState = _T("complete");
					break;
				case PS_PAUSED:
					pFileState = pPartFile->IsStopped() ? _T("stopped") : _T("paused");
					break;
				default:
					pFileState = (pPartFile->GetDatarate() > 0) ? _T("downloading") : _T("waiting");
				}
				dFile.sFileState = CString(pFileState);
				dFile.bFileAutoPrio = pPartFile->IsAutoDownPriority();
				dFile.nFilePrio = pPartFile->GetDownPriority();
				dFile.iCategory = pPartFile->GetCategory();

				CString strCategory(thePrefs.GetCategoryDisplayTitle(dFile.iCategory));
				strCategory.Replace(_T("'"), _T("\\'"));
				dFile.sCategory = strCategory;

				dFile.sFileHash = md4str(pPartFile->GetFileHash());
				dFile.lSourceCount = pPartFile->GetSourceCount();
				dFile.lNotCurrentSourceCount = pPartFile->GetNotCurrentSourcesCount();
				dFile.lTransferringSourceCount = pPartFile->GetTransferringSrcCount();
				dFile.bIsComplete = !pPartFile->IsPartFile();
				dFile.bIsPreview = pPartFile->IsReadyForPreview();
				dFile.bIsGetFLC = pPartFile->GetPreviewPrio();
				dFile.sDownloadBar = _GetDownloadGraph(Data, pPartFile);

				if (!theApp.GetPublicIP().IsNull() && !theApp.IsFirewalled())
					dFile.sED2kLink = pPartFile->GetED2kLink(false, false, false, true, theApp.GetPublicIP());
				else
					dFile.sED2kLink = pPartFile->GetED2kLink();

				dFile.sFileInfo = _SpecialChars(pPartFile->GetInfoSummary(true), false);
				snapshot.FilesArray.Add(dFile);
			}
		}
		if (pos == NULL || snapshot.FilesArray.GetCount() >= WEB_MAX_TABLE_ROWS)
			break;
	}

	SortParams dprm{ (int)pThis->m_Params.DownloadSort, pThis->m_Params.bDownloadSortReverse };
	qsort_s(snapshot.FilesArray.GetData(), snapshot.FilesArray.GetCount(), sizeof(DownloadFiles), &_DownloadCmp, &dprm);

	for (POSITION pos = theApp.uploadqueue->GetFirstFromUploadList(); pos != NULL && snapshot.UploadArray.GetCount() < WEB_MAX_TABLE_ROWS;) {
		UploadUsers dUser;
		const CUpDownClient &cur_client(*theApp.uploadqueue->GetNextFromUploadList(pos));
		dUser.sUserHash = md4str(cur_client.GetUserHash());
		if (cur_client.GetUploadDatarate() > 0) {
			dUser.sActive = _T("downloading");
			dUser.sClientState = _T("uploading");
		} else {
			dUser.sActive = _T("waiting");
			dUser.sClientState = _T("connecting");
		}

		const CString strUploadFileName(GetWebSharedSnapshotFileNameByHash(cur_client.GetUploadFileID()));
		dUser.sFileInfo = _SpecialChars(_GetClientSummary(cur_client, strUploadFileName), false);
		dUser.sFileInfo.Replace(_T("\\"), _T("\\\\"));
		dUser.sFileInfo.Replace(_T("\n"), _T("<br>"));
		dUser.sFileInfo.Replace(_T("'"), _T("&#8217;"));

		_GetClientversionImage(cur_client, dUser.sClientSoft);
		if (cur_client.IsBanned())
			dUser.sClientExtra = _T("banned");
		else if (cur_client.IsFriend())
			dUser.sClientExtra = _T("friend");
		else if (cur_client.Credits()->GetScoreRatio(cur_client.GetIP()) > 1)
			dUser.sClientExtra = _T("credit");
		else
			dUser.sClientExtra = _T("none");

		CString cname(cur_client.GetUserName());
		if (cname.GetLength() > SHORT_LENGTH_MIN) {
			cname.Truncate(SHORT_LENGTH_MIN - 3);
			cname += _T("...");
		}
		dUser.sUserName = _SpecialChars(cname);

		dUser.sFileName = strUploadFileName.IsEmpty() ? _GetPlainResString(_T("REQ_UNKNOWNFILE")) : _SpecialChars(strUploadFileName);
		dUser.nTransferredDown = cur_client.GetTransferredDown();
		dUser.nTransferredUp = cur_client.GetTransferredUp();
		UINT uDataRate = cur_client.GetUploadDatarate();
		dUser.nDataRate = (uDataRate == UNLIMITED) ? 0 : uDataRate;
		dUser.sClientNameVersion = cur_client.DbgGetFullClientSoftVer();
		snapshot.UploadArray.Add(dUser);
	}

	SortParams uprm{ (int)pThis->m_Params.UploadSort, pThis->m_Params.bUploadSortReverse };
	qsort_s(snapshot.UploadArray.GetData(), snapshot.UploadArray.GetCount(), sizeof(UploadUsers), &_UploadCmp, &uprm);

	for (POSITION pos = theApp.uploadqueue->waitinglist.GetHeadPosition(); pos != NULL && snapshot.QueueArray.GetCount() < WEB_MAX_TABLE_ROWS;) {
		QueueUsers dUser;
		CUpDownClient &cur_client(*theApp.uploadqueue->waitinglist.GetNext(pos));
		int iSecure = static_cast<int>(cur_client.Credits()->GetCurrentIdentState(cur_client.GetIP()) == IS_IDENTIFIED);
		if (cur_client.IsBanned()) {
			dUser.sClientExtra = _T("banned");
			++snapshot.nCountQueueBanned;
			snapshot.nCountQueueBannedSecure += iSecure;
		} else if (cur_client.IsFriend()) {
			dUser.sClientExtra = _T("friend");
			++snapshot.nCountQueueFriend;
			snapshot.nCountQueueFriendSecure += iSecure;
		} else {
			dUser.sClientExtra = _T("none");
			++snapshot.nCountQueue;
			snapshot.nCountQueueSecure += iSecure;
		}

		CString usn(cur_client.GetUserName());
		if (usn.GetLength() > SHORT_LENGTH_MIN) {
			usn.Truncate(SHORT_LENGTH_MIN - 3);
			usn += _T("...");
		}
		dUser.sUserName = _SpecialChars(usn);
		dUser.sClientNameVersion = cur_client.DbgGetFullClientSoftVer();
		const CString strUploadFileName(GetWebSharedSnapshotFileNameByHash(cur_client.GetUploadFileID()));
		dUser.sFileName = strUploadFileName.IsEmpty() ? _GetPlainResString(_T("REQ_UNKNOWNFILE")) : _SpecialChars(strUploadFileName);
		dUser.sClientState = dUser.sClientExtra;
		dUser.sClientStateSpecial = _T("connecting");
		dUser.nScore = cur_client.GetScore(false);
		_GetClientversionImage(cur_client, dUser.sClientSoft);
		dUser.sUserHash = md4str(cur_client.GetUserHash());

		switch (pThis->m_Params.QueueSort) {
		case QU_SORT_CLIENT:
			dUser.sIndex = dUser.sClientSoft;
			break;
		case QU_SORT_USER:
			dUser.sIndex = dUser.sUserName;
			break;
		case QU_SORT_VERSION:
			dUser.sIndex = dUser.sClientNameVersion;
			break;
		case QU_SORT_FILENAME:
			dUser.sIndex = dUser.sFileName;
			break;
		case QU_SORT_SCORE:
			dUser.sIndex.Format(_T("%09u"), dUser.nScore);
		}
		snapshot.QueueArray.Add(dUser);
	}

	INT_PTR nNextPos = 0;
	uint32 nNextScore = 0;
	for (INT_PTR i = snapshot.QueueArray.GetCount(); --i >= 0;)
		if (snapshot.QueueArray[i].nScore > nNextScore) {
			nNextPos = i;
			nNextScore = snapshot.QueueArray[i].nScore;
		}

	if (snapshot.QueueArray.GetCount() > 0) {
		snapshot.QueueArray[nNextPos].sClientState = _T("next");
		snapshot.QueueArray[nNextPos].sClientStateSpecial = snapshot.QueueArray[nNextPos].sClientState;
	}

	if ((snapshot.nCountQueue > 0 && pThis->m_Params.bShowUploadQueue)
		|| (snapshot.nCountQueueBanned > 0 && pThis->m_Params.bShowUploadQueueBanned)
		|| (snapshot.nCountQueueFriend > 0 && pThis->m_Params.bShowUploadQueueFriend))
	{
#ifdef _DEBUG
		const DWORD dwStart = ::GetTickCount();
#endif
		snapshot.QueueArray.QuickSort(pThis->m_Params.bQueueSortReverse);
#ifdef _DEBUG
		AddDebugLogLine(false, _T("WebServer: Waitingqueue with %u elements sorted in %u ms"), snapshot.QueueArray.GetCount(), ::GetTickCount() - dwStart);
#endif
	}
	return true;
}

bool CWebServer::_GetSharedFilesSnapshot(CWebServer *pThis, CArray<SharedFiles> &SharedArray)
{
	SharedArray.RemoveAll();
	if (pThis == NULL)
		return false;
	if (theApp.IsUiThread()) {
		const bool bResult = BuildSharedFilesSnapshotForWebThread(SharedArray);
		if (bResult)
			pThis->StoreWebSharedFilesSnapshot(SharedArray);
		return bResult;
	}

	const bool bHasCachedSnapshot = pThis->CopyWebSharedFilesSnapshot(SharedArray);
	SWebSharedFilesSnapshotRequest *pRequest = new SWebSharedFilesSnapshotRequest();
	pRequest->m_pThis = pThis;
	if (!bHasCachedSnapshot) {
		const bool bCompleted = PostWebUiRequestAndWait(WEB_GET_SHARED_FILES_SNAPSHOT, 0, pRequest, WEB_SHARED_FILES_SNAPSHOT_SYNC_WAIT_MS);
		if (bCompleted && pRequest->m_bResult && pRequest->m_pSharedArray != NULL) {
			SharedArray.RemoveAll();
			SharedArray.Append(*pRequest->m_pSharedArray);
			pRequest->ReleaseReference();
			return true;
		}
		pRequest->ReleaseReference();
		return pThis->CopyWebSharedFilesSnapshot(SharedArray);
	}

	QueueWebUiSnapshotRefresh(WEB_GET_SHARED_FILES_SNAPSHOT, 0, pRequest);
	return bHasCachedSnapshot;
}

bool CWebServer::BuildSharedFilesSnapshotForWebThread(CArray<SharedFiles> &SharedArray)
{
	SharedArray.RemoveAll();
	if (theApp.sharedfiles == NULL)
		return false;

	std::vector<CSharedFileList::SWebSharedFileSnapshot> webSnapshots;
	theApp.sharedfiles->CopyWebSharedFileSnapshots(webSnapshots, static_cast<size_t>(WEB_MAX_TABLE_ROWS));
	SharedArray.SetSize(0, static_cast<INT_PTR>(std::min<size_t>(webSnapshots.size(), static_cast<size_t>(WEB_MAX_TABLE_ROWS))));
	for (size_t i = 0; i < webSnapshots.size() && SharedArray.GetCount() < WEB_MAX_TABLE_ROWS; ++i) {
		const CSharedFileList::SWebSharedFileSnapshot &snapshot = webSnapshots[i];
		if (snapshot.strFileHash.IsEmpty())
			continue;

		SharedFiles dFile;
		dFile.bIsPartFile = snapshot.bPartFile;
		dFile.sFileName = snapshot.strFileName;
		dFile.sFileState = snapshot.bPartFile ? _T("filedown") : _T("file");
		dFile.sFileType = _GetWebImageNameForFileType(dFile.sFileName);
		dFile.m_qwFileSize = snapshot.uFileSize;
		dFile.sED2kLink = BuildWebSharedFileEd2kLink(snapshot);
		dFile.nFileTransferred = snapshot.uTransferred;
		dFile.nFileAllTimeTransferred = snapshot.uAllTimeTransferred;
		dFile.nFileRequests = snapshot.uRequests;
		dFile.nFileAllTimeRequests = snapshot.uAllTimeRequests;
		dFile.nFileAccepts = snapshot.uAccepts;
		dFile.nFileAllTimeAccepts = snapshot.uAllTimeAccepts;
		dFile.sFileHash = snapshot.strFileHash;
		dFile.sFileCompletes = snapshot.strFileCompletes;
		dFile.dblFileCompletes = snapshot.dblFileCompletes;
		dFile.sFilePriority = snapshot.strFilePriority;
		dFile.nFilePriority = snapshot.nFilePriority;
		dFile.bFileAutoPriority = snapshot.bFileAutoPriority;
		dFile.bDownloadable = snapshot.bDownloadable;
		dFile.bReleasePriority = snapshot.bReleasePriority;
		SharedArray.Add(dFile);
	}
	return true;
}

CString CWebServer::_GetServerList(const ThreadData &Data)
{
	CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	const CString &sSession(_ParseURL(Data.sURL, _T("ses")));
	bool bAdmin = _IsSessionAdmin(Data, sSession);
	const CString &sAddServerBox(_GetAddServerBox(Data));

	const CString &sCmd(_ParseURL(Data.sURL, _T("c")));
	const CString &sIP(_ParseURL(Data.sURL, _T("ip")));
	int nPort = _tstoi(_ParseURL(Data.sURL, _T("port")));
	if (bAdmin) {
		if (sCmd == _T("connect"))
			ExecuteServerCommandForWebThread(sIP.IsEmpty() ? WEB_SERVER_COMMAND_CONNECT_ANY : WEB_SERVER_COMMAND_CONNECT, sIP, nPort);
		else if (sCmd == _T("disconnect"))
			ExecuteServerCommandForWebThread(WEB_SERVER_COMMAND_DISCONNECT_ED2K, CString(), 0);
		else if (sCmd == _T("remove")) {
			if (!sIP.IsEmpty())
				ExecuteServerCommandForWebThread(WEB_SERVER_COMMAND_REMOVE, sIP, nPort);
		} else if (sCmd == _T("addtostatic")) {
			if (!sIP.IsEmpty())
				ExecuteServerCommandForWebThread(WEB_SERVER_COMMAND_ADD_TO_STATIC, sIP, nPort);
		} else if (sCmd == _T("removefromstatic")) {
			if (!sIP.IsEmpty())
				ExecuteServerCommandForWebThread(WEB_SERVER_COMMAND_REMOVE_FROM_STATIC, sIP, nPort);
		} else if (sCmd == _T("priolow")) {
			if (!sIP.IsEmpty())
				ExecuteServerCommandForWebThread(WEB_SERVER_COMMAND_SET_PRIORITY, sIP, nPort, SRV_PR_LOW);
		} else if (sCmd == _T("prionormal")) {
			if (!sIP.IsEmpty())
				ExecuteServerCommandForWebThread(WEB_SERVER_COMMAND_SET_PRIORITY, sIP, nPort, SRV_PR_NORMAL);
		} else if (sCmd == _T("priohigh")) {
			if (!sIP.IsEmpty())
				ExecuteServerCommandForWebThread(WEB_SERVER_COMMAND_SET_PRIORITY, sIP, nPort, SRV_PR_HIGH);
		}
	} else if (sCmd == _T("menu")) {
		int iMenu = _tstol(_ParseURL(Data.sURL, _T("m")));
		bool bValue = _ParseURL(Data.sURL, _T("v")) == _T("1");
		WSserverColumnHidden[iMenu] = bValue;
		_SaveWIConfigArray(WSserverColumnHidden, _countof(WSserverColumnHidden), _T("serverColumnHidden"));
	}

	CString strTmp(_ParseURL(Data.sURL, _T("sortreverse")));
	const CString &sSort(_ParseURL(Data.sURL, _T("sort")));

	if (!sSort.IsEmpty()) {
		bool bDirection = false;

		if (sSort == _T("state"))
			pThis->m_Params.ServerSort = SERVER_SORT_STATE;
		else if (sSort == _T("name")) {
			pThis->m_Params.ServerSort = SERVER_SORT_NAME;
			bDirection = true;
		} else if (sSort == _T("ip"))
			pThis->m_Params.ServerSort = SERVER_SORT_IP;
		else if (sSort == _T("description")) {
			pThis->m_Params.ServerSort = SERVER_SORT_DESCRIPTION;
			bDirection = true;
		} else if (sSort == _T("ping"))
			pThis->m_Params.ServerSort = SERVER_SORT_PING;
		else if (sSort == _T("users"))
			pThis->m_Params.ServerSort = SERVER_SORT_USERS;
		else if (sSort == _T("files"))
			pThis->m_Params.ServerSort = SERVER_SORT_FILES;
		else if (sSort == _T("priority"))
			pThis->m_Params.ServerSort = SERVER_SORT_PRIORITY;
		else if (sSort == _T("failed"))
			pThis->m_Params.ServerSort = SERVER_SORT_FAILED;
		else if (sSort == _T("limit"))
			pThis->m_Params.ServerSort = SERVER_SORT_LIMIT;
		else if (sSort == _T("version"))
			pThis->m_Params.ServerSort = SERVER_SORT_VERSION;

		if (strTmp.IsEmpty())
			pThis->m_Params.bServerSortReverse = bDirection;
	}
	if (!strTmp.IsEmpty())
		pThis->m_Params.bServerSortReverse = (strTmp == _T("true"));

	CString Out(pThis->m_Templates.sServerList);

	Out.Replace(_T("[AddServerBox]"), sAddServerBox);
	Out.Replace(_T("[Session]"), sSession);

	strTmp = (pThis->m_Params.bServerSortReverse) ? _T("&amp;sortreverse=false") : _T("&amp;sortreverse=true");

	if (pThis->m_Params.ServerSort == SERVER_SORT_STATE)
		Out.Replace(_T("[SortState]"), strTmp);
	else
		Out.Replace(_T("[SortState]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_NAME)
		Out.Replace(_T("[SortName]"), strTmp);
	else
		Out.Replace(_T("[SortName]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_IP)
		Out.Replace(_T("[SortIP]"), strTmp);
	else
		Out.Replace(_T("[SortIP]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_DESCRIPTION)
		Out.Replace(_T("[SortDescription]"), strTmp);
	else
		Out.Replace(_T("[SortDescription]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_PING)
		Out.Replace(_T("[SortPing]"), strTmp);
	else
		Out.Replace(_T("[SortPing]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_USERS)
		Out.Replace(_T("[SortUsers]"), strTmp);
	else
		Out.Replace(_T("[SortUsers]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_FILES)
		Out.Replace(_T("[SortFiles]"), strTmp);
	else
		Out.Replace(_T("[SortFiles]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_PRIORITY)
		Out.Replace(_T("[SortPriority]"), strTmp);
	else
		Out.Replace(_T("[SortPriority]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_FAILED)
		Out.Replace(_T("[SortFailed]"), strTmp);
	else
		Out.Replace(_T("[SortFailed]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_LIMIT)
		Out.Replace(_T("[SortLimit]"), strTmp);
	else
		Out.Replace(_T("[SortLimit]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_VERSION)
		Out.Replace(_T("[SortVersion]"), strTmp);
	else
		Out.Replace(_T("[SortVersion]"), _T(""));
	Out.Replace(_T("[ServerList]"), _GetPlainResString(_T("FSTAT_SERVERS")));

	CString sSortIcon = _WebSelectString(pThis->m_Params.bServerSortReverse, pThis->m_Templates.sUpArrow, pThis->m_Templates.sDownArrow);
		LPCTSTR pcSortIcon = sSortIcon;

	_GetPlainResString(strTmp, _T("SL_SERVERNAME"));
	if (WSserverColumnHidden[0]) {
		Out.Replace(_T("[ServernameI]"), _T(""));
		Out.Replace(_T("[ServernameH]"), _T(""));
	} else {
		Out.Replace(_T("[ServernameI]"), _WebSelectString(pThis->m_Params.ServerSort == SERVER_SORT_NAME, pcSortIcon, _T("")));
		Out.Replace(_T("[ServernameH]"), strTmp);
	}
	Out.Replace(_T("[ServernameM]"), strTmp);

	_GetPlainResString(strTmp, _T("IP"));
	if (WSserverColumnHidden[1]) {
		Out.Replace(_T("[AddressI]"), _T(""));
		Out.Replace(_T("[AddressH]"), _T(""));
	} else {
		Out.Replace(_T("[AddressI]"), _WebSelectString(pThis->m_Params.ServerSort == SERVER_SORT_IP, pcSortIcon, _T("")));
		Out.Replace(_T("[AddressH]"), strTmp);
	}
	Out.Replace(_T("[AddressM]"), strTmp);

	_GetPlainResString(strTmp, _T("DESCRIPTION"));
	if (WSserverColumnHidden[2]) {
		Out.Replace(_T("[DescriptionI]"), _T(""));
		Out.Replace(_T("[DescriptionH]"), _T(""));
	} else {
		Out.Replace(_T("[DescriptionI]"), _WebSelectString(pThis->m_Params.ServerSort == SERVER_SORT_DESCRIPTION, pcSortIcon, _T("")));
		Out.Replace(_T("[DescriptionH]"), strTmp);
	}
	Out.Replace(_T("[DescriptionM]"), strTmp);

	_GetPlainResString(strTmp, _T("PING"));
	if (WSserverColumnHidden[3]) {
		Out.Replace(_T("[PingI]"), _T(""));
		Out.Replace(_T("[PingH]"), _T(""));
	} else {
		Out.Replace(_T("[PingI]"), _WebSelectString(pThis->m_Params.ServerSort == SERVER_SORT_PING, pcSortIcon, _T("")));
		Out.Replace(_T("[PingH]"), strTmp);
	}
	Out.Replace(_T("[PingM]"), strTmp);

	_GetPlainResString(strTmp, _T("UUSERS"));
	if (WSserverColumnHidden[4]) {
		Out.Replace(_T("[UsersI]"), _T(""));
		Out.Replace(_T("[UsersH]"), _T(""));
	} else {
		Out.Replace(_T("[UsersI]"), _WebSelectString(pThis->m_Params.ServerSort == SERVER_SORT_USERS, pcSortIcon, _T("")));
		Out.Replace(_T("[UsersH]"), strTmp);
	}
	Out.Replace(_T("[UsersM]"), strTmp);

	_GetPlainResString(strTmp, _T("FILES"));
	if (WSserverColumnHidden[5]) {
		Out.Replace(_T("[FilesI]"), _T(""));
		Out.Replace(_T("[FilesH]"), _T(""));
	} else {
		Out.Replace(_T("[FilesI]"), _WebSelectString(pThis->m_Params.ServerSort == SERVER_SORT_FILES, pcSortIcon, _T("")));
		Out.Replace(_T("[FilesH]"), strTmp);
	}
	Out.Replace(_T("[FilesM]"), strTmp);

	_GetPlainResString(strTmp, _T("PRIORITY"));
	if (WSserverColumnHidden[6]) {
		Out.Replace(_T("[PriorityI]"), _T(""));
		Out.Replace(_T("[PriorityH]"), _T(""));
	} else {
		Out.Replace(_T("[PriorityI]"), _WebSelectString(pThis->m_Params.ServerSort == SERVER_SORT_PRIORITY, pcSortIcon, _T("")));
		Out.Replace(_T("[PriorityH]"), strTmp);
	}
	Out.Replace(_T("[PriorityM]"), strTmp);

	_GetPlainResString(strTmp, _T("UFAILED"));
	if (WSserverColumnHidden[7]) {
		Out.Replace(_T("[FailedI]"), _T(""));
		Out.Replace(_T("[FailedH]"), _T(""));
	} else {
		Out.Replace(_T("[FailedI]"), _WebSelectString(pThis->m_Params.ServerSort == SERVER_SORT_FAILED, pcSortIcon, _T("")));
		Out.Replace(_T("[FailedH]"), strTmp);
	}
	Out.Replace(_T("[FailedM]"), strTmp);

	_GetPlainResString(strTmp, _T("SERVER_LIMITS"));
	if (WSserverColumnHidden[8]) {
		Out.Replace(_T("[LimitI]"), _T(""));
		Out.Replace(_T("[LimitH]"), _T(""));
	} else {
		Out.Replace(_T("[LimitI]"), _WebSelectString(pThis->m_Params.ServerSort == SERVER_SORT_LIMIT, pcSortIcon, _T("")));
		Out.Replace(_T("[LimitH]"), strTmp);
	}
	Out.Replace(_T("[LimitM]"), strTmp);

	_GetPlainResString(strTmp, _T("SV_SERVERINFO"));
	if (WSserverColumnHidden[9]) {
		Out.Replace(_T("[VersionI]"), _T(""));
		Out.Replace(_T("[VersionH]"), _T(""));
	} else {
		Out.Replace(_T("[VersionI]"), _WebSelectString(pThis->m_Params.ServerSort == SERVER_SORT_VERSION, pcSortIcon, _T("")));
		Out.Replace(_T("[VersionH]"), strTmp);
	}
	Out.Replace(_T("[VersionM]"), strTmp);

	Out.Replace(_T("[Actions]"), _GetPlainResString(_T("WEB_ACTIONS")));

	CArray<ServerEntry> ServerArray;
	if (!_GetServerListSnapshot(pThis, ServerArray))
		ServerArray.RemoveAll();

	SortParams prm{ (int)pThis->m_Params.ServerSort, pThis->m_Params.bServerSortReverse };
	qsort_s(ServerArray.GetData(), ServerArray.GetCount(), sizeof(ServerEntry), &_ServerCmp, &prm);

	// Displaying
	CString OutE(pThis->m_Templates.sServerLine); // List Entry Templates

	OutE.Replace(_T("[admin]"), _WebSelectString(bAdmin, _T("admin"), _T("")));
	OutE.Replace(_T("[session]"), sSession);

	CString sList, HTTPProcessData, sServerPort, ed2k;
	for (INT_PTR i = 0; i < ServerArray.GetCount(); ++i) {
		const ServerEntry &cur_srv(ServerArray[i]);
		HTTPProcessData = OutE;	// Copy Entry Line to Temp

		sServerPort.Format(_T("%i"), cur_srv.nServerPort);
		ed2k.Format(_T("ed2k://|server|%s|%s|/"), (LPCTSTR)cur_srv.sServerIP, (LPCTSTR)sServerPort);

		bool b = (cur_srv.sServerIP == _ParseURL(Data.sURL, _T("ip")) && sServerPort == _ParseURL(Data.sURL, _T("port")));
		HTTPProcessData.Replace(_T("[LastChangedDataset]"), _WebSelectString(b, _T("checked"), _T("checked_no")));
		b = cur_srv.bServerStatic;
		HTTPProcessData.Replace(_T("[isstatic]"), _WebSelectString(b, _T("staticsrv"), _T("")));
		HTTPProcessData.Replace(_T("[ServerType]"), _WebSelectString(b, _T("static"), _T("none")));

		LPCTSTR pcSrvPriority;
		switch (cur_srv.nServerPriority) {
		case 0:
			pcSrvPriority = _T("Low");
			break;
		case 1:
			pcSrvPriority = _T("Normal");
			break;
		case 2:
			pcSrvPriority = _T("High");
			break;
		default:
			pcSrvPriority = _T("");
		}

		HTTPProcessData.Replace(_T("[ed2k]"), ed2k);
		HTTPProcessData.Replace(_T("[ip]"), cur_srv.sServerIP);
		HTTPProcessData.Replace(_T("[port]"), sServerPort);
		HTTPProcessData.Replace(_T("[server-priority]"), pcSrvPriority);

		// DonGato: reduced large server names or descriptions
		if (WSserverColumnHidden[0])
			HTTPProcessData.Replace(_T("[Servername]"), _T(""));
		else if (cur_srv.sServerName.GetLength() > (SHORT_LENGTH)) {
			CString s;
			s.Format(_T("<acronym title=\"%s\">%s...</acronym>"), (LPCTSTR)cur_srv.sServerName, (LPCTSTR)cur_srv.sServerName.Left(SHORT_LENGTH - 3));
			HTTPProcessData.Replace(_T("[Servername]"), s);
		} else
			HTTPProcessData.Replace(_T("[Servername]"), cur_srv.sServerName);

		if (WSserverColumnHidden[1])
			HTTPProcessData.Replace(_T("[Address]"), _T(""));
		else {
			CString sAddr(cur_srv.sServerIP);
			sAddr.AppendFormat(_T(":%d"), cur_srv.nServerPort);
			HTTPProcessData.Replace(_T("[Address]"), sAddr);
		}
		if (WSserverColumnHidden[2])
			HTTPProcessData.Replace(_T("[Description]"), _T(""));
		else if (cur_srv.sServerDescription.GetLength() > SHORT_LENGTH) {
			CString s;
			s.Format(_T("<acronym title=\"%s\">%s...</acronym>"), (LPCTSTR)cur_srv.sServerDescription, (LPCTSTR)cur_srv.sServerDescription.Left(SHORT_LENGTH - 3));
			HTTPProcessData.Replace(_T("[Description]"), s);
		} else
			HTTPProcessData.Replace(_T("[Description]"), cur_srv.sServerDescription);

		if (WSserverColumnHidden[3])
			HTTPProcessData.Replace(_T("[Ping]"), _T(""));
		else {
			CString sPing;
			sPing.Format(_T("%u"), cur_srv.nServerPing);
			HTTPProcessData.Replace(_T("[Ping]"), sPing);
		}

		if (WSserverColumnHidden[4])
			HTTPProcessData.Replace(_T("[Users]"), _T(""));
		else {
			CString sT;
			if (cur_srv.nServerUsers > 0) {
				sT = CastItoIShort(cur_srv.nServerUsers);
				if (cur_srv.nServerMaxUsers > 0)
					sT.AppendFormat(_T(" (%s)"), (LPCTSTR)CastItoIShort(cur_srv.nServerMaxUsers));
			}
			HTTPProcessData.Replace(_T("[Users]"), sT);
		}
		if (WSserverColumnHidden[5] && (cur_srv.nServerFiles > 0))
			HTTPProcessData.Replace(_T("[Files]"), _T(""));
		else
			HTTPProcessData.Replace(_T("[Files]"), CastItoIShort(cur_srv.nServerFiles));
		if (WSserverColumnHidden[6])
			HTTPProcessData.Replace(_T("[Priority]"), _T(""));
		else
			HTTPProcessData.Replace(_T("[Priority]"), cur_srv.sServerPriority);
		if (WSserverColumnHidden[7])
			HTTPProcessData.Replace(_T("[Failed]"), _T(""));
		else {
			CString sFailed;
			sFailed.Format(_T("%d"), cur_srv.nServerFailed);
			HTTPProcessData.Replace(_T("[Failed]"), sFailed);
		}
		if (WSserverColumnHidden[8])
			HTTPProcessData.Replace(_T("[Limit]"), _T(""));
		else {
			CString strTemp(CastItoIShort(cur_srv.nServerSoftLimit));
			strTemp.AppendFormat(_T(" (%s)"), (LPCTSTR)CastItoIShort(cur_srv.nServerHardLimit));
			HTTPProcessData.Replace(_T("[Limit]"), strTemp);
		}
		if (WSserverColumnHidden[9])
			HTTPProcessData.Replace(_T("[Version]"), _T(""));
		else if (cur_srv.sServerVersion.GetLength() > SHORT_LENGTH_MIN) {
			CString s;
			s.Format(_T("<acronym title=\"%s\">%s...</acronym>"), (LPCTSTR)cur_srv.sServerVersion, (LPCTSTR)cur_srv.sServerVersion.Left(SHORT_LENGTH_MIN - 3));
			HTTPProcessData.Replace(_T("[Version]"), s);
		} else
			HTTPProcessData.Replace(_T("[Version]"), cur_srv.sServerVersion);

		HTTPProcessData.Replace(_T("[ServerState]"), cur_srv.sServerState);
		sList += HTTPProcessData;
	}
	Out.Replace(_T("[ServersList]"), sList);
	Out.Replace(_T("[Session]"), sSession);

	return Out;
}

CString CWebServer::_GetTransferList(const ThreadData &Data)
{
	CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	const CString &sSession(_ParseURL(Data.sURL, _T("ses")));
	long lSession = _tstol(sSession);
	bool bAdmin = _IsSessionAdmin(Data, lSession);

	// cat
	int cat;
	const CString &catp(_ParseURL(Data.sURL, _T("cat")));
	if (catp.IsEmpty())
		cat = _GetLastUserCat(Data, lSession);
	else {
		cat = _tstoi(catp);
		_SetLastUserCat(Data, lSession, cat);
	}
	// commands
	CString sCat;
	if (cat != 0)
		sCat.Format(_T("&amp;cat=%i"), cat);

	CString Out;
	if (thePrefs.GetCatCount() > 1)
		_InsertCatBox(Out, cat, _T(""), true, true, sSession, CString());
	else
		Out.Replace(_T("[CATBOX]"), _T(""));


	const CString &sClear(_ParseURL(Data.sURL, _T("clearcompleted")));
	if (bAdmin && !sClear.IsEmpty()) {
		if (sClear.CompareNoCase(_T("all")) == 0)
			theApp.ExecuteWebServerClearCompletedCommand(NULL, cat);
		else if (!sClear.IsEmpty()) {
			uchar FileHash[MDX_DIGEST_SIZE];
			if (strmd4(sClear, FileHash))
				theApp.ExecuteWebServerClearCompletedCommand(sClear, -1);
		}
	}

	CString HTTPTemp(_ParseURL(Data.sURL, _T("ed2k")));

	if (bAdmin && !HTTPTemp.IsEmpty())
		QueueWebDownloadLinks(HTTPTemp, cat);

	HTTPTemp = _ParseURL(Data.sURL, _T("c"));

	if (HTTPTemp == _T("menudown")) {
		int iMenu = _tstol(_ParseURL(Data.sURL, _T("m")));
		WSdownloadColumnHidden[iMenu] = (_tstol(_ParseURL(Data.sURL, _T("v"))) != 0);

		CIni ini(thePrefs.GetConfigFile(), _T("WebServer"));

		_SaveWIConfigArray(WSdownloadColumnHidden, _countof(WSdownloadColumnHidden), _T("downloadColumnHidden"));
	} else if (HTTPTemp == _T("menuup")) {
		int iMenu = _tstol(_ParseURL(Data.sURL, _T("m")));
		WSuploadColumnHidden[iMenu] = (_tstol(_ParseURL(Data.sURL, _T("v"))) != 0);
		_SaveWIConfigArray(WSuploadColumnHidden, _countof(WSuploadColumnHidden), _T("uploadColumnHidden"));
	} else if (HTTPTemp == _T("menuqueue")) {
		int iMenu = _tstol(_ParseURL(Data.sURL, _T("m")));
		WSqueueColumnHidden[iMenu] = (_tstol(_ParseURL(Data.sURL, _T("v"))) != 0);
		_SaveWIConfigArray(WSqueueColumnHidden, _countof(WSqueueColumnHidden), _T("queueColumnHidden"));
	} else if (HTTPTemp == _T("menuprio") && bAdmin) {
		const CString &sPrio(_ParseURL(Data.sURL, _T("p")));
		int prio;
		if (sPrio == _T("low"))
			prio = PR_LOW;
		else if (sPrio == _T("high"))
			prio = PR_HIGH;
		else if (sPrio == _T("normal"))
			prio = PR_NORMAL;
		else //if (sPrio == _T("auto"))
			prio = PR_AUTO; //make auto the default
		theApp.ExecuteWebServerCategoryPriorityCommand(cat, static_cast<uint8>(prio));
	}

	if (bAdmin) {
		const CString &sOp(_ParseURL(Data.sURL, _T("op")));

		if (!sOp.IsEmpty()) {
			const CString &sFile(_ParseURL(Data.sURL, _T("file")));

			if (sFile.IsEmpty()) {
				const CString &sUser(_ParseURL(Data.sURL, _T("userhash")));
				if (sOp == _T("addfriend"))
					ExecuteWebFriendCommand(sUser, true);
				else if (sOp == _T("removefriend"))
					ExecuteWebFriendCommand(sUser, false);
			} else {
				uchar FileHash[MDX_DIGEST_SIZE];
				bool bHash = strmd4(sFile, FileHash);
				if (bHash) {
					if (sOp == _T("setcat")) {
						const CString &newcat(_ParseURL(Data.sURL, _T("filecat")));
						if (!newcat.IsEmpty())
							theApp.ExecuteWebServerDownloadActionCommand(sFile, sOp, _tstol(newcat));
					} else if (sOp == _T("rename")) {
						const CString &sNewName(_ParseURL(Data.sURL, _T("name")));
						if (!sNewName.IsEmpty() && IsValidEd2kString(sNewName))
							pThis->RememberWebTransferRenamePreview(sFile, sNewName);
						theApp.ExecuteWebServerDownloadActionCommand(sFile, sOp, 0, sNewName);
					} else
						theApp.ExecuteWebServerDownloadActionCommand(sFile, sOp);
				}
			}
		}
	}

	HTTPTemp = _ParseURL(Data.sURL, _T("sortreverse"));
	const CString &sSort(_ParseURL(Data.sURL, _T("sort")));

	if (!sSort.IsEmpty()) {
		bool bDirection = false;
		if (sSort == _T("dstate"))
			pThis->m_Params.DownloadSort = DOWN_SORT_STATE;
		else if (sSort == _T("dtype"))
			pThis->m_Params.DownloadSort = DOWN_SORT_TYPE;
		else if (sSort == _T("dname")) {
			pThis->m_Params.DownloadSort = DOWN_SORT_NAME;
			bDirection = true;
		} else if (sSort == _T("dsize"))
			pThis->m_Params.DownloadSort = DOWN_SORT_SIZE;
		else if (sSort == _T("dtransferred"))
			pThis->m_Params.DownloadSort = DOWN_SORT_TRANSFERRED;
		else if (sSort == _T("dspeed"))
			pThis->m_Params.DownloadSort = DOWN_SORT_SPEED;
		else if (sSort == _T("dprogress"))
			pThis->m_Params.DownloadSort = DOWN_SORT_PROGRESS;
		else if (sSort == _T("dsources"))
			pThis->m_Params.DownloadSort = DOWN_SORT_SOURCES;
		else if (sSort == _T("dpriority"))
			pThis->m_Params.DownloadSort = DOWN_SORT_PRIORITY;
		else if (sSort == _T("dcategory")) {
			pThis->m_Params.DownloadSort = DOWN_SORT_CATEGORY;
			bDirection = true;
		} else if (sSort == _T("uuser")) {
			pThis->m_Params.UploadSort = UP_SORT_USER;
			bDirection = true;
		} else if (sSort == _T("uclient"))
			pThis->m_Params.UploadSort = UP_SORT_CLIENT;
		else if (sSort == _T("uversion"))
			pThis->m_Params.UploadSort = UP_SORT_VERSION;
		else if (sSort == _T("ufilename")) {
			pThis->m_Params.UploadSort = UP_SORT_FILENAME;
			bDirection = true;
		} else if (sSort == _T("utransferred"))
			pThis->m_Params.UploadSort = UP_SORT_TRANSFERRED;
		else if (sSort == _T("uspeed"))
			pThis->m_Params.UploadSort = UP_SORT_SPEED;
		else if (sSort == _T("qclient"))
			pThis->m_Params.QueueSort = QU_SORT_CLIENT;
		else if (sSort == _T("quser")) {
			pThis->m_Params.QueueSort = QU_SORT_USER;
			bDirection = true;
		} else if (sSort == _T("qversion"))
			pThis->m_Params.QueueSort = QU_SORT_VERSION;
		else if (sSort == _T("qfilename")) {
			pThis->m_Params.QueueSort = QU_SORT_FILENAME;
			bDirection = true;
		} else if (sSort == _T("qscore"))
			pThis->m_Params.QueueSort = QU_SORT_SCORE;

		if (!HTTPTemp.IsEmpty())
			bDirection = (HTTPTemp.CompareNoCase(_T("true")) == 0);

		switch (sSort[0]) {
		case _T('d'):
			pThis->m_Params.bDownloadSortReverse = bDirection;
			break;
		case _T('u'):
			pThis->m_Params.bUploadSortReverse = bDirection;
			break;
		case _T('q'):
			pThis->m_Params.bQueueSortReverse = bDirection;
		}
	}

	_SetBoolean(pThis->m_Params.bShowUploadQueue, Data.sURL, _T("showuploadqueue"));
	_SetBoolean(pThis->m_Params.bShowUploadQueueBanned, Data.sURL, _T("showuploadqueuebanned"));
	_SetBoolean(pThis->m_Params.bShowUploadQueueFriend, Data.sURL, _T("showuploadqueuefriend"));

	Out += pThis->m_Templates.sTransferImages;
	Out += pThis->m_Templates.sTransferList;
	Out.Replace(_T("[DownloadHeader]"), pThis->m_Templates.sTransferDownHeader);
	Out.Replace(_T("[DownloadFooter]"), pThis->m_Templates.sTransferDownFooter);
	Out.Replace(_T("[UploadHeader]"), pThis->m_Templates.sTransferUpHeader);
	Out.Replace(_T("[UploadFooter]"), pThis->m_Templates.sTransferUpFooter);
	_InsertCatBox(Out, cat, pThis->m_Templates.sCatArrow, true, true, sSession, _T(""));

	HTTPTemp = (pThis->m_Params.bDownloadSortReverse) ? _T("&amp;sortreverse=false") : _T("&amp;sortreverse=true");

	if (pThis->m_Params.DownloadSort == DOWN_SORT_STATE)
		Out.Replace(_T("[SortDState]"), HTTPTemp);
	else
		Out.Replace(_T("[SortDState]"), _T(""));
	if (pThis->m_Params.DownloadSort == DOWN_SORT_TYPE)
		Out.Replace(_T("[SortDType]"), HTTPTemp);
	else
		Out.Replace(_T("[SortDType]"), _T(""));
	if (pThis->m_Params.DownloadSort == DOWN_SORT_NAME)
		Out.Replace(_T("[SortDName]"), HTTPTemp);
	else
		Out.Replace(_T("[SortDName]"), _T(""));
	if (pThis->m_Params.DownloadSort == DOWN_SORT_SIZE)
		Out.Replace(_T("[SortDSize]"), HTTPTemp);
	else
		Out.Replace(_T("[SortDSize]"), _T(""));
	if (pThis->m_Params.DownloadSort == DOWN_SORT_TRANSFERRED)
		Out.Replace(_T("[SortDTransferred]"), HTTPTemp);
	else
		Out.Replace(_T("[SortDTransferred]"), _T(""));
	if (pThis->m_Params.DownloadSort == DOWN_SORT_SPEED)
		Out.Replace(_T("[SortDSpeed]"), HTTPTemp);
	else
		Out.Replace(_T("[SortDSpeed]"), _T(""));
	if (pThis->m_Params.DownloadSort == DOWN_SORT_PROGRESS)
		Out.Replace(_T("[SortDProgress]"), HTTPTemp);
	else
		Out.Replace(_T("[SortDProgress]"), _T(""));
	if (pThis->m_Params.DownloadSort == DOWN_SORT_SOURCES)
		Out.Replace(_T("[SortDSources]"), HTTPTemp);
	else
		Out.Replace(_T("[SortDSources]"), _T(""));
	if (pThis->m_Params.DownloadSort == DOWN_SORT_PRIORITY)
		Out.Replace(_T("[SortDPriority]"), HTTPTemp);
	else
		Out.Replace(_T("[SortDPriority]"), _T(""));
	if (pThis->m_Params.DownloadSort == DOWN_SORT_CATEGORY)
		Out.Replace(_T("[SortDCategory]"), HTTPTemp);
	else
		Out.Replace(_T("[SortDCategory]"), _T(""));

	HTTPTemp = (pThis->m_Params.bUploadSortReverse) ? _T("&amp;sortreverse=false") : _T("&amp;sortreverse=true");

	if (pThis->m_Params.UploadSort == UP_SORT_CLIENT)
		Out.Replace(_T("[SortUClient]"), HTTPTemp);
	else
		Out.Replace(_T("[SortUClient]"), _T(""));
	if (pThis->m_Params.UploadSort == UP_SORT_USER)
		Out.Replace(_T("[SortUUser]"), HTTPTemp);
	else
		Out.Replace(_T("[SortUUser]"), _T(""));
	if (pThis->m_Params.UploadSort == UP_SORT_VERSION)
		Out.Replace(_T("[SortUVersion]"), HTTPTemp);
	else
		Out.Replace(_T("[SortUVersion]"), _T(""));
	if (pThis->m_Params.UploadSort == UP_SORT_FILENAME)
		Out.Replace(_T("[SortUFilename]"), HTTPTemp);
	else
		Out.Replace(_T("[SortUFilename]"), _T(""));
	if (pThis->m_Params.UploadSort == UP_SORT_TRANSFERRED)
		Out.Replace(_T("[SortUTransferred]"), HTTPTemp);
	else
		Out.Replace(_T("[SortUTransferred]"), _T(""));
	if (pThis->m_Params.UploadSort == UP_SORT_SPEED)
		Out.Replace(_T("[SortUSpeed]"), HTTPTemp);
	else
		Out.Replace(_T("[SortUSpeed]"), _T(""));

	CString sSortIcon = _WebSelectString(pThis->m_Params.bDownloadSortReverse, pThis->m_Templates.sUpArrow, pThis->m_Templates.sDownArrow);
		LPCTSTR pcSortIcon = sSortIcon;

	_GetPlainResString(HTTPTemp, _T("DL_FILENAME"));
	if (WSdownloadColumnHidden[0]) {
		Out.Replace(_T("[DFilenameI]"), _T(""));
		Out.Replace(_T("[DFilename]"), _T(""));
	} else {
		Out.Replace(_T("[DFilenameI]"), _WebSelectString(pThis->m_Params.DownloadSort == DOWN_SORT_NAME, pcSortIcon, _T("")));
		Out.Replace(_T("[DFilename]"), HTTPTemp);
	}
	Out.Replace(_T("[DFilenameM]"), HTTPTemp);

	_GetPlainResString(HTTPTemp, _T("DL_SIZE"));
	if (WSdownloadColumnHidden[1]) {
		Out.Replace(_T("[DSizeI]"), _T(""));
		Out.Replace(_T("[DSize]"), _T(""));
	} else {
		Out.Replace(_T("[DSizeI]"), _WebSelectString(pThis->m_Params.DownloadSort == DOWN_SORT_SIZE, pcSortIcon, _T("")));
		Out.Replace(_T("[DSize]"), HTTPTemp);
	}
	Out.Replace(_T("[DSizeM]"), HTTPTemp);

	_GetPlainResString(HTTPTemp, _T("DL_TRANSFCOMPL"));
	if (WSdownloadColumnHidden[2]) {
		Out.Replace(_T("[DTransferredI]"), _T(""));
		Out.Replace(_T("[DTransferred]"), _T(""));
	} else {
		Out.Replace(_T("[DTransferredI]"), _WebSelectString(pThis->m_Params.DownloadSort == DOWN_SORT_TRANSFERRED, pcSortIcon, _T("")));
		Out.Replace(_T("[DTransferred]"), HTTPTemp);
	}
	Out.Replace(_T("[DTransferredM]"), HTTPTemp);

	_GetPlainResString(HTTPTemp, _T("DL_PROGRESS"));
	if (WSdownloadColumnHidden[3]) {
		Out.Replace(_T("[DProgressI]"), _T(""));
		Out.Replace(_T("[DProgress]"), _T(""));
	} else {
		Out.Replace(_T("[DProgressI]"), _WebSelectString(pThis->m_Params.DownloadSort == DOWN_SORT_PROGRESS, pcSortIcon, _T("")));
		Out.Replace(_T("[DProgress]"), HTTPTemp);
	}
	Out.Replace(_T("[DProgressM]"), HTTPTemp);

	_GetPlainResString(HTTPTemp, _T("DL_SPEED"));
	if (WSdownloadColumnHidden[4]) {
		Out.Replace(_T("[DSpeedI]"), _T(""));
		Out.Replace(_T("[DSpeed]"), _T(""));
	} else {
		Out.Replace(_T("[DSpeedI]"), _WebSelectString(pThis->m_Params.DownloadSort == DOWN_SORT_SPEED, pcSortIcon, _T("")));
		Out.Replace(_T("[DSpeed]"), HTTPTemp);
	}
	Out.Replace(_T("[DSpeedM]"), HTTPTemp);

	_GetPlainResString(HTTPTemp, _T("DL_SOURCES"));
	if (WSdownloadColumnHidden[5]) {
		Out.Replace(_T("[DSourcesI]"), _T(""));
		Out.Replace(_T("[DSources]"), _T(""));
	} else {
		Out.Replace(_T("[DSourcesI]"), _WebSelectString(pThis->m_Params.DownloadSort == DOWN_SORT_SOURCES, pcSortIcon, _T("")));
		Out.Replace(_T("[DSources]"), HTTPTemp);
	}
	Out.Replace(_T("[DSourcesM]"), HTTPTemp);

	_GetPlainResString(HTTPTemp, _T("PRIORITY"));
	if (WSdownloadColumnHidden[6]) {
		Out.Replace(_T("[DPriorityI]"), _T(""));
		Out.Replace(_T("[DPriority]"), _T(""));
	} else {
		Out.Replace(_T("[DPriorityI]"), _WebSelectString(pThis->m_Params.DownloadSort == DOWN_SORT_PRIORITY, pcSortIcon, _T("")));
		Out.Replace(_T("[DPriority]"), HTTPTemp);
	}
	Out.Replace(_T("[DPriorityM]"), HTTPTemp);

	_GetPlainResString(HTTPTemp, _T("CAT"));
	if (WSdownloadColumnHidden[7]) {
		Out.Replace(_T("[DCategoryI]"), _T(""));
		Out.Replace(_T("[DCategory]"), _T(""));
	} else {
		Out.Replace(_T("[DCategoryI]"), _WebSelectString(pThis->m_Params.DownloadSort == DOWN_SORT_CATEGORY, pcSortIcon, _T("")));
		Out.Replace(_T("[DCategory]"), HTTPTemp);
	}
	Out.Replace(_T("[DCategoryM]"), HTTPTemp);

	// add 8th columns here

	sSortIcon = _WebSelectString(pThis->m_Params.bUploadSortReverse, pThis->m_Templates.sUpArrow, pThis->m_Templates.sDownArrow);
		pcSortIcon = sSortIcon;

	_GetPlainResString(HTTPTemp, _T("QL_USERNAME"));
	if (WSuploadColumnHidden[0]) {
		Out.Replace(_T("[UUserI]"), _T(""));
		Out.Replace(_T("[UUser]"), _T(""));
	} else {
		Out.Replace(_T("[UUserI]"), _WebSelectString(pThis->m_Params.UploadSort == UP_SORT_USER, pcSortIcon, _T("")));
		Out.Replace(_T("[UUser]"), HTTPTemp);
	}
	Out.Replace(_T("[UUserM]"), HTTPTemp);

	_GetPlainResString(HTTPTemp, _T("CD_VERSION"));
	if (WSuploadColumnHidden[1]) {
		Out.Replace(_T("[UVersionI]"), _T(""));
		Out.Replace(_T("[UVersion]"), _T(""));
	} else {
		Out.Replace(_T("[UVersionI]"), _WebSelectString(pThis->m_Params.UploadSort == UP_SORT_VERSION, pcSortIcon, _T("")));
		Out.Replace(_T("[UVersion]"), HTTPTemp);
	}
	Out.Replace(_T("[UVersionM]"), HTTPTemp);

	_GetPlainResString(HTTPTemp, _T("DL_FILENAME"));
	if (WSuploadColumnHidden[2]) {
		Out.Replace(_T("[UFilenameI]"), _T(""));
		Out.Replace(_T("[UFilename]"), _T(""));
	} else {
		Out.Replace(_T("[UFilenameI]"), _WebSelectString(pThis->m_Params.UploadSort == UP_SORT_FILENAME, pcSortIcon, _T("")));
		Out.Replace(_T("[UFilename]"), HTTPTemp);
	}
	Out.Replace(_T("[UFilenameM]"), HTTPTemp);

	_GetPlainResString(HTTPTemp, _T("STATS_SRATIO"));
	if (WSuploadColumnHidden[3]) {
		Out.Replace(_T("[UTransferredI]"), _T(""));
		Out.Replace(_T("[UTransferred]"), _T(""));
	} else {
		Out.Replace(_T("[UTransferredI]"), _WebSelectString(pThis->m_Params.UploadSort == UP_SORT_TRANSFERRED, pcSortIcon, _T("")));
		Out.Replace(_T("[UTransferred]"), HTTPTemp);
	}
	Out.Replace(_T("[UTransferredM]"), HTTPTemp);

	_GetPlainResString(HTTPTemp, _T("DL_SPEED"));
	if (WSuploadColumnHidden[4]) {
		Out.Replace(_T("[USpeedI]"), _T(""));
		Out.Replace(_T("[USpeed]"), _T(""));
	} else {
		Out.Replace(_T("[USpeedI]"), _WebSelectString(pThis->m_Params.UploadSort == UP_SORT_SPEED, pcSortIcon, _T("")));
		Out.Replace(_T("[USpeed]"), HTTPTemp);
	}
	Out.Replace(_T("[USpeedM]"), HTTPTemp);

	Out.Replace(_T("[DownloadList]"), _GetPlainResString(_T("TW_DOWNLOADS")));
	Out.Replace(_T("[UploadList]"), _GetPlainResString(_T("TW_UPLOADS")));
	Out.Replace(_T("[Actions]"), _GetPlainResString(_T("WEB_ACTIONS")));
	Out.Replace(_T("[TotalDown]"), _GetPlainResString(_T("INFLST_USER_TOTALDOWNLOAD")));
	Out.Replace(_T("[TotalUp]"), _GetPlainResString(_T("INFLST_USER_TOTALUPLOAD")));
	Out.Replace(_T("[admin]"), _WebSelectString(bAdmin, _T("admin"), _T("")));
	_InsertCatBox(Out, cat, _T(""), true, true, sSession, _T(""));

	WebTransferSnapshot transferSnapshot;
	if (!_GetTransferSnapshot(pThis, Data, cat, transferSnapshot))
		transferSnapshot.bStale = true;

	_MakeTransferList(Out, pThis, Data, transferSnapshot, bAdmin);

	Out.Replace(_T("[Session]"), sSession);
	Out.Replace(_T("[CatSel]"), sCat);

	return Out;
}

void CWebServer::_MakeTransferList(CString &Out, CWebServer *pThis, const ThreadData &Data, const WebTransferSnapshot &snapshot, bool bAdmin)
{
	const CArray<DownloadFiles> *FilesArray = &snapshot.FilesArray;
	const CArray<UploadUsers> *UploadArray = &snapshot.UploadArray;
	const CQArray<QueueUsers, QueueUsers> &QueueArray = snapshot.QueueArray;

	const int nCountQueue = snapshot.nCountQueue;
	const int nCountQueueBanned = snapshot.nCountQueueBanned;
	const int nCountQueueFriend = snapshot.nCountQueueFriend;
	const int nCountQueueSecure = snapshot.nCountQueueSecure;
	const int nCountQueueBannedSecure = snapshot.nCountQueueBannedSecure;
	const int nCountQueueFriendSecure = snapshot.nCountQueueFriendSecure;

	CString HTTPProcessData;
	CString sDownList, HTTPTemp;
	LPCTSTR pcTmp;
	double fTotalSize = 0, fTotalTransferred = 0, fTotalSpeed = 0;

	CString OutE(pThis->m_Templates.sTransferDownLine);
	for (INT_PTR i = 0; i < FilesArray->GetCount(); ++i) {
		const DownloadFiles &downf((*FilesArray)[i]);
		HTTPProcessData = OutE;

		pcTmp = (downf.sFileHash == _ParseURL(Data.sURL, _T("file"))) ? _T("checked") : _T("checked_no");
		HTTPProcessData.Replace(_T("[LastChangedDataset]"), pcTmp);


		CString strFinfo(downf.sFileInfo);
		strFinfo.Replace(_T("\\"), _T("\\\\"));
		CString strFileInfo(strFinfo);

		strFinfo.Replace(_T("'"), _T("&#8217;"));
		strFinfo.Replace(_T("\n"), _T("\\n"));

		strFileInfo.Replace(_T("\n"), _T("<br>"));

		if (!downf.iComment) {
			HTTPProcessData.Replace(_T("[HASCOMMENT]"), _T("<!--"));
			HTTPProcessData.Replace(_T("[HASCOMMENT_END]"), _T("-->"));
		} else {
			HTTPProcessData.Replace(_T("[HASCOMMENT]"), _T(""));
			HTTPProcessData.Replace(_T("[HASCOMMENT_END]"), _T(""));
		}

		if (downf.sFileState.CompareNoCase(_T("downloading")) == 0 || downf.sFileState.CompareNoCase(_T("waiting")) == 0) {
			HTTPProcessData.Replace(_T("[ISACTIVE]"), _T("<!--"));
			HTTPProcessData.Replace(_T("[ISACTIVE_END]"), _T("-->"));
			HTTPProcessData.Replace(_T("[!ISACTIVE]"), _T(""));
			HTTPProcessData.Replace(_T("[!ISACTIVE_END]"), _T(""));
		} else {
			HTTPProcessData.Replace(_T("[ISACTIVE]"), _T(""));
			HTTPProcessData.Replace(_T("[ISACTIVE_END]"), _T(""));
			HTTPProcessData.Replace(_T("[!ISACTIVE]"), _T("<!--"));
			HTTPProcessData.Replace(_T("[!ISACTIVE_END]"), _T("-->"));
		}

		CString ed2k(downf.sED2kLink); //ed2klink
		ed2k.Replace(_T("'"), _T("&#8217;"));
		CString fsize; //file size
		fsize.Format(_T("%I64u"), downf.m_qwFileSize);
		const CString &session(_ParseURL(Data.sURL, _T("ses")));

		CString isgetflc; //getflc
		if (!downf.bIsPreview)
			isgetflc = downf.bIsGetFLC ? _T("enabled") : _T("disabled");

		//priority
		if (downf.bFileAutoPrio)
			pcTmp = _T("Auto");
		else {
			switch (downf.nFilePrio) {
			case 0:
				pcTmp = _T("Low");
				break;
			case 1:
				pcTmp = _T("Normal");
				break;
			case 2:
				pcTmp = _T("High");
				break;
			default:
				pcTmp = _T("");
			}
		}

		HTTPProcessData.Replace(_T("[admin]"), _WebSelectString(bAdmin, _T("admin"), _T("")));
		HTTPProcessData.Replace(_T("[finfo]"), strFinfo);
		HTTPProcessData.Replace(_T("[fcomments]"), _WebSelectString(downf.iComment != 0, _T("yes"), _T("")));
		HTTPProcessData.Replace(_T("[ed2k]"), _SpecialChars(ed2k));
		HTTPProcessData.Replace(_T("[DownState]"), downf.sFileState);
		HTTPProcessData.Replace(_T("[isgetflc]"), isgetflc);
		HTTPProcessData.Replace(_T("[fname]"), _SpecialChars(downf.sFileNameJS));
		HTTPProcessData.Replace(_T("[fsize]"), fsize);
		HTTPProcessData.Replace(_T("[session]"), session);
		HTTPProcessData.Replace(_T("[filehash]"), downf.sFileHash);
		HTTPProcessData.Replace(_T("[down-priority]"), pcTmp);
		HTTPProcessData.Replace(_T("[FileType]"), downf.sFileType);
		HTTPProcessData.Replace(_T("[downloadable]"), _WebSelectString(bAdmin && (thePrefs.GetMaxWebUploadFileSizeMB() == 0 || downf.m_qwFileSize < ((uint64)thePrefs.GetMaxWebUploadFileSizeMB()) * 1024 * 1024), _T("yes"), _T("no")));

		// comment icon
		switch (downf.iComment) {
		case 1:
			pcTmp = _T("cmtgood");
			break;
		case 2:
			pcTmp = _T("cmtbad");
			break;
		//case 0:
		default:
			pcTmp = _T("none");
		}
		HTTPProcessData.Replace(_T("[FileCommentIcon]"), pcTmp);

		pcTmp = (!downf.bIsPreview && downf.bIsGetFLC) ? _T("getflc") : _T("halfnone");
		HTTPProcessData.Replace(_T("[FileIsGetFLC]"), pcTmp);

		if (WSdownloadColumnHidden[0])
			HTTPProcessData.Replace(_T("[ShortFileName]"), _T(""));
		else if (downf.sFileName.GetLength() > (SHORT_LENGTH_MAX))
			HTTPProcessData.Replace(_T("[ShortFileName]"), downf.sFileName.Left(SHORT_LENGTH_MAX - 3) + _T("..."));
		else
			HTTPProcessData.Replace(_T("[ShortFileName]"), downf.sFileName);

		HTTPProcessData.Replace(_T("[FileInfo]"), strFileInfo);
		fTotalSize += downf.m_qwFileSize;

		HTTPProcessData.Replace(_T("[2]"), _WebSelectString(WSdownloadColumnHidden[1] != FALSE, _T(""), CastItoXBytes(downf.m_qwFileSize)));

		if (WSdownloadColumnHidden[2])
			HTTPProcessData.Replace(_T("[3]"), _T(""));
		else if (downf.m_qwFileTransferred > 0) {
			fTotalTransferred += downf.m_qwFileTransferred;
			HTTPProcessData.Replace(_T("[3]"), CastItoXBytes(downf.m_qwFileTransferred));
		} else
			HTTPProcessData.Replace(_T("[3]"), _T("-"));

		HTTPProcessData.Replace(_T("[DownloadBar]"), _WebSelectString(WSdownloadColumnHidden[3] != FALSE, _T(""), downf.sDownloadBar));

		if (WSdownloadColumnHidden[4])
			pcTmp = _T("");
		else if (downf.lFileSpeed > 0) {
			fTotalSpeed += downf.lFileSpeed;
			HTTPTemp.Format(_T("%8.2f"), downf.lFileSpeed / 1024.0);
			pcTmp = HTTPTemp;
		} else
			pcTmp = _T("-");
		HTTPProcessData.Replace(_T("[4]"), pcTmp);

		if (WSdownloadColumnHidden[5])
			pcTmp = _T("");
		else if (downf.lSourceCount > 0) {
			HTTPTemp.Format(_T("%li&nbsp;/&nbsp;%8li&nbsp;(%li)"),
				downf.lSourceCount - downf.lNotCurrentSourceCount,
				downf.lSourceCount,
				downf.lTransferringSourceCount);
			pcTmp = HTTPTemp;
		} else
			pcTmp = _T("-");
		HTTPProcessData.Replace(_T("[5]"), pcTmp);

		if (WSdownloadColumnHidden[6] || downf.nFilePrio < 0 || downf.nFilePrio > 2)
			HTTPProcessData.Replace(_T("[PrioVal]"), _T(""));
		else {
			static const LPCTSTR uprio[2][3] =
			{
				{_T("PRIOLOW"), _T("PRIONORMAL"), _T("PRIOHIGH")},
				{_T("PRIOAUTOLOW"), _T("PRIOAUTONORMAL"), _T("PRIOAUTOHIGH")}
			};
			HTTPProcessData.Replace(_T("[PrioVal]"), GetResString(uprio[static_cast<unsigned>(downf.bFileAutoPrio)][downf.nFilePrio]));
		}

		pcTmp = WSdownloadColumnHidden[7] ? _T("") : (LPCTSTR)downf.sCategory;
		HTTPProcessData.Replace(_T("[Category]"), pcTmp);

		_InsertCatBox(HTTPProcessData, downf.iCategory, _T(""), false, false, session, downf.sFileHash, false, downf.iCategory);

		sDownList += HTTPProcessData;
	}

	Out.Replace(_T("[DownloadFilesList]"), sDownList);
	Out.Replace(_T("[TotalDownSize]"), CastItoXBytes(fTotalSize));

	Out.Replace(_T("[TotalDownTransferred]"), CastItoXBytes(fTotalTransferred));

	HTTPTemp.Format(_T("%8.2f"), fTotalSpeed / 1024.0);
	Out.Replace(_T("[TotalDownSpeed]"), HTTPTemp);

	HTTPTemp.Format(_T("%s: %i"), (LPCTSTR)GetResString(_T("SF_FILE")), (int)FilesArray->GetCount());
	Out.Replace(_T("[TotalFiles]"), HTTPTemp);

	HTTPTemp.Format(_T("%i"), pThis->m_Templates.iProgressbarWidth);
	Out.Replace(_T("[PROGRESSBARWIDTHVAL]"), HTTPTemp);

	fTotalSize = fTotalTransferred = fTotalSpeed = 0;

	OutE = pThis->m_Templates.sTransferUpLine;
	OutE.Replace(_T("[admin]"), _WebSelectString(bAdmin, _T("admin"), _T("")));

	CString sUpList;
	for (INT_PTR i = 0; i < UploadArray->GetCount(); ++i) {
		const UploadUsers &ulu((*UploadArray)[i]);
		HTTPProcessData = OutE;

		HTTPProcessData.Replace(_T("[UserHash]"), ulu.sUserHash);
		HTTPProcessData.Replace(_T("[UpState]"), ulu.sActive);
		HTTPProcessData.Replace(_T("[FileInfo]"), ulu.sFileInfo);
		HTTPProcessData.Replace(_T("[ClientState]"), ulu.sClientState);
		HTTPProcessData.Replace(_T("[ClientSoft]"), ulu.sClientSoft);
		HTTPProcessData.Replace(_T("[ClientExtra]"), ulu.sClientExtra);

		pcTmp = WSuploadColumnHidden[0] ? _T("") : (LPCTSTR)ulu.sUserName;
		HTTPProcessData.Replace(_T("[1]"), pcTmp);

		pcTmp = WSuploadColumnHidden[1] ? _T("") : (LPCTSTR)ulu.sClientNameVersion;
		HTTPProcessData.Replace(_T("[ClientSoftV]"), pcTmp);

		pcTmp = WSuploadColumnHidden[2] ? _T("") : (LPCTSTR)ulu.sFileName;
		HTTPProcessData.Replace(_T("[2]"), pcTmp);

		if (WSuploadColumnHidden[3])
			pcTmp = _T("");
		else {
			fTotalSize += ulu.nTransferredDown;
			fTotalTransferred += ulu.nTransferredUp;
			HTTPTemp.Format(_T("%s / %s"), (LPCTSTR)CastItoXBytes(ulu.nTransferredDown), (LPCTSTR)CastItoXBytes(ulu.nTransferredUp));
			pcTmp = HTTPTemp;
		}
		HTTPProcessData.Replace(_T("[3]"), pcTmp);

		if (WSuploadColumnHidden[4])
			pcTmp = _T("");
		else {
			fTotalSpeed += ulu.nDataRate;
			HTTPTemp.Format(_T("%8.2f "), max(ulu.nDataRate / 1024.0, 0.0));
			pcTmp = HTTPTemp;
		}
		HTTPProcessData.Replace(_T("[4]"), pcTmp);

		sUpList += HTTPProcessData;
	}
	Out.Replace(_T("[UploadFilesList]"), sUpList);
	HTTPTemp.Format(_T("%s / %s"), (LPCTSTR)CastItoXBytes(fTotalSize), (LPCTSTR)CastItoXBytes(fTotalTransferred));
	Out.Replace(_T("[TotalUpTransferred]"), HTTPTemp);
	HTTPTemp.Format(_T("%8.2f "), max(fTotalSpeed / 1024, 0.0));
	Out.Replace(_T("[TotalUpSpeed]"), HTTPTemp);

	if (pThis->m_Params.bShowUploadQueue) {
		Out.Replace(_T("[UploadQueue]"), pThis->m_Templates.sTransferUpQueueShow);
		Out.Replace(_T("[UploadQueueList]"), _GetPlainResString(_T("ONQUEUE")));

		OutE = pThis->m_Templates.sTransferUpQueueLine;
		OutE.Replace(_T("[admin]"), _WebSelectString(bAdmin, _T("admin"), _T("")));

		CString sQueue;
		for (INT_PTR i = 0; i < QueueArray.GetCount(); ++i) {
			if (QueueArray[i].sClientExtra == _T("none")) {
				HTTPProcessData = OutE;
				pcTmp = WSqueueColumnHidden[0] ? _T("") : (LPCTSTR)QueueArray[i].sUserName;
				HTTPProcessData.Replace(_T("[UserName]"), pcTmp);

				pcTmp = WSqueueColumnHidden[1] ? _T("") : (LPCTSTR)QueueArray[i].sClientNameVersion;
				HTTPProcessData.Replace(_T("[ClientSoftV]"), pcTmp);

				pcTmp = WSqueueColumnHidden[2] ? _T("") : (LPCTSTR)QueueArray[i].sFileName;
				HTTPProcessData.Replace(_T("[FileName]"), pcTmp);

				TCHAR HTTPTempC[20];
				if (WSqueueColumnHidden[3])
					*HTTPTempC = _T('\0');
				else
					_stprintf(HTTPTempC, _T("%i"), QueueArray[i].nScore);
				HTTPProcessData.Replace(_T("[Score]"), HTTPTempC);
				HTTPProcessData.Replace(_T("[ClientState]"), QueueArray[i].sClientState);
				HTTPProcessData.Replace(_T("[ClientStateSpecial]"), QueueArray[i].sClientStateSpecial);
				HTTPProcessData.Replace(_T("[ClientSoft]"), QueueArray[i].sClientSoft);
				HTTPProcessData.Replace(_T("[ClientExtra]"), QueueArray[i].sClientExtra);
				HTTPProcessData.Replace(_T("[UserHash]"), QueueArray[i].sUserHash);

				sQueue += HTTPProcessData;
			}
		}
		Out.Replace(_T("[QueueList]"), sQueue);
	} else
		Out.Replace(_T("[UploadQueue]"), pThis->m_Templates.sTransferUpQueueHide);

	if (pThis->m_Params.bShowUploadQueueBanned) {
		Out.Replace(_T("[UploadQueueBanned]"), pThis->m_Templates.sTransferUpQueueBannedShow);
		Out.Replace(_T("[UploadQueueBannedList]"), _GetPlainResString(_T("BANNED")));

		OutE = pThis->m_Templates.sTransferUpQueueBannedLine;

		CString sQueueBanned;
		for (INT_PTR i = 0; i < QueueArray.GetCount(); ++i) {
			if (QueueArray[i].sClientExtra == _T("banned")) {
				HTTPProcessData = OutE;
				pcTmp = WSqueueColumnHidden[0] ? _T("") : (LPCTSTR)QueueArray[i].sUserName;
				HTTPProcessData.Replace(_T("[UserName]"), pcTmp);

				pcTmp = WSqueueColumnHidden[1] ? _T("") : (LPCTSTR)QueueArray[i].sClientNameVersion;
				HTTPProcessData.Replace(_T("[ClientSoftV]"), pcTmp);

				pcTmp = WSqueueColumnHidden[2] ? _T("") : (LPCTSTR)QueueArray[i].sFileName;
				HTTPProcessData.Replace(_T("[FileName]"), pcTmp);

				TCHAR HTTPTempC[20];
				if (WSqueueColumnHidden[3])
					*HTTPTempC = _T('\0');
				else
					_stprintf(HTTPTempC, _T("%i"), QueueArray[i].nScore);
				HTTPProcessData.Replace(_T("[Score]"), HTTPTempC);

				HTTPProcessData.Replace(_T("[ClientState]"), QueueArray[i].sClientState);
				HTTPProcessData.Replace(_T("[ClientStateSpecial]"), QueueArray[i].sClientStateSpecial);
				HTTPProcessData.Replace(_T("[ClientSoft]"), QueueArray[i].sClientSoft);
				HTTPProcessData.Replace(_T("[ClientExtra]"), QueueArray[i].sClientExtra);
				HTTPProcessData.Replace(_T("[UserHash]"), QueueArray[i].sUserHash);

				sQueueBanned += HTTPProcessData;
			}
		}
		Out.Replace(_T("[QueueListBanned]"), sQueueBanned);
	} else
		Out.Replace(_T("[UploadQueueBanned]"), pThis->m_Templates.sTransferUpQueueBannedHide);

	if (pThis->m_Params.bShowUploadQueueFriend) {
		Out.Replace(_T("[UploadQueueFriend]"), pThis->m_Templates.sTransferUpQueueFriendShow);
		Out.Replace(_T("[UploadQueueFriendList]"), _GetPlainResString(_T("IRC_ADDTOFRIENDLIST")));

		OutE = pThis->m_Templates.sTransferUpQueueFriendLine;

		CString sQueueFriend;
		for (INT_PTR i = 0; i < QueueArray.GetCount(); ++i) {
			if (QueueArray[i].sClientExtra == _T("friend")) {
				HTTPProcessData = OutE;
				pcTmp = WSqueueColumnHidden[0] ? _T("") : (LPCTSTR)QueueArray[i].sUserName;
				HTTPProcessData.Replace(_T("[UserName]"), pcTmp);

				pcTmp = WSqueueColumnHidden[1] ? _T("") : (LPCTSTR)QueueArray[i].sClientNameVersion;
				HTTPProcessData.Replace(_T("[ClientSoftV]"), pcTmp);

				pcTmp = WSqueueColumnHidden[2] ? _T("") : (LPCTSTR)QueueArray[i].sFileName;
				HTTPProcessData.Replace(_T("[FileName]"), pcTmp);

				TCHAR HTTPTempC[20];
				if (WSqueueColumnHidden[3])
					*HTTPTempC = _T('\0');
				else
					_stprintf(HTTPTempC, _T("%i"), QueueArray[i].nScore);
				HTTPProcessData.Replace(_T("[Score]"), HTTPTempC);

				HTTPProcessData.Replace(_T("[ClientState]"), QueueArray[i].sClientState);
				HTTPProcessData.Replace(_T("[ClientStateSpecial]"), QueueArray[i].sClientStateSpecial);
				HTTPProcessData.Replace(_T("[ClientSoft]"), QueueArray[i].sClientSoft);
				HTTPProcessData.Replace(_T("[ClientExtra]"), QueueArray[i].sClientExtra);
				HTTPProcessData.Replace(_T("[UserHash]"), QueueArray[i].sUserHash);

				sQueueFriend += HTTPProcessData;
			}
		}
		Out.Replace(_T("[QueueListFriend]"), sQueueFriend);
	} else
		Out.Replace(_T("[UploadQueueFriend]"), pThis->m_Templates.sTransferUpQueueFriendHide);


	CString mCounter;
	mCounter.Format(_T("%i"), nCountQueue);
	Out.Replace(_T("[CounterQueue]"), mCounter);
	mCounter.Format(_T("%i"), nCountQueueBanned);
	Out.Replace(_T("[CounterQueueBanned]"), mCounter);
	mCounter.Format(_T("%i"), nCountQueueFriend);
	Out.Replace(_T("[CounterQueueFriend]"), mCounter);
	mCounter.Format(_T("%i"), nCountQueueSecure);
	Out.Replace(_T("[CounterQueueSecure]"), mCounter);
	mCounter.Format(_T("%i"), nCountQueueBannedSecure);
	Out.Replace(_T("[CounterQueueBannedSecure]"), mCounter);
	mCounter.Format(_T("%i"), nCountQueueFriendSecure);
	Out.Replace(_T("[CounterQueueFriendSecure]"), mCounter);
	mCounter.Format(_T("%i"), nCountQueue + nCountQueueBanned + nCountQueueFriend);
	Out.Replace(_T("[CounterAll]"), mCounter);
	mCounter.Format(_T("%i"), nCountQueueSecure + nCountQueueBannedSecure + nCountQueueFriendSecure);
	Out.Replace(_T("[CounterAllSecure]"), mCounter);
	Out.Replace(_T("[ShowUploadQueue]"), _GetPlainResString(_T("VIEWQUEUE")));
	Out.Replace(_T("[ShowUploadQueueList]"), _GetPlainResString(_T("VIEWQUEUE")));

	Out.Replace(_T("[ShowUploadQueueListBanned]"), _GetPlainResString(_T("WEB_SHOW_UPLOAD_QUEUE_BANNED")));
	Out.Replace(_T("[ShowUploadQueueListFriend]"), _GetPlainResString(_T("WEB_SHOW_UPLOAD_QUEUE_FRIEND")));

	CString strTmp(pThis->m_Params.bQueueSortReverse ? _T("&amp;sortreverse=false") : _T("&amp;sortreverse=true"));

	if (pThis->m_Params.QueueSort == QU_SORT_CLIENT)
		Out.Replace(_T("[SortQClient]"), strTmp);
	else
		Out.Replace(_T("[SortQClient]"), _T(""));
	if (pThis->m_Params.QueueSort == QU_SORT_USER)
		Out.Replace(_T("[SortQUser]"), strTmp);
	else
		Out.Replace(_T("[SortQUser]"), _T(""));
	if (pThis->m_Params.QueueSort == QU_SORT_VERSION)
		Out.Replace(_T("[SortQVersion]"), strTmp);
	else
		Out.Replace(_T("[SortQVersion]"), _T(""));
	if (pThis->m_Params.QueueSort == QU_SORT_FILENAME)
		Out.Replace(_T("[SortQFilename]"), strTmp);
	else
		Out.Replace(_T("[SortQFilename]"), _T(""));
	if (pThis->m_Params.QueueSort == QU_SORT_SCORE)
		Out.Replace(_T("[SortQScore]"), strTmp);
	else
		Out.Replace(_T("[SortQScore]"), _T(""));

	CString pcSortIcon(pThis->m_Params.bQueueSortReverse ? pThis->m_Templates.sUpArrow : pThis->m_Templates.sDownArrow);

	_GetPlainResString(strTmp, _T("QL_USERNAME"));
	if (WSqueueColumnHidden[0]) {
		Out.Replace(_T("[UserNameTitleI]"), _T(""));
		Out.Replace(_T("[UserNameTitle]"), _T(""));
	} else {
		if (pThis->m_Params.QueueSort == QU_SORT_USER)
			Out.Replace(_T("[UserNameTitleI]"), pcSortIcon);
		else
			Out.Replace(_T("[UserNameTitleI]"), _T(""));
		Out.Replace(_T("[UserNameTitle]"), strTmp);
	}
	Out.Replace(_T("[UserNameTitleM]"), strTmp);

	_GetPlainResString(strTmp, _T("CD_CSOFT"));
	if (WSqueueColumnHidden[1]) {
		Out.Replace(_T("[VersionI]"), _T(""));
		Out.Replace(_T("[Version]"), _T(""));
	} else {
		if (pThis->m_Params.QueueSort == QU_SORT_VERSION)
			Out.Replace(_T("[VersionI]"), pcSortIcon);
		else
			Out.Replace(_T("[VersionI]"), _T(""));
		Out.Replace(_T("[Version]"), strTmp);
	}
	Out.Replace(_T("[VersionM]"), strTmp);

	_GetPlainResString(strTmp, _T("DL_FILENAME"));
	if (WSqueueColumnHidden[2]) {
		Out.Replace(_T("[FileNameTitleI]"), _T(""));
		Out.Replace(_T("[FileNameTitle]"), _T(""));
	} else {
		if (pThis->m_Params.QueueSort == QU_SORT_FILENAME)
			Out.Replace(_T("[FileNameTitleI]"), pcSortIcon);
		else
			Out.Replace(_T("[FileNameTitleI]"), _T(""));
		Out.Replace(_T("[FileNameTitle]"), strTmp);
	}
	Out.Replace(_T("[FileNameTitleM]"), strTmp);

	_GetPlainResString(strTmp, _T("SCORE"));
	if (WSqueueColumnHidden[3]) {
		Out.Replace(_T("[ScoreTitleI]"), _T(""));
		Out.Replace(_T("[ScoreTitle]"), _T(""));
	} else {
		if (pThis->m_Params.QueueSort == QU_SORT_SCORE)
			Out.Replace(_T("[ScoreTitleI]"), pcSortIcon);
		else
			Out.Replace(_T("[ScoreTitleI]"), _T(""));
		Out.Replace(_T("[ScoreTitle]"), strTmp);
	}
	Out.Replace(_T("[ScoreTitleM]"), strTmp);
}

void CWebServer::_SetBoolean(bool &var, const CString &URL, LPCTSTR pFieldname)
{
	const CString &sBool(_ParseURL(URL, pFieldname));
	if (sBool == _T("true"))
		var = true;
	else if (sBool == _T("false"))
		var = false;
}

CString CWebServer::_GetSharedFilesList(const ThreadData &Data)
{
	CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	const CString &sSession(_ParseURL(Data.sURL, _T("ses")));
	bool bAdmin = _IsSessionAdmin(Data, sSession);
	const CString &strSort(_ParseURL(Data.sURL, _T("sort")));

	CString strTmp(_ParseURL(Data.sURL, _T("sortreverse")));

	if (!strSort.IsEmpty()) {
		bool bDirection = false;

		if (strSort == _T("state"))
			pThis->m_Params.SharedSort = SHARED_SORT_STATE;
		else if (strSort == _T("type"))
			pThis->m_Params.SharedSort = SHARED_SORT_TYPE;
		else if (strSort == _T("name")) {
			pThis->m_Params.SharedSort = SHARED_SORT_NAME;
			bDirection = true;
		} else if (strSort == _T("size"))
			pThis->m_Params.SharedSort = SHARED_SORT_SIZE;
		else if (strSort == _T("transferred"))
			pThis->m_Params.SharedSort = SHARED_SORT_TRANSFERRED;
		else if (strSort == _T("alltimetransferred"))
			pThis->m_Params.SharedSort = SHARED_SORT_ALL_TIME_TRANSFERRED;
		else if (strSort == _T("requests"))
			pThis->m_Params.SharedSort = SHARED_SORT_REQUESTS;
		else if (strSort == _T("alltimerequests"))
			pThis->m_Params.SharedSort = SHARED_SORT_ALL_TIME_REQUESTS;
		else if (strSort == _T("accepts"))
			pThis->m_Params.SharedSort = SHARED_SORT_ACCEPTS;
		else if (strSort == _T("alltimeaccepts"))
			pThis->m_Params.SharedSort = SHARED_SORT_ALL_TIME_ACCEPTS;
		else if (strSort == _T("completes"))
			pThis->m_Params.SharedSort = SHARED_SORT_COMPLETES;
		else if (strSort == _T("priority"))
			pThis->m_Params.SharedSort = SHARED_SORT_PRIORITY;

		if (strTmp.IsEmpty())
			pThis->m_Params.bSharedSortReverse = bDirection;
	}
	if (!strTmp.IsEmpty())
		pThis->m_Params.bSharedSortReverse = (strTmp == _T("true"));

	if (bAdmin) {
		CString hash(_ParseURL(Data.sURL, _T("hash")));
		const CString &sPrio(_ParseURL(Data.sURL, _T("prio")));
		if (!hash.IsEmpty() && !sPrio.IsEmpty())
			QueueWebSharedFilesPriority(hash, GetWebSharedFilesPriorityAction(sPrio));
	}

	if (_ParseURL(Data.sURL, _T("c")) == _T("menu")) {
		int iMenu = _tstoi(_ParseURL(Data.sURL, _T("m")));
		bool bValue = _tstoi(_ParseURL(Data.sURL, _T("v"))) != 0;
		WSsharedColumnHidden[iMenu] = bValue;
		_SaveWIConfigArray(WSsharedColumnHidden, _countof(WSsharedColumnHidden), _T("sharedColumnHidden"));
	}
	if (_ParseURL(Data.sURL, _T("reload")) == _T("true"))
		QueueWebSharedFilesReload();

	strTmp = (pThis->m_Params.bSharedSortReverse) ? _T("false") : _T("true");

	CString Out(pThis->m_Templates.sSharedList);
	//State sorting link
	if (pThis->m_Params.SharedSort == SHARED_SORT_STATE)
		Out.Replace(_T("[SortState]"), _T("sort=state&amp;sortreverse=") + strTmp);
	else
		Out.Replace(_T("[SortState]"), _T("sort=state"));
	//Type sorting link
	if (pThis->m_Params.SharedSort == SHARED_SORT_TYPE)
		Out.Replace(_T("[SortType]"), _T("sort=type&amp;sortreverse=") + strTmp);
	else
		Out.Replace(_T("[SortType]"), _T("sort=type"));
	//Name sorting link
	if (pThis->m_Params.SharedSort == SHARED_SORT_NAME)
		Out.Replace(_T("[SortName]"), _T("sort=name&amp;sortreverse=") + strTmp);
	else
		Out.Replace(_T("[SortName]"), _T("sort=name"));
	//Size sorting Link
	if (pThis->m_Params.SharedSort == SHARED_SORT_SIZE)
		Out.Replace(_T("[SortSize]"), _T("sort=size&amp;sortreverse=") + strTmp);
	else
		Out.Replace(_T("[SortSize]"), _T("sort=size"));
	//Complete Sources sorting Link
	if (pThis->m_Params.SharedSort == SHARED_SORT_COMPLETES)
		Out.Replace(_T("[SortCompletes]"), _T("sort=completes&amp;sortreverse=") + strTmp);
	else
		Out.Replace(_T("[SortCompletes]"), _T("sort=completes"));
	//Priority sorting Link
	if (pThis->m_Params.SharedSort == SHARED_SORT_PRIORITY)
		Out.Replace(_T("[SortPriority]"), _T("sort=priority&amp;sortreverse=") + strTmp);
	else
		Out.Replace(_T("[SortPriority]"), _T("sort=priority"));
	//Transferred sorting link
	if (pThis->m_Params.SharedSort == SHARED_SORT_TRANSFERRED) {
		if (pThis->m_Params.bSharedSortReverse)
			Out.Replace(_T("[SortTransferred]"), _T("sort=alltimetransferred&amp;sortreverse=") + strTmp);
		else
			Out.Replace(_T("[SortTransferred]"), _T("sort=transferred&amp;sortreverse=") + strTmp);
	} else if (pThis->m_Params.SharedSort == SHARED_SORT_ALL_TIME_TRANSFERRED) {
		if (pThis->m_Params.bSharedSortReverse)
			Out.Replace(_T("[SortTransferred]"), _T("sort=transferred&amp;sortreverse=") + strTmp);
		else
			Out.Replace(_T("[SortTransferred]"), _T("sort=alltimetransferred&amp;sortreverse=") + strTmp);
	} else
		Out.Replace(_T("[SortTransferred]"), _T("&amp;sort=transferred&amp;sortreverse=false"));
	//Request sorting link
	if (pThis->m_Params.SharedSort == SHARED_SORT_REQUESTS) {
		if (pThis->m_Params.bSharedSortReverse)
			Out.Replace(_T("[SortRequests]"), _T("sort=alltimerequests&amp;sortreverse=") + strTmp);
		else
			Out.Replace(_T("[SortRequests]"), _T("sort=requests&amp;sortreverse=") + strTmp);
	} else if (pThis->m_Params.SharedSort == SHARED_SORT_ALL_TIME_REQUESTS) {
		if (pThis->m_Params.bSharedSortReverse)
			Out.Replace(_T("[SortRequests]"), _T("sort=requests&amp;sortreverse=") + strTmp);
		else
			Out.Replace(_T("[SortRequests]"), _T("sort=alltimerequests&amp;sortreverse=") + strTmp);
	} else
		Out.Replace(_T("[SortRequests]"), _T("&amp;sort=requests&amp;sortreverse=false"));
	//Accepts sorting link
	if (pThis->m_Params.SharedSort == SHARED_SORT_ACCEPTS) {
		if (pThis->m_Params.bSharedSortReverse)
			Out.Replace(_T("[SortAccepts]"), _T("sort=alltimeaccepts&amp;sortreverse=") + strTmp);
		else
			Out.Replace(_T("[SortAccepts]"), _T("sort=accepts&amp;sortreverse=") + strTmp);
	} else if (pThis->m_Params.SharedSort == SHARED_SORT_ALL_TIME_ACCEPTS) {
		if (pThis->m_Params.bSharedSortReverse)
			Out.Replace(_T("[SortAccepts]"), _T("sort=accepts&amp;sortreverse=") + strTmp);
		else
			Out.Replace(_T("[SortAccepts]"), _T("sort=alltimeaccepts&amp;sortreverse=") + strTmp);
	} else
		Out.Replace(_T("[SortAccepts]"), _T("&amp;sort=accepts&amp;sortreverse=false"));

	if (_ParseURL(Data.sURL, _T("reload")) == _T("true")) {
		//Pick-up last line of the log
		CString strResultLog(_SpecialChars(theApp.emuledlg->GetLastLogEntry().TrimRight(_T('\n'))));
		int iStringIndex = strResultLog.ReverseFind(_T('\n'));
		if (iStringIndex > 0)
			strResultLog.Delete(0, iStringIndex);
		Out.Replace(_T("[Message]"), strResultLog);
	} else
		Out.Replace(_T("[Message]"), _T(""));

	CString sSortIcon = _WebSelectString(pThis->m_Params.bSharedSortReverse, pThis->m_Templates.sUpArrow, pThis->m_Templates.sDownArrow);
		LPCTSTR pcSortIcon = sSortIcon;

	_GetPlainResString(strTmp, _T("DL_FILENAME"));
	if (WSsharedColumnHidden[0]) {
		Out.Replace(_T("[FilenameI]"), _T(""));
		Out.Replace(_T("[Filename]"), _T(""));
	} else {
		Out.Replace(_T("[FilenameI]"), _WebSelectString(pThis->m_Params.SharedSort == SHARED_SORT_NAME, pcSortIcon, _T("")));
		Out.Replace(_T("[Filename]"), strTmp);
	}
	Out.Replace(_T("[FilenameM]"), strTmp);

	_GetPlainResString(strTmp, _T("SF_TRANSFERRED"));
	if (WSsharedColumnHidden[1]) {
		Out.Replace(_T("[FileTransferredI]"), _T(""));
		Out.Replace(_T("[FileTransferred]"), _T(""));
	} else {
		CString sIconTmp = _WebSelectString(pThis->m_Params.SharedSort == SHARED_SORT_TRANSFERRED, pcSortIcon, _T(""));
		if (pThis->m_Params.SharedSort == SHARED_SORT_ALL_TIME_TRANSFERRED)
			sIconTmp = _WebSelectString(pThis->m_Params.bSharedSortReverse, pThis->m_Templates.sUpDoubleArrow, pThis->m_Templates.sDownDoubleArrow);
		Out.Replace(_T("[FileTransferredI]"), sIconTmp);
		Out.Replace(_T("[FileTransferred]"), strTmp);
	}
	Out.Replace(_T("[FileTransferredM]"), strTmp);

	_GetPlainResString(strTmp, _T("SF_REQUESTS"));
	if (WSsharedColumnHidden[2]) {
		Out.Replace(_T("[FileRequestsI]"), _T(""));
		Out.Replace(_T("[FileRequests]"), _T(""));
	} else {
		CString sIconTmp = _WebSelectString(pThis->m_Params.SharedSort == SHARED_SORT_REQUESTS, pcSortIcon, _T(""));
		if (pThis->m_Params.SharedSort == SHARED_SORT_ALL_TIME_REQUESTS)
			sIconTmp = _WebSelectString(pThis->m_Params.bSharedSortReverse, pThis->m_Templates.sUpDoubleArrow, pThis->m_Templates.sDownDoubleArrow);
		Out.Replace(_T("[FileRequestsI]"), sIconTmp);
		Out.Replace(_T("[FileRequests]"), strTmp);
	}
	Out.Replace(_T("[FileRequestsM]"), strTmp);

	_GetPlainResString(strTmp, _T("SF_ACCEPTS"));
	if (WSsharedColumnHidden[3]) {
		Out.Replace(_T("[FileAcceptsI]"), _T(""));
		Out.Replace(_T("[FileAccepts]"), _T(""));
	} else {
		CString sIconTmp = _WebSelectString(pThis->m_Params.SharedSort == SHARED_SORT_ACCEPTS, pcSortIcon, _T(""));
		if (pThis->m_Params.SharedSort == SHARED_SORT_ALL_TIME_ACCEPTS)
			sIconTmp = _WebSelectString(pThis->m_Params.bSharedSortReverse, pThis->m_Templates.sUpDoubleArrow, pThis->m_Templates.sDownDoubleArrow);
		Out.Replace(_T("[FileAcceptsI]"), sIconTmp);
		Out.Replace(_T("[FileAccepts]"), strTmp);
	}
	Out.Replace(_T("[FileAcceptsM]"), strTmp);

	_GetPlainResString(strTmp, _T("DL_SIZE"));
	if (WSsharedColumnHidden[4]) {
		Out.Replace(_T("[SizeI]"), _T(""));
		Out.Replace(_T("[Size]"), _T(""));
	} else {
		Out.Replace(_T("[SizeI]"), _WebSelectString(pThis->m_Params.SharedSort == SHARED_SORT_SIZE, pcSortIcon, _T("")));
		Out.Replace(_T("[Size]"), strTmp);
	}
	Out.Replace(_T("[SizeM]"), strTmp);

	_GetPlainResString(strTmp, _T("COMPLSOURCES"));
	if (WSsharedColumnHidden[5]) {
		Out.Replace(_T("[CompletesI]"), _T(""));
		Out.Replace(_T("[Completes]"), _T(""));
	} else {
		Out.Replace(_T("[CompletesI]"), _WebSelectString(pThis->m_Params.SharedSort == SHARED_SORT_COMPLETES, pcSortIcon, _T("")));
		Out.Replace(_T("[Completes]"), strTmp);
	}
	Out.Replace(_T("[CompletesM]"), strTmp);

	_GetPlainResString(strTmp, _T("PRIORITY"));
	if (WSsharedColumnHidden[6]) {
		Out.Replace(_T("[PriorityI]"), _T(""));
		Out.Replace(_T("[Priority]"), _T(""));
	} else {
		Out.Replace(_T("[PriorityI]"), _WebSelectString(pThis->m_Params.SharedSort == SHARED_SORT_PRIORITY, pcSortIcon, _T("")));
		Out.Replace(_T("[Priority]"), strTmp);
	}
	Out.Replace(_T("[PriorityM]"), strTmp);

	Out.Replace(_T("[Actions]"), _GetPlainResString(_T("WEB_ACTIONS")));
	Out.Replace(_T("[Reload]"), _GetPlainResString(_T("SF_RELOAD")));
	Out.Replace(_T("[Session]"), sSession);
	Out.Replace(_T("[SharedList]"), _GetPlainResString(_T("SF_FILES")));

	CString OutE(pThis->m_Templates.sSharedLine);

	CArray<SharedFiles> SharedArray;
	if (!_GetSharedFilesSnapshot(pThis, SharedArray))
		SharedArray.RemoveAll();

	SortParams prm{ (int)pThis->m_Params.SharedSort, pThis->m_Params.bSharedSortReverse };
	qsort_s(SharedArray.GetData(), SharedArray.GetCount(), sizeof(SharedFiles), &_SharedCmp, &prm);

	// Displaying
	CString sSharedList;
	for (INT_PTR i = 0; i < SharedArray.GetCount(); ++i) {
		CString HTTPProcessData(OutE);

		bool b = (SharedArray[i].sFileHash == _ParseURL(Data.sURL, _T("hash")));
		HTTPProcessData.Replace(_T("[LastChangedDataset]"), _WebSelectString(b, _T("checked"), _T("checked_no")));

		LPCTSTR sharedpriority;	//priority
		if (SharedArray[i].bFileAutoPriority)
			sharedpriority = _T("Auto");
		else
			switch (SharedArray[i].nFilePriority) {
			case PR_VERYLOW:
				sharedpriority = _T("VeryLow");
				break;
			case PR_LOW:
				sharedpriority = _T("Low");
				break;
			case PR_NORMAL:
				sharedpriority = _T("Normal");
				break;
			case PR_HIGH:
				sharedpriority = _T("High");
				break;
			case PR_VERYHIGH:
				sharedpriority = _T("Release");
				break;
			default:
				sharedpriority = _T("");
			}

		CString ed2k(SharedArray[i].sED2kLink);		//ed2klink
		ed2k.Replace(_T("'"), _T("&#8217;"));
		const CString &hash(SharedArray[i].sFileHash);	//hash
		CString fname(SharedArray[i].sFileName);	//filename
		fname.Replace(_T("'"), _T("&#8217;"));

		bool downloadable = SharedArray[i].bDownloadable;
		if (!hash.IsEmpty()) {
			HTTPProcessData.Replace(_T("[hash]"), hash);
			HTTPProcessData.Replace(_T("[FileIsPriority]"), _WebSelectString(SharedArray[i].bReleasePriority, _T("release"), _T("none")));
		}

		HTTPProcessData.Replace(_T("[admin]"), _WebSelectString(bAdmin, _T("admin"), _T("")));
		HTTPProcessData.Replace(_T("[ed2k]"), _SpecialChars(ed2k));
		HTTPProcessData.Replace(_T("[fname]"), _SpecialChars(fname));
		HTTPProcessData.Replace(_T("[session]"), sSession);
		HTTPProcessData.Replace(_T("[shared-priority]"), sharedpriority); //DonGato: priority change

		HTTPProcessData.Replace(_T("[FileName]"), _SpecialChars(SharedArray[i].sFileName));
		HTTPProcessData.Replace(_T("[FileType]"), SharedArray[i].sFileType);
		HTTPProcessData.Replace(_T("[FileState]"), SharedArray[i].sFileState);

		HTTPProcessData.Replace(_T("[Downloadable]"), _WebSelectString(downloadable, _T("yes"), _T("no")));

		HTTPProcessData.Replace(_T("[IFDOWNLOADABLE]"), _WebSelectString(downloadable, _T(""), _T("<!--")));
		HTTPProcessData.Replace(_T("[/IFDOWNLOADABLE]"), _WebSelectString(downloadable, _T(""), _T("-->")));

		TCHAR HTTPTempC[100];
		//0
		if (WSsharedColumnHidden[0])
			HTTPProcessData.Replace(_T("[ShortFileName]"), _T(""));
		else if (SharedArray[i].sFileName.GetLength() > (SHORT_LENGTH))
			HTTPProcessData.Replace(_T("[ShortFileName]"), _SpecialChars(SharedArray[i].sFileName.Left(SHORT_LENGTH - 3)) + _T("..."));
		else
			HTTPProcessData.Replace(_T("[ShortFileName]"), _SpecialChars(SharedArray[i].sFileName));
		//1
		HTTPProcessData.Replace(_T("[FileTransferred]"), _WebSelectString(WSsharedColumnHidden[1] != FALSE, _T(""), CastItoXBytes(SharedArray[i].nFileTransferred)));
		if (WSsharedColumnHidden[1])
			*HTTPTempC = _T('\0');
		else
			_stprintf(HTTPTempC, _T(" (%s)"), (LPCTSTR)CastItoXBytes(SharedArray[i].nFileAllTimeTransferred));
		HTTPProcessData.Replace(_T("[FileAllTimeTransferred]"), HTTPTempC);
		//2
		if (WSsharedColumnHidden[2])
			*HTTPTempC = _T('\0');
		else
			_stprintf(HTTPTempC, _T("%i"), SharedArray[i].nFileRequests);
		HTTPProcessData.Replace(_T("[FileRequests]"), HTTPTempC);
		if (!WSsharedColumnHidden[2])
			_stprintf(HTTPTempC, _T(" (%i)"), SharedArray[i].nFileAllTimeRequests);
		HTTPProcessData.Replace(_T("[FileAllTimeRequests]"), HTTPTempC);
		//3
		if (WSsharedColumnHidden[3])
			*HTTPTempC = _T('\0');
		else
			_stprintf(HTTPTempC, _T("%i"), SharedArray[i].nFileAccepts);
		HTTPProcessData.Replace(_T("[FileAccepts]"), HTTPTempC);
		if (!WSsharedColumnHidden[3])
			_stprintf(HTTPTempC, _T(" (%i)"), SharedArray[i].nFileAllTimeAccepts);
		HTTPProcessData.Replace(_T("[FileAllTimeAccepts]"), HTTPTempC);
		//4..6
		if (WSsharedColumnHidden[4])
			HTTPProcessData.Replace(_T("[FileSize]"), _T(""));
		else
			HTTPProcessData.Replace(_T("[FileSize]"), CastItoXBytes(SharedArray[i].m_qwFileSize));
		if (WSsharedColumnHidden[5])
			HTTPProcessData.Replace(_T("[Completes]"), _T(""));
		else
			HTTPProcessData.Replace(_T("[Completes]"), SharedArray[i].sFileCompletes);
		if (WSsharedColumnHidden[6])
			HTTPProcessData.Replace(_T("[Priority]"), _T(""));
		else
			HTTPProcessData.Replace(_T("[Priority]"), SharedArray[i].sFilePriority);
		HTTPProcessData.Replace(_T("[FileHash]"), SharedArray[i].sFileHash);

		sSharedList += HTTPProcessData;
	}

	Out.Replace(_T("[SharedFilesList]"), sSharedList);
	Out.Replace(_T("[Session]"), sSession);
	return Out;
}

CString CWebServer::_GetGraphs(const ThreadData &Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	CString Out(pThis->m_Templates.sGraphs);

	CString strGraphDownload, strGraphUpload, strGraphCons;
	LPCTSTR pszFmt = _T("%u");
	INT_PTR cnt = min(WEB_GRAPH_WIDTH, pThis->m_Params.PointsForWeb.GetCount());
	for (INT_PTR i = 0; i < cnt; ++i) {
		const UpDown &pt = pThis->m_Params.PointsForWeb[i];
		// download
		strGraphDownload.AppendFormat(pszFmt, (uint32)(pt.download * 1024));
		// upload
		strGraphUpload.AppendFormat(pszFmt, (uint32)(pt.upload * 1024));
		// connections
		strGraphCons.AppendFormat(pszFmt, (uint32)(pt.connections));
		pszFmt = _T(",%u");
	}

	Out.Replace(_T("[GraphDownload]"), strGraphDownload);
	Out.Replace(_T("[GraphUpload]"), strGraphUpload);
	Out.Replace(_T("[GraphConnections]"), strGraphCons);

	Out.Replace(_T("[TxtDownload]"), _GetPlainResString(_T("TW_DOWNLOADS")));
	Out.Replace(_T("[TxtUpload]"), _GetPlainResString(_T("TW_UPLOADS")));
	Out.Replace(_T("[TxtTime]"), _GetPlainResString(_T("TIME")));
	Out.Replace(_T("[KByteSec]"), _GetPlainResString(_T("KBYTESSEC")));
	Out.Replace(_T("[TxtConnections]"), _GetPlainResString(_T("SP_ACTCON")));

	Out.Replace(_T("[ScaleTime]"), (LPCTSTR)CastSecondsToHM(((time_t)thePrefs.GetTrafficOMeterInterval()) * WEB_GRAPH_WIDTH));

	CString s1;
	s1.Format(_T("%u"), thePrefs.GetMaxGraphDownloadRate() + 4);
	Out.Replace(_T("[MaxDownload]"), s1);
	s1.Format(_T("%u"), thePrefs.GetMaxGraphUploadRate(true) + 4);
	Out.Replace(_T("[MaxUpload]"), s1);
	s1.Format(_T("%u"), thePrefs.GetMaxConnections() + 20);
	Out.Replace(_T("[MaxConnections]"), s1);

	return Out;
}

CString CWebServer::_GetAddServerBox(const ThreadData &Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	const CString &sSession(_ParseURL(Data.sURL, _T("ses")));
	if (!_IsSessionAdmin(Data, sSession))
		return CString();

	CString resultlog(_SpecialChars(theApp.emuledlg->GetLastLogEntry())); //Pick-up last line of the log

	CString Out(pThis->m_Templates.sAddServerBox);
	if (_ParseURL(Data.sURL, _T("addserver")) == _T("true")) {
		CString strServerAddress(_ParseURL(Data.sURL, _T("serveraddr")));
		CString strServerPort(_ParseURL(Data.sURL, _T("serverport")));
		if (!strServerAddress.Trim().IsEmpty() && !strServerPort.Trim().IsEmpty()) {
			CString strServerName(_ParseURL(Data.sURL, _T("servername")));
			if (strServerName.Trim().IsEmpty())
				strServerName = strServerAddress;

			const CString &sPrio(_ParseURL(Data.sURL, _T("priority")));
			int nPriority = SRV_PR_NORMAL;
			if (sPrio == _T("low"))
				nPriority = SRV_PR_LOW;
			else if (sPrio == _T("high"))
				nPriority = SRV_PR_HIGH;

			const bool bAddToStatic = _ParseURL(Data.sURL, _T("addtostatic")) == _T("true");
			const bool bConnectNow = _ParseURL(Data.sURL, _T("connectnow")) == _T("true");
			if (ExecuteServerCommandForWebThread(WEB_SERVER_COMMAND_ADD, strServerAddress, _tstoi(strServerPort), strServerName, CString(), bAddToStatic, nPriority, bAddToStatic, bConnectNow)) {
				resultlog.TrimRight(_T('\n'));
				resultlog.Delete(0, resultlog.ReverseFind(_T('\n')));
				Out.Replace(_T("[Message]"), resultlog);
			} else
				Out.Replace(_T("[Message]"), _GetPlainResString(_T("ERROR")));
		} else
			Out.Replace(_T("[Message]"), _GetPlainResString(_T("ERROR")));
		} else if (_ParseURL(Data.sURL, _T("updateservermetfromurl")) == _T("true")) {
			const CString &url(_ParseURL(Data.sURL, _T("servermeturl")));
			if (ExecuteServerCommandForWebThread(WEB_SERVER_COMMAND_UPDATE_SERVER_MET_FROM_URL, url, 0))
				Out.Replace(_T("[Message]"), _GetPlainResString(_T("DOWNLOADING_SERVERMET")));
			else
				Out.Replace(_T("[Message]"), _GetPlainResString(_T("ERROR")));
		} else
			Out.Replace(_T("[Message]"), _T(""));

	Out.Replace(_T("[AddServer]"), _GetPlainResString(_T("SV_NEWSERVER")));
	Out.Replace(_T("[IP:Port]"), _GetPlainResString(_T("SV_ADDRESS_PORT")));
	Out.Replace(_T("[Name]"), _GetPlainResString(_T("SW_NAME")));
	Out.Replace(_T("[Static]"), _GetPlainResString(_T("STATICSERVER")));
	Out.Replace(_T("[ConnectNow]"), _GetPlainResString(_T("IRC_CONNECT")));
	Out.Replace(_T("[Priority]"), _GetPlainResString(_T("PRIORITY")));
	Out.Replace(_T("[Low]"), _GetPlainResString(_T("PRIOLOW")));
	Out.Replace(_T("[Normal]"), _GetPlainResString(_T("PRIONORMAL")));
	Out.Replace(_T("[High]"), _GetPlainResString(_T("PRIOHIGH")));
	Out.Replace(_T("[Add]"), _GetPlainResString(_T("SV_ADD")));
	Out.Replace(_T("[UpdateServerMetFromURL]"), _GetPlainResString(_T("SV_MET")));
	Out.Replace(_T("[URL]"), _GetPlainResString(_T("SV_URL")));
	Out.Replace(_T("[Apply]"), _GetPlainResString(_T("PW_APPLY")));
	//for admin only (verified on entry)
	CString s;
	s.Format(_T("?ses=%s&amp;w=server&amp;c=disconnect"), (LPCTSTR)sSession);
	Out.Replace(_T("[URL_Disconnect]"), s);
	s.Format(_T("?ses=%s&amp;w=server&amp;c=connect"), (LPCTSTR)sSession);
	Out.Replace(_T("[URL_Connect]"), s);

	Out.Replace(_T("[Disconnect]"), _GetPlainResString(_T("IRC_DISCONNECT")));
	Out.Replace(_T("[Connect]"), _GetPlainResString(_T("CONNECTTOANYSERVER")));
	Out.Replace(_T("[ServerOptions]"), _GetPlainResString(_T("CONNECTION")));
	Out.Replace(_T("[Execute]"), _GetPlainResString(_T("IRC_PERFORM")));

	return Out;
}

CString CWebServer::_GetLog(const ThreadData &Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	const CString &sSession(_ParseURL(Data.sURL, _T("ses")));

	CString Out(pThis->m_Templates.sLog);

	if (_ParseURL(Data.sURL, _T("clear")) == _T("yes") && _IsSessionAdmin(Data, sSession))
		theApp.emuledlg->ResetLog();

	Out.Replace(_T("[Clear]"), _GetPlainResString(_T("PW_RESET")));
	Out.Replace(_T("[Log]"), _SpecialChars(theApp.emuledlg->GetAllLogEntries(), false) + _T("<br><a name=\"end\"></a>"));
	Out.Replace(_T("[Session]"), sSession);

	return Out;
}

CString CWebServer::_GetServerInfo(const ThreadData &Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	const CString &sSession(_ParseURL(Data.sURL, _T("ses")));

	CString Out(pThis->m_Templates.sServerInfo);

	if (_ParseURL(Data.sURL, _T("clear")) == _T("yes") && _IsSessionAdmin(Data, sSession))
		theApp.emuledlg->ResetServerInfo();

	Out.Replace(_T("[Clear]"), _GetPlainResString(_T("PW_RESET")));
	Out.Replace(_T("[ServerInfo]"), _SpecialChars(theApp.emuledlg->GetServerInfoText(), false) + _T("<br><a name=\"end\"></a>"));
	Out.Replace(_T("[Session]"), sSession);

	return Out;
}

CString CWebServer::_GetDebugLog(const ThreadData &Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	const CString &sSession(_ParseURL(Data.sURL, _T("ses")));

	CString Out(pThis->m_Templates.sDebugLog);

	if (_ParseURL(Data.sURL, _T("clear")) == _T("yes") && _IsSessionAdmin(Data, sSession))
		theApp.emuledlg->ResetDebugLog();

	Out.Replace(_T("[Clear]"), _GetPlainResString(_T("PW_RESET")));
	Out.Replace(_T("[DebugLog]"), _SpecialChars(theApp.emuledlg->GetAllDebugLogEntries(), false) + _T("<br><a name=\"end\"></a>"));
	Out.Replace(_T("[Session]"), sSession);

	return Out;
}

CString CWebServer::_GetMyInfo(const ThreadData &Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	CString Out(pThis->m_Templates.sMyInfoLog);

	Out.Replace(_T("[MYINFOLOG]"), theApp.emuledlg->serverwnd->GetMyInfoString());

	return Out;
}

CString CWebServer::_GetKadDlg(const ThreadData &Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();


	const CString &sSession(_ParseURL(Data.sURL, _T("ses")));
	CString Out(pThis->m_Templates.sKad);

	if (_IsSessionAdmin(Data, sSession)) {
		if (!_ParseURL(Data.sURL, _T("bootstrap")).IsEmpty()) {
			CString dest(_ParseURL(Data.sURL, _T("ip")));
			dest.AppendFormat(_T(":%s"), (LPCTSTR)_ParseURL(Data.sURL, _T("port")));
			ExecuteServerCommandForWebThread(WEB_SERVER_COMMAND_BOOTSTRAP_KAD, dest, 0);
		}

		const CString &sAction(_ParseURL(Data.sURL, _T("c")));
		if (sAction == _T("connect"))
			ExecuteServerCommandForWebThread(WEB_SERVER_COMMAND_CONNECT_KAD, CString(), 0);
		else if (sAction == _T("disconnect"))
			ExecuteServerCommandForWebThread(WEB_SERVER_COMMAND_DISCONNECT_KAD, CString(), 0);
		else if (sAction == _T("rcfirewall"))
			ExecuteServerCommandForWebThread(WEB_SERVER_COMMAND_RECHECK_KAD_FIREWALL, CString(), 0);
	}
	// check the condition if bootstrap is possible
	Out.Replace(_T("[BOOTSTRAPLINE]"), _WebSelectString(Kademlia::CKademlia::IsConnected(), _T(""), pThis->m_Templates.sBootstrapLine));

	// Infos
	CString buffer;
	if (Kademlia::CKademlia::IsConnected())
		if (Kademlia::CKademlia::IsFirewalled()) {
			Out.Replace(_T("[KADSTATUS]"), GetResString(_T("FIREWALLED")));
			buffer.Format(_T("<a href=\"?ses=%s&amp;w=kad&amp;c=rcfirewall\">%s</a>"), (LPCTSTR)sSession, (LPCTSTR)GetResString(_T("KAD_RECHECKFW")));
			buffer.AppendFormat(_T("<br><a href=\"?ses=%s&amp;w=kad&amp;c=disconnect\">%s</a>"), (LPCTSTR)sSession, (LPCTSTR)GetResString(_T("IRC_DISCONNECT")));
		} else {
			Out.Replace(_T("[KADSTATUS]"), GetResString(_T("CONNECTED")));
			buffer.Format(_T("<a href=\"?ses=%s&amp;w=kad&amp;c=disconnect\">%s</a>"), (LPCTSTR)sSession, (LPCTSTR)GetResString(_T("IRC_DISCONNECT")));
		} else if (Kademlia::CKademlia::IsRunning()) {
			Out.Replace(_T("[KADSTATUS]"), GetResString(_T("CONNECTING")));
			buffer.Format(_T("<a href=\"?ses=%s&amp;w=kad&amp;c=disconnect\">%s</a>"), (LPCTSTR)sSession, (LPCTSTR)GetResString(_T("IRC_DISCONNECT")));
		} else {
			Out.Replace(_T("[KADSTATUS]"), GetResString(_T("DISCONNECTED")));
			buffer.Format(_T("<a href=\"?ses=%s&amp;w=kad&amp;c=connect\">%s</a>"), (LPCTSTR)sSession, (LPCTSTR)GetResString(_T("IRC_CONNECT")));
		}

		Out.Replace(_T("[KADACTION]"), buffer);

		// kadstats
		// labels
		buffer.Format(_T("%s<br>%s"), (LPCTSTR)GetResString(_T("KADCONTACTLAB")), (LPCTSTR)GetResString(_T("KADSEARCHLAB")));
		Out.Replace(_T("[KADSTATSLABELS]"), buffer);

		// numbers
		buffer.Format(_T("%u<br>%i"), theApp.emuledlg->kademliawnd->GetContactCount()
			, theApp.emuledlg->kademliawnd->searchList->GetItemCount());
		Out.Replace(_T("[KADSTATSDATA]"), buffer);

		Out.Replace(_T("[BS_IP]"), GetResString(_T("IP")));
		Out.Replace(_T("[BS_PORT]"), GetResString(_T("PORT")));
		Out.Replace(_T("[BOOTSTRAP]"), GetResString(_T("BOOTSTRAP")));
		Out.Replace(_T("[KADSTAT]"), GetResString(_T("SF_STATISTICS")));
		Out.Replace(_T("[STATUS]"), GetResString(_T("STATUS")));
		Out.Replace(_T("[KAD]"), GetResString(_T("KADEMLIA")));
		Out.Replace(_T("[Session]"), sSession);

		return Out;
}

CString CWebServer::_GetStats(const ThreadData &Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	// refresh statistics
	::PostMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_SHOWSTATISTICS, 1);

	CString Out(pThis->m_Templates.sStats);
	// eklmn: new stats
	Out.Replace(_T("[Stats]"), theApp.emuledlg->statisticswnd->m_stattree.GetHTMLForExport());

	return Out;
}

CString CWebServer::_GetPreferences(const ThreadData &Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	const CString &sSession(_ParseURL(Data.sURL, _T("ses")));

	CString Out(pThis->m_Templates.sPreferences);
	Out.Replace(_T("[Session]"), sSession);

	if ((_ParseURL(Data.sURL, _T("saveprefs")) == _T("true")) && _IsSessionAdmin(Data, sSession)) {
		CString strTmp(_ParseURL(Data.sURL, _T("gzip")));
		if (strTmp == _T("true") || strTmp == _T("on"))
			thePrefs.SetWebUseGzip(true);
		else if (strTmp == _T("false") || strTmp.IsEmpty())
			thePrefs.SetWebUseGzip(false);

		if (!_ParseURL(Data.sURL, _T("refresh")).IsEmpty())
			thePrefs.SetWebPageRefresh(_tstoi(_ParseURL(Data.sURL, _T("refresh"))));

		strTmp = _ParseURL(Data.sURL, _T("maxcapdown"));
		if (!strTmp.IsEmpty())
			thePrefs.SetMaxGraphDownloadRate(ParseWebSpeedPreferenceValue(strTmp));
		strTmp = _ParseURL(Data.sURL, _T("maxcapup"));
		if (!strTmp.IsEmpty())
			thePrefs.SetMaxGraphUploadRate(ParseWebSpeedPreferenceValue(strTmp));

		strTmp = _ParseURL(Data.sURL, _T("maxdown"));
		if (!strTmp.IsEmpty()) {
			uint32 dwSpeed = ParseWebSpeedPreferenceValue(strTmp);
			thePrefs.SetMaxDownload(dwSpeed > 0 ? dwSpeed : UNLIMITED);
		}
		strTmp = _ParseURL(Data.sURL, _T("maxup"));
		if (!strTmp.IsEmpty()) {
			uint32 dwSpeed = ParseWebSpeedPreferenceValue(strTmp);
			thePrefs.SetMaxUpload(dwSpeed > 0 ? dwSpeed : UNLIMITED);
		}

		if (!_ParseURL(Data.sURL, _T("maxsources")).IsEmpty())
			thePrefs.SetMaxSourcesPerFile(_tstoi(_ParseURL(Data.sURL, _T("maxsources"))));
		if (!_ParseURL(Data.sURL, _T("maxconnections")).IsEmpty())
			thePrefs.SetMaxConnections(_tstoi(_ParseURL(Data.sURL, _T("maxconnections"))));
		if (!_ParseURL(Data.sURL, _T("maxconnectionsperfive")).IsEmpty())
			thePrefs.SetMaxConsPerFive(_tstoi(_ParseURL(Data.sURL, _T("maxconnectionsperfive"))));
	}

	// Fill form
	Out.Replace(_T("[UseGzipVal]"), _WebSelectString(thePrefs.GetWebUseGzip(), _T("checked"), _T("")));

	CString sRefresh;
	sRefresh.Format(_T("%d"), thePrefs.GetWebPageRefresh());
	Out.Replace(_T("[RefreshVal]"), sRefresh);

	sRefresh.Format(_T("%u"), thePrefs.GetMaxSourcePerFileDefault());
	Out.Replace(_T("[MaxSourcesVal]"), sRefresh);

	sRefresh.Format(_T("%u"), thePrefs.GetMaxConnections());
	Out.Replace(_T("[MaxConnectionsVal]"), sRefresh);

	sRefresh.Format(_T("%u"), thePrefs.GetMaxConperFive());
	Out.Replace(_T("[MaxConnectionsPer5Val]"), sRefresh);

	Out.Replace(_T("[KBS]"), _GetPlainResString(thePrefs.GetForceSpeedsToKB() ? _T("KBYTESSEC") : _T("MBITSSEC")) + _T(':'));
	Out.Replace(_T("[LimitForm]"), _GetPlainResString(_T("PW_CONLIMITS")) + _T(':'));
	Out.Replace(_T("[MaxSources]"), _GetPlainResString(_T("PW_MAXSOURCES")) + _T(':'));
	Out.Replace(_T("[MaxConnections]"), _GetPlainResString(_T("PW_MAXC")) + _T(':'));
	Out.Replace(_T("[MaxConnectionsPer5]"), _GetPlainResString(_T("MAXCON5SECLABEL")) + _T(':'));
	Out.Replace(_T("[UseGzipForm]"), _GetPlainResString(_T("WEB_GZIP_COMPRESSION")));
	Out.Replace(_T("[UseGzipComment]"), _GetPlainResString(_T("WEB_GZIP_COMMENT")));

	Out.Replace(_T("[RefreshTimeForm]"), _GetPlainResString(_T("WEB_REFRESH_TIME")));
	Out.Replace(_T("[RefreshTimeComment]"), _GetPlainResString(_T("WEB_REFRESH_COMMENT")));
	Out.Replace(_T("[SpeedForm]"), _GetPlainResString(_T("SPEED_LIMITS")));
	Out.Replace(_T("[SpeedCapForm]"), _GetPlainResString(_T("CAPACITY_LIMITS")));

	Out.Replace(_T("[MaxCapDown]"), _GetPlainResString(_T("DOWNLOAD")));
	Out.Replace(_T("[MaxCapUp]"), _GetPlainResString(_T("PW_CON_UPLBL")));
	Out.Replace(_T("[MaxDown]"), _GetPlainResString(_T("DOWNLOAD")));
	Out.Replace(_T("[MaxUp]"), _GetPlainResString(_T("PW_CON_UPLBL")));
	Out.Replace(_T("[WebControl]"), _GetPlainResString(_T("PW_WS")));
	Out.Replace(_T("[eMuleAppName]"), _T("eMule"));
	Out.Replace(_T("[Apply]"), _GetPlainResString(_T("PW_APPLY")));

	CString m_sTestURL;
	m_sTestURL.Format(PORTTESTURL, thePrefs.GetPort(), thePrefs.GetUDPPort(), thePrefs.GetLanguageID());

	// the portcheck will need to do an obfuscated callback too if obfuscation is requested, so we have to provide our userhash so it can create the key
	if (thePrefs.IsCryptLayerPreferred())
		m_sTestURL.AppendFormat(_T("&obfuscated_test=%s"), (LPCTSTR)md4str(thePrefs.GetUserHash()));

	Out.Replace(_T("[CONNECTIONTESTLINK]"), _SpecialChars(m_sTestURL));
	Out.Replace(_T("[CONNECTIONTESTLABEL]"), GetResString(_T("CONNECTIONTEST")));


	CString sT;
	sT = FormatWebSpeedPreferenceValue(thePrefs.GetMaxDownload() == UNLIMITED ? 0u : thePrefs.GetMaxDownload());
	Out.Replace(_T("[MaxDownVal]"), sT);

	sT = FormatWebSpeedPreferenceValue(thePrefs.GetMaxUpload() == UNLIMITED ? 0u : thePrefs.GetMaxUpload());
	Out.Replace(_T("[MaxUpVal]"), sT);

	sT = FormatWebSpeedPreferenceValue(thePrefs.GetMaxGraphDownloadRate());
	Out.Replace(_T("[MaxCapDownVal]"), sT);

	sT = FormatWebSpeedPreferenceValue(thePrefs.GetMaxGraphUploadRate(true));
	Out.Replace(_T("[MaxCapUpVal]"), sT);

	return Out;
}

CString CWebServer::_GetLoginScreen(const ThreadData &Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	CString Out(pThis->m_Templates.sLogin);

	Out.Replace(_T("[CharSet]"), HTTPENCODING);
	Out.Replace(_T("[eMuleAppName]"), _T("eMule"));
	Out.Replace(_T("[version]"), theApp.GetAppVersion().Mid(6));
	Out.Replace(_T("[Login]"), _GetPlainResString(_T("WEB_LOGIN")));
	Out.Replace(_T("[EnterPassword]"), _GetPlainResString(_T("WEB_ENTER_PASSWORD")));
	Out.Replace(_T("[LoginNow]"), _GetPlainResString(_T("WEB_LOGIN_NOW")));
	Out.Replace(_T("[WebControl]"), _GetPlainResString(_T("PW_WS")));

	CString sFailed;
	if (pThis->m_nIntruderDetect >= 1)
		sFailed.Format(_T("<p class=\"failed\">%s</p>"), (LPCTSTR)_GetPlainResString(_T("WEB_BADLOGINATTEMPT")));
	else
		sFailed = _T("&nbsp;");
	Out.Replace(_T("[FailedLogin]"), sFailed);

	return Out;
}

// We have to add gz-header and some other stuff
// to standard zlib functions
// in order to use gzip in web pages
int CWebServer::_GzipCompress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen, int level)
{
	static const int gz_magic[2] = {0x1f, 0x8b}; // gzip magic header
	z_stream stream = {};
	stream.zalloc = (alloc_func)NULL;
	stream.zfree = (free_func)NULL;
	stream.opaque = (voidpf)NULL;
	uLong crc = crc32(0, Z_NULL, 0);
	// init Zlib stream
	// NOTE windowBits is passed < 0 to suppress zlib header
	int err = deflateInit2(&stream, level, Z_DEFLATED, -MAX_WBITS, MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);
	if (err != Z_OK)
		return err;

	sprintf((char*)dest, "%c%c%c%c%c%c%c%c%c%c", gz_magic[0], gz_magic[1]
		, Z_DEFLATED, 0 /*flags*/, 0, 0, 0, 0 /*time*/, 0 /*xflags*/, 255);
	// wire buffers
	stream.next_in = (Bytef*)source;
	stream.avail_in = (uInt)sourceLen;
	stream.next_out = &dest[10];
	stream.avail_out = *destLen - 18;
	// do it
	err = deflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		deflateEnd(&stream);
		return err;
	}
	err = deflateEnd(&stream);
	crc = crc32(crc, (Bytef*)source, (uInt)sourceLen);
	size_t i = 10 + stream.total_out;
	//CRC
	*(uLong*)&dest[i] = crc;
	// Length
	*(uLong*)&dest[i + sizeof(uLong)] = sourceLen;
	*destLen = 10 + stream.total_out + 8;

	return err;
}

bool CWebServer::_IsLoggedIn(const ThreadData &Data, long lSession)
{
	CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return false;

	_RemoveTimeOuts(Data);

	// find our session
	// i should have used CMap there, but i like CArray more ;-)
	if (lSession != 0)
		for (INT_PTR i = pThis->m_Params.Sessions.GetCount(); --i >= 0;)
			if (pThis->m_Params.Sessions[i].lSession == lSession) {
				// if found, also reset expiration time
				pThis->m_Params.Sessions[i].startTime = CTime::GetCurrentTime();
				return true;
			}

	return false;
}

void CWebServer::_RemoveTimeOuts(const ThreadData &Data)
{
	// remove expired sessions
	CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis != NULL)
		pThis->UpdateSessionCount();
}

bool CWebServer::_RemoveSession(const ThreadData &Data, long lSession)
{
	CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL || lSession == 0)
		return false;

	// find our session
	for (INT_PTR i = pThis->m_Params.Sessions.GetCount(); --i >= 0;)
		if (pThis->m_Params.Sessions[i].lSession == lSession) {
			pThis->m_Params.Sessions.RemoveAt(i);
			AddLogLine(true, (LPCTSTR)GetResString(_T("WEB_SESSIONEND")), (LPCTSTR)ipstr(pThis->m_uCurIP));
			::PostMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_UPDATEMYINFO, 0);
			return true;
		}

	return false;
}

Session CWebServer::_GetSessionByID(const ThreadData &Data, long sessionID)
{
	CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis != NULL && sessionID != 0)
		for (INT_PTR i = pThis->m_Params.Sessions.GetCount(); --i >= 0;)
			if (pThis->m_Params.Sessions[i].lSession == sessionID)
				return pThis->m_Params.Sessions[i];

	return Session{};
}

bool CWebServer::_IsSessionAdmin(const ThreadData &Data, long sessionID)
{
	CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis != NULL && sessionID != 0)
		for (INT_PTR i = pThis->m_Params.Sessions.GetCount(); --i >= 0;)
			if (pThis->m_Params.Sessions[i].lSession == sessionID)
				return pThis->m_Params.Sessions[i].admin;

	return false;
}

bool CWebServer::_IsSessionAdmin(const ThreadData &Data, const CString &strSsessionID)
{
	return _IsSessionAdmin(Data, _tstol(strSsessionID));
}

CString CWebServer::_GetPermissionDenied()
{
	CString s;
	s.Format(_T("javascript:alert(\'%s\')"), (LPCTSTR)_GetPlainResString(_T("SFS_ACCESS_DENIED")));
	return s;
}

CString CWebServer::_GetPlainResString(LPCTSTR nID, bool noquote)
{
	CString sRet(GetResString(nID));
	sRet.Remove(_T('&'));
	if (noquote) {
		sRet.Replace(_T("'"), _T("&#8217;"));
		sRet.Replace(_T("\n"), _T("\\n"));
	}
	return sRet;
}

void CWebServer::_GetPlainResString(CString &rstrOut, LPCTSTR nID, bool noquote)
{
	rstrOut = _GetPlainResString(nID, noquote);
}

// Ornis: creating the progressbar. colored if resources are given/available
CString CWebServer::_GetDownloadGraph(const ThreadData &Data, const CPartFile *pPartFile)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	//	Color style (paused files)
	static LPCTSTR const styles_paused[12] =
	{
		_T("p_green.gif"), _T("p_black.gif"), _T("p_yellow.gif"), _T("p_red.gif"),
		_T("p_blue1.gif"), _T("p_blue2.gif"), _T("p_blue3.gif"), _T("p_blue4.gif"),
		_T("p_blue5.gif"), _T("p_blue6.gif"), _T("p_greenpercent.gif"), _T("transparent.gif")
	};
	//	Color style (active files)
	static LPCTSTR const styles_active[12] =
	{
		_T("green.gif"), _T("black.gif"), _T("yellow.gif"), _T("red.gif"),
		_T("blue1.gif"), _T("blue2.gif"), _T("blue3.gif"), _T("blue4.gif"),
		_T("blue5.gif"), _T("blue6.gif"), _T("greenpercent.gif"), _T("transparent.gif")
	};

	const LPCTSTR *barcolours = (pPartFile && (pPartFile->GetStatus() == PS_PAUSED)) ? styles_paused : styles_active;

	const uint16 uBarWidth = pThis->m_Templates.iProgressbarWidth;

	CString Out;
	if (pPartFile == NULL || !pPartFile->IsPartFile() || uBarWidth == 0) {
		Out.Format(pThis->m_Templates.sProgressbarImgsPercent, barcolours[10], uBarWidth);
		Out += _T("<br>");
		Out.AppendFormat(pThis->m_Templates.sProgressbarImgs, barcolours[0], uBarWidth);
	} else {
		const CStringA s_ChunkBar(pPartFile->GetProgressString(uBarWidth));
		// and now make a graph out of the array - need to be in a progressive way

		int iCompletedWidth = static_cast<int>((uBarWidth / 100.0) * pPartFile->GetPercentCompleted());
		if (iCompletedWidth < 0)
			iCompletedWidth = 0;
		else if (iCompletedWidth > uBarWidth)
			iCompletedWidth = uBarWidth;
		const int iMinimumProgressWidth = (uBarWidth < 5) ? uBarWidth : 5;
		Out.Format(pThis->m_Templates.sProgressbarImgsPercent, barcolours[iCompletedWidth > 0 ? 10 : 11], (iCompletedWidth > 0 ? iCompletedWidth : iMinimumProgressWidth));
		Out += _T("<br>");

		BYTE lastcolor = 1;
		uint16 lastindex = 0;
		const int iChunkBarLength = s_ChunkBar.GetLength();
		for (uint16 i = 0; i < uBarWidth; ++i) {
			BYTE color = 1;
			if (i < iChunkBarLength) {
				const char c = s_ChunkBar[i];
				if (c >= '0' && c <= '9')
					color = static_cast<BYTE>(c - '0');
			}

			if (lastcolor != color) {
				if (i > lastindex && lastcolor < _countof(styles_active))
					Out.AppendFormat(pThis->m_Templates.sProgressbarImgs, barcolours[lastcolor], i - lastindex);
				lastcolor = color;
				lastindex = i;
			}
		}
		if (uBarWidth > lastindex && lastcolor < _countof(styles_active))
			Out.AppendFormat(pThis->m_Templates.sProgressbarImgs, barcolours[lastcolor], uBarWidth - lastindex);
	}
	return Out;
}

CString CWebServer::_GetSearch(const ThreadData &Data)
{
	CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	CString sCat;
	int cat = _tstoi(_ParseURL(Data.sURL, _T("cat")));
	if (cat != 0)
		sCat.Format(_T("%i"), cat);

	const CString &sSession(_ParseURL(Data.sURL, _T("ses")));
	bool bSessionAdmin = _IsSessionAdmin(Data, sSession);
	CString Out(pThis->m_Templates.sSearch);

	if (_ParseURL(Data.sURL, _T("c")) == _T("menu")) {
		int iMenu = _tstoi(_ParseURL(Data.sURL, _T("m")));
		bool bValue = _tstoi(_ParseURL(Data.sURL, _T("v"))) != 0;
		WSsearchColumnHidden[iMenu] = bValue;

		_SaveWIConfigArray(WSsearchColumnHidden, _countof(WSsearchColumnHidden), _T("searchColumnHidden"));
	}

	if (!_ParseURL(Data.sURL, _T("tosearch")).IsEmpty() && bSessionAdmin) {

		// get method
		const CString &method((_ParseURL(Data.sURL, _T("method"))));

		SSearchParams params;
		params.strExpression = _ParseURL(Data.sURL, _T("tosearch"));
		params.strFileType = _ParseURL(Data.sURL, _T("type"));
		// for safety: this string is sent to servers and/or kad nodes, validate it!
		if (!params.strFileType.IsEmpty()
			&& params.strFileType != _T(ED2KFTSTR_ARCHIVE)
			&& params.strFileType != _T(ED2KFTSTR_AUDIO)
			&& params.strFileType != _T(ED2KFTSTR_CDIMAGE)
			&& params.strFileType != _T(ED2KFTSTR_DOCUMENT)
			&& params.strFileType != _T(ED2KFTSTR_IMAGE)
			&& params.strFileType != _T(ED2KFTSTR_PROGRAM)
			&& params.strFileType != _T(ED2KFTSTR_VIDEO)
			&& params.strFileType != _T(ED2KFTSTR_EMULECOLLECTION))
		{
			ASSERT(0);
			params.strFileType.Empty();
		}
		params.ullMinSize = _tstoi64(_ParseURL(Data.sURL, _T("min"))) * 1048576ui64;
		params.ullMaxSize = _tstoi64(_ParseURL(Data.sURL, _T("max"))) * 1048576ui64;
		if (params.ullMaxSize < params.ullMinSize)
			params.ullMaxSize = 0;

		const CString &s(_ParseURL(Data.sURL, _T("avail")));
		params.uAvailability = s.IsEmpty() ? 0 : _tstoi(s);
		if (params.uAvailability > 1000000)
			params.uAvailability = 1000000;

		params.strExtension = _ParseURL(Data.sURL, _T("ext"));
		if (method == _T("kademlia"))
			params.eType = SearchTypeKademlia;
		else if (method == _T("global"))
			params.eType = SearchTypeEd2kGlobal;
		else
			params.eType = SearchTypeEd2kServer;

		CString strResponse;
		QueueWebSearchStartCommand(params, strResponse);
		Out.Replace(_T("[Message]"), strResponse);

	} else {
		bool b = (_ParseURL(Data.sURL, _T("tosearch")).IsEmpty() || bSessionAdmin);
		Out.Replace(_T("[Message]"), _GetPlainResString(b ? _T("SW_REFETCHRES") : _T("SFS_ACCESS_DENIED")));
	}

	CString sSort(_ParseURL(Data.sURL, _T("sort")));
	if (!sSort.IsEmpty())
		pThis->m_iSearchSortby = _tstoi(sSort);
	sSort = _ParseURL(Data.sURL, _T("sortAsc"));
	if (!sSort.IsEmpty())
		pThis->m_bSearchAsc = _tstoi(sSort) != 0;

	CString result(pThis->m_Templates.sSearchHeader);

	CQArray<SearchFileStruct, SearchFileStruct> SearchFileArray;
	GetWebSearchList(pThis, SearchFileArray, pThis->m_iSearchSortby);

	SearchFileArray.QuickSort(pThis->m_bSearchAsc);

	if (!_ParseURL(Data.sURL, _T("downloads")).IsEmpty() && bSessionAdmin) {
		const CString &downloads(_ParseURLArray(Data.sURL, _T("downloads")));
		CString downloadLinks;
		GetWebSearchDownloadLinks(pThis, downloads, downloadLinks);
		if (!downloadLinks.IsEmpty())
			QueueWebDownloadLinks(downloadLinks, cat);
	}

	const INT_PTR iSearchRenderCount = min(SearchFileArray.GetCount(), WEB_MAX_TABLE_ROWS);
	for (INT_PTR i = 0; i < iSearchRenderCount; ++i) {
		SearchFileStruct structFile = SearchFileArray[i];
		LPCTSTR strOverlayImage = structFile.m_strOverlayImage.IsEmpty() ? _T("none") : (LPCTSTR)structFile.m_strOverlayImage;

		CString strFmt;
		strFmt.Format(_T("<font color=\"%s\">%%s</font>"), structFile.m_strTextColor.IsEmpty() ? _T("#ffffff") : (LPCTSTR)structFile.m_strTextColor);

		LPCTSTR strSourcesImage;
		if (structFile.m_uSourceCount < 5)
			strSourcesImage = _T("0");
		else if (structFile.m_uSourceCount < 10)
			strSourcesImage = _T("5");
		else if (structFile.m_uSourceCount < 25)
			strSourcesImage = _T("10");
		else if (structFile.m_uSourceCount < 50)
			strSourcesImage = _T("25");
		else
			strSourcesImage = _T("50");

		CString strSources;
		strSources.Format(_T("%u(%u)"), structFile.m_uSourceCount, structFile.m_dwCompleteSourceCount);

		CString strFilename(structFile.m_strFileName);
		strFilename.Replace(_T("'"), _T("\\'"));

		CString strLink;
		strLink.Format(_T("ed2k://|file|%s|%I64u|%s|/"),
			(LPCTSTR)_SpecialChars(strFilename), structFile.m_uFileSize, (LPCTSTR)structFile.m_strFileHash);

		CString s0, s1, s2, s3;
		if (!WSsearchColumnHidden[0])
			s0.Format(strFmt, (LPCTSTR)StringLimit(structFile.m_strFileName, 70));
		if (!WSsearchColumnHidden[1])
			s1.Format(strFmt, (LPCTSTR)CastItoXBytes(structFile.m_uFileSize));
		if (!WSsearchColumnHidden[2])
			s2.Format(strFmt, (LPCTSTR)structFile.m_strFileHash);
		if (!WSsearchColumnHidden[3])
			s3.Format(strFmt, (LPCTSTR)strSources);
		result.AppendFormat(pThis->m_Templates.sSearchResultLine
			, strSourcesImage
			, (LPCTSTR)strLink
			, strOverlayImage
			, (LPCTSTR)_GetWebImageNameForFileType(structFile.m_strFileName)
			, (LPCTSTR)s0 //file name
			, (LPCTSTR)s1 //file size
			, (LPCTSTR)s2 //hash
			, (LPCTSTR)s3 //sources
			, (LPCTSTR)structFile.m_strFileHash
		);
	}

	if (thePrefs.GetCatCount() > 1)
		_InsertCatBox(Out, 0, pThis->m_Templates.sCatArrow, false, false, sSession, _T(""));
	else
		Out.Replace(_T("[CATBOX]"), _T(""));

	Out.Replace(_T("[SEARCHINFOMSG]"), _T(""));
	Out.Replace(_T("[RESULTLIST]"), result);
	Out.Replace(_T("[Result]"), GetResString(_T("SW_RESULT")));
	Out.Replace(_T("[Session]"), sSession);
	Out.Replace(_T("[Name]"), _GetPlainResString(_T("SW_NAME")));
	Out.Replace(_T("[Type]"), _GetPlainResString(_T("TYPE")));
	Out.Replace(_T("[Any]"), _GetPlainResString(_T("SEARCH_ANY")));
	Out.Replace(_T("[Audio]"), _GetPlainResString(_T("AUDIO")));
	Out.Replace(_T("[Image]"), _GetPlainResString(_T("SEARCH_PICS")));
	Out.Replace(_T("[Video]"), _GetPlainResString(_T("VIDEO")));
	Out.Replace(_T("[Document]"), _GetPlainResString(_T("SEARCH_DOC")));
	Out.Replace(_T("[CDImage]"), _GetPlainResString(_T("SEARCH_CDIMG")));
	Out.Replace(_T("[Program]"), _GetPlainResString(_T("SEARCH_PRG")));
	Out.Replace(_T("[Archive]"), _GetPlainResString(_T("SEARCH_ARC")));
	Out.Replace(_T("[eMuleCollection]"), _GetPlainResString(_T("META_COLLECTION")));
	Out.Replace(_T("[Search]"), _GetPlainResString(_T("SW_SEARCHBOX")));
	Out.Replace(_T("[Size]"), _GetPlainResString(_T("DL_SIZE")));
	Out.Replace(_T("[Start]"), _GetPlainResString(_T("START_NOUN")));

	Out.Replace(_T("[USESSERVER]"), _GetPlainResString(_T("SERVER")));
	Out.Replace(_T("[USEKADEMLIA]"), _GetPlainResString(_T("KADEMLIA")));
	Out.Replace(_T("[METHOD]"), _GetPlainResString(_T("METHOD")));

	Out.Replace(_T("[SizeMin]"), _GetPlainResString(_T("SEARCHMINSIZE")));
	Out.Replace(_T("[SizeMax]"), _GetPlainResString(_T("SEARCHMAXSIZE")));
	Out.Replace(_T("[Availabl]"), _GetPlainResString(_T("SEARCHAVAIL")));
	Out.Replace(_T("[Extention]"), _GetPlainResString(_T("SEARCHEXTENTION")));
	Out.Replace(_T("[Global]"), _GetPlainResString(_T("GLOBALSEARCH")));
	Out.Replace(_T("[MB]"), _GetPlainResString(_T("MBYTES")));
	Out.Replace(_T("[Apply]"), _GetPlainResString(_T("PW_APPLY")));
	Out.Replace(_T("[CatSel]"), sCat);
	Out.Replace(_T("[Ed2klink]"), _GetPlainResString(_T("SW_LINK")));
	CString sSortIcon = _WebSelectString(pThis->m_bSearchAsc, pThis->m_Templates.sUpArrow, pThis->m_Templates.sDownArrow);
		LPCTSTR pcSortIcon = sSortIcon;

	CString strTmp;
	_GetPlainResString(strTmp, _T("DL_FILENAME"));
	Out.Replace(_T("[FilenameI]"), _WebSelectString(WSsearchColumnHidden[0] || pThis->m_iSearchSortby != 0, _T(""), pcSortIcon));
	Out.Replace(_T("[FilenameH]"), _WebSelectString(WSsearchColumnHidden[0] != FALSE, _T(""), strTmp));
	Out.Replace(_T("[FilenameM]"), strTmp);

	_GetPlainResString(strTmp, _T("DL_SIZE"));
	Out.Replace(_T("[FilesizeI]"), _WebSelectString(WSsearchColumnHidden[1] || pThis->m_iSearchSortby != 1, _T(""), pcSortIcon));
	Out.Replace(_T("[FilesizeH]"), _WebSelectString(WSsearchColumnHidden[1] != FALSE, _T(""), strTmp));
	Out.Replace(_T("[FilesizeM]"), strTmp);

	_GetPlainResString(strTmp, _T("FILEHASH"));
	Out.Replace(_T("[FilehashI]"), _WebSelectString(WSsearchColumnHidden[2] || pThis->m_iSearchSortby != 2, _T(""), pcSortIcon));
	Out.Replace(_T("[FilehashH]"), _WebSelectString(WSsearchColumnHidden[2] != FALSE, _T(""), strTmp));
	Out.Replace(_T("[FilehashM]"), strTmp);

	_GetPlainResString(strTmp, _T("DL_SOURCES"));
	Out.Replace(_T("[SourcesI]"), _WebSelectString(WSsearchColumnHidden[3] || pThis->m_iSearchSortby != 3, _T(""), pcSortIcon));
	Out.Replace(_T("[SourcesH]"), _WebSelectString(WSsearchColumnHidden[3] != FALSE, _T(""), strTmp));
	Out.Replace(_T("[SourcesM]"), strTmp);

	Out.Replace(_T("[Download]"), _GetPlainResString(_T("DOWNLOAD")));

	Out.Replace(_T("[SORTASCVALUE0]"), _WebSelectString(pThis->m_iSearchSortby == 0 && pThis->m_bSearchAsc, _T("0"), _T("1")));
	Out.Replace(_T("[SORTASCVALUE1]"), _WebSelectString(pThis->m_iSearchSortby == 1 && pThis->m_bSearchAsc, _T("0"), _T("1")));
	Out.Replace(_T("[SORTASCVALUE2]"), _WebSelectString(pThis->m_iSearchSortby == 2 && pThis->m_bSearchAsc, _T("0"), _T("1")));
	Out.Replace(_T("[SORTASCVALUE3]"), _WebSelectString(pThis->m_iSearchSortby == 3 && pThis->m_bSearchAsc, _T("0"), _T("1")));
	Out.Replace(_T("[SORTASCVALUE4]"), _WebSelectString(pThis->m_iSearchSortby == 4 && pThis->m_bSearchAsc, _T("0"), _T("1")));
	Out.Replace(_T("[SORTASCVALUE5]"), _WebSelectString(pThis->m_iSearchSortby == 5 && pThis->m_bSearchAsc, _T("0"), _T("1")));

	return Out;
}

INT_PTR CWebServer::UpdateSessionCount()
{
	if (thePrefs.GetWebTimeoutMins() > 0) {
		INT_PTR oldvalue = m_Params.Sessions.GetCount();
		CTime curTime(CTime::GetCurrentTime());
		for (INT_PTR i = oldvalue; --i >= 0;) {
			CTimeSpan ts = curTime - m_Params.Sessions[i].startTime;
			if (ts.GetTotalSeconds() >= MIN2S(thePrefs.GetWebTimeoutMins()))
				m_Params.Sessions.RemoveAt(i);
		}

		if (oldvalue != m_Params.Sessions.GetCount())
			::PostMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_UPDATEMYINFO, 0);
	}
	return m_Params.Sessions.GetCount();
}

void CWebServer::_InsertCatBox(CString &Out, int preselect, LPCTSTR boxlabel, bool jump, bool extraCats, const CString &sSession, const CString &sFileHash, bool ed2kbox, int iFileCategory)
{
	CString tempBuf;
	tempBuf.Format(_T("<form action=\"\">%s<select name=\"cat\" size=\"1\"%s>")
		, boxlabel
		, jump ? _T(" onchange=\"GotoCat(this.form.cat.options[this.form.cat.selectedIndex].value)\"") : _T(""));

	for (INT_PTR i = 0; i < thePrefs.GetCatCount(); ++i) {
		CString strCategory(thePrefs.GetCategoryDisplayTitle(i));
		strCategory.Replace(_T("'"), _T("\\'"));
		tempBuf.AppendFormat(_T("<option%s value=\"%i\">%s</option>\n"), (i == preselect) ? _T(" selected") : _T(""), (int)i, (LPCTSTR)strCategory);
	}
	if (extraCats) {
		if (thePrefs.GetCatCount() > 1)
			tempBuf += _T("<option>-------------------</option>\n");

		for (int i = 1; i < 16; ++i)
			tempBuf.AppendFormat(_T("<option%s value=\"%i\">%s</option>\n"), (-i == preselect) ? _T(" selected") : _T(""), -i, (LPCTSTR)_GetSubCatLabel(-i));
	}
	tempBuf += _T("</select></form>");
	Out.Replace(ed2kbox ? _T("[CATBOXED2K]") : _T("[CATBOX]"), tempBuf);

	LPCTSTR tempBuff3;
	CString tempBuff4;
	CString tempBuff;

	for (INT_PTR i = 0; i < thePrefs.GetCatCount(); ++i) {
		CString strCategory(thePrefs.GetCategoryDisplayTitle(i));
		if (i == preselect) {
			tempBuff3 = _T("checked.gif");
			tempBuff4 = strCategory;
		} else
			tempBuff3 = _T("checked_no.gif");

		strCategory.Replace(_T("'"), _T("\\'"));

		tempBuff.AppendFormat(_T("<a href=&quot;/?ses=%s&amp;w=transfer&amp;cat=%d&quot;><div class=menuitems><img class=menuchecked src=%s>%s&nbsp;</div></a>")
			, (LPCTSTR)sSession, (int)i, tempBuff3, (LPCTSTR)strCategory);
	}
	if (extraCats) {
		tempBuff += _T("<div class=menuitems>&nbsp;------------------------------&nbsp;</div>");
		for (int i = 1; i < 16; ++i) {
			if (-i == preselect) {
				tempBuff3 = _T("checked.gif");
				tempBuff4 = _GetSubCatLabel(-i);
			} else
				tempBuff3 = _T("checked_no.gif");

			tempBuff.AppendFormat(_T("<a href=&quot;/?ses=%s&amp;w=transfer&amp;cat=%d&quot;><div class=menuitems><img class=menuchecked src=%s>%s&nbsp;</div></a>")
				, (LPCTSTR)sSession, -i, tempBuff3, (LPCTSTR)_GetSubCatLabel(-i));
		}
	}
	Out.Replace(_T("[CatBox]"), tempBuff);
	tempBuff4.Replace(_T("'"), _T("\\'"));
	Out.Replace(_T("[Category]"), tempBuff4);

	if (!sFileHash.IsEmpty() && iFileCategory >= 0)
		preselect = iFileCategory;
	tempBuff.Empty();
	//	For each user category index...
	for (INT_PTR i = 0; i < thePrefs.GetCatCount(); ++i) {
		CString strCategory(i ? thePrefs.GetCategoryDisplayTitle(i) : GetResString(_T("CAT_UNASSIGN")));
		strCategory.Replace(_T("'"), _T("\\'"));

		tempBuff3 = (i == preselect) ? _T("checked.gif") : _T("checked_no.gif");
		tempBuff.AppendFormat(_T("<a href=&quot;/?ses=%s&amp;w=transfer[CatSel]&amp;op=setcat&amp;file=%s&amp;filecat=%d&quot;><div class=menuitems><img class=menuchecked src=%s>%s&nbsp;</div></a>")
			, (LPCTSTR)sSession, (LPCTSTR)sFileHash, (int)i, (LPCTSTR)tempBuff3, (LPCTSTR)strCategory);
	}

	Out.Replace(_T("[SetCatBox]"), tempBuff);
}

CString CWebServer::_GetSubCatLabel(int cat)
{
	if (cat >= 0 || cat < -16)
		return CString(_T('?'));

	static const LPCTSTR uids[16] =
	{
		_T("ALLOTHERS"), _T("STATUS_NOTCOMPLETED"), _T("DL_TRANSFCOMPL"), _T("WAITING")
		, _T("DOWNLOADING"), _T("ERRORLIKE"), _T("PAUSED"), _T("SEENCOMPL")
		, _T("VIDEO"), _T("AUDIO"), _T("SEARCH_ARC"), _T("SEARCH_CDIMG")
		, _T("SEARCH_DOC"), _T("SEARCH_PICS"), _T("SEARCH_PRG"), _T("META_COLLECTION")
	};
	return _GetPlainResString(uids[-cat - 1]);
}

CString CWebServer::_GetRemoteLinkAddedOk(const ThreadData &Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	int cat = _tstoi(_ParseURL(Data.sURL, _T("cat")));
	const CString &HTTPTemp(_ParseURL(Data.sURL, _T("c")));
	QueueWebDownloadLinks(HTTPTemp, cat);

	CString Out;
	Out.Format(_T("<status result=\"OK\"><description>%s</description>"), (LPCTSTR)GetResString(_T("WEB_REMOTE_LINK_ADDED")));
	Out.AppendFormat(_T("<filename>%s</filename></status>"), (LPCTSTR)HTTPTemp);
	return Out;
}

CString CWebServer::_GetRemoteLinkAddedFailed(const ThreadData &Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	CString Out(_T("<status result=\"FAILED\" reason=\"WRONG_PASSWORD\">"));
	Out.AppendFormat(_T("<description>%s</description></status>"), (LPCTSTR)GetResString(_T("WEB_REMOTE_LINK_NOT_ADDED")));

	return Out;
}

void CWebServer::_SetLastUserCat(const ThreadData &Data, long lSession, int cat)
{
	CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return;

	_RemoveTimeOuts(Data);

	// find our session
	if (lSession != 0)
		for (INT_PTR i = pThis->m_Params.Sessions.GetCount(); --i >= 0;) {
			Session &ses = pThis->m_Params.Sessions[i];
			if (ses.lSession == lSession) {
				// if found, also reset expiration time
				ses.startTime = CTime::GetCurrentTime();
				ses.lastcat = cat;
				break;
			}
		}
}

int CWebServer::_GetLastUserCat(const ThreadData &Data, long lSession)
{
	CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return 0;

	_RemoveTimeOuts(Data);

	if (lSession != 0)
		// find our session
		for (INT_PTR i = pThis->m_Params.Sessions.GetCount(); --i >= 0;) {
			Session &ses = pThis->m_Params.Sessions[i];
			if (ses.lSession == lSession) {
				// if found, also reset expiration time
				ses.startTime = CTime::GetCurrentTime();
				return ses.lastcat;
			}
		}

	return 0;
}

void CWebServer::_ProcessFileReq(const ThreadData &Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return;
	CString contenttype;

	CString filename(Data.sURL);
	LPCTSTR pDot = ::PathFindExtension(filename);
	if (CPTR(filename, filename.GetLength()) > pDot + 2) { //at least 2 characters
		CString ext(pDot + 1); //skip the dot
		ext.MakeLower();
		if (ext == _T("bmp") || ext == _T("gif") || ext == _T("jpeg") || ext == _T("jpg") || ext == _T("png"))
			contenttype.Format(_T("Content-Type: image/%s\r\n"), (LPCTSTR)ext);
		//DonQ - additional file types
		else if (ext == _T("ico"))
			contenttype = _T("Content-Type: image/x-icon\r\n");
		else if (ext == _T("css"))
			contenttype = _T("Content-Type: text/css\r\n");
		else if (ext == _T("js"))
			contenttype = _T("Content-Type: text/javascript\r\n");
	}

	contenttype.AppendFormat(_T("Last-Modified: %s\r\nETag: %s\r\n"), (LPCTSTR)pThis->m_Params.sLastModified, (LPCTSTR)pThis->m_Params.sETag);

	filename.Replace(_T('/'), _T('\\'));
	if (filename[0] == _T('\\'))
		filename.Delete(0, 1);
	filename.Insert(0, thePrefs.GetMuleDirectory(EMULE_WEBSERVERDIR));

	CFile file;
	if (file.Open(filename, CFile::modeRead | CFile::shareDenyWrite | CFile::typeBinary)) {
		if (thePrefs.GetMaxWebUploadFileSizeMB() == 0 || file.GetLength() <= thePrefs.GetMaxWebUploadFileSizeMB() * 1024ull * 1024ull) {
			UINT filesize = (UINT)file.GetLength();

			char *buffer = new char[filesize];
			UINT size = file.Read(buffer, filesize);
			file.Close();
			Data.pSocket->SendContent((CStringA)contenttype, buffer, size);
			delete[] buffer;
		} else
			Data.pSocket->SendReply("HTTP/1.1 403 Forbidden\r\n");
	} else
		Data.pSocket->SendReply("HTTP/1.1 404 File not found\r\n");
}

CString CWebServer::_GetWebImageNameForFileType(const CString &filename)
{
	LPCTSTR p;
	switch (GetED2KFileTypeID(filename)) {
	case ED2KFT_AUDIO:
		p = _T("audio");
		break;
	case ED2KFT_VIDEO:
		p = _T("video");
		break;
	case ED2KFT_IMAGE:
		p = _T("picture");
		break;
	case ED2KFT_PROGRAM:
		p = _T("program");
		break;
	case ED2KFT_DOCUMENT:
		p = _T("document");
		break;
	case ED2KFT_ARCHIVE:
		p = _T("archive");
		break;
	case ED2KFT_CDIMAGE:
		p = _T("cdimage");
		break;
	case ED2KFT_EMULECOLLECTION:
		p = _T("emulecollection");
		break;
	default: /*ED2KFT_ANY:*/
		p = _T("other");
	}
	return CString(p);
}

CString CWebServer::_GetClientSummary(const CUpDownClient &client, const CString &strUploadFileName)
{
	CString buffer(GetResStringWithColon(_T("SW_NAME")));
	// name
	buffer.AppendFormat(_T(" %s\n"), client.GetUserName());
	// client version
	buffer.AppendFormat(_T("%s: %s\n"), (LPCTSTR)GetResString(_T("CD_CSOFT")), (LPCTSTR)client.DbgGetFullClientSoftVer());

	// uploading file
	buffer.AppendFormat(_T("%s "), (LPCTSTR)GetResString(_T("CD_UPLOADREQ")));
	if (!strUploadFileName.IsEmpty())
		buffer += strUploadFileName;

	// transferring time
	buffer.AppendFormat(_T("\n\n%s: %s\n"), (LPCTSTR)GetResString(_T("UPLOADTIME")), (LPCTSTR)CastSecondsToHM(client.GetUpStartTimeDelay() / SEC2MS(1)));

	// transferred data (up, down, global, session)
	buffer.AppendFormat(_T("%s (%s):\n"), (LPCTSTR)GetResStringWithColon(_T("DL_TRANSF")), (LPCTSTR)GetResString(_T("STATS_SESSION")));
	buffer.AppendFormat(_T(".....%s: %s (%s )\n"), (LPCTSTR)GetResString(_T("PW_CON_UPLBL")), (LPCTSTR)CastItoXBytes(client.GetTransferredUp()), (LPCTSTR)CastItoXBytes(client.GetSessionUp()));
	buffer.AppendFormat(_T(".....%s: %s (%s )\n"), (LPCTSTR)GetResString(_T("DOWNLOAD")), (LPCTSTR)CastItoXBytes(client.GetTransferredDown()), (LPCTSTR)CastItoXBytes(client.GetSessionDown()));

	return buffer;
}

void CWebServer::_GetClientversionImage(const CUpDownClient &client, TCHAR pSoft[2])
{
	switch (client.GetClientSoft()) {
	case SO_EMULE:
	case SO_OLDEMULE:
		*pSoft = _T('1');
		break;
	case SO_EDONKEYHYBRID:
		*pSoft = _T('h');
		break;
	case SO_AMULE:
		*pSoft = _T('a');
		break;
	case SO_SHAREAZA:
		*pSoft = _T('s');
		break;
	case SO_MLDONKEY:
		*pSoft = _T('m');
		break;
	case SO_LPHANT:
		*pSoft = _T('l');
		break;
	case SO_URL:
		*pSoft = _T('u');
		break;
	//case SO_EDONKEY:
	default:
		*pSoft = _T('0');
	}
	pSoft[1] = _T('\0');
}

CString CWebServer::_GetCommentlist(const ThreadData &Data)
{
	CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	const CString strFileHash(_ParseURL(Data.sURL, _T("filehash")));
	if (theApp.IsUiThread()) {
		CString strCommentList(BuildCommentListForWebThread(Data));
		pThis->StoreWebCommentList(strFileHash, strCommentList);
		return strCommentList;
	}

	CString strCachedCommentList;
	const bool bHasCachedCommentList = pThis->CopyWebCommentList(strFileHash, strCachedCommentList);
	SWebCommentListRequest *pRequest = new SWebCommentListRequest();
	pRequest->m_pThis = pThis;
	pRequest->m_Data = Data;
	pRequest->m_strFileHash = strFileHash;
	QueueWebUiSnapshotRefresh(WEB_GET_COMMENT_LIST, 0, pRequest);
	return bHasCachedCommentList ? strCachedCommentList : CString();
}

CString CWebServer::BuildCommentListForWebThread(const ThreadData &Data)
{
	uchar FileHash[MDX_DIGEST_SIZE];
	bool bHash = strmd4(_ParseURL(Data.sURL, _T("filehash")), FileHash);
	const CPartFile *pPartFile = (bHash && theApp.downloadqueue != NULL) ? theApp.downloadqueue->GetFileByID(FileHash) : NULL;
	if (!pPartFile)
		return CString();

	const CWebServer *pThis = reinterpret_cast<CWebServer*>(Data.pThis);
	if (pThis == NULL)
		return CString();

	CString Out(pThis->m_Templates.sCommentList);

	CString comments(GetResString(_T("COMMENT")));
	comments.AppendFormat(_T(": %s"), (LPCTSTR)pPartFile->GetFileName());
	Out.Replace(_T("[COMMENTS]"), comments);

	CString commentlines;
	// prepare comments info string
	for (POSITION pos = pPartFile->srclist.GetHeadPosition(); pos != NULL;) {
		const CUpDownClient *cur_src = pPartFile->srclist.GetNext(pos);
		if (cur_src->HasFileRating() || !cur_src->GetFileComment().IsEmpty())
			commentlines.AppendFormat(pThis->m_Templates.sCommentListLine
				, (LPCTSTR)_SpecialChars(cur_src->GetUserName())
				, (LPCTSTR)_SpecialChars(cur_src->GetClientFilename())
				, (LPCTSTR)_SpecialChars(cur_src->GetFileComment())
				, (LPCTSTR)_SpecialChars(GetRateString(cur_src->GetFileRating())));
	}

	const CTypedPtrList<CPtrList, Kademlia::CEntry*> &list = pPartFile->getNotes();
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		const Kademlia::CEntry *entry = list.GetNext(pos);
		commentlines.AppendFormat(pThis->m_Templates.sCommentListLine
			, _T("")
			, (LPCTSTR)_SpecialChars(entry->GetCommonFileName())
			, (LPCTSTR)_SpecialChars(entry->GetStrTagValue(Kademlia::CKadTagNameString(TAG_DESCRIPTION)))
			, (LPCTSTR)_SpecialChars(GetRateString((UINT)entry->GetIntTagValue(Kademlia::CKadTagNameString(TAG_FILERATING)))));
	}

	Out.Replace(_T("[COMMENTLINES]"), commentlines);

	Out.Replace(_T("[COMMENTS]"), _T(""));
	Out.Replace(_T("[USERNAME]"), GetResString(_T("QL_USERNAME")));
	Out.Replace(_T("[FILENAME]"), GetResString(_T("DL_FILENAME")));
	Out.Replace(_T("[COMMENT]"), GetResString(_T("COMMENT")));
	Out.Replace(_T("[RATING]"), GetResString(_T("QL_RATING")));
	Out.Replace(_T("[CLOSE]"), GetResString(_T("CW_CLOSE")));
	Out.Replace(_T("[CharSet]"), HTTPENCODING);

	return Out;
}

int AFX_CDECL CWebServer::_DownloadCmp(void *prm, void const *pv1, void const *pv2)
{
	const DownloadFiles &p1 = *reinterpret_cast<const DownloadFiles*>(pv1);
	const DownloadFiles &p2 = *reinterpret_cast<const DownloadFiles*>(pv2);
	int iOrd;
	switch ((DownloadSort)((SortParams*)prm)->eSort) {
	case DOWN_SORT_STATE:
		iOrd = p1.iFileState - p2.iFileState;
		break;
	case DOWN_SORT_TYPE:
		iOrd = p1.sFileType.CompareNoCase(p2.sFileType);
		break;
	case DOWN_SORT_NAME:
		iOrd = p1.sFileName.CompareNoCase(p2.sFileName);
		break;
	case DOWN_SORT_SIZE:
		iOrd = CompareUnsigned(p1.m_qwFileSize, p2.m_qwFileSize);
		break;
	case DOWN_SORT_TRANSFERRED:
		iOrd = CompareUnsigned(p1.m_qwFileTransferred, p2.m_qwFileTransferred);
		break;
	case DOWN_SORT_SPEED:
		iOrd = p1.lFileSpeed - p2.lFileSpeed;
		break;
	case DOWN_SORT_PROGRESS:
		iOrd = sgn(p1.m_dblCompleted - p2.m_dblCompleted);
		break;
	case DOWN_SORT_SOURCES:
		iOrd = p1.lSourceCount - p2.lSourceCount;
		break;
	case DOWN_SORT_PRIORITY:
		iOrd = p1.nFilePrio - p2.nFilePrio;
		break;
	case DOWN_SORT_CATEGORY:
		iOrd = p1.sCategory.CompareNoCase(p2.sCategory);
		break;
	default: //unknown
		return 0;
	}
	return ((SortParams*)prm)->bReverse ? iOrd : -iOrd;
}

int AFX_CDECL CWebServer::_ServerCmp(void *prm, void const *pv1, void const *pv2)
{
	const ServerEntry &p1 = *reinterpret_cast<const ServerEntry*>(pv1);
	const ServerEntry &p2 = *reinterpret_cast<const ServerEntry*>(pv2);
	int iOrd;
	switch ((ServerSort)((SortParams*)prm)->eSort) {
	case SERVER_SORT_STATE:
		iOrd = p2.sServerState.CompareNoCase(p1.sServerState); //reversed
		break;
	case SERVER_SORT_NAME:
		iOrd = p1.sServerName.CompareNoCase(p2.sServerName);
		break;
	case SERVER_SORT_IP:
		iOrd = p1.sServerFullIP.Compare(p2.sServerFullIP);
		if (!iOrd)
			iOrd = p1.nServerPort - p2.nServerPort;
		break;
	case SERVER_SORT_DESCRIPTION:
		iOrd = p1.sServerDescription.CompareNoCase(p2.sServerDescription);
		break;
	case SERVER_SORT_PING:
		iOrd = p1.nServerPing - p2.nServerPing;
		break;
	case SERVER_SORT_USERS:
		iOrd = p1.nServerUsers - p2.nServerUsers;
		break;
	case SERVER_SORT_FILES:
		iOrd = p1.nServerFiles - p2.nServerFiles;
		break;
	case SERVER_SORT_PRIORITY:
		iOrd = p1.nServerPriority - p2.nServerPriority;
		break;
	case SERVER_SORT_FAILED:
		iOrd = p1.nServerFailed - p2.nServerFailed;
		break;
	case SERVER_SORT_LIMIT:
		iOrd = p1.nServerSoftLimit - p2.nServerSoftLimit;
		break;
	case SERVER_SORT_VERSION:
		iOrd = p1.sServerVersion.Compare(p2.sServerVersion);
		break;
	default: //unknown
		return 0;
	}
	return ((SortParams*)prm)->bReverse ? iOrd : -iOrd;
}

int AFX_CDECL CWebServer::_SharedCmp(void *prm, void const *pv1, void const *pv2)
{
	const SharedFiles &p1 = *reinterpret_cast<const SharedFiles*>(pv1);
	const SharedFiles &p2 = *reinterpret_cast<const SharedFiles*>(pv2);
	int iOrd;
	switch ((SharedSort)((SortParams*)prm)->eSort) {
	case SHARED_SORT_STATE:
		iOrd = p2.sFileState.CompareNoCase(p1.sFileState); //reversed
		break;
	case SHARED_SORT_TYPE:
		iOrd = p2.sFileType.CompareNoCase(p1.sFileType); //reversed
		break;
	case SHARED_SORT_NAME:
		iOrd = p1.sFileName.CompareNoCase(p2.sFileName);
		break;
	case SHARED_SORT_SIZE:
		iOrd = CompareUnsigned(p1.m_qwFileSize, p2.m_qwFileSize);
		break;
	case SHARED_SORT_TRANSFERRED:
		iOrd = CompareUnsigned(p1.nFileTransferred, p2.nFileTransferred);
		break;
	case SHARED_SORT_ALL_TIME_TRANSFERRED:
		iOrd = CompareUnsigned(p1.nFileAllTimeTransferred, p2.nFileAllTimeTransferred);
		break;
	case SHARED_SORT_REQUESTS:
		iOrd = p1.nFileRequests - p2.nFileRequests;
		break;
	case SHARED_SORT_ALL_TIME_REQUESTS:
		iOrd = p1.nFileAllTimeRequests - p2.nFileAllTimeRequests;
		break;
	case SHARED_SORT_ACCEPTS:
		iOrd = p1.nFileAccepts - p2.nFileAccepts;
		break;
	case SHARED_SORT_ALL_TIME_ACCEPTS:
		iOrd = p1.nFileAllTimeAccepts - p2.nFileAllTimeAccepts;
		break;
	case SHARED_SORT_COMPLETES:
		iOrd = sgn(p1.dblFileCompletes - p2.dblFileCompletes);
		break;
	case SHARED_SORT_PRIORITY:
		iOrd = CSharedFileList::GetRealPrio(p1.nFilePriority) - CSharedFileList::GetRealPrio(p2.nFilePriority);
		break;
	default: //unknown
		return 0;
	}
	return ((SortParams*)prm)->bReverse ? iOrd : -iOrd;
}

int AFX_CDECL CWebServer::_UploadCmp(void *prm, void const *pv1, void const *pv2)
{
	const UploadUsers &p1 = *reinterpret_cast<const UploadUsers*>(pv1);
	const UploadUsers &p2 = *reinterpret_cast<const UploadUsers*>(pv2);
	int iOrd;
	switch ((UploadSort)((SortParams*)prm)->eSort) {
	case UP_SORT_CLIENT:
		iOrd = *p1.sClientSoft - *p2.sClientSoft;
		break;
	case UP_SORT_USER:
		iOrd = p1.sUserName.CompareNoCase(p2.sUserName);
		break;
	case UP_SORT_VERSION:
		iOrd = p1.sClientNameVersion.CompareNoCase(p2.sClientNameVersion);
		break;
	case UP_SORT_FILENAME:
		iOrd = p1.sFileName.CompareNoCase(p2.sFileName);
		break;
	case UP_SORT_TRANSFERRED:
		iOrd = CompareUnsigned(p1.nTransferredUp, p2.nTransferredUp);
		break;
	case UP_SORT_SPEED:
		iOrd = p1.nDataRate - p2.nDataRate;
		break;
	default: //unknown
		return 0;
	}
	return ((SortParams*)prm)->bReverse ? iOrd : -iOrd;
}
