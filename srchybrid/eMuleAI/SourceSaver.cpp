//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#include "StdAfx.h"
#include "sourcesaver.h"
#include "PartFile.h"
#include "emule.h"
#include "emuledlg.h"
#include "OtherFunctions.h"
#include "DownloadQueue.h"
#include "updownclient.h"
#include "Preferences.h"
#include "Log.h"
#include "PartFileWriteThread.h"
#include "ListenSocket.h"

#define RELOADTIME	3600000 //60 minutes	
#define RESAVETIME	 600000 //10 minutes

namespace
{
	const DWORD kSourceSaverStartupGraceMs = 15000;
	const DWORD kSourceSaverUiThrottleMs = 750;
	const DWORD kSourceSaverBusyUiDeferMs = 250;

	bool CanRunSourceSaverDiskIoNow(DWORD dwNow)
	{
		if (!theApp.IsUiThread() || theApp.IsClosing())
			return true;

		static DWORD s_dwStartupMetadataReadyTick = 0;
		static DWORD s_dwNextAllowedTick = 0;
		if (s_dwStartupMetadataReadyTick == 0)
			s_dwStartupMetadataReadyTick = dwNow;
		if (static_cast<int>(dwNow - s_dwStartupMetadataReadyTick) < static_cast<int>(kSourceSaverStartupGraceMs))
			return false;
		if (static_cast<int>(dwNow - s_dwNextAllowedTick) < 0)
			return false;

		const UINT uQueueStatus = HIWORD(::GetQueueStatus(QS_KEY | QS_MOUSE | QS_PAINT));
		if ((uQueueStatus & (QS_KEY | QS_MOUSE | QS_PAINT)) != 0) {
			s_dwNextAllowedTick = dwNow + kSourceSaverBusyUiDeferMs;
			return false;
		}

		s_dwNextAllowedTick = dwNow + kSourceSaverUiThrottleMs;
		return true;
	}
}

CSourceSaver::CSourceSaver(void)
{
	m_dwLastTimeLoaded = ::GetTickCount() - RELOADTIME;
	m_dwLastTimeSaved = ::GetTickCount() + (rand() * 30000 / RAND_MAX) - 15000 - RESAVETIME;
}

CSourceSaver::CSourceData::CSourceData(CUpDownClient* client, const CString& exp)
{
	nSrcExchangeVer = client->GetSourceExchange1Version(); // khaos::kmod+ Modified to Save Source Exchange Version
	sourceIP = client->GetIP();
	if (sourceIP.GetType() == CAddress::IPv4 && nSrcExchangeVer > 2 && IsLowID(client->GetUserIDHybrid())) // nSrcExchangeVer should be 3 or 4 except very old clients
		sourceID = client->GetUserIDHybrid();
	else 
		sourceID = 0; // IPv4 or IPv6 is saved and loaded instead of LowID for this case
	sourcePort = client->GetUserPort();
	serverip = client->GetServerIP();
	serverport = client->GetServerPort();
	partsavailable = client->GetAvailablePartCount();
	expiration = exp;
	bWasDownloading = client->GetDownloadState() == DS_DOWNLOADING;
	bWasOnQueue = client->GetDownloadState() == DS_ONQUEUE;
}

CSourceSaver::~CSourceSaver(void)
{
	ClearSources();
}

void CSourceSaver::ClearSources()
{
	while (!sources.IsEmpty())
		delete sources.RemoveHead();
}

CString CSourceSaver::GetSourcesFilePath(const CPartFile* file)
{
	if (file == NULL)
		return CString();

	CString strSourcesFilePath(file->GetTmpPath());
	if (strSourcesFilePath.IsEmpty() || file->GetPartMetFileName().IsEmpty())
		return CString();

	MakeFoldername(strSourcesFilePath);
	strSourcesFilePath += file->GetPartMetFileName();
	strSourcesFilePath += _T(".txtsrc");
	return strSourcesFilePath;
}

bool CSourceSaver::Process(CPartFile* file) // return false if sources not saved
{
	if (!theApp.AllStartupMetadataReady())
		return false;

	const DWORD dwNow = ::GetTickCount();
	if ((int)(dwNow - m_dwLastTimeSaved) > RESAVETIME) {
		if (!CanRunSourceSaverDiskIoNow(dwNow))
			return false;
		if (GetSourcesFilePath(file).IsEmpty())
			return false;
		m_dwLastTimeSaved = ::GetTickCount() + (rand() * 30000 / RAND_MAX) - 15000;
		ClearSources();
		LoadSourcesFromFile(file);
		SaveSources(file, false);
		
		if ((int)(::GetTickCount() - m_dwLastTimeLoaded) > RELOADTIME) {
			m_dwLastTimeLoaded = ::GetTickCount() + (rand() * 30000 / RAND_MAX) - 15000;
			AddSourcesToDownload(file);
		}

		ClearSources();
		
		return true;
	}
	return false;
}

