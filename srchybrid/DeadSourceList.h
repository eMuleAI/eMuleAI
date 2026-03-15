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
#include "otherfunctions.h"

///////////////////////////////////////////////////////////////////////////////////////
//// CDeadSource

class CDeadSource : public CObject
{
public:
	CDeadSource();
	explicit CDeadSource(const CUpDownClient &client);
	CDeadSource(const CDeadSource &ds)			{ *this = ds; }

	CDeadSource& operator=(const CDeadSource &ds);
	friend bool operator==(const CDeadSource &ds1, const CDeadSource &ds2);

	uchar			m_aucHash[MDX_DIGEST_SIZE];
	uint32			m_dwServerIP;
	uint32			m_dwID;
	uint16			m_nPort;
	uint16			m_nKadPort;
};

template<> inline UINT AFXAPI HashKey(const CDeadSource &ds)
{
	uint32 hash = ds.m_dwID;
	if (hash != 0) {
		if (::IsLowID(hash))
			hash ^= ds.m_dwServerIP;
		return hash;
	}
	for (int i = MDX_DIGEST_SIZE; --i >= 0;)
		hash += (ds.m_aucHash[i] + 1) * (i * i + 1);
	return hash + 1;
};

///////////////////////////////////////////////////////////////////////////////////////
//// CDeadSourceList
class CUpDownClient;
class CDeadSourceList
{
public:
	CDeadSourceList();
	~CDeadSourceList() = default;
	void	AddDeadSource(const CUpDownClient &client);
	void	RemoveDeadSource(const CUpDownClient &client);
	bool	IsDeadSource(const CUpDownClient &client) const;
	INT_PTR	GetDeadSourcesCount() const			{ return m_mapDeadSources.GetCount(); }
	void	Init(bool bGlobalList);

protected:
	void	CleanUp();

private:
	typedef CMap<CDeadSource, const CDeadSource&, DWORD, DWORD> CDeadSourcesMap;
	CDeadSourcesMap m_mapDeadSources;
	DWORD	m_dwLastCleanUp;
	bool	m_bGlobalList;
};