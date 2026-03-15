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
#include "emule.h"
#include "Statistics.h"
#include "Preferences.h"
#include "Opcodes.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#ifdef _DEBUG
extern _CRT_ALLOC_HOOK g_pfnPrevCrtAllocHook;
#endif

#define MAXAVERAGETIME	SEC2MS(40)

///////////////////////////////////////////////////////////////////////////////
// CStatistics

CStatistics theStats;

float	CStatistics::maxDown;
float	CStatistics::maxDownavg;
float	CStatistics::cumDownavg;
float	CStatistics::maxcumDownavg;
float	CStatistics::maxcumDown;
float	CStatistics::cumUpavg;
float	CStatistics::maxcumUpavg;
float	CStatistics::maxcumUp;
float	CStatistics::maxUp;
float	CStatistics::maxUpavg;
float	CStatistics::rateDown;
float	CStatistics::rateUp;
DWORD	CStatistics::timeTransfers;
DWORD	CStatistics::timeDownloads;
DWORD	CStatistics::timeUploads;
DWORD	CStatistics::start_timeTransfers;
DWORD	CStatistics::start_timeDownloads;
DWORD	CStatistics::start_timeUploads;
DWORD	CStatistics::time_thisTransfer;
DWORD	CStatistics::time_thisDownload;
DWORD	CStatistics::time_thisUpload;
DWORD	CStatistics::timeServerDuration;
DWORD	CStatistics::time_thisServerDuration;
uint64	CStatistics::m_nDownDatarateOverhead;
uint64	CStatistics::m_nDownDataRateMSOverhead;
uint64	CStatistics::m_nDownDataOverheadSourceExchange;
uint64	CStatistics::m_nDownDataOverheadSourceExchangePackets;
uint64	CStatistics::m_nDownDataOverheadFileRequest;
uint64	CStatistics::m_nDownDataOverheadFileRequestPackets;
uint64	CStatistics::m_nDownDataOverheadServer;
uint64	CStatistics::m_nDownDataOverheadServerPackets;
uint64	CStatistics::m_nDownDataOverheadKad;
uint64	CStatistics::m_nDownDataOverheadKadPackets;
uint64	CStatistics::m_nDownDataOverheadOther;
uint64	CStatistics::m_nDownDataOverheadOtherPackets;
uint64	CStatistics::m_nUpDatarateOverhead;
uint64	CStatistics::m_nUpDataRateMSOverhead;
uint64	CStatistics::m_nUpDataOverheadSourceExchange;
uint64	CStatistics::m_nUpDataOverheadSourceExchangePackets;
uint64	CStatistics::m_nUpDataOverheadFileRequest;
uint64	CStatistics::m_nUpDataOverheadFileRequestPackets;
uint64	CStatistics::m_nUpDataOverheadServer;
uint64	CStatistics::m_nUpDataOverheadServerPackets;
uint64	CStatistics::m_nUpDataOverheadKad;
uint64	CStatistics::m_nUpDataOverheadKadPackets;
uint64	CStatistics::m_nUpDataOverheadOther;
uint64	CStatistics::m_nUpDataOverheadOtherPackets;
uint64	CStatistics::m_sumavgDDRO;
uint64	CStatistics::m_sumavgUDRO;

float	CStatistics::m_fGlobalDone;
float	CStatistics::m_fGlobalSize;
DWORD	CStatistics::m_dwOverallStatus;

uint64	CStatistics::sessionReceivedBytes;
uint64	CStatistics::sessionSentBytes;
uint64	CStatistics::sessionSentBytesToFriend;
uint16	CStatistics::reconnects;
DWORD	CStatistics::transferStarttime;
DWORD	CStatistics::serverConnectTime;
uint32	CStatistics::filteredclients;
DWORD	CStatistics::starttime;

