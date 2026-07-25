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
#include "emule.h"
#include "emuledlg.h"
#include "SharedFilesCtrl.h"
#include "KnownFileList.h"
#include "DownloadQueue.h" 
#include "UpDownClient.h"
#include "FileInfoDialog.h"
#include "MetaDataDlg.h"
#include "ED2kLinkDlg.h"
#include "ArchivePreviewDlg.h"
#include "CommentDialog.h"
#include "HighColorTab.hpp"
#include "ListViewWalkerPropertySheet.h"
#include "UserMsgs.h"
#include "ResizableLib/ResizableSheet.h"
#include "KnownFile.h"
#include "MapKey.h"
#include "SharedFileList.h"
#include "MemDC.h"
#include "PartFile.h"
#include "Preview.h"
#include "MenuCmds.h"
#include "IrcWnd.h"
#include "SharedFilesWnd.h"
#include "Opcodes.h"
#include "InputBox.h"
#include "WebServices.h"
#include "TransferDlg.h"
#include "ClientList.h"
#include "Collection.h"
#include "CollectionCreateDialog.h"
#include "CollectionViewDialog.h"
#include "SearchParams.h"
#include "SearchDlg.h"
#include "SearchResultsWnd.h"
#include "ToolTipCtrlX.h"
#include "kademlia/kademlia/kademlia.h"
#include "kademlia/kademlia/UDPFirewallTester.h"
#include "MediaInfo.h"
#include "Log.h"
#include "OtherFunctions.h"
#include "KnownFileList.h"
#include "ListViewSearchDlg.h"
#include <algorithm>
#include <set>
#include "MuleStatusBarCtrl.h"
#include "TransferDlg.h"
#include "eMuleAI/DarkMode.h"

#ifndef LVS_EX_DOUBLEBUFFER
#define LVS_EX_DOUBLEBUFFER 0x00010000
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

LPARAM CSharedFilesCtrl::m_pSortParam = NULL ;

namespace
{
	const EListStateField kSharedFilesViewState = static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL);
	const DWORD kSharedFilesSetItemCountFlags = LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL;
	const int kSharedFilesLargeHistoryContextMenuSelection = BULK_OPERATION_MIN_ITEMS;
	const int kSharedFilesColumnPermission = 20;
	const int kSharedFilesColumnPowershare = 21;

	struct SSharedFilesHashKey
	{
		SSharedFilesHashKey(const uchar* pHash = NULL)
		{
			if (pHash != NULL)
				md4cpy(abyHash, pHash);
			else
				md4clr(abyHash);
		}

		uchar abyHash[16];
	};

	struct SSharedFilesHashKeyLess
	{
		bool operator()(const SSharedFilesHashKey& lhs, const SSharedFilesHashKey& rhs) const
		{
			return memcmp(lhs.abyHash, rhs.abyHash, sizeof(lhs.abyHash)) < 0;
		}
	};
	const UINT kSharedFilesCommandToggleShareStatus = static_cast<UINT>(-1);
	const UINT UM_SHARED_FILESCTRL_FILESYSTEM_RELOAD_READY = WM_APP + 0x5A1;
	const UINT UM_SHARED_FILESCTRL_PROCESS_BULK_OPERATION = WM_APP + 0x5A2;
	const size_t kSharedFilesLargeListRows = 35000;
	const UINT_PTR TimerSharedFilesBulkOperation = 0x7E22;

	UINT ClampSharedFilesHashingCount(INT_PTR iHashingCount)
	{
		if (iHashingCount <= 0)
			return 0;
		if (static_cast<ULONGLONG>(iHashingCount) > UINT_MAX)
			return UINT_MAX;
		return static_cast<UINT>(iHashingCount);
	}


	bool TryCopyShareableFileString(const CShareableFile* pFile, const CString& (CShareableFile::*pfnGetter)() const, CString& rstrValue, int iMaxChars)
	{
		if (pFile == NULL) {
			rstrValue.Empty();
			return false;
		}
		rstrValue = (pFile->*pfnGetter)();
		if (iMaxChars > 0 && rstrValue.GetLength() > iMaxChars)
			rstrValue = rstrValue.Left(iMaxChars);
		return true;
	}

	bool TryCopyAbstractFileString(const CAbstractFile* pFile, const CString& (CAbstractFile::*pfnGetter)() const, CString& rstrValue, int iMaxChars)
	{
		if (pFile == NULL) {
			rstrValue.Empty();
			return false;
		}
		rstrValue = (pFile->*pfnGetter)();
		if (iMaxChars > 0 && rstrValue.GetLength() > iMaxChars)
			rstrValue = rstrValue.Left(iMaxChars);
		return true;
	}

	bool TryCopyShareableSharedDirectory(const CShareableFile* pFile, CString& rstrValue)
	{
		if (pFile == NULL) {
			rstrValue.Empty();
			return false;
		}
		rstrValue = pFile->GetSharedDirectory();
		return true;
	}

	bool TryGetShareableFileSize(const CShareableFile* pFile, uint64& ruFileSize)
	{
		ruFileSize = 0;
		if (pFile == NULL)
			return false;
		ruFileSize = pFile->GetFileSize();
		return true;
	}

	bool TryCopyShareableFileHash(const CShareableFile* pFile, uchar* paucHash, size_t uHashSize)
	{
		if (paucHash == NULL || uHashSize < MDX_DIGEST_SIZE)
			return false;
		memset(paucHash, 0, uHashSize);
		if (pFile == NULL)
			return false;
		const uchar* pHash = pFile->GetFileHash();
		if (pHash == NULL)
			return false;
		memcpy(paucHash, pHash, MDX_DIGEST_SIZE);
		return true;
	}

	bool TryIsSharedFileKindOf(const CObject* pObject, CRuntimeClass* pRuntimeClass, bool& rbIsKindOf)
	{
		rbIsKindOf = false;
		if (pObject == NULL || pRuntimeClass == NULL)
			return false;
		rbIsKindOf = pObject->IsKindOf(pRuntimeClass) != FALSE;
		return true;
	}

	bool TryIsShareableFileShellLinked(const CShareableFile* pFile, bool& rbShellLinked)
	{
		rbShellLinked = false;
		if (pFile == NULL)
			return false;
		rbShellLinked = pFile->IsShellLinked() != FALSE;
		return true;
	}

	DWORD GetRecentSharedFilesBulkInputAgeMs(DWORD dwNow)
	{
		LASTINPUTINFO lastInput;
		memset(&lastInput, 0, sizeof(lastInput));
		lastInput.cbSize = sizeof(lastInput);
		return ::GetLastInputInfo(&lastInput) ? static_cast<DWORD>(dwNow - lastInput.dwTime) : static_cast<DWORD>(-1);
	}

	void GetSharedFilesBulkSliceLimits(DWORD &dwSliceBudgetMs, UINT &uMaxItemsPerSlice)
	{
		const DWORD dwNow = ::GetTickCount();
		const UINT uQueueStatus = HIWORD(::GetQueueStatus(QS_KEY | QS_MOUSE | QS_PAINT | QS_TIMER | QS_POSTMESSAGE));
		const bool bInputPending = (uQueueStatus & (QS_KEY | QS_MOUSE)) != 0;
		const bool bPaintPending = (uQueueStatus & QS_PAINT) != 0;
		const bool bDispatchPending = (uQueueStatus & (QS_TIMER | QS_POSTMESSAGE)) != 0;
		const DWORD dwInputAge = GetRecentSharedFilesBulkInputAgeMs(dwNow);

		if (bInputPending || dwInputAge < 250) {
			dwSliceBudgetMs = 3;
			uMaxItemsPerSlice = 32;
			return;
		}
		if (bPaintPending || bDispatchPending) {
			dwSliceBudgetMs = 5;
			uMaxItemsPerSlice = 96;
			return;
		}
		if (dwInputAge < 1000) {
			dwSliceBudgetMs = 8;
			uMaxItemsPerSlice = 192;
			return;
		}

		dwSliceBudgetMs = 16;
		uMaxItemsPerSlice = 512;
	}

	struct SSharedFilesFileSystemEntry
	{
		CString strFilePath;
		CString strFileName;
		CString strDirectory;
		ULONGLONG ullFileSize;
		FILETIME tLastWriteTime;
		bool bHasLastWriteTime;
	};

	struct SSharedFilesFileSystemReloadResult
	{
		HWND hWnd;
		LONG lGeneration;
		uint64 uReloadToken;
		CString strDirectory;
		DWORD dwLastError;
		std::vector<SSharedFilesFileSystemEntry> vecEntries;

		SSharedFilesFileSystemReloadResult()
			: hWnd(NULL)
			, lGeneration(0)
			, uReloadToken(0)
			, dwLastError(ERROR_SUCCESS)
		{
		}
	};

	void DrainFileSystemReloadMessages(HWND hWnd)
	{
		if (hWnd == NULL)
			return;

		MSG msg;
		while (::PeekMessage(&msg, hWnd, UM_SHARED_FILESCTRL_FILESYSTEM_RELOAD_READY, UM_SHARED_FILESCTRL_FILESYSTEM_RELOAD_READY, PM_REMOVE))
			delete reinterpret_cast<SSharedFilesFileSystemReloadResult*>(msg.lParam);
	}

	bool ShouldSkipFileSystemEntry(const WIN32_FIND_DATA& wfd, const CString& strFilePath)
	{
		if ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 || (wfd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0 || (wfd.dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY) != 0)
			return true;

		const ULONGLONG ullFileSize = (static_cast<ULONGLONG>(wfd.nFileSizeHigh) << 32) | wfd.nFileSizeLow;
		if (ullFileSize == 0 || ullFileSize > MAX_EMULE_FILE_SIZE)
			return true;

		CString strFileName(wfd.cFileName);
		if (ExtensionIs(strFileName, _T(".lnk"))) {
			SHFILEINFO info;
			if (::SHGetFileInfo(strFilePath, 0, &info, sizeof info, SHGFI_ATTRIBUTES) && (info.dwAttributes & SFGAO_LINK))
				return true;
		}

		return IsThumbsDb(strFilePath, strFileName);
	}

	void BuildSharedFilesFileSystemReloadResult(SSharedFilesFileSystemReloadResult* pResult)
	{
		if (pResult == NULL)
			return;

		CString strDirectory(pResult->strDirectory);
		PathAddBackslash(strDirectory.GetBuffer(strDirectory.GetLength() + 1));
		strDirectory.ReleaseBuffer();

		CString strSearchPath(strDirectory);
		strSearchPath += _T("*");
		const CString strPreparedSearchPath(PreparePathForWin32LongPath(strSearchPath));

		WIN32_FIND_DATA wfd = {0};
		HANDLE hFind = FindFirstFileExW(strPreparedSearchPath, FindExInfoBasic, &wfd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
		if (hFind == INVALID_HANDLE_VALUE) {
			pResult->dwLastError = ::GetLastError();
		} else {
			do {
				const CString strFileName(wfd.cFileName);
				if (strFileName == _T(".") || strFileName == _T(".."))
					continue;

				CString strFilePath(strDirectory);
				strFilePath += strFileName;
				if (ShouldSkipFileSystemEntry(wfd, strFilePath))
					continue;

				SSharedFilesFileSystemEntry entry;
				entry.strFilePath = strFilePath;
				entry.strFileName = strFileName;
				entry.strDirectory = strDirectory;
				entry.ullFileSize = (static_cast<ULONGLONG>(wfd.nFileSizeHigh) << 32) | wfd.nFileSizeLow;
				entry.tLastWriteTime = wfd.ftLastWriteTime;
				entry.bHasLastWriteTime = true;
				pResult->vecEntries.push_back(entry);
			} while (FindNextFileW(hFind, &wfd));
			FindClose(hFind);
		}
	}
	CString MakeSharedFilesCommandKey(const CKnownFile *pFile)
	{
		if (pFile == NULL)
			return CString();
		uchar aucFileHash[16] = {0};
		if (!TryCopyShareableFileHash(pFile, aucFileHash, sizeof(aucFileHash)) || isnulmd4(aucFileHash))
			return CString();
		CString strFilePath;
		if (!TryCopyShareableFileString(pFile, &CShareableFile::GetFilePath, strFilePath, MAX_PATH * 8))
			return CString();
		CString strKey(md4str(aucFileHash));
		strKey += _T("\t");
		strKey += strFilePath;
		return strKey;
	}


	bool IsSharedFilesCommandKeyMatch(const CKnownFile *pFile, const CString &strCommandKey)
	{
		if (pFile == NULL || strCommandKey.IsEmpty())
			return false;
		if (MakeSharedFilesCommandKey(pFile) == strCommandKey)
			return true;
		if (strCommandKey.Find(_T('\t')) >= 0)
			return false;
		uchar aucFileHash[16] = {0};
		if (!TryCopyShareableFileHash(pFile, aucFileHash, sizeof(aucFileHash)) || isnulmd4(aucFileHash))
			return false;
		return md4str(aucFileHash).CompareNoCase(strCommandKey) == 0;
	}
	const int kSharedFilesColumnSpreadbarHistory = 22;
	const int kSharedFilesColumnHideOverShare = 23;
	const int kSharedFilesColumnShareOnlyTheNeed = 24;
	const int kSharedFilesColumnLastRequest = 25;

	typedef CMap<CString, LPCTSTR, CShareableFile*, CShareableFile*> CTempShareableFilesMap;
	typedef CMap<CShareableFile*, CShareableFile*, ULONGLONG, ULONGLONG> CTempShareableFileWriteTimeMap;

	CTempShareableFileWriteTimeMap g_mapTempShareableFileWriteTimes;


	void RebuildPreviewMenu(CMenuXP& menu, const CPartFile* file, bool bEnablePreview, bool bEnablePauseOnPreview, bool bPauseOnPreviewChecked, bool bEnablePreviewParts, bool bPreviewPartsChecked)
	{
		while (menu.GetMenuItemCount() > 0)
			menu.RemoveMenu(0, MF_BYPOSITION);

		CString strPrimaryCommand = thePrefs.GetVideoPlayer();
		if (file != NULL) {
			const int iPreviewApp = thePreviewApps.GetPreviewApp(file);
			if (iPreviewApp >= 0)
				strPrimaryCommand = thePreviewApps.GetPreviewAppCmd(iPreviewApp);
		}

		const CString strPrimaryLabel = thePreviewApps.GetPreviewAppDisplayNameByCommand(strPrimaryCommand);
		menu.AppendODMenu(MF_STRING | (bEnablePreview ? MF_ENABLED : MF_GRAYED), MP_PREVIEW, new CMenuXPText(MP_PREVIEW, strPrimaryLabel.IsEmpty() ? GetResStringWithAccel(_T("PREVIEW_AVAILABLE"), _T('v')) : strPrimaryLabel, thePreviewApps.GetPreviewCommandIcon(strPrimaryCommand)));
		thePreviewApps.GetAllMenuEntries(menu, file, strPrimaryCommand);
		menu.AppendMenu(MF_SEPARATOR);
		if (!thePrefs.GetPreviewPrio()) {
			menu.AppendMenu(MF_STRING | (bEnablePreviewParts ? MF_ENABLED : MF_GRAYED), MP_TRY_TO_GET_PREVIEW_PARTS, GetResString(_T("DL_TRY_TO_GET_PREVIEW_PARTS")));
			menu.CheckMenuItem(MP_TRY_TO_GET_PREVIEW_PARTS, bPreviewPartsChecked ? MF_CHECKED : MF_UNCHECKED);
		}
		menu.AppendMenu(MF_STRING | (bEnablePauseOnPreview ? MF_ENABLED : MF_GRAYED), MP_PAUSEONPREVIEW, GetResString(_T("PAUSEONPREVIEW")));
		menu.CheckMenuItem(MP_PAUSEONPREVIEW, bPauseOnPreviewChecked ? MF_CHECKED : MF_UNCHECKED);
	}

	UINT GetSharePermissionMenuItem(const CKnownFile* pFile)
	{
		if (pFile == NULL)
			return 0;

		switch (pFile->GetPermissions()) {
		case -1:
			return MP_PERMDEFAULT;
		case PERM_ALL:
			return MP_PERMALL;
		case PERM_FRIENDS:
			return MP_PERMFRIENDS;
		case PERM_NOONE:
			return MP_PERMNONE;
		default:
			ASSERT(false);
			return 0;
		}
	}

	UINT GetPowerShareMenuItem(const CKnownFile* pFile)
	{
		if (pFile == NULL)
			return 0;

		switch (pFile->GetPowerSharedMode()) {
		case -1:
			return MP_POWERSHARE_DEFAULT;
		case 0:
			return MP_POWERSHARE_OFF;
		case 1:
			return MP_POWERSHARE_ON;
		case 2:
			return MP_POWERSHARE_AUTO;
		case 3:
			return MP_POWERSHARE_LIMITED;
		default:
			ASSERT(false);
			return 0;
		}
	}

	UINT GetToggleMenuItem(const int iValue, const UINT uDefaultItem, const UINT uDisabledItem, const UINT uEnabledItem)
	{
		switch (iValue) {
		case -1:
			return uDefaultItem;
		case 0:
			return uDisabledItem;
		case 1:
			return uEnabledItem;
		default:
			ASSERT(false);
			return 0;
		}
	}

	CString GetEnabledDisabledLabel(const bool bEnabled)
	{
		return GetResString(bEnabled ? _T("ENABLED") : _T("DISABLED"));
	}

	CString GetDefaultShortLabel()
	{
		CString strDefault(GetResString(_T("DEFAULT")));
		return strDefault.IsEmpty() ? CString(_T("D")) : strDefault.Left(1);
	}

	CString BuildDefaultScopedLabel(const CString& strLabel)
	{
		CString strResult(GetDefaultShortLabel());
		strResult.Append(_T(". "));
		strResult.Append(strLabel);
		return strResult;
	}

	CString GetSharePermissionLabel(const int iPermission)
	{
		switch (iPermission) {
		case PERM_ALL:
			return GetResString(_T("PW_EVER"));
		case PERM_FRIENDS:
			return GetResString(_T("FSTATUS_FRIENDSONLY"));
		case PERM_NOONE:
			return GetResString(_T("SHARE_PERMISSION_HIDDEN"));
		default:
			return CString();
		}
	}

	int CompareIntValues(const int iLeft, const int iRight)
	{
		return (iLeft < iRight) ? -1 : static_cast<int>(iLeft > iRight);
	}

	bool IsSpreadbarEnabledForFile(const CKnownFile* pFile)
	{
		return pFile != NULL && (pFile->GetSpreadbarSetStatus() > 0 || (pFile->GetSpreadbarSetStatus() < 0 && thePrefs.GetSpreadbarSetStatus()));
	}

	int GetEffectivePermission(const CKnownFile* pFile)
	{
		if (pFile == NULL)
			return PERM_ALL;

		return (pFile->GetPermissions() >= 0) ? pFile->GetPermissions() : thePrefs.GetSharePermissions();
	}

	bool ShouldSuppressShareManagementColumns(const CKnownFile* pFile)
	{
		if (pFile == NULL)
			return false;

		return pFile->IsPartFile();
	}

	bool ShouldSuppressShareManagementRowColor(const CKnownFile* pFile, const FilterType eFilter)
	{
		if (pFile == NULL)
			return false;

		if (pFile->IsPartFile())
			return true;

		return eFilter == FilterType::Duplicate || theApp.knownfiles->DuplicatesCount(pFile->GetFileHash()) > 0;
	}

	bool ShouldShowLastRequestForSharedFile(const CKnownFile* pFile)
	{
		return pFile != NULL
			&& !pFile->IsPartFile()
			&& !pFile->GetFilePath().IsEmpty()
			&& theApp.sharedfiles != NULL
			&& theApp.sharedfiles->GetFileByID(pFile->GetFileHash()) == pFile;
	}

	int CompareLastRequestTime(time_t tLeft, bool bShowLeft, time_t tRight, bool bShowRight, bool bSortAscending)
	{
		const bool bDefinedLeft = bShowLeft && tLeft > 0;
		const bool bDefinedRight = bShowRight && tRight > 0;
		if (!bDefinedLeft) {
			if (!bDefinedRight)
				return 0;
			return bSortAscending ? 1 : -1;
		}
		if (!bDefinedRight)
			return bSortAscending ? -1 : 1;
		return (tLeft < tRight) ? -1 : static_cast<int>(tLeft > tRight);
	}

	CString BuildSharePermissionColumnText(const CKnownFile* pFile)
	{
		if (pFile == NULL)
			return CString();

		CString strPermission(GetSharePermissionLabel(GetEffectivePermission(pFile)));
		return (pFile->GetPermissions() < 0) ? BuildDefaultScopedLabel(strPermission) : strPermission;
	}

	CString BuildPriorityColumnText(const CKnownFile* pFile)
	{
		if (pFile == NULL)
			return CString();

		CString strText;
		if (pFile->GetPowerShared())
			strText.Format(_T("%s %s"), (LPCTSTR)GetResString(_T("POWERSHARE_PREFIX")), (LPCTSTR)pFile->GetUpPriorityDisplayString());
		else
			strText = pFile->GetUpPriorityDisplayString();
		return strText;
	}

	void UpdateSharePermissionMenuChecks(CMenu& menu, UINT uCheckedItem)
	{
		static const UINT s_auPermissionMenuItems[] = { MP_PERMDEFAULT, MP_PERMNONE, MP_PERMFRIENDS, MP_PERMALL };
		for (size_t i = 0; i < _countof(s_auPermissionMenuItems); ++i)
			menu.CheckMenuItem(s_auPermissionMenuItems[i], MF_BYCOMMAND | ((s_auPermissionMenuItems[i] == uCheckedItem) ? MF_CHECKED : MF_UNCHECKED));
	}

	CString GetPowerShareModeLabel(const int iMode)
	{
		switch (iMode) {
		case 0:
			return GetResString(_T("DISABLED"));
		case 1:
			return GetResString(_T("POWERSHARE_ACTIVATED"));
		case 2:
			return GetResString(_T("PRIOAUTO"));
		case 3:
			return GetResString(_T("POWERSHARE_LIMITED"));
		default:
			return CString();
		}
	}

	int GetEffectivePowerShareMode(const CKnownFile* pFile)
	{
		if (pFile == NULL)
			return 0;

		return (pFile->GetPowerSharedMode() >= 0) ? pFile->GetPowerSharedMode() : thePrefs.GetPowerShareMode();
	}

	CString BuildPowerShareColumnText(const CKnownFile* pFile)
	{
		if (pFile == NULL)
			return CString();

		CString strText;
		strText.Format(_T("[%s] "), (LPCTSTR)GetResString(pFile->GetPowerShared() ? _T("POWERSHARE_ON_LABEL") : _T("POWERSHARE_OFF_LABEL")));

		const int iPowerShareMode = GetEffectivePowerShareMode(pFile);
		CString strModeLabel(GetPowerShareModeLabel(iPowerShareMode));
		if (pFile->GetPowerSharedMode() < 0)
			strModeLabel = BuildDefaultScopedLabel(strModeLabel);
		strText.Append(strModeLabel);

		if (iPowerShareMode == 3) {
			if (pFile->GetPowerShareLimit() < 0)
				strText.AppendFormat(_T(" %s. %d"), (LPCTSTR)GetDefaultShortLabel(), thePrefs.GetPowerShareLimit());
			else
				strText.AppendFormat(_T(" %d"), pFile->GetPowerShareLimit());
		}

		CString strEffectiveState;
		if (pFile->GetPowerShareAuto())
			strEffectiveState = GetResString(_T("POWERSHARE_ADVISED_LABEL"));
		else if (pFile->GetPowerShareLimited() && iPowerShareMode == 3)
			strEffectiveState = GetResString(_T("POWERSHARE_LIMITED"));
		else if (pFile->GetPowerShareAuthorized())
			strEffectiveState = GetResString(_T("POWERSHARE_AUTHORIZED_LABEL"));
		else
			strEffectiveState = GetResString(_T("POWERSHARE_DENIED_LABEL"));

		strText.AppendFormat(_T(" (%s)"), (LPCTSTR)strEffectiveState);
		return strText;
	}

	CString BuildSpreadbarHistoryColumnText(const CKnownFile* pFile)
	{
		if (pFile == NULL)
			return CString();

		CString strText;
		strText.Format(_T("%.2f"), pFile->statistic.GetSpreadSortValue());
		return strText;
	}

	CString BuildHideOverShareColumnText(const CKnownFile* pFile)
	{
		if (pFile == NULL)
			return CString();

		CString strText;
		const UINT uHideOSInWork = pFile->HideOSInWork();
		strText.Format(_T("[%s] "), (LPCTSTR)GetResString(uHideOSInWork > 0 ? _T("POWERSHARE_ON_LABEL") : _T("POWERSHARE_OFF_LABEL")));

		if (pFile->GetHideOS() < 0)
			strText.AppendFormat(_T("%s. "), (LPCTSTR)GetDefaultShortLabel());

		const UINT uHideOSValue = (pFile->GetHideOS() >= 0) ? static_cast<UINT>(pFile->GetHideOS()) : static_cast<UINT>(thePrefs.GetHideOvershares());
		if (uHideOSValue > 0)
			strText.AppendFormat(_T("%u"), uHideOSValue);
		else if (!IsSpreadbarEnabledForFile(pFile))
			strText.AppendFormat(_T("%s %s"), (LPCTSTR)GetResString(_T("SPREADBAR")), (LPCTSTR)GetResString(_T("DISABLED")));
		else
			strText.Append(GetResString(_T("DISABLED")));

		if (pFile->GetSelectiveChunk() >= 0) {
			if (pFile->GetSelectiveChunk() != 0)
				strText.Append(_T(" + S"));
		} else if (thePrefs.IsSelectiveShareEnabled()) {
			strText.AppendFormat(_T(" + %s. S"), (LPCTSTR)GetDefaultShortLabel());
		}

		return strText;
	}

	CString BuildShareOnlyTheNeedColumnText(const CKnownFile* pFile)
	{
		if (pFile == NULL)
			return CString();

		CString strText;
		if (pFile->GetShareOnlyTheNeed() >= 0) {
			strText = GetEnabledDisabledLabel(pFile->GetShareOnlyTheNeed() != 0);
			return strText;
		}

		return BuildDefaultScopedLabel(GetEnabledDisabledLabel(thePrefs.GetShareOnlyTheNeed() != 0));
	}

	int ComparePermissionSettings(const CKnownFile* pLeft, const CKnownFile* pRight)
	{
		return CompareIntValues(pRight->GetPermissions(), pLeft->GetPermissions());
	}

	int ComparePowerShareSettings(const CKnownFile* pLeft, const CKnownFile* pRight)
	{
		if (!pLeft->GetPowerShared() && pRight->GetPowerShared())
			return -1;
		if (pLeft->GetPowerShared() && !pRight->GetPowerShared())
			return 1;

		int iResult = CompareIntValues(pLeft->GetPowerSharedMode(), pRight->GetPowerSharedMode());
		if (iResult != 0)
			return iResult;

		if (!pLeft->GetPowerShareAuthorized() && pRight->GetPowerShareAuthorized())
			return -1;
		if (pLeft->GetPowerShareAuthorized() && !pRight->GetPowerShareAuthorized())
			return 1;

		if (!pLeft->GetPowerShareAuto() && pRight->GetPowerShareAuto())
			return -1;
		if (pLeft->GetPowerShareAuto() && !pRight->GetPowerShareAuto())
			return 1;

		if (!pLeft->GetPowerShareLimited() && pRight->GetPowerShareLimited())
			return -1;
		if (pLeft->GetPowerShareLimited() && !pRight->GetPowerShareLimited())
			return 1;

		return 0;
	}

	int CompareHideOverShareSettings(const CKnownFile* pLeft, const CKnownFile* pRight)
	{
		const int iHideOSResult = CompareIntValues(pLeft->GetHideOS(), pRight->GetHideOS());
		if (iHideOSResult != 0)
			return iHideOSResult;
		return CompareIntValues(pLeft->GetSelectiveChunk(), pRight->GetSelectiveChunk());
	}

	int CompareShareOnlyTheNeedSettings(const CKnownFile* pLeft, const CKnownFile* pRight)
	{
		return CompareIntValues(pLeft->GetShareOnlyTheNeed(), pRight->GetShareOnlyTheNeed());
	}

	bool TryParseMenuInputNonNegativeInt(const CString& strInput, int& iValue)
	{
		CString strTrimmed(strInput);
		strTrimmed.Trim();
		if (strTrimmed.IsEmpty())
			return false;

		for (int i = 0; i < strTrimmed.GetLength(); ++i) {
			if (!_istdigit(strTrimmed[i]))
				return false;
		}

		iValue = _tstoi(strTrimmed);
		return true;
	}

	bool IsExactDuplicateKnownFile(const CKnownFile* pFile)
	{
		if (pFile == NULL || theApp.knownfiles == NULL)
			return false;

		CSingleLock slDuplicatesLock(&theApp.knownfiles->m_csDuplicatesLock, TRUE);
		for (CKnownFileList::KnownFileList::const_iterator it = theApp.knownfiles->m_duplicateFileList.begin(); it != theApp.knownfiles->m_duplicateFileList.end(); ++it) {
			if (*it == pFile)
				return true;
		}
		return false;
	}

	void UpdateSharedFilesItemCount(CListCtrl& listCtrl, const size_t itemCount, bool bInvalidateAll = false)
	{
		listCtrl.SetItemCountEx(static_cast<int>(itemCount), bInvalidateAll ? LVSICF_NOSCROLL : kSharedFilesSetItemCountFlags);
	}

	void FillSharedFilesFallbackOwnerDataRow(CListCtrl& listCtrl, LPDRAWITEMSTRUCT lpDrawItemStruct)
	{
		if (lpDrawItemStruct == NULL || lpDrawItemStruct->hDC == NULL)
			return;
		CDC* pDC = CDC::FromHandle(lpDrawItemStruct->hDC);
		if (pDC == NULL)
			return;
		CRect rcItem(lpDrawItemStruct->rcItem);
		CRect rcClient;
		listCtrl.GetClientRect(&rcClient);
		rcItem.left = rcClient.left;
		rcItem.right = rcClient.right;
		pDC->FillSolidRect(rcItem, GetCustomSysColor(COLOR_WINDOW));
	}

	CString BuildTempShareableFileKey(const CString& strFilePath)
	{
		CString strKey(strFilePath);
		strKey.MakeLower();
		return strKey;
	}

	ULONGLONG FileTimeToUInt64(const FILETIME& fileTime)
	{
		return (static_cast<ULONGLONG>(fileTime.dwHighDateTime) << 32) | fileTime.dwLowDateTime;
	}

	void RemoveTempShareableFileWriteTime(const CShareableFile* pTempShareableFile)
	{
		if (pTempShareableFile != NULL)
			g_mapTempShareableFileWriteTimes.RemoveKey(const_cast<CShareableFile*>(pTempShareableFile));
	}

	void SetTempShareableFileWriteTime(const CShareableFile& tempShareableFile, const FILETIME& fileTime)
	{
		g_mapTempShareableFileWriteTimes.SetAt(const_cast<CShareableFile*>(&tempShareableFile), FileTimeToUInt64(fileTime));
	}

	bool TryGetTempShareableFileWriteTime(const CShareableFile& tempShareableFile, ULONGLONG& ullFileTime)
	{
		return g_mapTempShareableFileWriteTimes.Lookup(const_cast<CShareableFile*>(&tempShareableFile), ullFileTime) != FALSE;
	}

	void DeleteTempShareableFilesList(CTypedPtrList<CPtrList, CShareableFile*>& liTempShareableFiles)
	{
		while (!liTempShareableFiles.IsEmpty()) {
			CShareableFile* pTempShareableFile = liTempShareableFiles.RemoveHead();
			RemoveTempShareableFileWriteTime(pTempShareableFile);
			delete pTempShareableFile;
		}
	}

	void DeleteTempShareableFilesMap(CTempShareableFilesMap& mapTempShareableFiles)
	{
		for (POSITION pos = mapTempShareableFiles.GetStartPosition(); pos != NULL;) {
			CString strKey;
			CShareableFile* pTempShareableFile = NULL;
			mapTempShareableFiles.GetNextAssoc(pos, strKey, pTempShareableFile);
			RemoveTempShareableFileWriteTime(pTempShareableFile);
			delete pTempShareableFile;
		}
		mapTempShareableFiles.RemoveAll();
	}

	bool CanReuseTempShareableFile(const CShareableFile* pTempShareableFile, const ULONGLONG ullFoundFileSize, const bool bHasFoundFileTime, const FILETIME& tFoundFileTime)
	{
		if (pTempShareableFile == NULL || pTempShareableFile->GetFileSize() != ullFoundFileSize || !bHasFoundFileTime)
			return false;

		ULONGLONG ullStoredFileTime = 0;
		return TryGetTempShareableFileWriteTime(*pTempShareableFile, ullStoredFileTime) && ullStoredFileTime == FileTimeToUInt64(tFoundFileTime);
	}

	void RefreshTempShareableFile(CShareableFile& tempShareableFile, const CString& strFoundFilePath, const CString& strFoundFileName, const CString& strFoundDirectory, const ULONGLONG ullFoundFileSize, const bool bHasFoundFileTime, const FILETIME& tFoundFileTime)
	{
		tempShareableFile.SetFilePath(strFoundFilePath);
		tempShareableFile.SetAFileName(strFoundFileName);
		tempShareableFile.SetPath(strFoundDirectory);
		tempShareableFile.SetSharedDirectory(_T(""));
		tempShareableFile.SetFileSize(ullFoundFileSize);
		tempShareableFile.SetVerifiedFileType(FILETYPE_UNKNOWN);
		tempShareableFile.ClearTags();
		const uchar aucMD4[MDX_DIGEST_SIZE] = {};
		tempShareableFile.SetFileHash(aucMD4);
		if (bHasFoundFileTime)
			SetTempShareableFileWriteTime(tempShareableFile, tFoundFileTime);
		else
			RemoveTempShareableFileWriteTime(&tempShareableFile);
	}

	class CSharedFilesSelectionRestoreGuard
	{
	public:
		explicit CSharedFilesSelectionRestoreGuard(CSharedFilesCtrl& listCtrl)
			: m_listCtrl(listCtrl)
		{
			m_listCtrl.SetSelectionRestoreInProgress(true);
		}

		~CSharedFilesSelectionRestoreGuard()
		{
			m_listCtrl.SetSelectionRestoreInProgress(false);
		}

	private:
		CSharedFilesCtrl& m_listCtrl;
	};
}

bool NeedArchiveInfoPage(const CSimpleArray<CObject*> *paItems);
void UpdateFileDetailsPages(CListViewPropertySheet *pSheet
	, CResizablePage *pArchiveInfo, CResizablePage *pMediaInfo, CResizablePage *pFileLink);


//////////////////////////////////////////////////////////////////////////////
// CSharedFileDetailsSheet

class CSharedFileDetailsSheet : public CListViewWalkerPropertySheet
{
	DECLARE_DYNAMIC(CSharedFileDetailsSheet)

	void Localize();
public:
	CSharedFileDetailsSheet(CTypedPtrList<CPtrList, CShareableFile*> &aFiles, UINT uInvokePage = 0, CListCtrlItemWalk *pListCtrl = NULL);

protected:
	CArchivePreviewDlg	m_wndArchiveInfo;
	CCommentDialog		m_wndFileComments;
	CED2kLinkDlg		m_wndFileLink;
	CFileInfoDialog		m_wndMediaInfo;
	CMetaDataDlg		m_wndMetaData;
	CClosableTabCtrl	m_tabDark;

