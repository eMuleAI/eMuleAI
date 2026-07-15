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
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#pragma once
#include "BarShader.h"
#include "StatisticFile.h"
#include "ShareableFile.h"

class CUpDownClient;
class Packet;
class CFileDataIO;
class CAICHHashTree;
class CAICHRecoveryHashSet;
class CCollection;
class CAICHHashAlgo;
class CSafeMemFile;
class CPartFile;

struct SKnownFileMetaData
{
	SKnownFileMetaData()
		: uLengthSec()
		, uBitrateKbps()
	{
	}

	uint32 uLengthSec;
	uint32 uBitrateKbps;
	CString strCodec;
	CString strTitle;
	CString strArtist;
	CString strAlbum;
};

typedef CTypedPtrList<CPtrList, CUpDownClient*> CUpDownClientPtrList;

struct SFileHashProgressContext
{
	CPartFile* pPartFile;
	DWORD dwRuntimeID;
	uchar abyFileHash[16];
};

class CKnownFile : public CShareableFile
{
	DECLARE_DYNAMIC(CKnownFile)

public:
	CKnownFile();
	virtual	~CKnownFile();

	virtual void SetFileName(LPCTSTR pszFileName, bool bReplaceInvalidFileSystemChars = false, bool bRemoveControlChars = false); // 'bReplaceInvalidFileSystemChars' is set to 'false' for backward compatibility!

	bool	CreateFromFile(LPCTSTR directory, LPCTSTR filename, const SFileHashProgressContext* pProgressContext, bool bUpdateMetaData = true); // create date, hashset and tags from a file
	bool	LoadFromFile(CFileDataIO &file, bool bLoadCachedAICHPartHashSet = true);	//load date, hashset and tags from a .met file
	bool	WriteToFile(CFileDataIO &file);
	bool	CreateAICHHashSetOnly();
	static bool CreateAICHHashSetFromFile(LPCTSTR pszFilePath, EMFileSize nFileSize, CAICHRecoveryHashSet &rAICHHashSet);

	// last file modification time in (DST corrected, if NTFS) real UTC format
	// NOTE: this value can *not* be compared with NT's version of the UTC time
	CTime	GetUtcCFileDate() const						{ return CTime(m_tUtcLastModified); }
	time_t	GetUtcFileDate() const						{ return m_tUtcLastModified; }
	void	SetUtcFileDate(time_t t)					{ m_tUtcLastModified = t; } // Refresh stored UTC file date

	// Did we not see this file for a long time so that some information should be purged?
	bool	ShouldPartiallyPurgeFile() const;
	void	SetLastSeen()								{ m_timeLastSeen = time(NULL); }

	virtual void	SetFileSize(EMFileSize nFileSize);

	// nr. of 9MB parts (file data)
	inline uint16 GetPartCount() const					{ return m_iPartCount; }

	// nr. of 9MB parts according the file size wrt ED2K protocol (OP_FILESTATUS)
	inline uint16 GetED2KPartCount() const				{ return m_iED2KPartCount; }

	// file upload priority
	uint8	GetUpPriority() const						{ return m_iUpPriority; }
	uint8	GetUpPriorityEx(void) const { return  m_iUpPriority + 1 == 5 ? 0 : m_iUpPriority + 1; }
	void	SetUpPriority(uint8 iNewUpPriority, bool bSave = true);
	bool	IsAutoUpPriority() const					{ return m_bAutoUpPriority; }
	void	SetAutoUpPriority(bool NewAutoUpPriority)	{ m_bAutoUpPriority = NewAutoUpPriority; }
	void	UpdateAutoUpPriority();

	// This has lost its meaning here. This is the total clients we know that want this file.
	// Right now this number is used for auto priorities.
	// This may be replaced with total complete source known in the network.
	INT_PTR	GetQueuedCount()							{ return m_ClientUploadList.GetCount(); }

	bool	HideOvershares(CSafeMemFile &file, CUpDownClient *client);
	void	AddUploadingClient(CUpDownClient *client);
	void	RemoveUploadingClient(CUpDownClient *client);
	virtual void	UpdatePartsInfo();
	void	RefreshUploadingClientPartStatusCache(bool bResetSelectedChunk = false);
	virtual	void	DrawShareStatusBar(CDC *dc, LPCRECT rect, bool onlygreyrect, bool bFlat) const;

