//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#include "stdafx.h"
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include "NetBind.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	bool AddressToString(const SOCKADDR* pSockAddr, int iSockAddrLen, CString& strAddress, CAddress::EAF& eFamily)
	{
		strAddress.Empty();
		eFamily = CAddress::None;
		if (pSockAddr == NULL)
			return false;

		if (pSockAddr->sa_family == AF_INET)
			eFamily = CAddress::IPv4;
		else if (pSockAddr->sa_family == AF_INET6)
			eFamily = CAddress::IPv6;
		else
			return false;

		TCHAR szAddress[128] = { 0 };
		DWORD dwAddressLen = _countof(szAddress);
		if (WSAAddressToString(const_cast<LPSOCKADDR>(pSockAddr), static_cast<DWORD>(iSockAddrLen), NULL, szAddress, &dwAddressLen) != 0)
			return false;

		strAddress = szAddress;
		strAddress.Trim();
		if (strAddress.Find(_T('%')) >= 0)
			return false;
		return !strAddress.IsEmpty();
	}

	void AddUniqueAddress(std::vector<SNetBindAddress>& addresses, const CString& strAddress, CAddress::EAF eFamily)
	{
		if (strAddress.IsEmpty())
			return;
		for (std::vector<SNetBindAddress>::const_iterator it = addresses.begin(); it != addresses.end(); ++it) {
			if (it->eFamily == eFamily && it->strAddress.CompareNoCase(strAddress) == 0)
				return;
		}
		SNetBindAddress address;
		address.strAddress = strAddress;
		address.eFamily = eFamily;
		addresses.push_back(address);
	}


	const SNetBindAddress* SelectDefaultBindAddress(const SNetBindInterface& iface)
	{
		for (std::vector<SNetBindAddress>::const_iterator itAddress = iface.addresses.begin(); itAddress != iface.addresses.end(); ++itAddress) {
			if (itAddress->eFamily == CAddress::IPv4)
				return &(*itAddress);
		}
		return iface.addresses.empty() ? NULL : &iface.addresses.front();
	}

	const SNetBindInterface* FindInterface(const std::vector<SNetBindInterface>& interfaces, const CString& strInterfaceId, const CString& strInterfaceName, ENetBindResolveResult& eResult)
	{
		CString strWantedId(strInterfaceId);
		strWantedId.Trim();
		CString strWantedName(strInterfaceName);
		strWantedName.Trim();

		if (!strWantedId.IsEmpty()) {
			for (std::vector<SNetBindInterface>::const_iterator it = interfaces.begin(); it != interfaces.end(); ++it) {
				if (it->strId.CompareNoCase(strWantedId) == 0) {
					eResult = NBR_Resolved;
					return &(*it);
				}
			}
			eResult = NBR_InterfaceNotFound;
			return NULL;
		}

		if (strWantedName.IsEmpty()) {
			eResult = NBR_Default;
			return NULL;
		}

		const SNetBindInterface* pMatchedInterface = NULL;
		for (std::vector<SNetBindInterface>::const_iterator it = interfaces.begin(); it != interfaces.end(); ++it) {
			if (it->strName.CompareNoCase(strWantedName) != 0)
				continue;
			if (pMatchedInterface != NULL) {
				eResult = NBR_InterfaceNameAmbiguous;
				return NULL;
			}
			pMatchedInterface = &(*it);
		}

		if (pMatchedInterface == NULL)
			eResult = NBR_InterfaceNotFound;
		else
			eResult = NBR_Resolved;
		return pMatchedInterface;
	}
}

