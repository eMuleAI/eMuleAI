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
#include <list>
#include "opcodes.h"
#include "FileIdentifier.h"

/*
										CPartFile
										/
							CKnownFile
							/
			CShareableFile
			/
CAbstractFile - CCollectionFile
			\
			CSearchFile
*/

namespace Kademlia
{
	class CUInt128;
	class CEntry;
	class CKadTagValueString;
	typedef std::list<CKadTagValueString> WordList;
};

class CTag;

typedef CTypedPtrList<CPtrList, Kademlia::CEntry*> CKadEntryPtrList;

class CAbstractFile: public CObject
{
	DECLARE_DYNAMIC(CAbstractFile)

public:
	CAbstractFile();
	explicit CAbstractFile(const CAbstractFile *pAbstractFile);
	virtual	~CAbstractFile();

	const CString& GetFileName() const					{ return m_strFileName; }
	// 'bReplaceInvalidFileSystemChars' is set to 'false' for backward compatibility!
	virtual void SetAFileName(LPCTSTR pszFileName, bool bReplaceInvalidFileSystemChars = false, bool bAutoSetFileType = true, bool bRemoveControlChars = false);

	// returns the ED2K file type (an ASCII string)
	const CString& GetFileType() const					{ return m_strFileType; }
	virtual void SetFileType(LPCTSTR pszFileType);

	// returns the file type which is used to be shown in the GUI
	CString GetFileTypeDisplayStr() const;

	CFileIdentifier& GetFileIdentifier()				{ return m_FileIdentifier; }
	const CFileIdentifier& GetFileIdentifierC() const	{ return m_FileIdentifier; }
	const uchar* GetFileHash() const					{ return m_FileIdentifier.GetMD4Hash(); }
	void SetFileHash(const uchar *pucFileHash)			{ m_FileIdentifier.SetMD4Hash(pucFileHash); }
	bool HasNullHash() const;
	CString GetED2kLink(bool bHashset = false, bool bHTML = false, bool bHostname = false, bool bSource = false, CAddress IP = CAddress()) const;
	EMFileSize GetFileSize() const						{ return m_nFileSize; }
	virtual void SetFileSize(EMFileSize nFileSize)		{ m_nFileSize = nFileSize; }
	bool	IsLargeFile() const							{ return (uint64)m_nFileSize > OLD_MAX_EMULE_FILE_SIZE; }

	uint32	GetIntTagValue(uint8 tagname) const;
	uint32	GetIntTagValue(LPCSTR tagname) const;
	bool	GetIntTagValue(uint8 tagname, uint32 &ruValue) const;
	uint64	GetInt64TagValue(uint8 tagname) const;
	uint64	GetInt64TagValue(LPCSTR tagname) const;
	bool	GetInt64TagValue(uint8 tagname, uint64 &ruValue) const;
	void	SetIntTagValue(uint8 tagname, uint32 uValue);
	void	SetInt64TagValue(uint8 tagname, uint64 uValue);
	const CString& GetStrTagValue(uint8 tagname) const;
	const CString& GetStrTagValue(LPCSTR tagname) const;
	void	SetStrTagValue(uint8 tagname, LPCTSTR);
	CTag*	GetTag(uint8 tagname, uint8 tagtype) const;
	CTag*	GetTag(LPCSTR tagname, uint8 tagtype) const;
	CTag*	GetTag(uint8 tagname) const;
	CTag*	GetTag(LPCSTR tagname) const;
	const CArray<CTag*, CTag*>& GetTags() const			{ return m_taglist; }
	void	AddTagUnique(CTag *pTag);
	void	DeleteTag(uint8 tagname);
	void	DeleteTag(CTag *pTag);
	void	ClearTags();
	void	CopyTags(const CArray<CTag*, CTag*> &tags);
	virtual bool IsPartFile() const						{ return false; }

	bool	HasComment() const							{ return m_bHasComment; }
	void	SetHasComment(bool in)						{ m_bHasComment = in; }
	UINT	UserRating(bool bKadSearchIndicator = false) const { return (bKadSearchIndicator && m_bKadCommentSearchRunning) ? 6 : m_uUserRating; }
	bool	HasRating()	const							{ return m_uUserRating > 0; }
	bool	HasBadRating()	const						{ return HasRating() && m_uUserRating < 2; }
	void	SetUserRating(UINT in)						{ m_uUserRating = in; }
	const CString &GetFileComment();
	UINT	GetFileRating();
	void	LoadComment();
	virtual void UpdateFileRatingCommentAvail(bool bForceUpdate = false) = 0;

	bool	AddNote(const Kademlia::CEntry &cEntry);
	void	RefilterKadNotes(bool bUpdate = true);
	const	CKadEntryPtrList& getNotes() const			{ return m_kadNotes; }

	bool	IsKadCommentSearchRunning() const			{ return m_bKadCommentSearchRunning; }
	void	SetKadCommentSearchRunning(bool bVal);

	CCriticalSection m_mutTagList;

#ifdef _DEBUG
	// Diagnostic Support
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext &dc) const;
#endif

protected:
	CArray<CTag*, CTag*> m_taglist;
	CKadEntryPtrList m_kadNotes;
	EMFileSize m_nFileSize; //must be before m_FileIdentifier due to initialisation list order
	CFileIdentifier m_FileIdentifier;
	CString	m_strFileName;
	CString	m_strComment;
	CString	m_strFileType;
	UINT	m_uRating;
	UINT	m_uUserRating;
	bool	m_bCommentLoaded;
	bool	m_bHasComment;
	bool	m_bKadCommentSearchRunning;
};