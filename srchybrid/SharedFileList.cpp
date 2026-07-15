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
#include <io.h>
#include <sys/stat.h>
#include <unordered_set>
#include "emule.h"
#include "KnownFileList.h"
#include "SharedFileList.h"
#include "Packets.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "kademlia/kademlia/search.h"
#include "kademlia/kademlia/SearchManager.h"
#include "kademlia/kademlia/prefs.h"
#include "kademlia/kademlia/Tag.h"
#include "DownloadQueue.h"
#include "UploadQueue.h"
#include "Statistics.h"
#include "Preferences.h"
#include "UpDownClient.h"
#include "KnownFile.h"
#include "ServerConnect.h"
#include "SafeFile.h"
#include "Server.h"
#include "PartFile.h"
#include "emuledlg.h"
#include "SharedFilesWnd.h"
#include "StringConversion.h"
#include "ClientList.h"
#include "SearchList.h"
#include "Log.h"
#include "Collection.h"
#include "kademlia/kademlia/UDPFirewallTester.h"
#include "md5sum.h"
#include "UserMsgs.h"
#include "MuleStatusBarCtrl.h"
#include "OtherFunctions.h"
#include "PartFileWriteThread.h"

struct SharedFileMetaDataTask_Struct
{
	SharedFileMetaDataTask_Struct()
		: uFileSize()
		, tUtcFileDate()
		, ftSourceLastWrite()
		, bProbeCompleted(false)
		, bSourceStable(false)
		, bManualUpdate(false)
		, bExtractMetaData(true)
		, pMetaData(NULL)
	{
		ZeroMemory(aucFileHash, sizeof aucFileHash);
	}

	~SharedFileMetaDataTask_Struct()
	{
		delete pMetaData;
	}

	CString strFileName;
	CString strDirectory;
	CString strFilePath;
	CString strQueueKey;
	uchar aucFileHash[MDX_DIGEST_SIZE];
	uint64 uFileSize;
	time_t tUtcFileDate;
	FILETIME ftSourceLastWrite;
	bool bProbeCompleted;
	bool bSourceStable;
	bool bManualUpdate;
	bool bExtractMetaData;
	SKnownFileMetaData* pMetaData;
};

struct SharedFileMetaDataThreadContext
{
	SharedFileMetaDataThreadContext()
		: lReferenceCount(1)
		, lThreadActive()
		, lStopping()
		, lDiscardedTasks()
		, lDiscardedManualTasks()
		, lReconciliationRequired()
		, lReconciliationActive()
	{
		latestTasks.InitHashTable(16381);
	}

	~SharedFileMetaDataThreadContext()
	{
		while (!pendingTasks.IsEmpty())
			delete pendingTasks.RemoveHead();
		while (!completedTasks.IsEmpty())
			delete completedTasks.RemoveHead();
		latestTasks.RemoveAll();
	}

	volatile LONG lReferenceCount;
	volatile LONG lThreadActive;
	volatile LONG lStopping;
	volatile LONG lDiscardedTasks;
	volatile LONG lDiscardedManualTasks;
	volatile LONG lReconciliationRequired;
	volatile LONG lReconciliationActive;
	CCriticalSection queueLock;
	CTypedPtrList<CPtrList, SharedFileMetaDataTask_Struct*> pendingTasks;
	CTypedPtrList<CPtrList, SharedFileMetaDataTask_Struct*> completedTasks;
	CMapStringToPtr latestTasks;
};

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

typedef CSimpleArray<CKnownFile*> CSimpleKnownFileArray;
#define	SHAREDFILES_FILE	_T("sharedfiles.dat")

enum { LONGPATH_WILDCARD_SLACK = 12 }; // Named slack for wildcard-based directory searches (accounts for "\*", extra separators, etc.). 

namespace
{
	const TCHAR kExcludedSharedFilePrefix = _T('-');
	const TCHAR kExcludedSharedDirectoryPrefix = _T('!');
	const UINT kSharedFilesCompletionKeywordPurgePerSlice = 512;
	const UINT kSharedFilesCompletionSnapshotClearPerSlice = 2048;
	const UINT kSharedFilesCompletionReloadPrunePerSlice = 256;
	const UINT kSharedFilesCompletionUploadWaiterPrunePerSlice = 128;
	const UINT kSharedFilesCompletionSharedCachePerSlice = 512;
	const UINT kSharedFilesFoundMaxFilesPerSlice = 12;
	const DWORD kSharedFilesSearchThreadShutdownWaitMs = 15000;
	const DWORD kSharedFilesSearchThreadShutdownSliceMs = 250;
	const INT_PTR kSharedFileMetaDataQueueLimit = 256;
	const UINT kSharedFileMetaDataReconciliationPerSlice = 12;
	typedef BOOL (WINAPI *PCancelSynchronousIo)(HANDLE);

	void AddRefMetaDataThreadContext(SharedFileMetaDataThreadContext* pContext)
	{
		ASSERT(pContext != NULL);
		::InterlockedIncrement(&pContext->lReferenceCount);
	}

	void ReleaseMetaDataThreadContext(SharedFileMetaDataThreadContext* pContext)
	{
		if (pContext != NULL && ::InterlockedDecrement(&pContext->lReferenceCount) == 0)
			delete pContext;
	}

	bool IsMetaDataUpdateTargetCurrent(const CKnownFile* pFile, const SharedFileMetaDataTask_Struct* pTask)
	{
		return pFile != NULL
			&& pTask != NULL
			&& !pFile->IsPartFile()
			&& pFile->GetFilePath().CompareNoCase(pTask->strFilePath) == 0
			&& md4equ(pFile->GetFileHash(), pTask->aucFileHash)
			&& static_cast<uint64>(pFile->GetFileSize()) == pTask->uFileSize
			&& IsFileDateEqual(pFile->GetUtcFileDate(), pTask->tUtcFileDate);
	}

	void CancelSharedFilesSearchSynchronousIo(HANDLE hThread)
	{
		if (hThread == NULL)
			return;

		static PCancelSynchronousIo s_pCancelSynchronousIo = reinterpret_cast<PCancelSynchronousIo>(::GetProcAddress(::GetModuleHandle(_T("kernel32.dll")), "CancelSynchronousIo"));
		if (s_pCancelSynchronousIo != NULL)
			s_pCancelSynchronousIo(hThread);
	}

	CString BuildNoCasePathKey(const CString& path)
	{
		CString key(path);
		const int len = key.GetLength();
		bool bNeedsWin32Lower = false;
		LPTSTR pszKey = key.GetBuffer(len);
		for (int i = 0; i < len; ++i) {
			const TCHAR ch = pszKey[i];
			const unsigned int uCh = static_cast<unsigned int>(static_cast<unsigned short>(ch));
			if (ch >= _T('A') && ch <= _T('Z'))
				pszKey[i] = static_cast<TCHAR>(ch + (_T('a') - _T('A')));
			else if (uCh >= 0x80)
				bNeedsWin32Lower = true;
		}
		key.ReleaseBuffer(len);
		if (bNeedsWin32Lower) {
			::CharLowerBuff(key.GetBuffer(len), len);
			key.ReleaseBuffer(len);
		}
		return key;
	}

	CString BuildNoCaseFilePathKey(const CString& directory, const CString& name)
	{
		CString path(directory);
		if (!path.IsEmpty()) {
			const TCHAR chLast = path[path.GetLength() - 1];
			if (chLast != _T('\\') && chLast != _T('/'))
				path += _T('\\');
		}
		path += name;
		return BuildNoCasePathKey(path);
	}

	bool IsSameMetaDataTaskIdentity(const SharedFileMetaDataTask_Struct* pLeft, const SharedFileMetaDataTask_Struct* pRight)
	{
		return pLeft != NULL
			&& pRight != NULL
			&& md4equ(pLeft->aucFileHash, pRight->aucFileHash)
			&& pLeft->uFileSize == pRight->uFileSize
			&& IsFileDateEqual(pLeft->tUtcFileDate, pRight->tUtcFileDate);
	}

	void RemoveMetaDataTaskIndexLocked(SharedFileMetaDataThreadContext* pContext, const SharedFileMetaDataTask_Struct* pTask)
	{
		if (pContext == NULL || pTask == NULL || pTask->strQueueKey.IsEmpty())
			return;

		void* pvLatestTask = NULL;
		if (pContext->latestTasks.Lookup(pTask->strQueueKey, pvLatestTask) && pvLatestTask == pTask)
			pContext->latestTasks.RemoveKey(pTask->strQueueKey);
	}

	CString BuildUnknownFilePath(const UnknownFile_Struct* pFile)
	{
		CString path;
		if (pFile == NULL)
			return path;

		path = pFile->strDirectory;
		if (!path.IsEmpty()) {
			const TCHAR chLast = path[path.GetLength() - 1];
			if (chLast != _T('\\') && chLast != _T('/'))
				path += _T('\\');
		}
		path += pFile->strName;
		return path;
	}

	struct SFoundFileShareRuleSnapshot
	{
		SFoundFileShareRuleSnapshot()
			: bAutoShareSubdirs(false)
			, bHasSingleSharedFiles(false)
			, bHasSingleExcludedFiles(false)
		{
			mapSingleSharedFiles.InitHashTable(257);
			mapSingleExcludedFiles.InitHashTable(257);
			mapDirectoryDecisions.InitHashTable(1021);
		}

		bool bAutoShareSubdirs;
		bool bHasSingleSharedFiles;
		bool bHasSingleExcludedFiles;
		CString sIncoming;
		CStringList liCategoryIncoming;
		CStringList liSharedDirs;
		CStringList liExcludedSharedDirs;
		CMapStringToPtr mapSingleSharedFiles;
		CMapStringToPtr mapSingleExcludedFiles;
		CMapStringToPtr mapDirectoryDecisions;
	};

	void AddNoCasePathToMap(CMapStringToPtr& mapPaths, LPCTSTR pszPath)
	{
		if (pszPath != NULL && *pszPath != _T('\0'))
			mapPaths.SetAt(BuildNoCasePathKey(CString(pszPath)), (void*)1);
	}

	void AddPathListToNoCaseMap(CMapStringToPtr& mapPaths, const CStringList& liPaths)
	{
		for (POSITION pos = liPaths.GetHeadPosition(); pos != NULL;)
			AddNoCasePathToMap(mapPaths, liPaths.GetNext(pos));
	}

	bool LookupNoCasePath(CMapStringToPtr& mapPaths, LPCTSTR pszPath)
	{
		if (pszPath == NULL || *pszPath == _T('\0'))
			return false;
		void* pv = NULL;
		return mapPaths.Lookup(BuildNoCasePathKey(CString(pszPath)), pv) != FALSE;
	}

	bool LookupNoCasePathKey(CMapStringToPtr& mapPaths, const CString& sPathKey)
	{
		if (sPathKey.IsEmpty())
			return false;
		void* pv = NULL;
		return mapPaths.Lookup(sPathKey, pv) != FALSE;
	}

	void CopyCStringList(CStringList& dst, const CStringList& src)
	{
		dst.RemoveAll();
		for (POSITION pos = src.GetHeadPosition(); pos != NULL;)
			dst.AddTail(src.GetNext(pos));
	}

	bool ContainsPathNoCase(const CStringList& liPaths, LPCTSTR pPath)
	{
		if (pPath == NULL)
			return false;

		for (POSITION pos = liPaths.GetHeadPosition(); pos != NULL;) {
			if (liPaths.GetNext(pos).CompareNoCase(pPath) == 0)
				return true;
		}

		return false;
	}

	bool RemovePathNoCase(CStringList& liPaths, LPCTSTR pPath)
	{
		if (pPath == NULL)
			return false;

		for (POSITION pos = liPaths.GetHeadPosition(); pos != NULL;) {
			POSITION posOld = pos;
			if (liPaths.GetNext(pos).CompareNoCase(pPath) == 0) {
				liPaths.RemoveAt(posOld);
				return true;
			}
		}

		return false;
	}

#if defined(_BETA) || defined(_DEVBUILD)
	void AppendTCharSnapshotBytes(std::vector<BYTE>& data, LPCTSTR pszText)
	{
		if (pszText == NULL || *pszText == _T('\0'))
			return;
		const BYTE* pBytes = reinterpret_cast<const BYTE*>(pszText);
		data.insert(data.end(), pBytes, pBytes + (_tcslen(pszText) * sizeof(TCHAR)));
	}
#endif

	CString NormalizeDirectoryRulePath(const CString& strDirPath)
	{
		CString sDir(strDirPath);
		if (!sDir.IsEmpty()) {
			slosh(sDir);
			sDir.MakeLower();
		}
		return sDir;
	}

	bool IsSameOrSubDirectoryOfRulePath(const CString& sDir, const CString& sRoot)
	{
		if (sDir.IsEmpty() || sRoot.IsEmpty())
			return false;
		if (sDir == sRoot)
			return true;
		const int nRootLen = sRoot.GetLength();
		return sDir.GetLength() > nRootLen && _tcsncmp((LPCTSTR)sDir, (LPCTSTR)sRoot, nRootLen) == 0;
	}

	int GetBestDirectoryRuleDepthSnapshot(const CStringList& liDirs, const CString& sDirPath, bool bIncludeSubdirectories)
	{
		const CString sDir(NormalizeDirectoryRulePath(sDirPath));
		if (sDir.IsEmpty())
			return -1;

		int nBestDepth = -1;
		for (POSITION pos = liDirs.GetHeadPosition(); pos != NULL;) {
			const CString& sRule(liDirs.GetNext(pos));
			if (sRule == sDir || (bIncludeSubdirectories && IsSameOrSubDirectoryOfRulePath(sDir, sRule)))
				nBestDepth = max(nBestDepth, sRule.GetLength());
		}

		return nBestDepth;
	}

	bool ShouldShareDirectoryBySnapshot(SFoundFileShareRuleSnapshot& snapshot, const CString& sDirPath)
	{
		const CString sDir(NormalizeDirectoryRulePath(sDirPath));
		if (sDir.IsEmpty())
			return false;

		void* pvDecision = NULL;
		const CString sDecisionKey(BuildNoCasePathKey(sDir));
		if (snapshot.mapDirectoryDecisions.Lookup(sDecisionKey, pvDecision))
			return pvDecision == (void*)1;

		bool bShared = false;
		if (sDir == snapshot.sIncoming || (snapshot.bAutoShareSubdirs && IsSameOrSubDirectoryOfRulePath(sDir, snapshot.sIncoming)))
			bShared = true;

		for (POSITION pos = snapshot.liCategoryIncoming.GetHeadPosition(); !bShared && pos != NULL;) {
			const CString& sCatDir(snapshot.liCategoryIncoming.GetNext(pos));
			if (sDir == sCatDir || (snapshot.bAutoShareSubdirs && IsSameOrSubDirectoryOfRulePath(sDir, sCatDir)))
				bShared = true;
		}

		if (!bShared) {
			const int nSharedDepth = GetBestDirectoryRuleDepthSnapshot(snapshot.liSharedDirs, sDir, snapshot.bAutoShareSubdirs);
			if (nSharedDepth >= 0) {
				const int nExcludedDepth = GetBestDirectoryRuleDepthSnapshot(snapshot.liExcludedSharedDirs, sDir, true);
				bShared = nSharedDepth >= nExcludedDepth;
			}
		}

		snapshot.mapDirectoryDecisions.SetAt(sDecisionKey, bShared ? (void*)1 : (void*)2);
		return bShared;
	}

	bool ShouldShareFoundFileBySnapshot(SFoundFileShareRuleSnapshot& snapshot, const CString& sDirPath, LPCTSTR pszFilePath)
	{
		if (snapshot.bHasSingleExcludedFiles && LookupNoCasePath(snapshot.mapSingleExcludedFiles, pszFilePath))
			return false;
		if (snapshot.bHasSingleSharedFiles && LookupNoCasePath(snapshot.mapSingleSharedFiles, pszFilePath))
			return true;
		return ShouldShareDirectoryBySnapshot(snapshot, sDirPath);
	}

	bool TryBuildSharedFileIdentity(const CString& strFilePath, CString& rFileName, CString& rDirectory, time_t& rtUtcFileDate, uint64& ruFileSize)
	{
		rFileName.Empty();
		rDirectory.Empty();
		rtUtcFileDate = static_cast<time_t>(-1);
		ruFileSize = 0;

		const int iSlash = strFilePath.ReverseFind(_T('\\'));
		if (iSlash < 0 || iSlash + 1 >= strFilePath.GetLength())
			return false;

		WIN32_FILE_ATTRIBUTE_DATA fad = {};
		if (!::GetFileAttributesEx(PreparePathForWin32LongPath(strFilePath), GetFileExInfoStandard, &fad))
			return false;

		if ((fad.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY)) != 0)
			return false;

		ruFileSize = (static_cast<uint64>(fad.nFileSizeHigh) << 32) | static_cast<uint64>(fad.nFileSizeLow);
		if (ruFileSize == 0 || ruFileSize > MAX_EMULE_FILE_SIZE)
			return false;

		rtUtcFileDate = static_cast<time_t>(FileTimeToUnixTime(fad.ftLastWriteTime));
		if (rtUtcFileDate <= 0)
			rtUtcFileDate = static_cast<time_t>(-1);
		else
			AdjustNTFSDaylightFileTime(rtUtcFileDate, strFilePath);

		rDirectory = CString(strFilePath, iSlash + 1);
		rFileName = strFilePath.Mid(iSlash + 1);
		return true;
	}

	bool TryGetHashSourceSnapshot(LPCTSTR pszFilePath, uint64& ruFileSize, FILETIME& rftLastWrite)
	{
		ruFileSize = 0;
		ZeroMemory(&rftLastWrite, sizeof(rftLastWrite));
		if (pszFilePath == NULL || *pszFilePath == _T('\0'))
			return false;

		WIN32_FILE_ATTRIBUTE_DATA fad = {};
		if (!::GetFileAttributesEx(PreparePathForWin32LongPath(CString(pszFilePath)), GetFileExInfoStandard, &fad))
			return false;
		if ((fad.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY)) != 0)
			return false;

		ruFileSize = (static_cast<uint64>(fad.nFileSizeHigh) << 32) | static_cast<uint64>(fad.nFileSizeLow);
		if (ruFileSize == 0 || ruFileSize > MAX_EMULE_FILE_SIZE)
			return false;

		rftLastWrite = fad.ftLastWriteTime;
		return true;
	}

	bool IsHashSourceSnapshotCurrent(LPCTSTR pszFilePath, uint64 uFileSize, const FILETIME& ftLastWrite)
	{
		uint64 uCurrentFileSize = 0;
		FILETIME ftCurrentLastWrite = {};
		return TryGetHashSourceSnapshot(pszFilePath, uCurrentFileSize, ftCurrentLastWrite)
			&& uCurrentFileSize == uFileSize
			&& ::CompareFileTime(&ftCurrentLastWrite, &ftLastWrite) == 0;
	}

	const uint32 uInvalidEServerBuddyMagicAnnounceEpoch = static_cast<uint32>(-1);

	struct EServerBuddyMagicAnnounceEntry
	{
		uchar aucHash[MDX_DIGEST_SIZE];
		uint64 uSize;
		CString strName;
	};

	bool ShouldIncludeEServerBuddyMagicFile()
	{
		return theApp.serverconnect != NULL
			&& theApp.serverconnect->IsConnected()
			&& !theApp.serverconnect->IsLowID()
			&& theApp.clientlist != NULL
			&& theApp.clientlist->HasEServerBuddySlotAvailable();
	}

	void BuildEServerBuddyMagicAnnounceEntries(std::vector<EServerBuddyMagicAnnounceEntry>& entries, uint32 uEpoch)
	{
		entries.clear();
		if (!ShouldIncludeEServerBuddyMagicFile())
			return;

		const uchar* pLocalHash = thePrefs.GetUserHash();
		const uint32 uFineBucket = CUpDownClient::SelectEServerBuddyMagicBucket(false, uEpoch, pLocalHash, 0);
		const uint32 uBootstrapBucket = (ESERVERBUDDY_MAGIC_BOOTSTRAP_BUCKET_COUNT > 0)
			? (uEpoch % ESERVERBUDDY_MAGIC_BOOTSTRAP_BUCKET_COUNT)
			: 0;

		EServerBuddyMagicAnnounceEntry fineEntry = {};
		CUpDownClient::BuildEServerBuddyMagicBucketInfo(false, uFineBucket, uEpoch, fineEntry.aucHash, fineEntry.uSize, fineEntry.strName);
		entries.push_back(fineEntry);

		if (ESERVERBUDDY_MAGIC_BOOTSTRAP_BUCKET_COUNT > 0) {
			EServerBuddyMagicAnnounceEntry bootstrapEntry = {};
			CUpDownClient::BuildEServerBuddyMagicBucketInfo(true, uBootstrapBucket, uEpoch, bootstrapEntry.aucHash, bootstrapEntry.uSize, bootstrapEntry.strName);
			if (!md4equ(bootstrapEntry.aucHash, fineEntry.aucHash))
				entries.push_back(bootstrapEntry);
		}
	}

	void CreateEServerBuddyMagicFilePacket(CSafeMemFile& files, const EServerBuddyMagicAnnounceEntry& entry)
	{
		files.WriteHash16(entry.aucHash);
		files.WriteUInt32(theApp.GetID());
		files.WriteUInt16(thePrefs.GetPort());

		CSimpleArray<CTag*> tags;
		tags.Add(new CTag(FT_FILENAME, entry.strName));
		tags.Add(new CTag(FT_FILESIZE, static_cast<uint32>(entry.uSize)));

		files.WriteUInt32(tags.GetSize());
		for (int i = 0; i < tags.GetSize(); ++i) {
			tags[i]->WriteTagToFile(files);
			delete tags[i];
		}
	}
}

// Resolve shell link (.lnk) target with modern PIDL-first fallback chain. Returns true and fills 'outResolved' + 'outFad' on success.
static bool ResolveShellLinkTargetModern(const CString& linkPath, CString& outResolved, WIN32_FILE_ATTRIBUTE_DATA& outFad)
{
	// 1) IShellLink -> PIDL -> SHGetNameFromIDList (dynamic) or SHGetPathFromIDListEx/SHGetPathFromIDListW
	CComPtr<IShellLink> pShellLink;
	bool got = false;
	CString resolved;

	if (SUCCEEDED(pShellLink.CoCreateInstance(CLSID_ShellLink))) {
		CComQIPtr<IPersistFile> pPersistFile(pShellLink.p);
		if (pPersistFile && SUCCEEDED(pPersistFile->Load(linkPath, STGM_READ))) {
			LPITEMIDLIST pidl = NULL;
			if (SUCCEEDED(pShellLink->GetIDList(&pidl)) && pidl) {
				typedef HRESULT(WINAPI* PFNSHGetNameFromIDList)(PCIDLIST_ABSOLUTE, SIGDN, PWSTR*);
				PFNSHGetNameFromIDList pfnSHGetNameFromIDList = (PFNSHGetNameFromIDList)::GetProcAddress(::GetModuleHandle(_T("shell32.dll")), "SHGetNameFromIDList");
				HMODULE hShell32Dyn1 = NULL;
				if (!pfnSHGetNameFromIDList) {
					hShell32Dyn1 = ::LoadLibrary(_T("shell32.dll"));
					if (hShell32Dyn1)
						pfnSHGetNameFromIDList = (PFNSHGetNameFromIDList)::GetProcAddress(hShell32Dyn1, "SHGetNameFromIDList");
				}

				if (pfnSHGetNameFromIDList) {
					PWSTR psz = NULL;
					if (SUCCEEDED(pfnSHGetNameFromIDList(pidl, SIGDN_FILESYSPATH, &psz)) && psz && *psz) {
						resolved = psz;
						got = true;
					}

					if (psz)
						::CoTaskMemFree(psz);
				}

				if (hShell32Dyn1) {
					::FreeLibrary(hShell32Dyn1);
					hShell32Dyn1 = NULL;
				}

				if (!got) {
					typedef BOOL(WINAPI* PFNSHGetPathFromIDListEx)(PCIDLIST_ABSOLUTE, PWSTR, DWORD, DWORD);
					PFNSHGetPathFromIDListEx pfnEx = (PFNSHGetPathFromIDListEx)::GetProcAddress(::GetModuleHandle(_T("shell32.dll")), "SHGetPathFromIDListEx");
					HMODULE hShell32Dyn2 = NULL;
					if (!pfnEx) {
						hShell32Dyn2 = ::LoadLibrary(_T("shell32.dll"));
						if (hShell32Dyn2)
							pfnEx = (PFNSHGetPathFromIDListEx)::GetProcAddress(hShell32Dyn2, "SHGetPathFromIDListEx");
					}

					if (pfnEx) {
						WCHAR buf[32768] = { 0 };
						if (pfnEx(pidl, buf, _countof(buf), 0) && buf[0] != 0) {
							resolved = buf;
							got = true;
						}
					} else {
						WCHAR buf[MAX_PATH] = { 0 };
						if (::SHGetPathFromIDListW(pidl, buf) && buf[0] != 0) {
							resolved = buf;
							got = true;
						}
					}

					if (hShell32Dyn2) {
						::FreeLibrary(hShell32Dyn2);
						hShell32Dyn2 = NULL;
					}
				}

				::CoTaskMemFree(pidl);
			}
		}
	}

	// 2) Fallback to IShellLink::GetPath (MAX_PATH-limited)
	if (!got) {
		CComPtr<IShellLink> pShellLink2;
		if (SUCCEEDED(pShellLink2.CoCreateInstance(CLSID_ShellLink))) {
			CComQIPtr<IPersistFile> pPersistFile2(pShellLink2.p);
			if (pPersistFile2 && SUCCEEDED(pPersistFile2->Load(linkPath, STGM_READ))) {
				TCHAR szResolvedPath[MAX_PATH] = { 0 };
				if (pShellLink2->GetPath(szResolvedPath, _countof(szResolvedPath), (WIN32_FIND_DATA*)NULL, 0) == NOERROR) {
					resolved = szResolvedPath;
					got = !resolved.IsEmpty();
				}
			}
		}
	}

	if (!got || resolved.IsEmpty())
		return false;

	// Long path policy check
	if (!IsWin32LongPathsEnabled() && resolved.GetLength() >= MAX_PATH)
		return false;

	const CString longResolved = PreparePathForWin32LongPath(resolved);
	if (!GetFileAttributesEx(longResolved, GetFileExInfoStandard, &outFad))
		return false;

	outResolved = resolved;
	return true;
}

///////////////////////////////////////////////////////////////////////////////
// CPublishKeyword

class CPublishKeyword
{
public:
	explicit CPublishKeyword(const Kademlia::CKadTagValueString &rstrKeyword)
		: m_strKeyword(rstrKeyword)
	{
		// min. keyword char is allowed to be < 3 in some cases (see also 'CSearchManager::GetWords')
		ASSERT(!rstrKeyword.IsEmpty());
		KadGetKeywordHash(rstrKeyword, &m_nKadID);
		SetNextPublishTime(0);
		SetPublishedCount(0);
	}

	const Kademlia::CUInt128 &GetKadID() const			{ return m_nKadID; }
	const Kademlia::CKadTagValueString &GetKeyword() const { return m_strKeyword; }
	int GetRefCount() const								{ return m_aFiles.GetSize(); }
	const CSimpleKnownFileArray &GetReferences() const	{ return m_aFiles; }

	time_t GetNextPublishTime() const					{ return m_tNextPublishTime; }
	void SetNextPublishTime(time_t tNextPublishTime)	{ m_tNextPublishTime = tNextPublishTime; }

	UINT GetPublishedCount() const						{ return m_uPublishedCount; }
	void SetPublishedCount(UINT uPublishedCount)		{ m_uPublishedCount = uPublishedCount; }
	void IncPublishedCount()							{ ++m_uPublishedCount; }

	BOOL AddRef(CKnownFile *pFile)
	{
		if (!m_setFiles.insert(pFile).second)
			return FALSE;
		if (!m_aFiles.Add(pFile)) {
			m_setFiles.erase(pFile);
			return FALSE;
		}
		return TRUE;
	}

	int RemoveRef(CKnownFile *pFile)
	{
		m_aFiles.Remove(pFile);
		m_setFiles.erase(pFile);
		return m_aFiles.GetSize();
	}

	void RemoveAllReferences()
	{
		m_aFiles.RemoveAll();
		m_setFiles.clear();
	}

	void RotateReferences(int iRotateSize)
	{
		CKnownFile **ppRotated = reinterpret_cast<CKnownFile**>(malloc(m_aFiles.m_nAllocSize * sizeof(*m_aFiles.GetData())));
		if (ppRotated != NULL) {
			int i = m_aFiles.GetSize() - iRotateSize;
			ASSERT(i > 0);
			memcpy(ppRotated, m_aFiles.GetData() + iRotateSize, i * sizeof(*m_aFiles.GetData()));
			memcpy(ppRotated + i, m_aFiles.GetData(), iRotateSize * sizeof(*m_aFiles.GetData()));
			free(m_aFiles.GetData());
			m_aFiles.m_aT = ppRotated;
		}
	}

protected:
	CSimpleKnownFileArray m_aFiles;
	std::unordered_set<CKnownFile*> m_setFiles;
	Kademlia::CKadTagValueString m_strKeyword;
	Kademlia::CUInt128 m_nKadID;
	time_t m_tNextPublishTime;
	UINT m_uPublishedCount;
};


///////////////////////////////////////////////////////////////////////////////
// CPublishKeywordList

class CPublishKeywordList
{
public:
	CPublishKeywordList();
	~CPublishKeywordList();

	void AddKeywords(CKnownFile *pFile);
	void RemoveKeywords(CKnownFile *pFile);
	void RemoveAllKeywords();

	void RemoveAllKeywordReferences();
	void PurgeUnreferencedKeywords();
	bool PurgeUnreferencedKeywordsChunk(CString& strCursorKeyword, bool& bStarted, UINT uMaxKeywords, UINT& uProcessed, INT_PTR& iRemaining);

	INT_PTR GetCount() const								{ return m_lstKeywords.GetCount(); }

	CPublishKeyword *GetNextKeyword();
	void ResetNextKeyword();

	time_t GetNextPublishTime() const						{ return m_tNextPublishKeywordTime; }
	void SetNextPublishTime(time_t tNextPublishKeywordTime)	{ m_tNextPublishKeywordTime = tNextPublishKeywordTime; }

#ifdef _DEBUG
	void Dump();
#endif

protected:
	CTypedPtrList<CPtrList, CPublishKeyword*> m_lstKeywords;
	CMapStringToPtr m_mapKeywords;
	POSITION m_posNextKeyword;
	POSITION m_posPurgeKeywordCursor;
	time_t m_tNextPublishKeywordTime;

	CPublishKeyword *FindKeyword(const CStringW &rstrKeyword, POSITION *ppos = NULL);
};

CPublishKeywordList::CPublishKeywordList()
	: m_posPurgeKeywordCursor(NULL)
{
	m_mapKeywords.InitHashTable(4099);
	ResetNextKeyword();
	SetNextPublishTime(0);
}

CPublishKeywordList::~CPublishKeywordList()
{
	RemoveAllKeywords();
}

CPublishKeyword *CPublishKeywordList::GetNextKeyword()
{
	if (m_posNextKeyword == NULL) {
		m_posNextKeyword = m_lstKeywords.GetHeadPosition();
		if (m_posNextKeyword == NULL)
			return NULL;
	}
	return m_lstKeywords.GetNext(m_posNextKeyword);
}

void CPublishKeywordList::ResetNextKeyword()
{
	m_posNextKeyword = m_lstKeywords.GetHeadPosition();
}

