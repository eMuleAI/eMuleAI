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
		if (!theApp.emuledlg->PostMessage(TM_FINISHEDHASHING, (m_pOwner ? 0 : (WPARAM)m_partfile), (LPARAM)newKnown))
			delete newKnown;
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

void CSharedFileList::AddDirectory(const CString &strDir, CStringList &dirlist)
{
	ASSERT(strDir.Right(1) == _T("\\"));
	CString slDir(strDir);
	slDir.MakeLower();

	if (dirlist.Find(slDir) == NULL) {
		dirlist.AddHead(slDir);
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
{
	m_Files_map.InitHashTable(1031);
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
		// Signal thread to exit and wait for full termination to avoid MFC cleanup race.
		m_searchThread->PostThreadMessageW(CSharedFileListSearchThread::SFS_EXIT, 0, 0);
		HANDLE hThread = m_searchThread->m_hThread;
		if (hThread != NULL && hThread != INVALID_HANDLE_VALUE) {
			WaitForSingleObject(hThread, INFINITE);
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

void CSharedFileList::FindSharedFiles()
{
	if (!m_Files_map.IsEmpty() && theApp.downloadqueue) {
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
				m_Files_map.RemoveKey(key);
				listlock.Unlock();
			}
		}
		theApp.downloadqueue->AddPartFilesToShare(); // read partfiles
	}

	// khaos::kmod+ Fix: Shared files loaded multiple times.
	CStringList l_sAdded;
	const CString &tempDir(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));

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
	AddDirectory(tempDir, l_sAdded);

	// If user prefers auto share for subdirectories, proactively seed them for scanning.
	if (thePrefs.GetAutoShareSubdirs())
		SeedAutoShareSubdirsForRoot(tempDir);

	for (INT_PTR i = 1; i < thePrefs.GetCatCount(); ++i)
	{
		const CString &cat = thePrefs.GetCatPath(i);
		AddDirectory(cat, l_sAdded);
		if (thePrefs.GetAutoShareSubdirs())
			SeedAutoShareSubdirsForRoot(cat);
	}

	for (POSITION pos = thePrefs.shareddir_list.GetHeadPosition(); pos != NULL;)
	{
		const CString& root = thePrefs.shareddir_list.GetNext(pos);
		AddDirectory(root, l_sAdded);
		if (thePrefs.GetAutoShareSubdirs())
			SeedAutoShareSubdirsForRoot(root);
	}

	// add all single shared files
	for (POSITION pos = m_liSingleSharedFiles.GetHeadPosition(); pos != NULL;)
		CheckAndAddSingleFile(m_liSingleSharedFiles.GetNext(pos));

	// Files are yet to be found and therefore we skip the hash part
}

// Best‑effort recursive seeding for AutoShareSubdirs. It enumerates subdirectories under
// the given root and enqueues each with AddFilesFromDirectory, so the search thread can
// process them regardless of UI state or prior tree operations.
void CSharedFileList::SeedAutoShareSubdirsForRoot(const CString &rootDir)
{
	CString root(rootDir);
	if (root.IsEmpty())
		return;
	if (root.Right(1) != _T("\\"))
		root += _T("\\");

	CList<CString, const CString&> stack;
	stack.AddTail(root);
	while (!stack.IsEmpty()) {
		CString cur = stack.RemoveHead();
		CString pattern = PreparePathForWin32LongPath(cur + _T("*"));
		WIN32_FIND_DATA wfd = {};
		HANDLE hFind = ::FindFirstFileExW(pattern, FindExInfoBasic, &wfd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
		if (hFind == INVALID_HANDLE_VALUE)
			continue;

		do {
			const DWORD attr = wfd.dwFileAttributes;
			if ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
				const CString name(wfd.cFileName);
				if (name != _T("." ) && name != _T("..")) {
					// Skip offline directories only
					if ((attr & FILE_ATTRIBUTE_OFFLINE) != 0)
						continue;
					CString sub = cur + name + _T("\\");
					if (thePrefs.IsShareableDirectory(sub)) {
						AddFilesFromDirectory(sub); // enqueue for search
						stack.AddTail(sub); // depth‑first
					}
				}
			}
		} while (::FindNextFileW(hFind, &wfd));
		::FindClose(hFind);
	}
}

void CSharedFileList::AddFilesFromDirectory(const CString& rstrDirectory)
{
	CString strSearchPath(rstrDirectory);
	PathAddBackslash(strSearchPath.GetBuffer(strSearchPath.GetLength() + 1));
	strSearchPath.ReleaseBuffer();
	strSearchPath += _T("*");
	m_searchThread->BeginSearch(strSearchPath);
}

bool CSharedFileList::AddSingleSharedFile(const CString &rstrFilePath, bool bNoUpdate)
{
	bool bExclude = false;
	// first check if we are explicitly excluding this file
	for (POSITION pos = m_liSingleExcludedFiles.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		if (rstrFilePath.CompareNoCase(m_liSingleExcludedFiles.GetNext(pos)) == 0) {
			bExclude = true;
			m_liSingleExcludedFiles.RemoveAt(pos2);
			break;
		}
	}

	// check if we share this file in general
	bool bShared = ShouldBeShared(rstrFilePath.Left(rstrFilePath.ReverseFind(_T('\\'))), rstrFilePath, false);

	if (bShared && !bExclude)
		return false; // we should be sharing this file already
	if (!bShared)
		m_liSingleSharedFiles.AddTail(rstrFilePath); // the directory is not shared, so we need a new entry

	return bNoUpdate || CheckAndAddSingleFile(rstrFilePath);
}

bool CSharedFileList::CheckAndAddSingleFile(const CString& rstrFilePath)
{
	bHaveSingleSharedFiles = true;
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
					TRACE2(_T("%hs: Already in duplicates list: %s %I64u \"%s\"\n"), __FUNCTION__, (LPCTSTR)md4str(pFileInDuplicatesList->GetFileHash()), (uint64)pFileInDuplicatesList->GetFileSize(), (LPCTSTR)pFileInDuplicatesList->GetFileName());
					pFileInDuplicatesList->SetFilePath(pFile->GetFilePath()); // Update the file path in the duplicates list
				} else
					TRACE2(_T("%hs: File already in known list: %s %I64u \"%s\"\n"), __FUNCTION__, (LPCTSTR)md4str(pFileInMap->GetFileHash()), (uint64)pFileInMap->GetFileSize(), (LPCTSTR)pFileInMap->GetFileName());
			} else
				DebugLog(_T("File shared twice, might have been a single shared file before - %s"), (LPCTSTR)EscPercent(pFileInMap->GetFilePath()));
		}
		return false;
	}
	m_UnsharedFiles_map.RemoveKey(CSKey(pFile->GetFileHash()));

	CSingleLock listlock(&m_mutWriteList, TRUE);
	m_Files_map[key] = pFile;
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
	if (GetFileByID(file->GetFileHash()) != NULL) {
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
	bHaveSingleSharedFiles = false;

	// Non-blocking reset of search work to avoid UI freezes when reloading often. Keep the worker thread alive and just clear its transient state and queues.
	if (m_searchThread == NULL)
		StartSearchThread();
	else
		m_searchThread->ResetWork();

	CSingleLock listlock(&m_mutWriteList, TRUE); // Serialize map mutation against concurrent readers
	m_Files_map.RemoveAll(); // Avoid logs of duplication
	listlock.Unlock();
	FindSharedFiles();
	m_keywords->PurgeUnreferencedKeywords();
	if (m_searchThread && !m_searchThread->IsBusy() && output) // Avoid reloading the GUI while we are searching for new files to share
			output->ReloadList(false, LSF_SELECTION);
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

CKnownFile* CSharedFileList::GetFileByID(const uchar *hash) const
{
	if (hash) {
		CKnownFile *found_file;
		if (m_Files_map.Lookup(CCKey(hash), found_file))
			return found_file;
	}
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
	if (file && file == GetFileByID(file->GetFileHash()))
		return true;

	if (file)
		for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
			if (file == pair->value)
				return true;

	return false;
}

void CSharedFileList::HashNextFile()
{
	// SLUGFILLER: SafeHash
	if (!::IsWindow(theApp.emuledlg->m_hWnd))	// wait for the dialog to open
		return;
	if (!theApp.IsClosing())
		theApp.emuledlg->sharedfileswnd->sharedfilesctrl.ShowFilesCount();
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

	bool m_bMediaInfoLibHintLogged = false;
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

				theApp.emuledlg->sharedfileswnd->SendMessage(UM_SHOWFILESCOUNT);
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

	theApp.emuledlg->sharedfileswnd->SendMessage(UM_SHOWFILESCOUNT);
	theApp.emuledlg->sharedfileswnd->SendMessage(UM_METADATAUPDATED);
	theApp.QueueLogLine(true, GetResString(_T("METADA_UPDATE_COMPLETED")));

	return 0;
}

bool CSharedFileList::ShouldBeShared(const CString& sDirPath, LPCTSTR const pFilePath, bool bMustBeShared) const
{
	CString sDir(sDirPath);
	CString sIncoming(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));

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

	if (pFilePath) { 
		// Check if this file is explicitly unshared
		for (POSITION pos = m_liSingleExcludedFiles.GetHeadPosition(); pos != NULL;)
			if (m_liSingleExcludedFiles.GetNext(pos).CompareNoCase(pFilePath) == 0)
				return false;

		// Check if this file is explicitly shared (as single file)
		for (POSITION pos = m_liSingleSharedFiles.GetHeadPosition(); pos != NULL;)
			if (m_liSingleSharedFiles.GetNext(pos).CompareNoCase(pFilePath) == 0)
				return true;
	}

	// Check if this directory is explicitly shared
	for (POSITION pos = thePrefs.shareddir_list.GetHeadPosition(); pos != NULL;) {
		CString sSharedRoot = thePrefs.shareddir_list.GetNext(pos);
		if (EqualPaths(sDir, sSharedRoot))
			return true;

		if (thePrefs.GetAutoShareSubdirs() && IsSubDirectoryOf(sDir, sSharedRoot))
			return true;
	}

	return false;
}