	UINT m_uInvokePage;
	static LPCTSTR m_pPshStartPage;

	void UpdateTitle();

	virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
	virtual BOOL OnInitDialog();

	DECLARE_MESSAGE_MAP()
	afx_msg void OnDestroy();
	afx_msg LRESULT OnDataChanged(WPARAM, LPARAM);
};

LPCTSTR CSharedFileDetailsSheet::m_pPshStartPage;

IMPLEMENT_DYNAMIC(CSharedFileDetailsSheet, CListViewWalkerPropertySheet)

BEGIN_MESSAGE_MAP(CSharedFileDetailsSheet, CListViewWalkerPropertySheet)
	ON_WM_DESTROY()
	ON_MESSAGE(UM_DATA_CHANGED, OnDataChanged)
END_MESSAGE_MAP()

void CSharedFileDetailsSheet::Localize()
{
	m_wndMediaInfo.Localize();
	SetTabTitle(_T("CONTENT_INFO"), &m_wndMediaInfo, this);
	m_wndMetaData.Localize();
	SetTabTitle(_T("META_DATA"), &m_wndMetaData, this);
	m_wndFileLink.Localize();
	SetTabTitle(_T("SW_LINK"), &m_wndFileLink, this);
	m_wndFileComments.Localize();
	SetTabTitle(_T("COMMENT"), &m_wndFileComments, this);
	m_wndArchiveInfo.Localize();
	SetTabTitle(_T("CONTENT_INFO"), &m_wndArchiveInfo, this);
}

CSharedFileDetailsSheet::CSharedFileDetailsSheet(CTypedPtrList<CPtrList, CShareableFile*> &aFiles, UINT uInvokePage, CListCtrlItemWalk *pListCtrl)
	: CListViewWalkerPropertySheet(pListCtrl)
	, m_uInvokePage(uInvokePage)
{
	for (POSITION pos = aFiles.GetHeadPosition(); pos != NULL;)
		m_aItems.Add(aFiles.GetNext(pos));
	m_psh.dwFlags &= ~PSH_HASHELP;

	m_wndFileComments.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndFileComments.m_psp.dwFlags |= PSP_USEICONID;
	m_wndFileComments.m_psp.pszIcon = _T("FileComments");
	m_wndFileComments.SetFiles(&m_aItems);
	AddPage(&m_wndFileComments);

	m_wndArchiveInfo.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndArchiveInfo.m_psp.dwFlags |= PSP_USEICONID;
	m_wndArchiveInfo.m_psp.pszIcon = _T("ARCHIVE_PREVIEW");
	m_wndArchiveInfo.SetFiles(&m_aItems);

	m_wndMediaInfo.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndMediaInfo.m_psp.dwFlags |= PSP_USEICONID;
	m_wndMediaInfo.m_psp.pszIcon = _T("MEDIAINFO");
	m_wndMediaInfo.SetFiles(&m_aItems);
	if (NeedArchiveInfoPage(&m_aItems))
		AddPage(&m_wndArchiveInfo);
	else
		AddPage(&m_wndMediaInfo);

	m_wndMetaData.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndMetaData.m_psp.dwFlags |= PSP_USEICONID;
	m_wndMetaData.m_psp.pszIcon = _T("METADATA");
	if (m_aItems.GetSize() == 1 && thePrefs.IsExtControlsEnabled()) {
		m_wndMetaData.SetFiles(&m_aItems);
		AddPage(&m_wndMetaData);
	}

	m_wndFileLink.m_psp.dwFlags &= ~PSP_HASHELP;
	m_wndFileLink.m_psp.dwFlags |= PSP_USEICONID;
	m_wndFileLink.m_psp.pszIcon = _T("ED2KLINK");
	m_wndFileLink.SetFiles(&m_aItems);
	AddPage(&m_wndFileLink);

	LPCTSTR pPshStartPage = m_pPshStartPage;
	if (m_uInvokePage != 0)
		pPshStartPage = MAKEINTRESOURCE(m_uInvokePage);
	for (int i = (int)m_pages.GetCount(); --i >= 0;)
		if (GetPage(i)->m_psp.pszTemplate == pPshStartPage) {
			m_psh.nStartPage = i;
			break;
		}
}

void CSharedFileDetailsSheet::OnDestroy()
{
	if (m_uInvokePage == 0)
		m_pPshStartPage = GetPage(GetActiveIndex())->m_psp.pszTemplate;
	CListViewWalkerPropertySheet::OnDestroy();
}

BOOL CSharedFileDetailsSheet::OnInitDialog()
{
	EnableStackedTabs(FALSE);
	BOOL bResult = CListViewWalkerPropertySheet::OnInitDialog();
	HighColorTab::UpdateImageList(*this);
	InitWindowStyles(this);
	EnableSaveRestore(_T("SharedFileDetailsSheet")); // call this after(!) OnInitDialog
	Localize();
	UpdateTitle();

	m_tabDark.m_bClosable = false;
	m_tabDark.m_bAllowTabReordering = false;

	if (IsDarkModeEnabled()) {
		HWND hTab = PropSheet_GetTabControl(m_hWnd);
		if (hTab != NULL) {
			::SetWindowTheme(hTab, _T(""), _T(""));
			m_tabDark.SubclassWindow(hTab);
		}
	}

	return bResult;
}

LRESULT CSharedFileDetailsSheet::OnDataChanged(WPARAM, LPARAM)
{
	UpdateTitle();
	UpdateFileDetailsPages(this, &m_wndArchiveInfo, &m_wndMediaInfo, &m_wndFileLink);
	return 1;
}

void CSharedFileDetailsSheet::UpdateTitle()
{
	CString sTitle(GetResString(_T("DETAILS")));
	if (m_aItems.GetSize() == 1)
		sTitle.AppendFormat(_T(": %s"), (LPCTSTR)(static_cast<CAbstractFile*>(m_aItems[0])->GetFileName()));
	SetWindowText(sTitle);
}

BOOL CSharedFileDetailsSheet::OnCommand(WPARAM wParam, LPARAM lParam)
{
	const UINT uCommand = LOWORD(wParam);
	const BOOL bResult = CListViewWalkerPropertySheet::OnCommand(wParam, lParam);
	if (uCommand == ID_APPLY_NOW || uCommand == IDOK) {
		CSharedFilesCtrl *pSharedFilesCtrl = DYNAMIC_DOWNCAST(CSharedFilesCtrl, m_pListCtrl->GetListCtrl());
		if (pSharedFilesCtrl)
			for (int i = m_aItems.GetSize(); --i >= 0;) {
				CKnownFile *pKnownFile = DYNAMIC_DOWNCAST(CKnownFile, m_aItems[i]);
				if (pKnownFile != NULL)
					pSharedFilesCtrl->UpdateFile(pKnownFile);
			}
	}
	return bResult;
}


//////////////////////////////////////////////////////////////////////////////
// CSharedFilesCtrl

IMPLEMENT_DYNAMIC(CSharedFilesCtrl, CMuleListCtrl)

BEGIN_MESSAGE_MAP(CSharedFilesCtrl, CMuleListCtrl)
	ON_NOTIFY_REFLECT(LVN_COLUMNCLICK, OnLvnColumnClick)
	ON_NOTIFY_REFLECT(LVN_GETDISPINFO, OnLvnGetDispInfo)
	ON_NOTIFY_REFLECT(LVN_GETINFOTIP, OnLvnGetInfoTip)
	ON_NOTIFY_REFLECT(NM_DBLCLK, OnNmDblClk)
	ON_NOTIFY_REFLECT_EX(NM_CLICK, OnNMClick)
	ON_WM_CONTEXTMENU()
	ON_WM_KEYDOWN()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_MOUSEMOVE()
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_MESSAGE(UM_SHARED_FILESCTRL_FILESYSTEM_RELOAD_READY, OnFileSystemReloadReady)
	ON_MESSAGE(UM_SHARED_FILESCTRL_PROCESS_BULK_OPERATION, OnProcessSharedFilesBulkOperation)
END_MESSAGE_MAP()
CSharedFilesCtrl::CSharedFilesCtrl()
	: CListCtrlItemWalk(this)
	, nAICHHashing()
	, m_eFilter(FilterType::Shared)
	, m_uFilterID(1)
	, m_aSortBySecondValue()
	, m_pDirectoryFilter()
	, m_iDataSize(-1)
	, m_pToolTip(NULL)
	, m_pHighlightedItem()
	, m_bSelectionRestoreInProgress(false)
	, m_bExecutingSharedFilesCommand(false)
	, m_lFileSystemReloadGeneration(0)
	, m_lFileSystemReloadActive(0)
	, m_eSharedFilesBulkOperation(SharedFilesBulkOperationNone)
	, m_uSharedFilesBulkAction(0)
	, m_uSharedFilesBulkProcessed(0)
	, m_uSharedFilesBulkFailed(0)
	, m_uSharedFilesBulkStale(0)
	, m_uSharedFilesBulkTotal(0)
	, m_uSharedFilesBulkSequence(0)
	, m_uSharedFilesBulkCorrelationId(0)
	, m_dwSharedFilesBulkStartedTick(0)
	, m_dwSharedFilesBulkLastProgressTick(0)
	, m_dwSharedFilesBulkLastCompactTick(0)
	, m_bSharedFilesBulkPending(false)
	, m_bSharedFilesBulkCollectingSelection(false)
	, m_iSharedFilesBulkNextSelectionIndex(-1)
	, m_uSharedFilesBulkSelectionQueued(0)
	, m_bSharedFilesBulkListStateBatchActive(false)
	, m_bSharedFilesBulkRemovePending(false)
	, m_bSharedFilesBulkRemoveRowsDetached(false)
	, m_bSharedFilesBulkRemoveVisibleSnapshotActive(false)
	, m_uSharedFilesBulkRemoveVisibleSnapshotRows(0)
	, m_bBackendDownloadRemoveOverlayActive(false)
	, m_bBackendDownloadRemoveRowsDetached(false)
	, m_bBackendDownloadRemoveVisibleSnapshotActive(false)
	, m_uBackendDownloadRemoveVisibleSnapshotRows(0)
	, m_uDownloadRemoveBatchDepth(0)
	, m_bDownloadRemoveBatchPending(false)
	, m_uBackendDownloadRemoveSequence(0)
	, m_uBackendDownloadRemoveCorrelationId(0)
	, m_uCompletedBackendDownloadRemoveSequence(0)
	, m_uCompletedBackendDownloadRemoveCorrelationId(0)
	, m_uSharedFilesBulkListStateID(0)
	, m_bSharedFilesBulkAddPending(false)
	, m_uSharedFilesHashingOverlayTotal(0)
	, m_uSharedFilesHashingOverlayLastRemaining(0)
	, m_uSharedFilesMetadataOverlayTotal(0)
	, m_uSharedFilesMetadataOverlayLastRemaining(0)
	, m_bSharedFilesRawSortInProgress(false)
	, m_uSharedFilesListReloadDeferDepth(0)
	, m_bSharedFilesListReloadDeferred(false)
	, m_bSharedFilesListReloadDeferredSortCurrentList(false)
	, m_eSharedFilesListReloadDeferredState(LSF_NONE)
{
	SetGeneralPurposeFind(true);
	m_pToolTip = new CToolTipCtrlX;
	SetSkinKey(_T("SharedFilesLv"));
}

CSharedFilesCtrl::~CSharedFilesCtrl()
{
	if (::IsWindow(m_hWnd)) {
		KillTimer(TimerSharedFilesBulkOperation);
	}
	ClearSharedFilesBulkOperation();
	theApp.CancelSharedFilesFileSystemReload(m_hWnd);
	DrainFileSystemReloadMessages(m_hWnd);
	InterlockedExchange(&m_lFileSystemReloadActive, 0);
	InterlockedIncrement(&m_lFileSystemReloadGeneration);
	DeleteTempShareableFilesList(liTempShareableFilesInDir);
	delete m_pToolTip;
}

void CSharedFilesCtrl::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TimerSharedFilesBulkOperation) {
		KillTimer(TimerSharedFilesBulkOperation);
		OnProcessSharedFilesBulkOperation(0, 0);
		return;
	}

	CMuleListCtrl::OnTimer(nIDEvent);
}

void CSharedFilesCtrl::OnDestroy()
{
	KillTimer(TimerSharedFilesBulkOperation);
	theApp.CancelSharedFilesFileSystemReload(m_hWnd);
	DrainFileSystemReloadMessages(m_hWnd);
	ClearSharedFilesBulkOperation();
	InterlockedExchange(&m_lFileSystemReloadActive, 0);
	InterlockedIncrement(&m_lFileSystemReloadGeneration);
	CMuleListCtrl::OnDestroy();
}

void CSharedFilesCtrl::BeginSharedFilesListReloadDefer()
{
	++m_uSharedFilesListReloadDeferDepth;
}

void CSharedFilesCtrl::EndSharedFilesListReloadDefer()
{
	if (m_uSharedFilesListReloadDeferDepth == 0)
		return;

	--m_uSharedFilesListReloadDeferDepth;
	if (m_uSharedFilesListReloadDeferDepth != 0 || !m_bSharedFilesListReloadDeferred)
		return;

	const bool bSortCurrentList = m_bSharedFilesListReloadDeferredSortCurrentList;
	const EListStateField LsfFlag = m_eSharedFilesListReloadDeferredState;
	m_bSharedFilesListReloadDeferred = false;
	m_bSharedFilesListReloadDeferredSortCurrentList = false;
	m_eSharedFilesListReloadDeferredState = LSF_NONE;
	ReloadList(bSortCurrentList, LsfFlag);
}

bool CSharedFilesCtrl::IsCompletedBackendDownloadRemoveOverlay(uint64 uSequence, uint64 uCorrelationId) const
{
	if (uSequence != 0 && m_uCompletedBackendDownloadRemoveSequence != 0 && uSequence <= m_uCompletedBackendDownloadRemoveSequence)
		return true;
	if (uCorrelationId != 0 && m_uCompletedBackendDownloadRemoveCorrelationId == uCorrelationId && (uSequence == 0 || m_uCompletedBackendDownloadRemoveSequence == 0 || m_uCompletedBackendDownloadRemoveSequence == uSequence))
		return true;
	return false;
}

void CSharedFilesCtrl::MarkCompletedBackendDownloadRemoveOverlay(uint64 uSequence, uint64 uCorrelationId)
{
	if (uSequence == 0 && uCorrelationId == 0)
		return;
	if (uSequence != 0) {
		if (m_uCompletedBackendDownloadRemoveSequence == 0 || uSequence > m_uCompletedBackendDownloadRemoveSequence) {
			m_uCompletedBackendDownloadRemoveSequence = uSequence;
			m_uCompletedBackendDownloadRemoveCorrelationId = uCorrelationId;
		}
		return;
	}
	m_uCompletedBackendDownloadRemoveCorrelationId = uCorrelationId;
}

void CSharedFilesCtrl::BeginDownloadRemoveBatch()
{
	if (!::IsWindow(m_hWnd))
		return;
	if (m_uDownloadRemoveBatchDepth == 0)
		m_bDownloadRemoveBatchPending = false;
	++m_uDownloadRemoveBatchDepth;
}

void CSharedFilesCtrl::EndDownloadRemoveBatch()
{
	if (m_uDownloadRemoveBatchDepth == 0)
		return;
	--m_uDownloadRemoveBatchDepth;
	if (m_uDownloadRemoveBatchDepth != 0 || !m_bDownloadRemoveBatchPending || !::IsWindow(m_hWnd))
		return;

	m_bDownloadRemoveBatchPending = false;
	HidePersistentInfoTip(true);
	SetRedraw(false);
	const bool bCompacted = CompactNullSharedFilesItems(_T("shared-download-remove-batch"));
	SetRedraw(true);
	if (bCompacted) {
		ShowFilesCount();
		if (theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL)
			theApp.emuledlg->sharedfileswnd->PostSelectedFilesDetailsAsync(true);
	}
}

void CSharedFilesCtrl::RequestSharedListRedrawForRange(int iFirst, int iLast)
{
	if (theApp.GetBackendLifecycleState() >= CemuleApp::BackendLifecycleStoppingUiUpdates || !::IsWindow(m_hWnd))
		return;
	if (iFirst < 0 || iLast < iFirst || GetItemCount() <= 0)
		return;
	iFirst = max(0, iFirst);
	iLast = min(iLast, GetItemCount() - 1);
	if (iLast >= iFirst)
		RequestRowRedrawAsync(iFirst, iLast);
}

void CSharedFilesCtrl::RequestSharedListRedraw()
{
	if (::IsWindow(m_hWnd))
		RequestFullRedrawAsync();
}


bool CSharedFilesCtrl::CompactNullSharedFilesItems(LPCTSTR pszReason)
{
	bool bHasNullItems = false;
	for (size_t i = 0; i < m_ListedItemsVector.size(); ++i) {
		if (m_ListedItemsVector[i] == NULL) {
			bHasNullItems = true;
			break;
		}
	}
	if (!bHasNullItems)
		return false;

	std::vector<CKnownFile*> vecCompactedItems;
	vecCompactedItems.reserve(m_ListedItemsVector.size());

	for (size_t i = 0; i < m_ListedItemsVector.size(); ++i) {
		CKnownFile *pFile = m_ListedItemsVector[i];
		if (pFile == NULL)
			continue;
		vecCompactedItems.push_back(pFile);
	}

	SaveListState(m_uFilterID, kSharedFilesViewState);
	m_ListedItemsVector.swap(vecCompactedItems);
	RebuildListedItemsMap();
	SetItemCountAndKeepPageFilled(m_ListedItemsVector.size(), 0);
	{
		CSharedFilesSelectionRestoreGuard guard(*this);
		RestoreListState(m_uFilterID, kSharedFilesViewState, false);
	}
	RequestSharedListRedraw();
	return true;
}

void CSharedFilesCtrl::UpdateBackendDownloadRemoveOverlay(UINT uDone, UINT uTotal, uint64 uSequence, uint64 uCorrelationId)
{
	if (uTotal < BULK_OPERATION_MIN_ITEMS)
		return;
	if (IsCompletedBackendDownloadRemoveOverlay(uSequence, uCorrelationId))
		return;

	m_bBackendDownloadRemoveOverlayActive = true;
	m_uBackendDownloadRemoveSequence = uSequence;
	m_uBackendDownloadRemoveCorrelationId = uCorrelationId;
	if (IsBackendDownloadRemoveSnapshotActive()) {
		DetachSharedFilesVisibleRemoveRows();
		ApplySharedFilesBulkRemoveVisibleItemCount(false);
	}

	if (m_eSharedFilesBulkOperation != SharedFilesBulkOperationNone) {
		if (theApp.emuledlg != NULL)
			theApp.emuledlg->RefreshActiveBulkOperationOverlays();
		return;
	}

	CString strDetail;
	strDetail.Format(GetResString(_T("BULKOP_PROGRESS_DETAIL")), uDone, uTotal);
	UpdateOperationOverlay(GetResString(_T("BULKOP_DELETE_DOWNLOADS_TITLE")), strDetail, uDone, uTotal, true);
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
}

void CSharedFilesCtrl::HideBackendDownloadRemoveOverlay(uint64 uSequence, uint64 uCorrelationId)
{
	const bool bHadHiddenRows = m_bBackendDownloadRemoveRowsDetached || m_backendDownloadRemoveHiddenRows.GetCount() != 0;
	if (uSequence == 0 && uCorrelationId == 0 && m_bBackendDownloadRemoveOverlayActive)
		MarkCompletedBackendDownloadRemoveOverlay(m_uBackendDownloadRemoveSequence, m_uBackendDownloadRemoveCorrelationId);
	else
		MarkCompletedBackendDownloadRemoveOverlay(uSequence, uCorrelationId);

	if (!m_bBackendDownloadRemoveOverlayActive) {
		if (bHadHiddenRows)
			ClearBackendDownloadRemoveHiddenRows(true);
		return;
	}
	if (uSequence != 0 && m_uBackendDownloadRemoveSequence != 0 && m_uBackendDownloadRemoveSequence != uSequence)
		return;
	if (uCorrelationId != 0 && m_uBackendDownloadRemoveCorrelationId != 0 && m_uBackendDownloadRemoveCorrelationId != uCorrelationId)
		return;

	m_bBackendDownloadRemoveOverlayActive = false;
	m_uBackendDownloadRemoveSequence = 0;
	m_uBackendDownloadRemoveCorrelationId = 0;
	if (bHadHiddenRows)
		ClearBackendDownloadRemoveHiddenRows(true);
	if (m_eSharedFilesBulkOperation == SharedFilesBulkOperationNone)
		HideOperationOverlay();
	if (theApp.emuledlg != NULL && !theApp.IsClosing())
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
}

void CSharedFilesCtrl::BeginBackendDownloadRemoveVisibleRows(const CStringArray& astrDownloadHashes, uint64 uSequence, uint64 uCorrelationId)
{
	if (theApp.IsClosing() || !::IsWindow(m_hWnd) || astrDownloadHashes.GetSize() < BULK_OPERATION_MIN_ITEMS)
		return;

	bool bAddedHiddenRows = false;
	for (INT_PTR i = 0; i < astrDownloadHashes.GetSize(); ++i) {
		if (QueueBackendDownloadRemoveHiddenHash(astrDownloadHashes.GetAt(i)))
			bAddedHiddenRows = true;
	}
	if (!bAddedHiddenRows && m_backendDownloadRemoveHiddenRows.GetCount() == 0)
		return;

	m_bBackendDownloadRemoveRowsDetached = true;
	m_bBackendDownloadRemoveOverlayActive = true;
	if (uSequence != 0 || m_uBackendDownloadRemoveSequence == 0)
		m_uBackendDownloadRemoveSequence = uSequence;
	if (uCorrelationId != 0 || m_uBackendDownloadRemoveCorrelationId == 0)
		m_uBackendDownloadRemoveCorrelationId = uCorrelationId;
	DetachSharedFilesVisibleRemoveRows();
}

void CSharedFilesCtrl::RemoveBackendDownloadRowsByHash(const std::vector<CString>& vecFileHashes)
{
	if (theApp.IsClosing() || !::IsWindow(m_hWnd) || vecFileHashes.empty())
		return;
	if (!m_bBackendDownloadRemoveOverlayActive && !m_bBackendDownloadRemoveRowsDetached && vecFileHashes.size() < BULK_OPERATION_MIN_ITEMS)
		return;

	bool bAddedHiddenRows = false;
	for (std::vector<CString>::const_iterator it = vecFileHashes.begin(); it != vecFileHashes.end(); ++it) {
		if (QueueBackendDownloadRemoveHiddenHash(*it))
			bAddedHiddenRows = true;
	}
	if (!bAddedHiddenRows && m_backendDownloadRemoveHiddenRows.GetCount() == 0)
		return;

	m_bBackendDownloadRemoveRowsDetached = true;
	DetachSharedFilesVisibleRemoveRows();
}

void CSharedFilesCtrl::Init()
{
	SetPrefsKey(_T("SharedFilesCtrl"));
	SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_DOUBLEBUFFER);
	ASSERT((GetStyle() & LVS_SINGLESEL) == 0);

	// Alignment rule: left for text, dates, and status labels; right for sizes, rates, counts, durations, and percentages.
	InsertColumn(0,		EMPTY,	LVCFMT_LEFT,	DFLT_FILENAME_COL_WIDTH);			//DL_FILENAME
	InsertColumn(1,		EMPTY,	LVCFMT_RIGHT,	DFLT_SIZE_COL_WIDTH);				//DL_SIZE
	InsertColumn(2,		EMPTY,	LVCFMT_LEFT,	DFLT_FILETYPE_COL_WIDTH);			//TYPE
	InsertColumn(3,		EMPTY,	LVCFMT_LEFT,	DFLT_PRIORITY_COL_WIDTH);			//PRIORITY
	InsertColumn(4,		EMPTY,	LVCFMT_LEFT,	DFLT_HASH_COL_WIDTH, -1, true);		//FILEID
	InsertColumn(5,		EMPTY,	LVCFMT_RIGHT,	100);								//SF_REQUESTS
	InsertColumn(6,		EMPTY,	LVCFMT_RIGHT,	100, -1, true);						//SF_ACCEPTS
	InsertColumn(7,		EMPTY,	LVCFMT_RIGHT,	120);								//SF_TRANSFERRED
	InsertColumn(8,		EMPTY,	LVCFMT_LEFT,	DFLT_PARTSTATUS_COL_WIDTH);			//SHARED_STATUS
	InsertColumn(9,		EMPTY,	LVCFMT_LEFT,	DFLT_FOLDER_COL_WIDTH, -1, true);	//FOLDER
	InsertColumn(10,	EMPTY,	LVCFMT_RIGHT,	60);								//COMPLSOURCES
	InsertColumn(11,	EMPTY,	LVCFMT_LEFT,	100);								//SHAREDTITLE
	InsertColumn(12,	EMPTY,	LVCFMT_LEFT,	DFLT_ARTIST_COL_WIDTH, -1, true);	//ARTIST
	InsertColumn(13,	EMPTY,	LVCFMT_LEFT,	DFLT_ALBUM_COL_WIDTH, -1, true);	//ALBUM
	InsertColumn(14,	EMPTY,	LVCFMT_LEFT,	DFLT_TITLE_COL_WIDTH, -1, true);	//TITLE
	InsertColumn(15,	EMPTY,	LVCFMT_RIGHT,	DFLT_LENGTH_COL_WIDTH, -1, true);	//LENGTH
	InsertColumn(16,	EMPTY,	LVCFMT_RIGHT,	DFLT_BITRATE_COL_WIDTH, -1, true);	//BITRATE
	InsertColumn(17,	EMPTY,	LVCFMT_LEFT,	DFLT_CODEC_COL_WIDTH, -1, true);	//CODEC
	InsertColumn(18,	EMPTY,	LVCFMT_RIGHT,	DFLT_LENGTH_COL_WIDTH);				//RATIO
	InsertColumn(19,	EMPTY,	LVCFMT_RIGHT,	DFLT_LENGTH_COL_WIDTH);				//RATIO_SESSION
	InsertColumn(kSharedFilesColumnPermission, EMPTY, LVCFMT_LEFT, 120);			//SHOW_PERMISSION
	InsertColumn(kSharedFilesColumnPowershare, EMPTY, LVCFMT_LEFT, 170);			//POWERSHARE
	InsertColumn(kSharedFilesColumnSpreadbarHistory, EMPTY, LVCFMT_LEFT, 170);		//SPREADBAR_UL_PART_HISTORY
	InsertColumn(kSharedFilesColumnHideOverShare, EMPTY, LVCFMT_LEFT, 120);		//HIDE_OVER_SHARE
	InsertColumn(kSharedFilesColumnShareOnlyTheNeed, EMPTY, LVCFMT_LEFT, 120);	//SHARE_ONLY_THE_NEED
	InsertColumn(kSharedFilesColumnLastRequest, EMPTY, LVCFMT_LEFT, 130);			//SF_LAST_REQUEST

	SetAllIcons();
	LoadSettings();

	m_aSortBySecondValue[0] = true; // Requests:			Sort by 2nd value by default
	m_aSortBySecondValue[1] = true; // Accepted Requests:	Sort by 2nd value by default
	m_aSortBySecondValue[2] = true; // Transferred Data:	Sort by 2nd value by default
	m_aSortBySecondValue[3] = false; // Shared ED2K|Kad:	Sort by 1st value by default
	if (GetSortItem() >= 5 && GetSortItem() <= 7)
		m_aSortBySecondValue[GetSortItem() - 5] = GetSortSecondValue();
	else if (GetSortItem() == 11)
		m_aSortBySecondValue[3] = GetSortSecondValue();
	SetSortArrow();
	m_pSortParam = MAKELONG(GetSortItem() + (GetSortSecondValue() ? 100 : 0), !GetSortAscending());
	UpdateSortHistory(m_pSortParam); // This will save sort parameter history in m_liSortHistory which will be used when we call GetNextSortOrder.

	CToolTipCtrl *tooltip = GetToolTips();
	if (tooltip) {
		m_pToolTip->SetFileIconToolTip(true);
		m_pToolTip->SubclassWindow(*tooltip);
		tooltip->ModifyStyle(0, TTS_NOPREFIX);
		tooltip->SetDelayTime(TTDT_AUTOPOP, SEC2MS(20));
		tooltip->SetDelayTime(TTDT_INITIAL, SEC2MS(thePrefs.GetToolTipDelay()));
	}

	m_ShareDropTarget.SetParent(this);
	VERIFY(m_ShareDropTarget.Register(this));
}

void CSharedFilesCtrl::OnSysColorChange()
{
	CMuleListCtrl::OnSysColorChange();
	SetAllIcons();
	CreateMenus();
}

void CSharedFilesCtrl::SetAllIcons()
{
	ApplyImageList(NULL);
	m_ImageList.DeleteImageList();
	m_ImageList.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	m_ImageList.Add(CTempIconLoader(_T("EMPTY"))); //0
	m_ImageList.Add(CTempIconLoader(_T("FileSharedServer"))); //1
	m_ImageList.Add(CTempIconLoader(_T("FileSharedKad"))); //2
	m_ImageList.Add(CTempIconLoader(_T("Rating_NotRated"))); //3
	m_ImageList.Add(CTempIconLoader(_T("Rating_Fake"))); //4
	m_ImageList.Add(CTempIconLoader(_T("Rating_Poor"))); //5
	m_ImageList.Add(CTempIconLoader(_T("Rating_Fair"))); //6
	m_ImageList.Add(CTempIconLoader(_T("Rating_Good"))); //7
	m_ImageList.Add(CTempIconLoader(_T("Rating_Excellent"))); //8
	m_ImageList.Add(CTempIconLoader(_T("Collection_Search"))); //9 rating for comments are searched on kad
	m_ImageList.SetOverlayImage(m_ImageList.Add(CTempIconLoader(_T("FileCommentsOvl"))), 1);
	// Apply the image list also to the listview control, even if we use our own 'DrawItem'.
	// This is needed to give the listview control a chance to initialize the row height.
	VERIFY(ApplyImageList(m_ImageList) == NULL);
	theApp.GetFileTypeSystemImageIdx(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR)); // This is just a dummy call to force it to set m_hSystemImageList.
}

void CSharedFilesCtrl::Localize()
{
	static const LPCTSTR uids[SharedFilesColumnCount] =
	{
		_T("DL_FILENAME"), _T("DL_SIZE"), _T("TYPE"), _T("PRIORITY"), _T("FILEID")
		, _T("SF_REQUESTS"), _T("SF_ACCEPTS"), _T("SF_TRANSFERRED"), _T("SHARED_STATUS"), _T("FOLDER")
		, _T("COMPLSOURCES"), _T("SHAREDTITLE"), _T("ARTIST"), _T("ALBUM"), _T("TITLE")
		, _T("LENGTH"), _T("BITRATE"), _T("CODEC")
		, _T("RATIO"), _T("RATIO_SESSION"), _T("SHARE_PERMISSION_GROUP"), _T("POWERSHARE"), _T("SPREADBAR_UL_PART_HISTORY"), _T("HIDE_OVER_SHARE_MENU"), _T("SHAREONLYTHENEED"), _T("SF_LAST_REQUEST")
	};

	LocaliseHeaderCtrl(uids, _countof(uids));

	CreateMenus();

	RequestSharedListRedraw();

	ShowFilesCount();
}

void CSharedFilesCtrl::AddFile(CKnownFile* file, bool bBatchVisibleListUpdate)
{
	int m_iIndex = -1;
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->sharedfileswnd || !IsWindowVisible() || !file || IsFilteredOut(file) || FindListedIndexByPointer(file) >= 0)
		return;

	if (IsSharedFilesVisibleRemoveSnapshotActive()) {
		m_bSharedFilesBulkAddPending = true;
		ApplySharedFilesBulkRemoveVisibleItemCount(false);
		return;
	}

	if (bBatchVisibleListUpdate) {
		m_bSharedFilesBulkAddPending = true;
		ShowFilesCount();
		return;
	}

	// if we are in the file system view, this might be a CKnownFile which has to replace a CShareableFile
	// (in case we start sharing this file), so make sure to replace the old one instead of adding a new
	if (m_eFilter == FilterType::FileSystem) {
		for (POSITION pos = liTempShareableFilesInDir.GetHeadPosition(); pos != NULL;) {
			CShareableFile* pFileSharable = liTempShareableFilesInDir.GetNext(pos);
			CKnownFile* pfileKnown = static_cast<CKnownFile*>(pFileSharable);
			if (pfileKnown->GetFileSize() == file->GetFileSize() && pfileKnown->GetFilePath().CompareNoCase(file->GetFilePath()) == 0) {
				int m_iOldFileIndex = -1;
				m_iOldFileIndex = FindListedIndexByPointer(pfileKnown);
				if (m_iOldFileIndex >= 0) {
					UpdateFile(pfileKnown, m_iOldFileIndex);
					ShowFilesCount();
					return;
				}
			}
		}
	} else
		// Don't save/reload list state if this is a file system view. Because all objects will be deleted and reloaded every time ReloadList is called.
		SaveListState(m_uFilterID, kSharedFilesViewState); // Save selections and scroll state

	const bool bLargeListUpdate = m_ListedItemsVector.size() >= kSharedFilesLargeListRows;
	int m_iStartIndex = static_cast<int>(m_ListedItemsVector.size());
	if (!bLargeListUpdate && HasActiveSortOrder()) {
		std::vector<CKnownFile*>::iterator itInsert = std::lower_bound(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), file, SortFunc);
		m_iStartIndex = static_cast<int>(std::distance(m_ListedItemsVector.begin(), itInsert));
	}

	if (m_iStartIndex >= 0) {
		SetRedraw(false); // Suspend painting
		m_ListedItemsVector.insert(m_ListedItemsVector.begin() + m_iStartIndex, file); // Insert the new value at the determined position.
		UpdateListedItemsMapRange(m_iStartIndex, static_cast<int>(m_ListedItemsVector.size()) - 1);
	} else { // This case is not expected, but handled for robustness.
		ReloadList(false, kSharedFilesViewState); // Something is wrong at this point, let's do a full reload instead having possible glitches or crashes.
		return;
	}

	UpdateSharedFilesItemCount(*this, m_ListedItemsVector.size()); // Set current count for virtual list.

	if (m_eFilter != FilterType::FileSystem) { // Don't save/reload list state if this is a file system view. Because all objects will be deleted and reloaded every time ReloadList is called.
		CSharedFilesSelectionRestoreGuard guard(*this);
		RestoreListState(m_uFilterID, kSharedFilesViewState, false); // Restore selections and scroll state
	}

	SetRedraw(true); // Resume painting
	RequestRowRedrawAsync(m_iStartIndex, static_cast<int>(m_ListedItemsVector.size()) - 1); // Coalesce updated rows.
	ShowFilesCount();
}

void CSharedFilesCtrl::FlushBulkAddListUpdate(const EListStateField LsfFlag)
{
	if (!m_bSharedFilesBulkAddPending || theApp.IsClosing() || !::IsWindow(m_hWnd))
		return;

	if (theApp.emuledlg == NULL || theApp.emuledlg->activewnd != theApp.emuledlg->sharedfileswnd || !IsWindowVisible())
		return;
	m_bSharedFilesBulkAddPending = false;
	ReloadList(false, LsfFlag);
}