CPublishKeyword *CPublishKeywordList::FindKeyword(const CStringW &rstrKeyword, POSITION *ppos)
{
	if (ppos == NULL) {
		void* pvKeyword = NULL;
		if (m_mapKeywords.Lookup(CString(rstrKeyword), pvKeyword))
			return static_cast<CPublishKeyword*>(pvKeyword);
		return NULL;
	}

	for (POSITION pos = m_lstKeywords.GetHeadPosition(); pos != NULL;) {
		POSITION posLast = pos;
		CPublishKeyword *pPubKw = m_lstKeywords.GetNext(pos);
		if (pPubKw->GetKeyword() == rstrKeyword) {
			*ppos = posLast;
			return pPubKw;
		}
	}
	return NULL;
}

void CPublishKeywordList::AddKeywords(CKnownFile *pFile)
{
	const Kademlia::WordList &wordlist(pFile->GetKadKeywords());
	for (Kademlia::WordList::const_iterator it = wordlist.begin(); it != wordlist.end(); ++it) {
		const CStringW &strKeyword(*it);
		CPublishKeyword *pPubKw = FindKeyword(strKeyword);
		if (pPubKw == NULL) {
			pPubKw = new CPublishKeyword(Kademlia::CKadTagValueString(strKeyword));
			m_lstKeywords.AddTail(pPubKw);
			m_mapKeywords.SetAt(CString(strKeyword), pPubKw);
			SetNextPublishTime(0);
		}
		if (pPubKw->AddRef(pFile) && pPubKw->GetNextPublishTime() > MIN2S(30)) {
			// User may be adding and removing files, so if this is a keyword that
			// has already been published, we reduce the time, but still give the user
			// enough time to finish what they are doing.
			// If this is a hot node, the Load list will prevent from republishing.
			pPubKw->SetNextPublishTime(MIN2S(30));
		}
	}
}

void CPublishKeywordList::RemoveKeywords(CKnownFile *pFile)
{
	const Kademlia::WordList &wordlist = pFile->GetKadKeywords();
	for (Kademlia::WordList::const_iterator it = wordlist.begin(); it != wordlist.end(); ++it) {
		const CStringW &strKeyword(*it);
		POSITION pos;
		CPublishKeyword *pPubKw = FindKeyword(strKeyword, &pos);
		if (pPubKw != NULL && pPubKw->RemoveRef(pFile) == 0) {
			if (pos == m_posNextKeyword)
				(void)m_lstKeywords.GetNext(m_posNextKeyword);
			if (pos == m_posPurgeKeywordCursor)
				(void)m_lstKeywords.GetNext(m_posPurgeKeywordCursor);
			m_mapKeywords.RemoveKey(CString(strKeyword));
			m_lstKeywords.RemoveAt(pos);
			delete pPubKw;
			SetNextPublishTime(0);
		}
	}
}

void CPublishKeywordList::RemoveAllKeywords()
{
	while (!m_lstKeywords.IsEmpty())
		delete m_lstKeywords.RemoveHead();
	m_mapKeywords.RemoveAll();
	m_posPurgeKeywordCursor = NULL;
	ResetNextKeyword();
	SetNextPublishTime(0);
}

void CPublishKeywordList::RemoveAllKeywordReferences()
{
	for (POSITION pos = m_lstKeywords.GetHeadPosition(); pos != NULL;)
		m_lstKeywords.GetNext(pos)->RemoveAllReferences();
}

void CPublishKeywordList::PurgeUnreferencedKeywords()
{
	for (POSITION pos = m_lstKeywords.GetHeadPosition(); pos != NULL;) {
		POSITION posLast = pos;
		const CPublishKeyword *pPubKw = m_lstKeywords.GetNext(pos);
		if (pPubKw->GetRefCount() == 0) {
			if (posLast == m_posNextKeyword)
				m_posNextKeyword = pos;
			if (posLast == m_posPurgeKeywordCursor)
				m_posPurgeKeywordCursor = pos;
			m_mapKeywords.RemoveKey(CString(pPubKw->GetKeyword()));
			m_lstKeywords.RemoveAt(posLast);
			delete pPubKw;
			SetNextPublishTime(0);
		}
	}
}

bool CPublishKeywordList::PurgeUnreferencedKeywordsChunk(CString& strCursorKeyword, bool& bStarted, UINT uMaxKeywords, UINT& uProcessed, INT_PTR& iRemaining)
{
	uProcessed = 0;
	if (!bStarted) {
		strCursorKeyword.Empty();
		m_posPurgeKeywordCursor = m_lstKeywords.GetHeadPosition();
		bStarted = true;
	}

	while (m_posPurgeKeywordCursor != NULL && (uMaxKeywords == 0 || uProcessed < uMaxKeywords)) {
		POSITION posCurrent = m_posPurgeKeywordCursor;
		const CPublishKeyword *pPubKw = m_lstKeywords.GetNext(m_posPurgeKeywordCursor);
		if (pPubKw == NULL)
			continue;

		const CString strCurrentKeyword(pPubKw->GetKeyword());
		strCursorKeyword = strCurrentKeyword;
		++uProcessed;
		if (pPubKw->GetRefCount() == 0) {
			if (posCurrent == m_posNextKeyword)
				m_posNextKeyword = m_posPurgeKeywordCursor;
			m_mapKeywords.RemoveKey(strCurrentKeyword);
			m_lstKeywords.RemoveAt(posCurrent);
			delete pPubKw;
			SetNextPublishTime(0);
		}
	}

	iRemaining = m_posPurgeKeywordCursor != NULL ? 1 : 0;
	if (m_posPurgeKeywordCursor == NULL) {
		bStarted = false;
		strCursorKeyword.Empty();
	}
	return true;
}

#ifdef _DEBUG
void CPublishKeywordList::Dump()
{
	unsigned i = 0;
	for (POSITION pos = m_lstKeywords.GetHeadPosition(); pos != NULL;) {
		CPublishKeyword *pPubKw = m_lstKeywords.GetNext(pos);
		TRACE(_T("%3u: %-10ls  ref=%u  %s\n"), i, (LPCTSTR)pPubKw->GetKeyword(), pPubKw->GetRefCount(), (LPCTSTR)CastSecondsToHM(pPubKw->GetNextPublishTime()));
		++i;
	}
}
#endif

///////////////////////////////////////////////////////////////////////////////
// CAddFileThread

IMPLEMENT_DYNCREATE(CAddFileThread, CWinThread)

CAddFileThread::CAddFileThread()
	: m_pOwner()
	, m_partfile()
	, m_pImportOperationContext()
	, m_strDirectory()
	, m_strFilename()
	, m_strSharedDir()
	, m_strImport()
	, m_strPartFileName()
	, m_dwPartFileRuntimeID(0)
	, m_bPartFileHashTokenValid(false)
	, m_bRequireStableHashSource(false)
	, m_pSharedHashResult()
{
	md4clr(m_abyPartFileHash);
}

CAddFileThread::~CAddFileThread()
{
	if (m_pSharedHashResult != NULL) {
		delete m_pSharedHashResult->pKnownFile;
		delete m_pSharedHashResult;
	}
}

void CAddFileThread::SetValues(CSharedFileList *pOwner, LPCTSTR directory, LPCTSTR filename, LPCTSTR strSharedDir, CPartFile *partfile, bool bRequireStableHashSource)
{
	m_pOwner = pOwner;
	m_strDirectory = directory;
	m_strFilename = filename;
	m_partfile = partfile;
	m_strSharedDir = strSharedDir;
	m_bRequireStableHashSource = bRequireStableHashSource;
	m_strPartFileName.Empty();
	m_dwPartFileRuntimeID = 0;
	md4clr(m_abyPartFileHash);
	m_bPartFileHashTokenValid = false;
	if (partfile != NULL) {
		m_strPartFileName = partfile->GetFileName();
		m_dwPartFileRuntimeID = partfile->GetRuntimeID();
		md4cpy(m_abyPartFileHash, partfile->GetFileHash());
		m_bPartFileHashTokenValid = true;
	}
}

void CAddFileThread::SetSharedHashResult(SharedFileHashResult_Struct* pResult)
{
	ASSERT(m_pSharedHashResult == NULL);
	m_pSharedHashResult = pResult;
}

void CAddFileThread::CompleteSharedHashResult(CKnownFile* pKnownFile)
{
	SharedFileHashResult_Struct* pResult = m_pSharedHashResult;
	m_pSharedHashResult = NULL;
	if (pResult == NULL) {
		delete pKnownFile;
		return;
	}

	pResult->pKnownFile = pKnownFile;
	if (theApp.IsClosing()) {
		delete pKnownFile;
		delete pResult;
		return;
	}

	const UINT uMessage = pKnownFile != NULL ? TM_FINISHEDHASHING : TM_HASHFAILED;
	if (theApp.emuledlg != NULL && ::IsWindow(theApp.emuledlg->m_hWnd) && theApp.emuledlg->PostMessage(uMessage, 0, reinterpret_cast<LPARAM>(pResult)))
		return;

	ASSERT(m_pOwner != NULL);
	if (m_pOwner != NULL)
		m_pOwner->QueueDeferredHashResult(pResult);
	else {
		delete pKnownFile;
		delete pResult;
	}
}

void CAddFileThread::SetImportOperationContext(ImportOperationContext* pContext)
{
	if (m_pImportOperationContext != NULL)
		ReleaseImportOperationContext(m_pImportOperationContext);
	m_pImportOperationContext = AcquireImportOperationContext(pContext);
}

// Special case for SR13-ImportParts
uint16 CAddFileThread::SetPartToImport(LPCTSTR import)
{
	if (m_partfile->GetFilePath() == import)
		return 0;

	m_strImport = import;

	for (UINT i = 0; i < m_partfile->GetPartCount(); ++i)
		if (!m_partfile->IsComplete(i))
			m_PartsToImport.Add((uint16)i);

	return (uint16)m_PartsToImport.GetSize();
}

bool CAddFileThread::ImportParts()
{
	ImportOperationContext* const pContext = m_pImportOperationContext;
	if (pContext == NULL)
		return false;

	uint64 fileSize = 0;
	HANDLE hImport = OpenImportSourceLongPath(m_strImport, fileSize);
	if (hImport == INVALID_HANDLE_VALUE) {
		::InterlockedExchange(&pContext->lPendingFinish, 1);
		::InterlockedExchange(&pContext->lAborted, 1);
		theApp.QueueImportPartFinished(pContext, true);
		return false;
	}

	CString strFilePath;
	_tmakepathlimit(strFilePath.GetBuffer(MAX_PATH), NULL, m_strDirectory, m_strFilename, NULL);
	strFilePath.ReleaseBuffer();

	Log(LOG_STATUSBAR, GetResString(_T("IMPORTPARTS_IMPORTSTART")), m_PartsToImport.GetSize(), (LPCTSTR)strFilePath);

	BYTE *partData = NULL;
	unsigned partsuccess = 0;
	CKnownFile kfimport;
	bool bImportAborted = IsImportOperationAborted(pContext);

	for (INT_PTR i = 0; i < m_PartsToImport.GetSize(); ++i) {
		bImportAborted = IsImportOperationAborted(pContext);
		if (bImportAborted || !theApp.IsRunning())
			break;

		const uint16 partnumber = m_PartsToImport[i];
		const uint64 uStart = PARTSIZE * partnumber;
		if (uStart > fileSize)
			break;

		try {
			uint32 partSize;
			try {
				if (partData == NULL)
					partData = new BYTE[PARTSIZE];
				*(uint64*)partData = 0; // Quick zero check
				CSingleLock sLock1(&theApp.hashing_mut, TRUE); // SafeHash

				LARGE_INTEGER _seekTo; _seekTo.QuadPart = (LONGLONG)uStart;
				if (!::SetFilePointerEx(hImport, _seekTo, NULL, FILE_BEGIN)) {
						LogWarning(LOG_STATUSBAR, GetResString(_T("IMPORTPARTS_SEEK_FAILED")), (unsigned)partnumber, (LPCTSTR)EscPercent(GetErrorMessage(::GetLastError())));
					continue;
				}

				DWORD _dwRead = 0;
				if (!::ReadFile(hImport, partData, PARTSIZE, &_dwRead, NULL)) {
						LogWarning(LOG_STATUSBAR, GetResString(_T("IMPORTPARTS_READ_FAILED")), (unsigned)partnumber, (LPCTSTR)EscPercent(GetErrorMessage(::GetLastError())));
					continue;
				}

				partSize = (uint32)_dwRead;

				if (*(uint64*)partData == 0 && (partSize <= sizeof(uint64) || !memcmp(partData, partData + sizeof(uint64), partSize - sizeof(uint64))))
					continue;
			} catch (...) {
				LogWarning(LOG_STATUSBAR, GetResString(_T("IMPORTPARTS_PART_NOT_ACCESSIBLE")), (int)partnumber);
				continue;
			}
			if (IsImportOperationAborted(pContext))
				break;

			uchar hash[MDX_DIGEST_SIZE];
			kfimport.CreateHash(partData, partSize, hash);
			ImportPart_Struct *importpart = new ImportPart_Struct;
			importpart->start = uStart;
			importpart->end = importpart->start + partSize - 1;
			importpart->data = partData;
			importpart->pContext = AcquireImportOperationContext(pContext);
			::InterlockedIncrement(&pContext->lQueuedBlocks);
			if (!theApp.QueueImportPartWrite(importpart)) {
				partData = NULL; // Queue helper owns and releases the buffer on failure.
				bImportAborted = true;
				break;
			}
			partData = NULL; // Will be deleted in async write thread
			++partsuccess;

			if (theApp.IsRunning()) {
				WPARAM uProgress = (WPARAM)(i * 100 / m_PartsToImport.GetSize());
				theApp.QueueImportPartProgress(pContext, uProgress);
				::Sleep(100);
			}

			bImportAborted = IsImportOperationAborted(pContext);
			if (!theApp.IsRunning() || partSize != PARTSIZE || bImportAborted)
				break;
		} catch (...) {
			bImportAborted = true;
			break;
		}
	}

	if (hImport != INVALID_HANDLE_VALUE)
		::CloseHandle(hImport);

	delete[] partData;

	bImportAborted = bImportAborted || !theApp.IsRunning() || IsImportOperationAborted(pContext);
	if (bImportAborted)
		::InterlockedExchange(&pContext->lAborted, 1);
	::InterlockedExchange(&pContext->lPendingFinish, 1);
	Log(LOG_STATUSBAR, GetResString(bImportAborted ? _T("IMPORTPARTS_IMPORT_ABORTED") : _T("IMPORTPARTS_IMPORT_COMPLETED"))
		, partsuccess
		, (LPCTSTR)m_strFilename);

	theApp.QueueImportPartFinished(pContext, bImportAborted);

	return true;
}

BOOL CAddFileThread::InitInstance()
{
	return TRUE;
}

int CAddFileThread::Run()
{
	try {
		return RunInternal();
	} catch (CException* ex) {
		ex->Delete();
		TRACE(_T("Shared file worker failed with an MFC exception.\n"));
	} catch (...) {
		TRACE(_T("Shared file worker failed with an unexpected exception.\n"));
	}

	if (m_pImportOperationContext != NULL) {
		ReleaseImportOperationContext(m_pImportOperationContext);
		m_pImportOperationContext = NULL;
	}
	if (m_pSharedHashResult != NULL)
		CompleteSharedHashResult(NULL);
	return 0;
}

int CAddFileThread::RunInternal()
{
	DbgSetThreadName(m_pImportOperationContext != NULL ? "ImportingParts %s" : "Hashing %s", (LPCTSTR)m_strFilename);
	if (!(m_pOwner || m_bPartFileHashTokenValid || m_pImportOperationContext) || m_strFilename.IsEmpty() || theApp.IsClosing()) {
		if (m_pImportOperationContext != NULL) {
			ReleaseImportOperationContext(m_pImportOperationContext);
			m_pImportOperationContext = NULL;
		}
		if (m_pSharedHashResult != NULL)
			CompleteSharedHashResult(NULL);
		return 0;
	}

	(void)CoInitialize(NULL);

	if (m_pImportOperationContext != NULL) {
		ImportParts();
		ReleaseImportOperationContext(m_pImportOperationContext);
		m_pImportOperationContext = NULL;
		CoUninitialize();
		return 0;
	}

	// Locking this hashing thread is needed because we may create a few of those threads
	// at startup when rehashing potentially corrupted downloading part files.
	// If all those hash threads would run concurrently, the I/O system would be under
	// very heavy load and slowly progressing
	CSingleLock hashingLock(&theApp.hashing_mut, TRUE); // hash only one file at a time

	if (theApp.IsClosing()) {
		hashingLock.Unlock();
		CoUninitialize();
		return 0;
	}

	TCHAR strFilePath[MAX_PATH];
	_tmakepathlimit(strFilePath, NULL, m_strDirectory, m_strFilename, NULL);
	if (m_bPartFileHashTokenValid)
		Log(_T("%s \"%s\" \"%s\""), (LPCTSTR)GetResString(_T("HASHINGFILE")), (LPCTSTR)EscPercent(m_strPartFileName), (LPCTSTR)EscPercent(strFilePath));
	else
		Log(_T("%s \"%s\""), (LPCTSTR)GetResString(_T("HASHINGFILE")), (LPCTSTR)EscPercent(strFilePath));

	uint64 uHashSourceFileSize = 0;
	FILETIME ftHashSourceLastWrite = {};
	bool bCheckHashSourceStability = false;
	CKnownFile *newKnown = NULL;
	bool bHashSourceChanged = false;
	SFileHashProgressContext progressContext = {};
	const SFileHashProgressContext* pProgressContext = NULL;
	if (m_bPartFileHashTokenValid) {
		progressContext.pPartFile = m_partfile;
		progressContext.dwRuntimeID = m_dwPartFileRuntimeID;
		md4cpy(progressContext.abyFileHash, m_abyPartFileHash);
		pProgressContext = &progressContext;
	}
	// Do not pass a CPartFile pointer into the hashing worker. It is only a UI-thread lifetime token.
	bool bHashSucceeded = false;
	try {
		bCheckHashSourceStability = m_bRequireStableHashSource && !m_bPartFileHashTokenValid && TryGetHashSourceSnapshot(strFilePath, uHashSourceFileSize, ftHashSourceLastWrite);
		newKnown = new CKnownFile();
		bHashSucceeded = !theApp.IsClosing() && newKnown->CreateFromFile(m_strDirectory, m_strFilename, pProgressContext, false); // SLUGFILLER: SafeHash - in case of shutdown while still hashing
		if (bHashSucceeded && bCheckHashSourceStability) {
			bHashSourceChanged = !IsHashSourceSnapshotCurrent(strFilePath, uHashSourceFileSize, ftHashSourceLastWrite);
			if (bHashSourceChanged)
				bHashSucceeded = false;
		}
		if (bHashSucceeded)
			newKnown->SetSharedDirectory(m_strSharedDir);
	}
	catch (CException* ex) {
		ex->Delete();
		TRACE(_T("Shared file hashing failed with an MFC exception.\n"));
	}
	catch (...) {
		TRACE(_T("Shared file hashing failed with an unexpected exception.\n"));
	}
	if (bHashSucceeded) {
		if (theApp.IsClosing()) {
			delete newKnown;
			newKnown = NULL;
		} else if (m_bPartFileHashTokenValid) {
			PartFileHash_Struct* hashed = new PartFileHash_Struct;
			hashed->pPartFile = m_partfile;
			hashed->dwRuntimeID = m_dwPartFileRuntimeID;
			md4cpy(hashed->abyFileHash, m_abyPartFileHash);
			hashed->pKnownFile = newKnown;
			if (theApp.emuledlg == NULL || !::IsWindow(theApp.emuledlg->m_hWnd) || !theApp.emuledlg->PostMessage(TM_FINISHEDPARTFILEHASHING, 0, (LPARAM)hashed)) {
				if (theApp.sharedfiles != NULL)
					theApp.sharedfiles->QueueDeferredPartFileHashResult(hashed);
				else {
					delete newKnown;
					delete hashed;
				}
			}
		} else {
			CompleteSharedHashResult(newKnown);
			newKnown = NULL;
		}
	} else {
		if (bHashSourceChanged && theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL)
			theApp.emuledlg->sharedfileswnd->PostAutoReloadSharedFilesAsync(0);
		if (!theApp.IsClosing() && m_bPartFileHashTokenValid) {
			// SLUGFILLER: SafeHash - inform main program of hash failure
			PartFileHash_Struct* hashed = new PartFileHash_Struct;
			hashed->pPartFile = m_partfile;
			hashed->dwRuntimeID = m_dwPartFileRuntimeID;
			md4cpy(hashed->abyFileHash, m_abyPartFileHash);
			hashed->pKnownFile = NULL;
			if (theApp.emuledlg == NULL || !::IsWindow(theApp.emuledlg->m_hWnd) || !theApp.emuledlg->PostMessage(TM_PARTFILEHASHFAILED, 0, (LPARAM)hashed)) {
				if (theApp.sharedfiles != NULL)
					theApp.sharedfiles->QueueDeferredPartFileHashResult(hashed);
				else
					delete hashed;
			}
		}
		// SLUGFILLER: SafeHash
		delete newKnown;
		newKnown = NULL;
		if (!m_bPartFileHashTokenValid)
			CompleteSharedHashResult(NULL);
	}

	hashingLock.Unlock();
	CoUninitialize();
	return 0;
}

///////////////////////////////////////////////////////////////////////////////
// CSharedFileList

void CSharedFileList::AddDirectory(const CString &strDir, CMapStringToPtr &dirset)
{
	ASSERT(strDir.Right(1) == _T("\\"));
	const CString slDir(BuildNoCasePathKey(strDir));
	void* pv = NULL;

	if (!dirset.Lookup(slDir, pv)) {
		dirset.SetAt(slDir, (void*)1);
		AddFilesFromDirectory(strDir);
	}
}

void CSharedFileList::InvalidateShareRuleSnapshot()
{
	::InterlockedIncrement(&m_lShareRuleGeneration);
	if (m_searchThread != NULL)
		m_searchThread->InvalidateShareRuleSnapshot();
}

CSharedFileList::CSharedFileList(CServerConnect *in_server)
	: server(in_server)
	, output()
	, m_currFileSrc()
	, m_currFileNotes()
	, m_lastPublishKadSrc()
	, m_lastPublishKadNotes()
	, m_lastPublishED2K()
	, m_lastPublishED2KFlag(true)
	, m_uLastEServerBuddyMagicAnnounceEpoch(uInvalidEServerBuddyMagicAnnounceEpoch)
	, bHaveSingleSharedFiles()
	, m_bExplicitShareRulesLoaded(false)
	, m_lRebuildMetaDataThreadActive()
	, m_pMetaDataThreadContext(new SharedFileMetaDataThreadContext)
	, m_posMetaDataReconciliation(NULL)
	, m_uMetaDataReconciliationPathRevision()
	, m_uSharedPathCacheRevision(1)
	, m_bMetaDataReconciliationStarted(false)
	, m_uMetadataUpdatingCount()
	, m_bInFoundFilesProcessing(false)
	, m_bTreeReloadPending(false)
	, m_bReloadLookupSnapshotActive(false)
	, m_bReloadScanActive(false)
	, m_lReloadScanDirWatchGeneration(0)
	, m_posReloadPruneCandidate(NULL)
	, m_bSharedFilesCompletionActive(false)
	, m_bSharedFilesCompletionPending(false)
	, m_bCompletionKeywordPurgeStarted(false)
	, m_bSharedFilesModelChangedSinceListUpdate(false)
	, m_lSharedFilesModelRevision(1)
	, m_bSharedCacheRefreshStarted(false)
	, m_bSharedCacheRefreshCommitted(false)
	, m_uSharedFilesCompletionStep(SharedFilesCompletionIdle)
	, m_strCompletionKeywordPurgeCursor()
	, m_uSharedCacheRefreshIndex(0)
	, m_bStartupScanDeferred(false)
	, m_bStartupScanCompleted(false)
	, m_lSharedFilesSaveGeneration(0)
	, m_lShareRuleGeneration(1)
	, m_searchThread(NULL)
	, m_lFoundFilesNotify(0)
	, m_bContinueFoundProcessing(false)
{
	m_pDeferredHashResult = NULL;
	m_Files_map.InitHashTable(1031);
	m_ReloadLookupFiles_map.InitHashTable(1031);
	m_mapSharedPathsNoCase.InitHashTable(16381);
	m_mapReloadFoundFileIdentities.InitHashTable(16381);
	m_mapWebSharedFileSnapshotIndexes.InitHashTable(16381);
	m_mapHashingPathsNoCase.InitHashTable(131071);
	m_keywords = new CPublishKeywordList;
#if defined(_BETA) || defined(_DEVBUILD)
	// In Beta and development versions we create a test file which is published in order to make
	// testing easier by allowing easily find files which are published and shared by "new" nodes
	// Compose the name of the test file
	m_strBetaFileName.Format(_T("eMule%u.%u%c.%u Beta Testfile "), CemuleApp::m_nVersionMjr
		, CemuleApp::m_nVersionMin, _T('a') + CemuleApp::m_nVersionUpd, CemuleApp::m_nVersionBld);
	const MD5Sum md5(m_strBetaFileName + CemuleApp::m_sPlatform);
	m_strBetaFileName.AppendFormat(_T("%s.txt"), (LPCTSTR)md5.GetHashString().Left(6));
#endif

	if (theApp.KnownFilesReady()) {
		m_bStartupScanCompleted = false;
		m_bSharedFilesCompletionPending = true;
		StartSearchThread();
		LoadSingleSharedFilesList();
		FindSharedFiles();
	}
	else {
		m_bStartupScanDeferred = true;
		m_bStartupScanCompleted = false;
		AddDebugLogLine(DLP_LOW, false, _T("Shared files startup scan deferred until known files are ready.\n"));
	}
}


void CSharedFileList::StartDeferredStartupScan()
{
	if (!m_bStartupScanDeferred)
		return;

	if (!theApp.KnownFilesReady()) {
		AddDebugLogLine(DLP_VERYLOW, false, _T("Shared files startup scan still waiting for known files readiness.\n"));
		return;
	}
	const CemuleApp::SStartupMetadataLoadState sharedRulesState = theApp.GetStartupMetadataLoadState(CemuleApp::StartupMetadataSharedRules);
	if (sharedRulesState.m_eState == CemuleApp::StartupMetadataStateNotStarted || sharedRulesState.m_eState == CemuleApp::StartupMetadataStateLoading || sharedRulesState.m_eState == CemuleApp::StartupMetadataStateApplying)
		return;
	if (!sharedRulesState.IsReady())
		AddDebugLogLine(DLP_LOW, false, _T("Shared files startup scan released after optional shared cache load failure. state=%u reason=%s error=%lu\n"), static_cast<UINT>(sharedRulesState.m_eState), (LPCTSTR)sharedRulesState.m_strReason, sharedRulesState.m_dwLastError);

	m_bStartupScanDeferred = false;
	m_bStartupScanCompleted = false;
	m_bSharedFilesCompletionPending = true;
	StartSearchThread();
	LoadSingleSharedFilesList();
	FindSharedFiles();
}

void CSharedFileList::StartDeferredStartupScanAfterKnownFilesFailure()
{
	if (!m_bStartupScanDeferred || theApp.knownfiles == NULL || theApp.IsClosing())
		return;

	const CemuleApp::SStartupMetadataLoadState knownState = theApp.GetStartupMetadataLoadState(CemuleApp::StartupMetadataKnownFiles);
	if (knownState.m_eState != CemuleApp::StartupMetadataStateFailed)
		return;

	m_bStartupScanDeferred = false;
	m_bStartupScanCompleted = false;
	m_bSharedFilesCompletionPending = true;
	AddDebugLogLine(DLP_HIGH, false, _T("Shared files startup scan released after known files load failure. reason=%s error=%lu\n"), (LPCTSTR)knownState.m_strReason, knownState.m_dwLastError);
	StartSearchThread();
	LoadSingleSharedFilesList();
	FindSharedFiles();
}

CSharedFileList::~CSharedFileList()
{
	ShutdownSearchThreadForExit();
	ShutdownMetaDataUpdateThread();

	while (!waitingforhash_list.IsEmpty())
		delete waitingforhash_list.RemoveHead();
	// SLUGFILLER: SafeHash
	while (!currentlyhashing_list.IsEmpty())
		delete currentlyhashing_list.RemoveHead();
	{
		CSingleLock lock(&m_csDeferredHashResults, TRUE);
		if (m_pDeferredHashResult != NULL) {
			delete m_pDeferredHashResult->pKnownFile;
			delete m_pDeferredHashResult;
		}
		m_pDeferredHashResult = NULL;
	}
	{
		CSingleLock lock(&m_csDeferredPartFileHashResults, TRUE);
		while (!m_deferredPartFileHashResults.IsEmpty()) {
			PartFileHash_Struct* pResult = m_deferredPartFileHashResults.RemoveHead();
			delete pResult->pKnownFile;
			delete pResult;
		}
	}
	m_mapHashingPathsNoCase.RemoveAll();
	// SLUGFILLER: SafeHash
	delete m_keywords;

#if defined(_BETA) || defined(_DEVBUILD)
	//Delete the test file
	CString sTest(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
	sTest += m_strBetaFileName;
	::DeleteFile(sTest);
#endif
}

void CSharedFileList::ShutdownSearchThreadForExit()
{
	StopSearchThread();
}

bool CSharedFileList::LoadSharedCacheForStartup(LONG lGeneration, uint64 uCancellationToken)
{
	return m_sharedCache.Load(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR), lGeneration, uCancellationToken);
}

void CSharedFileList::CopySharedFileMap(CKnownFilesMap &Files_Map)
{
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		Files_Map[pair->key] = pair->value;
}

void CSharedFileList::BeginReloadLookupSnapshot()
{
	CSingleLock listlock(&m_mutWriteList, TRUE);
	m_ReloadLookupFiles_map.RemoveAll();
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair)) {
		if (pair->value != NULL)
			m_ReloadLookupFiles_map[CSKey(pair->value->GetFileHash())] = pair->value;
	}
	m_bReloadLookupSnapshotActive = !m_ReloadLookupFiles_map.IsEmpty();
}

void CSharedFileList::EndReloadLookupSnapshot()
{
	CSingleLock listlock(&m_mutWriteList, TRUE);
	m_ReloadLookupFiles_map.RemoveAll();
	m_bReloadLookupSnapshotActive = false;
}

bool CSharedFileList::ClearReloadLookupSnapshotChunk(UINT uMaxFiles, UINT& uProcessed, INT_PTR& iRemaining)
{
	uProcessed = 0;
	CSingleLock listlock(&m_mutWriteList, TRUE);
	while (uProcessed < uMaxFiles && !m_ReloadLookupFiles_map.IsEmpty()) {
		const CReloadLookupFilesMap::CPair* pair = m_ReloadLookupFiles_map.PGetFirstAssoc();
		if (pair == NULL)
			break;
		m_ReloadLookupFiles_map.RemoveKey(pair->key);
		++uProcessed;
	}
	iRemaining = m_ReloadLookupFiles_map.GetCount();
	if (iRemaining == 0)
		m_bReloadLookupSnapshotActive = false;
	return true;
}

CString CSharedFileList::BuildReloadFileIdentityKey(LPCTSTR pszFilePath, uint64 uFileSize, time_t tUtcFileDate)
{
	CString strKey(BuildNoCasePathKey(pszFilePath != NULL ? CString(pszFilePath) : CString()));
	strKey.AppendFormat(_T("#%I64u#%I64d"), uFileSize, static_cast<__int64>(tUtcFileDate));
	return strKey;
}

