//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#include "StdAfx.h"
#include <afxinet.h>
#include <share.h>
#include "IPGeolocation.h"
#include "emule.h"
#include "Preferences.h"
#include "otherfunctions.h"
#include "log.h"
#include "serverlist.h"
#include "TransferDlg.h"
#include "clientlist.h"
#include "emuledlg.h"
#include "serverwnd.h"
#include "serverlistctrl.h"
#include "kademliawnd.h"
#include "ZIPFile.h"
#include "GZipFile.h"
#include "eMuleAI/DarkMode.h"
#include <system_error>
#include <eMuleAI/json.hpp> 

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	const LPCTSTR DFLT_GEOLOCATION_DB_FILENAME = IPGEOLOCATION_DB_FILENAME;
	const LPCTSTR DFLT_GEOLOCATION_DB_URL_TEMPLATE = _T("https://download.db-ip.com/free/dbip-city-lite-%Y-%m.mmdb.gz");
	const DWORD IPGEOLOCATION_DOWNLOAD_PROGRESS_INTERVAL = 250;

	struct SIPGeolocationDownloadJob
	{
		SIPGeolocationDownloadJob()
			: lRefCount(1)
			, lCancel()
			, hNotifyWnd()
			, uToken()
			, bInteractive(false)
		{
		}

		LONG lRefCount;
		volatile LONG lCancel;
		HWND hNotifyWnd;
		uint64 uToken;
		bool bInteractive;
		CString strURL;
		CString strTempFile;
	};

	struct SIPGeolocationDownloadProgress
	{
		SIPGeolocationDownloadProgress()
			: uToken()
			, uBytesRead()
			, uTotalBytes()
			, bHasTotalBytes(false)
		{
		}

		uint64 uToken;
		uint64 uBytesRead;
		uint64 uTotalBytes;
		bool bHasTotalBytes;
		CString strURL;
	};

	struct SIPGeolocationDownloadResult
	{
		SIPGeolocationDownloadResult()
			: uToken()
			, uBytesRead()
			, uTotalBytes()
			, bHasTotalBytes(false)
			, bSucceeded(false)
			, bCanceled(false)
			, bInteractive(false)
		{
		}

		uint64 uToken;
		uint64 uBytesRead;
		uint64 uTotalBytes;
		bool bHasTotalBytes;
		bool bSucceeded;
		bool bCanceled;
		bool bInteractive;
		CString strURL;
		CString strTempFile;
		CString strError;
	};

	SIPGeolocationDownloadJob* g_pIPGeolocationDownloadJob = NULL;
	uint64 g_uIPGeolocationDownloadToken = 0;
	uint64 g_uIPGeolocationDownloadBytesRead = 0;
	uint64 g_uIPGeolocationDownloadTotalBytes = 0;
	bool g_bIPGeolocationDownloadHasTotalBytes = false;
	bool g_bIPGeolocationDownloadOverlayDelayActive = false;
	CString g_strIPGeolocationDownloadURL;

	CString GetDefaultIPGeolocationFilePath()
	{
		return thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + DFLT_GEOLOCATION_DB_FILENAME;
	}

	void ReportIPGeolocationUpdateError(const CString &strError, bool bInteractive)
	{
		if (bInteractive)
			CDarkMode::MessageBox(strError, MB_ICONERROR);
		else
			AddDebugLogLine(DLP_LOW, false, _T("%s"), (LPCTSTR)strError);
	}

	void AddIPGeolocationDownloadJobRef(SIPGeolocationDownloadJob* pJob)
	{
		if (pJob == NULL)
			return;
		InterlockedIncrement(&pJob->lRefCount);
	}

	void ReleaseIPGeolocationDownloadJob(SIPGeolocationDownloadJob* pJob)
	{
		if (pJob != NULL && InterlockedDecrement(&pJob->lRefCount) == 0)
			delete pJob;
	}

	bool IsIPGeolocationDownloadCanceled(SIPGeolocationDownloadJob* pJob)
	{
		return pJob == NULL || InterlockedCompareExchange(&pJob->lCancel, 0, 0) != 0;
	}

	void SetIPGeolocationDownloadError(SIPGeolocationDownloadResult& result, LPCTSTR pszContext, DWORD dwError)
	{
		CString strLastError;
		if (dwError >= INTERNET_ERROR_BASE && dwError <= INTERNET_ERROR_LAST)
			GetModuleErrorString(dwError, strLastError, _T("wininet"));
		else
			GetSystemErrorString(dwError, strLastError);

		if (strLastError.IsEmpty())
			result.strError = pszContext;
		else
			result.strError.Format(_T("%s: %s"), pszContext, (LPCTSTR)strLastError);
	}

	bool PostIPGeolocationDownloadPayload(SIPGeolocationDownloadJob* pJob, UINT uMessage, LPARAM lParam)
	{
		HWND hWnd = pJob != NULL ? pJob->hNotifyWnd : NULL;
		return hWnd != NULL && ::IsWindow(hWnd) && ::PostMessage(hWnd, uMessage, 0, lParam);
	}

	void PostIPGeolocationDownloadProgress(SIPGeolocationDownloadJob* pJob, uint64 uBytesRead, uint64 uTotalBytes, bool bHasTotalBytes, DWORD& dwLastProgressTick, bool bForce)
	{
		const DWORD dwNow = ::GetTickCount();
		if (!bForce && dwLastProgressTick != 0 && dwNow - dwLastProgressTick < IPGEOLOCATION_DOWNLOAD_PROGRESS_INTERVAL)
			return;

		SIPGeolocationDownloadProgress* pProgress = new SIPGeolocationDownloadProgress;
		pProgress->uToken = pJob->uToken;
		pProgress->uBytesRead = uBytesRead;
		pProgress->uTotalBytes = uTotalBytes;
		pProgress->bHasTotalBytes = bHasTotalBytes;
		pProgress->strURL = pJob->strURL;

		if (PostIPGeolocationDownloadPayload(pJob, CemuleDlg::UWM_EMULEAI_IPGEOLOCATION_DOWNLOAD_PROGRESS, reinterpret_cast<LPARAM>(pProgress)))
			dwLastProgressTick = dwNow;
		else
			delete pProgress;
	}

	void BuildIPGeolocationDownloadOverlayText(uint64 uBytesRead, uint64 uTotalBytes, bool bHasTotalBytes, const CString& strURL, CString& strTitle, CString& strDetail, UINT& uDone, UINT& uTotal)
	{
		uDone = 0;
		uTotal = 0;
		if (bHasTotalBytes && uTotalBytes > 0) {
			const uint64 uClampedBytesRead = uBytesRead < uTotalBytes ? uBytesRead : uTotalBytes;
			uint64 uPercent64 = (uClampedBytesRead * 100) / uTotalBytes;
			if (uPercent64 > 100)
				uPercent64 = 100;
			const UINT uPercent = static_cast<UINT>(uPercent64);
			CString strBytesRead = CastItoXBytes(uBytesRead);
			CString strTotalBytes = CastItoXBytes(uTotalBytes);
			strDetail.Format(GetResString(_T("SERVERMET_DOWNLOAD_PROGRESS")), uPercent, (LPCTSTR)strBytesRead, (LPCTSTR)strTotalBytes, (LPCTSTR)strURL);
			uDone = uPercent;
			uTotal = 100;
		} else {
			CString strBytesRead = CastItoXBytes(uBytesRead);
			strDetail.Format(GetResString(_T("SERVERMET_DOWNLOAD_PROGRESS_UNKNOWN")), (LPCTSTR)strBytesRead, (LPCTSTR)strURL);
		}

		strTitle = GetResString(_T("DOWNLOADING_IPGEOLOCATION_FILE"));
	}

	void UpdateIPGeolocationDownloadOverlays(uint64 uBytesRead, uint64 uTotalBytes, bool bHasTotalBytes, const CString& strURL)
	{
		g_uIPGeolocationDownloadBytesRead = uBytesRead;
		g_uIPGeolocationDownloadTotalBytes = uTotalBytes;
		g_bIPGeolocationDownloadHasTotalBytes = bHasTotalBytes;
		g_strIPGeolocationDownloadURL = strURL;
		if (theApp.emuledlg != NULL)
			theApp.emuledlg->RefreshActiveBulkOperationOverlays();
	}

	void ResetIPGeolocationDownloadOverlayState()
	{
		g_uIPGeolocationDownloadBytesRead = 0;
		g_uIPGeolocationDownloadTotalBytes = 0;
		g_bIPGeolocationDownloadHasTotalBytes = false;
		g_bIPGeolocationDownloadOverlayDelayActive = false;
		g_strIPGeolocationDownloadURL.Empty();
	}

	void StartIPGeolocationDownloadOverlayDelay()
	{
		g_bIPGeolocationDownloadOverlayDelayActive = true;
		if (theApp.emuledlg != NULL)
			theApp.emuledlg->StartDownloadOverlayCompletionDelay();
	}

	void HideIPGeolocationDownloadOverlays()
	{
		ResetIPGeolocationDownloadOverlayState();
		if (theApp.emuledlg != NULL)
			theApp.emuledlg->RefreshActiveBulkOperationOverlays();
	}

	bool ValidateIPGeolocationDatabase(const CString& strFilePath, CString& strError)
	{
		try {
			IPGeolocationDB::DB validator((std::string)CT2CA(strFilePath.GetString()));
			(void)validator.get_metadata();
			return true;
		} catch (const std::exception& ex) {
			strError = UTF8To16(ex.what());
		} catch (...) {
			strError = _T("Unknown IP geolocation database validation error.");
		}
		return false;
	}

	bool ExtractDownloadedIPGeolocationDatabase(const CString& strArchivePath, CString& strDatabasePath, CString& strError)
	{
		const CString& sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
		strDatabasePath.Empty();

		CZIPFile zip;
		if (zip.Open(strArchivePath)) {
			CZIPFile::File* zfile = zip.GetFile(DFLT_GEOLOCATION_DB_FILENAME, TRUE);
			if (zfile == NULL) {
				for (int i = 0; i < zip.GetCount(); ++i) {
					CZIPFile::File* pFile = zip.GetFile(i);
					if (pFile != NULL && pFile->m_sName.Right(5).CompareNoCase(_T(".mmdb")) == 0) {
						zfile = pFile;
						break;
					}
				}
			}
			if (zfile == NULL) {
				strError.Format(GetResString(_T("IPGEOLOCATION_CONTENT_ERROR")), (LPCTSTR)strArchivePath);
				zip.Close();
				return false;
			}

			strDatabasePath = sConfDir + IPGEOLOCATION_DB_FILENAME _T(".unzip.tmp");
			if (!zfile->Extract(strDatabasePath)) {
				strError.Format(GetResString(_T("IPGEOLOCATION_ARCHIVE_ERROR")), (LPCTSTR)strArchivePath);
				zip.Close();
				return false;
			}
			zip.Close();
			return true;
		}

		CGZIPFile gz;
		if (gz.Open(strArchivePath)) {
			strDatabasePath = sConfDir + IPGEOLOCATION_DB_FILENAME _T(".unzip.tmp");
			if (!gz.Extract(strDatabasePath)) {
				strError.Format(GetResString(_T("IPGEOLOCATION_ARCHIVE_ERROR")), (LPCTSTR)strArchivePath);
				gz.Close();
				return false;
			}
			gz.Close();
			return true;
		}
		gz.Close();

		strDatabasePath = strArchivePath;
		return true;
	}

	bool ReplaceDefaultIPGeolocationFile(const CString& strSourceFilePath, const CString& strTempFileToRemove, bool bAddToStatusBar, CString& strError)
	{
		const CString strDefaultFilePath = GetDefaultIPGeolocationFilePath();
		if (theApp.ipgeolocation != NULL)
			theApp.ipgeolocation->UnloadIPGeolocation();

		if (!::MoveFileEx(PreparePathForWin32LongPath(strSourceFilePath), PreparePathForWin32LongPath(strDefaultFilePath), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			strError.Format(GetResString(_T("IPGEOLOCATION_INSTALL_ERROR")), (LPCTSTR)strDefaultFilePath);
			if (theApp.ipgeolocation != NULL)
				theApp.ipgeolocation->LoadIPGeolocation();
			return false;
		}

		if (!strTempFileToRemove.IsEmpty())
			(void)::DeleteFile(PreparePathForWin32LongPath(strTempFileToRemove));

		if (theApp.ipgeolocation != NULL) {
			theApp.ipgeolocation->LoadIPGeolocation(bAddToStatusBar);
			theApp.ipgeolocation->Redraw();
		}
		return true;
	}

	bool ApplyDownloadedIPGeolocationFile(const CString &url, const CString &strTempFilePath, bool bInteractive)
	{
		CString strDatabasePath;
		CString strError;
		if (!ExtractDownloadedIPGeolocationDatabase(strTempFilePath, strDatabasePath, strError)) {
			ReportIPGeolocationUpdateError(strError, bInteractive);
			return false;
		}

		CString strValidationError;
		if (!ValidateIPGeolocationDatabase(strDatabasePath, strValidationError)) {
			CString strReport(GetResString(_T("IPGEOLOCATION_DOWNLOAD_FAILED")));
			if (!strValidationError.IsEmpty())
				strReport.AppendFormat(_T("\r\n\r\n%s"), (LPCTSTR)strValidationError);
			ReportIPGeolocationUpdateError(strReport, bInteractive);
			if (strDatabasePath.CompareNoCase(strTempFilePath) != 0)
				(void)::DeleteFile(PreparePathForWin32LongPath(strDatabasePath));
			return false;
		}

		const bool bExtracted = strDatabasePath.CompareNoCase(strTempFilePath) != 0;
		if (!ReplaceDefaultIPGeolocationFile(strDatabasePath, bExtracted ? strTempFilePath : CString(), bInteractive, strError)) {
			ReportIPGeolocationUpdateError(strError, bInteractive);
			if (bExtracted)
				(void)::DeleteFile(PreparePathForWin32LongPath(strDatabasePath));
			return false;
		}

		thePrefs.SetLastIPGeolocationUpdate(time(nullptr));
		AddLogLine(bInteractive, GetResString(_T("IPGEOLOCATION_UPDATED")), (LPCTSTR)url);
		return true;
	}

	bool DownloadIPGeolocationToFile(SIPGeolocationDownloadJob* pJob, SIPGeolocationDownloadResult& result)
	{
		result.uToken = pJob->uToken;
		result.strURL = pJob->strURL;
		result.strTempFile = pJob->strTempFile;
		result.bInteractive = pJob->bInteractive;
		if (theApp.IsNetworkActivityBlockedByBind()) {
			result.strError = theApp.GetNetworkActivityBlockMessage();
			return false;
		}

		DWORD dwServiceType = 0;
		CString strServer;
		CString strObject;
		INTERNET_PORT nPort = 0;
		CString strURL(pJob->strURL);
		if (!AfxParseURL(strURL, dwServiceType, strServer, strObject, nPort)) {
			strURL = _T("http://") + strURL;
			if (!AfxParseURL(strURL, dwServiceType, strServer, strObject, nPort)) {
				result.strError = GetResString(_T("INVALIDURL"));
				return false;
			}
		}
		if (dwServiceType != AFX_INET_SERVICE_HTTP && dwServiceType != AFX_INET_SERVICE_HTTPS) {
			result.strError = GetResString(_T("INVALIDURL"));
			return false;
		}
		if (strObject.IsEmpty())
			strObject = _T("/");

		CFile file;
		bool bFileOpen = false;
		HINTERNET hInternetSession = NULL;
		HINTERNET hHttpConnection = NULL;
		HINTERNET hHttpFile = NULL;
		bool bSuccess = false;
		bool bDeleteTempFile = true;
		DWORD dwLastProgressTick = 0;
		DWORD dwTimeout = SEC2MS(30);
		DWORD dwRequestFlags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_DONT_CACHE | INTERNET_FLAG_KEEP_CONNECTION;
		static LPCTSTR ppszAcceptTypes[2] = {_T("*/*"), NULL};
		TCHAR szStatusCode[32] = {};
		TCHAR szContentLength[64] = {};
		DWORD dwInfoSize = 0;
		long nStatusCode = 0;
		char szReadBuf[16 * 1024];

		CFileException fex;
		if (!file.Open(PreparePathForWin32LongPath(pJob->strTempFile), CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite, &fex)) {
			SetIPGeolocationDownloadError(result, _T("Failed to open download target"), fex.m_lOsError);
			goto cleanup;
		}
		bFileOpen = true;

		hInternetSession = ::InternetOpen(AfxGetAppName(), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
		if (hInternetSession == NULL) {
			SetIPGeolocationDownloadError(result, _T("InternetOpen failed"), ::GetLastError());
			goto cleanup;
		}

		(void)::InternetSetOption(hInternetSession, INTERNET_OPTION_CONNECT_TIMEOUT, &dwTimeout, sizeof dwTimeout);
		(void)::InternetSetOption(hInternetSession, INTERNET_OPTION_SEND_TIMEOUT, &dwTimeout, sizeof dwTimeout);
		(void)::InternetSetOption(hInternetSession, INTERNET_OPTION_RECEIVE_TIMEOUT, &dwTimeout, sizeof dwTimeout);

		if (IsIPGeolocationDownloadCanceled(pJob)) {
			result.bCanceled = true;
			goto cleanup;
		}

		if (dwServiceType == AFX_INET_SERVICE_HTTPS) {
			dwRequestFlags |= INTERNET_FLAG_SECURE;
			dwServiceType = INTERNET_SERVICE_HTTP;
		}

		hHttpConnection = ::InternetConnect(hInternetSession, strServer, nPort, NULL, NULL, dwServiceType, 0, 0);
		if (hHttpConnection == NULL) {
			SetIPGeolocationDownloadError(result, _T("InternetConnect failed"), ::GetLastError());
			goto cleanup;
		}

		hHttpFile = ::HttpOpenRequest(hHttpConnection, _T("GET"), strObject, NULL, NULL, ppszAcceptTypes, dwRequestFlags, 0);
		if (hHttpFile == NULL) {
			SetIPGeolocationDownloadError(result, _T("HttpOpenRequest failed"), ::GetLastError());
			goto cleanup;
		}

		(void)::HttpAddRequestHeaders(hHttpFile, _T("Accept-Encoding: identity, *;q=0\r\n"), _UI32_MAX, HTTP_ADDREQ_FLAG_ADD);
		(void)::HttpAddRequestHeaders(hHttpFile, _T("User-Agent: Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.0; SLCC1)\r\n"), _UI32_MAX, HTTP_ADDREQ_FLAG_ADD);

		if (!::HttpSendRequest(hHttpFile, NULL, 0, NULL, 0)) {
			SetIPGeolocationDownloadError(result, _T("HttpSendRequest failed"), ::GetLastError());
			goto cleanup;
		}

		dwInfoSize = sizeof szStatusCode;
		if (::HttpQueryInfo(hHttpFile, HTTP_QUERY_STATUS_CODE, szStatusCode, &dwInfoSize, NULL)) {
			nStatusCode = _ttol(szStatusCode);
			if (nStatusCode < 200 || nStatusCode >= 300) {
				result.strError.Format(_T("HTTP status %ld"), nStatusCode);
				goto cleanup;
			}
		}

		dwInfoSize = sizeof szContentLength;
		if (::HttpQueryInfo(hHttpFile, HTTP_QUERY_CONTENT_LENGTH, szContentLength, &dwInfoSize, NULL)) {
			result.uTotalBytes = _ttoi64(szContentLength);
			result.bHasTotalBytes = result.uTotalBytes > 0;
		}

		PostIPGeolocationDownloadProgress(pJob, 0, result.uTotalBytes, result.bHasTotalBytes, dwLastProgressTick, true);
		for (;;) {
			DWORD dwBytesRead = 0;
			if (IsIPGeolocationDownloadCanceled(pJob)) {
				result.bCanceled = true;
				goto cleanup;
			}
			if (!::InternetReadFile(hHttpFile, szReadBuf, sizeof szReadBuf, &dwBytesRead)) {
				SetIPGeolocationDownloadError(result, _T("InternetReadFile failed"), ::GetLastError());
				goto cleanup;
			}
			if (dwBytesRead == 0) {
				bSuccess = result.uBytesRead > 0;
				break;
			}
			try {
				file.Write(szReadBuf, dwBytesRead);
			} catch (CFileException* ex) {
				SetIPGeolocationDownloadError(result, _T("Failed to write download target"), ex->m_lOsError);
				ex->Delete();
				goto cleanup;
			}
			result.uBytesRead += dwBytesRead;
			PostIPGeolocationDownloadProgress(pJob, result.uBytesRead, result.uTotalBytes, result.bHasTotalBytes, dwLastProgressTick, false);
		}

		if (bSuccess) {
			try {
				file.Close();
				bFileOpen = false;
			} catch (CFileException* ex) {
				SetIPGeolocationDownloadError(result, _T("Failed to close download target"), ex->m_lOsError);
				ex->Delete();
				bSuccess = false;
			}
		}

		if (bSuccess) {
			if (!result.bHasTotalBytes) {
				result.uTotalBytes = result.uBytesRead;
				result.bHasTotalBytes = result.uTotalBytes > 0;
			}
			PostIPGeolocationDownloadProgress(pJob, result.uBytesRead, result.uTotalBytes, result.bHasTotalBytes, dwLastProgressTick, true);
			bDeleteTempFile = false;
		}

cleanup:
		if (hHttpFile != NULL)
			::InternetCloseHandle(hHttpFile);
		if (hHttpConnection != NULL)
			::InternetCloseHandle(hHttpConnection);
		if (hInternetSession != NULL)
			::InternetCloseHandle(hInternetSession);
		if (bFileOpen) {
			try {
				file.Close();
			} catch (CFileException* ex) {
				ex->Delete();
			}
		}
		if (bDeleteTempFile)
			(void)::DeleteFile(PreparePathForWin32LongPath(pJob->strTempFile));

		result.bSucceeded = bSuccess && !result.bCanceled;
		return result.bSucceeded;
	}

	UINT AFX_CDECL IPGeolocationDownloadThreadProc(LPVOID pParam)
	{
		DbgSetThreadName("IPGeoDownload");
		SIPGeolocationDownloadJob* pJob = reinterpret_cast<SIPGeolocationDownloadJob*>(pParam);
		SIPGeolocationDownloadResult* pResult = new SIPGeolocationDownloadResult;
		(void)DownloadIPGeolocationToFile(pJob, *pResult);
		if (!PostIPGeolocationDownloadPayload(pJob, CemuleDlg::UWM_EMULEAI_IPGEOLOCATION_DOWNLOAD_FINISHED, reinterpret_cast<LPARAM>(pResult)))
			delete pResult;
		ReleaseIPGeolocationDownloadJob(pJob);
		return 0;
	}

	bool StartIPGeolocationDownload(const CString& url, bool bInteractive)
	{
		if (url.IsEmpty())
			return false;
		if (g_pIPGeolocationDownloadJob != NULL) {
			if (bInteractive) {
				AddLogLine(true, GetResString(_T("IPGEOLOCATION_UPDATE_IN_PROGRESS")));
				if (theApp.emuledlg != NULL)
					theApp.emuledlg->FlushQueuedUiLogLines();
			}
			return true;
		}
		if (theApp.emuledlg == NULL || !::IsWindow(theApp.emuledlg->GetSafeHwnd()))
			return false;

		const CString& sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
		const CString strTempFilePath = sConfDir + IPGEOLOCATION_DB_FILENAME _T(".download.tmp");

		SIPGeolocationDownloadJob* pJob = new SIPGeolocationDownloadJob;
		pJob->hNotifyWnd = theApp.emuledlg->GetSafeHwnd();
		pJob->uToken = ++g_uIPGeolocationDownloadToken;
		pJob->bInteractive = bInteractive;
		pJob->strURL = url;
		pJob->strTempFile = strTempFilePath;
		g_pIPGeolocationDownloadJob = pJob;
		g_bIPGeolocationDownloadOverlayDelayActive = false;
		AddIPGeolocationDownloadJobRef(pJob);

		UpdateIPGeolocationDownloadOverlays(0, 0, false, url);
		if (bInteractive) {
			AddLogLine(true, GetResString(_T("IPGEOLOCATION_UPDATE_STARTED")), (LPCTSTR)url);
			if (theApp.emuledlg != NULL)
				theApp.emuledlg->FlushQueuedUiLogLines();
		}

		CWinThread* pThread = AfxBeginThread(IPGeolocationDownloadThreadProc, pJob, THREAD_PRIORITY_NORMAL);
		if (pThread == NULL) {
			g_pIPGeolocationDownloadJob = NULL;
			ReleaseIPGeolocationDownloadJob(pJob);
			ReleaseIPGeolocationDownloadJob(pJob);
			HideIPGeolocationDownloadOverlays();
			ReportIPGeolocationUpdateError(GetResString(_T("IPGEOLOCATION_DOWNLOAD_FAILED")), bInteractive);
			return false;
		}

		return true;
	}
}

const char *IPGeolocationDB::ErrorCategory::name( void ) const noexcept
{
	return "IPGeolocationDB";
}



std::string IPGeolocationDB::ErrorCategory::message( int code ) const
{
	std::string msg = MMDB_strerror( code );
	if ( msg.empty() )
	{
		msg = "unknown MMDB error #" + std::to_string( code );
	}

	return msg;
}


const IPGeolocationDB::ErrorCategory & IPGeolocationDB::get_error_category( void ) noexcept
{
	static ErrorCategory ecat;
	return ecat;
}


std::error_code IPGeolocationDB::make_error_code( IPGeolocationDB::MMDBStatus s )
{
	return std::error_code( static_cast<int>(s), get_error_category() );
}


std::error_condition IPGeolocationDB::make_error_condition( IPGeolocationDB::MMDBStatus s )
{
	return std::error_condition( static_cast<int>(s), get_error_category() );
}

#define IPGEOLOCATIONDB_VERSION ""


IPGeolocationDB::DB::~DB( void )
{
	MMDB_close( &mmdb );

	return;
}


IPGeolocationDB::DB::DB( const std::string &database_filename )
{
	const int status = MMDB_open( database_filename.c_str(), MMDB_MODE_MMAP, &mmdb );

	if (status != MMDB_SUCCESS)
	{
		const ErrorCategory	&	cat( get_error_category() );
		const std::error_code	ec( status, cat );
		const std::string		msg = "Failed to open the MMDB database \"" + database_filename + "\"";

		/** @throw std::system_error if the database file cannot be opened.
		 * @see @ref IPGeolocationDB::MMDBStatus
		 * @see @ref IPGeolocationDB::ErrorCategory
		 */
		throw std::system_error( ec, msg );
	}

	return;
}


std::string IPGeolocationDB::DB::get_lib_version_mmdb( void ) const
{
	return MMDB_lib_version();
}


std::string IPGeolocationDB::DB::get_lib_version_ipgeolocation( void ) const
{
	return IPGEOLOCATIONDB_VERSION;
}


MMDB_metadata_s IPGeolocationDB::DB::get_metadata_raw( void )
{
	return mmdb.metadata;
}


std::string IPGeolocationDB::DB::get_metadata( void )
{
	MMDB_entry_data_list_s *node = nullptr;
	const int status = MMDB_get_metadata_as_entry_data_list( &mmdb, &node );
	if (status)
	{
		/// @throw std::system_error if the lookup resulted in a MMDB error
		const ErrorCategory	&	cat( get_error_category() );
		const std::error_code	ec( status, cat );
		const std::string		msg = "Failed to lookup up meta data";
		throw std::system_error( ec, msg );
	}

	return to_json( node );
}


MMDB_lookup_result_s IPGeolocationDB::DB::lookup_raw( const std::string &ip_address )
{
	int gai_error	= 0;	// get_address_info() error
	int mmdb_error	= 0;

	MMDB_lookup_result_s result = MMDB_lookup_string( &mmdb, ip_address.c_str(), &gai_error, &mmdb_error );

	if (gai_error)
	{
		/// @throw std::invalid_argument if the address is invalid
		std::wstring wstrerr(gai_strerror(gai_error));
		throw std::invalid_argument(std::string(wstrerr.begin(), wstrerr.end()));
	}
	if (mmdb_error)
	{
		/// @throw std::system_error if the lookup resulted in a MMDB error
		const ErrorCategory	&	cat( get_error_category() );
		const std::error_code	ec( mmdb_error, cat );
		const std::string		msg = "Database error while looking up address \"" + ip_address + "\"";
		throw std::system_error( ec, msg );
	}
	if (result.found_entry == false)
	{
		/// @throw std::length_error if an entry was not found in the database
		throw std::length_error( "Failed to find address \"" + ip_address + "\"" );
	}

	return result;
}


void IPGeolocationDB::DB::create_json_from_entry( std::stringstream &ss, size_t depth, uint32_t data_size, MMDB_entry_data_list_s * &node, const bool in_array )
{
	// we're normally dealing with pairs (keys + values) so double the data_size
	if ( ! in_array )
	{
		data_size *= 2;
	}

	if (data_size == 0)
	{
		// nothing left to do
		return;
	}

	if (node == nullptr)
	{
		/// @throw std::runtime_error if the data_size is non-zero but the next pointer is null
		throw std::runtime_error( "invalid entry pointer or data size value" );
	}

	while (node && data_size)
	{
		const bool is_value		= in_array || (data_size % 2);
		const bool is_key		= in_array || (! is_value);

		data_size --;

		if (node->entry_data.has_data == false)
		{
			/// @throw std::runtime_error if a node contains no data
			throw std::runtime_error( "invalid entry has no data" );
		}

		if (is_key)
		{
			ss << std::string( depth * 4, ' ' );
		}

		switch (node->entry_data.type)
		{
			case MMDB_DATA_TYPE_UTF8_STRING:
			{
				ss << "\"" << std::string(node->entry_data.utf8_string, node->entry_data.data_size) << "\"";
				if ( ! is_value )
				{
					ss << " : ";
				}
				break;
			}
			case MMDB_DATA_TYPE_MAP:
			{
				ss << std::endl << std::string( depth * 4, ' ' ) << "{" << std::endl;
				create_json_from_entry( ss, depth + 1, node->entry_data.data_size, node->next );
				ss << std::endl << std::string( depth * 4, ' ' ) << "}";
				break;
			}
			case MMDB_DATA_TYPE_ARRAY:
			{
				ss << std::endl << std::string( depth * 4, ' ' ) << "[" << std::endl;
				create_json_from_entry( ss, depth + 1, node->entry_data.data_size, node->next, true );
				ss << std::endl << std::string( depth * 4, ' ' ) << "]";
				break;
			}
			case MMDB_DATA_TYPE_DOUBLE:		ss << node->entry_data.double_value;					break;
			case MMDB_DATA_TYPE_UINT16:		ss << node->entry_data.uint16;							break;
			case MMDB_DATA_TYPE_UINT32:		ss << node->entry_data.uint32;							break;
			case MMDB_DATA_TYPE_INT32:		ss << node->entry_data.uint32;							break;
			case MMDB_DATA_TYPE_UINT64:		ss << node->entry_data.uint64;							break;
			case MMDB_DATA_TYPE_FLOAT:		ss << node->entry_data.float_value;						break;
			case MMDB_DATA_TYPE_BOOLEAN:	ss << (node->entry_data.boolean ? "true" : "false");	break;
			case MMDB_DATA_TYPE_EXTENDED:
			case MMDB_DATA_TYPE_POINTER:
			case MMDB_DATA_TYPE_CONTAINER:
			case MMDB_DATA_TYPE_END_MARKER:
			{
				/// @throw std::runtime_error if the data type is invalid
				throw std::runtime_error( "entry data type #" + std::to_string(node->entry_data.type) + " indicates a damaged or corrupt database" );
			}
			case MMDB_DATA_TYPE_BYTES:
			case MMDB_DATA_TYPE_UINT128:
			default:
			{
				/// @throw std::runtime_error if the data type is not supported (raw bytes or uint128)
				throw std::runtime_error( "unhandled data type #" + std::to_string(node->entry_data.type) + " cannot be processed" );
			}
		}

		if (is_value && data_size)
		{
			ss << "," << std::endl;
		}

		node = node->next;
	}

	return;
}


std::string IPGeolocationDB::DB::lookup( const std::string &ip_address )
{
	MMDB_lookup_result_s result = lookup_raw( ip_address );

	MMDB_entry_data_list_s *node = nullptr;
	const int status = MMDB_get_entry_data_list( &result.entry, &node );
	if (status)
	{
		/// @throw std::system_error if the lookup resulted in a MMDB error
		const ErrorCategory	&	cat( get_error_category() );
		const std::error_code	ec( status, cat );
		const std::string		msg = "Failed to lookup up address \"" + ip_address + "\"";
		throw std::system_error( ec, msg );
	}

	return to_json( node );
}


std::string IPGeolocationDB::DB::to_json( MMDB_entry_data_list_s *node )
{
	#if 0
	// for debug purposes:
	MMDB_dump_entry_data_list( stdout, node, 0 );
	#endif

	std::stringstream ss;

	try
	{
		const size_t			depth	= 0;
		const size_t			nodes	= 1;
		MMDB_entry_data_list_s *next	= node;
		create_json_from_entry( ss, depth, nodes, next );
	}
	catch (...)
	{
		MMDB_free_entry_data_list( node );
		throw;
	}

	MMDB_free_entry_data_list( node );

	return ss.str();
}


IPGeolocationDB::MStr IPGeolocationDB::DB::get_all_fields( const std::string &ip_address, const std::string &language )
{
	MMDB_lookup_result_s result = lookup_raw( ip_address );

	MStr m;
	add_to_map( m, &result, "continent"				, language	, VCStr{ "continent"			, "names"			} );
	add_to_map( m, &result, "registered_country"	, language	, VCStr{ "registered_country"	, "names"			} );
	add_to_map( m, &result, "country"				, language	, VCStr{ "country"				, "names"			} );
	add_to_map( m, &result, "city"					, language	, VCStr{ "city"					, "names"			} );
	add_to_map( m, &result, "subdivision"			, language	, VCStr{ "subdivisions", "0"	, "names"			} );
	add_to_map( m, &result, "subdivision_iso_code"	, ""		, VCStr{ "subdivisions", "0"	, "iso_code"		} );
	add_to_map( m, &result, "country_iso_code"		, ""		, VCStr{ "country"				, "iso_code"		} );
	add_to_map( m, &result, "accuracy_radius"		, ""		, VCStr{ "location"				, "accuracy_radius"	} );
	add_to_map( m, &result, "latitude"				, ""		, VCStr{ "location"				, "latitude"		} );
	add_to_map( m, &result, "longitude"				, ""		, VCStr{ "location"				, "longitude"		} );
	add_to_map( m, &result, "time_zone"				, ""		, VCStr{ "location"				, "time_zone"		} );
	add_to_map( m, &result, "postal_code"			, ""		, VCStr{ "postal"				, "code"			} );

	m[ "query_ip_address"	] = ip_address;
	m[ "query_language"		] = language;

	return m;
}


std::string IPGeolocationDB::DB::get_field( const std::string &ip_address, const std::string &language, const VCStr &v )
{
	MMDB_lookup_result_s lookup = lookup_raw( ip_address );

	return get_field( &lookup, language, v );
}


std::string IPGeolocationDB::DB::get_field( MMDB_lookup_result_s *lookup, const std::string &language, const VCStr &v )
{
	std::string str;
	static const size_t kMaxIPGeolocationStringBytes = 4096;
	static const size_t kMaxLookupPathEntries = 8;

	if (lookup)
	{
		MMDB_entry_s *entry = &lookup->entry;

		const char* lookup_path[kMaxLookupPathEntries] = {};
		size_t lookup_path_size = 0;
		for (VCStr::const_iterator it = v.begin(); it != v.end() && lookup_path_size + 1 < kMaxLookupPathEntries; ++it)
			lookup_path[lookup_path_size++] = *it;

		if (!language.empty() && lookup_path_size + 1 < kMaxLookupPathEntries)
			lookup_path[lookup_path_size++] = language.c_str();

		if (lookup_path_size == 0 || lookup_path[lookup_path_size - 1] != nullptr)
			lookup_path[lookup_path_size++] = nullptr;

		MMDB_entry_data_s result;
		MMDB_aget_value( entry, &result, lookup_path );

		if (result.has_data)
		{
			switch (result.type)
			{
				case MMDB_DATA_TYPE_UTF8_STRING:
					if (result.data_size <= kMaxIPGeolocationStringBytes)
						str = std::string( result.utf8_string, result.data_size );
					break;
				case MMDB_DATA_TYPE_DOUBLE:			str = std::to_string( result.double_value				);	break;
				case MMDB_DATA_TYPE_UINT16:			str = std::to_string( result.uint16						);	break;
				case MMDB_DATA_TYPE_UINT32:			str = std::to_string( result.uint32						);	break;
				case MMDB_DATA_TYPE_INT32:			str = std::to_string( result.uint32						);	break;
				case MMDB_DATA_TYPE_UINT64:			str = std::to_string( result.uint64						);	break;
				case MMDB_DATA_TYPE_FLOAT:			str = std::to_string( result.float_value				);	break;
				case MMDB_DATA_TYPE_BOOLEAN:		str = (result.boolean ? "true" : "false"				);	break;
				default:	/* data type not supported for this "quick" retrieval */							break;
			}
		}
		else if (language == "en")
		{
			// ignore the language and grab the first one available
			str = get_field( lookup, "0", v );
		}
		else if (language != "0" && language != "en")
		{
			// ignore the language and grab the English name
			str = get_field( lookup, "0", v );
		}
	}

	return str;
}


void IPGeolocationDB::DB::add_to_map( IPGeolocationDB::MStr &m, MMDB_lookup_result_s *node, const std::string &name, const std::string &language, const VCStr &v )
{
	const std::string result = get_field( node, language, v );

	if (result.empty() == false)
	{
		// try and find a decent key name to use to store this value into the map
		std::string key = name;
		if (key.empty() || m.count(key) != 0)
		{
			// try a different name
			key = *v.begin();
		}
		if (key.empty() || m.count(key) != 0)
		{
			// try a different name
			key = *v.rbegin();
		}
		m[ key ] = result;
	}

	return;
}


CIPGeolocation::CIPGeolocation()
{
	m_bRunning = false;
	IPGeolocationLoaded = false;
	db = NULL;

	LoadFlags();
	if(thePrefs.GetIPGeolocationMode() != IPGEO_DISABLE || thePrefs.GetIPGeolocationShowFlag())
		LoadIPGeolocation();

	m_bRunning = true;
}

CIPGeolocation::~CIPGeolocation()
{
	UnloadIPGeolocation();
	UnloadFlags();
}

// Finds and returns the configured MMDB file, falling back to the largest MMDB file in the config directory.
const CString CIPGeolocation::GetFilePath()
{
	const CString strDefaultFilePath = GetDefaultIPGeolocationFilePath();
	CFileStatus status;
	if (CFile::GetStatus(PreparePathForWin32LongPath(strDefaultFilePath), status) && status.m_size > 0)
		return strDefaultFilePath;

	CString m_strPath;
	ULONGLONG m_ulSize = 0;
	CFileFind current;
	BOOL bFound = current.FindFile(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + _T("*.mmdb"));
	if (bFound) {
		do {
			bFound = current.FindNextFile();
			if (!current.IsDirectory() && current.GetLength() > 0 && (!m_ulSize || current.GetLength() > m_ulSize)) {
				m_strPath = current.GetFilePath();
				m_ulSize = current.GetLength();
			}
		} while (bFound);
	}

	return m_strPath;
}

void CIPGeolocation::LoadIPGeolocation(bool bAddToStatusBar)
{
	UnloadIPGeolocation();
	m_strIPGeolocationCityFile = GetFilePath();

	if (!m_strIPGeolocationCityFile.IsEmpty()) {
		try {
			db = new IPGeolocationDB::DB((std::string)CT2CA(m_strIPGeolocationCityFile.GetString()));
		} catch (const std::system_error& e) {
			CString errmsg(e.what());
			Log(_T("%i %s %s"), e.code().value(), errmsg, m_strIPGeolocationCityFile);
		} catch (...) {
				Log(GetResString(_T("IP_GEOLOCATION_LOAD_FAILED")), (LPCTSTR)m_strIPGeolocationCityFile);
		}

		if (db != NULL) {
			IPGeolocationLoaded = true;
			// Be defensive: metadata JSON may be missing/corrupted; do not throw here.
			CString m_strMsg1, m_strMsg2;
			try { // Parse metadata to log DB build time, but keep running on failure
				const std::string meta = db->get_metadata();
				nlohmann::json json_obj = nlohmann::json::parse(meta);
				auto it = json_obj.find("build_epoch");
				if (it != json_obj.end() && it->is_number_integer()) {
					time_t build_epoch = static_cast<time_t>(it->get<long long>());
					m_strMsg2.Format(GetResString(_T("IPGEOLOCATION_LOADED2")), CString(epoch2str(build_epoch).c_str()));
				}
			} catch (const std::exception& ex) {
				// Log but do not propagate; keep IP geolocation enabled.
				CString exMsg = UTF8To16(ex.what());
				AddDebugLogLine(false, _T("[IP Geolocation] Failed to parse metadata JSON: %s"), (LPCTSTR)EscPercent(exMsg));
			} catch (...) {
				// Unknown error; keep running without build time info.
				AddDebugLogLine(false, _T("[IP Geolocation] Unknown error while parsing metadata JSON"));
			}
			m_strMsg1.Format(GetResString(_T("IPGEOLOCATION_LOADED")), m_strIPGeolocationCityFile);
			CString strLogMessage(m_strMsg2.IsEmpty() ? m_strMsg1 : m_strMsg1 + _T(" ") + m_strMsg2);
			Log(bAddToStatusBar ? LOG_STATUSBAR : LOG_DEFAULT, _T("%s"), (LPCTSTR)strLogMessage);
		}
	}

	if(m_bRunning)
		Reset();
}

void CIPGeolocation::UnloadIPGeolocation()
{
	IPGeolocationLoaded = false;
	m_strIPGeolocationCityFile.Empty();
	if (db != NULL) {
		try {
			delete db;
		} catch (...) {
			// Be safe during reload and shutdown.
		}
		db = NULL;
	}
}

LPCTSTR CIPGeolocation::GetDefaultUpdateURLTemplate()
{
	return DFLT_GEOLOCATION_DB_URL_TEMPLATE;
}

CString CIPGeolocation::ExpandUpdateURLTemplate(const CString& strURLTemplate)
{
	CString strURL(strURLTemplate);
	SYSTEMTIME st = {};
	::GetSystemTime(&st);

	CString strYear;
	strYear.Format(_T("%04u"), st.wYear);
	CString strMonth;
	strMonth.Format(_T("%02u"), st.wMonth);
	strURL.Replace(_T("%Y"), strYear);
	strURL.Replace(_T("%m"), strMonth);
	return strURL;
}

bool CIPGeolocation::UpdateIPGeolocationFromURL(const CString& url, bool bInteractive)
{
	CString strURL(ExpandUpdateURLTemplate(url));
	strURL.Trim();
	return StartIPGeolocationDownload(strURL, bInteractive);
}

bool CIPGeolocation::IsIPGeolocationDownloadActive()
{
	return g_pIPGeolocationDownloadJob != NULL || g_bIPGeolocationDownloadOverlayDelayActive;
}

bool CIPGeolocation::GetIPGeolocationDownloadOverlayInfo(CString& strTitle, CString& strDetail, UINT& uDone, UINT& uTotal)
{
	if (g_pIPGeolocationDownloadJob == NULL && !g_bIPGeolocationDownloadOverlayDelayActive)
		return false;
	BuildIPGeolocationDownloadOverlayText(g_uIPGeolocationDownloadBytesRead, g_uIPGeolocationDownloadTotalBytes, g_bIPGeolocationDownloadHasTotalBytes, g_strIPGeolocationDownloadURL, strTitle, strDetail, uDone, uTotal);
	return true;
}

void CIPGeolocation::FinishIPGeolocationDownloadOverlayDelay()
{
	if (!g_bIPGeolocationDownloadOverlayDelayActive)
		return;

	HideIPGeolocationDownloadOverlays();
}

void CIPGeolocation::CancelIPGeolocationDownload()
{
	SIPGeolocationDownloadJob* pJob = g_pIPGeolocationDownloadJob;
	if (pJob == NULL) {
		FinishIPGeolocationDownloadOverlayDelay();
		return;
	}

	g_pIPGeolocationDownloadJob = NULL;
	pJob->hNotifyWnd = NULL;
	InterlockedExchange(&pJob->lCancel, 1);
	ReleaseIPGeolocationDownloadJob(pJob);
	HideIPGeolocationDownloadOverlays();
}

LRESULT CIPGeolocation::OnIPGeolocationDownloadProgress(LPARAM lParam)
{
	SIPGeolocationDownloadProgress* pProgress = reinterpret_cast<SIPGeolocationDownloadProgress*>(lParam);
	if (pProgress != NULL) {
		if (g_pIPGeolocationDownloadJob != NULL && pProgress->uToken == g_uIPGeolocationDownloadToken)
			UpdateIPGeolocationDownloadOverlays(pProgress->uBytesRead, pProgress->uTotalBytes, pProgress->bHasTotalBytes, pProgress->strURL);
		delete pProgress;
	}
	return 0;
}

LRESULT CIPGeolocation::OnIPGeolocationDownloadFinished(LPARAM lParam)
{
	SIPGeolocationDownloadResult* pResult = reinterpret_cast<SIPGeolocationDownloadResult*>(lParam);
	if (pResult == NULL)
		return 0;

	if (g_pIPGeolocationDownloadJob != NULL && pResult->uToken == g_uIPGeolocationDownloadToken) {
		SIPGeolocationDownloadJob* pJob = g_pIPGeolocationDownloadJob;
		pJob->hNotifyWnd = NULL;

		bool bUpdateSucceeded = false;
		if (pResult->bSucceeded) {
			UpdateIPGeolocationDownloadOverlays(pResult->uBytesRead, pResult->uTotalBytes, pResult->bHasTotalBytes, pResult->strURL);
			bUpdateSucceeded = ApplyDownloadedIPGeolocationFile(pResult->strURL, pResult->strTempFile, pResult->bInteractive);
		} else if (!pResult->bCanceled) {
			CString strError(GetResString(_T("IPGEOLOCATION_DOWNLOAD_FAILED")));
			if (!pResult->strError.IsEmpty())
				strError.AppendFormat(_T("\r\n\r\n%s"), (LPCTSTR)pResult->strError);
			ReportIPGeolocationUpdateError(strError, pResult->bInteractive);
		}

		if (!bUpdateSucceeded && !pResult->strTempFile.IsEmpty())
			(void)::DeleteFile(PreparePathForWin32LongPath(pResult->strTempFile));
		g_pIPGeolocationDownloadJob = NULL;
		if (bUpdateSucceeded)
			StartIPGeolocationDownloadOverlayDelay();
		else
			HideIPGeolocationDownloadOverlays();
		ReleaseIPGeolocationDownloadJob(pJob);
	} else if (!pResult->strTempFile.IsEmpty())
		(void)::DeleteFile(PreparePathForWin32LongPath(pResult->strTempFile));

	delete pResult;
	return 0;
}

void CIPGeolocation::Reset()
{
	theApp.serverlist->ResetIPGeolocation();
	theApp.clientlist->ResetIPGeolocation();
	theApp.emuledlg->kademliawnd->ResetIPGeolocation();
}

void CIPGeolocation::Redraw()
{
	theApp.emuledlg->serverwnd->Invalidate(FALSE);
	theApp.emuledlg->kademliawnd->Invalidate(FALSE);
	theApp.emuledlg->transferwnd->GetDownloadList()->Invalidate(FALSE);
	theApp.emuledlg->transferwnd->GetClientList()->Invalidate(FALSE);
	theApp.emuledlg->transferwnd->GetDownloadClientsList()->Invalidate(FALSE);
	theApp.emuledlg->transferwnd->GetUploadList()->Invalidate(FALSE);
	theApp.emuledlg->transferwnd->GetQueueList()->Invalidate(FALSE);
}

void CIPGeolocation::LoadFlags() {
	FlagImageList.DeleteImageList();
	FlagImageList.Create(FLAG_WIDTH, FLAG_HEIGHT, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	FlagImageList.SetBkColor(CLR_NONE);

	HICON iconHandle = NULL;
	int iconIndex = -1;
	for (int i = 0; i < CountryCodeFlagArraySize; i++) {
		iconHandle = (HICON)::LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(CountryCodeFlagArray[i].uResourceID), IMAGE_ICON, FLAG_WIDTH, FLAG_HEIGHT, LR_DEFAULTCOLOR);
		if(iconHandle) {
			iconIndex = FlagImageList.Add(iconHandle);
			if(iconIndex != -1)
				CountryCodeFlagMap.SetAt(CountryCodeFlagArray[i].strCountryCode, (uint16)iconIndex);
			::DestroyIcon(iconHandle);
		} else
			AddDebugLogLine(LOG_WARNING, _T("[IP Geolocation] Invalid flag resource ID"), CountryCodeFlagArray[i].uResourceID);
	}

	AddLogLine(false, GetResString(_T("IPGEOLOCATION_FLAGLOAD")));
}

void CIPGeolocation::UnloadFlags() {
	//Clean out the map table
	CountryCodeFlagMap.RemoveAll();

	try {
		//destory all image
		if (FlagImageList && FlagImageList.m_hImageList) {
			FlagImageList.DeleteImageList();
			Log(GetResString(_T("IPGEOLOCATION_FLAGUNLD")));
		}
	} catch (...) {
		if (thePrefs.GetVerbose())
			AddDebugLogLine(false, _T("[IP Geolocation] CountryFlagImageList failed."));
		ASSERT(0);
	}
}

bool CIPGeolocation::EnsureFlagsLoaded()
{
	if (FlagImageList.m_hImageList == NULL)
		LoadFlags();
	return FlagImageList.m_hImageList != NULL;
}

int CIPGeolocation::GetFlagIndexByCountryCode(const CString& strCountryCode) const
{
	CString strCode(strCountryCode);
	strCode.Trim();
	strCode.MakeUpper();
	uint16 uIndex = NO_FLAG;
	if (CountryCodeFlagMap.Lookup(strCode, uIndex))
		return static_cast<int>(uIndex);
	return -1;
}

CString CIPGeolocation::GetLocalizedCountryName(const CString& strCountryCode, const CString& strFallbackName)
{
	CString strCode(strCountryCode);
	strCode.Trim();
	strCode.MakeUpper();

	CString strFallback(strFallbackName);
	strFallback.Trim();
	if (strCode.GetLength() != 2)
		return strFallback.IsEmpty() ? strCode : strFallback;

	CString strKey;
	strKey.Format(_T("IPGEO_COUNTRY_%s"), (LPCTSTR)strCode);
	CString strName(GetResString(strKey));
	strName.Trim();
	if (!strName.IsEmpty() && strName.Compare(strKey) != 0)
		return strName;
	return strFallback.IsEmpty() ? strCode : strFallback;
}

CString CIPGeolocation::FormatLocalizedCountryNameAndCode(const CString& strCountryCode, const CString& strFallbackName)
{
	CString strCode(strCountryCode);
	strCode.Trim();
	strCode.MakeUpper();
	const CString strName(GetLocalizedCountryName(strCode, strFallbackName));
	if (strCode.GetLength() != 2 || strName.IsEmpty() || strName.CompareNoCase(strCode) == 0)
		return strName.IsEmpty() ? strCode : strName;

	CString strText;
	strText.Format(_T("%s (%s)"), (LPCTSTR)strName, (LPCTSTR)strCode);
	return strText;
}

const struct GeolocationData_Struct CIPGeolocation::QueryGeolocationData(const CAddress& IP)
{
	GeolocationData_Struct ppFound;
	// Set default values first.
	ppFound.Country = GetResString(_T("IPGEOLOCATION_NA"));
	ppFound.CountryCode = GetResString(_T("IPGEOLOCATION_NA"));
	ppFound.City = GetResString(_T("IPGEOLOCATION_NA"));
	ppFound.FlagIndex = NO_FLAG;

	if (IPGeolocationLoaded == false || !IP.IsPublicIP())
		return ppFound;

	try {
		IPGeolocationDB::MStr m = db->get_all_fields(IP.ToString(), "en");
		for (const auto iter : m) {
			if (iter.first == "country")
				ppFound.Country = UTF8To16(iter.second.c_str());
			else if (iter.first == "country_iso_code") {
				ppFound.CountryCode = UTF8To16(iter.second.c_str());
				uint16 m_uTemp = -1;
				if (CountryCodeFlagMap.Lookup(ppFound.CountryCode, m_uTemp) && m_uTemp >= 0)
					ppFound.FlagIndex = m_uTemp;
			} else if (iter.first == "city")
				ppFound.City = UTF8To16(iter.second.c_str());
		}
	} catch (const std::length_error&) {
		// entry not found; keep defaults
	} catch (const std::exception& ex) {
		if (thePrefs.GetVerbose()) {
			CString exMsg = UTF8To16(ex.what());
			AddDebugLogLine(false, _T("[IP Geolocation] std::exception while querying IP geolocation: %s"), (LPCTSTR)EscPercent(exMsg));
		}
		ASSERT(0);
	} catch (...) {
		if (thePrefs.GetVerbose())
			AddDebugLogLine(false, _T("[IP Geolocation] Unknown exception while querying IP geolocation"));
		ASSERT(0);
	}

	ppFound.Country = GetLocalizedCountryName(ppFound.CountryCode, ppFound.Country);
	return ppFound;
}

const CString CIPGeolocation::GetGeolocationData(const GeolocationData_Struct m_structGeolocationData, bool bForceCountryCity) const
{
	if(IPGeolocationLoaded) {
		if(bForceCountryCity)
			return m_structGeolocationData.Country;
		switch(thePrefs.GetIPGeolocationMode()) {
			case IPGEO_COUNTRYCODE:
				return m_structGeolocationData.CountryCode;
			case IPGEO_COUNTRY:
				return m_structGeolocationData.Country;
			case IPGEO_COUNTRYCITY:
			{
				if (m_structGeolocationData.Country == GetResString(_T("IPGEOLOCATION_NA"))) // Show "N/A" instead of "N/A, N/A"
					return m_structGeolocationData.Country;
				CString m_strTemp;
				m_strTemp.Format(_T("%s, %s"), m_structGeolocationData.Country, m_structGeolocationData.City);
				return m_strTemp;
			}
		}
	} else if(bForceCountryCity)
		return GetResString(_T("DISABLED"));	

	return EMPTY;
}

const bool CIPGeolocation::ShowCountryFlag() {
	return (thePrefs.GetIPGeolocationShowFlag() && IPGeolocationLoaded); // User enabled flags and an IP geolocation database file has been loaded
}

IMAGELISTDRAWPARAMS CIPGeolocation::GetFlagImageDrawParams(CDC* dc,int iIndex, POINT point) const
{
	IMAGELISTDRAWPARAMS imldp;
	imldp.cbSize   = sizeof(IMAGELISTDRAWPARAMS);
	imldp.himl     = FlagImageList.m_hImageList;
	imldp.i        = iIndex;
	imldp.hdcDst   = dc->m_hDC;
	imldp.x        = point.x;
	imldp.y        = point.y;
	imldp.cx       = FLAG_WIDTH;
	imldp.cy       = FLAG_HEIGHT;
	imldp.xBitmap  = 0;
	imldp.yBitmap  = 0;
	imldp.rgbBk    = CLR_DEFAULT;
	imldp.rgbFg    = CLR_DEFAULT;
	imldp.fStyle   = ILD_SCALE;
	imldp.dwRop    = SRCCOPY;
#if (_WIN32_WINNT >= 0x501)
	imldp.fState   = ILS_NORMAL;
	imldp.Frame    = 0;
	imldp.crEffect = CLR_DEFAULT;
#endif

	return imldp;
}
