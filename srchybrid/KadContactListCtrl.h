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
#include "kademlia/routing/contact.h"

class CKademliaWnd;

class CKadContactListCtrl : public CMuleListCtrl
{
	DECLARE_DYNAMIC(CKadContactListCtrl)
	friend class CKademliaWnd;

public:
	CKadContactListCtrl();

private:
	enum ECols
	{
		colIP = 0,
		colID,
		colType,
		colDistance,
		colCountry
	};

protected:
	bool ContactAdd(const Kademlia::CContact *contact);
	void ContactRem(const Kademlia::CContact *contact);
	void ContactRef(const Kademlia::CContact *contact);

	void Init();
	void Localize();
public:
	void SaveAllSettings();
protected:
	void UpdateKadContactCount();
	void SetAllIcons();

	static int CALLBACK SortProc(const LPARAM lParam1, const LPARAM lParam2, const LPARAM lParamSort);

	virtual BOOL OnCommand(WPARAM, LPARAM);
	virtual void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);
	CImageList	m_ImageList;
	const CString GetItemDisplayText(const Kademlia::CContact* contact, const int iSubItem) const;

	DECLARE_MESSAGE_MAP()
	afx_msg void OnLvnColumnClick(LPNMHDR pNMHDR, LRESULT *pResult);
	afx_msg void OnLvnGetDispInfo(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnSysColorChange();
};