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
#include "SearchFile.h"
#include "SearchList.h"
#include "SearchParams.h"
#include "SearchResultsWnd.h"
#include "Packets.h"
#include "Preferences.h"
#include "UpDownClient.h"
#include "SafeFile.h"
#include "SharedFileList.h"
#include "KnownFileList.h"
#include "DownloadQueue.h"
#include "PartFile.h"
#include "kademlia/utils/uint128.h"
#include "Kademlia/Kademlia/Entry.h"
#include "Kademlia/Kademlia/SearchManager.h"
#include "emuledlg.h"
#include "SearchDlg.h"
#include "SearchListCtrl.h"
#include "Log.h"
#ifdef _DEBUG
#include "eMuleAI\DebugLeakHelper.h"
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif
namespace
{
	bool CFileOpenD(CFile &file, LPCTSTR lpszFileName, UINT nOpenFlags, LPCTSTR lpszMsg)
	{
		CFileException ex;
		if (!file.Open(lpszFileName, nOpenFlags, &ex)) {
			if (ex.m_cause != CFileException::fileNotFound)
				DebugLogError(_T("%s%s"), lpszMsg, (LPCTSTR)CExceptionStrDash(ex));
			return false;
		}
		return true;
	}

#ifdef _DEBUG
	void __cdecl ClearStoredSearchesBeforeLeakDump()
	{
		if (theApp.emuledlg != NULL && theApp.emuledlg->searchwnd != NULL && ::IsWindow(theApp.emuledlg->searchwnd->m_hWnd)) {
			theApp.emuledlg->searchwnd->DeleteAllSearches();
			return;
		}

		if (theApp.searchlist != NULL) {
			theApp.searchlist->Clear();
		}
	}
#endif
	static bool ShouldSkipSearchPersistenceForManualLeakDump()
	{
#if defined(_DEBUG) && defined(DEBUGLEAKHELPER)
		TCHAR szManualDump[8] = {};
		const DWORD dwManualDump = GetEnvironmentVariable(_T("EMULE_CRT_FORCE_MANUAL_DUMP"), szManualDump, _countof(szManualDump));
		return dwManualDump > 0 && dwManualDump < _countof(szManualDump) && szManualDump[0] != _T('0');
#else
		return false;
#endif
	}
}

#define SPAMFILTER_FILENAME		_T("SearchSpam.met")
#define SPAMFILTER_FILENAME_TMP	 _T("SearchSpam.met.tmp")
#define STOREDSEARCHES_FILENAME	_T("StoredSearches.met")
#define STOREDSEARCHES_FILENAME_TMP	_T("StoredSearches.met.tmp")

#define STOREDSEARCHES_VERSION	102
///////////////////////////////////////////////////////////////////////////////
// CSearchList

CSearchList::CSearchList()
	: outputwnd()
	, m_nCurED2KSearchID()
	, m_bSpamFilterLoaded()
	, m_bKadReloadWaiting()
	, m_dwKadLastReloadTick() 
{
	m_nLastSaved = ::GetTickCount();
#ifdef _DEBUG
	DebugLeakHelper::RegisterPreDumpHook(&ClearStoredSearchesBeforeLeakDump);
#endif
}

CSearchList::~CSearchList()
{
	Clear();
	for (POSITION pos = m_mUDPServerRecords.GetStartPosition(); pos != NULL;) {
		uint32 dwIP;
		UDPServerRecord *pRecord;
		m_mUDPServerRecords.GetNextAssoc(pos, dwIP, pRecord);
		delete pRecord;
	}
}

void CSearchList::Clear()
{
	for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
		POSITION posLast = pos;
		SearchListsStruct *listCur = m_listFileLists.GetNext(pos);
		while (!listCur->m_listSearchFiles.IsEmpty())
			delete listCur->m_listSearchFiles.RemoveHead();
		m_listFileLists.RemoveAt(posLast);
		delete listCur;
	}
}

void CSearchList::RemoveResults(uint32 nSearchID)
{
	// this will not delete the item from the window, make sure your code does it if you call this
	for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
		POSITION posLast = pos;
		SearchListsStruct *listCur = m_listFileLists.GetNext(pos);
		if (listCur->m_nSearchID == nSearchID) {
			while (!listCur->m_listSearchFiles.IsEmpty())
				delete listCur->m_listSearchFiles.RemoveHead();
			m_listFileLists.RemoveAt(posLast);
			delete listCur;
			m_foundFilesCount.RemoveKey(nSearchID);
			m_originalFoundFilesCount.RemoveKey(nSearchID);
			m_foundSourcesCount.RemoveKey(nSearchID);
			m_ReceivedUDPAnswersCount.RemoveKey(nSearchID);
			m_RequestedUDPAnswersCount.RemoveKey(nSearchID);
			m_mergedSearchHistory.RemoveKey(nSearchID);
			return;
		}
	}
}

void CSearchList::RemoveResult(CSearchFile* todel)
{
	if (!todel)
		return;

	SearchList* list = GetSearchListForID(todel->GetSearchID());
	if (!list)
		return;

	POSITION posParent = list->Find(todel);
	if (!posParent)
		return;

	// SearchFile can be parent item with children (They all have same hashes). When we delete SearchFile we should delete all of these copies.
	if (!todel->GetListParent()) {
		POSITION pos = list->GetHeadPosition();
		while (pos) {
			POSITION posCur = pos;
			CSearchFile* pCurFile = list->GetNext(pos);
			if (pCurFile && pCurFile != todel && md4equ(pCurFile->GetFileHash(), todel->GetFileHash())) {
				list->RemoveAt(posCur); // Remove parent and child items except the one we want to delete
				delete pCurFile;
			}
		}
	}

	list->RemoveAt(posParent); // Remove item
	delete todel;
}

void CSearchList::NewSearch(CSearchListCtrl *pWnd, const CString &strResultFileType, SSearchParams *pParams)
{
	if (pWnd)
		outputwnd = pWnd;

	m_strResultFileType = strResultFileType;
	ASSERT(pParams->eType != SearchTypeAutomatic);
	if (pParams->eType == SearchTypeEd2kServer || pParams->eType == SearchTypeEd2kGlobal) {
		m_nCurED2KSearchID = pParams->dwSearchID;
		m_aCurED2KSentRequestsIPs.RemoveAll();
		m_aCurED2KSentReceivedIPs.RemoveAll();
	}
	m_foundFilesCount[pParams->dwSearchID] = 0;
	m_originalFoundFilesCount[pParams->dwSearchID] = 0;
	m_foundSourcesCount[pParams->dwSearchID] = 0;
	m_ReceivedUDPAnswersCount[pParams->dwSearchID] = 0;
	m_RequestedUDPAnswersCount[pParams->dwSearchID] = 0;
	m_mergedSearchHistory[pParams->dwSearchID] = false;

	if (pParams->strBooleanExpr.IsEmpty())
		pParams->strBooleanExpr = pParams->strExpression;

	// convert the expression into an array of search keywords which the user has typed in
	// this is used for the spam filter later and not at all semantically equal to
	// the actual search expression any more
	m_astrSpamCheckCurSearchExp.RemoveAll();
	CString sExpr(pParams->strExpression);
	if (_tcsncmp(sExpr.MakeLower(), _T("related:"), 8) != 0) { // ignore special searches
		int nPos, nPos2;
		while ((nPos = sExpr.Find(_T('"'))) >= 0 && (nPos2 = sExpr.Find(_T('"'), nPos + 1)) >= 0) {
			const CString& strQuoted(sExpr.Mid(nPos + 1, (nPos2 - nPos) - 1));
			m_astrSpamCheckCurSearchExp.Add(strQuoted);
			sExpr.Delete(nPos, (nPos2 - nPos) + 1);
		}
		for (int iPos = 0; iPos >= 0;) {
			const CString& sToken(sExpr.Tokenize(_T(".[]()!-'_ "), iPos));
			if (!sToken.IsEmpty() && sToken != "and" && sToken != "or" && sToken != "not")
				m_astrSpamCheckCurSearchExp.Add(sToken);
		}
	}
}

UINT CSearchList::ProcessSearchAnswer(const uchar *in_packet, uint32 size
	, CUpDownClient &sender, bool *pbMoreResultsAvailable, LPCTSTR pszDirectory)
{
	uint32 uSearchID = sender.GetSearchID();
	if (!uSearchID) {
		uSearchID = theApp.emuledlg->searchwnd->m_pwndResults->GetNextSearchID();
		sender.SetSearchID(uSearchID);
	}
	ASSERT(uSearchID);
	SSearchParams *pParams = new SSearchParams;
	pParams->strExpression = sender.GetUserName();
	if (pParams->strExpression.IsEmpty())
		pParams->strExpression = md4str(sender.GetUserHash());
	pParams->dwSearchID = uSearchID;
	pParams->bClientSharedFiles = true;
	pParams->m_strClientHash = md4str(sender.GetUserHash());
	if (theApp.emuledlg->searchwnd->CreateNewTab(pParams)) {
		m_foundFilesCount[uSearchID] = 0;
		m_originalFoundFilesCount[uSearchID] = 0;
		m_foundSourcesCount[uSearchID] = 0;
		m_mergedSearchHistory[uSearchID] = false;
	} else
		delete pParams;

	CSafeMemFile packet(in_packet, size);
	for (uint32 results = packet.ReadUInt32(); results > 0; --results) {
		CSearchFile *toadd = new CSearchFile(packet, sender.GetUnicodeSupport() != UTF8strNone, uSearchID, 0, 0, pszDirectory);
		if (toadd->IsLargeFile() && !sender.SupportsLargeFiles()) {
			DebugLogWarning(_T("Client offers large file (%s) but did not announce support for it - ignoring file"), (LPCTSTR)EscPercent(toadd->GetFileName()));
			delete toadd;
			continue;
		}
		toadd->SetClientID(sender.GetIP().ToUInt32(false));
		toadd->SetClientPort(sender.GetUserPort());
		toadd->SetClientServerIP(sender.GetServerIP());
		toadd->SetClientServerPort(sender.GetServerPort());
		if (sender.GetServerIP() && sender.GetServerPort()) {
			CSearchFile::SServer server(sender.GetServerIP(), sender.GetServerPort(), false);
			server.m_uAvail = 1;
			toadd->AddServer(server);
		}
		toadd->SetPreviewPossible(sender.GetPreviewSupport() && ED2KFT_VIDEO == GetED2KFileTypeID(toadd->GetFileName()));
		AddToList(toadd, true, 0, true);
	}
	if (outputwnd) {
		if (uSearchID == theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.m_nResultsID)
			// Search results are accumulated in the model first and then projected to the visible list in one rebuild.
			outputwnd->ReloadList(false, LSF_SELECTION);
		else
			outputwnd->UpdateTabHeader(uSearchID, EMPTY, false);
	}

	if (pbMoreResultsAvailable)
		*pbMoreResultsAvailable = false;
	int iAddData = static_cast<int>(packet.GetLength() - packet.GetPosition());
	if (iAddData == 1) {
		uint8 ucMore = packet.ReadUInt8();
		if (ucMore <= 0x01) {
			if (pbMoreResultsAvailable)
				*pbMoreResultsAvailable = (ucMore != 0);
			if (thePrefs.GetDebugClientTCPLevel() > 0)
				Debug(_T("  Client search answer(%s): More=%u\n"), sender.GetUserName(), ucMore);
		} else if (thePrefs.GetDebugClientTCPLevel() > 0)
			Debug(_T("*** NOTE: Client ProcessSearchAnswer(%s): ***AddData: 1 byte: 0x%02x\n"), sender.GetUserName(), ucMore);

	} else if (iAddData > 0) {
		if (thePrefs.GetDebugClientTCPLevel() > 0) {
			Debug(_T("*** NOTE: Client ProcessSearchAnswer(%s): ***AddData: %u bytes\n"), sender.GetUserName(), iAddData);
			DebugHexDump(in_packet + packet.GetPosition(), iAddData);
		}
	}

	packet.Close();
	return GetResultCount(uSearchID);
}

