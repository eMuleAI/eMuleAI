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
#include "MuleListCtrl.h"
namespace Kademlia
{
	class CSearch;
	class CLookupHistory;
}

class CIni;

class CKadSearchListCtrl : public CMuleListCtrl
{
	DECLARE_DYNAMIC(CKadSearchListCtrl)

public:
	CKadSearchListCtrl();

	void	SearchAdd(const Kademlia::CSearch *search);
	void	SearchRem(const Kademlia::CSearch *search);
	void	SearchRef(const Kademlia::CSearch *search);

	void	Init();
	void	Localize();
	void	UpdateKadSearchCount();

	Kademlia::CLookupHistory* FetchAndSelectActiveSearch(bool bMark);

private:
	enum ECols
	{
		colNum = 0,
		colKey,
		colType,
		colName,
		colStop,
		colLoad,
		colPacketsSent,
		colResponses
	};

protected:
	void UpdateSearch(int iItem, const Kademlia::CSearch *search);
	void SetAllIcons();

	static int CALLBACK SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort);

	virtual BOOL OnCommand(WPARAM, LPARAM);

	DECLARE_MESSAGE_MAP()
	afx_msg void OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnSysColorChange();
};