void CSharedFilesCtrl::RemoveFile(CKnownFile*file, const bool bDeletedFromDisk, const bool bWillReloadListLater)
{
	if (theApp.IsClosing() || file == NULL)
		return;

	const int m_iIndex = FindListedIndexByPointer(file);
	if (m_iIndex < 0)
		return;
	HidePersistentInfoTip(true);

	if (theApp.emuledlg->activewnd != theApp.emuledlg->sharedfileswnd || !IsWindowVisible()) {
		if (m_ListedItemsMap.RemoveKey(file)) {
			if (static_cast<size_t>(m_iIndex) < m_ListedItemsVector.size() && m_ListedItemsVector[static_cast<size_t>(m_iIndex)] == file)
				m_ListedItemsVector[static_cast<size_t>(m_iIndex)] = NULL;
			if (m_uDownloadRemoveBatchDepth != 0)
				m_bDownloadRemoveBatchPending = true;
		}
		return;
	}

	if (bWillReloadListLater) {
		if (m_ListedItemsMap.RemoveKey(file)) {
			m_ListedItemsVector[m_iIndex] = NULL;
			if (m_uDownloadRemoveBatchDepth != 0)
				m_bDownloadRemoveBatchPending = true;
			if (m_eSharedFilesBulkOperation != SharedFilesBulkOperationNone)
				m_bSharedFilesBulkRemovePending = true;
		}
		return;
	}

	if (!bDeletedFromDisk && m_eFilter == FilterType::FileSystem) {
		// in the file system view we usually don't need to remove a file, if it becomes unshared it will
		// still be visible as its still in the file system and the knownfile object doesn't get deleted neither
		// so to avoid having to reload the whole list we just update it instead of removing and re-finding
		UpdateFile(file, true, bDeletedFromDisk, m_iIndex);
		return;
	}

	if (m_eFilter != FilterType::FileSystem)
		// Don't save/reload list state if this is a file system view. Because all objects will be deleted and reloaded every time ReloadList is called.
		SaveListState(m_uFilterID, kSharedFilesViewState); // Save selections and scroll state

	SetRedraw(false); // Suspend painting
	m_ListedItemsMap.RemoveKey(file); // Remove the item from the map
	m_ListedItemsVector.erase(m_ListedItemsVector.begin() + m_iIndex); // Remove the item from the vector.
	UpdateListedItemsMapRange(m_iIndex, static_cast<int>(m_ListedItemsVector.size()) - 1);

	UpdateSharedFilesItemCount(*this, m_ListedItemsVector.size(), true); // Set current count for virtual list

	if (m_eFilter != FilterType::FileSystem) { // Don't save/reload list state if this is a file system view. Because all objects will be deleted and reloaded every time ReloadList is called.
		CSharedFilesSelectionRestoreGuard guard(*this);
		RestoreListState(m_uFilterID, kSharedFilesViewState, false); // Restore selections and scroll state
	}

	SetRedraw(true); // Resume painting
	RequestRowRedrawAsync(m_iIndex, static_cast<int>(m_ListedItemsVector.size()) - 1); // Coalesce updated rows.
	ShowFilesCount();
	theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails(true);
}

void CSharedFilesCtrl::UpdateFile(CKnownFile* file, const bool bUpdateFileSummary, const bool bDeletedFromDisk, const int iIndex)
{
	// Note: For thread safety, do not call this function directly from worker threads. Instead, post TM_SHAREDFILESCTRLUPDATEFILE message.

	int m_iIndex = iIndex;
	// If index isn't provided by the input parameter and also not found in m_ListedItemsMap
	if (iIndex == -1)
		m_iIndex = FindListedIndexByPointer(file);
	if (theApp.IsClosing() || theApp.emuledlg->activewnd != theApp.emuledlg->sharedfileswnd || !IsWindowVisible() || !file || m_iIndex < 0)
		return;

	if (m_iIndex >= static_cast<int>(m_ListedItemsVector.size()) || m_ListedItemsVector[m_iIndex] != file) {
		m_iIndex = FindListedIndexByPointer(file);
		if (m_iIndex < 0)
			return;
	}

	if (IsSharedFilesVisibleRemoveSnapshotActive()) {
		ApplySharedFilesBulkRemoveVisibleItemCount(false);
		return;
	}

	if (theApp.sharedfiles != NULL)
		theApp.sharedfiles->StoreWebSharedFileSnapshot(file);

	if (bDeletedFromDisk) {
		RequestSharedListRedrawForRange(m_iIndex, static_cast<int>(m_ListedItemsVector.size()) - 1);
	} else {
		bool bItemMoved = false;
		if (HasActiveSortOrder() && NeedsSortReposition(m_iIndex))
			bItemMoved = RepositionFileByCurrentSort(file, m_iIndex);

		if (!bItemMoved)
			RequestSharedListRedrawForRange(m_iIndex, m_iIndex);
		else
			m_iIndex = FindListedIndexByPointer(file);
	}

	if (bUpdateFileSummary && m_iIndex >= 0 && GetItemState(m_iIndex, LVIS_SELECTED))
		theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails(false);
}

void CSharedFilesCtrl::RemoveFromHistory(CKnownFile* toRemove, const bool bWillReloadListLater, const bool bNotifySharedFilesList) {
	if (theApp.IsClosing() || !toRemove)
		return;

	if (toRemove->IsKindOf(RUNTIME_CLASS(CPartFile)))
		theApp.emuledlg->transferwnd->GetDownloadList()->ClearCompleted(static_cast<CPartFile*>(toRemove));

	RemoveFile(toRemove, true, bWillReloadListLater); // We need to remove it first from virtual list, otherwise OnLvnGetDispInfo can try to query a deleted file and crashes.
	if (theApp.knownfiles)
		theApp.knownfiles->RemoveKnownFile(toRemove, bNotifySharedFilesList);
}


bool CSharedFilesCtrl::IsSharedFilesBulkOperationAction(UINT uAction) const
{
	switch (uAction) {
	case MP_REMOVE:
	case MPG_DELETE:
	case MP_UNSHAREFILE:
	case MP_UPDATE_METADATA:
	case MP_REMOVEFROMHISTORY:
	case MP_CLEARHISTORY:
	case MP_PRIOVERYLOW:
	case MP_PRIOLOW:
	case MP_PRIONORMAL:
	case MP_PRIOHIGH:
	case MP_PRIOVERYHIGH:
	case MP_PRIOAUTO:
	case kSharedFilesCommandToggleShareStatus:
		return true;
	}
	return false;
}

CString CSharedFilesCtrl::BuildSharedFileCommandKey(const CKnownFile* pFile) const
{
	return MakeSharedFilesCommandKey(pFile);
}


bool CSharedFilesCtrl::HasQueuedSharedFilesBulkItem(const CString &strKey)
{
	if (strKey.IsEmpty())
		return true;
	void *pQueued = NULL;
	return m_sharedFilesBulkQueuedKeys.Lookup(strKey, pQueued) != FALSE;
}

bool CSharedFilesCtrl::QueueSharedFilesBulkItem(CKnownFile *pFile)
{
	if (pFile == NULL)
		return false;
	CString strKey(MakeSharedFilesCommandKey(pFile));
	if (strKey.IsEmpty() || HasQueuedSharedFilesBulkItem(strKey))
		return false;
	m_sharedFilesBulkQueuedKeys.SetAt(strKey, reinterpret_cast<void*>(static_cast<UINT_PTR>(1)));

	SSharedFilesBulkItem *pItem = new SSharedFilesBulkItem();
	pItem->m_strKey = strKey;
	CString strFilePath;
	TryCopyShareableFileString(pFile, &CShareableFile::GetFilePath, strFilePath, MAX_PATH * 8);
	pItem->m_strFilePath = strFilePath;
	m_sharedFilesBulkItems.AddTail(pItem);
	m_sharedFilesBulkResolver.SetAt(strKey, pFile);
	return true;
}

bool CSharedFilesCtrl::QueueSharedFilesBulkKey(const CString &strKey)
{
	CString strTrimmed(strKey);
	strTrimmed.Trim();
	if (HasQueuedSharedFilesBulkItem(strTrimmed))
		return false;
	m_sharedFilesBulkQueuedKeys.SetAt(strTrimmed, reinterpret_cast<void*>(static_cast<UINT_PTR>(1)));

	SSharedFilesBulkItem *pItem = new SSharedFilesBulkItem();
	pItem->m_strKey = strTrimmed;
	m_sharedFilesBulkItems.AddTail(pItem);
	return true;
}

CKnownFile* CSharedFilesCtrl::ResolveSharedFilesBulkItem(const SSharedFilesBulkItem &item)
{
	if (item.m_strKey.IsEmpty())
		return NULL;

		void *pCached = NULL;
		if (m_sharedFilesBulkResolver.Lookup(item.m_strKey, pCached)) {
			CKnownFile *pFile = static_cast<CKnownFile*>(pCached);
			if (pFile != NULL && IsSharedFilesCommandKeyMatch(pFile, item.m_strKey))
				return pFile;
			m_sharedFilesBulkResolver.RemoveKey(item.m_strKey);
		}

	CString strHashKey;
	const int iKeySeparator = item.m_strKey.Find(_T('\t'));
	if (iKeySeparator > 0)
		strHashKey = item.m_strKey.Left(iKeySeparator);
	else
		strHashKey = item.m_strKey;
	strHashKey.Trim();
	if (strHashKey.GetLength() == 32) {
		uchar abyHash[16];
		if (strmd4(strHashKey, abyHash)) {
			CKnownFile *pFile = theApp.downloadqueue != NULL ? theApp.downloadqueue->GetFileByID(abyHash) : NULL;
			if (pFile == NULL && theApp.sharedfiles != NULL)
				pFile = theApp.sharedfiles->GetFileByID(abyHash);
			if (pFile == NULL && theApp.knownfiles != NULL)
				pFile = theApp.knownfiles->FindKnownFileByID(abyHash);
			if (IsSharedFilesCommandKeyMatch(pFile, item.m_strKey)) {
				m_sharedFilesBulkResolver.SetAt(item.m_strKey, pFile);
				return pFile;
			}
		}
	}

		const int iIndexed = FindListedIndexByCommandKey(item.m_strKey);
		if (iIndexed >= 0) {
			CKnownFile *pFile = m_ListedItemsVector[static_cast<size_t>(iIndexed)];
			if (pFile != NULL) {
				m_sharedFilesBulkResolver.SetAt(item.m_strKey, pFile);
				return pFile;
			}
	}

	for (size_t i = 0; i < m_ListedItemsVector.size(); ++i) {
		CKnownFile *pFile = m_ListedItemsVector[i];
		if (pFile != NULL && IsSharedFilesCommandKeyMatch(pFile, item.m_strKey)) {
			m_sharedFilesBulkResolver.SetAt(item.m_strKey, pFile);
			return pFile;
		}
	}
	return NULL;
}

bool CSharedFilesCtrl::StartSharedFilesBulkOperation(UINT uAction, const std::vector<CString> &vecItemKeys, uint64 uSequence, uint64 uCorrelationId)
{
	if (theApp.IsClosing() || !::IsWindow(m_hWnd) || !IsSharedFilesBulkOperationAction(uAction))
		return false;

	if (m_eSharedFilesBulkOperation != SharedFilesBulkOperationNone)
		ClearSharedFilesBulkOperation();

	switch (uAction) {
	case MP_REMOVE:
	case MPG_DELETE:
		if (!CanDeleteSelectedSharedFilesFromDisk())
			return true;
		m_eSharedFilesBulkOperation = SharedFilesBulkOperationDelete;
		if (LocMessageBox(_T("CONFIRM_FILEDELETE"), MB_ICONWARNING | MB_DEFBUTTON2 | MB_YESNO, 0) != IDYES) {
			m_eSharedFilesBulkOperation = SharedFilesBulkOperationNone;
			return true;
		}
		break;
	case MP_UNSHAREFILE:
		m_eSharedFilesBulkOperation = SharedFilesBulkOperationUnshare;
		break;
	case MP_UPDATE_METADATA:
		if (!CanUpdateSelectedSharedFilesMetadata())
			return true;
		m_eSharedFilesBulkOperation = SharedFilesBulkOperationUpdateMetadata;
		break;
	case MP_REMOVEFROMHISTORY:
		m_eSharedFilesBulkOperation = SharedFilesBulkOperationRemoveHistory;
		if (CDarkMode::MessageBox(GetResString(_T("FILE_HISTORY_REMOVE_QUESTION")), MB_YESNO | MB_ICONQUESTION) != IDYES) {
			m_eSharedFilesBulkOperation = SharedFilesBulkOperationNone;
			return true;
		}
		break;
	case MP_CLEARHISTORY:
		m_eSharedFilesBulkOperation = SharedFilesBulkOperationClearHistory;
		if (CDarkMode::MessageBox(GetResString(_T("FILE_HISTORY_PURGE_QUESTION")), MB_YESNO | MB_ICONQUESTION) != IDYES) {
			m_eSharedFilesBulkOperation = SharedFilesBulkOperationNone;
			return true;
		}
		break;
	case MP_PRIOVERYLOW:
	case MP_PRIOLOW:
	case MP_PRIONORMAL:
	case MP_PRIOHIGH:
	case MP_PRIOVERYHIGH:
	case MP_PRIOAUTO:
		m_eSharedFilesBulkOperation = SharedFilesBulkOperationSetPriority;
		break;
	case kSharedFilesCommandToggleShareStatus:
		m_eSharedFilesBulkOperation = SharedFilesBulkOperationToggleShareStatus;
		break;
	default:
		m_eSharedFilesBulkOperation = SharedFilesBulkOperationNone;
		return false;
	}

	m_uSharedFilesBulkAction = uAction;
	m_uSharedFilesBulkSequence = uSequence;
	m_uSharedFilesBulkCorrelationId = uCorrelationId;
	m_uSharedFilesBulkProcessed = 0;
	m_uSharedFilesBulkFailed = 0;
	m_uSharedFilesBulkStale = 0;
	m_uSharedFilesBulkTotal = 0;
	m_bSharedFilesBulkCollectingSelection = false;
	m_iSharedFilesBulkNextSelectionIndex = -1;
	m_uSharedFilesBulkSelectionQueued = 0;
	m_dwSharedFilesBulkStartedTick = ::GetTickCount();
	m_dwSharedFilesBulkLastProgressTick = m_dwSharedFilesBulkStartedTick;
	m_dwSharedFilesBulkLastCompactTick = m_dwSharedFilesBulkStartedTick;
	m_bSharedFilesBulkRemovePending = false;
	m_bSharedFilesBulkRemoveRowsDetached = false;
	m_uSharedFilesBulkListStateID = m_uFilterID;

	if (uAction == MP_CLEARHISTORY) {
		for (POSITION pos = theApp.knownfiles != NULL ? theApp.knownfiles->m_Files_map.GetStartPosition() : NULL; pos != NULL;) {
			CKnownFile *pFile = NULL;
			CCKey key;
			theApp.knownfiles->m_Files_map.GetNextAssoc(pos, key, pFile);
			if (pFile != NULL && theApp.sharedfiles != NULL && theApp.sharedfiles->GetFileByID(pFile->GetFileHash()) == NULL)
				QueueSharedFilesBulkItem(pFile);
		}
		if (theApp.knownfiles != NULL) {
			CSingleLock slDuplicatesLock(&theApp.knownfiles->m_csDuplicatesLock, TRUE);
			for (CKnownFileList::KnownFileList::iterator it = theApp.knownfiles->m_duplicateFileList.begin(); it != theApp.knownfiles->m_duplicateFileList.end(); ++it) {
				CKnownFile *pFile = *it;
				if (pFile != NULL && theApp.sharedfiles != NULL && theApp.sharedfiles->GetFileByID(pFile->GetFileHash()) == NULL)
					QueueSharedFilesBulkItem(pFile);
			}
		}
	} else if (uSequence == 0 && uCorrelationId == 0 && vecItemKeys.empty()) {
		const int iSelectedCount = GetSelectedCount();
		if (iSelectedCount >= BULK_OPERATION_MIN_ITEMS) {
			m_bSharedFilesBulkCollectingSelection = true;
			m_iSharedFilesBulkNextSelectionIndex = -1;
			m_uSharedFilesBulkSelectionQueued = 0;
			m_uSharedFilesBulkTotal = static_cast<UINT>(iSelectedCount);
		} else {
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				const int iItem = GetNextSelectedItem(pos);
				if (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size())
					continue;
				QueueSharedFilesBulkItem(m_ListedItemsVector[static_cast<size_t>(iItem)]);
			}
		}
	} else {
		for (std::vector<CString>::const_iterator it = vecItemKeys.begin(); it != vecItemKeys.end(); ++it)
			QueueSharedFilesBulkKey(*it);
		if (!m_sharedFilesBulkItems.IsEmpty() && !m_ListedItemsVector.empty()) {
			for (size_t i = 0; i < m_ListedItemsVector.size(); ++i) {
				CKnownFile *pFile = m_ListedItemsVector[i];
				if (pFile == NULL)
					continue;
				CString strCommandKey(BuildSharedFileCommandKey(pFile));
				void *pQueued = NULL;
				if (!strCommandKey.IsEmpty() && m_sharedFilesBulkQueuedKeys.Lookup(strCommandKey, pQueued))
					m_sharedFilesBulkResolver.SetAt(strCommandKey, pFile);
			}
		}
	}

	if (!m_bSharedFilesBulkCollectingSelection)
		m_uSharedFilesBulkTotal = static_cast<UINT>(m_sharedFilesBulkItems.GetCount());
	if (m_uSharedFilesBulkTotal == 0) {
		ClearSharedFilesBulkOperation();
		return true;
	}

	const bool bNeedsReload = m_uSharedFilesBulkTotal > 1 && (m_eSharedFilesBulkOperation == SharedFilesBulkOperationDelete || m_eSharedFilesBulkOperation == SharedFilesBulkOperationRemoveHistory || m_eSharedFilesBulkOperation == SharedFilesBulkOperationClearHistory);
	if (bNeedsReload) {
		if (m_eFilter != FilterType::FileSystem) {
			BeginListStateBatch(m_uSharedFilesBulkListStateID, kSharedFilesViewState);
			m_bSharedFilesBulkListStateBatchActive = true;
		}
	}

	if (bNeedsReload && !m_bSharedFilesBulkCollectingSelection)
		DetachSharedFilesBulkRemoveVisibleRows();

	if (bNeedsReload && theApp.DownloadValidator != NULL)
		theApp.DownloadValidator->CancelReloadMap();

	UpdateSharedFilesBulkOverlay();
	if (!PostSharedFilesBulkOperationMessage())
		ClearSharedFilesBulkOperation();
	return true;
}

bool CSharedFilesCtrl::PostSharedFilesBulkOperationMessage()
{
	if (m_bSharedFilesBulkPending || theApp.IsClosing() || !::IsWindow(m_hWnd))
		return false;
	m_bSharedFilesBulkPending = SetTimer(TimerSharedFilesBulkOperation, 1, NULL) != 0;
	if (!m_bSharedFilesBulkPending)
		m_bSharedFilesBulkPending = PostMessage(UM_SHARED_FILESCTRL_PROCESS_BULK_OPERATION, 0, 0) != FALSE;
	return m_bSharedFilesBulkPending;
}

void CSharedFilesCtrl::UpdateSharedFilesBulkOverlay()
{
	if (m_eSharedFilesBulkOperation == SharedFilesBulkOperationUpdateMetadata) {
		ShowFilesCount();
		return;
	}

	if (m_eSharedFilesBulkOperation == SharedFilesBulkOperationNone || m_uSharedFilesBulkTotal < BULK_OPERATION_MIN_ITEMS || (m_sharedFilesBulkItems.IsEmpty() && !m_bSharedFilesBulkCollectingSelection)) {
		HideOperationOverlay();
		if (theApp.emuledlg != NULL)
			theApp.emuledlg->RefreshActiveBulkOperationOverlays();
		return;
	}

	UINT uDone = m_uSharedFilesBulkProcessed + m_uSharedFilesBulkFailed + m_uSharedFilesBulkStale;
	if (uDone > m_uSharedFilesBulkTotal)
		uDone = m_uSharedFilesBulkTotal;
	CString strDetail;
	strDetail.Format(GetResString(_T("BULKOP_PROGRESS_DETAIL")), uDone, m_uSharedFilesBulkTotal);
	const bool bDeleteLike = m_eSharedFilesBulkOperation == SharedFilesBulkOperationDelete || m_eSharedFilesBulkOperation == SharedFilesBulkOperationRemoveHistory || m_eSharedFilesBulkOperation == SharedFilesBulkOperationClearHistory;
	UpdateOperationOverlay(GetResString(bDeleteLike ? _T("BULKOP_DELETE_DOWNLOADS_TITLE") : _T("BULKOP_UPDATE_DOWNLOADS_TITLE")), strDetail, uDone, m_uSharedFilesBulkTotal, true);
	if (bDeleteLike)
		ApplySharedFilesBulkRemoveVisibleItemCount(false);
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
}

bool CSharedFilesCtrl::IsDeleteLikeBulkOperationActive() const
{
	return m_eSharedFilesBulkOperation == SharedFilesBulkOperationDelete || m_eSharedFilesBulkOperation == SharedFilesBulkOperationRemoveHistory || m_eSharedFilesBulkOperation == SharedFilesBulkOperationClearHistory;
}

bool CSharedFilesCtrl::GetActiveSharedFilesBulkOperationProgress(bool& bDeleteLike, UINT& uDone, UINT& uTotal) const
{
	bDeleteLike = false;
	uDone = 0;
	uTotal = 0;
	if (m_eSharedFilesBulkOperation == SharedFilesBulkOperationNone || m_eSharedFilesBulkOperation == SharedFilesBulkOperationUpdateMetadata || m_uSharedFilesBulkTotal < BULK_OPERATION_MIN_ITEMS)
		return false;

	bDeleteLike = m_eSharedFilesBulkOperation == SharedFilesBulkOperationDelete || m_eSharedFilesBulkOperation == SharedFilesBulkOperationRemoveHistory || m_eSharedFilesBulkOperation == SharedFilesBulkOperationClearHistory;
	uTotal = m_uSharedFilesBulkTotal;
	uDone = m_uSharedFilesBulkProcessed + m_uSharedFilesBulkFailed + m_uSharedFilesBulkStale;
	if (uDone > uTotal)
		uDone = uTotal;
	return true;
}

bool CSharedFilesCtrl::GetActiveSharedFilesHashingProgress(UINT& uDone, UINT& uTotal) const
{
	uDone = 0;
	uTotal = 0;
	if (theApp.sharedfiles == NULL || !theApp.sharedfiles->IsStartupScanComplete() || (m_eSharedFilesBulkOperation != SharedFilesBulkOperationNone && m_eSharedFilesBulkOperation != SharedFilesBulkOperationUpdateMetadata))
		return false;

	const UINT uRemaining = ClampSharedFilesHashingCount(theApp.sharedfiles->GetHashingCount());
	if (uRemaining == 0 || m_uSharedFilesHashingOverlayTotal == 0)
		return false;

	uTotal = m_uSharedFilesHashingOverlayTotal;
	uDone = (uTotal >= uRemaining) ? (uTotal - uRemaining) : 0;
	return true;
}

bool CSharedFilesCtrl::GetActiveSharedFilesMetadataProgress(UINT& uDone, UINT& uTotal) const
{
	uDone = 0;
	uTotal = 0;
	if (theApp.sharedfiles == NULL || !theApp.sharedfiles->IsStartupScanComplete())
		return false;

	const UINT uRemaining = theApp.sharedfiles->GetMetaDataUpdateCount();
	if (uRemaining == 0 || m_uSharedFilesMetadataOverlayTotal < BULK_OPERATION_MIN_ITEMS)
		return false;

	uTotal = m_uSharedFilesMetadataOverlayTotal;
	uDone = (uTotal >= uRemaining) ? (uTotal - uRemaining) : 0;
	return true;
}

void CSharedFilesCtrl::UpdateSharedFilesHashingOverlay()
{
	if (theApp.sharedfiles == NULL || !::IsWindow(m_hWnd))
		return;

	if (!theApp.sharedfiles->IsStartupScanComplete() || (m_eSharedFilesBulkOperation != SharedFilesBulkOperationNone && m_eSharedFilesBulkOperation != SharedFilesBulkOperationUpdateMetadata)) {
		m_uSharedFilesHashingOverlayTotal = 0;
		m_uSharedFilesHashingOverlayLastRemaining = 0;
		return;
	}

	const UINT uRemaining = ClampSharedFilesHashingCount(theApp.sharedfiles->GetHashingCount());
	if (uRemaining == 0) {
		const bool bHadHashingOverlay = m_uSharedFilesHashingOverlayTotal != 0 || m_uSharedFilesHashingOverlayLastRemaining != 0;
		m_uSharedFilesHashingOverlayTotal = 0;
		m_uSharedFilesHashingOverlayLastRemaining = 0;
		if (bHadHashingOverlay) {
			if (theApp.emuledlg != NULL)
				theApp.emuledlg->RefreshActiveBulkOperationOverlays();
			else if (!m_bBackendDownloadRemoveOverlayActive)
				HideOperationOverlay();
		}
		return;
	}

	if (m_uSharedFilesHashingOverlayTotal == 0)
		m_uSharedFilesHashingOverlayTotal = uRemaining;
	else if (uRemaining > m_uSharedFilesHashingOverlayLastRemaining) {
		const UINT uAdded = uRemaining - m_uSharedFilesHashingOverlayLastRemaining;
		m_uSharedFilesHashingOverlayTotal = (UINT_MAX - m_uSharedFilesHashingOverlayTotal >= uAdded) ? (m_uSharedFilesHashingOverlayTotal + uAdded) : UINT_MAX;
	}
	m_uSharedFilesHashingOverlayLastRemaining = uRemaining;

	UINT uDone = 0;
	UINT uTotal = 0;
	if (!GetActiveSharedFilesHashingProgress(uDone, uTotal))
		return;

	CString strDetail;
	strDetail.Format(GetResString(_T("BULKOP_PROGRESS_FINAL_RELOAD_DETAIL")), uDone, uTotal);
	UpdateOperationOverlay(GetResString(_T("BULKOP_HASH_SHAREDFILES_TITLE")), strDetail, uDone, uTotal, false);
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
}

void CSharedFilesCtrl::UpdateSharedFilesMetadataOverlay()
{
	if (theApp.sharedfiles == NULL || !::IsWindow(m_hWnd))
		return;

	if (!theApp.sharedfiles->IsStartupScanComplete()) {
		m_uSharedFilesMetadataOverlayTotal = 0;
		m_uSharedFilesMetadataOverlayLastRemaining = 0;
		return;
	}

	const UINT uRemaining = theApp.sharedfiles->GetMetaDataUpdateCount();
	if (uRemaining == 0) {
		const bool bHadMetadataOverlay = m_uSharedFilesMetadataOverlayTotal != 0 || m_uSharedFilesMetadataOverlayLastRemaining != 0;
		m_uSharedFilesMetadataOverlayTotal = 0;
		m_uSharedFilesMetadataOverlayLastRemaining = 0;
		if (bHadMetadataOverlay) {
			if (theApp.emuledlg != NULL)
				theApp.emuledlg->RefreshActiveBulkOperationOverlays();
			else if (!m_bBackendDownloadRemoveOverlayActive && m_uSharedFilesHashingOverlayTotal == 0)
				HideOperationOverlay();
		}
		return;
	}

	if (m_uSharedFilesMetadataOverlayTotal == 0)
		m_uSharedFilesMetadataOverlayTotal = uRemaining;
	else if (uRemaining > m_uSharedFilesMetadataOverlayLastRemaining) {
		const UINT uAdded = uRemaining - m_uSharedFilesMetadataOverlayLastRemaining;
		m_uSharedFilesMetadataOverlayTotal = (UINT_MAX - m_uSharedFilesMetadataOverlayTotal >= uAdded) ? (m_uSharedFilesMetadataOverlayTotal + uAdded) : UINT_MAX;
	}
	m_uSharedFilesMetadataOverlayLastRemaining = uRemaining;

	UINT uDone = 0;
	UINT uTotal = 0;
	if (!GetActiveSharedFilesMetadataProgress(uDone, uTotal))
		return;

	CString strDetail;
	strDetail.Format(GetResString(_T("BULKOP_PROGRESS_DETAIL")), uDone, uTotal);
	UpdateOperationOverlay(GetResString(_T("BULKOP_UPDATE_METADATA_TITLE")), strDetail, uDone, uTotal, false);
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
}

void CSharedFilesCtrl::OnOperationOverlayCancel()
{
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->CancelActiveBulkOperations();
}

void CSharedFilesCtrl::ClearSharedFilesBulkOperation()
{
	const ESharedFilesBulkOperation eClearedOperation = m_eSharedFilesBulkOperation;
	const bool bDeleteLike = eClearedOperation == SharedFilesBulkOperationDelete || eClearedOperation == SharedFilesBulkOperationRemoveHistory || eClearedOperation == SharedFilesBulkOperationClearHistory;
	if (::IsWindow(m_hWnd))
		KillTimer(TimerSharedFilesBulkOperation);
	m_bSharedFilesBulkPending = false;
	m_bSharedFilesBulkCollectingSelection = false;
	m_iSharedFilesBulkNextSelectionIndex = -1;
	m_uSharedFilesBulkSelectionQueued = 0;
	const bool bHadRemovePending = m_bSharedFilesBulkRemovePending;
	const bool bHadDetachedRows = m_bSharedFilesBulkRemoveRowsDetached;
	m_bSharedFilesBulkRemovePending = false;
	m_bSharedFilesBulkRemoveVisibleSnapshotActive = false;
	m_uSharedFilesBulkRemoveVisibleSnapshotRows = 0;
	while (!m_sharedFilesBulkItems.IsEmpty())
		delete m_sharedFilesBulkItems.RemoveHead();
	m_sharedFilesBulkResolver.RemoveAll();
	m_sharedFilesBulkQueuedKeys.RemoveAll();
	if (bHadRemovePending && !bHadDetachedRows && ::IsWindow(m_hWnd)) {
		SetRedraw(false);
		CompactNullSharedFilesItems(_T("shared-bulk-remove-clear"));
		SetRedraw(true);
	}
	if (m_bSharedFilesBulkListStateBatchActive && ::IsWindow(m_hWnd)) {
		m_bSharedFilesBulkListStateBatchActive = false;
		EndListStateBatch(m_uSharedFilesBulkListStateID, kSharedFilesViewState, false);
	}
	if (bHadDetachedRows)
		ClearSharedFilesBulkRemoveHiddenRows(true);
	ClearBackendDownloadRemoveHiddenRows(true);
	m_eSharedFilesBulkOperation = SharedFilesBulkOperationNone;
	m_uSharedFilesBulkAction = 0;
	m_uSharedFilesBulkTotal = 0;
	m_uSharedFilesBulkProcessed = 0;
	m_uSharedFilesBulkFailed = 0;
	m_uSharedFilesBulkStale = 0;
	m_uSharedFilesBulkSequence = 0;
	m_uSharedFilesBulkCorrelationId = 0;
	m_dwSharedFilesBulkLastCompactTick = 0;
	if (bDeleteLike && (bHadRemovePending || bHadDetachedRows) && theApp.DownloadValidator != NULL && !theApp.IsClosing())
		theApp.DownloadValidator->QueueReloadMap();
	if (eClearedOperation != SharedFilesBulkOperationUpdateMetadata)
		HideOperationOverlay();
	if (theApp.emuledlg != NULL && !theApp.IsClosing())
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
}


void CSharedFilesCtrl::FinishSharedFilesBulkOperation()
{
	const ESharedFilesBulkOperation eFinishedOperation = m_eSharedFilesBulkOperation;
	const UINT uAction = m_uSharedFilesBulkAction;
	const UINT uProcessed = m_uSharedFilesBulkProcessed;
	const UINT uFailed = m_uSharedFilesBulkFailed;
	const UINT uStale = m_uSharedFilesBulkStale;
	const UINT uTotal = m_uSharedFilesBulkTotal;
	const uint64 uSequence = m_uSharedFilesBulkSequence;
	const uint64 uCorrelationId = m_uSharedFilesBulkCorrelationId;

	m_sharedFilesBulkResolver.RemoveAll();
	m_sharedFilesBulkQueuedKeys.RemoveAll();
	m_bSharedFilesBulkCollectingSelection = false;
	m_iSharedFilesBulkNextSelectionIndex = -1;
	m_uSharedFilesBulkSelectionQueued = 0;
	if (eFinishedOperation == SharedFilesBulkOperationDelete || eFinishedOperation == SharedFilesBulkOperationRemoveHistory || eFinishedOperation == SharedFilesBulkOperationClearHistory) {
		bool bCompacted = false;
		const bool bHadDetachedRows = m_bSharedFilesBulkRemoveRowsDetached;
		if (m_bSharedFilesBulkRemovePending && !bHadDetachedRows) {
			SetRedraw(false);
			bCompacted = CompactNullSharedFilesItems(_T("shared-bulk-remove-finish"));
		}
		if (m_bSharedFilesBulkListStateBatchActive) {
			m_bSharedFilesBulkListStateBatchActive = false;
			EndListStateBatch(m_uSharedFilesBulkListStateID, kSharedFilesViewState, false);
		}
		if (bHadDetachedRows) {
			ClearSharedFilesBulkRemoveHiddenRows(true);
			bCompacted = true;
		}
		if (::IsWindow(m_hWnd))
			SetRedraw(true);
		if (!bCompacted)
			RequestFullRedrawAsync();
	} else {
		if (m_bSharedFilesBulkListStateBatchActive) {
			m_bSharedFilesBulkListStateBatchActive = false;
			EndListStateBatch(m_uSharedFilesBulkListStateID, kSharedFilesViewState, false);
		}
		RequestFullRedrawAsync();
	}
	m_bSharedFilesBulkRemovePending = false;
	m_bSharedFilesBulkRemoveVisibleSnapshotActive = false;
	m_uSharedFilesBulkRemoveVisibleSnapshotRows = 0;

	AutoSelectItem();
	if (theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL) {
		theApp.emuledlg->sharedfileswnd->PostSelectedFilesDetailsAsync(true);
		theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged();
	}
	ShowFilesCount();
	if ((eFinishedOperation == SharedFilesBulkOperationDelete || eFinishedOperation == SharedFilesBulkOperationRemoveHistory || eFinishedOperation == SharedFilesBulkOperationClearHistory) && theApp.DownloadValidator != NULL && !theApp.IsClosing())
		theApp.DownloadValidator->QueueReloadMap();

	AddDebugLogLine(DLP_LOW, false, _T("Shared files bulk command completed. action=%u processed=%u stale=%u failed=%u total=%u elapsed=%u\n"), uAction, uProcessed, uStale, uFailed, uTotal, static_cast<DWORD>(::GetTickCount() - m_dwSharedFilesBulkStartedTick));
	theApp.QueueSharedFilesCommandStatusEvent(CemuleApp::ApplicationEventSharedFilesCommandCompleted, uAction, uProcessed, uFailed, uStale, uTotal, uSequence, uCorrelationId);

	m_eSharedFilesBulkOperation = SharedFilesBulkOperationNone;
	m_uSharedFilesBulkAction = 0;
	m_uSharedFilesBulkTotal = 0;
	m_uSharedFilesBulkProcessed = 0;
	m_uSharedFilesBulkFailed = 0;
	m_uSharedFilesBulkStale = 0;
	m_uSharedFilesBulkSequence = 0;
	m_uSharedFilesBulkCorrelationId = 0;
	m_dwSharedFilesBulkLastCompactTick = 0;
	if (eFinishedOperation != SharedFilesBulkOperationUpdateMetadata)
		HideOperationOverlay();
	if (theApp.emuledlg != NULL && !theApp.IsClosing())
		theApp.emuledlg->RefreshActiveBulkOperationOverlays();
}