UINT CSearchList::ProcessSearchAnswer(const uchar *in_packet, uint32 size, bool bOptUTF8
	, uint32 nServerIP, uint16 nServerPort, bool *pbMoreResultsAvailable)
{
	CSafeMemFile packet(in_packet, size);
	for (uint32 i = packet.ReadUInt32(); i > 0; --i) {
		CSearchFile *toadd = new CSearchFile(packet, bOptUTF8, m_nCurED2KSearchID);
		toadd->SetClientServerIP(nServerIP);
		toadd->SetClientServerPort(nServerPort);
		if (nServerIP && nServerPort) {
			CSearchFile::SServer server(nServerIP, nServerPort, false);
			server.m_uAvail = toadd->GetIntTagValue(FT_SOURCES);
			toadd->AddServer(server);
		}
		AddToList(toadd, false, 0, true);
	}
	if (outputwnd) {
		if (m_nCurED2KSearchID == theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.m_nResultsID)
			outputwnd->ReloadList(false, LSF_SELECTION);
		else
			outputwnd->UpdateTabHeader(m_nCurED2KSearchID, EMPTY, false);
	}

	if (pbMoreResultsAvailable)
		*pbMoreResultsAvailable = false;
	int iAddData = (int)(packet.GetLength() - packet.GetPosition());
	if (iAddData == 1) {
		uint8 ucMore = packet.ReadUInt8();
		if (ucMore == 0x00 || ucMore == 0x01) {
			if (pbMoreResultsAvailable)
				*pbMoreResultsAvailable = ucMore != 0;
			if (thePrefs.GetDebugServerTCPLevel() > 0)
				Debug(_T("  Search answer(Server %s:%u): More=%u\n"), (LPCTSTR)ipstr(nServerIP), nServerPort, ucMore);
		} else if (thePrefs.GetDebugServerTCPLevel() > 0)
			Debug(_T("*** NOTE: ProcessSearchAnswer(Server %s:%u): ***AddData: 1 byte: 0x%02x\n"), (LPCTSTR)ipstr(nServerIP), nServerPort, ucMore);
	} else if (iAddData > 0) {
		if (thePrefs.GetDebugServerTCPLevel() > 0) {
			Debug(_T("*** NOTE: ProcessSearchAnswer(Server %s:%u): ***AddData: %u bytes\n"), (LPCTSTR)ipstr(nServerIP), nServerPort, iAddData);
			DebugHexDump(in_packet + packet.GetPosition(), iAddData);
		}
	}

	packet.Close();
	return GetED2KResultCount();
}

UINT CSearchList::ProcessUDPSearchAnswer(CFileDataIO &packet, bool bOptUTF8, uint32 nServerIP, uint16 nServerPort)
{
	CSearchFile *toadd = new CSearchFile(packet, bOptUTF8, m_nCurED2KSearchID, nServerIP, nServerPort, NULL, false, true);

	bool bFound = false;
	for (INT_PTR i = m_aCurED2KSentRequestsIPs.GetCount(); --i >= 0;)
		if (m_aCurED2KSentRequestsIPs[i] == nServerIP) {
			bFound = true;
			break;
		}

	if (!bFound) {
		DebugLogError(_T("Unrequested or delayed Server UDP Searchresult received from IP %s, ignoring"), (LPCTSTR)ipstr(nServerIP));
		delete toadd;
		return 0;
	}

	bool bNewResponse = true;
	for (INT_PTR i = m_aCurED2KSentReceivedIPs.GetCount(); --i >= 0;)
		if (m_aCurED2KSentReceivedIPs[i] == nServerIP) {
			bNewResponse = false;
			break;
		}

	if (bNewResponse) {
		uint32 nResponses;
		if (!m_ReceivedUDPAnswersCount.Lookup(m_nCurED2KSearchID, nResponses))
			nResponses = 0;
		m_ReceivedUDPAnswersCount[m_nCurED2KSearchID] = nResponses + 1;
		m_aCurED2KSentReceivedIPs.Add(nServerIP);
	}

	const CUDPServerRecordMap::CPair *pair = m_mUDPServerRecords.PLookup(nServerIP);
	if (pair)
		++pair->value->m_nResults;
	else {
		UDPServerRecord *pRecord = new UDPServerRecord;
		pRecord->m_nResults = 1;
		pRecord->m_nSpamResults = 0;
		m_mUDPServerRecords[nServerIP] = pRecord;
	}

	AddToList(toadd, false, nServerIP, true);
	if (outputwnd) {
		if (m_nCurED2KSearchID == theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.m_nResultsID)
			outputwnd->ReloadList(false, LSF_SELECTION);
		else
			outputwnd->UpdateTabHeader(m_nCurED2KSearchID, EMPTY, false);
	}
	return GetED2KResultCount();
}

UINT CSearchList::GetResultCount(uint32 nSearchID) const
{
	UINT nSources;
	return m_foundSourcesCount.Lookup(nSearchID, nSources) ? nSources : 0;
}

UINT CSearchList::GetED2KResultCount() const
{
	return GetResultCount(m_nCurED2KSearchID);
}

void CSearchList::GetWebList(CQArray<SearchFileStruct, SearchFileStruct> *SearchFileArray, int iSortBy) const
{
	for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
		SearchListsStruct *listCur = m_listFileLists.GetNext(pos);
		for (POSITION pos2 = listCur->m_listSearchFiles.GetHeadPosition(); pos2 != NULL;) {
			const CSearchFile *pFile = listCur->m_listSearchFiles.GetNext(pos2);
			if (pFile == NULL || pFile->GetListParent() != NULL || !(uint64)pFile->GetFileSize() || pFile->GetFileName().IsEmpty())
				continue;

			SearchFileStruct structFile;
			structFile.m_strFileName = pFile->GetFileName();
			structFile.m_strFileType = pFile->GetFileTypeDisplayStr();
			structFile.m_strFileHash = md4str(pFile->GetFileHash());
			structFile.m_uSourceCount = pFile->GetSourceCount();
			structFile.m_dwCompleteSourceCount = pFile->GetCompleteSourceCount();
			structFile.m_uFileSize = pFile->GetFileSize();

			switch (iSortBy) {
			case 0:
				structFile.m_strIndex = structFile.m_strFileName;
				break;
			case 1:
				structFile.m_strIndex.Format(_T("%10I64u"), structFile.m_uFileSize);
				break;
			case 2:
				structFile.m_strIndex = structFile.m_strFileHash;
				break;
			case 3:
				structFile.m_strIndex.Format(_T("%09u"), structFile.m_uSourceCount);
				break;
			case 4:
				structFile.m_strIndex = structFile.m_strFileType;
				break;
			default:
				structFile.m_strIndex.Empty();
			}
			SearchFileArray->Add(structFile);
		}
	}
}