void CSourceSaver::DeleteFile(CPartFile* file)
{
	ClearSources();
	const CString strSourcesFilePath(GetSourcesFilePath(file));
	if (strSourcesFilePath.IsEmpty())
		return;
	if (!DeleteFileLongPath(strSourcesFilePath)) {
		const DWORD dwError = ::GetLastError();
		if (dwError != ERROR_FILE_NOT_FOUND && dwError != ERROR_PATH_NOT_FOUND)
			AddLogLine(true, GetResString(_T("FAILED_TO_DELETE_FILE_MANUALLY")), (LPCTSTR)strSourcesFilePath);
	}
}

void CSourceSaver::LoadSourcesFromFile(CPartFile* file)
{
	const CString strSourcesFilePath(GetSourcesFilePath(file));
	if (strSourcesFilePath.IsEmpty())
		return;

	CString strLine;
	CStdioFile f;
	if (!f.Open(PreparePathForWin32LongPath(strSourcesFilePath), CFile::modeRead | CFile::typeText))
		return;
	while(f.ReadString(strLine)) {
		if (strLine.IsEmpty() || strLine.GetAt(0) == '#')
			continue;
		int pos = strLine.Find(':');
		if (pos == -1)
			continue;
		CString strIP = strLine.Left(pos);
		strLine = strLine.Mid(pos+1);
		uint32 dwID = 0; // Only LowID's are set to nonzero integers. IPv4 or IPv6 is saved and loaded instead of LowID for this case
		if (strIP.Find('.') == -1 && strIP.Find(':') == -1) { // This is a LowID, not IP address. So we'll set its value.
			dwID = _ttoi(strIP);
			if (dwID == INADDR_NONE)
				continue;
		}
		pos = strLine.Find(',');
		if (pos == -1)
			continue;
		CString strPort = strLine.Left(pos);
		strLine = strLine.Mid(pos+1);
		uint16 wPort = (uint16)_tstoi(strPort);
		if (!wPort)
			continue;
		pos = strLine.Find(',');
		if (pos == -1)
			continue;
		CString strExpiration = strLine.Left(pos);
		if (IsExpired(strExpiration))
			continue;
		strLine = strLine.Mid(pos+1);
		pos = strLine.Find(',');
		if (pos == -1)
			continue;
		uint8 nSrcExchangeVer = (uint8)_tstoi(strLine.Left(pos));
		strLine = strLine.Mid(pos+1);
		pos = strLine.Find(':');
		if (pos == -1)
			continue;
		CString strserverip = strLine.Left(pos);
		strLine = strLine.Mid(pos+1);
		uint32 dwserverip = inet_addr(CT2CA(strserverip));
		if (dwserverip == INADDR_NONE) 
			continue;
		pos = strLine.Find(';');
		if (pos == -1 || strLine.GetLength() < 2)
			continue;
		CString strserverport = strLine.Left(pos);
		uint16 wserverport = (uint16)_tstoi(strserverport);
		if (!wserverport)
			continue;
		CSourceData* newsource = new CSourceData(CAddress(strIP, true), dwID, wPort, dwserverip, wserverport, strExpiration, nSrcExchangeVer);
		sources.AddTail(newsource);
	}
    f.Close();
}

void CSourceSaver::AddSourcesToDownload(CPartFile* file)
{
	(void)AddSourcesToDownload(file, sources);
}