CStatistics::CStatistics()
{
	maxDown = 0;
	maxDownavg = 0;
	maxcumDown = 0;
	cumUpavg = 0;
	maxcumDownavg = 0;
	cumDownavg = 0;
	maxcumUpavg = 0;
	maxcumUp = 0;
	maxUp = 0;
	maxUpavg = 0;
	rateDown = 0;
	rateUp = 0;
	timeTransfers = 0;
	timeDownloads = 0;
	timeUploads = 0;
	start_timeTransfers = 0;
	start_timeDownloads = 0;
	start_timeUploads = 0;
	time_thisTransfer = 0;
	time_thisDownload = 0;
	time_thisUpload = 0;
	timeServerDuration = 0;
	time_thisServerDuration = 0;

	sessionReceivedBytes = 0;
	sessionSentBytes = 0;
	sessionSentBytesToFriend = 0;
	reconnects = 0;
	transferStarttime = 0;
	serverConnectTime = 0;
	filteredclients = 0;
	starttime = 0;

	m_fGlobalDone = 0;
	m_fGlobalSize = 0;
	m_dwOverallStatus = 0;
	m_nDownDataRateMSOverhead = 0;
	m_nDownDatarateOverhead = 0;
	m_nDownDataOverheadSourceExchange = 0;
	m_nDownDataOverheadSourceExchangePackets = 0;
	m_nDownDataOverheadFileRequest = 0;
	m_nDownDataOverheadFileRequestPackets = 0;
	m_nDownDataOverheadServer = 0;
	m_nDownDataOverheadServerPackets = 0;
	m_nDownDataOverheadKad = 0;
	m_nDownDataOverheadKadPackets = 0;
	m_nDownDataOverheadOther = 0;
	m_nDownDataOverheadOtherPackets = 0;
	m_sumavgDDRO = 0;

	m_nUpDataRateMSOverhead = 0;
	m_nUpDatarateOverhead = 0;
	m_nUpDataOverheadSourceExchange = 0;
	m_nUpDataOverheadSourceExchangePackets = 0;
	m_nUpDataOverheadFileRequest = 0;
	m_nUpDataOverheadFileRequestPackets = 0;
	m_nUpDataOverheadServer = 0;
	m_nUpDataOverheadServerPackets = 0;
	m_nUpDataOverheadKad = 0;
	m_nUpDataOverheadKadPackets = 0;
	m_nUpDataOverheadOther = 0;
	m_nUpDataOverheadOtherPackets = 0;
	m_sumavgUDRO = 0;
}

void CStatistics::Init()
{
	maxcumDown = thePrefs.GetConnMaxDownRate();
	cumUpavg = thePrefs.GetConnAvgUpRate();
	maxcumDownavg = thePrefs.GetConnMaxAvgDownRate();
	cumDownavg = thePrefs.GetConnAvgDownRate();
	maxcumUpavg = thePrefs.GetConnMaxAvgUpRate();
	maxcumUp = thePrefs.GetConnMaxUpRate();
}

// This function is going to basically calculate and save a bunch of averages.
//				I made a separate function so that it would always run instead of having
//				the averages not be calculated if the graphs are disabled (Which is bad!).
void CStatistics::UpdateConnectionStats(float uploadrate, float downloadrate)
{
	rateUp = uploadrate;
	rateDown = downloadrate;

	if (maxUp < uploadrate)
		maxUp = uploadrate;
	if (maxcumUp < maxUp) {
		maxcumUp = maxUp;
		thePrefs.SetConnMaxUpRate(maxcumUp);
	}

	if (maxDown < downloadrate)
		maxDown = downloadrate;
	if (maxcumDown < maxDown) {
		maxcumDown = maxDown;
		thePrefs.SetConnMaxDownRate(maxcumDown);
	}

	cumDownavg = GetAvgDownloadRate(AVG_TOTAL);
	if (maxcumDownavg < cumDownavg) {
		maxcumDownavg = cumDownavg;
		thePrefs.SetConnMaxAvgDownRate(maxcumDownavg);
	}

	cumUpavg = GetAvgUploadRate(AVG_TOTAL);
	if (maxcumUpavg < cumUpavg) {
		maxcumUpavg = cumUpavg;
		thePrefs.SetConnMaxAvgUpRate(maxcumUpavg);
	}

	// Transfer Times (Increment Session)
	if (uploadrate > 0 || downloadrate > 0) {
		const DWORD curTick = ::GetTickCount();
		if (start_timeTransfers)
			time_thisTransfer = (curTick - start_timeTransfers) / SEC2MS(1);
		else
			start_timeTransfers = curTick;

		if (uploadrate > 0) {
			if (start_timeUploads)
				time_thisUpload = (curTick - start_timeUploads) / SEC2MS(1);
			else
				start_timeUploads = curTick;
		}

		if (downloadrate > 0) {
			if (start_timeDownloads)
				time_thisDownload = (curTick - start_timeDownloads) / SEC2MS(1);
			else
				start_timeDownloads = curTick;
		}
	}

	if (uploadrate == 0 && downloadrate == 0 && (time_thisTransfer > 0 || start_timeTransfers > 0)) {
		timeTransfers += time_thisTransfer;
		time_thisTransfer = 0;
		start_timeTransfers = 0;
	}

	if (uploadrate == 0 && (time_thisUpload > 0 || start_timeUploads > 0)) {
		timeUploads += time_thisUpload;
		time_thisUpload = 0;
		start_timeUploads = 0;
	}

	if (downloadrate == 0 && (time_thisDownload > 0 || start_timeDownloads > 0)) {
		timeDownloads += time_thisDownload;
		time_thisDownload = 0;
		start_timeDownloads = 0;
	}

	// Server Durations
	if (theStats.serverConnectTime == 0)
		time_thisServerDuration = 0;
	else
		time_thisServerDuration = (::GetTickCount() - theStats.serverConnectTime) / SEC2MS(1);
}

