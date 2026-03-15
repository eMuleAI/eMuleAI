//This file is part of eMule AI
//Copyright (C)2002-2026 Merkur ( merkur-@users.sourceforge.net / https://www.emule-project.net )
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
#include "MuleListCtrl.h"
#include "ListCtrlItemWalk.h"

class CAbstractFile;

class CCollectionListCtrl : public CMuleListCtrl, public CListCtrlItemWalk
{
	DECLARE_DYNAMIC(CCollectionListCtrl)

public:
	CCollectionListCtrl();

	void Init(const CString &strNameAdd);

	void AddFileToList(CAbstractFile *pAbstractFile);
	void RemoveFileFromList(CAbstractFile *pAbstractFile);

private:
	enum ECols
	{
		colName = 0,
		colSize,
		colHash
	};

protected:
	static int CALLBACK SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnNmRClick(LPNMHDR, LRESULT *pResult);
};