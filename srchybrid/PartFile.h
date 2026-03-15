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
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#pragma once
#include "KnownFile.h"
#include "DeadSourceList.h"
#include "CorruptionBlackBox.h"
#include "eMuleAI/SourceSaver.h"
#include "ClientStateDefs.h"
#include "Packets.h"

enum EPartFileStatus
{
	PS_READY			= 0,
	PS_EMPTY			= 1,
	PS_WAITINGFORHASH	= 2,
	PS_HASHING			= 3,
	PS_ERROR			= 4,
	PS_INSUFFICIENT		= 5,
	PS_UNKNOWN			= 6,
	PS_PAUSED			= 7,
	PS_COMPLETING		= 8,
	PS_COMPLETE			= 9
};

#define PR_VERYLOW			4 // I Had to change this because it didn't save negative number correctly. Had to modify the sort function for this change.
#define PR_LOW				0 //*
#define PR_NORMAL			1 // Don't change this - needed for edonkey clients and server!
#define PR_HIGH				2 //*
#define PR_VERYHIGH			3
#define PR_AUTO				5 //UAP Hunter

//#define BUFFER_SIZE_LIMIT 500000 // Max bytes before forcing a flush

#define	PARTMET_BAK_EXT	_T(".bak")
#define	PARTMET_TMP_EXT	_T(".backup")

#define STATES_COUNT		17

enum EPartFileFormat : uint8
{
	PMT_UNKNOWN			= 0,
	PMT_DEFAULTOLD,
	PMT_SPLITTED,
	PMT_NEWOLD,
	PMT_SHAREAZA,
	PMT_BADFORMAT
};

enum EPartFileLoadResult
{
	PLR_FAILED_METFILE_NOACCESS	= -2,
	PLR_FAILED_METFILE_CORRUPT	= -1,
	PLR_FAILED_OTHER			= 0,
	PLR_LOADSUCCESS				= 1,
	PLR_CHECKSUCCESS			= 2
};

#define	FILE_COMPLETION_THREAD_FAILED	0x0000
#define	FILE_COMPLETION_THREAD_SUCCESS	0x0001
#define	FILE_COMPLETION_THREAD_RENAMED	0x0002

enum EPartFileOp
{
	PFOP_NONE = 0,
	PFOP_HASHING,
	PFOP_COPYING,
	PFOP_UNCOMPRESSING,
	PFOP_IMPORTPARTS
};

class CSearchFile;
class CUpDownClient;
enum EDownloadState : uint8;
class CSafeMemFile;
class CED2KFileLink;

#pragma pack(push, 1)
struct Requested_Block_Struct
{
	uint64	StartOffset;
	uint64	EndOffset;
	uchar	FileID[MDX_DIGEST_SIZE];
	uint64  transferred; // Barry - this counts completed bytes for partfile 'progress' display
};
#pragma pack(pop)

struct Gap_Struct
{
	uint64 start;
	uint64 end;
};

//part file buffer write status (in PartFileBufferedData.flushed)
#define PB_READY 0
#define PB_PENDING 1
#define PB_ERROR 2
#define PB_WRITTEN 3

struct PartFileBufferedData
{
	uint64 start;					// Barry - This is the start offset of the data
	uint64 end;						// Barry - This is the end offset of the data
	BYTE *data;						// Barry - This is the data to be written
	Requested_Block_Struct *block;	// Barry - This is the requested block that this data relates to
	DWORD dwError;					// returned from the writing thread
	byte flushed;					// 0 - ready 1 - sent to writing thread 2 - error 3 - written
};

struct PartfileSourceCache
{
	uint16			nPort;
	uint32			dwID;
	CAddress		IPv6;
	uint32			dwServerIP;
	uint16			nServerPort;
	uchar			achUserHash[16];
	bool			withuserhash;
	bool			ed2kIDFlag;
	uint8			byCryptOptions;
	ESourceFrom		sourcefrom;
	uint32			expires;
};
#define	SOURCECACHEPROCESSLOOP	MIN2MS(1)	//every one minute
#define SOURCECACHELIFETIME		MIN2MS(30)	//expires after 30 minutes

