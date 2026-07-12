//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#pragma once

#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <vector>
#include "Address.h"

struct SNetBindAddress
{
	CString strAddress;
	CAddress::EAF eFamily;
};

struct SNetBindInterface
{
	SNetBindInterface()
		: dwIpv4IfIndex(0)
		, dwIpv6IfIndex(0)
	{
	}

	CString strId;
	CString strName;
	CString strDisplayName;
	DWORD dwIpv4IfIndex;
	DWORD dwIpv6IfIndex;
	std::vector<SNetBindAddress> addresses;
};

enum ENetBindResolveResult
{
	NBR_Default = 0,
	NBR_Resolved,
	NBR_InterfaceNotFound,
	NBR_InterfaceNameAmbiguous,
	NBR_InterfaceHasNoAddress,
	NBR_AddressNotFoundOnInterface,
	NBR_AddressNotFound,
	NBR_InvalidAddress
};

struct SNetBindResolution
{
	SNetBindResolution()
		: eResult(NBR_Default)
		, eResolvedFamily(CAddress::None)
		, dwIpv4IfIndex(0)
		, dwIpv6IfIndex(0)
	{
	}

	ENetBindResolveResult eResult;
	CString strInterfaceId;
	CString strInterfaceName;
	CString strResolvedAddress;
	CAddress::EAF eResolvedFamily;
	DWORD dwIpv4IfIndex;
	DWORD dwIpv6IfIndex;
};

class CNetBind
{
public:
	static std::vector<SNetBindInterface> GetInterfaces();
	static bool TryParseAddress(const CString& strAddress, CAddress& address);
	static ENetBindResolveResult Resolve(const CString& strInterfaceId, const CString& strInterfaceName, const CString& strConfiguredAddress, SNetBindResolution& resolution);
	static bool HasExplicitSelection(const CString& strInterfaceId, const CString& strInterfaceName, const CString& strConfiguredAddress);
	static bool InterfaceMatchesAdapter(const CString& strInterfaceId, const CString& strInterfaceName, const IP_ADAPTER_ADDRESSES* pAdapter);
	static bool ApplyIpv4UnicastInterfaceOption(SOCKET hSocket, ADDRESS_FAMILY nFamily, bool bHasExplicitBindInterface, bool bBindAddressResolved, DWORD dwIpv4IfIndex, int* pnError);
	static bool ApplyIpv6UnicastInterfaceOption(SOCKET hSocket, ADDRESS_FAMILY nFamily, bool bHasExplicitBindInterface, bool bBindAddressResolved, DWORD dwIpv6IfIndex, int* pnError);
};