void CSharedFilesCtrl::QueueSharedFilesBulkFailureEvent(const SSharedFilesBulkItem &item, LPCTSTR pszStage, DWORD dwError)
{
	CString strStage;
	strStage.Format(_T("%s key=%s"), pszStage != NULL ? pszStage : _T("unknown"), (LPCTSTR)item.m_strKey);
	theApp.QueueSharedFilesCommandFailureEvent(m_uSharedFilesBulkAction, strStage, item.m_strFilePath, dwError, m_uSharedFilesBulkSequence, m_uSharedFilesBulkCorrelationId);
}

bool CSharedFilesCtrl::ProcessSharedFilesBulkDelete(CKnownFile *pFile, const SSharedFilesBulkItem &item)
{
	if (pFile == NULL) {
		++m_uSharedFilesBulkStale;
		return false;
	}
	if (!IsCurrentSharedFileForSharedFilesAction(pFile)) {
		++m_uSharedFilesBulkStale;
		return false;
	}

	const CString strFilePath(pFile->GetFilePath());
	if (!ShellDeleteFile(strFilePath, false)) {
		const DWORD dwError = ::GetLastError();
		AddDebugLogLine(DLP_HIGH, false, _T("Shared files bulk delete failed. error=%lu path=%s\n"), dwError, (LPCTSTR)strFilePath);
		++m_uSharedFilesBulkFailed;
		QueueSharedFilesBulkFailureEvent(item, _T("delete-file"), dwError);
		return false;
	}

	m_sharedFilesBulkResolver.RemoveKey(item.m_strKey);
	if (theApp.sharedfiles != NULL)
		theApp.sharedfiles->RemoveFile(pFile, true, true);
	else
		RemoveFile(pFile, true, true);
	++m_uSharedFilesBulkProcessed;
	return true;
}

bool CSharedFilesCtrl::ProcessSharedFilesBulkUnshare(CKnownFile *pFile, const SSharedFilesBulkItem &item)
{
	if (pFile == NULL) {
		++m_uSharedFilesBulkStale;
		return false;
	}
	CShareableFile *pShareable = static_cast<CShareableFile*>(pFile);
	if (!CanUnshareFile(pShareable)) {
		++m_uSharedFilesBulkStale;
		return false;
	}
	if (!theApp.sharedfiles->ExcludeFile(pShareable->GetFilePath())) {
		++m_uSharedFilesBulkFailed;
		QueueSharedFilesBulkFailureEvent(item, _T("unshare-file"), ERROR_ACCESS_DENIED);
		return false;
	}
	UpdateFile(pFile);
	++m_uSharedFilesBulkProcessed;
	return true;
}

bool CSharedFilesCtrl::ProcessSharedFilesBulkUpdateMetadata(CKnownFile *pFile, const SSharedFilesBulkItem &item)
{
	if (pFile == NULL) {
		++m_uSharedFilesBulkStale;
		return false;
	}
	if (!IsCurrentSharedFileForSharedFilesAction(pFile)) {
		++m_uSharedFilesBulkStale;
		return false;
	}
	if (theApp.sharedfiles == NULL || !theApp.sharedfiles->QueueMetaDataUpdateForFile(pFile)) {
		++m_uSharedFilesBulkFailed;
		QueueSharedFilesBulkFailureEvent(item, _T("queue-metadata-update"), ERROR_NOT_READY);
		return false;
	}
	++m_uSharedFilesBulkProcessed;
	return true;
}

bool CSharedFilesCtrl::ProcessSharedFilesBulkRemoveHistory(CKnownFile *pFile, const SSharedFilesBulkItem &item)
{
	if (pFile == NULL) {
		++m_uSharedFilesBulkStale;
		return false;
	}
	const bool bCurrentSharedFile = theApp.sharedfiles != NULL && theApp.sharedfiles->GetFileByID(pFile->GetFileHash()) != NULL;
	const bool bHistoryDuplicate = IsExactDuplicateKnownFile(pFile);
	if (pFile->IsPartFile() || (bCurrentSharedFile && !bHistoryDuplicate)) {
		++m_uSharedFilesBulkStale;
		return false;
	}
	m_sharedFilesBulkResolver.RemoveKey(item.m_strKey);
	RemoveFromHistory(pFile, true, false);
	++m_uSharedFilesBulkProcessed;
	return true;
}

bool CSharedFilesCtrl::ProcessSharedFilesBulkSetPriority(CKnownFile *pFile, const SSharedFilesBulkItem &item)
{
	if (pFile == NULL) {
		++m_uSharedFilesBulkStale;
		return false;
	}
	pFile->SetAutoUpPriority(m_uSharedFilesBulkAction == MP_PRIOAUTO);
	switch (m_uSharedFilesBulkAction) {
	case MP_PRIOVERYLOW:
		pFile->SetUpPriority(PR_VERYLOW);
		break;
	case MP_PRIOLOW:
		pFile->SetUpPriority(PR_LOW);
		break;
	case MP_PRIONORMAL:
		pFile->SetUpPriority(PR_NORMAL);
		break;
	case MP_PRIOHIGH:
		pFile->SetUpPriority(PR_HIGH);
		break;
	case MP_PRIOVERYHIGH:
		pFile->SetUpPriority(PR_VERYHIGH);
		break;
	case MP_PRIOAUTO:
		pFile->UpdateAutoUpPriority();
		break;
	default:
		++m_uSharedFilesBulkFailed;
		QueueSharedFilesBulkFailureEvent(item, _T("invalid-priority"), ERROR_INVALID_FUNCTION);
		return false;
	}
	UpdateFile(pFile);
	++m_uSharedFilesBulkProcessed;
	return true;
}

bool CSharedFilesCtrl::ProcessSharedFilesBulkToggleShareStatus(CKnownFile *pFile, const SSharedFilesBulkItem& /*item*/)
{
	int iIndex = -1;
	if (pFile != NULL)
		iIndex = FindListedIndexByPointer(pFile);
	if (pFile == NULL || iIndex < 0) {
		++m_uSharedFilesBulkStale;
		return false;
	}
	CheckBoxClicked(iIndex);
	++m_uSharedFilesBulkProcessed;
	return true;
}

bool CSharedFilesCtrl::ProcessSharedFilesBulkSelectionQueueSlice(DWORD dwSliceStartTick, DWORD& dwSliceBudgetMs, UINT& uMaxItemsPerSlice, UINT& uProcessedInSlice)
{
	if (!m_bSharedFilesBulkCollectingSelection)
		return true;

	while (true) {
		const int iItem = GetNextItem(m_iSharedFilesBulkNextSelectionIndex, LVNI_SELECTED);
		if (iItem < 0) {
			m_bSharedFilesBulkCollectingSelection = false;
			m_iSharedFilesBulkNextSelectionIndex = -1;
			return true;
		}

		m_iSharedFilesBulkNextSelectionIndex = iItem;
		if (static_cast<size_t>(iItem) < m_ListedItemsVector.size()) {
			if (QueueSharedFilesBulkItem(m_ListedItemsVector[static_cast<size_t>(iItem)]))
				++m_uSharedFilesBulkSelectionQueued;
			else
				++m_uSharedFilesBulkStale;
		} else
			++m_uSharedFilesBulkStale;
		++uProcessedInSlice;

		if ((uProcessedInSlice & 0x0F) == 0)
			GetSharedFilesBulkSliceLimits(dwSliceBudgetMs, uMaxItemsPerSlice);
		const DWORD dwElapsed = static_cast<DWORD>(::GetTickCount() - dwSliceStartTick);
		if (uProcessedInSlice >= uMaxItemsPerSlice || (uProcessedInSlice != 0 && dwElapsed >= dwSliceBudgetMs))
			return false;
	}
}

bool CSharedFilesCtrl::ProcessSharedFilesBulkItem(SSharedFilesBulkItem &item)
{
	CKnownFile *pFile = ResolveSharedFilesBulkItem(item);
	switch (m_eSharedFilesBulkOperation) {
	case SharedFilesBulkOperationDelete:
		return ProcessSharedFilesBulkDelete(pFile, item);
	case SharedFilesBulkOperationUnshare:
		return ProcessSharedFilesBulkUnshare(pFile, item);
	case SharedFilesBulkOperationUpdateMetadata:
		return ProcessSharedFilesBulkUpdateMetadata(pFile, item);
	case SharedFilesBulkOperationRemoveHistory:
	case SharedFilesBulkOperationClearHistory:
		return ProcessSharedFilesBulkRemoveHistory(pFile, item);
	case SharedFilesBulkOperationSetPriority:
		return ProcessSharedFilesBulkSetPriority(pFile, item);
	case SharedFilesBulkOperationToggleShareStatus:
		return ProcessSharedFilesBulkToggleShareStatus(pFile, item);
	default:
		++m_uSharedFilesBulkFailed;
		QueueSharedFilesBulkFailureEvent(item, _T("invalid-operation"), ERROR_INVALID_FUNCTION);
		return false;
	}
}

LRESULT CSharedFilesCtrl::OnProcessSharedFilesBulkOperation(WPARAM, LPARAM)
{
	m_bSharedFilesBulkPending = false;
	if (theApp.IsClosing() || !::IsWindow(m_hWnd)) {
		ClearSharedFilesBulkOperation();
		return 0;
	}
	if (m_eSharedFilesBulkOperation == SharedFilesBulkOperationNone || (!m_bSharedFilesBulkCollectingSelection && m_sharedFilesBulkItems.IsEmpty()))
		return 0;

	const DWORD dwSliceStartTick = ::GetTickCount();
	DWORD dwSliceBudgetMs = 8;
	UINT uMaxItemsPerSlice = 192;
	GetSharedFilesBulkSliceLimits(dwSliceBudgetMs, uMaxItemsPerSlice);
	UINT uProcessedInSlice = 0;
	const bool bDeleteLike = m_eSharedFilesBulkOperation == SharedFilesBulkOperationDelete || m_eSharedFilesBulkOperation == SharedFilesBulkOperationRemoveHistory || m_eSharedFilesBulkOperation == SharedFilesBulkOperationClearHistory;
	const bool bSelectionQueueComplete = ProcessSharedFilesBulkSelectionQueueSlice(dwSliceStartTick, dwSliceBudgetMs, uMaxItemsPerSlice, uProcessedInSlice);
	if (bDeleteLike && bSelectionQueueComplete)
		DetachSharedFilesBulkRemoveVisibleRows();
	SetRedraw(false);
	while (bSelectionQueueComplete && !m_sharedFilesBulkItems.IsEmpty()) {
		SSharedFilesBulkItem *pItem = m_sharedFilesBulkItems.RemoveHead();
		if (pItem != NULL) {
			ProcessSharedFilesBulkItem(*pItem);
			delete pItem;
		}
		++uProcessedInSlice;

		const DWORD dwNow = ::GetTickCount();
		if (static_cast<DWORD>(dwNow - m_dwSharedFilesBulkLastProgressTick) >= theApp.GetTimeBudgetedProgressTraceMs(CemuleApp::TimeBudgetSharedFilesBulk)) {
			m_dwSharedFilesBulkLastProgressTick = dwNow;
			theApp.QueueSharedFilesCommandStatusEvent(CemuleApp::ApplicationEventSharedFilesCommandProgress, m_uSharedFilesBulkAction, m_uSharedFilesBulkProcessed, m_uSharedFilesBulkFailed, m_uSharedFilesBulkStale, m_uSharedFilesBulkTotal, m_uSharedFilesBulkSequence, m_uSharedFilesBulkCorrelationId);
		}

		if ((uProcessedInSlice & 0x0F) == 0)
			GetSharedFilesBulkSliceLimits(dwSliceBudgetMs, uMaxItemsPerSlice);
		const DWORD dwElapsed = static_cast<DWORD>(::GetTickCount() - dwSliceStartTick);
		if (uProcessedInSlice >= uMaxItemsPerSlice || (uProcessedInSlice != 0 && dwElapsed >= dwSliceBudgetMs))
			break;
	}
	bool bCompactedSlice = false;
	if (bDeleteLike && m_bSharedFilesBulkRemovePending && !m_bSharedFilesBulkRemoveRowsDetached) {
		bCompactedSlice = CompactNullSharedFilesItems(_T("shared-bulk-remove-slice"));
		m_bSharedFilesBulkRemovePending = false;
		m_dwSharedFilesBulkLastCompactTick = ::GetTickCount();
	}
	SetRedraw(true);
	if (bCompactedSlice)
		ShowFilesCount();

	DWORD dwSliceElapsed = 0;
	if (theApp.IsTimeBudgetHardExceeded(dwSliceStartTick, CemuleApp::TimeBudgetSharedFilesBulk, &dwSliceElapsed))
		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetSharedFilesBulk, _T("OnProcessSharedFilesBulkOperation"), dwSliceElapsed, uProcessedInSlice, m_sharedFilesBulkItems.GetCount());

	if (m_bSharedFilesBulkCollectingSelection || !m_sharedFilesBulkItems.IsEmpty()) {
		UpdateSharedFilesBulkOverlay();
		if (!PostSharedFilesBulkOperationMessage()) {
			AddDebugLogLine(DLP_HIGH, false, _T("Shared files bulk command aborted because continuation message could not be posted. action=%u processed=%u remaining=%d\n"), m_uSharedFilesBulkAction, m_uSharedFilesBulkProcessed, static_cast<int>(m_sharedFilesBulkItems.GetCount()));
			ClearSharedFilesBulkOperation();
		}
	} else
		FinishSharedFilesBulkOperation();
	return 0;
}
bool CSharedFilesCtrl::ProcessFileSystemReloadWorkerItem(const CemuleApp::SWorkerTopologyItem &item)
{
	if (item.m_eType != CemuleApp::WorkerTopologyItemFileSystemReload || item.m_strStage != _T("shared-files-filesystem-reload") || item.m_lWorkerGeneration <= 0 || item.m_hNotifyWnd == NULL || item.m_strPayload.IsEmpty())
		return false;

	SSharedFilesFileSystemReloadResult* pResult = new SSharedFilesFileSystemReloadResult();
	pResult->hWnd = item.m_hNotifyWnd;
	pResult->lGeneration = item.m_lWorkerGeneration;
	pResult->uReloadToken = item.m_uCorrelationId;
	pResult->strDirectory = item.m_strPayload;

	BuildSharedFilesFileSystemReloadResult(pResult);

	if (pResult->hWnd != NULL && ::IsWindow(pResult->hWnd) && ::PostMessage(pResult->hWnd, UM_SHARED_FILESCTRL_FILESYSTEM_RELOAD_READY, 0, reinterpret_cast<LPARAM>(pResult)))
		return true;

	theApp.CompleteSharedFilesFileSystemReload(pResult->hWnd, pResult->lGeneration, pResult->uReloadToken);
	delete pResult;
	return false;
}

bool CSharedFilesCtrl::IsFileSystemReloadActive() const
{
	const bool bActive = theApp.IsSharedFilesFileSystemReloadActive(m_hWnd);
	InterlockedExchange(const_cast<LONG*>(&m_lFileSystemReloadActive), bActive ? 1 : 0);
	return bActive;
}

void CSharedFilesCtrl::StartFileSystemReloadJob(const CString &strDirectory)
{
	if (theApp.IsClosing() || m_hWnd == NULL || !::IsWindow(m_hWnd) || strDirectory.IsEmpty())
		return;

	if (IsFileSystemReloadActive()) {
		theApp.CancelSharedFilesFileSystemReload(m_hWnd);
		InterlockedExchange(&m_lFileSystemReloadActive, 0);
	}

	const LONG lGeneration = InterlockedIncrement(&m_lFileSystemReloadGeneration);
	uint64 uReloadToken = 0;
	if (!theApp.BeginSharedFilesFileSystemReload(m_hWnd, lGeneration, &uReloadToken)) {
		InterlockedExchange(&m_lFileSystemReloadActive, 0);
		AddDebugLogLine(DLP_LOW, false, _T("Shared files file-system reload could not be started. directory=\"%s\"\n"), (LPCTSTR)strDirectory);
		return;
	}
	InterlockedExchange(&m_lFileSystemReloadActive, 1);
	if (!theApp.QueueSharedFilesFileSystemReloadWorkerJob(m_hWnd, lGeneration, uReloadToken, strDirectory)) {
		if (theApp.CompleteSharedFilesFileSystemReload(m_hWnd, lGeneration, uReloadToken))
			InterlockedExchange(&m_lFileSystemReloadActive, 0);
		AddDebugLogLine(DLP_HIGH, false, _T("Shared files file-system reload could not be queued. directory=\"%s\"\n"), (LPCTSTR)strDirectory);
		return;
	}
}

LRESULT CSharedFilesCtrl::OnFileSystemReloadReady(WPARAM, LPARAM lParam)
{
	SSharedFilesFileSystemReloadResult* pResult = reinterpret_cast<SSharedFilesFileSystemReloadResult*>(lParam);
	if (pResult == NULL)
		return 0;

	const LONG lCurrentGeneration = InterlockedCompareExchange(&m_lFileSystemReloadGeneration, 0, 0);
	const bool bCurrentResult = pResult->lGeneration == lCurrentGeneration && theApp.CompleteSharedFilesFileSystemReload(pResult->hWnd, pResult->lGeneration, pResult->uReloadToken);
	if (bCurrentResult)
		InterlockedExchange(&m_lFileSystemReloadActive, 0);
	const bool bStaleResult = !bCurrentResult || theApp.IsClosing() || theApp.emuledlg == NULL || theApp.emuledlg->sharedfileswnd == NULL || theApp.emuledlg->activewnd != theApp.emuledlg->sharedfileswnd || m_eFilter != FilterType::FileSystem || m_pDirectoryFilter == NULL || pResult->strDirectory.CompareNoCase(m_pDirectoryFilter->m_strFullPath) != 0;
	if (bStaleResult) {
		delete pResult;
		return 0;
	}

	const DWORD dwSliceStart = ::GetTickCount();
	const bool bInitializing = (m_iDataSize == -1);
	if (bInitializing) {
		m_iDataSize = NextPrime(theApp.knownfiles->GetCount() + theApp.sharedfiles->GetCount() + 10000);
		m_ListedItemsVector.reserve(m_iDataSize);
		m_ListedItemsMap.InitHashTable(m_iDataSize);
	}

	SetRedraw(false);
	if (!bInitializing) {
		SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
		SetSelectionMark(-1);
	}
	m_ListedItemsVector.clear();
	m_ListedItemsMap.RemoveAll();

	CTempShareableFilesMap mapReusableTempFiles;
	while (!liTempShareableFilesInDir.IsEmpty()) {
		CShareableFile* pTempShareableFile = liTempShareableFilesInDir.RemoveHead();
		if (pTempShareableFile != NULL)
			mapReusableTempFiles.SetAt(BuildTempShareableFileKey(pTempShareableFile->GetFilePath()), pTempShareableFile);
	}

	if (pResult->dwLastError != ERROR_SUCCESS && pResult->dwLastError != ERROR_FILE_NOT_FOUND)
		DebugLogError(_T("Failed to find files for SharedFilesListCtrl in %s, %s"), (LPCTSTR)EscPercent(pResult->strDirectory), (LPCTSTR)EscPercent(GetErrorMessage(pResult->dwLastError)));

	UINT uProcessed = 0;
	for (size_t i = 0; i < pResult->vecEntries.size(); ++i) {
		const SSharedFilesFileSystemEntry& entry = pResult->vecEntries[i];
		CShareableFile* pTempShareableFile = NULL;
		const CString strTempShareableFileKey(BuildTempShareableFileKey(entry.strFilePath));
		if (mapReusableTempFiles.Lookup(strTempShareableFileKey, pTempShareableFile) && CanReuseTempShareableFile(pTempShareableFile, entry.ullFileSize, entry.bHasLastWriteTime, entry.tLastWriteTime))
			mapReusableTempFiles.RemoveKey(strTempShareableFileKey);
		else
			pTempShareableFile = new CShareableFile();

		RefreshTempShareableFile(*pTempShareableFile, entry.strFilePath, entry.strFileName, entry.strDirectory, entry.ullFileSize, entry.bHasLastWriteTime, entry.tLastWriteTime);
		liTempShareableFilesInDir.AddTail(pTempShareableFile);
		CKnownFile* pKnownFile = static_cast<CKnownFile*>(pTempShareableFile);
		if (!IsFilteredOut(pKnownFile))
			m_ListedItemsVector.push_back(pKnownFile);
		++uProcessed;
	}

	DeleteTempShareableFilesMap(mapReusableTempFiles);

	CombinedSort(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), SortFunc);
	RebuildListedItemsMap();
	if (m_bSharedFilesBulkRemoveRowsDetached || m_bBackendDownloadRemoveRowsDetached) {
		if (m_bSharedFilesBulkRemoveRowsDetached) {
			m_bSharedFilesBulkRemoveVisibleSnapshotActive = true;
			m_uSharedFilesBulkRemoveVisibleSnapshotRows = m_ListedItemsVector.size();
		}
		if (m_bBackendDownloadRemoveRowsDetached) {
			m_bBackendDownloadRemoveVisibleSnapshotActive = true;
			m_uBackendDownloadRemoveVisibleSnapshotRows = m_ListedItemsVector.size();
		}
	}
	UpdateSharedFilesItemCount(*this, m_ListedItemsVector.size());
	theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails(false);
	ShowFilesCount();
	SetRedraw(true);
	if (IsSharedFilesVisibleRemoveSnapshotActive())
		ApplySharedFilesBulkRemoveVisibleItemCount(false);
	Invalidate();

	DWORD dwSliceElapsed = 0;
	if (theApp.IsTimeBudgetHardExceeded(dwSliceStart, CemuleApp::TimeBudgetSharedFilesReload, &dwSliceElapsed))
		theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetSharedFilesReload, _T("SharedFilesCtrl::OnFileSystemReloadReady"), dwSliceElapsed, uProcessed, 0);

	delete pResult;
	return 0;
}

void CSharedFilesCtrl::ReloadList(const bool bSortCurrentList, const EListStateField LsfFlag)
{
	ReloadListInternal(bSortCurrentList, LsfFlag, false);
}

void CSharedFilesCtrl::ReloadListInternal(const bool bSortCurrentList, const EListStateField LsfFlag, const bool bAllowHidden)
{
	if (theApp.IsClosing() || theApp.emuledlg == NULL || theApp.emuledlg->sharedfileswnd == NULL || (!bAllowHidden && (theApp.emuledlg->activewnd != theApp.emuledlg->sharedfileswnd || !IsWindowVisible())))
		return;

	if (IsSharedFilesVisibleRemoveSnapshotActive() && (m_bSharedFilesBulkRemoveVisibleSnapshotActive || m_bBackendDownloadRemoveVisibleSnapshotActive)) {
		DetachSharedFilesVisibleRemoveRows();
		ApplySharedFilesBulkRemoveVisibleItemCount(false);
		return;
	}

	CCKey bufKey;
	bool bInitializing = (m_iDataSize == -1); // Check if this is the first call to ReloadList
	bool bReloadFilterPassthrough = false;
	const uint32 uNextFilterID = GetFilterId();
	const uint32 uPreviousFilterID = m_uFilterID;
	const bool bListIdentityChanged = !bInitializing && uPreviousFilterID != uNextFilterID;

	// Initializing the vector and map
	if (bInitializing) {
		m_iDataSize = NextPrime(theApp.knownfiles->GetCount() + theApp.sharedfiles->GetCount() + 10000); // Any reasonable prime number for the initial size.
		m_ListedItemsVector.reserve(m_iDataSize);
		m_ListedItemsMap.InitHashTable(m_iDataSize);
	} else {
		const bool bPreviousFilterWasFileSystem = uPreviousFilterID / 1000 == static_cast<uint32>(FilterType::FileSystem);
		if (!bPreviousFilterWasFileSystem)
			SaveListState(uPreviousFilterID, LsfFlag);
	}
	m_uFilterID = uNextFilterID;

	if (!bSortCurrentList && m_eFilter == FilterType::FileSystem && m_pDirectoryFilter != NULL && !m_pDirectoryFilter->m_strFullPath.IsEmpty()) {
		StartFileSystemReloadJob(m_pDirectoryFilter->m_strFullPath);
		return;
	}

	std::vector<CKnownFile*>& aNewListedItems = m_vecSharedFilesReloadScratch;
	if (!bSortCurrentList) {
		aNewListedItems.clear();
		if (m_iDataSize > 0 && aNewListedItems.capacity() < static_cast<size_t>(m_iDataSize))
			aNewListedItems.reserve(static_cast<size_t>(m_iDataSize));

		if (m_eFilter != FilterType::FileSystem || m_pDirectoryFilter == NULL || m_pDirectoryFilter->m_strFullPath.IsEmpty())
			DeleteTempShareableFilesList(liTempShareableFilesInDir);

		// List part files if "All Shared Files", "Incomplete Files" or "File History" views require them.
		// m_pDirectoryFilter can be NULL while loading the window first time. So we need to consider this case, too.
		const CDirectoryItem* pSelectedTreeFilter = (theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL)
			? theApp.emuledlg->sharedfileswnd->m_ctlSharedDirTree.GetSelectedFilter()
			: NULL;
		const ESpecialDirectoryItems eSelectedTreeItemType = (pSelectedTreeFilter != NULL) ? pSelectedTreeFilter->m_eItemType : SDI_ALL;
		bReloadFilterPassthrough = theApp.emuledlg->sharedfileswnd->m_astrFilter.IsEmpty() && (m_pDirectoryFilter == NULL || m_pDirectoryFilter->m_eItemType == SDI_ALL || m_pDirectoryFilter->m_eItemType == SDI_ALLHISTORY || m_pDirectoryFilter->m_eItemType == SDI_DUP);
		if ((eSelectedTreeItemType == SDI_ALL || eSelectedTreeItemType == SDI_TEMP) || (thePrefs.GetFileHistoryShowPart() && m_eFilter == FilterType::History)) {
			//Add all active part files from download list. This way will include 0bytes parts too.
			CArray<CPartFile*, CPartFile*> partlist;
			theApp.emuledlg->transferwnd->GetDownloadList()->GetDisplayedPartFiles(&partlist);
			for (INT_PTR i = 0; i < partlist.GetCount(); ++i) {
				CPartFile* pPartFile = partlist[i];
				if (pPartFile != NULL && (bReloadFilterPassthrough || !IsFilteredOut(pPartFile)))
					aNewListedItems.push_back(pPartFile);
			}
		}

		if (m_eFilter == FilterType::FileSystem) {
			// Valid File System reloads are handled asynchronously before this point.
		} else {
			CArray<CKnownFile*, CKnownFile*> arSharedFiles;
			CKnownFilesMap mapSharedHistoryFiles;
			if (m_eFilter == FilterType::Shared || m_eFilter == FilterType::History) {
				CSingleLock listlock(&theApp.sharedfiles->m_mutWriteList, TRUE);
				const INT_PTR iSharedFileCount = theApp.sharedfiles->m_Files_map.GetCount();
				if (iSharedFileCount > 0) {
					arSharedFiles.SetSize(0, iSharedFileCount);
					if (m_eFilter == FilterType::History) {
						INT_PTR iHashSize = iSharedFileCount * 2 + 1;
						if (iHashSize > INT_MAX)
							iHashSize = INT_MAX;
						mapSharedHistoryFiles.InitHashTable(static_cast<UINT>(NextPrime(static_cast<int>(iHashSize))));
					}
				}
				for (const CKnownFilesMap::CPair* pair = theApp.sharedfiles->m_Files_map.PGetFirstAssoc(); pair != NULL; pair = theApp.sharedfiles->m_Files_map.PGetNextAssoc(pair)) {
					if (pair->value != NULL) {
						arSharedFiles.Add(pair->value);
						if (m_eFilter == FilterType::History)
							mapSharedHistoryFiles.SetAt(CCKey(pair->value->GetFileHash()), pair->value);
					}
				}
			}

			if (m_eFilter == FilterType::Shared || (thePrefs.GetFileHistoryShowShared() && m_eFilter == FilterType::History)) {
				for (INT_PTR i = 0; i < arSharedFiles.GetCount(); ++i) {
					CKnownFile* pKF = arSharedFiles[i];
					// m_Files_map only contains part files with downloaded parts, we want to show all part files including 0bytes if GetFileHistoryShowPart is true, so exclude parts for this loop.
					if (pKF && !theApp.downloadqueue->IsPartFile(pKF) && (bReloadFilterPassthrough || !IsFilteredOut(pKF)))
						aNewListedItems.push_back(pKF);
				}
			}

			if (m_eFilter == FilterType::History) {
				// Known files
				for (POSITION pos = theApp.knownfiles->m_Files_map.GetStartPosition(); pos != NULL;) {
					CKnownFile* cur_file = NULL;
					theApp.knownfiles->m_Files_map.GetNextAssoc(pos, bufKey, cur_file);
					if (cur_file != NULL && (bReloadFilterPassthrough || !IsFilteredOut(cur_file))) {
						CKnownFile* pSharedHistoryFile = NULL;
						if (!mapSharedHistoryFiles.Lookup(CCKey(cur_file->GetFileHash()), pSharedHistoryFile))
							aNewListedItems.push_back(cur_file);
					}
				}
			}

			if (m_eFilter == FilterType::Duplicate || (m_eFilter == FilterType::History && thePrefs.GetFileHistoryShowDuplicate())) {
				// Duplicate shared files
				CSingleLock slDuplicatesLock(&theApp.knownfiles->m_csDuplicatesLock, TRUE);
				for (auto&& duplicateFile : theApp.knownfiles->m_duplicateFileList)
					if (duplicateFile != NULL && (bReloadFilterPassthrough || !IsFilteredOut(duplicateFile)))
						aNewListedItems.push_back(duplicateFile);
			}
		}
	}

	if (!bSortCurrentList)
		CombinedSort(aNewListedItems.begin(), aNewListedItems.end(), SortFunc);

	SetRedraw(false); // Suspend painting while the visible model is committed.
	if (bListIdentityChanged && (LsfFlag & LSF_SELECTION) != 0) {
		SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
		SetSelectionMark(-1);
	}

	if (!bSortCurrentList) {
		m_ListedItemsVector.swap(aNewListedItems);
		aNewListedItems.clear();
		m_ListedItemsMap.RemoveAll();
	} else
		CombinedSort(m_ListedItemsVector.begin(), m_ListedItemsVector.end(), SortFunc);
	RebuildListedItemsMap();

	if (m_bSharedFilesBulkRemoveRowsDetached || m_bBackendDownloadRemoveRowsDetached) {
		if (m_bSharedFilesBulkRemoveRowsDetached) {
			m_bSharedFilesBulkRemoveVisibleSnapshotActive = true;
			m_uSharedFilesBulkRemoveVisibleSnapshotRows = m_ListedItemsVector.size();
		}
		if (m_bBackendDownloadRemoveRowsDetached) {
			m_bBackendDownloadRemoveVisibleSnapshotActive = true;
			m_uBackendDownloadRemoveVisibleSnapshotRows = m_ListedItemsVector.size();
		}
	}

	UpdateSharedFilesItemCount(*this, m_ListedItemsVector.size()); // Set current count for the virtual list

	if (!bInitializing && m_eFilter != FilterType::FileSystem) {
		CSharedFilesSelectionRestoreGuard guard(*this);
		RestoreListState(m_uFilterID, LsfFlag, false);
	}

	theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails(false);
	ShowFilesCount();
	SetRedraw(true); // Resume painting
	if (IsSharedFilesVisibleRemoveSnapshotActive())
		ApplySharedFilesBulkRemoveVisibleItemCount(false);
	RequestFullRedrawAsync(); // Coalesce list redraw.
}

void CSharedFilesCtrl::ReloadListForActivation(const EListStateField LsfFlag)
{
	if (theApp.IsClosing() || theApp.emuledlg == NULL || theApp.emuledlg->sharedfileswnd == NULL || !::IsWindow(m_hWnd))
		return;

	HidePersistentInfoTip(true);
	m_pHighlightedItem = NULL;
	const bool bAsyncFileSystemReload = m_eFilter == FilterType::FileSystem && m_pDirectoryFilter != NULL && !m_pDirectoryFilter->m_strFullPath.IsEmpty();
	ReloadListInternal(false, LsfFlag, true);

	if (bAsyncFileSystemReload || IsSharedFilesVisibleRemoveSnapshotActive()) {
		UpdateSharedFilesItemCount(*this, 0, true);
		SetSelectionMark(-1);
		theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails(true);
	}
}

void CSharedFilesCtrl::ReloadListFromApplicationEvent(const bool bSortCurrentList, const EListStateField LsfFlag)
{
	if (m_uSharedFilesListReloadDeferDepth != 0) {
		if (!m_bSharedFilesListReloadDeferred) {
			m_bSharedFilesListReloadDeferred = true;
			m_bSharedFilesListReloadDeferredSortCurrentList = bSortCurrentList;
			m_eSharedFilesListReloadDeferredState = LsfFlag;
		} else {
			m_bSharedFilesListReloadDeferredSortCurrentList = m_bSharedFilesListReloadDeferredSortCurrentList && bSortCurrentList;
			m_eSharedFilesListReloadDeferredState = static_cast<EListStateField>(m_eSharedFilesListReloadDeferredState | LsfFlag);
		}
		return;
	}

	ReloadList(bSortCurrentList, LsfFlag);
}

// Index map after vector changes
void CSharedFilesCtrl::RebuildListedItemsMap()
{
	m_ListedItemsMap.RemoveAll();

	if (m_ListedItemsVector.empty()) {
		return;
	}

	for (int i = 0; i < static_cast<int>(m_ListedItemsVector.size()); ++i) {
		if (m_ListedItemsVector[i] != NULL) // Skip NULL entries that may exist temporarily during removal operations
			m_ListedItemsMap[m_ListedItemsVector[i]] = i;
	}
}

int CSharedFilesCtrl::FindListedIndexByPointer(CKnownFile* pFile) const
{
	if (pFile == NULL)
		return -1;

	int iIndex = -1;
	if (const_cast<CSharedFilesCtrl*>(this)->m_ListedItemsMap.Lookup(pFile, iIndex) && iIndex >= 0 && static_cast<size_t>(iIndex) < m_ListedItemsVector.size() && m_ListedItemsVector[static_cast<size_t>(iIndex)] == pFile)
		return iIndex;

	return -1;
}

int CSharedFilesCtrl::FindListedIndexByCommandKey(const CString& strCommandKey) const
{
	if (strCommandKey.IsEmpty())
		return -1;

	for (size_t i = 0; i < m_ListedItemsVector.size(); ++i) {
		CKnownFile *pFile = m_ListedItemsVector[i];
		if (IsSharedFilesCommandKeyMatch(pFile, strCommandKey))
			return static_cast<int>(i);
	}

	return -1;
}

void CSharedFilesCtrl::UpdateListedItemsMapRange(int iStartIndex, int iEndIndex)
{
	if (iStartIndex < 0 || iEndIndex < iStartIndex || iStartIndex >= static_cast<int>(m_ListedItemsVector.size()))
		return;

	iEndIndex = min(iEndIndex, static_cast<int>(m_ListedItemsVector.size()) - 1);

	for (int i = iStartIndex; i <= iEndIndex; ++i) {
		if (m_ListedItemsVector[i] != NULL)
			m_ListedItemsMap[m_ListedItemsVector[i]] = i;
	}
}