bool CSharedFileList::ParseReloadFileIdentityKey(const CString& strIdentity, uint64& ruFileSize, time_t& rtUtcFileDate)
{
	const int iTimeSep = strIdentity.ReverseFind(_T('#'));
	if (iTimeSep <= 0 || iTimeSep == strIdentity.GetLength() - 1)
		return false;

	const CString strPrefix(strIdentity.Left(iTimeSep));
	const int iSizeSep = strPrefix.ReverseFind(_T('#'));
	if (iSizeSep < 0 || iSizeSep == strPrefix.GetLength() - 1)
		return false;

	const CString strSize(strPrefix.Mid(iSizeSep + 1));
	const CString strTime(strIdentity.Mid(iTimeSep + 1));
	TCHAR* pszEnd = NULL;
	const uint64 uFileSize = static_cast<uint64>(_tcstoui64(strSize, &pszEnd, 10));
	if (pszEnd == NULL || *pszEnd != _T('\0'))
		return false;

	pszEnd = NULL;
	const __int64 iUtcFileDate = _tcstoi64(strTime, &pszEnd, 10);
	if (pszEnd == NULL || *pszEnd != _T('\0'))
		return false;

	ruFileSize = uFileSize;
	rtUtcFileDate = static_cast<time_t>(iUtcFileDate);
	return true;
}

bool CSharedFileList::IsReloadFileIdentityCurrent(const CString& strIdentity, uint64 uFileSize, time_t tUtcFileDate)
{
	uint64 uFoundFileSize = 0;
	time_t tFoundUtcFileDate = 0;
	if (!ParseReloadFileIdentityKey(strIdentity, uFoundFileSize, tFoundUtcFileDate))
		return false;

	return uFoundFileSize == uFileSize && IsFileDateEqual(tFoundUtcFileDate, tUtcFileDate);
}

void CSharedFileList::BeginReloadScan(LONG lDirWatchGeneration)
{
	POSITION posReloadPruneCandidate = NULL;
	{
		CSingleLock listlock(&m_mutWriteList, TRUE);
		posReloadPruneCandidate = m_ReloadLookupFiles_map.GetStartPosition();
	}

	CSingleLock reloadlock(&m_csReloadScan, TRUE);
	m_mapReloadFoundFileIdentities.RemoveAll();
	m_posReloadPruneCandidate = posReloadPruneCandidate;
	m_lReloadScanDirWatchGeneration = lDirWatchGeneration;
	m_bReloadScanActive = true;
}

void CSharedFileList::EndReloadScan()
{
	CSingleLock reloadlock(&m_csReloadScan, TRUE);
	m_mapReloadFoundFileIdentities.RemoveAll();
	m_posReloadPruneCandidate = NULL;
	m_lReloadScanDirWatchGeneration = 0;
	m_bReloadScanActive = false;
}

bool CSharedFileList::TrackScannedSharedFile(const CString& strFilePath, const CString& strFileName, time_t tUtcFileDate, uint64 uFileSize)
{
	const CString strPathKey(BuildNoCasePathKey(strFilePath));
	const CString strIdentity(BuildReloadFileIdentityKey(strFilePath, uFileSize, tUtcFileDate));
	bool bReloadScanActive = false;
	{
		CSingleLock reloadlock(&m_csReloadScan, TRUE);
		bReloadScanActive = m_bReloadScanActive;
		if (bReloadScanActive)
			m_mapReloadFoundFileIdentities.SetAt(strPathKey, strIdentity);
	}

	if (!bReloadScanActive) {
		{
			CSingleLock listlock(&m_mutWriteList, TRUE);
			void* pvFile = NULL;
			if (!m_mapSharedPathsNoCase.Lookup(strPathKey, pvFile))
				return false;
			if (pvFile != reinterpret_cast<void*>(1))
				return pvFile != NULL;
		}

		if (theApp.knownfiles != NULL) {
			CSingleLock duplicatesLock(&theApp.knownfiles->m_csDuplicatesLock, TRUE);
			if (theApp.knownfiles->IsDuplicatePathForSharedScan(strFileName, tUtcFileDate, uFileSize, strPathKey))
				return true;

			CSingleLock listlock(&m_mutWriteList, TRUE);
			void* pvFile = NULL;
			if (m_mapSharedPathsNoCase.Lookup(strPathKey, pvFile) && pvFile == reinterpret_cast<void*>(1)) {
				m_mapSharedPathsNoCase.RemoveKey(strPathKey);
				++m_uSharedPathCacheRevision;
				return false;
			}
			return pvFile != NULL;
		}
		return false;
	}

	CSingleLock listlock(&m_mutWriteList, TRUE);
	void* pvFile = NULL;
	if (!m_mapSharedPathsNoCase.Lookup(strPathKey, pvFile) || pvFile == NULL || pvFile == reinterpret_cast<void*>(1))
		return false;
	CKnownFile* pSharedFile = static_cast<CKnownFile*>(pvFile);
	return !pSharedFile->IsPartFile() && pSharedFile->GetFileName().CompareNoCase(strFileName) == 0
		&& IsReloadFileIdentityCurrent(strIdentity, static_cast<uint64>(pSharedFile->GetFileSize()), pSharedFile->GetUtcFileDate());
}


CKnownFile* CSharedFileList::FindKnownFileFromSharedCache(const CString& strFilePath, time_t tUtcFileDate, uint64 uFileSize) const
{
	return m_sharedCache.FindKnownFileByPath(strFilePath, tUtcFileDate, uFileSize);
}

bool CSharedFileList::IsCachedDuplicateSharedPath(const CString& strFilePath, time_t tUtcFileDate, uint64 uFileSize, const uchar* pucFileHash) const
{
	return m_sharedCache.IsDuplicatePath(strFilePath, tUtcFileDate, uFileSize, pucFileHash);
}

void CSharedFileList::RememberDuplicateSharedPath(const CString& strFilePath, const uchar* pucFileHash, time_t tUtcFileDate, uint64 uFileSize)
{
	m_sharedCache.RememberDuplicatePath(strFilePath, pucFileHash, tUtcFileDate, uFileSize);
}

void CSharedFileList::ResetSharedCacheRefresh()
{
	m_bSharedCacheRefreshStarted = false;
	m_aSharedCacheRefreshKeys.clear();
	m_uSharedCacheRefreshIndex = 0;
	m_sharedCache.CancelReplaceSharedRecords();
}

bool CSharedFileList::RefreshSharedCacheChunk(UINT uMaxFiles, UINT& uProcessed, INT_PTR& iRemaining)
{
	uProcessed = 0;
	iRemaining = 0;

	if (!m_bSharedCacheRefreshStarted) {
		m_sharedCache.BeginReplaceSharedRecords();
		{
			CSingleLock listlock(&m_mutWriteList, TRUE);
			m_aSharedCacheRefreshKeys.clear();
			m_aSharedCacheRefreshKeys.reserve(static_cast<size_t>(m_Files_map.GetCount()));
			for (const CKnownFilesMap::CPair* pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
				m_aSharedCacheRefreshKeys.push_back(CSKey(pair->key.m_key));
		}
		m_uSharedCacheRefreshIndex = 0;
		m_bSharedCacheRefreshStarted = true;
	}

	const UINT uLimit = (uMaxFiles != 0) ? uMaxFiles : 1;
	std::vector<CSharedCache::SSharedFileRecord> records;
	records.reserve(uLimit);
	{
		CSingleLock listlock(&m_mutWriteList, TRUE);
		while (uProcessed < uLimit && m_uSharedCacheRefreshIndex < m_aSharedCacheRefreshKeys.size()) {
			const CSKey& storedKey = m_aSharedCacheRefreshKeys[m_uSharedCacheRefreshIndex++];
			CKnownFile* pFile = NULL;
			m_Files_map.Lookup(CCKey(storedKey.m_key), pFile);
			++uProcessed;
			if (pFile == NULL || pFile->IsPartFile())
				continue;

			CSharedCache::SSharedFileRecord record;
			record.strFilePath = pFile->GetFilePath();
			record.tUtcFileDate = pFile->GetUtcFileDate();
			record.uFileSize = static_cast<uint64>(pFile->GetFileSize());
			md4cpy(record.aucFileHash, pFile->GetFileHash());
			records.push_back(record);
		}
	}

	m_sharedCache.AppendReplacementSharedRecords(records);
	iRemaining = (m_uSharedCacheRefreshIndex < m_aSharedCacheRefreshKeys.size()) ? 1 : 0;
	if (iRemaining == 0) {
		m_sharedCache.CommitReplaceSharedRecords();
		m_bSharedCacheRefreshCommitted = true;
		ResetSharedCacheRefresh();
	}
	return true;
}

void CSharedFileList::QueueSharedCachePersistenceSave()
{
	if (!m_bSharedCacheRefreshCommitted)
		return;

	m_bSharedCacheRefreshCommitted = false;
	theApp.ExecuteSavePersistenceFileCommand(CemuleApp::PersistenceCommandSaveSharedFiles, _T("shared-cache-refresh"));
}

bool CSharedFileList::IsReloadFoundFileCurrent(const CKnownFile* pFile) const
{
	if (pFile == NULL || pFile->IsPartFile())
		return true;

	return IsReloadFoundFileIdentityCurrent(pFile->GetFilePath(), static_cast<uint64>(pFile->GetFileSize()), pFile->GetUtcFileDate());
}

bool CSharedFileList::IsReloadFoundFileIdentityCurrent(const CString& strFilePath, uint64 uFileSize, time_t tUtcFileDate) const
{
	CSingleLock reloadlock(&m_csReloadScan, TRUE);
	if (!m_bReloadScanActive)
		return true;

	CString strFoundIdentity;
	if (!m_mapReloadFoundFileIdentities.Lookup(BuildNoCasePathKey(strFilePath), strFoundIdentity))
		return false;

	return IsReloadFileIdentityCurrent(strFoundIdentity, uFileSize, tUtcFileDate);
}

bool CSharedFileList::PruneReloadMissingSharedFilesChunk(UINT uMaxFiles, UINT& uProcessed, INT_PTR& iRemaining)
{
	uProcessed = 0;
	iRemaining = 0;

	LONG lDirWatchGeneration = 0;
	{
		CSingleLock reloadlock(&m_csReloadScan, TRUE);
		if (!m_bReloadScanActive)
			return true;
		lDirWatchGeneration = m_lReloadScanDirWatchGeneration;
	}

	if (lDirWatchGeneration != 0) {
		const LONG lCurrentGeneration = theApp.GetDirWatchChangeGeneration();
		if (lCurrentGeneration != lDirWatchGeneration) {
			AddDebugLogLine(DLP_LOW, false, _T("Shared files reload result ignored because directory watcher changed during scan. start=%ld current=%ld\n"), lDirWatchGeneration, lCurrentGeneration);
			EndReloadScan();
			m_sharedCache.CancelReplaceDuplicateRecords();
			m_bTreeReloadPending = true;
			m_uSharedFilesCompletionStep = SharedFilesCompletionAbortClearReloadSnapshot;
			return true;
		}
	}

	const UINT uLimit = (uMaxFiles != 0) ? uMaxFiles : 1;
	while (uProcessed < uLimit) {
		CSKey key;
		bool bHaveSnapshotFile = false;
		{
			CSingleLock listlock(&m_mutWriteList, TRUE);
			const CReloadLookupFilesMap::CPair* pair = m_ReloadLookupFiles_map.PGetFirstAssoc();
			if (pair != NULL) {
				key = pair->key;
				m_ReloadLookupFiles_map.RemoveKey(key);
				bHaveSnapshotFile = true;
			}
			iRemaining = m_ReloadLookupFiles_map.GetCount();
			if (iRemaining == 0)
				m_bReloadLookupSnapshotActive = false;
		}
		if (!bHaveSnapshotFile)
			break;

		++uProcessed;

		CString strLiveFilePath;
		uint64 uLiveFileSize = 0;
		time_t tLiveFileDate = 0;
		bool bLiveCompleteFile = false;
		{
			CSingleLock listlock(&m_mutWriteList, TRUE);
			CKnownFile* pLiveFile = NULL;
			if (m_Files_map.Lookup(CCKey(key.m_key), pLiveFile) && pLiveFile != NULL && !pLiveFile->IsPartFile()) {
				strLiveFilePath = pLiveFile->GetFilePath();
				uLiveFileSize = static_cast<uint64>(pLiveFile->GetFileSize());
				tLiveFileDate = pLiveFile->GetUtcFileDate();
				bLiveCompleteFile = true;
			}
		}

		if (bLiveCompleteFile && !IsReloadFoundFileIdentityCurrent(strLiveFilePath, uLiveFileSize, tLiveFileDate)) {
			CKnownFile* pFileToRemove = NULL;
			{
				CSingleLock listlock(&m_mutWriteList, TRUE);
				CKnownFile* pLiveFile = NULL;
				if (m_Files_map.Lookup(CCKey(key.m_key), pLiveFile) && pLiveFile != NULL && !pLiveFile->IsPartFile()
					&& pLiveFile->GetFilePath().CompareNoCase(strLiveFilePath) == 0
					&& static_cast<uint64>(pLiveFile->GetFileSize()) == uLiveFileSize
					&& IsFileDateEqual(pLiveFile->GetUtcFileDate(), tLiveFileDate))
				{
					pFileToRemove = pLiveFile;
				}
			}
			if (pFileToRemove != NULL)
				RemoveFile(pFileToRemove);
		}
	}

	if (iRemaining > 0)
		return true;

	EndReloadScan();
	return true;
}

bool CSharedFileList::StartSharedFilesCompletion()
{
	if (m_bSharedFilesCompletionActive)
		return true;
	if (!m_bSharedFilesCompletionPending)
		return false;
	m_bSharedFilesCompletionActive = true;
	m_bContinueFoundProcessing = true;
	m_bInFoundFilesProcessing = true;
	m_bCompletionKeywordPurgeStarted = false;
	m_uSharedFilesCompletionStep = SharedFilesCompletionPruneMissing;
	m_strCompletionKeywordPurgeCursor.Empty();
	m_bSharedCacheRefreshCommitted = false;
	ResetSharedCacheRefresh();
	return true;
}

bool CSharedFileList::ApplySharedFilesCompletionChunk(UINT& uProcessed, INT_PTR& iRemaining)
{
	uProcessed = 0;
	iRemaining = 0;
	const DWORD dwSliceStart = ::GetTickCount();

	while (m_bSharedFilesCompletionActive) {
		switch (m_uSharedFilesCompletionStep) {
			case SharedFilesCompletionPruneMissing:
			{
				UINT uStepProcessed = 0;
				INT_PTR iStepRemaining = 0;
				if (!PruneReloadMissingSharedFilesChunk(kSharedFilesCompletionReloadPrunePerSlice, uStepProcessed, iStepRemaining))
					return false;
				uProcessed += uStepProcessed;
				iRemaining = iStepRemaining;
				if (iStepRemaining > 0)
					return true;
				if (m_uSharedFilesCompletionStep == SharedFilesCompletionPruneMissing)
					m_uSharedFilesCompletionStep = SharedFilesCompletionLog;
				break;
			}

			case SharedFilesCompletionAbortClearReloadSnapshot:
			{
				UINT uStepProcessed = 0;
				INT_PTR iStepRemaining = 0;
				if (!ClearReloadLookupSnapshotChunk(kSharedFilesCompletionSnapshotClearPerSlice, uStepProcessed, iStepRemaining))
					return false;
				uProcessed += uStepProcessed;
				iRemaining = iStepRemaining;
				if (iStepRemaining > 0)
					return true;
				m_uSharedFilesCompletionStep = SharedFilesCompletionFinish;
				break;
			}

			case SharedFilesCompletionLog:
				if (waitingforhash_list.IsEmpty())
					AddLogLine(false, GetResString(_T("SHAREDFOUND")), m_Files_map.GetCount());
				else
					AddLogLine(false, GetResString(_T("SHAREDFOUNDHASHING")), m_Files_map.GetCount(), waitingforhash_list.GetCount());
				m_uSharedFilesCompletionStep = SharedFilesCompletionPurgeKeywords;
				break;

			case SharedFilesCompletionPurgeKeywords:
			{
				UINT uStepProcessed = 0;
				INT_PTR iStepRemaining = 0;
				if (!m_keywords->PurgeUnreferencedKeywordsChunk(m_strCompletionKeywordPurgeCursor, m_bCompletionKeywordPurgeStarted, kSharedFilesCompletionKeywordPurgePerSlice, uStepProcessed, iStepRemaining))
					return false;
				uProcessed += uStepProcessed;
				iRemaining = iStepRemaining;
				if (iStepRemaining > 0)
					return true;
				m_uSharedFilesCompletionStep = SharedFilesCompletionHashNextFile;
				break;
			}

			case SharedFilesCompletionHashNextFile:
				HashNextFile();
				m_uSharedFilesCompletionStep = SharedFilesCompletionClearReloadSnapshot;
				break;

			case SharedFilesCompletionClearReloadSnapshot:
			{
				UINT uStepProcessed = 0;
				INT_PTR iStepRemaining = 0;
				if (!ClearReloadLookupSnapshotChunk(kSharedFilesCompletionSnapshotClearPerSlice, uStepProcessed, iStepRemaining))
					return false;
				uProcessed += uStepProcessed;
				iRemaining = iStepRemaining;
				if (iStepRemaining > 0)
					return true;
				m_uSharedFilesCompletionStep = SharedFilesCompletionQueueListUpdate;
				break;
			}

			case SharedFilesCompletionQueueListUpdate:
				m_bSharedFilesModelChangedSinceListUpdate = false;
				theApp.QueueSharedFilesListChangedEvent(_T("shared-found-files-completed"));
				m_uSharedFilesCompletionStep = SharedFilesCompletionPruneWaiters;
				break;

			case SharedFilesCompletionPruneWaiters:
			{
				UINT uStepProcessed = 0;
				INT_PTR iStepRemaining = 0;
				if (theApp.uploadqueue != NULL && !theApp.uploadqueue->PruneWaitersForMissingSharedFilesChunk(kSharedFilesCompletionUploadWaiterPrunePerSlice, uStepProcessed, iStepRemaining))
					return false;
				uProcessed += uStepProcessed;
				iRemaining = iStepRemaining;
				if (iStepRemaining > 0)
					return true;
				m_uSharedFilesCompletionStep = SharedFilesCompletionRefreshSharedCache;
				break;
			}

			case SharedFilesCompletionRefreshSharedCache:
			{
				UINT uStepProcessed = 0;
				INT_PTR iStepRemaining = 0;
				if (!RefreshSharedCacheChunk(kSharedFilesCompletionSharedCachePerSlice, uStepProcessed, iStepRemaining))
					return false;
				uProcessed += uStepProcessed;
				iRemaining = iStepRemaining;
				if (iStepRemaining > 0)
					return true;
				m_uSharedFilesCompletionStep = SharedFilesCompletionFinish;
				break;
			}

			case SharedFilesCompletionFinish:
				FinishSharedFilesCompletion();
				return true;

			case SharedFilesCompletionIdle:
			default:
				FinishSharedFilesCompletion();
				return true;
		}

		if (uProcessed != 0 && theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetSharedFilesFound)) {
			iRemaining = 1;
			return true;
		}
	}

	return true;
}

void CSharedFileList::FinishSharedFilesCompletion()
{
	const bool bWasStartupScanCompletion = !m_bStartupScanCompleted;
	m_bSharedFilesCompletionActive = false;
	m_bSharedFilesCompletionPending = false;
	m_bCompletionKeywordPurgeStarted = false;
	m_uSharedFilesCompletionStep = SharedFilesCompletionIdle;
	m_strCompletionKeywordPurgeCursor.Empty();
	m_bContinueFoundProcessing = false;
	EndReloadScan();
	m_bStartupScanCompleted = true;
	ResetSharedCacheRefresh();
	if (m_sharedCache.CommitReplaceDuplicateRecords())
		m_bSharedCacheRefreshCommitted = true;
	QueueSharedCachePersistenceSave();
	if (bWasStartupScanCompletion) {
		theApp.BeginStartupKnown2IndexLoad();
		if (theApp.emuledlg != NULL)
			theApp.emuledlg->NotifyStartupSearchKnownTypesDependencyReady();
	}

	m_bInFoundFilesProcessing = false;
	if (m_bTreeReloadPending) {
		m_bTreeReloadPending = false;
		if (theApp.emuledlg && theApp.emuledlg->sharedfileswnd && ::IsWindow(theApp.emuledlg->sharedfileswnd->m_hWnd))
			theApp.emuledlg->sharedfileswnd->PostAutoReloadSharedFilesAsync(2);
	}

	NotifyShowFilesCount();
	if (theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL)
		theApp.emuledlg->sharedfileswnd->PostDeferredAutoReloadSharedFilesIfIdle();
}

bool CSharedFileList::IsReloading() const
{
	if (m_bContinueFoundProcessing || m_bSharedFilesCompletionActive)
		return true;
	if (m_bStartupScanDeferred)
		return true;
	if (m_searchThread != NULL) {
		if (!m_bStartupScanCompleted)
			return true;
		if (m_bReloadLookupSnapshotActive && (m_searchThread->IsBusy() || m_searchThread->HasQueuedFoundFiles()))
			return true;
	}
	return false;
}

bool CSharedFileList::HasActiveSharedFilesWork() const
{
	if (IsReloading())
		return true;
	if (m_searchThread != NULL && (m_searchThread->IsBusy() || m_searchThread->HasQueuedFoundFiles()))
		return true;
	return waitingforhash_list.GetCount() > 0 || currentlyhashing_list.GetCount() > 0;
}


void CSharedFileList::FindSharedFiles()
{
	m_sharedCache.BeginReplaceDuplicateRecords();

	if (theApp.downloadqueue) {
		bool bReloadScanActive = false;
		{
			CSingleLock reloadlock(&m_csReloadScan, TRUE);
			bReloadScanActive = m_bReloadScanActive;
		}

		if (!bReloadScanActive && !m_Files_map.IsEmpty()) {
			CSingleLock listlock(&m_mutWriteList);

			CCKey key;
			for (POSITION pos = m_Files_map.GetStartPosition(); pos != NULL;) {
				CKnownFile *cur_file;
				m_Files_map.GetNextAssoc(pos, key, cur_file);
				if (!cur_file->IsKindOf(RUNTIME_CLASS(CPartFile))
					|| theApp.downloadqueue->IsPartFile(cur_file)
					|| theApp.knownfiles->IsFilePtrInList(cur_file))
				{
					m_UnsharedFiles_map[CSKey(cur_file->GetFileHash())] = true;
					listlock.Lock();
					if (m_mapSharedPathsNoCase.RemoveKey(BuildNoCasePathKey(cur_file->GetFilePath())))
						++m_uSharedPathCacheRevision;
					m_Files_map.RemoveKey(key);
					listlock.Unlock();
				}
			}
		}

		// Startup download attach already shares ready part files incrementally. Avoid a second full scan while the overlay is active.
		const bool bStartupOverlayActive = theApp.emuledlg != NULL && theApp.emuledlg->IsStartupLoadingDialogVisible();
		if (!bStartupOverlayActive)
			theApp.downloadqueue->AddPartFilesToShare();
		else
			AddDebugLogLine(DLP_LOW, false, _T("Shared files startup scan skipped duplicate ready part-file attach while startup overlay is active.\n"));
	}

	// khaos::kmod+ Fix: Shared files loaded multiple times.
	const CString &tempDir(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
	CStringList sharedDirs;
	thePrefs.CopySharedDirectoryList(sharedDirs);
	CMapStringToPtr mapAddedDirs;
	const INT_PTR iExpectedDirCount = sharedDirs.GetCount() * 2 + thePrefs.GetCatCount() + 8;
	mapAddedDirs.InitHashTable(static_cast<UINT>((iExpectedDirCount > 257) ? iExpectedDirCount : 257));

#if defined(_BETA) || defined(_DEVBUILD)
	// Create the test file before adding the Incoming directory.
	const CString strBetaFilePath(tempDir + m_strBetaFileName);
	AsyncDiskWriteData betaFileData;
	betaFileData.strTempPath = strBetaFilePath + _T(".tmp");
	betaFileData.strFinalPath = strBetaFilePath;
	betaFileData.strLogName = m_strBetaFileName;
	betaFileData.strPayloadName = _T("beta-test-file");
	betaFileData.eReplacePolicy = AsyncDiskWriteReplaceFinal;
	AppendTCharSnapshotBytes(betaFileData.data, m_strBetaFileName); // Guarantees a different hash on different versions.
	AppendTCharSnapshotBytes(betaFileData.data, _T("\nThis file is automatically created by eMule Beta versions to help the developers testing and debugging the new features.")
		_T("\neMule will delete this file when exiting, otherwise you can remove this file at any time.")
		_T("\nThanks for beta testing eMule :)"));
	if (!CPartFileWriteThread::WriteDiskSnapshotNow(betaFileData, false))
		ASSERT(0);
#endif
	AddDirectory(tempDir, mapAddedDirs);
	// Queue only roots here. AutoShareSubdirs recursion is handled by the search thread.

	for (INT_PTR i = 1; i < thePrefs.GetCatCount(); ++i)
	{
		const CString &cat = thePrefs.GetCatPath(i);
		AddDirectory(cat, mapAddedDirs);
	}

	for (POSITION pos = sharedDirs.GetHeadPosition(); pos != NULL;)
	{
		const CString& root = sharedDirs.GetNext(pos);
		AddDirectory(root, mapAddedDirs);
	}

	// add all single shared files
	CStringList liSingleSharedFiles;
	{
		CSingleLock lock(&m_csShareRules, TRUE);
		CopyCStringList(liSingleSharedFiles, m_liSingleSharedFiles);
		bHaveSingleSharedFiles = !m_liSingleSharedFiles.IsEmpty();
	}
	for (POSITION pos = liSingleSharedFiles.GetHeadPosition(); pos != NULL;)
		CheckAndAddSingleFile(liSingleSharedFiles.GetNext(pos));

	// Files are yet to be found and therefore we skip the hash part
}

void CSharedFileList::AddFilesFromDirectory(const CString& rstrDirectory)
{
	CString strSearchPath(rstrDirectory);
	PathAddBackslash(strSearchPath.GetBuffer(strSearchPath.GetLength() + 1));
	strSearchPath.ReleaseBuffer();
	strSearchPath += _T("*");
	m_searchThread->BeginSearch(strSearchPath);
}

CString CSharedFileList::NormalizeDirectoryPath(const CString &strDirPath)
{
	CString sDir(strDirPath);
	if (!sDir.IsEmpty())
		slosh(sDir);
	return sDir;
}

int CSharedFileList::GetBestDirectoryRuleDepth(const CStringList &liDirs, const CString &sDirPath, bool bIncludeSubdirectories) const
{
	const CString sDir(NormalizeDirectoryPath(sDirPath));
	if (sDir.IsEmpty())
		return -1;

	int nBestDepth = -1;
	for (POSITION pos = liDirs.GetHeadPosition(); pos != NULL;) {
		const CString &sRule(liDirs.GetNext(pos));
		if (EqualPaths(sRule, sDir) || (bIncludeSubdirectories && IsSubDirectoryOf(sDir, sRule)))
			nBestDepth = max(nBestDepth, sRule.GetLength());
	}

	return nBestDepth;
}

bool CSharedFileList::AddSingleSharedFile(const CString &rstrFilePath, bool bNoUpdate)
{
	bool bExclude = false;
	bool bRulesChanged = false;
	{
		CSingleLock lock(&m_csShareRules, TRUE);
		bExclude = RemovePathNoCase(m_liSingleExcludedFiles, rstrFilePath);
		bRulesChanged = bExclude;
	}

	// check if we share this file in general
	bool bShared = ShouldBeShared(rstrFilePath.Left(rstrFilePath.ReverseFind(_T('\\'))), rstrFilePath, false);

	if (bShared && !bExclude)
		return false; // we should be sharing this file already
	if (!bShared) {
		CSingleLock lock(&m_csShareRules, TRUE);
		if (!ContainsPathNoCase(m_liSingleSharedFiles, rstrFilePath)) {
			m_liSingleSharedFiles.AddTail(rstrFilePath); // the directory is not shared, so we need a new entry
			bRulesChanged = true;
		}
		bHaveSingleSharedFiles = !m_liSingleSharedFiles.IsEmpty();
	}

	if (bRulesChanged)
		InvalidateShareRuleSnapshot();

	return bNoUpdate || CheckAndAddSingleFile(rstrFilePath);
}

bool CSharedFileList::CheckAndAddSingleFile(const CString& rstrFilePath)
{
	{
		CSingleLock lock(&m_csShareRules, TRUE);
		bHaveSingleSharedFiles = true;
	}
	m_bSharedFilesCompletionPending = true;
	m_searchThread->BeginSearch(rstrFilePath);
	// GUI updating needs to be done by caller
	return true; // This is probably not true anymore, but shouldn't hurt that much
}

bool CSharedFileList::SafeAddKFile(CKnownFile* toadd, bool bOnlyAdd, bool bHashingAlreadyDetached)
{
	const INT_PTR iHashingBeforeRemove = GetHashingCount();
	if (!bHashingAlreadyDetached) {
		const CString strPathKey = BuildNoCasePathKey(toadd->GetFilePath());
		if (!strPathKey.IsEmpty())
			RemoveWaitingFromHashingByPathKey(strPathKey);
		RemoveFromHashing(toadd, strPathKey);	// SLUGFILLER: SafeHash - hashed OK, remove from list if it was in
	}
	bool bAdded = AddFile(toadd);
	if (!bOnlyAdd) {
		if (bAdded && output) {
			// During runtime hashing, publish visible rows with one final list reload.
			const bool bBatchVisibleListAdd = iHashingBeforeRemove > 0 || output->HasPendingBulkAddListUpdate();
			output->AddFile(toadd, bBatchVisibleListAdd);
		}
		m_lastPublishED2KFlag = true;
	}
	FlushOutputBulkAddListUpdateIfIdle();
	NotifyShowFilesCount();
	return bAdded;
}

void CSharedFileList::RepublishFile(CKnownFile *pFile, bool bForce)
{
	// Completed part files are shared before the final move updates their path.
	UpdateSharedPathCache(pFile, NULL);

	CServer *pCurServer = server->GetCurrentServer();
	if (pCurServer && (bForce || (pCurServer->GetTCPFlags() & SRV_TCPFLG_COMPRESSION))) {
		m_lastPublishED2KFlag = true;
		pFile->SetPublishedED2K(false); // FIXME: this creates a wrong 'No' for the ed2k shared info in the listview until the file is shared again.
	}
}

bool CSharedFileList::AddFile(CKnownFile *pFile)
{
	ASSERT(pFile->GetFileIdentifier().HasExpectedMD4HashCount());
	ASSERT(!pFile->IsKindOf(RUNTIME_CLASS(CPartFile)) || !static_cast<CPartFile*>(pFile)->m_bMD4HashsetNeeded);
	ASSERT(!pFile->IsShellLinked() || ShouldBeShared(pFile->GetSharedDirectory(), NULL, false));
	CCKey key(pFile->GetFileHash());
	CKnownFile *pFileInMap;
	CKnownFile* pFileInDuplicatesList = theApp.knownfiles->IsOnDuplicates(pFile->GetFileName(), pFile->GetUtcFileDate(), pFile->GetFileSize());
	if (m_Files_map.Lookup(key, pFileInMap)) {
		if (!pFileInMap->IsKindOf(RUNTIME_CLASS(CPartFile)) || theApp.downloadqueue->IsPartFile(pFileInMap)) {
			if (pFileInMap->GetFilePath().CompareNoCase(pFile->GetFilePath()) != 0) { //is it actually really the same file in the same place we already share? if so don't bother too much
				LogWarning(GetResString(_T("ERR_DUPL_FILES2")), (LPCTSTR)EscPercent(pFileInMap->GetFilePath()), (LPCTSTR)EscPercent(pFile->GetFilePath()), (LPCTSTR)EscPercent(pFile->GetFileName()));
				if (pFileInDuplicatesList != NULL) {
					CSingleLock duplicatesLock(&theApp.knownfiles->m_csDuplicatesLock, TRUE);
					AddDebugLogLine(DLP_VERYLOW, false, _T("%hs: Already in duplicates list: %s %I64u \"%s\""), __FUNCTION__, (LPCTSTR)md4str(pFileInDuplicatesList->GetFileHash()), (uint64)pFileInDuplicatesList->GetFileSize(), (LPCTSTR)EscPercent(pFileInDuplicatesList->GetFileName()));
					const CString strOldFilePath(pFileInDuplicatesList->GetFilePath());
					pFileInDuplicatesList->SetPath(pFile->GetPath());
					pFileInDuplicatesList->SetFilePath(pFile->GetFilePath()); // Update the file path in the duplicates list
					pFileInDuplicatesList->SetSharedDirectory(pFile->GetSharedDirectory());
					UpdateSharedPathCacheByPath(strOldFilePath, pFileInDuplicatesList->GetFilePath());
				} else
					AddDebugLogLine(DLP_VERYLOW, false, _T("%hs: File already in known list: %s %I64u \"%s\""), __FUNCTION__, (LPCTSTR)md4str(pFileInMap->GetFileHash()), (uint64)pFileInMap->GetFileSize(), (LPCTSTR)EscPercent(pFileInMap->GetFileName()));
			} else
				DebugLog(_T("File shared twice, might have been a single shared file before - %s"), (LPCTSTR)EscPercent(pFileInMap->GetFilePath()));
		}
		return false;
	}
	m_UnsharedFiles_map.RemoveKey(CSKey(pFile->GetFileHash()));

	const CString strPathKey = BuildNoCasePathKey(pFile->GetFilePath());
	CSingleLock listlock(&m_mutWriteList, TRUE);
	m_Files_map[key] = pFile;
	m_mapSharedPathsNoCase[strPathKey] = pFile;
	++m_uSharedPathCacheRevision;
	listlock.Unlock();
	StoreWebSharedFileSnapshot(pFile);
	MarkSharedFilesModelChanged();
	if (!strPathKey.IsEmpty() && RemoveWaitingFromHashingByPathKey(strPathKey)) {
		FlushOutputBulkAddListUpdateIfIdle();
		NotifyShowFilesCount();
	}
	if (theApp.searchlist != NULL)
		theApp.searchlist->QueueKnownTypeRefreshForHash(pFile->GetFileHash());

	bool bKeywordsNeedUpdated = true;

	if (!pFile->IsPartFile() && !pFile->m_pCollection && CCollection::HasCollectionExtention(pFile->GetFileName())) {
		pFile->m_pCollection = new CCollection();
		if (!pFile->m_pCollection->InitCollectionFromFile(pFile->GetFilePath(), pFile->GetFileName())) {
			delete pFile->m_pCollection;
			pFile->m_pCollection = NULL;
		} else if (!pFile->m_pCollection->GetCollectionAuthorKeyString().IsEmpty()) {
			//If the collection has a key, resetting the file name will cause
			//the key to be added into the word list to be stored in Kad.
			pFile->SetFileName(pFile->GetFileName());
			//During the initial startup, shared files are not accessible
			//to SetFileName which will then not call AddKeywords.
			//But when it is accessible, we don't allow it to re-add them.
			if (theApp.sharedfiles)
				bKeywordsNeedUpdated = false;
		}
	}

	if (bKeywordsNeedUpdated)
		m_keywords->AddKeywords(pFile);

	pFile->SetLastSeen();

	theApp.knownfiles->m_nRequestedTotal += pFile->statistic.GetAllTimeRequests();
	theApp.knownfiles->m_nAcceptedTotal += pFile->statistic.GetAllTimeAccepts();
	theApp.knownfiles->m_nTransferredTotal += pFile->statistic.GetAllTimeTransferred();

	// If auto-share is enabled and this file resides under Incoming (or a category Incoming path),
	// request a forced tree reload so new subdirectories appear immediately in the Incoming branch.
	if (thePrefs.GetAutoShareSubdirs()) {
		CString dir = pFile->GetSharedDirectory();
		if (dir.IsEmpty())
			dir = pFile->GetPath();

		auto IsUnder = [&](const CString& root) -> bool {
			return !root.IsEmpty() && (EqualPaths(dir, root) || IsSubDirectoryOf(dir, root));
		};

		bool bUnderAuto = false;
		if (IsUnder(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR)))
			bUnderAuto = true;
		else {
			for (INT_PTR i = 1; i < thePrefs.GetCatCount(); ++i) {
				if (IsUnder(thePrefs.GetCatPath(i))) {
					bUnderAuto = true;
					break;
				}
			}
		}
	}

	return true;
}

