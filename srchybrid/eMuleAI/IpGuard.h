//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#pragma once

#include <WinSock2.h>
#include <atlstr.h>
#include <cstdint>
#include <vector>
#include "eMuleAI/Address.h"

enum EIpGuardMode
{
	IpGuardModeOff = 0,
	IpGuardModeBlock
};

struct SIpGuardAllowedPublicIpRange
{
	CAddress address;
	uint8_t uPrefixLength;
};

struct SIpGuardPublicIpProbeResult
{
	SIpGuardPublicIpProbeResult()
		: bAttempted(false)
		, bSucceeded(false)
		, uGeneration(0)
		, eAddressFamily(CAddress::None)
		, uPublicAddress(0)
		, dwBindInterfaceIndex(0)
	{
	}

	bool bAttempted;
	bool bSucceeded;
	uint32_t uGeneration;
	CStringA strPublicAddress;
	CAddress publicAddress;
	CAddress::EAF eAddressFamily;
	uint32_t uPublicAddress;
	CString strProviderUrl;
	CString strPurpose;
	CString strBindInterfaceName;
	CStringA strBindAddress;
	DWORD dwBindInterfaceIndex;
	CString strAttempts;
	CString strError;
};

class CIpGuard
{
public:
	static LPCTSTR GetModePreferenceText(EIpGuardMode eMode);
	static EIpGuardMode ParseModePreferenceText(CString strText);
	static bool TryParseAllowedPublicIpRanges(CString strText, std::vector<SIpGuardAllowedPublicIpRange>& ranges, CString& strError);
	static bool IsPublicIpAllowed(const CAddress& address, const std::vector<SIpGuardAllowedPublicIpRange>& ranges);
	static CString FormatAddress(const CAddress& address);
	static bool StartPublicIpProbe(HWND hNotifyWnd, UINT uNotifyMessage, uint32_t uGeneration, const CString& strPurpose, ADDRESS_FAMILY nFamily, CString& strError);
	static bool StartBoundPublicIpProbe(HWND hNotifyWnd, UINT uNotifyMessage, uint32_t uGeneration, const CString& strPurpose, ADDRESS_FAMILY nFamily, CString& strError);
};
