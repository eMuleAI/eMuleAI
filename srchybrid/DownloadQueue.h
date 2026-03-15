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
#include "otherfunctions.h"
#include "eMuleAI/Address.h"
#include "ClientStateDefs.h"

class CSafeMemFile;
class CSearchFile;
class CUpDownClient;
class CServer;
class CPartFile;
class CSharedFileList;
class CKnownFile;
class CED2KFileLink;
struct SUnresolvedHostname;

namespace Kademlia
{
	class CUInt128;
};

class CSourceHostnameResolveWnd : public CWnd
{
	// Construction
public:
	CSourceHostnameResolveWnd();
	virtual	~CSourceHostnameResolveWnd();

	void AddToResolve(const uchar *fileid, LPCSTR pszHostname, uint16 port);

protected:
	DECLARE_MESSAGE_MAP()
	afx_msg LRESULT OnHostnameResolved(WPARAM, LPARAM lParam);

private:
	struct Hostname_Entry
	{
		uchar fileid[MDX_DIGEST_SIZE];
		CStringA strHostname;
		uint16 port;
		CString strURL;
	};
	CTypedPtrList<CPtrList, Hostname_Entry*> m_toresolve;
	char m_aucHostnameBuffer[MAXGETHOSTSTRUCT];
};


class CDownloadQueue
{
	friend class CAddFileThread;
	friend class CServerSocket;

public:
	CDownloadQueue();
	~CDownloadQueue();

	void	Process();
	void	Init();

	// add/remove entries
	void	AddPartFilesToShare();
	void	AddDownload(CPartFile *newfile, bool paused);
	void	AddSearchToDownload(CSearchFile *toadd, uint8 paused = 2, int cat = 0, bool bBypassDownloadChecker = false);
	void	AddSearchToDownload(const CString &link, uint8 paused = 2, int cat = 0);
	void	AddFileLinkToDownload(const CED2KFileLink &Link, int cat = 0);
	void	RemoveFile(CPartFile *toremove);
	void	DeleteAll();

	INT_PTR	GetFileCount() const							{ return filelist.GetCount(); }

	UINT	GetDownloadingFileCount() const;
	UINT	GetPausedFileCount() const;

	bool	IsFileExisting(const uchar *fileid, bool bLogWarnings = true) const;
	bool	IsPartFile(const CKnownFile *file) const;

	CPartFile* GetFileByID(const uchar *filehash) const;
	CPartFile* GetFileNext(POSITION& pos) const; //trivial iterator
	CPartFile* GetFileByKadFileSearchID(uint32 id) const;

	void	StartNextFileIfPrefs(int cat);
	void	StartNextFile(int cat = -1, bool force = false);

	void	RefilterAllComments();

	// sources
	CUpDownClient* GetDownloadClientByIP(const CAddress& IP);
	CUpDownClient* GetDownloadClientByIP_UDP(const CAddress& IP, uint16 nUDPPort, bool bIgnorePortOnUniqueIP, bool* pbMultipleIPs = NULL);
	bool	IsInList(const CUpDownClient* client) const;

	bool	CheckAndAddSource(CPartFile *sender, CUpDownClient *source, ESourceFrom eSourceFrom, bool bSourceFromAuthoritative = true, CUpDownClient** ppResolvedSource = NULL);
	bool	CheckAndAddKnownSource(CPartFile *sender, CUpDownClient *source, bool bIgnoreGlobDeadList = false);
	bool	RemoveSource(CUpDownClient *toremove, bool bDoStatsUpdate = true);

	// statistics
	typedef struct
	{
		unsigned a[23];
	} SDownloadStats;
	void	GetDownloadSourcesStats(SDownloadStats &results);
	int		GetDownloadFilesStats(uint64 &rui64TotalFileSize, uint64 &rui64TotalLeftToTransfer, uint64 &rui64TotalAdditionalNeededSpace);
	uint32	GetDatarate() const								{ return m_datarate; }