void CSharedFileList::MarkSharedFilesModelChanged()
{
	m_bSharedFilesModelChangedSinceListUpdate = true;
	::InterlockedIncrement(&m_lSharedFilesModelRevision);
}

void CSharedFileList::FileHashingFinished(CKnownFile *file, LPCTSTR pszPathKey)
{
	// File hashing finished for a shared file (non-part file)
	// - Reading shared directories at startup and hashing files which were not found in known.met
	// - Reading shared directories during runtime (user hit Reload button, added a shared directory, ...)

	const bool bHashingDetached = RemoveFromHashing(file, pszPathKey);
	if (!bHashingDetached)
		TRACE(_T("Shared file hash completion did not match the active queue item.\n"));

	// If the user no longer wants to share this file, just drop it
	if (!ShouldBeShared(file->GetSharedDirectory(), file->GetFilePath(), false)) {
		if (!IsFilePtrInList(file) && !theApp.knownfiles->IsFilePtrInList(file))
			delete file; // delete only when not owned by shared or known lists
		return;
	}

	theApp.knownfiles->SafeAddKFile(file); // First, register with KnownFiles; it deduplicates and may already contain an instance

	// If already in the shared list, we are done (drop temp instance if not owned anywhere)
	CKnownFile* pLiveFile = GetLiveFileByID(file->GetFileHash());
	if (pLiveFile != NULL) {
		const bool bKnownDuplicate = !IsFilePtrInList(file) && theApp.knownfiles->IsFilePtrInList(file);
		if (bKnownDuplicate && output && (output->m_eFilter == FilterType::Duplicate || (output->m_eFilter == FilterType::History && thePrefs.GetFileHistoryShowDuplicate())))
			output->AddFile(file); // Insert only the new duplicate into the current duplicate/history view instead of rebuilding the whole list.
		if (bKnownDuplicate)
			UpdateSharedPathCacheByPath(NULL, file->GetFilePath());

		QueueMetaDataUpdate(bKnownDuplicate ? file : pLiveFile);

		if (!IsFilePtrInList(file) && !theApp.knownfiles->IsFilePtrInList(file))
			delete file;
		return;
	}

	if (SafeAddKFile(file, false, bHashingDetached))
		QueueMetaDataUpdate(file);
}

bool CSharedFileList::RemoveFile(CKnownFile* pFile, bool bDeleted, bool bWillReloadListLater)
{
	uchar aucRemovedFileHash[MDX_DIGEST_SIZE];
	md4cpy(aucRemovedFileHash, pFile->GetFileHash());
	const CString strRemovedFileName(pFile->GetFileName());
	const uint64 uRemovedFileSize = static_cast<uint64>(pFile->GetFileSize());
	const uint32 uRemovedAllTimeRequests = pFile->statistic.GetAllTimeRequests();
	const uint32 uRemovedAllTimeAccepts = pFile->statistic.GetAllTimeAccepts();
	const uint64 uRemovedAllTimeTransferred = pFile->statistic.GetAllTimeTransferred();

	CSingleLock listlock(&m_mutWriteList, TRUE);
	if (m_mapSharedPathsNoCase.RemoveKey(BuildNoCasePathKey(pFile->GetFilePath())))
		++m_uSharedPathCacheRevision;
	bool bResult = (m_Files_map.RemoveKey(CCKey(aucRemovedFileHash)) != FALSE);
	listlock.Unlock();
	if (bResult) {
		RemoveWebSharedFileSnapshot(aucRemovedFileHash);
		if (theApp.IsUiThread() && theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL && ::IsWindow(theApp.emuledlg->sharedfileswnd->sharedfilesctrl.GetSafeHwnd()))
			theApp.emuledlg->sharedfileswnd->sharedfilesctrl.RemoveFile(pFile, bDeleted, bWillReloadListLater);
	}

	theApp.DownloadValidator->RemoveFromMap(aucRemovedFileHash, strRemovedFileName, uRemovedFileSize);

	m_keywords->RemoveKeywords(pFile);
	CKnownFile* pPromotedFile = NULL;
	if (bResult && theApp.knownfiles != NULL)
		pPromotedFile = theApp.knownfiles->PromoteDuplicateForSharedFile(pFile);
	if (pPromotedFile != NULL)
		AddFile(pPromotedFile);

	if (bResult) {
		MarkSharedFilesModelChanged();
		theApp.QueueSharedFilesListChangedEvent(pPromotedFile != NULL ? _T("shared-duplicate-promoted") : _T("shared-file-removed"));
		NotifyShowFilesCount();
		if (pPromotedFile == NULL)
			m_UnsharedFiles_map[CSKey(aucRemovedFileHash)] = true;
		if (theApp.searchlist != NULL)
			theApp.searchlist->QueueKnownTypeRefreshForHash(aucRemovedFileHash);
		theApp.knownfiles->m_nRequestedTotal -= uRemovedAllTimeRequests;
		theApp.knownfiles->m_nAcceptedTotal -= uRemovedAllTimeAccepts;
		theApp.knownfiles->m_nTransferredTotal -= uRemovedAllTimeTransferred;
	}
	return bResult;
}

void CSharedFileList::Reload(LONG lDirWatchGeneration)
{
	ClearVolumeInfoCache();
	m_mapPseudoDirNames.RemoveAll();
	const bool bPreserveWaitingHashQueue = lDirWatchGeneration != 0 && GetHashingCount() > 0;
	m_mapHashingPathsNoCase.RemoveAll();
	if (bPreserveWaitingHashQueue) {
		for (POSITION pos = waitingforhash_list.GetHeadPosition(); pos != NULL;) {
			POSITION posCurrent = pos;
			UnknownFile_Struct* pWaitingFile = waitingforhash_list.GetNext(pos);
			if (pWaitingFile->strPathKey.IsEmpty())
				pWaitingFile->strPathKey = BuildNoCaseFilePathKey(pWaitingFile->strDirectory, pWaitingFile->strName);

			const CString strSharedDirectory = pWaitingFile->strSharedDirectory.IsEmpty() ? pWaitingFile->strDirectory : pWaitingFile->strSharedDirectory;
			if (!ShouldBeShared(strSharedDirectory, BuildUnknownFilePath(pWaitingFile), false)) {
				waitingforhash_list.RemoveAt(posCurrent);
				delete pWaitingFile;
				continue;
			}

			m_mapHashingPathsNoCase.SetAt(pWaitingFile->strPathKey, (void*)1);
		}
	} else {
		while (!waitingforhash_list.IsEmpty()) { // Delete waiting files; they will be re-added if still shared below.
			UnknownFile_Struct* pWaitingFile = waitingforhash_list.RemoveHead();
			delete pWaitingFile;
		}
	}
	for (POSITION pos = currentlyhashing_list.GetHeadPosition(); pos != NULL;) {
		UnknownFile_Struct* pHashingFile = currentlyhashing_list.GetNext(pos);
		if (pHashingFile->strPathKey.IsEmpty())
			pHashingFile->strPathKey = BuildNoCaseFilePathKey(pHashingFile->strDirectory, pHashingFile->strName);
		m_mapHashingPathsNoCase.SetAt(pHashingFile->strPathKey, (void*)1);
	}
	{
		CSingleLock lock(&m_csShareRules, TRUE);
		bHaveSingleSharedFiles = false;
	}
	InvalidateShareRuleSnapshot();

	// Abort any pending completion from an older reload before starting a new scan.
	m_bSharedFilesCompletionActive = false;
	m_bSharedFilesCompletionPending = false;
	m_bCompletionKeywordPurgeStarted = false;
	m_uSharedFilesCompletionStep = SharedFilesCompletionIdle;
	m_strCompletionKeywordPurgeCursor.Empty();
	m_bSharedCacheRefreshCommitted = false;
	ResetSharedCacheRefresh();
	m_sharedCache.CancelReplaceDuplicateRecords();
	m_bContinueFoundProcessing = false;
	m_bInFoundFilesProcessing = false;
	EndReloadScan();

	// Non-blocking reset of search work to avoid UI freezes when reloading often. Keep the worker thread alive and just clear its transient state and queues.
	if (m_searchThread == NULL)
		StartSearchThread();
	else
		m_searchThread->ResetWork();

	BeginReloadLookupSnapshot();
	BeginReloadScan(lDirWatchGeneration);
	m_bSharedFilesModelChangedSinceListUpdate = false;
	m_bSharedFilesCompletionPending = true;

	FindSharedFiles();
	if (m_searchThread && !m_searchThread->IsBusy() && StartSharedFilesCompletion()) {
		UINT uProcessed = 0;
		INT_PTR iRemaining = 0;
		if (!ApplySharedFilesCompletionChunk(uProcessed, iRemaining))
			FinishSharedFilesCompletion();
	}
}

void CSharedFileList::SetOutputCtrl(CSharedFilesCtrl *in_ctrl)
{
	output = in_ctrl;
	theApp.QueueSharedFilesListChangedEvent(_T("shared-output-attached"));
	HashNextFile();		// SLUGFILLER: SafeHash - if hashing not started yet, start it now
}


CSharedFileList::SWebSharedFileSnapshot::SWebSharedFileSnapshot()
	: dblFileCompletes(0.0)
	, uFileSize(0)
	, uTransferred(0)
	, uAllTimeTransferred(0)
	, uRequests(0)
	, uAllTimeRequests(0)
	, uAccepts(0)
	, uAllTimeAccepts(0)
	, nFilePriority(PR_NORMAL)
	, bPartFile(false)
	, bFileAutoPriority(false)
	, bDownloadable(false)
	, bReleasePriority(false)
{
}

CSharedFileList::SWebSharedFileSnapshot CSharedFileList::BuildWebSharedFileSnapshot(const CKnownFile *pFile)
{
	SWebSharedFileSnapshot snapshot;
	if (pFile == NULL)
		return snapshot;

	snapshot.strFileName = pFile->GetFileName();
	snapshot.strFilePath = pFile->GetFilePath();
	snapshot.uFileSize = static_cast<uint64>(pFile->GetFileSize());
	snapshot.uTransferred = pFile->statistic.GetTransferred();
	snapshot.uAllTimeTransferred = pFile->statistic.GetAllTimeTransferred();
	snapshot.uRequests = pFile->statistic.GetRequests();
	snapshot.uAllTimeRequests = pFile->statistic.GetAllTimeRequests();
	snapshot.uAccepts = pFile->statistic.GetAccepts();
	snapshot.uAllTimeAccepts = pFile->statistic.GetAllTimeAccepts();
	snapshot.strFileHash = md4str(pFile->GetFileHash());
	snapshot.bPartFile = pFile->IsPartFile();

	if (pFile->m_nCompleteSourcesCountLo == 0)
		snapshot.strFileCompletes.Format(_T("< %hu"), pFile->m_nCompleteSourcesCountHi);
	else if (pFile->m_nCompleteSourcesCountLo == pFile->m_nCompleteSourcesCountHi)
		snapshot.strFileCompletes.Format(_T("%hu"), pFile->m_nCompleteSourcesCountLo);
	else
		snapshot.strFileCompletes.Format(_T("%hu - %hu"), pFile->m_nCompleteSourcesCountLo, pFile->m_nCompleteSourcesCountHi);
	snapshot.dblFileCompletes = (pFile->m_nCompleteSourcesCountLo + pFile->m_nCompleteSourcesCountHi) / 2.0;

	LPCTSTR pszPriorityId = _T("PRIONORMAL");
	if (pFile->IsAutoUpPriority()) {
		switch (pFile->GetUpPriority()) {
		case PR_LOW:
			pszPriorityId = _T("PRIOAUTOLOW");
			break;
		case PR_HIGH:
			pszPriorityId = _T("PRIOAUTOHIGH");
			break;
		case PR_VERYHIGH:
			pszPriorityId = _T("PRIOAUTORELEASE");
			break;
		default:
			pszPriorityId = _T("PRIOAUTONORMAL");
		}
	} else {
		switch (pFile->GetUpPriority()) {
		case PR_VERYLOW:
			pszPriorityId = _T("PRIOVERYLOW");
			break;
		case PR_LOW:
			pszPriorityId = _T("PRIOLOW");
			break;
		case PR_HIGH:
			pszPriorityId = _T("PRIOHIGH");
			break;
		case PR_VERYHIGH:
			pszPriorityId = _T("PRIORELEASE");
			break;
		case PR_NORMAL:
		default:
			pszPriorityId = _T("PRIONORMAL");
		}
	}
	snapshot.strFilePriority = GetResString(pszPriorityId);
	snapshot.nFilePriority = pFile->GetUpPriority();
	snapshot.bFileAutoPriority = pFile->IsAutoUpPriority();
	snapshot.bDownloadable = !snapshot.bPartFile && (thePrefs.GetMaxWebUploadFileSizeMB() == 0 || snapshot.uFileSize < ((uint64)thePrefs.GetMaxWebUploadFileSizeMB()) * 1024 * 1024);
	snapshot.bReleasePriority = (pFile->GetUpPriority() == PR_VERYHIGH);
	return snapshot;
}

void CSharedFileList::StoreWebSharedFileSnapshot(const CKnownFile *pFile)
{
	if (pFile == NULL)
		return;
	SWebSharedFileSnapshot snapshot = BuildWebSharedFileSnapshot(pFile);
	if (snapshot.strFileHash.IsEmpty())
		return;

	CSingleLock lock(&m_csWebSharedFileSnapshots, TRUE);
	void* pvIndex = NULL;
	if (m_mapWebSharedFileSnapshotIndexes.Lookup(snapshot.strFileHash, pvIndex)) {
		const size_t uIndex = reinterpret_cast<size_t>(pvIndex) - 1;
		if (uIndex < m_webSharedFileSnapshots.size() && m_webSharedFileSnapshots[uIndex].strFileHash.CompareNoCase(snapshot.strFileHash) == 0) {
			m_webSharedFileSnapshots[uIndex] = snapshot;
			return;
		}
		m_mapWebSharedFileSnapshotIndexes.RemoveAll();
		for (size_t i = 0; i < m_webSharedFileSnapshots.size(); ++i)
			m_mapWebSharedFileSnapshotIndexes.SetAt(m_webSharedFileSnapshots[i].strFileHash, reinterpret_cast<void*>(i + 1));
		if (m_mapWebSharedFileSnapshotIndexes.Lookup(snapshot.strFileHash, pvIndex)) {
			const size_t uRebuiltIndex = reinterpret_cast<size_t>(pvIndex) - 1;
			if (uRebuiltIndex < m_webSharedFileSnapshots.size()) {
				m_webSharedFileSnapshots[uRebuiltIndex] = snapshot;
				return;
			}
		}
	}
	m_mapWebSharedFileSnapshotIndexes.SetAt(snapshot.strFileHash, reinterpret_cast<void*>(m_webSharedFileSnapshots.size() + 1));
	m_webSharedFileSnapshots.push_back(snapshot);
}

void CSharedFileList::RemoveWebSharedFileSnapshot(const uchar *fileHash)
{
	if (fileHash == NULL)
		return;
	const CString strHash(md4str(fileHash));
	CSingleLock lock(&m_csWebSharedFileSnapshots, TRUE);
	void* pvIndex = NULL;
	if (!m_mapWebSharedFileSnapshotIndexes.Lookup(strHash, pvIndex))
		return;
	const size_t uIndex = reinterpret_cast<size_t>(pvIndex) - 1;
	if (uIndex >= m_webSharedFileSnapshots.size() || m_webSharedFileSnapshots[uIndex].strFileHash.CompareNoCase(strHash) != 0) {
		m_mapWebSharedFileSnapshotIndexes.RemoveAll();
		for (size_t i = 0; i < m_webSharedFileSnapshots.size(); ++i)
			m_mapWebSharedFileSnapshotIndexes.SetAt(m_webSharedFileSnapshots[i].strFileHash, reinterpret_cast<void*>(i + 1));
		if (!m_mapWebSharedFileSnapshotIndexes.Lookup(strHash, pvIndex))
			return;
	}
	const size_t uRemoveIndex = reinterpret_cast<size_t>(pvIndex) - 1;
	if (uRemoveIndex >= m_webSharedFileSnapshots.size())
		return;
	m_webSharedFileSnapshots.erase(m_webSharedFileSnapshots.begin() + uRemoveIndex);
	m_mapWebSharedFileSnapshotIndexes.RemoveKey(strHash);
	for (size_t i = uRemoveIndex; i < m_webSharedFileSnapshots.size(); ++i)
		m_mapWebSharedFileSnapshotIndexes.SetAt(m_webSharedFileSnapshots[i].strFileHash, reinterpret_cast<void*>(i + 1));
}

void CSharedFileList::ClearWebSharedFileSnapshots()
{
	CSingleLock lock(&m_csWebSharedFileSnapshots, TRUE);
	m_webSharedFileSnapshots.clear();
	m_mapWebSharedFileSnapshotIndexes.RemoveAll();
}

bool CSharedFileList::CopyWebSharedFileSnapshot(const CString& strFileHash, SWebSharedFileSnapshot& snapshot) const
{
	snapshot = SWebSharedFileSnapshot();
	if (strFileHash.IsEmpty())
		return false;

	CSingleLock lock(&m_csWebSharedFileSnapshots, TRUE);
	void* pvIndex = NULL;
	CString strLookupHash(strFileHash);
	bool bFound = m_mapWebSharedFileSnapshotIndexes.Lookup(strLookupHash, pvIndex) != FALSE;
	if (!bFound && strFileHash.GetLength() == 32) {
		uchar fileHash[MDX_DIGEST_SIZE];
		if (DecodeBase16(strFileHash, strFileHash.GetLength(), fileHash, _countof(fileHash))) {
			strLookupHash = md4str(fileHash);
			bFound = m_mapWebSharedFileSnapshotIndexes.Lookup(strLookupHash, pvIndex) != FALSE;
		}
	}
	if (!bFound)
		return false;
	const size_t uIndex = reinterpret_cast<size_t>(pvIndex) - 1;
	if (uIndex >= m_webSharedFileSnapshots.size() || m_webSharedFileSnapshots[uIndex].strFileHash.CompareNoCase(strLookupHash) != 0)
		return false;
	snapshot = m_webSharedFileSnapshots[uIndex];
	return true;
}

void CSharedFileList::CopyWebSharedFileSnapshots(std::vector<SWebSharedFileSnapshot>& snapshots, size_t uMaxSnapshots) const
{
	CSingleLock lock(&m_csWebSharedFileSnapshots, TRUE);
	if (uMaxSnapshots == 0 || uMaxSnapshots >= m_webSharedFileSnapshots.size()) {
		snapshots = m_webSharedFileSnapshots;
		return;
	}
	snapshots.assign(m_webSharedFileSnapshots.begin(), m_webSharedFileSnapshots.begin() + uMaxSnapshots);
}

CSharedFileList::SOfferedFilePacketSnapshot::SOfferedFilePacketSnapshot()
	: uFileSize(0)
	, uFileRating(0)
	, uMetaDataVer(0)
	, bLargeFile(false)
	, bPartFile(false)
{
	md4clr(abyFileHash);
}

CSharedFileList::SOfferedFilePacketSnapshot::SOfferedFilePacketSnapshot(const SOfferedFilePacketSnapshot& src)
	: uFileSize(0)
	, uFileRating(0)
	, uMetaDataVer(0)
	, bLargeFile(false)
	, bPartFile(false)
{
	md4clr(abyFileHash);
	CopyFrom(src);
}

CSharedFileList::SOfferedFilePacketSnapshot::~SOfferedFilePacketSnapshot()
{
	Clear();
}

CSharedFileList::SOfferedFilePacketSnapshot& CSharedFileList::SOfferedFilePacketSnapshot::operator=(const SOfferedFilePacketSnapshot& src)
{
	if (this != &src)
		CopyFrom(src);
	return *this;
}

void CSharedFileList::SOfferedFilePacketSnapshot::Clear()
{
	for (size_t i = 0; i < aMetaTags.size(); ++i)
		delete aMetaTags[i];
	aMetaTags.clear();
	md4clr(abyFileHash);
	strFileName.Empty();
	uFileSize = 0;
	uFileRating = 0;
	uMetaDataVer = 0;
	bLargeFile = false;
	bPartFile = false;
}

void CSharedFileList::SOfferedFilePacketSnapshot::CopyFrom(const SOfferedFilePacketSnapshot& src)
{
	Clear();
	md4cpy(abyFileHash, src.abyFileHash);
	strFileName = src.strFileName;
	uFileSize = src.uFileSize;
	uFileRating = src.uFileRating;
	uMetaDataVer = src.uMetaDataVer;
	bLargeFile = src.bLargeFile;
	bPartFile = src.bPartFile;
	aMetaTags.reserve(src.aMetaTags.size());
	for (size_t i = 0; i < src.aMetaTags.size(); ++i)
		if (src.aMetaTags[i] != NULL)
			aMetaTags.push_back(new CTag(*src.aMetaTags[i]));
}

const CTag* CSharedFileList::SOfferedFilePacketSnapshot::GetTag(uint8 nName) const
{
	for (size_t i = 0; i < aMetaTags.size(); ++i)
		if (aMetaTags[i] != NULL && aMetaTags[i]->GetNameID() == nName)
			return aMetaTags[i];
	return NULL;
}

bool CSharedFileList::BuildOfferedFilePacketSnapshot(CKnownFile *cur_file, SOfferedFilePacketSnapshot& snapshot)
{
	if (cur_file == NULL)
		return false;

	snapshot.Clear();
	md4cpy(snapshot.abyFileHash, cur_file->GetFileHash());
	snapshot.strFileName = cur_file->GetFileName();
	snapshot.uFileSize = static_cast<uint64>(cur_file->GetFileSize());
	snapshot.uFileRating = cur_file->GetFileRating();
	snapshot.uMetaDataVer = cur_file->GetMetaDataVer();
	snapshot.bLargeFile = cur_file->IsLargeFile();
	snapshot.bPartFile = cur_file->IsPartFile();

	const CArray<CTag*, CTag*>& tags = cur_file->GetTags();
	snapshot.aMetaTags.reserve(static_cast<size_t>(tags.GetCount()));
	for (INT_PTR i = 0; i < tags.GetCount(); ++i)
		if (tags[i] != NULL)
			snapshot.aMetaTags.push_back(new CTag(*tags[i]));
	return true;
}


void CSharedFileList::SendListToServer()
{
	if (!server->IsConnected())
		return;
	
	std::vector<EServerBuddyMagicAnnounceEntry> magicEntries;
	uint32 uMagicEpoch = uInvalidEServerBuddyMagicAnnounceEpoch;
	if (ShouldIncludeEServerBuddyMagicFile()) {
		uMagicEpoch = CUpDownClient::GetEServerBuddyMagicEpoch();
		if (m_uLastEServerBuddyMagicAnnounceEpoch != uMagicEpoch)
			BuildEServerBuddyMagicAnnounceEntries(magicEntries, uMagicEpoch);
	}
	if (m_Files_map.IsEmpty() && magicEntries.empty())
		return;

	CServer* pCurServer = server->GetCurrentServer();
	CSafeMemFile files(1024);

	// add to packet
	uint32 limit = pCurServer ? pCurServer->GetSoftFiles() : 0;
	if (limit == 0 || limit > 200)
		limit = 200;
	if (magicEntries.size() > limit)
		magicEntries.resize(limit);

	const uint32 uMagicEntries = static_cast<uint32>(magicEntries.size());
	uint32 uRealLimit = limit;
	if (uRealLimit > uMagicEntries)
		uRealLimit -= uMagicEntries;
	else
		uRealLimit = 0;

	std::vector<SOfferedFilePacketSnapshot> m_Ed2kPublishListVector;
	std::vector<CKnownFile*> m_Ed2kPublishedFileVector;
	m_Ed2kPublishListVector.reserve(m_Files_map.GetCount());
	m_Ed2kPublishedFileVector.reserve(m_Files_map.GetCount());

	// These loops will add files sorted by their real priorities (GetRealPrio) until the vector size reaches to the limit.
	for (int prio = 4; prio >= 0 && m_Ed2kPublishListVector.size() < uRealLimit; --prio)
		for (const CKnownFilesMap::CPair* pair = m_Files_map.PGetFirstAssoc(); pair != NULL && m_Ed2kPublishListVector.size() < uRealLimit; pair = m_Files_map.PGetNextAssoc(pair)) // Fill vector with the map items
			if (!pair->value->GetPublishedED2K() && GetRealPrio(pair->value->GetUpPriority()) == prio && (!pair->value->IsLargeFile() || (pCurServer != NULL && pCurServer->SupportsLargeFilesTCP()))) {
				SOfferedFilePacketSnapshot snapshot;
				if (BuildOfferedFilePacketSnapshot(pair->value, snapshot)) {
					m_Ed2kPublishListVector.push_back(snapshot);
					m_Ed2kPublishedFileVector.push_back(pair->value);
				}
			}

	bool bSentMagicOnly = false;
	if ((uint32)m_Ed2kPublishListVector.size() < uRealLimit) {
		limit = (uint32)m_Ed2kPublishListVector.size();
		if (limit == 0 && uMagicEntries == 0) {
			m_lastPublishED2KFlag = false;
			return;
		}
		bSentMagicOnly = (limit == 0 && uMagicEntries > 0);
		limit += uMagicEntries;
	}
	files.WriteUInt32(limit);

	for (size_t i = 0; i < magicEntries.size(); ++i)
		CreateEServerBuddyMagicFilePacket(files, magicEntries[i]);

	uint32 count = limit - uMagicEntries;

	for (uint32 i = 0; i < count && i < m_Ed2kPublishListVector.size(); ++i) {
		CreateOfferedFilePacket(m_Ed2kPublishListVector[i], files, pCurServer);
		if (i < m_Ed2kPublishedFileVector.size() && m_Ed2kPublishedFileVector[i] != NULL)
			m_Ed2kPublishedFileVector[i]->SetPublishedED2K(true);
	}

	Packet* packet = new Packet(files);
	packet->opcode = OP_OFFERFILES;
	// compress packet
	//   - this kind of data is highly compressible (N * (1 MD4 and at least 3 string meta data tags and 1 integer meta data tag))
	//   - the min. amount of data needed for one published file is ~100 bytes
	//   - this function is called once when connecting to a server and when a file becomes shareable - so, it's called rarely.
	//   - if the compressed size is still >= the original size, we send the uncompressed packet
	// therefore we always try to compress the packet
	if (pCurServer && pCurServer->GetTCPFlags() & SRV_TCPFLG_COMPRESSION) {
		UINT uUncomprSize = packet->size;
		packet->PackPacket();
		if (thePrefs.GetDebugServerTCPLevel() > 0)
			Debug(_T(">>> Sending OP_OfferFiles(compressed); uncompr size=%u  compr size=%u  files=%u\n"), uUncomprSize, packet->size, limit);
	}
	else if (thePrefs.GetDebugServerTCPLevel() > 0)
		Debug(_T(">>> Sending OP_OfferFiles; size=%u  files=%u\n"), packet->size, limit);

	theStats.AddUpDataOverheadServer(packet->size);
	if (thePrefs.GetVerbose())
		AddDebugLogLine(false, _T("Server, Sendlist: Packet size:%u"), packet->size);
	server->SendPacket(packet);
	if (uMagicEntries > 0)
		m_uLastEServerBuddyMagicAnnounceEpoch = uMagicEpoch;
	if (bSentMagicOnly)
		m_lastPublishED2KFlag = false;
}

