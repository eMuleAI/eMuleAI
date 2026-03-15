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
#include "kademlia/utils/UInt128.h"

struct SIPFilter
{
	SIPFilter(Kademlia::CUInt128 newStart, Kademlia::CUInt128 newEnd, uint32 newLevel, const CStringA &newDesc)
		: start(newStart)
		, end(newEnd)
		, level(newLevel)
		, hits()
		, desc(newDesc)
	{
	}

	Kademlia::CUInt128	start;
	Kademlia::CUInt128	end;
	uint32				level;
	uint32				hits;
	CStringA			desc;
};

#define	DFLT_IPFILTER_FILENAME	_T("ipfilter.dat")
#define	DFLT_STATIC_IPFILTER_FILENAME	_T("ipfilter_static.dat")
#define	DFLT_WHITE_IPFILTER_FILENAME	_T("ipfilter_white.dat")


// 'CArray' would give us more cache hits, but would also be slow in array element creation
// (because of the implicit ctor in 'SIPFilter'
//typedef CArray<SIPFilter, SIPFilter> CIPFilterArray;
typedef CTypedPtrArray<CPtrArray, SIPFilter*> CIPFilterArray;

class CIPFilter
{
public:
	CIPFilter();
	~CIPFilter();

	static CString GetDefaultFilePath();

	void AddIPRange(const Kademlia::CUInt128 &start, const Kademlia::CUInt128 &end, const uint32 level, const CStringA &rstrDesc)
	{
		m_iplist.Add(new SIPFilter(start, end, level, rstrDesc));
	}
	void RemoveAllIPFilters();
	bool RemoveIPFilter(const SIPFilter *pFilter);
	void SetModified(const bool bModified = true)
	{
		m_bModified = bModified;
	}

	void  LoadAllP2PFiles();
	const INT_PTR LoadFromDefaultFile(const bool bShowResponse = true);
	void SaveToDefaultFile();
	const INT_PTR AddFromFile(const LPCTSTR pszFilePath, const bool bShowResponse = true);

	const bool IsFiltered(uint32 dwIP) /*const*/;
	const bool IsFiltered(const CAddress &IP) /*const*/;
	const CString GetLastHit() const;
	const CIPFilterArray& GetIPFilter() const;

private:
	const SIPFilter *m_pLastHit;
	CIPFilterArray m_iplist;
	bool m_bModified;

	static bool ParseIPFilterDatLine(const CStringA &sbuffer, Kademlia::CUInt128 &ip1, Kademlia::CUInt128 &ip2, uint32 &level, CStringA &desc);
	static bool ParsePeerGuardianLine(const CStringA &sbuffer, Kademlia::CUInt128 &ip1, Kademlia::CUInt128 &ip2, uint32 &level, CStringA &desc);

	void AddFromFileStatic(const LPCTSTR pszFilePath);
	const CString GetDefaultStaticFilePath() const;

	void AddFromFileWhite(const LPCTSTR pszFilePath);
	const CString GetDefaultWhiteFilePath() const;

	void AddIPRangeWhite(const Kademlia::CUInt128 &start, const Kademlia::CUInt128 &end, const uint32 level, const CStringA& rstrDesc) {
		m_iplist_White.Add(new SIPFilter(start, end, level, rstrDesc));
	}
	CIPFilterArray m_iplist_White;
};