typedef CTypedPtrList<CPtrList, CUpDownClient*> CUpDownClientPtrList;

class CPartFile : public CKnownFile
{
	DECLARE_DYNAMIC(CPartFile)
	friend class CPartFileConvert;

public:
	explicit CPartFile(UINT cat = 0);
	explicit CPartFile(CSearchFile *searchresult, UINT cat = 0);
	explicit CPartFile(const CString &edonkeylink, UINT cat = 0);
	explicit CPartFile(const CED2KFileLink &fileLink, UINT cat = 0);
	virtual	~CPartFile();
	static void ReleaseBarShaderBuffers() noexcept;

	struct PartMetFileDataStruct
	{
		CString m_fullname;
		CString m_partmetfilename;
		CString	m_strFileName;
		uint8	m_uPartFileVersion;
		uchar	m_abyMD4Hash[MDX_DIGEST_SIZE];
		CArray<CTag*, CTag*> m_taglist;
		CArray<uchar*, uchar*>	m_aMD4HashSet;
	};

	PartMetFileDataStruct* PartMetFileData;

	bool	IsPartFile() const							{ return (status != PS_COMPLETE); }

	// eD2K filename
	virtual void SetFileName(LPCTSTR pszFileName, bool bReplaceInvalidFileSystemChars = false, bool bRemoveControlChars = false); // 'bReplaceInvalidFileSystemChars' is set to 'false' for backward compatibility!

	// part.met filename (without path!)
	const CString& GetPartMetFileName() const			{ return m_partmetfilename; }

	// full path to part.met file or completed file
	const CString& GetFullName() const					{ return m_fullname; }
	void	SetFullName(const CString &name)			{ m_fullname = name; }
	CString	GetTmpPath() const;

	// local file system related properties
	bool	IsNormalFile() const						{ return (m_dwFileAttributes & (FILE_ATTRIBUTE_COMPRESSED | FILE_ATTRIBUTE_SPARSE_FILE)) == 0; }
	EMFileSize	GetRealFileSize() const					{ return GetDiskFileSize(GetFilePath()); }
	void	GetLeftToTransferAndAdditionalNeededSpace(uint64 &rui64LeftToTransfer, uint64 &rui64AdditionalNeededSpace) const;
	uint64	GetNeededSpace() const;
	virtual void SetFileSize(EMFileSize nFileSize);

	// last file modification time (NT's version of UTC), to be used for stats only!
	CTime	GetCFileDate() const						{ return CTime(m_tLastModified); }
	time_t	GetFileDate() const { return m_tLastModified; }
	time_t	GetLastReceptionDate() const				{ return (m_uTransferred > 0 && m_tLastModified > 0) ? m_tLastModified : (time_t)-1; }

	// file creation time (NT's version of UTC), to be used for stats only!
	CTime	GetCrCFileDate() const						{ return CTime(m_tCreated); }
	time_t	GetCrFileDate() const						{ return m_tCreated; }

	void	InitializeFromLink(const CED2KFileLink &fileLink, UINT cat = 0);
	uint32	Process(uint32 reducedownload, UINT icounter);
	EPartFileLoadResult	LoadPartFile(LPCTSTR in_directory, LPCTSTR in_filename, EPartFileFormat *pOutCheckFileFormat = NULL); //filename = *.part.met
	EPartFileLoadResult	ImportShareazaTempfile(LPCTSTR in_directory, LPCTSTR in_filename, EPartFileFormat *pOutCheckFileFormat = NULL);

	bool	SavePartFile(bool bDontOverrideBak = false);
	bool	SavePartFileThreaded(bool bDontOverrideBak = false);
	void	PartFileHashFinished(CKnownFile *result);
	bool	HashSinglePart(UINT partnumber, bool *pbAICHReportedOK = NULL); // true = OK, false = corrupted

