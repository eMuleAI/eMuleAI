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

class CServer;
class CUpDownClient;
typedef CTypedPtrList<CPtrList, CUpDownClient*> CUpDownClientPtrList;

struct CurrentPingStruct
{
	CString state;
	uint32	latency;
	uint32	lowest;
	uint32  currentLimit;
};

struct CurrentParamStruct
{
	double	dPingTolerance;
	uint32	uCurUpload; //bytes
	uint32	uMinUpload; //kilobytes
	uint32	uMaxUpload; //kilobytes
	uint32	uPingToleranceMilliseconds;
	uint32	uGoingUpDivider;
	uint32	uGoingDownDivider;
	uint32	uNumberOfPingsForAverage;
	uint32	uLowestInitialPingAllowed;
	bool	bUseMillisecondPingTolerance;
	bool	bEnabled;
};

class LastCommonRouteFinder : public CWinThread
{
public:
	LastCommonRouteFinder();
	~LastCommonRouteFinder() = default;
	LastCommonRouteFinder(const LastCommonRouteFinder&) = delete;
	LastCommonRouteFinder& operator=(const LastCommonRouteFinder&) = delete;

	void EndThread();

	bool AddHostsToCheck(CTypedPtrList<CPtrList, CServer*> &list);
	bool AddHostsToCheck(CUpDownClientPtrList &list);

	CurrentPingStruct GetCurrentPing();
	bool AcceptNewClient();

	bool SetPrefs(const CurrentParamStruct &cur);
	void InitiateFastReactionPeriod();

	uint32 GetUpload() const					{ return m_upload; }
private:
	static UINT AFX_CDECL RunProc(LPVOID pParam);
	UINT RunInternal();

	bool AddHostToCheck(uint32 ip);
	bool AddHostToCheckNoLock(uint32 ip);

	typedef CList<uint32, uint32> UInt32Clist;
	static uint32 Median(const UInt32Clist &list);

	CCriticalSection addHostLocker;
	CCriticalSection prefsLocker;
	CCriticalSection pingLocker;

	CEvent m_eventThreadEnded;
	CEvent m_eventNewTraceRouteHost;
	CEvent m_eventPrefs;

	typedef CMap<uint32, uint32, uint32, uint32> CHostsToTraceRouteMap;
	CHostsToTraceRouteMap hostsToTraceRoute;

	UInt32Clist pingDelays;

	double m_pingTolerance;
	uint64 m_pingDelaysTotal;
	uint32 m_LowestInitialPingAllowed;

	// all values in bytes
	uint32 m_minUpload;
	uint32 m_maxUpload;
	uint32 m_CurUpload;
	uint32 m_upload;

	uint32 m_iPingToleranceMilliseconds;
	uint32 m_goingUpDivider;
	uint32 m_goingDownDivider;
	uint32 m_iNumberOfPingsForAverage;

	uint32 m_pingAverage;
	uint32 m_lowestPing;
	LPCTSTR m_strState;

	volatile CHAR m_initiateFastReactionPeriod;
	volatile bool m_bRun;
	bool m_enabled;
	bool m_bUseMillisecondPingTolerance;
	bool acceptNewClient;
	bool needMoreHosts;
};