void CSearchList::AddFileToDownloadByHash(const uchar *hash, int cat)
{
	for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
		const SearchListsStruct *listCur = m_listFileLists.GetNext(pos);
		for (POSITION pos2 = listCur->m_listSearchFiles.GetHeadPosition(); pos2 != NULL;) {
			CSearchFile *sf = listCur->m_listSearchFiles.GetNext(pos2);
			if (md4equ(hash, sf->GetFileHash())) {
				theApp.downloadqueue->AddSearchToDownload(sf, 2, cat);
				return;
			}
		}
	}
}
bool CSearchList::AddToList(CSearchFile* toadd, bool bClientResponse, uint32 dwFromUDPServerIP, bool bDoSpamRating)
{
	if (!bClientResponse && !m_strResultFileType.IsEmpty() && m_strResultFileType != toadd->GetFileType()) {
		delete toadd;
		return false;
	}
	SearchList *list = GetSearchListForID(toadd->GetSearchID());

	// Spam filter: Calculate the filename without any used keywords (and separators) for later use
	CString strNameWithoutKeyword;
	CString strName(toadd->GetFileName());
	strName.MakeLower();
	strNameWithoutKeyword.GetBuffer(strName.GetLength());
	strNameWithoutKeyword.ReleaseBuffer(0);
	const LPCTSTR pszName = strName;
	const int nNameLength = strName.GetLength();

	for (int iPos = 0; iPos < nNameLength;) {
		while (iPos < nNameLength && _tcschr(_T(".[]()!-'_ "), pszName[iPos]) != NULL)
			++iPos;
		if (iPos >= nNameLength)
			break;

		const int nTokenStart = iPos;
		while (iPos < nNameLength && _tcschr(_T(".[]()!-'_ "), pszName[iPos]) == NULL)
			++iPos;
		const int nTokenLength = iPos - nTokenStart;
		if (nTokenLength <= 0)
			continue;

		bool bFound = false;
		if (!bClientResponse && toadd->GetSearchID() == m_nCurED2KSearchID) {
			for (INT_PTR i = m_astrSpamCheckCurSearchExp.GetCount(); --i >= 0;) {
				const CString& strSpamToken = m_astrSpamCheckCurSearchExp[i];
				if (strSpamToken.GetLength() == nTokenLength && _tcsncmp((LPCTSTR)strSpamToken, pszName + nTokenStart, nTokenLength) == 0) {
					bFound = true;
					break;
				}
			}
		}
		if (!bFound) {
			if (!strNameWithoutKeyword.IsEmpty())
				strNameWithoutKeyword.AppendChar(_T(' '));
			strNameWithoutKeyword.Append(pszName + nTokenStart, nTokenLength);
		}
	}
	toadd->SetNameWithoutKeyword(strNameWithoutKeyword);

	// search for a 'parent' with same file hash and search-id as the new search result entry
	for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
		CSearchFile *parent = list->GetNext(pos);
		if (parent->GetListParent() == NULL && md4equ(parent->GetFileHash(), toadd->GetFileHash())) {
			// if this parent does not have any child entries yet, create one child entry
			// which is equal to the current parent entry (needed for GUI when expanding the child list).
			if (!parent->GetListChildCount()) {
				CSearchFile *child = new CSearchFile(parent);
				child->m_bNowrite = true; // will not save
				child->SetListParent(parent);
				int iSources = parent->GetSourceCount();
				if (iSources == 0)
					iSources = 1;
				child->SetListChildCount(iSources);
				list->AddTail(child);
				parent->SetListChildCount(1);
			}

			// get the 'Availability' of the new search result entry
			uint32 uAvail = toadd->GetSourceCount();
			if (bClientResponse && !uAvail)
				// If this is a response from a client ("View Shared Files"), we set the "Availability" at least to 1.
				uAvail = 1;

			// get 'Complete Sources' of the new search result entry
			uint32 uCompleteSources = toadd->GetCompleteSourceCount();

			bool bFound = false;
			if (thePrefs.GetDebugSearchResultDetailLevel() >= 1)
				; // for debugging: do not merge search results
			else {
				// check if that parent already has a child with same filename as the new search result entry
				for (POSITION pos2 = list->GetHeadPosition(); pos2 != NULL && !bFound;) {
					CSearchFile *child = list->GetNext(pos2);
					if (child != toadd														// not the same object
						&& child->GetListParent() == parent									// is a child of our result (one file hash)
						&& toadd->GetFileName().CompareNoCase(child->GetFileName()) == 0)	// same name
					{
						bFound = true;

						// add properties of new search result entry to the already available child entry (with same filename)
						// ed2k: use the sum of all values, kad: use the max. values
						if (toadd->IsKademlia()) {
							if (uAvail > child->GetListChildCount())
								child->SetListChildCount(uAvail);
						} else
							child->AddListChildCount(uAvail);

						child->AddSources(uAvail);
						child->AddCompleteSources(uCompleteSources);

						// Check AICH Hash - if they differ, clear it (see KademliaSearchKeyword)
						//					 if we don't have a hash yet, take it over
						if (toadd->GetFileIdentifier().HasAICHHash()) {
							if (child->GetFileIdentifier().HasAICHHash()) {
								if (child->GetFileIdentifier().GetAICHHash() != toadd->GetFileIdentifier().GetAICHHash()) {
									DEBUG_ONLY(DebugLogWarning(_T("Kad: SearchList: AddToList: Received searchresult with different AICH hash than existing one, ignoring AICH for result %s"), (LPCTSTR)EscPercent(child->GetFileName())));
									child->SetFoundMultipleAICH();
									child->GetFileIdentifier().ClearAICHHash();
								}
							} else if (!child->HasFoundMultipleAICH()) {
								DEBUG_ONLY(DebugLog(_T("Kad: SearchList: AddToList: Received searchresult with new AICH hash %s, taking over to existing result. Entry: %s"), (LPCTSTR)toadd->GetFileIdentifier().GetAICHHash().GetString(), (LPCTSTR)EscPercent(child->GetFileName())));
								child->GetFileIdentifier().SetAICHHash(toadd->GetFileIdentifier().GetAICHHash());
							}
						}
						break;
					}
				}
			}
			if (!bFound) {
				// the parent which we had found does not yet have a child with that new search result's entry name,
				// add the new entry as a new child
				//
				toadd->SetListParent(parent);
				toadd->SetListChildCount(uAvail);
				parent->AddListChildCount(1);
				list->AddTail(toadd);
			}

			// copy possible available sources from new search result entry to parent
			if (IsValidSearchResultClientIPPort(toadd->GetClientID(), toadd->GetClientPort())) {
				// pre-filter sources which would be dropped in CPartFile::AddSources
				if (CPartFile::CanAddSource(toadd->GetClientID(), toadd->GetClientPort(), toadd->GetClientServerIP(), toadd->GetClientServerPort())) {
					CSearchFile::SClient client(toadd->GetClientID(), toadd->GetClientPort(), toadd->GetClientServerIP(), toadd->GetClientServerPort());
					if (parent->GetClients().Find(client) < 0)
						parent->AddClient(client);
				}
			} else if (thePrefs.GetDebugServerSearchesLevel() > 1)
				Debug(_T("Filtered source from search result %s:%u\n"), (LPCTSTR)DbgGetClientID(toadd->GetClientID()), toadd->GetClientPort());

			// copy possible available servers from new search result entry to parent
			// will be used in future
			if (toadd->GetClientServerIP() && toadd->GetClientServerPort()) {
				CSearchFile::SServer server(toadd->GetClientServerIP(), toadd->GetClientServerPort(), toadd->IsServerUDPAnswer());
				int iFound = parent->GetServers().Find(server);
				if (iFound == -1) {
					server.m_uAvail = uAvail;
					parent->AddServer(server);
				} else
					parent->GetServerAt(iFound).m_uAvail += uAvail;
			}

			UINT uAllChildrenSourceCount = 0;			// ed2k: sum of all sources, kad: the max. sources found
			UINT uAllChildrenCompleteSourceCount = 0; // ed2k: sum of all sources, kad: the max. sources found
			UINT uDifferentNames = 0; // max known different names
			UINT uPublishersKnown = 0; // max publishers known (might be changed to median)
			UINT uTrustValue = 0; // average trust value (might be changed to median)
			uint32 nPublishInfoTags = 0;
			const CSearchFile *bestEntry = NULL;
			bool bHasMultipleAICHHashes = false;
			CAICHHash aichHash;
			bool bAICHHashValid = false;
			for (POSITION pos2 = list->GetHeadPosition(); pos2 != NULL;) {
				const CSearchFile *child = list->GetNext(pos2);
				if (child->GetListParent() == parent) {
					const CFileIdentifier &fileid = child->GetFileIdentifierC();
					// figure out if the children of different AICH hashes
					if (fileid.HasAICHHash()) {
						if (bAICHHashValid && aichHash != fileid.GetAICHHash())
							bHasMultipleAICHHashes = true;
						else if (!bAICHHashValid) {
							aichHash = fileid.GetAICHHash();
							bAICHHashValid = true;
						}
					} else if (child->HasFoundMultipleAICH())
						bHasMultipleAICHHashes = true;

					if (parent->IsKademlia()) {
						if (child->GetListChildCount() > uAllChildrenSourceCount)
							uAllChildrenSourceCount = child->GetListChildCount();
						uint32 u = child->GetKadPublishInfo();
						if (u != 0) {
							++nPublishInfoTags;
							uDifferentNames = max(uDifferentNames, (u >> 24) & 0xFF);
							uPublishersKnown = max(uPublishersKnown, (u >> 16) & 0xFF);
							uTrustValue += u & 0x0000FFFF;
						}
					} else {
						uAllChildrenSourceCount += child->GetListChildCount();
						uAllChildrenCompleteSourceCount += child->GetCompleteSourceCount();
					}

					if (bestEntry == NULL || child->GetListChildCount() > bestEntry->GetListChildCount())
						bestEntry = child;
				}
			}
			if (bestEntry) {
				parent->SetFileSize(bestEntry->GetFileSize());
				parent->SetAFileName(bestEntry->GetFileName());
				parent->SetFileType(bestEntry->GetFileType());
				parent->SetSourceCount(uAllChildrenSourceCount);
				parent->SetCompleteSourceCount(uAllChildrenCompleteSourceCount);
				if (nPublishInfoTags > 0)
					uTrustValue /= nPublishInfoTags;
				parent->SetKadPublishInfo(((uDifferentNames & 0xff) << 24) | ((uPublishersKnown & 0xff) << 16) | ((uTrustValue & 0xffff) << 0));
				// if all children have the same AICH hash (or none), set the parent hash to it, otherwise clear it (see KademliaSearchKeyword)
				if (bHasMultipleAICHHashes || !bAICHHashValid)
					parent->GetFileIdentifier().ClearAICHHash();
				else //if (bAICHHashValid) always true
					parent->GetFileIdentifier().SetAICHHash(aichHash);
			}

			// Calculate spam rating skipping duplicates. Don't calculate spam rating when we are just merging tabs.
			if (!bFound && bDoSpamRating)
				DoSpamRating(toadd, bClientResponse, Calculate, false, dwFromUDPServerIP);

			// add the 'Availability' of the new search result entry to the total search result count for this search
			AddResultCount(parent->GetSearchID(), parent->GetFileHash(), uAvail, parent->IsConsideredSpam());

			// update parent in GUI
			if (outputwnd)
				outputwnd->UpdateSources(parent, false);

			if (bFound) {
				delete toadd;
			}
			return true;
		}
	}

	// no bounded result found yet -> add as parent to list
	toadd->SetListParent(NULL);
	UINT uAvail = toadd->GetSourceCount();
	if (list->AddTail(toadd)) {
		UINT tempValue;
		if (!m_foundFilesCount.Lookup(toadd->GetSearchID(), tempValue))
			tempValue = 0;
		m_foundFilesCount[toadd->GetSearchID()] = tempValue + 1;

		if (bDoSpamRating) {
			if (!m_originalFoundFilesCount.Lookup(toadd->GetSearchID(), tempValue))
				tempValue = 0;
			m_originalFoundFilesCount[toadd->GetSearchID()] = tempValue + 1;
		}

		// get the 'Availability' of this new search result entry
		if (bClientResponse)
			// If this is a response from a client ("View Shared Files"), we set the "Availability" at least to 1.
			toadd->AddSources(uAvail ? uAvail : 1);
	}

	if (thePrefs.GetDebugSearchResultDetailLevel() >= 1)
		toadd->SetListExpanded(true);

	if (bDoSpamRating) // Don't calculate spam rating when we are just merging tabs.
		DoSpamRating(toadd, bClientResponse, Calculate, false, dwFromUDPServerIP); // Calculate spam rating

	// add the 'Availability' of this new search result entry to the total search result count for this search
	AddResultCount(toadd->GetSearchID(), toadd->GetFileHash(), uAvail, toadd->IsConsideredSpam());

	// This will be done by ReloadList later

	return true;
}

CSearchFile* CSearchList::GetSearchFileByHash(const uchar *hash) const
{
	for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
		const SearchListsStruct *listCur = m_listFileLists.GetNext(pos);
		for (POSITION pos2 = listCur->m_listSearchFiles.GetHeadPosition(); pos2 != NULL;) {
			CSearchFile *sf = listCur->m_listSearchFiles.GetNext(pos2);
			if (md4equ(hash, sf->GetFileHash()))
				return sf;
		}
	}
	return NULL;
}

bool CSearchList::AddNotes(const Kademlia::CEntry &cEntry, const uchar *hash)
{
	bool flag = false;
	for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
		const SearchListsStruct *listCur = m_listFileLists.GetNext(pos);
		for (POSITION pos2 = listCur->m_listSearchFiles.GetHeadPosition(); pos2 != NULL;) {
			CSearchFile *sf = listCur->m_listSearchFiles.GetNext(pos2);
			if (md4equ(hash, sf->GetFileHash()) && sf->AddNote(cEntry))
				flag = true;
		}
	}
	return flag;
}

void CSearchList::SetNotesSearchStatus(const uchar *pFileHash, bool bSearchRunning)
{
	for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
		const SearchListsStruct *listCur = m_listFileLists.GetNext(pos);
		for (POSITION pos2 = listCur->m_listSearchFiles.GetHeadPosition(); pos2 != NULL;) {
			CSearchFile *sf = listCur->m_listSearchFiles.GetNext(pos2);
			if (md4equ(pFileHash, sf->GetFileHash()))
				sf->SetKadCommentSearchRunning(bSearchRunning);
		}
	}
}

void CSearchList::AddResultCount(uint32 nSearchID, const uchar *hash, UINT nCount, bool bSpam)
{
	// do not count already available or downloading files for the search result limit
	if (theApp.sharedfiles->GetFileByID(hash) || theApp.downloadqueue->GetFileByID(hash))
		return;

	UINT tempValue;
	if (!m_foundSourcesCount.Lookup(nSearchID, tempValue))
		tempValue = 0;

	// spam files count as max 5 availability
	m_foundSourcesCount[nSearchID] = tempValue + ((bSpam && thePrefs.IsSearchSpamFilterEnabled()) ? min(nCount, 5) : nCount);
}