	void	AddGap(uint64 start, uint64 end);
	void	FillGap(uint64 start, uint64 end);
	void	DrawStatusBar(CDC *dc, const CRect &rect, bool bFlat);
	virtual void	DrawShareStatusBar(CDC *dc, LPCRECT rect, bool onlygreyrect, bool bFlat) const;
	bool	IsComplete(uint64 start, uint64 end) const;
	bool	IsCompleteSafe(uint64 start, uint64 end) const;
	bool	IsComplete(UINT uPart) const;
	bool	IsCompleteBD(uint64 start, uint64 end) const;
	bool	IsCompleteBDSafe(uint64 start, uint64 end) const;
	bool	IsCompleteBD(UINT uPart) const;
	bool	IsPureGap(uint64 start, uint64 end) const;
	bool	IsAlreadyRequested(uint64 start, uint64 end, bool bCheckBuffers = false) const;
	bool	ShrinkToAvoidAlreadyRequested(uint64 &start, uint64 &end) const;
	bool	IsCorruptedPart(UINT partnumber) const		{ return corrupted_list.GetCount() == 0 ? false : corrupted_list.Find((uint16)partnumber); }
	uint64	GetTotalGapSizeInRange(uint64 uRangeStart, uint64 uRangeEnd) const;
	uint64	GetTotalGapSizeInPart(UINT uPart) const;
	void	UpdateCompletedInfos();
	void	UpdateCompletedInfos(uint64 uTotalGaps);
	virtual void	UpdatePartsInfo();

	bool	GetNextRequestedBlock(CUpDownClient *sender, Requested_Block_Struct **newblocks, int &iCount) /*const*/;
	void	WritePartStatus(CSafeMemFile &file) const;
	void	WriteCompleteSourcesCount(CSafeMemFile &file) const;
	void	AddSources(CSafeMemFile *sources, uint32 serverip, uint16 serverport, bool bWithObfuscationAndHash);
	static bool CanAddSource(uint32 userid, uint16 port, uint32 serverip, uint16 serverport, UINT *pdebug_lowiddropped = NULL, bool ed2kID = true);
	static bool IsOwnSource(uint32 userid, uint16 port, uint32 serverip, uint16 serverport, bool ed2kID = true);

	EPartFileStatus	GetStatus(bool ignorepause = false) const;
	void	SetStatus(EPartFileStatus eStatus);		// set status and update GUI
	void	_SetStatus(EPartFileStatus eStatus);	// set status and do *not* update GUI

	void	NotifyStatusChange();
	bool	IsStopped() const							{ return m_stopped; }
	bool	GetCompletionError() const					{ return m_bCompletionError; }
	EMFileSize	GetCompletedSize() const				{ return m_completedsize; }
	CString getPartfileStatus() const;
	int		getPartfileStatusRank() const;
	void	SetActive(bool bActive);

	uint8	GetDownPriority() const						{ return m_iDownPriority; }
	void	SetDownPriority(uint8 NewPriority, bool resort = true);
	bool	IsAutoDownPriority() const					{ return m_bAutoDownPriority; }
	void	SetAutoDownPriority(bool NewAutoDownPriority) { m_bAutoDownPriority = NewAutoDownPriority; }
	void	UpdateAutoDownPriority();

	UINT	GetSourceCount() const						{ return static_cast<UINT>(srclist.GetCount()); }
	UINT	GetSrcA4AFCount() const						{ return static_cast<UINT>(A4AFsrclist.GetCount()); }
	UINT	GetSrcStatisticsValue(EDownloadState nDLState) const { return m_anStates[nDLState]; } //ASSERT(nDLState < _countof(m_anStates));
	UINT	GetTransferringSrcCount() const;
	uint64	GetTransferred() const						{ return m_uTransferred; }
	uint32	GetDatarate() const							{ return m_datarate; }
	float	GetPercentCompleted() const					{ return m_percentcompleted; }
	UINT	GetNotCurrentSourcesCount() const;
	int		GetValidSourcesCount() const;
	UINT	GetAvailableSrcCount() const;
	bool	IsArchive(bool onlyPreviewable = false) const; // Barry - Also want to preview archives
	bool	IsPreviewableFileType() const;
	time_t	getTimeRemaining() const;
	time_t	GetDlActiveTime() const;