	// comment
	void	SetFileComment(LPCTSTR pszComment);

	void	SetFileRating(UINT uRating);

	bool	GetPublishedED2K() const					{ return m_PublishedED2K; }
	void	SetPublishedED2K(bool val);

	uint32	GetKadFileSearchID() const					{ return kadFileSearchID; }
	void	SetKadFileSearchID(uint32 id)				{ kadFileSearchID = id; } //Don't use this unless you know what your are DOING!! (Hopefully I do. :)

	const Kademlia::WordList &GetKadKeywords() const	{ return wordlist; }

	time_t	GetLastPublishTimeKadSrc() const			{ return m_lastPublishTimeKadSrc; }
	void	SetLastPublishTimeKadSrc(time_t time, uint32 servingBuddyIP)	{ m_lastPublishTimeKadSrc = time; m_lastServingBuddyIP = servingBuddyIP; }
	uint32	GetLastPublishServingBuddy() const			{ return m_lastServingBuddyIP; }
	void	SetLastPublishTimeKadNotes(time_t time)		{ m_lastPublishTimeKadNotes = time; }
	time_t	GetLastPublishTimeKadNotes() const			{ return m_lastPublishTimeKadNotes; }

	bool	PublishSrc();
	bool	PublishNotes();

	// file sharing
	virtual Packet* CreateSrcInfoPacket(const CUpDownClient *forClient, uint8 byRequestedVersion, uint16 nRequestedOptions) const;
	UINT	GetMetaDataVer() const						{ return m_uMetaDataVer; }
	static bool ExtractMetaData(const CShareableFile* pFile, SKnownFileMetaData& rMetaData);
	void	ApplyMetaDataTags(const SKnownFileMetaData* pMetaData);
	void	UpdateMetaDataTags();
	void	RemoveMetaDataTags(UINT uTagType = 0);
	void	RemoveBrokenUnicodeMetaDataTags();

	// preview
	bool	IsMovie() const;
	virtual bool GrabImage(uint8 nFramesToGrab, double dStartTime, bool bReduceColor, uint16 nMaxWidth, void *pSender);
	virtual void GrabbingFinished(HBITMAP *imgResults, uint8 nFramesGrabbed, void *pSender);

	bool	ImportParts();
	bool	ImportPartsFromFile(LPCTSTR pszImportPath);

	// Display / Info / Strings
	virtual CString	GetInfoSummary(bool bNoFormatCommands = false) const;
	CString	GetUpPriorityDisplayString() const;
	virtual void	UpdateFileRatingCommentAvail(bool bForceUpdate = false);
	double	GetRatio() const
	{
		const uint64 fileSize = static_cast<uint64>(GetFileSize());
		return fileSize > 0 ? static_cast<double>(statistic.GetTransferred()) / static_cast<double>(fileSize) : 0.0;
	}
	double	GetAllTimeRatio() const
	{
		const uint64 fileSize = static_cast<uint64>(GetFileSize());
		return fileSize > 0 ? static_cast<double>(statistic.GetAllTimeTransferred()) / static_cast<double>(fileSize) : 0.0;
	}

	//aich
	void	SetAICHRecoverHashSetAvailable(bool bVal)	{ m_bAICHRecoverHashSetAvailable = bVal; }
	bool	IsAICHRecoverHashSetAvailable() const		{ return m_bAICHRecoverHashSetAvailable; }

	int		GetPermissions() const						{ return m_iPermissions; }
	void	SetPermissions(int iNewPermissions);

	void	SetPowerShared(int newValue);
	bool	GetPowerShared() const;
	int		GetPowerSharedMode() const					{ return m_iPowerSharedMode; }
	bool	GetPowerShareAuthorized() const				{ return m_bPowerShareAuthorized; }
	bool	GetPowerShareAuto() const					{ return m_bPowerShareAuto; }
	void	SetPowerShareLimit(int newValue);
	int		GetPowerShareLimit() const					{ return m_iPowerShareLimit; }
	bool	GetPowerShareLimited() const				{ return m_bPowerShareLimited; }
	void	UpdatePowerShareLimit(bool authorizePowerShare, bool autoPowerShare, bool limitedPowerShare);