UINT CSourceSaver::AddSourcesToDownload(CPartFile* file, Sources& sourceList)
{
	if (file == NULL || theApp.downloadqueue == NULL)
		return 0;

	const UINT uSourceCountBefore = file->GetSourceCount();
	UINT uQueueStatesRestored = 0;
	UINT uImmediateRestartCandidates = 0;
	for (POSITION pos = sourceList.GetHeadPosition(); pos != NULL;) {
		if (file->GetMaxSources() <= file->GetSourceCount())
			break;

		CSourceData* cur_src = sourceList.GetNext(pos);
		if (cur_src == NULL)
			continue;

		CUpDownClient* newclient;
		if (!cur_src->sourceID || cur_src->nSrcExchangeVer >= 3) // Only LowID's are set to nonzero integers. IPv4 or IPv6 is saved and loaded instead of LowID for this case. nSrcExchangeVer should be >= 3 except very old clients.
			newclient = new CUpDownClient(file, cur_src->sourcePort, cur_src->sourceID ? cur_src->sourceID : cur_src->sourceIP.ToUInt32(true), cur_src->serverip, cur_src->serverport, false, cur_src->sourceIP);
		else
			newclient = new CUpDownClient(file, cur_src->sourcePort, cur_src->sourceID, cur_src->serverip, cur_src->serverport, true);

		newclient->SetSourceFrom(SF_SLS);
		CUpDownClient* pResolvedSource = NULL;
		theApp.downloadqueue->CheckAndAddSource(file, newclient, SF_SLS, true, &pResolvedSource);
		if (pResolvedSource == NULL || pResolvedSource->GetRequestFile() != file)
			continue;

		if (cur_src->bWasDownloading) {
			const EDownloadState eDownloadState = pResolvedSource->GetDownloadState();
			const bool bCanRestartConnectedSource = eDownloadState == DS_ONQUEUE || eDownloadState == DS_CONNECTED || eDownloadState == DS_NONE;
			if (bCanRestartConnectedSource && pResolvedSource->GetUploadState() != US_BANNED && pResolvedSource->socket != NULL && pResolvedSource->socket->IsConnected() && pResolvedSource->CheckHandshakeFinished()) {
				if (eDownloadState == DS_NONE)
					pResolvedSource->SetDownloadState(DS_ONQUEUE, _T("SourceSave: restore active source before resume"));
				pResolvedSource->SetSentCancelTransfer(true);
				++uImmediateRestartCandidates;
			}
		} else if (cur_src->bWasOnQueue && pResolvedSource->GetDownloadState() == DS_NONE) {
			pResolvedSource->SetDownloadState(DS_ONQUEUE, _T("SourceSave: restore remote queue state"));
			++uQueueStatesRestored;
		}
	}
	if ((uQueueStatesRestored != 0 || uImmediateRestartCandidates != 0) && thePrefs.GetDebugSourceExchange())
		AddDebugLogLine(DLP_LOW, false, _T("[SourceSave] Resume runtime state restored. Queued=%u Immediate=%u File=\"%s\""), uQueueStatesRestored, uImmediateRestartCandidates, (LPCTSTR)EscPercent(file->GetFileName()));
	const UINT uSourceCountAfter = file->GetSourceCount();
	return uSourceCountAfter > uSourceCountBefore ? uSourceCountAfter - uSourceCountBefore : 0;
}

UINT CSourceSaver::SaveSourcesOnStop(CPartFile* file)
{
	ClearSources();
	if (file == NULL || !thePrefs.GetSaveLoadSources())
		return 0;

	const int iMaxSources = thePrefs.GetSaveLoadSourcesMaxSources();
	if (iMaxSources > 0) {
		const CString strExpiration(CalcExpiration(thePrefs.GetSaveLoadSourcesExpirationDays()));
		for (int iPriorityPass = 0; iPriorityPass < 3; ++iPriorityPass) {
			for (POSITION pos = file->srclist.GetHeadPosition(); pos != NULL;) {
				if (static_cast<UINT>(sources.GetCount()) >= static_cast<UINT>(iMaxSources))
					break;

				CUpDownClient* cur_src = file->srclist.GetNext(pos);
				if (cur_src == NULL || !cur_src->IsValidSource())
					continue;
				const EDownloadState eDownloadState = cur_src->GetDownloadState();
				const bool bMatchesPriorityPass = (iPriorityPass == 0 && eDownloadState == DS_DOWNLOADING)
					|| (iPriorityPass == 1 && eDownloadState == DS_ONQUEUE)
					|| (iPriorityPass == 2 && eDownloadState != DS_DOWNLOADING && eDownloadState != DS_ONQUEUE);
				if (!bMatchesPriorityPass)
					continue;
				if (cur_src->RequiresCryptLayer() || thePrefs.IsCryptLayerRequired())
					continue;
				sources.AddTail(new CSourceData(cur_src, strExpiration));
			}
		}
	}

	SaveSources(file, true);
	return static_cast<UINT>(sources.GetCount());
}