void CSharedFileList::ClearED2KPublishInfo()
{
	m_lastPublishED2KFlag = true;
	m_uLastEServerBuddyMagicAnnounceEpoch = uInvalidEServerBuddyMagicAnnounceEpoch;
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		pair->value->SetPublishedED2K(false);
}

void CSharedFileList::ClearKadSourcePublishInfo()
{
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		pair->value->SetLastPublishTimeKadSrc(0, 0);
}

bool CSharedFileList::CanClientBrowseSharedFile(const CKnownFile *file, const CUpDownClient *client) const
{
	if (file == NULL)
		return false;

	if (thePrefs.CanSeeShares() == vsfaNobody)
		return false;
	if (thePrefs.CanSeeShares() == vsfaFriends && (client == NULL || !client->IsFriend()))
		return false;
	if (client == NULL)
		return true;

	int iPermission = file->GetPermissions();
	if (iPermission < 0)
		iPermission = thePrefs.GetSharePermissions();

	switch (iPermission) {
	case PERM_ALL:
		return true;
	case PERM_FRIENDS:
		return client->IsFriend();
	case PERM_NOONE:
		return false;
	default:
		return true;
	}
}

void CSharedFileList::CreateOfferedFilePacket(const SOfferedFilePacketSnapshot& snapshot, CSafeMemFile &files
	, CServer *pServer, CUpDownClient *pClient)
{
	UINT uEmuleVer = (pClient && pClient->IsEmuleClient()) ? pClient->GetVersion() : 0;

	// NOTE: This function is used for creating the offered file packet for Servers _and_ for Clients.
	files.WriteHash16(snapshot.abyFileHash);

	// *) This function is used for offering files to the local server and for sending
	//    shared files to some other client. In each case we send our IP+Port only, if
	//    we have a HighID.
	// *) Newer eservers also support 2 special IP+port values which are used to hold basic file status info.
	uint32 nClientID = 0;
	uint16 nClientPort = 0;
	if (pServer) {
		// we use the 'TCP-compression' server feature flag as indicator for a 'newer' server.
		if (pServer->GetTCPFlags() & SRV_TCPFLG_COMPRESSION) {
			if (snapshot.bPartFile) {
				// publishing an incomplete file
				nClientID = 0xFCFCFCFC;
				nClientPort = 0xFCFC;
			} else {
				// publishing a complete file
				nClientID = 0xFBFBFBFB;
				nClientPort = 0xFBFB;
			}
		} else {
			// check eD2K ID state
			if (theApp.serverconnect->IsConnected() && !theApp.serverconnect->IsLowID()) {
				nClientID = theApp.GetID();
				nClientPort = thePrefs.GetPort();
			}
		}
	} else if (theApp.IsConnected() && !theApp.IsFirewalled()) {
		nClientID = theApp.GetID();
		nClientPort = thePrefs.GetPort();
	}
	files.WriteUInt32(nClientID);
	files.WriteUInt16(nClientPort);

	CSimpleArray<CTag*> tags;

	tags.Add(new CTag(FT_FILENAME, snapshot.strFileName));

	const uint64 uFileSize = snapshot.uFileSize;
	if (!snapshot.bLargeFile)
		tags.Add(new CTag(FT_FILESIZE, LODWORD(uFileSize)));
	else {
		// we send two 32-bit tags to servers, but a 64-bit tag to other clients.
		if (pServer != NULL) {
			if (!pServer->SupportsLargeFilesTCP()) {
				ASSERT(0);
				tags.Add(new CTag(FT_FILESIZE, 0, false));
			} else {
				tags.Add(new CTag(FT_FILESIZE, LODWORD(uFileSize)));
				tags.Add(new CTag(FT_FILESIZE_HI, HIDWORD(uFileSize)));
			}
		} else if (pClient != NULL) {
			if (!pClient->SupportsLargeFiles()) {
				ASSERT(0);
				tags.Add(new CTag(FT_FILESIZE, 0, false));
			} else
				tags.Add(new CTag(FT_FILESIZE, uFileSize, true));
		}
	}

	// eserver 17.6+ supports eMule file rating tag. There is no TCP-capabilities bit available
	// to determine whether the server is really supporting it -- this is by intention (lug).
	// That's why we always send it.
	if (snapshot.uFileRating) {
		uint32 uRatingVal = snapshot.uFileRating;
		if (pClient) {
			// eserver is sending the rating which it received in a different format (see
			// 'CSearchFile::CSearchFile'). If we are creating the packet for other client
			// we must use eserver's format.
			uRatingVal *= (255 / 5/*RatingExcellent*/);
		}
		tags.Add(new CTag(FT_FILERATING, uRatingVal));
	}

	// NOTE: Archives and CD-Images are published+searched with file type "Pro"
	bool bAddedFileType = false;
	if (pServer && (pServer->GetTCPFlags() & SRV_TCPFLG_TYPETAGINTEGER)) {
		// Send integer file type tags to newer servers
		EED2KFileType eFileType = GetED2KFileTypeSearchID(GetED2KFileTypeID(snapshot.strFileName));
		if (eFileType >= ED2KFT_AUDIO && eFileType <= ED2KFT_CDIMAGE) {
			tags.Add(new CTag(FT_FILETYPE, (UINT)eFileType));
			bAddedFileType = true;
		}
	}
	if (!bAddedFileType) {
		// Send string file type tags to:
		//	- newer servers, in case there is no integer type available for the file type (e.g. emulecollection)
		//	- older servers
		//	- all clients
		const CString &strED2KFileType(GetED2KFileTypeSearchTerm(GetED2KFileTypeID(snapshot.strFileName), true));
		if (!strED2KFileType.IsEmpty())
			tags.Add(new CTag(FT_FILETYPE, strED2KFileType));
	}

	// eserver 16.4+ does not need the FT_FILEFORMAT tag at all nor does any eMule client. This tag
	// was used for older (very old) eDonkey servers only. -> We send it only to non-eMule clients.
	if (pServer == NULL && uEmuleVer == 0) {
		LPCTSTR pDot = ::PathFindExtension(snapshot.strFileName);
		if (*pDot && pDot[1]) {
			CString strExt(pDot + 1); //skip the dot
			tags.Add(new CTag(FT_FILEFORMAT, strExt.MakeLower())); // file extension without a "."
		}
	}

	// only send verified meta data to servers/clients
	if (snapshot.uMetaDataVer > 0) {
		static const struct
		{
			bool	bSendToServer;
			uint8	nName;
			uint8	nED2KType;
			LPCSTR	pszED2KName;
		} _aMetaTags[] =
		{
			// Artist, Album and Title are disabled because they should be already part of the filename
			// and would therefore be redundant information sent to the servers. and the servers count the
			// amount of sent data!
			{ false, FT_MEDIA_ARTIST,	TAGTYPE_STRING, FT_ED2K_MEDIA_ARTIST },
			{ false, FT_MEDIA_ALBUM,	TAGTYPE_STRING, FT_ED2K_MEDIA_ALBUM },
			{ false, FT_MEDIA_TITLE,	TAGTYPE_STRING, FT_ED2K_MEDIA_TITLE },
			{ true,  FT_MEDIA_LENGTH,	TAGTYPE_STRING, FT_ED2K_MEDIA_LENGTH },
			{ true,  FT_MEDIA_BITRATE,	TAGTYPE_UINT32, FT_ED2K_MEDIA_BITRATE },
			{ true,  FT_MEDIA_CODEC,	TAGTYPE_STRING, FT_ED2K_MEDIA_CODEC }
		};
		for (unsigned i = 0; i < _countof(_aMetaTags); ++i) {
			if (pServer != NULL && !_aMetaTags[i].bSendToServer)
				continue;
			const CTag *pTag = snapshot.GetTag(_aMetaTags[i].nName);
			if (pTag != NULL) {
				// skip string tags with empty string values
				if (pTag->IsStr() && pTag->GetStr().IsEmpty())
					continue;

				// skip integer tags with '0' values
				if (pTag->IsInt() && pTag->GetInt() == 0)
					continue;

				if (_aMetaTags[i].nED2KType == TAGTYPE_STRING && pTag->IsStr()) {
					if (pServer && (pServer->GetTCPFlags() & SRV_TCPFLG_NEWTAGS))
						tags.Add(new CTag(_aMetaTags[i].nName, pTag->GetStr()));
					else
						tags.Add(new CTag(_aMetaTags[i].pszED2KName, pTag->GetStr()));
				} else if (_aMetaTags[i].nED2KType == TAGTYPE_UINT32 && pTag->IsInt()) {
					if (pServer && (pServer->GetTCPFlags() & SRV_TCPFLG_NEWTAGS))
						tags.Add(new CTag(_aMetaTags[i].nName, pTag->GetInt()));
					else
						tags.Add(new CTag(_aMetaTags[i].pszED2KName, pTag->GetInt()));
				} else if (_aMetaTags[i].nName == FT_MEDIA_LENGTH && pTag->IsInt()) {
					ASSERT(_aMetaTags[i].nED2KType == TAGTYPE_STRING);
					// All 'eserver' versions and eMule versions >= 0.42.4 support the media length tag with type 'integer'
					if ((pServer != NULL && (pServer->GetTCPFlags() & SRV_TCPFLG_COMPRESSION))
						|| uEmuleVer >= MAKE_CLIENT_VERSION(0, 42, 4))
					{
						if (pServer && (pServer->GetTCPFlags() & SRV_TCPFLG_NEWTAGS))
							tags.Add(new CTag(_aMetaTags[i].nName, pTag->GetInt()));
						else
							tags.Add(new CTag(_aMetaTags[i].pszED2KName, pTag->GetInt()));
					} else
						tags.Add(new CTag(_aMetaTags[i].pszED2KName, SecToTimeLength(pTag->GetInt())));
				} else
					ASSERT(0);
			}
		}
	}

	EUTF8str eStrEncode;
	if ((pServer && (pServer->GetTCPFlags() & SRV_TCPFLG_UNICODE)) || !pClient || pClient->GetUnicodeSupport())
		eStrEncode = UTF8strRaw;
	else
		eStrEncode = UTF8strNone;

	files.WriteUInt32(tags.GetSize());
	for (int i = 0; i < tags.GetSize(); ++i) {
		const CTag *pTag = tags[i];
		if (pServer && (pServer->GetTCPFlags() & SRV_TCPFLG_NEWTAGS) || (uEmuleVer >= MAKE_CLIENT_VERSION(0, 42, 7)))
			pTag->WriteNewEd2kTag(files, eStrEncode);
		else
			pTag->WriteTagToFile(files, eStrEncode);
		delete pTag;
	}
}

void CSharedFileList::CreateOfferedFilePacket(CKnownFile *cur_file, CSafeMemFile &files, CServer *pServer, CUpDownClient *pClient)
{
	SOfferedFilePacketSnapshot snapshot;
	if (BuildOfferedFilePacketSnapshot(cur_file, snapshot))
		CreateOfferedFilePacket(snapshot, files, pServer, pClient);
}

// -khaos--+++> New param:  pbytesLargest, pointer to uint64.
//				Various other changes to accommodate our new statistic...
//				Point of this is to find the largest file currently shared.
uint64 CSharedFileList::GetDatasize(uint64 &pbytesLargest) const
{
	pbytesLargest = 0;
	uint64 fsize = 0;

	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair)) {
		uint64 cur_size = (uint64)pair->value->GetFileSize();
		fsize += cur_size;
		// -khaos--+++> If this file is bigger than all the others...well duh.
		if (cur_size > pbytesLargest)
			pbytesLargest = cur_size;
	}
	return fsize;
}

CKnownFile* CSharedFileList::GetLiveFileByID(const uchar *hash) const
{
	if (hash) {
		CKnownFile *found_file;
		if (m_Files_map.Lookup(CCKey(hash), found_file))
			return found_file;
	}
	return NULL;
}

CKnownFile* CSharedFileList::GetFileByID(const uchar *hash) const
{
	CKnownFile* pFile = GetLiveFileByID(hash);
	if (pFile != NULL || !IsReloading() || hash == NULL)
		return pFile;

	CKnownFile* pFallback = NULL;
	if (m_ReloadLookupFiles_map.Lookup(CSKey(hash), pFallback))
		return pFallback;

	return NULL;
}

CKnownFile* CSharedFileList::GetFileByIdentifier(const CFileIdentifierBase &rFileIdent, bool bStrict) const
{
	CKnownFile *pResult;
	if (m_Files_map.Lookup(CCKey(rFileIdent.GetMD4Hash()), pResult))
		if (bStrict) {
			if (pResult->GetFileIdentifier().CompareStrict(rFileIdent))
				return pResult;
		} else if (pResult->GetFileIdentifier().CompareRelaxed(rFileIdent))
			return pResult;
	return NULL;
}

CKnownFile* CSharedFileList::GetFileByIndex(INT_PTR index) const // slow
{
	ASSERT(!index || (index > 0 && index < m_Files_map.GetCount()));
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		if (--index < 0)
			return pair->value;
	return NULL;
}

CKnownFile* CSharedFileList::GetFileNext(POSITION &pos) const
{
	CKnownFile *cur_file = NULL;
	if (m_Files_map.IsEmpty()) //XP was crashing without this
		pos = NULL;
	else if (pos != NULL) {
		CCKey bufKey;
		m_Files_map.GetNextAssoc(pos, bufKey, cur_file);
	}
	return cur_file;
}

CKnownFile* CSharedFileList::GetFileByAICH(const CAICHHash &rHash) const // slow
{
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		if (pair->value->GetFileIdentifierC().HasAICHHash() && pair->value->GetFileIdentifierC().GetAICHHash() == rHash)
			return pair->value;

	return NULL;
}

bool CSharedFileList::IsFilePtrInList(const CKnownFile *file) const
{
	// Lookup for the file hash (which is fast) if the map has it and return true if found.
	if (file && file == GetLiveFileByID(file->GetFileHash()))
		return true;

	if (file)
		for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
			if (file == pair->value)
				return true;

	return false;
}

void CSharedFileList::NotifyShowFilesCount() const
{
	if (theApp.IsClosing() || theApp.emuledlg == NULL || theApp.emuledlg->sharedfileswnd == NULL)
		return;

	theApp.emuledlg->sharedfileswnd->PostShowFilesCountAsync();
	theApp.emuledlg->PostStartupOverlayRefresh();
}

void CSharedFileList::HashNextFile()
{
	// SLUGFILLER: SafeHash
	if (!::IsWindow(theApp.emuledlg->m_hWnd))	// wait for the dialog to open
		return;
	NotifyShowFilesCount();
	if (!currentlyhashing_list.IsEmpty())	// one hash at a time
		return;
	// SLUGFILLER: SafeHash
	bool bSkippedStaleHashFile = false;
	while (!waitingforhash_list.IsEmpty()) {
		UnknownFile_Struct *nextfile = waitingforhash_list.RemoveHead();
		if (nextfile->strPathKey.IsEmpty())
			nextfile->strPathKey = BuildNoCaseFilePathKey(nextfile->strDirectory, nextfile->strName);

		const CString strNextFilePath = BuildUnknownFilePath(nextfile);
		if (IsAlreadySharedByPathNoCase(strNextFilePath)) {
			m_mapHashingPathsNoCase.RemoveKey(nextfile->strPathKey);
			delete nextfile;
			bSkippedStaleHashFile = true;
			continue;
		}

		const CString strSharedDirectory = nextfile->strSharedDirectory.IsEmpty() ? nextfile->strDirectory : nextfile->strSharedDirectory;
		if (!ShouldBeShared(strSharedDirectory, strNextFilePath, false)) {
			m_mapHashingPathsNoCase.RemoveKey(nextfile->strPathKey);
			delete nextfile;
			bSkippedStaleHashFile = true;
			continue;
		}

		if (bSkippedStaleHashFile)
			NotifyShowFilesCount();

		SharedFileHashResult_Struct* pHashResult = NULL;
		CAddFileThread *addfilethread = NULL;
		try {
			pHashResult = new SharedFileHashResult_Struct;
			pHashResult->strName = nextfile->strName;
			pHashResult->strDirectory = nextfile->strDirectory;
			pHashResult->strPathKey = nextfile->strPathKey;

			addfilethread = static_cast<CAddFileThread*>(AfxBeginThread(RUNTIME_CLASS(CAddFileThread), THREAD_PRIORITY_BELOW_NORMAL, 0, CREATE_SUSPENDED));
			if (addfilethread != NULL) {
				addfilethread->SetValues(this, nextfile->strDirectory, nextfile->strName, nextfile->strSharedDirectory, NULL, m_bStartupScanCompleted);
				addfilethread->SetSharedHashResult(pHashResult);
				pHashResult = NULL;
				currentlyhashing_list.AddTail(nextfile);	// SLUGFILLER: SafeHash - keep track
			}
		} catch (CException* ex) {
			ex->Delete();
			if (addfilethread != NULL) {
				addfilethread->m_bAutoDelete = FALSE;
				delete addfilethread;
				addfilethread = NULL;
			}
		} catch (...) {
			if (addfilethread != NULL) {
				addfilethread->m_bAutoDelete = FALSE;
				delete addfilethread;
				addfilethread = NULL;
			}
		}
		delete pHashResult;
		if (addfilethread == NULL) {
			m_mapHashingPathsNoCase.RemoveKey(nextfile->strPathKey);
			delete nextfile;
			bSkippedStaleHashFile = true;
			continue;
		}
		if (addfilethread->ResumeThread() == static_cast<DWORD>(-1)) {
			addfilethread->m_bAutoDelete = FALSE;
			delete addfilethread;
			UnknownFile_Struct* pFailedFile = currentlyhashing_list.RemoveTail();
			ASSERT(pFailedFile == nextfile);
			m_mapHashingPathsNoCase.RemoveKey(nextfile->strPathKey);
			delete pFailedFile;
			bSkippedStaleHashFile = true;
			continue;
		}
		NotifyShowFilesCount();
		// SLUGFILLER: SafeHash - nextfile deletion is handled elsewhere
		//delete nextfile;
		return;
	}

	if (bSkippedStaleHashFile)
		NotifyShowFilesCount();

	FlushOutputBulkAddListUpdateIfIdle();
}

void CSharedFileList::QueueSharedFilesReloadIfModelChanged(LPCTSTR pszStage)
{
	if (!m_bSharedFilesModelChangedSinceListUpdate)
		return;

	m_bSharedFilesModelChangedSinceListUpdate = false;
	theApp.QueueSharedFilesListChangedEvent(pszStage != NULL ? pszStage : _T("shared-files-model-changed"));
}

void CSharedFileList::FlushOutputBulkAddListUpdateIfIdle()
{
	if (GetHashingCount() != 0)
		return;


	const bool bHadVisibleBulkAdd = output != NULL && output->HasPendingBulkAddListUpdate();
	if (bHadVisibleBulkAdd)
		output->FlushBulkAddListUpdate(LSF_SELECTION);
	else
		QueueSharedFilesReloadIfModelChanged(_T("shared-hash-queue-drained"));

	if (theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL)
		theApp.emuledlg->sharedfileswnd->PostDeferredAutoReloadSharedFilesIfIdle();
}

// SLUGFILLER: SafeHash
bool CSharedFileList::IsHashing(const CString &rstrDirectory, const CString &rstrName)
{
	return IsHashingByPathKey(BuildNoCaseFilePathKey(rstrDirectory, rstrName));
}

bool CSharedFileList::IsHashingByPathKey(LPCTSTR pszPathKey)
{
	void* pv = NULL;
	return pszPathKey != NULL && m_mapHashingPathsNoCase.Lookup(pszPathKey, pv) != FALSE;
}

bool CSharedFileList::RemoveWaitingFromHashingByPathKey(LPCTSTR pszPathKey)
{
	if (pszPathKey == NULL || *pszPathKey == _T('\0'))
		return false;

	bool bRemoved = false;
	for (POSITION pos = waitingforhash_list.GetHeadPosition(); pos != NULL;) {
		POSITION posLast = pos;
		UnknownFile_Struct *pFile = waitingforhash_list.GetNext(pos);
		if (pFile->strPathKey.IsEmpty())
			pFile->strPathKey = BuildNoCaseFilePathKey(pFile->strDirectory, pFile->strName);
		if (pFile->strPathKey.CompareNoCase(pszPathKey) == 0) {
			m_mapHashingPathsNoCase.RemoveKey(pFile->strPathKey);
			waitingforhash_list.RemoveAt(posLast);
			delete pFile;
			bRemoved = true;
		}
	}
	return bRemoved;
}

bool CSharedFileList::RemoveCurrentHashingByPathKey(LPCTSTR pszPathKey, LPCTSTR pszDirectory, LPCTSTR pszName)
{
	POSITION posMatch = NULL;
	for (POSITION pos = currentlyhashing_list.GetHeadPosition(); pos != NULL;) {
		POSITION posCurrent = pos;
		UnknownFile_Struct* pFile = currentlyhashing_list.GetNext(pos);
		if (pFile->strPathKey.IsEmpty())
			pFile->strPathKey = BuildNoCaseFilePathKey(pFile->strDirectory, pFile->strName);
		if ((pszPathKey != NULL && *pszPathKey != _T('\0') && pFile->strPathKey.CompareNoCase(pszPathKey) == 0)
			|| ((pszPathKey == NULL || *pszPathKey == _T('\0')) && pszDirectory != NULL && pszName != NULL && pFile->strName.CompareNoCase(pszName) == 0 && EqualPaths(pFile->strDirectory, pszDirectory))) {
			posMatch = posCurrent;
			break;
		}
	}

	if (posMatch == NULL)
		return false;

	UnknownFile_Struct* pFile = currentlyhashing_list.GetAt(posMatch);
	const CString strPathKey = !pFile->strPathKey.IsEmpty() ? pFile->strPathKey : BuildNoCaseFilePathKey(pFile->strDirectory, pFile->strName);
	m_mapHashingPathsNoCase.RemoveKey(strPathKey);
	currentlyhashing_list.RemoveAt(posMatch);
	delete pFile;
	HashNextFile();
	return true;
}

bool CSharedFileList::RemoveFromHashing(CKnownFile *hashed, LPCTSTR pszPathKey)
{
	if (hashed == NULL)
		return false;
	return RemoveCurrentHashingByPathKey(pszPathKey, hashed->GetPath(), hashed->GetFileName());
}

void CSharedFileList::HashFailed(SharedFileHashResult_Struct *hashed)
{
	if (hashed != NULL) {
		RemoveCurrentHashingByPathKey(hashed->strPathKey, hashed->strDirectory, hashed->strName);
		delete hashed->pKnownFile;
		delete hashed;
	}
	FlushOutputBulkAddListUpdateIfIdle();
}

void CSharedFileList::QueueDeferredHashResult(SharedFileHashResult_Struct* pResult)
{
	if (pResult == NULL)
		return;
	TRACE(_T("Shared file hash result message could not be posted. Deferring UI-thread processing.\n"));
	CSingleLock lock(&m_csDeferredHashResults, TRUE);
	ASSERT(m_pDeferredHashResult == NULL);
	if (m_pDeferredHashResult == NULL)
		m_pDeferredHashResult = pResult;
	else {
		delete pResult->pKnownFile;
		delete pResult;
	}
}

void CSharedFileList::ProcessDeferredHashResults()
{
	SharedFileHashResult_Struct* pResult = NULL;
	{
		CSingleLock lock(&m_csDeferredHashResults, TRUE);
		pResult = m_pDeferredHashResult;
		m_pDeferredHashResult = NULL;
	}

	if (pResult == NULL)
		return;
	if (theApp.IsClosing()) {
		delete pResult->pKnownFile;
		delete pResult;
	} else if (pResult->pKnownFile != NULL) {
		CKnownFile* pKnownFile = pResult->pKnownFile;
		pResult->pKnownFile = NULL;
		FileHashingFinished(pKnownFile, pResult->strPathKey);
		delete pResult;
	} else
		HashFailed(pResult);
}

bool CSharedFileList::StartMetaDataUpdateThread()
{
	SharedFileMetaDataThreadContext* pContext = m_pMetaDataThreadContext;
	if (pContext == NULL || ::InterlockedCompareExchange(&pContext->lStopping, 0, 0) != 0)
		return false;
	if (::InterlockedCompareExchange(&pContext->lThreadActive, 1, 0) != 0)
		return true;

	AddRefMetaDataThreadContext(pContext);
	CWinThread* pThread = NULL;
	try {
		pThread = AfxBeginThread(RunMetaDataUpdateProc, pContext, THREAD_PRIORITY_IDLE);
	} catch (CException* ex) {
		ex->Delete();
	} catch (...) {
	}
	if (pThread != NULL)
		return true;

	::InterlockedExchange(&pContext->lThreadActive, 0);
	ReleaseMetaDataThreadContext(pContext);
	return false;
}

void CSharedFileList::ShutdownMetaDataUpdateThread()
{
	SharedFileMetaDataThreadContext* pContext = m_pMetaDataThreadContext;
	if (pContext == NULL)
		return;

	m_pMetaDataThreadContext = NULL;
	::InterlockedExchange(&pContext->lStopping, 1);
	{
		CSingleLock queueLock(&pContext->queueLock, TRUE);
		while (!pContext->pendingTasks.IsEmpty())
			delete pContext->pendingTasks.RemoveHead();
		while (!pContext->completedTasks.IsEmpty())
			delete pContext->completedTasks.RemoveHead();
		pContext->latestTasks.RemoveAll();
	}
	ReleaseMetaDataThreadContext(pContext);
}

CSharedFileList::EMetaDataQueueResult CSharedFileList::QueueMetaDataUpdate(const CKnownFile* pFile, bool bManualUpdate, bool bForceUpdate)
{
	ASSERT(theApp.IsUiThread());
	if (pFile == NULL || pFile->IsPartFile() || pFile->GetPath().IsEmpty() || pFile->GetFilePath().IsEmpty())
		return MetaDataQueueUnchanged;
	if (theApp.IsClosing())
		return MetaDataQueueFailed;
	if (!bManualUpdate && !bForceUpdate && thePrefs.GetExtractMetaData() == 0)
		return MetaDataQueueUnchanged;

	SharedFileMetaDataThreadContext* pContext = m_pMetaDataThreadContext;
	if (pContext == NULL)
		return MetaDataQueueFailed;

	SharedFileMetaDataTask_Struct* pTask = NULL;
	try {
		pTask = new SharedFileMetaDataTask_Struct;
		pTask->strFileName = pFile->GetFileName();
		pTask->strDirectory = pFile->GetPath();
		pTask->strFilePath = pFile->GetFilePath();
		pTask->strQueueKey = BuildNoCasePathKey(pTask->strFilePath);
		md4cpy(pTask->aucFileHash, pFile->GetFileHash());
		pTask->uFileSize = static_cast<uint64>(pFile->GetFileSize());
		pTask->tUtcFileDate = pFile->GetUtcFileDate();
		pTask->bManualUpdate = bManualUpdate;
		pTask->bExtractMetaData = thePrefs.GetExtractMetaData() != 0;
	} catch (CException* ex) {
		delete pTask;
		ex->Delete();
		if (!bManualUpdate && !bForceUpdate)
			::InterlockedExchange(&pContext->lReconciliationRequired, 1);
		return MetaDataQueueFailed;
	} catch (...) {
		delete pTask;
		if (!bManualUpdate && !bForceUpdate)
			::InterlockedExchange(&pContext->lReconciliationRequired, 1);
		return MetaDataQueueFailed;
	}

	try {
		CSingleLock queueLock(&pContext->queueLock, TRUE);
		if (::InterlockedCompareExchange(&pContext->lStopping, 0, 0) != 0) {
			delete pTask;
			return MetaDataQueueFailed;
		}

		void* pvExistingTask = NULL;
		SharedFileMetaDataTask_Struct* pExistingTask = pContext->latestTasks.Lookup(pTask->strQueueKey, pvExistingTask)
			? static_cast<SharedFileMetaDataTask_Struct*>(pvExistingTask) : NULL;
		if (IsSameMetaDataTaskIdentity(pExistingTask, pTask) && pExistingTask->bExtractMetaData == pTask->bExtractMetaData) {
			delete pTask;
			return MetaDataQueueUnchanged;
		}
		if (!bManualUpdate && !bForceUpdate && pExistingTask == NULL && pContext->latestTasks.GetCount() >= kSharedFileMetaDataQueueLimit) {
			::InterlockedExchange(&pContext->lReconciliationRequired, 1);
			delete pTask;
			return MetaDataQueueFailed;
		}
		const bool bTransferManualUpdate = pExistingTask != NULL && pExistingTask->bManualUpdate && !pTask->bManualUpdate;

		POSITION posExistingPending = pExistingTask != NULL ? pContext->pendingTasks.Find(pExistingTask) : NULL;
		POSITION posExistingCompleted = pExistingTask != NULL ? pContext->completedTasks.Find(pExistingTask) : NULL;
		try {
			pContext->pendingTasks.AddTail(pTask);
			pContext->latestTasks.SetAt(pTask->strQueueKey, pTask);
		} catch (...) {
			POSITION posNewTask = pContext->pendingTasks.Find(pTask);
			if (posNewTask != NULL)
				pContext->pendingTasks.RemoveAt(posNewTask);
			RemoveMetaDataTaskIndexLocked(pContext, pTask);
			throw;
		}
		if (bTransferManualUpdate) {
			pTask->bManualUpdate = true;
			pExistingTask->bManualUpdate = false;
		}
		if (posExistingPending != NULL) {
			pContext->pendingTasks.RemoveAt(posExistingPending);
			if (pExistingTask->bManualUpdate)
				::InterlockedIncrement(&pContext->lDiscardedManualTasks);
			delete pExistingTask;
		} else if (posExistingCompleted != NULL) {
			pContext->completedTasks.RemoveAt(posExistingCompleted);
			if (pExistingTask->bManualUpdate)
				::InterlockedIncrement(&pContext->lDiscardedManualTasks);
			delete pExistingTask;
		}
	} catch (CException* ex) {
		delete pTask;
		ex->Delete();
		if (!bManualUpdate && !bForceUpdate)
			::InterlockedExchange(&pContext->lReconciliationRequired, 1);
		return MetaDataQueueFailed;
	} catch (...) {
		delete pTask;
		if (!bManualUpdate && !bForceUpdate)
			::InterlockedExchange(&pContext->lReconciliationRequired, 1);
		return MetaDataQueueFailed;
	}

	if (::InterlockedCompareExchange(&m_lRebuildMetaDataThreadActive, 0, 0) != 0
		|| ::InterlockedCompareExchange(&pContext->lReconciliationActive, 0, 0) != 0
		|| StartMetaDataUpdateThread())
		return MetaDataQueueQueued;

	bool bRemoved = false;
	{
		CSingleLock queueLock(&pContext->queueLock, TRUE);
		POSITION pos = pContext->pendingTasks.Find(pTask);
		if (pos != NULL) {
			pContext->pendingTasks.RemoveAt(pos);
			RemoveMetaDataTaskIndexLocked(pContext, pTask);
			bRemoved = true;
		}
	}
	if (bRemoved)
		delete pTask;
	if (!bManualUpdate && !bForceUpdate)
		::InterlockedExchange(&pContext->lReconciliationRequired, 1);
	return MetaDataQueueFailed;
}

bool CSharedFileList::QueueMetaDataUpdateForFile(const CKnownFile* pFile)
{
	const EMetaDataQueueResult eResult = QueueMetaDataUpdate(pFile, false, true);
	if (eResult == MetaDataQueueQueued)
		NotifyShowFilesCount();
	return eResult != MetaDataQueueFailed;
}