// FIXME LARGE FILES
void CSearchList::KademliaSearchKeyword(uint32 nSearchID, const Kademlia::CUInt128 *pFileID, LPCTSTR name
	, uint64 size, LPCTSTR type, UINT uKadPublishInfo
	, CArray<CAICHHash> &raAICHHashes, CArray<uint8, uint8> &raAICHHashPopularity
	, SSearchTerm *pQueriedSearchTerm, UINT numProperties, ...)
{
	va_list args;
	va_start(args, numProperties);

	EUTF8str eStrEncode = UTF8strRaw;
	Kademlia::CKeyEntry verifierEntry;

	verifierEntry.m_uKeyID.SetValue(*pFileID);
	uchar fileid[16];
	pFileID->ToByteArray(fileid);

	CSafeMemFile temp(250);
	temp.WriteHash16(fileid);
	temp.WriteUInt32(0);	// client IP
	temp.WriteUInt16(0);	// client port

	// write tag list
	UINT uFilePosTagCount = (UINT)temp.GetPosition();
	temp.WriteUInt32(0); // dummy tag count, will be filled later

	uint32 tagcount = 0;
	// standard tags
	CTag tagName(FT_FILENAME, name);
	tagName.WriteTagToFile(temp, eStrEncode);
	++tagcount;
	verifierEntry.SetFileName(Kademlia::CKadTagValueString(name));

	CTag tagSize(FT_FILESIZE, size, true);
	tagSize.WriteTagToFile(temp, eStrEncode);
	++tagcount;
	verifierEntry.m_uSize = size;

	if (type != NULL && type[0] != _T('\0')) {
		CTag tagType(FT_FILETYPE, type);
		tagType.WriteTagToFile(temp, eStrEncode);
		++tagcount;
		verifierEntry.AddTag(new Kademlia::CKadTagStr(TAG_FILETYPE, type));
	}

	// additional tags
	for (; numProperties > 0; --numProperties) {
		UINT uPropType = va_arg(args, UINT);
		LPCSTR pszPropName = va_arg(args, LPCSTR);
		LPVOID pvPropValue = va_arg(args, LPVOID);
		if (uPropType == TAGTYPE_STRING) {
			if ((LPCTSTR)pvPropValue != NULL && ((LPCTSTR)pvPropValue)[0] != _T('\0')) {
				if (strlen(pszPropName) == 1) {
					CTag tagProp((uint8)*pszPropName, (LPCTSTR)pvPropValue);
					tagProp.WriteTagToFile(temp, eStrEncode);
				} else {
					CTag tagProp(pszPropName, (LPCTSTR)pvPropValue);
					tagProp.WriteTagToFile(temp, eStrEncode);
				}
				verifierEntry.AddTag(new Kademlia::CKadTagStr(pszPropName, (LPCTSTR)pvPropValue));
				++tagcount;
			}
		} else if (uPropType == TAGTYPE_UINT32) {
			if ((uint32)pvPropValue != 0) {
				CTag tagProp(pszPropName, (uint32)pvPropValue);
				tagProp.WriteTagToFile(temp, eStrEncode);
				++tagcount;
				verifierEntry.AddTag(new Kademlia::CKadTagUInt(pszPropName, (uint32)pvPropValue));
			}
		} else
			ASSERT(0);
	}
	va_end(args);
	temp.Seek(uFilePosTagCount, CFile::begin);
	temp.WriteUInt32(tagcount);

	if (pQueriedSearchTerm == NULL || verifierEntry.StartSearchTermsMatch(*pQueriedSearchTerm)) {
		temp.SeekToBegin();
		CSearchFile *tempFile = new CSearchFile(temp, eStrEncode == UTF8strRaw, nSearchID, 0, 0, NULL, true);
		tempFile->SetKadPublishInfo(uKadPublishInfo);
		// About the AICH hash: We received a list of possible AICH hashes for this file and now have to decide what to do
		// If it wasn't for backwards compatibility, the choice would be easy: Each different md4+aich+size is its own result,
		// but we can't do this alone for the fact that for the next years we will always have publishers which don't report
		// the AICH hash at all (which would mean having a different entry, which leads to double files in search results).
		// So here is what we do for now:
		// If we have exactly 1 AICH hash and more than 1/3 of the publishers reported it, we set it as verified AICH hash for
		// the file (which is as good as using an ed2k link with an AICH hash attached). If less publishers reported it or if we
		// have multiple AICH hashes, we ignore them and use the MD4 only.
		// This isn't a perfect solution, but it makes sure not to open any new attack vectors (a wrong AICH hash means we cannot
		// download the file successfully) nor to confuse users by requiring them to select an entry out of several equal looking results.
		// Once the majority of nodes in the network publishes AICH hashes, this might get reworked to make the AICH hash more sticky
		if (raAICHHashes.GetCount() == 1 && raAICHHashPopularity.GetCount() == 1) {
			uint8 byPublishers = (uint8)((uKadPublishInfo >> 16) & 0xFF);
			if (byPublishers > 0 && raAICHHashPopularity[0] > 0 && byPublishers / raAICHHashPopularity[0] <= 3) {
				DEBUG_ONLY(DebugLog(_T("Received accepted AICH Hash for search result %s, %u out of %u Publishers, Hash: %s")
					, (LPCTSTR)EscPercent(tempFile->GetFileName()), raAICHHashPopularity[0], byPublishers, (LPCTSTR)raAICHHashes[0].GetString()));
				tempFile->GetFileIdentifier().SetAICHHash(raAICHHashes[0]);
			} else
				DEBUG_ONLY(DebugLog(_T("Received unaccepted AICH Hash for search result %s, %u out of %u Publishers, Hash: %s")
					, (LPCTSTR)EscPercent(tempFile->GetFileName()), raAICHHashPopularity[0], byPublishers, (LPCTSTR)raAICHHashes[0].GetString()));
		} else if (raAICHHashes.GetCount() > 1)
			DEBUG_ONLY(DebugLog(_T("Received multiple (%u) AICH hashes for search result %s, ignoring AICH"), raAICHHashes.GetCount(), (LPCTSTR)EscPercent(tempFile->GetFileName())));
		AddToList(tempFile, false, 0, true);
		if (outputwnd) {
			const DWORD curTick = ::GetTickCount();
			if (nSearchID == theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.m_nResultsID) {
				if (curTick >= m_dwKadLastReloadTick + KADEMLIASEARCHLISTRELOADELAY) {
					m_bKadReloadWaiting = false;
					m_dwKadLastReloadTick = curTick;
					outputwnd->ReloadList(false, LSF_SELECTION);
				} else
					m_bKadReloadWaiting = true;
			} else
				outputwnd->UpdateTabHeader(nSearchID, EMPTY, false);
		}
	} else
		DebugLogWarning(_T("Kad Searchresult failed sanitize check against search query, ignoring. (%s)"), (LPCTSTR)EscPercent(name));
}


// default spam threshold = 60
#define SPAM_FILEHASH_HIT					100

#define SPAM_FULLNAME_HIT					80
#define	SPAM_SMALLFULLNAME_HIT				50
#define SPAM_SIMILARNAME_HIT				60
#define SPAM_SMALLSIMILARNAME_HIT			40
#define SPAM_SIMILARNAME_NEARHIT			50
#define SPAM_SIMILARNAME_FARHIT				40

#define SPAM_SIMILARSIZE_HIT				10

#define SPAM_UDPSERVERRES_HIT				21
#define SPAM_UDPSERVERRES_NEARHIT			15
#define SPAM_UDPSERVERRES_FARHIT			10

#define SPAM_ONLYUDPSPAMSERVERS_HIT			30

#define SPAM_SOURCE_HIT						39

#define SPAM_HEURISTIC_BASEHIT				39
#define SPAM_HEURISTIC_MAXHIT				60


#define UDP_SPAMRATIO_THRESHOLD				50