UINT CSourceSaver::LoadSourcesOnResume(CPartFile* file)
{
	if (file == NULL || !thePrefs.GetSaveLoadSources())
		return 0;

	if (sources.IsEmpty())
		LoadSourcesFromFile(file);
	const UINT uAddedSources = AddSourcesToDownload(file, sources);
	ClearSources();
	return uAddedSources;
}

SaveSourcesData* CSourceSaver::BuildSaveSourcesSnapshot(CPartFile* file, bool bForce, bool bMarkInQueue)
{
	if (file == NULL)
		return NULL;

	CSingleLock sSaveSourcesLock(&file->m_SaveSourcesLock, FALSE);
	if (sSaveSourcesLock.IsLocked()) // Source are being saved inside the thread. Don't make GUI thread to wait it, return here and try next time.
		return NULL;
	sSaveSourcesLock.Lock(); // Lock is free, lets lock it and do the job.

	CSourceData* sourcedata;
	int m_iSaveLoadSourcesMaxSources = thePrefs.GetSaveLoadSourcesMaxSources();
	int m_iSaveLoadSourcesExpirationDays = thePrefs.GetSaveLoadSourcesExpirationDays();

	ASSERT2(file->srcstosave.IsEmpty());

	POSITION pos2, pos;
	CUpDownClient* cur_src;
	// Choose best sources for the file
	for (pos = file->srclist.GetHeadPosition(); pos != 0;) {
		cur_src = file->srclist.GetNext(pos);

		if (!cur_src->IsValidSource())
			continue;

		if (file->srcstosave.IsEmpty()) {
			sourcedata = new CSourceData(cur_src, CalcExpiration(m_iSaveLoadSourcesExpirationDays));
			file->srcstosave.AddHead(sourcedata);
			continue;
		}

		// Skip also Required Obfuscation, because we don't save the userhash (and we don't know if all settings are still valid on next restart)
		if (cur_src->RequiresCryptLayer() || thePrefs.IsCryptLayerRequired())
			continue;

		if ((UINT)file->srcstosave.GetCount() < m_iSaveLoadSourcesMaxSources || (cur_src->GetAvailablePartCount() > file->srcstosave.GetTail()->partsavailable) || (cur_src->GetSourceExchange1Version() > file->srcstosave.GetTail()->nSrcExchangeVer)) {
			if ((UINT)file->srcstosave.GetCount() == m_iSaveLoadSourcesMaxSources)
				delete file->srcstosave.RemoveTail();
			ASSERT((UINT)file->srcstosave.GetCount() < m_iSaveLoadSourcesMaxSources);
			bool bInserted = false;
			for (pos2 = file->srcstosave.GetTailPosition(); pos2 != 0; file->srcstosave.GetPrev(pos2)) {
				CSourceData* cur_srctosave = file->srcstosave.GetAt(pos2);
				if (file->GetAvailableSrcCount() > (m_iSaveLoadSourcesMaxSources * 2) && cur_srctosave->nSrcExchangeVer > cur_src->GetSourceExchange1Version())
					bInserted = true;
				else if (file->GetAvailableSrcCount() > (m_iSaveLoadSourcesMaxSources * 2) && cur_srctosave->nSrcExchangeVer == cur_src->GetSourceExchange1Version() && cur_srctosave->partsavailable > cur_src->GetAvailablePartCount())
					bInserted = true;
				else if (file->GetAvailableSrcCount() <= (m_iSaveLoadSourcesMaxSources * 2) && cur_srctosave->partsavailable > cur_src->GetAvailablePartCount())
					bInserted = true;

				if (bInserted) {
					sourcedata = new CSourceData(cur_src, CalcExpiration(m_iSaveLoadSourcesExpirationDays));
					file->srcstosave.InsertAfter(pos2, sourcedata);
					break;
				}
			}

			if (!bInserted) {
				sourcedata = new CSourceData(cur_src, CalcExpiration(m_iSaveLoadSourcesExpirationDays));
				file->srcstosave.AddHead(sourcedata);
			}
		}
	}

	// Add previously saved sources if found sources does not reach the limit
	for (pos = sources.GetHeadPosition(); pos; sources.GetNext(pos)) {
		CSourceData* cur_sourcedata = sources.GetAt(pos);

		if ((UINT)file->srcstosave.GetCount() == m_iSaveLoadSourcesMaxSources)
			break;
		ASSERT((UINT)file->srcstosave.GetCount() <= m_iSaveLoadSourcesMaxSources);

		bool bFound = false;
		for (pos2 = file->srcstosave.GetHeadPosition(); pos2; file->srcstosave.GetNext(pos2)) {
			if (file->srcstosave.GetAt(pos2)->Compare(cur_sourcedata)) {
				bFound = true;
				break;
			}
		}

		if (!bFound)
			file->srcstosave.AddTail(new CSourceData(cur_sourcedata));

	}
	const CString strSourcesFilePath(GetSourcesFilePath(file));
	const bool bQueueSaveSources = !strSourcesFilePath.IsEmpty() && (bForce || thePrefs.GetCommitFiles() >= 2 || (thePrefs.GetCommitFiles() >= 1 && theApp.IsClosing())) && (!bMarkInQueue || !file->m_bSaveSourcesInQueue);
	SaveSourcesData* pSaveSourcesData = NULL;
	if (bQueueSaveSources) {
		pSaveSourcesData = new SaveSourcesData;
		pSaveSourcesData->lGeneration = file->NextSaveSourcesGeneration();
		pSaveSourcesData->strSourcesFilePath = strSourcesFilePath;
		pSaveSourcesData->strED2kLink = file->GetED2kLink();
		pSaveSourcesData->rows.reserve(static_cast<size_t>(file->srcstosave.GetCount()));
		if (bMarkInQueue)
			file->m_bSaveSourcesInQueue = true;
	}
	while (!file->srcstosave.IsEmpty()) {
		CSourceData* cur_src = file->srcstosave.RemoveHead();
		if (pSaveSourcesData != NULL) {
			SSaveSourceSnapshotRow row;
			row.sourceIP = cur_src->sourceIP;
			row.sourceID = cur_src->sourceID;
			row.sourcePort = cur_src->sourcePort;
			row.serverip = cur_src->serverip;
			row.serverport = cur_src->serverport;
			row.expiration = cur_src->expiration;
			row.nSrcExchangeVer = cur_src->nSrcExchangeVer;
			pSaveSourcesData->rows.push_back(row);
		}
		delete cur_src;
	}
	sSaveSourcesLock.Unlock();
	return pSaveSourcesData;
}