UINT CSharedFileList::GetMetaDataUpdateCount() const
{
	SharedFileMetaDataThreadContext* pContext = m_pMetaDataThreadContext;
	if (pContext == NULL)
		return 0;

	CSingleLock queueLock(&pContext->queueLock, TRUE);
	const INT_PTR iCount = pContext->latestTasks.GetCount();
	if (iCount <= 0)
		return 0;
	if (static_cast<ULONGLONG>(iCount) > UINT_MAX)
		return UINT_MAX;
	return static_cast<UINT>(iCount);
}

void CSharedFileList::QueueMetaDataReconciliation()
{
	ASSERT(theApp.IsUiThread());
	SharedFileMetaDataThreadContext* pContext = m_pMetaDataThreadContext;
	if (pContext == NULL || theApp.IsClosing() || thePrefs.GetExtractMetaData() == 0) {
		if (pContext != NULL) {
			::InterlockedExchange(&pContext->lReconciliationRequired, 0);
			CSingleLock listlock(&m_mutWriteList, TRUE);
			m_posMetaDataReconciliation = NULL;
			m_bMetaDataReconciliationStarted = false;
		}
		return;
	}
	if (::InterlockedCompareExchange(&pContext->lReconciliationActive, 1, 0) != 0)
		return;

	bool bReconciliationCompleted = false;
	POSITION posRetry = NULL;
	uint32 uRetryPathRevision = 0;
	try {
		for (UINT uProcessed = 0; uProcessed < kSharedFileMetaDataReconciliationPerSlice;) {
			CString strPathKey;
			void* pvFile = NULL;
			{
				CSingleLock listlock(&m_mutWriteList, TRUE);
				if (!m_bMetaDataReconciliationStarted || m_uMetaDataReconciliationPathRevision != m_uSharedPathCacheRevision) {
					m_posMetaDataReconciliation = m_mapSharedPathsNoCase.GetStartPosition();
					m_uMetaDataReconciliationPathRevision = m_uSharedPathCacheRevision;
					m_bMetaDataReconciliationStarted = true;
				}
				if (m_posMetaDataReconciliation == NULL) {
					m_bMetaDataReconciliationStarted = false;
					bReconciliationCompleted = true;
					break;
				}

				posRetry = m_posMetaDataReconciliation;
				uRetryPathRevision = m_uMetaDataReconciliationPathRevision;
				m_mapSharedPathsNoCase.GetNextAssoc(m_posMetaDataReconciliation, strPathKey, pvFile);
			}

			EMetaDataQueueResult eQueueResult = MetaDataQueueUnchanged;
			bool bQueueCandidate = false;
			if (pvFile != NULL && pvFile != reinterpret_cast<void*>(1)) {
				CSingleLock listlock(&m_mutWriteList, TRUE);
				CKnownFile* pFile = static_cast<CKnownFile*>(pvFile);
				void* pvCurrentFile = NULL;
				if (m_mapSharedPathsNoCase.Lookup(strPathKey, pvCurrentFile) && pvCurrentFile == pFile && !pFile->IsPartFile()
					&& BuildNoCasePathKey(pFile->GetFilePath()) == strPathKey) {
					bQueueCandidate = true;
					eQueueResult = QueueMetaDataUpdate(pFile);
				}
			} else if (theApp.knownfiles != NULL) {
				CSingleLock duplicatesLock(&theApp.knownfiles->m_csDuplicatesLock, TRUE);
				for (CKnownFileList::KnownFileList::const_iterator it = theApp.knownfiles->m_duplicateFileList.begin(); it != theApp.knownfiles->m_duplicateFileList.end(); ++it) {
					CKnownFile* pFile = *it;
					if (pFile == NULL || pFile->IsPartFile() || BuildNoCasePathKey(pFile->GetFilePath()) != strPathKey)
						continue;

					CString strSharedDirectory(pFile->GetSharedDirectory());
					if (strSharedDirectory.IsEmpty())
						strSharedDirectory = pFile->GetPath();
					if (ShouldBeShared(strSharedDirectory, pFile->GetFilePath(), false)) {
						bQueueCandidate = true;
						eQueueResult = QueueMetaDataUpdate(pFile);
					}
					break;
				}
			}

			if (bQueueCandidate && eQueueResult == MetaDataQueueFailed)
				break;
			posRetry = NULL;
			++uProcessed;
		}
	} catch (CException* ex) {
		ex->Delete();
	} catch (...) {
	}
	if (posRetry != NULL) {
		CSingleLock listlock(&m_mutWriteList, TRUE);
		if (m_bMetaDataReconciliationStarted && m_uMetaDataReconciliationPathRevision == uRetryPathRevision && m_uSharedPathCacheRevision == uRetryPathRevision)
			m_posMetaDataReconciliation = posRetry;
		else {
			m_posMetaDataReconciliation = NULL;
			m_bMetaDataReconciliationStarted = false;
		}
	}
	if (!bReconciliationCompleted) {
		CSingleLock listlock(&m_mutWriteList, TRUE);
		if (m_bMetaDataReconciliationStarted && m_uMetaDataReconciliationPathRevision == m_uSharedPathCacheRevision && m_posMetaDataReconciliation == NULL) {
			m_bMetaDataReconciliationStarted = false;
			bReconciliationCompleted = true;
		}
	}

	::InterlockedExchange(&pContext->lReconciliationRequired, bReconciliationCompleted ? 0 : 1);
	::InterlockedExchange(&pContext->lReconciliationActive, 0);
	bool bPendingWork = false;
	{
		CSingleLock queueLock(&pContext->queueLock, TRUE);
		bPendingWork = !pContext->pendingTasks.IsEmpty();
	}
	if (bPendingWork) {
		NotifyShowFilesCount();
		StartMetaDataUpdateThread();
	}
}

UINT AFX_CDECL CSharedFileList::RunMetaDataUpdateProc(LPVOID pParam)
{
	DbgSetThreadName("SharedFileMetaData");
	SharedFileMetaDataThreadContext* pContext = static_cast<SharedFileMetaDataThreadContext*>(pParam);
	const HRESULT hrCoInitialize = ::CoInitialize(NULL);

	for (;;) {
		SharedFileMetaDataTask_Struct* pTask = NULL;
		{
			CSingleLock queueLock(&pContext->queueLock, TRUE);
			if (::InterlockedCompareExchange(&pContext->lStopping, 0, 0) != 0) {
				while (!pContext->pendingTasks.IsEmpty())
					delete pContext->pendingTasks.RemoveHead();
				pContext->latestTasks.RemoveAll();
				::InterlockedExchange(&pContext->lThreadActive, 0);
				break;
			}
			if (pContext->pendingTasks.IsEmpty()) {
				::InterlockedExchange(&pContext->lThreadActive, 0);
				break;
			}
			pTask = pContext->pendingTasks.RemoveHead();
		}

		try {
			if (!pTask->bExtractMetaData) {
				pTask->bProbeCompleted = true;
				pTask->bSourceStable = true;
			} else {
				uint64 uSourceFileSize = 0;
				FILETIME ftSourceLastWrite = {};
				time_t tSourceLastWrite = 0;
				if (TryGetHashSourceSnapshot(pTask->strFilePath, uSourceFileSize, ftSourceLastWrite)) {
					tSourceLastWrite = static_cast<time_t>(FileTimeToUnixTime(ftSourceLastWrite));
					if (tSourceLastWrite > 0)
						AdjustNTFSDaylightFileTime(tSourceLastWrite, pTask->strFilePath);
				}
				pTask->bProbeCompleted = true;
				pTask->bSourceStable = uSourceFileSize == pTask->uFileSize && tSourceLastWrite > 0 && IsFileDateEqual(tSourceLastWrite, pTask->tUtcFileDate);
				if (pTask->bSourceStable) {
					pTask->ftSourceLastWrite = ftSourceLastWrite;
					CShareableFile sourceFile;
					sourceFile.SetAFileName(pTask->strFileName);
					sourceFile.SetPath(pTask->strDirectory);
					sourceFile.SetFilePath(pTask->strFilePath);
					sourceFile.SetFileSize(pTask->uFileSize);

					pTask->pMetaData = new SKnownFileMetaData;
					const bool bExtracted = CKnownFile::ExtractMetaData(&sourceFile, *pTask->pMetaData);
					pTask->bSourceStable = IsHashSourceSnapshotCurrent(pTask->strFilePath, pTask->uFileSize, pTask->ftSourceLastWrite);
					if (!bExtracted || !pTask->bSourceStable) {
						delete pTask->pMetaData;
						pTask->pMetaData = NULL;
					}
				}
			}
		} catch (CException* ex) {
			ex->Delete();
		} catch (...) {
		}

		{
			CSingleLock queueLock(&pContext->queueLock, TRUE);
			void* pvLatestTask = NULL;
			const bool bLatestTask = pContext->latestTasks.Lookup(pTask->strQueueKey, pvLatestTask) && pvLatestTask == pTask;
			if (::InterlockedCompareExchange(&pContext->lStopping, 0, 0) != 0) {
				delete pTask;
			} else if (!bLatestTask) {
				if (pTask->bManualUpdate)
					::InterlockedIncrement(&pContext->lDiscardedManualTasks);
				delete pTask;
			} else {
				try {
					pContext->completedTasks.AddTail(pTask);
				} catch (CException* ex) {
					RemoveMetaDataTaskIndexLocked(pContext, pTask);
					::InterlockedIncrement(&pContext->lDiscardedTasks);
					if (pTask->bManualUpdate)
						::InterlockedIncrement(&pContext->lDiscardedManualTasks);
					delete pTask;
					ex->Delete();
				} catch (...) {
					RemoveMetaDataTaskIndexLocked(pContext, pTask);
					::InterlockedIncrement(&pContext->lDiscardedTasks);
					if (pTask->bManualUpdate)
						::InterlockedIncrement(&pContext->lDiscardedManualTasks);
					delete pTask;
				}
			}
		}
	}

	if (SUCCEEDED(hrCoInitialize))
		::CoUninitialize();
	ReleaseMetaDataThreadContext(pContext);
	return 0;
}

void CSharedFileList::ProcessDeferredMetaDataUpdates()
{
	ASSERT(theApp.IsUiThread());
	SharedFileMetaDataThreadContext* pContext = m_pMetaDataThreadContext;
	if (pContext == NULL)
		return;

	bool bUpdated = false;
	bool bReloadList = false;
	bool bReloadSharedFiles = false;
	uint64 uTasksProcessed = 0;
	uint64 uManualTasksProcessed = 0;
	for (;;) {
		SharedFileMetaDataTask_Struct* pTask = NULL;
		{
			CSingleLock queueLock(&pContext->queueLock, TRUE);
			if (pContext->completedTasks.IsEmpty())
				break;
			pTask = pContext->completedTasks.RemoveHead();
			RemoveMetaDataTaskIndexLocked(pContext, pTask);
		}
		try {
			if (!theApp.IsClosing() && pTask->bProbeCompleted && theApp.knownfiles != NULL) {
				CKnownFile* pTarget = theApp.knownfiles->FindKnownFileByID(pTask->aucFileHash);
				if (!IsMetaDataUpdateTargetCurrent(pTarget, pTask))
					pTarget = NULL;
				if (pTarget == NULL) {
					CSingleLock duplicatesLock(&theApp.knownfiles->m_csDuplicatesLock, TRUE);
					for (CKnownFileList::KnownFileList::const_iterator it = theApp.knownfiles->m_duplicateFileList.begin(); it != theApp.knownfiles->m_duplicateFileList.end(); ++it) {
						if (IsMetaDataUpdateTargetCurrent(*it, pTask)) {
							pTarget = *it;
							break;
						}
					}
				}

				if (pTarget != NULL && !pTask->bSourceStable) {
					if (!pTask->bManualUpdate)
						bReloadSharedFiles = true;
				} else if (pTarget != NULL) {
					pTarget->ApplyMetaDataTags(thePrefs.GetExtractMetaData() != 0 ? pTask->pMetaData : NULL);
					if (GetLiveFileByID(pTask->aucFileHash) == pTarget) {
						RemoveKeywords(pTarget);
						AddKeywords(pTarget);
						m_keywords->ResetNextKeyword();
						m_keywords->SetNextPublishTime(0);
						RepublishFile(pTarget, true);
						StoreWebSharedFileSnapshot(pTarget);
						if (output != NULL)
							output->UpdateFile(pTarget);
					} else
						bReloadList = true;
					bUpdated = true;
				}
			}
		} catch (CException* ex) {
			ex->Delete();
		} catch (...) {
		}
		if (pTask->bManualUpdate)
			++uManualTasksProcessed;
		++uTasksProcessed;
		delete pTask;
	}

	const LONG lDiscardedTasks = ::InterlockedExchange(&pContext->lDiscardedTasks, 0);
	if (lDiscardedTasks > 0)
		uTasksProcessed += static_cast<uint64>(lDiscardedTasks);
	const LONG lDiscardedManualTasks = ::InterlockedExchange(&pContext->lDiscardedManualTasks, 0);
	if (lDiscardedManualTasks > 0)
		uManualTasksProcessed += static_cast<uint64>(lDiscardedManualTasks);

	bool bManualUpdateCompleted = false;
	if (uManualTasksProcessed != 0) {
		CSingleLock countLock(&m_MetadataUpdatingCountLock, TRUE);
		if (uManualTasksProcessed >= m_uMetadataUpdatingCount)
			m_uMetadataUpdatingCount = 0;
		else
			m_uMetadataUpdatingCount -= uManualTasksProcessed;
		bManualUpdateCompleted = m_uMetadataUpdatingCount == 0 && ::InterlockedCompareExchange(&m_lRebuildMetaDataThreadActive, 0, 0) != 0;
	}
	if (bManualUpdateCompleted)
		::InterlockedExchange(&m_lRebuildMetaDataThreadActive, 0);

	if (bUpdated || bManualUpdateCompleted) {
		theApp.QueueSharedFilesListChangedEvent(_T("shared-metadata-updated"));
		if ((bReloadList || bManualUpdateCompleted) && theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL)
			theApp.emuledlg->sharedfileswnd->PostMetadataUpdatedAsync();
	}
	if (bReloadSharedFiles && theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL)
		theApp.emuledlg->sharedfileswnd->PostAutoReloadSharedFilesAsync(0);
	if (uTasksProcessed != 0)
		NotifyShowFilesCount();
	if (bManualUpdateCompleted)
		theApp.QueueLogLine(true, GetResString(_T("METADA_UPDATE_COMPLETED")));

	bool bPendingWork = false;
	bool bMetaDataWorkIdle = false;
	{
		CSingleLock queueLock(&pContext->queueLock, TRUE);
		bPendingWork = !pContext->pendingTasks.IsEmpty();
		bMetaDataWorkIdle = !bPendingWork && pContext->completedTasks.IsEmpty() && ::InterlockedCompareExchange(&pContext->lThreadActive, 0, 0) == 0;
	}
	if (bMetaDataWorkIdle
		&& ::InterlockedCompareExchange(&m_lRebuildMetaDataThreadActive, 0, 0) == 0
		&& ::InterlockedCompareExchange(&pContext->lReconciliationRequired, 0, 0) != 0)
		QueueMetaDataReconciliation();
	else if (bPendingWork && ::InterlockedCompareExchange(&m_lRebuildMetaDataThreadActive, 0, 0) == 0)
		StartMetaDataUpdateThread();
}

void CSharedFileList::QueueDeferredPartFileHashResult(PartFileHash_Struct* pResult)
{
	if (pResult == NULL)
		return;
	TRACE(_T("Part file hash result message could not be posted. Deferring UI-thread processing.\n"));
	CSingleLock lock(&m_csDeferredPartFileHashResults, TRUE);
	m_deferredPartFileHashResults.AddTail(pResult);
}

void CSharedFileList::ProcessDeferredPartFileHashResults()
{
	CTypedPtrList<CPtrList, PartFileHash_Struct*> pendingResults;
	{
		CSingleLock lock(&m_csDeferredPartFileHashResults, TRUE);
		while (!m_deferredPartFileHashResults.IsEmpty())
			pendingResults.AddTail(m_deferredPartFileHashResults.RemoveHead());
	}

	while (!pendingResults.IsEmpty()) {
		PartFileHash_Struct* pResult = pendingResults.RemoveHead();
		if (theApp.IsClosing()) {
			delete pResult->pKnownFile;
			delete pResult;
		} else if (theApp.emuledlg != NULL && ::IsWindow(theApp.emuledlg->m_hWnd))
			theApp.emuledlg->SendMessage(pResult->pKnownFile != NULL ? TM_FINISHEDPARTFILEHASHING : TM_PARTFILEHASHFAILED, 0, (LPARAM)pResult);
		else {
			CSingleLock lock(&m_csDeferredPartFileHashResults, TRUE);
			m_deferredPartFileHashResults.AddTail(pResult);
		}
	}
}

void CSharedFileList::UpdateFile(CKnownFile *toupdate)
{
	StoreWebSharedFileSnapshot(toupdate);
	if (theApp.emuledlg != NULL)
		theApp.emuledlg->PostSharedFilesCtrlUpdateFileAsync(toupdate);
}

bool CSharedFileList::ProbablyHaveSingleSharedFiles() const
{
	CSingleLock lock(&m_csShareRules, TRUE);
	return bHaveSingleSharedFiles && !m_liSingleSharedFiles.IsEmpty();
}

void CSharedFileList::CopyExcludedSharedDirectories(CStringList& liExcludedSharedDirs) const
{
	CSingleLock lock(&m_csShareRules, TRUE);
	CopyCStringList(liExcludedSharedDirs, m_liExcludedSharedDirs);
}

void CSharedFileList::CopyExplicitShareRules(CStringList& liSingleSharedFiles, CStringList& liSingleExcludedFiles, CStringList& liExcludedSharedDirs) const
{
	CSingleLock lock(&m_csShareRules, TRUE);
	CopyCStringList(liSingleSharedFiles, m_liSingleSharedFiles);
	CopyCStringList(liSingleExcludedFiles, m_liSingleExcludedFiles);
	CopyCStringList(liExcludedSharedDirs, m_liExcludedSharedDirs);
}

bool CSharedFileList::AreExplicitShareRulesLoaded() const
{
	CSingleLock lock(&m_csShareRules, TRUE);
	return m_bExplicitShareRulesLoaded;
}

void CSharedFileList::SetExplicitShareRulesLoaded(bool bLoaded)
{
	CSingleLock lock(&m_csShareRules, TRUE);
	m_bExplicitShareRulesLoaded = bLoaded;
}

CString CSharedFileList::BuildSharedPathCacheKey(const CString& strFilePath)
{
	return BuildNoCasePathKey(strFilePath);
}

void CSharedFileList::UpdateSharedPathCache(CKnownFile* pFile, LPCTSTR pOldFilePath)
{
	if (pFile == NULL)
		return;

	const CString strOldPathKey = pOldFilePath != NULL && pOldFilePath[0] != _T('\0') ? BuildNoCasePathKey(pOldFilePath) : CString();
	CString strNewPathKey;
	{
		CSingleLock listlock(&m_mutWriteList, TRUE);
		CKnownFile* pLiveFile = NULL;
		if (!m_Files_map.Lookup(CCKey(pFile->GetFileHash()), pLiveFile) || pLiveFile != pFile)
			return;

		if (!pFile->GetFilePath().IsEmpty())
			strNewPathKey = BuildNoCasePathKey(pFile->GetFilePath());

		bool bPathCacheChanged = false;
		if (!strOldPathKey.IsEmpty() && strOldPathKey != strNewPathKey && m_mapSharedPathsNoCase.RemoveKey(strOldPathKey))
			bPathCacheChanged = true;

		if (!strNewPathKey.IsEmpty()) {
			void* pvCurrentFile = NULL;
			if (!m_mapSharedPathsNoCase.Lookup(strNewPathKey, pvCurrentFile) || pvCurrentFile != pFile) {
				m_mapSharedPathsNoCase[strNewPathKey] = pFile;
				bPathCacheChanged = true;
			}
		}
		if (bPathCacheChanged)
			++m_uSharedPathCacheRevision;
	}

	if (!strNewPathKey.IsEmpty() && RemoveWaitingFromHashingByPathKey(strNewPathKey)) {
		FlushOutputBulkAddListUpdateIfIdle();
		NotifyShowFilesCount();
	}
}

void CSharedFileList::UpdateSharedPathCacheByPath(LPCTSTR pOldFilePath, LPCTSTR pNewFilePath)
{
	const CString strOldPathKey = pOldFilePath != NULL && pOldFilePath[0] != _T('\0') ? BuildNoCasePathKey(pOldFilePath) : CString();
	const CString strNewPathKey = pNewFilePath != NULL && pNewFilePath[0] != _T('\0') ? BuildNoCasePathKey(pNewFilePath) : CString();
	{
		CSingleLock listlock(&m_mutWriteList, TRUE);
		bool bPathCacheChanged = false;
		if (!strOldPathKey.IsEmpty() && strOldPathKey != strNewPathKey && m_mapSharedPathsNoCase.RemoveKey(strOldPathKey))
			bPathCacheChanged = true;

		if (!strNewPathKey.IsEmpty()) {
			void* pvCurrentFile = NULL;
			if (!m_mapSharedPathsNoCase.Lookup(strNewPathKey, pvCurrentFile) || pvCurrentFile != reinterpret_cast<void*>(1)) {
				m_mapSharedPathsNoCase[strNewPathKey] = (void*)1;
				bPathCacheChanged = true;
			}
		}
		if (bPathCacheChanged)
			++m_uSharedPathCacheRevision;
	}

	if (!strNewPathKey.IsEmpty() && RemoveWaitingFromHashingByPathKey(strNewPathKey)) {
		FlushOutputBulkAddListUpdateIfIdle();
		NotifyShowFilesCount();
	}
}

bool CSharedFileList::FindUniqueLiveSharedFileByIdentity(LPCTSTR pszFileName, time_t tUtcFileDate, uint64 uFileSize, LPCTSTR pszNewFilePath, uchar aucFileHash[MDX_DIGEST_SIZE])
{
	struct SIdentityCandidate
	{
		uchar aucFileHash[MDX_DIGEST_SIZE];
		CString strFilePath;
	};

	std::vector<SIdentityCandidate> candidates;
	{
		CSingleLock listlock(&m_mutWriteList, TRUE);
		for (const CKnownFilesMap::CPair* pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair)) {
			CKnownFile* pFile = pair->value;
			if (pFile == NULL || pFile->IsPartFile())
				continue;

			if ((uint64)pFile->GetFileSize() != uFileSize || !IsFileDateEqual(pFile->GetUtcFileDate(), tUtcFileDate) || pFile->GetFileName().CompareNoCase(pszFileName) != 0)
				continue;

			SIdentityCandidate candidate;
			md4cpy(candidate.aucFileHash, pFile->GetFileHash());
			candidate.strFilePath = pFile->GetFilePath();
			candidates.push_back(candidate);
		}
	}

	bool bHaveMatch = false;
	uchar aucMatchHash[MDX_DIGEST_SIZE] = {0};
	for (size_t i = 0; i < candidates.size(); ++i) {
		const SIdentityCandidate& candidate = candidates[i];

		if (pszNewFilePath != NULL && candidate.strFilePath.CompareNoCase(pszNewFilePath) == 0) {
			md4cpy(aucFileHash, candidate.aucFileHash);
			return true;
		}

		if (!candidate.strFilePath.IsEmpty() && ::PathFileExists(candidate.strFilePath))
			continue;

		if (bHaveMatch && memcmp(aucMatchHash, candidate.aucFileHash, MDX_DIGEST_SIZE) != 0)
			return false;

		md4cpy(aucMatchHash, candidate.aucFileHash);
		bHaveMatch = true;
	}

	if (!bHaveMatch)
		return false;

	md4cpy(aucFileHash, aucMatchHash);
	return true;
}

bool CSharedFileList::TryReconcileMovedSharedFile(const CString& strFilePath)
{
	CString strFileName;
	CString strDirectory;
	time_t tUtcFileDate = static_cast<time_t>(-1);
	uint64 uFileSize = 0;
	if (!TryBuildSharedFileIdentity(strFilePath, strFileName, strDirectory, tUtcFileDate, uFileSize))
		return false;

	if (thePrefs.IsTempFile(strDirectory, strFileName) || !ShouldBeShared(strDirectory, strFilePath, false))
		return false;

	uchar aucFileHash[MDX_DIGEST_SIZE];
	if (!FindUniqueLiveSharedFileByIdentity(strFileName, tUtcFileDate, uFileSize, strFilePath, aucFileHash))
		return false;

	CString strOldFilePath;
	{
		CSingleLock listlock(&m_mutWriteList, TRUE);
		CKnownFile* pMatch = NULL;
		for (const CKnownFilesMap::CPair* pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair)) {
			CKnownFile* pFile = pair->value;
			if (pFile == NULL || pFile->IsPartFile())
				continue;

			if (md4equ(pFile->GetFileHash(), aucFileHash)) {
				pMatch = pFile;
				break;
			}
		}
		if (pMatch == NULL)
			return false;

		if ((uint64)pMatch->GetFileSize() != uFileSize || !IsFileDateEqual(pMatch->GetUtcFileDate(), tUtcFileDate) || pMatch->GetFileName().CompareNoCase(strFileName) != 0)
			return false;

		strOldFilePath = pMatch->GetFilePath();
		if (strOldFilePath.CompareNoCase(strFilePath) == 0)
			return true;

		pMatch->SetPath(strDirectory);
		pMatch->SetFilePath(strFilePath);
		pMatch->SetSharedDirectory(strDirectory);
		pMatch->SetLastSeen();
		if (!strOldFilePath.IsEmpty())
			m_mapSharedPathsNoCase.RemoveKey(BuildNoCasePathKey(strOldFilePath));
		if (!pMatch->GetFilePath().IsEmpty())
			m_mapSharedPathsNoCase[BuildNoCasePathKey(pMatch->GetFilePath())] = pMatch;
		++m_uSharedPathCacheRevision;
		MarkSharedFilesModelChanged();
	}

	AddDebugLogLine(DLP_LOW, false, _T("%hs: Reconciled shared file move: \"%s\" -> \"%s\""), __FUNCTION__, (LPCTSTR)EscPercent(strOldFilePath), (LPCTSTR)EscPercent(strFilePath));
	return true;
}

void CSharedFileList::Process()
{
	ProcessDeferredHashResults();
	ProcessDeferredPartFileHashResults();
	ProcessDeferredMetaDataUpdates();
	Publish();
	const DWORD dwNow = ::GetTickCount();
	if (dwNow < m_lastPublishED2K + ED2KREPUBLISHTIME)
		return;

	if (m_lastPublishED2KFlag) {
		SendListToServer();
		m_lastPublishED2K = dwNow;
	}
	else if (ShouldIncludeEServerBuddyMagicFile()) {
		const uint32 uMagicEpoch = CUpDownClient::GetEServerBuddyMagicEpoch();
		if (m_uLastEServerBuddyMagicAnnounceEpoch != uMagicEpoch) {
			SendListToServer();
			m_lastPublishED2K = dwNow;
		}
	}
}

void CSharedFileList::Publish()
{
	if (!Kademlia::CKademlia::IsConnected()
		|| (theApp.IsFirewalled()
			&& theApp.clientlist->GetServingBuddyStatus() != Connected
			//direct callback
			&& (Kademlia::CUDPFirewallTester::IsFirewalledUDP(true) || !Kademlia::CUDPFirewallTester::IsVerified())
		   )
		|| !GetCount()
		|| !Kademlia::CKademlia::GetPublish())
	{
		return;
	}

	//We are connected to Kad. We are either open or have a serving buddy. And Kad is ready to start publishing.
	time_t tNow = time(NULL);
	if (Kademlia::CKademlia::GetTotalStoreKey() < KADEMLIATOTALSTOREKEY) {
		//We are not at the max simultaneous keyword publishes
		if (tNow >= m_keywords->GetNextPublishTime()) {
			//Enough time has passed since last keyword publish

			//Get the next keyword which has to be (re-)published
			CPublishKeyword *pPubKw = m_keywords->GetNextKeyword();
			if (pPubKw) {
				//We have the next keyword to check if it can be published

				//Debug check to make sure things are going well.
				ASSERT(pPubKw->GetRefCount() > 0);

				if (tNow >= pPubKw->GetNextPublishTime()) {
					//This keyword can be published.
					Kademlia::CSearch *pSearch = Kademlia::CSearchManager::PrepareLookup(Kademlia::CSearch::STOREKEYWORD, false, pPubKw->GetKadID());
					if (pSearch) {
						//pSearch was created. Which means no search was already being done with this HashID.
						//This also means that it was checked to see if network load wasn't a factor.

						//This sets the filename into the search object so we can show it in the GUI.
						pSearch->SetGUIName(pPubKw->GetKeyword());

						//Add all file IDs which relate to the current keyword to be published
						const CSimpleKnownFileArray &aFiles = pPubKw->GetReferences();
						uint32 count = 0;
						for (int f = 0; f < aFiles.GetSize(); ++f) {
							//Debug check to make sure things are working well.
							ASSERT_VALID(aFiles[f]);
							// JOHNTODO - Why is this happening. I think it may have to do with downloading a file
							// that is already in the known file list.

							//Only publish complete files as someone else should have the full file to publish these keywords.
							//As a side effect, this may help reduce people finding incomplete files in the network.
							if (!aFiles[f]->IsPartFile() && IsFilePtrInList(aFiles[f])) {
								//We only publish up to 150 files per keyword, then rotate the list.
								if (++count >= 150) {
									pPubKw->RotateReferences(f);
									break;
								}
								pSearch->AddFileID(Kademlia::CUInt128(aFiles[f]->GetFileHash()));
							}
						}

						if (count) {
							//Start our keyword publish
							pPubKw->SetNextPublishTime(tNow + KADEMLIAREPUBLISHTIMEK);
							pPubKw->IncPublishedCount();
							Kademlia::CSearchManager::StartSearch(pSearch);
						} else
							//There were no valid files to publish with this keyword.
							delete pSearch;
					}
				}
			}
			m_keywords->SetNextPublishTime(tNow + KADEMLIAPUBLISHTIME);
		}
	}

	if (Kademlia::CKademlia::GetTotalStoreSrc() < KADEMLIATOTALSTORESRC) {
		if (tNow >= m_lastPublishKadSrc) {
			if (m_currFileSrc >= GetCount())
				m_currFileSrc = 0;
			CKnownFile *pCurKnownFile = GetFileByIndex(m_currFileSrc);
			if (pCurKnownFile && pCurKnownFile->PublishSrc())
				if (Kademlia::CSearchManager::PrepareLookup(Kademlia::CSearch::STOREFILE, true, Kademlia::CUInt128(pCurKnownFile->GetFileHash())) == NULL)
					pCurKnownFile->SetLastPublishTimeKadSrc(0, 0);

			++m_currFileSrc;

			// even if we did not publish a source, reset the timer so that this list is processed
			// only every KADEMLIAPUBLISHTIME seconds.
			m_lastPublishKadSrc = tNow + KADEMLIAPUBLISHTIME;
		}
	}

	if (Kademlia::CKademlia::GetTotalStoreNotes() < KADEMLIATOTALSTORENOTES) {
		if (tNow >= m_lastPublishKadNotes) {
			if (m_currFileNotes >= GetCount())
				m_currFileNotes = 0;
			CKnownFile *pCurKnownFile = GetFileByIndex(m_currFileNotes);
			if (pCurKnownFile && pCurKnownFile->PublishNotes())
				if (Kademlia::CSearchManager::PrepareLookup(Kademlia::CSearch::STORENOTES, true, Kademlia::CUInt128(pCurKnownFile->GetFileHash())) == NULL)
					pCurKnownFile->SetLastPublishTimeKadNotes(0);

			++m_currFileNotes;

			// even if we did not publish a source, reset the timer so that this list is processed
			// only every KADEMLIAPUBLISHTIME seconds.
			m_lastPublishKadNotes = tNow + KADEMLIAPUBLISHTIME;
		}
	}
}