// Returns false only if processing is completely skipped by the first check
bool CSearchList::DoSpamRating(CSearchFile *pSearchFile, bool bIsClientFile, uint8 uActionType, bool bUpdate, uint32 dwFromUDPServerIP)
{
	/* This spam filter uses two simple approaches to try to identify spam search results:
	1 - detect general characteristics of fake results - not very reliable
		which are (each hit increases the score)
		* high availability from one udp server, but none from others
		* archive or program + size between 0,1 and 10 MB
		* 100% complete sources together with high availability
		Apparently, those characteristics target for current spyware fake results, other fake results like videos
		and so on will not be detectable, because only the first point is more or less common for server fake results,
		which would produce too many false positives

	2 - learn characteristics of files a user has marked as spam
		remembered data is:
		* FileHash (of course, a hit will always lead to a full score rating)
		* Equal filename
		* Equal or similar name after removing the search keywords and separators
			(if search for "emule", "blubby!! emule foo.rar" is remembered as "blubby foo rar")
		* Similar size (+- 5% but max 5MB) as other spam files
		* Equal search source server (UDP only)
		* Equal initial source clients
		* Ratio (Spam / NotSpam) of UDP Servers
	Both detection methods add to the same score rating.

	uActionType == MarkAsNotSpam Will remove all stored characteristics which would add to a positive spam score for this file
	*/

	if (!pSearchFile)
		return false; // No file to process

	if (pSearchFile->GetKnownType() == CSearchFile::NotDetermined) {
		if ((pSearchFile->GetListParent() && pSearchFile->GetListParent()->GetKnownType() != CSearchFile::NotDetermined)) // Check if this is a child item with a parent already marked as a known file
			pSearchFile->SetKnownType(pSearchFile->GetListParent()->GetKnownType());
		else // This is not a child item, so we need to check if the file is a known file
			SetSearchItemKnownType(pSearchFile);

		if (pSearchFile->GetKnownType() != CSearchFile::NotDetermined) 
			return true; // If this is a known file, then we don't need to proceed further with spam/blacklist checks
	} else
		return true; // If this is a known file, then we don't need to proceed further with spam/blacklist checks

	// Return if spam/blacklist checks are disabled by user. Also return if if this is a known file. Because marking as known files has a priority over marking as spam/blacklisted. 
	if ((!thePrefs.IsSearchSpamFilterEnabled() && !thePrefs.GetBlacklistManual() && !thePrefs.GetBlacklistAutomatic()))
		return false;

	if (!m_bSpamFilterLoaded)
		LoadSpamFilter();

	int nSpamScore = 0;
	CString strDebug;
	bool bSureNegative = false;
	int nDbgFileHash, nDbgStrings, nDbgSize, nDbgServer, nDbgSources, nDbgHeuristic, nDbgOnlySpamServer;
	nDbgFileHash = nDbgStrings = nDbgSize = nDbgServer = nDbgSources = nDbgHeuristic = nDbgOnlySpamServer = 0;

	CSearchFile* pParent = NULL;
	if (pSearchFile->GetListParent())
		pParent = pSearchFile->GetListParent();
	else if (pSearchFile->GetListChildCount() > 0)
		pParent = pSearchFile;

	if (uActionType == Calculate && thePrefs.GetBlacklistAutomatic()) { // Check if we need to calculate Automatic Blacklist and Spam Rating
		// 0.1- Automatic Blacklist (based on user definitions)
		// We need to bypass this when MarkAsNotSpam is true since it needs to do some important staff including calling UpdateSources and RecalculateSpamRatings.
		pSearchFile->SetAutomaticBlacklisted(false); // Reset Automatic Blacklist flag before checking conditions

		// Check if this is a child item with a parent already marked as AutoBlacklisted or a parent item with IsFilenameAutoBlacklisted = true
		if ((pSearchFile->GetListParent() && pSearchFile->GetListParent()->GetAutomaticBlacklisted()) || IsFilenameAutoBlacklisted(pSearchFile->GetFileName())) {
			pSearchFile->SetAutomaticBlacklisted(true);
			if (thePrefs.GetBlacklistAutoRemoveFromManual()) {
				pSearchFile->SetManualBlacklisted(false);
				m_mapBlacklistedHashes.RemoveKey(CSKey(pSearchFile->GetFileHash()));
			}
		}
		
		// Mark parent and children as Automatic Blacklisted
		if (pSearchFile->GetAutomaticBlacklisted() && pParent) {
			pParent->SetAutomaticBlacklisted(true); // Mark parent as Automatic Blacklisted
			// Remove parent from Manual Blacklist if it was already there
			if (thePrefs.GetBlacklistAutoRemoveFromManual()) {
				pParent->SetManualBlacklisted(false);
				m_mapBlacklistedHashes.RemoveKey(CSKey(pParent->GetFileHash()));
			}

			// Mark children as Automatic Blacklisted
			const SearchList* list = GetSearchListForID(pParent->GetSearchID());
			for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
				CSearchFile* pCurFile = list->GetNext(pos);
				if (pCurFile->GetListParent() == pParent) {
					pCurFile->SetAutomaticBlacklisted(true);
					// Remove child from Manual Blacklist if it was already there
					if (thePrefs.GetBlacklistAutoRemoveFromManual()) {
						pCurFile->SetManualBlacklisted(false);
						m_mapBlacklistedHashes.RemoveKey(CSKey(pCurFile->GetFileHash()));
					}
				}
			}
		}

		if (!thePrefs.IsSearchSpamFilterEnabled() && bUpdate && outputwnd)
			outputwnd->UpdateSources(pParent ? pParent : pSearchFile, true); // Update pSearchFile and sources in the output window

		// If file is marked as Automatic Blacklisted, then there's no need to proceed for the spam calculations since this will already be listed as spam unless we're coming here from the context menu
		// or GetBlacklistAutoRemoveFromManual is false. For the second case manual blacklist need to be calculated since it is priority over automatic while showing icon and Known column.
		if (pSearchFile->GetAutomaticBlacklisted())
			return true;
	}
	
	if (((uActionType == Calculate || uActionType == MarkAsBlacklisted || uActionType == MarkAsNotBlacklisted)) && thePrefs.GetBlacklistManual()) { // Check if we need to calculate/mark/unmark as Manual Blacklisted and Manual Blacklist is enabled
		// 0.2- Manual Blacklist (based on file hash)
		// Do not perform this checks if file is already automatic blacklisted.
		// We need to bypass this when bMarkAsNotSpam is true since it needs to do some important staff including calling UpdateSources and RecalculateSpamRatings.
		bool m_bDummyVar;

		if (uActionType == MarkAsNotBlacklisted) { // Function is called to remove file from Manual Blacklist
			m_mapBlacklistedHashes.RemoveKey(CSKey(pSearchFile->GetFileHash()));
			pSearchFile->SetManualBlacklisted(false);
		} else if ((pSearchFile->GetListParent() && pSearchFile->GetListParent()->GetManualBlacklisted())) // This is a child item with a parent already marked as ManualBlacklisted (Since this case has a lower cost action, we check this before uActionType == MarkAsBlacklisted)
			pSearchFile->SetManualBlacklisted(true);
		else if (uActionType == MarkAsBlacklisted || m_mapBlacklistedHashes.Lookup(CSKey(pSearchFile->GetFileHash()), m_bDummyVar) || // Function is called to add file to Manual Blacklist OR this is already marked as Manual Blacklisted
				(thePrefs.GetDownloadChecker() && thePrefs.GetDownloadCheckerAutoMarkAsBlacklisted() &&	theApp.DownloadChecker->CheckFile(pSearchFile->GetFileHash(), pSearchFile->GetFileName(), pSearchFile->GetFileSize(), false))) { // OR this is not blacklisted but DownloadChecker marks this file as manuel blacklisted
			pSearchFile->SetManualBlacklisted(true);
			MarkHashAsBlacklisted(CSKey(pSearchFile->GetFileHash()));
		}

		// Mark parent and children as Manual Blacklisted if MarkAsNotBlacklisted is false, otherwise mark them as not Manual Blacklisted.
		// Adding to or removing from the map has been lready done above. Since map is based file hash, a single operation is enough.
		if (pParent) {
			pParent->SetManualBlacklisted(pSearchFile->GetManualBlacklisted()); // Mark parent
			// Mark children
			const SearchList* list = GetSearchListForID(pParent->GetSearchID());
			for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
				CSearchFile* pCurFile = list->GetNext(pos);
				if (pCurFile->GetListParent() == pParent)
					pCurFile->SetManualBlacklisted(pParent->GetManualBlacklisted());
			}
		}

		if (bUpdate && outputwnd)
			outputwnd->UpdateSources(pParent ? pParent : pSearchFile, true); // Update pSearchFile and sources in the output window

		if (pSearchFile->GetManualBlacklisted() || uActionType != Calculate)
			return true; // There's no need to proceed for the spam calculations since pSearchFile is calculated as Manual Blacklisted or function is called to MarkAsSpam/MarkAsNotSpam only
	}

	if (!thePrefs.IsSearchSpamFilterEnabled()) // If spam filter is disabled, then there's no need to proceed further
		return true;

	CSearchFile* pTempFile = pSearchFile->GetListParent() ? pSearchFile->GetListParent() : pSearchFile;
	bool bOldSpamStatus = false;

	if (pSearchFile->GetListParent() && pSearchFile->GetListParent()->IsConsideredSpam(false))	// If this is a child item with a parent already marked as spam
		pSearchFile->SetSpamRating(pSearchFile->GetListParent()->GetSpamRating());
	else if (uActionType == MarkAsNotSpam) {
		// Remove file name
		for (int i = m_astrKnownSpamNames.GetSize() - 1; i >= 0; --i)
			if (m_astrKnownSpamNames[i].CompareNoCase(pSearchFile->GetFileName()) == 0) {
				m_astrKnownSpamNames.RemoveAt(i);
				break;
			}

		// Remove file name without keyword
		for (int i = m_astrKnownSimilarSpamNames.GetSize() - 1; i >= 0; --i)
			if (m_astrKnownSimilarSpamNames[i].CompareNoCase(pSearchFile->GetNameWithoutKeyword()) == 0) {
				m_astrKnownSimilarSpamNames.RemoveAt(i);
				break;
			}

		// Remove hash entry
		m_mapKnownSpamHashes.RemoveKey(CSKey(pSearchFile->GetFileHash()));

		// Remove file size
		for (int i = m_aui64KnownSpamSizes.GetSize() - 1; i >= 0; --i)
			if (m_aui64KnownSpamSizes[i] == static_cast<uint64>(pSearchFile->GetFileSize())) {
				m_aui64KnownSpamSizes.RemoveAt(i);
				break;
			}
	} else if (uActionType == MarkAsSpam) {
		m_astrKnownSpamNames.Add(pSearchFile->GetFileName()); // Add file name
		m_astrKnownSimilarSpamNames.Add(pSearchFile->GetNameWithoutKeyword()); // Add file name without keyword
		m_mapKnownSpamHashes[CSKey(pSearchFile->GetFileHash())] = true; // Add hash entry
		m_aui64KnownSpamSizes.Add((uint64)pSearchFile->GetFileSize()); // Add file size

		if (IsValidSearchResultClientIPPort(pSearchFile->GetClientID(), pSearchFile->GetClientPort()) && !::IsLowID(pSearchFile->GetClientID()))
			m_mapKnownSpamSourcesIPs[pSearchFile->GetClientID()] = true;

		for (int i = pSearchFile->GetClients().GetSize(); --i >= 0;)
			if (pSearchFile->GetClients()[i].m_nIP != 0)
				m_mapKnownSpamSourcesIPs[pSearchFile->GetClients()[i].m_nIP] = true;

		for (int i = pSearchFile->GetServers().GetSize(); --i >= 0;)
			if (pSearchFile->GetServers()[i].m_nIP != 0 && pSearchFile->GetServers()[i].m_bUDPAnswer)
				m_mapKnownSpamServerIPs[pSearchFile->GetServers()[i].m_nIP] = true;
	} else {
		// 1- file hash
		bool bSpam;
		if (m_mapKnownSpamHashes.Lookup(CSKey(pSearchFile->GetFileHash()), bSpam)) {
			if (bSpam) {
				nSpamScore += SPAM_FILEHASH_HIT;
				nDbgFileHash = SPAM_FILEHASH_HIT;
			} else
				bSureNegative = true;
		}

		if (bSureNegative) {
			// 2-3 FileNames: Consider also filenames of children / parents / siblings and take the highest rating
			uint32 nHighestRating;
			if (pParent) {
				nHighestRating = GetSpamFilenameRatings(pParent, false);
				const SearchList *list = GetSearchListForID(pParent->GetSearchID());
				for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
					const CSearchFile *pCurFile = list->GetNext(pos);
					if (pCurFile->GetListParent() == pParent) {
						uint32 nRating = GetSpamFilenameRatings(pCurFile, false);
						nHighestRating = max(nHighestRating, nRating);
					}
				}
			} else
				nHighestRating = GetSpamFilenameRatings(pSearchFile, false);
			nSpamScore += nHighestRating;
			nDbgStrings = nHighestRating;

			//4 - Sizes
			for (INT_PTR i = m_aui64KnownSpamSizes.GetCount(); --i >= 0;) {
				uint64 fsize = (uint64)pSearchFile->GetFileSize();
				if (fsize != 0 && _abs64(fsize - m_aui64KnownSpamSizes[i]) < 5242880 && ((_abs64(fsize - m_aui64KnownSpamSizes[i]) * 100) / fsize) < 5)	{
					nSpamScore += SPAM_SIMILARSIZE_HIT;
					nDbgSize = SPAM_SIMILARSIZE_HIT;
					break;
				}
			}
			if (!bIsClientFile) { // only to skip some useless calculations
				const CSimpleArray<CSearchFile::SServer> &aservers = pTempFile->GetServers();
				//5 Servers
				for (int i = 0; i != aservers.GetSize(); ++i) {
					bool bFound = false;
					if (aservers[i].m_nIP != 0 && aservers[i].m_bUDPAnswer && m_mapKnownSpamServerIPs.Lookup(aservers[i].m_nIP, bFound)) {
						strDebug.AppendFormat(_T(" (Serverhit: %s)"), (LPCTSTR)ipstr(aservers[i].m_nIP));
						if (pSearchFile->GetServers().GetSize() == 1 && m_mapKnownSpamServerIPs.GetCount() <= 10) {
							// source only from one server
							nSpamScore += SPAM_UDPSERVERRES_HIT;
							nDbgServer = SPAM_UDPSERVERRES_HIT;
						} else if (pSearchFile->GetServers().GetSize() == 1) {
							// source only from one server but the users seems to be a bit careless with the mark as spam option and has already added a lot UDP servers. To avoid false positives, we give a lower rating
							nSpamScore += SPAM_UDPSERVERRES_NEARHIT;
							nDbgServer = SPAM_UDPSERVERRES_NEARHIT;
						} else {
							// file was given by more than one server, lowest spam rating for server hits
							nSpamScore += SPAM_UDPSERVERRES_FARHIT;
							nDbgServer = SPAM_UDPSERVERRES_FARHIT;
						}
						break;
						m_mapKnownSpamServerIPs.RemoveKey(aservers[i].m_nIP);
					}
				}

				// partial heuristics - only udp spam servers have this file at least one server as origin which is not rated for spam or UDP or not a result from a server at all
				bool bNormalServerWithoutCurrentPresent = (aservers.GetSize() == 0);
				bool bNormalServerPresent = bNormalServerWithoutCurrentPresent;
				for (int i = 0; i < aservers.GetSize(); ++i) {
					UDPServerRecord *pRecord = NULL;
					if (aservers[i].m_bUDPAnswer && m_mUDPServerRecords.Lookup(aservers[i].m_nIP, pRecord) && pRecord != NULL) {
						ASSERT(pRecord->m_nResults >= pRecord->m_nSpamResults);
						if (pRecord->m_nResults >= pRecord->m_nSpamResults && pRecord->m_nResults > 0) {
							int nRatio = (pRecord->m_nSpamResults * 100) / pRecord->m_nResults;
							if (nRatio < 50) {
								bNormalServerWithoutCurrentPresent |= (dwFromUDPServerIP != aservers[i].m_nIP);
								bNormalServerPresent = true;
							}
						}
					} else if (!aservers[i].m_bUDPAnswer) {
						bNormalServerWithoutCurrentPresent = true;
						bNormalServerPresent = true;
						break;
					}
					ASSERT(pRecord);
				}
				if (!bNormalServerPresent) {
					nDbgOnlySpamServer = SPAM_ONLYUDPSPAMSERVERS_HIT;
					nSpamScore += SPAM_ONLYUDPSPAMSERVERS_HIT;
					strDebug += _T(" (AllSpamServers)");
				} else if (!bNormalServerWithoutCurrentPresent)
					strDebug += _T(" (AllSpamServersWoCurrent)");


				// 7 Heuristic (UDP Results)
				uint32 nResponses;
				if (!m_ReceivedUDPAnswersCount.Lookup(pTempFile->GetSearchID(), nResponses))
					nResponses = 0;
				uint32 nRequests;

				if (!m_RequestedUDPAnswersCount.Lookup(pTempFile->GetSearchID(), nRequests))
					nRequests = 0;

				if (!bNormalServerWithoutCurrentPresent	&& (nResponses >= 3 || nRequests >= 5) && pTempFile->GetSourceCount() > 100) {
					// check if the one of the files sources are in the same ip subnet as a udp server
					// which indicates that the server is advertising its own files
					bool bSourceServer = false;
					for (int i = 0; i < aservers.GetSize(); ++i) {
						if (aservers[i].m_nIP != 0) {
							if ((aservers[i].m_nIP & 0x00FFFFFF) == (pTempFile->GetClientID() & 0x00FFFFFF)) {
								bSourceServer = true;
								strDebug.AppendFormat(_T(" (Server: %s - Source: %s Hit)"), (LPCTSTR)ipstr(aservers[i].m_nIP), (LPCTSTR)ipstr(pTempFile->GetClientID()));
								break;
							}

							for (int j = 0; j < pTempFile->GetClients().GetSize(); ++j) {
								if ((aservers[i].m_nIP & 0x00FFFFFF) == (pTempFile->GetClients()[j].m_nIP & 0x00FFFFFF)) {
									bSourceServer = true;
									strDebug.AppendFormat(_T(" (Server: %s - Source: %s Hit)"), (LPCTSTR)ipstr(aservers[i].m_nIP), (LPCTSTR)ipstr(pTempFile->GetClients()[j].m_nIP));
									break;
								}
							}
						}
					}

					if (((GetED2KFileTypeID(pTempFile->GetFileName()) == ED2KFT_PROGRAM || GetED2KFileTypeID(pTempFile->GetFileName()) == ED2KFT_ARCHIVE)
							&& (uint64)pTempFile->GetFileSize() > 102400 && (uint64)pTempFile->GetFileSize() < 10485760)
							|| bSourceServer) {
						nSpamScore += SPAM_HEURISTIC_MAXHIT;
						nDbgHeuristic = SPAM_HEURISTIC_MAXHIT;
					} else {
						nSpamScore += SPAM_HEURISTIC_BASEHIT;
						nDbgHeuristic = SPAM_HEURISTIC_BASEHIT;
					}
				}
			}
			// 6 Sources
			bool bFound = false;
			if (IsValidSearchResultClientIPPort(pTempFile->GetClientID(), pTempFile->GetClientPort())
				&& !::IsLowID(pTempFile->GetClientID())
				&& m_mapKnownSpamSourcesIPs.Lookup(pTempFile->GetClientID(), bFound)) {
				strDebug.AppendFormat(_T(" (Sourceshit: %s)"), (LPCTSTR)ipstr(pTempFile->GetClientID()));
				nSpamScore += SPAM_SOURCE_HIT;
				nDbgSources = SPAM_SOURCE_HIT;
			} else {
				for (int i = 0; i != pTempFile->GetClients().GetSize(); ++i)
					if (pTempFile->GetClients()[i].m_nIP != 0 && m_mapKnownSpamSourcesIPs.Lookup(pTempFile->GetClients()[i].m_nIP, bFound)) {
						strDebug.AppendFormat(_T(" (Sources: %s)"), (LPCTSTR)ipstr(pTempFile->GetClients()[i].m_nIP));
						nSpamScore += SPAM_SOURCE_HIT;
						nDbgSources = SPAM_SOURCE_HIT;
						break;
						m_mapKnownSpamSourcesIPs.RemoveKey(pTempFile->GetClients()[i].m_nIP);
					}
			}
		}
	}

	bOldSpamStatus = pSearchFile->IsConsideredSpam();
	pSearchFile->SetSpamRating(uActionType == MarkAsNotSpam ? 0 : uActionType == MarkAsSpam ? SPAM_FILEHASH_HIT : nSpamScore);
	// If this item is marked as spam, then we need to update the parent and all its childs
	if ((uActionType == MarkAsNotSpam || uActionType == MarkAsSpam || pSearchFile->IsConsideredSpam(false)) && pParent) {
		pParent->SetSpamRating(pSearchFile->GetSpamRating()); // Mark parent as spam
		// Mark all children as spam
		const SearchList* list = GetSearchListForID(pParent->GetSearchID());
		for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
			CSearchFile* pCurFile = list->GetNext(pos);
			if (pCurFile->GetListParent() == pParent)
				pCurFile->SetSpamRating(pParent->GetSpamRating());
		}
	}

	if (uActionType == MarkAsNotSpam) {
		if (nSpamScore > 0)
			if (thePrefs.GetLogSpamRating())
				DebugLog(_T("Spamrating Result: %u. Details: Hash: %u, Name: %u, Size: %u, Server: %u, Sources: %u, Heuristic: %u, OnlySpamServers: %u. %s Filename: %s")
				, bSureNegative ? 0 : nSpamScore, nDbgFileHash, nDbgStrings, nDbgSize, nDbgServer, nDbgSources, nDbgHeuristic, nDbgOnlySpamServer, (LPCTSTR)EscPercent(strDebug), (LPCTSTR)EscPercent(pSearchFile->GetFileName()));
	} else
		DebugLog(_T("Marked file as No Spam, Old Rating: %u."), pSearchFile->GetSpamRating());

	// keep record about ratio of spam in UDP server results
	if (bOldSpamStatus != pSearchFile->IsConsideredSpam()) {
		const CSimpleArray<CSearchFile::SServer> &aservers = pTempFile->GetServers();
		for (int i = 0; i < aservers.GetSize(); ++i) {
			UDPServerRecord *pRecord;
			if (aservers[i].m_bUDPAnswer && m_mUDPServerRecords.Lookup(aservers[i].m_nIP, pRecord) && pRecord) {
				if (pSearchFile->IsConsideredSpam())
					++pRecord->m_nSpamResults;
				else {
					ASSERT(pRecord->m_nSpamResults > 0);
					--pRecord->m_nSpamResults;
				}
			}
		}
	} else if (dwFromUDPServerIP != 0 && pSearchFile->IsConsideredSpam()) {
		// files were a spam already, but server returned it in results - add it to server's spam stats
		const CUDPServerRecordMap::CPair *pair = m_mUDPServerRecords.PLookup(dwFromUDPServerIP);
		if (pair)
			++pair->value->m_nSpamResults;
	}

	if (bUpdate && outputwnd)
		outputwnd->UpdateSources(pParent ? pParent : pSearchFile, true);

	return true;
}

