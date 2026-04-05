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
#pragma once
#include "KnownFile.h"
#include "SearchFile.h"
#include "QArray.h"
#include "Mapkey.h"
#include "SearchParams.h"
#include <set>

enum ESearchType : uint8;

typedef struct
{
	CString	m_strFileName;
	CString	m_strFileType;
	CString	m_strFileHash;
	CString	m_strIndex;
	uint64	m_uFileSize;
	uint32	m_uSourceCount;
	uint32	m_dwCompleteSourceCount;
} SearchFileStruct;

typedef CTypedPtrList<CPtrList, CSearchFile*> SearchList;

typedef struct
{
	uint32 m_nSearchID;
	SearchList m_listSearchFiles;
} SearchListsStruct;

typedef struct
{
	uint32	m_nResults;
	uint32	m_nSpamResults;
} UDPServerRecord;



class CFileDataIO;
class CAbstractFile;
struct SSearchTerm;

class CSearchList
{
	friend class CSearchListCtrl;

public:
	CSearchList();
	~CSearchList();

	void	Clear();
	void	NewSearch(CSearchListCtrl *pWnd, const CString &strResultFileType, SSearchParams *pParams);
	UINT	ProcessSearchAnswer(const uchar *in_packet, uint32 size, CUpDownClient &sender, bool *pbMoreResultsAvailable, LPCTSTR pszDirectory = NULL);
	UINT	ProcessSearchAnswer(const uchar *in_packet, uint32 size, bool bOptUTF8, uint32 nServerIP, uint16 nServerPort, bool *pbMoreResultsAvailable);
	UINT	ProcessUDPSearchAnswer(CFileDataIO &packet, bool bOptUTF8, uint32 nServerIP, uint16 nServerPort);
	UINT	GetED2KResultCount() const;
	UINT	GetResultCount(uint32 nSearchID) const;
	void	AddResultCount(uint32 nSearchID, const uchar *hash, UINT nCount, bool bSpam);

	void	SetOutputWnd(CSearchListCtrl *in_wnd)		{ outputwnd = in_wnd; }
	void	RemoveResults(uint32 nSearchID);
	void	RemoveResult(CSearchFile* todel);
	void	GetWebList(CQArray<SearchFileStruct, SearchFileStruct> *SearchFileArray, int iSortBy) const;

	void	AddFileToDownloadByHash(const uchar *hash)	{ AddFileToDownloadByHash(hash, 0); }
	void	AddFileToDownloadByHash(const uchar *hash, int cat);
	bool	AddToList(CSearchFile* toadd, bool bClientRespons, uint32 dwFromUDPServerIP, bool bDoSpamRating);
	CSearchFile* GetSearchFileByHash(const uchar *hash) const;
	void	KademliaSearchKeyword(uint32 nSearchID, const Kademlia::CUInt128 *pFileID, LPCTSTR name, uint64 size, LPCTSTR type, UINT uKadPublishInfo, CArray<CAICHHash> &raAICHHashes, CArray<uint8, uint8> &raAICHHashPopularity, SSearchTerm *pQueriedSearchTerm, UINT numProperties, ...);
	bool	AddNotes(const Kademlia::CEntry &cEntry, const uchar *hash);
	void	SetNotesSearchStatus(const uchar *pFileHash, bool bSearchRunning);
	void	SentUDPRequestNotification(uint32 nSearchID, uint32 dwServerIP);

	void	StoreSearches();
	void	LoadSearches();

	enum EActionType
	{
		Calculate,
		MarkAsSpam,
		MarkAsNotSpam,
		MarkAsBlacklisted,
		MarkAsNotBlacklisted,
	};

	bool	DoSpamRating(CSearchFile* pSearchFile, bool bIsClientFile, uint8 uActionType, bool bUpdate, uint32 dwFromUDPServerIP);
	void	MarkHashAsBlacklisted(CSKey hash);
	bool	IsFilenameManualBlacklisted(CSKey hash);
	static bool	IsFilenameAutoBlacklisted(CString strFilename);
	void	MarkFileAsNotSpam(CSearchFile *pSpamFile) {	DoSpamRating(pSpamFile, false, MarkAsSpam, true, 0); }
	void	RecalculateSpamRatings(uint32 nSearchID, bool bExpectHigher, bool bExpectLower, bool bRecalculateAll);

	void	SetSearchItemKnownType(CSearchFile* src);

	void	SaveSpamFilter();

	UINT	GetFoundFiles(uint32 nSearchID) const
	{
		UINT returnVal;
		return m_foundFilesCount.Lookup(nSearchID, returnVal) ? returnVal : 0;
	}

	UINT	GetOriginalFoundFiles(uint32 nSearchID) const
	{
		UINT returnVal;
		return m_originalFoundFilesCount.Lookup(nSearchID, returnVal) ? returnVal : 0;
	}

	bool	HasMergedSearchHistory(uint32 nSearchID) const
	{
		bool bMerged = false;
		return m_mergedSearchHistory.Lookup(nSearchID, bMerged) && bMerged;
	}

	void	MarkSearchAsMerged(uint32 nSearchID);

	uint32 GetParentItemCount(uint32 nResultsID);

	void	Process();
	
	void	ReorderSearches();

	bool m_bKadReloadWaiting;
	DWORD m_dwKadLastReloadTick;

	SearchList* GetSearchListForID(uint32 nSearchID); // Moved to public
protected:
	uint32	GetSpamFilenameRatings(const CSearchFile *pSearchFile, bool bMarkAsNotSpam);
	void	LoadSpamFilter();

private:
	CTypedPtrList<CPtrList, SearchListsStruct*> m_listFileLists;
	CMap<uint32, uint32, UINT, UINT> m_foundFilesCount;
	CMap<uint32, uint32, UINT, UINT> m_originalFoundFilesCount;
	CMap<uint32, uint32, UINT, UINT> m_foundSourcesCount;
	CMap<uint32, uint32, UINT, UINT> m_ReceivedUDPAnswersCount;
	CMap<uint32, uint32, UINT, UINT> m_RequestedUDPAnswersCount;
	CMap<uint32, uint32, bool, bool> m_mergedSearchHistory;
	CSearchListCtrl *outputwnd;
	CString	m_strResultFileType;


	// spam filter
	typedef CMap<uint32, uint32, bool, bool> CSpammerIPMap;
	typedef CMap<uint32, uint32, UDPServerRecord*, UDPServerRecord*> CUDPServerRecordMap;
	CMap<CSKey, const CSKey&, bool, bool> m_mapKnownSpamHashes;
	CSpammerIPMap			m_mapKnownSpamServerIPs;
	CSpammerIPMap			m_mapKnownSpamSourcesIPs;
	CUDPServerRecordMap		m_mUDPServerRecords;
	CStringArray							m_astrSpamCheckCurSearchExp;
	CStringArray							m_astrKnownSpamNames;
	CStringArray							m_astrKnownSimilarSpamNames;
	CArray<uint64>							m_aui64KnownSpamSizes;
	CArray<uint32, uint32>					m_aCurED2KSentRequestsIPs;
	CArray<uint32, uint32>					m_aCurED2KSentReceivedIPs;
	uint32	m_nCurED2KSearchID;
	bool									m_bSpamFilterLoaded;
	CMap<CSKey, const CSKey&, bool, bool>	m_mapBlacklistedHashes;
	uint32	m_nLastSaved;
};