bool CSharedFilesCtrl::SortFunc(const CKnownFile* first, const CKnownFile* second)
{
	if (first == second)
		return false;
	if (first == NULL || second == NULL)
		return first != NULL;
	return SortProc(reinterpret_cast<LPARAM>(first), reinterpret_cast<LPARAM>(second), m_pSortParam) < 0;
}

bool CSharedFilesCtrl::IsSharedFilesBulkDeleteLikeOperation() const
{
	return m_eSharedFilesBulkOperation == SharedFilesBulkOperationDelete || m_eSharedFilesBulkOperation == SharedFilesBulkOperationRemoveHistory || m_eSharedFilesBulkOperation == SharedFilesBulkOperationClearHistory;
}

bool CSharedFilesCtrl::IsHiddenBySharedFilesBulkRemove(const CKnownFile *pFile) const
{
	if (!m_bSharedFilesBulkRemoveRowsDetached || pFile == NULL)
		return false;

	CString strKey(BuildSharedFileCommandKey(pFile));
	if (strKey.IsEmpty())
		return false;

	void *pQueued = NULL;
	return m_sharedFilesBulkQueuedKeys.Lookup(strKey, pQueued) != FALSE;
}

bool CSharedFilesCtrl::IsHiddenByBackendDownloadRemove(const CKnownFile *pFile) const
{
	if (pFile == NULL || m_backendDownloadRemoveHiddenRows.GetCount() == 0)
		return false;

	CString strHash(md4str(pFile->GetFileHash()));
	if (strHash.IsEmpty())
		return false;

	void *pHidden = NULL;
	return m_backendDownloadRemoveHiddenRows.Lookup(strHash, pHidden) != FALSE;
}

bool CSharedFilesCtrl::IsHiddenBySharedFilesVisibleRemove(const CKnownFile *pFile) const
{
	return IsHiddenBySharedFilesBulkRemove(pFile) || IsHiddenByBackendDownloadRemove(pFile);
}

bool CSharedFilesCtrl::IsSharedFilesBulkRemoveSnapshotActive() const
{
	return m_bSharedFilesBulkRemoveRowsDetached && (m_bSharedFilesBulkRemoveVisibleSnapshotActive || IsSharedFilesBulkDeleteLikeOperation() || m_bSharedFilesBulkListStateBatchActive);
}

bool CSharedFilesCtrl::IsBackendDownloadRemoveSnapshotActive() const
{
	return m_bBackendDownloadRemoveRowsDetached && (m_bBackendDownloadRemoveVisibleSnapshotActive || m_bBackendDownloadRemoveOverlayActive);
}

bool CSharedFilesCtrl::IsSharedFilesVisibleRemoveSnapshotActive() const
{
	return IsSharedFilesBulkRemoveSnapshotActive() || IsBackendDownloadRemoveSnapshotActive();
}

bool CSharedFilesCtrl::QueueBackendDownloadRemoveHiddenHash(LPCTSTR pszHash)
{
	if (pszHash == NULL || pszHash[0] == _T('\0'))
		return false;

	CString strHash(pszHash);
	strHash.Trim();
	if (strHash.IsEmpty())
		return false;

	uchar abyHash[16];
	if (!strmd4(strHash, abyHash))
		return false;

	const CString strCanonicalHash(md4str(abyHash));
	if (strCanonicalHash.IsEmpty())
		return false;

	void *pExisting = NULL;
	if (m_backendDownloadRemoveHiddenRows.Lookup(strCanonicalHash, pExisting))
		return false;

	m_backendDownloadRemoveHiddenRows.SetAt(strCanonicalHash, reinterpret_cast<void*>(static_cast<UINT_PTR>(1)));
	return true;
}

void CSharedFilesCtrl::ApplySharedFilesBulkRemoveVisibleItemCount(bool bForceFrameUpdate)
{
	if (theApp.IsClosing() || !::IsWindow(m_hWnd) || !IsSharedFilesVisibleRemoveSnapshotActive())
		return;

	size_t uSnapshotRows = m_ListedItemsVector.size();
	if (m_bBackendDownloadRemoveVisibleSnapshotActive)
		uSnapshotRows = m_uBackendDownloadRemoveVisibleSnapshotRows;
	else if (m_bSharedFilesBulkRemoveVisibleSnapshotActive)
		uSnapshotRows = m_uSharedFilesBulkRemoveVisibleSnapshotRows;
	const size_t uClampedCount = uSnapshotRows > static_cast<size_t>(INT_MAX) ? static_cast<size_t>(INT_MAX) : uSnapshotRows;
	const int iExpectedCount = static_cast<int>(uClampedCount);
	const bool bCountChanged = GetItemCount() != iExpectedCount;
	const bool bZeroCountScrollReset = iExpectedCount == 0;
	if (!bForceFrameUpdate && !bCountChanged && !bZeroCountScrollReset)
		return;

	if (iExpectedCount == 0) {
		if (bForceFrameUpdate || bCountChanged) {
			SetItemState(-1, 0, LVIS_FOCUSED | LVIS_SELECTED);
			SetSelectionMark(-1);
		}
		SetItemCountEx(0, 0);
		SCROLLINFO si = { sizeof(si) };
		si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
		si.nMin = 0;
		si.nMax = 0;
		si.nPage = 1;
		si.nPos = 0;
		SetScrollInfo(SB_VERT, &si, TRUE);
		ShowScrollBar(SB_VERT, FALSE);
	}
	else
		SetItemCountAndKeepPageFilled(uClampedCount, 0);

	if (bCountChanged || bForceFrameUpdate)
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_NOERASE | RDW_NOCHILDREN);
}

void CSharedFilesCtrl::DetachSharedFilesVisibleRemoveRows()
{
	if ((!m_bSharedFilesBulkRemoveRowsDetached && !m_bBackendDownloadRemoveRowsDetached) || theApp.IsClosing() || !::IsWindow(m_hWnd))
		return;

	if (theApp.emuledlg == NULL || theApp.emuledlg->activewnd != theApp.emuledlg->sharedfileswnd || !IsWindowVisible())
		return;

	HidePersistentInfoTip(true);
	std::vector<CKnownFile*> keptItems;
	keptItems.reserve(m_ListedItemsVector.size());
	for (size_t i = 0; i < m_ListedItemsVector.size(); ++i) {
		CKnownFile *pFile = m_ListedItemsVector[i];
		if (pFile == NULL || IsHiddenBySharedFilesVisibleRemove(pFile))
			continue;
		keptItems.push_back(pFile);
	}

	if (keptItems.size() == m_ListedItemsVector.size()) {
		if (m_bSharedFilesBulkRemoveRowsDetached) {
			m_bSharedFilesBulkRemoveVisibleSnapshotActive = true;
			m_uSharedFilesBulkRemoveVisibleSnapshotRows = m_ListedItemsVector.size();
		}
		if (m_bBackendDownloadRemoveRowsDetached) {
			m_bBackendDownloadRemoveVisibleSnapshotActive = true;
			m_uBackendDownloadRemoveVisibleSnapshotRows = m_ListedItemsVector.size();
		}
		ApplySharedFilesBulkRemoveVisibleItemCount(true);
		return;
	}

	SaveListState(m_uFilterID, kSharedFilesViewState);
	SetRedraw(false);
	m_ListedItemsVector.swap(keptItems);
	RebuildListedItemsMap();
	if (m_bSharedFilesBulkRemoveRowsDetached) {
		m_bSharedFilesBulkRemoveVisibleSnapshotActive = true;
		m_uSharedFilesBulkRemoveVisibleSnapshotRows = m_ListedItemsVector.size();
	}
	if (m_bBackendDownloadRemoveRowsDetached) {
		m_bBackendDownloadRemoveVisibleSnapshotActive = true;
		m_uBackendDownloadRemoveVisibleSnapshotRows = m_ListedItemsVector.size();
	}
	RequestSharedListRedraw();
	ShowFilesCount();
	ApplySharedFilesBulkRemoveVisibleItemCount(true);
	{
		CSharedFilesSelectionRestoreGuard guard(*this);
		RestoreListState(m_uFilterID, kSharedFilesViewState, false);
	}
	SetRedraw(true);
	Invalidate(FALSE);
}

void CSharedFilesCtrl::DetachSharedFilesBulkRemoveVisibleRows()
{
	if (m_bSharedFilesBulkRemoveRowsDetached || !IsSharedFilesBulkDeleteLikeOperation() || m_uSharedFilesBulkTotal < BULK_OPERATION_MIN_ITEMS || m_sharedFilesBulkQueuedKeys.GetCount() == 0 || theApp.IsClosing() || !::IsWindow(m_hWnd))
		return;

	m_bSharedFilesBulkRemoveRowsDetached = true;
	DetachSharedFilesVisibleRemoveRows();
}

void CSharedFilesCtrl::ClearSharedFilesBulkRemoveHiddenRows(bool bReloadVisibleList)
{
	if (!m_bSharedFilesBulkRemoveRowsDetached)
		return;

	m_bSharedFilesBulkRemoveRowsDetached = false;
	m_bSharedFilesBulkRemoveVisibleSnapshotActive = false;
	m_uSharedFilesBulkRemoveVisibleSnapshotRows = 0;
	if (bReloadVisibleList && !theApp.IsClosing() && ::IsWindow(m_hWnd))
		ReloadList(false, kSharedFilesViewState);
}

void CSharedFilesCtrl::ClearBackendDownloadRemoveHiddenRows(bool bReloadVisibleList)
{
	if (!m_bBackendDownloadRemoveRowsDetached && m_backendDownloadRemoveHiddenRows.GetCount() == 0)
		return;

	m_backendDownloadRemoveHiddenRows.RemoveAll();
	m_bBackendDownloadRemoveRowsDetached = false;
	m_bBackendDownloadRemoveVisibleSnapshotActive = false;
	m_uBackendDownloadRemoveVisibleSnapshotRows = 0;
	if (bReloadVisibleList && !theApp.IsClosing() && ::IsWindow(m_hWnd))
		ReloadList(false, kSharedFilesViewState);
}

bool CSharedFilesCtrl::HasActiveSortOrder() const
{
	return (GetSortItem() != -1 && m_ListedItemsVector.size() >= 2);
}

bool CSharedFilesCtrl::NeedsSortReposition(const int iIndex) const
{
	if (iIndex < 0 || iIndex >= static_cast<int>(m_ListedItemsVector.size()))
		return false;

	const CKnownFile* pFile = m_ListedItemsVector[iIndex];
	if (pFile == NULL)
		return false;

	CSharedFilesCtrl* pThis = const_cast<CSharedFilesCtrl*>(this);
	const bool bOldRawSortState = pThis->m_bSharedFilesRawSortInProgress;
	pThis->m_bSharedFilesRawSortInProgress = true;
	bool bNeedsReposition = false;
	if (iIndex > 0) {
		const CKnownFile* pPrev = m_ListedItemsVector[iIndex - 1];
		bNeedsReposition = (pPrev != NULL && SortProc((LPARAM)pPrev, (LPARAM)pFile, m_pSortParam) > 0);
	}

	if (!bNeedsReposition && iIndex + 1 < static_cast<int>(m_ListedItemsVector.size())) {
		const CKnownFile* pNext = m_ListedItemsVector[iIndex + 1];
		bNeedsReposition = (pNext != NULL && SortProc((LPARAM)pFile, (LPARAM)pNext, m_pSortParam) > 0);
	}
	pThis->m_bSharedFilesRawSortInProgress = bOldRawSortState;
	return bNeedsReposition;
}

bool CSharedFilesCtrl::RepositionFileByCurrentSort(CKnownFile* file, const int iIndex)
{
	if (file == NULL || iIndex < 0 || iIndex >= static_cast<int>(m_ListedItemsVector.size()) || m_ListedItemsVector[iIndex] != file)
		return false;

	const bool bOldRawSortState = m_bSharedFilesRawSortInProgress;
	m_bSharedFilesRawSortInProgress = true;
	const bool bMoveLeft = (iIndex > 0 && m_ListedItemsVector[iIndex - 1] != NULL && SortProc((LPARAM)m_ListedItemsVector[iIndex - 1], (LPARAM)file, m_pSortParam) > 0);
	const bool bMoveRight = (!bMoveLeft && iIndex + 1 < static_cast<int>(m_ListedItemsVector.size()) && m_ListedItemsVector[iIndex + 1] != NULL && SortProc((LPARAM)file, (LPARAM)m_ListedItemsVector[iIndex + 1], m_pSortParam) > 0);
	if (!bMoveLeft && !bMoveRight) {
		m_bSharedFilesRawSortInProgress = bOldRawSortState;
		return false;
	}

	int iNewIndex = iIndex;
	if (bMoveLeft) {
		std::vector<CKnownFile*>::iterator itNew = std::lower_bound(m_ListedItemsVector.begin(), m_ListedItemsVector.begin() + iIndex, file, SortFunc);
		iNewIndex = static_cast<int>(std::distance(m_ListedItemsVector.begin(), itNew));
	} else {
		std::vector<CKnownFile*>::iterator itNew = std::upper_bound(m_ListedItemsVector.begin() + iIndex + 1, m_ListedItemsVector.end(), file, SortFunc);
		iNewIndex = static_cast<int>(std::distance(m_ListedItemsVector.begin(), itNew)) - 1;
	}

	const int iStartIndex = min(iIndex, iNewIndex);
	const int iEndIndex = max(iIndex, iNewIndex);
	// Preserve only index-bound state to avoid list-wide redraws during live statistic sorting.
	const int iItemCount = GetItemCount();
	const int iRowsPerPage = GetCountPerPage();
	const bool bScrollable = iRowsPerPage > 0 && iItemCount > iRowsPerPage;
	const bool bWasAtBottom = bScrollable && IsAtBottom();
	const int iTopIndex = max(0, GetTopIndex());
	CKnownFile* pTopItem = (bScrollable && !bWasAtBottom && iTopIndex < static_cast<int>(m_ListedItemsVector.size())) ? m_ListedItemsVector[static_cast<size_t>(iTopIndex)] : NULL;
	const bool bAllItemsSelected = iItemCount > 0 && static_cast<int>(GetSelectedCount()) == iItemCount;
	CArray<CKnownFile*, CKnownFile*> aSelectedItems;
	CArray<int, int> aSelectedIndexes;
	if (!bAllItemsSelected) {
		for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
			const int iSelectedIndex = GetNextSelectedItem(pos);
			if (iSelectedIndex >= iStartIndex && iSelectedIndex <= iEndIndex) {
				aSelectedItems.Add(m_ListedItemsVector[static_cast<size_t>(iSelectedIndex)]);
				aSelectedIndexes.Add(iSelectedIndex);
			}
		}
	}

	const int iFocusedIndex = GetNextItem(-1, LVNI_FOCUSED);
	CKnownFile* pFocusedItem = (iFocusedIndex >= iStartIndex && iFocusedIndex <= iEndIndex) ? m_ListedItemsVector[static_cast<size_t>(iFocusedIndex)] : NULL;
	const int iSelectionMark = GetSelectionMark();
	CKnownFile* pSelectionMarkItem = (iSelectionMark >= iStartIndex && iSelectionMark <= iEndIndex) ? m_ListedItemsVector[static_cast<size_t>(iSelectionMark)] : NULL;

	if (bMoveLeft)
		std::rotate(m_ListedItemsVector.begin() + iNewIndex, m_ListedItemsVector.begin() + iIndex, m_ListedItemsVector.begin() + iIndex + 1);
	else
		std::rotate(m_ListedItemsVector.begin() + iIndex, m_ListedItemsVector.begin() + iIndex + 1, m_ListedItemsVector.begin() + iNewIndex + 1);
	m_bSharedFilesRawSortInProgress = bOldRawSortState;
	UpdateListedItemsMapRange(iStartIndex, iEndIndex);

	{
		CSharedFilesSelectionRestoreGuard guard(*this);
		if (!bAllItemsSelected) {
			for (INT_PTR i = 0; i < aSelectedIndexes.GetCount(); ++i)
				SetItemState(aSelectedIndexes[i], 0, LVIS_SELECTED);
			for (INT_PTR i = 0; i < aSelectedItems.GetCount(); ++i) {
				const int iSelectedIndex = FindListedIndexByPointer(aSelectedItems[i]);
				if (iSelectedIndex >= 0)
					SetItemState(iSelectedIndex, LVIS_SELECTED, LVIS_SELECTED);
			}
		}

		if (pFocusedItem != NULL) {
			const int iNewFocusedIndex = FindListedIndexByPointer(pFocusedItem);
			if (iNewFocusedIndex != iFocusedIndex) {
				SetItemState(iFocusedIndex, 0, LVIS_FOCUSED);
				if (iNewFocusedIndex >= 0)
					SetItemState(iNewFocusedIndex, LVIS_FOCUSED, LVIS_FOCUSED);
			}
		}

		if (pSelectionMarkItem != NULL)
			SetSelectionMark(FindListedIndexByPointer(pSelectionMarkItem));
	}

	if (bWasAtBottom) {
		if (GetTopIndex() != iTopIndex)
			ScrollToTopIndex(iTopIndex);
	} else if (pTopItem != NULL) {
		const int iNewTopIndex = FindListedIndexByPointer(pTopItem);
		if (iNewTopIndex >= 0 && iNewTopIndex != iTopIndex)
			ScrollToTopIndex(iNewTopIndex);
	}

	RequestRowRedrawAsync(iStartIndex, iEndIndex); // Coalesce list redraw for rows whose index changed.
	return true;
}


CObject* CSharedFilesCtrl::GetItemObject(int iIndex) const
{
	if (iIndex < 0 || iIndex >= m_ListedItemsVector.size())
		return nullptr;
	return m_ListedItemsVector[iIndex];
}

uint32 CSharedFilesCtrl::GetFilterId() const
{
	// We aim to differentiate different filter types here:
	uint retval = 0;
	const CDirectoryItem* pSelectedTreeFilter = (theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL)
		? theApp.emuledlg->sharedfileswnd->m_ctlSharedDirTree.GetSelectedFilter()
		: NULL;
	const ESpecialDirectoryItems eSelectedTreeItemType = (pSelectedTreeFilter != NULL) ? pSelectedTreeFilter->m_eItemType : SDI_ALL;
	if (eSelectedTreeItemType == SDI_ED2KFILETYPE) {
		if (m_pDirectoryFilter->m_nCatFilter == ED2KFT_OTHER)
			retval = (m_eFilter * 1000) + (99 * 100); // reserve 99 for "Other"
		else if (m_pDirectoryFilter->m_nCatFilter != -1)
			retval = (m_eFilter * 1000) + (m_pDirectoryFilter->m_nCatFilter * 100);
		else
			retval = (m_eFilter * 1000) + eSelectedTreeItemType;
	} else
		retval = (m_eFilter * 1000) + eSelectedTreeItemType;
	return retval;
}

void CSharedFilesCtrl::ShowFilesCount()
{
	if (theApp.IsClosing())
		return;

	CString m_strCount;
	m_strCount.Format(_T(":%Iu"), static_cast<size_t>(m_ListedItemsVector.size()));

	UpdateSharedFilesHashingOverlay();
	UpdateSharedFilesMetadataOverlay();

	if (m_eFilter == FilterType::History)
		theApp.emuledlg->sharedfileswnd->SetDlgItemText(IDC_TRAFFIC_TEXT, GetResString(_T("FILE_HISTORY")) + m_strCount);
	else if (m_eFilter == FilterType::Duplicate)
		theApp.emuledlg->sharedfileswnd->GetDlgItem(IDC_TRAFFIC_TEXT)->SetWindowText(GetResString(_T("DUPLICATE_FILES")) + m_strCount);
	else if (m_eFilter == FilterType::FileSystem)
		theApp.emuledlg->sharedfileswnd->GetDlgItem(IDC_TRAFFIC_TEXT)->SetWindowText(GetResString(_T("FILES")) + m_strCount);
	else
		theApp.emuledlg->sharedfileswnd->GetDlgItem(IDC_TRAFFIC_TEXT)->SetWindowText(GetResString(_T("SF_FILES")) + m_strCount);
}

void CSharedFilesCtrl::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	// for virtual lists, itemData is always nulluse dwItemSpec as the index
	int index = static_cast<int>(lpDrawItemStruct->itemID);
	if (index < 0 || theApp.IsClosing() || m_ListedItemsVector.empty() || static_cast<size_t>(index) >= m_ListedItemsVector.size()) {
		FillSharedFilesFallbackOwnerDataRow(*this, lpDrawItemStruct);
		return;
	}

	CKnownFile* pKnownFile = m_ListedItemsVector[static_cast<size_t>(index)];
	if (pKnownFile == NULL) {
		FillSharedFilesFallbackOwnerDataRow(*this, lpDrawItemStruct);
		return;
	}
	CShareableFile* file = static_cast<CShareableFile*>(pKnownFile);
	if (file == NULL) {
		FillSharedFilesFallbackOwnerDataRow(*this, lpDrawItemStruct);
		return;
	}
	if (!file->IsKindOf(RUNTIME_CLASS(CKnownFile)))
		pKnownFile = NULL;
	const bool bSuppressShareManagementColumns = pKnownFile != NULL && ShouldSuppressShareManagementColumns(pKnownFile);
	const bool bSuppressShareManagementRowColor = pKnownFile != NULL && ShouldSuppressShareManagementRowColor(pKnownFile, m_eFilter);

	CRect rcItem(lpDrawItemStruct->rcItem);
	CRect rcClientFullRow;
	GetClientRect(&rcClientFullRow);
	CRect rcPaint(rcClientFullRow.left, rcItem.top, rcClientFullRow.right, rcItem.bottom);
	CDC* pBaseDC = CDC::FromHandle(lpDrawItemStruct->hDC);
	CMemoryDC dc(pBaseDC, rcPaint);
	BOOL bCtrlFocused;
	InitItemMemDC(dc, lpDrawItemStruct, bCtrlFocused);

	COLORREF clrBk = (lpDrawItemStruct->itemState & ODS_SELECTED) ? GetCustomSysColor(COLOR_HIGHLIGHT) : GetCustomSysColor(COLOR_WINDOW);
	COLORREF clrText = (lpDrawItemStruct->itemState & ODS_SELECTED) ? GetCustomSysColor(COLOR_HIGHLIGHTTEXT) : GetCustomSysColor(COLOR_WINDOWTEXT);
	dc.FillSolidRect(rcPaint, clrBk);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(clrBk);

	RECT rcClient;
	GetClientRect(&rcClient);

	const CHeaderCtrl *pHeaderCtrl = GetHeaderCtrl();
	int iCount = pHeaderCtrl->GetItemCount();
	LONG itemLeft = rcItem.left;
	int iIconDrawWidth = theApp.GetSmallSytemIconSize().cx;
	LONG iIconY = max((rcItem.Height() - theApp.GetSmallSytemIconSize().cy - 1) / 2, 0);
	for (int iCurrent = 0; iCurrent < iCount; ++iCurrent) {
		int iColumn = pHeaderCtrl->OrderToIndex(iCurrent);
		if (IsColumnHidden(iColumn))
			continue;

		UINT uDrawTextAlignment;
		int iColumnWidth = GetColumnWidth(iColumn, uDrawTextAlignment);
		rcItem.left = itemLeft;
		rcItem.right = itemLeft + iColumnWidth;
		if (rcItem.left < rcItem.right && HaveIntersection(rcClient, rcItem)) {
			const CString &sItem(GetItemDisplayText(file, iColumn));
			COLORREF clrColumnText = clrText;
			if (!bSuppressShareManagementRowColor && iColumn != 0 && thePrefs.GetSharePermissionColorRows()) {
				switch (pKnownFile != NULL ? GetEffectivePermission(pKnownFile) : thePrefs.GetSharePermissions()) {
				case PERM_NOONE:
					clrColumnText = RGB(0, 175, 0);
					break;
				case PERM_FRIENDS:
					clrColumnText = RGB(208, 128, 0);
					break;
				case PERM_ALL:
					clrColumnText = RGB(240, 0, 0);
					break;
				}
			}
			dc.SetTextColor(clrColumnText);
			switch (iColumn) {
			case 0: //file name
				{
					rcItem.left += sm_iIconOffset;
					LONG rcIconTop = rcItem.top + iIconY;
					if (CheckBoxesEnabled()) {
						int iNoStyleState;
						const bool bShouldBeShared = theApp.sharedfiles != NULL && theApp.sharedfiles->ShouldBeShared(file->GetSharedDirectory(), file->GetFilePath(), false);
						const bool bShouldBeSharedByDefault = theApp.sharedfiles != NULL && theApp.sharedfiles->ShouldBeShared(file->GetSharedDirectory(), file->GetFilePath(), true);
						if ((file->IsShellLinked() && bShouldBeShared) || bShouldBeSharedByDefault)
							iNoStyleState = DFCS_CHECKED | DFCS_INACTIVE;
						else if (bShouldBeShared)
							iNoStyleState = DFCS_CHECKED;
						else if (!thePrefs.IsShareableDirectory(file->GetPath()))
							iNoStyleState = DFCS_INACTIVE;
						else
							iNoStyleState = 0;

						RECT rcCheckBox = { rcItem.left, rcIconTop, rcItem.left + 16, rcIconTop + 16 };
						dc.DrawFrameControl(&rcCheckBox, DFC_BUTTON, DFCS_BUTTONCHECK | iNoStyleState | DFCS_FLAT);
						rcItem.left += 16 + sm_iLabelOffset;
					}

					if (theApp.GetSystemImageList() != NULL) {
						int iImage = theApp.GetFileTypeSystemImageIdx(file->GetFileName());
						::ImageList_Draw(theApp.GetSystemImageList(), iImage, dc.GetSafeHdc(), rcItem.left, rcIconTop, ILD_TRANSPARENT);
					}

					if (!file->GetFileComment().IsEmpty() || file->GetFileRating())
						SafeImageListDraw(&m_ImageList, dc, 0, POINT{ rcItem.left, rcIconTop }, ILD_NORMAL | INDEXTOOVERLAYMASK(1));

					rcItem.left += iIconDrawWidth + sm_iLabelOffset;
					if (thePrefs.ShowRatingIndicator() && (file->HasComment() || file->HasRating() || file->IsKadCommentSearchRunning())) {
						SafeImageListDraw(&m_ImageList, dc, 3 + file->UserRating(true), POINT{ rcItem.left, rcIconTop }, ILD_NORMAL);
						rcItem.left += 16 + sm_iLabelOffset;
					}
					rcItem.left -= sm_iSubItemInset;
				}
			default: //any text column
				rcItem.left += sm_iSubItemInset;
				rcItem.right -= sm_iSubItemInset;
				dc.DrawText(sItem, -1, &rcItem, MLC_DT_TEXT | uDrawTextAlignment);
				break;
			case 8: //shared parts bar
				if (pKnownFile != NULL && pKnownFile->GetPartCount() > 0) {
					CRect rcShareStatus(rcItem);
					++rcShareStatus.top;
					--rcShareStatus.bottom;
					pKnownFile->DrawShareStatusBar(&dc, &rcShareStatus, false, thePrefs.UseFlatBar());
				}
				break;
			case kSharedFilesColumnSpreadbarHistory: //spread history bar
				if (pKnownFile != NULL && !bSuppressShareManagementColumns) {
					CRect rcSpread(rcItem);
					++rcSpread.top;
					--rcSpread.bottom;
					pKnownFile->statistic.DrawSpreadBar(&dc, &rcSpread, thePrefs.UseFlatBar());
				}
				break;
			case 11: //shared ed2k/kad
				if (pKnownFile != NULL) {
					rcItem.left += sm_iIconOffset;
					POINT point = { rcItem.left, rcItem.top + iIconY };
					if (pKnownFile->GetPublishedED2K())
						SafeImageListDraw(&m_ImageList, dc, 1, point, ILD_NORMAL);
					if (IsSharedInKad(pKnownFile)) {
						point.x += 16 + sm_iSubItemInset;
						SafeImageListDraw(&m_ImageList, dc, 2, point, ILD_NORMAL);
					}
				}
				break;
			}
		}
		itemLeft += iColumnWidth;
	}

	DrawFocusRect(dc, &lpDrawItemStruct->rcItem, (lpDrawItemStruct->itemState & ODS_FOCUS) != 0, bCtrlFocused, (lpDrawItemStruct->itemState & ODS_SELECTED) != 0);
}

const CString CSharedFilesCtrl::GetItemDisplayText(const CShareableFile *file, const int iSubItem) const
{
	CString sText;
	switch (iSubItem) {
	case 0:
		return file->GetFileName();
	case 1:
		return CastItoXBytes((uint64)file->GetFileSize());
	case 2:
		return file->GetFileTypeDisplayStr();
	case 9:
		sText = file->GetPath();
		unslosh(sText);
		return sText;
	}

	if (file->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
		const CKnownFile *pKnownFile = static_cast<const CKnownFile*>(file);
		const bool bSuppressShareManagementColumns = ShouldSuppressShareManagementColumns(pKnownFile);
		switch (iSubItem) {
		case 3:
			sText = BuildPriorityColumnText(pKnownFile);
			break;
		case 4:
			sText = md4str(pKnownFile->GetFileHash());
			break;
		case 5:
			sText.Format(_T("%u (%u)"), pKnownFile->statistic.GetRequests(), pKnownFile->statistic.GetAllTimeRequests());
			break;
		case 6:
			sText.Format(_T("%u (%u)"), pKnownFile->statistic.GetAccepts(), pKnownFile->statistic.GetAllTimeAccepts());
			break;
		case 7:
			sText.Format(_T("%s (%s)"), (LPCTSTR)CastItoXBytes(pKnownFile->statistic.GetTransferred()), (LPCTSTR)CastItoXBytes(pKnownFile->statistic.GetAllTimeTransferred()));
			break;
		case 8:
			sText.Format(_T("%u"), pKnownFile->GetPartCount());
			break;
		case 10:
			if (pKnownFile->m_nCompleteSourcesCountLo == pKnownFile->m_nCompleteSourcesCountHi)
				sText.Format(_T("%u"), pKnownFile->m_nCompleteSourcesCountLo);
			else if (pKnownFile->m_nCompleteSourcesCountLo == 0)
				sText.Format(_T("< %u"), pKnownFile->m_nCompleteSourcesCountHi);
			else
				sText.Format(_T("%u - %u"), pKnownFile->m_nCompleteSourcesCountLo, pKnownFile->m_nCompleteSourcesCountHi);
			break;
		case 11:
			sText.Format(_T("%s|%s"), (LPCTSTR)GetResString(pKnownFile->GetPublishedED2K() ? _T("YES") : _T("NO")), (LPCTSTR)GetResString(IsSharedInKad(pKnownFile) ? _T("YES") : _T("NO")));
			break;
		case 12:
			sText = pKnownFile->GetStrTagValue(FT_MEDIA_ARTIST);
			break;
		case 13:
			sText = pKnownFile->GetStrTagValue(FT_MEDIA_ALBUM);
			break;
		case 14:
			sText = pKnownFile->GetStrTagValue(FT_MEDIA_TITLE);
			break;
		case 15:
			{
				uint32 nMediaLength = pKnownFile->GetIntTagValue(FT_MEDIA_LENGTH);
				if (nMediaLength)
					sText = SecToTimeLength(nMediaLength);
			}
			break;
		case 16:
			{
				uint32 nBitrate = pKnownFile->GetIntTagValue(FT_MEDIA_BITRATE);
				if (nBitrate)
					sText.Format(_T("%u %s"), nBitrate, (LPCTSTR)GetResString(_T("KBITSSEC")));
			}
			break;
		case 17:
			sText = GetCodecDisplayName(pKnownFile->GetStrTagValue(FT_MEDIA_CODEC));
			break;
		case 18:
			sText.Format(_T("%.1f"), pKnownFile->GetAllTimeRatio());
			break;
		case 19:
			sText.Format(_T("%.1f"), pKnownFile->GetRatio());
			break;
		case kSharedFilesColumnPermission:
			if (!bSuppressShareManagementColumns)
				sText = BuildSharePermissionColumnText(pKnownFile);
			break;
		case kSharedFilesColumnPowershare:
			if (!bSuppressShareManagementColumns)
				sText = BuildPowerShareColumnText(pKnownFile);
			break;
		case kSharedFilesColumnSpreadbarHistory:
			if (!bSuppressShareManagementColumns)
				sText = BuildSpreadbarHistoryColumnText(pKnownFile);
			break;
		case kSharedFilesColumnHideOverShare:
			if (!bSuppressShareManagementColumns)
				sText = BuildHideOverShareColumnText(pKnownFile);
			break;
		case kSharedFilesColumnShareOnlyTheNeed:
			if (!bSuppressShareManagementColumns)
				sText = BuildShareOnlyTheNeedColumnText(pKnownFile);
			break;
		case kSharedFilesColumnLastRequest:
			if (ShouldShowLastRequestForSharedFile(pKnownFile))
				sText = (pKnownFile->statistic.GetLastRequestTime() > 0) ? CTime(pKnownFile->statistic.GetLastRequestTime()).Format(thePrefs.GetDateTimeFormat4Lists()) : GetResString(_T("NEVER"));
			break;
		}
	}
	return sText;
}