void CStatistics::RecordRate()
{
	if (theStats.transferStarttime == 0)
		return;

	// Accurate data rate calculation
	DWORD curTick = ::GetTickCount();
	downrateHistory.push_front(TransferredData{ theStats.sessionReceivedBytes, curTick });
	uprateHistory.push_front(TransferredData{ theStats.sessionSentBytes, curTick });

	DWORD avg = MIN2MS(thePrefs.GetStatsAverageMinutes());
	if (curTick > avg) {
		// limit to maxmins
		curTick -= avg;
		while (curTick > downrateHistory.back().timestamp)
			downrateHistory.pop_back();
		while (curTick > uprateHistory.back().timestamp)
			uprateHistory.pop_back();
	}
}

// Changed these two functions (khaos)...
float CStatistics::GetAvgDownloadRate(int averageType)
{
	DWORD running;

	switch (averageType) {
	case AVG_SESSION:
		if (theStats.transferStarttime > 0) {
			running = (::GetTickCount() - theStats.transferStarttime) / SEC2MS(1);
			if (running >= 5)
				return theStats.sessionReceivedBytes / 1024.0f / running;
		}
		return 0.0F;

	case AVG_TOTAL:
		if (theStats.transferStarttime > 0) {
			running = (::GetTickCount() - theStats.transferStarttime) / SEC2MS(1);
			if (running >= 5)
				return (theStats.sessionReceivedBytes / 1024.0f / running + thePrefs.GetConnAvgDownRate()) / 2.0F;
		}
		return thePrefs.GetConnAvgDownRate();

	case AVG_TIME:
		if (!downrateHistory.empty()) {
			running = (downrateHistory.front().timestamp - downrateHistory.back().timestamp) / SEC2MS(1);
			if (running > 0)
				return (downrateHistory.front().datalen - downrateHistory.back().datalen) / 1024.0f / running;
		}
	default:
		return 0.0F;
	}
}

float CStatistics::GetAvgUploadRate(int averageType)
{
	DWORD running;

	switch (averageType) {
	case AVG_SESSION:
		if (theStats.transferStarttime) {
			running = (::GetTickCount() - theStats.transferStarttime) / SEC2MS(1);
			if (running >= 5)
				return theStats.sessionSentBytes / 1024.0f / running;
		}
		return 0.0F;

	case AVG_TOTAL:
		if (theStats.transferStarttime > 0) {
			running = (::GetTickCount() - theStats.transferStarttime) / SEC2MS(1);
			if (running >= 5)
				return (theStats.sessionSentBytes / 1024.0f / running + thePrefs.GetConnAvgUpRate()) / 2.0F;
		}
		return thePrefs.GetConnAvgUpRate();

	case AVG_TIME:
		if (!uprateHistory.empty()) {
			running = (uprateHistory.front().timestamp - uprateHistory.back().timestamp) / SEC2MS(1);
			if (running > 0)
				return (uprateHistory.front().datalen - uprateHistory.back().datalen) / 1024.0f / running;
		}
	default:
		return 0.0F;
	}
}

