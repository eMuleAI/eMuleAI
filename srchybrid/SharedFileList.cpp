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
#include "Log.h"
#include "Collection.h"
#include "kademlia/kademlia/UDPFirewallTester.h"
#include "md5sum.h"
#include "UserMsgs.h"
#include "MuleStatusBarCtrl.h"
#include "OtherFunctions.h"

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

	CString BuildNoCasePathKey(const CString& path)
	{
		CString key(path);
		key.MakeLower();
		return key;
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

	CString NormalizeDirectoryRulePath(const CString& strDirPath)
	{
		CString sDir(strDirPath);
		if (!sDir.IsEmpty())
			slosh(sDir);
		return sDir;
	}

	int GetBestDirectoryRuleDepthSnapshot(const CStringList& liDirs, const CString& sDirPath, bool bIncludeSubdirectories)
	{
		const CString sDir(NormalizeDirectoryRulePath(sDirPath));
		if (sDir.IsEmpty())
			return -1;

		int nBestDepth = -1;
		for (POSITION pos = liDirs.GetHeadPosition(); pos != NULL;) {
			const CString& sRule(liDirs.GetNext(pos));
			if (EqualPaths(sRule, sDir) || (bIncludeSubdirectories && IsSubDirectoryOf(sDir, sRule)))
				nBestDepth = max(nBestDepth, sRule.GetLength());
		}

		return nBestDepth;
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

	void BuildEServerBuddyMagicAnnounceEntries(std::vector<EServerBuddyMagicAnnounceEntry>& entries)
	{
		entries.clear();
		if (!ShouldIncludeEServerBuddyMagicFile())
			return;

		const uint32 uEpoch = CUpDownClient::GetEServerBuddyMagicEpoch();
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
		CComQIPtr<IPersistFile> pPersistFile = pShellLink;
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
			CComQIPtr<IPersistFile> pPersistFile2 = pShellLink2;
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
		if (m_aFiles.Find(pFile) >= 0) {
			ASSERT(0);
			return FALSE;
		}
		return m_aFiles.Add(pFile);
	}

	int RemoveRef(CKnownFile *pFile)
	{
		m_aFiles.Remove(pFile);
		return m_aFiles.GetSize();
	}

	void RemoveAllReferences()
	{
		m_aFiles.RemoveAll();
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

	INT_PTR GetCount() const								{ return m_lstKeywords.GetCount(); }

	CPublishKeyword *GetNextKeyword();
	void ResetNextKeyword();

	time_t GetNextPublishTime() const						{ return m_tNextPublishKeywordTime; }
	void SetNextPublishTime(time_t tNextPublishKeywordTime)	{ m_tNextPublishKeywordTime = tNextPublishKeywordTime; }

#ifdef _DEBUG
	void Dump();
#endif

protected:
	// can't use a CMap - too many disadvantages in processing the 'list'
	//CTypedPtrMap<CMapStringToPtr, CString, CPublishKeyword*> m_lstKeywords;
	CTypedPtrList<CPtrList, CPublishKeyword*> m_lstKeywords;
	POSITION m_posNextKeyword;
	time_t m_tNextPublishKeywordTime;

	CPublishKeyword *FindKeyword(const CStringW &rstrKeyword, POSITION *ppos = NULL) const;
};

CPublishKeywordList::CPublishKeywordList()
{
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

CPublishKeyword *CPublishKeywordList::FindKeyword(const CStringW &rstrKeyword, POSITION *ppos) const
{
	for (POSITION pos = m_lstKeywords.GetHeadPosition(); pos != NULL;) {
		POSITION posLast = pos;
		CPublishKeyword *pPubKw = m_lstKeywords.GetNext(pos);
		if (pPubKw->GetKeyword() == rstrKeyword) {
			if (ppos)
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
			m_lstKeywords.RemoveAt(posLast);
			delete pPubKw;
			SetNextPublishTime(0);
		}
	}
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
{
}

void CAddFileThread::SetValues(CSharedFileList *pOwner, LPCTSTR directory, LPCTSTR filename, LPCTSTR strSharedDir, CPartFile *partfile)
{
	m_pOwner = pOwner;
	m_strDirectory = directory;
	m_strFilename = filename;
	m_partfile = partfile;
	m_strSharedDir = strSharedDir;
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
	uint64 fileSize = 0;
	HANDLE hImport = OpenImportSourceLongPath(m_strImport, fileSize);
	if (hImport == INVALID_HANDLE_VALUE)
		return false;

	CString strFilePath;
	_tmakepathlimit(strFilePath.GetBuffer(MAX_PATH), NULL, m_strDirectory, m_strFilename, NULL);
	strFilePath.ReleaseBuffer();

	Log(LOG_STATUSBAR, GetResString(_T("IMPORTPARTS_IMPORTSTART")), m_PartsToImport.GetSize(), (LPCTSTR)strFilePath);

	BYTE *partData = NULL;
	unsigned partsuccess = 0;
	CKnownFile kfimport;

	for (INT_PTR i = 0; i < m_PartsToImport.GetSize(); ++i) {
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
					LogWarning(LOG_STATUSBAR, _T("Part %u: Seek failed for import source - %s"), (unsigned)partnumber, (LPCTSTR)EscPercent(GetErrorMessage(::GetLastError())));
					continue;
				}

				DWORD _dwRead = 0;
				if (!::ReadFile(hImport, partData, PARTSIZE, &_dwRead, NULL)) {
					LogWarning(LOG_STATUSBAR, _T("Part %u: Read failed for import source - %s"), (unsigned)partnumber, (LPCTSTR)EscPercent(GetErrorMessage(::GetLastError())));
					continue;
				}

				partSize = (uint32)_dwRead;

				if (*(uint64*)partData == 0 && (partSize <= sizeof(uint64) || !memcmp(partData, partData + sizeof(uint64), partSize - sizeof(uint64))))
					continue;
			} catch (...) {
				LogWarning(LOG_STATUSBAR, _T("Part %i: Not accessible (You may have a bad cluster on your hard disk)."), (int)partnumber);
				continue;
			}
			uchar hash[MDX_DIGEST_SIZE];
			kfimport.CreateHash(partData, partSize, hash);
			ImportPart_Struct *importpart = new ImportPart_Struct;
			importpart->start = uStart;
			importpart->end = importpart->start + partSize - 1;
			importpart->data = partData;
			if (!theApp.emuledlg->PostMessage(TM_IMPORTPART, (WPARAM)importpart, (LPARAM)m_partfile))
				break;
			partData = NULL; // Will be deleted in async write thread
			++partsuccess;

			if (theApp.IsRunning()) {
				WPARAM uProgress = (WPARAM)(i * 100 / m_PartsToImport.GetSize());
				VERIFY(theApp.emuledlg->PostMessage(TM_FILEOPPROGRESS, uProgress, (LPARAM)m_partfile));
				::Sleep(100);
			}

			if (!theApp.IsRunning() || partSize != PARTSIZE || m_partfile->GetFileOp() != PFOP_IMPORTPARTS)
				break;
		} catch (...) {
		}
	}

	if (hImport != INVALID_HANDLE_VALUE)
		::CloseHandle(hImport);

	delete[] partData;

	try {
		bool importaborted = !theApp.IsRunning() || m_partfile->GetFileOp() == PFOP_NONE;
		if (m_partfile->GetFileOp() == PFOP_IMPORTPARTS)
			m_partfile->SetFileOp(PFOP_NONE);
		Log(LOG_STATUSBAR, _T("Import %s. %u parts imported to %s.")
			, importaborted ? _T("aborted") : _T("completed")
			, partsuccess
			, (LPCTSTR)m_strFilename);
	} catch (...) {
		// This could happen if we deleted the part file instance
	}

	return true;
}

BOOL CAddFileThread::InitInstance()
{
	return TRUE;
}

int CAddFileThread::Run()
{
	DbgSetThreadName(m_partfile && m_partfile->GetFileOp() == PFOP_IMPORTPARTS ? "ImportingParts %s" : "Hashing %s", (LPCTSTR)m_strFilename);
	if (!(m_pOwner || m_partfile) || m_strFilename.IsEmpty() || theApp.IsClosing())
		return 0;

	(void)CoInitialize(NULL);

	if (m_partfile && m_partfile->GetFileOp() == PFOP_IMPORTPARTS) {
		ImportParts();
		CoUninitialize();
		return 0;
	}

	// Locking this hashing thread is needed because we may create a few of those threads
	// at startup when rehashing potentially corrupted downloading part files.
	// If all those hash threads would run concurrently, the I/O system would be under
	// very heavy load and slowly progressing
	CSingleLock hashingLock(&theApp.hashing_mut, TRUE); // hash only one file at a time

	if (theApp.IsClosing())
		return 0;

	TCHAR strFilePath[MAX_PATH];
	_tmakepathlimit(strFilePath, NULL, m_strDirectory, m_strFilename, NULL);
	if (m_partfile)
		Log(_T("%s \"%s\" \"%s\""), (LPCTSTR)GetResString(_T("HASHINGFILE")), (LPCTSTR)EscPercent(m_partfile->GetFileName()), (LPCTSTR)EscPercent(strFilePath));
	else
		Log(_T("%s \"%s\""), (LPCTSTR)GetResString(_T("HASHINGFILE")), (LPCTSTR)EscPercent(strFilePath));

	CKnownFile *newKnown = new CKnownFile();
	if (!theApp.IsClosing() && newKnown->CreateFromFile(m_strDirectory, m_strFilename, m_partfile)) { // SLUGFILLER: SafeHash - in case of shutdown while still hashing
		newKnown->SetSharedDirectory(m_strSharedDir);
		if (m_partfile && m_partfile->GetFileOp() == PFOP_HASHING)
			m_partfile->SetFileOp(PFOP_NONE);
		if (theApp.IsClosing() || !::IsWindow(theApp.emuledlg->m_hWnd)) {
			delete newKnown;
		} else if (!theApp.emuledlg->PostMessage(TM_FINISHEDHASHING, (m_pOwner ? 0 : (WPARAM)m_partfile), (LPARAM)newKnown)) {
			delete newKnown;
		}
	} else {
		if (!theApp.IsClosing()) {
			if (m_partfile && m_partfile->GetFileOp() == PFOP_HASHING)
				m_partfile->SetFileOp(PFOP_NONE);

			// SLUGFILLER: SafeHash - inform main program of hash failure
			if (m_pOwner) {
				UnknownFile_Struct *hashed = new UnknownFile_Struct;
				hashed->strDirectory = m_strDirectory;
				hashed->strName = m_strFilename;
				if (!theApp.emuledlg->PostMessage(TM_HASHFAILED, 0, (LPARAM)hashed))
					delete hashed;
			}
		}
		// SLUGFILLER: SafeHash
		delete newKnown;
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

CSharedFileList::CSharedFileList(CServerConnect *in_server)
	: server(in_server)
	, output()
	, m_currFileSrc()
	, m_currFileNotes()
	, m_lastPublishKadSrc()
	, m_lastPublishKadNotes()
	, m_lastPublishED2K()
	, m_lastPublishED2KFlag(true)
	, bHaveSingleSharedFiles()
	, pRebuildMetaDataThread()
	, m_uMetadataUpdatingCount()
	, m_bInFoundFilesProcessing(false)
	, m_bTreeReloadPending(false)
	, m_bReloadLookupSnapshotActive(false)
{
	m_Files_map.InitHashTable(1031);
	m_ReloadLookupFiles_map.InitHashTable(1031);
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

	StartSearchThread();
	LoadSingleSharedFilesList();
	FindSharedFiles();
}

CSharedFileList::~CSharedFileList()
{
	while (!waitingforhash_list.IsEmpty())
		delete waitingforhash_list.RemoveHead();
	// SLUGFILLER: SafeHash
	while (!currentlyhashing_list.IsEmpty())
		delete currentlyhashing_list.RemoveHead();
	// SLUGFILLER: SafeHash
	delete m_keywords;

#if defined(_BETA) || defined(_DEVBUILD)
	//Delete the test file
	CString sTest(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
	sTest += m_strBetaFileName;
	::DeleteFile(sTest);
#endif
	if (m_searchThread != NULL) {
		// Ask the worker to abandon any in-progress scan before waiting for full termination.
		m_searchThread->PrepareForShutdown();
		m_searchThread->PostThreadMessageW(CSharedFileListSearchThread::SFS_EXIT, 0, 0);
		HANDLE hThread = m_searchThread->m_hThread;
		if (hThread != NULL && hThread != INVALID_HANDLE_VALUE) {
			const DWORD dwWait = WaitForSingleObject(hThread, 5000);
			if (dwWait != WAIT_OBJECT_0) {
				TRACE(_T("Shared files search thread did not stop within timeout during shutdown; leaving it to process exit.\n"));
				m_searchThread = NULL;
				return;
			}
		}
		delete m_searchThread;
		m_searchThread = NULL;
	}
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
	CopySharedFileMap(m_ReloadLookupFiles_map);
	m_bReloadLookupSnapshotActive = !m_ReloadLookupFiles_map.IsEmpty();
}

void CSharedFileList::EndReloadLookupSnapshot()
{
	CSingleLock listlock(&m_mutWriteList, TRUE);
	m_ReloadLookupFiles_map.RemoveAll();
	m_bReloadLookupSnapshotActive = false;
}

void CSharedFileList::FindSharedFiles()
{
	if (theApp.downloadqueue) {
		if (!m_Files_map.IsEmpty()) {
			CSingleLock listlock(&m_mutWriteList);

			CCKey key;
			for (POSITION pos = m_Files_map.GetStartPosition(); pos != NULL;) {
				CKnownFile *cur_file;
				m_Files_map.GetNextAssoc(pos, key, cur_file);
				if (!cur_file->IsKindOf(RUNTIME_CLASS(CPartFile))
					|| theApp.downloadqueue->IsPartFile(cur_file)
					|| theApp.knownfiles->IsFilePtrInList(cur_file)
					|| _taccess(cur_file->GetFilePath(), 0) != 0)
				{
					m_UnsharedFiles_map[CSKey(cur_file->GetFileHash())] = true;
					listlock.Lock();
					m_mapSharedPathsNoCase.RemoveKey(BuildNoCasePathKey(cur_file->GetFilePath()));
					m_Files_map.RemoveKey(key);
					listlock.Unlock();
				}
			}
		}

		// Keep ready part files in the shared map for both startup and reload scans.
		theApp.downloadqueue->AddPartFilesToShare();
	}

	// khaos::kmod+ Fix: Shared files loaded multiple times.
	const CString &tempDir(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
	CStringList sharedDirs;
	thePrefs.CopySharedDirectoryList(sharedDirs);
	CMapStringToPtr mapAddedDirs;
	const INT_PTR iExpectedDirCount = sharedDirs.GetCount() * 2 + thePrefs.GetCatCount() + 8;
	mapAddedDirs.InitHashTable(static_cast<UINT>((iExpectedDirCount > 257) ? iExpectedDirCount : 257));

#if defined(_BETA) || defined(_DEVBUILD)
	//Create the test file (before adding the Incoming directory)
	CStdioFile f;
	if (!f.Open(tempDir + m_strBetaFileName, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite))
		ASSERT(0);
	else {
		try {
			// do not translate the content!
			f.WriteString(m_strBetaFileName); // guarantees a different hash on different versions
			f.WriteString(_T("\nThis file is automatically created by eMule Beta versions to help the developers testing and debugging the new features.")
				_T("\neMule will delete this file when exiting, otherwise you can remove this file at any time.")
				_T("\nThanks for beta testing eMule :)"));
			f.Close();
		} catch (CFileException *ex) {
			ASSERT(0);
			ex->Delete();
		}
	}
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
	{
		CSingleLock lock(&m_csShareRules, TRUE);
		bExclude = RemovePathNoCase(m_liSingleExcludedFiles, rstrFilePath);
	}

	// check if we share this file in general
	bool bShared = ShouldBeShared(rstrFilePath.Left(rstrFilePath.ReverseFind(_T('\\'))), rstrFilePath, false);

	if (bShared && !bExclude)
		return false; // we should be sharing this file already
	if (!bShared) {
		CSingleLock lock(&m_csShareRules, TRUE);
		if (!ContainsPathNoCase(m_liSingleSharedFiles, rstrFilePath))
			m_liSingleSharedFiles.AddTail(rstrFilePath); // the directory is not shared, so we need a new entry
		bHaveSingleSharedFiles = !m_liSingleSharedFiles.IsEmpty();
	}

	return bNoUpdate || CheckAndAddSingleFile(rstrFilePath);
}

bool CSharedFileList::CheckAndAddSingleFile(const CString& rstrFilePath)
{
	{
		CSingleLock lock(&m_csShareRules, TRUE);
		bHaveSingleSharedFiles = true;
	}
	m_searchThread->BeginSearch(rstrFilePath);
	// GUI updating needs to be done by caller
	return true; // This is probably not true anymore, but shouldn't hurt that much
}

bool CSharedFileList::SafeAddKFile(CKnownFile* toadd, bool bOnlyAdd)
{
	RemoveFromHashing(toadd);	// SLUGFILLER: SafeHash - hashed OK, remove from list if it was in
	bool bAdded = AddFile(toadd);
	if (!bOnlyAdd) {
		if (bAdded && output) {
			output->AddFile(toadd);
			output->ShowFilesCount();
		}
		m_lastPublishED2KFlag = true;
	}
	return bAdded;
}

void CSharedFileList::RepublishFile(CKnownFile *pFile)
{
	CServer *pCurServer = server->GetCurrentServer();
	if (pCurServer && (pCurServer->GetTCPFlags() & SRV_TCPFLG_COMPRESSION)) {
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
					AddDebugLogLine(DLP_VERYLOW, false, _T("%hs: Already in duplicates list: %s %I64u \"%s\""), __FUNCTION__, (LPCTSTR)md4str(pFileInDuplicatesList->GetFileHash()), (uint64)pFileInDuplicatesList->GetFileSize(), (LPCTSTR)EscPercent(pFileInDuplicatesList->GetFileName()));
					pFileInDuplicatesList->SetFilePath(pFile->GetFilePath()); // Update the file path in the duplicates list
				} else
					AddDebugLogLine(DLP_VERYLOW, false, _T("%hs: File already in known list: %s %I64u \"%s\""), __FUNCTION__, (LPCTSTR)md4str(pFileInMap->GetFileHash()), (uint64)pFileInMap->GetFileSize(), (LPCTSTR)EscPercent(pFileInMap->GetFileName()));
			} else
				DebugLog(_T("File shared twice, might have been a single shared file before - %s"), (LPCTSTR)EscPercent(pFileInMap->GetFilePath()));
		}
		return false;
	}
	m_UnsharedFiles_map.RemoveKey(CSKey(pFile->GetFileHash()));

	CSingleLock listlock(&m_mutWriteList, TRUE);
	m_Files_map[key] = pFile;
	m_mapSharedPathsNoCase[BuildNoCasePathKey(pFile->GetFilePath())] = (void*)1;
	listlock.Unlock();

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

void CSharedFileList::FileHashingFinished(CKnownFile *file)
{
	// File hashing finished for a shared file (non-part file)
	// - Reading shared directories at startup and hashing files which were not found in known.met
	// - Reading shared directories during runtime (user hit Reload button, added a shared directory, ...)

	RemoveFromHashing(file); // Always detach from hashing before handing ownership anywhere

	// If the user no longer wants to share this file, just drop it
	if (!ShouldBeShared(file->GetSharedDirectory(), file->GetFilePath(), false)) {
		if (!IsFilePtrInList(file) && !theApp.knownfiles->IsFilePtrInList(file))
			delete file; // delete only when not owned by shared or known lists
		return;
	}

	theApp.knownfiles->SafeAddKFile(file); // First, register with KnownFiles; it deduplicates and may already contain an instance

	// If already in the shared list, we are done (drop temp instance if not owned anywhere)
	if (GetLiveFileByID(file->GetFileHash()) != NULL) {
		const bool bKnownDuplicate = !IsFilePtrInList(file) && theApp.knownfiles->IsFilePtrInList(file);
		if (bKnownDuplicate && output && (output->m_eFilter == FilterType::Duplicate || (output->m_eFilter == FilterType::History && thePrefs.GetFileHistoryShowDuplicate())))
			output->AddFile(file); // Insert only the new duplicate into the current duplicate/history view instead of rebuilding the whole list.

		if (!IsFilePtrInList(file) && !theApp.knownfiles->IsFilePtrInList(file))
			delete file;
		return;
	}

	SafeAddKFile(file); // Not shared yet, so add to shared list
}

bool CSharedFileList::RemoveFile(CKnownFile* pFile, bool bDeleted, bool bWillReloadListLater)
{
	//We need to remove it first from virtual list, otherwise OnLvnGetDispInfo can try to query a deleted file and crashes.
	output->RemoveFile(pFile, bDeleted, bWillReloadListLater);

	CSingleLock listlock(&m_mutWriteList, TRUE);
	m_mapSharedPathsNoCase.RemoveKey(BuildNoCasePathKey(pFile->GetFilePath()));
	bool bResult = (m_Files_map.RemoveKey(CCKey(pFile->GetFileHash())) != FALSE);
	listlock.Unlock();

	theApp.DownloadChecker->RemoveFromMap(pFile->GetFileHash(), pFile->GetFileName(), pFile->GetFileSize());

	m_keywords->RemoveKeywords(pFile);
	if (bResult) {
		m_UnsharedFiles_map[CSKey(pFile->GetFileHash())] = true;
		theApp.knownfiles->m_nRequestedTotal -= pFile->statistic.GetAllTimeRequests();
		theApp.knownfiles->m_nAcceptedTotal -= pFile->statistic.GetAllTimeAccepts();
		theApp.knownfiles->m_nTransferredTotal -= pFile->statistic.GetAllTimeTransferred();
	}
	return bResult;
}

void CSharedFileList::Reload()
{
	ClearVolumeInfoCache();
	m_mapPseudoDirNames.RemoveAll();
	m_keywords->RemoveAllKeywordReferences();
	while (!waitingforhash_list.IsEmpty()) // delete all files which are waiting to get hashed, will be re-added if still shared below
		delete waitingforhash_list.RemoveHead();
	{
		CSingleLock lock(&m_csShareRules, TRUE);
		bHaveSingleSharedFiles = false;
	}

	// Non-blocking reset of search work to avoid UI freezes when reloading often. Keep the worker thread alive and just clear its transient state and queues.
	if (m_searchThread == NULL)
		StartSearchThread();
	else
		m_searchThread->ResetWork();

	BeginReloadLookupSnapshot();

	CSingleLock listlock(&m_mutWriteList, TRUE); // Serialize map mutation against concurrent readers
	m_mapSharedPathsNoCase.RemoveAll();
	m_Files_map.RemoveAll(); // Avoid logs of duplication
	listlock.Unlock();
	FindSharedFiles();
	m_keywords->PurgeUnreferencedKeywords();
	if (m_searchThread && !m_searchThread->IsBusy()) {
		EndReloadLookupSnapshot();
		if (output) // Avoid reloading the GUI while we are searching for new files to share
			output->ReloadList(false, LSF_SELECTION);
	}
}

void CSharedFileList::SetOutputCtrl(CSharedFilesCtrl *in_ctrl)
{
	output = in_ctrl;
	output->ReloadList(false, LSF_SELECTION);
	HashNextFile();		// SLUGFILLER: SafeHash - if hashing not started yet, start it now
}

void CSharedFileList::SendListToServer()
{
	if (!server->IsConnected())
		return;
	
	std::vector<EServerBuddyMagicAnnounceEntry> magicEntries;
	BuildEServerBuddyMagicAnnounceEntries(magicEntries);
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

	std::vector<CKnownFile*> m_Ed2kPublishListVector;
	m_Ed2kPublishListVector.reserve(m_Files_map.GetCount());

	// These loops will add files sorted by their real priorities (GetRealPrio) until the vector size reaches to the limit.
	for (int prio = 4; prio >= 0 && m_Ed2kPublishListVector.size() < uRealLimit; --prio)
		for (const CKnownFilesMap::CPair* pair = m_Files_map.PGetFirstAssoc(); pair != NULL && m_Ed2kPublishListVector.size() < uRealLimit; pair = m_Files_map.PGetNextAssoc(pair)) // Fill vector with the map items
			if (!pair->value->GetPublishedED2K() && GetRealPrio(pair->value->GetUpPriority()) == prio && (!pair->value->IsLargeFile() || (pCurServer != NULL && pCurServer->SupportsLargeFilesTCP())))
				m_Ed2kPublishListVector.push_back(pair->value);

	if ((uint32)m_Ed2kPublishListVector.size() < uRealLimit) {
		limit = (uint32)m_Ed2kPublishListVector.size();
		if (limit == 0 && uMagicEntries == 0) {
			m_lastPublishED2KFlag = false;
			return;
		}
		limit += uMagicEntries;
	}
	files.WriteUInt32(limit);

	for (size_t i = 0; i < magicEntries.size(); ++i)
		CreateEServerBuddyMagicFilePacket(files, magicEntries[i]);

	uint32 count = limit - uMagicEntries;

	auto it = m_Ed2kPublishListVector.begin();
	while (it != m_Ed2kPublishListVector.end() && count-- > 0) {
		CreateOfferedFilePacket(*it, files, pCurServer);
		(*it)->SetPublishedED2K(true);
		it = m_Ed2kPublishListVector.erase(it);	// Due to deletion in loop, iterator became invalidated. So reset the iterator to next item.
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
}

void CSharedFileList::ClearED2KPublishInfo()
{
	m_lastPublishED2KFlag = true;
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		pair->value->SetPublishedED2K(false);
}

void CSharedFileList::ClearKadSourcePublishInfo()
{
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		pair->value->SetLastPublishTimeKadSrc(0, 0);
}

void CSharedFileList::CreateOfferedFilePacket(CKnownFile *cur_file, CSafeMemFile &files
	, CServer *pServer, CUpDownClient *pClient)
{
	UINT uEmuleVer = (pClient && pClient->IsEmuleClient()) ? pClient->GetVersion() : 0;

	// NOTE: This function is used for creating the offered file packet for Servers _and_ for Clients.
	files.WriteHash16(cur_file->GetFileHash());

	// *) This function is used for offering files to the local server and for sending
	//    shared files to some other client. In each case we send our IP+Port only, if
	//    we have a HighID.
	// *) Newer eservers also support 2 special IP+port values which are used to hold basic file status info.
	uint32 nClientID = 0;
	uint16 nClientPort = 0;
	if (pServer) {
		// we use the 'TCP-compression' server feature flag as indicator for a 'newer' server.
		if (pServer->GetTCPFlags() & SRV_TCPFLG_COMPRESSION) {
			if (cur_file->IsPartFile()) {
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

	tags.Add(new CTag(FT_FILENAME, cur_file->GetFileName()));

	const uint64 uFileSize = (uint64)cur_file->GetFileSize();
	if (!cur_file->IsLargeFile())
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
	if (cur_file->GetFileRating()) {
		uint32 uRatingVal = cur_file->GetFileRating();
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
		EED2KFileType eFileType = GetED2KFileTypeSearchID(GetED2KFileTypeID(cur_file->GetFileName()));
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
		const CString &strED2KFileType(GetED2KFileTypeSearchTerm(GetED2KFileTypeID(cur_file->GetFileName()), true));
		if (!strED2KFileType.IsEmpty())
			tags.Add(new CTag(FT_FILETYPE, strED2KFileType));
	}

	// eserver 16.4+ does not need the FT_FILEFORMAT tag at all nor does any eMule client. This tag
	// was used for older (very old) eDonkey servers only. -> We send it only to non-eMule clients.
	if (pServer == NULL && uEmuleVer == 0) {
		LPCTSTR pDot = ::PathFindExtension(cur_file->GetFileName());
		if (*pDot && pDot[1]) {
			CString strExt(pDot + 1); //skip the dot
			tags.Add(new CTag(FT_FILEFORMAT, strExt.MakeLower())); // file extension without a "."
		}
	}

	// only send verified meta data to servers/clients
	if (cur_file->GetMetaDataVer() > 0) {
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
			CTag *pTag = cur_file->GetTag(_aMetaTags[i].nName);
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
	if (pFile != NULL || !m_bReloadLookupSnapshotActive || hash == NULL)
		return pFile;

	CKnownFile* pFallback = NULL;
	if (m_ReloadLookupFiles_map.Lookup(CCKey(hash), pFallback))
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

	const HWND hWnd = theApp.emuledlg->sharedfileswnd->m_hWnd;
	if (::IsWindow(hWnd))
		::PostMessage(hWnd, UM_SHOWFILESCOUNT, 0, 0);
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
	if (waitingforhash_list.IsEmpty())
		return;
	UnknownFile_Struct *nextfile = waitingforhash_list.RemoveHead();
	currentlyhashing_list.AddTail(nextfile);	// SLUGFILLER: SafeHash - keep track
	CAddFileThread *addfilethread = static_cast<CAddFileThread*>(AfxBeginThread(RUNTIME_CLASS(CAddFileThread), THREAD_PRIORITY_BELOW_NORMAL, 0, CREATE_SUSPENDED));
	addfilethread->SetValues(this, nextfile->strDirectory, nextfile->strName, nextfile->strSharedDirectory);
	addfilethread->ResumeThread();
	// SLUGFILLER: SafeHash - nextfile deletion is handled elsewhere
	//delete nextfile;
}

// SLUGFILLER: SafeHash
bool CSharedFileList::IsHashing(const CString &rstrDirectory, const CString &rstrName)
{
	for (POSITION pos = waitingforhash_list.GetHeadPosition(); pos != NULL;) {
		const UnknownFile_Struct *pFile = waitingforhash_list.GetNext(pos);
		if (pFile->strName.CompareNoCase(rstrName) == 0 && EqualPaths(pFile->strDirectory, rstrDirectory))
			return true;
	}
	for (POSITION pos = currentlyhashing_list.GetHeadPosition(); pos != NULL;) {
		const UnknownFile_Struct *pFile = currentlyhashing_list.GetNext(pos);
		if (pFile->strName.CompareNoCase(rstrName) == 0 && EqualPaths(pFile->strDirectory, rstrDirectory))
			return true;
	}
	return false;
}

void CSharedFileList::RemoveFromHashing(CKnownFile *hashed)
{
	for (POSITION pos = currentlyhashing_list.GetHeadPosition(); pos != NULL;) {
		POSITION posLast = pos;
		const UnknownFile_Struct *pFile = currentlyhashing_list.GetNext(pos);
		if (pFile->strName.CompareNoCase(hashed->GetFileName()) == 0 && EqualPaths(pFile->strDirectory, hashed->GetPath())) {
			currentlyhashing_list.RemoveAt(posLast);
			delete pFile;
			HashNextFile();	// start next hash if possible, but only if a previous hash finished
			return;
		}
	}
}

void CSharedFileList::HashFailed(UnknownFile_Struct *hashed)
{
	for (POSITION pos = currentlyhashing_list.GetHeadPosition(); pos != NULL;) {
		POSITION posLast = pos;
		const UnknownFile_Struct *pFile = currentlyhashing_list.GetNext(pos);
		if (pFile->strName.CompareNoCase(hashed->strName) == 0 && EqualPaths(pFile->strDirectory, hashed->strDirectory)) {
			currentlyhashing_list.RemoveAt(posLast);
			delete pFile;
			HashNextFile();			// start next hash if possible, but only if a previous hash finished
			break;
		}
	}
	delete hashed;
}

void CSharedFileList::UpdateFile(CKnownFile *toupdate)
{
	output->UpdateFile(toupdate);
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

void CSharedFileList::UpdateSharedPathCache(CKnownFile* pFile, LPCTSTR pOldFilePath)
{
	if (pFile == NULL)
		return;

	CSingleLock listlock(&m_mutWriteList, TRUE);
	CKnownFile* pLiveFile = NULL;
	if (!m_Files_map.Lookup(CCKey(pFile->GetFileHash()), pLiveFile) || pLiveFile != pFile)
		return;

	if (pOldFilePath != NULL && pOldFilePath[0] != _T('\0'))
		m_mapSharedPathsNoCase.RemoveKey(BuildNoCasePathKey(pOldFilePath));

	if (!pFile->GetFilePath().IsEmpty())
		m_mapSharedPathsNoCase[BuildNoCasePathKey(pFile->GetFilePath())] = (void*)1;
}

CKnownFile* CSharedFileList::FindUniqueLiveSharedFileByIdentity(LPCTSTR pszFileName, time_t tUtcFileDate, uint64 uFileSize, LPCTSTR pszNewFilePath) const
{
	CKnownFile* pMatch = NULL;
	for (const CKnownFilesMap::CPair* pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair)) {
		CKnownFile* pFile = pair->value;
		if (pFile == NULL || pFile->IsPartFile())
			continue;

		if ((uint64)pFile->GetFileSize() != uFileSize || !IsFileDateEqual(pFile->GetUtcFileDate(), tUtcFileDate) || pFile->GetFileName().CompareNoCase(pszFileName) != 0)
			continue;

		if (pszNewFilePath != NULL && pFile->GetFilePath().CompareNoCase(pszNewFilePath) == 0)
			return pFile;

		const CString& strCurrentFilePath = pFile->GetFilePath();
		if (!strCurrentFilePath.IsEmpty() && ::PathFileExists(strCurrentFilePath))
			continue;

		if (pMatch != NULL && pMatch != pFile)
			return NULL;

		pMatch = pFile;
	}

	return pMatch;
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

	CKnownFile* pMatch = FindUniqueLiveSharedFileByIdentity(strFileName, tUtcFileDate, uFileSize, strFilePath);
	if (pMatch == NULL)
		return false;

	const CString strOldFilePath = pMatch->GetFilePath();
	if (strOldFilePath.CompareNoCase(strFilePath) == 0)
		return true;

	pMatch->SetPath(strDirectory);
	pMatch->SetFilePath(strFilePath);
	pMatch->SetSharedDirectory(strDirectory);
	pMatch->SetLastSeen();
	UpdateSharedPathCache(pMatch, strOldFilePath);

	AddDebugLogLine(DLP_LOW, false, _T("%hs: Reconciled shared file move: \"%s\" -> \"%s\""), __FUNCTION__, (LPCTSTR)EscPercent(strOldFilePath), (LPCTSTR)EscPercent(strFilePath));
	return true;
}

void CSharedFileList::Process()
{
	Publish();
	if (m_lastPublishED2KFlag && ::GetTickCount() >= m_lastPublishED2K + ED2KREPUBLISHTIME) {
		SendListToServer();
		m_lastPublishED2K = ::GetTickCount();
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

	if (pRebuildMetaDataThread != NULL) {
		DWORD lpExitCode;
		GetExitCodeThread(pRebuildMetaDataThread->m_hThread, &lpExitCode);
		if (lpExitCode == STILL_ACTIVE) {
			AddLogLine(true, GetResString(_T("METADA_UPDATE_IN_PROGRESS")));
			return;
		}
	}

	// Prepare m_MetaDataProcessList. This will be only accessed by the thread after this point, so there is no need for a lock.
	m_MetaDataProcessList.RemoveAll(); // Clean up the list first
	for (int i = theApp.emuledlg->sharedfileswnd->sharedfilesctrl.m_ListedItemsVector.size(); --i >= 0;) {
		CKnownFile* cur_file = theApp.emuledlg->sharedfileswnd->sharedfilesctrl.m_ListedItemsVector[i];
		if (cur_file != NULL && !cur_file->IsKindOf(RUNTIME_CLASS(CPartFile)) && !cur_file->GetPath().IsEmpty() && theApp.sharedfiles->GetFileByID(cur_file->GetFileHash()) != NULL) // Ensure that this is a shared file
			m_MetaDataProcessList.AddTail(cur_file); 
	}

	CSingleLock sMetadataUpdatingCountLock(&m_MetadataUpdatingCountLock, TRUE);
	m_uMetadataUpdatingCount = m_MetaDataProcessList.GetCount();
	if (!m_uMetadataUpdatingCount) {
		AddLogLine(true, GetResString(_T("METADA_UPDATE_NO_SHARED_FILE_LISTED")));
		return;
	}
	sMetadataUpdatingCountLock.Unlock();

	pRebuildMetaDataThread = AfxBeginThread(RunProc, (LPVOID)this, THREAD_PRIORITY_IDLE);
}

UINT AFX_CDECL CSharedFileList::RunProc(LPVOID pParam)
{
	AddLogLine(true, GetResString(_T("METADA_UPDATE_STARTED")));
	DbgSetThreadName("RebuildMetaData");

	CSharedFileList* sharedFileList = static_cast<CSharedFileList*>(pParam);

	while (!sharedFileList->m_MetaDataProcessList.IsEmpty()) {
		if (theApp.IsClosing())
			return 1;

		CKnownFile* pFile = sharedFileList->m_MetaDataProcessList.RemoveHead();
		try {
			if (pFile) {
				pFile->UpdateMetaDataTags();
				CSingleLock sMetadataUpdatingCountLock(&sharedFileList->m_MetadataUpdatingCountLock, TRUE);
				if (sharedFileList->m_uMetadataUpdatingCount) // Map can be modified by main instance during this loop. So we need to check this.
					sharedFileList->m_uMetadataUpdatingCount--;
				sMetadataUpdatingCountLock.Unlock();

				sharedFileList->NotifyShowFilesCount();
			}
		} catch (CException* ex) {
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("CSharedFileList::RunProc Unhandled exception while accessing file to update metadata: %s"), (LPCTSTR)EscPercent(CString(CExceptionStr(*ex))));
			ex->Delete();
			ASSERT(0);
		} catch (...) {
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("CSharedFileList::RunProc Unhandled exception while accessing file to update metadata."));
			ASSERT(0);
		}
	}

	CSingleLock sMetadataUpdatingCountLock(&sharedFileList->m_MetadataUpdatingCountLock, TRUE);
	sharedFileList->m_uMetadataUpdatingCount = 0; // Make sure this is set to 0 at the end of operation
	sMetadataUpdatingCountLock.Unlock();

	sharedFileList->NotifyShowFilesCount();
	theApp.emuledlg->sharedfileswnd->SendMessage(UM_METADATAUPDATED);
	theApp.QueueLogLine(true, GetResString(_T("METADA_UPDATE_COMPLETED")));

	return 0;
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
	{
		CSingleLock lock(&m_csShareRules, TRUE);
		bShared = RemovePathNoCase(m_liSingleSharedFiles, strFilePath);
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
		if (!ContainsPathNoCase(m_liSingleExcludedFiles, strFilePath))
			m_liSingleExcludedFiles.AddTail(strFilePath);
	}

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
	if (m_searchThread != NULL)
		m_searchThread->InvalidateShareRuleSnapshot();
	return true;
}

void CSharedFileList::ClearExcludedSharedDirectories()
{
	CSingleLock lock(&m_csShareRules, TRUE);
	m_liExcludedSharedDirs.RemoveAll();
	if (m_searchThread != NULL)
		m_searchThread->InvalidateShareRuleSnapshot();
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
	if (m_searchThread != NULL)
		m_searchThread->InvalidateShareRuleSnapshot();
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
	const CString &strFullPath(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + SHAREDFILES_FILE);
	CStringList liSingleSharedFiles;
	CStringList liSingleExcludedFiles;
	CStringList liExcludedSharedDirs;
	CopyExplicitShareRules(liSingleSharedFiles, liSingleExcludedFiles, liExcludedSharedDirs);
	CStdioFile file;
	if (file.Open(strFullPath, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite | CFile::typeBinary)) {
		try {
			// write Unicode byte order mark 0xFEFF
			static const WORD wBOM = u'\xFEFF';
			file.Write(&wBOM, sizeof(wBOM));

			for (POSITION pos = liSingleSharedFiles.GetHeadPosition(); pos != NULL;) {
				file.WriteString(liSingleSharedFiles.GetNext(pos));
				file.Write(_T("\r\n"), 2 * sizeof(TCHAR));
			}
			for (POSITION pos = liSingleExcludedFiles.GetHeadPosition(); pos != NULL;) {
				file.WriteString(CString(kExcludedSharedFilePrefix) + liSingleExcludedFiles.GetNext(pos)); // A '-' prefix means excluded file
				file.Write(_T("\r\n"), 2 * sizeof(TCHAR));
			}
			for (POSITION pos = liExcludedSharedDirs.GetHeadPosition(); pos != NULL;) {
				file.WriteString(CString(kExcludedSharedDirectoryPrefix) + liExcludedSharedDirs.GetNext(pos)); // A '!' prefix means excluded directory
				file.Write(_T("\r\n"), 2 * sizeof(TCHAR));
			}
			CommitAndClose(file);
		} catch (CFileException *ex) {
			DebugLogError(_T("Failed to save %s%s"), (LPCTSTR)strFullPath, (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
			ex->Delete();
		}
	} else
		DebugLogError(_T("Failed to save %s"), (LPCTSTR)EscPercent(strFullPath));
}

void CSharedFileList::LoadSingleSharedFilesList()
{
	const CString &strFullPath(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + SHAREDFILES_FILE);
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
		} catch (CFileException *ex) {
			DebugLogError(_T("Failed to load %s: %s"), (LPCTSTR)EscPercent(strFullPath), (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
			ex->Delete();
		}
	} else
		DebugLogError(_T("Failed to load %s"), (LPCTSTR)EscPercent(strFullPath));
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
	if (m_searchThread != NULL)
		m_searchThread->InvalidateShareRuleSnapshot();

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
		ASSERT(0);
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
	CSingleLock listlock(&m_mutWriteList, TRUE);
	void* pv = NULL;
	return m_mapSharedPathsNoCase.Lookup(BuildNoCasePathKey(rstrFilePath), pv) != FALSE;
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
	if (m_bContinueFoundProcessing) {
		InterlockedExchange(&m_lFoundFilesNotify, 0); // Consume stale posts while continuing the current batch
		return true;
	}
	return (InterlockedExchange(&m_lFoundFilesNotify, 0) > 0);
}

void CSharedFileList::OnSharedFilesFound()
{
	int processed = 0;
	CSharedFileListSearchThread::FoundFile* found = NULL;
	bool bHashQueueChanged = false;

	// Guard against transient null (e.g. during shutdown); drop stale callbacks safely.
	if (m_searchThread == NULL)
		return;

	m_bInFoundFilesProcessing = true; // Defer tree reload posts while processing found files

	while ((found = m_searchThread->PopFoundFile()) != NULL) {
		const CString& sSharedDir = found->linkdir.IsEmpty() ? found->dir : found->linkdir;
		if (!ShouldBeShared(sSharedDir, found->path, false)) {
			delete found; // Ensure we do not leak skipped items
			continue;
		}

		CKnownFile* toadd = theApp.knownfiles->FindKnownFile(found->name, found->date, found->size);
		if (toadd) {
			CCKey key(toadd->GetFileHash());
			CKnownFile* pFileInMap;
			CKnownFile* pFileInDuplicatesList = theApp.knownfiles->IsOnDuplicates(toadd->GetFileName(), toadd->GetUtcFileDate(), toadd->GetFileSize());
			if (pFileInDuplicatesList != NULL) {
				AddDebugLogLine(DLP_VERYLOW, false, _T("%hs: Already in duplicates list:   %s %I64u \"%s\""), __FUNCTION__, (LPCTSTR)EscPercent(md4str(pFileInDuplicatesList->GetFileHash())), (uint64)pFileInDuplicatesList->GetFileSize(), (LPCTSTR)EscPercent(pFileInDuplicatesList->GetFileName()));
				pFileInDuplicatesList->SetFilePath(found->path); // Update the file path in the duplicates list
			} else if (m_Files_map.Lookup(key, pFileInMap)) {
				AddDebugLogLine(DLP_VERYLOW, false, _T("%hs: File already in shared file list: %s %I64u \"%s\""), __FUNCTION__, (LPCTSTR)EscPercent(md4str(pFileInMap->GetFileHash())), (uint64)pFileInMap->GetFileSize(), (LPCTSTR)EscPercent(pFileInMap->GetFileName()));
				AddDebugLogLine(DLP_VERYLOW, false, _T("%hs: Old entry replaced with: %s %I64u \"%s\""), __FUNCTION__, (LPCTSTR)EscPercent(md4str(toadd->GetFileHash())), (uint64)toadd->GetFileSize(), (LPCTSTR)EscPercent(toadd->GetFileName()));
				if (!pFileInMap->IsKindOf(RUNTIME_CLASS(CPartFile)) || theApp.downloadqueue->IsPartFile(pFileInMap)) {
					if (pFileInMap->GetFilePath().CompareNoCase(toadd->GetFilePath()) != 0) { //is it actually really the same file in the same place we already share? if so don't bother too much
						LogWarning(GetResString(_T("ERR_DUPL_FILES2")), (LPCTSTR)EscPercent(pFileInMap->GetFilePath()), (LPCTSTR)EscPercent(toadd->GetFilePath()), (LPCTSTR)EscPercent(toadd->GetFileName()));
						AddDebugLogLine(DLP_VERYLOW, false, _T("%hs: File already in known list: %s %I64u \"%s\""), __FUNCTION__, (LPCTSTR)EscPercent(md4str(pFileInMap->GetFileHash())), (uint64)pFileInMap->GetFileSize(), (LPCTSTR)EscPercent(pFileInMap->GetFileName()));
					} else
						DebugLog(_T("File shared twice, might have been a single shared file before - %s"), (LPCTSTR)EscPercent(pFileInMap->GetFilePath()));
				}
			} else {
				if (!found->linkdir.IsEmpty())
					DebugLog(_T("Shared link: %s from %s"), (LPCTSTR)EscPercent(found->path), (LPCTSTR)EscPercent(found->linkdir));
				toadd->SetPath(found->dir);
				toadd->SetFilePath(found->path);
				toadd->SetSharedDirectory(found->linkdir.IsEmpty() ? found->dir : found->linkdir);
				AddFile(toadd);
			}
		} else {
			// not in knownfile list - start adding thread to hash file if the hashing of this file isn't already waiting
			// SLUGFILLER: SafeHash - don't double hash, MY way
			if (!IsHashing(found->dir, found->name) && !thePrefs.IsTempFile(found->dir, found->name)){
				UnknownFile_Struct* tohash = new UnknownFile_Struct;
				tohash->strDirectory = found->dir;
				tohash->strName = found->name;
				tohash->strSharedDirectory = found->linkdir.IsEmpty() ? found->dir : found->linkdir;
				waitingforhash_list.AddTail(tohash);
				bHashQueueChanged = true;
			} else
				AddDebugLogLine(DLP_VERYLOW, false, _T("%hs: Did not share file \"%s\" - already hashing or temp. file"), __FUNCTION__, (LPCTSTR)EscPercent(found->path));
			// SLUGFILLER: SafeHash
		}

		delete found;

		// Avoid processing too many files at once, or GUI will be unresponsible
		processed += 1;
		if (processed >= 50) {
			if (bHashQueueChanged)
				NotifyShowFilesCount();
			m_bContinueFoundProcessing = true;
			PostMessage(theApp.emuledlg->m_hWnd, TM_SHAREDFILELISTFOUNDFILES, 0, 0); // Post to our selves so we can continue later
			return;
		}
	}

	if (bHashQueueChanged)
		NotifyShowFilesCount();

	// Refresh the GUI and start hashing, if we are done searching for new files to share
	if (m_searchThread && !m_searchThread->IsBusy()) {
		if (waitingforhash_list.IsEmpty())
			AddLogLine(false,GetResString(_T("SHAREDFOUND")), m_Files_map.GetCount());
		else
			AddLogLine(false,GetResString(_T("SHAREDFOUNDHASHING")), m_Files_map.GetCount(), waitingforhash_list.GetCount());

		m_keywords->PurgeUnreferencedKeywords();
		HashNextFile();

		EndReloadLookupSnapshot();

		if (output)
			output->ReloadList(false, LSF_SELECTION);

		if (theApp.uploadqueue != NULL)
			theApp.uploadqueue->PruneWaitersForMissingSharedFiles();

		m_bContinueFoundProcessing = false;

		m_bInFoundFilesProcessing = false; // End of scan batch
		if (m_bTreeReloadPending) {
			m_bTreeReloadPending = false;
			if (theApp.emuledlg && theApp.emuledlg->sharedfileswnd && ::IsWindow(theApp.emuledlg->sharedfileswnd->m_hWnd))
				::PostMessage(theApp.emuledlg->sharedfileswnd->m_hWnd, UM_AUTO_RELOAD_SHARED_FILES, 2, 0);
		}
	}
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
		m_searchThread->PostThreadMessageW(CSharedFileListSearchThread::SFS_EXIT, 0, 0);
		if (hThread != NULL) {
			const DWORD dwWait = WaitForSingleObject(hThread, 3000); // Wait briefly for clean shutdown
			if (dwWait != WAIT_OBJECT_0) {
				TRACE(_T("Shared files search thread did not stop within timeout.\n"));
				return;
			}
		}
		delete m_searchThread;
		m_searchThread = NULL;
	}
}

IMPLEMENT_DYNCREATE(CSharedFileListSearchThread, CWinThread)

bool CSharedFileListSearchThread::HasQueuedFoundFiles()
{
	CSingleLock lock(&m_mutex, TRUE);
	return !m_foundFiles.IsEmpty();
}

bool CSharedFileListSearchThread::ShouldAbortWork(LONG lGeneration) const
{
	return IsExitRequested() || lGeneration != GetSearchGeneration() || theApp.IsClosing();
}

void CSharedFileListSearchThread::ResetWork()
{
	CSingleLock lock(&m_mutex, TRUE); // Reset queued paths, found files and dedup caches without stopping the thread.
	InterlockedIncrement(&m_lSearchGeneration);
	InterlockedExchange(&m_lSnapshotGeneration, -1);
	m_searchPaths.RemoveAll(); // Clear pending search paths

	// Drain and delete any queued found files
	while (!m_foundFiles.IsEmpty()) {
		FoundFile* f = m_foundFiles.RemoveHead();
		delete f;
	}

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
	while (!m_foundFiles.IsEmpty())
		delete m_foundFiles.RemoveHead();
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
	CStringList liSingleExcludedFilesDummy;
	CStringList liExcludedSharedDirs;

	m_shareRuleSnapshot.bAutoShareSubdirs = thePrefs.GetAutoShareSubdirs();
	m_shareRuleSnapshot.sIncoming = CSharedFileList::NormalizeDirectoryPath(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));

	for (INT_PTR i = 1; i < thePrefs.GetCatCount(); ++i) {
		CString sCatDir(CSharedFileList::NormalizeDirectoryPath(thePrefs.GetCatPath(i)));
		if (!sCatDir.IsEmpty())
			m_shareRuleSnapshot.liCategoryIncoming.AddTail(sCatDir);
	}

	thePrefs.CopySharedDirectoryList(sharedDirs);
	for (POSITION pos = sharedDirs.GetHeadPosition(); pos != NULL;) {
		CString sRoot(CSharedFileList::NormalizeDirectoryPath(sharedDirs.GetNext(pos)));
		if (!sRoot.IsEmpty())
			m_shareRuleSnapshot.liSharedDirs.AddTail(sRoot);
	}

	m_owner->CopyExplicitShareRules(liSingleSharedFilesDummy, liSingleExcludedFilesDummy, liExcludedSharedDirs);
	for (POSITION pos = liExcludedSharedDirs.GetHeadPosition(); pos != NULL;) {
		CString sExcluded(CSharedFileList::NormalizeDirectoryPath(liExcludedSharedDirs.GetNext(pos)));
		if (!sExcluded.IsEmpty())
			m_shareRuleSnapshot.liExcludedSharedDirs.AddTail(sExcluded);
	}
}

bool CSharedFileListSearchThread::ShouldShareDirectoryBySnapshotLocked(const CString& sDirPath) const
{
	if (!m_shareRuleSnapshot.bAutoShareSubdirs)
		return false;

	const CString sDir(CSharedFileList::NormalizeDirectoryPath(sDirPath));
	if (sDir.IsEmpty())
		return false;

	if (EqualPaths(sDir, m_shareRuleSnapshot.sIncoming) || IsSubDirectoryOf(sDir, m_shareRuleSnapshot.sIncoming))
		return true;

	for (POSITION pos = m_shareRuleSnapshot.liCategoryIncoming.GetHeadPosition(); pos != NULL;) {
		const CString& sCatDir(m_shareRuleSnapshot.liCategoryIncoming.GetNext(pos));
		if (EqualPaths(sDir, sCatDir) || IsSubDirectoryOf(sDir, sCatDir))
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
		BOOL result = GetMessageW(&msg, NULL, SFS_EXIT, SFS_SEARCH);
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
					ThreadListLock.Unlock();
					m_busy = false;
					exit = IsExitRequested();
					break;
				}

				while(!m_searchPaths.IsEmpty())	{
					searchPath = m_searchPaths.RemoveHead();
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
						m_searchPaths.RemoveAll();
						break;
					}
				}
				ThreadListLock.Unlock();

				// Force notification to ensure post processing
				ThreadListLock.Lock();
				m_busy = false;

				if (m_notify) {
					PostMessage(theApp.emuledlg->m_hWnd, TM_SHAREDFILELISTFOUNDFILES, 0, 0);
					m_notify=false;
				}

				ThreadListLock.Unlock();
				if (IsExitRequested())
					exit = true;
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
		if (name != _T(".") && name != _T("..") && (wfd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) == 0) {
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
			lock.Unlock();
		}
		return;
	}

	// Basic file validation
	ULONGLONG ullFoundFileSize = (static_cast<ULONGLONG>(wfd.nFileSizeHigh) << 32) | static_cast<ULONGLONG>(wfd.nFileSizeLow);
	if (name == _T(".") || name == _T("..") || (wfd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0 || (wfd.dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY) != 0 || ullFoundFileSize == 0 || ullFoundFileSize > MAX_EMULE_FILE_SIZE)
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
			strFoundDirectory = CString(strFoundFilePath, p + 1);
			strFoundFileName = strFoundFilePath.Mid(p + 1);
			ullFoundFileSize = (static_cast<ULONGLONG>(fad.nFileSizeHigh) << 32) | static_cast<ULONGLONG>(fad.nFileSizeLow);
			tFoundFileTime = fad.ftLastWriteTime;
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

	lock.Lock();
	if (ShouldAbortWork(lGeneration)) {
		lock.Unlock();
		return;
	}

	if (m_owner && m_owner->IsAlreadySharedByPathNoCase(strFoundFilePath)) // Already shared in map by same path; skip enqueue to prevent duplicate processing.
		return;

	const CString strFoundFileQueueKey = BuildNoCasePathKey(strFoundFilePath);
	void* pv = NULL;
	if (!m_inQueue.Lookup(strFoundFileQueueKey, pv)) {
		m_foundFiles.AddTail(new FoundFile(strFoundFileName, strFoundFilePath, strFoundDirectory, strShellLinkDir, fdate, ullFoundFileSize));
		m_inQueue.SetAt(strFoundFileQueueKey, (void*)1);
		if (m_notify) {
			PostMessage(theApp.emuledlg->m_hWnd, TM_SHAREDFILELISTFOUNDFILES, 0, 0);
			m_notify = false;
		}
	}

	lock.Unlock();
}

HANDLE OpenImportSourceLongPath(LPCTSTR path, uint64& outFileSize)
{
	outFileSize = 0;

	if (!path || !*path) {
		LogError(LOG_STATUSBAR, _T("Import source is empty."));
		return INVALID_HANDLE_VALUE;
	}

	const CString raw(path);
	if (!IsWin32LongPathsEnabled() && raw.GetLength() >= MAX_PATH) {
		LogWarning(LOG_STATUSBAR, _T("Skipped importing from \"%s\" - path too long (%u). Enable long path support to allow this."), (LPCTSTR)EscPercent(raw), (UINT)raw.GetLength());
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
		LogError(LOG_STATUSBAR, _T("Failed to get file size for \"%s\" - %s"), (LPCTSTR)EscPercent(raw), (LPCTSTR)EscPercent(GetErrorMessage(::GetLastError())));
		::CloseHandle(h);
		return INVALID_HANDLE_VALUE;
	}

	outFileSize = (uint64)li.QuadPart;
	return h;
}
