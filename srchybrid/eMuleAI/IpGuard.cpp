//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#include "stdafx.h"
#include "IpGuard.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <ws2tcpip.h>
#include "eMuleAI/NetBind.h"
#include "Log.h"
#include "Preferences.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace
{
	constexpr int kProbeTimeoutMs = 5000;
	constexpr size_t kMaxProbeResponseBytes = 8192;

	struct SPublicIpProbeProvider
	{
		ADDRESS_FAMILY nFamily;
		const char* pszUrl;
		const char* pszHost;
		const char* pszPath;
	};

	struct SProbeContext
	{
		CString strBindInterfaceName;
		CStringA strBindAddress;
		CAddress::EAF eBindFamily = CAddress::None;
		DWORD dwIpv4IfIndex = 0;
		DWORD dwIpv6IfIndex = 0;
	};

	struct SAsyncProbeState
	{
		std::atomic<long> nRefs{1};
		std::atomic<long> nRemaining{0};
		std::atomic<bool> bCompleted{false};
		std::mutex mutex;
		HWND hNotifyWnd = NULL;
		UINT uNotifyMessage = 0;
		uint32_t uGeneration = 0;
		CString strPurpose;
		SProbeContext context;
		CString strAttempts;
	};

	struct SProviderProbeContext
	{
		SAsyncProbeState* pState = NULL;
		SPublicIpProbeProvider provider = {};
	};

	class CSocketHandle
	{
	public:
		CSocketHandle() noexcept
			: m_hSocket(INVALID_SOCKET)
		{
		}

		CSocketHandle(const CSocketHandle&) = delete;
		CSocketHandle& operator=(const CSocketHandle&) = delete;

		~CSocketHandle()
		{
			Reset();
		}

		explicit operator bool() const noexcept
		{
			return m_hSocket != INVALID_SOCKET;
		}

		SOCKET Get() const noexcept
		{
			return m_hSocket;
		}

		void Reset(SOCKET hSocket = INVALID_SOCKET) noexcept
		{
			if (m_hSocket != INVALID_SOCKET && m_hSocket != hSocket)
				closesocket(m_hSocket);
			m_hSocket = hSocket;
		}

	private:
		SOCKET m_hSocket;
	};

	class CAddrInfoHandle
	{
	public:
		CAddrInfoHandle() noexcept
			: m_pAddressInfo(NULL)
		{
		}

		CAddrInfoHandle(const CAddrInfoHandle&) = delete;
		CAddrInfoHandle& operator=(const CAddrInfoHandle&) = delete;

		~CAddrInfoHandle()
		{
			Reset();
		}

		addrinfo* Get() const noexcept
		{
			return m_pAddressInfo;
		}

		addrinfo** Out() noexcept
		{
			Reset();
			return &m_pAddressInfo;
		}

		void Reset(addrinfo* pAddressInfo = NULL) noexcept
		{
			if (m_pAddressInfo != NULL && m_pAddressInfo != pAddressInfo)
				freeaddrinfo(m_pAddressInfo);
			m_pAddressInfo = pAddressInfo;
		}

	private:
		addrinfo* m_pAddressInfo;
	};

	const SPublicIpProbeProvider* GetPublicIpProbeProviders(size_t& nCount)
	{
		static const SPublicIpProbeProvider kProviders[] = {
			{AF_INET, "http://api.ipify.org/", "api.ipify.org", "/"},
			{AF_INET, "http://ipv4.icanhazip.com/", "ipv4.icanhazip.com", "/"},
			{AF_INET, "http://checkip.amazonaws.com/", "checkip.amazonaws.com", "/"},
			{AF_INET, "http://v4.ident.me/", "v4.ident.me", "/"},
			{AF_INET, "http://ipecho.net/plain", "ipecho.net", "/plain"},
			{AF_INET6, "http://api6.ipify.org/", "api6.ipify.org", "/"},
			{AF_INET6, "http://ipv6.icanhazip.com/", "ipv6.icanhazip.com", "/"},
			{AF_INET6, "http://v6.ident.me/", "v6.ident.me", "/"},
		};
		nCount = _countof(kProviders);
		return kProviders;
	}

	bool TryParseUnsignedDecimal(CString strText, unsigned& uValue)
	{
		strText.Trim();
		if (strText.IsEmpty())
			return false;

		unsigned uParsed = 0;
		for (int i = 0; i < strText.GetLength(); ++i) {
			const TCHAR ch = strText[i];
			if (ch < _T('0') || ch > _T('9'))
				return false;

			const unsigned uDigit = static_cast<unsigned>(ch - _T('0'));
			if (uParsed > (static_cast<unsigned>(-1) - uDigit) / 10u)
				return false;
			uParsed = uParsed * 10u + uDigit;
		}

		uValue = uParsed;
		return true;
	}

	bool PrefixEquals(const CAddress& left, const CAddress& right, uint8_t uPrefixLength)
	{
		if (left.GetType() != right.GetType())
			return false;
		const size_t nBytes = left.GetSize();
		const uint8_t* pLeft = left.Data();
		const uint8_t* pRight = right.Data();
		const size_t nFullBytes = uPrefixLength / 8;
		const uint8_t uRemainingBits = uPrefixLength % 8;
		if (nFullBytes > nBytes)
			return false;
		if (nFullBytes > 0 && memcmp(pLeft, pRight, nFullBytes) != 0)
			return false;
		if (uRemainingBits == 0)
			return true;
		if (nFullBytes >= nBytes)
			return false;
		const uint8_t uMask = static_cast<uint8_t>(0xffu << (8u - uRemainingBits));
		return (pLeft[nFullBytes] & uMask) == (pRight[nFullBytes] & uMask);
	}

	bool RangesOverlap(const CAddress& firstBase, uint8_t uFirstPrefix, const CAddress& secondBase, uint8_t uSecondPrefix)
	{
		if (firstBase.GetType() != secondBase.GetType())
			return false;
		const uint8_t uSharedPrefix = uFirstPrefix < uSecondPrefix ? uFirstPrefix : uSecondPrefix;
		return PrefixEquals(firstBase, secondBase, uSharedPrefix);
	}

	bool TryParseAddressLiteral(const CString& strText, CAddress& address)
	{
		CString strAddress(strText);
		strAddress.Trim();
		if (strAddress.IsEmpty())
			return false;
		if (strAddress.Find(_T('.')) < 0 && strAddress.Find(_T(':')) < 0)
			return false;
		return address.FromString(strAddress, false);
	}

	bool TryMakeAddress(LPCTSTR pszText, CAddress& address)
	{
		return TryParseAddressLiteral(CString(pszText), address);
	}

	bool IsPublicIpRangeOnly(const CAddress& address, uint8_t uPrefixLength)
	{
		struct SReservedRange
		{
			LPCTSTR pszAddress;
			uint8_t uPrefix;
		};
		static const SReservedRange kReservedIpv4Ranges[] = {
			{_T("0.0.0.0"), 8}, {_T("10.0.0.0"), 8}, {_T("100.64.0.0"), 10}, {_T("127.0.0.0"), 8},
			{_T("169.254.0.0"), 16}, {_T("172.16.0.0"), 12}, {_T("192.0.0.0"), 24}, {_T("192.0.2.0"), 24},
			{_T("192.88.99.0"), 24}, {_T("192.168.0.0"), 16}, {_T("198.18.0.0"), 15}, {_T("198.51.100.0"), 24},
			{_T("203.0.113.0"), 24}, {_T("224.0.0.0"), 4}, {_T("233.252.0.0"), 24}, {_T("255.255.255.255"), 32},
		};
		static const SReservedRange kReservedIpv6Ranges[] = {
			{_T("::"), 128}, {_T("::1"), 128}, {_T("fe80::"), 10}, {_T("fc00::"), 7}, {_T("::ffff:0:0"), 96},
			{_T("64:ff9b::"), 96}, {_T("64:ff9b:1::"), 48}, {_T("100::"), 64}, {_T("2001::"), 32},
			{_T("2001:20::"), 28}, {_T("2001:db8::"), 32}, {_T("2002::"), 16}, {_T("5f00::"), 16}, {_T("ff00::"), 8},
		};

		const SReservedRange* pRanges = address.GetType() == CAddress::IPv6 ? kReservedIpv6Ranges : kReservedIpv4Ranges;
		const size_t nCount = address.GetType() == CAddress::IPv6 ? _countof(kReservedIpv6Ranges) : _countof(kReservedIpv4Ranges);
		for (size_t i = 0; i < nCount; ++i) {
			CAddress reserved;
			if (TryMakeAddress(pRanges[i].pszAddress, reserved) && RangesOverlap(address, uPrefixLength, reserved, pRanges[i].uPrefix))
				return false;
		}
		return true;
	}

	bool TryParseAllowedPublicIpRange(CString strText, SIpGuardAllowedPublicIpRange& range, CString& strError)
	{
		strText.Trim();
		strError.Empty();
		if (strText.IsEmpty()) {
			strError = _T("empty range");
			return false;
		}

		CString strAddress(strText);
		unsigned uPrefix = 0;
		bool bHasPrefix = false;
		const int iSlash = strText.Find(_T('/'));
		if (iSlash >= 0) {
			strAddress = strText.Left(iSlash);
			CString strPrefix(strText.Mid(iSlash + 1));
			if (!TryParseUnsignedDecimal(strPrefix, uPrefix)) {
				strError.Format(_T("invalid prefix length in %s"), (LPCTSTR)strText);
				return false;
			}
			bHasPrefix = true;
		}

		CAddress address;
		if (!TryParseAddressLiteral(strAddress, address)) {
			strError.Format(_T("invalid IP address in %s"), (LPCTSTR)strText);
			return false;
		}
		const unsigned uMaxPrefix = address.GetType() == CAddress::IPv6 ? 128u : 32u;
		if (!bHasPrefix)
			uPrefix = uMaxPrefix;
		if (uPrefix > uMaxPrefix) {
			strError.Format(_T("invalid prefix length in %s"), (LPCTSTR)strText);
			return false;
		}
		if (!address.IsPublicIP() || !IsPublicIpRangeOnly(address, static_cast<uint8_t>(uPrefix))) {
			strError.Format(_T("range is not public IP: %s"), (LPCTSTR)strText);
			return false;
		}

		range.address = address;
		range.uPrefixLength = static_cast<uint8_t>(uPrefix);
		return true;
	}

	CStringA TrimAsciiWhitespace(CStringA strText)
	{
		strText.Trim(" \t\r\n");
		return strText;
	}

	bool TryParsePublicIpLiteral(CStringA strText, ADDRESS_FAMILY nFamily, CAddress& address, CStringA& strAddress)
	{
		strAddress.Empty();
		strText = TrimAsciiWhitespace(strText);
		if (strText.IsEmpty())
			return false;

		CAddress parsed;
		if (!parsed.FromString(CString(CA2T(strText)), false))
			return false;
		if ((nFamily == AF_INET && parsed.GetType() != CAddress::IPv4) || (nFamily == AF_INET6 && parsed.GetType() != CAddress::IPv6))
			return false;
		if (!parsed.IsPublicIP())
			return false;

		address = parsed;
		const CString strFormatted(parsed.ToStringC());
		strAddress = CStringA(CT2CA(strFormatted));
		return true;
	}

	bool TryParsePublicIpHttpResponse(CStringA strResponse, ADDRESS_FAMILY nFamily, CAddress& address, CStringA& strAddress)
	{
		strAddress.Empty();
		const int iHeaderEnd = strResponse.Find("\r\n\r\n");
		if (iHeaderEnd >= 0) {
			const int iStatusEnd = strResponse.Find("\r\n");
			const CStringA strStatusLine = iStatusEnd >= 0 ? strResponse.Left(iStatusEnd) : strResponse;
			if (strStatusLine.Find(" 200 ") < 0 && strStatusLine.Right(4) != " 200")
				return false;
			strResponse = strResponse.Mid(iHeaderEnd + 4);
		}
		return TryParsePublicIpLiteral(strResponse, nFamily, address, strAddress);
	}

	bool SendAll(SOCKET hSocket, const CStringA& strRequest, CString& strError)
	{
		const char* pszCursor = strRequest.GetString();
		int nRemaining = strRequest.GetLength();
		while (nRemaining > 0) {
			const int nSent = send(hSocket, pszCursor, nRemaining, 0);
			if (nSent <= 0) {
				strError.Format(_T("send failed (%d)"), WSAGetLastError());
				return false;
			}
			pszCursor += nSent;
			nRemaining -= nSent;
		}
		return true;
	}

	bool BindProbeSocket(SOCKET hSocket, ADDRESS_FAMILY nFamily, const SProbeContext& context, CString& strError)
	{
		const bool bHasBindInterface = context.dwIpv4IfIndex != 0 || context.dwIpv6IfIndex != 0;
		if (!bHasBindInterface)
			return true;

		int nBindInterfaceError = 0;
		if (nFamily == AF_INET) {
			if (!CNetBind::ApplyIpv4UnicastInterfaceOption(hSocket, nFamily, true, true, context.dwIpv4IfIndex, &nBindInterfaceError)) {
				strError.Format(_T("IP_UNICAST_IF failed (%d)"), nBindInterfaceError);
				return false;
			}
		}
		else if (nFamily == AF_INET6) {
			if (!CNetBind::ApplyIpv6UnicastInterfaceOption(hSocket, nFamily, true, true, context.dwIpv6IfIndex, &nBindInterfaceError)) {
				strError.Format(_T("IPV6_UNICAST_IF failed (%d)"), nBindInterfaceError);
				return false;
			}
		}
		else
			return true;

		if (context.strBindAddress.IsEmpty())
			return true;
		if ((nFamily == AF_INET && context.eBindFamily != CAddress::IPv4) || (nFamily == AF_INET6 && context.eBindFamily != CAddress::IPv6))
			return true;

		CAddress bindIp;
		if (!bindIp.FromString(CString(CA2T(context.strBindAddress.GetString())), false)) {
			strError.Format(_T("invalid bind address %hs"), context.strBindAddress.GetString());
			return false;
		}
		sockaddr_storage bindAddress = {};
		int iBindAddressLen = sizeof bindAddress;
		bindIp.ToSA(reinterpret_cast<sockaddr*>(&bindAddress), &iBindAddressLen, 0);
		if (bind(hSocket, reinterpret_cast<const sockaddr*>(&bindAddress), iBindAddressLen) == SOCKET_ERROR) {
			strError.Format(_T("bind failed (%d)"), WSAGetLastError());
			return false;
		}
		return true;
	}

	bool FetchProvider(const SPublicIpProbeProvider& provider, const SProbeContext& context, CStringA& strPublicAddress, CAddress& publicAddress, CString& strError)
	{
		strPublicAddress.Empty();
		strError.Empty();

		addrinfo hints = {};
		hints.ai_family = provider.nFamily;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		CAddrInfoHandle addresses;
		const int nLookup = getaddrinfo(provider.pszHost, "80", &hints, addresses.Out());
		if (nLookup != 0 || addresses.Get() == NULL) {
			strError.Format(_T("getaddrinfo failed (%d)"), nLookup);
			return false;
		}

		CString strLastError;
		for (addrinfo* pAddress = addresses.Get(); pAddress != NULL; pAddress = pAddress->ai_next) {
			CSocketHandle socketHandle;
			socketHandle.Reset(socket(pAddress->ai_family, pAddress->ai_socktype, pAddress->ai_protocol));
			if (!socketHandle) {
				strLastError.Format(_T("socket failed (%d)"), WSAGetLastError());
				continue;
			}

			setsockopt(socketHandle.Get(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&kProbeTimeoutMs), sizeof kProbeTimeoutMs);
			setsockopt(socketHandle.Get(), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&kProbeTimeoutMs), sizeof kProbeTimeoutMs);

			if (!BindProbeSocket(socketHandle.Get(), provider.nFamily, context, strLastError))
				continue;

			if (connect(socketHandle.Get(), pAddress->ai_addr, static_cast<int>(pAddress->ai_addrlen)) == SOCKET_ERROR) {
				strLastError.Format(_T("connect failed (%d)"), WSAGetLastError());
				continue;
			}

			CStringA strRequest;
			strRequest.Format(
				"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: eMuleAI-public-ip-probe\r\nAccept: text/plain\r\nConnection: close\r\n\r\n",
				provider.pszPath,
				provider.pszHost);
			if (!SendAll(socketHandle.Get(), strRequest, strLastError))
				continue;

			CStringA strResponse;
			char buffer[1024] = {};
			for (;;) {
				const int nReceived = recv(socketHandle.Get(), buffer, sizeof buffer, 0);
				if (nReceived == 0)
					break;
				if (nReceived < 0) {
					strLastError.Format(_T("recv failed (%d)"), WSAGetLastError());
					break;
				}
				if (static_cast<size_t>(strResponse.GetLength()) + static_cast<size_t>(nReceived) > kMaxProbeResponseBytes) {
					strLastError = _T("response too large");
					break;
				}
				strResponse.Append(buffer, nReceived);
			}
			if (TryParsePublicIpHttpResponse(strResponse, provider.nFamily, publicAddress, strPublicAddress))
				return true;
			if (strLastError.IsEmpty())
				strLastError = provider.nFamily == AF_INET6 ? _T("response did not contain a strict public IPv6 literal") : _T("response did not contain a strict public IPv4 literal");
		}

		strError = strLastError.IsEmpty() ? CString(_T("all address attempts failed")) : strLastError;
		return false;
	}

	bool BuildProbeContext(SProbeContext& context)
	{
		if (thePrefs.GetActiveBindResolveResult() != NBR_Resolved
			|| thePrefs.GetP2PBindAddrA() == NULL
			|| *thePrefs.GetP2PBindAddrA() == '\0')
			return false;

		context.strBindInterfaceName = thePrefs.GetActiveBindInterfaceName();
		context.strBindAddress = thePrefs.GetP2PBindAddrA();
		context.eBindFamily = thePrefs.GetActiveBindResolvedFamily();
		context.dwIpv4IfIndex = thePrefs.GetActiveBindIpv4IfIndex();
		context.dwIpv6IfIndex = thePrefs.GetActiveBindIpv6IfIndex();
		return context.eBindFamily == CAddress::IPv4 || context.eBindFamily == CAddress::IPv6;
	}

	void AddRef(SAsyncProbeState* pState)
	{
		pState->nRefs.fetch_add(1, std::memory_order_relaxed);
	}

	void Release(SAsyncProbeState* pState)
	{
		if (pState->nRefs.fetch_sub(1, std::memory_order_acq_rel) == 1)
			delete pState;
	}

	std::unique_ptr<SIpGuardPublicIpProbeResult> CreateProbeResult(const SAsyncProbeState& state)
	{
		std::unique_ptr<SIpGuardPublicIpProbeResult> pResult(new SIpGuardPublicIpProbeResult);
		pResult->bAttempted = true;
		pResult->uGeneration = state.uGeneration;
		pResult->strPurpose = state.strPurpose;
		pResult->strBindInterfaceName = state.context.strBindInterfaceName;
		pResult->strBindAddress = state.context.strBindAddress;
		pResult->dwBindInterfaceIndex = state.context.eBindFamily == CAddress::IPv6 ? state.context.dwIpv6IfIndex : state.context.dwIpv4IfIndex;
		return pResult;
	}

	void PostProbeResult(SAsyncProbeState* pState, std::unique_ptr<SIpGuardPublicIpProbeResult> pResult)
	{
		if (pState->hNotifyWnd != NULL && ::IsWindow(pState->hNotifyWnd)
			&& ::PostMessage(pState->hNotifyWnd, pState->uNotifyMessage, pResult->uGeneration, reinterpret_cast<LPARAM>(pResult.get())) != FALSE) {
			(void)pResult.release();
		}
	}

	UINT AFX_CDECL ProviderProbeThreadProc(LPVOID pvContext)
	{
		std::unique_ptr<SProviderProbeContext> pContext(static_cast<SProviderProbeContext*>(pvContext));
		SAsyncProbeState* pState = pContext->pState;

		CStringA strPublicAddress;
		CString strError;
		CAddress publicAddress;
		const bool bSuccess = FetchProvider(pContext->provider, pState->context, strPublicAddress, publicAddress, strError);

		if (bSuccess) {
			if (!pState->bCompleted.exchange(true, std::memory_order_acq_rel)) {
				std::unique_ptr<SIpGuardPublicIpProbeResult> pResult = CreateProbeResult(*pState);
				pResult->bSucceeded = true;
				pResult->strPublicAddress = strPublicAddress;
				pResult->publicAddress = publicAddress;
				pResult->eAddressFamily = publicAddress.GetType();
				pResult->uPublicAddress = publicAddress.GetType() == CAddress::IPv4 ? publicAddress.ToUInt32(false) : 0;
				pResult->strProviderUrl = CString(pContext->provider.pszUrl);
				pResult->dwBindInterfaceIndex = publicAddress.GetType() == CAddress::IPv6 ? pState->context.dwIpv6IfIndex : pState->context.dwIpv4IfIndex;
				PostProbeResult(pState, std::move(pResult));
			}
			DebugLog(_T("Network Guard public IP probe: family=%s provider=%s bindInterface=%s localBind=%hs publicIp=%hs"),
				pContext->provider.nFamily == AF_INET6 ? _T("IPv6") : _T("IPv4"),
				(LPCTSTR)CString(pContext->provider.pszUrl),
				(LPCTSTR)pState->context.strBindInterfaceName,
				pState->context.strBindAddress.GetString(),
				strPublicAddress.GetString());
			Release(pState);
			return 0;
		}

		CString strAttempt;
		strAttempt.Format(_T("%hs: %s"), pContext->provider.pszUrl, (LPCTSTR)strError);
		{
			std::lock_guard<std::mutex> lock(pState->mutex);
			if (!pState->strAttempts.IsEmpty())
				pState->strAttempts.Append(_T("; "));
			pState->strAttempts.Append(strAttempt);
		}
		if (pState->nRemaining.fetch_sub(1, std::memory_order_acq_rel) == 1
			&& !pState->bCompleted.exchange(true, std::memory_order_acq_rel)) {
			std::unique_ptr<SIpGuardPublicIpProbeResult> pResult = CreateProbeResult(*pState);
			{
				std::lock_guard<std::mutex> lock(pState->mutex);
				pResult->strAttempts = pState->strAttempts;
			}
			pResult->strError = pResult->strAttempts.IsEmpty() ? CString(_T("all public IP providers failed")) : pResult->strAttempts;
			DebugLogWarning(_T("Network Guard public IP probe failed: bindInterface=%s localBind=%hs attempts=%s"),
				(LPCTSTR)pState->context.strBindInterfaceName,
				pState->context.strBindAddress.GetString(),
				(LPCTSTR)pResult->strAttempts);
			PostProbeResult(pState, std::move(pResult));
		}
		Release(pState);
		return 1;
	}
}