	void	SetSpreadbarSetStatus(int newValue);
	int		GetSpreadbarSetStatus() const				{ return m_iSpreadbarSetStatus; }
	void	SetHideOS(int newValue);
	int		GetHideOS() const							{ return m_iHideOS; }
	void	SetSelectiveChunk(int newValue);
	int		GetSelectiveChunk() const					{ return m_iSelectiveChunk; }
	void	SetHideOSAuthorized(bool newValue)			{ m_bHideOSAuthorized = newValue; }
	UINT	HideOSInWork() const;
	void	SetShareOnlyTheNeed(int newValue);
	int		GetShareOnlyTheNeed() const				{ return m_iShareOnlyTheNeed; }
	uint16	GetVirtualCompleteSourcesCount() const		{ return m_nVirtualCompleteSourcesCount; }

	static bool	CreateHash(const uchar *pucData, uint32 uSize, uchar *pucHash, CAICHHashTree *pShaHashOut = NULL);


	CStatisticFile statistic;
	// last file modification time in (DST corrected, if NTFS) real UTC format
	// NOTE: this value can *not* be compared with NT's version of the UTC time
	time_t	m_tUtcLastModified;

	time_t	m_nCompleteSourcesTime;
	uint16	m_nCompleteSourcesCount;
	uint16	m_nCompleteSourcesCountLo;
	uint16	m_nCompleteSourcesCountHi;
	uint16	m_nVirtualCompleteSourcesCount;
	CUpDownClientPtrList m_ClientUploadList;
	CArray<uint16, uint16> m_AvailPartFrequency;
	CArray<uint64, uint64> m_PartSentCount;
	CCollection *m_pCollection;
	//overlapped disk reads
	HANDLE		m_hRead;
	int			nInUse; //count outstanding I/O (reads) to know if the file is in use
	bool		bCompress;
	bool		bNoNewReads; //blocks new overlapped reads
#ifdef _DEBUG
	// Diagnostic Support
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext &dc) const;
#endif

	bool	ShouldCompletelyPurgeFile() const;
	static void ReleaseBarShaderBuffers() noexcept;
protected:
	//preview
	bool	GrabImage(const CString &strFileName, uint8 nFramesToGrab, double dStartTime, bool bReduceColor, uint16 nMaxWidth, void *pSender);
	bool	LoadTagsFromFile(CFileDataIO &file, bool bLoadCachedAICHPartHashSet);
	bool	LoadDateFromFile(CFileDataIO &file);
	virtual void CalcPartSpread(CArray<uint64, uint64> &partspread, CUpDownClient *client);
	static void	CreateHash(CFile *pFile, uint64 Length, uchar *pucHash, CAICHHashTree *pShaHashOut = NULL);
	static bool	CreateHash(FILE *fp, uint64 uSize, uchar *pucHash, CAICHHashTree *pShaHashOut = NULL);

private:
	Kademlia::WordList wordlist;
	time_t	m_timeLastSeen; // we only "see" files when they are in a shared directory
	time_t	m_lastPublishTimeKadSrc;
	time_t	m_lastPublishTimeKadNotes;
	uint32	kadFileSearchID;
	uint32	m_lastServingBuddyIP;
	UINT	m_uMetaDataVer;
	uint16	m_iPartCount;
	uint16	m_iED2KPartCount;
	uint8	m_iUpPriority;
	bool	m_bAutoUpPriority;
	bool	m_PublishedED2K;
	bool	m_bAICHRecoverHashSetAvailable;
	int		m_iPermissions;
	int		m_iSpreadbarSetStatus;
	int		m_iHideOS;
	int		m_iSelectiveChunk;
	bool	m_bHideOSAuthorized;
	int		m_iShareOnlyTheNeed;
	int		m_iPowerSharedMode;
	bool	m_bPowerShareAuthorized;
	bool	m_bPowerShareAuto;
	bool	m_bPowerShared;
	int		m_iPowerShareLimit;
	bool	m_bPowerShareLimited;
};

#define PERM_ALL		0
#define PERM_FRIENDS	1
#define PERM_NOONE		2