uint32 CSearchList::GetSpamFilenameRatings(const CSearchFile *pSearchFile, bool bMarkAsNotSpam)
{
	for (INT_PTR i = m_astrKnownSpamNames.GetCount(); --i >= 0;) {
		if (pSearchFile->GetFileName().CompareNoCase(m_astrKnownSpamNames[i]) == 0) {
			if (!bMarkAsNotSpam)
				return (pSearchFile->GetFileName().GetLength() <= 10) ? SPAM_SMALLFULLNAME_HIT : SPAM_FULLNAME_HIT;

			m_astrKnownSpamNames.RemoveAt(i);
		}
	}

	uint32 nResult = 0;
	if (!m_astrKnownSimilarSpamNames.IsEmpty() && !pSearchFile->GetNameWithoutKeyword().IsEmpty()) {
		const CString &cname(pSearchFile->GetNameWithoutKeyword());
		for (INT_PTR i = m_astrKnownSimilarSpamNames.GetCount(); --i >= 0;) {
			bool bRemove = false;
			if (cname == m_astrKnownSimilarSpamNames[i]) {
				if (!bMarkAsNotSpam)
					return (cname.GetLength() <= 10) ? SPAM_SMALLSIMILARNAME_HIT : SPAM_SIMILARNAME_HIT;

				bRemove = true;
			} else if (cname.GetLength() > 10
				&& (cname.GetLength() == m_astrKnownSimilarSpamNames[i].GetLength()
					|| cname.GetLength() / abs(cname.GetLength() - m_astrKnownSimilarSpamNames[i].GetLength()) >= 3))
			{
				uint32 nStringComp = LevenshteinDistance(cname, m_astrKnownSimilarSpamNames[i]);
				if (nStringComp != 0) {
					nStringComp = cname.GetLength() / nStringComp;
					if (nStringComp >= 3)
						if (bMarkAsNotSpam)
							bRemove = true;
						else if (nStringComp >= 6)
							nResult = SPAM_SIMILARNAME_NEARHIT;
						else
							nResult = max(nResult, SPAM_SIMILARNAME_FARHIT);
				}
			}
			if (bRemove)
				m_astrKnownSimilarSpamNames.RemoveAt(i);
		}
	}
	return nResult;
}

SearchList* CSearchList::GetSearchListForID(uint32 nSearchID)
{
	for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
		SearchListsStruct *list = m_listFileLists.GetNext(pos);
		if (list->m_nSearchID == nSearchID)
			return &list->m_listSearchFiles;
	}
	SearchListsStruct *list = new SearchListsStruct;
	list->m_nSearchID = nSearchID;
	m_listFileLists.AddTail(list);
	return &list->m_listSearchFiles;
}

void CSearchList::SentUDPRequestNotification(uint32 nSearchID, uint32 dwServerIP)
{
	if (nSearchID == m_nCurED2KSearchID)
		m_RequestedUDPAnswersCount[nSearchID] = (uint32)m_aCurED2KSentRequestsIPs.Add(dwServerIP) + 1;
	else
		ASSERT(0);

}


void CSearchList::MarkHashAsBlacklisted(CSKey hash)
{
	if (!m_bSpamFilterLoaded)
		LoadSpamFilter();

	m_mapBlacklistedHashes[hash] = true;
}

bool CSearchList::IsFilenameManualBlacklisted(CSKey hash)
{
	if (!m_bSpamFilterLoaded)
		LoadSpamFilter();

	bool m_bIsBlacklisted = -1;
	if (m_mapBlacklistedHashes.Lookup(hash, m_bIsBlacklisted))
		return m_bIsBlacklisted;

	return false;
}

bool CSearchList::IsFilenameAutoBlacklisted(CString strFilename) {
	CString strFilenameLowerCase = strFilename;
	strFilenameLowerCase.MakeLower();

	for (POSITION pos = thePrefs.blacklist_list.GetHeadPosition(); pos != NULL;) {
		CString strFilenameTemp = strFilenameLowerCase;
		CString m_strBlacklistLine = thePrefs.blacklist_list.GetNext(pos);
		CString m_strBlacklistLineTemp = m_strBlacklistLine;
		bool m_bReplaceNonAlphaNumeric = false;
		bool m_bRemoveNonAlphaNumeric = false;
		bool m_bBlacklistExtension = false;

		if (m_strBlacklistLineTemp.TrimLeft()[0] == _T('#')) // Ignore commented out lines with #
			continue;
		else if (m_strBlacklistLineTemp.TrimLeft()[0] == _T('/')) {
			m_bReplaceNonAlphaNumeric = true;
			m_strBlacklistLineTemp = m_strBlacklistLineTemp.Right(m_strBlacklistLineTemp.GetLength() - 1); // We trimmed none/one/or more spaces and also a / here.
		} else if (m_strBlacklistLineTemp.TrimLeft()[0] == _T('|')) {
			m_bRemoveNonAlphaNumeric = true;
			m_strBlacklistLineTemp = m_strBlacklistLineTemp.Right(m_strBlacklistLineTemp.GetLength() - 1); // We trimmed none/one/or more spaces and also a | here.
		} else if (m_strBlacklistLineTemp.TrimLeft()[0] == _T('*')) {
			m_strBlacklistLineTemp = m_strBlacklistLineTemp.Right(m_strBlacklistLineTemp.GetLength() - 1); // We trimmed none/one/or more spaces and also a * here.
			m_bBlacklistExtension = true;
		} else if (m_strBlacklistLineTemp.TrimLeft()[0] == _T('\\')) {
			m_strBlacklistLineTemp = m_strBlacklistLineTemp.Right(m_strBlacklistLineTemp.GetLength() - 1); // We trimmed none/one/or more spaces and also a \ here.
			// This is a regex line and we'll use m_strBlacklistLineTemp as the regex definition.
			if (m_strBlacklistLineTemp.GetLength() && RegularExpressionMatch(m_strBlacklistLineTemp, strFilename)) { // We used original file name here instead of lower cased one to match regex definition as is.
				if (thePrefs.GetBlacklistLog() && thePrefs.GetVerbose())
					AddDebugLogLine(false, _T("[AUTOMATIC BLACKLIST] File \"%s\" blacklisted by definition: %s"), (LPCTSTR)EscPercent(strFilename), (LPCTSTR)EscPercent(m_strBlacklistLine));
				return true;
			} 
		}
		
		bool m_bIsBlacklisted = false;
		int m_iPosition = 0;
		CString m_strWord = m_strBlacklistLineTemp.Tokenize(L" ", m_iPosition);
		while (m_iPosition != -1) {
			//First we need to check quotation marks. We should have even number of them. Otherwise we need to continue reading m_strBlacklistLineTemp till it reaches to an even number.
			int m_iQuotationMarkCount = CharacterCount(m_strWord, L"\"");
			if (m_iQuotationMarkCount % 2 == 1) {
				m_strWord = m_strWord + L" " + m_strBlacklistLineTemp.Tokenize(L" ", m_iPosition);
				continue;
			}

			if (m_bBlacklistExtension) { // First word is the extension to blacklist. 
				m_strWord.Trim().Remove(L'\"'); // We have none or even number of quotation marks, we can trim spaces and quotation marks now.
				m_bBlacklistExtension = false;
				CString m_strExtension(::PathFindExtension(strFilenameTemp));
				if (m_strExtension.GetLength() > 2)
					m_strExtension = m_strExtension.Right(m_strExtension.GetLength() - 1); //Remove dot

				if (m_strExtension.GetLength() && m_strExtension.CompareNoCase(m_strWord) == 0) // Blacklisted extension matches.
					m_bIsBlacklisted = true; // This can still be updated by next blacklisted words (logical AND) or exclusion words (logical NOT)
				else { // Blacklisted extension doesn't match. So this file isn't blacklisted. We can break loop here.
					m_bIsBlacklisted = false;
					break;
				}
			} else if (m_strWord[0] == '-') { // This is an exclusion word (logical NOT) and it is found in filename. So this file is not blacklisted.
				m_strWord.Trim().Remove(L'\"'); // We have none or even number of quotation marks, we can trim spaces and quotation marks now.
				m_strWord = m_strWord.Right(m_strWord.GetLength() - 1); // Trim -
				if (m_bReplaceNonAlphaNumeric) {
					strFilenameTemp = ReplaceNonAlphaNumeric(strFilenameTemp);
					m_strWord = ReplaceNonAlphaNumeric(m_strWord);
				} else if (m_bRemoveNonAlphaNumeric) {
					strFilenameTemp = RemoveNonAlphaNumeric(strFilenameTemp);
					m_strWord = RemoveNonAlphaNumeric(m_strWord);
				}

				// Always add spaces to the beginning  of the file name. This helps defining filters matching to the beginning while excluding compound words.
				strFilenameTemp = ' ' + strFilenameTemp;

				if (strFilenameTemp.Find(m_strWord) >= 0) {
					m_bIsBlacklisted = false;
					break;
				}
			} else { // This is a blacklisted word (logical AND).
				m_strWord.Trim().Remove(L'\"'); // We have none or even number of quotation marks, we can trim spaces and quotation marks now.
				if (m_bReplaceNonAlphaNumeric) {
					strFilenameTemp = ReplaceNonAlphaNumeric(strFilenameTemp);
					m_strWord = ReplaceNonAlphaNumeric(m_strWord);
				} else if (m_bRemoveNonAlphaNumeric) {
					strFilenameTemp = RemoveNonAlphaNumeric(strFilenameTemp);
					m_strWord = RemoveNonAlphaNumeric(m_strWord);
				}

				// Always add spaces to the beginning and the end of the file name. This helps defining filters matching to the beginning and the end while excluding compound words.
				strFilenameTemp = ' ' + strFilenameTemp;

				if (strFilenameTemp.Find(m_strWord) >= 0) { // Blacklisted word isn't found in filename. 
					m_bIsBlacklisted = true; // This can still be updated by next blacklisted words (logical AND) or exclusion words (logical NOT)
				} else { // Blacklisted word isn't found in filename. So this file isn't blacklisted. We can break loop here.
					m_bIsBlacklisted = false;
					break;
				}
			}
			m_strWord = m_strBlacklistLineTemp.Tokenize(L" ", m_iPosition);
		}

		if (m_bIsBlacklisted) { // If we match this blacklist line with the filename, we can return true here. No need to check other blacklist lines.
			if (thePrefs.GetBlacklistLog() && thePrefs.GetVerbose())
				AddDebugLogLine(false, _T("[AUTOMATIC BLACKLIST] File \"%s\" blacklisted by definition: %s"), (LPCTSTR)EscPercent(strFilename), (LPCTSTR)EscPercent(m_strBlacklistLine));
			return true;
		}
	}

	return false; // None of the blacklist lines has matched with the filename. So this file isn't blacklisted.
}