LPCTSTR CIpGuard::GetModePreferenceText(EIpGuardMode eMode)
{
	return eMode == IpGuardModeBlock ? _T("Block") : _T("Off");
}

EIpGuardMode CIpGuard::ParseModePreferenceText(CString strText)
{
	strText.Trim();
	return strText.CompareNoCase(_T("Block")) == 0 ? IpGuardModeBlock : IpGuardModeOff;
}

bool CIpGuard::TryParseAllowedPublicIpRanges(CString strText, std::vector<SIpGuardAllowedPublicIpRange>& ranges, CString& strError)
{
	ranges.clear();
	strError.Empty();
	strText.Trim();
	if (strText.IsEmpty())
		return true;

	CString strToken;
	for (int i = 0; i <= strText.GetLength(); ++i) {
		const TCHAR ch = i < strText.GetLength() ? strText[i] : _T(',');
		if (ch == _T(',') || ch == _T(';') || ch == _T('\r') || ch == _T('\n') || ch == _T('\t') || ch == _T(' ')) {
			strToken.Trim();
			if (!strToken.IsEmpty()) {
				SIpGuardAllowedPublicIpRange range;
				if (!TryParseAllowedPublicIpRange(strToken, range, strError))
					return false;
				ranges.push_back(range);
				strToken.Empty();
			}
		} else
			strToken.AppendChar(ch);
	}

	return true;
}