	// Barry - Added as replacement for BlockReceived to buffer data before writing to disk
	uint32	WriteToBuffer(uint64 transize, const BYTE *data, uint64 start, uint64 end, Requested_Block_Struct *block, const CUpDownClient *client, bool bCopyData);
	void	FlushBuffer(bool bForceICH = false, bool bNoAICH = false);
	// Barry - This will invert the gap list, up to the caller to delete gaps when done
	// 'Gaps' returned are really the filled areas, and guaranteed to be in order
	void	GetFilledArray(CArray<Gap_Struct> &filled) const;

	// Barry - Added to prevent list containing deleted blocks on shutdown
	void	RemoveAllRequestedBlocks()					{ requestedblocks_list.RemoveAll(); }
	bool	RemoveBlockFromList(uint64 start, uint64 end);
	void	RemoveAllSources(bool bTryToSwap);

	bool	CanOpenFile() const;
	bool	IsReadyForPreview() const;
	bool	CanStopFile() const;
	bool	CanPauseFile() const;
	bool	CanResumeFile() const;
	bool	IsPausingOnPreview() const					{ return m_bPauseOnPreview && IsPreviewableFileType() && CanPauseFile(); }

	void	OpenFile() const;
	void	PreviewFile(const int iAppIndex = -1);
	void	DeletePartFile(bool bAddToCanceledMet = true);
	void	StopFile(bool bCancel);
	void	PauseFile(bool bInsufficient = false);
	void	StopPausedFile();
	void	ResumeFile(const bool resort = true, const bool savepart = true);
	void	ResumeFileInsufficient();
	void	SetPauseOnPreview(bool bVal)				{ m_bPauseOnPreview = bVal; }

	virtual Packet* CreateSrcInfoPacket(const CUpDownClient *forClient, uint8 byRequestedVersion, uint16 nRequestedOptions) const;
	void	AddClientSources(CSafeMemFile *sources, uint8 uClientSXVersion, bool bSourceExchange2, const CUpDownClient *pClient = NULL);

	UINT	GetAvailablePartCount() const				{ return (status == PS_COMPLETING || status == PS_COMPLETE) ? GetPartCount() : availablePartsCount; }
	void	UpdateAvailablePartsCount();

	DWORD	GetLastAnsweredTime() const					{ return m_ClientSrcAnswered; }
	void	SetLastAnsweredTime()						{ m_ClientSrcAnswered = ::GetTickCount(); }
	void	SetLastAnsweredTimeTimeout()				{ m_ClientSrcAnswered = ::GetTickCount() + 2 * CONNECTION_LATENCY - SOURCECLIENTREASKS; }

	uint64	GetCorruptionLoss() const					{ return m_uCorruptionLoss; }
	uint64	GetCompressionGain() const					{ return m_uCompressionGain; }
	uint32	GetRecoveredPartsByICH() const				{ return m_uPartsSavedDueICH; }

	virtual void	UpdateFileRatingCommentAvail(bool bForceUpdate = false);
	virtual void	RefilterFileComments();

	void	AddDownloadingSource(CUpDownClient *client);
	void	RemoveDownloadingSource(CUpDownClient *client);

	const CStringA GetProgressString(uint16 size) const;
	CString GetInfoSummary(bool bNoFormatCommands = false) const;

	void	UpdateDisplayedInfo(bool force = false);

	UINT	GetCategory() /*const*/;
	void	SetCategory(UINT cat);
	bool	HasDefaultCategory() const;
	bool	CheckShowItemInGivenCat(INT_PTR inCategory) /*const*/;

	//preview
	bool CopyPartFile(CArray<Gap_Struct> &raFilled, const CString &tempFileName);
	virtual bool GrabImage(uint8 nFramesToGrab, double dStartTime, bool bReduceColor, uint16 nMaxWidth, void *pSender);
	virtual void GrabbingFinished(HBITMAP *imgResults, uint8 nFramesGrabbed, void *pSender);

	void	FlushBuffersExceptionHandler(CFileException *ex);
	void	FlushBuffersExceptionHandler();

	void	PerformFileCompleteEnd(DWORD dwResult);