void CSearchList::RecalculateSpamRatings(uint32 nSearchID, bool bExpectHigher, bool bExpectLower, bool bRecalculateAll)
{
	ASSERT(!(bExpectHigher && bExpectLower));
	ASSERT(m_bSpamFilterLoaded);

	if (!thePrefs.GetBlacklistAutomatic() && !thePrefs.GetBlacklistAutomatic() && !thePrefs.IsSearchSpamFilterEnabled())
		return; // No need to recalculate spam ratings if blacklists or spam filter is not enabled.

	const SearchList *list = GetSearchListForID(nSearchID);
	if (!list || !list->GetCount()) // No list found or list is empty
		return;

	if (bRecalculateAll) {
		// As a first step, reset status of all items to prepare a clean recalculation.
		for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
			CSearchFile* pCurFile = list->GetNext(pos);
			// Skip if known. Because marking as known files has a priority over marking as spam/blacklisted. 
			if (pCurFile->GetKnownType() == CSearchFile::NotDetermined) {
				pCurFile->SetAutomaticBlacklisted(false);
				pCurFile->SetSpamRating(0);
			}
		}

		// When bRecalculateAll is set, we need to check parents before child items as Automatic Blacklist logic needs.
		for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
			CSearchFile* pCurFile = list->GetNext(pos);
			if (!pCurFile->GetListParent() && pCurFile->GetKnownType() == CSearchFile::NotDetermined)
				DoSpamRating(pCurFile, false, Calculate, false, 0);
		}

		// Check child items
		for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
			CSearchFile* pCurFile = list->GetNext(pos);
			if (pCurFile->GetListParent() != NULL && pCurFile->GetKnownType() == CSearchFile::NotDetermined) {
				// If parent is marked as Automatic or Manual Blacklisted, copy this status and dont call DoSpamRating since this will already be listed as spam.
				if (thePrefs.GetBlacklistAutomatic() && pCurFile->GetListParent()->GetAutomaticBlacklisted())
					pCurFile->SetAutomaticBlacklisted(true);
				else if (thePrefs.GetBlacklistManual() && pCurFile->GetListParent()->GetManualBlacklisted())
					pCurFile->SetManualBlacklisted(true);
				else if (thePrefs.IsSearchSpamFilterEnabled() && pCurFile->GetListParent()->IsConsideredSpam(false))
					pCurFile->SetSpamRating(pCurFile->GetListParent()->GetSpamRating());
				else
					DoSpamRating(pCurFile, false, Calculate, false, 0);
			}
		}
	} else {
		for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
			CSearchFile* pCurFile = list->GetNext(pos);
			// Check only parents and only if we expect a status change
			if (((pCurFile->GetListParent() == NULL && !(pCurFile->IsConsideredSpam(false) && bExpectHigher) && !(!pCurFile->IsConsideredSpam(false) && bExpectLower))))
				DoSpamRating(pCurFile, false, Calculate, false, 0);
		}
	}

	theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.ReloadList(true, LSF_SELECTION);
}