void CSourceSaver::SaveSources(CPartFile* file, bool bForce)
{
	SaveSourcesData* pSaveSourcesData = BuildSaveSourcesSnapshot(file, bForce, true);
	if (pSaveSourcesData == NULL)
		return;

	// We'll submit a command to the thread only if there's no save sources met command in queue
	CPartFileWriteThread* pThread = theApp.m_pPartFileWriteThread;
	if (pThread && pThread->IsRunning()) {
		CSingleLock sFlushListLock(&pThread->m_lockFlushList, TRUE);
		pThread->m_FlushList.AddTail(ToWrite{ file, NULL, NULL, pSaveSourcesData, NULL, NULL, NULL });
		pSaveSourcesData = NULL;

		if (!pThread->m_FlushList.IsEmpty()) //let it sleep if nothing to do
			pThread->WakeUpCall();
	}
	if (pSaveSourcesData != NULL) {
		const CString strTmpSourcesFilePath(pSaveSourcesData->strSourcesFilePath + _T(".tmp"));
		AddDebugLogLine(DLP_HIGH, false, _T("[AsyncDiskWrite] name=\"SLS\" generation=%ld result=failed reason=thread-unavailable error=%lu shutdownFallback=%u temp=\"%s\" final=\"%s\"\n"),
			pSaveSourcesData->lGeneration, static_cast<DWORD>(ERROR_SERVICE_NOT_ACTIVE), theApp.IsClosing() ? 1U : 0U, (LPCTSTR)strTmpSourcesFilePath, (LPCTSTR)pSaveSourcesData->strSourcesFilePath);
		theApp.QueueAsyncDiskWriteResultEvent(_T("SLS"), pSaveSourcesData->lGeneration, _T("failed"), _T("thread-unavailable"), strTmpSourcesFilePath, pSaveSourcesData->strSourcesFilePath, theApp.IsClosing(), ERROR_SERVICE_NOT_ACTIVE);
		if (file != NULL) {
			CSingleLock sResetSaveSourcesLock(&file->m_SaveSourcesLock, TRUE);
			file->m_bSaveSourcesInQueue = false;
		}
	}
	delete pSaveSourcesData;
}

