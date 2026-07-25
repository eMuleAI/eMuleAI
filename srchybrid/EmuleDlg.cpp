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
#include "stdafx.h"
#include <afxinet.h>
#include "eMuleAI/DarkMode.h"
#include "eMuleAI/DebugLeakHelper.h"
#include "ToolTipCtrlX.h"

#define MMNODRV			// mmsystem: Installable driver support
//#define MMNOSOUND		// mmsystem: Sound support
#define MMNOWAVE		// mmsystem: Waveform support
#define MMNOMIDI		// mmsystem: MIDI support
#define MMNOAUX			// mmsystem: Auxiliary audio support
#define MMNOMIXER		// mmsystem: Mixer support
#define MMNOTIMER		// mmsystem: Timer support
#define MMNOJOY			// mmsystem: Joystick support
#define MMNOMCI			// mmsystem: MCI support
#define MMNOMMIO		// mmsystem: Multimedia file I/O support
#define MMNOMMSYSTEM	// mmsystem: General MMSYSTEM functions

#include <Mmsystem.h>
#include <HtmlHelp.h>
#include <share.h>
#include <math.h>
#include <vector>
#include <memory>
#include <dbt.h>
#include "emule.h"
#include "emuleDlg.h"
#include "otherfunctions.h"
#include "ServerWnd.h"
#include "KademliaWnd.h"
#include "TransferWnd.h"
#include "TransferDlg.h"
#include "SearchResultsWnd.h"
#include "SearchDlg.h"
#include "SharedFilesWnd.h"
#include "ChatWnd.h"
#include "IrcWnd.h"
#include "StatisticsDlg.h"
#include "PreferencesDlg.h"
#include "PPgSecurity.h"
#include "eMuleAI\MigrationWizardDlg.h"
#include "ServerConnect.h"
#include "KnownFileList.h"
#include "SHAHashSet.h"
#include "ServerList.h"
#include "Opcodes.h"
#include "SharedFileList.h"
#include "ED2KLink.h"
#include "Splashscreen.h"
#include "PartFileConvert.h"
#include "EnBitmap.h"
#include "Exceptions.h"
#include "SearchList.h"
#include "eMuleAI/DownloadValidator.h"
#include "HTRichEditCtrl.h"
#include "FrameGrabThread.h"
#include "kademlia/kademlia/kademlia.h"
#include "kademlia/kademlia/SearchManager.h"
#include "kademlia/routing/RoutingZone.h"
#include "kademlia/routing/contact.h"
#include "kademlia/kademlia/prefs.h"
#include "KadSearchListCtrl.h"
#include "KadContactListCtrl.h"
#include "PerfLog.h"
#include "DropTarget.h"
#include "LastCommonRouteFinder.h"
#include "WebServer.h"
#include "DownloadQueue.h"
#include "ClientUDPSocket.h"
#include "UploadQueue.h"
#include "ClientList.h"
#include "UploadBandwidthThrottler.h"
#include "FriendList.h"
#include "IPFilter.h"
#include "Statistics.h"
#include "MuleToolbarCtrl.h"
#include "TaskbarNotifier.h"
#include "MuleStatusbarCtrl.h"
#include "ListenSocket.h"
#include "Server.h"
#include "PartFile.h"
#include "Scheduler.h"
#include "ClientCredits.h"
#include "MenuCmds.h"
#include "MuleSystrayDlg.h"
#include "IPFilterDlg.h"
#include "WebServices.h"
#include "DirectDownloadDlg.h"
#include "Statistics.h"
#include "StringConversion.h"
#include "aichsyncthread.h"
#include "Log.h"
#include "MiniMule.h"
#include "UserMsgs.h"
#include "TextToSpeech.h"
#include "Collection.h"
#include "CollectionViewDialog.h"
#include "UPnPImpl.h"
#include "UPnPImplWrapper.h"
#include "ExitBox.h"
#include "UploadDiskIOThread.h"
#include "PartFileWriteThread.h"
#include "Preferences.h"
#include "eMuleAI/IPGeolocation.h" 
#include "eMuleAI/NetBind.h"
#include "eMuleAI/IpGuard.h"
#include "Friend.h"
#include "EditDelayed.h"
#include <shellapi.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern BOOL FirstTimeWizard();

#define	SYS_TRAY_ICON_COOKIE_FORCE_UPDATE	UINT_MAX

UINT g_uMainThreadId = 0;
static const UINT UWM_ARE_YOU_EMULE = RegisterWindowMessage(EMULE_GUID);

#ifdef HAVE_WIN7_SDK_H
static const UINT UWM_TASK_BUTTON_CREATED = RegisterWindowMessage(_T("TaskbarButtonCreated"));
#endif

namespace
{
	typedef void (CALLBACK *PNetBindMibChangeCallback)(PVOID, PVOID, ULONG);
	typedef DWORD (WINAPI *PNotifyNetBindIpInterfaceChange)(USHORT, PNetBindMibChangeCallback, PVOID, BOOLEAN, HANDLE*);
	typedef DWORD (WINAPI *PNotifyNetBindUnicastIpAddressChange)(USHORT, PNetBindMibChangeCallback, PVOID, BOOLEAN, HANDLE*);
	typedef DWORD (WINAPI *PCancelNetBindMibChangeNotify2)(HANDLE);

	volatile LONG g_lNetBindNotificationGeneration = 0;
	volatile LONG g_lNetBindNotificationCancelled = 1;
	volatile LONG g_lNetBindAddressChangePostPending = 0;
	HWND g_hNetBindNotificationWnd = NULL;

	HMODULE GetNetBindIpHelperModule()
	{
		HMODULE hModule = ::GetModuleHandle(_T("iphlpapi.dll"));
		if (hModule == NULL)
			hModule = ::LoadLibrary(_T("iphlpapi.dll"));
		return hModule;
	}

	void PostNetBindAddressChanged(PVOID pCallerContext)
	{
		const LONG lGeneration = static_cast<LONG>(reinterpret_cast<INT_PTR>(pCallerContext));
		if (::InterlockedCompareExchange(&g_lNetBindNotificationCancelled, 0, 0) != 0)
			return;
		if (lGeneration != ::InterlockedCompareExchange(&g_lNetBindNotificationGeneration, 0, 0))
			return;

		HWND hWnd = g_hNetBindNotificationWnd;
		if (hWnd == NULL || !::IsWindow(hWnd))
			return;
		if (::InterlockedCompareExchange(&g_lNetBindAddressChangePostPending, 1, 0) != 0)
			return;
		if (!::PostMessage(hWnd, UM_BIND_ADDRESS_CHANGED, static_cast<WPARAM>(lGeneration), 0))
			::InterlockedExchange(&g_lNetBindAddressChangePostPending, 0);
	}

	void CALLBACK NetBindInterfaceChangeCallback(PVOID pCallerContext, PVOID, ULONG)
	{
		PostNetBindAddressChanged(pCallerContext);
	}

	void CALLBACK NetBindAddressChangeCallback(PVOID pCallerContext, PVOID, ULONG)
	{
		PostNetBindAddressChanged(pCallerContext);
	}


	ADDRESS_FAMILY GetIpGuardPublicIpProbeFamily(const std::vector<SIpGuardAllowedPublicIpRange>& ranges)
	{
		const CAddress::EAF eActiveFamily = thePrefs.GetActiveBindResolvedFamily();
		if (eActiveFamily == CAddress::IPv6)
			return AF_INET6;
		if (eActiveFamily == CAddress::IPv4)
			return AF_INET;

		bool bHasIpv4 = false;
		bool bHasIpv6 = false;
		for (const SIpGuardAllowedPublicIpRange& range : ranges) {
			if (range.address.GetType() == CAddress::IPv6)
				bHasIpv6 = true;
			else if (range.address.GetType() == CAddress::IPv4)
				bHasIpv4 = true;
		}
		if (bHasIpv6 && !bHasIpv4)
			return AF_INET6;
		if (bHasIpv4 && !bHasIpv6)
			return AF_INET;
		return AF_UNSPEC;
	}

	CString GetNotifierFallbackTitle(TbnMsg nMsgType)
	{
		switch (nMsgType) {
		case TBN_CHAT:
			return GetResString(_T("PW_TBN_POP_ALWAYS"));
		case TBN_DOWNLOADFINISHED:
			return GetResString(_T("PW_TBN_ONDOWNLOAD"));
		case TBN_DOWNLOADADDED:
			return GetResString(_T("TBN_ONNEWDOWNLOAD"));
		case TBN_LOG:
			return GetResString(_T("PW_TBN_ONLOG"));
		case TBN_IMPORTANTEVENT:
			return GetResString(_T("ERROR"));
		case TBN_NEWVERSION:
			return GetResString(_T("CB_TBN_ONNEWVERSION_EMULEAI_RELEASED"));
		default:
			return GetResString(_T("EMULENOTIFICATION"));
		}
	}

	void SplitNotifierText(LPCTSTR pszText, TbnMsg nMsgType, CString& strTitle, CString& strBody)
	{
		CString strText(pszText != NULL ? pszText : _T(""));
		strText.Replace(_T("\r\n"), _T("\n"));
		strText.Replace(_T("\r"), _T("\n"));
		strText.Trim();

		const int iBreak = strText.Find(_T('\n'));
		if (iBreak >= 0) {
			strTitle = strText.Left(iBreak);
			strBody = strText.Mid(iBreak + 1);
		} else {
			strTitle = GetNotifierFallbackTitle(nMsgType);
			strBody = strText;
		}

		strTitle.Trim();
		strBody.Trim();
		if (strTitle.IsEmpty())
			strTitle = GetNotifierFallbackTitle(nMsgType);
		if (strBody.IsEmpty())
			strBody = strTitle;
	}

	DWORD GetTrayBalloonInfoFlags(TbnMsg nMsgType)
	{
		return nMsgType == TBN_IMPORTANTEVENT ? NIIF_WARNING : NIIF_INFO;
	}

	template <typename T>
	void DestroyAndDeleteWnd(T*& pWnd)
	{
		if (pWnd == NULL)
			return;

		HWND hWnd = pWnd->GetSafeHwnd();
		if (hWnd != NULL && ::IsWindow(hWnd))
			pWnd->DestroyWindow();

		delete pWnd;
		pWnd = NULL;
	}

	template <typename T>
	void DestroyAndDeleteFrameWndSafe(T*& pWnd, HWND& hWndTracked)
	{
		if (hWndTracked == NULL) {
			pWnd = NULL;
			return;
		}

		CWnd* pLive = CWnd::FromHandlePermanent(hWndTracked);
		if (::IsWindow(hWndTracked)) {
			if (pLive != NULL)
				pLive->DestroyWindow();
			else
				::DestroyWindow(hWndTracked);
		}

		pWnd = NULL;
		hWndTracked = NULL;
	}

	void DeleteCollectionImportResult(SCollectionImportResult* pResult)
	{
		if (pResult == NULL)
			return;
		delete pResult->pCollection;
		delete pResult;
	}



	void DeleteStartupKnownFilesLoadResult(SStartupKnownFilesLoadResult* pResult)
	{
		if (pResult == NULL)
			return;
		CKnownFileList::DeleteStartupKnownFilesRecords(pResult->pKnownRecords, pResult->pCancelledRecords);
		CKnownFileList::DeleteStartupKnownFilesParsedFiles(pResult->vecParsedKnownFiles);
		delete pResult;
	}

	void DeleteStartupClientHistoryLoadResult(SStartupClientHistoryLoadResult* pResult)
	{
		if (pResult == NULL)
			return;
		CClientList::DeleteStartupClientHistoryRecords(pResult->pRecords);
		delete pResult;
	}

	void DeleteStartupDownloadsLoadResult(CDownloadQueue::SStartupDownloadLoadResult* pResult)
	{
		CDownloadQueue::DeleteStartupDownloadLoadResult(pResult);
	}

	void DeleteStartupStoredSearchesLoadResult(CSearchList::SStartupStoredSearchesLoadResult* pResult)
	{
		CSearchList::DeleteStartupStoredSearchesLoadResult(pResult);
	}

	void DeleteOwnedWebAsyncRequestMessage(LPARAM lParam)
	{
		SWebAsyncRequest* pRequest = reinterpret_cast<SWebAsyncRequest*>(lParam);
		if (pRequest != NULL && pRequest->IsValidWebAsyncRequest()) {
			pRequest->Complete();
			pRequest->ReleaseReference();
		}
	}

	void DrainOwnedWebAsyncMessages(HWND hWnd, UINT uMessage)
	{
		MSG msg;
		while (::PeekMessage(&msg, hWnd, uMessage, uMessage, PM_REMOVE))
			DeleteOwnedWebAsyncRequestMessage(msg.lParam);
	}

	struct SShutdownSourceSaveContext
	{
		SShutdownSourceSaveContext()
			: lNextIndex(0)
			, lDone(0)
			, lFailed(0)
		{
		}

		std::vector<SaveSourcesData*> snapshots;
		volatile LONG lNextIndex;
		volatile LONG lDone;
		volatile LONG lFailed;
	};

	UINT AFX_CDECL ShutdownSourceSaveWorkerProc(LPVOID pParam)
	{
		SShutdownSourceSaveContext* pContext = reinterpret_cast<SShutdownSourceSaveContext*>(pParam);
		if (pContext == NULL)
			return 1;

		for (;;) {
			const LONG lIndex = ::InterlockedIncrement(&pContext->lNextIndex) - 1;
			if (lIndex < 0 || static_cast<size_t>(lIndex) >= pContext->snapshots.size())
				break;

			SaveSourcesData* pData = pContext->snapshots[static_cast<size_t>(lIndex)];
			if (pData != NULL) {
				if (!CSourceSaver::WriteSourcesSnapshotNow(*pData, true))
					::InterlockedIncrement(&pContext->lFailed);
				delete pData;
			}
			::InterlockedIncrement(&pContext->lDone);
		}
		return 0;
	}

	UINT GetShutdownSourceSaveWorkerCount(UINT uSnapshotCount)
	{
		if (uSnapshotCount < 2)
			return uSnapshotCount;

		SYSTEM_INFO si;
		::GetSystemInfo(&si);
		const UINT uCpuCount = max(1U, si.dwNumberOfProcessors);
		return min(2U, min(uSnapshotCount, uCpuCount));
	}

	void RunShutdownSourceSaveWorkers(CemuleDlg* pDlg, SShutdownSourceSaveContext& context, UINT uProgressBase, UINT uProgressTotal)
	{
		const UINT uSnapshotCount = static_cast<UINT>(min(static_cast<size_t>(UINT_MAX), context.snapshots.size()));
		if (uSnapshotCount == 0)
			return;

		std::vector<CWinThread*> workers;
		workers.reserve(GetShutdownSourceSaveWorkerCount(uSnapshotCount));
		for (UINT i = 0; i < GetShutdownSourceSaveWorkerCount(uSnapshotCount); ++i) {
			CWinThread* pThread = AfxBeginThread(ShutdownSourceSaveWorkerProc, &context, THREAD_PRIORITY_BELOW_NORMAL, 0, CREATE_SUSPENDED);
			if (pThread == NULL)
				continue;
			pThread->m_bAutoDelete = FALSE;
			pThread->ResumeThread();
			workers.push_back(pThread);
		}

		if (workers.empty()) {
			ShutdownSourceSaveWorkerProc(&context);
			if (pDlg != NULL)
				pDlg->UpdateShutdownProgress(CemuleDlg::ShutdownProgressSaveData, min(uProgressTotal, uProgressBase + uSnapshotCount), uProgressTotal, true);
			return;
		}

		std::vector<HANDLE> handles;
		handles.reserve(workers.size());
		for (std::vector<CWinThread*>::const_iterator it = workers.begin(); it != workers.end(); ++it) {
			if ((*it)->m_hThread != NULL)
				handles.push_back((*it)->m_hThread);
		}

		DWORD dwLastProgressPaint = 0;
		while (!handles.empty()) {
			DWORD dwWait = ::WaitForMultipleObjects(static_cast<DWORD>(handles.size()), &handles[0], FALSE, 50);
			if (dwWait >= WAIT_OBJECT_0 && dwWait < WAIT_OBJECT_0 + handles.size())
				handles.erase(handles.begin() + (dwWait - WAIT_OBJECT_0));
			else if (dwWait == WAIT_FAILED)
				break;

			const DWORD dwNow = ::GetTickCount();
			if (pDlg != NULL && (handles.empty() || dwNow - dwLastProgressPaint >= 100)) {
				const UINT uDone = static_cast<UINT>(min(static_cast<LONG>(uSnapshotCount), ::InterlockedCompareExchange(&context.lDone, 0, 0)));
				pDlg->UpdateShutdownProgress(CemuleDlg::ShutdownProgressSaveData, min(uProgressTotal, uProgressBase + uDone), uProgressTotal, handles.empty());
				dwLastProgressPaint = dwNow;
			}
		}

		for (std::vector<CWinThread*>::iterator it = workers.begin(); it != workers.end(); ++it) {
			if ((*it)->m_hThread != NULL)
				::WaitForSingleObject((*it)->m_hThread, INFINITE);
			delete *it;
		}
	}

	const size_t kStartupDownloadsApplyFilesPerSlice = 16;
	const size_t kStartupStoredSearchesApplyFilesPerSlice = 64;
	const size_t kStartupKnownFilesApplyRecordsPerSlice = 64;
	const size_t kStartupKnownFilesParseRecordsPerSlice = 4096;
	const size_t kStartupKnownFilesAttachRecordsPerSlice = 512;
	const DWORD kStartupApplyProgressTraceIntervalMs = 1000;
	const UINT kStartupApplyBlockedPollIntervalMs = 50;
	const size_t kStartupKnownFilesCancelledRecordsPerSlice = 4096;
	const size_t kStartupClientHistoryApplyRecordsPerSlice = 64;
	const size_t kStartupClientHistoryCompletionRecordsPerSlice = 256;
	const size_t kStartupClientHistoryCompletionClientsPerSlice = 256;
	const size_t kStartupClientHistoryArchiveLoadClientsPerSlice = 128;

	bool ShouldYieldStartupApplyForUi()
	{
		if (theApp.emuledlg == NULL || !theApp.emuledlg->IsStartupLoadingDialogVisible())
			return false;
		const UINT uQueueStatus = HIWORD(::GetQueueStatus(QS_KEY | QS_MOUSE | QS_PAINT));
		return (uQueueStatus & (QS_KEY | QS_MOUSE | QS_PAINT)) != 0;
	}

	enum EStartupApplyPumpDomain
	{
		StartupApplyPumpDomainDownloads = 0,
		StartupApplyPumpDomainKnownFiles,
		StartupApplyPumpDomainClientHistory,
		StartupApplyPumpDomainStoredSearches,
		StartupApplyPumpDomainCount
	};

	enum EStartupClientHistoryCompletionStep
	{
		StartupClientHistoryCompletionPendingRecords = 0,
		StartupClientHistoryCompletionRuntimeMap,
		StartupClientHistoryCompletionActiveLinks,
		StartupClientHistoryCompletionArchiveLoad,
		StartupClientHistoryCompletionReloadList,
		StartupClientHistoryCompletionDone
	};

	bool ApplyStartupKnownFilesCompletionSlice(SStartupKnownFilesLoadResult* pResult, UINT& uApplied, INT_PTR& iRemaining)
	{
		uApplied = 0;
		iRemaining = 0;
		if (pResult == NULL || theApp.knownfiles == NULL)
			return true;
		return theApp.knownfiles->ApplyStartupKnownFilesCompletionChunk(pResult->pCancelledRecords, pResult->dwCancelledFilesSeed, pResult->bCompletionStarted, pResult->uNextCancelledRecord, kStartupKnownFilesCancelledRecordsPerSlice, uApplied, iRemaining);
	}

	bool ApplyStartupClientHistoryCompletionSlice(CemuleDlg* pDlg, SStartupClientHistoryLoadResult* pResult, UINT& uApplied, INT_PTR& iRemaining)
	{
		uApplied = 0;
		iRemaining = 0;
		if (pDlg == NULL || pResult == NULL || theApp.clientlist == NULL)
			return true;

		switch (pResult->uCompletionStep) {
		case StartupClientHistoryCompletionPendingRecords:
			if (!theApp.clientlist->ApplyStartupClientHistoryPendingRecordsChunk(pResult->uNextPendingRecord, kStartupClientHistoryCompletionRecordsPerSlice, uApplied, iRemaining))
				return true;
			if (iRemaining > 0)
				return false;
			pResult->uCompletionStep = StartupClientHistoryCompletionRuntimeMap;
			iRemaining = 1;
			return false;
		case StartupClientHistoryCompletionRuntimeMap:
			if (!theApp.clientlist->RebuildStartupClientHistoryRuntimeMapChunk(pResult->aRuntimeMapPendingDeleteClients, pResult->uNextRuntimeMapPendingDeleteClient, pResult->posNextRuntimeMapArchiveClient, pResult->bRuntimeMapRebuildStarted, uApplied, iRemaining))
				return true;
			if (iRemaining > 0)
				return false;
			pResult->uCompletionStep = StartupClientHistoryCompletionActiveLinks;
			iRemaining = 1;
			return false;
		case StartupClientHistoryCompletionActiveLinks:
			if (!pResult->bActiveClientLinkStarted) {
				pResult->posNextActiveClient = theApp.clientlist->list.GetHeadPosition();
				pResult->bActiveClientLinkStarted = true;
			}
			if (!theApp.clientlist->ApplyStartupClientHistoryActiveLinksChunk(pResult->posNextActiveClient, kStartupClientHistoryCompletionClientsPerSlice, uApplied, iRemaining))
				return true;
			if (iRemaining > 0)
				return false;
			pResult->uCompletionStep = StartupClientHistoryCompletionArchiveLoad;
			iRemaining = 1;
			return false;
		case StartupClientHistoryCompletionArchiveLoad:
			if (pDlg->transferwnd != NULL && pDlg->transferwnd->GetClientList() != NULL) {
				if (!pResult->bArchiveLoadStarted) {
					pResult->posNextArchiveLoadClient = theApp.clientlist->list.GetHeadPosition();
					pResult->bArchiveLoadStarted = true;
				}
				CClientListCtrl* pClientListCtrl = pDlg->transferwnd->GetClientList();
				const DWORD dwSliceStart = ::GetTickCount();
				while (pResult->posNextArchiveLoadClient != NULL && uApplied < kStartupClientHistoryArchiveLoadClientsPerSlice) {
					CUpDownClient* pClient = theApp.clientlist->list.GetNext(pResult->posNextArchiveLoadClient);
					if (pClient != NULL)
						pClientListCtrl->LoadArchive(pClient, _T("AsyncClientHistoryLoad"));
					++uApplied;
					if (theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetStartupApply))
						break;
				}
				if (pResult->posNextArchiveLoadClient != NULL) {
					iRemaining = 1;
					return false;
				}
			}
			pResult->uCompletionStep = StartupClientHistoryCompletionReloadList;
			iRemaining = 1;
			return false;
		case StartupClientHistoryCompletionReloadList:
			if (pDlg->transferwnd != NULL && pDlg->transferwnd->GetClientList() != NULL) {
				CClientListCtrl* pClientListCtrl = pDlg->transferwnd->GetClientList();
				if (::IsWindow(pClientListCtrl->GetSafeHwnd())) {
					if (pDlg->IsStartupLoadingDialogVisible())
						pClientListCtrl->MarkDeferredReload();
					else
						pClientListCtrl->ReloadList(false, LSF_SELECTION);
				}
			}
			pResult->uCompletionStep = StartupClientHistoryCompletionDone;
			iRemaining = 1;
			return false;
		default:
			theApp.clientlist->FinalizeStartupClientHistoryLoadApply();
			return true;
		}
	}


	bool ShouldTraceStartupApplySlice(LPCTSTR pszContext, DWORD dwNow)
	{
		struct SStartupApplyTraceRateLimit
		{
			bool bUsed;
			CString strContext;
			DWORD dwLastTraceTick;
		};

		static SStartupApplyTraceRateLimit s_aTraceRateLimits[8];
		static size_t s_uNextTraceRateLimitSlot = 0;
		const CString strContext(pszContext != NULL ? pszContext : _T(""));

		for (size_t uSlot = 0; uSlot < ARRSIZE(s_aTraceRateLimits); ++uSlot) {
			SStartupApplyTraceRateLimit& state = s_aTraceRateLimits[uSlot];
			if (!state.bUsed || state.strContext != strContext)
				continue;
			if (state.dwLastTraceTick != 0 && static_cast<DWORD>(dwNow - state.dwLastTraceTick) < kStartupApplyProgressTraceIntervalMs)
				return false;
			state.dwLastTraceTick = dwNow;
			return true;
		}

		for (size_t uSlot = 0; uSlot < ARRSIZE(s_aTraceRateLimits); ++uSlot) {
			SStartupApplyTraceRateLimit& state = s_aTraceRateLimits[uSlot];
			if (state.bUsed)
				continue;
			state.bUsed = true;
			state.strContext = strContext;
			state.dwLastTraceTick = dwNow;
			return true;
		}

		SStartupApplyTraceRateLimit& state = s_aTraceRateLimits[s_uNextTraceRateLimitSlot++ % ARRSIZE(s_aTraceRateLimits)];
		state.bUsed = true;
		state.strContext = strContext;
		state.dwLastTraceTick = dwNow;
		return true;
	}

	void TraceStartupApplySliceIfHardExceeded(DWORD dwApplyStartTick, LPCTSTR pszContext, UINT uApplied, INT_PTR iRemaining)
	{
		if (uApplied == 0 && iRemaining == 0)
			return;

		DWORD dwApplyElapsed = 0;
		if (!theApp.IsTimeBudgetHardExceeded(dwApplyStartTick, CemuleApp::TimeBudgetStartupApply, &dwApplyElapsed))
			return;

		const DWORD dwNow = ::GetTickCount();
		if (!ShouldTraceStartupApplySlice(pszContext, dwNow))
			return;

		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetStartupApply, pszContext, dwApplyElapsed, uApplied, iRemaining);
	}

	void DrainOwnedStartupAndCollectionMessages(HWND hWnd)
	{
		if (hWnd == NULL)
			return;

		theApp.ClearCollectionImportResults(hWnd);

		MSG msg;
		while (::PeekMessage(&msg, hWnd, CemuleDlg::UWM_EMULEAI_COLLECTION_IMPORT_READY, CemuleDlg::UWM_EMULEAI_COLLECTION_IMPORT_READY, PM_REMOVE))
			DeleteCollectionImportResult(reinterpret_cast<SCollectionImportResult*>(msg.lParam));
		while (::PeekMessage(&msg, hWnd, CemuleDlg::UWM_EMULEAI_STARTUP_KNOWNFILES_LOAD_READY, CemuleDlg::UWM_EMULEAI_STARTUP_KNOWNFILES_LOAD_READY, PM_REMOVE))
			DeleteStartupKnownFilesLoadResult(reinterpret_cast<SStartupKnownFilesLoadResult*>(msg.lParam));
		while (::PeekMessage(&msg, hWnd, CemuleDlg::UWM_EMULEAI_STARTUP_CLIENTHISTORY_LOAD_READY, CemuleDlg::UWM_EMULEAI_STARTUP_CLIENTHISTORY_LOAD_READY, PM_REMOVE))
			DeleteStartupClientHistoryLoadResult(reinterpret_cast<SStartupClientHistoryLoadResult*>(msg.lParam));
		while (::PeekMessage(&msg, hWnd, CemuleDlg::UWM_EMULEAI_STARTUP_DOWNLOADS_LOAD_READY, CemuleDlg::UWM_EMULEAI_STARTUP_DOWNLOADS_LOAD_READY, PM_REMOVE))
			DeleteStartupDownloadsLoadResult(reinterpret_cast<CDownloadQueue::SStartupDownloadLoadResult*>(msg.lParam));
		while (::PeekMessage(&msg, hWnd, CemuleDlg::UWM_EMULEAI_STARTUP_STOREDSEARCHES_LOAD_READY, CemuleDlg::UWM_EMULEAI_STARTUP_STOREDSEARCHES_LOAD_READY, PM_REMOVE))
			DeleteStartupStoredSearchesLoadResult(reinterpret_cast<CSearchList::SStartupStoredSearchesLoadResult*>(msg.lParam));
		while (::PeekMessage(&msg, hWnd, CemuleDlg::UWM_EMULEAI_STARTUP_APPLY_PUMP, CemuleDlg::UWM_EMULEAI_STARTUP_APPLY_PUMP, PM_REMOVE)) {
		}
		while (::PeekMessage(&msg, hWnd, CemuleDlg::UWM_EMULEAI_STARTUP_OVERLAY_REFRESH, CemuleDlg::UWM_EMULEAI_STARTUP_OVERLAY_REFRESH, PM_REMOVE)) {
		}

		const UINT aWebAsyncMessages[] =
		{
			WEB_GET_SEARCH_RESULTS,
			WEB_GET_TRANSFER_SNAPSHOT,
			WEB_GET_SHARED_FILES_SNAPSHOT,
			WEB_GET_SERVER_LIST_SNAPSHOT,
			WEB_SERVER_COMMAND,
			WEB_GET_HEADER_SNAPSHOT,
			WEB_GET_COMMENT_LIST,
			WEB_FRIEND_COMMAND
		};
		for (size_t i = 0; i < _countof(aWebAsyncMessages); ++i)
			DrainOwnedWebAsyncMessages(hWnd, aWebAsyncMessages[i]);
	}

	int GetMainWndDialogIdForPersistence(const CemuleDlg& dlg)
	{
		struct MainWndEntry
		{
			CWnd* pWnd;
			int iDialogId;
		};

		const MainWndEntry aVisibleWindowOrder[] =
		{
			{ dlg.serverwnd, IDD_SERVER },
			{ dlg.sharedfileswnd, IDD_FILES },
			{ dlg.searchwnd, IDD_SEARCH },
			{ dlg.chatwnd, IDD_CHAT },
			{ dlg.transferwnd, IDD_TRANSFER },
			{ dlg.statisticswnd, IDD_STATISTICS },
			{ dlg.kademliawnd, IDD_KADEMLIAWND },
			{ dlg.ircwnd, IDD_IRC }
		};

		for (const MainWndEntry& entry : aVisibleWindowOrder) {
			if (entry.pWnd != NULL && ::IsWindow(entry.pWnd->GetSafeHwnd()) && entry.pWnd->IsWindowVisible())
				return entry.iDialogId;
		}

		CWnd* pActiveWnd = dlg.GetActiveDialog();
		if (pActiveWnd == NULL)
			return 0;

		if (pActiveWnd->IsKindOf(RUNTIME_CLASS(CServerWnd)))
			return IDD_SERVER;
		if (pActiveWnd->IsKindOf(RUNTIME_CLASS(CSharedFilesWnd)))
			return IDD_FILES;
		if (pActiveWnd->IsKindOf(RUNTIME_CLASS(CSearchDlg)))
			return IDD_SEARCH;
		if (pActiveWnd->IsKindOf(RUNTIME_CLASS(CChatWnd)))
			return IDD_CHAT;
		if (pActiveWnd->IsKindOf(RUNTIME_CLASS(CTransferDlg)))
			return IDD_TRANSFER;
		if (pActiveWnd->IsKindOf(RUNTIME_CLASS(CStatisticsDlg)))
			return IDD_STATISTICS;
		if (pActiveWnd->IsKindOf(RUNTIME_CLASS(CKademliaWnd)))
			return IDD_KADEMLIAWND;
		if (pActiveWnd->IsKindOf(RUNTIME_CLASS(CIrcWnd)))
			return IDD_IRC;

		ASSERT(0);
		return 0;
	}

	int GetVisibleToolbarContentRightEdge(CMuleToolbarCtrl& toolbar)
	{
		CRect rcClient;
		toolbar.GetClientRect(&rcClient);

		int iRightEdge = rcClient.left;
		TBBUTTON button = {};
		CRect rcItem;
		CRect rcVisibleItem;
		for (int i = 0; i < toolbar.GetButtonCount(); ++i) {
			if (!toolbar.GetButton(i, &button))
				continue;
			if (button.fsState & TBSTATE_HIDDEN)
				continue;
			if (button.fsStyle & TBSTYLE_SEP)
				continue;
			if (!toolbar.GetItemRect(i, &rcItem))
				continue;
			if (rcVisibleItem.IntersectRect(&rcClient, &rcItem))
				iRightEdge = max(iRightEdge, rcVisibleItem.right);
		}

		CSize sizMax;
		if (toolbar.GetMaxSize(&sizMax) && sizMax.cx > 0)
			iRightEdge = min(iRightEdge, rcClient.left + sizMax.cx);

		return iRightEdge;
	}

	constexpr int kStatusBarTextRightPadding = 4;
	constexpr int kStatusBarIconLeftPadding = 5;
	constexpr int kStatusBarTextAfterIconPadding = 4;
	constexpr int kStatusBarDynamicPanePadding = 12;
	constexpr int kStatusBarMinimumChatPaneWidth = 25;
	constexpr int kStatusBarDynamicPaneCount = 5;

	int GetStatusBarPaneTextWidth(CDC& dc, const CString& strText)
	{
		if (strText.IsEmpty())
			return 0;

		return dc.GetTextExtent(strText).cx;
	}

	int GetStatusBarChatPaneWidth()
	{
		return max(kStatusBarMinimumChatPaneWidth, kStatusBarIconLeftPadding + GetSystemMetrics(SM_CXSMICON) + kStatusBarTextRightPadding);
	}

	int GetStatusBarPaneIconWidth(CMuleStatusBarCtrl& statusbar, EStatusBarPane ePane)
	{
		if ((HICON)statusbar.SendMessage(SB_GETICON, static_cast<WPARAM>(ePane), 0) == NULL)
			return 0;

		return kStatusBarIconLeftPadding + GetSystemMetrics(SM_CXSMICON) + kStatusBarTextAfterIconPadding;
	}

	int GetStatusBarPaneIdealWidth(CMuleStatusBarCtrl& statusbar, CDC& dc, EStatusBarPane ePane)
	{
		return GetStatusBarPaneIconWidth(statusbar, ePane) + GetStatusBarPaneTextWidth(dc, statusbar.GetText(ePane)) + kStatusBarTextRightPadding + kStatusBarDynamicPanePadding;
	}

	int GetStatusBarPaneMinimumWidth(CMuleStatusBarCtrl& statusbar, CDC& dc, EStatusBarPane ePane)
	{
		return GetStatusBarPaneIconWidth(statusbar, ePane) + GetStatusBarPaneTextWidth(dc, _T("...")) + kStatusBarTextRightPadding;
	}

	void ShrinkStatusBarPaneWidthsToFit(int* piWidths, const int* piMinimumWidths, int iPaneCount, int iAvailableWidth)
	{
		if (piWidths == NULL || piMinimumWidths == NULL || iPaneCount <= 0)
			return;

		int iTotalWidth = 0;
		int iTotalFlexibleWidth = 0;
		for (int i = 0; i < iPaneCount; ++i) {
			iTotalWidth += piWidths[i];
			iTotalFlexibleWidth += max(0, piWidths[i] - piMinimumWidths[i]);
		}

		int iOverflow = iTotalWidth - iAvailableWidth;
		if (iOverflow <= 0)
			return;

		if (iTotalFlexibleWidth > 0) {
			if (iPaneCount > kStatusBarDynamicPaneCount)
				return;

			int aiReductions[kStatusBarDynamicPaneCount] = {};
			int iAppliedReduction = 0;
			for (int i = 0; i < iPaneCount; ++i) {
				const int iFlexibleWidth = max(0, piWidths[i] - piMinimumWidths[i]);
				if (iFlexibleWidth <= 0)
					continue;

				aiReductions[i] = min(iFlexibleWidth, MulDiv(iOverflow, iFlexibleWidth, iTotalFlexibleWidth));
				iAppliedReduction += aiReductions[i];
			}

			for (int i = 0; i < iPaneCount; ++i)
				piWidths[i] -= aiReductions[i];

			iOverflow -= iAppliedReduction;
		}

		while (iOverflow > 0) {
			bool bReduced = false;
			for (int i = 0; i < iPaneCount && iOverflow > 0; ++i) {
				if (piWidths[i] > piMinimumWidths[i]) {
					--piWidths[i];
					--iOverflow;
					bReduced = true;
				}
			}

			if (!bReduced)
				break;
		}

		while (iOverflow > 0) {
			bool bReduced = false;
			for (int i = 0; i < iPaneCount && iOverflow > 0; ++i) {
				if (piWidths[i] > 0) {
					--piWidths[i];
					--iOverflow;
					bReduced = true;
				}
			}

			if (!bReduced)
				break;
		}
	}
}

#ifndef NMRBCUSTOMDRAW
typedef struct tagNMRBCUSTOMDRAW {
	NMCUSTOMDRAW nmcd;
	COLORREF     clrText;
	COLORREF     clrTextHighlight;
	RECT         rcBand;
	UINT         fStyle;
	UINT         wID;
	COLORREF     clrBtnText;
	COLORREF     clrBtnTextHighlight;
	RECT         rcChevronLocation;
	UINT         uChevronState;
} NMRBCUSTOMDRAW, * LPNMRBCUSTOMDRAW;
#endif

namespace
{
	const UINT TITLE_VERSION_ANIMATION_INTERVAL_MS = 60;
	const UINT TITLE_VERSION_ANIMATION_HUE_STEP = 7;
	const UINT TITLE_VERSION_CHARACTER_HUE_STEP = 28;
	const UINT TITLE_VERSION_WAVE_PHASE_STEP = 20;
	const UINT IP_GUARD_OVERLAY_BLINK_PHASE = 90;
	const UINT SPECIAL_THANKS_ANIMATION_INTERVAL_MS = 33;
	const DWORD SPLASH_AUTO_CLOSE_DELAY_MS = 1000;
	const DWORD VERSIONCHECK_HTTP_TIMEOUT_MS = SEC2MS(8);
	const int VERSIONCHECK_MAX_TEXT_BYTES = 4096;
	const UINT STARTUP_TIMER_INTERVAL_MS = SEC2MS(3) / 10;
	const UINT MAIN_TIMER_INTERVAL_MS = SEC2MS(DAY2S(1));
	const int TITLE_VERSION_TEXT_MIN_CHANNEL = 80;
	const int TITLE_VERSION_TEXT_SHADOW_OFFSET = 1;
	const int TITLE_VERSION_TEXT_HORIZONTAL_PADDING = 8;
	const int TITLE_VERSION_OVERLAY_PADDING_X = 3;
	const int TITLE_VERSION_OVERLAY_PADDING_Y = 2;
	const int TITLE_VERSION_TEXT_WAVE_AMPLITUDE = 2;
	const double TITLE_VERSION_DEG_TO_RAD = 3.14159265358979323846 / 180.0;
	const COLORREF TITLE_VERSION_OVERLAY_COLORKEY = RGB(255, 0, 255);
	const LPCTSTR TITLE_VERSION_LINK_TEXT_KEY = _T("CB_TBN_ONNEWVERSION_EMULEAI_RELEASED");
	const LPCTSTR IP_GUARD_OVERLAY_GENERIC_KEY = _T("IP_GUARD_OVERLAY_GENERIC");
	const LPCTSTR IP_GUARD_OVERLAY_BIND_UNAVAILABLE_KEY = _T("IP_GUARD_OVERLAY_BIND_UNAVAILABLE");
	const LPCTSTR IP_GUARD_OVERLAY_VERIFYING_KEY = _T("IP_GUARD_OVERLAY_VERIFYING");
	const LPCTSTR IP_GUARD_OVERLAY_PUBLIC_IP_FMT_KEY = _T("IP_GUARD_OVERLAY_PUBLIC_IP_FMT");
	const LPCTSTR EMULE_AI_CHATGPT_HELP_BASE_URL = _T("https://chatgpt.com/?");
	const LPCTSTR EMULE_AI_CHATGPT_PROMPT_PARAM = _T("prompt");

#ifndef DWMWA_CAPTION_BUTTON_BOUNDS
	#define DWMWA_CAPTION_BUTTON_BOUNDS 5
#endif

	class CNetworkBindSocketCreationScope
	{
	public:
		CNetworkBindSocketCreationScope()
		{
			theApp.BeginNetworkBindSocketCreation();
		}

		~CNetworkBindSocketCreationScope()
		{
			theApp.EndNetworkBindSocketCreation();
		}
	};

	struct EmuleAIVersionCheckRequest
	{
		HWND hNotifyWnd;
		CString strVersionRawUrl;
		CString strCurrentVersion;
		bool bManual;
	};

	struct EmuleAIVersionCheckResult
	{
		bool bRequestSucceeded;
		bool bHasNewVersion;
		CString strRemoteVersion;
		bool bManual;
	};

	bool IsUnreservedUrlByte(BYTE byValue)
	{
		return (byValue >= 'A' && byValue <= 'Z')
			|| (byValue >= 'a' && byValue <= 'z')
			|| (byValue >= '0' && byValue <= '9')
			|| byValue == '-'
			|| byValue == '_'
			|| byValue == '.'
			|| byValue == '~';
	}

	CString EncodeUrlQueryUtf8(const CString& strValue)
	{
		const CStringA strUtf8(StrToUtf8(strValue));
		CString strEncoded;
		static const TCHAR s_hexDigits[] = _T("0123456789ABCDEF");

		for (int i = 0; i < strUtf8.GetLength(); ++i) {
			const BYTE byValue = static_cast<BYTE>(strUtf8[i]);
			if (IsUnreservedUrlByte(byValue)) {
				strEncoded.AppendChar(static_cast<TCHAR>(byValue));
			} else {
				strEncoded.AppendChar(_T('%'));
				strEncoded.AppendChar(s_hexDigits[(byValue >> 4) & 0x0F]);
				strEncoded.AppendChar(s_hexDigits[byValue & 0x0F]);
			}
		}

		return strEncoded;
	}

	CString BuildEmuleAiAssistantPromptText(LPCTSTR pszPromptTemplateResKey)
	{
		const CString strPromptTemplate(GetResString(pszPromptTemplateResKey));
		CString strPromptText;
		strPromptText.Format(strPromptTemplate, MOD_PAGES_BASE_URL, MOD_REPO_BASE_URL);
		return strPromptText;
	}

	CString BuildEmuleAiAssistantUrl(LPCTSTR pszBaseUrl, LPCTSTR pszQueryParamName, LPCTSTR pszPromptTemplateResKey)
	{
		const CString strPromptText(BuildEmuleAiAssistantPromptText(pszPromptTemplateResKey));
		CString strUrl;
		strUrl.Format(_T("%s%s=%s"), pszBaseUrl, pszQueryParamName, (LPCTSTR)EncodeUrlQueryUtf8(strPromptText));
		return strUrl;
	}

	CString NormalizeVersionString(const CString& strVersion)
	{
		CString strNormalized(strVersion);
		strNormalized.Replace(_T("\r"), _T(""));
		strNormalized.Replace(_T("\n"), _T(""));
		strNormalized.Trim();

		CString strPrefixCheck(strNormalized);
		strPrefixCheck.MakeLower();
		if (strPrefixCheck.Left(4) == _T("ai v")) {
			strNormalized = strNormalized.Mid(4);
			strNormalized.TrimLeft();
		} else if (strPrefixCheck.Left(3) == _T("ai ")) {
			strNormalized = strNormalized.Mid(3);
			strNormalized.TrimLeft();
		}

		if (!strNormalized.IsEmpty() && (strNormalized[0] == _T('v') || strNormalized[0] == _T('V'))) {
			strNormalized = strNormalized.Mid(1);
			strNormalized.TrimLeft();
		}

		return strNormalized;
	}

	enum VersionSuffixType
	{
		VersionSuffixPreRelease = 0,
		VersionSuffixNone = 1
	};

	struct ParsedVersionString
	{
		CArray<UINT, UINT> aComponents;
		CString strSuffix;
		VersionSuffixType eSuffixType;
	};

	bool TryParseVersionString(const CString& str, ParsedVersionString& parsedVersion)
	{
		parsedVersion.aComponents.RemoveAll();
		parsedVersion.strSuffix.Empty();
		parsedVersion.eSuffixType = VersionSuffixNone;

		if (str.IsEmpty())
			return false;

		bool bHasDot = false;
		int nPos = 0;
		while (nPos < str.GetLength()) {
			if (!_istdigit(str[nPos]))
				return false;

			UINT uComponent = 0;
			while (nPos < str.GetLength() && _istdigit(str[nPos])) {
				const UINT uDigit = static_cast<UINT>(str[nPos] - _T('0'));
				if (uComponent > (UINT_MAX - uDigit) / 10)
					return false;
				uComponent = (uComponent * 10) + uDigit;
				++nPos;
			}
			parsedVersion.aComponents.Add(uComponent);

			if (nPos >= str.GetLength())
				break;

			if (str[nPos] == _T('.')) {
				bHasDot = true;
				++nPos;
				if (nPos >= str.GetLength())
					return false;
				continue;
			}

			if (str[nPos] == _T('-')) {
				parsedVersion.eSuffixType = VersionSuffixPreRelease;
				++nPos;
			} else if (_istspace(str[nPos])) {
				parsedVersion.eSuffixType = VersionSuffixPreRelease;
				while (nPos < str.GetLength() && _istspace(str[nPos]))
					++nPos;
			} else if (_istalpha(str[nPos])) {
				parsedVersion.eSuffixType = VersionSuffixPreRelease;
			} else {
				return false;
			}

			if (nPos >= str.GetLength() || !_istalpha(str[nPos]))
				return false;

			for (int i = nPos; i < str.GetLength(); ++i) {
				if (!_istalnum(str[i]))
					return false;
			}

			parsedVersion.strSuffix = str.Mid(nPos);
			break;
		}

		return bHasDot && parsedVersion.aComponents.GetSize() > 0;
	}

	bool IsValidVersionString(const CString& str)
	{
		ParsedVersionString parsedVersion;
		return TryParseVersionString(str, parsedVersion);
	}

	int GetVersionSuffixRank(const CString& strSuffixName)
	{
		if (strSuffixName.CompareNoCase(_T("alpha")) == 0 || strSuffixName.CompareNoCase(_T("a")) == 0)
			return 0;
		if (strSuffixName.CompareNoCase(_T("beta")) == 0 || strSuffixName.CompareNoCase(_T("b")) == 0)
			return 1;
		if (strSuffixName.CompareNoCase(_T("rc")) == 0)
			return 2;
		return 3;
	}

	void SplitVersionSuffix(const CString& strSuffix, CString& strSuffixName, UINT& uSuffixNumber, bool& bHasSuffixNumber)
	{
		strSuffixName.Empty();
		uSuffixNumber = 0;
		bHasSuffixNumber = false;

		int nPos = 0;
		while (nPos < strSuffix.GetLength() && _istalpha(strSuffix[nPos]))
			++nPos;

		strSuffixName = strSuffix.Left(nPos);
		if (nPos >= strSuffix.GetLength())
			return;

		bHasSuffixNumber = true;
		while (nPos < strSuffix.GetLength() && _istdigit(strSuffix[nPos])) {
			const UINT uDigit = static_cast<UINT>(strSuffix[nPos] - _T('0'));
			if (uSuffixNumber <= (UINT_MAX - uDigit) / 10)
				uSuffixNumber = (uSuffixNumber * 10) + uDigit;
			++nPos;
		}
	}

	int CompareVersionSuffixes(const CString& strLeft, const CString& strRight)
	{
		CString strLeftName;
		CString strRightName;
		UINT uLeftNumber = 0;
		UINT uRightNumber = 0;
		bool bHasLeftNumber = false;
		bool bHasRightNumber = false;
		SplitVersionSuffix(strLeft, strLeftName, uLeftNumber, bHasLeftNumber);
		SplitVersionSuffix(strRight, strRightName, uRightNumber, bHasRightNumber);

		const int nLeftRank = GetVersionSuffixRank(strLeftName);
		const int nRightRank = GetVersionSuffixRank(strRightName);
		if (nLeftRank < nRightRank)
			return -1;
		if (nLeftRank > nRightRank)
			return 1;

		const int nNameCompare = strLeftName.CompareNoCase(strRightName);
		if (nNameCompare < 0)
			return -1;
		if (nNameCompare > 0)
			return 1;

		if (bHasLeftNumber && bHasRightNumber) {
			if (uLeftNumber < uRightNumber)
				return -1;
			if (uLeftNumber > uRightNumber)
				return 1;
		} else if (bHasLeftNumber != bHasRightNumber)
			return bHasLeftNumber ? 1 : -1;

		return 0;
	}

	int CompareVersionStrings(const CString& strLeft, const CString& strRight)
	{
		ParsedVersionString leftVersion;
		ParsedVersionString rightVersion;
		if (!TryParseVersionString(strLeft, leftVersion) || !TryParseVersionString(strRight, rightVersion))
			return 0;

		const int nMaxComponentCount = max(leftVersion.aComponents.GetSize(), rightVersion.aComponents.GetSize());
		for (int i = 0; i < nMaxComponentCount; ++i) {
			const UINT uLeftComponent = (i < leftVersion.aComponents.GetSize()) ? leftVersion.aComponents[i] : 0;
			const UINT uRightComponent = (i < rightVersion.aComponents.GetSize()) ? rightVersion.aComponents[i] : 0;
			if (uLeftComponent < uRightComponent)
				return -1;
			if (uLeftComponent > uRightComponent)
				return 1;
		}

		if (leftVersion.eSuffixType < rightVersion.eSuffixType)
			return -1;
		if (leftVersion.eSuffixType > rightVersion.eSuffixType)
			return 1;

		const int nSuffixCompare = CompareVersionSuffixes(leftVersion.strSuffix, rightVersion.strSuffix);
		if (nSuffixCompare < 0)
			return -1;
		if (nSuffixCompare > 0)
			return 1;

		return 0;
	}

	bool IsRemoteVersionNewer(const CString& strRemoteVersion, const CString& strCurrentVersion)
	{
		if (!IsValidVersionString(strRemoteVersion) || !IsValidVersionString(strCurrentVersion))
			return false;

		return CompareVersionStrings(strRemoteVersion, strCurrentVersion) > 0;
	}

	CString GetFirstNonEmptyLine(const CString& strText)
	{
		int nPos = 0;
		for (;;) {
			CString strLine = strText.Tokenize(_T("\r\n"), nPos);
			strLine.Trim();
			if (!strLine.IsEmpty())
				return strLine;
			if (nPos == -1)
				break;
		}
		return _T("");
	}

	bool QueryVersionCheckHttpStatusCode(HINTERNET hVersionFile, DWORD& dwStatusCode)
	{
		dwStatusCode = 0;
		DWORD dwStatusCodeSize = sizeof(dwStatusCode);
		if (!::HttpQueryInfo(hVersionFile, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &dwStatusCode, &dwStatusCodeSize, NULL)) {
			TRACE(_T("Version check failed in HttpQueryInfo(STATUS_CODE), Error: %lu\n"), ::GetLastError());
			return false;
		}

		return true;
	}

	bool QueryVersionCheckContentType(HINTERNET hVersionFile, CString& strContentType)
	{
		strContentType.Empty();
		TCHAR szContentType[128] = {};
		DWORD dwContentTypeSize = _countof(szContentType);
		if (!::HttpQueryInfo(hVersionFile, HTTP_QUERY_CONTENT_TYPE, szContentType, &dwContentTypeSize, NULL)) {
			TRACE(_T("Version check failed in HttpQueryInfo(CONTENT_TYPE), Error: %lu\n"), ::GetLastError());
			return false;
		}

		strContentType = szContentType;
		strContentType.Trim();
		return !strContentType.IsEmpty();
	}

	bool IsVersionCheckContentTypeAllowed(const CString& strContentType)
	{
		CString strNormalizedContentType(strContentType);
		strNormalizedContentType.MakeLower();
		return strNormalizedContentType.Left(10) == _T("text/plain")
			|| strNormalizedContentType == _T("application/octet-stream");
	}

	bool TryExtractVersionFromDownloadedText(const CString& strDownloadedText, CString& strVersion)
	{
		strVersion = NormalizeVersionString(GetFirstNonEmptyLine(strDownloadedText));
		if (strVersion.IsEmpty()) {
			TRACE(_T("Version check failed because the response body did not contain a non-empty version line.\n"));
			return false;
		}

		if (!IsValidVersionString(strVersion)) {
			TRACE(_T("Version check failed because the response body was not a valid version string. Response=\"%s\"\n"), (LPCTSTR)strVersion);
			return false;
		}

		return true;
	}

	bool DownloadVersionTextFromUrl(const CString& strUrl, CString& strDownloadedText)
	{
		strDownloadedText.Empty();
		if (theApp.IsNetworkActivityBlockedByBind())
			return false;

		HINTERNET hInternet = ::InternetOpen(_T("eMuleAI-VersionCheck"), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
		if (hInternet == NULL) {
			TRACE(_T("Version check failed in InternetOpen, Error: %lu\n"), ::GetLastError());
			return false;
		}

		DWORD dwTimeoutMs = VERSIONCHECK_HTTP_TIMEOUT_MS;
		VERIFY(::InternetSetOption(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &dwTimeoutMs, sizeof(dwTimeoutMs)) != FALSE);
		VERIFY(::InternetSetOption(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &dwTimeoutMs, sizeof(dwTimeoutMs)) != FALSE);
		VERIFY(::InternetSetOption(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &dwTimeoutMs, sizeof(dwTimeoutMs)) != FALSE);

		const DWORD dwOpenUrlFlags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_KEEP_CONNECTION;
		HINTERNET hVersionFile = ::InternetOpenUrl(hInternet, strUrl, NULL, 0, dwOpenUrlFlags, 0);
		if (hVersionFile == NULL) {
			TRACE(_T("Version check failed in InternetOpenUrl, Error: %lu\n"), ::GetLastError());
			VERIFY(::InternetCloseHandle(hInternet));
			return false;
		}

		DWORD dwHttpStatusCode = 0;
		if (!QueryVersionCheckHttpStatusCode(hVersionFile, dwHttpStatusCode)) {
			VERIFY(::InternetCloseHandle(hVersionFile));
			VERIFY(::InternetCloseHandle(hInternet));
			return false;
		}
		if (dwHttpStatusCode != HTTP_STATUS_OK) {
			TRACE(_T("Version check failed because the server returned HTTP status %lu.\n"), dwHttpStatusCode);
			VERIFY(::InternetCloseHandle(hVersionFile));
			VERIFY(::InternetCloseHandle(hInternet));
			return false;
		}

		CString strContentType;
		if (!QueryVersionCheckContentType(hVersionFile, strContentType)) {
			VERIFY(::InternetCloseHandle(hVersionFile));
			VERIFY(::InternetCloseHandle(hInternet));
			return false;
		}
		if (!IsVersionCheckContentTypeAllowed(strContentType)) {
			TRACE(_T("Version check failed because the server returned an unexpected content type. ContentType=\"%s\"\n"), (LPCTSTR)strContentType);
			VERIFY(::InternetCloseHandle(hVersionFile));
			VERIFY(::InternetCloseHandle(hInternet));
			return false;
		}

		CStringA strContentA;
		char acBuffer[512];
		bool bReadOk = true;
		for (;;) {
			DWORD dwBytesRead = 0;
			if (!::InternetReadFile(hVersionFile, acBuffer, sizeof(acBuffer), &dwBytesRead)) {
				TRACE(_T("Version check failed in InternetReadFile, Error: %lu\n"), ::GetLastError());
				bReadOk = false;
				break;
			}
			if (dwBytesRead == 0)
				break;
			strContentA.Append(acBuffer, (int)dwBytesRead);
			if (strContentA.GetLength() > VERSIONCHECK_MAX_TEXT_BYTES) {
				strContentA = strContentA.Left(VERSIONCHECK_MAX_TEXT_BYTES);
				break;
			}
		}

		VERIFY(::InternetCloseHandle(hVersionFile));
		VERIFY(::InternetCloseHandle(hInternet));

		if (!bReadOk || strContentA.IsEmpty())
			return false;

		strDownloadedText = OptUtf8ToStr(strContentA);
		if (!strDownloadedText.IsEmpty() && strDownloadedText[0] == 0xFEFF)
			strDownloadedText = strDownloadedText.Mid(1);
		strDownloadedText.Trim();
		return !strDownloadedText.IsEmpty();
	}

	bool GetCaptionButtonBounds(HWND hWnd, CRect& rcCaptionButtons)
	{
		rcCaptionButtons.SetRectEmpty();

		HMODULE hDwmApi = ::LoadLibrary(_T("dwmapi.dll"));
		if (hDwmApi == NULL)
			return false;

		HRESULT(WINAPI * pfnDwmGetWindowAttribute)(HWND, DWORD, PVOID, DWORD) = NULL;
		(FARPROC&)pfnDwmGetWindowAttribute = ::GetProcAddress(hDwmApi, "DwmGetWindowAttribute");
		if (pfnDwmGetWindowAttribute == NULL) {
			VERIFY(::FreeLibrary(hDwmApi));
			return false;
		}

		RECT rcButtons = {};
		const HRESULT hResult = pfnDwmGetWindowAttribute(hWnd, DWMWA_CAPTION_BUTTON_BOUNDS, &rcButtons, sizeof(rcButtons));
		VERIFY(::FreeLibrary(hDwmApi));
		if (FAILED(hResult) || ::IsRectEmpty(&rcButtons))
			return false;

		rcCaptionButtons = rcButtons;
		CRect rcWindow;
		if (::GetWindowRect(hWnd, &rcWindow)) {
			// DWMWA_CAPTION_BUTTON_BOUNDS can be window-relative; normalize to screen space.
			const bool bLooksWindowRelative = (rcCaptionButtons.left >= 0 && rcCaptionButtons.top >= 0
				&& rcCaptionButtons.right <= rcWindow.Width() && rcCaptionButtons.bottom <= rcWindow.Height());
			if (bLooksWindowRelative)
				rcCaptionButtons.OffsetRect(rcWindow.left, rcWindow.top);
		}
		return true;
	}
}

class CTitleVersionOverlayWnd : public CWnd
{
public:
	CTitleVersionOverlayWnd()
		: m_pOwner(NULL)
	{
	}

	bool CreateOverlay(CemuleDlg* pOwner)
	{
		m_pOwner = pOwner;
		const CString strClassName(AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_HAND), NULL, NULL));
		if (!CWnd::CreateEx(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, strClassName, _T(""), WS_POPUP, 0, 0, 0, 0, (pOwner != NULL) ? pOwner->GetSafeHwnd() : NULL, NULL))
			return false;

		if (!::SetLayeredWindowAttributes(m_hWnd, TITLE_VERSION_OVERLAY_COLORKEY, 0, LWA_COLORKEY))
			TRACE(_T("SetLayeredWindowAttributes failed for title version overlay. Error: %lu\n"), ::GetLastError());

		return true;
	}

	void SetOwner(CemuleDlg* pOwner)
	{
		m_pOwner = pOwner;
	}

protected:
	afx_msg void OnPaint()
	{
		CPaintDC dc(this);
		CRect rcClient;
		GetClientRect(&rcClient);
		dc.FillSolidRect(&rcClient, TITLE_VERSION_OVERLAY_COLORKEY);

		if (m_pOwner != NULL)
			m_pOwner->DrawTitleVersionOverlay(dc, rcClient);
	}

	afx_msg void OnLButtonDown(UINT nFlags, CPoint point)
	{
		UNREFERENCED_PARAMETER(nFlags);
		UNREFERENCED_PARAMETER(point);
		if (m_pOwner != NULL && !m_pOwner->IsIpGuardOverlayVisible())
			m_pOwner->OpenVersionReleasesURL();
	}

	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
	{
		UNREFERENCED_PARAMETER(pWnd);
		UNREFERENCED_PARAMETER(nHitTest);
		UNREFERENCED_PARAMETER(message);
		if (m_pOwner == NULL || m_pOwner->IsIpGuardOverlayVisible())
			return FALSE;
		::SetCursor(AfxGetApp()->LoadStandardCursor(IDC_HAND));
		return TRUE;
	}

	afx_msg LRESULT OnNcHitTest(CPoint point)
	{
		if (m_pOwner == NULL || m_pOwner->IsIpGuardOverlayVisible() || !m_pOwner->m_rcTitleVersionLink.PtInRect(point))
			return HTTRANSPARENT;
		return HTCLIENT;
	}

	DECLARE_MESSAGE_MAP()

private:
	CemuleDlg* m_pOwner;
};

BEGIN_MESSAGE_MAP(CTitleVersionOverlayWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_LBUTTONDOWN()
	ON_WM_SETCURSOR()
	ON_WM_NCHITTEST()
END_MESSAGE_MAP()

///////////////////////////////////////////////////////////////////////////
// CemuleDlg Dialog

IMPLEMENT_DYNAMIC(CMsgBoxException, CException)

BEGIN_MESSAGE_MAP(CemuleDlg, CTrayDialog)
	///////////////////////////////////////////////////////////////////////////
	// Windows messages
	//
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_ENDSESSION()
	ON_WM_SIZE()
	ON_WM_MOVING()
	ON_WM_SIZING()
	ON_WM_WINDOWPOSCHANGING()
	ON_WM_WINDOWPOSCHANGED()
	ON_WM_CLOSE()
	ON_WM_MENUCHAR()
	ON_WM_QUERYENDSESSION()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_CTLCOLOR()
	ON_MESSAGE(WM_COPYDATA, OnWMData)
	ON_MESSAGE(WM_KICKIDLE, OnKickIdle)
	ON_MESSAGE(WM_USERCHANGED, OnUserChanged)
	ON_WM_SHOWWINDOW()
	ON_WM_DESTROY()
	ON_WM_SETTINGCHANGE()
	ON_WM_DEVICECHANGE()
	ON_WM_NCPAINT()
	ON_WM_NCLBUTTONDOWN()
	ON_WM_SETCURSOR()
	ON_WM_TIMER()
	ON_MESSAGE(WM_DISPLAYCHANGE, OnDisplayChange)
	ON_MESSAGE(WM_POWERBROADCAST, OnPowerBroadcast)

	///////////////////////////////////////////////////////////////////////////
	// WM_COMMAND messages
	//
	ON_COMMAND(MP_CONNECT, OnCommandConnect)
	ON_COMMAND(MP_DISCONNECT, CloseConnection)
	ON_COMMAND(MP_EXIT, OnClose)
	ON_COMMAND(MP_RESTORE, RestoreWindow)
	// quick-speed changer --
	ON_COMMAND_RANGE(MP_QS_U10, MP_QS_UP10, QuickSpeedUpload)
	ON_COMMAND_RANGE(MP_QS_D10, MP_QS_DC, QuickSpeedDownload)
	//--- quickspeed - paralize all ---
	ON_COMMAND_RANGE(MP_QS_PA, MP_QS_UA, QuickSpeedOther)
	// quick-speed changer -- based on xrmb
	ON_NOTIFY_EX_RANGE(RBN_CHEVRONPUSHED, 0, 0xffff, OnChevronPushed)

	ON_REGISTERED_MESSAGE(UWM_ARE_YOU_EMULE, OnAreYouEmule)
	ON_BN_CLICKED(IDC_HOTMENU, OnBnClickedHotmenu)

	ON_REGISTERED_MESSAGE(UWM_CONCHECKER, OnConChecker)

	ON_MESSAGE(UWM_POST_INIT_CONTROLS, OnPostInitControls)

	///////////////////////////////////////////////////////////////////////////
	// WM_USER messages
	//
	ON_MESSAGE(UM_TASKBARNOTIFIERCLICKED, OnTaskbarNotifierClicked)
	ON_MESSAGE(UM_TOAST_NOTIFICATION_CLICKED, OnToastNotificationClicked)
	ON_MESSAGE(UM_STARTUP_LOADING_CANCEL_EXIT, OnStartupLoadingCancelExit)
	ON_MESSAGE(UM_BIND_ADDRESS_CHANGED, OnBindAddressChanged)
	ON_MESSAGE(UM_IP_GUARD_PROBE_RESULT, OnIpGuardProbeResult)
	ON_MESSAGE(UM_CLOSE_MINIMULE, OnCloseMiniMule)

	// Web Server messages
	ON_MESSAGE(WEB_GUI_INTERACTION, OnWebGUIInteraction)
	ON_MESSAGE(WEB_GET_SEARCH_RESULTS, OnWebGetSearchResults)
	ON_MESSAGE(WEB_SERVER_COMMAND, OnWebServerCommand)
	ON_MESSAGE(WEB_GET_TRANSFER_SNAPSHOT, OnWebGetTransferSnapshot)
	ON_MESSAGE(WEB_GET_SHARED_FILES_SNAPSHOT, OnWebGetSharedFilesSnapshot)
	ON_MESSAGE(WEB_GET_SERVER_LIST_SNAPSHOT, OnWebGetServerListSnapshot)
	ON_MESSAGE(WEB_GET_HEADER_SNAPSHOT, OnWebGetHeaderSnapshot)
	ON_MESSAGE(WEB_GET_COMMENT_LIST, OnWebGetCommentList)
	ON_MESSAGE(WEB_FRIEND_COMMAND, OnWebFriendCommand)

	ON_MESSAGE(UWM_EMULEAI_VERSIONCHECK_RESULT, OnEmuleAIVersionCheckResult)
	ON_MESSAGE(UWM_EMULEAI_PROCESS_CHUNKED_DOWNLOADS, OnProcessChunkedDownloads)
	ON_MESSAGE(UWM_EMULEAI_PROCESS_CHUNKED_DOWNLOAD_PARSE, OnProcessChunkedDownloadParse)
	ON_MESSAGE(UWM_EMULEAI_PROCESS_CHUNKED_SEARCH_INGEST, OnProcessChunkedSearchIngest)
	ON_MESSAGE(UWM_EMULEAI_COLLECTION_IMPORT_READY, OnCollectionImportReady)
	ON_MESSAGE(UWM_EMULEAI_STARTUP_KNOWNFILES_LOAD_READY, OnStartupKnownFilesLoadReady)
	ON_MESSAGE(UWM_EMULEAI_STARTUP_CLIENTHISTORY_LOAD_READY, OnStartupClientHistoryLoadReady)
	ON_MESSAGE(UWM_EMULEAI_STARTUP_DOWNLOADS_LOAD_READY, OnStartupDownloadsLoadReady)
	ON_MESSAGE(UWM_EMULEAI_STARTUP_STOREDSEARCHES_LOAD_READY, OnStartupStoredSearchesLoadReady)
	ON_MESSAGE(UWM_EMULEAI_STARTUP_OVERLAY_REFRESH, OnStartupOverlayRefresh)
	ON_MESSAGE(UWM_EMULEAI_STARTUP_APPLY_PUMP, OnStartupApplyPump)
	ON_MESSAGE(UWM_EMULEAI_IPFILTER_DOWNLOAD_PROGRESS, OnIPFilterDownloadProgress)
	ON_MESSAGE(UWM_EMULEAI_IPFILTER_DOWNLOAD_FINISHED, OnIPFilterDownloadFinished)
	ON_MESSAGE(UWM_EMULEAI_IPGEOLOCATION_DOWNLOAD_PROGRESS, OnIPGeolocationDownloadProgress)
	ON_MESSAGE(UWM_EMULEAI_IPGEOLOCATION_DOWNLOAD_FINISHED, OnIPGeolocationDownloadFinished)
	ON_MESSAGE(UWM_EMULEAI_FLUSH_UI_LOG, OnFlushUiLog)
	ON_MESSAGE(UWM_EMULEAI_DISPATCH_APPLICATION_EVENT, OnDispatchApplicationEvent)
	ON_MESSAGE(UWM_EMULEAI_PROCESS_BACKEND_COMMANDS, OnProcessBackendCommands)

	// UPnP
	ON_MESSAGE(UM_UPNP_RESULT, OnUPnPResult)

	///////////////////////////////////////////////////////////////////////////
	// WM_APP messages
	//
	ON_MESSAGE(TM_FINISHEDHASHING, OnFileHashed)
	ON_MESSAGE(TM_FINISHEDPARTFILEHASHING, OnPartFileHashed)
	ON_MESSAGE(TM_FILEOPPROGRESS, OnFileOpProgress)
	ON_MESSAGE(TM_HASHFAILED, OnHashFailed)
	ON_MESSAGE(TM_PARTFILEHASHFAILED, OnPartFileHashFailed)
	ON_MESSAGE(TM_IMPORTPART, OnImportPart)
	ON_MESSAGE(TM_IMPORTPARTPROGRESS, OnImportPartProgress)
	ON_MESSAGE(TM_IMPORTPARTFINISHED, OnImportPartFinished)
	ON_MESSAGE(UM_FINALIZE_DELETE_PENDING_CLIENT, OnFinalizeDeletePendingClient)
	ON_MESSAGE(TM_FRAMEGRABFINISHED, OnFrameGrabFinished)
	ON_MESSAGE(TM_FILEALLOCEXC, OnFileAllocExc)
	ON_MESSAGE(TM_FILECOMPLETED, OnFileCompleted)
	ON_MESSAGE(TM_CONSOLETHREADEVENT, OnConsoleThreadEvent)
	ON_MESSAGE(TM_SHAREDFILELISTFOUNDFILES, OnSharedFileListFoundFiles)
	ON_MESSAGE(TM_SHAREDFILESCTRLUPDATEFILE, OnSharedFilesCtrlUpdateFile)

	ON_MESSAGE(UM_APP_SWITCH_DARKMODE, OnDarkModeSwitch)
	ON_NOTIFY(NM_CUSTOMDRAW, AFX_IDW_REBAR, OnRebarCustomDraw)

#ifdef HAVE_WIN7_SDK_H
	ON_REGISTERED_MESSAGE(UWM_TASK_BUTTON_CREATED, OnTaskbarBtnCreated)
#endif
END_MESSAGE_MAP()

bool CemuleDlg::SPartFileOpProgressKey::operator<(const SPartFileOpProgressKey& other) const
{
	const UINT_PTR uThisPartFile = reinterpret_cast<UINT_PTR>(pPartFile);
	const UINT_PTR uOtherPartFile = reinterpret_cast<UINT_PTR>(other.pPartFile);
	if (uThisPartFile != uOtherPartFile)
		return uThisPartFile < uOtherPartFile;
	if (dwRuntimeID != other.dwRuntimeID)
		return dwRuntimeID < other.dwRuntimeID;
	return memcmp(abyFileHash, other.abyFileHash, sizeof(abyFileHash)) < 0;
}

CemuleDlg::CemuleDlg(CWnd* pParent /*=NULL*/)
	: CTrayDialog(CemuleDlg::IDD, pParent)
	, m_pSplashWnd()
	, activewnd()
	, status()
	, m_wpFirstRestore()
	, m_hIcon()
	, m_hIconSmall()
	, m_connicons()
	, m_contactIcons()
	, transicons()
	, imicons()
	, m_icoSysTrayCurrent()
	, usericon()
	, m_icoSysTrayConnected()
	, m_icoSysTrayDisconnected()
	, m_icoSysTrayLowID()
	, m_pSystrayDlg()
	, m_pDropTarget()
	, m_iMsgIcon()
	, m_uLastSysTrayIconCookie(SYS_TRAY_ICON_COOKIE_FORCE_UPDATE)
	, m_uUpDatarate()
	, m_uDownDatarate()
	, m_bVersionCheckInProgress()
	, m_bNewVersionAvailable()
	, m_bStartMinimizedChecked()
	, m_bStartMinimized()
	, m_bMsgBlinkState()
	, m_bConnectRequestDelayedForUPnP()
	, m_bKadSuspendDisconnect()
	, m_bEd2kSuspendDisconnect()
	, m_bInitedCOM()
	, m_bSpecialThanksAnimationTimerActive()
	, m_thbButtons()
	, m_currentTBP_state(TBPF_NOPROGRESS)
	, m_prevProgress()
	, m_ovlIcon()
	, bPrevKadState()
	, bPrevEd2kState()
	, m_rcTitleVersionLink()
	, m_fontTitleVersionLink()
	, m_uTitleVersionAnimationHue()
	, m_bTitleVersionAnimationTimerActive()
	, m_bCloseAfterBulkOperations(false)
	, m_pTitleVersionOverlay()
	, m_pStartupLoadingDlg()
	, m_pShutdownProgressDlg()
	, m_bStartupLoadingMainWindowDeferred(false)
	, m_bStartupLoadingSuppressMainWindow(true)
	, m_bStartupLoadingExitRequested(false)
	, m_wpStartupLoadingRestorePlacement()
	, m_pPendingStartupDownloadsLoadResult(NULL)
	, m_pPendingStartupKnownFilesLoadResult(NULL)
	, m_pPendingStartupClientHistoryLoadResult(NULL)
	, m_pPendingStartupStoredSearchesLoadResult(NULL)
	, m_bStartupSearchKnownTypesRefreshed(false)
	, m_bStartupSearchKnownTypesRefreshQueued(false)
	, m_bStartupSearchKnownTypesReloadPending(false)
	, m_uStartupApplyPumpNextDomain(0)
	, m_bStartupApplyPumpTimerActive(false)
	, m_bStartupApplyPumpPostPending(false)
	, m_bStartupApplyPumpRunning(false)
	, m_lStartupOverlayRefreshPending(0)
	, m_dwLastStartupOverlayBulkRefreshTick(0)
	, m_uDroppedQueuedUiLogLines(0)
	, m_dwLastUiLogBacklogTrace(0)
	, m_dwUiLogFlushDeferredUntil(0)
	, m_bUiLogFlushTimerActive(false)
	, m_bUiLogFlushMessagePending(false)
	, m_hIpGuardInterfaceNotification(NULL)
	, m_hIpGuardAddressNotification(NULL)
	, m_bIpGuardStartupBlocked(false)
	, m_bIpGuardNetworkBlockActive(false)
	, m_bVpnGuardNetworkBlockActive(false)
	, m_bIpGuardMonitorActive(false)
	, m_bVpnGuardMonitorActive(false)
	, m_bIpGuardStartupProbePending(false)
	, m_bIpGuardStartupApproved(false)
	, m_bIpGuardRuntimeProbePending(false)
	, m_bVpnGuardProbePending(false)
	, m_bVpnGuardStartupApproved(false)
	, m_iVpnGuardPendingProbeCount(0)
	, m_bVpnGuardProbeHadSuccess(false)
	, m_bIpGuardRestoreEd2kConnection(false)
	, m_bIpGuardRestoreKadConnection(false)
	, m_uIpGuardProbeGeneration(0)
	, m_dwLastIpGuardRuntimeProbeTick(0)
	, m_dwLastVpnGuardRuntimeProbeTick(0)
	, m_lSharedFileListFoundFilesPendingMessage(0)
	, m_lSharedFilesCtrlUpdatePendingMessage(0)
	, m_lFileOpProgressPendingMessage(0)
	, m_dwSplashTime(_UI32_MAX)
	, m_pMiniMule()
	, m_hTimer()
	, m_hUPnPTimeOutTimer()
	, m_hWndSearchDlg()
	, m_hWndTransferDlg()
	, notifierenabled()
	, m_bNotifierRuntimeActive(true)
	, m_nTrayBalloonMsgType(TBN_NONOTIFY)
{
	m_wpStartupLoadingRestorePlacement.length = (UINT)sizeof m_wpStartupLoadingRestorePlacement;
	g_uMainThreadId = GetCurrentThreadId();
	theApp.SetBackendOwnerThreadId(g_uMainThreadId);
	SetClientIconList();
	preferenceswnd = new CPreferencesDlg;
	serverwnd = new CServerWnd;
	kademliawnd = new CKademliaWnd;
	transferwnd = new CTransferDlg;
	sharedfileswnd = new CSharedFilesWnd;
	searchwnd = new CSearchDlg;
	chatwnd = new CChatWnd;
	ircwnd = new CIrcWnd;
	statisticswnd = new CStatisticsDlg;
	toolbar = new CMuleToolbarCtrl;
	statusbar = new CMuleStatusBarCtrl;
	m_pDropTarget = new CMainFrameDropTarget;
}

void CemuleDlg::SetClientIconList()
{
	m_IconList.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	m_IconList.Add(CTempIconLoader(_T("ClientEDonkey")));			//0 - eDonkey
	m_IconList.Add(CTempIconLoader(_T("ClientEDonkeyPlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientCompatible")));		//2 - Compat
	m_IconList.Add(CTempIconLoader(_T("ClientCompatiblePlus")));
	m_IconList.Add(CTempIconLoader(_T("Friend")));					//4 - friend
	m_IconList.Add(CTempIconLoader(_T("ClientMLDonkey")));			//5 - ML
	m_IconList.Add(CTempIconLoader(_T("ClientMLDonkeyPlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientEDonkeyHybrid")));	//7 - Hybrid
	m_IconList.Add(CTempIconLoader(_T("ClientEDonkeyHybridPlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientShareaza")));			//9 - Shareaza
	m_IconList.Add(CTempIconLoader(_T("ClientShareazaPlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientAMule")));			//11 - amule
	m_IconList.Add(CTempIconLoader(_T("ClientAMulePlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientLPhant")));			//13 - Lphant
	m_IconList.Add(CTempIconLoader(_T("ClientLPhantPlus")));
	m_IconList.Add(CTempIconLoader(_T("Server")));					//15 - http source
	m_IconList.SetOverlayImage(m_IconList.Add(CTempIconLoader(_T("ClientSecureOvl"))), 1);
	m_IconList.SetOverlayImage(m_IconList.Add(CTempIconLoader(_T("OverlayObfu"))), 2);
	m_IconList.SetOverlayImage(m_IconList.Add(CTempIconLoader(_T("OverlaySecureObfu"))), 3);
}

CImageList& CemuleDlg::GetClientIconList()
{
	return m_IconList;
}


CemuleDlg::~CemuleDlg()
{
	UnregisterIpGuardNotifications();
	m_wndToastNotifier.Shutdown();
	ClearStartupApplyPumpState();
	HideStartupLoadingDialog(false);
	CloseTTS();
	DestroyMiniMule();
	if (m_icoSysTrayCurrent)
		VERIFY(::DestroyIcon(m_icoSysTrayCurrent));
	if (m_hIcon)
		VERIFY(::DestroyIcon(m_hIcon));
	if (m_hIconSmall)
		VERIFY(::DestroyIcon(m_hIconSmall));
	DestroyIconsArr(m_connicons, _countof(m_connicons));
	DestroyIconsArr(transicons, _countof(transicons));
	DestroyIconsArr(imicons, _countof(imicons));
	if (m_icoSysTrayConnected)
		VERIFY(::DestroyIcon(m_icoSysTrayConnected));
	if (m_icoSysTrayDisconnected)
		VERIFY(::DestroyIcon(m_icoSysTrayDisconnected));
	if (m_icoSysTrayLowID)
		VERIFY(::DestroyIcon(m_icoSysTrayLowID));
	if (usericon)
		VERIFY(::DestroyIcon(usericon));

#ifdef HAVE_WIN7_SDK_H
	if (m_pTaskbarList != NULL) {
		m_pTaskbarList.Release();
		ASSERT(m_bInitedCOM);
	}
	if (m_bInitedCOM)
		CoUninitialize();
#endif

	// already destroyed by windows?

	delete m_pDropTarget;
	delete statusbar;
	delete toolbar;
	DestroyAndDeleteWnd(statisticswnd);
	DestroyAndDeleteWnd(ircwnd);
	DestroyAndDeleteWnd(chatwnd);
	DestroyAndDeleteFrameWndSafe(searchwnd, m_hWndSearchDlg);
	DestroyAndDeleteWnd(sharedfileswnd);
	DestroyAndDeleteFrameWndSafe(transferwnd, m_hWndTransferDlg);
	DestroyAndDeleteWnd(kademliawnd);
	DestroyAndDeleteWnd(serverwnd);
	DestroyAndDeleteWnd(preferenceswnd);
}

void CemuleDlg::DoDataExchange(CDataExchange* pDX)
{
	CTrayDialog::DoDataExchange(pDX);
}

LRESULT CemuleDlg::OnAreYouEmule(WPARAM, LPARAM)
{
	return UWM_ARE_YOU_EMULE;
}

void DialogCreateIndirect(CDialog* pWnd, UINT uID)
{
#if 0
	// This could be a nice way to change the font size of the main windows without needing
	// to re-design the dialog resources. However, that technique does not work for the
	// SearchWnd and it also introduces new glitches (which would need to get resolved)
	// in almost all of the main windows.
	CDialogTemplate dlgTempl;
	dlgTempl.Load(MAKEINTRESOURCE(uID));
	dlgTempl.SetFont(_T("MS Shell Dlg"), 8);
	pWnd->CreateIndirect(dlgTempl.m_hTemplate);
	FreeResource(dlgTempl.Detach());
#else
	pWnd->Create(uID);
#endif
}

LRESULT CemuleDlg::OnPostInitControls(WPARAM, LPARAM)
{
	// Subclass and anchor status bar once the dialog and children are live
	if (!statusbar->m_hWnd) {
		if (CWnd* pSB = GetDlgItem(IDC_STATUSBAR); pSB && ::IsWindow(pSB->m_hWnd)) {
			statusbar->SubclassWindow(pSB->m_hWnd);
			statusbar->EnableToolTips(true);
			SetStatusBarPartsSize();
			AddOrReplaceAnchor(this, *statusbar, BOTTOM_LEFT, BOTTOM_RIGHT);
		} else
			return 0;
	}

	return 0;
}

LRESULT CemuleDlg::OnProcessChunkedDownloads(WPARAM, LPARAM)
{
	theApp.ProcessChunkedDownloadJobs();
	return 0;
}

LRESULT CemuleDlg::OnProcessChunkedDownloadParse(WPARAM, LPARAM)
{
	theApp.ProcessChunkedDownloadParseJobs();
	return 0;
}

LRESULT CemuleDlg::OnProcessChunkedSearchIngest(WPARAM, LPARAM)
{
	if (theApp.searchlist != NULL)
		theApp.searchlist->ProcessChunkedSearchIngestJobs();
	return 0;
}

LRESULT CemuleDlg::OnIPFilterDownloadProgress(WPARAM, LPARAM lParam)
{
	return CPPgSecurity::OnIPFilterDownloadProgress(lParam);
}

LRESULT CemuleDlg::OnIPFilterDownloadFinished(WPARAM, LPARAM lParam)
{
	return CPPgSecurity::OnIPFilterDownloadFinished(lParam);
}

LRESULT CemuleDlg::OnIPGeolocationDownloadProgress(WPARAM, LPARAM lParam)
{
	return CIPGeolocation::OnIPGeolocationDownloadProgress(lParam);
}

LRESULT CemuleDlg::OnIPGeolocationDownloadFinished(WPARAM, LPARAM lParam)
{
	return CIPGeolocation::OnIPGeolocationDownloadFinished(lParam);
}

LRESULT CemuleDlg::OnFlushUiLog(WPARAM, LPARAM)
{
	m_bUiLogFlushMessagePending = false;
	FlushQueuedUiLogLines();
	return 0;
}

LRESULT CemuleDlg::OnDispatchApplicationEvent(WPARAM, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	theApp.ProcessApplicationEventsFromUiThread();
	return 0;
}

LRESULT CemuleDlg::OnProcessBackendCommands(WPARAM, LPARAM)
{
	theApp.ProcessBackendCommands();
	return 0;
}

CString CemuleDlg::GetMainWindowTitleText() const
{
	CString strTitle(_T("eMule "));
	strTitle += MOD_VERSION;
#ifdef _DEBUG
	#if defined(_M_ARM64)
	const LPCTSTR pszPlatform = _T("arm64");
	#elif defined(_M_X64) || defined(_M_AMD64) || defined(_WIN64)
	const LPCTSTR pszPlatform = _T("x64");
	#elif defined(_M_IX86) || defined(_X86_)
	const LPCTSTR pszPlatform = _T("x86");
	#else
	const LPCTSTR pszPlatform = _T("unknown");
	#endif
	strTitle.AppendFormat(_T(" Debug %s"), pszPlatform);
#endif
	return strTitle;
}

BOOL CemuleDlg::OnInitDialog()
{
	theStats.starttime = ::GetTickCount();
#ifdef HAVE_WIN7_SDK_H
	// allow the TaskbarButtonCreated- & (tbb-)WM_COMMAND message to be sent to our window if our app is running elevated
	if (thePrefs.GetWindowsVersion() >= _WINVER_7_) {
		m_bInitedCOM = SUCCEEDED(CoInitialize(NULL));
		if (m_bInitedCOM) {
			typedef BOOL(WINAPI* PChangeWindowMessageFilter)(UINT message, DWORD dwFlag);
			PChangeWindowMessageFilter ChangeWindowMessageFilter
				= (PChangeWindowMessageFilter)(GetProcAddress(GetModuleHandle(_T("user32.dll")), "ChangeWindowMessageFilter"));
			if (ChangeWindowMessageFilter) {
				ChangeWindowMessageFilter(UWM_TASK_BUTTON_CREATED, 1);
				ChangeWindowMessageFilter(WM_COMMAND, 1);
				ChangeWindowMessageFilter(WM_SETTINGCHANGE, 1); // Also allow settings broadcasts while elevated (UIPI guard).
			}
		}
		else
			ASSERT(0);
	}
#endif

	// temporary disable the 'startup minimized' option, otherwise no window will be shown at all
	if (!thePrefs.IsFirstStart())
		m_bStartMinimized = thePrefs.GetStartMinimized() || theApp.DidWeAutoStart();

	// The startup loading dialog owns the initial progress UI. Show the splash only after it closes.

	// Create global GUI objects
	theApp.CreateAllFonts();
	theApp.CreateBackwardDiagonalBrush();
	m_wndTaskbarNotifier.SetTextDefaultFont();
	CTrayDialog::OnInitDialog();
	if (theApp.knownfiles != NULL && !theApp.KnownFilesReady())
		theApp.BeginStartupKnownFilesLoad();
	if (theApp.clientlist != NULL && thePrefs.GetClientHistory() && !theApp.ClientHistoryReady())
		theApp.BeginStartupClientHistoryLoad();
	CreateToolbarCmdIconMap();

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL) {
		pSysMenu->AppendMenu(MF_SEPARATOR);

		ASSERT((MP_ABOUTBOX & 0xFFF0) == MP_ABOUTBOX && MP_ABOUTBOX < 0xF000);
		pSysMenu->AppendMenu(MF_STRING, MP_ABOUTBOX, GetResString(_T("ABOUTBOX")));

		// remaining system menu entries are created later...
	}

	CWnd* pwndToolbarX = toolbar;
	if (toolbar->Create(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, CRect(), this, IDC_TOOLBAR)) {
		toolbar->Init();
		if (thePrefs.GetUseReBarToolbar()) {
			if (m_ctlMainTopReBar.Create(WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
				RBS_BANDBORDERS | RBS_AUTOSIZE | CCS_NODIVIDER,
				CRect(), this, AFX_IDW_REBAR))
			{
				CSize sizeBar;
				VERIFY(toolbar->GetMaxSize(&sizeBar));
				REBARBANDINFO rbbi = {};
				rbbi.cbSize = (UINT)sizeof rbbi;
				rbbi.fMask = RBBIM_STYLE | RBBIM_SIZE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_IDEALSIZE | RBBIM_ID;
				rbbi.fStyle = RBBS_NOGRIPPER | RBBS_BREAK | RBBS_USECHEVRON;
				rbbi.hwndChild = toolbar->m_hWnd;
				rbbi.cxMinChild = sizeBar.cy;
				rbbi.cyMinChild = sizeBar.cy;
				rbbi.cxIdeal = sizeBar.cx;
				rbbi.cx = rbbi.cxIdeal;
				VERIFY(m_ctlMainTopReBar.InsertBand(UINT_MAX, &rbbi));
				toolbar->SaveCurHeight();
				toolbar->UpdateBackground();

				pwndToolbarX = &m_ctlMainTopReBar;
			}
		}
	}

	// set title
	SetWindowText(GetMainWindowTitleText());
	EnsureMfcThreadToolTipCtrlX(this);

	// Init taskbar notifier
	m_wndTaskbarNotifier.CreateWnd(this);
	LoadNotifier(thePrefs.GetNotifierConfiguration());

	// set statusbar
	// the statusbar control is created as a custom control in the dialog resource,
	// this solves font and sizing problems when using large system fonts
	CWnd* pSB = GetDlgItem(IDC_STATUSBAR);
	if (pSB && ::IsWindow(pSB->m_hWnd)) {
		statusbar->SubclassWindow(GetDlgItem(IDC_STATUSBAR)->m_hWnd);
		statusbar->EnableToolTips(true);
		SetStatusBarPartsSize();
	} else
		PostMessage(UWM_POST_INIT_CONTROLS); // defer until created

	// create main window dialog pages
	DialogCreateIndirect(serverwnd, IDD_SERVER);
	DialogCreateIndirect(sharedfileswnd, IDD_FILES);
	searchwnd->CreateWnd(this); // can not use 'DialogCreateIndirect' for the SearchWnd, grrr...
	m_hWndSearchDlg = searchwnd->GetSafeHwnd();
	DialogCreateIndirect(chatwnd, IDD_CHAT);
	transferwnd->CreateWnd(this);
	m_hWndTransferDlg = transferwnd->GetSafeHwnd();
	DialogCreateIndirect(statisticswnd, IDD_STATISTICS);
	DialogCreateIndirect(kademliawnd, IDD_KADEMLIAWND);
	DialogCreateIndirect(ircwnd, IDD_IRC);

	// with the top rebar control, some XP themes look better with additional lite borders, some not.

	// optional: restore last used main window dialog
	if (thePrefs.GetRestoreLastMainWndDlg()) {
		CWnd* activate;
		switch (thePrefs.GetLastMainWndDlgID()) {
		case IDD_SERVER:
			activate = serverwnd;
			break;
		case IDD_FILES:
			activate = sharedfileswnd;
			break;
		case IDD_SEARCH:
			activate = searchwnd;
			break;
		case IDD_CHAT:
			activate = chatwnd;
			break;
		case IDD_TRANSFER:
			activate = transferwnd;
			break;
		case IDD_STATISTICS:
			activate = statisticswnd;
			break;
		case IDD_KADEMLIAWND:
			activate = kademliawnd;
			break;
		case IDD_IRC:
			activate = ircwnd;
			break;
		default:
			activate = serverwnd;
		}
		SetActiveDialog(activate);
	}
	// if still no active window, activate server window
	if (activewnd == NULL)
		SetActiveDialog(serverwnd);

	SetAllIcons();
	InitWindowStyles(this, true); // Moved down
	Localize();

	// set update interval of graphic rate display (in seconds)

	// adjust all main window sizes for toolbar height and maximize the child windows
	CRect rcClient, rcToolbar, rcStatusbar;
	GetClientRect(&rcClient);
	pwndToolbarX->GetWindowRect(&rcToolbar);

	rcStatusbar.SetRectEmpty();
	if (statusbar->m_hWnd)
		statusbar->GetWindowRect(&rcStatusbar);
	rcClient.top += rcToolbar.Height();
	rcClient.bottom -= rcStatusbar.Height();

	CWnd* const apWnds[] =
	{
		serverwnd,
		kademliawnd,
		transferwnd,
		sharedfileswnd,
		searchwnd,
		chatwnd,
		ircwnd,
		statisticswnd
	};
	for (unsigned i = 0; i < _countof(apWnds); ++i) {
		apWnds[i]->SetWindowPos(NULL, rcClient.left, rcClient.top, rcClient.Width(), rcClient.Height(), SWP_NOZORDER);
		AddOrReplaceAnchor(this, *apWnds[i], TOP_LEFT, BOTTOM_RIGHT);
	}

	ResizeSpeedGraph();

	// anchor bars
	AddOrReplaceAnchor(this, *pwndToolbarX, TOP_LEFT, TOP_RIGHT);
	if (statusbar->m_hWnd)
		AddOrReplaceAnchor(this, *statusbar, BOTTOM_LEFT, BOTTOM_RIGHT);

	statisticswnd->ShowInterval();

	// tray icon
	TraySetMinimizeToTray(thePrefs.GetMinTrayPTR());
	TrayMinimizeToTrayChange();

	ShowTransferRate(true);
	ShowPing();
	searchwnd->UpdateCatTabs();

	///////////////////////////////////////////////////////////////////////////
	// Restore saved window placement
	//
	WINDOWPLACEMENT wp;
	wp.length = (UINT)sizeof wp;
	wp = thePrefs.GetEmuleWindowPlacement();
	if (m_bStartMinimized) {
		// To avoid the window flickering during startup we try to set the proper window show state right here.
		if (*thePrefs.GetMinTrayPTR()) {
			// Minimize to System Tray
			//
			// Unfortunately this does not work. The eMule main window is a modal dialog which is invoked
			// by CDialog::DoModal which eventually calls CWnd::RunModalLoop. Look at 'MLF_SHOWONIDLE' and
			// 'bShowIdle' in the above noted functions to see why it's not possible to create the window
			// right in hidden state.

			//--- attempt #1
			//--- doesn't work at all

			//--- attempt #2
			//--- creates window flickering

			//--- attempt #3
			// Minimize the window into the task bar and later move it into the tray
			if (wp.showCmd == SW_SHOWMAXIMIZED)
				wp.flags = WPF_RESTORETOMAXIMIZED;
			wp.showCmd = SW_MINIMIZE;
			m_bStartMinimizedChecked = false;

			// to get properly restored from tray bar (after attempt #3) we have to use a patched 'restore' window cmd
			m_wpFirstRestore = thePrefs.GetEmuleWindowPlacement();
			m_wpFirstRestore.length = (UINT)sizeof m_wpFirstRestore;
			if (m_wpFirstRestore.showCmd != SW_SHOWMAXIMIZED)
				m_wpFirstRestore.showCmd = SW_SHOWNORMAL;
		}
		else {
			// Minimize to System Taskbar
			if (wp.showCmd == SW_SHOWMAXIMIZED)
				wp.flags = WPF_RESTORETOMAXIMIZED;
			wp.showCmd = SW_MINIMIZE; // Minimize window but do not activate it.
			m_bStartMinimizedChecked = true;
		}
	}
	else {
		// Allow only SW_SHOWNORMAL and SW_SHOWMAXIMIZED. Ignore SW_SHOWMINIMIZED to make sure
		// the window becomes visible.
		// If user wants SW_SHOWMINIMIZED, we already have an explicit option for this (see above).
		if (wp.showCmd != SW_SHOWMAXIMIZED)
			wp.showCmd = SW_SHOWNORMAL;
		m_bStartMinimizedChecked = true;
	}
	DeferMainWindowForStartupLoading(wp);
	if (thePrefs.IsFirstStart()) {
		// temporary disable the 'startup minimized' option, otherwise no window will be shown at all
		m_bStartMinimized = false;
		DestroySplash();
		FirstTimeWizard();
	}
	ShowStartupLoadingDialog();

	VERIFY((m_hTimer = ::SetTimer(NULL, 0, STARTUP_TIMER_INTERVAL_MS, StartupTimer)) != 0);
	if (thePrefs.GetVerbose() && !m_hTimer)
		AddDebugLogLine(true, _T("Failed to create 'startup' timer - %s"), (LPCTSTR)EscPercent(GetErrorMessage(::GetLastError())));

	VERIFY(m_pDropTarget->Register(this));

	// start aichsyncthread

	// debug info
	DebugLog(_T("Using '%s' as config directory"), (LPCTSTR)thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));

	if (!thePrefs.HasCustomTaskIconColor())
		SetTaskbarIconColor();

	// Ensure any files already queued by CSharedFileListSearchThread are processed.
	PostSharedFileListFoundFilesAsync();

	return TRUE;
}

void CemuleDlg::DoVersioncheck(bool manual)
{
	const CString strCurrentVersion = NormalizeVersionString(MOD_VERSION);
	CString strLastKnownVersionOnServer = NormalizeVersionString(thePrefs.GetLastKnownVersionOnServer());
	if (!strLastKnownVersionOnServer.IsEmpty()) {
		if (!IsRemoteVersionNewer(strLastKnownVersionOnServer, strCurrentVersion)) {
			thePrefs.SetLastKnownVersionOnServer(_T(""));
			strLastKnownVersionOnServer.Empty();
		} else if (!manual) {
			m_bNewVersionAvailable = true;
			StartTitleVersionAnimation();
			InvalidateTitleVersionFrame();
			TRACE(_T("eMuleAI version check skipped. Local=\"%s\" LastKnownServer=\"%s\"\n"), (LPCTSTR)strCurrentVersion, (LPCTSTR)strLastKnownVersionOnServer);
			return;
		}
	}

#ifndef _DEVBUILD
	if (!manual && thePrefs.GetLastVC() != 0) {
		CTime last(thePrefs.GetLastVC());
		struct tm tmTemp;
		time_t tLast = safe_mktime(last.GetLocalTm(&tmTemp));
		time_t tNow = safe_mktime(CTime::GetCurrentTime().GetLocalTm(&tmTemp));
		if (difftime(tNow, tLast) / DAY2S(1) < 1)
			return;
	}
#endif

	if (m_bVersionCheckInProgress)
		return;

	EmuleAIVersionCheckRequest* pRequest = new EmuleAIVersionCheckRequest;
	pRequest->hNotifyWnd = m_hWnd;
	pRequest->strVersionRawUrl = thePrefs.GetVersionCheckRawURL();
	pRequest->strCurrentVersion = MOD_VERSION;
	pRequest->bManual = manual;

	CWinThread* pThread = AfxBeginThread(EmuleAIVersionCheckThread, pRequest, THREAD_PRIORITY_BELOW_NORMAL);
	if (pThread == NULL) {
		delete pRequest;
		TRACE(_T("Version check thread could not be created.\n"));
		return;
	}

	AddLogLine(true, GetResString(_T("EMULE_AI_VERSION_CHECK_STARTED")));
	m_bVersionCheckInProgress = true;
}

void CemuleDlg::OpenVersionReleasesURL() const
{
	BrowserOpen(thePrefs.GetVersionCheckURL(), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
}

bool CemuleDlg::IsInitializing() const
{
	if (theApp.emuledlg->status == 255)
		return false;
	return true;
}

void CALLBACK CemuleDlg::StartupTimer(HWND /*hwnd*/, UINT /*uiMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/) noexcept
{
	// NOTE: Always handle all type of MFC exceptions in TimerProcs - otherwise we'll get mem leaks
	try {
		switch (theApp.emuledlg->status) {
		case 0:
			++theApp.emuledlg->status;
			theApp.sharedfiles->SetOutputCtrl(&theApp.emuledlg->sharedfileswnd->sharedfilesctrl);
			++theApp.emuledlg->status;
		case 1:
			break;
		case 2:
			++theApp.emuledlg->status;
			try {
				theApp.serverlist->Init();
			} catch (...) {
				ASSERT(0);
					LogError(LOG_STATUSBAR, GetResString(_T("SERVER_LIST_INIT_UNKNOWN_EXCEPTION")));
			}
			++theApp.emuledlg->status;
		case 3:
			break;
		case 4:
		{
			bool bError = false;

			try {
				theApp.BeginStartupCriticalLoads();
			}
			catch (...) {
				ASSERT(0);
				theApp.CancelStartupCriticalLoads(_T("startup-critical-load-exception"));
					LogError(LOG_STATUSBAR, GetResString(_T("STARTUP_METADATA_INIT_UNKNOWN_EXCEPTION")));
				bError = true;
			}

			if (!bError && theApp.KnownFilesReady() && theApp.sharedfiles != NULL) {
				theApp.sharedfiles->SetOutputCtrl(&theApp.emuledlg->sharedfileswnd->sharedfilesctrl);
				theApp.sharedfiles->StartDeferredStartupScan();
				if (theApp.sharedfiles->ShouldProcessFoundFilesTick())
					theApp.sharedfiles->OnSharedFilesFound();
				if (theApp.SharedFilesReady())
					theApp.BeginStartupKnown2IndexLoad();
			}

			theApp.emuledlg->RefreshActiveBulkOperationOverlays();
			if (!bError && !theApp.StartupCriticalMetadataReady() && !theApp.StartupCriticalMetadataLoadsTerminal())
				break;
			if (theApp.DownloadValidator != NULL && !theApp.DownloadValidator->IsMapInitialized())
				theApp.DownloadValidator->QueueReloadMap();

			CString strIpGuardBlockReason;
			if (!bError && theApp.emuledlg->ShouldBlockNetworkingForIpGuard(strIpGuardBlockReason))
				theApp.emuledlg->ApplyIpGuardNetworkBlock(strIpGuardBlockReason, GetResString(IP_GUARD_OVERLAY_BIND_UNAVAILABLE_KEY));
			if (!bError && !theApp.emuledlg->IsSessionNetworkBlocked() && thePrefs.IsIpGuardEnabled() && !theApp.emuledlg->m_bIpGuardStartupApproved) {
				std::vector<SIpGuardAllowedPublicIpRange> ranges;
				CString strError;
				if (!CIpGuard::TryParseAllowedPublicIpRanges(thePrefs.GetIpGuardAllowedPublicIpRanges(), ranges, strError)) {
					SIpGuardPublicIpProbeResult result;
					theApp.emuledlg->ApplyIpGuardNetworkBlock(theApp.emuledlg->FormatIpGuardPublicIpMessage(false, result));
				}
				else if (!ranges.empty()) {
					const CString strVerifying(GetResString(_T("IP_GUARD_PUBLIC_IP_VERIFYING")));
					theApp.emuledlg->ApplyIpGuardNetworkBlock(strVerifying, GetResString(IP_GUARD_OVERLAY_VERIFYING_KEY));
					if (!theApp.emuledlg->m_bIpGuardStartupProbePending && !theApp.emuledlg->m_bIpGuardRuntimeProbePending)
						theApp.emuledlg->StartIpGuardPublicIpProbe(_T("startup"));
				}
				else
					theApp.emuledlg->m_bIpGuardStartupApproved = true;
			}

			if (!bError && !theApp.emuledlg->IsSessionNetworkBlocked() && thePrefs.IsIpGuardEnabled()) {
				theApp.emuledlg->RegisterIpGuardNotifications();
				if (!theApp.emuledlg->CanUseP2PConnectionCommands()) {
					const CString strMonitorBlock(GetResString(_T("IP_GUARD_COMMANDS_BLOCKED_MONITOR")));
					theApp.emuledlg->ApplyIpGuardNetworkBlock(strMonitorBlock);
				}
			}
			if (!bError && !theApp.emuledlg->IsSessionNetworkBlocked() && thePrefs.IsVpnGuardEnabled())
				theApp.emuledlg->UpdateVpnGuardMonitor(true);

			if (!theApp.emuledlg->IsSessionNetworkBlocked()) {
				if (thePrefs.IsUPnPEnabled())
					theApp.emuledlg->StartUPnP();
				if (thePrefs.GetWSIsEnabled())
					theApp.webserver->StartServer();
				if (theApp.listensocket->GetConnectedPort() == 0 && !theApp.listensocket->StartListening(true)) {
					CString strError;
					strError.Format(GetResString(_T("MAIN_SOCKETERROR")), thePrefs.GetPort());
					LogError(LOG_STATUSBAR, _T("%s"), (LPCTSTR)strError);
					if (thePrefs.GetNotifierOnImportantError())
						theApp.emuledlg->ShowNotifier(strError, TBN_IMPORTANTEVENT);
					bError = true;
				}
				if (theApp.clientudp->GetConnectedPort() == 0 && !theApp.clientudp->Create()) {
					CString strError;
					strError.Format(GetResString(_T("MAIN_SOCKETERROR")), thePrefs.GetUDPPort());
					LogError(LOG_STATUSBAR, _T("%s"), (LPCTSTR)strError);
					if (thePrefs.GetNotifierOnImportantError())
						theApp.emuledlg->ShowNotifier(strError, TBN_IMPORTANTEVENT);
					bError = true;
				}
			}

			if (!bError) // show the success msg, only if we had no serious error
				AddLogLine(true, GetResString(_T("MAIN_READY")), (LPCTSTR)(theApp.GetAppVersion().Mid(6)));

			theApp.m_app_state = APP_STATE_RUNNING; //initialization completed
			if (!theApp.emuledlg->IsSessionNetworkBlocked()) {
				theApp.emuledlg->UpdateIpGuardMonitor(false);
				theApp.emuledlg->UpdateVpnGuardMonitor(false);
			}
			else
				theApp.emuledlg->InvalidateTitleVersionFrame();
			const bool bCanUseP2PConnectionCommands = theApp.emuledlg->CanUseP2PConnectionCommands();
			theApp.emuledlg->toolbar->EnableButton(TBBTN_CONNECT, TRUE);
			theApp.emuledlg->m_SysMenuOptions.EnableMenuItem(MP_CONNECT, MF_ENABLED);
			theApp.emuledlg->serverwnd->GetDlgItem(IDC_ED2KCONNECT)->EnableWindow(TRUE);
			theApp.emuledlg->kademliawnd->UpdateControlsState(); //application state change is not tracked - force update

			if (!bError && !theApp.emuledlg->IsSessionNetworkBlocked() && bCanUseP2PConnectionCommands)
				theApp.emuledlg->AutoConnectIfNeeded();

			// moved down
#ifdef HAVE_WIN7_SDK_H
			theApp.emuledlg->UpdateStatusBarProgress();
#endif
			++theApp.emuledlg->status;
		}
		break;
		// delay load shared files
		case 5:
		{
			const CemuleApp::SStartupMetadataLoadState knownState = theApp.GetStartupMetadataLoadState(CemuleApp::StartupMetadataKnownFiles);
			if (!theApp.KnownFilesReady() && knownState.m_eState != CemuleApp::StartupMetadataStateFailed && knownState.m_eState != CemuleApp::StartupMetadataStateCancelled) {
				theApp.emuledlg->RefreshActiveBulkOperationOverlays();
				break;
			}
			if (!theApp.KnownFilesReady()) {
				const CemuleApp::SStartupMetadataLoadState known2State = theApp.GetStartupMetadataLoadState(CemuleApp::StartupMetadataKnown2Index);
				if (known2State.m_eState == CemuleApp::StartupMetadataStateNotStarted) {
					uint64 uKnown2Token = 0;
					const LONG lKnown2Generation = theApp.BeginStartupMetadataLoad(CemuleApp::StartupMetadataKnown2Index, &uKnown2Token, _T("aich-sync-known-files-unavailable"));
					theApp.CompleteStartupMetadataLoad(CemuleApp::StartupMetadataKnown2Index, lKnown2Generation, uKnown2Token, false, ERROR_NOT_READY, _T("aich-sync-known-files-unavailable"));
				}
				theApp.emuledlg->status++;
				break;
			}

			if (theApp.sharedfiles != NULL) {
				theApp.sharedfiles->SetOutputCtrl(&theApp.emuledlg->sharedfileswnd->sharedfilesctrl);
				theApp.sharedfiles->StartDeferredStartupScan();
				if (theApp.sharedfiles->ShouldProcessFoundFilesTick())
					theApp.sharedfiles->OnSharedFilesFound();
				if (theApp.SharedFilesReady())
					theApp.BeginStartupKnown2IndexLoad();
			}

			// Shared file discovery and AICH indexing are background consistency work. Do not keep startup hidden for large shared trees.
			theApp.emuledlg->RefreshActiveBulkOperationOverlays();
			theApp.emuledlg->status++;
			break;
		}
		case 255:
			break;
		default:
			theApp.emuledlg->status = 255;
			theApp.emuledlg->AutoConnectIfNeeded();
			theApp.emuledlg->StopTimer();
		}
		}
	CATCH_DFLT_EXCEPTIONS(_T("CemuleDlg::StartupTimer"))
}

void CALLBACK CemuleDlg::MainTimer(HWND /*hwnd*/, UINT /*uiMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/) noexcept
{
	// NOTE: Always handle all type of MFC exceptions in TimerProcs - otherwise we'll get mem leaks
	try {
		if (theApp.emuledlg != NULL)
			theApp.emuledlg->CheckScheduledTasks();
	}
	CATCH_DFLT_EXCEPTIONS(_T("CemuleDlg::MainTimer"))
}

void CemuleDlg::KillMainTimer()
{
	if (m_hTimer) {
		VERIFY(::KillTimer(NULL, m_hTimer));
		m_hTimer = 0;
	}
}

void CemuleDlg::StartMainTimer()
{
	KillMainTimer();
	VERIFY((m_hTimer = ::SetTimer(NULL, 0, MAIN_TIMER_INTERVAL_MS, MainTimer)) != 0);
	if (thePrefs.GetVerbose() && !m_hTimer)
		AddDebugLogLine(true, _T("Failed to create 'main' timer - %s"), (LPCTSTR)EscPercent(GetErrorMessage(::GetLastError())));
}

void CemuleDlg::CheckScheduledTasks()
{
	if (theApp.IsClosing())
		return;

	TryRefreshStartupSearchKnownTypes();
	RefreshSearchResultsAfterStartupKnownTypes();
	theApp.StopStartupLoadWorkersIfIdle(_T("main-timer-idle"));
	if (IsInitializing())
		return;

	if (thePrefs.GetAutoIPFilterUpdate() && !CPPgSecurity::IsIPFilterDownloadActive()) {
		time_t tLast = thePrefs.GetLastIPFilterUpdate();
		int nDays = thePrefs.GetIPFilterUpdatePeriodDays();
		if (nDays < 1)
			nDays = 7;
		if (tLast == 0 || difftime(time(nullptr), tLast) >= DAY2S(nDays)) {
			const CString strUpdateUrl = CPPgSecurity::GetStoredIPFilterUpdateURL();
			if (strUpdateUrl.IsEmpty()) {
				AddLogLine(false, GetResString(_T("IPFILTER_AUTO_UPDATE_NO_URL")));
			} else {
				const CString strLastUpdate = tLast ? CTime(tLast).Format(_T("%Y-%m-%d %H:%M:%S")) : GetResString(_T("NEVER"));
				AddLogLine(true, GetResString(_T("IPFILTER_AUTO_UPDATE_STARTED")), (LPCTSTR)strLastUpdate, nDays);
				CPPgSecurity::UpdateIPFilterFromURL(strUpdateUrl, false);
			}
		}
	}

	if (thePrefs.GetAutoIPGeolocationUpdate() && !CIPGeolocation::IsIPGeolocationDownloadActive()) {
		time_t tLast = thePrefs.GetLastIPGeolocationUpdate();
		int nDays = thePrefs.GetIPGeolocationUpdatePeriodDays();
		if (nDays < 1)
			nDays = 30;
		if (tLast == 0 || difftime(time(nullptr), tLast) >= DAY2S(nDays)) {
			CString strUpdateUrl = thePrefs.GetIPGeolocationUpdateURL();
			strUpdateUrl.Trim();
			if (strUpdateUrl.IsEmpty()) {
				AddLogLine(false, GetResString(_T("IPGEOLOCATION_AUTO_UPDATE_NO_URL")));
			} else {
				const CString strLastUpdate = tLast ? CTime(tLast).Format(_T("%Y-%m-%d %H:%M:%S")) : GetResString(_T("NEVER"));
				AddLogLine(true, GetResString(_T("IPGEOLOCATION_AUTO_UPDATE_STARTED")), (LPCTSTR)strLastUpdate, nDays);
				CIPGeolocation::UpdateIPGeolocationFromURL(strUpdateUrl, false);
			}
		}
	}

	DoVersioncheck(false);
}

void CemuleDlg::StopTimer()
{
	HideStartupLoadingDialog();
	if (thePrefs.UseSplashScreen() && !m_bStartMinimized)
		ShowSplash();
	ShowMainWindowAfterStartupLoading();
	KillMainTimer();
	CheckScheduledTasks();
	StartMainTimer();
	if (theApp.DownloadValidator != NULL)
		theApp.DownloadValidator->StartDeferredBackgroundWork();

	if (!theApp.IsNetworkActivityBlockedByBind() && thePrefs.GetConnectionChecker() && theApp.ConChecker != NULL && !theApp.ConChecker->IsActive())
		theApp.ConChecker->Start();

	if (!theApp.m_strPendingLink.IsEmpty()) {
		OnWMData(NULL, (LPARAM)&theApp.sendstruct);
		theApp.m_strPendingLink.Empty();
	}
}


void CemuleDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	const UINT uSysCommand = nID & 0xFFF0;
	if (uSysCommand == SC_MAXIMIZE) {
		CTrayDialog::OnSysCommand(nID, lParam);
		if (!IsZoomed())
			ShowWindow(SW_MAXIMIZE);
		ShowTransferRate(true);
		ShowPing();
		if (transferwnd != NULL)
			transferwnd->UpdateCatTabTitles();
		return;
	}

	// System menu - Speed selector
	if (nID >= MP_QS_U10 && nID <= MP_QS_UP10) {
		QuickSpeedUpload(nID);
		return;
	}
	if (nID >= MP_QS_D10 && nID <= MP_QS_DC) {
		QuickSpeedDownload(nID);
		return;
	}
	if (nID == MP_QS_PA || nID == MP_QS_UA) {
		QuickSpeedOther(nID);
		return;
	}

	switch (nID) {
	case MP_ABOUTBOX:
		if (m_pSplashWnd != NULL)
			DestroySplash();
		ShowSplash(false);
		break;
	case MP_CONNECT:
		StartConnection(true);
		break;
	case MP_DISCONNECT:
		CloseConnection();
		break;
	default:
		CTrayDialog::OnSysCommand(nID, lParam);
	}

	switch (nID & 0xFFF0) {
	case SC_MINIMIZE:
	case MP_MINIMIZETOTRAY:
	case SC_RESTORE:
	case SC_MAXIMIZE:
		ShowTransferRate(true);
		ShowPing();
		transferwnd->UpdateCatTabTitles();
	}
}

void CemuleDlg::PostStartupMinimized()
{
	if (m_bStartupLoadingMainWindowDeferred || ShouldSuppressMainWindowForStartupLoading())
		return;

	if (!m_bStartMinimizedChecked) {
		//TODO: Use full initialized 'WINDOWPLACEMENT' and remove the 'OnCancel' call...
		// Isn't that easy. Read comments in OnInitDialog.
		m_bStartMinimizedChecked = true;
		if (m_bStartMinimized) {
			if (theApp.DidWeAutoStart() && !thePrefs.mintotray) {
				thePrefs.mintotray = true;
				MinimizeWindow();
				thePrefs.mintotray = false;
			}
			else
				MinimizeWindow();
		}
	}
}

void CemuleDlg::OnPaint()
{
	if (IsIconic()) {
		CPaintDC dc(this);

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		int cxIcon = ::GetSystemMetrics(SM_CXICON);
		int cyIcon = ::GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		dc.DrawIcon(x, y, m_hIcon);
	}
	else
		CTrayDialog::OnPaint();
}

HCURSOR CemuleDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

void CemuleDlg::OnBnClickedConnect()
{
	if (!theApp.IsConnected() && !theApp.serverconnect->IsConnecting() && !Kademlia::CKademlia::IsRunning())
		//connect if not currently connected or connecting
		StartConnection(true);
	else
		CloseConnection();
}

void CemuleDlg::OnCommandConnect()
{
	StartConnection(true);
}

void CemuleDlg::ResetServerInfo()
{
	serverwnd->servermsgbox->Reset();
}

void CemuleDlg::ResetLog()
{
	serverwnd->logbox->Reset();
}

void CemuleDlg::ResetDebugLog()
{
	serverwnd->debuglog->Reset();
}

void CemuleDlg::ResetLeecherLog()
{
	serverwnd->protectionlog->Reset();
}

void CemuleDlg::ScheduleUiLogFlush()
{
	if (theApp.IsClosing() || m_hWnd == NULL || !::IsWindow(m_hWnd) || m_queuedUiLogLines.empty())
		return;

	const bool bFlushDeferred = m_dwUiLogFlushDeferredUntil != 0 && static_cast<LONG>(m_dwUiLogFlushDeferredUntil - ::GetTickCount()) > 0;
	if (!bFlushDeferred && !m_bUiLogFlushMessagePending) {
		if (PostMessage(UWM_EMULEAI_FLUSH_UI_LOG))
			m_bUiLogFlushMessagePending = true;
	}

	if (!m_bUiLogFlushTimerActive && SetTimer(TIMER_UI_LOG_FLUSH, 20, NULL) != 0)
		m_bUiLogFlushTimerActive = true;
}

void CemuleDlg::DeferUiLogFlush(DWORD dwDelayMs)
{
	if (dwDelayMs == 0)
		return;
	const DWORD dwUntil = ::GetTickCount() + dwDelayMs;
	if (m_dwUiLogFlushDeferredUntil == 0 || static_cast<LONG>(dwUntil - m_dwUiLogFlushDeferredUntil) > 0)
		m_dwUiLogFlushDeferredUntil = dwUntil;
}

void CemuleDlg::QueueUiLogLine(const SQueuedUiLogLine& line)
{
	static const size_t MAX_QUEUED_UI_LOG_LINES = 8192;
	static const size_t UI_LOG_BACKLOG_TRACE_THRESHOLD = 4096;
	if (m_queuedUiLogLines.size() >= MAX_QUEUED_UI_LOG_LINES) {
		m_queuedUiLogLines.pop_front();
		++m_uDroppedQueuedUiLogLines;
	}
	m_queuedUiLogLines.push_back(line);
	if (m_queuedUiLogLines.size() >= UI_LOG_BACKLOG_TRACE_THRESHOLD) {
		const DWORD dwNow = ::GetTickCount();
		if (m_dwLastUiLogBacklogTrace == 0 || static_cast<DWORD>(dwNow - m_dwLastUiLogBacklogTrace) >= 5000) {
			m_dwLastUiLogBacklogTrace = dwNow;
			if (thePrefs.GetLogUiResponsivenessEvents())
				AddDebugLogLine(DLP_LOW, false, _T("UI log backlog high. queued=%Iu dropped=%Iu\n"), m_queuedUiLogLines.size(), m_uDroppedQueuedUiLogLines);
		}
	}
	ScheduleUiLogFlush();
}

bool CemuleDlg::TryAppendQueuedUiLogLine(const SQueuedUiLogLine& line)
{
	CHTRichEditCtrl* ctrl = NULL;
	switch (line.eTarget) {
		case QueuedUiLogTargetLog:
			ctrl = serverwnd ? serverwnd->logbox : NULL;
			break;
		case QueuedUiLogTargetDebug:
			ctrl = serverwnd ? serverwnd->debuglog : NULL;
			break;
		case QueuedUiLogTargetLeecher:
			ctrl = serverwnd ? serverwnd->protectionlog : NULL;
			break;
		default:
			return true;
	}

	if (theApp.IsClosing() || ctrl == NULL || ctrl->m_hWnd == NULL || !::IsWindow(ctrl->m_hWnd))
		return false;

	__try {
		ctrl->AddTyped(line.strFormattedLine, line.iLineLen, line.uFlags & LOGMSGTYPEMASK);
		return true;
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		if (thePrefs.GetLogUiResponsivenessEvents())
			AddDebugLogLine(DLP_LOW, false, _T("FlushQueuedUiLogLines: UI log append failed (code=%lx)\n"), GetExceptionCode());
		return false;
	}
}

void CemuleDlg::FlushQueuedUiLogLines()
{
	static const DWORD UI_LOG_FLUSH_BUDGET_MS = 3;
	static const DWORD UI_LOG_SINGLE_APPEND_HARD_MS = 25;
	static const UINT UI_LOG_FLUSH_MAX_LINES = 8;
	if (m_queuedUiLogLines.empty())
		return;

	if (m_dwUiLogFlushDeferredUntil != 0) {
		const DWORD dwNow = ::GetTickCount();
		if (static_cast<LONG>(m_dwUiLogFlushDeferredUntil - dwNow) > 0) {
			ScheduleUiLogFlush();
			return;
		}
		m_dwUiLogFlushDeferredUntil = 0;
	}

	const DWORD dwStart = ::GetTickCount();
	UINT uProcessed = 0;
	bool bHighlightLog = false;
	bool bHighlightDebug = false;
	bool bHighlightLeecher = false;
	bool bHasStatusText = false;
	bool bHasNotifierText = false;
	CString strStatusText;
	CString strNotifierText;

	while (!m_queuedUiLogLines.empty()) {
		SQueuedUiLogLine line = m_queuedUiLogLines.front();
		m_queuedUiLogLines.pop_front();

		const DWORD dwAppendStart = ::GetTickCount();
		const bool bAppended = TryAppendQueuedUiLogLine(line);
		const DWORD dwAppendElapsed = ::GetTickCount() - dwAppendStart;
		if (bAppended) {
			switch (line.eTarget) {
				case QueuedUiLogTargetLog:
					bHighlightLog = true;
					if (line.bNotify) {
						strNotifierText = line.strPlainText;
						bHasNotifierText = true;
					}
					break;
				case QueuedUiLogTargetDebug:
					bHighlightDebug = true;
					break;
				case QueuedUiLogTargetLeecher:
					bHighlightLeecher = true;
					break;
				default:
					break;
			}
		}

		if (line.bStatusBar) {
			strStatusText = line.strPlainText;
			bHasStatusText = true;
		}

		++uProcessed;
		if (dwAppendElapsed >= UI_LOG_SINGLE_APPEND_HARD_MS || uProcessed >= UI_LOG_FLUSH_MAX_LINES || (::GetTickCount() - dwStart) >= UI_LOG_FLUSH_BUDGET_MS)
			break;
		if (uProcessed != 0 && (::GetQueueStatus(QS_INPUT | QS_PAINT | QS_TIMER | QS_POSTMESSAGE) & 0xFFFF0000) != 0)
			break;
	}

	if (serverwnd != NULL && ::IsWindow(serverwnd->StatusSelector)) {
		const int iCurSel = serverwnd->StatusSelector.GetCurSel();
		if (bHighlightLog && iCurSel != CServerWnd::PaneLog)
			serverwnd->StatusSelector.HighlightItem(CServerWnd::PaneLog, TRUE);
		if (bHighlightDebug && iCurSel != CServerWnd::PaneVerboseLog)
			serverwnd->StatusSelector.HighlightItem(CServerWnd::PaneVerboseLog, TRUE);
		if (bHighlightLeecher && iCurSel != CServerWnd::PaneLeecherLog)
			serverwnd->StatusSelector.HighlightItem(CServerWnd::PaneLeecherLog, TRUE);
	}

	if (bHasStatusText) {
		if (statusbar && statusbar->m_hWnd && ::IsWindow(statusbar->m_hWnd) && !theApp.IsClosing())
			statusbar->SetText(strStatusText, SBarLog, 0);
		else if (!theApp.IsClosing())
			CDarkMode::MessageBox(strStatusText);
	}

	if (bHasNotifierText && status && !theApp.IsClosing())
		ShowNotifier(strNotifierText, TBN_LOG);

	if (m_queuedUiLogLines.empty() && m_uDroppedQueuedUiLogLines > 0) {
		if (thePrefs.GetLogUiResponsivenessEvents())
			AddDebugLogLine(DLP_LOW, false, _T("UI log backlog drained after dropping %Iu visible lines.\n"), m_uDroppedQueuedUiLogLines);
		m_uDroppedQueuedUiLogLines = 0;
	}

	ScheduleUiLogFlush();
}

void CemuleDlg::AddLogText(UINT uFlags, LPCTSTR pszText)
{
	if (pszText == NULL)
		return;

	if (GetCurrentThreadId() != g_uMainThreadId) {
		theApp.QueueLogLineEx(uFlags, _T("%s"), pszText);
		return;
	}

#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
	Debug(_T("%s\n"), pszText);
#endif

	const bool bVerboseSuppressed = (((uFlags & LOG_DEBUG) || (uFlags & LOG_LEECHER)) && !thePrefs.GetVerbose());
	if (bVerboseSuppressed && !(uFlags & LOG_STATUSBAR))
		return;

	TCHAR temp[1060];
	int iLen = _sntprintf(temp, _countof(temp), _T("%s: %s\r\n"), (LPCTSTR)CTime::GetCurrentTime().Format(thePrefs.GetDateTimeFormat4Log()), pszText);
	if (iLen < 0)
		return;

	SQueuedUiLogLine line;
	line.strPlainText = pszText;
	line.strFormattedLine = CString(temp, iLen);
	line.iLineLen = iLen;
	line.uFlags = uFlags;
	line.bStatusBar = (uFlags & LOG_STATUSBAR) != 0;
	line.bNotify = !(uFlags & LOG_DONTNOTIFY);

	if (bVerboseSuppressed) {
		QueueUiLogLine(line);
		return;
	}

	if (!(uFlags & LOG_DEBUG) && !(uFlags & LOG_LEECHER)) {
		line.eTarget = QueuedUiLogTargetLog;
		if (thePrefs.GetLog2Disk())
			theLog.Log(temp, iLen);
	}
	else if (thePrefs.GetVerbose() && (uFlags & LOG_LEECHER)) {
		line.eTarget = QueuedUiLogTargetLeecher;
		if (thePrefs.GetDebug2Disk())
			theVerboseLog.Log(temp, iLen);
	}
	else if (thePrefs.GetVerbose() && ((uFlags & LOG_DEBUG) || thePrefs.GetFullVerbose())) {
		line.eTarget = QueuedUiLogTargetDebug;
		if (thePrefs.GetDebug2Disk())
			theVerboseLog.Log(temp, iLen);
	}
	else if (thePrefs.GetVerbose() && thePrefs.GetDebug2Disk())
		theVerboseLog.Log(temp, iLen);

	if (line.eTarget != QueuedUiLogTargetNone || line.bStatusBar)
		QueueUiLogLine(line);
}

CString CemuleDlg::GetLastLogEntry()
{
	return serverwnd->logbox->GetLastLogEntry();
}

CString CemuleDlg::GetAllLogEntries()
{
	return serverwnd->logbox->GetAllLogEntries();
}

CString CemuleDlg::GetLastDebugLogEntry()
{
	return serverwnd->debuglog->GetLastLogEntry();
}

CString CemuleDlg::GetAllDebugLogEntries()
{
	return serverwnd->debuglog->GetAllLogEntries();
}

CString CemuleDlg::GetServerInfoText()
{
	return serverwnd->servermsgbox->GetText();
}

void CemuleDlg::AddServerMessageLine(UINT uFlags, LPCTSTR pszLine)
{
	CString strMsgLine(pszLine);
	strMsgLine += _T('\n');
	if ((uFlags & LOGMSGTYPEMASK) == LOG_INFO)
		serverwnd->servermsgbox->AppendText(strMsgLine);
	else
		serverwnd->servermsgbox->AddTyped(strMsgLine, strMsgLine.GetLength(), uFlags & LOGMSGTYPEMASK);
	if (::IsWindow(serverwnd->StatusSelector) && serverwnd->StatusSelector.GetCurSel() != CServerWnd::PaneServerInfo)
		serverwnd->StatusSelector.HighlightItem(CServerWnd::PaneServerInfo, TRUE);
}


UINT CemuleDlg::GetConnectionStateIconIndex() const
{
	//Calculate index in 'm_connicons' array
	//3 KAD states per group: "disconnected", "firewalled", "open"
	//Groups correspond to ED2K states: "disconnected", "low ID", "high ID"
	UINT idx = static_cast<UINT>(Kademlia::CKademlia::IsConnected());
	if (idx)
		idx += static_cast<UINT>(!Kademlia::CKademlia::IsFirewalled());
	if (theApp.serverconnect->IsConnected())
		idx += theApp.serverconnect->IsLowID() ? 3 : 6;
	return idx;
}

CString CemuleDlg::GetConnectionStateString()
{
	LPCTSTR ed2k;
	LPCTSTR kad;
	if (theApp.serverconnect->IsConnected())
		ed2k = _T("CONNECTED");
	else
		ed2k = theApp.serverconnect->IsConnecting() ? _T("CONNECTING") : _T("DISCONNECTED");

	if (Kademlia::CKademlia::IsConnected())
		kad = _T("CONNECTED");
	else
		kad = Kademlia::CKademlia::IsRunning() ? _T("CONNECTING") : _T("DISCONNECTED");

	CString state;
	state.Format(_T("eD2K:%s|Kad:%s"), (LPCTSTR)GetResString(ed2k), (LPCTSTR)GetResString(kad));
	return state;
}

void CemuleDlg::ShowConnectionStateIcon()
{
	// eD2K Icon
	int iED2KIcon;
	if (!theApp.serverconnect->IsConnected())
		iED2KIcon = 4; // CONTACT4
	else if (!theApp.serverconnect->IsLowID())
		iED2KIcon = 0; // CONTACT0 (High ID)
	else if (theApp.clientlist->GetEServerBuddyStatus() == Connected)
		iED2KIcon = 1; // CONTACT1 (Low ID + Buddy connected)
	else
		iED2KIcon = 2; // CONTACT2 (Low ID + No Buddy)
	statusbar->SetIcon(SBarED2K, m_contactIcons[iED2KIcon]);

	// Kad Icon
	int iKadIcon;
	if (!Kademlia::CKademlia::IsConnected())
		iKadIcon = 4; // CONTACT4
	else if (!Kademlia::CKademlia::IsFirewalled())
		iKadIcon = 0; // CONTACT0 (Open)
	else if (theApp.clientlist->GetServingBuddyStatus() == Connected)
		iKadIcon = 1; // CONTACT1 (Firewalled + Buddy connected)
	else
		iKadIcon = 2; // CONTACT2 (Firewalled + No Buddy)
	statusbar->SetIcon(SBarKad, m_contactIcons[iKadIcon]);
}


void CemuleDlg::ShowConnectionState()
{
	if (theApp.IsClosing())
		return;
	theApp.downloadqueue->OnConnectionState(theApp.IsConnected());
	serverwnd->UpdateMyInfo();
	serverwnd->UpdateControlsState();
	kademliawnd->UpdateControlsState();

	ShowConnectionStateIcon();

	LPCTSTR ed2kKey;
	if (theApp.serverconnect->IsConnected())
		ed2kKey = _T("IDS_SBAR_ED2K_CONNECTED");
	else
		ed2kKey = theApp.serverconnect->IsConnecting() ? _T("IDS_SBAR_ED2K_CONNECTING") : _T("IDS_SBAR_ED2K_DISCONNECTED");

	LPCTSTR kadKey;
	if (Kademlia::CKademlia::IsConnected())
		kadKey = _T("IDS_SBAR_KAD_CONNECTED");
	else
		kadKey = Kademlia::CKademlia::IsRunning() ? _T("IDS_SBAR_KAD_CONNECTING") : _T("IDS_SBAR_KAD_DISCONNECTED");

	statusbar->SetText(GetResString(ed2kKey), SBarED2K, 0);
	statusbar->SetText(GetResString(kadKey), SBarKad, 0);

	TBBUTTONINFO tbbi;
	tbbi.cbSize = (UINT)sizeof(TBBUTTONINFO);
	tbbi.dwMask = TBIF_IMAGE | TBIF_TEXT;

	if (theApp.IsConnected()) {
		CString strPane(GetResStringWithAccel(_T("IRC_DISCONNECT"), _T('c')));
		tbbi.iImage = 1;
		tbbi.pszText = const_cast<LPTSTR>((LPCTSTR)strPane);
		toolbar->SetButtonInfo(TBBTN_CONNECT, &tbbi);
		strPane.Remove(_T('&'));
		if (!theApp.emuledlg->m_SysMenuOptions.ModifyMenuW(MP_CONNECT, MF_STRING, MP_DISCONNECT, strPane))
			theApp.emuledlg->m_SysMenuOptions.ModifyMenuW(MP_DISCONNECT, MF_STRING, MP_DISCONNECT, strPane); //replace "Cancel" with "Disconnect"
	}
	else {
		if (theApp.serverconnect->IsConnecting() || Kademlia::CKademlia::IsRunning()) {
			CString strPane(GetResStringWithAccel(_T("CANCEL"), _T('C')));
			tbbi.iImage = 2;
			tbbi.pszText = const_cast<LPTSTR>((LPCTSTR)strPane);
			toolbar->SetButtonInfo(TBBTN_CONNECT, &tbbi);
			strPane.Remove(_T('&'));
			theApp.emuledlg->m_SysMenuOptions.ModifyMenuW(MP_CONNECT, MF_STRING, MP_DISCONNECT, strPane);
		}
		else {
			CString strPane(GetResStringWithAccel(_T("IRC_CONNECT"), _T('C')));
			tbbi.iImage = 0;
			tbbi.pszText = const_cast<LPTSTR>((LPCTSTR)strPane);
			toolbar->SetButtonInfo(TBBTN_CONNECT, &tbbi);
			strPane.Remove(_T('&'));
			theApp.emuledlg->m_SysMenuOptions.ModifyMenuW(MP_DISCONNECT, MF_STRING, MP_CONNECT, strPane);
		}
	}
	ShowUserCount();
#ifdef HAVE_WIN7_SDK_H
	UpdateThumbBarButtons();
#endif
}

void CemuleDlg::ShowUserCount()
{
	uint32 totaluser, totalfile;
	theApp.serverlist->GetUserFileStatus(totaluser, totalfile);
	CString buffer;
	if (theApp.serverconnect->IsConnected() && Kademlia::CKademlia::IsRunning() && Kademlia::CKademlia::IsConnected())
		buffer.Format(_T("%s:%s(%s)|%s:%s(%s)"), (LPCTSTR)GetResString(_T("UUSERS")), (LPCTSTR)CastItoIShort(totaluser, false, 1), (LPCTSTR)CastItoIShort(Kademlia::CKademlia::GetKademliaUsers(), false, 1), (LPCTSTR)GetResString(_T("FILES")), (LPCTSTR)CastItoIShort(totalfile, false, 1), (LPCTSTR)CastItoIShort(Kademlia::CKademlia::GetKademliaFiles(), false, 1));
	else if (theApp.serverconnect->IsConnected())
		buffer.Format(_T("%s:%s|%s:%s"), (LPCTSTR)GetResString(_T("UUSERS")), (LPCTSTR)CastItoIShort(totaluser, false, 1), (LPCTSTR)GetResString(_T("FILES")), (LPCTSTR)CastItoIShort(totalfile, false, 1));
	else if (Kademlia::CKademlia::IsRunning() && Kademlia::CKademlia::IsConnected())
		buffer.Format(_T("%s:%s|%s:%s"), (LPCTSTR)GetResString(_T("UUSERS")), (LPCTSTR)CastItoIShort(Kademlia::CKademlia::GetKademliaUsers(), false, 1), (LPCTSTR)GetResString(_T("FILES")), (LPCTSTR)CastItoIShort(Kademlia::CKademlia::GetKademliaFiles(), false, 1));
	else
		buffer.Format(_T("%s:0|%s:0"), (LPCTSTR)GetResString(_T("UUSERS")), (LPCTSTR)GetResString(_T("FILES")));
	statusbar->SetText(buffer, SBarUsers, 0);
	SetStatusBarPartsSize();
}

void CemuleDlg::ShowMessageState(UINT nIcon)
{
	m_iMsgIcon = nIcon;
	statusbar->SetIcon(SBarChatMsg, imicons[m_iMsgIcon]);
}

void CemuleDlg::ShowTransferStateIcon()
{
	int i = (m_uDownDatarate ? 1 : 0) | (m_uUpDatarate ? 2 : 0);
	statusbar->SetIcon(SBarUpDown, transicons[i]);
}

CString CemuleDlg::GetUpDatarateString(UINT uUpDatarate)
{
	if (uUpDatarate != UINT_MAX)
		m_uUpDatarate = uUpDatarate;
	else
		theApp.GetDisplayedTransferRates(m_uUpDatarate, m_uDownDatarate);
	CString szBuff;
	if (thePrefs.ShowOverhead())
		szBuff.Format(_T("%.1f (%.1f)"), m_uUpDatarate / 1024.0, theStats.GetUpDatarateOverhead() / 1024.0);
	else
		szBuff.Format(_T("%.1f"), m_uUpDatarate / 1024.0);
	return szBuff;
}

CString CemuleDlg::GetDownDatarateString(UINT uDownDatarate)
{
	if (uDownDatarate != UINT_MAX)
		m_uDownDatarate = uDownDatarate;
	else
		theApp.GetDisplayedTransferRates(m_uUpDatarate, m_uDownDatarate);
	CString szBuff;
	if (thePrefs.ShowOverhead())
		szBuff.Format(_T("%.1f (%.1f)"), m_uDownDatarate / 1024.0, theStats.GetDownDatarateOverhead() / 1024.0);
	else
		szBuff.Format(_T("%.1f"), m_uDownDatarate / 1024.0);
	return szBuff;
}

CString CemuleDlg::GetTransferRateString()
{
	CString szBuff;
	if (thePrefs.ShowOverhead())
		szBuff.Format(GetResString(_T("UPDOWN"))
			, m_uUpDatarate / 1024.0, theStats.GetUpDatarateOverhead() / 1024.0
			, m_uDownDatarate / 1024.0, theStats.GetDownDatarateOverhead() / 1024.0);
	else
		szBuff.Format(GetResString(_T("UPDOWNSMALL")), m_uUpDatarate / 1024.0, m_uDownDatarate / 1024.0);
	return szBuff;
}

void CemuleDlg::ShowTransferRate(bool bForceAll)
{
	if (bForceAll)
		m_uLastSysTrayIconCookie = SYS_TRAY_ICON_COOKIE_FORCE_UPDATE;

	theApp.GetDisplayedTransferRates(m_uUpDatarate, m_uDownDatarate);

	const CString& strTransferRate = GetTransferRateString();
	if (TrayIconVisible() || bForceAll) {
		// set tray icon
		int iDownRatePercent = (int)ceil((m_uDownDatarate / 10.24) / thePrefs.GetMaxGraphDownloadRate());
		UpdateTrayIcon(min(iDownRatePercent, 100));

		CString buffer;
		buffer.Format(_T("eMule v%s (%s)\r\n%s")
			, (LPCTSTR)theApp.GetAppVersion().Mid(6)
			, (LPCTSTR)GetResString(theApp.IsConnected() ? _T("CONNECTED") : _T("DISCONNECTED"))
			, (LPCTSTR)strTransferRate);

		TraySetToolTip(buffer);
	}

	if (IsWindowVisible() || bForceAll) {
		statusbar->SetText(strTransferRate, SBarUpDown, 0);
		ShowTransferStateIcon();
		SetStatusBarPartsSize();
	}
	if ((IsWindowVisible() || bForceAll) && thePrefs.ShowRatesOnTitle()) {
		CString szBuff;
		szBuff.Format(_T("(U:%.1f D:%.1f) %s"), m_uUpDatarate / 1024.0f, m_uDownDatarate / 1024.0f, (LPCTSTR)GetMainWindowTitleText());
		SetWindowText(szBuff);
	}
	if (m_pMiniMule && m_pMiniMule->m_hWnd && m_pMiniMule->IsWindowVisible() && !m_pMiniMule->GetAutoClose() && !m_pMiniMule->IsInInitDialog())
		m_pMiniMule->UpdateContent(m_uUpDatarate, m_uDownDatarate);
}

void CemuleDlg::ShowPing()
{
	if (IsWindowVisible() && thePrefs.IsDynUpEnabled()) {
		CurrentPingStruct lastPing = theApp.lastCommonRouteFinder->GetCurrentPing();
		CString& strState(lastPing.state);
		if (strState.IsEmpty()) {
			if (lastPing.lowest > 0 && !thePrefs.IsDynUpUseMillisecondPingTolerance())
				strState.Format(_T("%.1f | %ums | %u%%"), lastPing.currentLimit / 1024.0f, lastPing.latency, lastPing.latency * 100 / lastPing.lowest);
			else
				strState.Format(_T("%.1f | %ums"), lastPing.currentLimit / 1024.0f, lastPing.latency);
		}
		statusbar->SetText(strState, SBarUSS, 0);
		SetStatusBarPartsSize();
	}
}

void CemuleDlg::OnOK()
{
}

void CemuleDlg::OnCancel()
{
	if (!thePrefs.GetStraightWindowStyles())
		MinimizeWindow();
}

void CemuleDlg::MinimizeWindow()
{
	StopTitleVersionAnimation();
	HideTitleVersionOverlayWindow();

	if (*thePrefs.GetMinTrayPTR()) {
		TrayShow();
		ShowWindow(SW_HIDE);
	}
	else
		ShowWindow(SW_MINIMIZE);

	ShowTransferRate();
	ShowPing();
}

void CemuleDlg::SetActiveDialog(CWnd* dlg)
{
	if (dlg == activewnd)
		return;
	if (activewnd)
		activewnd->ShowWindow(SW_HIDE);

	if (!IsInitializing() && dlg == sharedfileswnd && sharedfileswnd != NULL && ::IsWindow(sharedfileswnd->sharedfilesctrl.GetSafeHwnd()))
		sharedfileswnd->sharedfilesctrl.ReloadListForActivation(LSF_SELECTION);

	dlg->ShowWindow(SW_SHOW);
	dlg->SetFocus();
	activewnd = dlg;
	RefreshActiveBulkOperationOverlays();
	int iToolbarButtonID = MapWindowToToolbarButton(dlg);
	if (iToolbarButtonID != -1)
		toolbar->PressMuleButton(iToolbarButtonID);
	if (dlg == searchwnd && searchwnd != NULL && searchwnd->m_pwndResults != NULL) {
		RefreshSearchResultsAfterStartupKnownTypes();
		searchwnd->m_pwndResults->EnsureActiveTabLoaded();
	}

	if (dlg == transferwnd) {
		if (thePrefs.ShowCatTabInfos())
			transferwnd->UpdateCatTabTitles();
	}
	else if (dlg == chatwnd)
		chatwnd->chatselector.ShowChat();
	else if (dlg == statisticswnd)
		statisticswnd->ShowStatistics();
}

void CemuleDlg::SetStatusBarPartsSize()
{
	if (statusbar == NULL || !statusbar->m_hWnd || !::IsWindow(statusbar->m_hWnd))
		return;

	CRect rect;
	statusbar->GetClientRect(&rect);
	if (rect.IsRectEmpty())
		return;

	CClientDC dc(statusbar);
	CFont* pFont = statusbar->GetFont();
	CFont* pOldFont = pFont != NULL ? dc.SelectObject(pFont) : NULL;

	int aiDynamicWidths[5] =
	{
		GetStatusBarPaneIdealWidth(*statusbar, dc, SBarUsers),
		GetStatusBarPaneIdealWidth(*statusbar, dc, SBarUpDown),
		GetStatusBarPaneIdealWidth(*statusbar, dc, SBarED2K),
		GetStatusBarPaneIdealWidth(*statusbar, dc, SBarKad),
		thePrefs.IsDynUpEnabled() ? GetStatusBarPaneIdealWidth(*statusbar, dc, SBarUSS) : 0
	};

	int aiMinimumWidths[5] =
	{
		GetStatusBarPaneMinimumWidth(*statusbar, dc, SBarUsers),
		GetStatusBarPaneMinimumWidth(*statusbar, dc, SBarUpDown),
		GetStatusBarPaneMinimumWidth(*statusbar, dc, SBarED2K),
		GetStatusBarPaneMinimumWidth(*statusbar, dc, SBarKad),
		thePrefs.IsDynUpEnabled() ? max(GetStatusBarPaneTextWidth(dc, _T("...")) + kStatusBarTextRightPadding, 1) : 0
	};

	const int iAvailableDynamicWidth = max(0, rect.Width() - GetStatusBarChatPaneWidth());
	ShrinkStatusBarPaneWidthsToFit(aiDynamicWidths, aiMinimumWidths, _countof(aiDynamicWidths), iAvailableDynamicWidth);

	int iUsedDynamicWidth = 0;
	for (int i = 0; i < _countof(aiDynamicWidths); ++i)
		iUsedDynamicWidth += aiDynamicWidths[i];

	const int iLogWidth = max(0, iAvailableDynamicWidth - iUsedDynamicWidth);

	int aiParts[7];
	aiParts[0] = iLogWidth;
	aiParts[1] = aiParts[0] + aiDynamicWidths[0];
	aiParts[2] = aiParts[1] + aiDynamicWidths[1];
	aiParts[3] = aiParts[2] + aiDynamicWidths[2];
	aiParts[4] = aiParts[3] + aiDynamicWidths[3];
	aiParts[5] = aiParts[4] + aiDynamicWidths[4];
	aiParts[6] = -1;
	statusbar->SetParts(_countof(aiParts), aiParts);

	if (pOldFont != NULL)
		dc.SelectObject(pOldFont);
}

void CemuleDlg::OnSize(UINT nType, int cx, int cy)
{
	CTrayDialog::OnSize(nType, cx, cy);
	SetStatusBarPartsSize();

	// we might receive this message during shutdown -> bad
	if (transferwnd != NULL && !theApp.IsClosing())
		transferwnd->VerifyCatTabSize();

	toolbar->Refresh();
	UpdateTitleVersionOverlayWindow();
	if (nType != SIZE_MINIMIZED && m_pSplashWnd != NULL && m_pSplashWnd->GetSafeHwnd() != NULL && ::IsWindow(m_pSplashWnd->GetSafeHwnd()))
		m_pSplashWnd->RepositionToParent();
	if (nType == SIZE_MINIMIZED) {
		StopTitleVersionAnimation();
		HideTitleVersionOverlayWindow();
	} else if (m_bNewVersionAvailable) {
		StartTitleVersionAnimation();
	}
}

void CemuleDlg::OnMoving(UINT fwSide, LPRECT pRect)
{
	CTrayDialog::OnMoving(fwSide, pRect);
	if (pRect == NULL)
		return;

	UpdateTitleVersionOverlayWindowForRect(*pRect);
	if (m_pSplashWnd != NULL && m_pSplashWnd->GetSafeHwnd() != NULL && ::IsWindow(m_pSplashWnd->GetSafeHwnd())) {
		CRect rcParent(*pRect);
		m_pSplashWnd->RepositionToParent(&rcParent);
	}
}

void CemuleDlg::OnSizing(UINT fwSide, LPRECT pRect)
{
	CTrayDialog::OnSizing(fwSide, pRect);
	if (pRect == NULL)
		return;

	UpdateTitleVersionOverlayWindowForRect(*pRect);
	if (m_pSplashWnd != NULL && m_pSplashWnd->GetSafeHwnd() != NULL && ::IsWindow(m_pSplashWnd->GetSafeHwnd())) {
		CRect rcParent(*pRect);
		m_pSplashWnd->RepositionToParent(&rcParent);
	}
}

void CemuleDlg::OnWindowPosChanging(WINDOWPOS* lpwndpos)
{
	CTrayDialog::OnWindowPosChanging(lpwndpos);
	if (lpwndpos != NULL && ShouldSuppressMainWindowForStartupLoading() && (lpwndpos->flags & SWP_SHOWWINDOW) != 0) {
		lpwndpos->flags &= ~SWP_SHOWWINDOW;
		lpwndpos->flags |= SWP_HIDEWINDOW | SWP_NOACTIVATE;
	}
}

void CemuleDlg::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
	CTrayDialog::OnWindowPosChanged(lpwndpos);
	if (lpwndpos == NULL)
		return;

	const UINT uFlags = lpwndpos->flags;
	const bool bHide = (uFlags & SWP_HIDEWINDOW) != 0;
	const bool bShow = (uFlags & SWP_SHOWWINDOW) != 0;
	const bool bMovedOrSized = ((uFlags & SWP_NOMOVE) == 0) || ((uFlags & SWP_NOSIZE) == 0);
	if (!bMovedOrSized && !bHide && !bShow)
		return;

	if (bHide) {
		StopTitleVersionAnimation();
		HideTitleVersionOverlayWindow();
		return;
	}
	if (bShow && ShouldSuppressMainWindowForStartupLoading())
		return;

	UpdateTitleVersionOverlayWindow();
	if ((bShow || bMovedOrSized) && m_pSplashWnd != NULL && m_pSplashWnd->GetSafeHwnd() != NULL && ::IsWindow(m_pSplashWnd->GetSafeHwnd()))
		m_pSplashWnd->RepositionToParent();
	if ((bShow || bMovedOrSized) && m_bNewVersionAvailable)
		StartTitleVersionAnimation();
}

void CemuleDlg::ProcessCollectionFile(const CString &strPath)
{
	if (strPath.IsEmpty() || theApp.IsClosing() || !::IsWindow(m_hWnd))
		return;

	theApp.QueueCollectionImportWorkerJob(m_hWnd, strPath);
}

LRESULT CemuleDlg::OnCollectionImportReady(WPARAM, LPARAM lParam)
{
	SCollectionImportResult *pResult = lParam != 0 ? reinterpret_cast<SCollectionImportResult*>(lParam) : theApp.PopCollectionImportResult(m_hWnd);
	if (pResult == NULL)
		return 0;

	if (!theApp.IsClosing() && pResult->bSuccess && pResult->pCollection != NULL) {
		CCollectionViewDialog dialog;
		dialog.SetCollection(pResult->pCollection);
		dialog.DoModal();
	} else {
		AddDebugLogLine(DLP_HIGH, false, _T("Collection import failed or was cancelled. stage=%s error=%lu path=%s\n"), (LPCTSTR)pResult->strStage, pResult->dwLastError, (LPCTSTR)pResult->strPath);
		if (!theApp.IsClosing())
			theApp.QueueCollectionImportFailureEvent(pResult->strPath, pResult->strStage.IsEmpty() ? _T("cancelled") : (LPCTSTR)pResult->strStage, pResult->dwLastError);
	}

	delete pResult->pCollection;
	delete pResult;
	return 0;
}

bool CemuleDlg::HasPendingStartupApplyWork() const
{
	return m_pPendingStartupDownloadsLoadResult != NULL || m_pPendingStartupKnownFilesLoadResult != NULL || m_pPendingStartupClientHistoryLoadResult != NULL || m_pPendingStartupStoredSearchesLoadResult != NULL;
}

void CemuleDlg::NotifyStartupSearchKnownTypesDependencyReady()
{
	TryRefreshStartupSearchKnownTypes();
}

void CemuleDlg::NotifyStartupSearchKnownTypesRefreshCompleted(bool bCompleted)
{
	m_bStartupSearchKnownTypesRefreshQueued = false;
	if (!bCompleted)
		return;
	m_bStartupSearchKnownTypesRefreshed = true;
	m_bStartupSearchKnownTypesReloadPending = true;
}

bool CemuleDlg::IsStartupSearchKnownTypesRefreshComplete() const
{
	return m_bStartupSearchKnownTypesRefreshed;
}

void CemuleDlg::RefreshSearchResultsAfterStartupKnownTypes()
{
	if (!theApp.IsUiThread())
		return;
	if (!m_bStartupSearchKnownTypesReloadPending || theApp.IsClosing() || IsInitializing())
		return;

	if (searchwnd == NULL || searchwnd->m_pwndResults == NULL || !::IsWindow(searchwnd->m_pwndResults->m_hWnd) || !::IsWindow(searchwnd->m_pwndResults->searchlistctrl.m_hWnd))
		return;
	if (activewnd != searchwnd)
		return;

	searchwnd->m_pwndResults->searchlistctrl.QueueDeferredReload(true, static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL), 1);
	m_bStartupSearchKnownTypesReloadPending = false;
}

void CemuleDlg::TryRefreshStartupSearchKnownTypes()
{
	if (m_bStartupSearchKnownTypesRefreshed || m_bStartupSearchKnownTypesRefreshQueued || theApp.IsClosing() || theApp.searchlist == NULL)
		return;
	if (!theApp.IsStartupMetadataDomainReady(CemuleApp::StartupMetadataDownloads) || !theApp.KnownFilesReady())
		return;
	if (thePrefs.IsStoringSearchesEnabled() && !theApp.IsStartupMetadataDomainReady(CemuleApp::StartupMetadataStoredSearches))
		return;
	if (!theApp.SharedFilesReady())
		return;
	if (theApp.searchlist->HasPendingSearchProcessing() || theApp.searchlist->HasKnownTypeRefreshWork())
		return;

	if (theApp.ExecuteSearchKnownTypeRefreshCommand(_T("startup-search-known-type-refresh"), true))
		m_bStartupSearchKnownTypesRefreshQueued = true;
}

void CemuleDlg::ScheduleStartupApplyPump()
{
	if (theApp.IsClosing() || !HasPendingStartupApplyWork() || !::IsWindow(m_hWnd) || m_bStartupApplyPumpTimerActive || m_bStartupApplyPumpPostPending)
		return;

	UINT uIntervalMs = STARTUP_APPLY_PUMP_INTERVAL_MS;
	if (m_pPendingStartupStoredSearchesLoadResult != NULL
		&& m_pPendingStartupDownloadsLoadResult == NULL
		&& m_pPendingStartupKnownFilesLoadResult == NULL
		&& m_pPendingStartupClientHistoryLoadResult == NULL) {
		const CemuleApp::SStartupMetadataLoadState downloadsState = theApp.GetStartupMetadataLoadState(CemuleApp::StartupMetadataDownloads);
		const CemuleApp::SStartupMetadataLoadState knownFilesState = theApp.GetStartupMetadataLoadState(CemuleApp::StartupMetadataKnownFiles);
		const CemuleApp::SStartupMetadataLoadState sharedRulesState = theApp.GetStartupMetadataLoadState(CemuleApp::StartupMetadataSharedRules);
		const bool bDependenciesReady = downloadsState.IsReady() && knownFilesState.IsReady() && sharedRulesState.IsReady();
		const bool bDependencyFailed = (downloadsState.IsTerminal() && !downloadsState.IsReady())
			|| (knownFilesState.IsTerminal() && !knownFilesState.IsReady())
			|| (sharedRulesState.IsTerminal() && !sharedRulesState.IsReady());
		if (!bDependenciesReady && !bDependencyFailed)
			uIntervalMs = kStartupApplyBlockedPollIntervalMs;
	}

	if (SetTimer(TIMER_STARTUP_APPLY_PUMP, uIntervalMs, NULL) != 0) {
		m_bStartupApplyPumpTimerActive = true;
		return;
	}

	if (::PostMessage(m_hWnd, UWM_EMULEAI_STARTUP_APPLY_PUMP, 0, 0)) {
		m_bStartupApplyPumpPostPending = true;
		return;
	}

	if (thePrefs.GetLogUiResponsivenessEvents())
		AddDebugLogLine(DLP_LOW, false, _T("Startup apply pump could not be scheduled.\n"));
}

void CemuleDlg::ClearStartupApplyPumpState()
{
	if (m_bStartupApplyPumpTimerActive) {
		KillTimer(TIMER_STARTUP_APPLY_PUMP);
		m_bStartupApplyPumpTimerActive = false;
	}
	m_bStartupApplyPumpPostPending = false;
	m_bStartupApplyPumpRunning = false;
	InterlockedExchange(&m_lStartupOverlayRefreshPending, 0);

	if (m_pPendingStartupDownloadsLoadResult != NULL) {
		CDownloadQueue::DeleteStartupDownloadLoadResult(static_cast<CDownloadQueue::SStartupDownloadLoadResult*>(m_pPendingStartupDownloadsLoadResult));
		m_pPendingStartupDownloadsLoadResult = NULL;
	}
	if (m_pPendingStartupKnownFilesLoadResult != NULL) {
		DeleteStartupKnownFilesLoadResult(static_cast<SStartupKnownFilesLoadResult*>(m_pPendingStartupKnownFilesLoadResult));
		m_pPendingStartupKnownFilesLoadResult = NULL;
	}
	if (m_pPendingStartupClientHistoryLoadResult != NULL) {
		DeleteStartupClientHistoryLoadResult(static_cast<SStartupClientHistoryLoadResult*>(m_pPendingStartupClientHistoryLoadResult));
		m_pPendingStartupClientHistoryLoadResult = NULL;
	}
	if (m_pPendingStartupStoredSearchesLoadResult != NULL) {
		CSearchList::DeleteStartupStoredSearchesLoadResult(static_cast<CSearchList::SStartupStoredSearchesLoadResult*>(m_pPendingStartupStoredSearchesLoadResult));
		m_pPendingStartupStoredSearchesLoadResult = NULL;
	}
}

bool CemuleDlg::ProcessStartupDownloadsApplySlice()
{
	CDownloadQueue::SStartupDownloadLoadResult *pResult = static_cast<CDownloadQueue::SStartupDownloadLoadResult*>(m_pPendingStartupDownloadsLoadResult);
	if (pResult == NULL)
		return false;

	const DWORD dwApplyStartTick = ::GetTickCount();
	if (theApp.IsClosing() || theApp.IsStartupMetadataLoadCancelled(CemuleApp::StartupMetadataDownloads, pResult->lGeneration, pResult->uCancellationToken)) {
		m_pPendingStartupDownloadsLoadResult = NULL;
		CDownloadQueue::DeleteStartupDownloadLoadResult(pResult);
		PostStartupOverlayRefresh();
		return false;
	}

	if (theApp.downloadqueue == NULL) {
		theApp.CompleteStartupMetadataLoad(CemuleApp::StartupMetadataDownloads, pResult->lGeneration, pResult->uCancellationToken, false, ERROR_INVALID_HANDLE, _T("async-downloads-load-apply"));
		m_pPendingStartupDownloadsLoadResult = NULL;
		CDownloadQueue::DeleteStartupDownloadLoadResult(pResult);
		PostStartupOverlayRefresh();
		return false;
	}

	if (pResult->bSuccess && !pResult->bApplyStarted) {
		theApp.SetStartupMetadataStateApplying(CemuleApp::StartupMetadataDownloads, pResult->lGeneration, pResult->uCancellationToken, pResult->strStage);
		pResult->bApplyStarted = true;
	}

	UINT uApplied = 0;
	INT_PTR iRemaining = 0;
	const bool bDone = theApp.downloadqueue->ApplyStartupDownloadLoadResult(pResult, kStartupDownloadsApplyFilesPerSlice, uApplied, iRemaining);
	TraceStartupApplySliceIfHardExceeded(dwApplyStartTick, _T("startup-apply-downloads"), uApplied, iRemaining);
	PostStartupOverlayRefresh();
	if (!bDone)
		return true;

	if (pResult->bSuccess) {
		theApp.CompleteStartupMetadataLoad(CemuleApp::StartupMetadataDownloads, pResult->lGeneration, pResult->uCancellationToken, true, 0, pResult->strStage);
		TryRefreshStartupSearchKnownTypes();
	}
	else
		theApp.CompleteStartupMetadataLoad(CemuleApp::StartupMetadataDownloads, pResult->lGeneration, pResult->uCancellationToken, false, pResult->dwLastError, pResult->strStage.IsEmpty() ? _T("async-downloads-load-failed") : (LPCTSTR)pResult->strStage);

	m_pPendingStartupDownloadsLoadResult = NULL;
	CDownloadQueue::DeleteStartupDownloadLoadResult(pResult);
	PostStartupOverlayRefresh();
	return false;
}

bool CemuleDlg::ProcessStartupStoredSearchesApplySlice()
{
	CSearchList::SStartupStoredSearchesLoadResult *pResult = static_cast<CSearchList::SStartupStoredSearchesLoadResult*>(m_pPendingStartupStoredSearchesLoadResult);
	if (pResult == NULL)
		return false;

	const CemuleApp::SStartupMetadataLoadState downloadsState = theApp.GetStartupMetadataLoadState(CemuleApp::StartupMetadataDownloads);
	const CemuleApp::SStartupMetadataLoadState knownFilesState = theApp.GetStartupMetadataLoadState(CemuleApp::StartupMetadataKnownFiles);
	const CemuleApp::SStartupMetadataLoadState sharedRulesState = theApp.GetStartupMetadataLoadState(CemuleApp::StartupMetadataSharedRules);
	const bool bDependencyFailed = (downloadsState.IsTerminal() && !downloadsState.IsReady())
		|| (knownFilesState.IsTerminal() && !knownFilesState.IsReady())
		|| (sharedRulesState.IsTerminal() && !sharedRulesState.IsReady());
	if (bDependencyFailed) {
		theApp.CompleteStartupMetadataLoad(CemuleApp::StartupMetadataStoredSearches, pResult->lGeneration, pResult->uCancellationToken, false, ERROR_NOT_READY, _T("stored-searches-dependency-unavailable"));
		if (theApp.searchlist != NULL)
			theApp.searchlist->CancelStartupLoad();
		m_pPendingStartupStoredSearchesLoadResult = NULL;
		CSearchList::DeleteStartupStoredSearchesLoadResult(pResult);
		PostStartupOverlayRefresh();
		return false;
	}
	if (!downloadsState.IsReady() || !knownFilesState.IsReady() || !sharedRulesState.IsReady())
		return true;

	const DWORD dwApplyStartTick = ::GetTickCount();
	if (theApp.IsClosing() || theApp.IsStartupMetadataLoadCancelled(CemuleApp::StartupMetadataStoredSearches, pResult->lGeneration, pResult->uCancellationToken)) {
		m_pPendingStartupStoredSearchesLoadResult = NULL;
		CSearchList::DeleteStartupStoredSearchesLoadResult(pResult);
		PostStartupOverlayRefresh();
		return false;
	}

	if (theApp.searchlist == NULL) {
		theApp.CompleteStartupMetadataLoad(CemuleApp::StartupMetadataStoredSearches, pResult->lGeneration, pResult->uCancellationToken, false, ERROR_INVALID_HANDLE, _T("async-stored-searches-load-apply"));
		m_pPendingStartupStoredSearchesLoadResult = NULL;
		CSearchList::DeleteStartupStoredSearchesLoadResult(pResult);
		PostStartupOverlayRefresh();
		return false;
	}

	UINT uApplied = 0;
	INT_PTR iRemaining = 0;
	const bool bDone = theApp.searchlist->ApplyStartupStoredSearchesLoadResult(pResult, kStartupStoredSearchesApplyFilesPerSlice, uApplied, iRemaining);
	TraceStartupApplySliceIfHardExceeded(dwApplyStartTick, _T("startup-apply-stored-searches"), uApplied, iRemaining);
	PostStartupOverlayRefresh();
	if (!bDone)
		return true;

	m_pPendingStartupStoredSearchesLoadResult = NULL;
	TryRefreshStartupSearchKnownTypes();
	CSearchList::DeleteStartupStoredSearchesLoadResult(pResult);
	PostStartupOverlayRefresh();
	return false;
}

bool CemuleDlg::ProcessStartupClientHistoryApplySlice()
{
	SStartupClientHistoryLoadResult *pResult = static_cast<SStartupClientHistoryLoadResult*>(m_pPendingStartupClientHistoryLoadResult);
	if (pResult == NULL)
		return false;

	const DWORD dwApplyStartTick = ::GetTickCount();
	if (theApp.IsClosing() || theApp.IsStartupMetadataLoadCancelled(CemuleApp::StartupMetadataClientHistory, pResult->lGeneration, pResult->uCancellationToken)) {
		m_pPendingStartupClientHistoryLoadResult = NULL;
		DeleteStartupClientHistoryLoadResult(pResult);
		PostStartupOverlayRefresh();
		return false;
	}

	if (pResult->bSuccess && theApp.clientlist != NULL) {
		if (!pResult->bApplyStarted) {
			theApp.SetStartupMetadataStateApplying(CemuleApp::StartupMetadataClientHistory, pResult->lGeneration, pResult->uCancellationToken, pResult->strStage);
			pResult->bApplyStarted = true;
		}

		if (pResult->pRecords != NULL) {
			const size_t uPreviousRecord = pResult->uNextRecord;
			if (!theApp.clientlist->ApplyStartupClientHistoryLoadChunk(pResult->pRecords, pResult->uNextRecord, kStartupClientHistoryApplyRecordsPerSlice)) {
				CClientList::DeleteStartupClientHistoryRecords(pResult->pRecords);
				pResult->pRecords = NULL;
				theApp.CompleteStartupMetadataLoad(CemuleApp::StartupMetadataClientHistory, pResult->lGeneration, pResult->uCancellationToken, false, ERROR_READ_FAULT, _T("async-client-history-load-apply"));
				m_pPendingStartupClientHistoryLoadResult = NULL;
				delete pResult;
				PostStartupOverlayRefresh();
				return false;
			}

			UINT uApplied = 0;
			if (pResult->uNextRecord > uPreviousRecord)
				uApplied = static_cast<UINT>(pResult->uNextRecord - uPreviousRecord);
			TraceStartupApplySliceIfHardExceeded(dwApplyStartTick, _T("startup-apply-client-history"), uApplied, static_cast<INT_PTR>(pResult->pRecords->size() - pResult->uNextRecord));
			PostStartupOverlayRefresh();
			if (pResult->uNextRecord < pResult->pRecords->size())
				return true;

			CClientList::DeleteStartupClientHistoryRecords(pResult->pRecords);
			pResult->pRecords = NULL;
			return true;
		}

		UINT uCompletionApplied = 0;
		INT_PTR iCompletionRemaining = 0;
		const bool bCompletionDone = ApplyStartupClientHistoryCompletionSlice(this, pResult, uCompletionApplied, iCompletionRemaining);
		TraceStartupApplySliceIfHardExceeded(dwApplyStartTick, _T("startup-complete-client-history"), uCompletionApplied, iCompletionRemaining);
		PostStartupOverlayRefresh();
		if (!bCompletionDone)
			return true;

		theApp.CompleteStartupMetadataLoad(CemuleApp::StartupMetadataClientHistory, pResult->lGeneration, pResult->uCancellationToken, true, 0, pResult->strStage);
	}
	else {
		CClientList::DeleteStartupClientHistoryRecords(pResult->pRecords);
		pResult->pRecords = NULL;
		theApp.CompleteStartupMetadataLoad(CemuleApp::StartupMetadataClientHistory, pResult->lGeneration, pResult->uCancellationToken, false, pResult->dwLastError, pResult->strStage.IsEmpty() ? _T("async-client-history-load-failed") : (LPCTSTR)pResult->strStage);
	}

	m_pPendingStartupClientHistoryLoadResult = NULL;
	delete pResult;
	PostStartupOverlayRefresh();
	return false;
}

bool CemuleDlg::ProcessStartupKnownFilesApplySlice()
{
	SStartupKnownFilesLoadResult *pResult = static_cast<SStartupKnownFilesLoadResult*>(m_pPendingStartupKnownFilesLoadResult);
	if (pResult == NULL)
		return false;

	const DWORD dwApplyStartTick = ::GetTickCount();
	if (theApp.IsClosing() || theApp.IsStartupMetadataLoadCancelled(CemuleApp::StartupMetadataKnownFiles, pResult->lGeneration, pResult->uCancellationToken)) {
		m_pPendingStartupKnownFilesLoadResult = NULL;
		DeleteStartupKnownFilesLoadResult(pResult);
		PostStartupOverlayRefresh();
		return false;
	}

	if (pResult->bSuccess && theApp.knownfiles != NULL) {
		if (!pResult->bApplyStarted) {
			theApp.SetStartupMetadataStateApplying(CemuleApp::StartupMetadataKnownFiles, pResult->lGeneration, pResult->uCancellationToken, pResult->strStage);
			pResult->bApplyStarted = true;
		}

		if (!pResult->bKnownRecordsParsed) {
			const size_t uPreviousKnownRecord = pResult->uNextKnownRecord;
			if (!theApp.knownfiles->ParseStartupKnownFilesLoadChunk(pResult->pKnownRecords, pResult->vecParsedKnownFiles, &pResult->vecParsedKnownFileWorkUnits, &pResult->uKnownFileWorkUnitsTotal, pResult->uNextKnownRecord, kStartupKnownFilesParseRecordsPerSlice)) {
				theApp.CompleteStartupMetadataLoad(CemuleApp::StartupMetadataKnownFiles, pResult->lGeneration, pResult->uCancellationToken, false, ERROR_INVALID_DATA, _T("async-known-files-load-parse"));
				if (theApp.sharedfiles != NULL)
					theApp.sharedfiles->StartDeferredStartupScanAfterKnownFilesFailure();
				m_pPendingStartupKnownFilesLoadResult = NULL;
				DeleteStartupKnownFilesLoadResult(pResult);
				PostStartupOverlayRefresh();
				return false;
			}

			UINT uParsed = 0;
			if (pResult->uNextKnownRecord > uPreviousKnownRecord)
				uParsed = static_cast<UINT>(pResult->uNextKnownRecord - uPreviousKnownRecord);
			const size_t uKnownRecordCount = pResult->pKnownRecords != NULL ? pResult->pKnownRecords->size() : 0;
			TraceStartupApplySliceIfHardExceeded(dwApplyStartTick, _T("startup-parse-known-files"), uParsed, static_cast<INT_PTR>(uKnownRecordCount - min(pResult->uNextKnownRecord, uKnownRecordCount)));
			PostStartupOverlayRefresh();
			if (pResult->pKnownRecords != NULL && pResult->uNextKnownRecord < pResult->pKnownRecords->size())
				return true;

			CKnownFileList::DeleteStartupKnownFilesRecords(pResult->pKnownRecords, NULL);
			pResult->pKnownRecords = NULL;
			pResult->bKnownRecordsParsed = true;
		}
	}

	if (pResult->bSuccess && pResult->bKnownRecordsParsed && theApp.knownfiles != NULL) {
		if (!pResult->bCompletionStarted) {
			const size_t uPreviousParsedFile = pResult->uNextParsedFile;
			if (!theApp.knownfiles->AttachStartupKnownFilesLoadChunk(pResult->vecParsedKnownFiles, pResult->uNextParsedFile, kStartupKnownFilesAttachRecordsPerSlice, &pResult->vecParsedKnownFileWorkUnits, &pResult->uKnownFileWorkUnitsApplied)) {
				theApp.CompleteStartupMetadataLoad(CemuleApp::StartupMetadataKnownFiles, pResult->lGeneration, pResult->uCancellationToken, false, ERROR_READ_FAULT, _T("async-known-files-load-attach"));
				if (theApp.sharedfiles != NULL)
					theApp.sharedfiles->StartDeferredStartupScanAfterKnownFilesFailure();
				m_pPendingStartupKnownFilesLoadResult = NULL;
				DeleteStartupKnownFilesLoadResult(pResult);
				PostStartupOverlayRefresh();
				return false;
			}

			UINT uApplied = 0;
			if (pResult->uNextParsedFile > uPreviousParsedFile)
				uApplied = static_cast<UINT>(pResult->uNextParsedFile - uPreviousParsedFile);
			TraceStartupApplySliceIfHardExceeded(dwApplyStartTick, _T("startup-apply-known-files"), uApplied, static_cast<INT_PTR>(pResult->vecParsedKnownFiles.size() - pResult->uNextParsedFile));
			PostStartupOverlayRefresh();
			if (pResult->uNextParsedFile < pResult->vecParsedKnownFiles.size())
				return true;

			CKnownFileList::DeleteStartupKnownFilesParsedFiles(pResult->vecParsedKnownFiles);
			pResult->vecParsedKnownFileWorkUnits.clear();
		}

		UINT uCompletionApplied = 0;
		INT_PTR iCompletionRemaining = 0;
		if (!ApplyStartupKnownFilesCompletionSlice(pResult, uCompletionApplied, iCompletionRemaining)) {
			theApp.CompleteStartupMetadataLoad(CemuleApp::StartupMetadataKnownFiles, pResult->lGeneration, pResult->uCancellationToken, false, ERROR_READ_FAULT, _T("async-known-files-load-complete"));
			if (theApp.sharedfiles != NULL)
				theApp.sharedfiles->StartDeferredStartupScanAfterKnownFilesFailure();
			m_pPendingStartupKnownFilesLoadResult = NULL;
			DeleteStartupKnownFilesLoadResult(pResult);
			PostStartupOverlayRefresh();
			return false;
		}

		TraceStartupApplySliceIfHardExceeded(dwApplyStartTick, _T("startup-complete-known-files"), uCompletionApplied, iCompletionRemaining);
		PostStartupOverlayRefresh();
		if (iCompletionRemaining > 0)
			return true;

		CKnownFileList::DeleteStartupKnownFilesRecords(NULL, pResult->pCancelledRecords);
		pResult->pCancelledRecords = NULL;
		theApp.CompleteStartupMetadataLoad(CemuleApp::StartupMetadataKnownFiles, pResult->lGeneration, pResult->uCancellationToken, true, 0, pResult->strStage);
		TryRefreshStartupSearchKnownTypes();
		if (sharedfileswnd != NULL && ::IsWindow(sharedfileswnd->m_hWnd))
			sharedfileswnd->PostAutoReloadSharedFilesAsync(0);
	}
	else {
		theApp.CompleteStartupMetadataLoad(CemuleApp::StartupMetadataKnownFiles, pResult->lGeneration, pResult->uCancellationToken, false, pResult->dwLastError, pResult->strStage.IsEmpty() ? _T("async-known-files-load-failed") : (LPCTSTR)pResult->strStage);
		if (theApp.sharedfiles != NULL)
			theApp.sharedfiles->StartDeferredStartupScanAfterKnownFilesFailure();
	}

	m_pPendingStartupKnownFilesLoadResult = NULL;
	DeleteStartupKnownFilesLoadResult(pResult);
	PostStartupOverlayRefresh();
	return false;
}

bool CemuleDlg::ProcessStartupApplyPump()
{
	if (m_bStartupApplyPumpRunning)
		return HasPendingStartupApplyWork();

	m_bStartupApplyPumpRunning = true;
	const DWORD dwPumpStartTick = ::GetTickCount();
	while (!theApp.IsClosing() && HasPendingStartupApplyWork()) {
		bool bProcessed = false;
		for (UINT uProbe = 0; uProbe < StartupApplyPumpDomainCount; ++uProbe) {
			const UINT uDomain = (m_uStartupApplyPumpNextDomain + uProbe) % StartupApplyPumpDomainCount;
			switch (uDomain) {
			case StartupApplyPumpDomainDownloads:
				if (m_pPendingStartupDownloadsLoadResult != NULL) {
					ProcessStartupDownloadsApplySlice();
					bProcessed = true;
				}
				break;
			case StartupApplyPumpDomainKnownFiles:
				if (m_pPendingStartupKnownFilesLoadResult != NULL) {
					ProcessStartupKnownFilesApplySlice();
					bProcessed = true;
				}
				break;
			case StartupApplyPumpDomainClientHistory:
				if (m_pPendingStartupClientHistoryLoadResult != NULL) {
					ProcessStartupClientHistoryApplySlice();
					bProcessed = true;
				}
				break;
			case StartupApplyPumpDomainStoredSearches:
				if (m_pPendingStartupStoredSearchesLoadResult != NULL) {
					const CemuleApp::SStartupMetadataLoadState downloadsState = theApp.GetStartupMetadataLoadState(CemuleApp::StartupMetadataDownloads);
					const CemuleApp::SStartupMetadataLoadState knownFilesState = theApp.GetStartupMetadataLoadState(CemuleApp::StartupMetadataKnownFiles);
					const CemuleApp::SStartupMetadataLoadState sharedRulesState = theApp.GetStartupMetadataLoadState(CemuleApp::StartupMetadataSharedRules);
					const bool bDependenciesReady = downloadsState.IsReady() && knownFilesState.IsReady() && sharedRulesState.IsReady();
					const bool bDependencyFailed = (downloadsState.IsTerminal() && !downloadsState.IsReady())
						|| (knownFilesState.IsTerminal() && !knownFilesState.IsReady())
						|| (sharedRulesState.IsTerminal() && !sharedRulesState.IsReady());
					if (bDependenciesReady || bDependencyFailed) {
						ProcessStartupStoredSearchesApplySlice();
						bProcessed = true;
					}
				}
				break;
			}

			if (bProcessed) {
				m_uStartupApplyPumpNextDomain = (uDomain + 1) % StartupApplyPumpDomainCount;
				break;
			}
		}

		if (!bProcessed || ShouldYieldStartupApplyForUi() || theApp.IsTimeBudgetExceeded(dwPumpStartTick, CemuleApp::TimeBudgetStartupApply))
			break;
	}

	m_bStartupApplyPumpRunning = false;
	if (theApp.IsClosing()) {
		ClearStartupApplyPumpState();
		return false;
	}

	const bool bHasMore = HasPendingStartupApplyWork();
	if (bHasMore)
		ScheduleStartupApplyPump();
	return bHasMore;
}

LRESULT CemuleDlg::OnStartupApplyPump(WPARAM, LPARAM)
{
	m_bStartupApplyPumpPostPending = false;
	if (m_bStartupApplyPumpTimerActive) {
		KillTimer(TIMER_STARTUP_APPLY_PUMP);
		m_bStartupApplyPumpTimerActive = false;
	}
	ProcessStartupApplyPump();
	return 0;
}

LRESULT CemuleDlg::OnStartupDownloadsLoadReady(WPARAM, LPARAM lParam)
{
	CDownloadQueue::SStartupDownloadLoadResult *pResult = reinterpret_cast<CDownloadQueue::SStartupDownloadLoadResult*>(lParam);
	if (pResult == NULL)
		return 0;
	if (m_pPendingStartupDownloadsLoadResult != NULL) {
		if (thePrefs.GetLogUiResponsivenessEvents())
			AddDebugLogLine(DLP_LOW, false, _T("Duplicate startup downloads apply result dropped.\n"));
		CDownloadQueue::DeleteStartupDownloadLoadResult(pResult);
		ScheduleStartupApplyPump();
		return 0;
	}
	m_pPendingStartupDownloadsLoadResult = pResult;
	ScheduleStartupApplyPump();
	return 0;
}

LRESULT CemuleDlg::OnStartupStoredSearchesLoadReady(WPARAM, LPARAM lParam)
{
	CSearchList::SStartupStoredSearchesLoadResult *pResult = reinterpret_cast<CSearchList::SStartupStoredSearchesLoadResult*>(lParam);
	if (pResult == NULL)
		return 0;
	if (m_pPendingStartupStoredSearchesLoadResult != NULL) {
		if (thePrefs.GetLogUiResponsivenessEvents())
			AddDebugLogLine(DLP_LOW, false, _T("Duplicate startup stored searches apply result dropped.\n"));
		CSearchList::DeleteStartupStoredSearchesLoadResult(pResult);
		ScheduleStartupApplyPump();
		return 0;
	}
	m_pPendingStartupStoredSearchesLoadResult = pResult;
	ScheduleStartupApplyPump();
	return 0;
}

LRESULT CemuleDlg::OnStartupClientHistoryLoadReady(WPARAM, LPARAM lParam)
{
	SStartupClientHistoryLoadResult *pResult = reinterpret_cast<SStartupClientHistoryLoadResult*>(lParam);
	if (pResult == NULL)
		return 0;
	if (m_pPendingStartupClientHistoryLoadResult != NULL) {
		if (thePrefs.GetLogUiResponsivenessEvents())
			AddDebugLogLine(DLP_LOW, false, _T("Duplicate startup client history apply result dropped.\n"));
		DeleteStartupClientHistoryLoadResult(pResult);
		ScheduleStartupApplyPump();
		return 0;
	}
	m_pPendingStartupClientHistoryLoadResult = pResult;
	ScheduleStartupApplyPump();
	return 0;
}

LRESULT CemuleDlg::OnStartupKnownFilesLoadReady(WPARAM, LPARAM lParam)
{
	SStartupKnownFilesLoadResult *pResult = reinterpret_cast<SStartupKnownFilesLoadResult*>(lParam);
	if (pResult == NULL)
		return 0;
	if (m_pPendingStartupKnownFilesLoadResult != NULL) {
		if (thePrefs.GetLogUiResponsivenessEvents())
			AddDebugLogLine(DLP_LOW, false, _T("Duplicate startup known files apply result dropped.\n"));
		DeleteStartupKnownFilesLoadResult(pResult);
		ScheduleStartupApplyPump();
		return 0;
	}
	m_pPendingStartupKnownFilesLoadResult = pResult;
	ScheduleStartupApplyPump();
	return 0;
}

void CemuleDlg::ProcessED2KLink(LPCTSTR pszData)
{
	try {
		CString link(pszData);
		link.Replace(_T("%7c"), _T("|"));
		CED2KLink* pLink = CED2KLink::CreateLinkFromUrl(OptUtf8ToStr(URLDecode(link)));
		ASSERT(pLink);
		switch (pLink->GetKind()) {
		case CED2KLink::kFile:
		{
			CED2KFileLink* pFileLink = pLink->GetFileLink();
			ASSERT(pFileLink);
			theApp.downloadqueue->AddFileLinkToDownload(*pFileLink, searchwnd->GetSelectedCat());
		}
		break;
		case CED2KLink::kServerList:
		{
			CED2KServerListLink* pListLink = pLink->GetServerListLink();
			ASSERT(pListLink);
			const CString& strAddress(pListLink->GetAddress());
			if (!strAddress.IsEmpty())
				serverwnd->UpdateServerMetFromURL(strAddress);
		}
		break;
		case CED2KLink::kNodesList:
		{
			const CED2KNodesListLink* pListLink = pLink->GetNodesListLink();
			ASSERT(pListLink);
			const CString& strAddress(pListLink->GetAddress());
			// Because the nodes.dat is vital for kad and its routing and doesn't need to be
			// updated in general, we request a confirm to avoid accidental / malicious updating
			// of this file. This is a bit inconsistent as the same kinda applies to the server.met,
			// but those require more updates and are easier to understand
			if (!strAddress.IsEmpty()) {
				CString strConfirm;
				strConfirm.Format(GetResString(_T("CONFIRMNODESDOWNLOAD")), (LPCTSTR)strAddress);
				if (CDarkMode::MessageBox(strConfirm, MB_YESNO | MB_ICONQUESTION, 0) == IDYES)
					kademliawnd->UpdateNodesDatFromURL(strAddress);
			}
		}
		break;
		case CED2KLink::kServer:
		{
			CED2KServerLink* pSrvLink = pLink->GetServerLink();
			ASSERT(pSrvLink);
			CServer* pSrv = new CServer(pSrvLink->GetPort(), pSrvLink->GetAddress());
			ASSERT(pSrv);
			CString defName;
			pSrvLink->GetDefaultName(defName);
			pSrv->SetListName(defName);

			// Barry - Default all new servers to high priority
			if (thePrefs.GetManualAddedServersHighPriority())
				pSrv->SetPreference(SRV_PR_HIGH);

			if (!serverwnd->serverlistctrl.AddServer(pSrv, true))
				delete pSrv;
			else
				AddLogLine(true, GetResString(_T("SERVERADDED")), (LPCTSTR)EscPercent(pSrv->GetListName()));
		}
		break;
		case CED2KLink::kSearch:
		{
			CED2KSearchLink* pListLink = pLink->GetSearchLink();
			ASSERT(pListLink);
			SetActiveDialog(searchwnd);
			searchwnd->ProcessEd2kSearchLinkRequest(pListLink->GetSearchTerm());
		}
		case CED2KLink::kFriend:
		{
			// Better with dynamic_cast, but no RTTI enabled in the project
			CED2KFriendLink* pFriendLink = static_cast<CED2KFriendLink*>(pLink);
			uchar userHash[16];
			pFriendLink->GetUserHash(userHash);

			if (!theApp.friendlist->IsAlreadyFriend(userHash))
				theApp.friendlist->AddFriend(userHash, 0U, CAddress(), 0U, 0U, pFriendLink->GetUserName(), 1U);
			else
			{
				CString msg;
				msg.Format(GetResString(_T("USER_ALREADY_FRIEND")), pFriendLink->GetUserName());
				AddLogLine(true, (LPCTSTR)EscPercent(msg));
			}
		}
		break;
		case CED2KLink::kFriendList:
		{
			// Better with dynamic_cast, but no RTTI enabled in the project
			CED2KFriendListLink* pFrndLstLink = static_cast<CED2KFriendListLink*>(pLink);
			CString sAddress = pFrndLstLink->GetAddress();
			if (!sAddress.IsEmpty())
				this->chatwnd->UpdateEmfriendsMetFromURL(sAddress);
		}
		break;
		default:
			break;
		}
		delete pLink;
	}
	catch (const CString& strError) {
		LogWarning(LOG_STATUSBAR, _T("%s - %s"), (LPCTSTR)GetResString(_T("LINKNOTADDED")), (LPCTSTR)strError);
	}
	catch (CException *e) {
		e->Delete();
		LogWarning(LOG_STATUSBAR, GetResString(_T("LINKNOTADDED")));
	}
	catch (...) {
		LogWarning(LOG_STATUSBAR, GetResString(_T("LINKNOTADDED")));
	}
}

LRESULT CemuleDlg::OnWMData(WPARAM, LPARAM lParam)
{
	PCOPYDATASTRUCT data = (PCOPYDATASTRUCT)lParam;
	ULONG_PTR op = data->dwData;
	if ((op == OP_ED2KLINK && thePrefs.IsBringToFront()) || op == OP_COLLECTION) {
		if (IsIconic())
			ShowWindow(SW_SHOWNORMAL);
		else
			RestoreWindow();
		FlashWindow(TRUE);
	}
	switch (op) {
	case OP_ED2KLINK:
		theApp.ProcessED2KLinksChunked((LPCTSTR)data->lpData);
		break;
	case OP_COLLECTION:
		theApp.ExecuteCollectionImportCommand(CString((LPCTSTR)data->lpData));
		break;
	case OP_CLCOMMAND:
	{
		// command line command received
		CString clcommand((LPCTSTR)data->lpData);
		clcommand.MakeLower();
			AddLogLine(true, GetResString(_T("CLI_COMMAND_LOG")), (LPCTSTR)EscPercent(clcommand));

		if (clcommand.Left(15) == _T("emule-ai-toast:"))
			RestoreWindow();
		else if (clcommand == _T("connect"))
			StartConnection();
		else if (clcommand == _T("disconnect"))
			theApp.serverconnect->Disconnect();
		else if (clcommand == _T("exit")) {
			theApp.m_app_state = APP_STATE_SHUTTINGDOWN; // do no ask to close
			OnClose();
		}
		else if (clcommand == _T("help") || clcommand == _T("/?"))
			; // show usage
		else if (clcommand.Left(7) == _T("limits=") && clcommand.GetLength() > 8) {
			clcommand.Delete(0, 7);
			int pos = clcommand.Find(_T(','));
			if (pos > 0) {
				if (clcommand[pos + 1])
					thePrefs.SetMaxDownload(_tstoi(CPTR(clcommand, pos + 1)));
				clcommand.Truncate(pos);
			}
			if (!clcommand.IsEmpty())
				thePrefs.SetMaxUpload(_tstoi(clcommand));
		}
		else if (clcommand == _T("reloadipf"))
			theApp.ipfilter->LoadFromDefaultFile();
		else if (clcommand == _T("restore"))
			RestoreWindow();
		else if (clcommand == _T("resume"))
			theApp.downloadqueue->StartNextFile();
		else if (clcommand == _T("status")) {
			FILE* file = _tfsopen(thePrefs.GetMuleDirectory(EMULE_CONFIGBASEDIR) + _T("status.log"), _T("wt"), _SH_DENYWR);
			if (file) {
				LPCTSTR uid;
				if (theApp.serverconnect->IsConnected())
					uid = _T("CONNECTED");
				else if (theApp.serverconnect->IsConnecting())
					uid = _T("CONNECTING");
				else
					uid = _T("DISCONNECTED");
				_ftprintf(file, _T("%s\n"), (LPCTSTR)GetResString(uid));
				CString strUpDown;
				UINT uUploadDatarate = 0;
				UINT uDownloadDatarate = 0;
				theApp.GetDisplayedTransferRates(uUploadDatarate, uDownloadDatarate);
				strUpDown.Format(GetResString(_T("UPDOWNSMALL")), uUploadDatarate / 1024.0f, uDownloadDatarate / 1024.0f);
				_ftprintf(file, _T("%s"), (LPCTSTR)strUpDown);
				// next string (getTextList) is already prefixed with '\n'!
				_ftprintf(file, _T("%s\n"), (LPCTSTR)transferwnd->GetDownloadList()->getTextList());

				fclose(file);
			}
		}
		//else show "unknown command"; Or "usage"
	}
	}
	return TRUE;
}

namespace
{
	CPartFile* ResolvePartFileToken(CPartFile* pPartFileToken, DWORD dwRuntimeID, const uchar* pucFileHash)
	{
		if (pPartFileToken == NULL || dwRuntimeID == 0 || pucFileHash == NULL || theApp.IsClosing() || theApp.downloadqueue == NULL)
			return NULL;

		CPartFile* pPartFile = theApp.downloadqueue->GetFileByID(pucFileHash);
		if (pPartFile != NULL && pPartFile == pPartFileToken && pPartFile->GetRuntimeID() == dwRuntimeID)
			return pPartFile;
		return NULL;
	}

	CPartFile* ResolvePartFileHashTarget(const PartFileHash_Struct* pHash)
	{
		if (pHash == NULL)
			return NULL;
		return ResolvePartFileToken(pHash->pPartFile, pHash->dwRuntimeID, pHash->abyFileHash);
	}

	void DeleteSharedFileHashResult(SharedFileHashResult_Struct* pHash)
	{
		if (pHash != NULL) {
			delete pHash->pKnownFile;
			delete pHash;
		}
	}

	void DeletePartFileHashResult(PartFileHash_Struct* pHash)
	{
		if (pHash != NULL) {
			delete pHash->pKnownFile;
			delete pHash;
		}
	}
}

LRESULT CemuleDlg::OnFileHashed(WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(wParam);
	SharedFileHashResult_Struct* hashed = reinterpret_cast<SharedFileHashResult_Struct*>(lParam);
	CKnownFile* result = hashed != NULL ? hashed->pKnownFile : NULL;
	ASSERT(result == NULL || result->IsKindOf(RUNTIME_CLASS(CKnownFile)));

	if (theApp.IsClosing() || theApp.sharedfiles == NULL || result == NULL) {
		DeleteSharedFileHashResult(hashed);
		return FALSE;
	}

	ASSERT(!result->IsKindOf(RUNTIME_CLASS(CPartFile)));

	// File hashing finished for a shared file (not a partfile) when:
	//	- reading shared directories at startup and hashing files which were not found in known.met
	//	- reading shared directories during runtime (user hit Reload button, added a shared directory, ...)
	hashed->pKnownFile = NULL;
	theApp.sharedfiles->FileHashingFinished(result, hashed->strPathKey);
	delete hashed;
	return TRUE;
}

LRESULT CemuleDlg::OnPartFileHashed(WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(wParam);
	PartFileHash_Struct* hashed = reinterpret_cast<PartFileHash_Struct*>(lParam);
	CKnownFile* result = hashed != NULL ? hashed->pKnownFile : NULL;
	ASSERT(result == NULL || result->IsKindOf(RUNTIME_CLASS(CKnownFile)));

	if (theApp.IsClosing()) {
		DeletePartFileHashResult(hashed);
		return FALSE;
	}

	// File hashing finished for a part file when:
	// - part file just completed
	// - part file was rehashed at startup because the file date of part.met did not match the part file date
	CPartFile* requester = ResolvePartFileHashTarget(hashed);
	if (requester != NULL && result != NULL) {
		ASSERT(requester->IsKindOf(RUNTIME_CLASS(CPartFile)));
		if (requester->GetFileOp() == PFOP_HASHING)
			requester->SetFileOp(PFOP_NONE);
		if (requester->GetFileSize() != result->GetFileSize())
			requester->SetFileSize(result->GetFileSize());
		hashed->pKnownFile = NULL;
		requester->PartFileHashFinished(result);
	} else
		delete result;
	delete hashed;
	return TRUE;
}

namespace
{
	void ApplyFileOpProgressUpdate(CKnownFile* pKnownFile, WPARAM uProgress)
	{
		if (pKnownFile == NULL || theApp.IsClosing())
			return;

		ASSERT(pKnownFile->IsKindOf(RUNTIME_CLASS(CKnownFile)));
		if (pKnownFile->IsKindOf(RUNTIME_CLASS(CPartFile))) {
			CPartFile* pPartFile = static_cast<CPartFile*>(pKnownFile);
			pPartFile->SetFileOpProgress(uProgress);
			pPartFile->UpdateDisplayedInfo(true);
		}
	}
}

LRESULT CemuleDlg::OnFileOpProgress(WPARAM wParam, LPARAM lParam)
{
	if (lParam != 0) {
		ApplyFileOpProgressUpdate(reinterpret_cast<CKnownFile*>(lParam), wParam);
		return 0;
	}

	InterlockedExchange(&m_lFileOpProgressPendingMessage, 0);

	std::map<CKnownFile*, WPARAM> pendingProgress;
	std::map<SPartFileOpProgressKey, WPARAM> pendingPartFileProgress;
	{
		CSingleLock lock(&m_fileOpProgressLock, TRUE);
		pendingProgress.swap(m_pendingFileOpProgress);
		pendingPartFileProgress.swap(m_pendingPartFileOpProgress);
	}

	if (theApp.IsClosing())
		return 0;

	for (std::map<CKnownFile*, WPARAM>::const_iterator it = pendingProgress.begin(); it != pendingProgress.end(); ++it)
		ApplyFileOpProgressUpdate(it->first, it->second);
	for (std::map<SPartFileOpProgressKey, WPARAM>::const_iterator it = pendingPartFileProgress.begin(); it != pendingPartFileProgress.end(); ++it) {
		CPartFile* pPartFile = ResolvePartFileToken(it->first.pPartFile, it->first.dwRuntimeID, it->first.abyFileHash);
		ApplyFileOpProgressUpdate(pPartFile, it->second);
	}

	return 0;
}

// SLUGFILLER: SafeHash
LRESULT CemuleDlg::OnHashFailed(WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(wParam);
	SharedFileHashResult_Struct* hashed = reinterpret_cast<SharedFileHashResult_Struct*>(lParam);
	if (theApp.IsClosing() || theApp.sharedfiles == NULL)
		DeleteSharedFileHashResult(hashed);
	else
		theApp.sharedfiles->HashFailed(hashed);
	return 0;
}

LRESULT CemuleDlg::OnPartFileHashFailed(WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(wParam);
	PartFileHash_Struct* hashed = reinterpret_cast<PartFileHash_Struct*>(lParam);
	CPartFile* requester = ResolvePartFileHashTarget(hashed);
	if (!theApp.IsClosing() && requester != NULL && requester->GetFileOp() == PFOP_HASHING)
		requester->PartFileHashFailed();
	DeletePartFileHashResult(hashed);
	return 0;
}
// SLUGFILLER: SafeHash

LRESULT CemuleDlg::OnFileAllocExc(WPARAM wParam, LPARAM lParam)
{
	if (lParam == 0)
		reinterpret_cast<CPartFile*>(wParam)->FlushBuffersExceptionHandler();
	else
		reinterpret_cast<CPartFile*>(wParam)->FlushBuffersExceptionHandler(reinterpret_cast<CFileException*>(lParam));
	return 0;
}

LRESULT CemuleDlg::OnFileCompleted(WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(wParam);
	PartFileComplete_Struct* pComplete = reinterpret_cast<PartFileComplete_Struct*>(lParam);
	CPartFile* partfile = pComplete != NULL ? ResolvePartFileToken(pComplete->pPartFile, pComplete->dwRuntimeID, pComplete->abyFileHash) : NULL;
	if (partfile != NULL)
		partfile->PerformFileCompleteEnd(pComplete->dwResult);
	delete pComplete;
	return 0;
}

LRESULT CemuleDlg::OnImportPart(WPARAM wParam, LPARAM lParam)
{
	ImportPart_Struct* imp = reinterpret_cast<ImportPart_Struct*>(wParam);
	UNREFERENCED_PARAMETER(lParam);
	ImportOperationContext* pContext = (imp != NULL) ? imp->pContext : NULL;
	CPartFile* partfile = NULL;
	if (pContext != NULL && !theApp.IsClosing() && theApp.downloadqueue != NULL) {
		partfile = theApp.downloadqueue->GetFileByID(pContext->aucFileHash);
		if (partfile == NULL || partfile->GetRuntimeID() != pContext->uPartFileRuntimeID || !partfile->IsImportOperationCurrent(pContext))
			partfile = NULL;
	}

	if (imp != NULL && partfile != NULL)
		if (partfile->WriteToBuffer(imp->end - imp->start + 1, imp->data, imp->start, imp->end, NULL, NULL, false))
			imp->data = NULL; //do not delete the buffer

	if (imp != NULL) {
		delete[] imp->data;
		delete imp;
	}

	if (partfile != NULL)
		partfile->MarkImportPartHandled(pContext);
	else if (pContext != NULL) {
		const LONG lQueuedBlocks = ::InterlockedDecrement(&pContext->lQueuedBlocks);
		if (lQueuedBlocks < 0) {
			ASSERT(0);
			::InterlockedExchange(&pContext->lQueuedBlocks, 0);
		}
	}
	ReleaseImportOperationContext(pContext);
	return 0;
}

LRESULT CemuleDlg::OnImportPartProgress(WPARAM wParam, LPARAM lParam)
{
	ImportOperationContext* pContext = reinterpret_cast<ImportOperationContext*>(lParam);
	if (pContext != NULL && !theApp.IsClosing() && theApp.downloadqueue != NULL) {
		CPartFile* partfile = theApp.downloadqueue->GetFileByID(pContext->aucFileHash);
		if (partfile != NULL && partfile->GetRuntimeID() == pContext->uPartFileRuntimeID && partfile->IsImportOperationCurrent(pContext)) {
			partfile->SetFileOpProgress(wParam);
			partfile->UpdateDisplayedInfo(true);
		}
	}
	ReleaseImportOperationContext(pContext);
	return 0;
}

LRESULT CemuleDlg::OnImportPartFinished(WPARAM wParam, LPARAM lParam)
{
	ImportOperationContext* pContext = reinterpret_cast<ImportOperationContext*>(lParam);
	if (wParam != 0)
		AbortImportOperationContext(pContext);

	if (pContext != NULL && !theApp.IsClosing() && theApp.downloadqueue != NULL) {
		CPartFile* partfile = theApp.downloadqueue->GetFileByID(pContext->aucFileHash);
		if (partfile != NULL && partfile->GetRuntimeID() == pContext->uPartFileRuntimeID && partfile->IsImportOperationCurrent(pContext))
			partfile->TryFinalizeImportPartsOperation(pContext);
	}
	ReleaseImportOperationContext(pContext);
	return 0;
}

LRESULT CemuleDlg::OnFinalizeDeletePendingClient(WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	if (theApp.clientlist != NULL)
		theApp.clientlist->FinalizeDeletePendingClientByRuntimeID((DWORD)wParam);
	return 0;
}

#ifdef _DEBUG
void BeBusy(UINT uSeconds, LPCSTR pszCaller)
{
	UINT s = 0;
	while (uSeconds--) {
		theVerboseLog.Logf(_T("%hs: called=%hs, waited %u sec."), __FUNCTION__, pszCaller, s++);
		::Sleep(SEC2MS(1));
	}
}
#endif

BOOL CemuleDlg::OnQueryEndSession()
{
	AddDebugLogLine(DLP_VERYLOW, _T("%hs"), __FUNCTION__);
	UnregisterIpGuardNotifications();
	if (!CTrayDialog::OnQueryEndSession())
		return FALSE;

	AddDebugLogLine(DLP_VERYLOW, _T("%hs: returning TRUE"), __FUNCTION__);
	return TRUE;
}

void CemuleDlg::OnEndSession(BOOL bEnding)
{
	AddDebugLogLine(DLP_VERYLOW, _T("%hs: bEnding=%d"), __FUNCTION__, bEnding);
	if (bEnding && !theApp.IsClosing()) {
		// If eMule was *not* started with "RUNAS":
		// When user is logging of (or reboots or shutdown system), Windows sends the
		// WM_QUERYENDSESSION/WM_ENDSESSION to all top level windows.
		// Here we can consume as much time as we need to perform our shutdown. Even if we
		// take longer than 20 seconds, Windows will just show a dialog box that 'emule'
		// is not terminating in time and gives the user a chance to cancel that. If the user
		// does not cancel the Windows dialog, Windows will though wait until eMule has
		// terminated by itself - no data loss, no file corruption, everything is fine.
		theApp.m_app_state = APP_STATE_SHUTTINGDOWN;
		OnClose();
	}

	CTrayDialog::OnEndSession(bEnding);
	AddDebugLogLine(DLP_VERYLOW, _T("%hs: returning"), __FUNCTION__);
}

LRESULT CemuleDlg::OnUserChanged(WPARAM, LPARAM)
{
	AddDebugLogLine(DLP_VERYLOW, _T("%hs"), __FUNCTION__);
	// Just want to know if we ever get this message. Maybe it helps us to handle the
	// logoff/reboot/shutdown problem when eMule was started with "RUNAS".
	return Default();
}

LRESULT CemuleDlg::OnConsoleThreadEvent(WPARAM wParam, LPARAM lParam)
{
	AddDebugLogLine(DLP_VERYLOW, _T("%hs: nEvent=%u, nThreadID=%u"), __FUNCTION__, wParam, lParam);

	// If eMule was started with "RUNAS":
	// This message handler receives a 'console event' from the concurrently and thus
	// asynchronously running console control handler thread which was spawned by Windows
	// in case the user logs off/reboots/shutdown. Even if the console control handler thread
	// is waiting on the result from this message handler (is waiting until the main thread
	// has finished processing this inter-application message), the application will get
	// forcefully terminated by Windows after 20 seconds! There is no known way to prevent
	// that. This means, that if we would invoke our standard shutdown code ('OnClose') here
	// and the shutdown takes longer than 20 sec, we will get forcefully terminated by
	// Windows, regardless of what we are doing. This means, MET-file and PART-file corruption
	// may occur. Because the shutdown code in 'OnClose' does also shutdown Kad (which takes
	// a noticeable amount of time) it is not that unlikely that we run into problems with
	// not being finished with our shutdown in 20 seconds.
	//
	if (!theApp.IsClosing()) {
#if 1
		// And it really should be OK to expect that emule can shutdown in 20 sec on almost
		// all computers. So, use the proper shutdown.
		theApp.m_app_state = APP_STATE_SHUTTINGDOWN;
		OnClose();	// do not invoke if shutdown takes longer than 20 sec, read above
#else
		// As a minimum action we at least set the 'shutting down' flag, this will help e.g.
		// interrupted by windows and which would then lead to corrupted MET-files.
		// Setting this flag also helps any possible running threads to stop their work.
		theApp.m_app_state = APP_STATE_SHUTTINGDOWN;

#ifdef _DEBUG
		// Simulate some work.
		//
		// NOTE: If the console thread has already exited, Windows may terminate the process
		// even before the 20 sec. timeout!
#endif

		// Actually, just calling 'ExitProcess' should be the most safe thing which we can
		// do here. Because we received this message via the main message queue we are
		// totally in-sync with the application and therefore we know that we are currently
		// not within a file save action and thus we simply can not cause any file corruption
		// when we exit right now.
		//
		// Of course, there may be some data loss. But it's the same amount of data loss which
		// could occur if we keep running. But if we keep running and wait until Windows
		// terminates us after 20 sec, there is also the chance for file corruption.
		if (thePrefs.GetDebug2Disk()) {
			theVerboseLog.Logf(_T("%hs: ExitProcess"), __FUNCTION__);
			theVerboseLog.Close();
		}
		ExitProcess(0);
#endif
	}

	AddDebugLogLine(DLP_VERYLOW, _T("%hs: returning"), __FUNCTION__);
	return 1;
}


namespace
{
	const int OverlayBaseWindowWidth = 500;
	const int OverlayPanelPadding = 24;
	const int OverlayTaskTitleInset = 16;
	const int OverlayTaskTitleToProgressGap = 12;
	const int OverlayBaseTaskTitleWidth = 192;
	const int OverlayTaskProgressWidth = 232;
	const int OverlayMinimumTaskProgressWidth = 80;
	const int OverlayTextPadding = 4;
	const int OverlayWorkAreaMargin = 16;
	const int OverlayShutdownCaptionReservedWidth = 58;
	const int OverlayStartupCaptionReservedWidth = 84;

	int MeasureOverlaySingleLineTextWidth(CDC& dc, const CString& strText)
	{
		CRect rcText(0, 0, 0, 0);
		dc.DrawText(strText, &rcText, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
		return rcText.Width();
	}
}

class CShutdownProgressDlg : public CWnd
{
public:
	CShutdownProgressDlg()
		: m_pOwner(NULL)
		, m_uActiveStage(0)
		, m_uAnimationPhase(0)
		, m_uHoverButton(0)
		, m_bTrackingMouse(false)
		, m_iTaskTitleWidth(OverlayBaseTaskTitleWidth)
	{
		for (UINT i = 0; i < CemuleDlg::ShutdownProgressStageCount; ++i) {
			m_uDone[i] = 0;
			m_uTotal[i] = 1;
			m_uDisplayedUnits[i] = 0;
			m_uStageWeight[i] = GetDefaultStageWeight(i);
		}
	}

	bool CreateShutdownProgressDlg(CemuleDlg* pOwner)
	{
		if (pOwner == NULL || !::IsWindow(pOwner->GetSafeHwnd()))
			return false;

		m_pOwner = pOwner;
		CString strClass(AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL));
		const CRect rcInitialDialog = GetInitialDialogWindowRect();
		if (!CreateEx(WS_EX_APPWINDOW, strClass, NULL, WS_POPUP, rcInitialDialog, NULL, 0))
			return false;
		const CRect rcDialog = GetContentSizedDialogWindowRect(rcInitialDialog);

		HICON hIcon = reinterpret_cast<HICON>(m_pOwner->SendMessage(WM_GETICON, ICON_BIG, 0));
		if (hIcon != NULL)
			SetIcon(hIcon, TRUE);
		hIcon = reinterpret_cast<HICON>(m_pOwner->SendMessage(WM_GETICON, ICON_SMALL, 0));
		if (hIcon != NULL)
			SetIcon(hIcon, FALSE);

		ShowWindow(SW_SHOW);
		SetWindowPos(&wndTop, rcDialog.left, rcDialog.top, rcDialog.Width(), rcDialog.Height(), SWP_SHOWWINDOW);
		ApplyRoundedWindowRegion(rcDialog.Width(), rcDialog.Height());
		SetForegroundWindow();
		UpdateWindow();
		SetTimer(TimerAnimation, 100, NULL);
		return true;
	}

	void ConfigureWorkEstimates(UINT uDownloadCount, bool bSaveSources)
	{
		uint64 uStageWork[CemuleDlg::ShutdownProgressStageCount];
		uStageWork[CemuleDlg::ShutdownProgressNetwork] = 150;
		uStageWork[CemuleDlg::ShutdownProgressDiskIo] = 250;
		uStageWork[CemuleDlg::ShutdownProgressSaveData] = 800;
		uStageWork[CemuleDlg::ShutdownProgressDownloads] = uDownloadCount != 0 ? 200 : 50;
		uStageWork[CemuleDlg::ShutdownProgressCleanup] = 400;

		const uint64 uCappedDownloads = min(static_cast<uint64>(uDownloadCount), 250000ULL);
		if (uCappedDownloads != 0) {
			if (bSaveSources)
				uStageWork[CemuleDlg::ShutdownProgressSaveData] += min(100000ULL, uCappedDownloads);
			else
				uStageWork[CemuleDlg::ShutdownProgressSaveData] += min(10000ULL, uCappedDownloads / 8ULL);
			uStageWork[CemuleDlg::ShutdownProgressDownloads] += min(300000ULL, uCappedDownloads * 3ULL);
		}

		for (UINT i = 0; i < CemuleDlg::ShutdownProgressStageCount; ++i)
			m_uStageWeight[i] = static_cast<UINT>(max(1ULL, min(static_cast<uint64>(UINT_MAX / 4), uStageWork[i])));
	}

	void UpdateProgress(UINT uStage, UINT uDone, UINT uTotal, bool bForcePaint)
	{
		if (uStage >= CemuleDlg::ShutdownProgressStageCount)
			uStage = CemuleDlg::ShutdownProgressCleanup;

		if (uStage == CemuleDlg::ShutdownProgressDownloads && m_uDisplayedUnits[uStage] != 0 && uDone == 0)
			uDone = m_uDone[uStage];

		m_uActiveStage = uStage;
		m_uTotal[uStage] = max(1U, uTotal);
		m_uDone[uStage] = min(uDone, m_uTotal[uStage]);

		UINT uDisplayedUnits = ScaleProgress(ShutdownProgressUnits, m_uDone[uStage], m_uTotal[uStage]);
		if (uDisplayedUnits < m_uDisplayedUnits[uStage])
			uDisplayedUnits = m_uDisplayedUnits[uStage];
		m_uDisplayedUnits[uStage] = min(ShutdownProgressUnits, uDisplayedUnits);

		if (::IsWindow(m_hWnd)) {
			Invalidate(FALSE);
			if (bForcePaint)
				UpdateWindow();
			PumpMessages();
		}
	}

	void PumpMessages()
	{
		if (!::IsWindow(m_hWnd))
			return;

		MSG msg;
		int iMessages = 0;
		while (iMessages < 16 && ::PeekMessage(&msg, m_hWnd, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				::PostQuitMessage(static_cast<int>(msg.wParam));
				break;
			}
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			++iMessages;
		}
	}

protected:
	enum { TimerAnimation = 1, ButtonMinimize = 1, WindowCornerDiameter = 18, BorderThickness = 2, ShutdownProgressUnits = 10000 };

	static CString GetDlgResString(LPCTSTR pszKey, LPCTSTR pszDefault)
	{
		CString strText(GetResString(pszKey));
		return strText == pszKey ? CString(pszDefault) : strText;
	}

	static UINT ScaleProgress(UINT uSpan, UINT uDone, UINT uTotal)
	{
		if (uTotal == 0)
			return 0;
		return static_cast<UINT>((static_cast<uint64>(uSpan) * min(uDone, uTotal)) / uTotal);
	}

	static UINT GetDefaultStageWeight(UINT uStage)
	{
		switch (uStage) {
			case CemuleDlg::ShutdownProgressNetwork: return 5;
			case CemuleDlg::ShutdownProgressDiskIo: return 10;
			case CemuleDlg::ShutdownProgressSaveData: return 20;
			case CemuleDlg::ShutdownProgressDownloads: return 55;
			case CemuleDlg::ShutdownProgressCleanup: return 10;
		}
		return 10;
	}

	void RebalanceStageWeights(UINT uStage, UINT uTotal)
	{
		if (uStage != CemuleDlg::ShutdownProgressDownloads)
			return;

		UINT uDownloadWeight = 25;
		if (uTotal >= 50000)
			uDownloadWeight = 55;
		else if (uTotal >= 10000)
			uDownloadWeight = 50;
		else if (uTotal >= 1000)
			uDownloadWeight = 45;
		else if (uTotal >= 100)
			uDownloadWeight = 35;

		m_uStageWeight[CemuleDlg::ShutdownProgressNetwork] = 5;
		m_uStageWeight[CemuleDlg::ShutdownProgressDiskIo] = 10;
		m_uStageWeight[CemuleDlg::ShutdownProgressDownloads] = uDownloadWeight;
		m_uStageWeight[CemuleDlg::ShutdownProgressCleanup] = 10;
		m_uStageWeight[CemuleDlg::ShutdownProgressSaveData] = max(1U, 100U - 25U - uDownloadWeight);
	}

	static LPCTSTR GetStageTitleKey(UINT uStage)
	{
		switch (uStage) {
			case CemuleDlg::ShutdownProgressNetwork: return _T("EXIT_LOAD_TASK_NETWORK");
			case CemuleDlg::ShutdownProgressDiskIo: return _T("EXIT_LOAD_TASK_DISKIO");
			case CemuleDlg::ShutdownProgressSaveData: return _T("EXIT_LOAD_TASK_SAVEDATA");
			case CemuleDlg::ShutdownProgressDownloads: return _T("EXIT_LOAD_TASK_DOWNLOADS");
			case CemuleDlg::ShutdownProgressCleanup: return _T("EXIT_LOAD_TASK_CLEANUP");
		}
		return _T("EXIT_LOAD_TASK_CLEANUP");
	}

	static LPCTSTR GetStageDefaultTitle(UINT uStage)
	{
		switch (uStage) {
			case CemuleDlg::ShutdownProgressNetwork: return _T("Stopping network");
			case CemuleDlg::ShutdownProgressDiskIo: return _T("Draining disk I/O");
			case CemuleDlg::ShutdownProgressSaveData: return _T("Saving data");
			case CemuleDlg::ShutdownProgressDownloads: return _T("Saving downloads");
			case CemuleDlg::ShutdownProgressCleanup: return _T("Cleaning up");
		}
		return _T("Cleaning up");
	}

	CRect GetInitialDialogWindowRect() const
	{
		const int iWidth = OverlayBaseWindowWidth;
		const int iHeight = 220;
		CRect rcAnchor;
		if (m_pOwner != NULL && ::IsWindow(m_pOwner->GetSafeHwnd()))
			m_pOwner->GetWindowRect(rcAnchor);
		else
			::SystemParametersInfo(SPI_GETWORKAREA, 0, &rcAnchor, 0);

		HMONITOR hMonitor = ::MonitorFromRect(rcAnchor, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi;
		ZeroMemory(&mi, sizeof(mi));
		mi.cbSize = sizeof(mi);
		CRect rcWork;
		if (hMonitor != NULL && ::GetMonitorInfo(hMonitor, &mi))
			rcWork = mi.rcWork;
		else
			::SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);

		return CRect(rcWork.left + (rcWork.Width() - iWidth) / 2, rcWork.top + (rcWork.Height() - iHeight) / 2, rcWork.left + (rcWork.Width() + iWidth) / 2, rcWork.top + (rcWork.Height() + iHeight) / 2);
	}

	CRect GetContentSizedDialogWindowRect(const CRect& rcInitial)
	{
		CClientDC dc(this);
		CFont* pFont = GetFont();
		CFont* pOldFont = pFont != NULL ? dc.SelectObject(pFont) : NULL;
		int iMaxTaskTextWidth = 0;
		for (UINT i = 0; i < CemuleDlg::ShutdownProgressStageCount; ++i)
			iMaxTaskTextWidth = max(iMaxTaskTextWidth, MeasureOverlaySingleLineTextWidth(dc, GetDlgResString(GetStageTitleKey(i), GetStageDefaultTitle(i))));
		const int iTitleTextWidth = MeasureOverlaySingleLineTextWidth(dc, GetDlgResString(_T("EXIT_LOAD_TITLE"), _T("Exiting eMule AI")));
		if (pOldFont != NULL)
			dc.SelectObject(pOldFont);

		m_iTaskTitleWidth = max(OverlayBaseTaskTitleWidth, iMaxTaskTextWidth + OverlayTextPadding);
		int iDesiredWidth = OverlayPanelPadding * 2 + OverlayTaskTitleInset + m_iTaskTitleWidth + OverlayTaskTitleToProgressGap + OverlayTaskProgressWidth;
		iDesiredWidth = max(iDesiredWidth, OverlayPanelPadding + iTitleTextWidth + OverlayShutdownCaptionReservedWidth + OverlayTextPadding);

		HMONITOR hMonitor = ::MonitorFromRect(rcInitial, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi;
		ZeroMemory(&mi, sizeof(mi));
		mi.cbSize = sizeof(mi);
		CRect rcWork;
		if (hMonitor != NULL && ::GetMonitorInfo(hMonitor, &mi))
			rcWork = mi.rcWork;
		else
			::SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);

		const int iMaxWidth = max(1, rcWork.Width() - OverlayWorkAreaMargin * 2);
		const int iWidth = min(max(OverlayBaseWindowWidth, iDesiredWidth), iMaxWidth);
		const int iFixedRowWidth = OverlayPanelPadding * 2 + OverlayTaskTitleInset + OverlayTaskTitleToProgressGap + OverlayTaskProgressWidth;
		m_iTaskTitleWidth = min(m_iTaskTitleWidth, max(32, iWidth - iFixedRowWidth));
		const int iHeight = rcInitial.Height();
		return CRect(rcWork.left + (rcWork.Width() - iWidth) / 2, rcWork.top + (rcWork.Height() - iHeight) / 2, rcWork.left + (rcWork.Width() + iWidth) / 2, rcWork.top + (rcWork.Height() + iHeight) / 2);
	}

	void ApplyRoundedWindowRegion(int iWidth, int iHeight)
	{
		if (!::IsWindow(m_hWnd))
			return;

		HRGN hRgn = ::CreateRoundRectRgn(0, 0, max(1, iWidth) + 1, max(1, iHeight) + 1, WindowCornerDiameter, WindowCornerDiameter);
		if (hRgn == NULL)
			return;
		if (::SetWindowRgn(m_hWnd, hRgn, TRUE) == 0)
			::DeleteObject(hRgn);
	}

	UINT GetOverallPercent() const
	{
		uint64 uWeightedDone = 0;
		uint64 uWeightedTotal = 0;
		for (UINT i = 0; i < CemuleDlg::ShutdownProgressStageCount; ++i) {
			const UINT uWeight = max(1U, m_uStageWeight[i]);
			uWeightedDone += static_cast<uint64>(uWeight) * min(ShutdownProgressUnits, m_uDisplayedUnits[i]);
			uWeightedTotal += static_cast<uint64>(uWeight) * ShutdownProgressUnits;
		}
		if (uWeightedTotal == 0)
			return 0;
		return min(100U, static_cast<UINT>((100ULL * uWeightedDone) / uWeightedTotal));
	}

	UINT GetStagePercent(UINT uStage) const
	{
		if (uStage >= CemuleDlg::ShutdownProgressStageCount)
			return 0;
		return ScaleProgress(100, min(ShutdownProgressUnits, m_uDisplayedUnits[uStage]), ShutdownProgressUnits);
	}

	void LayoutCaptionButtons(const CRect& rcPanel)
	{
		const int iButtonSize = 20;
		m_rcMinimize.SetRect(rcPanel.right - 22 - iButtonSize, rcPanel.top + 16, rcPanel.right - 22, rcPanel.top + 16 + iButtonSize);
	}

	void DrawCaptionButton(CDC* pDC, const CRect& rcButton, bool bDark)
	{
		const bool bHover = m_uHoverButton == ButtonMinimize;
		const COLORREF crButton = bDark ? (bHover ? RGB(56, 62, 78) : RGB(39, 44, 57)) : (bHover ? RGB(238, 243, 252) : RGB(247, 250, 255));
		const COLORREF crEdge = bHover ? RGB(96, 150, 238) : (bDark ? RGB(80, 88, 108) : RGB(190, 203, 224));
		const COLORREF crGlyph = bDark ? RGB(224, 232, 248) : RGB(34, 46, 70);
		CBrush brButton(crButton);
		CPen penEdge(PS_SOLID, 1, crEdge);
		CPen* pOldPen = pDC->SelectObject(&penEdge);
		CBrush* pOldBrush = pDC->SelectObject(&brButton);
		pDC->RoundRect(rcButton, CPoint(6, 6));
		CPen penGlyph(PS_SOLID, 1, crGlyph);
		pDC->SelectObject(&penGlyph);
		const int iY = rcButton.CenterPoint().y;
		pDC->MoveTo(rcButton.left + 6, iY);
		pDC->LineTo(rcButton.right - 6, iY);
		pDC->SelectObject(pOldPen);
		pDC->SelectObject(pOldBrush);
	}

	void DrawProgressBarWithPercent(CDC* pDC, const CRect& rcProgress, UINT uPercent, COLORREF crEdge, COLORREF crFill, COLORREF crBack, COLORREF crText)
	{
		pDC->FillSolidRect(rcProgress, crBack);
		const int iFillWidth = min(rcProgress.Width(), max(0, MulDiv(rcProgress.Width(), min(100U, uPercent), 100)));
		if (iFillWidth > 0) {
			CRect rcFill(rcProgress);
			rcFill.right = rcFill.left + iFillWidth;
			pDC->FillSolidRect(rcFill, crFill);
		}
		CPen penProgress(PS_SOLID, 1, crEdge);
		CPen* pOldPen = pDC->SelectObject(&penProgress);
		pDC->MoveTo(rcProgress.left, rcProgress.top);
		pDC->LineTo(rcProgress.right, rcProgress.top);
		pDC->LineTo(rcProgress.right, rcProgress.bottom);
		pDC->LineTo(rcProgress.left, rcProgress.bottom);
		pDC->LineTo(rcProgress.left, rcProgress.top);
		pDC->SelectObject(pOldPen);
		CString strPercent;
		strPercent.Format(_T("%u%%"), min(100U, uPercent));
		CRect rcText(rcProgress);
		pDC->SetTextColor(crText);
		pDC->DrawText(strPercent, rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}

	void DrawDialog(CDC* pDC, const CRect& rcClient)
	{
		const bool bDark = IsDarkModeEnabled();
		const COLORREF crPanel = bDark ? RGB(30, 33, 42) : RGB(250, 252, 255);
		const COLORREF crPanelEdge = bDark ? RGB(75, 82, 102) : RGB(196, 207, 224);
		const COLORREF crTitle = bDark ? RGB(242, 245, 255) : RGB(24, 35, 56);
		const COLORREF crText = bDark ? RGB(218, 224, 240) : RGB(42, 52, 72);
		const COLORREF crProgressFill = RGB(76, 132, 232);
		const COLORREF crProgressBack = bDark ? RGB(54, 60, 74) : RGB(224, 231, 242);
		pDC->FillSolidRect(rcClient, crPanel);

		CRect rcPanel(rcClient);
		CRgn rgnPanel;
		rgnPanel.CreateRoundRectRgn(rcPanel.left, rcPanel.top, rcPanel.right + 1, rcPanel.bottom + 1, WindowCornerDiameter, WindowCornerDiameter);
		CBrush brPanel(crPanel);
		pDC->FillRgn(&rgnPanel, &brPanel);
		CPen penEdge(PS_SOLID, 1, crPanelEdge);
		CPen* pOldPen = pDC->SelectObject(&penEdge);
		CGdiObject* pOldBrush = pDC->SelectStockObject(NULL_BRUSH);
		pDC->RoundRect(rcPanel, CPoint(WindowCornerDiameter, WindowCornerDiameter));
		DrawAnimatedRainbowBorder(pDC, rcPanel, m_uAnimationPhase, BorderThickness, WindowCornerDiameter);

		CFont* pOldFont = pDC->SelectObject(GetFont());
		pDC->SetBkMode(TRANSPARENT);
		LayoutCaptionButtons(rcPanel);
		DrawCaptionButton(pDC, m_rcMinimize, bDark);

		const int iPad = 24;
		CRect rcTitle(rcPanel.left + iPad, rcPanel.top + 20, m_rcMinimize.left - 12, rcPanel.top + 42);
		pDC->SetTextColor(crTitle);
		pDC->DrawText(GetDlgResString(_T("EXIT_LOAD_TITLE"), _T("Exiting eMule AI")), rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

		CRect rcProgress(rcPanel.left + iPad, rcTitle.bottom + 10, rcPanel.right - iPad, rcTitle.bottom + 26);
		DrawProgressBarWithPercent(pDC, rcProgress, GetOverallPercent(), crPanelEdge, crProgressFill, crProgressBack, crTitle);

		int iTop = rcProgress.bottom + 20;
		for (UINT i = 0; i < CemuleDlg::ShutdownProgressStageCount; ++i) {
			CRect rcRow(rcPanel.left + iPad, iTop, rcPanel.right - iPad, iTop + 24);
			DrawTaskRow(pDC, rcRow, i, crText, crPanelEdge, crProgressFill, crProgressBack, crTitle, bDark);
			iTop += 24;
		}

		pDC->SelectObject(pOldFont);
		pDC->SelectObject(pOldPen);
		pDC->SelectObject(pOldBrush);
	}

	void DrawTaskRow(CDC* pDC, const CRect& rcRow, UINT uStage, COLORREF crText, COLORREF crEdge, COLORREF crFill, COLORREF crBack, COLORREF crProgressText, bool bDark)
	{
		const bool bStageDone = uStage < CemuleDlg::ShutdownProgressStageCount && m_uDisplayedUnits[uStage] >= ShutdownProgressUnits;
		const COLORREF crDotFill = bStageDone ? RGB(82, 196, 98) : (uStage == m_uActiveStage ? RGB(255, 221, 82) : (bDark ? RGB(104, 112, 132) : RGB(180, 190, 210)));
		CRect rcDot(rcRow.left, rcRow.top + 8, rcRow.left + 8, rcRow.top + 16);
		CBrush brDot(crDotFill);
		CPen penDot(PS_SOLID, 1, crEdge);
		CPen* pOldPen = pDC->SelectObject(&penDot);
		CBrush* pOldBrush = pDC->SelectObject(&brDot);
		pDC->Ellipse(rcDot);
		pDC->SelectObject(pOldBrush);
		pDC->SelectObject(pOldPen);

		const int iTitleLeft = rcRow.left + OverlayTaskTitleInset;
		const int iProgressLeft = min(iTitleLeft + m_iTaskTitleWidth + OverlayTaskTitleToProgressGap, rcRow.right - OverlayMinimumTaskProgressWidth);
		CRect rcTitle(iTitleLeft, rcRow.top, max(iTitleLeft, iProgressLeft - OverlayTaskTitleToProgressGap), rcRow.bottom);
		pDC->SetTextColor(crText);
		pDC->DrawText(GetDlgResString(GetStageTitleKey(uStage), GetStageDefaultTitle(uStage)), rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

		CRect rcMiniProgress(iProgressLeft, rcRow.top + 4, rcRow.right, rcRow.top + 20);
		DrawProgressBarWithPercent(pDC, rcMiniProgress, GetStagePercent(uStage), crEdge, crFill, crBack, crProgressText);
	}

	UINT HitTestButton(CPoint point) const
	{
		return m_rcMinimize.PtInRect(point) ? ButtonMinimize : 0;
	}

	afx_msg void OnPaint()
	{
		CPaintDC dcPaint(this);
		CRect rcClient;
		GetClientRect(rcClient);
		CDC dcMem;
		dcMem.CreateCompatibleDC(&dcPaint);
		CBitmap bmpMem;
		bmpMem.CreateCompatibleBitmap(&dcPaint, max(1, rcClient.Width()), max(1, rcClient.Height()));
		CBitmap* pOldBitmap = dcMem.SelectObject(&bmpMem);
		DrawDialog(&dcMem, rcClient);
		dcPaint.BitBlt(0, 0, rcClient.Width(), rcClient.Height(), &dcMem, 0, 0, SRCCOPY);
		dcMem.SelectObject(pOldBitmap);
	}

	afx_msg BOOL OnEraseBkgnd(CDC*)
	{
		return TRUE;
	}

	afx_msg void OnSize(UINT nType, int cx, int cy)
	{
		CWnd::OnSize(nType, cx, cy);
		ApplyRoundedWindowRegion(cx, cy);
	}

	afx_msg void OnTimer(UINT_PTR nIDEvent)
	{
		if (nIDEvent == TimerAnimation) {
			m_uAnimationPhase = (m_uAnimationPhase + 18) % 1536;
			Invalidate(FALSE);
			return;
		}
		CWnd::OnTimer(nIDEvent);
	}

	afx_msg void OnLButtonDown(UINT nFlags, CPoint point)
	{
		if (HitTestButton(point) == 0 && point.y < 56) {
			SendMessage(WM_NCLBUTTONDOWN, HTCAPTION, 0);
			return;
		}
		CWnd::OnLButtonDown(nFlags, point);
	}

	afx_msg void OnLButtonUp(UINT, CPoint point)
	{
		if (HitTestButton(point) == ButtonMinimize)
			ShowWindow(SW_MINIMIZE);
	}

	afx_msg void OnClose()
	{
		ShowWindow(SW_MINIMIZE);
	}

	afx_msg void OnMouseMove(UINT nFlags, CPoint point)
	{
		const UINT uButton = HitTestButton(point);
		if (m_uHoverButton != uButton) {
			m_uHoverButton = uButton;
			Invalidate(FALSE);
		}

		if (!m_bTrackingMouse) {
			TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hWnd, 0 };
			m_bTrackingMouse = ::TrackMouseEvent(&tme) != FALSE;
		}
		CWnd::OnMouseMove(nFlags, point);
	}

	afx_msg void OnMouseLeave()
	{
		m_bTrackingMouse = false;
		if (m_uHoverButton != 0) {
			m_uHoverButton = 0;
			Invalidate(FALSE);
		}
	}

	afx_msg BOOL OnSetCursor(CWnd*, UINT, UINT)
	{
		CPoint point;
		::GetCursorPos(&point);
		ScreenToClient(&point);
		if (HitTestButton(point) != 0) {
			::SetCursor(::LoadCursor(NULL, IDC_HAND));
			return TRUE;
		}
		::SetCursor(::LoadCursor(NULL, IDC_ARROW));
		return TRUE;
	}

	virtual BOOL PreTranslateMessage(MSG* pMsg)
	{
		if (pMsg != NULL && pMsg->message == WM_KEYDOWN && (pMsg->wParam == VK_ESCAPE || pMsg->wParam == VK_RETURN))
			return TRUE;
		return CWnd::PreTranslateMessage(pMsg);
	}

	DECLARE_MESSAGE_MAP()

private:
	CemuleDlg* m_pOwner;
	UINT m_uDone[CemuleDlg::ShutdownProgressStageCount];
	UINT m_uTotal[CemuleDlg::ShutdownProgressStageCount];
	UINT m_uDisplayedUnits[CemuleDlg::ShutdownProgressStageCount];
	UINT m_uStageWeight[CemuleDlg::ShutdownProgressStageCount];
	UINT m_uActiveStage;
	UINT m_uAnimationPhase;
	UINT m_uHoverButton;
	bool m_bTrackingMouse;
	int m_iTaskTitleWidth;
	CRect m_rcMinimize;
};

BEGIN_MESSAGE_MAP(CShutdownProgressDlg, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_CLOSE()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_SETCURSOR()
END_MESSAGE_MAP()


class CStartupLoadingDlg : public CWnd
{
public:
	CStartupLoadingDlg()
		: m_pOwner(NULL)
		, m_uDone(0)
		, m_uTotal(0)
		, m_uDisplayedOverallUnits(0)
		, m_uAnimationPhase(0)
		, m_uHoverButton(0)
		, m_dwLastHeartbeatTick(0)
		, m_dwLastHeartbeatTraceTick(0)
		, m_bTrackingMouse(false)
		, m_bCancelExitPending(false)
		, m_iTaskTitleWidth(OverlayBaseTaskTitleWidth)
	{
	}

	bool CreateStartupLoadingDlg(CemuleDlg* pOwner)
	{
		if (pOwner == NULL || !::IsWindow(pOwner->GetSafeHwnd()) || !pOwner->IsInitializing())
			return false;

		m_pOwner = pOwner;
		RefreshProgress(true);

		CString strClass(AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL));
		const CRect rcInitialDialog = GetInitialDialogWindowRect();
		if (!CreateEx(WS_EX_APPWINDOW, strClass, NULL, WS_POPUP, rcInitialDialog, NULL, 0))
			return false;
		const CRect rcDialog = GetContentSizedDialogWindowRect(rcInitialDialog);
		HICON hIcon = reinterpret_cast<HICON>(m_pOwner->SendMessage(WM_GETICON, ICON_BIG, 0));
		if (hIcon != NULL)
			SetIcon(hIcon, TRUE);
		hIcon = reinterpret_cast<HICON>(m_pOwner->SendMessage(WM_GETICON, ICON_SMALL, 0));
		if (hIcon != NULL)
			SetIcon(hIcon, FALSE);

		ShowWindow(SW_SHOW);
		SetWindowPos(&wndTop, rcDialog.left, rcDialog.top, rcDialog.Width(), rcDialog.Height(), SWP_SHOWWINDOW);
		ApplyRoundedWindowRegion(rcDialog.Width(), rcDialog.Height());
		SetForegroundWindow();
		SetFocus();
		UpdateWindow();
		SetTimer(TimerRefresh, 150, NULL);
		SetTimer(TimerAnimation, 100, NULL);
		return true;
	}

	bool IsDialogMessageTarget(const MSG* pMsg) const
	{
		if (pMsg == NULL || !::IsWindow(m_hWnd))
			return false;
		return pMsg->hwnd == m_hWnd || ::IsChild(m_hWnd, pMsg->hwnd) != FALSE;
	}

	void Reactivate()
	{
		if (!::IsWindow(m_hWnd))
			return;
		m_bCancelExitPending = false;
		RefreshProgress(true);
		if (IsIconic())
			ShowWindow(SW_RESTORE);
		else
			ShowWindow(SW_SHOW);
		SetWindowPos(&wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
		SetForegroundWindow();
		SetFocus();
	}

	void RequestCancelAndExit()
	{
		if (m_bCancelExitPending)
			return;
		if (m_pOwner != NULL && ::IsWindow(m_pOwner->GetSafeHwnd())) {
			m_bCancelExitPending = true;
			if (!m_pOwner->PostMessage(UM_STARTUP_LOADING_CANCEL_EXIT))
				m_bCancelExitPending = false;
		}
	}

	void RefreshProgressNow(bool bForce)
	{
		RefreshProgress(bForce);
	}

protected:
	enum EStartupLoadTaskState
	{
		StartupLoadTaskWaiting,
		StartupLoadTaskLoading,
		StartupLoadTaskApplying,
		StartupLoadTaskReady,
		StartupLoadTaskFailed,
		StartupLoadTaskCancelled
	};

	enum EStartupLoadTaskMetricId
	{
		StartupLoadTaskMetricDownloads,
		StartupLoadTaskMetricStoredSearches,
		StartupLoadTaskMetricKnownFiles,
		StartupLoadTaskMetricClientHistory,
		StartupLoadTaskMetricSharedFiles,
		StartupLoadTaskMetricCount
	};

	struct SStartupLoadTaskProgress
	{
		SStartupLoadTaskProgress()
			: eMetricId(StartupLoadTaskMetricCount)
			, eState(StartupLoadTaskWaiting)
			, uDone(0)
			, uTotal(0)
			, uWeight(100)
		{
		}

		EStartupLoadTaskMetricId eMetricId;
		CString strTitle;
		EStartupLoadTaskState eState;
		UINT uDone;
		UINT uTotal;
		UINT uWeight;
	};

	struct SStartupLoadTaskTimeModel
	{
		SStartupLoadTaskTimeModel()
			: bObserved(false)
			, bCompleted(false)
			, dwFirstActiveTick(0)
			, dwCompletedTick(0)
		{
		}

		bool bObserved;
		bool bCompleted;
		DWORD dwFirstActiveTick;
		DWORD dwCompletedTick;
	};

	struct SStartupLoadTaskVisualState
	{
		SStartupLoadTaskVisualState()
			: uLastUnits(0)
			, bTerminal(false)
			, eTerminalState(StartupLoadTaskReady)
		{
		}

		UINT uLastUnits;
		bool bTerminal;
		EStartupLoadTaskState eTerminalState;
	};

	enum { TimerRefresh = 1, TimerAnimation = 2, ButtonMinimize = 1, ButtonClose = 2, StartupHeartbeatMaxMs = 250, StartupHeartbeatTraceMs = 1000, StartupProgressUnits = 10000, StartupTimeWeightMsPerUnit = 10, StartupWindowCornerDiameter = 18, StartupBorderThickness = 2 };

	static CString GetDlgResString(LPCTSTR pszKey, LPCTSTR pszDefault)
	{
		CString strText(GetResString(pszKey));
		return strText == pszKey ? CString(pszDefault) : strText;
	}

	static EStartupLoadTaskState MapMetadataState(CemuleApp::EStartupMetadataState eState)
	{
		switch (eState) {
			case CemuleApp::StartupMetadataStateLoading: return StartupLoadTaskLoading;
			case CemuleApp::StartupMetadataStateApplying: return StartupLoadTaskApplying;
			case CemuleApp::StartupMetadataStateReady: return StartupLoadTaskReady;
			case CemuleApp::StartupMetadataStateSkipped: return StartupLoadTaskReady;
			case CemuleApp::StartupMetadataStateFailed: return StartupLoadTaskFailed;
			case CemuleApp::StartupMetadataStateCancelled: return StartupLoadTaskCancelled;
			case CemuleApp::StartupMetadataStateNotStarted:
				break;
		}
		return StartupLoadTaskWaiting;
	}

	static EStartupLoadTaskMetricId MapMetadataTaskMetric(CemuleApp::EStartupMetadataDomain eDomain)
	{
		switch (eDomain) {
			case CemuleApp::StartupMetadataDownloads: return StartupLoadTaskMetricDownloads;
			case CemuleApp::StartupMetadataKnownFiles: return StartupLoadTaskMetricKnownFiles;
			case CemuleApp::StartupMetadataClientHistory: return StartupLoadTaskMetricClientHistory;
			case CemuleApp::StartupMetadataStoredSearches: return StartupLoadTaskMetricStoredSearches;
			case CemuleApp::StartupMetadataSharedRules: return StartupLoadTaskMetricSharedFiles;
			case CemuleApp::StartupMetadataKnown2Index:
			case CemuleApp::StartupMetadataDomainCount:
				break;
		}
		return StartupLoadTaskMetricCount;
	}

	static UINT ScaleStartupProgress(UINT uSpan, UINT uDone, UINT uTotal)
	{
		if (uTotal == 0)
			return 0;
		const UINT uBoundedDone = min(uDone, uTotal);
		return static_cast<UINT>((static_cast<uint64>(uSpan) * uBoundedDone) / uTotal);
	}

	static UINT ScaleStartupKnownAttachProgress(UINT uSpan, size_t uDone, size_t uTotal)
	{
		if (uTotal == 0)
			return 0;
		const uint64 uBoundedDone = static_cast<uint64>(min(uDone, uTotal));
		const uint64 uBoundedTotal = static_cast<uint64>(uTotal);
		const uint64 uLinear = (static_cast<uint64>(uSpan) * uBoundedDone) / uBoundedTotal;
		if (uTotal < 25000)
			return static_cast<UINT>(uLinear);

		const uint64 uQuadratic = (static_cast<uint64>(uSpan) * uBoundedDone * uBoundedDone) / ((uBoundedTotal * uBoundedTotal) != 0 ? (uBoundedTotal * uBoundedTotal) : 1);
		const UINT uMixPermille = min(850U, static_cast<UINT>((uTotal - 25000) / 250));
		return static_cast<UINT>((uLinear * (1000U - uMixPermille) + uQuadratic * uMixPermille) / 1000U);
	}

	static UINT GetTaskProgressUnits(const SStartupLoadTaskProgress& task)
	{
		return task.uTotal > 0 ? min(static_cast<UINT>(StartupProgressUnits), ScaleStartupProgress(StartupProgressUnits, task.uDone, task.uTotal)) : 0;
	}

	static bool IsTerminalTaskState(EStartupLoadTaskState eState)
	{
		return eState == StartupLoadTaskReady || eState == StartupLoadTaskFailed || eState == StartupLoadTaskCancelled;
	}

	static void SetTaskUnitProgress(UINT uDoneUnits, UINT& uDone, UINT& uTotal)
	{
		uTotal = StartupProgressUnits;
		uDone = min(static_cast<UINT>(StartupProgressUnits), uDoneUnits);
	}

	static void SetTaskPhaseProgress(UINT uPhaseStart, UINT uPhaseSpan, UINT uPhaseDone, UINT uPhaseTotal, UINT& uDone, UINT& uTotal)
	{
		const UINT uStart = min(static_cast<UINT>(StartupProgressUnits), uPhaseStart);
		const UINT uSpan = min(static_cast<UINT>(StartupProgressUnits) - uStart, uPhaseSpan);
		SetTaskUnitProgress(uStart + ScaleStartupProgress(uSpan, uPhaseDone, uPhaseTotal), uDone, uTotal);
	}

	static UINT GetTaskPercent(const SStartupLoadTaskProgress& task)
	{
		if (IsTerminalTaskState(task.eState))
			return 100;
		if (task.uTotal > 0)
			return min(100U, ScaleStartupProgress(100U, task.uDone, task.uTotal));
		return task.eState == StartupLoadTaskWaiting ? 0U : 1U;
	}

	static ULONGLONG GetConfigFileSize(LPCTSTR pszFileName)
	{
		if (pszFileName == NULL || pszFileName[0] == _T('\0'))
			return 0;
		CFileStatus status;
		const CString strPath(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + pszFileName);
		return CFile::GetStatus(strPath, status) ? static_cast<ULONGLONG>(status.m_size) : 0;
	}

	static UINT ClampStartupTaskWeight(ULONGLONG ullWeight, UINT uMinWeight)
	{
		static const UINT uMaxWeight = 60000;
		if (ullWeight < uMinWeight)
			return uMinWeight;
		if (ullWeight > uMaxWeight)
			return uMaxWeight;
		return static_cast<UINT>(ullWeight);
	}

	static UINT EstimateStartupTimeWeight(ULONGLONG ullMilliseconds, UINT uMinWeight)
	{
		return ClampStartupTaskWeight((ullMilliseconds + StartupTimeWeightMsPerUnit - 1ULL) / StartupTimeWeightMsPerUnit, uMinWeight);
	}

	static UINT EstimateConfigFileWeight(LPCTSTR pszFileName, UINT uComplexity, UINT uMinWeight)
	{
		const ULONGLONG ullSize = GetConfigFileSize(pszFileName);
		const ULONGLONG ullUnits = ((ullSize + 32767ULL) / 32768ULL) * max(1U, uComplexity);
		return ClampStartupTaskWeight(ullUnits, uMinWeight);
	}

	static UINT AddStartupTaskWeights(UINT uLeft, UINT uRight)
	{
		static const UINT uMaxWeight = 60000;
		if (uLeft > uMaxWeight - min(uRight, uMaxWeight))
			return uMaxWeight;
		return uLeft + uRight;
	}

	static UINT GetMetadataTaskWeight(CemuleApp::EStartupMetadataDomain eDomain)
	{
		switch (eDomain) {
		case CemuleApp::StartupMetadataKnownFiles:
			return AddStartupTaskWeights(EstimateConfigFileWeight(_T("known.met"), 5, 200), EstimateConfigFileWeight(_T("cancelled.met"), 2, 0));
		case CemuleApp::StartupMetadataClientHistory:
			return EstimateConfigFileWeight(_T("clienthistory.met"), 3, 80);
		case CemuleApp::StartupMetadataKnown2Index:
			return AddStartupTaskWeights(EstimateConfigFileWeight(_T("known2_64.met"), 1, 20), EstimateConfigFileWeight(_T("known2.met"), 1, 0));
		case CemuleApp::StartupMetadataStoredSearches:
			return EstimateConfigFileWeight(_T("StoredSearches.met"), 4, 80);
		case CemuleApp::StartupMetadataDownloads:
			return 160;
		case CemuleApp::StartupMetadataSharedRules:
			return EstimateConfigFileWeight(_T("sharedcache.dat"), 2, 40);
		default:
			break;
		}
		return 100;
	}

	UINT GetKnownFilesTaskWeight() const
	{
		UINT uWeight = GetMetadataTaskWeight(CemuleApp::StartupMetadataKnownFiles);
		const CemuleApp::SStartupMetadataLoadState state = theApp.GetStartupMetadataLoadState(CemuleApp::StartupMetadataKnownFiles);
		if (state.m_uProgressTotal > 0) {
			const ULONGLONG ullRecords = state.m_uProgressTotal;
			uWeight = max(uWeight, ClampStartupTaskWeight(250ULL + ullRecords / 4ULL + (ullRecords * ullRecords) / 2500000ULL, 250));
		}

		if (m_pOwner != NULL && m_pOwner->m_pPendingStartupKnownFilesLoadResult != NULL) {
			const SStartupKnownFilesLoadResult* pResult = static_cast<const SStartupKnownFilesLoadResult*>(m_pOwner->m_pPendingStartupKnownFilesLoadResult);
			if (pResult != NULL) {
				const size_t uKnownRecords = pResult->bKnownRecordsParsed ? pResult->vecParsedKnownFiles.size() : (pResult->pKnownRecords != NULL ? pResult->pKnownRecords->size() : 0);
				const size_t uCancelledRecords = pResult->pCancelledRecords != NULL ? pResult->pCancelledRecords->size() : 0;
				const ULONGLONG ullKnownRecords = static_cast<ULONGLONG>(uKnownRecords);
				const ULONGLONG ullCancelledRecords = static_cast<ULONGLONG>(uCancelledRecords);
				const ULONGLONG ullRecordWeight = 250ULL + ullKnownRecords / 4ULL + (ullKnownRecords * ullKnownRecords) / 2500000ULL + ullCancelledRecords / 32ULL;
				uWeight = max(uWeight, ClampStartupTaskWeight(ullRecordWeight, 250));
			}
		}
		return uWeight;
	}


	static UINT ClampStartupProgressValue(size_t uValue)
	{
		const size_t uMax = static_cast<size_t>(static_cast<UINT>(-1));
		return static_cast<UINT>(min(uValue, uMax));
	}

	static UINT ClampStartupProgressValue64(uint64 uValue)
	{
		const uint64 uMax = static_cast<uint64>(static_cast<UINT>(-1));
		return static_cast<UINT>(uValue > uMax ? uMax : uValue);
	}

	static UINT AddStartupProgressValues(size_t uLeft, size_t uRight)
	{
		const size_t uMax = static_cast<size_t>(static_cast<UINT>(-1));
		if (uLeft > uMax - min(uRight, uMax))
			return static_cast<UINT>(-1);
		return ClampStartupProgressValue(uLeft + uRight);
	}

	bool TryGetDownloadsApplyProgress(UINT& uDone, UINT& uTotal) const
	{
		if (m_pOwner == NULL || m_pOwner->m_pPendingStartupDownloadsLoadResult == NULL)
			return false;
		const CDownloadQueue::SStartupDownloadLoadResult* pResult = static_cast<const CDownloadQueue::SStartupDownloadLoadResult*>(m_pOwner->m_pPendingStartupDownloadsLoadResult);
		if (pResult == NULL || !pResult->bSuccess)
			return false;

		const size_t uTotalFiles = pResult->vecPartFiles.size();
		const size_t uDoneFiles = min(pResult->uNextPartFile, uTotalFiles);
		uDone = ClampStartupProgressValue(uDoneFiles);
		uTotal = max(1U, ClampStartupProgressValue(uTotalFiles));
		return true;
	}

	bool TryGetStoredSearchesApplyProgress(UINT& uDone, UINT& uTotal) const
	{
		if (m_pOwner == NULL || m_pOwner->m_pPendingStartupStoredSearchesLoadResult == NULL)
			return false;
		const CSearchList::SStartupStoredSearchesLoadResult* pResult = static_cast<const CSearchList::SStartupStoredSearchesLoadResult*>(m_pOwner->m_pPendingStartupStoredSearchesLoadResult);
		if (pResult == NULL || !pResult->bSuccess)
			return false;

		UINT uLoadedSearches = 0;
		UINT uTotalSearches = 0;
		UINT uLoadedFiles = 0;
		if (theApp.searchlist != NULL)
			theApp.searchlist->GetStartupLoadProgress(uLoadedSearches, uTotalSearches, uLoadedFiles);
		uDone = uLoadedFiles;
		uTotal = max(1U, pResult->uTotalFiles);
		(void)uLoadedSearches;
		(void)uTotalSearches;
		return true;
	}

	bool TryGetKnownFilesApplyProgress(UINT& uDone, UINT& uTotal) const
	{
		if (m_pOwner == NULL || m_pOwner->m_pPendingStartupKnownFilesLoadResult == NULL)
			return false;
		const SStartupKnownFilesLoadResult* pResult = static_cast<const SStartupKnownFilesLoadResult*>(m_pOwner->m_pPendingStartupKnownFilesLoadResult);
		if (pResult == NULL || !pResult->bSuccess)
			return false;

		if (!pResult->bKnownRecordsParsed) {
			const size_t uKnownTotal = pResult->pKnownRecords != NULL ? pResult->pKnownRecords->size() : 0;
			const size_t uKnownDone = min(pResult->uNextKnownRecord, uKnownTotal);
			uDone = ScaleStartupKnownAttachProgress(1000U, uKnownDone, uKnownTotal);
			uTotal = StartupProgressUnits;
			return true;
		}

		const size_t uKnownTotal = pResult->vecParsedKnownFiles.size();
		const uint64 uKnownWorkTotal = pResult->uKnownFileWorkUnitsTotal != 0 ? pResult->uKnownFileWorkUnitsTotal : static_cast<uint64>(uKnownTotal);
		const uint64 uKnownWorkDone = min(pResult->uKnownFileWorkUnitsApplied, uKnownWorkTotal);
		const size_t uCancelledTotal = pResult->pCancelledRecords != NULL ? pResult->pCancelledRecords->size() : 0;
		const size_t uCancelledDone = pResult->bCompletionStarted ? min(pResult->uNextCancelledRecord, uCancelledTotal) : 0;
		const bool bHasKnownRecords = uKnownTotal > 0;
		const UINT uKnownSpan = bHasKnownRecords ? (uCancelledTotal > 0 ? 8500U : 9000U) : 0U;
		const UINT uCancelledSpan = 9000U - uKnownSpan;
		uDone = 1000U + ScaleStartupProgress(uKnownSpan, ClampStartupProgressValue64(uKnownWorkDone), max(1U, ClampStartupProgressValue64(uKnownWorkTotal))) + ScaleStartupProgress(uCancelledSpan, ClampStartupProgressValue(uCancelledDone), max(1U, ClampStartupProgressValue(uCancelledTotal)));
		uTotal = StartupProgressUnits;
		return true;
	}

	bool TryGetClientHistoryApplyProgress(UINT& uDone, UINT& uTotal) const
	{
		if (m_pOwner == NULL || m_pOwner->m_pPendingStartupClientHistoryLoadResult == NULL)
			return false;
		const SStartupClientHistoryLoadResult* pResult = static_cast<const SStartupClientHistoryLoadResult*>(m_pOwner->m_pPendingStartupClientHistoryLoadResult);
		if (pResult == NULL || !pResult->bSuccess)
			return false;

		if (pResult->pRecords != NULL) {
			const size_t uRecordTotal = pResult->pRecords->size();
			const size_t uRecordDone = min(pResult->uNextRecord, uRecordTotal);
			uDone = ClampStartupProgressValue(uRecordDone);
			uTotal = max(1U, ClampStartupProgressValue(uRecordTotal));
			return true;
		}

		uDone = min(99U, 20U + pResult->uCompletionStep * 12U);
		uTotal = 100;
		return true;
	}

	bool TryGetSharedCacheLoadProgress(const CemuleApp::SStartupMetadataLoadState& state, UINT& uDone, UINT& uTotal) const
	{
		if (state.m_uProgressTotal > 0) {
			if (theApp.KnownFilesReady())
				SetTaskPhaseProgress(8500, 1500, state.m_uProgressDone, state.m_uProgressTotal, uDone, uTotal);
			else
				SetTaskPhaseProgress(0, StartupProgressUnits, state.m_uProgressDone, state.m_uProgressTotal, uDone, uTotal);
			return true;
		}

		const CemuleApp::SStartupMetadataLoadState knownState = theApp.GetStartupMetadataLoadState(CemuleApp::StartupMetadataKnownFiles);
		const EStartupLoadTaskState eKnownState = MapMetadataState(knownState.m_eState);
		if (eKnownState == StartupLoadTaskLoading && knownState.m_uProgressTotal > 0) {
			SetTaskPhaseProgress(0, 8500, knownState.m_uProgressDone, knownState.m_uProgressTotal, uDone, uTotal);
			return true;
		}
		if (eKnownState == StartupLoadTaskApplying) {
			UINT uKnownDone = 0;
			UINT uKnownTotal = StartupProgressUnits;
			if (TryGetKnownFilesApplyProgress(uKnownDone, uKnownTotal)) {
				SetTaskPhaseProgress(0, 8500, uKnownDone, max(1U, uKnownTotal), uDone, uTotal);
				return true;
			}
		}
		if (eKnownState == StartupLoadTaskLoading || eKnownState == StartupLoadTaskApplying) {
			SetTaskUnitProgress(100, uDone, uTotal);
			return true;
		}
		return false;
	}

	bool TryGetMetadataApplyProgress(CemuleApp::EStartupMetadataDomain eDomain, UINT& uDone, UINT& uTotal) const
	{
		switch (eDomain) {
		case CemuleApp::StartupMetadataDownloads:
			return TryGetDownloadsApplyProgress(uDone, uTotal);
		case CemuleApp::StartupMetadataStoredSearches:
			return TryGetStoredSearchesApplyProgress(uDone, uTotal);
		case CemuleApp::StartupMetadataKnownFiles:
			return TryGetKnownFilesApplyProgress(uDone, uTotal);
		case CemuleApp::StartupMetadataClientHistory:
			return TryGetClientHistoryApplyProgress(uDone, uTotal);
		default:
			break;
		}
		return false;
	}

	void AddTask(EStartupLoadTaskMetricId eMetricId, const CString& strTitle, EStartupLoadTaskState eState, UINT uDone, UINT uTotal, UINT uWeight = 100)
	{
		SStartupLoadTaskProgress task;
		task.eMetricId = eMetricId;
		task.strTitle = strTitle;
		task.eState = eState;
		task.uDone = uTotal > 0 ? min(uDone, uTotal) : uDone;
		task.uTotal = uTotal;
		task.uWeight = max(1U, uWeight);
		m_tasks.push_back(task);
	}

	void AddMetadataTask(CemuleApp::EStartupMetadataDomain eDomain, LPCTSTR pszTitleKey, LPCTSTR pszDefaultTitle)
	{
		const CemuleApp::SStartupMetadataLoadState state = theApp.GetStartupMetadataLoadState(eDomain);
		const EStartupLoadTaskState eTaskState = MapMetadataState(state.m_eState);
		UINT uDone = 0;
		UINT uTotal = StartupProgressUnits;
		if (eTaskState == StartupLoadTaskReady || eTaskState == StartupLoadTaskFailed || eTaskState == StartupLoadTaskCancelled)
			SetTaskUnitProgress(StartupProgressUnits, uDone, uTotal);
		else if (eDomain == CemuleApp::StartupMetadataKnownFiles && eTaskState == StartupLoadTaskApplying && TryGetMetadataApplyProgress(eDomain, uDone, uTotal))
			SetTaskPhaseProgress(8000, 2000, uDone, max(1U, uTotal), uDone, uTotal);
		else if (eDomain == CemuleApp::StartupMetadataKnownFiles && eTaskState == StartupLoadTaskLoading && state.m_uProgressTotal > 0)
			SetTaskPhaseProgress(0, 8000, state.m_uProgressDone, state.m_uProgressTotal, uDone, uTotal);
		else if (eDomain == CemuleApp::StartupMetadataSharedRules && eTaskState == StartupLoadTaskLoading && TryGetSharedCacheLoadProgress(state, uDone, uTotal)) {
		}
		else if (eTaskState == StartupLoadTaskApplying && TryGetMetadataApplyProgress(eDomain, uDone, uTotal))
			SetTaskPhaseProgress(6000, 4000, uDone, max(1U, uTotal), uDone, uTotal);
		else if (eTaskState == StartupLoadTaskLoading && state.m_uProgressTotal > 0)
			SetTaskPhaseProgress(0, 6000, state.m_uProgressDone, state.m_uProgressTotal, uDone, uTotal);
		else if (eTaskState == StartupLoadTaskLoading || eTaskState == StartupLoadTaskApplying)
			SetTaskUnitProgress(100, uDone, uTotal);
		AddTask(MapMetadataTaskMetric(eDomain), GetDlgResString(pszTitleKey, pszDefaultTitle), eTaskState, uDone, uTotal, eDomain == CemuleApp::StartupMetadataKnownFiles ? GetKnownFilesTaskWeight() : GetMetadataTaskWeight(eDomain));
	}

	void CollectTasks()
	{
		m_tasks.clear();

		if (theApp.downloadqueue != NULL) {
			UINT uLoaded = 0;
			UINT uTempDirIndex = 0;
			UINT uTempDirCount = 0;
			theApp.downloadqueue->GetStartupLoadProgress(uLoaded, uTempDirIndex, uTempDirCount);
			const bool bCompleted = theApp.downloadqueue->IsStartupLoadCompleted();
			EStartupLoadTaskState eState = bCompleted ? StartupLoadTaskReady : (theApp.downloadqueue->IsStartupLoadActive() ? StartupLoadTaskLoading : StartupLoadTaskWaiting);
			UINT uDone = 0;
			UINT uTotal = StartupProgressUnits;
			if (bCompleted)
				SetTaskUnitProgress(StartupProgressUnits, uDone, uTotal);
			else if (eState == StartupLoadTaskWaiting)
				SetTaskUnitProgress(0, uDone, uTotal);
			else if (TryGetDownloadsApplyProgress(uDone, uTotal)) {
				eState = StartupLoadTaskApplying;
				SetTaskPhaseProgress(6000, 4000, uDone, max(1U, uTotal), uDone, uTotal);
			}
			else
				SetTaskPhaseProgress(0, 6000, uTempDirIndex, max(1U, uTempDirCount), uDone, uTotal);
			const UINT uDownloadWeight = max(160U, AddStartupTaskWeights(uTempDirCount * 50U, uLoaded * 12U));
			AddTask(StartupLoadTaskMetricDownloads, GetDlgResString(_T("LOAD_DOWNLOADS"), _T("Loading downloads")), eState, uDone, uTotal, uDownloadWeight);
		}

		if (thePrefs.IsStoringSearchesEnabled() && theApp.searchlist != NULL) {
			UINT uLoadedSearches = 0;
			UINT uTotalSearches = 0;
			UINT uLoadedFiles = 0;
			theApp.searchlist->GetStartupLoadProgress(uLoadedSearches, uTotalSearches, uLoadedFiles);
			const bool bCompleted = theApp.searchlist->IsStartupLoadCompleted();
			EStartupLoadTaskState eState = bCompleted ? StartupLoadTaskReady : (theApp.searchlist->IsStartupLoadActive() ? StartupLoadTaskLoading : StartupLoadTaskWaiting);
			UINT uDone = 0;
			UINT uTotal = StartupProgressUnits;
			if (bCompleted)
				SetTaskUnitProgress(StartupProgressUnits, uDone, uTotal);
			else if (TryGetStoredSearchesApplyProgress(uDone, uTotal)) {
				eState = StartupLoadTaskApplying;
				SetTaskPhaseProgress(6000, 4000, uDone, max(1U, uTotal), uDone, uTotal);
			}
			else
				SetTaskPhaseProgress(0, 6000, uLoadedSearches, max(1U, uTotalSearches), uDone, uTotal);
			UINT uSearchWeight = GetMetadataTaskWeight(CemuleApp::StartupMetadataStoredSearches);
			if (uLoadedFiles > 0)
				uSearchWeight = max(uSearchWeight, ClampStartupTaskWeight(80ULL + static_cast<ULONGLONG>(uLoadedFiles) / 8ULL, 80));
			AddTask(StartupLoadTaskMetricStoredSearches, GetDlgResString(_T("STARTUP_LOAD_TASK_SEARCHES"), _T("Loading stored searches")), eState, uDone, uTotal, uSearchWeight);
		}

		AddMetadataTask(CemuleApp::StartupMetadataKnownFiles, _T("BULKOP_LOAD_KNOWNFILES_TITLE"), _T("Loading known files"));
		if (thePrefs.GetClientHistory())
			AddMetadataTask(CemuleApp::StartupMetadataClientHistory, _T("LOAD_CLIENTHISTORY"), _T("Loading client history"));
		AddMetadataTask(CemuleApp::StartupMetadataSharedRules, _T("STARTUP_LOAD_TASK_SHAREDCACHE"), _T("Loading shared cache"));

	}

	void ApplyMonotonicTaskProgress()
	{
		for (size_t i = 0; i < m_tasks.size(); ++i) {
			SStartupLoadTaskProgress& task = m_tasks[i];
			if (task.eMetricId >= StartupLoadTaskMetricCount)
				continue;

			SStartupLoadTaskVisualState& visual = m_taskVisualStates[task.eMetricId];
			if (IsTerminalTaskState(task.eState)) {
				visual.bTerminal = true;
				visual.eTerminalState = task.eState;
				visual.uLastUnits = StartupProgressUnits;
				SetTaskUnitProgress(StartupProgressUnits, task.uDone, task.uTotal);
				continue;
			}

			if (visual.bTerminal) {
				task.eState = visual.eTerminalState;
				SetTaskUnitProgress(StartupProgressUnits, task.uDone, task.uTotal);
				continue;
			}

			UINT uUnits = GetTaskProgressUnits(task);
			if (uUnits < visual.uLastUnits)
				uUnits = visual.uLastUnits;
			if (uUnits >= StartupProgressUnits)
				uUnits = StartupProgressUnits - 1;
			if (task.eState == StartupLoadTaskWaiting && uUnits > 0)
				task.eState = StartupLoadTaskLoading;
			visual.uLastUnits = uUnits;
			SetTaskUnitProgress(uUnits, task.uDone, task.uTotal);
		}
	}

	void UpdateAdaptiveTaskWeights()
	{
		const DWORD dwNow = ::GetTickCount();
		for (size_t i = 0; i < m_tasks.size(); ++i) {
			SStartupLoadTaskProgress& task = m_tasks[i];
			if (task.eMetricId >= StartupLoadTaskMetricCount)
				continue;

			SStartupLoadTaskTimeModel& model = m_taskTimeModels[task.eMetricId];
			const bool bActive = task.eState == StartupLoadTaskLoading || task.eState == StartupLoadTaskApplying;
			const bool bTerminal = task.eState == StartupLoadTaskReady || task.eState == StartupLoadTaskFailed || task.eState == StartupLoadTaskCancelled;
			if (bActive && !model.bObserved) {
				model.bObserved = true;
				model.bCompleted = false;
				model.dwFirstActiveTick = dwNow;
				model.dwCompletedTick = 0;
			}

			if (bTerminal && model.bObserved && !model.bCompleted) {
				model.bCompleted = true;
				model.dwCompletedTick = dwNow;
			}

			UINT uAdaptiveWeight = 0;
			if (model.bCompleted && model.dwFirstActiveTick != 0 && model.dwCompletedTick != 0) {
				const DWORD dwElapsed = (static_cast<DWORD>(model.dwCompletedTick - model.dwFirstActiveTick) < 150 ? 150 : static_cast<DWORD>(model.dwCompletedTick - model.dwFirstActiveTick));
				uAdaptiveWeight = EstimateStartupTimeWeight(dwElapsed, 20);
			}
			else if (bActive && model.bObserved && model.dwFirstActiveTick != 0) {
				const UINT uProgressUnits = GetTaskProgressUnits(task);
				if (uProgressUnits >= 200) {
					const DWORD dwElapsed = (static_cast<DWORD>(dwNow - model.dwFirstActiveTick) < 150 ? 150 : static_cast<DWORD>(dwNow - model.dwFirstActiveTick));
					const ULONGLONG ullEstimatedTotalMs = (static_cast<ULONGLONG>(dwElapsed) * StartupProgressUnits) / max(200U, uProgressUnits);
					uAdaptiveWeight = EstimateStartupTimeWeight(ullEstimatedTotalMs, 20);
				}
			}

			if (uAdaptiveWeight != 0)
				task.uWeight = uAdaptiveWeight;
		}
	}

	void RefreshProgress(bool bForce)
	{
		std::vector<SStartupLoadTaskProgress> oldTasks = m_tasks;
		UINT uOldDone = m_uDone;
		UINT uOldTotal = m_uTotal;

		CollectTasks();
		UpdateAdaptiveTaskWeights();
		ApplyMonotonicTaskProgress();
		UINT uWeightedDone = 0;
		UINT uWeightedTotal = 0;
		for (size_t i = 0; i < m_tasks.size(); ++i) {
			const UINT uWeight = max(1U, m_tasks[i].uWeight);
			uWeightedTotal += uWeight;
			uWeightedDone += ScaleStartupProgress(uWeight, m_tasks[i].uDone, max(1U, m_tasks[i].uTotal));
		}

		UINT uOverallUnits = uWeightedTotal > 0 ? ScaleStartupProgress(StartupProgressUnits, uWeightedDone, uWeightedTotal) : 0;
		if (uOverallUnits < m_uDisplayedOverallUnits)
			uOverallUnits = m_uDisplayedOverallUnits;
		else
			m_uDisplayedOverallUnits = uOverallUnits;
		m_uDone = uOverallUnits;
		m_uTotal = StartupProgressUnits;

		if (!bForce && uOldDone == m_uDone && uOldTotal == m_uTotal && oldTasks.size() == m_tasks.size()) {
			bool bSame = true;
			for (size_t i = 0; i < m_tasks.size(); ++i) {
				if (oldTasks[i].eMetricId != m_tasks[i].eMetricId || oldTasks[i].strTitle != m_tasks[i].strTitle || oldTasks[i].eState != m_tasks[i].eState || oldTasks[i].uDone != m_tasks[i].uDone || oldTasks[i].uTotal != m_tasks[i].uTotal || oldTasks[i].uWeight != m_tasks[i].uWeight) {
					bSame = false;
					break;
				}
			}
			if (bSame)
				return;
		}

		if (::IsWindow(m_hWnd))
			Invalidate(FALSE);
	}

	void ObserveHeartbeat(LPCTSTR pszTimerName)
	{
		const DWORD dwNow = ::GetTickCount();
		if (m_dwLastHeartbeatTick != 0) {
			const DWORD dwElapsed = static_cast<DWORD>(dwNow - m_dwLastHeartbeatTick);
			if (dwElapsed > StartupHeartbeatMaxMs && (m_dwLastHeartbeatTraceTick == 0 || static_cast<DWORD>(dwNow - m_dwLastHeartbeatTraceTick) >= StartupHeartbeatTraceMs)) {
				m_dwLastHeartbeatTraceTick = dwNow;
				AddDebugLogLine(DLP_LOW, false, _T("Startup loading dialog heartbeat exceeded. timer=%s elapsed=%u threshold=%u\n"), pszTimerName != NULL ? pszTimerName : _T("unknown"), dwElapsed, static_cast<UINT>(StartupHeartbeatMaxMs));
			}
		}
		m_dwLastHeartbeatTick = dwNow;
	}

	CRect GetInitialDialogWindowRect() const
	{
		const int iWidth = OverlayBaseWindowWidth;
		const int iTaskCount = static_cast<int>(m_tasks.size());
		const int iHeight = max(196, 120 + iTaskCount * 24);
		CRect rcAnchor;
		if (m_pOwner != NULL && ::IsWindow(m_pOwner->GetSafeHwnd()))
			m_pOwner->GetWindowRect(rcAnchor);
		else
			::SystemParametersInfo(SPI_GETWORKAREA, 0, &rcAnchor, 0);

		HMONITOR hMonitor = ::MonitorFromRect(rcAnchor, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi;
		ZeroMemory(&mi, sizeof(mi));
		mi.cbSize = sizeof(mi);
		CRect rcWork;
		if (hMonitor != NULL && ::GetMonitorInfo(hMonitor, &mi))
			rcWork = mi.rcWork;
		else
			::SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);

		return CRect(rcWork.left + (rcWork.Width() - iWidth) / 2, rcWork.top + (rcWork.Height() - iHeight) / 2, rcWork.left + (rcWork.Width() + iWidth) / 2, rcWork.top + (rcWork.Height() + iHeight) / 2);
	}

	CRect GetContentSizedDialogWindowRect(const CRect& rcInitial)
	{
		CClientDC dc(this);
		CFont* pFont = GetFont();
		CFont* pOldFont = pFont != NULL ? dc.SelectObject(pFont) : NULL;
		static const LPCTSTR s_apszTaskKeys[] = {
			_T("LOAD_DOWNLOADS"),
			_T("STARTUP_LOAD_TASK_SEARCHES"),
			_T("BULKOP_LOAD_KNOWNFILES_TITLE"),
			_T("LOAD_CLIENTHISTORY"),
			_T("STARTUP_LOAD_TASK_SHAREDCACHE")
		};
		static const LPCTSTR s_apszTaskDefaults[] = {
			_T("Loading downloads"),
			_T("Loading stored searches"),
			_T("Loading known files"),
			_T("Loading client history"),
			_T("Loading shared cache")
		};
		int iMaxTaskTextWidth = 0;
		for (size_t i = 0; i < _countof(s_apszTaskKeys); ++i)
			iMaxTaskTextWidth = max(iMaxTaskTextWidth, MeasureOverlaySingleLineTextWidth(dc, GetDlgResString(s_apszTaskKeys[i], s_apszTaskDefaults[i])));
		const int iTitleTextWidth = MeasureOverlaySingleLineTextWidth(dc, GetDlgResString(_T("STARTUP_LOAD_TITLE"), _T("Starting eMule AI")));
		if (pOldFont != NULL)
			dc.SelectObject(pOldFont);

		m_iTaskTitleWidth = max(OverlayBaseTaskTitleWidth, iMaxTaskTextWidth + OverlayTextPadding);
		int iDesiredWidth = OverlayPanelPadding * 2 + OverlayTaskTitleInset + m_iTaskTitleWidth + OverlayTaskTitleToProgressGap + OverlayTaskProgressWidth;
		iDesiredWidth = max(iDesiredWidth, OverlayPanelPadding + iTitleTextWidth + OverlayStartupCaptionReservedWidth + OverlayTextPadding);

		HMONITOR hMonitor = ::MonitorFromRect(rcInitial, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi;
		ZeroMemory(&mi, sizeof(mi));
		mi.cbSize = sizeof(mi);
		CRect rcWork;
		if (hMonitor != NULL && ::GetMonitorInfo(hMonitor, &mi))
			rcWork = mi.rcWork;
		else
			::SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);

		const int iMaxWidth = max(1, rcWork.Width() - OverlayWorkAreaMargin * 2);
		const int iWidth = min(max(OverlayBaseWindowWidth, iDesiredWidth), iMaxWidth);
		const int iFixedRowWidth = OverlayPanelPadding * 2 + OverlayTaskTitleInset + OverlayTaskTitleToProgressGap + OverlayTaskProgressWidth;
		m_iTaskTitleWidth = min(m_iTaskTitleWidth, max(32, iWidth - iFixedRowWidth));
		const int iHeight = rcInitial.Height();
		return CRect(rcWork.left + (rcWork.Width() - iWidth) / 2, rcWork.top + (rcWork.Height() - iHeight) / 2, rcWork.left + (rcWork.Width() + iWidth) / 2, rcWork.top + (rcWork.Height() + iHeight) / 2);
	}

	CRect GetPanelRect(const CRect& rcClient) const
	{
		return rcClient;
	}

	void ApplyRoundedWindowRegion(int iWidth, int iHeight)
	{
		if (!::IsWindow(m_hWnd))
			return;

		HRGN hRgn = ::CreateRoundRectRgn(0, 0, max(1, iWidth) + 1, max(1, iHeight) + 1, StartupWindowCornerDiameter, StartupWindowCornerDiameter);
		if (hRgn == NULL)
			return;
		if (::SetWindowRgn(m_hWnd, hRgn, TRUE) == 0)
			::DeleteObject(hRgn);
	}

	void SyncToOwnerRect()
	{
		if (!::IsWindow(m_hWnd) || IsIconic())
			return;
	}

	void DrawBackground(CDC* pDC, const CRect& rcClient)
	{
		const bool bDark = IsDarkModeEnabled();
		const COLORREF crTop = bDark ? RGB(18, 21, 29) : RGB(237, 243, 252);
		const COLORREF crBottom = bDark ? RGB(36, 42, 58) : RGB(250, 252, 255);
		const int iHeight = max(1, rcClient.Height());
		for (int y = 0; y < iHeight; ++y) {
			const int iFrom = iHeight - y;
			const COLORREF crLine = RGB((GetRValue(crTop) * iFrom + GetRValue(crBottom) * y) / iHeight, (GetGValue(crTop) * iFrom + GetGValue(crBottom) * y) / iHeight, (GetBValue(crTop) * iFrom + GetBValue(crBottom) * y) / iHeight);
			pDC->FillSolidRect(rcClient.left, rcClient.top + y, rcClient.Width(), 1, crLine);
		}
	}

	afx_msg void OnPaint()
	{
		CPaintDC dcPaint(this);
		CRect rcClient;
		GetClientRect(rcClient);
		CDC dcMem;
		dcMem.CreateCompatibleDC(&dcPaint);
		CBitmap bmpMem;
		bmpMem.CreateCompatibleBitmap(&dcPaint, max(1, rcClient.Width()), max(1, rcClient.Height()));
		CBitmap* pOldBitmap = dcMem.SelectObject(&bmpMem);
		DrawDialog(&dcMem, rcClient);
		dcPaint.BitBlt(0, 0, rcClient.Width(), rcClient.Height(), &dcMem, 0, 0, SRCCOPY);
		dcMem.SelectObject(pOldBitmap);
	}

	afx_msg BOOL OnEraseBkgnd(CDC*)
	{
		return TRUE;
	}

	afx_msg void OnSize(UINT nType, int cx, int cy)
	{
		CWnd::OnSize(nType, cx, cy);
		ApplyRoundedWindowRegion(cx, cy);
	}

	afx_msg void OnTimer(UINT_PTR nIDEvent)
	{
		if (nIDEvent == TimerRefresh) {
			ObserveHeartbeat(_T("refresh"));
			SyncToOwnerRect();
			if (m_pOwner != NULL && !m_pOwner->IsInitializing()) {
				ShowWindow(SW_HIDE);
				return;
			}
			RefreshProgress(false);
			return;
		}
		if (nIDEvent == TimerAnimation) {
			ObserveHeartbeat(_T("animation"));
			m_uAnimationPhase = (m_uAnimationPhase + 18) % 1536;
			Invalidate(FALSE);
			return;
		}
		CWnd::OnTimer(nIDEvent);
	}

	afx_msg void OnLButtonDown(UINT nFlags, CPoint point)
	{
		if (HitTestButton(point) == 0 && point.y < 56) {
			SendMessage(WM_NCLBUTTONDOWN, HTCAPTION, 0);
			return;
		}
		CWnd::OnLButtonDown(nFlags, point);
	}

	afx_msg void OnLButtonUp(UINT, CPoint point)
	{
		switch (HitTestButton(point)) {
			case ButtonMinimize:
				ShowWindow(SW_MINIMIZE);
				break;
			case ButtonClose:
				RequestCancelAndExit();
				break;
		}
	}

	afx_msg void OnClose()
	{
		RequestCancelAndExit();
	}

	afx_msg void OnMouseMove(UINT nFlags, CPoint point)
	{
		const UINT uButton = HitTestButton(point);
		if (m_uHoverButton != uButton) {
			m_uHoverButton = uButton;
			Invalidate(FALSE);
		}

		if (!m_bTrackingMouse) {
			TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hWnd, 0 };
			m_bTrackingMouse = ::TrackMouseEvent(&tme) != FALSE;
		}
		CWnd::OnMouseMove(nFlags, point);
	}

	afx_msg void OnMouseLeave()
	{
		m_bTrackingMouse = false;
		if (m_uHoverButton != 0) {
			m_uHoverButton = 0;
			Invalidate(FALSE);
		}
	}

	afx_msg BOOL OnSetCursor(CWnd*, UINT, UINT)
	{
		CPoint point;
		::GetCursorPos(&point);
		ScreenToClient(&point);
		if (HitTestButton(point) != 0) {
			::SetCursor(::LoadCursor(NULL, IDC_HAND));
			return TRUE;
		}
		::SetCursor(::LoadCursor(NULL, IDC_ARROW));
		return TRUE;
	}

	virtual BOOL PreTranslateMessage(MSG* pMsg)
	{
		if (pMsg != NULL && pMsg->message == WM_KEYDOWN) {
			if (pMsg->wParam == VK_ESCAPE) {
				RequestCancelAndExit();
				return TRUE;
			}
			if (pMsg->wParam == VK_RETURN)
				return TRUE;
		}
		return CWnd::PreTranslateMessage(pMsg);
	}

	void DrawProgressBarWithPercent(CDC* pDC, const CRect& rcProgress, UINT uPercent, COLORREF crEdge, COLORREF crFill, COLORREF crBack, COLORREF crText)
	{
		pDC->FillSolidRect(rcProgress, crBack);
		const int iFillWidth = min(rcProgress.Width(), max(0, MulDiv(rcProgress.Width(), min(100U, uPercent), 100)));
		if (iFillWidth > 0) {
			CRect rcFill(rcProgress);
			rcFill.right = rcFill.left + iFillWidth;
			pDC->FillSolidRect(rcFill, crFill);
		}

		CPen penProgress(PS_SOLID, 1, crEdge);
		CPen* pOldPen = pDC->SelectObject(&penProgress);
		pDC->MoveTo(rcProgress.left, rcProgress.top);
		pDC->LineTo(rcProgress.right, rcProgress.top);
		pDC->LineTo(rcProgress.right, rcProgress.bottom);
		pDC->LineTo(rcProgress.left, rcProgress.bottom);
		pDC->LineTo(rcProgress.left, rcProgress.top);
		pDC->SelectObject(pOldPen);

		CString strPercent;
		strPercent.Format(_T("%u%%"), min(100U, uPercent));
		CRect rcText(rcProgress);
		pDC->SetTextColor(crText);
		pDC->DrawText(strPercent, rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	}

	void DrawDialog(CDC* pDC, const CRect& rcClient)
	{
		const bool bDark = IsDarkModeEnabled();
		const COLORREF crPanel = bDark ? RGB(30, 33, 42) : RGB(250, 252, 255);
		const COLORREF crPanelEdge = bDark ? RGB(75, 82, 102) : RGB(196, 207, 224);
		const COLORREF crTitle = bDark ? RGB(242, 245, 255) : RGB(24, 35, 56);
		const COLORREF crText = bDark ? RGB(218, 224, 240) : RGB(42, 52, 72);
		const COLORREF crSoft = bDark ? RGB(167, 180, 210) : RGB(88, 101, 124);
		const COLORREF crProgressFill = RGB(76, 132, 232);
		const COLORREF crProgressBack = bDark ? RGB(54, 60, 74) : RGB(224, 231, 242);
		pDC->FillSolidRect(rcClient, crPanel);

		CRect rcPanel(GetPanelRect(rcClient));

		CRgn rgnPanel;
		rgnPanel.CreateRoundRectRgn(rcPanel.left, rcPanel.top, rcPanel.right + 1, rcPanel.bottom + 1, StartupWindowCornerDiameter, StartupWindowCornerDiameter);
		CBrush brPanel(crPanel);
		pDC->FillRgn(&rgnPanel, &brPanel);
		CPen penEdge(PS_SOLID, 1, crPanelEdge);
		CPen* pOldPen = pDC->SelectObject(&penEdge);
		CGdiObject* pOldBrush = pDC->SelectStockObject(NULL_BRUSH);
		pDC->RoundRect(rcPanel, CPoint(StartupWindowCornerDiameter, StartupWindowCornerDiameter));
		DrawAnimatedRainbowBorder(pDC, rcPanel, m_uAnimationPhase, StartupBorderThickness, StartupWindowCornerDiameter);

		CFont* pOldFont = pDC->SelectObject(GetFont());
		pDC->SetBkMode(TRANSPARENT);
		LayoutCaptionButtons(rcPanel);
		DrawCaptionButton(pDC, m_rcMinimize, ButtonMinimize, bDark);
		DrawCaptionButton(pDC, m_rcClose, ButtonClose, bDark);

		const int iPad = 24;
		CRect rcTitle(rcPanel.left + iPad, rcPanel.top + 20, m_rcMinimize.left - 12, rcPanel.top + 42);
		pDC->SetTextColor(crTitle);
		pDC->DrawText(GetDlgResString(_T("STARTUP_LOAD_TITLE"), _T("Starting eMule AI")), rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

		const UINT uOverallPercent = m_uTotal > 0 ? min(100U, ScaleStartupProgress(100U, m_uDone, m_uTotal)) : 0U;
		CRect rcProgress(rcPanel.left + iPad, rcTitle.bottom + 16, rcPanel.right - iPad, rcTitle.bottom + 32);
		DrawProgressBarWithPercent(pDC, rcProgress, uOverallPercent, crPanelEdge, crProgressFill, crProgressBack, crText);

		CRect rcTask(rcPanel.left + iPad, rcProgress.bottom + 17, rcPanel.right - iPad, rcProgress.bottom + 40);
		const int iTaskBottom = rcPanel.bottom - 16;
		for (size_t i = 0; i < m_tasks.size() && rcTask.bottom <= iTaskBottom; ++i) {
			DrawTaskRow(pDC, rcTask, m_tasks[i], crText, crSoft, crPanelEdge, crProgressFill, crProgressBack);
			rcTask.OffsetRect(0, 24);
		}

		pDC->SelectObject(pOldFont);
		pDC->SelectObject(pOldPen);
		pDC->SelectObject(pOldBrush);
	}

	void DrawTaskRow(CDC* pDC, const CRect& rcRow, const SStartupLoadTaskProgress& task, COLORREF crText, COLORREF crSoft, COLORREF crEdge, COLORREF crFill, COLORREF crBack)
	{
		(void)crSoft;
		COLORREF crDotFill = RGB(212, 82, 82);
		if (task.eState == StartupLoadTaskReady)
			crDotFill = RGB(76, 175, 80);
		else if (task.eState == StartupLoadTaskLoading || task.eState == StartupLoadTaskApplying)
			crDotFill = RGB(232, 192, 48);

		CRect rcDot(rcRow.left, rcRow.top + 8, rcRow.left + 8, rcRow.top + 16);
		CBrush brDot(crDotFill);
		CPen penDot(PS_SOLID, 1, crEdge);
		CPen* pOldPen = pDC->SelectObject(&penDot);
		CBrush* pOldBrush = pDC->SelectObject(&brDot);
		pDC->Ellipse(rcDot);
		pDC->SelectObject(pOldBrush);
		pDC->SelectObject(pOldPen);

		const int iTitleLeft = rcRow.left + OverlayTaskTitleInset;
		const int iProgressRight = rcRow.right;
		const int iProgressLeft = min(iTitleLeft + m_iTaskTitleWidth + OverlayTaskTitleToProgressGap, iProgressRight - OverlayMinimumTaskProgressWidth);

		CRect rcTitle(iTitleLeft, rcRow.top, max(iTitleLeft, iProgressLeft - OverlayTaskTitleToProgressGap), rcRow.bottom);
		CRect rcMiniProgress(iProgressLeft, rcRow.top + 4, iProgressRight, rcRow.top + 20);

		pDC->SetTextColor(crText);
		pDC->DrawText(task.strTitle, rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

		DrawProgressBarWithPercent(pDC, rcMiniProgress, GetTaskPercent(task), crEdge, crFill, crBack, crText);
	}

	void LayoutCaptionButtons(const CRect& rcPanel)
	{
		const int iButtonSize = 20;
		const int iGap = 6;
		const int iTop = rcPanel.top + 16;
		m_rcClose.SetRect(rcPanel.right - 22 - iButtonSize, iTop, rcPanel.right - 22, iTop + iButtonSize);
		m_rcMinimize.SetRect(m_rcClose.left - iGap - iButtonSize, iTop, m_rcClose.left - iGap, iTop + iButtonSize);
	}

	void DrawCaptionButton(CDC* pDC, const CRect& rcButton, UINT uButton, bool bDark)
	{
		const bool bHover = m_uHoverButton == uButton;
		const bool bClose = uButton == ButtonClose;
		const COLORREF crButton = bDark ? (bHover ? RGB(56, 62, 78) : RGB(39, 44, 57)) : (bHover ? RGB(238, 243, 252) : RGB(247, 250, 255));
		const COLORREF crEdge = bHover ? (bClose ? RGB(210, 92, 92) : RGB(96, 150, 238)) : (bDark ? RGB(80, 88, 108) : RGB(190, 203, 224));
		const COLORREF crGlyph = bHover && bClose ? RGB(242, 214, 214) : (bDark ? RGB(224, 232, 248) : RGB(34, 46, 70));
		CBrush brButton(crButton);
		CPen penEdge(PS_SOLID, 1, crEdge);
		CPen* pOldPen = pDC->SelectObject(&penEdge);
		CBrush* pOldBrush = pDC->SelectObject(&brButton);
		pDC->RoundRect(rcButton, CPoint(6, 6));

		CPen penGlyph(PS_SOLID, 1, crGlyph);
		pDC->SelectObject(&penGlyph);
		if (uButton == ButtonMinimize) {
			const int iY = rcButton.CenterPoint().y;
			pDC->MoveTo(rcButton.left + 6, iY);
			pDC->LineTo(rcButton.right - 6, iY);
		}
		else if (uButton == ButtonClose) {
			pDC->MoveTo(rcButton.left + 6, rcButton.top + 6);
			pDC->LineTo(rcButton.right - 6, rcButton.bottom - 6);
			pDC->MoveTo(rcButton.right - 6, rcButton.top + 6);
			pDC->LineTo(rcButton.left + 6, rcButton.bottom - 6);
		}

		pDC->SelectObject(pOldPen);
		pDC->SelectObject(pOldBrush);
	}

	UINT HitTestButton(CPoint point) const
	{
		if (m_rcMinimize.PtInRect(point))
			return ButtonMinimize;
		if (m_rcClose.PtInRect(point))
			return ButtonClose;
		return 0;
	}

	DECLARE_MESSAGE_MAP()

private:
	CemuleDlg* m_pOwner;
	std::vector<SStartupLoadTaskProgress> m_tasks;
	UINT m_uDone;
	UINT m_uTotal;
	UINT m_uDisplayedOverallUnits;
	UINT m_uAnimationPhase;
	UINT m_uHoverButton;
	DWORD m_dwLastHeartbeatTick;
	DWORD m_dwLastHeartbeatTraceTick;
	bool m_bTrackingMouse;
	bool m_bCancelExitPending;
	int m_iTaskTitleWidth;
	SStartupLoadTaskTimeModel m_taskTimeModels[StartupLoadTaskMetricCount];
	SStartupLoadTaskVisualState m_taskVisualStates[StartupLoadTaskMetricCount];
	CRect m_rcMinimize;
	CRect m_rcClose;
};

BEGIN_MESSAGE_MAP(CStartupLoadingDlg, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_CLOSE()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_SETCURSOR()
END_MESSAGE_MAP()

LRESULT CemuleDlg::OnStartupLoadingCancelExit(WPARAM, LPARAM)
{
	if (theApp.IsClosing())
		return 0;

	CStartupLoadingDlg* pStartupDlg = m_pStartupLoadingDlg;

	bool bExit = true;
	if (thePrefs.IsConfirmExitEnabled()) {
		const bool bRestoreRunningState = theApp.m_app_state == APP_STATE_RUNNING;
		if (bRestoreRunningState)
			theApp.m_app_state = APP_STATE_ASKCLOSE;
		CWnd* pConfirmParent = pStartupDlg != NULL && ::IsWindow(pStartupDlg->GetSafeHwnd()) ? static_cast<CWnd*>(pStartupDlg) : static_cast<CWnd*>(this);
		if (!m_bStartupLoadingMainWindowDeferred)
			RestoreWindow();
		ExitBox request(pConfirmParent);
		request.DoModal();
		bExit = !request.WasCancelled();
		if (!bExit) {
			if (bRestoreRunningState && theApp.m_app_state == APP_STATE_ASKCLOSE)
				theApp.m_app_state = APP_STATE_RUNNING;
			if (!theApp.IsClosing() && IsInitializing() && m_pStartupLoadingDlg != NULL && ::IsWindow(m_pStartupLoadingDlg->GetSafeHwnd())) {
				m_pStartupLoadingDlg->Reactivate();
			}
			else {
				HideStartupLoadingDialog();
			}
			return 0;
		}
	}

	m_bStartupLoadingExitRequested = true;
	HideStartupLoadingDialog(false);
	if (theApp.m_app_state != APP_STATE_SHUTTINGDOWN && theApp.m_app_state != APP_STATE_DONE)
		theApp.m_app_state = APP_STATE_ASKCLOSE;
	theApp.PrepareBackendShutdownForDiskIo(_T("startup-loading-dialog-cancel-exit"));
	DrainOwnedStartupAndCollectionMessages(m_hWnd);
	ClearStartupApplyPumpState();
	PostMessage(WM_CLOSE);
	return 0;
}

bool CemuleDlg::ShouldSuppressMainWindowForStartupLoading() const
{
	return m_bStartupLoadingSuppressMainWindow && IsInitializing() && !theApp.IsClosing();
}

void CemuleDlg::ShowStartupLoadingDialog()
{
	if (!IsInitializing() || theApp.IsClosing())
		return;
	m_bStartupLoadingSuppressMainWindow = true;
	if (m_pStartupLoadingDlg != NULL && ::IsWindow(m_pStartupLoadingDlg->GetSafeHwnd())) {
		m_pStartupLoadingDlg->ShowWindow(SW_SHOW);
		m_pStartupLoadingDlg->SetWindowPos(&wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
		return;
	}

	CStartupLoadingDlg* pDlg = new CStartupLoadingDlg();
	if (!pDlg->CreateStartupLoadingDlg(this)) {
		delete pDlg;
		m_bStartupLoadingSuppressMainWindow = false;
		if (m_bStartupLoadingMainWindowDeferred)
			ShowMainWindowAfterStartupLoading();
		else
			ShowWindow(SW_SHOW);
		return;
	}
	m_pStartupLoadingDlg = pDlg;
	SetActiveBulkOperationOverlaysSuppressed(true);
}

void CemuleDlg::DeferMainWindowForStartupLoading(const WINDOWPLACEMENT& wpRestore)
{
	m_wpStartupLoadingRestorePlacement = wpRestore;
	m_wpStartupLoadingRestorePlacement.length = (UINT)sizeof m_wpStartupLoadingRestorePlacement;
	m_bStartupLoadingMainWindowDeferred = true;
	m_bStartupLoadingSuppressMainWindow = true;
}

void CemuleDlg::ShowMainWindowAfterStartupLoading()
{
	if (!m_bStartupLoadingMainWindowDeferred || theApp.IsClosing())
		return;
	m_bStartupLoadingSuppressMainWindow = false;
	m_bStartupLoadingMainWindowDeferred = false;
	const bool bSharedFilesPreparedForShow = activewnd == sharedfileswnd && sharedfileswnd != NULL && ::IsWindow(sharedfileswnd->sharedfilesctrl.GetSafeHwnd());
	if (bSharedFilesPreparedForShow)
		sharedfileswnd->sharedfilesctrl.ReloadListForActivation(LSF_SELECTION);
	WINDOWPLACEMENT wpRestore = m_wpStartupLoadingRestorePlacement;
	wpRestore.length = (UINT)sizeof wpRestore;
	if (wpRestore.showCmd == SW_HIDE)
		wpRestore.showCmd = SW_SHOWNORMAL;
	SetWindowPlacement(&wpRestore);
	ShowWindow(wpRestore.showCmd);
	SetForegroundWindow();
	UpdateWindow();
	if (m_bStartMinimized && !m_bStartMinimizedChecked)
		PostStartupMinimized();
	else if (TrayIconVisible() && IsWindowVisible() && !IsIconic())
		TrayHide();
	UpdateTitleVersionOverlayWindow();
	RefreshSearchResultsAfterStartupKnownTypes();
	if (activewnd == transferwnd && transferwnd != NULL && transferwnd->GetDownloadList() != NULL && ::IsWindow(transferwnd->GetDownloadList()->GetSafeHwnd())) {
		transferwnd->GetDownloadList()->FlushDeferredReload(LSF_SELECTION);
		transferwnd->UpdateCatTabTitles(true);
		transferwnd->VerifyCatTabSize();
	}
	if (!bSharedFilesPreparedForShow && activewnd == sharedfileswnd && sharedfileswnd != NULL && ::IsWindow(sharedfileswnd->sharedfilesctrl.GetSafeHwnd()) && sharedfileswnd->sharedfilesctrl.IsWindowVisible())
		sharedfileswnd->sharedfilesctrl.ReloadList(false, LSF_SELECTION);
	else if (activewnd == searchwnd && searchwnd != NULL && searchwnd->m_pwndResults != NULL)
		searchwnd->m_pwndResults->EnsureActiveTabLoaded();
}

void CemuleDlg::RefreshStartupLoadingDialogProgress(bool bForcePaint)
{
	if (m_pStartupLoadingDlg == NULL || !::IsWindow(m_pStartupLoadingDlg->GetSafeHwnd()))
		return;
	m_pStartupLoadingDlg->RefreshProgressNow(bForcePaint);
	if (bForcePaint)
		m_pStartupLoadingDlg->Invalidate(FALSE);
}

void CemuleDlg::HideStartupLoadingDialog(bool bRestoreOverlays)
{
	CStartupLoadingDlg* pDlg = m_pStartupLoadingDlg;
	m_pStartupLoadingDlg = NULL;
	if (pDlg != NULL) {
		if (::IsWindow(pDlg->GetSafeHwnd()))
			pDlg->DestroyWindow();
		delete pDlg;
	}
	if (bRestoreOverlays)
		SetActiveBulkOperationOverlaysSuppressed(false);
}

bool CemuleDlg::IsStartupLoadingDialogVisible() const
{
	return m_pStartupLoadingDlg != NULL && ::IsWindow(m_pStartupLoadingDlg->GetSafeHwnd()) && m_pStartupLoadingDlg->IsWindowVisible();
}

void CemuleDlg::ShowShutdownProgressDialog()
{
	if (m_pShutdownProgressDlg != NULL && ::IsWindow(m_pShutdownProgressDlg->GetSafeHwnd())) {
		m_pShutdownProgressDlg->ShowWindow(SW_SHOW);
		m_pShutdownProgressDlg->SetWindowPos(&wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
		return;
	}

	HideStartupLoadingDialog(false);
	CShutdownProgressDlg* pDlg = new CShutdownProgressDlg();
	if (!pDlg->CreateShutdownProgressDlg(this)) {
		delete pDlg;
		return;
	}
	m_pShutdownProgressDlg = pDlg;
	ShowWindow(SW_HIDE);
	SetActiveBulkOperationOverlaysSuppressed(true);
}

void CemuleDlg::UpdateShutdownProgress(UINT uStage, UINT uDone, UINT uTotal, bool bForcePaint)
{
	if (m_pShutdownProgressDlg == NULL || !::IsWindow(m_pShutdownProgressDlg->GetSafeHwnd()))
		return;
	m_pShutdownProgressDlg->UpdateProgress(uStage, uDone, uTotal, bForcePaint);
}

void CemuleDlg::ConfigureShutdownProgressEstimates(UINT uDownloadCount, bool bSaveSources)
{
	if (m_pShutdownProgressDlg == NULL || !::IsWindow(m_pShutdownProgressDlg->GetSafeHwnd()))
		return;
	m_pShutdownProgressDlg->ConfigureWorkEstimates(uDownloadCount, bSaveSources);
}

void CemuleDlg::PumpShutdownProgressDialog()
{
	if (m_pShutdownProgressDlg != NULL && ::IsWindow(m_pShutdownProgressDlg->GetSafeHwnd()))
		m_pShutdownProgressDlg->PumpMessages();
}

void CemuleDlg::HideShutdownProgressDialog()
{
	CShutdownProgressDlg* pDlg = m_pShutdownProgressDlg;
	m_pShutdownProgressDlg = NULL;
	if (pDlg != NULL) {
		if (::IsWindow(pDlg->GetSafeHwnd()))
			pDlg->DestroyWindow();
		delete pDlg;
	}
}

bool CemuleDlg::IsShutdownProgressDialogVisible() const
{
	return m_pShutdownProgressDlg != NULL && ::IsWindow(m_pShutdownProgressDlg->GetSafeHwnd()) && m_pShutdownProgressDlg->IsWindowVisible();
}

void CemuleDlg::OnDestroy()
{
	AddDebugLogLine(DLP_VERYLOW, _T("%hs"), __FUNCTION__);
	UnregisterIpGuardNotifications();
	m_wndToastNotifier.Shutdown();
	DrainOwnedStartupAndCollectionMessages(m_hWnd);
	ClearStartupApplyPumpState();
	KillMainTimer();
	KillTimer(TIMER_CLOSE_AFTER_BULK_OPERATIONS);
	KillTimer(TIMER_UI_LOG_FLUSH);
	m_bUiLogFlushTimerActive = false;
	m_bUiLogFlushMessagePending = false;
	if (!m_queuedUiLogLines.empty() && !theApp.IsClosing())
		FlushQueuedUiLogLines();
	m_queuedUiLogLines.clear();
	m_uDroppedQueuedUiLogLines = 0;
	{
		CSingleLock lock(&m_fileOpProgressLock, TRUE);
		m_pendingFileOpProgress.clear();
		m_pendingPartFileOpProgress.clear();
	}
	InterlockedExchange(&m_lFileOpProgressPendingMessage, 0);
	HideStartupLoadingDialog(false);
	HideShutdownProgressDialog();
	StopTitleVersionAnimation();
	CPPgSecurity::CancelIPFilterDownload();
	CIPGeolocation::CancelIPGeolocationDownload();
	DestroyTitleVersionOverlayWindow();
	m_rcTitleVersionLink.SetRectEmpty();
	m_fontTitleVersionLink.DeleteObject();

	// If eMule was started with "RUNAS":
	// When user is logging of (or reboots or shutdown system), Windows may or may not send
	// a WM_DESTROY (depends on how long the application needed to process the
	// CTRL_LOGOFF_EVENT). But, regardless of what happened and regardless of how long any
	// application specific shutdown took, Windows fill forcefully terminate the process
	// after 1-2 seconds after WM_DESTROY! So, we can not use WM_DESTROY for any lengthy
	// shutdown actions in that case.
	CTrayDialog::OnDestroy();
}

class CBulkOperationExitOverlayWnd : public CWnd
{
public:
	CBulkOperationExitOverlayWnd(CemuleDlg* pOwner, const CString& strTitle, const CString& strBody, const CString& strCancelAndExit, const CString& strWaitAndExit, UINT uDone, UINT uTotal, bool bCanCancel)
		: m_pOwner(pOwner)
		, m_strTitle(strTitle)
		, m_strBody(strBody)
		, m_strCancelAndExit(strCancelAndExit)
		, m_strWaitAndExit(strWaitAndExit)
		, m_strReturn(GetResString(_T("BULKOP_EXIT_RETURN_TO_APP")))
		, m_uDone(uDone)
		, m_uTotal(uTotal)
		, m_bCanCancel(bCanCancel)
		, m_iResult(0)
		, m_uHoverButton(0)
		, m_uAnimationPhase(0)
		, m_bTrackingMouse(false)
		, m_sizeBackground(0, 0)
	{
	}

	int DoModalOverlay()
	{
		if (m_pOwner == NULL || !::IsWindow(m_pOwner->GetSafeHwnd()))
			return m_bCanCancel ? IDC_BULKOP_EXIT_CANCEL_EXIT : IDC_BULKOP_EXIT_RETURN;

		CaptureBackground();

		CString strClass(AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL));
		CRect rcOwner;
		m_pOwner->GetWindowRect(rcOwner);
		if (!CreateEx(WS_EX_TOOLWINDOW, strClass, NULL, WS_POPUP, rcOwner, m_pOwner, 0))
			return m_bCanCancel ? IDC_BULKOP_EXIT_CANCEL_EXIT : IDC_BULKOP_EXIT_RETURN;

		ShowWindow(SW_SHOW);
		SetWindowPos(&wndTop, rcOwner.left, rcOwner.top, rcOwner.Width(), rcOwner.Height(), SWP_SHOWWINDOW);
		SetForegroundWindow();
		SetFocus();
		SetTimer(TimerRefresh, 150, NULL);
		SetTimer(TimerAnimation, 80, NULL);

		MSG msg;
		while (m_iResult == 0 && ::IsWindow(m_hWnd)) {
			const BOOL bMessage = ::GetMessage(&msg, NULL, 0, 0);
			if (bMessage == -1)
				break;
			if (bMessage == 0) {
				::PostQuitMessage(static_cast<int>(msg.wParam));
				break;
			}
			if (!PreTranslateMessage(&msg)) {
				::TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}
		}

		const int iResult = m_iResult != 0 ? m_iResult : IDC_BULKOP_EXIT_RETURN;
		if (::IsWindow(m_hWnd))
			DestroyWindow();
		return iResult;
	}

protected:
	enum { TimerRefresh = 1, TimerAnimation = 2 };
	virtual BOOL PreTranslateMessage(MSG* pMsg)
	{
		if (pMsg != NULL && pMsg->message == WM_KEYDOWN) {
			if (pMsg->wParam == VK_ESCAPE) {
				Finish(IDC_BULKOP_EXIT_RETURN);
				return TRUE;
			}
			if (pMsg->wParam == VK_RETURN) {
				Finish(IDC_BULKOP_EXIT_RETURN);
				return TRUE;
			}
		}
		return CWnd::PreTranslateMessage(pMsg);
	}

	afx_msg void OnPaint()
	{
		CPaintDC dcPaint(this);
		CRect rcClient;
		GetClientRect(rcClient);
		CDC dcMem;
		dcMem.CreateCompatibleDC(&dcPaint);
		CBitmap bmpMem;
		bmpMem.CreateCompatibleBitmap(&dcPaint, max(1, rcClient.Width()), max(1, rcClient.Height()));
		CBitmap* pOldBitmap = dcMem.SelectObject(&bmpMem);
		DrawOverlay(&dcMem, rcClient);
		dcPaint.BitBlt(0, 0, rcClient.Width(), rcClient.Height(), &dcMem, 0, 0, SRCCOPY);
		dcMem.SelectObject(pOldBitmap);
	}

	afx_msg BOOL OnEraseBkgnd(CDC*)
	{
		return TRUE;
	}

	afx_msg void OnTimer(UINT_PTR nIDEvent)
	{
		if (nIDEvent == TimerRefresh) {
			SyncToOwnerRect();
			RefreshProgress();
			return;
		}
		if (nIDEvent == TimerAnimation) {
			m_uAnimationPhase = (m_uAnimationPhase + 24) % 1536;
			Invalidate(FALSE);
			return;
		}
		CWnd::OnTimer(nIDEvent);
	}

	afx_msg void OnLButtonUp(UINT, CPoint point)
	{
		const UINT uButton = HitTestButton(point);
		if (uButton != 0)
			Finish(static_cast<int>(uButton));
	}

	afx_msg void OnMouseMove(UINT nFlags, CPoint point)
	{
		const UINT uButton = HitTestButton(point);
		if (m_uHoverButton != uButton) {
			m_uHoverButton = uButton;
			Invalidate(FALSE);
		}

		if (!m_bTrackingMouse) {
			TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hWnd, 0 };
			m_bTrackingMouse = ::TrackMouseEvent(&tme) != FALSE;
		}
		CWnd::OnMouseMove(nFlags, point);
	}

	afx_msg void OnMouseLeave()
	{
		m_bTrackingMouse = false;
		if (m_uHoverButton != 0) {
			m_uHoverButton = 0;
			Invalidate(FALSE);
		}
	}

	afx_msg BOOL OnSetCursor(CWnd*, UINT, UINT)
	{
		CPoint point;
		::GetCursorPos(&point);
		ScreenToClient(&point);
		if (HitTestButton(point) != 0) {
			::SetCursor(::LoadCursor(NULL, IDC_HAND));
			return TRUE;
		}
		::SetCursor(::LoadCursor(NULL, IDC_ARROW));
		return TRUE;
	}

	DECLARE_MESSAGE_MAP()

private:
	static DWORD MakeDibColor(int iRed, int iGreen, int iBlue)
	{
		return (static_cast<DWORD>(iRed) << 16) | (static_cast<DWORD>(iGreen) << 8) | static_cast<DWORD>(iBlue);
	}

	static int GetDibRed(DWORD dwColor)
	{
		return static_cast<int>((dwColor >> 16) & 0xFF);
	}

	static int GetDibGreen(DWORD dwColor)
	{
		return static_cast<int>((dwColor >> 8) & 0xFF);
	}

	static int GetDibBlue(DWORD dwColor)
	{
		return static_cast<int>(dwColor & 0xFF);
	}

	static DWORD BlendDibColor(DWORD dwFrom, COLORREF crTo, int iToPercent)
	{
		const int iFromPercent = 100 - iToPercent;
		return MakeDibColor((GetDibRed(dwFrom) * iFromPercent + GetRValue(crTo) * iToPercent) / 100,
			(GetDibGreen(dwFrom) * iFromPercent + GetGValue(crTo) * iToPercent) / 100,
			(GetDibBlue(dwFrom) * iFromPercent + GetBValue(crTo) * iToPercent) / 100);
	}

	static void BlurPixels(DWORD* pPixels, int iWidth, int iHeight)
	{
		if (pPixels == NULL || iWidth <= 2 || iHeight <= 2)
			return;

		const int iRadius = 5;
		const size_t uPixelCount = static_cast<size_t>(iWidth) * static_cast<size_t>(iHeight);
		std::vector<DWORD> temp;
		try {
			temp.resize(uPixelCount);
		} catch (...) {
			return;
		}

		for (int y = 0; y < iHeight; ++y) {
			for (int x = 0; x < iWidth; ++x) {
				int iRed = 0;
				int iGreen = 0;
				int iBlue = 0;
				int iCount = 0;
				for (int dx = -iRadius; dx <= iRadius; ++dx) {
					const int xx = x + dx;
					if (xx < 0 || xx >= iWidth)
						continue;
					const DWORD dwColor = pPixels[y * iWidth + xx];
					iRed += GetDibRed(dwColor);
					iGreen += GetDibGreen(dwColor);
					iBlue += GetDibBlue(dwColor);
					++iCount;
				}
				temp[y * iWidth + x] = MakeDibColor(iRed / iCount, iGreen / iCount, iBlue / iCount);
			}
		}

		for (int y = 0; y < iHeight; ++y) {
			for (int x = 0; x < iWidth; ++x) {
				int iRed = 0;
				int iGreen = 0;
				int iBlue = 0;
				int iCount = 0;
				for (int dy = -iRadius; dy <= iRadius; ++dy) {
					const int yy = y + dy;
					if (yy < 0 || yy >= iHeight)
						continue;
					const DWORD dwColor = temp[yy * iWidth + x];
					iRed += GetDibRed(dwColor);
					iGreen += GetDibGreen(dwColor);
					iBlue += GetDibBlue(dwColor);
					++iCount;
				}
				pPixels[y * iWidth + x] = MakeDibColor(iRed / iCount, iGreen / iCount, iBlue / iCount);
			}
		}
	}

	void CaptureBackground()
	{
		if (m_pOwner == NULL || !::IsWindow(m_pOwner->GetSafeHwnd()))
			return;

		CRect rcOwner;
		m_pOwner->GetWindowRect(rcOwner);
		const int iWidth = rcOwner.Width();
		const int iHeight = rcOwner.Height();
		if (iWidth <= 0 || iHeight <= 0)
			return;

		HDC hdcScreen = ::GetDC(NULL);
		if (hdcScreen == NULL)
			return;

		BITMAPINFO bi;
		ZeroMemory(&bi, sizeof(bi));
		bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bi.bmiHeader.biWidth = iWidth;
		bi.bmiHeader.biHeight = -iHeight;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;

		void* pBits = NULL;
		HBITMAP hBitmap = ::CreateDIBSection(hdcScreen, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
		if (hBitmap != NULL && pBits != NULL) {
			CDC dcMem;
			dcMem.CreateCompatibleDC(CDC::FromHandle(hdcScreen));
			HGDIOBJ hOldBitmap = ::SelectObject(dcMem.GetSafeHdc(), hBitmap);
			BOOL bCaptured = ::PrintWindow(m_pOwner->GetSafeHwnd(), dcMem.GetSafeHdc(), 0);
			if (!bCaptured)
				bCaptured = ::BitBlt(dcMem.GetSafeHdc(), 0, 0, iWidth, iHeight, hdcScreen, rcOwner.left, rcOwner.top, SRCCOPY);
			::SelectObject(dcMem.GetSafeHdc(), hOldBitmap);

			if (bCaptured) {
				DWORD* pPixels = static_cast<DWORD*>(pBits);
				BlurPixels(pPixels, iWidth, iHeight);
				const bool bDark = IsDarkModeEnabled();
				const COLORREF crTint = bDark ? RGB(0, 0, 0) : RGB(246, 248, 252);
				const int iTintPercent = bDark ? 44 : 32;
				const size_t uPixelCount = static_cast<size_t>(iWidth) * static_cast<size_t>(iHeight);
				for (size_t i = 0; i < uPixelCount; ++i)
					pPixels[i] = BlendDibColor(pPixels[i], crTint, iTintPercent);

				m_bmpBackground.DeleteObject();
				m_bmpBackground.Attach(hBitmap);
				m_sizeBackground = CSize(iWidth, iHeight);
				hBitmap = NULL;
			}
		}

		if (hBitmap != NULL)
			::DeleteObject(hBitmap);
		::ReleaseDC(NULL, hdcScreen);
	}

	void DrawOverlay(CDC* pDC, const CRect& rcClient)
	{
		const bool bDark = IsDarkModeEnabled();
		const COLORREF crFallback = bDark ? RGB(22, 24, 30) : RGB(235, 240, 248);
		pDC->FillSolidRect(rcClient, crFallback);
		if (m_bmpBackground.GetSafeHandle() != NULL && m_sizeBackground.cx > 0 && m_sizeBackground.cy > 0) {
			CDC dcBitmap;
			dcBitmap.CreateCompatibleDC(pDC);
			CBitmap* pOldBitmap = dcBitmap.SelectObject(&m_bmpBackground);
			pDC->StretchBlt(0, 0, rcClient.Width(), rcClient.Height(), &dcBitmap, 0, 0, m_sizeBackground.cx, m_sizeBackground.cy, SRCCOPY);
			dcBitmap.SelectObject(pOldBitmap);
		}

		const int iPanelWidth = min(680, max(520, rcClient.Width() - 96));
		const int iHorizontalPadding = 28;
		CFont* pOldFont = pDC->SelectObject(GetFont());
		TEXTMETRIC tm;
		ZeroMemory(&tm, sizeof(tm));
		pDC->GetTextMetrics(&tm);
		const int iLineHeight = max(1, tm.tmHeight + tm.tmExternalLeading);
		CRect rcBodyMeasure(0, 0, max(120, iPanelWidth - iHorizontalPadding * 2), 0);
		pDC->DrawText(m_strBody, rcBodyMeasure, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL | DT_CALCRECT);
		const int iMeasuredBodyHeight = max(iLineHeight * 6, rcBodyMeasure.Height() + 2);
		const int iPanelHeightWanted = 28 + 26 + 12 + iMeasuredBodyHeight + 8 + 18 + 8 + 12 + 28 + 32 + 18;
		const int iPanelHeight = min(max(278, iPanelHeightWanted), max(160, rcClient.Height() - 32));
		CRect rcPanel(rcClient.left + (rcClient.Width() - iPanelWidth) / 2, rcClient.top + (rcClient.Height() - iPanelHeight) / 2,
			rcClient.left + (rcClient.Width() + iPanelWidth) / 2, rcClient.top + (rcClient.Height() + iPanelHeight) / 2);
		if (rcPanel.left < rcClient.left + 16)
			rcPanel.OffsetRect(rcClient.left + 16 - rcPanel.left, 0);
		if (rcPanel.top < rcClient.top + 16)
			rcPanel.OffsetRect(0, rcClient.top + 16 - rcPanel.top);

		const COLORREF crPanel = bDark ? RGB(30, 33, 42) : RGB(250, 252, 255);
		const COLORREF crPanelEdge = bDark ? RGB(75, 82, 102) : RGB(196, 207, 224);
		const COLORREF crTitle = bDark ? RGB(242, 245, 255) : RGB(24, 35, 56);
		const COLORREF crText = bDark ? RGB(218, 224, 240) : RGB(42, 52, 72);
		const COLORREF crSoft = bDark ? RGB(167, 180, 210) : RGB(88, 101, 124);
		const COLORREF crProgressFill = RGB(76, 132, 232);
		const COLORREF crProgressBack = bDark ? RGB(54, 60, 74) : RGB(224, 231, 242);

		CRgn rgnPanel;
		rgnPanel.CreateRoundRectRgn(rcPanel.left, rcPanel.top, rcPanel.right + 1, rcPanel.bottom + 1, 18, 18);
		CBrush brPanel(crPanel);
		pDC->FillRgn(&rgnPanel, &brPanel);
		CPen penEdge(PS_SOLID, 1, crPanelEdge);
		CPen* pOldPen = pDC->SelectObject(&penEdge);
		CGdiObject* pOldBrush = pDC->SelectStockObject(NULL_BRUSH);
		pDC->RoundRect(rcPanel, CPoint(18, 18));

		DrawAnimatedRainbowBorder(pDC, rcPanel, m_uAnimationPhase, 2, 18);

		pDC->SetBkMode(TRANSPARENT);

		CRect rcTitle(rcPanel.left + iHorizontalPadding, rcPanel.top + 28, rcPanel.right - iHorizontalPadding, rcPanel.top + 54);
		pDC->SetTextColor(crTitle);
		pDC->DrawText(m_strTitle, rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

		const int iBodyTop = rcTitle.bottom + 12;
		const int iBodyMaxBottom = max(iBodyTop + iLineHeight, rcPanel.bottom - 108);
		int iBodyHeight = min(iMeasuredBodyHeight, iBodyMaxBottom - iBodyTop);
		if (iBodyHeight < iMeasuredBodyHeight)
			iBodyHeight = max(iLineHeight, (iBodyHeight / iLineHeight) * iLineHeight);
		CRect rcBody(rcPanel.left + iHorizontalPadding, iBodyTop, rcPanel.right - iHorizontalPadding, iBodyTop + iBodyHeight);
		pDC->SetTextColor(crText);
		pDC->DrawText(m_strBody, rcBody, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);

		CString strProgress;
		strProgress.Format(GetResString(_T("BULKOP_PROGRESS_DETAIL")), m_uDone, m_uTotal);
		CRect rcProgressText(rcPanel.left + iHorizontalPadding, rcBody.bottom + 8, rcPanel.right - iHorizontalPadding, rcBody.bottom + 26);
		pDC->SetTextColor(crSoft);
		pDC->DrawText(strProgress, rcProgressText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

		CRect rcProgress(rcPanel.left + iHorizontalPadding, rcProgressText.bottom + 8, rcPanel.right - iHorizontalPadding, rcProgressText.bottom + 20);
		pDC->FillSolidRect(rcProgress, crProgressBack);
		if (m_uTotal > 0) {
			const int iFillWidth = min(rcProgress.Width(), max(0, MulDiv(rcProgress.Width(), min(m_uDone, m_uTotal), m_uTotal)));
			if (iFillWidth > 0) {
				CRect rcFill(rcProgress);
				rcFill.right = rcFill.left + iFillWidth;
				pDC->FillSolidRect(rcFill, crProgressFill);
			}
		}
		CPen penProgress(PS_SOLID, 1, crPanelEdge);
		pDC->SelectObject(&penProgress);
		pDC->MoveTo(rcProgress.left, rcProgress.top);
		pDC->LineTo(rcProgress.right, rcProgress.top);
		pDC->LineTo(rcProgress.right, rcProgress.bottom);
		pDC->LineTo(rcProgress.left, rcProgress.bottom);
		pDC->LineTo(rcProgress.left, rcProgress.top);

		LayoutButtons(rcPanel);
		DrawButton(pDC, m_rcReturn, m_strReturn, IDC_BULKOP_EXIT_RETURN, true, bDark);
		DrawButton(pDC, m_rcWait, m_strWaitAndExit, IDC_BULKOP_EXIT_WAIT_EXIT, false, bDark);
		if (m_bCanCancel)
			DrawButton(pDC, m_rcCancel, m_strCancelAndExit, IDC_BULKOP_EXIT_CANCEL_EXIT, false, bDark);

		pDC->SelectObject(pOldFont);
		pDC->SelectObject(pOldPen);
		pDC->SelectObject(pOldBrush);
	}

	void DrawButton(CDC* pDC, const CRect& rcButton, const CString& strText, UINT uButton, bool bDefault, bool bDark)
	{
		const bool bHover = (m_uHoverButton == uButton);
		const COLORREF crButton = bDefault ? (bHover ? RGB(63, 132, 245) : RGB(76, 132, 232)) : (bDark ? (bHover ? RGB(61, 67, 84) : RGB(47, 52, 66)) : (bHover ? RGB(235, 241, 252) : RGB(246, 248, 252)));
		const COLORREF crEdge = bDefault ? RGB(76, 132, 232) : (bDark ? RGB(78, 86, 106) : RGB(195, 205, 222));
		const COLORREF crText = bDefault ? RGB(255, 255, 255) : (bDark ? RGB(225, 231, 245) : RGB(30, 42, 66));
		CBrush brButton(crButton);
		CPen penButton(PS_SOLID, 1, crEdge);
		CPen* pOldPen = pDC->SelectObject(&penButton);
		CBrush* pOldBrush = pDC->SelectObject(&brButton);
		pDC->RoundRect(rcButton, CPoint(8, 8));
		pDC->SetTextColor(crText);
		CRect rcText(rcButton);
		pDC->DrawText(strText, rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		pDC->SelectObject(pOldPen);
		pDC->SelectObject(pOldBrush);
	}

	void LayoutButtons(const CRect& rcPanel)
	{
		const int iButtonHeight = 32;
		const int iGap = 14;
		const int iButtonCount = m_bCanCancel ? 3 : 2;
		const int iButtonWidth = min(178, max(136, (rcPanel.Width() - 56 - (iButtonCount - 1) * iGap) / iButtonCount));
		const int iTotalWidth = iButtonWidth * iButtonCount + iGap * (iButtonCount - 1);
		const int iLeft = rcPanel.left + (rcPanel.Width() - iTotalWidth) / 2;
		const int iTop = rcPanel.bottom - 50;
		m_rcReturn.SetRect(iLeft, iTop, iLeft + iButtonWidth, iTop + iButtonHeight);
		m_rcWait.SetRect(m_rcReturn.right + iGap, iTop, m_rcReturn.right + iGap + iButtonWidth, iTop + iButtonHeight);
		if (m_bCanCancel)
			m_rcCancel.SetRect(m_rcWait.right + iGap, iTop, m_rcWait.right + iGap + iButtonWidth, iTop + iButtonHeight);
		else
			m_rcCancel.SetRectEmpty();
	}

	UINT HitTestButton(CPoint point) const
	{
		if (m_rcReturn.PtInRect(point))
			return IDC_BULKOP_EXIT_RETURN;
		if (m_rcWait.PtInRect(point))
			return IDC_BULKOP_EXIT_WAIT_EXIT;
		if (m_bCanCancel && m_rcCancel.PtInRect(point))
			return IDC_BULKOP_EXIT_CANCEL_EXIT;
		return 0;
	}

	void Finish(int iResult)
	{
		m_iResult = iResult;
		if (::IsWindow(m_hWnd))
			DestroyWindow();
	}

	void RefreshProgress()
	{
		if (m_pOwner == NULL || !::IsWindow(m_pOwner->GetSafeHwnd()))
			return;

		CString strTitle;
		CString strBody;
		CString strCancelAndExit;
		CString strWaitAndExit;
		UINT uDone = 0;
		UINT uTotal = 0;
		bool bCanCancel = false;
		if (!m_pOwner->GetActiveBulkOperationCloseInfo(strTitle, strBody, strCancelAndExit, strWaitAndExit, uDone, uTotal, &bCanCancel)) {
			Finish(IDC_BULKOP_EXIT_WAIT_EXIT);
			return;
		}

		if (m_strTitle == strTitle && m_strBody == strBody && m_strCancelAndExit == strCancelAndExit && m_strWaitAndExit == strWaitAndExit && m_uDone == uDone && m_uTotal == uTotal && m_bCanCancel == bCanCancel)
			return;

		m_strTitle = strTitle;
		m_strBody = strBody;
		m_strCancelAndExit = strCancelAndExit;
		m_strWaitAndExit = strWaitAndExit;
		m_uDone = uDone;
		m_uTotal = uTotal;
		m_bCanCancel = bCanCancel;
		Invalidate(FALSE);
	}

	void SyncToOwnerRect()
	{
		if (m_pOwner == NULL || !::IsWindow(m_pOwner->GetSafeHwnd()) || !::IsWindow(m_hWnd))
			return;
		CRect rcOwner;
		m_pOwner->GetWindowRect(rcOwner);
		CRect rcWindow;
		GetWindowRect(rcWindow);
		if (rcOwner != rcWindow)
			SetWindowPos(&wndTop, rcOwner.left, rcOwner.top, rcOwner.Width(), rcOwner.Height(), SWP_NOACTIVATE | SWP_SHOWWINDOW);
	}

	CemuleDlg* m_pOwner;
	CString m_strTitle;
	CString m_strBody;
	CString m_strCancelAndExit;
	CString m_strWaitAndExit;
	CString m_strReturn;
	UINT m_uDone;
	UINT m_uTotal;
	bool m_bCanCancel;
	int m_iResult;
	UINT m_uHoverButton;
	UINT m_uAnimationPhase;
	bool m_bTrackingMouse;
	CRect m_rcReturn;
	CRect m_rcWait;
	CRect m_rcCancel;
	CBitmap m_bmpBackground;
	CSize m_sizeBackground;
};

BEGIN_MESSAGE_MAP(CBulkOperationExitOverlayWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_TIMER()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_SETCURSOR()
END_MESSAGE_MAP()


namespace
{
	struct SActiveBulkOperationSummary
	{
		SActiveBulkOperationSummary()
			: uCount(0)
			, uDone(0)
			, uTotal(0)
			, uStartupLoaded(0)
			, uStartupTempDirIndex(0)
			, uStartupTempDirCount(0)
			, uSearchStartupLoadedSearches(0)
			, uSearchStartupTotalSearches(0)
			, uSearchStartupLoadedFiles(0)
			, uIPFilterDone(0)
			, uIPFilterTotal(0)
			, bHasIPFilterDownload(false)
			, uIPGeolocationDone(0)
			, uIPGeolocationTotal(0)
			, bHasIPGeolocationDownload(false)
			, bHasSharedFilesLoad(false)
			, bHasKnownFilesLoad(false)
			, bHasClientHistoryLoad(false)
			, bCanCancel(false)
			, bHasStartupLoad(false)
			, bHasSearchStartupLoad(false)
			, bHasAdd(false)
			, bHasDelete(false)
			, bHasUpdate(false)
			, bListUpdateAfterCompletion(false)
			, bSavingDownloadsToDisk(false)
			, bHashing(false)
			, bMetadata(false)
		{
		}

		UINT uCount;
		UINT uDone;
		UINT uTotal;
		UINT uStartupLoaded;
		UINT uStartupTempDirIndex;
		UINT uStartupTempDirCount;
		UINT uSearchStartupLoadedSearches;
		UINT uSearchStartupTotalSearches;
		UINT uSearchStartupLoadedFiles;
		CString strIPFilterTitle;
		CString strIPFilterDetail;
		UINT uIPFilterDone;
		UINT uIPFilterTotal;
		bool bHasIPFilterDownload;
		CString strIPGeolocationTitle;
		CString strIPGeolocationDetail;
		UINT uIPGeolocationDone;
		UINT uIPGeolocationTotal;
		bool bHasIPGeolocationDownload;
		bool bHasSharedFilesLoad;
		bool bHasKnownFilesLoad;
		bool bHasClientHistoryLoad;
		bool bCanCancel;
		bool bHasStartupLoad;
		bool bHasSearchStartupLoad;
		bool bHasAdd;
		bool bHasDelete;
		bool bHasUpdate;
		bool bListUpdateAfterCompletion;
		bool bSavingDownloadsToDisk;
		bool bHashing;
		bool bMetadata;
	};

	static CString GetResStringOrDefault(LPCTSTR pszKey, LPCTSTR pszDefault)
	{
		CString strText(GetResString(pszKey));
		return strText == pszKey ? CString(pszDefault) : strText;
	}

	static void AddActiveBulkOperation(SActiveBulkOperationSummary& summary, UINT uDone, UINT uTotal, bool bCanCancel, bool bAdd, bool bDelete, bool bUpdate, bool bListUpdateAfterCompletion = false, bool bHashing = false, bool bSavingDownloadsToDisk = false)
	{
		if (uTotal == 0 || (!bHashing && uTotal < BULK_OPERATION_MIN_ITEMS))
			return;

		++summary.uCount;
		summary.uDone += min(uDone, uTotal);
		summary.uTotal += uTotal;
		summary.bCanCancel = summary.bCanCancel || bCanCancel;
		summary.bHasAdd = summary.bHasAdd || bAdd;
		summary.bHasDelete = summary.bHasDelete || bDelete;
		summary.bHasUpdate = summary.bHasUpdate || bUpdate;
		summary.bListUpdateAfterCompletion = summary.bListUpdateAfterCompletion || bListUpdateAfterCompletion;
		summary.bSavingDownloadsToDisk = summary.bSavingDownloadsToDisk || bSavingDownloadsToDisk;
		summary.bHashing = summary.bHashing || bHashing;
	}

	static void FormatBulkOperationProgressDetail(CString& strDetail, UINT uDone, UINT uTotal, bool bListUpdateAfterCompletion, bool bSavingDownloadsToDisk = false)
	{
		LPCTSTR pszKey = bSavingDownloadsToDisk ? _T("BULKOP_PROGRESS_SAVE_DOWNLOADS_DETAIL") : (bListUpdateAfterCompletion ? _T("BULKOP_PROGRESS_FINAL_RELOAD_DETAIL") : _T("BULKOP_PROGRESS_DETAIL"));
		strDetail.Format(GetResString(pszKey), uDone, uTotal);
	}

	static void AddIPFilterDownloadOperation(SActiveBulkOperationSummary& summary)
	{
		CString strTitle;
		CString strDetail;
		UINT uDone = 0;
		UINT uTotal = 0;
		if (!CPPgSecurity::GetIPFilterDownloadOverlayInfo(strTitle, strDetail, uDone, uTotal))
			return;

		++summary.uCount;
		summary.uDone += min(uDone, uTotal);
		summary.uTotal += uTotal;
		summary.bHasIPFilterDownload = true;
		summary.strIPFilterTitle = strTitle;
		summary.strIPFilterDetail = strDetail;
		summary.uIPFilterDone = uDone;
		summary.uIPFilterTotal = uTotal;
	}

	static void AddIPGeolocationDownloadOperation(SActiveBulkOperationSummary& summary)
	{
		CString strTitle;
		CString strDetail;
		UINT uDone = 0;
		UINT uTotal = 0;
		if (!CIPGeolocation::GetIPGeolocationDownloadOverlayInfo(strTitle, strDetail, uDone, uTotal))
			return;

		++summary.uCount;
		summary.uDone += min(uDone, uTotal);
		summary.uTotal += uTotal;
		summary.bHasIPGeolocationDownload = true;
		summary.strIPGeolocationTitle = strTitle;
		summary.strIPGeolocationDetail = strDetail;
		summary.uIPGeolocationDone = uDone;
		summary.uIPGeolocationTotal = uTotal;
	}

	static void AddStartupLoadOperation(SActiveBulkOperationSummary& summary)
	{
		if (theApp.downloadqueue == NULL || !theApp.downloadqueue->IsStartupLoadActive())
			return;

		++summary.uCount;
		summary.bHasStartupLoad = true;
		theApp.downloadqueue->GetStartupLoadProgress(summary.uStartupLoaded, summary.uStartupTempDirIndex, summary.uStartupTempDirCount);
	}

	static void AddSearchStartupLoadOperation(SActiveBulkOperationSummary& summary)
	{
		if (theApp.searchlist == NULL || !theApp.searchlist->IsStartupLoadActive())
			return;

		++summary.uCount;
		summary.bHasSearchStartupLoad = true;
		theApp.searchlist->GetStartupLoadProgress(summary.uSearchStartupLoadedSearches, summary.uSearchStartupTotalSearches, summary.uSearchStartupLoadedFiles);
	}

	static void AddSharedFilesLoadOperation(SActiveBulkOperationSummary& summary, const CemuleDlg* pDlg)
	{
		if (pDlg == NULL || pDlg->sharedfileswnd == NULL)
			return;
		const bool bSharedWork = theApp.sharedfiles != NULL && theApp.sharedfiles->HasActiveSharedFilesWork();
		const bool bFileSystemReload = pDlg->sharedfileswnd->sharedfilesctrl.IsFileSystemReloadActive();
		if (!bSharedWork && !bFileSystemReload)
			return;
		++summary.uCount;
		summary.bHasSharedFilesLoad = true;
	}


	static void AddKnownFilesLoadOperation(SActiveBulkOperationSummary& summary)
	{
		const CemuleApp::SStartupMetadataLoadState state = theApp.GetStartupMetadataLoadState(CemuleApp::StartupMetadataKnownFiles);
		if (state.m_eState != CemuleApp::StartupMetadataStateLoading && state.m_eState != CemuleApp::StartupMetadataStateApplying)
			return;
		++summary.uCount;
		summary.bHasKnownFilesLoad = true;
	}

	static void AddClientHistoryLoadOperation(SActiveBulkOperationSummary& summary, const CemuleDlg* pDlg)
	{
		if (pDlg == NULL || pDlg->transferwnd == NULL || pDlg->transferwnd->GetClientList() == NULL || !::IsWindow(pDlg->transferwnd->GetClientList()->GetSafeHwnd()) || !pDlg->transferwnd->GetClientList()->IsWindowVisible())
			return;
		const CemuleApp::SStartupMetadataLoadState state = theApp.GetStartupMetadataLoadState(CemuleApp::StartupMetadataClientHistory);
		if (state.m_eState != CemuleApp::StartupMetadataStateLoading && state.m_eState != CemuleApp::StartupMetadataStateApplying)
			return;
		++summary.uCount;
		summary.bHasClientHistoryLoad = true;
	}

	static bool HasTransferDownloadOverlayOperation(const CemuleDlg* pDlg)
	{
		if (theApp.downloadqueue != NULL && theApp.downloadqueue->IsStartupLoadActive())
			return true;

		CString strTitle;
		CString strBody;
		CString strCancelAndExit;
		CString strWaitAndExit;
		UINT uDone = 0;
		UINT uTotal = 0;

		if (theApp.GetActiveDownloadAddOperationProgress(uDone, uTotal))
			return true;

		bool bBackendRemove = true;
		if (theApp.GetActiveBackendDownloadListOperationProgress(bBackendRemove, uDone, uTotal))
			return true;

		return pDlg != NULL && pDlg->transferwnd != NULL && pDlg->transferwnd->GetDownloadList() != NULL && pDlg->transferwnd->GetDownloadList()->GetActiveChunkedDownloadOperationProgress(strTitle, strBody, strCancelAndExit, strWaitAndExit, uDone, uTotal);
	}

	static bool HasClientHistoryLoadOperation(const CemuleDlg* pDlg)
	{
		if (pDlg == NULL || pDlg->transferwnd == NULL || pDlg->transferwnd->GetClientList() == NULL || !::IsWindow(pDlg->transferwnd->GetClientList()->GetSafeHwnd()) || !pDlg->transferwnd->GetClientList()->IsWindowVisible())
			return false;
		const CemuleApp::SStartupMetadataLoadState state = theApp.GetStartupMetadataLoadState(CemuleApp::StartupMetadataClientHistory);
		return state.m_eState == CemuleApp::StartupMetadataStateLoading || state.m_eState == CemuleApp::StartupMetadataStateApplying;
	}

	static bool CollectActiveBulkOperationSummary(const CemuleDlg* pDlg, bool bIncludeStartupLoad, bool bIncludeSearchStartupLoad, bool bIncludeSharedFilesLoad, bool bIncludeKnownFilesLoad, bool bIncludeClientHistoryLoad, bool bIncludeIPFilterDownload, bool bIncludeDownloadAddOperation, bool bIncludeSharedFilesBulkOperation, bool bIncludeSharedFilesHashing, SActiveBulkOperationSummary& summary)
	{
		if (pDlg == NULL)
			return false;

		CString strTitle;
		CString strBody;
		CString strCancelAndExit;
		CString strWaitAndExit;
		UINT uDone = 0;
		UINT uTotal = 0;
		bool bSavingDownloadsToDisk = false;

		if (pDlg->searchwnd != NULL && pDlg->searchwnd->m_pwndResults != NULL && pDlg->searchwnd->m_pwndResults->GetActiveChunkedSearchDownloadProgress(strTitle, strBody, strCancelAndExit, strWaitAndExit, uDone, uTotal))
			AddActiveBulkOperation(summary, uDone, uTotal, true, true, false, false, true);

		if (bIncludeDownloadAddOperation) {
			if (theApp.GetActiveDownloadAddOperationProgress(uDone, uTotal, &bSavingDownloadsToDisk))
				AddActiveBulkOperation(summary, uDone, uTotal, !bSavingDownloadsToDisk, true, false, false, true, false, bSavingDownloadsToDisk);
			else {
				bool bBackendRemove = true;
				if (theApp.GetActiveBackendDownloadListOperationProgress(bBackendRemove, uDone, uTotal))
					AddActiveBulkOperation(summary, uDone, uTotal, true, !bBackendRemove, bBackendRemove, false);
			}

			if (pDlg->transferwnd != NULL && pDlg->transferwnd->GetDownloadList() != NULL && pDlg->transferwnd->GetDownloadList()->GetActiveChunkedDownloadOperationProgress(strTitle, strBody, strCancelAndExit, strWaitAndExit, uDone, uTotal)) {
				const bool bDelete = strCancelAndExit == GetResString(_T("BULKOP_EXIT_CANCEL_DELETE_AND_EXIT"));
				AddActiveBulkOperation(summary, uDone, uTotal, true, false, bDelete, !bDelete);
			}
		}

		bool bSharedDeleteLike = false;
		if (bIncludeSharedFilesBulkOperation && pDlg->sharedfileswnd != NULL && pDlg->sharedfileswnd->sharedfilesctrl.GetActiveSharedFilesBulkOperationProgress(bSharedDeleteLike, uDone, uTotal))
			AddActiveBulkOperation(summary, uDone, uTotal, true, false, bSharedDeleteLike, !bSharedDeleteLike);

		if (bIncludeSharedFilesHashing && pDlg->sharedfileswnd != NULL && pDlg->sharedfileswnd->sharedfilesctrl.GetActiveSharedFilesHashingProgress(uDone, uTotal))
			AddActiveBulkOperation(summary, uDone, uTotal, false, false, false, false, true, true);

		if (bIncludeSharedFilesHashing && pDlg->sharedfileswnd != NULL && pDlg->sharedfileswnd->sharedfilesctrl.GetActiveSharedFilesMetadataProgress(uDone, uTotal)) {
			AddActiveBulkOperation(summary, uDone, uTotal, false, false, false, true);
			summary.bMetadata = true;
		}

		if (bIncludeStartupLoad)
			AddStartupLoadOperation(summary);
		if (bIncludeSearchStartupLoad)
			AddSearchStartupLoadOperation(summary);
		if (bIncludeSharedFilesLoad)
			AddSharedFilesLoadOperation(summary, pDlg);
		if (bIncludeKnownFilesLoad)
			AddKnownFilesLoadOperation(summary);
		if (bIncludeClientHistoryLoad)
			AddClientHistoryLoadOperation(summary, pDlg);
		if (bIncludeIPFilterDownload) {
			AddIPFilterDownloadOperation(summary);
			AddIPGeolocationDownloadOperation(summary);
		}

		return summary.uCount != 0;
	}

	static void BuildActiveBulkOperationOverlayText(const SActiveBulkOperationSummary& summary, CString& strTitle, CString& strDetail, UINT& uDone, UINT& uTotal, bool& bCanCancel)
	{
		uDone = summary.uDone;
		uTotal = summary.uTotal;
		bCanCancel = summary.bCanCancel;

		if ((summary.bHasIPFilterDownload || summary.bHasIPGeolocationDownload) && summary.uCount > 1) {
			strTitle = GetResString(_T("BULKOP_MULTI_OPERATIONS_TITLE"));
			strDetail.Format(GetResString(_T("BULKOP_MULTI_OPERATIONS_DETAIL")), summary.uCount, summary.uDone, summary.uTotal);
			return;
		}
		if (summary.bHasIPFilterDownload) {
			strTitle = summary.strIPFilterTitle;
			strDetail = summary.strIPFilterDetail;
			uDone = summary.uIPFilterDone;
			uTotal = summary.uIPFilterTotal;
			bCanCancel = false;
			return;
		}
		if (summary.bHasIPGeolocationDownload) {
			strTitle = summary.strIPGeolocationTitle;
			strDetail = summary.strIPGeolocationDetail;
			uDone = summary.uIPGeolocationDone;
			uTotal = summary.uIPGeolocationTotal;
			bCanCancel = false;
			return;
		}

		if (summary.bHasStartupLoad) {
			strTitle = GetResString(_T("LOAD_DOWNLOADS"));
			strDetail.Format(GetResString(_T("BULKOP_LOAD_DOWNLOADS_DETAIL")), summary.uStartupLoaded, summary.uStartupTempDirIndex, summary.uStartupTempDirCount);
			uDone = 0;
			uTotal = 0;
			bCanCancel = false;
			return;
		}
		if (summary.bHasSearchStartupLoad) {
			strTitle = GetResString(_T("LOAD_SEARCHES"));
			strDetail.Format(GetResString(_T("BULKOP_LOAD_SEARCHES_DETAIL")), summary.uSearchStartupLoadedSearches, summary.uSearchStartupTotalSearches, summary.uSearchStartupLoadedFiles);
			uDone = 0;
			uTotal = 0;
			bCanCancel = false;
			return;
		}

		if (summary.bHasKnownFilesLoad) {
			strTitle = GetResStringOrDefault(_T("BULKOP_LOAD_KNOWNFILES_TITLE"), _T("Loading known files"));
			strDetail = GetResStringOrDefault(_T("BULKOP_LOAD_KNOWNFILES_DETAIL"), _T("Preparing the Files view"));
			uDone = 0;
			uTotal = 0;
			bCanCancel = false;
			return;
		}

		if (summary.bHasClientHistoryLoad) {
			strTitle = GetResStringOrDefault(_T("LOAD_CLIENTHISTORY"), _T("Loading client history"));
			strDetail = GetResStringOrDefault(_T("BULKOP_LOAD_CLIENTHISTORY_DETAIL"), _T("Preparing the Client List view"));
			uDone = 0;
			uTotal = 0;
			bCanCancel = false;
			return;
		}

		if (summary.bHasSharedFilesLoad) {
			strTitle = GetResStringOrDefault(_T("LOAD_SHAREDFILES"), _T("Loading shared files"));
			strDetail = GetResStringOrDefault(_T("BULKOP_LOAD_SHAREDFILES_DETAIL"), _T("Scanning shared folders"));
			if (theApp.sharedfiles != NULL) {
				CString strSharedDetail;
				theApp.sharedfiles->GetSharedFilesLoadProgress(uDone, uTotal, strSharedDetail);
				if (!strSharedDetail.IsEmpty())
					strDetail = strSharedDetail;
			} else {
				uDone = 0;
				uTotal = 0;
			}
			bCanCancel = false;
			return;
		}
		if (summary.bSavingDownloadsToDisk && summary.bHasAdd) {
			strTitle = GetResStringOrDefault(_T("EXIT_LOAD_TASK_DOWNLOADS"), _T("Saving downloads"));
			FormatBulkOperationProgressDetail(strDetail, summary.uDone, summary.uTotal, summary.bListUpdateAfterCompletion, true);
			bCanCancel = false;
			return;
		}
		if (summary.bHasAdd) {
			strTitle = GetResString(_T("BULKOP_ADD_DOWNLOADS_TITLE"));
		}
		else if (summary.bHasDelete) {
			strTitle = GetResString(_T("BULKOP_DELETE_DOWNLOADS_TITLE"));
		}
		else {
			if (summary.uCount > 1) {
				strTitle = GetResString(_T("BULKOP_MULTI_OPERATIONS_TITLE"));
				strDetail.Format(GetResString(_T("BULKOP_MULTI_OPERATIONS_DETAIL")), summary.uCount, summary.uDone, summary.uTotal);
				return;
			}
			strTitle = GetResString(summary.bMetadata ? _T("BULKOP_UPDATE_METADATA_TITLE") : (summary.bHashing ? _T("BULKOP_HASH_SHAREDFILES_TITLE") : _T("BULKOP_UPDATE_DOWNLOADS_TITLE")));
		}
		FormatBulkOperationProgressDetail(strDetail, summary.uDone, summary.uTotal, summary.bListUpdateAfterCompletion, summary.bSavingDownloadsToDisk);
	}

	static bool BuildActiveBulkOperationOverlayForList(const CemuleDlg* pDlg, bool bIncludeStartupLoad, bool bIncludeSearchStartupLoad, bool bIncludeSharedFilesLoad, bool bIncludeKnownFilesLoad, bool bIncludeClientHistoryLoad, bool bIncludeIPFilterDownload, bool bIncludeDownloadAddOperation, bool bIncludeSharedFilesBulkOperation, CString& strTitle, CString& strDetail, UINT& uDone, UINT& uTotal, bool& bCanCancel)
	{
		SActiveBulkOperationSummary summary;
		if (!CollectActiveBulkOperationSummary(pDlg, bIncludeStartupLoad, bIncludeSearchStartupLoad, bIncludeSharedFilesLoad, bIncludeKnownFilesLoad, bIncludeClientHistoryLoad, bIncludeIPFilterDownload, bIncludeDownloadAddOperation, bIncludeSharedFilesBulkOperation, bIncludeSharedFilesBulkOperation, summary))
			return false;
		BuildActiveBulkOperationOverlayText(summary, strTitle, strDetail, uDone, uTotal, bCanCancel);
		return true;
	}

	static void ShowActiveBulkOperationOverlayOnList(CMuleListCtrl* pList, const CString& strTitle, const CString& strDetail, UINT uDone, UINT uTotal, bool bCanCancel)
	{
		if (pList != NULL && ::IsWindow(pList->GetSafeHwnd()))
			pList->ShowOperationOverlay(strTitle, strDetail, uDone, uTotal, bCanCancel, bCanCancel ? GetResString(_T("BULKOP_CANCEL_REMAINING")) : CString());
	}

	static void HideActiveBulkOperationOverlayOnList(CMuleListCtrl* pList)
	{
		if (pList != NULL && ::IsWindow(pList->GetSafeHwnd()))
			pList->HideOperationOverlay();
	}
}

bool CemuleDlg::GetActiveBulkOperationCloseInfo(CString& strTitle, CString& strBody, CString& strCancelAndExit, CString& strWaitAndExit, UINT& uDone, UINT& uTotal, bool* pbCanCancel) const
{
	if (pbCanCancel != NULL)
		*pbCanCancel = false;

	SActiveBulkOperationSummary summary;
	if (!CollectActiveBulkOperationSummary(this, false, false, false, false, false, false, true, true, false, summary))
		return false;

	uDone = summary.uDone;
	uTotal = summary.uTotal;
	strTitle = GetResString(_T("BULKOP_EXIT_TITLE"));
	strWaitAndExit = GetResString(_T("BULKOP_EXIT_FINISH_AND_EXIT"));
	strCancelAndExit.Empty();

	if (summary.bSavingDownloadsToDisk && summary.bHasAdd) {
		strBody.Format(GetResStringOrDefault(_T("BULKOP_EXIT_SAVE_DOWNLOADS_BODY"), _T("Downloads are still being saved to disk.\n\nTotal: %u\nCompleted: %u\nRemaining: %u\n\nPlease let this finish before exiting so the newly added downloads are safely stored.")), summary.uTotal, summary.uDone, summary.uTotal >= summary.uDone ? summary.uTotal - summary.uDone : 0);
		return true;
	}

	if (summary.uCount > 1) {
		strBody.Format(GetResString(_T("BULKOP_EXIT_MULTI_BODY")), summary.uCount, summary.uTotal, summary.uDone, summary.uTotal >= summary.uDone ? summary.uTotal - summary.uDone : 0);
		if (summary.bCanCancel)
			strCancelAndExit = GetResString(_T("BULKOP_EXIT_CANCEL_ALL_AND_EXIT"));
		if (pbCanCancel != NULL)
			*pbCanCancel = summary.bCanCancel;
		return true;
	}

	if (summary.bHasAdd) {
		strBody.Format(GetResString(_T("BULKOP_EXIT_ADD_BODY")), summary.uTotal, summary.uDone, summary.uTotal >= summary.uDone ? summary.uTotal - summary.uDone : 0);
		if (summary.bCanCancel)
			strCancelAndExit = GetResString(_T("BULKOP_EXIT_CANCEL_ADD_AND_EXIT"));
	} else if (summary.bHasDelete) {
		strBody.Format(GetResString(_T("BULKOP_EXIT_DELETE_BODY")), summary.uTotal, summary.uDone, summary.uTotal >= summary.uDone ? summary.uTotal - summary.uDone : 0);
		if (summary.bCanCancel)
			strCancelAndExit = GetResString(_T("BULKOP_EXIT_CANCEL_DELETE_AND_EXIT"));
	} else {
		strBody.Format(GetResString(_T("BULKOP_EXIT_UPDATE_BODY")), summary.uTotal, summary.uDone, summary.uTotal >= summary.uDone ? summary.uTotal - summary.uDone : 0);
		if (summary.bCanCancel)
			strCancelAndExit = GetResString(_T("BULKOP_EXIT_CANCEL_ADD_AND_EXIT"));
	}
	if (pbCanCancel != NULL)
		*pbCanCancel = summary.bCanCancel;
	return true;
}

void CemuleDlg::CancelActiveBulkOperations()
{
	if (searchwnd != NULL && searchwnd->m_pwndResults != NULL)
		searchwnd->m_pwndResults->CancelActiveChunkedSearchDownload();

	if (transferwnd != NULL && transferwnd->GetDownloadList() != NULL)
		transferwnd->GetDownloadList()->CancelActiveChunkedDownloadOperation();

	if (sharedfileswnd != NULL)
		sharedfileswnd->sharedfilesctrl.ClearSharedFilesBulkOperation();

	theApp.CancelBackendDownloadListOperations();
	RefreshActiveBulkOperationOverlays();
}


void CemuleDlg::StartDownloadOverlayCompletionDelay()
{
	if (!::IsWindow(m_hWnd))
		return;
	if (SetTimer(TIMER_DOWNLOAD_OVERLAY_COMPLETION_DELAY, DOWNLOAD_OVERLAY_COMPLETION_DELAY_MS, NULL) != 0)
		return;

	KillTimer(TIMER_DOWNLOAD_OVERLAY_COMPLETION_DELAY);
	if (serverwnd != NULL)
		serverwnd->FinishServerMetDownloadOverlayDelay();
	CPPgSecurity::FinishIPFilterDownloadOverlayDelay();
	CIPGeolocation::FinishIPGeolocationDownloadOverlayDelay();
	RefreshActiveBulkOperationOverlays();
}

void CemuleDlg::SetActiveBulkOperationOverlaysSuppressed(bool bSuppress)
{
	if (searchwnd != NULL && searchwnd->m_pwndResults != NULL && ::IsWindow(searchwnd->m_pwndResults->searchlistctrl.GetSafeHwnd())) {
		searchwnd->m_pwndResults->searchlistctrl.SetOperationOverlaySuppressed(bSuppress);
	}

	if (transferwnd != NULL && transferwnd->GetDownloadList() != NULL && ::IsWindow(transferwnd->GetDownloadList()->GetSafeHwnd())) {
		transferwnd->GetDownloadList()->SetOperationOverlaySuppressed(bSuppress);
	}

	if (sharedfileswnd != NULL && ::IsWindow(sharedfileswnd->sharedfilesctrl.GetSafeHwnd())) {
		sharedfileswnd->sharedfilesctrl.SetOperationOverlaySuppressed(bSuppress);
	}

	if (transferwnd != NULL && transferwnd->GetClientList() != NULL && ::IsWindow(transferwnd->GetClientList()->GetSafeHwnd())) {
		transferwnd->GetClientList()->SetOperationOverlaySuppressed(bSuppress);
	}

	if (transferwnd != NULL && transferwnd->GetUploadList() != NULL && ::IsWindow(transferwnd->GetUploadList()->GetSafeHwnd())) {
		transferwnd->GetUploadList()->SetOperationOverlaySuppressed(bSuppress);
	}

	if (transferwnd != NULL && transferwnd->GetDownloadClientsList() != NULL && ::IsWindow(transferwnd->GetDownloadClientsList()->GetSafeHwnd())) {
		transferwnd->GetDownloadClientsList()->SetOperationOverlaySuppressed(bSuppress);
	}

	if (transferwnd != NULL && transferwnd->GetQueueList() != NULL && ::IsWindow(transferwnd->GetQueueList()->GetSafeHwnd())) {
		transferwnd->GetQueueList()->SetOperationOverlaySuppressed(bSuppress);
	}

	if (serverwnd != NULL && ::IsWindow(serverwnd->serverlistctrl.GetSafeHwnd())) {
		serverwnd->serverlistctrl.SetOperationOverlaySuppressed(bSuppress);
	}

	if (kademliawnd != NULL && kademliawnd->m_contactListCtrl != NULL && ::IsWindow(kademliawnd->m_contactListCtrl->GetSafeHwnd())) {
		kademliawnd->m_contactListCtrl->SetOperationOverlaySuppressed(bSuppress);
	}

	if (kademliawnd != NULL && kademliawnd->searchList != NULL && ::IsWindow(kademliawnd->searchList->GetSafeHwnd())) {
		kademliawnd->searchList->SetOperationOverlaySuppressed(bSuppress);
	}
}


void CemuleDlg::PostStartupOverlayRefresh()
{
	if (!::IsWindow(m_hWnd))
		return;
	if (InterlockedCompareExchange(&m_lStartupOverlayRefreshPending, 1, 0) != 0)
		return;
	if (!::PostMessage(m_hWnd, UWM_EMULEAI_STARTUP_OVERLAY_REFRESH, 0, 0))
		InterlockedExchange(&m_lStartupOverlayRefreshPending, 0);
}

LRESULT CemuleDlg::OnStartupOverlayRefresh(WPARAM, LPARAM)
{
	InterlockedExchange(&m_lStartupOverlayRefreshPending, 0);
	RefreshStartupLoadingDialogProgress(false);

	if (!IsStartupLoadingDialogVisible()) {
		const DWORD dwNow = ::GetTickCount();
		if (m_dwLastStartupOverlayBulkRefreshTick == 0 || static_cast<DWORD>(dwNow - m_dwLastStartupOverlayBulkRefreshTick) >= STARTUP_OVERLAY_BULK_REFRESH_THROTTLE_MS) {
			m_dwLastStartupOverlayBulkRefreshTick = dwNow;
			RefreshActiveBulkOperationOverlays();
		}
	}
	return 0;
}

void CemuleDlg::RefreshActiveBulkOperationOverlays()
{
	CMuleListCtrl* pSearchList = NULL;
	if (searchwnd != NULL && searchwnd->m_pwndResults != NULL && ::IsWindow(searchwnd->m_pwndResults->searchlistctrl.GetSafeHwnd()))
		pSearchList = &searchwnd->m_pwndResults->searchlistctrl;

	CMuleListCtrl* pDownloadList = NULL;
	CMuleListCtrl* pUploadList = NULL;
	CMuleListCtrl* pDownloadClientsList = NULL;
	CMuleListCtrl* pQueueList = NULL;
	CMuleListCtrl* pClientList = NULL;
	if (transferwnd != NULL) {
		if (transferwnd->GetDownloadList() != NULL && ::IsWindow(transferwnd->GetDownloadList()->GetSafeHwnd()))
			pDownloadList = transferwnd->GetDownloadList();
		if (transferwnd->GetUploadList() != NULL && ::IsWindow(transferwnd->GetUploadList()->GetSafeHwnd()))
			pUploadList = transferwnd->GetUploadList();
		if (transferwnd->GetDownloadClientsList() != NULL && ::IsWindow(transferwnd->GetDownloadClientsList()->GetSafeHwnd()))
			pDownloadClientsList = transferwnd->GetDownloadClientsList();
		if (transferwnd->GetQueueList() != NULL && ::IsWindow(transferwnd->GetQueueList()->GetSafeHwnd()))
			pQueueList = transferwnd->GetQueueList();
		if (transferwnd->GetClientList() != NULL && ::IsWindow(transferwnd->GetClientList()->GetSafeHwnd()))
			pClientList = transferwnd->GetClientList();
	}

	CMuleListCtrl* pSharedFilesList = NULL;
	if (sharedfileswnd != NULL && ::IsWindow(sharedfileswnd->sharedfilesctrl.GetSafeHwnd()))
		pSharedFilesList = &sharedfileswnd->sharedfilesctrl;

	CMuleListCtrl* pServerList = NULL;
	if (serverwnd != NULL && ::IsWindow(serverwnd->serverlistctrl.GetSafeHwnd()))
		pServerList = &serverwnd->serverlistctrl;

	CMuleListCtrl* pKadContactList = NULL;
	CMuleListCtrl* pKadSearchList = NULL;
	if (kademliawnd != NULL) {
		if (kademliawnd->m_contactListCtrl != NULL && ::IsWindow(kademliawnd->m_contactListCtrl->GetSafeHwnd()))
			pKadContactList = kademliawnd->m_contactListCtrl;
		if (kademliawnd->searchList != NULL && ::IsWindow(kademliawnd->searchList->GetSafeHwnd()))
			pKadSearchList = kademliawnd->searchList;
	}

	if (IsStartupLoadingDialogVisible()) {
		HideActiveBulkOperationOverlayOnList(pSearchList);
		HideActiveBulkOperationOverlayOnList(pDownloadList);
		HideActiveBulkOperationOverlayOnList(pUploadList);
		HideActiveBulkOperationOverlayOnList(pDownloadClientsList);
		HideActiveBulkOperationOverlayOnList(pQueueList);
		HideActiveBulkOperationOverlayOnList(pClientList);
		HideActiveBulkOperationOverlayOnList(pSharedFilesList);
		HideActiveBulkOperationOverlayOnList(pServerList);
		HideActiveBulkOperationOverlayOnList(pKadContactList);
		HideActiveBulkOperationOverlayOnList(pKadSearchList);
		return;
	}

	const bool bIPFilterDownloadActive = CPPgSecurity::IsIPFilterDownloadActive();
	const bool bIPGeolocationDownloadActive = CIPGeolocation::IsIPGeolocationDownloadActive();
	const bool bGeoAwareDownloadActive = bIPFilterDownloadActive || bIPGeolocationDownloadActive;
	CMuleListCtrl* pTargetList = NULL;
	if (activewnd == searchwnd && pSearchList != NULL && pSearchList->IsWindowVisible())
		pTargetList = pSearchList;
	else if (activewnd == sharedfileswnd && pSharedFilesList != NULL && pSharedFilesList->IsWindowVisible())
		pTargetList = pSharedFilesList;
	else if (activewnd == serverwnd && pServerList != NULL && pServerList->IsWindowVisible() && bGeoAwareDownloadActive)
		pTargetList = pServerList;
	else if (activewnd == kademliawnd && bGeoAwareDownloadActive && pKadContactList != NULL && pKadContactList->IsWindowVisible())
		pTargetList = pKadContactList;
	else if (activewnd == transferwnd) {
		const bool bDownloadListVisible = pDownloadList != NULL && pDownloadList->IsWindowVisible();
		const bool bUploadListVisible = pUploadList != NULL && pUploadList->IsWindowVisible();
		const bool bDownloadClientsListVisible = pDownloadClientsList != NULL && pDownloadClientsList->IsWindowVisible();
		const bool bQueueListVisible = pQueueList != NULL && pQueueList->IsWindowVisible();
		const bool bClientListVisible = pClientList != NULL && pClientList->IsWindowVisible();
		if (bDownloadListVisible && HasTransferDownloadOverlayOperation(this))
			pTargetList = pDownloadList;
		else if (bClientListVisible && HasClientHistoryLoadOperation(this))
			pTargetList = pClientList;
		else if (bDownloadListVisible)
			pTargetList = pDownloadList;
		else if (bUploadListVisible && bGeoAwareDownloadActive)
			pTargetList = pUploadList;
		else if (bDownloadClientsListVisible && bGeoAwareDownloadActive)
			pTargetList = pDownloadClientsList;
		else if (bQueueListVisible && bGeoAwareDownloadActive)
			pTargetList = pQueueList;
		else if (bClientListVisible)
			pTargetList = pClientList;
	}

	CString strTitle;
	CString strDetail;
	UINT uDone = 0;
	UINT uTotal = 0;
	bool bCanCancel = false;
	UINT uBackendOperationDone = 0;
	UINT uBackendOperationTotal = 0;
	bool bBackendAddSavingToDisk = false;
	const bool bBackendAddOperationActive = theApp.GetActiveDownloadAddOperationProgress(uBackendOperationDone, uBackendOperationTotal, &bBackendAddSavingToDisk);
	const bool bTargetIsIPFilterList = bGeoAwareDownloadActive && (pTargetList == pServerList || pTargetList == pDownloadList || pTargetList == pUploadList || pTargetList == pDownloadClientsList || pTargetList == pQueueList || pTargetList == pClientList || pTargetList == pKadContactList);
	bool bSharedFilesBulkDeleteLike = false;
	UINT uSharedFilesBulkDone = 0;
	UINT uSharedFilesBulkTotal = 0;
	const bool bSharedFilesBulkActive = sharedfileswnd != NULL && sharedfileswnd->sharedfilesctrl.GetActiveSharedFilesBulkOperationProgress(bSharedFilesBulkDeleteLike, uSharedFilesBulkDone, uSharedFilesBulkTotal);
	bool bTargetOverlayWasShown = false;
	bool bHasTargetOverlay = false;
	if (bTargetIsIPFilterList && pTargetList == pServerList && serverwnd != NULL && serverwnd->RefreshServerMetDownloadOverlay()) {
		bHasTargetOverlay = true;
		bTargetOverlayWasShown = true;
	} else if (pTargetList != NULL) {
		bool bDownloadValidatorOverlay = false;
		if (pTargetList == pSearchList && theApp.DownloadValidator != NULL) {
			const uint32 uActiveSearchID = searchwnd != NULL && searchwnd->m_pwndResults != NULL ? searchwnd->m_pwndResults->searchlistctrl.m_nResultsID : 0;
			const bool bNetworkSearchActive = searchwnd != NULL && searchwnd->m_pwndResults != NULL && searchwnd->m_pwndResults->IsNetworkSearchActive(uActiveSearchID);
			bool bDownloadValidatorSearchOverlay = false;
			if (!bNetworkSearchActive && theApp.searchlist != NULL)
				bDownloadValidatorSearchOverlay = theApp.searchlist->GetDownloadValidatorSearchProgress(uActiveSearchID, uDone, uTotal);
			bool bDownloadValidatorIndexOverlay = false;
			if (!bNetworkSearchActive && !bDownloadValidatorSearchOverlay)
				bDownloadValidatorIndexOverlay = theApp.DownloadValidator->GetBackgroundProgress(uDone, uTotal);
			bDownloadValidatorOverlay = bDownloadValidatorSearchOverlay || bDownloadValidatorIndexOverlay;
			if (bDownloadValidatorOverlay) {
				strTitle = GetResString(bDownloadValidatorIndexOverlay ? _T("DOWNLOAD_VALIDATOR_INDEX_PREPARATION") : _T("DOWNLOAD_VALIDATOR_SEARCH_RESULTS_CHECK"));
				strDetail.Format(GetResString(_T("BULKOP_PROGRESS_DETAIL")), uDone, uTotal);
				bCanCancel = false;
				bHasTargetOverlay = true;
			}
		}
		const bool bIncludeDownloadStartupLoad = pTargetList == pDownloadList || pTargetList == pSharedFilesList;
		const bool bIncludeSearchStartupLoad = pTargetList == pSearchList;
		const bool bIncludeSharedFilesStartupLoad = false;
		const bool bIncludeKnownFilesStartupLoad = pTargetList == pSharedFilesList;
		const bool bIncludeClientHistoryLoad = pTargetList == pClientList;
		const bool bIncludeDownloadAddOperation = pTargetList == pDownloadList || pTargetList == pSharedFilesList;
		const bool bIncludeSharedFilesBulkOperation = pTargetList == pSharedFilesList;
		if (!bDownloadValidatorOverlay)
			bHasTargetOverlay = BuildActiveBulkOperationOverlayForList(this, bIncludeDownloadStartupLoad, bIncludeSearchStartupLoad, bIncludeSharedFilesStartupLoad, bIncludeKnownFilesStartupLoad, bIncludeClientHistoryLoad, bTargetIsIPFilterList, bIncludeDownloadAddOperation, bIncludeSharedFilesBulkOperation, strTitle, strDetail, uDone, uTotal, bCanCancel);
	}
	if (bHasTargetOverlay && !bTargetOverlayWasShown)
		ShowActiveBulkOperationOverlayOnList(pTargetList, strTitle, strDetail, uDone, uTotal, bCanCancel);
	else if (!bHasTargetOverlay)
		HideActiveBulkOperationOverlayOnList(pTargetList);

	if (pSearchList != NULL && pSearchList != pTargetList)
		pSearchList->HideOperationOverlay();
	if (pDownloadList != NULL && pDownloadList != pTargetList && !bGeoAwareDownloadActive) {
		if (bBackendAddOperationActive) {
			strTitle = bBackendAddSavingToDisk ? GetResStringOrDefault(_T("EXIT_LOAD_TASK_DOWNLOADS"), _T("Saving downloads")) : GetResString(_T("BULKOP_ADD_DOWNLOADS_TITLE"));
			FormatBulkOperationProgressDetail(strDetail, uBackendOperationDone, uBackendOperationTotal, true, bBackendAddSavingToDisk);
			ShowActiveBulkOperationOverlayOnList(pDownloadList, strTitle, strDetail, uBackendOperationDone, uBackendOperationTotal, !bBackendAddSavingToDisk);
		} else
			pDownloadList->HideOperationOverlay();
	}
	if (pSharedFilesList != NULL && pSharedFilesList != pTargetList && !bGeoAwareDownloadActive) {
		if (bSharedFilesBulkActive) {
			strTitle = GetResString(bSharedFilesBulkDeleteLike ? _T("BULKOP_DELETE_DOWNLOADS_TITLE") : _T("BULKOP_UPDATE_DOWNLOADS_TITLE"));
			strDetail.Format(GetResString(_T("BULKOP_PROGRESS_DETAIL")), uSharedFilesBulkDone, uSharedFilesBulkTotal);
			ShowActiveBulkOperationOverlayOnList(pSharedFilesList, strTitle, strDetail, uSharedFilesBulkDone, uSharedFilesBulkTotal, true);
		} else if (bBackendAddOperationActive) {
			strTitle = bBackendAddSavingToDisk ? GetResStringOrDefault(_T("EXIT_LOAD_TASK_DOWNLOADS"), _T("Saving downloads")) : GetResString(_T("BULKOP_ADD_DOWNLOADS_TITLE"));
			FormatBulkOperationProgressDetail(strDetail, uBackendOperationDone, uBackendOperationTotal, true, bBackendAddSavingToDisk);
			ShowActiveBulkOperationOverlayOnList(pSharedFilesList, strTitle, strDetail, uBackendOperationDone, uBackendOperationTotal, !bBackendAddSavingToDisk);
		} else
			pSharedFilesList->HideOperationOverlay();
	}
	if (pClientList != NULL && pClientList != pTargetList && !bGeoAwareDownloadActive)
		pClientList->HideOperationOverlay();

	if (bGeoAwareDownloadActive) {
		HideActiveBulkOperationOverlayOnList(pKadSearchList);

		CMuleListCtrl* apIPFilterLists[] = { pServerList, pDownloadList, pUploadList, pDownloadClientsList, pQueueList, pClientList, pKadContactList };
		for (size_t i = 0; i < _countof(apIPFilterLists); ++i) {
			CMuleListCtrl* pList = apIPFilterLists[i];
			if (pList == NULL || pList == pTargetList)
				continue;
			if (pList == pServerList && serverwnd != NULL && serverwnd->RefreshServerMetDownloadOverlay())
				continue;
			if (BuildActiveBulkOperationOverlayForList(this, pList == pDownloadList, false, false, false, pList == pClientList, true, pList == pDownloadList, false, strTitle, strDetail, uDone, uTotal, bCanCancel))
				ShowActiveBulkOperationOverlayOnList(pList, strTitle, strDetail, uDone, uTotal, bCanCancel);
		}
		return;
	}

	HideActiveBulkOperationOverlayOnList(pUploadList);
	HideActiveBulkOperationOverlayOnList(pDownloadClientsList);
	HideActiveBulkOperationOverlayOnList(pQueueList);
	HideActiveBulkOperationOverlayOnList(pKadContactList);
	HideActiveBulkOperationOverlayOnList(pKadSearchList);
	if (serverwnd != NULL)
		serverwnd->RefreshServerMetDownloadOverlay();
	else
		HideActiveBulkOperationOverlayOnList(pServerList);
}

int CemuleDlg::ConfirmCloseWithActiveBulkOperations(const CString& strTitle, const CString& strBody, const CString& strCancelAndExit, const CString& strWaitAndExit)
{
	UINT uDone = 0;
	UINT uTotal = 0;
	bool bCanCancel = false;
	CString strRefreshTitle(strTitle);
	CString strRefreshBody(strBody);
	CString strRefreshCancelAndExit(strCancelAndExit);
	CString strRefreshWaitAndExit(strWaitAndExit);
	if (GetActiveBulkOperationCloseInfo(strRefreshTitle, strRefreshBody, strRefreshCancelAndExit, strRefreshWaitAndExit, uDone, uTotal, &bCanCancel)) {
		SetActiveBulkOperationOverlaysSuppressed(true);
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
		CBulkOperationExitOverlayWnd wndOverlay(this, strRefreshTitle, strRefreshBody, strRefreshCancelAndExit, strRefreshWaitAndExit, uDone, uTotal, bCanCancel);
		const int iResult = wndOverlay.DoModalOverlay();
		SetActiveBulkOperationOverlaysSuppressed(false);
		return iResult;
	}
	return IDC_BULKOP_EXIT_CANCEL_EXIT;
}

void CemuleDlg::ScheduleCloseAfterBulkOperations()
{
	m_bCloseAfterBulkOperations = true;
	if (::IsWindow(m_hWnd))
		SetTimer(TIMER_CLOSE_AFTER_BULK_OPERATIONS, 250, NULL);
}

void CemuleDlg::CheckCloseAfterBulkOperations()
{
	if (!m_bCloseAfterBulkOperations)
		return;

	CString strTitle;
	CString strBody;
	CString strCancelAndExit;
	CString strWaitAndExit;
	UINT uDone = 0;
	UINT uTotal = 0;
	if (GetActiveBulkOperationCloseInfo(strTitle, strBody, strCancelAndExit, strWaitAndExit, uDone, uTotal))
		return;

	if (::IsWindow(m_hWnd))
		KillTimer(TIMER_CLOSE_AFTER_BULK_OPERATIONS);
	PostMessage(WM_CLOSE);
}

bool CemuleDlg::CanClose()
{
	if (m_bCloseAfterBulkOperations) {
		CString strBulkTitle;
		CString strBulkBody;
		CString strBulkCancelAndExit;
		CString strBulkWaitAndExit;
		UINT uBulkDone = 0;
		UINT uBulkTotal = 0;
		if (!GetActiveBulkOperationCloseInfo(strBulkTitle, strBulkBody, strBulkCancelAndExit, strBulkWaitAndExit, uBulkDone, uBulkTotal)) {
			m_bCloseAfterBulkOperations = false;
			return true;
		}
	}

	if (theApp.m_app_state == APP_STATE_RUNNING) {
		CString strBulkTitle;
		CString strBulkBody;
		CString strBulkCancelAndExit;
		CString strBulkWaitAndExit;
		UINT uBulkDone = 0;
		UINT uBulkTotal = 0;
		if (GetActiveBulkOperationCloseInfo(strBulkTitle, strBulkBody, strBulkCancelAndExit, strBulkWaitAndExit, uBulkDone, uBulkTotal)) {
			theApp.m_app_state = APP_STATE_ASKCLOSE; //disable tray menu
			RestoreWindow(); // make sure the window is in foreground for this prompt
			const int iBulkResult = ConfirmCloseWithActiveBulkOperations(strBulkTitle, strBulkBody, strBulkCancelAndExit, strBulkWaitAndExit);
			if (iBulkResult == IDC_BULKOP_EXIT_RETURN) {
				if (theApp.m_app_state == APP_STATE_ASKCLOSE)
					theApp.m_app_state = APP_STATE_RUNNING;
				return false;
			}
			if (iBulkResult == IDC_BULKOP_EXIT_WAIT_EXIT) {
				if (theApp.m_app_state == APP_STATE_ASKCLOSE)
					theApp.m_app_state = APP_STATE_RUNNING;
				ScheduleCloseAfterBulkOperations();
				return false;
			}
			CancelActiveBulkOperations();
			return true;
		}
	}

	if (theApp.m_app_state == APP_STATE_RUNNING && thePrefs.IsConfirmExitEnabled()) {
		theApp.m_app_state = APP_STATE_ASKCLOSE; //disable tray menu
		if (m_pSplashWnd != NULL && m_pSplashWnd->IsAboutMode() && ::IsWindow(m_pSplashWnd->GetSafeHwnd()))
			m_pSplashWnd->DestroyWindow();
		RestoreWindow(); // make sure the window is in foreground for this prompt
		ExitBox request(this);
		request.DoModal();
		if (request.WasCancelled()) {
			if (theApp.m_app_state == APP_STATE_ASKCLOSE) //if the application state has not changed
				theApp.m_app_state = APP_STATE_RUNNING; //then keep running
			return false;
		}
	}
	return true;
}

void CemuleDlg::OnClose()
{
	static LONG closing = 0;
	if (::InterlockedExchange(&closing, 1))
		return; //already closing
	if (!CanClose()) {
		::InterlockedExchange(&closing, 0);
		return;
	}
	const bool bStartupLoadingExit = m_bStartupLoadingExitRequested;
	if (!bStartupLoadingExit) {
		WINDOWPLACEMENT wp;
		wp.length = (UINT)sizeof wp;
		if (GetWindowPlacement(&wp)) {
			if (wp.showCmd == SW_SHOWMINIMIZED && (wp.flags & WPF_RESTORETOMAXIMIZED))
				wp.showCmd = SW_SHOWMAXIMIZED;
			wp.flags = 0;
			thePrefs.SetWindowLayout(wp);
		}

		// Persist the last visible main page first and fall back to the cached active window.
		thePrefs.SetLastMainWndDlgID(GetMainWndDialogIdForPersistence(*this));
	}

	theApp.m_app_state = APP_STATE_SHUTTINGDOWN;
	ShowShutdownProgressDialog();
	const UINT uShutdownDownloadEstimate = (!bStartupLoadingExit && theApp.downloadqueue != NULL) ? static_cast<UINT>(min(static_cast<INT_PTR>(UINT_MAX), theApp.downloadqueue->filelist.GetCount())) : 0U;
	ConfigureShutdownProgressEstimates(uShutdownDownloadEstimate, !bStartupLoadingExit && thePrefs.GetSaveLoadSources());
	UpdateShutdownProgress(ShutdownProgressNetwork, 0, 1, true);
	KillMainTimer();

	notifierenabled = false;
	m_bNotifierRuntimeActive = false;
	ClearTrayBalloonNotificationPayload();
	m_wndToastNotifier.Shutdown();
	//flush queued messages
	theApp.HandleDebugLogQueue();
	theApp.HandleLogQueue();
	theApp.RefreshPartMetDiskSpaceCache();

	theApp.ConChecker->Stop();

	// Drain pending hash completion/failure messages before normal shutdown saves.
	MSG pendingMsg;
	while (::PeekMessage(&pendingMsg, m_hWnd, TM_FINISHEDHASHING, TM_HASHFAILED, PM_REMOVE)) {
		if (pendingMsg.message == TM_FINISHEDHASHING || pendingMsg.message == TM_HASHFAILED)
			DeleteSharedFileHashResult(reinterpret_cast<SharedFileHashResult_Struct*>(pendingMsg.lParam));
	}
	while (::PeekMessage(&pendingMsg, m_hWnd, TM_FINISHEDPARTFILEHASHING, TM_PARTFILEHASHFAILED, PM_REMOVE)) {
		PartFileHash_Struct* hashed = reinterpret_cast<PartFileHash_Struct*>(pendingMsg.lParam);
		DeletePartFileHashResult(hashed);
	}
	while (::PeekMessage(&pendingMsg, m_hWnd, TM_FILECOMPLETED, TM_FILECOMPLETED, PM_REMOVE)) {
		PartFileComplete_Struct* pComplete = reinterpret_cast<PartFileComplete_Struct*>(pendingMsg.lParam);
		delete pComplete;
	}

	Log(GetResString(_T("APP_CLOSING")));
	CloseTTS();
	m_pDropTarget->Revoke();
	theApp.serverconnect->Disconnect();
	theApp.OnlineSig(); // Added By Bouc7
	UpdateShutdownProgress(ShutdownProgressNetwork, 1, 1, true);

	UpdateShutdownProgress(ShutdownProgressDiskIo, 0, 1, true);
	Kademlia::CKademlia::Stop();	// couple of data files are written

	// try to wait until the hashing thread notices that we are shutting down
	CSingleLock sLock1(&theApp.hashing_mut); // only one file hash at a time
	sLock1.Lock(SEC2MS(2));

	theApp.PrepareBackendShutdownForDiskIo(_T("CemuleDlg::OnDestroy"));
	if (theApp.downloadqueue != NULL)
		theApp.downloadqueue->DrainDeferredPartFileDiskWorkForShutdown();
	if (theApp.m_pUploadDiskIOThread != NULL)
		theApp.m_pUploadDiskIOThread->EndThread();
	if (theApp.m_pPartFileWriteThread != NULL)
		theApp.m_pPartFileWriteThread->EndThread();
	UpdateShutdownProgress(ShutdownProgressDiskIo, 1, 1, true);
	UpdateShutdownProgress(ShutdownProgressSaveData, 0, 1, true);

	// saving data & stuff
	const bool bRunBackupOnExit = !bStartupLoadingExit && thePrefs.GetBackupOnExit();
	const UINT uDownloadCountForSourceSave = (!bStartupLoadingExit && theApp.downloadqueue != NULL) ? static_cast<UINT>(min(static_cast<INT_PTR>(UINT_MAX / 2), theApp.downloadqueue->filelist.GetCount())) : 0;
	const UINT uSourceProgressTotal = (!bStartupLoadingExit && thePrefs.GetSaveLoadSources()) ? uDownloadCountForSourceSave * 2 : 0;
	const UINT uFixedSaveDataTasks = 12U + (bRunBackupOnExit ? 1U : 0U);
	UINT uSaveDataDone = 0;
	const UINT uSaveDataTotal = max(1U, uSourceProgressTotal + uFixedSaveDataTasks);
	UpdateShutdownProgress(ShutdownProgressSaveData, uSaveDataDone, uSaveDataTotal, true);

	theApp.emuledlg->preferenceswnd->m_wndSecurity.DeleteDDB();
	UpdateShutdownProgress(ShutdownProgressSaveData, min(++uSaveDataDone, uSaveDataTotal), uSaveDataTotal, false);

	if (!bStartupLoadingExit) {
		SShutdownSourceSaveContext sourceSaveContext;
		if (thePrefs.GetSaveLoadSources() && theApp.downloadqueue != NULL) {
			sourceSaveContext.snapshots.reserve(uDownloadCountForSourceSave);
			for (POSITION pos = theApp.downloadqueue->filelist.GetHeadPosition(); pos != 0;) {
				CPartFile* file = theApp.downloadqueue->filelist.GetNext(pos);
				if (file != NULL && !file->IsStopped()) {
					SaveSourcesData* pSourcesSnapshot = file->m_sourcesaver.BuildSaveSourcesSnapshot(file, true, false);
					if (pSourcesSnapshot != NULL)
						sourceSaveContext.snapshots.push_back(pSourcesSnapshot);
				}
				++uSaveDataDone;
				if ((uSaveDataDone & 0x3F) == 0)
					UpdateShutdownProgress(ShutdownProgressSaveData, min(uSaveDataDone, uSaveDataTotal), uSaveDataTotal, false);
			}

			RunShutdownSourceSaveWorkers(this, sourceSaveContext, min(uSaveDataDone, uSaveDataTotal), uSaveDataTotal);
			uSaveDataDone = min(uSaveDataTotal, uSaveDataDone + static_cast<UINT>(min(static_cast<size_t>(UINT_MAX), sourceSaveContext.snapshots.size())));
			UpdateShutdownProgress(ShutdownProgressSaveData, uSaveDataDone, uSaveDataTotal, true);
		}

		theApp.knownfiles->Save();
		UpdateShutdownProgress(ShutdownProgressSaveData, min(++uSaveDataDone, uSaveDataTotal), uSaveDataTotal, false);
		theApp.sharedfiles->Save();
		UpdateShutdownProgress(ShutdownProgressSaveData, min(++uSaveDataDone, uSaveDataTotal), uSaveDataTotal, false);
		searchwnd->SaveAllSettings();
		UpdateShutdownProgress(ShutdownProgressSaveData, min(++uSaveDataDone, uSaveDataTotal), uSaveDataTotal, false);
		serverwnd->SaveAllSettings();
		UpdateShutdownProgress(ShutdownProgressSaveData, min(++uSaveDataDone, uSaveDataTotal), uSaveDataTotal, false);
		kademliawnd->SaveAllSettings();
		UpdateShutdownProgress(ShutdownProgressSaveData, min(++uSaveDataDone, uSaveDataTotal), uSaveDataTotal, false);

		theApp.scheduler->RestoreOriginals();
		UpdateShutdownProgress(ShutdownProgressSaveData, min(++uSaveDataDone, uSaveDataTotal), uSaveDataTotal, false);
		theApp.searchlist->SaveSpamFilter();
		UpdateShutdownProgress(ShutdownProgressSaveData, min(++uSaveDataDone, uSaveDataTotal), uSaveDataTotal, false);
		if (thePrefs.IsStoringSearchesEnabled())
			theApp.searchlist->StoreSearches();
		UpdateShutdownProgress(ShutdownProgressSaveData, min(++uSaveDataDone, uSaveDataTotal), uSaveDataTotal, false);
	}
	else {
		AddDebugLogLine(DLP_LOW, false, _T("Startup loading exit skips volatile data saves because startup metadata may be incomplete.\n"));
	}

	// close uPnP Ports
	theApp.m_pUPnPFinder->GetImplementation()->StopAsyncFind();
	UpdateShutdownProgress(ShutdownProgressSaveData, min(++uSaveDataDone, uSaveDataTotal), uSaveDataTotal, false);
	if (thePrefs.CloseUPnPOnExit())
		theApp.m_pUPnPFinder->GetImplementation()->DeletePorts();
	UpdateShutdownProgress(ShutdownProgressSaveData, min(++uSaveDataDone, uSaveDataTotal), uSaveDataTotal, false);

	thePrefs.Save();
	UpdateShutdownProgress(ShutdownProgressSaveData, min(++uSaveDataDone, uSaveDataTotal), uSaveDataTotal, false);
	thePerfLog.Shutdown();
	UpdateShutdownProgress(ShutdownProgressSaveData, min(++uSaveDataDone, uSaveDataTotal), uSaveDataTotal, false);

	if (bRunBackupOnExit) {
		theApp.Backup(true);
		UpdateShutdownProgress(ShutdownProgressSaveData, min(++uSaveDataDone, uSaveDataTotal), uSaveDataTotal, false);
	}
	UpdateShutdownProgress(ShutdownProgressSaveData, uSaveDataTotal, uSaveDataTotal, true);

	if (theApp.downloadqueue != NULL)
		theApp.downloadqueue->SavePartFilesForShutdown();
	else
		UpdateShutdownProgress(ShutdownProgressDownloads, 1, 1, true);
	UpdateShutdownProgress(ShutdownProgressCleanup, 0, 6, true);

	// explicitly delete all listview items which may hold ptrs to objects which will get deleted
	// by the dtors (some lines below) to avoid potential problems during application shutdown.
	transferwnd->GetDownloadList()->DeleteAllItems();
	chatwnd->chatselector.DeleteAllItems();
	chatwnd->m_FriendListCtrl.DeleteAllItems();
	theApp.clientlist->DeleteAll();
	transferwnd->GetQueueList()->DeleteAllItems();
	transferwnd->GetUploadList()->DeleteAllItems();
	transferwnd->GetDownloadClientsList()->DeleteAllItems();
	serverwnd->serverlistctrl.DeleteAllItems();
	UpdateShutdownProgress(ShutdownProgressCleanup, 1, 6, false);

	CPartFileConvert::CloseGUI();
	CPartFileConvert::RemoveAllJobs();

	theApp.uploadBandwidthThrottler->EndThread();
	theApp.lastCommonRouteFinder->EndThread();
	UpdateShutdownProgress(ShutdownProgressCleanup, 2, 6, false);

	theApp.sharedfiles->DeletePartFileInstances();
	UpdateShutdownProgress(ShutdownProgressCleanup, 3, 6, false);

	if (searchwnd != NULL)
		searchwnd->SendMessage(WM_CLOSE);
	if (transferwnd != NULL)
		transferwnd->SendMessage(WM_CLOSE);

	theApp.g_UtpSockets.clear();
	UpdateShutdownProgress(ShutdownProgressCleanup, 4, 6, false);

	// NOTE: Do not move those dtors into 'CemuleApp::InitInstance' (although they should be there). The
	// dtors are indirectly calling functions which access several windows which would not be available
	// after we have closed the main window -> crash!
	delete theApp.listensocket;				theApp.listensocket = NULL;
	delete theApp.clientudp;				theApp.clientudp = NULL;
	delete theApp.sharedfiles;				theApp.sharedfiles = NULL;
	delete theApp.serverconnect;			theApp.serverconnect = NULL;
	delete theApp.serverlist;				theApp.serverlist = NULL;		// CServerList::SaveServermetToFile
	delete theApp.knownfiles;				theApp.knownfiles = NULL;
	delete theApp.searchlist;				theApp.searchlist = NULL;
	delete theApp.clientcredits;			theApp.clientcredits = NULL;	// CClientCreditsList::SaveList
	delete theApp.downloadqueue;			theApp.downloadqueue = NULL;	// N * (CPartFile::FlushBuffer + CPartFile::SavePartFile)
	UpdateShutdownProgress(ShutdownProgressCleanup, 5, 6, false);
	delete theApp.uploadqueue;				theApp.uploadqueue = NULL;
	delete theApp.clientlist;				theApp.clientlist = NULL;
	delete theApp.friendlist;				theApp.friendlist = NULL;		// CFriendList::SaveList
	delete theApp.scheduler;				theApp.scheduler = NULL;
	delete theApp.ipfilter;					theApp.ipfilter = NULL;			// CIPFilter::SaveToDefaultFile
	delete theApp.searchlist;				theApp.searchlist = NULL;
	delete theApp.webserver;				theApp.webserver = NULL;
	delete theApp.uploadBandwidthThrottler;	theApp.uploadBandwidthThrottler = NULL;
	delete theApp.lastCommonRouteFinder;	theApp.lastCommonRouteFinder = NULL;
	delete theApp.m_pUPnPFinder;			theApp.m_pUPnPFinder = NULL;
	delete theApp.m_pUploadDiskIOThread;	theApp.m_pUploadDiskIOThread = NULL;
	delete theApp.m_pPartFileWriteThread;	theApp.m_pPartFileWriteThread = NULL;
	delete theApp.ConChecker;				theApp.ConChecker = NULL;
	delete theApp.DownloadValidator;			theApp.DownloadValidator = NULL;
	delete theApp.ipgeolocation;					theApp.ipgeolocation = NULL;
	delete theApp.shield;					theApp.shield = NULL;

	thePrefs.Uninit();
	UpdateShutdownProgress(ShutdownProgressCleanup, 6, 6, true);
	theApp.m_app_state = APP_STATE_DONE;
	HideShutdownProgressDialog();
	CTrayDialog::OnCancel();
	//flush queued messages
	theApp.HandleDebugLogQueue();
	theApp.HandleLogQueue();
	AddDebugLogLine(DLP_VERYLOW, _T("Closed eMule"));
}

void CemuleDlg::DestroyMiniMule()
{
	if (m_pMiniMule)
		if (m_pMiniMule->IsInInitDialog()) {
			TRACE("%s - *** Cannot destroy Minimule, it's still in 'OnInitDialog'\n", __FUNCTION__);
			m_pMiniMule->SetDestroyAfterInitDialog();
		} else if (m_pMiniMule->IsInCallback()) {
			TRACE("%s - *** Cannot destroy Minimule, it's still in callback. Hiding and deferring destruction.\n", __FUNCTION__);
			m_pMiniMule->SetDestroyAfterCallback();
			if (m_pMiniMule->m_hWnd && m_pMiniMule->IsWindowVisible())
				m_pMiniMule->ShowWindow(SW_HIDE);
		} else if (!m_pMiniMule->IsInCallback()) { // for safety
			TRACE("%s - m_pMiniMule->DestroyWindow();\n", __FUNCTION__);
			m_pMiniMule->DestroyWindow();
			ASSERT(m_pMiniMule == NULL);
			m_pMiniMule = NULL;
		}
}

LRESULT CemuleDlg::OnCloseMiniMule(WPARAM wParam, LPARAM)
{
	TRACE("%s -> DestroyMiniMule();\n", __FUNCTION__);
	DestroyMiniMule();
	if (wParam)
		RestoreWindow();
	return 0;
}

void CemuleDlg::OnTrayLButtonUp()
{
	if (theApp.IsClosing())
		return;

	// Avoid re-entrance problems with the main window, options dialog and minimule window
	if (IsPreferencesDlgOpen()) {
		MessageBeep(MB_OK);
		preferenceswnd->SetForegroundWindow();
		preferenceswnd->BringWindowToTop();
		return;
	}

	if (m_pMiniMule) {
		if (!m_pMiniMule->IsInInitDialog()) {
			TRACE("%s - m_pMiniMule->ShowWindow(SW_SHOW);\n", __FUNCTION__);
			m_pMiniMule->ShowWindow(SW_SHOW);
			m_pMiniMule->SetForegroundWindow();
			m_pMiniMule->BringWindowToTop();
		}
		return;
	}

	if (thePrefs.GetEnableMiniMule())
		try {
			TRACE("%s - m_pMiniMule = new CMiniMule(this);\n", __FUNCTION__);
			ASSERT(m_pMiniMule == NULL);
			m_pMiniMule = new CMiniMule(this);
			m_pMiniMule->Create(CMiniMule::IDD, this);
			if (m_pMiniMule->GetDestroyAfterInitDialog())
				DestroyMiniMule();
			else {
				m_pMiniMule->SetForegroundWindow();
				m_pMiniMule->BringWindowToTop();
			}
		} catch (...) {
			ASSERT(0);
			m_pMiniMule = NULL;
		}
	}

void CemuleDlg::OnTrayRButtonUp(CPoint pt)
{
	if (theApp.m_app_state != APP_STATE_RUNNING)
		return;

	// Avoid re-entrance problems with main window, options dialog and minimule window
	if (IsPreferencesDlgOpen()) {
		MessageBeep(MB_OK);
		preferenceswnd->SetForegroundWindow();
		preferenceswnd->BringWindowToTop();
		return;
	}

	if (m_pMiniMule) {
		if (m_pMiniMule->GetAutoClose()) {
			TRACE("%s - m_pMiniMule->GetAutoClose() -> DestroyMiniMule();\n", __FUNCTION__);
			DestroyMiniMule();
		} else if (m_pMiniMule->m_hWnd && !m_pMiniMule->IsWindowEnabled()) {
			// Avoid re-entrance problems with main window, options dialog and minimule window
			MessageBeep(MB_OK);
			return;
		}
	}

	if (m_pSystrayDlg) {
		m_pSystrayDlg->BringWindowToTop();
		return;
	}

	try {
		m_pSystrayDlg = new CMuleSystrayDlg(this, pt
			, thePrefs.GetMaxGraphUploadRate(true), thePrefs.GetMaxGraphDownloadRate()
			, thePrefs.GetMaxUpload(), thePrefs.GetMaxDownload());
	} catch (...) {
		return;
	}

	INT_PTR nResult = m_pSystrayDlg->DoModal();
	delete m_pSystrayDlg;
	m_pSystrayDlg = NULL;
	switch (nResult) {
	case IDC_TOMAX:
		QuickSpeedOther(MP_QS_UA);
		break;
	case IDC_TOMIN:
		QuickSpeedOther(MP_QS_PA);
		break;
	case IDC_RESTORE:
		RestoreWindow();
		break;
	case IDC_CONNECT:
		StartConnection(true);
		break;
	case IDC_DISCONNECT:
		CloseConnection();
		break;
	case IDC_EXIT:
		OnClose();
		break;
	case IDC_PREFERENCES:
		ShowPreferences();
	}
}

void CemuleDlg::AddSpeedSelectorMenus(CMenu* addToMenu)
{
	const CString& kbyps(GetResString(_T("KBYTESSEC")));
	// Create UploadPopup Menu
	ASSERT(m_menuUploadCtrl.m_hMenu == NULL);
	CString text;
	if (m_menuUploadCtrl.CreateMenu()) {
		int rate = thePrefs.GetMaxGraphUploadRate(true);
		text.Format(_T("20%%\t%i %s"), max(rate * 1 / 5, 1), (LPCTSTR)kbyps);
		m_menuUploadCtrl.AppendMenu(MF_STRING, MP_QS_U20, text);
		text.Format(_T("40%%\t%i %s"), max(rate * 2 / 5, 1), (LPCTSTR)kbyps);
		m_menuUploadCtrl.AppendMenu(MF_STRING, MP_QS_U40, text);
		text.Format(_T("60%%\t%i %s"), max(rate * 3 / 5, 1), (LPCTSTR)kbyps);
		m_menuUploadCtrl.AppendMenu(MF_STRING, MP_QS_U60, text);
		text.Format(_T("80%%\t%i %s"), max(rate * 4 / 5, 1), (LPCTSTR)kbyps);
		m_menuUploadCtrl.AppendMenu(MF_STRING, MP_QS_U80, text);
		text.Format(_T("100%%\t%i %s"), rate, (LPCTSTR)kbyps);
		m_menuUploadCtrl.AppendMenu(MF_STRING, MP_QS_U100, text);
		m_menuUploadCtrl.AppendMenu(MF_SEPARATOR);

		if (GetRecMaxUpload() > 0) {
			text.Format(GetResString(_T("PW_MINREC")) + GetResString(_T("KBYTESSEC")), GetRecMaxUpload());
			m_menuUploadCtrl.AppendMenu(MF_STRING, MP_QS_UP10, text);
		}

		text = GetResString(_T("PW_UPL")) + _T(':');
		addToMenu->AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_menuUploadCtrl.m_hMenu, text);
	}

	// Create DownloadPopup Menu
	ASSERT(m_menuDownloadCtrl.m_hMenu == NULL);
	if (m_menuDownloadCtrl.CreateMenu()) {
		int rate = thePrefs.GetMaxGraphDownloadRate();
		text.Format(_T("20%%\t%i %s"), (int)(rate * 0.2), (LPCTSTR)kbyps);
		m_menuDownloadCtrl.AppendMenu(MF_STRING | MF_POPUP, MP_QS_D20, text);
		text.Format(_T("40%%\t%i %s"), (int)(rate * 0.4), (LPCTSTR)kbyps);
		m_menuDownloadCtrl.AppendMenu(MF_STRING | MF_POPUP, MP_QS_D40, text);
		text.Format(_T("60%%\t%i %s"), (int)(rate * 0.6), (LPCTSTR)kbyps);
		m_menuDownloadCtrl.AppendMenu(MF_STRING | MF_POPUP, MP_QS_D60, text);
		text.Format(_T("80%%\t%i %s"), (int)(rate * 0.8), (LPCTSTR)kbyps);
		m_menuDownloadCtrl.AppendMenu(MF_STRING | MF_POPUP, MP_QS_D80, text);
		text.Format(_T("100%%\t%i %s"), rate, (LPCTSTR)kbyps);
		m_menuDownloadCtrl.AppendMenu(MF_STRING | MF_POPUP, MP_QS_D100, text);

		text = GetResString(_T("PW_DOWNL")) + _T(':');
		addToMenu->AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_menuDownloadCtrl.m_hMenu, text);
	}

	addToMenu->AppendMenu(MF_SEPARATOR);
	addToMenu->AppendMenu(MF_STRING | MF_GRAYED, MP_CONNECT, GetResStringWithAccel(_T("IRC_CONNECT"), _T('C')));
}


CString CemuleDlg::FormatBindResolveFailure(ENetBindResolveResult eResult, const CString& strInterfaceName, const CString& strAddress) const
{
	CString strTarget;
	if (!strAddress.IsEmpty())
		strTarget = strAddress;
	else if (!strInterfaceName.IsEmpty())
		strTarget = strInterfaceName;
	else if (!thePrefs.GetBindInterfaceId().IsEmpty())
		strTarget = thePrefs.GetBindInterfaceId();

	CString strFormat;
	switch (eResult) {
	case NBR_InterfaceNotFound:
		strFormat = GetResString(_T("NETBIND_REASON_INTERFACE_NOT_FOUND"));
		break;
	case NBR_InterfaceNameAmbiguous:
		strFormat = GetResString(_T("NETBIND_REASON_INTERFACE_AMBIGUOUS"));
		break;
	case NBR_InterfaceHasNoAddress:
		strFormat = GetResString(_T("NETBIND_REASON_INTERFACE_NO_ADDRESS"));
		break;
	case NBR_AddressNotFoundOnInterface:
		strFormat = GetResString(_T("NETBIND_REASON_ADDRESS_NOT_ON_INTERFACE"));
		break;
	case NBR_AddressNotFound:
		strFormat = GetResString(_T("NETBIND_REASON_ADDRESS_NOT_FOUND"));
		break;
	case NBR_InvalidAddress:
		strFormat = GetResString(_T("NETBIND_REASON_INVALID_ADDRESS"));
		break;
	default:
		return GetResString(_T("NETBIND_REASON_UNKNOWN"));
	}

	CString strReason;
	if (!strTarget.IsEmpty())
		strReason.Format(strFormat, (LPCTSTR)strTarget);
	else
		strReason = GetResString(_T("NETBIND_REASON_UNKNOWN"));
	return strReason;
}

bool CemuleDlg::ShouldBlockNetworkingForIpGuard(CString& strReason) const
{
	strReason.Empty();
	if (!thePrefs.IsIpGuardEnabled() || !thePrefs.HasExplicitBindSelection())
		return false;

	thePrefs.RefreshBindResolution();
	const ENetBindResolveResult eResult = thePrefs.GetActiveBindResolveResult();
	if (eResult == NBR_Default || eResult == NBR_Resolved)
		return false;

	CString strDetail = FormatBindResolveFailure(eResult, thePrefs.GetBindInterfaceName(), thePrefs.GetConfiguredBindAddr());
	strReason.Format(GetResString(_T("IP_GUARD_STARTUP_BIND_UNAVAILABLE_FMT")), (LPCTSTR)strDetail);
	return true;
}

void CemuleDlg::ApplySessionNetworkBlock(const CString& strReason, const CString& strOverlayText)
{
	CString strBlockReason(strReason);
	strBlockReason.Trim();
	if (strBlockReason.IsEmpty())
		strBlockReason = GetResString(IP_GUARD_OVERLAY_GENERIC_KEY);

	CString strBlockOverlay(strOverlayText);
	strBlockOverlay.Trim();
	if (strBlockOverlay.IsEmpty())
		strBlockOverlay = GetResString(IP_GUARD_OVERLAY_GENERIC_KEY);

	const bool bWasBlocked = m_bIpGuardStartupBlocked;
	const bool bReasonChanged = !bWasBlocked || m_strIpGuardStartupBlockReason.Compare(strBlockReason) != 0;
	if (!bWasBlocked) {
		m_bIpGuardRestoreEd2kConnection = theApp.serverconnect != NULL && (theApp.serverconnect->IsConnected() || theApp.serverconnect->IsConnecting());
		m_bIpGuardRestoreKadConnection = Kademlia::CKademlia::IsRunning();
	}
	m_bIpGuardStartupBlocked = true;
	m_strIpGuardStartupBlockReason = strBlockReason;
	m_strIpGuardOverlayText = strBlockOverlay;

	UnregisterIpGuardNotifications();

	if (theApp.serverconnect != NULL) {
		theApp.serverconnect->StopConnectionTry();
		theApp.serverconnect->Disconnect();
		theApp.serverconnect->CloseServerUDPSocketForIpGuardBlock();
	}
	Kademlia::CKademlia::Stop();
	if (theApp.listensocket != NULL) {
		theApp.listensocket->DisconnectAllSockets(_T("Network Guard network block"));
		theApp.listensocket->CloseForIpGuardBlock();
	}
	if (theApp.clientudp != NULL)
		theApp.clientudp->CloseForIpGuardBlock();
	if (theApp.webserver != NULL)
		theApp.webserver->StopServer();
	if (m_hUPnPTimeOutTimer != 0) {
		VERIFY(::KillTimer(NULL, m_hUPnPTimeOutTimer));
		m_hUPnPTimeOutTimer = 0;
	}
	if (theApp.m_pUPnPFinder != NULL) {
		try {
			theApp.m_pUPnPFinder->GetImplementation()->StopAsyncFind();
		} catch (const CUPnPImpl::UPnPError&) {
		} catch (CException* ex) {
			ex->Delete();
		}
	}

	m_bEd2kSuspendDisconnect = false;
	m_bKadSuspendDisconnect = false;
	m_bConnectRequestDelayedForUPnP = false;
	theApp.OnlineSig();
	ShowConnectionState();
	if (toolbar != NULL)
		toolbar->EnableButton(TBBTN_CONNECT, TRUE);
	m_SysMenuOptions.EnableMenuItem(MP_CONNECT, MF_ENABLED);
	if (serverwnd != NULL && serverwnd->GetSafeHwnd() != NULL)
		serverwnd->GetDlgItem(IDC_ED2KCONNECT)->EnableWindow(TRUE);
	if (kademliawnd != NULL)
		kademliawnd->UpdateControlsState();

	if (bReasonChanged) {
		LogError(LOG_STATUSBAR, _T("%s"), (LPCTSTR)strBlockReason);
		if (thePrefs.GetNotifierOnImportantError())
			ShowNotifier(strBlockReason, TBN_IMPORTANTEVENT);
	}
	InvalidateTitleVersionFrame();
}

bool CemuleDlg::GetRequestedSessionNetworkBlock(CString& strReason, CString& strOverlayText) const
{
	if (m_bIpGuardNetworkBlockActive) {
		strReason = m_strIpGuardBlockReason;
		strOverlayText = m_strIpGuardBlockOverlayText;
		return true;
	}
	if (m_bVpnGuardNetworkBlockActive) {
		strReason = m_strVpnGuardBlockReason;
		strOverlayText = m_strVpnGuardBlockOverlayText;
		return true;
	}
	strReason.Empty();
	strOverlayText.Empty();
	return false;
}

void CemuleDlg::RefreshSessionNetworkBlock(bool bRestartLocalSockets)
{
	CString strReason;
	CString strOverlayText;
	if (GetRequestedSessionNetworkBlock(strReason, strOverlayText)) {
		ApplySessionNetworkBlock(strReason, strOverlayText);
		return;
	}

	if (!m_bIpGuardStartupBlocked)
		return;

	m_bIpGuardStartupBlocked = false;
	m_strIpGuardStartupBlockReason.Empty();
	m_strIpGuardOverlayText.Empty();
	m_bIpGuardStartupProbePending = false;
	m_bIpGuardRuntimeProbePending = false;
	m_bVpnGuardProbePending = false;
	m_iVpnGuardPendingProbeCount = 0;
	m_bVpnGuardProbeHadSuccess = false;
	m_dwLastIpGuardRuntimeProbeTick = 0;
	m_strVpnGuardLastPublicAddress.Empty();

	if (bRestartLocalSockets && theApp.IsRunning() && !theApp.IsClosing()) {
		CNetworkBindSocketCreationScope allowSocketCreation;
		if (thePrefs.IsUPnPEnabled())
			StartUPnP();
		if (thePrefs.GetWSIsEnabled() && theApp.webserver != NULL)
			theApp.webserver->StartServer();
		if (theApp.listensocket != NULL && !theApp.listensocket->RecreateListeningSocket()) {
			CString strError;
			strError.Format(GetResString(_T("MAIN_SOCKETERROR")), thePrefs.GetPort());
			LogError(LOG_STATUSBAR, _T("%s"), (LPCTSTR)strError);
		}
		if (theApp.clientudp != NULL && !theApp.clientudp->Recreate()) {
			CString strError;
			strError.Format(GetResString(_T("MAIN_SOCKETERROR")), thePrefs.GetUDPPort());
			LogError(LOG_STATUSBAR, _T("%s"), (LPCTSTR)strError);
		}
		if (theApp.serverconnect != NULL)
			theApp.serverconnect->RecreateServerUDPSocket();
	}

	if (toolbar != NULL)
		toolbar->EnableButton(TBBTN_CONNECT, TRUE);
	m_SysMenuOptions.EnableMenuItem(MP_CONNECT, MF_ENABLED);
	if (serverwnd != NULL && serverwnd->GetSafeHwnd() != NULL)
		serverwnd->GetDlgItem(IDC_ED2KCONNECT)->EnableWindow(TRUE);
	if (kademliawnd != NULL)
		kademliawnd->UpdateControlsState();
	InvalidateTitleVersionFrame();
}

void CemuleDlg::ApplyIpGuardNetworkBlock(const CString& strReason, const CString& strOverlayText)
{
	m_bIpGuardNetworkBlockActive = true;
	m_strIpGuardBlockReason = strReason;
	m_strIpGuardBlockOverlayText = strOverlayText;
	m_bIpGuardStartupApproved = false;
	m_bIpGuardStartupProbePending = false;
	m_bIpGuardRuntimeProbePending = false;
	m_dwLastIpGuardRuntimeProbeTick = 0;
	RefreshSessionNetworkBlock(false);
}

void CemuleDlg::ClearIpGuardNetworkBlock(bool bRestartLocalSockets)
{
	m_bIpGuardNetworkBlockActive = false;
	m_strIpGuardBlockReason.Empty();
	m_strIpGuardBlockOverlayText.Empty();
	m_bIpGuardStartupProbePending = false;
	m_bIpGuardRuntimeProbePending = false;
	m_dwLastIpGuardRuntimeProbeTick = 0;
	RefreshSessionNetworkBlock(bRestartLocalSockets);
}

void CemuleDlg::ApplyVpnGuardNetworkBlock(const CString& strReason, const CString& strOverlayText)
{
	m_bVpnGuardNetworkBlockActive = true;
	m_strVpnGuardBlockReason = strReason;
	m_strVpnGuardBlockOverlayText = strOverlayText;
	m_bVpnGuardStartupApproved = false;
	m_bVpnGuardProbePending = false;
	m_iVpnGuardPendingProbeCount = 0;
	m_bVpnGuardProbeHadSuccess = false;
	RefreshSessionNetworkBlock(false);
	if (m_bVpnGuardMonitorActive)
		RegisterIpGuardNotifications();
}

void CemuleDlg::ClearVpnGuardNetworkBlock(bool bRestartLocalSockets)
{
	m_bVpnGuardNetworkBlockActive = false;
	m_strVpnGuardBlockReason.Empty();
	m_strVpnGuardBlockOverlayText.Empty();
	m_bVpnGuardProbePending = false;
	m_iVpnGuardPendingProbeCount = 0;
	m_bVpnGuardProbeHadSuccess = false;
	RefreshSessionNetworkBlock(bRestartLocalSockets);
}

void CemuleDlg::TryRestoreIpGuardNetworkConnections()
{
	if (m_bIpGuardStartupBlocked || !theApp.IsRunning() || theApp.IsClosing() || !CanUseP2PConnectionCommands())
		return;
	if (!m_bIpGuardRestoreEd2kConnection && !m_bIpGuardRestoreKadConnection)
		return;

	const bool bRestoreEd2k = m_bIpGuardRestoreEd2kConnection && theApp.serverconnect != NULL && !theApp.serverconnect->IsConnected() && !theApp.serverconnect->IsConnecting();
	const bool bRestoreKad = m_bIpGuardRestoreKadConnection && !Kademlia::CKademlia::IsRunning();
	m_bIpGuardRestoreEd2kConnection = false;
	m_bIpGuardRestoreKadConnection = false;
	if (bRestoreEd2k || bRestoreKad)
		RestoreConnectionAfterNetworkBindChange(bRestoreEd2k, bRestoreKad);
}

bool CemuleDlg::IsIpGuardMonitorConfigured(CString& strReason)
{
	strReason.Empty();
	m_strIpGuardExpectedBindAddress.Empty();
	m_strIpGuardExpectedBindTarget.Empty();

	if (!thePrefs.IsIpGuardEnabled() || !thePrefs.HasExplicitBindSelection() || theApp.IsClosing())
		return false;

	thePrefs.RefreshBindResolution();
	const ENetBindResolveResult eResult = thePrefs.GetActiveBindResolveResult();
	const CString strAddress(thePrefs.GetP2PBindAddrW() != NULL ? thePrefs.GetP2PBindAddrW() : _T(""));
	if (eResult != NBR_Resolved || strAddress.IsEmpty()) {
		CString strDetail = FormatBindResolveFailure(eResult, thePrefs.GetBindInterfaceName(), thePrefs.GetConfiguredBindAddr());
		strReason.Format(GetResString(_T("IP_GUARD_RUNTIME_BIND_UNAVAILABLE_FMT")), (LPCTSTR)strDetail);
		return false;
	}

	m_strIpGuardExpectedBindAddress = strAddress;
	if (!thePrefs.GetActiveBindInterfaceName().IsEmpty())
		m_strIpGuardExpectedBindTarget = thePrefs.GetActiveBindInterfaceName();
	else if (!thePrefs.GetBindInterfaceId().IsEmpty())
		m_strIpGuardExpectedBindTarget = thePrefs.GetBindInterfaceId();
	else
		m_strIpGuardExpectedBindTarget = thePrefs.GetConfiguredBindAddr();
	return true;
}

void CemuleDlg::RegisterIpGuardNotifications()
{
	UnregisterIpGuardNotifications();
	if (m_hWnd == NULL || !::IsWindow(m_hWnd))
		return;

	CString strReason;
	const bool bIpGuardConfigured = IsIpGuardMonitorConfigured(strReason);
	if (!bIpGuardConfigured && !thePrefs.IsVpnGuardEnabled())
		return;

	bool bNotificationArmed = false;
	HMODULE hIpHlp = GetNetBindIpHelperModule();
	if (hIpHlp != NULL) {
		PNotifyNetBindIpInterfaceChange pNotifyIpInterfaceChange = (PNotifyNetBindIpInterfaceChange)(void*)::GetProcAddress(hIpHlp, "NotifyIpInterfaceChange");
		PNotifyNetBindUnicastIpAddressChange pNotifyUnicastIpAddressChange = (PNotifyNetBindUnicastIpAddressChange)(void*)::GetProcAddress(hIpHlp, "NotifyUnicastIpAddressChange");
		if (pNotifyIpInterfaceChange != NULL && pNotifyUnicastIpAddressChange != NULL) {
			const LONG lGeneration = ::InterlockedIncrement(&g_lNetBindNotificationGeneration);
			g_hNetBindNotificationWnd = m_hWnd;
			::InterlockedExchange(&g_lNetBindNotificationCancelled, 0);
			::InterlockedExchange(&g_lNetBindAddressChangePostPending, 0);
			PVOID pCallbackContext = reinterpret_cast<PVOID>(static_cast<INT_PTR>(lGeneration));

			DWORD dwResult = pNotifyIpInterfaceChange(static_cast<USHORT>(AF_UNSPEC), NetBindInterfaceChangeCallback, pCallbackContext, FALSE, &m_hIpGuardInterfaceNotification);
			if (dwResult != NO_ERROR) {
				m_hIpGuardInterfaceNotification = NULL;
				TRACE(_T("IP Guard interface change notification registration failed: %lu\n"), dwResult);
			}
			else
				bNotificationArmed = true;

			dwResult = pNotifyUnicastIpAddressChange(static_cast<USHORT>(AF_UNSPEC), NetBindAddressChangeCallback, pCallbackContext, FALSE, &m_hIpGuardAddressNotification);
			if (dwResult != NO_ERROR) {
				m_hIpGuardAddressNotification = NULL;
				TRACE(_T("IP Guard address change notification registration failed: %lu\n"), dwResult);
			}
			else
				bNotificationArmed = true;
		}
		else
			TRACE(_T("IP Guard change notification API is not available.\n"));
	}
	else
		TRACE(_T("IP Guard change notification API is not available.\n"));

	const bool bTimerArmed = SetTimer(TIMER_IP_GUARD_MONITOR, SEC2MS(10), NULL) != 0;
	if (!bTimerArmed)
		TRACE(_T("Network Guard monitor timer could not be started.\n"));
	m_bIpGuardMonitorActive = bIpGuardConfigured && (bNotificationArmed || bTimerArmed);
}

void CemuleDlg::UnregisterIpGuardNotifications()
{
	m_bIpGuardMonitorActive = false;
	if (m_hWnd != NULL && ::IsWindow(m_hWnd) && !m_bVpnGuardMonitorActive)
		KillTimer(TIMER_IP_GUARD_MONITOR);
	::InterlockedIncrement(&g_lNetBindNotificationGeneration);
	::InterlockedExchange(&g_lNetBindNotificationCancelled, 1);
	::InterlockedExchange(&g_lNetBindAddressChangePostPending, 0);
	g_hNetBindNotificationWnd = NULL;

	HMODULE hIpHlp = GetNetBindIpHelperModule();
	PCancelNetBindMibChangeNotify2 pCancelMibChangeNotify2 = hIpHlp != NULL
		? (PCancelNetBindMibChangeNotify2)(void*)::GetProcAddress(hIpHlp, "CancelMibChangeNotify2")
		: NULL;
	if (m_hIpGuardInterfaceNotification != NULL) {
		if (pCancelMibChangeNotify2 != NULL)
			pCancelMibChangeNotify2(m_hIpGuardInterfaceNotification);
		m_hIpGuardInterfaceNotification = NULL;
	}
	if (m_hIpGuardAddressNotification != NULL) {
		if (pCancelMibChangeNotify2 != NULL)
			pCancelMibChangeNotify2(m_hIpGuardAddressNotification);
		m_hIpGuardAddressNotification = NULL;
	}
}

void CemuleDlg::UpdateIpGuardMonitor(bool bForcePublicIpProbe)
{
	if (!thePrefs.IsIpGuardEnabled()) {
		ClearIpGuardNetworkBlock(true);
		TryRestoreIpGuardNetworkConnections();
		m_bIpGuardStartupApproved = false;
		m_bIpGuardStartupProbePending = false;
		m_bIpGuardRuntimeProbePending = false;
		m_strIpGuardExpectedBindAddress.Empty();
		m_strIpGuardExpectedBindTarget.Empty();
		UnregisterIpGuardNotifications();
		if (thePrefs.IsVpnGuardEnabled())
			UpdateVpnGuardMonitor(bForcePublicIpProbe);
		return;
	}

	CString strReason;
	if (ShouldBlockNetworkingForIpGuard(strReason)) {
		ApplyIpGuardNetworkBlock(strReason, GetResString(IP_GUARD_OVERLAY_BIND_UNAVAILABLE_KEY));
		return;
	}

	std::vector<SIpGuardAllowedPublicIpRange> ranges;
	CString strError;
	if (!CIpGuard::TryParseAllowedPublicIpRanges(thePrefs.GetIpGuardAllowedPublicIpRanges(), ranges, strError)) {
		SIpGuardPublicIpProbeResult result;
		ApplyIpGuardNetworkBlock(FormatIpGuardPublicIpMessage(true, result));
		return;
	}
	if (!ranges.empty()) {
		if (bForcePublicIpProbe || m_bIpGuardStartupBlocked || !m_bIpGuardStartupApproved) {
			const CString strVerifying(GetResString(_T("IP_GUARD_PUBLIC_IP_VERIFYING")));
			ApplyIpGuardNetworkBlock(strVerifying, GetResString(IP_GUARD_OVERLAY_VERIFYING_KEY));
			if (!m_bIpGuardStartupProbePending && !m_bIpGuardRuntimeProbePending)
				StartIpGuardPublicIpProbe(_T("runtime"));
			return;
		}
	}

	ClearIpGuardNetworkBlock(true);
	RegisterIpGuardNotifications();
	if (!CanUseP2PConnectionCommands()) {
		ApplyIpGuardNetworkBlock(GetResString(_T("IP_GUARD_COMMANDS_BLOCKED_MONITOR")));
		return;
	}
	CheckIpGuardRuntimeBind();
	if (m_bIpGuardStartupBlocked)
		return;
	CheckIpGuardPublicIpMonitor(bForcePublicIpProbe);
	if (m_bIpGuardStartupBlocked)
		return;
	if (toolbar != NULL)
		toolbar->EnableButton(TBBTN_CONNECT, TRUE);
	m_SysMenuOptions.EnableMenuItem(MP_CONNECT, MF_ENABLED);
	if (serverwnd != NULL && serverwnd->GetSafeHwnd() != NULL)
		serverwnd->GetDlgItem(IDC_ED2KCONNECT)->EnableWindow(TRUE);
	if (kademliawnd != NULL)
		kademliawnd->UpdateControlsState();
	TryRestoreIpGuardNetworkConnections();
}

void CemuleDlg::CheckIpGuardRuntimeBind()
{
	if (!thePrefs.IsIpGuardEnabled() || !m_bIpGuardMonitorActive || m_bIpGuardStartupBlocked || theApp.IsClosing())
		return;

	SNetBindResolution resolution;
	const ENetBindResolveResult eResult = CNetBind::Resolve(thePrefs.GetBindInterfaceId(), thePrefs.GetBindInterfaceName(), thePrefs.GetConfiguredBindAddr(), resolution);
	CString strMessage;
	if (eResult != NBR_Resolved || resolution.strResolvedAddress.IsEmpty()) {
		const CString strDetail = FormatBindResolveFailure(eResult, thePrefs.GetBindInterfaceName(), thePrefs.GetConfiguredBindAddr());
		strMessage.Format(GetResString(_T("IP_GUARD_RUNTIME_BIND_UNAVAILABLE_FMT")), (LPCTSTR)strDetail);
	}
	else if (!m_strIpGuardExpectedBindAddress.IsEmpty() && resolution.strResolvedAddress.CompareNoCase(m_strIpGuardExpectedBindAddress) != 0) {
		CString strTarget(m_strIpGuardExpectedBindTarget);
		if (strTarget.IsEmpty())
			strTarget = thePrefs.GetBindInterfaceName().IsEmpty() ? thePrefs.GetConfiguredBindAddr() : thePrefs.GetBindInterfaceName();
		strMessage.Format(GetResString(_T("IP_GUARD_RUNTIME_BIND_ADDRESS_CHANGED_FMT")), (LPCTSTR)m_strIpGuardExpectedBindAddress, (LPCTSTR)resolution.strResolvedAddress, (LPCTSTR)strTarget);
	}
	else
		return;

	ApplyIpGuardNetworkBlock(strMessage, GetResString(IP_GUARD_OVERLAY_BIND_UNAVAILABLE_KEY));
}

CString CemuleDlg::FormatIpGuardPublicIpMessage(bool bRuntime, const SIpGuardPublicIpProbeResult& result) const
{
	const CString strRanges(thePrefs.GetIpGuardAllowedPublicIpRanges());
	CString strMessage;
	if (result.bSucceeded) {
		const CString strPublicAddress(CA2T(result.strPublicAddress));
		strMessage.Format(GetResString(bRuntime ? _T("IP_GUARD_RUNTIME_PUBLIC_IP_MISMATCH_FMT") : _T("IP_GUARD_RUNTIME_PUBLIC_IP_MISMATCH_FMT")), (LPCTSTR)strPublicAddress, (LPCTSTR)result.strProviderUrl, (LPCTSTR)strRanges);
	}
	else
		strMessage.Format(GetResString(bRuntime ? _T("IP_GUARD_RUNTIME_PROBE_FAILED_FMT") : _T("IP_GUARD_RUNTIME_PROBE_FAILED_FMT")), (LPCTSTR)strRanges);
	return strMessage;
}

bool CemuleDlg::StartIpGuardPublicIpProbe(const CString& strPurpose)
{
	const bool bRuntime = strPurpose.CompareNoCase(_T("runtime")) == 0;
	bool& rbPending = bRuntime ? m_bIpGuardRuntimeProbePending : m_bIpGuardStartupProbePending;
	if (rbPending || m_bVpnGuardProbePending)
		return true;

	std::vector<SIpGuardAllowedPublicIpRange> ranges;
	CString strError;
	if (!CIpGuard::TryParseAllowedPublicIpRanges(thePrefs.GetIpGuardAllowedPublicIpRanges(), ranges, strError)) {
		SIpGuardPublicIpProbeResult result;
		ApplyIpGuardNetworkBlock(FormatIpGuardPublicIpMessage(bRuntime, result));
		return false;
	}
	if (ranges.empty()) {
		if (!bRuntime)
			m_bIpGuardStartupApproved = true;
		return false;
	}

	if (!CIpGuard::StartBoundPublicIpProbe(m_hWnd, UM_IP_GUARD_PROBE_RESULT, ++m_uIpGuardProbeGeneration, strPurpose, GetIpGuardPublicIpProbeFamily(ranges), strError)) {
		SIpGuardPublicIpProbeResult result;
		ApplyIpGuardNetworkBlock(FormatIpGuardPublicIpMessage(bRuntime, result));
		return false;
	}

	rbPending = true;
	if (bRuntime)
		m_dwLastIpGuardRuntimeProbeTick = ::GetTickCount();
	return true;
}

void CemuleDlg::CheckIpGuardPublicIpMonitor(bool bForce)
{
	if (!thePrefs.IsIpGuardEnabled() || !m_bIpGuardMonitorActive || m_bIpGuardStartupBlocked || theApp.IsClosing())
		return;
	if (m_bIpGuardRuntimeProbePending)
		return;

	if (!bForce) {
		const DWORD dwNow = ::GetTickCount();
		if (m_dwLastIpGuardRuntimeProbeTick != 0 && dwNow - m_dwLastIpGuardRuntimeProbeTick < MIN2MS(5))
			return;
	}
	StartIpGuardPublicIpProbe(_T("runtime"));
}

void CemuleDlg::HandleIpGuardPublicIpProbeResult(const SIpGuardPublicIpProbeResult& result)
{
	const bool bRuntime = result.strPurpose.CompareNoCase(_T("runtime")) == 0;
	if (bRuntime)
		m_bIpGuardRuntimeProbePending = false;
	else
		m_bIpGuardStartupProbePending = false;

	if (!thePrefs.IsIpGuardEnabled() || theApp.IsClosing())
		return;

	std::vector<SIpGuardAllowedPublicIpRange> ranges;
	CString strError;
	if (!CIpGuard::TryParseAllowedPublicIpRanges(thePrefs.GetIpGuardAllowedPublicIpRanges(), ranges, strError)) {
		SIpGuardPublicIpProbeResult failedResult;
		ApplyIpGuardNetworkBlock(FormatIpGuardPublicIpMessage(bRuntime, failedResult));
		return;
	}
	if (ranges.empty()) {
		if (!bRuntime)
			m_bIpGuardStartupApproved = true;
		return;
	}
	if (result.bSucceeded && CIpGuard::IsPublicIpAllowed(result.publicAddress, ranges)) {
		m_bIpGuardStartupApproved = true;
		ClearIpGuardNetworkBlock(true);
		if (thePrefs.IsVpnGuardEnabled()) {
			UpdateVpnGuardMonitor(true);
			if (IsSessionNetworkBlocked())
				return;
		}
		if (thePrefs.IsIpGuardEnabled()) {
			RegisterIpGuardNotifications();
			if (!CanUseP2PConnectionCommands()) {
				ApplyIpGuardNetworkBlock(GetResString(_T("IP_GUARD_COMMANDS_BLOCKED_MONITOR")));
				return;
			}
			if (toolbar != NULL)
				toolbar->EnableButton(TBBTN_CONNECT, TRUE);
			m_SysMenuOptions.EnableMenuItem(MP_CONNECT, MF_ENABLED);
			if (serverwnd != NULL && serverwnd->GetSafeHwnd() != NULL)
				serverwnd->GetDlgItem(IDC_ED2KCONNECT)->EnableWindow(TRUE);
			if (kademliawnd != NULL)
				kademliawnd->UpdateControlsState();
			if (!bRuntime && theApp.IsRunning())
				AutoConnectIfNeeded();
		}
		TryRestoreIpGuardNetworkConnections();
		return;
	}

	const CString strMessage = FormatIpGuardPublicIpMessage(bRuntime, result);
	CString strOverlay;
	if (result.bSucceeded) {
		const CString strPublicAddress(CA2T(result.strPublicAddress));
		strOverlay.Format(GetResString(IP_GUARD_OVERLAY_PUBLIC_IP_FMT_KEY), (LPCTSTR)strPublicAddress);
	}
	ApplyIpGuardNetworkBlock(strMessage, strOverlay);
}

CString CemuleDlg::GetVpnGuardCountryName(const CString& strCountryCode) const
{
	return CIPGeolocation::GetLocalizedCountryName(strCountryCode, strCountryCode);
}

CString CemuleDlg::FormatVpnGuardPublicIpMessage(const SIpGuardPublicIpProbeResult& result, const CString& strCountryCode, const CString& strCountryName, bool bCountryUnknown) const
{
	CString strMessage;
	if (!result.bSucceeded)
		strMessage.Format(GetResString(_T("VPN_GUARD_PROBE_FAILED_FMT")), (LPCTSTR)strCountryName, (LPCTSTR)strCountryCode);
	else if (bCountryUnknown) {
		const CString strPublicAddress(CA2T(result.strPublicAddress));
		strMessage.Format(GetResString(_T("VPN_GUARD_COUNTRY_UNKNOWN_FMT")), (LPCTSTR)strPublicAddress, (LPCTSTR)result.strProviderUrl);
	}
	else {
		const CString strPublicAddress(CA2T(result.strPublicAddress));
		strMessage.Format(GetResString(_T("VPN_GUARD_COUNTRY_MATCH_FMT")), (LPCTSTR)strPublicAddress, (LPCTSTR)strCountryName, (LPCTSTR)strCountryCode);
	}
	return strMessage;
}

bool CemuleDlg::StartVpnGuardPublicIpProbe(const CString& strPurpose)
{
	if (m_bVpnGuardProbePending)
		return true;
	if (m_bIpGuardStartupProbePending || m_bIpGuardRuntimeProbePending)
		return true;

	CString strErrorIpv4;
	CString strErrorIpv6;
	const uint32_t uGeneration = ++m_uIpGuardProbeGeneration;
	m_dwLastVpnGuardRuntimeProbeTick = ::GetTickCount();
	m_iVpnGuardPendingProbeCount = 0;
	m_bVpnGuardProbeHadSuccess = false;
	m_vpnGuardLastFailureResult = SIpGuardPublicIpProbeResult();

	const CString strIpv4Purpose(strPurpose + _T("-ipv4"));
	if (CIpGuard::StartPublicIpProbe(m_hWnd, UM_IP_GUARD_PROBE_RESULT, uGeneration, strIpv4Purpose, AF_INET, strErrorIpv4))
		++m_iVpnGuardPendingProbeCount;
	else
		m_vpnGuardLastFailureResult.strError = strErrorIpv4;

	const CString strIpv6Purpose(strPurpose + _T("-ipv6"));
	if (CIpGuard::StartPublicIpProbe(m_hWnd, UM_IP_GUARD_PROBE_RESULT, uGeneration, strIpv6Purpose, AF_INET6, strErrorIpv6))
		++m_iVpnGuardPendingProbeCount;
	else if (m_vpnGuardLastFailureResult.strError.IsEmpty())
		m_vpnGuardLastFailureResult.strError = strErrorIpv6;

	if (m_iVpnGuardPendingProbeCount == 0) {
		const CString strCountryCode(thePrefs.GetVpnGuardCountryCode());
		ApplyVpnGuardNetworkBlock(FormatVpnGuardPublicIpMessage(m_vpnGuardLastFailureResult, strCountryCode, GetVpnGuardCountryName(strCountryCode), true));
		return false;
	}

	m_bVpnGuardProbePending = true;
	return true;
}

void CemuleDlg::CheckVpnGuardPublicIpMonitor(bool bForce)
{
	if (!thePrefs.IsVpnGuardEnabled() || !m_bVpnGuardMonitorActive || theApp.IsClosing())
		return;
	if (m_bVpnGuardProbePending || m_bIpGuardStartupProbePending || m_bIpGuardRuntimeProbePending)
		return;

	if (!bForce) {
		const DWORD dwNow = ::GetTickCount();
		if (m_dwLastVpnGuardRuntimeProbeTick != 0 && dwNow - m_dwLastVpnGuardRuntimeProbeTick < MIN2MS(5))
			return;
	}
	StartVpnGuardPublicIpProbe(_T("vpn-runtime"));
}

void CemuleDlg::ApproveVpnGuardPublicIpProbe()
{
	const bool bRestorePreviousConnections = m_bIpGuardRestoreEd2kConnection || m_bIpGuardRestoreKadConnection;
	m_bVpnGuardStartupApproved = true;
	ClearVpnGuardNetworkBlock(true);
	if (thePrefs.IsIpGuardEnabled())
		UpdateIpGuardMonitor(false);
	if (theApp.IsRunning() && !IsSessionNetworkBlocked() && CanUseP2PConnectionCommands()) {
		TryRestoreIpGuardNetworkConnections();
		if (!bRestorePreviousConnections)
			AutoConnectIfNeeded();
	}
}

void CemuleDlg::HandleVpnGuardPublicIpProbeResult(const SIpGuardPublicIpProbeResult& result)
{
	if (!m_bVpnGuardProbePending || m_iVpnGuardPendingProbeCount <= 0)
		return;
	if (!thePrefs.IsVpnGuardEnabled() || theApp.IsClosing()) {
		m_bVpnGuardProbePending = false;
		m_iVpnGuardPendingProbeCount = 0;
		m_bVpnGuardProbeHadSuccess = false;
		return;
	}

	const CString strSelectedCountryCode(thePrefs.GetVpnGuardCountryCode());
	const CString strSelectedCountryName(GetVpnGuardCountryName(strSelectedCountryCode));
	if (!result.bSucceeded) {
		if (m_vpnGuardLastFailureResult.strError.IsEmpty())
			m_vpnGuardLastFailureResult = result;
	}
	else {
		m_bVpnGuardProbeHadSuccess = true;
		m_strVpnGuardLastPublicAddress = result.strPublicAddress;
		GeolocationData_Struct geoData;
		if (theApp.ipgeolocation != NULL)
			geoData = theApp.ipgeolocation->QueryGeolocationData(result.publicAddress);
		else {
			geoData.Country = GetResString(_T("IPGEOLOCATION_NA"));
			geoData.CountryCode = GetResString(_T("IPGEOLOCATION_NA"));
			geoData.City = GetResString(_T("IPGEOLOCATION_NA"));
			geoData.FlagIndex = NO_FLAG;
		}
		CString strDetectedCountryCode(geoData.CountryCode);
		strDetectedCountryCode.Trim();
		strDetectedCountryCode.MakeUpper();
		const bool bCountryUnknown = strDetectedCountryCode.IsEmpty() || strDetectedCountryCode.CompareNoCase(GetResString(_T("IPGEOLOCATION_NA"))) == 0;
		if (!bCountryUnknown && strDetectedCountryCode.CompareNoCase(strSelectedCountryCode) == 0) {
			CString strDetectedCountryFallback(strSelectedCountryName);
			if (!geoData.Country.IsEmpty() && geoData.Country.CompareNoCase(GetResString(_T("IPGEOLOCATION_NA"))) != 0)
				strDetectedCountryFallback = geoData.Country;
			const CString strDetectedCountryName(CIPGeolocation::GetLocalizedCountryName(strDetectedCountryCode, strDetectedCountryFallback));
			const CString strDetectedCountryDisplay(CIPGeolocation::FormatLocalizedCountryNameAndCode(strDetectedCountryCode, strDetectedCountryName));
			CString strOverlay;
			strOverlay.Format(GetResString(_T("VPN_GUARD_OVERLAY_COUNTRY_FMT")), (LPCTSTR)strDetectedCountryDisplay);
			ApplyVpnGuardNetworkBlock(FormatVpnGuardPublicIpMessage(result, strDetectedCountryCode, strDetectedCountryName, false), strOverlay);
			return;
		}
		if (bCountryUnknown && thePrefs.IsVpnGuardBlockUnknownCountryEnabled()) {
			ApplyVpnGuardNetworkBlock(FormatVpnGuardPublicIpMessage(result, strSelectedCountryCode, strSelectedCountryName, true), GetResString(_T("VPN_GUARD_OVERLAY_UNKNOWN")));
			return;
		}
	}

	--m_iVpnGuardPendingProbeCount;
	if (m_iVpnGuardPendingProbeCount > 0)
		return;

	m_bVpnGuardProbePending = false;
	if (!m_bVpnGuardProbeHadSuccess && thePrefs.IsVpnGuardBlockUnknownCountryEnabled()) {
		ApplyVpnGuardNetworkBlock(FormatVpnGuardPublicIpMessage(m_vpnGuardLastFailureResult, strSelectedCountryCode, strSelectedCountryName, true));
		return;
	}

	m_bVpnGuardProbeHadSuccess = false;
	ApproveVpnGuardPublicIpProbe();
}

void CemuleDlg::UpdateVpnGuardMonitor(bool bForcePublicIpProbe)
{
	if (!thePrefs.IsVpnGuardEnabled()) {
		ClearVpnGuardNetworkBlock(true);
		TryRestoreIpGuardNetworkConnections();
		m_bVpnGuardStartupApproved = false;
		m_bVpnGuardProbePending = false;
		m_iVpnGuardPendingProbeCount = 0;
		m_bVpnGuardProbeHadSuccess = false;
		m_dwLastVpnGuardRuntimeProbeTick = 0;
		m_strVpnGuardLastPublicAddress.Empty();
		m_bVpnGuardMonitorActive = false;
		if (thePrefs.IsIpGuardEnabled())
			UpdateIpGuardMonitor(false);
		else
			UnregisterIpGuardNotifications();
		return;
	}

	m_bVpnGuardMonitorActive = m_hWnd != NULL && ::IsWindow(m_hWnd) && SetTimer(TIMER_IP_GUARD_MONITOR, SEC2MS(10), NULL) != 0;
	if (m_bVpnGuardMonitorActive)
		RegisterIpGuardNotifications();
	if (!m_bVpnGuardMonitorActive) {
		ApplyVpnGuardNetworkBlock(GetResString(_T("VPN_GUARD_COMMANDS_BLOCKED_MONITOR")));
		return;
	}

	if (bForcePublicIpProbe || !m_bVpnGuardStartupApproved) {
		ApplyVpnGuardNetworkBlock(GetResString(_T("VPN_GUARD_PUBLIC_IP_VERIFYING")), GetResString(IP_GUARD_OVERLAY_VERIFYING_KEY));
		StartVpnGuardPublicIpProbe(_T("vpn-startup"));
		return;
	}

	ClearVpnGuardNetworkBlock(true);
	TryRestoreIpGuardNetworkConnections();
	CheckVpnGuardPublicIpMonitor(bForcePublicIpProbe);
}

LRESULT CemuleDlg::OnBindAddressChanged(WPARAM wParam, LPARAM)
{
	::InterlockedExchange(&g_lNetBindAddressChangePostPending, 0);
	if (static_cast<LONG>(wParam) != ::InterlockedCompareExchange(&g_lNetBindNotificationGeneration, 0, 0))
		return 0;
	CheckIpGuardRuntimeBind();
	if (!m_bIpGuardStartupBlocked)
		CheckIpGuardPublicIpMonitor(false);
	CheckVpnGuardPublicIpMonitor(true);
	return 0;
}

LRESULT CemuleDlg::OnIpGuardProbeResult(WPARAM wParam, LPARAM lParam)
{
	std::unique_ptr<SIpGuardPublicIpProbeResult> pResult(reinterpret_cast<SIpGuardPublicIpProbeResult*>(lParam));
	if (pResult == NULL)
		return 0;
	if (static_cast<uint32_t>(wParam) != pResult->uGeneration || pResult->uGeneration != m_uIpGuardProbeGeneration)
		return 0;
	if (pResult->strPurpose.Left(4).CompareNoCase(_T("vpn-")) == 0)
		HandleVpnGuardPublicIpProbeResult(*pResult);
	else
		HandleIpGuardPublicIpProbeResult(*pResult);
	return 0;
}

bool CemuleDlg::CanUseP2PConnectionCommands() const
{
	if (m_bIpGuardStartupBlocked)
		return false;
	if (thePrefs.IsIpGuardEnabled() && !m_bIpGuardMonitorActive)
		return false;
	if (thePrefs.IsVpnGuardEnabled() && !m_bVpnGuardMonitorActive)
		return false;
	return true;
}

void CemuleDlg::LogP2PConnectionCommandBlocked(bool bUserVisible)
{
	CString strMessage;
	if (m_bIpGuardStartupBlocked)
		strMessage = m_strIpGuardStartupBlockReason;
	if (strMessage.IsEmpty())
		strMessage = GetResString(thePrefs.IsVpnGuardEnabled() ? _T("VPN_GUARD_COMMANDS_BLOCKED_MONITOR") : _T("IP_GUARD_COMMANDS_BLOCKED_MONITOR"));
	LogWarning(LOG_STATUSBAR, _T("%s"), (LPCTSTR)strMessage);
	if (bUserVisible)
		CDarkMode::MessageBox(strMessage, MB_OK | MB_ICONWARNING);
}

void CemuleDlg::StartConnection()
{
	StartConnection(false);
}

void CemuleDlg::StartConnection(bool bUserInitiated)
{
	if (!CanUseP2PConnectionCommands()) {
		LogP2PConnectionCommandBlocked(bUserInitiated);
		return;
	}

	if ((!theApp.serverconnect->IsConnecting() && !theApp.serverconnect->IsConnected()) || !Kademlia::CKademlia::IsRunning()) {
		// UPnP is still trying to open the ports. In order to not get a LowID by connecting to the servers / kad before
		// the ports are opened we delay the connection until UPnP gets a result or the timeout is reached
		// If the user clicks two times on the button, let him have his will and connect regardless
		m_bConnectRequestDelayedForUPnP = m_hUPnPTimeOutTimer != 0 && !m_bConnectRequestDelayedForUPnP;
		if (m_bConnectRequestDelayedForUPnP) {
			AddLogLine(false, GetResString(_T("DELAYEDBYUPNP")));
			AddLogLine(true, GetResString(_T("DELAYEDBYUPNP2")));
			return;
		}
		if (m_hUPnPTimeOutTimer != 0) {
			VERIFY(::KillTimer(NULL, m_hUPnPTimeOutTimer));
			m_hUPnPTimeOutTimer = 0;
		}

		const bool bWantEd2k = (thePrefs.GetNetworkED2K() || m_bEd2kSuspendDisconnect) && !theApp.serverconnect->IsConnecting()
			&& !theApp.serverconnect->IsConnected();
		const bool bWantKad = (thePrefs.GetNetworkKademlia() || m_bKadSuspendDisconnect) && !Kademlia::CKademlia::IsRunning();

		bool bWarnKad = false;
		bool bWarnEd2k = false;
		if (bUserInitiated) {
			bWarnKad = bWantKad && !HasNodesDatContacts();
			const bool bServerListEmpty = (theApp.serverlist == NULL) || (theApp.serverlist->GetServerCount() == 0);
			bWarnEd2k = bWantEd2k && bServerListEmpty;
			if (bWarnKad || bWarnEd2k) {
				CString strMsg;
				if (bWarnKad && bWarnEd2k) {
					const CString strKad = GetResString(_T("EMULE_AI_NODESDAT_REQUIRED_CONNECT"));
					const CString strEd2k = GetResString(_T("EMULE_AI_SERVERMET_REQUIRED_CONNECT"));
					strMsg.Format(_T("%s\n\n\n\n%s"), (LPCTSTR)strKad, (LPCTSTR)strEd2k);
				} else if (bWarnKad) {
					strMsg = GetResString(_T("EMULE_AI_NODESDAT_REQUIRED_CONNECT"));
				} else {
					strMsg = GetResString(_T("EMULE_AI_SERVERMET_REQUIRED_CONNECT"));
				}
				CDarkMode::MessageBox(strMsg, MB_OK | MB_ICONINFORMATION);
			}
		}

		const bool bStartEd2k = bWantEd2k && !bWarnEd2k;
		bool bStartKad = bWantKad;
		if (bStartKad && thePrefs.GetUDPPort() != 0 && theApp.clientudp->GetConnectedPort() == 0) {
			CString strError;
			strError.Format(GetResString(_T("MAIN_SOCKETERROR")), thePrefs.GetUDPPort());
			LogError(LOG_STATUSBAR, _T("%s"), (LPCTSTR)strError);
			bStartKad = false;
		}
		if (bStartEd2k || bStartKad)
			AddLogLine(true, GetResString(_T("CONNECTING")));

		// ed2k
		if (bStartEd2k)
			theApp.serverconnect->ConnectToAnyServer();

		// kad
		if (bStartKad)
			Kademlia::CKademlia::Start();

		ShowConnectionState();
	}
	m_bEd2kSuspendDisconnect = false;
	m_bKadSuspendDisconnect = false;
}

void CemuleDlg::AutoConnectIfNeeded()
{
	if (thePrefs.DoAutoConnect() && !m_bConnectRequestDelayedForUPnP && !theApp.IsConnected() && !theApp.serverconnect->IsConnecting() && !Kademlia::CKademlia::IsRunning())
		StartConnection(false);
}

void CemuleDlg::CloseConnection()
{
	theApp.serverconnect->StopConnectionTry();
	theApp.serverconnect->Disconnect();

	Kademlia::CKademlia::Stop();
	theApp.OnlineSig(); // Added By Bouc7
	ShowConnectionState();
}

void CemuleDlg::StopConnectionForNetworkBindChange(bool& rbRestoreEd2k, bool& rbRestoreKad)
{
	rbRestoreEd2k = theApp.serverconnect->IsConnected() || theApp.serverconnect->IsConnecting();
	rbRestoreKad = Kademlia::CKademlia::IsRunning();

	if (rbRestoreEd2k) {
		theApp.serverconnect->StopConnectionTry();
		theApp.serverconnect->Disconnect();
	}

	if (rbRestoreKad)
		Kademlia::CKademlia::Stop();

	theApp.OnlineSig();
	ShowConnectionState();
}

void CemuleDlg::RestoreConnectionAfterNetworkBindChange(bool bRestoreEd2k, bool bRestoreKad)
{
	m_bEd2kSuspendDisconnect = bRestoreEd2k;
	m_bKadSuspendDisconnect = bRestoreKad;
	if (m_bEd2kSuspendDisconnect || m_bKadSuspendDisconnect)
		StartConnection(false);
}

void CemuleDlg::RestoreWindow()
{
	if (IsPreferencesDlgOpen()) {
		MessageBeep(MB_OK);
		preferenceswnd->SetForegroundWindow();
		preferenceswnd->BringWindowToTop();
		return;
	}

	const bool bSharedFilesPreparedForRestore = sharedfileswnd != NULL && activewnd == sharedfileswnd && ::IsWindow(sharedfileswnd->sharedfilesctrl.GetSafeHwnd());
	if (bSharedFilesPreparedForRestore)
		sharedfileswnd->sharedfilesctrl.ReloadListForActivation(static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL));

	TrayHide();
	DestroyMiniMule();

	if (m_wpFirstRestore.length) {
		SetWindowPlacement(&m_wpFirstRestore);
		memset(&m_wpFirstRestore, 0, sizeof m_wpFirstRestore);
		SetForegroundWindow();
		BringWindowToTop();
	} else
		CTrayDialog::RestoreWindow();

	// Ensure the currently visible virtual list gets refreshed after the main window is shown again from the system tray.
	if (IsWindowVisible()) {
		if (!bSharedFilesPreparedForRestore && sharedfileswnd && sharedfileswnd == activewnd && sharedfileswnd->sharedfilesctrl && sharedfileswnd->sharedfilesctrl.IsWindowVisible())
			sharedfileswnd->sharedfilesctrl.ReloadList(false, static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL));
		else if (searchwnd && searchwnd == activewnd && searchwnd->m_pwndResults && searchwnd->m_pwndResults->searchlistctrl && searchwnd->m_pwndResults->searchlistctrl.IsWindowVisible())
			searchwnd->m_pwndResults->searchlistctrl.ReloadList(false, static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL));
		else if (transferwnd && transferwnd == activewnd && transferwnd->GetDownloadList() && transferwnd->GetDownloadList()->IsWindowVisible())
			transferwnd->GetDownloadList()->ReloadList(false, static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL));
		else if (transferwnd && transferwnd == activewnd && transferwnd->GetClientList() && transferwnd->GetClientList()->IsWindowVisible())
			transferwnd->GetClientList()->ReloadList(false, static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL));
	}

	UpdateTitleVersionOverlayWindow();
	if (m_bNewVersionAvailable)
		StartTitleVersionAnimation();
}

void CemuleDlg::UpdateTrayIcon(int iPercent)
{
	// compute an id of the icon to be generated
	UINT uSysTrayIconCookie = (iPercent > 0) ? (16 - ((iPercent * 15 / 100) + 1)) : 0;
	if (theApp.IsConnected()) {
		if (!theApp.IsFirewalled())
			uSysTrayIconCookie += 50;
	} else
		uSysTrayIconCookie += 100;

	// don't update if the same icon as displayed would be generated
	if (m_uLastSysTrayIconCookie == uSysTrayIconCookie)
		return;
	m_uLastSysTrayIconCookie = uSysTrayIconCookie;

	// prepare it up
	if (m_iMsgIcon != 0 && thePrefs.DoFlashOnNewMessage()) {
		m_bMsgBlinkState = !m_bMsgBlinkState;

		if (m_bMsgBlinkState)
			m_TrayIcon.Init(imicons[1], 100, 1, 1, 16, 16, thePrefs.GetStatsColor(11));
	} else
		m_bMsgBlinkState = false;

	if (!m_bMsgBlinkState) {
		HICON trayicon;
		if (theApp.IsConnected())
			trayicon = theApp.IsFirewalled() ? m_icoSysTrayLowID : m_icoSysTrayConnected;
		else
			trayicon = m_icoSysTrayDisconnected;
		m_TrayIcon.Init(trayicon, 100, 1, 1, 16, 16, thePrefs.GetStatsColor(11));
	}

	// load our limit and color info
	static const int aiLimits[1] = { 100 }; // set the limits of where the bar color changes (low-high)
	COLORREF aColors[1] = { thePrefs.GetStatsColor(11) }; // set the corresponding color for each level
	m_TrayIcon.SetColorLevels(aiLimits, aColors, _countof(aiLimits));

	// generate the icon (do *not* destroy that icon using DestroyIcon(), that's done in 'TrayUpdate')
	int aiVals[1] = { iPercent };
	m_icoSysTrayCurrent = m_TrayIcon.Create(aiVals);
	ASSERT(m_icoSysTrayCurrent != NULL);
	if (m_icoSysTrayCurrent)
		TraySetIcon(m_icoSysTrayCurrent, true);
	TrayUpdate();
}

int CemuleDlg::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	return CTrayDialog::OnCreate(lpCreateStruct);
}

void CemuleDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	if (!theApp.IsClosing()) {
		ShowTransferRate(true);

		if (bShow && activewnd == chatwnd)
			chatwnd->chatselector.ShowChat();
	}
	CTrayDialog::OnShowWindow(bShow, nStatus);
	if (!bShow) {
		StopTitleVersionAnimation();
		HideTitleVersionOverlayWindow();
		return;
	}

	if (bShow && ShouldSuppressMainWindowForStartupLoading())
		return;

	UpdateTitleVersionOverlayWindow();
	if (m_bNewVersionAvailable)
		StartTitleVersionAnimation();
}

void CemuleDlg::ShowNotifier(LPCTSTR pszText, TbnMsg nMsgType, LPCTSTR pszLink, bool bForceSoundOFF)
{
	if (!m_bNotifierRuntimeActive)
		return;

	LPCTSTR pszSoundEvent = NULL;
	int iSoundPrio = 0;
	bool bShowIt = false;
	switch (nMsgType) {
	case TBN_CHAT:
		if (thePrefs.GetNotifierOnChat()) {
			ShowNotificationPopup(pszText, nMsgType, pszLink);
			bShowIt = true;
			pszSoundEvent = _T("eMule_Chat");
			iSoundPrio = 1;
		}
		break;
	case TBN_DOWNLOADFINISHED:
		if (thePrefs.GetNotifierOnDownloadFinished()) {
			ShowNotificationPopup(pszText, nMsgType, pszLink);
			bShowIt = true;
			pszSoundEvent = _T("eMule_DownloadFinished");
			iSoundPrio = 1;
			SendNotificationMail(nMsgType, pszText);
		}
		break;
	case TBN_DOWNLOADADDED:
		if (thePrefs.GetNotifierOnNewDownload()) {
			ShowNotificationPopup(pszText, nMsgType, pszLink);
			bShowIt = true;
			pszSoundEvent = _T("eMule_DownloadAdded");
			iSoundPrio = 1;
		}
		break;
	case TBN_LOG:
		if (thePrefs.GetNotifierOnLog()) {
			ShowNotificationPopup(pszText, nMsgType, pszLink);
			bShowIt = true;
			pszSoundEvent = _T("eMule_LogEntryAdded");
		}
		break;
	case TBN_IMPORTANTEVENT:
		if (thePrefs.GetNotifierOnImportantError()) {
			ShowNotificationPopup(pszText, nMsgType, pszLink);
			bShowIt = true;
			pszSoundEvent = _T("eMule_Urgent");
			iSoundPrio = 1;
			SendNotificationMail(nMsgType, pszText);
		}
		break;
	case TBN_NEWVERSION:
		ShowNotificationPopup(pszText, nMsgType, pszLink);
		bShowIt = true;
		pszSoundEvent = _T("eMule_NewVersion");
		iSoundPrio = 1;
		break;
	case TBN_NULL:
		ShowNotificationPopup(pszText, nMsgType, pszLink);
		bShowIt = true;
	}

	if (bShowIt && !bForceSoundOFF && thePrefs.GetNotifierSoundType() != ntfstNoSound) {
		bool bNotifiedWithAudio = false;
		if (thePrefs.GetNotifierSoundType() == ntfstSpeech)
			bNotifiedWithAudio = Speak(pszText);

		if (!bNotifiedWithAudio) {
			if (!thePrefs.GetNotifierSoundFile().IsEmpty())
				PlaySound(thePrefs.GetNotifierSoundFile(), NULL, SND_FILENAME | SND_NOSTOP | SND_NOWAIT | SND_ASYNC);
			else if (pszSoundEvent) {
				// use 'SND_NOSTOP' only for low priority events, otherwise the 'Log message' event may overrule
				// a more important event which is fired nearly at the same time.
				PlaySound(pszSoundEvent, NULL, SND_APPLICATION | SND_ASYNC | SND_NODEFAULT | SND_NOWAIT | ((iSoundPrio > 0) ? 0 : SND_NOSTOP));
			}
		}
	}
}

void CemuleDlg::ShowNotificationPopup(LPCTSTR pszText, TbnMsg nMsgType, LPCTSTR pszLink)
{
	switch (thePrefs.GetNotifierDisplayMode()) {
	case ntfdmToastNotification:
		if (m_wndToastNotifier.Show(m_hWnd, pszText, nMsgType, pszLink))
			return;
		if (ShowTrayBalloonNotification(pszText, nMsgType, pszLink))
			return;
		break;
	case ntfdmTrayBalloon:
		if (ShowTrayBalloonNotification(pszText, nMsgType, pszLink))
			return;
		break;
	case ntfdmCustomPopup:
	default:
		break;
	}

	if (notifierenabled)
		m_wndTaskbarNotifier.Show(pszText, nMsgType, pszLink);
}

bool CemuleDlg::ShowTrayBalloonNotification(LPCTSTR pszText, TbnMsg nMsgType, LPCTSTR pszLink)
{
	if (!TrayIconVisible())
		TrayShow();
	if (!TrayIconVisible()) {
		ClearTrayBalloonNotificationPayload();
		return false;
	}

	CString strTitle;
	CString strBody;
	SplitNotifierText(pszText, nMsgType, strTitle, strBody);
	if (!TrayShowBalloon(strTitle, strBody, GetTrayBalloonInfoFlags(nMsgType))) {
		ClearTrayBalloonNotificationPayload();
		return false;
	}

	m_nTrayBalloonMsgType = nMsgType;
	m_strTrayBalloonLink = pszLink != NULL ? pszLink : _T("");
	return true;
}

void CemuleDlg::ClearTrayBalloonNotificationPayload()
{
	m_nTrayBalloonMsgType = TBN_NONOTIFY;
	m_strTrayBalloonLink.Empty();
}

void CemuleDlg::LoadNotifier(const CString& configuration)
{
	notifierenabled = m_wndTaskbarNotifier.LoadConfiguration(configuration);
}

LRESULT CemuleDlg::OnTaskbarNotifierClicked(WPARAM, LPARAM lParam)
{
	HandleNotifierClicked(static_cast<TbnMsg>(m_wndTaskbarNotifier.GetMessageType()), lParam);
	return 0;
}

LRESULT CemuleDlg::OnToastNotificationClicked(WPARAM wParam, LPARAM lParam)
{
	HandleNotifierClicked(static_cast<TbnMsg>(wParam), lParam);
	return 0;
}

void CemuleDlg::OnTrayBalloonUserClick()
{
	const TbnMsg nMsgType = m_nTrayBalloonMsgType;
	const CString strLink(m_strTrayBalloonLink);
	ClearTrayBalloonNotificationPayload();
	HandleNotifierClicked(nMsgType, strLink);
}

void CemuleDlg::HandleNotifierClicked(TbnMsg nMsgType, const CString& strLink)
{
	LPTSTR pszLink = NULL;
	if (!strLink.IsEmpty())
		pszLink = _tcsdup(strLink);
	HandleNotifierClicked(nMsgType, reinterpret_cast<LPARAM>(pszLink));
}

void CemuleDlg::HandleNotifierClicked(TbnMsg nMsgType, LPARAM lParam)
{
	const bool bHadLink = lParam != 0;
	if (lParam) {
		ShellDefaultVerb((LPTSTR)lParam);
		free((void*)lParam);
	}

	switch (nMsgType) {
	case TBN_CHAT:
		RestoreWindow();
		SetActiveDialog(chatwnd);
		break;
	case TBN_DOWNLOADFINISHED:
		// if we had a link and opened the downloaded file, don't restore the app window
		if (!bHadLink) {
			RestoreWindow();
			SetActiveDialog(transferwnd);
		}
		break;
	case TBN_DOWNLOADADDED:
		RestoreWindow();
		SetActiveDialog(transferwnd);
		break;
	case TBN_IMPORTANTEVENT:
	case TBN_LOG:
		RestoreWindow();
		SetActiveDialog(serverwnd);
		break;
	case TBN_NEWVERSION:
		OpenVersionReleasesURL();
		break;
	default:
		break;
	}
}

void CemuleDlg::OnSettingChange(UINT uFlags, LPCTSTR lpszSection)
{
	// Safe trace: pass both arguments and guard a possible NULL section pointer.
	LPCTSTR pszSection = (lpszSection != NULL) ? lpszSection : _T("(null)");
	TRACE(_T("CemuleDlg::OnSettingChange: uFlags=0x%08X  lpszSection=\"%s\"\n"), uFlags, pszSection);

	// Do not update the Shell's large icon size, because we still have an image list
	// from the shell which contains the old large icon size.
	theApp.UpdateDesktopColorDepth();

	// Check if the change is related to dark mode
	if (lpszSection && _tcscmp(lpszSection, _T("ImmersiveColorSet")) == 0) {
		GetSystemDarkModeStatus();
		PostMessage(UM_APP_SWITCH_DARKMODE, 0, 0); // Switch color scheme for the entire application
	}

	ApplyMainWindowIcons();

	if (m_fontTitleVersionLink.GetSafeHandle() != NULL)
		m_fontTitleVersionLink.DeleteObject();
	UpdateTitleVersionOverlayWindow();

	CTrayDialog::OnSettingChange(uFlags, lpszSection);
}

void CemuleDlg::OnSysColorChange()
{
	theApp.UpdateDesktopColorDepth();
	CTrayDialog::OnSysColorChange();
	SetAllIcons();
	if (m_fontTitleVersionLink.GetSafeHandle() != NULL)
		m_fontTitleVersionLink.DeleteObject();
	UpdateTitleVersionOverlayWindow();
}

HBRUSH CemuleDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = GetCtlColor(pDC, pWnd, nCtlColor);
	return hbr ? hbr : __super::OnCtlColor(pDC, pWnd, nCtlColor);
}

HBRUSH CemuleDlg::GetCtlColor(CDC* /*pDC*/, CWnd* /*pWnd*/, UINT /*nCtlColor*/)
{
	// This function could be used to give the entire eMule (at least all of the main windows)
	// a somewhat more Vista like look by giving them all a bright background color.
	// However, again, the owner drawn tab controls are noticeably disturbing that attempt. They
	// do not change their background color accordingly. They don't use NMCUSTOMDRAW nor to they
	// use WM_CTLCOLOR...
	//
	return NULL;
}

void CemuleDlg::SetAllIcons()
{
	if (theApp.IsClosing())
		return;

	// application icons
	if (m_hIcon)
		VERIFY(::DestroyIcon(m_hIcon));
	if (m_hIconSmall)
		VERIFY(::DestroyIcon(m_hIconSmall));

	// NOTE: the application icon name is prefixed with "AAA" to make sure it's alphabetically sorted by the
	// resource compiler as the 1st icon in the resource table!
	m_hIcon = theApp.LoadIcon(_T("AAAEMULEAPP"), 32, 32);
	m_hIconSmall = theApp.LoadIcon(_T("AAAEMULEAPP"), 16, 16);
	ApplyMainWindowIcons();

	// connection state
	DestroyIconsArr(m_connicons, _countof(m_connicons));
	m_connicons[0] = theApp.LoadIcon(_T("ConnectedNotNot"), 16, 16);
	m_connicons[1] = theApp.LoadIcon(_T("ConnectedNotLow"), 16, 16);
	m_connicons[2] = theApp.LoadIcon(_T("ConnectedNotHigh"), 16, 16);
	m_connicons[3] = theApp.LoadIcon(_T("ConnectedLowNot"), 16, 16);
	m_connicons[4] = theApp.LoadIcon(_T("ConnectedLowLow"), 16, 16);
	m_connicons[5] = theApp.LoadIcon(_T("ConnectedLowHigh"), 16, 16);
	m_connicons[6] = theApp.LoadIcon(_T("ConnectedHighNot"), 16, 16);
	m_connicons[7] = theApp.LoadIcon(_T("ConnectedHighLow"), 16, 16);
	m_connicons[8] = theApp.LoadIcon(_T("ConnectedHighHigh"), 16, 16);
	
	DestroyIconsArr(m_contactIcons, _countof(m_contactIcons));
	m_contactIcons[0] = theApp.LoadIcon(_T("CONTACT0"), 16, 16);
	m_contactIcons[1] = theApp.LoadIcon(_T("CONTACT1"), 16, 16);
	m_contactIcons[2] = theApp.LoadIcon(_T("CONTACT2"), 16, 16);
	m_contactIcons[3] = theApp.LoadIcon(_T("CONTACT3"), 16, 16);
	m_contactIcons[4] = theApp.LoadIcon(_T("CONTACT4"), 16, 16);

	ShowConnectionStateIcon();

	// transfer state
	DestroyIconsArr(transicons, _countof(transicons));
	transicons[0] = theApp.LoadIcon(_T("UP0DOWN0"), 16, 16);
	transicons[1] = theApp.LoadIcon(_T("UP0DOWN1"), 16, 16);
	transicons[2] = theApp.LoadIcon(_T("UP1DOWN0"), 16, 16);
	transicons[3] = theApp.LoadIcon(_T("UP1DOWN1"), 16, 16);
	ShowTransferStateIcon();

	// users state
	if (usericon)
		VERIFY(::DestroyIcon(usericon));
	usericon = theApp.LoadIcon(_T("StatsClients"), 16, 16);
	ShowUserStateIcon();

	// system tray icons
	if (m_icoSysTrayConnected)
		VERIFY(::DestroyIcon(m_icoSysTrayConnected));
	if (m_icoSysTrayDisconnected)
		VERIFY(::DestroyIcon(m_icoSysTrayDisconnected));
	if (m_icoSysTrayLowID)
		VERIFY(::DestroyIcon(m_icoSysTrayLowID));
	m_icoSysTrayConnected = theApp.LoadIcon(_T("EMULEAIHEAD"), 16, 16);
	m_icoSysTrayDisconnected = theApp.LoadIcon(_T("EMULEAITRAYDISCONNECTED"), 16, 16);
	m_icoSysTrayLowID = theApp.LoadIcon(_T("EMULEAITRAYLOWID"), 16, 16);
	ShowTransferRate(true);

	DestroyIconsArr(imicons, _countof(imicons));

	imicons[0] = NULL;
	imicons[1] = theApp.LoadIcon(_T("Message"), 16, 16);
	imicons[2] = theApp.LoadIcon(_T("MessagePending"), 16, 16);
	ShowMessageState(m_iMsgIcon);
}

void CemuleDlg::ApplyMainWindowIcons()
{
	if (theApp.IsClosing() || !::IsWindow(m_hWnd))
		return;

	if (m_hIcon != NULL)
		SetIcon(m_hIcon, TRUE);
	if (m_hIconSmall != NULL)
		SetIcon(m_hIconSmall, FALSE);
}

void CemuleDlg::Localize()
{
	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu) {
		VERIFY(pSysMenu->ModifyMenu(MP_ABOUTBOX, MF_BYCOMMAND | MF_STRING, MP_ABOUTBOX, GetResString(_T("ABOUTBOX"))));

			// localize the 'speed control' sub menus by deleting the current menus and creating a new ones.

			// remove any already available 'speed control' menus from system menu
			UINT uOptMenuPos = pSysMenu->GetMenuItemCount() - 1;
			CMenu* pAccelMenu = pSysMenu->GetSubMenu(uOptMenuPos);
			if (pAccelMenu) {
				ASSERT(pAccelMenu->m_hMenu == m_SysMenuOptions.m_hMenu);
				VERIFY(pSysMenu->RemoveMenu(uOptMenuPos, MF_BYPOSITION));
			}

			// destroy all 'speed control' menus
			if (m_menuUploadCtrl)
				VERIFY(m_menuUploadCtrl.DestroyMenu());
			if (m_menuDownloadCtrl)
				VERIFY(m_menuDownloadCtrl.DestroyMenu());
			if (m_SysMenuOptions)
				VERIFY(m_SysMenuOptions.DestroyMenu());

			// create new 'speed control' menus
			if (m_SysMenuOptions.CreateMenu()) {
				AddSpeedSelectorMenus(&m_SysMenuOptions);
				pSysMenu->AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_SysMenuOptions.m_hMenu, GetResStringWithAccel(_T("OPTIONS"), _T('O')));
			}
		}

	ShowUserStateIcon();
	toolbar->Localize();
	ShowConnectionState();
	ShowTransferRate(true);
	ShowUserCount();
	CPartFileConvert::Localize();
	if (m_pMiniMule && !m_pMiniMule->IsInInitDialog())
		m_pMiniMule->Localize();
}

void CemuleDlg::ShowUserStateIcon()
{
	statusbar->SetIcon(SBarUsers, usericon);
	SetStatusBarPartsSize();
}

void CemuleDlg::QuickSpeedOther(UINT nID)
{
	if (nID == MP_QS_PA) {
		thePrefs.SetMaxUpload(1);
		thePrefs.SetMaxDownload(1);
	} else if (nID == MP_QS_UA) {
		thePrefs.SetMaxUpload(thePrefs.GetMaxGraphUploadRate(true));
		thePrefs.SetMaxDownload(thePrefs.GetMaxGraphDownloadRate());
	}
}


void CemuleDlg::QuickSpeedUpload(UINT nID)
{
	switch (nID) {
	case MP_QS_U10:
		nID = 1;
		break;
	case MP_QS_U20:
		nID = 2;
		break;
	case MP_QS_U30:
		nID = 3;
		break;
	case MP_QS_U40:
		nID = 4;
		break;
	case MP_QS_U50:
		nID = 5;
		break;
	case MP_QS_U60:
		nID = 6;
		break;
	case MP_QS_U70:
		nID = 7;
		break;
	case MP_QS_U80:
		nID = 8;
		break;
	case MP_QS_U90:
		nID = 9;
		break;
	case MP_QS_U100:
		nID = 10;
		return;
	case MP_QS_UPC:
	default:
		return;
	case MP_QS_UP10:
		thePrefs.SetMaxUpload(GetRecMaxUpload());
		return;
	}
	thePrefs.SetMaxUpload((uint32)(thePrefs.GetMaxGraphUploadRate(true) * 0.1 * nID));
}

void CemuleDlg::QuickSpeedDownload(UINT nID)
{
	switch (nID) {
	case MP_QS_D10:
		nID = 1;
		break;
	case MP_QS_D20:
		nID = 2;
		break;
	case MP_QS_D30:
		nID = 3;
		break;
	case MP_QS_D40:
		nID = 4;
		break;
	case MP_QS_D50:
		nID = 5;
		break;
	case MP_QS_D60:
		nID = 6;
		break;
	case MP_QS_D70:
		nID = 7;
		break;
	case MP_QS_D80:
		nID = 8;
		break;
	case MP_QS_D90:
		nID = 9;
		break;
	case MP_QS_D100:
		nID = 10;
		return;
	case MP_QS_DC:
	default:
		return;
	}
	thePrefs.SetMaxDownload((UINT)(thePrefs.GetMaxGraphDownloadRate() * 0.1 * nID));
}

// quick-speed changer -- based on xrmb
int CemuleDlg::GetRecMaxUpload()
{
	int rate = thePrefs.GetMaxGraphUploadRate(true);
	if (rate < 7)
		return 0;
	if (rate < 15)
		return rate - 3;
	return rate - 4;
}

BOOL CemuleDlg::OnCommand(WPARAM wParam, LPARAM lParam)
{
	if (IsStartupLoadingDialogVisible() && m_pStartupLoadingDlg != NULL) {
		const UINT uCommand = LOWORD(wParam);
		if (uCommand == MP_HM_EXIT || uCommand == SC_CLOSE)
			m_pStartupLoadingDlg->RequestCancelAndExit();
		return TRUE;
	}

	switch (wParam) {
	case TBBTN_CONNECT:
	case MP_HM_CON:
		OnBnClickedConnect();
		break;
	case TBBTN_KAD:
	case MP_HM_KAD:
		SetActiveDialog(kademliawnd);
		break;
	case TBBTN_SERVER:
	case MP_HM_SRVR:
		SetActiveDialog(serverwnd);
		break;
	case TBBTN_TRANSFERS:
	case MP_HM_TRANSFER:
		SetActiveDialog(transferwnd);
		switch (transferwnd->m_pwndTransfer->m_dwShowListIDC) {
		case IDC_CLIENTLIST:
			if (!thePrefs.IsKnownClientListDisabled()) {
				theApp.emuledlg->transferwnd->GetClientList()->ReloadList(false, LSF_SELECTION);
				theApp.emuledlg->transferwnd->GetClientList()->FlushDeferredReload(LSF_SELECTION);
			}
			break;
		case IDC_DOWNLOADLIST:
		case IDC_UPLOADLIST + IDC_DOWNLOADLIST:
			theApp.emuledlg->transferwnd->GetDownloadList()->ReloadList(false, LSF_SELECTION);
			break;
		}
		break;
	case TBBTN_SEARCH:
	case MP_HM_SEARCH:
		SetActiveDialog(searchwnd);
		theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.ReloadList(false, LSF_SELECTION);
		break;
	case TBBTN_SHARED:
	case MP_HM_FILES:
		if (activewnd == sharedfileswnd)
			sharedfileswnd->sharedfilesctrl.ReloadList(false, LSF_SELECTION);
		else
			SetActiveDialog(sharedfileswnd);
		RefreshActiveBulkOperationOverlays();
		break;
	case TBBTN_MESSAGES:
	case MP_HM_MSGS:
		SetActiveDialog(chatwnd);
		break;
	case TBBTN_IRC:
	case MP_HM_IRC:
		SetActiveDialog(ircwnd);
		break;
	case TBBTN_STATS:
	case MP_HM_STATS:
		SetActiveDialog(statisticswnd);
		break;
	case TBBTN_OPTIONS:
	case MP_HM_PREFS:
		toolbar->CheckButton(TBBTN_OPTIONS, TRUE);
		ShowPreferences();
		toolbar->CheckButton(TBBTN_OPTIONS, FALSE);
		break;
	case TBBTN_SAVESTATE:
	case MP_HM_SAVESTATE:
		theApp.ExecuteSaveAppStateCommand(false, _T("EmuleDlg"));
		break;
	case TBBTN_RELOADCONF:
	case MP_HM_RELOADCONF:
		thePrefs.LoadBlacklistFile(); // Loads blacklist.conf
		if (theApp.DownloadValidator != NULL && theApp.DownloadValidator->ReloadRegexRules() && thePrefs.GetDownloadValidatorRegexMatching())
			theApp.DownloadValidator->QueueReloadRegexMap();
		theApp.shield->LoadShieldFile(); // Loads shield.conf
		break;
	case TBBTN_BACKUP:
	case MP_HM_BACKUP:
		theApp.Backup(false);
		break;
	case TBBTN_TOOLS:
		ShowToolPopup(true);
		break;
	case TBBTN_EMULEAI:
		ShowEmuleAIPopup();
		break;
	case MP_HM_OPENINC:
		ShellOpenFile(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
		break;
	case MP_HM_HELP:
		if (activewnd != NULL) {
			HELPINFO hi = {};
			hi.cbSize = (UINT)sizeof(HELPINFO);
			activewnd->SendMessage(WM_HELP, 0, (LPARAM)&hi);
		} else
			wParam = ID_HELP;
		break;
	case MP_HM_EXIT:
		OnClose();
		break;
	case MP_HM_LINK1: // MOD: don't remove!
		BrowserOpen(thePrefs.GetHomepageBaseURL(), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_HM_LINK2:
		BrowserOpen(thePrefs.GetHomepageBaseURL() + _T("/faq/"), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_EMULE_AI_ABOUT:
		if (m_pSplashWnd != NULL)
			DestroySplash();
		ShowSplash(false);
		break;
	case MP_EMULE_AI_SPECIAL_THANKS:
		if (m_pSplashWnd != NULL)
			DestroySplash();
		ShowSpecialThanks();
		break;
	case MP_EMULE_AI_CHECK_FOR_UPDATES:
		DoVersioncheck(true);
		break;
	case MP_EMULE_AI_GITHUB_REPO:
		BrowserOpen(MOD_REPO_BASE_URL, thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_EMULE_AI_REPORT_ISSUE:
		BrowserOpen(CString(MOD_ISSUES_URL), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_EMULE_AI_DISCUSSIONS:
		BrowserOpen(CString(MOD_DISCUSSIONS_URL), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_EMULE_AI_HOMEPAGE_DOCUMENTATION:
		BrowserOpen(MOD_PAGES_BASE_URL, thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_EMULE_AI_EMULE_HELP:
		BrowserOpen(_T("https://www.emule-project.com/home/perl/help.cgi?l=1"), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_EMULE_AI_ASK_CHATGPT_HELP:
		BrowserOpen(BuildEmuleAiAssistantUrl(EMULE_AI_CHATGPT_HELP_BASE_URL, EMULE_AI_CHATGPT_PROMPT_PARAM, _T("EMULE_AI_PROMPT_FOR_HELP")), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_WEBSVC_EDIT:
		theWebServices.Edit();
		break;
	case MP_HM_CONVERTPF:
		CPartFileConvert::ShowGUI();
		break;
	case MP_HM_SCHEDONOFF:
		thePrefs.SetSchedulerEnabled(!thePrefs.IsSchedulerEnabled());
		theApp.scheduler->Check(true);
		break;
	case MP_HM_1STSWIZARD:
		FirstTimeWizard();
		break;
	case MP_HM_MIGRATION_WIZARD:
	{
		thePrefs.SetMigrationWizardRunOnNextStart(true);
		CDarkMode::MessageBox(GetResString(_T("EMULE_AI_MIGRATION_WIZARD_SCHEDULED_RESTART")), MB_OK | MB_ICONINFORMATION);
	}
	break;
	case MP_HM_IPFILTER:
	{
		CIPFilterDlg dlg;
		dlg.DoModal();
	}
	break;
	case MP_HM_DIRECT_DOWNLOAD:
	{
		CDirectDownloadDlg dlg;
		dlg.DoModal();
	}
	}
	if (wParam >= MP_WEBURL && wParam <= MP_WEBURL + 99)
		theWebServices.RunURL(NULL, (UINT)wParam);
	else if (wParam >= MP_SCHACTIONS && wParam <= MP_SCHACTIONS + 99) {
		theApp.scheduler->ActivateSchedule(wParam - MP_SCHACTIONS);
		theApp.scheduler->SaveOriginals(); // use the new settings as original
#ifdef HAVE_WIN7_SDK_H
	}
	else if (HIWORD(wParam) == THBN_CLICKED) {
		OnTBBPressed(LOWORD(wParam));
		return TRUE;
#endif
	// Defining context menu of MuleToolBarCtrl as CMenuXP instead of CMenu causes an "Encounted and improper argument"
	// exception. Redirecting menu commands to theApp.emuledlg fixis this exception.
	} else
		toolbar->OnCommand(wParam, lParam);

	return CTrayDialog::OnCommand(wParam, lParam);
}

LRESULT CemuleDlg::OnMenuChar(UINT nChar, UINT nFlags, CMenu* pMenu)
{
	UINT nCmdID;
	if (toolbar->MapAccelerator((TCHAR)nChar, &nCmdID)) {
		OnCommand(nCmdID, 0);
		return MAKELONG(0, MNC_CLOSE);
	}
	return CTrayDialog::OnMenuChar(nChar, nFlags, pMenu);
}

void CemuleDlg::OnBnClickedHotmenu()
{
	ShowToolPopup(false);
}

void CemuleDlg::ShowToolPopup(bool toolsonly)
{
	POINT point = {};
	::GetCursorPos(&point);

	CMenuXP menu;
	menu.CreatePopupMenu();
	menu.AddMenuSidebar(GetResString(toolsonly ? _T("TOOLS") : _T("HOTMENU")));

	CMenuXP Links;
	Links.CreateMenu();
	theWebServices.GetGeneralMenuEntries(&Links);
	if (Links.GetMenuItemCount() > 0)
		Links.AppendMenu(MF_SEPARATOR);
	Links.AppendMenu(MF_STRING, MP_WEBSVC_EDIT, GetResString(_T("WEBSVEDIT")));

	CMenuXP scheduler;
	scheduler.CreatePopupMenu();
	const CString& schedonoff(GetResString(thePrefs.IsSchedulerEnabled() ? _T("HM_SCHED_OFF") : _T("HM_SCHED_ON")));

	scheduler.AppendMenu(MF_STRING, MP_HM_SCHEDONOFF, schedonoff);
	if (theApp.scheduler->GetCount() > 0) {
		scheduler.AppendMenu(MF_SEPARATOR);
		for (INT_PTR i = 0; i < theApp.scheduler->GetCount(); ++i)
			scheduler.AppendMenu(MF_STRING, MP_SCHACTIONS + i, theApp.scheduler->GetSchedule(i)->title);
	}

	if (!toolsonly) {
		if (theApp.serverconnect->IsConnected())
			menu.AppendMenu(MF_STRING, MP_HM_CON, GetResStringWithAccel(_T("IRC_DISCONNECT"), _T('c')), _T("DISCONNECT"));
		else if (theApp.serverconnect->IsConnecting())
			menu.AppendMenu(MF_STRING, MP_HM_CON, GetResStringWithAccel(_T("CANCEL"), _T('C')), _T("STOPCONNECTING"));
		else
			menu.AppendMenu(MF_STRING, MP_HM_CON, GetResStringWithAccel(_T("IRC_CONNECT"), _T('C')), _T("CONNECT"));

		menu.AppendMenu(MF_STRING, MP_HM_KAD, GetResStringWithAccel(_T("KADEMLIA"), _T('K')), _T("KADEMLIA"));
		menu.AppendMenu(MF_STRING, MP_HM_SRVR, GetResStringWithAccel(_T("FSTAT_SERVERS"), _T('v')), _T("SERVER"));
		menu.AppendMenu(MF_STRING, MP_HM_TRANSFER, GetResString(_T("EM_TRANS")), _T("TRANSFER"));
		menu.AppendMenu(MF_STRING, MP_HM_SEARCH, GetResStringWithAccel(_T("SW_SEARCHBOX"), _T('S')), _T("SEARCH"));
		menu.AppendMenu(MF_STRING, MP_HM_FILES, GetResStringWithAccel(_T("FILES"), _T('F')), _T("Files"));
		menu.AppendMenu(MF_STRING, MP_HM_MSGS, GetResStringWithAccel(_T("CW_MESSAGES"), _T('M')), _T("MESSAGES"));
		menu.AppendMenu(MF_STRING, MP_HM_IRC, GetResString(_T("IRC")), _T("IRC"));
		menu.AppendMenu(MF_STRING, MP_HM_STATS, GetResStringWithAccel(_T("SF_STATISTICS"), _T('a')), _T("STATISTICS"));
		menu.AppendMenu(MF_STRING, MP_HM_PREFS, GetResStringWithAccel(_T("OPTIONS"), _T('O')), _T("PREFERENCES"));
		menu.AppendMenu(MF_STRING, MP_HM_SAVESTATE, GetResString(_T("SAVE_APP_STATE")), _T("SAVE"));
		menu.AppendMenu(MF_STRING, MP_HM_RELOADCONF, GetResString(_T("RELOAD_CONF")), _T("RELOAD CONF"));
		menu.AppendMenu(MF_STRING, MP_HM_BACKUP, GetResString(_T("BACKUP")), _T("BACKUP"));
		menu.AppendMenu(MF_SEPARATOR);
	}

	menu.AppendMenu(MF_STRING, MP_HM_OPENINC, GetResString(_T("OPENINC")), _T("INCOMING"));
	menu.AppendMenu(MF_STRING, MP_HM_CONVERTPF, GetResString(_T("IMPORTSPLPF")), _T("CONVERT"));
	menu.AppendMenu(MF_STRING, MP_HM_1STSWIZARD, GetResString(_T("WIZ1")), _T("WIZARD"));
	menu.AppendMenu(MF_STRING, MP_HM_MIGRATION_WIZARD, GetResString(_T("EMULE_AI_MIGRATION_WIZARD")), _T("WIZARD"));
	menu.AppendMenu(MF_STRING, MP_HM_IPFILTER, GetResString(_T("IPFILTER")), _T("IPFILTER"));
	menu.AppendMenu(MF_STRING, MP_HM_DIRECT_DOWNLOAD, GetResString(_T("SW_DIRECTDOWNLOAD")), _T("PASTELINK"));

	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)Links.m_hMenu, GetResString(_T("LINKS")), _T("WEB"));
	menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)scheduler.m_hMenu, GetResString(_T("SCHEDULER")), _T("SCHEDULER"));

	if (!toolsonly) {
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, MP_HM_EXIT, GetResString(_T("EXIT")), _T("EXIT"));
	}
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
	VERIFY(Links.DestroyMenu());
	VERIFY(scheduler.DestroyMenu());
	VERIFY(menu.DestroyMenu());
}

void CemuleDlg::ShowEmuleAIPopup()
{
	POINT point = {};
	::GetCursorPos(&point);
	const LPCTSTR pszGithubIcon = IsDarkModeEnabled() ? _T("GITHUB_LIGHT") : _T("GITHUB");
	const LPCTSTR pszReportIssueIcon = _T("REPORTISSUE");
	const LPCTSTR pszChatGptIcon = IsDarkModeEnabled() ? _T("CHATGPT_LIGHT") : _T("CHATGPT");

	CMenuXP menu;
	menu.CreatePopupMenu();
	menu.AddMenuSidebar(GetResString(_T("PW_MOD")));

	// eMule AI main navigation
	menu.AppendMenu(MF_STRING, MP_EMULE_AI_HOMEPAGE_DOCUMENTATION, GetResString(_T("EMULE_AI_MENU_HOMEPAGE_DOCUMENTATION")), _T("EMULEAI"));
	menu.AppendMenu(MF_SEPARATOR);

	menu.AppendMenu(MF_STRING, MP_EMULE_AI_GITHUB_REPO, GetResString(_T("EMULE_AI_MENU_GITHUB_REPO_STAR")), pszGithubIcon);
	menu.AppendMenu(MF_STRING, MP_EMULE_AI_REPORT_ISSUE, GetResString(_T("EMULE_AI_MENU_REPORT_ISSUE")), pszReportIssueIcon);
	menu.AppendMenu(MF_STRING, MP_EMULE_AI_DISCUSSIONS, GetResString(_T("EMULE_AI_MENU_DISCUSSIONS")), _T("Discussions"));
	menu.AppendMenu(MF_SEPARATOR);

	menu.AppendMenu(MF_STRING, MP_EMULE_AI_ASK_CHATGPT_HELP, GetResString(_T("EMULE_AI_MENU_ASK_CHATGPT_HELP")), pszChatGptIcon);
	menu.AppendMenu(MF_SEPARATOR);

	menu.AppendMenu(MF_STRING, MP_HM_LINK1, GetResString(_T("HM_LINKHP")), _T("MULE_VISTA_ICON"));
	menu.AppendMenu(MF_STRING, MP_EMULE_AI_EMULE_HELP, GetResString(_T("EMULE_AI_MENU_EMULE_HELP")), _T("MULE_VISTA_ICON"));
	menu.AppendMenu(MF_STRING, MP_HM_LINK2, GetResString(_T("HM_LINKFAQ")), _T("MULE_VISTA_ICON"));
	menu.AppendMenu(MF_SEPARATOR);

	menu.AppendMenu(MF_STRING, MP_EMULE_AI_SPECIAL_THANKS, GetResString(_T("EMULE_AI_MENU_SPECIAL_THANKS")), _T("INFO"));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, MP_EMULE_AI_ABOUT, GetResString(_T("EMULE_AI_MENU_ABOUT")), _T("INFO"));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, MP_EMULE_AI_CHECK_FOR_UPDATES, GetResString(_T("EMULE_AI_MENU_CHECK_FOR_UPDATES")), _T("EMULE_AI_CHECK_UPDATE"));
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
	VERIFY(menu.DestroyMenu());
}


void CemuleDlg::ApplyHyperTextFont(LPLOGFONT pFont)
{
	theApp.m_fontHyperText.DeleteObject();
	if (theApp.m_fontHyperText.CreateFontIndirect(pFont)) {
		thePrefs.SetHyperTextFont(pFont);
		serverwnd->servermsgbox->SetFont(&theApp.m_fontHyperText);
		chatwnd->chatselector.UpdateFonts(&theApp.m_fontHyperText);
		ircwnd->UpdateFonts(&theApp.m_fontHyperText);
	}
}

void CemuleDlg::ApplyLogFont(LPLOGFONT pFont)
{
	theApp.m_fontLog.DeleteObject();
	if (theApp.m_fontLog.CreateFontIndirect(pFont)) {
		thePrefs.SetLogFont(pFont);
		serverwnd->logbox->SetFont(&theApp.m_fontLog);
		serverwnd->debuglog->SetFont(&theApp.m_fontLog);
		serverwnd->protectionlog->SetFont(&theApp.m_fontLog);
	}
}

LRESULT CemuleDlg::OnFrameGrabFinished(WPARAM wParam, LPARAM lParam)
{
	CKnownFile* pOwner = reinterpret_cast<CKnownFile*>(wParam);
	FrameGrabResult_Struct* result = (FrameGrabResult_Struct*)lParam;

	if (theApp.knownfiles->IsKnownFile(pOwner) || theApp.downloadqueue->IsPartFile(pOwner))
		pOwner->GrabbingFinished(result->imgResults, result->nImagesGrabbed, result->pSender);
	else
		ASSERT(0);

	delete result;
	return 0;
}

void StraightWindowStyles(CWnd* pWnd)
{
	for (CWnd* pWndChild = pWnd->GetWindow(GW_CHILD); pWndChild != NULL; pWndChild = pWndChild->GetNextWindow())
		StraightWindowStyles(pWndChild);

	TCHAR szClassName[MAX_PATH];
	if (GetClassName(*pWnd, szClassName, _countof(szClassName))) {
		if (_tcsicmp(szClassName, _T("Button")) == 0)
			pWnd->ModifyStyle(BS_FLAT, 0);
		else if (_tcsicmp(szClassName, _T("EDIT")) == 0 && (pWnd->GetExStyle() & WS_EX_STATICEDGE)
			|| _tcsicmp(szClassName, _T("SysListView32")) == 0
			|| _tcsicmp(szClassName, _T("msctls_trackbar32")) == 0)
		{
			pWnd->ModifyStyleEx(WS_EX_STATICEDGE, WS_EX_CLIENTEDGE);
		}
	}
}

void ApplySystemFont(CWnd* pWnd)
{
	for (CWnd* pWndChild = pWnd->GetWindow(GW_CHILD); pWndChild != NULL; pWndChild = pWndChild->GetNextWindow())
		ApplySystemFont(pWndChild);

	TCHAR szClassName[MAX_PATH];
	if (GetClassName(*pWnd, szClassName, _countof(szClassName))
		&& (_tcsicmp(szClassName, _T("SysListView32")) == 0 || _tcsicmp(szClassName, _T("SysTreeView32")) == 0))
	{
		pWnd->SendMessage(WM_SETFONT, NULL, FALSE);
	}
}

// Adding WS_EX_STATICEDGE to controls causes flickering on some controls, especially on owner drawn controls.
// So we only apply the flat style to buttons and leave the rest as it is.
void FlatWindowStyles(CWnd* pWnd)
{
	for (CWnd* pWndChild = pWnd->GetWindow(GW_CHILD); pWndChild != NULL; pWndChild = pWndChild->GetNextWindow())
		FlatWindowStyles(pWndChild);

	TCHAR szClassName[MAX_PATH];
	if (GetClassName(*pWnd, szClassName, _countof(szClassName)) && _tcsicmp(szClassName, _T("Button")) == 0)
		pWnd->ModifyStyle(0, BS_FLAT);
}

void InitWindowStyles(CWnd* pWnd, bool bForTheApp)
{
	if (!thePrefs.GetStraightWindowStyles())
		FlatWindowStyles(pWnd);

	if (IsDarkModeEnabled()) {
		CDarkMode::Initialize();
		ApplyTheme(pWnd->GetSafeHwnd());
	}
}
LRESULT CemuleDlg::OnDarkModeSwitch(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	CWaitCursor curWait;
	const bool bReopenPreferences = preferenceswnd != NULL && ::IsWindow(preferenceswnd->GetSafeHwnd()) && preferenceswnd->m_bApplyButtonClicked;

	ApplyTheme(AfxGetMainWnd()->GetSafeHwnd()); // Switch color scheme for the entire application
	if (sharedfileswnd != NULL && ::IsWindow(sharedfileswnd->GetSafeHwnd()))
		sharedfileswnd->m_dlgDetails.EnsureDarkModeTabControl();
	if (m_pMiniMule != NULL && !m_pMiniMule->IsInInitDialog() && ::IsWindow(m_pMiniMule->GetSafeHwnd()))
		ApplyThemeToWindow(m_pMiniMule->GetSafeHwnd(), true, false);

	if (bReopenPreferences)
		preferenceswnd->RequestModalReopen(IDD_PPG_MOD);

	return 0;
}

void CemuleDlg::OnRebarCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMRBCUSTOMDRAW pDraw = reinterpret_cast<LPNMRBCUSTOMDRAW>(pNMHDR);
	if (!pDraw || !IsDarkModeEnabled()) {
		*pResult = CDRF_DODEFAULT;
		return;
	}

	switch (pDraw->nmcd.dwDrawStage) {
	case CDDS_PREPAINT:
		*pResult = CDRF_NOTIFYITEMDRAW;
		return;

	case CDDS_ITEMPREPAINT:
		::FillRect(pDraw->nmcd.hdc, &pDraw->rcBand, (HBRUSH)CDarkMode::m_brDefault);
		*pResult = CDRF_DODEFAULT; // Request post-paint to draw chevron
		return;
	}

	*pResult = CDRF_DODEFAULT;
}

UINT AFX_CDECL CemuleDlg::EmuleAIVersionCheckThread(LPVOID pParam)
{
	DbgSetThreadName("EmuleAIVersionCheck");
	EmuleAIVersionCheckRequest* pRequest = reinterpret_cast<EmuleAIVersionCheckRequest*>(pParam);
	if (pRequest == NULL)
		return 0;

	EmuleAIVersionCheckResult* pResult = new EmuleAIVersionCheckResult;
	pResult->bRequestSucceeded = false;
	pResult->bHasNewVersion = false;
	pResult->bManual = pRequest->bManual;

	CString strDownloadedText;
	if (DownloadVersionTextFromUrl(pRequest->strVersionRawUrl, strDownloadedText)
		&& TryExtractVersionFromDownloadedText(strDownloadedText, pResult->strRemoteVersion))
	{
		pResult->bRequestSucceeded = true;

		const CString strCurrentVersion = NormalizeVersionString(pRequest->strCurrentVersion);
		if (!strCurrentVersion.IsEmpty()
			&& IsRemoteVersionNewer(pResult->strRemoteVersion, strCurrentVersion))
		{
			pResult->bHasNewVersion = true;
		}
	}

	if (::IsWindow(pRequest->hNotifyWnd))
		::PostMessage(pRequest->hNotifyWnd, UWM_EMULEAI_VERSIONCHECK_RESULT, 0, reinterpret_cast<LPARAM>(pResult));
	else
		delete pResult;

	delete pRequest;
	return 0;
}

LRESULT CemuleDlg::OnEmuleAIVersionCheckResult(WPARAM, LPARAM lParam)
{
	EmuleAIVersionCheckResult* pResult = reinterpret_cast<EmuleAIVersionCheckResult*>(lParam);
	m_bVersionCheckInProgress = false;
	if (pResult == NULL)
		return 0;

	if (false) {
		m_bNewVersionAvailable = false;
		StopTitleVersionAnimation();
		InvalidateTitleVersionFrame();
		delete pResult;
		return 0;
	}

	if (pResult->bRequestSucceeded) {
		thePrefs.UpdateLastVC();
		m_bNewVersionAvailable = pResult->bHasNewVersion;
		if (m_bNewVersionAvailable)
			thePrefs.SetLastKnownVersionOnServer(pResult->strRemoteVersion);
		else
			thePrefs.SetLastKnownVersionOnServer(_T(""));
		if (m_bNewVersionAvailable) {
			StartTitleVersionAnimation();
			TRACE(_T("eMuleAI new version found. Local=\"%s\" Remote=\"%s\"\n"), (LPCTSTR)NormalizeVersionString(MOD_VERSION), (LPCTSTR)pResult->strRemoteVersion);
			AddLogLine(true, GetResString(_T("EMULE_AI_VERSION_CHECK_NEW_VERSION")), (LPCTSTR)NormalizeVersionString(MOD_VERSION), (LPCTSTR)pResult->strRemoteVersion);
		} else {
			StopTitleVersionAnimation();
			AddLogLine(true, GetResString(_T("EMULE_AI_VERSION_CHECK_UP_TO_DATE")), (LPCTSTR)NormalizeVersionString(MOD_VERSION));
		}
		InvalidateTitleVersionFrame();
	} else {
		TRACE(_T("eMuleAI version check request failed.\n"));
		AddLogLine(true, GetResString(_T("EMULE_AI_VERSION_CHECK_FAILED")));
	}

	delete pResult;
	return 0;
}

void CemuleDlg::EnsureTitleVersionLinkFont()
{
	if (m_fontTitleVersionLink.GetSafeHandle() != NULL)
		return;

	NONCLIENTMETRICS ncm = {};
	ncm.cbSize = sizeof(ncm);
	if (!::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0))
		return;

	ncm.lfCaptionFont.lfUnderline = TRUE;
	ncm.lfCaptionFont.lfWeight = FW_BOLD;
	m_fontTitleVersionLink.CreateFontIndirect(&ncm.lfCaptionFont);
}

void CemuleDlg::EnsureIpGuardOverlayFont()
{
	if (m_fontIpGuardOverlay.GetSafeHandle() != NULL)
		return;

	NONCLIENTMETRICS ncm = {};
	ncm.cbSize = sizeof(ncm);
	if (!::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0))
		return;

	ncm.lfCaptionFont.lfUnderline = FALSE;
	ncm.lfCaptionFont.lfWeight = FW_BOLD;
	ncm.lfCaptionFont.lfQuality = NONANTIALIASED_QUALITY;
	if (ncm.lfCaptionFont.lfHeight < 0)
		ncm.lfCaptionFont.lfHeight -= 2;
	else if (ncm.lfCaptionFont.lfHeight > 0)
		ncm.lfCaptionFont.lfHeight += 2;
	m_fontIpGuardOverlay.CreateFontIndirect(&ncm.lfCaptionFont);
}

bool CemuleDlg::IsIpGuardOverlayVisible() const
{
	return m_bIpGuardStartupBlocked && !m_strIpGuardOverlayText.IsEmpty() && IsWindowVisible() && !IsIconic();
}

CString CemuleDlg::GetTitleVersionLinkText() const
{
	if (IsIpGuardOverlayVisible())
		return m_strIpGuardOverlayText;
	return GetResString(TITLE_VERSION_LINK_TEXT_KEY);
}

bool CemuleDlg::TryBuildTitleVersionLinkRect(CDC& dc, const CRect& rcWindow, CRect& rcOut)
{
	rcOut.SetRectEmpty();
	if (!IsTitleVersionLinkVisible() || rcWindow.IsRectEmpty())
		return false;

	EnsureTitleVersionLinkFont();
	EnsureIpGuardOverlayFont();

	CFont* pOldFont = NULL;
	if (IsIpGuardOverlayVisible()) {
		if (m_fontIpGuardOverlay.GetSafeHandle() != NULL)
			pOldFont = dc.SelectObject(&m_fontIpGuardOverlay);
	}
	else if (m_fontTitleVersionLink.GetSafeHandle() != NULL)
		pOldFont = dc.SelectObject(&m_fontTitleVersionLink);

	const CString strText(GetTitleVersionLinkText());
	if (strText.IsEmpty()) {
		if (pOldFont != NULL)
			dc.SelectObject(pOldFont);
		return false;
	}

	const CSize sizText = dc.GetTextExtent(strText);
	if (pOldFont != NULL)
		dc.SelectObject(pOldFont);
	if (sizText.cx <= 0 || sizText.cy <= 0)
		return false;

	CRect rcCurrentWindow;
	GetWindowRect(&rcCurrentWindow);
	CRect rcClient;
	GetClientRect(&rcClient);
	ClientToScreen(&rcClient);

	int nCaptionHeight = rcClient.top - rcCurrentWindow.top;
	if (nCaptionHeight <= 0)
		nCaptionHeight = ::GetSystemMetrics(SM_CYCAPTION);
	nCaptionHeight = max(1, nCaptionHeight);

	const int nTextTop = rcWindow.top + max(0, (nCaptionHeight - sizText.cy) / 2);

	int nPaddedBorder = 0;
#ifdef SM_CXPADDEDBORDER
	nPaddedBorder = ::GetSystemMetrics(SM_CXPADDEDBORDER);
#endif
	const int nFrameX = ::GetSystemMetrics(SM_CXFRAME) + nPaddedBorder;
	const int nCaptionButtonsWidth = ::GetSystemMetrics(SM_CXSIZE) * 3;

	int nLeftBound = rcWindow.left + nFrameX + ::GetSystemMetrics(SM_CXSMICON) + (TITLE_VERSION_TEXT_HORIZONTAL_PADDING * 2);
	int nRightBound = rcWindow.right - nFrameX - nCaptionButtonsWidth - TITLE_VERSION_TEXT_HORIZONTAL_PADDING;

	CRect rcCaptionButtons;
	if (GetCaptionButtonBounds(m_hWnd, rcCaptionButtons)) {
		const int nCaptionButtonsRightMargin = rcCurrentWindow.right - rcCaptionButtons.right;
		const int nCaptionButtonsLeft = rcWindow.right - nCaptionButtonsRightMargin - rcCaptionButtons.Width();
		nRightBound = min(nRightBound, nCaptionButtonsLeft - TITLE_VERSION_TEXT_HORIZONTAL_PADDING);
	}

	CString strWindowTitle;
	GetWindowText(strWindowTitle);
	strWindowTitle.Trim();
	if (!strWindowTitle.IsEmpty()) {
		NONCLIENTMETRICS ncm = {};
		ncm.cbSize = sizeof(ncm);
		int nTitleWidth = 0;
		if (::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0)) {
			CFont fontCaption;
			if (fontCaption.CreateFontIndirect(&ncm.lfCaptionFont)) {
				CFont* pOldCaptionFont = dc.SelectObject(&fontCaption);
				nTitleWidth = dc.GetTextExtent(strWindowTitle).cx;
				if (pOldCaptionFont != NULL)
					dc.SelectObject(pOldCaptionFont);
			}
		}
		if (nTitleWidth <= 0)
			nTitleWidth = dc.GetTextExtent(strWindowTitle).cx;

		const int nTitleLeft = rcWindow.left + nFrameX + ::GetSystemMetrics(SM_CXSMICON) + TITLE_VERSION_TEXT_HORIZONTAL_PADDING;
		nLeftBound = max(nLeftBound, nTitleLeft + nTitleWidth + TITLE_VERSION_TEXT_HORIZONTAL_PADDING);
	}

	const int nIdealLeft = rcWindow.left + max(0, (rcWindow.Width() - sizText.cx) / 2);
	const int nIdealRight = nIdealLeft + sizText.cx;
	if (IsIpGuardOverlayVisible()) {
		if (nRightBound - nLeftBound <= 60)
			return false;
		if (nIdealLeft >= nLeftBound && nIdealRight <= nRightBound)
			rcOut.SetRect(nIdealLeft, nTextTop, nIdealRight, nTextTop + sizText.cy);
		else
			rcOut.SetRect(nLeftBound, nTextTop, nRightBound, nTextTop + sizText.cy);
		return true;
	}

	if (nRightBound - nLeftBound <= sizText.cx)
		return false;

	if (nIdealLeft < nLeftBound || nIdealRight > nRightBound)
		return false;

	rcOut.SetRect(nIdealLeft, nTextTop, nIdealRight, nTextTop + sizText.cy);
	return true;
}

void CemuleDlg::UpdateTitleVersionLinkRect(CDC& dc)
{
	m_rcTitleVersionLink.SetRectEmpty();
	CRect rcWindow;
	GetWindowRect(&rcWindow);
	TryBuildTitleVersionLinkRect(dc, rcWindow, m_rcTitleVersionLink);
}

void CemuleDlg::DrawTitleVersionLink(CDC& dc)
{
	if (!IsTitleVersionLinkVisible())
		return;

	EnsureTitleVersionLinkFont();
	EnsureIpGuardOverlayFont();
	UpdateTitleVersionLinkRect(dc);
	if (m_rcTitleVersionLink.IsRectEmpty())
		return;

	CRect rcWindow;
	GetWindowRect(&rcWindow);
	const int nTextLeft = m_rcTitleVersionLink.left - rcWindow.left;
	const int nTextTop = m_rcTitleVersionLink.top - rcWindow.top;

	CFont* pOldFont = NULL;
	if (IsIpGuardOverlayVisible()) {
		if (m_fontIpGuardOverlay.GetSafeHandle() != NULL)
			pOldFont = dc.SelectObject(&m_fontIpGuardOverlay);
	}
	else if (m_fontTitleVersionLink.GetSafeHandle() != NULL)
		pOldFont = dc.SelectObject(&m_fontTitleVersionLink);

	dc.SetBkMode(TRANSPARENT);
	const CString strText(GetTitleVersionLinkText());
	if (strText.IsEmpty()) {
		if (pOldFont != NULL)
			dc.SelectObject(pOldFont);
		return;
	}
	const CSize sizText = dc.GetTextExtent(strText);
	const int nTextY = nTextTop + max(0, (m_rcTitleVersionLink.Height() - sizText.cy) / 2);

	if (IsIpGuardOverlayVisible()) {
		if (((m_uTitleVersionAnimationHue / IP_GUARD_OVERLAY_BLINK_PHASE) % 2) == 0) {
			CRect rcText(m_rcTitleVersionLink);
			rcText.OffsetRect(-rcWindow.left, -rcWindow.top);
			dc.SetTextColor(RGB(255, 48, 48));
			dc.DrawText(strText, rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		}
		if (pOldFont != NULL)
			dc.SelectObject(pOldFont);
		return;
	}

	int nTextX = nTextLeft;
	for (int i = 0; i < strText.GetLength(); ++i) {
		CString strChar(strText.Mid(i, 1));
		CSize sizChar = dc.GetTextExtent(strChar);
		dc.SetTextColor(RGB(0, 0, 0));
		dc.TextOut(nTextX + TITLE_VERSION_TEXT_SHADOW_OFFSET, nTextY + TITLE_VERSION_TEXT_SHADOW_OFFSET, strChar);
		dc.SetTextColor(GetRainbowColor((int)m_uTitleVersionAnimationHue + i * TITLE_VERSION_CHARACTER_HUE_STEP));
		dc.TextOut(nTextX, nTextY, strChar);
		nTextX += sizChar.cx;
	}

	if (pOldFont != NULL)
		dc.SelectObject(pOldFont);
}

COLORREF CemuleDlg::GetRainbowColor(int nHueDegrees) const
{
	nHueDegrees %= 360;
	if (nHueDegrees < 0)
		nHueDegrees += 360;

	const int nSection = nHueDegrees / 60;
	const int nRemainder = nHueDegrees % 60;
	const int nRise = (255 * nRemainder) / 60;
	const int nFall = (255 * (60 - nRemainder)) / 60;

	int nRed = 0;
	int nGreen = 0;
	int nBlue = 0;

	switch (nSection) {
	case 0:
		nRed = 255; nGreen = nRise; nBlue = 0;
		break;
	case 1:
		nRed = nFall; nGreen = 255; nBlue = 0;
		break;
	case 2:
		nRed = 0; nGreen = 255; nBlue = nRise;
		break;
	case 3:
		nRed = 0; nGreen = nFall; nBlue = 255;
		break;
	case 4:
		nRed = nRise; nGreen = 0; nBlue = 255;
		break;
	default:
		nRed = 255; nGreen = 0; nBlue = nFall;
		break;
	}

	nRed = TITLE_VERSION_TEXT_MIN_CHANNEL + ((255 - TITLE_VERSION_TEXT_MIN_CHANNEL) * nRed) / 255;
	nGreen = TITLE_VERSION_TEXT_MIN_CHANNEL + ((255 - TITLE_VERSION_TEXT_MIN_CHANNEL) * nGreen) / 255;
	nBlue = TITLE_VERSION_TEXT_MIN_CHANNEL + ((255 - TITLE_VERSION_TEXT_MIN_CHANNEL) * nBlue) / 255;

	return RGB(nRed, nGreen, nBlue);
}

bool CemuleDlg::IsTitleVersionLinkVisible() const
{
	return (IsIpGuardOverlayVisible() || m_bNewVersionAvailable) && IsWindowVisible() && !IsIconic();
}

void CemuleDlg::InvalidateTitleVersionFrame()
{
	if (!::IsWindow(m_hWnd))
		return;

	UpdateTitleVersionOverlayWindow();
	if (m_pTitleVersionOverlay != NULL && ::IsWindow(m_pTitleVersionOverlay->GetSafeHwnd()) && m_pTitleVersionOverlay->IsWindowVisible())
		m_pTitleVersionOverlay->Invalidate(FALSE);
}

void CemuleDlg::PaintTitleVersionLinkNow()
{
	if (!::IsWindow(m_hWnd))
		return;

	UpdateTitleVersionOverlayWindow();
	if (m_pTitleVersionOverlay != NULL && ::IsWindow(m_pTitleVersionOverlay->GetSafeHwnd()) && m_pTitleVersionOverlay->IsWindowVisible())
		m_pTitleVersionOverlay->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

void CemuleDlg::DrawTitleVersionOverlay(CDC& dc, const CRect& rcClient)
{
	if (!IsTitleVersionLinkVisible() || rcClient.IsRectEmpty())
		return;

	EnsureTitleVersionLinkFont();
	EnsureIpGuardOverlayFont();

	CFont* pOldFont = NULL;
	if (IsIpGuardOverlayVisible()) {
		if (m_fontIpGuardOverlay.GetSafeHandle() != NULL)
			pOldFont = dc.SelectObject(&m_fontIpGuardOverlay);
	}
	else if (m_fontTitleVersionLink.GetSafeHandle() != NULL)
		pOldFont = dc.SelectObject(&m_fontTitleVersionLink);

	const CString strText(GetTitleVersionLinkText());
	if (strText.IsEmpty()) {
		if (pOldFont != NULL)
			dc.SelectObject(pOldFont);
		return;
	}

	const CSize sizText = dc.GetTextExtent(strText);
	if (sizText.cx <= 0 || sizText.cy <= 0) {
		if (pOldFont != NULL)
			dc.SelectObject(pOldFont);
		return;
	}

	const int nTextXStart = TITLE_VERSION_OVERLAY_PADDING_X;
	const int nTextYBase = TITLE_VERSION_OVERLAY_PADDING_Y + TITLE_VERSION_TEXT_WAVE_AMPLITUDE;

	dc.SetBkMode(TRANSPARENT);
	if (IsIpGuardOverlayVisible()) {
		if (((m_uTitleVersionAnimationHue / IP_GUARD_OVERLAY_BLINK_PHASE) % 2) == 0) {
			CRect rcText(rcClient);
			rcText.DeflateRect(TITLE_VERSION_OVERLAY_PADDING_X, TITLE_VERSION_OVERLAY_PADDING_Y);
			dc.SetTextColor(RGB(255, 48, 48));
			dc.DrawText(strText, rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		}
		if (pOldFont != NULL)
			dc.SelectObject(pOldFont);
		return;
	}

	int nTextX = nTextXStart;
	for (int i = 0; i < strText.GetLength(); ++i) {
		const CString strChar(strText.Mid(i, 1));
		const CSize sizChar = dc.GetTextExtent(strChar);
		const double dWavePhase = ((double)m_uTitleVersionAnimationHue + (double)i * TITLE_VERSION_WAVE_PHASE_STEP) * TITLE_VERSION_DEG_TO_RAD;
		const double dWaveValue = ::sin(dWavePhase) * TITLE_VERSION_TEXT_WAVE_AMPLITUDE;
		const int nWaveOffset = (dWaveValue >= 0.0) ? (int)(dWaveValue + 0.5) : (int)(dWaveValue - 0.5);
		const int nCharY = nTextYBase + nWaveOffset;

		dc.SetTextColor(RGB(0, 0, 0));
		dc.TextOut(nTextX + TITLE_VERSION_TEXT_SHADOW_OFFSET, nCharY + TITLE_VERSION_TEXT_SHADOW_OFFSET, strChar);

		dc.SetTextColor(GetRainbowColor((int)m_uTitleVersionAnimationHue + i * TITLE_VERSION_CHARACTER_HUE_STEP));
		dc.TextOut(nTextX, nCharY, strChar);
		nTextX += sizChar.cx;
	}

	if (pOldFont != NULL)
		dc.SelectObject(pOldFont);
}

void CemuleDlg::EnsureTitleVersionOverlayWindow()
{
	if (m_pTitleVersionOverlay != NULL && ::IsWindow(m_pTitleVersionOverlay->GetSafeHwnd()))
		return;

	DestroyTitleVersionOverlayWindow();

	m_pTitleVersionOverlay = new CTitleVersionOverlayWnd;
	if (m_pTitleVersionOverlay == NULL)
		return;

	if (!m_pTitleVersionOverlay->CreateOverlay(this)) {
		TRACE(_T("Failed to create title version overlay window.\n"));
		delete m_pTitleVersionOverlay;
		m_pTitleVersionOverlay = NULL;
		return;
	}
}

void CemuleDlg::DestroyTitleVersionOverlayWindow()
{
	if (m_pTitleVersionOverlay == NULL)
		return;

	if (::IsWindow(m_pTitleVersionOverlay->GetSafeHwnd()))
		m_pTitleVersionOverlay->DestroyWindow();

	delete m_pTitleVersionOverlay;
	m_pTitleVersionOverlay = NULL;
}

void CemuleDlg::HideTitleVersionOverlayWindow()
{
	m_rcTitleVersionLink.SetRectEmpty();
	if (m_pTitleVersionOverlay != NULL && ::IsWindow(m_pTitleVersionOverlay->GetSafeHwnd()))
		m_pTitleVersionOverlay->ShowWindow(SW_HIDE);
}

void CemuleDlg::ApplyTitleVersionOverlayRect()
{
	if (m_pTitleVersionOverlay == NULL || !::IsWindow(m_pTitleVersionOverlay->GetSafeHwnd()))
		return;

	CRect rcOverlay(m_rcTitleVersionLink);
	rcOverlay.InflateRect(TITLE_VERSION_OVERLAY_PADDING_X, TITLE_VERSION_OVERLAY_PADDING_Y + TITLE_VERSION_TEXT_WAVE_AMPLITUDE);
	rcOverlay.right += TITLE_VERSION_TEXT_SHADOW_OFFSET;
	rcOverlay.bottom += TITLE_VERSION_TEXT_SHADOW_OFFSET;

	CRect rcCaptionButtons;
	if (GetCaptionButtonBounds(m_hWnd, rcCaptionButtons) && !rcCaptionButtons.IsRectEmpty() && rcOverlay.right > rcCaptionButtons.left)
		rcOverlay.right = rcCaptionButtons.left;
	if (rcOverlay.Width() <= 0 || rcOverlay.Height() <= 0) {
		HideTitleVersionOverlayWindow();
		return;
	}

	m_pTitleVersionOverlay->SetWindowPos(&CWnd::wndTop, rcOverlay.left, rcOverlay.top, max(1, rcOverlay.Width()), max(1, rcOverlay.Height()), SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOSENDCHANGING);
}

void CemuleDlg::UpdateTitleVersionOverlayWindowForRect(const CRect& rcWindow)
{
	if (!::IsWindow(m_hWnd)) {
		HideTitleVersionOverlayWindow();
		return;
	}

	if (!IsTitleVersionLinkVisible()) {
		StopTitleVersionAnimation();
		HideTitleVersionOverlayWindow();
		return;
	}

	CWindowDC dc(this);
	if (!TryBuildTitleVersionLinkRect(dc, rcWindow, m_rcTitleVersionLink) || m_rcTitleVersionLink.IsRectEmpty()) {
		HideTitleVersionOverlayWindow();
		return;
	}

	EnsureTitleVersionOverlayWindow();
	if (m_pTitleVersionOverlay == NULL || !::IsWindow(m_pTitleVersionOverlay->GetSafeHwnd()))
		return;

	ApplyTitleVersionOverlayRect();
	StartTitleVersionAnimation();
}

void CemuleDlg::UpdateTitleVersionOverlayWindow()
{
	if (!::IsWindow(m_hWnd)) {
		HideTitleVersionOverlayWindow();
		return;
	}

	if (!IsTitleVersionLinkVisible()) {
		StopTitleVersionAnimation();
		HideTitleVersionOverlayWindow();
		return;
	}

	CWindowDC dc(this);
	UpdateTitleVersionLinkRect(dc);
	if (m_rcTitleVersionLink.IsRectEmpty()) {
		HideTitleVersionOverlayWindow();
		return;
	}

	EnsureTitleVersionOverlayWindow();
	if (m_pTitleVersionOverlay == NULL || !::IsWindow(m_pTitleVersionOverlay->GetSafeHwnd()))
		return;

	ApplyTitleVersionOverlayRect();
	StartTitleVersionAnimation();
}

void CemuleDlg::StartTitleVersionAnimation()
{
	if (!IsTitleVersionLinkVisible() || !::IsWindow(m_hWnd) || m_bTitleVersionAnimationTimerActive)
		return;

	if (SetTimer(TIMER_TITLE_VERSION_ANIMATION, TITLE_VERSION_ANIMATION_INTERVAL_MS, NULL) == 0)
		TRACE(_T("Failed to start title version animation timer.\n"));
	else
		m_bTitleVersionAnimationTimerActive = true;
}

void CemuleDlg::StopTitleVersionAnimation()
{
	if (::IsWindow(m_hWnd) && m_bTitleVersionAnimationTimerActive)
		KillTimer(TIMER_TITLE_VERSION_ANIMATION);
	m_bTitleVersionAnimationTimerActive = false;
}

void CemuleDlg::OnNcPaint()
{
	CTrayDialog::OnNcPaint();
	UpdateTitleVersionOverlayWindow();
}

void CemuleDlg::OnNcLButtonDown(UINT nHitTest, CPoint point)
{
	CTrayDialog::OnNcLButtonDown(nHitTest, point);
}

BOOL CemuleDlg::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	return CTrayDialog::OnSetCursor(pWnd, nHitTest, message);
}

void CemuleDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TIMER_CLOSE_AFTER_BULK_OPERATIONS) {
		CheckCloseAfterBulkOperations();
		return;
	}

	if (nIDEvent == TIMER_DOWNLOAD_OVERLAY_COMPLETION_DELAY) {
		KillTimer(TIMER_DOWNLOAD_OVERLAY_COMPLETION_DELAY);
		if (serverwnd != NULL)
			serverwnd->FinishServerMetDownloadOverlayDelay();
		CPPgSecurity::FinishIPFilterDownloadOverlayDelay();
		CIPGeolocation::FinishIPGeolocationDownloadOverlayDelay();
		RefreshActiveBulkOperationOverlays();
		return;
	}

	if (nIDEvent == TIMER_STARTUP_APPLY_PUMP) {
		KillTimer(TIMER_STARTUP_APPLY_PUMP);
		m_bStartupApplyPumpTimerActive = false;
		ProcessStartupApplyPump();
		return;
	}

	if (nIDEvent == TIMER_UI_LOG_FLUSH) {
		KillTimer(TIMER_UI_LOG_FLUSH);
		m_bUiLogFlushTimerActive = false;
		FlushQueuedUiLogLines();
		return;
	}

	if (nIDEvent == TIMER_CHUNKED_DOWNLOAD_ADD) {
		KillTimer(TIMER_CHUNKED_DOWNLOAD_ADD);
		theApp.ProcessChunkedDownloadJobs();
		return;
	}

	if (nIDEvent == TIMER_IP_GUARD_MONITOR) {
		CheckIpGuardRuntimeBind();
		CheckIpGuardPublicIpMonitor(false);
		CheckVpnGuardPublicIpMonitor(false);
		return;
	}

	if (nIDEvent == TIMER_TITLE_VERSION_ANIMATION) {
		if (!IsTitleVersionLinkVisible()) {
			StopTitleVersionAnimation();
			return;
		}

		m_uTitleVersionAnimationHue = (m_uTitleVersionAnimationHue + TITLE_VERSION_ANIMATION_HUE_STEP) % 360;
		PaintTitleVersionLinkNow();
		return;
	}

	if (nIDEvent == TIMER_SPECIAL_THANKS_ANIMATION) {
		if (m_pSplashWnd == NULL || !m_pSplashWnd->IsSpecialThanksMode() || !::IsWindow(m_pSplashWnd->GetSafeHwnd())) {
			if (m_bSpecialThanksAnimationTimerActive) {
				KillTimer(TIMER_SPECIAL_THANKS_ANIMATION);
				m_bSpecialThanksAnimationTimerActive = false;
			}
			return;
		}

		m_pSplashWnd->AdvanceAnimationFrame();
		return;
	}

	CTrayDialog::OnTimer(nIDEvent);
}

void CemuleDlg::ShowSpecialThanks()
{
	ShowSplash(false, CSplashScreen::DisplayModeSpecialThanks);
}

void CemuleDlg::ShowSplash(bool bAutoClose, CSplashScreen::DisplayMode eDisplayMode)
{
	ASSERT(m_pSplashWnd == NULL);
	if (m_pSplashWnd == NULL) {
		if (!bAutoClose && eDisplayMode == CSplashScreen::DisplayModeSplash)
			eDisplayMode = CSplashScreen::DisplayModeAbout;

		try {
			m_pSplashWnd = new CSplashScreen;
			m_pSplashWnd->m_bAutoClose = bAutoClose;
			m_pSplashWnd->SetDisplayMode(eDisplayMode);
		} catch (...) {
			return;
		}
		ASSERT(m_hWnd);
		if (m_pSplashWnd->Create(CSplashScreen::IDD, this)) {
			m_pSplashWnd->ShowWindow(SW_SHOW);
			m_pSplashWnd->UpdateWindow();
			if (eDisplayMode == CSplashScreen::DisplayModeSpecialThanks) {
				if (SetTimer(TIMER_SPECIAL_THANKS_ANIMATION, SPECIAL_THANKS_ANIMATION_INTERVAL_MS, NULL) != 0)
					m_bSpecialThanksAnimationTimerActive = true;
				else
					TRACE(_T("Failed to start special thanks animation timer.\n"));
			}
			if (bAutoClose) {
				m_dwSplashTime = ::GetTickCount();
			} else {
				m_dwSplashTime = _UI32_MAX;
			}
		} else {
			delete m_pSplashWnd;
			m_pSplashWnd = NULL;
		}
	}
}

void CemuleDlg::DestroySplash()
{
	if (::IsWindow(m_hWnd) && m_bSpecialThanksAnimationTimerActive) {
		KillTimer(TIMER_SPECIAL_THANKS_ANIMATION);
		m_bSpecialThanksAnimationTimerActive = false;
	}

	if (m_pSplashWnd != NULL) {
		CSplashScreen* pSplashWnd = m_pSplashWnd;
		m_pSplashWnd = NULL;
		if (pSplashWnd->GetSafeHwnd() != NULL && ::IsWindow(pSplashWnd->GetSafeHwnd()))
			pSplashWnd->DestroyWindow();
		else
			delete pSplashWnd;
	}
}

BOOL CemuleApp::IsIdleMessage(MSG* pMsg)
{
	// This function is closely related to 'CemuleDlg::OnKickIdle'.
	//
	// * See MFC source code for 'CWnd::RunModalLoop' to see how those functions are related
	//	 to each other.
	//
	// * See MFC documentation for 'CWnd::IsIdleMessage' to see why WM_TIMER messages are
	//	 filtered here.
	//
	// Generally we want to filter WM_TIMER messages because they are triggering idle
	// processing (e.g. cleaning up temp. MFC maps) and because they are occurring very often
	// in eMule (we have a rather high frequency timer in upload queue). To save CPU load but
	// do not miss the chance to cleanup MFC temp. maps and other stuff, we do not use each
	// occurring WM_TIMER message -- that would just be overkill! However, we can not simply
	// filter all WM_TIMER messages. If eMule is running in taskbar the only messages which
	// are received by main window are those WM_TIMER messages, thus those messages are the
	// only chance to trigger some idle processing. So, we must use at last some of those
	// messages because otherwise we would not do any idle processing at all in some cases.

	static DWORD s_dwLastIdleMessage;
	if (pMsg->message == WM_TIMER) {
		// Allow this WM_TIMER message to trigger idle processing only if we did not do so
		// since some seconds.
		const DWORD curTick = ::GetTickCount();
		if (curTick >= s_dwLastIdleMessage + SEC2MS(5)) {
			s_dwLastIdleMessage = curTick;
			return TRUE;// Request idle processing (will send a WM_KICKIDLE)
		}
		return FALSE;	// No idle processing
	}

	if (!CWinApp::IsIdleMessage(pMsg))
		return FALSE;	// No idle processing

	s_dwLastIdleMessage = ::GetTickCount();
	return TRUE;		// Request idle processing (will send a WM_KICKIDLE)
}

LRESULT CemuleDlg::OnKickIdle(WPARAM, LPARAM lIdleCount)
{
	LRESULT lResult = 0;

	if (m_pSplashWnd) {
		if (m_pSplashWnd->m_bAutoClose) {
			if (::GetTickCount() >= m_dwSplashTime + SPLASH_AUTO_CLOSE_DELAY_MS) {
				// timeout expired, destroy the splash window
				DestroySplash();
				Invalidate(FALSE);
			} else {
				// check again later...
				lResult = 1;
			}
		}
	}

	if (m_bStartMinimized)
		PostStartupMinimized();

	if (searchwnd && searchwnd->m_hWnd) {
		if (!theApp.IsClosing()) {
			//			TCHAR szDbg[80];
						// NOTE: See also 'CemuleApp::IsIdleMessage'. If 'CemuleApp::IsIdleMessage'
						// would not filter most of the WM_TIMER messages we might get a performance
						// problem here because the idle processing would be performed very, very often.
						//
						// The default MFC implementation of 'CWinApp::OnIdle' is sufficient for us. We
						// will get called with 'lIdleCount=0' and with 'lIdleCount=1'.
						//
						// CWinApp::OnIdle(0)	takes care about pending MFC GUI stuff and returns 'TRUE'
						//						to request another invocation to perform more idle processing
						// CWinApp::OnIdle(>=1)	frees temporary internally MFC maps and returns 'FALSE'
						//						because no more idle processing is needed.
			lResult = theApp.OnIdle((LONG)lIdleCount);
		}
	}

	return lResult;
}

int CemuleDlg::MapWindowToToolbarButton(CWnd* pWnd) const
{
	if (pWnd == transferwnd)
		return TBBTN_TRANSFERS;
	if (pWnd == serverwnd)
		return TBBTN_SERVER;
	if (pWnd == sharedfileswnd)
		return TBBTN_SHARED;
	if (pWnd == searchwnd)
		return TBBTN_SEARCH;
	if (pWnd == statisticswnd)
		return TBBTN_STATS;
	if (pWnd == kademliawnd)
		return TBBTN_KAD;
	if (pWnd == ircwnd)
		return TBBTN_IRC;
	if (pWnd == chatwnd)
		return TBBTN_MESSAGES;
	ASSERT(0);
	return -1;
}

CWnd* CemuleDlg::MapToolbarButtonToWindow(int iButtonID) const
{
	switch (iButtonID) {
	case TBBTN_TRANSFERS:
		return transferwnd;
	case TBBTN_SERVER:
		return serverwnd;
	case TBBTN_SHARED:
		return sharedfileswnd;
	case TBBTN_SEARCH:
		return searchwnd;
	case TBBTN_STATS:
		return statisticswnd;
	case TBBTN_KAD:
		return kademliawnd;
	case TBBTN_IRC:
		return ircwnd;
	case TBBTN_MESSAGES:
		return chatwnd;
	}
	ASSERT(0);
	return NULL;
}

bool CemuleDlg::IsWindowToolbarButton(int iButtonID) const
{
	switch (iButtonID) {
	case TBBTN_TRANSFERS:
	case TBBTN_SERVER:
	case TBBTN_SHARED:
	case TBBTN_SEARCH:
	case TBBTN_STATS:
	case TBBTN_KAD:
	case TBBTN_IRC:
	case TBBTN_MESSAGES:
		return true;
	}
	return false;
}

int CemuleDlg::GetNextWindowToolbarButton(int iButtonID, int iDirection) const
{
	ASSERT(iDirection == 1 || iDirection == -1);
	int iButtonCount = toolbar->GetButtonCount();
	if (iButtonCount > 0) {
		int iButtonIdx = toolbar->CommandToIndex(iButtonID);
		if (iButtonIdx >= 0 && iButtonIdx < iButtonCount) {
			int iEvaluatedButtons = 0;
			while (iEvaluatedButtons < iButtonCount) {
				iButtonIdx = iButtonIdx + iDirection;
				if (iButtonIdx < 0)
					iButtonIdx = iButtonCount - 1;
				else if (iButtonIdx >= iButtonCount)
					iButtonIdx = 0;

				TBBUTTON tbbt = {};
				if (toolbar->GetButton(iButtonIdx, &tbbt)) {
					if (IsWindowToolbarButton(tbbt.idCommand))
						return tbbt.idCommand;
				}
				++iEvaluatedButtons;
			}
		}
	}
	return -1;
}

BOOL CemuleDlg::PreTranslateMessage(MSG* pMsg)
{
	if (IsStartupLoadingDialogVisible() && m_pStartupLoadingDlg != NULL && !m_pStartupLoadingDlg->IsDialogMessageTarget(pMsg)) {
		switch (pMsg != NULL ? pMsg->message : 0) {
			case WM_SYSCOMMAND:
				if ((pMsg->wParam & 0xFFF0) == SC_CLOSE)
					m_pStartupLoadingDlg->RequestCancelAndExit();
				return TRUE;
			case WM_COMMAND:
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
			case WM_LBUTTONDOWN:
			case WM_RBUTTONDOWN:
			case WM_MBUTTONDOWN:
			case WM_NCLBUTTONDOWN:
			case WM_NCRBUTTONDOWN:
			case WM_NCMBUTTONDOWN:
				return TRUE;
		}
	}

	BOOL bResult = CTrayDialog::PreTranslateMessage(pMsg);

	if (m_pSplashWnd && m_pSplashWnd->m_hWnd != NULL)
		switch (pMsg->message) {
		case WM_SYSCOMMAND:
			if (pMsg->wParam != SC_CLOSE)
				break;
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
			DestroySplash();
			Invalidate(FALSE);
			return bResult;
		}

	// Handle Ctrl+Tab and Ctrl+Shift+Tab
	if (pMsg->message == WM_KEYDOWN)
		if (pMsg->wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0) {
			int iButtonID = MapWindowToToolbarButton(activewnd);
			if (iButtonID != -1) {
				int iNextButtonID = GetNextWindowToolbarButton(iButtonID, GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
				if (iNextButtonID != -1) {
					CWnd* pWndNext = MapToolbarButtonToWindow(iNextButtonID);
					if (pWndNext)
						SetActiveDialog(pWndNext);
				}
			}
		}

	return bResult;
}

void CemuleDlg::HtmlHelp(DWORD_PTR dwData, UINT nCmd)
{
	CWinApp* pApp = AfxGetApp();
	ASSERT_VALID(pApp);
	ASSERT(pApp->m_pszHelpFilePath != NULL);
	// to use HtmlHelp the method EnableHtmlHelp() must be called in
	// application's constructor
	ASSERT(pApp->m_eHelpType == afxHTMLHelp);

	CWaitCursor wait;

	PrepareForHelp();

	// need to use top level parent (for the case where m_hWnd is in DLL)
	CWnd* pWnd = GetTopLevelParent();

	TRACE(traceAppMsg, 0, _T("HtmlHelp: pszHelpFile = '%s', dwData: $%lx, fuCommand: %d.\n"), pApp->m_pszHelpFilePath, dwData, nCmd);

	bool bHelpError = false;
	CString strHelpError;
	for (int iTry = 0; iTry < 2; ++iTry) {
		if (AfxHtmlHelp(pWnd->m_hWnd, pApp->m_pszHelpFilePath, nCmd, dwData))
			return;
		bHelpError = true;
		strHelpError.LoadString(AFX_IDP_FAILED_TO_LAUNCH_HELP);

		typedef struct tagHH_LAST_ERROR
		{
			int		cbStruct;
			HRESULT	hr;
			BSTR	description;
		} HH_LAST_ERROR;
		HH_LAST_ERROR hhLastError = {};
		hhLastError.cbStruct = (int)sizeof hhLastError;
		if (!AfxHtmlHelp(pWnd->m_hWnd, NULL, HH_GET_LAST_ERROR, reinterpret_cast<DWORD_PTR>(&hhLastError))) {
			if (FAILED(hhLastError.hr)) {
				if (hhLastError.description) {
					strHelpError = hhLastError.description;
					SysFreeString(hhLastError.description);
				}
				if ((ULONG)hhLastError.hr == 0x8004020Aul  /*no topics IDs available in Help file*/
					|| (ULONG)hhLastError.hr == 0x8004020Bul) /*requested Help topic ID not found*/
				{
					// try opening once again without help topic ID
					if (nCmd != HH_DISPLAY_TOC) {
						nCmd = HH_DISPLAY_TOC;
						dwData = 0;
						continue;
					}
				}
			}
		}
		break;
	}

	if (bHelpError) {
		CString msg;
		msg.Format(_T("%s\n\n%s\n\n%s"), pApp->m_pszHelpFilePath, (LPCTSTR)strHelpError, (LPCTSTR)GetResString(_T("ERR_NOHELP")));
		if (CDarkMode::MessageBox(msg, MB_YESNO | MB_ICONERROR) == IDYES)
			BrowserOpen(MOD_PAGES_BASE_URL, thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
	}
}

void CemuleDlg::CreateToolbarCmdIconMap()
{
	m_mapTbarCmdToIcon[TBBTN_CONNECT] = _T("Connect");
	m_mapTbarCmdToIcon[TBBTN_KAD] = _T("Kademlia");
	m_mapTbarCmdToIcon[TBBTN_SERVER] = _T("Server");
	m_mapTbarCmdToIcon[TBBTN_TRANSFERS] = _T("Transfer");
	m_mapTbarCmdToIcon[TBBTN_SEARCH] = _T("Search");
	m_mapTbarCmdToIcon[TBBTN_SHARED] = _T("SharedFiles");
	m_mapTbarCmdToIcon[TBBTN_MESSAGES] = _T("Messages");
	m_mapTbarCmdToIcon[TBBTN_IRC] = _T("IRC");
	m_mapTbarCmdToIcon[TBBTN_STATS] = _T("Statistics");
	m_mapTbarCmdToIcon[TBBTN_OPTIONS] = _T("Preferences");
	m_mapTbarCmdToIcon[TBBTN_TOOLS] = _T("Tools");
	m_mapTbarCmdToIcon[TBBTN_SAVESTATE] = _T("Save");
	m_mapTbarCmdToIcon[TBBTN_RELOADCONF] = _T("Reload");
	m_mapTbarCmdToIcon[TBBTN_BACKUP] = _T("Backup");
	m_mapTbarCmdToIcon[TBBTN_EMULEAI] = _T("EMULEAI");
}

LPCTSTR CemuleDlg::GetIconFromCmdId(UINT uId)
{
	LPCTSTR pszIconId;
	return m_mapTbarCmdToIcon.Lookup(uId, pszIconId) ? pszIconId : NULL;
}

BOOL CemuleDlg::OnChevronPushed(UINT id, LPNMHDR pNMHDR, LRESULT* plResult)
{
	UNREFERENCED_PARAMETER(id);
	if (!thePrefs.GetUseReBarToolbar())
		return FALSE;

	NMREBARCHEVRON* pnmrc = (NMREBARCHEVRON*)pNMHDR;

	ASSERT(id == AFX_IDW_REBAR);
	ASSERT(pnmrc->uBand == 0);
	ASSERT(pnmrc->wID == 0);
	ASSERT(!m_mapTbarCmdToIcon.IsEmpty());

	// get visible area of rebar/toolbar
	CRect rcVisibleButtons;
	toolbar->GetClientRect(&rcVisibleButtons);

	// search the first toolbar button which is not fully visible
	int iButtons = toolbar->GetButtonCount();
	int i = 0;
	for (; i < iButtons; ++i) {
		RECT rcButton;
		toolbar->GetItemRect(i, &rcButton);

		CRect rcVisible;
		if (!rcVisible.IntersectRect(&rcVisibleButtons, &rcButton) || !::EqualRect(&rcButton, rcVisible))
			break;
	}

	// create menu for all toolbar buttons which are not (fully) visible
	BOOL bLastMenuItemIsSep = TRUE;
	CMenuXP menu;
	menu.CreatePopupMenu();
	menu.AddMenuSidebar(_T("eMule"));
	TCHAR szString[256];

	TBBUTTONINFO tbbi;
	tbbi.cbSize = (UINT)sizeof tbbi;
	tbbi.dwMask = TBIF_BYINDEX | TBIF_COMMAND | TBIF_STYLE | TBIF_STATE | TBIF_TEXT;

		tbbi.cchText = _countof(szString);
		tbbi.pszText = szString;
	for (; i < iButtons; ++i)
		if (toolbar->GetButtonInfo(i, &tbbi) >= 0)
			if (tbbi.fsStyle & TBSTYLE_SEP) {
				if (!bLastMenuItemIsSep)
					bLastMenuItemIsSep = menu.AppendMenu(MF_SEPARATOR, 0, (LPCTSTR)NULL);
			} else if (*szString && menu.AppendMenu(MF_STRING, tbbi.idCommand, szString, GetIconFromCmdId(tbbi.idCommand))) {
				bLastMenuItemIsSep = FALSE;
				if (tbbi.fsState & TBSTATE_CHECKED)
					menu.CheckMenuItem(tbbi.idCommand, MF_BYCOMMAND | MF_CHECKED);
				if ((tbbi.fsState & TBSTATE_ENABLED) == 0)
					menu.EnableMenuItem(tbbi.idCommand, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
			}

	CPoint ptMenu(pnmrc->rc.left, pnmrc->rc.top);
	ClientToScreen(&ptMenu);
	ptMenu.y += rcVisibleButtons.Height();
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON, ptMenu.x, ptMenu.y, this);
	*plResult = 1;
	return FALSE;
}

bool CemuleDlg::IsPreferencesDlgOpen() const
{
	return (preferenceswnd->m_hWnd != NULL);
}

INT_PTR CemuleDlg::ShowPreferences(UINT uStartPageID)
{
	if (IsPreferencesDlgOpen()) {
		preferenceswnd->SetForegroundWindow();
		preferenceswnd->BringWindowToTop();
		return -1;
	}

	INT_PTR iResult = -1;
	UINT uPageId = uStartPageID;
	do {
		if (uPageId != UINT_MAX)
			preferenceswnd->SetStartPage(uPageId);
		iResult = preferenceswnd->DoModal();
		uPageId = UINT_MAX;
	} while (preferenceswnd->ConsumeModalReopenRequest(uPageId));
	return iResult;
}



//////////////////////////////////////////////////////////////////
// Web Server related

LRESULT CemuleDlg::OnWebGetSearchResults(WPARAM wParam, LPARAM lParam)
{
	SWebAsyncRequest* pAsyncRequest = reinterpret_cast<SWebAsyncRequest*>(lParam);
	if (pAsyncRequest != NULL && pAsyncRequest->IsValidWebAsyncRequest()) {
		SWebSearchResultsRequest* pRequest = reinterpret_cast<SWebSearchResultsRequest*>(pAsyncRequest);
		if (pRequest->m_pSearchFileArray != NULL && theApp.searchlist != NULL) {
			const int iSortBy = pRequest->m_iSortBy != 0 ? pRequest->m_iSortBy : static_cast<int>(wParam);
			theApp.searchlist->GetWebList(pRequest->m_pSearchFileArray, iSortBy);
			CWebServer::AnnotateWebSearchSnapshot(*pRequest->m_pSearchFileArray);
			if (pRequest->m_pThis != NULL)
				pRequest->m_pThis->StoreWebSearchSnapshot(iSortBy, *pRequest->m_pSearchFileArray);
			pRequest->m_bResult = true;
		}
		pRequest->Complete();
		pRequest->ReleaseReference();
		return 0;
	}

	CQArray<SearchFileStruct, SearchFileStruct> *pSearchFileArray = reinterpret_cast<CQArray<SearchFileStruct, SearchFileStruct>*>(lParam);
	if (pSearchFileArray != NULL && theApp.searchlist != NULL) {
		theApp.searchlist->GetWebList(pSearchFileArray, static_cast<int>(wParam));
		CWebServer::AnnotateWebSearchSnapshot(*pSearchFileArray);
	}
	return 0;
}

LRESULT CemuleDlg::OnWebServerCommand(WPARAM, LPARAM lParam)
{
	SWebServerCommandRequest* pRequest = reinterpret_cast<SWebServerCommandRequest*>(lParam);
	if (pRequest != NULL && pRequest->IsValidWebAsyncRequest()) {
		pRequest->m_bResult = CWebServer::ExecuteServerCommandForWebThread(pRequest->m_eCommand, pRequest->m_strIP, pRequest->m_nPort, pRequest->m_strName, pRequest->m_strDescription, pRequest->m_bStatic, pRequest->m_nPriority, pRequest->m_bAddToStatic, pRequest->m_bConnectNow);
		pRequest->Complete();
		pRequest->ReleaseReference();
	}
	return 0;
}

LRESULT CemuleDlg::OnWebGetTransferSnapshot(WPARAM, LPARAM lParam)
{
	SWebTransferSnapshotRequest *pRequest = reinterpret_cast<SWebTransferSnapshotRequest*>(lParam);
	if (pRequest != NULL && pRequest->IsValidWebAsyncRequest()) {
		if (pRequest->m_pSnapshot != NULL && pRequest->m_pThis != NULL) {
			pRequest->m_bResult = pRequest->m_pThis->BuildTransferSnapshotForWebThread(pRequest->m_pThis, pRequest->m_Data, pRequest->m_iCategory, *pRequest->m_pSnapshot);
			if (pRequest->m_bResult)
				pRequest->m_pThis->StoreWebTransferSnapshot(pRequest->m_iCategory, *pRequest->m_pSnapshot);
		}
		pRequest->Complete();
		pRequest->ReleaseReference();
	}
	return 0;
}

LRESULT CemuleDlg::OnWebGetSharedFilesSnapshot(WPARAM, LPARAM lParam)
{
	SWebSharedFilesSnapshotRequest *pRequest = reinterpret_cast<SWebSharedFilesSnapshotRequest*>(lParam);
	if (pRequest != NULL && pRequest->IsValidWebAsyncRequest()) {
		if (pRequest->m_pSharedArray != NULL) {
			pRequest->m_bResult = CWebServer::BuildSharedFilesSnapshotForWebThread(*pRequest->m_pSharedArray);
			if (pRequest->m_bResult && pRequest->m_pThis != NULL)
				pRequest->m_pThis->StoreWebSharedFilesSnapshot(*pRequest->m_pSharedArray);
		}
		pRequest->Complete();
		pRequest->ReleaseReference();
	}
	return 0;
}

LRESULT CemuleDlg::OnWebGetServerListSnapshot(WPARAM, LPARAM lParam)
{
	SWebServerListSnapshotRequest *pRequest = reinterpret_cast<SWebServerListSnapshotRequest*>(lParam);
	if (pRequest != NULL && pRequest->IsValidWebAsyncRequest()) {
		if (pRequest->m_pServerArray != NULL) {
			pRequest->m_bResult = CWebServer::BuildServerListSnapshotForWebThread(*pRequest->m_pServerArray);
			if (pRequest->m_bResult && pRequest->m_pThis != NULL)
				pRequest->m_pThis->StoreWebServerListSnapshot(*pRequest->m_pServerArray);
		}
		pRequest->Complete();
		pRequest->ReleaseReference();
	}
	return 0;
}

LRESULT CemuleDlg::OnWebGetHeaderSnapshot(WPARAM, LPARAM lParam)
{
	SWebHeaderSnapshotRequest *pRequest = reinterpret_cast<SWebHeaderSnapshotRequest*>(lParam);
	if (pRequest != NULL && pRequest->IsValidWebAsyncRequest()) {
		if (pRequest->m_pSnapshot != NULL) {
			pRequest->m_bResult = CWebServer::BuildHeaderSnapshotForWebThread(*pRequest->m_pSnapshot);
			if (pRequest->m_bResult && pRequest->m_pThis != NULL)
				pRequest->m_pThis->StoreWebHeaderSnapshot(*pRequest->m_pSnapshot);
		}
		pRequest->Complete();
		pRequest->ReleaseReference();
	}
	return 0;
}

LRESULT CemuleDlg::OnWebGetCommentList(WPARAM, LPARAM lParam)
{
	SWebCommentListRequest *pRequest = reinterpret_cast<SWebCommentListRequest*>(lParam);
	if (pRequest != NULL && pRequest->IsValidWebAsyncRequest()) {
		if (pRequest->m_pstrCommentList != NULL) {
			*pRequest->m_pstrCommentList = CWebServer::BuildCommentListForWebThread(pRequest->m_Data);
			pRequest->m_strCommentList = *pRequest->m_pstrCommentList;
			if (pRequest->m_pThis != NULL)
				pRequest->m_pThis->StoreWebCommentList(pRequest->m_strFileHash, pRequest->m_strCommentList);
			pRequest->m_bResult = !pRequest->m_strCommentList.IsEmpty();
		}
		pRequest->Complete();
		pRequest->ReleaseReference();
	}
	return 0;
}

LRESULT CemuleDlg::OnWebFriendCommand(WPARAM, LPARAM lParam)
{
	SWebFriendCommandRequest *pRequest = reinterpret_cast<SWebFriendCommandRequest*>(lParam);
	if (pRequest != NULL && pRequest->IsValidWebAsyncRequest()) {
		uchar UserHash[MDX_DIGEST_SIZE];
		if (strmd4(pRequest->m_strUserHash, UserHash)) {
			if (pRequest->m_bAdd) {
				CUpDownClient *cur_client = theApp.clientlist != NULL ? theApp.clientlist->FindClientByUserHash(UserHash) : NULL;
				if (cur_client != NULL && theApp.friendlist != NULL) {
					theApp.friendlist->AddFriend(cur_client);
					pRequest->m_bResult = true;
				}
			} else {
				CFriend *f = theApp.friendlist != NULL ? theApp.friendlist->SearchFriend(UserHash, CAddress(), 0) : NULL;
				if (f != NULL) {
					theApp.friendlist->RemoveFriend(f);
					pRequest->m_bResult = true;
				}
			}
		}
		pRequest->Complete();
		pRequest->ReleaseReference();
	}
	return 0;
}

LRESULT CemuleDlg::OnWebGUIInteraction(WPARAM wParam, LPARAM lParam)
{

	switch (wParam) {
	case WEBGUIIA_UPDATEMYINFO:
		serverwnd->UpdateMyInfo();
		break;
	case WEBGUIIA_WINFUNC:
		if (thePrefs.GetWebAdminAllowedHiLevFunc()) {
			try {
				HANDLE hToken;
				TOKEN_PRIVILEGES tkp;	// Get a token for this process.

				if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
					throw 0; //parameterless throw not allowed here
				LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
				tkp.PrivilegeCount = 1;  // one privilege to set
				tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;	// Get the shutdown privilege for this process.
				AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0);

				if (lParam == 1) // shutdown
					ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCE, 0);
				else if (lParam == 2)
					ExitWindowsEx(EWX_REBOOT | EWX_FORCE, 0);
			} catch (...) {
				AddLogLine(true, GetResString(_T("WEB_REBOOT")) + _T(' ') + GetResString(_T("FAILED")));
			}
		} else
			AddLogLine(true, GetResString(_T("WEB_REBOOT")) + _T(' ') + GetResStringWithExclamation(_T("SFS_ACCESS_DENIED")));
		break;
	case WEBGUIIA_UPD_CATTABS:
		theApp.emuledlg->transferwnd->UpdateCatTabTitles();
		break;
	case WEBGUIIA_UPD_SFUPDATE:
		if (lParam)
			theApp.sharedfiles->UpdateFile((CKnownFile*)lParam);
		break;
	case WEBGUIIA_UPDATESERVER:
		serverwnd->serverlistctrl.RefreshServer((CServer*)lParam);
		break;
	case WEBGUIIA_STOPCONNECTING:
		theApp.serverconnect->StopConnectionTry();
		break;
	case WEBGUIIA_CONNECTTOSERVER:
		if (!CanUseP2PConnectionCommands()) {
			LogP2PConnectionCommandBlocked(false);
			break;
		}
		if (!lParam)
			theApp.serverconnect->ConnectToAnyServer();
		else
			theApp.serverconnect->ConnectToServer(reinterpret_cast<CServer*>(lParam), false, false, true);
		break;
	case WEBGUIIA_DISCONNECT:
		if (lParam != 2)	// !KAD
			theApp.serverconnect->Disconnect();
		if (lParam != 1)	// !ED2K
			Kademlia::CKademlia::Stop();
		break;
	case WEBGUIIA_SERVER_REMOVE:
		serverwnd->serverlistctrl.RemoveServer(reinterpret_cast<CServer*>(lParam));
		break;
	case WEBGUIIA_SHARED_FILES_RELOAD:
		theApp.sharedfiles->Reload();
		break;
	case WEBGUIIA_ADD_TO_STATIC:
		serverwnd->serverlistctrl.StaticServerFileAppend(reinterpret_cast<CServer*>(lParam));
		break;
	case WEBGUIIA_REMOVE_FROM_STATIC:
		serverwnd->serverlistctrl.StaticServerFileRemove(reinterpret_cast<CServer*>(lParam));
		break;
	case WEBGUIIA_UPDATESERVERMETFROMURL:
		if (!CanUseP2PConnectionCommands())
			LogP2PConnectionCommandBlocked(false);
		else
			theApp.emuledlg->serverwnd->UpdateServerMetFromURL((TCHAR*)lParam);
		break;
	case WEBGUIIA_SHOWSTATISTICS:
		theApp.emuledlg->statisticswnd->ShowStatistics(lParam != 0);
		break;
	case WEBGUIIA_DELETEALLSEARCHES:
		theApp.emuledlg->searchwnd->DeleteAllSearches();
		break;
	case WEBGUIIA_KAD_BOOTSTRAP:
	{
		if (!CanUseP2PConnectionCommands()) {
			LogP2PConnectionCommandBlocked(false);
			break;
		}
		CString ip((LPCTSTR)lParam);
		int pos = ip.Find(_T(':'));
		if (pos >= 0) {
			uint16 port = (uint16)_tstoi(CPTR(ip, pos + 1));
			ip.Truncate(pos);
			Kademlia::CKademlia::Bootstrap(ip, port);
		}
	}
	break;
	case WEBGUIIA_KAD_START:
		if (!CanUseP2PConnectionCommands())
			LogP2PConnectionCommandBlocked(false);
		else
			Kademlia::CKademlia::Start();
		break;
	case WEBGUIIA_KAD_STOP:
		Kademlia::CKademlia::Stop();
		break;
	case WEBGUIIA_KAD_RCFW:
		Kademlia::CKademlia::RecheckFirewalled();
	}

	return 0;
}

void CemuleDlg::TrayMinimizeToTrayChange()
{
	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL) {
		if (!thePrefs.GetMinToTray()) {
			// just for safety, ensure that we are not adding duplicate menu entries
			if (pSysMenu->EnableMenuItem(MP_MINIMIZETOTRAY, MF_BYCOMMAND | MF_ENABLED) == UINT_MAX) {
				ASSERT((MP_MINIMIZETOTRAY & 0xFFF0) == MP_MINIMIZETOTRAY && MP_MINIMIZETOTRAY < 0xF000);
				VERIFY(pSysMenu->InsertMenu(SC_MINIMIZE, MF_BYCOMMAND, MP_MINIMIZETOTRAY, GetResString(_T("PW_TRAY"))));
			} else
				ASSERT(0);
		} else
			(void)pSysMenu->RemoveMenu(MP_MINIMIZETOTRAY, MF_BYCOMMAND);
	}
	CTrayDialog::TrayMinimizeToTrayChange();
}

void CemuleDlg::SetToolTipsDelay(UINT uMilliseconds)
{
	transferwnd->SetToolTipsDelay(uMilliseconds);
	sharedfileswnd->SetToolTipsDelay(uMilliseconds);
}

void CALLBACK CemuleDlg::UPnPTimeOutTimer(HWND /*hwnd*/, UINT /*uiMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/) noexcept
{
	theApp.emuledlg->PostMessage(UM_UPNP_RESULT, (WPARAM)CUPnPImpl::UPNP_TIMEOUT, 0);
}

LRESULT CemuleDlg::OnUPnPResult(WPARAM wParam, LPARAM lParam)
{
	bool bWasRefresh = lParam != 0;
	CUPnPImpl* impl = theApp.m_pUPnPFinder->GetImplementation();

	//>>> WiZaRd - handle "UPNP_TIMEOUT" events!
	if (!bWasRefresh && wParam != CUPnPImpl::UPNP_OK) {
		//just to be sure, stop any running services and also delete the forwarded ports (if necessary)
		if (wParam == CUPnPImpl::UPNP_TIMEOUT) {
			impl->StopAsyncFind();
			impl->DeletePorts();
		}
		// UPnP failed, check if we can retry it with another implementation
		if (theApp.m_pUPnPFinder->SwitchImplentation()) {
			StartUPnP(false);
			return 0;
		}

		DebugLog(_T("No more available UPnP implementations left"));
	}

	if (m_hUPnPTimeOutTimer != 0) {
		VERIFY(::KillTimer(NULL, m_hUPnPTimeOutTimer));
		m_hUPnPTimeOutTimer = 0;
	}
	if (!bWasRefresh)
		if (wParam == CUPnPImpl::UPNP_OK) {
			// remember the last working implementation
			thePrefs.SetLastWorkingUPnPImpl(impl->GetImplementationID());
			Log(GetResString(_T("UPNPSUCCESS")), impl->GetUsedTCPPort(), impl->GetUsedUDPPort());
		} else
			LogWarning(GetResString(_T("UPNPFAILED")));

	if (theApp.IsRunning() && m_bConnectRequestDelayedForUPnP)
		StartConnection();

	return 0;
}

LRESULT CemuleDlg::OnPowerBroadcast(WPARAM wParam, LPARAM lParam)
{
	//DebugLog(_T("DEBUG:Power state change. wParam=%d lPararm=%ld"),wParam,lParam);
	switch (wParam) {
	case PBT_APMRESUMEAUTOMATIC:
		theApp.ResetStandbyOff();
		if (m_bEd2kSuspendDisconnect || m_bKadSuspendDisconnect) {
			DebugLog(_T("Reconnect after Power state change. wParam=%d lPararm=%ld"), wParam, lParam);
			RefreshUPnP(true);
			PostMessage(WM_SYSCOMMAND, MP_CONNECT, 0); // tell to connect. a sec later...
		}
		return TRUE; // message processed.
	case PBT_APMSUSPEND:
		DebugLog(_T("System is going is suspending operation, disconnecting. wParam=%d lPararm=%ld"), wParam, lParam);
		m_bEd2kSuspendDisconnect = theApp.serverconnect->IsConnected();
		m_bKadSuspendDisconnect = Kademlia::CKademlia::IsConnected();
		CloseConnection();
		return TRUE; // message processed.
	}
	return FALSE; // we do not process this message
}

void CemuleDlg::StartUPnP(bool bReset, uint16 nForceTCPPort, uint16 nForceUDPPort)
{
	if (theApp.IsNetworkActivityBlockedByBind())
		return;

	if (theApp.m_pUPnPFinder != NULL && (m_hUPnPTimeOutTimer == 0 || !bReset)) {
		if (bReset) {
			theApp.m_pUPnPFinder->Reset();
			Log(GetResString(_T("UPNPSETUP")));
		}
		try {
			CUPnPImpl* impl = theApp.m_pUPnPFinder->GetImplementation();
			if (impl->IsReady()) {
				impl->SetMessageOnResult(this, UM_UPNP_RESULT);
				if (bReset)
					VERIFY((m_hUPnPTimeOutTimer = ::SetTimer(NULL, 0, SEC2MS(40), (TIMERPROC)UPnPTimeOutTimer)) != 0);
				impl->StartDiscovery((nForceTCPPort ? nForceTCPPort : thePrefs.GetPort())
					, (nForceUDPPort ? nForceUDPPort : thePrefs.GetUDPPort())
					, (thePrefs.GetWSUseUPnP() ? thePrefs.GetWSPort() : 0));
			} else
				/*theApp.emuledlg->*/PostMessage(UM_UPNP_RESULT, (WPARAM)CUPnPImpl::UPNP_FAILED, 0);
		} catch (const CUPnPImpl::UPnPError&) {
			//ignore
		} catch (CException *ex) {
			ex->Delete();
		}
	} else
		ASSERT(0);
}

void CemuleDlg::RefreshUPnP(bool bRequestAnswer)
{
	if (theApp.IsNetworkActivityBlockedByBind() || !thePrefs.IsUPnPEnabled())
		return;
	if (theApp.m_pUPnPFinder != NULL && m_hUPnPTimeOutTimer == 0) {
		try {
			CUPnPImpl* impl = theApp.m_pUPnPFinder->GetImplementation();
			if (impl->IsReady()) {
				if (bRequestAnswer)
					impl->SetMessageOnResult(this, UM_UPNP_RESULT);
				if (impl->CheckAndRefresh() && bRequestAnswer)
					VERIFY((m_hUPnPTimeOutTimer = ::SetTimer(NULL, 0, SEC2MS(10), UPnPTimeOutTimer)) != 0);
				else
					impl->SetMessageOnResult(NULL, 0);
			} else
				DebugLogWarning(_T("RefreshUPnP, implementation not ready"));
		} catch (const CUPnPImpl::UPnPError&) {
			//ignore
		} catch (CException *ex) {
			ex->Delete();
		}
	} else
		ASSERT(0);
}

BOOL CemuleDlg::OnDeviceChange(UINT nEventType, DWORD_PTR dwData)
{
	// WM_DEVICECHANGE is sent for:
	//	Drives which where created/deleted with "SUBST" command (handled like network drives)
	//	Drives which where created/deleted as regular network drives.
	//
	// WM_DEVICECHANGE is *NOT* sent for:
	//	Floppy disk drives
	//	ZIP disk drives (although Windows Explorer recognises a changed media, we do not get a message)
	//	CD-ROM drives (although MSDN says different...)
	//
	if ((nEventType == DBT_DEVICEARRIVAL || nEventType == DBT_DEVICEREMOVECOMPLETE) && !IsBadReadPtr((void*)dwData, sizeof(DEV_BROADCAST_HDR))) {
#ifdef _DEBUG
		CString strMsg(nEventType == DBT_DEVICEARRIVAL ? _T("DBT_DEVICEARRIVAL") : _T("DBT_DEVICEREMOVECOMPLETE"));
#endif
		const DEV_BROADCAST_HDR* pHdr = (DEV_BROADCAST_HDR*)dwData;
		if (pHdr->dbch_devicetype == DBT_DEVTYP_VOLUME && !IsBadReadPtr((void*)dwData, sizeof(DEV_BROADCAST_VOLUME))) {
			const DEV_BROADCAST_VOLUME* pVol = (DEV_BROADCAST_VOLUME*)pHdr;
#ifdef _DEBUG
			strMsg += _T(" Volume");
			if (pVol->dbcv_flags & DBTF_MEDIA)
				strMsg += _T(" Media");
			if (pVol->dbcv_flags & DBTF_NET)
				strMsg += _T(" Net");
			if ((pVol->dbcv_flags & ~(DBTF_NET | DBTF_MEDIA)) != 0)
				strMsg.AppendFormat(_T(" flags=0x%08x"), pVol->dbcv_flags);
#endif
			bool bVolumesChanged = false;
			for (UINT uDrive = 0; uDrive <= 25; ++uDrive) {
				UINT uMask = 1 << uDrive;
				if (pVol->dbcv_unitmask & uMask) {
					DEBUG_ONLY(strMsg.AppendFormat(_T(" %c:"), _T('A') + uDrive));
					if (pVol->dbcv_flags & (DBTF_MEDIA | DBTF_NET))
						ClearVolumeInfoCache(uDrive);
					bVolumesChanged = true;
				}
			}
			if (bVolumesChanged && sharedfileswnd)
				sharedfileswnd->OnVolumesChanged();
		} else
			DEBUG_ONLY(strMsg.AppendFormat(_T(" devicetype=0x%08x"), pHdr->dbch_devicetype));

#ifdef _DEBUG
		TRACE(_T("CemuleDlg::OnDeviceChange: %s\n"), (LPCTSTR)strMsg);
#endif
	} else
		TRACE2(_T("CemuleDlg::OnDeviceChange: nEventType=0x%08x  dwData=0x%08x\n"), nEventType, dwData);

	return __super::OnDeviceChange(nEventType, dwData);
}

LRESULT CemuleDlg::OnDisplayChange(WPARAM, LPARAM)
{
	ApplyMainWindowIcons();
	TrayReset();
	return 0;
}


//////////////////////////////////////////////////////////////////
// Windows 7 GUI goodies

#ifdef HAVE_WIN7_SDK_H
// update thumbbarbutton structs and add/update the GUI thumbbar
void CemuleDlg::UpdateThumbBarButtons(bool initialAddToDlg)
{
	if (!m_pTaskbarList)
		return;

	THUMBBUTTONMASK dwMask = THB_ICON | THB_FLAGS;
	for (int i = TBB_FIRST; i <= TBB_LAST; ++i) {
		m_thbButtons[i].dwMask = dwMask;
		m_thbButtons[i].iId = i;
		m_thbButtons[i].iBitmap = 0;
		m_thbButtons[i].dwFlags = THBF_DISMISSONCLICK;

		LPCTSTR uid;
		switch (i) {
		case TBB_CONNECT:
			m_thbButtons[i].hIcon = theApp.LoadIcon(_T("CONNECT"), 16, 16);
			uid = _T("IRC_CONNECT");
			if (theApp.IsConnected())
				m_thbButtons[i].dwFlags |= THBF_DISABLED;
			break;
		case TBB_DISCONNECT:
			m_thbButtons[i].hIcon = theApp.LoadIcon(_T("DISCONNECT"), 16, 16);
			uid = _T("IRC_DISCONNECT");
			if (!theApp.IsConnected())
				m_thbButtons[i].dwFlags |= THBF_DISABLED;
			break;
		case TBB_THROTTLE:
			m_thbButtons[i].hIcon = theApp.LoadIcon(_T("SPEEDMIN"), 16, 16);
			uid = _T("PW_PA");
			break;
		case TBB_UNTHROTTLE:
			m_thbButtons[i].hIcon = theApp.LoadIcon(_T("SPEEDMAX"), 16, 16);
			uid = _T("PW_UA");
			break;
		case TBB_PREFERENCES:
			m_thbButtons[i].hIcon = theApp.LoadIcon(_T("PREFERENCES"), 16, 16);
			uid = _T("OPTIONS");
			break;
		default:
			uid = EMPTY;
		}
		// set tooltips in widechar
		if (uid) {
			const CString& tooltip(GetResNoAmp(uid));
			wcscpy(m_thbButtons[i].szTip, tooltip);
			m_thbButtons[i].dwMask |= THB_TOOLTIP;
		}
	}

	if (initialAddToDlg)
		m_pTaskbarList->ThumbBarAddButtons(m_hWnd, ARRAYSIZE(m_thbButtons), m_thbButtons);
	else
		m_pTaskbarList->ThumbBarUpdateButtons(m_hWnd, ARRAYSIZE(m_thbButtons), m_thbButtons);

	// clean up icons, they were copied in the previous call
	for (int i = TBB_FIRST; i <= TBB_LAST; ++i)
		::DestroyIcon(m_thbButtons[i].hIcon);
}

// Handle thumbbar buttons
void CemuleDlg::OnTBBPressed(UINT id)
{
	switch (id) {
	case TBB_CONNECT:
		OnBnClickedConnect();
		break;
	case TBB_DISCONNECT:
		CloseConnection();
		break;
	case TBB_THROTTLE:
		QuickSpeedOther(MP_QS_PA);
		break;
	case TBB_UNTHROTTLE:
		QuickSpeedOther(MP_QS_UA);
		break;
	case TBB_PREFERENCES:
		ShowPreferences();
	}
}

// When Windows tells us, the taskbar button was created, it is safe to initialize our taskbar stuff
LRESULT CemuleDlg::OnTaskbarBtnCreated(WPARAM, LPARAM)
{
	// Sanity check that the OS is Win 7 or later
	if (thePrefs.GetWindowsVersion() >= _WINVER_7_ && !theApp.IsClosing()) {
		if (m_pTaskbarList)
			m_pTaskbarList.Release();

		if (m_pTaskbarList.CoCreateInstance(CLSID_TaskbarList) == S_OK) {
			m_pTaskbarList->SetProgressState(m_hWnd, TBPF_NOPROGRESS);

			m_currentTBP_state = TBPF_NOPROGRESS;
			m_prevProgress = 0;
			m_ovlIcon = NULL;

			ApplyMainWindowIcons();
			UpdateThumbBarButtons(true);
			UpdateStatusBarProgress();
		} else
			ASSERT(0);
	}
	return 0;
}

// Updates global progress and /down state overlay icon
// Overlay icon looks rather annoying than useful, so it's disabled by default for the common user and can be enabled by ini setting only (Ornis)
void CemuleDlg::EnableTaskbarGoodies(bool enable)
{
	if (m_pTaskbarList) {
		m_pTaskbarList->SetOverlayIcon(m_hWnd, NULL, EMPTY);
		if (!enable) {
			m_pTaskbarList->SetProgressState(m_hWnd, TBPF_NOPROGRESS);
			m_currentTBP_state = TBPF_NOPROGRESS;
			m_prevProgress = 0;
			m_ovlIcon = NULL;
		} else
			UpdateStatusBarProgress();
	}
}

void CemuleDlg::UpdateStatusBarProgress()
{
	if (m_pTaskbarList && thePrefs.IsWin7TaskbarGoodiesEnabled()) {
		// calc global progress & status
		float finishedsize = theApp.emuledlg->transferwnd->GetDownloadList()->GetFinishedSize();
		float globalSize = theStats.m_fGlobalSize + finishedsize;

		if (globalSize == 0) {
			// if there is no download, disable progress
			if (m_currentTBP_state != TBPF_NOPROGRESS)
				m_currentTBP_state = TBPF_NOPROGRESS;
		} else {
			TBPFLAG new_state;
			if (theStats.m_dwOverallStatus & STATE_ERROROUS) // an error
				new_state = TBPF_ERROR;
			else if (theStats.m_dwOverallStatus & STATE_DOWNLOADING) // something downloading
				new_state = TBPF_NORMAL;
			else
				new_state = TBPF_PAUSED;

			if (new_state != m_currentTBP_state)
				m_currentTBP_state = new_state;

			float globalDone = theStats.m_fGlobalDone + finishedsize;
			float overallProgress = globalDone / globalSize;
			if (overallProgress != m_prevProgress) {
				m_prevProgress = overallProgress;
				m_pTaskbarList->SetProgressValue(m_hWnd, (ULONGLONG)(overallProgress * 100), 100);
			}
		}
		m_pTaskbarList->SetProgressState(m_hWnd, m_currentTBP_state);

		// overlay up/down-speed
		if (thePrefs.IsShowUpDownIconInTaskbar()) {
			bool bUp = theApp.emuledlg->transferwnd->GetUploadList()->GetItemCount() > 0;
			bool bDown = theStats.m_dwOverallStatus & STATE_DOWNLOADING;

			HICON newicon;
			if (bUp && bDown)
				newicon = transicons[3];
			else if (bUp)
				newicon = transicons[2];
			else if (bDown)
				newicon = transicons[1];
			else
				newicon = NULL;

			if (m_ovlIcon != newicon) {
				m_ovlIcon = newicon;
				m_pTaskbarList->SetOverlayIcon(m_hWnd, m_ovlIcon, _T("eMule Up/Down Indicator"));
			}
		}
	}
}
#endif

void CemuleDlg::SetTaskbarIconColor()
{
	bool bBrightTaskbarIconSpeed = false;
	bool bTransparent = false;
	COLORREF cr = RGB(0, 0, 0);
	if (thePrefs.IsDwmCompositionEnabled()) {
		HMODULE hDWMAPI = LoadLibrary(_T("dwmapi.dll"));
		if (hDWMAPI) {
			HRESULT(WINAPI * pfnDwmGetColorizationColor)(DWORD*, BOOL*);
			(FARPROC&)pfnDwmGetColorizationColor = GetProcAddress(hDWMAPI, "DwmGetColorizationColor");
			if (pfnDwmGetColorizationColor != NULL) {
				DWORD dwGlassColor;
				BOOL bOpaque;
				if (pfnDwmGetColorizationColor(&dwGlassColor, &bOpaque) == S_OK) {
					uint8 byAlpha = (uint8)(dwGlassColor >> 24);
					cr = 0xFFFFFF & dwGlassColor;
					if (byAlpha < 200 && !bOpaque) {
						// on transparent themes we can never figure out what exact color is shown
						// (if we could in real time?), but given that a color is blended against
						// the background, it is a good guess that a bright speedbar will be
						// the best solution in most cases
						bTransparent = true;
					}
				}
			}
			FreeLibrary(hDWMAPI);
		}
	} else {
		DEBUG_ONLY(DebugLog(_T("Taskbar Notifier Color: GetCustomSysColor() used")));
		cr = GetCustomSysColor(COLOR_3DFACE);
	}
	uint8 iRed = GetRValue(cr);
	uint8 iBlue = GetBValue(cr);
	uint8 iGreen = GetGValue(cr);
	const float fRed = static_cast<float>(iRed);
	const float fGreen = static_cast<float>(iGreen);
	const float fBlue = static_cast<float>(iBlue);
	uint16 iBrightness = (uint16)sqrt(((fRed * fRed * 0.241f) + (fGreen * fGreen * 0.691f) + (fBlue * fBlue * 0.068f)));
	ASSERT(iBrightness <= 255);
	bBrightTaskbarIconSpeed = iBrightness < 132;
	DebugLog(_T("Taskbar Notifier Color: R:%u G:%u B:%u, Brightness: %u, Transparent: %s"), iRed, iGreen, iBlue, iBrightness, bTransparent ? _T("Yes") : _T("No"));
	thePrefs.SetStatsColor(11, ((bBrightTaskbarIconSpeed || bTransparent) ? RGB(255, 255, 255) : RGB(0, 0, 0)));
}
LRESULT CemuleDlg::OnConChecker(WPARAM wParam, LPARAM lParam)
{
	if (IsInitializing())
		return 0;

	if (theApp.IsClosing() || theApp.serverconnect == NULL)
		return 0;

	if (theApp.ConChecker == NULL || !theApp.ConChecker->IsWorkerGenerationActive(static_cast<LONG>(wParam)))
		return 0;

	{ // LPARAM = status
		switch (lParam)
		{
			case CONSTATE_ONLINE:
				if (theApp.GetConnectionState() != CONSTATE_ONLINE)	{
					const bool bShouldStartConnection = thePrefs.DoAutoConnect()
						|| bPrevKadState
						|| bPrevEd2kState
						|| m_bKadSuspendDisconnect
						|| m_bEd2kSuspendDisconnect;

					AddLogLine(true, GetResString(_T("CONN_ONLINE")));
					theApp.SetConnectionState(CONSTATE_ONLINE);
					if (bShouldStartConnection)
						StartConnection(); // Restore auto-connect or previously active connections after connectivity returns.

					if (bPrevKadState && !Kademlia::CKademlia::IsConnected()) // If auto connect to KAD option isn't active, but KAD was manually connected
						Kademlia::CKademlia::Start();

				if (bPrevEd2kState && (!theApp.serverconnect->IsConnecting() && !theApp.serverconnect->IsConnected()))  // If auto connect to eD2K option isn't active, but eD2K server was manually connected
					theApp.serverconnect->ConnectToAnyServer();
			}
			break;
		case CONSTATE_OFFLINE:
			if (theApp.GetConnectionState() != CONSTATE_OFFLINE) {
				AddLogLine(true, GetResString(_T("CONN_OFFLINE")));
				bPrevKadState = Kademlia::CKademlia::IsConnected();
				bPrevEd2kState = theApp.serverconnect->IsConnecting() || theApp.serverconnect->IsConnected();
				CloseConnection(); // no need to keep the connection 'alive'... or even try!
				theApp.SetConnectionState(CONSTATE_OFFLINE);
			}
			break;
		default:
			AddLogLine(false, GetResString(_T("CONN_UNKNOWN")));
			theApp.SetConnectionState(CONSTATE_NULL);
			break;
		}
	}
	return 0;
}

void CemuleDlg::ShowSpeedGraph(bool bShow)
{
	if (bShow) {
		if (toolbar != NULL && ::IsWindow(toolbar->GetSafeHwnd()))
			toolbar->Refresh();
		ResizeSpeedGraph();
		if (::IsWindow(m_UpSpeedGraph.GetSafeHwnd()) && ::IsWindow(m_DownSpeedGraph.GetSafeHwnd()))
			SetSpeedGraphLimits();
	} else {
		CWnd* const pParent = (toolbar != NULL && ::IsWindow(toolbar->GetSafeHwnd())) ? static_cast<CWnd*>(toolbar) : static_cast<CWnd*>(this);
		CRect rInvalidateUp, rInvalidateDown;
		if (::IsWindow(m_UpSpeedGraph.GetSafeHwnd())) {
			m_UpSpeedGraph.GetWindowRect(&rInvalidateUp);
			m_UpSpeedGraph.ShowWindow(SW_HIDE);
			m_UpSpeedGraph.EnableWindow(false);
			pParent->ScreenToClient(rInvalidateUp);
			pParent->InvalidateRect(rInvalidateUp, FALSE);
		}
		if (::IsWindow(m_DownSpeedGraph.GetSafeHwnd())) {
			m_DownSpeedGraph.GetWindowRect(&rInvalidateDown);
			m_DownSpeedGraph.ShowWindow(SW_HIDE);
			m_DownSpeedGraph.EnableWindow(false);
			pParent->ScreenToClient(rInvalidateDown);
			pParent->InvalidateRect(rInvalidateDown, FALSE);
		}
	}
}

void CemuleDlg::SetSpeedGraphLimits()
{
	if (!::IsWindow(m_UpSpeedGraph.GetSafeHwnd()) || !::IsWindow(m_DownSpeedGraph.GetSafeHwnd()))
		return;

	UINT uUploadDatarate = 0;
	UINT uDownloadDatarate = 0;
	theApp.GetDisplayedTransferRates(uUploadDatarate, uDownloadDatarate);
	m_UpSpeedGraph.Set_TrafficValue(uUploadDatarate);
	m_DownSpeedGraph.Set_TrafficValue(uDownloadDatarate);
}

void CemuleDlg::ResizeSpeedGraph()
{
	const int kSpeedGraphWidth = 265;
	const int kSpeedGraphMargin = 2;
	const int kSpeedGraphMinGap = 4;

	if (toolbar == NULL || !::IsWindow(toolbar->GetSafeHwnd()))
		return;

	if (!thePrefs.GetUITweaksSpeedGraph()) {
		if (::IsWindow(m_UpSpeedGraph.GetSafeHwnd()))
			m_UpSpeedGraph.ShowWindow(SW_HIDE);
		if (::IsWindow(m_DownSpeedGraph.GetSafeHwnd()))
			m_DownSpeedGraph.ShowWindow(SW_HIDE);
		return;
	}

	CRect rect;
	CRect rect1, rect2;

	toolbar->GetClientRect(&rect);
	if (rect.Width() <= kSpeedGraphWidth + kSpeedGraphMargin || rect.Height() <= 4) {
		if (::IsWindow(m_UpSpeedGraph.GetSafeHwnd()))
			m_UpSpeedGraph.ShowWindow(SW_HIDE);
		if (::IsWindow(m_DownSpeedGraph.GetSafeHwnd()))
			m_DownSpeedGraph.ShowWindow(SW_HIDE);
		return;
	}

	const int iGraphRight = rect.right - kSpeedGraphMargin;
	const int iGraphLeft = iGraphRight - kSpeedGraphWidth;

	// Set update interval of graphic rate display.
	rect1.top = rect.top + 2;
	rect1.right = iGraphRight;
	rect1.bottom = rect.top + (rect.Height() / 2) - 1;
	rect1.left = iGraphLeft;

	rect2.top = rect.top + (rect.Height() / 2) + 1;
	rect2.right = iGraphRight;
	rect2.bottom = rect.bottom - 2;
	rect2.left = iGraphLeft;

	if (!::IsWindow(m_UpSpeedGraph.GetSafeHwnd())) {
		if (!m_UpSpeedGraph.Create(IDD_SPEEDGRAPH, rect1, toolbar))
			return;
		m_UpSpeedGraph.Init_Graph(_T("Up"), thePrefs.GetMaxGraphUploadRate(true));
	}

	if (!::IsWindow(m_DownSpeedGraph.GetSafeHwnd())) {
		if (!m_DownSpeedGraph.Create(IDD_SPEEDGRAPH, rect2, toolbar))
			return;
		m_DownSpeedGraph.Init_Graph(_T("Down"), thePrefs.GetMaxGraphDownloadRate());
	}

	const int iVisibleToolbarRight = GetVisibleToolbarContentRightEdge(*toolbar);
	if (iVisibleToolbarRight + kSpeedGraphMinGap > iGraphLeft) {
		// Keep graphs initialized even while they are hidden.
		m_UpSpeedGraph.ShowWindow(SW_HIDE);
		m_DownSpeedGraph.ShowWindow(SW_HIDE);
		return;
	}

	m_UpSpeedGraph.EnableWindow(true);
	m_DownSpeedGraph.EnableWindow(true);
	m_UpSpeedGraph.SetWindowPos(&CWnd::wndTop, rect1.left, rect1.top, rect1.Width(), rect1.Height(), SWP_NOACTIVATE | SWP_SHOWWINDOW);
	m_DownSpeedGraph.SetWindowPos(&CWnd::wndTop, rect2.left, rect2.top, rect2.Width(), rect2.Height(), SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

bool CemuleDlg::PostSharedFileListFoundFilesAsync()
{
	if (theApp.IsClosing() || m_hWnd == NULL || !::IsWindow(m_hWnd))
		return false;

	if (InterlockedCompareExchange(&m_lSharedFileListFoundFilesPendingMessage, 1, 0) != 0)
		return true;

	if (!PostMessage(TM_SHAREDFILELISTFOUNDFILES, 0, 0)) {
		InterlockedExchange(&m_lSharedFileListFoundFilesPendingMessage, 0);
		return false;
	}
	return true;
}

bool CemuleDlg::PostSharedFilesCtrlUpdateFileAsync(CKnownFile* pFile)
{
	if (pFile == NULL || theApp.IsClosing() || m_hWnd == NULL || !::IsWindow(m_hWnd))
		return false;

	bool bPostMessage = false;
	{
		CSingleLock lock(&m_sharedFilesCtrlUpdateLock, TRUE);
		m_pendingSharedFilesCtrlUpdateFiles.insert(pFile);
		bPostMessage = InterlockedCompareExchange(&m_lSharedFilesCtrlUpdatePendingMessage, 1, 0) == 0;
	}

	if (bPostMessage && !PostMessage(TM_SHAREDFILESCTRLUPDATEFILE, 0, 0)) {
		CSingleLock lock(&m_sharedFilesCtrlUpdateLock, TRUE);
		m_pendingSharedFilesCtrlUpdateFiles.clear();
		InterlockedExchange(&m_lSharedFilesCtrlUpdatePendingMessage, 0);
		return false;
	}
	return true;
}

bool CemuleDlg::PostFileOpProgressAsync(CKnownFile* pFile, WPARAM uProgress)
{
	if (pFile == NULL || theApp.IsClosing() || m_hWnd == NULL || !::IsWindow(m_hWnd))
		return false;

	bool bPostMessage = false;
	{
		CSingleLock lock(&m_fileOpProgressLock, TRUE);
		m_pendingFileOpProgress[pFile] = uProgress;
		bPostMessage = InterlockedCompareExchange(&m_lFileOpProgressPendingMessage, 1, 0) == 0;
	}

	if (bPostMessage && !PostMessage(TM_FILEOPPROGRESS, 0, 0)) {
		CSingleLock lock(&m_fileOpProgressLock, TRUE);
		m_pendingFileOpProgress.clear();
		m_pendingPartFileOpProgress.clear();
		InterlockedExchange(&m_lFileOpProgressPendingMessage, 0);
		return false;
	}
	return true;
}

bool CemuleDlg::PostPartFileOpProgressAsync(CPartFile* pFile, DWORD dwRuntimeID, const uchar* pucFileHash, WPARAM uProgress)
{
	if (pFile == NULL || dwRuntimeID == 0 || pucFileHash == NULL || theApp.IsClosing() || m_hWnd == NULL || !::IsWindow(m_hWnd))
		return false;

	SPartFileOpProgressKey key = {};
	key.pPartFile = pFile;
	key.dwRuntimeID = dwRuntimeID;
	md4cpy(key.abyFileHash, pucFileHash);

	bool bPostMessage = false;
	{
		CSingleLock lock(&m_fileOpProgressLock, TRUE);
		m_pendingPartFileOpProgress[key] = uProgress;
		bPostMessage = InterlockedCompareExchange(&m_lFileOpProgressPendingMessage, 1, 0) == 0;
	}

	if (bPostMessage && !PostMessage(TM_FILEOPPROGRESS, 0, 0)) {
		CSingleLock lock(&m_fileOpProgressLock, TRUE);
		m_pendingFileOpProgress.clear();
		m_pendingPartFileOpProgress.clear();
		InterlockedExchange(&m_lFileOpProgressPendingMessage, 0);
		return false;
	}
	return true;
}

LRESULT CemuleDlg::OnSharedFileListFoundFiles(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	InterlockedExchange(&m_lSharedFileListFoundFilesPendingMessage, 0);
	if (theApp.IsClosing())
		return 0;

	if (theApp.sharedfiles != NULL)
		theApp.sharedfiles->OnSharedFilesFound();

	return 0;
}

LRESULT CemuleDlg::OnSharedFilesCtrlUpdateFile(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	InterlockedExchange(&m_lSharedFilesCtrlUpdatePendingMessage, 0);

	std::set<CKnownFile*> pendingFiles;
	{
		CSingleLock lock(&m_sharedFilesCtrlUpdateLock, TRUE);
		pendingFiles.swap(m_pendingSharedFilesCtrlUpdateFiles);
	}

	if (theApp.IsClosing())
		return 0;

	if (sharedfileswnd) {
		for (std::set<CKnownFile*>::const_iterator it = pendingFiles.begin(); it != pendingFiles.end(); ++it)
			sharedfileswnd->sharedfilesctrl.UpdateFile(*it);
	}

	return 0;
}