void CSearchList::LoadSpamFilter()
{
	m_astrKnownSpamNames.RemoveAll();
	m_astrKnownSimilarSpamNames.RemoveAll();
	m_mapKnownSpamServerIPs.RemoveAll();
	m_mapKnownSpamSourcesIPs.RemoveAll();
	m_mapKnownSpamHashes.RemoveAll();
	m_mapBlacklistedHashes.RemoveAll();
	m_aui64KnownSpamSizes.RemoveAll();

	m_bSpamFilterLoaded = true;

	CSafeBufferedFile file;
	if (!CFileOpenD(file
		, thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + SPAMFILTER_FILENAME
		, CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyWrite
		, _T("Failed to load ") SPAMFILTER_FILENAME))
	{
		return;
	}
	::setvbuf(file.m_pStream, NULL, _IOFBF, 16384);

	try {
		uint8 header = file.ReadUInt8();
		if (header != MET_HEADER_I64TAGS) {
			file.Close();
			DebugLogError(_T("Failed to load searchspam.met, invalid first byte"));
			return;
		}
		unsigned nDbgFileHashPos = 0;

		for (uint32 i = file.ReadUInt32(); i > 0; --i) { //number of records
			CTag tag(file, false);
			switch (tag.GetNameID()) {
			case SP_FILEHASHSPAM:
				ASSERT(tag.IsHash());
				if (tag.IsHash())
					m_mapKnownSpamHashes[CSKey(tag.GetHash())] = true;
				break;
			case SP_FILEHASHNOSPAM:
				ASSERT(tag.IsHash());
				if (tag.IsHash()) {
					m_mapKnownSpamHashes[CSKey(tag.GetHash())] = false;
					++nDbgFileHashPos;
				}
				break;
			case SP_FILEFULLNAME:
				ASSERT(tag.IsStr());
				if (tag.IsStr())
					m_astrKnownSpamNames.Add(tag.GetStr());
				break;
			case SP_FILESIMILARNAME:
				ASSERT(tag.IsStr());
				if (tag.IsStr())
					m_astrKnownSimilarSpamNames.Add(tag.GetStr());
				break;
			case SP_FILESOURCEIP:
				ASSERT(tag.IsInt());
				if (tag.IsInt())
					m_mapKnownSpamSourcesIPs[tag.GetInt()] = true;
				break;
			case SP_FILESERVERIP:
				ASSERT(tag.IsInt());
				if (tag.IsInt())
					m_mapKnownSpamServerIPs[tag.GetInt()] = true;
				break;
			case SP_FILESIZE:
				ASSERT(tag.IsInt64());
				if (tag.IsInt64())
					m_aui64KnownSpamSizes.Add(tag.GetInt64());
				break;
			case SP_UDPSERVERSPAMRATIO:
				ASSERT(tag.IsBlob() && tag.GetBlobSize() == 12);
				if (tag.IsBlob() && tag.GetBlobSize() == 12) {
					const BYTE *pBuffer = tag.GetBlob();
					UDPServerRecord *pRecord = new UDPServerRecord;
					pRecord->m_nResults = PeekUInt32(&pBuffer[4]);
					pRecord->m_nSpamResults = PeekUInt32(&pBuffer[8]);
					m_mUDPServerRecords[PeekUInt32(&pBuffer[0])] = pRecord;
					int nRatio;
					if (pRecord->m_nResults >= pRecord->m_nSpamResults && pRecord->m_nResults > 0)
						nRatio = (pRecord->m_nSpamResults * 100) / pRecord->m_nResults;
					else
						nRatio = 100;
					DEBUG_ONLY(DebugLog(_T("UDP Server Spam Record: IP: %s, Results: %u, SpamResults: %u, Ratio: %u")
						, (LPCTSTR)ipstr(PeekUInt32(&pBuffer[0])), pRecord->m_nResults, pRecord->m_nSpamResults, nRatio));
				}
				break;
			case SP_BLACKLISTED:
				ASSERT(tag.IsHash());
				if (tag.IsHash())
					m_mapBlacklistedHashes[CSKey(tag.GetHash())] = true;
				break;
			default:
				ASSERT(0);
			}
		}
		file.Close();

		DebugLog(_T("Loaded search Spam Filter. Entries - ServerIPs: %u, SourceIPs, %u, hashes: %u, PositiveHashes: %i, FileSizes: %u, FullNames: %u, SimilarNames: %u, Blacklisted hashes: %u")
			, (unsigned)m_mapKnownSpamSourcesIPs.GetCount()
			, (unsigned)m_mapKnownSpamServerIPs.GetCount()
			, (unsigned)m_mapKnownSpamHashes.GetCount() - nDbgFileHashPos
			, nDbgFileHashPos
			, (unsigned)m_aui64KnownSpamSizes.GetCount()
			, (unsigned)m_astrKnownSpamNames.GetCount()
			, (unsigned)m_astrKnownSimilarSpamNames.GetCount()
			, (unsigned)m_mapBlacklistedHashes.GetCount());
	} catch (CFileException *ex) {
		if (ex->m_cause == CFileException::endOfFile)
			DebugLogError(_T("Failed to load searchspam.met, file is corrupt or has a different version"));
		else
			DebugLogError(_T("Failed to load searchspam.met%s"), (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
		ex->Delete();
	}
}

void CSearchList::SaveSpamFilter()
{
	if (ShouldSkipSearchPersistenceForManualLeakDump())
		return;

	if (!m_bSpamFilterLoaded)
		return;

	m_nLastSaved = ::GetTickCount();

	const CString& sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	CSafeBufferedFile file;
	if (!CFileOpenD(file
		, sConfDir + SPAMFILTER_FILENAME_TMP
		, CFile::modeWrite | CFile::modeCreate | CFile::typeBinary | CFile::shareDenyWrite
		, _T("Failed to save ") SPAMFILTER_FILENAME_TMP))
	{
		return;
	}
	::setvbuf(file.m_pStream, NULL, _IOFBF, 16384);
	try {
		uint32 nCount = 0;
		file.WriteUInt8(MET_HEADER_I64TAGS);
		file.WriteUInt32(nCount);

		for (INT_PTR i = 0; i < m_astrKnownSpamNames.GetCount(); ++i) {
			CTag tag(SP_FILEFULLNAME, m_astrKnownSpamNames[i]);
			tag.WriteNewEd2kTag(file, UTF8strOptBOM);
			++nCount;
		}

		for (INT_PTR i = 0; i < m_astrKnownSimilarSpamNames.GetCount(); ++i) {
			CTag tag(SP_FILESIMILARNAME, m_astrKnownSimilarSpamNames[i]);
			tag.WriteNewEd2kTag(file, UTF8strOptBOM);
			++nCount;
		}

		for (INT_PTR i = 0; i < m_aui64KnownSpamSizes.GetCount(); ++i) {
			CTag tag(SP_FILESIZE, m_aui64KnownSpamSizes[i], true);
			tag.WriteNewEd2kTag(file);
			++nCount;
		}

		for (const CMap<CSKey, const CSKey&, bool, bool>::CPair *pair = m_mapKnownSpamHashes.PGetFirstAssoc(); pair != NULL; pair = m_mapKnownSpamHashes.PGetNextAssoc(pair)) {
			CTag tag((pair->value ? SP_FILEHASHSPAM : SP_FILEHASHNOSPAM), (BYTE*)pair->key.m_key);
			tag.WriteNewEd2kTag(file);
			++nCount;
		}

		for (const CSpammerIPMap::CPair *pair = m_mapKnownSpamServerIPs.PGetFirstAssoc(); pair != NULL; pair = m_mapKnownSpamServerIPs.PGetNextAssoc(pair)) {
			CTag tag(SP_FILESERVERIP, pair->key); //IP
			tag.WriteNewEd2kTag(file);
			++nCount;
		}

		for (const CSpammerIPMap::CPair *pair = m_mapKnownSpamSourcesIPs.PGetFirstAssoc(); pair != NULL; pair = m_mapKnownSpamSourcesIPs.PGetNextAssoc(pair)) {
			CTag tag(SP_FILESOURCEIP, pair->key); //IP
			tag.WriteNewEd2kTag(file);
			++nCount;
		}

		for (const CUDPServerRecordMap::CPair *pair = m_mUDPServerRecords.PGetFirstAssoc(); pair != NULL; pair = m_mUDPServerRecords.PGetNextAssoc(pair)) {
			const uint32 buf[3] = { pair->key, pair->value->m_nResults, pair->value->m_nSpamResults };
			CTag tag(SP_UDPSERVERSPAMRATIO, sizeof(buf), (const BYTE*)buf);
			tag.WriteNewEd2kTag(file);
			++nCount;
		}

		for (const CMap<CSKey, const CSKey&, bool, bool>::CPair* pair = m_mapBlacklistedHashes.PGetFirstAssoc(); pair != NULL; pair = m_mapBlacklistedHashes.PGetNextAssoc(pair)) {
			CTag tag(SP_BLACKLISTED, (BYTE*)pair->key.m_key);
			tag.WriteNewEd2kTag(file);
			++nCount;
		}

		file.Seek(1ull, CFile::begin);
		file.WriteUInt32(nCount);
		file.Close();
		MoveFileEx(sConfDir + SPAMFILTER_FILENAME_TMP, sConfDir + SPAMFILTER_FILENAME, MOVEFILE_REPLACE_EXISTING);
		DebugLog(_T("Stored searchspam.met, wrote %u records"), nCount);
	} catch (CFileException *ex) {
		DebugLogError(_T("Failed to save searchspam.met%s"), (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
		ex->Delete();
	}
}

void CSearchList::StoreSearches()
{
	if (ShouldSkipSearchPersistenceForManualLeakDump())
		return;

	m_nLastSaved = ::GetTickCount();

	// store open searches on shutdown to restore them on the next startup
	const CString& sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	CSafeBufferedFile file;
	if (!CFileOpenD(file
		, sConfDir + STOREDSEARCHES_FILENAME_TMP
		, CFile::modeWrite | CFile::modeCreate | CFile::typeBinary | CFile::shareDenyWrite
		, _T("Failed to save ") STOREDSEARCHES_FILENAME_TMP))
	{
		return;
	}

	::setvbuf(file.m_pStream, NULL, _IOFBF, 16384);
	try {
		file.WriteUInt8(MET_HEADER_I64TAGS);
		file.WriteUInt8(STOREDSEARCHES_VERSION);
		// count how many (if any) open searches we have which are GUI related
		uint16 nCount = 0;
		for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
			const SearchListsStruct *pSl = m_listFileLists.GetNext(pos);
			nCount += static_cast<uint16>(theApp.emuledlg->searchwnd->GetSearchParamsBySearchID(pSl->m_nSearchID) != NULL);
		}
		file.WriteUInt16(nCount);
		if (nCount > 0)
			for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
				const SearchListsStruct *pSl = m_listFileLists.GetNext(pos);
				const SSearchParams *pParams = theApp.emuledlg->searchwnd->GetSearchParamsBySearchID(pSl->m_nSearchID);
				if (pParams != NULL) {
					pParams->StorePartially(file);
					uint32 uCount = 0;
					for (POSITION pos2 = pSl->m_listSearchFiles.GetHeadPosition(); pos2 != NULL;)
						uCount += static_cast<uint32>(!pSl->m_listSearchFiles.GetNext(pos2)->m_bNowrite);

					file.WriteUInt32(uCount);
					for (POSITION pos2 = pSl->m_listSearchFiles.GetHeadPosition(); pos2 != NULL;) {
						CSearchFile *sf = pSl->m_listSearchFiles.GetNext(pos2);
						if (!sf->m_bNowrite)
							sf->StoreToFile(file);
					}
				}
			}

		file.Close();
		MoveFileEx(sConfDir + STOREDSEARCHES_FILENAME_TMP, sConfDir + STOREDSEARCHES_FILENAME, MOVEFILE_REPLACE_EXISTING);
		DebugLog(_T("Stored %u open search(es) for restoring on next start"), nCount);
	} catch (CFileException *ex) {
		DebugLogError(_T("Failed to save %s%s"), STOREDSEARCHES_FILENAME, (LPCTSTR)EscPercent(CExceptionStrDash(*ex)));
		ex->Delete();
	}
}

void CSearchList::LoadSearches()
{
	ASSERT(m_listFileLists.IsEmpty());
	CSafeBufferedFile file;
	if (!CFileOpenD(file
		, thePrefs.GetMuleDirectory(EMULE_CONFIGDIR) + STOREDSEARCHES_FILENAME
		, CFile::modeRead | CFile::osSequentialScan | CFile::typeBinary | CFile::shareDenyWrite
		, _T("Failed to load ") STOREDSEARCHES_FILENAME))
	{
		return;
	}
	::setvbuf(file.m_pStream, NULL, _IOFBF, 16384);

	try {
		uint8 header = file.ReadUInt8();
		if (header != MET_HEADER_I64TAGS) {
			file.Close();
			DebugLogError(_T("Failed to load %s, invalid first byte"), STOREDSEARCHES_FILENAME);
			return;
		}
		uint8 byVersion = file.ReadUInt8();
		if (byVersion < 101 || byVersion > STOREDSEARCHES_VERSION) {
			file.Close();
			DebugLogError(_T("Failed to load %s, incompatible file version"), STOREDSEARCHES_FILENAME);
			return;
		}

		uint32 nID = (uint32)-1;
		for (unsigned nCount = (unsigned)file.ReadUInt16(); nCount > 0; --nCount) {
			SSearchParams *pParams = new SSearchParams(file, byVersion);
			pParams->dwSearchID = ++nID; //renumber

			// create a new tab
			const CString &strResultType(pParams->strFileType);
			NewSearch(NULL, (strResultType == _T(ED2KFTSTR_PROGRAM) ? CString() : strResultType), pParams);

			bool bDeleteParams = !theApp.emuledlg->searchwnd->CreateNewTab(pParams, false, nCount == 1); //	We'll show reasults  only on the last tab
			if (!bDeleteParams) {
				m_foundFilesCount[pParams->dwSearchID] = 0;
				m_foundSourcesCount[pParams->dwSearchID] = 0;
			} else
				ASSERT(0); //failed to create tab

			// fill the list using stored data
			for (uint32 nFileCount = file.ReadUInt32(); nFileCount > 0; --nFileCount) {
				CSearchFile *toadd = new CSearchFile(file, true, pParams->dwSearchID, 0, 0, NULL, pParams->eType == SearchTypeKademlia);
				AddToList(toadd, pParams->bClientSharedFiles, 0, true);
			}
			if (outputwnd) {
				if (nCount == 1) // We'll fill only the last tab
					outputwnd->ReloadList(false, LSF_SELECTION);
				else
					outputwnd->UpdateTabHeader(pParams->dwSearchID, EMPTY, false);
			}

			if (bDeleteParams)
				delete pParams;
		}
		file.Close();
		// adjust the starting values for search IDs to avoid reused IDs in loaded searches
		Kademlia::CSearchManager::SetNextSearchID(++nID);
		theApp.emuledlg->searchwnd->SetNextSearchID(0x80000000u + nID);
	} catch (CFileException *ex) {
		DebugLogError(_T("Failed to load %s%s"), STOREDSEARCHES_FILENAME
			, (ex->m_cause == CFileException::endOfFile) ? _T(" - file is corrupt or has a different version") : (LPCTSTR)CExceptionStrDash(*ex));
		ex->Delete();
	}
}

void CSearchList::Process()
{
	if (::GetTickCount() >= m_nLastSaved + MIN2MS(12)) {
		if (thePrefs.IsStoringSearchesEnabled())
			theApp.searchlist->StoreSearches();

		SaveSpamFilter();
	}
}

void CSearchList::ReorderSearches() {
	if (m_listFileLists.IsEmpty())
		return;

	CTypedPtrList<CPtrList, SearchListsStruct*> m_listFileListsTemp;
	CClosableTabCtrl& searchselect = theApp.emuledlg->searchwnd->GetSearchSelector();
	for (int i = 0; i < searchselect.GetItemCount();) {
		TCITEM item;
		item.mask = TCIF_PARAM;
		if (searchselect.GetItem(i, &item) && item.lParam != NULL) {
			for (POSITION pos = m_listFileLists.GetHeadPosition(); pos != NULL;) {
				SearchListsStruct* list = m_listFileLists.GetNext(pos);
				if (reinterpret_cast<SSearchParams*>(item.lParam)->dwSearchID == list->m_nSearchID)
					m_listFileListsTemp.AddTail(list);
			}
		} 
		i++;
	}
	
	m_listFileLists.RemoveAll();
	for (POSITION pos = m_listFileListsTemp.GetHeadPosition(); pos != NULL;)
		m_listFileLists.AddTail(m_listFileListsTemp.GetNext(pos));
}

uint32 CSearchList::GetParentItemCount(uint32 nResultsID)
{
	const SearchList* list = GetSearchListForID(nResultsID);
	if (!list)
		return 0;

	uint32 nParents = 0;
	POSITION pos = list->GetHeadPosition();
	while (pos) {
		POSITION posCur = pos;
		const CSearchFile* cur_result = list->GetNext(pos);
		if (cur_result && !cur_result->GetListParent())
			nParents++;
	}

	return nParents;
}

void CSearchList::MarkSearchAsMerged(uint32 nSearchID)
{
	m_mergedSearchHistory[nSearchID] = true;
}

void CSearchList::SetSearchItemKnownType(CSearchFile* src)
{
	if (!src)
		return; // No file to process

	const CKnownFile* pFile = theApp.downloadqueue->GetFileByID(src->GetFileHash());
	if (pFile) {
		if (pFile->IsPartFile())
			src->SetKnownType(CSearchFile::Downloading);
		else
			src->SetKnownType(CSearchFile::Shared);
	} else if (theApp.sharedfiles->GetFileByID(src->GetFileHash()))
		src->SetKnownType(CSearchFile::Shared);
	else if (theApp.knownfiles->FindKnownFileByID(src->GetFileHash()))
		src->SetKnownType(CSearchFile::Downloaded);
	else if (theApp.knownfiles->IsCancelledFileByID(src->GetFileHash()))
		src->SetKnownType(CSearchFile::Cancelled);

	CSearchFile* pParent = NULL;
	if (src->GetListParent())
		pParent = src->GetListParent();
	else if (src->GetListChildCount() > 0)
		pParent = src;

	// Mark parent and children as known file if this is a known file
	if (pParent) {
		pParent->SetKnownType(src->GetKnownType()); // Mark parent same as calculated src
		const SearchList* list = GetSearchListForID(pParent->GetSearchID());
		for (POSITION pos = list->GetHeadPosition(); pos != NULL;) {
			CSearchFile* pCurFile = list->GetNext(pos);
			if (pCurFile->GetListParent() == pParent)
				pCurFile->SetKnownType(pParent->GetKnownType()); // Mark children same as parent
		}
	}
}
