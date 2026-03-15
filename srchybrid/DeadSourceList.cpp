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
#include "StdAfx.h"
#include "opcodes.h"
#include "deadsourcelist.h"
#include "updownclient.h"
#include "Log.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define	CLEANUPTIME		MIN2MS(60)

#define BLOCKTIME		(MIN2MS(m_bGlobalList ? 15 : 45))
#define BLOCKTIMEFW		(MIN2MS(m_bGlobalList ? 30 : 45))

///////////////////////////////////////////////////////////////////////////////////////
//// CDeadSource

CDeadSource::CDeadSource()
	: m_aucHash()
	, m_dwServerIP()
	, m_dwID()
	, m_nPort()
	, m_nKadPort()
{
}

CDeadSource::CDeadSource(const CUpDownClient &client)
{
	if (!client.HasLowID() || client.GetServerIP() != 0) {
		md4clr(m_aucHash);
		m_dwServerIP = client.GetServerIP();
		m_dwID = client.GetUserIDHybrid();
		m_nPort = client.GetUserPort();
		m_nKadPort = (client.HasLowID() ? 0 : client.GetKadPort());
	} else {
		if (client.HasValidServingBuddyID() || client.SupportsDirectUDPCallback())
			md4cpy(m_aucHash, client.GetUserHash());
		else
			md4clr(m_aucHash);
		m_dwServerIP = 0;
		m_dwID = 0;
		m_nPort = 0;
		m_nKadPort = 0;
	}
}

bool operator==(const CDeadSource &ds1, const CDeadSource &ds2)
{
	return (
		// lowid ed2k and highid kad + ed2k check
		((ds1.m_dwID != 0 && ds1.m_dwID == ds2.m_dwID) && ((ds1.m_nPort != 0 && ds1.m_nPort == ds2.m_nPort) || (ds1.m_nKadPort != 0 && ds1.m_nKadPort == ds2.m_nKadPort)) && (ds1.m_dwServerIP == ds2.m_dwServerIP || !::IsLowID(ds1.m_dwID)) )
		// lowid kad check
		|| (::IsLowID(ds1.m_dwID) && !isnulmd4(ds1.m_aucHash) && md4equ(ds1.m_aucHash, ds2.m_aucHash)) );
}

CDeadSource& CDeadSource::operator=(const CDeadSource &ds)
{
	md4cpy(m_aucHash, ds.m_aucHash);
	m_dwServerIP = ds.m_dwServerIP;
	m_dwID = ds.m_dwID;
	m_nPort = ds.m_nPort;
	m_nKadPort = ds.m_nKadPort;
	return *this;
}

///////////////////////////////////////////////////////////////////////////////////////
//// CDeadSourceList

CDeadSourceList::CDeadSourceList()
	: m_dwLastCleanUp()
	, m_bGlobalList()
{
}

void CDeadSourceList::Init(bool bGlobalList)
{
	m_mapDeadSources.InitHashTable(bGlobalList ? 3001 : 503);
	m_bGlobalList = bGlobalList;
	m_dwLastCleanUp = ::GetTickCount();
}

bool CDeadSourceList::IsDeadSource(const CUpDownClient &client) const
{
	const CDeadSourcesMap::CPair *pair = m_mapDeadSources.PLookup(CDeadSource(client));
	return (pair && ::GetTickCount() < pair->value);
}

void CDeadSourceList::AddDeadSource(const CUpDownClient &client)
{
	DWORD curTick = ::GetTickCount();
	m_mapDeadSources[CDeadSource(client)] = curTick + (client.HasLowID() ? BLOCKTIMEFW : BLOCKTIME);

	if (curTick >= m_dwLastCleanUp + CLEANUPTIME)
		CleanUp();
}

void CDeadSourceList::RemoveDeadSource(const CUpDownClient &client)
{
	m_mapDeadSources.RemoveKey(CDeadSource(client));
}

void CDeadSourceList::CleanUp()
{
	m_dwLastCleanUp = ::GetTickCount();

	CDeadSource dsKey;
	for (POSITION pos = m_mapDeadSources.GetStartPosition(); pos != NULL;) {
		DWORD dwExpTime;
		m_mapDeadSources.GetNextAssoc(pos, dsKey, dwExpTime);
		if (m_dwLastCleanUp >= dwExpTime)
			m_mapDeadSources.RemoveKey(dsKey);
	}
}