	void	SetFileOp(EPartFileOp eFileOp)				{ m_eFileOp = eFileOp; }
	EPartFileOp GetFileOp() const						{ return m_eFileOp; }
	void	SetFileOpProgress(WPARAM uProgress)			{ m_uFileOpProgress = uProgress; } //ASSERT(uProgress <= 100);
	WPARAM	GetFileOpProgress() const					{ return m_uFileOpProgress; }

	CAICHRecoveryHashSet* GetAICHRecoveryHashSet() const { return m_pAICHRecoveryHashSet; }
	void	RequestAICHRecovery(UINT nPart);
	void	AICHRecoveryDataAvailable(UINT nPart);
	bool	IsAICHPartHashSetNeeded() const				{ return m_FileIdentifier.HasAICHHash() && !m_FileIdentifier.HasExpectedAICHHashCount() && m_bAICHPartHashsetNeeded; }
	void	SetAICHHashSetNeeded(bool bVal)				{ m_bAICHPartHashsetNeeded = bVal; }

	bool	AllowSwapForSourceExchange(DWORD dwTick) const { return dwTick >= lastSwapForSourceExchangeTick + SEC2MS(30); } // ZZ:DownloadManager
	void	SetSwapForSourceExchangeTick()				{ lastSwapForSourceExchangeTick = ::GetTickCount(); } // ZZ:DownloadManager

	UINT	SetPrivateMaxSources(uint32 in)				{ return m_uMaxSources = in; }
	UINT	GetPrivateMaxSources() const				{ return m_uMaxSources; }
	UINT	GetMaxSources() const;
	UINT	GetMaxSourcePerFileSoft() const;
	UINT	GetMaxSourcePerFileUDP() const;

	bool	GetPreviewPrio() const						{ return m_bpreviewprio; }
	void	SetPreviewPrio(bool in)						{ m_bpreviewprio = in; }

	static bool RightFileNotHasHigherPrio(CPartFile* left, CPartFile* right);
	static bool RightFileHasHigherPrio(CPartFile *left, CPartFile *right);
	bool	IsDeleting() const							{ return m_bDelayDelete; }
#ifdef _DEBUG
	// Diagnostic Support
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext &dc) const;
#endif
	time_t	m_tLastChecked;
	CCriticalSection m_SavePartFileLock;
	CCriticalSection m_PartStatusLock;
	CCriticalSection m_SaveSourcesLock;
	CCriticalSection m_HPartFileLock;
	CCriticalSection m_PartFileDeleteLock;

private:
	CList<PartfileSourceCache>	m_sourcecache;
	uint32						m_lastSoureCacheProcesstime;
public:
	void	ProcessSourceCache();
	void	AddToSourceCache(uint16 nPort, uint32 dwID, const CAddress& IPv6, uint32 dwServerIP, uint16 nServerPort, ESourceFrom sourcefrom, bool ed2kIDFlag = false, const uchar* achUserHash = NULL, uint8 byCryptOptions = 0);
	void	ClearSourceCache();
	const  uint32	GetSourceCacheAmount() const { return m_sourcecache.GetCount(); }

private:
	CArray<uint16, uint16> m_SrcIncPartFrequency;
public:
	const bool GetNextRequestedBlockICS(CUpDownClient* sender, Requested_Block_Struct** newblocks, int& iCount);
	const bool GetNextRequestedBlockStd(CUpDownClient* sender, Requested_Block_Struct** newblocks, int& iCount) /*const*/;
	const uint16* CalcDownloadingParts(const CUpDownClient* client);
	void WriteIncPartStatus(CSafeMemFile* file);
	void NewSrcIncPartsInfo();
	const uint64 GetPartSizeToDownload(const uint16 partNumber) const;

	CDeadSourceList	m_DeadSourceList;
	CUpDownClientPtrList srclist;
	CUpDownClientPtrList A4AFsrclist;	//<<-- enkeyDEV(Ottavio84) -A4AF-
	CTime	lastseencomplete;
	CFile	m_hpartfile;				// permanent opened handle to avoid write conflicts
	CMutex	m_FileCompleteMutex;		// Lord KiRon - Mutex for file completion
	HANDLE	m_hWrite;					// asynchronous part file writing
	int		m_iWrites;					// outstanding I/O counter - read only in the main thread
	uint64	m_iAllocationSize;
	DWORD	m_LastSearchTime;
	DWORD	m_LastSearchTimeKad;
	uint16	src_stats[4];
	uint16	net_stats[3];
	uint8	m_TotalSearchesKad;
	volatile bool m_bPreviewing;
	volatile bool m_bRecoveringArchive; // Is archive recovery in progress
	bool	m_bLocalSrcReqQueued;
	bool	srcarevisible;				// used for downloadlistctrl
	bool	m_bMD4HashsetNeeded;
	CSourceSaver::Sources srcstosave;
	CSourceSaver m_sourcesaver;
	bool m_bSaveSourcesInQueue;
	bool m_bFlushPartMetInQueue;