void CSharedFileList::AddKeywords(CKnownFile *pFile)
{
	m_keywords->AddKeywords(pFile);
}

void CSharedFileList::RemoveKeywords(CKnownFile *pFile)
{
	m_keywords->RemoveKeywords(pFile);
}

void CSharedFileList::DeletePartFileInstances() const
{
	// this is allowed only in shutdown
	ASSERT(theApp.knownfiles && theApp.IsClosing());
	CCKey key;
	for (POSITION pos = m_Files_map.GetStartPosition(); pos != NULL;) {
		CKnownFile *cur_file;
		m_Files_map.GetNextAssoc(pos, key, cur_file);
		if (cur_file->IsKindOf(RUNTIME_CLASS(CPartFile))
			&& !theApp.downloadqueue->IsPartFile(cur_file)
			&& !theApp.knownfiles->IsFilePtrInList(cur_file))
		{
			delete cur_file; // only allowed during shut down
		}
	}
}

bool CSharedFileList::IsUnsharedFile(const uchar *auFileHash) const
{
	return auFileHash && m_UnsharedFiles_map.PLookup(CSKey(auFileHash));
}

void CSharedFileList::RebuildMetaData()
{
	if (m_Files_map.IsEmpty() || theApp.IsClosing())
		return;

	if (::InterlockedCompareExchange(&m_lRebuildMetaDataThreadActive, 1, 0) != 0) {
		AddLogLine(true, GetResString(_T("METADA_UPDATE_IN_PROGRESS")));
		return;
	}

	bool bAutoMetaDataWorkActive = false;
	SharedFileMetaDataThreadContext* pContext = m_pMetaDataThreadContext;
	if (pContext != NULL) {
		CSingleLock queueLock(&pContext->queueLock, TRUE);
		bAutoMetaDataWorkActive = ::InterlockedCompareExchange(&pContext->lThreadActive, 0, 0) != 0
			|| !pContext->pendingTasks.IsEmpty() || !pContext->completedTasks.IsEmpty();
	}
	if (bAutoMetaDataWorkActive) {
		::InterlockedExchange(&m_lRebuildMetaDataThreadActive, 0);
		AddLogLine(true, GetResString(_T("METADA_UPDATE_IN_PROGRESS")));
		return;
	}
	if (pContext == NULL) {
		::InterlockedExchange(&m_lRebuildMetaDataThreadActive, 0);
		return;
	}

	uint64 uQueuedFiles = 0;
	{
		CSingleLock listlock(&m_mutWriteList, TRUE);
		CCKey key;
		for (POSITION pos = m_Files_map.GetStartPosition(); pos != NULL;) {
			CKnownFile *cur_file = NULL;
			m_Files_map.GetNextAssoc(pos, key, cur_file);
			if (cur_file != NULL && !cur_file->IsKindOf(RUNTIME_CLASS(CPartFile)) && !cur_file->GetPath().IsEmpty() && QueueMetaDataUpdate(cur_file, true) == MetaDataQueueQueued)
				++uQueuedFiles;
		}
	}

	CSingleLock sMetadataUpdatingCountLock(&m_MetadataUpdatingCountLock, TRUE);
	m_uMetadataUpdatingCount = uQueuedFiles;
	if (!m_uMetadataUpdatingCount) {
		::InterlockedExchange(&m_lRebuildMetaDataThreadActive, 0);
		AddLogLine(true, GetResString(_T("METADA_UPDATE_NO_SHARED_FILE_LISTED")));
		return;
	}
	sMetadataUpdatingCountLock.Unlock();

	if (!StartMetaDataUpdateThread()) {
		{
			CSingleLock queueLock(&pContext->queueLock, TRUE);
			while (!pContext->pendingTasks.IsEmpty())
				delete pContext->pendingTasks.RemoveHead();
			while (!pContext->completedTasks.IsEmpty())
				delete pContext->completedTasks.RemoveHead();
			pContext->latestTasks.RemoveAll();
			::InterlockedExchange(&pContext->lDiscardedTasks, 0);
			::InterlockedExchange(&pContext->lDiscardedManualTasks, 0);
		}
		::InterlockedExchange(&m_lRebuildMetaDataThreadActive, 0);
		CSingleLock countLock(&m_MetadataUpdatingCountLock, TRUE);
		m_uMetadataUpdatingCount = 0;
		return;
	}

	AddLogLine(true, GetResString(_T("METADA_UPDATE_STARTED")));
	NotifyShowFilesCount();
}

