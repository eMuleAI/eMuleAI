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
#include "ShareableFile.h"
#include "emule.h"
#include "otherfunctions.h"

IMPLEMENT_DYNAMIC(CShareableFile, CAbstractFile)

CShareableFile::CShareableFile()
	: m_verifiedFileType(FILETYPE_UNKNOWN)
{
}

CString CShareableFile::GetInfoSummary(bool bNoFormatCommands) const
{
	CString strFolder(GetPath());
	unslosh(strFolder);

	CString strType(GetFileTypeDisplayStr());
	if (strType.IsEmpty())
		strType += _T('-');

	CString info(GetFileName());
	info.AppendFormat(_T("\n")
		_T("%s %s\n")
		_T("%s\n")
		_T("%s: %s\n")
		_T("%s: %s")
		, (LPCTSTR)GetResString(_T("FD_SIZE")), (LPCTSTR)CastItoXBytes((uint64)GetFileSize())
		, bNoFormatCommands ? EMPTY : _T("<br_head>")
		, (LPCTSTR)GetResString(_T("TYPE")), (LPCTSTR)strType
		, (LPCTSTR)GetResString(_T("FOLDER")), (LPCTSTR)strFolder
	);
	return info;
}