void CSharedFilesCtrl::OnContextMenu(CWnd*, CPoint point)
{
	// get merged settings
	bool bFirstItem = true;
	bool bContainsShareableFiles = false;
	bool bContainsOnlyShareableFile = true;
	bool m_bAllInDownloadList = true;
	int iDownloadListMatches = 0;
	int iFilesToPreview = 0;
	int iFilesCanPauseOnPreview = 0;
	int iFilesDoPauseOnPreview = 0;
	int iFilesPreviewType = 0;
	int iFilesGetPreviewParts = 0;
	const CPartFile* pSingleDownloadFile = NULL;
	int iSelectedItems = GetSelectedCount();
	UINT uPermMenuItem = 0;
	UINT uPowershareMenuItem = 0;
	UINT uPowerShareLimitMenuItem = 0;
	UINT uSpreadbarMenuItem = 0;
	UINT uHideOSMenuItem = 0;
	UINT uSelectiveChunkMenuItem = 0;
	UINT uShareOnlyTheNeedMenuItem = 0;
	int iPowerShareLimit = -1;
	int iHideOS = -1;
	UINT uPrioMenuItem = 0;
	const CShareableFile *pSingleSelFile = NULL;
	const bool bFastHistoryBulkMenu = iSelectedItems >= kSharedFilesLargeHistoryContextMenuSelection && m_eFilter == FilterType::History;
	if (bFastHistoryBulkMenu) {
		bContainsOnlyShareableFile = false;
		m_bAllInDownloadList = false;
	}
	if (!bFastHistoryBulkMenu) {
		for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
			int index = GetNextSelectedItem(pos);
				if (index < 0 || static_cast<size_t>(index) >= m_ListedItemsVector.size())
					continue;
				CKnownFile* cur_file = m_ListedItemsVector[index];
				if (cur_file == NULL)
					continue;
				const CShareableFile *pFile = static_cast<CShareableFile*>(cur_file);

		pSingleSelFile = bFirstItem ? pFile : NULL;

			const CPartFile* pDownloadFile = theApp.downloadqueue->GetFileByID(pFile->GetFileHash());
			if (pDownloadFile == NULL)
				m_bAllInDownloadList = false;
			else {
				++iDownloadListMatches;
				iFilesPreviewType += static_cast<int>(pDownloadFile->IsPreviewableFileType());
				iFilesToPreview += static_cast<int>(pDownloadFile->IsReadyForPreview());
				iFilesCanPauseOnPreview += static_cast<int>(pDownloadFile->IsPreviewableFileType() && !pDownloadFile->IsReadyForPreview() && pDownloadFile->CanPauseFile());
				iFilesDoPauseOnPreview += static_cast<int>(pDownloadFile->IsPausingOnPreview());
				iFilesGetPreviewParts += static_cast<int>(pDownloadFile->GetPreviewPrio());
				if (bFirstItem)
					pSingleDownloadFile = pDownloadFile;
			}


		if (pFile && pFile->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
			if (pFile->GetFilePath().GetLength() == 0)
				bContainsOnlyShareableFile = false;
			const CKnownFile* pKnownFile = static_cast<const CKnownFile*>(pFile);
			UINT uCurPrioMenuItem = 0;
			if (pKnownFile->IsAutoUpPriority())
				uCurPrioMenuItem = MP_PRIOAUTO;
			else
				switch (pKnownFile->GetUpPriority()) {
				case PR_VERYLOW:
					uCurPrioMenuItem = MP_PRIOVERYLOW;
					break;
				case PR_LOW:
					uCurPrioMenuItem = MP_PRIOLOW;
					break;
				case PR_NORMAL:
					uCurPrioMenuItem = MP_PRIONORMAL;
					break;
				case PR_HIGH:
					uCurPrioMenuItem = MP_PRIOHIGH;
					break;
				case PR_VERYHIGH:
					uCurPrioMenuItem = MP_PRIOVERYHIGH;
					break;
				default:
					ASSERT(0);
				}

			if (bFirstItem)
				uPrioMenuItem = uCurPrioMenuItem;
			else if (uPrioMenuItem != uCurPrioMenuItem)
				uPrioMenuItem = 0;

			const UINT uCurPermMenuItem = GetSharePermissionMenuItem(pKnownFile);
			const UINT uCurPowershareMenuItem = GetPowerShareMenuItem(pKnownFile);
			const UINT uCurSpreadbarMenuItem = GetToggleMenuItem(pKnownFile->GetSpreadbarSetStatus(), MP_SPREADBAR_DEFAULT, MP_SPREADBAR_OFF, MP_SPREADBAR_ON);
			const UINT uCurHideOSMenuItem = (pKnownFile->GetHideOS() < 0) ? MP_HIDEOS_DEFAULT : MP_HIDEOS_SET;
			const UINT uCurSelectiveChunkMenuItem = GetToggleMenuItem(pKnownFile->GetSelectiveChunk(), MP_SELECTIVE_CHUNK, MP_SELECTIVE_CHUNK_0, MP_SELECTIVE_CHUNK_1);
			const UINT uCurShareOnlyTheNeedMenuItem = GetToggleMenuItem(pKnownFile->GetShareOnlyTheNeed(), MP_SHAREONLYTHENEED, MP_SHAREONLYTHENEED_0, MP_SHAREONLYTHENEED_1);
			const UINT uCurPowerShareLimitMenuItem = (pKnownFile->GetPowerShareLimit() < 0) ? MP_POWERSHARE_LIMIT : MP_POWERSHARE_LIMIT_SET;
			const int iCurPowerShareLimit = pKnownFile->GetPowerShareLimit();
			const int iCurHideOS = pKnownFile->GetHideOS();

			if (bFirstItem) {
				uPermMenuItem = uCurPermMenuItem;
				uPowershareMenuItem = uCurPowershareMenuItem;
				uSpreadbarMenuItem = uCurSpreadbarMenuItem;
				uPowerShareLimitMenuItem = uCurPowerShareLimitMenuItem;
				uHideOSMenuItem = uCurHideOSMenuItem;
				uSelectiveChunkMenuItem = uCurSelectiveChunkMenuItem;
				uShareOnlyTheNeedMenuItem = uCurShareOnlyTheNeedMenuItem;
				iPowerShareLimit = iCurPowerShareLimit;
				iHideOS = iCurHideOS;
			} else {
				if (uPermMenuItem != uCurPermMenuItem)
					uPermMenuItem = 0;
				if (uPowershareMenuItem != uCurPowershareMenuItem)
					uPowershareMenuItem = 0;
				if (uSpreadbarMenuItem != uCurSpreadbarMenuItem)
					uSpreadbarMenuItem = 0;
				if (uPowerShareLimitMenuItem != uCurPowerShareLimitMenuItem || iPowerShareLimit != iCurPowerShareLimit) {
					uPowerShareLimitMenuItem = 0;
					iPowerShareLimit = -1;
				}
				if (uHideOSMenuItem != uCurHideOSMenuItem || iHideOS != iCurHideOS) {
					uHideOSMenuItem = 0;
					iHideOS = -1;
				}
				if (uSelectiveChunkMenuItem != uCurSelectiveChunkMenuItem)
					uSelectiveChunkMenuItem = 0;
				if (uShareOnlyTheNeedMenuItem != uCurShareOnlyTheNeedMenuItem)
					uShareOnlyTheNeedMenuItem = 0;
			}
		} else
			bContainsShareableFiles = true;

		bFirstItem = false;
	}
	}

	bool m_bContainsSharedFile = false;
	bool m_bContainsNotSharedFile = false;
	bool m_bContainsPartFile = false;
	if (!bFastHistoryBulkMenu) {
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				int index = GetNextSelectedItem(pos);
				if (index >= 0 && static_cast<size_t>(index) < m_ListedItemsVector.size()) {
					CKnownFile* cur_file = m_ListedItemsVector[index];
				if (cur_file != NULL) {
					if (theApp.sharedfiles->GetFileByID(cur_file->GetFileHash()) != NULL && !theApp.knownfiles->IsOnDuplicates(cur_file->GetFileName(), cur_file->GetUtcFileDate(), cur_file->GetFileSize()))
						m_bContainsSharedFile = true;
					else
					m_bContainsNotSharedFile = true;

				if (cur_file->IsPartFile())
					m_bContainsPartFile = true;
			} else
				m_bContainsNotSharedFile = true;
		}
	}
	}

	bool bSingleCompleteFileSelected = (iSelectedItems == 1 && (!m_bContainsPartFile || bContainsOnlyShareableFile));

	if (thePrefs.GetFileHistoryShowPart())
		m_SharedFilesMenu.CheckMenuItem(MP_VIEWPARTFILES, MF_CHECKED);
	else
		m_SharedFilesMenu.CheckMenuItem(MP_VIEWPARTFILES, MF_UNCHECKED);

	if (thePrefs.GetFileHistoryShowShared())
		m_SharedFilesMenu.CheckMenuItem(MP_VIEWSHAREDFILES, MF_CHECKED);
	else
		m_SharedFilesMenu.CheckMenuItem(MP_VIEWSHAREDFILES, MF_UNCHECKED);

	if (thePrefs.GetFileHistoryShowDuplicate())
		m_SharedFilesMenu.CheckMenuItem(MP_VIEWDUPLICATEFILES, MF_CHECKED);
	else
		m_SharedFilesMenu.CheckMenuItem(MP_VIEWDUPLICATEFILES, MF_UNCHECKED);

	m_SharedFilesMenu.EnableMenuItem(MP_OPEN, (iSelectedItems == 1 && ((m_eFilter != FilterType::History && !m_bContainsPartFile) || (m_eFilter == FilterType::History && !m_bContainsPartFile && m_bContainsSharedFile))) ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_REMOVEFROMHISTORY, (iSelectedItems != 0 && m_eFilter == FilterType::History && (bFastHistoryBulkMenu || (!m_bContainsPartFile && !m_bContainsSharedFile))) ? MF_ENABLED : MF_GRAYED);
	const bool bEnableDownloadOnlyMenu = (iSelectedItems > 0 && m_bAllInDownloadList && iDownloadListMatches == iSelectedItems);
	m_SharedFilesMenu.EnableMenuItem(MP_CANCEL, bEnableDownloadOnlyMenu ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_CANCEL_FORGET, bEnableDownloadOnlyMenu ? MF_ENABLED : MF_GRAYED);
	RebuildPreviewMenu(m_PreviewMenu, (bEnableDownloadOnlyMenu && iSelectedItems == 1) ? pSingleDownloadFile : NULL, bEnableDownloadOnlyMenu && iSelectedItems == 1 && iFilesToPreview == 1, bEnableDownloadOnlyMenu && iFilesCanPauseOnPreview > 0, bEnableDownloadOnlyMenu && iSelectedItems > 0 && iFilesDoPauseOnPreview == iSelectedItems, bEnableDownloadOnlyMenu && iSelectedItems == 1 && iFilesPreviewType == 1 && iFilesToPreview == 0 && iDownloadListMatches == 1, bEnableDownloadOnlyMenu && iSelectedItems == 1 && iFilesGetPreviewParts == 1);
	m_SharedFilesMenu.EnableMenuItem((UINT)m_PreviewMenu.m_hMenu, bEnableDownloadOnlyMenu && m_PreviewMenu.HasEnabledItems() ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_VIEWPARTFILES, m_eFilter == FilterType::History ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_VIEWSHAREDFILES, m_eFilter == FilterType::History ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_VIEWDUPLICATEFILES, m_eFilter == FilterType::History ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem((UINT)m_FileHistorysMenu.m_hMenu, m_eFilter == FilterType::History ? MF_ENABLED : MF_GRAYED);
	const bool bEnableShareManagementMenu = (iSelectedItems != 0
		&& ((m_eFilter != FilterType::Duplicate && m_eFilter != FilterType::History && !m_bContainsNotSharedFile && m_bContainsSharedFile)
			|| (m_eFilter == FilterType::History && !m_bContainsNotSharedFile && m_bContainsSharedFile && bContainsOnlyShareableFile)));
	m_SharedFilesMenu.EnableMenuItem((UINT)m_PrioMenu.m_hMenu, bEnableShareManagementMenu ? MF_ENABLED : MF_GRAYED);
	m_PrioMenu.CheckMenuRadioItem(MP_PRIOVERYLOW, MP_PRIOAUTO, uPrioMenuItem, MF_BYCOMMAND);

	m_SharedFilesMenu.EnableMenuItem((UINT)m_PermMenu.m_hMenu, bEnableShareManagementMenu ? MF_ENABLED : MF_GRAYED);
	CString buffer;
	CString strDefaultMenu(GetResString(_T("DEFAULT")));
	CString strSuffix(GetSharePermissionLabel(thePrefs.GetSharePermissions()));
	if (!strSuffix.IsEmpty())
		strDefaultMenu.AppendFormat(_T(" (%s)"), (LPCTSTR)strSuffix);
	m_PermMenu.SetMenuText(MP_PERMDEFAULT, strDefaultMenu);
	UpdateSharePermissionMenuChecks(m_PermMenu, uPermMenuItem);

	m_SharedFilesMenu.EnableMenuItem((UINT)m_PowershareMenu.m_hMenu, bEnableShareManagementMenu ? MF_ENABLED : MF_GRAYED);
	strDefaultMenu = GetResString(_T("DEFAULT"));
	strSuffix = GetPowerShareModeLabel(thePrefs.GetPowerShareMode());
	if (!strSuffix.IsEmpty())
		strDefaultMenu.AppendFormat(_T(" (%s)"), (LPCTSTR)strSuffix);
	m_PowershareMenu.SetMenuText(MP_POWERSHARE_DEFAULT, strDefaultMenu);
	m_PowershareMenu.CheckMenuRadioItem(MP_POWERSHARE_DEFAULT, MP_POWERSHARE_LIMITED, uPowershareMenuItem, MF_BYCOMMAND);

	m_PowershareMenu.EnableMenuItem((UINT)m_PowerShareLimitMenu.m_hMenu, bEnableShareManagementMenu ? MF_ENABLED : MF_GRAYED);
	buffer = GetResString(_T("DEFAULT"));
	if (thePrefs.GetPowerShareLimit() == 0)
		buffer.AppendFormat(_T(" (%s)"), (LPCTSTR)GetResString(_T("DISABLED")));
	else
		buffer.AppendFormat(_T(" (%u)"), thePrefs.GetPowerShareLimit());
	m_PowerShareLimitMenu.SetMenuText(MP_POWERSHARE_LIMIT, buffer);
	if (iPowerShareLimit < 0)
		buffer = GetResString(_T("EDIT"));
	else if (iPowerShareLimit == 0)
		buffer = GetResString(_T("DISABLED"));
	else
		buffer.Format(_T("%i"), iPowerShareLimit);
	m_PowerShareLimitMenu.SetMenuText(MP_POWERSHARE_LIMIT_SET, buffer);
	m_PowerShareLimitMenu.CheckMenuRadioItem(MP_POWERSHARE_LIMIT, MP_POWERSHARE_LIMIT_SET, uPowerShareLimitMenuItem, MF_BYCOMMAND);

	m_SharedFilesMenu.EnableMenuItem((UINT)m_SpreadbarMenu.m_hMenu, bEnableShareManagementMenu ? MF_ENABLED : MF_GRAYED);
	strDefaultMenu = GetResString(_T("DEFAULT"));
	strDefaultMenu.AppendFormat(_T(" (%s)"), (LPCTSTR)GetEnabledDisabledLabel(thePrefs.GetSpreadbarSetStatus()));
	m_SpreadbarMenu.SetMenuText(MP_SPREADBAR_DEFAULT, strDefaultMenu);
	m_SpreadbarMenu.CheckMenuRadioItem(MP_SPREADBAR_DEFAULT, MP_SPREADBAR_ON, uSpreadbarMenuItem, MF_BYCOMMAND);

	m_SharedFilesMenu.EnableMenuItem((UINT)m_HideOSMenu.m_hMenu, bEnableShareManagementMenu ? MF_ENABLED : MF_GRAYED);
	if (thePrefs.GetHideOvershares() == 0)
		buffer.Format(_T("%s (%s)"), (LPCTSTR)GetResString(_T("DEFAULT")), (LPCTSTR)GetResString(_T("DISABLED")));
	else
		buffer.Format(_T("%s (%u)"), (LPCTSTR)GetResString(_T("DEFAULT")), thePrefs.GetHideOvershares());
	m_HideOSMenu.SetMenuText(MP_HIDEOS_DEFAULT, buffer);
	if (iHideOS < 0)
		buffer = GetResString(_T("EDIT"));
	else if (iHideOS == 0)
		buffer = GetResString(_T("DISABLED"));
	else
		buffer.Format(_T("%i"), iHideOS);
	m_HideOSMenu.SetMenuText(MP_HIDEOS_SET, buffer);
	m_HideOSMenu.CheckMenuRadioItem(MP_HIDEOS_DEFAULT, MP_HIDEOS_SET, uHideOSMenuItem, MF_BYCOMMAND);

	m_HideOSMenu.EnableMenuItem((UINT)m_SelectiveChunkMenu.m_hMenu, bEnableShareManagementMenu ? MF_ENABLED : MF_GRAYED);
	strDefaultMenu = GetResString(_T("DEFAULT"));
	strDefaultMenu.AppendFormat(_T(" (%s)"), (LPCTSTR)GetEnabledDisabledLabel(thePrefs.IsSelectiveShareEnabled()));
	m_SelectiveChunkMenu.SetMenuText(MP_SELECTIVE_CHUNK, strDefaultMenu);
	m_SelectiveChunkMenu.CheckMenuRadioItem(MP_SELECTIVE_CHUNK, MP_SELECTIVE_CHUNK_1, uSelectiveChunkMenuItem, MF_BYCOMMAND);

	m_SharedFilesMenu.EnableMenuItem((UINT)m_ShareOnlyTheNeedMenu.m_hMenu, bEnableShareManagementMenu ? MF_ENABLED : MF_GRAYED);
	strDefaultMenu = GetResString(_T("DEFAULT"));
	strDefaultMenu.AppendFormat(_T(" (%s)"), (LPCTSTR)GetEnabledDisabledLabel(thePrefs.GetShareOnlyTheNeed()));
	m_ShareOnlyTheNeedMenu.SetMenuText(MP_SHAREONLYTHENEED, strDefaultMenu);
	m_ShareOnlyTheNeedMenu.CheckMenuRadioItem(MP_SHAREONLYTHENEED, MP_SHAREONLYTHENEED_1, uShareOnlyTheNeedMenuItem, MF_BYCOMMAND);

	UINT uInsertedMenuItem = 0;
	static const TCHAR _szSkinPkgSuffix1[] = _T(".") EMULSKIN_BASEEXT _T(".zip");
	static const TCHAR _szSkinPkgSuffix2[] = _T(".") EMULSKIN_BASEEXT _T(".rar");
	if (bSingleCompleteFileSelected
		&& pSingleSelFile
		&& (pSingleSelFile->GetFilePath().Right(_countof(_szSkinPkgSuffix1) - 1).CompareNoCase(_szSkinPkgSuffix1) == 0
			|| pSingleSelFile->GetFilePath().Right(_countof(_szSkinPkgSuffix2) - 1).CompareNoCase(_szSkinPkgSuffix2) == 0))
	{
		MENUITEMINFO mii = {};
		mii.cbSize = (UINT)sizeof mii;
		mii.fMask = MIIM_TYPE | MIIM_STATE | MIIM_ID;
		mii.fType = MFT_STRING;
		mii.fState = MFS_ENABLED;
		mii.wID = MP_INSTALL_SKIN;
		const CString &strBuff(GetResString(_T("INSTALL_SKIN")));
		mii.dwTypeData = const_cast<LPTSTR>((LPCTSTR)strBuff);
		if (m_SharedFilesMenu.InsertMenuItem(MP_OPENFOLDER, &mii, FALSE))
			uInsertedMenuItem = mii.wID;
	}

	m_SharedFilesMenu.EnableMenuItem(MP_OPENFOLDER, (iSelectedItems == 1 && ((m_eFilter != FilterType::History && !m_bContainsPartFile) || (m_eFilter == FilterType::History && !m_bContainsPartFile && m_bContainsSharedFile))) ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_RENAME, (iSelectedItems == 1 && ((m_eFilter != FilterType::History && !m_bContainsPartFile && m_bContainsSharedFile && bSingleCompleteFileSelected) || (m_eFilter == FilterType::History && !m_bContainsPartFile))) ? MF_ENABLED : MF_GRAYED);
	const bool bCanDeleteSelectedSharedFiles = !bFastHistoryBulkMenu && CanDeleteSelectedSharedFilesFromDisk();
	const bool bCanUpdateSelectedSharedFilesMetadata = !bFastHistoryBulkMenu && CanUpdateSelectedSharedFilesMetadata();
	const bool bCanUnshareSelectedSharedFiles = !bFastHistoryBulkMenu && CanUnshareSelectedSharedFiles();
	m_SharedFilesMenu.EnableMenuItem(MP_REMOVE, bCanDeleteSelectedSharedFiles ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_UPDATE_METADATA, bCanUpdateSelectedSharedFilesMetadata ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_UNSHAREFILE, bCanUnshareSelectedSharedFiles ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.SetDefaultItem(bSingleCompleteFileSelected ? MP_OPEN : -1);
	m_SharedFilesMenu.EnableMenuItem(MP_CMT, (!bContainsShareableFiles && iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_DETAIL, iSelectedItems > 0 ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_SHOWED2KLINK, (iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_CUT, (iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED);
	if (thePrefs.GetShowCopyEd2kLinkCmd())
		m_SharedFilesMenu.EnableMenuItem(MP_GETED2KLINK, (iSelectedItems > 0) ? MF_ENABLED : MF_GRAYED);
	m_SharedFilesMenu.EnableMenuItem(MP_FIND, !m_ListedItemsVector.empty() ? MF_ENABLED : MF_GRAYED);

	const CCollection *coll = pSingleSelFile ? static_cast<const CKnownFile*>(pSingleSelFile)->m_pCollection : NULL;
	m_CollectionsMenu.EnableMenuItem(MP_MODIFYCOLLECTION, (!bContainsShareableFiles && coll != NULL) ? MF_ENABLED : MF_GRAYED);
	m_CollectionsMenu.EnableMenuItem(MP_VIEWCOLLECTION, (!bContainsShareableFiles && coll != NULL) ? MF_ENABLED : MF_GRAYED);
	m_CollectionsMenu.EnableMenuItem(MP_SEARCHAUTHOR, (!bContainsShareableFiles && coll != NULL && !coll->GetAuthorKeyHashString().IsEmpty()) ? MF_ENABLED : MF_GRAYED);
#if defined(_DEBUG)
	if (thePrefs.IsExtControlsEnabled()) {
		//JOHNTODO: Not for release as we need kad lowID users in the network to see how well this work. Also, we do not support these links yet.
		bool bEnable = (iSelectedItems > 0 && theApp.IsConnected() && theApp.IsFirewalled() && theApp.clientlist->GetServingBuddy());
		m_SharedFilesMenu.EnableMenuItem(MP_GETKADSOURCELINK, (bEnable ? MF_ENABLED : MF_GRAYED));
	}
#endif
	m_SharedFilesMenu.EnableMenuItem(Irc_SetSendLink, (iSelectedItems == 1 && theApp.emuledlg->ircwnd->IsConnected()) ? MF_ENABLED : MF_GRAYED);

	CMenuXP WebMenu;
	WebMenu.CreateMenu();
	int iWebMenuEntries = theWebServices.GetFileMenuEntries(&WebMenu);
	UINT flag2 = (iWebMenuEntries == 0 || iSelectedItems == 0) ? MF_GRAYED : MF_STRING;
	m_SharedFilesMenu.AppendMenu(flag2 | MF_POPUP, (UINT_PTR)WebMenu.m_hMenu, GetResString(_T("WEBSERVICES")), _T("WEB"));

	GetPopupMenuPos(*this, point);
	m_SharedFilesMenu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);

	m_SharedFilesMenu.RemoveMenu(m_SharedFilesMenu.GetMenuItemCount() - 1, MF_BYPOSITION);
	VERIFY(WebMenu.DestroyMenu());
	if (uInsertedMenuItem)
		VERIFY(m_SharedFilesMenu.RemoveMenu(uInsertedMenuItem, MF_BYCOMMAND));
}

bool CSharedFilesCtrl::ShouldRouteSharedFilesCommand(UINT uAction) const
{
	if (uAction == MP_UNSHAREFILE && m_eFilter == FilterType::FileSystem)
		return false;

	switch (uAction) {
	case MP_POWERSHARE_DEFAULT:
	case MP_POWERSHARE_OFF:
	case MP_POWERSHARE_ON:
	case MP_POWERSHARE_AUTO:
	case MP_POWERSHARE_LIMITED:
	case MP_POWERSHARE_LIMIT:
	case MP_POWERSHARE_LIMIT_SET:
	case MP_SPREADBAR_DEFAULT:
	case MP_SPREADBAR_OFF:
	case MP_SPREADBAR_ON:
	case MP_SPREADBAR_RESET:
	case MP_HIDEOS_DEFAULT:
	case MP_HIDEOS_SET:
	case MP_SELECTIVE_CHUNK:
	case MP_SELECTIVE_CHUNK_0:
	case MP_SELECTIVE_CHUNK_1:
	case MP_PERMDEFAULT:
	case MP_PERMNONE:
	case MP_PERMFRIENDS:
	case MP_PERMALL:
	case MP_SHAREONLYTHENEED:
	case MP_SHAREONLYTHENEED_0:
	case MP_SHAREONLYTHENEED_1:
	case MP_RENAME:
	case MP_REMOVE:
	case MPG_DELETE:
	case MP_TRY_TO_GET_PREVIEW_PARTS:
	case MP_PAUSEONPREVIEW:
	case MP_UNSHAREFILE:
	case MP_UPDATE_METADATA:
	case MP_PRIOVERYLOW:
	case MP_PRIOLOW:
	case MP_PRIONORMAL:
	case MP_PRIOHIGH:
	case MP_PRIOVERYHIGH:
	case MP_PRIOAUTO:
	case MP_REMOVEFROMHISTORY:
	case MP_CLEARHISTORY:
	case MP_CREATECOLLECTION:
	case MP_VIEWPARTFILES:
	case MP_VIEWSHAREDFILES:
	case MP_VIEWDUPLICATEFILES:
		return true;
	}
	return false;
}

void CSharedFilesCtrl::QueueSharedFilesCommandFromCurrentSelection(UINT uAction)
{
	if ((uAction == MP_REMOVEFROMHISTORY || uAction == MP_CLEARHISTORY) && GetSelectedCount() >= BULK_OPERATION_MIN_ITEMS) {
		const std::vector<CString> vecItemKeys;
		StartSharedFilesBulkOperation(uAction, vecItemKeys, 0, 0);
		return;
	}

	CStringArray astrItemHashes;
	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		const int iItem = GetNextSelectedItem(pos);
		if (iItem >= 0 && static_cast<size_t>(iItem) < m_ListedItemsVector.size()) {
				CString strCommandKey(BuildSharedFileCommandKey(m_ListedItemsVector[static_cast<size_t>(iItem)]));
			if (!strCommandKey.IsEmpty())
				astrItemHashes.Add(strCommandKey);
		}
	}
	theApp.ExecuteSharedFilesCommand(uAction, astrItemHashes);
}

bool CSharedFilesCtrl::IsCurrentSharedFileForSharedFilesAction(const CKnownFile *pFile) const
{
	if (pFile == NULL || pFile->IsPartFile() || pFile->GetFilePath().IsEmpty() || theApp.sharedfiles == NULL)
		return false;
	if (theApp.sharedfiles->GetFileByID(pFile->GetFileHash()) == NULL)
		return false;
	if (theApp.knownfiles != NULL && theApp.knownfiles->IsOnDuplicates(pFile->GetFileName(), pFile->GetUtcFileDate(), pFile->GetFileSize()))
		return false;
	return true;
}

bool CSharedFilesCtrl::CanUnshareFile(const CShareableFile *pFile) const
{
	if (pFile == NULL || pFile->IsPartFile() || pFile->GetFilePath().IsEmpty() || theApp.sharedfiles == NULL)
		return false;

	const CString strFilePath(pFile->GetFilePath());
	const int iPathSeparator = strFilePath.ReverseFind(_T('\\'));
	if (iPathSeparator <= 0)
		return false;

	const CString strDirectory(strFilePath.Left(iPathSeparator));
	return theApp.sharedfiles->ShouldBeShared(strDirectory, strFilePath, false)
		&& !theApp.sharedfiles->ShouldBeShared(strDirectory, strFilePath, true);
}

bool CSharedFilesCtrl::CanUnshareSelectedSharedFiles()
{
	const int iSelectedItems = GetSelectedCount();
	if (iSelectedItems <= 0)
		return false;

	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		const int iItem = GetNextSelectedItem(pos);
		if (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size())
			return false;

		if (!CanUnshareFile(static_cast<CShareableFile*>(m_ListedItemsVector[static_cast<size_t>(iItem)])))
			return false;
	}

	return true;
}

bool CSharedFilesCtrl::CanDeleteSelectedSharedFilesFromDisk()
{
	const int iSelectedItems = GetSelectedCount();
	if (iSelectedItems <= 0)
		return false;

	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		const int iItem = GetNextSelectedItem(pos);
		if (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size())
			return false;

		CKnownFile *pFile = m_ListedItemsVector[static_cast<size_t>(iItem)];
		if (!IsCurrentSharedFileForSharedFilesAction(pFile))
			return false;
	}

	return true;
}

bool CSharedFilesCtrl::CanUpdateSelectedSharedFilesMetadata()
{
	const int iSelectedItems = GetSelectedCount();
	if (iSelectedItems <= 0)
		return false;

	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		const int iItem = GetNextSelectedItem(pos);
		if (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size())
			return false;
		if (!IsCurrentSharedFileForSharedFilesAction(m_ListedItemsVector[static_cast<size_t>(iItem)]))
			return false;
	}
	return true;
}

bool CSharedFilesCtrl::QueueDownloadRemoveCommandFromCurrentSelection(UINT uAction)
{
	if (uAction != MP_CANCEL && uAction != MP_CANCEL_FORGET)
		return false;
	if (theApp.downloadqueue == NULL)
		return true;

	const int iSelectedCount = GetSelectedCount();
	CString strFileList(GetResString(iSelectedCount == 1 ? _T("Q_CANCELDL2") : _T("Q_CANCELDL")));
	CStringArray astrDownloadHashes;
	astrDownloadHashes.SetSize(0, iSelectedCount > 16 ? iSelectedCount : 16);
	std::set<SSharedFilesHashKey, SSharedFilesHashKeyLess> setQueuedHashes;
	bool bValidDelete = false;
	bool bRemoveCompleted = false;
	int iDisplayFiles = 0;
	const int iMaxDisplayFiles = 10;

	for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
		const int iItem = GetNextSelectedItem(pos);
			if (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size())
				continue;
			const CKnownFile *pKnownFile = m_ListedItemsVector[static_cast<size_t>(iItem)];
			if (pKnownFile == NULL)
				continue;
		const CPartFile *pPartFile = theApp.downloadqueue->GetFileByID(pKnownFile->GetFileHash());
		if (pPartFile == NULL)
			continue;

		if (setQueuedHashes.insert(SSharedFilesHashKey(pPartFile->GetFileHash())).second)
			astrDownloadHashes.Add(md4str(pPartFile->GetFileHash()));

		if (pPartFile->GetStatus() != PS_COMPLETING && pPartFile->GetStatus() != PS_COMPLETE) {
			bValidDelete = true;
			if (++iDisplayFiles < iMaxDisplayFiles)
				strFileList.AppendFormat(_T("\n%s"), (LPCTSTR)pPartFile->GetFileName());
			else if (iDisplayFiles == iMaxDisplayFiles && pos != NULL)
				strFileList += _T("\n...");
		} else if (pPartFile->GetStatus() == PS_COMPLETE)
			bRemoveCompleted = true;
	}

	if (astrDownloadHashes.GetSize() == 0)
		return true;

	bool bConfirmed = bRemoveCompleted && !bValidDelete;
	if (bValidDelete) {
		BeginSharedFilesListReloadDefer();
		bConfirmed = CDarkMode::MessageBox(strFileList, MB_DEFBUTTON2 | MB_ICONQUESTION | MB_YESNO) == IDYES;
		EndSharedFilesListReloadDefer();
	}

	if (bConfirmed) {
		BeginBackendDownloadRemoveVisibleRows(astrDownloadHashes);
		theApp.ExecuteDownloadListRemoveCommand(astrDownloadHashes, uAction != MP_CANCEL_FORGET, true, CemuleApp::BackendCommandSourceUi, CemuleApp::BackendCommandOrderingDownloadList, _T("download-list:remove-from-shared-files"), true);
	}
	return true;
}

bool CSharedFilesCtrl::ExecuteSharedFilesCommandFromEvent(UINT uAction, const std::vector<CString> &vecItemHashes, uint64 uSequence, uint64 uCorrelationId)
{
	if (m_bExecutingSharedFilesCommand)
		return true;

	SetRedraw(false);
	for (int iSelectedItem = GetNextItem(-1, LVIS_SELECTED); iSelectedItem != -1; iSelectedItem = GetNextItem(-1, LVIS_SELECTED))
		SetItemState(iSelectedItem, 0, LVIS_SELECTED | LVIS_FOCUSED);

	bool bFocused = false;
	for (std::vector<CString>::const_iterator it = vecItemHashes.begin(); it != vecItemHashes.end(); ++it) {
		CString strCommandKey(*it);
		strCommandKey.Trim();
		const int iItem = FindListedIndexByCommandKey(strCommandKey);
		if (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size())
			continue;

			CKnownFile *pFile = m_ListedItemsVector[static_cast<size_t>(iItem)];
			if (pFile == NULL)
				continue;

		m_sharedFilesBulkResolver.SetAt(strCommandKey, pFile);
		UINT uState = LVIS_SELECTED;
		if (!bFocused) {
			uState |= LVIS_FOCUSED;
			bFocused = true;
		}
		SetItemState(iItem, uState, LVIS_SELECTED | LVIS_FOCUSED);
	}
	SetRedraw(true);

	if (IsSharedFilesBulkOperationAction(uAction)) {
		StartSharedFilesBulkOperation(uAction, vecItemHashes, uSequence, uCorrelationId);
		return false;
	}

	m_bExecutingSharedFilesCommand = true;
	OnCommand(static_cast<WPARAM>(uAction), 0);
	m_bExecutingSharedFilesCommand = false;
	return true;
}