bool CSharedFileList::ShouldBeShared(const CString& sDirPath, LPCTSTR const pFilePath, bool bMustBeShared) const
{
	const CString sDir(NormalizeDirectoryPath(sDirPath));
	CString sIncoming(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
	CStringList liSharedDirs;
	CStringList liSingleSharedFiles;
	CStringList liSingleExcludedFiles;
	CStringList liExcludedSharedDirs;

	if (EqualPaths(sDir, sIncoming))
		return true;

	if (thePrefs.GetAutoShareSubdirs() && IsSubDirectoryOf(sDir, sIncoming))
		return true;

	for (INT_PTR i = thePrefs.GetCatCount(); --i > 0;) {
		CString sCatDir(thePrefs.GetCatPath(i));
		if (EqualPaths(sDir, sCatDir))
			return true;

		if (thePrefs.GetAutoShareSubdirs() && IsSubDirectoryOf(sDir, sCatDir))
			return true;
	}

	if (bMustBeShared) // Check only incoming & categories (cannot be unshared)
		return false;

	thePrefs.CopySharedDirectoryList(liSharedDirs);
	CopyExplicitShareRules(liSingleSharedFiles, liSingleExcludedFiles, liExcludedSharedDirs);

	if (pFilePath) { 
		// Check if this file is explicitly unshared
		if (ContainsPathNoCase(liSingleExcludedFiles, pFilePath))
			return false;

		// Check if this file is explicitly shared (as single file)
		if (ContainsPathNoCase(liSingleSharedFiles, pFilePath))
			return true;
	}

	const int nSharedDepth = GetBestDirectoryRuleDepth(liSharedDirs, sDir, thePrefs.GetAutoShareSubdirs());
	if (nSharedDepth < 0)
		return false;

	const int nExcludedDepth = GetBestDirectoryRuleDepth(liExcludedSharedDirs, sDir, true);
	return nSharedDepth >= nExcludedDepth;
}

bool CSharedFileList::ContainsSingleSharedFiles(const CString &strDirectory) const
{
	int iLen = strDirectory.GetLength();
	CSingleLock lock(&m_csShareRules, TRUE);
	for (POSITION pos = m_liSingleSharedFiles.GetHeadPosition(); pos != NULL;)
		if (_tcsnicmp(strDirectory, m_liSingleSharedFiles.GetNext(pos), iLen) == 0)
			return true;

	return false;
}

bool CSharedFileList::ExcludeFile(const CString &strFilePath)
{
	bool bShared = false;
	bool bRulesChanged = false;
	{
		CSingleLock lock(&m_csShareRules, TRUE);
		bShared = RemovePathNoCase(m_liSingleSharedFiles, strFilePath);
		bRulesChanged = bShared;
		bHaveSingleSharedFiles = !m_liSingleSharedFiles.IsEmpty();
	}

	// if this file was not shared as single file, check if we implicitly share it
	if (!bShared && !ShouldBeShared(strFilePath.Left(strFilePath.ReverseFind(_T('\\'))), strFilePath, false)) {
		// we don't actually share this file, can't be excluded
		return false;
	}
	if (ShouldBeShared(strFilePath.Left(strFilePath.ReverseFind(_T('\\'))), strFilePath, true)) {
		// we cannot unshare this file (incoming directories)
		ASSERT(0); // checks should have been done earlier
		return false;
	}

	// add to exclude list
	{
		CSingleLock lock(&m_csShareRules, TRUE);
		if (!ContainsPathNoCase(m_liSingleExcludedFiles, strFilePath)) {
			m_liSingleExcludedFiles.AddTail(strFilePath);
			bRulesChanged = true;
		}
	}

	if (bRulesChanged)
		InvalidateShareRuleSnapshot();

	// check if the file is in the shared list (doesn't have to; for example, if it is hashing or not loaded yet) and remove
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		if (strFilePath.CompareNoCase(pair->value->GetFilePath()) == 0) {
			RemoveFile(pair->value);
			break;
		}

	// GUI update to be done by the caller
	return true;
}


bool CSharedFileList::AddExcludedSharedDirectory(const CString &strDirPath)
{
	const CString sDir(NormalizeDirectoryPath(strDirPath));
	if (sDir.IsEmpty() || !thePrefs.IsShareableDirectory(sDir) || ShouldBeShared(sDir, NULL, true) || IsExcludedSharedDirectory(sDir))
		return false;

	CSingleLock lock(&m_csShareRules, TRUE);
	for (POSITION pos = m_liExcludedSharedDirs.GetHeadPosition(); pos != NULL;) {
		if (EqualPaths(m_liExcludedSharedDirs.GetNext(pos), sDir))
			return false;
	}
	m_liExcludedSharedDirs.AddTail(sDir);
	InvalidateShareRuleSnapshot();
	return true;
}

void CSharedFileList::ClearExcludedSharedDirectories()
{
	CSingleLock lock(&m_csShareRules, TRUE);
	m_liExcludedSharedDirs.RemoveAll();
	InvalidateShareRuleSnapshot();
}

void CSharedFileList::RemoveExcludedSharedDirectory(const CString &strDirPath, bool bSubDirectories)
{
	const CString sDir(NormalizeDirectoryPath(strDirPath));
	if (sDir.IsEmpty())
		return;

	CSingleLock lock(&m_csShareRules, TRUE);
	for (POSITION pos = m_liExcludedSharedDirs.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		const CString &strExcluded(m_liExcludedSharedDirs.GetNext(pos));
		const bool bMatches = bSubDirectories ? (EqualPaths(strExcluded, sDir) || IsSubDirectoryOf(strExcluded, sDir)) : EqualPaths(strExcluded, sDir);
		if (bMatches) {
			m_liExcludedSharedDirs.RemoveAt(pos2);
			if (!bSubDirectories)
				break;
		}
	}
	InvalidateShareRuleSnapshot();
}

bool CSharedFileList::IsExcludedSharedDirectory(const CString &strDirPath) const
{
	const CString sDir(NormalizeDirectoryPath(strDirPath));
	CSingleLock lock(&m_csShareRules, TRUE);
	for (POSITION pos = m_liExcludedSharedDirs.GetHeadPosition(); pos != NULL;)
		if (EqualPaths(m_liExcludedSharedDirs.GetNext(pos), sDir))
			return true;

	return false;
}

bool CSharedFileList::IsSharedByDirectoryRules(const CString &sDirPath) const
{
	const CString sDir(NormalizeDirectoryPath(sDirPath));
	CStringList liSharedDirs;
	CStringList liExcludedSharedDirs;
	thePrefs.CopySharedDirectoryList(liSharedDirs);
	CStringList liSingleSharedFilesDummy;
	CStringList liSingleExcludedFilesDummy;
	CopyExplicitShareRules(liSingleSharedFilesDummy, liSingleExcludedFilesDummy, liExcludedSharedDirs);
	const int nSharedDepth = GetBestDirectoryRuleDepth(liSharedDirs, sDir, thePrefs.GetAutoShareSubdirs());
	if (nSharedDepth < 0)
		return false;

	const int nExcludedDepth = GetBestDirectoryRuleDepth(liExcludedSharedDirs, sDir, true);
	return nSharedDepth >= nExcludedDepth;
}

void CSharedFileList::CheckAndAddSingleFile(const CFileFind& ff)
{
	m_searchThread->BeginSearch(ff.GetFilePath());
}

void CSharedFileList::Save() const
{
	if (!AreExplicitShareRulesLoaded()) {
		AddDebugLogLine(DLP_HIGH, false, _T("Shared files rules save skipped because explicit share rules are not loaded.\n"));
		return;
	}

	const CString &strFullPath(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + SHAREDFILES_FILE);
	CStringList liSingleSharedFiles;
	CStringList liSingleExcludedFiles;
	CStringList liExcludedSharedDirs;
	CopyExplicitShareRules(liSingleSharedFiles, liSingleExcludedFiles, liExcludedSharedDirs);
	try {
		CSafeMemFile file;
		static const WORD wBOM = u'\xFEFF';
		file.Write(&wBOM, sizeof(wBOM));

		for (POSITION pos = liSingleSharedFiles.GetHeadPosition(); pos != NULL;) {
			CString strLine = liSingleSharedFiles.GetNext(pos) + _T("\r\n");
			file.Write((LPCTSTR)strLine, strLine.GetLength() * sizeof(TCHAR));
		}
		for (POSITION pos = liSingleExcludedFiles.GetHeadPosition(); pos != NULL;) {
			CString strLine = CString(kExcludedSharedFilePrefix) + liSingleExcludedFiles.GetNext(pos) + _T("\r\n");
			file.Write((LPCTSTR)strLine, strLine.GetLength() * sizeof(TCHAR));
		}
		for (POSITION pos = liExcludedSharedDirs.GetHeadPosition(); pos != NULL;) {
			CString strLine = CString(kExcludedSharedDirectoryPrefix) + liExcludedSharedDirs.GetNext(pos) + _T("\r\n");
			file.Write((LPCTSTR)strLine, strLine.GetLength() * sizeof(TCHAR));
		}

		AsyncDiskWriteData* pData = new AsyncDiskWriteData;
		pData->lGeneration = ::InterlockedIncrement(const_cast<volatile LONG*>(&m_lSharedFilesSaveGeneration));
		pData->plGeneration = const_cast<volatile LONG*>(&m_lSharedFilesSaveGeneration);
		pData->strTempPath = strFullPath + _T(".tmp");
		pData->strFinalPath = strFullPath;
		pData->strLogName = SHAREDFILES_FILE;
		pData->strPayloadName = _T("shared-files-rules");
		pData->eConflictPolicy = AsyncDiskWriteConflictLastSnapshotWins;
		pData->eReplacePolicy = AsyncDiskWriteReplaceFinal;
		const ULONGLONG uLength = file.GetLength();
		if (uLength != 0)
			pData->data.assign(file.GetBuffer(), file.GetBuffer() + static_cast<size_t>(uLength));
		CPartFileWriteThread::QueueOrWriteDiskSnapshot(pData);
		m_sharedCache.Save(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	} catch (CFileException *ex) {
		DebugLogError(_T("Failed to save %s%s"), (LPCTSTR)strFullPath, (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
		ex->Delete();
	} catch (...) {
		DebugLogError(_T("Failed to save %s"), (LPCTSTR)EscPercent(strFullPath));
	}
}

void CSharedFileList::LoadSingleSharedFilesList()
{
	SetExplicitShareRulesLoaded(false);

	const CString &strFullPath(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + SHAREDFILES_FILE);
	const DWORD dwAttributes = ::GetFileAttributes(strFullPath);
	if (dwAttributes == INVALID_FILE_ATTRIBUTES) {
		const DWORD dwError = ::GetLastError();
		if (dwError == ERROR_FILE_NOT_FOUND || dwError == ERROR_PATH_NOT_FOUND) {
			SetExplicitShareRulesLoaded(true);
			return;
		}

		DebugLogError(_T("Failed to load %s, error %lu"), (LPCTSTR)EscPercent(strFullPath), dwError);
		return;
	}

	bool bLoaded = false;
	bool bIsUnicodeFile = IsUnicodeFile(strFullPath); // check for BOM
	CStdioFile sdirfile;
	if (sdirfile.Open(strFullPath, CFile::modeRead | CFile::shareDenyWrite | (bIsUnicodeFile ? CFile::typeBinary : 0))) {
		try {
			if (bIsUnicodeFile)
				sdirfile.Seek(sizeof(WORD), CFile::current); // skip BOM

			CString toadd;
			while (sdirfile.ReadString(toadd)) {
				toadd.Trim(_T(" \t\r\n")); // need to trim '\r' in binary mode
				if (toadd.IsEmpty())
					continue;

				const bool bExcludeFile = (toadd[0] == kExcludedSharedFilePrefix);
				const bool bExcludeDir = (toadd[0] == kExcludedSharedDirectoryPrefix);
				if (bExcludeFile || bExcludeDir)
					toadd.Delete(0, 1);

				if (bExcludeDir) {
					// Preserve excluded subdirectory rules even if the path is temporarily unavailable.
					AddExcludedSharedDirectory(toadd);
				} else if (DirAccsess(toadd)) {
					if (bExcludeFile)
						ExcludeFile(toadd);
					else
						AddSingleSharedFile(toadd, true);
				}
			}
			sdirfile.Close();
			bLoaded = true;
		} catch (CFileException *ex) {
			DebugLogError(_T("Failed to load %s: %s"), (LPCTSTR)EscPercent(strFullPath), (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
			ex->Delete();
		}
	} else
		DebugLogError(_T("Failed to load %s"), (LPCTSTR)EscPercent(strFullPath));

	SetExplicitShareRulesLoaded(bLoaded);
}

bool CSharedFileList::AddSingleSharedDirectory(const CString &rstrFilePath, bool bNoUpdate)
{
	const CString sDir(NormalizeDirectoryPath(rstrFilePath));

	// check if we share this dir already or are not allowed to
	if (!thePrefs.IsShareableDirectory(sDir))
		return false;

	const bool bWasExcluded = IsExcludedSharedDirectory(sDir);
	if (!bWasExcluded && IsSharedByDirectoryRules(sDir))
		return false;

	RemoveExcludedSharedDirectory(sDir, thePrefs.GetAutoShareSubdirs());
	if (!IsSharedByDirectoryRules(sDir))
		thePrefs.AddSharedDirectoryIfAbsent(sDir);
	InvalidateShareRuleSnapshot();

	if (!bNoUpdate)
		AddFilesFromDirectory(sDir);

	return true;
}

CString CSharedFileList::GetPseudoDirName(const CString &strDirectoryName)
{
	// Those pseudo names are sent to other clients when requesting shared files instead of
	// the full directory names to avoid giving away too much information about our local
	// file structure, which might be sensitive data in some cases.
	// But we still want to use a descriptive name so the information of files sorted by directories is not lost
	// In general we use only the name of the directory, shared subdirs keep the path up to
	// the highest shared dir. This way we never reveal the name of any indirectly shared directory.
	// Then we make sure it's unique.
	if (!ShouldBeShared(strDirectoryName, NULL, false)) {
		ASSERT(0);
		return CString();
	}
	// does the name already exist?
	CString strTmpPseudo, strTmpPath;
	for (POSITION pos = m_mapPseudoDirNames.GetStartPosition(); pos != NULL;) {
		m_mapPseudoDirNames.GetNextAssoc(pos, strTmpPseudo, strTmpPath);
		if (EqualPaths(strTmpPath, strDirectoryName))
			return CString();	// not sending the same directory again
	}

	// create a new Pseudoname
	CString strDirectoryTmp(strDirectoryName);
	unslosh(strDirectoryTmp);

	CString strPseudoName;
	int iPos;
	while ((iPos = strDirectoryTmp.ReverseFind(_T('\\'))) >= 0) {
		strPseudoName = strDirectoryTmp.Right(strDirectoryTmp.GetLength() - iPos) + strPseudoName;
		strDirectoryTmp.Truncate(iPos);
		if (!ShouldBeShared(strDirectoryTmp, NULL, false))
			break;
	}
	if (strPseudoName.IsEmpty()) {
		// must be a root directory
		ASSERT(strDirectoryTmp.GetLength() == 2);
		strPseudoName = strDirectoryTmp;
	} else {
		// remove first backslash
		ASSERT(strPseudoName[0] == _T('\\'));
		strPseudoName.Delete(0, 1);
	}
	// we have the name, make sure it is unique
	if (m_mapPseudoDirNames.PLookup(strPseudoName)) {
		CString strUnique;
		for (iPos = 2; ; ++iPos) {
			strUnique.Format(_T("%s_%i"), (LPCTSTR)strPseudoName, iPos);
			if (!m_mapPseudoDirNames.PLookup(strUnique)) {
				strPseudoName = strUnique;
				break;
			}
			if (iPos > 200) {
				// wth?
				ASSERT(0);
				return CString();
			}
		}
	}

	DebugLog(_T("Using Pseudoname %s for directory %s"), (LPCTSTR)EscPercent(strPseudoName), (LPCTSTR)EscPercent(strDirectoryName));
	m_mapPseudoDirNames[strPseudoName] = strDirectoryName;
	return strPseudoName;
}

CString CSharedFileList::GetDirNameByPseudo(const CString &strPseudoName) const
{
	CString strResult;
	m_mapPseudoDirNames.Lookup(strPseudoName, strResult);
	return strResult;
}

bool CSharedFileList::GetPopularityRank(const CKnownFile *pFile, uint32 &rnOutSession, uint32 &rnOutTotal) const
{
	if (GetFileByIdentifier(pFile->GetFileIdentifierC()) == NULL) {
		rnOutSession = 0;
		rnOutTotal = 0;
		return false;
	}
	UINT uAllTimeReq = pFile->statistic.GetAllTimeRequests();
	UINT uReq = pFile->statistic.GetRequests();

	// we start at rank #1, not 0
	rnOutSession = 1;
	rnOutTotal = 1;
	// cycle all files, each file which has more requests than the given file lowers the rank
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		if (pair->value != pFile) {
			rnOutTotal += static_cast<uint32>(pair->value->statistic.GetAllTimeRequests() > uAllTimeReq);
			rnOutSession += static_cast<uint32>(pair->value->statistic.GetRequests() > uReq);
		}

	return true;
}

CString CSharedFileList::BuildScanKey(const CString& fullpath, ULONGLONG size, const FILETIME& ftWrite)
{
	CString key;
	key.Format(_T("%s#%I64u#%u:%u"), (LPCTSTR)fullpath, size, (UINT)ftWrite.dwLowDateTime, (UINT)ftWrite.dwHighDateTime);
	return key;
}

void CSharedFileList::MarkScanSeen(const CString& key)
{
	void* dummy = (void*)1; // non-null dummy
	m_mapScanSeen.SetAt(key, dummy);
}

void CSharedFileList::UnmarkScanSeen(const CString& key)
{
	if (!key.IsEmpty())
		m_mapScanSeen.RemoveKey(key);
}

size_t CSharedFileList::PendingHashingCount() const
{
	return (size_t)m_mapScanSeen.GetCount();
}

bool CSharedFileList::IsAlreadySharedByPathNoCase(const CString& rstrFilePath)
{
	if (rstrFilePath.IsEmpty())
		return false;

	const CString strPathKey = BuildNoCasePathKey(rstrFilePath);
	CSingleLock listlock(&m_mutWriteList, TRUE);
	void* pvCachedFile = NULL;
	if (m_mapSharedPathsNoCase.Lookup(strPathKey, pvCachedFile) && pvCachedFile != NULL && pvCachedFile != reinterpret_cast<void*>(1))
		return true;

	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair)) {
		CKnownFile *pFile = pair->value;
		if (pFile != NULL && pFile->GetFilePath().CompareNoCase(rstrFilePath) == 0) {
			m_mapSharedPathsNoCase[strPathKey] = pFile;
			++m_uSharedPathCacheRevision;
			return true;
		}
	}

	return false;
}

void CSharedFileList::ReconcileMovedSharedFiles(const CStringArray& changedFiles)
{
	CMapStringToPtr seenPaths;
	const INT_PTR iExpectedCount = changedFiles.GetCount();
	const UINT uHashSize = static_cast<UINT>((iExpectedCount * 2 + 1 > 257) ? (iExpectedCount * 2 + 1) : 257);
	seenPaths.InitHashTable(uHashSize);

	for (INT_PTR i = 0; i < changedFiles.GetCount(); ++i) {
		const CString& strFilePath = changedFiles.GetAt(i);
		if (strFilePath.IsEmpty())
			continue;

		const CString strKey = BuildNoCasePathKey(strFilePath);
		void* pv = NULL;
		if (seenPaths.Lookup(strKey, pv))
			continue;

		seenPaths.SetAt(strKey, reinterpret_cast<void*>(1));
		TryReconcileMovedSharedFile(strFilePath);
	}
}


void CSharedFileList::NotifyFoundFilesEvent()
{
	InterlockedIncrement(&m_lFoundFilesNotify);
}

bool CSharedFileList::ShouldProcessFoundFilesTick()
{
	if (m_bSharedFilesCompletionActive)
		return true;
	if (m_bContinueFoundProcessing) {
		InterlockedExchange(&m_lFoundFilesNotify, 0); // Consume stale posts while continuing the current batch
		return true;
	}
	if (InterlockedExchange(&m_lFoundFilesNotify, 0) > 0)
		return true;
	if (m_searchThread == NULL)
		return false;
	if (m_searchThread->HasQueuedFoundFiles())
		return true;
	return !m_searchThread->IsBusy() && m_bSharedFilesCompletionPending;
}

static UINT ClampSharedStartupProgressToUInt(INT_PTR iValue)
{
	if (iValue <= 0)
		return 0;
	const INT_PTR iMax = static_cast<INT_PTR>(static_cast<UINT>(-1));
	return static_cast<UINT>(min(iValue, iMax));
}

void CSharedFileList::GetStartupScanProgress(UINT& uSharedFiles, UINT& uQueuedFoundFiles, UINT& uHashingFiles, UINT& uPendingFolders, UINT& uCompletionStep, bool& bScanning, bool& bCompleting)
{
	uSharedFiles = ClampSharedStartupProgressToUInt(m_Files_map.GetCount());
	uQueuedFoundFiles = 0;
	uHashingFiles = ClampSharedStartupProgressToUInt(GetHashingCount());
	uPendingFolders = 0;
	uCompletionStep = m_uSharedFilesCompletionStep;
	bCompleting = m_bSharedFilesCompletionActive;
	bScanning = false;
	if (m_searchThread != NULL)
		m_searchThread->GetProgressCounts(uPendingFolders, uQueuedFoundFiles, bScanning);
	if (m_bContinueFoundProcessing || m_bReloadLookupSnapshotActive || m_bStartupScanDeferred || (m_searchThread != NULL && !m_bStartupScanCompleted))
		bScanning = true;
}

void CSharedFileList::GetSharedFilesLoadProgress(UINT& uDone, UINT& uTotal, CString& strDetail)
{
	UINT uSharedFiles = 0;
	UINT uQueuedFoundFiles = 0;
	UINT uHashingFiles = 0;
	UINT uPendingFolders = 0;
	UINT uCompletionStep = 0;
	bool bScanning = false;
	bool bCompleting = false;
	GetStartupScanProgress(uSharedFiles, uQueuedFoundFiles, uHashingFiles, uPendingFolders, uCompletionStep, bScanning, bCompleting);

	if (bCompleting) {
		uDone = uCompletionStep < SharedFilesCompletionFinish ? uCompletionStep : SharedFilesCompletionFinish;
		uTotal = SharedFilesCompletionFinish;
		strDetail.Format(GetResString(_T("BULKOP_LOAD_SHAREDFILES_APPLY_PROGRESS_DETAIL")), uSharedFiles, uQueuedFoundFiles, uHashingFiles, uDone, uTotal);
		return;
	}

	uDone = uSharedFiles;
	uTotal = uSharedFiles + uQueuedFoundFiles + uHashingFiles + uPendingFolders;
	if (uTotal == 0)
		uTotal = uDone;
	strDetail.Format(GetResString(_T("BULKOP_LOAD_SHAREDFILES_SCAN_PROGRESS_DETAIL")), uSharedFiles, uQueuedFoundFiles, uHashingFiles, uPendingFolders);
}

void CSharedFileList::OnSharedFilesFound()
{
	UINT uProcessedInSlice = 0;
	CSharedFileListSearchThread::FoundFile* found = NULL;
	bool bHashQueueChanged = false;
	const DWORD dwSliceStart = ::GetTickCount();

	if (m_bSharedFilesCompletionActive) {
		UINT uCompletionProcessed = 0;
		INT_PTR iCompletionRemaining = 0;
		if (!ApplySharedFilesCompletionChunk(uCompletionProcessed, iCompletionRemaining)) {
			FinishSharedFilesCompletion();
			return;
		}
		DWORD dwSliceElapsed = 0;
		if (theApp.IsTimeBudgetHardExceeded(dwSliceStart, CemuleApp::TimeBudgetSharedFilesFound, &dwSliceElapsed))
			theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetSharedFilesFound, _T("CSharedFileList::OnSharedFilesCompletion"), dwSliceElapsed, uCompletionProcessed, iCompletionRemaining);
		if (m_bSharedFilesCompletionActive)
			m_bContinueFoundProcessing = true;
		return;
	}

	// Guard against transient null (e.g. during shutdown); drop stale callbacks safely.
	if (m_searchThread == NULL)
		return;

	m_bInFoundFilesProcessing = true; // Defer tree reload posts while processing found files

	SFoundFileShareRuleSnapshot shareRules;
	bool bShareRulesLoaded = false;
	auto EnsureShareRulesLoaded = [&]() {
		if (bShareRulesLoaded)
			return;

		shareRules.bAutoShareSubdirs = thePrefs.GetAutoShareSubdirs();
		shareRules.sIncoming = NormalizeDirectoryRulePath(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
		for (INT_PTR i = 1; i < thePrefs.GetCatCount(); ++i) {
			const CString sCatDir(NormalizeDirectoryRulePath(thePrefs.GetCatPath(i)));
			if (!sCatDir.IsEmpty())
				shareRules.liCategoryIncoming.AddTail(sCatDir);
		}
		CStringList liSharedDirs;
		thePrefs.CopySharedDirectoryList(liSharedDirs);
		for (POSITION pos = liSharedDirs.GetHeadPosition(); pos != NULL;) {
			const CString sSharedDir(NormalizeDirectoryRulePath(liSharedDirs.GetNext(pos)));
			if (!sSharedDir.IsEmpty())
				shareRules.liSharedDirs.AddTail(sSharedDir);
		}
		CStringList liSingleSharedFiles;
		CStringList liSingleExcludedFiles;
		CStringList liExcludedSharedDirs;
		CopyExplicitShareRules(liSingleSharedFiles, liSingleExcludedFiles, liExcludedSharedDirs);
		shareRules.bHasSingleSharedFiles = !liSingleSharedFiles.IsEmpty();
		shareRules.bHasSingleExcludedFiles = !liSingleExcludedFiles.IsEmpty();
		if (shareRules.bHasSingleSharedFiles)
			AddPathListToNoCaseMap(shareRules.mapSingleSharedFiles, liSingleSharedFiles);
		if (shareRules.bHasSingleExcludedFiles)
			AddPathListToNoCaseMap(shareRules.mapSingleExcludedFiles, liSingleExcludedFiles);
		for (POSITION pos = liExcludedSharedDirs.GetHeadPosition(); pos != NULL;) {
			const CString sExcludedDir(NormalizeDirectoryRulePath(liExcludedSharedDirs.GetNext(pos)));
			if (!sExcludedDir.IsEmpty())
				shareRules.liExcludedSharedDirs.AddTail(sExcludedDir);
		}
		bShareRulesLoaded = true;
	};

	const LONG lCurrentShareRuleGeneration = GetShareRuleGeneration();

	for (;;) {
		if (!m_searchThread->TryPopFoundFile(found)) {
			m_bContinueFoundProcessing = true;
			return;
		}
		if (found == NULL)
			break;
		++uProcessedInSlice;
		const CString& sSharedDir = found->linkdir.IsEmpty() ? found->dir : found->linkdir;
		bool bShouldShareFoundFile = true;
		if (found->ruleGeneration != lCurrentShareRuleGeneration) {
			EnsureShareRulesLoaded();
			if (shareRules.bHasSingleExcludedFiles && LookupNoCasePathKey(shareRules.mapSingleExcludedFiles, found->pathKey))
				bShouldShareFoundFile = false;
			else
				bShouldShareFoundFile = ShouldShareFoundFileBySnapshot(shareRules, sSharedDir, found->path);
		}

		if (!bShouldShareFoundFile) {
			delete found; // Ensure we do not leak skipped items
			if (uProcessedInSlice >= kSharedFilesFoundMaxFilesPerSlice || theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetSharedFilesFound)) {
				if (bHashQueueChanged)
					NotifyShowFilesCount();
				m_bContinueFoundProcessing = true;
				return;
			}
			continue;
		}

		CKnownFile* toadd = found->knownFile;
		if (toadd) {
			CCKey key(toadd->GetFileHash());
			CKnownFile* pFileInMap = NULL;
			const bool bFileAlreadyShared = m_Files_map.Lookup(key, pFileInMap) != FALSE;
			CKnownFile* pFileInDuplicatesList = found->duplicateFile;
			const bool bDuplicateOfCurrentSharedFile = pFileInDuplicatesList != NULL && bFileAlreadyShared && IsReloadFoundFileCurrent(pFileInMap);
			if (bDuplicateOfCurrentSharedFile) {
				{
					CSingleLock duplicatesLock(&theApp.knownfiles->m_csDuplicatesLock, TRUE);
					const CString strOldFilePath(pFileInDuplicatesList->GetFilePath());
					pFileInDuplicatesList->SetPath(found->dir);
					pFileInDuplicatesList->SetFilePath(found->path); // Update the file path in the duplicates list
					pFileInDuplicatesList->SetSharedDirectory(sSharedDir);
					UpdateSharedPathCacheByPath(strOldFilePath, pFileInDuplicatesList->GetFilePath());
				}
				RememberDuplicateSharedPath(found->path, pFileInDuplicatesList->GetFileHash(), found->date, static_cast<uint64>(found->size));
			} else if (bFileAlreadyShared) {
				if (!pFileInMap->IsKindOf(RUNTIME_CLASS(CPartFile)) || theApp.downloadqueue->IsPartFile(pFileInMap)) {
					if (pFileInMap == toadd && pFileInMap->GetFilePath().CompareNoCase(found->path) != 0 && !IsReloadFoundFileCurrent(pFileInMap)) {
						const CString strOldFilePath(pFileInMap->GetFilePath());
						pFileInMap->SetPath(found->dir);
						pFileInMap->SetFilePath(found->path);
						pFileInMap->SetSharedDirectory(sSharedDir);
						UpdateSharedPathCache(pFileInMap, strOldFilePath);
						StoreWebSharedFileSnapshot(pFileInMap);
						m_bSharedFilesModelChangedSinceListUpdate = true;
					} else if (pFileInMap->GetFilePath().CompareNoCase(found->path) != 0) { //is it actually really the same file in the same place we already share? if so don't bother too much
						if (!IsCachedDuplicateSharedPath(found->path, found->date, static_cast<uint64>(found->size), pFileInMap->GetFileHash()))
							LogWarning(GetResString(_T("ERR_DUPL_FILES2")), (LPCTSTR)EscPercent(pFileInMap->GetFilePath()), (LPCTSTR)EscPercent(found->path), (LPCTSTR)EscPercent(found->name));
						RememberDuplicateSharedPath(found->path, pFileInMap->GetFileHash(), found->date, static_cast<uint64>(found->size));
					}
				}
			} else {
				toadd->SetPath(found->dir);
				toadd->SetFilePath(found->path);
				toadd->SetSharedDirectory(found->linkdir.IsEmpty() ? found->dir : found->linkdir);
				AddFile(toadd);
			}
		} else {
			// not in knownfile list - start adding thread to hash file if the hashing of this file isn't already waiting
			// SLUGFILLER: SafeHash - don't double hash, MY way
			if (IsAlreadySharedByPathNoCase(found->path)) {
				// The path cache can be refreshed after the worker snapshot was taken.
			} else if (!IsHashingByPathKey(found->pathKey) && !thePrefs.IsTempFile(found->dir, found->name)){
				UnknownFile_Struct* tohash = new UnknownFile_Struct;
				tohash->strDirectory = found->dir;
				tohash->strName = found->name;
				tohash->strSharedDirectory = found->linkdir.IsEmpty() ? found->dir : found->linkdir;
				tohash->strPathKey = found->pathKey;
				waitingforhash_list.AddTail(tohash);
				m_mapHashingPathsNoCase.SetAt(tohash->strPathKey, (void*)1);
				bHashQueueChanged = true;
			} else {
				// Skip hot-path per-file debug logging for duplicate hash/temp candidates.
			}
			// SLUGFILLER: SafeHash
		}

		delete found;

		if (uProcessedInSlice >= kSharedFilesFoundMaxFilesPerSlice || theApp.IsTimeBudgetExceeded(dwSliceStart, CemuleApp::TimeBudgetSharedFilesFound)) {
			if (bHashQueueChanged)
				NotifyShowFilesCount();
			m_bContinueFoundProcessing = true;
			DWORD dwSliceElapsed = 0;
			if (theApp.IsTimeBudgetHardExceeded(dwSliceStart, CemuleApp::TimeBudgetSharedFilesFound, &dwSliceElapsed))
				theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetSharedFilesFound, _T("CSharedFileList::OnSharedFilesFound"), dwSliceElapsed, uProcessedInSlice, 0);
			return;
		}
	}

	if (bHashQueueChanged)
		NotifyShowFilesCount();

	m_bContinueFoundProcessing = false;

	// Refresh the GUI and start hashing, if we are done searching for new files to share.
	if (m_searchThread && !m_searchThread->IsBusy() && StartSharedFilesCompletion()) {
		UINT uCompletionProcessed = 0;
		INT_PTR iCompletionRemaining = 0;
		if (!ApplySharedFilesCompletionChunk(uCompletionProcessed, iCompletionRemaining)) {
			FinishSharedFilesCompletion();
			return;
		}
		uProcessedInSlice += uCompletionProcessed;
		DWORD dwSliceElapsed = 0;
		if (theApp.IsTimeBudgetHardExceeded(dwSliceStart, CemuleApp::TimeBudgetSharedFilesFound, &dwSliceElapsed))
			theApp.TraceTimeBudgetSlice(CemuleApp::TimeBudgetSharedFilesFound, _T("CSharedFileList::OnSharedFilesCompletion"), dwSliceElapsed, uProcessedInSlice, iCompletionRemaining);
		if (m_bSharedFilesCompletionActive)
			m_bContinueFoundProcessing = true;
	}
	else
		m_bInFoundFilesProcessing = false;
}


// Helper methods to safely (re)start and stop the search thread without double-deletion.
void CSharedFileList::StartSearchThread()
{
	// Start a fresh thread and reset coalescing state.
	m_searchThread = new CSharedFileListSearchThread();
	m_searchThread->SetOwner(this);
	m_searchThread->CreateThread();
	InterlockedExchange(&m_lFoundFilesNotify, 0);
	m_bContinueFoundProcessing = false;
}

void CSharedFileList::StopSearchThread()
{
	if (m_searchThread) {
		m_searchThread->PrepareForShutdown();
		// Cache handle before posting exit to avoid use-after-free when the thread auto-deletes.
		HANDLE hThread = m_searchThread->m_hThread;
		CancelSharedFilesSearchSynchronousIo(hThread);
		if (!m_searchThread->PostThreadMessageW(CSharedFileListSearchThread::SFS_EXIT, 0, 0) && thePrefs.GetLogUiResponsivenessEvents())
			AddDebugLogLine(DLP_LOW, false, _T("Shared files search thread exit message could not be posted. err=%lu\n"), ::GetLastError());
		if (hThread != NULL) {
			DWORD dwWait = WAIT_TIMEOUT;
			DWORD dwWaited = 0;
			while (dwWaited < kSharedFilesSearchThreadShutdownWaitMs) {
				dwWait = WaitForSingleObject(hThread, kSharedFilesSearchThreadShutdownSliceMs);
				if (dwWait != WAIT_TIMEOUT)
					break;
				dwWaited += kSharedFilesSearchThreadShutdownSliceMs;
				CancelSharedFilesSearchSynchronousIo(hThread);
			}
			if (dwWait != WAIT_OBJECT_0) {
				if (thePrefs.GetLogUiResponsivenessEvents())
					AddDebugLogLine(DLP_LOW, false, _T("Shared files search thread did not stop within shutdown budget. waited=%lu result=%lu\n"), dwWaited, dwWait);
				return;
			}
		}
		delete m_searchThread;
		m_searchThread = NULL;
	}
}

IMPLEMENT_DYNCREATE(CSharedFileListSearchThread, CWinThread)

namespace
{
	const LONG kMaxQueuedSharedFoundFiles = 4096;
	const DWORD kSharedFoundFilesBackpressureSleepMs = 10;
	const DWORD kSharedFoundFilesBackpressureNotifyMs = 100;
}

bool CSharedFileListSearchThread::HasQueuedFoundFiles()
{
	return InterlockedCompareExchange((LONG*)&m_lQueuedFoundFiles, 0, 0) > 0;
}

void CSharedFileListSearchThread::GetProgressCounts(UINT& uPendingFolders, UINT& uQueuedFoundFiles, bool& bBusy)
{
	uPendingFolders = ClampSharedStartupProgressToUInt(InterlockedCompareExchange((LONG*)&m_lPendingSearchPaths, 0, 0));
	uQueuedFoundFiles = ClampSharedStartupProgressToUInt(InterlockedCompareExchange((LONG*)&m_lQueuedFoundFiles, 0, 0));
	bBusy = m_busy;
}

bool CSharedFileListSearchThread::ShouldAbortWork(LONG lGeneration) const
{
	return IsExitRequested() || lGeneration != GetSearchGeneration() || theApp.IsClosing();
}

bool CSharedFileListSearchThread::WaitForFoundFileQueueRoom(LONG lGeneration)
{
	DWORD dwLastNotify = 0;
	for (;;) {
		if (ShouldAbortWork(lGeneration))
			return false;
		if (InterlockedCompareExchange((LONG*)&m_lQueuedFoundFiles, 0, 0) < kMaxQueuedSharedFoundFiles)
			return true;

		const DWORD dwNow = ::GetTickCount();
		if (dwLastNotify == 0 || dwNow - dwLastNotify >= kSharedFoundFilesBackpressureNotifyMs) {
			if (theApp.emuledlg != NULL)
				theApp.emuledlg->PostSharedFileListFoundFilesAsync();
			dwLastNotify = dwNow;
		}
		::Sleep(kSharedFoundFilesBackpressureSleepMs);
	}
}

void CSharedFileListSearchThread::ResetWork()
{
	CSingleLock lock(&m_mutex, TRUE); // Reset queued paths, found files and dedup caches without stopping the thread.
	InterlockedIncrement(&m_lSearchGeneration);
	InterlockedExchange(&m_lSnapshotGeneration, -1);
	m_searchPaths.RemoveAll(); // Clear pending search paths
	InterlockedExchange(&m_lPendingSearchPaths, 0);

	// Drain and delete any queued found files
	while (!m_foundFiles.IsEmpty()) {
		FoundFile* f = m_foundFiles.RemoveHead();
		delete f;
	}
	InterlockedExchange(&m_lQueuedFoundFiles, 0);

	// Clear dedup/visited state
	m_seenDuringSearch.RemoveAll();
	m_inQueue.RemoveAll();
	m_shareRuleSnapshot.Clear();

	// Allow next enqueue to notify
	m_notify = true;
	m_busy = false;
}

void CSharedFileListSearchThread::PrepareForShutdown()
{
	CSingleLock lock(&m_mutex, TRUE);
	InterlockedExchange(&m_lExitRequested, 1);
	InterlockedIncrement(&m_lSearchGeneration);
	InterlockedExchange(&m_lSnapshotGeneration, -1);
	m_searchPaths.RemoveAll();
	InterlockedExchange(&m_lPendingSearchPaths, 0);
	while (!m_foundFiles.IsEmpty())
		delete m_foundFiles.RemoveHead();
	InterlockedExchange(&m_lQueuedFoundFiles, 0);
	m_seenDuringSearch.RemoveAll();
	m_inQueue.RemoveAll();
	m_shareRuleSnapshot.Clear();
	m_notify = true;
	m_busy = false;
	m_owner = NULL;
}

void CSharedFileListSearchThread::CaptureShareRuleSnapshotLocked()
{
	m_shareRuleSnapshot.Clear();
	if (m_owner == NULL)
		return;
	CStringList sharedDirs;
	CStringList liSingleSharedFilesDummy;
	CStringList liSingleExcludedFiles;
	CStringList liExcludedSharedDirs;

	m_shareRuleSnapshot.lRuleGeneration = m_owner->GetShareRuleGeneration();
	m_shareRuleSnapshot.bAutoShareSubdirs = thePrefs.GetAutoShareSubdirs();
	m_shareRuleSnapshot.sIncoming = NormalizeDirectoryRulePath(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));

	for (INT_PTR i = 1; i < thePrefs.GetCatCount(); ++i) {
		CString sCatDir(NormalizeDirectoryRulePath(thePrefs.GetCatPath(i)));
		if (!sCatDir.IsEmpty())
			m_shareRuleSnapshot.liCategoryIncoming.AddTail(sCatDir);
	}

	thePrefs.CopySharedDirectoryList(sharedDirs);
	for (POSITION pos = sharedDirs.GetHeadPosition(); pos != NULL;) {
		CString sRoot(NormalizeDirectoryRulePath(sharedDirs.GetNext(pos)));
		if (!sRoot.IsEmpty())
			m_shareRuleSnapshot.liSharedDirs.AddTail(sRoot);
	}

	m_owner->CopyExplicitShareRules(liSingleSharedFilesDummy, liSingleExcludedFiles, liExcludedSharedDirs);
	m_shareRuleSnapshot.bHasSingleExcludedFiles = !liSingleExcludedFiles.IsEmpty();
	if (m_shareRuleSnapshot.bHasSingleExcludedFiles)
		AddPathListToNoCaseMap(m_shareRuleSnapshot.mapSingleExcludedFiles, liSingleExcludedFiles);
	for (POSITION pos = liExcludedSharedDirs.GetHeadPosition(); pos != NULL;) {
		CString sExcluded(NormalizeDirectoryRulePath(liExcludedSharedDirs.GetNext(pos)));
		if (!sExcluded.IsEmpty())
			m_shareRuleSnapshot.liExcludedSharedDirs.AddTail(sExcluded);
	}
}

bool CSharedFileListSearchThread::ShouldShareDirectoryBySnapshotLocked(const CString& sDirPath) const
{
	if (!m_shareRuleSnapshot.bAutoShareSubdirs)
		return false;

	const CString sDir(NormalizeDirectoryRulePath(sDirPath));
	if (sDir.IsEmpty())
		return false;

	if (sDir == m_shareRuleSnapshot.sIncoming || IsSameOrSubDirectoryOfRulePath(sDir, m_shareRuleSnapshot.sIncoming))
		return true;

	for (POSITION pos = m_shareRuleSnapshot.liCategoryIncoming.GetHeadPosition(); pos != NULL;) {
		const CString& sCatDir(m_shareRuleSnapshot.liCategoryIncoming.GetNext(pos));
		if (sDir == sCatDir || IsSameOrSubDirectoryOfRulePath(sDir, sCatDir))
			return true;
	}

	const int nSharedDepth = GetBestDirectoryRuleDepthSnapshot(m_shareRuleSnapshot.liSharedDirs, sDir, true);
	if (nSharedDepth < 0)
		return false;

	const int nExcludedDepth = GetBestDirectoryRuleDepthSnapshot(m_shareRuleSnapshot.liExcludedSharedDirs, sDir, true);
	return nSharedDepth >= nExcludedDepth;
}

void CSharedFileListSearchThread::BeginSearch(CString searchPath)
{
	if (theApp.IsClosing() || IsExitRequested())	// Don't start any last-minute search
		return;

	CSingleLock lock(&m_mutex, TRUE);
	const LONG lGeneration = GetSearchGeneration();
	if (InterlockedCompareExchange((LONG*)&m_lSnapshotGeneration, 0, 0) != lGeneration) {
		CaptureShareRuleSnapshotLocked();
		InterlockedExchange(&m_lSnapshotGeneration, lGeneration);
	}
	m_searchPaths.AddTail(searchPath);
	InterlockedIncrement(&m_lPendingSearchPaths);
	m_busy = true;
	PostThreadMessageW(SFS_SEARCH, 0, 0);
}

int CSharedFileListSearchThread::Run()
{
	CSingleLock ThreadRunningLock(&m_running, TRUE);
	CSingleLock ThreadListLock(&m_mutex);

	bool exit = false;
	while(!exit) {
		MSG msg;
		BOOL result = GetMessageW(&msg, NULL, SFS_EXIT, SFS_CLEANUP);
		if (result != -1) {
			CString searchPath;
			CString checkFile;
			switch(msg.message)
			{
			case SFS_SEARCH:
				ThreadListLock.Lock();
				m_busy = true;

				// If app is closing, abort any pending search work immediately.
				if (theApp.IsClosing() || IsExitRequested()) {
					m_searchPaths.RemoveAll();
					InterlockedExchange(&m_lPendingSearchPaths, 0);
					ThreadListLock.Unlock();
					m_busy = false;
					exit = IsExitRequested();
					break;
				}

				while(!m_searchPaths.IsEmpty())	{
					searchPath = m_searchPaths.RemoveHead();
					InterlockedDecrement(&m_lPendingSearchPaths);
					const LONG lGeneration = GetSearchGeneration();
					ThreadListLock.Unlock();

					// Do not skip overlong paths; PreparePathForWin32LongPath will add the needed prefix.

					// Prepare pattern and base directory.
					CString prepared = PreparePathForWin32LongPath(searchPath);
					CString baseDir(searchPath);
					int bs = baseDir.ReverseFind(_T('\\'));
					if (bs >= 0) baseDir = baseDir.Left(bs + 1); else baseDir.Empty();

					WIN32_FIND_DATA wfd = { 0 };
					HANDLE hFind = FindFirstFileExW(prepared, FindExInfoBasic, &wfd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
					if (hFind == INVALID_HANDLE_VALUE) {
						DWORD const dwError = GetLastError();
						if (dwError != ERROR_FILE_NOT_FOUND)
							LogWarning(GetResString(_T("ERR_SHARED_DIR")), (LPCTSTR)EscPercent(searchPath), (LPCTSTR)EscPercent(GetErrorMessage(dwError)));
						ThreadListLock.Lock();
						continue;
					}

					do {
						if (ShouldAbortWork(lGeneration))
							break; // Abort enumeration promptly on shutdown

						CheckSingleFile(wfd, baseDir, lGeneration);
					} while (FindNextFileW(hFind, &wfd));
					FindClose(hFind);

					ThreadListLock.Lock();

					if (ShouldAbortWork(lGeneration)) {
						// A reset can invalidate the path currently being enumerated while a newer scan is already queued.
						// Keep and process newer queued paths unless the application is shutting down.
						if (theApp.IsClosing() || IsExitRequested()) {
							m_searchPaths.RemoveAll();
							InterlockedExchange(&m_lPendingSearchPaths, 0);
							break;
						}
						continue;
					}
				}
				ThreadListLock.Unlock();

				// Force notification to ensure post processing
				ThreadListLock.Lock();
				m_busy = false;
				if (m_foundFiles.IsEmpty()) {
					m_inQueue.RemoveAll();
					m_seenDuringSearch.RemoveAll();
				}

				if (m_notify || !m_foundFiles.IsEmpty()) {
					theApp.emuledlg->PostSharedFileListFoundFilesAsync();
					m_notify=false;
				}

				ThreadListLock.Unlock();
				if (IsExitRequested())
					exit = true;
				break;
			case SFS_CLEANUP:
				ThreadListLock.Lock();
				if (!m_busy && m_searchPaths.IsEmpty() && m_foundFiles.IsEmpty()) {
					m_inQueue.RemoveAll();
					m_seenDuringSearch.RemoveAll();
				}
				ThreadListLock.Unlock();
				break;
			case SFS_EXIT:
			case WM_QUIT:
				exit = true;
				break;
			}
		} else
			DebugLogError(_T("ERROR: Searching shared files failed with error code %lu"), GetLastError());
	}

	while(FoundFile* found = PopFoundFile())
		delete found;

	return 0;
}

void CSharedFileListSearchThread::CheckSingleFile(const WIN32_FIND_DATA& wfd, const CString& rootDir, LONG lGeneration)
{
	CSingleLock lock(&m_mutex); // Protect queue state and rule snapshots against reset/shutdown.
	if (ShouldAbortWork(lGeneration))
		return;

	// Build plain (non-\\?\) directory for stable keys and UI.
	auto StripLongPrefix = [](const CString& s) -> CString {
		// Avoid locale locks and unnecessary temporaries; compare raw characters safely.
		const TCHAR* psz = (LPCTSTR)s;
		const int len = s.GetLength();
		if (len >= 8 && _tcsncmp(psz, _T("\\\\?\\UNC\\"), 8) == 0) {
			CString out(_T("\\\\"));
			out += (psz + 8);
			return out;
		}

		if (len >= 4 && _tcsncmp(psz, _T("\\\\?\\"), 4) == 0)
			return CString(psz + 4);

		return s;
	};

	CString dir = rootDir;
	if (!dir.IsEmpty() && dir[dir.GetLength() - 1] != _T('\\'))
		dir += _T('\\');

	CString plainDir = StripLongPrefix(dir);
	CString name(wfd.cFileName);
	CString fullpath = plainDir + name;

	// Do not mark the parent directory as visited here; doing so would prematurely stop
	// processing further entries in the same folder. We only use the visited-set for subdirs.
	if ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
		if (name != _T(".") && name != _T("..") && !CSharedCache::ShouldIgnoreDirectoryName(name) && (wfd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) == 0) {
			CString subDir = plainDir + name; // No trailing backslash, no wildcard.
			CString subSearch(subDir);
			PathAddBackslash(subSearch.GetBuffer(subSearch.GetLength() + 1));
			subSearch.ReleaseBuffer();
			subSearch += _T("*");

			// Skip offline directories only; allow reparse points (visited-set prevents loops).
			const DWORD attr = wfd.dwFileAttributes;
			if ((attr & FILE_ATTRIBUTE_OFFLINE) != 0) {
				AddDebugLogLine(DLP_LOW, false, _T("%hs: Skipping directory \"%s\" (offline)\n"), __FUNCTION__, (LPCTSTR)EscPercent(subSearch));
				return;
			}

			// Use visited-set for the subdirectory itself (not the parent), to avoid loops.
			CString visitKey;
			const CString longSubDir = PreparePathForWin32LongPath(subDir);
			HANDLE hSub = ::CreateFile(longSubDir, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
			if (hSub != INVALID_HANDLE_VALUE) {
				BY_HANDLE_FILE_INFORMATION bhfi = {};
				if (::GetFileInformationByHandle(hSub, &bhfi))
					visitKey.Format(_T("ID:%08lX-%08lX%08lX"), bhfi.dwVolumeSerialNumber, bhfi.nFileIndexHigh, bhfi.nFileIndexLow);
				::CloseHandle(hSub);
			}
			if (visitKey.IsEmpty())
				visitKey = _T("PATH:") + subDir; // Fallback to path-based key.

			lock.Lock();
			if (ShouldAbortWork(lGeneration) || !ShouldShareDirectoryBySnapshotLocked(subDir)) {
				lock.Unlock();
				return;
			}

			void* pv = NULL;
			if (m_seenDuringSearch.Lookup(visitKey, pv)) {
				lock.Unlock();
				return; // Already queued/visited.
			}

			m_seenDuringSearch.SetAt(visitKey, (void*)1);
			m_searchPaths.AddTail(subSearch);
			InterlockedIncrement(&m_lPendingSearchPaths);
			lock.Unlock();
		}
		return;
	}

	// Basic file validation
	ULONGLONG ullFoundFileSize = (static_cast<ULONGLONG>(wfd.nFileSizeHigh) << 32) | static_cast<ULONGLONG>(wfd.nFileSizeLow);
	if (name == _T(".") || name == _T("..") || (wfd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0 || (wfd.dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY) != 0 || ullFoundFileSize == 0 || ullFoundFileSize > MAX_EMULE_FILE_SIZE)
		return;
	if (CSharedCache::ShouldIgnoreFileName(name))
		return;

	CSingleLock DontShareExtListLock(&thePrefs.m_csDontShareExtList, TRUE);
	if (thePrefs.GetDontShareExtensions() && isExtBanned(CString(name).MakeLower(), thePrefs.GetDontShareExtensionsList().MakeLower()))
		return;

	DontShareExtListLock.Unlock();

	CString strFoundFileName(name);
	CString strFoundFilePath(fullpath);
	CString strFoundDirectory(plainDir); // With backslash
	CString strShellLinkDir;
	FILETIME tFoundFileTime = wfd.ftLastWriteTime;

	// Try to resolve .lnk if configured
	if (ExtensionIs(strFoundFileName, _T(".lnk"))) {
		SHFILEINFO info;
		if (::SHGetFileInfo(strFoundFilePath, 0, &info, sizeof(info), SHGFI_ATTRIBUTES) && (info.dwAttributes & SFGAO_LINK)) {
			if (!thePrefs.GetResolveSharedShellLinks()) {
				AddDebugLogLine(DLP_LOW, false, _T("%hs: Did not share file \"%s\" - not supported file type\n"), __FUNCTION__, (LPCTSTR)EscPercent(strFoundFilePath));
				return;
			}

			WIN32_FILE_ATTRIBUTE_DATA fad = {};
			CString resolved;
			if (!ResolveShellLinkTargetModern(strFoundFilePath, resolved, fad))
				return;

			strShellLinkDir = strFoundDirectory;
			strFoundFilePath = resolved;
			const int p = strFoundFilePath.ReverseFind(_T('\\'));
			if (p < 0)
				return;
			strFoundDirectory = CString(strFoundFilePath, p + 1);
			strFoundFileName = strFoundFilePath.Mid(p + 1);
			ullFoundFileSize = (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | static_cast<ULONGLONG>(fad.nFileSizeLow);
			tFoundFileTime = fad.ftLastWriteTime;
			if ((fad.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_TEMPORARY)) != 0 || ullFoundFileSize == 0 || ullFoundFileSize > MAX_EMULE_FILE_SIZE)
				return;
			if (CSharedCache::ShouldIgnoreFileName(strFoundFileName))
				return;
			CSingleLock DontShareExtTargetListLock(&thePrefs.m_csDontShareExtList, TRUE);
			if (thePrefs.GetDontShareExtensions() && isExtBanned(CString(strFoundFileName).MakeLower(), thePrefs.GetDontShareExtensionsList().MakeLower()))
				return;
		}
	}

	// Ignore real(!) thumbs.db files -- seems that lot of ppl have 'thumbs.db' files without the 'System' file attribute
	if (IsThumbsDb(strFoundFilePath, strFoundFileName)) {
		AddDebugLogLine(DLP_LOW, false, _T("%hs: Did not share file \"%s\" - not supported file type\n"), __FUNCTION__, (LPCTSTR)EscPercent(strFoundFilePath));
		return;
	}

	time_t fdate = (time_t)FileTimeToUnixTime(tFoundFileTime);
	if (fdate <= 0)
		fdate = (time_t)-1;
	if (fdate == (time_t)-1) {
		if (thePrefs.GetVerbose())
			AddDebugLogLine(DLP_LOW, false, _T("Failed to get file date of \"%s\""), (LPCTSTR)EscPercent((strFoundFilePath)));
	} else
		AdjustNTFSDaylightFileTime(fdate, strFoundFilePath);

	const CString strFoundFileQueueKey = BuildNoCasePathKey(strFoundFilePath);

	lock.Lock();
	if (ShouldAbortWork(lGeneration)) {
		lock.Unlock();
		return;
	}

	void* pv = NULL;
	if (m_shareRuleSnapshot.bHasSingleExcludedFiles && m_shareRuleSnapshot.mapSingleExcludedFiles.Lookup(strFoundFileQueueKey, pv)) {
		lock.Unlock();
		return;
	}
	lock.Unlock();

	if (m_owner != NULL && m_owner->TrackScannedSharedFile(strFoundFilePath, strFoundFileName, fdate, static_cast<uint64>(ullFoundFileSize)))
		return;

	if (!WaitForFoundFileQueueRoom(lGeneration))
		return;

	CKnownFile* pResolvedKnownFile = NULL;
	CKnownFile* pResolvedDuplicateFile = NULL;
	if (m_owner != NULL)
		pResolvedKnownFile = m_owner->FindKnownFileFromSharedCache(strFoundFilePath, fdate, static_cast<uint64>(ullFoundFileSize));
	if (theApp.knownfiles != NULL) {
		if (pResolvedKnownFile == NULL)
			pResolvedKnownFile = theApp.knownfiles->FindKnownFileForSharedScan(strFoundFileName, fdate, static_cast<uint64>(ullFoundFileSize));
		if (pResolvedKnownFile != NULL)
			pResolvedDuplicateFile = theApp.knownfiles->IsOnDuplicatesForSharedScan(pResolvedKnownFile->GetFileName(), pResolvedKnownFile->GetUtcFileDate(), pResolvedKnownFile->GetFileSize());
	}

	lock.Lock();
	if (ShouldAbortWork(lGeneration)) {
		lock.Unlock();
		return;
	}

	pv = NULL;
	if (!m_inQueue.Lookup(strFoundFileQueueKey, pv)) {
		m_foundFiles.AddTail(new FoundFile(strFoundFileName, strFoundFilePath, strFoundDirectory, strShellLinkDir, fdate, ullFoundFileSize, strFoundFileQueueKey, m_shareRuleSnapshot.lRuleGeneration, pResolvedKnownFile, pResolvedDuplicateFile));
		m_inQueue.SetAt(strFoundFileQueueKey, (void*)1);
		InterlockedIncrement(&m_lQueuedFoundFiles);
		if (m_notify) {
			theApp.emuledlg->PostSharedFileListFoundFilesAsync();
			m_notify = false;
		}
	}

	lock.Unlock();
}

HANDLE OpenImportSourceLongPath(LPCTSTR path, uint64& outFileSize)
{
	outFileSize = 0;

	if (!path || !*path) {
		LogError(LOG_STATUSBAR, GetResString(_T("IMPORTPARTS_SOURCE_EMPTY")));
		return INVALID_HANDLE_VALUE;
	}

	const CString raw(path);
	if (!IsWin32LongPathsEnabled() && raw.GetLength() >= MAX_PATH) {
		LogWarning(LOG_STATUSBAR, GetResString(_T("IMPORTPARTS_SOURCE_PATH_TOO_LONG")), (LPCTSTR)EscPercent(raw), (UINT)raw.GetLength());
		return INVALID_HANDLE_VALUE;
	}

	const CString longPath = PreparePathForWin32LongPath(raw);

	HANDLE h = ::CreateFile(longPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		LogError(LOG_STATUSBAR, GetResString(_T("IMPORTPARTS_ERR_CANTOPENFILE")), (LPCTSTR)EscPercent(raw));
		return INVALID_HANDLE_VALUE;
	}

	LARGE_INTEGER li; li.QuadPart = 0;
	if (!::GetFileSizeEx(h, &li)) {
		LogError(LOG_STATUSBAR, GetResString(_T("FILE_SIZE_GET_FAILED")), (LPCTSTR)EscPercent(raw), (LPCTSTR)EscPercent(GetErrorMessage(::GetLastError())));
		::CloseHandle(h);
		return INVALID_HANDLE_VALUE;
	}

	outFileSize = (uint64)li.QuadPart;
	return h;
}