bool CIpGuard::IsPublicIpAllowed(const CAddress& address, const std::vector<SIpGuardAllowedPublicIpRange>& ranges)
{
	for (const SIpGuardAllowedPublicIpRange& range : ranges) {
		if (address.GetType() == range.address.GetType() && PrefixEquals(address, range.address, range.uPrefixLength))
			return true;
	}
	return false;
}

CString CIpGuard::FormatAddress(const CAddress& address)
{
	if (address.GetType() == CAddress::None)
		return CString();
	return address.ToStringC();
}

static bool StartPublicIpProbeWithContext(const SProbeContext& context, HWND hNotifyWnd, UINT uNotifyMessage, uint32_t uGeneration, const CString& strPurpose, ADDRESS_FAMILY nFamily, CString& strError)
{
	size_t nProviderCount = 0;
	const SPublicIpProbeProvider* pProviders = GetPublicIpProbeProviders(nProviderCount);
	if (nProviderCount == 0) {
		strError = _T("no public IP providers are configured");
		return false;
	}

	long nSelected = 0;
	for (size_t i = 0; i < nProviderCount; ++i) {
		if (nFamily == AF_UNSPEC || pProviders[i].nFamily == nFamily)
			++nSelected;
	}
	if (nSelected == 0) {
		strError = _T("no matching public IP providers are configured");
		return false;
	}

	SAsyncProbeState* pState = new SAsyncProbeState;
	pState->hNotifyWnd = hNotifyWnd;
	pState->uNotifyMessage = uNotifyMessage;
	pState->uGeneration = uGeneration;
	pState->strPurpose = strPurpose;
	pState->context = context;
	pState->nRemaining = nSelected;

	long nStarted = 0;
	for (size_t i = 0; i < nProviderCount; ++i) {
		if (nFamily != AF_UNSPEC && pProviders[i].nFamily != nFamily)
			continue;

		std::unique_ptr<SProviderProbeContext> pProviderContext(new SProviderProbeContext);
		pProviderContext->pState = pState;
		pProviderContext->provider = pProviders[i];
		AddRef(pState);
		CWinThread* pThread = AfxBeginThread(ProviderProbeThreadProc, pProviderContext.get(), THREAD_PRIORITY_BELOW_NORMAL, 0, 0, NULL);
		if (pThread == NULL) {
			Release(pState);
			pState->nRemaining.fetch_sub(1, std::memory_order_acq_rel);
			{
				std::lock_guard<std::mutex> lock(pState->mutex);
				if (!pState->strAttempts.IsEmpty())
					pState->strAttempts.Append(_T("; "));
				pState->strAttempts.AppendFormat(_T("%hs: could not start worker"), pProviders[i].pszUrl);
			}
			continue;
		}
		(void)pProviderContext.release();
		++nStarted;
	}

	if (nStarted == 0) {
		strError = pState->strAttempts.IsEmpty() ? CString(_T("could not start public IP probe workers")) : pState->strAttempts;
		Release(pState);
		return false;
	}
	Release(pState);
	return true;
}

bool CIpGuard::StartPublicIpProbe(HWND hNotifyWnd, UINT uNotifyMessage, uint32_t uGeneration, const CString& strPurpose, ADDRESS_FAMILY nFamily, CString& strError)
{
	SProbeContext context;
	return StartPublicIpProbeWithContext(context, hNotifyWnd, uNotifyMessage, uGeneration, strPurpose, nFamily, strError);
}

bool CIpGuard::StartBoundPublicIpProbe(HWND hNotifyWnd, UINT uNotifyMessage, uint32_t uGeneration, const CString& strPurpose, ADDRESS_FAMILY nFamily, CString& strError)
{
	SProbeContext context;
	if (!BuildProbeContext(context)) {
		strError = _T("active bind interface is not resolved");
		return false;
	}
	ADDRESS_FAMILY nProbeFamily = nFamily;
	if (nProbeFamily == AF_UNSPEC)
		nProbeFamily = context.eBindFamily == CAddress::IPv6 ? AF_INET6 : AF_INET;
	return StartPublicIpProbeWithContext(context, hNotifyWnd, uNotifyMessage, uGeneration, strPurpose, nProbeFamily, strError);
}