std::vector<SNetBindInterface> CNetBind::GetInterfaces()
{
	std::vector<SNetBindInterface> interfaces;

	ULONG ulOutBufLen = 16 * 1024;
	std::vector<BYTE> buffer(ulOutBufLen);
	const ULONG ulFlags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
	DWORD dwResult = ERROR_BUFFER_OVERFLOW;

	for (int iRetry = 0; iRetry < 3 && dwResult == ERROR_BUFFER_OVERFLOW; ++iRetry) {
		dwResult = GetAdaptersAddresses(AF_UNSPEC, ulFlags, NULL, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &ulOutBufLen);
		if (dwResult == ERROR_BUFFER_OVERFLOW)
			buffer.resize(ulOutBufLen);
	}

	if (dwResult != NO_ERROR)
		return interfaces;

	for (const IP_ADAPTER_ADDRESSES* pAdapter = reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(buffer.data()); pAdapter != NULL; pAdapter = pAdapter->Next) {
		if (pAdapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK || pAdapter->OperStatus != IfOperStatusUp)
			continue;

		SNetBindInterface iface;
		iface.strId = CString(CA2T(pAdapter->AdapterName));
		iface.strName = pAdapter->FriendlyName != NULL ? CString(CW2T(pAdapter->FriendlyName)) : CString();
		iface.dwIpv4IfIndex = pAdapter->IfIndex;
		iface.dwIpv6IfIndex = pAdapter->Ipv6IfIndex;
		iface.strName.Trim();

		for (const IP_ADAPTER_UNICAST_ADDRESS* pAddress = pAdapter->FirstUnicastAddress; pAddress != NULL; pAddress = pAddress->Next) {
			CString strAddress;
			CAddress::EAF eFamily = CAddress::None;
			if (AddressToString(pAddress->Address.lpSockaddr, pAddress->Address.iSockaddrLength, strAddress, eFamily))
				AddUniqueAddress(iface.addresses, strAddress, eFamily);
		}

		iface.strDisplayName = iface.strName.IsEmpty() ? iface.strId : iface.strName;
		if (!iface.addresses.empty())
			iface.strDisplayName.AppendFormat(_T(" (%s)"), (LPCTSTR)iface.addresses.front().strAddress);
		interfaces.push_back(iface);
	}

	return interfaces;
}

bool CNetBind::TryParseAddress(const CString& strAddress, CAddress& address)
{
	CString strTrimmed(strAddress);
	strTrimmed.Trim();
	if (strTrimmed.IsEmpty())
		return false;

	return address.FromString(strTrimmed, false);
}

ENetBindResolveResult CNetBind::Resolve(const CString& strInterfaceId, const CString& strInterfaceName, const CString& strConfiguredAddress, SNetBindResolution& resolution)
{
	resolution.eResult = NBR_Default;
	resolution.strInterfaceId.Empty();
	resolution.strInterfaceName.Empty();
	resolution.strResolvedAddress.Empty();
	resolution.eResolvedFamily = CAddress::None;
	resolution.dwIpv4IfIndex = 0;
	resolution.dwIpv6IfIndex = 0;

	CString strWantedAddress(strConfiguredAddress);
	strWantedAddress.Trim();
	CString strWantedInterfaceId(strInterfaceId);
	strWantedInterfaceId.Trim();
	CString strWantedInterfaceName(strInterfaceName);
	strWantedInterfaceName.Trim();

	if (strWantedInterfaceId.IsEmpty() && strWantedInterfaceName.IsEmpty() && strWantedAddress.IsEmpty())
		return NBR_Default;

	CAddress wantedAddress;
	CAddress::EAF eWantedFamily = CAddress::None;
	if (!strWantedAddress.IsEmpty()) {
		if (!TryParseAddress(strWantedAddress, wantedAddress)) {
			resolution.eResult = NBR_InvalidAddress;
			return resolution.eResult;
		}
		eWantedFamily = wantedAddress.GetType();
	}

	const std::vector<SNetBindInterface> interfaces = GetInterfaces();
	ENetBindResolveResult eInterfaceResult = NBR_Default;
	const SNetBindInterface* pInterface = FindInterface(interfaces, strWantedInterfaceId, strWantedInterfaceName, eInterfaceResult);
	if (!strWantedInterfaceId.IsEmpty() || !strWantedInterfaceName.IsEmpty()) {
		if (pInterface == NULL) {
			resolution.eResult = eInterfaceResult;
			return resolution.eResult;
		}
		resolution.strInterfaceId = pInterface->strId;
		resolution.strInterfaceName = pInterface->strName;
		resolution.dwIpv4IfIndex = pInterface->dwIpv4IfIndex;
		resolution.dwIpv6IfIndex = pInterface->dwIpv6IfIndex;
		if (pInterface->addresses.empty()) {
			resolution.eResult = NBR_InterfaceHasNoAddress;
			return resolution.eResult;
		}

		if (strWantedAddress.IsEmpty()) {
			const SNetBindAddress* pBindAddress = SelectDefaultBindAddress(*pInterface);
			if (pBindAddress != NULL) {
				resolution.strResolvedAddress = pBindAddress->strAddress;
				resolution.eResolvedFamily = pBindAddress->eFamily;
			}
			resolution.eResult = NBR_Resolved;
			return resolution.eResult;
		}

		for (std::vector<SNetBindAddress>::const_iterator itAddress = pInterface->addresses.begin(); itAddress != pInterface->addresses.end(); ++itAddress) {
			CAddress currentAddress;
			if (itAddress->eFamily == eWantedFamily && TryParseAddress(itAddress->strAddress, currentAddress) && currentAddress == wantedAddress) {
				resolution.strResolvedAddress = itAddress->strAddress;
				resolution.eResolvedFamily = itAddress->eFamily;
				resolution.eResult = NBR_Resolved;
				return resolution.eResult;
			}
		}

		resolution.eResult = NBR_AddressNotFoundOnInterface;
		return resolution.eResult;
	}

	if (!strWantedAddress.IsEmpty()) {
		for (std::vector<SNetBindInterface>::const_iterator it = interfaces.begin(); it != interfaces.end(); ++it) {
			for (std::vector<SNetBindAddress>::const_iterator itAddress = it->addresses.begin(); itAddress != it->addresses.end(); ++itAddress) {
				CAddress currentAddress;
				if (itAddress->eFamily == eWantedFamily && TryParseAddress(itAddress->strAddress, currentAddress) && currentAddress == wantedAddress) {
					resolution.strInterfaceId = it->strId;
					resolution.strInterfaceName = it->strName;
					resolution.dwIpv4IfIndex = it->dwIpv4IfIndex;
					resolution.dwIpv6IfIndex = it->dwIpv6IfIndex;
					resolution.strResolvedAddress = itAddress->strAddress;
					resolution.eResolvedFamily = itAddress->eFamily;
					resolution.eResult = NBR_Resolved;
					return resolution.eResult;
				}
			}
		}

		resolution.eResult = NBR_AddressNotFound;
		return resolution.eResult;
	}

	resolution.eResult = NBR_Default;
	return resolution.eResult;
}