	void	AddUDPFileReasks()								{ ++m_nUDPFileReasks; }
	uint32	GetUDPFileReasks() const						{ return m_nUDPFileReasks; }
	void	AddFailedUDPFileReasks()						{ ++m_nFailedUDPFileReasks; }
	uint32	GetFailedUDPFileReasks() const					{ return m_nFailedUDPFileReasks; }

	// categories
	void	ResetCatParts(UINT cat);
	void	SetCatPrio(UINT cat, uint8 newprio);
	void	RemoveAutoPrioInCat(UINT cat, uint8 newprio); // ZZ:DownloadManager
	void	SetCatStatus(UINT cat, int newstatus);
	void	MoveCat(UINT from, UINT to);
	static void	SetAutoCat(CPartFile *newfile);

	// searching on local server
	void	SendLocalSrcRequest(CPartFile *sender);
	void	RemoveLocalServerRequest(CPartFile *pFile);
	void	ResetLocalServerRequests();

	// searching in Kad
	void	SetLastKademliaFileRequest()					{ m_lastkademliafilerequest = ::GetTickCount(); }
	bool	DoKademliaFileRequest() const;
	void	KademliaSearchFile(uint32 nSearchID, const Kademlia::CUInt128* pcontactID, const Kademlia::CUInt128* pbuddyID, uint8 type, uint32 ip, uint16 tcp, uint16 udp, uint32 dwServingBuddyIP, uint16 dwServingBuddyPort, uint8 byCryptOptions, const Kademlia::CUInt128* pIPv6, const Kademlia::CUInt128* pBuddyIPv6);

	// searching on global servers
	void	StopUDPRequests();

	// check disk space
	void	SortByPriority();
	void	CheckDiskspace(bool bNotEnoughSpaceLeft = false);
	void	CheckDiskspaceTimed();

	void	ExportPartMetFilesOverview();
	void	OnConnectionState(bool bConnected);

	void	AddToResolved(CPartFile *pFile, SUnresolvedHostname *pUH);

	CString	GetOptimalTempDir(UINT nCat, EMFileSize nFileSize);

	CServer	*cur_udpserver;

	uint32	m_TCPFileReask;
	uint32	m_FailedTCPFileReask;
	void	IncrementTCPFileReask() { m_TCPFileReask++; }
	uint32	GetTCPFileReasks() const { return m_TCPFileReask; }
	void	IncrementFailedTCPFileReask() { m_FailedTCPFileReask++; }
	uint32	GetFailedTCPFileReasks() const { return m_FailedTCPFileReask; }
protected:
	bool	SendNextUDPPacket();
	void	ProcessLocalRequests();
	bool	IsMaxFilesPerUDPServerPacketReached(uint32 nFiles, uint32 nIncludedLargeFiles) const;
	bool	SendGlobGetSourcesUDPPacket(CSafeMemFile &data, bool bExt2Packet, uint32 nFiles, uint32 nIncludedLargeFiles);

private:

public:
	CTypedPtrList<CPtrList, CPartFile*> filelist;
private:
	CTypedPtrList<CPtrList, CPartFile*> m_localServerReqQueue;

	// By BadWolf - Accurate Speed Measurement
	typedef struct
	{
		uint32	datalen;
		DWORD	timestamp; //tick count
	} TransferredData;
	CList<TransferredData> average_dr_list;
	// END By BadWolf - Accurate Speed Measurement

	CSourceHostnameResolveWnd m_srcwnd;
	uint64	m_datarateMS;
	CPartFile *m_lastfile;
	DWORD	m_dwLastA4AFtime; // ZZ:DownloadManager
	DWORD	m_lastudpsearchtime;
	DWORD	m_lastudpstattime;
	DWORD	m_lastkademliafilerequest;
	DWORD	m_dwNextTCPSrcReq;
	UINT	m_udcounter;
	UINT	m_cRequestsSentToServer;
	int		m_iSearchedServers;

	uint32	m_nUDPFileReasks;
	uint32	m_nFailedUDPFileReasks;
	uint32	m_datarate;
};