bool CSharedFileList::ContainsSingleSharedFiles(const CString &strDirectory) const
{
	int iLen = strDirectory.GetLength();
	for (POSITION pos = m_liSingleSharedFiles.GetHeadPosition(); pos != NULL;)
		if (_tcsnicmp(strDirectory, m_liSingleSharedFiles.GetNext(pos), iLen) == 0)
			return true;

	return false;
}

bool CSharedFileList::ExcludeFile(const CString &strFilePath)
{
	bool bShared = false;
	// first remove from explicitly shared files
	for (POSITION pos = m_liSingleSharedFiles.GetHeadPosition(); pos != NULL;) {
		POSITION pos2 = pos;
		if (strFilePath.CompareNoCase(m_liSingleSharedFiles.GetNext(pos)) == 0) {
			bShared = true;
			m_liSingleSharedFiles.RemoveAt(pos2);
			break;
		}
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
	m_liSingleExcludedFiles.AddTail(strFilePath);

	// check if the file is in the shared list (doesn't have to; for example, if it is hashing or not loaded yet) and remove
	for (const CKnownFilesMap::CPair *pair = m_Files_map.PGetFirstAssoc(); pair != NULL; pair = m_Files_map.PGetNextAssoc(pair))
		if (strFilePath.CompareNoCase(pair->value->GetFilePath()) == 0) {
			RemoveFile(pair->value);
			break;
		}

	// GUI update to be done by the caller
	return true;
}

void CSharedFileList::CheckAndAddSingleFile(const CFileFind& ff)
{
	m_searchThread->BeginSearch(ff.GetFilePath());
}

void CSharedFileList::Save() const
{
	const CString &strFullPath(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + SHAREDFILES_FILE);
	CStdioFile file;
	if (file.Open(strFullPath, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite | CFile::typeBinary)) {
		try {
			// write Unicode byte order mark 0xFEFF
			static const WORD wBOM = u'\xFEFF';
			file.Write(&wBOM, sizeof(wBOM));

			for (POSITION pos = m_liSingleSharedFiles.GetHeadPosition(); pos != NULL;) {
				file.WriteString(m_liSingleSharedFiles.GetNext(pos));
				file.Write(_T("\r\n"), 2 * sizeof(TCHAR));
			}
			for (POSITION pos = m_liSingleExcludedFiles.GetHeadPosition(); pos != NULL;) {
				file.WriteString(_T('-') + m_liSingleExcludedFiles.GetNext(pos)); // a '-' prefix means excluded
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

				bool bExclude = (toadd[0] == _T('-')); // a '-' prefix means excluded
				if (bExclude)
					toadd.Delete(0, 1);

				// Skip non-existing directories on fixed disks only
				if (DirAccsess(toadd))
					if (bExclude)
						ExcludeFile(toadd);
					else
						AddSingleSharedFile(toadd, true);
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
	// check if we share this dir already or are not allowed to
	if (ShouldBeShared(rstrFilePath, NULL, false) || !thePrefs.IsShareableDirectory(rstrFilePath))
		return false;

	// add the new directory as shared, GUI update to be done by the caller
	thePrefs.shareddir_list.AddTail(rstrFilePath);
	if (!bNoUpdate) {
		AddFilesFromDirectory(rstrFilePath);
	}
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
	CCKey key;
	CKnownFile* cur_file = NULL;

	for (POSITION pos = m_Files_map.GetStartPosition(); pos != NULL; ) {
		m_Files_map.GetNextAssoc(pos, key, cur_file);
		if (cur_file != NULL && cur_file->GetFilePath().CompareNoCase(rstrFilePath) == 0)
			return true;
	}

	return false;
}


void CSharedFileList::NotifyFoundFilesEvent()
{
	InterlockedIncrement(&m_lFoundFilesNotify);
}

bool CSharedFileList::ShouldProcessFoundFilesTick()
{
	if (m_bContinueFoundProcessing)
		return true;
	return (InterlockedExchange(&m_lFoundFilesNotify, 0) > 0);
}

void CSharedFileList::OnSharedFilesFound()
{
	int processed = 0;
	CSharedFileListSearchThread::FoundFile* found = NULL;

	// Guard against transient null (e.g. during shutdown); drop stale callbacks safely.
	if (m_searchThread == NULL)
		return;

	m_bInFoundFilesProcessing = true; // Defer tree reload posts while processing found files

	while ((found = m_searchThread->PopFoundFile()) != NULL) {
		bool m_bExcluded = false;
		for (POSITION pos = m_liSingleExcludedFiles.GetHeadPosition(); pos != NULL; ) { // Check if this file is explicit unshared
			const CString& ex = m_liSingleExcludedFiles.GetNext(pos);
			if (found->path.CompareNoCase(ex) == 0) {
				m_bExcluded = true;
				break;
			}
		}

		// Override explicit excludes for incoming subtree when AutoShareSubdirs is enabled
		if (m_bExcluded && thePrefs.GetAutoShareSubdirs()) {
			CString sIncoming(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
			CString sDir(found->dir);
			if (EqualPaths(sDir, sIncoming) || IsSubDirectoryOf(sDir, sIncoming))
				m_bExcluded = false; // Auto-share incoming subdirectories wins over per-file exclude
		}

		if (m_bExcluded) { // If this file is explicitly unshared, skip it
			delete found; // Ensure we do not leak skipped items
			continue;
		}

		CKnownFile* toadd = theApp.knownfiles->FindKnownFile(found->name, found->date, found->size);
		if (toadd) {
			CCKey key(toadd->GetFileHash());
			CKnownFile* pFileInMap;
			CKnownFile* pFileInDuplicatesList = theApp.knownfiles->IsOnDuplicates(toadd->GetFileName(), toadd->GetUtcFileDate(), toadd->GetFileSize());
			if (pFileInDuplicatesList != NULL) {
				TRACE2(_T("%hs: Already in duplicates list:   %s %I64u \"%s\"\n"), __FUNCTION__, (LPCTSTR)EscPercent(md4str(pFileInDuplicatesList->GetFileHash())), (uint64)pFileInDuplicatesList->GetFileSize(), (LPCTSTR)EscPercent(pFileInDuplicatesList->GetFileName()));
				pFileInDuplicatesList->SetFilePath(found->path); // Update the file path in the duplicates list
			} else if (m_Files_map.Lookup(key, pFileInMap)) {
				TRACE2(_T("%hs: File already in shared file list: %s %I64u \"%s\"\n"), __FUNCTION__, (LPCTSTR)EscPercent(md4str(pFileInMap->GetFileHash())), (uint64)pFileInMap->GetFileSize(), (LPCTSTR)EscPercent(pFileInMap->GetFileName()));
				TRACE2(_T("%hs: Old entry replaced with: %s %I64u \"%s\"\n"), __FUNCTION__, (LPCTSTR)EscPercent(md4str(toadd->GetFileHash())), (uint64)toadd->GetFileSize(), (LPCTSTR)EscPercent(toadd->GetFileName()));
				if (!pFileInMap->IsKindOf(RUNTIME_CLASS(CPartFile)) || theApp.downloadqueue->IsPartFile(pFileInMap)) {
					if (pFileInMap->GetFilePath().CompareNoCase(toadd->GetFilePath()) != 0) { //is it actually really the same file in the same place we already share? if so don't bother too much
						LogWarning(GetResString(_T("ERR_DUPL_FILES2")), (LPCTSTR)EscPercent(pFileInMap->GetFilePath()), (LPCTSTR)EscPercent(toadd->GetFilePath()), (LPCTSTR)EscPercent(toadd->GetFileName()));
						TRACE2(_T("%hs: File already in known list: %s %I64u \"%s\"\n"), __FUNCTION__, (LPCTSTR)EscPercent(md4str(pFileInMap->GetFileHash())), (uint64)pFileInMap->GetFileSize(), (LPCTSTR)EscPercent(pFileInMap->GetFileName()));
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
			} else
				TRACE2(_T("%hs: Did not share file \"%s\" - already hashing or temp. file\n"), __FUNCTION__, (LPCTSTR)EscPercent(found->path));
			// SLUGFILLER: SafeHash
		}

		delete found;

		// Avoid processing too many files at once, or GUI will be unresponsible
		processed += 1;
		if (processed >= 50) {
			m_bContinueFoundProcessing = true;
			PostMessage(theApp.emuledlg->m_hWnd, TM_SHAREDFILELISTFOUNDFILES, 0, 0); // Post to our selves so we can continue later
			return;
		}
	}

	// Refresh the GUI and start hashing, if we are done searching for new files to share
	if (m_searchThread && !m_searchThread->IsBusy()) {
		if (waitingforhash_list.IsEmpty())
			AddLogLine(false,GetResString(_T("SHAREDFOUND")), m_Files_map.GetCount());
		else
			AddLogLine(false,GetResString(_T("SHAREDFOUNDHASHING")), m_Files_map.GetCount(), waitingforhash_list.GetCount());

		m_keywords->PurgeUnreferencedKeywords();
		HashNextFile();

		if (output)
			output->ReloadList(false, LSF_SELECTION);

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
		// Cache handle before posting exit to avoid use-after-free when the thread auto-deletes.
		HANDLE hThread = m_searchThread->m_hThread;
		m_searchThread->PostThreadMessageW(CSharedFileListSearchThread::SFS_EXIT, 0, 0);
		if (hThread != NULL)
			WaitForSingleObject(hThread, 3000); // Wait briefly for clean shutdown
		// Do NOT 'delete' here; CWinThread auto-deletes on exit. Just drop the pointer.
		m_searchThread = NULL;
	}
}

IMPLEMENT_DYNCREATE(CSharedFileListSearchThread, CWinThread)

bool CSharedFileListSearchThread::HasQueuedFoundFiles()
{
	CSingleLock lock(&m_mutex, TRUE);
	return !m_foundFiles.IsEmpty();
}

void CSharedFileListSearchThread::ResetWork()
{
	CSingleLock lock(&m_mutex, TRUE); // Reset queued paths, found files and dedup caches without stopping the thread.
	m_searchPaths.RemoveAll(); // Clear pending search paths

	// Drain and delete any queued found files
	while (!m_foundFiles.IsEmpty()) {
		FoundFile* f = m_foundFiles.RemoveHead();
		delete f;
	}

	// Clear dedup/visited state
	m_seenDuringSearch.RemoveAll();
	m_inQueue.RemoveAll();

	// Allow next enqueue to notify
	m_notify = true;
	m_busy = false;
}

void CSharedFileListSearchThread::BeginSearch(CString searchPath)
{
	if (theApp.IsClosing())	// Don't start any last-minute search
		return;

	CSingleLock lock(&m_mutex, TRUE);
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
				if (theApp.IsClosing()) {
					m_searchPaths.RemoveAll();
					ThreadListLock.Unlock();
					m_busy = false;
					break;
				}

				while(!m_searchPaths.IsEmpty())	{
					searchPath = m_searchPaths.RemoveHead();
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
						if (theApp.IsClosing())
							break; // Abort enumeration promptly on shutdown

						CheckSingleFile(wfd, baseDir);
					} while (FindNextFileW(hFind, &wfd));
					FindClose(hFind);

					ThreadListLock.Lock();

					if (theApp.IsClosing()) {
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

void CSharedFileListSearchThread::CheckSingleFile(const WIN32_FIND_DATA& wfd, const CString& rootDir)
{
	CSingleLock lock(&m_mutex); // Will be taken only when modifying lists

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

    // NOTE: Do NOT mark the parent directory as visited here; doing so would
    // prematurely stop processing further entries in the same folder. We only
    // use the visited-set to avoid recursion loops when enqueueing subdirectories.

    if ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        if (name != _T(".") && name != _T("..") && (wfd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) == 0 && thePrefs.GetAutoShareSubdirs()) {
            CString subSearch(plainDir + name);
            PathAddBackslash(subSearch.GetBuffer(subSearch.GetLength() + 1));
            subSearch.ReleaseBuffer();
            subSearch += _T("*");

				// Skip offline directories only; allow reparse points (visited-set prevents loops).
				const DWORD attr = wfd.dwFileAttributes;
				if ((attr & FILE_ATTRIBUTE_OFFLINE) != 0) {
					AddDebugLogLine(DLP_LOW, false, _T("%hs: Skipping directory \"%s\" (offline)\n"), __FUNCTION__, (LPCTSTR)EscPercent(subSearch));
					return;
				}

						// Allow overlong subdirectory scans; long path prefixing will handle deep paths.

            // Use visited-set for the subdirectory itself (not the parent), to avoid loops.
            CString subDir = plainDir + name; // no trailing backslash, no wildcard
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
                visitKey = _T("PATH:") + subDir; // Fallback to path-based key

            void* pv = NULL;
            if (m_seenDuringSearch.Lookup(visitKey, pv))
                return; // already queued/visited
            m_seenDuringSearch.SetAt(visitKey, (void*)1);

            lock.Lock();
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

	if (m_owner && m_owner->IsAlreadySharedByPathNoCase(strFoundFilePath)) // Already shared in map by same path; skip enqueue to prevent duplicate processing.
		return;

	// Deduplicate by full path to avoid infinite growth when re-queuing the same file.
	bool bAlreadyQueued = false;
	for (POSITION pos = m_foundFiles.GetHeadPosition(); pos != NULL; ) {
		FoundFile* queued = m_foundFiles.GetNext(pos);
		if (queued && queued->path.CompareNoCase(strFoundFilePath) == 0) {
			bAlreadyQueued = true;
			break;
		}
	}

	if (!bAlreadyQueued) {
		m_foundFiles.AddTail(new FoundFile(strFoundFileName, strFoundFilePath, strFoundDirectory, strShellLinkDir, fdate, ullFoundFileSize));
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