bool CNetBind::HasExplicitSelection(const CString& strInterfaceId, const CString& strInterfaceName, const CString& strConfiguredAddress)
{
	CString strId(strInterfaceId);
	strId.Trim();
	CString strName(strInterfaceName);
	strName.Trim();
	CString strAddress(strConfiguredAddress);
	strAddress.Trim();
	return !strId.IsEmpty() || !strName.IsEmpty() || !strAddress.IsEmpty();
}

bool CNetBind::InterfaceMatchesAdapter(const CString& strInterfaceId, const CString& strInterfaceName, const IP_ADAPTER_ADDRESSES* pAdapter)
{
	if (pAdapter == NULL)
		return false;

	CString strWantedId(strInterfaceId);
	strWantedId.Trim();
	if (!strWantedId.IsEmpty())
		return strWantedId.CompareNoCase(CString(CA2T(pAdapter->AdapterName))) == 0;

	CString strWantedName(strInterfaceName);
	strWantedName.Trim();
	if (strWantedName.IsEmpty())
		return true;

	CString strAdapterName(pAdapter->FriendlyName != NULL ? CString(CW2T(pAdapter->FriendlyName)) : CString());
	strAdapterName.Trim();
	return strWantedName.CompareNoCase(strAdapterName) == 0;
}


bool CNetBind::ApplyIpv4UnicastInterfaceOption(SOCKET hSocket, ADDRESS_FAMILY nFamily, bool bHasExplicitBindInterface, bool bBindAddressResolved, DWORD dwIpv4IfIndex, int* pnError)
{
	if (pnError != NULL)
		*pnError = 0;
	if (!bHasExplicitBindInterface || !bBindAddressResolved || dwIpv4IfIndex == 0)
		return true;
	if (nFamily != AF_INET) {
		if (pnError != NULL)
			*pnError = WSAEAFNOSUPPORT;
		return false;
	}

	const DWORD dwNetworkOrderIfIndex = htonl(dwIpv4IfIndex);
	if (setsockopt(hSocket, IPPROTO_IP, IP_UNICAST_IF, reinterpret_cast<const char*>(&dwNetworkOrderIfIndex), sizeof dwNetworkOrderIfIndex) == 0)
		return true;

	if (pnError != NULL)
		*pnError = WSAGetLastError();
	return false;
}


bool CNetBind::ApplyIpv6UnicastInterfaceOption(SOCKET hSocket, ADDRESS_FAMILY nFamily, bool bHasExplicitBindInterface, bool bBindAddressResolved, DWORD dwIpv6IfIndex, int* pnError)
{
	if (pnError != NULL)
		*pnError = 0;
	if (!bHasExplicitBindInterface || !bBindAddressResolved || dwIpv6IfIndex == 0)
		return true;
	if (nFamily != AF_INET6) {
		if (pnError != NULL)
			*pnError = WSAEAFNOSUPPORT;
		return false;
	}

	const DWORD dwIfIndex = dwIpv6IfIndex;
	if (setsockopt(hSocket, IPPROTO_IPV6, IPV6_UNICAST_IF, reinterpret_cast<const char*>(&dwIfIndex), sizeof dwIfIndex) == 0)
		return true;

	if (pnError != NULL)
		*pnError = WSAGetLastError();
	return false;
}