bool CSourceSaver::WriteSourcesSnapshotNow(const SaveSourcesData& data, bool bShutdownFallback)
{
	const CString strTmpSourcesFilePath(data.strSourcesFilePath + _T(".tmp"));
	if (data.strSourcesFilePath.IsEmpty()) {
		theApp.QueueAsyncDiskWriteResultEvent(_T("SLS"), data.lGeneration, _T("failed"), _T("invalid-path"), strTmpSourcesFilePath, data.strSourcesFilePath, bShutdownFallback, ERROR_INVALID_PARAMETER);
		return false;
	}

	CString strLine;
	CStdioFile f;
	CFileException fex;
	if (!f.Open(PreparePathForWin32LongPath(strTmpSourcesFilePath), CFile::modeCreate | CFile::modeWrite | CFile::typeText, &fex)) {
		theApp.QueueAsyncDiskWriteResultEvent(_T("SLS"), data.lGeneration, _T("failed"), _T("open-temp"), strTmpSourcesFilePath, data.strSourcesFilePath, bShutdownFallback, static_cast<DWORD>(fex.m_lOsError));
		return false;
	}

	try {
		f.WriteString(_T("#format: SourceIP/LowID:SourcePort,ExpirationDate(yymmddhhmm),SourceExchangeVersion,ServerIP,ServerPort;\r\n"));
		f.WriteString(_T("#") + data.strED2kLink + _T("\r\n"));

		for (std::vector<SSaveSourceSnapshotRow>::const_iterator it = data.rows.begin(); it != data.rows.end(); ++it) {
			if (it->sourceID)
				strLine.Format(_T("%i:%i,%s,%i,%s:%i;\r\n"), it->sourceID, it->sourcePort, it->expiration, it->nSrcExchangeVer, ipstr(it->serverip), it->serverport);
			else
				strLine.Format(_T("%s:%i,%s,%i,%s:%i;\r\n"), it->sourceIP.ToStringC(), it->sourcePort, it->expiration, it->nSrcExchangeVer, ipstr(it->serverip), it->serverport);
			f.WriteString(strLine);
		}

		f.Close();
	} catch (CFileException *ex) {
		const DWORD dwWriteError = static_cast<DWORD>(ex->m_lOsError);
		ex->Delete();
		f.Abort();
		(void)DeleteFileLongPath(strTmpSourcesFilePath);
		theApp.QueueAsyncDiskWriteResultEvent(_T("SLS"), data.lGeneration, _T("failed"), _T("write-temp"), strTmpSourcesFilePath, data.strSourcesFilePath, bShutdownFallback, dwWriteError);
		return false;
	} catch (...) {
		f.Abort();
		(void)DeleteFileLongPath(strTmpSourcesFilePath);
		theApp.QueueAsyncDiskWriteResultEvent(_T("SLS"), data.lGeneration, _T("failed"), _T("write-temp"), strTmpSourcesFilePath, data.strSourcesFilePath, bShutdownFallback, ERROR_WRITE_FAULT);
		return false;
	}

	if (!MoveFileExLongPath(strTmpSourcesFilePath, data.strSourcesFilePath, MOVEFILE_REPLACE_EXISTING)) {
		const DWORD dwPublishError = ::GetLastError();
		(void)DeleteFileLongPath(strTmpSourcesFilePath);
		theApp.QueueAsyncDiskWriteResultEvent(_T("SLS"), data.lGeneration, _T("failed"), _T("publish-final"), strTmpSourcesFilePath, data.strSourcesFilePath, bShutdownFallback, dwPublishError);
		return false;
	}
	if (!bShutdownFallback)
		theApp.QueueAsyncDiskWriteResultEvent(_T("SLS"), data.lGeneration, _T("success"), _T("published"), strTmpSourcesFilePath, data.strSourcesFilePath, bShutdownFallback);
	return true;
}

CString CSourceSaver::CalcExpiration(int Days)
{
	CTime expiration = CTime::GetCurrentTime();
	CTimeSpan timediff(Days, 0, 0, 0);
	expiration += timediff;
	CString strExpiration;
	strExpiration.Format(_T("%02i%02i%02i%02i%02i"), (expiration.GetYear() % 100), expiration.GetMonth(), expiration.GetDay(), expiration.GetHour(), expiration.GetMinute());
	return strExpiration;
}

bool CSourceSaver::IsExpired(CString expirationdate)
{
	int year = _tstoi(expirationdate.Mid(0, 2)) + 2000;
	int month = _tstoi(expirationdate.Mid(2, 2));
	int day = _tstoi(expirationdate.Mid(4, 2));
	int hour = _tstoi(expirationdate.Mid(6, 2));
	int minute = _tstoi(expirationdate.Mid(8, 2));
	CTime expiration(year, month, day, hour, minute, 0);
	return (expiration < CTime::GetCurrentTime());
}