protected:
	bool	GetNextEmptyBlockInPart(UINT partNumber, Requested_Block_Struct *pReqBlock) const;
	void	CompleteFile(bool bIsHashingDone);
	void	CreatePartFile(UINT cat = 0);
	void	Init();

private:
	BOOL	PerformFileComplete(); // Lord KiRon
	static UINT CompleteThreadProc(LPVOID pvParams); // Lord KiRon - Used as separate thread to complete file
	void	CharFillRange(CStringA &buffer, uint32 start, uint32 end, char color) const;
	void	AddToSharedFiles();
	void	DeleteWrittenItem(const POSITION pos);

	static CBarShader s_LoadBar;
	static CBarShader s_ChunkBar;
	CCorruptionBlackBox	m_CorruptionBlackBox;
	CList<Gap_Struct> m_gaplist;
	CTypedPtrList<CPtrList, Requested_Block_Struct*> requestedblocks_list;
	// Barry - Buffered data to be written
	CTypedPtrList<CPtrList, PartFileBufferedData*> m_BufferedData_list;
	CArray<uint16, uint16> m_SrcPartFrequency;
	CList<uint16, uint16> corrupted_list;
	CArray<bool, bool> m_aChangedPart;
	CUpDownClientPtrList m_downloadingSourceList;
	CString m_fullname;
	CString m_partmetfilename;
	CAICHRecoveryHashSet *m_pAICHRecoveryHashSet;
	float	m_percentcompleted;
	EMFileSize	m_completedsize;
	uint64	m_uTransferred;
	uint64	m_uCorruptionLoss;
	uint64	m_uCompressionGain;
	uint64	m_nTotalBufferData;
	time_t	m_iLastPausePurge;
	time_t	m_tActivated;
	time_t	m_nDlActiveTime;
	time_t	m_tLastModified;	// last file modification time (NT's version of UTC), to be used for stats only!
	time_t	m_tCreated;			// file creation time (NT's version of UTC), to be used for stats only!
	volatile WPARAM m_uFileOpProgress;
	DWORD	lastSwapForSourceExchangeTick; // ZZ:DownloadManaager
	DWORD	m_lastRefreshedDLDisplay;
	DWORD	m_nLastBufferFlushTime;
	DWORD	m_nNextMetFlushTime; //update .part.met file
	DWORD	m_nFileFlushTime; //if file is idle long enough, flush new data to disk
	DWORD	m_dwFileAttributes;
	DWORD	m_random_update_wait;
	UINT	m_anStates[STATES_COUNT];
	UINT	m_category;
	UINT	m_uMaxSources;
	UINT	availablePartsCount;
	DWORD	m_ClientSrcAnswered;
	DWORD	m_lastpurgetime;
	uint32	m_LastNoNeededCheck;
	uint32	m_uPartsSavedDueICH;
	uint32	m_datarate;
public:
	EPartFileStatus	status;
private:
	volatile EPartFileOp m_eFileOp;
	byte	m_refresh; //delay counter for display
	uint8	m_iDownPriority;
	bool	m_paused;
	bool	m_stopped;
	bool	m_bPauseOnPreview;
	bool	m_insufficient;
	bool	m_bCompletionError;
	bool	m_bAICHPartHashsetNeeded;
	bool	m_bAutoDownPriority;
	bool	m_bDelayDelete;
	bool	m_bpreviewprio;
};
