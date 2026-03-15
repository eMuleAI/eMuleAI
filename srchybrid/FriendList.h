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
#include "eMuleAI/Address.h"

class CFriend;
class CFriendListCtrl;
class CUpDownClient;

class CFriendList
{
public:
	CFriendList();
	~CFriendList();

	bool		IsAlreadyFriend(const CString &strUserHash) const;
	bool		IsValid(CFriend *pToCheck) const;
	void		SaveList();
	bool		LoadList();
	void		RefreshFriend(CFriend *torefresh) const;
	CFriend* SearchFriend(const uchar* achUserHash, CAddress dwIP, uint16 nPort) const;
	void		SetWindow(CFriendListCtrl *NewWnd)	{ m_wndOutput = NewWnd; }
	void		ShowFriends() const;
	bool		AddFriend(CUpDownClient *toadd);
	bool		AddFriend(const uchar* abyUserhash, uint32 dwLastSeen, const CAddress& dwLastUsedIP, uint16 nLastUsedPort
						, uint32 dwLastChatted, LPCTSTR pszName, uint32 dwHasHash);
	void		RemoveFriend(CFriend *todel);
	void		RemoveAllFriendSlots();
	void		Process();
	INT_PTR		GetCount() const					{ return m_listFriends.GetCount(); }

private:
	CTypedPtrList<CPtrList, CFriend*>	m_listFriends;
	CFriendListCtrl						*m_wndOutput;
	DWORD								m_nLastSaved;

public:
	bool		IsAlreadyFriend(uchar userHash[]) const;
	bool		AddEmfriendsMetToList(const CString& strFile);
};