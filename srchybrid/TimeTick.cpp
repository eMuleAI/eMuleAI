// TimeTick.cpp : implementation of the CTimeTick class
//
/////////////////////////////////////////////////////////////////////////////
//
// Copyright © 2001, Stefan Belopotocan, http://welcome.to/BeloSoft
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "TimeTick.h"
#include "opcodes.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


//////////////////////////////////////////////////////////////////////
// CTimeTick

__int64 CTimeTick::m_nPerformanceFrequency = CTimeTick::GetPerformanceFrequency();

CTimeTick::CTimeTick()
	: m_nTime()
{
}

void CTimeTick::Start()
{
	if (m_nPerformanceFrequency)
		QueryPerformanceCounter(&m_nTime);
}

float CTimeTick::Tick()
{
	if (!m_nPerformanceFrequency)
		return 0.0f;

	LARGE_INTEGER nTime = m_nTime;
	QueryPerformanceCounter(&m_nTime);
	return GetTimeInMilliSeconds(m_nTime.QuadPart - nTime.QuadPart);
}

__int64 CTimeTick::GetPerformanceFrequency()
{
	LARGE_INTEGER nPerformanceFrequency;
	return QueryPerformanceFrequency(&nPerformanceFrequency) ? nPerformanceFrequency.QuadPart : 0;
}

float CTimeTick::GetTimeInMilliSeconds(__int64 nTime)
{
	return SEC2MS(nTime) / (float)m_nPerformanceFrequency;
}