void CStatistics::CompDownDatarateOverhead()
{
	DWORD curTick = ::GetTickCount();

	m_AverageDDRO_list.push_back(TransferredData{ m_nDownDataRateMSOverhead, curTick });
	m_sumavgDDRO += m_nDownDataRateMSOverhead;
	m_nDownDataRateMSOverhead = 0;

	while (curTick > m_AverageDDRO_list.front().timestamp + MAXAVERAGETIME) {
		m_sumavgDDRO -= m_AverageDDRO_list.front().datalen;
		m_AverageDDRO_list.pop_front();
	}

	if (m_AverageDDRO_list.size() > 10) {
		const TransferredData &head = m_AverageDDRO_list.front();
		if (curTick > head.timestamp) {
			m_nDownDatarateOverhead = SEC2MS(m_sumavgDDRO - head.datalen) / (curTick - head.timestamp);
			return;
		}
	}
	m_nDownDatarateOverhead = 0;
}

void CStatistics::CompUpDatarateOverhead()
{
	DWORD curTick = ::GetTickCount();

	m_AverageUDRO_list.push_back(TransferredData{ m_nUpDataRateMSOverhead, curTick });
	m_sumavgUDRO += m_nUpDataRateMSOverhead;
	m_nUpDataRateMSOverhead = 0;

	while (curTick > m_AverageUDRO_list.front().timestamp + MAXAVERAGETIME) {
		m_sumavgUDRO -= m_AverageUDRO_list.front().datalen;
		m_AverageUDRO_list.pop_front();
	}

	if (m_AverageUDRO_list.size() > 10) {
		const TransferredData &head = m_AverageUDRO_list.front();
		if (curTick > head.timestamp) {
			m_nUpDatarateOverhead = SEC2MS(m_sumavgUDRO - head.datalen) / (curTick - head.timestamp);
			return;
		}
	}
	m_nUpDatarateOverhead = 0;
}

void CStatistics::ResetDownDatarateOverhead()
{
	m_AverageDDRO_list.clear();
	m_nDownDataRateMSOverhead = 0;
	m_nDownDatarateOverhead = 0;
	m_sumavgDDRO = 0;
}

void CStatistics::ResetUpDatarateOverhead()
{
	m_AverageUDRO_list.clear();
	m_nUpDataRateMSOverhead = 0;
	m_nUpDatarateOverhead = 0;
	m_sumavgUDRO = 0;
}

#ifdef USE_MEM_STATS

#ifdef _DEBUG
#error "Does not work when _DEBUG defined!"
#endif

#ifdef _AFXDLL
#error "Not supported for _AFXDLL!"
#endif

#if _MFC_VER!=0x0710
#error "Not verified for this _MFC_VER!"
#endif

ULONGLONG g_aAllocStats[ALLOC_SLOTS];

/////////////////////////////////////////////////////////////////////////////
// Non-diagnostic memory routines

int AFX_CDECL AfxNewHandler(size_t /* nSize */)
{
	AfxThrowMemoryException();
	return 0;
}

#pragma warning(push)
#pragma warning(disable: 4273)

_PNH _afxNewHandler = &AfxNewHandler;

_PNH AFXAPI AfxGetNewHandler()
{
	return _afxNewHandler;
}

_PNH AFXAPI AfxSetNewHandler(_PNH pfnNewHandler)
{
	_PNH pfnOldHandler = _afxNewHandler;
	_afxNewHandler = pfnNewHandler;
	return pfnOldHandler;
}

AFX_STATIC_DATA const _PNH _pfnUninitialized = (_PNH)-1;

inline int log2(unsigned x)
{
	int log = 0;
	while (x >>= 1)
		++log;
	return log;
}

void* my_new(size_t n)
{
	int i;
	if (n == 0)
		i = 0;
	else {
		i = log2(n) + 1;
		if (i >= ALLOC_SLOTS)
			i = ALLOC_SLOTS - 1;
	}
	++g_aAllocStats[i];

	void *pResult;
	for (;;) {
		pResult = malloc(n);
		if (pResult != NULL)
			return pResult;

		if (_afxNewHandler == NULL || (*_afxNewHandler)(n) == 0)
			break;
	}
	return pResult;
}

void* __cdecl operator new(size_t nSize)
{
	return my_new(nSize);
}

void* __cdecl operator new[](size_t nSize)
{
	return ::operator new(nSize);
}

void __cdecl operator delete(void *p)
{
	free(p);
}

void __cdecl operator delete[](void *p)
{
	::operator delete(p);
}

#pragma warning(pop)
#endif