BOOL CSharedFilesCtrl::OnCommand(WPARAM wParam, LPARAM)
{
	wParam = LOWORD(wParam);
	if (!m_bExecutingSharedFilesCommand && (wParam == MP_REMOVE || wParam == MPG_DELETE)) {
		const std::vector<CString> vecItemKeys;
		StartSharedFilesBulkOperation(static_cast<UINT>(wParam), vecItemKeys, 0, 0);
		return TRUE;
	}
	if (!m_bExecutingSharedFilesCommand && wParam == MP_UPDATE_METADATA && !CanUpdateSelectedSharedFilesMetadata())
		return TRUE;
	if (!m_bExecutingSharedFilesCommand && (wParam == MP_CANCEL || wParam == MP_CANCEL_FORGET))
		return QueueDownloadRemoveCommandFromCurrentSelection(static_cast<UINT>(wParam)) ? TRUE : FALSE;
	if (!m_bExecutingSharedFilesCommand && (wParam == MP_REMOVEFROMHISTORY || wParam == MP_CLEARHISTORY)) {
		if (wParam == MP_REMOVEFROMHISTORY && GetSelectedCount() == 0)
			return TRUE;
		const std::vector<CString> vecItemKeys;
		StartSharedFilesBulkOperation(static_cast<UINT>(wParam), vecItemKeys, 0, 0);
		return TRUE;
	}
	if (!m_bExecutingSharedFilesCommand && ShouldRouteSharedFilesCommand(static_cast<UINT>(wParam))) {
		QueueSharedFilesCommandFromCurrentSelection(static_cast<UINT>(wParam));
		return TRUE;
	}

	CKnownFile* pKnownFile = NULL;
	bool m_bFirstFile = true;
	CTypedPtrList<CPtrList, CShareableFile*> selectedList;
	CTypedPtrList<CPtrList, CPartFile*> selectedDownloadList;
		CPartFile* pSingleDownloadFile = NULL;
		for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
			int index = GetNextSelectedItem(pos);
			if (index >= 0 && static_cast<size_t>(index) < m_ListedItemsVector.size()) {
				CKnownFile* cur_file = m_ListedItemsVector[index];
				if (cur_file != NULL) {
					selectedList.AddTail(static_cast<CShareableFile*>(cur_file));
					CPartFile* pDownloadFile = theApp.downloadqueue->GetFileByID(cur_file->GetFileHash());
				if (pDownloadFile != NULL) {
					selectedDownloadList.AddTail(pDownloadFile);
					if (selectedDownloadList.GetCount() == 1)
						pSingleDownloadFile = pDownloadFile;
				}
				if (m_bFirstFile && selectedList.GetCount() == 1) {
					pKnownFile = cur_file;
					m_bFirstFile = false;
				}
			}
		}
	}

	if (wParam == MP_VIEWPARTFILES || wParam == MP_VIEWSHAREDFILES || wParam == MP_VIEWDUPLICATEFILES || wParam == MP_CREATECOLLECTION || wParam == MP_CLEARHISTORY || wParam == MP_FIND || !selectedList.IsEmpty()) {
		CShareableFile* file = (selectedList.GetCount() == 1) ? selectedList.GetHead() : NULL;


		switch (wParam) {
		case Irc_SetSendLink:
			if (pKnownFile != NULL)
				theApp.emuledlg->ircwnd->SetSendFileString(pKnownFile->GetED2kLink());
			break;
		case MP_CUT:
			{
				CString m_strFileNames;
					for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
						int index = GetNextSelectedItem(pos);
						if (index >= 0 && static_cast<size_t>(index) < m_ListedItemsVector.size()) {
							CKnownFile* pFile = m_ListedItemsVector[index];
							if (pFile != NULL) {
								if (!m_strFileNames.IsEmpty())
									m_strFileNames += _T("\r\n");
							m_strFileNames += pFile->GetFileName();
						}
					}
				}

				if (!m_strFileNames.IsEmpty()) {
					theApp.CopyTextToClipboard(m_strFileNames);
					theApp.emuledlg->statusbar->SetText(GetResString(_T("FILE_NAME_COPIED_TO_CLIPBOARD")), SBarLog, 0);
				}
			}
			break;
		case MP_COPYSELECTED:
		case MP_GETED2KLINK:
		{
				CString str;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					CKnownFile *pfile = static_cast<CKnownFile*>(selectedList.GetNext(pos));
					if (pfile != NULL && pfile->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
						if (!str.IsEmpty())
							str += _T("\r\n");
						str += pfile->GetED2kLink();
					}
				}
				if (!str.IsEmpty()) {
					theApp.CopyTextToClipboard(str);
					theApp.emuledlg->statusbar->SetText(GetResString(_T("ED2K_LINK_COPIED_TO_CLIPBOARD")), SBarLog, 0);
				}
			}
			break;
		case MP_POWERSHARE_DEFAULT:
		case MP_POWERSHARE_OFF:
		case MP_POWERSHARE_ON:
		case MP_POWERSHARE_AUTO:
		case MP_POWERSHARE_LIMITED:
			SetRedraw(false);
			while (!selectedList.IsEmpty()) {
				CShareableFile* pSelectedFile = selectedList.RemoveHead();
				if (pSelectedFile == NULL || !pSelectedFile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
					continue;

				CKnownFile* pSelectedKnownFile = static_cast<CKnownFile*>(pSelectedFile);
				switch (wParam) {
				case MP_POWERSHARE_DEFAULT:
					pSelectedKnownFile->SetPowerShared(-1);
					break;
				case MP_POWERSHARE_OFF:
					pSelectedKnownFile->SetPowerShared(0);
					break;
				case MP_POWERSHARE_ON:
					pSelectedKnownFile->SetPowerShared(1);
					break;
				case MP_POWERSHARE_AUTO:
					pSelectedKnownFile->SetPowerShared(2);
					break;
				default:
					pSelectedKnownFile->SetPowerShared(3);
					break;
				}
				UpdateFile(pSelectedKnownFile);
			}
			SetRedraw(true);
			break;
		case MP_POWERSHARE_LIMIT:
		case MP_POWERSHARE_LIMIT_SET:
			{
				int iNewPowerShareLimit = -1;
				if (wParam == MP_POWERSHARE_LIMIT_SET) {
					InputBox inputbox;
					CString strCurrentLimit;
					if (pKnownFile != NULL)
						strCurrentLimit.Format(_T("%i"), (pKnownFile->GetPowerShareLimit() >= 0) ? pKnownFile->GetPowerShareLimit() : thePrefs.GetPowerShareLimit());
					else
						strCurrentLimit = _T("0");
					inputbox.SetLabels(GetResString(_T("POWERSHARE")), GetResString(_T("POWERSHARE_LIMIT")), strCurrentLimit);
					if (inputbox.DoModal() != IDOK || !TryParseMenuInputNonNegativeInt(inputbox.GetInput(), iNewPowerShareLimit))
						break;
				}

				SetRedraw(false);
				while (!selectedList.IsEmpty()) {
					CShareableFile* pSelectedFile = selectedList.RemoveHead();
					if (pSelectedFile == NULL || !pSelectedFile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
						continue;

					CKnownFile* pSelectedKnownFile = static_cast<CKnownFile*>(pSelectedFile);
					pSelectedKnownFile->SetPowerShareLimit(iNewPowerShareLimit);
					UpdateFile(pSelectedKnownFile);
				}
				SetRedraw(true);
			}
			break;
		case MP_SPREADBAR_DEFAULT:
		case MP_SPREADBAR_OFF:
		case MP_SPREADBAR_ON:
			SetRedraw(false);
			while (!selectedList.IsEmpty()) {
				CShareableFile* pSelectedFile = selectedList.RemoveHead();
				if (pSelectedFile == NULL || !pSelectedFile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
					continue;

				CKnownFile* pSelectedKnownFile = static_cast<CKnownFile*>(pSelectedFile);
				switch (wParam) {
				case MP_SPREADBAR_DEFAULT:
					pSelectedKnownFile->SetSpreadbarSetStatus(-1);
					break;
				case MP_SPREADBAR_OFF:
					pSelectedKnownFile->SetSpreadbarSetStatus(0);
					break;
				default:
					pSelectedKnownFile->SetSpreadbarSetStatus(1);
					break;
				}
				UpdateFile(pSelectedKnownFile);
			}
			SetRedraw(true);
			break;
		case MP_SPREADBAR_RESET:
			SetRedraw(false);
			while (!selectedList.IsEmpty()) {
				CShareableFile* pSelectedFile = selectedList.RemoveHead();
				if (pSelectedFile == NULL || !pSelectedFile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
					continue;

				CKnownFile* pSelectedKnownFile = static_cast<CKnownFile*>(pSelectedFile);
				pSelectedKnownFile->statistic.ResetSpreadBar();
				UpdateFile(pSelectedKnownFile);
			}
			SetRedraw(true);
			break;
		case MP_HIDEOS_DEFAULT:
		case MP_HIDEOS_SET:
			{
				int iNewHideOS = -1;
				if (wParam == MP_HIDEOS_SET) {
					InputBox inputbox;
					CString strCurrentHideOS;
					if (pKnownFile != NULL)
						strCurrentHideOS.Format(_T("%i"), (pKnownFile->GetHideOS() >= 0) ? pKnownFile->GetHideOS() : thePrefs.GetHideOvershares());
					else
						strCurrentHideOS = _T("0");
					inputbox.SetLabels(GetResString(_T("HIDE_OVER_SHARE_MENU")), GetResString(_T("HIDEOVERSHARES")), strCurrentHideOS);
					if (inputbox.DoModal() != IDOK || !TryParseMenuInputNonNegativeInt(inputbox.GetInput(), iNewHideOS))
						break;
				}

				SetRedraw(false);
				while (!selectedList.IsEmpty()) {
					CShareableFile* pSelectedFile = selectedList.RemoveHead();
					if (pSelectedFile == NULL || !pSelectedFile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
						continue;

					CKnownFile* pSelectedKnownFile = static_cast<CKnownFile*>(pSelectedFile);
					pSelectedKnownFile->SetHideOS(iNewHideOS);
					UpdateFile(pSelectedKnownFile);
				}
				SetRedraw(true);
			}
			break;
		case MP_SELECTIVE_CHUNK:
		case MP_SELECTIVE_CHUNK_0:
		case MP_SELECTIVE_CHUNK_1:
			SetRedraw(false);
			while (!selectedList.IsEmpty()) {
				CShareableFile* pSelectedFile = selectedList.RemoveHead();
				if (pSelectedFile == NULL || !pSelectedFile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
					continue;

				CKnownFile* pSelectedKnownFile = static_cast<CKnownFile*>(pSelectedFile);
				switch (wParam) {
				case MP_SELECTIVE_CHUNK:
					pSelectedKnownFile->SetSelectiveChunk(-1);
					break;
				case MP_SELECTIVE_CHUNK_0:
					pSelectedKnownFile->SetSelectiveChunk(0);
					break;
				default:
					pSelectedKnownFile->SetSelectiveChunk(1);
					break;
				}
				UpdateFile(pSelectedKnownFile);
			}
			SetRedraw(true);
			break;
		case MP_PERMDEFAULT:
		case MP_PERMNONE:
		case MP_PERMFRIENDS:
		case MP_PERMALL:
			SetRedraw(false);
			while (!selectedList.IsEmpty()) {
				CShareableFile* pSelectedFile = selectedList.RemoveHead();
				if (pSelectedFile == NULL || !pSelectedFile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
					continue;

				CKnownFile* pSelectedKnownFile = static_cast<CKnownFile*>(pSelectedFile);
				switch (wParam) {
				case MP_PERMDEFAULT:
					pSelectedKnownFile->SetPermissions(-1);
					break;
				case MP_PERMNONE:
					pSelectedKnownFile->SetPermissions(PERM_NOONE);
					break;
				case MP_PERMFRIENDS:
					pSelectedKnownFile->SetPermissions(PERM_FRIENDS);
					break;
				default:
					pSelectedKnownFile->SetPermissions(PERM_ALL);
					break;
				}
				UpdateFile(pSelectedKnownFile);
			}
			SetRedraw(true);
			Invalidate();
			break;
		case MP_SHAREONLYTHENEED:
		case MP_SHAREONLYTHENEED_0:
		case MP_SHAREONLYTHENEED_1:
			SetRedraw(false);
			while (!selectedList.IsEmpty()) {
				CShareableFile* pSelectedFile = selectedList.RemoveHead();
				if (pSelectedFile == NULL || !pSelectedFile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
					continue;

				CKnownFile* pSelectedKnownFile = static_cast<CKnownFile*>(pSelectedFile);
				switch (wParam) {
				case MP_SHAREONLYTHENEED:
					pSelectedKnownFile->SetShareOnlyTheNeed(-1);
					break;
				case MP_SHAREONLYTHENEED_0:
					pSelectedKnownFile->SetShareOnlyTheNeed(0);
					break;
				default:
					pSelectedKnownFile->SetShareOnlyTheNeed(1);
					break;
				}
				UpdateFile(pSelectedKnownFile);
			}
			SetRedraw(true);
			break;
#if defined(_DEBUG)
		//JOHNTODO: Not for release as we need kad lowID users in the network to see how well this works. Also, we do not support these links yet.
		case MP_GETKADSOURCELINK:
			{
				CString str;
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					const CKnownFile *pfile = static_cast<CKnownFile*>(selectedList.GetNext(pos));
					if (pfile->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
						if (!str.IsEmpty())
							str += _T("\r\n");
						str += theApp.CreateKadSourceLink(pfile);
					}
				}
				theApp.CopyTextToClipboard(str);
			}
			break;
#endif
		// file operations
		case MP_OPEN:
#if TEST_FRAMEGRABBER //see also FrameGrabThread::GrabFrames
			if (file) {
				CKnownFile *previewFile = theApp.sharedfiles->GetFileByID(file->GetFileHash());
				if (previewFile != NULL)
					previewFile->GrabImage(4, 15, true, 450, this);
				break;
			}
#endif
		case IDA_ENTER:
			if (file && !file->IsPartFile())
				OpenFile(file);
			break;
		case MP_INSTALL_SKIN:
			if (file && !file->IsPartFile())
				InstallSkin(file->GetFilePath());
			break;
		case MP_OPENFOLDER:
			if (file && !file->IsPartFile()) {
				CString sParam;
				sParam.Format(_T("/select,\"%s\""), (LPCTSTR)file->GetFilePath());
				ShellOpen(_T("explorer"), sParam);
			}
			break;
		case MP_RENAME:
		case MPG_F2:
			if (pKnownFile && !pKnownFile->IsPartFile()) {
				InputBox inputbox;
				inputbox.SetLabels(GetResNoAmp(_T("RENAME")), GetResString(_T("DL_FILENAME")), pKnownFile->GetFileName());
				inputbox.SetEditFilenameMode();
				inputbox.DoModal();
				const CString &newname(inputbox.GetInput());
				if (!inputbox.WasCancelled() && !newname.IsEmpty()) {
					// at least prevent users from specifying something like "..\dir\file"
					if (newname.FindOneOf(sBadFileNameChar) >= 0) {
						CDarkMode::MessageBox(GetErrorMessage(ERROR_BAD_PATHNAME));
						break;
					}

					CString newpath(pKnownFile->GetPath());
					if (!newpath.IsEmpty() && newpath[newpath.GetLength() - 1] != _T('\\'))
						newpath += _T('\\');

					newpath += newname;
					bool bSharedFile = (theApp.sharedfiles->GetFileByID(pKnownFile->GetFileHash()) != NULL);
					CString oldpath;
					if (bSharedFile) {
						const CString src = pKnownFile->GetFilePath();
						const CString dst = newpath;
						const CString lsrc = PreparePathForWin32LongPath(src);
						const CString ldst = PreparePathForWin32LongPath(dst);
						oldpath = src;

						if (!::MoveFileEx(lsrc, ldst, MOVEFILE_COPY_ALLOWED)) {
							CString strError;
							strError.Format(GetResString(_T("ERR_RENAMESF")), (LPCTSTR)src, (LPCTSTR)dst, (LPCTSTR)EscPercent(GetErrorMessage(::GetLastError())));
							CDarkMode::MessageBox(strError);
							break;
						}
					}

					const CString oldname = pKnownFile->GetFileName();
					const uint64 oldsize = pKnownFile->GetFileSize();
					if (pKnownFile->IsKindOf(RUNTIME_CLASS(CPartFile))) {
						static_cast<CPartFile*>(pKnownFile)->SetAutoRenameToMajorityName(false);
						pKnownFile->SetFileName(newname);
						static_cast<CPartFile*>(pKnownFile)->SetFullName(newpath);
					} else {
						pKnownFile->SetFileName(newname);
					}
					if (theApp.knownfiles != NULL)
						theApp.knownfiles->ReindexKnownFile(pKnownFile, oldname, oldsize);

					if (bSharedFile) {
						pKnownFile->SetFilePath(newpath);
						theApp.sharedfiles->UpdateSharedPathCache(pKnownFile, oldpath);
					}
					UpdateFile(pKnownFile);
				}
			} else
				MessageBeep(MB_OK);
			break;
		case MP_REMOVE:
		case MPG_DELETE:
			{
				if (!CanDeleteSelectedSharedFilesFromDisk())
					break;

				if (pKnownFile && (pKnownFile->IsPartFile() || (m_eFilter == FilterType::History && theApp.sharedfiles != NULL && theApp.sharedfiles->GetFileByID(pKnownFile->GetFileHash()) == NULL)))
					break;

				if (LocMessageBox(_T("CONFIRM_FILEDELETE"), MB_ICONWARNING | MB_DEFBUTTON2 | MB_YESNO, 0) != IDYES)
					return TRUE;

				// Shared Files bulk removals may still use one ReloadList, but we also batch list state to avoid restoring large selections per item.
				const bool bWillReloadListLater = (selectedList.GetCount() > 1);
				const bool bBatchListState = (bWillReloadListLater && m_eFilter != FilterType::FileSystem);
				const uint32 uListStateID = m_uFilterID;
				if (bBatchListState)
					BeginListStateBatch(uListStateID, kSharedFilesViewState);

				SetRedraw(false);
				bool bRemovedItems = false;
				while (!selectedList.IsEmpty()) {
					CShareableFile *myfile = selectedList.RemoveHead();
					if (!myfile || myfile->IsPartFile())
						continue;

					bool delsucc = ShellDeleteFile(myfile->GetFilePath(), false);
					if (delsucc) {
						if (myfile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
							theApp.sharedfiles->RemoveFile(static_cast<CKnownFile*>(myfile), true, bWillReloadListLater);
						else
							RemoveFile(static_cast<CKnownFile*>(myfile), true, bWillReloadListLater);
						bRemovedItems = true;
						if (myfile->IsKindOf(RUNTIME_CLASS(CPartFile)))
							theApp.emuledlg->transferwnd->GetDownloadList()->ClearCompleted(static_cast<CPartFile*>(myfile));
					} else {
						CString strError;
						strError.Format(GetResString(_T("ERR_DELFILE")), (LPCTSTR)myfile->GetFilePath());
						strError.AppendFormat(_T("\r\n\r\n%s"), (LPCTSTR)EscPercent(GetErrorMessage(GetLastError())));
						CDarkMode::MessageBox(strError);
					}
				}
				SetRedraw(true);
				if (bRemovedItems && bWillReloadListLater)
					ReloadList(false, kSharedFilesViewState);
				if (bBatchListState)
					EndListStateBatch(uListStateID, kSharedFilesViewState, false);
				if (bRemovedItems) {
					AutoSelectItem();
					// Depending on <no-idea> this does not always cause an LVN_ITEMACTIVATE
					// message to be sent. So, explicitly redraw the item.
					theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails();
					theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged(); // might have been a single shared file
				}
			}
			break;
		case MP_CANCEL_FORGET:
		case MP_CANCEL:
			QueueDownloadRemoveCommandFromCurrentSelection(static_cast<UINT>(wParam));
			break;
		case MP_TRY_TO_GET_PREVIEW_PARTS:
			if (selectedDownloadList.GetCount() == 1 && pSingleDownloadFile != NULL)
				pSingleDownloadFile->SetPreviewPrio(!pSingleDownloadFile->GetPreviewPrio());
			break;
		case MP_PREVIEW:
			if (selectedDownloadList.GetCount() == 1 && pSingleDownloadFile != NULL)
				pSingleDownloadFile->PreviewFile();
			break;
		case MP_PREVIEW1:
		case MP_PREVIEW2:
		case MP_PREVIEW3:
		case MP_PREVIEW4:
		case MP_PREVIEW5:
		case MP_PREVIEW6:
		case MP_PREVIEW7:
		case MP_PREVIEW8:
		case MP_PREVIEW9:
		case MP_PREVIEW10:
			if (selectedDownloadList.GetCount() == 1 && pSingleDownloadFile != NULL)
				pSingleDownloadFile->PreviewFile((UINT)wParam - MP_PREVIEW1);
			break;
		case MP_PAUSEONPREVIEW:
			{
				bool bAllPausedOnPreview = true;
				for (POSITION pos = selectedDownloadList.GetHeadPosition(); pos != NULL && bAllPausedOnPreview;)
					bAllPausedOnPreview = selectedDownloadList.GetNext(pos)->IsPausingOnPreview();
				while (!selectedDownloadList.IsEmpty()) {
					CPartFile* pPartFile = selectedDownloadList.RemoveHead();
					if (pPartFile->IsPreviewableFileType() && !pPartFile->IsReadyForPreview())
						pPartFile->SetPauseOnPreview(!bAllPausedOnPreview);
				}
			}
			break;
		case MP_UNSHAREFILE:
			{
				SetRedraw(false);
				bool bUnsharedItems = false;
				while (!selectedList.IsEmpty()) {
					CShareableFile* myfile = selectedList.RemoveHead();
					if (CanUnshareFile(myfile))
					{
						bUnsharedItems |= theApp.sharedfiles->ExcludeFile(myfile->GetFilePath());
						ASSERT(bUnsharedItems);
						if (bUnsharedItems && myfile->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
							CKnownFile* pfile = static_cast<CKnownFile*>(myfile);
							UpdateFile(pfile);
						}
					}
				}
				SetRedraw(true);
				if (bUnsharedItems) {
					theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails();
					theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged();
					if (GetFirstSelectedItemPosition() == NULL)
						AutoSelectItem();
				}
			}
			break;
		case MP_UPDATE_METADATA:
		{
			if (!CanUpdateSelectedSharedFilesMetadata())
				break;
			while (!selectedList.IsEmpty()) {
				CShareableFile* myfile = selectedList.RemoveHead();
				if (!myfile || myfile->IsPartFile())
					continue;
				if (myfile->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
					CKnownFile* pfile = static_cast<CKnownFile*>(myfile);
					if (theApp.sharedfiles != NULL)
						theApp.sharedfiles->QueueMetaDataUpdateForFile(pfile);
				}
			}
		}
		break;
		case MP_CMT:
			ShowFileDialog(selectedList, IDD_COMMENT);
			break;
		case MPG_ALTENTER:
		case MP_DETAIL:
			ShowFileDialog(selectedList);
			break;
		case MP_FIND:
			OnFindStart();
			break;
		case MP_CREATECOLLECTION:
			{
				CCollection *pCollection = new CCollection();
				for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
					CShareableFile *pFile = selectedList.GetNext(pos);
					if (pFile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
						pCollection->AddFileToCollection(pFile, true);
				}
				CCollectionCreateDialog dialog;
				dialog.SetCollection(pCollection, true);
				dialog.DoModal();
				//We delete this collection object because when the newly created
				//collection file is added to the shared file list, it is read and verified
				//and which creates the collection object that is attached to that file.
				delete pCollection;
			}
			break;
		case MP_SEARCHAUTHOR:
			if (pKnownFile && pKnownFile->m_pCollection) {
				SSearchParams *pParams = new SSearchParams;
				pParams->strExpression = pKnownFile->m_pCollection->GetCollectionAuthorKeyString();
				pParams->eType = SearchTypeKademlia;
				pParams->strFileType = _T(ED2KFTSTR_EMULECOLLECTION);
				pParams->strSpecialTitle = pKnownFile->m_pCollection->m_sCollectionAuthorName;
				if (pParams->strSpecialTitle.GetLength() > 50) {
					pParams->strSpecialTitle.Truncate(50);
					pParams->strSpecialTitle += _T("...");
				}

				theApp.emuledlg->searchwnd->m_pwndResults->StartSearch(pParams);
			}
			break;
		case MP_VIEWCOLLECTION:
			if (pKnownFile && pKnownFile->m_pCollection) {
				CCollectionViewDialog dialog;
				dialog.SetCollection(pKnownFile->m_pCollection);
				dialog.DoModal();
			}
			break;
		case MP_MODIFYCOLLECTION:
			if (pKnownFile && pKnownFile->m_pCollection) {
				CCollectionCreateDialog dialog;
				CCollection *pCollection = new CCollection(pKnownFile->m_pCollection);
				dialog.SetCollection(pCollection, false);
				dialog.DoModal();
				delete pCollection;
			}
			break;
		case MP_SHOWED2KLINK:
			ShowFileDialog(selectedList, IDD_ED2KLINK);
			break;
		case MP_PRIOVERYLOW:
		case MP_PRIOLOW:
		case MP_PRIONORMAL:
		case MP_PRIOHIGH:
		case MP_PRIOVERYHIGH:
		case MP_PRIOAUTO:
			for (POSITION pos = selectedList.GetHeadPosition(); pos != NULL;) {
				CKnownFile *pfile = static_cast<CKnownFile*>(selectedList.GetNext(pos));
				if (pfile->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
					pfile->SetAutoUpPriority(wParam == MP_PRIOAUTO);
					switch (wParam) {
					case MP_PRIOVERYLOW:
						pfile->SetUpPriority(PR_VERYLOW);
						break;
					case MP_PRIOLOW:
						pfile->SetUpPriority(PR_LOW);
						break;
					case MP_PRIONORMAL:
						pfile->SetUpPriority(PR_NORMAL);
						break;
					case MP_PRIOHIGH:
						pfile->SetUpPriority(PR_HIGH);
						break;
					case MP_PRIOVERYHIGH:
						pfile->SetUpPriority(PR_VERYHIGH);
						break;
					case MP_PRIOAUTO:
						pfile->UpdateAutoUpPriority();
					}
					UpdateFile(pfile);
				}
			}
			break;
			case MP_REMOVEFROMHISTORY:
			{
				if (selectedList.IsEmpty() || CDarkMode::MessageBox(GetResString(_T("FILE_HISTORY_REMOVE_QUESTION")),	MB_YESNO | MB_ICONQUESTION) != IDYES) 
					break;

				const bool bMultipleFiles = (selectedList.GetCount() > 1);
				const bool bBatchListState = (bMultipleFiles && m_eFilter != FilterType::FileSystem);
				const uint32 uListStateID = m_uFilterID;
				if (bBatchListState)
					BeginListStateBatch(uListStateID, kSharedFilesViewState);

				while (!selectedList.IsEmpty()) {
					CShareableFile* myfile = selectedList.RemoveHead();
					if (myfile->IsKindOf(RUNTIME_CLASS(CKnownFile)))
						RemoveFromHistory(static_cast<CKnownFile*>(myfile), bMultipleFiles); // For multiple selected files we'll only set the item to NULL in map m_ListedItemsVector and reload at the end. This way will be faster.
				}

				if (bMultipleFiles)
					ReloadList(false, kSharedFilesViewState);
				if (bBatchListState)
					EndListStateBatch(uListStateID, kSharedFilesViewState, false);
			}
			break;
			case MP_CLEARHISTORY:
			{
				if (CDarkMode::MessageBox(GetResString(_T("FILE_HISTORY_PURGE_QUESTION")), MB_YESNO | MB_ICONQUESTION) == IDYES) {
					theApp.knownfiles->ClearHistory();
					ReloadList(false, kSharedFilesViewState);
				}
			}
			break;
			case MP_VIEWPARTFILES:
				thePrefs.SetFileHistoryShowPart(!thePrefs.GetFileHistoryShowPart());
				ReloadList(false, kSharedFilesViewState);
				break;
			case MP_VIEWSHAREDFILES:
				thePrefs.SetFileHistoryShowShared(!thePrefs.GetFileHistoryShowShared());
				ReloadList(false, kSharedFilesViewState);
			break;
			case MP_VIEWDUPLICATEFILES:
				thePrefs.SetFileHistoryShowDuplicate(!thePrefs.GetFileHistoryShowDuplicate());
				ReloadList(false, kSharedFilesViewState);
				break;
		default:
			if (wParam >= MP_PREVIEW_APP_MIN && wParam <= MP_PREVIEW_APP_MAX) {
				if (selectedDownloadList.GetCount() == 1 && pSingleDownloadFile != NULL)
					thePreviewApps.RunApp(pSingleDownloadFile, (UINT)wParam);
			}
				else if (wParam >= MP_WEBURL && wParam <= MP_WEBURL + 256) {
					for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
						int index = GetNextSelectedItem(pos);
						if (index >= 0 && static_cast<size_t>(index) < m_ListedItemsVector.size()) {
							CKnownFile* pFile = m_ListedItemsVector[index];
							if (pFile != NULL)
								theWebServices.RunURL(pFile, (UINT)wParam);
						}
					}
			}
		}
	}
	return TRUE;
}

void CSharedFilesCtrl::OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	const LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	bool sortAscending;
	if (GetSortItem() != pNMLV->iSubItem)
		switch (pNMLV->iSubItem) {
		case 3:  // Priority
		case 5:  // Requests
		case 6:  // Accepted Requests
		case 7:  // Transferred Data
		case 10: // Complete Sources
		case 11: // Shared ed2k/kad
		case kSharedFilesColumnLastRequest:
			// Keep the current 'm_aSortBySecondValue' for supported columns, but reset to 'descending'
			sortAscending = false;
			break;
		default:
			sortAscending = true;
		}
	else
		sortAscending = !GetSortAscending();

	// Ornis 4-way-sorting
	int adder = 0;
	if (pNMLV->iSubItem >= 5 && pNMLV->iSubItem <= 7) { // 5=SF_REQUESTS, 6=SF_ACCEPTS, 7=SF_TRANSFERRED
		ASSERT(pNMLV->iSubItem - 5 < _countof(m_aSortBySecondValue));
		if (GetSortItem() == pNMLV->iSubItem && !sortAscending) // check for 'descending' because the initial sort order is also 'descending'
			m_aSortBySecondValue[pNMLV->iSubItem - 5] = !m_aSortBySecondValue[pNMLV->iSubItem - 5];
		if (m_aSortBySecondValue[pNMLV->iSubItem - 5])
			adder = 100;
	} else if (pNMLV->iSubItem == 11) { // 11=SHAREDTITLE
		ASSERT(3 < _countof(m_aSortBySecondValue));
		if (GetSortItem() == pNMLV->iSubItem && !sortAscending) // check for 'descending' because the initial sort order is also 'descending'
			m_aSortBySecondValue[3] = !m_aSortBySecondValue[3];
		if (m_aSortBySecondValue[3])
			adder = 100;
	}

	// Sort table
	if (adder == 0)
		SetSortArrow(pNMLV->iSubItem, sortAscending);
	else
		SetSortArrow(pNMLV->iSubItem, sortAscending ? arrowDoubleUp : arrowDoubleDown);

	UpdateSortHistory(MAKELONG(pNMLV->iSubItem + adder, !sortAscending));
	m_pSortParam = MAKELONG(pNMLV->iSubItem + adder, !sortAscending);
	ReloadList(true, kSharedFilesViewState);
	*pResult = 0;
}

int CALLBACK CSharedFilesCtrl::SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort)
{
	const CShareableFile *item1 = reinterpret_cast<CShareableFile*>(lParam1);
	const CShareableFile *item2 = reinterpret_cast<CShareableFile*>(lParam2);

	bool bSortAscending = !HIWORD(lParamSort);

	int iResult = 0;
	bool bExtColumn = false;

	switch (LOWORD(lParamSort)) {
	case 0: //file name
		iResult = CompareLocaleStringNoCase(item1->GetFileName(), item2->GetFileName());
		break;
	case 1: //file size
		iResult = CompareUnsigned(item1->GetFileSize(), item2->GetFileSize());
		break;
	case 2: //file type
		iResult = CompareLocaleStringNoCase(item1->GetFileTypeDisplayStr(), item2->GetFileTypeDisplayStr());
		// if the type is equal, sub-sort by extension
		if (iResult == 0) {
			CString pszExt1(::PathFindExtension(item1->GetFileName()));
			CString pszExt2(::PathFindExtension(item2->GetFileName()));
			if (pszExt1.IsEmpty() != pszExt2.IsEmpty())
				iResult = pszExt1.IsEmpty() ? 1 : -1;
			else if (!pszExt1.IsEmpty())
				iResult = CompareLocaleStringNoCase(pszExt1, pszExt2);
		}
		break;
	case 9: //folder
		iResult = CompareLocaleStringNoCase(item1->GetPath(), item2->GetPath());
		break;
	default:
		bExtColumn = true;
	}

	if (bExtColumn) {
		if (item1->IsKindOf(RUNTIME_CLASS(CKnownFile)) && !item2->IsKindOf(RUNTIME_CLASS(CKnownFile)))
			iResult = -1;
		else if (!item1->IsKindOf(RUNTIME_CLASS(CKnownFile)) && item2->IsKindOf(RUNTIME_CLASS(CKnownFile)))
			iResult = 1;
		else if (item1->IsKindOf(RUNTIME_CLASS(CKnownFile)) && item2->IsKindOf(RUNTIME_CLASS(CKnownFile))) {
			const CKnownFile *kitem1 = static_cast<const CKnownFile*>(item1);
			const CKnownFile *kitem2 = static_cast<const CKnownFile*>(item2);

			switch (LOWORD(lParamSort)) {
			case 3: //prio
				{
					uint8 p1 = kitem1->GetUpPriority() + 1;
					if (p1 == 5)
						p1 = 0;
					uint8 p2 = kitem2->GetUpPriority() + 1;
					if (p2 == 5)
						p2 = 0;
					iResult = p1 - p2;
				}
				break;
			case 4: //fileID
				iResult = memcmp(kitem1->GetFileHash(), kitem2->GetFileHash(), 16);
				break;
			case 5: //requests
				iResult = CompareUnsigned(kitem1->statistic.GetRequests(), kitem2->statistic.GetRequests());
				break;
			case 6: //accepted requests
				iResult = CompareUnsigned(kitem1->statistic.GetAccepts(), kitem2->statistic.GetAccepts());
				break;
			case 7: //all transferred
				iResult = CompareUnsigned(kitem1->statistic.GetTransferred(), kitem2->statistic.GetTransferred());
				break;
			case 8: //shared status
				iResult = CompareUnsigned(kitem1->GetPartCount(), kitem2->GetPartCount());
				break;
			case 10: //complete sources
				iResult = CompareUnsigned(kitem1->m_nCompleteSourcesCount, kitem2->m_nCompleteSourcesCount);
				break;
			case 11: //ed2k shared
				iResult = kitem1->GetPublishedED2K() - kitem2->GetPublishedED2K();
				break;
			case 12:
				iResult = CompareOptLocaleStringNoCaseUndefinedAtBottom(kitem1->GetStrTagValue(FT_MEDIA_ARTIST), kitem2->GetStrTagValue(FT_MEDIA_ARTIST), bSortAscending);
				break;
			case 13:
				iResult = CompareOptLocaleStringNoCaseUndefinedAtBottom(kitem1->GetStrTagValue(FT_MEDIA_ALBUM), kitem2->GetStrTagValue(FT_MEDIA_ALBUM), bSortAscending);
				break;
			case 14:
				iResult = CompareOptLocaleStringNoCaseUndefinedAtBottom(kitem1->GetStrTagValue(FT_MEDIA_TITLE), kitem2->GetStrTagValue(FT_MEDIA_TITLE), bSortAscending);
				break;
			case 15:
				iResult = CompareUnsignedUndefinedAtBottom(kitem1->GetIntTagValue(FT_MEDIA_LENGTH), kitem2->GetIntTagValue(FT_MEDIA_LENGTH), bSortAscending);
				break;
			case 16:
				iResult = CompareUnsignedUndefinedAtBottom(kitem1->GetIntTagValue(FT_MEDIA_BITRATE), kitem2->GetIntTagValue(FT_MEDIA_BITRATE), bSortAscending);
				break;
			case 17:
				iResult = CompareOptLocaleStringNoCaseUndefinedAtBottom(GetCodecDisplayName(kitem1->GetStrTagValue(FT_MEDIA_CODEC)), GetCodecDisplayName(kitem2->GetStrTagValue(FT_MEDIA_CODEC)), bSortAscending);
				break;
			case 18:
				{
					const double ratio1 = kitem1->GetAllTimeRatio();
					const double ratio2 = kitem2->GetAllTimeRatio();
					iResult = (ratio1 < ratio2) ? -1 : static_cast<int>(ratio1 > ratio2);
				}
				break;
			case 19:
				{
					const double ratio1 = kitem1->GetRatio();
					const double ratio2 = kitem2->GetRatio();
					iResult = (ratio1 < ratio2) ? -1 : static_cast<int>(ratio1 > ratio2);
				}
				break;
			case kSharedFilesColumnPermission:
				iResult = ComparePermissionSettings(kitem1, kitem2);
				break;
			case kSharedFilesColumnPowershare:
				iResult = ComparePowerShareSettings(kitem1, kitem2);
				break;
			case kSharedFilesColumnSpreadbarHistory:
				{
					const float fSpread1 = kitem1->statistic.GetSpreadSortValue();
					const float fSpread2 = kitem2->statistic.GetSpreadSortValue();
					iResult = (fSpread1 < fSpread2) ? -1 : static_cast<int>(fSpread1 > fSpread2);
				}
				break;
			case kSharedFilesColumnHideOverShare:
				iResult = CompareHideOverShareSettings(kitem1, kitem2);
				break;
			case kSharedFilesColumnShareOnlyTheNeed:
				iResult = CompareShareOnlyTheNeedSettings(kitem1, kitem2);
				break;
			case kSharedFilesColumnLastRequest:
				iResult = CompareLastRequestTime(kitem1->statistic.GetLastRequestTime(), ShouldShowLastRequestForSharedFile(kitem1), kitem2->statistic.GetLastRequestTime(), ShouldShowLastRequestForSharedFile(kitem2), bSortAscending);
				break;

			case 105: //all requests
				iResult = CompareUnsigned(kitem1->statistic.GetAllTimeRequests(), kitem2->statistic.GetAllTimeRequests());
				break;
			case 106: //all accepted requests
				iResult = CompareUnsigned(kitem1->statistic.GetAllTimeAccepts(), kitem2->statistic.GetAllTimeAccepts());
				break;
			case 107: //all transferred
				iResult = CompareUnsigned(kitem1->statistic.GetAllTimeTransferred(), kitem2->statistic.GetAllTimeTransferred());
				break;
			case 111: //kad shared
				{
					time_t tNow = time(NULL);
					int i1 = static_cast<int>(tNow < kitem1->GetLastPublishTimeKadSrc());
					int i2 = static_cast<int>(tNow < kitem2->GetLastPublishTimeKadSrc());
					iResult = i1 - i2;
				}
			}
		}
	}

	// Call secondary sort order, if the first one resulted as equal
	if (iResult == 0) {
		LPARAM iNextSort = theApp.emuledlg->sharedfileswnd->sharedfilesctrl.GetNextSortOrder(lParamSort);
		if (iNextSort != -1)
			return SortProc(lParam1, lParam2, iNextSort);
	}

	return bSortAscending ? iResult : -iResult;
}

void CSharedFilesCtrl::OpenFile(const CShareableFile *file)
{
	if (file->IsKindOf(RUNTIME_CLASS(CKnownFile)) && static_cast<const CKnownFile*>(file)->m_pCollection) {
		CCollectionViewDialog dialog;
		dialog.SetCollection(static_cast<const CKnownFile*>(file)->m_pCollection);
		dialog.DoModal();
	//} else
	} else if (file->GetFilePath().GetLength()>0)
		ShellDefaultVerb(file->GetFilePath());
}

void CSharedFilesCtrl::OnNmDblClk(LPNMHDR, LRESULT* pResult)
{
	int iSel = GetNextItem(-1, LVIS_SELECTED | LVIS_FOCUSED);
	if (iSel >= 0 && static_cast<size_t>(iSel) < m_ListedItemsVector.size()) {
		CKnownFile* file = m_ListedItemsVector[iSel];
		if (file != NULL) {
			if (GetKeyState(VK_MENU) & 0x8000) {
				CTypedPtrList<CPtrList, CShareableFile*> aFiles;
				aFiles.AddHead(file);
				ShowFileDialog(aFiles);
			} else if (!file->IsPartFile())
				OpenFile(file);
		}
	}
	*pResult = 0;
}

void CSharedFilesCtrl::CreateMenus()
{
	// Destroy child submenus before their owner to avoid invalid handle asserts.
	if (m_SelectiveChunkMenu)
		VERIFY2(m_SelectiveChunkMenu.DestroyMenu());
	if (m_HideOSMenu)
		VERIFY2(m_HideOSMenu.DestroyMenu());
	if (m_ShareOnlyTheNeedMenu)
		VERIFY2(m_ShareOnlyTheNeedMenu.DestroyMenu());
	if (m_SpreadbarMenu)
		VERIFY2(m_SpreadbarMenu.DestroyMenu());
	if (m_PowerShareLimitMenu)
		VERIFY2(m_PowerShareLimitMenu.DestroyMenu());
	if (m_PowershareMenu)
		VERIFY2(m_PowershareMenu.DestroyMenu());
	if (m_PermMenu)
		VERIFY2(m_PermMenu.DestroyMenu());
	if (m_PreviewMenu)
		VERIFY2(m_PreviewMenu.DestroyMenu());
	if (m_PrioMenu)
		VERIFY2(m_PrioMenu.DestroyMenu());
	if (m_CollectionsMenu)
		VERIFY2(m_CollectionsMenu.DestroyMenu());
	if (m_FileHistorysMenu)
		VERIFY2(m_FileHistorysMenu.DestroyMenu());
	if (m_SharedFilesMenu)
		VERIFY2(m_SharedFilesMenu.DestroyMenu());

	m_FileHistorysMenu.CreateMenu();
	m_FileHistorysMenu.AppendMenu(MF_STRING, MP_CLEARHISTORY, GetResString(_T("FILE_HISTORY_PURGE")), _T("CLEARCOMPLETE"));

	m_PrioMenu.CreateMenu();
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOVERYLOW, GetResString(_T("PRIOVERYLOW")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOLOW, GetResString(_T("PRIOLOW")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIONORMAL, GetResString(_T("PRIONORMAL")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOHIGH, GetResString(_T("PRIOHIGH")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOVERYHIGH, GetResString(_T("PRIORELEASE")));
	m_PrioMenu.AppendMenu(MF_STRING, MP_PRIOAUTO, GetResString(_T("PRIOAUTO")));//UAP

	m_PermMenu.CreateMenu();
	m_PermMenu.AppendMenu(MF_STRING, MP_PERMDEFAULT, GetResString(_T("DEFAULT")));
	m_PermMenu.AppendMenu(MF_STRING, MP_PERMNONE, GetResString(_T("SHARE_PERMISSION_HIDDEN")));
	m_PermMenu.AppendMenu(MF_STRING, MP_PERMFRIENDS, GetResString(_T("FSTATUS_FRIENDSONLY")));
	m_PermMenu.AppendMenu(MF_STRING, MP_PERMALL, GetResString(_T("PW_EVER")));

	m_PowershareMenu.CreateMenu();
	m_PowershareMenu.AppendMenu(MF_STRING, MP_POWERSHARE_DEFAULT, GetResString(_T("DEFAULT")));
	m_PowershareMenu.AppendMenu(MF_STRING, MP_POWERSHARE_OFF, GetResString(_T("DISABLED")));
	m_PowershareMenu.AppendMenu(MF_STRING, MP_POWERSHARE_ON, GetResString(_T("POWERSHARE_ACTIVATED")));
	m_PowershareMenu.AppendMenu(MF_STRING, MP_POWERSHARE_AUTO, GetResString(_T("PRIOAUTO")));
	m_PowershareMenu.AppendMenu(MF_STRING, MP_POWERSHARE_LIMITED, GetResString(_T("POWERSHARE_LIMITED")));

	m_PowerShareLimitMenu.CreateMenu();
	m_PowerShareLimitMenu.AppendMenu(MF_STRING, MP_POWERSHARE_LIMIT, GetResString(_T("DEFAULT")));
	m_PowerShareLimitMenu.AppendMenu(MF_STRING, MP_POWERSHARE_LIMIT_SET, GetResString(_T("DISABLED")));
	m_PowershareMenu.AppendMenu(MF_SEPARATOR);
	m_PowershareMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_PowerShareLimitMenu.m_hMenu, GetResString(_T("POWERSHARE_LIMIT")));

	m_SpreadbarMenu.CreateMenu();
	m_SpreadbarMenu.AppendMenu(MF_STRING, MP_SPREADBAR_DEFAULT, GetResString(_T("DEFAULT")));
	m_SpreadbarMenu.AppendMenu(MF_STRING, MP_SPREADBAR_OFF, GetResString(_T("DISABLED")));
	m_SpreadbarMenu.AppendMenu(MF_STRING, MP_SPREADBAR_ON, GetResString(_T("ENABLED")));
	m_SpreadbarMenu.AppendMenu(MF_SEPARATOR);
	m_SpreadbarMenu.AppendMenu(MF_STRING, MP_SPREADBAR_RESET, GetResString(_T("PW_RESET")));

	m_HideOSMenu.CreateMenu();
	m_HideOSMenu.AppendMenu(MF_STRING, MP_HIDEOS_DEFAULT, GetResString(_T("DEFAULT")));
	m_HideOSMenu.AppendMenu(MF_STRING, MP_HIDEOS_SET, GetResString(_T("DISABLED")));

	m_SelectiveChunkMenu.CreateMenu();
	m_SelectiveChunkMenu.AppendMenu(MF_STRING, MP_SELECTIVE_CHUNK, GetResString(_T("DEFAULT")));
	m_SelectiveChunkMenu.AppendMenu(MF_STRING, MP_SELECTIVE_CHUNK_0, GetResString(_T("DISABLED")));
	m_SelectiveChunkMenu.AppendMenu(MF_STRING, MP_SELECTIVE_CHUNK_1, GetResString(_T("ENABLED")));
	m_HideOSMenu.AppendMenu(MF_SEPARATOR);
	m_HideOSMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_SelectiveChunkMenu.m_hMenu, GetResString(_T("SELECTIVESHARE")));

	m_ShareOnlyTheNeedMenu.CreateMenu();
	m_ShareOnlyTheNeedMenu.AppendMenu(MF_STRING, MP_SHAREONLYTHENEED, GetResString(_T("DEFAULT")));
	m_ShareOnlyTheNeedMenu.AppendMenu(MF_STRING, MP_SHAREONLYTHENEED_0, GetResString(_T("DISABLED")));
	m_ShareOnlyTheNeedMenu.AppendMenu(MF_STRING, MP_SHAREONLYTHENEED_1, GetResString(_T("ENABLED")));

	m_CollectionsMenu.CreateMenu();
	m_CollectionsMenu.AppendMenu(MF_STRING, MP_CREATECOLLECTION, GetResString(_T("CREATECOLLECTION")), _T("COLLECTION_ADD"));
	m_CollectionsMenu.AppendMenu(MF_STRING, MP_MODIFYCOLLECTION, GetResString(_T("MODIFYCOLLECTION")), _T("COLLECTION_EDIT"));
	m_CollectionsMenu.AppendMenu(MF_STRING, MP_VIEWCOLLECTION, GetResString(_T("VIEWCOLLECTION")), _T("COLLECTION_VIEW"));
	m_CollectionsMenu.AppendMenu(MF_STRING, MP_SEARCHAUTHOR, GetResString(_T("SEARCHAUTHORCOLLECTION")), _T("COLLECTION_SEARCH"));

	m_SharedFilesMenu.CreatePopupMenu();
	m_SharedFilesMenu.AddMenuSidebar(GetResString(_T("SF_FILES")));

	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_OPEN, GetResString(_T("DL_OPEN")), _T("DL_OPEN"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_OPENFOLDER, GetResString(_T("OPENFOLDER")), _T("OPENFOLDER"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_RENAME, GetResString(_T("RENAME")) + _T("..."), _T("FILERENAME"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_UPDATE_METADATA, GetResString(_T("UPDATE_METADATA")), _T("METADATA"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_REMOVE, GetResString(_T("DELETE")), _T("DELETE"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_REMOVEFROMHISTORY, GetResString(_T("FILE_HISTORY_REMOVE")), _T("DELETESELECTED"));

	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_UNSHAREFILE, GetResString(_T("UNSHARE")), _T("KADBOOTSTRAP")); // TODO: better icon
	if (thePrefs.IsExtControlsEnabled())
		m_SharedFilesMenu.AppendMenu(MF_STRING, Irc_SetSendLink, GetResString(_T("IRC_ADDLINKTOIRC")), _T("IRCCLIPBOARD"));

	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_CANCEL, GetResString(_T("CANCEL_DOWNLOAD")), _T("DELETE"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_CANCEL_FORGET, GetResString(_T("CANCEL_FORGET_DOWNLOAD")), _T("DELETE_FORGET"));
	m_PreviewMenu.CreateMenu();
	RebuildPreviewMenu(m_PreviewMenu, NULL, false, false, false, false, false);
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_PreviewMenu.m_hMenu, GetResString(_T("PREVIEWWITH")), _T("PREVIEW"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_PermMenu.m_hMenu, GetResString(_T("SHARE_PERMISSION_GROUP")), _T("FRIEND"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_PowershareMenu.m_hMenu, GetResString(_T("POWERSHARE")), _T("FILEPRIORITY"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_SpreadbarMenu.m_hMenu, GetResString(_T("SPREADBAR")), _T("SHAREDFILESLIST"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_HideOSMenu.m_hMenu, GetResString(_T("HIDE_OVER_SHARE_MENU")), _T("FILE"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_ShareOnlyTheNeedMenu.m_hMenu, GetResString(_T("SHAREONLYTHENEED")), _T("FILE"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_VIEWPARTFILES, GetResString(_T("FILE_HISTORY_SHOW_PART2")));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_VIEWSHAREDFILES, GetResString(_T("FILE_HISTORY_SHOW_SHARED2")));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_VIEWDUPLICATEFILES, GetResString(_T("FILE_HISTORY_SHOW_DUPLICATE2")));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_FileHistorysMenu.m_hMenu, GetResString(_T("FILE_HISTORY")), _T("DOWNLOAD"));

	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	CString sPrio(GetResString(_T("PRIORITY")));
	sPrio.AppendFormat(_T(" (%s)"), (LPCTSTR)GetResString(_T("PW_CON_UPLBL")));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_PrioMenu.m_hMenu, sPrio, _T("FILEPRIORITY"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);

	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_CollectionsMenu.m_hMenu, GetResString(_T("META_COLLECTION")), _T("AABCollectionFileType"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);

	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_DETAIL, GetResString(_T("DL_INFO")), _T("FILEINFO"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_CMT, GetResStringWithAccelAndEllipsis(_T("COMMENT"), _T('e')), _T("FILECOMMENTS"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_SHOWED2KLINK, GetResStringWithEllipsis(_T("SW_LINK")), _T("ED2KLINK"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_CUT, GetResString(_T("COPY_FILE_NAMES")), _T("FILERENAME"));
	if (thePrefs.GetShowCopyEd2kLinkCmd())
		m_SharedFilesMenu.AppendMenu(MF_STRING, MP_GETED2KLINK, GetResString(_T("DL_LINK1")), _T("ED2KLINK"));
	m_SharedFilesMenu.AppendMenu(MF_STRING, MP_FIND, GetResString(_T("FIND")), _T("Search"));
	m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);

#if defined(_DEBUG)
	if (thePrefs.IsExtControlsEnabled()) {
		//JOHNTODO: Not for release as we need kad lowID users in the network to see how well this works. Also, we do not support these links yet.
		m_SharedFilesMenu.AppendMenu(MF_STRING, MP_GETKADSOURCELINK, _T("Copy eD2K Links To Clipboard (Kad)"));
		m_SharedFilesMenu.AppendMenu(MF_STRING | MF_SEPARATOR);
	}
#endif
}

void CSharedFilesCtrl::ShowComments(CShareableFile *file)
{
	if (file) {
		CTypedPtrList<CPtrList, CShareableFile*> aFiles;
		aFiles.AddHead(file);
		ShowFileDialog(aFiles, IDD_COMMENT);
	}
}

void CSharedFilesCtrl::OnLvnGetDispInfo(LPNMHDR pNMHDR, LRESULT *pResult)
{
	if (!theApp.IsClosing()) {
		// Although we have an owner drawn listview control we store the text for the primary item in the
		// listview, to be capable of quick searching those items via the keyboard. Because our listview
		// items may change their contents, we do this via a text callback function. The listview control
		// will send us the LVN_DISPINFO notification if it needs to know the contents of the primary item.
		//
		// But, the listview control sends this notification all the time, even if we do not search for an item.
		// At least this notification is only sent for the visible items and not for all items in the list.
		// Though, because this function is invoked *very* often, do *NOT* put any time consuming code in here.
		//
		// Vista: That callback is used to get the strings for the label tips for the sub(!)-items.
		//
		// This isn't an owner drawn list anymore, instead this is implemented as a virtual list. So above description is now obsolete!
		LVITEMW& rItem = reinterpret_cast<NMLVDISPINFO*>(pNMHDR)->item;
		if (rItem.mask & LVIF_TEXT) {
			if (rItem.pszText != NULL && rItem.cchTextMax > 0) {
				rItem.pszText[0] = _T('\0');
				if (rItem.iItem >= 0 && static_cast<size_t>(rItem.iItem) < m_ListedItemsVector.size()) {
						CShareableFile *cur_file = static_cast<CShareableFile*>(m_ListedItemsVector[static_cast<size_t>(rItem.iItem)]);
						if (cur_file != NULL)
							_tcsncpy_s(rItem.pszText, rItem.cchTextMax, GetItemDisplayText(cur_file, rItem.iSubItem), _TRUNCATE);
				}
			}
		}
	}
	*pResult = 0;
}


void CSharedFilesCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar == VK_DELETE && !m_bExecutingSharedFilesCommand && !CanDeleteSelectedSharedFilesFromDisk())
		return;

	if (nChar == VK_SPACE && CheckBoxesEnabled()) {
		if (!m_bExecutingSharedFilesCommand) {
			QueueSharedFilesCommandFromCurrentSelection(kSharedFilesCommandToggleShareStatus);
			return;
		}
		// Toggle Checkboxes
		// selection and item position might change during processing (shouldn't though, but lets make sure), so first get all pointers instead using the selection pos directly
		SetRedraw(false);
		CTypedPtrList<CPtrList, CShareableFile*> selectedList;
			for (POSITION pos = GetFirstSelectedItemPosition(); pos != NULL;) {
				int index = GetNextSelectedItem(pos);
				if (index >= 0 && static_cast<size_t>(index) < m_ListedItemsVector.size()) {
					CKnownFile* cur_file = m_ListedItemsVector[index];
					if (cur_file != NULL)
						selectedList.AddTail(static_cast<CShareableFile*>(cur_file));
				}
			}
		while (!selectedList.IsEmpty()) {
			const int index = FindListedIndexByPointer(static_cast<CKnownFile*>(selectedList.RemoveHead()));
			if (index >= 0)
				CheckBoxClicked(index);
		}
		SetRedraw(true);
		return;
	}

	CMuleListCtrl::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CSharedFilesCtrl::ShowFileDialog(CTypedPtrList<CPtrList, CShareableFile*> &aFiles, UINT uInvokePage)
{
	if (!aFiles.IsEmpty()) {
		CSharedFileDetailsSheet dialog(aFiles, uInvokePage, this);
		dialog.DoModal();
	}
}

void CSharedFilesCtrl::SetDirectoryFilter(CDirectoryItem *pNewFilter, bool bRefresh)
{
	if (m_pDirectoryFilter != pNewFilter) {
		m_pDirectoryFilter = pNewFilter;
		if (bRefresh)
			ReloadList(false, kSharedFilesViewState);
	}
}

bool CSharedFilesCtrl::GetPersistentInfoTipText(const SPersistentInfoTipContext& context, CString& strText)
{
	if (context.iItem < 0 || static_cast<size_t>(context.iItem) >= m_ListedItemsVector.size())
		return false;

	CKnownFile* pFile = m_ListedItemsVector[static_cast<size_t>(context.iItem)];
	if (pFile == NULL)
		return false;

	int iMapped = -1;
	if (!m_ListedItemsMap.Lookup(pFile, iMapped) || iMapped != context.iItem)
		return false;
	if (context.dwItemKey != 0 && context.dwItemKey != reinterpret_cast<DWORD_PTR>(pFile))
		return false;

	strText = pFile->GetInfoSummary() + TOOLTIP_AUTOFORMAT_SUFFIX_CH;
	return true;
}

void CSharedFilesCtrl::OnLvnGetInfoTip(LPNMHDR pNMHDR, LRESULT *pResult)
{
	CMuleListCtrl::OnLvnGetInfoTip(pNMHDR, pResult);
}

const bool CSharedFilesCtrl::IsFilteredOut(const CShareableFile *pFile) const
{
	if (!pFile)
		return true;
	if (pFile->IsKindOf(RUNTIME_CLASS(CKnownFile)) && IsHiddenBySharedFilesVisibleRemove(static_cast<const CKnownFile*>(pFile)))
		return true;

	// check filter conditions if we should show this file right now
	if (m_pDirectoryFilter != NULL) {
		ASSERT(pFile->IsKindOf(RUNTIME_CLASS(CKnownFile)) || m_pDirectoryFilter->m_eItemType == SDI_UNSHAREDDIRECTORY);
		switch (m_pDirectoryFilter->m_eItemType) {
		case SDI_ALL: // No filter
		case SDI_ALLHISTORY: // No filter
			break;
		case SDI_DUP: // No filter
			break;
		case SDI_FILESYSTEMPARENT:
			return true;
		case SDI_UNSHAREDDIRECTORY: // Items from the whole file system tree
			if (pFile->IsPartFile())
				return true;
		case SDI_NO:
			// some shared directory
		case SDI_CATINCOMING: // Categories with special incoming dirs
			if (!EqualPaths(pFile->GetSharedDirectory(), m_pDirectoryFilter->m_strFullPath))
				return true;
			break;
		case SDI_TEMP: // only temp files
			if (!pFile->IsPartFile())
				return true;
			if (m_pDirectoryFilter->m_nCatFilter != -1 && (UINT)m_pDirectoryFilter->m_nCatFilter != ((CPartFile*)pFile)->GetCategory())
				return true;
			break;
		case SDI_DIRECTORY: // any user selected shared dir but not incoming or temp
			if (pFile->IsPartFile())
				return true;
			if (EqualPaths(pFile->GetSharedDirectory(), thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR)))
				return true;
			break;
		case SDI_INCOMING: // Main incoming directory
		{
			CString sIncoming(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
			if (!EqualPaths(pFile->GetPath(), sIncoming)) {
				if (thePrefs.GetAutoShareSubdirs() && IsSubDirectoryOf(pFile->GetPath(), sIncoming))
					break;
				return true;
			}
			break;
		}
	case SDI_ED2KFILETYPE:
	{
			// Special handling for GUI-only "Other" filter
			if (m_pDirectoryFilter->m_nCatFilter == ED2KFT_OTHER) {
				EED2KFileType t = GetED2KFileTypeID(pFile->GetFileName());
				// Accept only files which are not one of the known classes
				if (t == ED2KFT_AUDIO || t == ED2KFT_VIDEO || t == ED2KFT_IMAGE || t == ED2KFT_PROGRAM || t == ED2KFT_DOCUMENT || t == ED2KFT_ARCHIVE || t == ED2KFT_CDIMAGE || t == ED2KFT_EMULECOLLECTION)
					return true; // Filter out known types, keep the rest

				break;
			}

			if (m_pDirectoryFilter->m_nCatFilter == -1 || m_pDirectoryFilter->m_nCatFilter != GetED2KFileTypeID(pFile->GetFileName()))
				return true;

			break;
		}
		}
	}

	const CStringArray &rastrFilter = theApp.emuledlg->sharedfileswnd->m_astrFilter;
	if (!rastrFilter.IsEmpty()) {
		// filtering is done by text only for all columns to keep it consistent and simple for the user
		// even if that doesn't allow complex filters
		const CString &szFilterTarget(GetItemDisplayText(pFile, theApp.emuledlg->sharedfileswnd->GetFilterColumn()));

		for (INT_PTR i = rastrFilter.GetCount(); --i >= 0;) {
			LPCTSTR pszText = (LPCTSTR)rastrFilter[i];
			bool bAnd = (*pszText != _T('-'));
			if (!bAnd)
				++pszText;

			bool bFound = (stristr(szFilterTarget, pszText) != NULL);
			if (bAnd != bFound)
				return true;
		}
	}
	return false;
}

void CSharedFilesCtrl::SetToolTipsDelay(DWORD dwDelay)
{
	CToolTipCtrl *tooltip = GetToolTips();
	if (tooltip)
		tooltip->SetDelayTime(TTDT_INITIAL, dwDelay);
}

const bool CSharedFilesCtrl::IsSharedInKad(const CKnownFile *file) const
{
	if (!Kademlia::CKademlia::IsConnected() || time(NULL) >= file->GetLastPublishTimeKadSrc())
		return false;
	if (!Kademlia::CKademlia::IsFirewalled())
		return true;
	return (theApp.clientlist->GetServingBuddy() && (file->GetLastPublishServingBuddy() == theApp.clientlist->GetServingBuddy()->GetIP().ToUInt32(false)))
		|| (Kademlia::CKademlia::IsRunning() && !Kademlia::CUDPFirewallTester::IsFirewalledUDP(true) && Kademlia::CUDPFirewallTester::IsVerified());
}

BOOL CSharedFilesCtrl::OnNMClick(LPNMHDR pNMHDR, LRESULT *pResult)
{
	NMLISTVIEW *pNMListView = reinterpret_cast<NM_LISTVIEW*>(pNMHDR);
	int iItem = HitTest(pNMListView->ptAction);
	if (iItem >= 0) {
		if (CheckBoxesEnabled()) { // do we have checkboxes?
			// determine if the checkbox was clicked
			CRect rcItem;
			if (GetItemRect(iItem, rcItem, LVIR_BOUNDS)) {
				CPoint pointHit = pNMListView->ptAction;
				ASSERT(rcItem.PtInRect(pointHit));
				rcItem.left += sm_iIconOffset;
				rcItem.right = rcItem.left + 16;
				rcItem.top += (rcItem.Height() > 16) ? ((rcItem.Height() - 15) / 2) : 0;
					rcItem.bottom = rcItem.top + 16;
					if (rcItem.PtInRect(pointHit)) {
						// user clicked on the checkbox
						CheckBoxClicked(iItem);
					return (BOOL)(*pResult = 0); // Since this is a checkbox click, do not proceed selection checks, return now and pass on to the parent window
				}
			}
		}
	}

	return (BOOL)(*pResult = 0); // pass on to the parent window
}

void CSharedFilesCtrl::CheckBoxClicked(const int iItem)
{
	if (iItem == -1) {
		ASSERT(0);
		return;
		}
		// check which state the checkbox (should) currently have
		if (iItem < 0 || static_cast<size_t>(iItem) >= m_ListedItemsVector.size())
			return;
		CKnownFile* cur_file = m_ListedItemsVector[iItem];
		if (cur_file == NULL)
			return;
	const CShareableFile* pFile = static_cast<CShareableFile*>(cur_file);

	if (pFile->IsShellLinked())
		return; // no interacting with shell-linked files
	if (theApp.sharedfiles->ShouldBeShared(pFile->GetPath(), pFile->GetFilePath(), false)) {
		// this is currently shared so unshare it
		if (theApp.sharedfiles->ShouldBeShared(pFile->GetPath(), pFile->GetFilePath(), true))
			return; // not allowed to unshare this file
		VERIFY(theApp.sharedfiles->ExcludeFile(pFile->GetFilePath()));
		UpdateFile(cur_file);
		// update GUI stuff
		ShowFilesCount();
		theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails();
		theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged();
		// no need to update the list itself, will be handled in the RemoveFile function
	} else {
		if (!thePrefs.IsShareableDirectory(pFile->GetPath()))
			return; // not allowed to share
		VERIFY(theApp.sharedfiles->AddSingleSharedFile(pFile->GetFilePath()));
		ShowFilesCount();
		theApp.emuledlg->sharedfileswnd->ShowSelectedFilesDetails();
		theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged();
		UpdateFile(cur_file);
	}
}

bool CSharedFilesCtrl::CheckBoxesEnabled() const
{
	return (m_eFilter == FilterType::FileSystem);
}

void CSharedFilesCtrl::OnMouseMove(UINT nFlags, CPoint point)
{
	// highlighting Checkboxes
	if (CheckBoxesEnabled()) {
		// are we currently on any checkbox?
		int iItem = HitTest(point);
		if (iItem >= 0) {
			CRect rcItem;
			if (GetItemRect(iItem, rcItem, LVIR_BOUNDS)) {
				rcItem.left += sm_iIconOffset;
				rcItem.right = rcItem.left + 16;
				rcItem.top += (rcItem.Height() > 16) ? ((rcItem.Height() - 15) / 2) : 0;
				rcItem.bottom = rcItem.top + 16;
				if (rcItem.PtInRect(point)) {
					// is this checkbox already hot?
						CKnownFile* pHitKnownFile = static_cast<size_t>(iItem) < m_ListedItemsVector.size() ? m_ListedItemsVector[iItem] : NULL;
						CShareableFile* pHitItem = pHitKnownFile != NULL ? static_cast<CShareableFile*>(pHitKnownFile) : NULL;
						if (m_pHighlightedItem != pHitItem) {
							// update old highlighted item
							CShareableFile* pOldItem = m_pHighlightedItem;
							m_pHighlightedItem = pHitItem;
							if (pOldItem != NULL)
								UpdateFile(static_cast<CKnownFile*>(pOldItem), false);
							// highlight current item
							InvalidateRect(rcItem);
						}
					CMuleListCtrl::OnMouseMove(nFlags, point);
					return;
				}
			}
		}
		// no checkbox should be hot
		if (m_pHighlightedItem != NULL) {
			CShareableFile* pOldItem = m_pHighlightedItem;
			m_pHighlightedItem = NULL;
			if (pOldItem != NULL)
				UpdateFile(static_cast<CKnownFile*>(pOldItem), false);
		}
	}
	CMuleListCtrl::OnMouseMove(nFlags, point);
}


CSharedFilesCtrl::CShareDropTarget::CShareDropTarget()
{
	m_piDropHelper = NULL;
	m_pParent = NULL;
	m_bUseDnDHelper = SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, NULL, CLSCTX_INPROC_SERVER, IID_IDropTargetHelper, (void**)&m_piDropHelper));
}

CSharedFilesCtrl::CShareDropTarget::~CShareDropTarget()
{
	if (m_piDropHelper != NULL)
		m_piDropHelper->Release();
}

DROPEFFECT CSharedFilesCtrl::CShareDropTarget::OnDragEnter(CWnd *pWnd, COleDataObject *pDataObject, DWORD /*dwKeyState*/, CPoint point)
{
	DROPEFFECT dwEffect = pDataObject->IsDataAvailable(CF_HDROP) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
	if (m_bUseDnDHelper) {
		IDataObject *piDataObj = pDataObject->GetIDataObject(FALSE);
		m_piDropHelper->DragEnter(pWnd->GetSafeHwnd(), piDataObj, &point, dwEffect);
	}
	return dwEffect;
}

DROPEFFECT CSharedFilesCtrl::CShareDropTarget::OnDragOver(CWnd*, COleDataObject *pDataObject, DWORD, CPoint point)
{
	DROPEFFECT dwEffect = pDataObject->IsDataAvailable(CF_HDROP) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
	if (m_bUseDnDHelper)
		m_piDropHelper->DragOver(&point, dwEffect);
	return dwEffect;
}

BOOL CSharedFilesCtrl::CShareDropTarget::OnDrop(CWnd*, COleDataObject *pDataObject, DROPEFFECT dropEffect, CPoint point)
{
	HGLOBAL hGlobal = pDataObject->GetGlobalData(CF_HDROP);
	if (hGlobal != NULL) {
		HDROP hDrop = (HDROP)::GlobalLock(hGlobal);
		if (hDrop != NULL) {
			CString strFilePath;
			CFileFind ff;
			CStringList liToAddFiles; // all files to add
			CStringList liToAddDirs; // all directories to add
			bool bFromSingleDirectory = true;	// all files are in the same directory,
			CString strSingleDirectory;			// which would be this one

			UINT nFileCount = DragQueryFile(hDrop, UINT_MAX, NULL, 0);
			for (UINT nFile = 0; nFile < nFileCount; ++nFile) {
				if (DragQueryFile(hDrop, nFile, strFilePath.GetBuffer(MAX_PATH), MAX_PATH) > 0) {
					strFilePath.ReleaseBuffer();
					if (ff.FindFile(strFilePath)) {
						ff.FindNextFile();
						CString ffpath(ff.GetFilePath());
						if (ff.IsDirectory())
							slosh(ffpath);
						// just a quick pre-check, complete check is done later in the share function itself
						if (ff.IsDots() || ff.IsSystem() || ff.IsTemporary()
							|| (!ff.IsDirectory() && (ff.GetLength() == 0 || ff.GetLength() > MAX_EMULE_FILE_SIZE
								|| theApp.sharedfiles->ShouldBeShared(ffpath.Left(ffpath.ReverseFind(_T('\\'))), ffpath, false)))
							|| (ff.IsDirectory() && (!thePrefs.IsShareableDirectory(ffpath)
								|| theApp.sharedfiles->ShouldBeShared(ffpath, NULL, false))))
						{
							DebugLog(_T("Drag&Drop'ed shared File ignored (%s)"), (LPCTSTR)EscPercent(ffpath));
						} else if (ff.IsDirectory()) {
							DEBUG_ONLY(DebugLog(_T("Drag&Drop'ed directory: %s"), (LPCTSTR)EscPercent(ffpath)));
							liToAddDirs.AddTail(ffpath);
						} else {
							DEBUG_ONLY(DebugLog(_T("Drag&Drop'ed file: %s"), (LPCTSTR)EscPercent(ffpath)));
							liToAddFiles.AddTail(ffpath);
							if (bFromSingleDirectory) {
								if (strSingleDirectory.IsEmpty())
									strSingleDirectory = ffpath.Left(ffpath.ReverseFind(_T('\\')) + 1);
								else if (strSingleDirectory.CompareNoCase(ffpath.Left(ffpath.ReverseFind(_T('\\')) + 1)) != NULL)
									bFromSingleDirectory = false;
							}
						}
					} else
						DebugLogError(_T("Drag&Drop'ed shared File not found (%s)"), (LPCTSTR)EscPercent(strFilePath));

					ff.Close();
				} else {
					ASSERT(0);
					strFilePath.ReleaseBuffer();
				}
			}

			if (!liToAddFiles.IsEmpty() || !liToAddDirs.IsEmpty()) {
				// add the directories first as this would invalidate addition of
				// single files, contained in one of those dirs
				for (POSITION pos = liToAddDirs.GetHeadPosition(); pos != NULL;)
					VERIFY(theApp.sharedfiles->AddSingleSharedDirectory(liToAddDirs.GetNext(pos))); // should always succeed

				bool bHaveFiles = false;
				while (!liToAddFiles.IsEmpty())
					bHaveFiles |= theApp.sharedfiles->AddSingleSharedFile(liToAddFiles.RemoveHead()); // could fail, due to the dirs added above

				// GUI updates
				if (!liToAddDirs.IsEmpty())
					theApp.emuledlg->sharedfileswnd->m_ctlSharedDirTree.Reload(true);
				if (bHaveFiles)
					theApp.emuledlg->sharedfileswnd->OnSingleFileShareStatusChanged();
				m_pParent->ShowFilesCount();
	
				if (bHaveFiles && liToAddDirs.IsEmpty() && bFromSingleDirectory) {
					// if we added only files from the same directory, show and select this in the file system tree
					ASSERT(!strSingleDirectory.IsEmpty());
					VERIFY(theApp.emuledlg->sharedfileswnd->m_ctlSharedDirTree.ShowFileSystemDirectory(strSingleDirectory));
				} else if (!liToAddDirs.IsEmpty() && !bHaveFiles) {
					// only directories added, if only one select the specific shared dir, otherwise the Shared Directories section
					const CString &sShow(liToAddDirs.GetCount() == 1 ? liToAddDirs.GetHead() : EMPTY);
					theApp.emuledlg->sharedfileswnd->m_ctlSharedDirTree.ShowSharedDirectory(sShow);
				} else {
					// otherwise select the All Shared Files category
					theApp.emuledlg->sharedfileswnd->m_ctlSharedDirTree.ShowAllSharedFiles();
				}
			}
			::GlobalUnlock(hGlobal);
		}
		::GlobalFree(hGlobal);
	}

	if (m_bUseDnDHelper) {
		IDataObject *piDataObj = pDataObject->GetIDataObject(FALSE);
		m_piDropHelper->Drop(piDataObj, &point, dropEffect);
	}

	return TRUE;
}

void CSharedFilesCtrl::CShareDropTarget::OnDragLeave(CWnd*)
{
	if (m_bUseDnDHelper)
		m_piDropHelper->DragLeave();
}
